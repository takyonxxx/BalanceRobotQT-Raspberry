#include "alsatranslator.h"

AlsaTranslator::AlsaTranslator(QObject *parent)
    : QObject(parent)
{
    soundFormatTr = QString("espeak -vtr+f2");
    soundFormatEn = QString("espeak -ven+f2");

    if (!this->location.exists())
        this->location.mkpath(".");

    this->filePath = location.filePath(fileName);
    this->audioRecorder.setOutputLocation(filePath);
    this->audioRecorder.setDeviceName((char*)"plughw:1,0");
    this->audioRecorder.setChannels(1);
    this->audioRecorder.setSampleRate(44100);
    this->audioRecorder.initCaptureDevice();

    this->url.setUrl(baseApi);
    this->url.setQuery("key=" + apiKey);

    this->request.setUrl(this->url);
    this->request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    //this->request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

    file.setFileName(this->filePath);

    connect(&audioRecorder, &ALSARecorder::stateChanged, [this](auto state)
    {
        if (state == ALSARecorder::StoppedState)
        {
            this->translate();
        }
    });

    connect(&networkAccessManager, &QNetworkAccessManager::finished, this, &AlsaTranslator::responseReceived);

    qDebug() << "Flac location:" << this->filePath;
}

AlsaTranslator::~AlsaTranslator()
{
    audioRecorder.close();
}

void AlsaTranslator::responseReceived(QNetworkReply *response)
{
    if(m_stop)
        return;

    auto command = QString{};

    auto data = QJsonDocument::fromJson(response->readAll());
    response->deleteLater();

    auto error = data["error"]["message"];

    if (error.isUndefined()) {
        command = data["results"][0]["alternatives"][0]["transcript"].toString();
        setRunning(false);
    } else {
        setRunning(false);
        setError(error.toString());
    }

    if(!command.isEmpty())
    {
        QThread *thread = QThread::create([this, &command]{ speak(TR, command); });
        connect(thread,  &QThread::finished,  this,  [=]()
        {
            setCommand(command);
            record();;
        });
        thread->start();
    }
    else
    {
        setCommand(command);
        record();
    }
}

void AlsaTranslator::translate() {

    if(m_stop)
        return;

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
                    {"languageCode", "tr-TR"},
                    {"model", "command_and_search"},
                    {"enableWordTimeOffsets", false},
                    {"sampleRateHertz", (int)audioRecorder.getSampleRate()}
                }}
                    }
    };

    networkAccessManager.post(this->request, data.toJson(QJsonDocument::Compact));
}

void AlsaTranslator::stop()
{
    m_stop = true;
}

void AlsaTranslator::setRecordDuration(int value)
{
    recordDuration = value;
}

void AlsaTranslator::record()
{
    execCommand((char*)"aplay beep.wav");
    setError("");
    setCommand("");
    setRunning(true);
    audioRecorder.record(recordDuration);
}

void AlsaTranslator::speak(SType type, QString &text)
{
    execCommand((char*)"amixer -c 1 set Mic 0DB");
    if(type==SType::TR)
        speakTr(text);
    else if(type==SType::EN)
        speakEn(text);
    execCommand((char*)"amixer -c 1 set Mic 100DB");
}

void AlsaTranslator::speakTr(QString text)
{
    std::string sound = text.toStdString();
    std::string format = soundFormatTr.toStdString();
    std::string espeakBuff = format + std::string(" ")  + '"' + sound + '"' + " --stdout|aplay";
    execCommand((char*)espeakBuff.c_str());
}


void AlsaTranslator::speakEn(QString text)
{
    std::string sound = text.toStdString();
    std::string format = soundFormatEn.toStdString();
    std::string espeakBuff = format + std::string(" ")  + '"' + sound + '"' + " --stdout|aplay";
    execCommand((char*)espeakBuff.c_str());
}


void AlsaTranslator::start()
{
    qDebug() << "Alsa Translator is started...";
    record();
}
