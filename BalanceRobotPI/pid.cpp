#include "pid.h"
#include <wiringPi.h>
#include <algorithm>
#include <cmath>
#include <climits>

PID::PID()
{
    reset();
}

void PID::setSetpoint(float value)
{
    setpoint_ = std::clamp(value,
                           -PIDConstants::ANGLE_IRRECOVERABLE,
                            PIDConstants::ANGLE_IRRECOVERABLE);
}

void PID::setTunings(float Kp, float Ki, float Kd)
{
    Kp_ = std::max(0.0f, Kp);
    Ki_ = std::max(0.0f, Ki);
    Kd_ = std::max(0.0f, Kd);
}

void PID::setOutputLimit(float limit)
{
    outputLimit_ = std::max(1.0f, std::fabs(limit));
}

void PID::setDeadband(float band)
{
    deadband_ = std::max(0.0f, band);
}

void PID::setDerivativeFilter(float alpha)
{
    derivAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
}

void PID::resetIntegral()
{
    integral_ = 0.0f;
    lastI_ = 0.0f;
}

void PID::reset()
{
    Kp_ = 0.0f;
    Ki_ = 0.0f;
    Kd_ = 0.0f;
    integral_ = 0.0f;
    lastInput_ = 0.0f;
    lastDerivFiltered_ = 0.0f;
    setpoint_ = 0.0f;
    lastTimeUs_ = 0;
    firstCompute_ = true;
    lastP_ = lastI_ = lastD_ = 0.0f;
}

float PID::compute(float input, float measuredRate)
{
    // dt'yi microsaniye çözünürlüklü hesapla
    unsigned long nowUs = micros();
    float dt;
    if (firstCompute_) {
        firstCompute_ = false;
        lastTimeUs_ = nowUs;
        lastInput_ = input;
        return 0.0f;
    }

    if (nowUs >= lastTimeUs_) {
        dt = (float)(nowUs - lastTimeUs_) * 1.0e-6f;
    } else {
        // micros() overflow
        dt = (float)((ULONG_MAX - lastTimeUs_) + nowUs) * 1.0e-6f;
    }
    lastTimeUs_ = nowUs;

    // Çok küçük veya çok büyük dt'lerden korun
    if (dt < 1.0e-5f) dt = 1.0e-5f;
    if (dt > 0.1f)    dt = 0.1f;   // sahne arası uzun gecikme olursa türev patlamasın

    // Hata hesabı
    float error = setpoint_ - input;

    // Küçük bir deadband — sürekli titreme önlemek için.
    // Önceki sürümdeki 0.1°'lik deadband çok büyüktü ve denge kaybına
    // sebep oluyordu; burada 0.02° gibi çok düşük tutuyoruz.
    if (std::fabs(error) < deadband_) {
        error = 0.0f;
    }

    // P
    float P = Kp_ * error;

    // D — "derivative on measurement": türevi error farkı yerine ölçümün
    // değişiminden alıyoruz, böylece setpoint değişiminde tekme oluşmaz.
    // Eğer gyro gibi gerçek bir rate ölçümü verilmişse onu doğrudan kullan.
    float derivativeRaw;
    if (measuredRate != 0.0f) {
        // Açı setpoint'te artıyor demek error'un azalması demek → işaret ters
        derivativeRaw = -measuredRate;
    } else {
        derivativeRaw = -(input - lastInput_) / dt;
    }
    // Düşük geçiren filtre
    float derivFiltered = derivAlpha_ * derivativeRaw +
                          (1.0f - derivAlpha_) * lastDerivFiltered_;
    lastDerivFiltered_ = derivFiltered;
    float D = Kd_ * derivFiltered;

    // I — önce ekle, sonra back-calculation ile düzelt
    float trialIntegral = integral_ + error * dt;
    float trialI = Ki_ * trialIntegral;
    float trialOutput = P + trialI + D;

    // Back-calculation anti-windup: çıkış doyuma girdiğinde integratoru
    // doyumdan dolayı oluşan farka göre geri çek.
    float output;
    if (trialOutput > outputLimit_) {
        output = outputLimit_;
        // sadece doyum azaltacak yönde integratoru kabul et
        if (error < 0.0f) integral_ = trialIntegral;
    } else if (trialOutput < -outputLimit_) {
        output = -outputLimit_;
        if (error > 0.0f) integral_ = trialIntegral;
    } else {
        output = trialOutput;
        integral_ = trialIntegral;
    }

    // Telemetri için son bileşenleri sakla
    lastP_ = P;
    lastI_ = Ki_ * integral_;
    lastD_ = D;

    lastInput_ = input;
    return output;
}
