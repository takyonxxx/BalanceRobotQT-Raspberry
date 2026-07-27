#ifndef ROBOTCONTROL_H
#define ROBOTCONTROL_H

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QSettings>
#include <QString>
#include "pid.h"
#include "pidautotuner.h"
#include "constants.h"
#include "i2cdev.h"
#include "mpu6050.h"
#include "kalman.h"
#include <softPwm.h>
#include <wiringPi.h>
#include <mutex>
#include <atomic>

// -----------------------------------------------------------------------------
// RobotControl — encoder'sız, sadece IMU tabanlı denge robotu kontrolcüsü.
//
// İki kontrol döngüsü:
//   1) Pitch PID  : gyro X + accel kombinasyonu (Kalman) ile açı kestirimi,
//                   açı hatası → ortak motor PWM çıkışı.
//   2) Yaw  PID   : gyro Z ham hızı, hedef = 0 °/s,
//                   çıkış → sol/sağ motor diferansiyeli.
//                   Mekanik dengesizliği telafi eder, robot doğru yönü tutar.
//
// Encoder donanımı ve velocity/position-hold mantığı kasıtlı olarak yok.
// -----------------------------------------------------------------------------

class RobotControl : public QThread {
    Q_OBJECT

public:
    explicit RobotControl(QObject *parent = nullptr);
    ~RobotControl();
    static RobotControl* getInstance();

    bool initGyroMeter();
    bool initwiringPi();

    void stop();

    void loadSettings();
    void saveSettings();

    // Pitch PID kazançları (iç halka - denge)
    float getAggKp() const { return aggKp; }
    void  setAggKp(float v) { aggKp = v; saveDirty = true; }
    float getAggKi() const { return aggKi; }
    void  setAggKi(float v) { aggKi = v; saveDirty = true; }
    float getAggKd() const { return aggKd; }
    void  setAggKd(float v) { aggKd = v; saveDirty = true; }

    // AC = "angle correction" = ana trim (UI slider 0..25.5°)
    float getAggAC() const { return aggAC; }
    void  setAggAC(float v) { aggAC = v; saveDirty = true; }

    // SD slider artık yaw PID Kp olarak yeniden tanımlandı.
    // UI uyumluluğu için aynı slot kullanılıyor.
    float getAggSD() const { return aggSD; }
    void  setAggSD(float v) { aggSD = v; saveDirty = true; }

    // Speed PID parametreleri (B-Robot benzeri cascade için)
    float getSpdKp() const { return spdKp; }
    void  setSpdKp(float v) { spdKp = v; saveDirty = true; }
    float getSpdKi() const { return spdKi; }
    void  setSpdKi(float v) { spdKi = v; saveDirty = true; }
    float getSpdMaxTilt() const { return spdMaxTilt; }
    void  setSpdMaxTilt(float v) { spdMaxTilt = v; saveDirty = true; }
    float getSpdTiltSlew() const { return spdTiltSlew; }
    void  setSpdTiltSlew(float v) { spdTiltSlew = v; saveDirty = true; }
    // Geçici hız tavanı (sesli komut için). 0 = kapalı, spdMaxVel geçerli.
    // Kalıcı DEĞİLDİR: settings.ini'ye yazılmaz, joystick ayarına dokunmaz.
    void  setTempMaxVel(float v) { tempMaxVel_.store(v); }
    float getSpdMaxVel() const { return spdMaxVel; }
    void  setSpdMaxVel(float v) { spdMaxVel = v; saveDirty = true; }

    // Komutlu hareket (BLE'den)
    std::atomic<int> needSpeed{0};
    std::atomic<int> needTurnL{0};
    std::atomic<int> needTurnR{0};
    int  getNeedSpeed() const { return needSpeed.load(); }
    void setNeedSpeed(int v) { needSpeed.store(v); }
    int  getNeedTurnL() const { return needTurnL.load(); }
    void setNeedTurnL(int v) { needTurnL.store(v); }
    int  getNeedTurnR() const { return needTurnR.load(); }
    void setNeedTurnR(int v) { needTurnR.store(v); }

    // Arm/disarm
    bool getIsArmed() const { return isArmed.load(); }
    void setIsArmed(bool armed);

    // Auto mode (oto-arm + oto-recovery)
    bool getAutoMode() const { return autoMode.load(); }
    void setAutoMode(bool on) { autoMode.store(on); }

    // Yaw kilidi (otomatik doğrultu tutma) — varsayılan açık.
    // UI'da position-hold yerine yaw-hold olarak işlev görür.
    bool getPositionHold() const { return yawLock.load(); }
    void setPositionHold(bool on) { yawLock.store(on); }

    // Hassas trim (signed, °)
    float getTrimFine() const { return trimFine; }
    void  setTrimFine(float v) { trimFine = v; saveDirty = true; }

    void resetTrim();

    // PID öğrenme modu (mobil taraftan mPidLearn ile tetiklenir).
    // Gerçek iş kontrol döngüsü thread'inde yapılır; buradaki çağrılar
    // sadece atomik istek bayrakları set eder.
    void startPidLearning();
    void stopPidLearning();
    bool isPidLearning() const { return pidTuner_.isActive(); }
    QString takePidLearnStatus() { return pidTuner_.takeStatus(); }

    // Telemetri (BLE üzerinden)
    struct Telemetry {
        float angle;        // °  - pitch
        float gyroRate;     // °/s - pitch rate
        float yawRate;      // °/s - yaw rate (sapma)
        float targetAngle;  // °
        float trim;         // °
        int   pwmL;
        int   pwmR;
        bool  armed;
        bool  fallen;
        bool  autoMode;
        bool  positionHold;  // -> yaw lock state
        bool  pidLearning;   // PID öğrenme modu aktif mi
    };
    Telemetry getTelemetry();

private:
    // Adımlar
    void readImu();
    void updateEstimates(float dt);
    void controlLoop(float dt);
    void applyMotors(int pwmL, int pwmR);
    void stopMotors();
    void calibrateGyroBias();

    bool isUpright() const;
    bool isFallen() const;
    void resetControlState();

    void run() override;

    // -------- Bileşenler --------
    PID    anglePid;
    PID    yawPid;
    PidAutoTuner pidTuner_;
    MPU6050* gyroMPU{nullptr};
    Kalman   kalman;

    // -------- Sabitler --------
    static constexpr float RAD_TO_DEG = 57.29577951f;
    static constexpr int   PWM_LIMIT  = 255;
    static constexpr float ARM_TILT_THRESHOLD  = 8.0f;
    static constexpr float FALL_TILT_THRESHOLD = 40.0f;
    static constexpr int   ARM_STABLE_SAMPLES  = 60;
    static constexpr float LOOP_DT_TARGET      = 0.005f;
    static constexpr int   LOOP_DT_US          = 5000;
    static constexpr int   ARM_RAMP_SAMPLES    = 200;   // 1 sec @ 200 Hz

    // -------- IMU --------
    int16_t ax_{0}, ay_{0}, az_{0};
    int16_t gx_{0}, gy_{0}, gz_{0};
    float   gyroBiasX_{0.0f};
    float   gyroBiasY_{0.0f};
    float   gyroBiasZ_{0.0f};
    bool    gyroCalibrated_{false};

    float   accelAngle_{0.0f};
    float   gyroRateX_{0.0f};   // pitch
    float   gyroRateZ_{0.0f};   // yaw ham
    float   gyroRateZFilt_{0.0f}; // yaw LPF
    float   currentAngle_{0.0f};
    float   yawCorrection_{0.0f};

    // -------- Pitch PID kazançları --------
    // Default PID gains — keep these in sync with loadSettings() defaults
    // in robotcontrol.cpp. These values are used when no settings.ini
    // exists yet (constructor writes them out via saveSettings()).
    //   Kp=25, Ki=40 (UI scale → real Ki=0.4), Kd=0.10
    float aggKp{29.2f};
    float aggKi{64.0f};
    float aggKd{0.103f};
    float aggAC{0.0f};

    // Legacy: SD slider field kept for iOS-side compatibility but not
    // applied by the encoder yaw loop. Default left at 2.0 just so the
    // BLE getter returns something sensible.
    float aggSD{2.0f};

    // Speed PID defaults — settings.ini'den yüklenir, iOS Settings'ten ayarlanabilir.
    // Bu defaults önceki bench testte stabil çalışan değerler.
    //   spdKp=0.12, spdKi=0.20, spdMaxTilt=5°, spdMaxVel=3.0
    float spdKp{0.12f};
    float spdKi{0.20f};
    float spdMaxTilt{5.0f};
    // Hedef eğim rampa hızı (derece/döngü). 0.02=4°/s (çok nazik),
    // 0.04=8°/s (canlı). Sesli/joystick komutlarının ne kadar çabuk
    // ivmelendiğini belirler; settings.ini spdTiltSlew.
    float spdTiltSlew{0.04f};
    std::atomic<float> tempMaxVel_{0.0f};
    float spdMaxVel{3.0f};

    float trimFine{0.0f};
    float autoZeroIntegral{0.0f};

    float targetAngle_{0.0f};

    int   pwmL_{0};
    int   pwmR_{0};

    // Speed command low-pass filter — prevents the joystick from injecting
    // a step into the motor PWM, which would shake the robot. Filtered
    // value ramps toward needSpeed.
    float speedOffsetFilt_{0.0f};

    // Speed command transition tracking (debounced release detection).
    // BLE delivers speed updates at ~20 Hz; a single glitched zero
    // frame between commands should NOT count as a release. We require
    // a few consecutive zero frames before declaring the command released.
    int   prevNeedSpeed_{0};
    int   zeroSpeedFrames_{0};
    int   lastCommitedSpeed_{0};

    // Anti-momentum brake pulse: when speed command is released, briefly
    // tilt the target the OPPOSITE way to bleed forward/backward motion.
    int   brakePulseFrames_{0};
    float brakePulseValue_{0.0f};

    // Position hold (encoder-based).
    // Wheel ticks are read from the static volatile counters; sign
    // depends on motor wiring and is normalized via encoderInvertL/R.
    // When no speed command is active, lockedPos_ pins the chassis
    // location and a small target-angle tilt is generated to nudge the
    // robot back. When the user IS commanding motion, lockedPos_ tracks
    // the current position so release is seamless.
    //
    // Calibration (measured from run logs):
    //   ~19 ticks/cm (1 m push ≈ 1886 ticks; previously 21 t/cm from
    //   a 3 m slide that included some wheel slip)
    //   → 1 tick ≈ 5.3 mm
    //   → SAFETY threshold 1500 ticks ≈ 79 cm
    //
    // Tuning history:
    //   - P-only: oscillation that grew (robot passed lock with leftover speed)
    //   - PD with weak D (P=0.002, D=0.007): 2-3 cycles to settle,
    //     ~12 cm steady-state offset
    //   - PD with strong D (P=0.003, D=0.18): limit-cycle oscillation,
    //     huge velocities, PWM saturated, eventual fall
    //   - PD with calmer gains (P=0.0015, D=0.09, MAX_TILT=1.0):
    //     stays subordinate to pitch balance; pos PID can't break it.
    //     Re-lock SAFETY accepts >75 cm drift instead of fighting it.
    bool  encoderInvertL_{true};
    bool  encoderInvertR_{true};
    long  lockedPos_{0};
    bool  posHoldActive_{false};
    long  lastChassisPos_{0};
    float chassisVelFilt_{0.0f};
    float spdPidIntegral_{0.0f};   // integral term for the speed PID
    float targetVelFilt_{0.0f};    // slew-limited target velocity
    static constexpr float POS_GAIN_DEG_PER_TICK              = 0.0015f;
    static constexpr float POS_VEL_GAIN_DEG_PER_TICK_PER_LOOP = 0.09f;
    static constexpr float POS_VEL_ALPHA                      = 0.5f;
    static constexpr float POS_MAX_TILT_DEG                   = 1.0f;

    // Yaw hold (encoder-based — replaces gyro-Z rate control).
    //
    // Gyro Z drifts and is noisy. Wheel encoders give the *actual* yaw
    // angle as (encR - encL) — no integration drift since it's a direct
    // tick difference. When the user isn't commanding a turn, lock the
    // current (encR - encL) and apply a PD correction (turnBias) to
    // restore it if the robot drifts off course.
    //
    // Tuning (initial guesses):
    //   YAW_P: 100 tick offset (~50 cm wheel separation walked off
    //          course ~5 cm sideways) → 15 PWM turnBias. P = 0.15
    //   YAW_D: 5 tick/loop rotation rate → 10 PWM damping. D = 2.0
    long  lockedYawDiff_{0};            // (encR_eff - encL_eff) at lock
    bool  yawHoldActive_{false};
    float yawDiffVelFilt_{0.0f};
    long  lastYawDiff_{0};
    static constexpr float YAW_GAIN_PWM_PER_TICK          = 0.15f;
    static constexpr float YAW_VEL_GAIN_PWM_PER_TICK_LOOP = 2.0f;
    static constexpr float YAW_VEL_ALPHA                  = 0.5f;
    static constexpr int   YAW_MAX_PWM                    = 30;

    // İşaret bayrakları
    bool pidInvert_{true};
    bool motorInvertL_{false};
    bool motorInvertR_{false};
    bool yawInvert_{true};   // yaw düzeltmesi işareti — yanlış yönde olursa false yap

    // Arm state
    std::atomic<bool> isArmed{false};
    std::atomic<bool> autoMode{true};
    std::atomic<bool> yawLock{true};      // Yaw kilidi (eski positionHold yerine)
    bool  fallen_{false};
    int   uprightSampleCount_{0};
    int   armRampCount_{0};

    // Telemetri snapshot
    mutable std::mutex telMutex_;
    Telemetry latestTelemetry_{};

    // Loop / dosyalar
    bool  m_stop{false};
    bool  saveDirty{false};
    QString m_sSettingsFile;
    QString m_sBiasFile;

    // Encoder diagnostic ISRs (wiringPi callbacks must be static / free).
    // Quadrature: each ISR reads the companion pin to determine direction
    // and increments OR decrements a signed tick counter.
    static void encLeftISR();
    static void encRightISR();
    static volatile long encLeftTicks_;
    static volatile long encRightTicks_;

    static RobotControl* theInstance_;
};

#endif // ROBOTCONTROL_H
