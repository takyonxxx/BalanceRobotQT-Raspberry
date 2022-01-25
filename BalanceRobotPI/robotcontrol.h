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
    void correctSpeedDiff();
    void calculatePwm();
    void calculateGyro();
    void controlRobot();
    void ResetValues();
    void stop();

    static void encodeL(void);
    static void encodeR(void);

    void loadSettings();
    void saveSettings();

    float getAggKp() const;
    void setAggKp(float newAggKp);

    float getAggKi() const;
    void setAggKi(float newAggKi);

    float getAggKd() const;
    void setAggKd(float newAggKd);

    float getAggAC() const;
    void setAggAC(float newAggAC);

    float getAggSD() const;
    void setAggSD(float newAggSD);

    int getNeedSpeed() const;
    void setNeedSpeed(int newNeedSpeed);

    int getNeedTurnL() const;
    void setNeedTurnL(int newNeedTurnL);

    int getNeedTurnR() const;
    void setNeedTurnR(int newNeedTurnR);

private:

    PID anglePID;

    MPU6050 *gyroMPU{};

    QString m_sSettingsFile;
    std::mutex mutex_loop;
    bool m_stop{false};

    double RAD_TO_DEG = 57.2958;    

    float aggKp{0};
    float aggKi{0};
    float aggKd{0};
    float aggAC{0};
    float aggSD{0};

    int needSpeed{0};
    int needTurnL{0};
    int needTurnR{0};

    float Input{0};
    float Output{0};
    float accX, accY, accZ;
    float gyroX, gyroY, gyroZ;
    float gyroXangle, gyroYangle; // Angle calculate using the gyro only
    float compAngleX, compAngleY; // Calculated angle using a complementary filter
    float kalAngleX, kalAngleY; // Calculated angle using a Kalman filter
    float timeDiff{0};

    float currentAngle{0};
    float currentGyro{0};
    float targetAngle{0};
    float lastSpeedError{0};
    float speedAdjust{0};
    float errorSpeed{0};
    float SKp ,SKi ,SKd;
    float DataAvg[3];
    int pwmLimit{0};
    int pwm, pwm_l, pwm_r;

    int diffSpeed{0};
    int diffAllSpeed{0};
    int avgPosition{0};
    int addPosition{0};

    bool mpu_test{false};
    Kalman kalmanX{};
    Kalman kalmanY{};

    uint32_t timer{0};
    uint32_t timer_speed{0};
    double timer_speed_total{0};
    bool reset_timer_speed{false};

    int16_t ax, ay, az;
    int16_t gx, gy, gz;

    static RobotControl *theInstance_;

protected:
    void run() override;

};

#endif // ROBOTCONTROL_H
