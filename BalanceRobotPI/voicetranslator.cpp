#include "voicetranslator.h"

VoiceTranslator::VoiceTranslator(QObject *parent)
    : QThread(parent)
{
    if (!this->location.exists())
        this->location.mkpath(".");

    this->filePath = location.filePath(fileName);

    this->audioSettings.setCodec("audio/x-flac");
    this->audioSettings.setSampleRate(16000);
    this->audioSettings.setQuality(QMultimedia::VeryHighQuality);
    this->audioRecorder.setEncodingSettings(audioSettings);
    this->audioRecorder.setOutputLocation(filePath);

    this->url.setUrl(this->baseApi);
    this->url.setQuery("key=" + this->apiKey);

    this->request.setUrl(this->url);
    this->request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    this->request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    file.setFileName(this->filePath);

    connect(&audioRecorder, &QAudioRecorder::durationChanged, [this](auto duration)
    {
        if (duration >= this->recordDuration &&
                this->audioRecorder.state() != QAudioRecorder::StoppedState &&
                this->audioRecorder.status() == QAudioRecorder::Status::RecordingStatus)
        {
            this->audioRecorder.stop();
        }
    });

    connect(&audioRecorder, &QAudioRecorder::statusChanged, [](auto status)
    {
        //qDebug() << "status:" << status;
    });

    connect(&audioRecorder, &QAudioRecorder::stateChanged, [this](auto state)
    {
        //qDebug() << "state:" << state;

        if (state == QAudioRecorder::StoppedState)
            this->translate();
    });

    connect(&audioRecorder, QOverload<QAudioRecorder::Error>::of(&QAudioRecorder::error), [this]
    {
        qDebug() << "error:" << audioRecorder.errorString();

        setRunning(false);
        setError(audioRecorder.errorString());
    });

    connect(&qam, &QNetworkAccessManager::finished, [this](QNetworkReply *response)
    {
        auto data = QJsonDocument::fromJson(response->readAll());
        response->deleteLater();

        QString strFromJson = QJsonDocument(data).toJson(QJsonDocument::Compact).toStdString().c_str();

        auto error = data["error"]["message"];

        if (error.isUndefined()) {
            auto command = data["results"][0]["alternatives"][0]["transcript"].toString();
            if(!command.isEmpty())
                emit speechChanged(command);
            else
                emit speechChanged(strFromJson);

            setRunning(false);
            setCommand(command);

        } else {
            setRunning(false);
            setError(error.toString());
        }
    });

    qDebug() << "Temp flac location:" << this->filePath;
}

void VoiceTranslator::close()
{
    m_stop = true;
}

void VoiceTranslator::run()
{
    qDebug() << "Google Speech to Text is listening...";
    timer.start();
    setError("");
    setCommand("");

    while (true)
    {
        if(m_stop)
            break;

        if(!this->running)
        {           
            setRunning(true);
            audioRecorder.record();
            nanoSec = timer.nsecsElapsed();
            qDebug() << "Elapsed time : " << QString::number((float)(nanoSec / 1000000000.f), 'f', 2) << "secs.";
            timer.restart();
        }
        QThread::msleep(5);
    }
}
