// Fabric folds as fold lines. Each fold is a ridge along a short, slightly bowed line on the garment, in the garment's
// own parametrization (limbs: along the limb x arc length around it; torso: height x arc length), tapered to nothing at
// its ends, with sharper valleys either side: compression folds in the crook of the elbow and behind the knee, stacking
// above long cuffs and at the trouser break, drag lines from the crotch, smile lines under the seat, drape from the
// armpits, pipe folds hanging from the chest of a loose top, blousing over a waistband, denim whiskers. The folds move
// the shell at LOD0 (refineShell adds vertices where the skin tessellation cannot carry them) and write the crease
// channel (valleys plus fine-wrinkle zones), which the cloth shader keeps at every LOD.
struct FoldLine {
    u8 part;          // PART_ARM / PART_LEG / PART_TORSO
    s8 side;          // limbs: 0 left, 1 right, -1 both (torso folds are placed by their angle)
    float a0, th0;    // centre: along the limb (m) or height (torso, m), and the angle around it (rad)
    float halfLen;    // half length along the fold (m)
    float w;          // ridge half width (m)
    float h;          // ridge height (m)
    float cd, sd;     // direction: (1, 0) runs around the limb / torso, (0, 1) along it
    float trough;     // crease channel strength of the valleys either side
    float bend;       // bow of the line (1/m)
};
struct FoldSet {
    std::vector<FoldLine> L;
    // fine-wrinkle zones (crease channel only): elbow crook / knee back centres (along, m) and strengths
    float elbowA = -1.f, elbowK = 0.f, kneeA = -1.f, kneeK = 0.f, zoneW = 0.03f;
};
struct FoldSpec {
    float amp = 1.f;          // overall scale (0 = none): looser garments fold more
    float sleeveEnd = 0.f;    // arm: along-length of the sleeve (0 = no sleeve)
    float legCuffZ = 0.f;     // leg: height of the trouser hem (0 = no trouser legs)
    bool legLong = false;     // full-length trousers (break on the shoe)
    float waistZ = 0.f;       // torso: height of the hem / waistband the top bunches over (0 = none)
    bool tucked = false;      // tucked hem: blousing just above the waistband
    bool denim = false;       // jeans whiskers
    float hangTop = 0.f;      // torso: where a loose top starts to hang (pipe folds below it)
    float hang = 0.f;         // 0..1 how freely the top hangs (pipe folds)
    bool openFront = false;   // open outer layer: deeper vertical folds on the front panels
    u32 seed = 0;
};

static void addFold(FoldSet& F, u8 part, int side, float a0, float th0, float halfLen, float w, float h, float dir, float trough, float bend = 0.f) {
    FoldLine f;
    f.part = part;
    f.side = (s8)side;
    f.a0 = a0;
    f.th0 = th0;
    f.halfLen = halfLen;
    f.w = w;
    f.h = h;
    f.cd = cosf(dir);
    f.sd = sinf(dir);
    f.trough = trough;
    f.bend = bend;
    F.L.push_back(f);
}

// Along-coordinate of the leg skin at height z (legs run almost straight down from the hip joint).
static float legAlongAtZ(const BodyDims& D, float z) {
    float vz = Max(fabsf(D.legDir[0].z), 0.5f);
    return (D.J[B_THIGH_L].z - z) / vz;
}

static std::shared_ptr<FoldSet> makeFolds(const BuildCtx& c, const FoldSpec& fs) {
    auto F = std::make_shared<FoldSet>();
    if (fs.amp <= 0.f) return F;
    const BodyDims& D = *c.D;
    const float s = D.s, A = fs.amp;
    Rng r(hash32(fs.seed * 747796405u + 0x3C6EF372u));
    auto R = [&](float a, float b) { return r.range(a, b); };
    // ---- sleeves
    if (fs.sleeveEnd > 0.f) {
        const float eA = D.upperArm, rArm = D.rUpperArm * 1.15f;
        const bool longS = fs.sleeveEnd > eA + 0.1f * s;
        for (int sd = 0; sd < 2; sd++) {
            if (longS) {
                // compression folds in the crook of the elbow: three or four bowed ridges on the inside of the arm
                int n = 3 + (r.f() < 0.5f ? 1 : 0);
                for (int k = 0; k < n; k++)
                    addFold(*F, PART_ARM, sd, eA + ((float)k - 0.5f * (n - 1)) * 0.021f * s + R(-0.004f, 0.004f) * s, R(-0.35f, 0.35f),
                            rArm * R(1.1f, 1.6f), R(0.0045f, 0.006f) * s, R(0.0035f, 0.0055f) * A, R(-0.35f, 0.35f), 0.75f, R(-5.f, 5.f));
                // spiral stacking above the cuff
                float cuffA = Min(fs.sleeveEnd, D.upperArm + D.forearm);
                for (int k = 0; k < 3; k++)
                    addFold(*F, PART_ARM, sd, cuffA - (0.04f + 0.027f * k) * s + R(-0.004f, 0.004f) * s, R(0.f, kTwoPi), rArm * R(1.1f, 2.f),
                            R(0.0045f, 0.0058f) * s, R(0.0025f, 0.0042f) * A, R(-0.6f, 0.6f), 0.6f, R(-4.f, 4.f));
                F->elbowA = eA;
                F->elbowK = 0.35f;
            }
            // drape from the armpit down the underside of the upper arm
            int nd = longS ? 2 : 1;
            for (int k = 0; k < nd; k++) {
                float a0 = longS ? R(0.08f, 0.14f) * s : Min(R(0.05f, 0.08f) * s, fs.sleeveEnd - 0.02f * s);
                addFold(*F, PART_ARM, sd, a0, 1.5f * kPi + R(-0.4f, 0.4f), R(0.035f, 0.055f) * s, R(0.005f, 0.007f) * s, R(0.0022f, 0.0035f) * A,
                        kHalfPi + R(-0.45f, 0.45f), 0.4f);
            }
            if (!longS && fs.sleeveEnd > 0.06f * s) {
                // a short sleeve's hem flares in one or two soft folds on the outside
                int nf = r.f() < 0.6f ? 1 : 2;
                for (int k = 0; k < nf; k++)
                    addFold(*F, PART_ARM, sd, fs.sleeveEnd - R(0.015f, 0.03f) * s, kHalfPi + R(-0.9f, 0.9f), R(0.02f, 0.03f) * s,
                            R(0.005f, 0.007f) * s, R(0.0018f, 0.003f) * A, kHalfPi + R(-0.3f, 0.3f), 0.35f);
            }
        }
    }
    // ---- torso
    if (fs.waistZ > 0.f) {
        // drape from the armpits: a diagonal fold down and in towards the front and the back on either side
        const float zA = D.zArmpit;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            addFold(*F, PART_TORSO, -1, zA - R(0.04f, 0.06f) * s, sx * (kHalfPi - R(0.45f, 0.65f)), R(0.05f, 0.07f) * s, R(0.006f, 0.008f) * s,
                    R(0.0022f, 0.0032f) * A, kHalfPi - sx * R(0.45f, 0.65f), 0.4f);
            addFold(*F, PART_TORSO, -1, zA - R(0.04f, 0.07f) * s, sx * (kHalfPi + R(0.5f, 0.7f)), R(0.05f, 0.07f) * s, R(0.006f, 0.008f) * s,
                    R(0.002f, 0.003f) * A, kHalfPi + sx * R(0.45f, 0.65f), 0.35f);
        }
        if (fs.tucked) {
            // blousing all round just above the waistband, and short vertical creases running into it
            for (int k = 0; k < 6; k++)
                addFold(*F, PART_TORSO, -1, fs.waistZ + R(0.028f, 0.04f) * s, kTwoPi * (k + R(0.f, 0.6f)) / 6.f, R(0.07f, 0.1f) * s,
                        R(0.01f, 0.014f) * s, R(0.003f, 0.0045f) * A, R(-0.12f, 0.12f), 0.3f);
            int n = 10 + (int)(r.f() * 4.f);
            for (int k = 0; k < n; k++)
                addFold(*F, PART_TORSO, -1, fs.waistZ + R(0.015f, 0.03f) * s, kTwoPi * (k + R(0.f, 0.7f)) / n, R(0.018f, 0.03f) * s,
                        R(0.0035f, 0.0045f) * s, R(0.0014f, 0.0022f) * A, kHalfPi + R(-0.3f, 0.3f), 0.55f);
        } else {
            // bunching at the sides above the hem
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                for (int k = 0; k < 2; k++)
                    addFold(*F, PART_TORSO, -1, fs.waistZ + (0.035f + 0.03f * k) * s + R(-0.006f, 0.006f) * s, sx * kHalfPi + R(-0.35f, 0.35f),
                            R(0.05f, 0.08f) * s, R(0.006f, 0.008f) * s, R(0.0022f, 0.0035f) * A, R(-0.25f, 0.25f), 0.35f);
            }
        }
        if (fs.hang > 0.f && fs.hangTop > fs.waistZ + 0.1f * s) {
            // pipe folds hanging from the chest (or the bust) to the hem, a few on the front and the back
            float mid = 0.5f * (fs.hangTop + fs.waistZ), half = 0.45f * (fs.hangTop - fs.waistZ);
            int nf = 2 + (r.f() < 0.5f ? 1 : 0), nb = 2;
            for (int k = 0; k < nf; k++)
                addFold(*F, PART_TORSO, -1, mid - R(0.f, 0.04f) * s, R(-0.75f, 0.75f), half * R(0.7f, 1.f), R(0.009f, 0.013f) * s,
                        R(0.003f, 0.0048f) * fs.hang * A, kHalfPi + R(-0.12f, 0.12f), 0.3f);
            for (int k = 0; k < nb; k++)
                addFold(*F, PART_TORSO, -1, mid - R(0.f, 0.05f) * s, kPi + R(-0.7f, 0.7f), half * R(0.6f, 0.95f), R(0.01f, 0.014f) * s,
                        R(0.0025f, 0.004f) * fs.hang * A, kHalfPi + R(-0.15f, 0.15f), 0.25f);
        }
        if (fs.openFront) {
            // the open front panels fall in two deeper vertical folds each, the back in one from each shoulder blade
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                for (int k = 0; k < 2; k++)
                    addFold(*F, PART_TORSO, -1, 0.5f * (D.zArmpit + fs.waistZ) - R(0.f, 0.05f) * s, sx * R(0.25f, 1.1f),
                            0.42f * (D.zArmpit - fs.waistZ), R(0.011f, 0.015f) * s, R(0.004f, 0.006f) * A, kHalfPi + sx * R(-0.08f, 0.15f), 0.35f);
                addFold(*F, PART_TORSO, -1, 0.5f * (D.zArmpit + fs.waistZ), kPi - sx * R(0.35f, 0.7f), 0.38f * (D.zArmpit - fs.waistZ),
                        R(0.012f, 0.016f) * s, R(0.003f, 0.0045f) * A, kHalfPi, 0.25f);
            }
        }
    }
    // ---- trouser legs
    if (fs.legCuffZ > 0.f) {
        const float kA = D.thigh, cuffA = legAlongAtZ(D, fs.legCuffZ), rLeg = D.rKnee * 1.25f;
        const bool coversKnee = cuffA > kA + 0.03f * s;
        for (int sd = 0; sd < 2; sd++) {
            if (coversKnee) {
                // compression folds behind the knee, one soft line across the front above it
                for (int k = 0; k < 3; k++)
                    addFold(*F, PART_LEG, sd, kA + ((float)k - 1.f) * 0.022f * s + R(-0.004f, 0.004f) * s, kPi + R(-0.3f, 0.3f), rLeg * R(1.f, 1.45f),
                            R(0.0045f, 0.0058f) * s, R(0.0038f, 0.0058f) * A, R(-0.3f, 0.3f), 0.7f, R(-4.f, 4.f));
                addFold(*F, PART_LEG, sd, kA - R(0.035f, 0.05f) * s, R(-0.2f, 0.2f), rLeg * R(0.7f, 1.f), R(0.007f, 0.009f) * s, 0.0018f * A,
                        R(-0.2f, 0.2f), 0.2f);
                F->kneeA = kA;
                F->kneeK = 0.3f;
            }
            // drag lines from the crotch down the front of the inner thigh, smile lines under the seat
            for (int k = 0; k < 2; k++)
                addFold(*F, PART_LEG, sd, R(0.05f, 0.1f) * s, R(-0.85f, -0.45f), R(0.045f, 0.065f) * s, R(0.005f, 0.0065f) * s, R(0.0022f, 0.0032f) * A,
                        R(0.85f, 1.15f), 0.45f);
            for (int k = 0; k < 2; k++)
                addFold(*F, PART_LEG, sd, R(0.03f, 0.075f) * s, kPi - R(0.2f, 0.6f), R(0.04f, 0.055f) * s, R(0.0045f, 0.006f) * s,
                        R(0.0018f, 0.0028f) * A, R(0.1f, 0.4f), 0.45f);
            if (fs.legLong) {
                // the break: the trouser front stacks where it meets the shoe, a smaller fold at the back
                for (int k = 0; k < 2; k++)
                    addFold(*F, PART_LEG, sd, cuffA - (0.028f + 0.03f * k) * s + R(-0.004f, 0.004f) * s, R(-0.25f, 0.25f), rLeg * R(0.9f, 1.3f),
                            R(0.0055f, 0.007f) * s, R(0.005f, 0.007f) * A * (k ? 0.7f : 1.f), R(-0.3f, 0.3f), 0.6f, R(-3.f, 3.f));
                addFold(*F, PART_LEG, sd, cuffA - R(0.03f, 0.045f) * s, kPi + R(-0.3f, 0.3f), rLeg * R(0.7f, 1.f), 0.0055f * s, 0.003f * A,
                        R(-0.25f, 0.25f), 0.4f);
            } else if (!coversKnee) {
                // shorts: a soft fold or two near the hem
                for (int k = 0; k < 2; k++)
                    addFold(*F, PART_LEG, sd, cuffA - R(0.02f, 0.045f) * s, R(0.f, kTwoPi), rLeg * R(1.f, 1.6f), R(0.006f, 0.008f) * s,
                            R(0.0022f, 0.0035f) * A, R(-0.5f, 0.5f), 0.35f);
            }
            if (fs.denim)
                for (int k = 0; k < 4; k++)
                    addFold(*F, PART_LEG, sd, R(0.02f, 0.075f) * s, -R(0.12f, 0.6f), R(0.022f, 0.035f) * s, 0.0024f * s, 0.0009f * A,
                            R(0.45f, 0.85f), 0.85f);
        }
    }
    return F;
}

static void evalFolds(const FoldSet& F, const BVert& v, float s, float& off, float& crease) {
    off = 0.f;
    crease = 0.f;
    if (v.part != PART_ARM && v.part != PART_LEG && v.part != PART_TORSO) return;
    float r = -1.f;
    for (const FoldLine& f : F.L) {
        if (f.part != v.part) continue;
        if (f.side >= 0 && f.side != (int)v.side) continue;
        if (r < 0.f) {
            vec3 d = v.bp - v.axisPt;
            if (v.part == PART_TORSO) d.z = 0.f;
            r = Clamp(length(d), 0.02f * s, 0.25f * s);
        }
        float da = v.pa - f.a0;
        if (fabsf(da) > f.halfLen + 4.f * f.w) continue;
        float ds = wrapAngle(v.pb - f.th0) * r;
        float u = ds * f.cd + da * f.sd;
        if (fabsf(u) >= f.halfLen) continue;
        float n = -ds * f.sd + da * f.cd - f.bend * u * u;
        float x = n / f.w;
        if (fabsf(x) > 3.2f) continue;
        float q = u / f.halfLen;
        float taper = Sq(1.f - q * q);
        off += f.h * taper * expf(-x * x);
        crease += f.trough * taper * expf(-Sq((fabsf(x) - 1.4f) / 0.45f));
    }
    // fine-wrinkle zones: the shader's wrinkles in the crook of the elbow and behind the knee
    if (v.part == PART_ARM && F.elbowA > 0.f) crease += F.elbowK * bump(v.pa, F.elbowA, F.zoneW * s) * sstep(-0.2f, 0.9f, cosf(v.pb));
    if (v.part == PART_LEG && F.kneeA > 0.f) crease += F.kneeK * bump(v.pa, F.kneeA + 0.01f * s, F.zoneW * s) * sstep(-0.2f, 0.9f, -cosf(v.pb));
    crease = Saturate(crease);
}

// Fold function of a fold set, for GarmentDef::foldFn.
static std::function<void(const BVert&, float&, float&)> foldFnOf(const std::shared_ptr<FoldSet>& F, float s) {
    return [F, s](const BVert& v, float& off, float& cr) { evalFolds(*F, v, s, off, cr); };
}

