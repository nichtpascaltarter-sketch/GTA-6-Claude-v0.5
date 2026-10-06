// Ambient beds driven by Audio::Ambience: city rumble with passing traffic, birds/cicadas by day,
// crickets/frogs/owls by night, surf and gulls on the coast, wetland insects, rain (with roof
// drumming inside vehicles) and thunder, gusting wind, and an underwater drone. Continuous layers
// are synthesized in real time; sparse events are spawned as positional one-shots around the
// listener. All layer gains crossfade smoothly.
#include "audio_internal.h"

namespace Audio {
namespace detail {

using namespace dsp;

namespace amb {

struct PassBy {
    bool active = false;
    float t = 0, dur = 3, pan0 = -1, pan1 = 1, gain = 0.5f, f0 = 800, engF = 80, engPh = 0;
    Svf tireBp;
    OnePoleLP engLp;
    PinkNoise pink;
};

struct Cricket {
    float f = 4500, ph = 0, pulsePh = 0, timer = 0, pan = 0, amp = 0.5f, pulseRate = 28, gate = 0;
    int pulsesLeft = 0;
};

struct Buzz {
    float f = 450, ph = 0, amPh = 0, amRate = 0.3f, pan = 0, amp = 0.5f;
};

struct Wave {
    float u = 0, period = 9, pan = 0, amp = 1;
    Svf lpL, lpR, hpL, hpR;
    PinkNoise pl, pr;
};

}  // namespace amb

struct AmbienceRenderer {
    Ambience s;  // smoothed targets
    bool first = true;
    Noise nz{0xA3B1C2D3u};
    // gains (smoothed)
    float gCity = 0, gDay = 0, gNight = 0, gCoast = 0, gWet = 0, gRain = 0, gWind = 0, gUnder = 0, gCicada = 0;
    // city
    BrownNoise brL, brR;
    PinkNoise pkL, pkR;
    Svf cityLpL, cityLpR, cityMidL, cityMidR;
    float cityMod = 1, cityModT = 1;
    amb::PassBy pass[4];
    float passTimer = 1.f;
    // nature
    amb::Cricket crickets[6];
    float cicadaSwell = 0, cicadaTarget = 0.5f, cicadaTimer = 0;
    Svf cicadaBpL, cicadaBpR;
    float cicadaAmPh = 0;
    PinkNoise leafPink;
    Svf leafHpL, leafHpR;
    // wetland
    amb::Buzz buzz[5];
    Svf wetLp;
    // coast
    amb::Wave waves[3];
    BrownNoise surfBr;
    Svf surfLp;
    // wind
    PinkNoise windPkL, windPkR;
    Svf windBpL, windBpR, windLpL, windLpR, whistle;
    float gust = 0, gustT = 0, gustTimer = 0;
    // rain
    PinkNoise rainPkL, rainPkR;
    Svf rainHpL, rainHpR, rainLpL, rainLpR, patterBp;
    Svf dropL, dropR, roofL, roofR;
    float dropEnvL = 0, dropEnvR = 0;
    // underwater
    BrownNoise uwBr;
    Svf uwLp;
    float uwPh = 0;
    // spawn timers
    float tBird = 2, tGull = 3, tFrog = 2, tOwl = 20, tHeron = 30, tBullfrog = 8, tCrow = 25, tHorn = 8, tSiren = 30,
          tDog = 25, tThunder = 20, tBubble = 1, tLap = 3, tCricketPos = 2;
    int ctl = 0;
    ListenerState ls;

    AmbienceRenderer() {
        for (int i = 0; i < 6; i++) {
            crickets[i].f = nz.range(3900.f, 5400.f);
            crickets[i].pan = nz.range(-0.9f, 0.9f);
            crickets[i].amp = nz.range(0.3f, 1.f);
            crickets[i].pulseRate = nz.range(22.f, 34.f);
            crickets[i].timer = nz.range(0.f, 1.f);
        }
        for (int i = 0; i < 5; i++) {
            buzz[i].f = nz.range(380.f, 720.f);
            buzz[i].pan = nz.range(-0.8f, 0.8f);
            buzz[i].amRate = nz.range(0.15f, 0.6f);
            buzz[i].amPh = nz.uni();
            buzz[i].amp = nz.range(0.3f, 1.f);
        }
        for (int i = 0; i < 3; i++) {
            waves[i].u = nz.uni();
            waves[i].period = nz.range(7.f, 12.f);
            waves[i].pan = -0.7f + 0.7f * (float)i;
            waves[i].amp = nz.range(0.6f, 1.f);
        }
        cicadaBpL.set(5200.f, 3.f);
        cicadaBpR.set(5600.f, 3.f);
        leafHpL.set(2500.f, 0.6f);
        leafHpR.set(2800.f, 0.6f);
        wetLp.set(2000.f, 0.7f);
        surfLp.set(220.f, 0.7f);
        whistle.set(900.f, 8.f);
        rainHpL.set(500.f, 0.7f);
        rainHpR.set(520.f, 0.7f);
        patterBp.set(1500.f, 0.5f);
        dropL.set(3000.f, 6.f);
        dropR.set(3500.f, 6.f);
        roofL.set(700.f, 4.f);
        roofR.set(760.f, 4.f);
        uwLp.set(260.f, 0.7f);
        cityMidL.set(480.f, 0.5f);
        cityMidR.set(520.f, 0.5f);
    }

    vec3 randomAround(float dmin, float dmax, float zmin, float zmax) {
        float a = nz.uni() * kTwoPi;
        float d = nz.range(dmin, dmax);
        return ls.pos + vec3(cosf(a) * d, sinf(a) * d, nz.range(zmin, zmax));
    }

    // Control-rate update: smoothing, filter coefficients, event spawning.
    void control(float dt, const Ambience& tgt) {
        if (first) {
            s = tgt;
            first = false;
        }
        float k = 1.f - expf(-dt / 1.5f);
        s.urban += (tgt.urban - s.urban) * k;
        s.nature += (tgt.nature - s.nature) * k;
        s.coast += (tgt.coast - s.coast) * k;
        s.wetland += (tgt.wetland - s.wetland) * k;
        s.rain += (tgt.rain - s.rain) * k;
        s.wind += (tgt.wind - s.wind) * k;
        s.underwater += (tgt.underwater - s.underwater) * (1.f - expf(-dt / 0.15f));
        // time of day: follow directly (handle wrap)
        s.timeOfDay = tgt.timeOfDay;
        float t = fmodf(Max(0.f, s.timeOfDay), 24.f);
        float day = SmoothStep(5.5f, 7.f, t) * (1.f - SmoothStep(19.f, 20.5f, t));
        float night = 1.f - day;
        float dawn = SmoothStep(5.f, 6.f, t) * (1.f - SmoothStep(7.5f, 9.f, t));
        float cic = SmoothStep(9.f, 11.f, t) * (1.f - SmoothStep(17.5f, 19.f, t));
        float dry = 1.f - s.underwater;
        float rainDamp = 1.f - 0.7f * s.rain;  // animals hide in rain
        float kg = 1.f - expf(-dt / 0.8f);
        gCity += (s.urban * (0.65f + 0.35f * day) * dry - gCity) * kg;
        gDay += (s.nature * day * dry * rainDamp - gDay) * kg;
        gNight += (Max(s.nature, s.wetland) * night * dry * rainDamp - gNight) * kg;
        gCicada += (Max(s.nature * 0.6f, s.wetland) * cic * dry * rainDamp - gCicada) * kg;
        gCoast += (s.coast * dry - gCoast) * kg;
        gWet += (s.wetland * dry - gWet) * kg;
        gRain += (s.rain * dry - gRain) * kg;
        gWind += (s.wind * dry - gWind) * kg;
        gUnder += (s.underwater - gUnder) * kg;

        // filters
        float cutoff = 110.f + 60.f * s.urban;
        cityLpL.set(cutoff, 0.6f);
        cityLpR.set(cutoff * 1.07f, 0.6f);
        cityModT += (nz.range(0.75f, 1.25f) - cityModT) * 0.05f;
        gustTimer -= dt;
        if (gustTimer <= 0.f) {
            gustT = nz.range(-0.35f, 0.45f) * (0.4f + s.wind);
            gustTimer = nz.range(0.8f, 3.f);
        }
        gust += (gustT - gust) * (1.f - expf(-dt / 0.7f));
        float wf = (250.f + 700.f * s.wind) * (1.f + 0.5f * gust);
        windBpL.set(wf, 0.55f);
        windBpR.set(wf * 1.1f, 0.55f);
        windLpL.set(120.f + 80.f * s.wind, 0.7f);
        windLpR.set(125.f + 80.f * s.wind, 0.7f);
        whistle.set((650.f + 900.f * s.wind) * (1.f + 0.6f * gust), 9.f);
        float rlp = 3500.f + 6000.f * s.rain;
        rainLpL.set(rlp, 0.6f);
        rainLpR.set(rlp * 1.05f, 0.6f);

        // cicada swell
        cicadaTimer -= dt;
        if (cicadaTimer <= 0.f) {
            cicadaTarget = nz.range(0.1f, 1.f);
            cicadaTimer = nz.range(3.f, 9.f);
        }
        cicadaSwell += (cicadaTarget - cicadaSwell) * (1.f - expf(-dt / 2.f));

        // traffic pass-bys
        passTimer -= dt;
        if (passTimer <= 0.f) {
            float rate = 0.05f + 0.9f * s.urban * (0.6f + 0.4f * day);
            passTimer = nz.range(0.4f, 2.2f) / Max(rate, 0.05f);
            if (s.urban > 0.08f && dry > 0.5f) {
                for (auto& p : pass) {
                    if (p.active) continue;
                    p.active = true;
                    p.t = 0;
                    p.dur = nz.range(2.5f, 5.5f);
                    bool ltr = nz.uni() < 0.5f;
                    float spread = nz.range(0.4f, 0.95f);
                    p.pan0 = ltr ? -spread : spread;
                    p.pan1 = -p.pan0;
                    p.gain = nz.range(0.25f, 1.f);
                    p.f0 = nz.range(600.f, 1100.f);
                    p.engF = nz.range(55.f, 120.f);
                    p.engLp.set(400.f);
                    break;
                }
            }
        }

        // positional one-shots
        auto tick = [&](float& timer, float rate, float jitter) -> bool {
            if (rate <= 1e-4f) { timer = Max(timer, 1.f); return false; }
            timer -= dt;
            if (timer > 0.f) return false;
            timer = (1.f / rate) * nz.range(1.f - jitter, 1.f + jitter);
            return true;
        };
        float birds = gDay * (1.f + 2.5f * dawn);
        if (tick(tBird, 0.45f * birds, 0.8f))
            spawnWorldOneShot(nz.uni() < 0.4f ? (int)AMB_BIRD_SONG : (int)SFX_BIRD_CHIRP, randomAround(8.f, 45.f, 3.f, 14.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tCrow, 0.03f * gDay, 0.5f)) spawnWorldOneShot(AMB_CROW, randomAround(30.f, 120.f, 5.f, 20.f), 0.8f, 1.f);
        if (tick(tGull, 0.22f * gCoast * (0.3f + 0.7f * day), 0.7f))
            spawnWorldOneShot(SFX_SEAGULL, randomAround(15.f, 70.f, 6.f, 25.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tOwl, 0.025f * gNight * s.nature, 0.5f)) spawnWorldOneShot(AMB_OWL, randomAround(40.f, 150.f, 6.f, 20.f), 0.8f, 1.f);
        if (tick(tFrog, 0.6f * gNight * (0.3f * s.nature + s.wetland) + 0.2f * gWet, 0.8f))
            spawnWorldOneShot(AMB_TREEFROG, randomAround(6.f, 40.f, 0.f, 3.f), nz.range(0.4f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tBullfrog, 0.12f * gWet * (0.4f + 0.6f * night), 0.6f))
            spawnWorldOneShot(AMB_BULLFROG, randomAround(10.f, 60.f, 0.f, 1.f), nz.range(0.6f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tHeron, 0.02f * gWet * day, 0.5f)) spawnWorldOneShot(AMB_HERON, randomAround(40.f, 150.f, 1.f, 10.f), 0.7f, 1.f);
        if (tick(tLap, 0.35f * Max(gWet, gCoast * 0.5f), 0.6f))
            spawnWorldOneShot(AMB_WATER_LAP, randomAround(4.f, 18.f, -1.f, 0.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tCricketPos, 0.5f * gNight, 0.8f))
            spawnWorldOneShot(AMB_CRICKET_CHIRP, randomAround(2.f, 12.f, 0.f, 1.f), nz.range(0.5f, 1.f), nz.range(0.95f, 1.05f));
        if (tick(tHorn, 0.09f * gCity, 0.8f))
            spawnWorldOneShot(AMB_HORN_DISTANT, randomAround(60.f, 350.f, 0.f, 5.f), nz.range(0.5f, 1.f), nz.range(0.95f, 1.05f));
        if (tick(tSiren, 0.012f * gCity * (0.5f + 0.5f * night + 0.3f), 0.5f))
            spawnWorldOneShot(AMB_SIREN_DISTANT, randomAround(300.f, 1200.f, 0.f, 10.f), 1.f, nz.range(0.97f, 1.03f));
        if (tick(tDog, 0.02f * (gCity * 0.6f + gNight * 0.5f), 0.6f))
            spawnWorldOneShot(AMB_DOG_DISTANT, randomAround(60.f, 300.f, 0.f, 3.f), nz.range(0.5f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tThunder, s.rain > 0.55f ? 0.04f * (s.rain - 0.5f) * 2.f : 0.f, 0.7f))
            spawnWorldOneShot(SFX_THUNDER, randomAround(1500.f, 6000.f, 400.f, 1500.f), 1.f, nz.range(0.85f, 1.1f));
        if (tick(tBubble, 1.2f * gUnder, 0.8f)) spawnWorldOneShot(AMB_BUBBLES, randomAround(0.5f, 3.f, -1.f, 1.f), nz.range(0.5f, 1.f), 1.f);
    }

    void render(float* L, float* R, int n, const Ambience& tgt, const ListenerState& lsIn) {
        ls = lsIn;
        control((float)n * kInvSR, tgt);
        const float inVeh = ls.inVehicle;
        for (int i = 0; i < n; i++) {
            float l = 0.f, r = 0.f;
            // ---- city
            if (gCity > 1e-4f) {
                if ((i & 63) == 0) cityMod += (cityModT - cityMod) * 0.02f;
                float wl = nz.white(), wr = nz.white();
                float rl = cityLpL.lp(brL.process(wl)), rr = cityLpR.lp(brR.process(wr));
                float ml = cityMidL.bp(pkL.process(wl)), mr = cityMidR.bp(pkR.process(wr));
                l += (rl * 0.16f + ml * 0.2f * cityMod) * gCity;
                r += (rr * 0.16f + mr * 0.2f * cityMod) * gCity;
                for (auto& p : pass) {
                    if (!p.active) continue;
                    p.t += kInvSR;
                    float u = p.t / p.dur;
                    if (u >= 1.f) { p.active = false; continue; }
                    float x = (u - 0.5f) * 5.f;
                    float env = 1.f / sqrtf(1.f + x * x) - 0.19f;
                    env = Max(env, 0.f) * 1.25f;
                    float dop = 1.f - 0.08f * (u - 0.5f) * 2.f;
                    if ((i & 15) == 0) p.tireBp.setG(svfG(p.f0 * dop), 0.9f);
                    float tire = p.tireBp.bp(p.pink.process(nz.white()));
                    p.engPh += p.engF * dop * kInvSR;
                    if (p.engPh >= 1.f) p.engPh -= 1.f;
                    float eng = p.engLp.process(2.f * p.engPh - 1.f) * 0.5f;
                    float v = (tire * 0.5f + eng * 0.12f) * env * p.gain * gCity;
                    float pan = p.pan0 + (p.pan1 - p.pan0) * u;
                    l += v * (0.5f - 0.45f * pan);
                    r += v * (0.5f + 0.45f * pan);
                }
            }
            // ---- nature day: leaves
            if (gDay > 1e-4f) {
                float w = leafPink.process(nz.white());
                float lv = (0.3f + 0.7f * s.wind) * (1.f + gust) * gDay * 0.05f;
                l += leafHpL.hp(w) * lv;
                r += leafHpR.hp(nz.white() * 0.5f + w * 0.5f) * lv;
            }
            // ---- cicadas
            if (gCicada > 1e-4f) {
                cicadaAmPh += 95.f * kInvSR;
                if (cicadaAmPh >= 1.f) cicadaAmPh -= 1.f;
                float am = 0.6f + 0.4f * sinWrapped(cicadaAmPh);
                float g = gCicada * cicadaSwell * am * 0.06f;
                l += cicadaBpL.bp(nz.white()) * g;
                r += cicadaBpR.bp(nz.white()) * g;
            }
            // ---- crickets (night)
            if (gNight > 1e-4f) {
                for (auto& c : crickets) {
                    c.timer -= kInvSR;
                    if (c.timer <= 0.f && c.pulsesLeft == 0) {
                        c.pulsesLeft = 3 + (int)(nz.uni() * 3.f);
                        c.pulsePh = 0.f;
                        c.timer = nz.range(0.35f, 0.9f);
                    }
                    if (c.pulsesLeft > 0) {
                        c.pulsePh += c.pulseRate * kInvSR;
                        if (c.pulsePh >= 1.f) {
                            c.pulsePh -= 1.f;
                            c.pulsesLeft--;
                        }
                        float g = c.pulsePh < 0.45f ? sinf(c.pulsePh / 0.45f * kPi) : 0.f;
                        c.gate = g;
                    } else {
                        c.gate *= 0.99f;
                    }
                    if (c.gate > 1e-4f) {
                        c.ph += c.f * kInvSR;
                        if (c.ph >= 1.f) c.ph -= 1.f;
                        float v = sinWrapped(c.ph) * c.gate * c.amp * gNight * 0.022f;
                        l += v * (0.5f - 0.4f * c.pan);
                        r += v * (0.5f + 0.4f * c.pan);
                    }
                }
            }
            // ---- wetland insects
            if (gWet > 1e-4f) {
                float sl = 0.f, sr = 0.f;
                for (auto& bz : buzz) {
                    bz.ph += bz.f * kInvSR;
                    if (bz.ph >= 1.f) bz.ph -= 1.f;
                    bz.amPh += bz.amRate * kInvSR;
                    if (bz.amPh >= 1.f) {
                        bz.amPh -= 1.f;
                        bz.f = nz.range(380.f, 720.f);
                    }
                    float am = sinWrapped(bz.amPh);
                    am = am > 0.f ? am * am : 0.f;
                    float tri = 4.f * fabsf(bz.ph - 0.5f) - 1.f;
                    float v = tri * am * bz.amp;
                    sl += v * (0.5f - 0.4f * bz.pan);
                    sr += v * (0.5f + 0.4f * bz.pan);
                }
                l += wetLp.lp(sl) * gWet * 0.012f;
                r += sr * gWet * 0.012f;
            }
            // ---- coast surf
            if (gCoast > 1e-4f) {
                for (auto& w : waves) {
                    w.u += kInvSR / w.period;
                    if (w.u >= 1.f) {
                        w.u -= 1.f;
                        w.period = nz.range(7.f, 12.f);
                        w.amp = nz.range(0.5f, 1.f);
                    }
                    float u = w.u, g, fc;
                    if (u < 0.25f) { float x = u / 0.25f; g = 0.15f + 0.6f * x * x; fc = 450.f + 1500.f * x; }
                    else if (u < 0.31f) { g = 1.f; fc = 5500.f; }
                    else { float x = (u - 0.31f); g = expf(-x * 5.5f) * 0.9f + 0.08f; fc = 1400.f + 4000.f * expf(-x * 4.f); }
                    if ((i & 31) == 0) {
                        float gg = svfG(fc);
                        w.lpL.setG(gg, 0.6f);
                        w.lpR.setG(gg * 1.05f, 0.6f);
                    }
                    float nl = w.pl.process(nz.white()), nr = w.pr.process(nz.white());
                    float vl = w.lpL.lp(nl) * g * w.amp, vr = w.lpR.lp(nr) * g * w.amp;
                    l += vl * (0.55f - 0.35f * w.pan) * gCoast * 0.22f;
                    r += vr * (0.55f + 0.35f * w.pan) * gCoast * 0.22f;
                }
                float rum = surfLp.lp(surfBr.process(nz.white())) * gCoast * 0.1f;
                l += rum;
                r += rum;
            }
            // ---- wind
            if (gWind > 1e-4f) {
                float wl = windPkL.process(nz.white()), wr = windPkR.process(nz.white());
                float g = gWind * gWind * (1.f + gust);
                float wh = whistle.bp(wl) * SmoothStep(0.5f, 1.f, s.wind) * 0.35f;
                l += (windBpL.bp(wl) * 0.5f + windLpL.lp(wl) * 0.9f + wh) * g * 0.35f;
                r += (windBpR.bp(wr) * 0.5f + windLpR.lp(wr) * 0.9f + wh * 0.7f) * g * 0.35f;
            }
            // ---- rain
            if (gRain > 1e-4f) {
                float wl = rainPkL.process(nz.white()), wr = rainPkR.process(nz.white());
                float hiss = (1.f - 0.6f * inVeh) * gRain;
                l += rainLpL.lp(rainHpL.hp(wl)) * hiss * 0.28f;
                r += rainLpR.lp(rainHpR.hp(wr)) * hiss * 0.28f;
                float pat = patterBp.bp(nz.white()) * gRain * 0.05f;
                l += pat;
                r += pat;
                float rate = (60.f + 900.f * gRain) * kInvSR;
                if (nz.uni() < rate) {
                    dropEnvL = nz.range(0.3f, 1.f);
                    if ((i & 7) == 0) dropL.setG(svfG(nz.range(1500.f, 6000.f)), 6.f);
                }
                if (nz.uni() < rate) {
                    dropEnvR = nz.range(0.3f, 1.f);
                    if ((i & 7) == 0) dropR.setG(svfG(nz.range(1500.f, 6000.f)), 6.f);
                }
                float il = dropEnvL, ir = dropEnvR;
                dropEnvL *= 0.6f;
                dropEnvR *= 0.6f;
                float dl = dropL.bp(il) * (1.f - 0.7f * inVeh), dr = dropR.bp(ir) * (1.f - 0.7f * inVeh);
                float roofl = roofL.bp(il * 3.f) * inVeh, roofr = roofR.bp(ir * 3.f) * inVeh;
                l += (dl * 0.25f + roofl * 0.6f) * gRain;
                r += (dr * 0.25f + roofr * 0.6f) * gRain;
            }
            // ---- underwater drone
            if (gUnder > 1e-4f) {
                uwPh += 0.25f * kInvSR;
                if (uwPh >= 1.f) uwPh -= 1.f;
                float v = uwLp.lp(uwBr.process(nz.white())) * (0.8f + 0.2f * sinWrapped(uwPh)) * gUnder * 0.25f;
                l += v;
                r += v;
            }
            L[i] = l;
            R[i] = r;
        }
        ctl++;
    }
};

AmbienceRenderer* ambienceCreate() { return new AmbienceRenderer(); }
void ambienceDestroy(AmbienceRenderer* a) { delete a; }
void ambienceRender(AmbienceRenderer* a, float* L, float* R, int n, const Ambience& target, const ListenerState& ls) {
    a->render(L, R, n, target, ls);
}

}  // namespace detail
}  // namespace Audio
