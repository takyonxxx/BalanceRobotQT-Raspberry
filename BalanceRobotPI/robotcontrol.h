#ifndef ROBOTCONTROL_H
#define ROBOTCONTROL_H

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include "constants.h"

#include "pid.h"
#include "i2cdev.h"
#include "mpu6050.h"
#include "kalman.h"
#include <softPwm.h>
#include <wiringPi.h>

static int Speed_L = 0;
static int Speed_R = 0;

class RobotControl: public QThread
{
    Q_OBJECT

public:
    explicit RobotControl(QObject *parent = nullptr);
    ~RobotControl();

    static RobotControl* getInstance();

    bool initGyroMeter();
    bool initwiringPi();
    void initPid();
    void correctSpeedDiff();
    void calculatePwm();
    void calculateGyro();
    void controlRobot();
    void ResetValues();

    static void encodeL(void);
    static void encodeR(void);

    void loadSettings();
    void saveSettings();

    double getAggKp() const;
    void setAggKp(double newAggKp);

    double getAggKi() const;
    void setAggKi(double newAggKi);

    double getAggKd() const;
    void setAggKd(double newAggKd);

    double getAggAC() const;
    void setAggAC(double newAggAC);

    double getAggSD() const;
    void setAggSD(double newAggSD);

    int getNeedSpeed() const;
    void setNeedSpeed(int newNeedSpeed);

    int getNeedTurnL() const;
    void setNeedTurnL(int newNeedTurnL);

    int getNeedTurnR() const;
    void setNeedTurnR(int newNeedTurnR);

private:

    PID *balancePID;
    MPU6050 *gyroMPU{};

    QString m_sSettingsFile;
    std::mutex mutex_loop;
    bool m_stop{false};

    double RAD_TO_DEG = 57.2958;    

    double aggKp;
    double aggKi;
    double aggKd;
    double aggAC;
    double aggSD;

    int needSpeed;
    int needTurnL;
    int needTurnR;

    double Input;
    double Output;
    double accX, accY, accZ;
    double gyroX, gyroY, gyroZ;
    double gyroXangle, gyroYangle; // Angle calculate using the gyro only
    double compAngleX, compAngleY; // Calculated angle using a complementary filter
    double kalAngleX, kalAngleY; // Calculated angle using a Kalman filter
    double timeDiff;

    double currentAngle{90};
    double currentGyro;
    double currentTemp;
    double errorAngle;
    double oldErrorAngle;
    double targetAngle;    
    double lastSpeedError;
    double speedAdjust;
    double errorSpeed;
    double SKp ,SKi ,SKd;
    double DataAvg[3];
    int pwmLimit;
    int pwm, pwm_l, pwm_r;

    int diffSpeed;
    int diffAllSpeed;
    int avgPosition;
    int addPosition;

    bool mpu_test{false};
    Kalman kalmanX{};
    Kalman kalmanY{};

    uint32_t timer;
    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    static RobotControl *theInstance_;

protected:
    void run() override;

};

#endif // ROBOTCONTROL_H
