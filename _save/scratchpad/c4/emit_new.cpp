// ------------------------------------------------------------------------------------------------
// Silhouette fit: cloth hangs from where it rests on the body instead of shrink-wrapping it.
//
// The torso profile is read from the skin torso grid (32 columns, horizontal rows from the crotch to the armpit): per
// row its height, the torso axis and each column's horizontal distance from the axis. A garment's hang table is the
// running maximum of that radius per column from hangTop downwards (less hangDrift per metre of drop): a T-shirt
// drops straight from the chest or the bust instead of following the waist in, a jacket from the shoulder blades, the
// seat of a pair of trousers from the glutes. Because it depends only on the skin and the garment's parameters,
// decals (pockets, seams, plackets) get exactly the displacement of the shell they sit on.

static void buildTorsoProfile(OutfitCtx& o) {
    if (o.profN) return;
    const MeshB& m = o.c.m;
    const int N = 32;
    struct Rv { float z, y, r; int k; };
    std::vector<Rv> pts;
    for (const BVert& v : m.v) {
        if (v.part != PART_TORSO || v.pc < 0.f || v.pc > 0.8001f) continue;
        int k = (int)lrintf(v.pb / kTwoPi * N) % N;
        if (k < 0) k += N;
        vec2 d(v.p.x - v.axisPt.x, v.p.y - v.axisPt.y);
        pts.push_back({v.p.z, v.axisPt.y, length(d), k});
    }
    if (pts.empty()) return;
    std::sort(pts.begin(), pts.end(), [](const Rv& a, const Rv& b) { return a.z < b.z; });
    std::vector<float> zs, ys, rs;
    std::vector<u8> have;
    for (size_t i = 0; i < pts.size();) {
        size_t j = i;
        while (j < pts.size() && pts[j].z - pts[i].z < 1e-5f) j++;
        zs.push_back(pts[i].z);
        ys.push_back(pts[i].y);
        size_t row = zs.size() - 1;
        rs.resize((row + 1) * N, 0.f);
        have.resize((row + 1) * N, 0);
        for (size_t q = i; q < j; q++) {
            rs[row * N + pts[q].k] = Max(rs[row * N + pts[q].k], pts[q].r);
            have[row * N + pts[q].k] = 1;
        }
        i = j;
    }
    // fill any missing column from its neighbours in the row
    const size_t R = zs.size();
    for (size_t j = 0; j < R; j++)
        for (int k = 0; k < N; k++) {
            if (have[j * N + k]) continue;
            for (int d = 1; d < N / 2; d++) {
                int a = (k + d) % N, b = (k - d + N) % N;
                if (have[j * N + a] || have[j * N + b]) {
                    float ra = have[j * N + a] ? rs[j * N + a] : rs[j * N + b], rb = have[j * N + b] ? rs[j * N + b] : ra;
                    rs[j * N + k] = 0.5f * (ra + rb);
                    break;
                }
            }
        }
    o.profZ = zs;
    o.profY = ys;
    o.profR = rs;
    o.profN = N;
}

// Row interval of the profile at height z (clamped).
static void profRow(const OutfitCtx& o, float z, int& j0, float& f) {
    const std::vector<float>& Z = o.profZ;
    int n = (int)Z.size();
    if (n < 2 || z <= Z[0]) { j0 = 0; f = 0.f; return; }
    if (z >= Z[n - 1]) { j0 = n - 2; f = 1.f; return; }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (Z[mid] <= z) lo = mid;
        else hi = mid;
    }
    j0 = lo;
    f = (z - Z[lo]) / Max(Z[lo + 1] - Z[lo], 1e-6f);
}

// Torso axis y at height z (profile rows; the axis is on the skin grid's ray origins).
static float profAxisY(const OutfitCtx& o, float z) {
    if (o.profZ.empty()) return 0.f;
    int j;
    float f;
    profRow(o, z, j, f);
    return Lerp(o.profY[j], o.profY[Min(j + 1, (int)o.profY.size() - 1)], f);
}

// Hang table of a garment: extra horizontal radius (m) per profile row and column.
static void hangTable(const OutfitCtx& o, const GarmentDef& g, std::vector<float>& ext) {
    const int N = o.profN, R = (int)o.profZ.size();
    ext.assign((size_t)R * N, 0.f);
    if (g.hangDrift < 0.f || !N) return;
    for (int k = 0; k < N; k++) {
        float w = g.hangWeight ? Saturate(g.hangWeight(kTwoPi * k / N)) : 1.f;
        if (w <= 0.f) continue;
        float m = -1e9f, zPrev = 0.f;
        for (int j = R - 1; j >= 0; j--) {
            float z = o.profZ[j], r = o.profR[(size_t)j * N + k];
            if (z > g.hangTop) continue;
            m = m < -1e8f ? r : Max(r, m - g.hangDrift * (zPrev - z));
            zPrev = z;
            ext[(size_t)j * N + k] = w * (m - r);
        }
    }
}

static float hangAt(const OutfitCtx& o, const std::vector<float>& ext, float z, float th) {
    const int N = o.profN;
    if (!N || ext.empty() || z > o.profZ.back() + 0.01f || z < o.profZ[0] - 0.08f) return 0.f;
    int j;
    float f;
    profRow(o, z, j, f);
    float u = th / kTwoPi * N;
    u -= floorf(u / N) * N;
    int k0 = (int)u % N, k1 = (k0 + 1) % N;
    float fu = u - floorf(u);
    int j1 = Min(j + 1, (int)o.profZ.size() - 1);
    float a = Lerp(ext[(size_t)j * N + k0], ext[(size_t)j * N + k1], fu);
    float b = Lerp(ext[(size_t)j1 * N + k0], ext[(size_t)j1 * N + k1], fu);
    return Lerp(a, b, f);
}

// Move a shell point to the garment's silhouette: torso hang (horizontal, away from the torso axis) and limb tubes
// (radial from the limb axis; a trouser leg's inner side stops `gap` short of the midplane).
static vec3 fitPoint(const OutfitCtx& o, const GarmentDef& g, const std::vector<float>& ext, const BVert& v, vec3 p) {
    if (v.part == PART_TORSO && !ext.empty()) {
        float e = hangAt(o, ext, v.bp.z, v.pb);
        if (e > 1e-5f) {
            vec2 d(p.x, p.y - profAxisY(o, v.bp.z));
            float l = length(d);
            if (l > 1e-5f) {
                d = d / l;
                p.x += d.x * e;
                p.y += d.y * e;
            }
        }
    }
    if ((v.part == PART_ARM || v.part == PART_LEG) && g.tubeR) {
        float R = g.tubeR(v);
        if (R > 0.f) {
            vec3 d = p - v.axisPt;
            float l = length(d);
            if (l > 1e-5f && l < R) {
                vec3 q = v.axisPt + d * (R / l);
                if (v.part == PART_LEG) {
                    // the inner side of the leg stops 4 mm short of the midplane (the other leg's tube)
                    float sx = v.side ? 1.f : -1.f;
                    float lim = 0.004f * o.c.D->s, dx = d.x * sx;
                    if (dx < -1e-6f) {
                        float rhoMax = (v.axisPt.x * sx - lim) * l / -dx;
                        q = rhoMax <= l ? p : v.axisPt + d * (Min(R, rhoMax) / l);
                    }
                }
                p = q;
            }
        }
    }
    return p;
}

// Adaptive refinement: split shell edges where the fold offset is under-sampled (1 level, conforming red / green
// splits so no T-junctions), at most maxNew new triangles.
static void refineShell(MeshB& gm, const GarmentDef& g, int maxNew) {
    if (!g.foldFn || g.refineTol <= 0.f) return;
    const size_t nt = gm.idx.size() / 3;
    auto fold = [&](const BVert& v) {
        float off = 0.f, cr = 0.f;
        g.foldFn(v, off, cr);
        return off;
    };
    std::vector<float> fv(gm.v.size());
    for (size_t i = 0; i < gm.v.size(); i++) fv[i] = fold(gm.v[i]);
    std::unordered_map<u64, u32> split;
    auto key = [](u32 a, u32 b) { return a < b ? ((u64)a << 32 | b) : ((u64)b << 32 | a); };
    int budget = maxNew;
    for (size_t t = 0; t < nt && budget > 0; t++)
        for (int k = 0; k < 3; k++) {
            u32 a = gm.idx[t * 3 + k], b = gm.idx[t * 3 + (k + 1) % 3];
            u64 kk = key(a, b);
            if (split.count(kk)) continue;
            const BVert &va = gm.v[a], &vb = gm.v[b];
            if (length2(va.bp - vb.bp) < 0.005f * 0.005f) continue;
            if (Max(fabsf(fv[a]), fabsf(fv[b])) < 1e-5f && fabsf(fold(lerpVert(va, vb, 0.5f))) < 1e-5f) continue;
            BVert mid = lerpVert(va, vb, 0.5f);
            float fm = fold(mid);
            if (fabsf(fm - 0.5f * (fv[a] + fv[b])) < g.refineTol) continue;
            mid.flags = va.flags & vb.flags;
            split.insert(std::make_pair(kk, gm.add(mid)));
            fv.push_back(fm);
            budget -= 2;   // each split edge adds about two triangles (one per side)
        }
    if (split.empty()) return;
    std::vector<u32> idx;
    idx.reserve(gm.idx.size() + split.size() * 6);
    for (size_t t = 0; t < nt; t++) {
        u32 v[3] = {gm.idx[t * 3], gm.idx[t * 3 + 1], gm.idx[t * 3 + 2]};
        u32 m[3];
        int ns = 0, first = -1;
        for (int k = 0; k < 3; k++) {
            auto it = split.find(key(v[k], v[(k + 1) % 3]));
            m[k] = it == split.end() ? 0xffffffffu : it->second;
            if (m[k] != 0xffffffffu) {
                ns++;
                if (first < 0) first = k;
            }
        }
        auto T = [&](u32 a, u32 b, u32 c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); };
        if (ns == 0) T(v[0], v[1], v[2]);
        else if (ns == 3) {
            T(v[0], m[0], m[2]);
            T(v[1], m[1], m[0]);
            T(v[2], m[2], m[1]);
            T(m[0], m[1], m[2]);
        } else if (ns == 1) {
            int k = first;
            u32 a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3], mm = m[k];
            T(a, mm, c);
            T(mm, b, c);
        } else {
            // two split edges: the unsplit one is k; fan from its far corner's split points
            int k = m[0] == 0xffffffffu ? 0 : (m[1] == 0xffffffffu ? 1 : 2);
            u32 a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3];
            u32 mbc = m[(k + 1) % 3], mca = m[(k + 2) % 3];
            T(c, mca, mbc);
            // quad a, b, mbc, mca: split along the shorter diagonal
            if (length2(gm.v[a].bp - gm.v[mbc].bp) < length2(gm.v[b].bp - gm.v[mca].bp)) {
                T(a, b, mbc);
                T(a, mbc, mca);
            } else {
                T(a, b, mca);
                T(b, mbc, mca);
            }
        }
    }
    gm.idx.swap(idx);
}

// Emit one garment shell into o.out. Returns false if empty.
bool emitGarment(OutfitCtx& o, const GarmentDef& g) {
    const MeshB& bm = o.c.m;
    const size_t nv = bm.v.size();
    std::vector<float> cv(nv, -1.f);
    for (size_t i = 0; i < nv; i++) {
        const BVert& v = bm.v[i];
        if (!((1u << v.part) & kSurfParts & g.parts)) continue;
        cv[i] = g.cov(v);
    }
    MeshB gm;
    std::vector<u32> map(nv, 0xffffffffu);
    std::unordered_map<u64, u32> emap;
    auto G = [&](u32 i) -> u32 {
        if (map[i] == 0xffffffffu) {
            BVert v = bm.v[i];
            v.flags = 0;
            map[i] = gm.add(v);
        }
        return map[i];
    };
    auto E = [&](u32 a, u32 b) -> u32 {
        u64 key = a < b ? ((u64)a << 32 | b) : ((u64)b << 32 | a);
        auto it = emap.find(key);
        if (it != emap.end()) return it->second;
        float t = cv[a] / (cv[a] - cv[b]);
        BVert v = lerpVert(bm.v[a], bm.v[b], Saturate(t));
        v.flags = 1;   // boundary
        u32 id = gm.add(v);
        emap.insert(std::make_pair(key, id));
        return id;
    };
    for (size_t t = 0; t + 2 < o.c.surfaceIdxEnd; t += 3) {
        u32 tri[3] = {bm.idx[t], bm.idx[t + 1], bm.idx[t + 2]};
        bool in[3] = {cv[tri[0]] > 0.f, cv[tri[1]] > 0.f, cv[tri[2]] > 0.f};
        int cnt = (int)in[0] + (int)in[1] + (int)in[2];
        if (cnt == 0) continue;
        if (cnt == 3) {
            gm.tri(G(tri[0]), G(tri[1]), G(tri[2]));
            continue;
        }
        u32 poly[4];
        int np = 0;
        for (int k = 0; k < 3; k++) {
            u32 a = tri[k], b = tri[(k + 1) % 3];
            if (in[k]) poly[np++] = G(a);
            if (in[k] != in[(k + 1) % 3]) poly[np++] = E(a, b);
        }
        for (int k = 1; k + 1 < np; k++) gm.tri(poly[0], poly[k], poly[k + 1]);
    }
    if (gm.idx.empty()) return false;
    // folds under-sampled by the skin tessellation get extra vertices
    refineShell(gm, g, 1400);
    // offsets, then the silhouette fit
    buildTorsoProfile(o);
    std::vector<float> ext;
    if (g.hangDrift >= 0.f) hangTable(o, g, ext);
    const size_t gn = gm.v.size();
    std::vector<float> off(gn);
    for (size_t i = 0; i < gn; i++) {
        BVert& v = gm.v[i];
        off[i] = g.thick + (g.extraFn ? g.extraFn(v) : 0.f);
        v.p = v.bp + v.n * off[i];
        if (g.hangDrift >= 0.f || g.tubeR) {
            v.p = fitPoint(o, g, ext, v, v.p);
            off[i] = Max(off[i], dot(v.p - v.bp, v.n));
        }
    }
    // boundary detection (half-edges without twin)
    std::unordered_map<u64, int> he;
    for (size_t t = 0; t < gm.idx.size(); t += 3)
        for (int k = 0; k < 3; k++) {
            u32 a = gm.idx[t + k], b = gm.idx[t + (k + 1) % 3];
            he[(u64)a << 32 | b]++;
        }
    std::vector<u8> isB(gn, 0);
    std::vector<std::pair<u32, u32>> bEdges;
    for (size_t t = 0; t < gm.idx.size(); t += 3)
        for (int k = 0; k < 3; k++) {
            u32 a = gm.idx[t + k], b = gm.idx[t + (k + 1) % 3];
            if (!he.count((u64)b << 32 | a)) {
                isB[a] = isB[b] = 1;
                bEdges.push_back(std::make_pair(a, b));
            }
        }
    // constrained smoothing (looseness): stays outside 90 % of the fitted offset
    if (g.smooth > 0) {
        std::vector<std::vector<u32>> adj(gn);
        for (size_t t = 0; t < gm.idx.size(); t += 3)
            for (int k = 0; k < 3; k++) {
                u32 a = gm.idx[t + k], b = gm.idx[t + (k + 1) % 3];
                if (std::find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) adj[a].push_back(b);
                if (std::find(adj[b].begin(), adj[b].end(), a) == adj[b].end()) adj[b].push_back(a);
            }
        std::vector<vec3> np(gn);
        for (int it = 0; it < g.smooth; it++) {
            for (size_t i = 0; i < gn; i++) {
                vec3 acc(0);
                int n = 0;
                for (u32 j : adj[i]) {
                    if (isB[i] && !isB[j]) continue;
                    acc += gm.v[j].p;
                    n++;
                }
                np[i] = n ? lerp(gm.v[i].p, acc / (float)n, 0.5f) : gm.v[i].p;
            }
            for (size_t i = 0; i < gn; i++) {
                BVert& v = gm.v[i];
                float minOff = off[i] * 0.9f;
                float d = dot(np[i] - v.bp, v.n);
                if (d < minOff) np[i] += v.n * (minOff - d);
                v.p = np[i];
            }
        }
    }
    // folds along the shell normal, and the crease channel
    std::vector<float> crease(gn, 0.f);
    bool anyCrease = false;
    if (g.foldFn) {
        gm.computeNormals(0, gm.idx.size());
        for (size_t i = 0; i < gn; i++) {
            BVert& v = gm.v[i];
            float fo = 0.f, cr = 0.f;
            g.foldFn(v, fo, cr);
            vec3 sn = length2(v.n) > 1e-12f ? normalize(v.n) : vec3(0, 0, 1);
            v.p += sn * fo;
            crease[i] = Saturate(cr);
            anyCrease = anyCrease || cr > 0.004f;
        }
    }
    // materials / colors
    const bool cloth = g.mat == MAT_CLOTH || g.mat == MAT_DENIM;
    for (size_t i = 0; i < gn; i++) {
        BVert& v = gm.v[i];
        v.mat = g.mat;
        v.matParam = cloth ? (g.matParam | (anyCrease ? 4u : 0u)) : 0u;
        v.col = g.colFn ? g.colFn(v, g.col) : g.col;
        v.alpha = cloth && anyCrease ? 1.f - crease[i] : 1.f;
        if (g.swapUV) {
            v.uv = vec2(v.uv.y, v.uv.x);
            v.uPer = 0.f;
        }
    }
    size_t tStart = gm.idx.size();
    gm.computeNormals(0, tStart);
    // rolled hem (along the shell normal)
    for (size_t i = 0; i < gn; i++)
        if (isB[i]) gm.v[i].p += gm.v[i].n * 0.0012f;
    // hem: a rim turned in to the skin for close-fitting edges; loose openings (sleeves, trouser legs, hanging hems)
    // get an inside facing instead, so looking into the opening shows cloth, not a funnel to the skin
    if (g.hem) {
        // outward in-plane direction of every boundary edge (away from the triangle that owns it), and the inward
        // direction per boundary vertex (average over its boundary edges)
        std::unordered_map<u64, u32> third;
        for (size_t t = 0; t < tStart; t += 3)
            for (int k = 0; k < 3; k++) {
                u32 a = gm.idx[t + k], b = gm.idx[t + (k + 1) % 3];
                third[(u64)a << 32 | b] = gm.idx[t + (k + 2) % 3];
            }
        std::vector<vec3> edgeOut(bEdges.size());
        std::unordered_map<u32, vec3> inward;
        for (size_t ei = 0; ei < bEdges.size(); ei++) {
            u32 a = bEdges[ei].first, b = bEdges[ei].second;
            vec3 nn = normalize(gm.v[a].n + gm.v[b].n);
            vec3 outDir = normalize(cross(gm.v[b].p - gm.v[a].p, nn));
            auto tIt = third.find((u64)a << 32 | b);
            if (tIt != third.end() && dot(gm.v[tIt->second].p - gm.v[a].p, outDir) > 0.f) outDir = -outDir;
            edgeOut[ei] = outDir;
            inward[a] += -outDir;
            inward[b] += -outDir;
        }
        std::unordered_map<u32, u32> inner, fac, edgeIn;
        auto gapOf = [&](u32 a) { return dot(gm.v[a].p - gm.v[a].bp, normalize(gm.v[a].n)); };
        auto I = [&](u32 a) -> u32 {
            auto it = inner.find(a);
            if (it != inner.end()) return it->second;
            BVert v = gm.v[a];
            v.p = v.bp + v.n * Max(0.0008f, off[a] * 0.25f);
            v.col = v.col * 0.7f;
            u32 id = gm.add(v);
            inner.insert(std::make_pair(a, id));
            return id;
        };
        // facing: the hem's thickness (1.5 mm in), then 12 mm up the inside of the garment
        auto Fe = [&](u32 a) -> u32 {
            auto it = edgeIn.find(a);
            if (it != edgeIn.end()) return it->second;
            BVert v = gm.v[a];
            v.p = v.p - normalize(v.n) * 0.0015f;
            v.col = v.col * 0.8f;
            u32 id = gm.add(v);
            edgeIn.insert(std::make_pair(a, id));
            return id;
        };
        auto Ff = [&](u32 a) -> u32 {
            auto it = fac.find(a);
            if (it != fac.end()) return it->second;
            BVert v = gm.v[a];
            vec3 in = inward[a];
            in = in - normalize(v.n) * dot(in, normalize(v.n));
            in = length2(in) > 1e-12f ? normalize(in) : vec3(0);
            v.p = v.p - normalize(v.n) * 0.0015f + in * 0.012f;
            v.n = -normalize(v.n);
            v.col = v.col * 0.62f;
            u32 id = gm.add(v);
            fac.insert(std::make_pair(a, id));
            return id;
        };
        for (size_t ei = 0; ei < bEdges.size(); ei++) {
            u32 a = bEdges[ei].first, b = bEdges[ei].second;
            vec3 nn = normalize(gm.v[a].n + gm.v[b].n);
            vec3 outDir = edgeOut[ei];
            bool loose = g.facing && Max(gapOf(a), gapOf(b)) > 0.006f;
            if (!loose) {
                u32 ai = I(a), bi = I(b);
                vec3 nrm = cross(gm.v[b].p - gm.v[a].p, gm.v[bi].p - gm.v[a].p);
                if (dot(nrm, outDir) < 0.f) {
                    gm.tri(a, bi, b);
                    gm.tri(a, ai, bi);
                } else {
                    gm.tri(a, b, bi);
                    gm.tri(a, bi, ai);
                }
                gm.v[ai].n = outDir;
                gm.v[bi].n = outDir;
            } else {
                // hem thickness strip (faces out of the opening), then the facing (faces the body)
                u32 ae = Fe(a), be = Fe(b), af = Ff(a), bf = Ff(b);
                vec3 nrm = cross(gm.v[b].p - gm.v[a].p, gm.v[be].p - gm.v[a].p);
                if (dot(nrm, outDir) < 0.f) {
                    gm.tri(a, be, b);
                    gm.tri(a, ae, be);
                } else {
                    gm.tri(a, b, be);
                    gm.tri(a, be, ae);
                }
                gm.v[ae].n = outDir;
                gm.v[be].n = outDir;
                vec3 inN = -nn;
                vec3 n2 = cross(gm.v[be].p - gm.v[ae].p, gm.v[bf].p - gm.v[ae].p);
                if (dot(n2, inN) >= 0.f) {
                    gm.tri(ae, be, bf);
                    gm.tri(ae, bf, af);
                } else {
                    gm.tri(ae, bf, be);
                    gm.tri(ae, af, bf);
                }
            }
        }
    }
    // hide skin and inner layers underneath (loose garments keep the skin a little further in from their edges, so
    // looking into an opening never shows a hole)
    if (g.hides) {
        float margin = g.hideMargin;
        if (g.tubeR || g.hangDrift >= 0.f) {
            float gapMax = 0.f;
            for (size_t i = 0; i < gn; i++)
                if (isB[i]) gapMax = Max(gapMax, dot(gm.v[i].p - gm.v[i].bp, normalize(gm.v[i].n)));
            margin = Max(margin, Min(gapMax * 1.6f, 0.045f));
        }
        for (size_t t = 0; t + 2 < o.c.surfaceIdxEnd; t += 3)
            if (cv[bm.idx[t]] > margin && cv[bm.idx[t + 1]] > margin && cv[bm.idx[t + 2]] > margin) o.hideBody[t / 3] = 1;
        for (auto& L : o.layers)
            for (size_t t = L.t0; t < L.t1; t++) {
                const BVert& a = o.out.v[o.out.idx[t * 3]];
                const BVert& b = o.out.v[o.out.idx[t * 3 + 1]];
                const BVert& cc = o.out.v[o.out.idx[t * 3 + 2]];
                auto covAt = [&](const BVert& v) { return g.cov(v); };
                if (covAt(a) > margin + 0.004f && covAt(b) > margin + 0.004f && covAt(cc) > margin + 0.004f) o.hideOut[t] = 1;
            }
    }
    size_t t0 = o.out.idx.size() / 3;
    o.out.append(gm);
    size_t t1 = o.out.idx.size() / 3;
    o.hideOut.resize(t1, 0);
    OutfitCtx::Layer L;
    L.cov = g.cov;
    L.margin = g.hideMargin;
    L.t0 = t0;
    L.t1 = t1;
    o.layers.push_back(L);   // later garments hide it where they cover it (decals included: pocket stitching under a shirt)
    return true;
}
