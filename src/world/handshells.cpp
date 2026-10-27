// The hand-built world's buildings in their cells (docs/HANDBUILT_WORLD.md 7.2, the near shell). Each placed shell
// (buildingsbin.h) is drawn as:
// - its walls with their real window and door openings, the base and the foundation down to the ground;
// - the windows' glass, which shows the room behind it and is lit by that room's schedule (facade.hlsli, through a
//   hand-built window's facade entry: roomlight.h kFacadeHandWindow), with reveals, sills, glazing bars and shutters;
// - the doors' leaves, the trims of its look, and its roof (flat behind a parapet, gable, hip or shed);
// - its footprint as the cell's collision.
// The far LOD keeps the walls, the lit glass, the doors and the roof. Included from cellgen.cpp, as buildings.cpp
// includes blockstyle.cpp, so the tools that build cells have it too; handworld.cpp prepares the placements at load.
#include "buildingsbin.h"
#include "buildings.h"
#include "roomlight.h"
#include "../render/mesh.h"
#include "../core/rng.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace World {

namespace handshells_detail {

constexpr float kParapet = 0.6f;       // a flat roof's parapet, above the top floor
constexpr float kParapetT = 0.2f;      // its thickness
constexpr float kRecess = 0.1f;        // the glass and the door leaves sit this far behind the wall's face (near LOD)
constexpr float kEps = 0.01f;          // a window's glass keeps this far inside its facade cell
constexpr float kRoofT = 0.12f;        // a pitched roof's thickness at its edges (the fascia)
constexpr u32 kOpaque = 0xff000000u;

u32 rgb(float r, float g, float b) { return packRGBA8(r, g, b, 1.f); }
u32 vcol(u32 rgb8) { return (rgb8 & 0x00ffffffu) | kOpaque; }

// A placement's frame: plan coordinates (mirrored across the footprint's width first, then turned by the yaw) into
// the world, z above the ground floor (docs/NTW_FORMAT.md 4: mirror, yaw)
struct Frame {
    vec2 at = vec2(0.f);
    float c = 1.f, s = 0.f, w = 0.f, z = 0.f;
    bool mirror = false;
    vec2 point(vec2 l) const {
        const float x = mirror ? w - l.x : l.x;
        return at + vec2(x * c - l.y * s, x * s + l.y * c);
    }
    vec2 dir(vec2 l) const {
        const float x = mirror ? -l.x : l.x;
        return vec2(x * c - l.y * s, x * s + l.y * c);
    }
};
Frame frameOf(const HandPlacement& p, const HandShell& s) {
    Frame f;
    f.at = p.at;
    f.c = cosf(p.yaw);
    f.s = sinf(p.yaw);
    f.w = s.size.x;
    f.z = p.z;
    f.mirror = p.mirror;
    return f;
}

float signedArea(const std::vector<vec2>& p) {
    float a = 0.f;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += p[j].x * p[i].y - p[i].x * p[j].y;
    return a * 0.5f;
}
bool inside(const std::vector<vec2>& poly, vec2 q) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const vec2 a = poly[i], b = poly[j];
        if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}
// A simple polygon moved out (d > 0) or in by d along its edges' normals, mitred at the corners (counter-clockwise in,
// counter-clockwise out)
std::vector<vec2> offsetPoly(std::vector<vec2> p, float d) {
    if (signedArea(p) < 0.f) std::reverse(p.begin(), p.end());
    const size_t n = p.size();
    std::vector<vec2> out(n);
    for (size_t i = 0; i < n; i++) {
        const vec2 a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
        const vec2 e0 = normalize(b - a), e1 = normalize(c - b);
        const vec2 n0(e0.y, -e0.x), n1(e1.y, -e1.x);   // (outward: to the right of a counter-clockwise edge)
        vec2 m = n0 + n1;
        const float ml = length(m);
        m = ml > 1e-4f ? m * (1.f / ml) : n0;
        const float cosH = Max(dot(m, n0), 0.25f);   // (a miter four times the offset at most)
        out[i] = b + m * (d / cosH);
    }
    return out;
}
// The footprint as an axis-aligned rectangle in the plan's frame, where it is one (4 corners on its bounds)
bool planRect(const std::vector<vec2>& fp, vec2& lo, vec2& hi) {
    if (fp.size() != 4) return false;
    lo = hi = fp[0];
    for (vec2 p : fp) {
        lo = vec2(Min(lo.x, p.x), Min(lo.y, p.y));
        hi = vec2(Max(hi.x, p.x), Max(hi.y, p.y));
    }
    for (vec2 p : fp)
        if ((fabsf(p.x - lo.x) > 0.01f && fabsf(p.x - hi.x) > 0.01f) || (fabsf(p.y - lo.y) > 0.01f && fabsf(p.y - hi.y) > 0.01f)) return false;
    return hi.x - lo.x > 0.5f && hi.y - lo.y > 0.5f;
}

// The look and colours of one placement: its own palette slots, else its shell's, else the defaults
struct Paint {
    HandLook look;
    u32 slot[PAL_COUNT] = {};
    bool roofColour = false;
    static u32 slotOf(u8 s, u32 fallback) { return s < (u8)PAL_COUNT ? (u32)s : fallback; }
    u32 wall() const { return vcol(slot[slotOf(look.wallColor, PAL_WALL)]); }
    u32 frame() const { return vcol(slot[slotOf(look.frameColor, PAL_TRIM)]); }
    u32 door() const { return vcol(slot[slotOf(look.doorColor, PAL_DOOR)]); }
    u32 colour(u8 s) const { return vcol(slot[slotOf(s, PAL_TRIM)]); }
    u32 roof() const { return roofColour ? vcol(slot[PAL_ROOF]) : vcol(rgb(1.f, 1.f, 1.f)); }
};
Paint paintOf(const HandBuildings& hb, const HandPlacement& p, const HandShell& s) {
    Paint pt;
    const int li = p.look >= 0 ? p.look : s.look;
    if (li >= 0 && (size_t)li < hb.looks.size()) pt.look = hb.looks[(size_t)li];
    else {
        pt.look.wallMaterial = MAT_STUCCO;
        pt.look.frameMaterial = MAT_METAL_PAINTED;
        pt.look.baseMaterial = MAT_STUCCO;
    }
    const u32 kDefault[PAL_COUNT] = {rgb(0.93f, 0.9f, 0.84f), rgb(0.96f, 0.95f, 0.92f), rgb(0.38f, 0.25f, 0.16f), rgb(0.25f, 0.42f, 0.38f),
                                     rgb(1.f, 1.f, 1.f)};
    for (int k = 0; k < PAL_COUNT; k++) pt.slot[k] = p.palette[k] != kNone32 ? p.palette[k] : s.palette[k] != kNone32 ? s.palette[k] : kDefault[k];
    pt.roofColour = p.palette[PAL_ROOF] != kNone32 || s.palette[PAL_ROOF] != kNone32;
    return pt;
}

// The top of a shell's walls (above its ground floor), and the thickness of its top floor's walls
float shellTop(const HandShell& s, float* t) {
    float top = 0.f;
    for (const HandShell::Wall& w : s.walls) {
        const HandShell::Floor& f = s.floors[(size_t)w.floor];
        top = Max(top, f.z + f.h);
    }
    float tt = 0.f;
    for (const HandShell::Wall& w : s.walls) {
        const HandShell::Floor& f = s.floors[(size_t)w.floor];
        if (fabsf(f.z + f.h - top) < 0.01f) tt = Max(tt, w.t);
    }
    if (t) *t = tt > 0.f ? tt : 0.25f;
    return top;
}
float shellBottom(const HandShell& s) {
    float lo = 1e9f;
    for (const HandShell::Floor& f : s.floors) lo = Min(lo, f.z);
    return lo < 1e8f ? lo : 0.f;
}

// Mesh building with world-space inputs, cell-relative output
struct Out {
    MeshData* m = nullptr;
    vec3 org = vec3(0.f);
    // A quad seen from its front (counter-clockwise a, b, c, d), with uv per corner; its tangent along a -> b
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, u32 col, u32 mat) const {
        m->quad(a - org, b - org, c - org, d - org, ua, ub, uc, ud, col, mat);
    }
    // A quad turned to face `facing` whatever its corners' order
    void quadF(vec3 a, vec3 b, vec3 c, vec3 d, u32 col, u32 mat, vec3 facing, float uvScale = 1.f) const {
        const float lab = length(b - a), lad = length(d - a);
        m->quadFacing(a - org, b - org, c - org, d - org, vec2(0.f), vec2(lab * uvScale, 0.f), vec2(lab * uvScale, lad * uvScale), vec2(0.f, lad * uvScale), col, mat,
                      facing);
    }
    void tri(vec3 a, vec3 b, vec3 c, u32 col, u32 mat, vec3 facing, vec2 ua, vec2 ub, vec2 uc) const {
        vec3 n = cross(b - a, c - a);
        if (dot(n, facing) < 0.f) {
            std::swap(b, c);
            std::swap(ub, uc);
            n = -n;
        }
        const float ln = length(n);
        if (ln < 1e-8f) return;
        n = n * (1.f / ln);
        vec3 t = b - a;
        t = length2(t) > 1e-10f ? normalize(t) : vec3(1.f, 0.f, 0.f);
        const u32 i0 = m->addVertex(a - org, n, t, ua, col, mat), i1 = m->addVertex(b - org, n, t, ub, col, mat), i2 = m->addVertex(c - org, n, t, uc, col, mat);
        m->tri(i0, i1, i2);
    }
    // A box from its centre and half extents along three axes (made right-handed: a mirrored frame's would turn the
    // box inside out)
    void box(vec3 c, vec3 ax, vec3 ay, vec3 az, vec3 he, u32 col, u32 mat) const {
        if (dot(cross(ax, ay), az) < 0.f) ay = -ay;
        m->box(c - org, ax, ay, az, he, col, mat, true);
    }
    // A flat polygon (world xy, any winding) at z, facing up or down
    void flat(std::vector<vec2> p, float z, bool up, u32 col, u32 mat) const {
        if (p.size() < 3) return;
        if (signedArea(p) < 0.f) std::reverse(p.begin(), p.end());
        std::vector<vec3> q;
        for (vec2 v : p) q.push_back(vec3(v, z) - org);
        m->polygon(q, vec3(0.f, 0.f, up ? 1.f : -1.f), col, mat, 1.f);
    }
};

// One wall's outer face, seen from outside: from its left end s along r (unit) for len metres, facing n
struct Face {
    vec2 s = vec2(0.f), r = vec2(1.f, 0.f), n = vec2(0.f, -1.f);
    float len = 0.f;
    vec3 at(float u, float z, float out = 0.f) const { return vec3(s + r * u + n * out, z); }
    vec3 N() const { return vec3(n, 0.f); }
    vec3 R() const { return vec3(r, 0.f); }
};

// A rectangle of a wall's face, u0..u1 along it and z0..z1 up, `out` metres in front of it, facing out
void faceRect(const Out& o, const Face& f, float u0, float u1, float z0, float z1, float out, u32 col, u32 mat, float zRef) {
    if (u1 - u0 < 0.002f || z1 - z0 < 0.002f) return;
    o.quad(f.at(u0, z0, out), f.at(u1, z0, out), f.at(u1, z1, out), f.at(u0, z1, out), vec2(u0, z0 - zRef), vec2(u1, z0 - zRef), vec2(u1, z1 - zRef),
           vec2(u0, z1 - zRef), col, mat);
}

// The uv rectangle of a hand-built window's glass in its facade entry (prepareHandBuildings lays the entries out):
// inside the entry's one window cell, as facade.hlsli finds it (a storefront's 0.92 of its bay from 0.35 m up)
void glassUV(const FacadeGPU& f, float w, float h, vec2& uv0, vec2& uv1) {
    const bool shopfront = (f.flags & 1u) != 0u;
    const float x0 = shopfront ? f.bayW * 0.04f : (f.bayW - f.bayW * f.winW) * 0.5f, y0 = shopfront ? 0.35f : f.sillH;
    uv0 = vec2(x0 + kEps, y0 + kEps);
    uv1 = vec2(x0 + Max(w - kEps, 2.f * kEps), y0 + Max(h - kEps, 2.f * kEps));
}

struct Hole {
    float u0, u1, z0, z1;
    int opening;
};

struct BuildCtx {
    const HandBuildings& hb;
    const HandPlacement& p;
    const HandShell& s;
    const WorldMap& map;
    const Out& o;
    bool detail;
    Frame fr;
    Paint pt;
    float zFound = 0.f;   // the foundation's bottom (world)
    float zTop = 0.f;     // the walls' top above the ground floor (plan)
    float tTop = 0.25f;
    u32 firstOpening = kNone32;
};

// The facade entry of opening k of wall w (prepareHandBuildings), or kNone32
u32 openingFacade(const BuildCtx& x, size_t w, size_t k) {
    if (x.firstOpening == kNone32) return kNone32;
    size_t idx = x.firstOpening;
    for (size_t i = 0; i < w; i++) idx += x.s.walls[i].openings.size();
    idx += k;
    return idx < x.hb.openingFacades.size() ? x.hb.openingFacades[idx] : kNone32;
}

// A window's or glass door's glass at `out` (behind the face: negative), lit through its facade entry where it has one
void glass(const BuildCtx& x, const Face& f, const Hole& h, float out, u32 facade) {
    const vec3 a = f.at(h.u0, h.z0, out), b = f.at(h.u1, h.z0, out), c = f.at(h.u1, h.z1, out), d = f.at(h.u0, h.z1, out);
    if (facade != kNone32 && gBuildings && facade < gBuildings->facades.size()) {
        vec2 uv0, uv1;
        glassUV(gBuildings->facades[facade], h.u1 - h.u0, h.z1 - h.z0, uv0, uv1);
        x.o.quad(a, b, c, d, uv0, vec2(uv1.x, uv0.y), uv1, vec2(uv0.x, uv1.y), vcol(rgb(1.f, 1.f, 1.f)), makeMat(MAT_FACADE, facade));
    } else {
        x.o.quad(a, b, c, d, vec2(0.f), vec2(h.u1 - h.u0, 0.f), vec2(h.u1 - h.u0, h.z1 - h.z0), vec2(0.f, h.z1 - h.z0), vcol(rgb(0.55f, 0.6f, 0.62f)), makeMat(MAT_GLASS));
    }
}

// A thin bar across the glass (glazing bars, a door's meeting stiles) or a frame's side, `depth` deep from the glass
void bar(const BuildCtx& x, const Face& f, float u0, float u1, float z0, float z1, float glassOut, float depth, u32 col, u32 mat) {
    const vec3 c = f.at((u0 + u1) * 0.5f, (z0 + z1) * 0.5f, glassOut + depth * 0.5f);
    x.o.box(c, f.R(), f.N(), vec3(0.f, 0.f, 1.f), vec3((u1 - u0) * 0.5f, depth * 0.5f, (z1 - z0) * 0.5f), col, mat);
}

// One exterior wall: its face around the openings (the base below the look's base height on the ground floor), and in
// each opening its reveals, glass or door leaf, and (near LOD) sill, glazing bars, shutters and eyebrow
void buildWall(const BuildCtx& x, size_t wi, bool topWall, bool parapet) {
    const HandShell::Wall& w = x.s.walls[wi];
    const HandShell::Floor& fl = x.s.floors[(size_t)w.floor];
    const vec2 d = w.b - w.a;
    const float len = length(d);
    if (len < 0.05f) return;
    const vec2 e = d * (1.f / len);
    vec2 nl(e.y, -e.x);   // (outward: away from the footprint)
    if (inside(x.s.footprint, (w.a + w.b) * 0.5f + nl * 0.05f)) nl = -nl;
    const float ht = w.t * 0.5f;
    // the outer face, past each end by half the wall's thickness (to close the corners)
    vec2 A = x.fr.point(w.a + nl * ht - e * ht), B = x.fr.point(w.b + nl * ht + e * ht);
    Face f;
    f.n = normalize(x.fr.dir(nl));
    f.r = vec2(-f.n.y, f.n.x);   // (Z x n: to the right seen from outside)
    if (dot(B - A, f.r) < 0.f) std::swap(A, B);
    f.s = A;
    f.len = length(B - A);
    const bool ground = fabsf(fl.z - shellBottom(x.s)) < 0.01f;
    const float zRef = x.fr.z;
    const float z0 = ground ? x.zFound : x.fr.z + fl.z;
    const float z1 = x.fr.z + fl.z + fl.h + (topWall && parapet ? kParapet : 0.f);
    const float zBase = ground && x.pt.look.baseH > 0.f ? x.fr.z + fl.z + x.pt.look.baseH : z0;
    const u32 wallCol = x.pt.wall(), wallMat = makeMat(x.pt.look.wallMaterial);
    const u32 baseCol = x.pt.look.baseMaterial == x.pt.look.wallMaterial ? x.pt.colour(PAL_TRIM) : vcol(rgb(1.f, 1.f, 1.f));
    const u32 baseMat = makeMat(x.pt.look.baseMaterial);
    // the openings, along the face, clipped to it and to each other
    std::vector<Hole> holes;
    for (size_t k = 0; k < w.openings.size(); k++) {
        const HandShell::Opening& op = w.openings[k];
        const vec2 cw = x.fr.point(w.a + e * op.at + nl * ht);
        const float uc = dot(cw - f.s, f.r);
        Hole h;
        h.u0 = Max(uc - op.w * 0.5f, 0.05f);
        h.u1 = Min(uc + op.w * 0.5f, f.len - 0.05f);
        h.z0 = x.fr.z + fl.z + (op.door() ? 0.f : op.sill);
        h.z1 = Min(h.z0 + op.h, x.fr.z + fl.z + fl.h - 0.05f);
        h.opening = (int)k;
        if (h.u1 - h.u0 > 0.1f && h.z1 - h.z0 > 0.1f) holes.push_back(h);
    }
    std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) { return a.u0 < b.u0; });
    std::vector<Hole> kept;
    for (const Hole& h : holes)
        if (kept.empty() || h.u0 >= kept.back().u1 + 0.02f) kept.push_back(h);   // (overlapping openings: the first stands)
    // the face around them
    const auto rect = [&](float u0, float u1, float a, float b) {
        if (b <= a + 0.002f) return;
        if (zBase > a + 0.002f) {
            faceRect(x.o, f, u0, u1, a, Min(b, zBase), 0.f, baseCol, baseMat, zRef);
            if (b > zBase + 0.002f) faceRect(x.o, f, u0, u1, zBase, b, 0.f, wallCol, wallMat, zRef);
        } else {
            faceRect(x.o, f, u0, u1, a, b, 0.f, wallCol, wallMat, zRef);
        }
    };
    float u = 0.f;
    for (const Hole& h : kept) {
        rect(u, h.u0, z0, z1);
        rect(h.u0, h.u1, z0, h.z0);
        rect(h.u0, h.u1, h.z1, z1);
        u = h.u1;
    }
    rect(u, f.len, z0, z1);
    // in each opening
    const vec3 Z(0.f, 0.f, 1.f);
    for (const Hole& h : kept) {
        const HandShell::Opening& op = w.openings[(size_t)h.opening];
        const u32 fac = openingFacade(x, wi, (size_t)h.opening);
        const u8 kind = (u8)(op.kind & 255u);
        const bool door = op.door();
        const bool glassDoor = door && (kind == DOOR_PAIR || kind == DOOR_SLIDING || kind == DOOR_REVOLVING);
        const bool openOnly = door && kind == DOOR_OPENING;
        const float back = openOnly ? -w.t : (x.detail ? -Min(kRecess, w.t * 0.6f) : 0.004f);
        if (x.detail || openOnly) {   // the reveals: the jambs, the head and the sill's bottom
            const float rb = back;
            const u32 rc = h.z0 < zBase ? baseCol : wallCol, rm = h.z0 < zBase ? baseMat : wallMat;
            x.o.quadF(f.at(h.u0, h.z0, 0.f), f.at(h.u0, h.z0, rb), f.at(h.u0, h.z1, rb), f.at(h.u0, h.z1, 0.f), rc, rm, f.R());
            x.o.quadF(f.at(h.u1, h.z0, 0.f), f.at(h.u1, h.z0, rb), f.at(h.u1, h.z1, rb), f.at(h.u1, h.z1, 0.f), rc, rm, -f.R());
            x.o.quadF(f.at(h.u0, h.z1, 0.f), f.at(h.u1, h.z1, 0.f), f.at(h.u1, h.z1, rb), f.at(h.u0, h.z1, rb), wallCol, wallMat, -Z);
            if (!door) x.o.quadF(f.at(h.u0, h.z0, 0.f), f.at(h.u1, h.z0, 0.f), f.at(h.u1, h.z0, rb), f.at(h.u0, h.z0, rb), x.pt.colour(PAL_TRIM), wallMat, Z);
        }
        if (!door || glassDoor) {
            glass(x, f, h, back, fac);
        } else if (openOnly) {
            faceRect(x.o, f, h.u0, h.u1, h.z0, h.z1, back, vcol(rgb(0.08f, 0.08f, 0.08f)), makeMat(MAT_CONCRETE), zRef);
        } else {   // a leaf: a panel door, a roll-up shutter or a garage door
            const u32 mat = makeMat(kind == DOOR_ROLLUP ? MAT_CORRUGATED : kind == DOOR_LIFT ? MAT_METAL_PAINTED : MAT_WOOD);
            const u32 col = kind == DOOR_ROLLUP ? vcol(rgb(0.72f, 0.72f, 0.7f)) : x.pt.door();
            faceRect(x.o, f, h.u0, h.u1, h.z0, h.z1, back, col, mat, h.z0);
        }
        if (!x.detail) continue;
        const u32 frameCol = x.pt.frame(), frameMat = makeMat(x.pt.look.frameMaterial);
        const float gw = h.u1 - h.u0, gh = h.z1 - h.z0;
        if (glassDoor && kind == DOOR_PAIR) bar(x, f, h.u0 + gw * 0.5f - 0.03f, h.u0 + gw * 0.5f + 0.03f, h.z0, h.z1, back, 0.05f, frameCol, frameMat);
        if (fac == kNone32 && (!door || glassDoor)) {   // (no facade entry draws its frame: four bars round the glass)
            bar(x, f, h.u0, h.u1, h.z0, h.z0 + 0.06f, back, 0.05f, frameCol, frameMat);
            bar(x, f, h.u0, h.u1, h.z1 - 0.06f, h.z1, back, 0.05f, frameCol, frameMat);
            bar(x, f, h.u0, h.u0 + 0.06f, h.z0, h.z1, back, 0.05f, frameCol, frameMat);
            bar(x, f, h.u1 - 0.06f, h.u1, h.z0, h.z1, back, 0.05f, frameCol, frameMat);
        }
        if (door) {
            if (!glassDoor && !openOnly && kind == DOOR_HINGED)   // the handle
                x.o.box(f.at(h.u1 - 0.12f, h.z0 + 1.0f, back + 0.03f), f.R(), f.N(), Z, vec3(0.05f, 0.03f, 0.015f), vcol(rgb(0.7f, 0.66f, 0.55f)),
                        makeMat(MAT_METAL_BRUSHED));
            continue;
        }
        // glazing bars by the window's kind (the facade shader frames the glass; storefronts have its mullions)
        if (kind == WIN_DHUNG) bar(x, f, h.u0, h.u1, h.z0 + gh * 0.5f - 0.025f, h.z0 + gh * 0.5f + 0.025f, back, 0.04f, frameCol, frameMat);
        if (kind == WIN_CASEMENT || kind == WIN_SLIDER) bar(x, f, h.u0 + gw * 0.5f - 0.025f, h.u0 + gw * 0.5f + 0.025f, h.z0, h.z1, back, 0.04f, frameCol, frameMat);
        if (kind == WIN_JALOUSIE)
            for (float z = h.z0 + 0.1f; z < h.z1 - 0.05f; z += 0.1f) bar(x, f, h.u0, h.u1, z - 0.012f, z + 0.012f, back, 0.06f, frameCol, frameMat);
        if (kind == WIN_GLASSBLOCK) {
            for (float z = h.z0 + 0.2f; z < h.z1 - 0.05f; z += 0.2f) bar(x, f, h.u0, h.u1, z - 0.01f, z + 0.01f, back, 0.03f, vcol(rgb(0.8f, 0.8f, 0.78f)), makeMat(MAT_CONCRETE));
            for (float uu = h.u0 + 0.2f; uu < h.u1 - 0.05f; uu += 0.2f) bar(x, f, uu - 0.01f, uu + 0.01f, h.z0, h.z1, back, 0.03f, vcol(rgb(0.8f, 0.8f, 0.78f)), makeMat(MAT_CONCRETE));
        }
        // the sill: a ledge under the window, out of the face
        if (kind != WIN_STOREFRONT)
            x.o.box(f.at((h.u0 + h.u1) * 0.5f, h.z0 - 0.03f, 0.02f), f.R(), f.N(), Z, vec3(gw * 0.5f + 0.06f, 0.07f, 0.03f), x.pt.colour(PAL_TRIM),
                    makeMat(MAT_CONCRETE));
        // the eyebrow (a look's trim): a ledge over the window
        for (const HandLook::Trim& t : x.pt.look.trims)
            if (t.kind == TRIM_EYEBROW && kind != WIN_STOREFRONT)
                x.o.box(f.at((h.u0 + h.u1) * 0.5f, h.z1 + 0.08f + t.h * 0.5f, 0.09f), f.R(), f.N(), Z, vec3(gw * 0.5f + 0.12f, 0.09f, t.h * 0.5f), x.pt.colour(t.color),
                        makeMat(t.material));
        // shutters: a pair beside the window, or a Bahama shutter hinged over it and propped out
        if (x.pt.look.shutters != SHUTTER_NONE && kind != WIN_STOREFRONT && kind != WIN_GLASSBLOCK && gh > 0.7f) {
            const u32 sc = x.pt.colour(PAL_ACCENT), sm = makeMat(MAT_WOOD);
            if (x.pt.look.shutters == SHUTTER_FIXED) {
                const float sw = Min(gw * 0.5f, 0.6f);
                if (h.u0 - sw > 0.06f && h.u1 + sw < f.len - 0.06f) {
                    x.o.box(f.at(h.u0 - sw * 0.5f - 0.02f, (h.z0 + h.z1) * 0.5f, 0.03f), f.R(), f.N(), Z, vec3(sw * 0.5f, 0.02f, gh * 0.5f), sc, sm);
                    x.o.box(f.at(h.u1 + sw * 0.5f + 0.02f, (h.z0 + h.z1) * 0.5f, 0.03f), f.R(), f.N(), Z, vec3(sw * 0.5f, 0.02f, gh * 0.5f), sc, sm);
                }
            } else {
                const float l = gh * 0.85f, ang = 0.6f;   // (a 34 degree prop)
                const vec3 hinge0 = f.at(h.u0 - 0.04f, h.z1 + 0.04f, 0.02f), hinge1 = f.at(h.u1 + 0.04f, h.z1 + 0.04f, 0.02f);
                const vec3 drop = vec3(0.f, 0.f, -l * cosf(ang)) + f.N() * (l * sinf(ang));
                const vec3 nOut = normalize(f.N() * cosf(ang) + Z * sinf(ang));
                x.o.quadF(hinge0, hinge1, hinge1 + drop, hinge0 + drop, sc, sm, nOut);
                x.o.quadF(hinge0, hinge1, hinge1 + drop, hinge0 + drop, sc, sm, -nOut);
            }
        }
    }
}

// A band of trim round an outline (counter-clockwise, world xy) from z0 to z1, `out` beyond it: its face, top and
// bottom
void band(const Out& o, const std::vector<vec2>& outline, float out, float z0, float z1, u32 col, u32 mat) {
    const std::vector<vec2> a = offsetPoly(outline, 0.f), b = offsetPoly(outline, out);
    const size_t n = a.size();
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        const vec2 e = b[j] - b[i];
        const float l = length(e);
        if (l < 0.01f) continue;
        const vec3 nrm(e.y / l, -e.x / l, 0.f);
        o.quadF(vec3(b[i], z0), vec3(b[j], z0), vec3(b[j], z1), vec3(b[i], z1), col, mat, nrm);
        o.quadF(vec3(a[i], z1), vec3(a[j], z1), vec3(b[j], z1), vec3(b[i], z1), col, mat, vec3(0.f, 0.f, 1.f));
        o.quadF(vec3(a[i], z0), vec3(a[j], z0), vec3(b[j], z0), vec3(b[i], z0), col, mat, vec3(0.f, 0.f, -1.f));
    }
}

// The roof over the walls' top: a flat roof behind its parapet, or a pitched one (gable, hip, shed) over a rectangular
// footprint, with its overhang, fascia and soffit; a pitched form over any other footprint is flat for now
void buildRoof(const BuildCtx& x, bool& parapet) {
    const HandShell& s = x.s;
    const Frame& fr = x.fr;
    const float zW = fr.z + x.zTop;
    std::vector<vec2> outer;
    for (vec2 q : offsetPoly(s.footprint, x.tTop * 0.5f)) outer.push_back(fr.point(q));
    const u32 roofCol = x.pt.roof(), roofMat = makeMat(s.roofMaterial);
    const u32 trimCol = x.pt.colour(PAL_TRIM);
    vec2 lo, hi;
    const bool rect = planRect(s.footprint, lo, hi);
    int form = s.roofForm;
    if (form == RFORM_MANSARD) form = RFORM_HIP;
    if (form == RFORM_BARREL) form = RFORM_GABLE;
    if (!rect || s.pitch < 1.f) form = RFORM_FLAT;
    parapet = form == RFORM_FLAT;
    if (form == RFORM_FLAT) {
        const std::vector<vec2> inner = offsetPoly(outer, -kParapetT);
        x.o.flat(inner, zW, true, roofCol, roofMat);
        // the parapet's inside and cap (its outside is the walls')
        const float zp = zW + kParapet;
        for (size_t i = 0; i < inner.size(); i++) {
            const size_t j = (i + 1) % inner.size();
            const vec2 e = inner[j] - inner[i];
            const float l = length(e);
            if (l < 0.01f) continue;
            const vec3 in(-e.y / l, e.x / l, 0.f);   // (inward: to the left of the counter-clockwise edge)
            x.o.quadF(vec3(inner[i], zW), vec3(inner[j], zW), vec3(inner[j], zp), vec3(inner[i], zp), x.pt.wall(), makeMat(x.pt.look.wallMaterial), in);
        }
        band(x.o, offsetPoly(outer, -kParapetT), kParapetT + 0.04f, zp, zp + 0.06f, trimCol, makeMat(MAT_CONCRETE));
        return;
    }
    // the eaves' rectangle in the plan's frame, and the roof's planes in it
    const float o = Max(s.overhang, 0.f), ht = x.tTop * 0.5f;
    const vec2 elo = lo - vec2(ht + o), ehi = hi + vec2(ht + o);
    const float tanP = tanf(Clamp(s.pitch, 1.f, 70.f) * kDegToRad);
    const float zE = zW - tanP * o;
    const bool alongX = (ehi.x - elo.x) >= (ehi.y - elo.y);   // the ridge runs along the longer side
    // plan point (x, y) at height z, into the world
    const auto P = [&](float px, float py, float z) { return vec3(fr.point(vec2(px, py)), z); };
    const auto up = [&](vec2 dirLocal, float slope) {   // the outward-and-up normal of a plane falling along dirLocal
        const vec2 dw = normalize(fr.dir(dirLocal));
        return normalize(vec3(dw * slope, 1.f));
    };
    const u32 soffitCol = trimCol, soffitMat = makeMat(MAT_WOOD);
    const auto plane = [&](vec3 a, vec3 b, vec3 c, vec3 d, vec3 n) {   // a roof plane, its underside a little lower
        x.o.quadF(a, b, c, d, roofCol, roofMat, n);
        if (x.detail) {
            const vec3 dn(0.f, 0.f, -kRoofT);
            x.o.quadF(a + dn, b + dn, c + dn, d + dn, soffitCol, soffitMat, -n);
        }
    };
    const auto planeTri = [&](vec3 a, vec3 b, vec3 c, vec3 n) {
        x.o.tri(a, b, c, roofCol, roofMat, n, vec2(0.f), vec2(length(b - a), 0.f), vec2(0.f, length(c - a)));
        if (x.detail) {
            const vec3 dn(0.f, 0.f, -kRoofT);
            x.o.tri(a + dn, b + dn, c + dn, soffitCol, soffitMat, -n, vec2(0.f), vec2(1.f, 0.f), vec2(0.f, 1.f));
        }
    };
    const auto fascia = [&](vec3 a, vec3 b, vec3 outward) {   // the edge board under a roof edge
        x.o.quadF(a + vec3(0.f, 0.f, -kRoofT), b + vec3(0.f, 0.f, -kRoofT), b, a, trimCol, makeMat(MAT_WOOD), outward);
    };
    const u32 wallCol = x.pt.wall(), wallMat = makeMat(x.pt.look.wallMaterial);
    const vec2 olo = lo - vec2(ht), ohi = hi + vec2(ht);   // (the walls' outer faces)
    if (form == RFORM_GABLE || form == RFORM_SHED) {
        // gable: two planes from the eaves to the ridge; shed: one from the low back eave up to the front
        if (form == RFORM_GABLE) {
            if (alongX) {
                const float ym = (elo.y + ehi.y) * 0.5f, zR = zE + tanP * (ehi.y - elo.y) * 0.5f;
                plane(P(elo.x, elo.y, zE), P(ehi.x, elo.y, zE), P(ehi.x, ym, zR), P(elo.x, ym, zR), up(vec2(0.f, -1.f), tanP));
                plane(P(ehi.x, ehi.y, zE), P(elo.x, ehi.y, zE), P(elo.x, ym, zR), P(ehi.x, ym, zR), up(vec2(0.f, 1.f), tanP));
                // the gable ends: the end walls' triangles up to the roof's underside
                const float zRw = zW + tanP * (ohi.y - olo.y) * 0.5f;
                for (int side = 0; side < 2; side++) {
                    const float gx = side ? ohi.x : olo.x;
                    const vec3 nw = vec3(normalize(fr.dir(vec2(side ? 1.f : -1.f, 0.f))), 0.f);
                    x.o.tri(P(gx, olo.y, zW), P(gx, ohi.y, zW), P(gx, ym, zRw - kRoofT), wallCol, wallMat, nw, vec2(0.f), vec2(ohi.y - olo.y, 0.f),
                            vec2((ohi.y - olo.y) * 0.5f, zRw - zW));
                }
                if (x.detail) {
                    fascia(P(elo.x, elo.y, zE), P(ehi.x, elo.y, zE), vec3(normalize(fr.dir(vec2(0.f, -1.f))), 0.f));
                    fascia(P(ehi.x, ehi.y, zE), P(elo.x, ehi.y, zE), vec3(normalize(fr.dir(vec2(0.f, 1.f))), 0.f));
                    for (int side = 0; side < 2; side++) {   // the barge boards along the gables' slopes
                        const float gx = side ? ehi.x : elo.x;
                        const vec3 nw = vec3(normalize(fr.dir(vec2(side ? 1.f : -1.f, 0.f))), 0.f);
                        x.o.quadF(P(gx, elo.y, zE - kRoofT), P(gx, ym, zR - kRoofT), P(gx, ym, zR), P(gx, elo.y, zE), trimCol, makeMat(MAT_WOOD), nw);
                        x.o.quadF(P(gx, ym, zR - kRoofT), P(gx, ehi.y, zE - kRoofT), P(gx, ehi.y, zE), P(gx, ym, zR), trimCol, makeMat(MAT_WOOD), nw);
                    }
                }
            } else {
                const float xm = (elo.x + ehi.x) * 0.5f, zR = zE + tanP * (ehi.x - elo.x) * 0.5f;
                plane(P(elo.x, ehi.y, zE), P(elo.x, elo.y, zE), P(xm, elo.y, zR), P(xm, ehi.y, zR), up(vec2(-1.f, 0.f), tanP));
                plane(P(ehi.x, elo.y, zE), P(ehi.x, ehi.y, zE), P(xm, ehi.y, zR), P(xm, elo.y, zR), up(vec2(1.f, 0.f), tanP));
                const float zRw = zW + tanP * (ohi.x - olo.x) * 0.5f;
                for (int side = 0; side < 2; side++) {
                    const float gy = side ? ohi.y : olo.y;
                    const vec3 nw = vec3(normalize(fr.dir(vec2(0.f, side ? 1.f : -1.f))), 0.f);
                    x.o.tri(P(olo.x, gy, zW), P(ohi.x, gy, zW), P(xm, gy, zRw - kRoofT), wallCol, wallMat, nw, vec2(0.f), vec2(ohi.x - olo.x, 0.f),
                            vec2((ohi.x - olo.x) * 0.5f, zRw - zW));
                }
                if (x.detail) {
                    fascia(P(elo.x, ehi.y, zE), P(elo.x, elo.y, zE), vec3(normalize(fr.dir(vec2(-1.f, 0.f))), 0.f));
                    fascia(P(ehi.x, elo.y, zE), P(ehi.x, ehi.y, zE), vec3(normalize(fr.dir(vec2(1.f, 0.f))), 0.f));
                    for (int side = 0; side < 2; side++) {
                        const float gy = side ? ehi.y : elo.y;
                        const vec3 nw = vec3(normalize(fr.dir(vec2(0.f, side ? 1.f : -1.f))), 0.f);
                        x.o.quadF(P(elo.x, gy, zE - kRoofT), P(xm, gy, zR - kRoofT), P(xm, gy, zR), P(elo.x, gy, zE), trimCol, makeMat(MAT_WOOD), nw);
                        x.o.quadF(P(xm, gy, zR - kRoofT), P(ehi.x, gy, zE - kRoofT), P(ehi.x, gy, zE), P(xm, gy, zR), trimCol, makeMat(MAT_WOOD), nw);
                    }
                }
            }
        } else {
            // shed: low at the back (plan +y), high over the front; the side walls close it with their triangles and
            // the front wall goes up to it
            const float zBack = zE, zFront = zE + tanP * (ehi.y - elo.y);
            plane(P(ehi.x, ehi.y, zBack), P(elo.x, ehi.y, zBack), P(elo.x, elo.y, zFront), P(ehi.x, elo.y, zFront), up(vec2(0.f, 1.f), tanP));
            const float zFw = zW + tanP * (ohi.y - olo.y);
            const vec3 nf = vec3(normalize(fr.dir(vec2(0.f, -1.f))), 0.f);
            x.o.quadF(P(olo.x, olo.y, zW), P(ohi.x, olo.y, zW), P(ohi.x, olo.y, zFw - kRoofT), P(olo.x, olo.y, zFw - kRoofT), wallCol, wallMat, nf);
            for (int side = 0; side < 2; side++) {
                const float gx = side ? ohi.x : olo.x;
                const vec3 nw = vec3(normalize(fr.dir(vec2(side ? 1.f : -1.f, 0.f))), 0.f);
                x.o.tri(P(gx, olo.y, zW), P(gx, ohi.y, zW), P(gx, olo.y, zFw - kRoofT), wallCol, wallMat, nw, vec2(0.f), vec2(ohi.y - olo.y, 0.f), vec2(0.f, zFw - zW));
            }
            if (x.detail) {
                fascia(P(elo.x, elo.y, zFront), P(ehi.x, elo.y, zFront), nf);
                fascia(P(ehi.x, ehi.y, zBack), P(elo.x, ehi.y, zBack), vec3(normalize(fr.dir(vec2(0.f, 1.f))), 0.f));
            }
        }
        return;
    }
    // hip: four planes; the ridge along the longer side, short by the span (a pyramid over a square)
    if (alongX) {
        const float S = ehi.y - elo.y, ym = (elo.y + ehi.y) * 0.5f, zR = zE + tanP * S * 0.5f;
        const float rx0 = Min(elo.x + S * 0.5f, (elo.x + ehi.x) * 0.5f), rx1 = Max(ehi.x - S * 0.5f, (elo.x + ehi.x) * 0.5f);
        plane(P(elo.x, elo.y, zE), P(ehi.x, elo.y, zE), P(rx1, ym, zR), P(rx0, ym, zR), up(vec2(0.f, -1.f), tanP));
        plane(P(ehi.x, ehi.y, zE), P(elo.x, ehi.y, zE), P(rx0, ym, zR), P(rx1, ym, zR), up(vec2(0.f, 1.f), tanP));
        planeTri(P(elo.x, ehi.y, zE), P(elo.x, elo.y, zE), P(rx0, ym, zR), up(vec2(-1.f, 0.f), tanP));
        planeTri(P(ehi.x, elo.y, zE), P(ehi.x, ehi.y, zE), P(rx1, ym, zR), up(vec2(1.f, 0.f), tanP));
    } else {
        const float S = ehi.x - elo.x, xm = (elo.x + ehi.x) * 0.5f, zR = zE + tanP * S * 0.5f;
        const float ry0 = Min(elo.y + S * 0.5f, (elo.y + ehi.y) * 0.5f), ry1 = Max(ehi.y - S * 0.5f, (elo.y + ehi.y) * 0.5f);
        plane(P(elo.x, ehi.y, zE), P(elo.x, elo.y, zE), P(xm, ry0, zR), P(xm, ry1, zR), up(vec2(-1.f, 0.f), tanP));
        plane(P(ehi.x, elo.y, zE), P(ehi.x, ehi.y, zE), P(xm, ry1, zR), P(xm, ry0, zR), up(vec2(1.f, 0.f), tanP));
        planeTri(P(elo.x, elo.y, zE), P(ehi.x, elo.y, zE), P(xm, ry0, zR), up(vec2(0.f, -1.f), tanP));
        planeTri(P(ehi.x, ehi.y, zE), P(elo.x, ehi.y, zE), P(xm, ry1, zR), up(vec2(0.f, 1.f), tanP));
    }
    if (x.detail) {
        fascia(P(elo.x, elo.y, zE), P(ehi.x, elo.y, zE), vec3(normalize(fr.dir(vec2(0.f, -1.f))), 0.f));
        fascia(P(ehi.x, elo.y, zE), P(ehi.x, ehi.y, zE), vec3(normalize(fr.dir(vec2(1.f, 0.f))), 0.f));
        fascia(P(ehi.x, ehi.y, zE), P(elo.x, ehi.y, zE), vec3(normalize(fr.dir(vec2(0.f, 1.f))), 0.f));
        fascia(P(elo.x, ehi.y, zE), P(elo.x, elo.y, zE), vec3(normalize(fr.dir(vec2(-1.f, 0.f))), 0.f));
    }
}

// The look's trims round the walls (near LOD): a band at each floor line above the ground floor, a cornice at the
// top, quoins up the corners of a rectangular footprint
void buildTrims(const BuildCtx& x, bool parapet) {
    const HandShell& s = x.s;
    std::vector<vec2> outer;
    for (vec2 q : offsetPoly(s.footprint, x.tTop * 0.5f)) outer.push_back(x.fr.point(q));
    const float bottom = shellBottom(s);
    for (const HandLook::Trim& t : x.pt.look.trims) {
        const u32 col = x.pt.colour(t.color), mat = makeMat(t.material);
        if (t.kind == TRIM_BAND) {
            for (const HandShell::Floor& f : s.floors) {
                if (f.z <= bottom + 0.01f || f.z >= x.zTop - 0.01f) continue;
                bool walls = false;
                for (const HandShell::Wall& w : s.walls) walls = walls || fabsf(s.floors[(size_t)w.floor].z - f.z) < 0.01f;
                if (!walls) continue;
                band(x.o, outer, 0.06f, x.fr.z + f.z - t.h * 0.5f, x.fr.z + f.z + t.h * 0.5f, col, mat);
            }
        } else if (t.kind == TRIM_CORNICE) {
            const float zt = x.fr.z + x.zTop + (parapet ? kParapet - 0.06f : 0.f);
            band(x.o, outer, parapet ? 0.18f : 0.08f, zt - t.h, zt, col, mat);
        } else if (t.kind == TRIM_QUOIN) {
            vec2 lo, hi;
            if (!planRect(s.footprint, lo, hi)) continue;
            const float ht = x.tTop * 0.5f;
            const vec2 olo = lo - vec2(ht), ohi = hi + vec2(ht);
            const vec2 corners[4] = {olo, vec2(ohi.x, olo.y), ohi, vec2(olo.x, ohi.y)};
            const vec2 inX[4] = {vec2(1.f, 0.f), vec2(-1.f, 0.f), vec2(-1.f, 0.f), vec2(1.f, 0.f)}, inY[4] = {vec2(0.f, 1.f), vec2(0.f, 1.f), vec2(0.f, -1.f), vec2(0.f, -1.f)};
            const vec3 ax = vec3(normalize(x.fr.dir(vec2(1.f, 0.f))), 0.f), ay = vec3(normalize(x.fr.dir(vec2(0.f, 1.f))), 0.f);
            const float h = t.h > 0.05f ? t.h : 0.3f;
            int k = 0;
            for (float z = x.fr.z + bottom + 0.05f; z + h <= x.fr.z + x.zTop; z += h + 0.05f, k++)
                for (int c = 0; c < 4; c++) {
                    const float lx = (k + c) % 2 ? 0.5f : 0.3f, ly = (k + c) % 2 ? 0.3f : 0.5f;
                    const vec2 cl = corners[c] + inX[c] * (lx * 0.5f - 0.03f) + inY[c] * (ly * 0.5f - 0.03f);
                    x.o.box(vec3(x.fr.point(cl), z + h * 0.5f), ax, ay, vec3(0.f, 0.f, 1.f), vec3(lx * 0.5f, ly * 0.5f, h * 0.5f), col, mat);
                }
        }
    }
}

// A placement as the game's building queries know a building (BuildingSet::pointInBuilding, buildingsNear, and what
// is built on them: peds' spawn, flee and window-shopping checks, traffic, police, wildlife, missions, the map): the box
// of its walls' outer faces in the plan's frame, from its ground floor to its walls' top (the roof deck or the eaves),
// its floors, its plan's class as its style (the class list is BuildingStyle's order; a landmark a tower), its front,
// and its street door at its main entrance where that is on its front wall (else none: game/ai.cpp leaves it out). It
// has no facade entry (the shell draws it) and no interior yet, and a seed without a garage (buildings.h
// houseGarage), so the population parks no car on a driveway that isn't there.
Building buildingRecord(const HandShell& s, const HandPlacement& p, const Frame& fr) {
    Building b;
    float t = 0.25f;
    const float top = shellTop(s, &t);
    const std::vector<vec2> outer = offsetPoly(s.footprint, t * 0.5f);
    vec2 lo = outer[0], hi = outer[0];
    for (vec2 q : outer) {
        lo = vec2(Min(lo.x, q.x), Min(lo.y, q.y));
        hi = vec2(Max(hi.x, q.x), Max(hi.y, q.y));
    }
    b.c = fr.point((lo + hi) * 0.5f);
    b.ax = normalize(fr.dir(vec2(1.f, 0.f)));
    b.hx = (hi.x - lo.x) * 0.5f;
    b.hy = (hi.y - lo.y) * 0.5f;
    b.baseZ = p.z;
    b.height = top;
    int floors = 0;
    for (const HandShell::Floor& f : s.floors) {
        bool walls = false;
        for (const HandShell::Wall& w : s.walls) walls = walls || &s.floors[(size_t)w.floor] == &f;
        floors += walls ? 1 : 0;
    }
    b.floors = (u16)Max(floors, 1);
    b.style = (u8)(s.cls >= 0 && s.cls < (int)BS_COUNT ? s.cls : (int)BS_TOWER);
    b.roof = (u8)(s.roofForm == RFORM_GABLE ? ROOF_GABLE : s.roofForm == RFORM_HIP || s.roofForm == RFORM_MANSARD ? ROOF_HIP
                  : s.roofForm == RFORM_SHED ? ROOF_SHED : s.roofForm == RFORM_BARREL ? ROOF_BARREL : ROOF_FLAT);
    b.seed = (hash32(hashString(p.id.c_str())) / 20u) * 10u + 7u;   // (seed % 10 = 7: no garage)
    b.facade = kNone32;
    b.front = normalize(fr.dir(vec2(0.f, -1.f)));   // (the plan's front faces -y)
    b.region = gMap ? (u8)gMap->regionAt(b.c.x, b.c.y) : (u8)0;
    b.lotKind = 0;
    b.lotHy = b.hy;
    b.lotC = b.c;
    b.lotHx = b.hx;
    b.roofColor = 0u;
    b.noStreetDoor = true;
    if (s.entranceWall >= 0 && (size_t)s.entranceWall < s.walls.size()) {
        const HandShell::Wall& w = s.walls[(size_t)s.entranceWall];
        const vec2 d = w.b - w.a;
        if (w.side == 0 && length2(d) > 1e-6f && (size_t)s.entranceOpening < w.openings.size()) {
            const vec2 door = w.a + normalize(d) * w.openings[(size_t)s.entranceOpening].at;   // (on the wall's centreline)
            b.doorAlong = dot(fr.point(door) - b.c, b.ax);
            b.noStreetDoor = false;
        }
    }
    return b;
}

}  // namespace handshells_detail

// At load (handworld.cpp):
// - each placement's streaming cell (its footprint's centre), and its record in the building set `bs`, for the game's
//   building queries (buildingRecord);
// - a facade entry for each window and glass door, appended to `bs.facades` for the renderer's upload. An entry is one
//   window cell (facade.hlsli) the size of the glass, flagged a hand-built window with the class of the room behind it
//   (roomlight.h handWindowFlags), a storefront's where it is one, an office's for an office; the windows of one room
//   share its seed, so they show the same room and light together. Entries that come out the same are shared.
void prepareHandBuildings(HandBuildings& hb, BuildingSet& bs) {
    using namespace handshells_detail;
    hb.placementsByCell.assign((size_t)kCellsPerSide * kCellsPerSide, {});
    hb.placementBuilding.assign(hb.placements.size(), -1);
    hb.firstOpening.assign(hb.placements.size(), kNone32);
    hb.openingFacades.clear();
    std::map<std::string, u32> shared;
    static const vec3 kGlassTints[] = {vec3(0.6f, 0.62f, 0.65f), vec3(0.55f, 0.65f, 0.7f), vec3(0.62f, 0.66f, 0.62f), vec3(0.66f, 0.64f, 0.6f)};
    for (size_t pi = 0; pi < hb.placements.size(); pi++) {
        const HandPlacement& p = hb.placements[pi];
        if (p.shell < 0 || (size_t)p.shell >= hb.shells.size()) continue;
        const HandShell& s = hb.shells[(size_t)p.shell];
        if (s.footprint.size() < 3 || s.floors.empty()) continue;
        const Frame fr = frameOf(p, s);
        vec2 c(0.f);
        for (vec2 q : s.footprint) c += fr.point(q);
        c = c * (1.f / (float)s.footprint.size());
        const int cx = (int)floorf((c.x + kWorldHalf) / kCellSize), cy = (int)floorf((c.y + kWorldHalf) / kCellSize);
        if (cx < 0 || cy < 0 || cx >= kCellsPerSide || cy >= kCellsPerSide) continue;
        const size_t cell = (size_t)cy * kCellsPerSide + cx;
        hb.placementsByCell[cell].push_back((int)pi);
        if (cell < bs.cellLists.size()) {   // (handworld.cpp sizes the building set's cell lists)
            hb.placementBuilding[pi] = (int)bs.buildings.size();
            bs.cellLists[cell].push_back((int)bs.buildings.size());
            bs.buildings.push_back(buildingRecord(s, p, fr));
        }
        hb.firstOpening[pi] = (u32)hb.openingFacades.size();
        const Paint pt = paintOf(hb, p, s);
        const u32 nameHash = hashString(p.id.c_str());
        const vec3 tint = kGlassTints[nameHash % 4u];
        for (const HandShell::Wall& w : s.walls)
            for (size_t k = 0; k < w.openings.size(); k++) {
                const HandShell::Opening& op = w.openings[k];
                const u8 kind = (u8)(op.kind & 255u);
                const bool glassDoor = op.door() && (kind == DOOR_PAIR || kind == DOOR_SLIDING || kind == DOOR_REVOLVING);
                if ((op.door() && !glassDoor) || op.w < 0.2f || op.h < 0.2f) {
                    hb.openingFacades.push_back(kNone32);
                    continue;
                }
                const u32 sched = op.schedule <= 15u ? op.schedule : (u32)RS_NEVER;
                const bool shopfront = kind == WIN_STOREFRONT || (glassDoor && (sched == RS_SHOP || sched == RS_RESTAURANT || sched == RS_NIGHTLIFE));
                FacadeGPU e;
                memset(&e, 0, sizeof(e));
                if (shopfront) {   // (facade.hlsli's storefront: 0.92 of the bay wide, from 0.35 m up to 1.3 m below the floor's top)
                    e.bayW = op.w / 0.92f;
                    e.groundH = op.h + 1.3f;
                    e.floorH = op.h + 1.3f;
                    e.winW = 0.92f;
                    e.winH = 1.f;
                    e.sillH = 0.35f;
                } else {           // (a window cell the glass's size, with 0.2 m of wall each side and 0.3 below)
                    e.bayW = op.w + 0.4f;
                    e.floorH = op.h + 0.6f;
                    e.groundH = 0.f;
                    e.winW = op.w / e.bayW;
                    e.winH = op.h / e.floorH;
                    e.sillH = 0.3f;
                }
                e.roomDepth = shopfront ? 5.f : 4.f;
                e.style = 0.f;
                e.wallColor = pt.wall();
                e.frameColor = pt.frame();
                e.glassColor = rgb(tint.x, tint.y, tint.z);
                e.flags = handWindowFlags(sched) | (shopfront ? 1u : 0u) | (sched == RS_OFFICE ? 16u : 0u);
                e.wallLayer = (float)pt.look.wallMaterial;   // (a MaterialId: the upload turns it into its texture layer)
                e.litFrac = 1.f;                             // (every room is someone's: none left dark at random)
                e.seed = hash32(nameHash ^ hash32((u32)w.floor * 0x9e3779b9u + (op.room != kNone32 ? op.room : 0x8000u + (u32)k) * 0x85ebca77u + 1u));
                e.signIndex = 0.f;
                const std::string key((const char*)&e, sizeof(e));
                auto it = shared.find(key);
                if (it == shared.end()) {
                    it = shared.emplace(key, (u32)bs.facades.size()).first;
                    bs.facades.push_back(e);
                }
                hb.openingFacades.push_back(it->second);
            }
    }
}

// A cell's share of the hand-built world's buildings: each placement whose footprint's centre lies in it, its shell
// drawn by the LOD, and its footprint's bounds as collision at every LOD
void buildHandBuildingsCell(const HandBuildings& hb, const WorldMap& map, int cx, int cy, bool detail, vec3 org, CellGeometry& out) {
    using namespace handshells_detail;
    const size_t cell = (size_t)cy * kCellsPerSide + cx;
    if (cell >= hb.placementsByCell.size()) return;
    Out o;
    o.m = &out.opaque;
    o.org = org;
    for (int pi : hb.placementsByCell[cell]) {
        const HandPlacement& p = hb.placements[(size_t)pi];
        const HandShell& s = hb.shells[(size_t)p.shell];
        if (s.walls.empty()) continue;
        BuildCtx x{hb, p, s, map, o, detail, frameOf(p, s), paintOf(hb, p, s)};
        x.firstOpening = (size_t)pi < hb.firstOpening.size() ? hb.firstOpening[(size_t)pi] : kNone32;
        x.zTop = shellTop(s, &x.tTop);
        // the foundation goes down to 0.3 m under the lowest ground at the walls' corners
        float gMin = p.z;
        for (vec2 q : offsetPoly(s.footprint, x.tTop * 0.5f)) {
            const vec2 w = x.fr.point(q);
            gMin = Min(gMin, map.heightAt(w.x, w.y));
        }
        x.zFound = gMin - 0.3f;
        bool parapet = false;
        buildRoof(x, parapet);
        for (size_t wi = 0; wi < s.walls.size(); wi++) {
            const HandShell::Floor& f = s.floors[(size_t)s.walls[wi].floor];
            buildWall(x, wi, fabsf(f.z + f.h - x.zTop) < 0.01f, parapet);
        }
        if (detail) buildTrims(x, parapet);
        // collision: the walls' outer bounds in the plan's frame, from the foundation to the roof's top
        std::vector<vec2> outer = offsetPoly(s.footprint, x.tTop * 0.5f);
        vec2 lo = outer[0], hi = outer[0];
        for (vec2 q : outer) {
            lo = vec2(Min(lo.x, q.x), Min(lo.y, q.y));
            hi = vec2(Max(hi.x, q.x), Max(hi.y, q.y));
        }
        float zRoof = p.z + x.zTop + (parapet ? kParapet : 0.f);
        if (!parapet) zRoof += tanf(Clamp(s.pitch, 1.f, 70.f) * kDegToRad) * Min(hi.x - lo.x, hi.y - lo.y) * 0.5f;
        CollisionBox cb;
        const vec2 cc = x.fr.point((lo + hi) * 0.5f);
        cb.c = vec3(cc, (x.zFound + zRoof) * 0.5f);
        cb.ax = normalize(x.fr.dir(vec2(1.f, 0.f)));
        cb.he = vec3((hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (zRoof - x.zFound) * 0.5f);
        out.collision.push_back(cb);
    }
}

}  // namespace World
