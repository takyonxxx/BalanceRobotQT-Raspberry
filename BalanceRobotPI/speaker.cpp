#include "speaker.h"
Speaker *Speaker::theInstance_= nullptr;

int SynthCallback(short *wav, int numsamples, espeak_EVENT *events) {
  std::cout << "Callback: ";
  for (unsigned int i(0); events[i].type != espeakEVENT_LIST_TERMINATED; i++) {
    if (i != 0) {
      std::cout << ", ";
    }
    switch (events[i].type) {
      case espeakEVENT_LIST_TERMINATED:
        std::cout << "espeakEVENT_LIST_TERMINATED";
        break;
      case espeakEVENT_WORD:
        std::cout << "espeakEVENT_WORD";
        break;
      case espeakEVENT_SENTENCE:
        std::cout << "espeakEVENT_SENTENCE";
        break;
      case espeakEVENT_MARK:
        std::cout << "espeakEVENT_MARK";
        break;
      case espeakEVENT_PLAY:
        std::cout << "espeakEVENT_PLAY";
        break;
      case espeakEVENT_END:
        std::cout << "espeakEVENT_END";
        break;
      case espeakEVENT_MSG_TERMINATED:
        std::cout << "espeakEVENT_MSG_TERMINATED";
        break;
      case espeakEVENT_PHONEME:
        std::cout << "espeakEVENT_PHONEME";
        break;
      case espeakEVENT_SAMPLERATE:
        std::cout << "espeakEVENT_SAMPLERATE";
        break;
      default:
        break;
    }
  }
  std::cout << std::endl;
  return 0;
}


Speaker* Speaker::getInstance()
{
    if (theInstance_ == nullptr)
    {
        theInstance_ = new Speaker();
    }
    return theInstance_;
}

Speaker::Speaker(QObject *parent) : QObject(parent)
{
    espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, Buflength, nullptr, 1 << 15);
    //espeak_SetSynthCallback(SynthCallback);
}

Speaker::~Speaker()
{
}

void Speaker::speak(QString &text)
{
    if(text.isEmpty())
        return;

    if(languageCode==TR)
        speak_by_language(text, "TR");
    else if(languageCode==EN)
        speak_by_language(text, "EN");
}

void Speaker::speak_by_language(QString text, QString lang)
{
    if (espeak_IsPlaying())
        espeak_Cancel();

    espeak_SetParameter(espeakVOLUME, 100, 0);

    espeak_VOICE voice;
    memset(&voice, 0, sizeof(espeak_VOICE)); // Zero out the voice first
    voice.languages = lang.toLower().toStdString().c_str();
    voice.name = lang.toStdString().c_str();
    voice.variant = 2;
    voice.gender = 2;
    espeak_SetVoiceByProperties(&voice);
    auto size = strlen(text.toStdString().c_str())+1;

    espeak_Synth( text.toStdString().c_str(), size, 0, POS_CHARACTER, 0, espeakCHARS_AUTO | espeakPHONEMES | espeakENDPAUSE, nullptr, nullptr );
    espeak_Synchronize( );
}

void Speaker::setLanguageCode(SType newLanguageCode)
{
    languageCode = newLanguageCode;
    if(languageCode==TR)
        espeak_SetVoiceByName("Turkish");
    else if(languageCode==EN)
        espeak_SetVoiceByName("English");
}
