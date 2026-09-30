// Speech extensions: speaking styles (inline markup), transmission channels (megaphone / PA / radio / phone), crowd
// walla, character personas, lip-sync timing and facial-expression cues.
// Implemented in the speech unity chain (audio/speech.cpp); include this header to call them.
// Pure computation, thread-safe (no mutable global state), deterministic.
#pragma once
#include "speech.h"

namespace Speech {

// ---- Speaking style ----------------------------------------------------------------------------------------------
// Text passed to synthesize() / estimateDuration() / lipSync() may contain inline tags. Tags are silent and
// case-insensitive; a tag applies from its position to the end of the text or until the next tag of its kind:
//   emotion:  [neutral] [angry] [scared] [calm] [sad] [happy] [shout] [whisper] [drunk]  intensity: [angry:0.5]
//   delivery: [talk] [dj] [ad] [fineprint] [news] [dispatch]
//   accent:   [accent:general|south|newyork|latino|caribbean|british]            strength: [accent:south:0.6] (0..1)
//   timbre:   [timbre:normal|nasal|husky|gravelly|bright|dark|fry|aged] (or bare [husky], [gravelly], ...)
//   pause:    [pause] (0.5 s) or [pause:1.2] (seconds) after the preceding word
//   take:     [take:N] a different performance of the same line (repeated barks don't sound cloned)
//   channel:  [megaphone] [pa] [radio] [phone] (whole line: police megaphone, public-address hall with reverb,
//             two-way radio with squelch, telephone); the output then includes the channel's tail (channelTail())
//   fluency:  [fluent] no automatic hesitations / breaths (long conversational lines get an occasional "uh" and
//             breath intakes, deterministically; broadcast deliveries never do)
// Common stage directions are voiced as nonverbal sounds: [laughs] [chuckles] [giggles] [sighs] [gasps] [coughs]
// [hmm] [scoffs] [groans] [sobs] [yawns]. Any other [bracketed text] is a silent stage direction. displayText()
// removes all markup, so the same string can drive both the voice and the subtitle.
enum Emotion : u8 {
    EMOTION_NEUTRAL = 0,
    EMOTION_ANGRY,    // higher, wider pitch, pressed bright voice, faster, clipped pauses, hard stressed syllables
    EMOTION_SCARED,   // high pitch with tremor, breathy, fast, rising phrase ends
    EMOTION_CALM,     // low narrow pitch, relaxed voice, slower with longer pauses
    EMOTION_SAD,      // low flat pitch, breathy lax voice, slow, creaky phrase ends
    EMOTION_HAPPY,    // high lively pitch, "smiling" formants, brisk
    EMOTION_SHOUT,    // raised pitch and jaw opening, pressed loud voice, lengthened stressed syllables
    EMOTION_WHISPER,  // no voicing (noise-excited), slower; output peak ~0.45 instead of 0.8
    EMOTION_DRUNK,    // slurred: slow, wobbly swingy pitch, lax voice, soft consonants, smeared "s"
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

// Voice quality on top of VoiceParams (character colour).
enum Timbre : u8 {
    TIMBRE_NORMAL = 0,
    TIMBRE_NASAL,     // nasal twang (constant velopharyngeal coupling)
    TIMBRE_HUSKY,     // breathy, airy (smoky)
    TIMBRE_GRAVELLY,  // rough, irregular, creaky (gravel voice)
    TIMBRE_BRIGHT,    // forward, tense, "smiling" resonance
    TIMBRE_DARK,      // throaty, lowered larynx, warm
    TIMBRE_FRY,       // vocal fry at phrase ends, low and relaxed
    TIMBRE_AGED,      // elderly: slight tremor, unsteady, breathy, a little slower
    TIMBRE_COUNT
};

struct Style {
    u8 emotion = EMOTION_NEUTRAL;
    u8 delivery = DELIVERY_TALK;
    u8 accent = ACCENT_GENERAL;
    u8 timbre = TIMBRE_NORMAL;
    float intensity = 1.f;       // emotion strength (0..1.5)
    float accentStrength = 1.f;  // accent strength (0..1)
    u32 take = 0;                // performance variant ([take:N]): different intonation details / pacing, 0 = default
    u8 channel = 0;              // Channel the whole line is heard through ([radio], ...; the last channel tag wins)
};

// Markup prefix selecting `s`, e.g. "[accent:latino:0.6][husky][angry]" (empty for the default style).
std::string styleTags(const Style& s);
// `text` without markup: tags, stage directions and *emphasis* stars removed, spaces tidied (for subtitles).
std::string displayText(const char* text);

// ---- Transmission channels ---------------------------------------------------------------------------------------
enum Channel : u8 {
    CHANNEL_DIRECT = 0,  // unprocessed
    CHANNEL_MEGAPHONE,   // bullhorn: narrow horn band, overdriven, street slap echo
    CHANNEL_PA,          // public address in a big hall (airport, station, mall): band-limited, long reverb
    CHANNEL_RADIO,       // two-way / police radio: 350 Hz - 3 kHz, hard compression and clipping, hiss, squelch tail
    CHANNEL_PHONE,       // telephone line: 300 Hz - 3.4 kHz, mild saturation
    CHANNEL_COUNT
};
// Processes existing mono PCM in place (any source, e.g. radio-show callers); appends channelTail() seconds.
void applyChannel(int channel, int sampleRate, u32 seed, std::vector<float>& pcm);
float channelTail(int channel);  // seconds of echo / reverb / squelch appended after the speech

// ---- Crowd walla ---------------------------------------------------------------------------------------------------
// Unintelligible multi-voice murmur (pseudo-English syllables, conversational prosody, occasional laughs), rendered as
// a seamless mono loop for the ambience system: loop it and scale its gain with crowd density; render two or three
// seeds and crossfade for dense or long-lived places. Cost: roughly voices x seconds of speech synthesis
// (8 voices x 12 s is ~0.2 s of CPU at 22 kHz) -- do it on a worker thread or at load time.
struct WallaParams {
    u32 seed = 1;
    int voices = 8;              // simultaneous talkers (1..32)
    float seconds = 12.f;        // loop length (2..120)
    float femaleRatio = 0.5f;
    float excitement = 0.3f;     // 0 hushed lounge murmur .. 1 lively beach / club crowd
    float laughter = 0.08f;      // chance of a laugh per conversational turn
    u8 accent = ACCENT_GENERAL;  // optional local flavour (e.g. ACCENT_LATINO for Calle Luna)
    float accentMix = 0.f;       // fraction of turns spoken with that accent
    float panic = 0.f;           // 0 normal chatter .. 1 a fleeing crowd (shouts, screams, frightened voices)
};
void walla(const WallaParams& p, int sampleRate, std::vector<float>& out);  // appends; RMS ~ -20 dBFS, peak <= 0.5

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

// ---- Facial expression cues ----------------------------------------------------------------------------------------
// Style spans (same timeline): which emotion / delivery is being voiced when, e.g. to drive an angry or smiling face
// in sync with "[angry]" or "[happy]" parts of a line. Consecutive words with the same style form one span.
struct StyleSpan {
    float start, end;  // seconds
    Style style;
};
void styleTimeline(const char* text, const Audio::VoiceParams& voice, std::vector<StyleSpan>& out);

// Pitch accents (stressed, prominent syllables): natural moments for eyebrow raises, head nods and blinks.
struct AccentCue {
    float time;      // seconds (peak of the accented vowel)
    float strength;  // 0..1 (emphasized words and shouting are strongest)
    bool nuclear;    // last accent of its phrase (usually the strongest gesture)
};
void accentCues(const char* text, const Audio::VoiceParams& voice, std::vector<AccentCue>& out);

}  // namespace Speech
