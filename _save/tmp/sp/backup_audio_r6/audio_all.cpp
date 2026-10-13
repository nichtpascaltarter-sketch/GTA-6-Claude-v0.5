// Unity include for the audio module (procedural SFX, emitters, ambience, music, radio, mixer and
// WASAPI output). Add `#include "audio/audio_all.cpp"` to the unity build. speech.cpp (formant TTS)
// is a separate module and must be included separately.
#include "dsp.h"
#include "audio_internal.h"
#include "emitters.cpp"
#include "sfx.cpp"
#include "ambience.cpp"
#include "music.cpp"
#include "radio.cpp"
#include "audio.cpp"
#include "wasapi.cpp"
