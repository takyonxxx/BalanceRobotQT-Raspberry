﻿#include "alsatranslator.h"

AlsaTranslator::AlsaTranslator(QObject *parent)
    : QObject(parent)
{
    soundFormatTr = QString("espeak -vtr+f2");
    soundFormatEn = QString("espeak -ven+f2");

    setRunning(false);

    char capture_devname[10] = {0};

    findCaptureDevice(capture_devname);
    qDebug() << "Capture device" << capture_devname;

    if(QString(capture_devname).isEmpty())
        foundCapture = false;
    else
    {
        if (!this->location.exists())
            this->location.mkpath(".");

        this->audioRecorder.setDeviceName(capture_devname);
        this->audioRecorder.setChannels(1);
        this->audioRecorder.setSampleRate(44100);
        this->audioRecorder.initCaptureDevice();
        foundCapture = true;

        this->url.setUrl(baseSpeechApi);
        this->url.setQuery("key=" + apiSpeechKey);

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
                printf("PCM next device error: %s\n", snd_strerror(err));
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

void AlsaTranslator::findPlaybackDevice(char *devname)
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
                printf("PCM next device error: %s\n", snd_strerror(err));
                break;
            }
            if (dev < 0)
                break;
            snd_pcm_info_set_device(pcminfo, dev);
            snd_pcm_info_set_subdevice(pcminfo, 0);
            snd_pcm_info_set_stream(pcminfo, SND_PCM_STREAM_PLAYBACK);
            if ((err = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
                printf("Sound card - %i - '%s' has no playback device.\n",
                       snd_ctl_card_info_get_card(info), snd_ctl_card_info_get_name(info));
                continue;
            }
            printf("Sound card - %i - '%s' has playback device.\n", snd_ctl_card_info_get_card(info), snd_ctl_card_info_get_name(info));
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
        qDebug() << error.toString();
    }

    if(!command.isEmpty())
    {
        setCommand(command);
    }
    else
    {
        record();
    }
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

    QString language = "en-US";

    if (languageCode == TR)
        language = "tr-TR";


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
                    {"languageCode", language},
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

void AlsaTranslator::setLanguageCode(SType newLanguageCode)
{
    languageCode = newLanguageCode;
}

const SType &AlsaTranslator::getLanguageCode() const
{
    return languageCode;
}

void AlsaTranslator::record()
{
    if(!foundCapture)
        return;

    if(m_stop)
        return;

    while(getRunning())
    {
        QThread::sleep(1);
        qDebug() << "waiting";
        continue;
    }

    const QString fileName = QUuid::createUuid().toString() + ".flac"; // random unique recording name

    this->filePath = location.filePath(fileName);
    this->audioRecorder.setOutputLocation(filePath);    
    //execCommand((char*)"amixer -c 1 set Mic 16");
    audioRecorder.record(recordDuration);
    //execCommand((char*)"amixer -c 1 set Mic 0");
    filecount++;
}


void AlsaTranslator::start()
{
     m_stop = false;
     record();

    /*QThread *thread = QThread::create([this]
    {
        qDebug() << "Alsa Translator is listening.";

        //record();
        while (true)
        {
            if (m_stop)
                break;

            if (getRunning() || ignore_record)
            {
                if(ignore_record)
                    ignore_record = false;
                QThread::msleep(10);
                continue;
            }

            record();

            //auto dedect sound, should be improved.

            double len = 10;
            double val = audioRecorder.GetMicLevel();
            double instance; //Increment each time input accepted.

            deque<double> dq;
            if(instance < len){
                dq.push_back(val);
            }
            else {
                dq.pop_front();
                dq.push_back(val);
            }
            double cs = 0;
            for(auto i : dq){
                cs += i;
            }
            double decibel = cs / len;
            if (decibel > dedect_sound_decibel)
            {
               qDebug() << decibel;
               record();
            }

            QThread::msleep(50);
        }
    });
    thread->start();*/
}

