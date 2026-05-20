#include "robotcontrol.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <sys/time.h>

RobotControl *RobotControl::theInstance_ = nullptr;

RobotControl* RobotControl::getInstance()
{
    if (theInstance_ == nullptr)
        theInstance_ = new RobotControl();
    return theInstance_;
}

RobotControl::RobotControl(QObject *parent) : QThread(parent)
{
    m_sSettingsFile = QCoreApplication::applicationDirPath() + "/settings.ini";
    m_sBiasFile     = QCoreApplication::applicationDirPath() + "/imu_bias.ini";

    if (QFile(m_sSettingsFile).exists()) {
        loadSettings();
    } else {
        saveSettings();
    }

    if (!initwiringPi()) {
        qDebug() << "WiringPi init failed";
        return;
    }
    if (!initGyroMeter()) {
        qDebug() << "MPU init failed";
        return;
    }

    // Gyro bias kalibrasyonu — robot tamamen hareketsiz olmalı.
    calibrateGyroBias();

    // İlk açıyı accel'den al
    readImu();
    float initAngle = std::atan2((float)ay_, (float)az_) * RAD_TO_DEG;
    currentAngle_ = initAngle;
    kalman.setAngle(initAngle);

    // Pitch PID
    anglePid.setOutputLimit((float)PWM_LIMIT);
    // 0.3° deadband — small wobble around vertical doesn't get amplified.
    // Mechanical play in the wheels means anything tighter creates motor chatter.
    anglePid.setDeadband(0.3f);
    // Tighter derivative low-pass: 0.10 instead of 0.15 smooths more
    // high-frequency gyro noise that was driving the back-and-forth oscillation.
    anglePid.setDerivativeFilter(0.10f);
    // Integral output cap: stop wind-up at ±50 PWM. The I-term is there to
    // compensate steady-state bias (CG offset, motor mismatch), not to
    // dominate the control loop. Without this cap, Ki=0.8 was creating
    // ~1 Hz oscillations as integral built up, overshot, and unwound.
    anglePid.setIntegralOutputCap(50.0f);
    anglePid.setSetpoint(0.0f);

    // Yaw PID — gyro Z hızı sıfırda tutulur, çıkış motor diferansiyeli.
    // Sıkı sınırlarla başla: yaw, pitch dengesini bozmamalı.
    yawPid.setOutputLimit(25.0f);     // ±25 PWM yeterli, denge bozulmaz
    yawPid.setDeadband(5.0f);         // küçük gürültü dikkate alınmasın (°/s)
    yawPid.setDerivativeFilter(0.20f);
    yawPid.setSetpoint(0.0f);
    // Kazançlar setIsArmed/controlLoop'ta aggSD'den ölçeklenerek atanır

    qDebug() << "RobotControl ready. Kp=" << aggKp << "Ki=" << aggKi
             << "Kd=" << aggKd << " trim=" << aggAC
             << " yawKp(SD)=" << aggSD
             << " bias=" << gyroBiasX_ << gyroBiasZ_;
}

RobotControl::~RobotControl()
{
    m_stop = true;
    wait(500);
    stopMotors();
    // Make sure any pending settings get persisted even if the user
    // killed the process before the 5-second save interval fired.
    if (saveDirty) {
        saveSettings();
    }
    delete gyroMPU;
}

void RobotControl::loadSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    aggKp     = settings.value("aggKp",     20.0f).toFloat();
    aggKi     = settings.value("aggKi",     50.0f).toFloat();
    // Kd default is 0.15 — but the iOS BLE protocol uses 1-byte mPD with
    // *10 scaling, so iOS cannot send exactly 0.15 (it can only send 0.1
    // or 0.2). On clean Pi start the file is missing and we use 0.15.
    // If iOS issues "Reset to Defaults", it will send byte=2 → 0.20.
    aggKd     = settings.value("aggKd",      0.15f).toFloat();
    aggSD     = settings.value("aggSD",      2.0f).toFloat();   // yaw Kp
    aggAC     = settings.value("angleCorrection", 0.0f).toFloat();
    trimFine  = settings.value("trimFine",   0.0f).toFloat();
    autoZeroIntegral = settings.value("autoZero", 0.0f).toFloat();

    pidInvert_    = settings.value("pidInvert",    true ).toBool();
    motorInvertL_ = settings.value("motorInvertL", false).toBool();
    motorInvertR_ = settings.value("motorInvertR", false).toBool();
    yawInvert_    = settings.value("yawInvert",    true ).toBool();

    QSettings bias(m_sBiasFile, QSettings::IniFormat);
    gyroBiasX_ = bias.value("gx", 0.0f).toFloat();
    gyroBiasY_ = bias.value("gy", 0.0f).toFloat();
    gyroBiasZ_ = bias.value("gz", 0.0f).toFloat();
}

void RobotControl::saveSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.setValue("aggKp",     aggKp);
    settings.setValue("aggKi",     aggKi);
    settings.setValue("aggKd",     aggKd);
    settings.setValue("aggSD",     aggSD);
    settings.setValue("angleCorrection", aggAC);
    settings.setValue("trimFine",  trimFine);
    settings.setValue("autoZero",  autoZeroIntegral);
    settings.setValue("pidInvert",    pidInvert_);
    settings.setValue("motorInvertL", motorInvertL_);
    settings.setValue("motorInvertR", motorInvertR_);
    settings.setValue("yawInvert",    yawInvert_);
    settings.sync();
    saveDirty = false;
}

bool RobotControl::initGyroMeter()
{
    qDebug("Initializing MPU6050...");
    gyroMPU = new MPU6050(MPU6050_I2C_ADDRESS);
    if (!gyroMPU) return false;

    gyroMPU->initialize();

    int tries = 0;
    while (!gyroMPU->testConnection() && tries < 20) {
        QThread::msleep(50);
        tries++;
    }
    bool ok = gyroMPU->testConnection();
    qDebug(ok ? "MPU6050 OK" : "MPU6050 connection failed");

    if (ok) {
        // DLPF 3 ≈ 44 Hz bandwidth - gyro gürültüsünü azalt
        gyroMPU->setDLPFMode(3);
    }
    return ok;
}

bool RobotControl::initwiringPi()
{
    if (wiringPiSetupPhys() < 0) {
        fprintf(stderr, "wiringPiSetupPhys failed: %s\n", strerror(errno));
        return false;
    }

    pinMode(PWML1, OUTPUT);
    pinMode(PWML2, OUTPUT);
    pinMode(PWMR1, OUTPUT);
    pinMode(PWMR2, OUTPUT);
    pinMode(PWML,  OUTPUT);
    pinMode(PWMR,  OUTPUT);

    digitalWrite(PWML1, LOW);
    digitalWrite(PWML2, LOW);
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, LOW);

    softPwmCreate(PWML, 0, PWM_LIMIT);
    softPwmCreate(PWMR, 0, PWM_LIMIT);

    // Encoder pinleri YAPILANDIRILMIYOR — donanım encoder'ları artık kullanılmıyor.
    // İstenirse pinler tamamen başka amaca da kullanılabilir.

    qDebug("WiringPi ready (no encoders)");
    return true;
}

// ---------------- IMU ----------------

void RobotControl::readImu()
{
    if (!gyroMPU) return;
    gyroMPU->getMotion6(&ax_, &ay_, &az_, &gx_, &gy_, &gz_);
}

void RobotControl::calibrateGyroBias()
{
    // Acceptable thresholds:
    //   - bias magnitude on each axis  : < 5 °/s (typical < 1 °/s)
    //   - sample stddev on each axis   : < 3 °/s (motion during cal)
    // If either is exceeded we retry. After several failed attempts we
    // give up and keep whatever we got, but warn loudly.
    const int   N            = 400;     // ~2 s @ 5 ms
    const float BIAS_MAX     = 5.0f;    // °/s
    const float JITTER_MAX   = 3.0f;    // °/s sample stddev
    const int   MAX_ATTEMPTS = 3;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
        qDebug("Calibrating gyro bias (attempt %d/%d) - keep the robot still...",
               attempt, MAX_ATTEMPTS);

        // Two-pass: mean, then stddev to detect motion during cal.
        double sx = 0, sy = 0, sz = 0;
        std::vector<float> bufX(N), bufY(N), bufZ(N);

        for (int i = 0; i < N; ++i) {
            readImu();
            float xf = (float)gx_ / 131.0f;
            float yf = (float)gy_ / 131.0f;
            float zf = (float)gz_ / 131.0f;
            bufX[i] = xf; bufY[i] = yf; bufZ[i] = zf;
            sx += xf; sy += yf; sz += zf;
            QThread::msleep(5);
        }

        float mx = (float)(sx / N);
        float my = (float)(sy / N);
        float mz = (float)(sz / N);

        // Stddev — how much was the gyro signal moving during the window?
        double varX = 0, varY = 0, varZ = 0;
        for (int i = 0; i < N; ++i) {
            float dx = bufX[i] - mx; varX += dx*dx;
            float dy = bufY[i] - my; varY += dy*dy;
            float dz = bufZ[i] - mz; varZ += dz*dz;
        }
        float sdX = std::sqrt(varX / N);
        float sdY = std::sqrt(varY / N);
        float sdZ = std::sqrt(varZ / N);

        bool motion = (sdX > JITTER_MAX) || (sdY > JITTER_MAX) || (sdZ > JITTER_MAX);
        bool absurd = (std::fabs(mx) > BIAS_MAX)
                   || (std::fabs(my) > BIAS_MAX)
                   || (std::fabs(mz) > BIAS_MAX);

        qDebug() << "  mean X=" << mx << " Y=" << my << " Z=" << mz;
        qDebug() << "  stddev X=" << sdX << " Y=" << sdY << " Z=" << sdZ;

        if (!motion && !absurd) {
            // Looks good — commit and exit
            gyroBiasX_ = mx;
            gyroBiasY_ = my;
            gyroBiasZ_ = mz;

            QSettings bias(m_sBiasFile, QSettings::IniFormat);
            bias.setValue("gx", gyroBiasX_);
            bias.setValue("gy", gyroBiasY_);
            bias.setValue("gz", gyroBiasZ_);
            bias.sync();

            gyroCalibrated_ = true;
            qDebug() << "Gyro bias OK: X=" << gyroBiasX_
                     << " Y=" << gyroBiasY_
                     << " Z=" << gyroBiasZ_ << "°/s";
            return;
        }

        if (motion) {
            qDebug("  ⚠ motion detected during calibration — please hold the robot perfectly still.");
        }
        if (absurd) {
            qDebug("  ⚠ bias magnitude exceeds %.1f°/s — sensor or position issue.", BIAS_MAX);
        }
        if (attempt < MAX_ATTEMPTS) {
            qDebug("  Retrying in 1 second...");
            QThread::msleep(1000);
        }
    }

    // All attempts failed. Use the LAST measurement but don't persist it
    // to disk — next start will retry. Warn user.
    qDebug("  ✗ Calibration FAILED after %d attempts. Robot may not balance properly.", MAX_ATTEMPTS);
    qDebug("    Place the robot flat on a hard surface and restart.");
    // gyroBiasX_/Y_/Z_ retain whatever loadSettings() previously loaded
    // (or zeros on first run). Better than committing garbage.
}

void RobotControl::updateEstimates(float dt)
{
    // Pitch açısı: accel y/z düzleminde atan2
    accelAngle_ = std::atan2((float)ay_, (float)az_) * RAD_TO_DEG;

    // Gyro rate, bias düzeltili
    gyroRateX_ = ((float)gx_ / 131.0f) - gyroBiasX_;
    gyroRateZ_ = ((float)gz_ / 131.0f) - gyroBiasZ_;

    // Yaw için düşük geçiren filtre — gyroZ çok gürültülü olabiliyor,
    // PID'e ham veri vermek diferansiyele patlamalar bindirir.
    const float yawAlpha = 0.25f;   // 0..1, küçük → daha yumuşak
    gyroRateZFilt_ = yawAlpha * gyroRateZ_ + (1.0f - yawAlpha) * gyroRateZFilt_;

    // Kalman ile pitch füzyonu
    currentAngle_ = (float)kalman.getAngle(accelAngle_, gyroRateX_, dt);
}

// ---------------- Yardımcılar ----------------

bool RobotControl::isUpright() const
{
    return std::fabs(currentAngle_ - (aggAC + trimFine)) < ARM_TILT_THRESHOLD
           && std::fabs(gyroRateX_) < 40.0f;
}

bool RobotControl::isFallen() const
{
    return std::fabs(currentAngle_) > FALL_TILT_THRESHOLD;
}

void RobotControl::resetControlState()
{
    anglePid.resetIntegral();
    yawPid.resetIntegral();
    pwmL_ = 0;
    pwmR_ = 0;
    yawCorrection_ = 0.0f;
    prevNeedSpeed_ = 0;
    brakeCounter_ = 0;
    brakeBiasAngle_ = 0.0f;
    brakeDirection_ = 0;
    speedOffsetFilt_ = 0.0f;
}

void RobotControl::stopMotors()
{
    softPwmWrite(PWML, 0);
    softPwmWrite(PWMR, 0);
    digitalWrite(PWML1, LOW);
    digitalWrite(PWML2, LOW);
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, LOW);
    pwmL_ = 0;
    pwmR_ = 0;
}

void RobotControl::stop()
{
    stopMotors();
    qDebug("Motors stopped");
}

void RobotControl::setIsArmed(bool armed)
{
    bool wasArmed = isArmed.exchange(armed);
    if (armed && !wasArmed) {
        resetControlState();
        armRampCount_ = 0;
        anglePid.setTunings(aggKp, aggKi * 0.01f, aggKd);
        yawPid.setTunings(aggSD * 0.1f, aggSD * 0.001f, 0.0f);
        qDebug().noquote() << QString::asprintf(
            "ARMED — Kp=%.2f Ki=%.4f Kd=%.2f  yawKp=%.3f yawKi=%.4f  trim=%.2f  inv=%c%c%c%c",
            aggKp, aggKi*0.01f, aggKd,
            aggSD*0.1f, aggSD*0.001f,
            aggAC + trimFine + autoZeroIntegral,
            pidInvert_    ? 'P' : '-',
            motorInvertL_ ? 'L' : '-',
            motorInvertR_ ? 'R' : '-',
            yawInvert_    ? 'Y' : '-');
    } else if (!armed && wasArmed) {
        stopMotors();
        armRampCount_ = 0;
        qDebug("DISARMED");
    }
}

void RobotControl::resetTrim()
{
    autoZeroIntegral = 0.0f;
    trimFine = 0.0f;
    saveDirty = true;
    qDebug("Trim reset");
}

// ---------------- Ana kontrol döngüsü ----------------

void RobotControl::controlLoop(float /*dt*/)
{
    // -------- Düşme / kalkış mantığı --------
    if (isFallen()) {
        if (!fallen_) {
            qDebug() << "FALLEN at angle:" << currentAngle_;
            fallen_ = true;
            if (isArmed.load()) {
                setIsArmed(false);
            } else {
                stopMotors();
            }
            uprightSampleCount_ = 0;
        }
        return;
    } else {
        fallen_ = false;
    }

    // Auto-arm
    if (autoMode.load() && !isArmed.load()) {
        if (isUpright()) {
            uprightSampleCount_++;
            if (uprightSampleCount_ >= ARM_STABLE_SAMPLES) {
                qDebug("Auto-arming (upright detected)");
                setIsArmed(true);
                uprightSampleCount_ = 0;
            }
        } else {
            uprightSampleCount_ = 0;
        }
        if (!isArmed.load()) return;
    }

    if (!isArmed.load()) {
        return;
    }

    // -------- Pitch PID --------
    targetAngle_ = aggAC + trimFine + autoZeroIntegral;

    // ---- Brake assist (anti-overshoot when speed command is released) ----
    // When the user was actively moving and now releases the joystick,
    // the robot still has forward/back momentum. We fight it in two ways:
    //
    //   1. OPEN-LOOP pulse: a short tilt in the opposite direction sized
    //      by how much PWM the user was commanding. Decays linearly.
    //
    //   2. CLOSED-LOOP feedback: while the brake window is active, also
    //      add a term proportional to gyroX (pitch rate). If the robot is
    //      still rotating in the direction it was going, lean it harder
    //      the other way. Once it stops rotating, gyroX → 0 and the brake
    //      automatically fades. This is the gyro-driven term you asked for.
    int curSpeed = needSpeed.load();
    
    // If a new command arrives, immediately cancel any in-progress brake.
    // Otherwise the leftover opposing tilt would fight the new command and
    // cause visible shudder/oscillation.
    if (curSpeed != 0 && brakeCounter_ > 0) {
        brakeCounter_ = 0;
        brakeBiasAngle_ = 0.0f;
        brakeDirection_ = 0;
    }
    
    if (prevNeedSpeed_ != 0 && curSpeed == 0) {
        // Just released — arm the brake pulse opposing the previous direction.
        brakeCounter_    = BRAKE_TICKS_AT_5MS;
        brakeBiasAngle_  = (float)prevNeedSpeed_ * BRAKE_PER_PWM;
        brakeBiasAngle_  = std::clamp(brakeBiasAngle_, -BRAKE_BIAS_LIMIT, BRAKE_BIAS_LIMIT);
        brakeDirection_  = (prevNeedSpeed_ > 0) ? 1 : -1;
        // Reset the angle integrator: while leaning forward to drive
        // the robot, it accumulated I that would keep pushing forward.
        anglePid.resetIntegral();
    }
    prevNeedSpeed_ = curSpeed;

    // Brake only runs while there is no active command (brakeCounter was
    // zeroed above if a new command came in).
    if (brakeCounter_ > 0 && curSpeed == 0) {
        // 1) Open-loop fade: decays linearly to zero over the window.
        float k = (float)brakeCounter_ / (float)BRAKE_TICKS_AT_5MS;
        float openLoop = brakeBiasAngle_ * k;

        // 2) Closed-loop gyro feedback:
        //    pidInvert mapping: if needSpeed was positive (Backward command
        //    -> robot moves "backward" in motor terms), the robot pitches
        //    one way; gyroX picks that up. Sign is symmetric because we
        //    multiply by brakeDirection_:
        //
        //      If robot is still pitching in the original direction
        //      (sign(gyroX) == brakeDirection_), gyroFeedback opposes it.
        //      As the robot stops, gyroX → 0 and the term vanishes.
        //
        //    We only apply this when the rate is in the same direction
        //    as the prior motion (decelerating phase). If the robot
        //    already overshot the other way, this term goes to zero.
        float gyroFeedback = 0.0f;
        if ((gyroRateX_ > 0 && brakeDirection_ > 0) ||
            (gyroRateX_ < 0 && brakeDirection_ < 0)) {
            gyroFeedback = gyroRateX_ * BRAKE_GYRO_GAIN;
            gyroFeedback = std::clamp(gyroFeedback, -BRAKE_GYRO_MAX, BRAKE_GYRO_MAX);
        }

        float total = openLoop + gyroFeedback;
        total = std::clamp(total, -BRAKE_BIAS_LIMIT, BRAKE_BIAS_LIMIT);
        targetAngle_ += total;
        brakeCounter_--;
    }

    targetAngle_ = std::clamp(targetAngle_, -10.0f, 10.0f);

    anglePid.setSetpoint(targetAngle_);
    anglePid.setTunings(aggKp, aggKi * 0.01f, aggKd);

    float pidOut = anglePid.compute(currentAngle_, gyroRateX_);
    if (pidInvert_) pidOut = -pidOut;

    // -------- Otomatik trim öğrenme (uzun süreli) --------
    // Komut yokken ve dengedeyken, sürekli taşınan integral ofsetini yavaşça
    // autoZeroIntegral'a aktar.
    if (needSpeed.load() == 0 && needTurnL.load() == 0 && needTurnR.load() == 0 &&
        std::fabs(currentAngle_ - targetAngle_) < 1.5f) {
        float i_term = anglePid.lastI();
        autoZeroIntegral += 0.00005f * i_term;
        autoZeroIntegral = std::clamp(autoZeroIntegral, -8.0f, 8.0f);
    }

    // -------- Yaw kontrolü --------
    // Kullanıcı dönüş komutu (needTurnL/R) veriyorsa yaw kilidi kapalı,
    // robot serbestçe dönsün. Komut yoksa gyroZ ile sıfırda tut.
    int turnBias = 0;
    int needL = needTurnL.load();
    int needR = needTurnR.load();
    if (needR > 0)      turnBias = +needR;
    else if (needL > 0) turnBias = -needL;

    yawCorrection_ = 0.0f;
    if (turnBias == 0 && yawLock.load()) {
        // Yaw kazançlarını güncelle (UI'dan değiştirilebilir)
        yawPid.setTunings(aggSD * 0.1f, aggSD * 0.001f, 0.0f);
        // FİLTRELİ gyroZ kullan — ham veri çok gürültülü
        yawCorrection_ = yawPid.compute(gyroRateZFilt_);
        if (yawInvert_) yawCorrection_ = -yawCorrection_;

        // ÖNEMLİ: pitch denge stresi varsa yaw'ı bastır.
        // Robot büyük açıyla eğikken yaw düzeltmesi motor PWM'sini bozar
        // ve denge düşer. Önce ayakta kalmaya odaklan.
        //   |angle| < 2°  → yaw tam çalışır
        //   |angle| > 6°  → yaw tamamen bastırılır
        //   arada       → lineer azaltma
        float absAng = std::fabs(currentAngle_ - targetAngle_);
        float yawScale = 1.0f;
        if (absAng >= 6.0f)      yawScale = 0.0f;
        else if (absAng >= 2.0f) yawScale = (6.0f - absAng) / 4.0f;
        yawCorrection_ *= yawScale;
    } else {
        yawPid.resetIntegral();
    }

    // -------- Hız offset'i (BLE forward/backward) --------
    // Raw target offset from BLE. Negative = forward.
    float speedOffsetTarget = -(float)needSpeed.load();
    // Low-pass filter the offset so a sudden joystick push doesn't
    // step-inject 180 PWM and shake the robot. Time constant ~0.25 s.
    // alpha = dt / (tau + dt), with dt = 5 ms → alpha ≈ 0.020.
    // Smaller alpha = smoother ramp, less command-induced oscillation.
    constexpr float SPEED_ALPHA = 0.03f;
    speedOffsetFilt_ += SPEED_ALPHA * (speedOffsetTarget - speedOffsetFilt_);
    float speedOffset = speedOffsetFilt_;

    // -------- Motor çıkışları --------
    int yawDiff = (int)std::round(yawCorrection_);
    int rawL = (int)std::round(pidOut + speedOffset) - turnBias - yawDiff;
    int rawR = (int)std::round(pidOut + speedOffset) + turnBias + yawDiff;

    rawL = std::clamp(rawL, -PWM_LIMIT, PWM_LIMIT);
    rawR = std::clamp(rawR, -PWM_LIMIT, PWM_LIMIT);

    if (motorInvertL_) rawL = -rawL;
    if (motorInvertR_) rawR = -rawR;

    // Soft start
    if (armRampCount_ < 20) {
        float k = (float)armRampCount_ / 20.0f;
        rawL = (int)(rawL * k);
        rawR = (int)(rawR * k);
        armRampCount_++;
    }

    // PWM minimum threshold: anything below ±8 PWM can't overcome motor
    // static friction anyway, so writing it just creates electrical buzz
    // and contributes nothing to balance. Snap small PWM to zero. This
    // suppresses the visible back-and-forth motor chatter when the robot
    // is near vertical.
    constexpr int PWM_MIN = 8;
    if (std::abs(rawL) < PWM_MIN) rawL = 0;
    if (std::abs(rawR) < PWM_MIN) rawR = 0;

    pwmL_ = rawL;
    pwmR_ = rawR;
    applyMotors(pwmL_, pwmR_);
}

void RobotControl::applyMotors(int pwmL, int pwmR)
{
    // Sağ motor
    if (pwmR > 0) {
        digitalWrite(PWMR1, HIGH);
        digitalWrite(PWMR2, LOW);
        softPwmWrite(PWMR, pwmR);
    } else if (pwmR < 0) {
        digitalWrite(PWMR1, LOW);
        digitalWrite(PWMR2, HIGH);
        softPwmWrite(PWMR, -pwmR);
    } else {
        digitalWrite(PWMR1, LOW);
        digitalWrite(PWMR2, LOW);
        softPwmWrite(PWMR, 0);
    }

    // Sol motor (ters monte edildiği için pin'ler ters)
    if (pwmL > 0) {
        digitalWrite(PWML1, LOW);
        digitalWrite(PWML2, HIGH);
        softPwmWrite(PWML, pwmL);
    } else if (pwmL < 0) {
        digitalWrite(PWML1, HIGH);
        digitalWrite(PWML2, LOW);
        softPwmWrite(PWML, -pwmL);
    } else {
        digitalWrite(PWML1, LOW);
        digitalWrite(PWML2, LOW);
        softPwmWrite(PWML, 0);
    }
}

// ---------------- Telemetri ----------------

RobotControl::Telemetry RobotControl::getTelemetry()
{
    std::lock_guard<std::mutex> lk(telMutex_);
    return latestTelemetry_;
}

// ---------------- Thread ----------------

void RobotControl::run()
{
    stopMotors();

    QThread::currentThread()->setPriority(QThread::TimeCriticalPriority);

    auto getMicros = []() -> uint64_t {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    };

    uint64_t prev = getMicros();
    uint64_t telemetryLastUs = prev;
    uint64_t saveLastUs = prev;

    while (!m_stop)
    {
        uint64_t now = getMicros();
        float dt = (float)(now - prev) * 1.0e-6f;
        if (dt < 0.0005f) dt = LOOP_DT_TARGET;
        if (dt > 0.05f)   dt = LOOP_DT_TARGET;
        prev = now;

        readImu();
        updateEstimates(dt);
        controlLoop(dt);

        // Telemetri snapshot ~20 Hz
        if (now - telemetryLastUs > 50000) {
            Telemetry t;
            t.angle        = currentAngle_;
            t.gyroRate     = gyroRateX_;
            t.yawRate      = gyroRateZ_;
            t.targetAngle  = targetAngle_;
            t.trim         = aggAC + trimFine + autoZeroIntegral;
            t.pwmL         = pwmL_;
            t.pwmR         = pwmR_;
            t.armed        = isArmed.load();
            t.fallen       = fallen_;
            t.autoMode     = autoMode.load();
            t.positionHold = yawLock.load();
            {
                std::lock_guard<std::mutex> lk(telMutex_);
                latestTelemetry_ = t;
            }
            telemetryLastUs = now;
        }

        if (saveDirty && (now - saveLastUs) > 5000000) {
            saveSettings();
            saveLastUs = now;
        }

        // Sabit oran sleep
        int64_t elapsedUs = (int64_t)(getMicros() - now);
        int64_t sleepUs = (int64_t)LOOP_DT_US - elapsedUs;
        if (sleepUs > 0) {
            usleep((useconds_t)sleepUs);
        }
    }

    stopMotors();
}
