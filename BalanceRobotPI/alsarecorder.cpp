#include "alsarecorder.h"

ALSARecorder::ALSARecorder(QObject *parent)
    : QObject(parent)
{   
}

ALSARecorder::~ALSARecorder()
{
    close();
}


void ALSARecorder::setPause(bool pause)
{
    m_pause = pause;
}

bool ALSARecorder::initCaptureDevice() {

    snd_pcm_hw_params_t *params;

    int err = 0;

    if ((err = snd_pcm_open(&capture_handle, deviceName.toStdString().c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0)

    {
        std::cerr << "cannot open audio device " << capture_handle << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return OPEN_ERROR;
    }

    if ((err = snd_pcm_hw_params_malloc(&params)) < 0)
    {
        std::cerr << "cannot allocate hardware parameter structure " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return MALLOC_ERROR;
    }

    if ((err = snd_pcm_hw_params_any(capture_handle, params)) < 0)
    {
        std::cerr << "cannot initialize hardware parameter structure " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return ANY_ERROR;
    }

    if ((err = snd_pcm_hw_params_set_access(capture_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        std::cerr << "cannot set access type " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return ACCESS_ERROR;
    }

    if ((err = snd_pcm_hw_params_set_format(capture_handle, params, SND_PCM_FORMAT_S16_LE)) < 0)
    {
        std::cerr << "cannot set sample format " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return FORMAT_ERROR;
    }

    if ((err = snd_pcm_hw_params_set_rate_near(capture_handle, params, &sampleRate, 0)) < 0)
    {
        std::cerr << "cannot set sample rate " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return RATE_ERROR;
    }

    if ((err = snd_pcm_hw_params_set_channels(capture_handle, params, channels))< 0)
    {
        std::cerr << "cannot set channel count " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return CHANNELS_ERROR;
    }

    if ((err =  snd_pcm_hw_params_set_period_size_near(capture_handle, params, &frames_per_period, 0)) < 0)
    {
        std::cerr << "cannot set period_size " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return RATE_ERROR;
    }

    if ((err = snd_pcm_hw_params(capture_handle, params)) < 0)
    {
        std::cerr << "cannot set parameters " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return PARAMS_ERROR;
    }

    if ((err = snd_pcm_prepare(capture_handle)) < 0)
    {
        std::cerr << "cannot prepare audio interface for use " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return PREPARE_ERROR;
    }

    if ((err = snd_pcm_start(capture_handle)) < 0)
    {
        std::cerr << "cannot start soundcard " << "(" << snd_strerror(err) << ", " << err << ")" << "\n";
        return START_ERROR;
    }


    unsigned int val;
    int dir;

    snd_pcm_hw_params_get_period_size(params, &frames_per_period, &dir);
    int size = frames_per_period * 4; /* 2 bytes/sample, 2 channels */

    printf("\n");
    //printf("ALSA library version: %s\n", SND_LIB_VERSION_STR);
    printf("Device type: capture\n");
    printf("Device name = '%s'\n", snd_pcm_name(capture_handle));
    snd_pcm_hw_params_get_channels(params, &val);
    printf("Channels = %d\n", val);
    snd_pcm_hw_params_get_rate(params, &val, &dir);
    printf("Rate = %d bps\n", val);
    printf("Size_of_one_frame = %d frames\n", get_frames_per_period());
    val = snd_pcm_hw_params_get_sbits(params);
    printf("Significant bits = %d\n", val);
    printf("Size = %d\n", size);
    printf("\n");
    return true;
}

void ALSARecorder::close() {

    m_pause = true;

    if(capture_handle)
    {
        snd_pcm_drain(capture_handle);
        snd_pcm_close(capture_handle);
    }
}

unsigned int ALSARecorder::getSampleRate() const
{
    return sampleRate;
}

void ALSARecorder::setChannels(unsigned int value)
{
    channels = value;
}

void ALSARecorder::setSampleRate(unsigned int value)
{
    sampleRate = value;
    frames_per_period = (int)(sampleRate/60.0);
}

void ALSARecorder::setOutputLocation(const QString &value)
{
    fileName = value;
}

void ALSARecorder::setDeviceName(const QString &value)
{
    deviceName = value;
}

char* ALSARecorder::allocate_buffer() {
    unsigned int size_of_one_frame = (snd_pcm_format_width(format)/8) * channels;
    return (char*) calloc(frames_per_period, size_of_one_frame);
}

unsigned int ALSARecorder::get_frames_per_period() {
    return frames_per_period;
}

unsigned int ALSARecorder::get_bytes_per_frame() {
    unsigned int size_of_one_frame = (snd_pcm_format_width(format)/8) * channels;
    return size_of_one_frame;
}

unsigned int ALSARecorder::capture_into_buffer(char* buffer) {

    snd_pcm_sframes_t frames_read = snd_pcm_readi(capture_handle, buffer, get_frames_per_period());


    if(frames_read == 0) {
        fprintf(stderr, "End of file.\n");
        return 0;
    }
    else if (frames_read == -EPIPE) {
        /* EPIPE means overrun */
        snd_pcm_prepare(capture_handle);
        return 0;
    }
    return frames_read;
}

bool ALSARecorder::initFlacDecoder(char *flacfile)
{
    FLAC__bool ok = true;
    FLAC__StreamEncoderInitStatus initStatus;
    totalSamples = 0;

    /* allocate the encoder */
    if((pcm_encoder = FLAC__stream_encoder_new()) == NULL)
    {
        fprintf(stderr, "ERROR: allocating encoder\n");
        return false;
    }

    ok &= FLAC__stream_encoder_set_verify(pcm_encoder, true);
    ok &= FLAC__stream_encoder_set_compression_level(pcm_encoder, 5);
    ok &= FLAC__stream_encoder_set_channels(pcm_encoder, channels);
    ok &= FLAC__stream_encoder_set_bits_per_sample(pcm_encoder, bps);
    ok &= FLAC__stream_encoder_set_sample_rate(pcm_encoder, sampleRate);
    ok &= FLAC__stream_encoder_set_total_samples_estimate(pcm_encoder, totalSamples);

    /* initialize encoder */
    if(ok)
    {
        initStatus = FLAC__stream_encoder_init_file(pcm_encoder, flacfile, nullptr, nullptr);
        if(initStatus != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
        {
            fprintf(stderr, "ERROR: initializing encoder: %s\n", FLAC__StreamEncoderInitStatusString[initStatus]);
            ok = false;
        }
    }

    return ok;
}

bool ALSARecorder::finishFlacDecoder()
{
    if(!pcm_encoder)
        return false;

    if (!FLAC__stream_encoder_finish(pcm_encoder))
    {
        fprintf(stderr, "ERROR: finishing encoder: %s\n", FLAC__StreamEncoderStateString[FLAC__stream_encoder_get_state(pcm_encoder)]);
        return false;
    }

    FLAC__stream_encoder_delete(pcm_encoder);

    return true;
}

bool ALSARecorder::record(int mseconds)
{  
    if(m_pause)
        return false;

    if(!capture_handle)
        return false;

    qDebug() << "Listening...";

    QThread *thread = QThread::create([this, &mseconds]{

        FLAC__bool flac_ok = true;
        FLAC__int32 pcm[get_frames_per_period() * channels];

        initFlacDecoder((char*)fileName.toStdString().c_str());

        char* buffer = allocate_buffer();
        auto endwait = QDateTime::currentMSecsSinceEpoch() + mseconds;

        do
        {
            if(m_pause)
            {
                break;
            }

            auto read = capture_into_buffer(buffer);

            if(read == 0)
                continue;

            totalSamples = read;
            size_t left = (size_t)totalSamples;
            while(flac_ok && left)
            {
                /* convert the packed little-endian 16-bit PCM samples from WAVE into an interleaved FLAC__int32 buffer for libFLAC */
                size_t i;
                for(i = 0; i < read*channels; i++)
                {
                    /* inefficient but simple and works on big- or little-endian machines */
                    pcm[i] = (FLAC__int32)(((FLAC__int16)(FLAC__int8)buffer[2 * i + 1] << 8) | (FLAC__int16)buffer[2 * i]);
                }
                /* feed samples to encoder */
                flac_ok = FLAC__stream_encoder_process_interleaved(pcm_encoder, pcm, read);
                left -= read;
            }


        } while (QDateTime::currentMSecsSinceEpoch() < endwait);

        finishFlacDecoder();

    });
    connect(thread,  &QThread::finished,  this,  [=]()
    {
        emit stateChanged(StoppedState);
    });
    thread->start();
    thread->wait();
    return true;
}
