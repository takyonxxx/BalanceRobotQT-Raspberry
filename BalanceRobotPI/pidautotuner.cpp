#include "pidautotuner.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <limits>

// Out-of-class definitions for the constexpr arrays (ODR-used in clamp).
constexpr float PidAutoTuner::GMIN[3];
constexpr float PidAutoTuner::GMAX[3];

PidAutoTuner::PidAutoTuner() = default;

// ---------------------------------------------------------------------------
// Control (any thread)
// ---------------------------------------------------------------------------

void PidAutoTuner::begin(const Gains &current)
{
    if (active_.load()) return;
    beginGains_ = current;
    beginRequest_.store(true);
    active_.store(true);
    pushStatus(QString::asprintf(
        "LEARN start Kp=%.1f Ki=%.2f Kd=%.3f",
        current.kp, current.ki, current.kd));
}

void PidAutoTuner::requestStop()
{
    if (!active_.load()) return;
    stopRequest_.store(true);
}

void PidAutoTuner::notifyFall()
{
    if (!active_.load()) return;
    fallFlag_.store(true);
}

// ---------------------------------------------------------------------------
// Status queue
// ---------------------------------------------------------------------------

void PidAutoTuner::pushStatus(const QString &s)
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    statusQueue_.append(s);
    // Keep the queue bounded; BLE drains one per telemetry tick.
    while (statusQueue_.size() > 12) statusQueue_.removeFirst();
    qDebug().noquote() << "[PIDLEARN]" << s;
}

QString PidAutoTuner::takeStatus()
{
    std::lock_guard<std::mutex> lk(statusMutex_);
    if (statusQueue_.isEmpty()) return QString();
    return statusQueue_.takeFirst();
}

// ---------------------------------------------------------------------------
// Internal helpers (control thread)
// ---------------------------------------------------------------------------

float PidAutoTuner::clampGain(int idx, float v) const
{
    return std::clamp(v, GMIN[idx], GMAX[idx]);
}

void PidAutoTuner::applyParamVector()
{
    currentGains_.kp = clampGain(0, p_[0]);
    currentGains_.kd = clampGain(1, p_[1]);
    currentGains_.ki = clampGain(2, p_[2]);
}

void PidAutoTuner::startCandidate(const Gains &g)
{
    currentGains_ = g;
    phase_ = Phase::Settle;
    loopCount_ = 0;
    disturbanceDeg_ = 0.0f;
    sumErrSq_ = 0.0;
    sumRateSq_ = 0.0;
    sumPwmSq_ = 0.0;
    nSamples_ = 0;
    candidateFailed_ = false;
}

void PidAutoTuner::finish(const char *reason)
{
    phase_ = Phase::Idle;
    disturbanceDeg_ = 0.0f;
    currentGains_ = bestGains_;
    active_.store(false);
    stopRequest_.store(false);
    fallFlag_.store(false);
    pushStatus(QString::asprintf(
        "LEARN done (%s) best=%.1f Kp=%.1f Ki=%.2f Kd=%.3f",
        reason, bestCost_, bestGains_.kp, bestGains_.ki, bestGains_.kd));
}

// Twiddle bookkeeping after each candidate evaluation.
void PidAutoTuner::evalDone(float cost)
{
    evalCount_++;

    if (!baselineDone_) {
        // First evaluation measures the STARTING gains — this is the
        // reference the search has to beat.
        baselineDone_ = true;
        bestCost_ = cost;
        bestGains_ = currentGains_;
        pushStatus(QString::asprintf("baseline cost=%.1f", cost));
        // Begin perturbing param 0 (Kp), stage 0 (p+dp).
        paramIdx_ = 0;
        twiddleStage_ = 0;
        p_[paramIdx_] += dp_[paramIdx_];
        applyParamVector();
        startCandidate(currentGains_);
        return;
    }

    bool improved = std::isfinite(cost) && (cost < bestCost_);
    if (improved) {
        bestCost_ = cost;
        bestGains_ = currentGains_;
    }

    pushStatus(QString::asprintf(
        "eval %d/%d p%d%s cost=%.1f best=%.1f Kp=%.1f Ki=%.2f Kd=%.3f",
        evalCount_, MAX_EVALS, paramIdx_, twiddleStage_ == 0 ? "+" : "-",
        std::isfinite(cost) ? cost : -1.0f, bestCost_,
        currentGains_.kp, currentGains_.ki, currentGains_.kd));

    nextTwiddleStep(improved);
}

void PidAutoTuner::nextTwiddleStep(bool improved)
{
    // Standard Twiddle:
    //   stage 0: tried p+dp.  better -> dp*=1.1, next param.
    //                         worse  -> try p-dp (stage 1).
    //   stage 1: tried p-dp.  better -> dp*=1.1, next param.
    //                         worse  -> restore p, dp*=0.9, next param.
    bool advanceParam = true;

    if (twiddleStage_ == 0) {
        if (improved) {
            dp_[paramIdx_] *= 1.1f;
        } else {
            float minusVal = p_[paramIdx_] - 2.0f * dp_[paramIdx_];
            if (clampGain(paramIdx_, minusVal) == minusVal) {
                // Minus side is inside the bounds - evaluate it next.
                p_[paramIdx_] = minusVal;
                twiddleStage_ = 1;
                advanceParam = false;
            } else {
                // Minus side would leave the bounds - restore and shrink.
                p_[paramIdx_] -= dp_[paramIdx_];
                dp_[paramIdx_] *= 0.9f;
            }
        }
    } else { // stage 1 result
        if (improved) {
            dp_[paramIdx_] *= 1.1f;
        } else {
            p_[paramIdx_] += dp_[paramIdx_];       // restore original
            dp_[paramIdx_] *= 0.9f;
        }
    }

    // Convergence / caps - checked before launching the next candidate.
    float sumDp = dp_[0] / std::max(p_[0], 1e-3f)
                + dp_[1] / std::max(p_[1], 1e-3f)
                + dp_[2] / std::max(p_[2], 1e-3f);
    if (sumDp < 3.0f * CONVERGE_FRAC) {
        finish("converged");
        return;
    }
    if (evalCount_ >= MAX_EVALS) {
        finish("max evals");
        return;
    }

    if (advanceParam) {
        twiddleStage_ = 0;
        paramIdx_ = (paramIdx_ + 1) % 3;
        p_[paramIdx_] += dp_[paramIdx_];
    }
    applyParamVector();
    startCandidate(currentGains_);
}

// ---------------------------------------------------------------------------
// Per-loop entry point (control thread, 200 Hz)
// ---------------------------------------------------------------------------

bool PidAutoTuner::feedSample(float angleErrDeg, float gyroRateDps,
                              int pwmAbs, bool userCmdActive)
{
    if (!active_.load()) return false;

    // Deferred begin — runs in the control thread so all state is owned here.
    if (beginRequest_.exchange(false)) {
        originalGains_ = beginGains_;
        bestGains_     = beginGains_;
        bestCost_      = 1e12f;
        baselineDone_  = false;
        evalCount_     = 0;
        fallCount_     = 0;
        // Parameter vector [kp, kd, ki] and initial search deltas.
        p_[0] = beginGains_.kp;
        p_[1] = beginGains_.kd;
        p_[2] = beginGains_.ki;
        dp_[0] = std::max(0.15f * p_[0], 1.0f);     // Kp: ±15 %
        dp_[1] = std::max(0.30f * p_[1], 0.02f);    // Kd: ±30 %
        dp_[2] = std::max(0.30f * p_[2], 0.05f);    // Ki: ±30 %
        applyParamVector();
        startCandidate(currentGains_);
    }

    // User asked to stop from the app.
    if (stopRequest_.load()) {
        finish("stopped by user");
        return true;
    }

    // A fall anywhere during a candidate — infinite cost, revert, continue
    // (up to MAX_FALLS, then abort with the best-so-far).
    if (fallFlag_.exchange(false)) {
        fallCount_++;
        pushStatus(QString::asprintf("FALL during eval (%d/%d)",
                                     fallCount_, MAX_FALLS));
        if (fallCount_ >= MAX_FALLS) {
            finish("too many falls");
            return true;
        }
        if (baselineDone_) {
            candidateFailed_ = true;
            evalDone(std::numeric_limits<float>::infinity());
        } else {
            // Fell while measuring the baseline — restart the baseline.
            startCandidate(currentGains_);
        }
        return !active_.load();
    }

    // Joystick touched mid-evaluation → measurements are contaminated.
    // Restart the current candidate from the settle phase.
    if (userCmdActive) {
        if (phase_ == Phase::Measure || loopCount_ > 0) {
            startCandidate(currentGains_);
        }
        return false;
    }

    switch (phase_) {
    case Phase::Idle:
        return false;

    case Phase::Settle:
        loopCount_++;
        if (loopCount_ >= SETTLE_LOOPS) {
            phase_ = Phase::Measure;
            loopCount_ = 0;
        }
        return false;

    case Phase::Measure: {
        loopCount_++;

        // Disturbance pulses — a virtual forward push then a backward push.
        // This is what makes the cost function reward gains that recover
        // from joystick steps instead of only rewarding standing still.
        if (loopCount_ >= PULSE1_START && loopCount_ < PULSE1_START + PULSE_LEN)
            disturbanceDeg_ = +PULSE_DEG;
        else if (loopCount_ >= PULSE2_START && loopCount_ < PULSE2_START + PULSE_LEN)
            disturbanceDeg_ = -PULSE_DEG;
        else
            disturbanceDeg_ = 0.0f;

        sumErrSq_  += (double)angleErrDeg * (double)angleErrDeg;
        sumRateSq_ += (double)gyroRateDps * (double)gyroRateDps;
        sumPwmSq_  += (double)pwmAbs * (double)pwmAbs;
        nSamples_++;

        if (loopCount_ >= MEASURE_LOOPS) {
            disturbanceDeg_ = 0.0f;
            // Cost weighting:
            //   err²  : tracking accuracy         (deg²,   ~0.1..4)   ×100
            //   rate² : oscillation / jitter      (dps²,   ~50..2000) ×0.05
            //   pwm²  : control effort / chatter  (pwm²,   ~100..8000)×0.002
            float meanErr  = (float)(sumErrSq_  / std::max(nSamples_, 1));
            float meanRate = (float)(sumRateSq_ / std::max(nSamples_, 1));
            float meanPwm  = (float)(sumPwmSq_  / std::max(nSamples_, 1));
            float cost = 100.0f * meanErr + 0.05f * meanRate + 0.002f * meanPwm;
            evalDone(cost);
            return !active_.load();
        }
        return false;
    }
    }
    return false;
}
