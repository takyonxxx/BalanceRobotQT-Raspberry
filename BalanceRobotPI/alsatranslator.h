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

    Q_PROPERTY(QString command READ getCommand NOTIFY commandChanged)
    Q_PROPERTY(QString error READ getError NOTIFY errorChanged)
    Q_PROPERTY(bool running READ getRunning NOTIFY runningChanged)

    void speak(SType type, QString &text);
    void start();
    void stop();
    void setRecordDuration(int value);

private:    
    QNetworkRequest request;
    QFile file;
    QUrl url;
    QString filePath;


    const QDir location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString fileName = "record.flac";//QUuid::createUuid().toString() + ".flac"; // random unique recording name
    const int maxDuration = 10000; // maximum recording duration allowed
    const int minDuration = 1000; // minimium recording duration allowed

    ALSARecorder audioRecorder;
    QNetworkAccessManager networkAccessManager;
    std::thread soundhread;

    QString command = ""; // last command
    QString speech = ""; // last speech
    QString error = ""; // last error
    int recordDuration = 1000; // recording duration in miliseconds
    bool foundCapture {false};
    bool running {false}; // translation state

    QElapsedTimer timer;
    qint64 nanoSec;

    void record();
    void findCaptureDevice(char* devname);
    void speakTr(QString text);
    void speakEn(QString text);

    QString soundFormatTr;
    QString soundFormatEn;
    int soundCardNumber{0};

    bool getRunning() const {
        return this->running;
    }


    void setRunning(bool running) {
        if (running != this->running) {
            this->running = running;
            emit runningChanged(running);
        }
    }

    QString getCommand() const {
        return this->command;
    }

    void setCommand(QString command) {
        if (this->command != command) {
            this->command = command;
            emit commandChanged(command);
        }
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
