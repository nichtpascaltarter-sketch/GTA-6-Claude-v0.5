// Building mesh generation per cell (full detail and far LOD).
#include "buildings.h"
#include "sites.h"
#include "interiors.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace buildmesh_detail {

const u32 kWhite = 0xffffffffu;

// Ground around a house kept clear of its yard trees: porches, decks, stairs, the cistern, the pool deck, the garage wing
// and its driveway (centre, the two axes and the half extents along them); the house builders fill it, the yard trees read it
struct YardKeep {
    vec2 c, a, b;
    float ha, hb;
};

struct Ctx {
    MeshData* m;
    vec3 org;  // cell origin (world) subtracted from all positions
    bool detail;
    std::vector<CollisionBox>* col;
    std::vector<PropInstance>* props;
    std::vector<LightInstance>* lights;
    std::vector<FacadeMass>* masses = nullptr;  // facade masses for the street-level detail pass (LOD0)
    int interior = -1;  // enterable interior of the building (world/interiors.cpp): facade cut-outs, hollow collision
    u32 roofCol = 0xffe0e5e5u, roofMat = 0;      // flat roof finish of the building (roofFinish), set per building
    std::vector<YardKeep>* keep = nullptr;       // the house's yard keep-outs (YardKeep), per building
    std::vector<vec4>* holes = nullptr;          // paved ground the lawn leaves out (lawnHole), per building
    u32 trimMat = 0;                             // painted porch posts, rails and brackets (0: bare wood), per building
    int porchPosts = 0;                          // housePorch's posts: 0 slim, 1 Craftsman (column on a pier, knee wall), 2 round columns
    u32 pierCol = 0, pierMat = 0;                // the Craftsman porch's piers and knee wall (brick, stone or the wall's cladding)
};

// The material of a porch's posts, rails, brackets and ceiling: painted in town (plaster takes the trim colour: the wood
// texture browned the white trim), bare wood on the farmhouses and the stilt shacks
inline u32 trimWood(const Ctx& x) { return x.trimMat ? x.trimMat : makeMat(MAT_WOOD); }

inline void yardKeep(Ctx& x, vec2 c, vec2 a, vec2 b, float ha, float hb) {
    if (x.keep) x.keep->push_back({c, a, b, fabsf(ha), fabsf(hb)});
}
// Ground the house's lawn leaves out (facadedetail.cpp lawn()): the pool deck, the driveway, the carport floor. A rectangle
// on the building's axes: centre and the half extents along b.ax and across it. (The lawn lies 3.5 cm over the ground and
// used to cover the pool, which stood below it, and poke through the driveways on uneven ground.)
inline void lawnHole(Ctx& x, vec2 c, float hu, float hv) {
    if (x.holes) x.holes->push_back(vec4(c.x, c.y, fabsf(hu), fabsf(hv)));
}
inline bool yardKept(const Ctx& x, vec2 p, float margin) {
    if (!x.keep) return false;
    for (const YardKeep& k : *x.keep) {
        vec2 d = p - k.c;
        if (fabsf(dot(d, k.a)) < k.ha + margin && fabsf(dot(d, k.b)) < k.hb + margin) return true;
    }
    return false;
}

// Flat roof finish of a building: white or light-grey membrane, gravel ballast (grey to tan), silver coating or black
// tar - newer buildings mostly membrane, the older fabric gravel, tar and silver (seeded, same in every LOD)
void roofFinish(const Building& b, u32& col, u32& mat) {
    u32 h = hash32(b.seed ^ 0x500F7u);
    float p = hashToFloat(h), t = hashToFloat(hash32(h + 1u));
    bool old = (b.archFlags & ABF_OLD) != 0 || b.region == REG_CALLE_LUNA || b.region == REG_FLATS || b.region == REG_FORT_CASTELL;
    if (b.arch != AR_NONE && (b.archFlags & ABF_OLD) == 0) old = false;
    // (white cool roofs on the newer buildings, but not so many that the city reads washed out from the air)
    float pWhite = old ? 0.18f : 0.36f, pGravel = old ? 0.37f : 0.36f, pSilver = old ? 0.2f : 0.1f;
    if (p < pWhite) {
        float g = 0.88f + 0.12f * t;
        col = packRGBA8(g, g, g * 0.99f, 1);
        mat = makeMat(MAT_PLASTER);
    } else if (p < pWhite + pGravel) {
        vec3 c = lerp(vec3(0.78f, 0.78f, 0.76f), vec3(0.95f, 0.88f, 0.76f), t);
        col = packRGBA8(c.x, c.y, c.z, 1);
        mat = makeMat(MAT_ROOF_GRAVEL);
    } else if (p < pWhite + pGravel + pSilver) {
        float g = 0.8f + 0.15f * t;
        col = packRGBA8(g, g, g, 1);
        mat = makeMat(MAT_METAL_BRUSHED);
    } else {
        float g = 0.75f + 0.25f * t;
        col = packRGBA8(g, g, g, 1);
        mat = makeMat(MAT_ASPHALT_OLD);
    }
}

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
                 u32 gableColor, u32 gableMat, int ridge = -1) {
    vec2 ay = perp(ax);
    // ridge along the longer axis (ridge 0: along ax, 1: along perp(ax))
    bool alongX = ridge < 0 ? hx >= hy : ridge == 0;
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
    // what this roof collects (so neighbouring blocks differ): a mix weighted per building
    const float wAC = r.range(0.5f, 2.f), wVent = r.range(0.3f, 1.f), wSat = r.range(0.f, 0.8f), wSolar = hx * hy > 120.f ? r.range(0.f, 1.2f) : 0.f;
    const float wDuct = r.range(0.f, 0.8f), wSky = r.range(0.f, 0.7f), wMast = r.chance(0.12f) ? 1.f : 0.f;
    const float wSum = wAC + wVent + 0.35f + wSat + wSolar + wDuct + wSky + wMast;
    bool mastDone = false;
    for (int i = 0; i < n && i < 10; i++) {
        vec2 p = c + ax * r.range(-hx * 0.75f, hx * 0.75f) + ay * r.range(-hy * 0.75f, hy * 0.75f);
        vec3 he(r.range(0.6f, 1.8f), r.range(0.6f, 1.4f), r.range(0.4f, 1.0f));
        float pick = r.f() * wSum;
        int kind;
        if ((pick -= wAC) < 0.f) kind = r.chance(0.5f) ? 0 : 1;
        else if ((pick -= wVent) < 0.f) kind = 2;
        else if ((pick -= 0.35f) < 0.f) kind = 3;
        else if ((pick -= wSat) < 0.f) kind = 4;
        else if ((pick -= wSolar) < 0.f) kind = 5;
        else if ((pick -= wDuct) < 0.f) kind = 6;
        else if ((pick -= wSky) < 0.f) kind = 7;
        else kind = mastDone ? 0 : 8;
        if (kind == 4) {
            // satellite dish on a short pipe mount, aimed up toward the southern sky
            float dr = r.range(0.35f, 0.6f);
            vec3 base(p, z);
            x.m->box(base + vec3(0, 0, 0.05f) - x.org, X, Y, Z, vec3(0.25f, 0.25f, 0.05f), packRGBA8(0.5f, 0.5f, 0.5f, 1), makeMat(MAT_CONCRETE));
            x.m->cylinder(base - x.org, 0.04f, 0.04f, 0.9f, 6, packRGBA8(0.6f, 0.6f, 0.62f, 1), brushed, false);
            vec3 aim = normalize(vec3(0.f, -0.55f, 0.6f));
            vec3 dc = base + vec3(0, 0, 0.95f);
            vec3 du = normalize(cross(aim, vec3(1, 0, 0))), dv = cross(aim, du);
            u32 b0 = (u32)x.m->verts.size();
            u32 dishC = packRGBA8(0.88f, 0.88f, 0.86f, 1);
            x.m->addVertex(dc + aim * (dr * 0.3f) * -1.f - x.org, aim, du, vec2(0, 0), dishC, metal);
            for (int k = 0; k <= 12; k++) {
                float a = kTwoPi * k / 12;
                vec3 rp = dc + (du * cosf(a) + dv * sinf(a)) * dr;
                x.m->addVertex(rp - x.org, normalize(aim + (du * cosf(a) + dv * sinf(a)) * 0.4f), du, vec2(cosf(a), sinf(a)), dishC, metal);
            }
            for (int k = 0; k < 12; k++) {
                x.m->tri(b0, b0 + 1 + k, b0 + 2 + k);
                x.m->tri(b0, b0 + 2 + k, b0 + 1 + k);
            }
            x.m->box(dc + aim * (dr * 0.7f) - x.org, X, Y, Z, vec3(0.05f), packRGBA8(0.3f, 0.3f, 0.32f, 1), metal);
            continue;
        }
        if (kind == 5) {
            // solar array: rows of tilted panels on low frames, facing south
            int rows = r.irange(2, 4), cols = r.irange(3, 6);
            vec3 south(0, -1, 0), east(1, 0, 0);
            for (int rw = 0; rw < rows; rw++)
                for (int cl = 0; cl < cols; cl++) {
                    vec3 pc = vec3(p, z) + east * ((cl - cols * 0.5f) * 1.05f) - south * (rw * 2.2f);
                    if (fabsf(dot(pc.xy() - c, ax)) > hx * 0.9f || fabsf(dot(pc.xy() - c, ay)) > hy * 0.9f) continue;
                    vec3 lo = pc + vec3(0, 0, 0.35f) + south * 0.8f, hi = pc + vec3(0, 0, 1.15f) - south * 0.6f;
                    vec3 a0 = lo - east * 0.5f, a1 = lo + east * 0.5f, b1 = hi + east * 0.5f, b0 = hi - east * 0.5f;
                    x.m->quadFacing(a0 - x.org, a1 - x.org, b1 - x.org, b0 - x.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), packRGBA8(0.08f, 0.12f, 0.22f, 1),
                                    makeMat(MAT_GLASS), normalize(vec3(0, -0.6f, 0.8f)));
                    x.m->quadFacing(a0 - x.org, b0 - x.org, b1 - x.org, a1 - x.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), packRGBA8(0.7f, 0.7f, 0.7f, 1), brushed,
                                    normalize(vec3(0, 0.6f, -0.8f)));
                    x.m->box(pc + vec3(0, 0, 0.35f) - south * 0.2f - x.org, X, Y, Z, vec3(0.04f, 0.04f, 0.35f), packRGBA8(0.6f, 0.6f, 0.62f, 1), brushed);
                }
            continue;
        }
        if (kind == 6) {
            // HVAC duct run: a long rectangular duct on stands turning down into the roof
            float L = r.range(4.f, Min(12.f, hx * 1.4f));
            vec3 d = r.chance(0.5f) ? X : Y;
            vec3 a = vec3(p, z + 0.9f) - d * (L * 0.5f);
            u32 dc = packRGBA8(0.72f, 0.73f, 0.74f, 1);
            x.m->box(vec3(p, z + 0.9f) - x.org, d, normalize(cross(Z, d)), Z, vec3(L * 0.5f, 0.35f, 0.3f), dc, brushed);
            x.m->box(a + vec3(0, 0, -0.45f) - x.org, d, normalize(cross(Z, d)), Z, vec3(0.35f, 0.35f, 0.45f), dc, brushed);
            for (float s = 1.f; s < L; s += 2.5f) x.m->box(a + d * s + vec3(0, 0, -0.45f) - x.org, d, normalize(cross(Z, d)), Z, vec3(0.04f, 0.3f, 0.3f), packRGBA8(0.3f, 0.3f, 0.32f, 1), metal);
            continue;
        }
        if (kind == 7) {
            // skylight: glazed pyramid on an upstand
            vec3 sc(p, z);
            float s = r.range(0.8f, 1.6f);
            x.m->box(sc + vec3(0, 0, 0.2f) - x.org, X, Y, Z, vec3(s, s, 0.2f), packRGBA8(0.8f, 0.8f, 0.78f, 1), makeMat(MAT_CONCRETE));
            vec3 apex = sc + vec3(0, 0, 0.4f + s * 0.6f);
            for (int k = 0; k < 4; k++) {
                vec3 dA = (k == 0 ? X : (k == 1 ? Y : (k == 2 ? -X : -Y)));
                vec3 dB = (k == 0 ? Y : (k == 1 ? -X : (k == 2 ? -Y : X)));
                vec3 e0 = sc + vec3(0, 0, 0.4f) + (dA + dB) * s, e1 = sc + vec3(0, 0, 0.4f) + (dA - dB) * s;
                u32 bI = (u32)x.m->verts.size();
                vec3 nn = normalize(cross(e0 - e1, apex - e1));
                if (dot(nn, dA) < 0.f) nn = -nn;
                x.m->addVertex(e1 - x.org, nn, dB, vec2(0, 0), packRGBA8(0.25f, 0.35f, 0.4f, 1), makeMat(MAT_GLASS));
                x.m->addVertex(e0 - x.org, nn, dB, vec2(1, 0), packRGBA8(0.25f, 0.35f, 0.4f, 1), makeMat(MAT_GLASS));
                x.m->addVertex(apex - x.org, nn, dB, vec2(0.5f, 1), packRGBA8(0.25f, 0.35f, 0.4f, 1), makeMat(MAT_GLASS));
                vec3 fn = cross(x.m->verts[bI + 1].pos - x.m->verts[bI].pos, x.m->verts[bI + 2].pos - x.m->verts[bI].pos);
                if (dot(fn, nn) > 0.f) x.m->tri(bI, bI + 1, bI + 2);
                else x.m->tri(bI, bI + 2, bI + 1);
            }
            continue;
        }
        if (kind == 8) {
            // antenna mast: steel pole with cross arms, whips and a red beacon, guyed to the roof
            mastDone = true;
            float H = r.range(6.f, 11.f);
            u32 st = packRGBA8(0.55f, 0.56f, 0.58f, 1);
            x.m->cylinder(vec3(p, z) - x.org, 0.08f, 0.05f, H, 6, st, brushed, false);
            for (int k = 0; k < 3; k++) {
                float zz = z + H * (0.55f + 0.18f * k);
                x.m->box(vec3(p, zz) - x.org, k & 1 ? X : Y, k & 1 ? Y : -X, Z, vec3(0.6f - k * 0.12f, 0.025f, 0.025f), st, brushed);
                x.m->cylinder(vec3(p + (k & 1 ? ax : ay) * (0.55f - k * 0.12f), zz) - x.org, 0.012f, 0.008f, 1.2f, 4, st, brushed, false);
            }
            x.m->box(vec3(p, z + H + 0.08f) - x.org, X, Y, Z, vec3(0.08f), packRGBA8(1.f, 0.1f, 0.05f, 0.9f), makeMat(MAT_EMISSIVE, 1u | (7u << 4)));
            for (int k = 0; k < 3; k++) {
                float a = kTwoPi * k / 3.f + 0.4f;
                vec2 g2 = p + (ax * cosf(a) + ay * sinf(a)) * Min(H * 0.4f, Min(hx, hy) * 0.7f);
                vec3 top(p, z + H * 0.7f), bot(g2, z);
                vec3 dd = normalize(bot - top);
                x.m->box((top + bot) * 0.5f - x.org, dd, normalize(anyPerp(dd)), normalize(cross(dd, normalize(anyPerp(dd)))), vec3(length(bot - top) * 0.5f, 0.008f, 0.008f),
                         packRGBA8(0.35f, 0.35f, 0.37f, 1), metal);
            }
            continue;
        }
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
    (void)roofColor;
    flatRoof(x, fp, z1 + 0.02f, x.roofCol, x.roofMat ? x.roofMat : makeMat(MAT_ROOF_GRAVEL));
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

// The parking court of a strip mall or the forecourt of a gas station, between its front and the street (pc: the
// court's centre, pd: its half depth toward the street). Usually asphalt on a grid that follows the ground - the parked
// cars (population.cpp) and the shoppers stand on the ground - with a raised walk 1 m deep along the shopfronts (its
// curb face to the asphalt, walkable when the step is low). Where the building stands well above the ground in front
// (lifted to an embankment road's level) the court is a level plateau at the shop floors instead, walled down to the
// ground and solid underfoot. Stall lines 2.7 m apart, 1 to 6 m out from the shopfronts. Returns the plateau's height,
// or -1e9 when the court follows the ground (courtFoot() gives the surface under a canopy support or a pump).
float stripCourt(Ctx& x, const Building& b, vec2 pc, float pd, bool lines) {
    const WorldMap& map = *gMap;
    const vec2 ay = perp(b.ax);
    const vec3 X(b.ax, 0.f), Y(ay, 0.f), Z(0, 0, 1);
    const float hxC = b.hx + 1.f, walk = 1.f, zw = b.baseZ - 0.08f;
    const vec2 f0 = b.c + b.front * b.hy;   // the shopfronts' line (the court's back edge)
    const float depth = 2.f * pd;
    const u32 am = makeMat(MAT_ASPHALT_OLD), paint = makeMat(MAT_PAINT_WHITE);
    // the ground along the walk's edge: a low curb, or a building lifted well above the ground
    float gLo = 1e9f, gMin = 1e9f;
    for (int i = 0; i <= 8; i++) {
        vec2 p = f0 + b.ax * (-hxC + 2.f * hxC * i / 8.f) + b.front * walk;
        gLo = Min(gLo, map.heightAt(p.x, p.y));
    }
    for (int j = 0; j <= 4; j++)
        for (int i = 0; i <= 4; i++) {
            vec2 p = f0 + b.ax * (-hxC + 2.f * hxC * i / 4.f) + b.front * (depth * j / 4.f);
            gMin = Min(gMin, map.heightAt(p.x, p.y));
        }
    if (zw - (gLo + 0.05f) > 0.5f) {
        // the plateau: one slab at the walk's level over the whole court, walls to the ground, a collider under it
        const float zp = b.baseZ - 0.1f;
        auto pfp = rectFP(pc, b.ax, hxC, pd);
        std::vector<vec3> poly;
        for (auto& p : pfp) poly.push_back(vec3(p, zp) - x.org);
        x.m->polygon(poly, vec3(0, 0, 1), kWhite, am, 1.f);
        plainWalls(x, pfp, gMin - 0.3f, zp, packRGBA8(0.85f, 0.84f, 0.8f, 1), makeMat(MAT_CONCRETE));
        addCollision(x, pc, b.ax, hxC, pd, gMin - 0.5f, zp);
        if (lines && x.detail && pd > 5.f)
            for (float u = -b.hx + 2.f; u < b.hx - 1.f; u += 2.7f) {
                vec2 s0 = f0 + b.ax * u + b.front * (walk + 0.05f), s1 = s0 + b.front * 5.f;
                vec2 w = b.ax * 0.06f;
                x.m->quadFacing(vec3(s0 - w, zp + 0.02f) - x.org, vec3(s0 + w, zp + 0.02f) - x.org, vec3(s1 + w, zp + 0.02f) - x.org, vec3(s1 - w, zp + 0.02f) - x.org,
                                vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), kWhite, paint, vec3(0, 0, 1));
            }
        return zp;
    }
    // asphalt: a grid of about 5 m from the walk's curb to the street end (one quad far away)
    {
        float v0 = walk, v1 = depth;
        int nx = x.detail ? Clamp((int)(2.f * hxC / 5.f), 1, 16) : 1, ny = x.detail ? Clamp((int)((v1 - v0) / 5.f), 1, 12) : 1;
        thread_local std::vector<vec3> g;
        g.assign((size_t)(nx + 1) * (ny + 1), vec3(0.f));
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                vec2 p = f0 + b.ax * (-hxC + 2.f * hxC * i / nx) + b.front * (v0 + (v1 - v0) * j / ny);
                g[(size_t)j * (nx + 1) + i] = vec3(p, map.heightAt(p.x, p.y) + 0.05f) - x.org;
            }
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                vec3 a = g[(size_t)j * (nx + 1) + i], c1 = g[(size_t)j * (nx + 1) + i + 1], c2 = g[(size_t)(j + 1) * (nx + 1) + i + 1], d = g[(size_t)(j + 1) * (nx + 1) + i];
                x.m->quadFacing(a, c1, c2, d, vec2(a.x, a.y) * 0.25f, vec2(c1.x, c1.y) * 0.25f, vec2(c2.x, c2.y) * 0.25f, vec2(d.x, d.y) * 0.25f, kWhite, am, vec3(0, 0, 1));
            }
    }
    // the walk along the shopfronts: concrete from below the ground to just under the shop floors, its face the curb
    // (walkable: the curb is at most 0.5 m here, under a walker's step)
    {
        vec2 wc = f0 + b.front * (walk * 0.5f);
        float zb = Min(gLo, zw) - 0.4f;
        x.m->box(vec3(wc, (zb + zw) * 0.5f) - x.org, X, Y, Z, vec3(hxC, walk * 0.5f, (zw - zb) * 0.5f), packRGBA8(0.9f, 0.89f, 0.86f, 1), makeMat(MAT_SIDEWALK), true);
        addCollision(x, wc, b.ax, hxC, walk * 0.5f, zb, zw);
    }
    // stall lines: 2.7 m apart, 1 to 6 m out from the shopfronts, on the asphalt
    if (lines && x.detail && pd > 5.f) {
        for (float u = -b.hx + 2.f; u < b.hx - 1.f; u += 2.7f) {
            vec2 w = b.ax * 0.06f;
            for (int sgm = 0; sgm < 2; sgm++) {
                vec2 s0 = f0 + b.ax * u + b.front * (walk + 0.05f + 2.5f * sgm), s1 = s0 + b.front * 2.5f;
                float z0s = map.heightAt(s0.x, s0.y) + 0.07f, z1s = map.heightAt(s1.x, s1.y) + 0.07f;
                x.m->quadFacing(vec3(s0 - w, z0s) - x.org, vec3(s0 + w, z0s) - x.org, vec3(s1 + w, z1s) - x.org, vec3(s1 - w, z1s) - x.org, vec2(0, 0), vec2(1, 0),
                                vec2(1, 1), vec2(0, 1), kWhite, paint, vec3(0, 0, 1));
            }
        }
    }
    return -1e9f;
}

// The surface of a court made by stripCourt under a point (a canopy support's or a pump's foot): the plateau, or the
// asphalt on the ground (never above the shop floor)
inline float courtFoot(float plateau, vec2 p, float z0) {
    if (plateau > -1e8f) return plateau;
    return gMap ? Min(gMap->heightAt(p.x, p.y) + 0.05f, z0) : z0;
}

// Back yard pool of a house or villa (blockstyle.cpp poolDeck decides whether there is one and fits it). The deck
// stands on the highest ground under it, so the water clears the terrain and the lawn (which leaves the deck out:
// lawnHole), and a skirt runs down to the ground round it. The pools used to sit below the lawn and read as squares of
// grass from the air. Shapes: a rectangle, rounded ends, a lap pool behind the villas; a spa on the end of some; a coping
// round the water; the deck in brick pavers, keystone, poured concrete or, in the Keys, wood. The water is a glossy
// turquoise, lit from below at night on most.
void backyardPool(Ctx& x, const Building& b) {
    if (!gMap) return;
    const WorldMap& map = *gMap;
    MeshData& m = *x.m;
    const vec3 org = x.org;
    const vec2 ax = b.ax, ay = perp(b.ax);
    Rng pr(b.seed ^ 0x9001Fu);
    vec2 pc;
    float pw, pdd, dw;
    int kind;
    if (!blockstyle::poolDeck(b, pr, pc, pw, pdd, dw, kind)) return;
    const bool villa = b.style == BS_VILLA;
    const bool keys = b.region == REG_KEYS || b.region == REG_KEY_TOWN || b.region == REG_GULF_TOWN;
    const float du = pw + dw, dv = pdd + dw;
    vec2 dc[4];   // the deck's corners (CCW from +ax +ay)
    float gHi = map.heightAt(pc.x, pc.y);
    for (int c = 0; c < 4; c++) {
        float sx = (c == 0 || c == 3) ? 1.f : -1.f, sy = c < 2 ? 1.f : -1.f;
        dc[c] = pc + ax * (sx * du) + ay * (sy * dv);
        gHi = Max(gHi, map.heightAt(dc[c].x, dc[c].y));
    }
    const float pz = gHi + 0.1f;
    yardKeep(x, pc, ax, ay, du, dv);
    lawnHole(x, pc, du, dv);
    // the deck: 0 brick pavers, 1 keystone, 2 poured concrete, 3 wood
    const float dm = pr.f();
    const int deck = keys ? (dm < 0.6f ? 3 : 2) : (villa ? (dm < 0.55f ? 1 : 0) : (dm < 0.45f ? 0 : (dm < 0.8f ? 2 : 1)));
    u32 dcol, dmat;
    switch (deck) {
        case 0: {
            const vec3 pav[] = {vec3(0.92f, 0.9f, 0.88f), vec3(1.f, 0.82f, 0.72f), vec3(0.86f, 0.86f, 0.86f)};
            vec3 t = pav[pr.next() % 3u];
            dcol = packRGBA8(t.x, t.y, t.z, 1);
            dmat = makeMat(MAT_PAVERS);
            break;
        }
        case 1: dcol = packRGBA8(1.f, 0.98f, 0.9f, 1); dmat = makeMat(MAT_STONE); break;
        case 2: dcol = packRGBA8(1.f, 1.f, 0.97f, 1); dmat = makeMat(MAT_CONCRETE); break;
        default: dcol = packRGBA8(0.95f, 0.85f, 0.75f, 1); dmat = makeMat(MAT_WOOD); break;
    }
    // the water: glossy turquoise (a flat colour on the smooth emissive material), its light on at night on most
    const float wt = pr.range(-0.06f, 0.06f);
    const bool lit = pr.chance(0.65f);
    const u32 wcol = packRGBA8(0.12f + wt, 0.5f + wt * 0.5f, 0.62f - wt, lit ? 0.02f : 0.f), wmat = makeMat(MAT_EMISSIVE, 6u);
    // (pavers: 20 x 10 cm bricks in world metres (sitegeo kPaverUV), wrapped every 1200 m; wood: boards along the pool)
    const vec2 uvOrg(floorf(org.x / 1200.f) * 1200.f, floorf(org.y / 1200.f) * 1200.f);
    auto duv = [&](vec2 p) {
        vec2 q = p - uvOrg;
        if (deck == 0) return q * (1.f / 0.6f);
        if (deck == 3) return vec2(dot(q, ax), dot(q, ay));
        return q;
    };
    const u32 copeCol = deck == 3 ? packRGBA8(0.98f, 0.97f, 0.94f, 1) : (deck == 1 ? packRGBA8(1.f, 1.f, 0.95f, 1) : packRGBA8(0.97f, 0.96f, 0.93f, 1));
    const u32 copeMat = makeMat(deck == 1 ? MAT_STONE : MAT_CONCRETE);
    // the outline: per corner (CCW from +ax +ay) the samples of its arc, or the corner itself; the coping's outer edge; and
    // the deck's edge along the same rays (both offsets are dw + r, so the 45-degree ray meets the deck's corner)
    const float r = kind == 1 ? Min(pw, pdd) * 0.96f : 0.f, cw = 0.32f;
    struct Ring {
        vec2 in, cop, out;
    };
    Ring ring[20];
    int nr = 0;
    for (int c = 0; c < 4; c++) {
        float sx = (c == 0 || c == 3) ? 1.f : -1.f, sy = c < 2 ? 1.f : -1.f;
        vec2 C = pc + ax * (sx * (pw - r)) + ay * (sy * (pdd - r));
        int ns = r > 0.05f ? 5 : 1;
        for (int i = 0; i < ns; i++) {
            float th = (float)c * kHalfPi + (ns == 1 ? kHalfPi * 0.5f : kHalfPi * (float)i / (float)(ns - 1));
            float ca = cosf(th), sa = sinf(th);
            vec2 dir = ax * ca + ay * sa;
            float t = Min(fabsf(ca) > 1e-4f ? (dw + r) / fabsf(ca) : 1e9f, fabsf(sa) > 1e-4f ? (dw + r) / fabsf(sa) : 1e9f);
            ring[nr++] = {C + dir * r, C + dir * (r + cw * (ns == 1 ? 1.41421f : 1.f)), C + dir * t};
        }
    }
    const vec3 up(0, 0, 1);
    if (!x.detail) {
        // the far LOD: the water alone as a rectangle (a rounded one a little smaller), a little higher (the coarser terrain
        // far off), so the yards keep their pools from the air
        const float sh = r > 0.f ? 0.88f : 1.f;
        std::vector<vec3> water;
        for (int c = 0; c < 4; c++) {
            float sx = (c == 0 || c == 3) ? 1.f : -1.f, sy = c < 2 ? 1.f : -1.f;
            water.push_back(vec3(pc + ax * (sx * pw * sh) + ay * (sy * pdd * sh), pz + 0.1f) - org);
        }
        m.polygon(water, up, wcol, wmat, 1.f);
        return;
    }
    for (int i = 0; i < nr; i++) {
        const Ring &a = ring[i], &c = ring[(i + 1) % nr];
        m.quadFacing(vec3(a.in, pz) - org, vec3(c.in, pz) - org, vec3(c.cop, pz) - org, vec3(a.cop, pz) - org, duv(a.in), duv(c.in), duv(c.cop), duv(a.cop), copeCol,
                     copeMat, up);
        m.quadFacing(vec3(a.cop, pz) - org, vec3(c.cop, pz) - org, vec3(c.out, pz) - org, vec3(a.out, pz) - org, duv(a.cop), duv(c.cop), duv(c.out), duv(a.out), dcol,
                     dmat, up);
        // the pool's tiled side down to the water
        vec2 mid = (a.in + c.in) * 0.5f;
        float L = length(c.in - a.in);
        if (L > 1e-3f)
            m.quadFacing(vec3(a.in, pz) - org, vec3(c.in, pz) - org, vec3(c.in, pz - 0.12f) - org, vec3(a.in, pz - 0.12f) - org, vec2(0, 0), vec2(L, 0), vec2(L, 0.12f),
                         vec2(0, 0.12f), packRGBA8(0.85f, 0.9f, 0.92f, 1), makeMat(MAT_TILE_POOL), vec3(pc - mid, 0.f));
    }
    {
        std::vector<vec3> water;
        for (int i = 0; i < nr; i++) water.push_back(vec3(ring[i].in, pz - 0.07f) - org);
        m.polygon(water, up, wcol, wmat, 1.f);
    }
    // the skirt round the deck, down into the ground
    for (int c = 0; c < 4; c++) {
        vec2 a = dc[c], e = dc[(c + 1) % 4];
        float zl = Min(map.heightAt(a.x, a.y), map.heightAt(e.x, e.y)) - 0.06f;
        float L = length(e - a);
        m.quadFacing(vec3(a, pz) - org, vec3(e, pz) - org, vec3(e, zl) - org, vec3(a, zl) - org, vec2(0, pz), vec2(L, pz), vec2(L, zl), vec2(0, zl), dcol, dmat,
                     vec3((a + e) * 0.5f - pc, 0.f));
    }
    // a spa on one end of a rectangular pool (its rim over the coping, the water up to the brim)
    if (kind == 0 && dw >= 1.f && pr.chance(villa ? 0.35f : 0.12f)) {
        const float s = pr.chance(0.5f) ? 1.f : -1.f, hs = Min(0.95f, dw - 0.15f), rim = 0.16f, zt = pz + 0.42f;
        const vec2 sc = pc + ax * (s * (pw + 0.1f));
        const vec3 X(ax, 0.f), Y(ay, 0.f), Z(0, 0, 1);
        for (int k = 0; k < 4; k++) {
            bool alongU = (k & 1) == 0;
            float sg = (k & 2) ? -1.f : 1.f;
            vec2 wc = sc + (alongU ? ay * (sg * (hs - rim * 0.5f)) : ax * (sg * (hs - rim * 0.5f)));
            vec3 he = alongU ? vec3(hs, rim * 0.5f, (zt - pz + 0.1f) * 0.5f) : vec3(rim * 0.5f, hs - rim, (zt - pz + 0.1f) * 0.5f);
            m.box(vec3(wc, (zt + pz - 0.1f) * 0.5f) - org, X, Y, Z, he, copeCol, copeMat);
        }
        std::vector<vec3> sw;
        for (int c = 0; c < 4; c++) {
            float sx = (c == 0 || c == 3) ? 1.f : -1.f, sy = c < 2 ? 1.f : -1.f;
            sw.push_back(vec3(sc + ax * (sx * (hs - rim)) + ay * (sy * (hs - rim)), zt - 0.05f) - org);
        }
        m.polygon(sw, up, packRGBA8(0.14f, 0.52f, 0.6f, 0.02f), makeMat(MAT_EMISSIVE, 6u), 1.f);
    }
}

// Driveway surface by district and house: poured concrete; brick pavers (the suburbs, the Mediterranean houses, the villas
// and the Grove); old asphalt (the older city, the towns); crushed shell and gravel (the Keys, the country). fp: the
// driveway's corners, 6 cm over the ground
void drivewaySurface(Ctx& x, const Building& b, const std::vector<vec2>& fp) {
    if (!gMap) return;
    const WorldMap& map = *gMap;
    const u32 h = hash32(b.seed ^ 0xD71E5u);
    const float p = hashToFloat(h);
    int kind = 0;   // 0 concrete, 1 pavers, 2 asphalt, 3 gravel
    switch (b.region) {
        case REG_SUBURBS: kind = p < 0.5f ? 0 : (p < 0.82f ? 1 : 2); break;
        case REG_GROVE:
        case REG_KEY_CORAL:
        case REG_BAY_ISLAND: kind = p < 0.55f ? 1 : (p < 0.82f ? 0 : 3); break;
        case REG_CALLE_LUNA:
        case REG_FLATS:
        case REG_NORTH_CITY:
        case REG_MIDTOWN: kind = p < 0.45f ? 0 : (p < 0.78f ? 2 : 1); break;
        case REG_KEYS:
        case REG_KEY_TOWN:
        case REG_GULF_TOWN: kind = p < 0.45f ? 3 : (p < 0.8f ? 0 : 2); break;
        case REG_LAKE_TOWN:
        case REG_HARLOW:
        case REG_FORT_CASTELL: kind = p < 0.4f ? 0 : (p < 0.72f ? 2 : 3); break;
        case REG_REDLAND:
        case REG_FARMLAND:
        case REG_RIDGE:
        case REG_SAWGRASS: kind = p < 0.5f ? 3 : (p < 0.8f ? 2 : 0); break;
        default: kind = p < 0.6f ? 0 : (p < 0.85f ? 1 : 2); break;
    }
    if (b.style == BS_VILLA && kind == 2) kind = 1;
    if ((b.arch == AR_HOUSE_MED || b.arch == AR_VILLA_MED) && kind == 0 && hashToFloat(hash32(h + 7u)) < 0.6f) kind = 1;
    u32 col = kWhite, mat = makeMat(MAT_CONCRETE);
    float uvs = 1.f;
    switch (kind) {
        case 1: {
            const vec3 pav[] = {vec3(1.f, 0.82f, 0.72f), vec3(0.92f, 0.9f, 0.88f), vec3(0.86f, 0.86f, 0.86f), vec3(1.f, 0.95f, 0.8f)};
            vec3 t = pav[(h >> 8) % 4u];
            col = packRGBA8(t.x, t.y, t.z, 1);
            mat = makeMat(MAT_PAVERS);
            uvs = 1.f / 0.6f;
            break;
        }
        case 2: mat = makeMat(MAT_ASPHALT_OLD); break;
        case 3: col = packRGBA8(1.f, 1.f, 0.96f, 1); mat = makeMat(MAT_ROOF_GRAVEL); break;
        default: {
            float t = 0.9f + 0.1f * hashToFloat(h >> 12);
            col = packRGBA8(t, t, t * 0.98f, 1);
            break;
        }
    }
    std::vector<vec3> poly;
    for (const vec2& q : fp) poly.push_back(vec3(q, map.heightAt(q.x, q.y) + 0.06f) - x.org);
    x.m->polygon(poly, vec3(0, 0, 1), col, mat, uvs);
}

}  // namespace buildmesh_detail

using namespace buildmesh_detail;

// propmesh.cpp (defined later in the unity build; the defaults come with its definition)
void leafBlobEx(MeshData& m, vec3 org, vec3 c, vec3 ax, vec3 ay, vec3 R, vec3 tint, u32 seed, float bump, int SU, int SV, vec3 blossom);

}  // namespace World

#include "massing.cpp"

namespace World {

// facadedetail.cpp
// (lawnHoles: paved ground the house's lawn leaves out, lawnHole above)
void buildFacadeDetail(const Building& b, const FacadeGPU& fac, const WorldMap& map, vec3 org, MeshData& m, std::vector<CollisionBox>* col,
                       std::vector<PropInstance>* props, std::vector<LightInstance>* lights, const std::vector<FacadeMass>& masses,
                       const std::vector<vec4>* lawnHoles = nullptr);

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
    roofFinish(b, x.roofCol, x.roofMat);
    if (b.interior >= 0 && interiorOwnsShell(b.interior)) return;   // the whole structure streams with its interior
    if (b.siteElem >= 0) return;   // hand-built by its site element (places.h): hotels, hospitals, campus halls, chapels
    thread_local std::vector<FacadeMass> masses;
    masses.clear();
    x.masses = detail ? &masses : nullptr;
    thread_local std::vector<YardKeep> keeps;
    keeps.clear();
    x.keep = props ? &keeps : nullptr;
    thread_local std::vector<vec4> holes;
    holes.clear();
    x.holes = detail ? &holes : nullptr;
    Rng r(b.seed ^ 0xB111D1u);
    vec2 ay = perp(b.ax);
    float z0 = b.baseZ;
    float bay = fac.bayW;
    u32 roofGray = packRGBA8(0.9f, 0.9f, 0.88f, 1);
    // archetype massing (blockstyle.cpp / massing.cpp): shops, apartments, offices, condos, industry
    if (buildArchMesh(x, b, fac)) {
        if (detail && !masses.empty()) buildFacadeDetail(b, fac, map, org, m, col, props, lights, masses);
        return;
    }
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
            // Signature crowns (their own stream, the same in every LOD): a stepped crown with LED bands on each step, or a
            // sloped glass top
            Rng cr(b.seed ^ 0xC2041Bu);
            int special = (b.floors >= 18 && b.interior < 0 && shape <= 4) ? cr.irange(0, 9) : 99;
            int crown = r.irange(0, 5);
            if (special <= 1) {
                float sx = thx, sy = thy;
                vec2 sc = tc;
                vec3 lc = hsvToRgb(cr.f(), cr.range(0.3f, 0.8f), 1.f);
                if (cr.chance(0.4f)) lc = vec3(1.f, 0.95f, 0.85f);
                int steps = cr.irange(2, 4);
                float stepH = Max(fac.floorH * cr.irange(1, 2), 4.f);
                for (int k = 0; k < steps; k++) {
                    sx *= cr.range(0.72f, 0.84f);
                    sy *= cr.range(0.72f, 0.84f);
                    auto fp = rectFP(sc, b.ax, sx, sy);
                    flatMass(x, fp, zc, zc + stepH, z0, b.facade, bay, false, roofGray, FM_TIER, 0.f);
                    addCollision(x, sc, b.ax, sx, sy, zc, zc + stepH);
                    // the LED band round the top of the step
                    auto rim = rectFP(sc, b.ax, sx + 0.03f, sy + 0.03f);
                    plainWalls(x, rim, zc + stepH - 0.35f, zc + stepH - 0.1f, packRGBA8(lc.x, lc.y, lc.z, 0.55f), makeMat(MAT_EMISSIVE));
                    zc += stepH;
                }
                if (cr.chance(0.6f)) {
                    m.cylinder(vec3(sc, zc) - org, 0.5f, 0.06f, cr.range(8.f, 18.f), 8, packRGBA8(0.75f, 0.76f, 0.78f, 1), makeMat(MAT_METAL_BRUSHED), false);
                    if (lights) {
                        LightInstance li;
                        li.pos = vec3(sc, zc + 8.f);
                        li.color = vec3(1, 0.1f, 0.05f) * 400.f;
                        li.radius = 10.f;
                        li.dir = vec3(0);
                        li.cone = 0;
                        li.type = 4;
                        lights->push_back(li);
                    }
                }
            } else if (special == 2) {
                // sloped glass top rising toward the back of the lot
                float rise = Clamp(Min(thx, thy) * cr.range(0.5f, 0.9f), 4.f, 18.f);
                vec2 ay2 = perp(b.ax);
                float fs = dot(b.front, ay2) >= 0.f ? 1.f : -1.f;   // the street side along ay2
                vec3 c0 = vec3(tc - b.ax * thx + ay2 * (fs * thy), zc), c1 = vec3(tc + b.ax * thx + ay2 * (fs * thy), zc);   // front edge, low
                vec3 c2 = vec3(tc + b.ax * thx - ay2 * (fs * thy), zc + rise), c3 = vec3(tc - b.ax * thx - ay2 * (fs * thy), zc + rise);   // back edge, high
                vec4 gc = unpackRGBA8(fac.glassColor);
                u32 glassC = packRGBA8(gc.x * 0.5f, gc.y * 0.5f, gc.z * 0.5f, 1);
                m.quadFacing(c0 - org, c1 - org, c2 - org, c3 - org, vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), glassC, makeMat(MAT_GLASS), vec3(b.front, 1.f));
                // back wall (facade) and the two side triangles
                vec3 b0 = vec3(tc + b.ax * thx - ay2 * (fs * thy), zc), b1 = vec3(tc - b.ax * thx - ay2 * (fs * thy), zc);
                m.quadFacing(b0 - org, b1 - org, c3 - org, c2 - org, vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), packRGBA8(0.8f, 0.8f, 0.8f, 1), makeMat(MAT_CONCRETE_PANEL),
                             vec3(-b.front, 0.f));
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    vec3 p0 = sgn < 0 ? c0 : c1, p1 = sgn < 0 ? b1 : b0, p2 = sgn < 0 ? c3 : c2;
                    vec3 nn(b.ax * (float)sgn, 0.f);
                    u32 i0 = m.addVertex(p0 - org, nn, vec3(ay2, 0.f), vec2(0, 0), packRGBA8(0.8f, 0.8f, 0.8f, 1), makeMat(MAT_CONCRETE_PANEL));
                    u32 i1 = m.addVertex(p1 - org, nn, vec3(ay2, 0.f), vec2(1, 0), packRGBA8(0.8f, 0.8f, 0.8f, 1), makeMat(MAT_CONCRETE_PANEL));
                    u32 i2 = m.addVertex(p2 - org, nn, vec3(ay2, 0.f), vec2(1, 1), packRGBA8(0.8f, 0.8f, 0.8f, 1), makeMat(MAT_CONCRETE_PANEL));
                    if (dot(cross(p1 - p0, p2 - p0), nn) >= 0.f) m.tri(i0, i1, i2);
                    else m.tri(i0, i2, i1);
                }
                addCollision(x, tc, b.ax, thx, thy, zc, zc + rise * 0.5f);
            } else if (crown <= 1) {
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
            // (asphalt on the ground, the walk along the shopfront, the stalls of a strip mall; a plateau above low ground)
            float plateau = pd > 2.f ? stripCourt(x, b, pc, pd, b.style == BS_STRIPMALL) : -1e9f;
            if (b.style == BS_STRIPMALL && detail) {
                // Covered walkway canopy with columns (standing on the court's asphalt)
                vec2 cc = b.c + b.front * (b.hy + 1.6f);
                m.box(vec3(cc, z0 + 3.6f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx, 1.6f, 0.25f), packRGBA8(0.92f, 0.9f, 0.86f, 1),
                      makeMat(MAT_PLASTER), true);
                for (float u = -b.hx + 1.f; u <= b.hx; u += 6.f) {
                    vec2 cp = b.c + b.front * (b.hy + 3.f) + b.ax * u;
                    float gz = courtFoot(plateau, cp, z0 - 0.1f), zt = z0 + 3.5f;
                    m.box(vec3(cp, (gz + zt) * 0.5f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(0.2f, 0.2f, (zt - gz) * 0.5f), kWhite, makeMat(MAT_PLASTER));
                }
            }
            if (b.style == BS_GASSTATION && detail) {
                // Canopy over the pumps in one of four kinds (seeded; the columns and pumps stay where population.cpp
                // parks the cars): the flat canopy with a lit brand band, a sixties butterfly canopy on a middle row of
                // tapered columns, a barrel-tile hip on stucco piers, or a slab carried out from the kiosk
                vec2 cc = pc;
                float cw = Min(b.hx * 2.2f, 16.f), cd = Min(pd * 0.8f, 9.f);
                vec3 bc = hsvToRgb(r.f(), 0.8f, 0.9f);
                const u32 brand = packRGBA8(bc.x, bc.y, bc.z, 1), metal = makeMat(MAT_METAL_PAINTED);
                const vec3 X(b.ax, 0.f), Y(ay, 0.f), Z(0, 0, 1);
                u32 kh = hash32(b.seed ^ 0x6A5C0u) % 100u;
                int kind = kh < 40u ? 0 : (kh < 65u ? 1 : (kh < 85u ? 2 : 3));
                float zc = z0 + 4.8f;   // the canopy's underside
                if (kind == 0) {
                    m.box(vec3(cc, z0 + 5.2f) - org, X, Y, Z, vec3(cw, cd, 0.45f), kWhite, metal, true);
                    auto rim = rectFP(cc, b.ax, cw + 0.02f, cd + 0.02f);
                    plainWalls(x, rim, z0 + 4.8f, z0 + 5.3f, packRGBA8(bc.x, bc.y, bc.z, 0.15f), makeMat(MAT_EMISSIVE, 6u));
                } else if (kind == 1) {
                    // butterfly: two plates rising from a valley over the middle row, brand-coloured edges
                    float zv = z0 + 4.6f, ze = z0 + 5.6f;
                    zc = zv;
                    for (int sd = -1; sd <= 1; sd += 2) {
                        vec3 a0 = vec3(cc - b.ax * (cw + 0.3f), zv) - org, a1 = vec3(cc + b.ax * (cw + 0.3f), zv) - org;
                        vec3 b0 = vec3(cc - b.ax * (cw + 0.3f) + b.front * (sd * (cd + 0.4f)), ze) - org, b1 = vec3(cc + b.ax * (cw + 0.3f) + b.front * (sd * (cd + 0.4f)), ze) - org;
                        vec3 up = normalize(vec3(b.front * (-sd * (ze - zv)), cd + 0.4f));
                        m.quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(2.f * cw, 0), vec2(2.f * cw, cd), vec2(0, cd), packRGBA8(0.96f, 0.96f, 0.95f, 1), makeMat(MAT_PLASTER), up);
                        m.quadFacing(a0, b0, b1, a1, vec2(0, 0), vec2(cd, 0), vec2(cd, 2.f * cw), vec2(0, 2.f * cw), packRGBA8(0.9f, 0.9f, 0.88f, 1), makeMat(MAT_PLASTER), -up);
                        // the edge fascia in the brand colour
                        m.box(vec3(cc + b.front * (sd * (cd + 0.4f)), ze - 0.12f) - org, X, Y, Z, vec3(cw + 0.3f, 0.06f, 0.14f), brand, metal, true);
                    }
                    for (int k = -1; k <= 1; k += 2) {
                        vec2 mp = cc + b.ax * (k * cw * 0.35f);
                        float gm = courtFoot(plateau, mp, z0);
                        m.cylinder(vec3(mp, gm) - org, 0.32f, 0.16f, zv - gm, 10, packRGBA8(0.95f, 0.95f, 0.94f, 1), makeMat(MAT_PLASTER), false);
                    }
                } else if (kind == 2) {
                    // barrel-tile hip on a stucco frieze (the old service stations)
                    m.box(vec3(cc, z0 + 5.05f) - org, X, Y, Z, vec3(cw + 0.2f, cd + 0.2f, 0.3f), packRGBA8(0.95f, 0.91f, 0.82f, 1), makeMat(MAT_STUCCO), true);
                    pitchedRoof(x, cc, b.ax, cw + 0.2f, cd + 0.2f, z0 + 5.35f, 0.32f, true, 0.45f, packRGBA8(1.f, 0.95f, 0.92f, 1), makeMat(MAT_ROOF_TILE), kWhite,
                                makeMat(MAT_PLASTER));
                    m.box(vec3(cc, z0 + 4.95f) - org, X, Y, Z, vec3(cw + 0.22f, cd + 0.22f, 0.08f), brand, metal, false);
                } else {
                    // a slab carried out from the kiosk over the pumps, the kiosk's colours
                    vec2 k0 = b.c + b.front * b.hy;
                    float v1 = dot(cc - k0, b.front) + cd + 0.3f;
                    vec2 sc = k0 + b.front * (v1 * 0.5f);
                    m.box(vec3(sc, z0 + 5.15f) - org, X, Y, Z, vec3(cw + 0.3f, v1 * 0.5f, 0.35f), packRGBA8(0.97f, 0.97f, 0.96f, 1), makeMat(MAT_PLASTER), true);
                    m.box(vec3(sc + b.front * (v1 * 0.5f), z0 + 5.15f) - org, X, Y, Z, vec3(cw + 0.32f, 0.04f, 0.36f), brand, metal, true);
                }
                for (int k = -1; k <= 1; k += 2)
                    for (int j = -1; j <= 1; j += 2) {
                        // (column and pump on the forecourt's asphalt, which follows the ground; the butterfly canopy stands
                        // on its middle columns)
                        vec2 cp = cc + b.ax * (k * cw * 0.6f) + b.front * (j * cd * 0.45f), pp = cp + b.ax * 1.2f;
                        float gc = courtFoot(plateau, cp, z0), gp = courtFoot(plateau, pp, z0);
                        if (kind != 1) {
                            u32 colC = kind == 2 ? packRGBA8(0.95f, 0.91f, 0.82f, 1) : kWhite;
                            u32 colM = kind == 2 ? makeMat(MAT_STUCCO) : metal;
                            float hw = kind == 2 ? 0.34f : 0.25f;
                            m.box(vec3(cp, (gc + zc) * 0.5f) - org, X, Y, Z, vec3(hw, hw, (zc - gc) * 0.5f), colC, colM);
                        }
                        // the pump island (a low curb) and the pump
                        m.box(vec3(pp, gp + 0.08f) - org, X, Y, Z, vec3(0.9f, 0.45f, 0.08f), packRGBA8(0.85f, 0.84f, 0.8f, 1), makeMat(MAT_CONCRETE));
                        m.box(vec3(pp, gp + 0.96f) - org, X, Y, Z, vec3(0.5f, 0.3f, 0.8f), brand, metal);
                    }
                if (lights)
                    for (int k = -1; k <= 1; k += 2) {
                        LightInstance li;
                        li.pos = vec3(cc + b.ax * (k * cw * 0.4f), zc - 0.2f);
                        li.color = vec3(0.95f, 0.97f, 1.f) * 6000.f;
                        li.radius = 18.f;
                        li.dir = vec3(0, 0, -1);
                        li.cone = 0.1f;
                        li.type = 1;
                        lights->push_back(li);
                    }
                // the price sign on a pole at a street corner of the forecourt
                if ((b.seed >> 9) % 10u < 7u) {
                    float sd = (b.seed & 512u) ? 1.f : -1.f;
                    vec2 sp = b.lotC + b.front * (b.lotHy - 1.2f) + b.ax * (sd * Min(b.lotHx - 1.2f, cw + 2.f));
                    float gs = map.heightAt(sp.x, sp.y);
                    m.box(vec3(sp, gs + 3.2f) - org, X, Y, Z, vec3(0.14f, 0.14f, 3.2f), packRGBA8(0.75f, 0.76f, 0.78f, 1), metal);
                    m.box(vec3(sp, gs + 6.1f) - org, X, Y, Z, vec3(1.1f, 0.22f, 0.7f), packRGBA8(bc.x, bc.y, bc.z, 0.22f), makeMat(MAT_EMISSIVE, 6u), true);
                    m.box(vec3(sp, gs + 4.7f) - org, X, Y, Z, vec3(1.0f, 0.2f, 0.6f), packRGBA8(0.98f, 0.98f, 0.96f, 0.15f), makeMat(MAT_EMISSIVE, 6u), true);
                    addCollision(x, sp, b.ax, 0.15f, 0.15f, gs - 0.5f, gs + 7.f);
                }
            }
            break;
        }
        case BS_HOUSE:
        case BS_VILLA:
        case BS_FARMHOUSE:
        case BS_SHACK: {
            bool stilts = b.style == BS_SHACK;
            float zb = z0 + (stilts ? (b.arch == AR_SHACK_STILT ? massing::shackStiltH(b) : 2.2f) : (b.arch == AR_HOUSE_RAISED ? massing::stiltFloorH(b) : 0.f));
            u32 roofMat, roofCol;
            // archetype houses (massing.cpp): ranch, bungalow, Mediterranean, two-storey, MiMo, split level, conch, modern villa
            if (!buildArchHouseBody(x, b, fac, roofCol, roofMat)) {
                auto fp = rectFP(b.c, b.ax, b.hx, b.hy);
                recordMass(x, fp, zb, zb + b.height, zb, FM_HOUSE, false);
                facadeWalls(x, fp, zb - (stilts ? 0.3f : 2.f), zb + b.height, zb, b.facade, bay);
                addCollision(x, b.c, b.ax, b.hx, b.hy, z0 - 2.f, zb + b.height + 2.f);
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
            }
            if (stilts && b.arch != AR_SHACK_STILT) {   // (the stilt-house archetype builds its own posts and decks)
                for (int k = -1; k <= 1; k += 2)
                    for (int j = -1; j <= 1; j += 2)
                        m.box(vec3(b.c + b.ax * (k * (b.hx - 0.3f)) + ay * (j * (b.hy - 0.3f)), z0 + 0.6f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1),
                              vec3(0.15f, 0.15f, 2.f), kWhite, makeMat(MAT_WOOD));
                m.box(vec3(b.c, zb - 0.15f) - org, vec3(b.ax, 0), vec3(ay, 0), vec3(0, 0, 1), vec3(b.hx + 1.2f, b.hy + 1.2f, 0.12f), kWhite, makeMat(MAT_WOOD), true);
            }
            // Back yard pool (backyardPool: on the deck's highest corner, the lawn cut away under it; blockstyle.cpp poolDeck
            // decides and fits it); in the far LOD its water alone
            auto yardPool = [&]() { backyardPool(x, b); };
            if (!detail) {
                if (b.style == BS_HOUSE || b.style == BS_VILLA) yardPool();
                break;
            }
            // a paved front yard (frontPaved): concrete or pavers from the house to the lawn's street edge; the driveways
            // below then only cross the strip beyond it
            const int paved = blockstyle::frontPaved(b);
            const float pavedEdge = dot(b.lotC - b.c, b.front) + b.lotHy - 0.3f;   // (along b.front from b.c)
            if (paved) {
                const float lat = dot(b.lotC - b.c, b.ax), v0 = b.hy - 0.05f, hu = b.lotHx - 0.15f;
                if (pavedEdge - v0 > 1.f) {
                    const vec2 pcen = b.c + b.ax * lat + b.front * ((v0 + pavedEdge) * 0.5f);
                    const float hv = (pavedEdge - v0) * 0.5f;
                    lawnHole(x, pcen, hu, hv);
                    std::vector<vec3> poly;
                    for (int c = 0; c < 4; c++) {
                        float sx = (c == 0 || c == 3) ? 1.f : -1.f, sy = c < 2 ? 1.f : -1.f;
                        vec2 q = pcen + b.ax * (sx * hu) + b.front * (sy * hv);
                        poly.push_back(vec3(q, map.heightAt(q.x, q.y) + 0.05f) - org);
                    }
                    const float t = 0.88f + 0.1f * hashToFloat(hash32(b.seed ^ 0xF4A80u));
                    if (paved == 2) m.polygon(poly, vec3(0, 0, 1), packRGBA8(1.f, 0.86f * t + 0.1f, 0.78f * t + 0.1f, 1), makeMat(MAT_PAVERS), 1.f / 0.6f);
                    else m.polygon(poly, vec3(0, 0, 1), packRGBA8(t, t, t * 0.98f, 1), makeMat(MAT_CONCRETE), 1.f);
                }
            }
            // (a driveway from a to the street, only its part beyond a paved front yard)
            auto drivewayFrom = [&](vec2 a) {
                if (!paved) return a;
                float va = dot(a - b.c, b.front);
                return va < pavedEdge ? a + b.front * (pavedEdge - va) : a;
            };
            // Garage wing (suburban houses / villas)
            // (deterministic from the seed and the lot, garageKind: facadedetail.cpp keeps hedges and garden walls clear of
            // this driveway)
            const int gk = garageKind(b);
            float side = garageSide(b);
            // (the block houses, the MiMo houses and the mobile homes have a carport instead: a flat roof on posts, a utility
            // room at the back)
            const bool carport = b.arch == AR_HOUSE_CBS || b.arch == AR_HOUSE_MIMO || b.arch == AR_HOUSE_TRAILER;
            if (gk == 1 && carport) {
                float gw = 3.2f, gd = massing::carportHalfDepth(b);   // (a mobile home's carport no deeper than the home)
                vec2 gc = b.c + b.ax * (side * (b.hx + gw)) + b.front * (b.hy - gd);
                const vec3 X(b.ax, 0.f), Y(ay, 0.f), Z(0, 0, 1);
                const vec2 Fw = b.front;
                const float zr = z0 + 2.7f;
                const u32 white = packRGBA8(0.95f, 0.95f, 0.94f, 1), steel = makeMat(MAT_METAL_PAINTED);
                // the roof slab, a little over the front and the outer side
                vec2 rc = gc + Fw * 0.15f + b.ax * (side * 0.15f);
                m.box(vec3(rc, zr + 0.11f) - org, X, Y, Z, vec3(gw + 0.15f, gd + 0.15f, 0.11f), white, makeMat(MAT_PLASTER), true);
                addCollision(x, rc, b.ax, gw + 0.15f, gd + 0.15f, zr, zr + 0.22f);
                // the utility room across the back (a door to the carport), the floor slab
                float ud = Min(1.8f, gd * 0.5f);
                vec2 uc = gc - Fw * (gd - ud);
                auto ufp = rectFP(uc, b.ax, gw, ud);
                facadeWalls(x, ufp, z0 - 1.f, zr, z0 + 100.f, b.facade, 20.f);
                addCollision(x, uc, b.ax, gw, ud, z0 - 1.f, zr);
                vec2 ud0 = uc + Fw * (ud + 0.02f) - b.ax * 0.45f, ud1 = ud0 + b.ax * 0.9f;
                m.quadFacing(vec3(ud0, z0) - org, vec3(ud1, z0) - org, vec3(ud1, z0 + 2.05f) - org, vec3(ud0, z0 + 2.05f) - org, vec2(0, 0), vec2(0.9f, 0),
                             vec2(0.9f, 2.05f), vec2(0, 2.05f), packRGBA8(0.85f, 0.85f, 0.82f, 1), makeMat(MAT_METAL_PAINTED), vec3(Fw, 0));
                {
                    auto ffp = rectFP(gc + Fw * ud, b.ax, gw, gd - ud);
                    lawnHole(x, gc + Fw * ud, gw, gd - ud);
                    std::vector<vec3> poly;
                    for (auto& p : ffp) poly.push_back(vec3(p, map.heightAt(p.x, p.y) + 0.06f) - org);
                    m.polygon(poly, vec3(0, 0, 1), kWhite, makeMat(MAT_CONCRETE), 1.f);
                }
                // supports along the outer side: slender steel posts, or on the MiMo houses a screen of fins
                float uo = side * (b.hx + 2.f * gw - 0.12f);
                if (b.arch == AR_HOUSE_MIMO && (b.seed & 128u)) {
                    float va = b.hy - 2.f * gd + 2.f * ud, vb = b.hy - 0.6f;
                    for (float v = va + 0.15f; v < vb; v += 0.32f) {
                        vec2 fp = b.c + b.ax * uo + Fw * v;
                        m.box(vec3(fp, (z0 + zr) * 0.5f) - org, X, Y, Z, vec3(0.04f, 0.12f, (zr - z0) * 0.5f), white, makeMat(MAT_CONCRETE));
                    }
                    addCollision(x, b.c + b.ax * uo + Fw * ((va + vb) * 0.5f), b.ax, 0.08f, (vb - va) * 0.5f, z0 - 0.5f, zr);
                }
                for (int k = 0; k < 2; k++) {
                    vec2 pp = b.c + b.ax * uo + Fw * (k ? b.hy - 0.12f : b.hy - 2.f * gd + 2.f * ud + 0.12f);
                    m.box(vec3(pp, (z0 + zr) * 0.5f) - org, X, Y, Z, vec3(0.05f, 0.05f, (zr - z0) * 0.5f), white, steel);
                    addCollision(x, pp, b.ax, 0.06f, 0.06f, z0 - 0.5f, zr);
                }
                yardKeep(x, gc, b.ax, ay, gw, gd);
                // driveway to the street
                vec2 dA = gc + Fw * gd, dB = b.lotC + Fw * (b.lotHy + 1.5f);
                dB = dA + Fw * Max(1.f, dot(dB - dA, Fw));
                yardKeep(x, (dA + dB) * 0.5f, b.ax, ay, 2.6f, length(dB - dA) * 0.5f);
                lawnHole(x, (dA + dB) * 0.5f, 2.6f, length(dB - dA) * 0.5f);
                dA = drivewayFrom(dA);
                if (dot(dB - dA, Fw) > 0.3f) drivewaySurface(x, b, rectFP((dA + dB) * 0.5f, b.ax, 2.6f, length(dB - dA) * 0.5f));
            } else if (gk == 2) {
                // the old frame houses and the raised Keys houses (parking under the house) keep no garage wing, nor does a
                // house on a lot too narrow for one: a concrete strip beside the house to the street (the car parks on it)
                float gw = 3.2f, gd = Min(b.hy, 3.4f);
                vec2 dA = b.c + b.ax * (side * (b.hx + gw)) + b.front * (b.hy - gd), dB = b.lotC + b.front * (b.lotHy + 1.5f);
                dB = dA + b.front * Max(1.f, dot(dB - dA, b.front));
                yardKeep(x, (dA + dB) * 0.5f, b.ax, ay, 1.6f, length(dB - dA) * 0.5f);
                for (int k = -1; k <= 1; k += 2) {
                    // (two wheel strips with grass between, the older driveway)
                    auto dfp = rectFP((dA + dB) * 0.5f + b.ax * (k * 0.75f), b.ax, 0.38f, length(dB - dA) * 0.5f);
                    lawnHole(x, (dA + dB) * 0.5f + b.ax * (k * 0.75f), 0.38f, length(dB - dA) * 0.5f);
                    std::vector<vec3> poly;
                    for (auto& p : dfp) poly.push_back(vec3(p, map.heightAt(p.x, p.y) + 0.06f) - org);
                    m.polygon(poly, vec3(0, 0, 1), kWhite, makeMat(MAT_CONCRETE), 1.f);
                }
            } else if (gk == 1) {
                float gw = 3.2f, gd = Min(b.hy, 3.4f);
                vec2 gc = b.c + b.ax * (side * (b.hx + gw)) + b.front * (b.hy - gd);
                auto gfp = rectFP(gc, b.ax, gw, gd);
                facadeWalls(x, gfp, z0 - 1.f, z0 + 2.9f, z0 + 100.f, b.facade, 20.f);
                if (b.arch == AR_HOUSE_MIMO || b.arch == AR_VILLA_MODERN) massing::eaveSlab(x, gfp, z0 + 2.9f, 0.5f, 0.22f, packRGBA8(0.96f, 0.96f, 0.95f, 1));
                else pitchedRoof(x, gc, b.ax, gw, gd, z0 + 2.9f, 0.35f, true, 0.4f, roofCol, roofMat, kWhite, makeMat(MAT_PLASTER));
                addCollision(x, gc, b.ax, gw, gd, z0 - 1.f, z0 + 3.5f);
                yardKeep(x, gc, b.ax, ay, gw, gd);
                // garage door
                vec2 d0 = gc + b.front * (gd + 0.02f) - b.ax * 2.4f, d1 = gc + b.front * (gd + 0.02f) + b.ax * 2.4f;
                m.quadFacing(vec3(d0, z0) - org, vec3(d1, z0) - org, vec3(d1, z0 + 2.2f) - org, vec3(d0, z0 + 2.2f) - org, vec2(0, 0), vec2(4.8f, 0),
                             vec2(4.8f, 2.2f), vec2(0, 2.2f), packRGBA8(0.92f, 0.92f, 0.9f, 1), makeMat(MAT_METAL_PAINTED), vec3(b.front, 0));
                // driveway to the street
                vec2 dA = gc + b.front * gd, dB = b.lotC + b.front * (b.lotHy + 1.5f);
                dB = dA + b.front * Max(1.f, dot(dB - dA, b.front));
                yardKeep(x, (dA + dB) * 0.5f, b.ax, ay, 2.6f, length(dB - dA) * 0.5f);
                lawnHole(x, (dA + dB) * 0.5f, 2.6f, length(dB - dA) * 0.5f);
                dA = drivewayFrom(dA);
                if (dot(dB - dA, b.front) > 0.3f) drivewaySurface(x, b, rectFP((dA + dB) * 0.5f, b.ax, 2.6f, length(dB - dA) * 0.5f));
            }
            yardPool();
            // Yard trees
            if (props) {
                int nt = r.irange(1, b.style == BS_VILLA ? 5 : 3);
                // the front and back yards measured from the house (a house set forward of its lot's centre has a short
                // front yard: the trees used to be measured from the lot's centre and could stand inside the house)
                const float off = dot(b.lotC - b.c, b.front), lat = dot(b.lotC - b.c, b.ax);
                const float fLo = b.hy + 1.5f, fHi = off + b.lotHy - 1.f, bLo = off - b.lotHy + 0.6f, bHi = -(b.hy + 2.f);
                for (int k = 0; k < nt; k++) {
                    PropInstance pi;
                    float tu = r.range(-1.1f, 1.1f);
                    bool inFront = r.chance(0.5f);
                    float tv = r.f();
                    if (inFront && fHi < fLo + 0.5f) inFront = false;
                    if (!inFront && bHi < bLo + 0.5f) inFront = fHi >= fLo + 0.5f;
                    float vy = inFront ? Lerp(fLo, Max(fLo, fHi), tv) : Lerp(bLo, Max(bLo, bHi), tv);
                    float ux = Clamp(tu * b.hx, lat - b.lotHx + 0.8f, lat + b.lotHx - 0.8f);
                    vec2 p = b.c + b.ax * ux + b.front * vy;
                    pi.pos = vec3(p, map.heightAt(p.x, p.y));
                    pi.yaw = r.f() * kTwoPi;
                    pi.scale = r.range(0.7f, 1.2f);
                    int reg = b.region;
                    pi.type = (reg == REG_FARMLAND || reg == REG_RIDGE) ? (r.chance(0.5f) ? PROP_TREE_OAK : PROP_TREE_PINE)
                                                                           : (r.chance(0.6f) ? PROP_PALM : (r.chance(0.5f) ? PROP_TREE_OAK : PROP_BUSH));
                    pi.variant = (u8)r.irange(0, 3);
                    pi.flags = 0;
                    // (not through a porch, a deck or its stair, the pool, the garage or the driveway, nor a house or a
                    // back-yard building: when the spot is taken, the spot mirrored across the house's middle, or the same
                    // spot in the other yard)
                    float vOther = inFront ? Lerp(bLo, Max(bLo, bHi), tv) : Lerp(fLo, Max(fLo, fHi), tv);
                    const vec2 cand[3] = {p, b.c + b.ax * Clamp(-ux, lat - b.lotHx + 0.8f, lat + b.lotHx - 0.8f) + b.front * vy, b.c + b.ax * ux + b.front * vOther};
                    int ok = -1;
                    for (int ci = 0; ci < 3 && ok < 0; ci++)
                        if (!yardKept(x, cand[ci], 0.5f) && !(gBuildings && gBuildings->pointInBuilding(cand[ci], 0.6f))) ok = ci;
                    if (ok < 0) continue;
                    if (ok > 0) pi.pos = vec3(cand[ok], map.heightAt(cand[ok].x, cand[ok].y));
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
    if (detail && !masses.empty()) buildFacadeDetail(b, fac, map, org, m, col, props, lights, masses, &holes);
}

}  // namespace World
