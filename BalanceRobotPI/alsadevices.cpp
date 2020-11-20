#include "alsadevices.h"

void ALSAPCMDevice::setBreak(bool val)
{
    m_break = val;
}

ALSAPCMDevice::ALSAPCMDevice(QObject *parent, char* device_name, int channels)
    : QThread(parent), device_name(device_name), channels(channels)
{
    QString argType{"-s"};
    r_type = RequestType::Speech;

    sample_rate = SAMPLING_RATE;
    frames_per_period = (int)(sample_rate/60.0); ;    
}

bool ALSAPCMDevice::init()
{
    QString fileName = ":/robot.dic";
    createFile(fileName);
    fileName = ":/robot.lm";
    createFile(fileName);

    QString logPath = QDir().currentPath() + "/errors.log";
    QString lmPath = QDir().currentPath() + "/robot.lm";
    QString dicPath = QDir().currentPath() + "/robot.dic";


    if (!(config = cmd_ln_init(NULL, ps_args(), TRUE,                                         // Load the configuration structure - ps_args() passes the default values
                                    "-hmm", MODELDIR "/en-us/en-us",     // path to the standard english language model
                                    "-lm", lmPath.toStdString().c_str(),                                             // custom language model (file must be present)
                                    "-dict", dicPath.toStdString().c_str(),                                          // custom dictionary (file must be present)
                                    "-remove_noise", "yes",
                                    "-logfn", logPath.toStdString().c_str(),
                                    nullptr)))
    {
        return false;
    }

    if (!(mDecoder = ps_init(config)))
        return false;

    if (!(mDevice = ad_open_dev(device_name.c_str(), (int) cmd_ln_float32_r(config, "-samprate"))))
    {
        std::cout << "Capture device not found..." << std::endl;
        std::cout << "Voice recognize disabled..." << std::endl;
        m_stop = true;
        return false;
    }

    return true;
}

string ALSAPCMDevice::recognize_from_microphone()
{
    bool uttStarted = false;
    const char* data = nullptr;
    int16 buffer[frames_per_period * 4];

    if(ad_start_rec(mDevice) >= 0 && ps_start_utt(mDecoder) >= 0)
    {
        while(true)
        {
            if(!m_pause && !m_break)
            {
                int32 numberSamples = ad_read(mDevice, buffer, frames_per_period * 4);
                if(ps_process_raw(mDecoder, buffer, numberSamples, 0, 0) < 0) break;
                bool inSpeech = (ps_get_in_speech(mDecoder) > 0) ? true : false;
                if(inSpeech && !uttStarted) uttStarted = true;
                if(!inSpeech && uttStarted)
                {
                    ps_end_utt(mDecoder);
                    ad_stop_rec(mDevice);
                    data = ps_get_hyp(mDecoder, nullptr);
                    if(data) return string(data);
                    break;
                }
            }

            QThread::msleep(25);
        }
    }
    ps_end_utt(mDecoder);
    ad_stop_rec(mDevice);

    return empty_string;
}

void ALSAPCMDevice::pause_recognize()
{
    if(m_stop)
        return;

    m_pause = true;
    std::cout << "Recognize paused..."<< std::endl;
}

void ALSAPCMDevice::resume_recognize()
{
    if(m_stop)
        return;

    m_pause = false;
    std::cout << "Recognize resumed..."<< std::endl;
}

void ALSAPCMDevice::run()
{
    if(m_stop)
        return;

    std::cout << "Voice recognize started..." << std::endl;

    while (true)
    {
        if(m_stop)
            break;

        if(!m_pause && !m_break)
        {
            decoded_speech = recognize_from_microphone();

            if(!QString(decoded_speech.c_str()).isEmpty())
            {
                auto speech = QString(decoded_speech.c_str());
                emit decodedSpeech(speech);
            }
        }

        QThread::msleep(1);
    }

    ad_close(mDevice);

}
