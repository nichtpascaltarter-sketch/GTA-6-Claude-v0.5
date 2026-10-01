// Clothing, shoes, hats, glasses and jewelry.
//
// Garments are offset shells extracted from the skin surface with "marching triangles" over a per-vertex coverage
// field (positive = covered). Cut lines are exact iso-lines of the field (smooth hems at any angle, independent of
// the body tessellation). The shell inherits the skin's parametrization (UVs in meters) and skin weights, is pushed
// out along the skin normal by the garment thickness, relaxed (Laplacian smoothing constrained to stay outside a
// minimum offset) to bridge concavities for loose garments, and gets a turned hem along its boundary. Skin triangles
// fully covered by an opaque garment are removed, as are inner garment layers fully covered by outer ones.
// "Decal garments" (sub-regions with a slightly larger offset and different color/material) produce pockets,
// waistbands, reflective tape, lapels, ties, seams and shoe panels at low cost.
#include "anim_internal.h"
#include <memory>
#include <unordered_map>

namespace Anim {
namespace detail {

static const u32 kSurfParts = (1u << PART_TORSO) | (1u << PART_NECK) | (1u << PART_HEAD) | (1u << PART_ARM) | (1u << PART_HAND) |
                              (1u << PART_LEG);

static inline float angLerp(float a, float b, float t) { return a + wrapAngle(b - a) * t; }

static BVert lerpVert(const BVert& a, const BVert& b, float t) {
    BVert v = a;
    v.n = normalize(lerp(a.n, b.n, t));
    // points between two surface points bulge with the surface (the chord would cut inside a curved body): the
    // quadratic arc through both points with their normals, offset t (1 - t) / 2 * (nb - na) . (pb - pa) along n
    float bulge = 0.5f * t * (1.f - t) * dot(b.n - a.n, b.bp - a.bp);
    bulge = Clamp(bulge, -0.2f * length(b.bp - a.bp), 0.2f * length(b.bp - a.bp));
    v.p = lerp(a.p, b.p, t) + v.n * bulge;
    v.bp = lerp(a.bp, b.bp, t) + v.n * bulge;
    v.t = lerp(a.t, b.t, t);
    vec2 ub = b.uv;
    if (a.uPer > 0.f && fabsf(ub.x - a.uv.x) > a.uPer * 0.5f) ub.x += ub.x < a.uv.x ? a.uPer : -a.uPer;
    v.uv = lerp(a.uv, ub, t);
    v.col = lerp(a.col, b.col, t);
    v.sw = lerpSkin(a.sw, b.sw, t);
    if (a.part == PART_HEAD) {
        // head grid: pa = azimuth (periodic, 0 at the front), pb = elevation (not periodic)
        v.pa = angLerp(a.pa, b.pa, t);
        if (v.pa < 0.f) v.pa += kTwoPi;
        if (v.pa >= kTwoPi) v.pa -= kTwoPi;
        v.pb = Lerp(a.pb, b.pb, t);
    } else {
        v.pa = Lerp(a.pa, b.pa, t);
        v.pb = (a.part == PART_TORSO || a.part == PART_NECK || a.part == PART_ARM || a.part == PART_LEG) ? angLerp(a.pb, b.pb, t) : Lerp(a.pb, b.pb, t);
        if (v.pb < 0.f) v.pb += kTwoPi;
    }
    v.pc = Lerp(a.pc, b.pc, t);
    v.axisPt = lerp(a.axisPt, b.axisPt, t);
    v.flags = a.flags & b.flags;
    return v;
}

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
        if (g.hangFade1 > -1e8f) e *= sstep(g.hangFade0, g.hangFade1, v.bp.z);
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

// Conforming one-level refinement of a triangle mesh: every edge for which want(a, b, mid, attr) returns true gets its
// midpoint `mid` (with the per-vertex attribute `attr` appended to *attrs); triangles are then split red / green
// (1, 2 or 3 split edges), so no T-junctions appear. At most maxSplits edges are split. Returns the number split.
template <class Want>
static int splitEdges(MeshB& m, std::vector<float>* attrs, int maxSplits, Want want) {
    const size_t nt = m.idx.size() / 3;
    std::unordered_map<u64, u32> split;
    auto key = [](u32 a, u32 b) { return a < b ? ((u64)a << 32 | b) : ((u64)b << 32 | a); };
    for (size_t t = 0; t < nt && (int)split.size() < maxSplits; t++)
        for (int k = 0; k < 3; k++) {
            u32 a = m.idx[t * 3 + k], b = m.idx[t * 3 + (k + 1) % 3];
            u64 kk = key(a, b);
            if (split.count(kk)) continue;
            BVert mid;
            float attr = 0.f;
            if (!want(a, b, mid, attr)) continue;
            split.insert(std::make_pair(kk, m.add(mid)));
            if (attrs) attrs->push_back(attr);
        }
    if (split.empty()) return 0;
    std::vector<u32> idx;
    idx.reserve(m.idx.size() + split.size() * 6);
    for (size_t t = 0; t < nt; t++) {
        u32 v[3] = {m.idx[t * 3], m.idx[t * 3 + 1], m.idx[t * 3 + 2]};
        u32 md[3];
        int ns = 0, first = -1;
        for (int k = 0; k < 3; k++) {
            auto it = split.find(key(v[k], v[(k + 1) % 3]));
            md[k] = it == split.end() ? 0xffffffffu : it->second;
            if (md[k] != 0xffffffffu) {
                ns++;
                if (first < 0) first = k;
            }
        }
        auto T = [&](u32 a, u32 b, u32 c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); };
        if (ns == 0) T(v[0], v[1], v[2]);
        else if (ns == 3) {
            T(v[0], md[0], md[2]);
            T(v[1], md[1], md[0]);
            T(v[2], md[2], md[1]);
            T(md[0], md[1], md[2]);
        } else if (ns == 1) {
            int k = first;
            u32 a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3], mm = md[k];
            T(a, mm, c);
            T(mm, b, c);
        } else {
            // two split edges: the unsplit one is k; a corner triangle, then the quad split along its shorter diagonal
            int k = md[0] == 0xffffffffu ? 0 : (md[1] == 0xffffffffu ? 1 : 2);
            u32 a = v[k], b = v[(k + 1) % 3], c = v[(k + 2) % 3];
            u32 mbc = md[(k + 1) % 3], mca = md[(k + 2) % 3];
            T(c, mca, mbc);
            if (length2(m.v[a].bp - m.v[mbc].bp) < length2(m.v[b].bp - m.v[mca].bp)) {
                T(a, b, mbc);
                T(a, mbc, mca);
            } else {
                T(a, b, mca);
                T(b, mbc, mca);
            }
        }
    }
    m.idx.swap(idx);
    return (int)split.size();
}

// Fold refinement: split shell edges where the fold offset is under-sampled (the offset at the midpoint differs from
// the linear interpolation by more than refineTol).
static void refineShell(MeshB& gm, const GarmentDef& g, int maxNew) {
    if (!g.foldFn || g.refineTol <= 0.f) return;
    auto fold = [&](const BVert& v) {
        float off = 0.f, cr = 0.f;
        g.foldFn(v, off, cr);
        return off;
    };
    std::vector<float> fv(gm.v.size());
    for (size_t i = 0; i < gm.v.size(); i++) fv[i] = fold(gm.v[i]);
    splitEdges(gm, &fv, maxNew / 2, [&](u32 a, u32 b, BVert& mid, float& fm) {
        const BVert &va = gm.v[a], &vb = gm.v[b];
        if (va.part != vb.part || length2(va.bp - vb.bp) < 0.005f * 0.005f) return false;
        mid = lerpVert(va, vb, 0.5f);
        fm = fold(mid);
        if (fabsf(fm - 0.5f * (fv[a] + fv[b])) < g.refineTol) return false;
        mid.flags = va.flags & vb.flags;
        return true;
    });
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
    // extraction mesh: the skin triangles in or near the coverage; where the coverage is not linear across a triangle
    // (thin straps and stitch lines, pocket corners, curved necklines) its edges are split so the cut follows the
    // coverage function instead of the skin's facets (two levels, at most a few hundred splits)
    MeshB em;
    std::vector<float> ecv;
    {
        std::vector<u32> emap(nv, 0xffffffffu);
        for (size_t t = 0; t + 2 < o.c.surfaceIdxEnd; t += 3) {
            u32 tri[3] = {bm.idx[t], bm.idx[t + 1], bm.idx[t + 2]};
            if (cv[tri[0]] <= -0.03f && cv[tri[1]] <= -0.03f && cv[tri[2]] <= -0.03f) continue;
            for (int k = 0; k < 3; k++)
                if (emap[tri[k]] == 0xffffffffu) {
                    emap[tri[k]] = em.add(bm.v[tri[k]]);
                    ecv.push_back(cv[tri[k]]);
                }
            em.tri(emap[tri[0]], emap[tri[1]], emap[tri[2]]);
        }
        const u32 partsMask = kSurfParts & g.parts;
        for (int level = 0; level < g.cutRefine; level++)
            splitEdges(em, &ecv, level ? 150 : 300, [&](u32 a, u32 b, BVert& mid, float& cm) {
                float ca = ecv[a], cb = ecv[b];
                // edges joining two parts mix their parametrizations (the coverage is continuous across them anyway)
                if (em.v[a].part != em.v[b].part) return false;
                float len = length(em.v[a].bp - em.v[b].bp);
                if (len < 0.006f) return false;
                // only edges the cut crosses or passes close to
                if ((ca > 0.f) == (cb > 0.f) && Min(fabsf(ca), fabsf(cb)) > 0.6f * len) return false;
                mid = lerpVert(em.v[a], em.v[b], 0.5f);
                mid.flags = 0;
                cm = ((1u << mid.part) & partsMask) ? g.cov(mid) : -1.f;
                return fabsf(cm - 0.5f * (ca + cb)) > 0.0012f;
            });
    }
    MeshB gm;
    std::vector<u32> map(em.v.size(), 0xffffffffu);
    std::unordered_map<u64, u32> emap;
    auto G = [&](u32 i) -> u32 {
        if (map[i] == 0xffffffffu) {
            BVert v = em.v[i];
            v.flags = 0;
            map[i] = gm.add(v);
        }
        return map[i];
    };
    auto E = [&](u32 a, u32 b) -> u32 {
        u64 key = a < b ? ((u64)a << 32 | b) : ((u64)b << 32 | a);
        auto it = emap.find(key);
        if (it != emap.end()) return it->second;
        float t = ecv[a] / (ecv[a] - ecv[b]);
        BVert v = lerpVert(em.v[a], em.v[b], Saturate(t));
        v.flags = 1;   // boundary
        u32 id = gm.add(v);
        emap.insert(std::make_pair(key, id));
        return id;
    };
    for (size_t t = 0; t + 2 < em.idx.size(); t += 3) {
        u32 tri[3] = {em.idx[t], em.idx[t + 1], em.idx[t + 2]};
        bool in[3] = {ecv[tri[0]] > 0.f, ecv[tri[1]] > 0.f, ecv[tri[2]] > 0.f};
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
            v.p = g.rimDepth > 0.f ? v.p - normalize(v.n) * g.rimDepth : v.bp + v.n * Max(0.0008f, off[a] * 0.25f);
            v.col = v.col * 0.7f;
            u32 id = gm.add(v);
            inner.insert(std::make_pair(a, id));
            return id;
        };
        // facing: from the edge, 12 mm up the inside of the garment (the rolled hem gives the edge its thickness)
        auto Fe = [&](u32 a) -> u32 {
            auto it = edgeIn.find(a);
            if (it != edgeIn.end()) return it->second;
            BVert v = gm.v[a];
            v.p = v.p - normalize(v.n) * 0.0012f;
            v.n = -normalize(v.n);
            v.col = v.col * 0.7f;
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
                // the facing (faces the body)
                u32 ae = Fe(a), be = Fe(b), af = Ff(a), bf = Ff(b);
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
            margin = Max(margin, Min(gapMax * 1.4f, 0.025f));
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

// Garment vertices keep the skin part/parametrization they came from, so coverage functions work on them too.

// ------------------------------------------------------------------------------------------------
// Reference heights and helpers for coverage functions

struct Ref {
    const BodyDims* D;
    float s;
    float zCrotch, zHip, zBeltLow, zBeltMid, zBeltHigh, zWaist, zChest, zArmpit, zKnee, zAnkle, zShinMid, zThighMid;
    float torsoLen;
    float upperArm, forearm, armLen;
    vec3 breast[2];
    float breastR;
};

static void makeRef(const BuildCtx& c, Ref& R) {
    const BodyDims& D = *c.D;
    R.D = &D;
    R.s = D.s;
    R.zCrotch = D.zCrotch;
    R.zHip = D.zHip;
    R.zBeltLow = D.zHip + 0.045f * D.s;
    R.zBeltMid = D.zHip + 0.075f * D.s;
    R.zBeltHigh = D.zWaist - 0.01f * D.s;
    R.zWaist = D.zWaist;
    R.zChest = D.J[B_CHEST].z;
    R.zArmpit = D.zArmpit;
    R.zKnee = D.J[B_CALF_L].z;
    R.zAnkle = D.J[B_FOOT_L].z;
    R.zShinMid = Lerp(R.zKnee, R.zAnkle, 0.5f);
    R.zThighMid = Lerp(D.zCrotch, R.zKnee, 0.5f);
    R.torsoLen = D.zAcromion - D.zCrotch;
    R.upperArm = D.upperArm;
    R.forearm = D.forearm;
    R.armLen = D.upperArm + D.forearm;
    float rb = (0.043f + 0.03f * D.bust) * D.s;
    R.breastR = rb;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        R.breast[sd] = vec3(sx * 0.074f * D.s, D.chestDepth * 0.62f + rb * 0.25f + rb * 0.9f, D.J[B_CHEST].z - 0.008f * D.s - rb * 0.25f);
    }
}

// The skin part a vertex belongs to (garment copies keep the skin's part in `side` high bits? no: in parts table)
static inline bool isPart(const BVert& v, u8 part) { return v.part == part; }

// Coverage building blocks (meters, positive inside)
static inline float cvMin(float a, float b) { return Min(a, b); }
static inline float cvMax(float a, float b) { return Max(a, b); }

// ------------------------------------------------------------------------------------------------
// Separate geometry helpers

static void addBoxOriented(MeshB& m, vec3 c, vec3 ax, vec3 ay, vec3 az, vec3 he, vec3 col, u8 mat, const SkinW& sw) {
    vec3 X = ax * he.x, Y = ay * he.y, Z = az * he.z;
    vec3 p[8] = {c - X - Y - Z, c + X - Y - Z, c + X + Y - Z, c - X + Y - Z, c - X - Y + Z, c + X - Y + Z, c + X + Y + Z, c - X + Y + Z};
    const int f[6][4] = {{0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}, {4, 5, 6, 7}, {3, 2, 1, 0}};
    for (int i = 0; i < 6; i++) {
        vec3 a = p[f[i][0]], b = p[f[i][1]], cc = p[f[i][2]], d = p[f[i][3]];
        vec3 n = normalize(cross(b - a, d - a));
        if (dot(n, (a + cc) * 0.5f - c) < 0.f) {
            std::swap(b, d);
            n = -n;
        }
        u32 base = (u32)m.v.size();
        vec3 q[4] = {a, b, cc, d};
        for (int k = 0; k < 4; k++) {
            BVert v;
            v.p = q[k];
            v.bp = q[k];
            v.n = n;
            v.t = normalize(b - a);
            v.uv = vec2(dot(q[k], ax) + dot(q[k], az), dot(q[k], ay));
            v.col = col;
            v.mat = mat;
            v.part = PART_ACC;
            v.sw = sw;
            m.add(v);
        }
        m.quad(base, base + 1, base + 2, base + 3);
    }
}

// Tube along a polyline (closed = loop). Radius per point.
static void addTube(MeshB& m, const std::vector<vec3>& pts, const std::vector<float>& rad, int seg, bool closed, vec3 col, u8 mat,
                    const std::vector<SkinW>& sw, vec3 upHint = vec3(0, 0, 1)) {
    int n = (int)pts.size();
    if (n < 2) return;
    std::vector<u32> prev, first;
    int count = closed ? n + 1 : n;
    for (int i = 0; i < count; i++) {
        int ii = i % n;
        vec3 p = pts[ii];
        vec3 tng = closed ? pts[(ii + 1) % n] - pts[(ii + n - 1) % n] : (ii + 1 < n ? pts[ii + 1] - p : p - pts[ii - 1]);
        tng = normalize(tng);
        vec3 a = normalize(upHint - tng * dot(upHint, tng));
        if (length2(a) < 1e-6f) a = normalize(anyPerp(tng));
        vec3 b = cross(tng, a);
        std::vector<u32> ring(seg);
        if (closed && i == n) ring = first;
        else
            for (int k = 0; k < seg; k++) {
                float th = kTwoPi * k / seg;
                vec3 d = a * cosf(th) + b * sinf(th);
                BVert v;
                v.p = p + d * rad[ii];
                v.bp = v.p;
                v.n = d;
                v.t = tng;
                v.uv = vec2(th * rad[ii], (float)i * 0.02f);
                v.col = col;
                v.mat = mat;
                v.part = PART_ACC;
                v.sw = sw[ii];
                ring[k] = m.add(v);
            }
        if (i == 0) first = ring;
        if (i > 0)
            for (int k = 0; k < seg; k++) {
                u32 a0 = prev[k], a1 = prev[(k + 1) % seg], b0 = ring[k], b1 = ring[(k + 1) % seg];
                vec3 nn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
                if (dot(nn, m.v[a0].n) >= 0.f) m.quad(a0, b0, b1, a1);
                else m.quad(a0, a1, b1, b0);
            }
        prev = ring;
    }
}

// Flat disc (button / badge base) facing n.
static void addDisc(MeshB& m, vec3 c, vec3 n, float r, int seg, float thick, vec3 col, u8 mat, const SkinW& sw) {
    vec3 a, b;
    orthoFrame(n, a, b);
    BVert cv;
    cv.p = c + n * thick;
    cv.bp = cv.p;
    cv.n = n;
    cv.t = a;
    cv.col = col;
    cv.mat = mat;
    cv.part = PART_ACC;
    cv.sw = sw;
    u32 ci = m.add(cv);
    std::vector<u32> top(seg), bot(seg);
    for (int k = 0; k < seg; k++) {
        float th = kTwoPi * k / seg;
        vec3 d = a * cosf(th) + b * sinf(th);
        BVert v = cv;
        v.p = c + d * r + n * thick;
        v.n = normalize(n + d * 0.5f);
        top[k] = m.add(v);
        v.p = c + d * r;
        v.n = d;
        bot[k] = m.add(v);
    }
    for (int k = 0; k < seg; k++) {
        int k1 = (k + 1) % seg;
        vec3 nn = cross(m.v[top[k]].p - cv.p, m.v[top[k1]].p - cv.p);
        if (dot(nn, n) >= 0.f) m.tri(ci, top[k], top[k1]);
        else m.tri(ci, top[k1], top[k]);
        vec3 d = normalize(m.v[top[k]].p - cv.p);
        vec3 qn = cross(m.v[top[k1]].p - m.v[top[k]].p, m.v[bot[k]].p - m.v[top[k]].p);
        if (dot(qn, d) >= 0.f) m.quad(top[k], top[k1], bot[k1], bot[k]);
        else m.quad(top[k], bot[k], bot[k1], top[k1]);
    }
}

// Surface point of the torso at height z and angle th (0 = front, +X at pi/2), offset along the normal.
static void torsoPoint(const BuildCtx& c, float z, float th, vec3& p, vec3& n, u32 mask = MK_TORSO) {
    const BodyDims& D = *c.D;
    vec3 o(0, Lerp(-0.012f * D.s, D.J[B_CHEST].y + 0.032f * D.s, lstep(D.zHip, D.J[B_CHEST].z, z)), z);
    vec3 dir(sinf(th), cosf(th), 0.f);
    float t = c.sdf.castOut(o, dir, mask, 0.5f * D.s);
    p = o + dir * t;
    n = normalize(c.sdf.grad(p, mask));
}

// Ring (belt, band) around the torso at height z with given width and offset.
static void addTorsoBand(const BuildCtx& c, MeshB& m, float z, float halfW, float off, float bandThick, vec3 col, u8 mat, int seg = 32) {
    const BodyDims& D = *c.D;
    std::vector<u32> rows[3];
    for (int r = 0; r < 3; r++) rows[r].resize(seg);
    for (int k = 0; k < seg; k++) {
        float th = kTwoPi * k / seg;
        for (int r = 0; r < 3; r++) {
            float zz = z + (r == 0 ? -halfW : (r == 1 ? halfW : halfW));
            vec3 p, n;
            torsoPoint(c, zz, th, p, n);
            n = normalize(vec3(n.x, n.y, 0.f));
            BVert v;
            v.bp = p;
            v.p = p + n * (off + (r == 2 ? 0.f : bandThick));
            if (r == 2) v.p = p + n * off * 0.5f + vec3(0, 0, 0.001f);
            v.n = r == 2 ? vec3(0, 0, 1) : n;
            v.t = vec3(cosf(th), -sinf(th), 0);
            v.uv = vec2(uWrap(th, kPi, 0.16f * D.s), zz);
            v.uPer = kTwoPi * 0.16f * D.s;
            v.col = col;
            v.mat = mat;
            v.part = PART_ACC;
            WAcc acc;
            float a1 = sstep(D.J[B_SPINE1].z - 0.07f * D.s, D.J[B_SPINE1].z + 0.035f * D.s, zz);
            acc.add(B_PELVIS, 1.f - a1);
            acc.add(B_SPINE1, a1);
            v.sw = acc.finish();
            rows[r][k] = m.add(v);
        }
    }
    for (int k = 0; k < seg; k++) {
        int k1 = (k + 1) % seg;
        // outer face (bottom row 0 -> top row 1), top face (1 -> 2)
        vec3 out = normalize(m.v[rows[0][k]].n);
        u32 a0 = rows[0][k], a1 = rows[0][k1], b0 = rows[1][k], b1 = rows[1][k1];
        vec3 nn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
        if (dot(nn, out) < 0.f) m.quad(a0, b0, b1, a1);
        else m.quad(a0, a1, b1, b0);
        u32 c0 = rows[2][k], c1 = rows[2][k1];
        nn = cross(m.v[c0].p - m.v[b0].p, m.v[b1].p - m.v[b0].p);
        if (dot(nn, vec3(0, 0, 1)) < 0.f) m.quad(b0, c0, c1, b1);
        else m.quad(b0, b1, c1, c0);
    }
}

// ------------------------------------------------------------------------------------------------
// Garment coverage functions (all return meters; positive = covered)

// Torso region between two torso height fractions with an optional neckline dip at the front.
static float covTorsoRange(const Ref& R, const BVert& v, float zLo, float pcHi, float vDip, float vWidth) {
    float L = R.torsoLen;
    float front = Max(0.f, cosf(v.pb));
    float dip = vDip * powf(front, 1.f / Max(vWidth, 0.05f));
    float top = (pcHi - dip - v.pc) * L;
    float bot = v.bp.z - zLo;
    return Min(top, bot);
}

// Shoulder straps (tank tops, dresses): band over the shoulder around |x| = cx.
static float covStraps(const Ref& R, const BVert& v, float cx, float halfW, float pcHi) {
    float d = halfW - fabsf(fabsf(v.bp.x) - cx);
    return Min(d, (pcHi - v.pc) * R.torsoLen);
}

// Leg openings of briefs / bikinis / swimsuits (positive above the opening line).
static float covLegOpening(const Ref& R, const BVert& v, float cutFront, float cutBack) {
    float lat = Saturate(fabsf(v.bp.x) / (0.9f * R.D->hipHalfW));
    float cut = v.bp.y > 0.f ? cutFront : cutBack;
    float zOpen = R.zCrotch - 0.035f * R.s + lat * lat * cut;
    return v.bp.z - zOpen;
}

// ------------------------------------------------------------------------------------------------
// Outfit construction

struct Palette {
    vec3 top, bottom, shoe, accent;
};

static vec3 darker(vec3 c, float k) { return c * k; }

// Blotchy floral pattern for hawaiian shirts (world-space noise on the bind pose).
static vec3 floral(const BVert& v, vec3 base, u32 seed) {
    vec3 p = v.bp * 18.f;
    float h = sinf(p.x * 1.7f + seed * 0.37f) * sinf(p.y * 1.3f + 1.1f) * sinf(p.z * 1.9f + seed * 0.11f);
    float h2 = sinf(p.x * 2.9f + 2.f) * sinf(p.z * 2.3f + seed * 0.7f);
    vec3 flower = (seed & 1) ? vec3(0.9f, 0.85f, 0.7f) : vec3(0.95f, 0.45f, 0.2f);
    vec3 leaf = vec3(0.08f, 0.32f, 0.12f);
    vec3 c = base;
    if (h > 0.35f) c = flower;
    else if (h2 > 0.55f) c = leaf;
    return c;
}

// Suit jacket coverage: torso with a V opening at the front (shirt and tie show through), long sleeves.
static float covSuitJacket(const Ref& R, const BVert& v, float hemZ, float sleeve) {
    const BodyDims& D = *R.D;
    if (v.part == PART_TORSO) {
        float cv = covTorsoRange(R, v, hemZ, 0.975f, 0.f, 0.3f);
        if (v.bp.y > 0.f) {
            float zV = R.zChest - 0.07f * R.s;
            float h = Saturate((v.bp.z - zV) / (D.zNeckFront - zV));
            float vx = Lerp(0.0f, 0.075f, h) * R.s;
            float inV = v.bp.z > zV ? (vx - fabsf(v.bp.x)) : -1.f;
            cv = Min(cv, -inV);
        }
        return cv;
    }
    if (v.part == PART_ARM) return sleeve - v.pa;
    return -1.f;
}

// ------------------------------------------------------------------------------------------------
// Fabric folds as fold lines. Each fold is a ridge along a short, slightly bowed line on the garment, in the garment's
// own parametrization (limbs: along the limb x arc length around it; torso: height x arc length), tapered to nothing at
// its ends, with sharper valleys either side: compression folds in the crook of the elbow and behind the knee, stacking
// above long cuffs and at the trouser break, drag lines from the crotch, smile lines under the seat, drape from the
// armpits, pipe folds hanging from the chest of a loose top, blousing over a waistband, denim whiskers. The folds move
// the shell at LOD0 (refineShell adds vertices where the skin tessellation cannot carry them) and write the crease
// channel (valleys plus fine-wrinkle zones), which the cloth shader keeps at every LOD.
struct FoldLine {
    u8 part;          // PART_ARM / PART_LEG / PART_TORSO
    int side;         // limbs: 0 left, 1 right, -1 both (torso folds are placed by their angle)
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
    f.side = side;
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
        const float kA = D.thigh, cuffA = legAlongAtZ(D, fs.legCuffZ), rLeg = D.rKnee * 1.25f, aCrotch = legAlongAtZ(D, D.zCrotch);
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
                addFold(*F, PART_LEG, sd, aCrotch + R(0.02f, 0.07f) * s, R(-0.85f, -0.45f), R(0.045f, 0.065f) * s, R(0.005f, 0.0065f) * s, R(0.0022f, 0.0032f) * A,
                        R(0.85f, 1.15f), 0.45f);
            for (int k = 0; k < 2; k++)
                addFold(*F, PART_LEG, sd, aCrotch + R(-0.005f, 0.035f) * s, kPi - R(0.2f, 0.6f), R(0.04f, 0.055f) * s, R(0.0045f, 0.006f) * s,
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
                    addFold(*F, PART_LEG, sd, aCrotch + R(-0.01f, 0.05f) * s, -R(0.12f, 0.6f), R(0.022f, 0.035f) * s, 0.0024f * s, 0.0009f * A,
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

// Height of the top's hem when it hangs over the trousers (untucked tops that reach below the waistband), else -1.
static float untuckedHemZ(const Ref& R, const CharacterDesc& d) {
    const float s = R.s;
    switch (d.top) {
        case TOP_TSHIRT: case TOP_POLO: case TOP_HAWAIIAN: case TOP_HIVIS: return R.zCrotch + 0.075f * s;
        case TOP_TANK: return R.zCrotch + 0.07f * s;
        case TOP_OVERSIZED: return R.zCrotch - 0.02f * s;
        case TOP_HOODIE: return R.zCrotch + 0.03f * s;
        case TOP_SUIT: return R.zCrotch - 0.03f * s;
        case TOP_BLOUSE: return R.zCrotch + 0.08f * s;
        default: return -1.f;
    }
}

// ------------------------------------------------------------------------------------------------
// Seams and stitch lines as ribbons on a garment (2 triangles per segment). A line a couple of millimetres wide cut
// from the shell as a decal needs its own vertices on every skin triangle it crosses (or vanishes between the skin's
// columns); a ribbon sampled on the garment's surface is cheaper and always there. Lines run along a limb or down the
// torso (fixed angle) or round it (fixed along / height). The LODs drop them (kParamLodDetail).

// Skin rings of one part (and side, for limbs): rows of the torso grid below the armpit, the rings of an arm or leg;
// each ring's vertices sorted by angle.
struct PartGrid {
    std::vector<float> ringA;
    std::vector<std::vector<u32>> ring;
};

static void buildPartGrid(const BuildCtx& c, u8 part, int side, PartGrid& G) {
    std::vector<std::pair<float, u32>> vs;
    for (u32 i = 0; i < (u32)c.m.v.size(); i++) {
        const BVert& v = c.m.v[i];
        if (v.part != part || v.mat != MAT_SKIN) continue;
        if (part == PART_TORSO ? (v.pc < 0.f || v.pc > 0.8001f) : (int)v.side != side) continue;
        vs.push_back(std::make_pair(v.pa, i));
    }
    std::sort(vs.begin(), vs.end(), [](const std::pair<float, u32>& a, const std::pair<float, u32>& b) { return a.first < b.first; });
    for (size_t i = 0; i < vs.size();) {
        size_t j = i;
        while (j < vs.size() && vs[j].first - vs[i].first < 1e-5f) j++;
        if (j - i >= 6) {
            std::vector<u32> r;
            for (size_t k = i; k < j; k++) r.push_back(vs[k].second);
            std::sort(r.begin(), r.end(), [&](u32 a, u32 b) { return c.m.v[a].pb < c.m.v[b].pb; });
            G.ringA.push_back(vs[i].first);
            G.ring.push_back(r);
        }
        i = j;
    }
}

static BVert sampleRing(const BuildCtx& c, const std::vector<u32>& r, float th) {
    th = th - floorf(th / kTwoPi) * kTwoPi;
    size_t n = r.size(), hi = 0;
    while (hi < n && c.m.v[r[hi]].pb < th) hi++;
    u32 a = r[(hi + n - 1) % n], b = r[hi % n];
    float ta = c.m.v[a].pb, tb = c.m.v[b].pb;
    float span = wrapAngle(tb - ta);
    if (span <= 1e-6f) span += kTwoPi;
    float t = Saturate(wrapAngle(th - ta) / span);
    if (wrapAngle(th - ta) < 0.f) t = 0.f;
    return lerpVert(c.m.v[a], c.m.v[b], t);
}

// The skin at (along / height a, angle th) of a part grid; false outside its rings.
static bool sampleGrid(const BuildCtx& c, const PartGrid& G, float a, float th, BVert& out) {
    const size_t n = G.ringA.size();
    if (n < 2 || a < G.ringA[0] || a > G.ringA[n - 1]) return false;
    size_t i = 0;
    while (i + 2 < n && G.ringA[i + 1] < a) i++;
    float f = Saturate((a - G.ringA[i]) / Max(G.ringA[i + 1] - G.ringA[i], 1e-6f));
    out = lerpVert(sampleRing(c, G.ring[i], th), sampleRing(c, G.ring[i + 1], th), f);
    return true;
}

// Point of garment g over a skin sample (offsets, fit and folds, as emitGarment places the shell) and the normal.
static vec3 garmentPointAt(OutfitCtx& o, const GarmentDef& g, const std::vector<float>& ext, const BVert& v) {
    vec3 n = normalize(v.n);
    vec3 p = v.bp + n * (g.thick + (g.extraFn ? g.extraFn(v) : 0.f));
    if (g.hangDrift >= 0.f || g.tubeR) p = fitPoint(o, g, ext, v, p);
    if (g.foldFn) {
        float fo = 0.f, cr = 0.f;
        g.foldFn(v, fo, cr);
        p += n * fo;
    }
    return p;
}

// Ribbon through skin samples on garment g: halfW wide, `lift` above the cloth; registered as a layer so garments
// emitted later hide it where they cover it.
static void addGarmentRibbon(OutfitCtx& o, const GarmentDef& g, const std::vector<BVert>& samples, float halfW, float lift, vec3 col, u8 mat,
                             u32 matParam) {
    const int n = (int)samples.size();
    if (n < 2) return;
    std::vector<float> ext;
    if (g.hangDrift >= 0.f) {
        buildTorsoProfile(o);
        hangTable(o, g, ext);
    }
    std::vector<vec3> P(n);
    for (int i = 0; i < n; i++) P[i] = garmentPointAt(o, g, ext, samples[i]);
    MeshB m;
    std::vector<u32> L(n), Rr(n);
    for (int i = 0; i < n; i++) {
        vec3 t = normalize(P[Min(i + 1, n - 1)] - P[Max(i - 1, 0)]);
        vec3 nn = normalize(samples[i].n);
        vec3 w = normalize(cross(t, nn));
        BVert v = samples[i];
        v.n = nn;
        v.t = t;
        v.col = col;
        v.mat = mat;
        v.matParam = matParam | kParamLodDetail;
        v.alpha = 1.f;
        v.flags = 0;
        v.p = P[i] + nn * lift - w * halfW;
        L[i] = m.add(v);
        v.p = P[i] + nn * lift + w * halfW;
        Rr[i] = m.add(v);
    }
    for (int i = 0; i + 1 < n; i++) {
        vec3 nn = cross(m.v[Rr[i]].p - m.v[L[i]].p, m.v[L[i + 1]].p - m.v[L[i]].p);
        if (dot(nn, m.v[L[i]].n) >= 0.f) m.quad(L[i], Rr[i], Rr[i + 1], L[i + 1]);
        else m.quad(L[i], L[i + 1], Rr[i + 1], Rr[i]);
    }
    size_t t0 = o.out.idx.size() / 3;
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    OutfitCtx::Layer Ly;
    Ly.cov = g.cov;
    Ly.margin = 0.f;
    Ly.t0 = t0;
    Ly.t1 = o.out.idx.size() / 3;
    o.layers.push_back(Ly);
}

// A seam down a limb or the torso at angle th from a0 to a1 (along / height), where the garment covers it.
static void seamAlong(OutfitCtx& o, const GarmentDef& g, u8 part, int side, float th, float a0, float a1, float halfW, vec3 col, u8 mat, u32 mp) {
    PartGrid G;
    buildPartGrid(o.c, part, side, G);
    std::vector<BVert> sm;
    float last = -1e9f;
    for (size_t i = 0; i < G.ringA.size(); i++) {
        float a = G.ringA[i];
        if (a < Min(a0, a1) || a > Max(a0, a1)) continue;
        // a sample every ~1.5 cm is plenty for a straight seam (the rings are denser round the joints)
        if (a - last < 0.015f * o.c.D->s && i + 1 < G.ringA.size() && G.ringA[i + 1] <= Max(a0, a1)) continue;
        last = a;
        BVert v;
        if (!sampleGrid(o.c, G, a, th, v) || g.cov(v) <= 0.f) {
            if (sm.size() >= 2) addGarmentRibbon(o, g, sm, halfW, 0.0006f, col, mat, mp);
            sm.clear();
            continue;
        }
        sm.push_back(v);
    }
    addGarmentRibbon(o, g, sm, halfW, 0.0006f, col, mat, mp);
}

// A line round a limb or the torso at along / height a, over the angles th0 .. th1 (th1 = th0 + 2 pi: all round).
static void seamAround(OutfitCtx& o, const GarmentDef& g, u8 part, int side, float a, float th0, float th1, float halfW, vec3 col, u8 mat,
                       u32 mp) {
    PartGrid G;
    buildPartGrid(o.c, part, side, G);
    const bool full = th1 - th0 > kTwoPi - 1e-3f;
    const int N = Max(6, (int)((th1 - th0) / kTwoPi * (part == PART_TORSO ? 40.f : 24.f)));
    std::vector<BVert> sm;
    for (int k = 0; k <= N; k++) {
        float th = Lerp(th0, th1, (float)k / N);
        BVert v;
        if (!sampleGrid(o.c, G, a, th, v) || g.cov(v) <= 0.f) {
            if (sm.size() >= 2) addGarmentRibbon(o, g, sm, halfW, 0.0006f, col, mat, mp);
            sm.clear();
            continue;
        }
        sm.push_back(v);
    }
    (void)full;
    addGarmentRibbon(o, g, sm, halfW, 0.0006f, col, mat, mp);
}

// Decal (pocket, seam, placket, band, stitching) riding on a garment: the same fit and folds as its base, `extraOff`
// further out.
static GarmentDef decalOf(const GarmentDef& base, CovFn cov, vec3 col, u8 mat, float extraOff, u32 parts, u32 matParam, int cutRefine = 1) {
    GarmentDef dg;
    dg.cutRefine = cutRefine;
    dg.rimDepth = extraOff + 0.001f;   // a hemmed decal's rim ends on the garment under it, not on the skin
    dg.facing = false;
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

// Shoulder straps as ribbons (tank and crop tops, sundresses, one-piece swimsuits): a band of half-width halfW over each
// shoulder at |x| = cx, from the garment's top edge on the chest (bodyCov) over the shoulder down to its edge on the
// back, `off` off the skin with its edges rolled down towards it. A ribbon keeps a clean edge where a coverage cut would break up on the shoulder's
// coarse skin triangles; its ends tuck over the garment's top edge.
SkinW skinWeightsAt(OutfitCtx& o, vec3 p, u32 parts) {
    const BuildCtx& c = o.c;
    const size_t nEnd = c.surfaceIdxEnd;   // (vertices of the connected surface come first)
    if (o.skinCells.empty()) {
        vec3 lo(1e9f), hi(-1e9f);
        for (size_t t = 0; t < nEnd; t++) {
            vec3 q = c.m.v[c.m.idx[t]].p;
            lo = vmin(lo, q);
            hi = vmax(hi, q);
        }
        o.skinCell = 0.03f;
        o.skinCellLo = lo - vec3(0.001f);
        for (int k = 0; k < 3; k++) o.skinCellN[k] = Max(1, (int)((hi[k] - lo[k]) / o.skinCell) + 2);
        o.skinCells.assign((size_t)o.skinCellN[0] * o.skinCellN[1] * o.skinCellN[2], {});
        std::vector<u8> seen(c.m.v.size(), 0);
        for (size_t t = 0; t < nEnd; t++) {
            u32 i = c.m.idx[t];
            if (seen[i]) continue;
            seen[i] = 1;
            vec3 q = (c.m.v[i].p - o.skinCellLo) / o.skinCell;
            int ix = Clamp((int)q.x, 0, o.skinCellN[0] - 1), iy = Clamp((int)q.y, 0, o.skinCellN[1] - 1), iz = Clamp((int)q.z, 0, o.skinCellN[2] - 1);
            o.skinCells[((size_t)iz * o.skinCellN[1] + iy) * o.skinCellN[0] + ix].push_back(i);
        }
    }
    // the 4 nearest vertices within two cells
    u32 best[4] = {0, 0, 0, 0};
    float bd[4] = {1e9f, 1e9f, 1e9f, 1e9f};
    vec3 q = (p - o.skinCellLo) / o.skinCell;
    int cx = (int)floorf(q.x), cy = (int)floorf(q.y), cz = (int)floorf(q.z);
    for (int z = cz - 2; z <= cz + 2; z++)
        for (int y = cy - 2; y <= cy + 2; y++)
            for (int x = cx - 2; x <= cx + 2; x++) {
                if (x < 0 || y < 0 || z < 0 || x >= o.skinCellN[0] || y >= o.skinCellN[1] || z >= o.skinCellN[2]) continue;
                for (u32 i : o.skinCells[((size_t)z * o.skinCellN[1] + y) * o.skinCellN[0] + x]) {
                    const BVert& v = c.m.v[i];
                    if (v.mat != MAT_SKIN || !((1u << v.part) & parts)) continue;
                    float d2 = length2(v.p - p);
                    if (d2 >= bd[3]) continue;
                    int k = 3;
                    while (k > 0 && bd[k - 1] > d2) {
                        bd[k] = bd[k - 1];
                        best[k] = best[k - 1];
                        k--;
                    }
                    bd[k] = d2;
                    best[k] = i;
                }
            }
    if (bd[0] > 1e8f) return torsoSkinWeights(*c.D, p);
    WAcc acc;
    for (int k = 0; k < 4; k++) {
        if (bd[k] > 1e8f) break;
        float w = 1.f / (bd[k] + 1e-6f);
        const SkinW& sw = c.m.v[best[k]].sw;
        for (int j = 0; j < 4; j++) acc.add(sw.b[j], sw.w[j] * w);
    }
    return acc.finish();
}

static void addShoulderStraps(OutfitCtx& o, const CovFn& bodyCov, float cx, float halfW, float off, vec3 col, u32 matParam) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    MeshB m;
    for (int sd = 0; sd < 2; sd++) {
        const float sx = sd ? 1.f : -1.f;
        // the garment's top edge under the strap, front and back: the highest covered skin point near |x| = cx; the
        // strap starts 1.5 cm below it (it lies over the edge)
        float zFront = D.zArmpit, zBack = D.zArmpit;
        {
            float bestF = -1.f, bestB = -1.f;
            for (const BVert& v : c.m.v) {
                if (v.part != PART_TORSO || fabsf(v.bp.x - sx * cx) > 0.015f * s || bodyCov(v) <= 0.f) continue;
                if (v.bp.y > 0.f) bestF = Max(bestF, v.bp.z);
                else bestB = Max(bestB, v.bp.z);
            }
            if (bestF > 0.f) zFront = bestF - 0.015f * s;
            if (bestB > 0.f) zBack = bestB - 0.015f * s;
        }
        // four parallel lines across the strap (rolled edge, edge, edge, rolled edge), each in its own plane x = const
        // round the shoulder, forward-down over the top to backward-down; the rows share the angles, so the band stays
        // flat across
        const float acr[4] = {-1.f, -0.8f, 0.8f, 1.f};
        const vec3 C0(sx * cx, D.J[B_CHEST].y + 0.005f * s, D.zArmpit - 0.03f * s);
        // (the line's points are found from outside: from a point on a 17 cm circle round the shoulder, a ray in to the
        // body's surface, so the line stays in its plane x = const, and a thin shoulder whose inside the centre misses
        // still gives a smooth line; a projection from that far would wander across the plane and fold the band)
        auto hit = [&](float phi, float xoff, vec3& p, vec3& n) {
            vec3 dir(0.f, cosf(phi), sinf(phi));
            vec3 o0 = C0 + vec3(xoff, 0.f, 0.f) + dir * (0.17f * s);
            float t = 0.f, f = c.sdf.eval(o0, MK_TORSO);
            bool found = false;
            for (int it = 0; it < 128 && t < 0.17f * s && f > 0.f; it++) {
                float tn = t + Max(f * 0.8f, 0.0008f);
                float fn = c.sdf.eval(o0 - dir * tn, MK_TORSO);
                if (fn <= 0.f) {
                    // bracketed: false position
                    float lo = t, hi = tn, flo = f, fhi = fn;
                    for (int b = 0; b < 10; b++) {
                        float mid = lo + (hi - lo) * Saturate(flo / Max(flo - fhi, 1e-12f));
                        float fm = c.sdf.eval(o0 - dir * mid, MK_TORSO);
                        if (fm > 0.f) {
                            lo = mid;
                            flo = fm;
                        } else {
                            hi = mid;
                            fhi = fm;
                        }
                    }
                    t = 0.5f * (lo + hi);
                    found = true;
                    break;
                }
                t = tn;
                f = fn;
            }
            p = found ? o0 - dir * t : c.sdf.project(o0, MK_TORSO, 10);
            n = c.sdf.grad(p, MK_TORSO);
            n = length2(n) > 1e-12f ? normalize(n) : dir;
        };
        // angle range where the centre line is above the start heights
        const int NA = 60;
        float phi0 = -1.f, phi1 = -1.f;
        for (int i = 0; i <= NA; i++) {
            float phi = Lerp(-0.7f, kPi + 0.7f, (float)i / NA);
            vec3 p, n;
            hit(phi, 0.f, p, n);
            bool above = p.z >= (phi < kHalfPi ? zFront : zBack);
            if (above && phi0 < -0.5f) phi0 = phi;
            if (above) phi1 = phi;
        }
        if (phi0 < -0.5f || phi1 <= phi0) continue;
        const int NP = 18, NL = 3 * (NP - 1) + 1;
        // each line sampled densely in its plane, then pulled taut: a strap bridges the hollows under the collarbone
        // and round the shoulder blade instead of following them (the upper envelope of the samples seen from the
        // line's centre, by relaxing every sample out to the chord between its neighbours)
        auto traceLine = [&](float xoff, std::vector<vec3>& P, std::vector<vec3>& N) {
            P.resize(NL);
            N.resize(NL);
            std::vector<float> r(NL), ph(NL);
            const vec3 Ck = C0 + vec3(xoff, 0.f, 0.f);
            for (int j = 0; j < NL; j++) {
                ph[j] = Lerp(phi0, phi1, (float)j / (NL - 1));
                vec3 p, n;
                hit(ph[j], xoff, p, n);
                r[j] = sqrtf(Sq(p.y - Ck.y) + Sq(p.z - Ck.z));
                N[j] = n;
            }
            for (int it = 0; it < 48; it++) {
                bool moved = false;
                for (int j = 1; j + 1 < NL; j++) {
                    vec2 a(r[j - 1] * cosf(ph[j - 1]), r[j - 1] * sinf(ph[j - 1])), b(r[j + 1] * cosf(ph[j + 1]), r[j + 1] * sinf(ph[j + 1]));
                    vec2 e = b - a, dir(cosf(ph[j]), sinf(ph[j]));
                    float den = dir.x * e.y - dir.y * e.x;
                    if (fabsf(den) < 1e-9f) continue;
                    float t = (a.x * e.y - a.y * e.x) / den;
                    if (t > r[j] + 1e-5f) {
                        r[j] = t;
                        moved = true;
                    }
                }
                if (!moved) break;
            }
            for (int j = 0; j < NL; j++) P[j] = Ck + vec3(0.f, r[j] * cosf(ph[j]), r[j] * sinf(ph[j]));
            // normals: out of the taut curve in its plane, with the skin's sideways lean
            for (int j = 0; j < NL; j++) {
                vec3 tg = P[Min(j + 1, NL - 1)] - P[Max(j - 1, 0)];
                vec3 inPlane = normalize(vec3(0.f, tg.z, -tg.y));
                if (dot(inPlane, P[j] - Ck) < 0.f) inPlane = -inPlane;
                N[j] = normalize(inPlane + vec3(N[j].x, 0.f, 0.f));
            }
        };
        std::vector<vec3> LP[4], LN[4], CP, CN;
        traceLine(0.f, CP, CN);
        for (int k = 0; k < 4; k++) traceLine(sx * acr[k] * halfW, LP[k], LN[k]);
        std::vector<u32> rows[4];
        float along = 0.f;
        for (int i = 0; i < NP; i++) {
            const int j = i * 3;
            const float phi = Lerp(phi0, phi1, (float)j / (NL - 1));
            if (i) along += length(CP[j] - CP[j - 3]);
            for (int k = 0; k < 4; k++) {
                const vec3 q = LP[k][j], qn = LN[k][j];
                BVert v;
                v.bp = q;
                v.p = q + qn * (k == 0 || k == 3 ? off * 0.35f : off);
                v.n = qn;
                v.t = normalize(vec3(0.f, -sinf(phi), cosf(phi)));
                v.uv = vec2(acr[k] * halfW, along);
                v.col = k == 0 || k == 3 ? col * 0.85f : col;
                v.mat = MAT_CLOTH;
                v.matParam = matParam;
                v.part = PART_GARMENT;
                v.side = (u8)sd;
                v.sw = skinWeightsAt(o, q, (1u << PART_TORSO) | (1u << PART_ARM) | (1u << PART_NECK));
                v.layer = off;
                rows[k].push_back(m.add(v));
            }
        }
        for (int i = 0; i + 1 < NP; i++)
            for (int k = 0; k + 1 < 4; k++) {
                u32 a0 = rows[k][i], a1 = rows[k + 1][i], b0 = rows[k][i + 1], b1 = rows[k + 1][i + 1];
                vec3 nn = cross(m.v[a1].p - m.v[a0].p, m.v[b0].p - m.v[a0].p);
                if (dot(nn, m.v[a0].n + m.v[a1].n) >= 0.f) m.quad(a0, a1, b1, b0);
                else m.quad(a0, b0, b1, a1);
            }
    }
    m.computeNormals(0, m.idx.size());
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// Folded shirt collar round the neck base, `off` out from the skin: a stand and a fall resting on the garment, points
// either side of the front opening (tipAt: their angle from the front, tipAmt: how long they are).
static void addCollar(OutfitCtx& o, float off, vec3 ccol, u32 matParam, float frontDrop, float tipAt, float tipAmt, bool openFront, float height,
                      float fallOut = 0.f) {
    BuildCtx& c = o.c;
    const float s = c.D->s;
    const std::vector<u32>& ring = c.torsoTop;
    int n = (int)ring.size();
    std::vector<u32> r0(n), r1(n), r2(n);
    vec3 cen(0);
    for (u32 vi : ring) cen += c.m.v[vi].p;
    cen /= (float)n;
    MeshB cm;
    for (int k = 0; k < n; k++) {
        const BVert& bv = c.m.v[ring[k]];
        vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
        float front = Max(0.f, cosf(bv.pb));
        float h = height - 0.012f * s * front * frontDrop;
        vec3 base = bv.p + radial * (off + 0.001f);
        vec3 upv(0, 0, 1);
        vec3 p0 = base - upv * 0.004f * s;
        vec3 p1 = base + upv * h + radial * 0.004f * s;
        vec3 p2 = base - upv * (0.006f * s) + radial * (0.008f * s + 0.005f * front * s + fallOut);   // the fall rests on the garment
        float thw = wrapAngle(bv.pb);
        float tip = bump(fabsf(thw), tipAt, 0.13f) * tipAmt;
        p2 += (-upv * 0.024f * s + radial * 0.004f * s) * tip;
        BVert v = bv;
        v.part = PART_ACC;
        v.mat = MAT_CLOTH;
        v.matParam = matParam;
        v.col = ccol;
        v.bp = bv.p;
        v.flags = 0;
        v.p = p0; v.n = radial; r0[k] = cm.add(v);
        v.p = p1; v.n = normalize(radial + upv); r1[k] = cm.add(v);
        v.p = p2; v.n = normalize(radial - upv * 0.3f); r2[k] = cm.add(v);
    }
    for (int k = 0; k < n; k++) {
        int k1 = (k + 1) % n;
        if (openFront && (k == 0 || k1 == 0)) continue;   // the front opening
        vec3 rad = normalize(cm.v[r1[k]].n);
        auto q = [&](u32 a, u32 b, u32 c2, u32 d2, vec3 f) {
            vec3 nn = cross(cm.v[b].p - cm.v[a].p, cm.v[d2].p - cm.v[a].p);
            if (dot(nn, f) >= 0.f) cm.quad(a, b, c2, d2);
            else cm.quad(a, d2, c2, b);
        };
        q(r0[k], r0[k1], r1[k1], r1[k], -rad);   // stand (inner face, visible at the opening)
        q(r1[k], r1[k1], r2[k1], r2[k], rad);    // fall (outer face)
    }
    cm.computeNormals(0, cm.idx.size());
    o.out.append(cm);
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
        case TOP_TANK: tank = true; loose = 0.003f; hemZ = R.zCrotch + 0.07f * s; drift = 0.16f; foldAmp = 0.4f; hang = 0.f; break;
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
    // tank tops: scoop depths front / back, the height where the straps join and the armhole's bottom
    const float zScoopF = D.zNeckFront - 0.075f * s, zScoopB = D.zNeckBack - 0.1f * s, zStrapJ = D.zNeckFront - 0.025f * s, zArmHole = D.zArmpit - 0.02f * s;
    g.cov = [=, &R](const BVert& v) -> float {
        if (v.part == PART_TORSO || v.part == PART_NECK) {
            if (v.part == PART_NECK) return -1.f;
            float cv = covTorsoRange(R, v, hz, np, vd, vw);
            if (tk) {
                // the body's top edge: a U scoop at the front (and a shallower one at the back) rising to where the
                // straps join, then dropping into a deep armhole 2 cm under the armpit (where the arm hanging at the side
                // folds in); the straps are ribbons (addShoulderStraps)
                // (heights, not the grid's length fraction: over the shoulders the skin grid's rows are rays from the
                // armpit, so a cut by row would come out scalloped)
                // (across the panel by |x|: flat under the strap, which sits at |x| = 0.105 s, see addShoulderStraps)
                float ax = fabsf(v.bp.x), cxs = 0.105f * s;
                float zCentre = v.bp.y >= 0.f ? zScoopF : zScoopB;
                float zTop = ax <= cxs ? Lerp(zCentre, zStrapJ, sstep(0.012f * s, cxs - 0.02f * s, ax))
                                       : Lerp(zStrapJ, zArmHole, sstep(cxs + 0.022f * s, cxs + 0.07f * s, ax));
                // (the armhole reaches its bottom at the side whatever the torso's width)
                float th = fabsf(wrapAngle(v.pb)), a = th <= kHalfPi ? th : kPi - th;
                zTop = Min(zTop, Lerp(zStrapJ + 0.1f * s, zArmHole, sstep(1.f, 1.38f, a)));
                cv = Min(cv, zTop - v.bp.z);
            }
            return cv;
        }
        if (v.part == PART_ARM) return tk ? -1.f : sl - v.pa;
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
    o.torsoOuter = std::make_shared<GarmentDef>(g);
    if (tank)
        addShoulderStraps(o, g.cov, 0.105f * s, (top == TOP_CROP ? 0.018f : (d.gender == FEMALE ? 0.019f : 0.023f)) * s, g.thick + loose + 0.0012f, col,
                          g.matParam);
    // ---- details
    const float off = g.thick + loose;
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, u32 mp = 0xffffffffu) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, mp == 0xffffffffu ? g.matParam : mp));
    };
    // curved stitch lines (straight ones along / round a part are ribbons, seamAlong / seamAround): a thin decal whose
    // cut is refined twice, so the line is found between the skin's vertices
    auto line = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts) {
        GarmentDef dg = decalOf(g, cov, dcol, mat, extraOff, parts, g.matParam | kParamLodDetail, 2);
        emitGarment(o, dg);
    };
    if (collar) addCollar(o, off, top == TOP_SUIT ? shirtCol : col, top == TOP_POLO ? 3u : 1u, top == TOP_HAWAIIAN ? 1.f : 0.4f,
                          top == TOP_POLO ? 0.36f : 0.3f, top == TOP_POLO ? 0.55f : 1.f, top != TOP_POLICE && top != TOP_MEDIC, 0.03f * s);
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
        for (int sd = 0; sd < 2; sd++) seamAlong(o, g, PART_TORSO, -1, sd ? kHalfPi : 1.5f * kPi, hz, zAp, 0.0012f * s, seamCol, g.mat, g.matParam);
        line([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.z < zAp) return -1.f;
            // along the ridge of the shoulder (y ~ torso axis) from the neck to the acromion
            float ax = fabsf(v.bp.x);
            float t = Saturate((ax - 0.05f * s) / Max(shW - 0.05f * s, 0.01f));
            float yRidge = D.J[B_CHEST].y + 0.005f * s;
            float onTop = v.bp.z - Lerp(zNk + 0.01f * s, D.zAcromion - 0.005f * s, t);
            return Min(Min(0.0016f * s - fabsf(v.bp.y - yRidge), 0.03f * s + onTop), baseCov(v));
        }, seamCol, g.mat, 0.0007f, 1u << PART_TORSO);
        if (sleeve > 0.08f * s)
            for (int sd = 0; sd < 2; sd++) seamAround(o, g, PART_ARM, sd, 0.055f * s, 0.f, kTwoPi, 0.0012f * s, seamCol, g.mat, g.matParam);
    }
    if (top == TOP_TSHIRT || top == TOP_OVERSIZED || top == TOP_HIVIS || top == TOP_POLO || top == TOP_HOODIE) {
        // double-needle stitching above the sleeve hems and round the bottom hem (tees); rib bands on polo sleeves
        vec3 stitch = darker(col, 0.78f);
        if (sleeve > 0.08f * s && top != TOP_HOODIE) {
            if (top == TOP_POLO)
                decal([=](const BVert& v) { return v.part == PART_ARM ? Min(0.012f * s - fabsf(v.pa - (sl - 0.012f * s)), sl - v.pa) : -1.f; },
                      darker(col, 0.93f), MAT_CLOTH, 0.0011f, 1u << PART_ARM, 3u);
            else
                for (int k = 0; k < 2; k++)
                    for (int sd = 0; sd < 2; sd++)
                        seamAround(o, g, PART_ARM, sd, sl - (0.017f + 0.0055f * k) * s, 0.f, kTwoPi, 0.0006f * s, stitch, MAT_CLOTH, g.matParam);
        }
        if (top != TOP_HOODIE && top != TOP_POLO)
            for (int k = 0; k < 2; k++)
                seamAround(o, g, PART_TORSO, -1, hz + (0.018f + 0.0055f * k) * s, 0.f, kTwoPi, 0.0006f * s, stitch, MAT_CLOTH, g.matParam);
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
            line([=](const BVert& v) {
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
        line([=](const BVert& v) { return v.part == PART_ARM ? 0.0012f * s - fabsf(v.pa - (sl - 0.022f * s)) : -1.f; }, darker(col, 0.72f), MAT_CLOTH,
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
            gapNeck = 0.05f * s; zV = R.zChest - 0.1f * s; gapChest = buttoned ? 0.002f * s : 0.04f * s; gapHem = buttoned ? 0.003f * s : 0.07f * s;
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
                hemZ = R.zHip + 0.03f * s; gapNeck = 0.05f * s; zV = R.zChest - 0.07f * s; gapChest = 0.002f * s; gapHem = 0.002f * s;
                closedBelowV = true; nButtons = 5; loose = 0.003f;
            }
            break;
        case OUT_BLAZER: {
            hemZ = R.zCrotch - 0.04f * s; lapels = true; loose = 0.005f;
            bool buttoned = rng.chance(0.35f);
            gapNeck = 0.06f * s; zV = R.zWaist + 0.02f * s; gapChest = buttoned ? 0.002f * s : 0.07f * s; gapHem = buttoned ? 0.03f * s : 0.1f * s;
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
    // clear the top with the shell (and the bottoms' waistband and belt round the waist only: over the chest and the
    // shoulders that clearance would float the layer off the body), the sleeves with the tubes
    const float puffExtra = puffer ? 0.004f : 0.f;
    const float clearTop = o.topTorsoOff + 0.005f + puffExtra, clearBot = Max(o.topTorsoOff, o.botTorsoOff) + 0.005f + puffExtra;
    const float zBand = o.botTopZ > 0.f ? o.botTopZ : R.zWaist;
    const float ls = loose, zc = R.zCrotch, zw = R.zWaist;
    // (a puffer's fill thins out over the shoulders: the yoke is flat, so the narrow shoulder of a gilet does not
    // stand off the body)
    const float zYoke = D.zArmpit;
    g.extraFn = [=](const BVert& v) -> float {
        float e = ls;
        if (v.part == PART_TORSO) {
            float clearT = Lerp(clearBot, clearTop, sstep(zBand + 0.03f * s, zBand + 0.12f * s, v.bp.z));
            e = Max(clearT, ls * (0.6f + 0.8f * sstep(zw + 0.1f, zc, v.bp.z)));
            if (puffExtra > 0.f) e -= (puffExtra + 0.004f) * sstep(zYoke, zYoke + 0.07f * s, v.bp.z);
            if (noSleeve) {
                // a vest's armholes are bound flat: the fill thins towards them, so the arm hanging beside the body
                // does not press into a thick edge
                float dHole = Max(0.125f * s - fabsf(v.bp.x), zArmhole - v.bp.z);
                e *= Lerp(0.35f, 1.f, sstep(0.f, 0.04f * s, dHole));
            }
        } else if (v.part == PART_ARM)
            e = ls + 0.004f;
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
        const float z0 = hemZ, pitch = 0.07f * s, zYk = D.zArmpit;
        g.extraFn = [=](const BVert& v) {
            float e = be(v);
            if (v.part == PART_TORSO) {
                float ph = (v.bp.z - z0) / pitch;
                float f = ph - floorf(ph);
                e += 0.007f * s * sqrtf(Max(0.f, sinf(kPi * f))) * (1.f - 0.7f * sstep(zYk, zYk + 0.07f * s, v.bp.z));
            }
            return e;
        };
    }
    g.smooth = 2;
    emitGarment(o, g);
    o.outerTorsoOff = g.thick + clearBot;
    o.torsoOuter = std::make_shared<GarmentDef>(g);
    // ---- details
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, u32 mp) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, mp));
    };
    auto line = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, g.matParam | kParamLodDetail, 2));
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
        for (float z = hemZ + 0.07f * s; z < D.zArmpit; z += 0.07f * s)
            seamAround(o, g, PART_TORSO, -1, z, 0.f, kTwoPi, 0.001f * s, darker(col, 0.7f), MAT_CLOTH, g.matParam);
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
    const float offC = clearTop + g.thick;
    if (kind == OUT_OVERSHIRT || (kind == OUT_JACKET && !zip))
        addCollar(o, offC, darker(col, 0.97f), g.matParam, 0.8f, 0.42f, 1.1f, true, 0.034f * s, 0.006f * s);
    else if (kind == OUT_JACKET && zip)
        addCollar(o, offC, darker(col, 0.9f), 3u, 0.2f, 0.4f, 0.f, true, 0.028f * s);
    else if (kind == OUT_ZIPHOODIE)
        buildHood(o, g, col);
    if (sleeves && kind != OUT_VEST) {
        // seam round the sleeve root and down the outside of the sleeve
        for (int sd = 0; sd < 2; sd++) seamAround(o, g, PART_ARM, sd, 0.06f * s, 0.f, kTwoPi, 0.0012f * s, darker(col, 0.8f), g.mat, g.matParam);
    }
}

static void buildBottomGarments(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const int bot = d.bottom;
    vec3 col = d.bottomColor;
    Rng rng(hash32(d.seed * 13u + 5u));
    GarmentDef g;
    g.parts = (1u << PART_TORSO) | (1u << PART_LEG);
    g.col = col;
    g.thick = 0.0035f;
    float zTop = R.zBeltMid, zCuff = R.zAnkle + 0.03f * s, loose = 0.006f, flare = 0.f;
    bool belt = false, backPockets = false, sidePockets = false, skirt = false, briefs = false, dutyBelt = false;
    switch (bot) {
        case BOT_JEANS: g.mat = MAT_DENIM; zTop = R.zBeltMid; loose = 0.006f; backPockets = true; belt = rng.chance(0.5f); break;
        case BOT_BAGGY: g.mat = MAT_DENIM; zTop = R.zBeltLow - 0.02f * s; zCuff = R.zAnkle - 0.005f * s; loose = 0.018f; flare = 0.02f;
            backPockets = true; g.smooth = 3; break;
        case BOT_SHORTS: zCuff = R.zKnee + 0.08f * s; loose = 0.012f; flare = 0.012f; g.smooth = 2; break;
        case BOT_CARGO: zCuff = R.zKnee - 0.01f * s; loose = 0.014f; flare = 0.014f; sidePockets = true; g.smooth = 2; break;
        case BOT_SLACKS: zTop = R.zBeltHigh; loose = 0.008f; flare = 0.004f; belt = true; break;
        case BOT_POLICE: zTop = R.zBeltHigh; loose = 0.008f; flare = 0.004f; dutyBelt = true; break;
        case BOT_WORK: zTop = R.zBeltMid; loose = 0.012f; flare = 0.006f; sidePockets = true; belt = rng.chance(0.6f); g.smooth = 2; break;
        case BOT_LEGGINGS: zTop = R.zBeltHigh; zCuff = R.zAnkle + 0.05f * s; loose = 0.0f; g.thick = 0.0015f; g.smooth = 0; break;
        case BOT_HOTPANTS: zCuff = D.zCrotch - 0.045f * s; loose = 0.003f; break;
        case BOT_TRUNKS: zCuff = R.zThighMid + 0.02f * s; loose = 0.012f; flare = 0.014f; g.smooth = 2; break;
        case BOT_SKIRT: skirt = true; break;
        case BOT_BIKINI: briefs = true; break;
        default: break;
    }
    // weave (MAT_CLOTH; denim has its own twill): twill trousers, shorts and work wear, woven skirts and swim trunks
    switch (bot) {
        case BOT_SHORTS: case BOT_CARGO: case BOT_SLACKS: case BOT_POLICE: case BOT_WORK: case BOT_HOTPANTS: g.matParam = 2; break;
        case BOT_SKIRT: case BOT_TRUNKS: g.matParam = 1; break;
        default: break;
    }
    if (d.top == TOP_SUNDRESS) skirt = true;
    if (d.top == TOP_ONEPIECE) briefs = true;
    if (briefs) {
        GarmentDef b;
        b.parts = (1u << PART_TORSO) | (1u << PART_LEG);
        b.col = d.top == TOP_ONEPIECE ? d.topColor : col;
        b.thick = 0.0018f;
        b.smooth = 0;
        b.hideMargin = 0.006f;
        float zt = R.zHip + (d.top == TOP_ONEPIECE ? 0.3f : 0.035f) * s;
        bool one = d.top == TOP_ONEPIECE;
        b.cov = [=, &R](const BVert& v) -> float {
            if (v.part != PART_TORSO && v.part != PART_LEG) return -1.f;
            float open = covLegOpening(R, v, 0.14f * R.s, 0.1f * R.s);
            float topc = one ? covTorsoRange(R, v, -10.f, 0.9f, 0.08f, 0.4f) : (zt - v.bp.z - 0.03f * R.s * Sq(Saturate(fabsf(v.bp.x) / (R.D->hipHalfW))));
            if (one) {
                // open back scoop (the straps are ribbons)
                float back = (v.bp.y < 0.f) ? (R.zWaist + 0.02f * R.s - v.bp.z) : 1.f;
                topc = Min(topc, back);
            }
            return Min(open, topc);
        };
        emitGarment(o, b);
        if (one) addShoulderStraps(o, b.cov, 0.085f * s, 0.016f * s, b.thick + 0.001f, d.topColor * 0.95f, 0u);
        if (!skirt && bot == BOT_BIKINI && d.top != TOP_ONEPIECE) return;
    }
    if (skirt) {
        // skirt / dress: fitted part to the hip from the shell, then a flared cone around both legs. The cut picks
        // the flare, the length and the hem's flutes: pencil (straight, knee), A-line (knee or above), full (wide,
        // fluted), midi (mid-calf, fluted), pleated (knife pleats, above the knee); sundresses are A-line or full.
        vec3 scol = d.top == TOP_SUNDRESS ? d.topColor : col;
        float zS = d.top == TOP_SUNDRESS ? R.zWaist + 0.03f * s : R.zBeltHigh;
        enum { SK_PENCIL, SK_ALINE, SK_FULL, SK_MIDI, SK_PLEATED };
        int cut;
        {
            float u = rng.f();
            if (d.top == TOP_SUNDRESS) cut = u < 0.45f ? SK_ALINE : (u < 0.8f ? SK_FULL : SK_MIDI);
            else if (d.role == 3) cut = u < 0.6f ? SK_PENCIL : (u < 0.85f ? SK_ALINE : SK_MIDI);
            else cut = u < 0.15f ? SK_PENCIL : (u < 0.45f ? SK_ALINE : (u < 0.65f ? SK_FULL : (u < 0.85f ? SK_MIDI : SK_PLEATED)));
        }
        float hemZ, flareK, fluteA = 0.f;
        int nFlutes = 0;
        bool pleats = false;
        switch (cut) {
            case SK_PENCIL: hemZ = R.zKnee + rng.range(-0.02f, 0.04f) * s; flareK = 0.035f; break;
            case SK_FULL: hemZ = R.zKnee + rng.range(-0.02f, 0.06f) * s; flareK = 0.27f; nFlutes = 9 + (int)(rng.f() * 3.f); fluteA = 0.013f * s; break;
            case SK_MIDI: hemZ = Lerp(R.zKnee, R.zAnkle, rng.range(0.35f, 0.5f)); flareK = 0.17f; nFlutes = 8 + (int)(rng.f() * 3.f); fluteA = 0.011f * s; break;
            case SK_PLEATED: hemZ = R.zKnee + rng.range(0.06f, 0.12f) * s; flareK = 0.12f; nFlutes = 20; fluteA = 0.006f * s; pleats = true; break;
            default: hemZ = R.zKnee + (rng.chance(0.5f) ? 0.1f : -0.03f) * s; flareK = 0.14f; nFlutes = 6; fluteA = 0.006f * s; break;
        }
        GarmentDef top;
        top.parts = 1u << PART_TORSO;
        top.matParam = 1;   // woven
        top.col = scol;
        top.thick = 0.003f;
        top.smooth = 1;
        top.hem = false;
        float zc = R.zCrotch + 0.03f * s;
        top.cov = [=](const BVert& v) -> float {
            if (v.part != PART_TORSO) return -1.f;
            return Min(zS - v.bp.z, v.bp.z - zc + 0.04f);
        };
        top.extraFn = [](const BVert&) { return 0.004f; };
        top.hangDrift = 0.1f;   // the seat and hips: fabric drops from the widest point instead of following the waist in
        top.hangTop = D.zHip + 0.03f * s;
        emitGarment(o, top);
        // an untucked top over the skirt's waist clears it
        o.botTopZ = zS;
        o.botTorsoOff = top.thick + 0.004f + 0.004f;
        // cone
        MeshB sk;
        const int NS = nFlutes >= 16 ? 60 : (nFlutes > 0 ? 44 : 32), NR = cut == SK_MIDI ? 9 : 7;
        std::vector<std::vector<u32>> rows(NR);
        float zStart = zc;
        std::vector<vec3> p0s(NS), n0s(NS);
        for (int k = 0; k < NS; k++) {
            // the cone starts on the fitted part as it hangs (not on the skin), so there is no step at the hips
            float th = kTwoPi * k / NS;
            p0s[k] = garmentTorsoPoint(o, top, zStart, th, n0s[k]) - normalize(vec3(sinf(th), cosf(th) * 0.8f, 0.f)) * (0.006f * s);
        }
        const float phase = rng.range(0.f, kTwoPi), zKnee = R.zKnee;
        const float tRamp = Saturate(0.05f * s / Max(zStart - hemZ, 0.1f));
        std::vector<float> creaseV;
        for (int r = 0; r < NR; r++) {
            float t = (float)r / (NR - 1);
            float z = Lerp(zStart, hemZ, t);
            rows[r].resize(NS);
            for (int k = 0; k < NS; k++) {
                float th = kTwoPi * k / NS;
                vec3 radial = normalize(vec3(sinf(th), cosf(th) * 0.8f, 0.f));
                // flutes / pleats grow towards the hem (knife pleats: a sawtooth, flutes: a soft wave)
                float wv = 0.f;
                if (nFlutes > 0) {
                    float ph = th * nFlutes + phase;
                    wv = pleats ? (fabsf(fmodf(ph / kTwoPi + 100.f, 1.f) - 0.5f) * 4.f - 1.f) : sinf(ph);
                }
                float fl = fluteA * wv * (pleats ? sstep(0.f, 0.3f, t) : t * t);
                vec3 p = p0s[k] + radial * (0.007f * s + (zStart - z) * flareK + fl);
                p.z = z;
                BVert v;
                v.p = p;
                v.bp = p;
                v.n = radial;
                v.t = vec3(cosf(th), -sinf(th), 0);
                v.uv = vec2(uWrap(th, kPi, 0.18f * s), z);
                v.uPer = kTwoPi * 0.18f * s;
                v.col = scol;
                v.mat = MAT_CLOTH;
                v.matParam = 1 | 4u;
                v.part = PART_GARMENT;
                // stride: below the fitted part (the cone starts under the hip joints) each half of the skirt rides on
                // its own thigh almost rigidly, so a knee swinging forward carries the cloth in front of it and a seated
                // lap keeps the skirt lying over the thighs instead of cutting through them (a partial thigh weight
                // would rotate the cloth only part of the way); the back lags a little and the centre panels split
                // between both thighs (taut between the knees in a long stride); below the knee the calves take a
                // share where the hem reaches the knee (the hem then keeps round the knee as it bends: a seated skirt
                // spreads flat over the knees instead of standing out as a ring). The first 5 cm blend in from the
                // fitted part's own weights, so the two stay joined.
                WAcc acc;
                float side = sinf(th);
                float wr = sstep(-0.25f, 0.25f, side), wl = 1.f - wr;
                float legW = Lerp(0.82f, 0.96f, sstep(-0.6f, 0.3f, cosf(th)));
                float calf = 0.65f * sstep(zKnee + 0.08f * s, zKnee - 0.02f * s, z) * sstep(zKnee + 0.07f * s, zKnee + 0.02f * s, hemZ);
                acc.add(B_PELVIS, 1.f - legW);
                acc.add(B_THIGH_R, legW * wr * (1.f - calf));
                acc.add(B_THIGH_L, legW * wl * (1.f - calf));
                acc.add(B_CALF_R, legW * wr * calf);
                acc.add(B_CALF_L, legW * wl * calf);
                v.sw = lerpSkin(torsoSkinWeights(D, p0s[k]), acc.finish(), sstep(0.f, tRamp, t));
                // crease channel: the inside of each flute / pleat
                float cr = nFlutes > 0 ? Saturate((0.5f - 0.5f * wv) * (pleats ? 0.8f : 0.6f) * sstep(0.f, 0.5f, t)) : 0.f;
                v.alpha = 1.f - cr;
                rows[r][k] = sk.add(v);
            }
        }
        for (int r = 0; r + 1 < NR; r++)
            for (int k = 0; k < NS; k++) {
                int k1 = (k + 1) % NS;
                u32 a0 = rows[r][k], a1 = rows[r][k1], b0 = rows[r + 1][k], b1 = rows[r + 1][k1];
                vec3 nn = cross(sk.v[a1].p - sk.v[a0].p, sk.v[b0].p - sk.v[a0].p);
                if (dot(nn, sk.v[a0].n) >= 0.f) sk.quad(a0, a1, b1, b0);
                else sk.quad(a0, b0, b1, a1);
            }
        // inner surface (so the inside of the skirt is visible from below)
        size_t iv0 = sk.v.size();
        for (int r = 0; r < NR; r++)
            for (int k = 0; k < NS; k++) {
                BVert v = sk.v[rows[r][k]];
                v.p -= v.n * 0.003f;
                v.n = -v.n;
                v.col = scol * 0.6f;
                v.alpha = 1.f;
                sk.add(v);
            }
        for (int r = 0; r + 1 < NR; r++)
            for (int k = 0; k < NS; k++) {
                int k1 = (k + 1) % NS;
                u32 a0 = (u32)(iv0 + r * NS + k), a1 = (u32)(iv0 + r * NS + k1), b0 = (u32)(iv0 + (r + 1) * NS + k), b1 = (u32)(iv0 + (r + 1) * NS + k1);
                vec3 nn = cross(sk.v[a1].p - sk.v[a0].p, sk.v[b0].p - sk.v[a0].p);
                if (dot(nn, sk.v[a0].n) >= 0.f) sk.quad(a0, a1, b1, b0);
                else sk.quad(a0, b0, b1, a1);
            }
        sk.computeNormals(0, sk.idx.size());
        {
            // the hem: the fabric's thickness along the bottom edge (own vertices, so the faces keep their normals)
            std::vector<u32> ho(NS), hi(NS);
            for (int k = 0; k < NS; k++) {
                BVert v = sk.v[rows[NR - 1][k]];
                v.n = vec3(0, 0, -1);
                v.col = scol * 0.8f;
                v.alpha = 1.f;
                ho[k] = sk.add(v);
                v.p -= normalize(vec3(sk.v[rows[NR - 1][k]].n.x, sk.v[rows[NR - 1][k]].n.y, 0.f)) * 0.003f;
                hi[k] = sk.add(v);
            }
            for (int k = 0; k < NS; k++) {
                int k1 = (k + 1) % NS;
                vec3 nn = cross(sk.v[ho[k1]].p - sk.v[ho[k]].p, sk.v[hi[k]].p - sk.v[ho[k]].p);
                if (nn.z <= 0.f) sk.quad(ho[k], ho[k1], hi[k1], hi[k]);
                else sk.quad(ho[k], hi[k], hi[k1], ho[k1]);
            }
        }
        o.out.append(sk);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
        // hide skin inside the skirt (upper legs)
        for (size_t t = 0; t + 2 < c.surfaceIdxEnd; t += 3) {
            bool all = true;
            for (int k = 0; k < 3; k++) {
                const BVert& v = c.m.v[c.m.idx[t + k]];
                if (!((v.part == PART_LEG && v.bp.z > hemZ + 0.08f * s) || (v.part == PART_TORSO && v.bp.z < zS - 0.02f * s))) all = false;
            }
            if (all) o.hideBody[t / 3] = 1;
        }
        return;
    }
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
    if (zCuff < R.zKnee - 0.1f * s) o.legHemZ = zCuff;
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, bool hem = false) {
        GarmentDef dg = decalOf(g, cov, dcol, mat, extraOff, parts, g.matParam);
        dg.hem = hem;
        emitGarment(o, dg);
    };
    auto line = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, g.matParam | kParamLodDetail, 2));
    };
    CovFn base = g.cov;
    const vec3 thread = g.mat == MAT_DENIM ? srgbToLinear(vec3(0.78f, 0.6f, 0.3f)) : darker(col, 0.8f);   // denim: gold topstitching
    // waistband
    decal([=, &R](const BVert& v) { return v.part == PART_TORSO ? Min(zt - v.bp.z, v.bp.z - (zt - 0.035f * R.s)) : -1.f; }, darker(col, 0.9f), g.mat, 0.0015f,
          1u << PART_TORSO);
    if (g.mat == MAT_DENIM) {
        // outer seams (felled, topstitched), the fly's J-stitch, the back yoke, coin pocket
        for (int sd = 0; sd < 2; sd++) seamAlong(o, g, PART_LEG, sd, kHalfPi, 0.f, 2.f * s, 0.0018f * s, thread, MAT_DENIM, 0u);
        const float zFlyTop = zt - 0.035f * s, zFlyBot = R.zCrotch + 0.035f * s;
        line([=](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f || v.bp.z > zFlyTop || v.bp.x > 0.002f * s) return -1.f;
            // J: straight down 3.2 cm left of the centre, then a quarter circle into the centre seam
            float r0 = 0.032f * s, d0;
            if (v.bp.z >= zFlyBot) d0 = fabsf(v.bp.x + r0);
            else if (v.bp.z >= zFlyBot - r0) d0 = fabsf(length(vec2(v.bp.x, v.bp.z - zFlyBot)) - r0);
            else return -1.f;
            return 0.0011f * s - d0;
        }, thread, MAT_DENIM, 0.0009f, 1u << PART_TORSO);
        const float zYoke = R.zHip + 0.055f * s;
        line([=](const BVert& v) {
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
        for (int sd = 0; sd < 2; sd++) seamAlong(o, g, PART_LEG, sd, kHalfPi, 0.f, 2.f * s, 0.0011f * s, darker(col, 0.82f), g.mat, g.matParam);
        if (bot == BOT_SLACKS || bot == BOT_POLICE) {
            // pressed crease down the front and back of each leg: a light line catching the light
            for (int sd = 0; sd < 2; sd++)
                for (int k = 0; k < 2; k++)
                    seamAlong(o, g, PART_LEG, sd, k ? kPi : 0.f, legAlongAtZ(D, R.zCrotch) + 0.03f * s, 2.f * s, 0.0009f * s, darker(col, 1.12f), g.mat,
                              g.matParam);
        }
    }
    if (bot != BOT_LEGGINGS) {
        // inseam
        for (int sd = 0; sd < 2; sd++)
            seamAlong(o, g, PART_LEG, sd, 1.5f * kPi, legAlongAtZ(D, R.zCrotch) + 0.01f * s, 2.f * s, (g.mat == MAT_DENIM ? 0.0014f : 0.0011f) * s,
                      g.mat == MAT_DENIM ? thread * 0.8f : darker(col, 0.82f), g.mat, g.matParam);
    }
    if (bot == BOT_JEANS || bot == BOT_BAGGY || bot == BOT_SLACKS || bot == BOT_WORK || bot == BOT_POLICE || bot == BOT_CARGO || bot == BOT_SHORTS) {
        // front pocket openings: a stitched curve from the waistband down to the side seam
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float zTopP = zt - 0.012f * s;
            bool slant = bot == BOT_SLACKS || bot == BOT_POLICE;
            line([=](const BVert& v) {
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
    // belts
    const float pantsOff = g.thick + loose * 0.7f;
    const float hemOver = untuckedHemZ(R, d);
    const bool waistCovered = hemOver > 0.f && hemOver < zt - 0.03f * s;   // an untucked top hangs over the waistband
    if (!waistCovered && (bot == BOT_JEANS || bot == BOT_BAGGY || bot == BOT_SLACKS || bot == BOT_WORK || bot == BOT_POLICE || bot == BOT_CARGO)) {
        // belt loops on the waistband (the belt, when there is one, runs through them)
        const float loopTh[7] = {0.36f, -0.36f, 1.45f, -1.45f, 2.45f, -2.45f, kPi};
        float zl = zt - 0.017f * s;
        for (float th : loopTh) {
            vec3 p, n;
            torsoPoint(c, zl, th, p, n);
            n = normalize(vec3(n.x, n.y, 0.f));
            vec3 ax = normalize(cross(vec3(0, 0, 1), n));
            addBoxOriented(o.out, p + n * (pantsOff + (belt || dutyBelt ? 0.0075f : 0.0022f)), ax, vec3(0, 0, 1), n, vec3(0.0055f, 0.019f, 0.0012f) * s,
                           darker(col, 0.95f), g.mat, torsoSkinWeights(D, p));
        }
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    if (belt || dutyBelt) {
        vec3 bc = dutyBelt ? vec3(0.03f) : (rng.chance(0.5f) ? vec3(0.35f) : vec3(1.0f));
        float bw = dutyBelt ? 0.026f * s : 0.017f * s;
        float zb = zt - bw - 0.004f * s;
        addTorsoBand(c, o.out, zb, bw, pantsOff + 0.002f, 0.004f, bc, MAT_LEATHER);
        // buckle (under an untucked top only the band remains: the buckle would poke through the shirt)
        vec3 p, n;
        torsoPoint(c, zb, 0.f, p, n);
        n = normalize(vec3(n.x, n.y, 0.f));
        SkinW sw = torsoSkinWeights(D, p);
        vec3 ax = normalize(cross(vec3(0, 0, 1), n));
        if (!waistCovered || dutyBelt)
            addBoxOriented(o.out, p + n * (pantsOff + 0.008f), ax, vec3(0, 0, 1), n, vec3(0.03f, bw * 1.05f, 0.003f) * (dutyBelt ? 1.f : 0.85f),
                           vec3(0.85f, 0.83f, 0.78f), MAT_CHROME, sw);
        if (dutyBelt) {
            // holster (right hip), pouches (front left), radio (left side), cuff case (back)
            struct Item { float th, dz; vec3 he; vec3 col; };
            const Item items[] = {
                {kHalfPi * 1.05f, -0.07f, vec3(0.03f, 0.09f, 0.035f), vec3(0.04f)},
                {-0.45f, -0.01f, vec3(0.022f, 0.035f, 0.016f), vec3(0.04f)},
                {-0.8f, -0.01f, vec3(0.022f, 0.035f, 0.016f), vec3(0.04f)},
                {-kHalfPi * 1.1f, 0.0f, vec3(0.03f, 0.05f, 0.022f), vec3(0.03f)},
                {kPi, -0.005f, vec3(0.035f, 0.03f, 0.02f), vec3(0.04f)},
            };
            for (const Item& it : items) {
                torsoPoint(c, zb + it.dz * s, it.th, p, n);
                n = normalize(vec3(n.x, n.y, 0.f));
                ax = normalize(cross(vec3(0, 0, 1), n));
                addBoxOriented(o.out, p + n * (pantsOff + 0.006f + it.he.z * s), ax, vec3(0, 0, 1), n, it.he * s, it.col, MAT_LEATHER,
                               torsoSkinWeights(D, p));
            }
            // pistol grip sticking out of the holster
            torsoPoint(c, zb, kHalfPi * 1.05f, p, n);
            n = normalize(vec3(n.x, n.y, 0.f));
            ax = normalize(cross(vec3(0, 0, 1), n));
            addBoxOriented(o.out, p + n * (pantsOff + 0.04f * s) + vec3(0, -0.02f * s, 0.035f * s), ax, vec3(0, 0, 1), n, vec3(0.012f, 0.03f, 0.014f) * s,
                           vec3(0.05f), MAT_PLASTIC, torsoSkinWeights(D, p));
        }
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
}

// ------------------------------------------------------------------------------------------------
// Coverall (TOP_JUMPSUIT, prison inmates): one shell over the torso, short sleeves and both legs, cut roomy (the seat
// and the legs hang straight, the sleeves stand off the arm), with a shirt collar over a V that shows a white crew-neck
// tee, a zip from the V to below the navel, a patch pocket on the left chest, a waist seam and side seams down the
// legs. Plain: no lettering or badges.
static void buildJumpsuit(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const vec3 col = d.topColor;
    // the tee underneath (only its neck shows in the V)
    {
        GarmentDef t;
        t.parts = 1u << PART_TORSO;
        t.col = srgbToLinear(vec3(0.9f, 0.9f, 0.88f));
        t.thick = 0.0015f;
        t.smooth = 0;
        t.matParam = 0;
        t.cov = [=, &R](const BVert& v) -> float {
            if (v.part != PART_TORSO) return -1.f;
            return covTorsoRange(R, v, R.zChest - 0.1f * s, 0.972f, 0.f, 0.3f);
        };
        emitGarment(o, t);
    }
    GarmentDef g;
    g.parts = (1u << PART_TORSO) | (1u << PART_ARM) | (1u << PART_LEG);
    g.col = col;
    g.thick = 0.0035f;
    g.matParam = 2;   // twill
    const float sl = 0.16f * s, zCuff = R.zAnkle + 0.025f * s, vd = 0.1f, vw = 0.3f;
    g.cov = [=, &R](const BVert& v) -> float {
        if (v.part == PART_TORSO) return covTorsoRange(R, v, -10.f, 0.972f, vd, vw);
        if (v.part == PART_ARM) return sl - v.pa;
        if (v.part == PART_LEG) return v.bp.z - zCuff;
        return -1.f;
    };
    // roomy: clears the tee, hangs looser towards the seat; the legs flare a little towards the hem
    const float clearT = 0.0015f + 0.004f, zc = R.zCrotch, zw = R.zWaist;
    g.extraFn = [=](const BVert& v) -> float {
        if (v.part == PART_TORSO) return Max(clearT, 0.008f * (0.6f + 0.8f * sstep(zw + 0.1f, zc, v.bp.z)));
        if (v.part == PART_LEG) return 0.01f + 0.006f * Saturate((zc - v.bp.z) / Max(zc - zCuff, 0.05f));
        return 0.006f;
    };
    g.hangDrift = 0.06f;
    g.hangTop = D.J[B_CHEST].z + 0.04f * s;
    {
        const float rk = D.rKnee, rt = D.rThigh;
        const float aC = legAlongAtZ(D, D.zCrotch), aK = D.thigh, aH = legAlongAtZ(D, zCuff), rTop = rt * 1.03f + 0.006f * s;
        const float rkn = rk * 1.32f + 0.009f * s, rhm = rk * 1.26f + 0.008f * s;
        const float rsT = D.rUpperArm * 1.3f + 0.006f * s, rsE = D.rUpperArm * 1.4f + 0.006f * s;
        g.tubeR = [=](const BVert& v) -> float {
            if (v.part == PART_ARM) {
                if (v.pa > sl + 0.01f * s) return 0.f;
                return Lerp(rsT, rsE, lstep(0.08f * s, Max(sl, 0.1f * s), v.pa)) * sstep(0.035f * s, 0.1f * s, v.pa);
            }
            if (v.part != PART_LEG) return 0.f;
            float a = v.pa;
            if (a > aH + 0.02f * s) return 0.f;
            float Rr = a < aK ? Lerp(rTop, rkn, lstep(aC, aK, a)) : Lerp(rkn, rhm, lstep(aK, Max(aH, aK + 0.05f), a));
            return Rr * sstep(aC - 0.02f * s, aC + 0.03f * s, a);
        };
    }
    {
        FoldSpec fs;
        fs.amp = 1.f;
        fs.sleeveEnd = sl;
        fs.legCuffZ = zCuff;
        fs.legLong = true;
        fs.waistZ = R.zWaist;   // the suit bunches a little at the waist
        fs.hangTop = g.hangTop;
        fs.hang = 0.4f;
        fs.seed = d.seed * 19u + 11u;
        g.foldFn = foldFnOf(makeFolds(c, fs), s);
        g.refineTol = 0.0008f;
    }
    g.smooth = 2;
    emitGarment(o, g);
    o.topTorsoOff = o.botTorsoOff = g.thick + 0.012f;
    o.topSleeveR = 0.f;
    o.legHemZ = zCuff;
    o.torsoOuter = std::make_shared<GarmentDef>(g);
    // ---- details
    const vec3 dark = darker(col, 0.8f);
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, u32 mp) {
        emitGarment(o, decalOf(g, cov, dcol, mat, extraOff, parts, mp));
    };
    CovFn base = g.cov;
    const float zVBot = Lerp(D.zNeckFront, R.zChest, 0.55f) - 0.005f * s, zZipBot = R.zHip + 0.02f * s;
    // zip: a narrow tape down the middle from the V to below the navel, with its pull at the top
    decal([=](const BVert& v) {
        if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
        return Min(Min(base(v), 0.0055f * s - fabsf(v.bp.x)), Min(zVBot - v.bp.z, v.bp.z - zZipBot));
    }, dark, MAT_CLOTH, 0.0009f, 1u << PART_TORSO, g.matParam);
    // patch pocket on the left chest with a stitched rim
    const float px = -0.065f * s, pz = R.zChest + 0.005f * s, pw = 0.042f * s, ph = 0.05f * s;
    decal([=](const BVert& v) {
        if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
        return Min(base(v), Min(pw - fabsf(v.bp.x - px), ph - fabsf(v.bp.z - pz)));
    }, darker(col, 0.95f), MAT_CLOTH, 0.0012f, 1u << PART_TORSO, g.matParam);
    // collar over the V
    addCollar(o, g.thick + 0.008f, darker(col, 0.97f), g.matParam, 0.5f, 0.3f, 1.f, true, 0.03f * s);
    // waist seam all round and the side seams down the legs (ribbons on the shell)
    seamAround(o, g, PART_TORSO, -1, R.zWaist, 0.f, kTwoPi, 0.0011f * s, dark, MAT_CLOTH, g.matParam);
    for (int sd = 0; sd < 2; sd++) {
        seamAlong(o, g, PART_LEG, sd, kHalfPi, 0.06f * s, legAlongAtZ(D, zCuff) - 0.01f * s, 0.0011f * s, dark, MAT_CLOTH, g.matParam);
        seamAround(o, g, PART_ARM, sd, sl - 0.012f * s, 0.f, kTwoPi, 0.0009f * s, dark, MAT_CLOTH, g.matParam);   // sleeve hem stitch
    }
    // zip pull
    {
        MeshB bm;
        vec3 n;
        vec3 p = garmentTorsoPoint(o, g, zVBot - 0.006f * s, 0.f, n);
        addBoxOriented(bm, p + n * 0.0022f, vec3(1, 0, 0), vec3(0, 0, 1), n, vec3(0.0035f, 0.008f, 0.0012f) * s, vec3(0.55f, 0.55f, 0.56f), MAT_CHROME,
                       torsoSkinWeights(D, p));
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
}

// ------------------------------------------------------------------------------------------------
// Shoes, socks and soles

static void buildShoes(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    (void)R;
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s, lift = D.lift;
    const int kind = d.shoes;
    if (kind == SHOE_BARE) return;
    vec3 col = d.shoeColor;
    Rng rng(hash32(d.seed * 31u + 7u));
    float ankZ = D.J[B_FOOT_L].z;
    // socks for closed shoes
    if (kind == SHOE_SNEAKER || kind == SHOE_RUNNER || kind == SHOE_DRESS || kind == SHOE_BOOT) {
        vec3 sockCol = kind == SHOE_DRESS ? vec3(0.03f) : (rng.chance(0.6f) ? vec3(0.9f) : vec3(0.05f));
        if (d.role == 7) sockCol = vec3(0.9f);   // (state-issue white)
        float sockTop = ankZ + (kind == SHOE_DRESS ? 0.12f : (rng.chance(0.5f) ? 0.035f : 0.1f)) * s;
        // under full-length trousers only the band below the hem can show
        if (o.legHemZ > 0.f) sockTop = Min(sockTop, o.legHemZ + 0.02f * s);
        GarmentDef sk;
        sk.parts = 1u << PART_LEG;
        sk.col = sockCol;
        sk.thick = 0.0012f;
        sk.smooth = 0;
        sk.hem = false;
        sk.cov = [=](const BVert& v) { return v.part == PART_LEG ? sockTop - v.bp.z : -1.f; };
        if (d.bottom != BOT_BAGGY && d.bottom != BOT_JEANS && d.bottom != BOT_SLACKS && d.bottom != BOT_POLICE && d.bottom != BOT_WORK)
            emitGarment(o, sk);
        else {
            sk.hides = true;
            emitGarment(o, sk);
        }
    }
    GarmentDef g;
    g.parts = 1u << PART_LEG;
    g.col = col;
    g.thick = 0.0045f;
    g.smooth = 4;   // a smooth last over the toes
    u8 soleMat = MAT_RUBBER;
    vec3 soleCol = vec3(1.f);
    float soleH = lift;
    float cutSide = ankZ - 0.012f * s, cutBack = ankZ + 0.012f * s, cutFront = ankZ + 0.03f * s;
    // styles within a kind: sneakers are low-top trainers, canvas plimsolls or high-tops; boots are laced work boots or
    // Chelsea boots; sandals are thongs or two-strap slides
    Rng rs(hash32(d.seed * 0x632BE5ABu + 0x55u));
    int sneakStyle = rs.f() < 0.55f ? 0 : (rs.f() < 0.65f ? 1 : 2);
    // inmates' state-issue canvas slip-ons: the plimsoll without laces, a low throat with elastic gussets at its sides
    const bool slipOn = kind == SHOE_SNEAKER && d.role == 7;
    if (slipOn) sneakStyle = 1;
    const bool chelsea = kind == SHOE_BOOT && d.role != 5 && d.role != 1 && d.role != 6 && rs.chance(d.gender == FEMALE ? 0.7f : 0.35f);
    const bool slides = kind == SHOE_SANDAL && rs.chance(d.gender == FEMALE ? 0.5f : 0.3f);
    switch (kind) {
        case SHOE_SNEAKER:
            g.mat = MAT_CLOTH; soleMat = MAT_CLOTH; soleCol = rng.chance(0.7f) ? vec3(0.9f) : col * 0.5f;
            if (sneakStyle == 1) { soleCol = vec3(0.92f); g.matParam = 1; }                                        // canvas
            if (sneakStyle == 2) cutSide = cutBack = cutFront = ankZ + 0.055f * s;                               // high-top
            if (slipOn) cutFront = ankZ - 0.008f * s;
            break;
        case SHOE_RUNNER: g.mat = MAT_CLOTH; soleMat = MAT_CLOTH; soleCol = vec3(0.92f); break;
        case SHOE_DRESS: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = ankZ - 0.03f * s; cutBack = ankZ - 0.015f * s; cutFront = ankZ - 0.022f * s;
            soleCol = vec3(1.f); g.thick = 0.004f; break;
        case SHOE_LOAFER: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = ankZ - 0.032f * s; cutBack = ankZ - 0.018f * s; cutFront = ankZ - 0.012f * s;
            soleCol = vec3(0.7f); g.thick = 0.0042f; break;
        case SHOE_BOOT: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = cutBack = cutFront = ankZ + (chelsea ? 0.08f : 0.11f) * s; g.thick = 0.006f; break;
        case SHOE_FLATS: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = ankZ - 0.035f * s; cutBack = ankZ - 0.02f * s; cutFront = ankZ - 0.038f * s;
            g.thick = 0.003f; break;
        case SHOE_SANDAL: g.mat = MAT_CLOTH; break;
        default: break;
    }
    // the toe box of a closed shoe reaches the sole's toe, a little higher and rounder than the foot (pointed on dress
    // shoes, roomy on boots and trainers)
    const float toeExt = kind == SHOE_DRESS ? 0.016f * s : (kind == SHOE_BOOT ? 0.012f * s : (kind == SHOE_FLATS ? 0.008f * s : 0.011f * s));
    // the collar stands a little off the ankle (a stiff, padded opening that the ankle turns in), so an ankle bent in
    // the stride stays inside it
    const float cSide = cutSide, cBack = cutBack, cFront = cutFront;
    const float collarOff = kind == SHOE_SANDAL ? 0.f : 0.005f * s;
    g.extraFn = [=](const BVert& v) -> float {
        if (v.part != PART_LEG) return 0.f;
        vec3 ank = D.J[v.side ? B_FOOT_R : B_FOOT_L];
        float y = v.bp.y - ank.y;
        float t = sstep(D.ballFwd * 0.7f, D.toeFwd, y);
        float fwd = Saturate(v.n.y);   // the toe cap faces forward
        float front = Max(0.f, cosf(v.pb)), back = Max(0.f, -cosf(v.pb));
        float cut = cSide + (cFront - cSide) * front * front + (cBack - cSide) * back * back;
        return toeExt * t * (0.35f + 0.65f * fwd) + collarOff * sstep(cut - 0.035f * s, cut, v.bp.z);
    };
    size_t upV0 = 0, upV1 = 0;   // the closed upper's vertices in o.out (the laces sit on them)
    if (kind == SHOE_SANDAL) {
        // straps: a band across the instep and a thong between the first toes
        GarmentDef st;
        st.parts = 1u << PART_LEG;
        st.col = col;
        st.mat = MAT_CLOTH;
        st.thick = 0.002f;
        st.smooth = 0;
        st.hides = false;
        st.cov = [=](const BVert& v) -> float {
            if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
            float sx = v.side ? 1.f : -1.f;
            vec3 ank = D.J[v.side ? B_FOOT_R : B_FOOT_L];
            vec2 toe(ank.x - sx * 0.012f * s, ank.y + D.toeFwd * 0.86f);
            float y = v.bp.y;
            float topSide = v.bp.z - (lift + 0.012f * s);
            float best = -1.f;
            if (slides) {
                // slides: a wide band across the forefoot and a narrower one across the instep
                float yy = y - ank.y;
                best = Max(0.017f * s - fabsf(yy - D.ballFwd * 0.95f), 0.009f * s - fabsf(yy - D.ballFwd * 0.42f));
                return Min(best, topSide);
            }
            // two straps from the toe gap to the sides of the forefoot
            for (int k = 0; k < 2; k++) {
                vec2 end(ank.x + (k ? 1.f : -1.f) * D.footW * 0.46f, ank.y + D.ballFwd * 0.55f);
                vec2 p(v.bp.x, y);
                float tt;
                float dd = distPointSegment2D(p, toe, end, &tt);
                best = Max(best, 0.0065f * s - dd);
            }
            return Min(best, topSide);
        };
        emitGarment(o, st);
    } else {
        float cs = cutSide, cb = cutBack, cf = cutFront;
        g.cov = [=](const BVert& v) -> float {
            if (v.part != PART_LEG) return -1.f;
            float th = v.pb;
            float front = Max(0.f, cosf(th)), back = Max(0.f, -cosf(th));
            float cut = cs + (cf - cs) * front * front + (cb - cs) * back * back;
            return cut - v.bp.z;
        };
        g.hideMargin = 0.006f;
        upV0 = o.out.v.size();
        emitGarment(o, g);
        upV1 = o.out.v.size();
        auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff) {
            GarmentDef dg;
            dg.parts = 1u << PART_LEG;
            dg.cov = cov;
            dg.col = dcol;
            dg.mat = mat;
            dg.thick = g.thick + extraOff;
            dg.extraFn = g.extraFn;
            dg.smooth = g.smooth;
            dg.hem = false;
            dg.hides = false;
            emitGarment(o, dg);
        };
        CovFn base = g.cov;
        auto ankOf = [=](const BVert& v) { return D.J[v.side ? B_FOOT_R : B_FOOT_L]; };
        if ((kind == SHOE_SNEAKER && sneakStyle != 1) || kind == SHOE_RUNNER) {
            // heel counter and toe cap in an accent colour, and a sweeping side band from the heel to the midfoot (an
            // original two-curve design on both sides)
            vec3 accent = rng.chance(0.5f) ? vec3(0.9f) - col * 0.7f : srgbToLinear(vec3(rng.f(), rng.f() * 0.5f, rng.f()));
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
                float y = v.bp.y - ankOf(v).y;
                float heel = Min(-0.01f * s - y, ankZ - 0.01f * s - v.bp.z);
                float toe = y - (D.toeFwd - 0.045f * s);
                return Max(heel, toe);
            }, saturate(accent), MAT_CLOTH, 0.0008f);
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ + 0.01f * s || fabsf(sinf(v.pb)) < 0.55f) return -1.f;
                float y = (v.bp.y - ankOf(v).y) / (0.11f * s), z = (v.bp.z - lift) / (0.05f * s);
                // a band rising from the heel towards the laces, thinning as it goes
                float zc = 0.35f + 0.45f * Saturate(y), w = 0.2f * (1.f - 0.6f * Saturate(y));
                return Min(Min(w - fabsf(z - zc), y + 0.35f), 0.95f - y) * 0.05f * s;
            }, saturate(accent * 0.9f + vec3(0.04f)), MAT_CLOTH, 0.001f);
        }
        if (kind == SHOE_SNEAKER && sneakStyle == 1) {
            // canvas plimsoll: a rubber toe cap and a rubber stripe round the base of the upper
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
                float y = v.bp.y - ankOf(v).y;
                return Max(y - (D.toeFwd - 0.035f * s), (lift + 0.014f * s) - v.bp.z);
            }, vec3(0.9f, 0.89f, 0.85f), MAT_CLOTH, 0.001f);   // (white rubber: the rubber material is tyre-black)
        }
        if (kind == SHOE_SNEAKER && sneakStyle == 2) {
            // high-top: a round ankle patch on the outside
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || fabsf(wrapAngle(v.pb - kHalfPi)) > 0.9f) return -1.f;
                vec3 a = ankOf(v);
                return 0.017f * s - length(vec2(v.bp.y - a.y + 0.005f * s, v.bp.z - a.z - 0.012f * s));
            }, vec3(0.92f), MAT_CLOTH, 0.001f);
        }
        if (kind == SHOE_LOAFER) {
            // moccasin apron: a raised U seam over the toes, and a saddle strap across the vamp with its slot
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
                vec3 a = ankOf(v);
                float y = v.bp.y - a.y - 0.07f * s, x = (v.bp.x - a.x) / (D.footW * 0.36f);
                float u = sqrtf(Sq(x) + Sq(Max(0.f, y) / (0.075f * s)));
                return Min(0.0016f * s - fabsf(u - 1.f) * 0.02f * s, cosf(v.pb) - 0.2f);
            }, g.col * 0.7f, MAT_LEATHER, 0.0012f);
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
                float y = v.bp.y - ankOf(v).y - 0.045f * s;
                return Min(0.009f * s - fabsf(y), cosf(v.pb) - 0.15f);
            }, g.col * 0.85f, MAT_LEATHER, 0.0018f);
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
                vec3 a = ankOf(v);
                float y = v.bp.y - a.y - 0.045f * s;
                return Min(Min(0.0025f * s - fabsf(y), 0.012f * s - fabsf(v.bp.x - a.x)), cosf(v.pb) - 0.6f);
            }, vec3(0.02f), MAT_LEATHER, 0.0024f);
        }
        if (slipOn) {
            // elastic gussets either side of the throat
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ + 0.01f * s) return -1.f;
                vec3 a = ankOf(v);
                float y = v.bp.y - a.y - 0.03f * s, side = fabsf(sinf(v.pb));
                return Min(Min(0.011f * s - fabsf(y), side - 0.45f), v.bp.z - (ankZ - 0.03f * s)) * 0.5f + Min(0.f, 0.85f - side) * 0.02f;
            }, vec3(0.62f, 0.62f, 0.6f), MAT_CLOTH, 0.0008f);
        }
        if (chelsea) {
            // elastic gussets on both sides and a pull tab at the back
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || fabsf(sinf(v.pb)) < 0.8f) return -1.f;
                float top = cutSide - v.bp.z, a = ankZ - 0.005f * s;
                return Min(top, 0.022f * s - fabsf(v.bp.y - ankOf(v).y) - Max(0.f, a - v.bp.z) * 0.6f);
            }, vec3(0.02f), MAT_CLOTH, 0.0008f);
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || cosf(v.pb) > -0.85f) return -1.f;
                return Min(cutBack + 0.018f * s - v.bp.z, v.bp.z - (cutBack - 0.012f * s)) * 0.3f + Min(0.f, 0.3f + cosf(v.pb)) * 0.02f;
            }, g.col * 0.6f, MAT_LEATHER, 0.0022f);
        }
        (void)base;
    }
    // soles: a smooth last-shaped outline (rounded heel, widest at the ball, rounded toe box a little past the toes),
    // extruded to the ground with a rounded bottom edge and a toe spring; sneakers / runners get a white midsole over
    // a darker outsole, dress shoes a thin sole with a heel block (the arch lifts off the ground), boots a thick lug
    // sole. Laced shoes get crossed laces over the tongue and a bow.
    const bool laced = (kind == SHOE_SNEAKER && !slipOn) || kind == SHOE_RUNNER || (kind == SHOE_BOOT && !chelsea);
    const float upperThick = g.thick;
    for (int sd = 0; sd < 2; sd++) {
        const u32 FM = sd ? MK_FOOT_R : MK_FOOT_L;
        const int o4 = sd ? 4 : 0;
        vec3 ank = D.J[B_FOOT_L + o4];
        float sx = sd ? 1.f : -1.f;
        const bool closed = kind != SHOE_SANDAL;
        float margin = kind == SHOE_SANDAL ? 0.004f * s : (kind == SHOE_BOOT ? 0.008f * s : 0.005f * s);
        float yHeel = ank.y - D.heelBack - margin, yToe = ank.y + D.toeFwd + (closed ? 0.012f * s : 0.004f * s);
        float xc = ank.x - sx * 0.004f * s;
        float h = kind == SHOE_SANDAL ? 0.014f : soleH;
        auto halfW = [&](float t, bool medial) {
            // foot half-width profile along the sole (0 heel .. 1 toe); the medial side is straighter
            const float T[] = {0.f, 0.12f, 0.3f, 0.5f, 0.7f, 0.86f, 1.f};
            const float Wl[] = {0.26f, 0.34f, 0.37f, 0.42f, 0.5f, 0.45f, 0.3f};
            const float Wm[] = {0.26f, 0.32f, 0.28f, 0.33f, 0.47f, 0.44f, 0.32f};
            const float* W = medial ? Wm : Wl;
            float w = W[6];
            for (int i = 0; i + 1 < 7; i++)
                if (t <= T[i + 1]) {
                    w = Lerp(W[i], W[i + 1], sstep((t - T[i]) / (T[i + 1] - T[i])));
                    break;
                }
            return w * D.footW + margin;
        };
        const int NS = 32;
        std::vector<vec3> outline(NS);
        std::vector<float> tOf(NS);
        const float yc = 0.5f * (yHeel + yToe), Lh = 0.5f * (yToe - yHeel);
        for (int k = 0; k < NS; k++) {
            float a = kTwoPi * k / NS;
            float ca = cosf(a), sa = sinf(a);
            float ey = (ca >= 0.f ? 1.f : -1.f) * powf(fabsf(ca), 0.75f);   // boxier than an ellipse
            float y = yc + Lh * ey;
            float t = Saturate((y - yHeel) / (yToe - yHeel));
            bool lateral = (sa >= 0.f) == (sx > 0.f);
            float ex = (sa >= 0.f ? 1.f : -1.f) * powf(fabsf(sa), 0.8f);
            float x = xc + ex * halfW(t, !lateral) * (ex * sx > 0.f ? 1.f : 1.f);
            outline[k] = vec3(x, y, 0.f);
            tOf[k] = t;
        }
        float topZ = lift + (kind == SHOE_SANDAL ? 0.f : 0.005f * s);
        auto spring = [&](float t) { return (kind == SHOE_BOOT ? 0.004f : 0.009f) * s * sstep(0.78f, 1.f, t); };
        auto archLift = [&](float t) { return kind == SHOE_DRESS || kind == SHOE_LOAFER ? 0.008f * s * bump(t, 0.38f, 0.08f) : 0.f; };
        // rings from the top edge down: top (tucked under the upper), welt, split (two-tone), lower edge, bottom
        const int NRr = 6;
        const float zf[NRr] = {1.f, 0.9f, 0.45f, 0.41f, 0.1f, 0.f};
        const float ins[NRr] = {0.004f, 0.f, 0.f, 0.f, 0.001f, 0.004f};
        vec3 outsole = kind == SHOE_SNEAKER || kind == SHOE_RUNNER ? lerp(soleCol, vec3(0.06f), 0.7f) : soleCol * 0.8f;
        MeshB sm;
        std::vector<u32> rings[NRr];
        float ballY = ank.y + D.ballFwd;
        for (int r = 0; r < NRr; r++) {
            rings[r].resize(NS);
            for (int k = 0; k < NS; k++) {
                vec3 p = outline[k];
                vec3 rad = normalize(vec3(p.x - xc, (p.y - yc) * 0.45f, 0.f));
                p -= rad * ins[r] * s;
                float t = tOf[k];
                float zb = spring(t) + archLift(t);
                float ztop = r == 0 ? topZ : h;
                p.z = r == 0 ? topZ : Lerp(zb, ztop, zf[r]);
                BVert v;
                v.p = p;
                v.bp = p;
                v.n = r >= 4 ? normalize(rad - vec3(0, 0, r == 5 ? 2.f : 0.6f)) : (r == 0 ? normalize(rad + vec3(0, 0, 1)) : rad);
                v.t = vec3(-rad.y, rad.x, 0);
                v.uv = vec2(p.x, p.y);
                v.col = r >= 3 && (kind == SHOE_SNEAKER || kind == SHOE_RUNNER) ? outsole : soleCol;
                if (r == 1 && (kind == SHOE_DRESS || kind == SHOE_LOAFER)) v.col = soleCol * 0.7f;   // welt stitching line
                v.mat = soleMat;
                v.part = PART_ACC;
                float tb = sstep(ballY - 0.015f * s, ballY + 0.02f * s, p.y);
                v.sw = skin2(B_FOOT_L + o4, B_TOE_L + o4, tb);
                rings[r][k] = sm.add(v);
            }
        }
        auto triOut = [&](u32 a0, u32 b0, u32 c0, vec3 f) {
            vec3 nn = cross(sm.v[b0].p - sm.v[a0].p, sm.v[c0].p - sm.v[a0].p);
            if (dot(nn, f) >= 0.f) sm.tri(a0, b0, c0);
            else sm.tri(a0, c0, b0);
        };
        for (int r = 0; r + 1 < NRr; r++)
            for (int k = 0; k < NS; k++) {
                int k1 = (k + 1) % NS;
                vec3 f = normalize(sm.v[rings[r][k]].n + sm.v[rings[r + 1][k]].n);
                triOut(rings[r][k], rings[r][k1], rings[r + 1][k1], f);
                triOut(rings[r][k], rings[r + 1][k1], rings[r + 1][k], f);
            }
        // bottom and top caps (fans)
        BVert cb = sm.v[rings[NRr - 1][0]];
        cb.p = vec3(xc, yc, 0.f);
        cb.n = vec3(0, 0, -1);
        cb.col = outsole * 0.8f;
        u32 cbi = sm.add(cb);
        BVert ct = sm.v[rings[0][0]];
        ct.p = vec3(xc, yc, topZ);
        ct.n = vec3(0, 0, 1);
        u32 cti = sm.add(ct);
        for (int k = 0; k < NS; k++) {
            int k1 = (k + 1) % NS;
            triOut(cbi, rings[NRr - 1][k], rings[NRr - 1][k1], vec3(0, 0, -1));
            triOut(cti, rings[0][k], rings[0][k1], vec3(0, 0, 1));
        }
        sm.computeNormals(0, sm.idx.size());
        for (int k = 0; k < NS; k++) sm.v[rings[NRr - 1][k]].n = normalize(sm.v[rings[NRr - 1][k]].n + vec3(0, 0, -1));
        o.out.append(sm);
        if (laced && upV1 > upV0) {
            // laces over the tongue: eyelet rows either side of the opening, crossed ribbons between them, a bow. The
            // lacing line is traced over the skin from the front row on the instep (an upward cast there always leaves
            // through the top of the foot) back and up towards the ankle, on boots on up the front of the shin to below
            // the collar; every lace point sits on the emitted upper (its height and skinning read from the nearest
            // upper vertices), so laces and bow follow the shoe instead of sinking into the ankle or floating off it.
            const int nRows = kind == SHOE_BOOT ? 7 : 5;
            const u32 LM = FM | (sd ? MK_LEG_R : MK_LEG_L);
            vec3 lc = rng.chance(0.6f) ? vec3(0.9f) : col * 0.8f;
            if (kind == SHOE_BOOT) lc = vec3(0.12f, 0.07f, 0.03f);
            auto onSkin = [&](vec3 p, vec3& nn) {
                vec3 q = c.sdf.project(p, LM, 6);
                vec3 gr = c.sdf.grad(q, LM);
                nn = length2(gr) > 1e-12f ? normalize(gr) : vec3(0, 0, 1);
                return q;
            };
            // the point h above the upper over the skin nearest p, with the upper's normal and skinning there
            auto onUpper = [&](vec3 p, float h, vec3& nn, SkinW& sw) {
                vec3 q = onSkin(p, nn);
                float wsum = 0.f, off = 0.f, bestD = 1e9f;
                sw = skin2(B_FOOT_L + o4, B_TOE_L + o4, 0.15f);
                for (size_t i = upV0; i < upV1; i++) {
                    const BVert& u = o.out.v[i];
                    if (dot(u.n, nn) < 0.3f) continue;
                    float d2 = length2(u.p - q);
                    if (d2 > Sq(0.03f * s)) continue;
                    float w = 1.f / (d2 + Sq(0.004f * s));
                    off += w * dot(u.p - q, nn);
                    wsum += w;
                    if (d2 < bestD) {
                        bestD = d2;
                        sw = u.sw;
                    }
                }
                off = wsum > 0.f ? Max(off / wsum, upperThick) : upperThick;
                return q + nn * (off + h);
            };
            // keeps p at least h above the upper (loops and ends of the bow where the shoe curves up under them)
            auto aboveUpper = [&](vec3 p, float h) {
                vec3 nn;
                SkinW sw;
                vec3 fl = onUpper(p, h, nn, sw);
                float below = dot(fl - p, nn);
                return below > 0.f ? p + nn * below : p;
            };
            vec3 o0(ank.x, ank.y + 0.105f * s, lift + 0.01f * s), cn;
            vec3 cur = onSkin(o0 + vec3(0, 0, c.sdf.castOut(o0, vec3(0, 0, 1), LM, 0.12f * s)), cn);
            std::vector<vec3> path{cur};
            std::vector<float> arc{0.f};
            // (trainers stop short of the ankle crease, so a bent ankle does not fold the shin onto the bow)
            const float lenEnd = kind == SHOE_BOOT ? 0.3f * s : 0.066f * s, zEnd = cutFront - 0.018f * s;
            const vec3 heading = normalize(vec3(0.f, -1.f, 1.2f));
            for (int it = 0; it < 160; it++) {
                vec3 td = heading - cn * dot(heading, cn);
                if (length2(td) < 1e-8f) break;
                vec3 nxt = onSkin(cur + normalize(td) * 0.002f * s, cn);
                if (nxt.z >= zEnd) break;
                arc.push_back(arc.back() + length(nxt - cur));
                path.push_back(nxt);
                cur = nxt;
                if (arc.back() >= lenEnd) break;
            }
            const float total = arc.back();
            auto along = [&](float a) {   // the lacing line at arc length a
                size_t k = 1;
                while (k + 1 < arc.size() && arc[k] < a) k++;
                float f = Saturate((a - arc[k - 1]) / Max(arc[k] - arc[k - 1], 1e-6f));
                return lerp(path[k - 1], path[k], f);
            };
            if (total > 0.02f * s) {
                std::vector<vec3> L(nRows), Rr(nRows), Nn(nRows), Tt(nRows), Ss(nRows);
                std::vector<SkinW> swLr(nRows), swRr(nRows);
                for (int i = 0; i < nRows; i++) {
                    float u = (float)i / (nRows - 1);
                    vec3 pc = along(total * u);
                    vec3 tn = normalize(along(Min(total * u + 0.004f * s, total)) - along(Max(total * u - 0.004f * s, 0.f)));
                    vec3 nn, n1, n2;
                    onSkin(pc, nn);
                    vec3 side = normalize(cross(tn, nn));   // towards -x
                    float halfGap = Lerp(0.007f, 0.011f, u) * s;
                    L[i] = onUpper(pc + side * halfGap, 0.0022f * s, n1, swLr[i]);
                    Rr[i] = onUpper(pc - side * halfGap, 0.0022f * s, n2, swRr[i]);
                    Nn[i] = normalize(n1 + n2);
                    Tt[i] = tn;
                    Ss[i] = side;
                }
                MeshB lm;
                auto ribbon = [&](vec3 a0, vec3 b0, vec3 n0, const SkinW& swa, const SkinW& swb) {
                    vec3 dd = b0 - a0;
                    vec3 side = normalize(cross(n0, dd)) * (0.0028f * s);
                    u32 base = (u32)lm.v.size();
                    vec3 q[4] = {a0 - side, a0 + side, b0 + side, b0 - side};
                    for (int k = 0; k < 4; k++) {
                        BVert v;
                        v.p = q[k] + n0 * (k == 1 || k == 2 ? 0.0004f : 0.f);
                        v.bp = v.p;
                        v.n = n0;
                        v.t = normalize(dd);
                        v.col = lc;
                        v.mat = MAT_CLOTH;
                        v.part = PART_ACC;
                        v.sw = k < 2 ? swa : swb;
                        lm.add(v);
                    }
                    vec3 fn = cross(q[1] - q[0], q[2] - q[0]);
                    if (dot(fn, n0) >= 0.f) lm.quad(base, base + 1, base + 2, base + 3);
                    else lm.quad(base, base + 3, base + 2, base + 1);
                };
                for (int i = 0; i + 1 < nRows; i++) {
                    vec3 n0 = normalize(Nn[i] + Nn[i + 1]);
                    ribbon(L[i], Rr[i + 1] + n0 * 0.0008f * s, n0, swLr[i], swRr[i + 1]);
                    ribbon(Rr[i], L[i + 1] + n0 * 0.0016f * s, n0, swRr[i], swLr[i + 1]);
                }
                ribbon(L[0], Rr[0], Nn[0], swLr[0], swRr[0]);   // bottom bar
                // bow at the top row: two loops lying back towards the ankle and two ends hanging forward over the laces
                const int it = nRows - 1;
                const vec3 N0 = Nn[it], T0 = Tt[it], S0 = Ss[it];
                const SkinW swTop = lerpSkin(swLr[it], swRr[it], 0.5f);
                const vec3 top = (L[it] + Rr[it]) * 0.5f + N0 * 0.002f * s;
                for (int e = 0; e < 2; e++) {
                    float ex = e ? 1.f : -1.f;
                    std::vector<vec3> pts;
                    std::vector<float> rad;
                    for (int k = 0; k <= 8; k++) {
                        float a = kPi * k / 8.f;
                        vec3 p = top + S0 * (ex * 0.012f * s * sinf(a)) + T0 * (0.003f * s * (1.f - cosf(a)) - 0.004f * s * sinf(a)) +
                                 N0 * (0.002f * s * sinf(a));
                        pts.push_back(aboveUpper(p, 0.0035f * s));
                        rad.push_back(0.0013f * s);
                    }
                    addTube(lm, pts, rad, 4, false, lc, MAT_CLOTH, std::vector<SkinW>(pts.size(), swTop));
                    const float lat[3] = {0.f, 0.006f, 0.009f}, fwd[3] = {0.f, 0.012f, 0.022f};
                    std::vector<vec3> tail;
                    for (int k = 0; k < 3; k++) tail.push_back(aboveUpper(top + S0 * (ex * lat[k] * s) - T0 * (fwd[k] * s), 0.005f * s));
                    std::vector<float> tr(3, 0.0012f * s);
                    addTube(lm, tail, tr, 4, false, lc, MAT_CLOTH, std::vector<SkinW>(3, swTop));
                }
                o.out.append(lm);
            }
        }
    }
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// ------------------------------------------------------------------------------------------------
// Hats (built on the head grid)

static void buildHat(OutfitCtx& o, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const HeadInfo& H = c.head;
    const float hs = D.headS;
    int hat = d.hat;
    if (hat < 0) return;
    Rng rng(hash32(d.seed * 41u + 11u));
    vec3 col = d.topColor;
    u8 mat = MAT_CLOTH;
    float edgeFront = 21.f, edgeSide = 16.f, edgeBack = 2.f;   // phi (deg) of the hat edge
    float thick = 0.006f;
    switch (hat) {
        case HAT_CAP: case HAT_CAP_BACK: col = rng.chance(0.5f) ? d.topColor : srgbToLinear(vec3(rng.f(), rng.f(), rng.f())); break;
        case HAT_POLICE: col = srgbToLinear(vec3(0.08f, 0.1f, 0.2f)); edgeFront = 20.f; edgeSide = 17.f; edgeBack = 8.f; thick = 0.01f; break;
        case HAT_HARDHAT: col = rng.pick(std::vector<vec3>{vec3(0.95f, 0.75f, 0.05f), vec3(0.95f, 0.95f, 0.9f), vec3(0.95f, 0.4f, 0.05f)});
            mat = MAT_METAL_PAINTED; edgeFront = 22.f; edgeSide = 19.f; edgeBack = 10.f; thick = 0.022f; break;
        case HAT_SUNHAT: col = rng.chance(0.5f) ? vec3(0.8f, 0.7f, 0.45f) : vec3(0.85f); edgeFront = 24.f; edgeSide = 20.f; edgeBack = 12.f; thick = 0.012f; break;
        case HAT_FEDORA: col = rng.chance(0.5f) ? vec3(0.75f, 0.68f, 0.5f) : vec3(0.12f); edgeFront = 23.f; edgeSide = 20.f; edgeBack = 12.f; thick = 0.014f; break;
        case HAT_BEANIE: col = srgbToLinear(rng.chance(0.5f) ? vec3(0.1f) : vec3(rng.f(), rng.f(), rng.f())); edgeFront = 26.f; edgeSide = 20.f; edgeBack = 0.f;
            thick = 0.007f; break;
        case HAT_BANDANA: col = rng.chance(0.5f) ? srgbToLinear(vec3(0.7f, 0.08f, 0.08f)) : srgbToLinear(vec3(0.1f, 0.2f, 0.55f)); edgeFront = 28.f;
            edgeSide = 22.f; edgeBack = 4.f; thick = 0.003f; break;
        default: break;
    }
    const float deg = kDegToRad;
    auto edgePhi = [=](float th) {
        float f = cosf(th);
        float e = f > 0.f ? Lerp(edgeSide, edgeFront, f) : Lerp(edgeSide, edgeBack, -f);
        return e * deg;
    };
    GarmentDef g;
    g.parts = 1u << PART_HEAD;
    g.col = col;
    g.mat = mat;
    g.thick = thick;
    g.smooth = hat == HAT_HARDHAT ? 6 : 2;
    g.hideMargin = 0.01f;
    const BuildCtx* cp = &c;
    g.cov = [=](const BVert& v) -> float {
        if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
        return (v.pb - edgePhi(v.pa)) * 0.1f;
    };
    g.extraFn = [=](const BVert& v) -> float {
        float hv = Min(hairVolumeAt(*cp, v), 0.02f);
        float dome = 0.f;
        if (hat == HAT_POLICE || hat == HAT_FEDORA) dome = 0.012f * sstep(30.f * deg, 70.f * deg, v.pb);
        return hv + dome;
    };
    emitGarment(o, g);
    // brim / bill
    MeshB bm;
    vec3 C = H.C;
    auto headPt = [&](float th, float ph, float extra) {
        vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
        float t = c.sdf.castOut(C, dir, MK_HEAD, 0.25f * hs);
        BVert tmp;
        tmp.pa = th;
        tmp.pb = ph;
        tmp.part = PART_HEAD;
        tmp.pc = 1.5f;
        float hv = Min(hairVolumeAt(c, tmp), 0.02f);
        return C + dir * (t + thick + hv + extra);
    };
    SkinW sw = skin1(B_HEAD);
    auto addV = [&](vec3 p, vec3 n, vec3 cc, u8 mm) {
        BVert v;
        v.p = p;
        v.bp = p;
        v.n = n;
        v.t = vec3(1, 0, 0);
        v.uv = vec2(p.x * 3.f, p.y * 3.f);
        v.col = cc;
        v.mat = mm;
        v.part = PART_ACC;
        v.sw = sw;
        return bm.add(v);
    };
    auto quadF = [&](u32 a, u32 b, u32 cc, u32 dd, vec3 f) {
        vec3 nn = cross(bm.v[b].p - bm.v[a].p, bm.v[dd].p - bm.v[a].p);
        if (dot(nn, f) >= 0.f) bm.quad(a, b, cc, dd);
        else bm.quad(a, dd, cc, b);
    };
    if (hat == HAT_CAP || hat == HAT_CAP_BACK || hat == HAT_POLICE) {
        // bill: curved plate from the edge, projecting forward (or backward)
        bool back = hat == HAT_CAP_BACK;
        vec3 bcol = hat == HAT_POLICE ? vec3(0.02f) : (rng.chance(0.3f) ? vec3(0.1f) : col);
        u8 bmat = hat == HAT_POLICE ? MAT_PLASTIC : MAT_CLOTH;
        const int NB = 9;
        float span = 62.f * deg;
        float len = (hat == HAT_POLICE ? 0.06f : 0.075f) * hs;
        std::vector<u32> t0(NB), t1(NB), b0(NB), b1(NB);
        for (int i = 0; i < NB; i++) {
            float u = (float)i / (NB - 1) * 2.f - 1.f;
            float th = (back ? kPi : 0.f) + u * span;
            float ph = edgePhi(th) + 1.f * deg;
            vec3 root = headPt(th, ph, 0.001f);
            vec3 outD = normalize(vec3(sinf(th), cosf(th), 0.f));
            float l = len * (1.f - 0.45f * u * u);
            vec3 tip = root + outD * l + vec3(0, 0, -(hat == HAT_POLICE ? 0.012f : 0.018f) * hs * (0.6f + 0.4f * (1.f - u * u)));
            vec3 up(0, 0, 1);
            t0[i] = addV(root + up * 0.002f, up, bcol, bmat);
            t1[i] = addV(tip + up * 0.002f, up, bcol, bmat);
            b0[i] = addV(root - up * 0.002f, -up, bcol * 0.7f, bmat);
            b1[i] = addV(tip - up * 0.002f, -up, bcol * 0.7f, bmat);
        }
        for (int i = 0; i + 1 < NB; i++) {
            quadF(t0[i], t0[i + 1], t1[i + 1], t1[i], vec3(0, 0, 1));
            quadF(b0[i], b0[i + 1], b1[i + 1], b1[i], vec3(0, 0, -1));
            vec3 f = normalize(bm.v[t1[i]].p - bm.v[t0[i]].p);
            quadF(t1[i], t1[i + 1], b1[i + 1], b1[i], f);
        }
        if (hat == HAT_POLICE) {
            // cap badge
            vec3 p = headPt(0.f, edgePhi(0.f) + 10.f * deg, 0.004f);
            addDisc(bm, p, normalize(p - C), 0.012f * hs, 7, 0.002f, srgbToLinear(vec3(0.85f, 0.68f, 0.3f)), MAT_CHROME, sw);
        } else {
            // button on top
            vec3 p = headPt(0.f, 88.f * deg, 0.f);
            addDisc(bm, p, vec3(0, 0, 1), 0.007f * hs, 6, 0.003f, col, MAT_CLOTH, sw);
        }
    }
    if (hat == HAT_SUNHAT || hat == HAT_FEDORA || hat == HAT_HARDHAT) {
        // round brim
        const int NB = 32;
        float w = hat == HAT_SUNHAT ? 0.13f * hs : (hat == HAT_FEDORA ? 0.055f * hs : 0.025f * hs);
        std::vector<u32> it0(NB), it1(NB), ib0(NB), ib1(NB);
        for (int i = 0; i < NB; i++) {
            float th = kTwoPi * i / NB;
            float ph = edgePhi(th);
            vec3 root = headPt(th, ph, 0.f);
            vec3 outD = normalize(vec3(sinf(th), cosf(th), 0.f));
            float frontExt = hat == HAT_HARDHAT ? (cosf(th) > 0.f ? 1.f + 1.4f * cosf(th) : 1.f) : 1.f;
            float droop = hat == HAT_SUNHAT ? 0.035f * hs : (hat == HAT_FEDORA ? (0.01f + 0.012f * Max(0.f, cosf(th))) * hs : 0.005f * hs);
            vec3 tip = root + outD * (w * frontExt) - vec3(0, 0, droop);
            vec3 up(0, 0, 1);
            it0[i] = addV(root + up * 0.002f, up, col, mat);
            it1[i] = addV(tip + up * 0.002f, up, col, mat);
            ib0[i] = addV(root - up * 0.002f, -up, col * 0.7f, mat);
            ib1[i] = addV(tip - up * 0.002f, -up, col * 0.7f, mat);
        }
        for (int i = 0; i < NB; i++) {
            int i1 = (i + 1) % NB;
            quadF(it0[i], it0[i1], it1[i1], it1[i], vec3(0, 0, 1));
            quadF(ib0[i], ib0[i1], ib1[i1], ib1[i], vec3(0, 0, -1));
            vec3 f = normalize(bm.v[it1[i]].p - bm.v[it0[i]].p);
            quadF(it1[i], it1[i1], ib1[i1], ib1[i], f);
        }
        if (hat == HAT_FEDORA || hat == HAT_SUNHAT) {
            // hat band
            GarmentDef band;
            band.parts = 1u << PART_HEAD;
            band.col = hat == HAT_FEDORA ? vec3(0.03f) : srgbToLinear(vec3(0.6f, 0.1f, 0.12f));
            band.thick = thick + 0.002f;
            band.smooth = g.smooth;
            band.hem = false;
            band.hides = false;
            band.cov = [=](const BVert& v) -> float {
                if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
                float e = edgePhi(v.pa);
                return Min(v.pb - e, e + 7.f * deg - v.pb) * 0.1f;
            };
            band.extraFn = g.extraFn;
            emitGarment(o, band);
        }
    }
    if (hat == HAT_BEANIE) {
        GarmentDef cuff;
        cuff.parts = 1u << PART_HEAD;
        cuff.col = col * 0.9f;
        cuff.thick = thick + 0.004f;
        cuff.smooth = 2;
        cuff.hides = false;
        cuff.cov = [=](const BVert& v) -> float {
            if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
            float e = edgePhi(v.pa);
            return Min(v.pb - e, e + 9.f * deg - v.pb) * 0.1f;
        };
        cuff.extraFn = g.extraFn;
        emitGarment(o, cuff);
    }
    bm.computeNormals(0, bm.idx.size());
    o.out.append(bm);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// ------------------------------------------------------------------------------------------------
// Glasses
//
// Real frame sizes (lens box ~48-58 mm wide, 32-46 mm tall, 15-18 mm bridge, the front ~12 mm in front of the
// corneas) so they span the face as real frames do. Plastic frames are flat bands (front, back and both edges, 4-6 mm
// wide) that read at a distance; metal frames are thin wire rims. The front is pushed forward as one piece wherever
// the rim would come closer than 3 mm to the skin (full cheeks, a high nose bridge).

namespace GlassesDetail {
// Lens outline (lateral x towards the temple is +, z up), unit half-sizes: 0 rounded rectangle (reading / office),
// 1 wayfarer (wider at the top), 2 aviator teardrop (deeper towards the nose at the bottom).
static vec2 outline(int shape, float a) {
    float c = cosf(a), s = sinf(a);
    float pw = shape == 2 ? 2.3f : (shape == 1 ? 3.2f : 3.6f);
    float x = Sign(c) * powf(fabsf(c), 2.f / pw), z = Sign(s) * powf(fabsf(s), 2.f / pw);
    if (shape == 1) x *= 1.f + 0.07f * z;                         // trapezoid: top wider
    if (shape == 2 && z < 0.f) {                                  // teardrop: deeper, and the low point towards the nose
        z *= 1.22f;
        x = x * (1.f + 0.1f * z) - 0.12f * z * (1.f - fabsf(x));
    }
    if (shape == 0 && z < 0.f && x < 0.f) x *= 1.f + 0.06f * z;   // bottom inner corner cut back for the nose
    return vec2(x, z);
}
}   // namespace GlassesDetail

// A pair of glasses of `kind` on the face, or (up) sunglasses pushed up onto the head: the frame is swung up about the
// line through the ear rests until its front lies over the hairline, then lifted clear of the hair.
static void buildGlassesKind(OutfitCtx& o, const CharacterDesc& d, int kind, bool up) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const HeadInfo& H = c.head;
    const float hs = D.headS;
    MeshB gm;
    SkinW sw = skin1(B_HEAD);
    bool sun = kind == GL_SUN, avi = kind == GL_AVIATOR, read = kind == GL_READING;
    Rng rng(hash32(d.seed * 53u + 17u));
    // style: aviators are gold / silver wire; sunglasses black or tortoise plastic; reading glasses metal (gunmetal,
    // gold, dark brown) or plastic (black, tortoise, dark red, navy)
    bool plastic = sun || (read && rng.chance(0.55f));
    bool tortoise = plastic && rng.chance(sun ? 0.25f : 0.35f);
    vec3 frameCol;
    u8 frameMat;
    if (avi) {
        frameCol = rng.chance(0.6f) ? vec3(0.85f, 0.72f, 0.45f) : vec3(0.8f);
        frameMat = MAT_CHROME;
    } else if (plastic) {
        const vec3 pc[4] = {vec3(0.012f), vec3(0.09f, 0.035f, 0.012f), vec3(0.16f, 0.02f, 0.02f), vec3(0.02f, 0.03f, 0.07f)};
        frameCol = sun ? vec3(0.012f) : pc[rng.irange(0, 3)];
        frameMat = MAT_PLASTIC;
    } else {
        // painted dark brown, gunmetal or gold
        const vec3 mc[3] = {srgbToLinear(vec3(0.25f, 0.18f, 0.12f)), vec3(0.08f, 0.08f, 0.09f), vec3(0.7f, 0.55f, 0.3f)};
        int mi = rng.irange(0, 2);
        frameCol = mc[mi];
        frameMat = mi == 0 ? MAT_METAL_PAINTED : MAT_CHROME;
    }
    const int shape = avi ? 2 : (sun ? 1 : 0);
    const float lw = (avi ? 0.057f : (sun ? 0.053f : 0.049f)) * hs * rng.range(0.96f, 1.04f);
    const float lh = (avi ? 0.045f : (sun ? 0.041f : 0.033f)) * hs * rng.range(0.94f, 1.06f);
    const float bridge = (avi ? 0.015f : (sun ? 0.017f : 0.0175f)) * hs;
    const float band = (sun ? 0.0052f : 0.0042f) * hs, depth = (sun ? 0.0042f : 0.0036f) * hs;   // plastic rim
    const float wire = (avi ? 0.0011f : 0.0012f) * hs;                                          // metal rim radius
    const float wrap = sun ? 0.2f : 0.12f;   // face-form wrap: the outer half of the front bends back
    const int NL = 32;
    std::vector<vec3> rim[2];
    std::vector<vec3> rimOut[2];   // in-plane outward direction per rim point
    vec3 lensC[2];
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 e = H.eyeC[sd];
        // box centre: half a lens plus half the bridge from the midline, 1.5 mm below the pupils, 12 mm in front of the
        // corneas
        vec3 cen(H.origin.x + sx * 0.5f * (lw + bridge), e.y + H.eyeR + 0.012f * hs, e.z - 0.0015f * hs);
        std::vector<vec2> q(NL);
        for (int k = 0; k < NL; k++) q[k] = GlassesDetail::outline(shape, kTwoPi * k / NL);
        rim[sd].resize(NL);
        rimOut[sd].resize(NL);
        for (int k = 0; k < NL; k++) {
            float x = q[k].x * 0.5f * lw;   // + = towards the temple
            float back = x > 0.f ? wrap * x * x / (0.5f * lw) : 0.03f * fabsf(x);
            rim[sd][k] = cen + vec3(sx * x, -back, q[k].y * 0.5f * lh);
            vec2 t = q[(k + 1) % NL] - q[(k + NL - 1) % NL];
            vec2 nrm = normalize(vec2(t.y, -t.x));
            if (dot(nrm, q[k]) < 0.f) nrm = -nrm;
            rimOut[sd][k] = normalize(vec3(sx * nrm.x, 0.f, nrm.y));
        }
        // clearance: move this front forward as one piece until every rim point is 3 mm off the skin
        float push = 0.f;
        for (int k = 0; k < NL; k++) {
            float dist = c.sdf.eval(rim[sd][k] - vec3(0, plastic ? 0.5f * depth : wire, 0), MK_HEAD);
            push = Max(push, 0.003f * hs - dist);
        }
        if (push > 0.f) {
            for (vec3& p : rim[sd]) p.y += push;
            cen.y += push;
        }
        lensC[sd] = cen;
    }
    // both fronts at the same depth (the frame is one rigid piece)
    float fy = Max(lensC[0].y, lensC[1].y);
    for (int sd = 0; sd < 2; sd++) {
        float dy = fy - lensC[sd].y;
        for (vec3& p : rim[sd]) p.y += dy;
        lensC[sd].y = fy;
    }
    // tortoise shell: mottled amber / dark brown per vertex
    auto rimCol = [&](vec3 p) {
        if (!tortoise) return frameCol;
        float n = sinf(p.x * 900.f + 1.3f) * sinf(p.z * 700.f + 0.7f) + 0.6f * sinf((p.x + p.z) * 1500.f);
        return lerp(vec3(0.03f, 0.012f, 0.004f), vec3(0.28f, 0.11f, 0.025f), Saturate(0.5f + 0.6f * n));
    };
    for (int sd = 0; sd < 2; sd++) {
        const std::vector<vec3>& R = rim[sd];
        // lens (sunglasses and aviators are tinted; reading glasses stay open)
        if (!read) {
            BVert lc;
            lc.p = lensC[sd] + vec3(0, 0.0015f * hs, 0);
            lc.bp = lc.p;
            lc.n = vec3(0, 1, 0);
            lc.t = vec3(1, 0, 0);
            lc.col = avi ? vec3(0.3f, 0.2f, 0.1f) : vec3(0.05f);
            lc.mat = MAT_CAR_GLASS;
            lc.part = PART_ACC;
            lc.sw = sw;
            u32 ci = gm.add(lc);
            std::vector<u32> ring;
            for (const vec3& p : R) {
                BVert v = lc;
                v.p = p;
                v.bp = p;
                v.n = normalize(vec3(0, 1, 0) + (p - lensC[sd]) * 8.f);
                ring.push_back(gm.add(v));
            }
            for (size_t k = 0; k < ring.size(); k++) {
                u32 a = ring[k], b = ring[(k + 1) % ring.size()];
                vec3 nn = cross(gm.v[a].p - lc.p, gm.v[b].p - lc.p);
                if (dot(nn, vec3(0, 1, 0)) >= 0.f) gm.tri(ci, a, b);
                else gm.tri(ci, b, a);
            }
        }
        if (plastic) {
            // flat band: inner edge on the lens outline, outer edge `band` outwards, front and back faces `depth` apart
            // (the band is a little wider at the top and the outer corners, as moulded fronts are)
            std::vector<u32> fi(NL), fo(NL), bi(NL), bo(NL);
            for (int k = 0; k < NL; k++) {
                vec3 p = R[k], out = rimOut[sd][k];
                float wdt = band * (1.f + 0.35f * Saturate(out.z) + 0.2f * Saturate(out.x * (sd ? 1.f : -1.f)));
                vec3 fr(0, 0.5f * depth, 0);
                BVert v;
                v.col = rimCol(p);
                v.mat = frameMat;
                v.part = PART_ACC;
                v.sw = sw;
                v.t = normalize(R[(k + 1) % NL] - R[(k + NL - 1) % NL]);
                v.uv = vec2(0.f);
                v.p = p + fr; v.bp = v.p; v.n = normalize(vec3(0, 1, 0) - out * 0.3f); fi[k] = gm.add(v);
                v.p = p + out * wdt + fr; v.bp = v.p; v.n = normalize(vec3(0, 1, 0) + out * 0.3f); fo[k] = gm.add(v);
                v.p = p - fr; v.bp = v.p; v.n = -out; bi[k] = gm.add(v);
                v.p = p + out * wdt - fr; v.bp = v.p; v.n = out; bo[k] = gm.add(v);
            }
            // back face vertices shared with the edges would smear the normals: separate copies for the back face
            std::vector<u32> bbi(NL), bbo(NL);
            for (int k = 0; k < NL; k++) {
                BVert v = gm.v[bi[k]];
                v.n = vec3(0, -1, 0);
                bbi[k] = gm.add(v);
                v = gm.v[bo[k]];
                v.n = vec3(0, -1, 0);
                bbo[k] = gm.add(v);
            }
            std::vector<u32> ei(NL), eo(NL);   // edge copies of the front rings with edge normals
            for (int k = 0; k < NL; k++) {
                BVert v = gm.v[fi[k]];
                v.n = gm.v[bi[k]].n;
                ei[k] = gm.add(v);
                v = gm.v[fo[k]];
                v.n = gm.v[bo[k]].n;
                eo[k] = gm.add(v);
            }
            auto quadFacing = [&](u32 a, u32 b, u32 cc, u32 dd, vec3 want) {
                vec3 nn = cross(gm.v[b].p - gm.v[a].p, gm.v[cc].p - gm.v[a].p);
                if (dot(nn, want) >= 0.f) gm.quad(a, b, cc, dd);
                else gm.quad(a, dd, cc, b);
            };
            for (int k = 0; k < NL; k++) {
                int k1 = (k + 1) % NL;
                vec3 out = rimOut[sd][k];
                quadFacing(fi[k], fi[k1], fo[k1], fo[k], vec3(0, 1, 0));      // front
                quadFacing(bbi[k], bbo[k], bbo[k1], bbi[k1], vec3(0, -1, 0));  // back
                quadFacing(eo[k], eo[k1], bo[k1], bo[k], out);                // outer edge
                quadFacing(ei[k], bi[k], bi[k1], ei[k1], -out);               // inner edge (lens groove)
            }
        } else {
            std::vector<float> rad(NL, wire);
            std::vector<SkinW> sws(NL, sw);
            addTube(gm, R, rad, 5, true, frameCol, frameMat, sws, vec3(0, 1, 0));
        }
    }
    // bridge: from the upper inner corner of one front to the other, arched up and forward over the nose
    auto innerTop = [&](int sd) {
        // rim point with the largest (nasal * 1 + up * 0.8) score
        float sx = sd ? 1.f : -1.f;
        int best = 0;
        float bs = -1e9f;
        for (int k = 0; k < NL; k++) {
            vec3 v = rim[sd][k] - lensC[sd];
            float sc = -sx * v.x + 0.8f * v.z;
            if (sc > bs) {
                bs = sc;
                best = k;
            }
        }
        return rim[sd][best];
    };
    {
        vec3 a = innerTop(0), b = innerTop(1);
        vec3 mid = (a + b) * 0.5f + vec3(0, 0.0015f * hs, 0.0025f * hs);
        float dist = c.sdf.eval(mid, MK_HEAD);
        if (dist < 0.004f * hs) mid.y += 0.004f * hs - dist;
        std::vector<vec3> pts = {a, lerp(a, mid, 0.6f) + vec3(0, 0, 0.001f * hs), mid, lerp(b, mid, 0.6f) + vec3(0, 0, 0.001f * hs), b};
        std::vector<float> rad(5, plastic ? 0.5f * depth * 1.15f : wire * 1.1f);
        std::vector<SkinW> sws(5, sw);
        addTube(gm, pts, rad, plastic ? 6 : 5, false, frameCol, frameMat, sws, vec3(0, 1, 0));
        if (avi) {   // aviator brow bar
            vec3 a2 = a + vec3(0, 0, 0.0085f * hs), b2 = b + vec3(0, 0, 0.0085f * hs);
            std::vector<vec3> bar = {a2, (a2 + b2) * 0.5f + vec3(0, 0.001f * hs, 0.0005f * hs), b2};
            std::vector<float> br(3, wire);
            std::vector<SkinW> bsw(3, sw);
            addTube(gm, bar, br, 5, false, frameCol, frameMat, bsw, vec3(0, 1, 0));
        }
        if (!plastic) {   // nose pads on metal frames
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 pad = lensC[sd] + vec3(-sx * 0.5f * lw * 0.9f, -0.006f * hs, -0.004f * hs);
                std::vector<vec3> arm = {innerTop(sd), pad};
                std::vector<float> ar(2, wire * 0.8f);
                std::vector<SkinW> asw(2, sw);
                addTube(gm, arm, ar, 4, false, frameCol, frameMat, asw, vec3(0, 1, 0));
            }
        }
    }
    // temples: from the hinge at the outer upper corner back past the head's widest point to the top of the ear, then
    // down behind it; bars on plastic frames, wire on metal ones
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        int best = 0;
        float bs = -1e9f;
        for (int k = 0; k < NL; k++) {
            vec3 v = rim[sd][k] - lensC[sd];
            float sc = sx * v.x + 0.45f * v.z;
            if (sc > bs) {
                bs = sc;
                best = k;
            }
        }
        vec3 hinge = rim[sd][best] + rimOut[sd][best] * (plastic ? band : wire) - vec3(0, plastic ? 0.5f * depth : 0.f, 0);
        vec3 ear = H.earPos[sd] + vec3(sx * 0.004f * hs, 0.008f * hs, 0.018f * hs);
        vec3 back = ear + vec3(-sx * 0.003f * hs, -0.02f * hs, -0.012f * hs);
        std::vector<vec3> pts = {hinge};
        for (int k = 1; k <= 3; k++) {
            vec3 p = lerp(hinge, ear, k / 4.f);
            vec3 dir = normalize(p - H.C);
            float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.2f * hs);
            vec3 onSkin = H.C + dir * (t + 0.0035f * hs);
            // never inside the head, and flaring slightly outwards from the hinge
            if (dot(p - onSkin, dir) < 0.f) p = onSkin;
            pts.push_back(p);
        }
        pts.push_back(ear);
        pts.push_back(back);
        std::vector<float> rad(pts.size(), plastic ? 0.0017f * hs : 0.0009f * hs);
        rad[0] = plastic ? 0.0021f * hs : 0.0011f * hs;
        std::vector<SkinW> sws(pts.size(), sw);
        addTube(gm, pts, rad, plastic ? 6 : 4, false, plastic && tortoise ? rimCol(hinge) : frameCol, frameMat, sws);
    }
    if (up) {
        // swing the frame up about the ear rests, then lift it clear of the hair (and the scalp)
        vec3 pl = H.earPos[0] + vec3(-0.004f * hs, 0.008f * hs, 0.018f * hs), pr = H.earPos[1] + vec3(0.004f * hs, 0.008f * hs, 0.018f * hs);
        vec3 pivot = (pl + pr) * 0.5f;
        quat q = quatAxisAngle(vec3(1, 0, 0), 76.f * kDegToRad);
        for (BVert& v : gm.v) {
            v.p = pivot + rotate(q, v.p - pivot);
            v.n = rotate(q, v.n);
            v.t = rotate(q, v.t);
        }
        vec3 cen(0);
        for (const BVert& v : gm.v) cen += v.p;
        cen /= (float)Max((size_t)1, gm.v.size());
        vec3 lift = normalize(cen - H.C);
        float need = 0.f;
        for (const BVert& v : gm.v) {
            vec3 dir = normalize(v.p - H.C);
            float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
            BVert pr2;
            float th = atan2f(dir.x, dir.y);
            pr2.pa = th < 0.f ? th + kTwoPi : th;
            pr2.pb = asinf(Clamp(dir.z, -1.f, 1.f));
            pr2.part = PART_HEAD;
            pr2.pc = 1.5f;
            float want = t + hairVolumeAt(c, pr2) + 0.003f * hs;
            float have = dot(v.p - H.C, dir);
            need = Max(need, (want - have) / Max(dot(lift, dir), 0.3f));
        }
        for (BVert& v : gm.v) v.p += lift * need;
        for (BVert& v : gm.v) v.bp = v.p;
    }
    gm.computeNormals(0, 0);
    o.out.append(gm);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

static void buildGlasses(OutfitCtx& o, const CharacterDesc& d) {
    if (d.glasses >= 0) buildGlassesKind(o, d, Clamp(d.glasses, 0, GL_COUNT - 1), false);
    else if ((d.extras & ACC_SUNGLASSES_UP) && d.hat < 0) buildGlassesKind(o, d, GL_SUN, true);
}

// ------------------------------------------------------------------------------------------------
// Jewelry: chains, earrings, watches

static void buildJewelry(OutfitCtx& o, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    Rng rng(hash32(d.seed * 61u + 19u));
    bool fem = d.gender == FEMALE;
    const float s = D.s;
    vec3 gold = srgbToLinear(vec3(1.0f, 0.78f, 0.35f)), silver = vec3(0.9f);
    // necklace / chain, earrings and the watch: from the desc's extras bits when they were chosen explicitly
    // (randomCharacter), else drawn from the seed as before
    const bool expl = (d.extras & ACC_EXPLICIT) != 0;
    bool chain = (d.role == 2 && rng.chance(0.7f)) || (d.role == 0 && rng.chance(fem ? 0.3f : 0.12f)) || (d.role == 4 && rng.chance(0.25f));
    if (expl) chain = (d.extras & ACC_NECKLACE) != 0;
    bool openNeck = d.top == TOP_TANK || d.top == TOP_NONE || d.top == TOP_BIKINI || d.top == TOP_ONEPIECE || d.top == TOP_SUNDRESS ||
                    d.top == TOP_TSHIRT || d.top == TOP_OVERSIZED || d.top == TOP_HAWAIIAN || d.top == TOP_CROP || d.top == TOP_BLOUSE;
    if (chain && openNeck) {
        const std::vector<u32>& ring = c.torsoTop;
        int n = (int)ring.size();
        vec3 cen(0);
        for (u32 vi : ring) cen += c.m.v[vi].p;
        cen /= (float)n;
        std::vector<vec3> pts;
        std::vector<float> rad;
        std::vector<SkinW> sws;
        float drop = (d.role == 2 ? 0.07f : 0.03f) * s;
        float thick = (d.role == 2 ? 0.004f : 0.0018f) * s;
        for (int k = 0; k < n; k++) {
            const BVert& bv = c.m.v[ring[k]];
            vec3 radial = normalize(vec3(bv.p.x - cen.x, bv.p.y - cen.y, 0.f));
            float front = Max(0.f, cosf(bv.pb));
            vec3 p = bv.p + radial * (0.009f * s) - vec3(0, 0, drop * front * front) + vec3(0, drop * 0.25f * front * front, 0);
            // follow the chest surface where the chain drops
            vec3 dir = normalize(p - vec3(0, cen.y - 0.03f * s, p.z));
            float t = c.sdf.castOut(vec3(0, cen.y - 0.03f * s, p.z), dir, MK_TORSO | MK_NECK, 0.3f * s);
            vec3 q = vec3(0, cen.y - 0.03f * s, p.z) + dir * (t + 0.006f * s + thick);
            pts.push_back(front > 0.2f ? q : p);
            rad.push_back(thick);
            sws.push_back(torsoSkinWeights(D, bv.p));
        }
        addTube(o.out, pts, rad, 5, true, rng.chance(0.75f) ? gold : silver, MAT_CHROME, sws);
    }
    // earrings (women mostly)
    bool ear = (fem && rng.chance(0.6f)) || (!fem && rng.chance(0.08f));
    if (expl) ear = (d.extras & ACC_EARRINGS) != 0;
    if (ear) {
        for (int sd = 0; sd < 2; sd++) {
            vec3 lobe = c.head.earPos[sd] + vec3((sd ? 1.f : -1.f) * 0.006f, -0.004f, -0.024f) * D.headS * D.earSize;
            bool hoop = rng.chance(0.4f);
            std::vector<vec3> pts;
            std::vector<float> rad;
            if (hoop) {
                float r = 0.012f * D.headS;
                for (int k = 0; k < 10; k++) {
                    float a = kTwoPi * k / 10;
                    pts.push_back(lobe + vec3(0, sinf(a) * r, -r + cosf(a) * r));
                    rad.push_back(0.0012f);
                }
            } else {
                for (int k = 0; k < 6; k++) {
                    float a = kTwoPi * k / 6;
                    pts.push_back(lobe + vec3(0, sinf(a) * 0.0028f, cosf(a) * 0.0028f));
                    rad.push_back(0.0016f);
                }
            }
            std::vector<SkinW> sws(pts.size(), skin1(B_HEAD));
            addTube(o.out, pts, rad, 4, true, gold, MAT_CHROME, sws, vec3(1, 0, 0));
        }
    }
    // wrist watch (left wrist)
    bool longSleeve = d.top == TOP_SUIT || d.top == TOP_HOODIE;
    bool watch = (d.role == 3 && rng.chance(0.8f)) || rng.chance(0.3f);
    if (expl) watch = (d.extras & ACC_WATCH) != 0;
    if (!longSleeve && watch) {
        int o4 = 0;
        vec3 wr = D.J[B_HAND_L + o4] - D.armDir[0] * 0.022f * s;
        vec3 ad = D.armDir[0], fr(0, 1, 0), pn = D.palmN[0];
        std::vector<vec3> pts;
        std::vector<float> rad;
        const int N = 12;
        for (int k = 0; k < N; k++) {
            float a = kTwoPi * k / N;
            vec3 dd = fr * cosf(a) * 1.1f + (-pn) * sinf(a) * 0.8f;
            vec3 dir = normalize(dd);
            float t = c.sdf.castOut(wr, dir, MK_ARM_L, 0.1f);
            pts.push_back(wr + dir * (t + 0.003f));
            rad.push_back(0.004f);
        }
        std::vector<SkinW> sws(N, skin2(B_FOREARM_L, B_HAND_L, 0.5f));
        vec3 band = rng.chance(0.5f) ? vec3(0.03f) : silver;
        addTube(o.out, pts, rad, 4, true, band, rng.chance(0.5f) ? MAT_LEATHER : MAT_CHROME, sws, ad);
        // watch face on the back of the wrist (-palmN side)
        vec3 dir = -pn;
        float t = c.sdf.castOut(wr, dir, MK_ARM_L, 0.1f);
        addDisc(o.out, wr + dir * (t + 0.004f), dir, 0.016f * s, 10, 0.004f, silver, MAT_CHROME, skin2(B_FOREARM_L, B_HAND_L, 0.5f));
    }
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

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
            // a run between two points on the clothes: the chord's points projected out from the torso axis onto them
            auto run = [&](vec3 from, vec3 to, bool front, std::vector<vec3>& RP, std::vector<vec3>& RN, std::vector<SkinW>& RW) {
                (void)front;
                const int K = 10;
                for (int k = 0; k <= K; k++) {
                    float t = (float)k / K;
                    vec3 q = lerp(from, to, t);
                    float a = atan2f(q.x, q.y - profAxisY(o, q.z));
                    vec3 gn;
                    vec3 gp = outerTorsoPoint(o, q.z, a, gn);
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
        addBoxOriented(m, gp + fwd * 0.006f * s, ax, vec3(0, 0, 1), fwd, vec3(0.027f, 0.042f, 0.0012f) * s, vec3(0.88f), MAT_METAL_PAINTED, cw);
        addBoxOriented(m, gp + fwd * 0.0073f * s + vec3(0, 0, 0.028f * s), ax, vec3(0, 0, 1), fwd, vec3(0.027f, 0.01f, 0.0003f) * s, cordCol,
                       MAT_METAL_PAINTED, cw);
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

// Bikini top (TOP_BIKINI): thin fabric 1.5 mm off the skin that follows the breasts, in one of two cuts:
//  - triangle: a fabric triangle over each breast (base along the underbust, apex towards the neck), an underbust
//    string round the back tied in a knot with hanging ends, halter strings from the apexes up the sides of the neck
//    and round the nape (tied there);
//  - bandeau: a straight band round the chest from the underbust to about half way up the breasts.
static void buildBikiniTop(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = R.s, rb = R.breastR;
    Rng rng(hash32(d.seed * 0x2F6B3C1Du + 0x77u));
    const bool bandeau = rng.chance(0.3f);
    const float zBot = R.breast[0].z - rb * 0.85f;   // underbust line
    const float zRing = D.zNeckFront + 0.024f * s;    // halter strings round the neck
    const float napeRise = 0.04f * s;                    // (tied higher at the nape)
    const float yNeck = D.J[B_NECK].y;
    GarmentDef g;
    g.parts = 1u << PART_TORSO;
    g.col = d.topColor;
    g.thick = 0.0015f;
    g.smooth = 0;
    g.hideMargin = 0.003f;
    if (bandeau) {
        g.cov = [=](const BVert& v) -> float {
            if (v.part != PART_TORSO) return -1.f;
            float zTop = Lerp(zBot + 0.045f * s, R.breast[0].z + rb * 0.45f, sstep(-0.02f * s, 0.06f * s, v.bp.y));
            return Min(v.bp.z - (zBot - 0.002f * s), zTop - v.bp.z);
        };
        emitGarment(o, g);
        return;
    }
    auto cupCorners = [=](int sd, vec2& inner, vec2& outer, vec2& apex) {
        float sx = sd ? 1.f : -1.f;
        vec3 bc = R.breast[sd];
        inner = vec2(bc.x - sx * rb * 0.8f, zBot);   // clear of the cleavage fold
        outer = vec2(bc.x + sx * rb * 1.05f, zBot + rb * 0.12f);
        apex = vec2(bc.x - sx * rb * 0.02f, bc.z + rb * 1.15f);
    };
    // cups: triangular patches sampled straight on the body surface (a dense grid of their own, so the edges are
    // clean whatever the torso tessellation), 1.5 mm off the skin, with a thin rim for the fabric's edge
    for (int sd = 0; sd < 2; sd++) {
        vec2 inner, outer, apex;
        cupCorners(sd, inner, outer, apex);
        const int N = 7;
        std::vector<u32> idx((size_t)(N + 1) * (N + 2) / 2);
        auto at = [&](int i, int j) -> u32& { return idx[(size_t)i * (N + 1) - (size_t)i * (i - 1) / 2 + j]; };
        // the cup outline is drawn in the frontal plane: a ray forwards from inside the chest finds the surface point
        // in front of each outline point (from the breast's centre where that start lies outside the body)
        const vec3 bo = R.breast[sd] - vec3(0.f, rb * 0.9f, 0.f);
        for (int i = 0; i <= N; i++)
            for (int j = 0; j <= N - i; j++) {
                vec2 q = inner + (outer - inner) * ((float)j / N) + (apex - inner) * ((float)i / N);
                vec3 org(q.x, bo.y - 0.02f * s, q.y), dir(0, 1, 0);
                if (c.sdf.eval(org, MK_TORSO) > -0.002f * s) {
                    org = bo;
                    dir = normalize(vec3(q.x, R.breast[sd].y, q.y) - bo);
                }
                vec3 sp = org + dir * c.sdf.castOut(org, dir, MK_TORSO, 0.4f * s);
                vec3 gn = c.sdf.grad(sp, MK_TORSO);
                vec3 n = length2(gn) > 1e-12f ? normalize(gn) : vec3(0, 1, 0);
                BVert v;
                v.p = sp + n * g.thick;
                v.bp = sp;
                v.n = n;
                v.t = vec3(1, 0, 0);
                v.uv = vec2(q.x, q.y);
                v.col = g.col;
                v.mat = MAT_CLOTH;
                v.part = PART_GARMENT;
                v.side = (u8)sd;
                v.sw = torsoSkinWeights(D, sp);
                v.layer = g.thick;
                at(i, j) = o.out.add(v);
            }
        size_t i0 = o.out.idx.size();
        auto triOut = [&](u32 a, u32 b, u32 cc) {
            vec3 fn = cross(o.out.v[b].p - o.out.v[a].p, o.out.v[cc].p - o.out.v[a].p);
            if (dot(fn, o.out.v[a].n) >= 0.f) o.out.tri(a, b, cc);
            else o.out.tri(a, cc, b);
        };
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N - i; j++) {
                triOut(at(i, j), at(i, j + 1), at(i + 1, j));
                if (j + 1 < N - i) triOut(at(i, j + 1), at(i + 1, j + 1), at(i + 1, j));
            }
        // rim: the three edges curl down to the skin
        std::vector<u32> edge;
        for (int j = 0; j <= N; j++) edge.push_back(at(0, j));
        for (int i = 1; i <= N; i++) edge.push_back(at(i, N - i));
        for (int i = N - 1; i >= 1; i--) edge.push_back(at(i, 0));
        vec3 cen(0);
        for (u32 e : edge) cen += o.out.v[e].p;
        cen /= (float)edge.size();
        std::vector<u32> rim(edge.size());
        for (size_t k = 0; k < edge.size(); k++) {
            BVert v = o.out.v[edge[k]];
            v.p = v.bp + v.n * 0.0003f;
            v.col = v.col * 0.8f;
            rim[k] = o.out.add(v);
        }
        for (size_t k = 0; k < edge.size(); k++) {
            size_t k1 = (k + 1) % edge.size();
            u32 a = edge[k], b = edge[k1], ra = rim[k], rb2 = rim[k1];
            vec3 outDir = normalize(o.out.v[a].p - cen);
            vec3 fn = cross(o.out.v[b].p - o.out.v[a].p, o.out.v[ra].p - o.out.v[a].p);
            if (dot(fn, outDir) >= 0.f) {
                o.out.tri(a, b, ra);
                o.out.tri(b, rb2, ra);
            } else {
                o.out.tri(a, ra, b);
                o.out.tri(b, ra, rb2);
            }
        }
        o.out.computeNormals(i0, o.out.idx.size());
    }
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    // strings (thin round cords on the skin): the underbust string all round (the cups ride on it at the front), the
    // halter from one apex up the side of the neck, round the nape and down to the other apex; knots at the back and
    // at the nape
    const float rs = 0.0018f * s;
    const vec3 cordCol = d.topColor * 0.92f;
    // `tMax` keeps the cords on the neck (the trapezius primitives share the neck's mask)
    auto surf = [&](vec3 org, vec3 dir, u32 mask, float off, float tMax = 1e9f) {
        float t = Min(c.sdf.castOut(org, dir, mask, 0.4f * s), tMax);
        return org + dir * (t + off);
    };
    {
        std::vector<vec3> pts;
        std::vector<float> rad;
        std::vector<SkinW> sws;
        const float z = zBot + 0.002f * s, yAx = D.J[B_CHEST].y - 0.01f * s;
        for (int k = 0; k < 40; k++) {
            float a = kTwoPi * k / 40.f;
            vec3 p = surf(vec3(0.f, yAx, z), vec3(sinf(a), cosf(a), 0.f), MK_TORSO, g.thick + rs * 0.8f);
            pts.push_back(p);
            rad.push_back(rs);
            sws.push_back(torsoSkinWeights(D, p));
        }
        addTube(o.out, pts, rad, 5, true, cordCol, MAT_CLOTH, sws);
    }
    {
        std::vector<vec3> pts;
        std::vector<float> rad;
        std::vector<SkinW> sws;
        vec2 inner, outer, apexL, apexR;
        cupCorners(0, inner, outer, apexL);
        cupCorners(1, inner, outer, apexR);
        // the cord's points are guide points projected onto the body (the union of chest and neck, so it runs over
        // the neck's junction with the trapezius instead of through it), lifted by the cord's radius; the weights
        // are the skin's under each point
        const u32 HM = MK_TORSO | MK_NECK;
        auto onBody = [&](vec3 guide, float off) {
            vec3 q = c.sdf.project(guide, HM, 8);
            vec3 gn = c.sdf.grad(q, HM);
            vec3 n = length2(gn) > 1e-12f ? normalize(gn) : vec3(0, 1, 0);
            return q + n * off;
        };
        // front halves: from the apex (on the cup) up over the upper chest to the side of the neck
        auto frontRun = [&](vec2 apex, float sx, bool up) {
            const int N = 11;
            vec3 a0 = surf(vec3(apex.x, D.J[B_CHEST].y - 0.02f * s, apex.y), vec3(0, 1, 0), MK_TORSO, g.thick);
            vec3 a1(sx * D.neckR * 0.95f, yNeck + D.neckR * 0.3f, zRing);
            for (int i = 0; i < N; i++) {
                float t = up ? (float)i / (N - 1) : 1.f - (float)i / (N - 1);
                vec3 guide = lerp(a0, a1, t) + vec3(0.f, 0.03f * s * sinf(kPi * t), 0.f);   // (bowed out, near the chest)
                // (lifted a little more over the collarbones and the neck's tendons, which the tessellated skin
                // carries beyond the field)
                vec3 p = onBody(guide, (t < 0.12f ? g.thick : 0.f) + rs * Lerp(0.8f, 1.8f, sstep(0.3f, 0.7f, t)));
                pts.push_back(p);
                rad.push_back(rs);
                sws.push_back(skinWeightsAt(o, p, (1u << PART_TORSO) | (1u << PART_NECK)));
            }
        };
        frontRun(apexL, -1.f, true);
        // round the back of the neck, rising to the nape (the back of the neck starts higher than the front: at the
        // front ring's height the cord would run inside the trapezius)
        for (int k = 1; k < 10; k++) {
            float a = -kPi + kPi * (float)k / 10.f;   // left side -> nape -> right side
            float zk = zRing + napeRise * sstep(0.f, 1.f, -sinf(a));
            vec3 guide(D.neckR * 1.05f * cosf(a), yNeck + D.neckR * (sinf(a) + 0.3f * (1.f - fabsf(sinf(a)))), zk);
            vec3 p = onBody(guide, rs * 1.8f);
            pts.push_back(p);
            rad.push_back(rs);
            sws.push_back(skinWeightsAt(o, p, (1u << PART_TORSO) | (1u << PART_NECK)));
        }
        frontRun(apexR, 1.f, false);
        // relax the path (the projection can step where the nearest surface changes, over a collarbone) and put it
        // back on the body; the ends stay on the cups
        for (int it = 0; it < 3; it++) {
            std::vector<vec3> sm = pts;
            for (size_t i = 1; i + 1 < pts.size(); i++) {
                float lift = length(pts[i] - c.sdf.project(pts[i], HM, 4));
                sm[i] = onBody((pts[i - 1] + pts[i + 1]) * 0.25f + pts[i] * 0.5f, lift);
            }
            pts.swap(sm);
        }
        addTube(o.out, pts, rad, 5, false, cordCol, MAT_CLOTH, sws);
    }
    auto backPoint = [&](float z, u32 mask, float y0) { return surf(vec3(0.f, y0, z), vec3(0, -1, 0), mask, 0.f); };
    const vec3 knotCol = d.topColor * 0.85f;
    vec3 kb = backPoint(zBot + 0.002f * s, MK_TORSO, D.J[B_CHEST].y);
    SkinW swB = torsoSkinWeights(D, kb);
    addBoxOriented(o.out, kb + vec3(0, -0.004f * s, 0), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0), vec3(0.007f, 0.005f, 0.0035f) * s, knotCol, MAT_CLOTH, swB);
    for (int e = 0; e < 2; e++) {
        float ex = e ? 1.f : -1.f;
        vec3 ay = normalize(vec3(ex * 0.25f, 0.f, -1.f));
        addBoxOriented(o.out, kb + vec3(ex * 0.005f * s, -0.0035f * s, 0.f) + ay * (0.022f * s), normalize(cross(ay, vec3(0, -1, 0))), ay, vec3(0, -1, 0),
                       vec3(0.0018f, 0.02f, 0.0012f) * s, knotCol, MAT_CLOTH, swB);
    }
    vec3 kn = backPoint(zRing + napeRise, MK_NECK, yNeck);
    addBoxOriented(o.out, kn + vec3(0, -0.0035f * s, 0), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0), vec3(0.0055f, 0.004f, 0.003f) * s, knotCol, MAT_CLOTH,
                   skin2(B_CHEST, B_NECK, 0.6f));
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// ------------------------------------------------------------------------------------------------

void buildOutfit(BuildCtx& c, MeshB& out, std::vector<u8>& hideTri) {
    const CharacterDesc& d = *c.d;
    Ref R;
    makeRef(c, R);
    MeshB raw;
    OutfitCtx o(c, raw, hideTri);
    buildHairLayer(o);
    // inner layers first: bottoms (unless tucked shirts: then tops first so pants cover them)
    bool tucked = d.top == TOP_DRESS_SHIRT || d.top == TOP_POLICE || d.top == TOP_MEDIC;
    if (d.top == TOP_BIKINI) buildBikiniTop(o, R, d);
    if (d.top == TOP_SUNDRESS) {
        // dress bodice with straps
        GarmentDef g;
        g.parts = 1u << PART_TORSO;
        g.col = d.topColor;
        g.thick = 0.003f;
        g.smooth = 1;
        g.cov = [=, &R](const BVert& v) -> float {
            if (v.part != PART_TORSO) return -1.f;
            return Min(v.bp.z - R.zWaist, (0.8f - v.pc) * R.torsoLen);
        };
        g.extraFn = [](const BVert&) { return 0.003f; };
        g.matParam = 1;
        g.hangDrift = 0.08f;   // the bodice falls from the bust
        g.hangTop = c.D->J[B_CHEST].z + 0.04f * R.s;
        emitGarment(o, g);
        addShoulderStraps(o, g.cov, 0.09f * R.s, 0.0065f * R.s, 0.0045f, d.topColor * 0.95f, 1u);
    }
    if (d.top == TOP_JUMPSUIT) {
        buildJumpsuit(o, R, d);
    } else if (tucked) {
        buildTopGarments(o, R, d);
        buildBottomGarments(o, R, d);
    } else {
        buildBottomGarments(o, R, d);
        if (d.top == TOP_SUIT) {
            // jacket with V opening (shirt/tie added inside buildTopGarments)
            CharacterDesc dj = d;
            buildTopGarments(o, R, dj);
        } else if (d.top == TOP_HIVIS) {
            buildTopGarments(o, R, d);
            // vest over the tee: sleeveless, bright, with reflective tape
            vec3 vcol = (d.seed & 1) ? vec3(0.8f, 1.0f, 0.02f) : vec3(1.0f, 0.3f, 0.01f);
            GarmentDef v;
            v.parts = (1u << PART_TORSO);
            v.col = vcol;
            v.thick = 0.012f;
            v.smooth = 2;
            v.cov = [=, &R](const BVert& q) -> float {
                if (q.part != PART_TORSO) return -1.f;
                float body = Min(covTorsoRange(R, q, R.zHip + 0.0f, 0.95f, 0.1f, 0.35f), (0.815f - q.pc) * R.torsoLen + 0.06f * R.s);
                float strap = covStraps(R, q, 0.1f * R.s, 0.035f * R.s, 0.955f);
                return Max(Min(body, (0.79f - q.pc) * R.torsoLen + 0.03f), strap);
            };
            emitGarment(o, v);
            GarmentDef tape;
            tape.parts = 1u << PART_TORSO;
            tape.col = vec3(0.75f, 0.76f, 0.74f);
            tape.mat = MAT_CHROME;
            tape.thick = 0.0135f;
            tape.smooth = 2;
            tape.hem = false;
            tape.hides = false;
            CovFn vc = v.cov;
            tape.cov = [=, &R](const BVert& q) -> float {
                float band1 = 0.013f * R.s - fabsf(q.bp.z - (R.zWaist - 0.02f * R.s));
                float band2 = 0.013f * R.s - fabsf(q.bp.z - (R.zChest - 0.05f * R.s));
                float vert = 0.013f * R.s - fabsf(fabsf(q.bp.x) - 0.1f * R.s);
                vert = Min(vert, q.bp.z - (R.zChest - 0.05f * R.s));
                return Min(Max(Max(band1, band2), vert), vc(q) - 0.004f);
            };
            emitGarment(o, tape);
        } else {
            buildTopGarments(o, R, d);
        }
    }
    buildOuterLayer(o, R, d);
    buildShoes(o, R, d);
    buildHat(o, d);
    buildGlasses(o, d);
    buildJewelry(o, d);
    buildSmallAccessories(o, R, d);
    buildBags(o, R, d);
    // drop hidden inner-layer triangles
    std::vector<u8> hideRaw = o.hideOut;
    hideRaw.resize(raw.idx.size() / 3, 0);
    std::vector<u32> remap(raw.v.size(), 0xffffffffu);
    size_t nt = raw.idx.size() / 3;
    for (size_t t = 0; t < nt; t++) {
        if (hideRaw[t]) continue;
        for (int k = 0; k < 3; k++) {
            u32 vi = raw.idx[t * 3 + k];
            if (remap[vi] == 0xffffffffu) {
                remap[vi] = (u32)out.v.size();
                out.v.push_back(raw.v[vi]);
            }
            out.idx.push_back(remap[vi]);
        }
    }
}

}  // namespace detail
}  // namespace Anim
