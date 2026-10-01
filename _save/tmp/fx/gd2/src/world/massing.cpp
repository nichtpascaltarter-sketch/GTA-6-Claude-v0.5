// Archetype massing (included by buildmesh.cpp after its helpers): the volumes an archetype carves out of a building's
// envelope (L and U plans, courtyards, stepped and podium sections, split lots, corner towers, curved fronts, wings), the
// roof form on each volume (parapet, eave slab, barrel-tile hip, tile pent, roof terrace, metal gable, sawtooth,
// butterfly), the archetype houses (ranch, bungalow, Mediterranean, two-storey, MiMo, split level, conch) and the
// industrial variants. Everything that shapes the silhouette is drawn from one seeded stream before any geometry, so
// the far LOD builds the same outline as the full-detail cell.
#include "buildings.h"

namespace World {

namespace massing {

using namespace buildmesh_detail;

// Envelope frame: u along Building::ax in [-hx, hx], v toward the street (Building::front) in [-hy, hy]
struct Env {
    vec2 c, ax, fr;
    float hx, hy;
    bool flip;   // (u, v) -> world mirrors the winding when front == -perp(ax)
    vec2 P(float u, float v) const { return c + ax * u + fr * v; }
};
Env envOf(const Building& b) {
    Env e;
    e.c = b.c;
    e.ax = b.ax;
    e.fr = b.front;
    e.hx = b.hx;
    e.hy = b.hy;
    e.flip = dot(b.front, perp(b.ax)) < 0.f;
    return e;
}
// polygon given counter-clockwise in (u, v) -> counter-clockwise in the world
std::vector<vec2> toWorld(const Env& e, const std::vector<vec2>& uv) {
    std::vector<vec2> w(uv.size());
    for (size_t i = 0; i < uv.size(); i++) w[i] = e.P(uv[i].x, uv[i].y);
    if (e.flip) std::reverse(w.begin(), w.end());
    return w;
}
std::vector<vec2> rectUV(float u0, float u1, float v0, float v1) { return {vec2(u0, v0), vec2(u1, v0), vec2(u1, v1), vec2(u0, v1)}; }

struct Vol {
    std::vector<vec2> uv;      // footprint, CCW in (u, v)
    std::vector<vec4> rects;   // rectangles covering it (u0, u1, v0, v1): collision, pitched roofs
    std::vector<vec2> hole;    // courtyard (CCW in (u, v)), empty if none
    float z0 = 0.f, z1 = 0.f;
    u32 facade = 0;
    u8 kind = FM_MAIN;
    u8 roof = RFM_PARAPET;
    bool ground = true;        // stands on the ground: walls run 3 m below it (terrain), collision from below
    bool top = true;           // nothing stands on its roof (clutter, terraces)
};

// Outward offset of a CCW polygon by d (mitred corners, clamped)
std::vector<vec2> offsetPoly(const std::vector<vec2>& fp, float d) {
    int n = (int)fp.size();
    std::vector<vec2> out(n);
    for (int i = 0; i < n; i++) {
        vec2 p = fp[i], prev = fp[(i + n - 1) % n], next = fp[(i + 1) % n];
        vec2 n0 = -perp(normalize(p - prev)), n1 = -perp(normalize(next - p));   // right normals = outward for CCW
        vec2 nm = normalize(n0 + n1);
        float sc = 1.f / Max(0.35f, dot(nm, n1));
        out[i] = p + nm * (d * sc);
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ roof forms
// Flat roof slab projecting past the walls (MiMo eave): edge faces, top, underside
void eaveSlab(Ctx& x, const std::vector<vec2>& fp, float z, float over, float t, u32 col) {
    std::vector<vec2> o = offsetPoly(fp, over);
    flatRoof(x, o, z + t, col, makeMat(MAT_ROOF_GRAVEL));
    std::vector<vec3> under;
    for (int i = (int)o.size() - 1; i >= 0; i--) under.push_back(vec3(o[i], z) - x.org);
    x.m->polygon(under, vec3(0, 0, -1), packRGBA8(0.93f, 0.93f, 0.92f, 1), makeMat(MAT_PLASTER), 1.f);
    plainWalls(x, o, z, z + t, packRGBA8(0.96f, 0.96f, 0.95f, 1), makeMat(MAT_PLASTER));
}

// Barrel-tile pent roof along a wall a -> b (CCW footprint edge): from the parapet top out over the facade
void pentRoof(Ctx& x, vec2 a, vec2 b, float zTop, float out, u32 tile) {
    vec2 t = normalize(b - a), n(t.y, -t.x);
    vec2 a0 = a - t * 0.25f, b0 = b + t * 0.25f;
    vec3 p0 = vec3(a0 - n * 0.2f, zTop + 0.32f) - x.org, p1 = vec3(b0 - n * 0.2f, zTop + 0.32f) - x.org;
    vec3 q0 = vec3(a0 + n * out, zTop - 0.12f) - x.org, q1 = vec3(b0 + n * out, zTop - 0.12f) - x.org;
    float len = length(b0 - a0);
    x.m->quadFacing(p0, p1, q1, q0, vec2(0, 0), vec2(len, 0), vec2(len, out), vec2(0, out), tile, makeMat(MAT_ROOF_TILE), vec3(n, 2.f));
    x.m->quadFacing(p0, q0, q1, p1, vec2(0, 0), vec2(out, 0), vec2(out, len), vec2(0, len), packRGBA8(0.9f, 0.88f, 0.84f, 1), makeMat(MAT_PLASTER), vec3(-n, -2.f));
    // drip edge
    x.m->quadFacing(q0, q1, q1 - vec3(0, 0, 0.1f), q0 - vec3(0, 0, 0.1f), vec2(0, 0), vec2(len, 0), vec2(len, 0.1f), vec2(0, 0.1f), tile, makeMat(MAT_ROOF_TILE), vec3(n, 0.f));
}

// Sawtooth (north light) roof over a rectangle: teeth across the depth, glazed vertical faces, gable ends
void sawtoothRoof(Ctx& x, const Env& e, float u0, float u1, float v0, float v1, float z, u32 roofCol, u32 wallCol, u32 wallMat) {
    float depth = v1 - v0;
    int teeth = Max(2, (int)roundf(depth / 7.5f));
    float td = depth / teeth, rise = Clamp(td * 0.42f, 2.2f, 3.4f);
    vec3 X(e.ax, 0.f);
    for (int k = 0; k < teeth; k++) {
        float va = v0 + k * td, vb = va + td;
        // slope rising toward the back (glazing faces the back of the lot)
        vec3 a0 = vec3(e.P(u0, vb), z) - x.org, a1 = vec3(e.P(u1, vb), z) - x.org;
        vec3 b0 = vec3(e.P(u0, va), z + rise) - x.org, b1 = vec3(e.P(u1, va), z + rise) - x.org;
        vec3 up = normalize(vec3(e.fr * rise, td));
        x.m->quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(u1 - u0, 0), vec2(u1 - u0, td), vec2(0, td), roofCol, makeMat(MAT_ROOF_METAL), up);
        // glazed face (vertical) at va
        vec3 g0 = vec3(e.P(u0, va), z) - x.org, g1 = vec3(e.P(u1, va), z) - x.org;
        x.m->quadFacing(g0, g1, b1, b0, vec2(0, 0), vec2(u1 - u0, 0), vec2(u1 - u0, rise), vec2(0, rise), packRGBA8(0.35f, 0.42f, 0.45f, 1), makeMat(MAT_GLASS),
                        vec3(-e.fr, 0.f));
        // gable triangles at both ends
        for (int s = 0; s < 2; s++) {
            float u = s ? u1 : u0;
            vec3 p0 = vec3(e.P(u, vb), z) - x.org, p1 = vec3(e.P(u, va), z) - x.org, p2 = vec3(e.P(u, va), z + rise) - x.org;
            vec3 nn = s ? X : -X;
            u32 i0 = x.m->addVertex(p0, nn, vec3(e.fr, 0.f), vec2(0, 0), wallCol, wallMat);
            u32 i1 = x.m->addVertex(p1, nn, vec3(e.fr, 0.f), vec2(td, 0), wallCol, wallMat);
            u32 i2 = x.m->addVertex(p2, nn, vec3(e.fr, 0.f), vec2(td, rise), wallCol, wallMat);
            if (dot(cross(p1 - p0, p2 - p0), nn) >= 0.f) x.m->tri(i0, i1, i2);
            else x.m->tri(i0, i2, i1);
        }
    }
}

// Butterfly roof: two planes falling to a valley along the long axis of the rectangle
void butterflyRoof(Ctx& x, const Env& e, float u0, float u1, float v0, float v1, float z, float over, u32 col) {
    bool alongU = (u1 - u0) >= (v1 - v0);
    float rise = 0.18f * (alongU ? (v1 - v0) : (u1 - u0));
    u0 -= over, u1 += over, v0 -= over, v1 += over;
    float um = (u0 + u1) * 0.5f, vm = (v0 + v1) * 0.5f;
    auto V = [&](float u, float v, float h) { return vec3(e.P(u, v), z + h) - x.org; };
    u32 m = makeMat(MAT_ROOF_METAL);
    if (alongU) {
        x.m->quadFacing(V(u0, v0, rise), V(u1, v0, rise), V(u1, vm, 0.f), V(u0, vm, 0.f), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), col, m, vec3(0, 0, 1));
        x.m->quadFacing(V(u0, vm, 0.f), V(u1, vm, 0.f), V(u1, v1, rise), V(u0, v1, rise), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), col, m, vec3(0, 0, 1));
        x.m->quadFacing(V(u0, v0, rise), V(u1, v0, rise), V(u1, vm, 0.f), V(u0, vm, 0.f), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), packRGBA8(0.92f, 0.92f, 0.9f, 1),
                        makeMat(MAT_PLASTER), vec3(0, 0, -1));
        x.m->quadFacing(V(u0, vm, 0.f), V(u1, vm, 0.f), V(u1, v1, rise), V(u0, v1, rise), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), packRGBA8(0.92f, 0.92f, 0.9f, 1),
                        makeMat(MAT_PLASTER), vec3(0, 0, -1));
    } else {
        x.m->quadFacing(V(u0, v0, rise), V(um, v0, 0.f), V(um, v1, 0.f), V(u0, v1, rise), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), col, m, vec3(0, 0, 1));
        x.m->quadFacing(V(um, v0, 0.f), V(u1, v0, rise), V(u1, v1, rise), V(um, v1, 0.f), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), col, m, vec3(0, 0, 1));
        x.m->quadFacing(V(u0, v0, rise), V(um, v0, 0.f), V(um, v1, 0.f), V(u0, v1, rise), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), packRGBA8(0.92f, 0.92f, 0.9f, 1),
                        makeMat(MAT_PLASTER), vec3(0, 0, -1));
        x.m->quadFacing(V(um, v0, 0.f), V(u1, v0, rise), V(u1, v1, rise), V(um, v1, 0.f), vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), packRGBA8(0.92f, 0.92f, 0.9f, 1),
                        makeMat(MAT_PLASTER), vec3(0, 0, -1));
    }
}

// The roof of a rectangle part of a volume for pitched forms (hip / gable), in the envelope frame
void pitchedPart(Ctx& x, const Env& e, vec4 r, float z, float pitch, bool hip, float over, u32 col, u32 mat, u32 gableCol, u32 gableMat, int ridge = -1) {
    float u0 = r.x, u1 = r.y, v0 = r.z, v1 = r.w;
    vec2 c = e.P((u0 + u1) * 0.5f, (v0 + v1) * 0.5f);
    pitchedRoof(x, c, e.ax, (u1 - u0) * 0.5f, (v1 - v0) * 0.5f, z, pitch, hip, over, col, mat, gableCol, gableMat, ridge);
}

// ------------------------------------------------------------------------------------------------ volumes
// Office block of an office-front warehouse (AR_WARE_OFFICE): width along the front, depth, side (+1 / -1 along ax)
inline float officeWidth(const Building& b) { return Clamp(2.f * b.hx * (0.35f + 0.2f * hashToFloat(hash32(b.seed ^ 0x0FF1CEu))), 6.f, Max(6.f, 2.f * b.hx - 8.f)); }
inline float officeDepth(const Building& b) { return Clamp(2.f * b.hy * 0.3f, 6.f, 10.f); }
inline float officeSide(const Building& b) { return (hash32(b.seed ^ 0x51DEu) & 1u) ? 1.f : -1.f; }

// Loading doors of an archetype warehouse: centres along ax (u) on the front face; returns the count
int archLoadingDoors(const Building& b, float* out, int maxN) {
    float ua = -b.hx, ub = b.hx;
    if (b.arch == AR_WARE_OFFICE) {
        float ow = officeWidth(b);
        if (officeSide(b) > 0.f) ub = b.hx - ow - 0.5f;
        else ua = -b.hx + ow + 0.5f;
    }
    float span = ub - ua;
    if (span < 4.5f) return 0;
    float pitch = b.arch == AR_WARE_BODYSHOP ? 5.5f : 14.f;
    int n = Clamp((int)(span / pitch), 1, maxN);
    for (int k = 0; k < n; k++) out[k] = ua + (k + 0.5f) * span / n;
    return n;
}

inline float arcadeDepth(const Building& b) { return Clamp(2.6f + 0.6f * hashToFloat(hash32(b.seed ^ 0xA2CADEu)), 2.6f, Max(2.6f, b.hy * 0.4f)); }

struct Plan {
    bool arcade = false;
    std::vector<Vol> vols;
    vec4 towerRect = vec4(0.f);   // corner tower footprint (u0, u1, v0, v1), zero if none
    float canopyZ = 0.f;          // MiMo entrance canopy height (0 none)
};

// Height of floor line k above the base (k = 0: ground)
inline float floorLine(const FacadeGPU& f, int k) { return k <= 0 ? 0.f : f.groundH + (k - 1) * f.floorH; }

Plan planVolumes(const Building& b, const FacadeGPU& fac, Rng& mr) {
    Plan P;
    const float hx = b.hx, hy = b.hy, z0 = b.baseZ, zTop = b.baseZ + b.height;
    const bool store = (fac.flags & 1u) != 0;
    const u32 f1 = b.facade, f2 = b.facade2 != 0xffffffffu ? b.facade2 : b.facade;
    const int floors = Max(1, (int)b.floors);
    auto vol = [&](std::vector<vec2> uv, std::vector<vec4> rects, float a, float c, u32 fac2, u8 kind, bool ground) {
        Vol v;
        v.uv = std::move(uv);
        v.rects = std::move(rects);
        v.z0 = a;
        v.z1 = c;
        v.facade = fac2;
        v.kind = kind;
        v.roof = b.roofForm;
        v.ground = ground;
        P.vols.push_back(v);
        return (int)P.vols.size() - 1;
    };
    auto full = [&](float zz0, float zz1, u32 f, u8 kind) {
        return vol(rectUV(-hx, hx, -hy, hy), {vec4(-hx, hx, -hy, hy)}, zz0, zz1, f, kind, true);
    };
    // the floor line at k floors (clamped inside the building)
    auto zAt = [&](int k) { return z0 + floorLine(fac, Clamp(k, 0, floors)); };
    bool cornerLeft = (b.archFlags & ABF_CORNER_LEFT) != 0;
    bool corner = (b.archFlags & ABF_CORNER) != 0;
    float side = corner ? (cornerLeft ? -1.f : 1.f) : (mr.chance(0.5f) ? -1.f : 1.f);
    if (b.arch == AR_SHOP_ARCADE && floors >= 2 && 2.f * hy > 12.f && b.massing != MK_CORNER_TOWER) {
        // Coral Gables arcade: the shopfronts recessed behind a colonnade, the upper floors carried over it
        float rec = arcadeDepth(b);
        float zg = zAt(1);
        vol(rectUV(-hx, hx, -hy, hy - rec), {vec4(-hx, hx, -hy, hy - rec)}, z0, zg, f1, FM_MAIN, true);
        int ui = vol(rectUV(-hx, hx, -hy, hy), {vec4(-hx, hx, -hy, hy)}, zg, zTop, f1, FM_TIER, false);
        (void)ui;
        P.arcade = true;
        return P;
    }
    if (b.arch == AR_WARE_OFFICE) {
        // a two-storey office block on one side of the street front, the shed wrapping round behind it
        float ow = officeWidth(b), od = officeDepth(b);
        float s = officeSide(b);
        float ua = s > 0.f ? hx - ow : -hx, ub = ua + ow;
        float vo = hy - od;
        float zo = Min(zTop - 0.8f, z0 + 7.4f);
        int oi = vol(rectUV(ua, ub, vo, hy), {vec4(ua, ub, vo, hy)}, z0, zo, f2, FM_PODIUM, true);
        P.vols[oi].roof = RFM_PARAPET;
        if (s > 0.f)
            vol({vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, vo), vec2(ua, vo), vec2(ua, hy), vec2(-hx, hy)}, {vec4(-hx, hx, -hy, vo), vec4(-hx, ua, vo, hy)}, z0, zTop, f1,
                FM_MAIN, true);
        else
            vol({vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(ub, hy), vec2(ub, vo), vec2(-hx, vo)}, {vec4(-hx, hx, -hy, vo), vec4(ub, hx, vo, hy)}, z0, zTop, f1,
                FM_MAIN, true);
        return P;
    }
    switch (b.massing) {
        case MK_L: {
            // a back corner cut away (away from the street; houses keep the garage side whole)
            float cw = Clamp(2.f * hx * mr.range(0.38f, 0.55f), 0.f, 2.f * hx - 6.f), cd = Clamp(2.f * hy * mr.range(0.35f, 0.55f), 0.f, 2.f * hy - 6.f);
            if (cw < 3.f || cd < 3.f) { full(z0, zTop, f1, FM_MAIN); break; }
            float s = -side;
            if (b.style == BS_HOUSE) s = (b.seed & 64u) ? -1.f : 1.f;   // (garage wing on the +1 side when seed & 64)
            if (s > 0.f) {
                float uc = hx - cw, vc = -hy + cd;
                vol({vec2(-hx, -hy), vec2(uc, -hy), vec2(uc, vc), vec2(hx, vc), vec2(hx, hy), vec2(-hx, hy)}, {vec4(-hx, uc, -hy, hy), vec4(uc, hx, vc, hy)}, z0, zTop, f1,
                    FM_MAIN, true);
            } else {
                float uc = -hx + cw, vc = -hy + cd;
                vol({vec2(-hx, vc), vec2(uc, vc), vec2(uc, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(-hx, hy)}, {vec4(uc, hx, -hy, hy), vec4(-hx, uc, vc, hy)}, z0, zTop, f1,
                    FM_MAIN, true);
            }
            break;
        }
        case MK_U: {
            // a notch in the middle: open to the back behind storefronts, open to the street for garden apartments
            float nw = Clamp(2.f * hx * mr.range(0.32f, 0.45f), 0.f, 2.f * hx - 12.f), nd = Clamp(2.f * hy * mr.range(0.4f, 0.6f), 0.f, 2.f * hy - 6.f);
            if (nw < 4.f || nd < 4.f) { full(z0, zTop, f1, FM_MAIN); break; }
            float off = mr.range(-0.15f, 0.15f) * (2.f * hx - nw - 12.f);
            float ua = off - nw * 0.5f, ub = off + nw * 0.5f;
            bool openFront = !store && b.arch == AR_MID_MIMO;
            if (!openFront) {
                float vc = -hy + nd;
                vol({vec2(-hx, -hy), vec2(ua, -hy), vec2(ua, vc), vec2(ub, vc), vec2(ub, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(-hx, hy)},
                    {vec4(-hx, ua, -hy, hy), vec4(ub, hx, -hy, hy), vec4(ua, ub, vc, hy)}, z0, zTop, f1, FM_MAIN, true);
            } else {
                float vc = hy - nd;
                vol({vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(ub, hy), vec2(ub, vc), vec2(ua, vc), vec2(ua, hy), vec2(-hx, hy)},
                    {vec4(-hx, ua, -hy, hy), vec4(ub, hx, -hy, hy), vec4(ua, ub, -hy, vc)}, z0, zTop, f1, FM_MAIN, true);
            }
            break;
        }
        case MK_COURT: {
            float wg = mr.range(6.5f, 9.f);
            float iu = hx - wg, iv = hy - wg;
            if (iu < 2.5f || iv < 2.5f) { full(z0, zTop, f1, FM_MAIN); break; }
            int vi = vol(rectUV(-hx, hx, -hy, hy), {vec4(-hx, hx, iv, hy), vec4(-hx, hx, -hy, -iv), vec4(-hx, -iu, -iv, iv), vec4(iu, hx, -iv, iv)}, z0, zTop, f1, FM_MAIN, true);
            P.vols[vi].hole = rectUV(-iu, iu, -iv, iv);
            break;
        }
        case MK_STEP_FRONT: {
            // shops (or an office block) low along the street, the body set back behind them
            int low = Clamp(mr.irange(1, 2), 1, Max(1, floors - 1));
            float zl = zAt(low);
            float sb = Clamp(2.f * hy * mr.range(0.3f, 0.45f), 4.f, 2.f * hy - 8.f);
            if (floors < 2 || 2.f * hy < 14.f) { full(z0, zTop, f1, FM_MAIN); break; }
            u32 fb = b.facade2 != 0xffffffffu ? f2 : f1;   // the low front band's own cladding
            full(z0, zl, fb, FM_PODIUM);
            P.vols.back().top = true;
            vol(rectUV(-hx, hx, -hy, hy - sb), {vec4(-hx, hx, -hy, hy - sb)}, zl, zTop, f1, FM_TIER, false);
            break;
        }
        case MK_STEP_BACK: {
            // full height on the street, a lower rear wing
            int low = Clamp(floors - mr.irange(1, 3), 1, Max(1, floors - 1));
            if (floors < 2) {
                // one storey: the rear wing simply lower (shorter ground floor) - a step in the roofline behind the parapet
                float vb = -hy + 2.f * hy * mr.range(0.35f, 0.5f);
                full(z0, zTop - Min(1.2f, b.height * 0.25f), f1, FM_MAIN);
                vol(rectUV(-hx, hx, vb, hy), {vec4(-hx, hx, vb, hy)}, zTop - Min(1.2f, b.height * 0.25f), zTop, f1, FM_TIER, false);
                break;
            }
            float zl = zAt(low);
            float vb = -hy + 2.f * hy * mr.range(0.35f, 0.55f);
            full(z0, zl, f1, FM_MAIN);
            vol(rectUV(-hx, hx, vb, hy), {vec4(-hx, hx, vb, hy)}, zl, zTop, f1, FM_TIER, false);
            break;
        }
        case MK_SPLIT: {
            // two buildings on one lot: different heights, the second in its own cladding
            float us = -hx + 2.f * hx * mr.range(0.38f, 0.62f);
            if (us + hx < 4.f || hx - us < 4.f) { full(z0, zTop, f1, FM_MAIN); break; }
            int other = floors > 1 ? Max(1, floors - mr.irange(1, 2)) : 1;
            float zo = floors > 1 ? zAt(other) : zTop - Min(1.4f, b.height * 0.3f);
            bool tallLeft = mr.chance(0.5f);
            vol(rectUV(-hx, us, -hy, hy), {vec4(-hx, us, -hy, hy)}, z0, tallLeft ? zTop : zo, tallLeft ? f1 : f2, FM_MAIN, true);
            vol(rectUV(us, hx, -hy, hy), {vec4(us, hx, -hy, hy)}, z0, tallLeft ? zo : zTop, tallLeft ? f2 : f1, FM_MAIN, true);
            break;
        }
        case MK_CORNER_TOWER: {
            full(z0, zTop, f1, FM_MAIN);
            float ts = Clamp(Min(2.f * hx, 2.f * hy) * mr.range(0.24f, 0.34f), 3.6f, 7.f);
            int extra = mr.irange(1, 2);
            float zt = zTop + (extra == 1 ? Max(fac.floorH, 2.8f) : 2.f * Max(fac.floorH, 2.8f));
            float u0 = side > 0.f ? hx - ts : -hx, u1 = u0 + ts;
            int ti = vol(rectUV(u0, u1, hy - ts, hy), {vec4(u0, u1, hy - ts, hy)}, zTop, zt, f1, FM_WING, false);
            P.vols[ti].roof = b.roofForm == RFM_EAVE ? RFM_EAVE : (b.roofForm == RFM_PARAPET ? RFM_PARAPET : RFM_TILE_HIP);
            P.vols[0].top = true;
            P.towerRect = vec4(u0, u1, hy - ts, hy);
            break;
        }
        case MK_PODIUM_SLAB: {
            int pf = Clamp(mr.irange(2, floors > 16 ? 6 : 4), 1, Max(1, floors - 2));
            float zp = zAt(pf);
            float fi = Clamp(mr.range(3.f, 9.f), 2.f, Max(2.f, 2.f * hy - 14.f));
            float bi = Clamp(mr.range(0.f, 4.f), 0.f, Max(0.f, 2.f * hy - fi - 12.f));
            float si = Clamp(mr.range(0.f, 5.f), 0.f, Max(0.f, hx - 8.f));
            if (2.f * hy - fi - bi < 10.f || floors < 4) { full(z0, zTop, f1, FM_MAIN); break; }
            full(z0, zp, f2, FM_PODIUM);
            vol(rectUV(-hx + si, hx - si, -hy + bi, hy - fi), {vec4(-hx + si, hx - si, -hy + bi, hy - fi)}, zp, zTop, f1, FM_TIER, false);
            break;
        }
        case MK_CURVE: {
            // a bowed front: concave toward the street (the resort-hotel curve) or convex, inside the envelope
            float sag = Clamp(mr.range(2.5f, 6.f), 1.f, hy * 0.6f);
            bool concave = mr.chance(0.6f);
            std::vector<vec2> uv = {vec2(-hx, -hy), vec2(hx, -hy)};
            const int seg = 10;
            for (int k = 0; k <= seg; k++) {
                float t = (float)k / seg;
                float u = Lerp(hx, -hx, t);
                float bulge = 4.f * t * (1.f - t);   // 0 at the ends, 1 in the middle
                float v = concave ? hy - sag * bulge : hy - sag * (1.f - bulge);
                uv.push_back(vec2(u, v));
            }
            // rectangles under the curve for collision
            float vmid = hy - sag;
            vol(uv, {vec4(-hx, hx, -hy, vmid), vec4(-hx, -hx * 0.55f, vmid, concave ? hy - sag * 0.2f : hy - sag * 0.75f),
                     vec4(hx * 0.55f, hx, vmid, concave ? hy - sag * 0.2f : hy - sag * 0.75f)},
                z0, zTop, f1, FM_MAIN, true);
            break;
        }
        case MK_WINGS: {
            // a taller middle block with lower wings either side
            int low = Clamp(floors - mr.irange(1, 3), 1, Max(1, floors - 1));
            float zl = floors > 1 ? zAt(low) : zTop;
            float mw = hx * mr.range(0.35f, 0.55f);
            if (floors < 2) { full(z0, zTop, f1, FM_MAIN); break; }
            full(z0, zl, f1, FM_MAIN);
            vol(rectUV(-mw, mw, -hy, hy), {vec4(-mw, mw, -hy, hy)}, zl, zTop, f1, FM_TIER, false);
            break;
        }
        case MK_CHAMFER: {
            // the corner on the cross street cut at 45 degrees, full height
            float ch = Clamp(Min(hx, hy) * mr.range(0.3f, 0.45f), 2.f, 4.5f);
            bool left = corner ? cornerLeft : side < 0.f;
            if (left)
                vol({vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(-hx + ch, hy), vec2(-hx, hy - ch)}, {vec4(-hx, hx, -hy, hy - ch), vec4(-hx + ch, hx, hy - ch, hy)}, z0, zTop,
                    f1, FM_MAIN, true);
            else
                vol({vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, hy - ch), vec2(hx - ch, hy), vec2(-hx, hy)}, {vec4(-hx, hx, -hy, hy - ch), vec4(-hx, hx - ch, hy - ch, hy)}, z0, zTop,
                    f1, FM_MAIN, true);
            break;
        }
        case MK_ROUNDED: {
            // streamline moderne: rounded street corners (both, or the cross-street one on a corner lot)
            float R = Clamp(Min(hx, hy) * mr.range(0.35f, 0.55f), 2.f, 6.f);
            bool rl = !corner || cornerLeft, rr = !corner || !cornerLeft;
            const int seg = 6;
            std::vector<vec2> uv = {vec2(-hx, -hy), vec2(hx, -hy)};
            if (rr)
                for (int k = 0; k <= seg; k++) {
                    float a = kPi * 0.5f * k / seg;
                    uv.push_back(vec2(hx - R + R * cosf(a), hy - R + R * sinf(a)));
                }
            else uv.push_back(vec2(hx, hy));
            if (rl)
                for (int k = 0; k <= seg; k++) {
                    float a = kPi * 0.5f + kPi * 0.5f * k / seg;
                    uv.push_back(vec2(-hx + R + R * cosf(a), hy - R + R * sinf(a)));
                }
            else uv.push_back(vec2(-hx, hy));
            vol(uv, {vec4(-hx, hx, -hy, hy - R), vec4(rl ? -hx + R : -hx, rr ? hx - R : hx, hy - R, hy)}, z0, zTop, f1, FM_MAIN, true);
            break;
        }
        default: full(z0, zTop, f1, FM_MAIN); break;
    }
    // volumes with something on their roof
    for (size_t i = 0; i < P.vols.size(); i++)
        for (size_t j = 0; j < P.vols.size(); j++)
            if (i != j && fabsf(P.vols[j].z0 - P.vols[i].z1) < 0.05f) P.vols[i].top = false;
    // a MiMo entrance canopy over the frontage
    if ((b.arch == AR_SHOP_MIMO || b.arch == AR_CONDO_MIMO || b.arch == AR_MID_OFFICE60) && mr.chance(b.arch == AR_MID_OFFICE60 ? 0.4f : 0.8f))
        P.canopyZ = z0 + Min(fac.groundH - 0.5f, b.arch == AR_CONDO_MIMO ? 4.6f : 3.7f);
    return P;
}

// Emits a volume: facade walls (with the courtyard walls), roof form, collision, the facade mass for the detail pass
void emitVolume(Ctx& x, const Building& b, const Env& e, const Vol& v, const FacadeGPU& fac, Rng& dr) {
    const float bay = (gBuildings && v.facade < gBuildings->facades.size()) ? gBuildings->facades[v.facade].bayW : fac.bayW;
    std::vector<vec2> fp = toWorld(e, v.uv);
    std::vector<vec2> hole;
    if (!v.hole.empty()) {
        hole = toWorld(e, v.hole);
        std::reverse(hole.begin(), hole.end());   // clockwise: its walls face into the courtyard
    }
    u8 rf = v.roof;
    bool pitched = rf == RFM_TILE_HIP || rf == RFM_METAL_GABLE || rf == RFM_SAWTOOTH || rf == RFM_BUTTERFLY;
    if (!v.top && pitched) rf = RFM_PARAPET;   // (a lower volume under a taller one keeps a flat roof)
    pitched = rf == RFM_TILE_HIP || rf == RFM_METAL_GABLE || rf == RFM_SAWTOOTH || rf == RFM_BUTTERFLY;
    bool parapetOn = rf == RFM_PARAPET || rf == RFM_TILE_PENT || rf == RFM_TERRACE;
    float skirt = v.ground ? 3.f : 0.f;
    const u32 roofGray = packRGBA8(0.9f, 0.9f, 0.88f, 1);
    recordMass(x, fp, v.z0, v.z1, b.baseZ, (u8)v.kind, parapetOn, v.facade);
    facadeWalls(x, fp, v.z0 - skirt, v.z1 + (parapetOn ? 1.0f : 0.f), b.baseZ, v.facade, bay);
    if (!hole.empty()) {
        recordMass(x, hole, v.z0, v.z1, b.baseZ, FM_WING, parapetOn, v.facade);
        facadeWalls(x, hole, v.z0 - skirt, v.z1 + (parapetOn ? 1.0f : 0.f), b.baseZ, v.facade, bay);
    }
    // roof
    u32 tile = b.roofTint;
    vec4 wc = unpackRGBA8(fac.wallColor);
    u32 wallTone = packRGBA8(Saturate(wc.x * 1.5f), Saturate(wc.y * 1.5f), Saturate(wc.z * 1.5f), 1.f);
    if (!hole.empty() && !pitched) {
        // ring roof between the outer outline and the courtyard (each covering rectangle)
        for (const vec4& r : v.rects) flatRoof(x, toWorld(e, rectUV(r.x, r.y, r.z, r.w)), v.z1 + 0.02f, x.roofCol, x.roofMat);
        if (parapetOn && x.detail) {
            parapet(x, fp, v.z1, 1.0f, 0.3f, packRGBA8(0.8f, 0.8f, 0.78f, 1), makeMat(MAT_CONCRETE));
            parapet(x, hole, v.z1, 1.0f, 0.3f, packRGBA8(0.8f, 0.8f, 0.78f, 1), makeMat(MAT_CONCRETE));
        }
    } else {
        switch (rf) {
            case RFM_EAVE: eaveSlab(x, fp, v.z1, b.style == BS_HOUSE || b.style == BS_VILLA ? 0.9f : 0.75f, 0.28f, wallTone); break;
            case RFM_TILE_HIP:
                for (const vec4& r : v.rects) pitchedPart(x, e, r, v.z1, 0.36f, true, 0.45f, tile, makeMat(MAT_ROOF_TILE), wallTone, makeMat(MAT_PLASTER));
                break;
            case RFM_METAL_GABLE:
                for (const vec4& r : v.rects) pitchedPart(x, e, r, v.z1, 0.5f, false, 0.4f, tile, makeMat(MAT_ROOF_METAL), wallTone, makeMat((u32)fac.wallLayer));
                break;
            case RFM_SAWTOOTH:
                for (const vec4& r : v.rects) sawtoothRoof(x, e, r.x, r.y, r.z, r.w, v.z1, packRGBA8(0.7f, 0.71f, 0.72f, 1), wallTone, makeMat((u32)fac.wallLayer));
                break;
            case RFM_BUTTERFLY:
                for (const vec4& r : v.rects) butterflyRoof(x, e, r.x, r.y, r.z, r.w, v.z1, 0.8f, packRGBA8(0.85f, 0.86f, 0.87f, 1));
                break;
            default:
                flatRoof(x, fp, v.z1 + 0.02f, x.roofCol, x.roofMat);
                if (parapetOn && x.detail) parapet(x, fp, v.z1, rf == RFM_TERRACE ? 0.6f : 1.0f, 0.3f, packRGBA8(0.8f, 0.8f, 0.78f, 1), makeMat(MAT_CONCRETE));
                break;
        }
        // tile pent roof along the street front (and the cross-street side of a corner lot)
        if (rf == RFM_TILE_PENT) {
            int n = (int)fp.size();
            for (int i = 0; i < n; i++) {
                vec2 a = fp[i], c = fp[(i + 1) % n];
                vec2 t = c - a;
                float len = length(t);
                if (len < 2.f) continue;
                vec2 nn(t.y / len, -t.x / len);
                bool front = dot(nn, b.front) > 0.9f;
                bool sideStreet = (b.archFlags & ABF_CORNER) && fabsf(dot(nn, b.ax)) > 0.9f && dot(nn, b.ax) * ((b.archFlags & ABF_CORNER_LEFT) ? -1.f : 1.f) > 0.f;
                if (front || sideStreet) pentRoof(x, a, c, v.z1 + 1.0f, 0.75f, tile);
            }
        }
    }
    // collision per covering rectangle
    for (const vec4& r : v.rects) {
        vec2 cc = e.P((r.x + r.y) * 0.5f, (r.z + r.w) * 0.5f);
        addCollision(x, cc, e.ax, (r.y - r.x) * 0.5f, (r.w - r.z) * 0.5f, v.z0 - (v.ground ? 3.f : 0.f), v.z1 + (pitched ? 1.5f : 0.f));
    }
    (void)dr;
}

// Roof terrace: a pergola, planters and umbrellas on part of the top roof; glass railing on the low parapet
void roofTerrace(Ctx& x, const Env& e, const vec4& r, float z, Rng& dr) {
    if (!x.detail) return;
    float u0 = r.x + 1.f, u1 = r.y - 1.f, v0 = r.z + 1.f, v1 = r.w - 1.f;
    if (u1 - u0 < 4.f || v1 - v0 < 4.f) return;
    vec3 X(e.ax, 0.f), Y(perp(e.ax), 0.f), Z(0, 0, 1);
    // decking
    float du0 = u0, du1 = Lerp(u0, u1, dr.range(0.45f, 0.75f)), dv0 = v0, dv1 = v1;
    flatRoof(x, toWorld(e, rectUV(du0, du1, dv0, dv1)), z + 0.06f, packRGBA8(0.75f, 0.6f, 0.45f, 1), makeMat(MAT_WOOD));
    // pergola over part of it
    float pu0 = du0 + 0.5f, pu1 = Min(du1 - 0.5f, pu0 + dr.range(3.5f, 6.f)), pv0 = dv0 + 0.5f, pv1 = Min(dv1 - 0.5f, pv0 + dr.range(3.f, 5.f));
    u32 wood = packRGBA8(0.45f, 0.33f, 0.22f, 1), wm = makeMat(MAT_WOOD);
    for (int k = 0; k < 4; k++) {
        vec2 p = e.P((k & 1) ? pu1 : pu0, (k & 2) ? pv1 : pv0);
        x.m->box(vec3(p, z + 1.25f) - x.org, X, Y, Z, vec3(0.08f, 0.08f, 1.25f), wood, wm);
    }
    for (float u = pu0; u <= pu1 + 0.01f; u += 0.6f) {
        vec2 p = e.P(u, (pv0 + pv1) * 0.5f);
        x.m->box(vec3(p, z + 2.55f) - x.org, X, Y, Z, vec3(0.04f, (pv1 - pv0) * 0.5f + 0.25f, 0.08f), wood, wm);
    }
    // planters with shrubs along one edge, umbrellas over tables elsewhere
    int np = dr.irange(2, 4);
    for (int k = 0; k < np; k++) {
        float u = Lerp(du0 + 0.6f, du1 - 0.6f, (k + 0.5f) / np);
        vec2 p = e.P(u, dv1 - 0.35f);
        x.m->box(vec3(p, z + 0.35f) - x.org, X, Y, Z, vec3(0.5f, 0.3f, 0.35f), packRGBA8(0.75f, 0.74f, 0.7f, 1), makeMat(MAT_CONCRETE));
        leafBlobEx(*x.m, x.org, vec3(p, z + 0.95f), X, Y, vec3(0.5f, 0.32f, 0.35f), vec3(0.6f, 0.95f, 0.45f), dr.next(), 0.05f, 8, 5, vec3(-1.f));
    }
    if (x.props && (u1 - du1) > 3.f) {
        int nu = dr.irange(1, 3);
        for (int k = 0; k < nu; k++) {
            PropInstance pi;
            vec2 p = e.P(Lerp(du1 + 1.2f, u1 - 1.f, (k + 0.5f) / nu), Lerp(v0 + 1.f, v1 - 1.f, dr.f()));
            pi.pos = vec3(p, z);
            pi.yaw = dr.f() * kTwoPi;
            pi.scale = 0.85f;
            pi.type = PROP_UMBRELLA;
            pi.variant = (u8)dr.irange(0, 3);
            pi.flags = 0;
            x.props->push_back(pi);
        }
    }
}

// MiMo entrance canopy: a thin cantilevered slab across the frontage, its edge raked
void mimoCanopy(Ctx& x, const Building& b, const Env& e, float z, Rng& mr) {
    float w = Min(b.hx * mr.range(0.6f, 0.95f), b.hx - 0.3f), dep = mr.range(1.8f, 2.8f);
    float off = (b.archFlags & ABF_CORNER) ? ((b.archFlags & ABF_CORNER_LEFT) ? -1.f : 1.f) * (b.hx - w) : mr.range(-1.f, 1.f) * (b.hx - w);
    vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
    vec2 c = e.P(off, b.hy + dep * 0.5f);
    vec4 wc = unpackRGBA8(gBuildings ? gBuildings->facades[b.facade].frameColor : 0xffffffffu);
    u32 edge = (b.archFlags & ABF_ACCENT) ? packRGBA8(wc.x, wc.y, wc.z, 1) : packRGBA8(0.96f, 0.96f, 0.95f, 1);
    x.m->box(vec3(c, z + 0.12f) - x.org, X, Y, Z, vec3(w, dep * 0.5f, 0.12f), packRGBA8(0.95f, 0.95f, 0.94f, 1), makeMat(MAT_PLASTER), true);
    // raked fascia band on the edge (taller at one end: the "atomic" wedge)
    vec2 a0 = e.P(off - w, b.hy + dep), a1 = e.P(off + w, b.hy + dep);
    float h0 = 0.35f, h1 = mr.chance(0.5f) ? 0.9f : 0.35f;
    x.m->quadFacing(vec3(a0, z - 0.02f) - x.org, vec3(a1, z - 0.02f) - x.org, vec3(a1, z + h1) - x.org, vec3(a0, z + h0) - x.org, vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), edge,
                    makeMat(MAT_PLASTER), vec3(e.fr, 0.f));
    // (cantilevered: no posts, which would stand on the sidewalk)
}

}  // namespace massing

// Archetype building (non-house): volumes, roofs, rooftop dressing, canopies. Returns false to fall back to the plain
// generator (no archetype, interiors).
bool buildArchMesh(Ctx& x, const Building& b, const FacadeGPU& fac) {
    using namespace massing;
    if (b.arch == AR_NONE || b.interior >= 0) return false;
    if (b.style == BS_HOUSE || b.style == BS_VILLA || b.style == BS_FARMHOUSE || b.style == BS_SHACK) return false;   // houses: buildArchHouse
    Rng mr(b.seed ^ 0x4D455353u);
    Env e = envOf(b);
    Plan P = planVolumes(b, fac, mr);
    Rng dr(b.seed ^ 0xD37A11u);
    for (const Vol& v : P.vols) emitVolume(x, b, e, v, fac, dr);
    // rooftop dressing on the open roofs (water tanks on the older fabric; nothing on back-yard garages and sheds)
    bool old = (b.archFlags & ABF_OLD) != 0;
    bool rear = b.arch == AR_REAR_COTTAGE || b.arch == AR_REAR_GARAGE || b.arch == AR_REAR_SHED;
    for (const Vol& v : P.vols) {
        if (!v.top || v.kind == FM_WING || rear) continue;
        u8 rf = v.roof;
        if (rf == RFM_TILE_HIP || rf == RFM_METAL_GABLE || rf == RFM_SAWTOOTH || rf == RFM_BUTTERFLY) continue;
        // the largest covering rectangle
        vec4 best = v.rects[0];
        for (const vec4& r : v.rects)
            if ((r.y - r.x) * (r.w - r.z) > (best.y - best.x) * (best.w - best.z)) best = r;
        vec2 rc = e.P((best.x + best.y) * 0.5f, (best.z + best.w) * 0.5f);
        float zr = v.z1 + (rf == RFM_EAVE ? 0.3f : 0.f);
        if (rf == RFM_TERRACE) roofTerrace(x, e, best, v.z1, dr);
        else rooftopClutter(x, rc, b.ax, (best.y - best.x) * 0.45f, (best.w - best.z) * 0.45f, zr, dr, old ? 0.35f : 0.06f);
    }
    if (P.canopyZ > 0.f) mimoCanopy(x, b, e, P.canopyZ, mr);
    if (P.arcade) {
        // the arcade: its ceiling under the upper floors, piers on the street line (arches by the detail pass)
        float rec = arcadeDepth(b), zg = P.vols[1].z0;
        std::vector<vec2> ceil = toWorld(e, rectUV(-b.hx, b.hx, b.hy - rec, b.hy));
        std::vector<vec3> poly;
        for (int i = (int)ceil.size() - 1; i >= 0; i--) poly.push_back(vec3(ceil[i], zg) - x.org);
        x.m->polygon(poly, vec3(0, 0, -1), packRGBA8(0.93f, 0.9f, 0.85f, 1), makeMat(MAT_PLASTER), 1.f);
        const FacadeGPU& f = fac;
        int bays = Max(1, (int)roundf(2.f * b.hx / Max(f.bayW, 0.5f)));
        float bw = 2.f * b.hx / bays;
        vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
        vec4 wc = unpackRGBA8(f.wallColor);
        u32 pierCol = packRGBA8(Saturate(wc.x * 1.5f), Saturate(wc.y * 1.5f), Saturate(wc.z * 1.5f), 1);
        for (int k = 0; k <= bays; k++) {
            float u = -b.hx + k * bw;
            u = Clamp(u, -b.hx + 0.3f, b.hx - 0.3f);
            vec2 p = e.P(u, b.hy - 0.3f);
            x.m->box(vec3(p, (b.baseZ + zg) * 0.5f) - x.org, X, Y, Z, vec3(0.3f, 0.3f, (zg - b.baseZ) * 0.5f), pierCol, makeMat((u32)f.wallLayer));
            addCollision(x, p, e.ax, 0.3f, 0.3f, b.baseZ, zg);
        }
    }
    // back of the block: lock-up garage doors along the yard side, a shed's double door, a back house's door and lamp
    if (rear && x.detail) {
        vec3 X(b.ax, 0.f), Y(b.front, 0.f);
        vec2 fc = b.c + b.front * (b.hy + 0.03f);
        if (b.arch == AR_REAR_GARAGE) {
            int nd = Max(1, (int)(2.f * b.hx / 3.3f));
            float pitch = 2.f * b.hx / nd;
            const vec3 doorPal[] = {vec3(0.85f, 0.85f, 0.83f), vec3(0.5f, 0.55f, 0.6f), vec3(0.6f, 0.3f, 0.25f), vec3(0.3f, 0.45f, 0.35f)};
            for (int k = 0; k < nd; k++) {
                float u = -b.hx + (k + 0.5f) * pitch;
                vec3 dc = doorPal[(b.seed + (u32)k * 7u) % 4u] * (0.85f + 0.15f * hashToFloat(hash32(b.seed + (u32)k)));
                vec2 d0 = fc + b.ax * (u - pitch * 0.4f), d1 = fc + b.ax * (u + pitch * 0.4f);
                float dh = Min(2.3f, b.height - 0.5f);
                x.m->quadFacing(vec3(d0, b.baseZ) - x.org, vec3(d1, b.baseZ) - x.org, vec3(d1, b.baseZ + dh) - x.org, vec3(d0, b.baseZ + dh) - x.org, vec2(0, 0),
                                vec2(pitch * 0.8f, 0), vec2(pitch * 0.8f, dh), vec2(0, dh), packRGBA8(dc.x, dc.y, dc.z, 1), makeMat(MAT_CORRUGATED), vec3(b.front, 0));
            }
        } else {
            float dw = b.arch == AR_REAR_SHED ? Min(2.4f, b.hx) : 0.95f, dh = b.arch == AR_REAR_SHED ? Min(2.4f, b.height - 0.3f) : 2.1f;
            float u = (hash32(b.seed ^ 0xD00Du) & 1u) ? b.hx * 0.4f : -b.hx * 0.4f;
            vec2 d0 = fc + b.ax * (u - dw * 0.5f), d1 = fc + b.ax * (u + dw * 0.5f);
            u32 dcol = b.arch == AR_REAR_SHED ? packRGBA8(0.45f, 0.35f, 0.25f, 1) : packRGBA8(0.2f, 0.3f, 0.45f, 1);
            x.m->quadFacing(vec3(d0, b.baseZ) - x.org, vec3(d1, b.baseZ) - x.org, vec3(d1, b.baseZ + dh) - x.org, vec3(d0, b.baseZ + dh) - x.org, vec2(0, 0), vec2(dw, 0),
                            vec2(dw, dh), vec2(0, dh), dcol, makeMat(MAT_WOOD), vec3(b.front, 0));
            if (b.arch == AR_REAR_COTTAGE && x.lights) {
                LightInstance li;
                li.pos = vec3(fc + b.ax * (u + dw * 0.5f + 0.4f) + b.front * 0.3f, b.baseZ + 2.3f);
                li.color = vec3(1.f, 0.75f, 0.45f) * 300.f;
                li.radius = 7.f;
                li.dir = vec3(0);
                li.cone = 0;
                li.type = 1;
                x.lights->push_back(li);
            }
        }
    }
    // industry: roll-up loading doors on the shed front, chimneys and a tank at factories
    if ((b.style == BS_WAREHOUSE || b.style == BS_FACTORY) && x.detail) {
        float du[8];
        int nd = archLoadingDoors(b, du, 8);
        float dh = Min(4.2f, b.height - 1.2f);
        for (int k = 0; k < nd; k++) {
            vec2 d0 = b.c + b.front * (b.hy + 0.03f) + b.ax * (du[k] - 1.8f), d1 = d0 + b.ax * 3.6f;
            x.m->quadFacing(vec3(d0, b.baseZ + 0.2f) - x.org, vec3(d1, b.baseZ + 0.2f) - x.org, vec3(d1, b.baseZ + dh) - x.org, vec3(d0, b.baseZ + dh) - x.org, vec2(0, 0),
                            vec2(3.6f, 0), vec2(3.6f, dh), vec2(0, dh), packRGBA8(0.6f, 0.62f, 0.65f, 1), makeMat(MAT_CORRUGATED), vec3(b.front, 0));
        }
        if (b.style == BS_FACTORY) {
            int nc = dr.irange(1, 2);
            for (int k = 0; k < nc; k++) {
                vec2 cp = b.c + b.ax * dr.range(-b.hx * 0.6f, b.hx * 0.6f) - b.front * dr.range(0.f, b.hy * 0.5f);
                x.m->cylinder(vec3(cp, b.baseZ + b.height) - x.org, 1.3f, 0.95f, dr.range(12.f, 26.f), 12, packRGBA8(0.55f, 0.4f, 0.35f, 1), makeMat(MAT_BRICK), false);
            }
        }
    }
    return true;
}

}  // namespace World

namespace World {

namespace massing {

// House roof finish by archetype: tile, shingle or metal, and its colour (deterministic from the seed)
void houseRoofFinish(const Building& b, Rng& hr, u32& col, u32& mat) {
    const vec3 shingle[] = {vec3(0.55f, 0.55f, 0.56f), vec3(0.78f, 0.7f, 0.6f), vec3(0.5f, 0.45f, 0.42f), vec3(0.9f, 0.9f, 0.88f), vec3(0.65f, 0.6f, 0.52f),
                            vec3(0.45f, 0.5f, 0.56f), vec3(0.62f, 0.5f, 0.42f)};
    const vec3 metal[] = {vec3(0.95f, 0.96f, 0.97f), vec3(0.8f, 0.81f, 0.83f), vec3(0.62f, 0.3f, 0.25f), vec3(0.4f, 0.56f, 0.5f), vec3(0.38f, 0.48f, 0.62f)};
    float p = hr.f();
    int kind;   // 0 tile, 1 shingle, 2 metal
    switch (b.arch) {
        case AR_HOUSE_MED: kind = 0; break;
        case AR_HOUSE_RANCH: kind = p < 0.5f ? 0 : 1; break;
        case AR_HOUSE_BUNGALOW: kind = p < 0.55f ? 1 : (p < 0.85f ? 2 : 0); break;
        case AR_HOUSE_TWO: kind = p < 0.7f ? 1 : 0; break;
        case AR_HOUSE_SPLIT: kind = p < 0.6f ? 1 : 0; break;
        case AR_HOUSE_CONCH: kind = 2; break;
        case AR_HOUSE_MIMO: kind = 2; break;
        case AR_VILLA_MED: kind = 0; break;
        case AR_VILLA_COLONIAL: kind = p < 0.55f ? 1 : 0; break;
        case AR_SHACK_STILT: kind = 2; break;
        default: kind = 0; break;
    }
    if (kind == 0) {
        float t = hr.range(0.85f, 1.12f);
        col = packRGBA8(Saturate(t), Saturate(t * hr.range(0.88f, 1.02f)), Saturate(t * hr.range(0.9f, 1.f)), 1);
        mat = makeMat(MAT_ROOF_TILE);
    } else if (kind == 1) {
        vec3 c = shingle[hr.next() % 7] * hr.range(0.9f, 1.1f);
        col = packRGBA8(Saturate(c.x), Saturate(c.y), Saturate(c.z), 1);
        mat = makeMat(MAT_ROOF_SHINGLE);
    } else {
        vec3 c = metal[hr.next() % 5];
        if ((b.arch == AR_HOUSE_CONCH || b.arch == AR_SHACK_STILT) && hr.chance(0.6f)) c = metal[hr.next() % 2];
        // (old tin on the fishing shacks: a rust brown)
        if (b.arch == AR_SHACK_STILT && hr.chance(0.2f)) c = vec3(0.52f, 0.36f, 0.26f);
        col = packRGBA8(c.x, c.y, c.z, 1);
        mat = makeMat(MAT_ROOF_METAL);
    }
}

// Steps from a raised deck (top zTop, front edge at v = v0 facing +v) down to the ground: treads with colliders, each
// rise under the walkers' step height, centred on um, w wide
void deckSteps(Ctx& x, const Env& e, float um, float w, float v0, float zTop, float zGround, u32 col, u32 mat) {
    float rise = zTop - zGround;
    if (rise < 0.22f) return;
    int n = Max(1, (int)ceilf(rise / 0.2f) - 1);
    float h = rise / (n + 1), run = 0.28f;
    vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
    for (int k = 1; k <= n; k++) {
        float zt = zTop - k * h, va = v0 + (k - 1) * run;
        vec2 c = e.P(um, va + run * 0.5f);
        x.m->box(vec3(c, (zt + zGround - 0.25f) * 0.5f) - x.org, X, Y, Z, vec3(w * 0.5f, run * 0.5f, (zt - zGround + 0.25f) * 0.5f), col, mat);
        addCollision(x, c, e.ax, w * 0.5f, run * 0.5f, zGround - 0.6f, zt);
    }
}

// Porch on posts in front of the house: roof (0 small front gable, 1 shed, 2 none: a gallery stands on it), posts,
// deck; u0..u1 along u at the front. A deck raised above zGround (conch houses on piers) is walkable, with steps down
// at stepU (the middle when NaN-free default) unless steps is false.
void housePorch(Ctx& x, const Env& e, float hy, float u0, float u1, float dep, float zb, float zEave, int roofKind, u32 col, u32 mat, u32 trim, bool rail,
                float zGround = -1e9f, bool steps = true) {
    vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
    float um = (u0 + u1) * 0.5f, hw = (u1 - u0) * 0.5f;
    vec2 c = e.P(um, hy + dep * 0.5f);
    // deck
    x.m->box(vec3(c, zb - 0.12f) - x.org, X, Y, Z, vec3(hw, dep * 0.5f, 0.14f), packRGBA8(0.75f, 0.73f, 0.7f, 1), makeMat(roofKind != 0 ? MAT_WOOD : MAT_CONCRETE), true);
    if (zGround > -1e8f && zb - zGround > 0.25f) {
        // raised: solid underneath (lattice skirt), walkable on top, steps down at the entry
        x.m->box(vec3(c, (zb - 0.26f + zGround - 0.3f) * 0.5f) - x.org, X, Y, Z, vec3(hw - 0.05f, dep * 0.5f - 0.05f, (zb - 0.26f - zGround + 0.3f) * 0.5f),
                 packRGBA8(0.9f, 0.9f, 0.88f, 1), makeMat(MAT_WOOD_SIDING));
        addCollision(x, c, e.ax, hw, dep * 0.5f, zGround - 0.6f, zb + 0.02f);
        if (steps) deckSteps(x, e, um, 1.5f, hy + dep, zb + 0.02f, zGround, packRGBA8(0.72f, 0.7f, 0.66f, 1), makeMat(MAT_WOOD));
    }
    // posts
    int np = Max(2, (int)roundf((u1 - u0) / 2.6f) + 1);
    for (int k = 0; k < np; k++) {
        vec2 p = e.P(Lerp(u0 + 0.15f, u1 - 0.15f, (float)k / (np - 1)), hy + dep - 0.15f);
        x.m->box(vec3(p, (zb + zEave) * 0.5f) - x.org, X, Y, Z, vec3(0.09f, 0.09f, (zEave - zb) * 0.5f), trim, makeMat(MAT_WOOD));
        addCollision(x, p, e.ax, 0.1f, 0.1f, zb, zEave);
    }
    if (rail && x.detail) {
        x.m->box(vec3(e.P(um, hy + dep - 0.15f), zb + 0.9f) - x.org, X, Y, Z, vec3(hw, 0.03f, 0.03f), trim, makeMat(MAT_WOOD));
        for (float u = u0 + 0.3f; u < u1 - 0.2f; u += 0.14f)
            if (fabsf(u - um) > 0.6f) x.m->box(vec3(e.P(u, hy + dep - 0.15f), zb + 0.45f) - x.org, X, Y, Z, vec3(0.018f, 0.018f, 0.45f), trim, makeMat(MAT_WOOD));
    }
    // roof
    vec2 rc = e.P(um, hy + dep * 0.5f);
    if (roofKind == 2) return;
    if (roofKind == 0) {
        // front-facing gable over the porch
        pitchedRoof(x, rc, e.ax, hw + 0.2f, dep * 0.5f + 0.15f, zEave, 0.6f, false, 0.2f, col, mat, trim, makeMat(MAT_WOOD_SIDING), 1);
    } else {
        // shed roof sloping away from the wall
        vec3 a0 = vec3(e.P(u0 - 0.2f, hy), zEave + 0.7f) - x.org, a1 = vec3(e.P(u1 + 0.2f, hy), zEave + 0.7f) - x.org;
        vec3 b0 = vec3(e.P(u0 - 0.2f, hy + dep + 0.3f), zEave) - x.org, b1 = vec3(e.P(u1 + 0.2f, hy + dep + 0.3f), zEave) - x.org;
        x.m->quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(u1 - u0, 0), vec2(u1 - u0, dep), vec2(0, dep), col, mat, vec3(e.fr, 2.f));
        x.m->quadFacing(a0, b0, b1, a1, vec2(0, 0), vec2(dep, 0), vec2(dep, u1 - u0), vec2(0, u1 - u0), trim, makeMat(MAT_WOOD), vec3(-e.fr, -2.f));
    }
}

// The frame of a side wall (sgn = +1: the +u side): u along the wall (the front's v), v outward; for porches on a side
Env sideEnv(const Env& e, float sgn) {
    Env s;
    s.c = e.c;
    s.ax = e.fr;
    s.fr = e.ax * sgn;
    s.hx = e.hy;
    s.hy = e.hx;
    s.flip = dot(s.fr, perp(s.ax)) < 0.f;
    return s;
}

// Raised floor of a conch house on its piers, and its porch type (deterministic from the seed: the facade detail and
// the far LOD agree)
inline float conchRaise(const Building& b) { return 0.55f + 0.4f * hashToFloat(hash32(b.seed ^ 0xC0C4u)); }

// Two-storey conch house: a double gallery across the front, the upper deck on the lower posts, railed, under a shed roof
void galleryPorch(Ctx& x, const Env& e, float hy, float u0, float u1, float dep, float zb, float zUp, float zTop, u32 col, u32 mat, u32 trim, float zGround) {
    vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
    housePorch(x, e, hy, u0, u1, dep, zb, zUp - 0.2f, 2, col, mat, trim, true, zGround);
    float um = (u0 + u1) * 0.5f, hw = (u1 - u0) * 0.5f;
    // the upper deck (the lower porch's ceiling)
    x.m->box(vec3(e.P(um, hy + dep * 0.5f), zUp - 0.1f) - x.org, X, Y, Z, vec3(hw, dep * 0.5f, 0.1f), packRGBA8(0.92f, 0.91f, 0.88f, 1), makeMat(MAT_WOOD), true);
    // upper posts, rail and balusters
    float zE = zTop - 0.75f;
    int np = Max(2, (int)roundf((u1 - u0) / 2.6f) + 1);
    for (int k = 0; k < np; k++) {
        vec2 p = e.P(Lerp(u0 + 0.15f, u1 - 0.15f, (float)k / (np - 1)), hy + dep - 0.15f);
        x.m->box(vec3(p, (zUp + zE) * 0.5f) - x.org, X, Y, Z, vec3(0.08f, 0.08f, (zE - zUp) * 0.5f), trim, makeMat(MAT_WOOD));
    }
    x.m->box(vec3(e.P(um, hy + dep - 0.15f), zUp + 0.95f) - x.org, X, Y, Z, vec3(hw, 0.035f, 0.035f), trim, makeMat(MAT_WOOD));
    if (x.detail)
        for (float u = u0 + 0.25f; u < u1 - 0.2f; u += 0.14f)
            x.m->box(vec3(e.P(u, hy + dep - 0.15f), zUp + 0.47f) - x.org, X, Y, Z, vec3(0.018f, 0.018f, 0.47f), trim, makeMat(MAT_WOOD));
    // shed roof under the main eave
    vec3 a0 = vec3(e.P(u0 - 0.2f, hy), zE + 0.7f) - x.org, a1 = vec3(e.P(u1 + 0.2f, hy), zE + 0.7f) - x.org;
    vec3 b0 = vec3(e.P(u0 - 0.2f, hy + dep + 0.3f), zE) - x.org, b1 = vec3(e.P(u1 + 0.2f, hy + dep + 0.3f), zE) - x.org;
    x.m->quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(u1 - u0, 0), vec2(u1 - u0, dep), vec2(0, dep), col, mat, vec3(e.fr, 2.f));
    x.m->quadFacing(a0, b0, b1, a1, vec2(0, 0), vec2(dep, 0), vec2(dep, u1 - u0), vec2(0, u1 - u0), trim, makeMat(MAT_WOOD), vec3(-e.fr, -2.f));
}

// Colonial villa: a two-storey portico of columns across the middle of the front, entablature and pediment
void colonialPortico(Ctx& x, const Env& e, float hx, float hy, float zb, float zTop, u32 roofCol, u32 roofMat, Rng& mr) {
    vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
    float pw = Clamp(2.f * hx * mr.range(0.34f, 0.46f), 5.5f, 11.f) * 0.5f, pd = mr.range(2.6f, 3.2f);
    const u32 white = packRGBA8(0.97f, 0.97f, 0.95f, 1), stone = packRGBA8(0.86f, 0.84f, 0.8f, 1);
    // platform and steps
    vec2 pc = e.P(0.f, hy + pd * 0.5f);
    x.m->box(vec3(pc, zb - 0.1f) - x.org, X, Y, Z, vec3(pw + 0.3f, pd * 0.5f, 0.28f), stone, makeMat(MAT_CONCRETE), true);
    addCollision(x, pc, e.ax, pw + 0.3f, pd * 0.5f, zb - 1.f, zb + 0.18f);
    // columns
    float zE = zTop - 0.1f;
    int nc = pw > 3.6f ? 6 : 4;
    for (int k = 0; k < nc; k++) {
        vec2 cp = e.P(Lerp(-pw + 0.35f, pw - 0.35f, (float)k / (nc - 1)), hy + pd - 0.4f);
        x.m->cylinder(vec3(cp, zb + 0.18f) - x.org, 0.26f, 0.21f, zE - 0.75f - zb - 0.18f, 12, white, makeMat(MAT_PLASTER), false);
        x.m->box(vec3(cp, zb + 0.3f) - x.org, X, Y, Z, vec3(0.34f, 0.34f, 0.12f), white, makeMat(MAT_PLASTER));
        x.m->box(vec3(cp, zE - 0.82f) - x.org, X, Y, Z, vec3(0.32f, 0.32f, 0.07f), white, makeMat(MAT_PLASTER));
        addCollision(x, cp, e.ax, 0.27f, 0.27f, zb, zE);
    }
    // entablature
    vec2 ec = e.P(0.f, hy + pd * 0.5f - 0.05f);
    x.m->box(vec3(ec, zE - 0.4f) - x.org, X, Y, Z, vec3(pw + 0.15f, pd * 0.5f + 0.05f, 0.36f), white, makeMat(MAT_PLASTER), true);
    // pediment: a front gable over the portico, the house roof's finish
    pitchedRoof(x, ec, e.ax, pw + 0.15f, pd * 0.5f + 0.05f, zE - 0.04f, 0.42f, false, 0.25f, roofCol, roofMat, white, makeMat(MAT_PLASTER), 1);
}

// Stilt house of the fishing towns (AR_SHACK_STILT): the floor on posts over the flats, decks round the front, a side
// or the whole house with railings, a stair down to the yard, a porch roof, a metal roof (side or front gable, shed,
// hip) and a rain-water cistern. Height of the floor above the ground from the seed (buildmesh.cpp reads it too).
inline float shackStiltH(const Building& b) { return 1.4f + 1.2f * hashToFloat(hash32(b.seed ^ 0x5717u)); }

// Mono-pitch roof over a rectangle: high along v1 (the front), low along v0, side walls closed in the wall colour
void shedRoofPart(Ctx& x, const Env& e, vec4 r, float z, float rise, float over, u32 col, u32 mat, u32 wallCol, u32 wallMat) {
    float u0 = r.x - over, u1 = r.y + over, v0 = r.z - over, v1 = r.w + over;
    float k = rise / Max(0.1f, r.w - r.z);
    float zLo = z - over * k, zHi = z + rise + over * k;
    vec3 a0 = vec3(e.P(u0, v0), zLo) - x.org, a1 = vec3(e.P(u1, v0), zLo) - x.org, b0 = vec3(e.P(u0, v1), zHi) - x.org, b1 = vec3(e.P(u1, v1), zHi) - x.org;
    vec3 up = normalize(vec3(-e.fr * rise, r.w - r.z));
    float sl = length(b0 - a0);
    x.m->quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(u1 - u0, 0), vec2(u1 - u0, sl), vec2(0, sl), col, mat, up);
    x.m->quadFacing(a0, b0, b1, a1, vec2(0, 0), vec2(sl, 0), vec2(sl, u1 - u0), vec2(0, u1 - u0), packRGBA8(0.6f, 0.55f, 0.48f, 1), makeMat(MAT_WOOD), -up);
    // the front wall's top up to the high edge, the side triangles
    vec3 X(e.ax, 0.f);
    for (int s = 0; s < 2; s++) {
        float u = s ? r.y : r.x;
        vec3 p0 = vec3(e.P(u, r.z), z) - x.org, p1 = vec3(e.P(u, r.w), z) - x.org, p2 = vec3(e.P(u, r.w), z + rise) - x.org;
        vec3 nn = s ? X : -X;
        u32 i0 = x.m->addVertex(p0, nn, vec3(e.fr, 0.f), vec2(0, 0), wallCol, wallMat);
        u32 i1 = x.m->addVertex(p1, nn, vec3(e.fr, 0.f), vec2(r.w - r.z, 0), wallCol, wallMat);
        u32 i2 = x.m->addVertex(p2, nn, vec3(e.fr, 0.f), vec2(r.w - r.z, rise), wallCol, wallMat);
        if (dot(cross(p1 - p0, p2 - p0), nn) >= 0.f) x.m->tri(i0, i1, i2);
        else x.m->tri(i0, i2, i1);
    }
    vec3 f0 = vec3(e.P(r.x, r.w), z) - x.org, f1 = vec3(e.P(r.y, r.w), z) - x.org;
    x.m->quadFacing(f0, f1, f1 + vec3(0, 0, rise), f0 + vec3(0, 0, rise), vec2(0, 0), vec2(r.y - r.x, 0), vec2(r.y - r.x, rise), vec2(0, rise), wallCol, wallMat,
                    vec3(e.fr, 0.f));
}

void buildStiltShack(Ctx& x, const Building& b, const FacadeGPU& fac, u32& roofCol, u32& roofMat) {
    Rng mr(b.seed ^ 0x5AC4u);
    Env e = envOf(b);
    const float hx = b.hx, hy = b.hy, z0 = b.baseZ;
    const float zf = z0 + shackStiltH(b), zTop = zf + b.height;
    houseRoofFinish(b, mr, roofCol, roofMat);
    vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
    const u32 post = packRGBA8(0.6f, 0.54f, 0.46f, 1), deckCol = packRGBA8(0.7f, 0.64f, 0.55f, 1);
    const u32 woodMat = makeMat(MAT_WOOD);
    vec4 wc = unpackRGBA8(fac.frameColor);
    const u32 railCol = packRGBA8(wc.x, wc.y, wc.z, 1);
    vec4 wl = unpackRGBA8(fac.wallColor);
    const u32 wallCol = packRGBA8(Saturate(wl.x * 1.45f), Saturate(wl.y * 1.45f), Saturate(wl.z * 1.45f), 1);
    const u32 wallMat = makeMat((u32)fac.wallLayer == MAT_CORRUGATED ? MAT_CORRUGATED : MAT_WOOD_SIDING);
    // everything that shapes the outline first (one stream for both LODs)
    const bool L = b.massing == MK_L;
    const float ls = mr.chance(0.5f) ? 1.f : -1.f;
    const float nw = 2.f * hx * mr.range(0.38f, 0.5f), nd = 2.f * hy * mr.range(0.35f, 0.5f);
    const float dkw[3] = {0.35f, 0.4f, 0.25f};
    const int deckKind = mr.weighted(dkw, 3);   // 0 all round, 1 the front, 2 the front and a side (drawn: dk below)
    const float dw = mr.range(1.0f, 1.4f), df = mr.range(1.2f, 1.6f);
    const float side = mr.chance(0.5f) ? 1.f : -1.f;
    const float rkw[4] = {0.35f, 0.25f, 0.25f, 0.15f};
    const int roofKind = mr.weighted(rkw, 4);   // side gable, front gable, shed, hip
    const float pitch = mr.range(0.36f, 0.55f);
    const bool porchRoof = deckKind != 0 ? mr.chance(0.6f) : mr.chance(0.25f);
    const bool brace = mr.chance(0.45f);
    const bool cistern = mr.chance(0.3f);
    const float stairSide = mr.chance(0.5f) ? 1.f : -1.f;
    // room to the lot's side lines (the neighbours' decks): decks round the sides only where they fit
    const float lateral = dot(b.c - b.lotC, b.ax);
    const float roomPos = b.lotHx - lateral - hx, roomNeg = b.lotHx + lateral - hx;
    int dk = deckKind;
    if (dk == 0 && Min(roomPos, roomNeg) < dw + 0.4f) dk = 1;
    if (dk == 2 && (side > 0.f ? roomPos : roomNeg) < dw + 0.4f) dk = 1;
    // body
    std::vector<vec4> parts;
    std::vector<vec2> uvPoly;
    if (L) {
        float vc = hy - nd;
        if (ls > 0.f) {
            float ua = hx - nw;
            uvPoly = {vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, vc), vec2(ua, vc), vec2(ua, hy), vec2(-hx, hy)};
            parts = {vec4(-hx, hx, -hy, vc), vec4(-hx, ua, vc, hy)};
        } else {
            float ub = -hx + nw;
            uvPoly = {vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(ub, hy), vec2(ub, vc), vec2(-hx, vc)};
            parts = {vec4(-hx, hx, -hy, vc), vec4(ub, hx, vc, hy)};
        }
    } else {
        uvPoly = rectUV(-hx, hx, -hy, hy);
        parts = {vec4(-hx, hx, -hy, hy)};
    }
    std::vector<vec2> fp = toWorld(e, uvPoly);
    recordMass(x, fp, zf, zTop, zf, FM_HOUSE, false);
    facadeWalls(x, fp, zf - 0.3f, zTop, zf, b.facade, fac.bayW);
    {
        std::vector<vec3> under;
        for (int i = (int)fp.size() - 1; i >= 0; i--) under.push_back(vec3(fp[i], zf - 0.3f) - x.org);
        x.m->polygon(under, vec3(0, 0, -1), post, woodMat, 1.f);
    }
    for (const vec4& p : parts) addCollision(x, e.P((p.x + p.y) * 0.5f, (p.z + p.w) * 0.5f), e.ax, (p.y - p.x) * 0.5f, (p.w - p.z) * 0.5f, z0 - 2.f, zTop + 1.5f);
    // roofs
    for (size_t k = 0; k < parts.size(); k++) {
        const vec4& p = parts[k];
        if (roofKind == 2 && k == 0) shedRoofPart(x, e, p, zTop, Clamp((p.w - p.z) * 0.18f, 0.6f, 1.4f), 0.45f, roofCol, roofMat, wallCol, wallMat);
        else {
            int ridge = (roofKind == 1 && k == 0) ? 1 : (roofKind == 0 && k == 0 ? 0 : -1);
            pitchedPart(x, e, p, zTop, pitch, roofKind == 3, 0.45f, roofCol, roofMat, wallCol, wallMat, ridge);
        }
    }
    // decks: rectangles (u0, u1, v0, v1) at the floor; the front deck reaches round the corner to a side deck
    std::vector<vec4> decks;
    const float dep = dk == 0 ? dw : df;   // the deck's depth in front of the house
    if (dk == 0) decks.push_back(vec4(-hx - dw, hx + dw, -hy - dw, hy + dw));
    else {
        float fu0 = -hx - 0.2f, fu1 = hx + 0.2f;
        if (dk == 2) (side > 0.f ? fu1 : fu0) = side * (hx + dw);
        decks.push_back(vec4(fu0, fu1, hy - (L ? nd : 0.f), hy + df));
        if (dk == 2) {
            if (side > 0.f) decks.push_back(vec4(hx, hx + dw, -hy + 0.6f, hy - (L ? nd : 0.f)));
            else decks.push_back(vec4(-hx - dw, -hx, -hy + 0.6f, hy - (L ? nd : 0.f)));
        }
    }
    const vec4 front = decks[0];
    const float zd = zf - 0.03f;
    for (const vec4& d : decks) {
        vec2 dc = e.P((d.x + d.y) * 0.5f, (d.z + d.w) * 0.5f);
        float dhx = (d.y - d.x) * 0.5f, dhy = (d.w - d.z) * 0.5f;
        x.m->box(vec3(dc, zd - 0.12f) - x.org, X, Y, Z, vec3(dhx, dhy, 0.12f), deckCol, woodMat, true);
        addCollision(x, dc, e.ax, dhx, dhy, zd - 0.3f, zd);
        // posts under the outer edges, every 2.4 m or so
        int nu = Max(1, (int)ceilf((d.y - d.x) / 2.4f)), nv = Max(1, (int)ceilf((d.w - d.z) / 2.4f));
        for (int i = 0; i <= nu; i++)
            for (int j = 0; j <= nv; j++) {
                if (i != 0 && i != nu && j != 0 && j != nv) continue;
                float u = Lerp(d.x + 0.12f, d.y - 0.12f, (float)i / nu), v = Lerp(d.z + 0.12f, d.w - 0.12f, (float)j / nv);
                if (u > -hx && u < hx && v > -hy && v < hy) continue;   // under the house: inside its collision
                vec2 pp = e.P(u, v);
                x.m->box(vec3(pp, (z0 - 0.4f + zd - 0.24f) * 0.5f) - x.org, X, Y, Z, vec3(0.11f, 0.11f, (zd - 0.24f - z0 + 0.4f) * 0.5f), post, woodMat);
                addCollision(x, pp, e.ax, 0.12f, 0.12f, z0 - 0.5f, zd - 0.24f);
            }
    }
    // the posts under the house corners
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2) {
            vec2 pp = e.P(i * (hx - 0.25f), j * (hy - 0.25f));
            x.m->box(vec3(pp, (z0 - 0.4f + zf - 0.3f) * 0.5f) - x.org, X, Y, Z, vec3(0.13f, 0.13f, (zf - 0.3f - z0 + 0.4f) * 0.5f), post, woodMat);
        }
    // cross bracing on the front and the sides between the corner posts
    if (brace) {
        float hb = zf - 0.4f - (z0 + 0.2f);
        for (int s = 0; s < 3; s++) {
            vec2 a, c;
            if (s == 0) a = e.P(-hx + 0.25f, hy - 0.25f), c = e.P(hx - 0.25f, hy - 0.25f);
            else {
                float u = s == 1 ? -hx + 0.25f : hx - 0.25f;
                a = e.P(u, -hy + 0.25f), c = e.P(u, hy - 0.25f);
            }
            vec3 hn = normalize(vec3(perp(normalize(c - a)), 0.f));
            for (int dgl = 0; dgl < 2; dgl++) {
                vec3 p0 = vec3(dgl ? c : a, z0 + 0.2f), p1 = vec3(dgl ? a : c, z0 + 0.2f + hb);
                vec3 dir = p1 - p0;
                float len = length(dir);
                if (len < 0.5f) continue;
                dir = dir / len;
                vec3 up = normalize(cross(dir, hn));
                x.m->box((p0 + p1) * 0.5f - x.org, dir, hn, up, vec3(len * 0.5f, 0.03f, 0.07f), post, woodMat);
            }
        }
    }
    // the stair down to the yard from the front deck's corner: straight toward the street when the front yard has the
    // room, else along the deck's front edge toward the middle; a landing at the head
    const float rise = zd - z0;
    const int nSteps = Max(2, (int)ceilf(rise / 0.19f));
    const float stepH = rise / nSteps, run = 0.27f, flight = (nSteps - 1) * run;
    const float yard = dot(b.lotC - b.c, b.front) + b.lotHy - b.hy - dep;
    const float ue = stairSide * ((stairSide > 0.f ? front.y : -front.x) - 0.6f);   // the head's centre along the front
    const bool toStreet = yard > flight + 1.4f, alongFront = !toStreet && yard > 1.6f;
    if (toStreet || alongFront) {
        // the landing in front of the deck edge
        vec2 lc = e.P(ue, front.w + 0.5f);
        x.m->box(vec3(lc, zd - 0.06f) - x.org, X, Y, Z, vec3(0.5f, 0.5f, 0.06f), deckCol, woodMat, true);
        addCollision(x, lc, e.ax, 0.5f, 0.5f, zd - 0.3f, zd);
        for (int i = -1; i <= 1; i += 2)
            for (int j = 0; j < 2; j++) {
                vec2 pp = e.P(ue + i * 0.42f, front.w + 0.08f + j * 0.84f);
                x.m->box(vec3(pp, (z0 - 0.3f + zd - 0.12f) * 0.5f) - x.org, X, Y, Z, vec3(0.07f, 0.07f, (zd - 0.12f - z0 + 0.3f) * 0.5f), post, woodMat);
            }
        // treads (u along the flight, w across it) and the two stringers
        vec2 fdir = toStreet ? e.fr : e.ax * -stairSide, fside = toStreet ? e.ax : e.fr;
        vec2 start = toStreet ? e.P(ue, front.w + 1.f) : e.P(ue - stairSide * 0.5f, front.w + 0.5f);
        for (int k = 1; k < nSteps; k++) {
            float zt = zd - k * stepH;
            vec2 c = start + fdir * ((k - 0.5f) * run);
            x.m->box(vec3(c, zt - 0.03f) - x.org, vec3(fdir, 0.f), vec3(fside, 0.f), Z, vec3(run * 0.5f + 0.02f, 0.46f, 0.03f), deckCol, woodMat);
            addCollision(x, c, fdir, run * 0.5f, 0.46f, zt - 0.4f, zt);
        }
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 p0 = vec3(start + fside * (sd * 0.48f), zd - 0.12f), p1 = vec3(start + fdir * flight + fside * (sd * 0.48f), z0 - 0.05f);
            vec3 dir = p1 - p0;
            float l = length(dir);
            dir = dir / l;
            vec3 up = normalize(cross(dir, vec3(fside, 0.f)));
            if (up.z < 0.f) up = -up;
            x.m->box((p0 + p1) * 0.5f - x.org, dir, vec3(fside, 0.f), up, vec3(l * 0.5f, 0.03f, 0.13f), post, woodMat);
            if (x.detail) {
                // handrail
                x.m->box((p0 + p1) * 0.5f + vec3(0, 0, 0.95f) - x.org, dir, vec3(fside, 0.f), up, vec3(l * 0.5f, 0.025f, 0.03f), railCol, woodMat);
            }
        }
    }
    // railings round the deck edges: not along the house walls, open where the decks meet and at the stair head
    const float zr = zd + 0.95f;
    auto railRun = [&](vec2 a, vec2 c) {   // (u, v) ends
        vec2 wa = e.P(a.x, a.y), wb = e.P(c.x, c.y);
        vec2 t = wb - wa;
        float len = length(t);
        if (len < 0.3f) return;
        t = t / len;
        vec3 T(t, 0.f), N(perp(t), 0.f);
        x.m->box(vec3((wa + wb) * 0.5f, zr) - x.org, T, N, Z, vec3(len * 0.5f, 0.04f, 0.04f), railCol, woodMat);
        if (x.detail) {
            int nb = Max(1, (int)(len / 1.2f));
            for (int i = 0; i <= nb; i++) {
                vec2 pp = wa + t * (len * i / nb);
                x.m->box(vec3(pp, (zd + zr) * 0.5f) - x.org, T, N, Z, vec3(0.04f, 0.04f, (zr - zd) * 0.5f), railCol, woodMat);
            }
            x.m->box(vec3((wa + wb) * 0.5f, zd + 0.45f) - x.org, T, N, Z, vec3(len * 0.5f, 0.025f, 0.025f), railCol, woodMat);
        }
    };
    for (size_t di = 0; di < decks.size(); di++) {
        const vec4& d = decks[di];
        vec2 corners[4] = {vec2(d.x, d.z), vec2(d.y, d.z), vec2(d.y, d.w), vec2(d.x, d.w)};
        for (int k = 0; k < 4; k++) {
            vec2 a = corners[k], c = corners[(k + 1) % 4];
            vec2 mid = (a + c) * 0.5f;
            if (fabsf(mid.x) <= hx + 0.01f && fabsf(mid.y) <= hy + 0.01f) continue;   // along a house wall
            if (di == 1 && k == 2) continue;                                           // the side deck's end meets the front deck
            bool frontEdge = di == 0 && k == 2;
            if (frontEdge && (toStreet || alongFront)) {
                // (c -> a runs along the front edge from u = d.y to u = d.x)
                float g0 = ue - 0.5f, g1 = ue + 0.5f;
                if (g1 < d.y) railRun(vec2(d.y, d.w), vec2(g1, d.w));
                if (g0 > d.x) railRun(vec2(g0, d.w), vec2(d.x, d.w));
                continue;
            }
            railRun(a, c);
        }
    }
    // porch roof over the front deck (or the front strip of an all-round deck), its posts clear of the stair head
    if (porchRoof) {
        float u0 = front.x, u1 = front.y;
        float zE = zTop - 0.75f;
        int np = Max(2, (int)roundf((u1 - u0) / 2.6f) + 1);
        for (int k = 0; k < np; k++) {
            float u = Lerp(u0 + 0.12f, u1 - 0.12f, (float)k / (np - 1));
            if ((toStreet || alongFront) && fabsf(u - ue) < 0.6f) continue;
            vec2 pp = e.P(u, hy + dep - 0.12f);
            x.m->box(vec3(pp, (zd + zE) * 0.5f) - x.org, X, Y, Z, vec3(0.07f, 0.07f, (zE - zd) * 0.5f), railCol, woodMat);
            addCollision(x, pp, e.ax, 0.08f, 0.08f, zd, zE);
        }
        vec3 a0 = vec3(e.P(u0 - 0.15f, hy), zE + 0.6f) - x.org, a1 = vec3(e.P(u1 + 0.15f, hy), zE + 0.6f) - x.org;
        vec3 b0 = vec3(e.P(u0 - 0.15f, hy + dep + 0.25f), zE) - x.org, b1 = vec3(e.P(u1 + 0.15f, hy + dep + 0.25f), zE) - x.org;
        x.m->quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(u1 - u0, 0), vec2(u1 - u0, dep), vec2(0, dep), roofCol, roofMat, vec3(e.fr, 2.f));
        x.m->quadFacing(a0, b0, b1, a1, vec2(0, 0), vec2(dep, 0), vec2(dep, u1 - u0), vec2(0, u1 - u0), post, woodMat, vec3(-e.fr, -2.f));
    }
    // rain-water cistern on a stand at a back corner
    if (cistern) {
        vec2 cc = e.P(side * (hx - 1.f), -hy - (dk == 0 ? dw : 0.f) - 1.1f);
        float r = 0.75f, hh = 1.6f;
        for (int i = -1; i <= 1; i += 2)
            for (int j = -1; j <= 1; j += 2)
                x.m->box(vec3(cc + e.ax * (i * 0.55f) + e.fr * (j * 0.55f), (z0 + zf - 0.9f) * 0.5f) - x.org, X, Y, Z, vec3(0.07f, 0.07f, (zf - 0.9f - z0) * 0.5f), post, woodMat);
        x.m->box(vec3(cc, zf - 0.95f) - x.org, X, Y, Z, vec3(0.75f, 0.75f, 0.06f), post, woodMat, true);
        x.m->cylinder(vec3(cc, zf - 0.9f) - x.org, r, r, hh, 12, packRGBA8(0.88f, 0.88f, 0.86f, 1), makeMat(MAT_METAL_PAINTED), true);
        addCollision(x, cc, e.ax, 0.8f, 0.8f, z0 - 0.5f, zf - 0.9f + hh);
    }
}

}  // namespace massing

// Archetype house body: walls, roofs, porch and small tower; the garage wing, garden, pool and trees stay with the
// generic house code (buildmesh.cpp), which gets the roof finish for the garage. Returns false for plain houses.
bool buildArchHouseBody(Ctx& x, const Building& b, const FacadeGPU& fac, u32& roofCol, u32& roofMat) {
    using namespace massing;
    if (b.arch == AR_NONE || b.interior >= 0) return false;
    if (b.style == BS_SHACK && b.arch == AR_SHACK_STILT) {
        buildStiltShack(x, b, fac, roofCol, roofMat);
        return true;
    }
    if (b.style != BS_HOUSE && b.style != BS_VILLA) return false;
    Rng mr(b.seed ^ 0x4D455353u), hr(b.seed ^ 0x40053u);
    Env e = envOf(b);
    const float hx = b.hx, hy = b.hy;
    const bool conch = b.arch == AR_HOUSE_CONCH;
    const float zb = b.baseZ + (conch ? conchRaise(b) : 0.f);
    const float gs = (b.seed & 64u) ? 1.f : -1.f;   // the garage wing's side (buildmesh.cpp / population.cpp)
    // conch porch: 0 full-width shed porch, 1 a front-gabled porch (the house gable to the street), 2 round the front
    // and a side, 3 a small entry porch; two-storey houses mostly a double gallery (4)
    int conchPorch = 0;
    if (conch) {
        u32 cv = hash32(b.seed ^ 0xC0C5u) % 100u;
        conchPorch = (b.floors >= 2 && cv < 55u) ? 4 : (cv < 35u ? 0 : (cv < 60u ? 1 : (cv < 82u ? 2 : 3)));
        // (round the side only where the side yard has room for it beside the hedge: the side away from the garage)
        float sideRoom = b.lotHx - dot(b.c - b.lotC, b.ax) * -gs - hx;
        if (conchPorch == 2 && sideRoom < 4.3f) conchPorch = 0;
    }
    const float H = b.height, zTop = zb + H;
    houseRoofFinish(b, hr, roofCol, roofMat);
    const u32 wallTrim = packRGBA8(0.95f, 0.95f, 0.93f, 1);
    vec4 wcol = unpackRGBA8(fac.wallColor);
    const u32 gableCol = packRGBA8(Saturate(wcol.x * 1.45f), Saturate(wcol.y * 1.45f), Saturate(wcol.z * 1.45f), 1);
    const u32 gableMat = makeMat((u32)fac.wallLayer == MAT_WOOD_SIDING ? MAT_WOOD_SIDING : MAT_PLASTER);
    const float skirt = conch ? 0.75f : 2.f;
    struct Part {
        vec4 r;
        float z1;
        u8 kind;
    };
    std::vector<Part> parts;
    std::vector<vec2> mainUV = rectUV(-hx, hx, -hy, hy);
    bool flat = b.arch == AR_HOUSE_MIMO || b.arch == AR_VILLA_MODERN;
    float pitch = 0.4f;
    bool hip = b.roof == ROOF_HIP;
    int ridge = -1;
    switch (b.arch) {
        case AR_HOUSE_RANCH: pitch = mr.range(0.26f, 0.34f); break;
        case AR_HOUSE_BUNGALOW: pitch = mr.range(0.45f, 0.6f); ridge = hx <= hy * 1.25f ? 1 : -1; break;   // gable to the street
        case AR_HOUSE_MED: pitch = mr.range(0.3f, 0.36f); hip = true; break;
        case AR_HOUSE_TWO: pitch = mr.range(0.4f, 0.55f); break;
        case AR_HOUSE_SPLIT: pitch = mr.range(0.35f, 0.45f); break;
        case AR_HOUSE_CONCH: pitch = mr.range(0.5f, 0.68f); if (conchPorch == 1 && hx <= hy * 1.4f) ridge = 1; break;
        case AR_VILLA_MED: pitch = mr.range(0.3f, 0.36f); hip = true; break;
        case AR_VILLA_COLONIAL: pitch = mr.range(0.36f, 0.46f); hip = true; break;
        default: break;
    }
    // massing: rectangles of the body (u0, u1, v0, v1) and their eave heights
    std::vector<vec2> uvPoly;
    switch (b.massing) {
        case MK_L: {
            // full width at the back, a front leg on the garage side (the entry sits in the inner corner)
            float lw = Clamp(2.f * hx * mr.range(0.4f, 0.55f), 3.5f, 2.f * hx - 3.5f), ld = Clamp(2.f * hy * mr.range(0.32f, 0.48f), 2.5f, 2.f * hy - 4.f);
            float vc = hy - ld;
            if (gs > 0.f) {
                float ua = hx - lw;
                uvPoly = {vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, hy), vec2(ua, hy), vec2(ua, vc), vec2(-hx, vc)};
                parts.push_back({vec4(-hx, hx, -hy, vc), zTop, FM_HOUSE});
                parts.push_back({vec4(ua, hx, vc, hy), zTop, FM_HOUSE});
            } else {
                float ub = -hx + lw;
                uvPoly = {vec2(-hx, -hy), vec2(hx, -hy), vec2(hx, vc), vec2(ub, vc), vec2(ub, hy), vec2(-hx, hy)};
                parts.push_back({vec4(-hx, hx, -hy, vc), zTop, FM_HOUSE});
                parts.push_back({vec4(-hx, ub, vc, hy), zTop, FM_HOUSE});
            }
            break;
        }
        case MK_SPLIT: {
            // split level: a two-storey wing beside a one-storey wing (the garage side)
            float us = -hx + 2.f * hx * mr.range(0.42f, 0.56f);
            float zLow = zb + fac.groundH;
            if (gs > 0.f) {
                parts.push_back({vec4(-hx, us, -hy, hy), zTop, FM_HOUSE});
                parts.push_back({vec4(us, hx, -hy + 1.f, hy - 0.6f), zLow, FM_WING});
            } else {
                parts.push_back({vec4(us, hx, -hy, hy), zTop, FM_HOUSE});
                parts.push_back({vec4(-hx, us, -hy + 1.f, hy - 0.6f), zLow, FM_WING});
            }
            break;
        }
        case MK_WINGS: {
            // two-storey block with a one-storey wing on the side away from the garage
            float mw = 2.f * hx * mr.range(0.58f, 0.7f);
            float zLow = zb + fac.groundH;
            if (gs > 0.f) {
                parts.push_back({vec4(hx - mw, hx, -hy, hy), zTop, FM_HOUSE});
                parts.push_back({vec4(-hx, hx - mw, -hy + 0.8f, hy - 1.2f), zLow, FM_WING});
            } else {
                parts.push_back({vec4(-hx, -hx + mw, -hy, hy), zTop, FM_HOUSE});
                parts.push_back({vec4(-hx + mw, hx, -hy + 0.8f, hy - 1.2f), zLow, FM_WING});
            }
            break;
        }
        case MK_STEP_BACK: {
            // modern villa: the ground floor recessed under a cantilevered upper floor
            float rec = Min(2.6f, hy * 0.3f);
            float zl = zb + fac.groundH;
            parts.push_back({vec4(-hx, hx, -hy, hy - rec), zl, FM_HOUSE});
            parts.push_back({vec4(-hx + Min(3.f, hx * 0.3f), hx, -hy + 1.5f, hy), zTop, FM_WING});
            break;
        }
        default: parts.push_back({vec4(-hx, hx, -hy, hy), zTop, FM_HOUSE}); break;
    }
    // walls and masses: the main body (one FM_HOUSE mass: garden, door) and the other parts as wings
    bool mainDone = false;
    if (!uvPoly.empty()) {
        std::vector<vec2> fp = toWorld(e, uvPoly);
        recordMass(x, fp, zb, zTop, zb, FM_HOUSE, false);
        facadeWalls(x, fp, zb - skirt, zTop, zb, b.facade, fac.bayW);
        mainDone = true;
    }
    for (const Part& p : parts) {
        bool isMain = p.kind == FM_HOUSE;
        if (!(isMain && mainDone)) {
            // a wing standing on the upper floor of a stepped villa starts at the floor line
            float zs = (b.massing == MK_STEP_BACK && !isMain) ? zb + fac.groundH : zb;
            std::vector<vec2> fp = toWorld(e, rectUV(p.r.x, p.r.y, p.r.z, p.r.w));
            recordMass(x, fp, zs, p.z1, zb, isMain ? FM_HOUSE : FM_WING, false);
            facadeWalls(x, fp, zs - (zs > zb + 0.1f ? 0.f : skirt), p.z1, zb, b.facade, fac.bayW);
            if (isMain) mainDone = true;
        }
        vec2 cc = e.P((p.r.x + p.r.y) * 0.5f, (p.r.z + p.r.w) * 0.5f);
        addCollision(x, cc, e.ax, (p.r.y - p.r.x) * 0.5f, (p.r.w - p.r.z) * 0.5f, b.baseZ - 2.f, p.z1 + (flat ? 0.3f : 2.f));
    }
    // roofs
    for (const Part& p : parts) {
        vec4 r = p.r;
        if (flat) {
            if (b.roofForm == RFM_BUTTERFLY && p.kind == FM_HOUSE) butterflyRoof(x, e, r.x, r.y, r.z, r.w, p.z1, 0.9f, roofCol);
            else eaveSlab(x, toWorld(e, rectUV(r.x, r.y, r.z, r.w)), p.z1, b.arch == AR_VILLA_MODERN ? 0.7f : 1.1f, 0.25f, packRGBA8(0.96f, 0.96f, 0.95f, 1));
        } else {
            bool partHip = hip || p.kind == FM_WING;
            // the archetype's ridge (bungalow: gable to the street); the leg of an L and the wings along their longer side
            int rd = (b.massing == MK_L || p.kind == FM_WING) ? -1 : ridge;
            pitchedPart(x, e, r, p.z1, pitch, partHip, b.arch == AR_HOUSE_RANCH ? 0.85f : 0.6f, roofCol, roofMat, gableCol, gableMat, rd);
        }
    }
    // the cantilevered upper floor of a stepped villa: its underside over the recessed entrance
    if (b.massing == MK_STEP_BACK && parts.size() > 1) {
        const vec4& r = parts[1].r;
        float vr = parts[0].r.w;
        std::vector<vec2> under = toWorld(e, rectUV(r.x, r.y, vr, r.w));
        std::vector<vec3> poly;
        for (int i = (int)under.size() - 1; i >= 0; i--) poly.push_back(vec3(under[i], zb + fac.groundH) - x.org);
        x.m->polygon(poly, vec3(0, 0, -1), packRGBA8(0.92f, 0.92f, 0.9f, 1), makeMat(MAT_PLASTER), 1.f);
    }
    // Mediterranean house: a small tower with a tile hip roof at the front corner away from the garage
    if ((b.arch == AR_HOUSE_MED || b.arch == AR_VILLA_MED) && b.massing == MK_CORNER_TOWER) {
        float ts = b.arch == AR_VILLA_MED ? Clamp(Min(hx, hy) * 0.5f, 3.6f, 5.2f) : Clamp(Min(hx, hy) * 0.55f, 2.8f, 4.2f);
        float u0 = gs > 0.f ? -hx : hx - ts, u1 = u0 + ts;
        std::vector<vec2> fp = toWorld(e, rectUV(u0, u1, hy - ts, hy));
        float zt = zTop + 2.6f;
        recordMass(x, fp, zTop - 0.3f, zt, zb, FM_WING, false);
        facadeWalls(x, fp, zTop - 0.3f, zt, zb, b.facade, fac.bayW);
        pitchedPart(x, e, vec4(u0, u1, hy - ts, hy), zt, 0.55f, true, 0.35f, roofCol, roofMat, gableCol, gableMat);
        addCollision(x, e.P((u0 + u1) * 0.5f, hy - ts * 0.5f), e.ax, ts * 0.5f, ts * 0.5f, zTop - 0.3f, zt + 1.5f);
    }
    // porches: bungalow front gable, conch house full-width shed porch (two-level gallery on two-storey conch houses)
    if (b.arch == AR_HOUSE_BUNGALOW) {
        float pw = 2.f * hx * mr.range(0.4f, 0.6f);
        float off = -gs * (hx - pw * 0.5f - 0.3f) * mr.range(0.f, 1.f);
        housePorch(x, e, hy, off - pw * 0.5f, off + pw * 0.5f, mr.range(1.9f, 2.4f), zb, zb + 2.6f, 0, roofCol, roofMat, wallTrim, mr.chance(0.5f));
    } else if (conch) {
        float dep = mr.range(2.1f, 2.6f);
        // (the porch and its steps stay inside the front yard)
        float yardC = dot(b.lotC - b.c, b.front) + b.lotHy - hy;
        float stepsRun = Max(1.f, ceilf((zb + 0.02f - b.baseZ) / 0.2f) - 1.f) * 0.28f;
        dep = Clamp(dep, 1.5f, Max(1.5f, yardC - stepsRun - 0.4f));
        float u0 = -hx + 0.2f, u1 = hx - 0.2f;
        float zE = zb + Min(2.8f, fac.groundH - 0.2f);
        if (conchPorch == 4) galleryPorch(x, e, hy, u0, u1, dep, zb, zb + fac.groundH, zTop, roofCol, roofMat, wallTrim, b.baseZ);
        else if (conchPorch == 1) {
            float pw = 2.f * hx * mr.range(0.45f, 0.6f);
            float off = -gs * (hx - pw * 0.5f - 0.3f) * mr.range(0.f, 1.f);
            housePorch(x, e, hy, off - pw * 0.5f, off + pw * 0.5f, dep, zb, zE, 0, roofCol, roofMat, wallTrim, true, b.baseZ);
        } else if (conchPorch == 3) {
            float pw = mr.range(1.4f, 1.8f);
            housePorch(x, e, hy, -pw, pw, mr.range(1.5f, 1.9f), zb, zE, 0, roofCol, roofMat, wallTrim, false, b.baseZ);
        } else {
            housePorch(x, e, hy, u0, u1, dep, zb, zE, 1, roofCol, roofMat, wallTrim, true, b.baseZ);
            if (conchPorch == 2) {
                // round the corner along the side away from the garage
                Env se = sideEnv(e, -gs);
                housePorch(x, se, hx - 0.2f, -hy + 1.f, hy + dep, dep, zb, zE, 1, roofCol, roofMat, wallTrim, true, b.baseZ, false);
            }
        }
        // piers under the raised floor
        vec3 X(e.ax, 0.f), Y(e.fr, 0.f), Z(0, 0, 1);
        if (x.detail)
            for (float u = -hx + 0.4f; u <= hx - 0.3f; u += 2.4f)
                for (int k = 0; k < 2; k++) {
                    vec2 p = e.P(u, k ? hy - 0.3f : -hy + 0.3f);
                    x.m->box(vec3(p, b.baseZ - 0.2f + 0.45f) - x.org, X, Y, Z, vec3(0.2f, 0.2f, 0.45f), packRGBA8(0.7f, 0.68f, 0.64f, 1), makeMat(MAT_CONCRETE));
                }
    } else if (b.arch == AR_VILLA_COLONIAL) {
        colonialPortico(x, e, hx, hy, zb, zTop, roofCol, roofMat, mr);
    }
    return true;
}

}  // namespace World
