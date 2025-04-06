#include "robotcontrol.h"
#include <cmath>
#include <algorithm>

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

    // Robotun başlangıçta dik olduğunu varsayarak kalibrasyon yap
    for(int i=0; i<100; i++) {
        calculateGyro();
        QThread::msleep(5); // Örnekler arasında kısa beklemeler
    }

    // İlk açı değerini hemen ayarla - ilk ivmeölçer verisini doğrudan kullan
    float initialAccelAngle = atan2(accY, accZ) * RAD_TO_DEG;
    currentAngle = initialAccelAngle;
    DataAvg[0] = DataAvg[1] = DataAvg[2] = currentAngle;

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
    // int pulValue = digitalRead(SPD_INT_L);
    // if (pulValue)
    //     Speed_L += 1;
    // else
    //     Speed_L -= 1;

    Speed_L += 1;
}

void RobotControl::encodeR(void)
{
    // int pulValue = digitalRead(SPD_INT_R);
    // if (pulValue)
    //     Speed_R += 1;
    // else
    //     Speed_R -= 1;

    Speed_R += 1;
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

    resetControlVariables();
}

void RobotControl::stop()
{
    stopMotors();

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
    if(!isArmed)
        return;

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

    // Apply angle correction with potential speed adjustment
    targetAngle = aggAC;

    // Calculate PID output for angle control
    Input = currentAngle;
    anglePID.setTunings(aggKp, aggKi, aggKd);
    anglePID.setSetpoint(targetAngle);  // Set the target angle first
    Output = anglePID.compute(Input);   // Compute with only the input parameter

    // Ters yön için PID çıktısının işaretini değiştir
    Output = -Output;  // İşareti tersine çevir

    // Apply motor speed limits with separate limits for each direction
    if (currentAngle < 0) {  // Geri hareket
        // Geri hareket sırasında PWM sınırını sağ motor için daha yüksek, sol motor için daha düşük tutuyoruz
        int maxPwmLeft = pwmLimit / 2;  // Sol motor için sınırı yarıya düşür
        int maxPwmRight = pwmLimit;     // Sağ motor için tam sınır

        // Yönlere göre farklı sınırları uygula
        if (Output < 0) {
            // Geri hareket kontrolü
            pwm = std::clamp(Output, static_cast<float>(-maxPwmLeft), 0.0f);
        } else {
            pwm = std::clamp(Output, 0.0f, static_cast<float>(maxPwmRight));
        }
    } else {
        // Normal pwm sınırları
        pwm = std::clamp(Output, static_cast<float>(-pwmLimit), static_cast<float>(pwmLimit));
    }

    // Apply speed adjustment from motor feedback
    correctSpeedDiff();

    // İleri/geri hız kontrolü için direct motor offset kullanıyoruz
    // NeedSpeed doğrudan pwm değerine eklenir, çok daha yüksek etki için
    float speedOffset = static_cast<float>(needSpeed);

    // Motorlar arasındaki dengeyi sağlamak için kalibrasyon faktörleri
    float leftMotorCalibration = 1.0;   // İleri yönde sol motor kalibrasyonu
    float rightMotorCalibration = 1.0; // İleri yönde sağ motor kalibrasyonu

    // Geri hareket için tamamen farklı bir kontrol yapısı uygulayalım
    if (currentAngle < -5.0f) {  // Anlamlı bir geri hareket olduğunda
        // Simetrik motor gücü uygula - her iki motor için aynı değer
        int symmetricValue = std::clamp(static_cast<int>(Output), -pwmLimit, pwmLimit);

        // Her iki motoru da aynı değerde çalıştır
        pwm_l = symmetricValue;
        pwm_r = symmetricValue;

        // İleri/geri hız komutunu ekle
        pwm_l += speedOffset;
        pwm_r += speedOffset;
    } else {
        // Apply turning adjustments from user input (normal ileri hareket için)
        if (needTurnL > 0) {
            // Sola dönüş - Komutlar ters olduğundan düzeltilmiş hali
            pwm_l = pwm + needTurnL;  // Sol motor daha hızlı
            pwm_r = pwm - needTurnL;  // Sağ motor daha yavaş
        } else if (needTurnR > 0) {
            // Sağa dönüş - Komutlar ters olduğundan düzeltilmiş hali
            pwm_l = pwm - needTurnR;  // Sol motor daha yavaş
            pwm_r = pwm + needTurnR;  // Sağ motor daha hızlı
        } else {
            pwm_l = pwm - (speedAdjust);
            pwm_r = pwm + (speedAdjust);
        }

        // İleri/geri hız komutunu ekle
        pwm_l += speedOffset;
        pwm_r += speedOffset;
    }

    pwm_l = pwm_l * leftMotorCalibration;
    pwm_r = pwm_r * rightMotorCalibration;

    // Apply final PWM values to the motors
    pwm_l = std::clamp(pwm_l, -pwmLimit, pwmLimit);
    pwm_r = std::clamp(pwm_r, -pwmLimit, pwmLimit);

    // Debug
    // Sensor değerlerini ve motor değerlerini düzenli olarak logla
    // static int logCounter = 0;
    // if (logCounter++ % 100 == 0) {  // Her 100 iterasyonda bir logla
    //     qDebug() << "Angle:" << currentAngle
    //              << "Target:" << targetAngle
    //              << "GyroRate:" << gyroXrate
    //              << "PWM_L:" << pwm_l
    //              << "PWM_R:" << pwm_r
    //              << "Speed_L:" << Speed_L
    //              << "Speed_R:" << Speed_R
    //              << "needSpeed:" << needSpeed
    //              << "speedOffset:" << speedOffset
    //              << "needTurnL:" << needTurnL
    //              << "needTurnR:" << needTurnR
    //              << "SpAdj:" << speedAdjust
    //              << "Sym:" << (currentAngle < -5.0f);  // Simetrik mod aktif mi?
    // }

    // Control the robot with calculated PWM values
    controlRobot();
}

void RobotControl::correctSpeedDiff()
{
    // Calculate current speed difference
    diffSpeed = Speed_R - Speed_L;

    // Hız sensörleri belirli bir değerin altındaysa, henüz güvenilir değil, düzeltmeyi atla
    if (abs(Speed_L) < 100 || abs(Speed_R) < 100) {
        speedAdjust = 0;
        return;
    }

    // Speed_L veya Speed_R değeri anormal görünüyorsa, güvenilir düzeltme yapamayız
    // Eşik değerlerini belirle
    const int maxSpeedDiff = 400;

    // Hız değerlerini güvenli aralıkta tut
    if (abs(diffSpeed) > maxSpeedDiff) {
        // Büyük bir dengesizlik var, düzeltmeyi sınırla ama sıfırlama
        diffSpeed = (diffSpeed > 0) ? maxSpeedDiff : -maxSpeedDiff;
    }

    // Accumulate speed difference over time for smoother correction
    diffAllSpeed += static_cast<int32_t>(diffSpeed * 0.1f);  // Integral etkisini azalt

    // Limit the accumulated difference to avoid overcorrection
    diffAllSpeed = std::clamp(diffAllSpeed, static_cast<int32_t>(-1000), static_cast<int32_t>(1000));  // Daha düşük bir limit

    float adjustFactor = 1.0f;
    float integralFactor = 0.01f;

    // Hız düzeltmesi uygula
    speedAdjust = aggSD * adjustFactor * diffSpeed + (aggSD * integralFactor) * diffAllSpeed;

    // Limit the speed adjustment to avoid abrupt changes - Çok daha düşük sınır
    speedAdjust = std::clamp(speedAdjust, -15.0f, 15.0f);

    // Reset the speed counters periodically to avoid overflow
    if (abs(Speed_L) > 100 || abs(Speed_R) > 100) {
        Speed_L = 0;
        Speed_R = 0;
        diffAllSpeed = 0;
    }
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
    aggKp = 40.0;
    aggKi = 5.0;
    aggKd = 0.8;
    aggSD = 5.0;

    gyroXrate = 0;

    qDebug() << "Values Reset - PID Values Kp:" << aggKp
             << "Ki:" << aggKi
             << "Kd:" << aggKd;
}

void RobotControl::run()
{
    stopMotors();

    timer = micros();

    while (!m_stop)
    {
        const std::lock_guard<std::mutex> lock(mutex_loop);
        calculateGyro();
        calculatePwm();
        QThread::msleep(5);
    }
}

bool RobotControl::getIsArmed() const
{
    return isArmed;
}

void RobotControl::setIsArmed(bool newIsArmed)
{
    isArmed = newIsArmed;
}
