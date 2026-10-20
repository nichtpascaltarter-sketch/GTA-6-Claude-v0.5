// Decal (pocket, seam, placket, band, stitching) riding on a garment: the same fit and folds as its base, `extraOff`
// further out.
static GarmentDef decalOf(const GarmentDef& base, CovFn cov, vec3 col, u8 mat, float extraOff, u32 parts, u32 matParam) {
    GarmentDef dg;
    dg.parts = parts;
    dg.cov = cov;
    dg.col = col;
    dg.mat = mat;
    dg.matParam = matParam;
    dg.thick = base.thick;
    auto be = base.extraFn;
    dg.extraFn = [be, extraOff](const BVert& v) { return (be ? be(v) : 0.f) + extraOff; };
    dg.smooth = Min(base.smooth, 1);
    dg.hem = false;
    dg.hides = false;
    dg.hangDrift = base.hangDrift;
    dg.hangTop = base.hangTop;
    dg.hangWeight = base.hangWeight;
    dg.hangFade0 = base.hangFade0;
    dg.hangFade1 = base.hangFade1;
    if (base.tubeR) {
        auto tb = base.tubeR;
        dg.tubeR = [tb, extraOff](const BVert& v) {
            float r = tb(v);
            return r > 0.f ? r + extraOff : 0.f;
        };
    }
    dg.foldFn = base.foldFn;
    dg.refineTol = base.refineTol;
    return dg;
}

// Point on a garment's torso shell at height z and angle th (fit and folds included) and its outward normal: buttons,
// badges and zips sit on the cloth where it actually hangs.
static vec3 garmentTorsoPoint(OutfitCtx& o, const GarmentDef& g, float z, float th, vec3& nOut) {
    vec3 p, n;
    torsoPoint(o.c, z, th, p, n);
    buildTorsoProfile(o);
    BVert v;
    v.p = v.bp = p;
    v.n = n;
    v.part = PART_TORSO;
    v.pa = z;
    v.pb = th < 0.f ? th + kTwoPi : th;
    v.axisPt = vec3(0.f, profAxisY(o, z), z);
    float off = g.thick + (g.extraFn ? g.extraFn(v) : 0.f);
    vec3 q = p + n * off;
    if (g.hangDrift >= 0.f) {
        std::vector<float> ext;
        hangTable(o, g, ext);
        q = fitPoint(o, g, ext, v, q);
    }
    if (g.foldFn) {
        float fo = 0.f, cr = 0.f;
        g.foldFn(v, fo, cr);
        q += n * fo;
    }
    nOut = n;
    return q;
}

// Height of the bottoms' waistband top for a desc (matches buildBottomGarments).
static float bottomTopZ(const Ref& R, const CharacterDesc& d) {
    switch (d.bottom) {
        case BOT_BAGGY: return R.zBeltLow - 0.02f * R.s;
        case BOT_SLACKS: case BOT_POLICE: case BOT_LEGGINGS: return R.zBeltHigh;
        default: return R.zBeltMid;
    }
}

// Point on a sleeve at `along` from the shoulder joint in direction `dir` (perpendicular to the arm is enough): the
// larger of the skin plus the cloth and the sleeve's tube radius.
static vec3 sleeveSurfacePoint(const BuildCtx& c, const GarmentDef& g, int sd, float along, vec3 dir, float extra) {
    const BodyDims& D = *c.D;
    vec3 ad = D.armDir[sd];
    vec3 ax = D.J[sd ? B_UPPERARM_R : B_UPPERARM_L] + ad * along;
    dir = normalize(dir - ad * dot(dir, ad));
    float t = c.sdf.castOut(ax, dir, sd ? MK_ARM_R : MK_ARM_L, 0.12f * D.s);
    BVert v;
    v.part = PART_ARM;
    v.side = (u8)sd;
    v.pa = along;
    float R = g.tubeR ? g.tubeR(v) : 0.f;
    return ax + dir * (Max(t + g.thick + extra, R + g.thick * 0.5f) + 0.0012f);
}

// A hood lying down on the upper back: a pillow of fabric over the shoulder blades (the same fit and folds as the
// hoodie) whose opening rolls round the back of the neck.
static void buildHood(OutfitCtx& o, const GarmentDef& g, vec3 col) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const float zTop = D.zNeckBack + 0.005f * s, zBot = D.zNeckBack - 0.17f * s, hw = 0.115f * s;
    GarmentDef h = decalOf(g, [=](const BVert& v) -> float {
        if (v.part != PART_TORSO || v.bp.y > 0.02f * s) return -1.f;
        float x = fabsf(v.bp.x) / hw;
        float bot = zBot + 0.03f * s * x * x;   // rounded bottom
        return Min(Min(hw - fabsf(v.bp.x), v.bp.z - bot), zTop - v.bp.z);
    }, darker(col, 0.96f), MAT_CLOTH, 0.f, 1u << PART_TORSO, g.matParam);
    auto be = h.extraFn;
    h.extraFn = [=](const BVert& v) {
        float x = Saturate(fabsf(v.bp.x) / hw);
        float up = sstep(zBot, zBot + 0.06f * s, v.bp.z) * (1.f - 0.5f * sstep(zTop - 0.05f * s, zTop, v.bp.z));
        return be(v) + 0.004f * s + 0.02f * s * (1.f - x * x) * up;
    };
    h.hem = true;
    h.facing = true;
    h.smooth = 2;
    emitGarment(o, h);
    // the rolled opening round the back of the neck
    std::vector<vec3> pts;
    std::vector<float> rad;
    std::vector<SkinW> sws;
    const std::vector<u32>& ring = c.torsoTop;
    int n = (int)ring.size();
    vec3 cen(0);
    for (u32 vi : ring) cen += c.m.v[vi].p;
    cen /= (float)n;
    for (int k = n / 4 - 1; k <= 3 * n / 4 + 1; k++) {
        const BVert& bv = c.m.v[ring[k % n]];
        vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
        float back = Max(0.f, -cosf(bv.pb));
        pts.push_back(bv.p + radial * (0.02f + 0.022f * back) * s + vec3(0, 0, (0.01f - 0.008f * back) * s));
        rad.push_back((0.014f + 0.016f * back) * s);
        WAcc acc;
        acc.add(B_CHEST, 0.7f);
        acc.add(B_NECK, 0.3f);
        sws.push_back(acc.finish());
    }
    addTube(o.out, pts, rad, 8, false, col, MAT_CLOTH, sws);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// Hoodie drawstrings: two cords from the neck opening hanging down the chest, with plastic aglets.
static void buildDrawstrings(OutfitCtx& o, const GarmentDef& g, vec3 col) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    Rng r(hash32(c.d->seed * 0x51ED27u + 9u));
    vec3 cordCol = r.chance(0.5f) ? darker(col, 0.85f) : vec3(0.85f, 0.84f, 0.8f);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        float len = r.range(0.14f, 0.21f) * s, z0 = D.zNeckFront - 0.004f * s;
        std::vector<vec3> pts;
        std::vector<float> rad;
        std::vector<SkinW> sws;
        const int N = 8;
        for (int k = 0; k <= N; k++) {
            float z = z0 - len * k / N;
            float x = sx * (0.032f + 0.004f * k / N) * s;
            vec3 pr, pn;
            torsoPoint(c, z, 0.f, pr, pn);
            float rr = Max(length(vec2(pr.x, pr.y - profAxisY(o, z))), 0.05f);
            vec3 n;
            vec3 p = garmentTorsoPoint(o, g, z, asinf(Clamp(x / rr, -0.9f, 0.9f)), n);
            pts.push_back(p + n * (0.0045f * s));
            rad.push_back(k >= N - 1 ? 0.0032f * s : 0.0022f * s);
            sws.push_back(torsoSkinWeights(D, p));
        }
        addTube(o.out, pts, rad, 5, false, cordCol, MAT_CLOTH, sws, vec3(0, 1, 0));
        // aglet: the last segment in hard plastic
        std::vector<vec3> ag = {pts[N - 1], pts[N]};
        std::vector<float> ar(2, 0.0033f * s);
        std::vector<SkinW> asw(2, sws[N]);
        addTube(o.out, ag, ar, 5, false, darker(cordCol, 0.6f), MAT_PLASTIC, asw, vec3(0, 1, 0));
    }
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

static void buildTopGarments(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const int top = d.top;
    vec3 col = d.topColor;
    Rng rng(hash32(d.seed * 7u + 3u));
    const u32 torsoArms = (1u << PART_TORSO) | (1u << PART_ARM) | (1u << PART_NECK);
    if (top == TOP_NONE || top == TOP_BIKINI || top == TOP_ONEPIECE || top == TOP_SUNDRESS) return;
    GarmentDef g;
    g.parts = torsoArms;
    g.col = col;
    float sleeve = 0.14f * s, hemZ = R.zCrotch + 0.075f * s, neckPc = 0.972f, vDip = 0.f, vW = 0.3f;
    float loose = 0.004f;
    // silhouette: how the body hangs from the chest (drift) and the sleeve tube radii below the deltoid / at the hem,
    // with an optional snug cuff (rib) over its last cuffLen
    float drift = 0.12f, rTop = D.rUpperArm * 1.18f + 0.005f * s, rEnd = D.rUpperArm * 1.28f + 0.004f * s, rCuff = 0.f, cuffLen = 0.f;
    float foldAmp = 0.8f, hang = 0.5f;
    bool tank = false, collar = false, buttons = false, tucked = false, rolled = false;
    int nButtons = 0;
    const bool explicitAcc = (d.extras & ACC_EXPLICIT) != 0;
    switch (top) {
        case TOP_TSHIRT: sleeve = 0.15f * s; break;
        case TOP_OVERSIZED:
            sleeve = 0.3f * s; hemZ = R.zCrotch - 0.02f * s; loose = 0.007f; g.smooth = 3; drift = 0.02f;
            rTop = D.rUpperArm * 1.45f + 0.01f * s; rEnd = D.rElbow * 1.75f + 0.01f * s; foldAmp = 1.2f; hang = 1.f;
            break;
        case TOP_TANK: tank = true; loose = 0.003f; hemZ = R.zCrotch + 0.07f * s; drift = 0.3f; foldAmp = 0.4f; hang = 0.f; break;
        case TOP_POLO:
            sleeve = 0.155f * s; collar = true; buttons = true; nButtons = 2; drift = 0.1f;
            rTop = D.rUpperArm * 1.14f + 0.004f * s; rEnd = D.rUpperArm * 1.16f + 0.004f * s; foldAmp = 0.75f; hang = 0.4f;
            break;
        case TOP_HAWAIIAN:
            sleeve = 0.17f * s; collar = true; buttons = true; nButtons = 5; loose = 0.005f; vDip = 0.07f; vW = 0.25f; drift = 0.04f;
            rTop = D.rUpperArm * 1.32f + 0.008f * s; rEnd = D.rUpperArm * 1.45f + 0.008f * s; foldAmp = 1.f; hang = 0.8f;
            break;
        case TOP_DRESS_SHIRT: {
            bool longS = rng.chance(0.5f);
            rolled = longS && (explicitAcc ? (d.extras & ACC_ROLLED_SLEEVES) != 0 : rng.chance(0.4f));
            sleeve = !longS ? R.upperArm + 0.02f * s : (rolled ? R.upperArm + rng.range(-0.02f, 0.03f) * s : R.armLen - 0.02f * s);
            collar = true; buttons = true; nButtons = 6; tucked = true; drift = 0.14f; foldAmp = 0.9f; hang = 0.f;
            rTop = D.rUpperArm + 0.013f * s;
            rEnd = longS && !rolled ? D.rWrist + 0.013f * s : D.rUpperArm * 1.08f + 0.01f * s;
            if (longS && !rolled) { rCuff = D.rWrist + 0.01f * s; cuffLen = 0.06f * s; }
            break;
        }
        case TOP_HOODIE:
            sleeve = R.armLen - 0.01f * s; hemZ = R.zCrotch + 0.03f * s; loose = 0.006f; g.smooth = 3; drift = 0.06f;
            rTop = D.rUpperArm + 0.02f * s; rEnd = D.rForearm + 0.014f * s; rCuff = D.rWrist + 0.005f * s; cuffLen = 0.05f * s; foldAmp = 1.1f; hang = 0.7f;
            break;
        case TOP_SUIT:
            sleeve = R.armLen - 0.03f * s; hemZ = R.zCrotch - 0.03f * s; loose = 0.005f; drift = 0.03f;
            rTop = D.rUpperArm + 0.017f * s; rEnd = D.rWrist + 0.017f * s; foldAmp = 0.7f; hang = 0.3f;
            break;
        case TOP_POLICE: case TOP_MEDIC:
            sleeve = 0.17f * s; collar = true; buttons = true; nButtons = top == TOP_POLICE ? 6 : 5; tucked = true; drift = 0.12f;
            rTop = D.rUpperArm * 1.18f + 0.005f * s; rEnd = D.rUpperArm * 1.25f + 0.005f * s; foldAmp = 0.8f; hang = 0.f;
            break;
        case TOP_HIVIS: sleeve = 0.15f * s; col = d.topColor; break;
        case TOP_BLOUSE:
            sleeve = rng.chance(0.5f) ? 0.12f * s : 0.f; vDip = 0.09f; vW = 0.3f; loose = 0.005f; hemZ = R.zCrotch + 0.08f * s; drift = 0.07f;
            rTop = D.rUpperArm * 1.25f + 0.006f * s; rEnd = D.rUpperArm * 1.35f + 0.006f * s; foldAmp = 0.9f; hang = 0.8f;
            break;
        case TOP_CROP: tank = true; hemZ = R.zWaist + 0.06f * s; loose = 0.003f; drift = -1.f; foldAmp = 0.3f; hang = 0.f; break;
        default: break;
    }
    // weave: woven shirts, jackets and uniforms; ribbed tanks; knit jersey for tees, polos, hoodies
    if (top == TOP_HAWAIIAN || top == TOP_DRESS_SHIRT || top == TOP_SUIT || top == TOP_POLICE || top == TOP_MEDIC || top == TOP_BLOUSE)
        g.matParam = 1;
    else if (top == TOP_TANK)
        g.matParam = 3;
    const float waistTop = bottomTopZ(R, d);
    if (tucked) hemZ = R.zHip + 0.02f * s;
    const float hangTop = D.J[B_CHEST].z + 0.04f * s;
    vec3 shirtCol = rng.chance(0.7f) ? vec3(0.85f, 0.85f, 0.83f) : srgbToLinear(vec3(0.7f, 0.8f, 0.95f));
    if (top == TOP_SUIT) {
        // shirt under the jacket (visible in the V) and tie; the jacket shell follows
        collar = true;
        buttons = true;
        nButtons = 2;
        GarmentDef sh;
        sh.parts = torsoArms;
        sh.matParam = 1;
        sh.col = shirtCol;
        sh.thick = 0.003f;
        sh.smooth = 1;
        float zh = R.zHip + 0.02f * s;
        sh.cov = [=, &R](const BVert& v) -> float {
            if (v.part == PART_TORSO) return covTorsoRange(R, v, zh, 0.975f, 0.f, 0.3f);
            return -1.f;
        };
        sh.extraFn = [](const BVert&) { return 0.001f; };
        sh.hides = true;
        emitGarment(o, sh);
        // tie
        vec3 tieCol = srgbToLinear(rng.pick(std::vector<vec3>{vec3(0.55f, 0.08f, 0.1f), vec3(0.1f, 0.15f, 0.4f), vec3(0.08f), vec3(0.3f, 0.3f, 0.35f)}));
        float zTieTop = D.zNeckFront - 0.005f * s, zTieBot = R.zWaist - 0.02f * s;
        GarmentDef tie;
        tie.parts = 1u << PART_TORSO;
        tie.col = tieCol;
        tie.matParam = 1;
        tie.thick = 0.006f;
        tie.hem = true;
        tie.hides = false;
        tie.smooth = 1;
        tie.cov = [=](const BVert& v) -> float {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            float t = Saturate((zTieTop - v.bp.z) / (zTieTop - zTieBot));
            float hw = Lerp(0.012f, 0.038f, t) * s;
            return Min(Min(hw - fabsf(v.bp.x), v.bp.z - zTieBot), zTieTop - v.bp.z);
        };
        emitGarment(o, tie);
    }
    // ---- base shell
    const float sl = sleeve, hz = hemZ, np = neckPc, vd = vDip, vw = vW;
    const bool tk = tank;
    g.cov = [=, &R](const BVert& v) -> float {
        if (v.part == PART_TORSO || v.part == PART_NECK) {
            if (v.part == PART_NECK) return -1.f;
            float cv = covTorsoRange(R, v, hz, np, vd, vw);
            if (tk) {
                // body up to a scoop (pc 0.9 front/back, 0.8 at the armholes) plus straps over the shoulders
                float side = fabsf(sinf(v.pb));
                float body = Min(cv, (0.8f + 0.09f * (1.f - side) - v.pc) * R.torsoLen);
                // straps run continuously over the shoulders (only the hem limits them, not the neckline)
                float strap = Min(covStraps(R, v, 0.105f * R.s, 0.026f * R.s, 1.05f), v.bp.z - hz);
                cv = Max(body, strap);
            }
            return cv;
        }
        if (v.part == PART_ARM) {
            // tank straps may cross the shoulder junction: evaluate them on the arm root as well (clean edges)
            if (tk) return v.pa < 0.08f * R.s ? Min(covStraps(R, v, 0.105f * R.s, 0.026f * R.s, 1.05f), v.bp.z - hz) : -1.f;
            return sl - v.pa;
        }
        return -1.f;
    };
    if (top == TOP_SUIT) g.cov = [=, &R](const BVert& v) -> float { return covSuitJacket(R, v, hz, sl); };
    g.thick = 0.0035f;
    const float ls = loose, zc = R.zCrotch, zw = R.zWaist;
    // an untucked top hangs over the waistband: clear the bottoms' shell (plus belt) where they overlap
    const float clearE = o.botTopZ > 0.f ? o.botTorsoOff + 0.004f - g.thick : 0.f;
    const float zbt = o.botTopZ;
    g.extraFn = [=](const BVert& v) -> float {
        float e = ls;
        if (v.part == PART_TORSO) {
            e = ls * (0.5f + 0.9f * sstep(zw + 0.1f, zc, v.bp.z));   // hangs looser at the hem
            if (clearE > 0.f) e = Max(e, clearE * sstep(zbt + 0.06f, zbt - 0.005f, v.bp.z));
        }
        return e;
    };
    // hang from the chest / bust / shoulder blades; a tucked shirt blouses down to its waistband and goes in under it
    if (drift >= 0.f) {
        g.hangDrift = drift;
        g.hangTop = hangTop;
        if (tucked) {
            g.hangFade1 = waistTop + 0.055f * s;
            g.hangFade0 = waistTop - 0.005f * s;
        }
    }
    if (!tank && sleeve > 0.02f * s) {
        const float rt = rTop, re = rEnd, rc = rCuff, cl = cuffLen;
        g.tubeR = [=](const BVert& v) -> float {
            if (v.part != PART_ARM || v.pa > sl + 0.01f * s) return 0.f;
            float a = v.pa;
            float Rr = Lerp(rt, re, lstep(0.08f * s, Max(sl, 0.1f * s), a));
            if (cl > 0.f) Rr = Lerp(Rr, rc, sstep(sl - cl, sl - cl + 0.012f * s, a));
            return Rr * sstep(0.035f * s, 0.1f * s, a);
        };
    }
    o.topTorsoOff = g.thick + ls * 1.4f;
    o.topSleeveR = rEnd;
    if (top == TOP_HAWAIIAN) {
        u32 sd = d.seed;
        g.colFn = [=](const BVert& v, vec3 base) { return floral(v, base, sd); };
    }
    {
        FoldSpec fs;
        fs.amp = foldAmp;
        fs.sleeveEnd = tank ? 0.f : sleeve;
        fs.waistZ = tucked ? waistTop : hemZ;
        fs.tucked = tucked;
        fs.hangTop = hangTop;
        fs.hang = hang;
        fs.seed = d.seed * 3u + 1u;
        g.foldFn = foldFnOf(makeFolds(c, fs), s);
        g.refineTol = 0.0008f;
    }
    g.smooth = Max(g.smooth, 2);
    emitGarment(o, g);
    // ---- details
    const float off = g.thick + loose;
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, u32 mp = 0xffffffffu) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, mp == 0xffffffffu ? g.matParam : mp));
    };
    if (collar) {
        // folded collar band around the neck base
        const std::vector<u32>& ring = c.torsoTop;
        int n = (int)ring.size();
        std::vector<u32> r0(n), r1(n), r2(n);
        vec3 cen(0);
        for (u32 vi : ring) cen += c.m.v[vi].p;
        cen /= (float)n;
        bool openFront = top != TOP_POLICE && top != TOP_MEDIC;
        vec3 ccol = top == TOP_SUIT ? shirtCol : col;
        MeshB cm;
        for (int k = 0; k < n; k++) {
            const BVert& bv = c.m.v[ring[k]];
            vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
            float front = Max(0.f, cosf(bv.pb));
            float h = (0.03f - 0.012f * front * (top == TOP_HAWAIIAN ? 1.f : 0.4f)) * s;
            vec3 base = bv.p + radial * (off + 0.001f);
            vec3 upv(0, 0, 1);
            vec3 p0 = base - upv * 0.004f * s;
            vec3 p1 = base + upv * h + radial * 0.004f * s;
            vec3 p2 = base - upv * (0.006f * s) + radial * (0.008f * s + 0.005f * front * s);   // the fall rests on the shirt
            // collar points either side of the opening (shirts; the polo's are short and round)
            float thw = wrapAngle(bv.pb);
            float tip = bump(fabsf(thw), top == TOP_POLO ? 0.36f : 0.3f, 0.13f) * (top == TOP_POLO ? 0.55f : 1.f);
            p2 += (-upv * 0.024f * s + radial * 0.004f * s) * tip;
            BVert v = bv;
            v.part = PART_ACC;
            v.mat = MAT_CLOTH;
            v.matParam = top == TOP_POLO ? 3u : 1u;   // ribbed polo collar, woven shirt collars
            v.col = ccol;
            v.bp = bv.p;
            v.flags = 0;
            v.p = p0; v.n = radial; r0[k] = cm.add(v);
            v.p = p1; v.n = normalize(radial + upv); r1[k] = cm.add(v);
            v.p = p2; v.n = normalize(radial - upv * 0.3f); r2[k] = cm.add(v);
        }
        for (int k = 0; k < n; k++) {
            int k1 = (k + 1) % n;
            // skip the front opening segment
            if (openFront && (k == 0 || k1 == 0)) continue;
            vec3 rad = normalize(cm.v[r1[k]].n);
            auto q = [&](u32 a, u32 b, u32 c2, u32 d2, vec3 f) {
                vec3 nn = cross(cm.v[b].p - cm.v[a].p, cm.v[d2].p - cm.v[a].p);
                if (dot(nn, f) >= 0.f) cm.quad(a, b, c2, d2);
                else cm.quad(a, d2, c2, b);
            };
            q(r0[k], r0[k1], r1[k1], r1[k], -rad);          // stand (inner face, visible at the opening)
            q(r1[k], r1[k1], r2[k1], r2[k], rad);           // fall (outer face)
        }
        cm.computeNormals(0, cm.idx.size());
        o.out.append(cm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    if (buttons) {
        MeshB bm;
        float zTop = D.zNeckFront - 0.03f * s;
        float zBot = top == TOP_POLO ? zTop - 0.08f * s : hemZ + 0.05f * s;
        if (tucked) zBot = Max(zBot, waistTop + 0.03f * s);
        if (top == TOP_SUIT) {
            zTop = R.zChest - 0.08f * s;
            zBot = zTop - 0.085f * s;
        }
        int nb = nButtons;
        for (int i = 0; i < nb; i++) {
            float z = Lerp(zTop, zBot, nb > 1 ? (float)i / (nb - 1) : 0.f);
            vec3 n;
            vec3 p = garmentTorsoPoint(o, g, z, 0.f, n);
            SkinW sw = torsoSkinWeights(D, p);
            vec3 bc = top == TOP_POLICE ? vec3(0.75f, 0.6f, 0.25f) : vec3(0.85f, 0.83f, 0.78f);
            addDisc(bm, p + n * 0.0008f, n, 0.0048f * s, 6, 0.0015f, bc, top == TOP_POLICE ? MAT_CHROME : MAT_METAL_PAINTED, sw);
        }
        bm.computeNormals(0, 0);
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    CovFn baseCov = g.cov;
    if (!tank && top != TOP_SUIT) {
        // seams: side seams from the armpit to the hem, shoulder seams over the top of the shoulders, and the sleeve
        // seam round the arm root
        const float zAp = D.zArmpit, zNk = D.zNeckFront, shW = D.shoulderHalfW;
        vec3 seamCol = darker(col, 0.82f);
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.z > zAp) return -1.f;
            float th = wrapAngle(v.pb);
            float dth = Min(fabsf(th - kHalfPi), fabsf(th + kHalfPi));
            return Min(0.0016f * s - dth * 0.13f * s, baseCov(v));
        }, seamCol, g.mat, 0.0007f, 1u << PART_TORSO);
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.z < zAp) return -1.f;
            // along the ridge of the shoulder (y ~ torso axis) from the neck to the acromion
            float ax = fabsf(v.bp.x);
            float t = Saturate((ax - 0.05f * s) / Max(shW - 0.05f * s, 0.01f));
            float yRidge = D.J[B_CHEST].y + 0.005f * s;
            float onTop = v.bp.z - Lerp(zNk + 0.01f * s, D.zAcromion - 0.005f * s, t);
            return Min(Min(0.0016f * s - fabsf(v.bp.y - yRidge), 0.03f * s + onTop), baseCov(v));
        }, seamCol, g.mat, 0.0007f, 1u << PART_TORSO);
        if (sleeve > 0.08f * s)
            decal([=](const BVert& v) {
                if (v.part != PART_ARM) return -1.f;
                return Min(0.0016f * s - fabsf(v.pa - 0.055f * s), baseCov(v));
            }, seamCol, g.mat, 0.0007f, 1u << PART_ARM);
    }
    if (top == TOP_TSHIRT || top == TOP_OVERSIZED || top == TOP_HIVIS || top == TOP_POLO || top == TOP_HOODIE) {
        // double-needle stitching above the sleeve hems and round the bottom hem (tees); rib bands on polo sleeves
        vec3 stitch = darker(col, 0.78f);
        if (sleeve > 0.08f * s && top != TOP_HOODIE) {
            if (top == TOP_POLO)
                decal([=](const BVert& v) { return v.part == PART_ARM ? Min(0.012f * s - fabsf(v.pa - (sl - 0.012f * s)), sl - v.pa) : -1.f; },
                      darker(col, 0.93f), MAT_CLOTH, 0.0011f, 1u << PART_ARM, 3u);
            else
                for (int k = 0; k < 2; k++) {
                    float at = sl - (0.017f + 0.0055f * k) * s;
                    decal([=](const BVert& v) { return v.part == PART_ARM ? 0.0007f * s - fabsf(v.pa - at) : -1.f; }, stitch, MAT_CLOTH, 0.0005f,
                          1u << PART_ARM);
                }
        }
        if (top != TOP_HOODIE && top != TOP_POLO)
            for (int k = 0; k < 2; k++) {
                float zs = hz + (0.018f + 0.0055f * k) * s;
                decal([=](const BVert& v) { return v.part == PART_TORSO ? 0.0007f * s - fabsf(v.bp.z - zs) : -1.f; }, stitch, MAT_CLOTH, 0.0005f,
                      1u << PART_TORSO);
            }
    }
    if (top == TOP_POLICE || top == TOP_MEDIC) {
        // chest pockets with flaps
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float cx = sx * 0.075f * s, zc0 = R.zChest + 0.005f * s;
            decal([=, &R](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
                return Min(0.045f * R.s - fabsf(v.bp.x - cx), 0.055f * R.s - fabsf(v.bp.z - zc0));
            }, darker(col, 0.92f), MAT_CLOTH, 0.0022f, 1u << PART_TORSO);
            float zf = zc0 + 0.045f * s;
            decal([=, &R](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
                return Min(0.047f * R.s - fabsf(v.bp.x - cx), 0.013f * R.s - fabsf(v.bp.z - zf));
            }, darker(col, 0.8f), MAT_CLOTH, 0.004f, 1u << PART_TORSO);
        }
        // shoulder patches on both sleeves
        vec3 patch = top == TOP_POLICE ? srgbToLinear(vec3(0.75f, 0.62f, 0.2f)) : srgbToLinear(vec3(0.9f, 0.9f, 0.92f));
        decal([=](const BVert& v) {
            if (v.part != PART_ARM) return -1.f;
            float dth = fabsf(wrapAngle(v.pb - kHalfPi));
            return Min(0.035f * s - fabsf(v.pa - 0.1f * s), (0.55f - dth) * 0.05f);
        }, patch, MAT_CLOTH, 0.0022f, 1u << PART_ARM);
        if (top == TOP_MEDIC) {
            // reflective bands around the sleeves and chest
            decal([=](const BVert& v) {
                if (v.part != PART_ARM) return -1.f;
                return 0.012f * s - fabsf(v.pa - (sl - 0.03f * s));
            }, vec3(0.8f, 0.8f, 0.78f), MAT_CHROME, 0.0016f, 1u << PART_ARM);
            decal([=, &R](const BVert& v) {
                if (v.part != PART_TORSO) return -1.f;
                return 0.012f * R.s - fabsf(v.bp.z - (R.zChest - 0.07f * R.s));
            }, vec3(0.8f, 0.8f, 0.78f), MAT_CHROME, 0.0016f, 1u << PART_TORSO);
        }
    }
    if (top == TOP_POLICE) {
        // badge (gold shield) on the left chest, name plate on the right
        vec3 n;
        vec3 p = garmentTorsoPoint(o, g, R.zChest + 0.07f * s, -0.42f, n);
        MeshB bm;
        addDisc(bm, p + n * 0.004f, n, 0.026f * s, 7, 0.003f, srgbToLinear(vec3(0.85f, 0.68f, 0.3f)), MAT_CHROME, torsoSkinWeights(D, p));
        p = garmentTorsoPoint(o, g, R.zChest + 0.065f * s, 0.42f, n);
        vec3 ax = normalize(cross(vec3(0, 0, 1), n)), ay(0, 0, 1);
        addBoxOriented(bm, p + n * 0.004f, ax, ay, n, vec3(0.03f, 0.007f, 0.002f) * s, vec3(0.8f, 0.8f, 0.82f), MAT_CHROME, torsoSkinWeights(D, p));
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    if (top == TOP_POLO || top == TOP_DRESS_SHIRT || top == TOP_HAWAIIAN) {
        // placket strip down the front
        float zb = top == TOP_POLO ? D.zNeckFront - 0.12f * s : hemZ;
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            return Min(0.011f * s - fabsf(v.bp.x), v.bp.z - zb);
        }, darker(col, 0.93f), MAT_CLOTH, 0.0012f, 1u << PART_TORSO);
    }
    if (top == TOP_DRESS_SHIRT || top == TOP_HAWAIIAN) {
        // chest pocket on the left (outline stitched in the shirt's colour), and the back yoke seam of a dress shirt
        float px = -0.07f * s, pz = R.zChest + 0.03f * s;
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            float dx = fabsf(v.bp.x - px), dz = v.bp.z - pz;
            return Min(0.05f * s - dx * 1.12f, Min(0.034f * s - dz, 0.038f * s + dz - Max(0.f, dx - 0.028f * s) * 0.8f));
        }, darker(col, 0.95f), MAT_CLOTH, 0.0014f, 1u << PART_TORSO);
        if (top == TOP_DRESS_SHIRT) {
            float zy = D.zArmpit + 0.035f * s;
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y > 0.f) return -1.f;
                return Min(0.0012f * s - fabsf(v.bp.z - zy), baseCov(v));
            }, darker(col, 0.84f), MAT_CLOTH, 0.0006f, 1u << PART_TORSO);
        }
    }
    if (top == TOP_DRESS_SHIRT && sleeve > R.armLen - 0.05f * s) {
        // shirt cuffs: a stiff band over the last 6 cm with a button on the outside of the wrist
        decal([=](const BVert& v) { return v.part == PART_ARM ? Min(v.pa - (sl - 0.06f * s), sl - v.pa) : -1.f; }, darker(col, 0.97f), MAT_CLOTH, 0.0016f,
              1u << PART_ARM);
        MeshB bm;
        for (int sd = 0; sd < 2; sd++) {
            vec3 dir = normalize(-D.palmN[sd] * 0.8f + vec3(0.f, -0.6f, 0.f));
            vec3 p = sleeveSurfacePoint(c, g, sd, sl - 0.03f * s, dir, 0.0016f + loose);
            addDisc(bm, p, dir, 0.0045f * s, 6, 0.0014f, vec3(0.85f, 0.83f, 0.78f), MAT_METAL_PAINTED,
                    skin2(sd ? B_FOREARM_R : B_FOREARM_L, sd ? B_HAND_R : B_HAND_L, 0.4f));
        }
        bm.computeNormals(0, 0);
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    if (rolled) {
        // sleeves rolled up to the elbow: a thick turned-back band, the lining side slightly darker
        decal([=](const BVert& v) { return v.part == PART_ARM ? Min(v.pa - (sl - 0.045f * s), sl + 0.001f - v.pa) : -1.f; }, darker(col, 0.9f), MAT_CLOTH,
              0.0055f * s, 1u << PART_ARM);
        decal([=](const BVert& v) { return v.part == PART_ARM ? 0.0012f * s - fabsf(v.pa - (sl - 0.022f * s)) : -1.f; }, darker(col, 0.72f), MAT_CLOTH,
              0.0062f * s, 1u << PART_ARM);
    }
    if (top == TOP_TSHIRT || top == TOP_OVERSIZED) {
        // ribbed neckline band
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO) return -1.f;
            return Min(0.012f * s - fabsf((neckPc - 0.012f - v.pc) * R.torsoLen), baseCov(v));
        }, darker(col, 0.88f), MAT_CLOTH, 0.0012f, 1u << PART_TORSO, 3u);
        if (rng.chance(0.45f)) {
            // chest print: a simple two-tone graphic block
            vec3 pc = rng.chance(0.5f) ? vec3(0.9f) - col * 0.6f : srgbToLinear(vec3(rng.f(), rng.f(), rng.f()));
            float zc0 = R.zChest + 0.02f * s;
            decal([=, &R](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
                return Min(0.07f * R.s - fabsf(v.bp.x), 0.05f * R.s - fabsf(v.bp.z - zc0));
            }, saturate(pc), MAT_CLOTH, 0.0008f, 1u << PART_TORSO);
        }
    }
    if (top == TOP_HOODIE) {
        // kangaroo pocket, rib cuffs and hem band, a draped hood on the upper back, drawstrings with aglets
        float zp = R.zWaist - 0.02f * s;
        decal([=, &R](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            return Min(0.1f * R.s - fabsf(v.bp.x) - 0.3f * Max(0.f, v.bp.z - zp), 0.06f * R.s - fabsf(v.bp.z - zp));
        }, darker(col, 0.9f), MAT_CLOTH, 0.004f, 1u << PART_TORSO);
        decal([=](const BVert& v) { return v.part == PART_ARM ? Min(v.pa - (sl - 0.05f * s), sl - v.pa) : -1.f; }, darker(col, 0.92f), MAT_CLOTH, 0.0014f,
              1u << PART_ARM, 3u);
        decal([=](const BVert& v) { return v.part == PART_TORSO ? Min(v.bp.z - hz, hz + 0.055f * s - v.bp.z) : -1.f; }, darker(col, 0.92f), MAT_CLOTH,
              0.0014f, 1u << PART_TORSO, 3u);
        buildHood(o, g, col);
        buildDrawstrings(o, g, col);
    }
    if (top == TOP_SUIT) {
        // lapels, a breast pocket welt, flap pockets and buttons on the cuffs
        float zV = R.zChest - 0.07f * s;
        decal([=, &R](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            float h = Saturate((v.bp.z - zV) / (D.zNeckFront - zV));
            float vx = Lerp(0.0f, 0.075f, h) * R.s;
            float lw = Lerp(0.02f, 0.05f, h) * R.s;
            float dist = fabsf(v.bp.x) - vx;
            return Min(Min(dist, lw - dist), v.bp.z - zV + 0.01f * R.s);
        }, darker(col, 0.85f), MAT_CLOTH, 0.004f, 1u << PART_TORSO);
        float wx = -0.085f * s, wz = R.zChest + 0.035f * s;
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            return Min(0.05f * s - fabsf(v.bp.x - wx) * 1.1f, 0.006f * s - fabsf(v.bp.z - wz - (v.bp.x - wx) * 0.12f));
        }, darker(col, 0.8f), MAT_CLOTH, 0.0012f, 1u << PART_TORSO);
        for (int sd = 0; sd < 2; sd++) {
            float fx = (sd ? 1.f : -1.f) * 0.1f * s, fz = R.zHip + 0.02f * s;
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < -0.01f) return -1.f;
                return Min(0.058f * s - fabsf(v.bp.x - fx), 0.016f * s - fabsf(v.bp.z - fz));
            }, darker(col, 0.9f), MAT_CLOTH, 0.0022f, 1u << PART_TORSO);
        }
        MeshB bm;
        for (int sd = 0; sd < 2; sd++) {
            vec3 dir = normalize(-D.palmN[sd] * 0.55f + vec3(0.f, -0.85f, 0.f));
            for (int k = 0; k < 3; k++) {
                vec3 p = sleeveSurfacePoint(c, g, sd, sl - (0.03f + 0.017f * k) * s, dir, loose + 0.001f);
                addDisc(bm, p, dir, 0.0042f * s, 6, 0.0014f, darker(col, 0.5f) + vec3(0.02f), MAT_PLASTIC,
                        skin2(sd ? B_FOREARM_R : B_FOREARM_L, sd ? B_HAND_R : B_HAND_L, 0.3f));
            }
        }
        bm.computeNormals(0, 0);
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
}
