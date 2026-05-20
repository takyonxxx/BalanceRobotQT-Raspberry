#ifndef ROBOTCONTROL_H
#define ROBOTCONTROL_H

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QSettings>
#include <QString>
#include "pid.h"
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
    // ÖNEMLİ ÖLÇEKLEME:
    //   PID v2 türevi gyro°/s ile çarpıyor. Bu yüzden Kd birim ölçeği eski
    //   koddan farklıdır. Eski Kd=1.2 ≈ yeni Kd=0.1 sönüme denk gelir.
    //   Ki ise UI değerinin /100'üyle uygulanır (slider 0..255, gerçek 0..2.55).
    float aggKp{18.0f};
    float aggKi{80.0f};
    float aggKd{0.1f};
    float aggAC{0.0f};

    // SD slider artık yaw PID kazancı. Slider 0..100 → gerçek Kp 0..10.0
    // Başlangıç DÜŞÜK olmalı; çok agresif yaw, denge bozar.
    float aggSD{2.0f};   // slider 20 → gerçek yawKp=0.2

    float trimFine{0.0f};
    float autoZeroIntegral{0.0f};

    float targetAngle_{0.0f};

    int   pwmL_{0};
    int   pwmR_{0};

    // Anti-overshoot / braking after speed command release:
    // - Open-loop part: short opposing tilt pulse to bleed momentum.
    // - Closed-loop part: while braking, also feed back gyroX (pitch rate)
    //   into the target angle. If the robot is still pitching forward
    //   (gyroX in the same direction as the prior command) we lean it
    //   slightly opposite. As the motion dies down gyroX → 0 and the
    //   closed-loop term automatically fades.
    int   prevNeedSpeed_{0};
    int   brakeCounter_{0};
    float brakeBiasAngle_{0.0f};
    int   brakeDirection_{0};        // sign of previous command (+1 / -1)
    static constexpr int   BRAKE_TICKS_AT_5MS = 120;     // ~0.6 s of brake window
    static constexpr float BRAKE_PER_PWM      = 0.010f;  // ° per PWM (open-loop kick)
    // Closed-loop gyro feedback gain. SIGN depends on IMU orientation —
    // if braking pushes the robot the WRONG way (accelerates instead of
    // slowing), flip the sign of this constant.
    static constexpr float BRAKE_GYRO_GAIN    = 0.03f;
    static constexpr float BRAKE_GYRO_MAX     = 2.0f;    // hard cap on closed-loop tilt
    static constexpr float BRAKE_BIAS_LIMIT   = 3.5f;    // hard cap on total brake tilt

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

    static RobotControl* theInstance_;
};

#endif // ROBOTCONTROL_H
