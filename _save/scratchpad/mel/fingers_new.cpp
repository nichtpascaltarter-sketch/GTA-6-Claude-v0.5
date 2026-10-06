// Fingers and thumbs: one tube per digit along its joint polyline (a base inside the palm / thenar, the knuckle or
// the thumb's CMC joint, the middle and end joints, a rounded tip), elliptical in section with palmar pads, dorsal
// knuckle bumps, joint creases (geometry plus the skin shader's crease channel) and a separate nail plate; skinned
// per phalanx (hand -> proximal -> middle -> distal) with blend zones centred on the joints. The dorsal side hands
// over to the next bone a little later than the palmar side, so a flexed knuckle keeps its bony corner while the
// palm side folds.
struct Digit {
    vec3 P[5];      // base, joint 1 (knuckle; thumb: CMC), joint 2, joint 3, tip
    float S[5];     // arc length of each point from the base
    int bone[4];    // skin bone of each segment
    vec3 back;      // dorsal (nail side) direction
    float r0;       // radius at joint 1
    bool thumb;
};

// Centre point and tangent of a digit's axis at arc length u (extrapolated beyond the base and the tip).
static void digitAxis(const Digit& g, float u, vec3& p, vec3& t) {
    int i = 0;
    while (i < 3 && u > g.S[i + 1]) i++;
    vec3 d = normalize(g.P[i + 1] - g.P[i]);
    p = g.P[i] + d * (u - g.S[i]);
    t = d;
    for (int j = 1; j <= 3; j++) {
        float x = (u - g.S[j]) / 0.0035f;
        if (fabsf(x) < 1.f) t = normalize(lerp(normalize(g.P[j] - g.P[j - 1]), normalize(g.P[j + 1] - g.P[j]), sstep(0.5f + 0.5f * x)));
    }
}

// Section half-axes at arc length u without the tip rounding: a = half width, bd / bp = dorsal / palmar half
// thickness.
static void digitSection(const Digit& g, float u, float s, float& a, float& bd, float& bp) {
    const float S1 = g.S[1], S2 = g.S[2], S3 = g.S[3], S4 = g.S[4];
    const float l1 = S2 - S1, l2 = S3 - S2, l3 = S4 - S3;
    float R;
    if (!g.thumb) R = g.r0 * (u < S3 ? Lerp(1.f, 0.875f, sstep(S1, S3, u)) : Lerp(0.875f, 0.85f, sstep(S3, S4, u)));
    else R = g.r0 * (u < S2 ? Lerp(1.24f, 1.f, sstep(S1, S2, u)) : (u < S3 ? Lerp(1.f, 0.95f, sstep(S2, S3, u)) : 0.94f));
    float knuckle = 0.07f * bump(u, S2, 0.0035f * s) + 0.05f * bump(u, S3, 0.003f * s);
    if (!g.thumb) knuckle += 0.14f * bump(u, S1 - 0.0015f * s, 0.0055f * s);
    float pads = 0.08f * bump(u, S2 + 0.5f * l2, 0.3f * l2) + 0.11f * bump(u, S3 + 0.52f * l3, 0.28f * l3);
    if (!g.thumb) pads += 0.07f * bump(u, S1 + 0.62f * l1, 0.25f * l1);
    float creases = 0.075f * bump(u, S2, 0.0022f * s) + 0.06f * bump(u, S3 - 0.0008f * s, 0.002f * s);
    a = R * 1.05f * (1.f + 0.035f * bump(u, S2, 0.004f * s) + 0.03f * bump(u, S3, 0.003f * s));
    bd = R * 0.86f * (1.f + knuckle);
    bp = R * 0.95f * (1.f + pads - creases);
    (void)l1;
}

static float digitCapLen(const Digit& g, float s) {
    float a, bd, bp;
    digitSection(g, g.S[4], s, a, bd, bp);
    return 1.1f * a;
}

// Surface point of a digit at arc length u and section angle th (0 = dorsal), tip rounding included; `n` receives the
// section normal (for offsets), `cap` whether the point lies on the rounded tip.
static vec3 digitPoint(const Digit& g, float u, float th, float s, vec3* nOut = nullptr, bool round = true) {
    vec3 p, t;
    digitAxis(g, u, p, t);
    vec3 dor = normalize(g.back - t * dot(g.back, t)), lat = cross(t, dor);
    float a, bd, bp;
    digitSection(g, Min(u, g.S[4]), s, a, bd, bp);
    const float capL = digitCapLen(g, s), c0 = g.S[4] - capL;
    if (round && u > c0) {
        // fingertip: the pad curls up to meet the nail line, which stays nearly straight until the free edge; seen
        // from above the tip is rounded
        float pr = Saturate((u - c0) / capL), pr2 = pr * pr;
        p += dor * (a * 0.2f * pr2);
        a *= sqrtf(Max(0.f, 1.f - pr2 * pr2 * sqrtf(pr)));
        bd *= sqrtf(Max(0.f, 1.f - pr2 * pr2));
        bp *= sqrtf(Max(0.f, 1.f - pr2));
    }
    float cd = cosf(th), sl = sinf(th);
    float b = cd > 0.f ? bd : bp;
    if (nOut) *nOut = normalize(dor * (cd / Max(b, 1e-5f)) + lat * (sl / Max(a, 1e-5f)));
    return p + dor * (cd * b) + lat * (sl * a);
}

static void buildFingers(BuildCtx& c, int side) {
    const BodyDims& D = *c.D;
    const CharacterDesc& d = *c.d;
    const vec3* J = D.J;
    const float s = D.s;
    MeshB& m = c.m;
    const bool right = side == 1;
    const int hb = right ? B_HAND_R : B_HAND_L;
    const vec3 pn = D.palmN[side];
    const int NF = 10;
    const float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
    const float fair = sstep(0.1f, 0.45f, lum);
    // knuckles: darker on dark skin, redder on fair skin; wrinkles deepen with age
    const vec3 knuckleTint = lerp(vec3(0.8f, 0.77f, 0.77f), vec3(1.f, 0.9f, 0.89f), fair);
    const float wrinkle = 0.7f + 0.8f * D.age;
    // nails: the plate shows the pink bed on fair skin, a lighter beige-pink than the skin on dark skin; some women
    // wear them longer and polished (one colour on all ten)
    Rng nr(hash32(d.seed * 0x3C6EF372u + 0x1Bu));
    const bool polish = D.fem > 0.5f && nr.chance(0.38f);
    const float nailExt = D.fem > 0.5f ? (polish ? nr.range(0.0012f, 0.0035f) : nr.range(0.f, 0.0012f)) : nr.range(0.f, 0.0006f);
    static const vec3 kPolish[6] = {vec3(0.42f, 0.02f, 0.03f), vec3(0.62f, 0.2f, 0.28f), vec3(0.58f, 0.4f, 0.34f),
                                    vec3(0.2f, 0.02f, 0.05f), vec3(0.04f, 0.035f, 0.04f), vec3(0.7f, 0.08f, 0.1f)};
    const vec3 polishCol = kPolish[nr.irange(0, 5)];
    vec3 nailCol = lerp(vmax(mulColor(c.skin, vec3(1.6f, 1.45f, 1.45f)), vec3(0.3f, 0.2f, 0.17f)), vec3(0.7f, 0.49f, 0.46f), fair);
    nailCol = lerp(nailCol, vec3(0.62f, 0.55f, 0.43f), 0.25f * sstep(0.5f, 1.f, D.age));
    const vec3 edgeCol = lerp(vec3(0.78f, 0.74f, 0.68f), nailCol, 0.25f);
    const vec3 bedCol = lerp(c.skin, lerp(c.skin * 1.3f, vec3(0.62f, 0.42f, 0.4f), fair), 0.55f);
    for (int f = 0; f < 5; f++) {
        Digit g;
        g.thumb = f == 4;
        const int b0 = phalanxBone(right, f, 0);
        if (!g.thumb) {
            g.P[1] = J[b0];
            g.P[2] = J[b0 + 1];
            g.P[3] = J[b0 + 2];
            g.P[4] = D.fingTip[side][f];
            vec3 fdir = normalize(cross(pn, D.fingAx[side][f]));   // straight finger direction (flexion axis x palm)
            g.P[0] = g.P[1] - fdir * (0.013f * s);
            g.bone[0] = hb;
            g.bone[1] = b0;
            g.bone[2] = b0 + 1;
            g.bone[3] = b0 + 2;
            g.back = -pn;
        } else {
            const int tb = right ? B_THUMB_R : B_THUMB_L;
            g.P[1] = J[tb];
            g.P[2] = J[b0];
            g.P[3] = J[b0 + 1];
            g.P[4] = D.fingTip[side][4];
            g.P[0] = g.P[1] - D.thumbDir[side] * (0.006f * s);
            g.bone[0] = hb;
            g.bone[1] = tb;
            g.bone[2] = b0;
            g.bone[3] = b0 + 1;
            g.back = -thumbPadDir(pn);
        }
        g.S[0] = 0.f;
        for (int i = 1; i < 5; i++) g.S[i] = g.S[i - 1] + length(g.P[i] - g.P[i - 1]);
        g.r0 = D.fingR[side][f];
        const float S1 = g.S[1], S2 = g.S[2], S3 = g.S[3], S4 = g.S[4];
        const float l1 = S2 - S1, l2 = S3 - S2, l3 = S4 - S3;
        const float capL = digitCapLen(g, s), c0 = S4 - capL;
        std::vector<float> us;
        if (!g.thumb) {
            const float u[] = {0.f, 0.0065f * s, S1 - 0.0022f * s, S1 + 0.0028f * s, S1 + 0.009f * s, S1 + 0.5f * l1, S2 - 0.0038f * s, S2,
                               S2 + 0.0038f * s, S2 + 0.5f * l2, S3 - 0.0028f * s, S3 + 0.0028f * s, S3 + 0.42f * l3};
            us.assign(u, u + sizeof(u) / sizeof(u[0]));
        } else {
            const float u[] = {0.f, S1 + 0.3f * l1, S1 + 0.62f * l1, S2 - 0.0045f * s, S2, S2 + 0.0045f * s, S2 + 0.5f * l2, S3 - 0.003f * s,
                               S3 + 0.003f * s, S3 + 0.42f * l3};
            us.assign(u, u + sizeof(u) / sizeof(u[0]));
        }
        while (!us.empty() && us.back() > c0 - 0.002f * s) us.pop_back();
        us.push_back(c0);
        us.push_back(c0 + 0.5f * capL);
        us.push_back(c0 + 0.82f * capL);
        // crease channel phase: global along the digit (linear in u, so it interpolates exactly between rings); line
        // pairs straddle the middle joint (thumb: the end joint)
        const float crSp = 0.0022f * s, crRef = g.thumb ? S3 : S2;
        std::vector<u32> prev;
        bool flip = false;
        for (size_t r = 0; r < us.size(); r++) {
            const float u = us[r];
            vec3 ap, at;
            digitAxis(g, u, ap, at);
            vec3 dor = normalize(g.back - at * dot(g.back, at)), lat = cross(at, dor);
            std::vector<u32> ring(NF);
            for (int k = 0; k < NF; k++) {
                float th = kTwoPi * k / NF;
                float cd = cosf(th);
                vec3 p = digitPoint(g, u, th, s);
                // skin weights per phalanx
                float dn = 0.5f + 0.5f * cd;
                WAcc acc;
                if (!g.thumb) {
                    float w1 = sstep(S1 + Lerp(-0.006f, -0.001f, dn) * s, S1 + Lerp(0.005f, 0.009f, dn) * s, u);
                    float w2 = sstep(S2 - 0.0035f * s, S2 + 0.0035f * s, u), w3 = sstep(S3 - 0.003f * s, S3 + 0.003f * s, u);
                    acc.add(g.bone[0], 1.f - w1);
                    acc.add(g.bone[1], w1 * (1.f - w2));
                    acc.add(g.bone[2], w2 * (1.f - w3));
                    acc.add(g.bone[3], w3);
                } else {
                    float wm = Lerp(0.5f, 1.f, sstep(0.f, S1 + 0.5f * l1, u));
                    float w2 = sstep(S2 - 0.005f * s, S2 + 0.005f * s, u), w3 = sstep(S3 - 0.0035f * s, S3 + 0.0035f * s, u);
                    acc.add(g.bone[0], (1.f - wm) * (1.f - w2));
                    acc.add(g.bone[1], wm * (1.f - w2));
                    acc.add(g.bone[2], w2 * (1.f - w3));
                    acc.add(g.bone[3], w3);
                }
                BVert v = skinVert(c, p, g.thumb ? PART_THUMB : PART_FINGER, (u8)side, acc.finish(), u, th, vec2(0.f));
                v.pc = u / S4;
                v.t = at;
                v.axisPt = ap;
                // colour: lighter palmar side, tinted knuckles, the nail bed under the plate, pinker finger pads
                float palmar = sstep(-0.15f, -0.7f, cd), dors = sstep(0.3f, 0.9f, cd);
                vec3 col = lerp(c.skin, c.palmCol, 0.75f * palmar);
                float kn = g.thumb ? 0.7f * bump(u, S3, 0.004f * s) + 0.5f * bump(u, S2, 0.005f * s)
                                   : bump(u, S1 - 0.001f * s, 0.006f * s) + 0.8f * bump(u, S2, 0.0045f * s) + 0.55f * bump(u, S3, 0.0035f * s);
                col = mulColor(col, lerp(vec3(1.f), knuckleTint, Saturate(kn) * dors));
                if (u > S3 + 0.3f * l3) col = lerp(col, bedCol, dors * sstep(S3 + 0.3f * l3, S3 + 0.45f * l3, u));
                col = lerp(col, mulColor(col, vec3(1.05f, 0.94f, 0.94f)), palmar * sstep(S3, S3 + 0.3f * l3, u));
                v.col = col;
                if (cd < -0.35f) v.flags |= BuildCtx::F_PALM;
                // creases: palmar flexion lines at the joints (and the finger's base line), dorsal knuckle wrinkles
                auto win = [&](float x, float w) { return 1.f - sstep(0.f, w, fabsf(x)); };
                float depth;
                if (!g.thumb)
                    depth = palmar * (0.34f * win(u - S2, 0.0026f * s) + 0.28f * win(u - S3 + 0.0008f * s, 0.0022f * s) +
                                      0.24f * win(u - (S1 + 0.5f * l1), 0.003f * s)) +
                            dors * wrinkle * (0.2f * win(u - S2, 0.0045f * s) + 0.13f * win(u - S3, 0.0035f * s) + 0.1f * win(u - S1, 0.004f * s));
                else
                    depth = palmar * (0.34f * win(u - S3, 0.003f * s) + 0.25f * win(u - S2, 0.0045f * s)) +
                            dors * wrinkle * (0.18f * win(u - S3, 0.004f * s) + 0.12f * win(u - S2, 0.0045f * s));
                v.uv = vec2((u - crRef) / crSp, depth);
                ring[k] = m.add(v);
            }
            if (r == 0) flip = dot(cross(at, lat), dor) < 0.f;
            else
                for (int k = 0; k < NF; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % NF], prev[(k + 1) % NF]);
            prev = ring;
        }
        {
            // rounded tip: pole at the dorsal front where the nail line ends
            vec3 p = digitPoint(g, S4, 0.f, s);
            vec3 ap, at;
            digitAxis(g, S4, ap, at);
            WAcc acc;
            acc.add(g.bone[3], 1.f);
            BVert v = skinVert(c, p, g.thumb ? PART_THUMB : PART_FINGER, (u8)side, acc.finish(), S4, 0.f, vec2((S4 - crRef) / crSp, 0.f));
            v.pc = 1.f;
            v.t = at;
            v.axisPt = ap;
            v.col = lerp(c.skin, bedCol, 0.5f);
            u32 ti = m.add(v);
            for (int k = 0; k < NF; k++) m.triMirror(flip, prev[k], ti, prev[(k + 1) % NF]);
        }
        // nail plate: from the proximal fold to the free edge at the fingertip (longer nails run on past it), 0.3 mm
        // above the distal surface, its base and sides tucked under the skin folds; a thin rim shows its thickness
        {
            const float u0 = S4 - (g.thumb ? 0.62f : 0.6f) * l3, uE = c0 + 0.85f * capL;
            const int NU = 4, NV = 4;
            const float vRow[NV] = {0.f, 0.35f, 0.75f, 1.f};
            const float thMax = g.thumb ? 0.95f : 0.9f;
            const float ext = (g.thumb ? 0.6f : 1.f) * nailExt * s;
            u32 grid[NV][NU];
            vec3 rowDor[NV];
            for (int j = 0; j < NV; j++) {
                float uu = Lerp(u0, uE, vRow[j]);
                vec3 ap, at;
                digitAxis(g, uu, ap, at);
                rowDor[j] = normalize(g.back - at * dot(g.back, at));
                for (int i = 0; i < NU; i++) {
                    float x = -1.f + 2.f * (float)i / (NU - 1);
                    float th = x * thMax * (j == 0 ? 0.86f : 1.f);
                    float ua = uu + (j == 0 ? 0.0011f * s * x * x : 0.f);   // rounded proximal corners
                    vec3 n;
                    vec3 sp = digitPoint(g, ua, th, s, &n);
                    float x4 = x * x * x * x;
                    float h = (j == 0 ? -0.00025f : 0.0003f) * s * (1.f - 0.55f * x * x) - 0.0008f * s * x4;
                    vec3 pos = sp + n * h;
                    if (j == NV - 1 && ext > 0.f) {
                        // long nails: continue the plate's own curve past the tip
                        vec3 pv = m.v[grid[j - 1][i]].p;
                        pos += normalize(pos - pv) * ext;
                    }
                    WAcc acc;
                    acc.add(g.bone[3], 1.f);
                    BVert v = skinVert(c, pos, PART_ACC, (u8)side, acc.finish(), ua, th, vec2(0.f));
                    v.flags |= BuildCtx::F_NAIL;
                    v.t = at;
                    v.axisPt = ap;
                    vec3 col = nailCol;
                    if (j == 0) col = lerp(col, vec3(0.8f, 0.72f, 0.68f), g.thumb ? 0.4f : 0.22f);   // lunula
                    if (j == NV - 1) col = lerp(nailCol, edgeCol, 0.4f + 0.6f * sstep(0.f, 0.0018f * s, ext));
                    if (polish) col = polishCol;
                    v.col = col;
                    grid[j][i] = m.add(v);
                }
            }
            for (int j = 0; j + 1 < NV; j++)
                for (int i = 0; i + 1 < NU; i++) m.quad(grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]);
            // free-edge rim (the nail's thickness)
            u32 rim[NU];
            for (int i = 0; i < NU; i++) {
                BVert v = m.v[grid[NV - 1][i]];
                v.p = v.p - rowDor[NV - 1] * (0.00055f * s);
                v.col = polish ? polishCol * 0.8f : edgeCol * 0.85f;
                rim[i] = m.add(v);
            }
            for (int i = 0; i + 1 < NU; i++) m.quad(grid[NV - 1][i], grid[NV - 1][i + 1], rim[i + 1], rim[i]);
        }
    }
}
