// Geometry toolkit for the procedural vehicle generator (included from vehicle_models.cpp, unity build).
// An intermediate polygon mesh with smoothing groups (welded positions + angle-threshold auto smoothing),
// parametric primitives (lathe, tube, rounded box, ellipsoid, extrusion) and surface-projected decals.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vehicles {
namespace detail {

// ------------------------------------------------------------------------------------------------
// Colors (vertex colors are linear multipliers; MAT_CARPAINT uses alpha to pick primary/secondary paint)
inline u32 col(float r, float g, float b, float a = 1.f) { return packRGBA8(r, g, b, a); }
inline u32 colv(vec3 c, float a = 1.f) { return packRGBA8(c.x, c.y, c.z, a); }
inline vec3 srgb(float r, float g, float b) { return srgbToLinear(vec3(r, g, b) * (1.f / 255.f)); }
static const u32 kCol1 = 0xffffffffu;   // neutral tint / primary paint
static const u32 kCol2 = 0x00ffffffu;   // secondary paint (alpha 0)

enum UvMode : u8 { UV_BOX = 0, UV_EXPLICIT = 1 };

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth01(float x) { x = Saturate(x); return x * x * (3.f - 2.f * x); }
inline float remap01(float x, float a, float b) { return Saturate((x - a) / (b - a)); }

// ------------------------------------------------------------------------------------------------
// Polygon mesh with per-face material/color and smoothing groups.
struct PMesh {
    struct Face {
        u32 v[3];
        u32 color;
        u8 mat;
        u8 uvMode;
        u16 group;
    };
    struct Mark { size_t p, f; };
    std::vector<vec3> P;
    std::vector<vec2> T;
    std::vector<Face> F;
    std::vector<float> groupCos;
    u32 color = kCol1;
    u8 mat = MAT_CARPAINT;
    u8 uvMode = UV_BOX;
    u16 group = 0;

    PMesh() { groupCos.push_back(cosf(40.f * kDegToRad)); }
    // Starts a new smoothing group: faces only share normals with faces of the same group whose normals
    // differ by less than `deg`.
    u16 newGroup(float deg = 40.f) {
        groupCos.push_back(cosf(deg * kDegToRad));
        group = (u16)(groupCos.size() - 1);
        return group;
    }
    void use(u8 m, u32 c = kCol1) { mat = m; color = c; }
    Mark mark() const { return Mark{P.size(), F.size()}; }
    u32 add(vec3 p, vec2 uv = vec2(0, 0)) {
        P.push_back(p);
        T.push_back(uv);
        return (u32)P.size() - 1;
    }
    void tri(u32 a, u32 b, u32 c) {
        Face f;
        f.v[0] = a; f.v[1] = b; f.v[2] = c;
        f.color = color; f.mat = mat; f.uvMode = uvMode; f.group = group;
        F.push_back(f);
    }
    // Quad a,b,c,d counter-clockwise seen from the front; split along the shorter diagonal.
    void quad(u32 a, u32 b, u32 c, u32 d) {
        if (length2(P[a] - P[c]) <= length2(P[b] - P[d]) * 1.0001f) { tri(a, b, c); tri(a, c, d); }
        else { tri(a, b, d); tri(b, c, d); }
    }
    void quadFacing(u32 a, u32 b, u32 c, u32 d, vec3 facing) {
        vec3 n = cross(P[b] - P[a], P[d] - P[a]) + cross(P[d] - P[c], P[b] - P[c]);
        if (dot(n, facing) < 0.f) quad(a, d, c, b);
        else quad(a, b, c, d);
    }
    void transform(Mark m, const mat4& M) {
        for (size_t i = m.p; i < P.size(); i++) P[i] = transformPoint(M, P[i]);
        vec3 c0 = M.c[0].xyz(), c1 = M.c[1].xyz(), c2 = M.c[2].xyz();
        if (dot(cross(c0, c1), c2) < 0.f)
            for (size_t i = m.f; i < F.size(); i++) std::swap(F[i].v[1], F[i].v[2]);
    }
    void translate(Mark m, vec3 t) { for (size_t i = m.p; i < P.size(); i++) P[i] += t; }
    // Appends a copy of everything added since `m`, mirrored across the plane x = 0.
    void mirrorX(Mark m) {
        size_t np = P.size(), nf = F.size();
        u32 off = (u32)(np - m.p);
        for (size_t i = m.p; i < np; i++) {
            P.push_back(vec3(-P[i].x, P[i].y, P[i].z));
            T.push_back(T[i]);
        }
        for (size_t i = m.f; i < nf; i++) {
            Face f = F[i];
            u32 a = f.v[0], b = f.v[1], c = f.v[2];
            f.v[0] = a + off; f.v[1] = c + off; f.v[2] = b + off;
            F.push_back(f);
        }
    }
    void flipSince(Mark m) {
        for (size_t i = m.f; i < F.size(); i++) std::swap(F[i].v[1], F[i].v[2]);
    }
    // Re-colors / re-materials faces added since `m`.
    void recolor(Mark m, u8 matId, u32 c) {
        for (size_t i = m.f; i < F.size(); i++) { F[i].mat = matId; F[i].color = c; }
    }
};

struct WeldKey {
    i32 x, y, z;
    u32 g;
    bool operator==(const WeldKey& o) const { return x == o.x && y == o.y && z == o.z && g == o.g; }
};
struct WeldKeyHash {
    size_t operator()(const WeldKey& k) const {
        u32 h = hash32((u32)k.x * 0x8da6b343U ^ (u32)k.y * 0xd8163841U ^ (u32)k.z * 0xcb1ab31fU ^ k.g * 0x165667b1U);
        return (size_t)h;
    }
};
struct OutKey {
    u32 w, color, normal, matUv;
    u32 u, v;
    bool operator==(const OutKey& o) const {
        return w == o.w && color == o.color && normal == o.normal && matUv == o.matUv && u == o.u && v == o.v;
    }
};
struct OutKeyHash {
    size_t operator()(const OutKey& k) const {
        u32 h = hash32(k.w * 0x9e3779b1U ^ hash32(k.color ^ k.normal * 31u) ^ k.matUv * 0x85ebca6bU ^ hash32(k.u * 7u + k.v));
        return (size_t)h;
    }
};

inline u32 floatBits(float f) { u32 u; memcpy(&u, &f, 4); return u; }

// Converts the polygon mesh into the renderer's vertex format with auto-smoothed normals.
inline void finalizeMesh(const PMesh& pm, MeshData& out) {
    out.clear();
    size_t nf = pm.F.size();
    std::vector<vec3> fn(nf);
    std::vector<u8> valid(nf, 0);
    std::vector<float> cang(nf * 3, 0.f);
    for (size_t i = 0; i < nf; i++) {
        const PMesh::Face& f = pm.F[i];
        vec3 a = pm.P[f.v[0]], b = pm.P[f.v[1]], c = pm.P[f.v[2]];
        vec3 n = cross(b - a, c - a);
        float l = length(n);
        if (!(l > 2e-9f)) continue;
        valid[i] = 1;
        fn[i] = n / l;
        vec3 pts[3] = {a, b, c};
        for (int k = 0; k < 3; k++) {
            vec3 e1 = normalize(pts[(k + 1) % 3] - pts[k]), e2 = normalize(pts[(k + 2) % 3] - pts[k]);
            cang[i * 3 + k] = acosf(Clamp(dot(e1, e2), -1.f, 1.f));
        }
    }
    // Weld corners by quantized position (0.1 mm) within the smoothing group
    std::unordered_map<WeldKey, u32, WeldKeyHash> weld;
    weld.reserve(nf * 2 + 16);
    std::vector<u32> cw(nf * 3, 0xffffffffu);
    u32 nw = 0;
    for (size_t i = 0; i < nf; i++) {
        if (!valid[i]) continue;
        const PMesh::Face& f = pm.F[i];
        for (int k = 0; k < 3; k++) {
            vec3 p = pm.P[f.v[k]];
            WeldKey key{(i32)floorf(p.x * 10000.f + 0.5f), (i32)floorf(p.y * 10000.f + 0.5f), (i32)floorf(p.z * 10000.f + 0.5f), f.group};
            auto it = weld.find(key);
            if (it == weld.end()) { weld.emplace(key, nw); cw[i * 3 + k] = nw++; }
            else cw[i * 3 + k] = it->second;
        }
    }
    std::vector<u32> start(nw + 1, 0);
    for (size_t c = 0; c < nf * 3; c++) if (cw[c] != 0xffffffffu) start[cw[c] + 1]++;
    for (u32 i = 0; i < nw; i++) start[i + 1] += start[i];
    std::vector<u32> adj(start[nw]);
    {
        std::vector<u32> fill(start.begin(), start.end() - 1);
        for (size_t c = 0; c < nf * 3; c++) if (cw[c] != 0xffffffffu) adj[fill[cw[c]]++] = (u32)c;
    }
    std::unordered_map<OutKey, u32, OutKeyHash> outMap;
    outMap.reserve(nf * 2 + 16);
    out.verts.reserve(nf);
    out.indices.reserve(nf * 3);
    for (size_t i = 0; i < nf; i++) {
        if (!valid[i]) continue;
        const PMesh::Face& f = pm.F[i];
        float cosT = pm.groupCos[f.group];
        u32 idx[3];
        // face tangent from explicit uvs
        vec3 ftan;
        vec3 p0 = pm.P[f.v[0]], p1 = pm.P[f.v[1]], p2 = pm.P[f.v[2]];
        int ax = 0;
        {
            vec3 an = vabs(fn[i]);
            ax = (an.x > an.y && an.x > an.z) ? 0 : (an.y > an.z ? 1 : 2);
        }
        if (f.uvMode == UV_EXPLICIT) {
            vec2 t0 = pm.T[f.v[0]], t1 = pm.T[f.v[1]], t2 = pm.T[f.v[2]];
            vec3 e1 = p1 - p0, e2 = p2 - p0;
            vec2 d1 = t1 - t0, d2 = t2 - t0;
            float det = d1.x * d2.y - d2.x * d1.y;
            if (fabsf(det) > 1e-12f) ftan = (e1 * d2.y - e2 * d1.y) / det;
            else ftan = anyPerp(fn[i]);
        } else {
            ftan = ax == 0 ? vec3(0, 1, 0) : vec3(1, 0, 0);
        }
        for (int k = 0; k < 3; k++) {
            u32 w = cw[i * 3 + k];
            vec3 n(0, 0, 0);
            for (u32 a = start[w]; a < start[w + 1]; a++) {
                u32 c = adj[a];
                u32 j = c / 3;
                if (dot(fn[i], fn[j]) >= cosT) n += fn[j] * cang[c];
            }
            float nl = length(n);
            n = nl > 1e-8f ? n / nl : fn[i];
            vec3 p = pm.P[f.v[k]];
            vec2 uv;
            if (f.uvMode == UV_EXPLICIT) uv = pm.T[f.v[k]];
            else uv = ax == 0 ? vec2(p.y, p.z) : (ax == 1 ? vec2(p.x, p.z) : vec2(p.x, p.y));
            u32 pn = packNormalOct(n);
            OutKey key{w, f.color, pn, (u32)f.mat | ((u32)f.uvMode << 8), floatBits(uv.x), floatBits(uv.y)};
            auto it = outMap.find(key);
            if (it != outMap.end()) { idx[k] = it->second; continue; }
            vec3 t = ftan - n * dot(ftan, n);
            if (length2(t) < 1e-12f) t = anyPerp(n);
            t = normalize(t);
            u32 vi = out.addVertex(p, n, t, uv, f.color, makeMat(f.mat));
            outMap.emplace(key, vi);
            idx[k] = vi;
        }
        out.tri(idx[0], idx[1], idx[2]);
    }
}

// ------------------------------------------------------------------------------------------------
// Local frame helper
struct Frame {
    vec3 o, x, y, z;
    Frame() : o(0, 0, 0), x(1, 0, 0), y(0, 1, 0), z(0, 0, 1) {}
    Frame(vec3 org, vec3 ax, vec3 ay, vec3 az) : o(org), x(ax), y(ay), z(az) {}
    vec3 at(vec3 l) const { return o + x * l.x + y * l.y + z * l.z; }
    vec3 dir(vec3 l) const { return x * l.x + y * l.y + z * l.z; }
    mat4 m() const { return mat4FromBasis(x, y, z, o); }
};
inline bool leftHanded(const Frame& f) { return dot(cross(f.x, f.y), f.z) < 0.f; }
// Orthonormal frame with forward axis f (local +Y) and approximate up u (local +Z).
inline Frame frameFY(vec3 o, vec3 f, vec3 u) {
    vec3 y = normalize(f);
    vec3 x = normalize(cross(y, u));
    vec3 z = cross(x, y);
    return Frame(o, x, y, z);
}

// Emits a structured grid of existing vertex indices (rows x cols) as quads whose normal is
// cross(dP/drow, dP/dcol); `flip` reverses. Degenerate quads are dropped by the finalizer.
inline void gridQuads(PMesh& m, const std::vector<u32>& id, int rows, int cols, bool wrapCols, bool flip) {
    int cc = wrapCols ? cols : cols - 1;
    for (int i = 0; i + 1 < rows; i++)
        for (int j = 0; j < cc; j++) {
            int j1 = (j + 1) % cols;
            u32 a = id[i * cols + j], b = id[(i + 1) * cols + j], c = id[(i + 1) * cols + j1], d = id[i * cols + j1];
            if (flip) m.quad(a, d, c, b);
            else m.quad(a, b, c, d);
        }
}

// Surface of revolution. prof = (a along axis, r radius). The front side is on the LEFT of the walking
// direction in the (a right, r up) half plane (i.e. walk clockwise around a solid's cross-section).
inline void lathe(PMesh& m, vec3 o, vec3 axis, vec3 ref, const std::vector<vec2>& prof, int seg, float a0 = 0.f,
                  float a1 = kTwoPi) {
    vec3 ax = normalize(axis);
    vec3 rf = normalize(ref - ax * dot(ref, ax));
    vec3 bn = cross(ax, rf);
    bool full = fabsf(a1 - a0 - kTwoPi) < 1e-4f;
    int cols = full ? seg : seg + 1;
    int rows = (int)prof.size();
    std::vector<u32> id(rows * cols);
    std::vector<float> arc(rows, 0.f);
    for (int i = 1; i < rows; i++) arc[i] = arc[i - 1] + length(prof[i] - prof[i - 1]);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) {
            float th = a0 + (a1 - a0) * j / seg;
            vec3 rd = rf * cosf(th) + bn * sinf(th);
            float r = prof[i].y;
            id[i * cols + j] = m.add(o + ax * prof[i].x + rd * r, vec2(th * Max(r, 0.05f), arc[i]));
        }
    gridQuads(m, id, rows, cols, full, true);
}

// Parallel-transport frames along a polyline.
inline void pathFrames(const std::vector<vec3>& path, std::vector<vec3>& T, std::vector<vec3>& N, bool closed, vec3 upHint) {
    size_t n = path.size();
    T.resize(n);
    N.resize(n);
    for (size_t i = 0; i < n; i++) {
        vec3 a = (i > 0) ? path[i - 1] : (closed ? path[n - 1] : path[i]);
        vec3 b = (i + 1 < n) ? path[i + 1] : (closed ? path[0] : path[i]);
        T[i] = normalize(b - a);
    }
    vec3 n0 = upHint - T[0] * dot(upHint, T[0]);
    if (length2(n0) < 1e-6f) n0 = anyPerp(T[0]);
    N[0] = normalize(n0);
    for (size_t i = 1; i < n; i++) {
        vec3 v = N[i - 1] - T[i] * dot(N[i - 1], T[i]);
        if (length2(v) < 1e-10f) v = anyPerp(T[i]);
        N[i] = normalize(v);
    }
}

// Tube swept along a path (outward normals); radii per point.
inline void tube(PMesh& m, const std::vector<vec3>& path, const std::vector<float>& rad, int seg, bool capA, bool capB,
                 bool closed = false, vec3 upHint = vec3(0, 0, 1)) {
    std::vector<vec3> T, N;
    pathFrames(path, T, N, closed, upHint);
    int rows = (int)path.size();
    int cols = seg;
    std::vector<u32> id(rows * cols);
    float along = 0;
    for (int i = 0; i < rows; i++) {
        if (i > 0) along += length(path[i] - path[i - 1]);
        vec3 B = cross(T[i], N[i]);
        float r = rad[Min(i, (int)rad.size() - 1)];
        for (int j = 0; j < cols; j++) {
            float th = kTwoPi * j / seg;
            id[i * cols + j] = m.add(path[i] + (N[i] * cosf(th) + B * sinf(th)) * r, vec2(th * r, along));
        }
    }
    if (closed) {
        std::vector<u32> id2(id);
        for (int j = 0; j < cols; j++) id2.push_back(id[j]);
        gridQuads(m, id2, rows + 1, cols, true, true);
    } else {
        gridQuads(m, id, rows, cols, true, true);
    }
    if (!closed && capA) {
        u32 c = m.add(path[0]);
        for (int j = 0; j < cols; j++) m.tri(c, id[(j + 1) % cols], id[j]);
    }
    if (!closed && capB) {
        u32 c = m.add(path[rows - 1]);
        int b = (rows - 1) * cols;
        for (int j = 0; j < cols; j++) m.tri(c, id[b + j], id[b + (j + 1) % cols]);
    }
}
inline void tube1(PMesh& m, const std::vector<vec3>& path, float r, int seg, bool caps = true, vec3 upHint = vec3(0, 0, 1)) {
    std::vector<float> rad(1, r);
    tube(m, path, rad, seg, caps, caps, false, upHint);
}
// Straight cylinder between two points
inline void cyl(PMesh& m, vec3 a, vec3 b, float r, int seg, bool caps = true) {
    std::vector<vec3> p;
    p.push_back(a);
    p.push_back(b);
    vec3 d = normalize(b - a);
    tube1(m, p, r, seg, caps, fabsf(d.z) > 0.9f ? vec3(1, 0, 0) : vec3(0, 0, 1));
}

// Smooth polyline through control points (Catmull-Rom), n samples per span.
inline std::vector<vec3> catmull(const std::vector<vec3>& c, int n) {
    std::vector<vec3> out;
    int k = (int)c.size();
    if (k < 2) return c;
    for (int i = 0; i + 1 < k; i++) {
        vec3 p0 = c[Max(i - 1, 0)], p1 = c[i], p2 = c[i + 1], p3 = c[Min(i + 2, k - 1)];
        for (int s = 0; s < n; s++) {
            float t = (float)s / n, t2 = t * t, t3 = t2 * t;
            out.push_back((p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f);
        }
    }
    out.push_back(c[k - 1]);
    return out;
}

// Rounded box (edges filleted with radius r) in a frame; `rs` fillet subdivisions per 45 degrees.
inline void roundedBox(PMesh& m, const Frame& fr, vec3 h, float r, int rs = 2) {
    PMesh::Mark mk0 = m.mark();
    r = Min(r, Min(h.x, Min(h.y, h.z)) * 0.999f);
    // samples along one axis of half length hh
    auto samples = [&](float hh) {
        std::vector<float> s;
        float in = hh - r;
        if (r > 1e-5f) {
            for (int k = rs; k >= 1; k--) s.push_back(-(in + r * tanf(kPi * 0.25f * k / rs)));
        }
        s.push_back(-in);
        if (in > 1e-5f) s.push_back(in);
        if (r > 1e-5f) {
            for (int k = 1; k <= rs; k++) s.push_back(in + r * tanf(kPi * 0.25f * k / rs));
        }
        return s;
    };
    vec3 inner = h - vec3(r);
    for (int ax = 0; ax < 3; ax++)
        for (int sg = -1; sg <= 1; sg += 2) {
            int ua = (ax + 1) % 3, va = (ax + 2) % 3;
            if (sg < 0) std::swap(ua, va);
            std::vector<float> su = samples(h[ua]), sv = samples(h[va]);
            int rows = (int)su.size(), cols = (int)sv.size();
            std::vector<u32> id(rows * cols);
            for (int i = 0; i < rows; i++)
                for (int j = 0; j < cols; j++) {
                    vec3 q;
                    q[ax] = h[ax] * sg;
                    q[ua] = su[i];
                    q[va] = sv[j];
                    vec3 c(Clamp(q.x, -inner.x, inner.x), Clamp(q.y, -inner.y, inner.y), Clamp(q.z, -inner.z, inner.z));
                    vec3 d = q - c;
                    vec3 p = r > 1e-5f ? c + normalize(d) * r : q;
                    id[i * cols + j] = m.add(fr.at(p));
                }
            gridQuads(m, id, rows, cols, false, false);
        }
    if (leftHanded(fr)) m.flipSince(mk0);
}
inline void roundedBoxAt(PMesh& m, vec3 c, vec3 h, float r, int rs = 2) { roundedBox(m, Frame(c, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), h, r, rs); }

// Ellipsoid (or partial, v from v0..v1 in [0, pi] measured from the +z pole)
inline void ellipsoid(PMesh& m, const Frame& fr, vec3 rad, int su, int sv, float v0 = 0.f, float v1 = kPi) {
    PMesh::Mark mk0 = m.mark();
    int rows = sv + 1, cols = su;
    std::vector<u32> id(rows * cols);
    for (int i = 0; i < rows; i++) {
        float v = v0 + (v1 - v0) * i / sv;
        for (int j = 0; j < cols; j++) {
            float u = kTwoPi * j / su;
            vec3 l(sinf(v) * cosf(u) * rad.x, sinf(v) * sinf(u) * rad.y, cosf(v) * rad.z);
            id[i * cols + j] = m.add(fr.at(l));
        }
    }
    gridQuads(m, id, rows, cols, true, false);
    if (v1 < kPi - 1e-3f) {  // cap the open end
        u32 c = m.add(fr.at(vec3(0, 0, cosf(v1) * rad.z)));
        int b = sv * cols;
        for (int j = 0; j < cols; j++) m.tri(c, id[b + (j + 1) % cols], id[b + j]);
    }
    if (leftHanded(fr)) m.flipSince(mk0);
}

// Prism: polygon (CCW in (U,V)) extruded along D = cross(U,V) from d0 to d1.
inline void extrude(PMesh& m, const std::vector<vec2>& polyIn, const Frame& fr, float d0, float d1, bool cap0 = true, bool cap1 = true) {
    PMesh::Mark mk0 = m.mark();
    std::vector<vec2> poly = polyIn;
    if (polygonArea2D(poly.data(), (int)poly.size()) < 0.f) std::reverse(poly.begin(), poly.end());
    if (d1 < d0) std::swap(d0, d1);
    int n = (int)poly.size();
    std::vector<u32> lo(n), hi(n);
    for (int i = 0; i < n; i++) {
        lo[i] = m.add(fr.at(vec3(poly[i].x, poly[i].y, d0)));
        hi[i] = m.add(fr.at(vec3(poly[i].x, poly[i].y, d1)));
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        m.quad(lo[i], lo[j], hi[j], hi[i]);
    }
    std::vector<u32> tris;
    triangulatePolygon(poly, tris);
    for (size_t k = 0; k + 2 < tris.size(); k += 3) {
        u32 a = tris[k], b = tris[k + 1], c = tris[k + 2];
        vec2 e1 = poly[b] - poly[a], e2 = poly[c] - poly[a];
        if (cross(e1, e2) < 0.f) std::swap(b, c);
        if (cap1) m.tri(hi[a], hi[b], hi[c]);
        if (cap0) m.tri(lo[a], lo[c], lo[b]);
    }
    if (leftHanded(fr)) m.flipSince(mk0);
}

// Flat disk (fan) facing `n`
inline void disk(PMesh& m, vec3 c, vec3 n, float r, int seg, float r0 = 0.f) {
    vec3 x = normalize(anyPerp(n)), y = cross(n, x);
    std::vector<u32> ring(seg), inner(seg);
    for (int i = 0; i < seg; i++) {
        float a = kTwoPi * i / seg;
        ring[i] = m.add(c + (x * cosf(a) + y * sinf(a)) * r);
        if (r0 > 0.f) inner[i] = m.add(c + (x * cosf(a) + y * sinf(a)) * r0);
    }
    if (r0 > 0.f) {
        for (int i = 0; i < seg; i++) m.quad(inner[i], ring[i], ring[(i + 1) % seg], inner[(i + 1) % seg]);
    } else {
        u32 ci = m.add(c);
        for (int i = 0; i < seg; i++) m.tri(ci, ring[i], ring[(i + 1) % seg]);
    }
}

// Torus around axis (ring radius R, tube radius r)
inline void torus(PMesh& m, const Frame& fr, float R, float r, int su, int sv, float a0 = 0.f, float a1 = kTwoPi) {
    PMesh::Mark mk0 = m.mark();
    bool full = fabsf(a1 - a0 - kTwoPi) < 1e-4f;
    int rows = full ? su : su + 1;
    std::vector<u32> id(rows * sv);
    for (int i = 0; i < rows; i++) {
        float a = a0 + (a1 - a0) * i / su;
        vec3 rd(cosf(a), sinf(a), 0);
        for (int j = 0; j < sv; j++) {
            float b = kTwoPi * j / sv;
            vec3 l = rd * (R + r * cosf(b)) + vec3(0, 0, r * sinf(b));
            id[i * sv + j] = m.add(fr.at(l));
        }
    }
    if (full) {
        std::vector<u32> id2(id);
        for (int j = 0; j < sv; j++) id2.push_back(id[j]);
        gridQuads(m, id2, rows + 1, sv, true, false);
    } else {
        gridQuads(m, id, rows, sv, true, false);
    }
    if (leftHanded(fr)) m.flipSince(mk0);
}

// ------------------------------------------------------------------------------------------------
// 2D outline helpers (for decals and extrusions)
inline std::vector<vec2> shapeEllipse(vec2 c, float rx, float ry, int n) {
    std::vector<vec2> s;
    for (int i = 0; i < n; i++) {
        float a = kTwoPi * i / n;
        s.push_back(c + vec2(cosf(a) * rx, sinf(a) * ry));
    }
    return s;
}
// Polygon with rounded corners (per-corner radius r[i] or a single radius). CCW input -> CCW output.
inline std::vector<vec2> shapeRounded(const std::vector<vec2>& pts, const std::vector<float>& rad, int seg = 4) {
    std::vector<vec2> s;
    int n = (int)pts.size();
    for (int i = 0; i < n; i++) {
        vec2 p = pts[i], a = pts[(i + n - 1) % n], b = pts[(i + 1) % n];
        float r = rad[Min(i, (int)rad.size() - 1)];
        vec2 da = normalize(a - p), db = normalize(b - p);
        float ang = acosf(Clamp(dot(da, db), -1.f, 1.f));
        if (r < 1e-4f || ang > kPi - 0.01f) { s.push_back(p); continue; }
        float t = r / tanf(ang * 0.5f);
        t = Min(t, Min(length(a - p), length(b - p)) * 0.49f);
        vec2 p0 = p + da * t, p1 = p + db * t;
        // quadratic bezier p0 -> p -> p1
        for (int k = 0; k <= seg; k++) {
            float u = (float)k / seg;
            s.push_back(p0 * ((1 - u) * (1 - u)) + p * (2 * u * (1 - u)) + p1 * (u * u));
        }
    }
    return s;
}
inline std::vector<vec2> shapeRounded(const std::vector<vec2>& pts, float r, int seg = 4) {
    return shapeRounded(pts, std::vector<float>(1, r), seg);
}
inline std::vector<vec2> shapeRoundRect(vec2 c, float hw, float hh, float r, int seg = 4) {
    std::vector<vec2> p;
    p.push_back(c + vec2(-hw, -hh));
    p.push_back(c + vec2(hw, -hh));
    p.push_back(c + vec2(hw, hh));
    p.push_back(c + vec2(-hw, hh));
    return shapeRounded(p, r, seg);
}
// Resample a closed outline to n points evenly spaced by arc length.
inline std::vector<vec2> resampleClosed(const std::vector<vec2>& s, int n) {
    int k = (int)s.size();
    std::vector<float> acc(k + 1, 0.f);
    for (int i = 0; i < k; i++) acc[i + 1] = acc[i] + length(s[(i + 1) % k] - s[i]);
    std::vector<vec2> out;
    float total = acc[k];
    int seg = 0;
    for (int i = 0; i < n; i++) {
        float d = total * i / n;
        while (seg < k - 1 && acc[seg + 1] < d) seg++;
        float t = (d - acc[seg]) / Max(acc[seg + 1] - acc[seg], 1e-9f);
        out.push_back(lerp(s[seg], s[(seg + 1) % k], t));
    }
    return out;
}
inline vec2 centroid(const std::vector<vec2>& s) {
    vec2 c(0, 0);
    for (auto& p : s) c += p;
    return c / (float)Max((int)s.size(), 1);
}
// Offset a CCW closed outline inward by d (miter, clamped).
inline std::vector<vec2> insetClosed(const std::vector<vec2>& s, float d) {
    int n = (int)s.size();
    std::vector<vec2> o(n);
    for (int i = 0; i < n; i++) {
        vec2 a = s[(i + n - 1) % n], p = s[i], b = s[(i + 1) % n];
        vec2 e0 = normalize(p - a), e1 = normalize(b - p);
        vec2 n0 = perp(e0), n1 = perp(e1);  // inward for CCW
        vec2 nm = normalize(n0 + n1);
        float c = Max(dot(nm, n0), 0.35f);
        o[i] = p + nm * (d / c);
    }
    return o;
}

// ------------------------------------------------------------------------------------------------
// Surface projector: ray casts against a triangle soup with smooth vertex normals.
struct Projector {
    struct Tri { vec3 a, b, c, na, nb, nc, fn; };
    std::vector<Tri> tris;
    std::vector<u32> cand;
    vec3 dir;
    void addTri(vec3 a, vec3 b, vec3 c, vec3 na, vec3 nb, vec3 nc) {
        vec3 n = cross(b - a, c - a);
        if (length2(n) < 1e-14f) return;
        Tri t{a, b, c, na, nb, nc, normalize(n)};
        tris.push_back(t);
    }
    // Restrict to triangles facing against `d` whose projection overlaps the plane rectangle
    void begin(const Frame& fr, vec2 mn, vec2 mx) {
        dir = fr.z;
        cand.clear();
        for (size_t i = 0; i < tris.size(); i++) {
            const Tri& t = tris[i];
            if (dot(t.fn, dir) > -0.02f) continue;
            vec3 pts[3] = {t.a - fr.o, t.b - fr.o, t.c - fr.o};
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (int k = 0; k < 3; k++) {
                float u = dot(pts[k], fr.x), v = dot(pts[k], fr.y);
                u0 = Min(u0, u); u1 = Max(u1, u); v0 = Min(v0, v); v1 = Max(v1, v);
            }
            if (u1 < mn.x || u0 > mx.x || v1 < mn.y || v0 > mx.y) continue;
            cand.push_back((u32)i);
        }
    }
    // Moller-Trumbore with a small tolerance so rays on shared edges (e.g. the mirror seam) never slip through
    static bool hitTri(vec3 o, vec3 d, vec3 a, vec3 b, vec3 c, float& t, float& u, float& v) {
        vec3 e1 = b - a, e2 = c - a;
        vec3 pv = cross(d, e2);
        float det = dot(e1, pv);
        if (fabsf(det) < 1e-12f) return false;
        float inv = 1.f / det;
        vec3 tv = o - a;
        u = dot(tv, pv) * inv;
        const float eps = 1e-4f;
        if (u < -eps || u > 1.f + eps) return false;
        vec3 qv = cross(tv, e1);
        v = dot(d, qv) * inv;
        if (v < -eps || u + v > 1.f + eps) return false;
        t = dot(e2, qv) * inv;
        u = Clamp(u, 0.f, 1.f);
        v = Clamp(v, 0.f, 1.f - u);
        return t > 0.f;
    }
    bool cast(vec3 o, vec3& p, vec3& n) const {
        float best = 1e30f;
        bool hit = false;
        for (u32 i : cand) {
            const Tri& t = tris[i];
            float tt, u, v;
            if (hitTri(o, dir, t.a, t.b, t.c, tt, u, v) && tt < best) {
                best = tt;
                p = o + dir * tt;
                n = normalize(t.na * (1 - u - v) + t.nb * u + t.nc * v);
                hit = true;
            }
        }
        return hit;
    }
};

// A decal frame: origin o, plane axes x (u) and y (v), projection direction z (into the surface).
// Rays start `back` meters in front of the plane.
struct Decal {
    Projector* pr = nullptr;
    Frame fr;
    float back = 3.f;
    void begin(Projector& p, const Frame& f, vec2 mn, vec2 mx) {
        pr = &p;
        fr = f;
        pr->begin(fr, mn - vec2(0.05f, 0.05f), mx + vec2(0.05f, 0.05f));
    }
    void beginShape(Projector& p, const Frame& f, const std::vector<vec2>& s) {
        vec2 mn(1e9f, 1e9f), mx(-1e9f, -1e9f);
        for (auto& q : s) { mn = vmin(mn, q); mx = vmax(mx, q); }
        begin(p, f, mn, mx);
    }
    bool at(vec2 uv, vec3& p, vec3& n) const {
        vec3 o = fr.o + fr.x * uv.x + fr.y * uv.y - fr.z * back;
        return pr->cast(o, p, n);
    }
};

// Projected patch over a star-shaped outline: `rings` concentric rings from the centroid. Emits the surface
// offset along the surface normal by `off`. Returns false if any sample missed the surface.
struct PatchGrid {
    std::vector<vec3> P, N;
    std::vector<u8> ok;
    int rings = 0, n = 0;
    vec3 cP, cN;
    bool cOk = false;
};
inline void patchSample(const Decal& dc, const std::vector<vec2>& outline, vec2 c, int rings, PatchGrid& g) {
    g.rings = rings;
    g.n = (int)outline.size();
    g.P.assign(rings * g.n, vec3(0, 0, 0));
    g.N.assign(rings * g.n, vec3(0, 0, 1));
    g.ok.assign(rings * g.n, 0);
    g.cOk = dc.at(c, g.cP, g.cN);
    for (int r = 1; r <= rings; r++) {
        float s = (float)r / rings;
        for (int i = 0; i < g.n; i++) {
            vec2 q = c + (outline[i] - c) * s;
            int k = (r - 1) * g.n + i;
            g.ok[k] = dc.at(q, g.P[k], g.N[k]) ? 1 : 0;
        }
    }
}
// Emit the patch surface (all rings) offset by `off` along the normal
inline void patchEmit(PMesh& m, const PatchGrid& g, float off, int ringFrom = 0) {
    int n = g.n;
    std::vector<u32> id(g.rings * n);
    for (int k = 0; k < g.rings * n; k++) id[k] = m.add(g.P[k] + g.N[k] * off);
    if (ringFrom == 0 && g.cOk) {
        u32 c = m.add(g.cP + g.cN * off);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            if (!g.ok[i] || !g.ok[j]) continue;
            vec3 f = g.cN + g.N[i] + g.N[j];
            vec3 fn = cross(m.P[id[i]] - m.P[c], m.P[id[j]] - m.P[c]);
            if (dot(fn, f) >= 0.f) m.tri(c, id[i], id[j]);
            else m.tri(c, id[j], id[i]);
        }
    }
    for (int r = Max(ringFrom, 1); r < g.rings; r++)
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            int a = (r - 1) * n + i, b = (r - 1) * n + j, cc = r * n + j, d = r * n + i;
            if (!(g.ok[a] && g.ok[b] && g.ok[cc] && g.ok[d])) continue;
            m.quadFacing(id[a], id[d], id[cc], id[b], g.N[a] + g.N[b] + g.N[cc] + g.N[d]);
        }
}
// Wall along the outer ring of a patch from offset o0 to o1 (facing outward from the patch center)
inline void patchWall(PMesh& m, const PatchGrid& g, float o0, float o1, float scale = 1.f) {
    int n = g.n, r = g.rings - 1;
    std::vector<u32> a(n), b(n);
    for (int i = 0; i < n; i++) {
        int k = r * n + i;
        vec3 p = g.cP + (g.P[k] - g.cP) * scale;
        a[i] = m.add(p + g.N[k] * o0);
        b[i] = m.add(p + g.N[k] * o1);
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        if (!g.ok[r * n + i] || !g.ok[r * n + j]) continue;
        // outward = away from center; order so that the normal faces out
        vec3 mid = (m.P[a[i]] + m.P[a[j]]) * 0.5f;
        m.quadFacing(a[i], a[j], b[j], b[i], mid - g.cP);
    }
}
// Projected strip along a polyline (plane coords) with width w; emitted at offset `off`.
inline void stripDecal(PMesh& m, const Decal& dc, const std::vector<vec2>& line, float w, float off, float step = 0.03f) {
    // resample
    std::vector<vec2> pts;
    for (size_t i = 0; i + 1 < line.size(); i++) {
        float l = length(line[i + 1] - line[i]);
        int n = Max(1, (int)ceilf(l / step));
        for (int k = 0; k < n; k++) pts.push_back(lerp(line[i], line[i + 1], (float)k / n));
    }
    pts.push_back(line.back());
    int n = (int)pts.size();
    std::vector<u32> L(n), R(n);
    std::vector<u8> ok(n, 1);
    for (int i = 0; i < n; i++) {
        vec2 t = normalize(pts[Min(i + 1, n - 1)] - pts[Max(i - 1, 0)]);
        vec2 nn = perp(t) * (w * 0.5f);
        vec3 p0, n0, p1, n1;
        bool a = dc.at(pts[i] + nn, p0, n0), b = dc.at(pts[i] - nn, p1, n1);
        // end the strip cleanly where the surface turns away from the projection (> ~70 degrees):
        // grazing hits there would break it into dashes or wrap it onto the neighbouring face
        ok[i] = a && b && dot(n0, dc.fr.z) < -0.34f && dot(n1, dc.fr.z) < -0.34f;
        L[i] = m.add(p0 + n0 * off);
        R[i] = m.add(p1 + n1 * off);
    }
    auto triF = [&](u32 a, u32 b, u32 c, vec3 f) {
        vec3 nrm = cross(m.P[b] - m.P[a], m.P[c] - m.P[a]);
        if (dot(nrm, f) >= 0) m.tri(a, b, c);
        else m.tri(a, c, b);
    };
    for (int i = 0; i + 1 < n; i++) {
        if (!ok[i] || !ok[i + 1]) continue;
        vec3 facing = -dc.fr.z;
        triF(R[i], R[i + 1], L[i + 1], facing);
        triF(R[i], L[i + 1], L[i], facing);
    }
}

}  // namespace detail
}  // namespace Vehicles

namespace Vehicles {
namespace detail {

// ------------------------------------------------------------------------------------------------
// Generic superellipse loft along +Y: each station gives centre (x,z), half extents and exponent.
// Emits a closed tube with outward normals; ends are capped by fans (or pinched when size -> 0).
struct LoftSec {
    float y, cx, cz, hw, hh, e;   // e: superellipse exponent (2 = ellipse, larger = boxier)
    float bot = 1.f;              // scale of the lower half height (hh * bot below cz)
};
inline vec2 superEll(float a, float hw, float hh, float e) {
    float c = cosf(a), s = sinf(a);
    float x = hw * Sign(c) * powf(fabsf(c), 2.f / e);
    float z = hh * Sign(s) * powf(fabsf(s), 2.f / e);
    return vec2(x, z);
}
inline void loftY(PMesh& m, const std::vector<LoftSec>& secs, int around, bool capA, bool capB) {
    int rows = (int)secs.size();
    std::vector<u32> id(rows * around);
    for (int i = 0; i < rows; i++) {
        const LoftSec& q = secs[i];
        for (int j = 0; j < around; j++) {
            float a = kTwoPi * j / around;
            vec2 p = superEll(a, q.hw, q.hh, q.e);
            if (p.y < 0.f) p.y *= q.bot;
            id[i * around + j] = m.add(vec3(q.cx + p.x, q.y, q.cz + p.y));
        }
    }
    for (int i = 0; i + 1 < rows; i++)
        for (int j = 0; j < around; j++) {
            int j1 = (j + 1) % around;
            u32 a = id[i * around + j], b = id[(i + 1) * around + j], c = id[(i + 1) * around + j1], d = id[i * around + j1];
            vec3 ctr = (vec3(secs[i].cx, secs[i].y, secs[i].cz) + vec3(secs[i + 1].cx, secs[i + 1].y, secs[i + 1].cz)) * 0.5f;
            vec3 mid = (m.P[a] + m.P[b] + m.P[c] + m.P[d]) * 0.25f;
            vec3 out = mid - ctr;
            out.y = 0.f;
            if (length2(out) < 1e-10f) out = vec3(0, secs[i + 1].y - secs[i].y, 0);
            m.quadFacing(a, b, c, d, out);
        }
    for (int e = 0; e < 2; e++) {
        if ((e == 0 && !capA) || (e == 1 && !capB)) continue;
        int i = e == 0 ? 0 : rows - 1;
        const LoftSec& q = secs[i];
        if (q.hw < 1e-4f && q.hh < 1e-4f) continue;
        u32 c = m.add(vec3(q.cx, q.y, q.cz));
        float dir = (e == 0) ? (secs[0].y - secs[1].y) : (secs[rows - 1].y - secs[rows - 2].y);
        for (int j = 0; j < around; j++) {
            u32 a = id[i * around + j], b = id[i * around + (j + 1) % around];
            vec3 fn = cross(m.P[a] - m.P[c], m.P[b] - m.P[c]);
            if (fn.y * dir >= 0.f) m.tri(c, a, b);
            else m.tri(c, b, a);
        }
    }
}
// Same but along an arbitrary straight axis: sections given in a frame (x = side, y = along, z = up)
inline void loftFrame(PMesh& m, const Frame& fr, const std::vector<LoftSec>& secs, int around, bool capA, bool capB) {
    PMesh::Mark mk = m.mark();
    loftY(m, secs, around, capA, capB);
    m.transform(mk, fr.m());
}

}  // namespace detail
}  // namespace Vehicles
