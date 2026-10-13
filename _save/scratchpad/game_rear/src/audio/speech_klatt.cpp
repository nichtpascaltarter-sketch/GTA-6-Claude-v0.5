// Formant synthesis back end.
//  1) Parameter tracks: per 2.5 ms frame, F1..F3 follow a Holmes-style coarticulation model (segment targets,
//     rank-dominated boundary values from locus equations, (1-x)^2 transitions), amplitudes/source controls
//     follow keypoint envelopes (voicing onset after VOT, bursts, fricative ramps), F0 follows the anchor
//     contour plus micro-prosody.
//  2) Klatt-style cascade/parallel synthesizer: Liljencrants-Fant glottal flow (area-balanced, per period,
//     Rd voice-quality parameter, sampled as flow differences for low aliasing), jitter/shimmer/flutter,
//     diplophonia and creak, flow-modulated aspiration, nasal pole/zero pair plus place anti-resonance,
//     six cascade formants, parallel frication branch (high-passed noise into three peak-normalized
//     resonators plus bypass), DC blocking.
#include "speech_internal.h"

namespace Speech {
namespace detail {

static const float kFrameSec = 0.0025f;
static const float kWhisperGain = 0.22f;  // noise excitation level of whispered voicing (relative to glottal flow)

struct Frame {
    float f0;
    float av, ah, af;              // linear amplitudes
    float f[8], b[8];              // cascade formants F1..F8
    float nasal;                   // nasal coupling 0..1
    float nzF, nzG;                // place anti-resonance (Hz) and mix
    float ff[3], fb[3], fa[3], ab; // frication resonators (Hz, Hz, linear gain), bypass gain
    float fhp;                     // frication high-pass cutoff (Hz)
    float rd, tilt, creak;
    float breath, jit, whisper;    // speaking-style source modifiers: extra breathiness, jitter scale, whisper
};

// Keypoint envelope, evaluated with a forward-moving cursor.
struct KTrack {
    std::vector<float> t, v;
    float def = 0.f;
    size_t cur = 0;
    void add(float time, float val) {
        t.push_back(time);
        v.push_back(val);
    }
    void finalize() {
        std::vector<size_t> idx(t.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return t[a] < t[b]; });
        std::vector<float> t2(t.size()), v2(t.size());
        for (size_t i = 0; i < idx.size(); i++) t2[i] = t[idx[i]], v2[i] = v[idx[i]];
        t.swap(t2);
        v.swap(v2);
        cur = 0;
    }
    float eval(float time) {
        if (t.empty()) return def;
        if (time <= t[0]) return v[0];
        if (time >= t.back()) return v.back();
        while (cur + 1 < t.size() && t[cur + 1] <= time) cur++;
        while (cur > 0 && t[cur] > time) cur--;
        size_t n = cur + 1;
        float dt = t[n] - t[cur];
        if (dt <= 1e-7f) return v[n];
        float a = (time - t[cur]) / dt;
        return v[cur] + (v[n] - v[cur]) * a;
    }
};

static inline float dbLin(float db) { return db <= 1.f ? 0.f : powf(10.f, (db - 60.f) * 0.05f); }

// Acoustic view of a segment for the coarticulation model (targets already scaled to the voice).
struct SegAco {
    float T0[3], T1[3];
    float bw[3];
    int rank;
    float fix[3], prop[3];
    float tInt, tExt, tExtF1;  // seconds
    bool vowel, velar, diph;
};

static void velarBoundary(float fsc, const float* n, float* V) {
    bool front = n[1] > 1600.f * fsc;
    float f2 = front ? 300.f * fsc + 0.95f * n[1] : 700.f * fsc + 0.6f * n[1];
    float f3 = front ? std::max(f2 + 350.f * fsc, 900.f * fsc + 0.6f * n[2]) : 1300.f * fsc + 0.3f * n[2];
    V[0] = 150.f * fsc + 0.22f * n[0];
    V[1] = f2;
    V[2] = f3;
}

struct Bound {
    float V[3];
    float dl[3], dr[3];  // transition durations into left / right segment
};

static void computeBoundary(const SegAco& L, const SegAco& R, float fsc, Bound& b) {
    if (L.rank > R.rank) {
        if (L.vowel) {
            for (int k = 0; k < 3; k++) b.V[k] = L.T1[k], b.dl[k] = b.dr[k] = 0.f;
            return;
        }
        if (L.velar) velarBoundary(fsc, R.T0, b.V);
        else
            for (int k = 0; k < 3; k++) b.V[k] = L.fix[k] + L.prop[k] * R.T0[k];
        for (int k = 0; k < 3; k++) b.dl[k] = L.tInt, b.dr[k] = k == 0 ? L.tExtF1 : L.tExt;
    } else if (R.rank > L.rank) {
        if (R.vowel) {
            for (int k = 0; k < 3; k++) b.V[k] = R.T0[k], b.dl[k] = b.dr[k] = 0.f;
            return;
        }
        if (R.velar) velarBoundary(fsc, L.T1, b.V);
        else
            for (int k = 0; k < 3; k++) b.V[k] = R.fix[k] + R.prop[k] * L.T1[k];
        for (int k = 0; k < 3; k++) b.dr[k] = R.tInt, b.dl[k] = k == 0 ? R.tExtF1 : R.tExt;
    } else {
        for (int k = 0; k < 3; k++) {
            b.V[k] = 0.5f * (L.T1[k] + R.T0[k]);
            b.dl[k] = L.tInt;
            b.dr[k] = R.tInt;
        }
    }
}

static inline float trans(float x) {  // deviation weight: 1 at the boundary, 0 after the transition
    if (x >= 1.f) return 0.f;
    if (x <= 0.f) return 1.f;
    float y = 1.f - x;
    return y * y;
}

static bool isTransparent(int ph) { return ph == PH_SIL || ph == PH_HH || ph == PH_Q; }

struct FrameBuilder {
    const Utterance& u;
    const Audio::VoiceParams& voice;
    float fsc;
    float speed;
    std::vector<SegAco> aco;
    std::vector<Bound> bounds;  // bounds[i] = boundary before segment i; bounds[M] = end
    KTrack av, ah, af, rd, tilt, fhp;
    KTrack ff[3], fb[3], fa[3], ab;
    std::vector<std::pair<float, float>> clicks;  // release transients (time, amplitude) into the cascade

    FrameBuilder(const Utterance& uu, const Audio::VoiceParams& v) : u(uu), voice(v) {
        fsc = Clamp(v.formantScale, 0.7f, 1.5f);
        speed = Clamp(v.speed, 0.4f, 2.5f);
    }

    void setupAcoustics() {
        const std::vector<Seg>& S = u.segs;
        int M = (int)S.size();
        aco.resize(M);
        float tscale = 1.f / std::sqrt(speed);
        for (int i = 0; i < M; i++) {
            const PhInfo& p = phInfo(S[i].ph);
            SegAco& a = aco[i];
            for (int k = 0; k < 3; k++) {
                a.T0[k] = p.f[k] * fsc;
                a.T1[k] = p.fe[k] * fsc;
                a.bw[k] = p.bw[k] * std::sqrt(fsc);
                a.fix[k] = p.fix[k] * fsc;
                a.prop[k] = p.prop[k];
            }
            a.rank = p.rank;
            const float glide = 1.f + 0.5f * u.segStyle(i).slur;  // sluggish articulators when slurring
            a.tInt = p.tInt * 0.001f * tscale * glide;
            a.tExt = p.tExt * 0.001f * tscale * glide;
            a.tExtF1 = p.tExtF1 * 0.001f * tscale * glide;
            a.vowel = isVowel(S[i].ph);
            a.velar = (p.flags & PF_VELAR) != 0;
            a.diph = (p.flags & PF_DIPH) != 0;
            // Contextual vowel allophones: /u/ fronting after coronals, reduced vowels shifted to neighbours.
            if (a.vowel && i > 0) {
                int pv = S[i - 1].ph;
                if ((S[i].ph == PH_UW || S[i].ph == PH_UH) &&
                    (hasFlag(pv, PF_ALVEOLAR) || hasFlag(pv, PF_POSTALV) || pv == PH_Y)) {
                    a.T0[1] += 350.f * fsc;
                    a.T1[1] += 250.f * fsc;
                }
            }
            if (a.vowel && S[i].stress == 0 && (S[i].ph == PH_AX || S[i].ph == PH_IX)) {
                // schwa takes on some of the colour of the following consonant (lower F2 before labials etc.)
                int nx = i + 1 < M ? (int)S[i + 1].ph : (int)PH_SIL;
                if (hasFlag(nx, PF_LABIAL)) a.T0[1] = a.T1[1] = a.T0[1] - 150.f * fsc;
                if (hasFlag(nx, PF_VELAR) || hasFlag(nx, PF_POSTALV)) a.T1[1] += 100.f * fsc;
            }
            // Accent vowel qualities (absolute targets) and speaking-style formant scaling (jaw / smile).
            const Seg& sg = S[i];
            if (sg.fo[0] > 0.f) {
                for (int k = 0; k < 3; k++) {
                    a.T0[k] = sg.fo[k] * fsc;
                    a.T1[k] = sg.fo[3 + k] * fsc;
                }
                a.diph = fabsf(sg.fo[3] - sg.fo[0]) > 30.f || fabsf(sg.fo[4] - sg.fo[1]) > 60.f;
            }
            const StyleParams& P = u.segStyle(i);
            if (P.f1 != 1.f || P.f23 != 1.f || P.fscale != 1.f) {
                float g1 = P.f1 * P.fscale, g23 = P.f23 * P.fscale;
                a.T0[0] *= g1, a.T1[0] *= g1, a.fix[0] *= g1;
                for (int k = 1; k < 3; k++) a.T0[k] *= g23, a.T1[k] *= g23, a.fix[k] *= g23;
            }
        }
        // Transparent segments copy the targets of their neighbours.
        for (int i = 0; i < M; i++) {
            if (!isTransparent(S[i].ph)) continue;
            int src = -1;
            if (S[i].ph == PH_HH) {
                if (i + 1 < M && !isTransparent(S[i + 1].ph)) src = i + 1;
                else if (i > 0 && !isTransparent(S[i - 1].ph)) src = i - 1;
            } else {
                if (i > 0 && !isTransparent(S[i - 1].ph)) src = i - 1;
                else if (i + 1 < M && !isTransparent(S[i + 1].ph)) src = i + 1;
            }
            if (src >= 0) {
                for (int k = 0; k < 3; k++) {
                    aco[i].T0[k] = aco[src].T0[k];
                    aco[i].T1[k] = aco[src].T0[k];
                    if (src < i) aco[i].T0[k] = aco[i].T1[k] = aco[src].T1[k];
                }
            }
        }
        // Long pauses between phrases hold an open, neutral tract (the breath intake is shaped by it).
        for (int i = 0; i + 1 < M; i++) {
            if (S[i].ph != PH_SIL || S[i].dur < 0.3f) continue;
            static const float kBreathF[3] = {600.f, 1350.f, 2500.f};
            for (int k = 0; k < 3; k++) aco[i].T0[k] = aco[i].T1[k] = kBreathF[k] * fsc;
        }
        // Intervocalic /h/ glides between its neighbours (no formant jump that would sound like a stop release).
        for (int i = 1; i + 1 < M; i++) {
            if (S[i].ph != PH_HH) continue;
            int pv = S[i - 1].ph, nx = S[i + 1].ph;
            if ((isVowel(pv) || hasFlag(pv, PF_SONOR)) && (isVowel(nx) || hasFlag(nx, PF_SONOR)))
                for (int k = 0; k < 3; k++) aco[i].T0[k] = aco[i - 1].T1[k], aco[i].T1[k] = aco[i + 1].T0[k];
        }
        bounds.resize(M + 1);
        for (int k = 0; k < 3; k++) {
            bounds[0].V[k] = aco.empty() ? 500.f : aco[0].T0[k];
            bounds[0].dl[k] = bounds[0].dr[k] = 0.f;
            bounds[M].V[k] = aco.empty() ? 500.f : aco[M - 1].T1[k];
            bounds[M].dl[k] = bounds[M].dr[k] = 0.f;
        }
        for (int i = 1; i < M; i++) computeBoundary(aco[i - 1], aco[i], fsc, bounds[i]);
    }

    void formantsAt(int i, float t, float* F) const {
        const Seg& s = u.segs[i];
        const SegAco& a = aco[i];
        float s0 = s.t0, d = std::max(1e-4f, s.dur), s1 = s0 + d;
        float uu = (t - s0) / d;
        const Bound& L = bounds[i];
        const Bound& R = bounds[i + 1];
        for (int k = 0; k < 3; k++) {
            float ts = a.T0[k], te = a.T1[k];
            float tgt = a.diph ? Lerp(ts, te, SmoothStep(0.22f, 1.0f, uu)) : Lerp(ts, te, Saturate(uu));
            float dl = L.dr[k], dr = R.dl[k];
            float devL = dl > 0.f ? (L.V[k] - ts) * trans((t - s0) / dl) : 0.f;
            float devR = dr > 0.f ? (R.V[k] - te) * trans((s1 - t) / dr) : 0.f;
            float v = tgt + devL + devR;
            float lo = std::min(std::min(L.V[k], R.V[k]), std::min(ts, te));
            float hi = std::max(std::max(L.V[k], R.V[k]), std::max(ts, te));
            if (dl <= 0.f) lo = std::min(ts, te), hi = std::max(ts, te);
            if (dl <= 0.f && dr > 0.f) lo = std::min(lo, R.V[k]), hi = std::max(hi, R.V[k]);
            if (dl > 0.f && dr <= 0.f) {
                lo = std::min(std::min(L.V[k], ts), te);
                hi = std::max(std::max(L.V[k], ts), te);
            }
            F[k] = Clamp(v, lo, hi);
        }
    }

    // Amplitude and source envelopes.
    void buildEnvelopes() {
        const std::vector<Seg>& S = u.segs;
        int M = (int)S.size();
        const float breath = Saturate(voice.breathiness);
        float rd0 = 0.62f + 0.3f * Saturate((voice.pitch - 110.f) / 100.f) + 1.2f * breath;
        rd.def = rd0;
        tilt.def = 0.f;
        fhp.def = 1000.f;
        for (int k = 0; k < 3; k++) ff[k].def = 3000.f, fb[k].def = 1000.f, fa[k].def = 0.f;
        ab.def = 0.f;
        av.add(0.f, 0.f);
        ah.add(0.f, 0.f);
        af.add(0.f, 0.f);
        // Intensity declination: voicing falls ~2 dB across a phrase, phrase-final syllables a little more.
        std::vector<float> declDb((size_t)M, 0.f);
        for (const UPhrase& ph : u.phrases) {
            if (ph.firstSeg < 0 || ph.lastSeg < ph.firstSeg || ph.lastSeg >= M) continue;
            float p0 = S[ph.firstSeg].t0, p1 = S[ph.lastSeg].t0 + S[ph.lastSeg].dur;
            float len = std::max(0.05f, p1 - p0);
            for (int i = ph.firstSeg; i <= ph.lastSeg; i++) {
                float x = Saturate((S[i].t0 + 0.5f * S[i].dur - p0) / len);
                declDb[(size_t)i] = -2.f * x - ((S[i].flags & SF_PHRASE_FINAL) ? 1.f : 0.f);
            }
        }
        for (int i = 0; i < M; i++) {
            const Seg& s = S[i];
            const PhInfo& p = phInfo(s.ph);
            float t0 = s.t0, d = s.dur, t1 = t0 + d;
            int prev = i > 0 ? (int)S[i - 1].ph : (int)PH_SIL, next = i + 1 < M ? (int)S[i + 1].ph : (int)PH_SIL;
            bool prevVoiced = i > 0 && hasFlag(prev, PF_VOICED) && !hasFlag(prev, PF_STOP) && prev != PH_SIL;
            // voiced stop preceded by a voiced stop whose closure was voiced ("word bat"): voicing continues
            bool prevVoicedClosure = i > 1 && hasFlag(prev, PF_STOP) && hasFlag(prev, PF_VOICED) &&
                                     (isVowel(S[i - 2].ph) || hasFlag(S[i - 2].ph, PF_SONOR));
            // previous segment releases into continuing voicing (intervocalic voiced stop / affricate)
            bool prevVoicedRelease = i > 0 && (hasFlag(prev, PF_STOP) || prev == PH_JH) && hasFlag(prev, PF_VOICED) &&
                                     s.vot <= 0.f;
            bool vowel = isVowel(s.ph);
            // /h/ between voiced sounds is breathy-voiced ("a house")
            bool hVoiced = s.ph == PH_HH && i + 1 < M && (isVowel(next) || hasFlag(next, PF_SONOR)) && prevVoiced;
            // phrase position for intensity declination
            float emphDb = (s.flags & SF_EMPH) ? 3.f : 0.f;
            float shoutDb = (s.flags & SF_SHOUT) ? 4.f : 0.f;
            const StyleParams& P = u.segStyle(i);
            const float styleDb = P.loud;
            if (s.ph == PH_SIL) {
                av.add(t0 + 0.0005f, 0.f);
                av.add(t1, 0.f);
                af.add(t0 + 0.004f, 0.f);
                af.add(t1, 0.f);
                // audible breath intake in longer pauses between phrases (not at the utterance edges)
                const float bin = i + 1 < M ? u.segStyle(i + 1).breathIn : 0.f;
                if (d >= 0.3f && i + 1 < M && bin > 0.f) {
                    float L = dbLin(39.f + 10.f * breath) * std::min(2.f, bin);
                    float ta = t0 + 0.35f * d, tb = t1 - std::min(0.12f, 0.25f * d), tc = t1 - 0.04f;
                    ah.add(ta, 0.f);
                    ah.add(tb, L);
                    ah.add(tc, 0.f);
                }
                continue;
            }
            // ---- voicing
            if (vowel || hasFlag(s.ph, PF_SONOR)) {
                float db = p.av;
                if (vowel) {
                    if (s.stress == 1) db += 1.f;
                    else if (s.stress == 0) db -= (hasFlag(s.ph, PF_REDUCED) ? 3.5f : 2.f);
                } else if (s.stress == 0) db -= 1.f;
                db += emphDb + shoutDb + styleDb + declDb[(size_t)i];
                float A = dbLin(db);
                float on = t0 + s.vot;
                bool fromSilence = !prevVoiced && !prevVoicedRelease;
                float ramp = prev == PH_Q ? 0.005f : prev == PH_SIL ? 0.015f : (fromSilence ? 0.008f : 0.006f);
                if (s.vot > 0.f || fromSilence) {
                    av.add(on, 0.f);
                    av.add(on + ramp, A);
                } else {
                    bool sonPrev = hasFlag(prev, PF_NASAL) || hasFlag(prev, PF_LIQUID);
                    av.add(t0 + (sonPrev ? 0.01f : 0.004f), A);
                }
                bool nextVoiceless = !(hasFlag(next, PF_VOICED)) || next == PH_SIL;
                bool prePause = next == PH_SIL || i + 1 >= M;
                if (prePause && vowel) {
                    av.add(t0 + std::max(0.6f * d, s.vot + ramp + 0.005f), A);
                    av.add(t1 - 0.01f, A * 0.45f);
                    av.add(t1 + 0.012f, 0.f);
                } else if (nextVoiceless) {
                    float off = hasFlag(next, PF_STOP) || next == PH_CH ? 0.006f : 0.012f;
                    av.add(t1 - off, A);
                    av.add(t1 + (hasFlag(next, PF_FRIC) ? 0.006f : 0.0015f), 0.f);
                } else {
                    av.add(t1 - 0.004f, A);
                }
            } else if (hasFlag(s.ph, PF_FRIC) && hasFlag(s.ph, PF_VOICED)) {
                float A = dbLin(p.av + shoutDb + styleDb);
                av.add(t0 + 0.008f, A);
                bool nextVoicelessObs = hasFlag(next, PF_OBSTRUENT) && !hasFlag(next, PF_VOICED);
                if (nextVoicelessObs || next == PH_SIL || i + 1 >= M) {
                    // a final voiced fricative devoices progressively before a voiceless sound or a pause
                    // ("has to" [hass tu]); the preceding vowel length keeps the voicing contrast
                    av.add(t0 + 0.5f * d, A * (nextVoicelessObs ? 0.5f : 0.7f));
                    av.add(t1 - 0.004f, A * (nextVoicelessObs ? 0.1f : 0.3f));
                } else {
                    av.add(t1 - 0.008f, A * 0.9f);
                }
            } else if (s.ph == PH_JH) {
                // closure with a low-passed voice bar, then voiced frication
                float A = dbLin(p.av);
                bool vc = prevVoiced || isVowel(prev) || prevVoicedClosure;
                bool merged = i > 0 && hasFlag(prev, PF_STOP) && (S[i - 1].flags & SF_UNRELEASED);
                float tc = t0 + (merged ? 0.12f : 0.45f) * d;
                float bar = dbLin(42.f);
                av.add(t0 + 0.006f, vc ? bar : 0.f);
                av.add(std::max(t0 + 0.007f, tc - 0.004f), vc ? bar * 0.8f : bar * 0.5f);
                av.add(tc + 0.006f, A * (vc ? 0.8f : 0.6f));
                av.add(t1 - 0.006f, A * (vc ? 0.8f : 0.6f));
            } else if (hasFlag(s.ph, PF_STOP) && hasFlag(s.ph, PF_VOICED)) {
                // Voice bar: through the whole closure after voiced sounds, otherwise prevoicing in its last part.
                float burst = p.burstMs * 0.001f;
                float A = dbLin(p.av);
                float tb = t1 - burst;
                if (prevVoiced || isVowel(prev) || prevVoicedClosure) {
                    av.add(t0 + 0.006f, A);
                    av.add(std::max(t0 + 0.007f, tb - 0.004f), A * 0.7f);
                } else {
                    float pv = std::max(t0 + 0.002f, tb - std::min(0.035f, 0.6f * d));
                    av.add(t0 + 0.001f, 0.f);
                    av.add(pv, 0.f);
                    av.add(pv + 0.008f, A * 0.6f);
                    av.add(std::max(pv + 0.009f, tb - 0.003f), A * 0.6f);
                }
                bool releaseVoiced = (prevVoiced || isVowel(prev) || prevVoicedClosure) && i + 1 < M &&
                                     (isVowel(next) || hasFlag(next, PF_SONOR)) && S[i + 1].vot <= 0.f;
                if (releaseVoiced) {
                    av.add(tb + 0.001f, A * 0.6f);
                    av.add(t1, A * 0.8f);
                } else {
                    av.add(tb + 0.001f, A * 0.3f);
                    av.add(t1, 0.f);
                }
            } else if (s.ph == PH_DX) {
                float A = dbLin(p.av);
                av.add(t0 + 0.35f * d, A);
                av.add(t1 - 0.35f * d, A);
            } else if (s.ph == PH_HH) {
                if (hVoiced) {
                    av.add(t0 + 0.3f * d, dbLin(33.f));
                    av.add(t1 - 0.3f * d, dbLin(33.f));
                } else {
                    av.add(t0 + 0.004f, 0.f);
                    av.add(t1 - 0.001f, 0.f);
                }
            } else {
                // voiceless obstruents, glottal stop
                av.add(t0 + 0.002f, 0.f);
                av.add(t1, 0.f);
            }

            // ---- aspiration
            if (s.ph == PH_HH) {
                float L = dbLin(p.ah + ((s.flags & SF_STRESSED) ? 1.f : -2.f) + shoutDb + 0.5f * styleDb);
                ah.add(t0, 0.f);
                ah.add(t0 + std::min(0.015f, 0.3f * d), L);
                ah.add(t1 - 0.004f, L * 0.8f);
                ah.add(t1 + 0.012f, 0.f);
            }
            if (s.vot > 0.f && i > 0 && hasFlag(prev, PF_STOP) && !hasFlag(prev, PF_VOICED)) {
                bool asp = (S[i - 1].flags & SF_ASPIRATED) != 0;
                float L0 = dbLin(asp ? 51.f : 45.f), L1 = dbLin(asp ? 47.f : 40.f);
                ah.add(t0 - 0.0005f, 0.f);
                ah.add(t0 + 0.0015f, L0);
                ah.add(t0 + s.vot * 0.85f, L1);
                ah.add(t0 + s.vot + 0.006f, 0.f);
            }
            if (vowel && (next == PH_SIL || i + 1 >= M)) {
                // breathy phrase-final offset
                ah.add(t1 - 0.03f, 0.f);
                ah.add(t1, dbLin(40.f + 8.f * breath));
                ah.add(t1 + 0.035f, 0.f);
            }

            // ---- frication / bursts
            // slurred speech smears /s z/ toward /sh/
            const float sRetract = (s.ph == PH_S || s.ph == PH_Z) ? 1.f - 0.25f * P.slur : 1.f;
            auto setSpectrum = [&](float ta, float tb, const PhInfo& q, float velarF) {
                for (int k = 0; k < 3; k++) {
                    float f = q.fricF[k] * fsc * sRetract;
                    if (k == 0 && velarF > 0.f) f = velarF;
                    float g = q.fricA[k] <= -90.f ? 0.f : powf(10.f, q.fricA[k] * 0.05f);
                    ff[k].add(ta, f);
                    ff[k].add(tb, f);
                    fb[k].add(ta, q.fricB[k] * fsc);
                    fb[k].add(tb, q.fricB[k] * fsc);
                    fa[k].add(ta, g);
                    fa[k].add(tb, g);
                }
                float g = q.fricByp <= -90.f ? 0.f : powf(10.f, q.fricByp * 0.05f);
                ab.add(ta, g);
                ab.add(tb, g);
                float hp = 300.f;
                if (hasFlag(s.ph, PF_SIBILANT)) hp = (s.ph == PH_S || s.ph == PH_Z) ? 3000.f * fsc * sRetract : 1500.f * fsc;
                else if (s.ph == PH_F || s.ph == PH_V) hp = 900.f * fsc;
                else if (s.ph == PH_TH || s.ph == PH_DH) hp = 1400.f * fsc;
                else if (s.ph == PH_T || s.ph == PH_D) hp = 2200.f * fsc;
                else if (s.ph == PH_K || s.ph == PH_G) hp = 900.f * fsc;
                else hp = 250.f;
                fhp.add(ta, hp);
                fhp.add(tb, hp);
            };
            if (hasFlag(s.ph, PF_FRIC)) {
                float db = p.af + ((s.flags & SF_STRESSED) ? 1.f : -1.f) + (shoutDb + styleDb) * 0.5f - 4.f * P.slur;
                // a voiced fricative devoicing before a voiceless consonant gets the stronger voiceless frication
                if (hasFlag(s.ph, PF_VOICED) && hasFlag(next, PF_OBSTRUENT) && !hasFlag(next, PF_VOICED)) db += 3.f;
                float L = dbLin(db);
                bool sib = hasFlag(s.ph, PF_SIBILANT);
                float rin = std::min(sib ? 0.022f : 0.012f, 0.35f * d), rout = std::min(0.014f, 0.3f * d);
                float pre = (i > 0 && (isVowel(prev) || hasFlag(prev, PF_SONOR))) ? 0.008f : 0.f;
                af.add(t0 - pre, 0.f);
                af.add(t0 + rin, L);
                af.add(t1 - rout, L);
                af.add(t1 + 0.002f, 0.f);
                setSpectrum(t0, t1, p, 0.f);
            } else if (hasFlag(s.ph, PF_AFFR)) {
                bool mergedClosure = i > 0 && hasFlag(prev, PF_STOP) && (S[i - 1].flags & SF_UNRELEASED);
                float tc = t0 + (mergedClosure ? 0.12f : 0.45f) * d;
                float L = dbLin(p.af + (shoutDb + styleDb) * 0.5f - 4.f * P.slur);
                af.add(t0, 0.f);
                af.add(tc - 0.0005f, 0.f);
                af.add(tc + 0.001f, L * 1.3f);
                af.add(tc + 0.012f, L);
                af.add(t1 - 0.01f, L);
                af.add(t1 + 0.002f, 0.f);
                setSpectrum(t0, t1, p, 0.f);
            } else if (hasFlag(s.ph, PF_STOP)) {
                float burst = std::max(0.004f, p.burstMs * 0.001f / std::sqrt(speed));
                // /tr/, /dr/ are affricated (retracted, longer, lower-frequency release)
                bool affricated = (s.ph == PH_T || s.ph == PH_D) && next == PH_R;
                if (affricated) burst = std::max(burst, 0.016f / std::sqrt(speed));
                burst = std::min(burst, 0.6f * d);
                float tb = t1 - burst;
                float db = p.burstAf + (shoutDb + styleDb) * 0.5f - 8.f * P.slur;
                if (s.flags & (SF_UNRELEASED | SF_WEAKREL)) {
                    // weak release only when the next consonant has a different place (keeps the place cue)
                    u32 placeMask = PF_LABIAL | PF_ALVEOLAR | PF_VELAR | PF_POSTALV;
                    bool homorganic = (phInfo(next).flags & placeMask) == (p.flags & placeMask);
                    db = homorganic ? 0.f : db - ((s.flags & SF_UNRELEASED) ? 16.f : 14.f);
                }
                if (!(s.flags & SF_STRESSED) && !(s.flags & SF_PREPAUSE)) db -= 2.f;
                float L = dbLin(db);
                af.add(t0, 0.f);
                af.add(tb - 0.0003f, 0.f);
                af.add(tb + 0.0008f, L);
                af.add(t1 - 0.0005f, L * 0.45f);
                af.add(t1 + 0.0025f, 0.f);
                float velarF = 0.f;
                if (hasFlag(s.ph, PF_VELAR)) {
                    // compact burst near F2/F3 of the neighbouring vowel
                    const Bound& bd = bounds[i + 1];
                    velarF = bd.V[1] > 1700.f * fsc ? 0.5f * (bd.V[1] + bd.V[2]) : bd.V[1] * 1.05f;
                }
                setSpectrum(t0, t1, affricated ? phInfo(PH_CH) : p, velarF);
                if (!(s.flags & (SF_UNRELEASED | SF_WEAKREL))) {
                    // Release transient: an impulse exciting the whole vocal tract (dominant cue for labials).
                    float amp = hasFlag(s.ph, PF_LABIAL) ? 1.0f : hasFlag(s.ph, PF_VELAR) ? 0.2f : 0.25f;
                    amp *= hasFlag(s.ph, PF_VOICED) ? 0.6f : 1.3f;
                    clicks.push_back(std::make_pair(tb, amp));
                }
                if ((s.flags & SF_PREPAUSE) && !hasFlag(s.ph, PF_VOICED)) {
                    // audible release of a final voiceless stop
                    ah.add(t1 - 0.0005f, 0.f);
                    ah.add(t1 + 0.001f, dbLin(48.f));
                    ah.add(t1 + 0.035f, 0.f);
                }
            } else {
                af.add(t0 + 0.002f, 0.f);
                af.add(std::max(t0 + 0.003f, t1 - 0.012f), 0.f);
            }

            // ---- source quality
            float r = rd0;
            float tl = 0.f;
            if (vowel) {
                if (s.stress == 1) r -= 0.1f;
                else if (s.stress == 0) r += 0.12f;
                if (s.flags & SF_PHRASE_FINAL) r += 0.25f;
                if (s.flags & SF_EMPH) r -= 0.15f;
            } else if (hasFlag(s.ph, PF_NASAL)) {
                r += 0.1f;
                tl = 2.f;
            } else if (hasFlag(s.ph, PF_AFFR)) {
                r += 0.4f;
                tilt.add(t0 + 0.2f * d, 16.f);
                tilt.add(t0 + 0.42f * d, 16.f);
                tl = 6.f;
            } else if (hasFlag(s.ph, PF_FRIC)) {
                r += 0.3f;
                tl = 6.f;
            } else if (hasFlag(s.ph, PF_STOP)) {
                r += 0.5f;
                tl = 16.f;
            } else if (s.ph == PH_DX) {
                tl = 4.f;
            } else if (hasFlag(s.ph, PF_SONOR)) {
                r += 0.05f;
            }
            if (s.flags & SF_SHOUT) r -= 0.3f;
            r += P.rd + 0.6f * P.breath;
            tl += P.tilt;
            rd.add(t0 + 0.5f * d, Clamp(r, 0.35f, 2.6f));
            tilt.add(t0 + 0.25f * d, tl);
            tilt.add(t1 - 0.25f * d, tl);
        }
        av.finalize();
        ah.finalize();
        af.finalize();
        rd.finalize();
        tilt.finalize();
        fhp.finalize();
        for (int k = 0; k < 3; k++) ff[k].finalize(), fb[k].finalize(), fa[k].finalize();
        ab.finalize();
    }
};

// ---------------------------------------------------------------------------------------------
// Frame generation

static void buildFrames(const Utterance& u, const std::vector<F0Point>& f0pts, const Audio::VoiceParams& voice,
                        std::vector<Frame>& frames, std::vector<std::pair<float, float>>& clicks) {
    FrameBuilder fb(u, voice);
    fb.setupAcoustics();
    fb.buildEnvelopes();
    const std::vector<Seg>& S = u.segs;
    const int M = (int)S.size();
    const float fsc = fb.fsc;
    const float total = u.total + 0.02f;
    int nF = (int)(total / kFrameSec) + 2;
    frames.resize(nF);
    const float rough = Saturate(voice.roughness);
    int seg = 0;
    size_t fcur = 0;
    for (int fi = 0; fi < nF; fi++) {
        float t = fi * kFrameSec;
        while (seg + 1 < M && S[seg + 1].t0 <= t) seg++;
        Frame& F = frames[fi];
        const Seg& s = S[seg];
        const SegAco& a = fb.aco[seg];
        const StyleParams& P = u.segStyle(seg);
        float Fm[3];
        fb.formantsAt(seg, t, Fm);
        float s0 = s.t0, s1 = s.t0 + s.dur;
        for (int k = 0; k < 3; k++) {
            F.f[k] = Fm[k];
            float bwv = a.bw[k];
            // blend bandwidths across boundaries (10 ms)
            if (seg > 0 && t - s0 < 0.01f) bwv = Lerp(0.5f * (fb.aco[seg - 1].bw[k] + bwv), bwv, (t - s0) / 0.01f);
            if (seg + 1 < M && s1 - t < 0.01f) bwv = Lerp(0.5f * (fb.aco[seg + 1].bw[k] + bwv), bwv, (s1 - t) / 0.01f);
            F.b[k] = bwv;
        }
        // open glottis during aspiration widens F1
        if (s.ph == PH_HH || s.ph == PH_SIL || (s.vot > 0.f && t < s0 + s.vot)) {
            F.b[0] = std::max(F.b[0], 260.f);
            F.b[1] += 60.f;
        }
        // Higher formants (fixed per voice): F4..F8 approximate the vocal tract's higher poles, which keeps
        // realistic energy above 4 kHz at high sample rates.
        bool rhotic = hasFlag(s.ph, PF_RHOTIC) && s.fo[0] <= 0.f;
        static const float kHiF[5] = {3350.f, 4250.f, 5150.f, 6100.f, 7100.f};
        static const float kHiB[5] = {250.f, 320.f, 400.f, 500.f, 650.f};
        for (int k = 0; k < 5; k++) {
            F.f[3 + k] = kHiF[k] * fsc;
            F.b[3 + k] = kHiB[k] * std::sqrt(fsc);
        }
        if (rhotic) F.f[3] = 3050.f * fsc;
        // keep formants ordered and apart
        for (int k = 1; k < 8; k++) F.f[k] = std::max(F.f[k], F.f[k - 1] + 150.f);

        // nasal coupling (analytic)
        float nas = 0.f, place = 0.f, placeF = 0.f;
        if (hasFlag(s.ph, PF_NASAL)) {
            nas = 1.f;
            float e = std::min(t - s0, s1 - t);
            place = Saturate(e / 0.008f);
            placeF = phInfo(s.ph).nasalZero * fsc;
        } else if (hasFlag(s.ph, PF_LIQUID) && phInfo(s.ph).nasalZero > 0.f) {
            float e = std::min(t - s0, s1 - t);
            place = 0.75f * Saturate(e / 0.012f);  // lateral anti-resonance
            placeF = phInfo(s.ph).nasalZero * fsc;
        } else if (isVowel(s.ph)) {
            if (seg + 1 < M && hasFlag(S[seg + 1].ph, PF_NASAL)) {
                float w = std::min(0.07f, 0.45f * s.dur);
                nas = std::max(nas, 0.45f * Saturate((t - (s1 - w)) / w));
            }
            if (seg > 0 && hasFlag(S[seg - 1].ph, PF_NASAL)) {
                float w = std::min(0.03f, 0.3f * s.dur);
                nas = std::max(nas, 0.35f * (1.f - Saturate((t - s0) / w)));
            }
        }
        if (P.nasal > 0.f && (isVowel(s.ph) || hasFlag(s.ph, PF_LIQUID) || hasFlag(s.ph, PF_GLIDE)))
            nas = std::max(nas, P.nasal);  // nasal twang
        F.nasal = nas;
        F.nzG = place;
        F.nzF = placeF > 0.f ? placeF : 1500.f;

        F.av = fb.av.eval(t);
        F.ah = fb.ah.eval(t);
        F.af = fb.af.eval(t);
        for (int k = 0; k < 3; k++) {
            F.ff[k] = fb.ff[k].eval(t);
            F.fb[k] = fb.fb[k].eval(t);
            F.fa[k] = fb.fa[k].eval(t);
        }
        F.ab = fb.ab.eval(t);
        F.fhp = fb.fhp.eval(t);
        F.rd = fb.rd.eval(t);
        F.tilt = fb.tilt.eval(t);
        F.breath = P.breath;
        F.jit = P.jitter;
        F.whisper = P.whisper;
        if (P.whisper > 0.f) F.b[0] += 80.f * P.whisper;  // whispered vowels: open glottis damps F1

        // F0: anchors + micro-prosody
        float st = 0.f;
        if (!f0pts.empty()) {
            while (fcur + 1 < f0pts.size() && f0pts[fcur + 1].t <= t) fcur++;
            if (t <= f0pts[0].t) st = f0pts[0].st;
            else if (fcur + 1 >= f0pts.size()) st = f0pts.back().st;
            else {
                const F0Point& p0 = f0pts[fcur];
                const F0Point& p1 = f0pts[fcur + 1];
                float x = (t - p0.t) / std::max(1e-4f, p1.t - p0.t);
                st = Lerp(p0.st, p1.st, SmoothStep(0.f, 1.f, x) * 0.5f + x * 0.5f);
            }
        }
        if (P.tremor > 0.f)  // fearful voice tremor (~5.5 Hz, slowly varying depth)
            st += P.tremor * sinf(kTwoPi * 5.5f * t) * (0.75f + 0.25f * sinf(kTwoPi * 0.9f * t));
        if (isVowel(s.ph)) {
            int pv = seg > 0 ? (int)S[seg - 1].ph : (int)PH_SIL;
            float on = s0 + s.vot;
            if (t >= on) {
                if (hasFlag(pv, PF_OBSTRUENT) && !hasFlag(pv, PF_VOICED)) st += 1.3f * expf(-(t - on) / 0.03f);
                else if (hasFlag(pv, PF_OBSTRUENT) && hasFlag(pv, PF_VOICED)) st -= 0.8f * expf(-(t - on) / 0.03f);
            }
            if (hasFlag(s.ph, PF_HIGH)) st += 0.4f;
            if (hasFlag(s.ph, PF_LOW)) st -= 0.3f;
        } else if (hasFlag(s.ph, PF_OBSTRUENT) && hasFlag(s.ph, PF_VOICED)) {
            st -= 1.0f;
        }
        F.f0 = Clamp(voice.pitch * powf(2.f, st / 12.f), 40.f, 650.f);

        // creak toward the end of falling phrases, stronger with roughness; creaky onset after a glottal stop
        float creak = rough * 0.12f + 0.5f * P.creak;
        if (isVowel(s.ph) && seg > 0 && (S[seg - 1].ph == PH_Q || S[seg - 1].ph == PH_SIL))
            creak += 0.9f * (1.f - Saturate((t - s0) / 0.035f));
        // glottalized onset of a vowel-initial function word after a consonant ("in a") marks the word boundary
        if (isVowel(s.ph) && (s.flags & SF_WORD_START) && (s.flags & SF_FUNCTION) && seg > 0 &&
            !isVowel(S[seg - 1].ph) && S[seg - 1].ph != PH_SIL)
            creak += 0.8f * (1.f - Saturate((t - s0) / 0.03f));
        if ((s.flags & SF_PHRASE_FINAL) && (s.flags & SF_PREPAUSE || (seg + 1 < M && S[seg + 1].ph == PH_SIL))) {
            float x = Saturate((t - s0) / std::max(0.02f, s.dur));
            creak += (0.15f + 0.75f * rough + P.creak) * x * x;
        }
        F.creak = Saturate(creak);
    }
    clicks = fb.clicks;
    std::sort(clicks.begin(), clicks.end());
    // Light smoothing of F0 and formant tracks (binomial 1-2-1, two passes).
    for (int pass = 0; pass < 2; pass++) {
        float prevF0 = frames[0].f0, prevF[3] = {frames[0].f[0], frames[0].f[1], frames[0].f[2]};
        for (int fi = 1; fi + 1 < nF; fi++) {
            float c0 = frames[fi].f0;
            frames[fi].f0 = 0.25f * prevF0 + 0.5f * c0 + 0.25f * frames[fi + 1].f0;
            prevF0 = c0;
            for (int k = 0; k < 3; k++) {
                float c = frames[fi].f[k];
                frames[fi].f[k] = 0.25f * prevF[k] + 0.5f * c + 0.25f * frames[fi + 1].f[k];
                prevF[k] = c;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Synthesizer

struct Reso {
    float a = 0.f, b = 0.f, c = 0.f, y1 = 0.f, y2 = 0.f;
    void set(float f, float bw, float sr) {  // unity DC gain (cascade)
        float r = expf(-kPi * bw / sr);
        c = -r * r;
        b = 2.f * r * cosf(kTwoPi * f / sr);
        a = 1.f - b - c;
    }
    void setPeak(float f, float bw, float sr) {  // unity gain at the resonance (parallel)
        float r = expf(-kPi * bw / sr);
        float th = kTwoPi * f / sr;
        c = -r * r;
        b = 2.f * r * cosf(th);
        a = (1.f - r) * sqrtf(1.f - 2.f * r * cosf(2.f * th) + r * r);
    }
    FORCEINLINE float tick(float x) {
        float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct AntiReso {
    float a = 1.f, b = 0.f, c = 0.f, x1 = 0.f, x2 = 0.f;
    void set(float f, float bw, float sr) {
        float r = expf(-kPi * bw / sr);
        float cc = -r * r;
        float bb = 2.f * r * cosf(kTwoPi * f / sr);
        float aa = 1.f - bb - cc;
        a = 1.f / aa;
        b = -bb / aa;
        c = -cc / aa;
    }
    FORCEINLINE float tick(float x) {
        float y = a * x + b * x1 + c * x2;
        x2 = x1;
        x1 = x;
        return y;
    }
};

// Second-order Butterworth high-pass / low-pass (RBJ biquads).
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void highpass(float f, float sr) {
        float w = kTwoPi * Clamp(f, 10.f, 0.45f * sr) / sr, cw = cosf(w), sw = sinf(w);
        float alpha = sw / (2.f * 0.7071f), a0 = 1.f + alpha;
        b0 = (1.f + cw) * 0.5f / a0;
        b1 = -(1.f + cw) / a0;
        b2 = b0;
        a1 = -2.f * cw / a0;
        a2 = (1.f - alpha) / a0;
    }
    void lowpass(float f, float sr) {
        float w = kTwoPi * Clamp(f, 10.f, 0.45f * sr) / sr, cw = cosf(w), sw = sinf(w);
        float alpha = sw / (2.f * 0.7071f), a0 = 1.f + alpha;
        b0 = (1.f - cw) * 0.5f / a0;
        b1 = (1.f - cw) / a0;
        b2 = b0;
        a1 = -2.f * cw / a0;
        a2 = (1.f - alpha) / a0;
    }
    FORCEINLINE float tick(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Dimensionless LF pulse shape for one Rd value (T0 = 1, Ee = 1).
struct LFShape {
    double tp, te, ta, eps, alpha, wg, E0, Ue, Upeak, exr, invEpsTa, invA2W2;
};

static double lfUopen(const LFShape& s, double t) {
    double e = exp(s.alpha * t);
    return s.E0 / (s.alpha * s.alpha + s.wg * s.wg) * (e * (s.alpha * sin(s.wg * t) - s.wg * cos(s.wg * t)) + s.wg);
}

static LFShape lfCompute(double rd) {
    LFShape s;
    rd = rd < 0.3 ? 0.3 : (rd > 2.7 ? 2.7 : rd);
    double Rap = (-1.0 + 4.8 * rd) / 100.0;
    double Rk = (22.4 + 11.8 * rd) / 100.0;
    double Rg = Rk / (4.0 * (0.11 * rd / (0.5 + 1.2 * Rk) - Rap));
    s.tp = 1.0 / (2.0 * Rg);
    s.te = s.tp * (1.0 + Rk);
    if (s.te > 0.95) s.te = 0.95;
    // Return phase: Fant's regression (Ra = Rap) gives a steep extra tilt for modal voices (Fa ~ 600 Hz); a
    // shorter return phase (Fa ~ 1.5 kHz for modal, growing with Rd) matches natural long-term spectra better.
    s.ta = Rap * (0.35 + 0.25 * (rd - 0.3) / 2.4);
    if (s.ta < 0.002) s.ta = 0.002;
    if (s.ta > 0.5 * (1.0 - s.te)) s.ta = 0.5 * (1.0 - s.te);
    s.wg = kPi / s.tp;
    double tr = 1.0 - s.te;
    double eps = 1.0 / s.ta;
    for (int it = 0; it < 30; it++) {
        double ex = exp(-eps * tr);
        double f = eps * s.ta - 1.0 + ex;
        double fp = s.ta - tr * ex;
        if (fabs(fp) < 1e-12) break;
        double ne = eps - f / fp;
        if (ne <= 0) ne = eps * 0.5;
        if (fabs(ne - eps) < 1e-10 * eps) {
            eps = ne;
            break;
        }
        eps = ne;
    }
    s.eps = eps;
    double exr = exp(-eps * tr);
    double A2 = -(1.0 / (eps * s.ta)) * ((1.0 - exr) / eps - tr * exr);
    double swte = sin(s.wg * s.te), cwte = cos(s.wg * s.te);
    auto area = [&](double a) {
        double ea = exp(a * s.te);
        double E0 = -1.0 / (ea * swte);
        return E0 * (ea * (a * swte - s.wg * cwte) + s.wg) / (a * a + s.wg * s.wg) + A2;
    };
    double lo = -20.0, hi = 60.0;
    double flo = area(lo), fhi = area(hi);
    for (int k = 0; k < 20 && flo * fhi > 0; k++) {
        lo -= 20.0;
        hi += 40.0;
        flo = area(lo);
        fhi = area(hi);
    }
    double a = 0.0;
    if (flo * fhi <= 0) {
        for (int it = 0; it < 60; it++) {
            double mid = 0.5 * (lo + hi);
            double fm = area(mid);
            if ((fm > 0) == (flo > 0)) lo = mid, flo = fm;
            else hi = mid;
        }
        a = 0.5 * (lo + hi);
    }
    s.alpha = a;
    s.E0 = -1.0 / (exp(a * s.te) * swte);
    s.Ue = lfUopen(s, s.te);
    s.Upeak = lfUopen(s, s.tp);
    s.exr = exp(-s.eps * (1.0 - s.te));
    s.invEpsTa = 1.0 / (s.eps * s.ta);
    s.invA2W2 = 1.0 / (s.alpha * s.alpha + s.wg * s.wg);
    if (s.Upeak <= 1e-9) s.Upeak = 1e-9;
    return s;
}

struct LFTable {
    LFShape e[121];  // Rd 0.3 .. 2.7 step 0.02
};
static LFTable buildLFTable() {
    LFTable t;
    for (int i = 0; i < 121; i++) t.e[i] = lfCompute(0.3 + 0.02 * i);
    return t;
}
static const LFShape& lfShape(float rd) {
    static const LFTable table = buildLFTable();  // thread-safe one-time init
    int i = (int)((rd - 0.3f) / 0.02f + 0.5f);
    i = i < 0 ? 0 : (i > 120 ? 120 : i);
    return table.e[i];
}

struct NoiseGen {
    u32 s;
    explicit NoiseGen(u32 seed) : s(seed ? seed : 0x9e3779b9u) {}
    FORCEINLINE float uni() {  // [-1, 1)
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(i32)s * (1.f / 2147483648.f);
    }
    FORCEINLINE float gaussish() { return (uni() + uni() + uni()) * 0.577f; }
};

// Glottal source: LF flow sampled at the output rate, differentiated by first difference.
struct Glottis {
    const LFShape* shape = nullptr;
    double T0 = 0.008;       // current period (s)
    double tau = 0.0;        // dimensionless time within period [0,1)
    double dtau = 0.0;
    double zr = 1, zi = 0, mr = 1, mi = 0;  // e^{(a+iw) tau} and per-sample multiplier
    double q = 1, qm = 1;                   // return-phase exponential
    bool inReturn = false;
    double Uprev = 0.0;
    float amp = 1.f;         // per-period amplitude (shimmer, diplophonia, creak)
    float flowNorm = 0.f;    // current flow / peak flow (for noise modulation)
    int periodIndex = 0;

    void startPeriod(double newT0, const LFShape* sh, double sr) {
        shape = sh;
        T0 = newT0;
        dtau = 1.0 / (T0 * sr);
        const LFShape& s = *shape;
        double er = exp(s.alpha * tau);
        zr = er * cos(s.wg * tau);
        zi = er * sin(s.wg * tau);
        double em = exp(s.alpha * dtau);
        mr = em * cos(s.wg * dtau);
        mi = em * sin(s.wg * dtau);
        inReturn = tau >= s.te;
        if (inReturn) q = exp(-s.eps * (tau - s.te));
        qm = exp(-s.eps * dtau);
        periodIndex++;
    }
};

void render(const Utterance& u, const std::vector<F0Point>& f0pts, const Audio::VoiceParams& voice, int sampleRate,
            u32 seed, std::vector<float>& out) {
    if (u.segs.empty() || u.total <= 0.f) return;
    const float sr = (float)Clamp(sampleRate, 8000, 96000);
    std::vector<Frame> frames;
    std::vector<std::pair<float, float>> clicks;
    buildFrames(u, f0pts, voice, frames, clicks);
    size_t clickIdx = 0;
    const int pulseLen = std::max(2, (int)(0.00025f * sr + 0.5f));  // 0.25 ms half-sine release transient
    int pulsePos = -1;
    float pulseAmp = 0.f;
    const int nF = (int)frames.size();
    const int nS = (int)((nF - 1) * kFrameSec * sr);
    if (nS <= 0) return;
    const size_t base = out.size();
    out.resize(base + (size_t)nS, 0.f);
    float* dst = out.data() + base;

    const float rough = Saturate(voice.roughness);
    const float breath = Saturate(voice.breathiness);
    const float fsc = Clamp(voice.formantScale, 0.7f, 1.5f);
    const int sub = std::max(8, (int)(sr / 2000.f));  // coefficient update interval (~0.5 ms)
    int nCasc = 3;  // number of active cascade formants: all below 0.45 * sr
    {
        static const float kHiF[5] = {3350.f, 4250.f, 5150.f, 6100.f, 7100.f};
        for (int k = 0; k < 5; k++)
            if (kHiF[k] * fsc < 0.45f * sr) nCasc = 4 + k;
    }

    NoiseGen nAsp(hashCombine(seed, 0x1234567u)), nFric(hashCombine(seed, 0x89abcdefu)), nRand(hashCombine(seed, 0x2468aceu));
    Reso casc[8], rnp, fr[3];
    AntiReso rnz, rnz2;
    Biquad fricHP, fricLP, aspHP;
    fricLP.lowpass(std::min(10500.f, 0.45f * sr), sr);
    aspHP.highpass(300.f, sr);
    const bool lpNoise = sr > 24000.f;
    float tiltState = 0.f;
    float dcX = 0.f, dcY = 0.f;
    const float dcR = 1.f - kTwoPi * 40.f / sr;

    // flutter phases
    const float fl1 = hashToFloat(hashCombine(seed, 11u)) * kTwoPi, fl2 = hashToFloat(hashCombine(seed, 12u)) * kTwoPi,
                fl3 = hashToFloat(hashCombine(seed, 13u)) * kTwoPi;
    const float flutterAmt = 0.005f + 0.012f * rough;
    const float jitterAmt = 0.004f + 0.028f * rough;
    const float shimmerAmt = 0.03f + 0.2f * rough;
    const float breathMix = 0.035f + 0.3f * breath;

    Glottis g;
    g.tau = 0.0;
    double Uprev = 0.0;
    bool started = false;
    const double dt = 1.0 / sr;

    // per-block interpolated amplitude values
    float avA = 0, avB = 0, ahA = 0, ahB = 0, afA = 0, afB = 0, gzA = 0, gzB = 0;
    float faA[3] = {0, 0, 0}, faB[3] = {0, 0, 0}, abA = 0, abB = 0;
    float tiltA = 0.f;
    float boostG = 0.f, boostState = 0.f;
    // whisper excitation: white noise plus a low-passed (~800 Hz) part so nasals and F1 stay audible
    const float whA = 1.f - expf(-kTwoPi * 800.f / sr);
    const float whNorm = 1.f / std::sqrt(whA / (2.f - whA));
    float whLP = 0.f;
    const float boostA = expf(-kTwoPi * 1800.f / sr);  // one-pole low-pass for the high-shelf boost

    auto interp = [&](float t, Frame& o) {
        float x = t / kFrameSec;
        int k = (int)x;
        if (k >= nF - 1) {
            o = frames[nF - 1];
            return;
        }
        float a = x - (float)k;
        const Frame& A = frames[k];
        const Frame& B = frames[k + 1];
        o.f0 = Lerp(A.f0, B.f0, a);
        o.av = Lerp(A.av, B.av, a);
        o.ah = Lerp(A.ah, B.ah, a);
        o.af = Lerp(A.af, B.af, a);
        for (int i = 0; i < 8; i++) o.f[i] = Lerp(A.f[i], B.f[i], a), o.b[i] = Lerp(A.b[i], B.b[i], a);
        o.nasal = Lerp(A.nasal, B.nasal, a);
        o.nzF = Lerp(A.nzF, B.nzF, a);
        o.nzG = Lerp(A.nzG, B.nzG, a);
        for (int i = 0; i < 3; i++) {
            o.ff[i] = Lerp(A.ff[i], B.ff[i], a);
            o.fb[i] = Lerp(A.fb[i], B.fb[i], a);
            o.fa[i] = Lerp(A.fa[i], B.fa[i], a);
        }
        o.ab = Lerp(A.ab, B.ab, a);
        o.fhp = Lerp(A.fhp, B.fhp, a);
        o.rd = Lerp(A.rd, B.rd, a);
        o.tilt = Lerp(A.tilt, B.tilt, a);
        o.creak = Lerp(A.creak, B.creak, a);
        o.breath = Lerp(A.breath, B.breath, a);
        o.jit = Lerp(A.jit, B.jit, a);
        o.whisper = Lerp(A.whisper, B.whisper, a);
    };

    Frame cur, nxt;
    interp(0.f, cur);
    float curRd = cur.rd, curCreak = 0.f;
    const float nyq = 0.47f * sr;
    for (int n0 = 0; n0 < nS; n0 += sub) {
        int n1 = std::min(nS, n0 + sub);
        float tA = n0 / sr, tB = n1 / sr, tM = 0.5f * (tA + tB);
        Frame fm;
        interp(tM, fm);
        interp(tA, cur);
        interp(tB, nxt);
        avA = cur.av, avB = nxt.av, ahA = cur.ah, ahB = nxt.ah, afA = cur.af, afB = nxt.af;
        gzA = cur.nzG, gzB = nxt.nzG, abA = cur.ab, abB = nxt.ab;
        for (int i = 0; i < 3; i++) faA[i] = cur.fa[i], faB[i] = nxt.fa[i];
        // filter coefficients at block centre
        for (int i = 0; i < nCasc; i++) casc[i].set(std::min(fm.f[i], nyq), fm.b[i], sr);
        float fnp = 270.f * std::sqrt(fsc);
        rnp.set(fnp, 100.f, sr);
        rnz.set(fnp + 230.f * fm.nasal * std::sqrt(fsc), 100.f + 40.f * fm.nasal, sr);
        rnz2.set(std::min(fm.nzF, nyq), 300.f * std::sqrt(fsc), sr);
        bool fricActive = (afA > 1e-5f || afB > 1e-5f);
        if (fricActive) {
            for (int i = 0; i < 3; i++) fr[i].setPeak(std::min(fm.ff[i], nyq), fm.fb[i], sr);
            fricHP.highpass(fm.fhp, sr);
        }
        // spectral tilt low-pass coefficient: attenuation "tilt" dB at 3 kHz; negative tilt = high-frequency boost
        boostG = fm.tilt < -0.1f ? powf(10.f, -fm.tilt / 20.f) - 1.f : 0.f;
        {
            float tl = fm.tilt;
            if (tl < 0.1f) tiltA = 0.f;
            else {
                float gg = powf(10.f, -tl / 20.f), w = kTwoPi * 3000.f / sr, cw = cosf(w);
                float g2 = gg * gg, A = 1.f - g2 * cw, B = 1.f - g2;
                float disc = A * A - B * B;
                tiltA = B > 1e-6f ? (A - sqrtf(std::max(0.f, disc))) / B : 0.f;
                tiltA = Clamp(tiltA, 0.f, 0.99f);
            }
        }
        curRd = fm.rd;
        curCreak = fm.creak;
        const float jitScale = fm.jit;
        const float breathMixF = breathMix + 0.3f * fm.breath;
        const float wsp = Saturate(fm.whisper);
        const float invLen = 1.f / (float)(n1 - n0);
        for (int n = n0; n < n1; n++) {
            float w = (float)(n - n0) * invLen;
            float av = avA + (avB - avA) * w;
            float ahv = ahA + (ahB - ahA) * w;
            float afv = afA + (afB - afA) * w;
            // ---- glottal source
            if (!started || g.tau >= 1.0) {
                double t = n * dt;
                float f0 = fm.f0;
                float T = (float)t;
                float flut = 1.f + flutterAmt * (sinf(kTwoPi * 12.7f * T + fl1) + sinf(kTwoPi * 7.1f * T + fl2) +
                                                 sinf(kTwoPi * 4.7f * T + fl3)) * (1.f / 3.f);
                float jit = 1.f + jitterAmt * jitScale * nRand.gaussish();
                double T0 = 1.0 / (double)(f0 * flut * jit);
                float amp = 1.f + shimmerAmt * jitScale * nRand.gaussish();
                float rdv = curRd;
                if (rough > 0.25f && (g.periodIndex & 1)) {
                    amp *= 1.f - 0.35f * (rough - 0.25f);
                    T0 *= 1.0 + 0.05 * (rough - 0.25f);
                }
                if (curCreak > 0.f && nRand.uni() * 0.5f + 0.5f < curCreak * 0.55f) {
                    T0 *= 1.4 + 0.8 * (nRand.uni() * 0.5 + 0.5);
                    amp *= 0.6f + 0.6f * (nRand.uni() * 0.5f + 0.5f);
                    rdv = std::min(rdv, 0.6f);
                }
                if (T0 > 0.04) T0 = 0.04;
                if (started) g.tau -= 1.0;
                if (g.tau < 0.0) g.tau = 0.0;
                g.amp = Clamp(amp, 0.2f, 1.8f);
                g.startPeriod(T0, &lfShape(rdv), sr);
                started = true;
            }
            const LFShape& s = *g.shape;
            double U;
            if (!g.inReturn && g.tau >= s.te) {
                g.inReturn = true;
                g.q = exp(-s.eps * (g.tau - s.te));
            }
            if (!g.inReturn) {
                U = s.E0 * s.invA2W2 * (s.alpha * g.zi - s.wg * g.zr + s.wg);
            } else {
                double x = g.tau - s.te;
                U = s.Ue - s.invEpsTa * ((1.0 - g.q) / s.eps - x * s.exr);
                if (U < 0.0) U = 0.0;
            }
            // advance recursion
            double nzr = g.zr * g.mr - g.zi * g.mi;
            g.zi = g.zr * g.mi + g.zi * g.mr;
            g.zr = nzr;
            if (g.inReturn) g.q *= g.qm;
            g.tau += g.dtau;
            double Uphys = U * g.T0;
            float dU = (float)((Uphys - Uprev) * sr);
            Uprev = Uphys;
            float flowN = (float)(U / s.Upeak);
            float voice = dU * g.amp;

            // spectral tilt
            tiltState = voice + tiltA * (tiltState - voice);
            voice = tiltState;
            if (boostG > 0.f) {
                boostState = voice + boostA * (boostState - voice);
                voice += boostG * (voice - boostState);
            }

            // ---- aspiration: explicit (h, VOT) + breathy voicing noise, modulated by glottal flow
            float nraw = nAsp.gaussish();
            float na = aspHP.tick(nraw);
            float asp = na * (ahv * 0.55f + av * breathMixF * (0.25f + 0.75f * Clamp(flowN, 0.f, 1.2f)));
            // whisper: the voicing source is replaced by turbulence noise through the same vocal tract
            float x = voice * av * (1.f - wsp) + asp;
            if (wsp > 0.f) {
                whLP += whA * (nraw - whLP);
                x += (0.45f * nraw + 0.25f * na + 0.5f * whLP * whNorm) * av * wsp * kWhisperGain;
            }
            // release transients (stop bursts exciting the vocal tract)
            if (clickIdx < clicks.size() && n >= (int)(clicks[clickIdx].first * sr)) {
                pulsePos = 0;
                pulseAmp = clicks[clickIdx].second;
                clickIdx++;
            }
            if (pulsePos >= 0) {
                x += 3.f * pulseAmp * sinf(kPi * ((float)pulsePos + 0.5f) / (float)pulseLen);
                if (++pulsePos >= pulseLen) pulsePos = -1;
            }

            // ---- cascade: nasal pole/zero, place anti-resonance, formants
            x = rnp.tick(x);
            x = rnz.tick(x);
            float gz = gzA + (gzB - gzA) * w;
            float xz = rnz2.tick(x);
            x = x + gz * (xz - x);
            for (int ci = 0; ci < nCasc; ci++) x = casc[ci].tick(x);
            float y = x;

            // ---- parallel frication branch
            if (fricActive) {
                float nf = nFric.gaussish();
                if (lpNoise) nf = fricLP.tick(nf);
                nf = fricHP.tick(nf);
                if (av > 0.02f) nf *= 0.45f + 0.55f * Clamp(flowN, 0.f, 1.f);  // voiced frication modulation
                float e = afv * nf;
                float fa0 = faA[0] + (faB[0] - faA[0]) * w, fa1 = faA[1] + (faB[1] - faA[1]) * w,
                      fa2 = faA[2] + (faB[2] - faA[2]) * w, abv = abA + (abB - abA) * w;
                y += fr[0].tick(e) * fa0 - fr[1].tick(e) * fa1 + fr[2].tick(e) * fa2 + e * abv;
            }
            // ---- DC blocker
            float o = y - dcX + dcR * dcY;
            dcX = y;
            dcY = o;
            dst[n] = o;
        }
    }

    // Loudness consistency: gently compress rare peaks (bursts, onsets) above the 99.5th percentile with a soft
    // knee, then normalize to peak 0.8 and apply de-click fades.
    std::vector<float> mag;
    mag.reserve((size_t)nS);
    for (int n = 0; n < nS; n++) {
        float v = dst[n];
        if (!(v == v) || v > 1e6f || v < -1e6f) v = dst[n] = 0.f;  // NaN / blow-up guard
        mag.push_back(fabsf(v));
    }
    size_t kth = (size_t)((double)mag.size() * 0.995);
    if (kth >= mag.size()) kth = mag.size() - 1;
    std::nth_element(mag.begin(), mag.begin() + (long)kth, mag.end());
    const float knee = mag[kth];
    float peak = 0.f;
    if (knee > 1e-9f) {
        for (int n = 0; n < nS; n++) {
            float v = dst[n], a = fabsf(v);
            if (a > knee) {
                float over = (a - knee) / knee;
                a = knee * (1.f + over / (1.f + 1.5f * over));  // asymptotically <= 1.67 * knee
                dst[n] = v < 0.f ? -a : a;
            }
            peak = std::max(peak, a);
        }
    }
    // whispered speech is normalized to a lower peak (it is quieter); mixed utterances are weighted by time
    float wsum = 0.f, tsum = 0.f;
    for (int i = 0; i < (int)u.segs.size(); i++) {
        if (u.segs[i].ph == PH_SIL) continue;
        wsum += u.segStyle(i).whisper * u.segs[i].dur;
        tsum += u.segs[i].dur;
    }
    const float peakTarget = 0.8f - 0.35f * (tsum > 0.f ? wsum / tsum : 0.f);
    float gain = peak > 1e-9f ? peakTarget / peak : 0.f;
    int fin = std::min(nS, (int)(0.004f * sr)), fout = std::min(nS, (int)(0.010f * sr));
    for (int n = 0; n < nS; n++) {
        float gv = gain;
        if (n < fin) gv *= (float)n / (float)fin;
        if (n >= nS - fout) gv *= (float)(nS - 1 - n) / (float)fout;
        dst[n] *= gv;
    }
}

}  // namespace detail
}  // namespace Speech
