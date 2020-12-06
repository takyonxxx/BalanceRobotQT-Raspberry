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

    networkAccessManager = new QNetworkAccessManager(this);

    connect(networkAccessManager, &QNetworkAccessManager::finished, [this](QNetworkReply *response)
    {
        if(m_stop)
            return;

        networkAccessManager->clearAccessCache();
        networkAccessManager->clearConnectionCache();

        auto command = QString{};

        auto data = QJsonDocument::fromJson(response->readAll());
        response->deleteLater();

        auto error = data["error"]["message"];

        if (error.isUndefined()) {
            command = data["results"][0]["alternatives"][0]["transcript"].toString();
            setRunning(false);
            setCommand(command);

        } else {
            setRunning(false);
            setError(error.toString());
        }

        if(!command.isEmpty())
        {
            QThread *thread = QThread::create([this, &command]{ speak(TR, command); });
            connect(thread,  &QThread::finished,  this,  [=]()
            {
                record();
            });
            thread->start();
        }
        else
        {
            record();
        }
    });

    qDebug() << "Flac location:" << this->filePath;
}

AlsaTranslator::~AlsaTranslator()
{
    audioRecorder.close();
    delete networkAccessManager;
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

    networkAccessManager->post(this->request, data.toJson(QJsonDocument::Compact));
}

void AlsaTranslator::start()
{
    qDebug() << "Alsa Translator is started...";
    record();
}

void AlsaTranslator::execCommand(const char* cmd)
{
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != nullptr)
                result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
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
    execCommand("amixer -c 1 set Mic 100DB");
    execCommand("aplay beep.wav");   
    QThread::msleep(250);
    setError("");
    setCommand("");
    setRunning(true);
    audioRecorder.record(recordDuration);
    execCommand("amixer -c 1 set Mic 0DB");
}

void AlsaTranslator::speak(SType type, QString &text)
{
    if(type==SType::TR)
        speakTr(text);
    else if(type==SType::EN)
        speakEn(text);
}

void AlsaTranslator::speakTr(QString text)
{
    std::string sound = text.toStdString();
    std::string format = soundFormatTr.toStdString();
    std::string espeakBuff = format + std::string(" ")  + '"' + sound + '"' + " --stdout|aplay";
    execCommand(espeakBuff.c_str());
}


void AlsaTranslator::speakEn(QString text)
{
    std::string sound = text.toStdString();
    std::string format = soundFormatEn.toStdString();
    std::string espeakBuff = format + std::string(" ")  + '"' + sound + '"' + " --stdout|aplay";
    execCommand(espeakBuff.c_str());
}
