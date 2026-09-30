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
#include <unordered_map>

namespace Anim {
namespace detail {

static const u32 kSurfParts = (1u << PART_TORSO) | (1u << PART_NECK) | (1u << PART_HEAD) | (1u << PART_ARM) | (1u << PART_HAND) |
                              (1u << PART_LEG);

static inline float angLerp(float a, float b, float t) { return a + wrapAngle(b - a) * t; }

static BVert lerpVert(const BVert& a, const BVert& b, float t) {
    BVert v = a;
    v.p = lerp(a.p, b.p, t);
    v.bp = lerp(a.bp, b.bp, t);
    v.n = normalize(lerp(a.n, b.n, t));
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
    // offsets
    const size_t gn = gm.v.size();
    std::vector<float> off(gn);
    for (size_t i = 0; i < gn; i++) {
        BVert& v = gm.v[i];
        off[i] = g.thick + (g.extraFn ? g.extraFn(v) : 0.f);
        v.p = v.bp + v.n * off[i];
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
    // constrained smoothing (looseness)
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
    // materials / colors
    for (size_t i = 0; i < gn; i++) {
        BVert& v = gm.v[i];
        v.mat = g.mat;
        v.col = g.colFn ? g.colFn(v, g.col) : g.col;
        v.alpha = 1.f;
        if (g.swapUV) {
            v.uv = vec2(v.uv.y, v.uv.x);
            v.uPer = 0.f;
        }
        if (isB[i]) v.p += v.n * 0.0012f;   // rolled hem
    }
    size_t tStart = gm.idx.size();
    gm.computeNormals(0, tStart);
    // hem rim towards the skin
    if (g.hem) {
        std::unordered_map<u32, u32> inner;
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
        for (auto& e : bEdges) {
            u32 a = e.first, b = e.second;
            vec3 nn = normalize(gm.v[a].n + gm.v[b].n);
            vec3 outDir = normalize(cross(gm.v[b].p - gm.v[a].p, nn));
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
        }
    }
    // hide skin and inner layers underneath
    if (g.hides) {
        for (size_t t = 0; t + 2 < o.c.surfaceIdxEnd; t += 3)
            if (cv[bm.idx[t]] > g.hideMargin && cv[bm.idx[t + 1]] > g.hideMargin && cv[bm.idx[t + 2]] > g.hideMargin) o.hideBody[t / 3] = 1;
        for (auto& L : o.layers)
            for (size_t t = L.t0; t < L.t1; t++) {
                const BVert& a = o.out.v[o.out.idx[t * 3]];
                const BVert& b = o.out.v[o.out.idx[t * 3 + 1]];
                const BVert& cc = o.out.v[o.out.idx[t * 3 + 2]];
                auto covAt = [&](const BVert& v) { return g.cov(v); };
                if (covAt(a) > g.hideMargin + 0.004f && covAt(b) > g.hideMargin + 0.004f && covAt(cc) > g.hideMargin + 0.004f) o.hideOut[t] = 1;
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
    if (g.hides) o.layers.push_back(L);
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
// Fabric folds: extra shell offset (meters) where cloth bunches, plus a shade for the fold troughs (vertex colour; the
// renderer adds its curvature occlusion on top). Compression ridges in the crook of the elbow and behind the knee,
// stacking above long cuffs and trouser hems, bunching over the waist / blousing over a tucked hem, soft drape from
// the shoulder towards the armpit and denim whiskers at the front of the hips. Ridges are wavy (per-garment phase)
// rather than rings, sharp crests over wide troughs like real compression folds.
struct FoldSpec {
    float amp = 1.f;          // overall scale (0 = none): looser garments fold more
    float sleeveEnd = 0.f;    // arm: along-length of the sleeve (0 = no sleeve)
    float legCuffZ = 0.f;     // leg: height of the trouser hem (0 = no trouser legs)
    bool legLong = false;     // full-length trousers (stack on the shoe)
    float waistZ = 0.f;       // torso: height of the hem / waistband the top bunches over (0 = none)
    bool tucked = false;      // tucked hem: blousing just above the waistband
    bool denim = false;       // jeans whiskers
    u32 seed = 0;
};
static FORCEINLINE float foldCrest(float x) {
    // periodic 0..1 with narrow crests (x in cycles)
    float s = 0.5f + 0.5f * sinf(kTwoPi * x);
    return s * s * s;
}
static void fabricFold(const BuildCtx& c, const FoldSpec& f, const BVert& v, float& off, float& shade) {
    off = 0.f;
    shade = 0.f;
    if (f.amp <= 0.f) return;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const float ph0 = hashToFloat(hash32(f.seed * 747796405u + (u32)v.side * 2891336453u)) * kTwoPi;
    float fold = 0.f, trough = 0.f;
    auto ridge = [&](float x, float lambda, float wav, float envW) {
        float cr = foldCrest(x / lambda + wav);
        fold += envW * cr;
        trough += envW * (1.f - cr) * 0.6f;
    };
    if (v.part == PART_ARM && f.sleeveEnd > 0.f) {
        float al = v.pa, th = v.pb;
        float eA = D.upperArm;
        float crook = sstep(-0.2f, 0.85f, cosf(th));
        float envE = bump(al, eA - 0.004f * s, 0.05f * s) * (0.25f + 0.75f * crook) * (al < f.sleeveEnd - 0.01f * s ? 1.f : 0.f);
        if (envE > 0.01f) ridge(al - eA, 0.032f * s, 0.35f * sinf(th * 1.5f + ph0), envE * 0.008f);
        // stacking above a long cuff
        if (f.sleeveEnd > D.upperArm + 0.1f * s) {
            float envC = bump(al, f.sleeveEnd - 0.04f * s, 0.028f * s) * (0.55f + 0.45f * crook);
            ridge(al, 0.027f * s, 0.5f * sinf(th + ph0 * 1.7f), envC * 0.006f);
        }
        // drape from the shoulder towards the armpit (under the arm)
        float under = sstep(0.2f, -0.9f, sinf(th));
        float envS = bump(al, 0.06f * s, 0.05f * s) * under;
        if (envS > 0.01f) ridge(al * 0.7f + th * 0.012f * s, 0.03f * s, ph0, envS * 0.004f);
    }
    if (v.part == PART_LEG && f.legCuffZ > 0.f) {
        float al = v.pa, th = v.pb, z = v.bp.z;
        float kA = D.thigh;
        float back = sstep(-0.1f, -0.85f, cosf(th)), front = sstep(0.1f, 0.85f, cosf(th));
        float envK = bump(al, kA + 0.01f * s, 0.045f * s) * (0.2f + 0.8f * back) * (z > f.legCuffZ + 0.02f * s ? 1.f : 0.f);
        if (envK > 0.01f) ridge(al - kA, 0.034f * s, 0.3f * sinf(th * 2.f + ph0), envK * 0.0065f);
        // stacking on the shoe (long trousers)
        if (f.legLong) {
            float envH = bump(z, f.legCuffZ + 0.05f * s, 0.045f * s) * (0.5f + 0.5f * front);
            ridge(z, 0.045f * s, 0.9f * sinf(th * 1.3f + ph0 * 2.3f), envH * 0.0095f);
        }
        // denim whiskers: fine diagonal creases at the front of the hip
        if (f.denim) {
            float envW = bump(al, 0.07f * s, 0.05f * s) * bump(th, (v.side ? 1.f : -1.f) * 0.55f, 0.45f);
            if (envW > 0.02f) ridge(al + th * 0.03f * s * (v.side ? 1.f : -1.f), 0.018f * s, ph0, envW * 0.0022f);
        }
    }
    if (v.part == PART_TORSO && f.waistZ > 0.f) {
        float z = v.bp.z, th = v.pb;
        if (f.tucked) {
            float envB = bump(z, f.waistZ + 0.035f * s, 0.035f * s);
            fold += envB * 0.006f;   // blousing
            ridge(th * 0.16f * s, 0.028f * s, 0.2f * sinf(z * 60.f + ph0), envB * 0.004f);   // vertical creases into the waistband
        } else {
            float envW = bump(z, f.waistZ + 0.06f * s, 0.05f * s) * (0.55f + 0.45f * fabsf(sinf(th)));
            ridge(z - f.waistZ, 0.045f * s, 0.45f * sinf(th * 2.f + ph0), envW * 0.006f);
        }
    }
    off = fold * f.amp;
    shade = Saturate(trough * f.amp / 0.005f) * 0.2f;
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

static void buildTopGarments(OutfitCtx& o, const Ref& R, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float s = D.s;
    const int top = d.top;
    vec3 col = d.topColor;
    Rng rng(hash32(d.seed * 7u + 3u));
    auto torsoArms = (1u << PART_TORSO) | (1u << PART_ARM) | (1u << PART_NECK);
    if (top == TOP_NONE) return;
    GarmentDef g;
    g.parts = torsoArms;
    g.col = col;
    float sleeve = 0.14f * s, hemZ = R.zCrotch + 0.075f * s, neckPc = 0.972f, vDip = 0.f, vW = 0.3f;
    float loose = 0.006f;
    bool tank = false, collar = false, buttons = false, tucked = false;
    int nButtons = 0;
    switch (top) {
        case TOP_TSHIRT: sleeve = 0.15f * s; loose = 0.007f; break;
        case TOP_OVERSIZED: sleeve = 0.3f * s; hemZ = R.zCrotch - 0.02f * s; loose = 0.016f; g.smooth = 3; break;
        case TOP_TANK: tank = true; loose = 0.005f; hemZ = R.zCrotch + 0.07f * s; break;
        case TOP_POLO: sleeve = 0.155f * s; collar = true; buttons = true; nButtons = 2; loose = 0.007f; break;
        case TOP_HAWAIIAN: sleeve = 0.17f * s; collar = true; buttons = true; nButtons = 5; loose = 0.011f; vDip = 0.07f; vW = 0.25f; break;
        case TOP_DRESS_SHIRT: sleeve = rng.chance(0.5f) ? R.armLen - 0.02f * s : R.upperArm + 0.02f * s; collar = true; buttons = true;
            nButtons = 6; tucked = true; loose = 0.006f; break;
        case TOP_HOODIE: sleeve = R.armLen - 0.01f * s; hemZ = R.zCrotch + 0.03f * s; loose = 0.014f; g.smooth = 3; break;
        case TOP_SUIT: sleeve = R.armLen - 0.03f * s; hemZ = R.zCrotch - 0.03f * s; loose = 0.009f; break;
        case TOP_POLICE: sleeve = 0.17f * s; collar = true; buttons = true; nButtons = 6; tucked = true; loose = 0.007f; break;
        case TOP_MEDIC: sleeve = 0.17f * s; collar = true; buttons = true; nButtons = 5; tucked = true; loose = 0.007f; break;
        case TOP_HIVIS: sleeve = 0.15f * s; loose = 0.007f; col = d.topColor; break;
        case TOP_BLOUSE: sleeve = rng.chance(0.5f) ? 0.12f * s : 0.f; vDip = 0.09f; vW = 0.3f; loose = 0.009f; hemZ = R.zCrotch + 0.08f * s; break;
        case TOP_CROP: tank = true; hemZ = R.zWaist + 0.06f * s; loose = 0.004f; break;
        default: break;
    }
    if (top == TOP_BIKINI || top == TOP_ONEPIECE || top == TOP_SUNDRESS) return;   // handled with bottoms/dresses
    if (tucked) hemZ = R.zHip + 0.02f * s;
    vec3 shirtCol = rng.chance(0.7f) ? vec3(0.85f, 0.85f, 0.83f) : srgbToLinear(vec3(0.7f, 0.8f, 0.95f));
    if (top == TOP_SUIT) {
        // shirt under the jacket (visible in the V) and tie; the jacket shell follows
        collar = true;
        buttons = true;
        nButtons = 2;
        GarmentDef sh;
        sh.parts = torsoArms;
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
    float sl = sleeve;
    float hz = hemZ, np = neckPc, vd = vDip, vw = vW;
    bool tk = tank;
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
    float ls = loose;
    float zc = R.zCrotch, zw = R.zWaist;
    // an untucked top hangs over the waistband: clear the bottoms' shell (plus belt) where they overlap
    float clearE = o.botTopZ > 0.f ? o.botTorsoOff + 0.004f - g.thick : 0.f;
    float zbt = o.botTopZ;
    g.extraFn = [=](const BVert& v) -> float {
        float e = ls;
        if (v.part == PART_ARM) e = ls * (0.6f + 0.8f * Saturate(v.pa / Max(sl, 0.05f)));
        else if (v.part == PART_TORSO) {
            e = ls * (0.5f + 0.9f * sstep(zw + 0.1f, zc, v.bp.z));   // hangs looser at the hem
            if (clearE > 0.f) e = Max(e, clearE * sstep(zbt + 0.06f, zbt - 0.005f, v.bp.z));
        }
        return e;
    };
    o.topTorsoOff = g.thick + ls * 1.4f;
    if (top == TOP_HAWAIIAN) {
        u32 sd = d.seed;
        g.colFn = [=](const BVert& v, vec3 base) { return floral(v, base, sd); };
    }
    {
        // folds: elbows / cuffs / armpits on the sleeves, bunching or blousing at the waist
        FoldSpec fs;
        fs.amp = Lerp(0.45f, 1.2f, Saturate(loose / 0.012f));
        fs.sleeveEnd = tank ? 0.f : sleeve;
        fs.waistZ = hemZ;
        fs.tucked = tucked;
        fs.seed = d.seed * 3u + 1u;
        const BuildCtx* cp = &c;
        auto baseExtra = g.extraFn;
        g.extraFn = [=](const BVert& v) {
            float off, sh;
            fabricFold(*cp, fs, v, off, sh);
            return baseExtra(v) + off;
        };
        auto baseCol = g.colFn;
        g.colFn = [=](const BVert& v, vec3 cc) {
            vec3 r = baseCol ? baseCol(v, cc) : cc;
            float off, sh;
            fabricFold(*cp, fs, v, off, sh);
            return r * (1.f - sh);
        };
    }
    if (top == TOP_HIVIS) {
        // tee under the vest
        g.col = d.topColor;
    }
    if (g.smooth < 1) g.smooth = 1;
    g.smooth = Max(g.smooth, 2);
    emitGarment(o, g);
    // ---- details
    const float off = g.thick + loose;
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
            vec3 p2 = base + upv * (h * 0.15f) + radial * (0.018f * s + 0.01f * front * s);
            // collar points either side of the opening (shirts; the polo's are short and round)
            float thw = wrapAngle(bv.pb);
            float tip = bump(fabsf(thw), top == TOP_POLO ? 0.36f : 0.3f, 0.13f) * (top == TOP_POLO ? 0.55f : 1.f);
            p2 += (-upv * 0.024f * s + radial * 0.004f * s) * tip;
            BVert v = bv;
            v.part = PART_ACC;
            v.mat = MAT_CLOTH;
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
        if (top == TOP_SUIT) {
            zTop = R.zChest - 0.08f * s;
            zBot = zTop - 0.085f * s;
        }
        int nb = nButtons;
        for (int i = 0; i < nb; i++) {
            float z = Lerp(zTop, zBot, nb > 1 ? (float)i / (nb - 1) : 0.f);
            vec3 p, n;
            torsoPoint(c, z, 0.f, p, n);
            SkinW sw = torsoSkinWeights(D, p);
            vec3 bc = top == TOP_POLICE ? vec3(0.75f, 0.6f, 0.25f) : vec3(0.85f, 0.83f, 0.78f);
            addDisc(bm, p + n * (off + 0.0008f), n, 0.0048f * s, 6, 0.0015f, bc, top == TOP_POLICE ? MAT_CHROME : MAT_METAL_PAINTED, sw);
        }
        bm.computeNormals(0, 0);
        o.out.append(bm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }
    // placket / decals
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts) {
        GarmentDef dg;
        dg.parts = parts;
        dg.cov = cov;
        dg.col = dcol;
        dg.mat = mat;
        dg.thick = 0.0035f;
        float eo = extraOff;
        auto base = g.extraFn;
        dg.extraFn = [=](const BVert& v) { return base(v) + eo; };
        dg.smooth = g.smooth;
        dg.hem = false;
        dg.hides = false;
        emitGarment(o, dg);
    };
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
        vec3 p, n;
        torsoPoint(c, R.zChest + 0.07f * s, -0.42f, p, n);
        MeshB bm;
        addDisc(bm, p + n * (off + 0.004f), n, 0.026f * s, 7, 0.003f, srgbToLinear(vec3(0.85f, 0.68f, 0.3f)), MAT_CHROME, torsoSkinWeights(D, p));
        torsoPoint(c, R.zChest + 0.065f * s, 0.42f, p, n);
        vec3 ax = normalize(cross(vec3(0, 0, 1), n)), ay(0, 0, 1);
        addBoxOriented(bm, p + n * (off + 0.004f), ax, ay, n, vec3(0.03f, 0.007f, 0.002f) * s, vec3(0.8f, 0.8f, 0.82f), MAT_CHROME,
                       torsoSkinWeights(D, p));
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
    if (top == TOP_TSHIRT || top == TOP_OVERSIZED) {
        // ribbed neckline band
        decal([=](const BVert& v) {
            if (v.part != PART_TORSO) return -1.f;
            return Min(0.012f * s - fabsf((neckPc - 0.012f - v.pc) * R.torsoLen), baseCov(v));
        }, darker(col, 0.88f), MAT_CLOTH, 0.0012f, 1u << PART_TORSO);
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
        // kangaroo pocket and a hood roll behind the neck
        float zp = R.zWaist - 0.02f * s;
        decal([=, &R](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            return Min(0.1f * R.s - fabsf(v.bp.x) - 0.3f * Max(0.f, v.bp.z - zp), 0.06f * R.s - fabsf(v.bp.z - zp));
        }, darker(col, 0.9f), MAT_CLOTH, 0.004f, 1u << PART_TORSO);
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
            pts.push_back(bv.p + radial * (0.02f + 0.02f * back) * s + vec3(0, 0, 0.012f * s));
            rad.push_back((0.016f + 0.018f * back) * s);
            WAcc acc;
            acc.add(B_CHEST, 0.7f);
            acc.add(B_NECK, 0.3f);
            sws.push_back(acc.finish());
        }
        addTube(o.out, pts, rad, 8, false, col, MAT_CLOTH, sws);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
    }

    if (top == TOP_SUIT) {
        // collar of the shirt
        // (jacket was emitted as the base shell above with the V opening defined below via decal lapels)
        float zV = R.zChest - 0.07f * s;
        decal([=, &R](const BVert& v) {
            if (v.part != PART_TORSO || v.bp.y < 0.f) return -1.f;
            float h = Saturate((v.bp.z - zV) / (D.zNeckFront - zV));
            float vx = Lerp(0.0f, 0.075f, h) * R.s;
            float lw = Lerp(0.02f, 0.05f, h) * R.s;
            float dist = fabsf(v.bp.x) - vx;
            return Min(Min(dist, lw - dist), v.bp.z - zV + 0.01f * R.s);
        }, darker(col, 0.85f), MAT_CLOTH, 0.004f, 1u << PART_TORSO);
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
                // open back scoop and straps
                float back = (v.bp.y < 0.f) ? (R.zWaist + 0.02f * R.s - v.bp.z) + (fabsf(v.bp.x) - 0.09f * R.s) * 0.f : 1.f;
                float strap = covStraps(R, v, 0.085f * R.s, 0.018f * R.s, 0.975f);
                topc = Max(Min(topc, back), v.pc > 0.72f ? strap : -1.f);
            }
            return Min(open, topc);
        };
        emitGarment(o, b);
        if (!skirt && bot == BOT_BIKINI && d.top != TOP_ONEPIECE) return;
    }
    if (skirt) {
        // skirt / dress: fitted part to the hip from the shell, then a flared cone around both legs
        vec3 scol = d.top == TOP_SUNDRESS ? d.topColor : col;
        float zS = d.top == TOP_SUNDRESS ? R.zWaist + 0.03f * s : R.zBeltHigh;
        float hemZ = R.zKnee + (d.top == TOP_SUNDRESS ? 0.02f : (rng.chance(0.5f) ? 0.1f : -0.03f)) * s;
        GarmentDef top;
        top.parts = 1u << PART_TORSO;
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
        emitGarment(o, top);
        // cone
        MeshB sk;
        const int NS = 32, NR = 7;
        std::vector<u32> rows[NR];
        float zStart = zc;
        vec3 p0s[NS], n0s[NS];
        for (int k = 0; k < NS; k++) {
            float th = kTwoPi * k / NS;
            torsoPoint(c, zStart, th, p0s[k], n0s[k], MK_TORSO);
        }
        float flareK = d.top == TOP_SUNDRESS ? 0.22f : 0.14f;
        for (int r = 0; r < NR; r++) {
            float t = (float)r / (NR - 1);
            float z = Lerp(zStart, hemZ, t);
            rows[r].resize(NS);
            for (int k = 0; k < NS; k++) {
                float th = kTwoPi * k / NS;
                vec3 radial = normalize(vec3(sinf(th), cosf(th) * 0.8f, 0.f));
                vec3 p = p0s[k] + radial * (0.007f * s + (zStart - z) * flareK) ;
                p.z = z;
                // keep outside the legs: at least the hip half width at the sides
                BVert v;
                v.p = p;
                v.bp = p;
                v.n = radial;
                v.t = vec3(cosf(th), -sinf(th), 0);
                v.uv = vec2(uWrap(th, kPi, 0.18f * s), z);
                v.uPer = kTwoPi * 0.18f * s;
                v.col = scol;
                v.mat = MAT_CLOTH;
                v.part = PART_GARMENT;
                WAcc acc;
                float legW = t * 0.55f;
                float side = sinf(th);
                acc.add(B_PELVIS, 1.f - legW);
                acc.add(B_THIGH_R, legW * Saturate(0.5f + side * 0.9f));
                acc.add(B_THIGH_L, legW * Saturate(0.5f - side * 0.9f));
                v.sw = acc.finish();
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
    {
        // folds: behind the knees, stacking on the shoe for full-length trousers, whiskers on jeans
        FoldSpec fs;
        fs.amp = bot == BOT_LEGGINGS ? 0.f : Lerp(0.55f, 1.25f, Saturate(loose / 0.016f));
        fs.legCuffZ = zCuff;
        fs.legLong = zCuff < R.zAnkle + 0.05f * s && bot != BOT_LEGGINGS;
        fs.denim = g.mat == MAT_DENIM;
        fs.seed = d.seed * 5u + 2u;
        const BuildCtx* cp = &c;
        auto baseExtra = g.extraFn;
        g.extraFn = [=](const BVert& v) {
            float off, sh;
            fabricFold(*cp, fs, v, off, sh);
            return baseExtra(v) + off;
        };
        g.colFn = [=](const BVert& v, vec3 cc) {
            float off, sh;
            fabricFold(*cp, fs, v, off, sh);
            return cc * (1.f - sh);
        };
    }
    if (g.smooth < 1) g.smooth = 1;
    emitGarment(o, g);
    o.botTorsoOff = g.thick + torsoE + (belt || dutyBelt ? 0.009f : 0.004f);
    o.botTopZ = zt;
    auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff, u32 parts, bool hem = false) {
        GarmentDef dg;
        dg.parts = parts;
        dg.cov = cov;
        dg.col = dcol;
        dg.mat = mat;
        dg.thick = g.thick;
        float eo = extraOff;
        auto base = g.extraFn;
        dg.extraFn = [=](const BVert& v) { return base(v) + eo; };
        dg.smooth = g.smooth;
        dg.hem = hem;
        dg.hides = false;
        emitGarment(o, dg);
    };
    CovFn base = g.cov;
    // waistband
    decal([=, &R](const BVert& v) { return v.part == PART_TORSO ? Min(zt - v.bp.z, v.bp.z - (zt - 0.035f * R.s)) : -1.f; }, darker(col, 0.9f), g.mat, 0.0015f,
          1u << PART_TORSO);
    if (g.mat == MAT_DENIM) {
        // outer seams and back pockets
        decal([=](const BVert& v) {
            if (v.part != PART_LEG) return -1.f;
            return Min(0.004f - fabsf(wrapAngle(v.pb - kHalfPi)) * 0.05f, base(v));
        }, vec3(0.85f, 0.8f, 0.6f), MAT_DENIM, 0.0008f, 1u << PART_LEG);
    } else if (bot != BOT_LEGGINGS) {
        // outer seam of plain trousers / shorts
        decal([=](const BVert& v) {
            if (v.part != PART_LEG) return -1.f;
            return Min(0.0022f - fabsf(wrapAngle(v.pb - kHalfPi)) * 0.05f, base(v));
        }, darker(col, 0.82f), g.mat, 0.0007f, 1u << PART_LEG);
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
            }, darker(col, g.mat == MAT_DENIM ? 0.75f : 0.8f), g.mat, 0.0009f, 1u << PART_TORSO);
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
        float sockTop = ankZ + (kind == SHOE_DRESS ? 0.12f : (rng.chance(0.5f) ? 0.035f : 0.1f)) * s;
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
    switch (kind) {
        case SHOE_SNEAKER: g.mat = MAT_CLOTH; soleMat = MAT_CLOTH; soleCol = rng.chance(0.7f) ? vec3(0.9f) : col * 0.5f; break;
        case SHOE_RUNNER: g.mat = MAT_CLOTH; soleMat = MAT_CLOTH; soleCol = vec3(0.92f); break;
        case SHOE_DRESS: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = ankZ - 0.03f * s; cutBack = ankZ - 0.015f * s; cutFront = ankZ - 0.022f * s;
            soleCol = vec3(1.f); g.thick = 0.004f; break;
        case SHOE_BOOT: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = cutBack = cutFront = ankZ + 0.11f * s; g.thick = 0.006f; break;
        case SHOE_FLATS: g.mat = MAT_LEATHER; g.col = col * 3.f; cutSide = ankZ - 0.035f * s; cutBack = ankZ - 0.02f * s; cutFront = ankZ - 0.038f * s;
            g.thick = 0.003f; break;
        case SHOE_SANDAL: g.mat = MAT_CLOTH; break;
        default: break;
    }
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
            // two straps from the toe gap to the sides of the forefoot
            float best = -1.f;
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
        emitGarment(o, g);
        auto decal = [&](CovFn cov, vec3 dcol, u8 mat, float extraOff) {
            GarmentDef dg;
            dg.parts = 1u << PART_LEG;
            dg.cov = cov;
            dg.col = dcol;
            dg.mat = mat;
            dg.thick = g.thick + extraOff;
            dg.smooth = g.smooth;
            dg.hem = false;
            dg.hides = false;
            emitGarment(o, dg);
        };
        CovFn base = g.cov;
        if (kind == SHOE_SNEAKER || kind == SHOE_RUNNER) {
            // side panel stripe and laces over the instep
            vec3 accent = rng.chance(0.5f) ? vec3(0.9f) - col * 0.7f : srgbToLinear(vec3(rng.f(), rng.f() * 0.5f, rng.f()));
            // heel counter and toe cap in the accent color
            decal([=](const BVert& v) {
                if (v.part != PART_LEG || v.bp.z > ankZ) return -1.f;
                vec3 ank = D.J[v.side ? B_FOOT_R : B_FOOT_L];
                float y = v.bp.y - ank.y;
                float heel = Min(-0.01f * s - y, ankZ - 0.01f * s - v.bp.z);
                float toe = y - (D.toeFwd - 0.045f * s);
                return Max(heel, toe);
            }, saturate(accent), MAT_CLOTH, 0.0008f);
            for (int li = 0; li < 4; li++) {
                decal([=](const BVert& v) {
                    if (v.part != PART_LEG || v.bp.z > ankZ - 0.004f * s) return -1.f;
                    vec3 ank = D.J[v.side ? B_FOOT_R : B_FOOT_L];
                    float y = v.bp.y - ank.y;
                    float yl = 0.03f * s + li * 0.016f * s;
                    return Min(Min(0.0035f * s - fabsf(y - yl), 0.015f * s - fabsf(v.bp.x - ank.x)), cosf(v.pb) - 0.6f);
                }, vec3(0.92f), MAT_CLOTH, 0.0012f);
            }
        }
        (void)base;
    }
    // soles: a smooth last-shaped outline (rounded heel, widest at the ball, rounded toe box a little past the toes),
    // extruded to the ground with a rounded bottom edge and a toe spring; sneakers / runners get a white midsole over
    // a darker outsole, dress shoes a thin sole with a heel block (the arch lifts off the ground), boots a thick lug
    // sole. Laced shoes get crossed laces over the tongue and a bow.
    const bool laced = kind == SHOE_SNEAKER || kind == SHOE_RUNNER || kind == SHOE_BOOT;
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
        const int NS = 40;
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
        auto archLift = [&](float t) { return kind == SHOE_DRESS ? 0.008f * s * bump(t, 0.38f, 0.08f) : 0.f; };
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
                if (r == 1 && kind == SHOE_DRESS) v.col = soleCol * 0.7f;   // welt stitching line
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
        if (laced) {
            // laces over the tongue: eyelet rows either side of the opening, crossed ribbons between them, a bow
            const int nRows = kind == SHOE_BOOT ? 7 : 5;
            float y0 = ank.y + (kind == SHOE_BOOT ? 0.005f : 0.035f) * s, y1 = ank.y + 0.105f * s;
            vec3 lc = rng.chance(0.6f) ? vec3(0.9f) : col * 0.8f;
            if (kind == SHOE_BOOT) lc = vec3(0.12f, 0.07f, 0.03f);
            std::vector<vec3> L(nRows), Rr(nRows);
            std::vector<vec3> Nn(nRows);
            auto upperPt = [&](float x, float y) {
                vec3 o0(x, y, lift + 0.01f * s);
                float t = c.sdf.castOut(o0, vec3(0, 0, 1), FM | (sd ? MK_LEG_R : MK_LEG_L), 0.12f * s);
                vec3 p = o0 + vec3(0, 0, t);
                vec3 gr = c.sdf.grad(p, FM | (sd ? MK_LEG_R : MK_LEG_L));
                vec3 nn = length2(gr) > 1e-12f ? normalize(gr) : vec3(0, 0, 1);
                return std::make_pair(p + nn * (upperThick + 0.0022f * s), nn);
            };
            for (int i = 0; i < nRows; i++) {
                float u = (float)i / (nRows - 1);
                float y = Lerp(y1, y0, u);
                float halfGap = Lerp(0.007f, 0.011f, u) * s;
                auto pl = upperPt(ank.x - halfGap, y), pr = upperPt(ank.x + halfGap, y);
                L[i] = pl.first;
                Rr[i] = pr.first;
                Nn[i] = normalize(pl.second + pr.second);
            }
            MeshB lm;
            SkinW swL = skin2(B_FOOT_L + o4, B_TOE_L + o4, 0.15f);
            auto ribbon = [&](vec3 a0, vec3 b0, vec3 n0) {
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
                    v.sw = swL;
                    lm.add(v);
                }
                vec3 fn = cross(q[1] - q[0], q[2] - q[0]);
                if (dot(fn, n0) >= 0.f) lm.quad(base, base + 1, base + 2, base + 3);
                else lm.quad(base, base + 3, base + 2, base + 1);
            };
            for (int i = 0; i + 1 < nRows; i++) {
                vec3 n0 = Nn[i];
                ribbon(L[i], Rr[i + 1] + n0 * 0.0008f * s, n0);
                ribbon(Rr[i], L[i + 1] + n0 * 0.0016f * s, n0);
            }
            ribbon(L[0], Rr[0], Nn[0]);   // bottom bar
            // bow at the top row: two loops and two ends
            vec3 top = (L[nRows - 1] + Rr[nRows - 1]) * 0.5f + Nn[nRows - 1] * 0.002f * s;
            for (int e = 0; e < 2; e++) {
                float ex = e ? 1.f : -1.f;
                std::vector<vec3> pts;
                std::vector<float> rad;
                std::vector<SkinW> sws;
                for (int k = 0; k <= 8; k++) {
                    float a = kPi * k / 8.f;
                    pts.push_back(top + vec3(ex * (0.012f * s) * sinf(a), (-0.006f * s) * (1.f - cosf(a)) * 0.5f + 0.004f * s * sinf(a), 0.002f * s * sinf(a)));
                    rad.push_back(0.0013f * s);
                    sws.push_back(swL);
                }
                addTube(lm, pts, rad, 4, false, lc, MAT_CLOTH, sws);
                std::vector<vec3> tail = {top, top + vec3(ex * 0.006f * s, -0.012f * s, -0.004f * s), top + vec3(ex * 0.009f * s, -0.022f * s, -0.012f * s)};
                std::vector<float> tr(3, 0.0012f * s);
                std::vector<SkinW> tsw(3, swL);
                addTube(lm, tail, tr, 4, false, lc, MAT_CLOTH, tsw);
            }
            o.out.append(lm);
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

static void buildGlasses(OutfitCtx& o, const CharacterDesc& d) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const HeadInfo& H = c.head;
    if (d.glasses < 0) return;
    const float hs = D.headS;
    MeshB gm;
    SkinW sw = skin1(B_HEAD);
    bool sun = d.glasses == GL_SUN, avi = d.glasses == GL_AVIATOR, read = d.glasses == GL_READING;
    Rng rng(hash32(d.seed * 53u + 17u));
    vec3 frameCol = avi ? vec3(0.85f, 0.75f, 0.5f) : (read ? srgbToLinear(vec3(0.25f, 0.18f, 0.12f)) : vec3(1.f));
    u8 frameMat = avi ? MAT_CHROME : (read ? MAT_METAL_PAINTED : MAT_PLASTIC);
    float lw = (sun ? 0.026f : (avi ? 0.027f : 0.024f)) * hs, lh = (sun ? 0.019f : (avi ? 0.023f : 0.016f)) * hs;
    std::vector<vec3> rimPts[2];
    vec3 lensC[2];
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 e = H.eyeC[sd];
        vec3 cen = e + vec3(sx * 0.002f * hs, H.eyeR + 0.012f * hs, 0.002f * hs);
        lensC[sd] = cen;
        const int NL = 16;
        for (int k = 0; k < NL; k++) {
            float a = kTwoPi * k / NL;
            float cx = cosf(a), cy = sinf(a);
            float rx = lw * 0.5f, ry = lh * 0.5f;
            if (avi && cy < 0.f) { ry *= 1.25f; rx *= 1.f + 0.1f * cy; }
            if (sun) { float sq = powf(fabsf(cx), 0.7f) * Sign(cx); cx = sq; }
            // wrap slightly around the face
            float x = cx * rx;
            vec3 p = cen + vec3(x, -fabsf(x) * 0.25f * (sx * x > 0.f ? 1.f : 0.3f), cy * ry);
            rimPts[sd].push_back(p);
        }
        // lens
        if (!read) {
            BVert lc;
            lc.p = cen + vec3(0, 0.0015f * hs, 0);
            lc.bp = lc.p;
            lc.n = vec3(0, 1, 0);
            lc.t = vec3(1, 0, 0);
            lc.col = avi ? vec3(0.3f, 0.2f, 0.1f) : vec3(0.05f);
            lc.mat = MAT_CAR_GLASS;
            lc.part = PART_ACC;
            lc.sw = sw;
            u32 ci = gm.add(lc);
            std::vector<u32> ring;
            for (auto& p : rimPts[sd]) {
                BVert v = lc;
                v.p = p;
                v.n = normalize(vec3(0, 1, 0) + (p - cen) * 8.f);
                ring.push_back(gm.add(v));
            }
            for (size_t k = 0; k < ring.size(); k++) {
                u32 a = ring[k], b = ring[(k + 1) % ring.size()];
                vec3 nn = cross(gm.v[a].p - lc.p, gm.v[b].p - lc.p);
                if (dot(nn, vec3(0, 1, 0)) >= 0.f) gm.tri(ci, a, b);
                else gm.tri(ci, b, a);
            }
        }
        // rim tube
        std::vector<float> rad(rimPts[sd].size(), (avi ? 0.0011f : (sun ? 0.0024f : 0.0017f)) * hs);
        std::vector<SkinW> sws(rimPts[sd].size(), sw);
        addTube(gm, rimPts[sd], rad, 5, true, frameCol * (sun ? 0.05f : 1.f), frameMat, sws, vec3(0, 1, 0));
    }
    // bridge
    {
        std::vector<vec3> pts = {rimPts[0][0], (rimPts[0][0] + rimPts[1][8]) * 0.5f + vec3(0, 0.003f * hs, 0.004f * hs), rimPts[1][8]};
        pts[0] = lensC[0] + vec3(0.5f * lw, 0, 0.003f * hs);
        pts[2] = lensC[1] - vec3(0.5f * lw, 0, -0.003f * hs);
        pts[1] = (pts[0] + pts[2]) * 0.5f + vec3(0, 0.002f * hs, 0.002f * hs);
        std::vector<float> rad(3, (avi ? 0.0011f : 0.0018f) * hs);
        std::vector<SkinW> sws(3, sw);
        addTube(gm, pts, rad, 5, false, frameCol * (sun ? 0.05f : 1.f), frameMat, sws, vec3(0, 1, 0));
        if (avi) {
            for (auto& p : pts) p.z += 0.009f * hs;
            addTube(gm, pts, rad, 5, false, frameCol, frameMat, sws, vec3(0, 1, 0));
        }
    }
    // temples to the ears
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 hinge = lensC[sd] + vec3(sx * lw * 0.55f, -0.004f * hs, 0.004f * hs);
        vec3 ear = H.earPos[sd] + vec3(sx * 0.004f * hs, 0.008f * hs, 0.018f * hs);
        vec3 back = ear + vec3(-sx * 0.003f * hs, -0.02f * hs, -0.012f * hs);
        // keep outside the head: push the midpoint outwards
        vec3 mid = lerp(hinge, ear, 0.5f);
        vec3 dir = normalize(mid - H.C);
        float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.2f * hs);
        mid = H.C + dir * (t + 0.004f * hs);
        std::vector<vec3> pts = {hinge, mid, ear, back};
        std::vector<float> rad(4, (sun ? 0.002f : 0.0012f) * hs);
        std::vector<SkinW> sws(4, sw);
        addTube(gm, pts, rad, 4, false, frameCol * (sun ? 0.05f : 1.f), frameMat, sws);
    }
    gm.computeNormals(0, 0);
    o.out.append(gm);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
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
    // necklace / chain
    bool chain = (d.role == 2 && rng.chance(0.7f)) || (d.role == 0 && rng.chance(fem ? 0.3f : 0.12f)) || (d.role == 4 && rng.chance(0.25f));
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
    if ((fem && rng.chance(0.6f)) || (!fem && rng.chance(0.08f))) {
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
    if (!longSleeve && ((d.role == 3 && rng.chance(0.8f)) || rng.chance(0.3f))) {
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

void buildOutfit(BuildCtx& c, MeshB& out, std::vector<u8>& hideTri) {
    const CharacterDesc& d = *c.d;
    Ref R;
    makeRef(c, R);
    MeshB raw;
    OutfitCtx o(c, raw, hideTri);
    buildHairLayer(o);
    // inner layers first: bottoms (unless tucked shirts: then tops first so pants cover them)
    bool tucked = d.top == TOP_DRESS_SHIRT || d.top == TOP_POLICE || d.top == TOP_MEDIC;
    if (d.top == TOP_BIKINI) {
        // bikini top: cups + neck straps + back band
        GarmentDef g;
        g.parts = 1u << PART_TORSO;
        g.col = d.topColor;
        g.thick = 0.002f;
        g.smooth = 0;
        g.hideMargin = 0.005f;
        g.cov = [=, &R](const BVert& v) -> float {
            if (v.part != PART_TORSO) return -1.f;
            float best = -1.f;
            for (int sd = 0; sd < 2; sd++) {
                float r = R.breastR * 1.05f;
                vec3 bc = R.breast[sd];
                best = Max(best, r - length(vec3(v.bp.x - bc.x, (v.bp.y - bc.y) * 0.5f, v.bp.z - bc.z)));
            }
            float band = 0.011f * R.s - fabsf(v.bp.z - (R.breast[0].z - R.breastR * 0.8f));
            float strap = covStraps(R, v, 0.06f * R.s, 0.008f * R.s, 0.99f);
            if (v.bp.y > 0.f) strap = Min(strap, v.bp.z - R.breast[0].z);
            return Max(Max(best, band), strap);
        };
        emitGarment(o, g);
    }
    if (d.top == TOP_SUNDRESS) {
        // dress bodice with straps
        GarmentDef g;
        g.parts = 1u << PART_TORSO;
        g.col = d.topColor;
        g.thick = 0.003f;
        g.smooth = 1;
        g.cov = [=, &R](const BVert& v) -> float {
            if (v.part != PART_TORSO) return -1.f;
            float body = Min(v.bp.z - R.zWaist, (0.8f - v.pc) * R.torsoLen);
            float strap = covStraps(R, v, 0.09f * R.s, 0.012f * R.s, 0.975f);
            return Max(body, strap);
        };
        g.extraFn = [](const BVert&) { return 0.003f; };
        emitGarment(o, g);
    }
    if (tucked) {
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
    buildShoes(o, R, d);
    buildHat(o, d);
    buildGlasses(o, d);
    buildJewelry(o, d);
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
