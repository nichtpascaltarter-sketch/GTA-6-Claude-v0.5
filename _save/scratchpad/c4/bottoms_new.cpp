    if (bot == BOT_BIKINI) return;
    // ---- trousers / shorts shell
    const bool cuffedHem = (bot == BOT_JEANS || bot == BOT_WORK || bot == BOT_SLACKS) &&
                           ((d.extras & ACC_EXPLICIT) ? (d.extras & ACC_CUFFED_HEM) != 0 : false);
    if (cuffedHem) zCuff += 0.03f * s;
    float zt = zTop, zcuf = zCuff, ls = loose, fl = flare;
    g.cov = [=](const BVert& v) -> float {
        if (v.part == PART_TORSO) return zt - v.bp.z;
        if (v.part == PART_LEG) return Min(v.bp.z - zcuf, zt - v.bp.z);
        return -1.f;
    };
    float zc = R.zCrotch;
    // over a tucked shirt the waist must clear the shirt shell
    float torsoE = Max(ls * 0.7f, o.topTorsoOff > 0.f ? o.topTorsoOff + 0.004f - g.thick : 0.f);
    g.extraFn = [=](const BVert& v) -> float {
        if (v.part == PART_LEG) {
            float down = Saturate((zc - v.bp.z) / Max(zc - zcuf, 0.05f));
            return Max(ls + fl * down, v.bp.z > zc ? torsoE : 0.f);
        }
        return torsoE;
    };
    // silhouette: trouser legs are tubes from the thigh to the hem (the cut sets the knee and hem widths) instead of
    // following the knee and the calf; the seat hangs from the glutes at the back
    float rKneeT = 0.f, rHemT = 0.f, foldAmp = 0.9f;
    {
        const float rk = D.rKnee, rt = D.rThigh;
        int cut = 1;   // jeans: 0 skinny, 1 slim, 2 straight, 3 relaxed
        if (bot == BOT_JEANS) {
            float u = rng.f();
            cut = D.fem > 0.5f ? (u < 0.45f ? 0 : (u < 0.8f ? 1 : 2)) : (u < 0.35f ? 1 : (u < 0.85f ? 2 : 3));
        }
        switch (bot) {
            case BOT_JEANS: {
                const float kk[4] = {1.0f, 1.1f, 1.16f, 1.28f}, hh[4] = {0.f, 1.0f, 1.08f, 1.2f};
                rKneeT = rk * kk[cut] + 0.003f * s;
                rHemT = cut == 0 ? 0.f : rk * hh[cut] + 0.003f * s;
                foldAmp = cut == 0 ? 0.55f : 0.9f;
                break;
            }
            case BOT_BAGGY: rKneeT = rk * 1.55f + 0.01f * s; rHemT = rk * 1.5f + 0.008f * s; foldAmp = 1.3f; break;
            case BOT_SLACKS: rKneeT = rk * 1.2f + 0.006f * s; rHemT = rk * 1.14f + 0.005f * s; foldAmp = 0.8f; break;
            case BOT_POLICE: rKneeT = rk * 1.22f + 0.006f * s; rHemT = rk * 1.16f + 0.006f * s; foldAmp = 0.85f; break;
            case BOT_WORK: rKneeT = rk * 1.3f + 0.008f * s; rHemT = rk * 1.24f + 0.007f * s; foldAmp = 1.f; break;
            case BOT_CARGO: rKneeT = rHemT = rk * 1.6f + 0.012f * s; foldAmp = 1.f; break;
            case BOT_SHORTS: rKneeT = rHemT = rt * 0.95f + 0.012f * s; foldAmp = 0.9f; break;
            case BOT_TRUNKS: rKneeT = rHemT = rt * 0.95f + 0.01f * s; foldAmp = 0.7f; break;
            case BOT_HOTPANTS: foldAmp = 0.3f; break;
            case BOT_LEGGINGS: foldAmp = 0.f; break;
            default: break;
        }
        if (rKneeT > 0.f) {
            const float aC = legAlongAtZ(D, D.zCrotch), aK = D.thigh, aH = legAlongAtZ(D, zCuff), rTop = rt * 1.02f + 0.004f * s;
            const float rkn = rKneeT, rhm = rHemT;
            g.tubeR = [=](const BVert& v) -> float {
                if (v.part != PART_LEG) return 0.f;
                float a = v.pa;
                if (a > aH + 0.02f * s) return 0.f;
                float Rr = a < aK ? Lerp(rTop, rkn, lstep(aC, aK, a)) : (rhm > 0.f ? Lerp(rkn, rhm, lstep(aK, Max(aH, aK + 0.05f), a)) : 0.f);
                return Rr * sstep(aC - 0.02f * s, aC + 0.03f * s, a);
            };
        }
        if (bot != BOT_LEGGINGS && bot != BOT_HOTPANTS) {
            g.hangDrift = 0.12f;
            g.hangTop = D.zHip - 0.005f * s;
            g.hangWeight = [](float th) { return sstep(0.25f, 0.75f, -cosf(th)); };
        }
        FoldSpec fs;
        fs.amp = foldAmp;
        fs.legCuffZ = zCuff;
        fs.legLong = zCuff < R.zAnkle + 0.05f * s && bot != BOT_LEGGINGS;
        fs.denim = g.mat == MAT_DENIM;
        fs.seed = d.seed * 5u + 2u;
        g.foldFn = foldFnOf(makeFolds(c, fs), s);
        g.refineTol = 0.0008f;
    }
    if (g.smooth < 1) g.smooth = 1;
    emitGarment(o, g);
    o.botTorsoOff = g.thick + torsoE + (belt || dutyBelt ? 0.009f : 0.004f);
    o.botTopZ = zt;
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, bool hem = false) {
        GarmentDef dg = decalOf(g, cov, dcol, mat, extraOff, parts, g.matParam);
        dg.hem = hem;
        emitGarment(o, dg);
    };
    CovFn base = g.cov;
    const vec3 thread = g.mat == MAT_DENIM ? srgbToLinear(vec3(0.78f, 0.6f, 0.3f)) : darker(col, 0.8f);   // denim: gold topstitching
    // waistband
    decal([=, &R](const BVert& v) { return v.part == PART_TORSO ? Min(zt - v.bp.z, v.bp.z - (zt - 0.035f * R.s)) : -1.f; }, darker(col, 0.9f), g.mat, 0.0015f,
          1u << PART_TORSO);
    if (g.mat == MAT_DENIM) {
        // outer seams (felled, topstitched), the fly's J-stitch, the back yoke, coin pocket
        decal([=](const BVert& v) {
            if (v.part != PART_LEG) return -1.f;
            return Min(0.004f - fabsf(wrapAngle(v.pb - kHalfPi)) * 0.05f, base(v));
        }, vec3(0.85f, 0.8f, 0.6f), MAT_DENIM, 0.0008f, 1u << PART_LEG);
        const float zFlyTop = zt - 0.035f * s, zFlyBot = R.zCrotch + 0.035f * s;
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f || v.bp.z > zFlyTop || v.bp.x > 0.002f * s) return -1.f;
            // J: straight down 3.2 cm left of the centre, then a quarter circle into the centre seam
            float r0 = 0.032f * s, d0;
            if (v.bp.z >= zFlyBot) d0 = fabsf(v.bp.x + r0);
            else if (v.bp.z >= zFlyBot - r0) d0 = fabsf(length(vec2(v.bp.x, v.bp.z - zFlyBot)) - r0);
            else return -1.f;
            return 0.0011f * s - d0;
        }, thread, MAT_DENIM, 0.0009f, 1u << PART_TORSO);
        const float zYoke = R.zHip + 0.055f * s;
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y > -0.01f) return -1.f;
            float zy = zYoke - 0.035f * s * (1.f - Saturate(fabsf(v.bp.x) / (0.1f * s)));   // V dipping to the centre seam
            return Min(0.0012f * s - fabsf(v.bp.z - zy), zt - 0.03f * s - v.bp.z);
        }, thread, MAT_DENIM, 0.0009f, 1u << PART_TORSO);
        if (bot == BOT_JEANS) {
            // coin pocket inside the right front pocket, rivets at the pocket corners
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO) return -1.f;
                float th = wrapAngle(v.pb);
                return Min(0.018f * s - fabsf((th - 0.78f) * 0.15f * s), Min(zt - 0.013f * s - v.bp.z, v.bp.z - (zt - 0.052f * s)));
            }, darker(col, 0.93f), MAT_DENIM, 0.0012f, 1u << PART_TORSO);
            MeshB rm;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                const float ths[2] = {0.5f, 1.3f}, zs[2] = {zt - 0.013f * s, zt - 0.075f * s};
                for (int k = 0; k < 2; k++) {
                    vec3 n;
                    vec3 p = garmentTorsoPoint(o, g, zs[k], sx * ths[k], n);
                    addDisc(rm, p + n * 0.0012f, n, 0.0025f * s, 6, 0.0012f, srgbToLinear(vec3(0.78f, 0.52f, 0.3f)), MAT_CHROME, torsoSkinWeights(D, p));
                }
            }
            rm.computeNormals(0, 0);
            o.out.append(rm);
            o.hideOut.resize(o.out.idx.size() / 3, 0);
        }
    } else if (bot != BOT_LEGGINGS) {
        // outer seam of plain trousers / shorts
        decal([=](const BVert& v) {
            if (v.part != PART_LEG) return -1.f;
            return Min(0.0022f - fabsf(wrapAngle(v.pb - kHalfPi)) * 0.05f, base(v));
        }, darker(col, 0.82f), g.mat, 0.0007f, 1u << PART_LEG);
        if (bot == BOT_SLACKS || bot == BOT_POLICE) {
            // pressed crease down the front and back of each leg
            for (int k = 0; k < 2; k++) {
                float th0 = k ? kPi : 0.f;
                decal([=](const BVert& v) {
                    if (v.part != PART_LEG) return -1.f;
                    return Min(0.0012f - fabsf(wrapAngle(v.pb - th0)) * 0.05f, base(v));
                }, darker(col, 1.08f), g.mat, 0.0009f, 1u << PART_LEG);
            }
        }
    }
    if (bot != BOT_LEGGINGS) {
        // inseam
        decal([=](const BVert& v) {
            if (v.part != PART_LEG) return -1.f;
            return Min(0.0022f - fabsf(wrapAngle(v.pb + kHalfPi)) * 0.05f, base(v));
        }, g.mat == MAT_DENIM ? vec3(0.8f, 0.75f, 0.56f) * 0.8f : darker(col, 0.82f), g.mat, 0.0007f, 1u << PART_LEG);
    }
    if (bot == BOT_JEANS || bot == BOT_BAGGY || bot == BOT_SLACKS || bot == BOT_WORK || bot == BOT_POLICE || bot == BOT_CARGO || bot == BOT_SHORTS) {
        // front pocket openings: a stitched curve from the waistband down to the side seam
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float zTopP = zt - 0.012f * s;
            bool slant = bot == BOT_SLACKS || bot == BOT_POLICE;
            decal([=](const BVert& v) {
                if (v.part != PART_TORSO) return -1.f;
                float th = wrapAngle(v.pb);
                if (th * sx < 0.f) return -1.f;
                float best = 1e9f;
                for (int k = 0; k <= 12; k++) {
                    float u = k / 12.f;
                    float thc = sx * Lerp(0.5f, 1.3f, slant ? u : sqrtf(u));
                    float zc0 = zTopP - (slant ? 0.1f * u : 0.075f * u * u) * s;
                    float dth = (th - thc) * 0.15f * s, dz = v.bp.z - zc0;
                    best = Min(best, dth * dth + dz * dz);
                }
                return Min(0.0018f * s - sqrtf(best), base(v));
            }, g.mat == MAT_DENIM ? thread : darker(col, 0.8f), g.mat, 0.0009f, 1u << PART_TORSO);
        }
    }
    if (backPockets) {
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float cx = sx * 0.065f * s, cz = R.zHip - 0.005f * s;
            decal([=, &R](const BVert& v) {
                if ((v.part != PART_TORSO && v.part != PART_LEG) || v.bp.y > -0.02f) return -1.f;
                float dx = fabsf(v.bp.x - cx), dz = v.bp.z - cz;
                float pent = Min(0.055f * R.s - dz, Min(0.075f * R.s + dz - dx * 0.9f, 0.052f * R.s - dx));
                return pent;
            }, darker(col, g.mat == MAT_DENIM ? 1.08f : 0.9f), g.mat, 0.002f, (1u << PART_TORSO) | (1u << PART_LEG));
        }
    }
    if (sidePockets) {
        for (int sd = 0; sd < 2; sd++) {
            decal([=, &R](const BVert& v) {
                if (v.part != PART_LEG || v.side != sd) return -1.f;
                float lat = fabsf(wrapAngle(v.pb - kHalfPi));
                return Min(0.55f - lat, 0.075f * R.s - fabsf(v.bp.z - (R.zThighMid - 0.01f * R.s))) * 0.3f;
            }, darker(col, 0.92f), MAT_CLOTH, 0.007f, 1u << PART_LEG, true);
            // flap over the bellows pocket
            decal([=, &R](const BVert& v) {
                if (v.part != PART_LEG || v.side != sd) return -1.f;
                float lat = fabsf(wrapAngle(v.pb - kHalfPi));
                return Min(0.58f - lat, 0.013f * R.s - fabsf(v.bp.z - (R.zThighMid + 0.058f * R.s))) * 0.3f;
            }, darker(col, 0.86f), MAT_CLOTH, 0.0105f, 1u << PART_LEG, true);
        }
    }
    if (bot == BOT_WORK) {
        // knee reinforcement panels
        decal([=, &R](const BVert& v) {
            if (v.part != PART_LEG) return -1.f;
            float frontness = cosf(v.pb);
            return Min(frontness - 0.35f, 0.07f * R.s - fabsf(v.bp.z - R.zKnee)) * 0.3f;
        }, darker(col, 0.8f), MAT_CLOTH, 0.002f, 1u << PART_LEG);
    }
    if (cuffedHem) {
        // turned-up hem: the lighter inside of the fabric shows in a 4 cm band
        vec3 inside = g.mat == MAT_DENIM ? col * 1.45f + vec3(0.03f) : darker(col, 0.92f);
        decal([=](const BVert& v) { return v.part == PART_LEG ? Min(v.bp.z - zcuf, zcuf + 0.04f * s - v.bp.z) : -1.f; }, inside, g.mat, 0.0035f, 1u << PART_LEG,
              true);
    }
