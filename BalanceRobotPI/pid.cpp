
#include "pid.h"
#include <wiringPi.h>

PID::PID()
{
    this->Ci=0;
    this->lastTime=0;
    this->lastError=0;
}

float PID::compute(float input)
{
    /* Performs a PID computation and returns a control value based on
    the elapsed time (dt) and the error signal from a summing junction
    (the error parameter)*/
    unsigned long now = millis();
    float dt;
    float error;
    float de;
    float output;

    /* Calculate delta time (seconds) */
    dt = (float)(now - lastTime)/1000.0f;
    //Serial.println("dt: " + String(dt));

    /* Calculate delta error */
    error = setpoint - input;

    de = error - lastError;
    //Serial.println("input: " + String(input));
    //Serial.println("error: " + String(error));
    //Serial.println("de: " + String(de));

    /* Proportional Term */
    Cp = error;
    //Serial.println("cp: " + String(Cp));

    /* Integral Term */
    Ci += error*dt;
    //Serial.println("ci: " + String(Ci));

    Cd = 0;
    /* to avoid division by zero */
    if(dt>0)
    {
        /* Derivative term */
        Cd = de/dt;
        //Serial.println("cd: " + String(Cd));
    }

    /* Save for the next iteration */
    lastError = error;
    lastTime = now;

    /* Sum terms: pTerm+iTerm+dTerm */
    output = Cp*Kp + Ci*Ki + Cd*Kd;
    //Serial.println("output: " + String(output));

    /* Saturation - Windup guard for Integral term do not reach very large values */
    if(output > WINDUP_GUARD){
        output = WINDUP_GUARD;
    }
    else if (output < -WINDUP_GUARD){
        output = -WINDUP_GUARD;
    }

    return output;
}

void PID::setSetpoint(float value)
{
    this->setpoint = value;
}

float PID::getSetpoint()
{
    return setpoint;
}

void PID::setPidTuning(PIDTuning tunning)
{
    pidTunning = tunning;
}

void PID::setTunings(float Kp, float Ki, float Kd)
{
    this->Kp = Kp;
    this->Ki = Ki;
    this->Kd = Kd;
}
