// ------------------------------------------------------------------------------------------------
// Outer layer (CharacterDesc::outer): an overshirt, zip hoodie, cardigan, light jacket, vest or blazer worn open over
// the top. The shell hangs straight from the shoulder blades and the chest, clears the top and its sleeves, and leaves a
// front opening that widens towards the hem (a V to the waist button on cardigans and blazers). The open edges get
// facings (the inside of the panels shows), plackets or zip tapes; the inner top is hidden wherever the layer covers it.

// Whether an outer layer goes with the top (suits, uniforms, hi-vis, swimwear and bare chests take none).
bool outerFits(const CharacterDesc& d) {
    if (d.outer < 0 || d.outer >= OUT_COUNT) return false;
    switch (d.top) {
        case TOP_TSHIRT: case TOP_TANK: case TOP_POLO: case TOP_DRESS_SHIRT: case TOP_BLOUSE: case TOP_CROP: case TOP_OVERSIZED: return true;
        case TOP_HOODIE: return d.outer == OUT_VEST || d.outer == OUT_JACKET;
        case TOP_SUNDRESS: return d.outer == OUT_JACKET || d.outer == OUT_CARDIGAN;
        default: return false;
    }
}

static void buildOuterLayer(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    if (!outerFits(d)) return;
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const int kind = d.outer;
    Rng rng(hash32(d.seed * 0x3A8F05C5u + 0x1Du));
    vec3 col = d.outerColor;
    const u32 torsoArms = (1u << PART_TORSO) | (1u << PART_ARM) | (1u << PART_NECK);
    GarmentDef g;
    g.parts = torsoArms;
    g.col = col;
    g.thick = 0.0045f;
    g.matParam = 1;
    // hem, front opening half-widths (at the neckline, at the chest, at the hem), V point and whether it is closed below
    float hemZ = R.zCrotch + 0.02f * s, gapNeck = 0.04f * s, gapChest = 0.05f * s, gapHem = 0.09f * s, zV = -1.f;
    bool closedBelowV = false, sleeves = true, rolled = false, zip = false, rib = false, lapels = false, puffer = false;
    int nButtons = 0;
    float loose = 0.006f;
    switch (kind) {
        case OUT_OVERSHIRT:
            rolled = (d.extras & ACC_ROLLED_SLEEVES) != 0;
            gapNeck = 0.035f * s; gapChest = rng.range(0.045f, 0.07f) * s; gapHem = gapChest + rng.range(0.03f, 0.06f) * s; nButtons = 6;
            break;
        case OUT_ZIPHOODIE:
            g.matParam = 0; hemZ = R.zCrotch + 0.03f * s; zip = true; rib = true; loose = 0.008f;
            gapNeck = 0.035f * s; gapChest = rng.range(0.04f, 0.06f) * s; gapHem = gapChest + rng.range(0.02f, 0.05f) * s;
            break;
        case OUT_CARDIGAN: {
            g.matParam = 3; hemZ = R.zCrotch + rng.range(-0.02f, 0.03f) * s; rib = true; nButtons = 5;
            bool buttoned = d.age > 0.55f ? rng.chance(0.6f) : rng.chance(0.25f);
            gapNeck = 0.05f * s; zV = R.zChest - 0.1f * s; gapChest = buttoned ? 0.006f * s : 0.04f * s; gapHem = buttoned ? 0.01f * s : 0.07f * s;
            closedBelowV = buttoned;
            break;
        }
        case OUT_JACKET:
            hemZ = R.zHip + 0.02f * s; gapNeck = 0.045f * s; gapChest = 0.06f * s; gapHem = 0.1f * s; nButtons = 5;
            if (rng.chance(0.4f)) { zip = true; rib = true; nButtons = 0; }   // bomber / windbreaker
            else g.mat = MAT_DENIM;                                            // denim jacket
            g.thick = 0.005f;
            break;
        case OUT_VEST:
            sleeves = false;
            if (rng.chance(0.55f)) {   // quilted puffer gilet, zipped half-way or open
                puffer = true; zip = true; hemZ = R.zHip + 0.0f * s; gapNeck = 0.03f * s; gapChest = rng.chance(0.5f) ? 0.004f * s : 0.05f * s;
                gapHem = gapChest + 0.02f * s; g.thick = 0.009f;
            } else {                   // waistcoat, buttoned below a V
                hemZ = R.zHip + 0.03f * s; gapNeck = 0.05f * s; zV = R.zChest - 0.07f * s; gapChest = 0.004f * s; gapHem = 0.004f * s;
                closedBelowV = true; nButtons = 5; loose = 0.003f;
            }
            break;
        case OUT_BLAZER: {
            hemZ = R.zCrotch - 0.04f * s; lapels = true; loose = 0.005f;
            bool buttoned = rng.chance(0.35f);
            gapNeck = 0.06f * s; zV = R.zWaist + 0.02f * s; gapChest = buttoned ? 0.004f * s : 0.07f * s; gapHem = buttoned ? 0.03f * s : 0.1f * s;
            closedBelowV = buttoned; nButtons = buttoned ? 1 : 0;
            break;
        }
        default: break;
    }
    if (kind == OUT_OVERSHIRT && rng.chance(0.25f)) g.mat = MAT_DENIM;   // chambray / denim shirt
    const float sl = !sleeves ? 0.f : (rolled ? R.upperArm + rng.range(-0.01f, 0.04f) * s : R.armLen - (rib ? 0.01f : 0.025f) * s);
    const float zNk = D.zNeckFront, zChestL = R.zChest, hz = hemZ;
    const float gN = gapNeck, gC = gapChest, gH = gapHem, zv = zV;
    // half-width of the front opening at height z
    auto gapAt = [=](float z) -> float {
        if (zv > 0.f) {
            // a V from the neckline down to zV, then closed (buttoned) or opening out towards the hem
            if (z >= zv) return Lerp(gC, gN, Saturate((z - zv) / Max(zNk - zv, 0.01f)));
            return Lerp(gC, gH, Saturate((zv - z) / Max(zv - hz, 0.01f)));
        }
        if (z >= zChestL) return Lerp(gC, gN, Saturate((z - zChestL) / Max(zNk - zChestL, 0.01f)));
        return Lerp(gC, gH, Saturate((zChestL - z) / Max(zChestL - hz, 0.01f)));
    };
    auto frontCut = [=](const BVert& v) -> float {
        // + outside the opening: distance from the opening's edge, measured across the front
        if (v.bp.y < 0.f) return 1.f;
        return fabsf(v.bp.x) - gapAt(v.bp.z);
    };
    const bool noSleeve = !sleeves;
    const float zArmhole = D.zArmpit - 0.03f * s;
    g.cov = [=, &R](const BVert& v) -> float {
        if (v.part == PART_TORSO) {
            float cv = covTorsoRange(R, v, hz, 0.975f, 0.f, 0.3f);
            if (noSleeve) cv = Min(cv, Max(0.125f * R.s - fabsf(v.bp.x), zArmhole - v.bp.z));   // a vest's armholes
            return Min(cv, frontCut(v));
        }
        if (v.part == PART_ARM) return noSleeve ? -1.f : sl - v.pa;
        return -1.f;
    };
    // clear the top (and a tucked top's waistband) with the shell, the sleeves with the tubes
    const float clearT = Max(o.topTorsoOff, o.botTorsoOff) + 0.005f + (puffer ? 0.004f : 0.f);
    const float ls = loose, zc = R.zCrotch, zw = R.zWaist;
    g.extraFn = [=](const BVert& v) -> float {
        float e = ls;
        if (v.part == PART_TORSO) e = Max(clearT, ls * (0.6f + 0.8f * sstep(zw + 0.1f, zc, v.bp.z)));
        else if (v.part == PART_ARM) e = ls + 0.004f;
        return e;
    };
    g.hangDrift = kind == OUT_CARDIGAN ? 0.05f : 0.015f;
    g.hangTop = D.J[B_CHEST].z + 0.05f * s;
    if (sleeves) {
        const float rt = Max(o.topSleeveR + 0.009f * s, D.rUpperArm + 0.021f * s), re = rolled ? rt * 0.95f : D.rWrist + (rib ? 0.007f : 0.016f) * s;
        const float rc = rib ? D.rWrist + 0.005f * s : 0.f, cl = rib ? 0.05f * s : 0.f;
        g.tubeR = [=](const BVert& v) -> float {
            if (v.part != PART_ARM || v.pa > sl + 0.01f * s) return 0.f;
            float a = v.pa;
            float Rr = Lerp(rt, re, lstep(0.08f * s, Max(sl, 0.1f * s), a));
            if (cl > 0.f) Rr = Lerp(Rr, rc, sstep(sl - cl, sl - cl + 0.012f * s, a));
            return Rr * sstep(0.03f * s, 0.09f * s, a);
        };
    }
    {
        FoldSpec fs;
        fs.amp = puffer ? 0.3f : (kind == OUT_BLAZER ? 0.7f : 1.f);
        fs.sleeveEnd = sl;
        fs.waistZ = hemZ;
        fs.hangTop = g.hangTop;
        fs.hang = 0.3f;
        fs.openFront = gapChest > 0.02f * s;
        fs.seed = d.seed * 11u + 7u;
        g.foldFn = foldFnOf(makeFolds(c, fs), s);
        g.refineTol = 0.0008f;
    }
    if (puffer) {
        // quilted channels: the fill puffs out between horizontal stitch lines ~7 cm apart
        auto be = g.extraFn;
        const float z0 = hemZ, pitch = 0.07f * s;
        g.extraFn = [=](const BVert& v) {
            float e = be(v);
            if (v.part == PART_TORSO) {
                float ph = (v.bp.z - z0) / pitch;
                float f = ph - floorf(ph);
                e += 0.007f * s * sqrtf(Max(0.f, sinf(kPi * f)));
            }
            return e;
        };
    }
    g.smooth = 2;
    emitGarment(o, g);
    o.outerTorsoOff = g.thick + clearT;
    // ---- details
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, u32 mp) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, mp));
    };
    auto line = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, g.matParam, 0));
    };
    CovFn baseCov = g.cov;
    const float edgeW = zip ? 0.009f * s : 0.016f * s;
    if (zip) {
        // zip tapes along both open edges: a dark tape with the metal teeth line
        decal([=](const BVert& v) { return v.part == PART_TORSO ? Min(baseCov(v), edgeW - frontCut(v)) : -1.f; }, darker(col, 0.55f), MAT_CLOTH, 0.0012f,
              1u << PART_TORSO, 1u);
        line([=](const BVert& v) { return v.part == PART_TORSO ? Min(baseCov(v), 0.0022f * s - fabsf(frontCut(v) - 0.002f * s)) : -1.f; },
             vec3(0.55f, 0.55f, 0.56f), MAT_CHROME, 0.0018f, 1u << PART_TORSO);
    } else if (!lapels && kind != OUT_VEST) {
        // plackets / front bands along the open edges
        decal([=](const BVert& v) { return v.part == PART_TORSO ? Min(baseCov(v), edgeW - frontCut(v)) : -1.f; }, darker(col, 0.93f), g.mat, 0.0013f,
              1u << PART_TORSO, g.matParam);
    }
    if (lapels) {
        // notched lapels folded back along the V, wider at the top
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f || v.bp.z < zv - 0.01f * s) return -1.f;
            float h = Saturate((v.bp.z - zv) / Max(zNk - zv, 0.01f));
            return Min(baseCov(v), Lerp(0.02f, 0.06f, h) * s - frontCut(v));
        }, darker(col, 0.88f), g.mat, 0.004f, 1u << PART_TORSO, g.matParam);
    }
    if (rib) {
        // rib hem band and cuffs
        decal([=](const BVert& v) { return v.part == PART_TORSO ? Min(baseCov(v), hz + 0.05f * s - v.bp.z) : -1.f; }, darker(col, 0.92f), MAT_CLOTH, 0.0014f,
              1u << PART_TORSO, 3u);
        if (sleeves && !rolled)
            decal([=](const BVert& v) { return v.part == PART_ARM ? Min(v.pa - (sl - 0.05f * s), sl - v.pa) : -1.f; }, darker(col, 0.92f), MAT_CLOTH,
                  0.0014f, 1u << PART_ARM, 3u);
    }
    if (rolled) {
        decal([=](const BVert& v) { return v.part == PART_ARM ? Min(v.pa - (sl - 0.045f * s), sl + 0.001f - v.pa) : -1.f; }, darker(col, 0.88f), g.mat,
              0.0055f * s, 1u << PART_ARM, g.matParam);
    }
    if (puffer) {
        // the quilting stitch lines
        for (float z = hemZ + 0.07f * s; z < D.zArmpit; z += 0.07f * s) {
            float zz = z;
            line([=](const BVert& v) { return v.part == PART_TORSO ? Min(baseCov(v), 0.0012f * s - fabsf(v.bp.z - zz)) : -1.f; }, darker(col, 0.7f), MAT_CLOTH,
                 0.0004f, 1u << PART_TORSO);
        }
    }
    // pockets: chest pockets with flaps (overshirt, denim jacket), hip pockets (cardigan, zip hoodie: slanted welts),
    // flap pockets (blazer, bomber)
    if (kind == OUT_OVERSHIRT || (kind == OUT_JACKET && !zip)) {
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f, cx = sx * 0.075f * s, cz = R.zChest + 0.01f * s;
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
                return Min(Min(0.042f * s - fabsf(v.bp.x - cx), 0.05f * s - fabsf(v.bp.z - cz)), frontCut(v) - 0.006f * s);
            }, darker(col, 0.95f), g.mat, 0.0016f, 1u << PART_TORSO, g.matParam);
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
                return Min(Min(0.044f * s - fabsf(v.bp.x - cx), 0.012f * s - fabsf(v.bp.z - cz - 0.045f * s)), frontCut(v) - 0.004f * s);
            }, darker(col, 0.85f), g.mat, 0.0032f, 1u << PART_TORSO, g.matParam);
        }
    } else if (kind == OUT_CARDIGAN || kind == OUT_ZIPHOODIE || kind == OUT_BLAZER || (kind == OUT_JACKET && zip)) {
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f, cx = sx * 0.1f * s, cz = Lerp(hz, R.zWaist, 0.45f);
            bool welt = kind == OUT_ZIPHOODIE || kind == OUT_JACKET;
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO || v.bp.y < -0.01f * s) return -1.f;
                if (welt) return Min(0.008f * s - fabsf((v.bp.x - cx) * 0.9f - (v.bp.z - cz) * sx * 0.45f), 0.06f * s - length(vec2(v.bp.x - cx, v.bp.z - cz)));
                return Min(Min(0.055f * s - fabsf(v.bp.x - cx), (kind == OUT_BLAZER ? 0.015f : 0.05f) * s - fabsf(v.bp.z - cz)), frontCut(v) - 0.01f * s);
            }, darker(col, welt ? 0.75f : 0.92f), g.mat, 0.0022f, 1u << PART_TORSO, g.matParam);
        }
    }
    // buttons down the open edge(s): on the wearer's right edge (they are sewn to the placket), or at the V point
    if (nButtons > 0) {
        MeshB bm;
        float zTop = zV > 0.f ? zV : D.zNeckFront - 0.04f * s, zBot = hemZ + 0.05f * s;
        for (int i = 0; i < nButtons; i++) {
            float z = nButtons > 1 ? Lerp(zTop, zBot, (float)i / (nButtons - 1)) : zTop;
            float xg = (closedBelowV && z < zTop + 0.001f) ? 0.f : gapAt(z) + edgeW * 0.5f;
            vec3 pr, pn;
            torsoPoint(c, z, 0.f, pr, pn);
            float rr = Max(length(vec2(pr.x, pr.y - profAxisY(o, z))), 0.05f);
            vec3 n;
            vec3 p = garmentTorsoPoint(o, g, z, asinf(Clamp((d.gender == FEMALE ? -xg : xg) / rr, -0.9f, 0.9f)), n);
            vec3 bc = g.mat == MAT_DENIM ? srgbToLinear(vec3(0.7f, 0.5f, 0.3f)) : darker(col, 0.5f) + vec3(0.03f);
            addDisc(bm, p + n * 0.0016f, n, (kind == OUT_BLAZER ? 0.0075f : 0.0055f) * s, 6, 0.0015f, bc, g.mat == MAT_DENIM ? MAT_CHROME : MAT_PLASTIC,
                    torsoSkinWeights(D, p));
        }
        bm.computeNormals(0, 0);
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    // collar / hood
    const float offC = clearT + g.thick;
    if (kind == OUT_OVERSHIRT || (kind == OUT_JACKET && !zip))
        addCollar(o, offC, darker(col, 0.97f), g.matParam, 0.8f, 0.42f, 1.1f, true, 0.034f * s, 0.006f * s);
    else if (kind == OUT_JACKET && zip)
        addCollar(o, offC, darker(col, 0.9f), 3u, 0.2f, 0.4f, 0.f, true, 0.028f * s);
    else if (kind == OUT_ZIPHOODIE)
        buildHood(o, g, col);
    if (sleeves && kind != OUT_VEST) {
        // seam round the sleeve root and down the outside of the sleeve
        line([=](const BVert& v) { return v.part == PART_ARM ? Min(0.0016f * s - fabsf(v.pa - 0.06f * s), sl - v.pa) : -1.f; }, darker(col, 0.8f), g.mat, 0.0007f,
             1u << PART_ARM);
    }
}

