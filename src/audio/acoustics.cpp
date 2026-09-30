// Environment acoustics. An acoustic probe around the listener (rays cast with the game's raycast on the game thread,
// a few per frame) classifies the surroundings - open ground, street canyons between facades, covered spaces
// (underpasses, SkyLine stations), tunnels and rooms - and drives the mixer's environment effects:
//   - geometric early reflections: one tap per probe direction, timed (2d/c) and panned from the measured wall
//     distances, so a shot in a street slaps back from the facades on each side at the right delay and direction;
//   - a flutter echo between parallel facades / walls (comb at the round-trip time across the street);
//   - sparse far echoes over open ground (tree lines, hills, distant blocks) for impulsive sounds;
//   - two FDN reverbs, enclosed and outdoor, whose decay follows the estimated volume and absorption of the space
//     (Sabine estimate from the probed box, openings to the sky count as absorption).
// The probe also gives a cheap occlusion estimate for sources behind nearby walls (probeOcclusion).
#include "audio_internal.h"

namespace Audio {
namespace detail {
namespace acoustics {

using namespace dsp;

constexpr float kC = 343.f;

// Direction (world space, z up) and range of probe ray r.
static void rayDir(int r, vec3& d, float& maxD) {
    if (r < kProbeH) {
        float a = (float)r * (kTwoPi / (float)kProbeH);
        d = vec3(cosf(a), sinf(a), 0.f);
        maxD = kProbeHMax;
    } else if (r < kProbeH + kProbeE) {
        float a = (float)(r - kProbeH) * (kTwoPi / (float)kProbeE);
        float ce = cosf(kProbeElev), se = sinf(kProbeElev);
        d = vec3(cosf(a) * ce, sinf(a) * ce, se);
        maxD = kProbeEMax;
    } else {
        d = vec3(0.f, 0.f, 1.f);
        maxD = kProbeUpMax;
    }
}

// ---------------------------------------------------------------------------------------------
// Probe (game thread)
struct Probe {
    float dist[kProbeRays];
    int cursor = 0;
    int got = 0;
    vec3 origin;
    bool haveOrigin = false;
    Probe() { reset(); }
    void reset() {
        for (int r = 0; r < kProbeRays; r++) {
            vec3 d;
            float m;
            rayDir(r, d, m);
            dist[r] = m;
        }
        cursor = 0;
        got = 0;
    }
    // Casts `budget` rays round-robin (all of them after a teleport / camera cut).
    void step(vec3 pos, RaycastFn fn, int budget) {
        if (!haveOrigin || length2(pos - origin) > 25.f * 25.f) {
            reset();
            budget = kProbeRays;
        }
        haveOrigin = true;
        origin = pos;
        for (int k = 0; k < budget; k++) {
            int r = cursor;
            cursor = (cursor + 1) % kProbeRays;
            vec3 d;
            float maxD;
            rayDir(r, d, maxD);
            float hit = maxD;
            if (fn(pos, d, maxD, &hit) && std::isfinite(hit)) dist[r] = Clamp(hit, 0.25f, maxD);
            else dist[r] = maxD;
            if (got < kProbeRays) got++;
        }
    }
    bool complete() const { return got >= kProbeRays; }
    void fill(AcousticState& s) const {
        for (int i = 0; i < kProbeH; i++) s.h[i] = dist[i];
        for (int j = 0; j < kProbeE; j++) s.e[j] = dist[kProbeH + j];
        s.up = dist[kProbeRays - 1];
        s.probed = true;
    }
};

// Derives the reverb-zone parameters from the probe distances. interiorHint: Listener::interior without the car cabin
// (inside a building), urbanHint: Ambience::urban.
static void analyze(AcousticState& s, float interiorHint, float urbanHint) {
    const int half = kProbeH / 2;
    int nNear = 0;
    for (int i = 0; i < kProbeH; i++)
        if (s.h[i] < 25.f) nNear++;
    float nearFrac = (float)nNear / (float)kProbeH;
    int nE = 0;
    for (int j = 0; j < kProbeE; j++)
        if (s.e[j] < kProbeEMax - 0.5f) nE++;
    float eFrac = (float)nE / (float)kProbeE;  // share of the upper hemisphere blocked (facades, roof)
    bool upHit = s.up < kProbeUpMax - 0.5f;
    float cover = upHit ? SmoothStep(24.f, 6.f, s.up) : 0.f;
    float interior = Saturate(interiorHint);
    float geoEnclosed = cover * Saturate(0.25f + 0.45f * nearFrac + 0.5f * eFrac);
    float enclosed = Max(geoEnclosed, interior);
    // street canyon: facades on both sides (narrowest opposite pair); tall when the elevated rays hit too
    float best = 0.f, bestW = 1e9f;
    int axis = -1;
    for (int i = 0; i < half; i++) {
        float a = s.h[i], b = s.h[i + half];
        if (a >= kProbeHMax - 0.5f || b >= kProbeHMax - 0.5f) continue;
        float W = a + b;
        float c = SmoothStep(90.f, 16.f, W);
        if (c > best + 1e-3f || (fabsf(c - best) <= 1e-3f && W < bestW)) {
            best = c;
            bestW = W;
            axis = i;
        }
    }
    // a single tall facade close by (a plaza edge, one side of a boulevard) still slaps
    float oneSided = 0.f;
    for (int i = 0; i < kProbeH; i++) oneSided = Max(oneSided, SmoothStep(45.f, 8.f, s.h[i]));
    float tall = Saturate(0.3f + 1.2f * eFrac);
    float canyon = Max(best, 0.45f * oneSided) * tall * (1.f - cover) * (1.f - enclosed);
    s.enclosed = enclosed;
    s.cover = cover;
    s.canyon = canyon;
    // enclosed reverb: Sabine estimate from the probed box
    float wMin = 1e9f, wMax = 0.f;
    for (int i = 0; i < half; i++) {
        float W = s.h[i] + s.h[i + half];
        wMin = Min(wMin, W);
        wMax = Max(wMax, W);
    }
    float Hh = upHit ? Clamp(s.up + 1.6f, 2.4f, 40.f) : 14.f;
    float L1 = Clamp(wMin, 2.f, 160.f), L2 = Clamp(wMax, L1, 160.f);
    float V = L1 * L2 * Hh, S = 2.f * (L1 * L2 + (L1 + L2) * Hh);
    float furnished = Saturate(interior * 1.25f);
    float alpha = Lerp(0.07f, 0.24f, furnished);   // bare concrete (tunnel, garage, station) .. furnished room
    float sky = 1.f - eFrac;                       // openings to the sky (open station sides, underpass ends)
    float aEff = alpha * (1.f - sky) + 0.85f * sky;
    float rt = 0.161f * V / Max(S * aEff, 1.f);
    s.rtIn = Clamp(rt, 0.22f, 4.2f);
    s.dampIn = Lerp(0.18f, 0.55f, furnished);
    s.preIn = Clamp(L1 * 0.5f / kC, 0.002f, 0.03f);
    s.wetIn = enclosed * Lerp(0.95f, 0.7f, furnished);
    // outdoor reverb: longer and louder among tall facades
    float urban = Saturate(urbanHint);
    s.rtOut = Lerp(0.85f, 1.35f, urban) + 1.0f * canyon;
    s.dampOut = Lerp(0.62f, 0.45f, canyon);
    s.wetOut = (1.f - enclosed) * (0.28f + 0.22f * urban + 0.6f * canyon) + enclosed * 0.08f;
    s.er = Lerp(Lerp(0.5f, 0.85f, canyon), 0.75f, enclosed);
    // flutter between parallel facades (weaker indoors where furniture breaks it up)
    if (axis >= 0 && bestW < 75.f) {
        s.flutterA = axis;
        s.flutterDelay = 2.f * bestW / kC;
        s.flutterFb = Clamp(0.6f * tall * SmoothStep(80.f, 10.f, bestW) * (1.f - 0.5f * cover) * (1.f - 0.6f * furnished), 0.f, 0.6f);
    } else {
        s.flutterA = -1;
        s.flutterDelay = 0.f;
        s.flutterFb = 0.f;
    }
    // open ground: sparse late echoes from tree lines, hills and distant blocks
    s.echo = (1.f - enclosed) * (1.f - 0.6f * canyon) * Lerp(0.65f, 0.4f, urban);
    s.urbanFar = Saturate(0.15f + 0.9f * urban + canyon);
    s.sendScale = Lerp(0.85f, 1.25f, canyon) * Lerp(1.f, 1.5f, enclosed);
}

// Distance from the centre of an axis-aligned box of half extents (hx, hy) to its wall along (cx, cy).
static float boxDist(float hx, float hy, float cx, float cy) {
    float tx = fabsf(cx) > 1e-4f ? hx / fabsf(cx) : 1e9f;
    float ty = fabsf(cy) > 1e-4f ? hy / fabsf(cy) : 1e9f;
    return Min(tx, ty);
}

// Environment without a raycast: a generic room indoors, a generic street in town, open ground elsewhere.
static void fallback(AcousticState& s, float interiorHint, float urbanHint) {
    s = AcousticState();
    float interior = Saturate(interiorHint), urban = Saturate(urbanHint);
    if (interior > 0.3f) {
        for (int i = 0; i < kProbeH; i++) {
            float a = (float)i * (kTwoPi / (float)kProbeH);
            s.h[i] = Min(boxDist(4.5f, 3.5f, cosf(a), sinf(a)), kProbeHMax);
        }
        for (int j = 0; j < kProbeE; j++) s.e[j] = 2.5f;
        s.up = 1.3f;
    } else if (urban > 0.3f) {
        float half = 7.f + 14.f * (1.f - urban);
        for (int i = 0; i < kProbeH; i++) {
            float a = (float)i * (kTwoPi / (float)kProbeH);
            float d = fabsf(cosf(a)) > 1e-3f ? half / fabsf(cosf(a)) : 1e9f;
            s.h[i] = d < kProbeHMax ? d : kProbeHMax;
        }
        if (urban > 0.7f)
            for (int j = 0; j < kProbeE; j++) {
                float a = (float)j * (kTwoPi / (float)kProbeE);
                float d = fabsf(cosf(a)) > 0.3f ? half / (fabsf(cosf(a)) * cosf(kProbeElev)) : 1e9f;
                s.e[j] = d < kProbeEMax ? d : kProbeEMax;
            }
    }
    s.probed = false;
    analyze(s, interiorHint, urbanHint);
}

// Probability-like occlusion (0..1) of a source at `rel` (listener -> source) behind walls the probe has seen.
static float probeOcclusion(const AcousticState& s, vec3 rel) {
    if (!s.probed) return 0.f;
    float dh = sqrtf(rel.x * rel.x + rel.y * rel.y);
    if (dh < 3.f) return 0.f;
    float a = atan2f(rel.y, rel.x);
    if (a < 0.f) a += kTwoPi;
    float fi = a * ((float)kProbeH / kTwoPi);
    int i0 = (int)fi;
    float fr = fi - (float)i0;
    i0 %= kProbeH;
    int i1 = (i0 + 1) % kProbeH;
    // a gap in either neighbouring ray lets the sound through
    float w = Max(s.h[i0], s.h[i1]) >= kProbeHMax - 0.5f ? kProbeHMax : s.h[i0] + (s.h[i1] - s.h[i0]) * fr;
    if (w >= kProbeHMax - 1.f) return 0.f;
    float occ = SmoothStep(w + 1.5f, w + 9.f, dh);
    if (occ <= 0.f) return 0.f;
    int j = (int)(a * ((float)kProbeE / kTwoPi) + 0.5f) % kProbeE;
    bool tallWall = s.e[j] < kProbeEMax - 0.5f;
    // sources well above the probed facades (helicopters, rooftops) are heard over them
    if (!tallWall && rel.z > dh * 0.5f) return 0.f;
    return occ * (tallWall ? 1.f : 0.6f);
}

// ---------------------------------------------------------------------------------------------
// Environment effects (audio thread)
struct EnvFx {
    static constexpr int kTaps = kProbeH + 1;
    static constexpr int kErLen = 32768;      // 0.68 s: facades up to 80 m (0.47 s round trip) + glide
    static constexpr int kFlLen = 32768;
    static constexpr int kEcLen = 131072;     // 2.7 s of far echoes
    FdnReverb revIn, revOut;
    AcousticState cur, tgt;
    bool haveCur = false;
    std::vector<float> erBuf, flBuf, ecBuf;
    int erW = 0, flW = 0, ecW = 0;
    struct Tap {
        float d = 1.f, g = 0.f, td = 1.f, tg = 0.f;
        float gl = 0.f, gr = 0.f;
        vec3 dir;
    };
    Tap taps[kTaps];
    OnePoleLP erLp;
    float flD = 4000.f, flFb = 0.f, flLpS = 0.f, flG = 0.f;
    struct Echo {
        float d = 24000.f, g = 0.f, gl = 0.f, gr = 0.f, lpA = 0.3f, lpS = 0.f;
    };
    Echo ec[4];
    u32 ecSeed = 0x9e3779b9u;
    float ecIdle = 0.f;            // seconds without echo input (re-randomise the pattern when idle)
    float rtInSet = -1.f, rtOutSet = -1.f, dampInSet = -1.f, dampOutSet = -1.f, preInSet = -1.f;
    float inIdle = 10.f;           // seconds the enclosed reverb had no input (skip when silent)

    void init() {
        revIn.init(0.85f, 0x1A2B3Cu);
        revIn.erLevel = 0.12f;
        revIn.setDecay(0.8f, 0.3f);
        revIn.setPreDelay(0.006f);
        revOut.init(1.6f, 0x5EEDu);
        revOut.erLevel = 0.1f;
        revOut.setDecay(1.1f, 0.55f);
        revOut.setPreDelay(0.018f);
        erBuf.assign(kErLen, 0.f);
        flBuf.assign(kFlLen, 0.f);
        ecBuf.assign(kEcLen, 0.f);
        erLp.set(6500.f);
        for (int t = 0; t < kTaps; t++) {
            vec3 d;
            float m;
            rayDir(t < kProbeH ? t : kProbeRays - 1, d, m);
            taps[t].dir = d;
        }
        randomiseEchoes();
    }

    void randomiseEchoes() {
        static const float kGain[4] = {0.42f, 0.3f, 0.24f, 0.18f};
        float t = 0.f;
        for (int k = 0; k < 4; k++) {
            ecSeed = ecSeed * 1664525u + 1013904223u;
            float u = (float)(ecSeed >> 8) / 16777216.f;
            t += 0.28f + 0.5f * u;
            ec[k].d = Min(t, 2.5f) * kSR;
            ecSeed = ecSeed * 1664525u + 1013904223u;
            float pan = ((float)(ecSeed >> 8) / 16777216.f) * 1.6f - 0.8f;
            panGains(pan, ec[k].gl, ec[k].gr);
            ec[k].gl *= 1.41421f;
            ec[k].gr *= 1.41421f;
            ec[k].g = kGain[k];
            ec[k].lpA = onePoleCoef(2600.f / (1.f + 0.7f * (float)k));
            ec[k].lpS = 0.f;
        }
    }

    // New target environment (every block; the probe updates it a few times per second).
    void setTarget(const AcousticState& a, const ListenerState& lis, float blockSec) {
        tgt = a;
        if (!haveCur) {
            cur = a;
            haveCur = true;
        }
        float k = 1.f - expf(-blockSec / 0.35f);
        cur.enclosed += (a.enclosed - cur.enclosed) * k;
        cur.canyon += (a.canyon - cur.canyon) * k;
        cur.rtIn += (a.rtIn - cur.rtIn) * k;
        cur.rtOut += (a.rtOut - cur.rtOut) * k;
        cur.dampIn += (a.dampIn - cur.dampIn) * k;
        cur.dampOut += (a.dampOut - cur.dampOut) * k;
        cur.wetIn += (a.wetIn - cur.wetIn) * k;
        cur.wetOut += (a.wetOut - cur.wetOut) * k;
        cur.preIn += (a.preIn - cur.preIn) * k;
        cur.er += (a.er - cur.er) * k;
        cur.echo += (a.echo - cur.echo) * k;
        cur.urbanFar += (a.urbanFar - cur.urbanFar) * k;
        cur.sendScale += (a.sendScale - cur.sendScale) * k;
        if (fabsf(cur.rtIn - rtInSet) > 0.03f * rtInSet || fabsf(cur.dampIn - dampInSet) > 0.02f) {
            rtInSet = cur.rtIn;
            dampInSet = cur.dampIn;
            revIn.setDecay(rtInSet, dampInSet);
        }
        if (fabsf(cur.rtOut - rtOutSet) > 0.03f * rtOutSet || fabsf(cur.dampOut - dampOutSet) > 0.02f) {
            rtOutSet = cur.rtOut;
            dampOutSet = cur.dampOut;
            revOut.setDecay(rtOutSet, dampOutSet);
        }
        if (fabsf(cur.preIn - preInSet) > 0.0015f) {
            preInSet = cur.preIn;
            revIn.setPreDelay(preInSet);
        }
        // early reflection taps: specular (local minimum) directions strongest
        for (int t = 0; t < kProbeH; t++) {
            float d = a.h[t];
            bool hit = d < kProbeHMax - 0.5f;
            float dl = a.h[(t + kProbeH - 1) % kProbeH], dr = a.h[(t + 1) % kProbeH];
            float spec = (d <= dl + 0.05f && d <= dr + 0.05f) ? 1.f : 0.45f;
            taps[t].tg = hit ? cur.er * spec * 0.75f / (1.f + 2.f * d / 6.f) : 0.f;
            taps[t].td = Clamp(2.f * d / kC * kSR, 16.f, (float)kErLen - 600.f);
        }
        {
            Tap& u = taps[kProbeH];
            bool hit = a.up < kProbeUpMax - 0.5f;
            u.tg = hit ? cur.er * 0.6f / (1.f + 2.f * a.up / 6.f) : 0.f;
            u.td = Clamp(2.f * a.up / kC * kSR, 16.f, (float)kErLen - 600.f);
        }
        // pans follow the listener's orientation
        for (int t = 0; t < kTaps; t++) {
            vec3 d = taps[t].dir;
            float x = dot(d, lis.right), y = dot(d, lis.forward);
            float pan = x * 0.85f;
            float gl, gr;
            panGains(pan, gl, gr);
            float rear = y < 0.f ? 1.f + 0.2f * y : 1.f;
            taps[t].gl = gl * 1.41421f * rear;
            taps[t].gr = gr * 1.41421f * rear;
        }
        // flutter comb (delay glides; feedback follows the canyon)
        float fb = a.flutterA >= 0 ? a.flutterFb : 0.f;
        flFb += (fb - flFb) * k;
        if (a.flutterA >= 0) {
            float td = Clamp(a.flutterDelay * kSR, 300.f, (float)kFlLen - 600.f);
            if (flG < 1e-3f) flD = td;
            else flD += (td - flD) * Min(1.f, k * 0.5f);
            flG += (1.f - flG) * k;
        } else {
            flG += (0.f - flG) * k;
        }
    }

    // rev/er/echo: mono sends summed by the voices. Adds the wet environment (stereo) to outL/outR.
    void process(const float* rev, const float* er, const float* echo, float* outL, float* outR, int n) {
        float erL[kMaxBlock], erR[kMaxBlock], erM[kMaxBlock], flIn[kMaxBlock], tmp[kMaxBlock];
        for (int i = 0; i < n; i++) erL[i] = erR[i] = erM[i] = flIn[i] = 0.f;
        // ---- early reflections
        const int emask = kErLen - 1;
        float* eb = erBuf.data();
        float inAbs = 0.f;
        for (int i = 0; i < n; i++) {
            float x = erLp.process(er[i]);
            eb[(erW + i) & emask] = x;
            inAbs += fabsf(x);
        }
        const float invN = 1.f / (float)n;
        for (int t = 0; t < kTaps; t++) {
            Tap& tp = taps[t];
            if (tp.g < 1e-5f && tp.tg < 1e-5f) {
                tp.g = 0.f;
                tp.d = tp.td;
                continue;
            }
            // large geometry changes: fade the tap out, jump, fade back in (no pitch sweeps)
            bool jump = fabsf(tp.td - tp.d) > 0.08f * tp.d + 96.f;
            float g0 = tp.g, g1 = jump ? 0.f : tp.g + (tp.tg - tp.g) * 0.25f;
            if (jump && g0 < 1e-4f) {
                tp.d = tp.td;
                g0 = 0.f;
                g1 = tp.tg * 0.25f;
                jump = false;
            }
            float d0 = tp.d, d1 = jump ? tp.d : tp.d + (tp.td - tp.d) * 0.2f;
            bool isFlutter = cur.flutterA >= 0 && (t == tgt.flutterA || t == tgt.flutterA + kProbeH / 2);
            if (fabsf(d1 - d0) < 1e-3f) {
                int di = (int)(d0 + 0.5f);
                ringRead(eb, emask, erW - di, tmp, n);
                for (int i = 0; i < n; i++) {
                    float g = g0 + (g1 - g0) * ((float)i * invN);
                    float y = tmp[i] * g;
                    erL[i] += y * tp.gl;
                    erR[i] += y * tp.gr;
                    erM[i] += y;
                    if (isFlutter) flIn[i] += y;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    float u = (float)i * invN;
                    float d = d0 + (d1 - d0) * u;
                    int di = (int)d;
                    float fr = d - (float)di;
                    float a0 = eb[(erW + i - di) & emask], a1 = eb[(erW + i - di - 1) & emask];
                    float y = (a0 + (a1 - a0) * fr) * (g0 + (g1 - g0) * u);
                    erL[i] += y * tp.gl;
                    erR[i] += y * tp.gr;
                    erM[i] += y;
                    if (isFlutter) flIn[i] += y;
                }
            }
            tp.g = g1;
            tp.d = d1;
        }
        erW = (erW + n) & emask;
        // ---- flutter between parallel facades
        if (flG > 1e-3f && flFb > 1e-3f) {
            const int fmask = kFlLen - 1;
            float* fb = flBuf.data();
            int D = (int)flD;
            ringRead(fb, fmask, flW - D, tmp, n);
            const float lpA = 0.55f;
            for (int i = 0; i < n; i++) {
                flLpS += (tmp[i] - flLpS) * lpA;
                float y = flLpS;
                fb[(flW + i) & fmask] = (flIn[i] + y) * flFb;
                float o = y * flG;
                erL[i] += o * 0.8f;
                erR[i] += o * 0.8f;
                erM[i] += o;
            }
            flW = (flW + n) & fmask;
        } else if (flLpS != 0.f) {
            std::fill(flBuf.begin(), flBuf.end(), 0.f);
            flLpS = 0.f;
        }
        // ---- open-field far echoes (impulsive sends only)
        float ecM[kMaxBlock];
        for (int i = 0; i < n; i++) ecM[i] = 0.f;
        {
            const int cmask = kEcLen - 1;
            float* cb = ecBuf.data();
            float eAbs = 0.f;
            for (int i = 0; i < n; i++) {
                cb[(ecW + i) & cmask] = echo[i];
                eAbs += fabsf(echo[i]);
            }
            if (eAbs > 1e-6f) ecIdle = 0.f;
            else ecIdle += (float)n * kInvSR;
            if (ecIdle > 3.2f && ecIdle < 3.2f + (float)n * kInvSR * 1.5f) randomiseEchoes();
            if (ecIdle < 3.f && cur.echo > 1e-3f) {
                for (int k = 0; k < 4; k++) {
                    Echo& e = ec[k];
                    ringRead(cb, cmask, ecW - (int)e.d, tmp, n);
                    float g = e.g * cur.echo;
                    float s = e.lpS;
                    for (int i = 0; i < n; i++) {
                        s += (tmp[i] - s) * e.lpA;
                        float y = s * g;
                        erL[i] += y * e.gl;
                        erR[i] += y * e.gr;
                        ecM[i] += y;
                    }
                    e.lpS = s;
                }
            }
            ecW = (ecW + n) & cmask;
        }
        // ---- reverbs: enclosed and outdoor, fed by the sends plus the early field
        float inIn[kMaxBlock], inOut[kMaxBlock], rl[kMaxBlock], rr[kMaxBlock];
        const float e = Saturate(cur.enclosed), ss = cur.sendScale;
        float inSum = 0.f;
        for (int i = 0; i < n; i++) {
            inIn[i] = (rev[i] * ss + erM[i] * 0.35f) * e;
            inOut[i] = rev[i] * ss * (1.f - e) + erM[i] * 0.2f + ecM[i] * 0.3f;
            inSum += fabsf(inIn[i]);
        }
        if (inSum > 1e-7f) inIdle = 0.f;
        else inIdle += (float)n * kInvSR;
        if (inIdle < cur.rtIn * 1.3f + 0.2f) {
            revIn.process(inIn, inIn, rl, rr, n);
            const float w = cur.wetIn;
            for (int i = 0; i < n; i++) {
                erL[i] += rl[i] * w;
                erR[i] += rr[i] * w;
            }
        }
        revOut.process(inOut, inOut, rl, rr, n);
        const float wo = cur.wetOut;
        for (int i = 0; i < n; i++) {
            outL[i] += erL[i] + rl[i] * wo;
            outR[i] += erR[i] + rr[i] * wo;
        }
        (void)inAbs;
    }
};

}  // namespace acoustics
}  // namespace detail
}  // namespace Audio
