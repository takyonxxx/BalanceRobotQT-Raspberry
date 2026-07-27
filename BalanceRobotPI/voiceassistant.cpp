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
#include <QNetworkInterface>

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
    // Tek TTS motoru: Piper. Eski motor anahtarlarını temizle.
    if (s.contains("ttsEngine")) s.remove("ttsEngine");
    if (s.contains("ttsVoice"))  s.remove("ttsVoice");
    if (!s.contains("piperModel"))    s.setValue("piperModel", "");
    if (!s.contains("btSpeakerMac"))  s.setValue("btSpeakerMac", "F8:5C:7D:82:CE:9B");  // JBL Clip 4
    if (!s.contains("speakerDevice")) s.setValue("speakerDevice", "");
    if (!s.contains("geminiApiKey"))  s.setValue("geminiApiKey", "");
    if (!s.contains("geminiModel"))   s.setValue("geminiModel", "gemini-2.0-flash");
    if (!s.contains("claudeApiKey"))  s.setValue("claudeApiKey", "");
    if (!s.contains("claudeModel"))   s.setValue("claudeModel", "claude-sonnet-4-6");
    if (!s.contains("moveByteFwd"))   s.setValue("moveByteFwd", 180);
    if (!s.contains("moveByteTurn"))  s.setValue("moveByteTurn", 60);
    if (!s.contains("moveDefaultPct"))  s.setValue("moveDefaultPct", 100);
    if (!s.contains("moveDefaultSecs")) s.setValue("moveDefaultSecs", 1.5);
    if (!s.contains("voiceMaxVel"))     s.setValue("voiceMaxVel", 6.0);
    if (!s.contains("turnDefaultPct"))  s.setValue("turnDefaultPct", 50);
    if (!s.contains("turnDefaultSecs")) s.setValue("turnDefaultSecs", 1.0);

    enabled_       = s.value("enabled").toBool();
    micDevice_     = s.value("micDevice").toString();
    micAuto_       = (micDevice_.isEmpty() || micDevice_ == "auto");
    if (micAuto_)
        micDevice_ = detectMicDevice();
    voskModelPath_ = s.value("voskModelPath").toString();
    wakeWord_      = s.value("wakeWord").toString().trimmed().toLower();
    piperModel_    = s.value("piperModel").toString().trimmed();
    // Ayar boşsa uygulama klasöründeki doğal Türkçe KADIN sesini otomatik
    // kullan (indirilmişse): tr_TR-dfki-medium. Model yoksa TTS devre dışı
    // kalır ve açılışta uyarı loglanır (tek motor: Piper).
    if (piperModel_.isEmpty()) {
        const QString candidate = QCoreApplication::applicationDirPath()
                                  + "/tr_TR-dfki-medium.onnx";
        if (QFile::exists(candidate))
            piperModel_ = candidate;
    }
    btSpeakerMac_  = s.value("btSpeakerMac").toString().trimmed().toUpper();
    speakerDevice_ = s.value("speakerDevice").toString().trimmed();
    // Çözümlenmiş TTS cihazı: override > BlueALSA (MAC'ten otomatik) > varsayılan.
    if (!speakerDevice_.isEmpty())
        effSpeakerDevice_ = speakerDevice_;
    else if (!btSpeakerMac_.isEmpty())
        effSpeakerDevice_ = QString("bluealsa:DEV=%1,PROFILE=a2dp").arg(btSpeakerMac_);
    else
        effSpeakerDevice_.clear();
    moveByteFwd_   = qBound(30, s.value("moveByteFwd").toInt(), 255);
    moveByteTurn_  = qBound(20, s.value("moveByteTurn").toInt(), 255);
    moveDefaultPct_  = qBound(10, s.value("moveDefaultPct").toInt(), 100);
    moveDefaultSecs_ = qBound(0.5, s.value("moveDefaultSecs").toDouble(), 8.0);
    voiceMaxVel_     = qBound(0.0, s.value("voiceMaxVel").toDouble(), 12.0);
    turnDefaultPct_  = qBound(10, s.value("turnDefaultPct").toInt(), 100);
    turnDefaultSecs_ = qBound(0.3, s.value("turnDefaultSecs").toDouble(), 8.0);

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

    // BT hoparlör: açılışta bağlan, sonra periyodik yeniden dene
    // (JBL kapanıp açılırsa kendiliğinden geri gelir).
    if (!btSpeakerMac_.isEmpty()) {
        ensureBtSpeaker();
        btReconnectTimer_ = new QTimer(this);
        btReconnectTimer_->setInterval(30000);
        connect(btReconnectTimer_, &QTimer::timeout,
                this, &VoiceAssistant::ensureBtSpeaker);
        btReconnectTimer_->start();
        qDebug().noquote() << "VoiceAssistant: BT speaker" << btSpeakerMac_
                           << "-> device" << effSpeakerDevice_;
    }

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
    qDebug().noquote() << "VoiceAssistant: starting arecord" << args.join(' ');
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
    // Asıl ALSA hatasını göster - "exited (1)" tek başına teşhis ettirmiyor.
    const QString err = recorder_
        ? QString::fromUtf8(recorder_->readAllStandardError()).trimmed()
        : QString();
    qDebug().noquote() << "VoiceAssistant: arecord exited (" << exitCode << ")"
                       << (err.isEmpty() ? QString() : "- " + err)
                       << "- restarting in 3 s";
    QTimer::singleShot(3000, this, [this]() {
        if (recorder_) { recorder_->deleteLater(); recorder_ = nullptr; }
        // Kart numarası değişmiş olabilir (USB yeniden numaralanması):
        // "auto" modundaysak her denemede mikrofonu baştan algıla.
        if (micAuto_) {
            const QString dev = detectMicDevice();
            if (dev != micDevice_) {
                qDebug().noquote() << "VoiceAssistant: mic device changed ->" << dev;
                micDevice_ = dev;
            }
        }
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
    bool viaWakeWord = wakeWord_.isEmpty();
    if (!wakeWord_.isEmpty()) {
        if (lower.contains(wakeWord_)) {
            viaWakeWord = true;
            lower.remove(wakeWord_);
            lower = lower.trimmed();
            attentionUntilMs_ = now + 10000;
            if (lower.size() < 3) {
                qDebug().noquote() << "ASSIST heard:" << text;
                speak("Evet?");
                return;
            }
        } else if (now < attentionUntilMs_) {
            // Dikkat penceresi içinde: sözcüksüz devam edilebilir,
            // ama eşleşmezse SESSİZCE atlanır (muhtemelen ortam sesi).
        } else {
            return;   // bize söylenmedi - log yok
        }
    }

    if (tryLocalCommand(lower)) {
        qDebug().noquote() << "ASSIST heard:" << text;
        qDebug("ASSIST -> local command executed");
        emit statusLine("ASSIST heard: " + text);
        // Pencere yalnızca BAŞARILI komutla uzar - ortam sesi uzatamaz.
        attentionUntilMs_ = now + 10000;
        return;
    }

    // Komut eşleşmedi:
    if (!viaWakeWord) {
        // Pencere içi ortam sesi - sessizce yut, cevap verme, pencereyi uzatma.
        return;
    }

    // Kullanıcı "robot ..." diye AÇIKÇA seslendi ama komut değil - soru say.
    qDebug().noquote() << "ASSIST heard:" << text;
    qDebug("ASSIST -> no local command match, treating as question");
    emit statusLine("ASSIST heard: " + text);
    attentionUntilMs_ = now + 10000;

    if (llm_->hasProvider()) {
        askLlm(text);
    } else {
        speak("Bunu anlamadım, tekrar söyler misin?");
    }
}

bool VoiceAssistant::tryLocalCommand(const QString &t)
{
    // ---- Durdurma (en yüksek öncelik; tam kelime - "durum"/"durdur" doğal olarak eşleşmez) ----
    if (containsWord(t, {"dur", "stop", "kes", "bekle"})) {
        stopAllMotion();
        speak("Tamam, durdum.");
        return true;
    }

    // ---- PID öğrenme ----
    if (t.contains("pid") || t.contains("öğren") || t.contains("ogren") ||
        t.contains("learn")) {
        if (containsAny(t, {"durdur", "bitir", "iptal", "stop", "kapat"})) {
            robot_->stopPidLearning();
            speak("PID öğrenme durdu, bulduğum en iyi değerleri kaydettim.");
            return true;
        }
        if (containsAny(t, {"başlat", "baslat", "başla", "basla", "start", "aç", "ac"})) {
            robot_->startPidLearning();
            speak("PID öğrenme başladı. Bilerek sallanacağım, birkaç dakika sürer.");
            return true;
        }
    }

    // ---- Trim sıfırlama ----
    if (t.contains("trim") &&
        containsAny(t, {"sıfırla", "sifirla", "resetle", "reset", "temizle"})) {
        robot_->resetTrim();
        speak("Trim sıfırlandı.");
        return true;
    }

    // ---- Kendini tanıtma ----
    // Vosk "tanıt"ı bazen "tanımla" diye çıkarıyor; ikisi de kabul.
    if (containsWord(t, {"tanıt", "tanit", "tanımla", "tanimla", "kimsin"}) ||
        t.contains("kendini")) {
        speak("Merhaba! Ben iki tekerlek üzerinde kendi kendine dengede duran "
              "bir robotum. Beni Türkay tasarladı. İçimde bir Raspberry Pi beş "
              "var; hareket sensörümle saniyede iki yüz kez dengemi düzeltirim. "
              "Sesli komutlarla ilerleyebilir, dönebilir, hatta kendi denge "
              "ayarlarımı kendim öğrenebilirim. Bir şey söylemek istersen "
              "önce robot demen yeterli.");
        return true;
    }

    // ---- Durum sorgusu ----
    if (containsWord(t, {"durum", "nasıl", "nasil", "nasılsın", "nasilsin",
                         "status", "telemetri", "açı", "aci", "denge"})) {
        speak(statusText());
        return true;
    }

    // ---- Arm / Disarm ----
    if (containsAny(t, {"disarm", "motorları kapat", "motorlari kapat",
                        "motoru kapat", "devre dışı", "devre disi"})) {
        robot_->setIsArmed(false);
        speak("Motorlar kapatıldı.");
        return true;
    }
    if (containsWord(t, {"arm", "hazırlan", "hazirlan", "dengele"}) ||
        containsAny(t, {"motorları aç", "motorlari ac", "motoru aç", "motoru ac"})) {
        robot_->setIsArmed(true);
        speak("Denge kontrolü aktif, hazırım.");
        return true;
    }

    // ---- Hareket ----
    // Yönler TAM KELİME olarak aranır: "sağladı" içindeki "sağ" gibi
    // alt-dizi yanlış pozitifleri komut sayılmasın.
    QString dir;
    if      (containsWord(t, {"ileri", "öne", "forward"}))            dir = "forward";
    else if (containsWord(t, {"geri", "arkaya", "back"}))             dir = "backward";
    else if (containsWord(t, {"sol", "sola", "left"}))                dir = "left";
    else if (containsWord(t, {"sağ", "sağa", "sag", "saga", "right"})) dir = "right";

    if (!dir.isEmpty()) {
        // Hız: "yüzde ..." veya "%" işaretinden SONRAKİ sayıyı al.
        // ("yüzde" kelimesi "yüz"ü içerdiği için tüm metinde arama yanlış olur.)
        int pct = moveDefaultPct_;
        bool pctExplicit = false;
        int pctPos = -1, pctLen = 0;
        for (const QString &kw : {QString("yüzde"), QString("yuzde"), QString("%")}) {
            const int i = t.indexOf(kw);
            if (i >= 0 && (pctPos < 0 || i < pctPos)) { pctPos = i; pctLen = kw.size(); }
        }
        if (pctPos >= 0) {
            const double n = firstNumber(t.mid(pctPos + pctLen));
            if (n > 0) { pct = qBound(10, (int)n, 100); pctExplicit = true; }
        }

        // Süre: önce "yüzde <sayı>" kısmını çıkar ki hız sayısı süre sanılmasın.
        QString td = t;
        static const QRegularExpression pctRe("(yüzde|yuzde|%)\\s*\\S*");
        td.remove(pctRe);
        double secs = moveDefaultSecs_;
        bool secsExplicit = false;
        if (containsAny(td, {"saniye", "second", "sec"})) {
            const double n = firstNumber(td);
            if (n > 0) { secs = qBound(0.3, n, 8.0); secsExplicit = true; }
        }
        if (containsAny(t, {"tam gaz", "full"}))                              { pct = 100; pctExplicit = true; }
        else if (containsAny(t, {"çok hızlı", "cok hizli"}))                  { pct = 90;  pctExplicit = true; }
        else if (containsAny(t, {"hızlı", "hizli", "fast"}) && pctPos < 0)    { pct = 75;  pctExplicit = true; }
        else if (containsAny(t, {"yavaş", "yavas", "slow", "nazik"}))         { pct = 30;  pctExplicit = true; }

        // Dönüşlerde hız söylenmediyse daha nazik varsayılan:
        // tam güç dönüş yalpa ekseninde ani sarsıntıyla robotu deviriyor.
        if (!pctExplicit && (dir == "left" || dir == "right"))
            pct = turnDefaultPct_;

        // Dönüşlerde süre söylenmediyse de daha kısa varsayılan (1 sn):
        // 1.5 sn tam dönüş çoğu zaman istenenden fazla yön değiştiriyor.
        if (!secsExplicit && (dir == "left" || dir == "right"))
            secs = turnDefaultSecs_;

        speak(doMove(dir, pct, secs, pctExplicit, secsExplicit));
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
        return doMove(dir, pct, secs,
                      args.contains("speed_percent"),
                      args.contains("duration_seconds"));
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

QString VoiceAssistant::doMove(const QString &direction, int speedPercent, double seconds,
                               bool sayPct, bool saySecs)
{
    if (!robot_->getIsArmed())
        return "Şu an dengede değilim, önce beni dik konuma getir.";

    stopAllMotion();

    QString what;
    if (direction == "forward") {
        if (voiceMaxVel_ > 0) robot_->setTempMaxVel((float)voiceMaxVel_);
        robot_->setNeedSpeed(moveByteFwd_ * speedPercent / 100);
        what = "İleri gidiyorum";
    } else if (direction == "backward") {
        if (voiceMaxVel_ > 0) robot_->setTempMaxVel((float)voiceMaxVel_);
        robot_->setNeedSpeed(-moveByteFwd_ * speedPercent / 100);
        what = "Geri gidiyorum";
    } else if (direction == "left") {
        robot_->setNeedTurnL(moveByteTurn_ * speedPercent / 100);
        robot_->setNeedTurnR(0);
        what = "Sola dönüyorum";
    } else if (direction == "right") {
        robot_->setNeedTurnR(moveByteTurn_ * speedPercent / 100);
        robot_->setNeedTurnL(0);
        what = "Sağa dönüyorum";
    } else {
        return "Bilinmeyen yön.";
    }

    moveStopTimer_->start(int(seconds * 1000));
    qDebug().noquote() << QString("ASSIST move: dir=%1 cmd(needSpeed/turn)=%2 pct=%3 dur=%4s")
                          .arg(direction)
                          .arg(direction == "left" || direction == "right"
                                   ? moveByteTurn_ * speedPercent / 100
                                   : moveByteFwd_ * speedPercent / 100)
                          .arg(speedPercent)
                          .arg(seconds, 0, 'f', 1);

    // Kisa onay: yalnizca kullanicinin SOYLEDIGI niteleyiciler tekrarlanir.
    // "sola don" -> "Sola donuyorum." / "1 saniye sola don" -> "... 1 saniye."
    QString reply = "Tamam, " + what;
    if (saySecs) {
        const QString secStr = (seconds == (int)seconds)
            ? QString::number((int)seconds)
            : QString::number(seconds, 'f', 1);
        reply += QString(", %1 saniye").arg(secStr);
    }
    if (sayPct)
        reply += QString(", yuzde %1").arg(speedPercent);
    return reply + ".";
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
    QString s;

    // Önce en önemli bilgi: arm durumu ve devrilme.
    if (t.fallen)
        s += "Düşmüş durumdayım, motorlar devre dışı. Beni dik konuma getirirsen kendim toparlanırım. ";
    else if (t.armed)
        s += "Motorlar aktif, dengedeyim. ";
    else
        s += "Motorlar kapalı, denge kontrolü beklemede. ";

    s += QString("Eğim açım %1 derece").arg(t.angle, 0, 'f', 1);
    if (t.armed)
        s += QString(", hedef açı %1 derece").arg(t.targetAngle, 0, 'f', 1);
    s += ". ";

    if (qAbs(t.trim) > 0.05f)
        s += QString("Trim düzeltmesi %1 derece. ").arg(t.trim, 0, 'f', 1);

    if (t.pidLearning)   s += "Şu an PID öğrenme modundayım, bilerek sallanıyorum. ";
    if (t.autoMode)      s += "Otomatik mod açık. ";
    if (t.positionHold)  s += "Yön kilidi aktif. ";

    s += QString("Motor güçleri: sol %1, sağ %2.").arg(t.pwmL).arg(t.pwmR);
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
    startSpeaker(speakQueue_.takeFirst(), false);
}

// TTS başlat. fallbackToDefault=true ise BT/özel cihaz atlanır, varsayılan
// ses çıkışı kullanılır - hoparlör kapalıysa robot sessiz kalmaz.
// Tek motor: Piper (tr_TR-dfki-medium). Model/binary yoksa konuşma atlanır.
void VoiceAssistant::startSpeaker(const QString &text, bool fallbackToDefault)
{
    speaking_ = true;
    lastSpokenText_  = text;
    lastWasFallback_ = fallbackToDefault;
    lastUsedDevice_  = fallbackToDefault ? QString() : effSpeakerDevice_;
    const QString dev = lastUsedDevice_;

    if (piperModel_.isEmpty() || !QFile::exists(piperModel_)) {
        qDebug("VoiceAssistant: Piper model missing - reply not spoken "
               "(see README: Natural Turkish voice / Piper setup)");
        speaking_ = false;
        speakQueue_.clear();
        return;
    }

    speaker_ = new QProcess(this);
    connect(speaker_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VoiceAssistant::onSpeakFinished);

    const QString aplayDev = dev.isEmpty() ? "" : QString(" -D \"%1\"").arg(dev);
    const QString cmd = QString("piper -m %1 --output_raw 2>/dev/null | "
                                "aplay -q%2 -r 22050 -f S16_LE -t raw -c 1")
                            .arg(piperModel_, aplayDev);
    speaker_->start("sh", {"-c", cmd});
    if (speaker_->waitForStarted(2000)) {
        speaker_->write(text.toUtf8());
        speaker_->closeWriteChannel();
        return;
    }

    qDebug("VoiceAssistant: piper failed to start (is /usr/local/bin/piper installed?)");
    speaker_->deleteLater();
    speaker_ = nullptr;
    speaking_ = false;
    speakQueue_.clear();
}

// BT hoparlör bakımı. Önce durum sorgulanır: BAĞLIYKEN hiçbir şey yapılmaz
// (log kirliliği yok). Kopuksa bağlanılır; bağlantı KURULDUĞU anda cihazın
// IP adresi hoparlörden seslendirilir - robotu ekransız açınca SSH için
// IP'yi öğrenmenin en pratik yolu.
void VoiceAssistant::ensureBtSpeaker()
{
    if (btSpeakerMac_.isEmpty())
        return;

    QProcess *info = new QProcess(this);
    connect(info, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, info](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(info->readAllStandardOutput());
        info->deleteLater();

        if (out.contains("Connected: yes")) {
            if (!btConnected_) {          // ilk tespit (uygulama açılışında bağlıysa)
                btConnected_ = true;
                announceIp();
            }
            return;                       // bağlı - sessizce çık
        }

        btConnected_ = false;
        QProcess *conn = new QProcess(this);
        connect(conn, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, conn](int, QProcess::ExitStatus) {
            const QString res = QString::fromUtf8(conn->readAllStandardOutput());
            conn->deleteLater();
            if (res.contains("successful")) {
                qDebug("VoiceAssistant: BT speaker connected");
                btConnected_ = true;
                announceIp();
            }
        });
        conn->start("bluetoothctl", {"connect", btSpeakerMac_});
    });
    info->start("bluetoothctl", {"info", btSpeakerMac_});
}

// Mobil uygulama (BLE) bağlanınca/kopunca sesli bildirim.
void VoiceAssistant::announceBleClient(bool connected, const QString &clientInfo)
{
    if (!connected) {
        speak("Mobil bağlantı kesildi.");
        return;
    }
    if (clientInfo.isEmpty())
        speak("Mobil uygulama bağlandı.");
    else
        speak(QString("Mobil uygulama bağlandı. İstemci: %1.").arg(clientInfo));
}

// Cihazın yerel IPv4 adresini hoparlörden söyle ("nokta" ile, Piper düzgün okusun).
void VoiceAssistant::announceIp()
{
    QString ip;
    for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
            ip = a.toString();
            break;
        }
    }
    if (ip.isEmpty()) {
        speak("Bağlandım.");
        return;
    }
    QString spoken = ip;
    spoken.replace(".", " nokta ");
    // "IP" -> "ip", "Aypi" -> "ipe" okunuyordu; iki ayrı hece olarak
    // yazmak ("ay pi") Piper'a doğru telaffuzu garantiliyor.
    speak(QString("Bağlandım. Ay pi adresim %1.").arg(spoken));
    qDebug().noquote() << "VoiceAssistant: announced IP" << ip;
}

void VoiceAssistant::onSpeakFinished(int exitCode, QProcess::ExitStatus)
{
    // Gerçek ALSA/BlueALSA hatasını yakala - teşhis için şart.
    const QString err = speaker_
        ? QString::fromUtf8(speaker_->readAllStandardError()).trimmed()
        : QString();
    if (speaker_) {
        speaker_->deleteLater();
        speaker_ = nullptr;
    }

    // BT/özel hoparlörde çalma başarısız olduysa (JBL kapalı, bağlantı yok)
    // aynı metni bir kez varsayılan çıkışta tekrarla ve yeniden bağlanmayı tetikle.
    if (exitCode != 0 && !lastUsedDevice_.isEmpty() && !lastWasFallback_) {
        qDebug().noquote() << "VoiceAssistant: speaker device failed ("
                           << lastUsedDevice_ << ")"
                           << (err.isEmpty() ? QString() : "- " + err)
                           << "- falling back to default output";
        ensureBtSpeaker();
        startSpeaker(lastSpokenText_, true);
        return;
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

// Tam kelime eşleşmesi: "sağladı" içindeki "sağ" gibi alt-dizi yanlış
// pozitiflerini engeller. Vosk çıktısı boşlukla ayrılmış kelimelerdir.
bool VoiceAssistant::containsWord(const QString &t, const QStringList &keys)
{
    const QStringList tokens = t.split(' ', Qt::SkipEmptyParts);
    for (const QString &tok : tokens)
        for (const QString &k : keys)
            if (tok == k) return true;
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
