// Building mesh generation per cell (full detail and far LOD).
#include "buildings.h"
#include "sites.h"
#include "interiors.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace buildmesh_detail {

const u32 kWhite = 0xffffffffu;

struct Ctx {
    MeshData* m;
    vec3 org;  // cell origin (world) subtracted from all positions
    bool detail;
    std::vector<CollisionBox>* col;
    std::vector<PropInstance>* props;
    std::vector<LightInstance>* lights;
    std::vector<FacadeMass>* masses = nullptr;  // facade masses for the street-level detail pass (LOD0)
    int interior = -1;  // enterable interior of the building (world/interiors.cpp): facade cut-outs, hollow collision
};

// Polygon footprint helpers (CCW)
std::vector<vec2> rectFP(vec2 c, vec2 ax, float hx, float hy) {
    vec2 ay = perp(ax);
    return {c - ax * hx - ay * hy, c + ax * hx - ay * hy, c + ax * hx + ay * hy, c - ax * hx + ay * hy};
}
std::vector<vec2> chamferFP(vec2 c, vec2 ax, float hx, float hy, float ch) {
    vec2 ay = perp(ax);
    ch = Min(ch, Min(hx, hy) * 0.45f);
    std::vector<vec2> p;
    vec2 corners[4] = {vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1)};
    for (int i = 0; i < 4; i++) {
        vec2 k = corners[i];
        vec2 prev = corners[(i + 3) % 4];
        vec2 next = corners[(i + 1) % 4];
        vec2 cp = vec2(k.x * hx, k.y * hy);
        vec2 dPrev = normalize(vec2(prev.x * hx, prev.y * hy) - cp), dNext = normalize(vec2(next.x * hx, next.y * hy) - cp);
        vec2 a = cp + dPrev * ch, b = cp + dNext * ch;
        p.push_back(c + ax * a.x + ay * a.y);
        p.push_back(c + ax * b.x + ay * b.y);
    }
    return p;
}
std::vector<vec2> roundFP(vec2 c, float r, int seg, float rot) {
    std::vector<vec2> p;
    for (int i = 0; i < seg; i++) {
        float a = rot + kTwoPi * i / seg;
        p.push_back(c + vec2(cosf(a), sinf(a)) * r);
    }
    return p;
}

// Facade walls around a footprint from z0 to z1. v is measured from vBase (building base).
void facadeWalls(Ctx& x, const std::vector<vec2>& fp, float z0, float z1, float vBase, u32 facadeId, float bay, u32 color = kWhite) {
    int n = (int)fp.size();
    for (int i = 0; i < n; i++) {
        vec2 a = fp[i], b = fp[(i + 1) % n];
        float len = length(b - a);
        if (len < 0.05f) continue;
        // stretch bays to fit the wall exactly; each wall starts on a bay boundary so windows sit centered and whole
        // (facadedetail.cpp aligns sills, frames and storefront mullions with this grid)
        float bays = Max(1.f, roundf(len / bay));
        float uLen = bays * bay;
        float u0 = (floorf((float)i * 1000.f / bay) + 0.002f) * bay;
        // enterable interior: the wall is emitted with its door / window openings cut out
        if (x.interior >= 0 && x.detail && interiorFacadeWall(x.interior, *x.m, x.org, a, b, z0, z1, u0, uLen, vBase, color, makeMat(MAT_FACADE, facadeId)))
            continue;
        vec3 p0 = vec3(a, z0) - x.org, p1 = vec3(b, z0) - x.org;
        vec3 up(0, 0, z1 - z0);
        x.m->quad(p0, p1, p1 + up, p0 + up, vec2(u0, z0 - vBase), vec2(u0 + uLen, z0 - vBase), vec2(u0 + uLen, z1 - vBase),
                  vec2(u0, z1 - vBase), color, makeMat(MAT_FACADE, facadeId));
    }
}

void plainWalls(Ctx& x, const std::vector<vec2>& fp, float z0, float z1, u32 color, u32 mat) {
    int n = (int)fp.size();
    float u = 0;
    for (int i = 0; i < n; i++) {
        vec2 a = fp[i], b = fp[(i + 1) % n];
        float len = length(b - a);
        vec3 p0 = vec3(a, z0) - x.org, p1 = vec3(b, z0) - x.org;
        vec3 up(0, 0, z1 - z0);
        x.m->quad(p0, p1, p1 + up, p0 + up, vec2(u, z0), vec2(u + len, z0), vec2(u + len, z1), vec2(u, z1), color, mat);
        u += len;
    }
}

void flatRoof(Ctx& x, const std::vector<vec2>& fp, float z, u32 color, u32 mat) {
    std::vector<vec3> poly;
    for (auto& p : fp) poly.push_back(vec3(p, z) - x.org);
    x.m->polygon(poly, vec3(0, 0, 1), color, mat, 1.f);
}

// Parapet: a low wall ring inset by thickness, with top cap
void parapet(Ctx& x, const std::vector<vec2>& fp, float z, float h, float t, u32 color, u32 mat) {
    int n = (int)fp.size();
    // centroid for inward direction
    vec2 c(0, 0);
    for (auto& p : fp) c += p;
    c = c / (float)n;
    std::vector<vec2> inner;
    for (int i = 0; i < n; i++) {
        vec2 p = fp[i];
        vec2 prev = fp[(i + n - 1) % n], next = fp[(i + 1) % n];
        vec2 n0 = perp(normalize(p - prev)), n1 = perp(normalize(next - p));  // left normals = inward for CCW
        vec2 nm = normalize(n0 + n1);
        float scale = 1.f / Max(0.3f, dot(nm, n1));
        inner.push_back(p + nm * (t * scale));
    }
    // outer face continues the facade (drawn by caller); inner face + cap
    for (int i = 0; i < n; i++) {
        vec2 a = inner[i], b = inner[(i + 1) % n];
        vec3 p0 = vec3(b, z) - x.org, p1 = vec3(a, z) - x.org;
        vec3 up(0, 0, h);
        x.m->quad(p0, p1, p1 + up, p0 + up, vec2(0, 0), vec2(length(b - a), 0), vec2(length(b - a), h), vec2(0, h), color, mat);
        vec2 oa = fp[i], ob = fp[(i + 1) % n];
        x.m->quadFacing(vec3(oa, z + h) - x.org, vec3(ob, z + h) - x.org, vec3(b, z + h) - x.org, vec3(a, z + h) - x.org,
                        vec2(0, 0), vec2(1, 0), vec2(1, t), vec2(0, t), color, mat, vec3(0, 0, 1));
    }
}

// Hip or gable roof on a rectangle
void pitchedRoof(Ctx& x, vec2 c, vec2 ax, float hx, float hy, float z, float pitch, bool hip, float overhang, u32 color, u32 mat,
                 u32 gableColor, u32 gableMat) {
    vec2 ay = perp(ax);
    // ridge along the longer axis
    bool alongX = hx >= hy;
    vec2 L = alongX ? ax : ay, S = alongX ? ay : -ax;
    float hl = (alongX ? hx : hy) + overhang, hs = (alongX ? hy : hx) + overhang;
    float rise = hs * pitch;
    float ridgeHalf = hip ? Max(0.f, hl - hs) : hl;
    vec3 o = x.org;
    vec3 e00 = vec3(c - L * hl - S * hs, z) - o, e10 = vec3(c + L * hl - S * hs, z) - o;
    vec3 e11 = vec3(c + L * hl + S * hs, z) - o, e01 = vec3(c - L * hl + S * hs, z) - o;
    vec3 r0 = vec3(c - L * ridgeHalf, z + rise) - o, r1 = vec3(c + L * ridgeHalf, z + rise) - o;
    float slopeLen = sqrtf(hs * hs + rise * rise);
    // Long sides
    x.m->quadFacing(e00, e10, r1, r0, vec2(0, 0), vec2(2 * hl, 0), vec2(hl + ridgeHalf, slopeLen), vec2(hl - ridgeHalf, slopeLen), color, mat,
                    vec3(-S, 0.5f));
    x.m->quadFacing(e11, e01, r0, r1, vec2(0, 0), vec2(2 * hl, 0), vec2(hl + ridgeHalf, slopeLen), vec2(hl - ridgeHalf, slopeLen), color, mat,
                    vec3(S, 0.5f));
    if (hip) {
        // End triangles (as degenerate quads)
        u32 i0, i1, i2;
        vec3 n0 = normalize(cross(e01 - e00, r0 - e00));
        if (dot(n0, vec3(-L, 0)) < 0) n0 = -n0;
        i0 = x.m->addVertex(e00, n0, vec3(S, 0), vec2(0, 0), color, mat);
        i1 = x.m->addVertex(e01, n0, vec3(S, 0), vec2(2 * hs, 0), color, mat);
        i2 = x.m->addVertex(r0, n0, vec3(S, 0), vec2(hs, slopeLen), color, mat);
        if (dot(cross(e01 - e00, r0 - e00), n0) >= 0) x.m->tri(i0, i1, i2); else x.m->tri(i0, i2, i1);
        vec3 n1 = normalize(cross(e11 - e10, r1 - e10));
        if (dot(n1, vec3(L, 0)) < 0) n1 = -n1;
        i0 = x.m->addVertex(e10, n1, vec3(S, 0), vec2(0, 0), color, mat);
        i1 = x.m->addVertex(e11, n1, vec3(S, 0), vec2(2 * hs, 0), color, mat);
        i2 = x.m->addVertex(r1, n1, vec3(S, 0), vec2(hs, slopeLen), color, mat);
        if (dot(cross(e11 - e10, r1 - e10), n1) >= 0) x.m->tri(i0, i1, i2); else x.m->tri(i0, i2, i1);
    } else {
        // Gable walls (triangles) in wall material
        vec3 g00 = vec3(c - L * (hl - overhang) - S * (hs - overhang), z) - o, g01 = vec3(c - L * (hl - overhang) + S * (hs - overhang), z) - o;
        vec3 g10 = vec3(c + L * (hl - overhang) - S * (hs - overhang), z) - o, g11 = vec3(c + L * (hl - overhang) + S * (hs - overhang), z) - o;
        vec3 gr0 = vec3(c - L * (hl - overhang), z + (hs - overhang) * pitch) - o, gr1 = vec3(c + L * (hl - overhang), z + (hs - overhang) * pitch) - o;
        for (int k = 0; k < 2; k++) {
            vec3 a = k ? g10 : g00, b = k ? g11 : g01, r = k ? gr1 : gr0;
            vec3 want = vec3(k ? L : -L, 0);
            vec3 n = normalize(cross(b - a, r - a));
            if (dot(n, want) < 0) n = -n;
            u32 i0 = x.m->addVertex(a, n, normalize(b - a), vec2(0, 0), gableColor, gableMat);
            u32 i1 = x.m->addVertex(b, n, normalize(b - a), vec2(length(b - a), 0), gableColor, gableMat);
            u32 i2 = x.m->addVertex(r, n, normalize(b - a), vec2(length(b - a) * 0.5f, (hs - overhang) * pitch), gableColor, gableMat);
            if (dot(cross(b - a, r - a), n) >= 0) x.m->tri(i0, i1, i2); else x.m->tri(i0, i2, i1);
        }
    }
    // soffit (underside of the overhang)
    if (overhang > 0.05f) {
        vec3 s00 = vec3(c - L * hl - S * hs, z) - o, s10 = vec3(c + L * hl - S * hs, z) - o;
        vec3 s11 = vec3(c + L * hl + S * hs, z) - o, s01 = vec3(c - L * hl + S * hs, z) - o;
        x.m->quadFacing(s00, s01, s11, s10, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), gableColor, makeMat(MAT_PLASTER), vec3(0, 0, -1));
    }
}

void addCollision(Ctx& x, vec2 c, vec2 ax, float hx, float hy, float z0, float z1) {
    if (!x.col) return;
    if (x.interior >= 0 && interiorShellCollision(x.interior, c, ax, hx, hy, z0, z1, *x.col)) return;   // hollow shell
    CollisionBox b;
    b.c = vec3(c, (z0 + z1) * 0.5f);
    b.ax = ax;
    b.he = vec3(hx, hy, (z1 - z0) * 0.5f);
    x.col->push_back(b);
}

// Rooftop equipment: packaged HVAC units with fan shrouds, vent stacks with rain caps, stair bulkheads, and (older
// districts mostly) a wooden water tank with hoops and a conical roof on a braced steel stand
void rooftopClutter(Ctx& x, vec2 c, vec2 ax, float hx, float hy, float z, Rng& r, float tankChance = 0.3f) {
    if (!x.detail) return;
    vec2 ay = perp(ax);
    const u32 unitC = packRGBA8(0.75f, 0.75f, 0.73f, 1), grille = packRGBA8(0.12f, 0.12f, 0.13f, 1);
    const u32 metal = makeMat(MAT_METAL_PAINTED), brushed = makeMat(MAT_METAL_BRUSHED);
    vec3 X(ax, 0), Y(ay, 0), Z(0, 0, 1);
    int n = r.irange(1, 4 + (int)(hx * hy / 150.f));
    for (int i = 0; i < n && i < 10; i++) {
        vec2 p = c + ax * r.range(-hx * 0.75f, hx * 0.75f) + ay * r.range(-hy * 0.75f, hy * 0.75f);
        vec3 he(r.range(0.6f, 1.8f), r.range(0.6f, 1.4f), r.range(0.4f, 1.0f));
        int kind = r.irange(0, 3);
        if (kind <= 1) {
            x.m->box(vec3(p, z + he.z) - x.org, X, Y, Z, he, unitC, metal);
            int fans = he.x > 1.2f ? 2 : 1;
            float fr = Min(he.y, he.x / fans) * 0.72f;
            for (int f = 0; f < fans; f++) {
                vec2 fp = p + ax * (fans == 2 ? (f ? 0.5f : -0.5f) * he.x : 0.f);
                x.m->cylinder(vec3(fp, z + 2.f * he.z) - x.org, fr, fr, 0.22f, 8, unitC, metal, false);
                vec3 gc = vec3(fp, z + 2.f * he.z + 0.17f) - x.org, gx = X * (fr * 0.9f), gy = Y * (fr * 0.9f);
                x.m->quadFacing(gc - gx - gy, gc + gx - gy, gc + gx + gy, gc - gx + gy, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), grille, brushed, Z);
            }
        } else if (kind == 2) {
            int nv = r.irange(1, 3);
            for (int v = 0; v < nv; v++) {
                vec2 vp = p + ax * (v * 0.7f);
                float vh = r.range(0.7f, 1.6f);
                x.m->cylinder(vec3(vp, z) - x.org, 0.11f, 0.11f, vh, 6, packRGBA8(0.62f, 0.63f, 0.64f, 1), brushed, false);
                x.m->cylinder(vec3(vp, z + vh + 0.08f) - x.org, 0.24f, 0.02f, 0.18f, 6, packRGBA8(0.55f, 0.56f, 0.57f, 1), brushed, false);
            }
        } else {
            // stair bulkhead with a steel door
            vec3 bh(1.3f, 1.1f, 1.35f);
            x.m->box(vec3(p, z + bh.z) - x.org, X, Y, Z, bh, packRGBA8(0.82f, 0.8f, 0.76f, 1), makeMat(MAT_CONCRETE));
            vec3 d0 = vec3(p + ay * (bh.y + 0.01f) - ax * 0.45f, z) - x.org, d1 = vec3(p + ay * (bh.y + 0.01f) + ax * 0.45f, z) - x.org;
            x.m->quadFacing(d0, d1, d1 + vec3(0, 0, 2.05f), d0 + vec3(0, 0, 2.05f), vec2(0, 0), vec2(0.9f, 0), vec2(0.9f, 2.05f), vec2(0, 2.05f),
                            packRGBA8(0.35f, 0.38f, 0.42f, 1), metal, Y);
        }
    }
    if (r.chance(tankChance)) {
        vec2 p = c + ax * r.range(-hx * 0.5f, hx * 0.5f) + ay * r.range(-hy * 0.5f, hy * 0.5f);
        const u32 steel = packRGBA8(0.2f, 0.2f, 0.21f, 1), wood = packRGBA8(r.range(0.45f, 0.55f), 0.4f, 0.33f, 1);
        float sz = 2.2f;
        // braced stand and deck
        for (int k = 0; k < 4; k++) {
            vec2 lp = p + ax * ((k & 1) ? 1.15f : -1.15f) + ay * ((k & 2) ? 1.15f : -1.15f);
            x.m->box(vec3(lp, z + sz * 0.5f) - x.org, X, Y, Z, vec3(0.09f, 0.09f, sz * 0.5f), steel, metal);
        }
        for (int k = 0; k < 2; k++)
            x.m->box(vec3(p + (k ? ay : ax) * 0.f, z + sz * 0.45f) - x.org, k ? Y : X, k ? X : Y, Z, vec3(1.2f, 0.05f, 0.05f), steel, metal);
        x.m->cylinder(vec3(p, z + sz) - x.org, 1.95f, 1.95f, 0.14f, 10, steel, metal, false);
        // staved tank, iron hoops, conical roof, finial
        x.m->cylinder(vec3(p, z + sz + 0.14f) - x.org, 1.6f, 1.55f, 3.1f, 10, wood, makeMat(MAT_WOOD), false);
        for (int h = 0; h < 2; h++)
            x.m->cylinder(vec3(p, z + sz + 0.7f + h * 1.5f) - x.org, 1.63f, 1.63f, 0.07f, 10, steel, metal, false);
        x.m->cylinder(vec3(p, z + sz + 3.2f) - x.org, 1.78f, 0.05f, 1.05f, 10, wood, makeMat(MAT_ROOF_SHINGLE), false);
        x.m->box(vec3(p, z + sz + 4.35f) - x.org, X, Y, Z, vec3(0.06f, 0.06f, 0.14f), steel, metal);
        // ladder up the stand and tank side
        for (int k = -1; k <= 1; k += 2)
            x.m->box(vec3(p + ay * 1.72f + ax * (k * 0.22f), z + (sz + 3.2f) * 0.5f) - x.org, X, Y, Z, vec3(0.025f, 0.025f, (sz + 3.2f) * 0.5f), steel, metal);
    }
}

void recordMass(Ctx& x, const std::vector<vec2>& fp, float z0, float z1, float vBase, u8 kind, bool parapetOn, u32 facadeId = 0xffffffffu) {
    if (!x.masses) return;
    FacadeMass fm;
    fm.fp = fp;
    fm.z0 = z0;
    fm.z1 = z1;
    fm.vBase = vBase;
    fm.kind = kind;
    fm.parapet = parapetOn;
    fm.facade = facadeId;
    x.masses->push_back(std::move(fm));
}

// Mass with facade walls + flat roof + parapet + collision
void flatMass(Ctx& x, const std::vector<vec2>& fp, float z0, float z1, float vBase, u32 facadeId, float bay, bool parapetOn, u32 roofColor,
              u8 kind = FM_MAIN, float skirt = 3.f) {
    recordMass(x, fp, z0, z1, vBase, kind, parapetOn, facadeId);
    facadeWalls(x, fp, z0 - skirt, z1 + (parapetOn ? 1.0f : 0.f), vBase, facadeId, bay);
    flatRoof(x, fp, z1 + 0.02f, roofColor, makeMat(MAT_ROOF_GRAVEL));
    if (parapetOn && x.detail) parapet(x, fp, z1, 1.0f, 0.3f, packRGBA8(0.8f, 0.8f, 0.78f, 1), makeMat(MAT_CONCRETE));
}

// Roof antennas: lattice or pole masts with dishes and panel antennas, red obstruction lamp on the tallest
void rooftopAntennas(Ctx& x, vec2 c, vec2 ax, float hx, float hy, float z, Rng& r, bool beacon) {
    if (!x.detail) return;
    vec2 ay = perp(ax);
    vec3 X(ax, 0), Y(ay, 0), Z(0, 0, 1);
    const u32 steel = packRGBA8(0.62f, 0.63f, 0.65f, 1), white = packRGBA8(0.92f, 0.92f, 0.9f, 1), metal = makeMat(MAT_METAL_PAINTED);
    int n = r.irange(1, 3);
    float tallest = 0.f;
    vec2 tallP = c;
    for (int i = 0; i < n; i++) {
        vec2 p = c + ax * r.range(-hx * 0.6f, hx * 0.6f) + ay * r.range(-hy * 0.6f, hy * 0.6f);
        float h = r.range(4.f, 11.f);
        if (r.chance(0.5f)) {
            // three-leg lattice mast
            for (int k = 0; k < 3; k++) {
                float a = kTwoPi * k / 3.f;
                vec2 lp = p + vec2(cosf(a), sinf(a)) * 0.35f;
                x.m->box(vec3(lp, z + h * 0.5f) - x.org, X, Y, Z, vec3(0.04f, 0.04f, h * 0.5f), steel, metal);
            }
            for (float zz = 1.2f; zz < h; zz += 1.4f) x.m->cylinder(vec3(p, z + zz) - x.org, 0.36f, 0.36f, 0.05f, 3, steel, metal, false);
        } else {
            x.m->cylinder(vec3(p, z) - x.org, 0.09f, 0.05f, h, 6, steel, metal, false);
        }
        // panel antennas and a dish
        for (int k = 0; k < 3; k++) {
            float a = kTwoPi * k / 3.f + r.f();
            vec2 d(cosf(a), sinf(a));
            x.m->box(vec3(p + d * 0.42f, z + h - 1.1f) - x.org, vec3(d, 0), vec3(perp(d), 0), Z, vec3(0.06f, 0.15f, 0.6f), white, metal, true);
        }
        if (r.chance(0.6f)) {
            vec2 d = normalize(ax * r.range(-1.f, 1.f) + ay * r.range(-1.f, 1.f) + vec2(0.01f, 0));
            vec3 dc = vec3(p + d * 0.55f, z + h * 0.6f);
            x.m->cylinder(dc - x.org - vec3(0, 0, 0.05f), 0.55f, 0.1f, 0.25f, 10, white, metal, true);
        }
        if (h > tallest) { tallest = h; tallP = p; }
    }
    if (beacon && x.lights && tallest > 0.f) {
        x.m->box(vec3(tallP, z + tallest + 0.1f) - x.org, X, Y, Z, vec3(0.1f), packRGBA8(1.f, 0.1f, 0.05f, 0.5f), makeMat(MAT_EMISSIVE, 1u), true);
        LightInstance li;
        li.pos = vec3(tallP, z + tallest + 0.3f);
        li.color = vec3(1, 0.1f, 0.05f) * 300.f;
        li.radius = 10.f;
        li.dir = vec3(0);
        li.cone = 0;
        li.type = 4;  // aviation beacon
        x.lights->push_back(li);
    }
}

// Helipad markings on a round pad: yellow ring, white H, perimeter lamps
void helipadMarks(Ctx& x, vec2 c, float rad, float z, vec2 ax) {
    if (!x.detail) return;
    vec2 ay = perp(ax);
    const u32 yellow = packRGBA8(0.95f, 0.78f, 0.1f, 1), white = packRGBA8(0.95f, 0.95f, 0.95f, 1), paint = makeMat(MAT_PAINT_WHITE);
    const int seg = 24;
    float r0 = rad * 0.72f, r1 = rad * 0.78f;
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec2 d0(cosf(a0), sinf(a0)), d1(cosf(a1), sinf(a1));
        x.m->quadFacing(vec3(c + d0 * r0, z) - x.org, vec3(c + d1 * r0, z) - x.org, vec3(c + d1 * r1, z) - x.org, vec3(c + d0 * r1, z) - x.org, vec2(0, 0),
                        vec2(1, 0), vec2(1, 1), vec2(0, 1), yellow, paint, vec3(0, 0, 1));
    }
    float hs = rad * 0.32f, bw = rad * 0.07f;
    vec3 X(ax, 0), Y(ay, 0), Z(0, 0, 1);
    for (int s = -1; s <= 1; s += 2) x.m->box(vec3(c + ax * (s * hs * 0.6f), z) - x.org, X, Y, Z, vec3(bw, hs, 0.01f), white, paint);
    x.m->box(vec3(c, z) - x.org, X, Y, Z, vec3(hs * 0.6f, bw, 0.01f), white, paint);
    for (int k = 0; k < 8; k++) {
        float a = kTwoPi * k / 8.f;
        vec2 p = c + vec2(cosf(a), sinf(a)) * (rad * 0.93f);
        x.m->box(vec3(p, z + 0.06f) - x.org, X, Y, Z, vec3(0.08f, 0.08f, 0.06f), packRGBA8(0.3f, 1.f, 0.4f, 0.35f), makeMat(MAT_EMISSIVE, 6u), true);
    }
}

void awning(Ctx& x, vec2 a, vec2 b, vec2 out, float z, float depth, u32 color) {
    vec3 p0 = vec3(a, z) - x.org, p1 = vec3(b, z) - x.org;
    vec3 q0 = vec3(a + out * depth, z - 0.7f) - x.org, q1 = vec3(b + out * depth, z - 0.7f) - x.org;
    x.m->quadFacing(p0, p1, q1, q0, vec2(0, 0), vec2(length(b - a), 0), vec2(length(b - a), depth), vec2(0, depth), color, makeMat(MAT_FABRIC), vec3(out, 1.f));
    x.m->quadFacing(p0, q0, q1, p1, vec2(0, 0), vec2(depth, 0), vec2(depth, length(b - a)), vec2(0, length(b - a)), color, makeMat(MAT_FABRIC), vec3(-out, -1.f));
    // valance
    x.m->quadFacing(q0, q1, q1 - vec3(0, 0, 0.35f), q0 - vec3(0, 0, 0.35f), vec2(0, 0), vec2(length(b - a), 0), vec2(length(b - a), 0.35f), vec2(0, 0.35f), color,
                    makeMat(MAT_FABRIC), vec3(out, 0));
}

}  // namespace buildmesh_detail

using namespace buildmesh_detail;

// facadedetail.cpp
void buildFacadeDetail(const Building& b, const FacadeGPU& fac, const WorldMap& map, vec3 org, MeshData& m, std::vector<CollisionBox>* col,
                       std::vector<PropInstance>* props, std::vector<LightInstance>* lights, const std::vector<FacadeMass>& masses);

void buildBuildingMesh(const Building& b, const FacadeGPU& fac, const WorldMap& map, bool detail, vec3 org, MeshData& m,
                       std::vector<CollisionBox>* col, std::vector<PropInstance>* props, std::vector<LightInstance>* lights) {
    Ctx x;
    x.m = &m;
    x.org = org;
    x.detail = detail;
    x.col = col;
    x.props = props;
    x.lights = lights;
    x.interior = b.interior;
    if (b.interior >= 0 && interiorOwnsShell(b.interior)) return;   // the whole structure streams with its interior
    thread_local std::vector<FacadeMass> masses;
    masses.clear();
    x.masses = detail ? &masses : nullptr;
    Rng r(b.seed ^ 0xB111D1u);
    vec2 ay = perp(b.ax);
    float z0 = b.baseZ;
    float bay = fac.bayW;
    u32 roofGray = packRGBA8(0.9f, 0.9f, 0.88f, 1);
    switch ((BuildingStyle)b.style) {
        case BS_TOWER: {
            int podiumFloors = Min((int)b.floors, r.irange(2, 6));
            float podH = fac.groundH + (podiumFloors - 1) * fac.floorH;
            bool hasPodium = b.floors > podiumFloors + 3 && r.chance(0.75f);
            float topZ = z0 + b.height;
            // massing variety (separate stream so the far LOD builds the same silhouette): stone podium under a curtain wall,
            // setbacks on the street side only (stepped profile), extra tiers on mid-height towers
            Rng mr(b.seed ^ 0x3A55E7u);
            bool mixed = hasPodium && b.facade2 != 0xffffffffu && b.interior < 0 && gBuildings && b.facade2 < gBuildings->facades.size();
            bool stepped = mr.chance(0.45f);
            if (hasPodium) {
                auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
                u32 pf = mixed ? b.facade2 : b.facade;
                flatMass(x, fp, z0, z0 + podH, z0, pf, mixed ? gBuildings->facades[pf].bayW : bay, true, roofGray, FM_PODIUM);
                addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, z0 + podH);
            }
            float inset = hasPodium ? r.range(2.5f, Min(9.f, Min(b.hx, b.hy) * 0.35f)) : 0.f;
            float thx = b.hx - inset, thy = b.hy - inset;
            vec2 tc = b.c + b.front * (hasPodium ? -r.range(0.f, inset * 0.6f) : 0.f);
            int shape = r.irange(0, 9);
            float towerBase = hasPodium ? z0 + podH : z0;
            // Setback tiers
            int tiers = b.floors > 35 ? r.irange(1, 3) : 1;
            if (tiers == 1 && b.floors > 20 && mr.chance(0.5f)) tiers = 2;
            float remaining = topZ - towerBase;
            float zc = towerBase;
            for (int t = 0; t < tiers; t++) {
                float h = (t == tiers - 1) ? (topZ - zc) : remaining * r.range(0.45f, 0.65f);
                remaining -= h;
                std::vector<vec2> fp;
                if (shape <= 4) fp = rectFP(tc, b.ax, thx, thy);
                else if (shape <= 7) fp = chamferFP(tc, b.ax, thx, thy, Min(thx, thy) * r.range(0.2f, 0.4f));
                else fp = roundFP(tc, Min(thx, thy), 20, atan2f(b.ax.y, b.ax.x));
                flatMass(x, fp, zc, zc + h, z0, b.facade, bay, true, roofGray, hasPodium || t > 0 ? FM_TIER : FM_MAIN);
                addCollision(x, tc, b.ax, thx, thy, zc - (t == 0 && !hasPodium ? 3.f : 0.f), zc + h);
                zc += h;
                float sx = r.range(0.72f, 0.88f), sy = r.range(0.72f, 0.88f);
                if (stepped) {
                    // keep the width, step back from the street: the rear face stays flush
                    float ny = thy * sy;
                    tc = tc - b.front * (thy - ny);
                    thy = ny;
                    thx *= mr.range(0.92f, 1.f);
                } else {
                    thx *= sx;
                    thy *= sy;
                }
            }
            // Crown
            int crown = r.irange(0, 5);
            if (crown <= 1) {
                // mechanical penthouse
                auto fp = rectFP(tc, b.ax, thx * 0.55f, thy * 0.55f);
                plainWalls(x, fp, zc, zc + 4.5f, packRGBA8(0.7f, 0.7f, 0.68f, 1), makeMat(MAT_CONCRETE_PANEL));
                flatRoof(x, fp, zc + 4.5f, roofGray, makeMat(MAT_ROOF_GRAVEL));
                rooftopClutter(x, tc, b.ax, thx * 0.9f, thy * 0.9f, zc, r);
                if (mr.chance(0.55f)) rooftopAntennas(x, tc, b.ax, thx * 0.4f, thy * 0.4f, zc + 4.5f, mr, true);
            } else if (crown == 2) {
                // spire
                m.cylinder(vec3(tc, zc) - org, 0.9f, 0.08f, r.range(15.f, 45.f), 8, packRGBA8(0.7f, 0.72f, 0.75f, 1), makeMat(MAT_METAL_BRUSHED), false);
                if (lights) {
                    LightInstance li;
                    li.pos = vec3(tc, zc + 20.f);
                    li.color = vec3(1, 0.1f, 0.05f) * 500.f;
                    li.radius = 12.f;
                    li.dir = vec3(0);
                    li.cone = 0;
                    li.type = 4;  // aviation beacon
                    lights->push_back(li);
                }
            } else if (crown == 3) {
                // helipad
                auto fp = roundFP(tc, Min(thx, thy) * 0.7f, 16, 0.f);
                flatRoof(x, fp, zc + 0.35f, packRGBA8(0.25f, 0.25f, 0.27f, 1), makeMat(MAT_CONCRETE));
                plainWalls(x, fp, zc, zc + 0.35f, kWhite, makeMat(MAT_CONCRETE));
                helipadMarks(x, tc, Min(thx, thy) * 0.7f, zc + 0.37f, b.ax);
            } else if (crown == 4 && detail) {
                // glass lantern top with emissive crown lights
                auto fp = rectFP(tc, b.ax, thx * 0.8f, thy * 0.8f);
                facadeWalls(x, fp, zc, zc + 6.f, z0, b.facade, bay);
                flatRoof(x, fp, zc + 6.f, roofGray, makeMat(MAT_ROOF_GRAVEL));
                auto rim = rectFP(tc, b.ax, thx * 0.82f, thy * 0.82f);
                float hue = r.f();
                vec3 lc = hsvToRgb(hue, 0.6f, 1.f);
                plainWalls(x, rim, zc + 6.f, zc + 6.3f, packRGBA8(lc.x, lc.y, lc.z, 0.6f), makeMat(MAT_EMISSIVE));
            } else {
                rooftopClutter(x, tc, b.ax, thx, thy, zc, r);
                if (mr.chance(0.3f)) rooftopAntennas(x, tc, b.ax, thx * 0.5f, thy * 0.5f, zc, mr, true);
            }
            break;
        }
        case BS_MIDRISE:
        case BS_CONDO:
        case BS_GARAGE: {
            auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
            // garages whose roof is an open parking deck (airport) get parapet, ramp and deck furniture from the site generator
            bool deck = b.style == BS_GARAGE && gSites && gSites->roofDeckAt(b.c, z0 + b.height);
            // midrise massing: storefront base band in its own cladding, top floors set back from the street behind a terrace
            Rng mr(b.seed ^ 0x3A55E7u);
            bool plainMid = b.style == BS_MIDRISE && b.interior < 0;
            bool mixed = plainMid && b.facade2 != 0xffffffffu && gBuildings && b.facade2 < gBuildings->facades.size() && b.floors >= 2;
            int topFloors = (plainMid && b.floors >= 6 && mr.chance(0.4f)) ? mr.irange(1, 2) : 0;
            float setD = topFloors ? Min(mr.range(2.2f, 3.8f), b.hy * 0.4f) : 0.f;
            float zTop = z0 + b.height;
            float zSet = topFloors ? z0 + fac.groundH + (b.floors - 1 - topFloors) * fac.floorH : zTop;
            float zBase = z0 + fac.groundH;
            vec2 roofC = b.c;
            float roofHy = b.hy;
            if (!mixed && !topFloors) {
                flatMass(x, fp, z0, zTop, z0, b.facade, bay, !deck, roofGray);
                addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, zTop);
            } else {
                float zLow = z0;
                if (mixed) {
                    u32 f2 = b.facade2;
                    facadeWalls(x, fp, z0 - 3.f, zBase, z0, f2, gBuildings->facades[f2].bayW);
                    recordMass(x, fp, z0, zBase, z0, FM_PODIUM, false, f2);
                    zLow = zBase;
                }
                // body (full footprint) up to the setback or the roof
                recordMass(x, fp, zLow, zSet, z0, FM_MAIN, true, b.facade);
                facadeWalls(x, fp, zLow - (mixed ? 0.f : 3.f), zSet + 1.f, z0, b.facade, bay);
                flatRoof(x, fp, zSet + 0.02f, roofGray, makeMat(MAT_ROOF_GRAVEL));
                if (detail) parapet(x, fp, zSet, 1.0f, 0.3f, packRGBA8(0.8f, 0.8f, 0.78f, 1), makeMat(MAT_CONCRETE));
                addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, zSet);
                if (topFloors) {
                    roofC = b.c - b.front * (setD * 0.5f);
                    roofHy = b.hy - setD * 0.5f;
                    auto tfp = rectFP(roofC, b.ax, b.hx, roofHy);
                    flatMass(x, tfp, zSet, zTop, z0, b.facade, bay, true, roofGray, FM_TIER, 0.f);
                    addCollision(x, roofC, b.ax, b.hx, roofHy, zSet, zTop);
                }
            }
            if (!deck) {
                bool oldFabric = b.region == REG_CALLE_LUNA || b.region == REG_NORTH_CITY || b.region == REG_MIDTOWN || b.region == REG_FLATS;
                rooftopClutter(x, roofC, b.ax, b.hx, roofHy, zTop, r, b.style == BS_MIDRISE && oldFabric ? 0.4f : 0.12f);
                if (b.style != BS_GARAGE && mr.chance(b.style == BS_CONDO ? 0.15f : 0.25f)) rooftopAntennas(x, roofC, b.ax, b.hx * 0.6f, roofHy * 0.6f, zTop, mr, false);
            }
            if (b.style == BS_CONDO && detail) {
                // balcony slabs on the front and back facades
                for (int f = 1; f < b.floors; f++) {
                    float z = z0 + fac.groundH + (f - 1) * fac.floorH;
                    for (int s = -1; s <= 1; s += 2) {
                        vec2 fc = b.c + b.front * (s * (b.hy + 0.8f));
                        m.box(vec3(fc, z) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx * 0.92f, 0.8f, 0.12f), kWhite, makeMat(MAT_CONCRETE), true);
                        // glass railing
                        vec2 rc = b.c + b.front * (s * (b.hy + 1.55f));
                        m.box(vec3(rc, z + 0.6f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx * 0.92f, 0.03f, 0.5f),
                              packRGBA8(0.6f, 0.75f, 0.8f, 1), makeMat(MAT_GLASS));
                    }
                }
            }
            break;
        }
        case BS_DECO: {
            // Main block + taller central tower element facing the street, eyebrow ledges, sign fin
            auto fp = chamferFP(b.c, b.ax, b.hx, b.hy, r.chance(0.5f) ? 2.5f : 0.1f);
            flatMass(x, fp, z0, z0 + b.height, z0, b.facade, bay, true, roofGray);
            addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, z0 + b.height);
            float tw = Min(b.hx * 0.3f, 5.f);
            vec2 tcen = b.c + b.front * (b.hy - 1.5f);
            auto tfp = rectFP(tcen, b.ax, tw, 1.8f);
            float th = b.height + r.range(3.f, 7.f);
            facadeWalls(x, tfp, z0 + b.height - 0.5f, z0 + th, z0, b.facade, bay);
            flatRoof(x, tfp, z0 + th, roofGray, makeMat(MAT_CONCRETE));
            recordMass(x, tfp, z0 + b.height - 0.5f, z0 + th, z0, FM_DECO_TOWER, false);
            if (detail) {
                // (the vertical neon blade sign on the tower is built by the street-level detail pass)
                // eyebrow ledges every floor on the front
                for (int f = 1; f <= b.floors; f++) {
                    float z = z0 + fac.groundH + (f - 1) * fac.floorH - 0.35f;
                    vec2 lc = b.c + b.front * (b.hy + 0.35f);
                    m.box(vec3(lc, z) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx * 0.95f, 0.35f, 0.07f), kWhite, makeMat(MAT_PLASTER), true);
                }
                // awning over the entrance
                vec2 a0 = b.c + b.front * (b.hy + 0.05f) - b.ax * (b.hx * 0.6f), a1 = b.c + b.front * (b.hy + 0.05f) + b.ax * (b.hx * 0.6f);
                vec3 ac = hsvToRgb(r.f(), 0.5f, 0.9f);
                awning(x, a1, a0, b.front, z0 + 3.4f, 2.2f, packRGBA8(ac.x, ac.y, ac.z, 1));
            }
            break;
        }
        case BS_SHOPS:
        case BS_MOTEL:
        case BS_CHURCH: {
            auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
            bool flat = b.roof == ROOF_FLAT;
            if (flat) flatMass(x, fp, z0, z0 + b.height, z0, b.facade, bay, true, roofGray);
            else {
                recordMass(x, fp, z0, z0 + b.height, z0, FM_MAIN, false);
                facadeWalls(x, fp, z0 - 3.f, z0 + b.height, z0, b.facade, bay);
                pitchedRoof(x, b.c, b.ax, b.hx, b.hy, z0 + b.height, 0.55f, false, 0.4f, packRGBA8(0.35f, 0.35f, 0.36f, 1), makeMat(MAT_ROOF_SHINGLE),
                            kWhite, makeMat(MAT_PLASTER));
            }
            addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, z0 + b.height);
            if (b.style == BS_CHURCH) {
                // steeple
                vec2 sc = b.c + b.front * (b.hy - 2.5f);
                auto sfp = rectFP(sc, b.ax, 2.2f, 2.2f);
                plainWalls(x, sfp, z0 + b.height, z0 + b.height + 9.f, kWhite, makeMat(MAT_PLASTER));
                pitchedRoof(x, sc, b.ax, 2.2f, 2.2f, z0 + b.height + 9.f, 2.4f, true, 0.1f, packRGBA8(0.3f, 0.3f, 0.32f, 1), makeMat(MAT_ROOF_METAL), kWhite,
                            makeMat(MAT_PLASTER));
            }
            // (shop awnings, security gates and signs are part of the street-level detail pass)
            if (detail && b.style == BS_SHOPS) rooftopClutter(x, b.c, b.ax, b.hx, b.hy, z0 + b.height, r);
            if (b.style == BS_SHOPS && flat) {
                // false front: a raised, stepped parapet centred on the street face (older main streets)
                Rng mr(b.seed ^ 0x3A55E7u);
                bool oldMain = b.region == REG_CALLE_LUNA || b.region == REG_KEY_TOWN || b.region == REG_LAKE_TOWN || b.region == REG_HARLOW ||
                               b.region == REG_NORTH_CITY || b.region == REG_FLATS || b.region == REG_FORT_CASTELL;
                if (oldMain && b.hx > 3.5f && mr.chance(0.45f)) {
                    float w = b.hx * mr.range(0.35f, 0.7f), hgt = mr.range(0.8f, 1.8f);
                    vec2 fc = b.c + b.front * (b.hy - 0.15f);
                    u32 pc = packRGBA8(0.93f, 0.92f, 0.88f, 1);
                    float zt = z0 + b.height + 1.f;
                    m.box(vec3(fc, zt + hgt * 0.5f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(w, 0.15f, hgt * 0.5f), pc, makeMat(MAT_PLASTER), false);
                    if (mr.chance(0.6f))
                        m.box(vec3(fc, zt + hgt + 0.3f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(w * 0.45f, 0.15f, 0.3f), pc, makeMat(MAT_PLASTER), false);
                    m.box(vec3(fc + b.front * 0.02f, zt + hgt + 0.02f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(w + 0.08f, 0.2f, 0.05f), pc,
                          makeMat(MAT_CONCRETE), false);
                }
            }
            if (detail && b.style == BS_MOTEL) {
                // walkway slab + railing along the front at the 2nd floor
                vec2 wc = b.c + b.front * (b.hy + 1.1f);
                m.box(vec3(wc, z0 + fac.groundH - 0.1f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx, 1.1f, 0.12f), kWhite, makeMat(MAT_CONCRETE), true);
                m.box(vec3(b.c + b.front * (b.hy + 2.15f), z0 + fac.groundH + 0.45f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1),
                      vec3(b.hx, 0.04f, 0.5f), packRGBA8(0.9f, 0.9f, 0.9f, 1), makeMat(MAT_METAL_PAINTED));
                for (float u = -b.hx + 1.f; u <= b.hx; u += 4.f) {
                    vec2 pc = b.c + b.front * (b.hy + 2.05f) + b.ax * u;
                    m.box(vec3(pc, z0 + fac.groundH * 0.5f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(0.1f, 0.1f, fac.groundH * 0.5f), kWhite,
                          makeMat(MAT_METAL_PAINTED));
                }
                // tall motel sign
                vec2 sp = b.lotC + b.front * (b.lotHy - 1.5f) + b.ax * (b.hx * 0.8f);
                m.cylinder(vec3(sp, z0) - org, 0.25f, 0.25f, 7.f, 8, kWhite, makeMat(MAT_METAL_PAINTED), false);
                vec3 nc = hsvToRgb(r.f(), 0.8f, 1.f);
                // backlit sign box, lit at night at a lightbox level
                m.box(vec3(sp, z0 + 8.2f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(2.2f, 0.25f, 1.3f), packRGBA8(nc.x, nc.y, nc.z, 0.22f),
                      makeMat(MAT_EMISSIVE, 6u), true);
            }
            break;
        }
        case BS_STRIPMALL:
        case BS_GASSTATION: {
            auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
            flatMass(x, fp, z0, z0 + b.height, z0, b.facade, bay, true, roofGray);
            addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, z0 + b.height);
            // Parking lot / forecourt between building and street
            vec2 lotFront = b.lotC + b.front * b.lotHy;
            vec2 pc = (lotFront + (b.c + b.front * b.hy)) * 0.5f;
            float pd = length(lotFront - (b.c + b.front * b.hy)) * 0.5f;
            if (pd > 2.f) {
                auto pfp = rectFP(pc, b.ax, b.hx + 1.f, pd);
                std::vector<vec3> poly;
                for (auto& p : pfp) poly.push_back(vec3(p, z0 - 0.1f) - org);
                m.polygon(poly, vec3(0, 0, 1), kWhite, makeMat(MAT_ASPHALT_OLD), 1.f);
                plainWalls(x, pfp, z0 - 1.f, z0 - 0.1f, kWhite, makeMat(MAT_CONCRETE));
            }
            if (b.style == BS_STRIPMALL && detail) {
                // Covered walkway canopy with columns
                vec2 cc = b.c + b.front * (b.hy + 1.6f);
                m.box(vec3(cc, z0 + 3.6f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx, 1.6f, 0.25f), packRGBA8(0.92f, 0.9f, 0.86f, 1),
                      makeMat(MAT_PLASTER), true);
                for (float u = -b.hx + 1.f; u <= b.hx; u += 6.f)
                    m.box(vec3(b.c + b.front * (b.hy + 3.f) + b.ax * u, z0 + 1.7f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(0.2f, 0.2f, 1.8f),
                          kWhite, makeMat(MAT_PLASTER));
                // parking stall lines
                if (pd > 5.f) {
                    for (float u = -b.hx + 2.f; u < b.hx - 1.f; u += 2.7f) {
                        vec2 s0 = pc + b.ax * u - b.front * (pd - 1.f), s1 = s0 + b.front * 5.f;
                        vec2 w = b.ax * 0.06f;
                        m.quadFacing(vec3(s0 - w, z0 - 0.08f) - org, vec3(s0 + w, z0 - 0.08f) - org, vec3(s1 + w, z0 - 0.08f) - org, vec3(s1 - w, z0 - 0.08f) - org,
                                     vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), kWhite, makeMat(MAT_PAINT_WHITE), vec3(0, 0, 1));
                    }
                }
            }
            if (b.style == BS_GASSTATION && detail) {
                // Canopy over pumps
                vec2 cc = pc;
                float cw = Min(b.hx * 2.2f, 16.f), cd = Min(pd * 0.8f, 9.f);
                m.box(vec3(cc, z0 + 5.2f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(cw, cd, 0.45f), kWhite, makeMat(MAT_METAL_PAINTED), true);
                vec3 bc = hsvToRgb(r.f(), 0.8f, 0.9f);
                auto rim = rectFP(cc, b.ax, cw + 0.02f, cd + 0.02f);
                plainWalls(x, rim, z0 + 4.8f, z0 + 5.3f, packRGBA8(bc.x, bc.y, bc.z, 0.15f), makeMat(MAT_EMISSIVE, 6u));
                for (int k = -1; k <= 1; k += 2)
                    for (int j = -1; j <= 1; j += 2) {
                        vec2 cp = cc + b.ax * (k * cw * 0.6f) + b.front * (j * cd * 0.45f);
                        m.box(vec3(cp, z0 + 2.4f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(0.25f, 0.25f, 2.4f), kWhite, makeMat(MAT_METAL_PAINTED));
                        m.box(vec3(cp + b.ax * 1.2f, z0 + 0.8f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(0.5f, 0.3f, 0.8f), packRGBA8(bc.x, bc.y, bc.z, 1),
                              makeMat(MAT_METAL_PAINTED));
                    }
                if (lights)
                    for (int k = -1; k <= 1; k += 2) {
                        LightInstance li;
                        li.pos = vec3(cc + b.ax * (k * cw * 0.4f), z0 + 4.6f);
                        li.color = vec3(0.95f, 0.97f, 1.f) * 6000.f;
                        li.radius = 18.f;
                        li.dir = vec3(0, 0, -1);
                        li.cone = 0.1f;
                        li.type = 1;
                        lights->push_back(li);
                    }
            }
            break;
        }
        case BS_HOUSE:
        case BS_VILLA:
        case BS_FARMHOUSE:
        case BS_SHACK: {
            bool stilts = b.style == BS_SHACK;
            float zb = z0 + (stilts ? 2.2f : 0.f);
            auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
            recordMass(x, fp, zb, zb + b.height, zb, FM_HOUSE, false);
            facadeWalls(x, fp, zb - (stilts ? 0.3f : 2.f), zb + b.height, zb, b.facade, bay);
            addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 2.f, zb + b.height + 2.f);
            u32 roofMat, roofCol;
            if (b.style == BS_VILLA || (b.style == BS_HOUSE && (b.seed & 3) != 0)) {
                roofMat = makeMat(MAT_ROOF_TILE);
                float t = r.range(0.85f, 1.15f);
                roofCol = packRGBA8(t, t * r.range(0.9f, 1.05f), t, 1);
            } else if (b.style == BS_FARMHOUSE || b.style == BS_SHACK) {
                roofMat = makeMat(MAT_ROOF_METAL);
                roofCol = r.chance(0.5f) ? packRGBA8(0.9f, 0.9f, 0.92f, 1) : packRGBA8(0.55f, 0.25f, 0.2f, 1);
            } else {
                roofMat = makeMat(MAT_ROOF_SHINGLE);
                float g = r.range(0.7f, 1.3f);
                roofCol = packRGBA8(g, g * 0.95f, g * 0.9f, 1);
            }
            bool hip = b.roof == ROOF_HIP;
            float pitch = b.style == BS_VILLA ? 0.42f : r.range(0.35f, 0.6f);
            pitchedRoof(x, b.c, b.ax, b.hx, b.hy, zb + b.height, pitch, hip, 0.6f, roofCol, roofMat, packRGBA8(0.95f, 0.95f, 0.93f, 1),
                        makeMat(MAT_PLASTER));
            if (stilts) {
                for (int k = -1; k <= 1; k += 2)
                    for (int j = -1; j <= 1; j += 2)
                        m.box(vec3(b.c + b.ax * (k * (b.hx - 0.3f)) + ay * (j * (b.hy - 0.3f)), z0 + 0.6f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1),
                              vec3(0.15f, 0.15f, 2.f), kWhite, makeMat(MAT_WOOD));
                m.box(vec3(b.c, zb - 0.15f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx + 1.2f, b.hy + 1.2f, 0.12f), kWhite, makeMat(MAT_WOOD), true);
            }
            if (!detail) break;
            // Garage wing (suburban houses / villas)
            // (deterministic from the seed: facadedetail.cpp keeps hedges and garden walls clear of this driveway)
            bool garage = (b.style == BS_HOUSE && (b.seed % 10u) < 7u) || b.style == BS_VILLA;
            float side = (b.seed & 64u) ? 1.f : -1.f;
            if (garage) {
                float gw = 3.2f, gd = Min(b.hy, 3.4f);
                vec2 gc = b.c + b.ax * (side * (b.hx + gw)) + b.front * (b.hy - gd);
                auto gfp = rectFP(gc, b.ax, gw, gd);
                facadeWalls(x, gfp, z0 - 1.f, z0 + 2.9f, z0 + 100.f, b.facade, 20.f);
                pitchedRoof(x, gc, b.ax, gw, gd, z0 + 2.9f, 0.35f, true, 0.4f, roofCol, roofMat, kWhite, makeMat(MAT_PLASTER));
                addCollision(x, gc, b.ax, gw, gd, z0 - 1.f, z0 + 3.5f);
                // garage door
                vec2 d0 = gc + b.front * (gd + 0.02f) - b.ax * 2.4f, d1 = gc + b.front * (gd + 0.02f) + b.ax * 2.4f;
                m.quadFacing(vec3(d0, z0) - org, vec3(d1, z0) - org, vec3(d1, z0 + 2.2f) - org, vec3(d0, z0 + 2.2f) - org, vec2(0, 0), vec2(4.8f, 0),
                             vec2(4.8f, 2.2f), vec2(0, 2.2f), packRGBA8(0.92f, 0.92f, 0.9f, 1), makeMat(MAT_METAL_PAINTED), vec3(b.front, 0));
                // driveway to the street
                vec2 dA = gc + b.front * gd, dB = b.lotC + b.front * (b.lotHy + 1.5f);
                dB = dA + b.front * Max(1.f, dot(dB - dA, b.front));
                auto dfp = rectFP((dA + dB) * 0.5f, b.ax, 2.6f, length(dB - dA) * 0.5f);
                std::vector<vec3> poly;
                for (auto& p : dfp) poly.push_back(vec3(p, map.heightAt(p.x, p.y) + 0.06f) - org);
                m.polygon(poly, vec3(0, 0, 1), kWhite, makeMat(MAT_CONCRETE), 1.f);
            }
            // Back yard pool
            if ((b.style == BS_VILLA || (b.style == BS_HOUSE && r.chance(0.35f))) && b.region != REG_FARMLAND) {
                float backSpace = (b.lotHy * 2.f) - b.hy * 2.f - 6.f;
                if (backSpace > 6.f) {
                    vec2 pc = b.c - b.front * (b.hy + 2.f + Min(backSpace * 0.4f, 4.f)) + b.ax * r.range(-b.hx * 0.3f, b.hx * 0.3f);
                    float pw = r.range(2.5f, 4.5f), pdd = r.range(1.8f, 2.6f);
                    float pz = map.heightAt(pc.x, pc.y) + 0.1f;
                    // deck
                    auto deck = rectFP(pc, b.ax, pw + 1.5f, pdd + 1.5f);
                    auto pool = rectFP(pc, b.ax, pw, pdd);
                    std::vector<vec3> dpoly;
                    // deck as 4 strips around the pool
                    for (int k = 0; k < 4; k++) {
                        vec2 a0 = deck[k], a1 = deck[(k + 1) % 4], b0 = pool[k], b1 = pool[(k + 1) % 4];
                        m.quadFacing(vec3(a0, pz) - org, vec3(a1, pz) - org, vec3(b1, pz) - org, vec3(b0, pz) - org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1),
                                     kWhite, makeMat(MAT_PAVERS), vec3(0, 0, 1));
                        // pool walls (inside)
                        m.quadFacing(vec3(b0, pz) - org, vec3(b1, pz) - org, vec3(b1, pz - 1.6f) - org, vec3(b0, pz - 1.6f) - org, vec2(0, 0), vec2(length(b1 - b0), 0),
                                     vec2(length(b1 - b0), 1.6f), vec2(0, 1.6f), kWhite, makeMat(MAT_TILE_POOL), vec3(pc - (b0 + b1) * 0.5f, 0));
                    }
                    std::vector<vec3> floor;
                    for (auto& p : pool) floor.push_back(vec3(p, pz - 1.6f) - org);
                    m.polygon(floor, vec3(0, 0, 1), kWhite, makeMat(MAT_TILE_POOL), 1.f);
                    // water surface (emissive-free bright tile tinted); real water shading added by the water pass
                    std::vector<vec3> water;
                    for (auto& p : pool) water.push_back(vec3(p, pz - 0.15f) - org);
                    m.polygon(water, vec3(0, 0, 1), packRGBA8(0.35f, 0.75f, 0.9f, 1), makeMat(MAT_GLASS), 1.f);
                }
            }
            // Yard trees
            if (props) {
                int nt = r.irange(1, b.style == BS_VILLA ? 5 : 3);
                for (int k = 0; k < nt; k++) {
                    PropInstance pi;
                    vec2 p = b.lotC + b.ax * r.range(-b.hx * 1.1f, b.hx * 1.1f) + b.front * (r.chance(0.5f) ? r.range(b.hy + 1.5f, b.lotHy - 1.f) : -r.range(b.hy + 2.f, b.lotHy));
                    pi.pos = vec3(p, map.heightAt(p.x, p.y));
                    pi.yaw = r.f() * kTwoPi;
                    pi.scale = r.range(0.7f, 1.2f);
                    int reg = b.region;
                    pi.type = (reg == REG_FARMLAND || reg == REG_RIDGE) ? (r.chance(0.5f) ? PROP_TREE_OAK : PROP_TREE_PINE)
                                                                           : (r.chance(0.6f) ? PROP_PALM : (r.chance(0.5f) ? PROP_TREE_OAK : PROP_BUSH));
                    pi.variant = (u8)r.irange(0, 3);
                    pi.flags = 0;
                    props->push_back(pi);
                }
            }
            // Porch light
            if (lights && r.chance(0.6f)) {
                LightInstance li;
                li.pos = vec3(b.c + b.front * (b.hy + 0.4f), zb + 2.4f);
                li.color = vec3(1.f, 0.75f, 0.45f) * 400.f;
                li.radius = 8.f;
                li.dir = vec3(0);
                li.cone = 0;
                li.type = 1;
                lights->push_back(li);
            }
            break;
        }
        case BS_WAREHOUSE:
        case BS_FACTORY:
        case BS_BARN: {
            auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
            if (b.roof == ROOF_FLAT) flatMass(x, fp, z0, z0 + b.height, z0, b.facade, bay, b.style != BS_BARN, roofGray);
            else {
                recordMass(x, fp, z0, z0 + b.height, z0, FM_MAIN, false);
                facadeWalls(x, fp, z0 - 2.f, z0 + b.height, z0, b.facade, bay);
                u32 rc = b.style == BS_BARN ? packRGBA8(0.55f, 0.52f, 0.5f, 1) : packRGBA8(0.75f, 0.76f, 0.78f, 1);
                pitchedRoof(x, b.c, b.ax, b.hx, b.hy, z0 + b.height, b.roof == ROOF_BARREL ? 0.3f : 0.4f, false, 0.3f, rc, makeMat(MAT_ROOF_METAL), kWhite,
                            makeMat(b.style == BS_BARN ? MAT_WOOD_SIDING : MAT_CORRUGATED));
            }
            addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 2.f, z0 + b.height + 2.f);
            if (detail) {
                // Loading dock doors on the front
                int doors = Max(1, (int)(b.hx / 7.f));
                for (int k = 0; k < doors; k++) {
                    float u = -b.hx + (k + 0.5f) * (2.f * b.hx / doors);
                    if (b.interior >= 0 && interiorHidesLoadingDoor(b, u)) continue;   // a real door of the interior (interiors.cpp)
                    vec2 d0 = b.c + b.front * (b.hy + 0.03f) + b.ax * (u - 1.8f), d1 = d0 + b.ax * 3.6f;
                    m.quadFacing(vec3(d0, z0 + 0.2f) - org, vec3(d1, z0 + 0.2f) - org, vec3(d1, z0 + 4.2f) - org, vec3(d0, z0 + 4.2f) - org, vec2(0, 0), vec2(3.6f, 0),
                                 vec2(3.6f, 4.f), vec2(0, 4.f), packRGBA8(0.6f, 0.62f, 0.65f, 1), makeMat(MAT_CORRUGATED), vec3(b.front, 0));
                }
                if (b.style == BS_FACTORY) {
                    int nc = r.irange(1, 3);
                    for (int k = 0; k < nc; k++) {
                        vec2 cp = b.c + b.ax * r.range(-b.hx * 0.6f, b.hx * 0.6f) - b.front * r.range(0.f, b.hy * 0.5f);
                        m.cylinder(vec3(cp, z0 + b.height) - org, 1.4f, 1.0f, r.range(12.f, 30.f), 12, packRGBA8(0.55f, 0.4f, 0.35f, 1), makeMat(MAT_BRICK), false);
                    }
                    // storage tank beside the building
                    vec2 tp = b.lotC - b.front * (b.lotHy * 0.6f) + b.ax * (b.hx * 0.8f);
                    m.cylinder(vec3(tp, z0) - org, 4.5f, 4.5f, 9.f, 16, packRGBA8(0.85f, 0.85f, 0.85f, 1), makeMat(MAT_METAL_PAINTED), true);
                }
                rooftopClutter(x, b.c, b.ax, b.hx * 0.8f, b.hy * 0.8f, z0 + b.height, r);
            }
            break;
        }
        default: {
            auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
            flatMass(x, fp, z0, z0 + b.height, z0, b.facade, bay, true, roofGray);
            addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 3.f, z0 + b.height);
            break;
        }
    }
    // Street-level architectural detail and garden dressing (full-detail cells only)
    if (detail && !masses.empty()) buildFacadeDetail(b, fac, map, org, m, col, props, lights, masses);
}

}  // namespace World
