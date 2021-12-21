#ifndef SPEAKER_H
#define SPEAKER_H

#include <QCoreApplication>
#include "constants.h"
//#include <espeak-ng/speak_lib.h>
#include <espeak/speak_lib.h>

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
    int Buflength = 1024;
    SType languageCode{EN};
    static Speaker *theInstance_;

};


#endif // SPEAKER_H
