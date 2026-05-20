#ifndef SPEAKER_H
#define SPEAKER_H

#include <QCoreApplication>
#include <QString>
#include <QObject>
#include "constants.h"

// Speaker — espeak komutunu shell üzerinden çağırır.
// Avantaj: PortAudio/libespeak doğrudan bağlantısı yok, ses kartı
// yapılandırması olmasa bile programı bozmaz (sessiz başarısızlık).
//
// Pi'de espeak-ng veya espeak CLI kurulu olmalı (zaten gerekliydi):
//     sudo apt install espeak-ng

class Speaker: public QObject
{
    Q_OBJECT

public:
    explicit Speaker(QObject *parent = nullptr);
    ~Speaker();

    static Speaker* getInstance();

    void speak(QString &text);
    void speak_by_language(QString text, QString lang);
    void setLanguageCode(SType newLanguageCode);

private:
    SType languageCode{EN};
    bool  available_{false};

    static Speaker *theInstance_;
};

#endif // SPEAKER_H
