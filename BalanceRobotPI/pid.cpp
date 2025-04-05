#include "pid.h"
#include <wiringPi.h>
#include <algorithm>
#include <limits.h>
#include <cmath>

PID::PID()
    : Cp(0)
    , Ci(0)
    , Cd(0)
    , lastTime(0)
    , lastError(0)
    , setpoint(PIDConstants::ANGLE_SETPOINT)
    , pidTunning(CONSERVATIVE)
{
    // Initialize with conservative tuning by default
    if (pidTunning == CONSERVATIVE) {
        Kp = PIDConstants::ANGLE_KP_CONS;
        Ki = PIDConstants::ANGLE_KI_CONS;
        Kd = PIDConstants::ANGLE_KD_CONS;
    }
}

float PID::compute(float input)
{
    /* Performs a PID computation and returns a control value based on
    the elapsed time (dt) and the error signal from a summing junction
    (the error parameter) */
    unsigned long now = millis();

    // Improved time delta calculation with overflow protection
    float dt;
    if (now >= lastTime) {
        dt = (float)(now - lastTime) / 1000.0f;
    } else {
        // Handle timer overflow
        dt = (float)((ULONG_MAX - lastTime) + now) / 1000.0f;
    }

    // Avoid division by zero and ensure minimum time step
    if (dt < 0.001f) {
        return Cp * Kp + Ci * Ki; // Return last calculation without derivative
    }

    // Calculate error terms
    float error = setpoint - input;

    // Optional: Add deadband for small errors
    if (fabs(error) < 0.1f) {
        error = 0;
    }

    float de = error - lastError;

    // Proportional term
    Cp = error;

    // Integral term with anti-windup
    float potentialCi = Ci + error * dt;
    float potentialOutput = Cp * Kp + potentialCi * Ki;

    // Only update integral if it won't cause windup
    if (std::abs(potentialOutput) < PIDConstants::WINDUP_GUARD) {
        Ci = potentialCi;
    }

    // Derivative term with noise filtering
    // Add low-pass filter to reduce noise sensitivity
    const float alpha = 0.2f;  // Filtering factor (0.0-1.0)
    float rawDerivative = de / dt;
    Cd = alpha * rawDerivative + (1.0f - alpha) * Cd;

    // Save state for next iteration
    lastError = error;
    lastTime = now;

    // Calculate final output
    float output = Cp * Kp + Ci * Ki + Cd * Kd;

    // Apply output limits
    return std::clamp(output, -PIDConstants::WINDUP_GUARD, PIDConstants::WINDUP_GUARD);
}

void PID::setSetpoint(float value)
{
    // Limit setpoint to recoverable angle range
    setpoint = std::clamp(value,
                          -PIDConstants::ANGLE_IRRECOVERABLE,
                          PIDConstants::ANGLE_IRRECOVERABLE);
}

float PID::getSetpoint()
{
    return setpoint;
}

void PID::setPidTuning(PIDTuning tuning)
{
    if (pidTunning == tuning) {
        return; // No change needed
    }

    pidTunning = tuning;

    // Update PID coefficients based on tuning mode
    if (pidTunning == AGGRESSIVE) {
        Kp = PIDConstants::ANGLE_KP_AGGR;
        Ki = PIDConstants::ANGLE_KI_AGGR;
        Kd = PIDConstants::ANGLE_KD_AGGR;
    } else {
        Kp = PIDConstants::ANGLE_KP_CONS;
        Ki = PIDConstants::ANGLE_KI_CONS;
        Kd = PIDConstants::ANGLE_KD_CONS;
    }

    // Reset integral term when changing tuning
    resetIntegral();
}

void PID::setTunings(float newKp, float newKi, float newKd)
{
    // Ensure non-negative gains
    Kp = std::max(0.0f, newKp);
    Ki = std::max(0.0f, newKi);
    Kd = std::max(0.0f, newKd);
}

void PID::resetIntegral()
{
    Ci = 0;
}

// New method to completely reset PID state
void PID::reset()
{
    Cp = 0;
    Ci = 0;
    Cd = 0;
    lastError = 0;
    lastTime = millis();
}
