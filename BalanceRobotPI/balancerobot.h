#ifndef BALANCEROBOT_H
#define BALANCEROBOT_H
#include <QObject>
#include <QSettings>

#include <gattserver.h>

#include "pid.h"
#include "i2cdev.h"
#include "mpu6050.h"
#include "kalman.h"
#include <softPwm.h>
#include <wiringPi.h>
#include <alsa/asoundlib.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <iostream>
#include <message.h>

#define SLEEP_PERIOD 1000 //us;
#define SERIAL_TIME  100 //ms
#define SAMPLE_TIME  1//ms

//physcal pins
#define PWMR1  31
#define PWMR2  33
#define PWML1  38
#define PWML2  40
#define PWMR   32
#define PWML   37

//encoder define
#define SPD_INT_L 12   //interrupt R Phys:12
#define SPD_PUL_L 16   //Phys:16
#define SPD_INT_R 18   //interrupt L Phys:18
#define SPD_PUL_R 22   //Phys:22

class BalanceRobot : public QObject
{
    Q_OBJECT

public:
    explicit BalanceRobot(QObject *parent = nullptr);
    ~BalanceRobot();
    void init();

    static BalanceRobot* getInstance();
    static BalanceRobot *theInstance_;      

private slots:
    void onConnectionStatedChanged(bool state);
    void onDataReceived(QByteArray data);

private:

    bool initGyroMeter();
    bool initwiringPi();
    void initPid();
    void correctSpeedDiff();
    void calculatePwm();
    void calculateGyro();
    void controlRobot();
    void SetAlsaMasterVolume(long volume);
    void ResetValues();
    //void execCommand(const char* cmd);
    void createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result);
    void parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw);
    void requestData(uint8_t command);
    void sendData(uint8_t command, uint8_t value);
    static void* mainLoop(void* this_ptr);
    static void* speak(void* this_ptr);
    static void encodeL(void);
    static void encodeR(void);

    void loadSettings();
    void saveSettings();

    QString m_sSettingsFile;

    PID *balancePID;
    Message message;

    GattServer *gattServer;
    pthread_t mainThread;
    pthread_t soundhread;

    MPU6050 gyroMPU;
    Kalman kalmanX;
    Kalman kalmanY;

    std::string currentSound;
    QString soundFormat;
    QString soundText;

    double Input;
    double Output;
    double accX, accY, accZ;
    double gyroX, gyroY, gyroZ;
    double gyroXangle, gyroYangle; // Angle calculate using the gyro only
    double compAngleX, compAngleY; // Calculated angle using a complementary filter
    double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter
    double timeDiff;
    double angleCorrection;
    double aggVs; //Velocity wheel    
    double currentAngle;
    double currentGyro;
    double currentTemp;
    double errorAngle;
    double oldErrorAngle;
    double targetAngle;
    double aggKp;
    double aggKi;
    double aggKd;    
    double lastSpeedError;
    double speedAdjust;
    double errorSpeed;
    double SKp ,SKi ,SKd;
    double DataAvg[3];   
    int pwmLimit;
    int pwm, pwm_l, pwm_r;
    int needSpeed;
    int needTurnL;
    int needTurnR;
    int diffSpeed;
    int diffAllSpeed;
    int avgPosition;
    int addPosition;

    uint32_t timer;
    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    template<class T>
    const T& constrain(const T& x, const T& a, const T& b) {
        if(x < a) {
            return a;
        }
        else if(b < x) {
            return b;
        }
        else
            return x;
    }

    unsigned int millis()
    {
        struct timeval timer;
        gettimeofday(&timer, NULL);
        double time_in_mill = (timer.tv_sec) * 1000 + (timer.tv_usec) / 1000 ; // convert tv_sec & tv_usec to millisecond
        return (unsigned int)time_in_mill;
    }

    unsigned int micros()
    {
        struct timeval timer;
        gettimeofday(&timer, NULL);
        unsigned int time_in_micros = 1000000 * timer.tv_sec + timer.tv_usec;
        return time_in_micros;
    }

};

#endif // BALANCEROBOT_H
