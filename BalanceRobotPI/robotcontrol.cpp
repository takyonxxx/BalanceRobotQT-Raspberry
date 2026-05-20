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
    // Defaults below are the values found most stable after extensive
    // bench testing with the encoder-based position hold enabled.
    //   Kp=25, Ki=40 (UI scale → real Ki=0.4), Kd=0.10
    aggKp     = settings.value("aggKp",     25.0f).toFloat();
    aggKi     = settings.value("aggKi",     40.0f).toFloat();
    aggKd     = settings.value("aggKd",      0.10f).toFloat();
    aggSD     = settings.value("aggSD",      2.0f).toFloat();   // yaw Kp
    aggAC     = settings.value("angleCorrection", 0.0f).toFloat();
    trimFine  = settings.value("trimFine",   0.0f).toFloat();
    autoZeroIntegral = settings.value("autoZero", 0.0f).toFloat();

    pidInvert_    = settings.value("pidInvert",    true ).toBool();
    motorInvertL_ = settings.value("motorInvertL", false).toBool();
    motorInvertR_ = settings.value("motorInvertR", false).toBool();
    yawInvert_    = settings.value("yawInvert",    true ).toBool();
    encoderInvertL_ = settings.value("encoderInvertL", true).toBool();
    encoderInvertR_ = settings.value("encoderInvertR", true).toBool();

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
    // Position hold — re-lock at whatever the current chassis position is.
    long encL_eff = encoderInvertL_ ? -encLeftTicks_  : encLeftTicks_;
    long encR_eff = encoderInvertR_ ? -encRightTicks_ : encRightTicks_;
    lockedPos_ = (encL_eff + encR_eff) / 2;
    lastChassisPos_ = lockedPos_;
    chassisVelFilt_ = 0.0f;
    posHoldActive_ = false;
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
    int curSpeedRaw = needSpeed.load();
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

    // Target tilt from speed command:
    //   needSpeed -180 → target tilt +Xdeg (forward lean)
    //   needSpeed +180 → target tilt -Xdeg (backward lean)
    //
    // CRITICAL: a static MAX_SPEED_TILT means constant acceleration.
    // The robot keeps speeding up until pitch can't keep up and falls.
    // Solution: as the chassis builds speed in the COMMANDED direction,
    // reduce the tilt — so the system targets a *speed*, not endless
    // acceleration. The encoder velocity gives us this signal cleanly.
    constexpr float MAX_SPEED_TILT_DEG = 2.0f;
    constexpr float CRUISE_VEL_THRESHOLD = 3.0f;   // ticks/loop — start fading at ~15 cm/s
    constexpr float STOP_VEL_THRESHOLD   = 6.0f;   // ticks/loop — zero tilt at ~30 cm/s

    float baseTilt = -(float)curSpeed / 180.0f * MAX_SPEED_TILT_DEG;
    baseTilt = std::clamp(baseTilt, -MAX_SPEED_TILT_DEG, MAX_SPEED_TILT_DEG);

    // Velocity in the same sign convention as baseTilt.
    // baseTilt sign: forward command → positive baseTilt (before pidInvert)
    // velocity sign: forward motion → positive chassisVel (encoders normalized)
    // So a velocity matching the command direction has the SAME sign as
    // baseTilt — we fade tilt when |vel| is in the requested direction.
    float velSameDir = (baseTilt >= 0) ? chassisVelFilt_ : -chassisVelFilt_;
    float speedTiltTarget = baseTilt;
    if (velSameDir > CRUISE_VEL_THRESHOLD) {
        float fade = std::max(0.0f,
            1.0f - (velSameDir - CRUISE_VEL_THRESHOLD) /
                   (STOP_VEL_THRESHOLD - CRUISE_VEL_THRESHOLD));
        speedTiltTarget = baseTilt * fade;
    }

    // SYMMETRIC smoothing — ramps both up AND down slowly.
    constexpr float TILT_ALPHA = 0.04f;   // ~125 ms time constant
    speedOffsetFilt_ += TILT_ALPHA * (speedTiltTarget - speedOffsetFilt_);

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

    float posTilt = 0.0f;
    if (curSpeed == 0 && zeroSpeedFrames_ > 20) {
        // Truly stationary command. Two-phase engagement:
        //   1) "Coast" phase — chassis is still moving fast from prior
        //      acceleration. Don't engage HOLD; let pitch loop do its
        //      job and let motion bleed off naturally. Track lockedPos_
        //      so we'll engage at the *current* point once it slows.
        //   2) "Hold" phase — chassis slow enough that the position
        //      controller can pull it back without provoking pitch
        //      saturation. Engage HOLD normally.
        //
        // The 5 tick/loop threshold (~25 cm/s at 19 t/cm calibration)
        // is well below the safe range where pos PID can act without
        // upsetting pitch.
        float velAbs = std::fabs(chassisVelFilt_);
        if (!posHoldActive_ && velAbs > 5.0f) {
            // Still coasting — keep lockedPos_ chasing current position
            // so HOLD engages right where the robot eventually stops.
            lockedPos_ = chassisPos;
        } else {
            // Either already in HOLD (committed) or chassis is slow
            // enough to engage now.
            if (!posHoldActive_) {
                lockedPos_ = chassisPos;
                posHoldActive_ = true;
            }
            long posErr = chassisPos - lockedPos_;
            
            // SAFETY: if the error is huge (> ~75 cm), the robot is way off.
            // Chasing it back hard risks losing pitch balance. Re-lock at
            // current position and start over — accept the drift, stay upright.
            if (std::abs(posErr) > 1500) {
                lockedPos_ = chassisPos;
                posErr = 0;
            }
            
            // P term: corrects position offset
            float pTerm = (float)posErr * POS_GAIN_DEG_PER_TICK;
            // D term: damps velocity. Velocity here is ticks per ~5 ms loop.
            // SAFEGUARD: when the chassis is already moving very fast (high
            // |vel|), the robot is in a recovery swing — adding more counter-
            // tilt via D only fights the pitch loop and risks saturation.
            // Fade D out as |vel| grows past 4 ticks/loop.
            float dGain  = POS_VEL_GAIN_DEG_PER_TICK_PER_LOOP;
            if (velAbs > 4.0f) {
                // Linear fade: 4 → full gain, 8+ → zero
                float fade = std::max(0.0f, 1.0f - (velAbs - 4.0f) / 4.0f);
                dGain *= fade;
            }
            float dTerm = chassisVelFilt_ * dGain;
            posTilt = pTerm + dTerm;
            posTilt = std::clamp(posTilt, -POS_MAX_TILT_DEG, POS_MAX_TILT_DEG);
        }
    } else {
        // Motion command active — release the lock and let lockedPos_
        // chase the current position so the next release "starts here".
        if (posHoldActive_) {
            posHoldActive_ = false;
        }
        lockedPos_ = chassisPos;
    }
    // Apply position tilt with same sign convention as speed tilt.
    if (pidInvert_) posTilt = -posTilt;
    targetAngle_ += posTilt;

    targetAngle_ = std::clamp(targetAngle_, -12.0f, 12.0f);

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

    // -------- Motor çıkışları --------
    // Note: speed command no longer added here — it's baked into targetAngle_
    // above, so pidOut already contains the motion-driving component via the
    // tilt error term.
    int yawDiff = (int)std::round(yawCorrection_);
    int rawL = (int)std::round(pidOut) - turnBias - yawDiff;
    int rawR = (int)std::round(pidOut) + turnBias + yawDiff;

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
