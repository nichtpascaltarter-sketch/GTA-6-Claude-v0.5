// Speech extensions: speaking styles (inline markup), character personas and lip-sync timing.
// Implemented in the speech unity chain (audio/speech.cpp); include this header to call them.
// Pure computation, thread-safe (no mutable global state), deterministic.
#pragma once
#include "speech.h"

namespace Speech {

// ---- Speaking style ----------------------------------------------------------------------------------------------
// Text passed to synthesize() / estimateDuration() / lipSync() may contain inline tags. Tags are silent and
// case-insensitive; a tag applies from its position to the end of the text or until the next tag of its kind:
//   emotion:  [neutral] [angry] [scared] [calm] [sad] [happy] [shout] [whisper]   intensity: [angry:0.5] (0..1.5)
//   delivery: [talk] [dj] [ad] [fineprint] [news] [dispatch]
//   accent:   [accent:general|south|newyork|latino|caribbean|british]            strength: [accent:south:0.6] (0..1)
//   pause:    [pause] (0.5 s) or [pause:1.2] (seconds) after the preceding word
// Any other [bracketed text] is a stage direction and is not spoken ("[laughs]"). displayText() removes all markup,
// so the same string can drive both the voice and the subtitle.
enum Emotion : u8 {
    EMOTION_NEUTRAL = 0,
    EMOTION_ANGRY,    // higher, wider pitch, pressed bright voice, faster, clipped pauses, hard stressed syllables
    EMOTION_SCARED,   // high pitch with tremor, breathy, fast, rising phrase ends
    EMOTION_CALM,     // low narrow pitch, relaxed voice, slower with longer pauses
    EMOTION_SAD,      // low flat pitch, breathy lax voice, slow, creaky phrase ends
    EMOTION_HAPPY,    // high lively pitch, "smiling" formants, brisk
    EMOTION_SHOUT,    // raised pitch and jaw opening, pressed loud voice, lengthened stressed syllables
    EMOTION_WHISPER,  // no voicing (noise-excited), slower; output peak ~0.45 instead of 0.8
    EMOTION_COUNT
};
enum Delivery : u8 {
    DELIVERY_TALK = 0,   // conversation (default)
    DELIVERY_DJ,         // radio host: energetic, wide swooping pitch, fast, punchy stress
    DELIVERY_AD,         // commercial announcer: fast, bright, emphatic sales pitch
    DELIVERY_FINEPRINT,  // the legal disclaimer at the end of an ad: very fast, flat and quiet
    DELIVERY_NEWS,       // newsreader: measured, authoritative falls, careful articulation
    DELIVERY_DISPATCH,   // police/taxi dispatcher: fast, flat, clipped list intonation
    DELIVERY_COUNT
};
enum Accent : u8 {
    ACCENT_GENERAL = 0,  // General American
    ACCENT_SOUTH,        // Southern US: drawl, "ah" for "I", pin/pen merger, fronted "oo", -in'
    ACCENT_NEWYORK,      // New York: non-rhotic, raised "aw" (cawfee), tensed "a", "da" for "the"
    ACCENT_LATINO,       // Spanish-influenced: syllable timing, full vowels, tense i/u, unaspirated stops
    ACCENT_CARIBBEAN,    // Caribbean: th-stopping, non-rhotic, monophthong ey/ow, lilting pitch
    ACCENT_BRITISH,      // British RP: non-rhotic, clear /t/, broad "a" (bath), rounded "o", "oh" for ow
    ACCENT_COUNT
};

struct Style {
    u8 emotion = EMOTION_NEUTRAL;
    u8 delivery = DELIVERY_TALK;
    u8 accent = ACCENT_GENERAL;
    float intensity = 1.f;       // emotion strength (0..1.5)
    float accentStrength = 1.f;  // accent strength (0..1)
};

// Markup prefix selecting `s`, e.g. "[accent:latino:0.6][angry]" (empty for the default style).
std::string styleTags(const Style& s);
// `text` without markup: tags, stage directions and *emphasis* stars removed, spaces tidied (for subtitles).
std::string displayText(const char* text);

// ---- Personas ----------------------------------------------------------------------------------------------------
// A cast voice: timbre (VoiceParams) plus default accent / delivery / mood. Speak a line as the persona with
// synthesize(p.tags() + line, p.voice, ...); a tag inside the line (e.g. "[angry]") overrides the default.
struct Persona {
    Audio::VoiceParams voice;
    Style style;
    std::string tags() const { return styleTags(style); }
};
// Story characters ("mari", "dex", "cast_tomas", "cast_lucha", "cast_rook", "cast_kit", "cast_jonah",
// "cast_sandoval", "cast_holt", "cast_cuervo", "cast_thug_a".."cast_thug_d", "cast_guard_a"/"_b", "cast_cop_a"/"_b",
// "cast_bouncer", "cast_docker", "cast_reporter", "cast_banker", "cast_pilot", "cast_mechanic") and stock roles
// ("cop", "dispatcher", "dj", "dj_female", "announcer", "announcer_female", "newsreader", "newsreader_female",
// "redneck", "tourist", "gangster", "old_woman", "old_man", "kid"). Keys are case-insensitive; unknown keys get a
// stable voice hashed from the key (gender from `femaleIfUnknown`).
Persona persona(const char* key, bool femaleIfUnknown = false);

// ---- Lip sync ----------------------------------------------------------------------------------------------------
// Viseme set: the common 15-shape set (Oculus/Meta lip-sync order).
enum Viseme : u8 {
    VISEME_SIL = 0,  // rest (silence, pauses)
    VISEME_PP,       // p b m           lips pressed together
    VISEME_FF,       // f v             lower lip under the upper teeth
    VISEME_TH,       // th (thin, this) tongue tip between the teeth
    VISEME_DD,       // t d             tongue tip on the ridge, jaw slightly open
    VISEME_KK,       // k g ng h        back of the tongue raised, jaw open
    VISEME_CH,       // ch j sh zh      lips pushed forward, teeth nearly closed
    VISEME_SS,       // s z             teeth nearly closed, lips spread
    VISEME_NN,       // n l             tongue tip up, jaw relaxed
    VISEME_RR,       // r er            lips slightly rounded and forward
    VISEME_AA,       // aa ae ah (a)    jaw wide open
    VISEME_E,        // eh ey           jaw half open, lips spread
    VISEME_I,        // ih iy y         jaw nearly closed, lips spread
    VISEME_O,        // ao ow oy        jaw open, lips rounded
    VISEME_U,        // uw uh w         lips rounded and protruded
    VISEME_COUNT
};
const char* visemeName(int v);  // "sil", "PP", "FF", "TH", "DD", "kk", "CH", "SS", "nn", "RR", "aa", "E", "I", "O", "U"

struct VisemeKey {
    float time;      // seconds from the first sample of synthesize(text, voice, ...) (at any sample rate)
    float duration;  // seconds until the next key (keys are contiguous; the last key is VISEME_SIL)
    u8 viseme;       // Viseme
    float weight;    // articulation strength 0..1 (mouth opening for vowels; 0 at rest)
};
// Viseme keys with the exact timing of synthesize() for the same text + voice, computed without rendering audio
// (~0.1 ms per sentence). Blend between consecutive keys (e.g. 40-60 ms crossfades) for smooth mouths.
void lipSync(const char* text, const Audio::VoiceParams& voice, std::vector<VisemeKey>& out);

struct PhonemeTiming {
    float start, duration;  // seconds, same timeline as lipSync()
    char name[4];           // ARPAbet ("AA", "SH", "DX" flap, "Q" glottal stop, "SIL" pause)
    u8 stress;              // vowels: 0 unstressed, 1 primary, 2 secondary
    u8 viseme;              // Viseme
    i16 word;               // index of the spoken word (-1 for pauses)
};
void phonemeTiming(const char* text, const Audio::VoiceParams& voice, std::vector<PhonemeTiming>& out);

}  // namespace Speech
