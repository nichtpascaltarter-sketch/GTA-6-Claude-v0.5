// Ambience one-shots (included by sfx.cpp inside namespace sfxgen, after the synthesis toolkit): district sounds
// spawned by the ambience renderer - airboats, boats and trains passing in the distance (stereo, the pass is baked
// with its pan sweep, level swell and doppler), port cranes, containers and reverse beepers, construction, wetland
// frogs and birds, mosquitoes, marina halyards, a bell buoy and thunder by distance.

// A distant pass-by trajectory: level swell (1/r along a straight path), doppler and pan sweep.
struct PassPath {
    float dur, tc, w, dop, panW;
    FORCEINLINE float level(float t) const {
        float x = (t - tc) / w;
        float e = 1.f / sqrtf(1.f + x * x);
        float fade = Min(1.f, t / 0.8f) * Min(1.f, (dur - t) / 1.2f);
        return e * Max(fade, 0.f);
    }
    FORCEINLINE float doppler(float t) const { return 1.f - dop * tanhf((t - tc) / (w * 0.6f)); }
    FORCEINLINE float pan(float t) const { return 0.8f * tanhf((t - tc) / panW); }
};

static FORCEINLINE void addStereo(Buf& b, int i, float v, float pan) {
    float gl, gr;
    panGains(pan, gl, gr);
    b.L[(size_t)i] += v * gl * 1.41421f;
    b.R[(size_t)i] += v * gr * 1.41421f;
}

// Airboat on the sawgrass: a big 2/3-blade propeller (raspy buzz at the blade-pass rate) on a V8, passing.
static void a_airboat(Buf& b) {
    PassPath p{b.rnd(8.f, 10.f), 0.f, b.rnd(1.6f, 2.4f), b.rnd(0.035f, 0.05f), b.rnd(1.8f, 2.8f)};
    p.tc = p.dur * b.rnd(0.42f, 0.55f);
    int n = S(p.dur);
    b.ensure(n);
    float blades = b.chance(0.6f) ? 2.f : 3.f;
    float rpm = b.rnd(2500.f, 3100.f);
    float fbp = rpm / 60.f * blades, feng = rpm / 60.f * 4.f;
    float ph = 0.f, phe = 0.f, wob = 0.f;
    Svf nzHp, nzLp, outLpL;
    nzHp.set(280.f, 0.7f);
    nzLp.set(2800.f, 0.7f);
    OnePoleLP engLp;
    engLp.set(700.f);
    PinkNoise pk;
    float cycAmp = 1.f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        if ((i & 255) == 0) wob = 1.f + 0.012f * sinf(t * 1.3f) + 0.006f * b.nz.white();
        float dop = p.doppler(t) * wob;
        ph += fbp * dop / SR;
        if (ph >= 1.f) ph -= 1.f;
        float pe = phe + feng * dop / SR;
        if (pe >= 1.f) {
            pe -= 1.f;
            cycAmp = b.rnd(0.75f, 1.15f);
        }
        phe = pe;
        // prop: bright buzz (sum of harmonics with slowly falling weights)
        float prop = 0.f;
        for (int k = 1; k <= 10; k++) prop += sinCycle(ph * (float)k) / powf((float)k, 0.85f);
        // blade-slap modulated air noise
        float am = sinCycle(ph);
        am = 0.35f + 0.65f * am * am;
        float air = nzLp.lp(nzHp.hp(pk.process(b.nz.white()))) * am;
        float eng = engLp.process((2.f * pe - 1.f) * cycAmp);
        float v = (prop * 0.32f + air * 0.9f + eng * 0.35f) * p.level(t);
        addStereo(b, i, v, p.pan(t));
    }
    lowpass(b, 2600.f, 0.6f);
}

// Outboard skiff passing out on the water: two-stroke buzz, prop wash and hull slaps.
static void a_boatPass(Buf& b) {
    PassPath p{b.rnd(6.f, 8.f), 0.f, b.rnd(1.2f, 1.9f), b.rnd(0.03f, 0.045f), b.rnd(1.4f, 2.2f)};
    p.tc = p.dur * b.rnd(0.4f, 0.55f);
    int n = S(p.dur);
    b.ensure(n);
    float f = b.rnd(160.f, 210.f);
    float ph = 0.f;
    Svf buzzBp, washLp;
    buzzBp.set(f * 3.f, 1.2f);
    washLp.set(900.f, 0.7f);
    PinkNoise pk;
    float slap = 0.f, slapT = b.rnd(0.2f, 0.6f);
    Svf slapBp;
    slapBp.set(220.f, 3.f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float dop = p.doppler(t);
        ph += f * dop / SR;
        if (ph >= 1.f) ph -= 1.f;
        float saw = 2.f * ph - 1.f;
        float buzz = saw * 0.4f + buzzBp.bp(saw) * 0.6f;
        float wash = washLp.lp(pk.process(b.nz.white())) * 0.6f;
        slapT -= 1.f / SR;
        if (slapT <= 0.f) {
            slap = b.rnd(0.4f, 1.f);
            slapT = b.rnd(0.18f, 0.5f);
        }
        float sl = slapBp.bp(slap * b.nz.white());
        slap *= 0.9985f;
        float v = (buzz * 0.4f + wash + sl * 0.5f) * p.level(t);
        addStereo(b, i, v, p.pan(t));
    }
    lowpass(b, 3000.f, 0.6f);
}

// A train passing at a distance (SkyLine metro or a freight line): traction whine, wheel roar, rail joints.
static void a_trainPass(Buf& b) {
    bool metro = b.chance(0.65f);
    PassPath p{b.rnd(9.f, 12.f), 0.f, b.rnd(2.2f, 3.2f), b.rnd(0.012f, 0.02f), b.rnd(2.5f, 3.5f)};
    p.tc = p.dur * 0.5f;
    int n = S(p.dur);
    b.ensure(n);
    float whineF = b.rnd(760.f, 980.f), ph = 0.f;
    Svf roarBp, rumLp;
    roarBp.set(metro ? 1300.f : 900.f, 0.8f);
    rumLp.set(metro ? 180.f : 120.f, 0.7f);
    PinkNoise pk;
    BrownNoise br;
    // rail joints: bogie pairs clacking as each car crosses a joint
    float carLen = metro ? 18.f : 16.f, speed = metro ? b.rnd(14.f, 18.f) : b.rnd(12.f, 20.f);
    float carT = carLen / speed, axleT = (metro ? 2.3f : 1.8f) / speed;
    float bogieT = (carLen - 4.f) / speed;
    int cars = metro ? 4 : b.irnd(8, 14);
    float trainLen = carT * (float)cars;
    float t0 = p.tc - trainLen * 0.5f;
    for (int c = 0; c < cars; c++)
        for (int bg = 0; bg < 2; bg++)
            for (int ax = 0; ax < 2; ax++) {
                float tt = t0 + (float)c * carT + (float)bg * bogieT + (float)ax * axleT;
                if (tt < 0.05f || tt > p.dur - 0.3f) continue;
                float lv = p.level(tt) * b.rnd(0.7f, 1.f);
                float pan = p.pan(tt);
                Mode m[3] = {{b.rnd(700.f, 900.f), 0.018f, 1.f}, {b.rnd(1500.f, 1800.f), 0.012f, 0.6f}, {b.rnd(280.f, 340.f), 0.03f, 0.8f}};
                modes(b, tt, 0.35f * lv, m, 3, pan);
                noise(b, tt, 0.04f, 0.25f * lv, 0.0005f, 0.008f, FLP, 1200.f, -1.f, 0.1f, 0.7f, 0, pan);
            }
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float lv = p.level(t);
        float dop = p.doppler(t);
        ph += whineF * dop / SR;
        if (ph >= 1.f) ph -= 1.f;
        float whine = metro ? (sinCycle(ph) * 0.12f + sinCycle(ph * 2.f) * 0.05f) : 0.f;
        float roar = roarBp.bp(pk.process(b.nz.white())) * 0.5f;
        float rum = rumLp.lp(br.process(b.nz.white())) * 1.4f;
        addStereo(b, i, (whine + roar + rum) * lv, p.pan(t));
    }
    lowpass(b, 3200.f, 0.6f);
}

// Gantry crane at the container terminal: drive whine up to speed, trolley rumble, a stop clunk.
static void a_crane(Buf& b) {
    float dur = b.rnd(3.5f, 5.5f);
    int n = S(dur);
    b.ensure(n);
    float f0 = b.rnd(260.f, 340.f), f1 = f0 * b.rnd(1.9f, 2.4f);
    float ph = 0.f;
    Svf gearBp, rumLp;
    gearBp.set(f1 * 3.1f, 4.f);
    rumLp.set(260.f, 0.7f);
    BrownNoise br;
    float acc = 1.1f, dec = 1.f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float u = t < acc ? SmoothStep(0.f, acc, t) : (t > dur - dec ? SmoothStep(dur, dur - dec, t) : 1.f);
        float f = f0 + (f1 - f0) * u;
        ph += f / SR;
        if (ph >= 1.f) ph -= 1.f;
        float whine = sinCycle(ph) * 0.5f + sinCycle(ph * 2.f) * 0.25f + sinCycle(ph * 3.02f) * 0.12f;
        float gear = gearBp.bp(b.nz.white()) * 0.4f;
        float rum = rumLp.lp(br.process(b.nz.white())) * 2.f;
        float env = Min(1.f, t / 0.15f) * Min(1.f, (dur - t) / 0.2f);
        b.L[(size_t)i] += (whine * (0.3f + 0.7f * u) + gear * u + rum * (0.4f + 0.6f * u)) * env * 0.5f;
    }
    // travel stops: brake clunk and a metallic ring through the structure
    subHit(b, dur - 0.05f, 0.6f, 90.f, 55.f, 0.08f);
    Mode ring[4] = {{b.rnd(180.f, 220.f), 0.5f, 1.f}, {b.rnd(430.f, 480.f), 0.35f, 0.6f}, {b.rnd(880.f, 950.f), 0.25f, 0.4f},
                    {b.rnd(1500.f, 1650.f), 0.15f, 0.3f}};
    modes(b, dur - 0.04f, 0.25f, ring, 4);
    reverb(b, 1.6f, 0.3f, 0.55f, 0.02f, 1.6f, 0.25f);
}

// A container set down on another: the steel box booms and rings, twist-locks clack.
static void a_container(Buf& b) {
    subHit(b, 0.f, 1.f, b.rnd(70.f, 95.f), b.rnd(38.f, 48.f), b.rnd(0.12f, 0.18f));
    Mode m[8];
    for (int k = 0; k < 8; k++) m[k] = {b.rnd(110.f, 950.f), b.rnd(0.25f, 0.9f), b.rnd(0.3f, 1.f)};
    modes(b, 0.f, 0.35f, m, 8);
    noise(b, 0.f, 0.3f, 0.6f, 0.001f, 0.05f, FLP, 1600.f, 500.f, 0.08f, 0.7f, 1);
    Mode c[3] = {{b.rnd(2200.f, 2600.f), 0.03f, 1.f}, {b.rnd(3400.f, 3900.f), 0.02f, 0.7f}, {b.rnd(5200.f, 5800.f), 0.015f, 0.4f}};
    modes(b, b.rnd(0.005f, 0.02f), 0.4f, c, 3);
    if (b.chance(0.6f)) mechClack(b, b.rnd(0.6f, 1.1f), 0.3f, 0.7f);
    reverb(b, 2.2f, 0.4f, 0.55f, 0.03f, 1.8f, 0.3f);
}

// Truck / straddle carrier reversing: the beeper.
static void a_reverseBeep(Buf& b) {
    float f = b.rnd(1050.f, 1250.f);
    int beeps = b.irnd(5, 8);
    float period = b.rnd(0.9f, 1.1f);
    for (int k = 0; k < beeps; k++) {
        float t0 = (float)k * period;
        tone(b, t0, 0.46f, f, f, 0.f, 0.5f, 0.004f, 10.f, 0.f, 0.f, 0.25f);
        noise(b, t0, 0.46f, 0.06f, 0.004f, 10.f, FBP, f * 3.f, -1.f, 0.1f, 4.f);
        int s1 = S(t0 + 0.45f);
        b.ensure(s1 + S(0.02f));
        for (int i = 0; i < S(0.02f); i++) b.L[(size_t)(s1 + i)] *= 1.f - (float)i / (float)S(0.02f);
        int s2 = s1 + S(0.02f), s3 = S(t0 + period);
        b.ensure(s3);
        for (int i = s2; i < s3; i++) b.L[(size_t)i] = 0.f;
    }
    lowpass(b, 4000.f);
    reverb(b, 1.2f, 0.3f, 0.5f, 0.02f, 1.4f, 0.3f);
}

// Construction: hammering on steel, an impact wrench, or a jackhammer burst.
static void a_construction(Buf& b, int var) {
    switch (var % 3) {
        case 0: {
            int hits = b.irnd(3, 6);
            float t = 0.f;
            for (int k = 0; k < hits; k++) {
                click(b, t, 0.7f, 3);
                Mode m[5] = {{b.rnd(380.f, 460.f), 0.25f, 1.f}, {b.rnd(1050.f, 1200.f), 0.18f, 0.7f}, {b.rnd(2100.f, 2400.f), 0.12f, 0.5f},
                             {b.rnd(3300.f, 3800.f), 0.08f, 0.35f}, {b.rnd(5200.f, 5900.f), 0.05f, 0.25f}};
                modes(b, t, 0.5f, m, 5);
                noise(b, t, 0.05f, 0.4f, 0.0003f, 0.008f, FLP, 1500.f, -1.f, 0.1f, 0.7f);
                t += b.rnd(0.45f, 0.75f);
            }
            break;
        }
        case 1: {  // impact wrench: motor whine and rapid hammer blows on a nut
            float dur = b.rnd(0.9f, 1.5f);
            int n = S(dur);
            b.ensure(n);
            float ph = 0.f, f = b.rnd(260.f, 320.f);
            for (int i = 0; i < n; i++) {
                ph += f / SR;
                if (ph >= 1.f) ph -= 1.f;
                float env = Min(1.f, (float)i / (SR * 0.05f)) * Min(1.f, (float)(n - i) / (SR * 0.05f));
                b.L[(size_t)i] += (sinCycle(ph) * 0.2f + sinCycle(ph * 2.f) * 0.1f) * env;
            }
            for (float t = 0.02f; t < dur; t += 1.f / b.rnd(24.f, 32.f)) {
                Mode m[2] = {{b.rnd(1800.f, 2200.f), 0.01f, 1.f}, {b.rnd(3600.f, 4200.f), 0.008f, 0.6f}};
                modes(b, t, 0.3f, m, 2);
                click(b, t, 0.25f, 2);
            }
            break;
        }
        default: {  // jackhammer
            float dur = b.rnd(1.2f, 2.2f);
            for (float t = 0.f; t < dur; t += 1.f / b.rnd(22.f, 26.f)) {
                subHit(b, t, 0.35f, 110.f, 70.f, 0.012f);
                Mode m[2] = {{b.rnd(900.f, 1100.f), 0.02f, 1.f}, {b.rnd(2400.f, 2800.f), 0.012f, 0.6f}};
                modes(b, t, 0.3f, m, 2);
                noise(b, t, 0.02f, 0.35f, 0.0003f, 0.005f, FBP, 1800.f, -1.f, 0.1f, 0.8f);
            }
            noise(b, 0.f, dur, 0.08f, 0.02f, 10.f, FHP, 3000.f, -1.f, 0.1f, 0.7f);
            break;
        }
    }
    reverb(b, 1.4f, 0.35f, 0.5f, 0.02f, 1.5f, 0.3f);
}

// Pig frog: a run of low grunts (a rapid pulse train through the vocal sac resonance).
static void a_pigFrog(Buf& b) {
    int grunts = b.irnd(3, 7);
    float t = 0.f;
    float f0 = b.rnd(260.f, 340.f), pulse = b.rnd(42.f, 56.f);
    for (int k = 0; k < grunts; k++) {
        float d = b.rnd(0.12f, 0.2f);
        int s0 = S(t), n = S(d);
        b.ensure(s0 + n);
        Svf r1, r2;
        r1.set(f0, 6.f);
        r2.set(f0 * 2.4f, 4.f);
        float ph = 0.f;
        for (int i = 0; i < n; i++) {
            ph += pulse / SR;
            float exc = 0.f;
            if (ph >= 1.f) {
                ph -= 1.f;
                exc = 1.f;
            }
            float u = (float)i / (float)n;
            float env = sinf(kPi * u);
            b.L[(size_t)(s0 + i)] += (r1.bp(exc) * 1.2f + r2.bp(exc) * 0.4f) * env;
        }
        t += d + b.rnd(0.15f, 0.35f);
    }
}

// Chorus frog: a rising trill of short clicks ("a fingernail along a comb").
static void a_chorusFrog(Buf& b) {
    int calls = b.irnd(2, 4);
    float t = 0.f;
    float f = b.rnd(2600.f, 3300.f);
    for (int c = 0; c < calls; c++) {
        int pulses = b.irnd(14, 22);
        float gap = b.rnd(0.03f, 0.04f);
        for (int k = 0; k < pulses; k++) {
            float u = (float)k / (float)pulses;
            float ff = f * (0.92f + 0.12f * u);
            tone(b, t, 0.012f, ff, ff, 0.f, 0.4f + 0.6f * u, 0.0015f, 0.004f, 0.f, 0.3f);
            t += gap * (1.f - 0.3f * u);
        }
        t += b.rnd(0.4f, 0.9f);
    }
}

// Limpkin: the wailing "kree-ow" of the Florida marsh at night.
static void a_limpkin(Buf& b) {
    int calls = b.irnd(2, 4);
    float t = 0.f;
    for (int c = 0; c < calls; c++) {
        float d = b.rnd(0.55f, 0.8f);
        int s0 = S(t), n = S(d);
        b.ensure(s0 + n);
        float ph = 0.f, jit = 0.f;
        Svf f1, f2;
        f1.set(1800.f, 3.f);
        f2.set(3100.f, 3.f);
        float fLo = b.rnd(1100.f, 1300.f), fHi = b.rnd(2000.f, 2400.f);
        for (int i = 0; i < n; i++) {
            float u = (float)i / (float)n;
            float contour = u < 0.35f ? SmoothStep(0.f, 0.35f, u) : 1.f - 0.55f * SmoothStep(0.35f, 1.f, u);
            if ((i & 63) == 0) jit = b.nz.white() * 0.02f;
            float f = (fLo + (fHi - fLo) * contour) * 0.5f * (1.f + jit);
            ph += f / SR;
            if (ph >= 1.f) ph -= 1.f;
            float src = (2.f * ph - 1.f) + b.nz.white() * 0.35f;  // harsh, noisy voice
            float env = Min(1.f, u / 0.08f) * Min(1.f, (1.f - u) / 0.15f);
            b.L[(size_t)(s0 + i)] += (f1.bp(src) * 0.9f + f2.bp(src) * 0.5f + src * 0.08f) * env;
        }
        t += d + b.rnd(0.25f, 0.45f);
    }
}

// Red-winged blackbird: "conk-la-reee" (two notes and a buzzy trill).
static void a_blackbird(Buf& b) {
    float p = b.rnd(0.94f, 1.06f);
    tone(b, 0.f, 0.09f, 1500.f * p, 2100.f * p, 0.02f, 0.5f, 0.004f, 0.03f, 0.f, 0.3f);
    tone(b, 0.12f, 0.07f, 2600.f * p, 2400.f * p, 0.02f, 0.45f, 0.004f, 0.025f, 0.f, 0.2f);
    float d = b.rnd(0.6f, 0.9f);
    int s0 = S(0.22f), n = S(d);
    b.ensure(s0 + n);
    float ph = 0.f, am = 0.f;
    float fc = b.rnd(3300.f, 3900.f) * p, rate = b.rnd(38.f, 48.f);
    Svf bp;
    bp.set(fc * 1.3f, 2.f);
    for (int i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        ph += fc / SR;
        if (ph >= 1.f) ph -= 1.f;
        am += rate / SR;
        if (am >= 1.f) am -= 1.f;
        float g = 0.5f + 0.5f * sinCycle(am);
        float v = (sinCycle(ph) * 0.6f + bp.bp(b.nz.white()) * 0.6f) * g;
        float env = Min(1.f, u / 0.1f) * Min(1.f, (1.f - u) / 0.3f);
        b.L[(size_t)(s0 + i)] += v * env * 0.45f;
    }
}

// A mosquito flying past the ear (stereo).
static void a_mosquito(Buf& b) {
    float dur = b.rnd(1.2f, 2.2f);
    int n = S(dur);
    b.ensure(n);
    float f = b.rnd(470.f, 640.f), ph = 0.f, wob = 0.f;
    float dir = b.chance(0.5f) ? 1.f : -1.f;
    for (int i = 0; i < n; i++) {
        float u = (float)i / (float)n;
        if ((i & 127) == 0) wob = b.nz.white() * 0.015f;
        ph += f * (1.f + 0.03f * sinf(u * 17.f) + wob) / SR;
        if (ph >= 1.f) ph -= 1.f;
        float v = sinCycle(ph) * 0.5f + sinCycle(ph * 2.f) * 0.35f + sinCycle(ph * 3.f) * 0.15f;
        float x = (u - 0.5f) * 3.2f;
        float lvl = 1.f / (1.f + x * x) * Min(1.f, u / 0.1f) * Min(1.f, (1.f - u) / 0.1f);
        addStereo(b, i, v * lvl * 0.5f, dir * 0.9f * tanhf(x));
    }
}

// Marina: halyards slapping an aluminium mast in the breeze.
static void a_halyard(Buf& b) {
    int hits = b.irnd(2, 5);
    float t = 0.f;
    float f = b.rnd(1700.f, 2100.f);
    for (int k = 0; k < hits; k++) {
        Mode m[4] = {{f, b.rnd(0.2f, 0.35f), 1.f}, {f * 2.13f, 0.18f, 0.6f}, {f * 3.4f, 0.12f, 0.4f}, {b.rnd(600.f, 700.f), 0.6f, 0.5f}};
        modes(b, t, b.rnd(0.3f, 0.6f), m, 4);
        click(b, t, 0.2f, 2);
        t += b.rnd(0.25f, 0.9f);
    }
    reverb(b, 0.9f, 0.2f, 0.5f, 0.01f, 1.2f, 0.3f);
}

// Bell buoy rocking in the swell: a few irregular strikes of a heavy bell.
static void a_buoyBell(Buf& b) {
    int hits = b.irnd(2, 4);
    float t = 0.f;
    float f = b.rnd(420.f, 520.f);
    for (int k = 0; k < hits; k++) {
        Mode m[6] = {{f * 0.5f, 2.2f, 0.5f}, {f, 1.8f, 1.f}, {f * 1.19f, 1.4f, 0.55f}, {f * 1.5f, 1.1f, 0.4f}, {f * 2.f, 0.9f, 0.45f},
                     {f * 2.74f, 0.6f, 0.25f}};
        modes(b, t, b.rnd(0.25f, 0.5f), m, 6, 0.f, 5.f);
        click(b, t, 0.15f, 3);
        t += b.rnd(1.1f, 2.4f);
    }
    reverb(b, 2.f, 0.25f, 0.6f, 0.03f, 1.6f, 0.2f);
}

// Thunder by distance (stereo, diffuse). band 0: close (< 1 km) - a tearing crack before the boom; 1: 1-3 km -
// boom and a long roll; 2: far - a low rumble.
static void a_thunder(Buf& b, int band) {
    float dur = band == 0 ? 7.5f : band == 1 ? 8.5f : 9.5f;
    float lp = band == 0 ? 2400.f : band == 1 ? 900.f : 380.f;
    float t0 = band == 0 ? 0.12f : band == 1 ? 0.05f : 0.3f;
    if (band == 0) {
        // the tearing crack (channel ripping) and the shock of the stroke
        click(b, 0.f, 1.f, 12, true, b.rnd(-0.3f, 0.3f));
        crackles(b, 0.f, 0.35f, 160.f, 0.9f, 400.f, 7000.f, 0.12f, 0.8f);
        noise(b, 0.f, 0.4f, 0.8f, 0.001f, 0.06f, FHP, 1200.f, 600.f, 0.2f, 0.6f, 0, -0.4f);
        noise(b, 0.f, 0.4f, 0.8f, 0.001f, 0.06f, FHP, 1300.f, 650.f, 0.2f, 0.6f, 0, 0.4f);
    }
    // the main boom
    noise(b, t0, 2.f, band == 2 ? 0.5f : 1.f, band == 0 ? 0.01f : 0.08f, band == 0 ? 0.45f : 0.7f, FLP4, band == 0 ? 900.f : 400.f, 120.f, 0.4f, 0.7f, 2,
          -0.3f);
    noise(b, t0 + 0.01f, 2.f, band == 2 ? 0.5f : 1.f, band == 0 ? 0.01f : 0.08f, band == 0 ? 0.45f : 0.7f, FLP4, band == 0 ? 950.f : 420.f, 110.f, 0.4f,
          0.7f, 2, 0.3f);
    tone(b, t0, 4.f, band == 0 ? 60.f : 48.f, 28.f, 1.f, band == 2 ? 0.25f : 0.45f, band == 0 ? 0.01f : 0.2f, 1.1f);
    // the roll: sound from farther parts of the channel arriving later, swelling and fading
    int swells = band == 0 ? 7 : band == 1 ? 10 : 8;
    for (int i = 0; i < swells; i++) {
        float ts = t0 + b.rnd(0.3f, dur * 0.6f);
        float a = b.rnd(0.3f, 0.9f) * (band == 2 ? 0.6f : 0.8f);
        noise(b, ts, dur - ts, a, b.rnd(0.15f, 0.7f), b.rnd(0.5f, 1.5f), FLP4, b.rnd(140.f, 320.f), b.rnd(60.f, 120.f), 1.f, 0.7f, 2,
              b.rnd(-0.7f, 0.7f));
    }
    lowpass(b, lp, 0.6f);
    reverb(b, 3.2f, 0.3f, 0.75f, 0.04f, 1.8f, 0.2f);
}

static void s_ambLayer(Buf& b, int id, int var) {
    switch (id) {
        case AMB_AIRBOAT: a_airboat(b); break;
        case AMB_BOAT_PASS: a_boatPass(b); break;
        case AMB_TRAIN_PASS: a_trainPass(b); break;
        case AMB_CRANE: a_crane(b); break;
        case AMB_CONTAINER: a_container(b); break;
        case AMB_REVERSE_BEEP: a_reverseBeep(b); break;
        case AMB_CONSTRUCTION: a_construction(b, var); break;
        case AMB_PIG_FROG: a_pigFrog(b); break;
        case AMB_CHORUS_FROG: a_chorusFrog(b); break;
        case AMB_LIMPKIN: a_limpkin(b); break;
        case AMB_BLACKBIRD: a_blackbird(b); break;
        case AMB_MOSQUITO: a_mosquito(b); break;
        case AMB_HALYARD: a_halyard(b); break;
        case AMB_BUOY_BELL: a_buoyBell(b); break;
        case AMB_THUNDER_CLOSE: a_thunder(b, 0); break;
        case AMB_THUNDER_MID: a_thunder(b, 1); break;
        case AMB_THUNDER_FAR: a_thunder(b, 2); break;
        default: break;
    }
}
