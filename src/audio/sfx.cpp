// Procedural one-shot sound effects. Every Audio::Sfx (plus internal ambience one-shots) is
// synthesized from scratch - transients, filtered noise, modal resonators, formant voices, FM
// bells, supersaws - and pre-rendered into several random variations at startup on worker threads.
#include "audio_internal.h"

namespace Audio {
namespace detail {
namespace sfxgen {

using namespace dsp;
constexpr float SR = 48000.f;
FORCEINLINE int S(float sec) { return (int)(sec * SR + 0.5f); }

// ---------------------------------------------------------------------------------------------
// Offline synthesis toolkit
struct Buf {
    std::vector<float> L, R;
    bool stereo = false;
    Rng rng;
    Noise nz;
    explicit Buf(u32 seed, bool st = false)
        : stereo(st), rng(seed, 0x5851f42d4c957f2dULL ^ (u64)seed), nz(hash32(seed * 7919u + 1u) | 1u) {}
    void ensure(int n) {
        if ((int)L.size() < n) {
            L.resize((size_t)n, 0.f);
            if (stereo) R.resize((size_t)n, 0.f);
        }
    }
    int len() const { return (int)L.size(); }
    float rnd(float a, float b) { return rng.range(a, b); }
    int irnd(int a, int b) { return rng.irange(a, b); }
    bool chance(float p) { return rng.f() < p; }
    FORCEINLINE void addPan(int i, float v, float gl, float gr) {
        if (stereo) {
            L[(size_t)i] += v * gl;
            R[(size_t)i] += v * gr;
        } else {
            L[(size_t)i] += v;
        }
    }
};

FORCEINLINE void panG(float pan, float& gl, float& gr) {
    panGains(pan, gl, gr);
    gl *= 1.41421356f;
    gr *= 1.41421356f;
}

struct AD {  // linear attack, exponential decay
    float v = 0, dec = 0;
    int attN = 1, i = 0;
    AD(float a, float tau) {
        attN = Max(1, S(a));
        dec = expf(-1.f / Max(tau * SR, 1.f));
    }
    FORCEINLINE float next() {
        if (i < attN) {
            i++;
            v = (float)i / (float)attN;
            return v;
        }
        v *= dec;
        return v;
    }
};

// Sine with exponential pitch glide f0 -> f1, AD envelope, optional harmonics.
static void tone(Buf& b, float t0, float dur, float f0, float f1, float glide, float amp, float att, float tau,
                 float pan = 0.f, float h2 = 0.f, float h3 = 0.f) {
    int s0 = S(t0), n = S(dur);
    if (n <= 0) return;
    b.ensure(s0 + n);
    float gl, gr;
    panG(pan, gl, gr);
    AD env(att, tau);
    float ph = 0.f, f = f0;
    float gk = glide > 0.f ? expf(-1.f / (glide * SR)) : 0.f;
    int fadeN = Max(1, Min(n, S(0.004f)));
    for (int i = 0; i < n; i++) {
        f = f1 + (f - f1) * gk;
        ph += f / SR;
        ph -= floorf(ph);
        float v = sinWrapped(ph);
        if (h2 != 0.f) v += h2 * sinCycle(ph * 2.f);
        if (h3 != 0.f) v += h3 * sinCycle(ph * 3.f);
        float e = env.next();
        if (i > n - fadeN) e *= (float)(n - i) / (float)fadeN;
        b.addPan(s0 + i, v * e * amp, gl, gr);
        if (i > env.attN && e < 1e-4f) break;
    }
}

enum FType { FNone = 0, FLP, FHP, FBP, FLP4, FHP4 };
// Filtered noise burst (color 0 white, 1 pink, 2 brown), cutoff glides fc0 -> fc1.
static void noise(Buf& b, float t0, float dur, float amp, float att, float tau, int ft, float fc0, float fc1 = -1.f,
                  float glide = 0.1f, float q = 0.707f, int color = 0, float pan = 0.f) {
    int s0 = S(t0), n = S(dur);
    if (n <= 0) return;
    b.ensure(s0 + n);
    if (fc1 < 0.f) fc1 = fc0;
    float gl, gr;
    panG(pan, gl, gr);
    AD env(att, tau);
    Svf f1, f2;
    PinkNoise pk;
    BrownNoise br;
    float fc = fc0;
    float gk = expf(-1.f / Max(glide * SR, 1.f));
    int fadeN = Max(1, Min(n, S(0.004f)));
    for (int i = 0; i < n; i++) {
        if ((i & 15) == 0) {
            float g = svfG(fc);
            f1.setG(g, q);
            f2.setG(g, q);
        }
        fc = fc1 + (fc - fc1) * gk;
        float w = b.nz.white();
        if (color == 1) w = pk.process(w);
        else if (color == 2) w = br.process(w);
        float y;
        switch (ft) {
            case FLP: y = f1.lp(w); break;
            case FHP: y = f1.hp(w); break;
            case FBP: y = f1.bp(w) * f1.k; break;
            case FLP4: y = f2.lp(f1.lp(w)); break;
            case FHP4: y = f2.hp(f1.hp(w)); break;
            default: y = w; break;
        }
        float e = env.next();
        if (i > n - fadeN) e *= (float)(n - i) / (float)fadeN;
        b.addPan(s0 + i, y * e * amp, gl, gr);
        if (i > env.attN && e < 1e-4f) break;
    }
}

struct Mode {
    float f, tau, a;
};
// Sum of exponentially decaying sinusoids (modal synthesis).
static void modes(Buf& b, float t0, float amp, const Mode* m, int count, float pan = 0.f, float maxDur = 6.f) {
    int s0 = S(t0);
    float gl, gr;
    panG(pan, gl, gr);
    for (int k = 0; k < count; k++) {
        if (m[k].f <= 20.f || m[k].f >= SR * 0.48f || m[k].a == 0.f) continue;
        float dur = Min(maxDur, m[k].tau * 7.f);
        int n = S(dur);
        b.ensure(s0 + n);
        float r = expf(-1.f / Max(m[k].tau * SR, 1.f));
        float w = kTwoPi * m[k].f / SR;
        float c = 2.f * r * cosf(w), r2 = r * r;
        float y2 = 0.f, y1 = r * sinf(w) * m[k].a * amp;
        b.addPan(s0, 0.f, gl, gr);
        if (n > 1) b.addPan(s0 + 1, y1, gl, gr);
        for (int i = 2; i < n; i++) {
            float y = c * y1 - r2 * y2;
            y2 = y1;
            y1 = y;
            b.addPan(s0 + i, y, gl, gr);
        }
    }
}

// Band-limited-ish click (Hann pulse); nwave adds a negative lobe (supersonic crack shape).
static void click(Buf& b, float t0, float amp, int width = 4, bool nwave = false, float pan = 0.f) {
    int s0 = S(t0);
    b.ensure(s0 + width * 2 + 2);
    float gl, gr;
    panG(pan, gl, gr);
    for (int i = 0; i < width; i++) {
        float w = sinf(kPi * ((float)i + 0.5f) / (float)width);
        b.addPan(s0 + i, amp * w, gl, gr);
        if (nwave) b.addPan(s0 + width + i, -amp * w * 0.85f, gl, gr);
    }
}

// Random resonant grains (debris, gravel, shards, sparks). Poisson arrivals.
static void grains(Buf& b, float t0, float dur, float rate, float amp, float fLo, float fHi, float tauLo, float tauHi,
                   float rateDecay = 0.f, float pan = 0.f, float panSpread = 0.f) {
    float t = t0;
    for (int guard = 0; guard < 20000; guard++) {
        float r = rate * (rateDecay > 0.f ? expf(-(t - t0) / rateDecay) : 1.f);
        if (r < 0.05f) break;
        t += -logf(Max(1e-6f, b.rng.f())) / r;
        if (t > t0 + dur) break;
        Mode m;
        m.f = fLo * powf(fHi / fLo, b.rng.f());
        m.tau = tauLo + (tauHi - tauLo) * b.rng.f();
        m.a = (0.15f + 0.85f * b.rng.f() * b.rng.f()) * (rateDecay > 0.f ? 0.4f + 0.6f * expf(-(t - t0) / rateDecay) : 1.f);
        modes(b, t, amp, &m, 1, pan + b.rnd(-panSpread, panSpread));
    }
}

// Random short noise pops (crackle).
static void crackles(Buf& b, float t0, float dur, float rate, float amp, float fLo, float fHi, float rateDecay = 0.f,
                     float panSpread = 0.f) {
    float t = t0;
    for (int guard = 0; guard < 5000; guard++) {
        float r = rate * (rateDecay > 0.f ? expf(-(t - t0) / rateDecay) : 1.f);
        if (r < 0.05f) break;
        t += -logf(Max(1e-6f, b.rng.f())) / r;
        if (t > t0 + dur) break;
        float a = amp * (0.2f + 0.8f * b.rng.f() * b.rng.f() * b.rng.f());
        float f = fLo * powf(fHi / fLo, b.rng.f());
        noise(b, t, 0.02f, a, 0.0002f, b.rnd(0.0008f, 0.004f), FBP, f, f, 0.1f, 1.2f, 0, b.rnd(-panSpread, panSpread));
    }
}

// Mixes an FDN reverb tail into the buffer (extends it).
static void reverb(Buf& b, float rt, float wet, float damp = 0.4f, float pre = 0.012f, float size = 1.f, float er = 0.3f) {
    int n0 = b.len();
    b.ensure(n0 + S(rt * 1.15f));
    int n = b.len();
    FdnReverb rv;
    rv.init(size, (u32)b.rng.next());
    rv.setDecay(rt, damp);
    rv.setPreDelay(pre);
    rv.erLevel = er;
    std::vector<float> oL((size_t)n), oR((size_t)n);
    const float* inL = b.L.data();
    const float* inR = b.stereo ? b.R.data() : b.L.data();
    rv.process(inL, inR, oL.data(), oR.data(), n);
    for (int i = 0; i < n; i++) {
        if (b.stereo) {
            b.L[(size_t)i] += oL[(size_t)i] * wet;
            b.R[(size_t)i] += oR[(size_t)i] * wet;
        } else {
            b.L[(size_t)i] += 0.5f * (oL[(size_t)i] + oR[(size_t)i]) * wet;
        }
    }
}

// Discrete filtered echoes of the first `srcDur` seconds (outdoor slapback from buildings/hills).
static void echoes(Buf& b, int count, float first, float spacing, float gain, float decay, float lpfc,
                   float srcDur = 0.25f) {
    int n = Min(b.len(), S(srcDur));
    if (n <= 0) return;
    std::vector<float> src(b.L.begin(), b.L.begin() + n);
    Biquad lp;
    lp.setLP(lpfc, 0.6f);
    for (auto& v : src) v = lp.process(v);
    float t = first, g = gain;
    for (int k = 0; k < count; k++) {
        int off = S(t);
        b.ensure(off + n);
        float pan = b.stereo ? b.rnd(-0.7f, 0.7f) : 0.f;
        float gl, gr;
        panG(pan, gl, gr);
        for (int i = 0; i < n; i++) b.addPan(off + i, src[(size_t)i] * g, gl, gr);
        lp.reset();
        lp.setLP(lpfc * powf(0.8f, (float)(k + 1)), 0.6f);
        for (auto& v : src) v = lp.process(v);
        t += spacing * b.rnd(0.7f, 1.35f);
        g *= decay;
    }
}

static void filt(Buf& b, Biquad proto, int from = 0) {
    Biquad f = proto;
    for (size_t i = (size_t)from; i < b.L.size(); i++) b.L[i] = f.process(b.L[i]);
    if (b.stereo) {
        f = proto;
        f.reset();
        for (size_t i = (size_t)from; i < b.R.size(); i++) b.R[i] = f.process(b.R[i]);
    }
}
static void lowpass(Buf& b, float fc, float q = 0.707f) {
    Biquad f;
    f.setLP(fc, q);
    filt(b, f);
}
static void highpass(Buf& b, float fc, float q = 0.707f) {
    Biquad f;
    f.setHP(fc, q);
    filt(b, f);
}
static void peakEq(Buf& b, float fc, float q, float db) {
    Biquad f;
    f.setPeak(fc, q, db);
    filt(b, f);
}
static void saturate(Buf& b, float drive) {
    for (auto& v : b.L) v = fastTanh(v * drive) / fastTanh(drive);
    if (b.stereo)
        for (auto& v : b.R) v = fastTanh(v * drive) / fastTanh(drive);
}

// ---------------------------------------------------------------------------------------------
// Formant voice (glottal source -> cascade formant resonators -> lip radiation).
struct VFrame {
    float f0 = 120.f, amp = 1.f, breath = 0.05f, bright = 0.5f, rough = 0.f;
    float F[4] = {700.f, 1220.f, 2600.f, 3500.f};
    float BW[4] = {80.f, 90.f, 120.f, 180.f};
};
struct KRes {
    float a = 0, bb = 0, c = 0, y1 = 0, y2 = 0;
    void set(float f, float bw) {
        float T = 1.f / SR;
        c = -expf(-kTwoPi * bw * T);
        bb = 2.f * expf(-kPi * bw * T) * cosf(kTwoPi * Min(f, SR * 0.45f) * T);
        a = 1.f - bb - c;
    }
    FORCEINLINE float process(float x) {
        float y = a * x + bb * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};
template <class Fn>
static void vocal(Buf& b, float t0, float dur, float jitter, Fn fn, float gain = 1.f, float pan = 0.f) {
    int s0 = S(t0), n = S(dur);
    if (n <= 0) return;
    b.ensure(s0 + n);
    float gl, gr;
    panG(pan, gl, gr);
    VFrame fr;
    KRes r[4];
    OnePoleLP tilt;
    OnePoleHP aspHp;
    aspHp.set(1200.f);
    float f0s = -1.f, ph = 0.f, prevY = 0.f, jit = 0.f, shim = 1.f, ampS = 0.f;
    int periods = 0;
    for (int i = 0; i < n; i++) {
        if ((i & 31) == 0) {
            fn((float)i / (float)n, fr);
            for (int k = 0; k < 4; k++) r[k].set(fr.F[k], fr.BW[k]);
            tilt.set(600.f + 5200.f * fr.bright);
            if (f0s < 0.f) {
                f0s = fr.f0;
                ampS = fr.amp;
            }
        }
        f0s += (fr.f0 - f0s) * 0.03f;
        ampS += (fr.amp - ampS) * 0.02f;
        jit += (b.nz.white() * jitter - jit) * 0.02f;
        float f = f0s * (1.f + jit);
        float dt = Min(f / SR, 0.45f);
        ph += dt;
        if (ph >= 1.f) {
            ph -= 1.f;
            periods++;
            shim = 1.f - fr.rough * (0.45f * (float)(periods & 1) + 0.35f * b.nz.uni());
        }
        float src = -((2.f * ph - 1.f) - polyBlep(ph, dt));
        src = tilt.process(src) * shim;
        float asp = aspHp.process(b.nz.white()) * fr.breath * (ph < 0.5f ? 1.f : 0.45f);
        float x = src * (1.f - 0.6f * fr.breath) + asp * 1.6f;
        float y = r[3].process(r[2].process(r[1].process(r[0].process(x))));
        float o = y - prevY;
        prevY = y;
        b.addPan(s0 + i, o * ampS * gain, gl, gr);
    }
}

// FM operator pair: sin(2pi fc t + I(t) sin(2pi fm t)).
static void fmNote(Buf& b, float t0, float dur, float f, float ratio, float index, float idxTau, float amp, float att,
                   float tau, float pan = 0.f) {
    int s0 = S(t0), n = S(dur);
    if (n <= 0) return;
    b.ensure(s0 + n);
    float gl, gr;
    panG(pan, gl, gr);
    AD env(att, tau);
    float pc = 0.f, pm = 0.f, idx = index;
    float ik = expf(-1.f / Max(idxTau * SR, 1.f));
    int fadeN = Max(1, Min(n, S(0.01f)));
    for (int i = 0; i < n; i++) {
        pc += f / SR;
        pc -= floorf(pc);
        pm += f * ratio / SR;
        pm -= floorf(pm);
        idx *= ik;
        float v = sinCycle(pc + idx * sinWrapped(pm) * 0.159155f);
        float e = env.next();
        if (i > n - fadeN) e *= (float)(n - i) / (float)fadeN;
        b.addPan(s0 + i, v * e * amp, gl, gr);
        if (i > env.attN && e < 1e-4f) break;
    }
}

// Detuned saw stack through a resonant low-pass with cutoff envelope (brass stabs, pads).
static void sawNote(Buf& b, float t0, float dur, float f, float amp, float att, float rel, float fcStart, float fcPeak,
                    float fcSustain, float fcTau, int voices, float detuneCents, float pan = 0.f, float q = 0.9f,
                    float pitchGlideSemis = 0.f, float glideTime = 1.f) {
    int s0 = S(t0), nOn = S(dur), n = nOn + S(rel * 1.6f);
    b.ensure(s0 + n);
    voices = Clamp(voices, 1, 7);
    BlepOsc osc[7];
    float det[7], vpan[7];
    for (int v = 0; v < voices; v++) {
        osc[v].phase = b.rng.f();
        float d = voices > 1 ? ((float)v / (float)(voices - 1) - 0.5f) * 2.f : 0.f;
        det[v] = semis(d * detuneCents / 100.f);
        vpan[v] = d;
    }
    Svf fl, fr;
    float atk = 1.f / Max(1.f, att * SR);
    float relK = expf(-4.6f / Max(rel * SR, 1.f));  // -40 dB after `rel` seconds
    float e = 0.f;
    float fc = fcStart;
    float fcK = expf(-1.f / Max(fcTau * SR, 1.f));
    float gpl, gpr;
    panG(pan, gpl, gpr);
    float norm = 1.f / sqrtf((float)voices);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        if (i < nOn) e = Min(1.f, e + atk);
        else e *= relK;
        // cutoff: attack sweep to peak then decay to sustain
        float target = t < att + 0.02f ? fcPeak : fcSustain;
        fc = target + (fc - target) * fcK;
        if ((i & 15) == 0) {
            float g = svfG(fc);
            fl.setG(g, q);
            fr.setG(g, q);
        }
        float glide = pitchGlideSemis != 0.f ? semis(pitchGlideSemis * Min(1.f, t / glideTime)) : 1.f;
        float sl = 0.f, sr = 0.f;
        for (int v = 0; v < voices; v++) {
            float dt = f * det[v] * glide / SR;
            float s = osc[v].saw(Min(dt, 0.45f));
            float pl = 0.5f - 0.35f * vpan[v], pr = 0.5f + 0.35f * vpan[v];
            sl += s * pl;
            sr += s * pr;
        }
        float yl = fl.lp(sl * norm), yr = fr.lp(sr * norm);
        if (b.stereo) {
            b.L[(size_t)(s0 + i)] += yl * e * amp * gpl;
            b.R[(size_t)(s0 + i)] += yr * e * amp * gpr;
        } else {
            b.L[(size_t)(s0 + i)] += 0.5f * (yl + yr) * e * amp;
        }
        if (i >= nOn && e < 1e-4f) break;
    }
}

// Mechanical clack (gun actions, latches).
static void mechClack(Buf& b, float t0, float amp, float pitch = 1.f, float pan = 0.f) {
    Mode m[5] = {{2100.f * pitch * b.rnd(0.9f, 1.1f), 0.012f, 1.f},
                 {3700.f * pitch * b.rnd(0.9f, 1.1f), 0.009f, 0.8f},
                 {5600.f * pitch * b.rnd(0.9f, 1.1f), 0.007f, 0.6f},
                 {8200.f * pitch * b.rnd(0.9f, 1.1f), 0.005f, 0.4f},
                 {1250.f * pitch * b.rnd(0.9f, 1.1f), 0.015f, 0.5f}};
    modes(b, t0, amp * 0.5f, m, 5, pan);
    click(b, t0, amp * 0.6f, 3, false, pan);
    noise(b, t0, 0.02f, amp * 0.4f, 0.0002f, 0.003f, FHP, 3000.f, -1.f, 0.1f, 0.7f, 0, pan);
}

// Musical helpers for stingers
static void kickDrum(Buf& b, float t0, float amp, float pan = 0.f) {
    tone(b, t0, 0.5f, 160.f, 45.f, 0.035f, amp, 0.001f, 0.16f, pan, 0.1f);
    click(b, t0, amp * 0.5f, 6, false, pan);
}
static void snareDrum(Buf& b, float t0, float amp, float pan = 0.f) {
    tone(b, t0, 0.2f, 240.f, 180.f, 0.02f, amp * 0.5f, 0.001f, 0.05f, pan);
    noise(b, t0, 0.35f, amp * 0.8f, 0.001f, 0.09f, FHP, 1800.f, -1.f, 0.1f, 0.7f, 0, pan);
    noise(b, t0, 0.2f, amp * 0.4f, 0.001f, 0.05f, FBP, 3500.f, -1.f, 0.1f, 0.9f, 0, pan);
}
static void crashCym(Buf& b, float t0, float amp, float pan = 0.f) {
    noise(b, t0, 2.8f, amp * 0.5f, 0.002f, 0.9f, FHP4, 5500.f, 4000.f, 0.8f, 0.7f, 0, pan);
    Mode m[8];
    for (int i = 0; i < 8; i++) m[i] = {b.rnd(3000.f, 9000.f), b.rnd(0.3f, 1.2f), b.rnd(0.05f, 0.2f)};
    modes(b, t0, amp * 0.3f, m, 8, pan);
}
static void subHit(Buf& b, float t0, float amp, float f0 = 70.f, float f1 = 32.f, float tau = 0.5f) {
    tone(b, t0, tau * 5.f, f0, f1, tau * 0.6f, amp, 0.003f, tau, 0.f, 0.15f);
    noise(b, t0, tau * 3.f, amp * 0.5f, 0.002f, tau * 0.6f, FLP, 500.f, 120.f, tau * 0.5f, 0.7f, 2);
}
static void bellNote(Buf& b, float t0, float midi, float amp, float tau = 0.6f, float pan = 0.f) {
    float f = midiToHz(midi);
    fmNote(b, t0, tau * 6.f, f, 3.5f, 2.2f, tau * 0.25f, amp, 0.001f, tau, pan);
    fmNote(b, t0, tau * 4.f, f * 2.f, 1.f, 0.6f, tau * 0.2f, amp * 0.25f, 0.001f, tau * 0.6f, pan);
}
static void marimba(Buf& b, float t0, float midi, float amp, float pan = 0.f) {
    float f = midiToHz(midi);
    Mode m[3] = {{f, 0.28f, 1.f}, {f * 3.93f, 0.05f, 0.35f}, {f * 9.9f, 0.015f, 0.12f}};
    modes(b, t0, amp, m, 3, pan);
    noise(b, t0, 0.02f, amp * 0.15f, 0.0005f, 0.003f, FBP, f * 2.f, -1.f, 0.1f, 2.f, 0, pan);
}

// ---------------------------------------------------------------------------------------------
// Footsteps
enum Surface { SurfConcrete, SurfGrass, SurfWood, SurfMetal, SurfSand, SurfWater, SurfGravel };
static void s_step(Buf& b, int surf) {
    float toe = b.rnd(0.045f, 0.085f);
    for (int k = 0; k < 2; k++) {
        float t0 = k == 0 ? 0.f : toe;
        float a = k == 0 ? 1.f : b.rnd(0.35f, 0.6f);
        float p = b.rnd(0.88f, 1.12f);
        switch (surf) {
            case SurfConcrete:
                click(b, t0, 0.5f * a, 3);
                noise(b, t0, 0.07f, 0.9f * a, 0.0004f, 0.011f, FBP, 3200.f * p, 2400.f * p, 0.04f, 0.9f);
                noise(b, t0, 0.06f, 0.8f * a, 0.0003f, 0.008f, FLP, 1100.f * p, 400.f, 0.02f, 0.7f);
                tone(b, t0, 0.06f, 140.f * p, 85.f, 0.01f, 0.35f * a, 0.0005f, 0.012f);
                if (k == 1) noise(b, t0 + 0.01f, 0.09f, 0.18f * a, 0.012f, 0.025f, FHP, 2800.f, -1.f, 0.1f, 0.7f);
                break;
            case SurfGrass:
                noise(b, t0, 0.18f, 0.5f * a, 0.006f, 0.04f, FBP, 3800.f * p, 2600.f, 0.08f, 0.6f);
                grains(b, t0, 0.12f, 260.f, 0.25f * a, 2500.f, 7000.f, 0.001f, 0.004f, 0.06f);
                noise(b, t0, 0.08f, 0.5f * a, 0.003f, 0.02f, FLP, 280.f * p, -1.f, 0.1f, 0.7f);
                break;
            case SurfWood: {
                click(b, t0, 0.4f * a, 4);
                Mode m[4] = {{180.f * p, 0.045f, 1.f}, {395.f * p, 0.03f, 0.7f}, {660.f * p, 0.022f, 0.5f}, {1150.f * p, 0.012f, 0.35f}};
                modes(b, t0, 0.8f * a, m, 4);
                noise(b, t0, 0.05f, 0.4f * a, 0.0004f, 0.008f, FBP, 1800.f * p, -1.f, 0.1f, 1.f);
                if (k == 1 && b.chance(0.3f)) {  // creak
                    fmNote(b, t0 + 0.02f, 0.12f, b.rnd(700.f, 1100.f), 1.01f, 3.f, 0.05f, 0.05f, 0.02f, 0.04f);
                }
                break;
            }
            case SurfMetal: {
                click(b, t0, 0.4f * a, 3);
                Mode m[6] = {{420.f * p, 0.18f, 0.6f}, {1130.f * p, 0.14f, 0.5f}, {1840.f * p, 0.1f, 0.45f},
                             {2650.f * p, 0.08f, 0.4f}, {3900.f * p, 0.05f, 0.3f}, {5300.f * p, 0.03f, 0.25f}};
                modes(b, t0, 0.6f * a, m, 6);
                noise(b, t0, 0.05f, 0.5f * a, 0.0003f, 0.007f, FLP, 900.f, -1.f, 0.1f, 0.7f);
                grains(b, t0 + 0.01f, 0.1f, 60.f, 0.08f * a, 1500.f, 4000.f, 0.005f, 0.02f);
                break;
            }
            case SurfSand:
                noise(b, t0, 0.2f, 0.7f * a, 0.018f, 0.045f, FLP, 1600.f * p, 900.f, 0.08f, 0.6f, 1);
                noise(b, t0, 0.15f, 0.25f * a, 0.01f, 0.035f, FBP, 4200.f * p, -1.f, 0.1f, 0.7f);
                grains(b, t0, 0.1f, 120.f, 0.08f * a, 3000.f, 8000.f, 0.001f, 0.003f);
                break;
            case SurfWater: {
                noise(b, t0, 0.25f, 0.6f * a, 0.004f, 0.05f, FBP, 2600.f * p, 1600.f, 0.1f, 0.7f);
                noise(b, t0, 0.2f, 0.6f * a, 0.006f, 0.05f, FLP, 420.f * p, -1.f, 0.1f, 0.7f, 1);
                int nb = b.irnd(2, 5);
                for (int j = 0; j < nb; j++) {
                    float f0 = b.rnd(500.f, 1300.f);
                    tone(b, t0 + b.rnd(0.01f, 0.12f), 0.05f, f0, f0 * b.rnd(1.6f, 2.4f), 0.015f, 0.18f * a, 0.001f, 0.012f);
                }
                break;
            }
            case SurfGravel:
                grains(b, t0, 0.14f, 700.f, 0.3f * a, 1200.f, 6500.f, 0.0015f, 0.006f, 0.07f);
                noise(b, t0, 0.12f, 0.45f * a, 0.002f, 0.03f, FBP, 2200.f * p, 1500.f, 0.05f, 0.7f);
                noise(b, t0, 0.07f, 0.45f * a, 0.002f, 0.015f, FLP, 350.f, -1.f, 0.1f, 0.7f);
                break;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Firearms: see sfx_guns.cpp (layered at runtime by the mixer). Rocket launcher:
static void s_weapon(Buf& b, int id) {
    switch (id) {
        case SFX_ROCKET_LAUNCH: {
            tone(b, 0.f, 0.3f, 110.f, 50.f, 0.03f, 0.9f, 0.001f, 0.04f);
            noise(b, 0.f, 0.3f, 1.f, 0.001f, 0.06f, FBP, 1500.f, 900.f, 0.05f, 0.7f);
            // sustained rocket roar with flutter, receding
            int s0 = S(0.02f), n = S(1.6f);
            b.ensure(s0 + n);
            Svf bp1, bp2;
            float fc = 1900.f, fl = 1.f;
            for (int i = 0; i < n; i++) {
                float t = (float)i / SR;
                if ((i & 15) == 0) {
                    fc = 700.f + 1200.f * expf(-t * 1.6f);
                    bp1.setG(svfG(fc), 0.6f);
                    bp2.setG(svfG(fc * 2.6f), 0.8f);
                    fl = 0.7f + 0.6f * b.nz.uni();
                }
                float env = Min(1.f, t / 0.03f) * expf(-t * 1.5f);
                float w = b.nz.white();
                b.L[(size_t)(s0 + i)] += (bp1.bp(w) * 0.9f + bp2.bp(w) * 0.4f) * env * fl * 0.9f;
            }
            noise(b, 0.02f, 1.2f, 0.4f, 0.02f, 0.4f, FLP, 400.f, 150.f, 0.5f, 0.7f, 2);
            reverb(b, 1.5f, 0.2f, 0.6f, 0.01f, 1.3f, 0.3f);
            break;
        }
    }
}

static void s_reload(Buf& b) {
    float j = b.rnd(0.9f, 1.1f);
    mechClack(b, 0.f, 0.4f, 1.1f);
    noise(b, 0.04f, 0.18f, 0.12f, 0.03f, 0.05f, FBP, 2500.f, 1800.f, 0.1f, 1.f);
    float tIn = 0.42f * j;
    noise(b, tIn, 0.05f, 0.6f, 0.0005f, 0.012f, FLP, 700.f, -1.f, 0.1f, 0.7f);
    mechClack(b, tIn + 0.004f, 0.55f, 0.6f);
    float tRack = 0.7f * j;
    noise(b, tRack, 0.09f, 0.15f, 0.04f, 0.03f, FBP, 3200.f, 2500.f, 0.05f, 1.2f);
    mechClack(b, tRack + 0.07f, 0.5f, 0.95f);
    mechClack(b, tRack + 0.16f * j, 0.7f, 1.05f);
}
static void s_dryFire(Buf& b) {
    // trigger pull (sear) then hammer fall on an empty chamber
    Mode s1[2] = {{b.rnd(4200.f, 4800.f), 0.004f, 1.f}, {b.rnd(7000.f, 7800.f), 0.003f, 0.6f}};
    modes(b, 0.f, 0.3f, s1, 2);
    float th = b.rnd(0.035f, 0.05f);
    Mode m[4] = {{b.rnd(3000.f, 3500.f), 0.012f, 1.f}, {b.rnd(5500.f, 6200.f), 0.009f, 0.7f}, {b.rnd(7600.f, 8400.f), 0.006f, 0.5f},
                 {b.rnd(1400.f, 1700.f), 0.02f, 0.6f}};
    modes(b, th, 0.8f, m, 4);
    click(b, th, 0.6f, 3);
    noise(b, th, 0.05f, 0.5f, 0.0003f, 0.008f, FLP, 900.f, -1.f, 0.1f, 0.7f);
    tone(b, th, 0.05f, 220.f, 140.f, 0.01f, 0.25f, 0.0005f, 0.01f);
}
static void s_weaponSwitch(Buf& b) {
    int s0 = 0, n = S(0.32f);
    b.ensure(n);
    Svf bp;
    bp.set(1500.f, 0.8f);
    float bump = 0.f;
    for (int i = 0; i < n; i++) {
        if ((i & 255) == 0) bump = 0.3f + 0.7f * b.nz.uni();
        float t = (float)i / SR;
        float env = sinf(kPi * t / 0.32f);
        b.L[(size_t)(s0 + i)] += bp.bp(b.nz.white()) * env * bump * 0.25f;
    }
    Mode m[3] = {{b.rnd(2200.f, 2500.f), 0.04f, 1.f}, {b.rnd(4300.f, 4600.f), 0.03f, 0.6f}, {b.rnd(6800.f, 7200.f), 0.02f, 0.4f}};
    modes(b, 0.17f, 0.35f, m, 3);
    mechClack(b, 0.2f, 0.3f, 0.8f);
}
static void s_shellCasing(Buf& b) {
    float f1 = b.rnd(3400.f, 4600.f);
    float t = 0.f, a = 1.f;
    int bounces = b.irnd(3, 4);
    for (int k = 0; k < bounces; k++) {
        Mode m[4] = {{f1, b.rnd(0.05f, 0.12f), 1.f}, {f1 * 1.58f, 0.07f, 0.6f}, {f1 * 2.24f, 0.05f, 0.4f}, {f1 * 2.93f, 0.035f, 0.3f}};
        modes(b, t, a * 0.5f, m, 4);
        click(b, t, a * 0.3f, 2);
        t += b.rnd(0.06f, 0.11f) * (1.f - 0.2f * (float)k);
        a *= b.rnd(0.4f, 0.6f);
    }
    grains(b, t, 0.25f, 30.f, 0.05f, f1 * 0.9f, f1 * 1.6f, 0.01f, 0.03f);
}
static void s_bulletWhiz(Buf& b) {
    click(b, 0.f, 0.6f, 3, true);
    float f0 = b.rnd(3000.f, 4200.f), f1 = b.rnd(1000.f, 1500.f);
    noise(b, 0.f, 0.4f, 0.9f, 0.012f, 0.07f, FBP, f0, f1, 0.08f, 3.f);
    tone(b, 0.f, 0.35f, f0 * 0.75f, f1 * 0.9f, 0.08f, 0.12f, 0.015f, 0.06f);
}

// Bullet / projectile impacts
static void s_impact(Buf& b, int id) {
    float p = b.rnd(0.88f, 1.12f);
    switch (id) {
        case SFX_IMPACT_CONCRETE:
            click(b, 0.f, 0.8f, 3);
            noise(b, 0.f, 0.06f, 1.f, 0.0002f, 0.005f, FHP, 1800.f * p, -1.f, 0.1f, 0.7f);
            noise(b, 0.f, 0.08f, 0.6f, 0.0003f, 0.012f, FLP, 1600.f * p, 600.f, 0.02f, 0.7f);
            tone(b, 0.f, 0.06f, 180.f * p, 110.f, 0.01f, 0.4f, 0.0005f, 0.01f);
            grains(b, 0.005f, 0.3f, 180.f, 0.18f, 1800.f, 7000.f, 0.001f, 0.004f, 0.08f);
            break;
        case SFX_IMPACT_METAL: {
            click(b, 0.f, 0.7f, 3);
            Mode m[6];
            for (int i = 0; i < 6; i++) m[i] = {b.rnd(900.f, 5200.f) * p, b.rnd(0.04f, 0.22f), b.rnd(0.3f, 1.f)};
            modes(b, 0.f, 0.45f, m, 6);
            noise(b, 0.f, 0.04f, 0.6f, 0.0002f, 0.004f, FHP, 2500.f, -1.f, 0.1f, 0.7f);
            if (b.chance(0.4f)) {  // ricochet whine
                float f0 = b.rnd(2800.f, 3800.f);
                tone(b, 0.03f, 0.5f, f0, f0 * 0.6f, 0.25f, 0.25f, 0.01f, 0.12f);
                noise(b, 0.03f, 0.4f, 0.15f, 0.01f, 0.1f, FBP, f0, f0 * 0.6f, 0.25f, 4.f);
            }
            break;
        }
        case SFX_IMPACT_GLASS: {
            click(b, 0.f, 0.7f, 2);
            noise(b, 0.f, 0.05f, 0.7f, 0.0002f, 0.006f, FHP, 3500.f, -1.f, 0.1f, 0.7f);
            Mode m[16];
            for (int i = 0; i < 16; i++) m[i] = {b.rnd(2200.f, 11000.f), b.rnd(0.01f, 0.12f), b.rnd(0.2f, 1.f)};
            modes(b, 0.f, 0.3f, m, 16);
            grains(b, 0.02f, 0.4f, 70.f, 0.15f, 3000.f, 10000.f, 0.004f, 0.03f, 0.15f);
            break;
        }
        case SFX_IMPACT_FLESH:
            tone(b, 0.f, 0.15f, 110.f * p, 60.f, 0.02f, 0.8f, 0.001f, 0.025f);
            noise(b, 0.f, 0.15f, 1.f, 0.0008f, 0.025f, FLP, 500.f * p, 250.f, 0.03f, 0.7f, 1);
            noise(b, 0.f, 0.06f, 0.35f, 0.0003f, 0.006f, FBP, 1600.f * p, -1.f, 0.1f, 0.9f);
            noise(b, 0.01f, 0.12f, 0.25f, 0.005f, 0.03f, FBP, 700.f * p, 400.f, 0.05f, 1.5f);
            break;
        case SFX_IMPACT_DIRT:
            tone(b, 0.f, 0.12f, 120.f * p, 70.f, 0.02f, 0.6f, 0.001f, 0.02f);
            noise(b, 0.f, 0.15f, 0.9f, 0.001f, 0.025f, FLP, 700.f * p, 300.f, 0.04f, 0.7f, 1);
            noise(b, 0.005f, 0.25f, 0.4f, 0.005f, 0.06f, FLP, 2400.f * p, 1200.f, 0.1f, 0.6f);
            grains(b, 0.02f, 0.3f, 120.f, 0.08f, 1500.f, 5000.f, 0.001f, 0.004f, 0.1f);
            break;
        case SFX_IMPACT_WATER:
            tone(b, 0.f, 0.08f, 350.f * p, 1000.f * p, 0.02f, 0.35f, 0.001f, 0.02f);
            noise(b, 0.f, 0.25f, 0.8f, 0.002f, 0.06f, FBP, 2600.f * p, 1800.f, 0.08f, 0.6f);
            noise(b, 0.f, 0.15f, 0.4f, 0.002f, 0.03f, FLP, 500.f, -1.f, 0.1f, 0.7f);
            for (int j = 0; j < 4; j++) {
                float f0 = b.rnd(700.f, 1800.f);
                tone(b, b.rnd(0.03f, 0.25f), 0.04f, f0, f0 * 1.8f, 0.01f, 0.08f, 0.001f, 0.01f);
            }
            break;
        case SFX_IMPACT_WOOD: {
            click(b, 0.f, 0.6f, 3);
            Mode m[5] = {{b.rnd(220.f, 300.f), 0.04f, 1.f}, {b.rnd(520.f, 680.f), 0.03f, 0.8f}, {b.rnd(900.f, 1150.f), 0.02f, 0.6f},
                         {b.rnd(1500.f, 1800.f), 0.012f, 0.4f}, {b.rnd(2600.f, 3100.f), 0.008f, 0.3f}};
            modes(b, 0.f, 0.7f, m, 5);
            noise(b, 0.f, 0.05f, 0.5f, 0.0003f, 0.006f, FBP, 2000.f * p, -1.f, 0.1f, 0.9f);
            crackles(b, 0.005f, 0.12f, 120.f, 0.2f, 2000.f, 7000.f, 0.04f);
            break;
        }
    }
}

// Explosions
static void s_explosion(Buf& b, bool big) {
    float sc = big ? 1.f : 0.6f;
    float v = b.rnd(0.85f, 1.15f);
    click(b, 0.f, 1.f, 8, true);
    noise(b, 0.f, 0.4f, 1.1f, 0.0005f, 0.05f * sc, FLP, 7000.f, 1500.f, 0.1f, 0.7f);
    tone(b, 0.f, 3.f * sc, 75.f * v, 28.f, 0.3f * sc, 1.3f, 0.004f, 0.7f * sc, 0.f, 0.25f);
    noise(b, 0.f, 3.5f * sc, 1.1f, 0.008f, 1.0f * sc, FLP4, 320.f, 110.f, 0.6f * sc, 0.7f, 2);
    noise(b, 0.f, 2.5f * sc, 0.7f, 0.015f, 0.5f * sc, FLP, 2600.f, 400.f, 0.6f * sc, 0.7f, 1);
    // debris, crackle, falling bits
    grains(b, 0.25f, 2.8f * sc, big ? 35.f : 18.f, 0.12f, 700.f, 6000.f, 0.005f, 0.04f, 0.9f * sc, 0.f, 0.f);
    crackles(b, 0.1f, 2.5f * sc, big ? 28.f : 14.f, 0.35f, 800.f, 5000.f, 1.f * sc);
    if (big) {
        Mode m[5];
        for (int i = 0; i < 5; i++) m[i] = {b.rnd(400.f, 2500.f), b.rnd(0.1f, 0.4f), b.rnd(0.3f, 1.f)};
        modes(b, b.rnd(0.6f, 1.2f), 0.08f, m, 5);
        modes(b, b.rnd(1.3f, 2.0f), 0.05f, m, 3);
    }
    // rolling rumble tail with slow swells
    int nsw = big ? 4 : 2;
    for (int i = 0; i < nsw; i++)
        noise(b, b.rnd(0.2f, 1.2f) * sc, 3.5f * sc, 0.35f, b.rnd(0.2f, 0.5f), b.rnd(0.6f, 1.3f) * sc, FLP, 110.f, 70.f, 1.f, 0.7f, 2);
    reverb(b, big ? 2.6f : 1.7f, 0.22f, 0.7f, 0.02f, 1.6f, 0.3f);
    saturate(b, 1.6f);
}
static void s_grenadeBounce(Buf& b) {
    float t = 0.f, a = 1.f;
    int nb = b.irnd(2, 3);
    for (int k = 0; k < nb; k++) {
        Mode m[4] = {{b.rnd(1000.f, 1200.f), 0.07f, 1.f}, {b.rnd(2500.f, 2700.f), 0.05f, 0.7f}, {b.rnd(4100.f, 4400.f), 0.035f, 0.5f}, {b.rnd(6000.f, 6400.f), 0.02f, 0.3f}};
        modes(b, t, a * 0.4f, m, 4);
        noise(b, t, 0.05f, a * 0.6f, 0.0004f, 0.01f, FLP, 600.f, -1.f, 0.1f, 0.7f);
        click(b, t, a * 0.4f, 3);
        t += b.rnd(0.22f, 0.32f) * (1.f - 0.35f * (float)k);
        a *= 0.5f;
    }
}

// Melee / bodies
static void s_punch(Buf& b, bool kick) {
    float p = b.rnd(0.9f, 1.1f);
    float sc = kick ? 1.25f : 1.f;
    tone(b, 0.f, 0.2f, (kick ? 95.f : 120.f) * p, kick ? 45.f : 60.f, 0.02f, 0.9f, 0.001f, 0.025f * sc);
    noise(b, 0.f, 0.2f, 1.f, 0.0006f, 0.03f * sc, FLP, (kick ? 320.f : 420.f) * p, 200.f, 0.03f, 0.7f, 1);
    noise(b, 0.f, 0.05f, kick ? 0.35f : 0.5f, 0.0002f, 0.006f, FHP, 2000.f * p, -1.f, 0.1f, 0.7f);
    noise(b, 0.005f, 0.12f, 0.2f, 0.003f, 0.04f, FBP, 850.f * p, 500.f, 0.05f, 1.2f);
    // cloth rustle of the swing
    noise(b, 0.f, 0.15f, 0.08f, 0.01f, 0.05f, FBP, 1800.f, -1.f, 0.1f, 0.8f);
}
// Air swing of an arm, bat or blade: band-passed noise sweeping up in pitch with a swell-and-fade envelope.
static void s_whoosh(Buf& b) {
    float p = b.rnd(0.85f, 1.2f);
    noise(b, 0.f, 0.26f, 0.5f, 0.07f, 0.05f, FBP, 480.f * p, 1500.f * p, 0.06f, 1.4f);
    noise(b, 0.03f, 0.2f, 0.22f, 0.05f, 0.04f, FBP, 1300.f * p, 3000.f * p, 0.05f, 1.1f);
}
static void s_bodyFall(Buf& b) {
    tone(b, 0.f, 0.3f, 75.f, 40.f, 0.04f, 0.8f, 0.002f, 0.05f);
    noise(b, 0.f, 0.3f, 1.f, 0.002f, 0.06f, FLP, 260.f, -1.f, 0.1f, 0.7f, 1);
    float t2 = b.rnd(0.09f, 0.16f);
    tone(b, t2, 0.2f, 95.f, 55.f, 0.03f, 0.35f, 0.002f, 0.04f);
    noise(b, t2, 0.2f, 0.45f, 0.002f, 0.04f, FLP, 350.f, -1.f, 0.1f, 0.7f, 1);
    int n = S(0.55f);
    b.ensure(n);
    Svf bp;
    bp.set(1800.f, 0.7f);
    float bump = 0.f;
    for (int i = 0; i < n; i++) {
        if ((i & 511) == 0) bump = b.nz.uni();
        float t = (float)i / SR;
        b.L[(size_t)i] += bp.bp(b.nz.white()) * 0.14f * bump * expf(-t * 5.f);
    }
    if (b.chance(0.5f)) grains(b, 0.02f, 0.3f, 25.f, 0.05f, 2000.f, 5000.f, 0.01f, 0.04f);
}

// Vocalizations (formant synthesis)
static void s_grunt(Buf& b, bool female) {
    int type = b.irnd(0, 4);
    float fs = female ? 1.16f : 1.f;
    float f0 = (female ? b.rnd(200.f, 245.f) : b.rnd(95.f, 130.f));
    float dur = b.rnd(0.18f, 0.34f);
    static const float kV[5][3] = {{640, 1190, 2390}, {620, 1200, 2500}, {800, 1300, 2550}, {320, 870, 2250}, {280, 1900, 2600}};
    const float* V = kV[type];
    float rough = type == 1 ? 0.4f : b.rnd(0.05f, 0.2f);
    float breathOn = (type == 2) ? 0.6f : 0.2f;
    vocal(b, 0.f, dur, 0.012f, [&](float u, VFrame& fr) {
        fr.f0 = f0 * (u < 0.15f ? 1.f + 0.12f * (u / 0.15f) : 1.12f - 0.38f * (u - 0.15f));
        fr.amp = (u < 0.06f ? u / 0.06f : 1.f) * (u > 0.7f ? Max(0.f, (1.f - u) / 0.3f) : 1.f);
        fr.breath = u < 0.1f ? breathOn : 0.08f + 0.2f * u;
        fr.bright = type == 4 ? 0.2f : 0.75f;
        fr.rough = rough + (u > 0.7f ? 0.3f : 0.f);
        for (int k = 0; k < 3; k++) fr.F[k] = V[k] * fs;
        fr.F[3] = 3400.f * fs;
        fr.BW[0] = type == 4 ? 60.f : 90.f;
    });
    lowpass(b, 6000.f);
}
static void s_scream(Buf& b, bool female) {
    float fs = female ? 1.17f : 1.f;
    float base = female ? b.rnd(520.f, 700.f) : b.rnd(260.f, 360.f);
    float dur = b.rnd(0.8f, 1.5f);
    float vibR = b.rnd(5.f, 7.f);
    float peak = b.rnd(1.15f, 1.35f);
    vocal(b, 0.f, dur, 0.02f, [&](float u, VFrame& fr) {
        float t = u * dur;
        float contour = u < 0.2f ? 1.f + (peak - 1.f) * (u / 0.2f) : peak - (peak - 0.8f) * ((u - 0.2f) / 0.8f) * ((u - 0.2f) / 0.8f);
        fr.f0 = base * contour * (1.f + 0.025f * sinf(kTwoPi * vibR * t));
        fr.amp = Min(1.f, u / 0.05f) * (u > 0.75f ? (1.f - u) / 0.25f : 1.f);
        fr.breath = 0.18f;
        fr.bright = 0.95f;
        fr.rough = 0.35f;
        fr.F[0] = 850.f * fs; fr.F[1] = 1450.f * fs; fr.F[2] = 2800.f * fs; fr.F[3] = 3700.f * fs;
        fr.BW[0] = 110.f; fr.BW[1] = 120.f; fr.BW[2] = 160.f; fr.BW[3] = 220.f;
    });
    reverb(b, 0.8f, 0.08f, 0.5f, 0.01f, 0.8f, 0.3f);
}

// ---------------------------------------------------------------------------------------------
// Vehicles
static void s_carDoorOpen(Buf& b) {
    mechClack(b, 0.f, 0.3f, 0.8f);
    float t1 = b.rnd(0.05f, 0.08f);
    Mode m[3] = {{b.rnd(550.f, 650.f), 0.03f, 1.f}, {b.rnd(1400.f, 1600.f), 0.02f, 0.6f}, {b.rnd(3000.f, 3400.f), 0.012f, 0.4f}};
    modes(b, t1, 0.5f, m, 3);
    noise(b, t1, 0.06f, 0.6f, 0.0005f, 0.015f, FLP, 1200.f, -1.f, 0.1f, 0.7f);
    noise(b, t1 + 0.02f, 0.25f, 0.18f, 0.02f, 0.06f, FBP, 800.f, 500.f, 0.1f, 0.8f);
    if (b.chance(0.4f)) fmNote(b, t1 + 0.1f, 0.25f, b.rnd(850.f, 1200.f), 1.003f, 4.f, 0.2f, 0.04f, 0.04f, 0.1f);
}
static void s_carDoorClose(Buf& b) {
    float p = b.rnd(0.9f, 1.1f);
    noise(b, 0.f, 0.3f, 1.f, 0.001f, 0.05f, FLP, 320.f * p, -1.f, 0.1f, 0.7f, 1);
    tone(b, 0.f, 0.35f, 78.f * p, 55.f, 0.05f, 0.9f, 0.002f, 0.06f);
    Mode m[4] = {{130.f * p, 0.12f, 1.f}, {212.f * p, 0.1f, 0.7f}, {338.f * p, 0.08f, 0.5f}, {520.f * p, 0.06f, 0.35f}};
    modes(b, 0.f, 0.3f, m, 4);
    Mode l[2] = {{b.rnd(2300.f, 2600.f), 0.015f, 1.f}, {b.rnd(4000.f, 4300.f), 0.01f, 0.6f}};
    modes(b, 0.012f, 0.35f, l, 2);
    click(b, 0.012f, 0.3f, 3);
    grains(b, 0.03f, 0.18f, 50.f, 0.05f, 1500.f, 4000.f, 0.005f, 0.02f, 0.08f);
}
static void s_carCrash(Buf& b, bool heavy) {
    float sc = heavy ? 1.f : 0.55f;
    tone(b, 0.f, 0.6f * (heavy ? 2.f : 1.f), heavy ? 62.f : 82.f, heavy ? 30.f : 45.f, 0.06f, 1.1f, 0.002f, heavy ? 0.1f : 0.06f);
    noise(b, 0.f, 0.5f, 1.f, 0.001f, heavy ? 0.08f : 0.05f, FLP, 600.f, 250.f, 0.05f, 0.7f, 1);
    click(b, 0.f, 0.8f, 6, true);
    // crunch: dense grains + gritty noise
    grains(b, 0.f, heavy ? 0.6f : 0.25f, heavy ? 500.f : 400.f, 0.2f, 700.f, 6000.f, 0.002f, 0.012f, heavy ? 0.3f : 0.12f);
    noise(b, 0.f, heavy ? 0.9f : 0.4f, 0.55f, 0.002f, heavy ? 0.2f : 0.1f, FBP, 2200.f, 1200.f, 0.2f, 0.7f);
    crackles(b, 0.f, 0.3f * (heavy ? 2.f : 1.f), 90.f, 0.3f, 1500.f, 6000.f, 0.15f);
    Mode m[7];
    for (int i = 0; i < 7; i++) m[i] = {b.rnd(heavy ? 180.f : 300.f, heavy ? 1800.f : 2600.f), b.rnd(0.12f, 0.5f) * (heavy ? 1.4f : 1.f), b.rnd(0.3f, 1.f)};
    modes(b, 0.f, 0.22f, m, 7);
    if (heavy || b.chance(0.4f)) {
        Mode g[14];
        for (int i = 0; i < 14; i++) g[i] = {b.rnd(2500.f, 10000.f), b.rnd(0.01f, 0.1f), b.rnd(0.2f, 1.f)};
        modes(b, 0.01f, heavy ? 0.12f : 0.07f, g, 14);
        grains(b, 0.05f, 0.9f * sc + 0.2f, 45.f, 0.08f, 3000.f, 9000.f, 0.005f, 0.03f, 0.3f);
    }
    if (heavy) {
        // scrape / settle
        noise(b, 0.15f, 1.2f, 0.25f, 0.05f, 0.35f, FBP, 1500.f, 900.f, 0.5f, 2.f);
        grains(b, 0.3f, 1.2f, 20.f, 0.06f, 600.f, 3000.f, 0.02f, 0.08f, 0.5f);
    }
    reverb(b, heavy ? 1.2f : 0.8f, 0.1f, 0.6f, 0.01f, 1.1f, 0.3f);
    saturate(b, 1.4f);
}
static void s_glassBreak(Buf& b) {
    click(b, 0.f, 0.8f, 3);
    noise(b, 0.f, 0.08f, 0.9f, 0.0002f, 0.01f, FHP, 3000.f, -1.f, 0.1f, 0.7f);
    Mode m[40];
    for (int i = 0; i < 40; i++) m[i] = {b.rnd(1800.f, 12000.f), b.rnd(0.015f, 0.25f), b.rnd(0.1f, 1.f)};
    modes(b, 0.f, 0.14f, m, 40);
    grains(b, 0.02f, 1.1f, 90.f, 0.12f, 2500.f, 10000.f, 0.006f, 0.05f, 0.35f);
    float tg = b.rnd(0.3f, 0.5f);
    grains(b, tg, 0.7f, 70.f, 0.09f, 2000.f, 9000.f, 0.004f, 0.04f, 0.25f);
    noise(b, tg, 0.3f, 0.12f, 0.005f, 0.08f, FHP, 4000.f, -1.f, 0.1f, 0.7f);
}

static void s_engineStart(Buf& b) {
    int n = S(2.4f);
    b.ensure(n);
    // starter motor: geared whine with compression-stroke modulation
    float crank = b.rnd(0.55f, 0.9f);
    float ph = 0.f, cph = 0.f;
    Svf bp;
    bp.set(1400.f, 1.f);
    for (int i = 0; i < S(crank + 0.05f); i++) {
        float t = (float)i / SR;
        cph += 9.5f / SR;
        float comp = 0.75f + 0.25f * sinCycle(cph);
        float f = 175.f * comp;
        ph += f / SR;
        ph -= floorf(ph);
        float s = (ph < 0.5f ? 1.f : -1.f) * 0.3f + sinWrapped(ph) * 0.5f;
        float env = Min(1.f, t / 0.02f) * (t > crank ? Max(0.f, 1.f - (t - crank) / 0.05f) : 1.f);
        b.L[(size_t)i] += (s * 0.3f + bp.bp(b.nz.white()) * 0.25f) * env * comp;
    }
    // engine catches
    emit::EngineSpec spec = emit::kEngineSpecs[b.chance(0.5f) ? ENGINE_I4 : ENGINE_V6];
    float realIdle = spec.idleRpm;
    spec.idleRpm = 200.f;
    emit::EngineCore core;
    core.init(spec, (u32)b.rng.next());
    float idle01 = (realIdle - 200.f) / (spec.maxRpm - 200.f);
    float flare = idle01 * b.rnd(2.2f, 3.0f);
    float buf[64];
    int s0 = S(crank - 0.08f);
    for (int i = s0; i < n; i += 64) {
        float t = (float)(i - s0) / SR;
        float r = t < 0.12f ? 0.02f + idle01 * t / 0.12f : (t < 0.45f ? idle01 + (flare - idle01) * SmoothStep(0.12f, 0.35f, t)
                                                                      : idle01 + (flare - idle01) * expf(-(t - 0.45f) * 3.f));
        float thr = t < 0.4f ? 0.5f : 0.08f;
        core.setParams(r, thr, thr);
        int m = Min(64, n - i);
        core.render(buf, m);
        float env = Min(1.f, t / 0.1f);
        for (int k = 0; k < m; k++) b.L[(size_t)(i + k)] += buf[k] * env * 1.6f;
    }
    // fade tail so the loop emitter can take over
    for (int i = S(1.9f); i < n; i++) b.L[(size_t)i] *= Max(0.f, 1.f - (float)(i - S(1.9f)) / (float)S(0.5f));
}
static void s_engineStop(Buf& b) {
    int n = S(1.2f);
    b.ensure(n);
    emit::EngineSpec spec = emit::kEngineSpecs[b.chance(0.5f) ? ENGINE_I4 : ENGINE_V6];
    float realIdle = spec.idleRpm;
    spec.idleRpm = 60.f;
    emit::EngineCore core;
    core.init(spec, (u32)b.rng.next());
    float idle01 = (realIdle - 60.f) / (spec.maxRpm - 60.f);
    float buf[64];
    float stopT = b.rnd(0.45f, 0.6f);
    for (int i = 0; i < n; i += 64) {
        float t = (float)i / SR;
        float r = idle01 * Max(0.f, 1.f - t / stopT);
        core.setParams(r, 0.05f, 0.1f);
        int m = Min(64, n - i);
        core.render(buf, m);
        float env = t < stopT ? 1.f : Max(0.f, 1.f - (t - stopT) / 0.08f);
        for (int k = 0; k < m; k++) b.L[(size_t)(i + k)] += buf[k] * env * 1.6f;
    }
    tone(b, stopT - 0.02f, 0.4f, 45.f, 32.f, 0.1f, 0.35f, 0.01f, 0.08f);
    noise(b, stopT - 0.02f, 0.3f, 0.25f, 0.005f, 0.05f, FLP, 300.f, -1.f, 0.1f, 0.7f);
    Mode m[2] = {{b.rnd(2500.f, 3500.f), 0.02f, 1.f}, {b.rnd(5000.f, 6000.f), 0.01f, 0.5f}};
    modes(b, stopT + b.rnd(0.2f, 0.4f), 0.04f, m, 2);
}
static void s_gearShift(Buf& b) {
    noise(b, 0.f, 0.08f, 0.8f, 0.0005f, 0.012f, FLP, 800.f, -1.f, 0.1f, 0.7f);
    tone(b, 0.f, 0.08f, 110.f, 70.f, 0.01f, 0.6f, 0.0005f, 0.015f);
    Mode m[2] = {{b.rnd(1200.f, 1400.f), 0.02f, 1.f}, {b.rnd(2700.f, 3000.f), 0.015f, 0.6f}};
    modes(b, 0.f, 0.25f, m, 2);
    mechClack(b, b.rnd(0.04f, 0.06f), 0.2f, 0.7f);
}
static void s_tirePop(Buf& b) {
    click(b, 0.f, 1.f, 5, true);
    noise(b, 0.f, 0.1f, 1.f, 0.0002f, 0.012f, FHP, 300.f, -1.f, 0.1f, 0.6f);
    tone(b, 0.f, 0.15f, 90.f, 50.f, 0.02f, 0.7f, 0.001f, 0.03f);
    noise(b, 0.002f, 0.9f, 0.55f, 0.002f, 0.2f, FLP, 8000.f, 1800.f, 0.25f, 0.7f);
    int n = S(0.6f), s0 = S(0.25f);
    b.ensure(s0 + n);
    Svf lp;
    lp.set(300.f, 0.7f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float am = 0.5f + 0.5f * sinf(kTwoPi * 12.f * t);
        b.L[(size_t)(s0 + i)] += lp.lp(b.nz.white()) * am * 0.3f * expf(-t * 4.f);
    }
    reverb(b, 0.8f, 0.1f, 0.5f, 0.01f, 1.f, 0.3f);
}
static void s_metalScrape(Buf& b) {
    float dur = b.rnd(1.0f, 1.5f);
    int n = S(dur);
    b.ensure(n);
    Svf r[3], rum;
    float rf[3] = {b.rnd(1100.f, 1400.f), b.rnd(2100.f, 2500.f), b.rnd(3400.f, 4000.f)};
    float sq = b.rnd(2800.f, 3500.f), sqPh = 0.f, stick = 0.f, sph = 0.f;
    rum.set(200.f, 0.7f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        if ((i & 255) == 0)
            for (int k = 0; k < 3; k++) r[k].setG(svfG(rf[k] * (1.f + 0.03f * b.nz.white())), 12.f);
        sph += (70.f + 130.f * b.nz.uni()) / SR;
        if (sph >= 1.f) { sph -= 1.f; stick = 0.5f + 0.5f * b.nz.uni(); }
        stick *= 0.998f;
        float w = b.nz.white();
        float exc = w * (0.4f + stick);
        float env = Min(1.f, t / 0.03f) * (t > dur - 0.15f ? Max(0.f, (dur - t) / 0.15f) : 1.f) * (0.7f + 0.3f * sinf(t * 17.f));
        float y = (r[0].bp(exc) + r[1].bp(exc) * 0.8f + r[2].bp(exc) * 0.6f) * 0.12f + rum.lp(w) * 0.3f;
        sqPh += sq * (1.f + 0.01f * b.nz.white()) / SR;
        sqPh -= floorf(sqPh);
        y += sinWrapped(sqPh) * 0.08f * (0.5f + 0.5f * sinf(t * 23.f));
        b.L[(size_t)i] += y * env;
    }
    grains(b, 0.f, dur, 90.f, 0.07f, 4000.f, 9500.f, 0.001f, 0.004f);
}
static void s_kickstand(Buf& b) {
    click(b, 0.f, 0.5f, 3);
    Mode m[4] = {{b.rnd(900.f, 1000.f), 0.2f, 1.f}, {b.rnd(2600.f, 2800.f), 0.12f, 0.7f}, {b.rnd(5000.f, 5300.f), 0.08f, 0.5f}, {b.rnd(7100.f, 7500.f), 0.05f, 0.3f}};
    modes(b, 0.f, 0.35f, m, 4);
    tone(b, 0.f, 0.1f, 150.f, 90.f, 0.02f, 0.4f, 0.001f, 0.02f);
    noise(b, 0.f, 0.05f, 0.4f, 0.0003f, 0.008f, FLP, 900.f, -1.f, 0.1f, 0.7f);
}
static void s_splash(Buf& b, bool big) {
    if (!big) {
        tone(b, 0.f, 0.1f, 350.f, 950.f, 0.02f, 0.4f, 0.001f, 0.025f);
        noise(b, 0.f, 0.4f, 0.8f, 0.004f, 0.08f, FBP, 2500.f, 1600.f, 0.15f, 0.7f);
        noise(b, 0.f, 0.3f, 0.5f, 0.004f, 0.05f, FLP, 600.f, -1.f, 0.1f, 0.7f, 1);
        for (int j = 0; j < 10; j++) {
            float f0 = b.rnd(800.f, 2500.f);
            tone(b, b.rnd(0.08f, 0.5f), 0.04f, f0, f0 * b.rnd(1.5f, 2.2f), 0.01f, 0.07f, 0.001f, 0.01f);
        }
    } else {
        tone(b, 0.f, 0.5f, 62.f, 40.f, 0.08f, 0.9f, 0.003f, 0.08f);
        noise(b, 0.f, 0.6f, 1.f, 0.003f, 0.12f, FLP, 280.f, -1.f, 0.1f, 0.7f, 1);
        noise(b, 0.f, 1.2f, 1.f, 0.02f, 0.35f, FBP, 1600.f, 1100.f, 0.3f, 0.5f, 1);
        noise(b, 0.01f, 0.9f, 0.3f, 0.01f, 0.3f, FHP, 4000.f, -1.f, 0.1f, 0.7f);
        // falling water
        int s0 = S(0.3f), n = S(1.4f);
        b.ensure(s0 + n);
        Svf bp;
        bp.set(3000.f, 0.6f);
        float g = 0.f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / SR;
            if (b.nz.uni() < 400.f / SR) g = 0.4f + 0.6f * b.nz.uni();
            g *= 0.998f;
            b.L[(size_t)(s0 + i)] += bp.bp(b.nz.white()) * (0.2f + g) * 0.35f * expf(-t * 2.f);
        }
        for (int j = 0; j < 30; j++) {
            float f0 = b.rnd(400.f, 2500.f);
            tone(b, b.rnd(0.1f, 1.6f), 0.05f, f0, f0 * b.rnd(1.5f, 2.5f), 0.012f, 0.08f, 0.001f, 0.012f);
        }
    }
}
static void s_boatSlam(Buf& b) {
    tone(b, 0.f, 0.5f, 58.f, 40.f, 0.08f, 1.f, 0.002f, 0.08f);
    noise(b, 0.f, 0.4f, 1.f, 0.002f, 0.08f, FLP, 230.f, -1.f, 0.1f, 0.7f, 1);
    Mode m[4] = {{b.rnd(85.f, 95.f), 0.15f, 1.f}, {b.rnd(150.f, 165.f), 0.12f, 0.7f}, {b.rnd(250.f, 270.f), 0.1f, 0.5f}, {b.rnd(400.f, 420.f), 0.07f, 0.35f}};
    modes(b, 0.f, 0.35f, m, 4);
    noise(b, 0.005f, 0.8f, 0.45f, 0.01f, 0.25f, FHP, 2500.f, -1.f, 0.1f, 0.7f);
    noise(b, 0.01f, 0.6f, 0.4f, 0.01f, 0.15f, FBP, 1200.f, -1.f, 0.1f, 0.6f, 1);
}

// ---------------------------------------------------------------------------------------------
// World
static void s_thunder(Buf& b, bool closeStrike) {
    float dur = closeStrike ? 7.f : 8.5f;
    if (closeStrike) {
        click(b, 0.f, 1.f, 10, true);
        noise(b, 0.f, 0.3f, 1.f, 0.0005f, 0.025f, FHP, 900.f, -1.f, 0.1f, 0.6f);
        crackles(b, 0.f, 0.45f, 90.f, 0.7f, 300.f, 4000.f, 0.25f);
        noise(b, 0.02f, 1.2f, 0.9f, 0.01f, 0.35f, FLP, 1500.f, 300.f, 0.4f, 0.7f, 1);
    }
    int swells = closeStrike ? 8 : 10;
    for (int i = 0; i < swells; i++) {
        float t0 = closeStrike ? b.rnd(0.05f, 3.5f) : b.rnd(0.2f, 5.0f);
        float a = b.rnd(0.4f, 1.f) * (closeStrike ? 0.8f : 0.7f);
        noise(b, t0, dur - t0, a, b.rnd(0.1f, 0.6f), b.rnd(0.4f, 1.4f), FLP4, b.rnd(120.f, 260.f), b.rnd(60.f, 110.f), 1.f, 0.7f, 2);
    }
    tone(b, closeStrike ? 0.f : 0.3f, 4.f, 55.f, 30.f, 1.f, 0.4f, 0.2f, 1.2f);
    reverb(b, 3.f, 0.25f, 0.75f, 0.03f, 1.8f, 0.2f);
}
static void s_dogBark(Buf& b, bool distant) {
    bool big = b.chance(0.5f);
    float fs = big ? 0.8f : 1.1f;
    float f0 = big ? b.rnd(230.f, 300.f) : b.rnd(380.f, 520.f);
    int nb = b.irnd(1, distant ? 4 : 3);
    float t = 0.f;
    for (int k = 0; k < nb; k++) {
        float d = b.rnd(0.12f, 0.2f);
        float ff = f0 * b.rnd(0.92f, 1.08f);
        vocal(b, t, d, 0.03f, [&](float u, VFrame& fr) {
            fr.f0 = ff * (u < 0.15f ? 1.f + u * 1.5f : 1.22f - 0.5f * (u - 0.15f));
            fr.amp = Min(1.f, u / 0.04f) * expf(-u * 2.5f);
            fr.breath = 0.3f;
            fr.bright = 0.8f;
            fr.rough = 0.45f;
            fr.F[0] = 750.f * fs; fr.F[1] = 1500.f * fs; fr.F[2] = 2600.f * fs; fr.F[3] = 3600.f * fs;
            fr.BW[0] = 130.f; fr.BW[1] = 150.f; fr.BW[2] = 200.f; fr.BW[3] = 250.f;
        });
        noise(b, t, 0.05f, 0.3f, 0.002f, 0.015f, FBP, 1500.f * fs, -1.f, 0.1f, 0.8f);
        t += d + b.rnd(0.18f, 0.4f);
    }
    if (distant) {
        lowpass(b, 1900.f);
        reverb(b, 1.4f, 0.45f, 0.6f, 0.03f, 1.4f, 0.3f);
    } else {
        reverb(b, 0.7f, 0.1f, 0.5f, 0.01f, 1.f, 0.3f);
    }
}
static void s_seagull(Buf& b) {
    int calls = b.irnd(1, 4);
    bool laugh = b.chance(0.4f);
    float t = 0.f;
    for (int k = 0; k < calls; k++) {
        float d = laugh ? b.rnd(0.1f, 0.15f) : b.rnd(0.3f, 0.5f);
        float f0 = b.rnd(950.f, 1250.f);
        vocal(b, t, d, 0.03f, [&](float u, VFrame& fr) {
            if (laugh) fr.f0 = f0 * (1.25f - 0.35f * u);
            else fr.f0 = f0 * (u < 0.12f ? 0.8f + 0.7f * (u / 0.12f) : 1.5f - 0.8f * (u - 0.12f));
            fr.amp = Min(1.f, u / 0.06f) * (u > 0.7f ? (1.f - u) / 0.3f : 1.f);
            fr.breath = 0.2f;
            fr.bright = 0.9f;
            fr.rough = 0.35f;
            fr.F[0] = 1350.f; fr.F[1] = 2600.f; fr.F[2] = 3800.f; fr.F[3] = 5000.f;
            fr.BW[0] = 200.f; fr.BW[1] = 250.f; fr.BW[2] = 300.f; fr.BW[3] = 400.f;
        });
        t += d + (laugh ? b.rnd(0.03f, 0.07f) : b.rnd(0.15f, 0.35f));
    }
    reverb(b, 1.0f, 0.12f, 0.5f, 0.02f, 1.2f, 0.2f);
}
static void birdSyllables(Buf& b, float t, int count, float fBase, float spread) {
    for (int k = 0; k < count; k++) {
        int type = b.irnd(0, 3);
        float d = b.rnd(0.025f, 0.08f);
        float f0 = fBase * b.rnd(1.f - spread, 1.f + spread);
        switch (type) {
            case 0: tone(b, t, d, f0, f0 * b.rnd(1.3f, 1.7f), d * 0.5f, 0.6f, 0.003f, d * 0.4f, 0.f, 0.08f); break;
            case 1: tone(b, t, d, f0 * 1.5f, f0 * b.rnd(0.6f, 0.8f), d * 0.4f, 0.6f, 0.003f, d * 0.4f, 0.f, 0.08f); break;
            case 2: {  // trill
                int nt = b.irnd(3, 7);
                for (int j = 0; j < nt; j++) tone(b, t + (float)j * 0.035f, 0.03f, f0, f0 * 1.15f, 0.01f, 0.45f, 0.002f, 0.008f);
                d = (float)nt * 0.035f;
                break;
            }
            default: fmNote(b, t, d, f0, 0.5f, 1.5f, d, 0.5f, 0.004f, d * 0.4f); break;
        }
        t += d + b.rnd(0.02f, 0.09f);
    }
}
static void s_birdChirp(Buf& b) {
    birdSyllables(b, 0.f, b.irnd(3, 8), b.rnd(2600.f, 5200.f), 0.2f);
    reverb(b, 0.6f, 0.1f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void s_alarmChirp(Buf& b) {
    int n = b.irnd(1, 2);
    float f = b.rnd(2000.f, 2600.f);
    for (int k = 0; k < n + 1; k++) {
        int s0 = S(0.14f * (float)k), m = S(k == n ? 0.12f : 0.07f);
        b.ensure(s0 + m);
        float ph = 0.f, ff = (k == 1 && n == 1) ? f * 0.8f : f;
        for (int i = 0; i < m; i++) {
            ph += ff / SR;
            ph -= floorf(ph);
            float env = Min(1.f, (float)i / 60.f) * Min(1.f, (float)(m - i) / 60.f);
            b.L[(size_t)(s0 + i)] += (ph < 0.5f ? 0.5f : -0.5f) * env;
        }
    }
    peakEq(b, 2200.f, 1.2f, 8.f);
    highpass(b, 600.f);
    reverb(b, 0.6f, 0.1f, 0.5f, 0.01f, 1.f, 0.2f);
}
static void s_doorBuzz(Buf& b) {
    float dur = b.rnd(0.7f, 1.1f);
    int n = S(dur);
    b.ensure(n);
    float ph = 0.f, f = b.rnd(95.f, 125.f);
    for (int i = 0; i < n; i++) {
        ph += f / SR;
        ph -= floorf(ph);
        float v = (ph < 0.35f ? 1.f : -0.55f) + 0.4f * sinCycle(ph * 2.f);
        float env = Min(1.f, (float)i / 200.f) * Min(1.f, (float)(n - i) / 200.f);
        b.L[(size_t)i] += v * env * 0.5f;
    }
    peakEq(b, 900.f, 0.8f, 10.f);
    highpass(b, 300.f);
    lowpass(b, 3500.f);
    saturate(b, 2.f);
}
static void s_cashRegister(Buf& b) {
    noise(b, 0.f, 0.05f, 0.6f, 0.0005f, 0.01f, FLP, 1000.f, -1.f, 0.1f, 0.7f);
    Mode k[2] = {{b.rnd(650.f, 750.f), 0.03f, 1.f}, {b.rnd(1500.f, 1700.f), 0.02f, 0.6f}};
    modes(b, 0.f, 0.3f, k, 2);
    float f = b.rnd(2000.f, 2250.f);
    Mode m[4] = {{f, 0.35f, 1.f}, {f * 2.76f, 0.22f, 0.5f}, {f * 5.4f, 0.12f, 0.3f}, {f * 8.93f, 0.06f, 0.2f}};
    modes(b, 0.09f, 0.4f, m, 4);
    click(b, 0.09f, 0.3f, 2);
    noise(b, 0.13f, 0.3f, 0.2f, 0.02f, 0.12f, FBP, 1200.f, -1.f, 0.1f, 0.8f);
    mechClack(b, 0.28f, 0.3f, 0.6f);
    grains(b, 0.16f, 0.3f, 40.f, 0.1f, 3000.f, 7000.f, 0.02f, 0.08f);
}
static void s_bell(Buf& b) {
    static const float kRatio[10] = {0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.51f, 2.66f, 3.01f, 4.07f, 5.43f};
    static const float kTau[10] = {6.f, 4.5f, 3.5f, 3.f, 2.5f, 1.5f, 1.3f, 1.1f, 0.7f, 0.5f};
    static const float kAmp[10] = {0.5f, 0.7f, 0.8f, 0.4f, 1.f, 0.3f, 0.35f, 0.25f, 0.2f, 0.15f};
    float f = b.rnd(200.f, 330.f);
    Mode m[20];
    for (int i = 0; i < 10; i++) {
        float fr = f * kRatio[i];
        m[2 * i] = {fr, kTau[i] * 0.7f, kAmp[i]};
        m[2 * i + 1] = {fr + b.rnd(0.4f, 1.5f), kTau[i] * 0.7f, kAmp[i] * 0.7f};
    }
    modes(b, 0.f, 0.25f, m, 20, 0.f, 6.f);
    noise(b, 0.f, 0.05f, 0.5f, 0.0003f, 0.004f, FHP, 2500.f, -1.f, 0.1f, 0.7f);
    click(b, 0.f, 0.4f, 4);
    reverb(b, 2.f, 0.15f, 0.5f, 0.02f, 1.5f, 0.2f);
}
static void s_heliFlyby(Buf& b) {
    const float total = 8.f;
    int srcN = S(total + 2.f);
    std::vector<float> src((size_t)srcN);
    emit::RotorCore rc;
    rc.init((u32)b.rng.next());
    rc.setParams(1.f, b.rnd(0.55f, 0.85f));
    for (int i = 0; i < srcN; i++) src[(size_t)i] = rc.tick();
    int n = S(total);
    b.ensure(n);
    float v = b.rnd(40.f, 60.f), h = b.rnd(40.f, 90.f), tc = total * 0.5f;
    Svf lp;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float tau = t;
        for (int it = 0; it < 3; it++) {
            float x = v * (tau - tc);
            float d = sqrtf(x * x + h * h);
            tau = t - d / 343.f;
        }
        float x = v * (tau - tc);
        float d = sqrtf(x * x + h * h);
        float pos = (tau + 1.5f) * SR;
        int ip = (int)pos;
        float fr = pos - (float)ip;
        float s = 0.f;
        if (ip >= 1 && ip + 2 < srcN) s = hermite(src[(size_t)ip - 1], src[(size_t)ip], src[(size_t)ip + 1], src[(size_t)ip + 2], fr);
        if ((i & 63) == 0) lp.setG(svfG(22000.f / (1.f + d / 60.f)), 0.7f);
        b.L[(size_t)i] += lp.lp(s) * (h / d);
    }
    for (int i = 0; i < S(0.3f); i++) {
        b.L[(size_t)i] *= (float)i / (float)S(0.3f);
        b.L[(size_t)(n - 1 - i)] *= (float)i / (float)S(0.3f);
    }
}

// ---------------------------------------------------------------------------------------------
// UI / game flow (clean, musical; E major palette)
static void s_uiMove(Buf& b) {
    float f = midiToHz(88.f) * b.rnd(0.99f, 1.01f);
    fmNote(b, 0.f, 0.08f, f, 2.f, 0.8f, 0.01f, 0.6f, 0.001f, 0.018f);
    click(b, 0.f, 0.15f, 3);
}
static void s_uiSelect(Buf& b) {
    bellNote(b, 0.f, 83.f, 0.5f, 0.12f, -0.2f);
    bellNote(b, 0.065f, 88.f, 0.6f, 0.18f, 0.2f);
    reverb(b, 0.5f, 0.12f, 0.4f, 0.005f, 0.8f, 0.2f);
}
static void s_uiBack(Buf& b) {
    fmNote(b, 0.f, 0.3f, midiToHz(88.f), 1.f, 1.2f, 0.03f, 0.45f, 0.002f, 0.05f, 0.2f);
    fmNote(b, 0.06f, 0.35f, midiToHz(83.f), 1.f, 1.0f, 0.03f, 0.5f, 0.002f, 0.07f, -0.2f);
    reverb(b, 0.4f, 0.1f, 0.4f, 0.005f, 0.8f, 0.2f);
}
static void s_uiError(Buf& b) {
    for (int k = 0; k < 2; k++) {
        int s0 = S(0.13f * (float)k), n = S(0.09f);
        b.ensure(s0 + n);
        float ph = 0.f, f = k == 0 ? 185.f : 174.6f;
        for (int i = 0; i < n; i++) {
            ph += f / SR;
            ph -= floorf(ph);
            float env = Min(1.f, (float)i / 100.f) * Min(1.f, (float)(n - i) / 150.f);
            b.L[(size_t)(s0 + i)] += ((ph < 0.5f ? 0.5f : -0.5f) + 0.3f * sinWrapped(ph)) * env;
        }
    }
    lowpass(b, 1400.f);
    reverb(b, 0.3f, 0.08f, 0.5f, 0.005f, 0.7f, 0.2f);
}
static void s_uiNotify(Buf& b) {
    bellNote(b, 0.f, 88.f, 0.5f, 0.35f, -0.35f);
    bellNote(b, 0.07f, 92.f, 0.45f, 0.35f, 0.f);
    bellNote(b, 0.14f, 95.f, 0.45f, 0.45f, 0.35f);
    reverb(b, 1.0f, 0.2f, 0.45f, 0.01f, 1.f, 0.2f);
}
static void s_uiText(Buf& b) {
    float f = b.rnd(2000.f, 2400.f);
    tone(b, 0.f, 0.03f, f, f, 0.f, 0.5f, 0.0005f, 0.006f);
    click(b, 0.f, 0.25f, 2);
}
static void s_phoneRing(Buf& b) {
    static const float kMel[10] = {76, 83, 80, 83, 88, 87, 83, 80, 83, 76};
    static const float kT[10] = {0, 0.14f, 0.28f, 0.42f, 0.56f, 0.77f, 0.91f, 1.05f, 1.19f, 1.4f};
    for (int i = 0; i < 10; i++) {
        float pan = ((i & 1) ? 0.25f : -0.25f);
        marimba(b, kT[i], kMel[i], i == 9 ? 0.8f : 0.6f, pan);
        marimba(b, kT[i], kMel[i] + 12.f, 0.15f, -pan);
    }
    reverb(b, 0.8f, 0.15f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void s_phoneMsg(Buf& b) {
    bellNote(b, 0.f, 85.f, 0.55f, 0.25f, -0.15f);
    bellNote(b, 0.12f, 88.f, 0.6f, 0.4f, 0.15f);
    reverb(b, 0.8f, 0.15f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void s_pickupCash(Buf& b) {
    click(b, 0.f, 0.3f, 3);
    for (int i = 0; i < 6; i++) {
        float f = b.rnd(3500.f, 7000.f);
        Mode m[2] = {{f, b.rnd(0.05f, 0.15f), 1.f}, {f * 2.4f, 0.04f, 0.4f}};
        modes(b, b.rnd(0.f, 0.18f), 0.12f, m, 2, b.rnd(-0.6f, 0.6f));
    }
    bellNote(b, 0.05f, 100.f, 0.35f, 0.25f, 0.f);
    bellNote(b, 0.11f, 104.f, 0.3f, 0.3f, 0.f);
    reverb(b, 0.7f, 0.15f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void s_pickupWeapon(Buf& b) {
    mechClack(b, 0.f, 0.6f, 0.8f);
    mechClack(b, 0.09f, 0.5f, 1.f);
    noise(b, 0.f, 0.4f, 0.25f, 0.15f, 0.08f, FBP, 600.f, 2400.f, 0.2f, 1.f);
    tone(b, 0.f, 0.3f, 110.f, 80.f, 0.05f, 0.5f, 0.002f, 0.08f);
    reverb(b, 0.5f, 0.1f, 0.5f, 0.01f, 0.9f, 0.2f);
}
static void s_pickupHealth(Buf& b) {
    static const float kN[4] = {76, 80, 83, 88};
    for (int i = 0; i < 4; i++) {
        float f = midiToHz(kN[i]);
        fmNote(b, (float)i * 0.06f, 0.7f, f, 1.f, 0.7f, 0.2f, 0.35f, 0.01f, 0.25f, -0.3f + 0.2f * (float)i);
        tone(b, (float)i * 0.06f, 0.6f, f * 2.f, f * 2.f, 0.f, 0.08f, 0.01f, 0.15f, 0.3f - 0.2f * (float)i);
    }
    reverb(b, 1.0f, 0.2f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void s_pickupCollectible(Buf& b) {
    static const float kP[8] = {88, 90, 92, 95, 97, 100, 102, 104};
    for (int i = 0; i < 8; i++) bellNote(b, (float)i * 0.035f, kP[i], 0.28f, 0.2f + 0.04f * (float)i, -0.6f + 0.17f * (float)i);
    noise(b, 0.f, 0.6f, 0.06f, 0.1f, 0.15f, FHP, 7000.f, -1.f, 0.1f, 0.7f, 0, 0.f);
    reverb(b, 1.2f, 0.22f, 0.35f, 0.01f, 1.1f, 0.2f);
}
static void s_checkpoint(Buf& b) {
    bellNote(b, 0.f, 88.f, 0.5f, 0.4f, -0.2f);
    bellNote(b, 0.f, 95.f, 0.35f, 0.4f, 0.2f);
    tone(b, 0.f, 0.2f, 120.f, 80.f, 0.03f, 0.3f, 0.002f, 0.05f);
    reverb(b, 0.9f, 0.18f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void chordStab(Buf& b, float t0, float dur, const float* notes, int n, float amp, float fcPeak, float att = 0.005f,
                      float rel = 0.25f, int voices = 5) {
    for (int i = 0; i < n; i++)
        sawNote(b, t0, dur, midiToHz(notes[i]), amp, att, rel, 400.f, fcPeak, fcPeak * 0.45f, 0.15f, voices, 18.f,
                ((float)i / Max(1.f, (float)(n - 1)) - 0.5f) * 1.2f, 0.9f);
}
static void s_missionPassed(Buf& b, int var) {
    float E[5] = {64, 68, 71, 76, 80};
    float A[4] = {69, 73, 76, 81};
    float B[4] = {71, 75, 78, 83};
    float Cs[4] = {61, 64, 68, 73};
    float bpm = 132.f, beat = 60.f / bpm;
    kickDrum(b, 0.f, 1.f);
    crashCym(b, 0.f, 0.6f, -0.3f);
    subHit(b, 0.f, 0.6f, 82.f, 41.f, 0.25f);
    if (var == 0) chordStab(b, 0.f, beat * 1.5f, E, 4, 0.22f, 5000.f);
    else chordStab(b, 0.f, beat * 1.5f, Cs, 4, 0.22f, 5000.f);
    for (int i = 0; i < 4; i++) bellNote(b, beat * 0.5f + beat * 0.25f * (float)i, 88.f + (float)(i == 1 ? 4 : i == 2 ? 7 : i == 3 ? 12 : 0), 0.22f, 0.25f, -0.4f + 0.27f * (float)i);
    snareDrum(b, beat * 2.f, 0.6f, 0.1f);
    chordStab(b, beat * 2.f, beat * 1.2f, A, 4, 0.2f, 4200.f);
    snareDrum(b, beat * 3.f, 0.6f, 0.1f);
    chordStab(b, beat * 3.f, beat * 0.9f, B, 4, 0.2f, 4500.f);
    float tEnd = beat * 4.f;
    kickDrum(b, tEnd, 1.f);
    crashCym(b, tEnd, 0.7f, 0.3f);
    subHit(b, tEnd, 0.7f, 82.f, 41.f, 0.4f);
    chordStab(b, tEnd, beat * 4.f, E, 5, 0.22f, 6000.f, 0.01f, 0.9f, 6);
    sawNote(b, tEnd, beat * 4.f, midiToHz(40.f), 0.35f, 0.01f, 0.8f, 200.f, 900.f, 400.f, 0.2f, 2, 8.f);
    bellNote(b, tEnd, 100.f, 0.2f, 0.8f, 0.4f);
    bellNote(b, tEnd + beat * 0.5f, 95.f, 0.18f, 0.8f, -0.4f);
    reverb(b, 2.2f, 0.3f, 0.45f, 0.015f, 1.4f, 0.25f);
}
static void s_missionFailed(Buf& b) {
    float Am[3] = {57, 60, 64};
    float F[3] = {53, 57, 60};
    float E[3] = {52, 56, 59};
    subHit(b, 0.f, 0.8f, 60.f, 30.f, 0.6f);
    chordStab(b, 0.f, 0.8f, Am, 3, 0.2f, 900.f, 0.08f, 0.5f, 4);
    chordStab(b, 0.85f, 0.8f, F, 3, 0.2f, 800.f, 0.08f, 0.5f, 4);
    chordStab(b, 1.7f, 1.4f, E, 3, 0.22f, 700.f, 0.08f, 1.2f, 4);
    static const float kMel[4] = {76, 74, 72, 71};
    for (int i = 0; i < 4; i++) bellNote(b, 0.1f + 0.45f * (float)i, kMel[i], 0.2f, 0.6f, -0.3f + 0.2f * (float)i);
    sawNote(b, 0.f, 3.f, midiToHz(33.f), 0.3f, 0.05f, 1.f, 150.f, 400.f, 250.f, 0.4f, 2, 6.f);
    reverb(b, 2.6f, 0.35f, 0.55f, 0.02f, 1.5f, 0.2f);
}
static void s_wasted(Buf& b) {
    subHit(b, 0.f, 1.f, 58.f, 28.f, 0.6f);
    noise(b, 0.f, 0.8f, 0.6f, 0.002f, 0.25f, FLP, 1500.f, 300.f, 0.3f, 0.7f, 1);
    noise(b, 0.f, 2.f, 0.25f, 0.05f, 0.6f, FBP, 3000.f, 300.f, 0.6f, 1.5f, 0, 0.f);
    float cl[4] = {50, 53, 57, 61};
    for (int i = 0; i < 4; i++)
        sawNote(b, 0.05f, 3.2f, midiToHz(cl[i]), 0.16f, 0.3f, 1.2f, 300.f, 1100.f, 800.f, 0.5f, 4, 22.f,
                -0.5f + 0.33f * (float)i, 0.8f, -3.f, 3.5f);
    for (int k = 0; k < 2; k++) {
        float t = 1.2f + 1.2f * (float)k;
        float a = k == 0 ? 0.6f : 0.4f;
        tone(b, t, 0.3f, 62.f, 40.f, 0.03f, a, 0.003f, 0.07f);
        tone(b, t + 0.24f, 0.3f, 58.f, 38.f, 0.03f, a * 0.7f, 0.003f, 0.07f);
    }
    reverb(b, 3.f, 0.35f, 0.6f, 0.03f, 1.8f, 0.2f);
}
static void s_busted(Buf& b) {
    kickDrum(b, 0.f, 1.f);
    snareDrum(b, 0.f, 0.7f);
    crashCym(b, 0.f, 0.6f, -0.2f);
    subHit(b, 0.f, 0.6f, 75.f, 38.f, 0.3f);
    for (int i = 0; i < 6; i++) {
        float n = (i & 1) ? 67.f : 71.f;
        sawNote(b, 0.2f + 0.16f * (float)i, 0.14f, midiToHz(n), 0.25f, 0.005f, 0.06f, 500.f, 3500.f, 2000.f, 0.08f, 3, 10.f,
                (i & 1) ? 0.4f : -0.4f);
    }
    for (int i = 0; i < 12; i++) snareDrum(b, 1.2f + 0.066f * (float)i, 0.15f + 0.04f * (float)i, 0.f);
    float Gm[4] = {55, 58, 62, 67};
    kickDrum(b, 2.f, 1.f);
    crashCym(b, 2.f, 0.7f, 0.2f);
    subHit(b, 2.f, 0.7f, 70.f, 35.f, 0.4f);
    chordStab(b, 2.f, 0.9f, Gm, 4, 0.22f, 3500.f, 0.005f, 0.7f);
    reverb(b, 2.f, 0.28f, 0.5f, 0.015f, 1.4f, 0.25f);
}
static void s_wantedUp(Buf& b) {
    float cl[3] = {64, 65, 71};
    chordStab(b, 0.f, 0.22f, cl, 3, 0.25f, 5000.f, 0.003f, 0.18f, 4);
    subHit(b, 0.f, 0.6f, 90.f, 45.f, 0.2f);
    noise(b, 0.f, 0.35f, 0.25f, 0.2f, 0.05f, FBP, 800.f, 4000.f, 0.15f, 2.f);
    reverb(b, 1.2f, 0.25f, 0.5f, 0.01f, 1.2f, 0.2f);
}
static void s_wantedLost(Buf& b) {
    float E[4] = {64, 68, 71, 76};
    for (int i = 0; i < 4; i++)
        sawNote(b, 0.f, 1.1f, midiToHz(E[i]), 0.14f, 0.35f, 0.9f, 400.f, 1600.f, 1400.f, 0.5f, 4, 14.f, -0.45f + 0.3f * (float)i);
    bellNote(b, 0.4f, 83.f, 0.25f, 0.6f, 0.3f);
    bellNote(b, 0.6f, 88.f, 0.2f, 0.7f, -0.3f);
    reverb(b, 1.8f, 0.3f, 0.45f, 0.015f, 1.3f, 0.2f);
}
static void s_cameraShutter(Buf& b) {
    mechClack(b, 0.f, 0.6f, 1.2f);
    noise(b, 0.f, 0.05f, 0.4f, 0.0005f, 0.01f, FLP, 1500.f, -1.f, 0.1f, 0.7f);
    float t2 = b.rnd(0.035f, 0.07f);
    mechClack(b, t2, 0.5f, 0.9f);
    noise(b, t2, 0.06f, 0.3f, 0.0005f, 0.012f, FLP, 1200.f, -1.f, 0.1f, 0.7f);
    fmNote(b, 0.005f, 0.05f, b.rnd(3000.f, 4000.f), 1.5f, 2.f, 0.02f, 0.04f, 0.002f, 0.015f);
}
static void s_purchase(Buf& b) {
    click(b, 0.f, 0.3f, 3);
    mechClack(b, 0.f, 0.25f, 0.7f);
    bellNote(b, 0.06f, 88.f, 0.4f, 0.4f, -0.2f);
    bellNote(b, 0.06f, 95.f, 0.3f, 0.4f, 0.2f);
    for (int i = 0; i < 4; i++) {
        float f = b.rnd(4000.f, 7500.f);
        Mode m = {f, b.rnd(0.05f, 0.12f), 1.f};
        modes(b, 0.08f + b.rnd(0.f, 0.15f), 0.1f, &m, 1);
    }
    reverb(b, 0.8f, 0.15f, 0.4f, 0.01f, 1.f, 0.2f);
}
static void s_beep(Buf& b, float f, float dur, bool chord) {
    tone(b, 0.f, dur, f, f, 0.f, 0.5f, 0.003f, dur * 2.f, 0.f, 0.f, 0.15f);
    if (chord) {
        tone(b, 0.f, dur, f * 1.26f, f * 1.26f, 0.f, 0.3f, 0.003f, dur * 2.f);
        tone(b, 0.f, dur, f * 1.498f, f * 1.498f, 0.f, 0.3f, 0.003f, dur * 2.f);
    }
    reverb(b, 0.5f, 0.1f, 0.4f, 0.005f, 0.8f, 0.2f);
}

// ---------------------------------------------------------------------------------------------
// Internal ambience one-shots
static void s_cricket(Buf& b) {
    float f = b.rnd(4200.f, 5200.f);
    int chirps = b.irnd(1, 2);
    float t = 0.f;
    for (int c = 0; c < chirps; c++) {
        int pulses = b.irnd(3, 5);
        for (int p = 0; p < pulses; p++) tone(b, t + (float)p * 0.038f, 0.022f, f, f * 0.99f, 0.01f, 0.5f, 0.003f, 0.008f, 0.f, 0.1f);
        t += (float)pulses * 0.038f + b.rnd(0.25f, 0.45f);
    }
}
static void s_treefrog(Buf& b) {
    if (b.chance(0.5f)) {  // peeper
        float f = b.rnd(2500.f, 2900.f);
        int n = b.irnd(1, 3);
        for (int k = 0; k < n; k++) tone(b, (float)k * 0.5f, 0.15f, f, f * 1.18f, 0.08f, 0.5f, 0.01f, 0.06f, 0.f, 0.05f);
    } else {  // ribbit: pulsed buzzy tone
        float f = b.rnd(1400.f, 2000.f);
        for (int k = 0; k < 2; k++) {
            int s0 = S((float)k * 0.22f), n = S(0.13f);
            b.ensure(s0 + n);
            float ph = 0.f, am = 0.f;
            for (int i = 0; i < n; i++) {
                ph += f / SR;
                ph -= floorf(ph);
                am += 95.f / SR;
                am -= floorf(am);
                float env = sinf(kPi * (float)i / (float)n);
                b.L[(size_t)(s0 + i)] += sinWrapped(ph) * (am < 0.35f ? 1.f : 0.1f) * env * 0.5f;
            }
        }
        lowpass(b, 3500.f);
    }
}
static void s_bullfrog(Buf& b) {
    int n = b.irnd(2, 3);
    float f0 = b.rnd(95.f, 140.f);
    for (int k = 0; k < n; k++) {
        float d = b.rnd(0.35f, 0.6f);
        vocal(b, (float)k * (d + 0.25f), d, 0.02f, [&](float u, VFrame& fr) {
            fr.f0 = f0 * (1.05f - 0.1f * u);
            fr.amp = sinf(kPi * u) * (0.7f + 0.3f * sinf(kTwoPi * 11.f * u));
            fr.breath = 0.05f;
            fr.bright = 0.2f;
            fr.rough = 0.3f;
            fr.F[0] = 320.f; fr.F[1] = 700.f; fr.F[2] = 1900.f; fr.F[3] = 2900.f;
            fr.BW[0] = 70.f; fr.BW[1] = 90.f; fr.BW[2] = 150.f; fr.BW[3] = 200.f;
        });
    }
    lowpass(b, 1500.f);
}
static void s_owl(Buf& b) {
    float f = b.rnd(340.f, 420.f);
    int n = b.irnd(2, 4);
    float t = 0.f;
    for (int k = 0; k < n; k++) {
        float d = k == n - 1 ? b.rnd(0.45f, 0.7f) : b.rnd(0.2f, 0.3f);
        tone(b, t, d, f * 1.03f, f * 0.96f, d, 0.5f, d * 0.3f, d * 0.5f, 0.f, 0.06f);
        noise(b, t, d, 0.05f, d * 0.3f, d * 0.5f, FBP, f * 2.f, -1.f, 0.1f, 3.f);
        t += d + b.rnd(0.15f, 0.3f);
    }
    reverb(b, 1.5f, 0.3f, 0.6f, 0.03f, 1.4f, 0.2f);
}
static void s_birdSong(Buf& b) {
    float base = b.rnd(2400.f, 4800.f);
    float t = 0.f;
    int phrases = b.irnd(2, 3);
    for (int p = 0; p < phrases; p++) {
        birdSyllables(b, t, b.irnd(4, 9), base * b.rnd(0.85f, 1.15f), 0.25f);
        t += b.rnd(0.6f, 0.9f);
    }
    reverb(b, 0.9f, 0.12f, 0.45f, 0.015f, 1.1f, 0.2f);
}
static void s_heron(Buf& b) {
    float d = b.rnd(0.35f, 0.5f);
    float f0 = b.rnd(220.f, 300.f);
    vocal(b, 0.f, d, 0.05f, [&](float u, VFrame& fr) {
        fr.f0 = f0 * (1.1f - 0.3f * u);
        fr.amp = Min(1.f, u / 0.08f) * (u > 0.6f ? (1.f - u) / 0.4f : 1.f);
        fr.breath = 0.35f;
        fr.bright = 0.7f;
        fr.rough = 0.7f;
        fr.F[0] = 800.f; fr.F[1] = 1300.f; fr.F[2] = 2500.f; fr.F[3] = 3500.f;
        fr.BW[0] = 150.f; fr.BW[1] = 180.f; fr.BW[2] = 250.f; fr.BW[3] = 300.f;
    });
    reverb(b, 1.2f, 0.2f, 0.5f, 0.02f, 1.3f, 0.2f);
}
static void s_crow(Buf& b) {
    int n = b.irnd(2, 3);
    float f0 = b.rnd(480.f, 600.f);
    for (int k = 0; k < n; k++) {
        float d = b.rnd(0.22f, 0.3f);
        vocal(b, (float)k * (d + 0.18f), d, 0.04f, [&](float u, VFrame& fr) {
            fr.f0 = f0 * (1.05f - 0.2f * u);
            fr.amp = Min(1.f, u / 0.08f) * (u > 0.65f ? (1.f - u) / 0.35f : 1.f);
            fr.breath = 0.25f;
            fr.bright = 0.8f;
            fr.rough = 0.6f;
            fr.F[0] = 1000.f; fr.F[1] = 1700.f; fr.F[2] = 2800.f; fr.F[3] = 3800.f;
            fr.BW[0] = 160.f; fr.BW[1] = 200.f; fr.BW[2] = 260.f; fr.BW[3] = 320.f;
        });
    }
    reverb(b, 1.2f, 0.2f, 0.5f, 0.02f, 1.3f, 0.2f);
}
static void s_hornDistant(Buf& b) {
    emit::HornCore hc;
    hc.init(b.rnd(0.f, 1.f));
    int honks = b.irnd(1, 3);
    float t = 0.f;
    for (int k = 0; k < honks; k++) {
        float d = honks == 1 ? b.rnd(0.4f, 1.1f) : b.rnd(0.12f, 0.3f);
        int s0 = S(t), n = S(d);
        b.ensure(s0 + n);
        hc.env = 0.f;
        hc.t = 0.f;
        for (int i = 0; i < n; i++) {
            float rel = Min(1.f, (float)(n - i) / 300.f);
            b.L[(size_t)(s0 + i)] += hc.tick() * rel;
        }
        t += d + b.rnd(0.08f, 0.15f);
    }
    lowpass(b, 2200.f);
    reverb(b, 1.6f, 0.45f, 0.6f, 0.03f, 1.5f, 0.3f);
}
static void s_sirenDistant(Buf& b, int var) {
    emit::SirenCore sc;
    sc.init();
    sc.mode = var % 3 == 2 ? 2 : var % 2;
    sc.cyc = b.rnd(0.f, 1.f);
    float dur = b.rnd(6.f, 8.f);
    int n = S(dur);
    b.ensure(n);
    float peakT = b.rnd(0.35f, 0.65f);
    for (int i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        float env = u < peakT ? u / peakT : (1.f - u) / (1.f - peakT);
        env = env * env * (3.f - 2.f * env);
        b.L[(size_t)i] += sc.tick() * env;
    }
    lowpass(b, 1700.f);
    reverb(b, 2.f, 0.5f, 0.6f, 0.04f, 1.6f, 0.3f);
}
static void s_bubbles(Buf& b) {
    int n = b.irnd(6, 14);
    for (int k = 0; k < n; k++) {
        float f0 = b.rnd(300.f, 1300.f);
        tone(b, b.rnd(0.f, 0.5f), 0.06f, f0, f0 * b.rnd(1.4f, 2.2f), 0.02f, b.rnd(0.2f, 0.5f), 0.001f, b.rnd(0.01f, 0.03f));
    }
    lowpass(b, 2500.f);
}
static void s_waterLap(Buf& b) {
    float d = b.rnd(1.2f, 1.8f);
    noise(b, 0.f, d, 0.6f, d * 0.35f, d * 0.3f, FLP, 700.f, 400.f, 0.5f, 0.7f, 1);
    grains(b, d * 0.25f, d * 0.6f, 25.f, 0.12f, 700.f, 2800.f, 0.008f, 0.03f);
    noise(b, d * 0.3f, d * 0.6f, 0.15f, 0.05f, 0.2f, FBP, 1800.f, -1.f, 0.1f, 0.8f);
}

// ---------------------------------------------------------------------------------------------
// Wildlife calls (game/wildlife.cpp)
// Wing claps of a flock bursting into the air: air whooshes with sharp tip claps, slowing as the birds climb.
static void s_wingFlap(Buf& b) {
    int birds = b.irnd(3, 6);
    for (int bird = 0; bird < birds; bird++) {
        int n = b.irnd(5, 10);
        float rate = b.rnd(7.f, 11.f), t = b.rnd(0.f, 0.25f), pan = b.rnd(-0.5f, 0.5f);
        for (int k = 0; k < n; k++) {
            float amp = (1.f - 0.55f * (float)k / (float)n) * b.rnd(0.6f, 1.f) / (float)birds * 2.f;
            noise(b, t, 0.09f, 0.55f * amp, 0.004f, 0.025f, FBP, b.rnd(600.f, 1100.f), b.rnd(1500.f, 2600.f), 0.04f, 1.2f, 0, pan);
            if (b.chance(0.75f)) click(b, t + 0.012f, 0.4f * amp, b.irnd(4, 8), false, pan);
            noise(b, t + 0.01f, 0.05f, 0.22f * amp, 0.001f, 0.012f, FHP, 3200.f, -1.f, 0.1f, 0.707f, 0, pan);
            t += 1.f / rate * b.rnd(0.85f, 1.2f);
            rate *= 0.97f;
        }
    }
    reverb(b, 0.6f, 0.1f, 0.4f, 0.01f, 1.f, 0.2f);
}
// Rock pigeon coo: a soft throaty "coo-roo-coo" with a gurgling tremolo.
static void s_pigeonCoo(Buf& b) {
    float f0 = b.rnd(250.f, 320.f);
    int syl = b.irnd(2, 3);
    float t = 0.f;
    for (int k = 0; k < syl; k++) {
        float d = k == syl - 1 ? b.rnd(0.45f, 0.7f) : b.rnd(0.18f, 0.3f);
        float trem = b.rnd(22.f, 30.f);
        vocal(b, t, d, 0.02f, [&](float u, VFrame& fr) {
            fr.f0 = f0 * (k == 1 ? 1.12f : 1.f) * (1.f + 0.12f * sinf(kPi * u) - (k == syl - 1 ? 0.15f * u : 0.f));
            fr.amp = sinf(kPi * u) * (0.65f + 0.35f * sinf(kTwoPi * trem * u * d));
            fr.breath = 0.12f;
            fr.bright = 0.15f;
            fr.rough = 0.25f;
            fr.F[0] = 330.f; fr.F[1] = 760.f; fr.F[2] = 2100.f; fr.F[3] = 3000.f;
            fr.BW[0] = 90.f; fr.BW[1] = 120.f; fr.BW[2] = 200.f; fr.BW[3] = 260.f;
        });
        t += d + b.rnd(0.04f, 0.1f);
    }
    lowpass(b, 1800.f);
    reverb(b, 0.5f, 0.1f, 0.5f, 0.01f, 0.8f, 0.2f);
}
// Alligator hiss: a long forced breath through the open jaws with a low body resonance.
static void s_gatorHiss(Buf& b) {
    float d = b.rnd(0.8f, 1.4f);
    noise(b, 0.f, d + 0.3f, 0.8f, 0.12f, d * 0.55f, FBP, b.rnd(2200.f, 3000.f), b.rnd(1500.f, 2000.f), d, 0.6f, 1);
    noise(b, 0.f, d + 0.3f, 0.5f, 0.1f, d * 0.5f, FHP, 4500.f, -1.f, 0.1f, 0.707f, 0);
    noise(b, 0.02f, d, 0.35f, 0.15f, d * 0.4f, FLP, 250.f, -1.f, 0.1f, 0.9f, 2);
    tone(b, 0.f, d, 70.f, 55.f, d, 0.12f, 0.2f, d * 0.5f);
    reverb(b, 0.5f, 0.08f, 0.5f, 0.01f, 0.8f, 0.2f);
}
// Alligator bellow: a deep rumbling roar, rough and throaty, with a sub-bass body.
static void s_gatorBellow(Buf& b) {
    float d = b.rnd(1.4f, 2.2f);
    float f0 = b.rnd(38.f, 52.f);
    vocal(b, 0.f, d, 0.05f, [&](float u, VFrame& fr) {
        fr.f0 = f0 * (1.f + 0.15f * sinf(kPi * u));
        fr.amp = Min(1.f, u / 0.12f) * (u > 0.7f ? (1.f - u) / 0.3f : 1.f) * (0.8f + 0.2f * sinf(u * 40.f));
        fr.breath = 0.25f;
        fr.bright = 0.3f;
        fr.rough = 0.8f;
        fr.F[0] = 260.f; fr.F[1] = 620.f; fr.F[2] = 1500.f; fr.F[3] = 2500.f;
        fr.BW[0] = 100.f; fr.BW[1] = 140.f; fr.BW[2] = 220.f; fr.BW[3] = 300.f;
    });
    tone(b, 0.05f, d, 32.f, 28.f, d, 0.5f, 0.3f, d * 0.6f);
    saturate(b, 1.6f);
    reverb(b, 1.8f, 0.25f, 0.6f, 0.03f, 1.5f, 0.3f);
}
// Jaws slamming shut: a bony clack with a heavy knock.
static void s_jawSnap(Buf& b) {
    click(b, 0.f, 0.9f, 5);
    Mode m[3] = {{b.rnd(170.f, 220.f), 0.05f, 1.f}, {b.rnd(850.f, 1100.f), 0.018f, 0.6f}, {b.rnd(2300.f, 2800.f), 0.008f, 0.35f}};
    modes(b, 0.f, 0.7f, m, 3);
    noise(b, 0.f, 0.08f, 0.5f, 0.0005f, 0.012f, FBP, 1800.f, 900.f, 0.03f, 0.8f);
    noise(b, 0.f, 0.25f, 0.3f, 0.002f, 0.06f, FLP, 300.f, -1.f, 0.1f, 0.7f, 2);
    reverb(b, 0.4f, 0.08f, 0.4f, 0.005f, 0.7f, 0.2f);
}
// Dog growl: a low rough rumble with a rhythmic flutter.
static void s_dogGrowl(Buf& b) {
    float d = b.rnd(0.7f, 1.3f);
    float f0 = b.rnd(85.f, 120.f);
    float flut = b.rnd(18.f, 26.f);
    vocal(b, 0.f, d, 0.04f, [&](float u, VFrame& fr) {
        fr.f0 = f0 * (1.f + 0.1f * sinf(kPi * u));
        fr.amp = Min(1.f, u / 0.08f) * (u > 0.75f ? (1.f - u) / 0.25f : 1.f) * (0.6f + 0.4f * fabsf(sinf(kPi * flut * u * d)));
        fr.breath = 0.35f;
        fr.bright = 0.5f;
        fr.rough = 0.85f;
        fr.F[0] = 480.f; fr.F[1] = 1150.f; fr.F[2] = 2400.f; fr.F[3] = 3300.f;
        fr.BW[0] = 140.f; fr.BW[1] = 170.f; fr.BW[2] = 230.f; fr.BW[3] = 280.f;
    });
    reverb(b, 0.5f, 0.08f, 0.5f, 0.01f, 0.8f, 0.2f);
}
// Dog yelp: sharp high cries of pain.
static void s_dogYelp(Buf& b) {
    int n = b.irnd(1, 3);
    float t = 0.f;
    for (int k = 0; k < n; k++) {
        float d = b.rnd(0.14f, 0.28f);
        float f0 = b.rnd(750.f, 1000.f) * (1.f - 0.12f * (float)k);
        vocal(b, t, d, 0.03f, [&](float u, VFrame& fr) {
            fr.f0 = f0 * (u < 0.15f ? 1.f + u : 1.15f - 0.55f * (u - 0.15f));
            fr.amp = Min(1.f, u / 0.05f) * (u > 0.6f ? (1.f - u) / 0.4f : 1.f);
            fr.breath = 0.25f;
            fr.bright = 0.9f;
            fr.rough = 0.3f;
            fr.F[0] = 900.f; fr.F[1] = 1800.f; fr.F[2] = 2900.f; fr.F[3] = 3800.f;
            fr.BW[0] = 150.f; fr.BW[1] = 180.f; fr.BW[2] = 240.f; fr.BW[3] = 300.f;
        });
        t += d + b.rnd(0.06f, 0.16f);
    }
    reverb(b, 0.7f, 0.12f, 0.5f, 0.01f, 1.f, 0.3f);
}
// Cat meow: "mee-ow", the mouth opening from ee to ow while the pitch rises then falls.
static void s_catMeow(Buf& b) {
    float d = b.rnd(0.5f, 0.9f);
    float f0 = b.rnd(480.f, 620.f);
    vocal(b, 0.f, d, 0.02f, [&](float u, VFrame& fr) {
        fr.f0 = f0 * (u < 0.35f ? 1.f + 0.35f * u / 0.35f : 1.35f - 0.55f * (u - 0.35f));
        fr.amp = Min(1.f, u / 0.08f) * (u > 0.7f ? (1.f - u) / 0.3f : 1.f);
        fr.breath = 0.18f;
        fr.bright = 0.7f;
        fr.rough = 0.1f;
        float o = Saturate(u * 1.4f - 0.2f);
        o = o * o * (3.f - 2.f * o);
        fr.F[0] = 420.f + 380.f * o; fr.F[1] = 2300.f - 1200.f * o; fr.F[2] = 3000.f - 400.f * o; fr.F[3] = 4000.f;
        fr.BW[0] = 90.f; fr.BW[1] = 120.f; fr.BW[2] = 180.f; fr.BW[3] = 250.f;
    });
    reverb(b, 0.6f, 0.1f, 0.5f, 0.01f, 0.9f, 0.2f);
}
// Cow moo: a long nasal "mmm-ooo" rising then sagging.
static void s_cowMoo(Buf& b) {
    float d = b.rnd(1.1f, 1.9f);
    float f0 = b.rnd(105.f, 150.f);
    vocal(b, 0.f, d, 0.015f, [&](float u, VFrame& fr) {
        fr.f0 = f0 * (u < 0.3f ? 1.f + 0.25f * u / 0.3f : 1.25f - 0.35f * (u - 0.3f));
        fr.amp = Min(1.f, u / 0.1f) * (u > 0.75f ? (1.f - u) / 0.25f : 1.f);
        fr.breath = 0.12f;
        fr.bright = 0.35f;
        fr.rough = 0.2f;
        float o = Saturate(u * 2.f - 0.3f);
        o = o * o * (3.f - 2.f * o);
        fr.F[0] = 280.f + 380.f * o; fr.F[1] = 800.f + 300.f * o; fr.F[2] = 2300.f; fr.F[3] = 3100.f;
        fr.BW[0] = 80.f; fr.BW[1] = 110.f; fr.BW[2] = 200.f; fr.BW[3] = 260.f;
    });
    reverb(b, 1.6f, 0.25f, 0.6f, 0.03f, 1.5f, 0.3f);
}
// Horse whinny: a high quavering neigh falling away, ending in a snort.
static void s_horseNeigh(Buf& b) {
    float d = b.rnd(1.f, 1.5f);
    float f0 = b.rnd(750.f, 1000.f);
    float vib = b.rnd(9.f, 13.f);
    vocal(b, 0.f, d, 0.04f, [&](float u, VFrame& fr) {
        float t = u * d;
        fr.f0 = f0 * (1.1f - 0.6f * u) * (1.f + 0.06f * sinf(kTwoPi * vib * t) * Min(1.f, u * 3.f));
        fr.amp = Min(1.f, u / 0.05f) * (u > 0.65f ? (1.f - u) / 0.35f : 1.f);
        fr.breath = 0.3f;
        fr.bright = 0.85f;
        fr.rough = 0.35f;
        fr.F[0] = 800.f; fr.F[1] = 1500.f; fr.F[2] = 2700.f; fr.F[3] = 3700.f;
        fr.BW[0] = 150.f; fr.BW[1] = 180.f; fr.BW[2] = 240.f; fr.BW[3] = 300.f;
    });
    noise(b, d + 0.05f, 0.3f, 0.5f, 0.01f, 0.08f, FBP, 900.f, 500.f, 0.1f, 0.9f, 1);
    reverb(b, 1.4f, 0.22f, 0.5f, 0.02f, 1.3f, 0.3f);
}
// Parakeet squawks: harsh bright screeches in a quick burst.
static void s_parrotSquawk(Buf& b) {
    int n = b.irnd(1, 3);
    float t = 0.f;
    for (int k = 0; k < n; k++) {
        float d = b.rnd(0.1f, 0.24f);
        float f0 = b.rnd(1500.f, 2300.f);
        vocal(b, t, d, 0.06f, [&](float u, VFrame& fr) {
            fr.f0 = f0 * (1.f + 0.2f * sinf(kPi * u) - 0.15f * u);
            fr.amp = Min(1.f, u / 0.08f) * (u > 0.7f ? (1.f - u) / 0.3f : 1.f);
            fr.breath = 0.2f;
            fr.bright = 1.f;
            fr.rough = 0.7f;
            fr.F[0] = 1600.f; fr.F[1] = 2900.f; fr.F[2] = 4200.f; fr.F[3] = 5600.f;
            fr.BW[0] = 250.f; fr.BW[1] = 300.f; fr.BW[2] = 380.f; fr.BW[3] = 450.f;
        });
        t += d + b.rnd(0.05f, 0.14f);
    }
    reverb(b, 0.8f, 0.12f, 0.5f, 0.01f, 1.f, 0.2f);
}
// Dolphin: a gliding whistle and a quick click train.
static void s_dolphinCall(Buf& b) {
    float d = b.rnd(0.35f, 0.7f);
    float fA = b.rnd(5000.f, 7000.f), fB = b.rnd(8000.f, 12000.f);
    int n = S(d);
    b.ensure(n);
    float ph = 0.f, wig = b.rnd(4.f, 9.f);
    for (int i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        float f = Lerp(fA, fB, sinf(kPi * u * 0.8f)) * (1.f + 0.05f * sinf(kTwoPi * wig * u * d));
        ph += f / SR;
        ph -= floorf(ph);
        float e = sinf(kPi * u);
        b.L[(size_t)i] += sinWrapped(ph) * e * 0.35f;
    }
    int clicks = b.irnd(6, 16);
    float t = d + b.rnd(0.05f, 0.2f), rate = b.rnd(35.f, 80.f);
    for (int k = 0; k < clicks; k++) {
        click(b, t, 0.3f, 3);
        noise(b, t, 0.006f, 0.15f, 0.0002f, 0.002f, FHP, 6000.f);
        t += 1.f / rate;
    }
    reverb(b, 0.5f, 0.15f, 0.3f, 0.005f, 0.6f, 0.2f);
}
// Breath of a surfacing dolphin / manatee: an explosive "pfff" through the blowhole.
static void s_animalBlow(Buf& b) {
    float d = b.rnd(0.3f, 0.5f);
    noise(b, 0.f, d, 0.8f, 0.006f, d * 0.35f, FBP, b.rnd(1100.f, 1600.f), b.rnd(600.f, 900.f), d * 0.5f, 0.7f, 1);
    noise(b, 0.f, d * 0.6f, 0.4f, 0.004f, d * 0.2f, FHP, 3500.f, -1.f, 0.1f, 0.707f, 0);
    noise(b, 0.f, d, 0.3f, 0.01f, d * 0.3f, FLP, 300.f, -1.f, 0.1f, 0.8f, 2);
    reverb(b, 0.6f, 0.12f, 0.5f, 0.01f, 1.f, 0.2f);
}
// Raccoon chitter: rapid squeaky churrs.
static void s_raccoonChitter(Buf& b) {
    int n = b.irnd(8, 18);
    float t = 0.f, rate = b.rnd(18.f, 28.f);
    float f0 = b.rnd(1300.f, 1900.f);
    for (int k = 0; k < n; k++) {
        float d = 0.6f / rate;
        vocal(b, t, d, 0.05f, [&](float u, VFrame& fr) {
            fr.f0 = f0 * (1.f + 0.2f * sinf(kPi * u));
            fr.amp = sinf(kPi * u) * (0.6f + 0.4f * (float)((k * 7) % 3) / 2.f);
            fr.breath = 0.3f;
            fr.bright = 0.8f;
            fr.rough = 0.5f;
            fr.F[0] = 1400.f; fr.F[1] = 2600.f; fr.F[2] = 3800.f; fr.F[3] = 5000.f;
            fr.BW[0] = 250.f; fr.BW[1] = 300.f; fr.BW[2] = 360.f; fr.BW[3] = 420.f;
        });
        t += 1.f / rate * b.rnd(0.85f, 1.15f);
    }
    reverb(b, 0.4f, 0.08f, 0.5f, 0.01f, 0.7f, 0.2f);
}
// Deer alarm snort: a sharp nasal blast.
static void s_deerSnort(Buf& b) {
    float d = b.rnd(0.2f, 0.32f);
    noise(b, 0.f, d, 1.f, 0.003f, d * 0.3f, FBP, b.rnd(1300.f, 1800.f), b.rnd(700.f, 1000.f), d * 0.5f, 1.1f, 1);
    tone(b, 0.f, d * 0.6f, 180.f, 130.f, d * 0.3f, 0.25f, 0.004f, d * 0.2f);
    if (b.chance(0.5f)) noise(b, d + b.rnd(0.15f, 0.3f), d * 0.8f, 0.6f, 0.003f, d * 0.25f, FBP, 1400.f, 800.f, 0.1f, 1.f, 1);
    reverb(b, 1.2f, 0.2f, 0.5f, 0.02f, 1.3f, 0.3f);
}
// Boat-tailed grackle: harsh "chek" notes, a wooden rattle, and now and then the rising rusty-hinge squeal.
static void s_grackleCall(Buf& b) {
    float t = 0.f;
    int cheks = b.irnd(1, 3);
    for (int k = 0; k < cheks; k++) {
        noise(b, t, 0.05f, 0.7f, 0.002f, 0.015f, FBP, b.rnd(2200.f, 2900.f), -1.f, 0.1f, 2.2f);
        tone(b, t, 0.05f, b.rnd(1100.f, 1400.f), 900.f, 0.04f, 0.35f, 0.002f, 0.015f, 0.f, 0.5f, 0.35f);
        t += b.rnd(0.12f, 0.2f);
    }
    if (b.chance(0.5f)) {   // rattle
        int n = b.irnd(6, 11);
        float rate = b.rnd(22.f, 32.f);
        for (int k = 0; k < n; k++) {
            click(b, t, 0.35f, 5);
            noise(b, t, 0.012f, 0.3f, 0.001f, 0.004f, FBP, 3000.f);
            t += 1.f / rate;
        }
        t += 0.08f;
    }
    if (b.chance(0.6f)) {   // rising squeal
        float d = b.rnd(0.25f, 0.4f);
        tone(b, t, d, b.rnd(1300.f, 1700.f), b.rnd(3400.f, 4200.f), d * 0.9f, 0.45f, 0.01f, d * 0.6f, 0.f, 0.35f, 0.2f);
        noise(b, t, d, 0.12f, 0.01f, d * 0.5f, FBP, 2500.f, 4500.f, d, 1.5f);
    }
    reverb(b, 0.9f, 0.14f, 0.5f, 0.01f, 1.f, 0.25f);
}
// Sanderling flock calls: quick, thin "twick" notes.
static void s_shorebirdPeep(Buf& b) {
    float t = 0.f;
    int n = b.irnd(2, 4);
    for (int k = 0; k < n; k++) {
        float f = b.rnd(4400.f, 5400.f);
        tone(b, t, 0.05f, f, f * 0.8f, 0.04f, 0.55f, 0.002f, 0.018f, b.rnd(-0.3f, 0.3f), 0.12f);
        t += b.rnd(0.07f, 0.14f);
    }
    reverb(b, 0.7f, 0.12f, 0.5f, 0.01f, 1.f, 0.2f);
}

// ---------------------------------------------------------------------------------------------
// Public transit
// Door-closing chime of the SkyLine trains and stations: three descending metallophone notes (E5 C5 G4)
static void s_transitChime(Buf& b) {
    const float notes[3] = {76.f, 72.f, 67.f};
    for (int i = 0; i < 3; i++) {
        float f = midiToHz(notes[i]);
        float t = i * 0.34f;
        Mode m[4] = {{f, 0.9f, 1.f}, {f * 2.003f, 0.45f, 0.22f}, {f * 3.94f, 0.12f, 0.07f}, {f * 0.5f, 0.6f, 0.08f}};
        modes(b, t, 0.34f, m, 4);
        click(b, t, 0.05f, 6);
    }
    lowpass(b, 6000.f);
    reverb(b, 1.4f, 0.22f, 0.45f, 0.02f, 1.3f, 0.25f);
}
// Sliding plug doors: motor whine and seal hiss, then the leaves thud home against the rubber seals
static void s_trainDoors(Buf& b) {
    float d = b.rnd(1.15f, 1.35f);
    float f0 = b.rnd(170.f, 200.f);
    tone(b, 0.f, d, f0, f0 * 1.25f, d * 0.8f, 0.11f, 0.08f, d * 1.5f, 0.f, 0.5f, 0.25f);
    noise(b, 0.f, d * 0.7f, 0.35f, 0.02f, 0.12f, FBP, 2400.f, 1300.f, d * 0.4f, 0.8f, 1);
    noise(b, 0.05f, d, 0.18f, 0.2f, d * 0.6f, FBP, 900.f, 1500.f, d * 0.6f, 1.2f, 1);
    Mode th[3] = {{b.rnd(85.f, 105.f), 0.07f, 1.f}, {b.rnd(210.f, 250.f), 0.04f, 0.6f}, {b.rnd(900.f, 1100.f), 0.015f, 0.25f}};
    modes(b, d, 0.55f, th, 3);
    click(b, d, 0.25f, 12);
    noise(b, d, 0.08f, 0.2f, 0.001f, 0.02f, FLP, 1200.f, -1.f, 0.1f, 0.7f, 2);
    reverb(b, 0.5f, 0.12f, 0.5f, 0.01f, 0.6f, 0.2f);
}
// One wheelset over a rail joint: heavy knock with a short ring of the rail
static void s_railClack(Buf& b) {
    float p = b.rnd(0.9f, 1.12f);
    Mode m[5] = {{165.f * p, 0.06f, 1.f}, {410.f * p, 0.04f, 0.7f}, {1120.f * p, 0.022f, 0.35f}, {2650.f * p, 0.012f, 0.2f}, {5200.f * p, 0.006f, 0.08f}};
    modes(b, 0.f, 0.7f, m, 5);
    click(b, 0.f, 0.5f, 10);
    noise(b, 0.f, 0.09f, 0.35f, 0.0005f, 0.025f, FLP, 900.f, 300.f, 0.05f, 0.7f, 2);
    if (b.chance(0.5f)) {
        Mode r[2] = {{b.rnd(3100.f, 3600.f), 0.12f, 0.12f}, {b.rnd(4700.f, 5300.f), 0.08f, 0.06f}};
        modes(b, 0.005f, 0.3f, r, 2);
    }
}
// Ferry horn: a deep reed-like note with a slightly beating second voice and a harbour echo
static void s_shipHorn(Buf& b) {
    float f = b.rnd(112.f, 128.f), d = b.rnd(2.1f, 2.5f);
    const float harm[6] = {1.f, 0.62f, 0.46f, 0.3f, 0.2f, 0.12f};
    for (int h = 0; h < 6; h++) {
        tone(b, 0.f, d, f * (h + 1) * 1.01f, f * (h + 1), 0.4f, 0.16f * harm[h], 0.18f, 6.f);
        tone(b, 0.02f, d, f * (h + 1) * 1.006f, f * (h + 1) * 1.004f, 0.4f, 0.08f * harm[h], 0.22f, 6.f);
    }
    noise(b, 0.f, d, 0.05f, 0.15f, 5.f, FBP, f * 6.f, -1.f, 0.1f, 1.4f, 1);
    saturate(b, 1.6f);
    lowpass(b, 2200.f);
    echoes(b, 3, 0.9f, 0.7f, 0.35f, 0.55f, 900.f, d);
    reverb(b, 2.4f, 0.3f, 0.6f, 0.04f, 1.8f, 0.3f);
}

#include "sfx_guns.cpp"
#include "sfx_amb.cpp"

// ---------------------------------------------------------------------------------------------
// Definitions table (order must match BankId)
#define W Bus::World
#define U Bus::Ui
static const SoundDef kDefs[BANK_COUNT] = {
    // name              bus prio vars gain  ref    max     rev   pvar  air
    {"none",              W, 0,   1, 0.f,   1.f,   1.f,    0.f,  0.f,  1.f},
    {"step_concrete",     W, 40,  6, 0.676f, 1.5f,  35.f,   0.12f, 0.05f, 1.f},
    {"step_grass",        W, 40,  6, 0.361f, 1.5f,  30.f,   0.08f, 0.05f, 1.f},
    {"step_wood",         W, 40,  6, 0.380f, 1.5f,  35.f,   0.12f, 0.05f, 1.f},
    {"step_metal",        W, 40,  6, 0.286f, 1.5f,  40.f,   0.14f, 0.05f, 1.f},
    {"step_sand",         W, 40,  6, 0.305f, 1.5f,  25.f,   0.06f, 0.05f, 1.f},
    {"step_water",        W, 40,  6, 0.395f, 1.5f,  30.f,   0.08f, 0.05f, 1.f},
    {"step_gravel",       W, 40,  6, 0.454f, 1.5f,  35.f,   0.1f,  0.05f, 1.f},
    {"pistol",            W, 200, 1, 1.303f,  4.f,   650.f,  0.35f, 0.03f, 0.7f},
    {"smg",               W, 200, 1, 1.582f,  4.f,   600.f,  0.3f,  0.03f, 0.7f},
    {"rifle",             W, 200, 1, 1.288f,  5.f,   900.f,  0.35f, 0.03f, 0.7f},
    {"shotgun",           W, 200, 1, 1.059f,  5.f,   800.f,  0.35f, 0.03f, 0.7f},
    {"sniper",            W, 210, 1, 1.216f,  6.f,   1500.f, 0.4f,  0.02f, 0.6f},
    {"rocket_launch",     W, 210, 3, 1.349f,  5.f,   700.f,  0.35f, 0.03f, 0.7f},
    {"silenced",          W, 150, 1, 0.733f, 2.f,   60.f,   0.1f,  0.04f, 1.f},
    {"reload",            W, 100, 3, 1.057f,  1.5f,  25.f,   0.1f,  0.03f, 1.f},
    {"dry_fire",          W, 90,  3, 1.600f,  1.f,   15.f,   0.05f, 0.04f, 1.f},
    {"weapon_switch",     W, 90,  3, 0.628f, 1.f,   15.f,   0.05f, 0.04f, 1.f},
    {"shell_casing",      W, 30,  6, 0.315f, 1.f,   20.f,   0.08f, 0.06f, 1.f},
    {"bullet_whiz",       W, 150, 5, 1.122f,  3.f,   60.f,   0.1f,  0.06f, 1.f},
    {"impact_concrete",   W, 110, 6, 1.423f,  2.f,   120.f,  0.2f,  0.06f, 1.f},
    {"impact_metal",      W, 110, 6, 0.576f, 2.f,   140.f,  0.2f,  0.06f, 1.f},
    {"impact_glass",      W, 110, 6, 0.967f, 2.f,   100.f,  0.2f,  0.06f, 1.f},
    {"impact_flesh",      W, 120, 6, 0.689f,  1.5f,  60.f,   0.1f,  0.06f, 1.f},
    {"impact_dirt",       W, 100, 6, 0.490f, 2.f,   90.f,   0.12f, 0.06f, 1.f},
    {"impact_water",      W, 100, 6, 0.677f, 2.f,   80.f,   0.12f, 0.06f, 1.f},
    {"impact_wood",       W, 100, 6, 0.646f, 2.f,   100.f,  0.15f, 0.06f, 1.f},
    {"explosion",         W, 250, 3, 1.0f,  15.f,  2500.f, 0.45f, 0.06f, 0.5f},
    {"explosion_small",   W, 240, 4, 0.804f,  8.f,   1500.f, 0.4f,  0.06f, 0.6f},
    {"grenade_bounce",    W, 120, 4, 0.472f,  2.f,   50.f,   0.12f, 0.05f, 1.f},
    {"punch",             W, 120, 6, 1.183f,  1.5f,  40.f,   0.1f,  0.06f, 1.f},
    {"kick",              W, 120, 6, 1.210f, 1.5f,  40.f,   0.1f,  0.06f, 1.f},
    {"body_fall",         W, 100, 5, 0.523f,  2.f,   50.f,   0.12f, 0.05f, 1.f},
    {"grunt_male",        W, 110, 6, 0.468f, 2.f,   40.f,   0.1f,  0.04f, 1.f},
    {"grunt_female",      W, 110, 6, 0.398f, 2.f,   40.f,   0.1f,  0.04f, 1.f},
    {"scream_male",       W, 150, 5, 0.586f,  4.f,   160.f,  0.25f, 0.04f, 1.f},
    {"scream_female",     W, 150, 5, 0.349f,  4.f,   160.f,  0.25f, 0.04f, 1.f},
    {"car_door_open",     W, 100, 4, 0.555f,  2.f,   50.f,   0.1f,  0.04f, 1.f},
    {"car_door_close",    W, 110, 4, 0.624f,  2.f,   70.f,   0.12f, 0.04f, 1.f},
    {"car_crash_light",   W, 180, 4, 0.721f,  4.f,   220.f,  0.25f, 0.05f, 1.f},
    {"car_crash_heavy",   W, 220, 3, 1.0f,  6.f,   400.f,  0.3f,  0.05f, 0.8f},
    {"glass_break",       W, 160, 4, 1.600f,  4.f,   150.f,  0.25f, 0.05f, 1.f},
    {"engine_start",      W, 150, 3, 0.324f,  4.f,   100.f,  0.15f, 0.03f, 1.f},
    {"engine_stop",       W, 120, 3, 0.406f,  4.f,   80.f,   0.12f, 0.03f, 1.f},
    {"gear_shift",        W, 100, 4, 0.481f,  2.f,   40.f,   0.08f, 0.05f, 1.f},
    {"tire_pop",          W, 170, 3, 1.600f,  4.f,   250.f,  0.25f, 0.04f, 1.f},
    {"metal_scrape",      W, 150, 4, 0.617f,  4.f,   150.f,  0.2f,  0.05f, 1.f},
    {"bike_kickstand",    W, 80,  3, 0.361f,  1.5f,  30.f,   0.08f, 0.05f, 1.f},
    {"splash_small",      W, 110, 5, 0.800f,  2.f,   80.f,   0.12f, 0.06f, 1.f},
    {"splash_big",        W, 170, 4, 0.9f,  5.f,   200.f,  0.2f,  0.05f, 1.f},
    {"boat_slam",         W, 140, 4, 0.8f,  4.f,   150.f,  0.15f, 0.05f, 1.f},
    {"thunder",           W, 200, 3, 1.0f,  600.f, 15000.f,0.3f,  0.08f, 0.15f},
    {"dog_bark",          W, 90,  5, 0.842f,  4.f,   220.f,  0.2f,  0.05f, 1.f},
    {"seagull",           W, 60,  5, 0.379f,  5.f,   220.f,  0.2f,  0.06f, 1.f},
    {"bird_chirp",        W, 40,  6, 0.234f, 4.f,   120.f,  0.2f,  0.08f, 1.f},
    {"car_alarm_chirp",   W, 90,  3, 0.210f,  3.f,   120.f,  0.2f,  0.03f, 1.f},
    {"door_buzz",         W, 70,  3, 0.242f,  2.f,   30.f,   0.1f,  0.03f, 1.f},
    {"cash_register",     W, 80,  3, 0.384f,  2.f,   30.f,   0.1f,  0.04f, 1.f},
    {"bell",              W, 120, 2, 0.673f,  20.f,  1500.f, 0.3f,  0.03f, 0.6f},
    {"heli_flyby",        W, 150, 2, 1.0f,  40.f,  2500.f, 0.3f,  0.03f, 0.3f},
    {"ui_move",           U, 255, 3, 0.256f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"ui_select",         U, 255, 2, 0.213f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"ui_back",           U, 255, 2, 0.248f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"ui_error",          U, 255, 2, 0.116f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"ui_notify",         U, 255, 2, 0.283f,  1.f,   1.f,    0.f,   0.f,  0.f},
    {"ui_text",           U, 255, 4, 0.267f, 1.f,   1.f,    0.f,   0.02f, 0.f},
    {"phone_ring",        U, 255, 1, 0.350f,  1.f,   1.f,    0.f,   0.f,  0.f},
    {"phone_msg",         U, 255, 1, 0.287f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"pickup_cash",       U, 255, 3, 0.349f, 1.f,   1.f,    0.f,   0.02f, 0.f},
    {"pickup_weapon",     U, 255, 2, 0.586f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"pickup_health",     U, 255, 2, 0.301f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"pickup_collectible",U, 255, 2, 0.420f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"checkpoint",        U, 255, 2, 0.374f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"mission_passed",    U, 255, 2, 0.813f,  1.f,   1.f,    0.f,   0.f,  0.f},
    {"mission_failed",    U, 255, 1, 0.405f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"wasted",            U, 255, 1, 0.507f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"busted",            U, 255, 1, 0.842f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"wanted_up",         U, 255, 2, 0.500f,  1.f,   1.f,    0.f,   0.f,  0.f},
    {"wanted_lost",       U, 255, 1, 0.384f,  1.f,   1.f,    0.f,   0.f,  0.f},
    {"camera_shutter",    U, 255, 3, 0.738f, 1.f,   1.f,    0.f,   0.02f, 0.f},
    {"purchase",          U, 255, 2, 0.362f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"race_countdown",    U, 255, 1, 0.272f,  1.f,   1.f,    0.f,   0.f,  0.f},
    {"race_go",           U, 255, 1, 0.408f, 1.f,   1.f,    0.f,   0.f,  0.f},
    {"whoosh",            W, 90,  4, 1.f,    1.2f,  25.f,   0.05f, 0.08f, 1.f},
    {"wing_flap",         W, 60,  4, 0.7f,  3.f,   120.f,  0.15f, 0.08f, 1.f},
    {"pigeon_coo",        W, 30,  4, 0.35f, 2.f,   40.f,   0.1f,  0.06f, 1.f},
    {"gator_hiss",        W, 120, 3, 0.9f,  3.f,   60.f,   0.1f,  0.05f, 1.f},
    {"gator_bellow",      W, 90,  2, 1.f,   10.f,  500.f,  0.35f, 0.05f, 0.6f},
    {"jaw_snap",          W, 130, 3, 1.f,   3.f,   80.f,   0.15f, 0.05f, 1.f},
    {"dog_growl",         W, 90,  3, 0.7f,  3.f,   70.f,   0.1f,  0.05f, 1.f},
    {"dog_yelp",          W, 100, 4, 0.8f,  4.f,   180.f,  0.2f,  0.06f, 1.f},
    {"cat_meow",          W, 70,  4, 0.5f,  3.f,   90.f,   0.15f, 0.06f, 1.f},
    {"cow_moo",           W, 70,  3, 1.f,   8.f,   500.f,  0.3f,  0.05f, 0.8f},
    {"horse_neigh",       W, 80,  3, 0.9f,  8.f,   450.f,  0.3f,  0.04f, 0.8f},
    {"parrot_squawk",     W, 50,  5, 0.6f,  5.f,   250.f,  0.2f,  0.07f, 1.f},
    {"heron_call",        W, 50,  3, 0.8f,  8.f,   350.f,  0.3f,  0.05f, 1.f},
    {"dolphin_call",      W, 40,  3, 0.35f, 4.f,   120.f,  0.15f, 0.08f, 1.f},
    {"animal_blow",       W, 50,  3, 0.6f,  4.f,   120.f,  0.15f, 0.08f, 1.f},
    {"raccoon_chitter",   W, 40,  3, 0.4f,  2.f,   50.f,   0.1f,  0.08f, 1.f},
    {"deer_snort",        W, 80,  3, 0.9f,  5.f,   250.f,  0.25f, 0.06f, 1.f},
    {"grackle_call",      W, 50,  4, 0.55f, 5.f,   200.f,  0.2f,  0.07f, 1.f},
    {"shorebird_peep",    W, 40,  4, 0.35f, 3.f,   90.f,   0.15f, 0.08f, 1.f},
    {"transit_chime",     W, 130, 1, 0.55f, 6.f,   90.f,   0.3f,  0.f,   1.f},
    {"train_doors",       W, 90,  3, 0.7f,  3.f,   60.f,   0.2f,  0.04f, 1.f},
    {"rail_clack",        W, 70,  6, 0.9f,  4.f,   220.f,  0.25f, 0.08f, 0.8f},
    {"ship_horn",         W, 200, 2, 1.0f,  40.f,  3500.f, 0.45f, 0.02f, 0.4f},
    {"revolver",          W, 200, 1, 1.0f,  4.f,   700.f,  0.35f, 0.03f, 0.7f},
    {"amb_cricket",       W, 20,  4, 0.2f,  3.f,   60.f,   0.2f,  0.04f, 1.f},
    {"amb_treefrog",      W, 20,  4, 0.25f, 4.f,   90.f,   0.2f,  0.05f, 1.f},
    {"amb_bullfrog",      W, 20,  3, 0.4f,  5.f,   160.f,  0.25f, 0.05f, 1.f},
    {"amb_owl",           W, 20,  3, 0.3f,  8.f,   300.f,  0.3f,  0.03f, 1.f},
    {"amb_bird_song",     W, 25,  4, 0.3f,  6.f,   160.f,  0.25f, 0.06f, 1.f},
    {"amb_heron",         W, 20,  3, 0.35f, 8.f,   260.f,  0.3f,  0.05f, 1.f},
    {"amb_horn_distant",  W, 30,  4, 0.5f,  8.f,   900.f,  0.4f,  0.04f, 0.8f},
    {"amb_siren_distant", W, 30,  2, 0.6f,  20.f,  3000.f, 0.4f,  0.03f, 0.4f},
    {"amb_bubbles",       W, 20,  4, 0.35f, 2.f,   30.f,   0.1f,  0.08f, 1.f},
    {"amb_dog_distant",   W, 25,  3, 0.5f,  6.f,   700.f,  0.35f, 0.05f, 0.8f},
    {"amb_water_lap",     W, 20,  4, 0.3f,  3.f,   50.f,   0.15f, 0.06f, 1.f},
    {"amb_crow",          W, 20,  3, 0.35f, 6.f,   300.f,  0.3f,  0.05f, 1.f},
    {"amb_airboat",       W, 35,  2, 0.55f, 1.f,   1.f,    0.2f,  0.04f, 1.f},
    {"amb_boat_pass",     W, 30,  2, 0.45f, 1.f,   1.f,    0.2f,  0.04f, 1.f},
    {"amb_train_pass",    W, 30,  2, 0.5f,  1.f,   1.f,    0.2f,  0.03f, 1.f},
    {"amb_crane",         W, 30,  3, 0.5f,  25.f,  900.f,  0.35f, 0.05f, 0.7f},
    {"amb_container",     W, 30,  3, 0.7f,  40.f,  1500.f, 0.4f,  0.05f, 0.6f},
    {"amb_reverse_beep",  W, 25,  2, 0.3f,  10.f,  500.f,  0.3f,  0.03f, 0.8f},
    {"amb_construction",  W, 25,  3, 0.5f,  15.f,  700.f,  0.35f, 0.05f, 0.7f},
    {"amb_pig_frog",      W, 20,  3, 0.45f, 6.f,   200.f,  0.2f,  0.05f, 1.f},
    {"amb_chorus_frog",   W, 20,  3, 0.3f,  4.f,   120.f,  0.2f,  0.05f, 1.f},
    {"amb_limpkin",       W, 20,  3, 0.45f, 10.f,  500.f,  0.3f,  0.04f, 0.8f},
    {"amb_blackbird",     W, 20,  3, 0.35f, 5.f,   200.f,  0.2f,  0.05f, 1.f},
    {"amb_mosquito",      W, 20,  3, 0.12f, 1.f,   1.f,    0.f,   0.05f, 1.f},
    {"amb_halyard",       W, 20,  3, 0.35f, 5.f,   120.f,  0.25f, 0.05f, 1.f},
    {"amb_buoy_bell",     W, 20,  2, 0.4f,  20.f,  800.f,  0.35f, 0.02f, 0.8f},
    {"amb_thunder_close", W, 210, 2, 1.0f,  1.f,   1.f,    0.3f,  0.05f, 1.f},
    {"amb_thunder_mid",   W, 200, 3, 0.95f, 1.f,   1.f,    0.3f,  0.05f, 1.f},
    {"amb_thunder_far",   W, 190, 3, 0.8f,  1.f,   1.f,    0.3f,  0.05f, 1.f},
    // gunfire layers (Mixer::startGunshot)
    {"gun_near_pistol",   W, 200, 4, 1.06f, 5.f,   700.f,  0.4f,  0.03f, 1.2f},
    {"gun_near_revolver", W, 200, 4, 1.13f, 5.f,   800.f,  0.4f,  0.03f, 1.2f},
    {"gun_near_smg",      W, 200, 4, 1.01f, 5.f,   650.f,  0.4f,  0.03f, 1.2f},
    {"gun_near_rifle",    W, 200, 4, 1.13f, 6.f,   950.f,  0.4f,  0.03f, 1.2f},
    {"gun_near_shotgun",  W, 200, 4, 1.18f, 6.f,   800.f,  0.4f,  0.03f, 1.2f},
    {"gun_near_sniper",   W, 210, 4, 1.23f, 7.f,   1300.f, 0.4f,  0.02f, 1.2f},
    {"gun_fp_pistol",     W, 240, 3, 1.70f, 1.f,   60.f,   0.45f, 0.02f, 1.f},
    {"gun_fp_revolver",   W, 240, 3, 1.70f, 1.f,   60.f,   0.45f, 0.02f, 1.f},
    {"gun_fp_smg",        W, 240, 3, 1.70f, 1.f,   60.f,   0.45f, 0.02f, 1.f},
    {"gun_fp_rifle",      W, 240, 3, 1.70f, 1.f,   60.f,   0.45f, 0.02f, 1.f},
    {"gun_fp_shotgun",    W, 240, 3, 1.70f, 1.f,   60.f,   0.45f, 0.02f, 1.f},
    {"gun_fp_sniper",     W, 240, 3, 1.70f, 1.f,   60.f,   0.45f, 0.02f, 1.f},
    {"gun_mech_pistol",   W, 190, 3, 0.45f, 1.5f,  30.f,   0.12f, 0.04f, 1.f},
    {"gun_mech_revolver", W, 190, 3, 0.40f, 1.5f,  30.f,   0.12f, 0.04f, 1.f},
    {"gun_mech_smg",      W, 190, 3, 0.45f, 1.5f,  30.f,   0.12f, 0.04f, 1.f},
    {"gun_mech_rifle",    W, 190, 3, 0.45f, 1.5f,  30.f,   0.12f, 0.04f, 1.f},
    {"gun_mech_shotgun",  W, 190, 3, 0.50f, 1.5f,  30.f,   0.12f, 0.03f, 1.f},
    {"gun_mech_sniper",   W, 190, 3, 0.50f, 1.5f,  30.f,   0.12f, 0.03f, 1.f},
    {"gun_sup_pistol",    W, 190, 4, 1.01f, 2.f,   150.f,  0.3f,  0.04f, 1.f},
    {"gun_sup_revolver",  W, 190, 4, 1.13f, 3.f,   300.f,  0.35f, 0.04f, 0.9f},
    {"gun_sup_smg",       W, 190, 4, 0.95f, 2.f,   150.f,  0.3f,  0.04f, 1.f},
    {"gun_sup_rifle",     W, 190, 4, 1.20f, 3.f,   350.f,  0.35f, 0.04f, 0.9f},
    {"gun_sup_shotgun",   W, 190, 4, 1.13f, 2.f,   200.f,  0.3f,  0.04f, 1.f},
    {"gun_sup_sniper",    W, 200, 4, 1.26f, 3.f,   450.f,  0.35f, 0.03f, 0.9f},
    {"gun_far_urban_light", W, 180, 3, 1.80f, 30.f, 1800.f, 0.12f, 0.04f, 0.5f},
    {"gun_far_urban_heavy", W, 180, 3, 1.89f, 30.f, 2300.f, 0.12f, 0.04f, 0.5f},
    {"gun_far_urban_big",   W, 185, 3, 1.98f, 34.f, 3000.f, 0.12f, 0.03f, 0.45f},
    {"gun_far_open_light",  W, 180, 3, 1.80f, 30.f, 1800.f, 0.12f, 0.04f, 0.5f},
    {"gun_far_open_heavy",  W, 180, 3, 1.89f, 30.f, 2300.f, 0.12f, 0.04f, 0.5f},
    {"gun_far_open_big",    W, 185, 3, 1.98f, 34.f, 3000.f, 0.12f, 0.03f, 0.45f},
    {"gun_crack",         W, 200, 4, 1.20f, 3.f,   60.f,   0.15f, 0.05f, 1.f},
};
#undef W
#undef U
static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == BANK_COUNT, "SoundDef table does not match BankId");

static bool isStereo(int id) {
    switch (id) {
        case SFX_UI_SELECT: case SFX_UI_BACK: case SFX_UI_NOTIFY: case SFX_PHONE_RING: case SFX_PHONE_MSG:
        case SFX_PICKUP_CASH: case SFX_PICKUP_HEALTH: case SFX_PICKUP_COLLECTIBLE: case SFX_CHECKPOINT:
        case SFX_MISSION_PASSED: case SFX_MISSION_FAILED: case SFX_WASTED: case SFX_BUSTED: case SFX_WANTED_UP:
        case SFX_WANTED_LOST: case SFX_PURCHASE:
            return true;
        case AMB_AIRBOAT: case AMB_BOAT_PASS: case AMB_TRAIN_PASS: case AMB_MOSQUITO: case AMB_THUNDER_CLOSE: case AMB_THUNDER_MID:
        case AMB_THUNDER_FAR:
            return true;
        default: return id >= GUN_FP && id < GUN_MECH;
    }
}

static void synthesize(int id, int var, Buf& b) {
    switch (id) {
        case SFX_STEP_CONCRETE: s_step(b, SurfConcrete); break;
        case SFX_STEP_GRASS: s_step(b, SurfGrass); break;
        case SFX_STEP_WOOD: s_step(b, SurfWood); break;
        case SFX_STEP_METAL: s_step(b, SurfMetal); break;
        case SFX_STEP_SAND: s_step(b, SurfSand); break;
        case SFX_STEP_WATER: s_step(b, SurfWater); break;
        case SFX_STEP_GRAVEL: s_step(b, SurfGravel); break;
        case SFX_ROCKET_LAUNCH: s_weapon(b, id); break;
        case SFX_RELOAD: s_reload(b); break;
        case SFX_DRY_FIRE: s_dryFire(b); break;
        case SFX_WEAPON_SWITCH: s_weaponSwitch(b); break;
        case SFX_SHELL_CASING: s_shellCasing(b); break;
        case SFX_BULLET_WHIZ: s_bulletWhiz(b); break;
        case SFX_IMPACT_CONCRETE: case SFX_IMPACT_METAL: case SFX_IMPACT_GLASS: case SFX_IMPACT_FLESH:
        case SFX_IMPACT_DIRT: case SFX_IMPACT_WATER: case SFX_IMPACT_WOOD: s_impact(b, id); break;
        case SFX_EXPLOSION: s_explosion(b, true); break;
        case SFX_EXPLOSION_SMALL: s_explosion(b, false); break;
        case SFX_GRENADE_BOUNCE: s_grenadeBounce(b); break;
        case SFX_PUNCH: s_punch(b, false); break;
        case SFX_KICK: s_punch(b, true); break;
        case SFX_BODY_FALL: s_bodyFall(b); break;
        case SFX_GRUNT_MALE: s_grunt(b, false); break;
        case SFX_GRUNT_FEMALE: s_grunt(b, true); break;
        case SFX_SCREAM_MALE: s_scream(b, false); break;
        case SFX_SCREAM_FEMALE: s_scream(b, true); break;
        case SFX_CAR_DOOR_OPEN: s_carDoorOpen(b); break;
        case SFX_CAR_DOOR_CLOSE: s_carDoorClose(b); break;
        case SFX_CAR_CRASH_LIGHT: s_carCrash(b, false); break;
        case SFX_CAR_CRASH_HEAVY: s_carCrash(b, true); break;
        case SFX_GLASS_BREAK: s_glassBreak(b); break;
        case SFX_ENGINE_START: s_engineStart(b); break;
        case SFX_ENGINE_STOP: s_engineStop(b); break;
        case SFX_GEAR_SHIFT: s_gearShift(b); break;
        case SFX_TIRE_POP: s_tirePop(b); break;
        case SFX_METAL_SCRAPE: s_metalScrape(b); break;
        case SFX_BIKE_KICKSTAND: s_kickstand(b); break;
        case SFX_SPLASH_SMALL: s_splash(b, false); break;
        case SFX_SPLASH_BIG: s_splash(b, true); break;
        case SFX_BOAT_SLAM: s_boatSlam(b); break;
        case SFX_THUNDER: s_thunder(b, var == 0); break;
        case SFX_DOG_BARK: s_dogBark(b, false); break;
        case SFX_SEAGULL: s_seagull(b); break;
        case SFX_BIRD_CHIRP: s_birdChirp(b); break;
        case SFX_CAR_ALARM_CHIRP: s_alarmChirp(b); break;
        case SFX_DOOR_BUZZ: s_doorBuzz(b); break;
        case SFX_CASH_REGISTER: s_cashRegister(b); break;
        case SFX_BELL: s_bell(b); break;
        case SFX_HELI_FLYBY: s_heliFlyby(b); break;
        case SFX_UI_MOVE: s_uiMove(b); break;
        case SFX_UI_SELECT: s_uiSelect(b); break;
        case SFX_UI_BACK: s_uiBack(b); break;
        case SFX_UI_ERROR: s_uiError(b); break;
        case SFX_UI_NOTIFY: s_uiNotify(b); break;
        case SFX_UI_TEXT: s_uiText(b); break;
        case SFX_PHONE_RING: s_phoneRing(b); break;
        case SFX_PHONE_MSG: s_phoneMsg(b); break;
        case SFX_PICKUP_CASH: s_pickupCash(b); break;
        case SFX_PICKUP_WEAPON: s_pickupWeapon(b); break;
        case SFX_PICKUP_HEALTH: s_pickupHealth(b); break;
        case SFX_PICKUP_COLLECTIBLE: s_pickupCollectible(b); break;
        case SFX_CHECKPOINT: s_checkpoint(b); break;
        case SFX_MISSION_PASSED: s_missionPassed(b, var); break;
        case SFX_MISSION_FAILED: s_missionFailed(b); break;
        case SFX_WASTED: s_wasted(b); break;
        case SFX_BUSTED: s_busted(b); break;
        case SFX_WANTED_UP: s_wantedUp(b); break;
        case SFX_WANTED_LOST: s_wantedLost(b); break;
        case SFX_CAMERA_SHUTTER: s_cameraShutter(b); break;
        case SFX_PURCHASE: s_purchase(b); break;
        case SFX_RACE_COUNTDOWN: s_beep(b, 880.f, 0.22f, false); break;
        case SFX_RACE_GO: s_beep(b, 1760.f, 0.75f, true); break;
        case SFX_WHOOSH: s_whoosh(b); break;
        case SFX_WING_FLAP: s_wingFlap(b); break;
        case SFX_PIGEON_COO: s_pigeonCoo(b); break;
        case SFX_GATOR_HISS: s_gatorHiss(b); break;
        case SFX_GATOR_BELLOW: s_gatorBellow(b); break;
        case SFX_JAW_SNAP: s_jawSnap(b); break;
        case SFX_DOG_GROWL: s_dogGrowl(b); break;
        case SFX_DOG_YELP: s_dogYelp(b); break;
        case SFX_CAT_MEOW: s_catMeow(b); break;
        case SFX_COW_MOO: s_cowMoo(b); break;
        case SFX_HORSE_NEIGH: s_horseNeigh(b); break;
        case SFX_PARROT_SQUAWK: s_parrotSquawk(b); break;
        case SFX_HERON_CALL: s_heron(b); break;
        case SFX_DOLPHIN_CALL: s_dolphinCall(b); break;
        case SFX_ANIMAL_BLOW: s_animalBlow(b); break;
        case SFX_RACCOON_CHITTER: s_raccoonChitter(b); break;
        case SFX_DEER_SNORT: s_deerSnort(b); break;
        case SFX_GRACKLE_CALL: s_grackleCall(b); break;
        case SFX_SHOREBIRD_PEEP: s_shorebirdPeep(b); break;
        case SFX_TRANSIT_CHIME: s_transitChime(b); break;
        case SFX_TRAIN_DOORS: s_trainDoors(b); break;
        case SFX_RAIL_CLACK: s_railClack(b); break;
        case SFX_SHIP_HORN: s_shipHorn(b); break;
        case AMB_CRICKET_CHIRP: s_cricket(b); break;
        case AMB_TREEFROG: s_treefrog(b); break;
        case AMB_BULLFROG: s_bullfrog(b); break;
        case AMB_OWL: s_owl(b); break;
        case AMB_BIRD_SONG: s_birdSong(b); break;
        case AMB_HERON: s_heron(b); break;
        case AMB_HORN_DISTANT: s_hornDistant(b); break;
        case AMB_SIREN_DISTANT: s_sirenDistant(b, var); break;
        case AMB_BUBBLES: s_bubbles(b); break;
        case AMB_DOG_DISTANT: s_dogBark(b, true); break;
        case AMB_WATER_LAP: s_waterLap(b); break;
        case AMB_CROW: s_crow(b); break;
        default:
            if (id >= AMB_AIRBOAT && id <= AMB_THUNDER_FAR) s_ambLayer(b, id, var);
            else if (id >= GUN_NEAR && id <= GUN_CRACK) s_gunLayer(b, id);
            else tone(b, 0.f, 0.01f, 440.f, 440.f, 0.f, 0.f, 0.001f, 0.01f);  // routed ids (guns): silent stub buffer
            break;
    }
}

// DC removal, tail trim, fades and peak normalization into the final bank buffer.
static void finish(Buf& b, SoundBuffer& out) {
    OnePoleHP hp;
    hp.set(12.f);
    for (auto& v : b.L) v = hp.process(v);
    if (b.stereo) {
        hp.reset();
        for (auto& v : b.R) v = hp.process(v);
    }
    int n = b.len();
    float pk = 0.f;
    for (int i = 0; i < n; i++) {
        pk = Max(pk, fabsf(b.L[(size_t)i]));
        if (b.stereo) pk = Max(pk, fabsf(b.R[(size_t)i]));
    }
    if (n <= 0 || pk < 1e-9f || !std::isfinite(pk)) {
        out.data.assign(2, (i16)0);
        out.channels = 1;
        out.frames = 2;
        return;
    }
    float thr = pk * 3.5e-3f;  // trim below ~-49 dB (the mixer's reverb provides environmental tails)
    int last = n - 1;
    while (last > 0 && fabsf(b.L[(size_t)last]) < thr && (!b.stereo || fabsf(b.R[(size_t)last]) < thr)) last--;
    int len = Min(n, last + S(0.02f));
    int fadeN = Min(len, S(0.03f));
    for (int i = 0; i < fadeN; i++) {
        float g = 0.5f - 0.5f * cosf(kPi * (float)i / (float)fadeN);
        b.L[(size_t)(len - 1 - i)] *= g;
        if (b.stereo) b.R[(size_t)(len - 1 - i)] *= g;
    }
    float g = 0.98f / pk * 32767.f;
    out.channels = b.stereo ? 2 : 1;
    out.frames = len;
    out.data.resize((size_t)len * (size_t)out.channels);
    auto q = [](float v) { return (i16)Clamp((int)lrintf(v), -32767, 32767); };
    for (int i = 0; i < len; i++) {
        if (b.stereo) {
            out.data[(size_t)i * 2] = q(b.L[(size_t)i] * g);
            out.data[(size_t)i * 2 + 1] = q(b.R[(size_t)i] * g);
        } else {
            out.data[(size_t)i] = q(b.L[(size_t)i] * g);
        }
    }
}

static void renderVariation(int id, int var, SoundBuffer& out) {
    u32 seed = hash32((u32)id * 7919u + (u32)var * 104729u + 0x5eedu);
    Buf b(seed, isStereo(id));
    synthesize(id, var, b);
    finish(b, out);
}

// Bank storage and background rendering
BankEntry g_bank[BANK_COUNT];
std::vector<std::pair<int, int>> g_work;
std::atomic<int> g_workNext{0};
std::atomic<int> g_remaining[BANK_COUNT];
std::vector<std::thread> g_threads;
std::mutex g_bankMutex;
std::atomic<bool> g_started{false};

static float estCost(int id) {
    switch (id) {
        case SFX_EXPLOSION: case SFX_THUNDER: case SFX_HELI_FLYBY: return 10.f;
        case SFX_BELL: case SFX_EXPLOSION_SMALL: case AMB_SIREN_DISTANT: case SFX_MISSION_PASSED:
        case SFX_WASTED: case SFX_BUSTED: case SFX_MISSION_FAILED: return 6.f;
        case SFX_CAR_CRASH_HEAVY: case SFX_SPLASH_BIG: case SFX_ENGINE_START:
        case SFX_ROCKET_LAUNCH: case SFX_GLASS_BREAK: return 4.f;
        default:
            if (id >= GUN_FAR_URBAN && id < GUN_CRACK) return 8.f;
            if (id == AMB_AIRBOAT || id == AMB_TRAIN_PASS || id == AMB_BOAT_PASS || (id >= AMB_THUNDER_CLOSE && id <= AMB_THUNDER_FAR)) return 8.f;
            if (id >= GUN_NEAR && id < GUN_MECH) return 2.f;
            return 1.f;
    }
}

static void workerMain() {
    dsp::enableFlushDenormals();
    for (;;) {
        int idx = g_workNext.fetch_add(1);
        if (idx >= (int)g_work.size()) break;
        int id = g_work[(size_t)idx].first, var = g_work[(size_t)idx].second;
        renderVariation(id, var, g_bank[id].vars[var]);
        if (g_remaining[id].fetch_sub(1) == 1) {
            g_bank[id].count = Clamp((int)kDefs[id].variations, 1, kMaxVariations);
            g_bank[id].ready.store(true, std::memory_order_release);
        }
    }
}

}  // namespace sfxgen

const SoundDef& soundDef(int id) { return sfxgen::kDefs[Clamp(id, 0, BANK_COUNT - 1)]; }
BankEntry& bankEntry(int id) { return sfxgen::g_bank[Clamp(id, 0, BANK_COUNT - 1)]; }
bool bankReady(int id) {
    if (id <= 0 || id >= BANK_COUNT) return false;
    return sfxgen::g_bank[id].ready.load(std::memory_order_acquire);
}
bool bankStarted() { return sfxgen::g_started.load(); }

void bankRenderEntry(int id) {
    using namespace sfxgen;
    dsp::ScopedFlushDenormals ftz;
    if (id <= 0 || id >= BANK_COUNT) return;
    int vars = Clamp((int)kDefs[id].variations, 1, kMaxVariations);
    for (int v = 0; v < vars; v++) renderVariation(id, v, g_bank[id].vars[v]);
    g_bank[id].count = vars;
    g_bank[id].ready.store(true, std::memory_order_release);
}

void bankStartAsync(int threads) {
    using namespace sfxgen;
    std::lock_guard<std::mutex> lk(g_bankMutex);
    if (g_started.load()) return;
    g_started = true;
    g_work.clear();
    for (int id = 1; id < BANK_COUNT; id++) {
        int vars = Clamp((int)kDefs[id].variations, 1, kMaxVariations);
        g_remaining[id].store(vars);
        for (int v = 0; v < vars; v++) g_work.push_back(std::make_pair(id, v));
    }
    std::stable_sort(g_work.begin(), g_work.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return estCost(a.first) > estCost(b.first); });
    g_workNext = 0;
    threads = Clamp(threads, 1, 8);
    for (int i = 0; i < threads; i++) g_threads.emplace_back(workerMain);
}

void bankWaitAll() {
    using namespace sfxgen;
    if (!g_started.load()) {
        int hc = (int)std::thread::hardware_concurrency();
        bankStartAsync(Max(1, Min(hc, 6)));
    }
    std::lock_guard<std::mutex> lk(g_bankMutex);
    for (auto& t : g_threads)
        if (t.joinable()) t.join();
    g_threads.clear();
}

void bankShutdown() { bankWaitAll(); }

}  // namespace detail
}  // namespace Audio
