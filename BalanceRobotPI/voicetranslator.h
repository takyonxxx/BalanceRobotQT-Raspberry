#ifndef VOICETRANSLATOR_H
#define VOICETRANSLATOR_H

#include <QThread>
#include <QAudioRecorder>
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

class VoiceTranslator:public QThread
{
    Q_OBJECT

public:
    VoiceTranslator(QObject* parent);
    void close();


    Q_PROPERTY(int recordDuration READ getRecordDuration WRITE setRecordDuration NOTIFY recordDurationChanged)
    Q_PROPERTY(QString command READ getCommand NOTIFY commandChanged)
    Q_PROPERTY(QString error READ getError NOTIFY errorChanged)
    Q_PROPERTY(QString speech READ getSpeech NOTIFY speechChanged)
    Q_PROPERTY(bool running READ getRunning NOTIFY runningChanged)


private:
    QNetworkAccessManager qam;
    QNetworkRequest request;
    QFile file;

    QString baseApi = "https://speech.googleapis.com/v1/speech:recognize";
    QString apiKey = "AIzaSyCY8Xg5wfn6Ld67287SGDBQPZvGCEN6Fsg";

    QUrl url;
    QString filePath;

    const QDir location = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString fileName = QUuid::createUuid().toString(); // random unique recording name
    const int maxDuration = 10000; // maximum recording duration allowed
    const int minDuration = 1000; // minimium recording duration allowed

    QAudioRecorder audioRecorder;
    QAudioEncoderSettings audioSettings;

    QString command = ""; // last command
    QString speech = ""; // last speech
    QString error = ""; // last error
    int recordDuration = 1000; // recording duration in miliseconds
    bool running = false; // translation state

    QElapsedTimer timer;
    qint64 nanoSec;

    int getRecordDuration() const {
        return this->recordDuration;
    }

    void setRecordDuration(int duration) {
        if (duration > maxDuration) {
            qInfo() << "duration is too big, setting to max value (miliseconds):" << maxDuration;
            duration = maxDuration;
        }

        if (duration < minDuration) {
            qInfo() << "duration is too small, setting to max value (miliseconds):" << minDuration;
            duration = minDuration;
        }

        if (duration != this->recordDuration) {
            this->recordDuration = duration;

            emit recordDurationChanged(recordDuration);
        }
    }

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

    QString getSpeech() const {
        return this->speech;
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

    void translate() {

        if (!file.open(QIODevice::ReadOnly)) {
           qDebug()  << "cannot open file:" << file.errorString() << file.fileName();
           setRunning(false);
           setError(file.errorString());
           return;
        }

        QByteArray fileData = file.readAll();
        file.close();
        file.remove();

        QJsonDocument data {
            QJsonObject { {
                    "audio",
                    QJsonObject { {"content", QJsonValue::fromVariant(fileData.toBase64())} }
                },  {
                    "config",
                    QJsonObject {
                        {"encoding", "FLAC"},
                        {"languageCode", "en-US"},
                        {"model", "command_and_search"},
                        {"sampleRateHertz", audioSettings.sampleRate()}
                    }}
            }
        };

        qam.post(this->request, data.toJson(QJsonDocument::Compact));
    }

signals:
    void recordDurationChanged(qint64 duration);
    void runningChanged(bool running);
    void commandChanged(QString text);
    void errorChanged(QString text);
    void speechChanged(QString text);

protected:
    bool m_stop{false};
    void run() override; // reimplemented from QThread
};

#endif // VOICETRANSLATOR_H
