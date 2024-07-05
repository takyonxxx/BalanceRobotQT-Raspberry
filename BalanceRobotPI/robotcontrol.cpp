#include "robotcontrol.h"

RobotControl *RobotControl::theInstance_= nullptr;


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

    qDebug() << aggKp << aggKi << aggKd << aggSD << aggAC;
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

float RobotControl::getAggKp() const
{
    return aggKp;
}

void RobotControl::setAggKp(float newAggKp)
{
    aggKp = newAggKp;
}

float RobotControl::getAggKi() const
{
    return aggKi;
}

void RobotControl::setAggKi(float newAggKi)
{
    aggKi = newAggKi;
}

float RobotControl::getAggKd() const
{
    return aggKd;
}

void RobotControl::setAggKd(float newAggKd)
{
    aggKd = newAggKd;
}

float RobotControl::getAggAC() const
{
    return aggAC;
}

void RobotControl::setAggAC(float newAggAC)
{
    aggAC = newAggAC;
}

float RobotControl::getAggSD() const
{
    return aggSD;
}

void RobotControl::setAggSD(float newAggSD)
{
    aggSD = newAggSD;
}

int RobotControl::getNeedSpeed() const
{
    return needSpeed;
}

void RobotControl::setNeedSpeed(int newNeedSpeed)
{
    needSpeed = newNeedSpeed;
}

int RobotControl::getNeedTurnL() const
{
    return needTurnL;
}

void RobotControl::setNeedTurnL(int newNeedTurnL)
{
    needTurnL = newNeedTurnL;
}

int RobotControl::getNeedTurnR() const
{
    return needTurnR;
}

void RobotControl::setNeedTurnR(int newNeedTurnR)
{
    needTurnR = newNeedTurnR;
}

void RobotControl::ResetValues()
{
    Input = 0.0;
    timeDiff = 0.0;
    targetAngle = 0.0;

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

    SKp = 4.5;
    SKi = 0.15;
    SKd = 0.3;
    DataAvg[0]=0; DataAvg[1]=0; DataAvg[2]=0;
    mpu_test = false;

    pwm_l = 0;
    pwm_r = 0;

    aggKp = 9.0;
    aggKi = 0.3;
    aggKd = 0.6;
    aggSD = 1.0;
    aggAC = 5.0; //angel correction

    gyroXrate = 0;
}

void RobotControl::stop()
{
    m_stop = true;
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

    // qDebug() << "Speed_L" << Speed_L;
}

void RobotControl::encodeR(void)
{
    if (digitalRead(SPD_PUL_R))
        Speed_R += 1;
    else
        Speed_R -= 1;

     // qDebug() << "Speed_R" << Speed_R;
}

bool RobotControl::initwiringPi()
{
    if (wiringPiSetupPhys () < 0)
    {
        fprintf (stderr, "Unable to setup wiringPiSetupGpio: %s\n", strerror (errno)) ;
        return false;
    }
    else
    {
        pinMode(PWML1, OUTPUT);
        pinMode(PWML2, OUTPUT);
        pinMode(PWMR1, OUTPUT);
        pinMode(PWMR2, OUTPUT);

        printf("Set Gpio PWM pinModes to output ok.\n");

        softPwmCreate(PWML,0,pwmLimit);
        softPwmCreate(PWMR,0,pwmLimit);

        if (wiringPiISR (SPD_INT_L, INT_EDGE_FALLING, &encodeL) < 0)
        {
            fprintf (stderr, "Unable to setup ISR for left channel: %s\n", strerror (errno));
            return false;
        }
        else
        {
            qDebug("Setup encodeL for left channel successful.");
        }

        if (wiringPiISR (SPD_INT_R, INT_EDGE_FALLING, &encodeR) < 0)
        {
            fprintf (stderr, "Unable to setup ISR for right channel: %s\n", strerror (errno));
            return false;
        }
        else
        {
            qDebug("Setup encodeR for right channel successful.");
        }

        softPwmWrite(PWML, 0);
        softPwmWrite(PWMR, 0);

        qDebug("wiringPi Setup ok.");
    }

    return true;
}

void RobotControl::correctSpeedDiff()
{
    errorSpeed = diffSpeed - lastSpeedError;
    // Low-pass filter for speed adjustment
    speedAdjust = speedAdjust * 0.7f + (constrain(int((SKp * diffSpeed) + (SKi * diffAllSpeed) + (SKd * errorSpeed)), -pwmLimit, pwmLimit)) * 0.3f;
    lastSpeedError = diffSpeed;
}

void RobotControl::calculatePwm()
{
    const float DEAD_ZONE = 0.5f;
    const float COMPLEMENTARY_FILTER_ALPHA = 0.98f;

    // Complementary filter for angle estimation
    float accelAngle = atan2(accY, accZ) * RAD_TO_DEG;
    currentAngle = COMPLEMENTARY_FILTER_ALPHA * (currentAngle + gyroXrate * timeDiff) + (1 - COMPLEMENTARY_FILTER_ALPHA) * accelAngle;

    if (std::abs(currentAngle) > ANGLE_LIMIT)
    {
        resetControlVariables();
        return;
    }

    // Dead zone implementation
    if (std::abs(currentAngle) < DEAD_ZONE)
    {
        currentAngle = 0;
    }

    Input = currentAngle;
    targetAngle = -2.5f;

    diffSpeed = Speed_R - Speed_L;
    diffAllSpeed += diffSpeed;    

    correctSpeedDiff();

    // Dynamic PID tuning based on angle error
    float angleError = std::abs(Input - targetAngle);
    float dynamicKp = aggKp + (angleError * 0.1f); // Increase Kp for larger errors
    float dynamicKi = (angleError < 5.0f) ? aggKi : 0; // Use Ki only for small errors
    float dynamicKd = aggKd + (angleError * 0.05f); // Increase Kd for larger errors

    anglePID.setTunings(dynamicKp, dynamicKi / 10, dynamicKd / 10);
    anglePID.setSetpoint(targetAngle + aggAC);

    // Compute Angle PID with anti-windup
    Output = anglePID.compute(Input);

    // Implement integral windup prevention
    if (std::abs(Output) >= pwmLimit)
    {
        anglePID.resetIntegral();
    }

    pwm = -static_cast<int>(Output) + needSpeed;

    // Apply non-linear scaling to turning
    float turnFactor = 1.0f + std::abs(currentAngle) / ANGLE_LIMIT;
    int scaledTurnL = needTurnL * turnFactor;
    int scaledTurnR = needTurnR * turnFactor;

    pwm_r = int(pwm + 2 * scaledTurnR + aggSD * speedAdjust);
    pwm_l = int(pwm + 2 * scaledTurnL - aggSD * speedAdjust);

    // Reset differential speed when turning
    if (needTurnR != 0 || needTurnL != 0)
    {
        diffSpeed = 0;
        diffAllSpeed = 0;
    }

    pwm_r = constrain(pwm_r, -pwmLimit, pwmLimit);
    pwm_l = constrain(pwm_l, -pwmLimit, pwmLimit);

    Speed_L = 0;
    Speed_R = 0;
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
    if (pwm_r>0)
    {
        digitalWrite(PWMR1, HIGH);
        digitalWrite(PWMR2, LOW);
    }

    if (pwm_l>0)
    {
        digitalWrite(PWML1, LOW);
        digitalWrite(PWML2, HIGH);
    }

    if (pwm_r<0)
    {
        digitalWrite(PWMR1, LOW);
        digitalWrite(PWMR2, HIGH);
        pwm_r = -pwm_r;
    }

    if (pwm_l<0)
    {
        digitalWrite(PWML1, HIGH);
        digitalWrite(PWML2, LOW);
        pwm_l = -pwm_l;
    }

    softPwmWrite(PWML, pwm_l);
    softPwmWrite(PWMR, pwm_r);
}

void RobotControl::calculateGyro()
{

    //timeDiff = (double)(micros() - timer)/1000000;
    double dt = (double)(micros() - timer) / 1000000; // Calculate delta time
    timer = micros();

    gyroMPU->getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    accX = (int16_t)(ax);
    accY = (int16_t)(ay);
    accZ = (int16_t)(az);

    gyroX = (int16_t)(gx);
    gyroY = (int16_t)(gy);
    gyroZ = (int16_t)(gz);

    gyroXrate = gyroX / 131.0;

    //qDebug("accX: %.1f  accY: %.1f accZ %.1f  gyroX: %.1f gyroY: %.1f  gyroZ: %.1f\n", accX, accY, accZ, gyroX, gyroY, gyroZ);

#ifdef RESTRICT_PITCH // Eq. 25 and 26
    double roll  = atan2(accY, accZ) * RAD_TO_DEG;
    double pitch = atan(-accX / sqrt(accY * accY + accZ * accZ)) * RAD_TO_DEG;
#else // Eq. 28 and 29
    double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
    double pitch = atan2(-accX, accZ) * RAD_TO_DEG;
#endif

    double gyroXrate = gyroX / 131.0; // Convert to deg/s
    double gyroYrate = gyroY / 131.0; // Convert to deg/s

#ifdef RESTRICT_PITCH
    // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
    if ((roll < -90 && kalAngleX > 90) || (roll > 90 && kalAngleX < -90)) {
        kalmanX.setAngle(roll);
        compAngleX = roll;
        kalAngleX = roll;
        gyroXangle = roll;
    } else
        kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter

    if (abs(kalAngleX) > 90)
        gyroYrate = -gyroYrate; // Invert rate, so it fits the restriced accelerometer reading
    kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt);
#else
    // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
    if ((pitch < -90 && kalAngleY > 90) || (pitch > 90 && kalAngleY < -90)) {
        kalmanY.setAngle(pitch);
        compAngleY = pitch;
        kalAngleY = pitch;
        gyroYangle = pitch;
    } else
        kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt); // Calculate the angle using a Kalman filter

    if (abs(kalAngleY) > 90)
        gyroXrate = -gyroXrate; // Invert rate, so it fits the restriced accelerometer reading

    kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter
#endif

    if (gyroXangle < -180 || gyroXangle > 180)
        gyroXangle = kalAngleX;
    if (gyroYangle < -180 || gyroYangle > 180)
        gyroYangle = kalAngleY;

    DataAvg[2] = DataAvg[1];
    DataAvg[1] = DataAvg[0];
    DataAvg[0] = kalAngleX;

    currentAngle = (DataAvg[0]+DataAvg[1]+DataAvg[2])/3;

    currentGyro = gyroXrate;
    //qDebug() << QString("currentAngle: %1").arg(QString::number(currentAngle, 'f', 1));
}


void RobotControl::run()
{
    timer = micros();

    while (!m_stop)
    {
        const std::lock_guard<std::mutex> lock(mutex_loop);

        // mpu_test = gyroMPU->testConnection();
        // if(!mpu_test)
        //     continue;

        calculateGyro();
        calculatePwm();
        controlRobot();

        QThread::msleep(5);
    }
}
