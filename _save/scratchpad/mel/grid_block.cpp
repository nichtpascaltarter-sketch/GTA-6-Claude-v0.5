    // ---- column layouts (right half, KH columns strictly between 0 and pi). Feature families put columns on the
    // eye corners, the mouth corner and the nose wing; rows blend between families.
    const int KH = 28;
    const int NC = 2 * KH + 2;
    H.cols = NC;
    std::vector<float> colUniform(KH), colEye(KH), colMouth(KH), colNose(KH), colFace(KH);
    for (int k = 0; k < KH; k++) colUniform[k] = kPi * (k + 1) / (KH + 1);
    for (int k = 0; k < KH; k++) colFace[k] = kPi * powf((k + 1.f) / (KH + 1), 1.3f);
    auto featCols = [&](std::vector<float>& out, const float* stops, const int* counts, int nseg, float pw) {
        // piecewise uniform up to the last stop, then a power ramp towards the back of the head
        int n = 0;
        float a = 0.f;
        for (int sgi = 0; sgi < nseg; sgi++) {
            for (int k = 1; k <= counts[sgi]; k++) out[n++] = a + (stops[sgi] - a) * k / counts[sgi];
            a = stops[sgi];
        }
        int rest = KH - n;
        for (int k = 1; k <= rest; k++) out[n++] = a + (kPi - a) * powf((float)k / (rest + 1), pw);
    };
    {
        const float st[2] = {L.thI, L.thO};
        const int cn[2] = {4, 11};
        featCols(colEye, st, cn, 2, 1.3f);
    }
    {
        const float st[1] = {L.thMC};
        const int cn[1] = {9};
        featCols(colMouth, st, cn, 1, 1.38f);
    }
    {
        const float st[1] = {L.thAla};
        const int cn[1] = {6};
        featCols(colNose, st, cn, 1, 1.4f);
    }
    auto fullCols = [&](const std::vector<float>& a, const std::vector<float>& b, float t, std::vector<float>& out) {
        out.resize(NC);
        out[0] = 0.f;
        for (int k = 0; k < KH; k++) {
            float v = Lerp(a[k], b[k], t);
            out[1 + k] = v;
            out[NC - 1 - k] = kTwoPi - v;
        }
        out[KH + 1] = kPi;
    };
    // ---- rows (bottom to top). phi: front elevation (degrees) away from the features; feature rows follow the lip
    // line / lid margins at an offset (degrees, scaled by the lip pinch / lid span) and relax to evenly spaced values
    // between the bracketing plain rows away from the mouth and eyes. Each row also carries its share of the speech
    // (lip) bones, the eye bones (upper lid) and the brow bones.
    enum { RK_PLAIN, RK_LIP, RK_MOUTHLO, RK_MOUTHHI, RK_LIDLO, RK_EYELO, RK_EYEHI, RK_LIDUP };
    struct RowDef {
        float phi;
        int kind;
        int colA, colB;   // column family: 0 uniform 1 face 2 mouth 3 nose 4 eye
        float colT;
        float off;        // feature offset (degrees)
        float lipW, lidW, browW;
    };
    const RowDef rows[] = {
        {-68.0f, RK_PLAIN, 0, 1, 0.3f, 0, 0, 0, 0},      {-65.0f, RK_PLAIN, 0, 1, 0.6f, 0, 0, 0, 0},
        {-62.0f, RK_PLAIN, 1, 2, 0.15f, 0, 0, 0, 0},     {-59.2f, RK_PLAIN, 1, 2, 0.35f, 0, 0, 0, 0},
        {-56.4f, RK_PLAIN, 1, 2, 0.55f, 0, 0, 0, 0},     {-53.8f, RK_PLAIN, 1, 2, 0.75f, 0, 0, 0, 0},
        {-51.3f, RK_PLAIN, 1, 2, 0.9f, 0, 0, 0, 0},      {-48.9f, RK_PLAIN, 2, 2, 0.f, 0, 0.1f, 0, 0},
        {-46.6f, RK_PLAIN, 2, 2, 0.f, 0, 0.3f, 0, 0},
        // lower lip: skin below the vermilion, vermilion border, lip body, wet edge
        {0.f, RK_LIP, 2, 2, 0.f, -7.0f, 0.55f, 0, 0},    {0.f, RK_LIP, 2, 2, 0.f, -5.3f, 0.8f, 0, 0},
        {0.f, RK_LIP, 2, 2, 0.f, -3.6f, 0.95f, 0, 0},    {0.f, RK_LIP, 2, 2, 0.f, -1.8f, 1.f, 0, 0},
        {0.f, RK_MOUTHLO, 2, 2, 0.f, 0.f, 1.f, 0, 0},
        {0.f, RK_MOUTHHI, 2, 2, 0.f, 0.f, 1.f, 0, 0},
        // upper lip: wet edge, lip body, vermilion border (white roll), then the philtrum up to the nose
        {0.f, RK_LIP, 2, 2, 0.f, 1.3f, 1.f, 0, 0},       {0.f, RK_LIP, 2, 2, 0.f, 2.6f, 0.95f, 0, 0},
        {0.f, RK_LIP, 2, 2, 0.f, 3.9f, 0.85f, 0, 0},
        {-31.2f, RK_PLAIN, 2, 3, 0.2f, 0, 0.55f, 0, 0},  {-29.3f, RK_PLAIN, 2, 3, 0.4f, 0, 0.25f, 0, 0},
        {-27.5f, RK_PLAIN, 2, 3, 0.6f, 0, 0.08f, 0, 0},  {-25.6f, RK_PLAIN, 2, 3, 0.8f, 0, 0, 0, 0},
        // nose and cheeks
        {-23.6f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},       {-21.5f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},
        {-19.3f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},       {-17.1f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},
        {-14.8f, RK_PLAIN, 3, 4, 0.15f, 0, 0, 0, 0},     {-12.4f, RK_PLAIN, 3, 4, 0.3f, 0, 0, 0, 0},
        {-9.9f, RK_PLAIN, 3, 4, 0.45f, 0, 0, 0, 0},      {-7.4f, RK_PLAIN, 3, 4, 0.6f, 0, 0, 0, 0},
        {-4.9f, RK_PLAIN, 3, 4, 0.75f, 0, 0, 0, 0},      {-2.5f, RK_PLAIN, 3, 4, 0.9f, 0, 0, 0, 0},
        // lower lid: lid-cheek junction, lid bulge, margin outer edge, margin
        {0.f, RK_LIDLO, 4, 4, 0.f, 3.6f, 0, 0, 0},       {0.f, RK_LIDLO, 4, 4, 0.f, 2.2f, 0, 0, 0},
        {0.f, RK_LIDLO, 4, 4, 0.f, 1.0f, 0, 0, 0},       {0.f, RK_EYELO, 4, 4, 0.f, 0.f, 0, 0, 0},
        // upper lid: margin, lash line, tarsal plate, crease, fold
        {0.f, RK_EYEHI, 4, 4, 0.f, 0.f, 0, 1.f, 0},      {0.f, RK_LIDUP, 4, 4, 0.f, 0.9f, 0, 1.f, 0},
        {0.f, RK_LIDUP, 4, 4, 0.f, 2.0f, 0, 0.83f, 0},   {0.f, RK_LIDUP, 4, 4, 0.f, 3.1f, 0, 0.52f, 0},
        {0.f, RK_LIDUP, 4, 4, 0.f, 4.3f, 0, 0.15f, 0.3f},
        // brows, forehead, scalp
        {16.2f, RK_PLAIN, 4, 4, 0.f, 0, 0, 0, 0.75f},    {18.4f, RK_PLAIN, 4, 1, 0.2f, 0, 0, 0, 0.95f},
        {20.8f, RK_PLAIN, 4, 1, 0.4f, 0, 0, 0, 1.f},     {23.5f, RK_PLAIN, 4, 1, 0.6f, 0, 0, 0, 0.9f},
        {26.7f, RK_PLAIN, 4, 1, 0.8f, 0, 0, 0, 0.7f},    {30.5f, RK_PLAIN, 1, 1, 0.f, 0, 0, 0, 0.45f},
        {35.2f, RK_PLAIN, 1, 0, 0.2f, 0, 0, 0, 0.22f},   {40.6f, RK_PLAIN, 1, 0, 0.4f, 0, 0, 0, 0.08f},
        {46.5f, RK_PLAIN, 1, 0, 0.6f, 0, 0, 0, 0},       {53.0f, RK_PLAIN, 1, 0, 0.8f, 0, 0, 0, 0},
        {60.0f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},        {67.5f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},
        {75.0f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},        {82.5f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},
    };
    const int NRD = (int)(sizeof(rows) / sizeof(rows[0]));
    const int NR = NRD + 1;   // + row 0 (neck ring)
    H.rows = NR;
    // named rows (grid row j = index into rows[] + 1)
    H.rowLipLo = H.rowLipHi = H.rowLidLo = H.rowLidHi = -1;
    H.rowNoseBase = H.rowBrow = H.rowHairline = H.rowChin = -1;
    for (int r = 0; r < NRD; r++) {
        int j = r + 1;
        const RowDef& rd = rows[r];
        if (rd.kind == RK_MOUTHLO) H.rowMouthLo = j;
        if (rd.kind == RK_MOUTHHI) H.rowMouthHi = j;
        if (rd.kind == RK_EYELO) H.rowEyeLo = j;
        if (rd.kind == RK_EYEHI) H.rowEyeHi = j;
        if (rd.kind == RK_LIP && H.rowLipLo < 0) H.rowLipLo = j;
        if (rd.kind == RK_LIP) H.rowLipHi = j;
        if (rd.kind == RK_LIDLO && H.rowLidLo < 0) H.rowLidLo = j;
        if (rd.kind == RK_LIDUP) H.rowLidHi = j;
        if (rd.kind == RK_PLAIN && H.rowChin < 0 && rd.phi >= -60.f) H.rowChin = j;
        if (rd.kind == RK_PLAIN && H.rowNoseBase < 0 && rd.phi >= -26.f) H.rowNoseBase = j;
        if (rd.kind == RK_PLAIN && H.rowBrow < 0 && rd.phi >= 18.f) H.rowBrow = j;
        if (rd.kind == RK_PLAIN && H.rowHairline < 0 && rd.phi >= 44.f) H.rowHairline = j;
    }
    // bracketing plain rows of every feature row (index into rows[])
    std::vector<int> plainBelow(NRD, 0), plainAbove(NRD, 0);
    for (int r = 0; r < NRD; r++) {
        int a = r, b = r;
        while (a > 0 && rows[a].kind != RK_PLAIN) a--;
        while (b < NRD - 1 && rows[b].kind != RK_PLAIN) b++;
        plainBelow[r] = a;
        plainAbove[r] = b;
    }
    const std::vector<float>* colLists[5] = {&colUniform, &colFace, &colMouth, &colNose, &colEye};
    H.grid.assign((size_t)NR * NC, 0);
    // ---- row 0: ring on the neck just below the jaw/skull
    vec3 n0c = headToModel(c, vec3(0, -0.017f, -0.052f));
    const float tilt = 32.f * deg;
    vec3 nAx(0, sinf(tilt), cosf(tilt));
    vec3 nF(0, cosf(tilt), -sinf(tilt));
    std::vector<float> row0Phi(NC), row0Th(NC);
    std::vector<float> col0;
    fullCols(colUniform, colUniform, 0.f, col0);
    for (int k = 0; k < NC; k++) {
        float th = col0[k];
        vec3 dir = nF * cosf(th) + vec3(1, 0, 0) * sinf(th);
        float t = c.sdf.castOut(n0c, dir, MK_NECK | MK_HEAD, 0.2f * hs);
        vec3 p = n0c + dir * t;
        float pth, pph;
        angOf(p - C, pth, pph);
        row0Phi[k] = pph;
        if (pth < 0.f) pth += kTwoPi;
        if (k == 0 && pth > kPi) pth -= kTwoPi;
        row0Th[k] = pth;
        WAcc acc;
        acc.add(B_HEAD, 0.8f);
        acc.add(B_NECK, 0.2f);
        BVert v;
        v.p = p;
        v.col = c.skin;
        v.mat = MAT_SKIN;
        v.part = PART_HEAD;
        v.side = p.x < 0.f ? 0 : 1;
        v.sw = acc.finish();
        v.pa = th;
        v.pb = pph;
        v.uv = vec2(uWrap(th, kPi, 0.07f * hs), p.z);
        v.uPer = kTwoPi * 0.07f * hs;
        v.pc = 1.2f;
        v.t = normalize(cross(nAx, dir));
        v.axisPt = n0c;
        H.grid[k] = m.add(v);
    }
    c.neckTopFirst = H.grid[0];
    // ---- rows 1..NRD
    const float backTop = 84.f * deg;
    std::vector<float> cols;
    for (int r = 0; r < NRD; r++) {
        const RowDef& rd = rows[r];
        int j = r + 1;
        fullCols(*colLists[rd.colA], *colLists[rd.colB], rd.colT, cols);
        float rowFrac = (float)j / NRD;   // for back layout
        for (int k = 0; k < NC; k++) {
            float th = cols[k];
            if (j <= 3) {
                // align the first rows with the actual azimuths of the neck ring (avoids twisted quads)
                float a0 = row0Th[k];
                while (a0 - th > kPi) a0 -= kTwoPi;
                while (th - a0 > kPi) a0 += kTwoPi;
                th = Lerp(a0, th, (float)j / 4.f);
                if (th < 0.f) th += kTwoPi;
                if (th >= kTwoPi) th -= kTwoPi;
            }
            float ath = th > kPi ? kTwoPi - th : th;
            // front (face) phi: feature rows follow the eye/lip contours near the features and relax to evenly
            // spaced "plain" values (between the bracketing plain rows) away from them.
            float phF = rd.phi * deg;
            float phC, hHi, hLo, inSpan, gap, pinch;
            eyeRows(L, ath, phC, hHi, hLo, inSpan);
            float phM = lipLine(L, ath, gap, pinch);
            float dEye = ath < L.thI ? L.thI - ath : (ath > L.thO ? ath - L.thO : 0.f);
            float wEye = 1.f - sstep(0.f, 12.f * deg, dEye);
            float wLip = 1.f - sstep(0.f, 13.f * deg, ath - L.thMC);
            if (rd.kind != RK_PLAIN) {
                bool lip = rd.kind <= RK_MOUTHHI;
                int first = plainBelow[r], last = plainAbove[r];
                float plain = Lerp(rows[first].phi, rows[last].phi, (float)(r - first) / (last - first)) * deg;
                float feat = phF;
                switch (rd.kind) {
                    case RK_EYELO: feat = phC - hLo; break;
                    case RK_EYEHI: feat = phC + hHi; break;
                    case RK_LIDLO: feat = phC - hLo - rd.off * deg * (0.79f + 0.21f * inSpan); break;
                    case RK_LIDUP: feat = phC + hHi + rd.off * deg * (0.7f + 0.3f * inSpan); break;
                    case RK_MOUTHLO: feat = phM - gap; break;
                    case RK_MOUTHHI: feat = phM + gap; break;
                    case RK_LIP: feat = rd.off < 0.f ? phM - gap + rd.off * deg * pinch : phM + gap + rd.off * deg * pinch; break;
                    default: break;
                }
                phF = Lerp(plain, feat, lip ? wLip : wEye);
            }
            // under-chin rows start from the neck ring
            float ph0 = row0Phi[k];
            if (j <= 2) {
                float target = rows[2].phi * deg;
                phF = Lerp(ph0, target, (float)j / 3.f);
            }
            // back layout: spread rows evenly from the neck ring up to the crown
            float phB = Lerp(ph0, backTop, powf(rowFrac, 0.92f));
            float w = sstep(118.f * deg, 58.f * deg, ath);
            float ph = Lerp(phB, phF, w);
            // keep rows above the neck ring
            ph = Max(ph, ph0 + 0.6f * deg * (float)j);
            vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
            float t = c.sdf.castOut(C, dir, HM, 0.22f * hs);
            vec3 p = C + dir * t;
            BVert v;
            v.p = p;
            v.col = c.skin;
            v.mat = MAT_SKIN;
            v.part = PART_HEAD;
            v.side = p.x < 0.f ? 0 : 1;
            v.pa = th;
            v.pb = ph;
            v.uv = vec2(uWrap(th, kPi, 0.07f * hs), ph * 0.09f * hs);
            v.uPer = kTwoPi * 0.07f * hs;
            v.pc = 1.2f + (float)j / NRD;
            v.t = vec3(cosf(th), -sinf(th), 0.f);
            v.axisPt = C;
            // ---- weights: jaw for the lower face
            vec3 hp = (p - D.J[B_HEAD]) / hs;
            float front = sstep(95.f * deg, 50.f * deg, ath);
            float below = (j < H.rowMouthHi) ? 1.f : 0.f;
            float jawW = below * front;
            if (j < H.rowMouthLo) jawW = front * sstep(-0.004f, -0.012f, hp.z - Lm.stomion.z + 0.01f * (1.f - front));
            if (j <= H.rowMouthLo) jawW = Max(jawW, front * sstep(80.f * deg, 30.f * deg, ath));
            jawW *= sstep(-0.02f, 0.03f, hp.y);   // towards the ear the jaw influence fades
            if (j <= 2) jawW *= 0.6f;
            WAcc acc;
            acc.add(B_JAW, jawW);
            acc.add(B_HEAD, 1.f - jawW);
            v.sw = acc.finish();
            // ---- base colours: vermilion, nostril shade (the regional skin tones are painted by paintFaceSkin)
            vec3 col = c.skin;
            if (rd.kind == RK_LIP || rd.kind == RK_MOUTHLO || rd.kind == RK_MOUTHHI) {
                float u = ath / L.thMC;
                float lipMask = 1.f - sstep(0.85f, 1.12f, u);
                float ao = fabsf(rd.off);
                if (rd.kind == RK_LIP) lipMask *= rd.off < 0.f ? 1.f - sstep(4.6f, 6.4f, ao) : 1.f - sstep(3.3f, 4.6f, ao);
                if (lipMask > 0.f) {
                    col = lerp(col, c.lipCol, lipMask);
                    v.flags |= BuildCtx::F_LIP;
                }
            }
            {
                // nostrils: underside of the nose between columella and alae (a shade: the grid has no holes)
                vec3 hq = hp;
                float nx = fabsf(hq.x);
                if (hq.z < Lm.ala[1].z + 0.002f && hq.z > Lm.subnasale.z - 0.001f && hq.y > Lm.subnasale.y + 0.0015f &&
                    hq.y < Lm.noseTip.y - 0.005f && nx > 0.003f && nx < fabsf(Lm.ala[1].x) - 0.002f) {
                    vec3 gn = normalize(c.sdf.grad(p, HM));
                    col = col * Lerp(1.f, 0.55f, sstep(-0.25f, -0.7f, gn.z));
                }
            }
            if (j >= H.rowMouthHi + 2 && j <= H.rowHairline) v.flags |= BuildCtx::F_FACE;
            if (ath < 110.f * deg && j < H.rowLidLo && j >= 1) v.flags |= BuildCtx::F_BEARD;
            if (j > H.rowLidHi) v.flags |= BuildCtx::F_SCALP;
            v.col = col;
            H.grid[(size_t)j * NC + k] = m.add(v);
        }
    }
    // pole
    {
        vec3 dir = normalize(vec3(0, -0.08f, 1.f));
        float t = c.sdf.castOut(C, dir, HM, 0.25f * hs);
        BVert v;
        v.p = C + dir * t;
        v.col = c.skin;
        v.mat = MAT_SKIN;
        v.part = PART_HEAD;
        v.sw = skin1(B_HEAD);
        v.pa = 0.f;
        v.pb = kHalfPi;
        v.pc = 2.2f;
        v.uv = vec2(0, 0.14f * hs);
        v.t = vec3(1, 0, 0);
        v.flags = BuildCtx::F_SCALP;
        v.axisPt = C;
        u32 pole = m.add(v);
        for (int k = 0; k < NC; k++) m.tri(H.grid[(size_t)(NR - 1) * NC + k], pole, H.grid[(size_t)(NR - 1) * NC + (k + 1) % NC]);
    }
    // ---- faces (skip the eye fissures and the mouth slit)
    for (int j = 0; j + 1 < NR; j++)
        for (int k = 0; k < NC; k++) {
            int k1 = (k + 1) % NC;
            u32 a0 = H.grid[(size_t)j * NC + k], a1 = H.grid[(size_t)j * NC + k1];
            u32 b0 = H.grid[(size_t)(j + 1) * NC + k], b1 = H.grid[(size_t)(j + 1) * NC + k1];
            if (j == H.rowEyeLo) {
                float t0 = m.v[a0].pa, t1 = m.v[a1].pa;
                float a0t = t0 > kPi ? kTwoPi - t0 : t0, a1t = t1 > kPi ? kTwoPi - t1 : t1;
                if (a0t >= L.thI - 1e-4f && a0t <= L.thO + 1e-4f && a1t >= L.thI - 1e-4f && a1t <= L.thO + 1e-4f) continue;
            }
            if (j == H.rowMouthLo) {
                float t0 = m.v[a0].pa, t1 = m.v[a1].pa;
                float a0t = t0 > kPi ? kTwoPi - t0 : t0, a1t = t1 > kPi ? kTwoPi - t1 : t1;
                if (a0t <= L.thMC + 1e-4f && a1t <= L.thMC + 1e-4f) continue;
            }
            // split each quad along the diagonal that keeps the surface convex-ish (follows the shorter diagonal)
            float d0 = length2(m.v[a0].p - m.v[b1].p), d1 = length2(m.v[b0].p - m.v[a1].p);
            if (d0 <= d1) m.quad(a0, b0, b1, a1);
            else {
                m.tri(a0, b0, a1);
                m.tri(b0, b1, a1);
            }
        }
    // Speech, eye and brow bones share the face: every row carries its weights for them (RowDef lipW / lidW /
    // browW), faded across the face by theta so the skin stretches smoothly into the head / jaw weights.
    //  - lips: the lower lip rows on B_LIP_LOWER (a child of the jaw), the upper lip rows on B_LIP_UPPER, the corners
    //    and the skin just outside them on B_LIP_CORNER_*;
    //  - upper lids ride on the eye bones (the skeleton has no lid bones): the margin rotates about the eyeball centre
    //    with the eye, so it follows vertical gaze like a real lid and the animator blinks by pitching the eye down;
    //    full weight across the middle of the fissure, nothing at the corners, the crease and fold follow partially;
    //  - forehead skin under the brows rides on the brow bones (raised / knitted brows move the skin with them).
    {
        const float halfSpan = 0.5f * (L.thO - L.thI);
        const float thIn = H.thetaEye - 22.f * deg, thOut = H.thetaEye + 26.f * deg;
        for (int r = 0; r < NRD; r++) {
            const RowDef& rd = rows[r];
            if (rd.lipW <= 0.f && rd.lidW <= 0.f && rd.browW <= 0.f) continue;
            int j = r + 1;
            bool upperLip = j >= H.rowMouthHi;
            for (int k = 0; k < NC; k++) {
                BVert& v = m.v[H.grid[(size_t)j * NC + k]];
                float th = v.pa;
                bool right = th < kPi;
                float at = right ? th : kTwoPi - th;
                if (rd.lipW > 0.f) {
                    float u = at / Max(L.thMC, 1e-3f);   // 0 centre .. 1 mouth corner
                    float wLip = 1.f - sstep(0.5f, 1.05f, u);
                    float wCor = sstep(0.4f, 0.95f, u) * (1.f - sstep(1.25f, 2.1f, u)) * (upperLip ? 0.85f : 0.8f);
                    float sum = wLip + wCor;
                    if (sum > 1.f) {
                        wLip /= sum;
                        wCor /= sum;
                    }
                    wLip *= rd.lipW;
                    wCor *= rd.lipW;
                    if (wLip + wCor > 1e-3f) {
                        WAcc acc;
                        float keep = 1.f - wLip - wCor;
                        for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * keep);
                        acc.add(upperLip ? B_LIP_UPPER : B_LIP_LOWER, wLip);
                        acc.add(right ? B_LIP_CORNER_R : B_LIP_CORNER_L, wCor);
                        v.sw = acc.finish();
                    }
                }
                if (rd.lidW > 0.f) {
                    float dd = fabsf(at - H.thetaEye);
                    float w = rd.lidW * (1.f - sstep(0.62f * halfSpan, 1.02f * halfSpan, dd));
                    if (w > 0.f) {
                        WAcc acc;
                        for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * (1.f - w));
                        acc.add(right ? B_EYE_R : B_EYE_L, w);
                        v.sw = acc.finish();
                    }
                }
                if (rd.browW > 0.f) {
                    float w = rd.browW * sstep(thIn - 10.f * deg, thIn + 4.f * deg, at) * (1.f - sstep(thOut - 6.f * deg, thOut + 10.f * deg, at));
                    if (w > 1e-3f) {
                        WAcc acc;
                        for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * (1.f - w));
                        acc.add(right ? B_BROW_R : B_BROW_L, w);
                        v.sw = acc.finish();
                    }
                }
            }
        }
    }
