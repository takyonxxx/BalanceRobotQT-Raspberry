#include "robotcontrol.h"
#include <cmath>

RobotControl *RobotControl::theInstance_ = nullptr;

RobotControl* RobotControl::getInstance()
{
    if (theInstance_ == nullptr)
    {
        theInstance_ = new RobotControl();
    }
    return theInstance_;
}

RobotControl::RobotControl(QObject *parent) : QThread(parent)
{
    ResetValues();

    m_sSettingsFile = QCoreApplication::applicationDirPath() + "/settings.ini";

    if (QFile(m_sSettingsFile).exists())
        loadSettings();
    else
        saveSettings();

    if(!initwiringPi()) return;
    if(!initGyroMeter()) return;

    // Initialize by taking multiple readings
    for(int i=0; i<255; i++)
        calculateGyro();
}

RobotControl::~RobotControl()
{
    m_stop = true;
    softPwmWrite(PWML, 0);
    softPwmWrite(PWMR, 0);
    delete gyroMPU;
}

void RobotControl::loadSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    aggKp = settings.value("aggKp", "").toString().toDouble();
    aggKi = settings.value("aggKi", "").toString().toDouble();
    aggKd = settings.value("aggKd", "").toString().toDouble();
    aggSD = settings.value("aggVs", "").toString().toDouble();
    aggAC = settings.value("angleCorrection", "").toString().toDouble();
}

void RobotControl::saveSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.setValue("aggKp", QString::number(aggKp));
    settings.setValue("aggKi", QString::number(aggKi));
    settings.setValue("aggKd", QString::number(aggKd));
    settings.setValue("aggVs", QString::number(aggSD));
    settings.setValue("angleCorrection", QString::number(aggAC));
}

bool RobotControl::initGyroMeter()
{
    qDebug("Initializing MPU6050 device...");

    gyroMPU = new MPU6050(MPU6050_I2C_ADDRESS);
    if(gyroMPU)
    {
        gyroMPU->initialize();
        while(!mpu_test)
        {
            mpu_test = gyroMPU->testConnection();
        }
    }

    qDebug(mpu_test? "MPU6050 connection successful" : "MPU6050 connection failed");
    return mpu_test;
}

void RobotControl::encodeL(void)
{
    if (digitalRead(SPD_PUL_L))
        Speed_L += 1;
    else
        Speed_L -= 1;
}

void RobotControl::encodeR(void)
{
    if (digitalRead(SPD_PUL_R))
        Speed_R += 1;
    else
        Speed_R -= 1;
}

bool RobotControl::initwiringPi()
{
    if (wiringPiSetupPhys() < 0)
    {
        fprintf(stderr, "Unable to setup wiringPiSetupGpio: %s\n", strerror(errno));
        return false;
    }

    // Set pin modes
    pinMode(PWML1, OUTPUT);
    pinMode(PWML2, OUTPUT);
    pinMode(PWMR1, OUTPUT);
    pinMode(PWMR2, OUTPUT);
    pinMode(PWML, OUTPUT);
    pinMode(PWMR, OUTPUT);

    // Initialize all pins to LOW
    digitalWrite(PWML1, LOW);
    digitalWrite(PWML2, LOW);
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, LOW);

    // Create PWM
    softPwmCreate(PWML, 0, pwmLimit);
    softPwmCreate(PWMR, 0, pwmLimit);

    // Setup speed sensors
    if (wiringPiISR(SPD_INT_L, INT_EDGE_FALLING, &encodeL) < 0)
    {
        fprintf(stderr, "Unable to setup ISR for left channel: %s\n", strerror(errno));
        return false;
    }

    if (wiringPiISR(SPD_INT_R, INT_EDGE_FALLING, &encodeR) < 0)
    {
        fprintf(stderr, "Unable to setup ISR for right channel: %s\n", strerror(errno));
        return false;
    }

    qDebug("WiringPi setup completed successfully");
    return true;
}

void RobotControl::correctSpeedDiff()
{
    errorSpeed = diffSpeed - lastSpeedError;

    // More conservative speed adjustment with stronger low-pass filter
    float newSpeedAdjust = constrain(int((SKp * diffSpeed) + (SKi * diffAllSpeed) + (SKd * errorSpeed)),
                                     -pwmLimit/2, pwmLimit/2);  // Reduced adjustment range

    speedAdjust = speedAdjust * 0.8f + newSpeedAdjust * 0.2f;  // Smoother transitions
    lastSpeedError = diffSpeed;
}

void RobotControl::stopMotors()
{
    // Immediately stop motors
    softPwmWrite(PWML, 0);
    softPwmWrite(PWMR, 0);

    // Set motor direction pins to stop
    digitalWrite(PWML1, LOW);
    digitalWrite(PWML2, LOW);
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, LOW);

    // Reset motor-related variables
    pwm = 0;
    pwm_l = 0;
    pwm_r = 0;
    diffSpeed = 0;
    diffAllSpeed = 0;
    speedAdjust = 0;
    Speed_L = 0;
    Speed_R = 0;
}

void RobotControl::stop()
{
    const std::lock_guard<std::mutex> lock(mutex_loop);

    // Set stop flag
    m_stop = true;

    // Immediately stop motors
    softPwmWrite(PWML, 0);
    softPwmWrite(PWMR, 0);

    // Set motor direction pins to stop
    digitalWrite(PWML1, LOW);
    digitalWrite(PWML2, LOW);
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, LOW);

    // Reset all control variables
    resetControlVariables();

    qDebug("Robot stopped due to excessive tilt angle or external stop request");
}

void RobotControl::resetControlVariables()
{
    pwm = 0;
    pwm_l = 0;
    pwm_r = 0;
    diffSpeed = 0;
    diffAllSpeed = 0;
    speedAdjust = 0;
    addPosition = 0;
    Speed_L = 0;
    Speed_R = 0;
    Input = 0;
    anglePID.resetIntegral();
}

void RobotControl::controlRobot()
{
    qDebug() << "PWM Values - Left:" << pwm_l << "Right:" << pwm_r;

    // Right motor control
    if (pwm_r > 0)
    {
        digitalWrite(PWMR1, HIGH);  // İleri
        digitalWrite(PWMR2, LOW);
        softPwmWrite(PWMR, pwm_r);
    }
    else if (pwm_r < 0)
    {
        digitalWrite(PWMR1, LOW);   // Geri
        digitalWrite(PWMR2, HIGH);
        softPwmWrite(PWMR, -pwm_r);
    }
    else
    {
        digitalWrite(PWMR1, LOW);   // Dur
        digitalWrite(PWMR2, LOW);
        softPwmWrite(PWMR, 0);
    }

    // Left motor control - Direction reversed compared to right motor
    if (pwm_l > 0)
    {
        digitalWrite(PWML1, LOW);    // İleri (ters)
        digitalWrite(PWML2, HIGH);
        softPwmWrite(PWML, pwm_l);
    }
    else if (pwm_l < 0)
    {
        digitalWrite(PWML1, HIGH);   // Geri (ters)
        digitalWrite(PWML2, LOW);
        softPwmWrite(PWML, -pwm_l);
    }
    else
    {
        digitalWrite(PWML1, LOW);    // Dur
        digitalWrite(PWML2, LOW);
        softPwmWrite(PWML, 0);
    }

    // qDebug() << "Motor Pins Status - PWML1:" << digitalRead(PWML1)
    //          << "PWML2:" << digitalRead(PWML2)
    //          << "PWMR1:" << digitalRead(PWMR1)
    //          << "PWMR2:" << digitalRead(PWMR2);
}

void RobotControl::calculatePwm()
{
    float accelAngle = atan2(accY, accZ) * RAD_TO_DEG;
    currentAngle = 0.96f * (currentAngle + gyroXrate * timeDiff) + 0.04f * accelAngle;

    if (std::abs(currentAngle) > 45.0f)
    {
        stopMotors();
        return;
    }

    if (std::abs(currentAngle) < 0.5f)
    {
        currentAngle = 0;
    }

    Input = currentAngle;
    targetAngle = -2.5f;

    diffSpeed = Speed_R - Speed_L;
    diffAllSpeed += diffSpeed;

    correctSpeedDiff();

    float angleError = std::abs(Input - targetAngle);
    float dynamicKp = aggKp + (angleError * 0.1f);
    float dynamicKi = (angleError < 5.0f) ? aggKi : 0;
    float dynamicKd = aggKd + (angleError * 0.05f);

    anglePID.setTunings(dynamicKp, dynamicKi, dynamicKd);
    Output = anglePID.compute(Input);

    // Orijinal PWM hesaplama mantığı
    pwm = -static_cast<int>(Output) + needSpeed;

    float turnFactor = 1.0f + std::abs(currentAngle) / 45.0f;
    int scaledTurnL = needTurnL * turnFactor;
    int scaledTurnR = needTurnR * turnFactor;

    pwm_r = constrain(int(pwm + 2 * scaledTurnR + aggSD * speedAdjust), -pwmLimit, pwmLimit);
    pwm_l = constrain(int(pwm + 2 * scaledTurnL - aggSD * speedAdjust), -pwmLimit, pwmLimit);

    // qDebug() << "Motor Values - Base PWM:" << pwm
    //          << "Speed Adjust:" << speedAdjust
    //          << "Final PWM L:" << pwm_l
    //          << "Final PWM R:" << pwm_r;

    if (needTurnR != 0 || needTurnL != 0)
    {
        diffSpeed = 0;
        diffAllSpeed = 0;
    }

    Speed_L = 0;
    Speed_R = 0;

    controlRobot();
}

void RobotControl::calculateGyro()
{
    timeDiff = (double)(micros() - timer)/1000000;
    timer = micros();

    gyroMPU->getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Debug sensor readings
    // qDebug() << "Raw Gyro - AX:" << ax << "AY:" << ay << "AZ:" << az
    //          << "GX:" << gx << "GY:" << gy << "GZ:" << gz;

    accX = static_cast<float>(ax);
    accY = static_cast<float>(ay);
    accZ = static_cast<float>(az);
    gyroX = static_cast<float>(gx);
    gyroY = static_cast<float>(gy);
    gyroZ = static_cast<float>(gz);

    gyroXrate = gyroX / 131.0;

    DataAvg[2] = DataAvg[1];
    DataAvg[1] = DataAvg[0];
    DataAvg[0] = currentAngle;

    currentAngle = (DataAvg[0] + DataAvg[1] + DataAvg[2])/3;
    currentGyro = gyroXrate;

    // Debug processed values
    // qDebug() << "Processed - Angle:" << currentAngle
    //          << "GyroRate:" << gyroXrate
    //          << "TimeDiff:" << timeDiff;
}

void RobotControl::ResetValues()
{
    Input = 0.0;
    timeDiff = 0.0;
    targetAngle = -2.5f;
    currentAngle = 0.0;
    currentGyro = 0.0;
    pwmLimit = 100;
    needSpeed = 0;
    needTurnL = 0;
    needTurnR = 0;
    diffSpeed = 0;
    diffAllSpeed = 0;
    avgPosition = 0;
    addPosition = 0;
    lastSpeedError = 0;
    speedAdjust = 0;
    errorSpeed = 0;

    // Starting PID values
    SKp = 3.5;
    SKi = 0.05;
    SKd = 0.4;

    DataAvg[0]=0; DataAvg[1]=0; DataAvg[2]=0;
    mpu_test = false;

    pwm = 0;
    pwm_l = 0;
    pwm_r = 0;

    // Starting with more conservative values
    aggKp = 5.0;
    aggKi = 0.4;
    aggKd = 0.2;
    aggSD = 0.8;
    aggAC = 3.0;

    gyroXrate = 0;

    qDebug() << "Values Reset - PID Values Kp:" << aggKp
             << "Ki:" << aggKi
             << "Kd:" << aggKd;
}

void RobotControl::run()
{
    timer = micros();

    while (!m_stop)
    {
        const std::lock_guard<std::mutex> lock(mutex_loop);
        calculateGyro();
        calculatePwm();
        QThread::msleep(5);
    }
}
