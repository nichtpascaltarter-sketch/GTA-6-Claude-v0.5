// Mesh decimation for the character LODs: quadric-error (Garland-Heckbert) half-edge collapses on the build mesh.
// A half-edge collapse u -> v removes u and keeps v with all its attributes (position, normal, colour, uv, material,
// skin weights), so the result never needs attribute interpolation and every surviving vertex is skinned exactly as
// in the full mesh. Costs add colour / skin weight / normal differences to the geometric error; open boundaries (hems,
// eye fissures, mouth slit, piece borders) are held by perpendicular constraint planes and may only collapse along
// themselves; border vertices shared by two pieces (coincident twins) are locked so junctions never crack. Collapses
// that flip or sliver a triangle or break the link condition (non-manifold result) are rejected.
#include "anim_internal.h"
#include <algorithm>
#include <queue>
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

struct DecCand {
    double cost;
    u32 u, v;
    u32 su, sv;   // vertex stamps when evaluated
    bool operator<(const DecCand& o) const { return cost > o.cost; }   // min-heap
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
    std::vector<std::vector<u32>> vt(NV);
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
    for (u32 t = 0; t < NT; t++) {
        if (!triAlive[t]) continue;
        vec3 p0 = m.v[T[t * 3]].p, p1 = m.v[T[t * 3 + 1]].p, p2 = m.v[T[t * 3 + 2]].p;
        vec3 n = cross(p1 - p0, p2 - p0);
        float area2 = length(n);
        if (area2 < 1e-14f) continue;
        n = n / area2;
        // area weighted planes (units: m^2 * m^2), normalized below by a typical triangle area
        double w = area2 * 0.5;
        for (int k = 0; k < 3; k++) Q[T[t * 3 + k]].addPlane(n.x, n.y, n.z, -dot(n, p0), w);
    }
    // boundary constraint planes (perpendicular to the face through the border edge) and locks
    double meanArea = 0.0;
    {
        int cnt = 0;
        for (u32 t = 0; t < NT; t++)
            if (triAlive[t]) {
                vec3 p0 = m.v[T[t * 3]].p, p1 = m.v[T[t * 3 + 1]].p, p2 = m.v[T[t * 3 + 2]].p;
                meanArea += 0.5 * length(cross(p1 - p0, p2 - p0));
                cnt++;
            }
        meanArea = Max(meanArea / Max(cnt, 1), 1e-9);
    }
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) j++;
        u32 a = (u32)(edges[i].key >> 32), b = (u32)(edges[i].key & 0xffffffffu);
        if (j - i == 1) {
            vBound[a] = vBound[b] = 1;
            u32 t = edges[i].tri;
            vec3 p0 = m.v[T[t * 3]].p, p1 = m.v[T[t * 3 + 1]].p, p2 = m.v[T[t * 3 + 2]].p;
            vec3 fn = cross(p1 - p0, p2 - p0);
            vec3 e = m.v[b].p - m.v[a].p;
            vec3 pn = cross(e, fn);
            float l = length(pn);
            if (l > 1e-12f) {
                pn = pn / l;
                double w = 8.0 * meanArea * Max(1.0, (double)length2(e) / meanArea);
                Q[a].addPlane(pn.x, pn.y, pn.z, -dot(pn, m.v[a].p), w);
                Q[b].addPlane(pn.x, pn.y, pn.z, -dot(pn, m.v[a].p), w);
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
            vec3 p = m.v[i].p;
            return std::make_tuple((int)floorf(p.x * 2e4f), (int)floorf(p.y * 2e4f), (int)floorf(p.z * 2e4f));
        };
        std::sort(bv.begin(), bv.end(), [&](u32 x, u32 y) { return qk(x) < qk(y); });
        for (size_t i = 0; i + 1 < bv.size(); i++)
            if (qk(bv[i]) == qk(bv[i + 1])) vLock[bv[i]] = vLock[bv[i + 1]] = 1;
    }
    const double errScale = 1.0 / meanArea;   // error ~ squared distance in m^2
    auto isBoundaryEdge = [&](u32 u, u32 v) {
        int n = 0;
        for (u32 t : vt[u]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            if (tr[0] == v || tr[1] == v || tr[2] == v) n++;
        }
        return n == 1;
    };
    auto evalCand = [&](u32 u, u32 v, double& cost) -> bool {
        if (!vAlive[u] || !vAlive[v] || vLock[u]) return false;
        const BVert& a = m.v[u];
        const BVert& b = m.v[v];
        if (a.mat != b.mat || a.part != b.part) return false;
        if (vBound[u] && (!vBound[v] || !isBoundaryEdge(u, v))) return false;
        Quadric q = Q[u];
        q.add(Q[v]);
        double geo = Max(0.0, q.eval(b.p)) * errScale;
        vec3 dc = a.col - b.col;
        double col = (double)length2(dc) * 2e-4;
        double sk = (double)skinDiff(a.sw, b.sw) * 3e-4;
        double nrm = (1.0 - Clamp((double)dot(a.n, b.n), -1.0, 1.0)) * 2e-5;
        double len = (double)length2(a.p - b.p) * 0.02;   // mild preference for short edges (even tessellation)
        cost = geo + col + sk + nrm + len;
        if (partWeight) cost *= partWeight[Min((int)a.part, (int)PART_COUNT - 1)];
        return true;
    };
    std::priority_queue<DecCand> heap;
    auto pushPair = [&](u32 a, u32 b) {
        double c;
        if (evalCand(a, b, c)) heap.push({c, a, b, stamp[a], stamp[b]});
        if (evalCand(b, a, c)) heap.push({c, b, a, stamp[b], stamp[a]});
    };
    std::vector<u32> nbTmp;
    // candidates on the edges around v (after its quadric changed)
    auto pushEdges = [&](u32 v) {
        nbTmp.clear();
        for (u32 t : vt[v]) {
            if (!triAlive[t]) continue;
            for (int k = 0; k < 3; k++)
                if (T[t * 3 + k] != v) nbTmp.push_back(T[t * 3 + k]);
        }
        std::sort(nbTmp.begin(), nbTmp.end());
        nbTmp.erase(std::unique(nbTmp.begin(), nbTmp.end()), nbTmp.end());
        for (u32 w : nbTmp) pushPair(v, w);
    };
    for (size_t i = 0; i < edges.size();) {
        size_t j = i;
        while (j < edges.size() && edges[j].key == edges[i].key) j++;
        pushPair((u32)(edges[i].key >> 32), (u32)(edges[i].key & 0xffffffffu));
        i = j;
    }
    int alive = 0;
    for (u32 t = 0; t < NT; t++) alive += triAlive[t];
    std::vector<u32> nbU, nbV;
    while (alive > targetTris && !heap.empty()) {
        DecCand c = heap.top();
        heap.pop();
        u32 u = c.u, v = c.v;
        if (!vAlive[u] || !vAlive[v] || stamp[u] != c.su || stamp[v] != c.sv) continue;
        // link condition: common neighbours of u and v == triangles on the edge (1 border / 2 interior)
        nbU.clear();
        nbV.clear();
        int edgeTris = 0;
        for (u32 t : vt[u]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            bool hasV = tr[0] == v || tr[1] == v || tr[2] == v;
            edgeTris += hasV;
            for (int k = 0; k < 3; k++)
                if (tr[k] != u) nbU.push_back(tr[k]);
        }
        if (edgeTris == 0) continue;
        for (u32 t : vt[v]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            for (int k = 0; k < 3; k++)
                if (tr[k] != v) nbV.push_back(tr[k]);
        }
        std::sort(nbU.begin(), nbU.end());
        nbU.erase(std::unique(nbU.begin(), nbU.end()), nbU.end());
        std::sort(nbV.begin(), nbV.end());
        nbV.erase(std::unique(nbV.begin(), nbV.end()), nbV.end());
        int common = 0;
        for (size_t i = 0, j = 0; i < nbU.size() && j < nbV.size();) {
            if (nbU[i] == nbV[j]) {
                common++;
                i++;
                j++;
            } else if (nbU[i] < nbV[j]) i++;
            else j++;
        }
        if (common != edgeTris) continue;
        // geometry checks on the triangles that survive (u replaced by v): no flips, no slivers
        bool ok = true;
        vec3 pv = m.v[v].p;
        for (u32 t : vt[u]) {
            if (!triAlive[t]) continue;
            const u32* tr = &T[t * 3];
            if (tr[0] == v || tr[1] == v || tr[2] == v) continue;
            vec3 p[3], q[3];
            for (int k = 0; k < 3; k++) {
                p[k] = m.v[tr[k]].p;
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
        if (!ok) continue;
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
        if (vBound[u]) vBound[v] = 1;
        // compact v's triangle list and refresh the neighbourhood
        std::vector<u32>& lv = vt[v];
        size_t w = 0;
        for (size_t i = 0; i < lv.size(); i++)
            if (triAlive[lv[i]]) lv[w++] = lv[i];
        lv.resize(w);
        std::sort(lv.begin(), lv.end());
        lv.erase(std::unique(lv.begin(), lv.end()), lv.end());
        // only costs involving v changed (validity of the others is re-checked when they are popped)
        stamp[v]++;
        pushEdges(v);
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
