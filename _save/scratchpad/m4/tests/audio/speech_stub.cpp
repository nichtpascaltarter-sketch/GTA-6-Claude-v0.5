// Test-only stand-in for the formant TTS (used when AUDIO_TEST_SPEECH_STUB is defined): produces a
// buzzy vowel-like tone whose length follows estimateDuration().
#include "audio/speech_ext.h"
namespace Speech {
float estimateDuration(const char* text, const Audio::VoiceParams& voice) {
    int words = 1;
    for (const char* p = text; *p; p++) words += (*p == ' ');
    return (0.28f * (float)words + 0.2f) / Max(0.3f, voice.speed);
}
void synthesize(const char* text, const Audio::VoiceParams& voice, int sampleRate, std::vector<float>& out) {
    float dur = estimateDuration(text, voice);
    int n = (int)(dur * (float)sampleRate);
    size_t base = out.size();
    out.resize(base + (size_t)n);
    float ph = 0.f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)sampleRate;
        float f0 = voice.pitch * (1.f + 0.1f * sinf(t * 3.f));
        ph += f0 / (float)sampleRate;
        ph -= floorf(ph);
        float syl = 0.5f + 0.5f * sinf(t * 2.f * 3.14159f * 4.f);
        out[base + (size_t)i] = (2.f * ph - 1.f) * 0.3f * syl * Min(1.f, t * 20.f) * Min(1.f, (dur - t) * 20.f);
    }
}
Audio::VoiceParams presetVoice(bool female, u32 seed) {
    Audio::VoiceParams v;
    v.pitch = female ? 200.f + (float)(seed % 40u) : 110.f + (float)(seed % 30u);
    v.formantScale = female ? 1.15f : 1.f;
    return v;
}
// speech_ext.h entry points used by the audio module (radio hosts, crowd beds)
std::string styleTags(const Style&) { return std::string(); }
Persona persona(const char* key, bool femaleIfUnknown) {
    Persona p;
    p.voice = presetVoice(femaleIfUnknown, key ? (u32)strlen(key) : 0u);
    return p;
}
void walla(const WallaParams& wp, int sampleRate, std::vector<float>& out) {
    // stub murmur: low-level filtered noise of the requested length
    size_t n = (size_t)(Clamp(wp.seconds, 2.f, 120.f) * (float)sampleRate);
    u32 st = wp.seed * 2654435761u + 1u;
    float y = 0.f;
    for (size_t i = 0; i < n; i++) {
        st = st * 1664525u + 1013904223u;
        float w = (float)(st >> 8) * (1.f / 8388608.f) - 1.f;
        y += 0.05f * (w - y);
        out.push_back(y * 0.5f);
    }
}
}  // namespace Speech
