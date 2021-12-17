#ifndef ALSATRANSLATOR_H
#define ALSATRANSLATOR_H

#include <QCoreApplication>
#include <QThread>
#include <QStandardPaths>
#include <QUrl>
#include <QDir>
#include <QDebug>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>

#include "alsarecorder.h"
#include "constants.h"

class AlsaTranslator:public QObject
{
    Q_OBJECT

public:
    AlsaTranslator(QObject* parent);
    ~AlsaTranslator();

    Q_PROPERTY(QString error READ getError NOTIFY errorChanged)
    Q_PROPERTY(bool running READ getRunning NOTIFY runningChanged)

    void speak(SType type, QString &text);
    void start();
    void stop();
    void record();
    void setRecordDuration(int value);

    void setLanguageCode(const QString &newLanguageCode);

    const QString &getLanguageCode() const;

    bool getRunning() const {
        return this->running;
    }


    void setRunning(bool running) {
        if (running != this->running) {
            this->running = running;
            emit runningChanged(running);
        }
    }
    void setDedectSoundDecibel(float newDedect_sound_decibel);

    void setIgnoreRecord(bool newIgnore_record);

private:
    QNetworkRequest request;
    QUrl url{};
    QString filePath{};
    QString languageCode{"en-US"};


    const QDir location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString fileName {};
    const int maxDuration = 10000; // maximum recording duration allowed
    const int minDuration = 1000; // minimium recording duration allowed

    ALSARecorder audioRecorder;
    QNetworkAccessManager networkAccessManager;
    std::thread soundhread{};

    QString speech = {}; // last speech
    QString error = {}; // last error
    int recordDuration{0}; // recording duration in miliseconds
    bool foundCapture {false};
    bool running {false}; // translation state
    bool ignore_record {false};

    QElapsedTimer timer{};
    qint64 nanoSec{};

    void findCaptureDevice(char* devname);
    void speakTr(QString text);
    void speakEn(QString text);

    QString soundFormatTr{};
    QString soundFormatEn{};
    int soundCardNumber{0};

    float dedect_sound_decibel{0};


    void setCommand(QString command) {       
        emit commandChanged(command);
    }

    QString getError() const {
        return this->error;
    }

    void setError(QString error) {
        if (this->error != error) {
            this->error = error;
            emit errorChanged(error);
        }
    }

    void translate();

private slots:
    void responseReceived(QNetworkReply *response);

signals:
    void runningChanged(bool running);
    void commandChanged(QString text);
    void errorChanged(QString text);

protected:
    bool m_stop{false};
};

#endif // ALSATRANSLATOR_H
