// Procedural English text-to-speech (rule-based formant synthesis, no recorded data).
//
// Pipeline: text normalization (speech_text.cpp) -> pronunciation: exception dictionary + morphology +
// letter-to-sound rules (speech_dict.cpp, speech_rules.cpp) -> phrasing, post-lexical rules, Klatt duration
// rules and F0 contour (speech_prosody.cpp) -> 2.5 ms parameter frames with Holmes-style coarticulation and a
// Klatt cascade/parallel formant synthesizer driven by an LF glottal source (speech_klatt.cpp).
//
// Unity build: this file #includes the other speech_*.cpp files; add only `#include "audio/speech.cpp"`.
// Thread safety: all state is per call; tables are immutable (function-local statics are initialized once,
// thread-safely, on first use).
#include "speech.h"
#include "speech_internal.h"
#include "speech_phonemes.cpp"
#include "speech_dict.cpp"
#include "speech_rules.cpp"
#include "speech_text.cpp"
#include "speech_prosody.cpp"
#include "speech_klatt.cpp"

namespace Speech {

void synthesize(const char* text, const Audio::VoiceParams& voice, int sampleRate, std::vector<float>& out) {
    if (!text || !*text) return;
    detail::Utterance utt;
    detail::buildUtterance(text, voice, utt);
    if (utt.segs.empty() || utt.total <= 0.f) return;
    std::vector<detail::F0Point> f0;
    detail::buildF0(utt, voice, f0);
    // Deterministic seed: same text + voice -> identical waveform.
    u32 seed = hashString(text);
    u32 vbits[6];
    memcpy(&vbits[0], &voice.pitch, 4);
    memcpy(&vbits[1], &voice.formantScale, 4);
    memcpy(&vbits[2], &voice.speed, 4);
    memcpy(&vbits[3], &voice.breathiness, 4);
    memcpy(&vbits[4], &voice.roughness, 4);
    memcpy(&vbits[5], &voice.expressiveness, 4);
    for (int i = 0; i < 6; i++) seed = hashCombine(seed, vbits[i]);
    detail::render(utt, f0, voice, sampleRate, seed, out);
}

float estimateDuration(const char* text, const Audio::VoiceParams& voice) {
    if (!text || !*text) return 0.f;
    detail::Utterance utt;
    detail::buildUtterance(text, voice, utt);
    if (utt.segs.empty() || utt.total <= 0.f) return 0.f;
    return utt.total + 0.02f;
}

Audio::VoiceParams presetVoice(bool female, u32 seed) {
    Rng r(hash32(seed * 2654435761u + (female ? 0x51ed270bu : 0x2c1b3c6du)), 0x9e3779b97f4a7c15ULL);
    Audio::VoiceParams v;
    // Age: 0 young adult, 1 adult, 2 older. Timbre: 0 neutral, 1 gruff, 2 soft.
    float ages[3] = {0.3f, 0.45f, 0.25f};
    float timbres[3] = {0.55f, 0.22f, 0.23f};
    int age = r.weighted(ages, 3);
    int timbre = r.weighted(timbres, 3);
    if (!female) {
        static const float pitchLo[3] = {112.f, 98.f, 88.f}, pitchHi[3] = {138.f, 124.f, 112.f};
        v.pitch = r.range(pitchLo[age], pitchHi[age]);
        static const float fsLo[3] = {0.99f, 0.95f, 0.92f}, fsHi[3] = {1.05f, 1.01f, 0.98f};
        v.formantScale = r.range(fsLo[age], fsHi[age]);
        v.breathiness = r.range(0.06f, 0.16f);
        v.roughness = r.range(0.f, 0.08f);
        if (timbre == 1) {  // gruff
            v.pitch = std::max(85.f, v.pitch - r.range(6.f, 14.f));
            v.formantScale -= 0.02f;
            v.breathiness = r.range(0.03f, 0.1f);
            v.roughness = r.range(0.3f, 0.55f);
        } else if (timbre == 2) {  // soft
            v.pitch += r.range(2.f, 8.f);
            v.breathiness = r.range(0.25f, 0.4f);
            v.roughness = r.range(0.f, 0.05f);
        }
    } else {
        static const float pitchLo[3] = {200.f, 182.f, 165.f}, pitchHi[3] = {240.f, 222.f, 195.f};
        v.pitch = r.range(pitchLo[age], pitchHi[age]);
        static const float fsLo[3] = {1.16f, 1.12f, 1.10f}, fsHi[3] = {1.22f, 1.18f, 1.15f};
        v.formantScale = r.range(fsLo[age], fsHi[age]);
        v.breathiness = r.range(0.12f, 0.24f);
        v.roughness = r.range(0.f, 0.05f);
        if (timbre == 1) {  // husky
            v.pitch = std::max(165.f, v.pitch - r.range(8.f, 18.f));
            v.breathiness = r.range(0.1f, 0.2f);
            v.roughness = r.range(0.15f, 0.3f);
        } else if (timbre == 2) {  // soft / breathy
            v.breathiness = r.range(0.3f, 0.45f);
        }
    }
    if (age == 2) {
        v.roughness = std::min(0.7f, v.roughness + r.range(0.08f, 0.22f));
        v.breathiness = std::min(0.5f, v.breathiness + 0.04f);
    }
    static const float spLo[3] = {1.02f, 0.95f, 0.86f}, spHi[3] = {1.12f, 1.06f, 0.98f};
    v.speed = r.range(spLo[age], spHi[age]);
    static const float exLo[3] = {0.9f, 0.8f, 0.7f}, exHi[3] = {1.35f, 1.25f, 1.05f};
    v.expressiveness = r.range(exLo[age], exHi[age]);
    if (timbre == 1) v.expressiveness *= 0.85f;
    return v;
}

}  // namespace Speech
