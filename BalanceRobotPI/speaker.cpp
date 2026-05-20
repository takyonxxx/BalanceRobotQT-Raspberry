#include "speaker.h"
#include <QProcess>
#include <QFile>
#include <QDebug>
#include <QStringList>

Speaker *Speaker::theInstance_ = nullptr;

Speaker* Speaker::getInstance()
{
    if (theInstance_ == nullptr) {
        theInstance_ = new Speaker();
    }
    return theInstance_;
}

Speaker::Speaker(QObject *parent) : QObject(parent)
{
    // espeak veya espeak-ng CLI'sı kurulu mu kontrol et.
    // Hiçbiri yoksa Speak özelliği sessizce devre dışı kalır.
    const QStringList candidates = { "/usr/bin/espeak-ng",
                                     "/usr/local/bin/espeak-ng",
                                     "/usr/bin/espeak",
                                     "/usr/local/bin/espeak" };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            available_ = true;
            qDebug() << "Speaker: using" << path;
            break;
        }
    }
    if (!available_) {
        qDebug() << "Speaker: espeak not found, TTS disabled";
    }
}

Speaker::~Speaker()
{
}

void Speaker::speak(QString &text)
{
    if (text.isEmpty()) return;

    if (languageCode == TR)      speak_by_language(text, "tr");
    else if (languageCode == EN) speak_by_language(text, "en");
}

void Speaker::speak_by_language(QString text, QString lang)
{
    if (!available_) return;
    if (text.isEmpty()) return;

    // espeak'i fire-and-forget şekilde çalıştır.
    // ALSA varsayılan device'a yazıyor; eğer ses kartı yoksa hata stderr'e
    // gider ama programı bozmaz.
    //
    //   espeak-ng -v <lang> -s 160 -a 200 "metin"
    //
    // -v dil/voice, -s konuşma hızı (kelime/dk), -a ses (0..200)
    QStringList args;
    args << "-v" << lang
         << "-s" << "160"
         << "-a" << "180"
         << text;

    QProcess *proc = new QProcess(this);
    // Çıktıyı yutmak için stderr/stdout'u /dev/null'a yönlendir — log temiz kalır.
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());

    // Bittiğinde otomatik temizle
    QObject::connect(proc,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        proc, &QObject::deleteLater);

    // Hangi binary varsa onu kullan
    QString bin = "espeak-ng";
    if (!QFile::exists("/usr/bin/espeak-ng")
        && !QFile::exists("/usr/local/bin/espeak-ng")) {
        bin = "espeak";
    }
    proc->start(bin, args);
}

void Speaker::setLanguageCode(SType newLanguageCode)
{
    languageCode = newLanguageCode;
}
