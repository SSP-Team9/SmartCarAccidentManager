#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPiI2C.h>
#include <wiringPi.h>
#include <time.h>

#include "led.h"
#include "lcd.h"

#define MAX_BUFFER_LEN 4096
#define true 1

/*
    SOCKET
*/

struct thread_data{
    int sock_fd;
};

int serv_sock, clnt_sock = -1;
struct sockaddr_in serv_addr, clnt_addr;
socklen_t clnt_addr_size;
int status;
int fd;

/*
    THREAD
*/

pthread_mutex_t lock;
pthread_t car_crash_thread;
pthread_t car_engine_thread;
pthread_t car_gps_thread;
pthread_t car_sudden_acceleration_thread;
int pi_number = 0;

char on[2] = "1";
char off[2] = "0";

/*
    BUFFER
*/

char car_crash_output[10];
char car_engine_output[10];
char car_gps_output[MAX_BUFFER_LEN];
char car_sudden_acceleration_output[100];

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

/*
    THREAD FUNCTIONS
*/

void *car_crash(void *data) {
    struct thread_data *client_data;
    pthread_detach(pthread_self());

    client_data = (struct thread_data*)data;

    int client_fd = client_data->sock_fd;

    if (GPIOExport(CAR_CRASH_POUT) == -1) {
        printf("error export\n");
    }

    if (GPIODirection(CAR_CRASH_POUT, OUT) == -1) {
        printf("error direction\n");
    }

    while(true) {
        // 읽어온 값이 1이라면, LED 점등
        int num = read(client_fd, car_crash_output, sizeof(car_crash_output));

        if (num == 0) {
            break;
        }

        if (strncmp(on, car_crash_output, 1) == 0) {
            GPIOWrite(CAR_CRASH_POUT, 1);
        } else if (strncmp(off, car_crash_output, 1) == 0) {
            GPIOWrite(CAR_CRASH_POUT, 0);
        }
    }

    printf("[fd = %d] Connection closed with socket fd number %d\n", client_fd, client_fd);
    GPIOWrite(CAR_ENGINE_POUT, 0);

    close(client_fd);
    free(client_data);

    pthread_mutex_lock(&lock);
    pi_number--;
    pthread_mutex_unlock(&lock);
}


void *car_engine(void *data) {
    struct thread_data *client_data;
    pthread_detach(pthread_self());

    client_data = (struct thread_data*)data;

    int client_fd = client_data->sock_fd;

    if (GPIOExport(CAR_ENGINE_POUT) == -1) {
        printf("error export\n");
    }

    if (GPIODirection(CAR_ENGINE_POUT, OUT) == -1) {
        printf("error direction\n");
    }

    while(true) {
        // 읽어온 값이 1이라면, LED 점등
        int num = read(client_fd, car_engine_output, sizeof(car_engine_output));

        if (num == 0) {
            break;
        }

        if (strncmp(on, car_engine_output, 1) == 0) {
            GPIOWrite(CAR_ENGINE_POUT, 1);
        } else if (strncmp(off, car_engine_output, 1) == 0) {
            GPIOWrite(CAR_ENGINE_POUT, 0);
        }
    }

    printf("[fd = %d] Connection closed with socket fd number %d\n", client_fd, client_fd);
    GPIOWrite(CAR_ENGINE_POUT, 0);

    close(client_fd);
    free(client_data);

    pthread_mutex_lock(&lock);
    pi_number--;
    pthread_mutex_unlock(&lock);
}

// gps 값 문자열로 받아서 lcd로 출력 or 콘솔에 출력 or led로 표시
void *car_gps(void *data) {
    struct thread_data *client_data;
    pthread_detach(pthread_self());

    client_data = (struct thread_data*)data;

    int client_fd = client_data->sock_fd;

    while(true) {
        int num = read(client_fd, car_gps_output, sizeof(car_gps_output));

        if (num == 0) {
            break;
        }

        // console에 출력
        printf("%s\n", car_gps_output);

        // 파일에 저장
        FILE* fp = fopen("gps.txt","a"); //txt파일을 a(append) 모드로 열기

        // 현재 시간 출력
        fputs("--------------------------------------------\n", fp);
        fputs("\n", fp);
        time_t seconds = time(NULL);
        fputs(ctime(&seconds), fp);
        fputs("\n", fp);

        // 위도, 경도 출력
        fputs(car_gps_output, fp);
        fputs("\n", fp);
        fputs("\n", fp);
        fputs("--------------------------------------------\n", fp);

        // 파일 닫기
        fclose(fp);

        lcdLoc(LINE1);
        typeln("Your current");
        lcdLoc(LINE2);
        typeln("location sent !!");
        delay(5000);
        ClrLcd();
    }

    printf("[fd = %d] Connection closed with socket fd number %d\n", client_fd, client_fd);

    close(client_fd);
    free(client_data);

    pthread_mutex_lock(&lock);
    pi_number--;
    pthread_mutex_unlock(&lock);
}

void *car_sudden_acceleration(void *data) {
    struct thread_data *client_data;
    pthread_detach(pthread_self());

    client_data = (struct thread_data*)data;

    int client_fd = client_data->sock_fd;

    while(true) {
        int num = read(client_fd, car_sudden_acceleration_output, sizeof(car_sudden_acceleration_output));

        if (num == 0) {
            break;
        }

        // console에 출력
        printf("%s\n", car_sudden_acceleration_output);

        // 파일에 저장
        FILE* fp = fopen("car_acceleration.txt","a"); //txt파일을 a(append) 모드로 열기

        // 현재 시간 출력
        fputs("--------------------------------------------\n", fp);
        fputs("\n", fp);
        time_t seconds = time(NULL);
        fputs(ctime(&seconds), fp);
        fputs("\n", fp);

        // 급발진 출력
        fputs(car_sudden_acceleration_output, fp);
        fputs("\n", fp);
        fputs("--------------------------------------------\n", fp);

        // 파일 닫기
        fclose(fp);

        lcdLoc(LINE1);
        typeln("****WARNING****");
        lcdLoc(LINE2);
        typeln("****************");
        delay(5000);
        ClrLcd();
    }

    printf("[fd = %d] Connection closed with socket fd number %d\n", client_fd, client_fd);

    close(client_fd);
    free(client_data);

    pthread_mutex_lock(&lock);
    pi_number--;
    pthread_mutex_unlock(&lock);
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Usage : %s <port>\n", argv[0]);
    }
    // socket 세팅
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1])); // IP주소 설정

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        error_handling("bind() error");
    }

    if (listen(serv_sock, 5) == -1) error_handling("listen() error");

    if (wiringPiSetup () == -1) exit (1);

    fd = wiringPiI2CSetup(I2C_ADDR);

    lcd_init(); // setup LCD

    //ClrLcd();
    lcdLoc(LINE1);
    typeln("Server Start !");
    lcdLoc(LINE2);
    typeln("Listening ... ");
    delay(3000);
    ClrLcd();

    while (true) {
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);

        // 클라이언트 번호 1 늘리기 -> 각 클라이언트 구분해서 처리하기 위함
        pi_number++;

        struct thread_data *data;
        data = (struct thread_data*)malloc(sizeof(struct thread_data));
        data->sock_fd = clnt_sock;

        if (clnt_sock == -1) error_handling("accept() error");

        printf("[*] Connection established with pi_number %d\n", pi_number);
        printf("[fd = %d] Socket fd number is %d\n", clnt_sock, clnt_sock);

        if (pi_number == 1) {
            pthread_create(&car_crash_thread, NULL, car_crash, (void*)data);
        } else if (pi_number == 2) {
            pthread_create(&car_engine_thread, NULL, car_engine, (void*)data);
        } else if (pi_number == 3) {
            pthread_create(&car_gps_thread, NULL, car_gps, (void*)data);
        } else if (pi_number == 4) {
            pthread_create(&car_sudden_acceleration_thread, NULL, car_sudden_acceleration, (void*)data);
        }
    }

    pthread_join(car_crash_thread, (void **)&status);
    pthread_join(car_engine_thread, (void **)&status);
    pthread_join(car_gps_thread, (void **)&status);
    pthread_join(car_sudden_acceleration_thread, (void **)&status);

    close(clnt_sock);
    close(serv_sock);

    return (0);
}

