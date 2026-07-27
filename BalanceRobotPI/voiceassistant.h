#ifndef VOICEASSISTANT_H
#define VOICEASSISTANT_H

//
// VoiceAssistant - Pi üzerinde TELEFONSUZ sesli asistan.
//
// USB mikrofonu sürekli dinler (arecord), Vosk ile çevrimdışı Türkçe
// konuşma tanıma yapar, söyleneni önce yerel komut çözümleyicisinden
// geçirir (ileri/geri/dur/PID öğren/durum...). Komut değilse ve API
// anahtarı tanımlıysa soruyu Gemini (ücretsiz kota) veya Claude'a
// gönderir; cevabı Piper/espeak-ng ile sesli okur.
//
// Ana kontrol tamamen Pi tarafındadır - mobil uygulama kapalıyken de
// çalışır. Mobil uygulama bağlanırsa iki taraf da aynı atomik
// RobotControl arayüzünü kullandığı için çakışma olmaz.
//
// Bağımlılıklar (hepsi opsiyonel, yoksa asistan kendini kapatır):
//   - libvosk.so (dlopen ile yüklenir, derleme bağımlılığı DEĞİLDİR)
//   - Vosk Türkçe modeli (settings.ini -> [assistant]/voskModelPath)
//   - arecord (alsa-utils), espeak-ng veya piper
//
// CPU bütçesi: Vosk small modeli tek çekirdekte gerçek zamanlı çalışır;
// thread'ler düşük öncelikte tutulur ki 200 Hz denge döngüsü etkilenmesin.
//

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class RobotControl;
class LlmClient;

class VoiceAssistant : public QObject
{
    Q_OBJECT

public:
    explicit VoiceAssistant(RobotControl *robot, QObject *parent = nullptr);
    ~VoiceAssistant() override;

    // Kendi thread'i içinde çağrılır (moveToThread sonrası).
    Q_INVOKABLE void start();
    Q_INVOKABLE void shutdown();

    bool isEnabled() const { return enabled_; }

signals:
    // İsteyen dinlesin diye (ör. BLE üzerinden telefona akıtmak).
    void statusLine(const QString &line);

private slots:
    void onAudioData();
    void onRecorderFinished(int exitCode, QProcess::ExitStatus st);
    void onSpeakFinished(int exitCode, QProcess::ExitStatus st);

private:
    // ---- Kurulum ----
    void loadSettings();
    bool loadVosk();          // libvosk'u dlopen et, modeli yükle
    void startRecorder();     // arecord sürecini başlat
    QString detectMicDevice() const;  // "auto": arecord -l çıktısından USB/webcam mikrofonu bul

    // ---- Konuşma işleme hattı ----
    void handleUtterance(QString text);
    bool tryLocalCommand(const QString &lower);   // true = komut işlendi
    void askLlm(const QString &text);

    // ---- Robot eylemleri (RobotControl atomikleri - her thread'den güvenli) ----
    QString doMove(const QString &direction, int speedPercent, double seconds);
    void    stopAllMotion();
    QString statusText() const;
    QString execTool(const QString &name, const class QJsonObject &args);

    // ---- TTS ----
    void speak(const QString &text);
    void speakNext();

    // ---- Yardımcılar ----
    static bool containsAny(const QString &t, const QStringList &keys);
    static double firstNumber(const QString &t);

    RobotControl *robot_ = nullptr;
    LlmClient    *llm_   = nullptr;

    // Ayarlar ([assistant] bölümü, settings.ini)
    bool    enabled_      = false;
    QString micDevice_;          // arecord -D cihazı ("default", "plughw:1,0"...)
    QString voskModelPath_;      // Vosk model klasörü
    QString wakeWord_;           // boş = her söylenen işlenir
    QString piperModel_;         // boş = espeak-ng kullan
    int     moveByteFwd_  = 180; // ileri/geri komut baytı tavanı (BLE ile aynı ölçek)
    int     moveByteTurn_ = 60;  // dönüş komut baytı tavanı

    // Vosk (dlopen)
    void *voskLib_   = nullptr;
    void *voskModel_ = nullptr;
    void *voskRec_   = nullptr;

    // Ses yakalama
    QProcess  *recorder_ = nullptr;
    bool       speaking_ = false;   // TTS çalarken tanımayı duraklat (yankı önleme)

    // Uyandırma sözcüğü dikkat penceresi
    qint64 attentionUntilMs_ = 0;   // wake word sonrası bu ana kadar direkt dinle

    // Hareket otomatik durdurma
    QTimer *moveStopTimer_ = nullptr;

    // TTS kuyruğu
    QProcess    *speaker_ = nullptr;
    QStringList  speakQueue_;
};

#endif // VOICEASSISTANT_H
