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
    int pulValue = digitalRead(SPD_PUL_L);
    if (pulValue)
        Speed_L += 1;
    else
        Speed_L -= 1;
}

void RobotControl::encodeR(void)
{
    int pulValue = digitalRead(SPD_PUL_R);
    if (pulValue)
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

    // In your initialization code
    pinMode(SPD_PUL_L, INPUT);
    pinMode(SPD_PUL_R, INPUT);
    pullUpDnControl(SPD_PUL_L, PUD_UP); // Enable pull-up resistor
    pullUpDnControl(SPD_PUL_R, PUD_UP); // Enable pull-up resistor

    // Setup speed sensors
    if (wiringPiISR(SPD_INT_L, INT_EDGE_BOTH, &encodeL) < 0)
    {
        fprintf(stderr, "Unable to setup ISR for left channel: %s\n", strerror(errno));
        return false;
    }

    if (wiringPiISR(SPD_INT_R, INT_EDGE_BOTH, &encodeR) < 0)
    {
        fprintf(stderr, "Unable to setup ISR for right channel: %s\n", strerror(errno));
        return false;
    }

    qDebug("WiringPi setup completed successfully");
    return true;
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
    anglePID.reset();
}

void RobotControl::calculatePwm()
{
    float accelAngle = atan2(accY, accZ) * RAD_TO_DEG;

    // Sensör füzyonu
    const float gyroWeight = 0.98f;
    currentAngle = gyroWeight * (currentAngle + gyroXrate * timeDiff) + (1.0f - gyroWeight) * accelAngle;

    // Aşırı açıları kontrol et
    if (std::abs(currentAngle) > 45.0f)
    {
        stopMotors();
        return;
    }

    Input = currentAngle;
    targetAngle = 2*aggAC;   // Hedef açı

    correctSpeedDiff();
    // Dinamik PID ayarları
    float angleError = std::abs(Input - targetAngle);
    float dynamicKp = aggKp + (angleError * 0.05f);
    float dynamicKi = (angleError < 8.0f) ? aggKi : 0;
    float dynamicKd = aggKd + (angleError * 0.01f);

    // PID değerlerini uygula
    anglePID.setTunings(dynamicKp, dynamicKi, dynamicKd);
    Output = anglePID.compute(Input);
    // PWM hesaplama
    pwm = -static_cast<int>(Output) + needSpeed * 1.5f;

    // Dönüş faktörü hesaplama
    float turnFactor = 1.0f + std::abs(currentAngle) / 60.0f;
    int scaledTurnL = needTurnL * turnFactor;
    int scaledTurnR = needTurnR * turnFactor;

    // Hız düzeltme faktörü
    float speedAdjustFactor = aggSD * 0.8f;

    // Açıya bağlı motor dengesi düzeltme faktörleri - iyileştirilmiş faktörler
    float leftFactor = 1.0f;
    float rightFactor = 1.0f;

    // Final PWM hesaplama - açıya bağlı faktörleri uygula
    int baseR = pwm + 2 * scaledTurnR;
    int baseL = pwm + 2 * scaledTurnL;

    // Bu kalibrasyon faktörlerini sınıfınıza ekleyin
    float leftMotorCalibration = 1.0f;
    float rightMotorCalibration = 1.0f;  // Sağ motor gücünü %20 arttırın

    // Sonra calculatePwm() içinde uygulayın
    int pwmR = static_cast<int>(rightFactor * rightMotorCalibration * (baseR + static_cast<int>(speedAdjustFactor * speedAdjust / 2)));
    int pwmL = static_cast<int>(leftFactor * leftMotorCalibration * (baseL - static_cast<int>(speedAdjustFactor * speedAdjust / 2)));

    // PWM değerlerini sınırla
    if (pwmR > pwmLimit) pwmR = pwmLimit;
    if (pwmR < -pwmLimit) pwmR = -pwmLimit;
    if (pwmL > pwmLimit) pwmL = pwmLimit;
    if (pwmL < -pwmLimit) pwmL = -pwmLimit;

    pwm_r = pwmR;
    pwm_l = pwmL;

    // Dönüş komutu varsa hız farkını sıfırla
    if (needTurnR != 0 || needTurnL != 0)
    {
        diffSpeed = 0;
        diffAllSpeed = 0;
    }

    // Motor kontrolünü çağır
    controlRobot();

    Speed_L = 0;
    Speed_R = 0;
}

void RobotControl::correctSpeedDiff()
{
    // Calculate current speed difference
    diffSpeed = Speed_R - Speed_L;

    // Update total speed counters
    totalSpeedL += Speed_L;
    totalSpeedR += Speed_R;

    // Calculate the accumulated speed difference
    int totalDiff = totalSpeedR - totalSpeedL;

    // Prevent excessive accumulation
    if (abs(totalSpeedL) > 10000 || abs(totalSpeedR) > 10000) {
        totalSpeedL /= 2;
        totalSpeedR /= 2;
    }

    // Calculate error change
    errorSpeed = diffSpeed - lastSpeedError;

    // Limit integral term accumulation
    diffAllSpeed += diffSpeed;
    if (diffAllSpeed > 5000) diffAllSpeed = 5000;
    if (diffAllSpeed < -5000) diffAllSpeed = -5000;

    // PID-based speed correction - MUCH GENTLER VALUES
    float newSpeedAdjust = (0.5f * diffSpeed) +        // Reduced from 5.0 to 0.5
                           (0.01f * diffAllSpeed) +     // Reduced from 0.05 to 0.01
                           (0.2f * errorSpeed) +        // Reduced from 1.0 to 0.2
                           (0.005f * totalDiff);        // Reduced from 0.02 to 0.005

    // Much slower transition
    speedAdjust = speedAdjust * 0.9f + newSpeedAdjust * 0.1f;  // More dampening

    // Limit correction range - more conservative
    if (speedAdjust > pwmLimit/4) speedAdjust = pwmLimit/4;     // Reduced from /2 to /4
    if (speedAdjust < -pwmLimit/4) speedAdjust = -pwmLimit/4;   // Reduced from /2 to /4

    // Save error for next iteration
    lastSpeedError = diffSpeed;
}

void RobotControl::controlRobot()
{
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

    // Left motor control - Sol motor ters monte edilmiş olabilir
    if (pwm_l > 0)
    {
        digitalWrite(PWML1, LOW);    // İleri (ters monte edilmiş)
        digitalWrite(PWML2, HIGH);
        softPwmWrite(PWML, pwm_l);
    }
    else if (pwm_l < 0)
    {
        digitalWrite(PWML1, HIGH);   // Geri (ters monte edilmiş)
        digitalWrite(PWML2, LOW);
        softPwmWrite(PWML, -pwm_l);
    }
    else
    {
        digitalWrite(PWML1, LOW);    // Dur
        digitalWrite(PWML2, LOW);
        softPwmWrite(PWML, 0);
    }
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
    // qDebug() << "Processed - Angle:" << currentAngle;
}

void RobotControl::ResetValues()
{
    Input = 0.0;
    timeDiff = 0.0;
    targetAngle = -2.5f;
    currentAngle = 0.0;
    currentGyro = 0.0;
    pwmLimit = 255;
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

    DataAvg[0]=0; DataAvg[1]=0; DataAvg[2]=0;
    mpu_test = false;

    pwm = 0;
    pwm_l = 0;
    pwm_r = 0;

    aggAC = 3.0;

    SKp = 2.5;
    SKi = 0.03;
    SKd = 0.3;

    // Ana PID değerleri
    aggKp = 8.0;
    aggKi = 0.6;
    aggKd = 0.4;
    aggSD = 0.6;

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
        QThread::msleep(1);
    }
}
