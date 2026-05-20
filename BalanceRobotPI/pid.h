#ifndef PID_H
#define PID_H

/*
 * Improved PID controller.
 *
 * Key improvements over the previous version:
 *   - dt artık microsaniye çözünürlüklü (micros()) ile hesaplanır;
 *     5 ms döngüde millis() quantization sorunu kalkar.
 *   - Anti-windup back-calculation tekniği ile yapılır; integrator
 *     ancak çıkış doyuma girmezse büyür.
 *   - Türev terimi setpoint'e değil ölçüme (derivative on measurement)
 *     uygulanır; setpoint değiştiğinde "türev tekme" oluşmaz.
 *   - Derivative low-pass filtresi konfigüre edilebilir.
 *   - Deadband çok küçük (0.02) — durağan dengede de küçük düzeltmeler
 *     yapılabilsin diye. Önceki 0.1° deadband balansı kaybediyordu.
 *   - Çıkış sınırı (output limit) ayrı parametre olarak verilebilir;
 *     WINDUP_GUARD'a sabitli değil.
 */

namespace PIDConstants {
    constexpr float DEFAULT_OUTPUT_LIMIT = 255.0f;
    constexpr float DEFAULT_DEADBAND     = 0.02f;   // °
    constexpr float DEFAULT_DERIV_ALPHA  = 0.30f;   // 0..1, low-pass filtre faktörü
    constexpr float ANGLE_IRRECOVERABLE  = 60.0f;   // setpoint clamp
}

class PID
{
public:
    PID();

    // input: ölçülen değer (örn. açı)
    // measuredRate: ölçülen değişim hızı (örn. gyro deg/s). 0 verilirse
    //   türev klasik (error farkı / dt) hesaplanır.
    float compute(float input, float measuredRate = 0.0f);

    void  setSetpoint(float value);
    float getSetpoint() const { return setpoint_; }

    void  setTunings(float Kp, float Ki, float Kd);
    void  setOutputLimit(float limit);   // ±limit
    void  setDeadband(float band);
    void  setDerivativeFilter(float alpha); // 0..1
    
    /// Cap the I-term's contribution to the output. When the integral
    /// would push the controller harder than ±cap, it's clamped at the
    /// integrator level (so it can never accumulate the kind of wind-up
    /// that causes 1-2 Hz oscillation in balance robots). Set to 0 to
    /// disable (default).
    void  setIntegralOutputCap(float cap);

    void  resetIntegral();
    void  reset();

    // İzleme/telemetri için
    float lastP() const { return lastP_; }
    float lastI() const { return lastI_; }
    float lastD() const { return lastD_; }
    float integral() const { return integral_; }

private:
    // Kazançlar
    float Kp_{0.0f};
    float Ki_{0.0f};
    float Kd_{0.0f};

    // Durum
    float integral_{0.0f};
    float lastInput_{0.0f};
    float lastDerivFiltered_{0.0f};
    float setpoint_{0.0f};
    unsigned long lastTimeUs_{0};
    bool  firstCompute_{true};

    // Parametreler
    float outputLimit_{PIDConstants::DEFAULT_OUTPUT_LIMIT};
    float deadband_{PIDConstants::DEFAULT_DEADBAND};
    float derivAlpha_{PIDConstants::DEFAULT_DERIV_ALPHA};
    float integralOutputCap_{0.0f};   // 0 = no cap

    // Son hesaplama bileşenleri (telemetri için)
    float lastP_{0.0f};
    float lastI_{0.0f};
    float lastD_{0.0f};
};

#endif // PID_H
