// Lip-sync timing: phoneme and viseme keys on exactly the timeline that synthesize() renders (both run the same
// text / prosody front end; no audio is generated here).
#include "speech_internal.h"

namespace Speech {
namespace detail {

static int visemeOfPhoneme(int ph) {
    switch (ph) {
        case PH_P: case PH_B: case PH_M: return VISEME_PP;
        case PH_F: case PH_V: return VISEME_FF;
        case PH_TH: case PH_DH: return VISEME_TH;
        case PH_T: case PH_D: case PH_DX: return VISEME_DD;
        case PH_K: case PH_G: case PH_NG: case PH_HH: return VISEME_KK;
        case PH_CH: case PH_JH: case PH_SH: case PH_ZH: return VISEME_CH;
        case PH_S: case PH_Z: return VISEME_SS;
        case PH_N: case PH_L: case PH_LX: return VISEME_NN;
        case PH_R: case PH_ER: case PH_AXR: return VISEME_RR;
        case PH_AA: case PH_AE: case PH_AH: case PH_AX: case PH_AY: case PH_AW: return VISEME_AA;
        case PH_EH: case PH_EY: return VISEME_E;
        case PH_IH: case PH_IY: case PH_IX: case PH_Y: return VISEME_I;
        case PH_AO: case PH_OW: case PH_OY: return VISEME_O;
        case PH_UW: case PH_UH: case PH_W: return VISEME_U;
        default: return VISEME_SIL;
    }
}

// Viseme of a vowel quality (accent-modified vowels).
static int visemeOfQuality(float f1, float f2) {
    if (f2 < 1100.f) return f1 < 420.f ? VISEME_U : VISEME_O;
    if (f1 >= 650.f) return VISEME_AA;
    if (f2 >= 1800.f) return f1 < 470.f ? VISEME_I : VISEME_E;
    return f1 >= 560.f ? VISEME_AA : VISEME_E;
}

// Final viseme of a diphthong (VISEME_SIL = none).
static int visemeGlideEnd(int ph) {
    switch (ph) {
        case PH_AY: case PH_OY: case PH_EY: return VISEME_I;
        case PH_AW: case PH_OW: return VISEME_U;
        default: return VISEME_SIL;
    }
}

static float consonantWeight(int vis) {
    switch (vis) {
        case VISEME_PP: return 1.f;
        case VISEME_FF: return 0.9f;
        case VISEME_TH: return 0.8f;
        case VISEME_CH: return 0.85f;
        case VISEME_SS: return 0.8f;
        case VISEME_RR: return 0.7f;
        case VISEME_U: return 0.8f;
        case VISEME_I: return 0.6f;
        case VISEME_DD: return 0.6f;
        default: return 0.55f;
    }
}

struct VisemeSeg {
    float t0, dur;
    int vis, visEnd;  // visEnd != VISEME_SIL: glide to it after 55 % of the segment
    float weight;
};

static void segmentVisemes(const Utterance& u, std::vector<VisemeSeg>& out) {
    const std::vector<Seg>& S = u.segs;
    const int M = (int)S.size();
    out.clear();
    for (int i = 0; i < M; i++) {
        const Seg& s = S[i];
        const StyleParams& P = u.segStyle(i);
        VisemeSeg v;
        v.t0 = s.t0;
        v.dur = s.dur;
        v.vis = visemeOfPhoneme(s.ph);
        v.visEnd = VISEME_SIL;
        v.weight = 0.f;
        if (s.ph == PH_SIL) {
            v.vis = VISEME_SIL;
        } else if (isVowel(s.ph)) {
            const PhInfo& p = phInfo(s.ph);
            float f1 = s.fo[0] > 0.f ? s.fo[0] : p.f[0];
            if (s.fo[0] > 0.f) {
                v.vis = visemeOfQuality(s.fo[0], s.fo[1]);
                int e = visemeOfQuality(s.fo[3], s.fo[4]);
                if (e != v.vis) v.visEnd = e;
                if (s.ph == PH_ER && s.fo[2] <= 0.f) v.vis = VISEME_RR;
            } else {
                v.visEnd = visemeGlideEnd(s.ph);
            }
            float w = Clamp((f1 * P.f1 - 250.f) / 500.f, 0.2f, 1.f);
            if (s.stress == 0) w *= 0.75f;
            if (s.flags & (SF_EMPH | SF_SHOUT)) w = std::min(1.f, w * 1.15f);
            if (P.whisper > 0.f) w *= 1.f - 0.15f * P.whisper;
            v.weight = w;
        } else if ((s.ph == PH_HH || s.ph == PH_Q) && i + 1 < M && isVowel(S[i + 1].ph)) {
            // the mouth already takes the shape of the following vowel
            v.vis = visemeOfPhoneme(S[i + 1].ph);
            if (S[i + 1].fo[0] > 0.f) v.vis = visemeOfQuality(S[i + 1].fo[0], S[i + 1].fo[1]);
            v.weight = s.ph == PH_HH ? 0.5f : 0.3f;
        } else if (s.ph == PH_Q) {
            v.vis = VISEME_SIL;
        } else {
            v.weight = consonantWeight(v.vis);
        }
        out.push_back(v);
    }
}

}  // namespace detail

const char* visemeName(int v) {
    static const char* const kNames[VISEME_COUNT] = {"sil", "PP", "FF", "TH", "DD", "kk", "CH", "SS",
                                                     "nn",  "RR", "aa", "E",  "I",  "O",  "U"};
    return v >= 0 && v < VISEME_COUNT ? kNames[v] : "sil";
}

void lipSync(const char* text, const Audio::VoiceParams& voice, std::vector<VisemeKey>& out) {
    out.clear();
    if (!text || !*text) return;
    detail::Utterance utt;
    detail::buildUtterance(text, voice, utt);
    if (utt.segs.empty() || utt.total <= 0.f) return;
    std::vector<detail::VisemeSeg> vs;
    detail::segmentVisemes(utt, vs);
    auto emit = [&](float t, int vis, float w) {
        if (!out.empty()) {
            VisemeKey& b = out.back();
            if (b.viseme == vis) {  // merge repeats
                b.weight = std::max(b.weight, w);
                return;
            }
            if (t <= b.time + 1e-4f) {  // zero-length key: replace
                b.viseme = (u8)vis;
                b.weight = w;
                return;
            }
        }
        VisemeKey k;
        k.time = t;
        k.duration = 0.f;
        k.viseme = (u8)vis;
        k.weight = w;
        out.push_back(k);
    };
    for (const detail::VisemeSeg& v : vs) {
        emit(v.t0, v.vis, v.weight);
        if (v.visEnd != VISEME_SIL) emit(v.t0 + 0.55f * v.dur, v.visEnd, v.weight * 0.8f);
    }
    emit(utt.total, VISEME_SIL, 0.f);
    for (size_t i = 0; i + 1 < out.size(); i++) out[i].duration = out[i + 1].time - out[i].time;
    // synthesize() output ends 20 ms after the last segment (plus the channel's echo / squelch tail)
    out.back().duration = utt.total + 0.02f + detail::channelTailSec(detail::utteranceChannel(utt)) - out.back().time;
}

void styleTimeline(const char* text, const Audio::VoiceParams& voice, std::vector<StyleSpan>& out) {
    out.clear();
    if (!text || !*text) return;
    detail::Utterance utt;
    detail::buildUtterance(text, voice, utt);
    if (utt.segs.empty() || utt.total <= 0.f) return;
    for (const detail::UWord& w : utt.words) {
        if (w.lastSeg < w.firstSeg || w.firstSeg < 0 || w.lastSeg >= (int)utt.segs.size()) continue;
        float a = utt.segs[(size_t)w.firstSeg].t0;
        float b = utt.segs[(size_t)w.lastSeg].t0 + utt.segs[(size_t)w.lastSeg].dur;
        if (!out.empty() && detail::styleEqual(out.back().style, w.style)) {
            out.back().end = b;  // extend (pauses inside a span belong to it)
            continue;
        }
        StyleSpan sp;
        sp.start = out.empty() ? 0.f : a;
        if (!out.empty()) out.back().end = a;
        sp.end = b;
        sp.style = w.style;
        out.push_back(sp);
    }
    if (!out.empty()) out.back().end = utt.total + 0.02f + detail::channelTailSec(detail::utteranceChannel(utt));
}

void accentCues(const char* text, const Audio::VoiceParams& voice, std::vector<AccentCue>& out) {
    out.clear();
    if (!text || !*text) return;
    detail::Utterance utt;
    detail::buildUtterance(text, voice, utt);
    const std::vector<detail::Seg>& S = utt.segs;
    for (const detail::UPhrase& ph : utt.phrases) {
        int last = -1;
        for (int i = ph.firstSeg; i <= ph.lastSeg && i < (int)S.size(); i++)
            if (S[(size_t)i].flags & detail::SF_ACCENT) last = i;
        for (int i = ph.firstSeg; i <= ph.lastSeg && i < (int)S.size(); i++) {
            const detail::Seg& s = S[(size_t)i];
            if (!(s.flags & detail::SF_ACCENT)) continue;
            AccentCue c;
            c.time = s.t0 + s.vot + 0.45f * std::max(0.f, s.dur - s.vot);
            float k = s.accent;
            if (s.flags & (detail::SF_EMPH | detail::SF_SHOUT)) k *= 1.3f;
            c.strength = Saturate(0.6f * k);
            c.nuclear = i == last;
            out.push_back(c);
        }
    }
}

void phonemeTiming(const char* text, const Audio::VoiceParams& voice, std::vector<PhonemeTiming>& out) {
    out.clear();
    if (!text || !*text) return;
    detail::Utterance utt;
    detail::buildUtterance(text, voice, utt);
    if (utt.segs.empty() || utt.total <= 0.f) return;
    std::vector<detail::VisemeSeg> vs;
    detail::segmentVisemes(utt, vs);
    for (size_t i = 0; i < utt.segs.size(); i++) {
        const detail::Seg& s = utt.segs[i];
        PhonemeTiming p;
        p.start = s.t0;
        p.duration = s.dur;
        memset(p.name, 0, sizeof(p.name));
        const char* nm = detail::phInfo(s.ph).name;
        for (int k = 0; k < 3 && nm[k]; k++) p.name[k] = nm[k];
        p.stress = detail::isVowel(s.ph) ? s.stress : 0;
        p.viseme = (u8)vs[i].vis;
        p.word = (i16)std::max(-1, std::min(32767, s.word));
        out.push_back(p);
    }
}

}  // namespace Speech
