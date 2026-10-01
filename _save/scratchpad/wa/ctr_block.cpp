// ------------------------------------------------------------------------------------------------ container stacks
// Stack height per stack by block variant: 0 import / export mix, 1 reefers (low), 2 empties (tall, by line), 3 sparse
int stackHeight(u32 h, int variant) {
    float r = hashToFloat(h);
    switch (variant) {
        case 1: return r < 0.12f ? 0 : (r < 0.42f ? 1 : (r < 0.8f ? 2 : 3));
        case 2: return r < 0.08f ? 0 : (r < 0.2f ? 2 : (r < 0.42f ? 3 : (r < 0.72f ? 4 : (r < 0.9f ? 5 : 6))));
        case 3: return r < 0.5f ? 0 : (r < 0.8f ? 1 : 2);
        default: return r < 0.08f ? 0 : (r < 0.25f ? 1 : (r < 0.52f ? 2 : (r < 0.82f ? 3 : 4)));
    }
}

// A straddle-carrier block: rows of ground slots 12.4 m apart along the row, rows 4.3 m apart (1.86 m legs lanes between
// them). Each slot holds one 40 ft stack or two 20 ft stacks of their own heights, boxes 2.5 cm apart on twistlocks and a
// few centimetres out of line. Faces get their detail from what they look onto (see CtrLod).
void genContainerBlock(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    using namespace ctr;
    int bays = (int)e.p[0], rows = (int)e.p[1];
    vec2 X = e.ax, Y = perp(e.ax);
    const float pitchX = 12.4f, pitchY = 4.3f, L40 = 12.192f, L20 = 6.058f, CW = 2.438f;
    vec2 origin = e.c - X * (bays * pitchX * 0.5f) - Y * (rows * pitchY * 0.5f);
    const int kMaxT = 7;
    const int ns = bays * 2;  // 20 ft sub-slots along a row
    std::vector<u8> cnt((size_t)ns * rows, 0), is40((size_t)bays * rows, 1);
    std::vector<float> tz((size_t)ns * rows * (kMaxT + 1), e.z);  // tier floor heights; index kMaxT = stack top
    const float p20 = e.variant == 1 ? 0.f : (e.variant == 3 ? 0.3f : 0.22f);
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < bays; i++) {
            u32 hs = hash3i(i, j, (int)e.seed);
            bool outer = j == 0 || j == rows - 1;
            bool pair = hashToFloat(hash32(hs ^ 0x2020u)) < p20;
            is40[(size_t)j * bays + i] = pair ? 0 : 1;
            for (int s = 0; s < 2; s++) {
                if (!pair && s == 1) {
                    cnt[(size_t)j * ns + i * 2 + 1] = cnt[(size_t)j * ns + i * 2];
                    continue;
                }
                int h = stackHeight(pair ? hash3i(i * 2 + s, j, (int)e.seed ^ 0x51) : hs, e.variant);
                if (outer && hashToFloat(hash32(hs ^ (0x77u + (u32)s))) < 0.1f) h = 0;  // gaps along the lanes
                cnt[(size_t)j * ns + i * 2 + s] = (u8)Min(h, kMaxT);
            }
        }
    auto tierH = [&](int s, int j, int t, bool big) {
        u32 h = hash3i((big ? s / 2 : s) * 8 + t, j, (int)e.seed ^ 0x4C);
        float hc = big ? (e.variant == 1 ? 0.9f : 0.55f) : 0.1f;
        return hashToFloat(h) < hc ? 2.896f : 2.591f;
    };
    auto T = [&](int s, int j, int t) -> float& { return tz[((size_t)j * ns + s) * (kMaxT + 1) + t]; };
    for (int j = 0; j < rows; j++)
        for (int s = 0; s < ns; s++) {
            bool big = is40[(size_t)j * bays + s / 2] != 0;
            int n = cnt[(size_t)j * ns + s];
            float z = e.z;
            for (int t = 0; t < n; t++) {
                T(s, j, t) = z;
                z += tierH(s, j, t, big) + kGap;
            }
            T(s, j, kMaxT) = n ? z - kGap : e.z;
        }
    auto topAt = [&](int s, int j) { return T(s, j, kMaxT); };
    auto sx0 = [&](int s) { return (s / 2) * pitchX + ((s & 1) ? L20 + 0.076f : 0.f); };
    auto sx1 = [&](int s) { return (s / 2) * pitchX + ((s & 1) ? L40 : L20); };
    // which line a box belongs to: reefers and mixed blocks random, empties grouped per row
    auto boxLine = [&](int s, int j, int t, int& line, int& lease) {
        u32 h = hash3i(s + t * 61, j, (int)e.seed ^ 0x77);
        line = -1;
        lease = -1;
        if (e.variant == 2 && hashToFloat(hash32(h)) < 0.78f) {
            line = (int)((hash2i(j, (int)e.seed) >> 4) % kLineCount);
            return;
        }
        if (e.variant != 1 && hashToFloat(hash32(h + 9)) < 0.24f) lease = (int)(hash32(h + 3) % (u32)kLeaseCount);
        else line = (int)(h % (u32)kLineCount);
    };
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < bays; i++) {
            bool big = is40[(size_t)j * bays + i] != 0;
            for (int s = i * 2; s < i * 2 + (big ? 1 : 2); s++) {
                int n = cnt[(size_t)j * ns + s];
                float len = big ? L40 : L20;
                int sEnd = big ? s + 1 : s;  // last sub-slot covered
                bool endBay = s == 0 || sEnd == ns - 1;
                for (int t = 0; t < n; t++) {
                    Ctr k;
                    float zb = T(s, j, t), H = tierH(s, j, t, big), zt = zb + H;
                    u32 hc = hash3i(s * 8 + t, j, (int)e.seed ^ 0x3D);
                    float jx = (hashToFloat(hc) - 0.5f) * 0.04f, jy = (hashToFloat(hash32(hc)) - 0.5f) * 0.06f;
                    vec2 c2 = origin + X * (sx0(s) + len * 0.5f + jx) + Y * (j * pitchY + CW * 0.5f + 0.9f + jy);
                    k.base = vec3(c2, zb);
                    k.ax = X;
                    k.len = len;
                    k.wid = CW;
                    k.hgt = H;
                    k.seed = hash3i(s * 16 + t, j, (int)e.seed ^ 0x5EED);
                    k.stacked = t > 0;
                    k.reefer = e.variant == 1;
                    k.doorsPlus = (hash32(k.seed) & 1u) != 0;
                    int line, lease;
                    boxLine(s, j, t, line, lease);
                    k.line = line;
                    if (line >= 0) {
                        k.col = e.variant == 1 ? vec3(0.93f, 0.93f, 0.91f) : kLines[line].color;
                        k.code = kLines[line].code;
                    } else {
                        k.col = kLeaseCol[lease];
                        k.code = kLeaseCode[lease];
                    }
                    k.col = k.col * Lerp(0.9f, 1.06f, hashToFloat(hash32(k.seed + 11)));
                    k.fade = hashToFloat(hash32(k.seed + 12)) * 0.45f + (t >= 3 ? 0.08f : 0.f);
                    float rr = hashToFloat(hash32(k.seed + 13));
                    k.rust = rr * rr * 0.95f;
                    k.grime = hashToFloat(hash32(k.seed + 14));
                    // long faces: the lanes outside the block, the alleys between rows, or above a lower row
                    for (int side = 0; side < 2; side++) {
                        int jn = side == 0 ? j - 1 : j + 1;
                        u8 lod;
                        if (jn < 0 || jn >= rows) {
                            lod = t == 0 ? CL_HERO : CL_MID;
                            if (line >= 0 && t <= 2) k.marks |= (u8)(1u << side);
                        } else {
                            float nt = topAt(s, jn);
                            for (int q = s + 1; q <= sEnd; q++) nt = Min(nt, topAt(q, jn));
                            if (nt >= zt - 0.3f) lod = (endBay && t == 0) ? CL_MID : CL_FLAT;
                            else lod = CL_FLATC;
                        }
                        k.lod[side] = lod;
                    }
                    // ends: the block ends (boulevard / quay road), the gaps between stacks, or above a lower neighbour
                    for (int en = 0; en < 2; en++) {
                        int sn = en == 0 ? s - 1 : sEnd + 1;
                        u8 lod;
                        if (sn < 0 || sn >= ns) lod = t == 0 ? CL_HERO : CL_MID;
                        else lod = topAt(sn, j) >= zt - 0.3f ? CL_FLAT : CL_FLATC;
                        k.lod[2 + en] = lod;
                    }
                    k.lod[4] = t == n - 1 ? 1 : 0;
                    drawContainer(g, k);
                }
            }
        }
    // Reefer power racks along reefer blocks
    if (e.variant == 1) {
        for (int j = 0; j <= rows; j += 2) {
            vec2 a = origin + Y * (j * pitchY + 0.4f), b = a + X * (bays * pitchX);
            beam(g, vec3(a, e.z + 3.2f), vec3(b, e.z + 3.2f), 0.4f, 0.6f, rgb(0.8f, 0.75f, 0.2f), M(MAT_METAL_PAINTED));
            for (int i = 0; i <= bays; i += 4) {
                vec2 p = a + X * (i * pitchX);
                boxY(g, vec3(p, e.z + 1.6f), X, vec3(0.15f, 0.15f, 1.6f), rgb(0.5f), M(MAT_METAL_PAINTED));
            }
        }
    }
    // collision: one box per run of equal stack tops along each row (stack tops are walkable)
    for (int j = 0; j < rows; j++) {
        int s = 0;
        while (s < ns) {
            float top = topAt(s, j);
            int k2 = s;
            while (k2 + 1 < ns && fabsf(topAt(k2 + 1, j) - top) < 0.01f) k2++;
            if (top > e.z + 0.5f) {
                float x0 = sx0(s), x1 = sx1(k2);
                vec2 c = origin + X * ((x0 + x1) * 0.5f) + Y * (j * pitchY + CW * 0.5f + 0.9f);
                collide(g, vec3(c, (e.z + top) * 0.5f), X, vec3((x1 - x0) * 0.5f, CW * 0.5f, (top - e.z) * 0.5f));
            }
            s = k2 + 1;
        }
    }
}
