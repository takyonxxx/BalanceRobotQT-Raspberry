#ifndef __ALSADevices_H
#define __ALSADevices_H

#include "constants.h"

class ALSAPCMDevice: public QThread
{
    Q_OBJECT

protected:
    std::string device_name;
    unsigned int sample_rate, channels;             // Quality of the recorded audio.
    snd_pcm_uframes_t frames_per_period;            // Latency - lower numbers will decrease latency and increase CPU usage.
    RequestType r_type;
    bool m_stop{false};
    bool m_pause{false};
    bool m_break{false};
public:
    ALSAPCMDevice(QObject* parent=nullptr, char* device_name = (char*)"plughw:0,0", int channels = 2);

    ~ALSAPCMDevice(){};

    bool init();
    void pause_recognize();
    void resume_recognize();
    void setBreak(bool val);

private:
    string recognize_from_microphone();

    uint8 utt_started, in_speech;            // flags for tracking active speech - has speech started? - is speech currently happening?
    string decoded_speech;
    ps_decoder_t *mDecoder{};                  // create pocketsphinx decoder structure
    cmd_ln_t *config{};                        // create configuration structure
    ad_rec_t *mDevice{};                       // create audio recording structure - for use with ALSA functions

signals:
    void decodedSpeech(QString speech);

protected:
    void run() override; // reimplemented from QThread
};

#endif
