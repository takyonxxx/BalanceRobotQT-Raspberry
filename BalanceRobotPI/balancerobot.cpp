#include "balancerobot.h"
#include "constants.h"

#define MPU6050_I2C_ADDRESS 0x68
#define RESTRICT_PITCH
BalanceRobot *BalanceRobot::theInstance_= nullptr;
static std::atomic<bool> m_MainEnableThread(false);

double RAD_TO_DEG = 57.2958;
static int Speed_L = 0;
static int Speed_R = 0;

BalanceRobot* BalanceRobot::getInstance()
{
    if (theInstance_ == nullptr)
    {
        theInstance_ = new BalanceRobot();
    }
    return theInstance_;
}

BalanceRobot::BalanceRobot(QObject *parent) : QObject(parent)
{   
    init();
}

BalanceRobot::~BalanceRobot()
{
    softPwmWrite(PWML, 0);
    softPwmWrite(PWMR, 0);
    delete gattServer;
    delete translator;
    delete gyroMPU;
}

void BalanceRobot::loadSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    aggKp = settings.value("aggKp", "").toString().toDouble();
    aggKi = settings.value("aggKi", "").toString().toDouble();
    aggKd = settings.value("aggKd", "").toString().toDouble();
    aggSD = settings.value("aggVs", "").toString().toDouble();
    aggAC = settings.value("angleCorrection", "").toString().toDouble();
}

void BalanceRobot::saveSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.setValue("aggKp", QString::number(aggKp));
    settings.setValue("aggKi", QString::number(aggKi));
    settings.setValue("aggKd", QString::number(aggKd));
    settings.setValue("aggVs", QString::number(aggSD));
    settings.setValue("angleCorrection", QString::number(aggAC));
}

void BalanceRobot::ResetValues()
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
    aggKd = 0.8;
    aggSD = 4.0;
    aggAC = 1.0;//defaulf 1.0
}

bool BalanceRobot::initGyroMeter()
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

void BalanceRobot::encodeL(void)
{
    if (digitalRead(SPD_PUL_L))
        Speed_L += 1;
    else
        Speed_L -= 1;

}

void BalanceRobot::encodeR(void)
{
    if (digitalRead(SPD_PUL_R))
        Speed_R += 1;
    else
        Speed_R -= 1;
}

bool BalanceRobot::initwiringPi()
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

void BalanceRobot::initPid()
{
    //Specify the links and initial tuning parameters
    balancePID = new PID(&Input, &Output, &targetAngle, aggKp, aggKi, aggKd, DIRECT);

    balancePID->SetMode(AUTOMATIC);
    balancePID->SetSampleTime(SAMPLE_TIME);
    balancePID->SetOutputLimits(-pwmLimit, pwmLimit);
    balancePID->Reset();

    qDebug("PID Setup ok.");
}

void BalanceRobot::correctSpeedDiff()
{
    errorSpeed = diffSpeed - lastSpeedError;

    speedAdjust = constrain(int((SKp * diffSpeed) + (SKi * diffAllSpeed) + (SKd * errorSpeed)), -pwmLimit, pwmLimit);
    lastSpeedError = diffSpeed;

}

void BalanceRobot::calculatePwm()
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

    pwm = -static_cast<int>(Output - (currentGyro + addPosition) * aggKd * aggKi);

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

        robotActive = false;

    }
    else if(!robotActive)
    {
        robotActive = true;
    }

    Speed_L = 0;
    Speed_R = 0;
}

void BalanceRobot::controlRobot()
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

void BalanceRobot::calculateGyro()
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

//slots

void BalanceRobot::onConnectionStatedChanged(bool state)
{
    if(state)
    {
        soundText = ("Bluetooth connection is succesfull.");
        speaker->speak(soundText);
    }
    else
    {
        soundText = ("Bluetooth connection lost.");
        speaker->speak(soundText);
    }
}

void BalanceRobot::createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result)
{
    uint8_t buffer[MaxPayload+8] = {'\0'};
    uint8_t command = msgId;

    int len = message.create_pack(rw , command , payload, buffer);

    for (int i = 0; i < len; i++)
    {
        result->append(buffer[i]);
    }
}

bool BalanceRobot::parseMessage(QByteArray *data, uint8_t &command, QByteArray &value,  uint8_t &rw)
{
    MessagePack parsedMessage;

    uint8_t* dataToParse = reinterpret_cast<uint8_t*>(data->data());
    QByteArray returnValue;

    if(message.parse(dataToParse, (uint8_t)data->length(), &parsedMessage))
    {
        command = parsedMessage.command;
        rw = parsedMessage.rw;

        for(int i = 0; i< parsedMessage.len; i++)
        {
            value.append(parsedMessage.data[i]);
        }

        return true;
    }

    return false;
}

void BalanceRobot::requestData(uint8_t command)
{
    QByteArray payload;
    QByteArray sendData;
    createMessage(command, mRead, payload, &sendData);
    gattServer->writeValue(sendData);
}

void BalanceRobot::sendData(uint8_t command, uint8_t value)
{
    QByteArray payload;
    payload[0] = value;

    QByteArray sendData;
    createMessage(command, mWrite, payload, &sendData);
    gattServer->writeValue(sendData);
}

void BalanceRobot::sendString(uint8_t command, QString value)
{
    QByteArray sendData;
    QByteArray bytedata;
    bytedata.append(value);
    createMessage(command, mWrite, bytedata, &sendData);
    gattServer->writeValue(sendData);
}

void BalanceRobot::onDataReceived(QByteArray data)
{
    uint8_t parsedCommand;
    uint8_t rw;
    QByteArray parsedValue;
    auto parsed = parseMessage(&data, parsedCommand, parsedValue, rw);

    if(!parsed)return;

    bool ok;
    int value =  parsedValue.toHex().toInt(&ok, 16);

    if(rw == mRead)
    {
        switch (parsedCommand)
        {
        case mPP: //Proportional
        {
            sendData(mPP, (int)aggKp);
            break;
        }
        case mPI: //Integral control
        {
            sendData(mPI, (int)10*aggKi);
            break;
        }
        case mPD: //Derivative constant
        {
            sendData(mPD, (int)10*aggKd);
            break;
        }
        case mAC://angle correction
        {
            sendData(mAC, (int)10*aggAC);
            break;
        }
        case mDS://speed diff constant wheel
        {
            sendData(mDS, (int)10*aggSD);
            break;
        }

        default:
            break;
        }
    }
    else if(rw == mWrite)
    {
        switch (parsedCommand)
        {
        case mPP:
        {
            aggKp = value;
            break;
        }
        case mPI:
        {
            aggKi = static_cast<float>(value / 10.0);
            break;
        }
        case mPD:
        {
            aggKd = static_cast<float>(value / 10.0);
            break;
        }
        case mAC:
        {
            aggAC = static_cast<float>(value / 10.0);
            break;
        }
        case mDS:
        {
            aggSD = static_cast<float>(value / 10.0);
            break;
        }
        case mSpeak:
        {
            soundText = QString(parsedValue.data());
            speaker->speak(soundText);
            break;
        }
        case mForward:
        {
            needSpeed = -1*value + 10;
            break;
        }
        case mBackward:
        {
            needSpeed = value - 10;
            break;
        }
        case mLeft:
        {
            needTurnL = value;
            break;
        }
        case mRight:
        {
            needTurnR = value;
            break;
        }
        default:
            break;
        }

        saveSettings();
    }

    auto pidInfo = QString("P:")
            + QString::number(aggKp, 'f', 1)
            + QString(" ")
            + QString("I:")
            + QString::number(aggKi, 'f', 1)
            + QString(" ")
            + QString("D:")
            + QString::number(aggKd, 'f', 1)
            + QString(" ")
            + QString("SD")
            + QString::number(aggSD, 'f', 1)
            + QString(" ")
            + QString("AC")
            + QString::number(aggAC, 'f', 1) ;

    sendString(mData, pidInfo);
}

void BalanceRobot::onCommandReceived(QString command)
{    
    if(networkRequest && !command.isEmpty())
    {
        qDebug() << "Voice received: " << command;        
        networkRequest->sendRequest(command);
    }
}

void BalanceRobot::recievedResponse(QString result)
{
    if (!result.isEmpty())
    {
        if (result.contains("no result"))
        {
             qDebug() << "No response.";
             translator->record();
             return;
        }

        soundText = result;

        qDebug() << "Response received: " << soundText;

        auto thread = QThread::create([this]{
            speaker->speak(soundText);
        });
        connect(thread,  &QThread::finished,  this,  [=]()
        {
            translator->record();
        });
        thread->start();
    }
}

//loops
void BalanceRobot::mainLoop()
{   
    while (m_MainEnableThread)
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

void BalanceRobot::init()
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

    execCommand((char*)"aplay r2d2.wav");    

    QString device, ip, mac, mask;

    while(ip.size() == 0)
    {
        this->getDeviceInfo(device, ip, mac, mask);
        QThread::msleep(10);
    }

    speaker = Speaker::getInstance();
    speaker->setLanguageCode(TR);

    gattServer = GattServer::getInstance();
    QObject::connect(gattServer, &GattServer::connectionState, this, &BalanceRobot::onConnectionStatedChanged);
    QObject::connect(gattServer, &GattServer::dataReceived, this, &BalanceRobot::onDataReceived);
    gattServer->startBleService();

    networkRequest = NetworkRequest::getInstance();
    connect(networkRequest, &NetworkRequest::sendResponse, this, &BalanceRobot::recievedResponse);

    for(int i=0; i<250; i++)
        calculateGyro();

    qDebug() << ip << mac;
    auto soundText = QString("Robot başlıyor.");
    speaker->speak(soundText);
    soundText = QString("Aypi adresi " + ip.replace(".", ", ") + ".");
    speaker->speak(soundText);

    translator = new AlsaTranslator(this);
    translator->setRecordDuration(2000);
    translator->setLanguageCode(TR);
    QObject::connect(translator, &AlsaTranslator::commandChanged, this, &BalanceRobot::onCommandReceived);
    this->translator->record();

    timer = micros();
    m_MainEnableThread = true;
    mainThread = std::thread(&BalanceRobot::mainLoop, this);
    mainThread.detach();
}
