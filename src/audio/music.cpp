// Procedural music engine: music theory helpers, instrument voices (subtractive/unison, FM,
// Karplus-Strong strings, sampled piano, formant "vox", drawbar organ, 808), synthesized drum kits,
// a sample-accurate song player with per-track mixing (EQ, drive/cab, chorus, sidechain, sends),
// FDN reverb, tempo-synced delay and a master bus; plus genre composers (synthwave, trap/boom-bap,
// reggaeton, house, rock, jazz/bossa, country, lo-fi), jingles/beds and the adaptive mission score.
#include "audio_internal.h"

namespace Audio {
namespace detail {
namespace music {

using namespace dsp;

// =============================================================================================
// Theory
enum class Scale : u8 { Major = 0, Minor, Dorian, Mixolydian, HarmonicMinor, Phrygian, Lydian, Count };
static const int kScaleSteps[(int)Scale::Count][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10},
    {0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}};

struct KeySig {
    int tonic = 0;  // pitch class
    Scale scale = Scale::Minor;
};
FORCEINLINE int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// MIDI pitch of scale degree `deg` (any integer) relative to the tonic in the octave starting at baseC (e.g. 60).
FORCEINLINE int degPitch(const KeySig& k, int deg, int baseC) {
    int o = floorDiv(deg, 7), d = deg - o * 7;
    return baseC + k.tonic + kScaleSteps[(int)k.scale][d] + 12 * o;
}
FORCEINLINE bool inScale(const KeySig& k, int midi) {
    int pc = ((midi - k.tonic) % 12 + 12) % 12;
    for (int i = 0; i < 7; i++)
        if (kScaleSteps[(int)k.scale][i] == pc) return true;
    return false;
}
// Nearest scale-degree index for a MIDI pitch (inverse of degPitch, rounding down to scale tones).
static int pitchToDeg(const KeySig& k, int midi, int baseC) {
    int rel = midi - baseC - k.tonic;
    int o = floorDiv(rel, 12);
    int pc = rel - o * 12;
    int d = 0;
    for (int i = 0; i < 7; i++)
        if (kScaleSteps[(int)k.scale][i] <= pc) d = i;
    return o * 7 + d;
}

struct Chord {
    int root = 0;  // pitch class
    i8 iv[6] = {0, 4, 7, -1, -1, -1};
    int n = 3;
    int bass = -1;  // pitch class for slash chords (-1 = root)
    int bassPc() const { return bass >= 0 ? bass : root; }
    bool hasPc(int pc) const {
        for (int i = 0; i < n; i++)
            if ((root + iv[i]) % 12 == ((pc % 12) + 12) % 12) return true;
        return false;
    }
};
static Chord mkChord(int root, std::initializer_list<int> ivs) {
    Chord c;
    c.root = ((root % 12) + 12) % 12;
    c.n = 0;
    for (int v : ivs)
        if (c.n < 6) c.iv[c.n++] = (i8)v;
    return c;
}
// Progression step: scale degree, root alteration, quality code, length in beats.
// Quality: 'a' diatonic triad, 's' diatonic seventh, 'M' maj, 'm' min, 'd' dim, '7' dom7, 'j' maj7,
// 'n' min7, 'h' half-dim, '9' dom9, 'N' min9, 'J' maj9, '5' power, 'b' 7b9, '6' maj6, '4' sus4,
// 'A' add9, 'x' min6, 'D' dim7, '3' dom13
struct PStep {
    i8 deg;
    i8 acc;
    char q;
    i8 beats;
};

static Chord buildChord(const KeySig& k, const PStep& p) {
    int root = k.tonic + kScaleSteps[(int)k.scale][((p.deg % 7) + 7) % 7] + p.acc;
    auto st = [&](int off) {
        int d = p.deg + off;
        int o = floorDiv(d, 7), dd = d - o * 7;
        return kScaleSteps[(int)k.scale][dd] + 12 * o - kScaleSteps[(int)k.scale][((p.deg % 7) + 7) % 7];
    };
    switch (p.q) {
        case 'a': return mkChord(root, {0, st(2), st(4)});
        case 's': return mkChord(root, {0, st(2), st(4), st(6)});
        case 'M': return mkChord(root, {0, 4, 7});
        case 'm': return mkChord(root, {0, 3, 7});
        case 'd': return mkChord(root, {0, 3, 6});
        case '7': return mkChord(root, {0, 4, 7, 10});
        case 'j': return mkChord(root, {0, 4, 7, 11});
        case 'n': return mkChord(root, {0, 3, 7, 10});
        case 'h': return mkChord(root, {0, 3, 6, 10});
        case '9': return mkChord(root, {0, 4, 7, 10, 14});
        case 'N': return mkChord(root, {0, 3, 7, 10, 14});
        case 'J': return mkChord(root, {0, 4, 7, 11, 14});
        case '5': return mkChord(root, {0, 7, 12});
        case 'b': return mkChord(root, {0, 4, 7, 10, 13});
        case '6': return mkChord(root, {0, 4, 7, 9});
        case '4': return mkChord(root, {0, 5, 7});
        case 'A': return mkChord(root, {0, 4, 7, 14});
        case 'x': return mkChord(root, {0, 3, 7, 9});
        case 'D': return mkChord(root, {0, 3, 6, 9});
        case '3': return mkChord(root, {0, 4, 10, 14, 21});
        default: return mkChord(root, {0, st(2), st(4)});
    }
}

// Voices `count` chord tones within [lo, hi], keeping close to the previous voicing.
static int voiceChord(const Chord& c, int lo, int hi, int count, int* out, const int* prev, int prevN) {
    int pcs[6];
    int npc = Min(c.n, 6);
    for (int i = 0; i < npc; i++) pcs[i] = (c.root + c.iv[i]) % 12;
    int best[8];
    int bestN = 0;
    float bestCost = 1e9f;
    count = Clamp(count, 1, 8);
    for (int rot = 0; rot < npc; rot++) {
        for (int base = lo; base < lo + 12; base++) {
            if (((base % 12) + 12) % 12 != pcs[rot]) continue;
            int cand[8];
            int cn = 0;
            int p = base;
            int idx = rot;
            while (cn < count) {
                if (p > hi) break;
                cand[cn++] = p;
                int nextPc = pcs[(idx + 1) % npc];
                idx = (idx + 1) % npc;
                int step = ((nextPc - p % 12) % 12 + 12) % 12;
                if (step == 0) step = 12;
                p += step;
            }
            if (cn < Min(count, 2)) continue;
            float cost = 0.f;
            if (prevN > 0) {
                for (int i = 0; i < cn; i++) {
                    int dmin = 99;
                    for (int j = 0; j < prevN; j++) dmin = Min(dmin, abs(cand[i] - prev[j]));
                    cost += (float)dmin;
                }
                cost /= (float)cn;
            } else {
                float mid = (float)(lo + hi) * 0.5f;
                float avg = 0;
                for (int i = 0; i < cn; i++) avg += (float)cand[i];
                cost = fabsf(avg / (float)cn - mid) * 0.5f;
            }
            cost += (float)(count - cn) * 4.f;
            if (cost < bestCost) {
                bestCost = cost;
                bestN = cn;
                for (int i = 0; i < cn; i++) best[i] = cand[i];
            }
        }
    }
    for (int i = 0; i < bestN; i++) out[i] = best[i];
    return bestN;
}

// =============================================================================================
// Instruments
enum class Eng : u8 { Sub, Fm, Pluck, Piano, Vox, Organ, Bass808, Drums };
enum class FMode : u8 { LP, BP, HP };

struct Patch {
    Eng eng = Eng::Sub;
    float aA = 0.005f, aD = 0.3f, aS = 0.7f, aR = 0.2f;
    float gain = 1.f;
    int poly = 8;
    bool mono = false;
    float glide = 0.f;
    float velSens = 0.6f;
    // subtractive
    Wave w1 = Wave::Saw, w2 = Wave::Saw;
    float mix2 = 0.f, det2 = 0.08f;
    int oct2 = 0;
    float pw = 0.5f, pwmDepth = 0.f, pwmRate = 0.4f;
    int unison = 1;
    float uniDet = 0.15f, uniSpread = 0.8f;
    float subLvl = 0.f, noiseLvl = 0.f;
    FMode fmode = FMode::LP;
    float cutoff = 3000.f, reso = 0.7f, fEnv = 0.f, keyTrk = 0.5f, velCut = 1.f;
    float fA = 0.003f, fD = 0.3f, fS = 0.f, fR = 0.3f;
    float pEnv = 0.f, pDecay = 0.03f;
    float vibRate = 5.2f, vibDepth = 0.f, vibDelay = 0.3f;
    float drive = 0.f;
    // FM
    float fmRatio = 1.f, fmIndex = 2.f, fmIdxDecay = 0.4f, fmIdxSus = 0.2f;
    float fmRatio2 = 14.f, fmIndex2 = 0.f, fmIdx2Decay = 0.03f, fmFeedback = 0.f, fmDetune = 0.f;
    float tremRate = 0.f, tremDepth = 0.f;
    // pluck
    float kDecay = 2.f, kBright = 0.5f, kPick = 0.15f, kMute = 0.f, kNoiseLP = 6000.f;
    // vox
    int vowelA = 0, vowelB = 1;
    float breath = 0.1f, voxBright = 0.6f;
    // organ
    float drawbar[6] = {0.7f, 1.f, 0.6f, 0.5f, 0.3f, 0.15f};
    float percussion = 0.3f, orgClick = 0.3f;
};

// ---- Piano multisample (rendered once, background thread)
struct PianoZone {
    int root = 60;
    std::vector<i16> data;
};
constexpr int kPianoZones = 22;  // every 3 semitones from MIDI 33
struct PianoSet {
    PianoZone zones[2][kPianoZones];  // [soft/hard]
    std::atomic<bool> ready{false};
};
PianoSet g_piano;
std::thread g_pianoThread;
std::mutex g_pianoMutex;

static void renderPianoNote(int midi, float vel, std::vector<i16>& out, u32 seed) {
    float f = midiToHz((float)midi);
    float u = Saturate(((float)midi - 21.f) / 87.f);
    float dur = Lerp(3.2f, 1.4f, u);
    int n = (int)(dur * kSR);
    std::vector<float> buf((size_t)n, 0.f);
    float B = Lerp(0.00025f, 0.0022f, u);
    float tau1 = Lerp(7.f, 1.1f, u);
    float alpha = Lerp(2.1f, 1.25f, vel);
    Noise nz(seed | 1);
    int K = Clamp((int)(9000.f / f), 3, 18);
    for (int k = 1; k <= K; k++) {
        float fk = f * (float)k * sqrtf(1.f + B * (float)(k * k));
        if (fk > 18000.f) break;
        float a = 1.f / powf((float)k, alpha) * fabsf(sinf(kPi * (float)k * 0.118f)) * 2.f;
        float tauFast = tau1 * 0.18f / (1.f + 0.25f * (float)(k - 1));
        float tauSlow = tau1 / (1.f + 0.12f * (float)(k - 1));
        int strings = k <= 8 ? 2 : 1;
        for (int s = 0; s < strings; s++) {
            float det = strings == 2 ? (s == 0 ? -1.f : 1.f) * nz.range(0.4f, 1.3f) : 0.f;
            float fr = fk * semis(det / 100.f);
            float w = kTwoPi * fr / kSR;
            float rf = expf(-1.f / (tauFast * kSR)), rs = expf(-1.f / (tauSlow * kSR));
            float cw = 2.f * cosf(w);
            // two damped oscillators (prompt + aftersound) per string
            float y1a = sinf(w) * a * 0.65f / (float)strings, y2a = 0.f;
            float y1b = sinf(w) * a * 0.35f / (float)strings, y2b = 0.f;
            float ca = cw * rf, cb = cw * rs, ra2 = rf * rf, rb2 = rs * rs;
            for (int i = 1; i < n; i++) {
                float ya = ca * y1a - ra2 * y2a;
                y2a = y1a;
                y1a = ya;
                float yb = cb * y1b - rb2 * y2b;
                y2b = y1b;
                y1b = yb;
                buf[(size_t)i] += ya + yb;
                if (i > 4096 && (i & 1023) == 0 && fabsf(y1b) + fabsf(y1a) < 1e-6f && fabsf(y2a) + fabsf(y2b) < 1e-6f) break;
            }
        }
    }
    // hammer thump
    OnePoleLP hl;
    hl.set(1500.f + 3000.f * vel);
    for (int i = 0; i < Min(n, 900); i++) {
        float e = expf(-(float)i / 120.f);
        buf[(size_t)i] += hl.process(nz.white()) * e * 0.25f * (0.4f + vel);
    }
    // soundboard color
    Biquad lowS, pk;
    lowS.setLowShelf(180.f, 3.f);
    pk.setPeak(2500.f, 1.f, -2.f);
    float peak = 0.f;
    for (int i = 0; i < n; i++) {
        buf[(size_t)i] = pk.process(lowS.process(buf[(size_t)i]));
        peak = Max(peak, fabsf(buf[(size_t)i]));
    }
    int fadeN = (int)(0.3f * kSR);
    for (int i = 0; i < fadeN; i++) buf[(size_t)(n - 1 - i)] *= (float)i / (float)fadeN;
    float g = peak > 1e-9f ? 0.9f / peak * 32767.f * (0.55f + 0.45f * vel) : 0.f;
    out.resize((size_t)n);
    for (int i = 0; i < n; i++) out[(size_t)i] = (i16)Clamp((int)lrintf(buf[(size_t)i] * g), -32767, 32767);
}
static void renderPianoSet() {
    for (int v = 0; v < 2; v++)
        for (int z = 0; z < kPianoZones; z++) {
            g_piano.zones[v][z].root = 33 + z * 3;
            renderPianoNote(33 + z * 3, v == 0 ? 0.35f : 0.9f, g_piano.zones[v][z].data, hash32((u32)(z * 31 + v)));
        }
    g_piano.ready.store(true, std::memory_order_release);
}
void pianoStartAsync() {
    std::lock_guard<std::mutex> lk(g_pianoMutex);
    if (g_piano.ready.load() || g_pianoThread.joinable()) return;
    g_pianoThread = std::thread(renderPianoSet);
}
void pianoWait() {
    std::lock_guard<std::mutex> lk(g_pianoMutex);
    if (g_pianoThread.joinable()) g_pianoThread.join();
    if (!g_piano.ready.load()) renderPianoSet();
}

// ---- Formant vowels (F1, F2, F3)
static const float kVowels[6][3] = {{730, 1090, 2440},   // ah
                                    {570, 840, 2410},    // oh
                                    {300, 870, 2240},    // oo
                                    {530, 1840, 2480},   // eh
                                    {270, 2290, 3010},   // ee
                                    {660, 1720, 2410}};  // ae

struct KRes {
    float a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
    void set(float f, float bw) {
        c = -expf(-kTwoPi * bw * kInvSR);
        b = 2.f * expf(-kPi * bw * kInvSR) * cosf(kTwoPi * Min(f, 20000.f) * kInvSR);
        a = 1.f - b - c;
    }
    FORCEINLINE float process(float x) {
        float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// ---- Voice
constexpr int kKsCap = 2048;
struct Voice {
    bool active = false, released = false;
    int note = 60;
    float vel = 1.f, freq = 440.f, target = 440.f;
    u32 age = 0;
    float pan = 0.f;
    int ctl = 0;
    Adsr amp, filt;
    // sub
    BlepOsc osc[5], osc2, subOsc;
    float uniMul[5] = {1, 1, 1, 1, 1}, uniPan[5] = {0, 0, 0, 0, 0};
    Svf fL, fR;
    float pEnvLvl = 0.f, pEnvK = 0.f;
    float vibPh = 0.f, vibT = 0.f, pwmPh = 0.f, curPw = 0.5f;
    float dt = 0.01f;
    // fm
    float pc = 0, pc2 = 0, pm = 0, pm2 = 0, idxEnv = 1, idxK = 1, idx2Env = 1, idx2K = 1, fbPrev = 0, tremPh = 0;
    // pluck
    float* ks = nullptr;
    int ksW = 0;
    float ksDelay = 100.f, ksLoss = 0.99f, ksDamp = 0.3f, ksPrev = 0.f, ksRelLoss = 0.9f;
    // piano
    const PianoZone* zone = nullptr;
    double spos = 0.0;
    float srate = 1.f;
    OnePoleLP velLp;
    // vox
    KRes fr1, fr2, fr3;
    float voxPh = 0.f, voxMorph = 0.f, voxMorphK = 0.f;
    OnePoleLP voxTilt;
    float voxA[3], voxB[3];
    // organ
    float oph[6] = {0, 0, 0, 0, 0, 0};
    float percEnv = 0.f, clickEnv = 0.f;
    // 808
    float ph808 = 0.f;
    Noise nz;
};

struct NoteOnInfo {
    int note;
    float vel;
    bool slide;
    float pan;
};

static void voiceStart(Voice& v, const Patch& p, const NoteOnInfo& ni, float* ksMem, u32 rnd) {
    bool legato = v.active && p.mono && (ni.slide || p.glide > 0.f) && !v.released;
    v.nz.seed(rnd | 1u);
    v.note = ni.note;
    v.vel = ni.vel;
    v.target = midiToHz((float)ni.note);
    if (!legato || p.glide <= 0.f) v.freq = v.target;
    v.pan = ni.pan;
    v.age = 0;
    v.released = false;
    v.ctl = 0;
    v.amp.set(p.aA, p.aD, p.aS, p.aR);
    v.filt.set(p.fA, p.fD, p.fS, p.fR);
    if (!legato) {
        v.amp.noteOn(!v.active);
        v.filt.noteOn(true);
        v.vibT = 0.f;
    } else {
        if (v.amp.stage == Adsr::kRelease) v.amp.noteOn(false);
    }
    v.active = true;
    v.pEnvLvl = legato ? 0.f : p.pEnv;
    v.pEnvK = expf(-1.f / Max(p.pDecay * kSR, 1.f));
    switch (p.eng) {
        case Eng::Sub: {
            if (!legato) {
                int u = Clamp(p.unison, 1, 5);
                for (int i = 0; i < u; i++) {
                    float d = u > 1 ? ((float)i / (float)(u - 1) - 0.5f) * 2.f : 0.f;
                    v.uniMul[i] = semis(d * p.uniDet);
                    v.uniPan[i] = d * p.uniSpread;
                    v.osc[i].phase = v.nz.uni();
                }
                v.osc2.phase = v.nz.uni();
                v.subOsc.phase = 0.f;
                v.fL.reset();
                v.fR.reset();
            }
            break;
        }
        case Eng::Fm: {
            v.pc = v.pc2 = 0.f;
            v.pm = v.pm2 = 0.f;
            v.idxEnv = 1.f;
            v.idxK = expf(-1.f / Max(p.fmIdxDecay * kSR, 1.f));
            v.idx2Env = 1.f;
            v.idx2K = expf(-1.f / Max(p.fmIdx2Decay * kSR, 1.f));
            v.fbPrev = 0.f;
            break;
        }
        case Eng::Pluck: {
            v.ks = ksMem;
            float f = v.target;
            float L = kSR / f;
            v.ksDamp = 0.5f * (1.f - Saturate(p.kBright)) * (1.f - 0.3f * ni.vel) + 0.02f;
            v.ksDelay = Clamp(L - v.ksDamp - 1.f, 2.f, (float)kKsCap - 4.f);
            float t60 = p.kDecay * sqrtf(220.f / f) * (1.f - 0.85f * p.kMute);
            v.ksLoss = powf(10.f, -3.f / (Max(t60, 0.05f) * f));
            v.ksRelLoss = powf(10.f, -3.f / (0.12f * f));
            // excitation: filtered noise burst with pick-position comb
            int len = Clamp((int)L, 2, kKsCap - 4);
            OnePoleLP lp;
            lp.set(p.kNoiseLP * (0.35f + 0.65f * ni.vel) * (1.f - 0.6f * p.kMute));
            int pick = Clamp((int)(L * p.kPick), 1, len - 1);
            float tmp[kKsCap];
            for (int i = 0; i < len; i++) tmp[i] = lp.process(v.nz.white());
            float mean = 0.f;
            for (int i = 0; i < len; i++) mean += tmp[i];
            mean /= (float)len;
            memset(v.ks, 0, sizeof(float) * kKsCap);
            for (int i = 0; i < len; i++) {
                float e = tmp[i] - mean - (i >= pick ? tmp[i - pick] - mean : 0.f);
                v.ks[i] = e * ni.vel * 0.9f;
            }
            v.ksW = len;
            v.ksPrev = 0.f;
            break;
        }
        case Eng::Piano: {
            int z = Clamp((ni.note - 33 + 1) / 3, 0, kPianoZones - 1);
            int layer = ni.vel > 0.62f ? 1 : 0;
            v.zone = &g_piano.zones[layer][z];
            v.spos = 0.0;
            v.srate = semis((float)(ni.note - v.zone->root));
            v.velLp.set(900.f + 14000.f * ni.vel * ni.vel);
            v.velLp.reset(0.f);
            break;
        }
        case Eng::Vox: {
            int va = (p.vowelA + (int)(rnd % 3)) % 6, vb = (p.vowelB + (int)((rnd >> 8) % 3)) % 6;
            for (int i = 0; i < 3; i++) {
                v.voxA[i] = kVowels[va][i];
                v.voxB[i] = kVowels[vb][i];
            }
            if (!legato) {
                v.voxMorph = 0.f;
                v.voxPh = v.nz.uni();
            }
            v.voxMorphK = 1.f / (0.35f * kSR);
            v.voxTilt.set(900.f + 4500.f * p.voxBright);
            break;
        }
        case Eng::Organ: {
            if (!legato) {
                for (int i = 0; i < 6; i++) v.oph[i] = v.nz.uni();
                v.percEnv = 1.f;
                v.clickEnv = 1.f;
            }
            break;
        }
        case Eng::Bass808: {
            if (!legato) v.ph808 = 0.f;
            break;
        }
        default: break;
    }
}

FORCEINLINE void voiceRelease(Voice& v) {
    if (!v.active || v.released) return;
    v.released = true;
    v.amp.noteOff();
    v.filt.noteOff();
}

static const float kOrganRatios[6] = {0.5f, 1.f, 1.5f, 2.f, 3.f, 4.f};

// Renders one voice (adds into L/R). Returns false when the voice finished.
static bool voiceRender(Voice& v, const Patch& p, float* L, float* R, int n) {
    float gl, gr;
    panGains(v.pan, gl, gr);
    const float velGain = 1.f - p.velSens + p.velSens * v.vel;
    const float gain = p.gain * velGain;
    switch (p.eng) {
        case Eng::Sub: {
            int u = Clamp(p.unison, 1, 5);
            float norm = 1.f / sqrtf((float)u);
            bool stereo = u > 1;
            for (int i = 0; i < n; i++) {
                if ((v.ctl++ & 15) == 0) {
                    // glide / pitch envelope / vibrato
                    if (p.glide > 0.f) v.freq += (v.target - v.freq) * (1.f - expf(-16.f / (p.glide * kSR)));
                    else v.freq = v.target;
                    v.vibT += 16.f * kInvSR;
                    v.vibPh += p.vibRate * 16.f * kInvSR;
                    if (v.vibPh >= 1.f) v.vibPh -= 1.f;
                    float vib = p.vibDepth * SmoothStep(p.vibDelay, p.vibDelay + 0.4f, v.vibT) * sinWrapped(v.vibPh);
                    float f = v.freq * semis(v.pEnvLvl + vib);
                    v.dt = Min(f * kInvSR, 0.45f);
                    float fe = v.filt.level;
                    float kt = semis(((float)v.note - 60.f) * p.keyTrk);
                    float co = p.cutoff * kt * exp2f(p.fEnv * fe + p.velCut * (v.vel - 0.7f));
                    float g = svfG(Clamp(co, 30.f, 20000.f));
                    v.fL.setG(g, p.reso);
                    if (stereo) v.fR.setG(g, p.reso);
                    if (p.pwmDepth > 0.f) {
                        v.pwmPh += p.pwmRate * 16.f * kInvSR;
                        if (v.pwmPh >= 1.f) v.pwmPh -= 1.f;
                        v.curPw = Clamp(p.pw + p.pwmDepth * sinWrapped(v.pwmPh), 0.05f, 0.95f);
                    } else {
                        v.curPw = p.pw;
                    }
                }
                v.pEnvLvl *= v.pEnvK;
                v.filt.process();
                float a = v.amp.process();
                float sl = 0.f, sr = 0.f;
                for (int k = 0; k < u; k++) {
                    float s = v.osc[k].wave(p.w1, Min(v.dt * v.uniMul[k], 0.45f), v.curPw, v.nz);
                    if (stereo) {
                        sl += s * (0.5f - 0.5f * v.uniPan[k]);
                        sr += s * (0.5f + 0.5f * v.uniPan[k]);
                    } else {
                        sl += s;
                    }
                }
                if (stereo) {
                    sl *= norm * 1.41f;
                    sr *= norm * 1.41f;
                }
                float extra = 0.f;
                if (p.mix2 > 0.f) {
                    float m2 = (p.oct2 == 0 ? 1.f : exp2f((float)p.oct2)) * semis(p.det2);
                    extra += v.osc2.wave(p.w2, Min(v.dt * m2, 0.45f), v.curPw, v.nz) * p.mix2;
                }
                if (p.subLvl > 0.f) extra += v.subOsc.sine(v.dt * 0.5f) * p.subLvl;
                if (p.noiseLvl > 0.f) extra += v.nz.white() * p.noiseLvl;
                sl += extra;
                sr += extra;
                if (p.drive > 0.f) {
                    sl = fastTanh(sl * (1.f + p.drive * 3.f));
                    if (stereo) sr = fastTanh(sr * (1.f + p.drive * 3.f));
                }
                float yl, yr;
                switch (p.fmode) {
                    case FMode::LP:
                        yl = v.fL.lp(sl);
                        yr = stereo ? v.fR.lp(sr) : yl;
                        break;
                    case FMode::BP:
                        yl = v.fL.bp(sl) * v.fL.k;
                        yr = stereo ? v.fR.bp(sr) * v.fR.k : yl;
                        break;
                    default:
                        yl = v.fL.hp(sl);
                        yr = stereo ? v.fR.hp(sr) : yl;
                        break;
                }
                L[i] += yl * a * gain * gl * 1.41f;
                R[i] += yr * a * gain * gr * 1.41f;
            }
            break;
        }
        case Eng::Fm: {
            float velIdx = 0.5f + 0.5f * v.vel;
            for (int i = 0; i < n; i++) {
                if ((v.ctl++ & 15) == 0) {
                    if (p.glide > 0.f) v.freq += (v.target - v.freq) * (1.f - expf(-16.f / (p.glide * kSR)));
                    else v.freq = v.target;
                    v.vibT += 16.f * kInvSR;
                    v.vibPh += p.vibRate * 16.f * kInvSR;
                    if (v.vibPh >= 1.f) v.vibPh -= 1.f;
                    float vib = p.vibDepth * SmoothStep(p.vibDelay, p.vibDelay + 0.4f, v.vibT) * sinWrapped(v.vibPh);
                    v.dt = v.freq * semis(v.pEnvLvl + vib) * kInvSR;
                }
                v.pEnvLvl *= v.pEnvK;
                float a = v.amp.process();
                v.idxEnv *= v.idxK;
                v.idx2Env *= v.idx2K;
                float idx = p.fmIndex * velIdx * (p.fmIdxSus + (1.f - p.fmIdxSus) * v.idxEnv);
                v.pm += v.dt * p.fmRatio;
                if (v.pm >= 1.f) v.pm -= floorf(v.pm);
                float mod = sinWrapped(v.pm) * idx + v.fbPrev * p.fmFeedback;
                float mod2 = 0.f;
                if (p.fmIndex2 > 0.f) {
                    v.pm2 += v.dt * p.fmRatio2;
                    if (v.pm2 >= 1.f) v.pm2 -= floorf(v.pm2);
                    mod2 = sinWrapped(v.pm2) * p.fmIndex2 * v.idx2Env * velIdx;
                }
                v.pc += v.dt;
                if (v.pc >= 1.f) v.pc -= 1.f;
                float y = sinCycle(v.pc + (mod + mod2) * 0.159155f);
                v.fbPrev = y;
                if (p.fmDetune > 0.f) {
                    v.pc2 += v.dt * semis(p.fmDetune * 0.01f);
                    if (v.pc2 >= 1.f) v.pc2 -= 1.f;
                    y = 0.6f * y + 0.6f * sinCycle(v.pc2 + (mod + mod2) * 0.159155f);
                }
                if (p.tremDepth > 0.f) {
                    v.tremPh += p.tremRate * kInvSR;
                    if (v.tremPh >= 1.f) v.tremPh -= 1.f;
                    y *= 1.f - p.tremDepth * (0.5f + 0.5f * sinWrapped(v.tremPh));
                }
                float o = y * a * gain;
                L[i] += o * gl;
                R[i] += o * gr;
            }
            break;
        }
        case Eng::Pluck: {
            if (!v.ks) return false;
            for (int i = 0; i < n; i++) {
                float a = v.amp.process();
                float loss = v.released ? v.ksRelLoss : v.ksLoss;
                float d = v.ksDelay;
                if (p.vibDepth > 0.f) {
                    v.vibT += kInvSR;
                    v.vibPh += p.vibRate * kInvSR;
                    if (v.vibPh >= 1.f) v.vibPh -= 1.f;
                    d *= 1.f - (semis(p.vibDepth * SmoothStep(p.vibDelay, p.vibDelay + 0.3f, v.vibT) * sinWrapped(v.vibPh)) - 1.f);
                }
                int di = (int)d;
                float fr = d - (float)di;
                int r0 = (v.ksW - 1 - di) & (kKsCap - 1);
                int r1 = (v.ksW - 2 - di) & (kKsCap - 1);
                float x = v.ks[r0] + (v.ks[r1] - v.ks[r0]) * fr;
                float y = loss * (x * (1.f - v.ksDamp) + v.ksPrev * v.ksDamp);
                v.ksPrev = x;
                v.ks[v.ksW & (kKsCap - 1)] = y;
                v.ksW = (v.ksW + 1) & (kKsCap - 1);
                float o = y * gain * a;
                L[i] += o * gl;
                R[i] += o * gr;
            }
            v.age += (u32)n;
            // done when quiet
            if (v.age > 2048 && (v.age & 1023) < (u32)n) {
                float e = 0.f;
                for (int k = 0; k < 64; k++) e = Max(e, fabsf(v.ks[(v.ksW - 1 - k) & (kKsCap - 1)]));
                if (e < 2e-5f) v.active = false;
            }
            if (v.amp.stage == Adsr::kIdle) v.active = false;
            return v.active;
        }
        case Eng::Piano: {
            if (!v.zone || v.zone->data.empty()) {
                v.active = false;
                return false;
            }
            const i16* d = v.zone->data.data();
            int len = (int)v.zone->data.size();
            for (int i = 0; i < n; i++) {
                int ip = (int)v.spos;
                if (ip + 1 >= len) {
                    v.active = false;
                    break;
                }
                float fr = (float)(v.spos - (double)ip);
                float s = ((float)d[ip] + ((float)d[ip + 1] - (float)d[ip]) * fr) * (1.f / 32767.f);
                v.spos += (double)v.srate;
                s = v.velLp.process(s);
                float a = v.amp.process();
                float o = s * a * gain;
                L[i] += o * gl;
                R[i] += o * gr;
            }
            break;
        }
        case Eng::Vox: {
            for (int i = 0; i < n; i++) {
                if ((v.ctl++ & 31) == 0) {
                    if (p.glide > 0.f) v.freq += (v.target - v.freq) * (1.f - expf(-32.f / (p.glide * kSR)));
                    else v.freq = v.target;
                    v.vibT += 32.f * kInvSR;
                    v.vibPh += p.vibRate * 32.f * kInvSR;
                    if (v.vibPh >= 1.f) v.vibPh -= 1.f;
                    float vib = p.vibDepth * SmoothStep(p.vibDelay, p.vibDelay + 0.35f, v.vibT) * sinWrapped(v.vibPh);
                    v.dt = Min(v.freq * semis(vib) * kInvSR, 0.45f);
                    float m = SmoothStep(0.f, 1.f, v.voxMorph);
                    float fs = 1.f + 0.12f * Saturate(((float)v.note - 60.f) / 24.f);
                    v.fr1.set((v.voxA[0] + (v.voxB[0] - v.voxA[0]) * m) * fs, 80.f);
                    v.fr2.set((v.voxA[1] + (v.voxB[1] - v.voxA[1]) * m) * fs, 100.f);
                    v.fr3.set((v.voxA[2] + (v.voxB[2] - v.voxA[2]) * m) * fs, 140.f);
                }
                v.voxMorph = Min(1.f, v.voxMorph + v.voxMorphK);
                float a = v.amp.process();
                v.voxPh += v.dt;
                if (v.voxPh >= 1.f) v.voxPh -= 1.f;
                float src = -((2.f * v.voxPh - 1.f) - polyBlep(v.voxPh, v.dt));
                src = v.voxTilt.process(src) + v.nz.white() * p.breath * 0.5f;
                float y = v.fr3.process(v.fr2.process(v.fr1.process(src)));
                float o = y * a * gain * 2.5f;
                L[i] += o * gl;
                R[i] += o * gr;
            }
            break;
        }
        case Eng::Organ: {
            float dts[6];
            for (int k = 0; k < 6; k++) dts[k] = v.target * kOrganRatios[k] * kInvSR;
            float percK = expf(-1.f / (0.25f * kSR)), clickK = expf(-1.f / (0.004f * kSR));
            for (int i = 0; i < n; i++) {
                float a = v.amp.process();
                float s = 0.f;
                for (int k = 0; k < 6; k++) {
                    v.oph[k] += dts[k];
                    if (v.oph[k] >= 1.f) v.oph[k] -= 1.f;
                    if (dts[k] < 0.45f) s += sinWrapped(v.oph[k]) * p.drawbar[k];
                }
                s += sinWrapped(v.oph[4]) * v.percEnv * p.percussion;
                v.percEnv *= percK;
                s += v.nz.white() * v.clickEnv * p.orgClick * 0.3f;
                v.clickEnv *= clickK;
                float o = s * a * gain * 0.35f;
                L[i] += o * gl;
                R[i] += o * gr;
            }
            break;
        }
        case Eng::Bass808: {
            for (int i = 0; i < n; i++) {
                if ((v.ctl++ & 15) == 0) {
                    if (p.glide > 0.f) v.freq += (v.target - v.freq) * (1.f - expf(-16.f / (p.glide * kSR)));
                    else v.freq = v.target;
                    v.dt = v.freq * semis(v.pEnvLvl) * kInvSR;
                }
                v.pEnvLvl *= v.pEnvK;
                float a = v.amp.process();
                v.ph808 += v.dt;
                if (v.ph808 >= 1.f) v.ph808 -= 1.f;
                float s = sinWrapped(v.ph808);
                s = fastTanh(s * (1.f + p.drive * 4.f)) * (1.f / fastTanh(1.f + p.drive * 4.f));
                float o = s * a * gain;
                L[i] += o * gl;
                R[i] += o * gr;
            }
            break;
        }
        default: break;
    }
    v.age += (u32)n;
    if (v.amp.stage == Adsr::kIdle) v.active = false;
    return v.active;
}

// =============================================================================================
// Drum kits: synthesized once per song (tempo-dependent FX included) and played back as samples.
enum Drum : u8 {
    DK_KICK = 0, DK_SNARE, DK_CLAP, DK_HAT_C, DK_HAT_O, DK_HAT_P, DK_RIDE, DK_RIDE_BELL, DK_CRASH, DK_TOM_L, DK_TOM_M,
    DK_TOM_H, DK_RIM, DK_SHAKER, DK_TAMB, DK_COWBELL, DK_CONGA_L, DK_CONGA_H, DK_BONGO, DK_TIMBALE_L, DK_TIMBALE_H,
    DK_CLAVE, DK_GUIRO, DK_SNAP, DK_BRUSH_SWISH, DK_BRUSH_TAP, DK_RISER, DK_IMPACT, DK_REV_CYM, DK_KICK_SUB, DK_COUNT
};
enum class KitStyle : u8 { Retro80s, Trap, BoomBap, Dembow, House, Rock, Jazz, Country, LoFi, Score };

struct DrumSample {
    std::vector<float> d[2];
    int vars = 0;
};
struct DrumKit {
    DrumSample s[DK_COUNT];
    KitStyle style = KitStyle::Rock;
    u32 seed = 0;
    float bpm = 0;
};

namespace dk {
struct G {
    std::vector<float>& b;
    Noise nz;
    G(std::vector<float>& buf, u32 seed) : b(buf), nz(seed | 1) {}
    void ensure(int n) {
        if ((int)b.size() < n) b.resize((size_t)n, 0.f);
    }
    void sine(float t0, float dur, float f0, float f1, float ptau, float amp, float att, float tau, float drive = 0.f) {
        int s0 = (int)(t0 * kSR), n = (int)(dur * kSR);
        ensure(s0 + n);
        float ph = 0.f, f = f0, pk = expf(-1.f / Max(ptau * kSR, 1.f)), e = 0.f, ek = expf(-1.f / Max(tau * kSR, 1.f));
        int an = Max(1, (int)(att * kSR));
        for (int i = 0; i < n; i++) {
            f = f1 + (f - f1) * pk;
            ph += f * kInvSR;
            if (ph >= 1.f) ph -= 1.f;
            e = i < an ? (float)i / (float)an : e * ek;
            float v = sinWrapped(ph);
            if (drive > 0.f) v = fastTanh(v * (1.f + drive * 5.f));
            b[(size_t)(s0 + i)] += v * e * amp;
            if (i > an && e < 1e-4f) break;
        }
    }
    void noise(float t0, float dur, float amp, float att, float tau, int type, float fc, float q, float fc1 = -1.f,
               float glide = 0.1f) {
        int s0 = (int)(t0 * kSR), n = (int)(dur * kSR);
        ensure(s0 + n);
        Svf f;
        if (fc1 < 0.f) fc1 = fc;
        float c = fc, gk = expf(-1.f / Max(glide * kSR, 1.f));
        float e = 0.f, ek = expf(-1.f / Max(tau * kSR, 1.f));
        int an = Max(1, (int)(att * kSR));
        for (int i = 0; i < n; i++) {
            if ((i & 15) == 0) f.setG(svfG(c), q);
            c = fc1 + (c - fc1) * gk;
            float w = nz.white(), y;
            if (type == 1) y = f.lp(w);
            else if (type == 2) y = f.hp(w);
            else if (type == 3) y = f.bp(w) * f.k;
            else y = w;
            e = i < an ? (float)i / (float)an : e * ek;
            b[(size_t)(s0 + i)] += y * e * amp;
            if (i > an && e < 1e-4f) break;
        }
    }
    void metal(float t0, float dur, float base, float amp, float tau, float hpf, float bright) {
        // 808-style six square oscillators at inharmonic ratios
        static const float kR[6] = {1.f, 1.4826f, 1.8004f, 2.5461f, 2.6302f, 3.8968f};
        int s0 = (int)(t0 * kSR), n = (int)(dur * kSR);
        ensure(s0 + n);
        float ph[6] = {0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
        Svf hp1, hp2, bp;
        hp1.set(hpf, 0.7f);
        hp2.set(hpf, 0.7f);
        bp.set(Min(hpf * 1.3f, 16000.f), 0.8f);
        float e = 1.f, ek = expf(-1.f / Max(tau * kSR, 1.f));
        for (int i = 0; i < n; i++) {
            float s = 0.f;
            for (int k = 0; k < 6; k++) {
                ph[k] += base * kR[k] * kInvSR;
                if (ph[k] >= 1.f) ph[k] -= 1.f;
                s += ph[k] < 0.5f ? 1.f : -1.f;
            }
            s = s * (1.f / 6.f) + nz.white() * (0.3f + 0.4f * bright);
            float y = hp2.hp(hp1.hp(s));
            y = y * (1.f - bright * 0.4f) + bp.bp(s) * bright * 0.5f;
            e *= ek;
            b[(size_t)(s0 + i)] += y * e * amp;
            if (e < 1e-4f) break;
        }
    }
    void modes(float t0, const float* f, const float* tau, const float* a, int cnt, float amp) {
        int s0 = (int)(t0 * kSR);
        for (int k = 0; k < cnt; k++) {
            int n = (int)(tau[k] * 7.f * kSR);
            ensure(s0 + n);
            float r = expf(-1.f / (tau[k] * kSR)), w = kTwoPi * f[k] * kInvSR;
            float c = 2.f * r * cosf(w), r2 = r * r, y2 = 0.f, y1 = r * sinf(w) * a[k] * amp;
            for (int i = 1; i < n; i++) {
                float y = c * y1 - r2 * y2;
                y2 = y1;
                y1 = y;
                b[(size_t)(s0 + i)] += y;
            }
        }
    }
    void click(float amp, int width) {
        ensure(width + 2);
        for (int i = 0; i < width; i++) b[(size_t)i] += amp * sinf(kPi * ((float)i + 0.5f) / (float)width);
    }
    void normalize(float peak) {
        float m = 0.f;
        for (float v : b) m = Max(m, fabsf(v));
        if (m > 1e-9f)
            for (float& v : b) v *= peak / m;
        // trim trailing silence
        int last = (int)b.size() - 1;
        while (last > 0 && fabsf(b[(size_t)last]) < peak * 1e-3f) last--;
        b.resize((size_t)Max(1, last + 64), 0.f);
        int fade = Min((int)b.size(), 256);
        for (int i = 0; i < fade; i++) b[b.size() - 1 - (size_t)i] *= (float)i / (float)fade;
    }
};

static void gatedReverb(std::vector<float>& b, float rt, float wet, float gateSec, u32 seed) {
    int n0 = (int)b.size();
    int n = n0 + (int)(gateSec * kSR);
    b.resize((size_t)n, 0.f);
    FdnReverb rv;
    rv.init(0.9f, seed);
    rv.setDecay(rt, 0.35f);
    rv.setPreDelay(0.005f);
    rv.erLevel = 0.5f;
    std::vector<float> oL((size_t)n), oR((size_t)n);
    rv.process(b.data(), b.data(), oL.data(), oR.data(), n);
    int gateN = (int)(gateSec * kSR);
    for (int i = 0; i < n; i++) {
        float g = i < gateN ? 1.f : Max(0.f, 1.f - (float)(i - gateN) / 900.f);
        b[(size_t)i] += 0.5f * (oL[(size_t)i] + oR[(size_t)i]) * wet * g;
    }
    b.resize((size_t)Min(n, gateN + 900));
}

static void renderPiece(int id, KitStyle st, float bpm, int var, std::vector<float>& out, u32 seed) {
    out.clear();
    G g(out, seed);
    float r = g.nz.range(0.97f, 1.03f);
    float barSec = 240.f / Max(bpm, 40.f);
    switch (id) {
        case DK_KICK:
            switch (st) {
                case KitStyle::Trap: g.sine(0, 1.4f, 120.f * r, 48.f, 0.05f, 1.f, 0.001f, 0.45f, 0.35f); g.click(0.5f, 6); break;
                case KitStyle::House: g.sine(0, 0.6f, 190.f * r, 50.f, 0.028f, 1.f, 0.001f, 0.2f, 0.2f); g.click(0.6f, 5); g.noise(0, 0.02f, 0.3f, 0.0005f, 0.004f, 2, 3000.f, 0.7f); break;
                case KitStyle::Retro80s: g.sine(0, 0.5f, 160.f * r, 52.f, 0.03f, 1.f, 0.001f, 0.16f, 0.25f); g.click(0.7f, 6); g.noise(0, 0.03f, 0.25f, 0.0005f, 0.006f, 3, 2500.f, 0.8f); break;
                case KitStyle::Dembow: g.sine(0, 0.6f, 150.f * r, 50.f, 0.03f, 1.f, 0.001f, 0.2f, 0.3f); g.click(0.5f, 6); break;
                case KitStyle::BoomBap: case KitStyle::LoFi: g.sine(0, 0.5f, 110.f * r, 55.f, 0.04f, 1.f, 0.002f, 0.15f, 0.4f); g.noise(0, 0.05f, 0.25f, 0.001f, 0.01f, 1, 1200.f, 0.7f); break;
                case KitStyle::Jazz: g.sine(0, 0.4f, 95.f * r, 60.f, 0.04f, 0.8f, 0.003f, 0.12f); g.noise(0, 0.04f, 0.15f, 0.002f, 0.01f, 1, 700.f, 0.7f); break;
                case KitStyle::Score: g.sine(0, 0.8f, 130.f * r, 42.f, 0.05f, 1.f, 0.001f, 0.3f, 0.4f); g.click(0.4f, 8); break;
                default: g.sine(0, 0.5f, 125.f * r, 52.f, 0.035f, 1.f, 0.001f, 0.15f, 0.3f); g.click(0.6f, 6); g.noise(0, 0.03f, 0.3f, 0.0005f, 0.008f, 3, 3500.f, 0.7f); break;
            }
            g.normalize(0.95f);
            break;
        case DK_KICK_SUB:
            g.sine(0, 2.f, 70.f * r, 45.f, 0.08f, 1.f, 0.002f, 0.6f, 0.2f);
            g.normalize(0.9f);
            break;
        case DK_SNARE: {
            float body = st == KitStyle::Trap ? 210.f : st == KitStyle::House ? 200.f : st == KitStyle::Retro80s ? 190.f : 180.f;
            float ntau = st == KitStyle::Jazz ? 0.1f : st == KitStyle::Retro80s ? 0.18f : st == KitStyle::LoFi || st == KitStyle::BoomBap ? 0.12f : 0.15f;
            g.sine(0, 0.3f, body * 1.4f * r, body * r, 0.01f, 0.55f, 0.0008f, 0.06f);
            g.sine(0, 0.2f, body * 1.9f * r, body * 1.75f * r, 0.01f, 0.25f, 0.0008f, 0.04f);
            g.noise(0, 0.5f, 0.75f, 0.0008f, ntau, 2, 1600.f, 0.7f);
            g.noise(0, 0.3f, 0.35f, 0.0008f, ntau * 0.6f, 3, 4200.f, 0.9f);
            if (var == 1) g.noise(0, 0.1f, 0.15f, 0.001f, 0.02f, 3, 6000.f, 1.f);
            if (st == KitStyle::Retro80s) gatedReverb(out, 1.4f, 0.9f, 0.32f, seed);
            else if (st == KitStyle::Rock) gatedReverb(out, 0.9f, 0.35f, 0.45f, seed);
            g.normalize(0.95f);
            break;
        }
        case DK_CLAP: {
            for (int k = 0; k < 4; k++) g.noise(0.009f * (float)k * r, 0.25f, k == 3 ? 1.f : 0.7f, 0.0005f, k == 3 ? 0.11f : 0.008f, 3, 1250.f, 1.1f);
            g.noise(0, 0.3f, 0.3f, 0.001f, 0.09f, 2, 2500.f, 0.7f);
            if (st == KitStyle::Retro80s || st == KitStyle::House) gatedReverb(out, 1.f, 0.4f, 0.25f, seed);
            g.normalize(0.9f);
            break;
        }
        case DK_HAT_C:
            g.metal(0, 0.2f, 320.f * r, 1.f, st == KitStyle::Trap ? 0.035f : 0.045f, 7000.f, st == KitStyle::LoFi || st == KitStyle::Jazz ? 0.3f : 0.8f);
            g.normalize(0.8f);
            break;
        case DK_HAT_O:
            g.metal(0, 0.8f, 320.f * r, 1.f, 0.28f, 6500.f, 0.7f);
            g.normalize(0.8f);
            break;
        case DK_HAT_P:
            g.metal(0, 0.1f, 320.f * r, 1.f, 0.02f, 5000.f, 0.3f);
            g.noise(0, 0.05f, 0.3f, 0.001f, 0.012f, 3, 3000.f, 1.f);
            g.normalize(0.7f);
            break;
        case DK_RIDE: {
            float f[8], t[8], a[8];
            for (int k = 0; k < 8; k++) { f[k] = g.nz.range(2800.f, 7500.f); t[k] = g.nz.range(0.6f, 1.5f); a[k] = g.nz.range(0.2f, 0.6f); }
            g.modes(0, f, t, a, 8, 0.25f);
            g.metal(0, 1.5f, 420.f * r, 0.5f, 0.9f, 5000.f, 0.4f);
            g.noise(0, 0.05f, 0.4f, 0.0005f, 0.008f, 2, 5000.f, 0.7f);
            g.normalize(0.75f);
            break;
        }
        case DK_RIDE_BELL: {
            float f[4] = {880.f * r, 2130.f * r, 3300.f * r, 5100.f * r}, t[4] = {1.2f, 0.8f, 0.6f, 0.4f}, a[4] = {1.f, 0.6f, 0.4f, 0.3f};
            g.modes(0, f, t, a, 4, 0.4f);
            g.normalize(0.7f);
            break;
        }
        case DK_CRASH:
            g.noise(0, 3.f, 0.6f, 0.002f, 0.9f, 2, 4500.f, 0.7f);
            g.metal(0, 3.f, 380.f * r, 0.6f, 1.1f, 4000.f, 0.6f);
            g.normalize(0.8f);
            break;
        case DK_TOM_L: case DK_TOM_M: case DK_TOM_H: {
            float f = id == DK_TOM_L ? 95.f : id == DK_TOM_M ? 135.f : 180.f;
            if (st == KitStyle::Retro80s) {  // electronic toms
                g.sine(0, 0.8f, f * 2.f * r, f * r, 0.12f, 1.f, 0.001f, 0.25f, 0.2f);
                g.noise(0, 0.1f, 0.2f, 0.001f, 0.02f, 1, 3000.f, 0.7f);
                gatedReverb(out, 1.f, 0.4f, 0.35f, seed);
            } else {
                g.sine(0, 0.8f, f * 1.25f * r, f * r, 0.03f, 1.f, 0.001f, 0.22f);
                g.noise(0, 0.1f, 0.25f, 0.001f, 0.03f, 3, f * 6.f, 1.f);
            }
            g.normalize(0.9f);
            break;
        }
        case DK_RIM: {
            float f[3] = {1700.f * r, 470.f * r, 3100.f * r}, t[3] = {0.02f, 0.03f, 0.01f}, a[3] = {1.f, 0.6f, 0.4f};
            g.modes(0, f, t, a, 3, 0.8f);
            g.click(0.5f, 3);
            g.normalize(0.8f);
            break;
        }
        case DK_SHAKER:
            g.noise(0, 0.12f, 1.f, 0.012f * r, 0.03f, 3, 6500.f, 1.2f);
            g.normalize(0.6f);
            break;
        case DK_TAMB: {
            g.noise(0, 0.3f, 0.6f, 0.002f, 0.08f, 2, 7000.f, 0.7f);
            float f[5], t[5], a[5];
            for (int k = 0; k < 5; k++) { f[k] = g.nz.range(5500.f, 10000.f); t[k] = g.nz.range(0.05f, 0.15f); a[k] = 0.5f; }
            g.modes(0.002f, f, t, a, 5, 0.3f);
            g.normalize(0.6f);
            break;
        }
        case DK_COWBELL: {
            int n = (int)(0.4f * kSR);
            g.ensure(n);
            float p1 = 0, p2 = 0;
            Svf bp;
            bp.set(1000.f, 1.5f);
            for (int i = 0; i < n; i++) {
                p1 += 545.f * r * kInvSR; if (p1 >= 1.f) p1 -= 1.f;
                p2 += 815.f * r * kInvSR; if (p2 >= 1.f) p2 -= 1.f;
                float s = (p1 < 0.5f ? 1.f : -1.f) + (p2 < 0.5f ? 1.f : -1.f);
                float e = (float)i < 0.004f * kSR ? 1.f : expf(-((float)i / kSR) / 0.07f) * 0.8f + 0.2f * expf(-((float)i / kSR) / 0.25f);
                out[(size_t)i] += bp.bp(s) * e;
            }
            g.normalize(0.6f);
            break;
        }
        case DK_CONGA_L: case DK_CONGA_H: case DK_BONGO: {
            float f = id == DK_CONGA_L ? 190.f : id == DK_CONGA_H ? 290.f : 420.f;
            bool slap = var == 1;
            g.sine(0, 0.5f, f * 1.08f * r, f * r, 0.02f, 1.f, 0.001f, slap ? 0.06f : 0.16f);
            float fm[2] = {f * 1.59f, f * 2.3f}, t[2] = {0.05f, 0.03f}, a[2] = {0.4f, 0.2f};
            g.modes(0, fm, t, a, 2, 0.6f);
            g.noise(0, 0.05f, slap ? 0.6f : 0.2f, 0.0005f, slap ? 0.012f : 0.006f, 3, slap ? 3500.f : 1500.f, 1.f);
            g.normalize(0.8f);
            break;
        }
        case DK_TIMBALE_L: case DK_TIMBALE_H: {
            float f = id == DK_TIMBALE_L ? 320.f : 470.f;
            g.sine(0, 0.8f, f * 1.05f * r, f * r, 0.01f, 0.8f, 0.0005f, 0.25f);
            float fm[3] = {f * 2.09f, f * 3.31f, f * 4.6f}, t[3] = {0.2f, 0.15f, 0.1f}, a[3] = {0.5f, 0.35f, 0.25f};
            g.modes(0, fm, t, a, 3, 0.6f);
            g.noise(0, 0.04f, 0.5f, 0.0003f, 0.006f, 2, 3000.f, 0.7f);
            g.normalize(0.8f);
            break;
        }
        case DK_CLAVE: {
            float f[2] = {2500.f * r, 5100.f * r}, t[2] = {0.04f, 0.02f}, a[2] = {1.f, 0.3f};
            g.modes(0, f, t, a, 2, 1.f);
            g.normalize(0.7f);
            break;
        }
        case DK_GUIRO:
            for (int k = 0; k < 14; k++) g.noise((float)k * 0.014f, 0.01f, 0.5f + 0.5f * (float)k / 14.f, 0.0005f, 0.003f, 3, 3200.f, 1.2f);
            g.normalize(0.6f);
            break;
        case DK_SNAP:
            g.noise(0, 0.08f, 1.f, 0.0005f, 0.02f, 3, 2200.f, 1.2f);
            g.click(0.5f, 3);
            g.normalize(0.7f);
            break;
        case DK_BRUSH_SWISH:
            g.noise(0, 0.35f, 1.f, 0.08f, 0.1f, 3, 3500.f, 0.8f);
            g.normalize(0.5f);
            break;
        case DK_BRUSH_TAP:
            g.noise(0, 0.15f, 1.f, 0.002f, 0.045f, 3, 2400.f, 0.8f);
            g.sine(0, 0.1f, 200.f, 180.f, 0.01f, 0.25f, 0.001f, 0.03f);
            g.normalize(0.6f);
            break;
        case DK_RISER: {
            float dur = barSec * 2.f;
            int n = (int)(dur * kSR);
            g.ensure(n);
            Svf bp;
            for (int i = 0; i < n; i++) {
                float u = (float)i / (float)n;
                if ((i & 15) == 0) bp.setG(svfG(300.f * powf(25.f, u)), 1.5f + 3.f * u);
                out[(size_t)i] += bp.bp(g.nz.white()) * u * u * 1.2f;
            }
            g.normalize(0.7f);
            break;
        }
        case DK_IMPACT:
            g.sine(0, 2.f, 80.f, 30.f, 0.25f, 1.f, 0.002f, 0.5f, 0.3f);
            g.noise(0, 1.5f, 0.6f, 0.001f, 0.3f, 1, 2000.f, 0.7f, 200.f, 0.5f);
            g.noise(0, 2.5f, 0.3f, 0.002f, 0.8f, 2, 4000.f, 0.7f);
            g.normalize(0.9f);
            break;
        case DK_REV_CYM: {
            std::vector<float> tmp;
            G g2(tmp, seed ^ 0x55u);
            float dur = barSec;
            g2.noise(0, dur, 0.6f, 0.002f, dur * 0.4f, 2, 4500.f, 0.7f);
            g2.metal(0, dur, 380.f, 0.5f, dur * 0.5f, 4000.f, 0.6f);
            int n = (int)(dur * kSR);
            tmp.resize((size_t)n, 0.f);
            out.resize((size_t)n);
            for (int i = 0; i < n; i++) out[(size_t)i] = tmp[(size_t)(n - 1 - i)];
            g.normalize(0.7f);
            break;
        }
        default: out.assign(16, 0.f); break;
    }
    if (st == KitStyle::LoFi || st == KitStyle::BoomBap) {  // dusty, band-limited
        Biquad lp;
        lp.setLP(st == KitStyle::LoFi ? 6500.f : 9000.f, 0.6f);
        for (float& v : out) v = lp.process(v);
    }
}
}  // namespace dk

static void buildKit(DrumKit& kit, KitStyle st, float bpm, u32 seed, const bool* used) {
    kit.style = st;
    kit.seed = seed;
    kit.bpm = bpm;
    for (int id = 0; id < DK_COUNT; id++) {
        kit.s[id].vars = 0;
        if (!used[id]) {
            kit.s[id].d[0].clear();
            kit.s[id].d[1].clear();
            continue;
        }
        bool twoVars = id == DK_SNARE || id == DK_HAT_C || id == DK_SHAKER || id == DK_CONGA_L || id == DK_CONGA_H || id == DK_BONGO;
        int vars = twoVars ? 2 : 1;
        for (int v = 0; v < vars; v++) dk::renderPiece(id, st, bpm, v, kit.s[id].d[v], hash32(seed + (u32)id * 977u + (u32)v * 131u));
        kit.s[id].vars = vars;
    }
}

// =============================================================================================
// Song data + player
enum NoteFlags : u8 { NF_SLIDE = 1, NF_ACCENT = 2 };
struct NoteEv {
    u32 start;
    u32 dur;
    u8 track;
    u8 note;
    u8 vel;
    u8 flags;
};

struct TrackDef {
    Patch patch;
    float gain = 0.5f, pan = 0.f;
    float hpf = 0.f, lpf = 0.f;
    float lsF = 150.f, lsDb = 0.f, hsF = 6000.f, hsDb = 0.f, pkF = 1000.f, pkQ = 1.f, pkDb = 0.f;
    float rev = 0.1f, dly = 0.f, chorus = 0.f;
    float sidechain = 0.f;
    float drive = 0.f;
    bool cab = false;
    int layer = -1;
    float panSpread = 0.f;
    bool drums = false;
};

struct Automation {
    u32 at;
    float hp, lp;
};

struct SongData {
    int genre = 0;
    u32 seed = 0;
    float bpm = 120.f;
    KeySig key;
    float swing = 0.f;
    u32 musicEnd = 0, length = 0;
    std::vector<TrackDef> tracks;
    std::vector<NoteEv> events;
    KitStyle kitStyle = KitStyle::Rock;
    bool kitUsed[DK_COUNT] = {};
    float revSize = 1.f, revDecay = 1.6f, revDamp = 0.4f, revPre = 0.015f;
    float dlyL = 0.75f, dlyR = 1.f, dlyFb = 0.3f;
    float master = 1.f;
    float vinyl = 0.f, wow = 0.f;
    std::vector<Automation> autom;
    bool looping = false;
    void sortEvents() {
        std::stable_sort(events.begin(), events.end(), [](const NoteEv& a, const NoteEv& b) { return a.start < b.start; });
    }
};

struct TrackState {
    std::vector<Voice> voices;
    std::vector<float> ksMem;
    std::vector<u32> offAt;
    Biquad eq[2][5];
    int eqN = 0;
    Biquad cab[2][3];
    Chorus chorus;
    bool hasChorus = false;
    float gainNow = 1.f;
    float bufL[kProdBlock], bufR[kProdBlock];
    u32 rnd = 1;
};

struct DrumVoice {
    const float* d = nullptr;
    int len = 0;
    float pos = 0, rate = 1, gain = 0, gl = 0, gr = 0, fade = 1.f, fadeK = 1.f;
    int piece = -1;
    int track = 0;
    bool active = false;
};

struct SongPlayer {
    std::shared_ptr<SongData> song;
    DrumKit kit;
    u32 pos = 0;
    size_t ev = 0;
    std::vector<TrackState> ts;
    DrumVoice dv[28];
    FdnReverb rev;
    StereoDelay dly;
    Compressor bus;
    bool fxInit = false;
    float scEnv = 0.f, scK = 0.999f;
    Svf autoHpL, autoHpR, autoLpL, autoLpR;
    float autoHp = 0.f, autoLp = 20000.f;
    size_t autoIdx = 0;
    float layerGain[4] = {0, 0, 0, 0};
    float layerTarget[4] = {1, 1, 1, 1};
    float fade = 1.f;
    // lo-fi extras
    Noise nz{0x1234567u};
    OnePoleLP hissLp;
    float crackleEnv = 0.f;
    DelayLine wowL, wowR;
    float wowPh = 0.f;
    bool started = false;
    float sendL[kProdBlock], sendR[kProdBlock], dSendL[kProdBlock], dSendR[kProdBlock];

    void init() {
        if (fxInit) return;
        rev.init(1.f, 0xBEEFu);
        dly.init(96000);
        wowL.init(4096);
        wowR.init(4096);
        fxInit = true;
    }
    bool finished() const { return !song || pos >= song->length; }

    void setupTrack(TrackState& t, const TrackDef& d, u32 seed) {
        int poly = d.patch.mono ? 1 : Clamp(d.patch.poly, 1, 16);
        if (d.drums) poly = 0;
        t.voices.assign((size_t)poly, Voice());
        t.offAt.assign((size_t)poly, 0u);
        if (d.patch.eng == Eng::Pluck) t.ksMem.assign((size_t)poly * kKsCap, 0.f);
        else t.ksMem.clear();
        t.eqN = 0;
        for (int c = 0; c < 2; c++) {
            int k = 0;
            if (d.hpf > 0.f) t.eq[c][k++].setHP(d.hpf, 0.7f);
            if (d.lsDb != 0.f) t.eq[c][k++].setLowShelf(d.lsF, d.lsDb);
            if (d.pkDb != 0.f) t.eq[c][k++].setPeak(d.pkF, d.pkQ, d.pkDb);
            if (d.hsDb != 0.f) t.eq[c][k++].setHighShelf(d.hsF, d.hsDb);
            if (d.lpf > 0.f) t.eq[c][k++].setLP(d.lpf, 0.7f);
            t.eqN = k;
            if (d.cab) {
                t.cab[c][0].setHP(95.f, 0.7f);
                t.cab[c][1].setPeak(1700.f, 1.f, 5.f);
                t.cab[c][2].setLP(4600.f, 0.8f);
            }
        }
        t.hasChorus = d.chorus > 0.f;
        if (t.hasChorus) {
            t.chorus.init();
            t.chorus.set(0.45f, 3.f, 11.f, d.chorus);
        }
        t.gainNow = d.layer >= 0 ? 0.f : 1.f;
        t.rnd = seed | 1u;
    }

    // Starts playback of `s` at sample offset `startPos` (re-triggers notes spanning the offset).
    void start(std::shared_ptr<SongData> s, u32 startPos, const DrumKit* sharedKit = nullptr) {
        init();
        song = std::move(s);
        pos = startPos;
        ev = 0;
        fade = 1.f;
        const SongData& sd = *song;
        if (sharedKit && sharedKit->style == sd.kitStyle && sharedKit->seed == sd.seed && fabsf(sharedKit->bpm - sd.bpm) < 0.01f) {
            kit = *sharedKit;
        } else {
            buildKit(kit, sd.kitStyle, sd.bpm, sd.seed, sd.kitUsed);
        }
        ts.clear();
        ts.resize(sd.tracks.size());
        for (size_t i = 0; i < ts.size(); i++) setupTrack(ts[i], sd.tracks[i], hash32(sd.seed + (u32)i));
        for (auto& d : dv) d.active = false;
        rev.clear();
        rev.init(sd.revSize, sd.seed | 1u);
        rev.setDecay(sd.revDecay, sd.revDamp);
        rev.setPreDelay(sd.revPre);
        rev.erLevel = 0.35f;
        float spb = 60.f / sd.bpm * kSR;
        dly.l.clear();
        dly.r.clear();
        dly.set(sd.dlyL * spb, sd.dlyR * spb, sd.dlyFb, true);
        dly.setTone(4200.f, 300.f);
        bus.set(-14.f, 2.5f, 12.f, 180.f, 8.f, 0.f);
        scK = expf(-1.f / Max(0.35f * spb, 1.f));
        scEnv = 0.f;
        autoIdx = 0;
        autoHp = 0.f;
        autoLp = 20000.f;
        hissLp.set(5000.f);
        for (int i = 0; i < 4; i++) layerGain[i] = layerTarget[i];
        // seek events
        while (ev < sd.events.size() && sd.events[ev].start < pos) {
            const NoteEv& e = sd.events[ev];
            if (e.start + e.dur > pos && !sd.tracks[e.track].drums && pos - e.start < (u32)(kSR * 4.f)) trigger(e);
            ev++;
        }
        started = true;
    }

    void trigger(const NoteEv& e) {
        const SongData& sd = *song;
        if (e.track >= ts.size()) return;
        const TrackDef& td = sd.tracks[e.track];
        TrackState& t = ts[e.track];
        t.rnd = t.rnd * 1664525u + 1013904223u;
        float panJ = td.panSpread > 0.f ? ((float)(t.rnd >> 9) / 8388608.f - 0.5f) * 2.f * td.panSpread : 0.f;
        if (td.drums) {
            int piece = e.note;
            if (piece >= DK_COUNT) return;
            const DrumSample& ds = kit.s[piece];
            if (ds.vars <= 0) return;
            if (piece == DK_KICK || piece == DK_KICK_SUB) scEnv = 1.f;
            if (piece == DK_HAT_C || piece == DK_HAT_P) {
                for (auto& d : dv)
                    if (d.active && d.piece == DK_HAT_O) d.fadeK = 0.995f;
            }
            DrumVoice* slot = nullptr;
            for (auto& d : dv)
                if (!d.active) { slot = &d; break; }
            if (!slot) {
                float best = 1e9f;
                for (auto& d : dv) {
                    float rem = (float)d.len - d.pos;
                    if (rem < best) { best = rem; slot = &d; }
                }
            }
            int var = ds.vars > 1 ? (int)((t.rnd >> 5) % (u32)ds.vars) : 0;
            slot->d = ds.d[var].data();
            slot->len = (int)ds.d[var].size();
            slot->pos = 0.f;
            slot->rate = 1.f + ((float)((t.rnd >> 11) & 1023) / 1023.f - 0.5f) * 0.02f;
            float vel = (float)e.vel / 127.f;
            slot->gain = vel * vel;
            float pan = Clamp(panJ + drumPan(piece), -1.f, 1.f);
            panGains(pan, slot->gl, slot->gr);
            slot->gl *= 1.41f;
            slot->gr *= 1.41f;
            slot->fade = 1.f;
            slot->fadeK = 1.f;
            slot->piece = piece;
            slot->track = e.track;
            slot->active = true;
            return;
        }
        const Patch& p = td.patch;
        int vi = 0;
        if (!p.mono) {
            int best = -1;
            u32 bestAge = 0;
            for (size_t i = 0; i < t.voices.size(); i++)
                if (!t.voices[i].active) { best = (int)i; break; }
            if (best < 0) {
                for (size_t i = 0; i < t.voices.size(); i++) {
                    u32 a = t.voices[i].age + (t.voices[i].released ? 0x40000000u : 0u);
                    if (a >= bestAge) { bestAge = a; best = (int)i; }
                }
            }
            vi = best;
        }
        if (vi < 0 || vi >= (int)t.voices.size()) return;
        NoteOnInfo ni;
        ni.note = e.note;
        ni.vel = (float)e.vel / 127.f;
        ni.slide = (e.flags & NF_SLIDE) != 0;
        ni.pan = Clamp(panJ, -1.f, 1.f);
        float* ks = t.ksMem.empty() ? nullptr : &t.ksMem[(size_t)vi * kKsCap];
        voiceStart(t.voices[(size_t)vi], p, ni, ks, t.rnd);
        t.offAt[(size_t)vi] = e.start + Max(e.dur, 1u);
    }

    static float drumPan(int piece) {
        switch (piece) {
            case DK_HAT_C: case DK_HAT_O: case DK_HAT_P: return 0.3f;
            case DK_RIDE: case DK_RIDE_BELL: return -0.35f;
            case DK_CRASH: return -0.25f;
            case DK_TOM_H: return 0.35f;
            case DK_TOM_M: return 0.05f;
            case DK_TOM_L: return -0.3f;
            case DK_SHAKER: return 0.45f;
            case DK_TAMB: return -0.45f;
            case DK_CONGA_L: return -0.4f;
            case DK_CONGA_H: return -0.2f;
            case DK_BONGO: return 0.4f;
            case DK_TIMBALE_L: return 0.25f;
            case DK_TIMBALE_H: return 0.5f;
            case DK_COWBELL: return 0.3f;
            case DK_GUIRO: return -0.5f;
            case DK_CLAVE: return 0.55f;
            default: return 0.f;
        }
    }

    void renderSpan(u32 a, u32 b, float* L, float* R, int off) {
        int n = (int)(b - a);
        if (n <= 0) return;
        const SongData& sd = *song;
        for (size_t ti = 0; ti < ts.size(); ti++) {
            TrackState& t = ts[ti];
            const TrackDef& td = sd.tracks[ti];
            if (td.drums) continue;
            for (size_t vi = 0; vi < t.voices.size(); vi++) {
                Voice& v = t.voices[vi];
                if (!v.active) continue;
                voiceRender(v, td.patch, t.bufL + off, t.bufR + off, n);
            }
        }
        // drums (into their track buffers)
        for (auto& d : dv) {
            if (!d.active) continue;
            TrackState& t = ts[(size_t)d.track];
            float* bl = t.bufL + off;
            float* br = t.bufR + off;
            for (int i = 0; i < n; i++) {
                int ip = (int)d.pos;
                if (ip + 1 >= d.len) { d.active = false; break; }
                float fr = d.pos - (float)ip;
                float s = (d.d[ip] + (d.d[ip + 1] - d.d[ip]) * fr) * d.gain * d.fade;
                d.fade *= d.fadeK;
                bl[i] += s * d.gl;
                br[i] += s * d.gr;
                d.pos += d.rate;
            }
            if (d.fade < 1e-3f) d.active = false;
        }
    }

    // Renders n frames (n <= kProdBlock) into L/R (overwrites).
    void render(float* L, float* R, int n) {
        memset(L, 0, sizeof(float) * (size_t)n);
        memset(R, 0, sizeof(float) * (size_t)n);
        if (!song) return;
        const SongData& sd = *song;
        for (auto& t : ts) {
            memset(t.bufL, 0, sizeof(float) * (size_t)n);
            memset(t.bufR, 0, sizeof(float) * (size_t)n);
        }
        u32 end = pos + (u32)n;
        u32 cur = pos;
        while (cur < end) {
            // next change: event start or note-off
            u32 next = end;
            if (ev < sd.events.size()) next = Min(next, Max(cur, sd.events[ev].start));
            for (size_t ti = 0; ti < ts.size(); ti++) {
                TrackState& t = ts[ti];
                for (size_t vi = 0; vi < t.voices.size(); vi++)
                    if (t.voices[vi].active && !t.voices[vi].released) next = Min(next, Max(cur, t.offAt[vi]));
            }
            if (next > cur) {
                renderSpan(cur, next, L, R, (int)(cur - pos));
                cur = next;
            }
            // note-offs due
            for (size_t ti = 0; ti < ts.size(); ti++) {
                TrackState& t = ts[ti];
                for (size_t vi = 0; vi < t.voices.size(); vi++)
                    if (t.voices[vi].active && !t.voices[vi].released && t.offAt[vi] <= cur) voiceRelease(t.voices[vi]);
            }
            while (ev < sd.events.size() && sd.events[ev].start <= cur) {
                trigger(sd.events[ev]);
                ev++;
            }
            if (next == cur && cur < end) {
                // guard against zero-length loops
                bool pending = ev < sd.events.size() && sd.events[ev].start <= cur;
                if (!pending) {
                    bool anyOff = false;
                    for (size_t ti = 0; ti < ts.size() && !anyOff; ti++)
                        for (size_t vi = 0; vi < ts[ti].voices.size(); vi++)
                            if (ts[ti].voices[vi].active && !ts[ti].voices[vi].released && ts[ti].offAt[vi] <= cur) { anyOff = true; break; }
                    if (!anyOff) {
                        renderSpan(cur, end, L, R, (int)(cur - pos));
                        cur = end;
                    }
                }
            }
        }
        // ---- mix tracks
        for (int i = 0; i < n; i++) sendL[i] = sendR[i] = dSendL[i] = dSendR[i] = 0.f;
        float scStart = scEnv;
        for (size_t ti = 0; ti < ts.size(); ti++) {
            TrackState& t = ts[ti];
            const TrackDef& td = sd.tracks[ti];
            float targetG = td.layer >= 0 ? layerGain[Clamp(td.layer, 0, 3)] : 1.f;
            float g0 = t.gainNow, g1 = targetG;
            t.gainNow = targetG;
            if (g0 < 1e-5f && g1 < 1e-5f) continue;
            float gl, gr;
            panGains(td.pan, gl, gr);
            gl *= 1.41f * td.gain;
            gr *= 1.41f * td.gain;
            float sc = scStart;
            for (int i = 0; i < n; i++) {
                float l = t.bufL[i], r = t.bufR[i];
                if (td.drive > 0.f) {
                    float dg = 1.f + td.drive * 14.f, comp = 1.f / fastTanh(dg * 0.5f);
                    l = fastTanh(l * dg) * comp * 0.5f;
                    r = fastTanh(r * dg) * comp * 0.5f;
                }
                if (td.cab) {
                    l = t.cab[0][2].process(t.cab[0][1].process(t.cab[0][0].process(l)));
                    r = t.cab[1][2].process(t.cab[1][1].process(t.cab[1][0].process(r)));
                }
                for (int k = 0; k < t.eqN; k++) {
                    l = t.eq[0][k].process(l);
                    r = t.eq[1][k].process(r);
                }
                if (t.hasChorus) t.chorus.process(l, r);
                float lg = g0 + (g1 - g0) * ((float)i / (float)n);
                if (td.sidechain > 0.f) {
                    float duck = 1.f - td.sidechain * sc;
                    sc *= scK;
                    lg *= duck;
                }
                l *= gl * lg;
                r *= gr * lg;
                L[i] += l;
                R[i] += r;
                sendL[i] += l * td.rev;
                sendR[i] += r * td.rev;
                dSendL[i] += l * td.dly;
                dSendR[i] += r * td.dly;
            }
        }
        for (int i = 0; i < n; i++) scEnv *= scK;
        // delay -> also feeds reverb a bit
        for (int i = 0; i < n; i++) {
            float ol, orr;
            dly.process(dSendL[i], dSendR[i], ol, orr);
            L[i] += ol;
            R[i] += orr;
            sendL[i] += ol * 0.3f;
            sendR[i] += orr * 0.3f;
        }
        float rl[kProdBlock], rr[kProdBlock];
        rev.process(sendL, sendR, rl, rr, n);
        for (int i = 0; i < n; i++) {
            L[i] += rl[i];
            R[i] += rr[i];
        }
        // master automation (filter sweeps)
        if (!sd.autom.empty()) {
            while (autoIdx + 1 < sd.autom.size() && sd.autom[autoIdx + 1].at <= pos) autoIdx++;
            const Automation& a0 = sd.autom[autoIdx];
            float hp = a0.hp, lp = a0.lp;
            if (autoIdx + 1 < sd.autom.size() && pos >= a0.at) {
                const Automation& a1 = sd.autom[autoIdx + 1];
                float u = Saturate((float)(pos - a0.at) / (float)Max(1u, a1.at - a0.at));
                hp = a0.hp + (a1.hp - a0.hp) * u;
                lp = expf(logf(Max(a0.lp, 20.f)) + (logf(Max(a1.lp, 20.f)) - logf(Max(a0.lp, 20.f))) * u);
            }
            autoHp = hp;
            autoLp = lp;
            if (autoHp > 25.f) {
                float g = svfG(autoHp);
                autoHpL.setG(g, 0.9f);
                autoHpR.setG(g, 0.9f);
                for (int i = 0; i < n; i++) {
                    L[i] = autoHpL.hp(L[i]);
                    R[i] = autoHpR.hp(R[i]);
                }
            }
            if (autoLp < 19000.f) {
                float g = svfG(autoLp);
                autoLpL.setG(g, 0.9f);
                autoLpR.setG(g, 0.9f);
                for (int i = 0; i < n; i++) {
                    L[i] = autoLpL.lp(L[i]);
                    R[i] = autoLpR.lp(R[i]);
                }
            }
        }
        // lo-fi: tape wow + vinyl
        if (sd.wow > 0.f || sd.vinyl > 0.f) {
            for (int i = 0; i < n; i++) {
                if (sd.wow > 0.f) {
                    wowL.write(L[i]);
                    wowR.write(R[i]);
                    wowPh += 0.55f * kInvSR;
                    if (wowPh >= 1.f) wowPh -= 1.f;
                    float d = 240.f + sd.wow * 60.f * sinWrapped(wowPh);
                    L[i] = wowL.read(d);
                    R[i] = wowR.read(d);
                }
                if (sd.vinyl > 0.f) {
                    float h = hissLp.process(nz.white()) * 0.006f * sd.vinyl;
                    if (nz.uni() < 6.f * kInvSR) crackleEnv = nz.range(0.2f, 1.f) * sd.vinyl;
                    float c = nz.white() * crackleEnv * 0.25f;
                    crackleEnv *= 0.7f;
                    L[i] += h + c;
                    R[i] += h + c * 0.8f;
                }
            }
        }
        // bus glue + master gain + end fade
        bus.processStereo(L, R, n);
        float m = sd.master;
        for (int i = 0; i < n; i++) {
            L[i] = softClip(L[i] * m);
            R[i] = softClip(R[i] * m);
        }
        pos = end;
    }
};

// =============================================================================================
// Composition
enum class Genre : int { Synthwave = 0, HipHop, Reggaeton, House, Rock, Jazz, Country, LoFi, Talk, Count };
enum class Part : u8 { Intro, Verse, Pre, Chorus, Bridge, Breakdown, Build, Drop, Solo, Outro };
enum class Mode : u8 { Song, Jingle, Bed };

struct Section {
    Part part;
    int bars;
    int startBar;
    float energy;
    int prog;
};

struct SongPlan {
    Genre genre = Genre::Synthwave;
    u32 seed = 0;
    float bpm = 110.f;
    KeySig key;
    float swing = 0.f;
    bool swing16 = false;
    int variant = 0;
    Mode mode = Mode::Song;
    std::vector<Section> sections;
    int totalBars = 0;
    float barSec() const { return 240.f / bpm; }
    float durationSec() const { return (float)totalBars * barSec() + (mode == Mode::Song ? 2.5f : 1.5f); }
};

static float partEnergy(Part p) {
    switch (p) {
        case Part::Intro: return 0.35f;
        case Part::Verse: return 0.6f;
        case Part::Pre: return 0.75f;
        case Part::Chorus: return 1.f;
        case Part::Bridge: return 0.5f;
        case Part::Breakdown: return 0.3f;
        case Part::Build: return 0.8f;
        case Part::Drop: return 1.f;
        case Part::Solo: return 0.85f;
        case Part::Outro: return 0.4f;
    }
    return 0.5f;
}

static void finalizeSections(SongPlan& p) {
    int b = 0;
    for (auto& s : p.sections) {
        s.startBar = b;
        s.energy = partEnergy(s.part);
        b += s.bars;
    }
    p.totalBars = b;
}

// Builds a form from a template and fits it to 150..235 seconds.
static void fitForm(SongPlan& p, std::vector<Section> form, Rng& r) {
    p.sections = form;
    finalizeSections(p);
    float minD = r.range(150.f, 170.f), maxD = r.range(205.f, 235.f);
    for (int guard = 0; guard < 20 && p.durationSec() > maxD; guard++) {
        // shrink: halve the longest non-chorus section, else drop a middle section
        int best = -1, bestBars = 4;
        for (int i = 1; i + 1 < (int)p.sections.size(); i++)
            if (p.sections[(size_t)i].bars > bestBars && p.sections[(size_t)i].part != Part::Chorus && p.sections[(size_t)i].part != Part::Drop) {
                best = i;
                bestBars = p.sections[(size_t)i].bars;
            }
        if (best >= 0) p.sections[(size_t)best].bars /= 2;
        else if (p.sections.size() > 4) p.sections.erase(p.sections.begin() + (long)(p.sections.size() - 3));
        else break;
        finalizeSections(p);
    }
    for (int guard = 0; guard < 10 && p.durationSec() < minD; guard++) {
        // extend: repeat the last chorus (or the longest section)
        int idx = -1;
        for (int i = (int)p.sections.size() - 1; i >= 0; i--)
            if (p.sections[(size_t)i].part == Part::Chorus || p.sections[(size_t)i].part == Part::Drop) { idx = i; break; }
        if (idx < 0) idx = (int)p.sections.size() / 2;
        Section s = p.sections[(size_t)idx];
        p.sections.insert(p.sections.begin() + idx + 1, s);
        finalizeSections(p);
    }
}

static const int kMinorTonics[] = {9, 4, 6, 1, 2, 11, 0, 7, 5};
static const int kMajorTonics[] = {7, 2, 9, 0, 4, 5, 10, 3};

SongPlan planSong(Genre g, u32 seed) {
    Rng r(seed, 0x51A7u);
    SongPlan p;
    p.genre = g;
    p.seed = seed;
    p.mode = Mode::Song;
    auto minorKey = [&](float dorianChance) {
        p.key.tonic = r.pick(kMinorTonics);
        p.key.scale = r.chance(dorianChance) ? Scale::Dorian : Scale::Minor;
    };
    auto majorKey = [&](float mixoChance) {
        p.key.tonic = r.pick(kMajorTonics);
        p.key.scale = r.chance(mixoChance) ? Scale::Mixolydian : Scale::Major;
    };
    typedef Section S;
    switch (g) {
        case Genre::Synthwave: {
            p.bpm = (float)r.irange(88, 116);
            p.variant = r.irange(0, 2);
            if (r.chance(0.8f)) minorKey(0.2f);
            else majorKey(0.f);
            fitForm(p, {S{Part::Intro, 8, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 1}, S{Part::Chorus, 16, 0, 0, 2}, S{Part::Verse, 16, 0, 0, 1},
                        S{Part::Chorus, 16, 0, 0, 2}, S{Part::Bridge, 8, 0, 0, 3}, S{Part::Chorus, 16, 0, 0, 2}, S{Part::Outro, 8, 0, 0, 0}}, r);
            break;
        }
        case Genre::HipHop: {
            p.variant = r.chance(0.6f) ? 0 : 1;  // 0 trap, 1 boom-bap
            if (p.variant == 0) {
                p.bpm = (float)r.irange(132, 150);
                minorKey(0.f);
                if (r.chance(0.35f)) p.key.scale = Scale::HarmonicMinor;
                fitForm(p, {S{Part::Intro, 4, 0, 0, 0}, S{Part::Chorus, 8, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 1}, S{Part::Chorus, 8, 0, 0, 0},
                            S{Part::Verse, 16, 0, 0, 1}, S{Part::Bridge, 4, 0, 0, 0}, S{Part::Chorus, 8, 0, 0, 0}, S{Part::Outro, 4, 0, 0, 0}}, r);
            } else {
                p.bpm = (float)r.irange(84, 96);
                p.swing = r.range(0.35f, 0.6f);
                p.swing16 = true;
                minorKey(0.4f);
                fitForm(p, {S{Part::Intro, 4, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 0}, S{Part::Chorus, 8, 0, 0, 1}, S{Part::Verse, 16, 0, 0, 0},
                            S{Part::Chorus, 8, 0, 0, 1}, S{Part::Verse, 8, 0, 0, 0}, S{Part::Chorus, 8, 0, 0, 1}, S{Part::Outro, 4, 0, 0, 0}}, r);
            }
            break;
        }
        case Genre::Reggaeton: {
            p.bpm = (float)r.irange(88, 100);
            p.variant = r.irange(0, 1);
            if (r.chance(0.75f)) minorKey(0.1f);
            else majorKey(0.f);
            fitForm(p, {S{Part::Intro, 8, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 0}, S{Part::Pre, 8, 0, 0, 1}, S{Part::Chorus, 16, 0, 0, 2},
                        S{Part::Verse, 16, 0, 0, 0}, S{Part::Pre, 8, 0, 0, 1}, S{Part::Chorus, 16, 0, 0, 2}, S{Part::Bridge, 8, 0, 0, 1},
                        S{Part::Chorus, 16, 0, 0, 2}, S{Part::Outro, 4, 0, 0, 0}}, r);
            break;
        }
        case Genre::House: {
            p.bpm = (float)r.irange(120, 127);
            p.variant = r.irange(0, 1);  // 0 piano house, 1 big-room supersaw
            if (r.chance(0.7f)) minorKey(0.2f);
            else majorKey(0.f);
            fitForm(p, {S{Part::Intro, 16, 0, 0, 0}, S{Part::Build, 8, 0, 0, 0}, S{Part::Drop, 16, 0, 0, 1}, S{Part::Breakdown, 16, 0, 0, 2},
                        S{Part::Build, 8, 0, 0, 2}, S{Part::Drop, 16, 0, 0, 1}, S{Part::Outro, 16, 0, 0, 0}}, r);
            break;
        }
        case Genre::Rock: {
            p.bpm = (float)r.irange(108, 150);
            p.variant = r.irange(0, 1);
            static const int kRockTonics[] = {4, 9, 2, 7, 0};
            p.key.tonic = r.pick(kRockTonics);
            p.key.scale = r.chance(0.45f) ? Scale::Minor : (r.chance(0.3f) ? Scale::Mixolydian : Scale::Major);
            fitForm(p, {S{Part::Intro, 8, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 1}, S{Part::Pre, 8, 0, 0, 2}, S{Part::Chorus, 16, 0, 0, 0},
                        S{Part::Verse, 16, 0, 0, 1}, S{Part::Chorus, 16, 0, 0, 0}, S{Part::Solo, 16, 0, 0, 1}, S{Part::Chorus, 16, 0, 0, 0},
                        S{Part::Outro, 4, 0, 0, 0}}, r);
            break;
        }
        case Genre::Jazz: {
            p.variant = r.chance(0.6f) ? 0 : 1;  // 0 swing, 1 bossa
            static const int kJazzTonics[] = {5, 10, 3, 0, 7, 8};
            p.key.tonic = r.pick(kJazzTonics);
            p.key.scale = Scale::Major;
            if (p.variant == 0) {
                p.bpm = (float)r.irange(92, 138);
                p.swing = r.range(0.75f, 0.95f);
            } else {
                p.bpm = (float)r.irange(118, 138);
            }
            fitForm(p, {S{Part::Intro, 4, 0, 0, 1}, S{Part::Verse, 8, 0, 0, 0}, S{Part::Verse, 8, 0, 0, 0}, S{Part::Bridge, 8, 0, 0, 1},
                        S{Part::Verse, 8, 0, 0, 0}, S{Part::Solo, 16, 0, 0, 0}, S{Part::Solo, 16, 0, 0, 1}, S{Part::Verse, 8, 0, 0, 0},
                        S{Part::Verse, 8, 0, 0, 0}, S{Part::Outro, 4, 0, 0, 2}}, r);
            break;
        }
        case Genre::Country: {
            p.bpm = (float)r.irange(92, 128);
            p.variant = r.irange(0, 2);  // 0 train beat, 1 shuffle, 2 bluegrass-ish
            if (p.variant == 1) p.swing = 0.6f;
            static const int kCountryTonics[] = {7, 2, 9, 0, 4};
            p.key.tonic = r.pick(kCountryTonics);
            p.key.scale = Scale::Major;
            fitForm(p, {S{Part::Intro, 4, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 0}, S{Part::Chorus, 16, 0, 0, 1}, S{Part::Verse, 16, 0, 0, 0},
                        S{Part::Chorus, 16, 0, 0, 1}, S{Part::Solo, 8, 0, 0, 0}, S{Part::Chorus, 16, 0, 0, 1}, S{Part::Outro, 4, 0, 0, 0}}, r);
            break;
        }
        case Genre::LoFi: {
            p.bpm = (float)r.irange(70, 88);
            p.swing = r.range(0.4f, 0.7f);
            p.swing16 = true;
            p.key.tonic = r.irange(0, 11);
            p.key.scale = r.chance(0.5f) ? Scale::Major : Scale::Dorian;
            fitForm(p, {S{Part::Intro, 4, 0, 0, 0}, S{Part::Verse, 16, 0, 0, 0}, S{Part::Chorus, 16, 0, 0, 1}, S{Part::Verse, 16, 0, 0, 0},
                        S{Part::Chorus, 16, 0, 0, 1}, S{Part::Outro, 4, 0, 0, 0}}, r);
            break;
        }
        default: {
            p.bpm = 112.f;
            majorKey(0.f);
            fitForm(p, {S{Part::Verse, 8, 0, 0, 0}}, r);
            break;
        }
    }
    return p;
}

// ---- Instrument presets
static TrackDef tdDrums(float gain, float rev) {
    TrackDef t;
    t.drums = true;
    t.patch.eng = Eng::Drums;
    t.gain = gain;
    t.rev = rev;
    return t;
}
static Patch pSawBass() {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Saw;
    p.mix2 = 0.45f; p.w2 = Wave::Square; p.oct2 = -1; p.det2 = 0.f;
    p.subLvl = 0.35f;
    p.cutoff = 320.f; p.reso = 0.9f; p.fEnv = 2.6f; p.fA = 0.001f; p.fD = 0.16f; p.fS = 0.15f; p.keyTrk = 0.4f;
    p.aA = 0.002f; p.aD = 0.3f; p.aS = 0.85f; p.aR = 0.06f;
    p.mono = true;
    p.gain = 0.55f;
    return p;
}
static Patch pSubBass() {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Sine;
    p.mix2 = 0.3f; p.w2 = Wave::Triangle; p.det2 = 0.f; p.oct2 = 1;
    p.cutoff = 900.f; p.reso = 0.6f; p.keyTrk = 0.f;
    p.aA = 0.003f; p.aD = 0.4f; p.aS = 0.8f; p.aR = 0.07f;
    p.mono = true;
    p.gain = 0.8f;
    return p;
}
static Patch pSupersaw(float att, float rel, float cutoff) {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Saw;
    p.unison = 5; p.uniDet = 0.13f; p.uniSpread = 0.9f;
    p.cutoff = cutoff; p.reso = 0.6f; p.keyTrk = 0.3f; p.fEnv = 0.6f; p.fA = att * 0.5f; p.fD = 0.8f; p.fS = 0.6f;
    p.aA = att; p.aD = 0.5f; p.aS = 0.85f; p.aR = rel;
    p.poly = 6;
    p.gain = 0.22f;
    p.velSens = 0.3f;
    return p;
}
static Patch pPluck(Wave w, float cutoff, float decay) {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = w;
    p.pw = 0.32f;
    p.mix2 = 0.25f; p.w2 = Wave::Saw; p.det2 = 0.1f;
    p.cutoff = cutoff; p.reso = 1.1f; p.fEnv = 3.2f; p.fA = 0.001f; p.fD = decay * 0.6f; p.fS = 0.f;
    p.aA = 0.002f; p.aD = decay; p.aS = 0.f; p.aR = 0.12f;
    p.poly = 6;
    p.gain = 0.35f;
    return p;
}
static Patch pSawLead() {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Saw;
    p.mix2 = 0.5f; p.w2 = Wave::Square; p.det2 = 0.12f;
    p.cutoff = 2600.f; p.reso = 0.8f; p.fEnv = 1.2f; p.fA = 0.005f; p.fD = 0.3f; p.fS = 0.4f; p.keyTrk = 0.6f;
    p.aA = 0.008f; p.aD = 0.3f; p.aS = 0.85f; p.aR = 0.18f;
    p.vibDepth = 0.22f; p.vibDelay = 0.3f; p.vibRate = 5.4f;
    p.glide = 0.05f;
    p.mono = true;
    p.gain = 0.3f;
    return p;
}
static Patch pSquareLead() {
    Patch p = pSawLead();
    p.w1 = Wave::Pulse;
    p.pw = 0.45f; p.pwmDepth = 0.15f; p.pwmRate = 0.7f;
    p.mix2 = 0.f;
    p.cutoff = 3200.f;
    return p;
}
static Patch pFmBell() {
    Patch p;
    p.eng = Eng::Fm;
    p.fmRatio = 3.5f; p.fmIndex = 2.2f; p.fmIdxDecay = 0.35f; p.fmIdxSus = 0.05f;
    p.aA = 0.001f; p.aD = 1.4f; p.aS = 0.f; p.aR = 0.9f;
    p.poly = 8;
    p.gain = 0.3f;
    return p;
}
static Patch pFmEP() {
    Patch p;
    p.eng = Eng::Fm;
    p.fmRatio = 1.f; p.fmIndex = 1.6f; p.fmIdxDecay = 0.9f; p.fmIdxSus = 0.25f;
    p.fmRatio2 = 14.f; p.fmIndex2 = 1.1f; p.fmIdx2Decay = 0.02f;
    p.fmDetune = 4.f;
    p.tremRate = 4.3f; p.tremDepth = 0.18f;
    p.aA = 0.002f; p.aD = 3.5f; p.aS = 0.25f; p.aR = 0.3f;
    p.poly = 10;
    p.gain = 0.3f;
    p.velSens = 0.7f;
    return p;
}
static Patch pFmBass() {
    Patch p;
    p.eng = Eng::Fm;
    p.fmRatio = 1.f; p.fmIndex = 2.8f; p.fmIdxDecay = 0.14f; p.fmIdxSus = 0.15f; p.fmFeedback = 0.25f;
    p.aA = 0.002f; p.aD = 0.5f; p.aS = 0.7f; p.aR = 0.07f;
    p.mono = true;
    p.gain = 0.6f;
    return p;
}
static Patch pMallet(float ratio, float dec) {
    Patch p;
    p.eng = Eng::Fm;
    p.fmRatio = ratio; p.fmIndex = 1.3f; p.fmIdxDecay = 0.05f; p.fmIdxSus = 0.f;
    p.aA = 0.001f; p.aD = dec; p.aS = 0.f; p.aR = dec * 0.6f;
    p.poly = 8;
    p.gain = 0.35f;
    return p;
}
static Patch pVibes() {
    Patch p = pMallet(4.f, 2.2f);
    p.fmIndex = 0.7f; p.fmIdxDecay = 0.3f;
    p.tremRate = 5.2f; p.tremDepth = 0.35f;
    p.gain = 0.3f;
    return p;
}
static Patch pPiano() {
    Patch p;
    p.eng = Eng::Piano;
    p.aA = 0.001f; p.aD = 20.f; p.aS = 1.f; p.aR = 0.22f;
    p.poly = 12;
    p.gain = 0.5f;
    p.velSens = 0.5f;
    return p;
}
static Patch pString(float decay, float bright, float pick, float noiseLp) {
    Patch p;
    p.eng = Eng::Pluck;
    p.kDecay = decay; p.kBright = bright; p.kPick = pick; p.kNoiseLP = noiseLp;
    p.aA = 0.001f; p.aD = 1.f; p.aS = 1.f; p.aR = 0.08f;
    p.poly = 6;
    p.gain = 0.6f;
    p.velSens = 0.5f;
    return p;
}
static Patch pVox() {
    Patch p;
    p.eng = Eng::Vox;
    p.aA = 0.035f; p.aD = 0.2f; p.aS = 0.9f; p.aR = 0.16f;
    p.vibRate = 5.6f; p.vibDepth = 0.28f; p.vibDelay = 0.18f;
    p.glide = 0.06f;
    p.breath = 0.08f; p.voxBright = 0.55f;
    p.mono = true;
    p.gain = 0.35f;
    p.vowelA = 0; p.vowelB = 1;
    return p;
}
static Patch pChoir() {
    Patch p = pVox();
    p.mono = false;
    p.poly = 6;
    p.glide = 0.f;
    p.aA = 0.35f; p.aR = 0.7f;
    p.vibDepth = 0.1f;
    p.breath = 0.15f;
    p.gain = 0.16f;
    p.vowelA = 0; p.vowelB = 1;
    return p;
}
static Patch pStrings() {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Saw;
    p.unison = 3; p.uniDet = 0.08f; p.uniSpread = 0.7f;
    p.cutoff = 2800.f; p.reso = 0.6f; p.keyTrk = 0.4f;
    p.aA = 0.3f; p.aD = 0.5f; p.aS = 0.9f; p.aR = 0.6f;
    p.vibDepth = 0.07f; p.vibDelay = 0.3f; p.vibRate = 5.f;
    p.poly = 8;
    p.gain = 0.2f;
    return p;
}
static Patch pBrass() {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Saw;
    p.unison = 3; p.uniDet = 0.07f; p.uniSpread = 0.5f;
    p.cutoff = 700.f; p.reso = 0.7f; p.fEnv = 2.f; p.fA = 0.03f; p.fD = 0.3f; p.fS = 0.45f; p.keyTrk = 0.5f;
    p.aA = 0.02f; p.aD = 0.3f; p.aS = 0.85f; p.aR = 0.15f;
    p.pEnv = -0.4f; p.pDecay = 0.04f;
    p.vibDepth = 0.08f; p.vibDelay = 0.3f;
    p.poly = 6;
    p.gain = 0.25f;
    return p;
}
static Patch pOrgan() {
    Patch p;
    p.eng = Eng::Organ;
    p.aA = 0.005f; p.aD = 0.1f; p.aS = 1.f; p.aR = 0.05f;
    p.poly = 8;
    p.gain = 0.4f;
    p.velSens = 0.1f;
    return p;
}
static Patch p808() {
    Patch p;
    p.eng = Eng::Bass808;
    p.aA = 0.002f; p.aD = 1.6f; p.aS = 0.4f; p.aR = 0.12f;
    p.drive = 0.4f;
    p.glide = 0.045f;
    p.mono = true;
    p.pEnv = 0.6f; p.pDecay = 0.02f;
    p.gain = 0.75f;
    p.velSens = 0.3f;
    return p;
}
static Patch pSoftLead(Wave w) {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = w;
    p.mix2 = 0.15f; p.w2 = Wave::Triangle; p.oct2 = 1; p.det2 = 0.f;
    p.noiseLvl = 0.035f;
    p.cutoff = 3000.f; p.reso = 0.5f; p.keyTrk = 0.4f;
    p.aA = 0.04f; p.aD = 0.3f; p.aS = 0.85f; p.aR = 0.15f;
    p.vibDepth = 0.2f; p.vibDelay = 0.2f; p.vibRate = 5.3f;
    p.glide = 0.05f;
    p.mono = true;
    p.gain = 0.4f;
    return p;
}
static Patch pSax() {
    Patch p;
    p.eng = Eng::Sub;
    p.w1 = Wave::Saw;
    p.mix2 = 0.35f; p.w2 = Wave::Pulse; p.pw = 0.3f; p.det2 = 0.05f;
    p.noiseLvl = 0.05f;
    p.fmode = FMode::LP;
    p.cutoff = 1300.f; p.reso = 1.4f; p.fEnv = 1.2f; p.fA = 0.04f; p.fD = 0.25f; p.fS = 0.55f; p.keyTrk = 0.6f; p.velCut = 1.2f;
    p.aA = 0.035f; p.aD = 0.3f; p.aS = 0.85f; p.aR = 0.12f;
    p.vibDepth = 0.2f; p.vibDelay = 0.25f; p.vibRate = 5.2f;
    p.glide = 0.03f;
    p.drive = 0.25f;
    p.mono = true;
    p.gain = 0.35f;
    return p;
}

// ---- Composer context
struct Comp {
    SongData& sd;
    const SongPlan& pl;
    Rng rng;
    float spb;
    std::vector<Chord> beatChord;
    Comp(SongData& s, const SongPlan& p) : sd(s), pl(p), rng(p.seed ^ 0xC0FFEEu, 99) { spb = 60.f / p.bpm * kSR; }
    int track(const TrackDef& t) {
        sd.tracks.push_back(t);
        return (int)sd.tracks.size() - 1;
    }
    double swingPos(double beat) const {
        double b = floor(beat), f = beat - b;
        if (sd.swing > 0.f) {
            if (!pl.swing16) {
                if (fabs(f - 0.5) < 1e-3) f = 0.5 + sd.swing / 6.0;
            } else {
                double q = f * 4.0;
                int qi = (int)floor(q + 1e-4);
                if (fabs(q - (double)qi) < 1e-3 && (qi & 1)) f = (double)qi * 0.25 + sd.swing / 12.0;
            }
        }
        return b + f;
    }
    void note(int tr, int bar, float beat, float len, int midi, int vel, u8 flags = 0, float humanMs = 5.f) {
        double b0 = swingPos((double)bar * 4.0 + (double)beat);
        double b1 = swingPos((double)bar * 4.0 + (double)beat + (double)len);
        double h = humanMs > 0.f ? (double)rng.range(-humanMs, humanMs) * 0.001 * kSR : 0.0;
        i64 s = (i64)(b0 * spb + h);
        if (s < 0) s = 0;
        i64 e = (i64)(b1 * spb + h);
        NoteEv ev;
        ev.start = (u32)s;
        ev.dur = (u32)Max<i64>(1, e - s);
        ev.track = (u8)tr;
        ev.note = (u8)Clamp(midi, 0, 127);
        ev.vel = (u8)Clamp(vel, 1, 127);
        ev.flags = flags;
        sd.events.push_back(ev);
    }
    void hit(int tr, int bar, float beat, int piece, int vel, float humanMs = 4.f) {
        sd.kitUsed[piece] = true;
        note(tr, bar, beat, 0.25f, piece, vel, 0, humanMs);
    }
    const Chord& chordAt(int bar, float beat) const {
        int i = Clamp(bar * 4 + (int)beat, 0, (int)beatChord.size() - 1);
        return beatChord[(size_t)i];
    }
    bool chordChangesAt(int bar, float beat) const {
        int i = bar * 4 + (int)beat;
        if (i <= 0 || i >= (int)beatChord.size()) return true;
        const Chord& a = beatChord[(size_t)i];
        const Chord& b = beatChord[(size_t)(i - 1)];
        return a.root != b.root || a.n != b.n || a.iv[1] != b.iv[1];
    }
    void buildChords(const std::vector<std::vector<PStep>>& progs) {
        beatChord.assign((size_t)pl.totalBars * 4, Chord());
        for (const Section& s : pl.sections) {
            const auto& pr = progs[(size_t)Clamp(s.prog, 0, (int)progs.size() - 1)];
            int b = 0, total = s.bars * 4;
            while (b < total) {
                for (const PStep& st : pr) {
                    Chord c = buildChord(pl.key, st);
                    for (int k = 0; k < st.beats && b < total; k++, b++) beatChord[(size_t)(s.startBar * 4 + b)] = c;
                    if (b >= total) break;
                }
            }
        }
    }
    int bassPitch(const Chord& c, int lo) const {
        int pc = c.bassPc();
        int p = lo + ((pc - lo) % 12 + 12) % 12;
        return p;
    }
    int nearestChordTone(const Chord& c, int midi) const {
        int best = midi, bd = 99;
        for (int d = -6; d <= 6; d++) {
            int p = midi + d;
            if (c.hasPc(p) && abs(d) < bd) { bd = abs(d); best = p; }
        }
        return best;
    }
    const Section& sectionOfBar(int bar) const {
        for (const Section& s : pl.sections)
            if (bar >= s.startBar && bar < s.startBar + s.bars) return s;
        return pl.sections.back();
    }
};

// ---- Melody generation (motif based)
struct Cell {
    float len;
    int n;
    float on[4];
    float du[4];
};
static const Cell kCellsPop[] = {
    {1, 1, {0}, {1}}, {1, 2, {0, 0.5f}, {0.5f, 0.5f}}, {2, 2, {0, 1.5f}, {1.5f, 0.5f}}, {2, 1, {0}, {2}},
    {1, 1, {0.5f}, {0.5f}}, {2, 3, {0, 0.5f, 1}, {0.5f, 0.5f, 1}}, {1, 0, {0}, {0}}, {2, 2, {0.5f, 1.5f}, {1, 0.5f}}};
static const Cell kCellsBusy[] = {
    {1, 2, {0, 0.5f}, {0.5f, 0.5f}}, {1, 4, {0, 0.25f, 0.5f, 0.75f}, {0.25f, 0.25f, 0.25f, 0.25f}}, {1, 3, {0, 0.5f, 0.75f}, {0.5f, 0.25f, 0.25f}},
    {1, 3, {0, 0.25f, 0.5f}, {0.25f, 0.25f, 0.5f}}, {1, 1, {0}, {1}}, {1, 2, {0.25f, 0.75f}, {0.5f, 0.25f}}, {2, 2, {0, 1.5f}, {1.5f, 0.5f}}};
static const Cell kCellsSparse[] = {
    {2, 1, {0}, {2}}, {4, 1, {0}, {4}}, {2, 2, {0, 1}, {1, 1}}, {2, 1, {0.5f}, {1.5f}}, {2, 0, {0}, {0}}, {3, 2, {0, 1.5f}, {1.5f, 1.5f}}};
static const Cell kCellsLatin[] = {
    {2, 3, {0, 0.75f, 1.5f}, {0.75f, 0.75f, 0.5f}}, {1, 2, {0, 0.5f}, {0.5f, 0.5f}}, {2, 2, {0, 1}, {1, 1}}, {1, 1, {0.5f}, {0.5f}},
    {2, 3, {0.5f, 1.f, 1.5f}, {0.5f, 0.5f, 0.5f}}, {1, 1, {0}, {1}}};
static const Cell kCellsSwing[] = {
    {1, 2, {0, 0.5f}, {0.5f, 0.5f}}, {1, 2, {0, 0.5f}, {0.5f, 0.5f}}, {1, 1, {0}, {1}}, {2, 3, {0, 0.5f, 1.f}, {0.5f, 0.5f, 1.f}},
    {1, 1, {0.5f}, {0.5f}}, {2, 4, {0, 0.5f, 1.f, 1.5f}, {0.5f, 0.5f, 0.5f, 0.5f}}};

enum class MelStyle : u8 { Pop, Busy, Sparse, Latin, Swing };

struct MNote {
    float on, du;
    int step;  // scale-step move from previous note
};

struct Motif {
    std::vector<MNote> notes;
    float len = 4.f;
};

static Motif makeMotif(Rng& r, MelStyle st, float len) {
    const Cell* cells;
    int nc;
    switch (st) {
        case MelStyle::Busy: cells = kCellsBusy; nc = (int)ARRAY_COUNT(kCellsBusy); break;
        case MelStyle::Sparse: cells = kCellsSparse; nc = (int)ARRAY_COUNT(kCellsSparse); break;
        case MelStyle::Latin: cells = kCellsLatin; nc = (int)ARRAY_COUNT(kCellsLatin); break;
        case MelStyle::Swing: cells = kCellsSwing; nc = (int)ARRAY_COUNT(kCellsSwing); break;
        default: cells = kCellsPop; nc = (int)ARRAY_COUNT(kCellsPop); break;
    }
    Motif m;
    m.len = len;
    float t = 0.f;
    int dir = r.chance(0.5f) ? 1 : -1;
    int guard = 0;
    while (t < len - 0.01f && guard++ < 64) {
        const Cell& c = cells[r.irange(0, nc - 1)];
        if (t + c.len > len + 0.01f) continue;
        if (t == 0.f && (c.n == 0 || c.on[0] > 0.6f)) continue;
        for (int i = 0; i < c.n; i++) {
            MNote n;
            n.on = t + c.on[i];
            n.du = c.du[i];
            float rr = r.f();
            int step;
            if (rr < 0.18f) step = 0;
            else if (rr < 0.72f) step = dir;
            else if (rr < 0.87f) step = 2 * dir;
            else if (rr < 0.95f) step = -dir;
            else step = (r.chance(0.5f) ? 3 : 4) * dir;
            if (r.chance(0.3f)) dir = -dir;
            n.step = m.notes.empty() ? 0 : step;
            m.notes.push_back(n);
        }
        t += c.len;
    }
    if (m.notes.empty()) m.notes.push_back(MNote{0.f, len, 0});
    return m;
}

struct MelodyWriter {
    Comp& c;
    int tr;
    int lo, hi;
    int cur;
    float velBase = 96.f;
    float legato = 0.95f;
    MelodyWriter(Comp& comp, int track, int loMidi, int hiMidi) : c(comp), tr(track), lo(loMidi), hi(hiMidi) {
        cur = (lo + hi) / 2;
    }
    // Plays motif m starting at absolute beat (bar*4+beat). transform: 0 as-is, 1 shift up, 2 shift down,
    // 3 inverted, 4 cadence (end on stable tone with long last note).
    void play(const Motif& m, int bar, float beat, int transform, int startDeg, bool snapStrong = true) {
        const KeySig& k = c.pl.key;
        int deg = startDeg;
        if (transform == 1) deg += 2;
        if (transform == 2) deg -= 1;
        int count = (int)m.notes.size();
        for (int i = 0; i < count; i++) {
            const MNote& n = m.notes[(size_t)i];
            int step = n.step;
            if (transform == 3) step = -step;
            deg += step;
            float absBeat = beat + n.on;
            int b = bar + (int)(absBeat / 4.f);
            float bb = fmodf(absBeat, 4.f);
            int pitch = degPitch(k, deg, 60);
            while (pitch > hi) { deg -= 7; pitch = degPitch(k, deg, 60); }
            while (pitch < lo) { deg += 7; pitch = degPitch(k, deg, 60); }
            const Chord& ch = c.chordAt(b, bb);
            bool strong = fabsf(bb - floorf(bb)) < 0.01f && (((int)bb % 2) == 0 || n.du >= 1.f);
            float du = n.du;
            bool last = (i == count - 1);
            if (last && transform == 4) {
                // cadence: resolve to root/third of the current chord, hold
                int root = c.nearestChordTone(ch, pitch);
                int best = root, bd = 99;
                for (int d = -5; d <= 5; d++) {
                    int p = pitch + d;
                    int pc = ((p - ch.root) % 12 + 12) % 12;
                    if ((pc == 0 || pc == ch.iv[1]) && abs(d) < bd && p >= lo && p <= hi) { bd = abs(d); best = p; }
                }
                pitch = best;
                du = Max(du, m.len - n.on);
            } else if (snapStrong && (strong || du >= 1.5f) && !ch.hasPc(pitch)) {
                int p2 = c.nearestChordTone(ch, pitch);
                if (p2 >= lo && p2 <= hi) pitch = p2;
            } else if (!inScale(k, pitch)) {
                pitch += 1;
            }
            deg = pitchToDeg(k, pitch, 60);
            int vel = (int)(velBase + c.rng.range(-10.f, 8.f) + (strong ? 8.f : 0.f));
            u8 fl = (i > 0 && n.on - (m.notes[(size_t)i - 1].on + m.notes[(size_t)i - 1].du) < 0.02f) ? NF_SLIDE : 0;
            c.note(tr, b, bb, du * legato, pitch, vel, fl, 6.f);
            cur = pitch;
        }
    }
    // 8-bar phrase built from a 2-bar motif: A A' A B(cadence)
    void phrase8(const Motif& m, int bar, int startDeg, bool finalCadence) {
        play(m, bar, 0.f, 0, startDeg);
        play(m, bar + 2, 0.f, c.rng.chance(0.5f) ? 1 : 2, startDeg);
        play(m, bar + 4, 0.f, c.rng.chance(0.3f) ? 3 : 0, startDeg);
        play(m, bar + 6, 0.f, finalCadence ? 4 : 1, startDeg);
    }
    // Fill `bars` bars with phrases (handles 4, 8, 16...)
    void section(const Motif& m, int bar, int bars, int startDeg) {
        int b = 0;
        while (b + 8 <= bars) {
            phrase8(m, bar + b, startDeg, b + 8 >= bars);
            b += 8;
        }
        if (b + 4 <= bars) {
            play(m, bar + b, 0.f, 0, startDeg);
            play(m, bar + b + 2, 0.f, 4, startDeg);
            b += 4;
        }
    }
};

// Random-walk improvisation over the changes (solos): chord tones on beats, passing tones between.
static void improvise(Comp& c, int tr, int bar, int bars, int lo, int hi, MelStyle st, int velBase) {
    int pitch = (lo + hi) / 2;
    const KeySig& k = c.pl.key;
    for (int b = bar; b < bar + bars; b++) {
        Motif m = makeMotif(c.rng, st, 4.f);
        for (const MNote& n : m.notes) {
            const Chord& ch = c.chordAt(b, n.on);
            int dir = c.rng.chance(0.5f) ? 1 : -1;
            if (pitch > hi - 4) dir = -1;
            if (pitch < lo + 4) dir = 1;
            int step = c.rng.chance(0.7f) ? 1 : 2;
            int cand = pitch;
            for (int s = 0; s < step * 2 + 1; s++) {
                cand += dir;
                if (inScale(k, cand) || ch.hasPc(cand)) {
                    if (--step <= 0) break;
                }
            }
            bool strong = fabsf(n.on - floorf(n.on)) < 0.01f;
            if (strong && !ch.hasPc(cand)) cand = c.nearestChordTone(ch, cand);
            cand = Clamp(cand, lo, hi);
            pitch = cand;
            c.note(tr, b, n.on, n.du * 0.9f, pitch, velBase + c.rng.irange(-12, 10), 0, 7.f);
        }
    }
}

// Arpeggio of the current chord: pattern 0 up, 1 down, 2 up-down, 3 random. rate in beats.
static void arpeggiate(Comp& c, int tr, int bar, int bars, int lo, int span, float rate, int pattern, int vel, float gate) {
    int voicing[8];
    int prev[8];
    int prevN = 0;
    int idx = 0, dir = 1;
    for (int b = bar; b < bar + bars; b++) {
        for (float t = 0.f; t < 3.999f; t += rate) {
            const Chord& ch = c.chordAt(b, t);
            int vn = voiceChord(ch, lo, lo + span, Min(ch.n + 2, 6), voicing, prev, prevN);
            if (vn <= 0) continue;
            for (int i = 0; i < vn; i++) prev[i] = voicing[i];
            prevN = vn;
            int p;
            switch (pattern) {
                case 0: p = voicing[idx % vn]; idx++; break;
                case 1: p = voicing[vn - 1 - (idx % vn)]; idx++; break;
                case 2: {
                    if (idx >= vn - 1) dir = -1;
                    if (idx <= 0) dir = 1;
                    idx = Clamp(idx, 0, vn - 1);
                    p = voicing[idx];
                    idx += dir;
                    break;
                }
                default: p = voicing[c.rng.irange(0, vn - 1)]; break;
            }
            float accent = fmodf(t, 1.f) < 0.01f ? 10.f : 0.f;
            c.note(tr, b, t, rate * gate, p, vel + (int)accent + c.rng.irange(-6, 6), 0, 2.f);
        }
    }
}

// Sustained chord pads with voice leading; rhythm: hold chord for its duration (re-strike at changes).
static void padChords(Comp& c, int tr, int bar, int bars, int lo, int hi, int count, int vel) {
    int prev[8], prevN = 0;
    int b = bar;
    float t = 0.f;
    int endAbs = (bar + bars) * 4;
    while (b * 4 + (int)t < endAbs) {
        const Chord& ch = c.chordAt(b, t);
        // find chord length
        int absStart = b * 4 + (int)t;
        int absEnd = absStart + 1;
        while (absEnd < endAbs && !c.chordChangesAt(absEnd / 4, (float)(absEnd % 4))) absEnd++;
        int v[8];
        int n = voiceChord(ch, lo, hi, count, v, prev, prevN);
        for (int i = 0; i < n; i++) {
            c.note(tr, b, t, (float)(absEnd - absStart) - 0.05f, v[i], vel + c.rng.irange(-5, 5), 0, 3.f);
            prev[i] = v[i];
        }
        prevN = n;
        b = absEnd / 4;
        t = (float)(absEnd % 4);
    }
}

// Rhythmic chord stabs at given beat offsets within each bar.
static void stabChords(Comp& c, int tr, int bar, int bars, int lo, int hi, int count, const float* beats, int nb, float len, int vel) {
    int prev[8], prevN = 0;
    for (int b = bar; b < bar + bars; b++) {
        for (int k = 0; k < nb; k++) {
            const Chord& ch = c.chordAt(b, beats[k]);
            int v[8];
            int n = voiceChord(ch, lo, hi, count, v, prev, prevN);
            for (int i = 0; i < n; i++) {
                c.note(tr, b, beats[k], len, v[i], vel + c.rng.irange(-6, 6), 0, 3.f);
                prev[i] = v[i];
            }
            prevN = n;
        }
    }
}

// Guitar-style strum: notes of a voicing with small onset offsets (down/up strokes).
static void strum(Comp& c, int tr, int bar, float beat, float len, const int* v, int n, bool down, int vel, float spreadMs) {
    for (int i = 0; i < n; i++) {
        int idx = down ? i : n - 1 - i;
        float off = (float)i * spreadMs * 0.001f / (60.f / c.pl.bpm);
        c.note(tr, bar, beat + off, Max(0.1f, len - off), v[idx], vel - i * 2 + c.rng.irange(-4, 4), 0, 2.f);
    }
}

// Guitar voicing in standard tuning: bass note on the low E or A string, then one chord tone per
// higher string within a 5-fret hand position (produces realistic open/barre shapes).
static int guitarVoicing(const Chord& ch, int* out, int maxStrings = 6) {
    static const int kOpen[6] = {40, 45, 50, 55, 59, 64};
    int n = 0, start = -1, bassNote = -1;
    for (int s = 0; s < 2 && bassNote < 0; s++)
        for (int f = 0; f <= 7; f++)
            if ((kOpen[s] + f) % 12 == ch.bassPc()) {
                bassNote = kOpen[s] + f;
                start = s;
                break;
            }
    if (bassNote < 0) {
        bassNote = 40 + ((ch.bassPc() - 4) % 12 + 12) % 12;
        start = 0;
    }
    out[n++] = bassNote;
    int fretBase = Max(0, bassNote - kOpen[start] - 1);
    for (int s = start + 1; s < 6 && n < maxStrings; s++) {
        int best = -1;
        for (int f = fretBase; f <= fretBase + 4; f++) {
            int p = kOpen[s] + f;
            if (p > out[n - 1] && ch.hasPc(p)) { best = p; break; }
        }
        if (best < 0 && fretBase > 0 && ch.hasPc(kOpen[s]) && kOpen[s] > out[n - 1]) best = kOpen[s];
        if (best >= 0) out[n++] = best;
    }
    return n;
}
static int powerChord(const Chord& ch, int* out, int lowest = 40) {
    int r = lowest + ((ch.bassPc() - lowest) % 12 + 12) % 12;
    if (r > lowest + 7) r -= 12;
    if (r < lowest - 2) r += 12;
    out[0] = r;
    out[1] = r + 7;
    out[2] = r + 12;
    return 3;
}
static void tomFill(Comp& c, int tr, int bar, float fromBeat, float step, int vel) {
    static const int kP[3] = {DK_TOM_H, DK_TOM_M, DK_TOM_L};
    int total = Max(1, (int)((4.f - fromBeat) / step));
    int k = 0;
    for (float t = fromBeat; t < 3.999f; t += step, k++) c.hit(tr, bar, t, kP[Min(2, k * 3 / total)], vel + (int)(8.f * t / 4.f));
}
static void snareRoll(Comp& c, int tr, int bar, int bars, int piece, int v0, int v1) {
    for (int b = 0; b < bars; b++) {
        float rate = bars >= 4 ? (b < bars / 2 ? 0.5f : (b < bars - 1 ? 0.25f : 0.125f)) : (b < bars - 1 ? 0.25f : 0.125f);
        for (float t = 0.f; t < 3.999f; t += rate) {
            float u = ((float)b * 4.f + t) / ((float)bars * 4.f);
            c.hit(tr, bar + b, t, piece, (int)((float)v0 + (float)(v1 - v0) * u), 1.f);
        }
    }
}

// ---- Progression tables
typedef std::vector<PStep> Prog;
#define PROG(x) Prog(x, x + ARRAY_COUNT(x))
static const PStep kMinA[] = {{0, 0, 'a', 4}, {5, 0, 'a', 4}, {2, 0, 'a', 4}, {6, 0, 'a', 4}};
static const PStep kMinB[] = {{0, 0, 'a', 4}, {6, 0, 'a', 4}, {5, 0, 'a', 4}, {6, 0, 'a', 4}};
static const PStep kMinC[] = {{5, 0, 'a', 4}, {6, 0, 'a', 4}, {0, 0, 'a', 8}};
static const PStep kMinD[] = {{0, 0, 'a', 4}, {3, 0, 'a', 4}, {5, 0, 'a', 4}, {4, 0, 'M', 4}};
static const PStep kMinE[] = {{0, 0, 'a', 4}, {5, 0, 'a', 4}, {3, 0, 'a', 4}, {4, 0, 'M', 4}};
static const PStep kMinF[] = {{0, 0, 'a', 4}, {3, 0, 'a', 4}, {6, 0, 'a', 4}, {2, 0, 'a', 4}};
static const PStep kMinG[] = {{5, 0, 'a', 4}, {2, 0, 'a', 4}, {6, 0, 'a', 4}, {0, 0, 'a', 4}};
static const PStep kMinTrap1[] = {{0, 0, 'a', 8}, {5, 0, 'a', 8}};
static const PStep kMinTrap2[] = {{0, 0, 'a', 4}, {3, 0, 'a', 4}, {5, 0, 'a', 4}, {4, 0, 'M', 4}};
static const PStep kMinTrap3[] = {{0, 0, 'a', 4}, {0, 0, 'a', 4}, {5, 0, 'a', 4}, {6, 0, 'a', 4}};
static const PStep kMaj1[] = {{0, 0, 'a', 4}, {4, 0, 'a', 4}, {5, 0, 'a', 4}, {3, 0, 'a', 4}};
static const PStep kMaj2[] = {{5, 0, 'a', 4}, {3, 0, 'a', 4}, {0, 0, 'a', 4}, {4, 0, 'a', 4}};
static const PStep kMaj3[] = {{0, 0, 'a', 4}, {3, 0, 'a', 4}, {4, 0, 'a', 4}, {3, 0, 'a', 4}};
static const PStep kMaj4[] = {{0, 0, 'a', 4}, {6, -1, 'M', 4}, {3, 0, 'a', 4}, {0, 0, 'a', 4}};
static const PStep kMaj5[] = {{0, 0, 'a', 8}, {3, 0, 'a', 4}, {4, 0, 'a', 4}};
static const PStep kMaj6[] = {{0, 0, 'a', 4}, {3, 0, 'a', 4}, {0, 0, 'a', 4}, {4, 0, 'a', 4}};
static const PStep kMaj7[] = {{3, 0, 'a', 4}, {4, 0, 'a', 4}, {2, 0, 'a', 4}, {5, 0, 'a', 4}};
static const PStep kMaj8[] = {{0, 0, 'a', 4}, {5, 0, 'a', 4}, {3, 0, 'a', 4}, {4, 0, 'a', 4}};
static const PStep kJazzA[] = {{1, 0, 'n', 4}, {4, 0, '7', 4}, {0, 0, 'j', 8}};
static const PStep kJazzB[] = {{0, 0, 'j', 4}, {5, 0, 'n', 4}, {1, 0, 'n', 4}, {4, 0, '7', 4}};
static const PStep kJazzC[] = {{2, 0, 'n', 4}, {5, 0, '7', 4}, {1, 0, 'n', 4}, {4, 0, '7', 4}};
static const PStep kJazzD[] = {{3, 0, 'j', 4}, {3, 0, 'x', 4}, {2, 0, 'n', 4}, {5, 0, 'b', 4}, {1, 0, 'n', 4}, {4, 0, '9', 4}, {0, 0, 'j', 8}};
static const PStep kJazzE[] = {{0, 0, 'j', 2}, {5, 0, 'n', 2}, {1, 0, 'n', 2}, {4, 0, '7', 2}, {2, 0, 'n', 2}, {5, 0, '7', 2}, {1, 0, 'n', 2}, {4, 0, '7', 2}};
static const PStep kBossa[] = {{0, 0, 'j', 8}, {1, 1, '7', 8}, {1, 0, 'n', 4}, {1, -1, '7', 4}, {0, 0, '6', 8}};
static const PStep kLofiA[] = {{1, 0, 'N', 4}, {4, 0, '9', 4}, {0, 0, 'J', 4}, {5, 0, 'N', 4}};
static const PStep kLofiB[] = {{3, 0, 'J', 4}, {2, 0, 'n', 4}, {1, 0, 'N', 4}, {0, 0, 'J', 4}};
static const PStep kLofiC[] = {{0, 0, 'J', 4}, {3, 0, 'J', 4}, {2, 0, 'n', 4}, {5, 0, 'N', 4}};
static const PStep kLofiD[] = {{5, 0, 'N', 4}, {1, 0, 'N', 4}, {4, 0, '3', 4}, {0, 0, 'J', 4}};

static bool isMinorKey(const KeySig& k) { return k.scale != Scale::Major && k.scale != Scale::Mixolydian && k.scale != Scale::Lydian; }

static std::vector<Prog> popProgs(Rng& r, const KeySig& k, int count) {
    std::vector<Prog> pool;
    if (isMinorKey(k)) {
        pool = {PROG(kMinA), PROG(kMinB), PROG(kMinC), PROG(kMinD), PROG(kMinE), PROG(kMinF), PROG(kMinG)};
    } else {
        pool = {PROG(kMaj1), PROG(kMaj2), PROG(kMaj3), PROG(kMaj5), PROG(kMaj6), PROG(kMaj7), PROG(kMaj8)};
    }
    for (int i = (int)pool.size() - 1; i > 0; i--) std::swap(pool[(size_t)i], pool[(size_t)r.irange(0, i)]);
    std::vector<Prog> out;
    for (int i = 0; i < count; i++) out.push_back(pool[(size_t)(i % (int)pool.size())]);
    return out;
}

// ---- Shared arrangement helpers
static void finalHit(Comp& c, int tD, int bar, int tChord, int lo, int hi, int count, int vel) {
    if (tD >= 0) {
        c.hit(tD, bar, 0.f, DK_KICK, 120);
        c.hit(tD, bar, 0.f, DK_CRASH, 115);
    }
    if (tChord >= 0) {
        const Chord& ch = c.chordAt(bar, 0.f);
        int v[8], prev[1] = {0};
        int n = voiceChord(ch, lo, hi, count, v, prev, 0);
        for (int i = 0; i < n; i++) c.note(tChord, bar, 0.f, 3.5f, v[i], vel);
    }
}

// Tonic chord for endings
static void forceTonicEnding(Comp& c, int bar) {
    Chord t = buildChord(c.pl.key, PStep{0, 0, 'a', 4});
    for (int b = 0; b < 4; b++)
        if (bar * 4 + b < (int)c.beatChord.size()) c.beatChord[(size_t)(bar * 4 + b)] = t;
}

// =============================================================================================
// Synthwave / retrowave
static void composeSynthwave(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    std::vector<Prog> pp = popProgs(r, pl.key, 4);
    c.buildChords(pp);
    if (pl.mode != Mode::Bed) forceTonicEnding(c, pl.totalBars - 1);
    sd.kitStyle = KitStyle::Retro80s;
    sd.revSize = 1.4f; sd.revDecay = 2.4f; sd.revDamp = 0.35f;
    sd.dlyL = 0.75f; sd.dlyR = 1.f; sd.dlyFb = 0.38f;
    int tD = c.track(tdDrums(0.9f, 0.1f));
    TrackDef bass; bass.patch = pSawBass(); bass.gain = 0.55f; bass.hpf = 30.f; bass.rev = 0.02f;
    int tB = c.track(bass);
    TrackDef pad; pad.patch = pSupersaw(0.45f, 1.0f, 1900.f); pad.gain = 0.5f; pad.chorus = 0.45f; pad.rev = 0.35f; pad.hpf = 170.f;
    int tP = c.track(pad);
    TrackDef arp; arp.patch = pPluck(Wave::Pulse, 650.f, 0.2f); arp.gain = 0.3f; arp.pan = 0.2f; arp.dly = 0.3f; arp.rev = 0.2f; arp.hpf = 220.f;
    int tA = c.track(arp);
    TrackDef lead; lead.patch = pl.variant == 2 ? pVox() : (pl.variant == 1 ? pSquareLead() : pSawLead());
    lead.gain = pl.variant == 2 ? 0.55f : 0.42f; lead.dly = 0.25f; lead.rev = 0.3f; lead.hpf = 160.f;
    int tL = c.track(lead);
    TrackDef bell; bell.patch = pFmBell(); bell.gain = 0.32f; bell.pan = -0.25f; bell.rev = 0.4f; bell.dly = 0.3f;
    int tBell = c.track(bell);
    Motif mV = makeMotif(r, MelStyle::Pop, 8.f), mC = makeMotif(r, MelStyle::Pop, 8.f);
    int vDeg = r.irange(0, 4), cDeg = r.irange(4, 7);
    MelodyWriter mw(c, tL, 59, 81);
    MelodyWriter bw(c, tBell, 72, 96);
    bw.velBase = 80.f;
    int arpPat = r.irange(0, 2);
    auto drums = [&](int bar, int bars, float energy, bool fill, bool crash, bool hatsOnly) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            if (!hatsOnly) {
                c.hit(tD, b, 0.f, DK_KICK, 118);
                c.hit(tD, b, 2.f, DK_KICK, 112);
                if (energy > 0.8f && (b & 1)) c.hit(tD, b, 2.5f, DK_KICK, 96);
                c.hit(tD, b, 1.f, DK_SNARE, 112);
                if (!last) c.hit(tD, b, 3.f, DK_SNARE, 116);
            }
            float hr = energy > 0.9f ? 0.25f : 0.5f;
            for (float t = 0.f; t < (last ? 2.f : 4.f) - 0.01f; t += hr)
                c.hit(tD, b, t, DK_HAT_C, (fmodf(t, 1.f) < 0.01f ? 82 : 62) + r.irange(-6, 6));
            if (energy > 0.7f && !last && (b & 1)) c.hit(tD, b, 3.5f, DK_HAT_O, 70);
            if (last && !hatsOnly) tomFill(c, tD, b, 2.f, 0.25f, 100);
            if (crash && b == bar) c.hit(tD, b, 0.f, DK_CRASH, 108);
        }
    };
    auto bassline = [&](int bar, int bars, bool sixteen, int vel) {
        float step = sixteen ? 0.25f : 0.5f;
        for (int b = bar; b < bar + bars; b++)
            for (float t = 0.f; t < 3.999f; t += step) {
                const Chord& ch = c.chordAt(b, t);
                int root = c.bassPitch(ch, 33);
                int k = (int)(t / step);
                int p = root + ((sixteen ? (k % 4 == 2) : (k % 4 == 3)) ? 12 : 0);
                c.note(tB, b, t, step * 0.8f, p, vel + ((k % 2) == 0 ? 10 : -4) + r.irange(-4, 4), 0, 2.f);
            }
    };
    if (pl.mode == Mode::Jingle) {
        drums(0, 2, 1.f, true, true, false);
        bassline(0, 2, true, 100);
        padChords(c, tP, 0, 3, 55, 76, 4, 90);
        arpeggiate(c, tA, 0, 2, 64, 19, 0.25f, 0, 80, 0.6f);
        bw.play(makeMotif(r, MelStyle::Busy, 4.f), 0, 0.f, 0, cDeg);
        finalHit(c, tD, 2, tBell, 72, 88, 3, 100);
        return;
    }
    if (pl.mode == Mode::Bed) {
        padChords(c, tP, 0, pl.totalBars, 52, 74, 4, 60);
        arpeggiate(c, tA, 0, pl.totalBars, 60, 17, 0.5f, arpPat, 55, 0.5f);
        drums(0, pl.totalBars, 0.4f, false, false, true);
        return;
    }
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        switch (s.part) {
            case Part::Intro:
                padChords(c, tP, b0, nb, 52, 76, 4, 66);
                arpeggiate(c, tA, b0, nb, 60, 19, 0.5f, arpPat, 64, 0.6f);
                bw.section(mC, b0, nb, cDeg);
                if (nb >= 4) {
                    drums(b0 + nb / 2, nb - nb / 2, 0.4f, true, false, true);
                    bassline(b0 + nb / 2, nb - nb / 2, false, 88);
                }
                break;
            case Part::Verse:
                padChords(c, tP, b0, nb, 52, 74, 4, 64);
                arpeggiate(c, tA, b0, nb, 60, 19, 0.25f, arpPat, 58, 0.5f);
                drums(b0, nb, 0.65f, true, true, false);
                bassline(b0, nb, false, 96);
                mw.velBase = 90.f;
                mw.section(mV, b0, nb, vDeg);
                break;
            case Part::Chorus:
                padChords(c, tP, b0, nb, 55, 79, 5, 86);
                arpeggiate(c, tA, b0, nb, 64, 19, 0.25f, (arpPat + 1) % 3, 66, 0.55f);
                drums(b0, nb, 1.f, true, true, false);
                bassline(b0, nb, true, 100);
                mw.velBase = 104.f;
                mw.lo = 64; mw.hi = 86;
                mw.section(mC, b0, nb, cDeg);
                mw.lo = 59; mw.hi = 81;
                bw.play(mC, b0, 0.f, 0, cDeg + 7);
                break;
            case Part::Bridge:
            case Part::Breakdown:
                padChords(c, tP, b0, nb, 52, 76, 4, 72);
                bw.section(mV, b0, nb, vDeg + 2);
                drums(b0 + nb / 2, nb - nb / 2, 0.5f, true, false, true);
                improvise(c, tL, b0, nb, 62, 84, MelStyle::Sparse, 86);
                break;
            default:  // outro
                padChords(c, tP, b0, nb, 52, 76, 4, 70);
                arpeggiate(c, tA, b0, nb - 1, 60, 19, 0.5f, arpPat, 58, 0.6f);
                drums(b0, Max(1, nb - 2), 0.6f, false, false, false);
                bassline(b0, Max(1, nb - 1), false, 88);
                bw.play(mC, b0, 0.f, 4, cDeg);
                finalHit(c, tD, b0 + nb - 1, -1, 0, 0, 0, 0);
                break;
        }
    }
    // intro filter sweep
    sd.autom.push_back(Automation{0, 0.f, 900.f});
    sd.autom.push_back(Automation{(u32)((float)pl.sections[0].bars * 4.f * c.spb), 0.f, 20000.f});
}

// =============================================================================================
// Hip-hop: trap (808s, hat rolls, bells) and boom-bap (swung, dusty, keys)
static void composeHipHop(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    bool trap = pl.variant == 0;
    std::vector<Prog> pp;
    if (trap) {
        Prog opts[3] = {PROG(kMinTrap1), PROG(kMinTrap2), PROG(kMinTrap3)};
        int a = r.irange(0, 2);
        pp = {opts[a], opts[(a + 1) % 3]};
    } else {
        pp = popProgs(r, pl.key, 2);
    }
    c.buildChords(pp);
    sd.kitStyle = trap ? KitStyle::Trap : KitStyle::BoomBap;
    sd.revSize = trap ? 1.1f : 0.8f; sd.revDecay = trap ? 1.6f : 1.0f; sd.revDamp = 0.5f;
    sd.dlyL = 0.75f; sd.dlyR = 0.5f; sd.dlyFb = 0.3f;
    if (!trap) sd.vinyl = 0.6f;
    int tD = c.track(tdDrums(0.95f, 0.06f));
    int tB;
    if (trap) {
        TrackDef b; b.patch = p808(); b.gain = 0.75f; b.rev = 0.f; b.hpf = 25.f;
        tB = c.track(b);
    } else {
        TrackDef b; b.patch = pString(1.4f, 0.25f, 0.3f, 1400.f); b.patch.poly = 1; b.gain = 0.9f; b.lsF = 90.f; b.lsDb = 4.f; b.lpf = 2200.f; b.rev = 0.03f;
        tB = c.track(b);
    }
    TrackDef mel;
    if (trap) { mel.patch = r.chance(0.6f) ? pFmBell() : pPluck(Wave::Saw, 1200.f, 0.35f); mel.gain = 0.38f; mel.rev = 0.3f; mel.dly = 0.2f; mel.hpf = 250.f; }
    else { mel.patch = r.chance(0.5f) ? pPiano() : pFmEP(); mel.gain = 0.5f; mel.lpf = 5000.f; mel.rev = 0.2f; mel.hpf = 120.f; }
    int tM = c.track(mel);
    TrackDef pad; pad.patch = r.chance(0.5f) ? pChoir() : pStrings(); pad.gain = 0.45f; pad.rev = 0.4f; pad.hpf = 200.f;
    int tP = c.track(pad);
    TrackDef lead; lead.patch = pSoftLead(trap ? Wave::Sine : Wave::Triangle); lead.gain = 0.35f; lead.rev = 0.3f; lead.dly = 0.25f;
    int tL = c.track(lead);
    TrackDef perc = tdDrums(0.5f, 0.1f);
    perc.panSpread = 0.4f;
    int tPc = c.track(perc);
    Motif loop = makeMotif(r, trap ? MelStyle::Busy : MelStyle::Pop, 8.f);
    Motif hook = makeMotif(r, MelStyle::Sparse, 8.f);
    MelodyWriter mw(c, tM, trap ? 70 : 60, trap ? 91 : 79);
    mw.velBase = 88.f;
    MelodyWriter lw(c, tL, 67, 86);
    int loopDeg = r.irange(2, 7);
    // kick patterns (16 steps)
    static const u32 kKickPat[] = {0x0421u, 0x0489u, 0x4421u, 0x0911u, 0x2421u, 0x0425u};
    u32 kick = kKickPat[r.irange(0, (int)ARRAY_COUNT(kKickPat) - 1)];
    auto trapDrums = [&](int bar, int bars, float energy, bool fill) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            for (int st = 0; st < 16; st++)
                if (kick & (1u << st)) c.hit(tD, b, (float)st * 0.25f, DK_KICK, 115, 1.f);
            if (energy > 0.4f) {
                c.hit(tD, b, 2.f, DK_SNARE, 112, 1.f);
                c.hit(tD, b, 2.f, DK_CLAP, 100, 1.f);
            }
            // hats with rolls
            for (int beat = 0; beat < 4; beat++) {
                float rr = r.f();
                int div = rr < 0.55f ? 2 : rr < 0.8f ? 4 : rr < 0.93f ? 6 : 8;
                if (last && beat >= 2) div = 8;
                if (energy < 0.5f && div > 4) div = 2;
                for (int k = 0; k < div; k++) {
                    int v = 70 + (k == 0 ? 14 : 0) + (div >= 6 ? (k * 20) / div : 0) + r.irange(-6, 6);
                    c.hit(tD, b, (float)beat + (float)k / (float)div, DK_HAT_C, v, 1.f);
                }
            }
            if (energy > 0.8f && (b % 2 == 1)) c.hit(tD, b, 3.5f, DK_HAT_O, 72);
            if (energy > 0.6f && r.chance(0.5f)) c.hit(tPc, b, (float)r.irange(0, 15) * 0.25f, r.chance(0.5f) ? DK_RIM : DK_SNAP, 70);
            if (last && energy > 0.5f) c.hit(tD, b, 3.75f, DK_SNARE, 90, 1.f);
        }
    };
    auto boomDrums = [&](int bar, int bars, float energy, bool fill) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            c.hit(tD, b, 0.f, DK_KICK, 118);
            c.hit(tD, b, 2.5f, DK_KICK, 104);
            if (b & 1) c.hit(tD, b, 1.75f, DK_KICK, 86);
            c.hit(tD, b, 1.f, DK_SNARE, 110);
            c.hit(tD, b, 3.f, DK_SNARE, 114);
            for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tD, b, t, DK_HAT_C, (fmodf(t, 1.f) < 0.01f ? 80 : 58) + r.irange(-8, 8), 8.f);
            if (energy > 0.8f) c.hit(tPc, b, 1.5f, DK_SHAKER, 60);
            if (last) c.hit(tD, b, 3.5f, DK_SNARE, 80);
        }
    };
    auto bass808 = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            int steps[16], ns = 0;
            for (int st = 0; st < 16; st++)
                if (kick & (1u << st)) steps[ns++] = st;
            for (int i = 0; i < ns; i++) {
                int st = steps[i];
                int nextSt = i + 1 < ns ? steps[i + 1] : 16;
                const Chord& ch = c.chordAt(b, (float)st * 0.25f);
                int root = c.bassPitch(ch, 29);
                int p = root;
                if (i > 0 && r.chance(0.2f)) p += 12;
                bool slide = i > 0 && r.chance(0.25f);
                c.note(tB, b, (float)st * 0.25f, (float)(nextSt - st) * 0.25f * 0.95f, p, 110, slide ? NF_SLIDE : 0, 1.f);
            }
        }
    };
    auto boomBass = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            const Chord& ch = c.chordAt(b, 0.f);
            int root = c.bassPitch(ch, 33);
            c.note(tB, b, 0.f, 1.2f, root, 105);
            c.note(tB, b, 1.5f, 0.4f, root + (r.chance(0.5f) ? 7 : 12), 85);
            const Chord& ch2 = c.chordAt(b, 2.f);
            int r2 = c.bassPitch(ch2, 33);
            c.note(tB, b, 2.5f, 1.f, r2, 98);
            if (r.chance(0.5f)) c.note(tB, b, 3.5f, 0.4f, r2 + (r.chance(0.5f) ? 3 : 5), 80);
        }
    };
    auto keys = [&](int bar, int bars, int vel) {
        static const float kB[] = {0.f, 1.5f, 2.75f};
        stabChords(c, tM, bar, bars, 55, 74, 4, kB, 3, 0.9f, vel);
    };
    if (pl.mode == Mode::Jingle) {
        if (trap) { trapDrums(0, 2, 1.f, true); bass808(0, 2); }
        else { boomDrums(0, 2, 1.f, true); boomBass(0, 2); }
        mw.play(makeMotif(r, MelStyle::Busy, 4.f), 0, 0.f, 0, loopDeg);
        mw.play(makeMotif(r, MelStyle::Busy, 4.f), 1, 0.f, 4, loopDeg);
        finalHit(c, tD, 2, tP, 55, 72, 3, 90);
        c.note(tB, 2, 0.f, 3.f, c.bassPitch(c.chordAt(2, 0.f), 29), 115);
        return;
    }
    if (pl.mode == Mode::Bed) {
        padChords(c, tP, 0, pl.totalBars, 55, 72, 3, 55);
        for (int b = 0; b < pl.totalBars; b += 2) mw.play(loop, b, 0.f, 0, loopDeg);
        for (int b = 0; b < pl.totalBars; b++)
            for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tD, b, t, DK_HAT_C, 55 + r.irange(-5, 5));
        return;
    }
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        bool isHook = s.part == Part::Chorus;
        if (s.part == Part::Intro) {
            for (int b = b0; b < b0 + nb; b += 2) mw.play(loop, b, 0.f, 0, loopDeg);
            padChords(c, tP, b0, nb, 55, 72, 3, 60);
            if (!trap) keys(b0 + nb / 2, nb - nb / 2, 70);
            continue;
        }
        if (s.part == Part::Outro) {
            for (int b = b0; b < b0 + nb; b += 2) mw.play(loop, b, 0.f, b + 2 >= b0 + nb ? 4 : 0, loopDeg);
            padChords(c, tP, b0, nb, 55, 72, 3, 58);
            if (trap) trapDrums(b0, Max(1, nb - 2), 0.5f, false);
            else boomDrums(b0, Max(1, nb - 2), 0.5f, false);
            continue;
        }
        float energy = isHook ? 1.f : s.part == Part::Bridge ? 0.45f : 0.7f;
        if (trap) {
            trapDrums(b0, nb, energy, true);
            if (s.part != Part::Bridge) bass808(b0, nb);
        } else {
            boomDrums(b0, nb, energy, true);
            boomBass(b0, nb);
            keys(b0, nb, isHook ? 88 : 74);
        }
        for (int b = b0; b < b0 + nb; b += 2) mw.play(loop, b, 0.f, (isHook && (b / 2) % 2 == 1) ? 1 : 0, loopDeg);
        if (isHook) {
            padChords(c, tP, b0, nb, 55, 74, 4, 72);
            lw.section(hook, b0, nb, loopDeg + 2);
            c.hit(tD, b0, 0.f, DK_CRASH, 100);
        } else if (s.part == Part::Bridge) {
            padChords(c, tP, b0, nb, 55, 74, 4, 70);
        }
    }
    sd.autom.push_back(Automation{0, 0.f, 1100.f});
    sd.autom.push_back(Automation{(u32)((float)pl.sections[0].bars * 4.f * c.spb) - 1, 0.f, 2500.f});
    sd.autom.push_back(Automation{(u32)((float)pl.sections[0].bars * 4.f * c.spb), 0.f, 20000.f});
}

// =============================================================================================
// Reggaeton / latin pop (dembow)
static void composeReggaeton(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    std::vector<Prog> pp = popProgs(r, pl.key, 3);
    c.buildChords(pp);
    if (pl.mode == Mode::Song) forceTonicEnding(c, pl.totalBars - 1);
    sd.kitStyle = KitStyle::Dembow;
    sd.revSize = 1.0f; sd.revDecay = 1.5f; sd.revDamp = 0.45f;
    sd.dlyL = 0.75f; sd.dlyR = 0.5f; sd.dlyFb = 0.3f;
    int tD = c.track(tdDrums(0.9f, 0.08f));
    TrackDef bass; bass.patch = pSubBass(); bass.gain = 0.7f; bass.rev = 0.f; bass.hpf = 28.f;
    int tB = c.track(bass);
    TrackDef keys;
    bool guitar = pl.variant == 1;
    if (guitar) { keys.patch = pString(2.2f, 0.55f, 0.18f, 5000.f); keys.gain = 0.55f; keys.chorus = 0.2f; keys.rev = 0.2f; keys.hpf = 150.f; keys.panSpread = 0.2f; }
    else { keys.patch = pMallet(3.93f, 0.6f); keys.gain = 0.42f; keys.rev = 0.2f; keys.hpf = 200.f; }
    int tK = c.track(keys);
    TrackDef pluck; pluck.patch = pPluck(Wave::Square, 1400.f, 0.25f); pluck.gain = 0.34f; pluck.dly = 0.25f; pluck.rev = 0.2f; pluck.pan = 0.2f;
    int tPl = c.track(pluck);
    TrackDef pad; pad.patch = pSupersaw(0.3f, 0.6f, 1600.f); pad.gain = 0.34f; pad.rev = 0.3f; pad.hpf = 250.f; pad.chorus = 0.3f;
    int tP = c.track(pad);
    TrackDef vox; vox.patch = pVox(); vox.gain = 0.6f; vox.rev = 0.25f; vox.dly = 0.18f; vox.hpf = 180.f;
    int tV = c.track(vox);
    TrackDef perc = tdDrums(0.55f, 0.12f);
    perc.panSpread = 0.3f;
    int tPc = c.track(perc);
    Motif mV = makeMotif(r, MelStyle::Latin, 8.f), mC = makeMotif(r, MelStyle::Latin, 8.f), mHook = makeMotif(r, MelStyle::Latin, 4.f);
    int vDeg = r.irange(0, 4), cDeg = r.irange(3, 7);
    MelodyWriter vw(c, tV, 60, 79);
    vw.velBase = 95.f;
    MelodyWriter pw(c, tPl, 67, 88);
    auto dembow = [&](int bar, int bars, float energy, bool fill) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            for (int k = 0; k < 4; k++) c.hit(tD, b, (float)k, DK_KICK, k % 2 == 0 ? 118 : 108, 2.f);
            if (!last) {
                c.hit(tD, b, 0.75f, DK_SNARE, 100, 2.f);
                c.hit(tD, b, 1.5f, DK_SNARE, 104, 2.f);
                c.hit(tD, b, 2.75f, DK_SNARE, 100, 2.f);
                c.hit(tD, b, 3.5f, DK_SNARE, 106, 2.f);
            } else {
                for (float t = 0.f; t < 3.99f; t += 0.25f) c.hit(tPc, b, t, t < 2.f ? DK_TIMBALE_H : DK_TIMBALE_L, 80 + (int)(t * 8.f));
            }
            if (energy > 0.5f)
                for (float t = 0.f; t < 3.99f; t += 0.25f) c.hit(tPc, b, t, DK_SHAKER, (fmodf(t, 0.5f) < 0.01f ? 62 : 48) + r.irange(-5, 5));
            if (energy > 0.8f)
                for (float t = 0.5f; t < 3.99f; t += 1.f) c.hit(tD, b, t, DK_HAT_C, 70);
            if (energy > 0.6f && energy < 0.9f) {
                c.hit(tPc, b, 1.f, DK_CONGA_H, 70);
                c.hit(tPc, b, 2.5f, DK_CONGA_L, 76);
                c.hit(tPc, b, 3.f, DK_CONGA_H, 64);
            }
        }
    };
    auto bassLine = [&](int bar, int bars) {
        static const float kOn[] = {0.f, 1.5f, 2.f, 3.5f};
        static const float kLen[] = {1.2f, 0.4f, 1.2f, 0.4f};
        for (int b = bar; b < bar + bars; b++)
            for (int k = 0; k < 4; k++) {
                const Chord& ch = c.chordAt(b, kOn[k]);
                int root = c.bassPitch(ch, 31);
                c.note(tB, b, kOn[k], kLen[k], root + (k == 3 && r.chance(0.3f) ? 7 : 0), 112, 0, 1.f);
            }
    };
    auto tresillo = [&](int bar, int bars, int vel) {
        static const float kT[] = {0.f, 0.75f, 1.5f, 2.f, 2.75f, 3.5f};
        int prev[8], pn = 0;
        for (int b = bar; b < bar + bars; b++)
            for (float t : kT) {
                const Chord& ch = c.chordAt(b, t);
                int v[8];
                if (guitar) {
                    int n = guitarVoicing(ch, v, 5);
                    int start = Max(0, n - 4);
                    strum(c, tK, b, t, 0.5f, v + start, n - start, ((int)(t * 4.f) % 2) == 0, vel, 9.f);
                } else {
                    int n = voiceChord(ch, 60, 79, 3, v, prev, pn);
                    for (int i = 0; i < n; i++) {
                        c.note(tK, b, t, 0.4f, v[i], vel + r.irange(-6, 6));
                        prev[i] = v[i];
                    }
                    pn = n;
                }
            }
    };
    if (pl.mode == Mode::Jingle) {
        dembow(0, 2, 1.f, false);
        bassLine(0, 2);
        tresillo(0, 2, 88);
        pw.play(mHook, 0, 0.f, 0, cDeg);
        pw.play(mHook, 1, 0.f, 4, cDeg);
        finalHit(c, tD, 2, tP, 55, 76, 4, 90);
        return;
    }
    if (pl.mode == Mode::Bed) {
        tresillo(0, pl.totalBars, 62);
        padChords(c, tP, 0, pl.totalBars, 55, 74, 3, 50);
        for (int b = 0; b < pl.totalBars; b++)
            for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tPc, b, t, DK_SHAKER, 50);
        return;
    }
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        switch (s.part) {
            case Part::Intro:
                tresillo(b0, nb, 76);
                for (int b = b0; b < b0 + nb; b++) pw.play(mHook, b, 0.f, (b - b0) % 4 == 3 ? 4 : 0, cDeg);
                if (nb >= 4) dembow(b0 + nb / 2, nb - nb / 2, 0.5f, true);
                break;
            case Part::Verse:
                dembow(b0, nb, 0.7f, true);
                bassLine(b0, nb);
                tresillo(b0, nb, 70);
                vw.section(mV, b0, nb, vDeg);
                break;
            case Part::Pre:
                dembow(b0, nb, 0.6f, true);
                bassLine(b0, nb);
                padChords(c, tP, b0, nb, 55, 76, 4, 70);
                vw.section(mV, b0, nb, vDeg + 2);
                for (int b = b0; b < b0 + nb; b++) c.hit(tPc, b, 0.f, DK_GUIRO, 70);
                break;
            case Part::Chorus:
                dembow(b0, nb, 1.f, true);
                bassLine(b0, nb);
                tresillo(b0, nb, 82);
                padChords(c, tP, b0, nb, 55, 79, 4, 78);
                vw.velBase = 104.f;
                vw.section(mC, b0, nb, cDeg);
                vw.velBase = 95.f;
                for (int b = b0; b < b0 + nb; b += 4) pw.play(mHook, b + 3, 0.f, 0, cDeg + 7);
                c.hit(tD, b0, 0.f, DK_CRASH, 100);
                break;
            case Part::Bridge:
                tresillo(b0, nb, 72);
                padChords(c, tP, b0, nb, 55, 76, 4, 66);
                for (int b = b0; b < b0 + nb; b++)
                    for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tPc, b, t, DK_SHAKER, 55);
                improvise(c, tPl, b0, nb, 67, 86, MelStyle::Latin, 80);
                break;
            default:
                tresillo(b0, Max(1, nb - 1), 70);
                dembow(b0, Max(1, nb - 2), 0.6f, false);
                bassLine(b0, Max(1, nb - 1));
                finalHit(c, tD, b0 + nb - 1, tP, 55, 76, 4, 80);
                break;
        }
    }
}

// =============================================================================================
// House / EDM
static void composeHouse(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    std::vector<Prog> pp = popProgs(r, pl.key, 3);
    c.buildChords(pp);
    sd.kitStyle = KitStyle::House;
    sd.revSize = 1.2f; sd.revDecay = 1.9f; sd.revDamp = 0.4f;
    sd.dlyL = 0.75f; sd.dlyR = 0.75f; sd.dlyFb = 0.35f;
    bool bigRoom = pl.variant == 1;
    int tD = c.track(tdDrums(0.95f, 0.06f));
    TrackDef bass; bass.patch = bigRoom ? pSawBass() : pFmBass(); bass.gain = bigRoom ? 0.5f : 0.6f; bass.sidechain = 0.7f; bass.hpf = 32.f; bass.rev = 0.f;
    int tB = c.track(bass);
    TrackDef stab;
    if (bigRoom) { stab.patch = pSupersaw(0.005f, 0.2f, 3500.f); stab.gain = 0.5f; }
    else { stab.patch = r.chance(0.5f) ? pPiano() : pOrgan(); stab.gain = 0.5f; }
    stab.sidechain = 0.6f; stab.rev = 0.2f; stab.hpf = 180.f; stab.chorus = bigRoom ? 0.2f : 0.f;
    int tS = c.track(stab);
    TrackDef pad; pad.patch = r.chance(0.5f) ? pChoir() : pStrings(); pad.gain = 0.5f; pad.rev = 0.45f; pad.sidechain = 0.4f; pad.hpf = 200.f;
    int tP = c.track(pad);
    TrackDef lead; lead.patch = bigRoom ? pSupersaw(0.005f, 0.2f, 5000.f) : pPluck(Wave::Saw, 1600.f, 0.25f);
    if (bigRoom) { lead.patch.mono = false; lead.patch.poly = 4; }
    lead.gain = bigRoom ? 0.45f : 0.36f; lead.dly = 0.25f; lead.rev = 0.25f; lead.sidechain = 0.35f; lead.hpf = 200.f;
    int tL = c.track(lead);
    TrackDef vox; vox.patch = pVox(); vox.patch.glide = 0.03f; vox.gain = 0.5f; vox.rev = 0.35f; vox.dly = 0.3f; vox.hpf = 200.f;
    int tV = c.track(vox);
    TrackDef fx = tdDrums(0.6f, 0.3f);
    int tFx = c.track(fx);
    Motif mHook = makeMotif(r, MelStyle::Busy, 8.f), mVox = makeMotif(r, MelStyle::Sparse, 8.f);
    int hDeg = r.irange(4, 9);
    MelodyWriter lw(c, tL, 64, 88);
    lw.velBase = 100.f;
    MelodyWriter vw(c, tV, 62, 81);
    static const float kHouseStab[] = {0.f, 0.75f, 1.5f, 2.5f, 3.25f};
    auto four = [&](int bar, int bars, float energy, bool kick) {
        for (int b = bar; b < bar + bars; b++) {
            for (int k = 0; k < 4; k++) {
                if (kick) c.hit(tD, b, (float)k, DK_KICK, 120, 0.5f);
                if (energy > 0.3f) c.hit(tD, b, (float)k + 0.5f, DK_HAT_O, 78 + r.irange(-5, 5), 1.f);
            }
            if (energy > 0.5f) {
                c.hit(tD, b, 1.f, DK_CLAP, 108, 1.f);
                c.hit(tD, b, 3.f, DK_CLAP, 110, 1.f);
            }
            if (energy > 0.7f)
                for (float t = 0.f; t < 3.99f; t += 0.25f)
                    if (fmodf(t, 0.5f) > 0.01f) c.hit(tD, b, t, DK_HAT_C, 55 + r.irange(-6, 6), 1.f);
            if (energy > 0.9f)
                for (float t = 0.f; t < 3.99f; t += 1.f) c.hit(tD, b, t, DK_RIDE, 64, 1.f);
            if (energy > 0.6f) c.hit(tFx, b, 0.25f, DK_SHAKER, 50);
        }
    };
    auto offbeatBass = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++)
            for (int k = 0; k < 4; k++) {
                const Chord& ch = c.chordAt(b, (float)k);
                int root = c.bassPitch(ch, 33);
                int p = root + ((k == 3 && (b & 1)) ? 12 : 0);
                c.note(tB, b, (float)k + 0.5f, 0.42f, p, 108, 0, 1.f);
                if (bigRoom) c.note(tB, b, (float)k + 0.75f, 0.2f, p, 90, 0, 1.f);
            }
    };
    auto chords = [&](int bar, int bars, int vel) {
        if (bigRoom) padChords(c, tS, bar, bars, 55, 79, 5, vel);
        else stabChords(c, tS, bar, bars, 57, 76, 4, kHouseStab, 5, 0.35f, vel);
    };
    if (pl.mode == Mode::Jingle) {
        four(0, 2, 1.f, true);
        offbeatBass(0, 2);
        chords(0, 2, 96);
        lw.play(makeMotif(r, MelStyle::Busy, 4.f), 0, 0.f, 0, hDeg);
        lw.play(makeMotif(r, MelStyle::Busy, 4.f), 1, 0.f, 4, hDeg);
        finalHit(c, tD, 2, tP, 55, 76, 4, 90);
        c.hit(tFx, 2, 0.f, DK_IMPACT, 110);
        return;
    }
    if (pl.mode == Mode::Bed) {
        four(0, pl.totalBars, 0.35f, false);
        padChords(c, tP, 0, pl.totalBars, 55, 76, 4, 58);
        return;
    }
    u32 barS = (u32)(4.f * c.spb);
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        u32 t0 = (u32)b0 * barS, t1 = (u32)(b0 + nb) * barS;
        switch (s.part) {
            case Part::Intro:
                four(b0, nb, nb > 8 ? 0.45f : 0.35f, true);
                if (nb >= 8) offbeatBass(b0 + nb / 2, nb - nb / 2);
                sd.autom.push_back(Automation{t0, 0.f, 1200.f});
                sd.autom.push_back(Automation{t1 - 1, 0.f, 6000.f});
                sd.autom.push_back(Automation{t1, 0.f, 20000.f});
                break;
            case Part::Build:
                four(b0, nb, 0.5f, false);
                chords(b0, nb, 80);
                snareRoll(c, tD, b0, nb, DK_SNARE, 50, 118);
                c.hit(tFx, b0 + nb - 2, 0.f, DK_RISER, 100);
                vw.section(mVox, b0, nb, hDeg - 2);
                sd.autom.push_back(Automation{t0, 30.f, 20000.f});
                sd.autom.push_back(Automation{t1 - 1, 900.f, 20000.f});
                sd.autom.push_back(Automation{t1, 0.f, 20000.f});
                break;
            case Part::Drop:
                four(b0, nb, 1.f, true);
                offbeatBass(b0, nb);
                chords(b0, nb, 100);
                lw.section(mHook, b0, nb, hDeg);
                c.hit(tFx, b0, 0.f, DK_IMPACT, 115);
                c.hit(tD, b0, 0.f, DK_CRASH, 110);
                for (int b = b0 + 8; b < b0 + nb; b += 8) c.hit(tD, b, 0.f, DK_CRASH, 96);
                break;
            case Part::Breakdown:
                padChords(c, tP, b0, nb, 55, 79, 4, 76);
                if (!bigRoom) chords(b0 + nb / 2, nb - nb / 2, 70);
                vw.section(mVox, b0, nb, hDeg - 2);
                for (int b = b0 + nb / 2; b < b0 + nb; b++)
                    for (int k = 0; k < 4; k++) c.hit(tD, b, (float)k + 0.5f, DK_HAT_O, 50);
                sd.autom.push_back(Automation{t0, 0.f, 2500.f});
                sd.autom.push_back(Automation{t1 - 1, 0.f, 12000.f});
                sd.autom.push_back(Automation{t1, 0.f, 20000.f});
                break;
            default:
                four(b0, nb, 0.6f, true);
                offbeatBass(b0, nb / 2);
                sd.autom.push_back(Automation{t0 + (t1 - t0) / 2, 0.f, 20000.f});
                sd.autom.push_back(Automation{t1, 0.f, 900.f});
                break;
        }
    }
    std::sort(sd.autom.begin(), sd.autom.end(), [](const Automation& a, const Automation& b) { return a.at < b.at; });
}

// =============================================================================================
// Rock
static void composeRock(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    std::vector<Prog> pp;
    if (isMinorKey(pl.key)) pp = popProgs(r, pl.key, 3);
    else {
        Prog opts[5] = {PROG(kMaj1), PROG(kMaj4), PROG(kMaj3), PROG(kMaj6), PROG(kMaj2)};
        int a = r.irange(0, 4);
        pp = {opts[a], opts[(a + 2) % 5], opts[(a + 1) % 5]};
    }
    c.buildChords(pp);
    if (pl.mode != Mode::Bed) forceTonicEnding(c, pl.totalBars - 1);
    sd.kitStyle = KitStyle::Rock;
    sd.revSize = 1.0f; sd.revDecay = 1.3f; sd.revDamp = 0.5f;
    sd.dlyL = 0.75f; sd.dlyR = 0.5f; sd.dlyFb = 0.25f;
    int tD = c.track(tdDrums(0.95f, 0.1f));
    TrackDef bass; bass.patch = pString(3.f, 0.35f, 0.22f, 2600.f); bass.patch.poly = 2; bass.gain = 0.95f; bass.drive = 0.12f; bass.lpf = 3200.f; bass.lsF = 100.f; bass.lsDb = 3.f; bass.rev = 0.f;
    int tB = c.track(bass);
    TrackDef g1; g1.patch = pString(5.f, 0.7f, 0.18f, 7000.f); g1.gain = 0.5f; g1.drive = 0.75f; g1.cab = true; g1.pan = -0.75f; g1.rev = 0.08f; g1.hpf = 90.f;
    TrackDef g2 = g1; g2.pan = 0.75f; g2.patch.kBright = 0.65f;
    int tG1 = c.track(g1), tG2 = c.track(g2);
    TrackDef lg; lg.patch = pString(9.f, 0.8f, 0.14f, 8000.f); lg.patch.vibDepth = 0.35f; lg.patch.vibDelay = 0.18f; lg.patch.vibRate = 5.5f; lg.patch.poly = 2;
    lg.gain = 0.5f; lg.drive = 0.9f; lg.cab = true; lg.dly = 0.22f; lg.rev = 0.2f; lg.hpf = 150.f; lg.pan = 0.1f;
    int tLG = c.track(lg);
    TrackDef cl; cl.patch = pString(3.f, 0.6f, 0.2f, 6000.f); cl.gain = 0.4f; cl.chorus = 0.35f; cl.rev = 0.25f; cl.pan = -0.3f; cl.hpf = 120.f;
    int tCl = c.track(cl);
    TrackDef org; org.patch = pOrgan(); org.gain = 0.28f; org.chorus = 0.5f; org.rev = 0.2f; org.pan = 0.3f; org.hpf = 150.f;
    int tO = c.track(org);
    Motif riff = makeMotif(r, MelStyle::Busy, 4.f), mV = makeMotif(r, MelStyle::Pop, 8.f), mC = makeMotif(r, MelStyle::Pop, 8.f);
    int rDeg = r.irange(0, 4), vDeg = r.irange(0, 4), cDeg = r.irange(4, 7);
    MelodyWriter lw(c, tLG, 59, 86);
    lw.velBase = 100.f;
    auto drums = [&](int bar, int bars, float energy, bool fill, bool crash) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            c.hit(tD, b, 0.f, DK_KICK, 120);
            c.hit(tD, b, 2.f, DK_KICK, 112);
            if (energy > 0.7f) c.hit(tD, b, 2.5f, DK_KICK, 100);
            if ((b & 1) && energy > 0.5f) c.hit(tD, b, 1.5f, DK_KICK, 92);
            c.hit(tD, b, 1.f, DK_SNARE, 118);
            if (!last) c.hit(tD, b, 3.f, DK_SNARE, 120);
            int cym = energy > 0.9f ? DK_RIDE : DK_HAT_C;
            for (float t = 0.f; t < (last ? 2.f : 4.f) - 0.01f; t += 0.5f)
                c.hit(tD, b, t, (energy > 0.9f && fmodf(t, 1.f) > 0.01f) ? DK_HAT_O : cym, (fmodf(t, 1.f) < 0.01f ? 90 : 70) + r.irange(-6, 6));
            if (last) tomFill(c, tD, b, 2.f, 0.25f, 104);
            if ((crash && b == bar) || (energy > 0.9f && (b - bar) % 4 == 0)) c.hit(tD, b, 0.f, DK_CRASH, 112);
        }
    };
    auto bassline = [&](int bar, int bars, int vel) {
        for (int b = bar; b < bar + bars; b++)
            for (float t = 0.f; t < 3.99f; t += 0.5f) {
                const Chord& ch = c.chordAt(b, t);
                int root = c.bassPitch(ch, 28);
                int p = root;
                if (t >= 3.49f && c.chordChangesAt(b + 1, 0.f)) {
                    int nr = c.bassPitch(c.chordAt(b + 1, 0.f), 28);
                    p = nr + (nr > root ? -1 : 1) * (r.chance(0.5f) ? 2 : 1);
                }
                c.note(tB, b, t, 0.45f, p, vel + r.irange(-6, 6), 0, 3.f);
            }
    };
    auto power = [&](int tr, int bar, int bars, bool palm, bool sustained, int vel, float human) {
        for (int b = bar; b < bar + bars; b++) {
            if (sustained) {
                for (float t = 0.f; t < 3.99f; t += 2.f) {
                    int v[3];
                    int n = powerChord(c.chordAt(b, t), v, 40);
                    strum(c, tr, b, t, 1.95f, v, n, true, vel, 6.f + human);
                }
            } else {
                for (float t = 0.f; t < 3.99f; t += 0.5f) {
                    int v[3];
                    int n = powerChord(c.chordAt(b, t), v, 40);
                    strum(c, tr, b, t, palm ? 0.3f : 0.48f, v, palm ? 2 : n, true, vel - (palm ? 25 : 0) + (fmodf(t, 1.f) < 0.01f ? 8 : 0), 4.f + human);
                }
            }
        }
    };
    auto cleanArp = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            int v[6];
            int n = guitarVoicing(c.chordAt(b, 0.f), v, 6);
            for (int k = 0; k < 8; k++) {
                int idx = (k < n) ? k : (2 * n - 2 - k);
                idx = Clamp(idx, 0, n - 1);
                c.note(tCl, b, (float)k * 0.5f, 1.2f, v[idx], 78 + r.irange(-8, 8));
            }
        }
    };
    if (pl.mode == Mode::Jingle) {
        drums(0, 2, 1.f, true, true);
        bassline(0, 2, 110);
        power(tG1, 0, 2, false, false, 110, 0.f);
        power(tG2, 0, 2, false, false, 108, 3.f);
        lw.play(riff, 0, 0.f, 0, rDeg);
        lw.play(riff, 1, 0.f, 4, rDeg);
        finalHit(c, tD, 2, -1, 0, 0, 0, 0);
        power(tG1, 2, 1, false, true, 118, 0.f);
        power(tG2, 2, 1, false, true, 115, 3.f);
        return;
    }
    if (pl.mode == Mode::Bed) {
        cleanArp(0, pl.totalBars);
        return;
    }
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        switch (s.part) {
            case Part::Intro:
                for (int b = b0; b < b0 + nb; b++) lw.play(riff, b, 0.f, (b - b0) % 4 == 3 ? 4 : 0, rDeg);
                power(tG1, b0, nb, false, true, 100, 0.f);
                if (nb >= 4) {
                    drums(b0 + nb / 2, nb - nb / 2, 0.7f, true, true);
                    bassline(b0 + nb / 2, nb - nb / 2, 100);
                    power(tG2, b0 + nb / 2, nb - nb / 2, false, true, 100, 3.f);
                }
                break;
            case Part::Verse:
                drums(b0, nb, 0.6f, true, true);
                bassline(b0, nb, 100);
                power(tG1, b0, nb, true, false, 104, 0.f);
                power(tG2, b0, nb, true, false, 100, 3.f);
                if (pl.variant == 1) cleanArp(b0, nb);
                lw.velBase = 92.f;
                lw.section(mV, b0, nb, vDeg);
                break;
            case Part::Pre:
                drums(b0, nb, 0.75f, true, false);
                bassline(b0, nb, 104);
                power(tG1, b0, nb, false, true, 104, 0.f);
                power(tG2, b0, nb, false, true, 100, 3.f);
                padChords(c, tO, b0, nb, 55, 72, 4, 80);
                break;
            case Part::Chorus:
                drums(b0, nb, 1.f, true, true);
                bassline(b0, nb, 112);
                power(tG1, b0, nb, false, false, 112, 0.f);
                power(tG2, b0, nb, false, false, 110, 3.f);
                if (pl.variant == 1) padChords(c, tO, b0, nb, 55, 74, 4, 90);
                lw.velBase = 106.f;
                lw.section(mC, b0, nb, cDeg);
                break;
            case Part::Solo:
                drums(b0, nb, 0.9f, true, true);
                bassline(b0, nb, 108);
                power(tG1, b0, nb, false, false, 104, 0.f);
                improvise(c, tLG, b0, nb, 64, 90, MelStyle::Busy, 104);
                break;
            default:
                drums(b0, Max(1, nb - 1), 0.8f, true, true);
                bassline(b0, Max(1, nb - 1), 108);
                power(tG1, b0, nb - 1, false, false, 110, 0.f);
                power(tG2, b0, nb - 1, false, false, 108, 3.f);
                finalHit(c, tD, b0 + nb - 1, -1, 0, 0, 0, 0);
                power(tG1, b0 + nb - 1, 1, false, true, 118, 0.f);
                power(tG2, b0 + nb - 1, 1, false, true, 115, 3.f);
                c.note(tB, b0 + nb - 1, 0.f, 3.5f, c.bassPitch(c.chordAt(b0 + nb - 1, 0.f), 28), 115);
                break;
        }
    }
}

// =============================================================================================
// Jazz lounge (swing / bossa nova)
static int rootlessVoicing(const Chord& ch, int lo, int hi, int* out) {
    // 3rd, 7th (or 6th), 9th, 5th - omit root
    int pcs[5], n = 0;
    auto add = [&](int iv) { pcs[n++] = (ch.root + iv) % 12; };
    int third = ch.hasPc(ch.root + 4) ? 4 : 3;
    add(third);
    if (ch.hasPc(ch.root + 10)) add(10);
    else if (ch.hasPc(ch.root + 11)) add(11);
    else if (ch.hasPc(ch.root + 9)) add(9);
    add(2);
    if (ch.hasPc(ch.root + 6)) add(6);
    else add(7);
    Chord tmp;
    tmp.root = pcs[0];
    tmp.n = n;
    for (int i = 0; i < n; i++) tmp.iv[i] = (i8)((pcs[i] - pcs[0] + 12) % 12);
    int prev[1] = {0};
    return voiceChord(tmp, lo, hi, 4, out, prev, 0);
}

static void composeJazz(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    bool bossa = pl.variant == 1;
    bool ballad = !bossa && pl.bpm < 104.f;
    std::vector<Prog> pp;
    if (bossa) pp = {PROG(kBossa), PROG(kJazzD), PROG(kJazzA)};
    else {
        Prog a[3] = {PROG(kJazzB), PROG(kJazzC), PROG(kJazzE)};
        pp = {a[r.irange(0, 2)], PROG(kJazzD), PROG(kJazzA)};
    }
    c.buildChords(pp);
    sd.kitStyle = KitStyle::Jazz;
    sd.revSize = 0.85f; sd.revDecay = 1.3f; sd.revDamp = 0.55f;
    sd.dlyL = 0.75f; sd.dlyR = 1.f; sd.dlyFb = 0.2f;
    int tD = c.track(tdDrums(0.75f, 0.14f));
    TrackDef bass; bass.patch = pString(1.8f, 0.22f, 0.3f, 1500.f); bass.patch.poly = 2; bass.gain = 1.0f; bass.lsF = 110.f; bass.lsDb = 4.f; bass.lpf = 2400.f; bass.rev = 0.05f;
    int tB = c.track(bass);
    TrackDef comp;
    if (bossa) { comp.patch = pString(2.4f, 0.45f, 0.22f, 4000.f); comp.gain = 0.55f; comp.panSpread = 0.1f; comp.pan = -0.2f; }
    else { comp.patch = r.chance(0.7f) ? pPiano() : pFmEP(); comp.gain = 0.5f; comp.pan = -0.15f; }
    comp.rev = 0.2f; comp.hpf = 90.f;
    int tC = c.track(comp);
    TrackDef lead;
    int leadKind = r.irange(0, 2);
    if (bossa) leadKind = r.chance(0.5f) ? 2 : 0;
    if (leadKind == 0) { lead.patch = pSax(); lead.gain = 0.5f; }
    else if (leadKind == 1) { lead.patch = pVibes(); lead.gain = 0.42f; }
    else { lead.patch = pSoftLead(Wave::Sine); lead.gain = 0.42f; }
    lead.rev = 0.22f; lead.pan = 0.2f; lead.hpf = 150.f;
    int tL = c.track(lead);
    Motif mA = makeMotif(r, bossa ? MelStyle::Pop : MelStyle::Swing, 8.f), mB = makeMotif(r, bossa ? MelStyle::Sparse : MelStyle::Swing, 8.f);
    int aDeg = r.irange(2, 6), bDeg = r.irange(0, 4);
    MelodyWriter lw(c, tL, 60, 84);
    lw.velBase = 92.f;
    lw.legato = 0.9f;
    auto drumsSwing = [&](int bar, int bars, float energy, bool fill) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            static const float kRide[] = {0.f, 1.f, 1.5f, 2.f, 3.f, 3.5f};
            for (float t : kRide) c.hit(tD, b, t, DK_RIDE, (fmodf(t, 1.f) > 0.01f ? 62 : 80) + r.irange(-6, 6), 6.f);
            c.hit(tD, b, 1.f, DK_HAT_P, 70);
            c.hit(tD, b, 3.f, DK_HAT_P, 72);
            for (int k = 0; k < 4; k++) c.hit(tD, b, (float)k, DK_KICK, 38 + r.irange(-4, 4));
            if (ballad) {
                for (int k = 0; k < 4; k++) c.hit(tD, b, (float)k, DK_BRUSH_SWISH, 60);
                c.hit(tD, b, 1.f, DK_BRUSH_TAP, 70);
                c.hit(tD, b, 3.f, DK_BRUSH_TAP, 74);
            } else {
                int comps = energy > 0.7f ? 3 : 1;
                for (int k = 0; k < comps; k++) c.hit(tD, b, (float)r.irange(0, 7) * 0.5f, r.chance(0.5f) ? DK_SNARE : DK_BRUSH_TAP, 48 + r.irange(0, 20), 6.f);
            }
            if (last) {
                c.hit(tD, b, 2.5f, DK_SNARE, 80);
                c.hit(tD, b, 3.5f, DK_SNARE, 90);
                c.hit(tD, b, 3.5f, DK_KICK, 80);
            }
        }
    };
    auto drumsBossa = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            static const float kRimA[] = {0.f, 1.5f, 3.f};
            static const float kRimB[] = {1.f, 2.5f};
            if ((b & 1) == 0) for (float t : kRimA) c.hit(tD, b, t, DK_RIM, 74);
            else for (float t : kRimB) c.hit(tD, b, t, DK_RIM, 74);
            c.hit(tD, b, 0.f, DK_KICK, 70);
            c.hit(tD, b, 1.5f, DK_KICK, 55);
            c.hit(tD, b, 2.f, DK_KICK, 70);
            c.hit(tD, b, 3.5f, DK_KICK, 55);
            for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tD, b, t, DK_SHAKER, (fmodf(t, 1.f) < 0.01f ? 60 : 48) + r.irange(-4, 4));
        }
    };
    auto walking = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++)
            for (int k = 0; k < 4; k++) {
                const Chord& ch = c.chordAt(b, (float)k);
                int root = c.bassPitch(ch, 31);
                int p;
                if (k == 0) p = root;
                else if (k == 3) {
                    int nb = (b + 1) * 4 < (int)c.beatChord.size() ? b + 1 : b;
                    int nr = c.bassPitch(c.chordAt(nb, 0.f), 31);
                    p = nr + (r.chance(0.5f) ? 1 : -1);
                } else {
                    int opts[3] = {root + ch.iv[1], root + 7, root + (ch.n > 3 ? ch.iv[3] : 12)};
                    p = opts[r.irange(0, 2)];
                    if (p > 52) p -= 12;
                }
                c.note(tB, b, (float)k, 0.9f, Clamp(p, 28, 55), 96 + (k == 0 ? 10 : 0) + r.irange(-6, 6), 0, 6.f);
            }
    };
    auto bossaBass = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            const Chord& ch = c.chordAt(b, 0.f);
            int root = c.bassPitch(ch, 31);
            c.note(tB, b, 0.f, 1.4f, root, 100);
            c.note(tB, b, 1.5f, 0.5f, root + 7, 80);
            const Chord& ch2 = c.chordAt(b, 2.f);
            int r2 = c.bassPitch(ch2, 31);
            c.note(tB, b, 2.f, 1.4f, r2 + 7 > 52 ? r2 - 5 : r2 + 7, 96);
            c.note(tB, b, 3.5f, 0.5f, r2, 80);
        }
    };
    auto compPiano = [&](int bar, int bars, int vel) {
        for (int b = bar; b < bar + bars; b++) {
            int pattern = r.irange(0, 3);
            float hits[3];
            int nh;
            float lens[3];
            if (pattern == 0) { hits[0] = 0.f; lens[0] = 1.f; hits[1] = 1.5f; lens[1] = 0.5f; nh = 2; }
            else if (pattern == 1) { hits[0] = 0.5f; lens[0] = 1.f; hits[1] = 2.5f; lens[1] = 1.f; nh = 2; }
            else if (pattern == 2) { hits[0] = 1.5f; lens[0] = 1.f; hits[1] = 3.f; lens[1] = 0.5f; nh = 2; }
            else { hits[0] = 0.f; lens[0] = 2.f; hits[1] = 2.5f; lens[1] = 0.5f; hits[2] = 3.5f; lens[2] = 0.5f; nh = 3; }
            for (int h = 0; h < nh; h++) {
                int v[6];
                int n = rootlessVoicing(c.chordAt(b, hits[h]), 52, 72, v);
                for (int i = 0; i < n; i++) c.note(tC, b, hits[h], lens[h], v[i], vel + r.irange(-8, 8), 0, 8.f);
            }
        }
    };
    auto compBossa = [&](int bar, int bars) {
        static const float kH[] = {0.f, 1.f, 1.5f, 2.5f, 3.f};
        for (int b = bar; b < bar + bars; b++)
            for (float t : kH) {
                int v[6];
                int n = rootlessVoicing(c.chordAt(b, t), 52, 69, v);
                strum(c, tC, b, t, 0.45f, v, n, true, 72 + (fmodf(t, 1.f) < 0.01f ? 6 : 0), 12.f);
            }
    };
    auto rhythm = [&](int bar, int bars, float energy, bool fill) {
        if (bossa) {
            drumsBossa(bar, bars);
            bossaBass(bar, bars);
            compBossa(bar, bars);
        } else {
            drumsSwing(bar, bars, energy, fill);
            walking(bar, bars);
            compPiano(bar, bars, 70);
        }
    };
    if (pl.mode == Mode::Jingle) {
        rhythm(0, 2, 0.8f, true);
        lw.play(makeMotif(r, MelStyle::Swing, 4.f), 0, 0.f, 0, aDeg);
        lw.play(makeMotif(r, MelStyle::Swing, 4.f), 1, 0.f, 4, aDeg);
        int v[6];
        int n = rootlessVoicing(c.chordAt(2, 0.f), 52, 72, v);
        for (int i = 0; i < n; i++) c.note(tC, 2, 0.f, 3.5f, v[i], 80);
        c.note(tB, 2, 0.f, 3.5f, c.bassPitch(c.chordAt(2, 0.f), 31), 100);
        c.hit(tD, 2, 0.f, DK_RIDE_BELL, 80);
        return;
    }
    if (pl.mode == Mode::Bed) {
        if (bossa) { compBossa(0, pl.totalBars); bossaBass(0, pl.totalBars); drumsBossa(0, pl.totalBars); }
        else { compPiano(0, pl.totalBars, 60); walking(0, pl.totalBars); for (int b = 0; b < pl.totalBars; b++) for (int k = 0; k < 4; k++) c.hit(tD, b, (float)k, DK_BRUSH_SWISH, 50); }
        return;
    }
    bool firstSolo = true;
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        switch (s.part) {
            case Part::Intro:
                if (bossa) { compBossa(b0, nb); bossaBass(b0 + nb / 2, nb - nb / 2); }
                else { compPiano(b0, nb, 66); improvise(c, tC, b0, nb, 67, 86, MelStyle::Swing, 70); }
                break;
            case Part::Verse:
                rhythm(b0, nb, 0.6f, true);
                lw.section(mA, b0, nb, aDeg);
                break;
            case Part::Bridge:
                rhythm(b0, nb, 0.7f, true);
                lw.section(mB, b0, nb, bDeg);
                break;
            case Part::Solo:
                rhythm(b0, nb, 0.9f, true);
                if (firstSolo) improvise(c, tL, b0, nb, 60, 86, bossa ? MelStyle::Pop : MelStyle::Swing, 92);
                else if (!bossa) improvise(c, tC, b0, nb, 64, 88, MelStyle::Swing, 84);
                else improvise(c, tL, b0, nb, 62, 86, MelStyle::Latin, 88);
                firstSolo = false;
                break;
            default: {
                rhythm(b0, Max(1, nb - 1), 0.5f, false);
                int last = b0 + nb - 1;
                forceTonicEnding(c, last);
                Chord fin = buildChord(pl.key, PStep{0, 0, 'J', 4});
                int v[6];
                int n = rootlessVoicing(fin, 52, 74, v);
                for (int i = 0; i < n; i++) c.note(tC, last, 0.f, 3.8f, v[i], 76);
                c.note(tB, last, 0.f, 3.8f, c.bassPitch(fin, 31), 96);
                c.note(tL, last, 0.f, 3.5f, degPitch(pl.key, 7, 60), 84);
                c.hit(tD, last, 0.f, DK_CRASH, 70);
                break;
            }
        }
    }
}

// =============================================================================================
// Country / americana
static void composeCountry(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    Prog vOpts[3] = {PROG(kMaj5), PROG(kMaj6), PROG(kMaj3)};
    Prog cOpts[3] = {PROG(kMaj1), PROG(kMaj8), PROG(kMaj7)};
    std::vector<Prog> pp = {vOpts[r.irange(0, 2)], cOpts[r.irange(0, 2)]};
    c.buildChords(pp);
    if (pl.mode != Mode::Bed) forceTonicEnding(c, pl.totalBars - 1);
    sd.kitStyle = KitStyle::Country;
    sd.revSize = 0.9f; sd.revDecay = 1.3f; sd.revDamp = 0.5f;
    sd.dlyL = 0.25f; sd.dlyR = 0.33f; sd.dlyFb = 0.15f;  // slapback
    bool bluegrass = pl.variant == 2;
    int tD = c.track(tdDrums(bluegrass ? 0.55f : 0.8f, 0.1f));
    TrackDef bass; bass.patch = pString(bluegrass ? 1.4f : 2.6f, 0.3f, 0.25f, 2000.f); bass.patch.poly = 2; bass.gain = 0.95f; bass.lsF = 100.f; bass.lsDb = 3.f; bass.lpf = 2600.f; bass.rev = 0.03f;
    int tB = c.track(bass);
    TrackDef ac; ac.patch = pString(2.6f, 0.78f, 0.13f, 8000.f); ac.gain = 0.45f; ac.lsF = 120.f; ac.lsDb = 2.f; ac.pkF = 220.f; ac.pkQ = 1.2f; ac.pkDb = 3.f; ac.hsF = 5000.f; ac.hsDb = 3.f; ac.pan = -0.35f; ac.rev = 0.15f; ac.hpf = 80.f; ac.panSpread = 0.05f;
    int tAc = c.track(ac);
    TrackDef steel; steel.patch = pSoftLead(Wave::Triangle); steel.patch.glide = 0.12f; steel.patch.aA = 0.12f; steel.patch.vibDepth = 0.14f; steel.patch.vibRate = 5.8f; steel.patch.noiseLvl = 0.f;
    steel.gain = 0.3f; steel.pan = 0.4f; steel.rev = 0.3f; steel.dly = 0.1f;
    int tSt = c.track(steel);
    TrackDef fid; fid.patch = pSawLead(); fid.patch.cutoff = 3400.f; fid.patch.reso = 1.2f; fid.patch.vibDepth = 0.25f; fid.patch.vibRate = 6.f; fid.patch.vibDelay = 0.12f; fid.patch.glide = 0.03f; fid.patch.noiseLvl = 0.03f; fid.patch.aA = 0.05f;
    fid.gain = 0.3f; fid.pkF = 2500.f; fid.pkDb = 4.f; fid.pan = 0.3f; fid.rev = 0.2f;
    int tF = c.track(fid);
    TrackDef banjo; banjo.patch = pString(0.9f, 0.95f, 0.08f, 9000.f); banjo.gain = 0.38f; banjo.pkF = 1200.f; banjo.pkDb = 4.f; banjo.pan = 0.25f; banjo.rev = 0.12f; banjo.hpf = 150.f;
    int tBj = c.track(banjo);
    TrackDef tw; tw.patch = pString(2.2f, 0.85f, 0.12f, 9000.f); tw.gain = 0.42f; tw.drive = 0.15f; tw.dly = 0.3f; tw.rev = 0.12f; tw.pan = 0.2f; tw.hpf = 120.f; tw.hsF = 3000.f; tw.hsDb = 3.f;
    int tTw = c.track(tw);
    TrackDef vox; vox.patch = pVox(); vox.gain = 0.55f; vox.rev = 0.2f; vox.dly = 0.12f; vox.hpf = 150.f;
    int tV = c.track(vox);
    bool femaleSinger = r.chance(0.5f);
    Motif mV = makeMotif(r, MelStyle::Pop, 8.f), mC = makeMotif(r, MelStyle::Pop, 8.f), lick = makeMotif(r, MelStyle::Busy, 4.f);
    MelodyWriter vw(c, tV, femaleSinger ? 62 : 55, femaleSinger ? 81 : 74);
    vw.velBase = 95.f;
    MelodyWriter tww(c, tTw, 62, 86);
    int vDeg = r.irange(0, 4), cDeg = r.irange(2, 6);
    auto drums = [&](int bar, int bars, float energy, bool fill) {
        for (int b = bar; b < bar + bars; b++) {
            bool last = fill && b == bar + bars - 1;
            if (bluegrass) {
                c.hit(tD, b, 1.f, DK_BRUSH_TAP, 70);
                c.hit(tD, b, 3.f, DK_BRUSH_TAP, 74);
                continue;
            }
            c.hit(tD, b, 0.f, DK_KICK, 104);
            c.hit(tD, b, 2.f, DK_KICK, 98);
            if (pl.variant == 0) {
                for (float t = 0.f; t < 3.99f; t += 0.25f) {
                    bool acc = fabsf(t - 1.f) < 0.01f || fabsf(t - 3.f) < 0.01f;
                    c.hit(tD, b, t, DK_SNARE, acc ? 100 : 40 + r.irange(0, 12), 3.f);
                }
            } else {
                c.hit(tD, b, 1.f, DK_SNARE, 104);
                c.hit(tD, b, 3.f, DK_SNARE, 106);
                for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tD, b, t, DK_HAT_C, (fmodf(t, 1.f) < 0.01f ? 78 : 58) + r.irange(-5, 5));
            }
            if (energy > 0.9f && b == bar) c.hit(tD, b, 0.f, DK_CRASH, 96);
            if (last) tomFill(c, tD, b, 3.f, 0.25f, 90);
        }
    };
    auto bassBoomChick = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            const Chord& ch = c.chordAt(b, 0.f);
            int root = c.bassPitch(ch, 31);
            c.note(tB, b, 0.f, 0.9f, root, 104);
            bool change = c.chordChangesAt(b + 1, 0.f) && (b + 1) * 4 < (int)c.beatChord.size();
            if (change && r.chance(0.6f)) {
                int nr = c.bassPitch(c.chordAt(b + 1, 0.f), 31);
                int dir = nr > root ? 1 : -1;
                c.note(tB, b, 1.f, 0.9f, root + 7 > 50 ? root - 5 : root + 7, 92);
                int d0 = pitchToDeg(pl.key, nr, 0);
                c.note(tB, b, 2.f, 0.9f, degPitch(pl.key, d0 - 2 * dir, 0), 90);
                c.note(tB, b, 3.f, 0.9f, degPitch(pl.key, d0 - dir, 0), 94);
            } else {
                c.note(tB, b, 2.f, 0.9f, root + 7 > 50 ? root - 5 : root + 7, 96);
            }
        }
    };
    auto strumBars = [&](int bar, int bars, int vel) {
        static const float kT[] = {0.f, 1.f, 1.5f, 2.5f, 3.f, 3.5f};
        static const bool kDown[] = {true, true, false, false, true, false};
        for (int b = bar; b < bar + bars; b++)
            for (int k = 0; k < 6; k++) {
                int v[6];
                int n = guitarVoicing(c.chordAt(b, kT[k]), v, 6);
                strum(c, tAc, b, kT[k], 0.55f, v, n, kDown[k], vel - (kDown[k] ? 0 : 12), 11.f);
            }
    };
    auto banjoRoll = [&](int bar, int bars) {
        static const int kRoll[8] = {2, 1, 0, 2, 1, 0, 2, 1};
        for (int b = bar; b < bar + bars; b++) {
            int v[6];
            int n = guitarVoicing(c.chordAt(b, 0.f), v, 6);
            int top = Max(0, n - 3);
            for (int k = 0; k < 8; k++) {
                int idx = Clamp(top + kRoll[k], 0, n - 1);
                c.note(tBj, b, (float)k * 0.5f, 0.5f, v[idx] + 12 > 88 ? v[idx] : v[idx] + 12, 80 + r.irange(-8, 8), 0, 3.f);
            }
        }
    };
    auto steelSwells = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b += 2) {
            const Chord& ch = c.chordAt(b, 0.f);
            int p = c.nearestChordTone(ch, 72 + r.irange(-3, 4));
            c.note(tSt, b, 0.f, 3.5f, p, 80);
            const Chord& ch2 = c.chordAt(b + 1, 0.f);
            int p2 = c.nearestChordTone(ch2, p + r.irange(-2, 2));
            c.note(tSt, b + 1, 0.f, 3.5f, p2, 76, NF_SLIDE);
        }
    };
    if (pl.mode == Mode::Jingle) {
        drums(0, 2, 1.f, true);
        bassBoomChick(0, 2);
        strumBars(0, 2, 95);
        tww.play(lick, 0, 0.f, 0, 4);
        tww.play(lick, 1, 0.f, 4, 4);
        int v[6];
        int n = guitarVoicing(c.chordAt(2, 0.f), v, 6);
        strum(c, tAc, 2, 0.f, 3.5f, v, n, true, 105, 18.f);
        c.note(tB, 2, 0.f, 3.f, c.bassPitch(c.chordAt(2, 0.f), 31), 104);
        c.note(tSt, 2, 0.f, 3.5f, degPitch(pl.key, 9, 60), 84);
        return;
    }
    if (pl.mode == Mode::Bed) {
        strumBars(0, pl.totalBars, 62);
        steelSwells(0, pl.totalBars);
        return;
    }
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        switch (s.part) {
            case Part::Intro:
                strumBars(b0, nb, 80);
                for (int b = b0; b < b0 + nb; b++) tww.play(lick, b, 0.f, (b - b0) % 2 == 1 ? 4 : 0, 4);
                if (bluegrass) banjoRoll(b0, nb);
                break;
            case Part::Verse:
                drums(b0, nb, 0.6f, true);
                bassBoomChick(b0, nb);
                strumBars(b0, nb, 84);
                if (bluegrass) banjoRoll(b0, nb);
                vw.section(mV, b0, nb, vDeg);
                for (int b = b0 + 3; b < b0 + nb; b += 4) improvise(c, tF, b, 1, 67, 86, MelStyle::Busy, 74);
                break;
            case Part::Chorus:
                drums(b0, nb, 1.f, true);
                bassBoomChick(b0, nb);
                strumBars(b0, nb, 94);
                steelSwells(b0, nb);
                if (bluegrass) banjoRoll(b0, nb);
                vw.velBase = 104.f;
                vw.section(mC, b0, nb, cDeg);
                vw.velBase = 95.f;
                break;
            case Part::Solo:
                drums(b0, nb, 0.8f, true);
                bassBoomChick(b0, nb);
                strumBars(b0, nb, 86);
                if (pl.variant == 0) improvise(c, tTw, b0, nb, 62, 86, MelStyle::Busy, 96);
                else if (bluegrass) { banjoRoll(b0, nb); improvise(c, tF, b0, nb, 64, 88, MelStyle::Busy, 94); }
                else improvise(c, tSt, b0, nb, 64, 84, MelStyle::Pop, 90);
                break;
            default: {
                drums(b0, Max(1, nb - 1), 0.6f, false);
                bassBoomChick(b0, Max(1, nb - 1));
                strumBars(b0, Max(1, nb - 1), 84);
                int last = b0 + nb - 1;
                int v[6];
                int n = guitarVoicing(c.chordAt(last, 0.f), v, 6);
                strum(c, tAc, last, 0.f, 3.8f, v, n, true, 100, 20.f);
                c.note(tB, last, 0.f, 3.5f, c.bassPitch(c.chordAt(last, 0.f), 31), 100);
                c.note(tSt, last, 0.f, 3.8f, degPitch(pl.key, 9, 60), 80);
                c.hit(tD, last, 0.f, DK_CRASH, 80);
                break;
            }
        }
    }
}

// =============================================================================================
// Lo-fi chillhop
static void composeLoFi(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    Prog opts[4] = {PROG(kLofiA), PROG(kLofiB), PROG(kLofiC), PROG(kLofiD)};
    int a = r.irange(0, 3);
    std::vector<Prog> pp = {opts[a], opts[(a + 1 + r.irange(0, 2)) % 4]};
    c.buildChords(pp);
    sd.kitStyle = KitStyle::LoFi;
    sd.revSize = 0.9f; sd.revDecay = 1.6f; sd.revDamp = 0.65f;
    sd.dlyL = 0.75f; sd.dlyR = 0.5f; sd.dlyFb = 0.3f;
    sd.vinyl = 1.f;
    sd.wow = 1.f;
    int tD = c.track(tdDrums(0.85f, 0.08f));
    TrackDef bass; bass.patch = pSubBass(); bass.patch.mix2 = 0.2f; bass.gain = 0.6f; bass.rev = 0.02f;
    int tB = c.track(bass);
    TrackDef ep; ep.patch = pFmEP(); ep.gain = 0.55f; ep.rev = 0.22f; ep.lpf = 5500.f; ep.chorus = 0.2f;
    int tE = c.track(ep);
    TrackDef lead;
    int lk = r.irange(0, 2);
    if (lk == 0) lead.patch = pSoftLead(Wave::Sine);
    else if (lk == 1) lead.patch = pVibes();
    else lead.patch = pString(1.6f, 0.45f, 0.2f, 3500.f);
    lead.gain = 0.4f; lead.rev = 0.3f; lead.dly = 0.25f; lead.lpf = 6000.f; lead.pan = 0.2f;
    int tL = c.track(lead);
    Motif m = makeMotif(r, MelStyle::Sparse, 8.f), m2 = makeMotif(r, MelStyle::Pop, 8.f);
    MelodyWriter lw(c, tL, 64, 84);
    lw.velBase = 80.f;
    int deg = r.irange(2, 7);
    auto drums = [&](int bar, int bars, float energy) {
        for (int b = bar; b < bar + bars; b++) {
            c.hit(tD, b, 0.f, DK_KICK, 110, 10.f);
            c.hit(tD, b, 2.5f, DK_KICK, 96, 10.f);
            if (r.chance(0.4f)) c.hit(tD, b, 1.75f, DK_KICK, 78, 10.f);
            int sn = r.chance(0.3f) ? DK_RIM : DK_SNARE;
            c.hit(tD, b, 1.f, sn, 100, 12.f);
            c.hit(tD, b, 3.f, sn, 104, 12.f);
            for (float t = 0.f; t < 3.99f; t += 0.25f)
                if (r.chance(energy > 0.6f ? 0.9f : 0.7f)) c.hit(tD, b, t, DK_HAT_C, (fmodf(t, 0.5f) < 0.01f ? 62 : 44) + r.irange(-8, 8), 10.f);
        }
    };
    auto keys = [&](int bar, int bars, int vel) {
        for (int b = bar; b < bar + bars; b++) {
            int v[6];
            int prev[1] = {0};
            const Chord& ch = c.chordAt(b, 0.f);
            int n = voiceChord(ch, 55, 76, 5, v, prev, 0);
            if (r.chance(0.5f)) {
                for (int i = 0; i < n; i++) c.note(tE, b, 0.f + (float)i * 0.02f, 3.6f, v[i], vel + r.irange(-8, 6), 0, 10.f);
            } else {
                for (int i = 0; i < n; i++) {
                    c.note(tE, b, 0.f, 1.4f, v[i], vel + r.irange(-8, 6), 0, 10.f);
                    c.note(tE, b, 1.75f, 0.6f, v[i], vel - 12 + r.irange(-8, 6), 0, 10.f);
                    c.note(tE, b, 2.5f, 1.4f, v[i], vel - 6 + r.irange(-8, 6), 0, 10.f);
                }
            }
        }
    };
    auto bassline = [&](int bar, int bars) {
        for (int b = bar; b < bar + bars; b++) {
            int root = c.bassPitch(c.chordAt(b, 0.f), 33);
            c.note(tB, b, 0.f, 1.4f, root, 100, 0, 8.f);
            c.note(tB, b, 2.5f, 0.7f, root + (r.chance(0.5f) ? 7 : 0), 88, 0, 8.f);
            if (r.chance(0.5f)) c.note(tB, b, 3.5f, 0.4f, root + (r.chance(0.5f) ? 10 : 5), 80, 0, 8.f);
        }
    };
    sd.autom.push_back(Automation{0, 0.f, 9500.f});
    sd.autom.push_back(Automation{0xFFFFFFF0u, 0.f, 9500.f});
    if (pl.mode == Mode::Jingle) {
        drums(0, 2, 0.8f);
        keys(0, 2, 80);
        bassline(0, 2);
        lw.play(makeMotif(r, MelStyle::Pop, 4.f), 0, 0.f, 0, deg);
        lw.play(makeMotif(r, MelStyle::Pop, 4.f), 1, 0.f, 4, deg);
        keys(2, 1, 80);
        return;
    }
    if (pl.mode == Mode::Bed) {
        keys(0, pl.totalBars, 62);
        return;
    }
    for (const Section& s : pl.sections) {
        int b0 = s.startBar, nb = s.bars;
        switch (s.part) {
            case Part::Intro:
                keys(b0, nb, 72);
                break;
            case Part::Verse:
                drums(b0, nb, 0.5f);
                keys(b0, nb, 74);
                bassline(b0, nb);
                for (int b = b0 + 6; b < b0 + nb; b += 8) lw.play(m2, b, 0.f, 4, deg);
                break;
            case Part::Chorus:
                drums(b0, nb, 0.8f);
                keys(b0, nb, 80);
                bassline(b0, nb);
                lw.section(m, b0, nb, deg);
                break;
            default:
                keys(b0, nb, 70);
                drums(b0, Max(1, nb - 2), 0.4f);
                break;
        }
    }
}

// =============================================================================================
// Talk station news bumpers / beds (brass fanfare, strings, timpani, glockenspiel)
static void composeTalk(Comp& c) {
    SongData& sd = c.sd;
    const SongPlan& pl = c.pl;
    Rng& r = c.rng;
    std::vector<Prog> pp = {PROG(kMaj3)};
    c.buildChords(pp);
    if (pl.mode != Mode::Bed) forceTonicEnding(c, pl.totalBars - 1);
    sd.kitStyle = KitStyle::Score;
    sd.revSize = 1.3f; sd.revDecay = 1.8f; sd.revDamp = 0.4f;
    int tD = c.track(tdDrums(0.7f, 0.2f));
    TrackDef br; br.patch = pBrass(); br.gain = 0.5f; br.rev = 0.3f; br.hpf = 120.f;
    int tBr = c.track(br);
    TrackDef st; st.patch = pStrings(); st.gain = 0.45f; st.rev = 0.35f; st.chorus = 0.2f;
    int tSt = c.track(st);
    TrackDef gl; gl.patch = pFmBell(); gl.gain = 0.3f; gl.rev = 0.3f; gl.pan = 0.3f;
    int tG = c.track(gl);
    if (pl.mode == Mode::Bed) {
        padChords(c, tSt, 0, pl.totalBars, 55, 74, 4, 52);
        for (int b = 0; b < pl.totalBars; b++)
            for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tD, b, t, DK_HAT_C, 42 + r.irange(-3, 3));
        return;
    }
    static const float kFan[] = {0.f, 0.75f, 1.f, 2.f, 3.f};
    stabChords(c, tBr, 0, 2, 58, 77, 4, kFan, 5, 0.6f, 100);
    padChords(c, tSt, 0, pl.totalBars, 55, 76, 4, 80);
    for (int b = 0; b < 2; b++) {
        c.hit(tD, b, 0.f, DK_TOM_L, 110);
        c.hit(tD, b, 2.f, DK_TOM_M, 96);
        c.hit(tD, b, 3.5f, DK_TOM_L, 90);
    }
    for (int i = 0; i < 4; i++) c.note(tG, 1, (float)i * 0.5f, 0.5f, degPitch(pl.key, 7 + i * 2, 60), 90);
    finalHit(c, tD, pl.totalBars - 1, tBr, 58, 77, 4, 110);
    c.hit(tD, pl.totalBars - 1, 0.f, DK_TOM_L, 118);
}

// =============================================================================================
// Entry points
static void runComposer(Comp& c) {
    switch (c.pl.genre) {
        case Genre::Synthwave: composeSynthwave(c); break;
        case Genre::HipHop: composeHipHop(c); break;
        case Genre::Reggaeton: composeReggaeton(c); break;
        case Genre::House: composeHouse(c); break;
        case Genre::Rock: composeRock(c); break;
        case Genre::Jazz: composeJazz(c); break;
        case Genre::Country: composeCountry(c); break;
        case Genre::LoFi: composeLoFi(c); break;
        default: composeTalk(c); break;
    }
}

static float genreMaster(Genre g) {
    switch (g) {
        case Genre::Synthwave: return 1.0f;
        case Genre::HipHop: return 1.0f;
        case Genre::Reggaeton: return 1.0f;
        case Genre::House: return 1.0f;
        case Genre::Rock: return 1.0f;
        case Genre::Jazz: return 1.0f;
        case Genre::Country: return 1.0f;
        case Genre::LoFi: return 1.0f;
        default: return 1.0f;
    }
}

static std::shared_ptr<SongData> composeFromPlan(const SongPlan& pl) {
    auto sd = std::make_shared<SongData>();
    sd->genre = (int)pl.genre;
    sd->seed = pl.seed;
    sd->bpm = pl.bpm;
    sd->key = pl.key;
    sd->swing = pl.swing;
    Comp c(*sd, pl);
    runComposer(c);
    sd->sortEvents();
    sd->musicEnd = (u32)((float)pl.totalBars * 4.f * c.spb);
    sd->length = sd->musicEnd + (u32)((pl.mode == Mode::Song ? 2.5f : 1.2f) * kSR);
    sd->master = genreMaster(pl.genre);
    for (auto& e : sd->events) (void)e;
    return sd;
}

std::shared_ptr<SongData> composeSong(Genre g, u32 seed) { return composeFromPlan(planSong(g, seed)); }

float songDuration(Genre g, u32 seed) {
    SongPlan p = planSong(g, seed);
    return p.durationSec();
}

std::shared_ptr<SongData> composeJingle(Genre g, u32 seed) {
    SongPlan p = planSong(g == Genre::Talk ? Genre::Count : g, seed);
    p.genre = g;
    if (g == Genre::Talk) {
        p.bpm = 112.f;
        p.key.tonic = 0;
        p.key.scale = Scale::Major;
    }
    p.mode = Mode::Jingle;
    p.sections = {Section{Part::Chorus, 2, 0, 1.f, 0}, Section{Part::Outro, 1, 2, 0.5f, 0}};
    finalizeSections(p);
    return composeFromPlan(p);
}

std::shared_ptr<SongData> composeBed(Genre g, u32 seed, float seconds) {
    SongPlan p = planSong(g == Genre::Talk ? Genre::Count : g, seed);
    p.genre = g;
    if (g == Genre::Talk) {
        p.bpm = 100.f;
        p.key.tonic = 2;
        p.key.scale = Scale::Major;
    }
    p.mode = Mode::Bed;
    int bars = Max(2, (int)ceilf(seconds / p.barSec()) + 1);
    p.sections = {Section{Part::Breakdown, bars, 0, 0.3f, 0}};
    finalizeSections(p);
    auto sd = composeFromPlan(p);
    return sd;
}

// =============================================================================================
// Adaptive mission score: an endless composition generated in 8-bar chunks with four layers
// (0 pads/drones, 1 percussion, 2 bass ostinato, 3 leads/stabs) faded by intensity.
struct ScoreGen {
    int mood = 0;
    int style = 0;
    SongPlan plan;
    std::shared_ptr<SongData> sd;
    int chunk = 0;
    int tD = 0, tDrone = 0, tPad = 0, tBass = 0, tArp = 0, tBrass = 0, tLead = 0, tPerc = 0;
    std::vector<Prog> progs;
    Motif motif;

    void init(int moodSeed) {
        mood = moodSeed;
        u32 seed = hash32((u32)moodSeed * 2654435761u + 0x5C0Eu);
        Rng r(seed, 5);
        style = (int)(seed % 4u);
        plan = SongPlan();
        plan.seed = seed;
        plan.genre = Genre::Count;
        plan.mode = Mode::Song;
        static const float kBpmLo[4] = {100.f, 140.f, 80.f, 118.f}, kBpmHi[4] = {114.f, 158.f, 94.f, 128.f};
        plan.bpm = (float)r.irange((int)kBpmLo[style], (int)kBpmHi[style]);
        plan.key.tonic = r.irange(0, 11);
        static const Scale kSc[4] = {Scale::Minor, Scale::HarmonicMinor, Scale::Phrygian, Scale::Dorian};
        plan.key.scale = kSc[r.irange(0, 3)];
        plan.sections = {Section{Part::Verse, 8, 0, 1.f, 0}};
        finalizeSections(plan);
        sd = std::make_shared<SongData>();
        sd->genre = (int)Genre::Count;
        sd->seed = seed;
        sd->bpm = plan.bpm;
        sd->key = plan.key;
        sd->kitStyle = KitStyle::Score;
        sd->revSize = 1.4f; sd->revDecay = 2.2f; sd->revDamp = 0.45f;
        sd->dlyL = 0.75f; sd->dlyR = 1.f; sd->dlyFb = 0.3f;
        sd->looping = true;
        sd->length = 0xFFFFFFFFu;
        sd->musicEnd = 0xFFFFFFFFu;
        sd->master = 1.f;
        for (int k : {DK_KICK, DK_SNARE, DK_HAT_C, DK_HAT_O, DK_TOM_L, DK_TOM_M, DK_TOM_H, DK_SHAKER, DK_CRASH, DK_RIM, DK_CLAP, DK_IMPACT, DK_REV_CYM}) sd->kitUsed[k] = true;
        TrackDef d = tdDrums(0.85f, 0.12f); d.layer = 1;
        sd->tracks.push_back(d); tD = 0;
        TrackDef drone; drone.patch = pSubBass(); drone.patch.mono = false; drone.patch.poly = 2; drone.patch.aA = 1.f; drone.patch.aR = 1.5f; drone.gain = 0.35f; drone.layer = 0;
        sd->tracks.push_back(drone); tDrone = 1;
        TrackDef pad; pad.patch = style == 2 ? pChoir() : pStrings(); pad.gain = 0.5f; pad.rev = 0.45f; pad.layer = 0; pad.hpf = 120.f;
        sd->tracks.push_back(pad); tPad = 2;
        TrackDef bass; bass.patch = style == 3 ? pFmBass() : pSawBass(); bass.gain = 0.55f; bass.layer = 2; bass.hpf = 30.f;
        sd->tracks.push_back(bass); tBass = 3;
        TrackDef arp; arp.patch = pPluck(Wave::Saw, 900.f, 0.18f); arp.gain = 0.3f; arp.dly = 0.3f; arp.rev = 0.2f; arp.layer = 3; arp.pan = -0.2f;
        sd->tracks.push_back(arp); tArp = 4;
        TrackDef brass; brass.patch = pBrass(); brass.gain = 0.42f; brass.rev = 0.3f; brass.layer = 3; brass.pan = 0.15f;
        sd->tracks.push_back(brass); tBrass = 5;
        TrackDef lead; lead.patch = pSawLead(); lead.gain = 0.34f; lead.dly = 0.25f; lead.rev = 0.3f; lead.layer = 3;
        sd->tracks.push_back(lead); tLead = 6;
        TrackDef perc = tdDrums(0.6f, 0.15f); perc.layer = 1; perc.panSpread = 0.4f;
        sd->tracks.push_back(perc); tPerc = 7;
        Prog a[4] = {PROG(kMinD), PROG(kMinB), PROG(kMinG), PROG(kMinE)};
        progs = {a[r.irange(0, 3)], a[r.irange(0, 3)]};
        motif = makeMotif(r, style == 1 ? MelStyle::Busy : MelStyle::Pop, 8.f);
        chunk = 0;
    }

    // Appends the events of the next 8-bar chunk.
    void appendChunk() {
        int bar0 = chunk * 8;
        SongPlan pl = plan;
        pl.seed = plan.seed + (u32)chunk * 7919u;
        pl.sections = {Section{Part::Verse, bar0 + 8, 0, 1.f, chunk & 1}};
        finalizeSections(pl);
        size_t first = sd->events.size();
        Comp c(*sd, pl);
        std::vector<Prog> pp = {progs[(size_t)(chunk & 1)]};
        pl.sections[0].prog = 0;
        c.buildChords(pp);
        Rng& r = c.rng;
        // layer 0: drone + pads
        for (int b = bar0; b < bar0 + 8; b += 2) {
            int root = c.bassPitch(c.chordAt(b, 0.f), 26);
            c.note(tDrone, b, 0.f, 7.8f, root, 90);
        }
        padChords(c, tPad, bar0, 8, 50, 72, 4, 72);
        // layer 1: percussion
        for (int b = bar0; b < bar0 + 8; b++) {
            switch (style) {
                case 1:  // chase: driving
                    for (int k = 0; k < 4; k++) c.hit(tD, b, (float)k, DK_KICK, 112);
                    c.hit(tD, b, 1.f, DK_SNARE, 110);
                    c.hit(tD, b, 3.f, DK_SNARE, 112);
                    for (float t = 0.f; t < 3.99f; t += 0.25f) c.hit(tD, b, t, DK_HAT_C, (fmodf(t, 0.5f) < 0.01f ? 70 : 50) + r.irange(-5, 5));
                    break;
                case 2:  // stealth: sparse toms and ticks
                    c.hit(tD, b, 0.f, DK_TOM_L, 90);
                    if (b & 1) c.hit(tD, b, 2.5f, DK_TOM_M, 70);
                    for (float t = 0.f; t < 3.99f; t += 0.5f) c.hit(tPerc, b, t, DK_RIM, 40 + r.irange(0, 10));
                    break;
                case 3:  // heist: funky
                    c.hit(tD, b, 0.f, DK_KICK, 110);
                    c.hit(tD, b, 1.75f, DK_KICK, 90);
                    c.hit(tD, b, 2.5f, DK_KICK, 100);
                    c.hit(tD, b, 1.f, DK_CLAP, 104);
                    c.hit(tD, b, 3.f, DK_CLAP, 106);
                    for (float t = 0.f; t < 3.99f; t += 0.25f) c.hit(tD, b, t, DK_HAT_C, (fmodf(t, 0.5f) < 0.01f ? 66 : 46) + r.irange(-5, 5));
                    break;
                default:  // neon noir: taiko-ish toms + pulse
                    c.hit(tD, b, 0.f, DK_KICK, 110);
                    c.hit(tD, b, 2.f, DK_KICK, 100);
                    c.hit(tD, b, 1.f, DK_SNARE, 96);
                    c.hit(tD, b, 3.f, DK_SNARE, 100);
                    c.hit(tPerc, b, 1.5f, DK_TOM_M, 80);
                    c.hit(tPerc, b, 3.5f, DK_TOM_L, 86);
                    for (float t = 0.f; t < 3.99f; t += 0.25f) c.hit(tPerc, b, t, DK_SHAKER, 45 + r.irange(0, 12));
                    break;
            }
            if ((b - bar0) == 7) tomFill(c, tD, b, 2.f, 0.25f, 96);
        }
        c.hit(tD, bar0, 0.f, DK_CRASH, 90);
        // layer 2: bass ostinato
        float step = style == 1 ? 0.25f : 0.5f;
        for (int b = bar0; b < bar0 + 8; b++)
            for (float t = 0.f; t < 3.99f; t += step) {
                int root = c.bassPitch(c.chordAt(b, t), 31);
                int k = (int)(t / step);
                int p = root + ((style == 3 && k % 4 == 3) ? 7 : 0) + ((k % 8 == 6) ? 12 : 0);
                c.note(tBass, b, t, step * 0.75f, p, 100 + (k % 2 == 0 ? 8 : -4), 0, 1.f);
            }
        // layer 3: arps, brass stabs, lead motif
        arpeggiate(c, tArp, bar0, 8, 60, 19, style == 2 ? 0.5f : 0.25f, chunk % 3, 72, 0.5f);
        static const float kSt[] = {0.f, 1.5f, 3.f};
        stabChords(c, tBrass, bar0, 8, 55, 74, 3, kSt, style == 2 ? 1 : 3, 0.4f, 96);
        MelodyWriter mw(c, tLead, 62, 84);
        mw.velBase = 96.f;
        mw.phrase8(motif, bar0, 4, (chunk & 1) == 1);
        // keep chunk events sorted and after the chunk start
        u32 chunkStart = (u32)((float)bar0 * 4.f * c.spb);
        for (size_t i = first; i < sd->events.size(); i++) sd->events[i].start = Max(sd->events[i].start, chunkStart);
        std::stable_sort(sd->events.begin() + (long)first, sd->events.end(), [](const NoteEv& a, const NoteEv& b) { return a.start < b.start; });
        chunk++;
    }
    u32 chunkEndSample(int ch) const { return (u32)((float)(ch * 8) * 4.f * 60.f / plan.bpm * kSR); }
};

}  // namespace music
}  // namespace detail
}  // namespace Audio
