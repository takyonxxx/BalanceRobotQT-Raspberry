#include "voiceassistant.h"
#include "llmclient.h"
#include "robotcontrol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include <dlfcn.h>
#include <climits>

// ---------------------------------------------------------------- Vosk C API
// libvosk derleme bağımlılığı DEĞİLDİR: çalışma anında dlopen edilir.
// Kurulu değilse asistan STT'siz kapanır, firmware normal çalışır.

typedef struct VoskModel VoskModel;
typedef struct VoskRecognizer VoskRecognizer;

typedef void            (*fn_set_log_level)(int);
typedef VoskModel*      (*fn_model_new)(const char*);
typedef void            (*fn_model_free)(VoskModel*);
typedef VoskRecognizer* (*fn_rec_new)(VoskModel*, float);
typedef void            (*fn_rec_free)(VoskRecognizer*);
typedef int             (*fn_rec_accept)(VoskRecognizer*, const char*, int);
typedef const char*     (*fn_rec_result)(VoskRecognizer*);
typedef const char*     (*fn_rec_final)(VoskRecognizer*);

static fn_model_new  p_model_new  = nullptr;
static fn_model_free p_model_free = nullptr;
static fn_rec_new    p_rec_new    = nullptr;
static fn_rec_free   p_rec_free   = nullptr;
static fn_rec_accept p_rec_accept = nullptr;
static fn_rec_result p_rec_result = nullptr;
static fn_rec_final  p_rec_final  = nullptr;

// ---------------------------------------------------------------- kurulum

VoiceAssistant::VoiceAssistant(RobotControl *robot, QObject *parent)
    : QObject(parent)
    , robot_(robot)
{
}

VoiceAssistant::~VoiceAssistant()
{
    shutdown();
}

void VoiceAssistant::loadSettings()
{
    const QString path = QCoreApplication::applicationDirPath() + "/settings.ini";
    QSettings s(path, QSettings::IniFormat);

    s.beginGroup("assistant");
    // İlk çalıştırmada varsayılanları dosyaya yaz ki kullanıcı düzenleyebilsin.
    if (!s.contains("enabled"))       s.setValue("enabled", true);
    if (!s.contains("micDevice"))     s.setValue("micDevice", "auto");
    if (!s.contains("voskModelPath")) s.setValue("voskModelPath",
        QCoreApplication::applicationDirPath() + "/vosk-model-small-tr-0.3");
    if (!s.contains("wakeWord"))      s.setValue("wakeWord", "robot");
    if (!s.contains("piperModel"))    s.setValue("piperModel", "");
    if (!s.contains("geminiApiKey"))  s.setValue("geminiApiKey", "");
    if (!s.contains("geminiModel"))   s.setValue("geminiModel", "gemini-2.0-flash");
    if (!s.contains("claudeApiKey"))  s.setValue("claudeApiKey", "");
    if (!s.contains("claudeModel"))   s.setValue("claudeModel", "claude-sonnet-4-6");
    if (!s.contains("moveByteFwd"))   s.setValue("moveByteFwd", 180);
    if (!s.contains("moveByteTurn"))  s.setValue("moveByteTurn", 60);
    if (!s.contains("moveDefaultPct"))  s.setValue("moveDefaultPct", 100);
    if (!s.contains("moveDefaultSecs")) s.setValue("moveDefaultSecs", 1.5);
    if (!s.contains("voiceMaxVel"))     s.setValue("voiceMaxVel", 6.0);

    enabled_       = s.value("enabled").toBool();
    micDevice_     = s.value("micDevice").toString();
    if (micDevice_.isEmpty() || micDevice_ == "auto")
        micDevice_ = detectMicDevice();
    voskModelPath_ = s.value("voskModelPath").toString();
    wakeWord_      = s.value("wakeWord").toString().trimmed().toLower();
    piperModel_    = s.value("piperModel").toString();
    moveByteFwd_   = qBound(30, s.value("moveByteFwd").toInt(), 255);
    moveByteTurn_  = qBound(20, s.value("moveByteTurn").toInt(), 255);
    moveDefaultPct_  = qBound(10, s.value("moveDefaultPct").toInt(), 100);
    moveDefaultSecs_ = qBound(0.5, s.value("moveDefaultSecs").toDouble(), 8.0);
    voiceMaxVel_     = qBound(0.0, s.value("voiceMaxVel").toDouble(), 12.0);

    const QString ck = s.value("claudeApiKey").toString();
    const QString cm = s.value("claudeModel").toString();
    const QString gk = s.value("geminiApiKey").toString();
    const QString gm = s.value("geminiModel").toString();
    s.endGroup();
    s.sync();

    llm_->configure(ck, cm, gk, gm);
}

bool VoiceAssistant::loadVosk()
{
    // Sık kullanılan konumları dene.
    const QStringList candidates = {
        "libvosk.so",
        "/usr/local/lib/libvosk.so",
        QCoreApplication::applicationDirPath() + "/libvosk.so"
    };
    for (const QString &c : candidates) {
        voskLib_ = dlopen(c.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
        if (voskLib_) break;
    }
    if (!voskLib_) {
        qDebug("VoiceAssistant: libvosk.so not found - voice disabled. "
               "Install: see README 'Voice assistant on the Pi'.");
        return false;
    }

    auto set_log = (fn_set_log_level)dlsym(voskLib_, "vosk_set_log_level");
    p_model_new  = (fn_model_new) dlsym(voskLib_, "vosk_model_new");
    p_model_free = (fn_model_free)dlsym(voskLib_, "vosk_model_free");
    p_rec_new    = (fn_rec_new)   dlsym(voskLib_, "vosk_recognizer_new");
    p_rec_free   = (fn_rec_free)  dlsym(voskLib_, "vosk_recognizer_free");
    p_rec_accept = (fn_rec_accept)dlsym(voskLib_, "vosk_recognizer_accept_waveform");
    p_rec_result = (fn_rec_result)dlsym(voskLib_, "vosk_recognizer_result");
    p_rec_final  = (fn_rec_final) dlsym(voskLib_, "vosk_recognizer_final_result");

    if (!p_model_new || !p_rec_new || !p_rec_accept || !p_rec_result || !p_rec_final) {
        qDebug("VoiceAssistant: libvosk symbols missing - voice disabled.");
        return false;
    }
    if (set_log) set_log(-1);   // vosk loglarini sustur

    if (!QFile::exists(voskModelPath_)) {
        qDebug().noquote() << "VoiceAssistant: Vosk model not found at"
                           << voskModelPath_ << "- voice disabled.";
        return false;
    }

    voskModel_ = p_model_new(voskModelPath_.toUtf8().constData());
    if (!voskModel_) {
        qDebug("VoiceAssistant: Vosk model failed to load - voice disabled.");
        return false;
    }
    voskRec_ = p_rec_new((VoskModel*)voskModel_, 16000.0f);
    if (!voskRec_) {
        qDebug("VoiceAssistant: Vosk recognizer create failed - voice disabled.");
        return false;
    }
    return true;
}

void VoiceAssistant::start()
{
    // Denge döngüsüyle CPU yarışmasın.
    QThread::currentThread()->setPriority(QThread::LowPriority);

    llm_ = new LlmClient(this);
    loadSettings();

    if (!enabled_) {
        qDebug("VoiceAssistant: disabled in settings.ini ([assistant]/enabled=false).");
        return;
    }
    if (!loadVosk())
        return;

    moveStopTimer_ = new QTimer(this);
    moveStopTimer_->setSingleShot(true);
    connect(moveStopTimer_, &QTimer::timeout, this, [this]() { stopAllMotion(); });

    startRecorder();
    qDebug().noquote() << "VoiceAssistant: listening on" << micDevice_
                       << (wakeWord_.isEmpty()
                           ? QString("(no wake word)")
                           : QString("(wake word: \"%1\")").arg(wakeWord_))
                       << "- LLM:" << (llm_->hasProvider()
                                       ? llm_->providerName()
                                       : QString("none (offline commands only)"));
    emit statusLine("ASSIST ready");
}

void VoiceAssistant::startRecorder()
{
    recorder_ = new QProcess(this);
    QStringList args{"-D", micDevice_, "-f", "S16_LE", "-r", "16000",
                     "-c", "1", "-t", "raw", "-q"};
    connect(recorder_, &QProcess::readyReadStandardOutput,
            this, &VoiceAssistant::onAudioData);
    connect(recorder_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VoiceAssistant::onRecorderFinished);
    recorder_->start("arecord", args);
    if (!recorder_->waitForStarted(3000)) {
        qDebug("VoiceAssistant: arecord failed to start (alsa-utils installed? mic plugged?)");
    }
}

// "auto" mikrofon secimi: arecord -l ciktisini tarar, USB/webcam yakalama
// kartini bulur. Kamera stream servisi yalnizca VIDEO cihazini (v4l2)
// kullandigi icin webcam'in dahili mikrofonu ayni anda serbestce kullanilabilir.
QString VoiceAssistant::detectMicDevice() const
{
    QProcess p;
    p.start("arecord", {"-l"});
    if (!p.waitForFinished(3000))
        return "default";
    const QString out = QString::fromUtf8(p.readAllStandardOutput());

    // Satir formati: "card 1: Device [USB Audio Device], device 0: ..."
    static const QRegularExpression re(
        "card (\\d+): (\\S+) \\[([^\\]]+)\\], device (\\d+)");

    QString first, preferred;
    auto it = re.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString dev  = QString("plughw:%1,%2")
                                 .arg(m.captured(1), m.captured(4));
        const QString name = (m.captured(2) + " " + m.captured(3)).toLower();
        if (first.isEmpty())
            first = dev;
        // Webcam/USB mikrofonlari tercih et (dahili/HDMI kartlari atla).
        if (preferred.isEmpty() &&
            (name.contains("cam") || name.contains("usb") ||
             name.contains("mic")))
            preferred = dev;
    }

    const QString chosen = !preferred.isEmpty() ? preferred
                          : !first.isEmpty()    ? first
                                                : QString("default");
    qDebug().noquote() << "VoiceAssistant: mic auto-detect ->" << chosen;
    return chosen;
    // Not: "plughw" katmani 48 kHz'lik webcam mikrofonunu 16 kHz'e
    // otomatik cevirir; "hw:" kullanilsaydi arecord -r 16000 reddedilirdi.
}

void VoiceAssistant::onRecorderFinished(int exitCode, QProcess::ExitStatus)
{
    qDebug("VoiceAssistant: arecord exited (%d) - restarting in 3 s", exitCode);
    QTimer::singleShot(3000, this, [this]() {
        if (recorder_) { recorder_->deleteLater(); recorder_ = nullptr; }
        startRecorder();
    });
}

void VoiceAssistant::shutdown()
{
    if (recorder_) {
        recorder_->disconnect(this);
        recorder_->kill();
        recorder_->waitForFinished(1000);
        recorder_ = nullptr;
    }
    if (speaker_) {
        speaker_->disconnect(this);
        speaker_->kill();
        speaker_ = nullptr;
    }
    if (voskRec_ && p_rec_free)     { p_rec_free((VoskRecognizer*)voskRec_); voskRec_ = nullptr; }
    if (voskModel_ && p_model_free) { p_model_free((VoskModel*)voskModel_); voskModel_ = nullptr; }
    if (voskLib_)                   { dlclose(voskLib_); voskLib_ = nullptr; }
}

// ---------------------------------------------------------------- ses akışı

void VoiceAssistant::onAudioData()
{
    QByteArray chunk = recorder_->readAllStandardOutput();

    // TTS çalarken robot kendini duymasın: sesi yut, tanıyıcıyı sıfırla.
    if (speaking_) {
        if (p_rec_final && voskRec_) p_rec_final((VoskRecognizer*)voskRec_);
        return;
    }
    if (!voskRec_ || chunk.isEmpty())
        return;

    if (p_rec_accept((VoskRecognizer*)voskRec_, chunk.constData(), chunk.size()) == 1) {
        const char *res = p_rec_result((VoskRecognizer*)voskRec_);
        const QJsonObject json = QJsonDocument::fromJson(QByteArray(res)).object();
        const QString text = json.value("text").toString().trimmed();
        if (text.size() >= 3)
            handleUtterance(text);
    }
}

// ---------------------------------------------------------------- işleme hattı

void VoiceAssistant::handleUtterance(QString text)
{
    QString lower = text.toLower();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Uyandırma sözcüğü filtresi ÖNCE: bize söylenmeyen konuşmalar
    // (ortam sesi, TV, sohbet) loglanmadan sessizce atlanır.
    if (!wakeWord_.isEmpty()) {
        if (lower.contains(wakeWord_)) {
            // Uyandırma sözcüğünü çıkar, 10 sn'lik dikkat penceresi aç.
            lower.remove(wakeWord_);
            lower = lower.trimmed();
            attentionUntilMs_ = now + 10000;
            if (lower.size() < 3) {
                qDebug().noquote() << "ASSIST heard:" << text;
                speak("Evet, dinliyorum.");
                return;
            }
        } else if (now < attentionUntilMs_) {
            // Dikkat penceresi içinde: sözcüksüz devam et.
        } else {
            return;   // bize söylenmedi - log yok
        }
    }

    qDebug().noquote() << "ASSIST heard:" << text;
    emit statusLine("ASSIST heard: " + text);

    attentionUntilMs_ = now + 10000;   // konuşma sürdükçe pencereyi uzat

    if (tryLocalCommand(lower)) {
        qDebug("ASSIST -> local command executed");
        return;
    }
    qDebug("ASSIST -> no local command match, treating as question");

    if (llm_->hasProvider()) {
        askLlm(text);
    } else {
        speak("Bunu anlamadim. Komutlar: ileri, geri, sola don, saga don, dur, "
              "durum, pid ogrenmeyi baslat, trim sifirla.");
    }
}

bool VoiceAssistant::tryLocalCommand(const QString &t)
{
    // ---- Durdurma (en yüksek öncelik; "durum"/"durdur" hariç) ----
    if (containsAny(t, {"dur", "stop", "kes", "bekle"}) &&
        !containsAny(t, {"durum", "durdur"})) {
        stopAllMotion();
        speak("Durdum.");
        return true;
    }

    // ---- PID öğrenme ----
    if (t.contains("pid") || t.contains("öğren") || t.contains("ogren") ||
        t.contains("learn")) {
        if (containsAny(t, {"durdur", "bitir", "iptal", "stop", "kapat"})) {
            robot_->stopPidLearning();
            speak("PID ogrenme durduruluyor, en iyi degerler saklaniyor.");
            return true;
        }
        if (containsAny(t, {"başlat", "baslat", "başla", "basla", "start", "aç", "ac"})) {
            robot_->startPidLearning();
            speak("PID ogrenme basladi. Robot bilerek sallanacak, birkac dakika surer.");
            return true;
        }
    }

    // ---- Trim sıfırlama ----
    if (t.contains("trim") &&
        containsAny(t, {"sıfırla", "sifirla", "resetle", "reset", "temizle"})) {
        robot_->resetTrim();
        speak("Trim sifirlandi.");
        return true;
    }

    // ---- Durum sorgusu ----
    if (containsAny(t, {"durum", "nasıl", "nasil", "status", "telemetri",
                        "açı", "aci", "denge"})) {
        speak(statusText());
        return true;
    }

    // ---- Arm / Disarm ----
    if (containsAny(t, {"disarm", "motorları kapat", "motorlari kapat",
                        "motoru kapat", "devre dışı", "devre disi"})) {
        robot_->setIsArmed(false);
        speak("Motorlar kapatildi.");
        return true;
    }
    if (containsAny(t, {"arm", "hazırlan", "hazirlan", "dengele",
                        "motorları aç", "motorlari ac", "motoru aç", "motoru ac"})) {
        robot_->setIsArmed(true);
        speak("Denge kontrolu aktif.");
        return true;
    }

    // ---- Hareket ----
    QString dir;
    if      (containsAny(t, {"ileri", "öne", "one", "forward"})) dir = "forward";
    else if (containsAny(t, {"geri", "arkaya", "back"}))         dir = "backward";
    else if (containsAny(t, {"sol", "left"}))                    dir = "left";
    else if (containsAny(t, {"sağ", "sag", "right"}))            dir = "right";

    if (!dir.isEmpty()) {
        // Hız: "yüzde ..." veya "%" işaretinden SONRAKİ sayıyı al.
        // ("yüzde" kelimesi "yüz"ü içerdiği için tüm metinde arama yanlış olur.)
        int pct = moveDefaultPct_;
        int pctPos = -1, pctLen = 0;
        for (const QString &kw : {QString("yüzde"), QString("yuzde"), QString("%")}) {
            const int i = t.indexOf(kw);
            if (i >= 0 && (pctPos < 0 || i < pctPos)) { pctPos = i; pctLen = kw.size(); }
        }
        if (pctPos >= 0) {
            const double n = firstNumber(t.mid(pctPos + pctLen));
            if (n > 0) pct = qBound(10, (int)n, 100);
        }

        // Süre: önce "yüzde <sayı>" kısmını çıkar ki hız sayısı süre sanılmasın.
        QString td = t;
        static const QRegularExpression pctRe("(yüzde|yuzde|%)\\s*\\S*");
        td.remove(pctRe);
        double secs = moveDefaultSecs_;
        if (containsAny(td, {"saniye", "second", "sec"})) {
            const double n = firstNumber(td);
            if (n > 0) secs = qBound(0.3, n, 8.0);
        }
        if (containsAny(t, {"tam gaz", "full"}))                                pct = 100;
        else if (containsAny(t, {"çok hızlı", "cok hizli"}))                    pct = 90;
        else if (containsAny(t, {"hızlı", "hizli", "fast"}) && pctPos < 0)      pct = 75;
        else if (containsAny(t, {"yavaş", "yavas", "slow", "nazik"}))           pct = 30;

        speak(doMove(dir, pct, secs));
        return true;
    }

    return false;
}

void VoiceAssistant::askLlm(const QString &text)
{
    emit statusLine("ASSIST asking " + llm_->providerName());
    llm_->setSystemPromptExtra(statusText());
    llm_->ask(text,
        [this](const QString &name, const QJsonObject &args) {
            const QString r = execTool(name, args);
            qDebug().noquote() << "ASSIST tool" << name << "->" << r;
            emit statusLine("ASSIST tool " + name);
            return r;
        },
        [this](bool ok, const QString &msg) {
            qDebug().noquote() << "ASSIST" << (ok ? "reply:" : "error:") << msg;
            speak(msg);
        });
}

// ---------------------------------------------------------------- robot eylemleri

QString VoiceAssistant::execTool(const QString &name, const QJsonObject &args)
{
    if (name == "move_robot") {
        const QString dir = args.value("direction").toString();
        const int pct     = args.contains("speed_percent")
                              ? qBound(10, args.value("speed_percent").toInt(), 100)
                              : moveDefaultPct_;
        const double secs = args.contains("duration_seconds")
                              ? qBound(0.3, args.value("duration_seconds").toDouble(), 8.0)
                              : moveDefaultSecs_;
        return doMove(dir, pct, secs);
    }
    if (name == "stop_robot") {
        stopAllMotion();
        return "Stopped.";
    }
    if (name == "set_armed") {
        const bool armed = args.value("armed").toBool();
        robot_->setIsArmed(armed);
        return armed ? "Armed." : "Disarmed.";
    }
    if (name == "start_pid_learning") {
        robot_->startPidLearning();
        return "PID learning started. The robot will wobble on purpose for a few minutes.";
    }
    if (name == "stop_pid_learning") {
        robot_->stopPidLearning();
        return "PID learning stop requested; best gains so far are kept.";
    }
    if (name == "get_robot_status") {
        return statusText();
    }
    if (name == "reset_trim") {
        robot_->resetTrim();
        return "Trim reset.";
    }
    return "Unknown tool: " + name;
}

QString VoiceAssistant::doMove(const QString &direction, int speedPercent, double seconds)
{
    if (!robot_->getIsArmed())
        return "Robot su an dengede degil (disarmed), hareket edemem.";

    stopAllMotion();

    QString what;
    if (direction == "forward") {
        if (voiceMaxVel_ > 0) robot_->setTempMaxVel((float)voiceMaxVel_);
        robot_->setNeedSpeed(moveByteFwd_ * speedPercent / 100);
        what = "Ileri gidiyorum";
    } else if (direction == "backward") {
        if (voiceMaxVel_ > 0) robot_->setTempMaxVel((float)voiceMaxVel_);
        robot_->setNeedSpeed(-moveByteFwd_ * speedPercent / 100);
        what = "Geri gidiyorum";
    } else if (direction == "left") {
        robot_->setNeedTurnL(moveByteTurn_ * speedPercent / 100);
        robot_->setNeedTurnR(0);
        what = "Sola donuyorum";
    } else if (direction == "right") {
        robot_->setNeedTurnR(moveByteTurn_ * speedPercent / 100);
        robot_->setNeedTurnL(0);
        what = "Saga donuyorum";
    } else {
        return "Bilinmeyen yon: " + direction;
    }

    moveStopTimer_->start(int(seconds * 1000));
    qDebug().noquote() << QString("ASSIST move: dir=%1 cmd(needSpeed/turn)=%2 pct=%3 dur=%4s")
                          .arg(direction)
                          .arg(direction == "left" || direction == "right"
                                   ? moveByteTurn_ * speedPercent / 100
                                   : moveByteFwd_ * speedPercent / 100)
                          .arg(speedPercent)
                          .arg(seconds, 0, 'f', 1);
    return QString("%1, %2 saniye, yuzde %3 hiz.")
        .arg(what).arg(seconds, 0, 'f', 1).arg(speedPercent);
}

void VoiceAssistant::stopAllMotion()
{
    if (moveStopTimer_ && moveStopTimer_->isActive())
        moveStopTimer_->stop();
    robot_->setNeedSpeed(0);
    robot_->setNeedTurnL(0);
    robot_->setNeedTurnR(0);
    robot_->setTempMaxVel(0.0f);   // sesli hız tavanını bırak - joystick ayarı geçerli
}

QString VoiceAssistant::statusText() const
{
    const RobotControl::Telemetry t = robot_->getTelemetry();
    QString s = QString("Aci %1 derece. ").arg(t.angle, 0, 'f', 1);
    s += t.armed  ? "Denge aktif. "  : "Denge kapali. ";
    if (t.fallen)      s += "Robot dusmus durumda. ";
    if (t.pidLearning) s += "PID ogrenme calisiyor. ";
    s += QString("PWM sol %1, sag %2.").arg(t.pwmL).arg(t.pwmR);
    return s;
}

// ---------------------------------------------------------------- TTS

void VoiceAssistant::speak(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    qDebug().noquote() << "ASSIST say:" << text;
    speakQueue_.append(text);
    if (!speaker_)
        speakNext();
}

void VoiceAssistant::speakNext()
{
    if (speakQueue_.isEmpty()) {
        speaking_ = false;
        return;
    }
    const QString text = speakQueue_.takeFirst();
    speaking_ = true;

    speaker_ = new QProcess(this);
    connect(speaker_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VoiceAssistant::onSpeakFinished);

    if (!piperModel_.isEmpty() && QFile::exists(piperModel_)) {
        // Piper: doğal Türkçe ses. stdin'den metin alır, raw PCM üretir.
        const QString cmd = QString("piper -m %1 --output_raw 2>/dev/null | "
                                    "aplay -q -r 22050 -f S16_LE -t raw -c 1")
                                .arg(piperModel_);
        speaker_->start("sh", {"-c", cmd});
        if (speaker_->waitForStarted(2000)) {
            speaker_->write(text.toUtf8());
            speaker_->closeWriteChannel();
            return;
        }
        speaker_->deleteLater();
        speaker_ = nullptr;
        // düşerek espeak'e devam
    }

    // espeak-ng: hafif, robotik ama anlaşılır Türkçe.
    speaker_ = new QProcess(this);
    connect(speaker_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VoiceAssistant::onSpeakFinished);
    speaker_->start("espeak-ng", {"-v", "tr", "-s", "160", text});
    if (!speaker_->waitForStarted(2000)) {
        qDebug("VoiceAssistant: espeak-ng not found (sudo apt-get install espeak-ng)");
        speaker_->deleteLater();
        speaker_ = nullptr;
        speaking_ = false;
        speakQueue_.clear();
    }
}

void VoiceAssistant::onSpeakFinished(int, QProcess::ExitStatus)
{
    if (speaker_) {
        speaker_->deleteLater();
        speaker_ = nullptr;
    }
    // Hoparlör sesi mikrofona sızmış olabilir - tanıyıcıyı temizle.
    if (p_rec_final && voskRec_)
        p_rec_final((VoskRecognizer*)voskRec_);
    speakNext();
}

// ---------------------------------------------------------------- yardımcılar

bool VoiceAssistant::containsAny(const QString &t, const QStringList &keys)
{
    for (const QString &k : keys)
        if (t.contains(k)) return true;
    return false;
}

double VoiceAssistant::firstNumber(const QString &t)
{
    // Metinde EN ONCE gecen sayi kazanir (rakam veya sozcuk) - "yuzde kirk
    // yarim saniye" gibi cumlelerde dogru esleme icin konum karsilastirilir.
    int bestPos = INT_MAX;
    double bestVal = -1;

    static const QRegularExpression re("[0-9]+([.,][0-9]+)?");
    const QRegularExpressionMatch m = re.match(t);
    if (m.hasMatch()) {
        bool ok = false;
        const double v = QString(m.captured(0)).replace(',', '.').toDouble(&ok);
        if (ok) { bestPos = m.capturedStart(0); bestVal = v; }
    }
    // Sayı sözcükleri (vosk rakamları çoğunlukla yazıyla döker)
    // Sıralama önemli: onluklar ve "yüz" birimlerden ÖNCE denenir
    // ("altmış" içinden "altı" çıkmasın); kısa "on" en sonda (yanlış
    // eşleşme riski en yüksek olan o).
    static const QList<QPair<QString, double>> words = {
        {"yarım", 0.5}, {"yarim", 0.5}, {"buçuk", 1.5}, {"bucuk", 1.5},
        {"yirmi", 20}, {"otuz", 30}, {"kırk", 40}, {"kirk", 40},
        {"elli", 50}, {"altmış", 60}, {"altmis", 60},
        {"yetmiş", 70}, {"yetmis", 70}, {"seksen", 80}, {"doksan", 90},
        {"yüz", 100}, {"yuz", 100},
        {"iki", 2}, {"üç", 3}, {"dört", 4}, {"dort", 4},
        {"beş", 5}, {"bes", 5}, {"altı", 6}, {"alti", 6}, {"yedi", 7},
        {"sekiz", 8}, {"dokuz", 9}, {"bir", 1}, {"on", 10},
        {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}
    };
    for (const auto &w : words) {
        const int i = t.indexOf(w.first);
        if (i >= 0 && i < bestPos) { bestPos = i; bestVal = w.second; }
    }
    return bestVal;
}
