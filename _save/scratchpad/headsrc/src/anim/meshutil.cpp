// Mesh building utilities for characters: signed distance primitives, skin weights, stitching, output.
#include "anim_internal.h"
#include <unordered_map>

namespace Anim {
namespace detail {

// ------------------------------------------------------------------------------------------------
// SDF primitives

static float sdEllipsoidLocal(vec3 p, vec3 r) {
    vec3 pr(p.x / r.x, p.y / r.y, p.z / r.z);
    float k0 = length(pr);
    vec3 pr2(p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z));
    float k1 = length(pr2);
    if (k1 < 1e-9f) return -Min(r.x, Min(r.y, r.z));
    return k0 * (k0 - 1.f) / k1;
}

static FORCEINLINE float sgn(float x) { return x > 0.f ? 1.f : (x < 0.f ? -1.f : 0.f); }

// Round cone from origin along +Z (length L), radii r1 (at origin) -> r2 (at L). Exact SDF (Quilez).
static float sdRoundConeZ(vec3 p, float L, float r1, float r2) {
    float l2 = L * L;
    float rr = r1 - r2;
    float a2 = l2 - rr * rr;
    float il2 = 1.f / l2;
    float y = p.z * L;
    float z = y - l2;
    float x2 = (p.x * p.x + p.y * p.y) * l2 * l2;
    float y2 = y * y * l2;
    float z2 = z * z * l2;
    float k = sgn(rr) * rr * rr * x2;
    if (sgn(z) * a2 * z2 > k) return sqrtf(x2 + z2) * il2 - r2;
    if (sgn(y) * a2 * y2 < k) return sqrtf(x2 + y2) * il2 - r1;
    return (sqrtf(x2 * a2 * il2) + y * rr) * il2 - r1;
}

static FORCEINLINE float primDist(const Prim& q, vec3 p) {
    vec3 d = p - q.c;
    vec3 l(dot(d, q.ax), dot(d, q.ay), dot(d, q.az));
    if (q.type == PRIM_ELLIPSOID) return sdEllipsoidLocal(l, q.r);
    if (q.type == PRIM_ROUNDCONE) {
        float m = Min(q.sx, q.sy);
        return sdRoundConeZ(vec3(l.x / q.sx, l.y / q.sy, l.z), q.len, q.ra, q.rb) * m;
    }
    return -l.z;  // plane: inside where l.z > 0
}

int Sdf::ellipsoid(vec3 c, vec3 r, u32 mask, float k, vec3 ax, vec3 ay) {
    Prim q;
    q.type = PRIM_ELLIPSOID;
    q.mask = mask;
    q.k = k;
    q.c = c;
    q.ax = normalize(ax);
    q.az = normalize(cross(q.ax, ay));
    q.ay = cross(q.az, q.ax);
    q.r = vmax(r, vec3(1e-4f));
    q.bc = c;
    q.br = maxc(q.r);
    prims.push_back(q);
    return (int)prims.size() - 1;
}

int Sdf::cone(vec3 a, vec3 b, float ra, float rb, u32 mask, float k, float sx, float sy, vec3 xHint) {
    Prim q;
    q.type = PRIM_ROUNDCONE;
    q.mask = mask;
    q.k = k;
    q.c = a;
    vec3 ab = b - a;
    q.len = Max(length(ab), 1e-4f);
    q.az = ab / q.len;
    vec3 x = xHint - q.az * dot(xHint, q.az);
    if (length2(x) < 1e-8f) x = anyPerp(q.az);
    q.ax = normalize(x);
    q.ay = cross(q.az, q.ax);
    q.ra = Max(ra, 1e-4f);
    q.rb = Max(rb, 1e-4f);
    q.sx = sx;
    q.sy = sy;
    q.bc = a + ab * 0.5f;
    q.br = q.len * 0.5f + Max(q.ra, q.rb) * Max(sx, sy);
    prims.push_back(q);
    return (int)prims.size() - 1;
}

int Sdf::plane(vec3 p, vec3 n, u32 mask) {
    Prim q;
    q.type = PRIM_PLANE;
    q.op = OP_INTERSECT;
    q.mask = mask;
    q.k = 0.f;
    q.c = p;
    q.az = normalize(n);
    orthoFrame(q.az, q.ax, q.ay);
    q.bc = p;
    q.br = 1e9f;
    prims.push_back(q);
    return (int)prims.size() - 1;
}

float Sdf::eval(vec3 p, u32 mask) const {
    float d = 1e9f;
    for (size_t i = 0; i < prims.size(); i++) {
        const Prim& q = prims[i];
        if (!(q.mask & mask)) continue;
        if (q.op == OP_UNION) {
            float lb = length(p - q.bc) - q.br;
            if (lb > d + q.k) continue;
            d = sminf(d, primDist(q, p), q.k);
        } else if (q.op == OP_SUB) {
            float lb = length(p - q.bc) - q.br;
            if (lb > -d + q.k) continue;
            d = smaxf(d, -primDist(q, p), q.k);
        } else {
            d = smaxf(d, primDist(q, p), q.k);
        }
    }
    return d;
}

float Sdf::evalList(vec3 p, const u16* list, int n, float cap) const {
    float d = cap;
    for (int i = 0; i < n; i++) {
        const Prim& q = prims[list[i]];
        if (q.op == OP_UNION) {
            float lb = length(p - q.bc) - q.br;
            if (lb > d + q.k) continue;
            d = sminf(d, primDist(q, p), q.k);
        } else if (q.op == OP_SUB) {
            float lb = length(p - q.bc) - q.br;
            if (lb > -d + q.k) continue;
            d = smaxf(d, -primDist(q, p), q.k);
        } else {
            d = smaxf(d, primDist(q, p), q.k);
        }
    }
    return d;
}

vec3 Sdf::grad(vec3 p, u32 mask) const {
    const float h = 0.0006f;
    vec3 k0(1, -1, -1), k1(-1, -1, 1), k2(-1, 1, -1), k3(1, 1, 1);
    vec3 g = k0 * eval(p + k0 * h, mask) + k1 * eval(p + k1 * h, mask) + k2 * eval(p + k2 * h, mask) + k3 * eval(p + k3 * h, mask);
    return g * (1.f / (4.f * h));
}

float Sdf::castOut(vec3 o, vec3 dir, u32 mask, float tMax, float tStart) const {
    float t = 0.f;
    float f = eval(o, mask);
    if (f >= 0.f) return 0.f;
    if (tStart > 0.f) {
        // start closer to the surface when the caller knows the ray stays inside up to tStart
        float fs = eval(o + dir * tStart, mask);
        if (fs < 0.f) {
            t = tStart;
            f = fs;
        }
    }
    for (int it = 0; it < 160; it++) {
        float step = Max(-f * 0.8f, 0.0006f);
        float tn = Min(t + step, tMax);
        float fn = eval(o + dir * tn, mask);
        if (fn >= 0.f) {
            // bracketed crossing: Illinois false position (the field is close to linear across the surface)
            float lo = t, hi = tn, flo = f, fhi = fn;
            int side = 0;
            for (int b = 0; b < 8; b++) {
                float mid = lo + (hi - lo) * Saturate(-flo / Max(fhi - flo, 1e-12f));
                float fm = eval(o + dir * mid, mask);
                if (fabsf(fm) < 1e-6f || hi - lo < 1e-6f) return mid;
                if (fm >= 0.f) {
                    hi = mid;
                    fhi = fm;
                    if (side == 1) flo *= 0.5f;
                    side = 1;
                } else {
                    lo = mid;
                    flo = fm;
                    if (side == -1) fhi *= 0.5f;
                    side = -1;
                }
            }
            return 0.5f * (lo + hi);
        }
        t = tn;
        f = fn;
        if (t >= tMax) break;
    }
    return t;
}

vec3 Sdf::project(vec3 p, u32 mask, int iters) const {
    for (int i = 0; i < iters; i++) {
        float f = eval(p, mask);
        if (fabsf(f) < 1e-5f) break;
        vec3 g = grad(p, mask);
        float g2 = dot(g, g);
        if (g2 < 1e-8f) break;
        vec3 step = g * (f / g2);
        float sl = length(step);
        if (sl > 0.02f) step *= 0.02f / sl;
        p -= step;
    }
    return p;
}

// ------------------------------------------------------------------------------------------------
// Skin weights

SkinW skin1(int b) {
    SkinW s;
    s.b[0] = (u8)b;
    s.w[0] = 1.f;
    return s;
}

SkinW skin2(int b0, int b1, float t) {
    t = Saturate(t);
    WAcc a;
    a.add(b0, 1.f - t);
    a.add(b1, t);
    return a.finish();
}

void WAcc::add(int bone, float wt) {
    if (!(wt > 1e-5f)) return;
    for (int i = 0; i < n; i++)
        if (b[i] == bone) {
            w[i] += wt;
            return;
        }
    if (n < 12) {
        b[n] = (u8)bone;
        w[n] = wt;
        n++;
    }
}

SkinW WAcc::finish() const {
    SkinW s;
    if (n == 0) return s;
    int order[12];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (w[order[j]] > w[order[i]]) std::swap(order[i], order[j]);
    int k = Min(n, 4);
    float sum = 0.f;
    for (int i = 0; i < k; i++) sum += w[order[i]];
    for (int i = 0; i < 4; i++) {
        if (i < k) {
            s.b[i] = b[order[i]];
            s.w[i] = w[order[i]] / sum;
        } else {
            s.b[i] = s.b[0];
            s.w[i] = 0.f;
        }
    }
    return s;
}

SkinW lerpSkin(const SkinW& a, const SkinW& b, float t) {
    WAcc acc;
    for (int i = 0; i < 4; i++) acc.add(a.b[i], a.w[i] * (1.f - t));
    for (int i = 0; i < 4; i++) acc.add(b.b[i], b.w[i] * t);
    return acc.finish();
}

// ------------------------------------------------------------------------------------------------
// Mesh helpers

void MeshB::computeNormals(size_t i0, size_t i1) {
    std::vector<vec3> acc(v.size(), vec3(0));
    std::vector<u8> touched(v.size(), 0);
    size_t iEnd = Min(i1, idx.size());
    for (size_t i = i0; i + 3 <= iEnd; i += 3) {
        u32 a = idx[i], b = idx[i + 1], c = idx[i + 2];
        vec3 n = cross(v[b].p - v[a].p, v[c].p - v[a].p);
        acc[a] += n;
        acc[b] += n;
        acc[c] += n;
        touched[a] = touched[b] = touched[c] = 1;
    }
    for (size_t i = 0; i < v.size(); i++)
        if (touched[i] && length2(acc[i]) > 1e-20f) v[i].n = normalize(acc[i]);
}

void MeshB::append(const MeshB& o) {
    u32 base = (u32)v.size();
    v.insert(v.end(), o.v.begin(), o.v.end());
    idx.reserve(idx.size() + o.idx.size());
    for (u32 i : o.idx) idx.push_back(i + base);
}

void stitchLoops(MeshB& m, const std::vector<u32>& A, const std::vector<u32>& B, bool closed, bool flip) {
    int na = (int)A.size(), nb = (int)B.size();
    if (na < 1 || nb < 1) return;
    int ea = closed ? na : na - 1, eb = closed ? nb : nb - 1;
    std::vector<float> ta(ea + 1, 0.f), tb(eb + 1, 0.f);
    for (int i = 0; i < ea; i++) ta[i + 1] = ta[i] + length(m.v[A[(i + 1) % na]].p - m.v[A[i]].p);
    for (int j = 0; j < eb; j++) tb[j + 1] = tb[j] + length(m.v[B[(j + 1) % nb]].p - m.v[B[j]].p);
    if (ta[ea] > 0.f) for (float& x : ta) x /= ta[ea];
    if (tb[eb] > 0.f) for (float& x : tb) x /= tb[eb];
    int i = 0, j = 0;
    while (i < ea || j < eb) {
        bool advA;
        if (i >= ea) advA = false;
        else if (j >= eb) advA = true;
        else advA = ta[i + 1] <= tb[j + 1];
        u32 a0 = A[i % na], b0 = B[j % nb];
        if (advA) {
            u32 a1 = A[(i + 1) % na];
            m.triMirror(flip, a0, b0, a1);
            i++;
        } else {
            u32 b1 = B[(j + 1) % nb];
            m.triMirror(flip, a0, b0, b1);
            j++;
        }
    }
}

void fixUvSeams(MeshB& m) {
    std::unordered_map<u32, u32> dup;
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        float per = m.v[m.idx[t]].uPer;
        if (per <= 0.f || m.v[m.idx[t + 1]].uPer != per || m.v[m.idx[t + 2]].uPer != per) continue;
        float mn = 1e30f, mx = -1e30f;
        for (int k = 0; k < 3; k++) {
            float u = m.v[m.idx[t + k]].uv.x;
            mn = Min(mn, u);
            mx = Max(mx, u);
        }
        if (mx - mn <= per * 0.5f) continue;
        float mid = 0.5f * (mn + mx);
        for (int k = 0; k < 3; k++) {
            u32 vi = m.idx[t + k];
            if (m.v[vi].uv.x >= mid) continue;
            auto it = dup.find(vi);
            if (it == dup.end()) {
                BVert v = m.v[vi];
                v.uv.x += per;
                u32 ni = (u32)m.v.size();
                m.v.push_back(v);
                it = dup.insert(std::make_pair(vi, ni)).first;
            }
            m.idx[t + k] = it->second;
        }
    }
}

u32 emitCard(MeshB& m, const CardPt* pts, int n, u8 kind, u32 seed, vec3 colRoot, vec3 colTip, float density, u8 part,
             const HeadInfo* head) {
    u32 first = (u32)m.v.size();
    if (n < 2) return first;
    float total = 0.f;
    std::vector<float> s(n, 0.f);
    for (int i = 1; i < n; i++) s[i] = total += length(pts[i].p - pts[i - 1].p);
    if (total < 1e-6f) return first;
    u32 param = (u32)(kind & 15u) | ((seed & 0xffffu) << 4);
    Rng r(hash32(seed * 2654435761u + kind));
    float bright = r.range(0.86f, 1.12f);
    for (int i = 0; i < n; i++) {
        vec3 t = i + 1 < n ? pts[i + 1].p - pts[i].p : pts[i].p - pts[i - 1].p;
        if (i > 0 && i + 1 < n) t = pts[i + 1].p - pts[i - 1].p;
        vec3 nn = pts[i].n;
        t = t - nn * dot(t, nn);
        t = length2(t) > 1e-12f ? normalize(t) : anyPerp(nn);
        vec3 b = normalize(cross(nn, t));
        float u = s[i] / total;
        for (int e = 0; e < 2; e++) {
            BVert v;
            v.p = pts[i].p + b * (pts[i].w * (e ? 0.5f : -0.5f));
            v.bp = v.p;
            v.n = nn;
            v.t = t;
            v.uv = vec2((float)e, u);
            v.col = lerp(colRoot, colTip, sstep(0.f, 0.7f, u)) * bright;
            v.alpha = Saturate(density);
            v.mat = MAT_HAIR;
            v.matParam = param;
            v.part = part;
            v.sw = pts[i].sw;
            if (head) {
                vec3 d = v.p - head->C;
                float th = atan2f(d.x, d.y);
                v.pa = th < 0.f ? th + kTwoPi : th;
                v.pb = atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y));
                v.pc = 1.5f;
            }
            m.add(v);
        }
    }
    for (int i = 0; i + 1 < n; i++) {
        u32 a = first + (u32)i * 2, bb = a + 1, c = a + 2, d = a + 3;
        // winding: the triangle normal follows the card normal
        vec3 fn = cross(m.v[c].p - m.v[a].p, m.v[bb].p - m.v[a].p);
        if (dot(fn, pts[i].n) >= 0.f) {
            m.tri(a, c, bb);
            m.tri(bb, c, d);
        } else {
            m.tri(a, bb, c);
            m.tri(bb, d, c);
        }
    }
    return first;
}

void emitMesh(const MeshB& m, SkinnedMeshData& out) {
    out.verts.clear();
    out.indices.clear();
    out.bounds = AABB();
    out.verts.reserve(m.v.size());
    for (const BVert& b : m.v) {
        vec3 n = length2(b.n) > 1e-12f ? normalize(b.n) : vec3(0, 0, 1);
        vec3 t = b.t - n * dot(b.t, n);
        if (length2(t) < 1e-10f) t = anyPerp(n);
        t = normalize(t);
        u8 bones[4], wts[4];
        int sum = 0, best = 0;
        for (int i = 0; i < 4; i++) {
            bones[i] = b.sw.b[i];
            int q = (int)lrintf(Saturate(b.sw.w[i]) * 255.f);
            wts[i] = (u8)q;
            sum += q;
            if (b.sw.w[i] > b.sw.w[best]) best = i;
        }
        int fix = (int)wts[best] + (255 - sum);
        wts[best] = (u8)Clamp(fix, 0, 255);
        out.addVertex(b.p, n, t, b.uv, packColor(b.col, b.alpha), makeMat(b.mat, b.matParam), bones, wts);
    }
    // strand cards last (the renderer draws them in a separate alpha-tested, two-sided pass)
    out.indices.reserve(m.idx.size());
    for (int pass = 0; pass < 2; pass++)
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
            bool card = cardKind(m.v[m.idx[t]]) != CARD_NONE;
            if (card != (pass == 1)) continue;
            out.indices.push_back(m.idx[t]);
            out.indices.push_back(m.idx[t + 1]);
            out.indices.push_back(m.idx[t + 2]);
        }
}

}  // namespace detail
}  // namespace Anim
