#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gps.h>
#include <math.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define BUFFER_MAX 3
#define DIRECTION_MAX 256
#define VALUE_MAX 256

#define IN 0
#define OUT 1

#define LOW 0
#define HIGH 1

#define PIN 20
#define POUT 21

double latitude, longitude;

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

static int GPIOExport(int pin)
{
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open export for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIOUnexport(int pin)
{
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIODirection(int pin, int dir)
{
    static const char s_directions_str[] = "in\0out";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return (-1);
    }

    if (-1 ==
        write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3))
    {
        fprintf(stderr, "Failed to set direction!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

static int GPIORead(int pin)
{
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return (-1);
    }

    if (-1 == read(fd, value_str, 3))
    {
        fprintf(stderr, "Failed to read value!\n");
        return (-1);
    }

    close(fd);

    return (atoi(value_str));
}

static int GPIOWrite(int pin, int value)
{
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return (-1);
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1))
    {
        fprintf(stderr, "Failed to write value!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

void *send_to_socket(void *arg)
{
    int sock = *((int *)arg);

    int str_len;
    char buffer[100];

    // 형 변환
    snprintf(buffer, sizeof(buffer), "Latitude: %f, Longitude: %f", latitude, longitude);

    str_len = write(sock, buffer, sizeof(buffer));

    if (str_len == 0)
    {
        printf("[*] session closed\n");
        exit(0);
    }

    if (str_len == -1)
        error_handling("write() error");
}

int main(int argc, char *argv[])
{
    int sock;
    struct sockaddr_in serv_addr;
    int str_len;

    pthread_t p_thread[1];
    int thr_id;
    int status;

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }

    if (GPIOExport(POUT) == -1 || GPIOExport(PIN))
    {
        return 1;
    }

    if (GPIODirection(POUT, OUT) == -1 || GPIODirection(PIN, IN) == -1)
    {
        return 2;
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    printf("[*] now connected to %s:%s\n", argv[1], argv[2]);

    // GPS 데이터 구조체 및 변수 초기화
    struct gps_data_t gps_data;
    char buffer[4096];
    int rc;

    // GPSD에 연결
    if ((rc = gps_open("localhost", "2947", &gps_data)) == -1)
    {
        // GPSD 연결 실패 시 오류 메시지 출력
        printf("Error: %s\n", gps_errstr(rc));
        return EXIT_FAILURE;
    }

    gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

    while (true)
    {
        // GPS 데이터 읽기
        if (gps_waiting(&gps_data, 5000000))
        {
            if ((rc = gps_read(&gps_data, buffer, sizeof(buffer))) == -1)
            {
                printf("Error: %s\n", gps_errstr(rc));
            }
            else
            {
                // 유효한 데이터가 유효한 경우
                if ((gps_data.fix.mode == MODE_2D) &&
                    !isnan(gps_data.fix.latitude) && !isnan(gps_data.fix.longitude))
                {
                    // 위도와 경도를 전역 변수에 저장
                    latitude = gps_data.fix.latitude;
                    longitude = gps_data.fix.longitude;

                    printf("Latitude: %f, Longitude: %f\n", gps_data.fix.latitude, gps_data.fix.longitude);

                    // 위치 데이터가 유효한 경우 서버에 전송
                    if (latitude && longitude)
                    {
                        // GPIO 핀 상태 읽기 및 버튼 입력 감지
                        do
                        {
                            if (GPIOWrite(POUT, 1) == -1)
                            {
                                return 3;
                            }

                            printf("GPIORead: %d from pin %d\n", GPIORead(PIN), PIN);
                            usleep(1000 * 1000);
                            if (GPIORead(PIN) == LOW)
                            {
                                printf("버튼 눌림\n");

                                thr_id = pthread_create(&p_thread[0], NULL, send_to_socket, (void *)&sock);
                                if (thr_id < 0)
                                {
                                    perror("thread create error : ");
                                    exit(0);
                                }

                                pthread_join(p_thread[0], (void **)&status);

                                close(sock);
                                break;
                            }
                        } while (true);
                    }

                    break;
                }
                else
                {
                    // GPS 위치 데이터가 유효하지 않은 경우 메시지 출력
                    printf("No GPS fix available.\n");
                }
            }
        }

        sleep(3);
    }

    gps_stream(&gps_data, WATCH_DISABLE, NULL);
    gps_close(&gps_data);

    close(sock);

    if (GPIOUnexport(POUT) == -1 || GPIOUnexport(PIN) == -1)
    {
        return 4;
    }

    return EXIT_SUCCESS;
}
