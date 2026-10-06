// Broadcast mastering for the music radio stations. Songs, DJ links, jingles and ads all run through one chain per
// station, set up from a per-genre preset:
//   1. a slow gated wideband AGC that levels songs and links against each other, and a 22 Hz subsonic filter;
//   2. four bands split by Linkwitz-Riley crossovers (120 Hz, 500 Hz, 3.2 kHz; the bands sum flat): a slow
//      tonal-balance AGC nudges each band a few dB toward the genre's target share of the spectrum (frozen while a
//      band carries little programme, so a voice-only link is not pumped full of bass), then a stereo-linked RMS
//      compressor per band densifies it (slow attack in the lows so kick drums keep their punch);
//   3. stereo image: the side signal is high-passed (mono low end), then widened, with extra width in the highs;
//   4. tone: low shelf, presence peak and air shelf, then the 15 kHz band limit of FM;
//   5. a 2x oversampled soft clipper and a lookahead limiter with a true-peak-safe ceiling.
namespace Audio {
namespace detail {
namespace master {

using namespace dsp;

struct Preset {
    float target[4];      // tonal balance: band share of total power (dB); the four shares sum to ~0 dB
    float agcRange;       // max boost / cut of the tonal AGC per band (dB)
    float thr[4];         // band compressor threshold above the band's nominal RMS (dB)
    float ratio[4];
    float attMs[4], relMs[4];
    float width;          // side gain above the mono crossover
    float airWidthDb;     // extra side gain above ~4 kHz
    float monoHz;         // side high-pass (mono below)
    float lowShelfDb, presenceDb, airDb;
    float topHz;          // final low-pass (FM limit, darker for lo-fi)
    float clipDb;         // soft-clip knee below the ceiling (dB): more is denser
    float outDb;          // make-up gain into the clipper (density)
    float levelDb;        // trim after the limiter to the common station loudness
};

// Genre order: Synthwave, HipHop, Reggaeton, House, Rock, Jazz, Country, LoFi.
static const Preset kPresets[8] = {
    // synthwave: wide, glossy, moderately dense
    {{-3.4f, -4.9f, -7.7f, -12.8f}, 4.f, {4.f, 3.f, 3.f, 3.5f}, {3.f, 2.5f, 2.5f, 2.f}, {28.f, 14.f, 7.f, 3.f}, {200.f, 160.f, 120.f, 90.f},
     1.30f, 2.5f, 130.f, 0.5f, 0.5f, 1.5f, 15000.f, 1.5f, 3.0f, -1.4f},
    // hip-hop: big mono sub, crisp top, dense
    {{-2.2f, -6.0f, -9.4f, -14.5f}, 5.f, {4.f, 3.f, 3.f, 3.5f}, {3.5f, 3.f, 3.f, 2.f}, {30.f, 12.f, 6.f, 2.5f}, {220.f, 160.f, 110.f, 80.f},
     1.20f, 3.0f, 150.f, 1.0f, 1.0f, 1.5f, 15000.f, 2.0f, 3.5f, -1.2f},
    // reggaeton: punchy low end, bright percussion
    {{-2.7f, -5.6f, -8.7f, -13.5f}, 4.5f, {4.f, 3.f, 3.f, 3.5f}, {3.f, 3.f, 3.f, 2.f}, {28.f, 12.f, 6.f, 2.5f}, {200.f, 150.f, 110.f, 80.f},
     1.25f, 2.5f, 140.f, 0.5f, 1.0f, 1.0f, 15000.f, 2.0f, 3.5f, -1.6f},
    // house: pumping lows, open top
    {{-2.6f, -5.9f, -8.8f, -12.1f}, 4.f, {4.f, 3.f, 3.f, 3.5f}, {3.f, 2.5f, 2.5f, 2.f}, {30.f, 14.f, 7.f, 3.f}, {180.f, 150.f, 110.f, 80.f},
     1.30f, 2.5f, 140.f, 0.5f, 0.5f, 1.5f, 15000.f, 2.0f, 3.5f, -1.1f},
    // rock: full low mids, controlled upper mids
    {{-4.5f, -4.1f, -6.9f, -12.9f}, 5.f, {4.f, 3.f, 3.f, 3.5f}, {3.f, 2.5f, 3.f, 2.f}, {25.f, 12.f, 5.f, 2.5f}, {200.f, 150.f, 110.f, 90.f},
     1.20f, 1.5f, 120.f, 1.0f, -0.5f, 0.5f, 15000.f, 2.0f, 3.0f, -2.1f},
    // jazz: warm, open, light processing
    {{-5.4f, -3.5f, -6.5f, -13.6f}, 5.f, {6.f, 5.f, 5.f, 5.f}, {1.8f, 1.8f, 1.8f, 1.8f}, {35.f, 20.f, 10.f, 5.f}, {300.f, 250.f, 200.f, 150.f},
     1.15f, 1.0f, 110.f, 1.0f, -0.5f, 0.5f, 15000.f, 0.5f, 2.5f, -2.1f},
    // country: warm and forward
    {{-4.7f, -4.2f, -6.6f, -11.9f}, 5.f, {4.5f, 3.5f, 3.5f, 3.5f}, {2.5f, 2.5f, 2.5f, 2.f}, {28.f, 14.f, 7.f, 3.f}, {220.f, 170.f, 130.f, 100.f},
     1.20f, 1.5f, 120.f, 0.5f, 0.0f, 1.0f, 15000.f, 1.0f, 3.0f, -1.9f},
    // lo-fi: dark, soft, glued
    {{-3.0f, -4.3f, -8.2f, -19.0f}, 4.f, {4.f, 3.f, 3.f, 3.5f}, {2.5f, 2.5f, 2.5f, 2.f}, {35.f, 18.f, 10.f, 5.f}, {260.f, 220.f, 180.f, 140.f},
     1.20f, 1.0f, 130.f, 1.0f, -1.0f, -1.0f, 11000.f, 0.5f, 3.0f, -0.9f},
};

// Linkwitz-Riley 4th-order split (two cascaded Butterworth sections per side); lo + hi is a 2nd-order allpass.
struct LR4 {
    Biquad lp0, lp1, hp0, hp1;
    void set(float f) {
        lp0.setLP(f, 0.70710678f);
        lp1 = lp0;
        hp0.setHP(f, 0.70710678f);
        hp1 = hp0;
    }
    FORCEINLINE void split(float x, float& lo, float& hi) {
        lo = lp1.process(lp0.process(x));
        hi = hp1.process(hp0.process(x));
    }
    void reset() {
        lp0.reset(); lp1.reset(); hp0.reset(); hp1.reset();
    }
};

// Four bands of one channel; the lower bands get the allpass of the crossovers above them so the sum stays flat.
struct Crossover4 {
    LR4 x1, x2, x3;
    Biquad ap2, ap3a, ap3b;
    void set(float f1, float f2, float f3) {
        x1.set(f1);
        x2.set(f2);
        x3.set(f3);
        ap2.setAllpass(f2, 0.70710678f);
        ap3a.setAllpass(f3, 0.70710678f);
        ap3b = ap3a;
    }
    FORCEINLINE void process(float x, float* b) {
        float lo, hi, lo2, hi2;
        x1.split(x, lo, hi);
        x2.split(hi, lo2, hi2);
        x3.split(hi2, b[2], b[3]);
        b[0] = ap3a.process(ap2.process(lo));
        b[1] = ap3b.process(lo2);
    }
};

// Stereo-linked RMS compressor with a soft knee (gain reduction smoothed in dB).
struct BandComp {
    float thr = -20.f, slope = 0.5f, knee = 6.f;
    float att = 0.1f, rel = 0.01f, det = 0.f, detK = 0.01f, gr = 0.f;
    void set(float thrDb, float ratio, float attMs, float relMs) {
        thr = thrDb;
        slope = 1.f - 1.f / Max(ratio, 1.f);
        att = 1.f - expf(-1.f / Max(attMs * 0.001f * kSR, 1.f));
        rel = 1.f - expf(-1.f / Max(relMs * 0.001f * kSR, 1.f));
        detK = 1.f - expf(-1.f / (0.004f * kSR));
    }
    FORCEINLINE float gain(float power) {
        det += (power - det) * detK;
        float lvl = 0.5f * fastGainToDb(det + 1e-12f);
        float over = lvl - thr, t;
        if (over <= -0.5f * knee) t = 0.f;
        else if (over >= 0.5f * knee) t = over * slope;
        else {
            float u = over + 0.5f * knee;
            t = slope * u * u / (2.f * knee);
        }
        gr += (t - gr) * (t > gr ? att : rel);
        return fastDbToGain(-gr);
    }
};

// 2x oversampled soft clipper. Half-band interpolation / decimation (Blackman-windowed, 8 taps a side, designed at
// init), clipping at the 2x rate keeps the aliasing of the clipped peaks out of the audible band.
struct OsClipper {
    float c[8];
    float t = 0.8f, ceil_ = 0.95f;
    float x[2][32], zE[2][32], zO[2][32];
    int w = 0;
    void init(float kneeLin, float ceilingLin) {
        t = kneeLin;
        ceil_ = ceilingLin;
        float s = 0.f;
        for (int i = 0; i < 8; i++) {
            float m = (float)(2 * i + 1);
            float h = ((i & 1) ? -1.f : 1.f) / (kPi * m);  // sin(pi m / 2) / (pi m)
            float win = 0.42f + 0.5f * cosf(kPi * m / 16.f) + 0.08f * cosf(kTwoPi * m / 16.f);
            c[i] = 2.f * h * win;
            s += c[i];
        }
        for (int i = 0; i < 8; i++) c[i] *= 0.5f / s;
        memset(x, 0, sizeof x);
        memset(zE, 0, sizeof zE);
        memset(zO, 0, sizeof zO);
        w = 0;
    }
    FORCEINLINE float clip(float v) const {
        float a = fabsf(v);
        if (a <= t) return v;
        float k = ceil_ - t;
        float y = t + k * fastTanh((a - t) / k);
        return v < 0.f ? -y : y;
    }
    // One stereo sample in, one out (15 samples of latency).
    FORCEINLINE void process(float& l, float& r) {
        float in[2] = {l, r}, out[2];
        for (int ch = 0; ch < 2; ch++) {
            float* xs = x[ch];
            xs[w & 31] = in[ch];
            // midpoint between n-8 and n-7
            float mid = 0.f;
            for (int i = 0; i < 8; i++) mid += c[i] * (xs[(w - 8 - i) & 31] + xs[(w - 7 + i) & 31]);
            zE[ch][w & 31] = clip(xs[(w - 8) & 31]);
            zO[ch][w & 31] = clip(mid);
            // decimate at j = n-15 (needs zO[j-8 .. j+7])
            float o = 0.5f * zE[ch][(w - 7) & 31];
            for (int i = 0; i < 8; i++) o += 0.5f * c[i] * (zO[ch][(w - 7 + i) & 31] + zO[ch][(w - 8 - i) & 31]);
            out[ch] = o;
        }
        w++;
        l = out[0];
        r = out[1];
    }
};

struct Chain {
    const Preset* p = &kPresets[0];
    Crossover4 xo[2];
    BandComp comp[4];
    float agcDb[4] = {0, 0, 0, 0}, agcPrev[4] = {1, 1, 1, 1}, slowP[4] = {0, 0, 0, 0};
    float nominal = 0.141f;  // wideband RMS the input AGC aims for (-17 dBFS)
    float inRms = 1e-4f, inGain = 1.f;
    Biquad sideHp0, sideHp1, sideAir, subsonic[2];
    Biquad eqLs[2], eqPk[2], eqHs[2], lp0[2], lp1[2];
    OsClipper clipper;
    float outGain = 1.f, levelGain = 1.f;
    LookaheadLimiter lim;
    bool speech = false;  // DJ links, news and ads: the tonal AGC relaxes to neutral instead of reshaping voices
    float bandBuf[4][2][kProdBlock];

    void init(int genre) {
        p = &kPresets[Clamp(genre, 0, 7)];
        for (int c = 0; c < 2; c++) xo[c].set(120.f, 500.f, 3200.f);
        float nomDb = gainToDb(nominal);
        for (int b = 0; b < 4; b++) {
            comp[b].set(nomDb + p->target[b] + p->thr[b], p->ratio[b], p->attMs[b], p->relMs[b]);
            agcDb[b] = 0.f;
            agcPrev[b] = 1.f;
            slowP[b] = nominal * nominal * dbToGain(p->target[b]);
        }
        subsonic[0].setHP(22.f, 0.70710678f);
        subsonic[1] = subsonic[0];
        sideHp0.setHP(p->monoHz, 0.70710678f);
        sideHp1 = sideHp0;
        sideAir.setHighShelf(4000.f, p->airWidthDb);
        for (int c = 0; c < 2; c++) {
            eqLs[c].setLowShelf(70.f, p->lowShelfDb);
            eqPk[c].setPeak(3000.f, 0.8f, p->presenceDb);
            eqHs[c].setHighShelf(11000.f, p->airDb);
            lp0[c].setLP(p->topHz, 0.54f);
            lp1[c].setLP(p->topHz, 1.31f);
        }
        // make-up gain goes in ahead of the clipper; clipDb sets how far below the ceiling its soft knee starts
        clipper.init(0.86f * dbToGain(-p->clipDb), 0.86f);
        outGain = dbToGain(p->outDb);
        levelGain = dbToGain(p->levelDb);
        lim.init(72, 0.85f, 70.f);
        inRms = 1e-4f;
        inGain = 1.f;
    }

    void process(float* L, float* R, int n) {
        // 1) wideband AGC (gated: frozen while the programme is near silence)
        for (int i = 0; i < n; i++) {
            float x = 0.5f * (L[i] * L[i] + R[i] * R[i]);
            inRms += (x - inRms) * 0.00004f;
        }
        float rms = sqrtf(inRms);
        float g0 = inGain;
        if (rms > 0.006f) {
            float want = Clamp(nominal / rms, 0.4f, 3.2f);
            inGain += (want - inGain) * (want < inGain ? 0.015f : 0.005f);
        }
        // 2) bands: split, tonal AGC gains (block-rate, ramped), compressors
        float bp[4] = {0, 0, 0, 0};
        for (int i = 0; i < n; i++) {
            float g = g0 + (inGain - g0) * ((float)i / (float)n);
            float bl[4], br[4];
            xo[0].process(subsonic[0].process(L[i] * g), bl);
            xo[1].process(subsonic[1].process(R[i] * g), br);
            for (int b = 0; b < 4; b++) {
                bandBuf[b][0][i] = bl[b];
                bandBuf[b][1][i] = br[b];
                bp[b] += bl[b] * bl[b] + br[b] * br[b];
            }
        }
        const float kSlow = 1.f - expf(-(float)n / (2.5f * kSR));
        const float slew = 1.5f * (float)n / kSR;  // dB per block (1.5 dB/s)
        float tot = 0.f;
        for (int b = 0; b < 4; b++) {
            slowP[b] += (bp[b] / (2.f * (float)n) - slowP[b]) * kSlow;
            tot += slowP[b];
        }
        float gNow[4];
        for (int b = 0; b < 4; b++) {
            if (speech) {
                agcDb[b] += Clamp(-agcDb[b], -slew, slew);
            } else if (tot > 1e-5f) {
                float share = 10.f * log10f(Max(slowP[b], 1e-12f) / tot);
                if (share > p->target[b] - 14.f) {
                    float want = Clamp(p->target[b] - share, -p->agcRange, p->agcRange);
                    agcDb[b] += Clamp(want - agcDb[b], -slew, slew);
                }
            }
            gNow[b] = dbToGain(agcDb[b]);
        }
        for (int i = 0; i < n; i++) {
            float u = (float)i / (float)n;
            float l = 0.f, r = 0.f;
            for (int b = 0; b < 4; b++) {
                float ga = agcPrev[b] + (gNow[b] - agcPrev[b]) * u;
                float bl = bandBuf[b][0][i] * ga, br = bandBuf[b][1][i] * ga;
                float gc = comp[b].gain(0.5f * (bl * bl + br * br));
                l += bl * gc;
                r += br * gc;
            }
            // 3) stereo image
            float m = 0.5f * (l + r), s = 0.5f * (l - r);
            s = sideHp1.process(sideHp0.process(s));
            s = sideAir.process(s) * p->width;
            l = m + s;
            r = m - s;
            // 4) tone + band limit
            l = lp1[0].process(lp0[0].process(eqHs[0].process(eqPk[0].process(eqLs[0].process(l)))));
            r = lp1[1].process(lp0[1].process(eqHs[1].process(eqPk[1].process(eqLs[1].process(r)))));
            // 5) make-up gain, clipper (the limiter below only catches what the decimation filter lets over)
            l *= outGain;
            r *= outGain;
            clipper.process(l, r);
            L[i] = l;
            R[i] = r;
        }
        for (int b = 0; b < 4; b++) agcPrev[b] = gNow[b];
        lim.process(L, R, n);
        for (int i = 0; i < n; i++) {
            L[i] *= levelGain;
            R[i] *= levelGain;
        }
    }
};

}  // namespace master
}  // namespace detail
}  // namespace Audio
