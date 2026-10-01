// Transmission channels (police megaphone, public-address hall, two-way radio, telephone) applied to synthesized
// speech, and the crowd "walla" generator (unintelligible multi-voice murmur beds for the ambience system).
#include "speech_internal.h"

namespace Speech {
namespace detail {

// RBJ-cookbook biquad (direct form II transposed).
struct ChBiquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(float nb0, float nb1, float nb2, float a0, float na1, float na2) {
        b0 = nb0 / a0, b1 = nb1 / a0, b2 = nb2 / a0, a1 = na1 / a0, a2 = na2 / a0;
    }
    void highpass(float f, float q, float sr) {
        float w = kTwoPi * Clamp(f, 10.f, 0.45f * sr) / sr, cw = cosf(w), al = sinf(w) / (2.f * q);
        set((1.f + cw) * 0.5f, -(1.f + cw), (1.f + cw) * 0.5f, 1.f + al, -2.f * cw, 1.f - al);
    }
    void lowpass(float f, float q, float sr) {
        float w = kTwoPi * Clamp(f, 10.f, 0.45f * sr) / sr, cw = cosf(w), al = sinf(w) / (2.f * q);
        set((1.f - cw) * 0.5f, 1.f - cw, (1.f - cw) * 0.5f, 1.f + al, -2.f * cw, 1.f - al);
    }
    void peak(float f, float q, float gainDb, float sr) {
        float A = powf(10.f, gainDb / 40.f), w = kTwoPi * Clamp(f, 10.f, 0.45f * sr) / sr, cw = cosf(w);
        float al = sinf(w) / (2.f * q);
        set(1.f + al * A, -2.f * cw, 1.f - al * A, 1.f + al / A, -2.f * cw, 1.f - al / A);
    }
    FORCEINLINE float tick(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Small Schroeder reverb (4 parallel combs + 2 allpasses) for the public-address hall.
struct HallReverb {
    std::vector<float> comb[4], ap[2];
    size_t ci[4] = {0, 0, 0, 0}, ai[2] = {0, 0};
    float cfb[4] = {0, 0, 0, 0}, damp = 0.25f, lp[4] = {0, 0, 0, 0};
    void init(float sr, float rt60) {
        static const float kComb[4] = {0.0297f, 0.0371f, 0.0411f, 0.0437f}, kAp[2] = {0.005f, 0.0017f};
        for (int k = 0; k < 4; k++) {
            comb[k].assign((size_t)std::max(1.f, kComb[k] * sr), 0.f);
            cfb[k] = powf(10.f, -3.f * kComb[k] / rt60);
        }
        for (int k = 0; k < 2; k++) ap[k].assign((size_t)std::max(1.f, kAp[k] * sr), 0.f);
    }
    float tick(float x) {
        float y = 0.f;
        for (int k = 0; k < 4; k++) {
            float o = comb[k][ci[k]];
            lp[k] = o + damp * (lp[k] - o);
            comb[k][ci[k]] = x + cfb[k] * lp[k];
            if (++ci[k] >= comb[k].size()) ci[k] = 0;
            y += o;
        }
        y *= 0.25f;
        for (int k = 0; k < 2; k++) {
            float d = ap[k][ai[k]];
            float v = y + 0.5f * d;
            ap[k][ai[k]] = v;
            if (++ai[k] >= ap[k].size()) ai[k] = 0;
            y = d - 0.5f * v;
        }
        return y;
    }
};

int utteranceChannel(const Utterance& u) {
    int c = CHANNEL_DIRECT;
    for (const UWord& w : u.words)
        if (w.style.channel != CHANNEL_DIRECT && w.style.channel < CHANNEL_COUNT) c = w.style.channel;
    return c;
}

static float channelTailSec(int channel) {
    switch (channel) {
        case CHANNEL_MEGAPHONE: return 0.25f;  // slap echo off buildings
        case CHANNEL_PA: return 1.3f;          // hall reverb
        case CHANNEL_RADIO: return 0.18f;      // squelch tail
        default: return 0.f;
    }
}

static void normalizePeak(std::vector<float>& x, size_t from, float target) {
    float pk = 0.f;
    for (size_t i = from; i < x.size(); i++) pk = std::max(pk, fabsf(x[i]));
    if (pk < 1e-9f) return;
    float g = target / pk;
    for (size_t i = from; i < x.size(); i++) x[i] *= g;
}

// Applies a channel to samples [from, end) of `pcm` (appending the channel's tail).
static void applyChannelRange(int channel, int sampleRate, u32 seed, std::vector<float>& pcm, size_t from) {
    if (channel <= CHANNEL_DIRECT || channel >= CHANNEL_COUNT || from >= pcm.size()) return;
    const float sr = (float)Clamp(sampleRate, 8000, 96000);
    const size_t tail = (size_t)(channelTailSec(channel) * sr);
    pcm.resize(pcm.size() + tail, 0.f);
    float* x = pcm.data() + from;
    const size_t n = pcm.size() - from;
    Rng rng(hashCombine(seed, 0xC4A11E1u), 11);
    switch (channel) {
        case CHANNEL_MEGAPHONE: {
            // horn: narrow band, strong resonance, overdriven driver, metallic ring, echo from the street
            ChBiquad hp1, hp2, lp1, lp2, horn, ring;
            hp1.highpass(550.f, 0.8f, sr), hp2.highpass(550.f, 0.8f, sr);
            lp1.lowpass(3800.f, 0.8f, sr), lp2.lowpass(3800.f, 0.8f, sr);
            horn.peak(1700.f, 2.2f, 9.f, sr);
            ring.peak(2900.f, 6.f, 5.f, sr);
            size_t echo = (size_t)(0.14f * sr);
            std::vector<float> dry(n);
            for (size_t i = 0; i < n; i++) {
                float v = hp2.tick(hp1.tick(x[i] * 2.5f));
                v = horn.tick(v);
                v = tanhf(3.2f * v) * 0.45f;
                v = ring.tick(lp2.tick(lp1.tick(v)));
                dry[i] = v;
            }
            for (size_t i = 0; i < n; i++) x[i] = dry[i] + (i >= echo ? 0.22f * dry[i - echo] : 0.f);
            break;
        }
        case CHANNEL_PA: {
            // ceiling loudspeakers in a big hall: band-limited, gentle compression, long reverb
            ChBiquad hp, lp, pres;
            hp.highpass(220.f, 0.7f, sr);
            lp.lowpass(5500.f, 0.7f, sr);
            pres.peak(2500.f, 1.f, 4.f, sr);
            HallReverb rv;
            rv.init(sr, 1.8f);
            float env = 0.f;
            const float att = expf(-1.f / (0.005f * sr)), rel = expf(-1.f / (0.12f * sr));
            for (size_t i = 0; i < n; i++) {
                float v = pres.tick(lp.tick(hp.tick(x[i])));
                float a = fabsf(v);
                env = a > env ? a + att * (env - a) : a + rel * (env - a);
                v *= 1.f / (0.35f + 1.3f * env);  // compression
                float w = rv.tick(v);
                x[i] = 0.72f * v + 0.55f * w;
            }
            break;
        }
        case CHANNEL_RADIO: {
            // two-way radio: steep 350 Hz - 3 kHz band, hard compression and clipping, hiss, squelch burst at the end
            ChBiquad hp1, hp2, lp1, lp2, mid;
            hp1.highpass(380.f, 0.9f, sr), hp2.highpass(380.f, 0.9f, sr);
            lp1.lowpass(2900.f, 0.9f, sr), lp2.lowpass(2900.f, 0.9f, sr);
            mid.peak(1400.f, 1.2f, 5.f, sr);
            float env = 0.f;
            const float att = expf(-1.f / (0.002f * sr)), rel = expf(-1.f / (0.05f * sr));
            const size_t speechEnd = n - tail;
            u32 st = hashCombine(seed, 0x51A7u);
            for (size_t i = 0; i < n; i++) {
                st = st * 1664525u + 1013904223u;
                float noise = ((float)(st >> 8) * (1.f / 8388608.f) - 1.f);
                float v = x[i];
                float a = fabsf(v);
                env = a > env ? a + att * (env - a) : a + rel * (env - a);
                v *= 1.f / (0.08f + 1.4f * env);
                v = tanhf(2.5f * v);
                float hiss = 0.018f * noise;
                if (i >= speechEnd) {  // squelch tail: loud noise burst that cuts off
                    float k = (float)(i - speechEnd) / (float)std::max<size_t>(1, tail);
                    hiss = noise * 0.55f * (k < 0.8f ? 1.f : (1.f - k) * 5.f);
                }
                if (i < (size_t)(0.004f * sr)) hiss += 0.6f * noise;  // key-up click
                v = mid.tick(lp2.tick(lp1.tick(hp2.tick(hp1.tick(v + hiss)))));
                x[i] = v;
            }
            (void)rng;
            break;
        }
        case CHANNEL_PHONE: {
            ChBiquad hp1, hp2, lp1, lp2;
            hp1.highpass(320.f, 0.8f, sr), hp2.highpass(320.f, 0.8f, sr);
            lp1.lowpass(3300.f, 0.8f, sr), lp2.lowpass(3300.f, 0.8f, sr);
            for (size_t i = 0; i < n; i++) {
                float v = lp2.tick(lp1.tick(hp2.tick(hp1.tick(x[i]))));
                x[i] = tanhf(1.8f * v) / 1.8f;
            }
            break;
        }
        default: break;
    }
    // de-click the ends and restore the output level
    size_t fin = std::min(n, (size_t)(0.003f * sr)), fout = std::min(n, (size_t)(0.01f * sr));
    for (size_t i = 0; i < fin; i++) x[i] *= (float)i / (float)fin;
    for (size_t i = 0; i < fout; i++) x[n - 1 - i] *= (float)i / (float)fout;
    normalizePeak(pcm, from, 0.8f);
}

// Pseudo-English syllables for unintelligible crowd chatter.
static std::string wallaWord(Rng& r) {
    static const char* const kOnset[] = {"B", "D", "G", "K", "P", "T", "M", "N", "L", "R", "S", "SH", "F", "V", "W",
                                         "Y", "HH", "CH", "JH", "DH", "TH", "Z", "B R", "K L", "S T", "G R", "F L"};
    static const char* const kNucleus[] = {"AE", "EH", "IH", "AA", "AH", "OW", "EY", "AY", "IY", "UW", "ER", "AO"};
    static const char* const kCoda[] = {"", "", "", "N", "T", "D", "S", "Z", "K", "M", "L", "R", "N T", "S T"};
    int syl = r.irange(1, 3);
    int stressAt = r.irange(0, syl - 1);
    std::string w;
    for (int k = 0; k < syl; k++) {
        if (!w.empty()) w += ' ';
        w += kOnset[r.irange(0, (int)ARRAY_COUNT(kOnset) - 1)];
        w += ' ';
        w += kNucleus[r.irange(0, (int)ARRAY_COUNT(kNucleus) - 1)];
        w += k == stressAt ? '1' : '0';
        const char* c = kCoda[r.irange(0, (int)ARRAY_COUNT(kCoda) - 1)];
        if (*c) {
            w += ' ';
            w += c;
        }
    }
    return w;
}

}  // namespace detail

float channelTail(int channel) { return detail::channelTailSec(channel); }

void applyChannel(int channel, int sampleRate, u32 seed, std::vector<float>& pcm) {
    detail::applyChannelRange(channel, sampleRate, seed, pcm, 0);
}

void walla(const WallaParams& wp, int sampleRate, std::vector<float>& out) {
    using namespace detail;
    const int sr = Clamp(sampleRate, 8000, 96000);
    const float seconds = Clamp(wp.seconds, 2.f, 120.f);
    const float xfade = std::min(1.f, 0.25f * seconds);
    const size_t len = (size_t)(seconds * (float)sr), total = (size_t)((seconds + xfade) * (float)sr);
    const int voices = Clamp(wp.voices, 1, 32);
    const float excite = Saturate(wp.excitement);
    std::vector<float> mix(total, 0.f);
    Rng r(hash32(wp.seed * 2654435761u + 0x3a11au), 17);
    for (int v = 0; v < voices; v++) {
        bool female = r.f() < wp.femaleRatio;
        Audio::VoiceParams vp = presetVoice(female, hash32(wp.seed * 131u + (u32)v * 7919u));
        vp.expressiveness *= 0.8f + 0.6f * excite;
        vp.speed *= 0.95f + 0.15f * excite;
        std::vector<float> stream;
        stream.reserve(total);
        // leading offset so voices do not start together
        stream.resize((size_t)(r.range(0.f, 1.5f) * (float)sr), 0.f);
        while (stream.size() < total) {
            // one conversational turn: 1-3 short pseudo-sentences (panic: short frightened shouts and screams)
            std::string text;
            const bool panicking = r.f() < wp.panic;
            if (panicking) text += r.f() < 0.5f ? "[scared:1.4]" : "[shout:1.2]";
            else if (r.f() < 0.3f + 0.4f * excite) text += excite > 0.6f ? "[happy]" : "[happy:0.5]";
            if (wp.accent != ACCENT_GENERAL && r.f() < wp.accentMix)
                text += "[accent:" + std::string(wp.accent == ACCENT_SOUTH       ? "south"
                                                 : wp.accent == ACCENT_NEWYORK   ? "newyork"
                                                 : wp.accent == ACCENT_LATINO    ? "latino"
                                                 : wp.accent == ACCENT_CARIBBEAN ? "caribbean"
                                                                                 : "british") +
                        ":0.7]";
            if (!panicking && r.f() < wp.laughter) text += "[laughs] ";
            if (panicking && r.f() < 0.35f) {  // a scream
                static const char* const kScream[] = {"{AA1 AA0}", "{AY1 IY0}", "{EH1 AY0}", "{OW1 OW0}"};
                text += std::string(kScream[r.irange(0, 3)]) + "! ";
            }
            int sentences = panicking ? 1 : r.irange(1, 3);
            for (int s = 0; s < sentences; s++) {
                int words = panicking ? r.irange(1, 3) : r.irange(3, 8);
                for (int k = 0; k < words; k++) {
                    text += "{" + wallaWord(r) + "}";
                    text += (k + 1 == words) ? "" : (r.f() > 0.75f ? ", " : " ");
                }
                text += panicking ? "! " : r.f() > 0.8f ? "? " : (excite > 0.5f && r.f() > 0.7f ? "! " : ". ");
            }
            std::vector<float> pcm;
            synthesize(text.c_str(), vp, sr, pcm);
            stream.insert(stream.end(), pcm.begin(), pcm.end());
            float gapMax = Lerp(1.2f - 0.6f * excite, 0.35f, Saturate(wp.panic));
            stream.resize(stream.size() + (size_t)(r.range(0.1f, std::max(0.15f, gapMax)) * (float)sr), 0.f);
        }
        // distance: level and brightness vary per talker
        float gain = r.range(0.35f, 1.f);
        ChBiquad lp;
        lp.lowpass(r.range(1800.f, 6000.f), 0.7f, (float)sr);
        for (size_t i = 0; i < total; i++) mix[i] += gain * lp.tick(stream[i]);
    }
    // seamless loop: crossfade the extra tail into the start (equal power)
    const size_t xf = total - len;
    for (size_t i = 0; i < xf; i++) {
        float a = (float)i / (float)std::max<size_t>(1, xf);
        mix[i] = mix[i] * sinf(0.5f * kPi * a) + mix[len + i] * cosf(0.5f * kPi * a);
    }
    mix.resize(len);
    // gentle level: RMS to about -20 dBFS, peaks limited to 0.5
    double s2 = 0.0;
    for (float v : mix) s2 += (double)v * v;
    float rms = (float)std::sqrt(s2 / std::max<size_t>(1, mix.size()));
    float g = rms > 1e-9f ? 0.1f / rms : 0.f;
    for (float& v : mix) {
        float y = v * g;
        v = 0.5f * tanhf(y / 0.5f);
    }
    out.insert(out.end(), mix.begin(), mix.end());
}

}  // namespace Speech
