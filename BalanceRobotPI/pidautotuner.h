#ifndef PIDAUTOTUNER_H
#define PIDAUTOTUNER_H

#include <QString>
#include <QStringList>
#include <mutex>
#include <atomic>

// -----------------------------------------------------------------------------
// PidAutoTuner — on-line "Twiddle" (coordinate descent) tuner for the pitch
// PID of the balance robot.
//
// How it works:
//   - Started from the mobile app (mPidLearn command) while the robot is
//     balancing on its own (no joystick input, flat hard floor).
//   - Each candidate gain set [Kp, Kd, Ki] is evaluated for a few seconds.
//     During the evaluation window the tuner injects small target-angle
//     DISTURBANCE PULSES (like a virtual joystick push) so the cost function
//     measures exactly the weakness the user reported: recovery from
//     forward/backward command steps.
//   - Cost = tracking error + oscillation (gyro energy) + control effort.
//     A fall during evaluation gives that candidate an infinite cost and the
//     gains revert to the best-known set immediately.
//   - Twiddle shrinks/grows the search deltas until they converge, then the
//     best gains are committed to settings.ini.
//
// Threading model:
//   - begin() / requestStop() / notifyFall() may be called from the BLE (Qt)
//     thread; they only touch atomics.
//   - gains() / disturbance() / feedSample() are called ONLY from the
//     control-loop thread (200 Hz).
//   - Status strings go through a mutex-protected queue and are drained by
//     the BLE telemetry timer.
// -----------------------------------------------------------------------------

class PidAutoTuner
{
public:
    struct Gains {
        float kp;   // e.g. 25.0
        float ki;   // REAL scale (e.g. 0.40 — not the 0..255 UI scale)
        float kd;   // e.g. 0.10
    };

    PidAutoTuner();

    // ---- control (any thread) ----
    void begin(const Gains &current);   // arm the tuner; takes effect next loop
    void requestStop();                 // graceful stop; best gains committed
    void notifyFall();                  // robot fell — fail current candidate
    bool isActive() const { return active_.load(); }

    // ---- control-loop thread only ----
    /// Gains the control loop should use this cycle.
    Gains gains() const { return currentGains_; }
    /// Target-angle disturbance (deg) to add this cycle. 0 outside pulses.
    float disturbance() const { return disturbanceDeg_; }
    /// Advance the state machine with this loop's measurements.
    /// Returns true when the tuner has just FINISHED (converged, stopped or
    /// aborted); the caller should then read bestGains() and commit them.
    bool feedSample(float angleErrDeg, float gyroRateDps, int pwmAbs,
                    bool userCmdActive);

    Gains bestGains() const { return bestGains_; }

    // ---- status queue (any thread) ----
    QString takeStatus();               // "" when queue empty

private:
    enum class Phase { Idle, Settle, Measure };

    void  pushStatus(const QString &s);
    void  startCandidate(const Gains &g);
    float clampGain(int idx, float v) const;
    void  applyParamVector();
    void  finish(const char *reason);
    void  evalDone(float cost);
    void  nextTwiddleStep(bool improved);

    // ---- configuration (loops @ 200 Hz) ----
    static constexpr int   SETTLE_LOOPS       = 400;    // 2.0 s
    static constexpr int   MEASURE_LOOPS      = 1400;   // 7.0 s
    static constexpr int   PULSE1_START       = 300;    // fwd pulse
    static constexpr int   PULSE2_START       = 800;    // back pulse
    static constexpr int   PULSE_LEN          = 70;     // 0.35 s
    static constexpr float PULSE_DEG          = 1.5f;   // virtual push size
    static constexpr int   MAX_EVALS          = 40;     // hard cap (~6 min)
    static constexpr int   MAX_FALLS          = 3;      // safety abort
    static constexpr float CONVERGE_FRAC      = 0.05f;  // sum(dp)/sum(p)

    // Gain bounds: [kp, kd, ki] order (twiddle tunes kd before ki — damping
    // is what fixes the "falls over after a push" symptom).
    static constexpr float GMIN[3] = { 5.0f, 0.010f, 0.05f };
    static constexpr float GMAX[3] = { 60.0f, 0.60f, 1.50f };

    // ---- twiddle state ----
    float  p_[3]  = {0, 0, 0};      // [kp, kd, ki]
    float  dp_[3] = {0, 0, 0};
    int    paramIdx_{0};            // which param is being perturbed
    int    twiddleStage_{0};        // 0 = trying p+dp, 1 = trying p-dp
    bool   baselineDone_{false};
    float  bestCost_{0.0f};
    int    evalCount_{0};
    int    fallCount_{0};

    Gains  currentGains_{25.0f, 0.40f, 0.10f};
    Gains  bestGains_{25.0f, 0.40f, 0.10f};
    Gains  originalGains_{25.0f, 0.40f, 0.10f};

    // ---- evaluation state (control thread) ----
    Phase  phase_{Phase::Idle};
    int    loopCount_{0};
    float  disturbanceDeg_{0.0f};
    double sumErrSq_{0.0};
    double sumRateSq_{0.0};
    double sumPwmSq_{0.0};
    int    nSamples_{0};
    bool   candidateFailed_{false};

    // ---- cross-thread flags ----
    std::atomic<bool> active_{false};
    std::atomic<bool> beginRequest_{false};
    std::atomic<bool> stopRequest_{false};
    std::atomic<bool> fallFlag_{false};
    Gains  beginGains_{25.0f, 0.40f, 0.10f};

    // ---- status queue ----
    std::mutex   statusMutex_;
    QStringList  statusQueue_;
};

#endif // PIDAUTOTUNER_H
