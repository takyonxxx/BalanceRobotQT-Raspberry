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

    aggKp = 12.0;
    aggKi = 8.0;
    aggKd = 0.3;
    aggSD = 4.0; //speed diff
    aggAC = 1.0; //angel correction
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

void RobotControl::correctSpeedDiff()
{
    errorSpeed = diffSpeed - lastSpeedError;
    speedAdjust = constrain(int((SKp * diffSpeed) + (SKi * diffSpeed) + (SKd * errorSpeed)), -pwmLimit, pwmLimit);
    lastSpeedError = diffSpeed;
}

void RobotControl::calculatePwm()
{

    if( currentAngle > 45 || currentAngle < -45)
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
        reset_timer_speed = true;
        timer_speed_total = 0.0;
        return;
    } 

    double dt_control = (double)(micros() - timer_speed) / 1000000; // Calculate delta time
    timer_speed = micros();

    if(dt_control > 0.1)
        return;

    if(reset_timer_speed)
    {
        timer_speed_total += dt_control;

        if (timer_speed_total >= 0.5)
        {
            reset_timer_speed = false;
            timer_speed_total = 0.0;
        }

        if (reset_timer_speed)
        {
            return;
        }
    }

    Input = currentAngle;
    targetAngle = 0.0;

    diffSpeed = Speed_R + Speed_L;
    diffAllSpeed += diffSpeed;

    if(diffSpeed == 0)
    {
        speedAdjust = 0;
        diffAllSpeed = 0;
    }

    correctSpeedDiff();

    //Set angle setpoint and compensate to reach equilibrium point
    anglePID.setSetpoint(targetAngle + aggAC);
    anglePID.setTunings(aggKp, aggKi, aggKd / 10);

    //Compute Angle PID (input is current angle)
    Output = anglePID.compute(Input);

    pwm = -static_cast<int>(Output) + needSpeed;

    pwm_r = int(pwm + aggSD * speedAdjust  + needTurnR);
    pwm_l = int(pwm + aggSD * speedAdjust  + needTurnL);

    if(needTurnR != 0 || needTurnL != 0)
    {
        diffSpeed = 0;
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

    qDebug() << pwm_r << pwm_l << diffSpeed << speedAdjust;

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
    //qDebug() << QString("currentAngle: %1").arg(QString::number(currentAngle, 'f', 1));
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
