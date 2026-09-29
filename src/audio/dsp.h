// DSP building blocks for the procedural audio engine: fast math, noise, oscillators, filters,
// envelopes, delay lines, modulation effects, an FDN reverb and dynamics processors.
// Header-only. Everything lives in Audio::dsp. All processors are allocation-free after init().
#pragma once
#include "../core/base.h"

namespace Audio {
namespace dsp {

constexpr int kSampleRate = 48000;       // internal mixing / synthesis rate
constexpr float kSR = 48000.f;
constexpr float kInvSR = 1.f / 48000.f;

// ---------------------------------------------------------------------------------------------
// Math helpers
FORCEINLINE float fastTanh(float x) {
    if (x < -3.f) return -1.f;
    if (x > 3.f) return 1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
// Cubic soft clipper: unity slope at 0, reaches +-1 with zero slope at +-1.5.
FORCEINLINE float softClip(float x) {
    if (x <= -1.5f) return -1.f;
    if (x >= 1.5f) return 1.f;
    return x - (4.f / 27.f) * x * x * x;
}
// Asymmetric "tube" style saturation (even harmonics), output roughly in [-1, 1].
FORCEINLINE float tubeSat(float x, float bias = 0.15f) {
    float y = fastTanh(x + bias) - fastTanh(bias);
    return y;
}
FORCEINLINE float dbToGain(float db) { return powf(10.f, db * 0.05f); }
FORCEINLINE float gainToDb(float g) { return 20.f * log10f(Max(g, 1e-9f)); }
FORCEINLINE float midiToHz(float m) { return 440.f * exp2f((m - 69.f) * (1.f / 12.f)); }
FORCEINLINE float semis(float s) { return exp2f(s * (1.f / 12.f)); }
FORCEINLINE float frac(float x) { return x - floorf(x); }

// Fast exp2/log2 approximations (Mineiro), accurate to ~1e-4 relative. Used in per-sample paths.
FORCEINLINE float fastExp2(float p) {
    float clipp = p < -126.f ? -126.f : (p > 126.f ? 126.f : p);
    float offset = clipp < 0.f ? 1.f : 0.f;
    int w = (int)clipp;
    float z = clipp - (float)w + offset;
    u32 i = (u32)((float)(1 << 23) * (clipp + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z));
    float f;
    memcpy(&f, &i, 4);
    return f;
}
FORCEINLINE float fastLog2(float x) {
    u32 vi;
    memcpy(&vi, &x, 4);
    u32 mi = (vi & 0x007FFFFFu) | 0x3f000000u;
    float mf;
    memcpy(&mf, &mi, 4);
    float y = (float)vi * 1.1920928955078125e-7f;
    return y - 124.22551499f - 1.498030302f * mf - 1.72587999f / (0.3520887068f + mf);
}
FORCEINLINE float fastDbToGain(float db) { return fastExp2(db * 0.16609640474f); }
FORCEINLINE float fastGainToDb(float g) { return 6.0205999f * fastLog2(Max(g, 1e-12f)); }

// Catmull-Rom cubic interpolation between x0 and x1.
FORCEINLINE float hermite(float xm1, float x0, float x1, float x2, float t) {
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

// Equal-power pan (-1 left .. +1 right).
FORCEINLINE void panGains(float pan, float& gl, float& gr) {
    float a = (Clamp(pan, -1.f, 1.f) * 0.5f + 0.5f) * kHalfPi;
    gl = cosf(a);
    gr = sinf(a);
}

// ---------------------------------------------------------------------------------------------
// Sine lookup table (initialized during static init, before any audio thread exists).
constexpr int kSineN = 4096;
inline float g_sineTab[kSineN + 1];
inline bool initSineTable() {
    for (int i = 0; i <= kSineN; i++) g_sineTab[i] = sinf(kTwoPi * (float)i / (float)kSineN);
    return true;
}
inline const bool g_sineTabInit = initSineTable();

// sin(2*pi*phase), phase in cycles (any real value).
FORCEINLINE float sinCycle(float phase) {
    phase -= floorf(phase);
    float f = phase * (float)kSineN;
    int i = (int)f;
    float fr = f - (float)i;
    const float a = g_sineTab[i];
    return a + (g_sineTab[i + 1] - a) * fr;
}
// Same, for phase already wrapped to [0,1).
FORCEINLINE float sinWrapped(float phase) {
    float f = phase * (float)kSineN;
    int i = (int)f;
    float fr = f - (float)i;
    const float a = g_sineTab[i];
    return a + (g_sineTab[i + 1] - a) * fr;
}

// ---------------------------------------------------------------------------------------------
// Noise
struct Noise {
    u32 s;
    explicit Noise(u32 seed = 0x12345678u) : s(seed ? seed : 0x9e3779b9u) {}
    void seed(u32 v) { s = v ? v : 0x9e3779b9u; }
    FORCEINLINE u32 nextU() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    FORCEINLINE float white() { return (float)(i32)nextU() * (1.f / 2147483648.f); }  // [-1,1)
    FORCEINLINE float uni() { return (float)(nextU() >> 8) * (1.f / 16777216.f); }     // [0,1)
    FORCEINLINE float range(float a, float b) { return a + (b - a) * uni(); }
    FORCEINLINE bool chance(float p) { return uni() < p; }
};

struct PinkNoise {  // Paul Kellet's refined pink filter
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    FORCEINLINE float process(float w) {
        b0 = 0.99886f * b0 + w * 0.0555179f;
        b1 = 0.99332f * b1 + w * 0.0750759f;
        b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f;
        b4 = 0.55000f * b4 + w * 0.5329522f;
        b5 = -0.7616f * b5 - w * 0.0168980f;
        float p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return p * 0.11f;
    }
};

struct BrownNoise {
    float y = 0;
    FORCEINLINE float process(float w) {
        y = (y + 0.02f * w) * (1.f / 1.02f);
        return y * 3.5f;
    }
};

// ---------------------------------------------------------------------------------------------
// Simple filters
FORCEINLINE float onePoleCoef(float fc, float sr = kSR) {
    return 1.f - expf(-kTwoPi * Clamp(fc, 1.f, sr * 0.49f) / sr);
}
struct OnePoleLP {
    float y = 0, a = 1;
    void set(float fc, float sr = kSR) { a = onePoleCoef(fc, sr); }
    void setCoef(float c) { a = c; }
    FORCEINLINE float process(float x) { return y += a * (x - y); }
    void reset(float v = 0) { y = v; }
};
struct OnePoleHP {
    float y = 0, a = 1;
    void set(float fc, float sr = kSR) { a = onePoleCoef(fc, sr); }
    FORCEINLINE float process(float x) {
        y += a * (x - y);
        return x - y;
    }
    void reset() { y = 0; }
};
struct DCBlocker {
    float x1 = 0, y1 = 0, r = 0.9975f;
    FORCEINLINE float process(float x) {
        float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0; }
};

// Zero-delay-feedback state variable filter (Simper). Stable under fast modulation.
struct Svf {
    float ic1 = 0, ic2 = 0;
    float a1 = 1, a2 = 0, a3 = 0, k = 1.4142f;
    void set(float fc, float q, float sr = kSR) {
        float g = tanf(kPi * Clamp(fc, 5.f, sr * 0.47f) / sr);
        k = 1.f / Max(q, 0.05f);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // Faster variant with precomputed g (e.g. from a table or approximations).
    void setG(float g, float q) {
        k = 1.f / Max(q, 0.05f);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    FORCEINLINE void tick(float v0, float& lp, float& bp, float& hp) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }
    FORCEINLINE float lp(float v0) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return v2;
    }
    FORCEINLINE float bp(float v0) {  // peak gain = Q
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return v1;
    }
    FORCEINLINE float bpNorm(float v0) { return bp(v0) * k; }  // unity peak gain
    FORCEINLINE float hp(float v0) {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return v0 - k * v1 - v2;
    }
    void reset() { ic1 = ic2 = 0; }
};

// Cheap tan() for SVF coefficient computation (x in [0, 1.5]); relative error < 0.2%.
FORCEINLINE float svfG(float fc, float sr = kSR) {
    float x = kPi * Clamp(fc, 5.f, sr * 0.46f) / sr;
    float x2 = x * x;
    // [5/4] Pade approximant of tan(x)
    return x * (1.f + x2 * (-0.11111111f + x2 * 0.0010582011f)) / (1.f + x2 * (-0.44444444f + x2 * 0.015873016f));
}

// RBJ biquad, transposed direct form II.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    FORCEINLINE float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void norm(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        float inv = 1.f / na0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }
    void setLP(float fc, float q, float sr = kSR) {
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), al = sinf(w) / (2.f * q);
        norm((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void setHP(float fc, float q, float sr = kSR) {
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), al = sinf(w) / (2.f * q);
        norm((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void setBP(float fc, float q, float sr = kSR) {  // constant 0 dB peak
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), al = sinf(w) / (2.f * q);
        norm(al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void setNotch(float fc, float q, float sr = kSR) {
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), al = sinf(w) / (2.f * q);
        norm(1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
    }
    void setPeak(float fc, float q, float gainDb, float sr = kSR) {
        float A = powf(10.f, gainDb / 40.f);
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), al = sinf(w) / (2.f * q);
        norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf(float fc, float gainDb, float sr = kSR, float slope = 1.f) {
        float A = powf(10.f, gainDb / 40.f);
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), s = sinf(w);
        float al = s * 0.5f * sqrtf((A + 1.f / A) * (1.f / slope - 1.f) + 2.f);
        float sq = 2.f * sqrtf(A) * al;
        norm(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void setHighShelf(float fc, float gainDb, float sr = kSR, float slope = 1.f) {
        float A = powf(10.f, gainDb / 40.f);
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), s = sinf(w);
        float al = s * 0.5f * sqrtf((A + 1.f / A) * (1.f / slope - 1.f) + 2.f);
        float sq = 2.f * sqrtf(A) * al;
        norm(A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void setAllpass(float fc, float q, float sr = kSR) {
        float w = kTwoPi * Clamp(fc, 5.f, sr * 0.49f) / sr, c = cosf(w), al = sinf(w) / (2.f * q);
        norm(1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
};

// Two-pole resonator (for modal synthesis): y = x*g + 2r cos(w) y1 - r^2 y2.
struct Resonator {
    float c1 = 0, c2 = 0, g = 0, y1 = 0, y2 = 0;
    void set(float freq, float decaySec, float sr = kSR) {
        float r = expf(-1.f / Max(decaySec * sr, 1.f));
        float w = kTwoPi * Clamp(freq, 1.f, sr * 0.49f) / sr;
        c1 = 2.f * r * cosf(w);
        c2 = -r * r;
        g = (1.f - r * r) * 0.5f;
    }
    FORCEINLINE float process(float x) {
        float y = x * g + c1 * y1 + c2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
    void reset() { y1 = y2 = 0; }
};

// ---------------------------------------------------------------------------------------------
// Envelopes
struct Adsr {
    enum Stage : u8 { kIdle, kAttack, kDecay, kSustain, kRelease };
    Stage stage = kIdle;
    float level = 0;
    float attackInc = 1, decayCoef = 0, sustain = 1, releaseCoef = 0;
    void set(float a, float d, float s, float r, float sr = kSR) {
        attackInc = 1.f / Max(a * sr, 1.f);
        decayCoef = expf(-1.f / Max(d * sr * 0.3f, 1.f));   // ~ -60dB after 'd' seconds * 2
        sustain = s;
        releaseCoef = expf(-1.f / Max(r * sr * 0.3f, 1.f));
    }
    void noteOn(bool retrigFromZero = false) {
        if (retrigFromZero) level = 0;
        stage = kAttack;
    }
    void noteOff() {
        if (stage != kIdle) stage = kRelease;
    }
    FORCEINLINE float process() {
        switch (stage) {
            case kIdle: return 0.f;
            case kAttack:
                level += attackInc;
                if (level >= 1.f) { level = 1.f; stage = kDecay; }
                return level;
            case kDecay:
                level = sustain + (level - sustain) * decayCoef;
                if (level - sustain < 1e-4f) { level = sustain; stage = kSustain; }
                return level;
            case kSustain: return level;
            case kRelease:
                level *= releaseCoef;
                if (level < 1e-5f) { level = 0; stage = kIdle; }
                return level;
        }
        return 0.f;
    }
    // Advance n samples at once (control-rate use); returns the level after n samples.
    float processN(int n) {
        float v = 0;
        for (int i = 0; i < n; i++) v = process();
        return v;
    }
    bool active() const { return stage != kIdle; }
};

// Exponential decay envelope, used for percussive voices.
struct DecayEnv {
    float level = 0, coef = 0;
    void trigger(float amp, float decaySec, float sr = kSR) {
        level = amp;
        coef = expf(-6.9f / Max(decaySec * sr, 1.f));  // -60 dB after decaySec
    }
    FORCEINLINE float process() { return level *= coef; }
};

// Linear parameter smoother (per-block target, per-sample ramp).
struct Ramp {
    float cur = 0, inc = 0;
    int left = 0;
    void setImmediate(float v) { cur = v; inc = 0; left = 0; }
    void rampTo(float target, int samples) {
        if (samples <= 0) { setImmediate(target); return; }
        inc = (target - cur) / (float)samples;
        left = samples;
    }
    FORCEINLINE float next() {
        if (left > 0) { cur += inc; left--; }
        return cur;
    }
};

// One-pole smoother for control values (call once per block or per sample).
struct Smoother {
    float v = 0;
    FORCEINLINE float step(float target, float coef) { return v += (target - v) * coef; }
};

// ---------------------------------------------------------------------------------------------
// Oscillators
FORCEINLINE float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt) {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}
enum class Wave : u8 { Sine, Saw, Square, Pulse, Triangle, Noise };

struct BlepOsc {
    float phase = 0;
    FORCEINLINE float saw(float dt) {
        float t = phase;
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;
        return 2.f * t - 1.f - polyBlep(t, dt);
    }
    FORCEINLINE float pulse(float dt, float pw) {
        float t = phase;
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;
        float v = t < pw ? 1.f : -1.f;
        v += polyBlep(t, dt);
        float t2 = t + 1.f - pw;
        if (t2 >= 1.f) t2 -= 1.f;
        v -= polyBlep(t2, dt);
        return v;
    }
    FORCEINLINE float square(float dt) { return pulse(dt, 0.5f); }
    FORCEINLINE float tri(float dt) {
        float t = phase;
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;
        return 4.f * fabsf(t - 0.5f) - 1.f;
    }
    FORCEINLINE float sine(float dt) {
        float t = phase;
        phase += dt;
        if (phase >= 1.f) phase -= 1.f;
        return sinWrapped(t);
    }
    FORCEINLINE float wave(Wave w, float dt, float pw, Noise& n) {
        switch (w) {
            case Wave::Sine: return sine(dt);
            case Wave::Saw: return saw(dt);
            case Wave::Square: return pulse(dt, 0.5f);
            case Wave::Pulse: return pulse(dt, pw);
            case Wave::Triangle: return tri(dt);
            case Wave::Noise: return n.white();
        }
        return 0.f;
    }
};

// ---------------------------------------------------------------------------------------------
// Delay lines
struct DelayLine {
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init(int maxSamples) {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign((size_t)n, 0.f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
    FORCEINLINE void write(float x) {
        buf[(size_t)w] = x;
        w = (w + 1) & mask;
    }
    // Read 'd' samples behind the most recently written sample (d >= 1 means one sample back).
    FORCEINLINE float readInt(int d) const { return buf[(size_t)((w - 1 - d) & mask)]; }
    FORCEINLINE float read(float d) const {
        int di = (int)d;
        float fr = d - (float)di;
        float a = buf[(size_t)((w - 1 - di) & mask)];
        float b = buf[(size_t)((w - 2 - di) & mask)];
        return a + (b - a) * fr;
    }
    FORCEINLINE float readCubic(float d) const {
        int di = (int)d;
        float fr = d - (float)di;
        float xm1 = buf[(size_t)((w - di) & mask)];
        float x0 = buf[(size_t)((w - 1 - di) & mask)];
        float x1 = buf[(size_t)((w - 2 - di) & mask)];
        float x2 = buf[(size_t)((w - 3 - di) & mask)];
        return hermite(xm1, x0, x1, x2, fr);
    }
};

// Schroeder allpass with fixed delay.
struct AllpassDelay {
    std::vector<float> buf;
    int len = 1, pos = 0;
    float g = 0.6f;
    void init(int n, float gain) {
        len = Max(1, n);
        buf.assign((size_t)len, 0.f);
        pos = 0;
        g = gain;
    }
    FORCEINLINE float process(float x) {
        float d = buf[(size_t)pos];
        float v = x - g * d;
        buf[(size_t)pos] = v;
        if (++pos >= len) pos = 0;
        return d + g * v;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
};

// ---------------------------------------------------------------------------------------------
// FDN reverb: 8 lines, Householder feedback, per-line HF damping, input diffusion, early
// reflections, pre-delay and slow delay modulation. Stereo in/out (wet only).
struct FdnReverb {
    static constexpr int N = 8;
    DelayLine lines[N];
    float baseLen[N] = {};
    float gain[N] = {};
    float dampState[N] = {};
    float dampCoef = 0.3f;
    AllpassDelay diffL[2], diffR[2];
    DelayLine pre;
    DelayLine er;
    int preDelay = 480;
    float erTapsL[8] = {}, erTapsR[8] = {}, erGain[8] = {};
    float erLevel = 0.4f, lateLevel = 1.f;
    float modPhase = 0, modRate = 0.00002f, modDepth = 6.f;
    float lowCutState[2] = {};
    float rt60 = 1.5f;
    void init(float size, u32 seed) {
        static const float kLens[N] = {1433, 1601, 1867, 2053, 2251, 2399, 2687, 2903};
        u32 s = seed | 1u;
        for (int i = 0; i < N; i++) {
            s = s * 1664525u + 1013904223u;
            float jitter = 1.f + ((float)(s >> 9) / 8388608.f - 0.5f) * 0.06f;
            baseLen[i] = kLens[i] * size * jitter;
            lines[i].init((int)(baseLen[i] + modDepth * 2.f + 8.f));
            dampState[i] = 0;
        }
        diffL[0].init((int)(142 * size), 0.62f);
        diffL[1].init((int)(379 * size), 0.6f);
        diffR[0].init((int)(157 * size), 0.62f);
        diffR[1].init((int)(353 * size), 0.6f);
        pre.init(9600);
        er.init(9600);
        static const float kErL[8] = {7.1f, 11.3f, 17.9f, 23.3f, 31.7f, 41.2f, 53.9f, 67.1f};
        static const float kErR[8] = {8.3f, 12.9f, 19.7f, 26.1f, 33.9f, 44.5f, 57.3f, 71.9f};
        for (int i = 0; i < 8; i++) {
            erTapsL[i] = kErL[i] * size * 48.f;
            erTapsR[i] = kErR[i] * size * 48.f;
            erGain[i] = powf(0.82f, (float)i) * ((i & 1) ? -1.f : 1.f) * 0.35f;
        }
        setDecay(1.5f, 0.35f);
    }
    // rt60 seconds; damping 0 (bright) .. 0.9 (very dark)
    void setDecay(float rt, float damping) {
        rt60 = Max(rt, 0.1f);
        for (int i = 0; i < N; i++) gain[i] = powf(10.f, -3.f * baseLen[i] / (rt60 * kSR));
        dampCoef = Clamp(damping, 0.f, 0.95f);
    }
    void setPreDelay(float sec) { preDelay = Clamp((int)(sec * kSR), 1, 9000); }
    void clear() {
        for (auto& l : lines) l.clear();
        for (int i = 0; i < 2; i++) { diffL[i].clear(); diffR[i].clear(); }
        pre.clear();
        er.clear();
        for (float& d : dampState) d = 0;
    }
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        float y[N];
        for (int s = 0; s < n; s++) {
            float xl = inL[s], xr = inR[s];
            pre.write(0.5f * (xl + xr));
            float xp = pre.readInt(preDelay);
            er.write(xp);
            // early reflections
            float el = 0, erv = 0;
            for (int t = 0; t < 8; t++) {
                el += er.readInt((int)erTapsL[t]) * erGain[t];
                erv += er.readInt((int)erTapsR[t]) * erGain[t];
            }
            // diffused input (stereo)
            float dl = diffL[1].process(diffL[0].process(xp + 0.3f * (xl - xr)));
            float dr = diffR[1].process(diffR[0].process(xp - 0.3f * (xl - xr)));
            // read lines
            modPhase += modRate;
            if (modPhase >= 1.f) modPhase -= 1.f;
            float m0 = sinWrapped(modPhase) * modDepth;
            float ph2 = modPhase + 0.25f;
            if (ph2 >= 1.f) ph2 -= 1.f;
            float m1 = sinWrapped(ph2) * modDepth;
            for (int i = 0; i < N; i++) {
                float d = baseLen[i];
                if (i == 1) d += m0;
                else if (i == 6) d += m1;
                float v = (i == 1 || i == 6) ? lines[i].read(d) : lines[i].readInt((int)d);
                // HF damping
                dampState[i] = v + dampCoef * (dampState[i] - v);
                y[i] = dampState[i] * gain[i];
            }
            // Householder reflection
            float sum = 0;
            for (int i = 0; i < N; i++) sum += y[i];
            sum *= (2.f / N);
            for (int i = 0; i < N; i++) {
                float in = (i & 1) ? dr : dl;
                if (i & 2) in = -in;
                lines[i].write(y[i] - sum + in * 0.5f);
            }
            float ol = (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f - y[5] * 0.5f) * 0.5f;
            float orr = (y[1] - y[3] + y[5] - y[7] + y[2] * 0.5f - y[6] * 0.5f) * 0.5f;
            outL[s] = ol * lateLevel + el * erLevel;
            outR[s] = orr * lateLevel + erv * erLevel;
        }
    }
};

// ---------------------------------------------------------------------------------------------
// Modulation effects
struct Chorus {
    DelayLine l, r;
    float phase = 0, rate = 0.5f, depth = 96.f, base = 720.f, mix = 0.5f;
    void init() {
        l.init(4096);
        r.init(4096);
    }
    void set(float rateHz, float depthMs, float baseMs, float wet) {
        rate = rateHz * kInvSR;
        depth = depthMs * 48.f;
        base = baseMs * 48.f;
        mix = wet;
    }
    FORCEINLINE void process(float& xl, float& xr) {
        l.write(xl);
        r.write(xr);
        phase += rate;
        if (phase >= 1.f) phase -= 1.f;
        float p2 = phase + 0.33f;
        if (p2 >= 1.f) p2 -= 1.f;
        float p3 = phase + 0.5f;
        if (p3 >= 1.f) p3 -= 1.f;
        float wl = l.read(base + depth * (0.5f + 0.5f * sinWrapped(phase))) * 0.6f +
                   r.read(base * 1.3f + depth * (0.5f + 0.5f * sinWrapped(p2))) * 0.4f;
        float wr = r.read(base + depth * (0.5f + 0.5f * sinWrapped(p3))) * 0.6f +
                   l.read(base * 1.3f + depth * (0.5f + 0.5f * sinWrapped(frac(p2 + 0.5f)))) * 0.4f;
        xl = xl * (1.f - 0.5f * mix) + wl * mix;
        xr = xr * (1.f - 0.5f * mix) + wr * mix;
    }
};

// Stereo feedback delay with damping and optional ping-pong. Wet output only.
struct StereoDelay {
    DelayLine l, r;
    float timeL = 12000, timeR = 18000, fb = 0.35f;
    OnePoleLP lpL, lpR;
    OnePoleHP hpL, hpR;
    bool pingPong = true;
    void init(int maxSamples) {
        l.init(maxSamples);
        r.init(maxSamples);
        setTone(4500.f, 250.f);
    }
    void setTone(float lpHz, float hpHz) {
        lpL.set(lpHz); lpR.set(lpHz);
        hpL.set(hpHz); hpR.set(hpHz);
    }
    void set(float tL, float tR, float feedback, bool pp) {
        int cap = (int)l.buf.size() - 8;
        timeL = Clamp(tL, 1.f, (float)cap);
        timeR = Clamp(tR, 1.f, (float)cap);
        fb = feedback;
        pingPong = pp;
    }
    FORCEINLINE void process(float inL, float inR, float& outL, float& outR) {
        float dl = l.read(timeL), dr = r.read(timeR);
        float fl = hpL.process(lpL.process(dl)), fr = hpR.process(lpR.process(dr));
        if (pingPong) {
            l.write(0.5f * (inL + inR) + fr * fb);
            r.write(fl * fb);
        } else {
            l.write(inL + fl * fb);
            r.write(inR + fr * fb);
        }
        outL = dl;
        outR = dr;
    }
};

// ---------------------------------------------------------------------------------------------
// Dynamics
struct Compressor {
    float thresh = -18.f, ratio = 3.f, knee = 6.f, makeup = 0.f;
    float att = 0.01f, rel = 0.001f;
    float env = 0.f;  // current gain reduction in dB (>= 0)
    float detector = 0.f;
    bool rmsMode = false;
    float rmsCoef = 0.001f;
    void set(float threshDb, float r, float attackMs, float releaseMs, float kneeDb = 6.f, float makeupDb = 0.f,
             float sr = kSR) {
        thresh = threshDb;
        ratio = Max(r, 1.f);
        knee = Max(kneeDb, 0.01f);
        makeup = makeupDb;
        att = 1.f - expf(-1.f / Max(attackMs * 0.001f * sr, 1.f));
        rel = 1.f - expf(-1.f / Max(releaseMs * 0.001f * sr, 1.f));
        rmsCoef = 1.f - expf(-1.f / (0.01f * sr));
    }
    FORCEINLINE float gainReductionFor(float levelDb) const {
        float over = levelDb - thresh;
        float slope = 1.f - 1.f / ratio;
        if (over <= -knee * 0.5f) return 0.f;
        if (over >= knee * 0.5f) return over * slope;
        float t = over + knee * 0.5f;
        return slope * t * t / (2.f * knee);
    }
    // Returns the linear gain to apply for the given detector input (abs peak value).
    FORCEINLINE float computeGain(float level) {
        if (rmsMode) {
            detector += (level * level - detector) * rmsCoef;
            level = sqrtf(detector);
        }
        float target = gainReductionFor(fastGainToDb(level));
        env += (target - env) * (target > env ? att : rel);
        return fastDbToGain(makeup - env);
    }
    void processStereo(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) {
            float lv = Max(fabsf(L[i]), fabsf(R[i]));
            float g = computeGain(lv);
            L[i] *= g;
            R[i] *= g;
        }
    }
    void processMono(float* x, int n) {
        for (int i = 0; i < n; i++) x[i] *= computeGain(fabsf(x[i]));
    }
};

// Brick-wall lookahead limiter: moving-minimum gain + release smoothing + box-filter attack.
struct LookaheadLimiter {
    int la = 72;
    float ceiling = 0.97f;
    float relCoef = 0.0005f;
    std::vector<float> dL, dR;  // delay buffers (la+1)
    std::vector<float> box;     // box filter history (la)
    std::vector<float> dqVal;
    std::vector<i64> dqIdx;
    int dqHead = 0, dqCount = 0, dqCap = 0;
    int dpos = 0, bpos = 0;
    double boxSum = 0;
    float rel = 1.f;
    i64 idx = 0;
    float lastGain = 1.f;
    void init(int lookahead, float ceilingLin, float releaseMs, float sr = kSR) {
        la = Max(lookahead, 2);
        ceiling = ceilingLin;
        relCoef = 1.f - expf(-1.f / Max(releaseMs * 0.001f * sr, 1.f));
        dL.assign((size_t)la + 1, 0.f);
        dR.assign((size_t)la + 1, 0.f);
        box.assign((size_t)la, 1.f);
        boxSum = (double)la;
        dqCap = la + 4;
        dqVal.assign((size_t)dqCap, 1.f);
        dqIdx.assign((size_t)dqCap, 0);
        dqHead = dqCount = 0;
        dpos = bpos = 0;
        rel = 1.f;
        idx = 0;
    }
    FORCEINLINE void processSample(float& l, float& r) {
        float peak = Max(fabsf(l), fabsf(r));
        float target = peak > ceiling ? ceiling / peak : 1.f;
        // push into monotonic deque (increasing values from head)
        while (dqCount > 0) {
            int back = (dqHead + dqCount - 1) % dqCap;
            if (dqVal[(size_t)back] >= target) dqCount--;
            else break;
        }
        int ins = (dqHead + dqCount) % dqCap;
        dqVal[(size_t)ins] = target;
        dqIdx[(size_t)ins] = idx;
        dqCount++;
        while (dqIdx[(size_t)dqHead] <= idx - (la + 1)) {
            dqHead = (dqHead + 1) % dqCap;
            dqCount--;
        }
        float m = dqVal[(size_t)dqHead];
        if (m < rel) rel = m;
        else rel += (m - rel) * relCoef;
        // box filter
        boxSum += (double)rel - (double)box[(size_t)bpos];
        box[(size_t)bpos] = rel;
        if (++bpos >= la) bpos = 0;
        float g = (float)(boxSum / (double)la);
        // delay
        float ol = dL[(size_t)dpos], orr = dR[(size_t)dpos];
        dL[(size_t)dpos] = l;
        dR[(size_t)dpos] = r;
        if (++dpos > la) dpos = 0;
        idx++;
        lastGain = g;
        l = Clamp(ol * g, -1.f, 1.f);
        r = Clamp(orr * g, -1.f, 1.f);
    }
    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) processSample(L[i], R[i]);
        // periodically re-sync the running sum to avoid drift
        if ((idx & 0xFFFF) < (i64)n) {
            double s = 0;
            for (float v : box) s += v;
            boxSum = s;
        }
    }
};

// Envelope follower (peak, attack/release in seconds).
struct EnvFollower {
    float env = 0, att = 0.1f, rel = 0.001f;
    void set(float attackSec, float releaseSec, float sr = kSR) {
        att = 1.f - expf(-1.f / Max(attackSec * sr, 1.f));
        rel = 1.f - expf(-1.f / Max(releaseSec * sr, 1.f));
    }
    FORCEINLINE float process(float x) {
        float a = fabsf(x);
        env += (a - env) * (a > env ? att : rel);
        return env;
    }
};

}  // namespace dsp
}  // namespace Audio
