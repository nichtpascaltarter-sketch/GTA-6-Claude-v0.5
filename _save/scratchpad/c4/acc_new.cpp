// ------------------------------------------------------------------------------------------------
// Accessories on the clothes: bags (backpack, crossbody, tote), a lanyard badge, headphones round the neck, bracelets

// Point on the outermost garment over the torso at height z and angle th (the skin plus 2 mm when bare) and its normal.
static vec3 outerTorsoPoint(OutfitCtx& o, float z, float th, vec3& n) {
    if (o.torsoOuter) return garmentTorsoPoint(o, *o.torsoOuter, z, th, n);
    vec3 p;
    torsoPoint(o.c, z, th, p, n);
    return p + n * 0.002f;
}

// Angle on the torso whose surface point at height z lies at model x (front half when front, else back half).
static float torsoAngleForX(OutfitCtx& o, float z, float x, bool front) {
    vec3 p, n;
    torsoPoint(o.c, z, front ? 0.f : kPi, p, n);
    float r = Max(length(vec2(p.x, p.y - profAxisY(o, z))), 0.04f);
    float a = asinf(Clamp(x / (r * 1.05f), -0.95f, 0.95f));
    return front ? a : kPi - a;
}

// Rounded box (superellipsoid, exponent ~4) with half extents he along the frame (ax, ay, az), skinned by sw(p).
static void addRoundedBox(MeshB& m, vec3 c, vec3 ax, vec3 ay, vec3 az, vec3 he, float expo, int NU, int NV, vec3 col, u8 mat, u32 matParam,
                          const std::function<SkinW(vec3)>& sw, const std::function<vec3(vec3, vec3)>& colFn = nullptr) {
    auto sc = [&](float t, float e) { float v = cosf(t); return Sign(v) * powf(fabsf(v), e); };
    auto ss = [&](float t, float e) { float v = sinf(t); return Sign(v) * powf(fabsf(v), e); };
    const float e = 2.f / expo;
    u32 base = (u32)m.v.size();
    for (int j = 0; j <= NV; j++) {
        float phi = -kHalfPi + kPi * j / NV;
        for (int i = 0; i < NU; i++) {
            float th = kTwoPi * i / NU;
            vec3 l(he.x * sc(phi, e) * sc(th, e), he.y * sc(phi, e) * ss(th, e), he.z * ss(phi, e));
            // normal from the implicit function's gradient
            vec3 g(Sign(l.x) * powf(fabsf(l.x) / he.x, expo - 1.f) / he.x, Sign(l.y) * powf(fabsf(l.y) / he.y, expo - 1.f) / he.y,
                   Sign(l.z) * powf(fabsf(l.z) / he.z, expo - 1.f) / he.z);
            if (length2(g) < 1e-12f) g = vec3(0, 0, j < NV / 2 ? -1.f : 1.f);
            vec3 nl = normalize(g);
            BVert v;
            v.p = c + ax * l.x + ay * l.y + az * l.z;
            v.bp = v.p;
            v.n = normalize(ax * nl.x + ay * nl.y + az * nl.z);
            v.t = normalize(ax * -sinf(th) + ay * cosf(th));
            v.uv = vec2(th * (he.x + he.y) * 0.5f, phi * he.z);
            v.col = colFn ? colFn(l, col) : col;
            v.mat = mat;
            v.matParam = matParam;
            v.part = PART_ACC;
            v.sw = sw(v.p);
            m.add(v);
        }
    }
    for (int j = 0; j < NV; j++)
        for (int i = 0; i < NU; i++) {
            u32 a = base + j * NU + i, b = base + j * NU + (i + 1) % NU, cc = base + (j + 1) * NU + (i + 1) % NU, dd = base + (j + 1) * NU + i;
            vec3 nn = cross(m.v[b].p - m.v[a].p, m.v[dd].p - m.v[a].p);
            if (dot(nn, m.v[a].n + m.v[cc].n) >= 0.f) m.quad(a, b, cc, dd);
            else m.quad(a, dd, cc, b);
        }
}

// Flat band (strap) along a polyline with surface normals: halfW across, slightly thicker in the middle.
static void addBand(MeshB& m, const std::vector<vec3>& pts, const std::vector<vec3>& nrm, const std::vector<SkinW>& sws, float halfW, float thick,
                    vec3 col, u8 mat, u32 matParam) {
    const int n = (int)pts.size();
    if (n < 2) return;
    std::vector<u32> rows[4];
    float along = 0.f;
    for (int i = 0; i < n; i++) {
        vec3 t = i + 1 < n ? pts[i + 1] - pts[i] : pts[i] - pts[i - 1];
        if (i > 0) along += length(pts[i] - pts[i - 1]);
        t = normalize(t);
        vec3 nn = normalize(nrm[i] - t * dot(nrm[i], t));
        vec3 w = normalize(cross(t, nn));
        const float acr[4] = {-1.f, -0.75f, 0.75f, 1.f};
        for (int k = 0; k < 4; k++) {
            BVert v;
            v.p = pts[i] + w * (halfW * acr[k]) + nn * (k == 0 || k == 3 ? 0.f : thick);
            v.bp = v.p;
            v.n = normalize(nn + w * (k == 0 ? -0.8f : (k == 3 ? 0.8f : 0.f)));
            v.t = t;
            v.uv = vec2(acr[k] * halfW, along);
            v.col = k == 0 || k == 3 ? col * 0.8f : col;
            v.mat = mat;
            v.matParam = matParam;
            v.part = PART_ACC;
            v.sw = sws[i];
            rows[k].push_back(m.add(v));
        }
    }
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k + 1 < 4; k++) {
            u32 a0 = rows[k][i], a1 = rows[k + 1][i], b0 = rows[k][i + 1], b1 = rows[k + 1][i + 1];
            vec3 nn = cross(m.v[a1].p - m.v[a0].p, m.v[b0].p - m.v[a0].p);
            if (dot(nn, m.v[a0].n + m.v[a1].n) >= 0.f) m.quad(a0, a1, b1, b0);
            else m.quad(a0, b0, b1, a1);
        }
}

// Strap path over one shoulder at |x| = cx: from the chest at zFront over the top of the shoulder to the back at zBack,
// on the outermost garment (off: extra distance out), as points, normals and skin weights.
static void shoulderPath(OutfitCtx& o, float x, float zFront, float zBack, float off, std::vector<vec3>& P, std::vector<vec3>& N,
                         std::vector<SkinW>& W) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const vec3 C0(x, D.J[B_CHEST].y + 0.005f * s, D.zArmpit - 0.03f * s);
    const float clothOff = Max(o.outerTorsoOff, o.topTorsoOff);
    auto hit = [&](float phi, vec3& p, vec3& n) {
        vec3 dir(0.f, cosf(phi), sinf(phi));
        p = c.sdf.project(C0 + dir * (0.17f * s), MK_TORSO, 10);
        n = c.sdf.grad(p, MK_TORSO);
        n = length2(n) > 1e-12f ? normalize(n) : dir;
    };
    const int NA = 48;
    float phi0 = -1.f, phi1 = -1.f;
    for (int i = 0; i <= NA; i++) {
        float phi = Lerp(-0.8f, kPi + 0.8f, (float)i / NA);
        vec3 p, n;
        hit(phi, p, n);
        bool above = p.z >= (phi < kHalfPi ? zFront : zBack);
        if (above && phi0 < -0.5f) phi0 = phi;
        if (above) phi1 = phi;
    }
    if (phi0 < -0.5f || phi1 <= phi0) return;
    const int NP = 16;
    for (int i = 0; i < NP; i++) {
        float phi = Lerp(phi0, phi1, (float)i / (NP - 1));
        vec3 p, n;
        hit(phi, p, n);
        // the clothes under the strap: on the chest and the back below the shoulder line the garment's own surface
        vec3 q = p + n * (clothOff + off);
        if (o.torsoOuter && p.z < D.zArmpit + 0.02f * s) {
            float th = atan2f(p.x, p.y - profAxisY(o, p.z));
            vec3 gn;
            vec3 gp = garmentTorsoPoint(o, *o.torsoOuter, p.z, th, gn);
            q = gp + gn * off;
            n = gn;
        }
        P.push_back(q);
        N.push_back(n);
        W.push_back(torsoSkinWeights(D, p));
    }
}

static void buildBags(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    if (d.bag < 0 || d.bag >= BAG_COUNT) return;
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    Rng rng(hash32(d.seed * 0x1B873593u + 0x2Fu));
    const vec3 col = d.bagColor;
    const vec3 strapCol = rng.chance(0.6f) ? darker(col, 0.8f) : vec3(0.03f);
    MeshB m;
    const float clothOff = Max(o.outerTorsoOff, o.topTorsoOff);
    if (d.bag == BAG_BACKPACK) {
        const float w = 0.28f * s * rng.range(0.9f, 1.08f), h = 0.4f * s * rng.range(0.85f, 1.08f), dep = 0.12f * s * rng.range(0.9f, 1.15f);
        const float zc = R.zChest - 0.05f * s;
        vec3 n;
        vec3 back = outerTorsoPoint(o, zc, kPi, n);
        const vec3 C(0.f, back.y - 0.004f * s - dep * 0.5f, zc);
        const float zTop = zc + h * 0.5f, zBot = zc - h * 0.5f;
        auto bodyW = [=](vec3 p) {
            WAcc acc;
            float t = sstep(zBot, zTop, p.z);
            acc.add(B_CHEST, 0.35f + 0.4f * t);
            acc.add(B_SPINE2, 0.65f - 0.4f * t);
            return acc.finish();
        };
        // the main compartment, a front pocket and a zip line round it; a grab handle on top
        vec3 zipCol = darker(col, 0.55f);
        addRoundedBox(m, C, vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(w * 0.5f, dep * 0.5f, h * 0.5f), 4.f, 18, 12, col, MAT_CLOTH, 1u, bodyW,
                      [=](vec3 l, vec3 cc) {
                          // piping and the zip along the top and the sides of the front panel
                          float edge = fabsf(l.y - dep * 0.32f) < dep * 0.05f ? 1.f : 0.f;
                          return lerp(cc, zipCol, edge);
                      });
        addRoundedBox(m, C + vec3(0.f, -dep * 0.42f, -h * 0.2f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, 1),
                      vec3(w * 0.38f, dep * 0.22f, h * 0.2f), 3.5f, 12, 8, darker(col, 0.92f), MAT_CLOTH, 1u, bodyW);
        {
            std::vector<vec3> hp;
            std::vector<float> hr;
            std::vector<SkinW> hw;
            for (int k = 0; k <= 8; k++) {
                float a = kPi * k / 8.f;
                vec3 p = C + vec3(-0.04f * s * cosf(a), 0.f, h * 0.5f - 0.004f * s + 0.03f * s * sinf(a));
                hp.push_back(p);
                hr.push_back(0.004f * s);
                hw.push_back(bodyW(p));
            }
            addTube(m, hp, hr, 5, false, strapCol, MAT_CLOTH, hw);
        }
        // shoulder straps: from the top of the pack over the shoulders, down the chest to below the armpit, then back
        // under the arm to the bottom corners
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            std::vector<vec3> P, N;
            std::vector<SkinW> W;
            shoulderPath(o, sx * 0.075f * s, D.zArmpit - 0.07f * s, zTop - 0.03f * s, 0.004f * s, P, N, W);
            if (P.size() < 2) continue;
            // start on the pack's top edge, end at the chest; then round the side back to the pack's bottom corner
            std::reverse(P.begin(), P.end());
            std::reverse(N.begin(), N.end());
            std::reverse(W.begin(), W.end());
            vec3 frontEnd = P.back();
            for (int k = 1; k <= 6; k++) {
                float t = k / 6.f;
                float z = Lerp(frontEnd.z, zBot + 0.04f * s, t);
                float th = sx * Lerp(fabsf(atan2f(frontEnd.x, frontEnd.y - profAxisY(o, frontEnd.z))), kPi - 0.55f, t);
                vec3 gn;
                vec3 gp = outerTorsoPoint(o, z, th, gn);
                P.push_back(gp + gn * 0.004f * s);
                N.push_back(gn);
                W.push_back(torsoSkinWeights(D, gp));
            }
            P.push_back(C + vec3(sx * w * 0.42f, dep * 0.3f, -h * 0.42f));
            N.push_back(vec3(sx, 0, 0));
            W.push_back(bodyW(P.back()));
            addBand(m, P, N, W, 0.024f * s, 0.003f * s, strapCol, MAT_CLOTH, 1u);
        }
    } else {
        // crossbody bag behind one hip with its strap across the chest and the back over the other shoulder, or a tote
        // hanging at the side from the same shoulder
        const int side = bagSide(d);
        const float sx = side ? 1.f : -1.f;
        const bool tote = d.bag == BAG_TOTE;
        const float w = (tote ? 0.3f : 0.22f) * s * rng.range(0.9f, 1.1f), h = (tote ? 0.28f : 0.16f) * s * rng.range(0.9f, 1.1f),
                    dep = (tote ? 0.08f : 0.06f) * s;
        const float zc = tote ? R.zWaist - 0.02f * s : D.zHip + 0.035f * s;
        const float th = sx * (kHalfPi + (tote ? 0.35f : 0.62f));
        vec3 n;
        vec3 hip = outerTorsoPoint(o, zc, th, n);
        hip += n * Max(0.f, o.botTorsoOff - clothOff);
        vec3 radial = normalize(vec3(n.x, n.y, 0.f));
        vec3 tang = normalize(cross(vec3(0, 0, 1), radial));
        const vec3 C = hip + radial * (dep * 0.5f + 0.004f * s);
        auto bodyW = [=](vec3 p) {
            WAcc acc;
            acc.add(B_PELVIS, tote ? 0.3f : 0.75f);
            acc.add(tote ? B_SPINE2 : (side ? B_THIGH_R : B_THIGH_L), tote ? 0.7f : 0.25f);
            (void)p;
            return acc.finish();
        };
        addRoundedBox(m, C, tang, radial, vec3(0, 0, 1), vec3(w * 0.5f, dep * 0.5f, h * 0.5f), tote ? 3.f : 4.f, 16, 10, col, tote ? MAT_CLOTH : MAT_LEATHER,
                      tote ? 1u : 0u, bodyW);
        if (!tote) {
            // flap over the front
            addRoundedBox(m, C + radial * (dep * 0.46f) + vec3(0, 0, h * 0.12f), tang, radial, vec3(0, 0, 1), vec3(w * 0.51f, dep * 0.08f, h * 0.36f),
                          5.f, 12, 6, darker(col, 0.85f), MAT_LEATHER, 0u, bodyW);
        }
        // strap: over the opposite shoulder (crossbody) or the same one (tote)
        const float xs = (tote ? sx : -sx) * 0.08f * s;
        std::vector<vec3> P, N;
        std::vector<SkinW> W;
        shoulderPath(o, xs, D.zArmpit - 0.02f * s, D.zArmpit - 0.02f * s, 0.004f * s, P, N, W);
        if (P.size() >= 2) {
            // front run: from the bag's front corner up to the shoulder; back run: from the shoulder down to the back corner
            vec3 cf = C + tang * (w * 0.45f * (tang.y > 0.f ? 1.f : -1.f)) + vec3(0, 0, h * 0.45f);
            vec3 cb = C - tang * (w * 0.45f * (tang.y > 0.f ? 1.f : -1.f)) + vec3(0, 0, h * 0.45f);
            auto run = [&](vec3 from, vec3 to, bool front, std::vector<vec3>& RP, std::vector<vec3>& RN, std::vector<SkinW>& RW) {
                const int K = 9;
                for (int k = 0; k <= K; k++) {
                    float t = (float)k / K;
                    float z = Lerp(from.z, to.z, t), x = Lerp(from.x, to.x, t);
                    float a = torsoAngleForX(o, z, x, front);
                    vec3 gn;
                    vec3 gp = outerTorsoPoint(o, z, a, gn);
                    RP.push_back(gp + gn * 0.004f * s);
                    RN.push_back(gn);
                    RW.push_back(torsoSkinWeights(D, gp));
                }
            };
            std::vector<vec3> SP, SN;
            std::vector<SkinW> SW;
            run(cf, P.front(), true, SP, SN, SW);
            for (size_t i = 0; i < P.size(); i++) {
                SP.push_back(P[i]);
                SN.push_back(N[i]);
                SW.push_back(W[i]);
            }
            std::vector<vec3> BP, BN;
            std::vector<SkinW> BW;
            run(cb, P.back(), false, BP, BN, BW);
            for (int i = (int)BP.size() - 1; i >= 0; i--) {
                SP.push_back(BP[i]);
                SN.push_back(BN[i]);
                SW.push_back(BW[i]);
            }
            addBand(m, SP, SN, SW, (tote ? 0.014f : 0.012f) * s, 0.0025f * s, strapCol, tote ? MAT_CLOTH : MAT_LEATHER, 1u);
        }
    }
    m.computeNormals(0, m.idx.size());
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// Lanyard with an ID badge (office): a cord round the neck dropping to a card on the chest; headphones round the neck;
// bangles on the wrists.
static void buildSmallAccessories(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    Rng rng(hash32(d.seed * 0x7FEB352Du + 0x3Bu));
    MeshB m;
    const std::vector<u32>& ring = c.torsoTop;
    const int n = (int)ring.size();
    vec3 cen(0);
    for (u32 vi : ring) cen += c.m.v[vi].p;
    cen /= (float)Max(1, n);
    const float clothOff = Max(o.outerTorsoOff, o.topTorsoOff);
    if (d.extras & ACC_LANYARD) {
        vec3 cordCol = srgbToLinear(rng.pick(std::vector<vec3>{vec3(0.1f, 0.2f, 0.55f), vec3(0.6f, 0.08f, 0.1f), vec3(0.05f), vec3(0.1f, 0.4f, 0.2f)}));
        const float zCard = R.zChest - 0.07f * s;
        std::vector<vec3> pts;
        std::vector<float> rad;
        std::vector<SkinW> sws;
        // behind and beside the neck on the collar line, then down the chest in a V to the card clip
        for (int k = n / 2 - n / 4 + 1; k <= n / 2 + n / 4 - 1; k++) {
            const BVert& bv = c.m.v[ring[k % n]];
            vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
            pts.push_back(bv.p + radial * (clothOff + 0.006f * s) + vec3(0, 0, 0.004f * s));
            rad.push_back(0.0016f * s);
            sws.push_back(torsoSkinWeights(D, bv.p));
        }
        // the two front runs, from each side of the neck to the clip
        auto frontRun = [&](float xs, bool reverse) {
            std::vector<vec3> P;
            std::vector<SkinW> W;
            for (int k = 0; k <= 7; k++) {
                float t = k / 7.f;
                float z = Lerp(D.zNeckFront + 0.005f * s, zCard + 0.02f * s, t), x = Lerp(xs, 0.f, t * t);
                float a = torsoAngleForX(o, z, x, true);
                vec3 gn;
                vec3 gp = outerTorsoPoint(o, z, a, gn);
                P.push_back(gp + gn * 0.005f * s);
                W.push_back(torsoSkinWeights(D, gp));
            }
            if (reverse) {
                std::reverse(P.begin(), P.end());
                std::reverse(W.begin(), W.end());
            }
            return std::make_pair(P, W);
        };
        auto L = frontRun(-0.05f * s, false), Rr = frontRun(0.05f * s, false);
        std::vector<vec3> all;
        std::vector<float> ar;
        std::vector<SkinW> aw;
        for (int i = (int)L.first.size() - 1; i >= 0; i--) { all.push_back(L.first[i]); aw.push_back(L.second[i]); }
        for (size_t i = 0; i < pts.size(); i++) { all.push_back(pts[pts.size() - 1 - i]); aw.push_back(sws[pts.size() - 1 - i]); }
        for (size_t i = 0; i < Rr.first.size(); i++) { all.push_back(Rr.first[i]); aw.push_back(Rr.second[i]); }
        ar.assign(all.size(), 0.0016f * s);
        addTube(m, all, ar, 4, false, cordCol, MAT_CLOTH, aw);
        // the card in its sleeve: white with a coloured band, facing out from the chest
        vec3 gn;
        vec3 gp = outerTorsoPoint(o, zCard - 0.04f * s, 0.f, gn);
        vec3 fwd = normalize(vec3(gn.x * 0.3f, Max(gn.y, 0.5f), 0.f));
        vec3 ax = normalize(cross(vec3(0, 0, 1), fwd));
        SkinW cw = torsoSkinWeights(D, gp);
        addBoxOriented(m, gp + fwd * 0.006f * s, ax, vec3(0, 0, 1), fwd, vec3(0.027f, 0.042f, 0.0012f) * s, vec3(0.9f), MAT_PLASTIC, cw);
        addBoxOriented(m, gp + fwd * 0.0073f * s + vec3(0, 0, 0.028f * s), ax, vec3(0, 0, 1), fwd, vec3(0.027f, 0.01f, 0.0003f) * s, cordCol, MAT_PLASTIC,
                       cw);
    }
    if (d.extras & ACC_HEADPHONES) {
        // band behind the neck on the collar, a cup either side of the neck in front resting on the collarbones
        vec3 hc = srgbToLinear(rng.chance(0.5f) ? vec3(0.08f) : vec3(0.85f));
        std::vector<vec3> pts;
        std::vector<float> rad;
        std::vector<SkinW> sws;
        for (int k = n / 2 - n / 4; k <= n / 2 + n / 4; k++) {
            const BVert& bv = c.m.v[ring[k % n]];
            vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
            pts.push_back(bv.p + radial * (clothOff + 0.012f * s) + vec3(0, 0, 0.008f * s));
            rad.push_back(0.006f * s);
            WAcc acc;
            acc.add(B_NECK, 0.4f);
            acc.add(B_CHEST, 0.6f);
            sws.push_back(acc.finish());
        }
        addTube(m, pts, rad, 6, false, hc, MAT_PLASTIC, sws);
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            const BVert& bv = c.m.v[ring[(sd ? n / 4 : 3 * n / 4) % n]];
            vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
            vec3 cp = bv.p + radial * (clothOff + 0.03f * s) + vec3(0, 0.012f * s, -0.01f * s);
            WAcc acc;
            acc.add(B_NECK, 0.3f);
            acc.add(B_CHEST, 0.7f);
            SkinW cw = acc.finish();
            vec3 ay = normalize(radial + vec3(0, 0, 0.35f));
            vec3 ax = normalize(cross(vec3(0, 0, 1), ay));
            vec3 az = cross(ax, ay);
            addRoundedBox(m, cp, ax, ay, az, vec3(0.034f, 0.015f, 0.04f) * s, 3.f, 12, 6, hc, MAT_PLASTIC, 0u, [cw](vec3) { return cw; });
            (void)sx;
        }
    }
    for (int sd = 0; sd < 2; sd++) {
        if (!(d.extras & (sd ? ACC_BRACELET_R : ACC_BRACELET_L))) continue;
        // a bangle or a bead string round the wrist, loose (it rides down onto the hand's heel)
        const int arm = sd ? MK_ARM_R : MK_ARM_L;
        vec3 wr = D.J[sd ? B_HAND_R : B_HAND_L] - D.armDir[sd] * (0.012f * s);
        vec3 ad = D.armDir[sd], fr(0, 1, 0), pn = D.palmN[sd];
        bool beads = rng.chance(0.5f);
        vec3 bc = beads ? srgbToLinear(rng.pick(std::vector<vec3>{vec3(0.55f, 0.3f, 0.15f), vec3(0.1f), vec3(0.85f, 0.8f, 0.7f), vec3(0.2f, 0.4f, 0.6f)}))
                        : (rng.chance(0.6f) ? srgbToLinear(vec3(1.f, 0.78f, 0.35f)) : vec3(0.9f));
        std::vector<vec3> pts;
        std::vector<float> rad;
        const int N = beads ? 10 : 14;
        for (int k = 0; k < N; k++) {
            float a = kTwoPi * k / N;
            vec3 dir = normalize(fr * cosf(a) + (-pn) * sinf(a));
            float t = c.sdf.castOut(wr, dir, (u32)arm, 0.1f);
            pts.push_back(wr + dir * (t + 0.004f * s));
            rad.push_back((beads ? 0.0035f : 0.0022f) * s);
        }
        std::vector<SkinW> sws(N, skin2(sd ? B_FOREARM_R : B_FOREARM_L, sd ? B_HAND_R : B_HAND_L, 0.6f));
        addTube(m, pts, rad, beads ? 5 : 4, true, bc, beads ? MAT_PLASTIC : MAT_CHROME, sws, ad);
    }
    m.computeNormals(0, m.idx.size());
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

