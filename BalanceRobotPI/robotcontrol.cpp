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

    initPid();

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

double RobotControl::getAggKp() const
{
    return aggKp;
}

void RobotControl::setAggKp(double newAggKp)
{
    aggKp = newAggKp;
}

double RobotControl::getAggKi() const
{
    return aggKi;
}

void RobotControl::setAggKi(double newAggKi)
{
    aggKi = newAggKi;
}

double RobotControl::getAggKd() const
{
    return aggKd;
}

void RobotControl::setAggKd(double newAggKd)
{
    aggKd = newAggKd;
}

double RobotControl::getAggAC() const
{
    return aggAC;
}

void RobotControl::setAggAC(double newAggAC)
{
    aggAC = newAggAC;
}

double RobotControl::getAggSD() const
{
    return aggSD;
}

void RobotControl::setAggSD(double newAggSD)
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

    errorAngle = 0.0;
    oldErrorAngle = 0.0;
    currentAngle = 0.0;
    currentGyro = 0.0;
    currentTemp = 0.0;
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

    SKp = 1.0;
    SKi = 0.5;
    SKd = 0.3;
    DataAvg[0]=0; DataAvg[1]=0; DataAvg[2]=0;
    mpu_test = false;

    pwm_l = 0;
    pwm_r = 0;

    aggKp = 8.0;
    aggKi = 0.4;
    aggKd = 0.6;
    aggSD = 4.0;
    aggAC = 7.5;//defaulf 1.0
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

void RobotControl::initPid()
{
    //Specify the links and initial tuning parameters
    balancePID = new PID(&Input, &Output, &targetAngle, aggKp, aggKi, aggKd, DIRECT);

    balancePID->SetMode(AUTOMATIC);
    balancePID->SetSampleTime(SAMPLE_TIME);
    balancePID->SetOutputLimits(-pwmLimit, pwmLimit);
    balancePID->Reset();

    qDebug("PID Setup ok.");
}

void RobotControl::correctSpeedDiff()
{
    errorSpeed = diffSpeed - lastSpeedError;

    speedAdjust = constrain(int((SKp * diffSpeed) + (SKi * diffAllSpeed) + (SKd * errorSpeed)), -pwmLimit, pwmLimit);
    lastSpeedError = diffSpeed;

}

void RobotControl::calculatePwm()
{
    diffSpeed = Speed_R + Speed_L;
    diffAllSpeed += diffSpeed;

    targetAngle = aggAC +  (needSpeed / 10);
    Input = currentAngle;
    //qDebug() << currentAngle;
    errorAngle = abs(targetAngle - Input); //distance away from setpoint

    float ftmp = 0;
    ftmp = (Speed_L + Speed_R) * 0.5;
    if( ftmp > 0)
        avgPosition = ftmp + 0.5;
    else
        avgPosition = ftmp - 0.5;

    addPosition += avgPosition;  //position
    addPosition = constrain(addPosition, -pwmLimit, pwmLimit);

    if (errorAngle <= 1.5)
    {   //we're close to setpoint, use conservative tuning parameters
        balancePID->SetTunings(aggKp/2, aggKi/2, aggKd/2);
    }
    else
    {   //we're far from setpoint, use aggressive tuning parameters
        balancePID->SetTunings(aggKp,   aggKi,  aggKd);
    }

    balancePID->Compute();

    pwm = -static_cast<int>(Output - (currentGyro + addPosition)*2.0 * aggKd * aggKi);

    if(needTurnR != 0 || needTurnL != 0)
    {
        diffSpeed = 0;
        diffAllSpeed = 0;
    }

    correctSpeedDiff() ;

    pwm_r =int(pwm - aggSD * speedAdjust - needTurnR);
    pwm_l =int(pwm + aggSD * speedAdjust - needTurnL);

    if( currentAngle > 45 || currentAngle < -45)
    {
        pwm_l = 0;
        pwm_r = 0;
    }

    Speed_L = 0;
    Speed_R = 0;
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
        pwm_r =- pwm_r;  //cchange to positive
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
    //qDebug("currentAngle: %.1f", currentAngle);
}


void RobotControl::run()
{
    timer = micros();

    while (!m_stop)
    {
        const std::lock_guard<std::mutex> lock(mutex_loop);

        mpu_test = gyroMPU->testConnection();
        if(!mpu_test)
            continue;

        calculateGyro();
        calculatePwm();
        controlRobot();

        QThread::usleep(10);
    }
}
