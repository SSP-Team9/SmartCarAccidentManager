#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <wiringPi.h>
#include <stdint.h>

#define BUF_SIZE 1024

#define BUFFER_MAX 3
#define DIRECTION_MAX 256
#define VALUE_MAX 256

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1

// 초음파 센서(HC-SR04) GPIO 핀
#define POUT 23
#define PIN 24

#define MAX_TIMINGS 85
// 온습도센서(DHT-11) GPIO 핀
#define DHT_PIN 2

// 온습도 센서
int data[5] = { 0, 0, 0, 0, 0 };

// 소켓 통신에 사용할 쓰레드 인자 구조체
struct ThreadArgs {
    char* server_ip;
    int server_port;
    int socket_fd;
};

// *** GPIO 제어 함수 시작 ***
static int GPIOExport(int pin) {
  char buffer[BUFFER_MAX];
  ssize_t bytes_written;
  int fd;

  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open export for writing!\n");
    return (-1);
  }

  bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
  write(fd, buffer, bytes_written);
  close(fd);
  return (0);
}

static int GPIOUnexport(int pin) {
  char buffer[BUFFER_MAX];
  ssize_t bytes_written;
  int fd;

  fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open unexport for writing!\n");
    return (-1);
  }
  bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
  write(fd, buffer, bytes_written);
  close(fd);
  return (0);
}

static int GPIODirection(int pin, int dir) {
  static const char s_directions_str[] = "in\0out";
  char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
  int fd;

  snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

  fd = open(path, O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio direction for writing!\n");
    return (-1);
  }

  if (-1 ==
      write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
    fprintf(stderr, "Failed to set direction!\n");
    return (-1);
  }

  close(fd);
  return (0);
}

static int GPIORead(int pin) {
  char path[VALUE_MAX];
  char value_str[3];
  int fd;

  snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_RDONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio value for reading!\n");
    return (-1);
  }

  if (-1 == read(fd, value_str, 3)) {
    fprintf(stderr, "Failed to read value!\n");
    return (-1);
  }

  close(fd);

  return (atoi(value_str));
}

static int GPIOWrite(int pin, int value) {
  static const char s_values_str[] = "01";
  char path[VALUE_MAX];
  int fd;

  snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio value for writing!\n");
    return (-1);
  }

  if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
    fprintf(stderr, "Failed to write value!\n");
    return (-1);

    close(fd);
    return (0);
  }
}
// ***** GPIO 제어 함수 끝 ******

// 초음파 센서 제어 쓰레드 : 초음파 센서로부터 데이터를 읽고 거리값을 계산하여 서버로 전송
void *microwave_sensor_control(void *arg) {
    char notnorm[80] = "1";
    char norm[80] = "0";
    int send_len, bytes_sent;
    clock_t start_t, end_t;
    double time, distance;
    int i = 0;

    // 쓰레드 인자 구조체로부터 서버 정보 획득
    struct ThreadArgs *args = (struct ThreadArgs *)arg;

    // 소켓 생성
    int sock_send_local = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_send_local < 0) {
        printf("socket() failed\n");
        pthread_exit(NULL);
    }

    // 소켓 설정
    struct sockaddr_in addr_send_local;
    memset(&addr_send_local, 0, sizeof(addr_send_local));
    addr_send_local.sin_family = AF_INET;
    addr_send_local.sin_addr.s_addr = inet_addr(args->server_ip);
    addr_send_local.sin_port = htons(args->server_port);

    // 서버와 연결
    if (connect(sock_send_local, (struct sockaddr *)&addr_send_local, sizeof(addr_send_local)) < 0) {
        printf("connect() failed\n");
        close(sock_send_local);
        pthread_exit(NULL);
    }

    // GPIO 핀 활성화
    if (-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN)) {
        printf("gpio export err\n");
    }
    usleep(100000);

    // GPIO direction 설정
    if (-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN)) {
        printf("gpio direction err\n");
    }

    // 초음파 trigger init
    GPIOWrite(POUT, 0);
    usleep(10000);

    // 초음파 센서로 데이터 측정 시작
    do {
        if (-1 == GPIOWrite(POUT, 1)) {
        printf("gpio write/trigger err\n");
        }

        // 1sec == 1000000ultra_sec, 1ms = 1000ultra_sec
        usleep(10);
        GPIOWrite(POUT, 0);

        while (GPIORead(PIN) == 0) {
        start_t = clock();
        }
        while (GPIORead(PIN) == 1) {
        end_t = clock();
        }

        time = (double)(end_t - start_t) / CLOCKS_PER_SEC;  // ms
        distance = time / 2 * 34000;

        printf("distance : %.2lfcm\n", distance);

        // 거리 임계값(1m) 이하로 물체가 가까워지면 서버에게 "1" 전송
        if(distance < 100) {
            char buf[BUF_SIZE];
            strcpy(buf, notnorm);
            send_len = strlen(notnorm);
            bytes_sent = send(sock_send_local, buf, send_len, 0);
        }

        // 아니면 "0" 전송
        else {
            char buf[BUF_SIZE];
            strcpy(buf, norm);
            send_len = strlen(norm);
            bytes_sent = send(sock_send_local, buf, send_len, 0);
        }

        usleep(2000000);
    } while (1);

    close(args->socket_fd);
    pthread_exit(NULL);
}

// 온습도 센서 제어 쓰레드 : 센서로부터 데이터를 읽고 온도 값을 계산하여 서버로 전송
void *temperature_sensor_control(void *arg) {

    // 서버가 연결되는 소켓을 구분할 수 있게 소켓 통신의 연결 차이를 둠
    delay(2000);

    char notnorm[80] = "1";
    char norm[80] = "0";
    int send_len, bytes_sent;

    // 쓰레드 인자 구조체로부터 서버 정보 획득
    struct ThreadArgs *args = (struct ThreadArgs *)arg;

    // 소켓 생성
    int sock_send_local = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_send_local < 0) {
        printf("socket() failed\n");
        pthread_exit(NULL);
    }

    // 소켓 설정
    struct sockaddr_in addr_send_local;
    memset(&addr_send_local, 0, sizeof(addr_send_local));
    addr_send_local.sin_family = AF_INET;
    addr_send_local.sin_addr.s_addr = inet_addr(args->server_ip);
    addr_send_local.sin_port = htons(args->server_port);

    // 서버와 연결
    if (connect(sock_send_local, (struct sockaddr *)&addr_send_local, sizeof(addr_send_local)) < 0) {
        printf("connect() failed\n");
        close(sock_send_local);
        pthread_exit(NULL);
    }

    // 온습도 센서로 데이터 측정 시작
    while(1) {
        uint8_t laststate	= HIGH;
        uint8_t counter		= 0;
        uint8_t j			= 0, i;
        
        data[0] = data[1] = data[2] = data[3] = data[4] = 0;
      	pinMode( DHT_PIN, OUTPUT );
	    digitalWrite( DHT_PIN, LOW );
	    delay( 18 );

        pinMode( DHT_PIN, INPUT );

        for ( i = 0; i < MAX_TIMINGS; i++ )
        {
            counter = 0;
            while ( digitalRead( DHT_PIN ) == laststate )
            {
                counter++;
                delayMicroseconds( 1 );
                if ( counter == 255 )
                {
                    break;
                }
            }
            laststate = digitalRead( DHT_PIN );

            if ( counter == 255 )
                break;
        
            if ( (i >= 4) && (i % 2 == 0) )
            {
                data[j / 8] <<= 1;
                if ( counter > 32 )
                    data[j / 8] |= 1;
                j++;

                // 값 테스트
                // printf("Step %d - Counter: %d, Data: %d\n", i, counter, data[j / 8]);                                
            }

        }

        // 읽은 데이터를 이용하여 온도값 계산
        printf( "Temperature = %d.%d C\n", data[2], data[3]);

        // 온도 임계값(섭씨 30도) 이상으로 올라가면 서버로 "1" 전송
        if(data[2] >= 30) {
            char buf[BUF_SIZE];
            strcpy(buf, notnorm);
            send_len = strlen(notnorm);
            bytes_sent = send(sock_send_local, buf, send_len, 0);
        }

        // 아니면 "0" 전송
        else {
            char buf[BUF_SIZE];
            strcpy(buf, norm);
            send_len = strlen(norm);
            bytes_sent = send(sock_send_local, buf, send_len, 0);
        }

        delay(2000);
    }

    close(args->socket_fd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    pthread_t p_thread[2];
    int thr_id;
    int status;
    char p1[] = "microwave_control_thread";
    char p2[] = "temperature_control_thread";

    if (wiringPiSetupGpio() == -1) // wiringPi 라이브러리 init
    return -1;

     // 쓰레드 실행을 위한 쓰레드 인자 구조체 설정
    struct ThreadArgs args;
    if (argc < 3) {
        printf("Usage: %s <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    args.server_ip = argv[1];
    args.server_port = atoi(argv[2]);

    // 초음파 센서 제어 쓰레드 생성
    thr_id = pthread_create(&p_thread[0], NULL, microwave_sensor_control, (void *)&args);
    if(thr_id < 0) {
        perror("microwave thread creates error : ");
        exit(0);
    }

    // 온습도 센서 제어 쓰레드 생성
    thr_id = pthread_create(&p_thread[1], NULL, temperature_sensor_control, (void *)&args);
    if(thr_id < 0) {
        perror("temperature thread creates error : ");
        exit(0);
    }

    // 쓰레드들의 실행이 종료될 때 까지 대기
    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);

    close(args.socket_fd);

    return 0;
}
