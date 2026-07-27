#include "robotcontrol.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <chrono>
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
    qDebug() << "RobotControl ready. Kp=" << aggKp << "Ki=" << aggKi
             << "Kd=" << aggKd << " trim=" << aggAC
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
    // Defaults below are the values found most stable after extensive
    // bench testing with the encoder-based position hold enabled.
    //   Kp=25, Ki=40 (UI scale → real Ki=0.4), Kd=0.10
    // Defaults: Twiddle auto-tune sonucu (gerçek donanımda, push-recovery
    // maliyeti 166 -> 44). aggKi UI ölçeğidir (gerçek Ki = aggKi*0.01).
    aggKp     = settings.value("aggKp",     29.2f).toFloat();
    aggKi     = settings.value("aggKi",     64.0f).toFloat();
    aggKd     = settings.value("aggKd",      0.103f).toFloat();
    // Varsayılan -5°: bu şasinin gerçek mekanik denge noktası (donanımda
    // doğrulandı - robot bu trimle "çok iyi" dengede duruyor). Yeni/temiz
    // kurulum doğrudan doğru noktadan başlar; settings.ini'deki değer
    // her zaman önceliklidir.
    aggAC     = settings.value("angleCorrection", -5.0f).toFloat();
    trimFine  = settings.value("trimFine",   0.0f).toFloat();
    autoZeroIntegral = settings.value("autoZero", 0.0f).toFloat();

    pidInvert_      = settings.value("pidInvert",      true).toBool();
    motorInvertL_   = settings.value("motorInvertL",   false).toBool();
    motorInvertR_   = settings.value("motorInvertR",   false).toBool();
    encoderInvertL_ = settings.value("encoderInvertL", true).toBool();
    encoderInvertR_ = settings.value("encoderInvertR", true).toBool();

    // Speed PID — B-Robot tarzı cascade. Defaults bench testing'den.
    spdKp      = settings.value("spdKp",      0.12f).toFloat();
    spdKi      = settings.value("spdKi",      0.20f).toFloat();
    spdMaxTilt = settings.value("spdMaxTilt", 5.0f).toFloat();
    spdMaxVel  = settings.value("spdMaxVel",  3.0f).toFloat();
    spdTiltSlew = settings.value("spdTiltSlew", 0.04f).toFloat();
    learnMoveCmd_ = settings.value("learnMoveCmd", 60).toInt();
    learnTurnCmd_ = settings.value("learnTurnCmd", 20).toInt();

    // Legacy fields no longer read or written:
    //   aggSD       (was yaw PID gain for gyro-Z control; now encoder-based
    //               with hardcoded gains)
    //   yawInvert   (was sign for gyro-Z yaw; encoder yaw uses its own sign)

    QSettings bias(m_sBiasFile, QSettings::IniFormat);
    gyroBiasX_ = bias.value("gx", 0.0f).toFloat();
    gyroBiasY_ = bias.value("gy", 0.0f).toFloat();
    gyroBiasZ_ = bias.value("gz", 0.0f).toFloat();
}

void RobotControl::saveSettings()
{
    QSettings settings(m_sSettingsFile, QSettings::IniFormat);
    settings.setValue("aggKp",           aggKp);
    settings.setValue("aggKi",           aggKi);
    settings.setValue("aggKd",           aggKd);
    settings.setValue("angleCorrection", aggAC);
    settings.setValue("trimFine",        trimFine);
    settings.setValue("autoZero",        autoZeroIntegral);
    settings.setValue("pidInvert",       pidInvert_);
    settings.setValue("motorInvertL",    motorInvertL_);
    settings.setValue("motorInvertR",    motorInvertR_);
    settings.setValue("encoderInvertL",  encoderInvertL_);
    settings.setValue("encoderInvertR",  encoderInvertR_);
    settings.setValue("spdKp",           spdKp);
    settings.setValue("spdKi",           spdKi);
    settings.setValue("spdMaxTilt",      spdMaxTilt);
    settings.setValue("spdMaxVel",       spdMaxVel);
    settings.setValue("spdTiltSlew",     spdTiltSlew);
    settings.setValue("learnMoveCmd",    learnMoveCmd_);
    settings.setValue("learnTurnCmd",    learnTurnCmd_);
    // Migrate old key names: if present from older builds, drop them.
    if (settings.contains("aggSD"))     settings.remove("aggSD");
    if (settings.contains("yawInvert")) settings.remove("yawInvert");
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

    // Encoder interrupt pins — diagnostic mode only. Each rising edge
    // bumps a counter; the run loop logs counter deltas periodically so
    // we can see whether the wheels are physically reporting motion.
    pinMode(SPD_INT_L, INPUT);
    pinMode(SPD_PUL_L, INPUT);
    pinMode(SPD_INT_R, INPUT);
    pinMode(SPD_PUL_R, INPUT);
    pullUpDnControl(SPD_INT_L, PUD_UP);
    pullUpDnControl(SPD_PUL_L, PUD_UP);
    pullUpDnControl(SPD_INT_R, PUD_UP);
    pullUpDnControl(SPD_PUL_R, PUD_UP);

    if (wiringPiISR(SPD_INT_L, INT_EDGE_RISING, &RobotControl::encLeftISR) < 0) {
        qDebug("Failed to register left encoder ISR");
    }
    if (wiringPiISR(SPD_INT_R, INT_EDGE_RISING, &RobotControl::encRightISR) < 0) {
        qDebug("Failed to register right encoder ISR");
    }

    qDebug("WiringPi ready (encoder ISRs enabled)");
    return true;
}

// ---------------- Encoder ISRs (diagnostic only) ----------------
//
// ---------------- Encoder ISRs (quadrature decoding) ----------------
//
// On the rising edge of channel A (SPD_INT_*), read channel B (SPD_PUL_*).
// If B is HIGH, wheel turns one way; if LOW, the other. We accumulate a
// signed tick count. The "forward" direction depends on motor wiring;
// flip the sign at use site if needed.
//
// ISR notes:
//   - Must be fast — just one digitalRead and one increment/decrement.
//   - Counters are volatile long; the main loop reads them.
//   - No locking; tearing on 64-bit reads is harmless at this rate.

volatile long RobotControl::encLeftTicks_  = 0;
volatile long RobotControl::encRightTicks_ = 0;

void RobotControl::encLeftISR()
{
    if (digitalRead(SPD_PUL_L) == HIGH) {
        encLeftTicks_++;
    } else {
        encLeftTicks_--;
    }
}

void RobotControl::encRightISR()
{
    if (digitalRead(SPD_PUL_R) == HIGH) {
        encRightTicks_++;
    } else {
        encRightTicks_--;
    }
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

        // Only log details on failure
        qDebug() << "  mean X=" << mx << " Y=" << my << " Z=" << mz;
        qDebug() << "  stddev X=" << sdX << " Y=" << sdY << " Z=" << sdZ;

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
    zeroSpeedFrames_ = 0;
    lastCommitedSpeed_ = 0;
    speedOffsetFilt_ = 0.0f;
    brakePulseFrames_ = 0;
    brakePulseValue_ = 0.0f;
    spdPidIntegral_ = 0.0f;
    targetVelFilt_ = 0.0f;
    // Position hold — re-lock at whatever the current chassis position is.
    long encL_eff = encoderInvertL_ ? -encLeftTicks_  : encLeftTicks_;
    long encR_eff = encoderInvertR_ ? -encRightTicks_ : encRightTicks_;
    lockedPos_ = (encL_eff + encR_eff) / 2;
    lastChassisPos_ = lockedPos_;
    chassisVelFilt_ = 0.0f;
    posHoldActive_ = false;
    // Yaw hold — re-lock current heading
    lockedYawDiff_ = encR_eff - encL_eff;
    lastYawDiff_ = lockedYawDiff_;
    yawDiffVelFilt_ = 0.0f;
    yawHoldActive_ = false;
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
        qDebug().noquote() << QString::asprintf(
            "ARMED — Kp=%.2f Ki=%.4f Kd=%.2f  trim=%.2f  inv=%c%c%c",
            aggKp, aggKi*0.01f, aggKd,
            aggAC + trimFine + autoZeroIntegral,
            pidInvert_    ? 'P' : '-',
            motorInvertL_ ? 'L' : '-',
            motorInvertR_ ? 'R' : '-');
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

// ---------------- PID öğrenme modu ----------------
//
// begin()/requestStop() sadece atomik bayrak set eder; tuner'in tüm durum
// makinesi kontrol döngüsü thread'inde (feedSample) ilerler.

bool RobotControl::startPidLearning()
{
    if (pidTuner_.isActive()) return true;
    if (!isArmed.load()) {
        // Robot yatarken/dengede değilken öğrenme REDDEDİLİR: tuner durum
        // makinesi yalnızca armed kontrol dalında ilerlediği için yatarken
        // "başlatmak" hem anlamsız hem de durdurulamaz bir sahte-aktif
        // durum yaratıyordu. Kullanıcı robotu dik konuma getirip yeniden
        // istemeli.
        qDebug("PID learn REFUSED - robot is not balancing (disarmed)");
        return false;
    }
    pidTuner_.setManeuver(learnMoveCmd_, learnTurnCmd_);
    PidAutoTuner::Gains g;
    g.kp = aggKp;
    g.ki = aggKi * 0.01f;   // UI ölçeğinden gerçek ölçeğe
    g.kd = aggKd;
    pidTuner_.begin(g);
    return true;
}

void RobotControl::stopPidLearning()
{
    pidTuner_.requestStop();
    // Disarmed iken feedSample çalışmaz ve istek asla işlenmezdi
    // (mobil Stop'un "etki etmiyor" görünmesinin nedeni buydu):
    if (!isArmed.load())
        pidTuner_.abortNow();
}

// ---------------- Ana kontrol döngüsü ----------------

void RobotControl::controlLoop(float /*dt*/)
{
    // -------- Düşme / kalkış mantığı --------
    if (isFallen()) {
        if (!fallen_) {
            qDebug() << "FALLEN at angle:" << currentAngle_;
            fallen_ = true;
            if (pidTuner_.isActive()) pidTuner_.notifyFall();
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

    // ---- Speed command → target angle bias (not motor PWM!) ----
    //
    // Balance robots steer by leaning, not by direct motor push. Adding
    // speed offset directly to motor PWM (the old way) makes the chassis
    // fight Newton's 3rd law: motors push forward, robot pitches BACKWARD,
    // pitch loop chases — it lags badly under load and falls.
    //
    // Correct approach: forward command → tilt the target angle forward
    // by a few degrees. Robot leans forward → naturally accelerates
    // forward. Pitch PID effortlessly maintains the tilt because there's
    // no opposing reaction force to fight.
    //
    // Map: max joystick command (180 PWM) → max ±4° target tilt.
    // Asymmetric smoothing kept: ramp up smoothly, snap to zero on release.
    // Kullanicinin ham komutu ayrica saklanir: PID ogrenmedeki manevra
    // komutlari 'kullanici dokundu' sayilmasin (aday bosuna yeniden
    // baslamasin); joystick'e GERCEKTEN dokunulursa aday yine yenilenir.
    const int userSpeedRaw = needSpeed.load();
    int curSpeedRaw = userSpeedRaw;
    if (pidTuner_.isActive() && userSpeedRaw == 0)
        curSpeedRaw = pidTuner_.motionSpeed();
    int curSpeed = (std::abs(curSpeedRaw) < 15) ? 0 : curSpeedRaw;

    if (curSpeed == 0) {
        zeroSpeedFrames_++;
    } else {
        zeroSpeedFrames_ = 0;
    }
    if (zeroSpeedFrames_ == 10 && lastCommitedSpeed_ != 0) {
        anglePid.resetIntegral();
        lastCommitedSpeed_ = 0;
    }
    if (curSpeed != 0) {
        lastCommitedSpeed_ = curSpeed;
    }
    prevNeedSpeed_ = curSpeed;

    // ---- Chassis velocity from encoders (used by both speed tilt and pos hold) ----
    long encL_raw = encLeftTicks_;
    long encR_raw = encRightTicks_;
    long encL_eff = encoderInvertL_ ? -encL_raw : encL_raw;
    long encR_eff = encoderInvertR_ ? -encR_raw : encR_raw;
    long chassisPos = (encL_eff + encR_eff) / 2;
    long velRaw = chassisPos - lastChassisPos_;
    lastChassisPos_ = chassisPos;
    chassisVelFilt_ = (1.0f - POS_VEL_ALPHA) * chassisVelFilt_ + POS_VEL_ALPHA * (float)velRaw;

    // Target tilt from speed command — TWO-MODE strategy.
    //
    // Mode A (OPEN-LOOP, default when no encoder velocity available):
    //   joystick → fixed tilt magnitude → constant acceleration.
    //   Simple, but the robot keeps speeding up because nothing limits
    //   acceleration. Use only as a fallback.
    //
    // Mode B (CLOSED-LOOP SPEED PID, default — uses encoders):
    //   joystick → target chassis velocity (ticks/loop).
    //   PID measures actual velocity vs target and adjusts tilt to make
    //   the robot reach and HOLD that speed. Going up a slope, sand, or
    //   carpet — the system automatically compensates by tilting more
    //   to maintain the requested speed. Joystick centered → target 0,
    //   robot brakes to a stop, then position hold takes over.
    //
    // Velocity-PID parameters tuned conservatively. The output is a
    // tilt angle (not PWM), so it adds to the targetAngle the pitch
    // PID is chasing.
    // ---- Speed PID (B-Robot inspired cascade) ----
    //
    // Reference: jjrobots B-Robot. The outer (speed) PID's job is to find
    // the *correct* tilt angle that makes actual velocity equal to target.
    // The inner pitch PID then makes the body chase that tilt.
    //
    // Why this works where naive tilt-from-stick doesn't:
    //  - User wants speed=0 → speed PID finds whatever small tilt cancels
    //    any drift (compensates for CG offset, surface bias, asymmetry).
    //  - User wants speed=X → speed PID finds tilt that holds X, also
    //    automatically tilts BACK to brake when stick is released.
    //
    // Key insight from B-Robot constants:
    //   KP_THROTTLE = 0.075   (small proportional gain)
    //   KI_THROTTLE = 0.1     (LARGER integral — does the heavy lifting)
    //   MAX_TARGET_ANGLE = 14° (lots of tilt headroom for braking)
    //
    // The integral is the workhorse: when actual vel doesn't match target,
    // the integrator slowly accumulates corrective tilt. This gives smooth
    // acceleration AND smooth braking on release.
    // Speed PID parametreleri — settings.ini'den yüklenir, iOS Settings'ten ayarlanabilir.
    // Defaults: spdKp=0.15, spdKi=0.30, spdMaxTilt=6.0°, spdMaxVel=4.0
    const float MAX_SPEED_TILT_DEG  = spdMaxTilt;
    const float tmpVel_ = tempMaxVel_.load();
    const float MAX_TARGET_VEL      = (tmpVel_ > 0.0f) ? tmpVel_ : spdMaxVel;
    const float SPD_PID_KP          = spdKp;
    const float SPD_PID_KI          = spdKi;
    constexpr float SPD_PID_I_LIMIT = 3.0f;

    // Map joystick (-180..+180) → target velocity (-MAX..+MAX).
    // Sign convention finalized 2026-05-21:
    //   iOS joystick up = user's "forward" intent
    //   balancerobot.cpp maps mBackward (which iOS sends for "up") → cmd>0
    //   here: cmd>0 should drive encoder vel>0 (user's "forward" direction)
    //   so targetVel has SAME sign as cmd (no negate).
    //   velErr = vel - target gives correct brake-on-release behavior.
    float targetVel = (float)curSpeed / 180.0f * MAX_TARGET_VEL;
    targetVel = std::clamp(targetVel, -MAX_TARGET_VEL, MAX_TARGET_VEL);
    targetVelFilt_ = targetVel;   // no extra slew — Ki acts as natural slew

    // Speed PID — error = (actual - target). With pidInvert in the tilt
    // application stage, this sign gives:
    //   target=0, vel<0 → velErr<0 → integral<0 → speedTilt<0
    //                  → pidInvert: +speedTilt → targetAngle POSITIVE
    //                  → pitch loop drives robot in +vel direction (brake) ✓
    //   target>0, vel=0 → velErr<0 → speedTilt<0 → targetAngle>0
    //                  → robot accelerates in +vel direction (drive) ✓
    float velErr = chassisVelFilt_ - targetVel;
    spdPidIntegral_ += SPD_PID_KI * velErr * LOOP_DT_TARGET;
    spdPidIntegral_ = std::clamp(spdPidIntegral_, -SPD_PID_I_LIMIT, SPD_PID_I_LIMIT);

    // ANTI-WINDUP / BRAKE RELEASE: when user releases stick AND chassis
    // is nearly stopped, decay the integrator quickly. Otherwise leftover
    // integral from extended motion holds a tilt offset that causes drift
    // (saw in logs: spdTilt held +0.80 for seconds after stop, robot
    // crept forward). Decay only when both target=0 AND |vel| small —
    // during active braking we WANT the integral to stay big.
    if (curSpeed == 0 && std::fabs(chassisVelFilt_) < 0.5f) {
        spdPidIntegral_ *= 0.90f;   // ~22 loops (110 ms) to halve
    }

    float speedTiltTarget = velErr * SPD_PID_KP + spdPidIntegral_;
    speedTiltTarget = std::clamp(speedTiltTarget,
                                 -MAX_SPEED_TILT_DEG, MAX_SPEED_TILT_DEG);

    // Tight slew rate — log showed spdTilt jumping from 0 to -0.94° in
    // a single cycle when stick was pushed (cmd 0 → +95), too fast for
    // the pitch loop to track. Limit to 0.02°/loop = 4°/sec ramp.
    const float MAX_TILT_DELTA_PER_LOOP = spdTiltSlew;   // settings.ini'den
    float tiltDelta = speedTiltTarget - speedOffsetFilt_;
    if (tiltDelta >  MAX_TILT_DELTA_PER_LOOP) tiltDelta =  MAX_TILT_DELTA_PER_LOOP;
    if (tiltDelta < -MAX_TILT_DELTA_PER_LOOP) tiltDelta = -MAX_TILT_DELTA_PER_LOOP;
    speedOffsetFilt_ += tiltDelta;

    float speedTilt = speedOffsetFilt_;
    if (pidInvert_) speedTilt = -speedTilt;   // match motor/pid sign convention
    targetAngle_ += speedTilt;

    // ---- Position hold (encoder-based) ----
    //
    // Read current signed encoder counts, normalize sign per wheel, and
    // compute average chassis position (rotational components cancel).
    // Behaviour depends on whether the user is commanding motion:
    //
    //   command active  →  position lock DISABLED (track current position)
    //   command zero    →  position lock ENABLED  (nudge robot back to
    //                       lockedPos_ via an additional target tilt)
    //
    // The added tilt is small and clamped — it nudges the robot back, it
    // doesn't fight pitch balance.
    // (encL/R_eff, chassisPos, chassisVelFilt_ already computed above for
    // speed tilt; we just use them here.)

    // ---- Position hold DISABLED ----
    //
    // In the new B-Robot-style cascade, the Speed PID's integrator
    // handles "stay still" behavior automatically: if the robot drifts,
    // chassisVelFilt_ becomes non-zero with target=0, the integrator
    // accumulates corrective tilt. Position hold layered on top fought
    // with this and was the main source of post-release drift.
    float posTilt = 0.0f;
    lockedPos_ = chassisPos;
    posHoldActive_ = false;

    // ---- PID öğrenme modu: bozucu darbe enjeksiyonu ----
    // Tuner MEASURE fazındayken hedef açıya küçük sanal "joystick itmesi"
    // ekler; maliyet fonksiyonu bu itmeden toparlanma kalitesini ölçer.
    bool learnActive = pidTuner_.isActive();
    if (learnActive) {
        targetAngle_ += pidTuner_.disturbance();
    }

    targetAngle_ = std::clamp(targetAngle_, -12.0f, 12.0f);

    anglePid.setSetpoint(targetAngle_);

    // -------- Dynamic pitch PID gain shaping --------
    //
    // The slider values aggKp/aggKi/aggKd are the *steady-state* gains.
    // Three safeguards reduce specific terms in transient situations
    // to avoid PWM saturation and integral wind-up:
    //
    // (a) Integral wind-up protection (motion):
    //     When the chassis is moving fast (|vel| > 3 ticks/loop ≈ 15 cm/s)
    //     the integrator is paused (Ki → 0). Pitch error during motion
    //     is momentary, not a steady offset to compensate for.
    //
    // (b) Integral wind-up protection (arming):
    //     For the first second after arming, Ki is held at 0. The arm
    //     ramp is scaling PWM down anyway; the integrator would otherwise
    //     accumulate a giant bias by the time PWM is fully available.
    //
    // (c) Kd fade for fast pitch motion:
    //     When the gyro pitch rate is very high (|gyroX| > 100 °/s) the
    //     robot is already swinging hard; adding more D just clips PWM
    //     to the rail. Linear fade above 100°/s, zero above 200°/s.
    // Öğrenme aktifken slider kazançları yerine tuner adayı kullanılır.
    float baseKp = aggKp;
    float baseKi = aggKi * 0.01f;
    float baseKd = aggKd;
    if (learnActive) {
        PidAutoTuner::Gains g = pidTuner_.gains();
        baseKp = g.kp;
        baseKi = g.ki;
        baseKd = g.kd;
    }

    float effKi = baseKi;
    if (std::fabs(chassisVelFilt_) > 3.0f) {
        effKi = 0.0f;
    }
    if (armRampCount_ < ARM_RAMP_SAMPLES) {
        effKi = 0.0f;
    }
    float effKd = baseKd;
    float absGyroX = std::fabs(gyroRateX_);
    if (absGyroX > 100.0f) {
        float fade = std::max(0.0f, 1.0f - (absGyroX - 100.0f) / 100.0f);
        effKd = baseKd * fade;
    }
    anglePid.setTunings(baseKp, effKi, effKd);

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

    // -------- Yaw control (encoder-based) --------
    //
    // When the user is NOT commanding a turn (yawLock on, turnBias 0),
    // hold the chassis heading using the wheel encoder difference:
    //     yawDiff = encR_eff - encL_eff   (signed, in ticks)
    // A non-zero yawDiff means the wheels have spun by different amounts —
    // i.e. the robot has rotated. We lock yawDiff at the moment the turn
    // command ends and apply a PD correction (added to motor PWM) to
    // restore it if the robot drifts off heading.
    //
    // Why this beats gyro Z:
    //   - No bias drift (yawDiff is a direct measurement, no integration).
    //   - Insensitive to vibration (encoder ticks are discrete events).
    //   - Same encoders we already trust for position hold.
    int turnBias = 0;
    const int userTurnL = needTurnL.load();
    const int userTurnR = needTurnR.load();
    int needL = userTurnL;
    int needR = userTurnR;
    if (pidTuner_.isActive() && userTurnL == 0 && userTurnR == 0) {
        const int mt = pidTuner_.motionTurn();   // >0 sol, <0 sag
        if (mt > 0)      { needL = mt;  needR = 0; }
        else if (mt < 0) { needR = -mt; needL = 0; }
    }
    if (needR > 0)      turnBias = +needR;
    else if (needL > 0) turnBias = -needL;

    long yawDiff = encR_eff - encL_eff;
    float yawDiffVelRaw = (float)(yawDiff - lastYawDiff_);
    lastYawDiff_ = yawDiff;
    yawDiffVelFilt_ = (1.0f - YAW_VEL_ALPHA) * yawDiffVelFilt_
                    + YAW_VEL_ALPHA * yawDiffVelRaw;

    float yawCorrection = 0.0f;
    if (turnBias == 0 && yawLock.load()) {
        if (!yawHoldActive_) {
            lockedYawDiff_ = yawDiff;
            yawHoldActive_ = true;
        }
        long yawErr = yawDiff - lockedYawDiff_;
        float pTerm = (float)yawErr           * YAW_GAIN_PWM_PER_TICK;
        float dTerm = yawDiffVelFilt_         * YAW_VEL_GAIN_PWM_PER_TICK_LOOP;
        yawCorrection = pTerm + dTerm;
        // Note: yawInvert_ from settings.ini was for the OLD gyro-Z control
        // loop. The encoder-based loop's sign is established by how
        // yawCorrection is added to / subtracted from rawL/rawR below;
        // do NOT apply yawInvert here.
        yawCorrection = std::clamp(yawCorrection,
                                   (float)-YAW_MAX_PWM, (float)YAW_MAX_PWM);

        // Suppress yaw correction when pitch is under stress — keeping
        // upright is more important than holding heading. Linear fade
        // between 2° and 6° pitch error.
        float absAng = std::fabs(currentAngle_ - targetAngle_);
        float yawScale = 1.0f;
        if (absAng >= 6.0f)      yawScale = 0.0f;
        else if (absAng >= 2.0f) yawScale = (6.0f - absAng) / 4.0f;
        yawCorrection *= yawScale;
    } else {
        // Turn command active OR yaw lock disabled — let lockedYawDiff_
        // chase the current diff so release re-engages cleanly.
        yawHoldActive_ = false;
        lockedYawDiff_ = yawDiff;
    }
    yawCorrection_ = yawCorrection;   // also stored as member for telemetry

    // -------- Motor çıkışları --------
    // Yaw correction sign — VERIFIED CORRECT by bench testing on
    // 2026-05-21 with hand-held robot:
    //   rawL -= yawPwm, rawR += yawPwm  →  robot stays straight
    //   (opposite sign would turn robot to the right).
    int yawPwm = (int)std::round(yawCorrection_);
    int rawL = (int)std::round(pidOut) - turnBias - yawPwm;
    int rawR = (int)std::round(pidOut) + turnBias + yawPwm;

    rawL = std::clamp(rawL, -PWM_LIMIT, PWM_LIMIT);
    rawR = std::clamp(rawR, -PWM_LIMIT, PWM_LIMIT);

    if (motorInvertL_) rawL = -rawL;
    if (motorInvertR_) rawR = -rawR;

    // -------- Slip / airborne detection --------
    //
    // If the wheels are spinning fast (|encoder vel| large) but the robot
    // body is NOT rotating much (|gyroX| small), the wheels have lost
    // traction OR the robot is being held up off the ground. In either
    // case, pumping more PWM is pointless and will cause a violent jolt
    // when traction returns. Cut motor output and let the loop catch up.
    //
    // Thresholds:
    //   |vel| > 8 ticks/loop      ≈ 40 cm/s wheel speed
    //   |gyroX| < 30 °/s          robot body essentially still
    // When both true, suspect slip → output 0 this cycle.
    float velAbsCheck   = std::fabs(chassisVelFilt_);
    float gyroAbsCheck  = std::fabs(gyroRateX_);
    if (velAbsCheck > 8.0f && gyroAbsCheck < 30.0f) {
        rawL = 0;
        rawR = 0;
    }

    // -------- Predictive max-effort recovery --------
    //
    // The product (currentAngle - targetAngle) * gyroRateX is positive when
    // the robot is leaning AND the gyro confirms the lean is accelerating
    // in the same direction. That's the signature of an unrecoverable
    // fall starting to build. The normal PID gain may not catch up in
    // time. When this product exceeds a threshold, slam the motors at
    // max counter-effort to break the divergence before pitch becomes
    // unrecoverable.
    //
    // Threshold: |angle * gyroX| > 1500 = 8° × 188°/s, or 5° × 300°/s.
    // Previously 600 — that turned out to trigger during normal balance
    // motion (small angle + moderate gyro rate is COMMON, not a fall),
    // producing false max-PWM jolts that drove the chassis into
    // sustained oscillation. 1500 reserves this for genuine tip-overs.
    float angErr = currentAngle_ - targetAngle_;
    float divergence = angErr * gyroRateX_;
    if (divergence > 1500.0f) {
        int sign = (angErr > 0) ? +1 : -1;
        if (pidInvert_) sign = -sign;
        rawL = sign * PWM_LIMIT;
        rawR = sign * PWM_LIMIT;
    }

    // Soft start — 1 second linear PWM ramp (200 loops at 200 Hz).
    // Combined with integral suppression below, this prevents the robot
    // from snapping its motors when first armed.
    if (armRampCount_ < ARM_RAMP_SAMPLES) {
        float k = (float)armRampCount_ / (float)ARM_RAMP_SAMPLES;
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

    // ---- Sürüş teşhisi: komut varken 1 Hz özet satırı ----
    // 'gitmeye çalışıyor ama gidemiyor' tarzı şikayetlerde zincirin hangi
    // halkasında koptuğu buradan okunur: cmd -> tgtVel -> vel(enkoder) ->
    // spdTilt -> tgtAng/ang -> pwm. Ör: vel hep 0 + pwm yüksekse motor/
    // sürtünme; spdTilt tavandaysa spdMaxTilt dar; tgtVel küçükse spdMaxVel.
    if (curSpeed != 0 || needL != 0 || needR != 0) {
        if (++driveLogDiv_ >= 200) {
            driveLogDiv_ = 0;
            qDebug("DRIVE cmd=%d tgtVel=%.2f vel=%.2f spdTilt=%.2f tgtAng=%.2f ang=%.2f pwm=%d/%d",
                   curSpeed, (double)targetVelFilt_, (double)chassisVelFilt_,
                   (double)speedOffsetFilt_, (double)targetAngle_,
                   (double)currentAngle_, pwmL_, pwmR_);
        }
    } else {
        driveLogDiv_ = 199;   // komut başlar başlamaz ilk satır hemen gelsin
    }

    // ---- PID öğrenme modu: örnek besle / sonucu işle ----
    if (learnActive) {
        // Yalnizca GERCEK kullanici girisi aday olcumunu yeniden baslatir;
        // tuner'in kendi manevralari testin parcasidir.
        bool userCmd = (userSpeedRaw != 0) || (userTurnL != 0) || (userTurnR != 0);
        int pwmAbs = std::max(std::abs(pwmL_), std::abs(pwmR_));
        bool finished = pidTuner_.feedSample(angErr, gyroRateX_,
                                             pwmAbs, userCmd);
        if (finished) {
            PidAutoTuner::Gains best = pidTuner_.bestGains();
            aggKp = best.kp;
            aggKi = best.ki * 100.0f;   // gerçek ölçekten UI ölçeğine
            aggKd = best.kd;
            saveDirty = true;
            qDebug().noquote() << QString::asprintf(
                "PID learn committed: Kp=%.1f Ki=%.2f Kd=%.3f",
                best.kp, best.ki, best.kd);
        }
    }
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
            t.pidLearning  = pidTuner_.isActive();
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
