#ifndef __ALSADevices_H
#define __ALSADevices_H

#include <QThread>
#include <QDateTime>
#include <QDebug>
#include <alsa/asoundlib.h>
#include </usr/include/alsa/pcm.h>
#include "FLAC/metadata.h"
#include "FLAC/stream_encoder.h"

#define OPEN_ERROR        1
#define MALLOC_ERROR      2
#define ANY_ERROR         3
#define ACCESS_ERROR      4
#define FORMAT_ERROR      5
#define RATE_ERROR        6
#define CHANNELS_ERROR    7
#define PARAMS_ERROR      8
#define PREPARE_ERROR     9
#define FOPEN_ERROR       10
#define FCLOSE_ERROR      11
#define SNDREAD_ERROR     12
#define START_ERROR       13

#include <iostream>
#include <complex>
#include "kalmanfilter.h"

using namespace std;

#define READSIZE 1024
#define REAL 0
#define IMAG 1
#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))
#define Amplitude(re,im,len) (GetFrequencyIntensity(re,im)/(len))
#define AmplitudeScaled(re,im,len,scale) ((int)Amplitude(re,im,len)%scale)


static unsigned totalSamples{};

class ALSARecorder:public QObject
{
    Q_OBJECT

protected:
    snd_pcm_t* capture_handle{};
    snd_pcm_uframes_t frames_per_period{};            // Latency - lower numbers will decrease latency and increase CPU usage.
    snd_pcm_format_t format{SND_PCM_FORMAT_S16_BE};
    FLAC__StreamEncoder *pcm_encoder{nullptr};
    bool m_pause{false};
public:

    explicit ALSARecorder(QObject *parent = nullptr);
    ~ALSARecorder();

    enum State
    {
        StoppedState,
        RecordingState,
        PausedState
    };

    bool initCaptureDevice();

    void setDeviceName(const QString &value);
    void setOutputLocation(const QString &value);
    void setSampleRate(unsigned int value);
    unsigned int getSampleRate() const;
    void setChannels(unsigned int value);
    float GetMicLevel();
    float processRawData(char* buffer, int cap_size);
    bool record(int mseconds);
    void close();

    void setPause(bool pause);

    unsigned int getChannels() const;

private:
    QString deviceName;
    QString fileName;
    unsigned int sampleRate;
    unsigned int channels;
    unsigned bps{16};

    KalmanFilter *kalman_filter;
    long start_time{0};
    long end_time{0};
    long dt{0};

    bool initFlacDecoder(char* flacfile);
    bool finishFlacDecoder();

    char* allocate_buffer();
    unsigned int get_frames_per_period();
    unsigned int get_bytes_per_frame();

    unsigned int capture_into_buffer(char* buffer);
    int encodePcm(const char *wavfile, const char *flacfile);

signals:
    void stateChanged(State state);
};

#endif
