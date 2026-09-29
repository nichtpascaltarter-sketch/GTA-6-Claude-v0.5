// Private declarations shared by the speech_*.cpp implementation files.
// All of these files are #included by speech.cpp (unity build); nothing here is public API.
#pragma once
#include "speech.h"
#include "../core/rng.h"

namespace Speech {
namespace detail {

// ---------------------------------------------------------------------------------------------
// Phoneme inventory (ARPAbet based, plus a few allophones used after post-lexical rules).
enum Ph : u8 {
    PH_SIL = 0,
    // vowels
    PH_IY, PH_IH, PH_EY, PH_EH, PH_AE, PH_AA, PH_AO, PH_OW, PH_UH, PH_UW, PH_AH, PH_AX, PH_IX, PH_ER, PH_AXR,
    PH_AY, PH_AW, PH_OY,
    // stops, flap, glottal stop, affricates
    PH_P, PH_B, PH_T, PH_D, PH_K, PH_G, PH_DX, PH_Q, PH_CH, PH_JH,
    // fricatives
    PH_F, PH_V, PH_TH, PH_DH, PH_S, PH_Z, PH_SH, PH_ZH, PH_HH,
    // nasals, liquids (LX = dark / coda l), glides
    PH_M, PH_N, PH_NG, PH_L, PH_LX, PH_R, PH_W, PH_Y,
    PH_COUNT
};

enum PhFlag : u32 {
    PF_VOWEL = 1u << 0,
    PF_DIPH = 1u << 1,
    PF_CONS = 1u << 2,
    PF_VOICED = 1u << 3,
    PF_STOP = 1u << 4,
    PF_FRIC = 1u << 5,
    PF_AFFR = 1u << 6,
    PF_NASAL = 1u << 7,
    PF_LIQUID = 1u << 8,
    PF_GLIDE = 1u << 9,
    PF_SONOR = 1u << 10,   // vowels, nasals, liquids, glides
    PF_LABIAL = 1u << 11,
    PF_DENTAL = 1u << 12,
    PF_ALVEOLAR = 1u << 13,
    PF_POSTALV = 1u << 14,
    PF_VELAR = 1u << 15,
    PF_GLOTTAL = 1u << 16,
    PF_FRONT = 1u << 17,
    PF_ROUND = 1u << 18,
    PF_HIGH = 1u << 19,
    PF_LOW = 1u << 20,
    PF_RHOTIC = 1u << 21,
    PF_REDUCED = 1u << 22,
    PF_TENSE = 1u << 23,
    PF_SIL = 1u << 24,
    PF_FLAP = 1u << 25,
    PF_SIBILANT = 1u << 26,
    PF_OBSTRUENT = 1u << 27  // stops, fricatives, affricates
};

// Articulatory / acoustic description of one phoneme (adult male reference, formantScale = 1).
struct PhInfo {
    const char* name;
    u32 flags;
    float inh, mn;          // Klatt inherent and minimum durations (ms)
    float f[3];             // F1..F3 target (start target for diphthongs)
    float fe[3];            // F1..F3 end target (== f for monophthongs)
    float bw[3];            // B1..B3
    float av;               // voicing amplitude, dB (0 = voiceless). Vowel reference = 60
    float af;               // frication amplitude, dB (0 = none)
    float ah;               // aspiration amplitude, dB (0 = none)
    float fricF[3], fricB[3], fricA[3];  // frication spectrum: 3 peak resonators (Hz, Hz, dB rel.)
    float fricByp;          // flat (bypass) frication component, dB rel. (-99 = none)
    int rank;               // coarticulation dominance (Holmes style)
    float fix[3], prop[3];  // boundary value = fix + prop * neighbour target (per formant)
    float tInt;             // transition duration inside this segment (ms)
    float tExt, tExtF1;     // transition duration into the neighbour (ms), F2/F3 and F1
    float burstMs;          // plosive burst duration
    float burstAf;          // plosive burst amplitude (dB)
    float vot;              // aspiration after release in a stressed onset (ms)
    float nasalZero;        // place-specific anti-resonance of a nasal murmur (Hz), 0 = none
};

const PhInfo& phInfo(int ph);
int phFromName(const char* s, int len);  // "AA" -> PH_AA ; -1 if unknown

FORCEINLINE bool isVowel(int ph) { return (phInfo(ph).flags & PF_VOWEL) != 0; }
FORCEINLINE bool hasFlag(int ph, u32 f) { return (phInfo(ph).flags & f) != 0; }

// One phoneme of a pronunciation with lexical stress (0 unstressed, 1 primary, 2 secondary).
struct PhS {
    u8 ph;
    u8 stress;
};
typedef std::vector<PhS> Pron;

// Parses an ARPAbet string like "P AH0 L IY1 S". Returns false on unknown symbols.
bool parsePhonemes(const char* s, Pron& out);

// ---------------------------------------------------------------------------------------------
// Text normalization output.
enum BreakType : u8 {
    BRK_NONE = 0,
    BRK_MINOR,      // inserted phrase break inside long clauses (no punctuation)
    BRK_COMMA,
    BRK_CLAUSE,     // ; :
    BRK_DASH,
    BRK_ELLIPSIS,
    BRK_PERIOD,
    BRK_QUESTION,
    BRK_EXCLAIM
};

struct TextWord {
    std::string w;        // lowercase word (letters and apostrophes), or explicit phonemes if phon
    u8 emph = 0;          // 1 = emphasized (ALL CAPS in mixed text, *stars*)
    bool spell = false;   // spell as letters
    bool phon = false;    // w holds explicit ARPAbet phonemes
    bool shout = false;   // whole sentence in capitals
    bool gDrop = false;   // "nothin'" style -in' ending
    u8 brk = BRK_NONE;    // break after this word
};

void normalizeText(const char* text, std::vector<TextWord>& out);

// ---------------------------------------------------------------------------------------------
// Lexicon (exception dictionary + morphology + letter-to-sound rules).
struct WordPron {
    Pron ph;
    bool function = false;  // function word: never pitch-accented
};

const char* dictLookup(const std::string& w);  // raw dictionary entry or nullptr
void lookupWord(const TextWord& tw, WordPron& out);
void letterToSound(const std::string& word, Pron& out);  // rules + stress assignment
void spellWord(const std::string& word, Pron& out);
bool isFunctionWord(const std::string& w);

// ---------------------------------------------------------------------------------------------
// Utterance representation after phonetic processing.
enum SegFlag : u32 {
    SF_WORD_START = 1u << 0,
    SF_WORD_END = 1u << 1,
    SF_ONSET = 1u << 2,       // consonant in syllable onset
    SF_CODA = 1u << 3,        // consonant in syllable coda
    SF_STRESSED = 1u << 4,    // belongs to a syllable with primary or secondary stress
    SF_PRIMARY = 1u << 5,     // belongs to a primary-stressed syllable
    SF_PHRASE_FINAL = 1u << 6,// belongs to the last syllable before a phrase break
    SF_FUNCTION = 1u << 7,    // belongs to a function word
    SF_EMPH = 1u << 8,        // emphasized word
    SF_ASPIRATED = 1u << 9,   // voiceless stop released with aspiration into next segment
    SF_UNRELEASED = 1u << 10, // stop with weak/absent release burst
    SF_WORD_FINAL_SYL = 1u << 11,
    SF_POLYSYL = 1u << 12,    // word has more than one syllable
    SF_SYLLABIC = 1u << 13,   // schwa reduced to a syllabic consonant (very short)
    SF_ACCENT = 1u << 14,     // nucleus of a pitch-accented syllable
    SF_SHOUT = 1u << 15,
    SF_UTT_START = 1u << 16,  // first segment after a pause
    SF_PREPAUSE = 1u << 17    // last segment before a pause
};

struct Seg {
    u8 ph = PH_SIL;
    u8 stress = 0;         // vowels: lexical stress; consonants: stress of their syllable
    u32 flags = 0;
    int word = -1;         // index into Utterance::words (-1 for pauses)
    int syl = -1;          // global syllable index
    float dur = 0.f;       // seconds
    float t0 = 0.f;        // start time (seconds)
    float vot = 0.f;       // voiceless (aspirated) portion at segment start, seconds
    float accent = 0.f;    // pitch accent size (semitones) if SF_ACCENT
};

struct UWord {
    int firstSeg = 0, lastSeg = -1;
    int phrase = 0;
    bool function = false;
    u8 emph = 0;
    int nSyl = 0;
};

struct UPhrase {
    int firstSeg = 0, lastSeg = -1;  // speech segments of the phrase (pause excluded)
    u8 brk = BRK_PERIOD;             // break type ending the phrase
    u8 sentType = BRK_PERIOD;        // type of the sentence the phrase belongs to
    bool whQuestion = false;
    bool sentenceStart = true;       // first phrase of a sentence (full pitch reset)
    bool shout = false;
};

struct Utterance {
    std::vector<Seg> segs;
    std::vector<UWord> words;
    std::vector<UPhrase> phrases;
    float total = 0.f;  // seconds
};

// Text -> segments with durations and timing (no F0). Deterministic and cheap.
void buildUtterance(const char* text, const Audio::VoiceParams& voice, Utterance& utt);

// F0 anchor points (seconds, semitones relative to the voice's base pitch).
struct F0Point {
    float t, st;
};
void buildF0(const Utterance& utt, const Audio::VoiceParams& voice, std::vector<F0Point>& out);

// Renders the utterance (appends samples to out).
void render(const Utterance& utt, const std::vector<F0Point>& f0, const Audio::VoiceParams& voice, int sampleRate,
            u32 seed, std::vector<float>& out);

// Debug helper for tests: phonemes of a text as ARPAbet with stress digits and word separators.
std::string debugPhonemes(const char* text);

}  // namespace detail
}  // namespace Speech
