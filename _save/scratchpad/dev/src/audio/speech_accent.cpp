// Accent flavours: post-lexical phonology applied per word (with the word's accent and strength) to the
// syllabified utterance before the duration rules. Vowel qualities are changed through per-segment formant
// targets (Seg::fo), consonants by substitution, insertion or deletion. Rhythm and pitch differences of the
// accents come from their StyleParams (speech_style.cpp).
#include "speech_internal.h"

namespace Speech {
namespace detail {

// Vowel quality (Hz, adult male reference; scaled by formantScale in the synthesizer).
static void setQuality(Seg& s, float f1, float f2, float f3, float e1, float e2, float e3) {
    s.fo[0] = f1, s.fo[1] = f2, s.fo[2] = f3, s.fo[3] = e1, s.fo[4] = e2, s.fo[5] = e3;
}
static void setMono(Seg& s, float f1, float f2, float f3) { setQuality(s, f1, f2, f3, f1, f2, f3); }
// Adds a glide toward (e1, e2) at the end of a vowel, keeping its start quality.
static void setOffglide(Seg& s, float e1, float e2) {
    const PhInfo& p = phInfo(s.ph);
    float f1 = s.fo[0] > 0.f ? s.fo[0] : p.f[0], f2 = s.fo[1] > 0.f ? s.fo[1] : p.f[1], f3 = s.fo[2] > 0.f ? s.fo[2] : p.f[2];
    setQuality(s, f1, f2, f3, e1, e2, f3);
}

static bool inWordList(const std::string& w, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (w == list[i]) return true;
    return false;
}

// BATH words: broad [a:] in British English.
static const char* const kBathWords[] = {
    "bath", "baths", "path", "paths", "class", "classes", "glass", "glasses", "grass", "pass", "passed", "passing",
    "past", "fast", "faster", "last", "ask", "asked", "asking", "after", "afternoon", "can't", "dance", "dancing",
    "answer", "half", "laugh", "laughing", "rather", "master", "castle", "chance", "demand", "example", "plant",
    "aunt", "branch", "disaster", "nasty", "vast", "advantage", "command", "commander", "france", "grant", "chant",
    "sample", "staff", "graph", "craft", "draft", "raft", "shaft", "mask", "task", "blast", "cast", "mast", "vase"};
// PALM words keep [a:] (not rounded to the LOT vowel).
static const char* const kPalmWords[] = {"father", "calm", "palm", "balm", "spa", "ma", "pa", "bra", "drama",
                                         "pasta", "lager", "ah", "bravo", "garage", "massage", "llama", "saga",
                                         "mama", "papa", "karate", "safari", "sonata", "armada", "plaza", "pajamas"};

static bool isVoicelessCons(int ph) { return hasFlag(ph, PF_OBSTRUENT) && !hasFlag(ph, PF_VOICED); }

// Rebuilds word / phrase ranges and position flags after segments were inserted or removed.
static void refreshStructure(Utterance& u) {
    std::vector<Seg>& S = u.segs;
    const int N = (int)S.size();
    for (UWord& w : u.words) w.firstSeg = -1, w.lastSeg = -2;
    for (int i = 0; i < N; i++) {
        int w = S[i].word;
        if (w < 0 || w >= (int)u.words.size()) continue;
        UWord& uw = u.words[w];
        if (uw.firstSeg < 0) uw.firstSeg = i;
        uw.lastSeg = i;
    }
    for (size_t w = 0; w < u.words.size(); w++) {
        UWord& uw = u.words[w];
        if (uw.firstSeg < 0) {  // no segments left: point at the neighbour (keeps ranges well formed)
            uw.firstSeg = uw.lastSeg = w > 0 ? std::max(0, u.words[w - 1].lastSeg) : 0;
            continue;
        }
        for (int i = uw.firstSeg; i <= uw.lastSeg; i++) S[i].flags &= ~(u32)(SF_WORD_START | SF_WORD_END);
        S[uw.firstSeg].flags |= SF_WORD_START;
        S[uw.lastSeg].flags |= SF_WORD_END;
    }
    for (UPhrase& p : u.phrases) p.firstSeg = -1, p.lastSeg = -2;
    for (const UWord& uw : u.words) {
        if (uw.phrase < 0 || uw.phrase >= (int)u.phrases.size() || uw.lastSeg < uw.firstSeg) continue;
        UPhrase& p = u.phrases[uw.phrase];
        if (p.firstSeg < 0 || uw.firstSeg < p.firstSeg) p.firstSeg = uw.firstSeg;
        p.lastSeg = std::max(p.lastSeg, uw.lastSeg);
    }
    for (UPhrase& p : u.phrases)
        if (p.firstSeg < 0) p.firstSeg = p.lastSeg = std::max(0, N - 1);
    for (int i = 0; i < N; i++) {
        S[i].flags &= ~(u32)(SF_UTT_START | SF_PREPAUSE);
        if (S[i].ph == PH_SIL) continue;
        if (i == 0 || S[i - 1].ph == PH_SIL) S[i].flags |= SF_UTT_START;
        if (i + 1 >= N || S[i + 1].ph == PH_SIL) S[i].flags |= SF_PREPAUSE;
    }
}

// Non-rhotic codas: /r/ after a vowel and not before a vowel disappears; the vowel lengthens and (for most vowels)
// glides toward schwa ("here" [hi@], "four" [fo@], "car" [ka:]). Unstressed "-er" becomes schwa, or [a] when
// `erToA` (Caribbean "watah").
static void nonRhotic(std::vector<Seg>& S, int i, std::vector<bool>& del, bool erToA) {
    Seg& s = S[i];
    int N = (int)S.size();
    if (s.ph == PH_AXR) {
        bool linking = i + 1 < N && isVowel(S[i + 1].ph);
        if (!linking) {
            s.ph = PH_AX;
            if (erToA) setMono(s, 700.f, 1350.f, 2500.f);
        }
        return;
    }
    if (s.ph != PH_R || i == 0 || !isVowel(S[i - 1].ph)) return;
    if (!(s.flags & SF_CODA)) return;
    if (i + 1 < N && isVowel(S[i + 1].ph)) return;  // linking r ("far away")
    Seg& v = S[i - 1];
    if (v.ph == PH_ER) {
        // NURSE without r-colour: [3:]
        setMono(v, 500.f, 1400.f, 2400.f);
        v.durMul *= 1.1f;
    } else if (v.ph == PH_AA || v.ph == PH_AH) {
        v.durMul *= 1.3f;
    } else if (v.ph == PH_AXR) {
        v.ph = PH_AX;
    } else {
        setOffglide(v, 560.f, 1400.f);
        v.durMul *= 1.2f;
    }
    del[i] = true;
}

void applyAccents(Utterance& u) {
    std::vector<Seg>& S = u.segs;
    const int N = (int)S.size();
    bool any = false;
    for (const UWord& w : u.words)
        if (w.style.accent != ACCENT_GENERAL && w.style.accentStrength > 0.f) any = true;
    if (!any) return;
    std::vector<bool> del(N, false);
    std::vector<std::vector<Seg>> insBefore(N);

    for (int i = 0; i < N; i++) {
        Seg& s = S[i];
        if (s.word < 0 || s.ph == PH_SIL) continue;
        const UWord& w = u.words[s.word];
        const int acc = w.style.accent;
        const float st = Saturate(w.style.accentStrength);
        if (acc == ACCENT_GENERAL || st <= 0.f) continue;
        const std::string& text = w.text;
        const bool wordStart = (s.flags & SF_WORD_START) != 0 || (i > 0 && S[i - 1].ph == PH_Q && S[i - 1].word == s.word);
        const int nx = i + 1 < N ? (int)S[i + 1].ph : (int)PH_SIL;
        const bool nxSameWord = i + 1 < N && S[i + 1].word == s.word;
        const bool wordFinal = i == w.lastSeg;
        const bool vowel = isVowel(s.ph);

        switch (acc) {
            case ACCENT_SOUTH:
                if (s.ph == PH_AY) {
                    if (nxSameWord && isVoicelessCons(nx)) setQuality(s, 740.f, 1250.f, 2500.f, 580.f, 1600.f, 2500.f);
                    else if (st >= 0.15f) {  // "ah" for I / ride / time
                        setMono(s, 760.f, 1400.f, 2500.f);
                        s.durMul *= 1.1f;
                    }
                } else if (s.ph == PH_EH && nxSameWord && hasFlag(nx, PF_NASAL) && st >= 0.3f) {
                    s.ph = PH_IH;  // pin / pen merger
                } else if (vowel && s.stress == 1 && st >= 0.35f &&
                           (s.ph == PH_AE || s.ph == PH_EH || s.ph == PH_IH)) {
                    // drawl: stressed short vowels lengthen and break toward a glide
                    if (s.ph == PH_AE) setQuality(s, 720.f, 1700.f, 2450.f, 600.f, 1950.f, 2500.f);
                    else if (s.ph == PH_EH) setQuality(s, 600.f, 1800.f, 2550.f, 470.f, 2050.f, 2600.f);
                    else setQuality(s, 420.f, 1960.f, 2600.f, 500.f, 1600.f, 2500.f);
                    s.durMul *= 1.15f;
                } else if ((s.ph == PH_UW || s.ph == PH_UH) && st >= 0.45f) {
                    if (s.ph == PH_UW) setQuality(s, 330.f, 1500.f, 2300.f, 300.f, 1400.f, 2250.f);
                    else setMono(s, 450.f, 1400.f, 2350.f);
                } else if (s.ph == PH_OW && st >= 0.6f) {
                    setQuality(s, 550.f, 1250.f, 2400.f, 450.f, 1000.f, 2350.f);
                } else if (s.ph == PH_NG && wordFinal && i > 0 && (S[i - 1].ph == PH_IH || S[i - 1].ph == PH_IX) &&
                           S[i - 1].stress == 0 && st >= 0.5f) {
                    s.ph = PH_N;  // -in'
                }
                break;

            case ACCENT_NEWYORK:
                if (st >= 0.2f && (s.ph == PH_R || s.ph == PH_AXR)) nonRhotic(S, i, del, false);
                else if (s.ph == PH_AO && st >= 0.3f) {
                    setQuality(s, 480.f, 850.f, 2400.f, 560.f, 1250.f, 2450.f);  // cawfee
                    s.durMul *= 1.1f;
                } else if (s.ph == PH_AE && st >= 0.4f && nxSameWord &&
                           (hasFlag(nx, PF_NASAL) || nx == PH_F || nx == PH_TH || nx == PH_S || nx == PH_SH ||
                            nx == PH_B || nx == PH_D || nx == PH_G)) {
                    setQuality(s, 500.f, 2050.f, 2650.f, 640.f, 1600.f, 2500.f);  // tensed "a": [e@]
                    s.durMul *= 1.1f;
                } else if (s.ph == PH_AW && st >= 0.5f) {
                    setQuality(s, 700.f, 1550.f, 2500.f, 480.f, 1050.f, 2400.f);
                } else if (s.ph == PH_DH && wordStart && w.function && st >= 0.6f) {
                    s.ph = PH_D;  // "da", "dem", "dis"
                }
                break;

            case ACCENT_LATINO:
                if (s.ph == PH_AX) setMono(s, 640.f, 1250.f, 2550.f);  // no reduction to schwa
                else if (s.ph == PH_IX) setMono(s, 320.f, 2200.f, 2900.f);
                else if (s.ph == PH_IH && st >= 0.3f) s.ph = PH_IY;
                else if (s.ph == PH_UH && st >= 0.3f) s.ph = PH_UW;
                else if (s.ph == PH_AE && st >= 0.35f) setMono(s, 760.f, 1400.f, 2500.f);
                else if (s.ph == PH_AH && s.stress > 0 && st >= 0.5f) setMono(s, 740.f, 1300.f, 2500.f);
                else if (s.ph == PH_EY && st >= 0.5f) setMono(s, 450.f, 2000.f, 2650.f);
                else if (s.ph == PH_OW && st >= 0.5f) setMono(s, 480.f, 900.f, 2400.f);
                else if (s.ph == PH_Z && st >= 0.4f) s.ph = PH_S;
                else if (s.ph == PH_ZH && st >= 0.4f) s.ph = PH_SH;
                else if (s.ph == PH_V && st >= 0.4f && (wordStart || (i > 0 && isVowel(S[i - 1].ph)))) s.ph = PH_B;
                else if (s.ph == PH_DH && wordStart && st >= 0.4f) s.ph = PH_D;
                else if (s.ph == PH_TH && st >= 0.6f) s.ph = PH_T;
                else if (s.ph == PH_DX && st >= 0.5f) s.ph = (s.flags & SF_FROM_T) ? PH_T : PH_D;  // no flapping
                else if ((s.ph == PH_T || s.ph == PH_D) && wordFinal && st >= 0.6f && i > w.firstSeg &&
                         !isVowel(S[i - 1].ph) && S[i - 1].ph != PH_R && S[i - 1].ph != PH_Q)
                    del[i] = true;  // "las", "frien"
                else if (s.ph == PH_JH && wordStart && st >= 0.8f) s.ph = PH_Y;
                else if (s.ph == PH_SH && wordStart && st >= 0.8f) s.ph = PH_CH;
                if (S[i].ph == PH_S && wordStart && st >= 0.85f && nxSameWord && !isVowel(nx) && nx != PH_Y) {
                    Seg e = s;  // "estreet": epenthetic vowel before s + consonant
                    e.ph = PH_EH;
                    e.stress = 0;
                    e.flags &= ~(u32)(SF_ONSET | SF_CODA | SF_WORD_END);
                    setMono(e, 500.f, 1850.f, 2600.f);
                    e.durMul = 0.6f;
                    insBefore[i].push_back(e);
                    S[i].flags &= ~(u32)SF_WORD_START;
                }
                break;

            case ACCENT_CARIBBEAN:
                if (s.ph == PH_TH && st >= 0.2f) s.ph = PH_T;
                else if (s.ph == PH_DH && st >= 0.2f) s.ph = PH_D;
                else if ((s.ph == PH_R || s.ph == PH_AXR) && st >= 0.3f) nonRhotic(S, i, del, true);
                else if (s.ph == PH_EY && st >= 0.3f) setMono(s, 450.f, 2000.f, 2650.f);
                else if (s.ph == PH_OW && st >= 0.3f) setMono(s, 480.f, 880.f, 2400.f);
                else if (s.ph == PH_AE && st >= 0.4f) setMono(s, 760.f, 1400.f, 2500.f);
                else if (s.ph == PH_AH && s.stress > 0 && st >= 0.6f) setMono(s, 600.f, 950.f, 2450.f);
                else if (s.ph == PH_AX && st >= 0.5f) setMono(s, 650.f, 1300.f, 2550.f);
                else if (s.ph == PH_ER && st >= 0.3f) {
                    setMono(s, 560.f, 1300.f, 2450.f);  // non-rhotic NURSE
                    s.durMul *= 1.1f;
                }
                else if ((s.ph == PH_T || s.ph == PH_D) && wordFinal && st >= 0.5f && i > w.firstSeg &&
                         !isVowel(S[i - 1].ph) && S[i - 1].ph != PH_R && S[i - 1].ph != PH_Q)
                    del[i] = true;
                else if (s.ph == PH_HH && wordStart && st >= 0.7f && nxSameWord && isVowel(nx))
                    del[i] = true;  // h-dropping
                break;

            case ACCENT_BRITISH:
                if (s.ph == PH_AA && st >= 0.4f && !(nxSameWord && nx == PH_R) &&
                    !inWordList(text, kPalmWords, ARRAY_COUNT(kPalmWords)))
                    setMono(s, 620.f, 950.f, 2450.f);  // LOT: rounded [Q]
                else if (s.ph == PH_AE && st >= 0.4f && inWordList(text, kBathWords, ARRAY_COUNT(kBathWords))) {
                    setMono(s, 720.f, 1150.f, 2450.f);  // BATH: [a:]
                    s.durMul *= 1.2f;
                } else if (s.ph == PH_OW && st >= 0.5f) {
                    setQuality(s, 560.f, 1350.f, 2450.f, 450.f, 1050.f, 2350.f);  // GOAT: [@U]
                } else if (s.ph == PH_ER && st >= 0.3f) {
                    setMono(s, 500.f, 1400.f, 2450.f);  // non-rhotic NURSE [3:]
                    s.durMul *= 1.1f;
                } else if (s.ph == PH_DX && st >= 0.3f) {
                    s.ph = (s.flags & SF_FROM_T) ? PH_T : PH_D;  // no flapping: "better" [bet@]
                } else if (s.ph == PH_UW && st >= 0.5f && i > 0 && S[i - 1].word == s.word &&
                           (S[i - 1].ph == PH_T || S[i - 1].ph == PH_D || S[i - 1].ph == PH_N) &&
                           (S[i - 1].flags & SF_ONSET) && text.find("oo") == std::string::npos &&
                           (text.find('u') != std::string::npos || text.find("ew") != std::string::npos)) {
                    Seg y = S[i - 1];  // yod: "tune" [tju:n], "news" [nju:z]
                    y.ph = PH_Y;
                    y.flags &= ~(u32)(SF_WORD_START | SF_WORD_END);
                    insBefore[i].push_back(y);
                }
                if ((S[i].ph == PH_R || S[i].ph == PH_AXR) && st >= 0.2f) nonRhotic(S, i, del, false);
                break;

            default: break;
        }
    }

    bool edited = false;
    for (int i = 0; i < N && !edited; i++) edited = del[i] || !insBefore[i].empty();
    if (!edited) return;
    std::vector<Seg> out;
    out.reserve((size_t)N + 8);
    for (int i = 0; i < N; i++) {
        for (const Seg& x : insBefore[i]) out.push_back(x);
        if (del[i]) {
            // a deleted coda consonant passes its phrase-final / pre-pause role to what precedes it
            if (!out.empty() && (S[i].flags & SF_PHRASE_FINAL) && out.back().syl == S[i].syl)
                out.back().flags |= SF_PHRASE_FINAL;
            continue;
        }
        out.push_back(S[i]);
    }
    u.segs.swap(out);
    refreshStructure(u);
}

}  // namespace detail
}  // namespace Speech
