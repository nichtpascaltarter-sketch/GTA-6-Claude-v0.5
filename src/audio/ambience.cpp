// Ambient beds driven by Audio::Ambience, the time of day and the environment around the listener:
//   city: distant traffic roar with passing cars (tyre hiss, engines, trucks and bikes; spray on wet roads), building
//         HVAC / transformer hum downtown, far sirens, horns, dogs, distant helicopters and trains, construction by day,
//         pigeons and grackles;
//   port: generator / reefer drone, gantry cranes, containers set down, reverse beepers, ship horns, halyards, buoys;
//   coast: breaking waves (build-up, crash, foam wash, fizz), gulls, shorebirds, passing boats;
//   wetland (the Sawgrass): insect chorus, pig frogs, chorus frogs, bullfrogs, limpkins at night, red-winged
//         blackbirds and herons by day, mosquitoes, airboats passing in the distance;
//   nature: birds and the dawn chorus, cicadas at midday, crickets, katydids and tree crickets at night, owls, wind
//         in the vegetation;
//   weather: gusting wind; rain on the ground, leaves, puddles and parked cars, gutters running, drumming on an
//         awning or roof overhead (from the acoustic probe), on the car roof, muffled on the roof indoors; thunder
//         rumbling far off in heavy rain (close strikes come from Audio::playThunder with the lightning);
//   underwater drone.
// Outdoor layers are muffled indoors and in vehicles; continuous layers are synthesized in real time, sparse events
// are spawned as positional (or 2D stereo pass-by) one-shots. All layer gains crossfade smoothly.
#include "audio_internal.h"

namespace Audio {
namespace detail {

using namespace dsp;

namespace amb {

struct PassBy {
    bool active = false;
    float t = 0, dur = 3, pan0 = -1, pan1 = 1, gain = 0.5f, f0 = 800, engF = 80, engPh = 0, dopK = 0.08f;
    int kind = 0;  // 0 car, 1 truck, 2 motorbike
    Svf tireBp;
    OnePoleLP engLp;
    PinkNoise pink;
};

struct Cricket {
    float f = 4500, ph = 0, pulsePh = 0, timer = 0, pan = 0, amp = 0.5f, pulseRate = 28, gate = 0;
    int pulsesLeft = 0;
};

// Katydid: raspy "ch-ch-ch" calls (short high noise pulses), individuals answering each other.
struct Katydid {
    float timer = 0, pulseT = 0, env = 0, pan = 0, amp = 0.5f, fc = 8000;
    int pulses = 0;
    Svf bp;
};

// Tree cricket: a soft continuous trill.
struct TreeCricket {
    float f = 2900, ph = 0, amPh = 0, rate = 50, pan = 0, swell = 0.5f, swellT = 0.5f, timer = 0;
};

struct Buzz {
    float f = 450, ph = 0, amPh = 0, amRate = 0.3f, pan = 0, amp = 0.5f;
};

struct Wave {
    float u = 0, period = 9, pan = 0, amp = 1, crash = 0;
    Svf lpL, lpR, thumpLp;
    PinkNoise pl, pr;
};

}  // namespace amb

struct AmbienceRenderer {
    Ambience s;  // smoothed targets
    bool first = true;
    Noise nz{0xA3B1C2D3u};
    // gains (smoothed)
    float gCity = 0, gTraffic = 0, gDown = 0, gPort = 0, gDay = 0, gNight = 0, gCoast = 0, gWet = 0, gWetDay = 0, gRain = 0, gWind = 0,
          gUnder = 0, gCicada = 0, gVeg = 0, gKaty = 0;
    float shelter = 0.f, inside = 0.f, inVeh = 0.f, roofNear = 0.f;
    float encG = 1.f;
    // city
    BrownNoise brL, brR;
    PinkNoise pkL, pkR;
    Svf cityLpL, cityLpR, cityMidL, cityMidR;
    float cityMod = 1, cityModT = 1;
    amb::PassBy pass[6];
    float passTimer = 1.f;
    // building hum (downtown)
    float humPh = 0, fanPh1 = 0, fanPh2 = 0, fanAm = 0;
    PinkNoise fanPk;
    Svf fanLpL, fanLpR;
    // port drone (generators / reefers)
    float genPh1 = 0, genPh2 = 0;
    OnePoleLP genLp1, genLp2;
    PinkNoise genPk;
    Svf genFanBp;
    // nature
    amb::Cricket crickets[6];
    amb::Katydid katy[3];
    amb::TreeCricket tree[2];
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
    Svf surfLp, fizzHpL, fizzHpR;
    // wind
    PinkNoise windPkL, windPkR;
    Svf windBpL, windBpR, windLpL, windLpR, whistle;
    float gust = 0, gustT = 0, gustTimer = 0;
    // rain
    PinkNoise rainPkL, rainPkR;
    Svf rainHpL, rainHpR, rainLpL, rainLpR, patterBp;
    Svf dropL, dropR, roofL, roofR, awnL, awnR, carTapL, carTapR, gutterBp, indoorLp, curtainBp;
    float dropEnvL = 0, dropEnvR = 0, awnEnvL = 0, awnEnvR = 0, carEnv = 0, plopEnv = 0, plopPh = 0, plopF = 600, gurgle = 0;
    float gutterPan = 0.3f;
    // underwater
    BrownNoise uwBr;
    Svf uwLp;
    float uwPh = 0;
    // enclosure (indoors / in a vehicle) filter on the outdoor layers
    Svf encLpL, encLpR;
    float encCut = 22000.f;
    // spawn timers
    float tBird = 2, tGull = 3, tFrog = 2, tOwl = 20, tHeron = 30, tBullfrog = 8, tCrow = 25, tHorn = 8, tSiren = 30, tDog = 25, tThunder = 20,
          tBubble = 1, tLap = 3, tCricketPos = 2, tPigeon = 6, tGrackle = 12, tHeli = 60, tTrain = 90, tBuild = 15, tShipHorn = 40, tCrane = 12,
          tContainer = 10, tBeep = 25, tHalyard = 8, tBuoy = 30, tShorebird = 10, tBoat = 50, tAirboat = 70, tPigFrog = 6, tChorusFrog = 5,
          tLimpkin = 45, tBlackbird = 9, tMosquito = 40, tGator = 120, tParrot = 30;
    ListenerState ls;

    AmbienceRenderer() {
        for (int i = 0; i < 6; i++) {
            crickets[i].f = nz.range(3900.f, 5400.f);
            crickets[i].pan = nz.range(-0.9f, 0.9f);
            crickets[i].amp = nz.range(0.3f, 1.f);
            crickets[i].pulseRate = nz.range(22.f, 34.f);
            crickets[i].timer = nz.range(0.f, 1.f);
        }
        for (int i = 0; i < 3; i++) {
            katy[i].fc = nz.range(7200.f, 9400.f);
            katy[i].bp.set(katy[i].fc, 2.5f);
            katy[i].pan = nz.range(-0.85f, 0.85f);
            katy[i].amp = nz.range(0.5f, 1.f);
            katy[i].timer = nz.range(0.f, 1.5f);
        }
        for (int i = 0; i < 2; i++) {
            tree[i].f = nz.range(2700.f, 3200.f);
            tree[i].rate = nz.range(42.f, 58.f);
            tree[i].pan = i == 0 ? -0.5f : 0.55f;
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
            waves[i].thumpLp.set(140.f, 0.7f);
        }
        cicadaBpL.set(5200.f, 3.f);
        cicadaBpR.set(5600.f, 3.f);
        leafHpL.set(2500.f, 0.6f);
        leafHpR.set(2800.f, 0.6f);
        wetLp.set(2000.f, 0.7f);
        surfLp.set(220.f, 0.7f);
        fizzHpL.set(4500.f, 0.7f);
        fizzHpR.set(4800.f, 0.7f);
        whistle.set(900.f, 8.f);
        rainHpL.set(500.f, 0.7f);
        rainHpR.set(520.f, 0.7f);
        patterBp.set(1500.f, 0.5f);
        dropL.set(3000.f, 6.f);
        dropR.set(3500.f, 6.f);
        roofL.set(700.f, 4.f);
        roofR.set(760.f, 4.f);
        awnL.set(1100.f, 2.5f);
        awnR.set(1250.f, 2.5f);
        carTapL.set(3200.f, 9.f);
        carTapR.set(3700.f, 9.f);
        gutterBp.set(1300.f, 1.2f);
        indoorLp.set(320.f, 0.7f);
        curtainBp.set(2800.f, 0.7f);
        uwLp.set(260.f, 0.7f);
        cityMidL.set(480.f, 0.5f);
        cityMidR.set(520.f, 0.5f);
        fanLpL.set(520.f, 0.7f);
        fanLpR.set(560.f, 0.7f);
        genLp1.set(180.f);
        genLp2.set(200.f);
        genFanBp.set(900.f, 0.8f);
        encLpL.set(20000.f, 0.7f);
        encLpR.set(20000.f, 0.7f);
    }

    vec3 randomAround(float dmin, float dmax, float zmin, float zmax) {
        float a = nz.uni() * kTwoPi;
        float d = nz.range(dmin, dmax);
        return ls.pos + vec3(cosf(a) * d, sinf(a) * d, nz.range(zmin, zmax));
    }

    // Control-rate update: smoothing, filter coefficients, event spawning.
    void control(float dt, const Ambience& tgt, const AcousticState& ac) {
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
        s.downtown += (Saturate(tgt.downtown) - s.downtown) * k;
        s.port += (Saturate(tgt.port) - s.port) * k;
        float trafficT = tgt.traffic >= 0.f ? Saturate(tgt.traffic) : Saturate(tgt.urban);
        s.traffic = Max(s.traffic, 0.f);
        s.traffic += (trafficT - s.traffic) * k;
        s.underwater += (tgt.underwater - s.underwater) * (1.f - expf(-dt / 0.15f));
        // time of day: follow directly (handle wrap)
        s.timeOfDay = tgt.timeOfDay;
        float t = fmodf(Max(0.f, s.timeOfDay), 24.f);
        float day = SmoothStep(5.5f, 7.f, t) * (1.f - SmoothStep(19.f, 20.5f, t));
        float night = 1.f - day;
        float dawn = SmoothStep(5.f, 6.f, t) * (1.f - SmoothStep(7.5f, 9.f, t));
        float dusk = SmoothStep(18.f, 19.f, t) * (1.f - SmoothStep(21.f, 22.5f, t));
        float cic = SmoothStep(9.f, 11.f, t) * (1.f - SmoothStep(17.5f, 19.f, t));
        float deadNight = SmoothStep(0.5f, 1.5f, t) * (1.f - SmoothStep(4.5f, 5.5f, t));
        float rush = SmoothStep(6.5f, 7.5f, t) * (1.f - SmoothStep(9.f, 10.f, t)) + SmoothStep(16.5f, 17.5f, t) * (1.f - SmoothStep(19.f, 20.f, t));
        float dry = 1.f - s.underwater;
        float rainDamp = 1.f - 0.7f * s.rain;  // animals hide in rain
        float kg = 1.f - expf(-dt / 0.8f);
        gCity += (s.urban * (0.65f + 0.35f * day) * (1.f + 0.4f * s.downtown) * dry - gCity) * kg;
        gTraffic += (s.traffic * (1.f - 0.5f * deadNight + 0.2f * rush) * dry - gTraffic) * kg;
        gDown += (s.downtown * dry - gDown) * kg;
        gPort += (s.port * dry - gPort) * kg;
        gDay += (s.nature * day * dry * rainDamp - gDay) * kg;
        gNight += (Max(s.nature, s.wetland) * night * dry * rainDamp - gNight) * kg;
        gKaty += (Max(s.nature, s.wetland * 0.7f) * night * dry * rainDamp * (1.f - 0.6f * s.urban) - gKaty) * kg;
        gCicada += (Max(s.nature * 0.6f, s.wetland) * cic * dry * rainDamp - gCicada) * kg;
        gCoast += (s.coast * dry - gCoast) * kg;
        gWet += (s.wetland * dry - gWet) * kg;
        gWetDay += (s.wetland * day * dry * rainDamp - gWetDay) * kg;
        gRain += (s.rain * dry - gRain) * kg;
        gWind += (s.wind * dry - gWind) * kg;
        gVeg += (Max(s.nature, s.wetland * 0.8f) * (0.25f + s.wind) * dry - gVeg) * kg;
        gUnder += (s.underwater - gUnder) * kg;
        // where the listener is: under a roof / awning (probe), indoors, in a vehicle
        float coverT = ac.probed ? ac.cover * (1.f - ac.enclosed * 0.5f) : 0.f;
        shelter += (Saturate(coverT) - shelter) * kg;
        roofNear += ((ac.probed && ac.up < 7.f ? 1.f : 0.f) - roofNear) * kg;
        inside += (Saturate(ls.interior) * (1.f - Saturate(ls.inVehicle)) - inside) * (1.f - expf(-dt / 0.3f));
        inVeh += (Saturate(ls.inVehicle) - inVeh) * (1.f - expf(-dt / 0.3f));
        float cut = Min(Lerp(20000.f, 1300.f, inVeh), Lerp(20000.f, 650.f, inside));
        if (fabsf(cut - encCut) > 25.f) {
            encCut = cut;
            encLpL.set(cut, 0.7f);
            encLpR.set(cut, 0.7f);
        }
        encG = Lerp(1.f, 0.5f, inVeh) * Lerp(1.f, 0.28f, inside);

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
        float lh = (2400.f + 900.f * s.wind) * (1.f + 0.3f * gust);
        leafHpL.set(lh, 0.6f);
        leafHpR.set(lh * 1.1f, 0.6f);

        // cicada swell
        cicadaTimer -= dt;
        if (cicadaTimer <= 0.f) {
            cicadaTarget = nz.range(0.1f, 1.f);
            cicadaTimer = nz.range(3.f, 9.f);
        }
        cicadaSwell += (cicadaTarget - cicadaSwell) * (1.f - expf(-dt / 2.f));
        for (auto& tc : tree) {
            tc.timer -= dt;
            if (tc.timer <= 0.f) {
                tc.swellT = nz.range(0.f, 1.f) < 0.2f ? 0.f : nz.range(0.4f, 1.f);
                tc.timer = nz.range(2.f, 7.f);
            }
            tc.swell += (tc.swellT - tc.swell) * (1.f - expf(-dt / 0.6f));
        }

        // traffic pass-bys: cars, some trucks and motorbikes; wet roads hiss
        passTimer -= dt;
        if (passTimer <= 0.f) {
            float rate = 0.05f + 1.1f * gTraffic;
            passTimer = nz.range(0.4f, 2.2f) / Max(rate, 0.05f);
            if (gTraffic > 0.05f && dry > 0.5f) {
                for (auto& p : pass) {
                    if (p.active) continue;
                    p.active = true;
                    p.t = 0;
                    float kr = nz.uni();
                    p.kind = kr < 0.12f ? 1 : kr < 0.2f ? 2 : 0;
                    p.dur = p.kind == 1 ? nz.range(4.f, 6.5f) : nz.range(2.5f, 5.5f);
                    bool ltr = nz.uni() < 0.5f;
                    float spread = nz.range(0.4f, 0.95f);
                    p.pan0 = ltr ? -spread : spread;
                    p.pan1 = -p.pan0;
                    p.gain = nz.range(0.25f, 1.f) * (p.kind == 1 ? 1.3f : 1.f);
                    p.f0 = nz.range(600.f, 1100.f) * (s.rain > 0.2f ? 1.35f : 1.f);
                    p.engF = p.kind == 1 ? nz.range(38.f, 55.f) : p.kind == 2 ? nz.range(140.f, 230.f) : nz.range(55.f, 120.f);
                    p.dopK = nz.range(0.05f, 0.1f);
                    p.engLp.set(p.kind == 2 ? 900.f : 400.f);
                    break;
                }
            }
        }

        // positional / 2D one-shots
        auto tick = [&](float& timer, float rate, float jitter) -> bool {
            if (rate <= 1e-4f) {
                timer = Max(timer, 1.f / Max(rate, 0.02f) * 0.25f);
                return false;
            }
            timer -= dt;
            if (timer > 0.f) return false;
            timer = (1.f / rate) * nz.range(1.f - jitter, 1.f + jitter);
            return true;
        };
        auto flip = [&]() { return nz.uni() < 0.5f; };
        float birds = gDay * (1.f + 2.5f * dawn);
        if (tick(tBird, 0.45f * birds, 0.8f))
            spawnWorldOneShot(nz.uni() < 0.4f ? (int)AMB_BIRD_SONG : (int)SFX_BIRD_CHIRP, randomAround(8.f, 45.f, 3.f, 14.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tCrow, 0.03f * gDay, 0.5f)) spawnWorldOneShot(AMB_CROW, randomAround(30.f, 120.f, 5.f, 20.f), 0.8f, 1.f);
        if (tick(tGull, (0.22f * gCoast + 0.08f * gPort) * (0.3f + 0.7f * day), 0.7f))
            spawnWorldOneShot(SFX_SEAGULL, randomAround(15.f, 70.f, 6.f, 25.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tShorebird, 0.12f * gCoast * day * (1.f - s.urban * 0.5f), 0.7f))
            spawnWorldOneShot(SFX_SHOREBIRD_PEEP, randomAround(10.f, 40.f, 0.f, 1.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tOwl, 0.025f * gNight * s.nature, 0.5f)) spawnWorldOneShot(AMB_OWL, randomAround(40.f, 150.f, 6.f, 20.f), 0.8f, 1.f);
        if (tick(tFrog, 0.6f * gNight * (0.3f * s.nature + s.wetland) + 0.2f * gWet, 0.8f))
            spawnWorldOneShot(AMB_TREEFROG, randomAround(6.f, 40.f, 0.f, 3.f), nz.range(0.4f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tBullfrog, 0.12f * gWet * (0.4f + 0.6f * night), 0.6f))
            spawnWorldOneShot(AMB_BULLFROG, randomAround(10.f, 60.f, 0.f, 1.f), nz.range(0.6f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tPigFrog, 0.22f * gWet * (0.3f + 0.7f * Max(night, dusk)) * rainDamp, 0.7f))
            spawnWorldOneShot(AMB_PIG_FROG, randomAround(8.f, 50.f, 0.f, 0.5f), nz.range(0.5f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tChorusFrog, 0.3f * gWet * Max(night, dusk) * (0.5f + 0.5f * s.rain), 0.8f))
            spawnWorldOneShot(AMB_CHORUS_FROG, randomAround(5.f, 35.f, 0.f, 1.f), nz.range(0.4f, 1.f), nz.range(0.93f, 1.07f));
        if (tick(tLimpkin, 0.03f * gWet * night * rainDamp, 0.6f)) spawnWorldOneShot(AMB_LIMPKIN, randomAround(60.f, 250.f, 0.f, 3.f), 0.9f, 1.f);
        if (tick(tBlackbird, 0.16f * gWetDay, 0.7f)) spawnWorldOneShot(AMB_BLACKBIRD, randomAround(8.f, 50.f, 1.f, 4.f), nz.range(0.6f, 1.f), 1.f);
        if (tick(tHeron, 0.02f * gWet * day, 0.5f)) spawnWorldOneShot(AMB_HERON, randomAround(40.f, 150.f, 1.f, 10.f), 0.7f, 1.f);
        if (tick(tGator, 0.006f * gWet * (0.5f + 0.5f * dusk), 0.5f)) spawnWorldOneShot(SFX_GATOR_BELLOW, randomAround(120.f, 400.f, 0.f, 0.5f), 0.8f, 1.f);
        if (tick(tMosquito, 0.025f * gWet * (0.4f + 0.6f * Max(dusk, night)) * rainDamp * (1.f - inVeh) * (1.f - inside), 0.8f))
            spawnAmbient2D(AMB_MOSQUITO, nz.range(0.5f, 1.f), nz.range(0.9f, 1.15f), flip());
        if (tick(tAirboat, 0.012f * gWet * day * (1.f - 0.8f * s.rain), 0.6f))
            spawnAmbient2D(AMB_AIRBOAT, nz.range(0.35f, 0.8f), nz.range(0.95f, 1.05f), flip());
        if (tick(tLap, 0.35f * Max(gWet, Max(gCoast * 0.5f, gPort * 0.6f)), 0.6f))
            spawnWorldOneShot(AMB_WATER_LAP, randomAround(4.f, 18.f, -1.f, 0.f), nz.range(0.5f, 1.f), 1.f);
        if (tick(tCricketPos, 0.5f * gNight, 0.8f))
            spawnWorldOneShot(AMB_CRICKET_CHIRP, randomAround(2.f, 12.f, 0.f, 1.f), nz.range(0.5f, 1.f), nz.range(0.95f, 1.05f));
        // city
        if (tick(tHorn, 0.09f * gCity * (1.f + 0.5f * s.downtown), 0.8f))
            spawnWorldOneShot(AMB_HORN_DISTANT, randomAround(60.f, 350.f, 0.f, 5.f), nz.range(0.5f, 1.f), nz.range(0.95f, 1.05f));
        if (tick(tSiren, 0.012f * gCity * (0.5f + 0.5f * night + 0.3f) * (1.f + 1.5f * s.downtown), 0.5f))
            spawnWorldOneShot(AMB_SIREN_DISTANT, randomAround(300.f, 1200.f, 0.f, 10.f), 1.f, nz.range(0.97f, 1.03f));
        if (tick(tDog, 0.02f * (gCity * 0.6f + gNight * 0.5f) * (1.f - 0.7f * s.downtown), 0.6f))
            spawnWorldOneShot(AMB_DOG_DISTANT, randomAround(60.f, 300.f, 0.f, 3.f), nz.range(0.5f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tPigeon, 0.08f * gCity * day * (1.f - s.rain), 0.7f))
            spawnWorldOneShot(SFX_PIGEON_COO, randomAround(4.f, 25.f, 3.f, 12.f), nz.range(0.4f, 0.8f), nz.range(0.95f, 1.05f));
        if (tick(tGrackle, 0.05f * gCity * day * (1.f - s.rain), 0.7f))
            spawnWorldOneShot(SFX_GRACKLE_CALL, randomAround(10.f, 45.f, 4.f, 15.f), nz.range(0.4f, 0.8f), 1.f);
        if (tick(tParrot, 0.012f * gCity * day * (1.f - s.downtown) * (1.f - s.rain), 0.6f))
            spawnWorldOneShot(SFX_PARROT_SQUAWK, randomAround(20.f, 70.f, 6.f, 20.f), nz.range(0.4f, 0.8f), 1.f);
        if (tick(tHeli, 0.01f * (gCity * 0.3f + gDown) * (0.4f + 0.6f * day), 0.6f))
            spawnWorldOneShot(SFX_HELI_FLYBY, randomAround(600.f, 1600.f, 120.f, 300.f), nz.range(0.3f, 0.6f), nz.range(0.95f, 1.05f));
        if (tick(tTrain, 0.008f * gCity * (1.f - 0.6f * deadNight), 0.6f))
            spawnAmbient2D(AMB_TRAIN_PASS, nz.range(0.25f, 0.55f), nz.range(0.97f, 1.03f), flip());
        if (tick(tBuild, 0.05f * gDown * day * cic * (1.f - s.rain), 0.7f))
            spawnWorldOneShot(AMB_CONSTRUCTION, randomAround(80.f, 250.f, 2.f, 40.f), nz.range(0.5f, 1.f), nz.range(0.95f, 1.05f));
        // port
        if (tick(tShipHorn, 0.02f * (gPort + 0.25f * gCoast), 0.6f))
            spawnWorldOneShot(SFX_SHIP_HORN, randomAround(400.f, 1500.f, 5.f, 20.f), nz.range(0.5f, 0.9f), nz.range(0.97f, 1.03f));
        if (tick(tCrane, 0.06f * gPort * (0.5f + 0.5f * day), 0.7f))
            spawnWorldOneShot(AMB_CRANE, randomAround(60.f, 250.f, 20.f, 45.f), nz.range(0.5f, 1.f), nz.range(0.95f, 1.05f));
        if (tick(tContainer, 0.08f * gPort * (0.5f + 0.5f * day), 0.7f))
            spawnWorldOneShot(AMB_CONTAINER, randomAround(50.f, 300.f, 2.f, 20.f), nz.range(0.4f, 1.f), nz.range(0.9f, 1.1f));
        if (tick(tBeep, 0.04f * (gPort + 0.3f * gDown * day), 0.7f))
            spawnWorldOneShot(AMB_REVERSE_BEEP, randomAround(40.f, 220.f, 0.f, 3.f), nz.range(0.4f, 0.9f), nz.range(0.97f, 1.03f));
        if (tick(tHalyard, 0.12f * Max(gPort * 0.4f, gCoast * s.urban) * (0.3f + s.wind), 0.7f))
            spawnWorldOneShot(AMB_HALYARD, randomAround(10.f, 60.f, 3.f, 10.f), nz.range(0.4f, 0.9f), nz.range(0.9f, 1.1f));
        if (tick(tBuoy, 0.02f * Max(gPort, gCoast), 0.6f)) spawnWorldOneShot(AMB_BUOY_BELL, randomAround(150.f, 600.f, 0.f, 2.f), 0.7f, 1.f);
        if (tick(tBoat, 0.015f * Max(gCoast, gPort * 0.5f) * day, 0.6f))
            spawnAmbient2D(AMB_BOAT_PASS, nz.range(0.25f, 0.6f), nz.range(0.95f, 1.05f), flip());
        // weather: distant thunder in heavy rain (strikes with lightning come from the game through playThunder)
        if (tick(tThunder, s.rain > 0.5f ? 0.025f * (s.rain - 0.45f) * 2.f : 0.f, 0.7f))
            spawnAmbient2D(AMB_THUNDER_FAR, nz.range(0.4f, 0.8f), nz.range(0.9f, 1.05f), flip());
        if (tick(tBubble, 1.2f * gUnder, 0.8f)) spawnWorldOneShot(AMB_BUBBLES, randomAround(0.5f, 3.f, -1.f, 1.f), nz.range(0.5f, 1.f), 1.f);
    }

    void render(float* L, float* R, int n, const Ambience& tgt, const ListenerState& lsIn, const AcousticState& ac) {
        ls = lsIn;
        control((float)n * kInvSR, tgt, ac);
        const float open = (1.f - shelter) * (1.f - inside) * (1.f - inVeh);
        const float rainOut = gRain * (open + 0.45f * shelter * (1.f - inside) + 0.3f * inside + 0.35f * inVeh);
        const bool encOn = encCut < 19000.f;
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
            }
            // ---- passing traffic
            if (gTraffic > 1e-4f || pass[0].active) {
                for (auto& p : pass) {
                    if (!p.active) continue;
                    p.t += kInvSR;
                    float u = p.t / p.dur;
                    if (u >= 1.f) {
                        p.active = false;
                        continue;
                    }
                    float x = (u - 0.5f) * 5.f;
                    float env = 1.f / sqrtf(1.f + x * x) - 0.19f;
                    env = Max(env, 0.f) * 1.25f;
                    float dop = 1.f - p.dopK * (u - 0.5f) * 2.f;
                    if ((i & 15) == 0) p.tireBp.setG(svfG(p.f0 * dop), 0.9f);
                    float tire = p.tireBp.bp(p.pink.process(nz.white()));
                    p.engPh += p.engF * dop * kInvSR;
                    if (p.engPh >= 1.f) p.engPh -= 1.f;
                    float eng = p.engLp.process(2.f * p.engPh - 1.f) * (p.kind == 1 ? 0.8f : p.kind == 2 ? 0.35f : 0.5f);
                    float v = (tire * 0.5f + eng * 0.12f) * env * p.gain * gTraffic;
                    float pan = p.pan0 + (p.pan1 - p.pan0) * u;
                    l += v * (0.5f - 0.45f * pan);
                    r += v * (0.5f + 0.45f * pan);
                }
            }
            // ---- nature: wind in the leaves / sawgrass
            if (gVeg > 1e-4f) {
                float w = leafPink.process(nz.white());
                float lv = (1.f + gust) * gVeg * 0.045f;
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
            // ---- crickets, tree crickets and katydids (night)
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
                for (auto& tc : tree) {
                    if (tc.swell < 1e-3f) continue;
                    tc.ph += tc.f * kInvSR;
                    if (tc.ph >= 1.f) tc.ph -= 1.f;
                    tc.amPh += tc.rate * kInvSR;
                    if (tc.amPh >= 1.f) tc.amPh -= 1.f;
                    float am = tc.amPh < 0.55f ? sinf(tc.amPh / 0.55f * kPi) : 0.f;
                    float v = sinWrapped(tc.ph) * am * tc.swell * gNight * 0.008f;
                    l += v * (0.5f - 0.4f * tc.pan);
                    r += v * (0.5f + 0.4f * tc.pan);
                }
            }
            if (gKaty > 1e-4f) {
                for (auto& kd : katy) {
                    kd.timer -= kInvSR;
                    if (kd.timer <= 0.f && kd.pulses == 0) {
                        kd.pulses = 2 + (int)(nz.uni() * 3.f);
                        kd.pulseT = 0.f;
                        kd.timer = nz.range(0.9f, 2.2f);
                    }
                    if (kd.pulses > 0) {
                        kd.pulseT -= kInvSR;
                        if (kd.pulseT <= 0.f) {
                            kd.env = 1.f;
                            kd.pulseT = nz.range(0.055f, 0.075f);
                            kd.pulses--;
                        }
                    }
                    if (kd.env > 1e-4f) {
                        float v = kd.bp.bp(nz.white()) * kd.env * kd.amp * gKaty * 0.05f;
                        kd.env *= 0.9975f;
                        l += v * (0.5f - 0.45f * kd.pan);
                        r += v * (0.5f + 0.45f * kd.pan);
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
            // ---- coast: breaking waves (build-up, crash, foam wash, fizz) over the surf rumble
            if (gCoast > 1e-4f) {
                float fizz = 0.f;
                for (auto& w : waves) {
                    w.u += kInvSR / w.period;
                    if (w.u >= 1.f) {
                        w.u -= 1.f;
                        w.period = nz.range(7.f, 12.f);
                        w.amp = nz.range(0.5f, 1.f);
                    }
                    float u = w.u, g, fc;
                    if (u < 0.25f) {
                        float x = u / 0.25f;
                        g = 0.15f + 0.6f * x * x;
                        fc = 450.f + 1500.f * x;
                        w.crash = 0.f;
                    } else if (u < 0.31f) {
                        g = 1.f;
                        fc = 5500.f;
                        if (w.crash == 0.f) w.crash = 1.f;
                    } else {
                        float x = (u - 0.31f);
                        g = expf(-x * 5.5f) * 0.9f + 0.08f;
                        fc = 1400.f + 4000.f * expf(-x * 4.f);
                        fizz += expf(-x * 2.5f) * (0.3f + 0.7f * x) * w.amp;
                    }
                    if ((i & 31) == 0) {
                        float gg = svfG(fc);
                        w.lpL.setG(gg, 0.6f);
                        w.lpR.setG(gg * 1.05f, 0.6f);
                    }
                    float nl = w.pl.process(nz.white()), nr = w.pr.process(nz.white());
                    float vl = w.lpL.lp(nl) * g * w.amp, vr = w.lpR.lp(nr) * g * w.amp;
                    float thump = 0.f;
                    if (w.crash > 1e-3f) {
                        thump = w.thumpLp.lp(nz.white()) * w.crash * 2.2f * w.amp;
                        w.crash *= 0.99975f;
                    }
                    l += (vl + thump) * (0.55f - 0.35f * w.pan) * gCoast * 0.22f;
                    r += (vr + thump) * (0.55f + 0.35f * w.pan) * gCoast * 0.22f;
                }
                float rum = surfLp.lp(surfBr.process(nz.white())) * gCoast * 0.1f;
                float fzl = fizzHpL.hp(nz.white()) * fizz * gCoast * 0.012f, fzr = fizzHpR.hp(nz.white()) * fizz * gCoast * 0.012f;
                l += rum + fzl;
                r += rum + fzr;
            }
            // ---- wind
            if (gWind > 1e-4f) {
                float wl = windPkL.process(nz.white()), wr = windPkR.process(nz.white());
                float g = gWind * gWind * (1.f + gust);
                float wh = whistle.bp(wl) * SmoothStep(0.5f, 1.f, s.wind) * 0.35f;
                l += (windBpL.bp(wl) * 0.5f + windLpL.lp(wl) * 0.9f + wh) * g * 0.35f;
                r += (windBpR.bp(wr) * 0.5f + windLpR.lp(wr) * 0.9f + wh * 0.7f) * g * 0.35f;
            }
            // ---- rain outside: hiss, splashes near by, leaves, puddles, parked cars, a gutter running
            if (gRain > 1e-4f) {
                float wl = rainPkL.process(nz.white()), wr = rainPkR.process(nz.white());
                l += rainLpL.lp(rainHpL.hp(wl)) * rainOut * 0.28f;
                r += rainLpR.lp(rainHpR.hp(wr)) * rainOut * 0.28f;
                float pat = patterBp.bp(nz.white()) * gRain * (0.035f + 0.04f * s.nature) * (1.f - inside);
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
                l += dropL.bp(il) * 0.25f * gRain * open;
                r += dropR.bp(ir) * 0.25f * gRain * open;
                // puddle plops (town) and taps on parked cars
                if (s.urban > 0.05f) {
                    if (nz.uni() < (8.f + 30.f * gRain) * kInvSR * s.urban) {
                        plopEnv = nz.range(0.4f, 1.f);
                        plopF = nz.range(350.f, 900.f);
                    }
                    if (plopEnv > 1e-3f) {
                        plopPh += plopF * kInvSR;
                        plopF *= 1.00006f;
                        if (plopPh >= 1.f) plopPh -= 1.f;
                        float v = sinWrapped(plopPh) * plopEnv * 0.03f * gRain * (1.f - inside);
                        plopEnv *= 0.9985f;
                        l += v * 0.8f;
                        r += v * 0.6f;
                    }
                    if (nz.uni() < (20.f + 60.f * gRain) * kInvSR * s.urban) carEnv = nz.range(0.3f, 1.f);
                    float ct = carEnv;
                    carEnv *= 0.5f;
                    l += carTapL.bp(ct) * 0.08f * gRain * s.urban * (1.f - inside);
                    r += carTapR.bp(ct * 0.7f) * 0.08f * gRain * s.urban * (1.f - inside);
                    if (gRain > 0.3f) {
                        if ((i & 255) == 0) gurgle += (nz.range(0.3f, 1.2f) - gurgle) * 0.3f;
                        float gv = gutterBp.bp(nz.white()) * gurgle * SmoothStep(0.3f, 0.8f, gRain) * s.urban * 0.035f * (1.f - inside);
                        l += gv * (0.5f - 0.4f * gutterPan);
                        r += gv * (0.5f + 0.4f * gutterPan);
                    }
                }
            }
            // ---- underwater drone
            if (gUnder > 1e-4f) {
                uwPh += 0.25f * kInvSR;
                if (uwPh >= 1.f) uwPh -= 1.f;
                float v = uwLp.lp(uwBr.process(nz.white())) * (0.8f + 0.2f * sinWrapped(uwPh)) * gUnder * 0.25f;
                l += v;
                r += v;
            }
            // outdoor layers heard from inside a building or a vehicle
            if (encOn) {
                l = encLpL.lp(l) * encG;
                r = encLpR.lp(r) * encG;
            }
            // ---- layers at the listener: building hum, port drone, rain on the shelter / car roof / building roof
            if (gDown > 1e-4f) {
                humPh += 60.f * kInvSR;
                if (humPh >= 1.f) humPh -= 1.f;
                fanPh1 += 146.f * kInvSR;
                if (fanPh1 >= 1.f) fanPh1 -= 1.f;
                fanPh2 += 152.5f * kInvSR;
                if (fanPh2 >= 1.f) fanPh2 -= 1.f;
                if ((i & 127) == 0) fanAm += (nz.range(0.6f, 1.f) - fanAm) * 0.02f;
                float hum = sinWrapped(humPh * 2.f) * 0.35f + sinWrapped(humPh) * 0.2f + sinWrapped(humPh * 3.f) * 0.1f;
                float tones = (sinWrapped(fanPh1) + sinWrapped(fanPh2)) * 0.25f;
                float fw = fanPk.process(nz.white());
                float night = 1.f - SmoothStep(6.f, 8.f, s.timeOfDay) * (1.f - SmoothStep(19.f, 21.f, s.timeOfDay));
                float g = gDown * (0.55f + 0.45f * night) * 0.012f * (1.f - 0.6f * inVeh);
                l += (hum * 0.3f + tones + fanLpL.lp(fw) * 2.2f * fanAm) * g;
                r += (hum * 0.3f + tones * 0.9f + fanLpR.lp(nz.white() * 0.4f + fw * 0.6f) * 2.2f * fanAm) * g;
            }
            if (gPort > 1e-4f) {
                genPh1 += 75.f * kInvSR;
                if (genPh1 >= 1.f) genPh1 -= 1.f;
                genPh2 += 77.3f * kInvSR;
                if (genPh2 >= 1.f) genPh2 -= 1.f;
                float d1 = genLp1.process(2.f * genPh1 - 1.f), d2 = genLp2.process(2.f * genPh2 - 1.f);
                float fan = genFanBp.bp(genPk.process(nz.white()));
                float g = gPort * 0.03f * (1.f - 0.6f * inVeh) * (1.f - 0.7f * inside);
                l += (d1 * 0.8f + d2 * 0.4f + fan * 0.25f) * g;
                r += (d1 * 0.4f + d2 * 0.8f + fan * 0.25f) * g;
            }
            if (gRain > 1e-4f) {
                // drumming on an awning / station roof right overhead, the drip line in front
                if (shelter > 1e-3f) {
                    float rate = (250.f + 1600.f * gRain) * kInvSR;
                    if (nz.uni() < rate) awnEnvL = nz.range(0.2f, 1.f);
                    if (nz.uni() < rate) awnEnvR = nz.range(0.2f, 1.f);
                    float al = awnEnvL, ar = awnEnvR;
                    awnEnvL *= 0.72f;
                    awnEnvR *= 0.72f;
                    float closeK = 0.5f + 0.5f * roofNear;
                    float cur = curtainBp.bp(nz.white()) * 0.05f;
                    l += (awnL.bp(al) * 0.35f * closeK + cur) * gRain * shelter * (1.f - inside);
                    r += (awnR.bp(ar) * 0.35f * closeK + cur) * gRain * shelter * (1.f - inside);
                }
                // on the car roof
                if (inVeh > 1e-3f) {
                    float il = dropEnvL * 3.f, ir = dropEnvR * 3.f;
                    l += roofL.bp(il) * 0.6f * gRain * inVeh;
                    r += roofR.bp(ir) * 0.6f * gRain * inVeh;
                }
                // indoors: the roof and windows, muffled
                if (inside > 1e-3f) {
                    float v = indoorLp.lp(nz.white()) * 0.09f * gRain * inside;
                    l += v;
                    r += v * 0.9f;
                }
            }
            L[i] = l;
            R[i] = r;
        }
    }
};

AmbienceRenderer* ambienceCreate() { return new AmbienceRenderer(); }
void ambienceDestroy(AmbienceRenderer* a) { delete a; }
void ambienceRender(AmbienceRenderer* a, float* L, float* R, int n, const Ambience& target, const ListenerState& ls, const AcousticState& ac) {
    a->render(L, R, n, target, ls, ac);
}

}  // namespace detail
}  // namespace Audio
