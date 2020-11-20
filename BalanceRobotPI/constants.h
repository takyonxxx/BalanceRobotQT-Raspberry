#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QtCore>
#include <pocketsphinx.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/err.h>

#include <alsa/asoundlib.h>
#include </usr/include/alsa/pcm.h>

#include <sys/stat.h>
#include <string>
#include <iostream>
#include <complex>

using namespace std;

#define REAL 0
#define IMAG 1

#define ALSA_PCM_NEW_HW_PARAMS_API

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

// this is the bitrate
#define MAX_SAMPLES     512000 //wav
#define SAMPLING_RATE   44100

static QString KEY_TEST                 = "TEST";
static QString KEY_HEY                  = "HEY";
static QString KEY_ROBOT                = "ROBOT";
static QString KEY_START                = "START";
static QString KEY_STOP                 = "STOP";
static QString KEY_FORWARD              = "FORWARD";
static QString KEY_BACKWARD             = "BACKWARD";
static QString KEY_LEFT                 = "LEFT";
static QString KEY_RIGHT                = "RIGHT";
static QString KEY_GO                   = "GO";
static QString KEY_COME                 = "COME";
static QString KEY_GOODBYE              = "GOODBYE";
static QString KEY_WHAT_IS_YOUR_NAME    = "WHAT IS YOUR NAME?";
static QString KEY_WHERE_ARE_YOU        = "WHERE ARE YOU?";
static QString KEY_WHO_ARE_YOU          = "WHO ARE YOU?";
static QString KEY_ARE_YOU_OK           = "ARE YOU OK?";


#define MODELDIR "/usr/local/share/pocketsphinx/model"

#define MHZ(x)                      ((x)*1000*1000)
#define KHZ(x)                      ((x)*1*1000)

inline double GetFrequencyIntensity(double re, double im)
{
    return sqrt((re*re)+(im*im));
}
#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))
#define Amplitude(re,im,len) (GetFrequencyIntensity(re,im)/(len))
#define AmplitudeScaled(re,im,len,scale) ((int)Amplitude(re,im,len)%scale)

#define KF_VAR_ACCEL 0.0075 // Variance of value acceleration noise input.
#define KF_VAR_MEASUREMENT 0.05

const snd_pcm_format_t FORMAT = SND_PCM_FORMAT_S16_BE;

enum class RequestType
{
    Record,
    Play,
    Data,
    Speech
};

struct wav_header // Wav file header structure
{
    uint8_t ChunkID[4];
    uint32_t ChunkSize;
    uint8_t Format[4];
    uint8_t Subchunk1ID[4];
    uint32_t Subchunk1Size;
    uint16_t AudioFormat;
    uint16_t NumChannels;
    uint32_t SampleRate;
    uint32_t ByteRate;
    uint16_t BlockAlign;
    uint16_t BitsPerSample;
    uint8_t Subchunk2ID[4];
    uint32_t Subchunk2Size;
};

const string empty_string = string();

static void createFile(QString &fileName)
{
    QString data;
    QFile *temp{};

    QFile file(fileName);
    if(!file.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "%s file not opened\n", fileName.toStdString().c_str());
        return;
    }
    else
    {
        data = file.readAll();
    }

    file.close();
    if(!temp)
    {
        QString filename = fileName.remove(":").remove("/").remove("_win");
        temp = new QFile(filename);
        if (temp->open(QIODevice::ReadWrite)) {
            QTextStream stream(temp);
            stream << data << endl;
        }
    }
}


static int FileExists(char *path)
{
    struct stat fileStat;
    if ( stat(path, &fileStat) )
    {
        return 0;
    }
    if ( !S_ISREG(fileStat.st_mode) )
    {
        return 0;
    }
    return 1;
}

static int DirExists(char *path)
{
    struct stat fileStat;
    if ( stat(path, &fileStat) )
    {
        return 0;
    }
    if ( !S_ISDIR(fileStat.st_mode) )
    {
        return 0;
    }
    return 1;
}

#endif
