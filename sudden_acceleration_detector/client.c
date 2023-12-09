#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <wiringPiSPI.h>
#include <math.h>
#define IN 0
#define OUT 1
#define PWM 0

#define LOW 0
#define HIGH 1
#define VALUE_MAX 256
#define DIRECTION_MAX 256
#define BUFFER_MAX 3

#define POUT 23
#define PIN 24

#define delay_ms delay
#define delay_us delayMicroseconds
#define VCC 5.0
#define R1 1000

//#define POUT 17

int sock;
int str_len;
struct sockaddr_in serv_addr;

int press;
int realPress;
int accelX;
int accelY;

char output[50];
void error_handling(char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
//I2C 초기화
void I2Cinit(int sensor) {
    wiringPiI2CWriteReg8(sensor, 0x2D, 0x08) ;
    wiringPiI2CWriteReg8(sensor, 0x2E, 0x80) ;
    wiringPiI2CWriteReg8(sensor, 0x31, 0x0B) ;
}

// SPI 초기화
void SPIinit() {
    wiringPiSPISetup(0, 1000000);
}

// fsr402 값 계산
double fsr420_Registor(double voltage) {
    double R = (R1 * VCC) / voltage - R1;
    return R;
}

//변화율 계산 함수
float calculateRate(short current, short previous, int deltaTime) {
    return (current - previous) / (float)deltaTime;
}
// MCP3008에서 fsr402 저항 값을 읽는다.
int readChannel(int channel) {
    unsigned char buffer[3];
    buffer[0] = 1;
    buffer[1] = (8 + channel) << 4;
    buffer[2] = 0;

    wiringPiSPIDataRW(0, buffer, 3);

    int data = ((buffer[1] & 3) << 8) + buffer[2];
    return data;
}
void functionAccel() {
    int sensorADXL;
    short x, y, z;
    short prevX, prevY, prevZ;
    int Time;

    // 측정 주기를 설정
    double delay = 1;

    wiringPiSetup();

    if ((sensorADXL = wiringPiI2CSetup(0x53)) == -1) {
        fprintf(stderr, "sensorADXL: Unable to initialise I2C: %s\n", strerror(errno));
    }


    I2Cinit(sensorADXL);//센서 초기화

    prevX = prevY = prevZ = 0;
    Time = 100; // 가속도 구할 시간 설정(m/s2)

    while (1) {
        //각각 x, y, z 축을 담당하는 회로 주소 연결
        x = (wiringPiI2CReadReg8(sensorADXL, 0x33) << 8) | wiringPiI2CReadReg8(sensorADXL, 0x32);
        y = (wiringPiI2CReadReg8(sensorADXL, 0x35) << 8) | wiringPiI2CReadReg8(sensorADXL, 0x34);


        // 변화율 계산 (현재값-이전값)/시간 간격
        float rateX = calculateRate(x, prevX, Time);
        float rateY = calculateRate(y, prevY, Time);

        // 변화율 일정값이상이면 실행
        //if (fabs(rateX) > 0.1||fabs(rateY)>0.1) 
        //{
        //    printf ("Accelerometer Value : X=%5d, Y=%5d\n", x, y) ;
        //    printf("Accelerometer Rate : X=%.2f, Y=%.2f\n", rateX, rateY);
        //}
        accelX = fabs(rateX);
        accelY = fabs(rateY);

        //현재값을 이전값으로 재설정
        prevX = x;
        prevY = y;
        prevZ = z;

        usleep(delay * 1000000);
    }
}
void functionPress(void) {
    wiringPiSetup();
    SPIinit();

    // MCP3008 0번 이용
    int mcp3008_channel = 0;

    // 측정 주기를 설정
    double delay = 1;


    while (1) {
        
        int pressValue = readChannel(mcp3008_channel);

        //printf("Press: %d\n", pressValue);
        press = pressValue;
        usleep(delay * 1000000);

    }
}

void functionCal()
{
  int count = 3;
  int flag = 1;
  printf("press : %d\n", press);
  while(1) {
    count = 3;
    flag = 1;
    if (press > 100 && accelX>0) {
      printf("press : %d\n", press);
      while(count) //압력이 들어오지 않거나 가속도가 줄면 급발진x
      {
        if(press<100) flag = 0;
        if(accelX==0) flag = 0;
        //if(accelY==0) flag = 0;
        printf("time : %d\n", count);
        printf("press : %d\n", press);
        printf("accelX : %d\n", accelX);
        realPress = press;
        usleep(1000000);
        count--;
      }

      if(flag ==1)
      {
        printf("infalg\n");
        
        snprintf(output, sizeof(output), "press : %d, accelX : %d, 급발진입니다.\n", realPress, accelX); //press : nnn accele : nnn 급발진입니다
        printf(output);
        int str_len = write(sock, output, sizeof(output));
        if (str_len < 0) printf("error\n");

      }
    }
  }
  
}
void *t_funAccel(void *data) {
    functionAccel();
}

void *t_funPress(void *data) {
    functionPress();
}

void *t_funCal(void *data)
{
  functionCal();
}


int main(int argc, char *argv[]) {

  char msg[2];
  char on[2] = "1";
  int light = 0;

  pthread_t p_thread[3];//쓰레드 2개 만들기
  int thr_id;
  int status;
  char p1[] = "thread_1";
  char p2[] = "thread_2";
  char p3[] = "thread_3";
  char pM[] = "thread_m";


  sock = socket(PF_INET, SOCK_STREAM, 0);//클라이언트 소켓 생성
  if (sock == -1) error_handling("socket() error");

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
  serv_addr.sin_port = htons(atoi(argv[2]));//서버에 연결

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
    error_handling("connect() error");

  printf("Connection established\n");

  //char buff[10] = {'\n'};
  //snprintf(buff, 10, "jksjdlasj\n");
  //write(sock, buff, sizeof(buff));



  thr_id = pthread_create(&p_thread[0], NULL, t_funAccel, (void *)p1);
  if (thr_id < 0) {
    perror("thread create error : ");
    exit(0);
  }

  thr_id = pthread_create(&p_thread[1], NULL, t_funPress, (void *)p2);
  if (thr_id < 0) {
    perror("thread create error : ");
    exit(0);
  }
  thr_id = pthread_create(&p_thread[2], NULL, t_funCal, (void *)p3);
  if (thr_id < 0) {
    perror("thread create error : ");
    exit(0);
  }

  if (argc != 3) {
    printf("Usage : %s <IP> <port>\n", argv[0]); //출력
    exit(1);
  }

  pthread_join(p_thread[0], (void **)&status);
  pthread_join(p_thread[1], (void **)&status);  
  pthread_join(p_thread[2], (void **)&status);
  close(sock);

  return (0);
}
