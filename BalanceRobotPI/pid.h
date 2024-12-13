#ifndef PID_H
#define PID_H

namespace PIDConstants {
constexpr float SPEED_SETPOINT = 0.0f;
constexpr float SPEED_KP = 0.0f;
constexpr float SPEED_KI = 0.0f;
constexpr float SPEED_KD = 0.0f;
constexpr float ANGLE_SETPOINT = 0.0f;
constexpr float ANGLE_LIMIT = 45.0f;
constexpr float ANGLE_KP_AGGR = 0.0f;
constexpr float ANGLE_KI_AGGR = 0.0f;
constexpr float ANGLE_KD_AGGR = 0.0f;
constexpr float ANGLE_KP_CONS = 2.0f;
constexpr float ANGLE_KI_CONS = 0.0f;
constexpr float ANGLE_KD_CONS = 0.0f;
constexpr float ANGLE_IRRECOVERABLE = 45.0f;
constexpr float CALIBRATED_ZERO_ANGLE = 0.0f;
constexpr float WINDUP_GUARD = 100.0f;
}

enum PIDTuning {CONSERVATIVE, AGGRESSIVE};

class PID
{
public:
    PID();
    float compute(float input);
    void setSetpoint(float setpoint);
    float getSetpoint();
    void setPidTuning(PIDTuning tunning);
    void setTunings(float Kp, float Ki, float Kd);
    void resetIntegral() {
        Ci = 0;
    }

private:
    PIDTuning pidTunning{CONSERVATIVE};
    float lastError{0.0f};
    unsigned long lastTime{0};
    float setpoint{0.0f};
    float Cp{0.0f};
    float Ci{0.0f};
    float Cd{0.0f};
    float Kp{0.0f};
    float Ki{0.0f};
    float Kd{0.0f};
};

#endif // PID_H
