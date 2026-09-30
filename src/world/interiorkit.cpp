// Interior construction kit: a transform-stack mesh builder working in the interior frame (model space), room shells
// with cut openings, reveals and trims, doors, and the shared library of procedural furniture and props (detailed
// for third-person close-ups: bevelled cushions, turned legs, handles, products, generated art).
// Plan mode (IB::out == nullptr) only records the interior's metadata (openings, rooms, doors, portals, scenario
// points, markers) into the InteriorDef; build mode emits geometry, collision and lights and never touches the def.
// Geometry helpers never consume the layout's random stream, so both modes see the same layout decisions.
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ colors, materials
inline u32 C(float r, float g, float b, float a = 1.f) { return packRGBA8(Saturate(r), Saturate(g), Saturate(b), Saturate(a)); }
inline u32 C(vec3 c, float a = 1.f) { return C(c.x, c.y, c.z, a); }
inline u32 Gy(float v, float a = 1.f) { return C(v, v, v, a); }
inline u32 M(MaterialId m, u32 param = 0) { return makeMat(m, param); }
// animated emissive (dynamic.hlsl / world.hlsl patterns): 1 blink, 2 chase, 3 hue cycle, 4 pulse, 5 flash, 6 night, 7 slow blink
inline u32 EM(u32 anim = 0, u32 phase = 0) { return makeMat(MAT_EMISSIVE, anim ? (anim | ((phase & 255u) << 4)) : 0u); }
inline vec3 rgbOf(u32 c) { return vec3((float)(c & 255u), (float)((c >> 8) & 255u), (float)((c >> 16) & 255u)) * (1.f / 255.f); }
inline vec3 hsv(float h, float s, float v) { return hsvToRgb(h - floorf(h), s, v); }
// see-through glass (forward glass pass): vertex alpha = clarity (1 clear .. 0 dark)
inline u32 glassCol(float clarity, vec3 tint = vec3(0.85f, 0.92f, 0.92f)) { return C(tint, clarity); }
const u32 kGlassMat = makeMat(MAT_CAR_WINDOW);

enum Skip : u32 { SK_NONE = 0, SK_PX = 1, SK_NX = 2, SK_PY = 4, SK_NY = 8, SK_PZ = 16, SK_NZ = 32 };

// Buildings for layouts: gBuildings once the world is up; during world generation (plan mode runs inside
// BuildingSet::generate, before gBuildings is published) the set being planned
thread_local const BuildingSet* tPlanBuildings = nullptr;
inline const BuildingSet* layoutBuildings() { return gBuildings ? gBuildings : tPlanBuildings; }
inline const Building* layoutBuilding(const InteriorDef& d) {
    const BuildingSet* bs = layoutBuildings();
    return (bs && d.building >= 0 && d.building < (int)bs->buildings.size()) ? &bs->buildings[d.building] : nullptr;
}

// ------------------------------------------------------------------------------------------------ builder
struct Frame {
    vec3 o = vec3(0.f), x = vec3(1, 0, 0), y = vec3(0, 1, 0), z = vec3(0, 0, 1);
    vec3 P(vec3 p) const { return o + x * p.x + y * p.y + z * p.z; }
    vec3 D(vec3 d) const { return x * d.x + y * d.y + z * d.z; }
};

struct IB {
    MeshData* parts[IP_COUNT] = {};
    InteriorMesh* out = nullptr;   // build outputs (null in plan mode)
    InteriorDef* d = nullptr;
    int part = IP_FURNITURE;
    Frame f;
    std::vector<Frame> stack;
    int roomCounter = 0, doorCounter = 0;
    bool plan() const { return out == nullptr; }
    bool geo() const { return parts[0] != nullptr; }
    MeshData& m() { return *parts[part]; }
    void push(vec3 p, float yaw) {
        stack.push_back(f);
        Frame n;
        n.o = f.P(p);
        float c = cosf(yaw), s = sinf(yaw);
        n.x = f.D(vec3(c, s, 0));
        n.y = f.D(vec3(-s, c, 0));
        n.z = f.z;
        f = n;
    }
    void pushAxes(vec3 p, vec3 lx, vec3 ly, vec3 lz) {
        stack.push_back(f);
        Frame n;
        n.o = f.P(p);
        n.x = f.D(lx);
        n.y = f.D(ly);
        n.z = f.D(lz);
        f = n;
    }
    void pop() {
        f = stack.back();
        stack.pop_back();
    }
    float modelYaw(float localYaw) const { return atan2f(-f.y.x, f.y.y) + localYaw; }
};

struct At {  // scoped placement
    IB& b;
    At(IB& bb, vec3 p, float yaw = 0.f) : b(bb) { b.push(p, yaw); }
    ~At() { b.pop(); }
};
struct InPart {
    IB& b;
    int prev;
    InPart(IB& bb, int p) : b(bb), prev(bb.part) { b.part = p; }
    ~InPart() { b.part = prev; }
};

inline float hash01(float a, float b2, float c = 0.f) {
    u32 h = hash32((u32)(int)floorf(a * 97.f) * 0x8da6b343u ^ (u32)(int)floorf(b2 * 57.f) * 0xd8163841u ^ (u32)(int)floorf(c * 31.f));
    return hashToFloat(h);
}

// ------------------------------------------------------------------------------------------------ primitives
void quad(IB& b, vec3 a, vec3 bb, vec3 c, vec3 dd, vec2 ua, vec2 ub, vec2 uc, vec2 ud, u32 col, u32 mat) {
    if (!b.geo()) return;
    b.m().quad(b.f.P(a), b.f.P(bb), b.f.P(c), b.f.P(dd), ua, ub, uc, ud, col, mat);
}
// planar quad with uvs in meters derived from its own edges (a->b = u, a->d = v)
void quadM(IB& b, vec3 a, vec3 bb, vec3 c, vec3 dd, u32 col, u32 mat, vec2 uvo = vec2(0.f)) {
    float lu = length(bb - a), lv = length(dd - a);
    quad(b, a, bb, c, dd, uvo, uvo + vec2(lu, 0), uvo + vec2(lu, lv), uvo + vec2(0, lv), col, mat);
}
// quad whose winding is fixed so that it faces `facing` (local)
void quadF(IB& b, vec3 a, vec3 bb, vec3 c, vec3 dd, vec3 facing, u32 col, u32 mat, vec2 uvo = vec2(0.f)) {
    if (dot(cross(bb - a, dd - a), facing) < 0.f) quadM(b, a, dd, c, bb, col, mat, uvo);
    else quadM(b, a, bb, c, dd, col, mat, uvo);
}
void tri(IB& b, vec3 p0, vec3 p1, vec3 p2, u32 col, u32 mat) {
    if (!b.geo()) return;
    vec3 a = b.f.P(p0), c1 = b.f.P(p1), c2 = b.f.P(p2);
    vec3 n = normalize(cross(c1 - a, c2 - a));
    vec3 t = normalize(c1 - a);
    MeshData& m = b.m();
    u32 i0 = m.addVertex(a, n, t, p0.xy(), col, mat), i1 = m.addVertex(c1, n, t, p1.xy(), col, mat), i2 = m.addVertex(c2, n, t, p2.xy(), col, mat);
    m.tri(i0, i1, i2);
}

// Axis-aligned box in the local frame; skip = faces left out (hidden against walls / floor)
void box(IB& b, vec3 c, vec3 h, u32 col, u32 mat, u32 skip = SK_NZ) {
    if (!b.geo()) return;
    vec3 n = c - h, p = c + h;
    if (!(skip & SK_PX)) quad(b, vec3(p.x, n.y, n.z), vec3(p.x, p.y, n.z), vec3(p.x, p.y, p.z), vec3(p.x, n.y, p.z), vec2(n.y, n.z), vec2(p.y, n.z), vec2(p.y, p.z), vec2(n.y, p.z), col, mat);
    if (!(skip & SK_NX)) quad(b, vec3(n.x, p.y, n.z), vec3(n.x, n.y, n.z), vec3(n.x, n.y, p.z), vec3(n.x, p.y, p.z), vec2(-p.y, n.z), vec2(-n.y, n.z), vec2(-n.y, p.z), vec2(-p.y, p.z), col, mat);
    if (!(skip & SK_PY)) quad(b, vec3(p.x, p.y, n.z), vec3(n.x, p.y, n.z), vec3(n.x, p.y, p.z), vec3(p.x, p.y, p.z), vec2(-p.x, n.z), vec2(-n.x, n.z), vec2(-n.x, p.z), vec2(-p.x, p.z), col, mat);
    if (!(skip & SK_NY)) quad(b, vec3(n.x, n.y, n.z), vec3(p.x, n.y, n.z), vec3(p.x, n.y, p.z), vec3(n.x, n.y, p.z), vec2(n.x, n.z), vec2(p.x, n.z), vec2(p.x, p.z), vec2(n.x, p.z), col, mat);
    if (!(skip & SK_PZ)) quad(b, vec3(n.x, n.y, p.z), vec3(p.x, n.y, p.z), vec3(p.x, p.y, p.z), vec3(n.x, p.y, p.z), vec2(n.x, n.y), vec2(p.x, n.y), vec2(p.x, p.y), vec2(n.x, p.y), col, mat);
    if (!(skip & SK_NZ)) quad(b, vec3(n.x, p.y, n.z), vec3(p.x, p.y, n.z), vec3(p.x, n.y, n.z), vec3(n.x, n.y, n.z), vec2(n.x, -p.y), vec2(p.x, -p.y), vec2(p.x, -n.y), vec2(n.x, -n.y), col, mat);
}
// Box from min/max corners
inline void boxMM(IB& b, vec3 mn, vec3 mx, u32 col, u32 mat, u32 skip = SK_NZ) { box(b, (mn + mx) * 0.5f, (mx - mn) * 0.5f, col, mat, skip); }

// Rounded (chamfered, smooth shaded) box: cushions, mattresses, counters, appliances
void rbox(IB& b, vec3 c, vec3 h, float r, u32 col, u32 mat, bool bottom = false) {
    if (!b.geo()) return;
    r = Min(r, Min(h.x, Min(h.y, h.z)) * 0.95f);
    if (r < 0.003f) {
        box(b, c, h, col, mat, bottom ? SK_NONE : SK_NZ);
        return;
    }
    MeshData& m = b.m();
    vec3 in = h - vec3(r);
    auto V = [&](vec3 lp, vec3 ln, vec3 lt, vec2 uv) { return m.addVertex(b.f.P(c + lp), b.f.D(ln), b.f.D(lt), uv, col, mat); };
    auto orientTri = [&](u32 i0, u32 i1, u32 i2, vec3 want) {
        vec3 p0 = m.verts[i0].pos, p1 = m.verts[i1].pos, p2 = m.verts[i2].pos;
        if (dot(cross(p1 - p0, p2 - p0), b.f.D(want)) >= 0.f) m.tri(i0, i1, i2);
        else m.tri(i0, i2, i1);
    };
    // faces
    for (int ax = 0; ax < 3; ax++)
        for (int s = -1; s <= 1; s += 2) {
            if (ax == 2 && s < 0 && !bottom) continue;
            int u = (ax + 1) % 3, v = (ax + 2) % 3;
            vec3 nrm(0.f);
            (&nrm.x)[ax] = (float)s;
            vec3 tu(0.f);
            (&tu.x)[u] = 1.f;
            vec3 q[4];
            float su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
            u32 id[4];
            for (int k = 0; k < 4; k++) {
                vec3 lp(0.f);
                (&lp.x)[ax] = s * h[ax];
                (&lp.x)[u] = su[k] * in[u];
                (&lp.x)[v] = sv[k] * in[v];
                q[k] = lp;
                id[k] = V(lp, nrm, tu, vec2((&lp.x)[u], (&lp.x)[v]));
            }
            orientTri(id[0], id[1], id[2], nrm);
            orientTri(id[0], id[2], id[3], nrm);
        }
    // edge strips (smooth: each side keeps its face normal)
    for (int ax = 0; ax < 3; ax++) {  // edge runs along ax
        int i = (ax + 1) % 3, j = (ax + 2) % 3;
        for (int si = -1; si <= 1; si += 2)
            for (int sj = -1; sj <= 1; sj += 2) {
                if (!bottom && ((i == 2 && si < 0) || (j == 2 && sj < 0))) continue;
                vec3 ni(0.f), nj(0.f), ta(0.f);
                (&ni.x)[i] = (float)si;
                (&nj.x)[j] = (float)sj;
                (&ta.x)[ax] = 1.f;
                u32 id[4];
                for (int k = 0; k < 4; k++) {
                    float sa = (k == 0 || k == 3) ? -1.f : 1.f;
                    bool onI = k < 2;  // first two on face i, last two on face j
                    vec3 lp(0.f);
                    (&lp.x)[ax] = sa * in[ax];
                    (&lp.x)[i] = si * (onI ? h[i] : in[i]);
                    (&lp.x)[j] = sj * (onI ? in[j] : h[j]);
                    id[k] = V(lp, onI ? ni : nj, ta, vec2((&lp.x)[ax], (float)k * r));
                }
                // order: (a0 on i), (a1 on i), (a1 on j) -> k=0:(-,i) k=1:(+,i) k=2:(+,j)? rebuild explicit
                u32 a0i = id[0], a1i = id[1], a1j = id[2], a0j = id[3];
                vec3 want = ni + nj;
                orientTri(a0i, a1i, a1j, want);
                orientTri(a0i, a1j, a0j, want);
            }
    }
    // corners
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                if (!bottom && sz < 0) continue;
                vec3 s((float)sx, (float)sy, (float)sz);
                u32 i0 = V(vec3(s.x * h.x, s.y * in.y, s.z * in.z), vec3(s.x, 0, 0), vec3(0, 1, 0), vec2(0.f));
                u32 i1 = V(vec3(s.x * in.x, s.y * h.y, s.z * in.z), vec3(0, s.y, 0), vec3(1, 0, 0), vec2(r, 0.f));
                u32 i2 = V(vec3(s.x * in.x, s.y * in.y, s.z * h.z), vec3(0, 0, s.z), vec3(1, 0, 0), vec2(0.f, r));
                orientTri(i0, i1, i2, s);
            }
}

// Cylinder / cone along local z from base (r0 bottom, r1 top)
void cyl(IB& b, vec3 base, float r0, float r1, float h, int seg, u32 col, u32 mat, bool capTop = true, bool capBot = false) {
    if (!b.geo()) return;
    MeshData& m = b.m();
    u32 start = (u32)m.verts.size();
    float slope = (r0 - r1) / Max(h, 1e-3f);
    for (int i = 0; i <= seg; i++) {
        float a = kTwoPi * i / seg;
        vec3 rd(cosf(a), sinf(a), 0.f);
        vec3 n = normalize(rd + vec3(0, 0, slope));
        vec3 t(-rd.y, rd.x, 0.f);
        float u = (float)i / seg * kTwoPi * Max(Max(r0, r1), 0.05f);
        m.addVertex(b.f.P(base + rd * r0), b.f.D(n), b.f.D(t), vec2(u, 0.f), col, mat);
        m.addVertex(b.f.P(base + rd * r1 + vec3(0, 0, h)), b.f.D(n), b.f.D(t), vec2(u, h), col, mat);
    }
    for (int i = 0; i < seg; i++) {
        u32 a = start + i * 2, c = a + 2;
        m.quadIdx(a, c, c + 1, a + 1);
    }
    for (int cap = 0; cap < 2; cap++) {
        if ((cap == 0 && !capTop) || (cap == 1 && !capBot)) continue;
        float r = cap == 0 ? r1 : r0;
        if (r < 1e-3f) continue;
        float z = cap == 0 ? h : 0.f;
        vec3 n(0, 0, cap == 0 ? 1.f : -1.f);
        u32 ci = m.addVertex(b.f.P(base + vec3(0, 0, z)), b.f.D(n), b.f.D(vec3(1, 0, 0)), vec2(0.f), col, mat);
        u32 first = (u32)m.verts.size();
        for (int i = 0; i <= seg; i++) {
            float a = kTwoPi * i / seg;
            m.addVertex(b.f.P(base + vec3(cosf(a) * r, sinf(a) * r, z)), b.f.D(n), b.f.D(vec3(1, 0, 0)), vec2(cosf(a), sinf(a)) * r, col, mat);
        }
        for (int i = 0; i < seg; i++) {
            if (cap == 0) m.tri(ci, first + i, first + i + 1);
            else m.tri(ci, first + i + 1, first + i);
        }
    }
}

// Tube between two local points (legs, rails, pipes, rods)
void tube(IB& b, vec3 a, vec3 c, float r, int seg, u32 col, u32 mat, bool caps = false) {
    if (!b.geo()) return;
    vec3 d = c - a;
    float L = length(d);
    if (L < 1e-4f) return;
    vec3 z = d / L;
    vec3 x = normalize(anyPerp(z));
    vec3 y = cross(z, x);
    b.pushAxes(a, x, y, z);
    cyl(b, vec3(0.f), r, r, L, seg, col, mat, caps, caps);
    b.pop();
}

// Surface of revolution around local z: profile points (radius, height) bottom to top
void lathe(IB& b, vec3 base, const vec2* prof, int n, int seg, u32 col, u32 mat, bool capTop = true) {
    if (!b.geo() || n < 2) return;
    MeshData& m = b.m();
    u32 start = (u32)m.verts.size();
    float v = 0.f;
    for (int i = 0; i < n; i++) {
        vec2 dd = i + 1 < n ? prof[i + 1] - prof[i] : prof[i] - prof[i - 1];
        if (i > 0 && i + 1 < n) dd = prof[i + 1] - prof[i - 1];
        vec2 pn = normalize(vec2(dd.y, -dd.x));
        if (i > 0) v += length(prof[i] - prof[i - 1]);
        for (int k = 0; k <= seg; k++) {
            float an = kTwoPi * k / seg;
            vec3 rd(cosf(an), sinf(an), 0.f);
            vec3 nrm = normalize(rd * pn.x + vec3(0, 0, pn.y));
            m.addVertex(b.f.P(base + rd * prof[i].x + vec3(0, 0, prof[i].y)), b.f.D(nrm), b.f.D(vec3(-rd.y, rd.x, 0)),
                        vec2((float)k / seg * kTwoPi * Max(prof[i].x, 0.03f), v), col, mat);
        }
    }
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k < seg; k++) {
            u32 a = start + (u32)(i * (seg + 1) + k), c = a + 1, e = a + seg + 1, g = e + 1;
            m.quadIdx(a, c, g, e);
        }
    if (capTop && prof[n - 1].x > 0.002f) {
        vec3 ctr = base + vec3(0, 0, prof[n - 1].y);
        u32 ci = m.addVertex(b.f.P(ctr), b.f.D(vec3(0, 0, 1)), b.f.D(vec3(1, 0, 0)), vec2(0.f), col, mat);
        u32 first = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float an = kTwoPi * k / seg;
            m.addVertex(b.f.P(ctr + vec3(cosf(an), sinf(an), 0.f) * prof[n - 1].x), b.f.D(vec3(0, 0, 1)), b.f.D(vec3(1, 0, 0)),
                        vec2(cosf(an), sinf(an)) * prof[n - 1].x, col, mat);
        }
        for (int k = 0; k < seg; k++) m.tri(ci, first + k, first + k + 1);
    }
}
void lathe(IB& b, vec3 base, std::initializer_list<vec2> prof, int seg, u32 col, u32 mat, bool capTop = true) {
    lathe(b, base, prof.begin(), (int)prof.size(), seg, col, mat, capTop);
}
void sphere(IB& b, vec3 c, float r, int seg, u32 col, u32 mat, float squash = 1.f) {
    if (!b.geo()) return;
    vec2 prof[9];
    int n = Clamp(seg / 2 + 1, 3, 9);
    for (int i = 0; i < n; i++) {
        float a = -kHalfPi + kPi * i / (n - 1);
        prof[i] = vec2(Max(cosf(a) * r, 0.0005f), sinf(a) * r * squash + r * squash);
    }
    lathe(b, c - vec3(0, 0, r * squash), prof, n, seg, col, mat, false);
}
// Flat disc facing local +z
void disc(IB& b, vec3 c, float r, int seg, u32 col, u32 mat) {
    if (!b.geo()) return;
    MeshData& m = b.m();
    u32 ci = m.addVertex(b.f.P(c), b.f.D(vec3(0, 0, 1)), b.f.D(vec3(1, 0, 0)), vec2(0.f), col, mat);
    u32 first = (u32)m.verts.size();
    for (int k = 0; k <= seg; k++) {
        float an = kTwoPi * k / seg;
        m.addVertex(b.f.P(c + vec3(cosf(an), sinf(an), 0.f) * r), b.f.D(vec3(0, 0, 1)), b.f.D(vec3(1, 0, 0)), vec2(cosf(an), sinf(an)) * r, col, mat);
    }
    for (int k = 0; k < seg; k++) m.tri(ci, first + k, first + k + 1);
}

// Stroke text on a plane: origin = bottom-left, right/up = unit axes (local), front side = cross(right, up)
float text(IB& b, const char* s, vec3 origin, vec3 right, vec3 up, float h, float stroke, u32 col, u32 mat, float depth = 0.f, float spacing = 0.35f) {
    if (!b.geo()) return sitegeo::textAdvance(s, h, spacing);
    sitegeo::G g;
    g.m = &b.m();
    g.org = vec3(0.f);
    return sitegeo::strokeText(g, b.m(), s, b.f.P(origin), normalize(b.f.D(right)), normalize(b.f.D(up)), h, stroke, col, mat, depth, spacing);
}
// Centered text: center = middle of the baseline row
void textC(IB& b, const char* s, vec3 center, vec3 right, vec3 up, float h, float stroke, u32 col, u32 mat, float depth = 0.f, float spacing = 0.35f) {
    float w = sitegeo::textAdvance(s, h, spacing) - h / 9.f * 6.f * spacing;
    text(b, s, center - right * (w * 0.5f) - up * (h * 0.5f), right, up, h, stroke, col, mat, depth, spacing);
}

// ------------------------------------------------------------------------------------------------ metadata / outputs
void collide(IB& b, vec3 c, vec3 he) {
    if (!b.out) return;
    CollisionBox cb;
    cb.c = b.d->toWorld(b.f.P(c));
    vec2 axm = normalize(vec2(b.f.x.x, b.f.x.y));
    cb.ax = normalize(b.d->dirToWorld(axm));
    cb.he = he;
    b.out->col.push_back(cb);
}
void collideMM(IB& b, vec3 mn, vec3 mx) { collide(b, (mn + mx) * 0.5f, (mx - mn) * 0.5f); }

void light(IB& b, vec3 p, vec3 color, float radius, int room, vec3 dir = vec3(0.f), float outerDeg = 0.f, float innerDeg = 0.f, u8 anim = 0, u8 phase = 0) {
    if (!b.out) return;
    InteriorLight L;
    L.pos = b.f.P(p);
    L.color = color;
    L.radius = radius;
    if (outerDeg > 0.f && length2(dir) > 0.f) {
        L.dir = normalize(b.f.D(dir));
        L.cosOuter = cosf(outerDeg * kDegToRad);
        L.cosInner = cosf(Max(innerDeg, 1.f) * kDegToRad);
    } else {
        L.dir = vec3(0, 0, -1);
    }
    L.anim = anim;
    L.phase = phase;
    L.room = room < 0 ? 255 : (u8)room;
    b.out->lights.push_back(L);
}

void scenario(IB& b, vec3 p, float yawLocal, u8 stance, u8 role, u8 flags = 0, float lift = 0.f) {
    if (!b.plan()) return;
    InteriorScenario s;
    s.pos = b.f.P(p);
    s.yaw = b.modelYaw(yawLocal);
    s.stance = stance;
    s.role = role;
    s.flags = flags;
    s.lift = lift;
    b.d->scenarios.push_back(s);
}

void marker(IB& b, u8 kind, vec3 p, float yawLocal = 0.f) {
    if (!b.plan()) return;
    InteriorMarker mk;
    mk.kind = kind;
    mk.pos = b.f.P(p);
    mk.yaw = b.modelYaw(yawLocal);
    b.d->markers.push_back(mk);
}

// Rooms are declared in model space (the layout's root frame)
int room(IB& b, vec3 mn, vec3 mx, vec3 lampAmbient, float dayBounce, u8 schedule, bool outdoor = false) {
    int idx = b.roomCounter++;
    if (b.plan()) {
        InteriorRoom r;
        r.mn = mn;
        r.mx = mx;
        r.lampAmbient = lampAmbient;
        r.dayBounce = dayBounce;
        r.schedule = schedule;
        r.outdoor = outdoor;
        b.d->rooms.push_back(r);
    }
    return idx;
}

// ------------------------------------------------------------------------------------------------ openings & walls
// A hole in a wall face: s (along the face's u axis) and z ranges
struct Hole {
    float s0, s1, z0, z1;
};

// Wall face with rectangular holes: origin o (model space, bottom-left), unit u along the wall, face normal nrm,
// width w, height h. The grid split keeps vertices shared between neighbouring pieces (no T-junction cracks).
void wallWithHoles(IB& b, vec3 o, vec3 u, vec3 nrm, float w, float h, const std::vector<Hole>& holes, u32 col, u32 mat, float uOff = 0.f) {
    if (!b.geo() || w < 1e-3f || h < 1e-3f) return;
    float xs[40], zs[40];
    int nx = 0, nz = 0;
    xs[nx++] = 0.f;
    xs[nx++] = w;
    zs[nz++] = 0.f;
    zs[nz++] = h;
    for (const Hole& ho : holes) {
        float s0 = Clamp(ho.s0, 0.f, w), s1 = Clamp(ho.s1, 0.f, w), z0 = Clamp(ho.z0, 0.f, h), z1 = Clamp(ho.z1, 0.f, h);
        if (s1 - s0 < 1e-3f || z1 - z0 < 1e-3f) continue;
        if (nx < 38) { xs[nx++] = s0; xs[nx++] = s1; }
        if (nz < 38) { zs[nz++] = z0; zs[nz++] = z1; }
    }
    std::sort(xs, xs + nx);
    std::sort(zs, zs + nz);
    nx = (int)(std::unique(xs, xs + nx, [](float a, float c) { return fabsf(a - c) < 1e-4f; }) - xs);
    nz = (int)(std::unique(zs, zs + nz, [](float a, float c) { return fabsf(a - c) < 1e-4f; }) - zs);
    vec3 up(0, 0, 1);  // face frames satisfy cross(u, up) = nrm (see faceFrame)
    (void)nrm;
    for (int i = 0; i + 1 < nx; i++)
        for (int k = 0; k + 1 < nz; k++) {
            float xa = xs[i], xb = xs[i + 1], za = zs[k], zb = zs[k + 1];
            float xm = (xa + xb) * 0.5f, zm = (za + zb) * 0.5f;
            bool cut = false;
            for (const Hole& ho : holes)
                if (xm > ho.s0 && xm < ho.s1 && zm > ho.z0 && zm < ho.z1) { cut = true; break; }
            if (cut) continue;
            quad(b, o + u * xa + up * za, o + u * xb + up * za, o + u * xb + up * zb, o + u * xa + up * zb, vec2(uOff + xa, za), vec2(uOff + xb, za),
                 vec2(uOff + xb, zb), vec2(uOff + xa, zb), col, mat);
        }
}

// Lining of an opening through a wall: planes at depth d0..d1 along n, hole rect (s along u, z)
void reveal(IB& b, vec3 o, vec3 u, vec3 n, float s0, float s1, float z0, float z1, float d0, float d1, u32 col, u32 mat, bool sill = true) {
    if (!b.geo()) return;
    vec3 up(0, 0, 1);
    auto P = [&](float s, float z, float dd) { return o + u * s + up * z + n * dd; };
    // jambs face the opening center, the head faces down, the sill up
    quadF(b, P(s0, z0, d0), P(s0, z0, d1), P(s0, z1, d1), P(s0, z1, d0), u, col, mat);
    quadF(b, P(s1, z0, d0), P(s1, z0, d1), P(s1, z1, d1), P(s1, z1, d0), -u, col, mat);
    quadF(b, P(s0, z1, d0), P(s1, z1, d0), P(s1, z1, d1), P(s0, z1, d1), -up, col, mat);
    if (sill) quadF(b, P(s0, z0, d0), P(s1, z0, d0), P(s1, z0, d1), P(s0, z0, d1), up, col, mat);
}

// ------------------------------------------------------------------------------------------------ room shell
struct ShellStyle {
    u32 wallCol = 0xffe0e0e0u, wallMat = makeMat(MAT_PLASTER);
    u32 floorCol = 0xffffffffu, floorMat = makeMat(MAT_WOOD_FLOOR);
    float floorUV = 1.f;                 // uv scale (meters multiplier)
    u32 ceilCol = 0xfff0f0f0u, ceilMat = makeMat(MAT_PLASTER);
    float ceilUV = 1.f;
    u32 baseCol = 0xfff4f4f0u, baseMat = makeMat(MAT_PAINT_WHITE);
    float baseH = 0.1f;                  // baseboard height (0 = none)
    float crownH = 0.f;                  // crown molding height (0 = none)
    u32 crownCol = 0xfff4f4f0u;
    float wainscotH = 0.f;               // lower wall panel / tile height (0 = none)
    u32 wainCol = 0xffc0c0c0u, wainMat = makeMat(MAT_TILE);
    float chairRail = 0.f;               // chair rail height (0 = none)
    u32 revealCol = 0xffe8e8e8u, revealMat = makeMat(MAT_PLASTER);
    bool checker = false;                // checkerboard floor tiles
    u32 checkA = 0xffeeeeeeu, checkB = 0xff202020u;
    float checkSize = 0.3f;
    bool noCeiling = false;
    bool noFloor = false;
    u8 walls = 15;                       // bit mask of walls to emit: 1 front (-y), 2 right (+x), 4 back (+y), 8 left (-x)
};

// Extra holes registered by layouts before shell() (pass-through windows, archways) per room side
struct ExtraHole {
    int room, side;   // side: 0 front (-y face), 1 right (+x), 2 back (+y), 3 left (-x)
    Hole h;           // s measured along the face's u axis (see faceFrame)
};
thread_local std::vector<ExtraHole> tExtraHoles;

// Face frame of a room side: origin (bottom-left seen from inside), u along the wall (left to right seen from inside),
// inward normal, width
void faceFrame(const InteriorRoom& r, int side, vec3& o, vec3& u, vec3& n, float& w) {
    // o = bottom-left corner seen from inside the room, u = left-to-right along the wall, n = inward normal;
    // cross(u, +z) == n so quads (o, o+u, o+u+z, o+z) face into the room
    switch (side) {
        case 0: o = vec3(r.mx.x, r.mn.y, r.mn.z); u = vec3(-1, 0, 0); n = vec3(0, 1, 0); w = r.mx.x - r.mn.x; break;   // front (-y)
        case 1: o = vec3(r.mx.x, r.mx.y, r.mn.z); u = vec3(0, -1, 0); n = vec3(-1, 0, 0); w = r.mx.y - r.mn.y; break;  // right (+x)
        case 2: o = vec3(r.mn.x, r.mx.y, r.mn.z); u = vec3(1, 0, 0); n = vec3(0, -1, 0); w = r.mx.x - r.mn.x; break;   // back (+y)
        default: o = vec3(r.mn.x, r.mn.y, r.mn.z); u = vec3(0, 1, 0); n = vec3(1, 0, 0); w = r.mx.y - r.mn.y; break;   // left (-x)
    }
}

// Opening (model space) -> hole on a room face if the opening's plane is parallel and just outside that face
bool openingOnFace(const InteriorDef& d, const InteriorOpening& op, const InteriorRoom& r, int side, Hole& out, float& depth) {
    vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
    vec3 o, u, n;
    float w;
    faceFrame(r, side, o, u, n, w);
    vec2 dirL = normalize(c.xy() - a.xy());
    if (fabsf(dot(dirL, u.xy())) < 0.99f) return false;
    // distance of the opening plane behind the face (outward = -n)
    float da = dot(a - o, -n);
    if (da < -0.05f || da > 1.6f) return false;
    float s0 = dot(a - o, u), s1 = dot(c - o, u);
    if (s0 > s1) std::swap(s0, s1);
    if (s1 < 0.01f || s0 > w - 0.01f) return false;
    out.s0 = Max(s0, 0.f);
    out.s1 = Min(s1, w);
    out.z0 = op.z0 - d.origin.z - r.mn.z;
    out.z1 = op.z1 - d.origin.z - r.mn.z;
    depth = da;
    return true;
}

// Doorway (model space door) -> hole on a room face
bool doorOnFace(const InteriorDoor& dr, const InteriorRoom& r, int side, Hole& out) {
    vec3 o, u, n;
    float w;
    faceFrame(r, side, o, u, n, w);
    if (fabsf(dot(dr.t, u.xy())) < 0.99f) return false;
    float dist = dot(dr.c - o, n);   // door center relative to the face plane (inside positive)
    if (dist < -0.45f || dist > 0.2f) return false;
    float sc = dot(dr.c - o, u);
    if (sc < -dr.w * 0.5f || sc > w + dr.w * 0.5f) return false;
    out.s0 = sc - dr.w * 0.5f;
    out.s1 = sc + dr.w * 0.5f;
    out.z0 = dr.c.z - r.mn.z;
    out.z1 = dr.c.z - r.mn.z + dr.h;
    return true;
}

// Checkerboard / tiled floor as per-tile quads (vertex colors), clipped to the rect
void checkerFloor(IB& b, float x0, float y0, float x1, float y1, float z, float size, u32 ca, u32 cb, u32 mat) {
    if (!b.geo()) return;
    int i0 = (int)floorf(x0 / size), i1 = (int)ceilf(x1 / size), j0 = (int)floorf(y0 / size), j1 = (int)ceilf(y1 / size);
    for (int j = j0; j < j1; j++)
        for (int i = i0; i < i1; i++) {
            float ax = Max(x0, i * size), bx = Min(x1, (i + 1) * size), ay = Max(y0, j * size), by = Min(y1, (j + 1) * size);
            if (bx - ax < 1e-3f || by - ay < 1e-3f) continue;
            u32 col = ((i + j) & 1) ? cb : ca;
            quad(b, vec3(ax, ay, z), vec3(bx, ay, z), vec3(bx, by, z), vec3(ax, by, z), vec2(ax, ay), vec2(bx, ay), vec2(bx, by), vec2(ax, by), col, mat);
        }
}

// Floor, ceiling and the four inner wall faces of a room with every opening / doorway / extra hole cut out,
// reveals through the facade wall, baseboards, crown molding, wainscot and chair rail.
void shell(IB& b, int roomIdx, const ShellStyle& st) {
    if (!b.geo()) return;
    InPart ip(b, IP_SHELL);
    const InteriorDef& d = *b.d;
    const InteriorRoom& r = d.rooms[roomIdx];
    float H = r.mx.z - r.mn.z;
    // floor
    if (!st.noFloor) {
        if (st.checker) checkerFloor(b, r.mn.x, r.mn.y, r.mx.x, r.mx.y, r.mn.z, st.checkSize, st.checkA, st.checkB, st.floorMat);
        else {
            float k = st.floorUV;
            quad(b, vec3(r.mn.x, r.mn.y, r.mn.z), vec3(r.mx.x, r.mn.y, r.mn.z), vec3(r.mx.x, r.mx.y, r.mn.z), vec3(r.mn.x, r.mx.y, r.mn.z),
                 vec2(r.mn.x, r.mn.y) * k, vec2(r.mx.x, r.mn.y) * k, vec2(r.mx.x, r.mx.y) * k, vec2(r.mn.x, r.mx.y) * k, st.floorCol, st.floorMat);
        }
    }
    // ceiling (faces down)
    if (!st.noCeiling) {
        float k = st.ceilUV;
        quad(b, vec3(r.mn.x, r.mx.y, r.mx.z), vec3(r.mx.x, r.mx.y, r.mx.z), vec3(r.mx.x, r.mn.y, r.mx.z), vec3(r.mn.x, r.mn.y, r.mx.z),
             vec2(r.mn.x, r.mx.y) * k, vec2(r.mx.x, r.mx.y) * k, vec2(r.mx.x, r.mn.y) * k, vec2(r.mn.x, r.mn.y) * k, st.ceilCol, st.ceilMat);
    }
    for (int side = 0; side < 4; side++) {
        if (!(st.walls & (1 << side))) continue;
        vec3 o, u, n;
        float w;
        faceFrame(r, side, o, u, n, w);
        std::vector<Hole> holes;
        struct Rev { Hole h; float depth; bool door; };
        std::vector<Rev> revs;
        for (const InteriorOpening& op : d.openings) {
            Hole h;
            float dep;
            if (openingOnFace(d, op, r, side, h, dep)) {
                holes.push_back(h);
                revs.push_back({h, dep, op.kind == OP_DOOR || op.kind == OP_ROLLUP || op.kind == OP_OPEN || h.z0 < 0.05f});
            }
        }
        for (const InteriorDoor& dr : d.doors) {
            if (dr.exterior) continue;
            Hole h;
            if (doorOnFace(dr, r, side, h)) holes.push_back(h);
        }
        for (const ExtraHole& eh : tExtraHoles)
            if (eh.room == roomIdx && eh.side == side) holes.push_back(eh.h);
        // lower wall panel (wainscot / tiles) + upper paint
        float uOff = dot(o, u);
        if (st.wainscotH > 0.f) {
            std::vector<Hole> lower = holes, upper = holes;
            for (Hole& h : upper) { h.z0 -= st.wainscotH; h.z1 -= st.wainscotH; }
            wallWithHoles(b, o, u, n, w, st.wainscotH, lower, st.wainCol, st.wainMat, uOff);
            wallWithHoles(b, o + vec3(0, 0, st.wainscotH), u, n, w, H - st.wainscotH, upper, st.wallCol, st.wallMat, uOff);
        } else {
            wallWithHoles(b, o, u, n, w, H, holes, st.wallCol, st.wallMat, uOff);
        }
        // reveals of facade openings (from the facade plane to this face)
        for (const Rev& rv : revs) {
            if (rv.depth < 0.02f) continue;
            reveal(b, o, u, -n, rv.h.s0, rv.h.s1, rv.h.z0, rv.h.z1, 0.f, rv.depth, st.revealCol, st.revealMat, !rv.door);
        }
        // trims along the wall, split around holes reaching them
        auto run = [&](float z0, float z1, float proud, u32 col, u32 mat) {
            std::vector<std::pair<float, float>> segs = {{0.f, w}};
            for (const Hole& h : holes) {
                if (h.z0 > z1 || h.z1 < z0) continue;
                std::vector<std::pair<float, float>> next;
                for (auto& s : segs) {
                    if (h.s1 <= s.first || h.s0 >= s.second) { next.push_back(s); continue; }
                    if (h.s0 > s.first) next.push_back({s.first, h.s0});
                    if (h.s1 < s.second) next.push_back({h.s1, s.second});
                }
                segs = next;
            }
            for (auto& s : segs) {
                if (s.second - s.first < 0.02f) continue;
                vec3 a = o + u * s.first + n * (proud * 0.5f) + vec3(0, 0, (z0 + z1) * 0.5f);
                vec3 c2 = a + u * (s.second - s.first);
                vec3 ctr = (a + c2) * 0.5f;
                float yaw = atan2f(u.y, u.x);
                At at(b, ctr, yaw);
                // local x = u, local y = perp(u) = -n: the face against the wall is +y
                box(b, vec3(0.f), vec3((s.second - s.first) * 0.5f, proud * 0.5f, (z1 - z0) * 0.5f), col, mat, SK_PY | SK_NZ);
            }
        };
        if (st.baseH > 0.f) run(0.f, st.baseH, 0.018f, st.baseCol, st.baseMat);
        if (st.chairRail > 0.f) run(st.chairRail - 0.035f, st.chairRail + 0.035f, 0.025f, st.baseCol, st.baseMat);
        if (st.wainscotH > 0.f && st.wainMat != st.wallMat) run(st.wainscotH - 0.02f, st.wainscotH + 0.02f, 0.02f, st.baseCol, st.baseMat);
        if (st.crownH > 0.f) {
            run(H - st.crownH, H, 0.05f, st.crownCol, M(MAT_PLASTER));
            run(H - st.crownH * 0.45f, H, 0.09f, st.crownCol, M(MAT_PLASTER));
        }
    }
}

// Glass pane filling an opening (outer face, slightly inside the facade plane) and a slim frame on the inside
void glazeOpening(IB& b, const InteriorOpening& op, float clarity, u32 frameCol, u32 frameMat, bool mullions = false) {
    if (!b.geo()) return;
    const InteriorDef& d = *b.d;
    vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
    vec3 u = normalize(vec3(c.x - a.x, c.y - a.y, 0.f));
    vec3 n(u.y, -u.x, 0.f);   // outward (right of the wall direction for CCW footprints)
    float w = length(c.xy() - a.xy()), h = op.z1 - op.z0;
    vec3 base = a - n * 0.035f;
    {
        InPart ip(b, IP_SHELL);
        quad(b, base, base + u * w, base + u * w + vec3(0, 0, h), base + vec3(0, 0, h), vec2(0, 0), vec2(w, 0), vec2(w, h), vec2(0, h), glassCol(clarity), kGlassMat);
        // inner frame
        float fw = 0.045f;
        float yaw = atan2f(u.y, u.x);
        At at(b, base - n * 0.03f, yaw);   // local x = along the wall, local y = -n (inward)... inward = -n
        box(b, vec3(w * 0.5f, 0.f, fw * 0.5f), vec3(w * 0.5f, 0.03f, fw * 0.5f), frameCol, frameMat, SK_NONE);
        box(b, vec3(w * 0.5f, 0.f, h - fw * 0.5f), vec3(w * 0.5f, 0.03f, fw * 0.5f), frameCol, frameMat, SK_NONE);
        box(b, vec3(fw * 0.5f, 0.f, h * 0.5f), vec3(fw * 0.5f, 0.03f, h * 0.5f - fw), frameCol, frameMat, SK_PZ | SK_NZ);
        box(b, vec3(w - fw * 0.5f, 0.f, h * 0.5f), vec3(fw * 0.5f, 0.03f, h * 0.5f - fw), frameCol, frameMat, SK_PZ | SK_NZ);
        if (mullions) {
            int nm = Max(1, (int)floorf(w / 1.4f));
            for (int k = 1; k < nm; k++) {
                float s = w * k / nm;
                box(b, vec3(s, 0.f, h * 0.5f), vec3(0.025f, 0.03f, h * 0.5f - fw), frameCol, frameMat, SK_PZ | SK_NZ);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ doors
// Declares a door (plan) and builds its frame and leaves (build). Returns the door index.
int door(IB& b, vec3 c, vec2 t, vec2 n, float w, float h, u8 kind, u8 style, u32 color, bool exterior, float depth = 0.f) {
    int idx = b.doorCounter++;
    if (b.plan()) {
        InteriorDoor dr;
        dr.c = b.f.P(c);
        dr.t = normalize(b.f.D(vec3(t, 0.f)).xy());
        dr.n = normalize(b.f.D(vec3(n, 0.f)).xy());
        dr.w = w;
        dr.h = h;
        dr.depth = depth;
        dr.kind = kind;
        dr.style = style;
        dr.exterior = exterior;
        dr.color = color;
        b.d->doors.push_back(dr);
    }
    return idx;
}

// One leaf mesh in its own frame: x along the leaf (0..w), y = thickness axis (door normal), z up
void doorLeafGeo(IB& b, float w, float h, float th, u8 style, u32 color, bool handleRight) {
    u32 wood = M(MAT_WOOD), steel = M(MAT_METAL_BRUSHED), paint = M(MAT_METAL_PAINTED);
    u32 chrome = M(MAT_CHROME);
    switch (style) {
        case 1: {  // aluminium shopfront glass leaf
            u32 frameC = Gy(0.72f);
            float fw = 0.07f;
            box(b, vec3(w * 0.5f, 0.f, 0.06f), vec3(w * 0.5f, th * 0.5f, 0.06f), frameC, steel, SK_NONE);
            box(b, vec3(w * 0.5f, 0.f, h - fw * 0.5f), vec3(w * 0.5f, th * 0.5f, fw * 0.5f), frameC, steel, SK_NONE);
            box(b, vec3(fw * 0.5f, 0.f, h * 0.5f), vec3(fw * 0.5f, th * 0.5f, h * 0.5f - 0.07f), frameC, steel, SK_PZ | SK_NZ);
            box(b, vec3(w - fw * 0.5f, 0.f, h * 0.5f), vec3(fw * 0.5f, th * 0.5f, h * 0.5f - 0.07f), frameC, steel, SK_PZ | SK_NZ);
            for (int s = -1; s <= 1; s += 2) {
                float y = s * 0.004f;
                quadF(b, vec3(fw, y, 0.12f), vec3(w - fw, y, 0.12f), vec3(w - fw, y, h - fw), vec3(fw, y, h - fw), vec3(0, (float)s, 0), glassCol(0.95f), kGlassMat);
            }
            // push bar and handle (both faces)
            float hx = handleRight ? w - 0.16f : 0.16f;
            for (int s = -1; s <= 1; s += 2) {
                tube(b, vec3(hx, s * (th * 0.5f + 0.05f), 0.85f), vec3(hx, s * (th * 0.5f + 0.05f), 1.35f), 0.016f, 8, Gy(0.85f), chrome, true);
                tube(b, vec3(hx, s * th * 0.5f, 0.9f), vec3(hx, s * (th * 0.5f + 0.05f), 0.9f), 0.01f, 6, Gy(0.85f), chrome);
                tube(b, vec3(hx, s * th * 0.5f, 1.3f), vec3(hx, s * (th * 0.5f + 0.05f), 1.3f), 0.01f, 6, Gy(0.85f), chrome);
            }
            break;
        }
        case 2: {  // steel service door with a vision panel
            rbox(b, vec3(w * 0.5f, 0.f, h * 0.5f), vec3(w * 0.5f, th * 0.5f, h * 0.5f), 0.006f, color, paint, true);
            for (int s = -1; s <= 1; s += 2) {
                box(b, vec3(w * 0.5f, s * (th * 0.5f + 0.004f), 1.55f), vec3(w * 0.16f, 0.004f, 0.21f), Gy(0.3f), steel, SK_NONE);
                box(b, vec3(w * 0.5f, s * (th * 0.5f + 0.008f), 1.55f), vec3(w * 0.13f, 0.002f, 0.18f), C(0.2f, 0.25f, 0.28f), M(MAT_GLASS), SK_NONE);
                box(b, vec3(handleRight ? w - 0.12f : 0.12f, s * (th * 0.5f + 0.03f), 1.02f), vec3(0.07f, 0.012f, 0.012f), Gy(0.75f), chrome, SK_NONE);
                box(b, vec3(handleRight ? w - 0.07f : 0.07f, s * (th * 0.5f + 0.016f), 1.02f), vec3(0.018f, 0.016f, 0.018f), Gy(0.75f), chrome, SK_NONE);
            }
            // kick plate
            for (int s = -1; s <= 1; s += 2) box(b, vec3(w * 0.5f, s * (th * 0.5f + 0.002f), 0.15f), vec3(w * 0.48f, 0.002f, 0.14f), Gy(0.7f), steel, SK_NONE);
            break;
        }
        case 4: {  // saloon / cafe swing leaf (louvred half door)
            float z0 = 0.35f, z1 = 1.55f;
            box(b, vec3(w * 0.5f, 0.f, z0 + 0.04f), vec3(w * 0.5f, th * 0.5f, 0.04f), color, wood, SK_NONE);
            box(b, vec3(w * 0.5f, 0.f, z1 - 0.04f), vec3(w * 0.5f, th * 0.5f, 0.04f), color, wood, SK_NONE);
            box(b, vec3(0.03f, 0.f, (z0 + z1) * 0.5f), vec3(0.03f, th * 0.5f, (z1 - z0) * 0.5f - 0.08f), color, wood, SK_PZ | SK_NZ);
            box(b, vec3(w - 0.03f, 0.f, (z0 + z1) * 0.5f), vec3(0.03f, th * 0.5f, (z1 - z0) * 0.5f - 0.08f), color, wood, SK_PZ | SK_NZ);
            for (float z = z0 + 0.12f; z < z1 - 0.1f; z += 0.07f) {
                At at(b, vec3(w * 0.5f, 0.f, z), 0.f);
                b.pushAxes(vec3(0.f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.8f)), normalize(vec3(0, -0.8f, 1)));
                box(b, vec3(0.f), vec3(w * 0.5f - 0.06f, th * 0.45f, 0.006f), color, wood, SK_NONE);
                b.pop();
            }
            break;
        }
        case 5: {  // elevator car / landing door leaf (brushed steel)
            rbox(b, vec3(w * 0.5f, 0.f, h * 0.5f), vec3(w * 0.5f, th * 0.5f, h * 0.5f), 0.004f, Gy(0.8f), steel, true);
            break;
        }
        default: {  // wood panel door (style 0) / wood with glass lite (style 3)
            u32 frameC = color;
            float rail = 0.11f;
            box(b, vec3(w * 0.5f, 0.f, h * 0.5f), vec3(w * 0.5f, th * 0.5f * 0.55f, h * 0.5f), color, wood, SK_NONE);   // core
            // stiles and rails proud of the panels on both faces
            for (int s = -1; s <= 1; s += 2) {
                float y = s * th * 0.36f;
                box(b, vec3(rail * 0.5f, y, h * 0.5f), vec3(rail * 0.5f, th * 0.14f, h * 0.5f), frameC, wood, SK_NZ);
                box(b, vec3(w - rail * 0.5f, y, h * 0.5f), vec3(rail * 0.5f, th * 0.14f, h * 0.5f), frameC, wood, SK_NZ);
                box(b, vec3(w * 0.5f, y, h - rail * 0.5f), vec3(w * 0.5f - rail, th * 0.14f, rail * 0.5f), frameC, wood, SK_NONE);
                box(b, vec3(w * 0.5f, y, 0.16f), vec3(w * 0.5f - rail, th * 0.14f, 0.16f), frameC, wood, SK_NONE);
                box(b, vec3(w * 0.5f, y, h * 0.45f), vec3(w * 0.5f - rail, th * 0.14f, 0.05f), frameC, wood, SK_NONE);
                box(b, vec3(w * 0.5f, y, h * 0.45f), vec3(0.04f, th * 0.14f, h * 0.45f - 0.3f), frameC, wood, SK_NONE);
                if (style == 3) {
                    // glass lite in the upper half
                    float gy = s * (th * 0.1f);
                    quadF(b, vec3(rail, gy, h * 0.52f), vec3(w - rail, gy, h * 0.52f), vec3(w - rail, gy, h - rail), vec3(rail, gy, h - rail), vec3(0, (float)s, 0),
                          glassCol(0.7f, vec3(0.8f, 0.85f, 0.8f)), kGlassMat);
                }
                // lever handle + rose
                float hx = handleRight ? w - 0.075f : 0.075f;
                float dir = handleRight ? -1.f : 1.f;
                b.pushAxes(vec3(hx, s * th * 0.5f, 1.0f), vec3(1, 0, 0), vec3(0, 0, -(float)s), vec3(0, (float)s, 0));
                cyl(b, vec3(0.f), 0.028f, 0.028f, 0.012f, 10, C(0.78f, 0.68f, 0.42f), chrome, true);
                cyl(b, vec3(0.f), 0.009f, 0.009f, 0.055f, 6, C(0.78f, 0.68f, 0.42f), chrome, false);
                b.pop();
                tube(b, vec3(hx, s * (th * 0.5f + 0.055f), 1.0f), vec3(hx + dir * 0.11f, s * (th * 0.5f + 0.055f), 1.0f), 0.009f, 6, C(0.78f, 0.68f, 0.42f), chrome, true);
            }
            break;
        }
    }
}

// Casing (trim) around a doorway on one face: face plane at c + n*off, facing n
void casing(IB& b, vec3 c, vec2 t, vec2 n, float w, float h, float off, u32 col, u32 mat) {
    float yaw = atan2f(t.y, t.x);
    At at(b, c + vec3(n, 0.f) * off, yaw);
    // local x = t, local y = n? rotated frame: y = perp(t); casing proud along perp(t) direction sign
    float sy = dot(perp(t), n) >= 0.f ? 1.f : -1.f;
    float cw = 0.07f, cp = 0.018f;
    box(b, vec3(-w * 0.5f - cw * 0.5f, sy * cp * 0.5f, (h + cw) * 0.5f), vec3(cw * 0.5f, cp * 0.5f, (h + cw) * 0.5f), col, mat, SK_NZ);
    box(b, vec3(w * 0.5f + cw * 0.5f, sy * cp * 0.5f, (h + cw) * 0.5f), vec3(cw * 0.5f, cp * 0.5f, (h + cw) * 0.5f), col, mat, SK_NZ);
    box(b, vec3(0.f, sy * cp * 0.5f, h + cw * 0.5f), vec3(w * 0.5f + cw, cp * 0.5f, cw * 0.5f), col, mat, SK_NONE);
}

// Interior doorway between two rooms: jamb lining through the partition (thickness th) + casings on both faces
void doorFrameInner(IB& b, vec3 c, vec2 t, vec2 n, float w, float h, float th, u32 trimCol) {
    if (!b.geo()) return;
    InPart ip(b, IP_SHELL);
    vec3 u(t, 0.f), nn(n, 0.f);
    vec3 o = c - u * (w * 0.5f);
    reveal(b, o, u, nn, 0.f, w, 0.f, h, -th * 0.5f, th * 0.5f, trimCol, M(MAT_WOOD), true);   // sill = threshold
    casing(b, c, t, n, w, h, th * 0.5f, trimCol, M(MAT_PAINT_WHITE));
    casing(b, c, t, -n, w, h, th * 0.5f, trimCol, M(MAT_PAINT_WHITE));
}

// ------------------------------------------------------------------------------------------------ lights & fixtures
// Recessed ceiling downlight (can) with its light
void downlight(IB& b, vec3 p, int room, float cd = 280.f, vec3 tint = vec3(1.f, 0.86f, 0.68f), float radius = 6.5f) {
    {
        InPart ip(b, IP_SHELL);
        cyl(b, p - vec3(0, 0, 0.012f), 0.085f, 0.085f, 0.012f, 12, Gy(0.95f), M(MAT_PAINT_WHITE), false, true);
        b.pushAxes(p - vec3(0, 0, 0.0135f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
        disc(b, vec3(0.f), 0.06f, 12, C(tint, 0.9f), EM());
        b.pop();
    }
    light(b, p - vec3(0, 0, 0.1f), tint * cd, radius, room, vec3(0, 0, -1), 68.f, 30.f);
}

// Fluorescent / LED troffer panel in a drop ceiling
void troffer(IB& b, vec3 p, float lx, float ly, int room, float cd = 520.f, vec3 tint = vec3(0.95f, 0.98f, 1.f), float radius = 8.f, bool lightOn = true) {
    {
        InPart ip(b, IP_SHELL);
        box(b, p - vec3(0, 0, 0.01f), vec3(lx * 0.5f + 0.03f, ly * 0.5f + 0.03f, 0.012f), Gy(0.92f), M(MAT_METAL_PAINTED), SK_PZ);
        b.pushAxes(p - vec3(0, 0, 0.023f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
        quad(b, vec3(-lx * 0.5f, -ly * 0.5f, 0.f), vec3(lx * 0.5f, -ly * 0.5f, 0.f), vec3(lx * 0.5f, ly * 0.5f, 0.f), vec3(-lx * 0.5f, ly * 0.5f, 0.f), vec2(0, 0),
             vec2(1, 0), vec2(1, 1), vec2(0, 1), C(tint, 0.95f), EM());
        // louvre grid lines
        for (int k = 1; k < 4; k++) {
            float x = -lx * 0.5f + lx * k / 4.f;
            box(b, vec3(x, 0.f, 0.004f), vec3(0.006f, ly * 0.5f, 0.004f), Gy(0.85f), M(MAT_METAL_BRUSHED), SK_NONE);
        }
        b.pop();
    }
    if (lightOn) light(b, p - vec3(0, 0, 0.15f), tint * cd, radius, room, vec3(0, 0, -1), 80.f, 45.f);
}

// Pendant lamp with a shade (dome), cord from the ceiling
void pendant(IB& b, vec3 top, float drop, int room, u32 shadeCol, float cd = 220.f, vec3 tint = vec3(1.f, 0.8f, 0.55f), float radius = 5.5f, int style = 0) {
    vec3 p = top - vec3(0, 0, drop);
    {
        InPart ip(b, IP_FURNITURE);
        cyl(b, top - vec3(0, 0, 0.03f), 0.05f, 0.05f, 0.03f, 10, Gy(0.2f), M(MAT_METAL_PAINTED), false, true);
        tube(b, top, p + vec3(0, 0, 0.12f), 0.004f, 4, Gy(0.1f), M(MAT_RUBBER));
        if (style == 0) {
            // dome shade
            lathe(b, p, {vec2(0.2f, 0.0f), vec2(0.19f, 0.05f), vec2(0.15f, 0.1f), vec2(0.07f, 0.14f), vec2(0.025f, 0.16f)}, 16, shadeCol, M(MAT_METAL_PAINTED), true);
        } else if (style == 1) {
            // glass globe (diner / schoolhouse)
            sphere(b, p + vec3(0, 0, 0.02f), 0.12f, 12, C(1.f, 0.9f, 0.75f, 0.35f), EM());   // frosted glass glows softly
            cyl(b, p + vec3(0, 0, 0.12f), 0.05f, 0.04f, 0.05f, 10, shadeCol, M(MAT_CHROME));
        } else {
            // drum fabric shade
            cyl(b, p, 0.22f, 0.22f, 0.22f, 18, shadeCol, M(MAT_FABRIC), false, false);
        }
        // bulb
        sphere(b, p + vec3(0, 0, 0.03f), 0.035f, 8, C(tint, 1.f), EM());
    }
    light(b, p, tint * cd, radius, room);
}

// ------------------------------------------------------------------------------------------------ small props
// Soda can / food can
// (7-sided body with smooth normals; top = false for a can hidden under another one)
void can(IB& b, vec3 p, float r, float h, u32 col, u32 lid = 0, bool top = true) {
    cyl(b, p, r, r, h, 7, col, M(MAT_METAL_PAINTED), false);
    if (top) disc(b, p + vec3(0, 0, h), r * 0.97f, 7, lid ? lid : Gy(0.75f), M(MAT_METAL_BRUSHED));
}
// Bottle (glass or plastic): body color with cap
void bottle(IB& b, vec3 p, float r, float h, u32 col, u32 mat, u32 cap, int seg = 6) {
    lathe(b, p, {vec2(r * 0.94f, 0.f), vec2(r, h * 0.6f), vec2(r * 0.4f, h * 0.84f), vec2(r * 0.34f, h)}, seg, col, mat, false);
    cyl(b, p + vec3(0, 0, h), r * 0.38f, r * 0.38f, h * 0.06f, seg, cap, M(MAT_METAL_PAINTED));
}
// Product box (cereal / snacks) with a front label band
void productBox(IB& b, vec3 p, vec3 he, u32 col, u32 band) {
    box(b, p + vec3(0, 0, he.z), he, col, M(MAT_METAL_PAINTED), SK_NZ | SK_NY);
    float y = he.y + 0.001f, bx = he.x * 0.8f, z0 = he.z * 0.75f, z1 = he.z * 1.45f;
    quadF(b, p + vec3(-bx, y, z0), p + vec3(bx, y, z0), p + vec3(bx, y, z1), p + vec3(-bx, y, z1), vec3(0, 1, 0), band, M(MAT_PAINT_WHITE));
}

// Book row on a shelf: books of varied height / thickness along local x from x0 to x1, spines facing +y
void books(IB& b, float x0, float x1, float z, float depth, u32 seed) {
    Rng r(seed);
    float x = x0;
    while (x < x1 - 0.02f) {
        float t = r.range(0.018f, 0.045f), h = r.range(0.18f, 0.3f);
        if (x + t > x1) break;
        vec3 c = hsv(r.f(), r.range(0.3f, 0.75f), r.range(0.25f, 0.8f));
        bool lean = r.chance(0.06f) && x + h < x1;
        if (lean) {
            b.pushAxes(vec3(x + h * 0.5f, 0.f, z), vec3(0, 0, -1), vec3(0, 1, 0), vec3(1, 0, 0));
            box(b, vec3(-t * 0.5f, 0.f, h * 0.5f), vec3(t * 0.5f, depth * 0.5f, h * 0.5f), C(c), M(MAT_FABRIC), SK_NONE);
            b.pop();
            x += h + 0.01f;
        } else {
            box(b, vec3(x + t * 0.5f, 0.f, z + h * 0.5f), vec3(t * 0.5f, depth * 0.5f, h * 0.5f), C(c), M(MAT_FABRIC), SK_NZ);
            // spine label band
            if (r.chance(0.6f)) box(b, vec3(x + t * 0.5f, depth * 0.5f + 0.001f, z + h * 0.7f), vec3(t * 0.4f, 0.001f, 0.012f), C(0.9f, 0.85f, 0.6f), M(MAT_PAINT_WHITE), SK_NZ);
            x += t + r.range(0.0f, 0.004f);
        }
        if (r.chance(0.05f)) x += r.range(0.05f, 0.15f);
    }
}

// Potted plant: pot + stems + leaf clusters
void plant(IB& b, vec3 p, float size, u32 seed, u32 potCol = 0, int kind = 0) {
    Rng r(seed);
    float potR = 0.14f * size + 0.06f, potH = 0.28f * size + 0.08f;
    u32 pc = potCol ? potCol : C(hsv(r.range(0.02f, 0.1f), 0.5f, 0.55f));
    lathe(b, p, {vec2(potR * 0.72f, 0.f), vec2(potR * 0.82f, 0.02f), vec2(potR, potH * 0.92f), vec2(potR * 1.06f, potH * 0.94f), vec2(potR * 1.06f, potH), vec2(potR * 0.95f, potH)}, 14, pc,
          M(MAT_METAL_PAINTED), false);
    disc(b, p + vec3(0, 0, potH * 0.9f), potR * 0.95f, 12, C(0.18f, 0.12f, 0.08f), M(MAT_DIRT));
    u32 leafMat = M(MAT_LEAVES);
    vec3 leafC = kind == 1 ? vec3(0.85f, 1.f, 0.66f) : vec3(0.74f, 0.95f, 0.62f);   // tint over the leaf texture
    int stems = kind == 1 ? 7 : 5 + (int)(size * 4.f);
    for (int i = 0; i < stems; i++) {
        float a = r.f() * kTwoPi, tilt = r.range(0.15f, 0.55f);
        float len = size * r.range(0.45f, 0.9f) + 0.2f;
        vec3 dir = normalize(vec3(cosf(a) * tilt, sinf(a) * tilt, 1.f));
        vec3 s0 = p + vec3(0, 0, potH * 0.9f), s1 = s0 + dir * len;
        tube(b, s0, s1, 0.008f, 4, C(0.25f, 0.35f, 0.15f), M(MAT_BARK));
        // leaves: flat blades around the stem tip (kind 1: palm fronds)
        int nl = kind == 1 ? 9 : 4;
        for (int k = 0; k < nl; k++) {
            float t = kind == 1 ? 1.f - k * 0.08f : r.range(0.55f, 1.f);
            vec3 at = s0 + dir * (len * t);
            float la = a + (k - nl * 0.5f) * 0.7f + r.range(-0.3f, 0.3f);
            vec3 ld = normalize(vec3(cosf(la), sinf(la), r.range(-0.2f, 0.5f)));
            float L = kind == 1 ? 0.28f * size : r.range(0.12f, 0.2f) * (0.6f + size * 0.5f);
            float W = kind == 1 ? 0.035f : L * 0.45f;
            vec3 side = normalize(cross(ld, vec3(0, 0, 1))) * W;
            vec3 tip = at + ld * L;
            vec3 mid = at + ld * (L * 0.5f) + vec3(0, 0, 0.02f);
            vec3 cc = leafC * r.range(0.82f, 1.08f);
            // double-sided diamond leaf
            tri(b, at, mid + side, tip, C(cc), leafMat);
            tri(b, at, tip, mid + side, C(cc), leafMat);
            tri(b, at, tip, mid - side, C(cc), leafMat);
            tri(b, at, mid - side, tip, C(cc), leafMat);
        }
    }
}

// Framed picture on a wall: center (local), facing +y; generated art from the seed
void picture(IB& b, vec3 c, float w, float h, u32 seed, u32 frameCol = 0) {
    Rng r(seed);
    u32 fc = frameCol ? frameCol : (r.chance(0.5f) ? C(0.12f, 0.09f, 0.06f) : (r.chance(0.5f) ? Gy(0.9f) : C(0.75f, 0.6f, 0.3f)));
    float fw = r.range(0.03f, 0.06f), fd = 0.03f;
    u32 fm = M(MAT_WOOD);
    box(b, c + vec3(0, fd * 0.5f, h * 0.5f - fw * 0.5f), vec3(w * 0.5f, fd * 0.5f, fw * 0.5f), fc, fm, SK_NY);
    box(b, c + vec3(0, fd * 0.5f, -h * 0.5f + fw * 0.5f), vec3(w * 0.5f, fd * 0.5f, fw * 0.5f), fc, fm, SK_NY);
    box(b, c + vec3(-w * 0.5f + fw * 0.5f, fd * 0.5f, 0.f), vec3(fw * 0.5f, fd * 0.5f, h * 0.5f - fw), fc, fm, SK_NY | SK_PZ | SK_NZ);
    box(b, c + vec3(w * 0.5f - fw * 0.5f, fd * 0.5f, 0.f), vec3(fw * 0.5f, fd * 0.5f, h * 0.5f - fw), fc, fm, SK_NY | SK_PZ | SK_NZ);
    float iw = w * 0.5f - fw, ih = h * 0.5f - fw;
    float y = 0.012f;
    u32 pm = M(MAT_PAINT_WHITE);
    auto rect = [&](float x0, float z0, float x1, float z1, u32 col, float dy) {
        x0 = Clamp(x0, -iw, iw);
        x1 = Clamp(x1, -iw, iw);
        z0 = Clamp(z0, -ih, ih);
        z1 = Clamp(z1, -ih, ih);
        if (x1 - x0 < 1e-3f || z1 - z0 < 1e-3f) return;
        quad(b, c + vec3(x1, y + dy, z0), c + vec3(x0, y + dy, z0), c + vec3(x0, y + dy, z1), c + vec3(x1, y + dy, z1), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), col, pm);
    };
    auto circ = [&](float cx, float cz, float rad, u32 col, float dy) {
        if (!b.geo()) return;
        b.pushAxes(c + vec3(cx, y + dy, cz), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
        disc(b, vec3(0.f), rad, 16, col, pm);
        b.pop();
    };
    // mat board
    bool mat = r.chance(0.4f);
    rect(-iw, -ih, iw, ih, mat ? Gy(0.92f) : Gy(0.1f), 0.f);
    if (mat) { iw -= 0.05f; ih -= 0.05f; }
    int style = (int)(seed % 6u);
    float hue = r.f();
    switch (style) {
        case 0: {  // tropical sunset with palm silhouette
            for (int k = 0; k < 6; k++) {
                float t0 = (float)k / 6.f, t1 = (float)(k + 1) / 6.f;
                vec3 col = lerp(vec3(1.f, 0.55f, 0.2f), vec3(0.45f, 0.15f, 0.45f), t0);
                rect(-iw, -ih * 0.2f + t0 * ih * 1.2f, iw, -ih * 0.2f + t1 * ih * 1.2f, C(col), 0.001f);
            }
            rect(-iw, -ih, iw, -ih * 0.2f, C(0.15f, 0.3f, 0.45f), 0.001f);
            circ(iw * 0.2f, -ih * 0.05f, Min(iw, ih) * 0.28f, C(1.f, 0.85f, 0.4f), 0.002f);
            rect(-iw, -ih * 0.22f, iw, -ih * 0.18f, C(1.f, 0.7f, 0.35f), 0.003f);
            // palm trunk + fronds
            rect(-iw * 0.55f, -ih, -iw * 0.5f, ih * 0.45f, Gy(0.05f), 0.004f);
            for (int k = 0; k < 6; k++) {
                float a = k * 1.05f + 0.3f;
                float px = -iw * 0.525f, pz = ih * 0.45f;
                float ex = px + cosf(a) * iw * 0.35f, ez = pz + sinf(a) * ih * 0.25f;
                rect(Min(px, ex), Min(pz, ez) - 0.004f, Max(px, ex), Min(pz, ez) + 0.008f, Gy(0.05f), 0.005f);
            }
            break;
        }
        case 1: {  // color field abstract
            for (int k = 0; k < 3; k++) {
                float z0 = -ih + k * ih * 2.f / 3.f + 0.02f, z1 = z0 + ih * 2.f / 3.f - 0.04f;
                rect(-iw + 0.03f, z0, iw - 0.03f, z1, C(hsv(hue + k * 0.12f, 0.6f, 0.75f)), 0.001f);
            }
            break;
        }
        case 2: {  // geometric circles
            rect(-iw, -ih, iw, ih, C(hsv(hue, 0.15f, 0.9f)), 0.001f);
            for (int k = 0; k < 5; k++) circ(r.range(-iw * 0.6f, iw * 0.6f), r.range(-ih * 0.6f, ih * 0.6f), r.range(0.04f, Min(iw, ih) * 0.45f), C(hsv(hue + r.f() * 0.5f, 0.7f, 0.8f)), 0.002f + k * 0.0005f);
            break;
        }
        case 3: {  // beach scene: sky, sea, sand, umbrella
            rect(-iw, ih * 0.1f, iw, ih, C(0.45f, 0.7f, 0.95f), 0.001f);
            rect(-iw, -ih * 0.3f, iw, ih * 0.1f, C(0.1f, 0.5f, 0.65f), 0.001f);
            rect(-iw, -ih, iw, -ih * 0.3f, C(0.9f, 0.8f, 0.6f), 0.001f);
            circ(iw * 0.3f, -ih * 0.45f, iw * 0.2f, C(0.95f, 0.3f, 0.3f), 0.002f);
            rect(iw * 0.29f, -ih * 0.95f, iw * 0.31f, -ih * 0.45f, Gy(0.95f), 0.003f);
            break;
        }
        case 4: {  // stripes
            int n = 5 + (int)(r.f() * 6.f);
            for (int k = 0; k < n; k++) {
                float x0 = -iw + 2.f * iw * k / n, x1 = x0 + 2.f * iw / n;
                rect(x0, -ih, x1, ih, C(hsv(hue + (k & 1) * 0.5f, 0.5f + 0.3f * (k % 3 == 0), 0.85f - 0.2f * (k & 1))), 0.001f);
            }
            break;
        }
        default: {  // city skyline at night
            rect(-iw, -ih, iw, ih, C(0.08f, 0.06f, 0.2f), 0.001f);
            circ(iw * 0.5f, ih * 0.55f, Min(iw, ih) * 0.12f, C(0.95f, 0.95f, 0.85f), 0.002f);
            float x = -iw;
            while (x < iw) {
                float bw = r.range(0.04f, 0.12f), bh = r.range(0.2f, 1.4f) * ih;
                rect(x, -ih, x + bw, -ih + bh, Gy(0.03f), 0.002f);
                for (float wz = -ih + 0.03f; wz < -ih + bh - 0.03f; wz += 0.035f)
                    if (r.chance(0.35f)) rect(x + bw * 0.3f, wz, x + bw * 0.45f, wz + 0.015f, C(1.f, 0.8f, 0.4f), 0.003f);
                x += bw + 0.005f;
            }
            break;
        }
    }
}

// Rug on the floor (with border band and pattern)
// Small fixtures that make a room read as real: outlets along the walls, switch plates beside doorways, a smoke
// detector and a return-air grille on the ceiling; commercial rooms add sprinkler heads and lit EXIT signs over the
// exterior doors. Openings and doorways are respected.
void roomDressing(IB& b, int roomIdx, bool commercial, u32 seed) {
    if (!b.geo()) return;
    const InteriorDef& d = *b.d;
    const InteriorRoom& r = d.rooms[roomIdx];
    Rng rr(seed);
    InPart ip(b, IP_DETAIL);
    const float H = r.mx.z - r.mn.z;
    u32 plate = Gy(0.93f), pm = M(MAT_PLASTIC);
    for (int side = 0; side < 4; side++) {
        vec3 o, u, n;
        float w;
        faceFrame(r, side, o, u, n, w);
        std::vector<Hole> holes;
        for (const InteriorOpening& op : d.openings) {
            Hole h;
            float dep;
            if (openingOnFace(d, op, r, side, h, dep)) holes.push_back(h);
        }
        std::vector<Hole> doorHoles;
        for (const InteriorDoor& dr : d.doors) {
            Hole h;
            if (doorOnFace(dr, r, side, h)) doorHoles.push_back(h);
            if (dr.exterior) {
                // exterior doorways sit behind the facade wall: match them by plane distance
                vec3 dc = dr.c;
                float dist = dot(dc - o, n);
                if (dist < -0.4f || dist > 0.05f) continue;
                vec3 t3(dr.t, 0.f);
                if (fabsf(dot(t3, u)) < 0.99f) continue;
                float sc = dot(dc - o, u);
                if (sc < -dr.w || sc > w + dr.w) continue;
                Hole eh{sc - dr.w * 0.5f, sc + dr.w * 0.5f, dr.c.z - r.mn.z, dr.c.z - r.mn.z + dr.h};
                doorHoles.push_back(eh);
                if (commercial && eh.z1 + 0.45f < H) {
                    // EXIT sign over the door, lettering toward the room
                    vec3 sp = o + u * ((eh.s0 + eh.s1) * 0.5f) + n * 0.09f + vec3(0, 0, eh.z1 + 0.28f);
                    float yaw = atan2f(n.y, n.x) - kHalfPi;   // local +y = n
                    At at(b, sp, yaw);
                    box(b, vec3(0.f), vec3(0.18f, 0.04f, 0.09f), Gy(0.95f), pm, SK_NONE);
                    box(b, vec3(0, 0.041f, 0.f), vec3(0.16f, 0.001f, 0.07f), C(0.1f, 0.8f, 0.3f, 0.9f), EM(), SK_NZ);
                    textC(b, "EXIT", vec3(0, 0.043f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.07f, 0.012f, Gy(1.f), M(MAT_PAINT_WHITE));
                }
            }
        }
        for (const Hole& h : doorHoles) holes.push_back(h);
        auto clear = [&](float s, float z0, float z1, float m) {
            for (const Hole& h : holes)
                if (s > h.s0 - m && s < h.s1 + m && z1 > h.z0 - 0.05f && z0 < h.z1 + 0.05f) return false;
            return true;
        };
        // outlets along the wall
        for (float s = 0.9f + rr.range(0.f, 0.6f); s < w - 0.4f; s += rr.range(2.6f, 3.8f)) {
            if (!clear(s, 0.2f, 0.45f, 0.35f)) continue;
            vec3 c = o + u * s + n * 0.004f + vec3(0, 0, 0.32f);
            float yaw = atan2f(n.y, n.x) - kHalfPi;
            At at(b, c, yaw);
            box(b, vec3(0.f), vec3(0.036f, 0.004f, 0.058f), plate, pm, SK_NY);
            for (int k = -1; k <= 1; k += 2) box(b, vec3(0.f, 0.0045f, k * 0.022f), vec3(0.012f, 0.0006f, 0.01f), Gy(0.25f), pm, SK_NY);
        }
        // switch plates on the latch side of every doorway
        for (const Hole& h : doorHoles) {
            float s = h.s1 + 0.16f;
            if (s > w - 0.1f || !clear(s, 1.0f, 1.3f, 0.02f)) s = h.s0 - 0.16f;
            if (s < 0.1f) continue;
            vec3 c = o + u * s + n * 0.004f + vec3(0, 0, 1.15f);
            float yaw = atan2f(n.y, n.x) - kHalfPi;
            At at(b, c, yaw);
            box(b, vec3(0.f), vec3(0.04f, 0.004f, 0.062f), plate, pm, SK_NY);
            box(b, vec3(0.f, 0.006f, 0.f), vec3(0.008f, 0.004f, 0.018f), Gy(0.97f), pm, SK_NY);
        }
    }
    // ceiling: smoke detector, return grille, sprinklers
    vec3 cc((r.mn.x + r.mx.x) * 0.5f, (r.mn.y + r.mx.y) * 0.5f, r.mx.z);
    b.pushAxes(cc + vec3(0.35f, 0.25f, -0.001f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
    cyl(b, vec3(0.f), 0.065f, 0.058f, 0.035f, 12, Gy(0.95f), pm, true, false);
    b.pop();
    {
        vec3 gc(Lerp(r.mn.x, r.mx.x, 0.25f), Lerp(r.mn.y, r.mx.y, 0.7f), r.mx.z - 0.004f);
        box(b, gc, vec3(0.3f, 0.3f, 0.004f), Gy(0.9f), M(MAT_METAL_PAINTED), SK_PZ);
        for (int k = -3; k <= 3; k++) box(b, gc + vec3(k * 0.075f, 0.f, -0.005f), vec3(0.008f, 0.27f, 0.003f), Gy(0.55f), M(MAT_METAL_PAINTED), SK_PZ);
    }
    if (commercial) {
        for (float x = r.mn.x + 1.5f; x < r.mx.x - 0.5f; x += 3.f)
            for (float y = r.mn.y + 1.5f; y < r.mx.y - 0.5f; y += 3.f) {
                b.pushAxes(vec3(x + 0.4f, y + 0.4f, r.mx.z), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
                lathe(b, vec3(0.f), {vec2(0.03f, 0.f), vec2(0.012f, 0.03f), vec2(0.012f, 0.05f), vec2(0.022f, 0.055f)}, 6, Gy(0.85f), M(MAT_CHROME), true);
                b.pop();
            }
    }
}

void rug(IB& b, vec3 c, float w, float dp, u32 seed) {
    Rng r(seed);
    float hue = r.f();
    vec3 base = hsv(hue, r.range(0.35f, 0.7f), r.range(0.35f, 0.65f));
    vec3 border = hsv(hue + 0.5f, 0.4f, 0.8f);
    u32 fab = M(MAT_CARPET);
    float z = c.z + 0.008f;
    box(b, vec3(c.x, c.y, c.z + 0.004f), vec3(w * 0.5f, dp * 0.5f, 0.004f), C(border), fab, SK_NZ);
    box(b, vec3(c.x, c.y, c.z + 0.0045f), vec3(w * 0.5f - 0.1f, dp * 0.5f - 0.1f, 0.004f), C(base), fab, SK_NZ);
    int style = (int)(seed % 3u);
    if (style == 0) {
        box(b, vec3(c.x, c.y, z), vec3(w * 0.22f, dp * 0.22f, 0.0012f), C(border * 0.9f), fab, SK_NZ);
    } else if (style == 1) {
        for (int k = -2; k <= 2; k++) box(b, vec3(c.x + k * w * 0.16f, c.y, z), vec3(0.035f, dp * 0.5f - 0.14f, 0.0012f), C(border), fab, SK_NZ);
    } else {
        for (int k = 0; k < 4; k++) box(b, vec3(c.x, c.y, z + k * 0.0003f), vec3(w * (0.4f - k * 0.08f), dp * (0.4f - k * 0.08f), 0.0012f), C(k & 1 ? base * 1.3f : border), fab, SK_NZ);
    }
}

}  // namespace ikit
}  // namespace World
