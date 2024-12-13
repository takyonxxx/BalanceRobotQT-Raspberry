#ifndef ROBOTCONTROL_H
#define ROBOTCONTROL_H

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QSettings>
#include "constants.h"
#include "pid.h"
#include "i2cdev.h"
#include "mpu6050.h"
#include "kalman.h"
#include <softPwm.h>
#include <wiringPi.h>
#include <mutex>

// Static variables for interrupt handlers
static volatile int32_t Speed_L = 0;
static volatile int32_t Speed_R = 0;

class RobotControl : public QThread {
    Q_OBJECT

public:
    explicit RobotControl(QObject *parent = nullptr);
    ~RobotControl();
    static RobotControl* getInstance();

    // Initialization methods
    bool initGyroMeter();
    bool initwiringPi();

    // Control methods
    void stop();
    static void encodeL();
    static void encodeR();

    // Settings management
    void loadSettings();
    void saveSettings();

    // Getters and setters
    float getAggKp() const { return aggKp; }
    void setAggKp(float newAggKp) { aggKp = newAggKp; }
    float getAggKi() const { return aggKi; }
    void setAggKi(float newAggKi) { aggKi = newAggKi; }
    float getAggKd() const { return aggKd; }
    void setAggKd(float newAggKd) { aggKd = newAggKd; }
    float getAggAC() const { return aggAC; }
    void setAggAC(float newAggAC) { aggAC = newAggAC; }
    float getAggSD() const { return aggSD; }
    void setAggSD(float newAggSD) { aggSD = newAggSD; }
    int getNeedSpeed() const { return needSpeed; }
    void setNeedSpeed(int newNeedSpeed) { needSpeed = newNeedSpeed; }
    int getNeedTurnL() const { return needTurnL; }
    void setNeedTurnL(int newNeedTurnL) { needTurnL = newNeedTurnL; }
    int getNeedTurnR() const { return needTurnR; }
    void setNeedTurnR(int newNeedTurnR) { needTurnR = newNeedTurnR; }

private:
    // Control algorithms
    void calculateGyro();
    void calculatePwm();
    void controlRobot();
    void correctSpeedDiff();
    void ResetValues();
    void resetControlVariables();
    void stopMotors();

    // Thread implementation
    void run() override;

    // Constants
    const double RAD_TO_DEG = 57.2958;
    int pwmLimit{0};  // Changed from const to regular member variable

    // Components
    PID anglePID;
    MPU6050* gyroMPU{nullptr};
    Kalman kalmanX;
    Kalman kalmanY;

    // File handling
    QString m_sSettingsFile;

    // Threading
    std::mutex mutex_loop;
    bool m_stop{false};
    bool mpu_test{false};

    // Control parameters
    float aggKp{9.0f};
    float aggKi{0.3f};
    float aggKd{0.6f};
    float aggAC{5.0f};
    float aggSD{1.0f};
    float SKp{4.5f};
    float SKi{0.15f};
    float SKd{0.3f};

    // Motion control
    int needSpeed{0};
    int needTurnL{0};
    int needTurnR{0};
    int pwm{0};
    int pwm_l{0};
    int pwm_r{0};

    // Sensor data and calculations
    float Input{0};
    float Output{0};
    int16_t ax{0}, ay{0}, az{0};  // Raw accelerometer data
    int16_t gx{0}, gy{0}, gz{0};  // Raw gyroscope data
    float accX{0}, accY{0}, accZ{0};  // Processed accelerometer data
    float gyroX{0}, gyroY{0}, gyroZ{0};  // Processed gyroscope data
    double gyroXrate{0};
    float currentAngle{0};
    float currentGyro{0};
    float targetAngle{0};
    double timeDiff{0};

    // Speed control
    float speedAdjust{0};
    float lastSpeedError{0};
    float errorSpeed{0};
    int32_t diffSpeed{0};
    int32_t diffAllSpeed{0};
    int avgPosition{0};
    int addPosition{0};

    // Moving average filter
    float DataAvg[3]{0};

    // Timing
    uint32_t timer{0};

    // Singleton instance
    static RobotControl* theInstance_;
};

#endif // ROBOTCONTROL_H
