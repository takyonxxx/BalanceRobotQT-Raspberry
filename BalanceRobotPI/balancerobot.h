#ifndef BALANCEROBOT_H
#define BALANCEROBOT_H
#include <QObject>
#include <QSettings>
#include <QNetworkInterface>

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
#include "constants.h"
#include "alsadevices.h"


#define SLEEP_PERIOD 1000 //us;s
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
#define SPD_INT_R 12   //interrupt R Phys:12
#define SPD_PUL_R 16   //Phys:16
#define SPD_INT_L 18   //interrupt L Phys:18
#define SPD_PUL_L 22   //Phys:22

#define Ref_Meters		180.0		// Reference for meters in adaptive predictive control
#define NL				0.005		// Noise Level for Adaptive Mechanism.
#define GainA 			0.6			// Gain for Adaptive Mechanism A
#define GainB 			0.6			// Gain for Adaptive Mechanism B
#define PmA				2			// Delay Parameters a
#define PmB				2			// Delay Parameters b
#define nCP				9.0			// Conductor block periods control for rise to set point ts = n * CP
#define hz				5			// Prediction Horizon (Horizon max = n + 2)
#define UP_Roll			800.0		// Upper limit out
#define UP_Yaw			150.0		// Upper limit out
#define GainT_Roll		12.0		// Total Gain Roll Out Controller
#define GainT_Yaw		5.0			// Total Gain Yaw Out Controller
#define MaxOut_Roll		UP_Roll/GainT_Roll
#define MaxOut_Yaw		UP_Yaw/GainT_Yaw

#define Kp_ROLLPITCH 0.2		// Pitch&Roll Proportional Gain
#define Ki_ROLLPITCH 0.000001	// Pitch&Roll Integrator Gain

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
    void ResetValues();
    //void execCommand(const char* cmd);
    void createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result);
    bool parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw);
    void requestData(uint8_t command);
    void sendData(uint8_t command, uint8_t value);
    void sendString(uint8_t command, QString value);
    static void* mainLoop(void* this_ptr);
    static void* speakTr(void* this_ptr);
    static void* speakEn(void* this_ptr);
    static void encodeL(void);
    static void encodeR(void);

    void loadSettings();
    void saveSettings();

    QString m_sSettingsFile;
    ALSAPCMDevice *alsa_device{};

    PID *balancePID;
    Message message;

    GattServer *gattServer;
    pthread_t mainThread;
    pthread_t soundhread;

    MPU6050 *gyroMPU{};
    bool mpu_test{false};
    Kalman kalmanX{};
    Kalman kalmanY{};

    std::string currentSound;
    QString soundFormatTr;
    QString soundFormatEn;
    QString soundText;
    bool robotActive{true};
    bool soundActive{false};

    double Input;
    double Output;
    double accX, accY, accZ;
    double gyroX, gyroY, gyroZ;
    double gyroXangle, gyroYangle; // Angle calculate using the gyro only
    double compAngleX, compAngleY; // Calculated angle using a complementary filter
    double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter
    double timeDiff;
    double aggAC;
    double aggSD; //Velocity wheel
    double currentAngle{90};
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

    QStringList keyList{};

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

    void getDeviceInfo (QString &device, QString &ip, QString &mac, QString &mask)
    {

        bool found = false;
        foreach(QNetworkInterface interface, QNetworkInterface::allInterfaces())
        {
            unsigned int flags = interface.flags();
            bool isLoopback = (bool)(flags & QNetworkInterface::IsLoopBack);
            bool isP2P = (bool)(flags & QNetworkInterface::IsPointToPoint);
            bool isRunning = (bool)(flags & QNetworkInterface::IsRunning);
            if ( !isRunning ) continue;
            if ( !interface.isValid() || isLoopback || isP2P ) continue;

            foreach (QNetworkAddressEntry entry, interface.addressEntries())
            {
                // Ignore local host
                if ( entry.ip() == QHostAddress::LocalHost ) continue;

                // Ignore non-ipv4 addresses
                if ( !entry.ip().toIPv4Address() ) continue;

                if ( !found && interface.hardwareAddress() != "00:00:00:00:00:00" && entry.ip().toString().contains(".")
                     && !interface.humanReadableName().contains("VM"))
                {
                    device = interface.humanReadableName();
                    ip = entry.ip().toString();
                    mac = interface.hardwareAddress();
                    mask =  entry.netmask().toString();
                    found = true;
                }
            }
        }
    }

private slots:
    void decodedSpeech(QString speech);

};

#endif // BALANCEROBOT_H
