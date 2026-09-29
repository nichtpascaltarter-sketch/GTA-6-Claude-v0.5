// Continuous, parameter-driven sound sources: combustion engines (per-cylinder pulse model with
// exhaust resonances, intake, turbo, crackle), electric motors, sirens, horns, tire skids, wind,
// fire, helicopter rotors, propeller planes, jets, outboard motors, water wakes, car alarms and
// crowds. All synths are allocation-free and constructed in place inside mixer-owned storage.
#include "audio_internal.h"

namespace Audio {
namespace detail {
namespace emit {

using namespace dsp;

FORCEINLINE float smoothCoef(float tauSec) { return 1.f - expf(-1.f / Max(tauSec * kSR, 1.f)); }

// Approximate gaussian from the xorshift noise (sum of 3 uniforms).
FORCEINLINE float gaussish(Noise& n) { return (n.white() + n.white() + n.white()) * 0.577f; }

// =============================================================================================
// Engine core: per-cylinder combustion pulses shaped by a load-dependent pulse filter and an
// exhaust network (direct path + resonators + muffler low-pass), plus intake roar, mechanical
// noise, turbo whine / blow-off, decel crackle and rev-limiter cut.
struct EngineSpec {
    int cylinders;
    bool twoStroke;
    float fireAngles[12];  // degrees in cycle (720 four-stroke, 360 two-stroke); empty => even spacing
    signed char bank[12];  // 0/1 bank per firing slot (V engines)
    float idleRpm, maxRpm;
    float cylGainVar, cylTimeVar, jitterIdle;
    float pulseMs;
    float resF[3], resQ[3], resG[3];
    float directGain;
    float mufflerLP;
    float intakeGain, intakeFreq;
    float mechGain;
    float turbo;
    float crackle;
    float subGain;
    float level;
    float rasp;
    float bankImbalance;
    float knock;  // diesel clatter
};

static const EngineSpec kEngineSpecs[ENGINE_COUNT] = {
    // I4 (turbo hatch)
    {4, false, {0, 180, 360, 540}, {0, 0, 0, 0}, 850, 7000, 0.06f, 1.5f, 0.08f, 0.85f,
     {210, 560, 1500}, {1.2f, 2.0f, 2.6f}, {1.0f, 0.55f, 0.35f}, 0.35f, 2900, 0.30f, 900, 0.05f, 0.8f, 0.3f, 0.30f, 0.95f, 0.28f, 0.f, 0.f},
    // V6
    {6, false, {0, 120, 240, 360, 480, 600}, {0, 1, 0, 1, 0, 1}, 750, 6800, 0.05f, 1.2f, 0.06f, 1.0f,
     {170, 430, 1150}, {1.2f, 1.8f, 2.2f}, {1.0f, 0.6f, 0.3f}, 0.3f, 2500, 0.28f, 800, 0.04f, 0.55f, 0.25f, 0.38f, 0.95f, 0.22f, 0.08f, 0.f},
    // V8 cross-plane (firing order 1-8-4-3-6-5-7-2 => banks L R R L R L L R)
    {8, false, {0, 90, 180, 270, 360, 450, 540, 630}, {0, 1, 1, 0, 1, 0, 0, 1}, 650, 6400, 0.07f, 2.0f, 0.13f, 1.3f,
     {105, 290, 780}, {1.0f, 1.6f, 2.0f}, {1.0f, 0.65f, 0.35f}, 0.35f, 2100, 0.32f, 700, 0.04f, 0.f, 0.65f, 0.65f, 1.05f, 0.32f, 0.26f, 0.f},
    // V12
    {12, false, {0, 60, 120, 180, 240, 300, 360, 420, 480, 540, 600, 660}, {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}, 950, 8800,
     0.04f, 1.0f, 0.04f, 0.7f, {260, 700, 1900}, {1.3f, 2.0f, 2.6f}, {1.0f, 0.55f, 0.4f}, 0.4f, 4300, 0.42f, 1200, 0.03f, 0.f, 0.5f, 0.25f, 0.9f, 0.3f, 0.05f, 0.f},
    // Truck diesel I6 (turbo)
    {6, false, {0, 120, 240, 360, 480, 600}, {0, 0, 0, 0, 0, 0}, 600, 2600, 0.08f, 2.0f, 0.10f, 1.8f,
     {75, 190, 520}, {1.0f, 1.5f, 1.8f}, {1.0f, 0.7f, 0.45f}, 0.3f, 1500, 0.22f, 600, 0.10f, 1.0f, 0.f, 0.75f, 1.15f, 0.45f, 0.f, 0.55f},
    // Electric (unused by pulse model)
    {0, false, {0}, {0}, 0, 12000, 0, 0, 0, 1, {1000, 2000, 4000}, {1, 1, 1}, {0, 0, 0}, 0, 5000, 0, 0, 0, 0, 0, 0, 0.6f, 0, 0, 0},
    // Bike sport I4
    {4, false, {0, 180, 360, 540}, {0, 0, 0, 0}, 1300, 13500, 0.05f, 1.2f, 0.07f, 0.55f,
     {350, 900, 2400}, {1.3f, 2.0f, 2.5f}, {1.0f, 0.6f, 0.4f}, 0.45f, 5200, 0.55f, 1600, 0.04f, 0.f, 0.7f, 0.2f, 0.85f, 0.35f, 0.f, 0.f},
    // Bike cruiser V-twin 45 degrees (uneven 405/315)
    {2, false, {0, 405}, {0, 1}, 850, 5600, 0.10f, 3.0f, 0.16f, 2.0f,
     {88, 240, 650}, {1.0f, 1.5f, 1.8f}, {1.0f, 0.7f, 0.4f}, 0.35f, 1800, 0.18f, 500, 0.05f, 0.f, 0.3f, 0.85f, 1.0f, 0.35f, 0.1f, 0.f},
    // Scooter single 2-stroke
    {1, true, {0}, {0}, 1900, 9500, 0.f, 0.f, 0.12f, 0.5f,
     {430, 1100, 2900}, {2.5f, 3.0f, 3.0f}, {1.0f, 0.6f, 0.45f}, 0.3f, 3600, 0.22f, 1400, 0.03f, 0.f, 0.1f, 0.12f, 0.7f, 0.55f, 0.f, 0.f},
};

struct EngineCore {
    EngineSpec spec;
    Noise nz;
    // runtime params (smoothed)
    float rpm01 = 0, throttle = 0, load = 0;
    float tRpm = 0, tThrottle = 0, tLoad = 0;
    bool paramsInit = false;
    // crank
    float crankDeg = 0;
    float cycleDeg = 720;
    float angles[12];
    float cylGain[12];
    int bankOf[12];
    int nextFire = 0;
    // pulse shaping
    float carry = 0, carryKnock = 0;
    float s1 = 0, s2 = 0, aC = 0.02f, pulseScale = 40.f;
    float pulseEnv = 0;
    float bankDelayBuf[32] = {};
    int bdPos = 0;
    // exhaust
    Biquad res[3];
    OnePoleLP muffler;
    float mufflerFc = -1;
    OnePoleHP dcHp;
    Svf intakeBp;
    float intakeFc = -1;
    Svf knockBp;
    float knockEnv = 0;
    // turbo
    float boost = 0, turboPhase = 0, turboFreq = 1500, bovEnv = 0, bovFlutter = 0, bovPh = 0;
    Svf turboNoiseBp;
    OnePoleHP bovHp;
    float lastThrottleHi = 0;
    // crackle
    float crackleTimer = 0, popEnv = 0;
    Svf popBp;
    float throttleHist = 0;
    // idle wobble
    float wobblePhase = 0;
    int ctl = 0;
    float gainNorm = 1;
    float levelTrim = 1.f;
    float drive = 0.2f;

    void init(const EngineSpec& s, u32 seed) {
        spec = s;
        nz.seed(seed * 2654435761u + 12345u);
        cycleDeg = s.twoStroke ? 360.f : 720.f;
        Rng r(seed);
        int cyl = Clamp(s.cylinders, 0, 12);
        spec.cylinders = cyl;
        for (int i = 0; i < cyl; i++) {
            float a = s.fireAngles[i] + r.range(-1.f, 1.f) * s.cylTimeVar;
            if (a < 0.f) a += cycleDeg;
            if (a >= cycleDeg) a -= cycleDeg;
            angles[i] = a;
            cylGain[i] = 1.f + r.range(-1.f, 1.f) * s.cylGainVar;
            bankOf[i] = s.bank[i];
            if (bankOf[i]) cylGain[i] *= 1.f - s.bankImbalance;
        }
        for (int i = 0; i < cyl; i++)
            for (int j = i + 1; j < cyl; j++)
                if (angles[j] < angles[i]) {
                    std::swap(angles[i], angles[j]);
                    std::swap(cylGain[i], cylGain[j]);
                    std::swap(bankOf[i], bankOf[j]);
                }
        for (int i = 0; i < 3; i++) res[i].setBP(s.resF[i] * r.range(0.95f, 1.05f), s.resQ[i]);
        dcHp.set(25.f);
        popBp.set(1800.f, 1.2f);
        knockBp.set(3200.f, 1.6f);
        turboNoiseBp.set(3000.f, 2.f);
        bovHp.set(1800.f);
        crankDeg = r.range(0.f, cycleDeg * 0.999f);
        nextFire = 0;
        while (nextFire < cyl && angles[nextFire] <= crankDeg) nextFire++;
        s1 = s2 = 0;
        carry = carryKnock = 0;
        memset(bankDelayBuf, 0, sizeof(bankDelayBuf));
        float pulsesPerRev = (float)Max(cyl, 1) * (s.twoStroke ? 1.f : 0.5f);
        gainNorm = 1.f / sqrtf(Max(pulsesPerRev, 0.5f) / 2.f);
        ctl = 0;
    }
    void setParams(float rpm, float thr, float ld) {
        tRpm = Saturate(rpm);
        tThrottle = Saturate(thr);
        tLoad = Saturate(ld);
        if (!paramsInit) {
            rpm01 = tRpm;
            throttle = tThrottle;
            load = tLoad;
            paramsInit = true;
        }
    }
    FORCEINLINE float rpmHz() const { return spec.idleRpm + (spec.maxRpm - spec.idleRpm) * rpm01; }
    void control() {
        float fc = spec.intakeFreq * (0.7f + 0.9f * rpm01);
        if (fabsf(fc - intakeFc) > 5.f) { intakeBp.setG(svfG(fc), 1.4f); intakeFc = fc; }
        float mfc = spec.mufflerLP * (0.55f + 0.6f * throttle + 0.35f * load) * (0.8f + 0.4f * rpm01);
        if (fabsf(mfc - mufflerFc) > 10.f) { muffler.set(mfc); mufflerFc = mfc; }
        // pulse width: sharper under load
        float tauMs = spec.pulseMs * (1.35f - 0.7f * load * throttle) * (1.1f - 0.3f * rpm01);
        tauMs = Max(tauMs, 0.12f);
        aC = 1.f - expf(-1.f / (tauMs * 0.001f * kSR));
        pulseScale = 60.f / tauMs;
        drive = 0.18f + 0.82f * (0.3f + 0.7f * load) * (0.25f + 0.75f * throttle);
        if (spec.turbo > 0.f) {
            float target = throttle * SmoothStep(0.2f, 0.7f, rpm01) * (0.5f + 0.5f * load);
            boost += (target - boost) * (target > boost ? 0.0035f : 0.012f);
            if (lastThrottleHi - throttle > 0.45f && boost > 0.4f && bovEnv < 0.05f) {
                bovEnv = boost;
                bovFlutter = 1.f;
                boost *= 0.4f;
                lastThrottleHi = throttle;
            }
            lastThrottleHi = Max(throttle, lastThrottleHi * 0.999f);
            turboFreq = 1500.f + 7500.f * boost * (0.45f + 0.55f * rpm01);
            turboNoiseBp.setG(svfG(turboFreq * 0.5f), 2.f);
        }
        if (spec.crackle > 0.f) {
            throttleHist = Max(throttle, throttleHist * 0.9995f);
            if (throttle < 0.12f && throttleHist > 0.5f && rpm01 > 0.35f) {
                crackleTimer = Max(crackleTimer, 1.6f);
                throttleHist = 0.f;
            }
        }
    }
    FORCEINLINE void fire(int ci, float fr) {
        float jit = spec.jitterIdle * (1.f - 0.7f * rpm01) * gaussish(nz);
        float a = cylGain[ci] * drive * (1.f + jit);
        if (rpm01 > 0.985f && throttle > 0.7f && nz.uni() < 0.55f) a *= 0.05f;  // rev limiter cut
        if (rpm01 < 0.08f && nz.uni() < 0.01f) a *= 0.4f;                       // lumpy idle
        if (bankOf[ci] && spec.bankImbalance > 0.f) {
            float dFrac = fr + 12.f;  // other bank arrives ~0.25 ms later
            int di = (int)dFrac;
            float dfr = dFrac - (float)di;
            bankDelayBuf[(bdPos + di) & 31] += a * (1.f - dfr);
            bankDelayBuf[(bdPos + di + 1) & 31] += a * dfr;
        } else {
            bankDelayBuf[bdPos] += a * (1.f - fr);
            carry += a * fr;
        }
        if (spec.knock > 0.f) {
            knockEnv += a;
            (void)carryKnock;
        }
    }
    void render(float* out, int n) {
        const float kRpmC = smoothCoef(0.035f), kThrC = smoothCoef(0.03f), kLoadC = smoothCoef(0.08f);
        const int cyl = spec.cylinders;
        for (int i = 0; i < n; i++) {
            if ((ctl++ & 15) == 0) control();
            rpm01 += (tRpm - rpm01) * kRpmC;
            throttle += (tThrottle - throttle) * kThrC;
            load += (tLoad - load) * kLoadC;

            wobblePhase += 0.8f * kInvSR;
            if (wobblePhase >= 1.f) wobblePhase -= 1.f;
            float wob = 1.f + 0.012f * (1.f - SmoothStep(0.f, 0.15f, rpm01)) * sinWrapped(wobblePhase);
            float dDeg = rpmHz() * wob * (360.f / 60.f) * kInvSR;
            float prev = crankDeg;
            crankDeg += dDeg;
            // carry from previous sample's fractional impulse
            bankDelayBuf[bdPos] += carry;
            carry = 0;
            for (int guard = 0; guard < 24 && cyl > 0; guard++) {
                if (nextFire >= cyl) {
                    if (crankDeg >= cycleDeg) {
                        crankDeg -= cycleDeg;
                        prev -= cycleDeg;
                        nextFire = 0;
                        continue;
                    }
                    break;
                }
                float target = angles[nextFire];
                if (crankDeg < target) break;
                float fr = Clamp((target - prev) / Max(dDeg, 1e-6f), 0.f, 1.f);
                fire(nextFire, fr);
                nextFire++;
            }
            float impulse = bankDelayBuf[bdPos];
            bankDelayBuf[bdPos] = 0.f;
            bdPos = (bdPos + 1) & 31;

            // decel crackle pops
            float popImp = 0.f;
            if (crackleTimer > 0.f) {
                crackleTimer -= kInvSR;
                float rate = 14.f * spec.crackle * (crackleTimer / 1.6f) * (0.3f + rpm01);
                if (throttle < 0.2f && nz.uni() < rate * kInvSR) {
                    float mag = 1.5f + 3.5f * nz.uni() * nz.uni();
                    popImp = mag * spec.crackle * 0.5f;
                    popEnv = Max(popEnv, 0.6f * mag * spec.crackle);
                }
                if (throttle > 0.3f) crackleTimer = 0.f;
            }

            // pulse shaping: critically damped 2-pole low-pass
            s1 += ((impulse + popImp) * pulseScale - s1) * aC;
            s2 += (s1 - s2) * aC;
            float exc = s2;
            float aexc = fabsf(exc);
            pulseEnv += (aexc - pulseEnv) * 0.01f;
            float w = nz.white();
            exc += w * aexc * spec.rasp * (0.5f + 0.8f * load);
            float y = exc * spec.directGain;
            y += res[0].process(exc) * spec.resG[0];
            y += res[1].process(exc) * spec.resG[1];
            y += res[2].process(exc) * spec.resG[2];
            y = muffler.process(y);
            y += s2 * spec.subGain * 0.25f;
            if (spec.knock > 0.f) {
                knockEnv *= 0.9965f;
                y += knockBp.bp(w) * knockEnv * spec.knock * 0.9f;
            }
            float intake = intakeBp.bp(nz.white()) * (0.15f + pulseEnv * 3.f) * throttle * (0.3f + 0.7f * rpm01);
            y += intake * spec.intakeGain;
            y += w * pulseEnv * spec.mechGain * 2.f * (1.f - rpm01 * 0.5f);
            if (popEnv > 1e-4f) {
                y += popBp.bp(nz.white()) * popEnv * 1.5f;
                popEnv *= 0.992f;
            }
            if (spec.turbo > 0.f) {
                turboPhase += turboFreq * kInvSR;
                if (turboPhase >= 1.f) turboPhase -= 1.f;
                float ph2 = turboPhase * 2.f;
                if (ph2 >= 1.f) ph2 -= 1.f;
                float wh = sinWrapped(turboPhase) + 0.25f * sinWrapped(ph2);
                y += wh * boost * boost * 0.06f * spec.turbo;
                y += turboNoiseBp.bp(w) * boost * 0.05f * spec.turbo;
                if (bovEnv > 1e-4f) {
                    bovFlutter *= 0.9997f;
                    bovPh += 23.f * kInvSR;
                    if (bovPh >= 1.f) bovPh -= 1.f;
                    float fl = 1.f - 0.6f * bovFlutter * (0.5f + 0.5f * sinWrapped(bovPh));
                    y += bovHp.process(w) * bovEnv * 0.2f * fl * spec.turbo;
                    bovEnv *= 0.99985f;
                }
            }
            y = dcHp.process(y);
            out[i] = y * spec.level * gainNorm * 0.55f * levelTrim;
        }
    }
};

// ---------------------------------------------------------------------------------------------
struct ElectricCore {
    Noise nz;
    float rpm01 = 0, throttle = 0, tRpm = 0, tThr = 0;
    bool init0 = false;
    float ph1 = 0, ph2 = 0, ph3 = 0, phA = 0, phB = 0;
    PinkNoise pink;
    OnePoleLP roadLp;
    Svf whineBp;
    void init(u32 seed) {
        nz.seed(seed | 1);
        roadLp.set(900.f);
    }
    void setParams(float r, float t) {
        tRpm = Saturate(r);
        tThr = Saturate(t);
        if (!init0) { rpm01 = tRpm; throttle = tThr; init0 = true; }
    }
    void render(float* out, int n) {
        const float c = smoothCoef(0.04f);
        for (int i = 0; i < n; i++) {
            rpm01 += (tRpm - rpm01) * c;
            throttle += (tThr - throttle) * c;
            float f = 40.f + 5200.f * rpm01;
            ph1 += f * kInvSR; if (ph1 >= 1.f) ph1 -= 1.f;
            ph2 += f * 0.5f * kInvSR; if (ph2 >= 1.f) ph2 -= 1.f;
            ph3 += f * (1.f / 6.f) * kInvSR; if (ph3 >= 1.f) ph3 -= 1.f;
            float drive = 0.25f + 0.75f * throttle;
            float whine = (sinWrapped(ph1) * 0.5f + sinWrapped(ph2) * 0.3f) * SmoothStep(0.02f, 0.15f, rpm01) * drive;
            float gear = sinWrapped(ph3) * 0.4f * rpm01;
            // pedestrian warning hum at low speed
            float avasG = 1.f - SmoothStep(0.05f, 0.3f, rpm01);
            float fa = 310.f + 250.f * rpm01;
            phA += fa * kInvSR; if (phA >= 1.f) phA -= 1.f;
            phB += fa * 1.498f * kInvSR; if (phB >= 1.f) phB -= 1.f;
            float avas = (sinWrapped(phA) + 0.6f * sinWrapped(phB)) * 0.12f * avasG;
            float road = roadLp.process(pink.process(nz.white())) * rpm01 * 1.6f;
            out[i] = (whine * 0.07f + gear * 0.05f + avas + road * 0.5f) * 0.32f;
        }
    }
};

// Loudness calibration per engine kind (measured RMS of a full idle/rev/decel sweep at 4 m).
static const float kEngineTrim[ENGINE_COUNT] = {0.70f, 1.27f, 2.79f, 1.10f, 2.24f, 1.f, 0.62f, 2.88f, 0.53f};

// =============================================================================================
struct EngineSynth : EmitterSynth {
    EngineCore core;
    ElectricCore ev;
    int kind = -1;
    u32 seed;
    explicit EngineSynth(u32 s) : seed(s) {}
    void setParams(float p0, float p1, float p2, float p3) override {
        int k = Clamp((int)(p3 + 0.5f), 0, ENGINE_COUNT - 1);
        if (k != kind) {
            kind = k;
            if (k == ENGINE_ELECTRIC) ev.init(seed);
            else {
                core.init(kEngineSpecs[k], seed);
                core.levelTrim = kEngineTrim[k];
            }
        }
        if (kind == ENGINE_ELECTRIC) ev.setParams(p0, p1);
        else core.setParams(p0, p1, p2);
    }
    void render(float* out, int n) override {
        if (kind < 0) { memset(out, 0, sizeof(float) * (size_t)n); return; }
        if (kind == ENGINE_ELECTRIC) ev.render(out, n);
        else core.render(out, n);
    }
};

// =============================================================================================
// Electronic siren: square/trapezoid drive into a horn-speaker response.
struct SirenCore {
    int mode = 0;
    float cyc = 0, phase = 0, freq = 800;
    Biquad hornBp, hornHp, hornPeak;
    OnePoleLP smooth;
    void init() {
        hornBp.setBP(1500.f, 0.6f);
        hornHp.setHP(420.f, 0.7f);
        hornPeak.setPeak(2600.f, 1.5f, 5.f);
        smooth.set(9000.f);
    }
    static float period(int m) {
        static const float kP[4] = {4.4f, 0.33f, 1.1f, 1.3f};
        return kP[Clamp(m, 0, 3)];
    }
    float targetFreq() const {
        switch (mode) {
            case 0: {  // wail: slow rise, slow fall
                float c = cyc * 4.4f;
                if (c < 2.1f) return 650.f + 800.f * (1.f - expf(-c * 1.6f)) / (1.f - expf(-2.1f * 1.6f));
                float d = (c - 2.1f) / 2.3f;
                return 1450.f - 800.f * (d * d * (3.f - 2.f * d));
            }
            case 1:  // yelp
                return 650.f + 800.f * (cyc < 0.6f ? cyc / 0.6f : 1.f - (cyc - 0.6f) / 0.4f);
            case 2:  // hi-lo electronic
                return cyc < 0.5f ? 960.f : 770.f;
            default:  // ambulance two-tone (pneumatic-horn style, a fourth apart)
                return cyc < 0.5f ? 585.f : 440.f;
        }
    }
    FORCEINLINE float tick() {
        float tf = targetFreq();
        freq += (tf - freq) * (mode >= 2 ? 0.02f : 0.2f);
        cyc += kInvSR / period(mode);
        if (cyc >= 1.f) cyc -= 1.f;
        phase += freq * kInvSR;
        if (phase >= 1.f) phase -= 1.f;
        float v;
        if (mode == 3) {
            v = (phase < 0.3f ? 1.f : -0.43f);
        } else {
            float p = phase;
            v = p < 0.46f ? 1.f : (p < 0.5f ? 1.f - (p - 0.46f) * 50.f : (p < 0.96f ? -1.f : -1.f + (p - 0.96f) * 50.f));
        }
        v = smooth.process(v);
        float y = hornBp.process(v) * 1.2f + hornHp.process(v) * 0.4f;
        y = hornPeak.process(y);
        return fastTanh(y * 1.5f) * 0.22f;
    }
};

struct SirenSynth : EmitterSynth {
    SirenCore core;
    SirenSynth() { core.init(); }
    void setParams(float p0, float, float, float) override {
        int m = Clamp((int)(p0 + 0.5f), 0, 3);
        if (m != core.mode) { core.mode = m; }
    }
    void render(float* out, int n) override {
        for (int i = 0; i < n; i++) out[i] = core.tick();
    }
};

// =============================================================================================
// Horn: two (or three, trucks) disc-horn tones, nasal and slightly distorted.
struct HornCore {
    float f[3] = {420, 529, 0};
    float ph[3] = {0, 0, 0};
    int count = 2;
    float env = 0, t = 0;
    bool truck = false;
    Biquad formant, formant2, lp;
    OnePoleHP hp;
    void init(float variant) {
        truck = variant > 0.8f;
        if (truck) {
            float b = 185.f + 30.f * (variant - 0.8f) * 5.f;
            f[0] = b; f[1] = b * 1.26f; f[2] = b * 1.5f;
            count = 3;
            formant.setPeak(900.f, 1.2f, 8.f);
            formant2.setPeak(1900.f, 1.5f, 5.f);
            lp.setLP(3500.f, 0.7f);
        } else {
            float b = 360.f + 160.f * Saturate(variant / 0.8f);
            f[0] = b; f[1] = b * 1.26f;
            count = 2;
            formant.setPeak(2300.f, 1.4f, 9.f);
            formant2.setPeak(3400.f, 2.f, 5.f);
            lp.setLP(5500.f, 0.7f);
        }
        hp.set(250.f);
    }
    FORCEINLINE float tick() {
        if (t < 1.f) t += kInvSR;
        env += (1.f - env) * 0.004f;
        float bend = 1.f - 0.035f * expf(-t * 25.f);
        float s = 0;
        for (int k = 0; k < count; k++) {
            ph[k] += f[k] * bend * kInvSR;
            if (ph[k] >= 1.f) ph[k] -= 1.f;
            float p = ph[k];
            // asymmetric pulse (diaphragm slap)
            s += (p < 0.28f ? 1.f : -0.39f) + 0.3f * (2.f * p - 1.f);
        }
        s = hp.process(s);
        float y = formant2.process(formant.process(s));
        y = lp.process(fastTanh(y * 1.2f));
        return y * env * (truck ? 0.5f : 0.45f);
    }
};

struct HornSynth : EmitterSynth {
    HornCore core;
    float variant = -1.f;
    void setParams(float p0, float, float, float) override {
        float v = Saturate(p0);
        if (variant < 0.f || fabsf(v - variant) > 0.05f) {
            variant = v;
            core.init(v);
        }
    }
    void render(float* out, int n) override {
        if (variant < 0.f) { memset(out, 0, sizeof(float) * (size_t)n); return; }
        for (int i = 0; i < n; i++) out[i] = core.tick();
    }
};

// =============================================================================================
// Tire skid: tonal squeal (narrow resonances excited by stick-slip noise) on asphalt, gravel crunch
// on dirt.
struct SkidSynth : EmitterSynth {
    Noise nz;
    float slip = 0, tSlip = 0, surf = 0, tSurf = 0;
    Svf sq[3];
    float sqF[3] = {950, 1520, 2280};
    float wander[3] = {0, 0, 0};
    Svf broad, dirtLp, rumble;
    float stick = 0, stickPhase = 0;
    float grain = 0, slipPow = 0;
    int ctl = 0;
    explicit SkidSynth(u32 seed) : nz(seed | 7) {
        broad.set(1400.f, 0.6f);
        dirtLp.set(2600.f, 0.6f);
        rumble.set(180.f, 0.7f);
        Rng r(seed);
        for (int i = 0; i < 3; i++) sqF[i] *= r.range(0.9f, 1.1f);
    }
    void setParams(float p0, float p1, float, float) override {
        tSlip = Saturate(p0);
        tSurf = Saturate(p1);
    }
    void render(float* out, int n) override {
        const float c = smoothCoef(0.05f);
        for (int i = 0; i < n; i++) {
            slip += (tSlip - slip) * c;
            surf += (tSurf - surf) * c;
            if ((ctl++ & 31) == 0) {
                slipPow = powf(slip, 1.3f);
                for (int k = 0; k < 3; k++) {
                    wander[k] += nz.white() * 0.004f;
                    wander[k] *= 0.995f;
                    float f = sqF[k] * (1.f + wander[k] + 0.08f * slip);
                    sq[k].setG(svfG(f), 28.f);
                }
            }
            float w = nz.white();
            // stick-slip modulation: irregular pulse train 40..90 Hz
            stickPhase += (40.f + 50.f * slip + 20.f * nz.uni()) * kInvSR;
            if (stickPhase >= 1.f) { stickPhase -= 1.f; stick = 0.6f + 0.4f * nz.uni(); }
            stick *= 0.9985f;
            float exc = w * (0.4f + stick);
            float squeal = (sq[0].bp(exc) * 1.0f + sq[1].bp(exc) * 0.7f + sq[2].bp(exc) * 0.35f) * 0.09f;
            float rubber = broad.bp(w) * 0.25f;
            float asphalt = (squeal * SmoothStep(0.15f, 0.6f, slip) + rubber) * slipPow;
            // dirt: granular crunch + rumble
            if (nz.uni() < (200.f + 1800.f * slip) * kInvSR) grain = 0.5f + nz.uni();
            grain *= 0.993f;
            float dirt = (dirtLp.lp(w) * (0.3f + grain) + rumble.lp(w) * 1.5f) * slip * 0.5f;
            out[i] = (asphalt * (1.f - surf) + dirt * surf) * 2.f;
        }
    }
};

// =============================================================================================
struct WindSynth : EmitterSynth {
    Noise nz;
    PinkNoise pink;
    float speed = 0, tSpeed = 0;
    Svf band, rumble, whistle;
    float gust = 0, gustTarget = 0, gustTimer = 0, xPow = 0;
    int ctl = 0;
    explicit WindSynth(u32 seed) : nz(seed | 3) {}
    void setParams(float p0, float, float, float) override { tSpeed = Max(p0, 0.f); }
    void render(float* out, int n) override {
        const float c = smoothCoef(0.2f);
        for (int i = 0; i < n; i++) {
            speed += (tSpeed - speed) * c;
            float x = Clamp(speed / 60.f, 0.f, 1.5f);
            if ((ctl++ & 31) == 0) {
                gustTimer -= 32.f * kInvSR;
                if (gustTimer <= 0.f) { gustTarget = nz.range(-0.3f, 0.35f); gustTimer = nz.range(0.6f, 2.5f); }
                gust += (gustTarget - gust) * 0.02f;
                float g = 1.f + gust;
                band.setG(svfG((280.f + 1700.f * x) * g), 0.7f);
                rumble.setG(svfG(90.f + 120.f * x), 0.8f);
                whistle.setG(svfG((700.f + 1200.f * x) * (1.f + 0.5f * gust)), 9.f);
                xPow = powf(x, 1.6f);
            }
            float w = pink.process(nz.white());
            float g = 1.f + gust * 0.8f;
            float y = band.bp(w) * 1.3f + rumble.lp(w) * 1.6f + whistle.bp(w) * 0.5f * x * x;
            out[i] = y * xPow * g * 0.9f;
        }
    }
};

// =============================================================================================
struct FireSynth : EmitterSynth {
    Noise nz;
    BrownNoise brown;
    float size = 0, tSize = 0;
    OnePoleLP roarLp;
    OnePoleHP hissHp;
    Svf popBp;
    float popEnv = 0, flicker = 1, flickT = 1;
    int ctl = 0;
    explicit FireSynth(u32 seed) : nz(seed | 5) {
        hissHp.set(3500.f);
        popBp.set(2500.f, 1.5f);
    }
    void setParams(float p0, float, float, float) override { tSize = Saturate(p0); }
    void render(float* out, int n) override {
        const float c = smoothCoef(0.3f);
        for (int i = 0; i < n; i++) {
            size += (tSize - size) * c;
            if ((ctl++ & 63) == 0) {
                roarLp.set(160.f + 380.f * size);
                flickT = 0.6f + 0.8f * nz.uni();
            }
            flicker += (flickT - flicker) * 0.002f;
            float w = nz.white();
            float roar = roarLp.process(brown.process(w)) * flicker * (0.5f + size);
            float hiss = hissHp.process(w) * 0.05f * flicker;
            if (nz.uni() < (4.f + 30.f * size) * kInvSR) {
                popEnv = 0.3f + 1.2f * nz.uni() * nz.uni() * nz.uni();
                popBp.setG(svfG(nz.range(900.f, 5000.f)), 1.8f);
            }
            float pop = popBp.bp(w) * popEnv;
            popEnv *= 0.992f;
            out[i] = (roar * 1.4f + hiss + pop * 0.6f) * (0.3f + 0.7f * size) * 0.8f;
        }
    }
};

// =============================================================================================
// Helicopter: main rotor blade slap (low thump + mid burst per blade pass) over AM rotor wash,
// buzzing tail rotor and turbine whine.
struct RotorCore {
    Noise nz;
    PinkNoise pink;
    float rpm01 = 0, throttle = 0, tRpm = 0, tThr = 0;
    bool init0 = false;
    float bladePhase = 0, tailPhase = 0, turb1 = 0, turb2 = 0;
    Svf thumpBp;
    Svf slapBp, washLp, tailLp;
    float slapEnv = 0;
    BlepOsc tailOsc;
    OnePoleHP hissHp;
    float blades = 4.f, mainHz = 5.4f;
    void init(u32 seed) {
        nz.seed(seed | 11);
        Rng r(seed);
        blades = (float)r.irange(2, 5);
        mainHz = r.range(4.6f, 6.2f) * (4.f / blades);
        thumpBp.set(r.range(55.f, 80.f), 6.f);
        slapBp.set(r.range(380.f, 650.f), 1.2f);
        washLp.set(900.f, 0.6f);
        tailLp.set(1400.f, 0.8f);
        hissHp.set(4000.f);
    }
    void setParams(float r, float t) {
        tRpm = Saturate(r);
        tThr = Saturate(t);
        if (!init0) { rpm01 = tRpm; throttle = tThr; init0 = true; }
    }
    FORCEINLINE float tick() {
        rpm01 += (tRpm - rpm01) * 0.00005f;
        throttle += (tThr - throttle) * 0.0004f;
        float bp = mainHz * blades * rpm01;
        bladePhase += bp * kInvSR;
        float w = nz.white();
        float kick = 0.f;
        if (bladePhase >= 1.f) {
            bladePhase -= 1.f;
            float a = (0.35f + 0.65f * throttle) * rpm01 * rpm01 * (0.85f + 0.3f * nz.uni());
            slapEnv = a;
            kick = a * 60.f;
        }
        slapEnv *= 0.9975f;
        float slap = slapBp.bp(w) * slapEnv * 1.3f + thumpBp.bp(kick) * 0.25f;
        // rotor wash AM
        float am = 0.4f + 0.6f * expf(-bladePhase * 5.f);
        float wash = washLp.lp(pink.process(w)) * am * rpm01 * (0.5f + 0.5f * throttle) * 1.2f;
        // tail rotor
        float tf = bp * 5.2f;
        float tail = tailLp.lp(tailOsc.saw(Min(tf * kInvSR, 0.45f))) * 0.12f * rpm01;
        // turbine
        turb1 += (5600.f * (0.4f + 0.6f * rpm01)) * kInvSR;
        if (turb1 >= 1.f) turb1 -= 1.f;
        turb2 += (1830.f * (0.4f + 0.6f * rpm01)) * kInvSR;
        if (turb2 >= 1.f) turb2 -= 1.f;
        float turb = (sinWrapped(turb1) * 0.02f + sinWrapped(turb2) * 0.025f) * SmoothStep(0.f, 0.3f, rpm01) +
                     hissHp.process(w) * 0.03f * rpm01;
        return (slap + wash + tail + turb) * 0.6f;
    }
};

struct RotorSynth : EmitterSynth {
    RotorCore core;
    explicit RotorSynth(u32 seed) { core.init(seed); }
    void setParams(float p0, float p1, float, float) override { core.setParams(p0, p1); }
    void render(float* out, int n) override {
        for (int i = 0; i < n; i++) out[i] = core.tick();
    }
};

// =============================================================================================
// Propeller plane: radial engine pulses + blade-pass buzz.
static const EngineSpec kRadialSpec = {9, false, {0, 80, 160, 240, 320, 400, 480, 560, 640}, {0}, 650, 2700, 0.08f, 2.f, 0.1f, 1.4f,
                                       {95, 260, 700}, {1.0f, 1.5f, 1.8f}, {1.0f, 0.6f, 0.4f}, 0.35f, 1900, 0.2f, 500, 0.06f, 0.f, 0.2f, 0.5f, 1.0f, 0.45f, 0.f, 0.f};
struct PropSynth : EmitterSynth {
    EngineCore eng;
    Noise nz;
    BlepOsc prop;
    Svf propLp, washBp;
    float bladePhase = 0, rpm01 = 0, thr = 0;
    explicit PropSynth(u32 seed) : nz(seed | 13) {
        eng.init(kRadialSpec, seed);
        propLp.set(700.f, 0.8f);
        washBp.set(900.f, 0.7f);
    }
    void setParams(float p0, float p1, float, float) override {
        rpm01 = Saturate(p0);
        thr = Saturate(p1);
        eng.setParams(p0, p1, p1);
    }
    void render(float* out, int n) override {
        eng.render(out, n);
        for (int i = 0; i < n; i++) {
            float rpm = eng.rpmHz();
            float bp = rpm / 60.f * 3.f;
            bladePhase += bp * kInvSR;
            if (bladePhase >= 1.f) bladePhase -= 1.f;
            float buzz = propLp.lp(prop.saw(Min(bp * kInvSR, 0.45f)));
            float am = 0.5f + 0.5f * sinWrapped(bladePhase);
            float wash = washBp.bp(nz.white()) * am;
            out[i] = out[i] * 0.8f + (buzz * 0.35f + wash * 0.25f) * (0.3f + 0.7f * eng.rpm01) * (0.5f + 0.5f * eng.throttle);
        }
    }
};

// =============================================================================================
struct JetSynth : EmitterSynth {
    Noise nz;
    PinkNoise pink;
    BrownNoise brown;
    float rpm01 = 0, thr = 0, tRpm = 0, tThr = 0;
    bool init0 = false;
    float fanPh = 0, fanPh2 = 0, compPh = 0;
    Svf roarLp, roarBp, rumbleLp;
    OnePoleHP hissHp;
    OnePoleLP abLp;
    float popEnv = 0;
    int ctl = 0;
    explicit JetSynth(u32 seed) : nz(seed | 17) {
        hissHp.set(5000.f);
        abLp.set(1200.f);
    }
    void setParams(float p0, float p1, float, float) override {
        tRpm = Saturate(p0);
        tThr = Saturate(p1);
        if (!init0) { rpm01 = tRpm; thr = tThr; init0 = true; }
    }
    void render(float* out, int n) override {
        for (int i = 0; i < n; i++) {
            rpm01 += (tRpm - rpm01) * 0.00008f;
            thr += (tThr - thr) * 0.0003f;
            if ((ctl++ & 31) == 0) {
                roarLp.setG(svfG(350.f + 1800.f * thr), 0.6f);
                roarBp.setG(svfG(180.f + 250.f * thr), 0.5f);
                rumbleLp.setG(svfG(110.f), 0.7f);
            }
            float w = nz.white();
            float p = pink.process(w);
            float ff = 220.f + 2900.f * rpm01;
            fanPh += ff * kInvSR; if (fanPh >= 1.f) fanPh -= 1.f;
            fanPh2 += ff * 2.01f * kInvSR; if (fanPh2 >= 1.f) fanPh2 -= 1.f;
            compPh += (4800.f + 6200.f * rpm01) * kInvSR; if (compPh >= 1.f) compPh -= 1.f;
            float fan = (sinWrapped(fanPh) + 0.35f * sinWrapped(fanPh2)) * 0.05f * rpm01;
            float comp = sinWrapped(compPh) * 0.02f * rpm01;
            float roar = (roarLp.lp(p) * 1.3f + roarBp.bp(p)) * (0.15f + thr * thr) * (0.3f + 0.7f * rpm01);
            float rumble = rumbleLp.lp(brown.process(w)) * (0.2f + thr) * rpm01;
            float hiss = hissHp.process(w) * 0.04f * rpm01;
            if (thr > 0.92f && nz.uni() < 45.f * kInvSR) popEnv = 0.5f + nz.uni();
            float ab = abLp.process(w) * popEnv * 0.5f;
            popEnv *= 0.995f;
            out[i] = (fan + comp + roar * 1.2f + rumble + hiss + ab) * 0.42f;
        }
    }
};

// =============================================================================================
static const EngineSpec kOutboardSpec = {3, true, {0, 120, 240}, {0}, 750, 6000, 0.06f, 1.5f, 0.1f, 0.8f,
                                         {150, 420, 1100}, {1.2f, 1.8f, 2.2f}, {1.0f, 0.6f, 0.4f}, 0.35f, 3000, 0.25f, 900, 0.05f, 0.f, 0.1f, 0.35f, 0.9f, 0.45f, 0.f, 0.f};
struct BoatSynth : EmitterSynth {
    EngineCore eng;
    Noise nz;
    float inWater = 1, tInWater = 1;
    Svf waterLp, cavHp;
    float bubble = 1, bubbleT = 1;
    float gearPh = 0;
    int ctl = 0;
    explicit BoatSynth(u32 seed) : nz(seed | 19) {
        eng.init(kOutboardSpec, seed);
        waterLp.set(750.f, 0.8f);
        cavHp.set(2500.f, 0.7f);
    }
    void setParams(float p0, float p1, float p2, float) override {
        eng.setParams(p0, p1, p1 * 0.8f + 0.2f);
        tInWater = Saturate(p2);
    }
    void render(float* out, int n) override {
        eng.render(out, n);
        for (int i = 0; i < n; i++) {
            inWater += (tInWater - inWater) * 0.001f;
            if ((ctl++ & 63) == 0) bubbleT = 0.55f + 0.9f * nz.uni();
            bubble += (bubbleT - bubble) * 0.02f;
            float e = out[i];
            float wet = waterLp.lp(e) * 1.5f * bubble;
            float dry = e * 1.3f;
            float cav = cavHp.hp(nz.white()) * 0.05f * eng.throttle * eng.rpm01 * inWater;
            gearPh += eng.rpmHz() * 0.36f * kInvSR;
            if (gearPh >= 1.f) gearPh -= 1.f;
            float gear = sinWrapped(gearPh) * 0.012f * eng.rpm01;
            out[i] = wet * inWater + dry * (1.f - inWater) + cav + gear;
        }
    }
};

// =============================================================================================
struct WakeSynth : EmitterSynth {
    Noise nz;
    PinkNoise pink;
    float speed = 0, tSpeed = 0;
    Svf rush, swash;
    Svf slapBpf;
    Svf splashBp;
    float splashEnv = 0, xPow = 0;
    int ctl = 0;
    explicit WakeSynth(u32 seed) : nz(seed | 23) {
        swash.set(300.f, 0.7f);
        slapBpf.set(85.f, 8.f);
        splashBp.set(2200.f, 0.8f);
    }
    void setParams(float p0, float, float, float) override { tSpeed = Max(p0, 0.f); }
    void render(float* out, int n) override {
        const float c = smoothCoef(0.25f);
        for (int i = 0; i < n; i++) {
            speed += (tSpeed - speed) * c;
            float x = Clamp(speed / 25.f, 0.f, 1.2f);
            if ((ctl++ & 31) == 0) {
                rush.setG(svfG(450.f + 1500.f * x), 0.55f);
                xPow = powf(x, 1.3f);
            }
            float w = nz.white();
            float p = pink.process(w);
            float kick = 0.f;
            if (nz.uni() < (0.4f + 3.f * x) * kInvSR) {
                float a = (0.4f + 0.6f * nz.uni()) * x;
                kick = a * 60.f;
                splashEnv = Max(splashEnv, a);
            }
            splashEnv *= 0.9996f;
            float y = rush.bp(p) * xPow * 1.4f + swash.lp(p) * x * 1.2f + slapBpf.bp(kick) * 0.2f +
                      splashBp.bp(w) * splashEnv * 0.6f;
            out[i] = y * 0.7f;
        }
    }
};

// =============================================================================================
struct AlarmSynth : EmitterSynth {
    float t = 0, phase = 0, freq = 1000;
    Biquad piezo, hp;
    u32 seed;
    int order[5] = {0, 1, 2, 3, 4};
    explicit AlarmSynth(u32 s) : seed(s) {
        piezo.setPeak(2100.f, 1.2f, 9.f);
        hp.setHP(500.f, 0.7f);
        Rng r(s);
        for (int i = 4; i > 0; i--) std::swap(order[i], order[r.irange(0, i)]);
    }
    void setParams(float, float, float, float) override {}
    void render(float* out, int n) override {
        for (int i = 0; i < n; i++) {
            t += kInvSR;
            if (t >= 15.f) t -= 15.f;
            float cyc = t;
            int ph = order[(int)(cyc / 3.f) % 5];
            float lt = fmodf(cyc, 3.f);
            float f, gate = 1.f;
            switch (ph) {
                case 0: { float c = fmodf(lt, 0.36f); f = 800.f + 1000.f * (c / 0.36f); break; }
                case 1: f = fmodf(lt, 0.36f) < 0.18f ? 1400.f : 950.f; break;
                case 2: f = 2200.f; gate = fmodf(lt, 0.1f) < 0.055f ? 1.f : 0.f; break;
                case 3: f = 1300.f + 400.f * sinCycle(lt * 6.f); break;
                default: { float c = fmodf(lt, 1.f); f = 900.f + 700.f * sinf(c * kPi); break; }
            }
            freq += (f - freq) * 0.05f;
            phase += freq * kInvSR;
            if (phase >= 1.f) phase -= 1.f;
            float v = phase < 0.5f ? 1.f : -1.f;
            float y = piezo.process(hp.process(v)) * gate;
            out[i] = fastTanh(y * 0.8f) * 0.25f;
        }
    }
};

// =============================================================================================
// Crowd: babble of formant-filtered voices; panic raises pitch/rate and adds screams.
struct KlattRes {
    float a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
    void set(float f, float bw) {
        float T = kInvSR;
        c = -expf(-kTwoPi * bw * T);
        b = 2.f * expf(-kPi * bw * T) * cosf(kTwoPi * f * T);
        a = 1.f - b - c;
    }
    FORCEINLINE float process(float x) {
        float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct Talker {
    float f0 = 120, f0Base = 120, phase = 0;
    float env = 0, envTarget = 0;
    float timer = 0;
    bool voiced = false;
    bool screaming = false;
    KlattRes f1, f2;
    float consonant = 0;
    float vibPh = 0;
    bool female = false;
};

struct CrowdSynth : EmitterSynth {
    static constexpr int kTalkers = 10;
    Talker tk[kTalkers];
    Noise nz;
    float density = 0, panic = 0, tDen = 0, tPanic = 0;
    OnePoleLP outLp;
    OnePoleHP outHp;
    int pendingScream = -1;
    float screamTimer = 2.f;
    int ctl = 0;
    explicit CrowdSynth(u32 seed) : nz(seed | 29) {
        for (int i = 0; i < kTalkers; i++) {
            tk[i].female = (i & 1) != 0;
            tk[i].f0Base = tk[i].female ? nz.range(175.f, 240.f) : nz.range(95.f, 140.f);
            tk[i].f0 = tk[i].f0Base;
            tk[i].timer = nz.range(0.f, 1.f);
            tk[i].f1.set(600, 90);
            tk[i].f2.set(1400, 120);
        }
        outLp.set(3800.f);
        outHp.set(120.f);
    }
    void setParams(float p0, float p1, float, float) override {
        tDen = Saturate(p0);
        tPanic = Saturate(p1);
    }
    void nextSyllable(Talker& t, int idx) {
        static const float kVowF1[6] = {730, 530, 270, 570, 300, 660};
        static const float kVowF2[6] = {1090, 1840, 2290, 840, 870, 1720};
        int active = 2 + (int)(density * 8.f);
        bool on = idx < active;
        if (!on) { t.voiced = false; t.envTarget = 0.f; t.timer = 0.3f; return; }
        if (t.voiced && nz.uni() < 0.35f - 0.2f * panic) {
            // gap
            t.voiced = false;
            t.envTarget = 0.f;
            t.timer = nz.uni() < 0.12f ? nz.range(0.6f, 2.0f) * (1.f - 0.6f * panic) : nz.range(0.03f, 0.15f);
            return;
        }
        t.voiced = true;
        t.screaming = panic > 0.3f && nz.uni() < panic * 0.35f;
        int v = (int)(nz.uni() * 6.f) % 6;
        float fs = t.female ? 1.17f : 1.f;
        if (t.screaming) v = 0;
        t.f1.set(kVowF1[v] * fs * nz.range(0.92f, 1.08f), 80.f);
        t.f2.set(kVowF2[v] * fs * nz.range(0.92f, 1.08f), 110.f);
        float pitchUp = 1.f + panic * (t.screaming ? 1.8f : 0.5f);
        t.f0 = t.f0Base * pitchUp * nz.range(0.85f, 1.2f);
        t.envTarget = t.screaming ? 1.6f : nz.range(0.5f, 1.f);
        t.timer = t.screaming ? nz.range(0.4f, 1.0f) : nz.range(0.08f, 0.22f) * (1.f - 0.35f * panic);
        t.consonant = nz.uni() < 0.6f ? 1.f : 0.f;
    }
    void render(float* out, int n) override {
        const float c = smoothCoef(0.4f);
        for (int i = 0; i < n; i++) {
            density += (tDen - density) * c;
            panic += (tPanic - panic) * c;
            float sum = 0;
            for (int k = 0; k < kTalkers; k++) {
                Talker& t = tk[k];
                t.timer -= kInvSR;
                if (t.timer <= 0.f) nextSyllable(t, k);
                t.env += (t.envTarget - t.env) * 0.004f;
                if (t.env < 1e-4f && !t.voiced) continue;
                float f = t.f0;
                if (t.screaming) {
                    t.vibPh += 5.5f * kInvSR;
                    if (t.vibPh >= 1.f) t.vibPh -= 1.f;
                    f *= 1.f + 0.03f * sinWrapped(t.vibPh);
                }
                float dt = f * kInvSR;
                t.phase += dt;
                if (t.phase >= 1.f) t.phase -= 1.f;
                float src = (2.f * t.phase - 1.f) - polyBlep(t.phase, dt);
                src += nz.white() * 0.15f;
                float cons = t.consonant * nz.white() * 0.6f;
                t.consonant *= 0.996f;
                float v = t.f2.process(t.f1.process(src)) + cons * 0.3f;
                sum += v * t.env;
            }
            float y = outHp.process(outLp.process(sum));
            out[i] = y * (0.035f + 0.04f * panic) * (0.4f + 0.6f * density);
        }
        // occasional scream one-shots in panic
        if (panic > 0.4f && density > 0.1f) {
            screamTimer -= (float)n * kInvSR;
            if (screamTimer <= 0.f) {
                pendingScream = nz.uni() < 0.5f ? SFX_SCREAM_FEMALE : SFX_SCREAM_MALE;
                screamTimer = nz.range(1.5f, 5.f) / (0.3f + panic * density);
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------
static const EmitterDef kEmitterDefs[EMIT_COUNT] = {
    // refDist, maxDist, reverb, gain, priority
    {6.f, 260.f, 0.25f, 1.0f, 150},   // ENGINE
    {10.f, 1100.f, 0.45f, 1.0f, 220}, // SIREN
    {6.f, 450.f, 0.35f, 1.0f, 170},   // HORN
    {5.f, 220.f, 0.25f, 0.9f, 140},   // TIRE_SKID
    {1.f, 20.f, 0.0f, 0.8f, 120},     // WIND_RUSH
    {4.f, 150.f, 0.25f, 1.0f, 120},   // FIRE
    {25.f, 2200.f, 0.4f, 1.2f, 200},  // ROTOR
    {20.f, 2000.f, 0.35f, 1.1f, 190}, // PROP
    {35.f, 3500.f, 0.4f, 1.2f, 200},  // JET
    {6.f, 380.f, 0.3f, 1.0f, 150},    // BOAT
    {4.f, 120.f, 0.2f, 0.9f, 110},    // WATER_WAKE
    {8.f, 380.f, 0.4f, 0.9f, 160},    // ALARM
    {5.f, 160.f, 0.35f, 0.9f, 130},   // RADIO_WORLD
    {10.f, 250.f, 0.4f, 1.0f, 120},   // CROWD
};

}  // namespace emit

const EmitterDef& emitterDef(EmitterType t) { return emit::kEmitterDefs[Clamp((int)t, 0, EMIT_COUNT - 1)]; }

EmitterSynth* constructEmitterSynth(EmitterType type, void* storage, u32 seed) {
    using namespace emit;
    static_assert(sizeof(EngineSynth) <= kEmitterStorage, "EngineSynth too large");
    static_assert(sizeof(CrowdSynth) <= kEmitterStorage, "CrowdSynth too large");
    static_assert(sizeof(PropSynth) <= kEmitterStorage, "PropSynth too large");
    static_assert(sizeof(BoatSynth) <= kEmitterStorage, "BoatSynth too large");
    switch (type) {
        case EMIT_ENGINE: return new (storage) EngineSynth(seed);
        case EMIT_SIREN: return new (storage) SirenSynth();
        case EMIT_HORN: return new (storage) HornSynth();
        case EMIT_TIRE_SKID: return new (storage) SkidSynth(seed);
        case EMIT_WIND_RUSH: return new (storage) WindSynth(seed);
        case EMIT_FIRE: return new (storage) FireSynth(seed);
        case EMIT_ROTOR: return new (storage) RotorSynth(seed);
        case EMIT_PROP: return new (storage) PropSynth(seed);
        case EMIT_JET: return new (storage) JetSynth(seed);
        case EMIT_BOAT: return new (storage) BoatSynth(seed);
        case EMIT_WATER_WAKE: return new (storage) WakeSynth(seed);
        case EMIT_ALARM: return new (storage) AlarmSynth(seed);
        case EMIT_CROWD: return new (storage) CrowdSynth(seed);
        default: return nullptr;
    }
}

// Crowd synth one-shot polling (screams) - used by the mixer.
int emitterPollOneShot(EmitterType type, EmitterSynth* s) {
    if (type == EMIT_CROWD && s) {
        auto* c = static_cast<emit::CrowdSynth*>(s);
        int r = c->pendingScream;
        c->pendingScream = -1;
        return r;
    }
    return -1;
}

}  // namespace detail
}  // namespace Audio
