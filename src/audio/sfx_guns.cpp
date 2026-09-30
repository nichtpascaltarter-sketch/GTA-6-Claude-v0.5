// Firearms (included by sfx.cpp inside namespace sfxgen, after the synthesis toolkit). Every shot is assembled at
// runtime by the mixer (Mixer::startGunshot) from dry layers rendered here per weapon class:
//   GUN_NEAR    the report heard by a bystander: Friedlander blast wave (steep shock front, natural negative phase),
//               the resonant "bark" of the muzzle gases, turbulent gas noise, a low thump, HF sizzle and the ground
//               reflection a couple of milliseconds behind the direct wave;
//   GUN_FP      the shooter's own perspective: stronger low-end punch and transient, tighter decay, stereo sizzle;
//   GUN_MECH    the action cycling (slide / bolt carrier / open bolt / pump / bolt action / revolver frame ring);
//   GUN_SUP     the suppressed report (supersonic rifles keep their crack, a revolver leaks at the cylinder gap);
//   GUN_FAR_*   distant shots: the direct wave arrives rounded and low-passed, then urban rolling slap echoes or the
//               open-country boom with a long low rumble;
//   GUN_CRACK   the N-wave of a supersonic bullet passing near the listener.
// No reverb is baked into the close layers: the mixer adds the environment (early reflections from the probed
// facades, flutter, open-field echoes, room / tunnel reverb).

// Friedlander blast wave p(t) = A (1 - t/T) exp(-k t/T) after a raised-cosine rise of `rise` seconds.
static void blastWave(Buf& b, float t0, float amp, float T, float k, float rise, float lpFc, float pan = 0.f) {
    int s0 = S(t0), nr = Max(1, S(rise)), n = nr + S(T * 16.f) + 8;
    b.ensure(s0 + n);
    float gl, gr;
    panG(pan, gl, gr);
    Biquad lp;
    lp.setLP(Min(lpFc, SR * 0.45f), 0.55f);
    for (int i = 0; i < n; i++) {
        float v;
        if (i < nr) v = 0.5f - 0.5f * cosf(kPi * (float)i / (float)nr);
        else {
            float tt = (float)(i - nr) / SR;
            v = (1.f - tt / T) * expf(-k * tt / T);
        }
        b.addPan(s0 + i, lp.process(v * amp), gl, gr);
    }
}

// Copies [0, len) of the buffer to `delay` later with gain and a low-pass (ground / nearby surface reflection).
static void surfaceReflection(Buf& b, float delay, float len, float gain, float lpFc) {
    int n = Min(b.len(), S(len)), off = S(delay);
    if (n <= 0) return;
    b.ensure(off + n);
    Biquad lp;
    lp.setLP(lpFc, 0.6f);
    std::vector<float> src(b.L.begin(), b.L.begin() + n);
    for (int i = 0; i < n; i++) b.L[(size_t)(off + i)] += lp.process(src[(size_t)i]) * gain;
    if (b.stereo) {
        std::vector<float> srcR(b.R.begin(), b.R.begin() + n);
        lp.reset();
        for (int i = 0; i < n; i++) b.R[(size_t)(off + i)] += lp.process(srcR[(size_t)i]) * gain;
    }
}

struct GunTone {
    float T, blastLp;                          // blast positive phase (s) and bandwidth
    float barkAmp, barkF, barkQ, barkTau;      // resonant body of the report
    float gasAmp, gasTau, gasFc0, gasFc1;      // turbulent gas (low-pass glide)
    float thumpAmp, thumpF0, thumpF1, thumpTau;
    float sizzleAmp, sizzleTau, sizzleFc;      // high crackle
    float groundDelay, groundGain;
    float drive;
    float muzzleCrack;                         // supersonic bullet leaving the muzzle (rifles)
};

static const GunTone kGunTone[GC_COUNT] = {
    // T       lp      bark: amp  f      q     tau     gas: amp  tau    fc0    fc1    thump: amp f0    f1    tau     sizzle: amp tau   fc      ground     drive crack
    {0.00055f, 9000.f, 0.55f, 1150.f, 0.9f, 0.009f, 0.50f, 0.022f, 5200.f, 700.f, 0.55f, 115.f, 55.f, 0.025f, 0.26f, 0.005f, 5000.f, 0.0022f, 0.45f, 1.8f, 0.f},   // pistol
    {0.00080f, 8000.f, 0.65f, 850.f, 0.8f, 0.014f, 0.65f, 0.040f, 4200.f, 520.f, 0.80f, 95.f, 45.f, 0.042f, 0.34f, 0.007f, 4500.f, 0.0025f, 0.5f, 2.1f, 0.f},     // revolver
    {0.00045f, 10000.f, 0.45f, 1400.f, 1.0f, 0.007f, 0.42f, 0.016f, 6000.f, 900.f, 0.36f, 130.f, 70.f, 0.016f, 0.26f, 0.004f, 6000.f, 0.0020f, 0.4f, 1.6f, 0.f},  // smg
    {0.00035f, 12000.f, 0.50f, 1300.f, 0.8f, 0.010f, 0.60f, 0.030f, 6000.f, 720.f, 0.62f, 100.f, 48.f, 0.030f, 0.45f, 0.006f, 5500.f, 0.0023f, 0.45f, 2.0f, 0.55f}, // rifle
    {0.00100f, 6000.f, 0.60f, 650.f, 0.7f, 0.018f, 0.80f, 0.055f, 3500.f, 380.f, 1.00f, 80.f, 38.f, 0.055f, 0.20f, 0.006f, 4000.f, 0.0028f, 0.5f, 2.3f, 0.f},     // shotgun
    {0.00050f, 11000.f, 0.60f, 950.f, 0.8f, 0.014f, 0.80f, 0.045f, 5000.f, 500.f, 0.90f, 85.f, 40.f, 0.050f, 0.45f, 0.008f, 5000.f, 0.0025f, 0.5f, 2.2f, 0.7f},  // sniper
};

// The report. fp: shooter perspective (stereo buffer): punchier low end, tighter body, decorrelated sizzle.
static void gunReport(Buf& b, int cls, bool fp) {
    GunTone g = kGunTone[Clamp(cls, 0, GC_COUNT - 1)];
    float v = b.rnd(0.93f, 1.07f);
    if (fp) {
        g.T *= 0.8f;
        g.barkTau *= 0.8f;
        g.gasTau *= 0.72f;
        g.thumpAmp *= 1.35f;
        g.thumpTau *= 0.8f;
        g.sizzleAmp *= 1.4f;
        g.drive *= 1.2f;
        g.groundGain *= 0.6f;
    }
    // shock front + negative phase
    blastWave(b, 0.f, 1.f, g.T * v, 1.05f, 0.00004f, g.blastLp);
    if (g.muzzleCrack > 0.f) click(b, 0.f, g.muzzleCrack * (fp ? 1.1f : 1.f), b.irnd(4, 6), true);
    // resonant bark and turbulent gas
    noise(b, 0.00015f, g.barkTau * 9.f, g.barkAmp, 0.0003f, g.barkTau * v, FBP, g.barkF * v, g.barkF * 0.75f, g.barkTau * 2.f, g.barkQ);
    float gasPanL = fp ? -0.35f : 0.f, gasPanR = fp ? 0.35f : 0.f;
    noise(b, 0.0002f, g.gasTau * 7.f, g.gasAmp * (fp ? 0.72f : 1.f), 0.0006f, g.gasTau * v, FLP, g.gasFc0 * v, g.gasFc1, g.gasTau * 0.8f, 0.75f, 1, gasPanL);
    if (fp)
        noise(b, 0.0002f, g.gasTau * 7.f, g.gasAmp * 0.72f, 0.0006f, g.gasTau * v * 0.95f, FLP, g.gasFc0 * v * 1.05f, g.gasFc1, g.gasTau * 0.8f, 0.75f, 1, gasPanR);
    // low thump (the pressure felt in the chest)
    tone(b, 0.f, g.thumpTau * 7.f, g.thumpF0 * v, g.thumpF1, g.thumpTau * 0.5f, g.thumpAmp, 0.0006f, g.thumpTau, 0.f, 0.22f);
    // sizzle: HF crackle of the expanding gases
    noise(b, 0.f, g.sizzleTau * 8.f, g.sizzleAmp, 0.0002f, g.sizzleTau, FHP, g.sizzleFc, -1.f, 0.1f, 0.7f, 0, fp ? -0.5f : 0.f);
    if (fp) noise(b, 0.00005f, g.sizzleTau * 8.f, g.sizzleAmp, 0.0002f, g.sizzleTau * 1.1f, FHP, g.sizzleFc * 1.08f, -1.f, 0.1f, 0.7f, 0, 0.5f);
    crackles(b, 0.001f, g.gasTau * 2.5f, 900.f, g.sizzleAmp * 0.35f, 2500.f, 9000.f, g.gasTau * 0.8f, fp ? 0.6f : 0.f);
    // ground reflection a couple of milliseconds behind (both shooter and listener stand above the ground)
    surfaceReflection(b, g.groundDelay * b.rnd(0.85f, 1.2f), 0.012f, g.groundGain, 4500.f);
    saturate(b, g.drive);
}

// Action cycling heard up close.
static void gunMech(Buf& b, int cls) {
    float j = b.rnd(0.9f, 1.1f);
    auto casingDrop = [&](float t0, float amp, float pitch) {
        float f1 = b.rnd(3600.f, 4800.f) * pitch;
        float t = t0, a = amp;
        int bounces = b.irnd(2, 4);
        for (int k = 0; k < bounces; k++) {
            Mode m[4] = {{f1, b.rnd(0.04f, 0.09f), 1.f}, {f1 * 1.58f, 0.06f, 0.6f}, {f1 * 2.24f, 0.045f, 0.45f}, {f1 * 2.93f, 0.03f, 0.3f}};
            modes(b, t, a * 0.5f, m, 4);
            click(b, t, a * 0.25f, 2);
            t += b.rnd(0.05f, 0.1f) * (1.f - 0.2f * (float)k);
            a *= b.rnd(0.35f, 0.55f);
        }
    };
    switch (cls) {
        case GC_PISTOL:
            mechClack(b, 0.007f * j, 0.7f, 1.15f);                 // slide back, casing out
            click(b, 0.011f * j, 0.2f, 2);
            mechClack(b, 0.026f * j, 0.95f, 0.9f);                 // slide returns to battery
            tone(b, 0.026f * j, 0.03f, 420.f, 300.f, 0.01f, 0.25f, 0.0003f, 0.008f);
            casingDrop(b.rnd(0.38f, 0.62f), 0.3f, 1.f);
            break;
        case GC_REVOLVER: {
            Mode ring[4] = {{b.rnd(2100.f, 2400.f), 0.06f, 1.f}, {b.rnd(3300.f, 3700.f), 0.045f, 0.7f}, {b.rnd(5100.f, 5600.f), 0.03f, 0.5f},
                            {b.rnd(1250.f, 1400.f), 0.08f, 0.5f}};
            modes(b, 0.001f, 0.35f, ring, 4);                       // frame ringing with the shot
            Mode ratchet[2] = {{b.rnd(4200.f, 4700.f), 0.006f, 1.f}, {b.rnd(6900.f, 7400.f), 0.004f, 0.6f}};
            modes(b, b.rnd(0.16f, 0.24f), 0.35f, ratchet, 2);       // trigger reset / cylinder stop
            break;
        }
        case GC_SMG: {
            mechClack(b, 0.004f * j, 0.8f, 1.2f);                  // bolt slams into the buffer
            mechClack(b, 0.019f * j, 1.f, 0.85f);                  // bolt forward (open bolt)
            Mode spring[2] = {{b.rnd(850.f, 950.f), 0.03f, 1.f}, {b.rnd(2300.f, 2500.f), 0.02f, 0.6f}};
            modes(b, 0.006f * j, 0.12f, spring, 2);
            if (b.chance(0.5f)) casingDrop(b.rnd(0.3f, 0.5f), 0.18f, 1.1f);
            break;
        }
        case GC_RIFLE: {
            mechClack(b, 0.008f * j, 0.75f, 1.05f);                // bolt carrier back
            Mode spring[3] = {{b.rnd(760.f, 840.f), 0.035f, 1.f}, {b.rnd(2200.f, 2400.f), 0.025f, 0.7f}, {b.rnd(3900.f, 4200.f), 0.015f, 0.4f}};
            modes(b, 0.009f * j, 0.18f, spring, 3);                // buffer spring "sproing"
            mechClack(b, 0.036f * j, 1.f, 0.82f);                  // carrier forward, bolt locks
            tone(b, 0.036f * j, 0.03f, 360.f, 250.f, 0.01f, 0.28f, 0.0003f, 0.01f);
            casingDrop(b.rnd(0.45f, 0.75f), 0.28f, 0.95f);
            break;
        }
        case GC_SHOTGUN: {
            float tp = b.rnd(0.38f, 0.48f);                        // pump: back...
            noise(b, tp, 0.09f, 0.22f, 0.012f, 0.03f, FBP, 2300.f, 1800.f, 0.05f, 1.f);
            mechClack(b, tp + 0.07f, 0.5f, 0.75f);
            tone(b, tp + 0.07f, 0.04f, 260.f, 180.f, 0.01f, 0.2f, 0.0005f, 0.012f);
            float tf = tp + b.rnd(0.14f, 0.18f);                   // ...and forward
            noise(b, tf, 0.08f, 0.18f, 0.01f, 0.03f, FBP, 2700.f, 2100.f, 0.05f, 1.f);
            mechClack(b, tf + 0.065f, 0.6f, 0.9f);
            // the spent hull hits the ground (plastic: duller)
            float th = tp + b.rnd(0.45f, 0.6f);
            Mode hull[3] = {{b.rnd(900.f, 1100.f), 0.02f, 1.f}, {b.rnd(1900.f, 2300.f), 0.015f, 0.6f}, {b.rnd(3400.f, 3800.f), 0.01f, 0.35f}};
            modes(b, th, 0.25f, hull, 3);
            modes(b, th + b.rnd(0.08f, 0.12f), 0.1f, hull, 3);
            break;
        }
        case GC_SNIPER: {
            float t = b.rnd(0.55f, 0.75f);                         // bolt action after the shot settles
            mechClack(b, t, 0.45f, 1.2f);                          // lift the handle
            noise(b, t + 0.05f, 0.11f, 0.16f, 0.02f, 0.04f, FBP, 2600.f, 2000.f, 0.08f, 1.2f);
            mechClack(b, t + 0.15f, 0.6f, 0.95f);                  // bolt back, casing out
            casingDrop(t + b.rnd(0.35f, 0.5f), 0.26f, 0.85f);
            noise(b, t + 0.3f, 0.1f, 0.14f, 0.02f, 0.035f, FBP, 2900.f, 2300.f, 0.08f, 1.2f);
            mechClack(b, t + 0.4f, 0.65f, 0.9f);                   // forward, chamber a round
            mechClack(b, t + 0.47f, 0.5f, 1.1f);                   // handle down, locked
            break;
        }
    }
}

// Suppressed report.
static void gunSuppressed(Buf& b, int cls) {
    float v = b.rnd(0.92f, 1.08f);
    switch (cls) {
        case GC_PISTOL:
        case GC_SMG: {
            float k = cls == GC_SMG ? 0.8f : 1.f;
            noise(b, 0.f, 0.06f, 0.55f, 0.0004f, 0.007f * k, FBP, 1700.f * v, 1100.f, 0.02f, 0.75f);
            noise(b, 0.f, 0.1f, 0.6f, 0.0005f, 0.013f * k, FLP, 900.f * v, 320.f, 0.02f, 0.8f, 1);
            tone(b, 0.f, 0.08f, 170.f * v, 90.f, 0.01f, 0.4f, 0.0004f, 0.011f * k);
            click(b, 0.f, 0.18f, 6);
            noise(b, 0.0005f, 0.03f, 0.12f, 0.0002f, 0.003f, FHP, 4500.f, -1.f, 0.1f, 0.7f);
            break;
        }
        case GC_REVOLVER: {  // the cylinder gap leaks: still a crack
            blastWave(b, 0.f, 0.5f, 0.0006f * v, 1.05f, 0.00005f, 7000.f);
            noise(b, 0.f, 0.1f, 0.5f, 0.0004f, 0.012f, FBP, 1500.f * v, 900.f, 0.03f, 0.8f);
            noise(b, 0.f, 0.12f, 0.45f, 0.0005f, 0.018f, FLP, 1100.f, 350.f, 0.03f, 0.8f, 1);
            noise(b, 0.f, 0.04f, 0.3f, 0.0002f, 0.005f, FHP, 4200.f, -1.f, 0.1f, 0.7f);
            tone(b, 0.f, 0.1f, 140.f * v, 70.f, 0.015f, 0.45f, 0.0005f, 0.016f);
            break;
        }
        case GC_RIFLE:
        case GC_SNIPER: {  // the bullet is still supersonic: sharp crack + a thud from the can
            float big = cls == GC_SNIPER ? 1.25f : 1.f;
            click(b, 0.f, 0.8f, b.irnd(4, 6), true);
            noise(b, 0.f, 0.04f, 0.35f, 0.0002f, 0.004f, FHP, 3500.f, -1.f, 0.1f, 0.7f);
            noise(b, 0.0005f, 0.15f, 0.55f * big, 0.0006f, 0.018f * big, FLP, 800.f * v, 300.f, 0.03f, 0.8f, 1);
            noise(b, 0.0005f, 0.08f, 0.35f, 0.0005f, 0.01f, FBP, 1300.f * v, 900.f, 0.02f, 0.8f);
            tone(b, 0.f, 0.12f, 125.f * v, 60.f, 0.015f, 0.55f * big, 0.0005f, 0.02f * big);
            break;
        }
        case GC_SHOTGUN: {
            noise(b, 0.f, 0.2f, 0.7f, 0.0008f, 0.03f, FLP, 500.f * v, 200.f, 0.04f, 0.8f, 1);
            noise(b, 0.f, 0.1f, 0.35f, 0.0005f, 0.012f, FBP, 1000.f * v, 700.f, 0.02f, 0.8f);
            tone(b, 0.f, 0.3f, 95.f * v, 45.f, 0.03f, 0.8f, 0.0008f, 0.045f);
            click(b, 0.f, 0.2f, 8);
            break;
        }
    }
    saturate(b, 1.4f);
}

// Distant shot. group: GunFar; urban: among buildings (rolling slap echoes) or over open country (boom + rumble).
static void gunFar(Buf& b, int group, bool urban) {
    float v = b.rnd(0.92f, 1.08f);
    float T = group == GF_LIGHT ? 0.0016f : group == GF_HEAVY ? 0.0022f : 0.0027f;
    float big = group == GF_LIGHT ? 0.7f : group == GF_HEAVY ? 1.f : 1.25f;
    // the direct wave: rounded by distance, highs absorbed by the air
    blastWave(b, 0.f, 1.f, T * v, 1.1f, 0.0004f, group == GF_LIGHT ? 3400.f : 2800.f);
    tone(b, 0.f, 0.5f, 85.f * v, 42.f, 0.04f, 0.6f * big, 0.002f, 0.06f * big, 0.f, 0.15f);
    noise(b, 0.f, 0.3f, 0.5f * big, 0.002f, 0.035f * big, FLP, 1400.f * v, 400.f, 0.05f, 0.7f, 1);
    noise(b, 0.0005f, 0.15f, 0.3f, 0.001f, 0.012f, FBP, 900.f * v, 600.f, 0.05f, 0.8f);
    if (urban) {
        // facades all around: a dense cluster of slap echoes rolling away over 1-2 s
        int count = group == GF_LIGHT ? 12 : group == GF_HEAVY ? 16 : 20;
        echoes(b, count, b.rnd(0.05f, 0.09f), 0.075f * big, 0.62f, 0.86f, 2200.f, 0.09f);
        reverb(b, 1.6f + 0.4f * big, 0.42f, 0.5f, 0.02f, 1.5f, 0.3f);
    } else {
        // tree lines and hills: a few late echoes over a long low rumble
        int count = group == GF_LIGHT ? 3 : group == GF_HEAVY ? 4 : 6;
        echoes(b, count, b.rnd(0.3f, 0.55f), 0.42f * big, 0.42f, 0.75f, 1300.f, 0.12f);
        noise(b, 0.04f, 3.5f * big, 0.2f * big, 0.12f, 0.9f * big, FLP, 260.f, 140.f, 1.5f, 0.7f, 2);
        reverb(b, 2.4f + 0.6f * big, 0.3f, 0.72f, 0.03f, 1.6f, 0.2f);
    }
    lowpass(b, group == GF_LIGHT ? 4200.f : 3600.f, 0.6f);
    saturate(b, 1.25f);
}

// Supersonic bullet passing close by: N-wave (sharp positive then negative jump) and a short air whip.
static void gunCrack(Buf& b) {
    int w = b.irnd(5, 12);
    click(b, 0.f, 1.f, w, true);
    noise(b, 0.f, 0.03f, 0.35f, 0.0001f, 0.0025f, FHP, 4000.f, -1.f, 0.1f, 0.7f);
    noise(b, 0.0008f, 0.12f, 0.12f, 0.004f, 0.02f, FBP, b.rnd(2200.f, 3200.f), 1400.f, 0.06f, 1.2f);
}

static void s_gunLayer(Buf& b, int id) {
    if (id >= GUN_NEAR && id < GUN_FP) gunReport(b, id - GUN_NEAR, false);
    else if (id >= GUN_FP && id < GUN_MECH) gunReport(b, id - GUN_FP, true);
    else if (id >= GUN_MECH && id < GUN_SUP) gunMech(b, id - GUN_MECH);
    else if (id >= GUN_SUP && id < GUN_FAR_URBAN) gunSuppressed(b, id - GUN_SUP);
    else if (id >= GUN_FAR_URBAN && id < GUN_FAR_OPEN) gunFar(b, id - GUN_FAR_URBAN, true);
    else if (id >= GUN_FAR_OPEN && id < GUN_CRACK) gunFar(b, id - GUN_FAR_OPEN, false);
    else if (id == GUN_CRACK) gunCrack(b);
}
