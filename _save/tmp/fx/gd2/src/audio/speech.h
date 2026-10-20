// Procedural text-to-speech: English text -> phonemes -> prosody -> formant synthesis.
// Pure computation, thread-safe (no mutable global state), no OS dependencies.
#pragma once
#include "audio.h"

namespace Speech {

// Synthesizes `text` into mono float PCM in [-1,1] at `sampleRate` (appends to `out`).
void synthesize(const char* text, const Audio::VoiceParams& voice, int sampleRate, std::vector<float>& out);

// Approximate spoken duration in seconds (cheap; no synthesis).
float estimateDuration(const char* text, const Audio::VoiceParams& voice);

// Convenience voice presets (deterministic variety for NPCs): seed picks age/gender/timbre.
Audio::VoiceParams presetVoice(bool female, u32 seed);

}  // namespace Speech
