#include "alsatranslator.h"

AlsaTranslator::AlsaTranslator(QObject *parent)
    : QObject(parent)
{
    soundFormatTr = QString("espeak -vtr+f2");
    soundFormatEn = QString("espeak -ven+f2");

    char devname[10] = {0};

    findCaptureDevice(devname);
    qDebug() << "Capture device" << devname;

    if(QString(devname).isEmpty())
        foundCapture = false;
    else
    {
        if (!this->location.exists())
            this->location.mkpath(".");

        this->audioRecorder.setDeviceName(devname);
        this->audioRecorder.setChannels(1);
        this->audioRecorder.setSampleRate(44100);
        this->audioRecorder.initCaptureDevice();
        foundCapture = true;

        this->url.setUrl(baseApi);
        this->url.setQuery("key=" + apiKey);

        this->request.setUrl(this->url);
        this->request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        //this->request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

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
}

AlsaTranslator::~AlsaTranslator()
{
    audioRecorder.close();
}

void AlsaTranslator::findCaptureDevice(char *devname)
{
    int idx, dev, err;
    snd_ctl_t *handle;
    snd_ctl_card_info_t *info;
    snd_pcm_info_t *pcminfo;
    char str[128];
    bool found = false;

    snd_ctl_card_info_alloca(&info);
    snd_pcm_info_alloca(&pcminfo);
    printf("\n");

    idx = -1;
    while (!found)
    {
        if ((err = snd_card_next(&idx)) < 0) {
            printf("Card next error: %s\n", snd_strerror(err));
            break;
        }
        if (idx < 0)
            break;
        sprintf(str, "hw:CARD=%i", idx);
        if ((err = snd_ctl_open(&handle, str, 0)) < 0) {
            printf("Open error: %s\n", snd_strerror(err));
            continue;
        }
        if ((err = snd_ctl_card_info(handle, info)) < 0) {
            printf("HW info error: %s\n", snd_strerror(err));
            continue;
        }

        dev = -1;
        while (1) {
            snd_pcm_sync_id_t sync;
            if ((err = snd_ctl_pcm_next_device(handle, &dev)) < 0) {
                printf("  PCM next device error: %s\n", snd_strerror(err));
                break;
            }
            if (dev < 0)
                break;
            snd_pcm_info_set_device(pcminfo, dev);
            snd_pcm_info_set_subdevice(pcminfo, 0);
            snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_CAPTURE);
            if ((err = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
                printf("Sound card - %i - '%s' has no capture device.\n",
                       snd_ctl_card_info_get_card(info), snd_ctl_card_info_get_name(info));
                continue;
            }
            printf("Sound card - %i - '%s' has capture device.\n", snd_ctl_card_info_get_card(info), snd_ctl_card_info_get_name(info));
            sprintf(devname, "plughw:%d,0", snd_ctl_card_info_get_card(info));
            found = true;
            break;
        }
        snd_ctl_close(handle);
    }

    snd_config_update_free_global();
}

void AlsaTranslator::responseReceived(QNetworkReply *response)
{
    if(m_stop)
        return;

    auto command = QString{};
    auto confidence = 0.0;

    auto data = QJsonDocument::fromJson(response->readAll());
    response->deleteLater();

    auto error = data["error"]["message"];

    if (error.isUndefined()) {
        command = data["results"][0]["alternatives"][0]["transcript"].toString();
        if(!data["results"][0]["alternatives"][0]["confidence"].isUndefined())
        {
            confidence = data["results"][0]["alternatives"][0]["confidence"].toDouble();
        }
    } else {
        setError(error.toString());
    }

    qDebug() << command << confidence;

    if (confidence >= 0.7)
    {
        setCommand(command);
    }
    else
    {
        record();
    }

    setRunning(false);
}

void AlsaTranslator::translate() {

    if(m_stop)
        return;

    QFile file{};
    file.setFileName(this->filePath);

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
        QJsonObject
        {
            {
                "audio",
                QJsonObject {
                    {"content", QJsonValue::fromVariant(fileData.toBase64())}
                }
            },
            {
                "config",
                QJsonObject {
                    {"encoding", "FLAC"},
                    {"languageCode", languageCode},
                    {"model", "command_and_search"},
                    {"enableWordTimeOffsets", false},
                    {"sampleRateHertz", (int)audioRecorder.getSampleRate()}
                }
            }
        }
    };

    fileData.clear();
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

void AlsaTranslator::setLanguageCode(const QString &newLanguageCode)
{
    languageCode = newLanguageCode;
}

const QString &AlsaTranslator::getLanguageCode() const
{
    return languageCode;
}

void AlsaTranslator::record()
{
    if(!foundCapture)
        return;

    const QString fileName = QUuid::createUuid().toString() + ".flac"; // random unique recording name
    this->filePath = location.filePath(fileName);
    this->audioRecorder.setOutputLocation(filePath);

    setRunning(true);
    setError("");
    execCommand((char*)"aplay beep.wav");
    audioRecorder.record(recordDuration);
}

void AlsaTranslator::speak(SType type, QString &text)
{
    if(text.isEmpty())
        return;

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
    std::string espeakBuff = format + std::string(" ")  + '"' + sound + '"' ;
    execCommand((char*)espeakBuff.c_str());
}


void AlsaTranslator::speakEn(QString text)
{
    std::string sound = text.toStdString();
    std::string format = soundFormatEn.toStdString();
    std::string espeakBuff = format + std::string(" ")  + '"' + sound + '"' ;
    execCommand((char*)espeakBuff.c_str());
}


void AlsaTranslator::start()
{
    if(!foundCapture)
        return;

    qDebug() << "Alsa Translator is started...";
    record();
}
