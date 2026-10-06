// Mesh decimation for the character LODs: quadric-error (Garland-Heckbert) half-edge collapses on the build mesh.
// A half-edge collapse u -> v removes u and keeps v with all its attributes (position, normal, colour, uv, material,
// skin weights), so the result never needs attribute interpolation and every surviving vertex is skinned exactly as
// in the full mesh. Costs add colour / skin weight / normal differences to the geometric error; open boundaries (hems,
// eye fissures, mouth slit, piece borders) are held by perpendicular constraint planes and may only collapse along
// themselves; border vertices shared by two pieces (coincident twins) are locked so junctions never crack. Collapses
// that flip or sliver a triangle or break the link condition (non-manifold result) are rejected.
// Speed (it runs for every character the game builds): the cost inputs are packed per vertex, the heap holds one entry
// per edge (its cheaper direction; the other is tried once if that one is rejected), stale entries are recognised by
// vertex stamps, and neighbour sets use generation marks instead of sorting.
#include "anim_internal.h"
#include <algorithm>
#include <tuple>

namespace Anim {
namespace detail {

struct Quadric {
    double q[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // a2 ab ac ad b2 bc bd c2 cd d2
    void addPlane(double a, double b, double c, double d, double w) {
        q[0] += w * a * a; q[1] += w * a * b; q[2] += w * a * c; q[3] += w * a * d;
        q[4] += w * b * b; q[5] += w * b * c; q[6] += w * b * d;
        q[7] += w * c * c; q[8] += w * c * d; q[9] += w * d * d;
    }
    void add(const Quadric& o) {
        for (int i = 0; i < 10; i++) q[i] += o.q[i];
    }
    double eval(vec3 p) const {
        double x = p.x, y = p.y, z = p.z;
        return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y +
               q[7] * z * z + 2 * q[8] * z + q[9];
    }
};

static float skinDiff(const SkinW& a, const SkinW& b) {
    float d = 0.f;
    for (int i = 0; i < 4; i++) {
        float wb = 0.f;
        for (int j = 0; j < 4; j++)
            if (b.b[j] == a.b[i]) wb += b.w[j];
        d += fabsf(a.w[i] - Min(wb, 1.f)) * (a.w[i] > 0.f ? 1.f : 0.f);
        float wa = 0.f;
        for (int j = 0; j < 4; j++)
            if (a.b[j] == b.b[i]) wa += a.w[j];
        if (wa <= 0.f) d += b.w[i];
    }
    return d * 0.5f;
}

void decimateMesh(MeshB& m, int targetTris, const float* partWeight) {
    const u32 NV = (u32)m.v.size();
    const u32 NT = (u32)(m.idx.size() / 3);
    if ((int)NT <= targetTris || NV < 4) return;
    std::vector<u32> T(m.idx);
    std::vector<u8> triAlive(NT, 1), vAlive(NV, 1), vBound(NV, 0), vLock(NV, 0);
    std::vector<u32> stamp(NV, 0);
    // vertex -> triangles (sized exactly up front)
    std::vector<std::vector<u32>> vt(NV);
    {
        std::vector<u32> cnt(NV, 0);
        for (u32 t = 0; t < NT; t++) {
            u32 a = T[t * 3], b = T[t * 3 + 1], c = T[t * 3 + 2];
            if (a == b || b == c || a == c) continue;
            cnt[a]++;
            cnt[b]++;
            cnt[c]++;
        }
        for (u32 i = 0; i < NV; i++) vt[i].reserve(cnt[i] + 4);
    }
    for (u32 t = 0; t < NT; t++) {
        u32 a = T[t * 3], b = T[t * 3 + 1], c = T[t * 3 + 2];
        if (a == b || b == c || a == c) {
            triAlive[t] = 0;
            continue;
        }
        vt[a].push_back(t);
        vt[b].push_back(t);
        vt[c].push_back(t);
    }
    // the attributes the costs read, packed (the build vertex is large: reading it per candidate misses the cache)
    std::vector<vec3> P(NV), Nr(NV), Cl(NV);
    std::vector<SkinW> SW(NV);
    std::vector<u16> key(NV);
    std::vector<float> PW(NV, 1.f);
    for (u32 i = 0; i < NV; i++) {
        const BVert& b = m.v[i];
        P[i] = b.p;
        Nr[i] = b.n;
        Cl[i] = b.col;
        SW[i] = b.sw;
        key[i] = (u16)((b.mat << 8) | b.part);
        if (partWeight) PW[i] = partWeight[Min((int)b.part, (int)PART_COUNT - 1)];
    }
    // edge use counts -> boundary / non-manifold edges
    struct EdgeUse {
        u64 key;
        u32 tri;
    };
    std::vector<EdgeUse> edges;
    edges.reserve(NT * 3);
    auto ekey = [](u32 a, u32 b) { return a < b ? ((u64)a << 32) | b : ((u64)b << 32) | a; };
    for (u32 t = 0; t < NT; t++) {
        if (!triAlive[t]) continue;
        for (int k = 0; k < 3; k++) edges.push_back({ekey(T[t * 3 + k], T[t * 3 + (k + 1) % 3]), t});
    }
    std::sort(edges.begin(), edges.end(), [](const EdgeUse& x, const EdgeUse& y) { return x.key < y.key; });
    std::vector<Quadric> Q(NV);
    double meanArea = 0.0;
    int cntA = 0;
    for (u32 t = 0; t < NT; t++) {
        if (!triAlive[t]) continue;
        vec3 p0 = P[T[t * 3]], p1 = P[T[t * 3 + 1]], p2 = P[T[t * 3 + 2]];
        vec3 n = cross(p1 - p0, p2 - p0);
        float area2 = length(n);
        meanArea += 0.5 * area2;
        cntA++;
        if (area2 < 1e-14f) continue;
        n = n / area2;
        // area weighted planes (units: m^2 * m^2), normalized below by a typical triangle area
        double w = area2 * 0.5;
        for (int k = 0; k < 3; k++) Q[T[t * 3 + k]].addPlane(n.x, n.y, n.z, -dot(n, p0), w);
    }
    meanArea = Max(meanArea / Max(cntA, 1), 1e-9);
    // boundary constraint planes (perpendicular to the face through the border edge) and locks
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) j++;
        u32 a = (u32)(edges[i].key >> 32), b = (u32)(edges[i].key & 0xffffffffu);
        if (j - i == 1) {
            vBound[a] = vBound[b] = 1;
            u32 t = edges[i].tri;
            vec3 p0 = P[T[t * 3]], p1 = P[T[t * 3 + 1]], p2 = P[T[t * 3 + 2]];
            vec3 fn = cross(p1 - p0, p2 - p0);
            vec3 e = P[b] - P[a];
            vec3 pn = cross(e, fn);
            float l = length(pn);
            if (l > 1e-12f) {
                pn = pn / l;
                double w = 8.0 * meanArea * Max(1.0, (double)length2(e) / meanArea);
                Q[a].addPlane(pn.x, pn.y, pn.z, -dot(pn, P[a]), w);
                Q[b].addPlane(pn.x, pn.y, pn.z, -dot(pn, P[a]), w);
            }
        } else if (j - i > 2) {
            vLock[a] = vLock[b] = 1;   // non-manifold
        }
        i = j;
    }
    // border vertices with a coincident twin (pieces meeting at duplicated vertices) stay put
    {
        std::vector<u32> bv;
        for (u32 i = 0; i < NV; i++)
            if (vBound[i]) bv.push_back(i);
        auto qk = [&](u32 i) {
            vec3 p = P[i];
            return std::make_tuple((int)floorf(p.x * 2e4f), (int)floorf(p.y * 2e4f), (int)floorf(p.z * 2e4f));
        };
        std::sort(bv.begin(), bv.end(), [&](u32 x, u32 y) { return qk(x) < qk(y); });
        for (size_t i = 0; i + 1 < bv.size(); i++)
            if (qk(bv[i]) == qk(bv[i + 1])) vLock[bv[i]] = vLock[bv[i + 1]] = 1;
    }
    const double errScale = 1.0 / meanArea;   // error ~ squared distance in m^2
    // the error of each vertex's own quadric at its own position (refreshed when the quadric grows)
    std::vector<double> selfErr(NV);
    for (u32 i = 0; i < NV; i++) selfErr[i] = Q[i].eval(P[i]);
    auto isBoundaryEdge = [&](u32 u, u32 v) {
        int n = 0;
        for (u32 t : vt[u]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            if (tr[0] == v || tr[1] == v || tr[2] == v) n++;
        }
        return n == 1;
    };
    auto evalCand = [&](u32 u, u32 v, float& cost) -> bool {
        if (!vAlive[u] || !vAlive[v] || vLock[u]) return false;
        if (key[u] != key[v]) return false;
        if (vBound[u] && (!vBound[v] || !isBoundaryEdge(u, v))) return false;
        const vec3 pv = P[v];
        double geo = Max(0.0, Q[u].eval(pv) + selfErr[v]) * errScale;
        vec3 dc = Cl[u] - Cl[v];
        double col = (double)length2(dc) * 2e-4;
        double sk = (double)skinDiff(SW[u], SW[v]) * 3e-4;
        double nrm = (1.0 - Clamp((double)dot(Nr[u], Nr[v]), -1.0, 1.0)) * 2e-5;
        double len = (double)length2(P[u] - pv) * 0.02;   // mild preference for short edges (even tessellation)
        cost = (float)((geo + col + sk + nrm + len) * PW[u]);
        return true;
    };
    // min-heap of collapse candidates (one per edge: its cheaper direction; the other is tried if that one fails)
    struct HE {
        float cost;
        u32 u, v, su, sv;
        u32 retry;   // the other direction of an edge whose cheaper one failed (tried once, not bounced back)
    };
    auto heCmp = [](const HE& a, const HE& b) { return a.cost > b.cost; };
    std::vector<HE> heap;
    heap.reserve(edges.size() / 2 + NV);
    // both directions of an edge at once: everything but the quadric term (and the locks) is symmetric
    auto pushPair = [&](u32 a, u32 b, bool heapify) {
        if (!vAlive[a] || !vAlive[b] || key[a] != key[b]) return;
        bool va = !vLock[a], vb = !vLock[b];
        if (vBound[a] || vBound[b]) {
            const bool be = vBound[a] && vBound[b] && isBoundaryEdge(a, b);
            if (vBound[a] && !be) va = false;
            if (vBound[b] && !be) vb = false;
        }
        if (!va && !vb) return;
        vec3 dc = Cl[a] - Cl[b];
        const double sym = (double)length2(dc) * 2e-4 + (double)skinDiff(SW[a], SW[b]) * 3e-4 +
                           (1.0 - Clamp((double)dot(Nr[a], Nr[b]), -1.0, 1.0)) * 2e-5 + (double)length2(P[a] - P[b]) * 0.02;
        float ca = 0.f, cb = 0.f;
        if (va) ca = (float)((Max(0.0, Q[a].eval(P[b]) + selfErr[b]) * errScale + sym) * PW[a]);
        if (vb) cb = (float)((Max(0.0, Q[b].eval(P[a]) + selfErr[a]) * errScale + sym) * PW[b]);
        HE e = (va && (!vb || ca <= cb)) ? HE{ca, a, b, stamp[a], stamp[b], 0u} : HE{cb, b, a, stamp[b], stamp[a], 0u};
        heap.push_back(e);
        if (heapify) std::push_heap(heap.begin(), heap.end(), heCmp);
    };
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) j++;
        pushPair((u32)(edges[i].key >> 32), (u32)(edges[i].key & 0xffffffffu), false);
        i = j;
    }
    std::make_heap(heap.begin(), heap.end(), heCmp);
    // neighbour marks (generation counters instead of sorted sets)
    std::vector<u32> mark(NV, 0);
    u32 gen = 0;
    int alive = 0;
    for (u32 t = 0; t < NT; t++) alive += triAlive[t];
    while (alive > targetTris && !heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), heCmp);
        HE c = heap.back();
        heap.pop_back();
        u32 u = c.u, v = c.v;
        if (!vAlive[u] || !vAlive[v] || stamp[u] != c.su || stamp[v] != c.sv) continue;
        // link condition: common neighbours of u and v == triangles on the edge (1 border / 2 interior)
        ++gen;
        int edgeTris = 0;
        for (u32 t : vt[u]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            edgeTris += tr[0] == v || tr[1] == v || tr[2] == v;
            for (int k = 0; k < 3; k++)
                if (tr[k] != u) mark[tr[k]] = gen;
        }
        if (edgeTris == 0) continue;
        int common = 0;
        const u32 genU = gen;
        ++gen;
        for (u32 t : vt[v]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            for (int k = 0; k < 3; k++) {
                u32 w = tr[k];
                if (w == v || mark[w] == gen) continue;
                if (mark[w] == genU) common++;
                mark[w] = gen;
            }
        }
        // (u itself is a neighbour of v and marked by neither side's "other" set: it is not counted)
        bool ok = common == edgeTris;
        // geometry checks on the triangles that survive (u replaced by v): no flips, no slivers
        const vec3 pv = P[v];
        if (ok)
            for (u32 t : vt[u]) {
                if (!triAlive[t]) continue;
                const u32* tr = &T[t * 3];
                if (tr[0] == v || tr[1] == v || tr[2] == v) continue;
                vec3 p[3], q[3];
                for (int k = 0; k < 3; k++) {
                    p[k] = P[tr[k]];
                    q[k] = tr[k] == u ? pv : p[k];
                }
                vec3 n0 = cross(p[1] - p[0], p[2] - p[0]), n1 = cross(q[1] - q[0], q[2] - q[0]);
                float l0 = length(n0), l1 = length(n1);
                if (l1 < 1e-12f || dot(n0, n1) < 0.25f * l0 * l1) {
                    ok = false;
                    break;
                }
                // sliver: area vs longest edge squared
                float e2 = Max(length2(q[1] - q[0]), Max(length2(q[2] - q[1]), length2(q[0] - q[2])));
                if (l1 < 0.04f * e2) {
                    ok = false;
                    break;
                }
            }
        if (!ok) {
            // the other direction of this edge gets its chance
            float cr;
            if (!c.retry && evalCand(v, u, cr)) {
                heap.push_back(HE{cr, v, u, stamp[v], stamp[u], 1u});
                std::push_heap(heap.begin(), heap.end(), heCmp);
            }
            continue;
        }
        // apply
        for (u32 t : vt[u]) {
            if (!triAlive[t]) continue;
            u32* tr = &T[t * 3];
            if (tr[0] == v || tr[1] == v || tr[2] == v) {
                triAlive[t] = 0;
                alive--;
                continue;
            }
            for (int k = 0; k < 3; k++)
                if (tr[k] == u) tr[k] = v;
            vt[v].push_back(t);
        }
        vAlive[u] = 0;
        vt[u].clear();
        Q[v].add(Q[u]);
        selfErr[v] = Q[v].eval(P[v]);
        if (vBound[u]) vBound[v] = 1;
        // compact v's triangle list (u's triangles are new to it, so no duplicates) and refresh the neighbourhood
        std::vector<u32>& lv = vt[v];
        size_t w = 0;
        for (size_t i = 0; i < lv.size(); i++)
            if (triAlive[lv[i]]) lv[w++] = lv[i];
        lv.resize(w);
        // only costs involving v changed (validity of the others is re-checked when they are popped)
        stamp[v]++;
        ++gen;
        for (u32 t : lv) {
            const u32* tr = &T[t * 3];
            for (int k = 0; k < 3; k++) {
                u32 x = tr[k];
                if (x == v || mark[x] == gen) continue;
                mark[x] = gen;
                pushPair(v, x, true);
            }
        }
    }
    // compact
    std::vector<u32> remap(NV, 0xffffffffu);
    MeshB out;
    out.v.reserve(NV);
    for (u32 t = 0; t < NT; t++) {
        if (!triAlive[t]) continue;
        for (int k = 0; k < 3; k++) {
            u32 a = T[t * 3 + k];
            if (remap[a] == 0xffffffffu) remap[a] = out.add(m.v[a]);
            out.idx.push_back(remap[a]);
        }
    }
    m = std::move(out);
}

}  // namespace detail
}  // namespace Anim
