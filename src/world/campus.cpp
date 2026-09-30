// Porto Sol University, North Porto Sol: a campus of Mediterranean revival halls in coral stone under red tile roofs on
// the south bank of the river. The main quad (lawns cut by diagonal walks, a fountain at the crossing, live oaks) runs
// from the gate plaza on N 34th Street to the domed library and its colonnade; lecture halls with arcaded loggias line
// both sides, the student union and the admin hall flank the gate, the bell tower stands beside the library, three
// residence halls face the river on Bayshore Boulevard and Tarpon Field (track, bleachers, press box, scoreboard,
// floodlights) fills the east side. Bike racks, benches, lamps and wayfinding signs throughout.
#include "places.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace campus {

using namespace sitegeo;
using namespace place_kit;

// Campus extent between the street centrelines that stay: Sunrise Blvd (x 1800), 27th Ave (x 2100), N 34th St (y 3450),
// Bayshore Blvd on the river (y 3950). The grid lines inside are left out (roads.cpp checks the road blocks).
constexpr float kX0 = 1800.f, kX1 = 2100.f, kY0 = 3450.f, kY1 = 3950.f;

enum HallKind { HK_LIBRARY = 0, HK_LECTURE, HK_UNION, HK_ADMIN, HK_DORM, HK_FIELDHOUSE, HK_COUNT };

const vec3 kStone(1.f, 0.9f, 0.77f), kStoneLight(1.f, 0.95f, 0.86f), kTile(0.78f, 0.36f, 0.22f), kTrim(0.98f, 0.96f, 0.9f), kBronze(0.28f, 0.22f, 0.16f);

// Hall element: c = centre, ax = front (toward the quad / street), hx = half width along perp(front), hy = half depth,
// z = ground floor level, p[0] floors, p[1] ground floor height, p[2] floor height, p[3] loggia depth (0 = none),
// p[4] name index, p[6] building index, p[7] facade (set by facades())
const char* kHallNames[] = {"MERIDIAN LIBRARY", "CASTELL HALL", "OKAHATCHEE HALL", "PALMERA HALL", "SAWGRASS HALL", "STUDENT UNION", "ADMINISTRATION",
                            "EGRET HALL", "MANGROVE HALL", "BRISA HALL", "TARPON FIELD HOUSE"};

struct Hall {
    vec2 c, f, a;
    float hw, hd;
    float z, gh, fh, H;
    int floors, kind, name;
    float loggia;
    u32 seed;
    vec2 P(float u, float v) const { return c + a * u + f * v; }
};
Hall hallOf(const SiteElem& e) {
    Hall h;
    h.c = e.c;
    h.f = e.ax;
    h.a = perp(e.ax);
    h.hw = e.hx;
    h.hd = e.hy;
    h.z = e.z;
    h.floors = (int)e.p[0];
    h.gh = e.p[1];
    h.fh = e.p[2];
    h.H = h.gh + (h.floors - 1) * h.fh;
    h.loggia = e.p[3];
    h.name = (int)e.p[4];
    h.kind = e.variant;
    h.seed = e.seed;
    return h;
}

// Classical column: base, fluted-looking shaft (12 sides), capital; standing on z, height hgt
void column(G& g, vec2 p, float z, float hgt, float r, u32 col) {
    int seg = g.detail ? 12 : 6;
    cyl(g, vec3(p, z), r * 1.35f, r * 1.3f, 0.25f, seg, col, M(MAT_MARBLE), true);
    cyl(g, vec3(p, z + 0.25f), r * 1.05f, r * 0.9f, hgt - 0.7f, seg, col, M(MAT_MARBLE), false);
    lathe(g, vec3(p, z + hgt - 0.45f), {vec2(r * 0.9f, 0.f), vec2(r * 1.1f, 0.12f), vec2(r * 1.25f, 0.28f), vec2(r * 1.2f, 0.33f)}, seg, col, M(MAT_MARBLE), false);
    boxY(g, vec3(p, z + hgt - 0.06f), vec2(1, 0), vec3(r * 1.4f, r * 1.4f, 0.06f), col, M(MAT_MARBLE), true);
}

// Triangular pediment over a portico front: base width w along a, rise, depth d toward -f from the front plane
void pediment(G& g, vec2 c, vec2 a, vec2 f, float z, float w, float rise, float d, u32 col) {
    vec3 l(c - a * w * 0.5f, z), r(c + a * w * 0.5f, z), t(c, z + rise);
    MeshData& m = *g.m;
    for (int side = 0; side < 2; side++) {
        vec3 off = vec3(-f * (side ? d : 0.f), 0.f);
        vec3 n = side ? vec3(-f, 0.f) : vec3(f, 0.f);
        u32 i0 = m.addVertex(l + off - g.org, n, vec3(a, 0.f), vec2(0, 0), col, M(MAT_STUCCO));
        u32 i1 = m.addVertex(r + off - g.org, n, vec3(a, 0.f), vec2(w, 0), col, M(MAT_STUCCO));
        u32 i2 = m.addVertex(t + off - g.org, n, vec3(a, 0.f), vec2(w * 0.5f, rise), col, M(MAT_STUCCO));
        if (dot(cross(r - l, t - l), n) > 0) m.tri(i0, i1, i2);
        else m.tri(i0, i2, i1);
    }
    // sloped tile tops
    vec3 dd = vec3(-f * d, 0.f);
    quad(g, m, l - vec3(f * 0.3f, 0.f), t - vec3(f * 0.3f, 0.f), t + dd, l + dd, rgbv(kTile), M(MAT_ROOF_TILE), normalize(vec3(-a, 0.f) + vec3(0, 0, 1.5f)));
    quad(g, m, t - vec3(f * 0.3f, 0.f), r - vec3(f * 0.3f, 0.f), r + dd, t + dd, rgbv(kTile), M(MAT_ROOF_TILE), normalize(vec3(a, 0.f) + vec3(0, 0, 1.5f)));
    // cornice under the triangle
    boxY(g, vec3(c - f * (d * 0.5f - 0.15f), z - 0.2f), a, vec3(w * 0.5f + 0.25f, d * 0.5f + 0.2f, 0.2f), col, M(MAT_STUCCO), true);
}

// Campus lamp: dark bronze post with a glass lantern
void campusLamp(G& g, vec2 p, float z) {
    u32 post = rgbv(kBronze);
    cyl(g, vec3(p, z), 0.14f, 0.12f, 0.4f, 8, post, M(MAT_METAL_PAINTED), true);
    cyl(g, vec3(p, z + 0.4f), 0.06f, 0.05f, 3.3f, 6, post, M(MAT_METAL_PAINTED), false);
    boxY(g, vec3(p, z + 3.95f), vec2(1, 0), vec3(0.2f, 0.2f, 0.26f), rgb(1.f, 0.88f, 0.66f, 0.45f), emMat(EA_NIGHT), false);
    lathe(g, vec3(p, z + 4.2f), {vec2(0.28f, 0.f), vec2(0.05f, 0.25f), vec2(0.f, 0.3f)}, 4, post, M(MAT_METAL_PAINTED), false, kPi * 0.25f);
    light(g, vec3(p, z + 3.9f), vec3(1.f, 0.85f, 0.62f) * 2000.f, 13.f, 0);
    collide(g, vec3(p, z + 2.f), vec2(1, 0), vec3(0.08f, 0.08f, 2.f));
}

// Name in raised bronze letters on a wall facing f, centred at c (bottom z)
void wallName(G& g, const char* t, vec2 c, vec2 f, float z, float h) {
    vec2 rt = perp(f);
    plainLetters(g, t, vec3(c + f * 0.03f, z) - vec3(rt * (textAdvance(t, h, 0.3f) * 0.5f), 0.f), vec3(rt, 0.f), vec3(0, 0, 1), h, rgbv(kBronze),
                 M(MAT_METAL_BRUSHED), 0.04f, 0.13f);
}

// Wayfinding pylon: slim stone post, two faces of labels
void pylon(G& g, vec2 p, float z, vec2 face, const char* text) {
    vec2 rt = perp(face);
    boxY(g, vec3(p, z + 1.1f), rt, vec3(0.35f, 0.14f, 1.1f), rgbv(kStone * 0.9f), M(MAT_STONE), false);
    boxY(g, vec3(p, z + 2.25f), rt, vec3(0.4f, 0.18f, 0.06f), rgbv(kTile), M(MAT_ROOF_TILE), true);
    collide(g, vec3(p, z + 1.1f), rt, vec3(0.35f, 0.14f, 1.1f));
    if (!g.detail) return;
    for (int s = 0; s < 2; s++) {
        vec2 fn = s ? -face : face;
        airport_mesh::signPanel(g, p + fn * 0.14f, fn, z + 0.9f, 0.62f, 1.2f, rgbv(kBronze), rgb(0.95f, 0.9f, 0.75f), text, M(MAT_METAL_PAINTED), 0.12f);
    }
}

// ------------------------------------------------------------------------------------------------ halls
void genHall(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    const Hall h = hallOf(e);
    bool detail = g.detail;
    const u32 fac = (u32)e.p[7];
    const vec2 f = h.f, a = h.a;
    const float z0 = h.z, top = z0 + h.H;
    const u32 trim = rgbv(kTrim), stone = rgbv(kStone), tile = rgbv(kTile);
    Rng r(h.seed);
    // walls and roof
    std::vector<vec2> fp = rectPoly(h.c, f, h.hd, h.hw);
    facadeRing(g, fp, z0 - 0.8f, top, z0, fac, gBuildings && e.p[6] >= 0.f ? gBuildings->facades[fac].bayW : 3.6f);
    collide(g, vec3(h.c, (z0 - 0.8f + top) * 0.5f), f, vec3(h.hd, h.hw, (top - z0 + 0.8f) * 0.5f));
    // plinth and cornice bands
    if (detail) {
        for (int k = 0; k < 4; k++) {
            vec2 p0 = fp[k], p1 = fp[(k + 1) % 4];
            vec2 d = normalize(p1 - p0), on = -perp(d);
            if (dot(on, (p0 + p1) * 0.5f - h.c) < 0) on = -on;
            float L = length(p1 - p0);
            boxY(g, vec3((p0 + p1) * 0.5f + on * 0.08f, z0 + 0.35f), d, vec3(L * 0.5f + 0.08f, 0.12f, 0.45f), rgbv(kStone * 0.82f), M(MAT_STONE));
            boxY(g, vec3((p0 + p1) * 0.5f + on * 0.18f, top - 0.25f), d, vec3(L * 0.5f + 0.18f, 0.22f, 0.25f), trim, M(MAT_STUCCO), true);
            if (h.floors > 1) boxY(g, vec3((p0 + p1) * 0.5f + on * 0.06f, z0 + h.gh - 0.1f), d, vec3(L * 0.5f + 0.06f, 0.08f, 0.1f), trim, M(MAT_STUCCO), true);
        }
    }
    // red tile roof
    landmark_mesh::hipRoof(g, h.c, a, h.hw + 0.7f, h.hd + 0.7f, top, Min(h.hw, h.hd) * 0.42f, tile, M(MAT_ROOF_TILE));
    polyFlat(g, *g.m, rectPoly(h.c, f, h.hd + 0.7f, h.hw + 0.7f), top - 0.02f, trim, M(MAT_STUCCO), vec3(0, 0, -1));
    // front: loggia (arcade) or portico
    const float frontV = h.hd;
    if (h.kind == HK_LIBRARY) {
        // portico: eight columns, pediment with the name, broad stairs; shallow copper dome on a drum over the centre
        float pw = Min(h.hw * 1.1f, 17.f);
        float colH = h.H - 1.6f;
        float pz = z0 + 1.2f;   // podium: the library sits up a flight of steps
        vec2 pc = h.P(0.f, frontV + 3.6f);
        boxY(g, vec3(pc, z0 + 0.6f), a, vec3(pw + 1.f, 3.8f, 0.6f), stone, M(MAT_MARBLE), false);
        for (int k = 0; k < 8; k++) {
            float u = -pw + 2.f * pw * (k + 0.5f) / 8.f;
            column(g, h.P(u, frontV + 6.2f), pz, colH, 0.55f, rgbv(kStoneLight));
            collide(g, vec3(h.P(u, frontV + 6.2f), pz + colH * 0.5f), a, vec3(0.6f, 0.6f, colH * 0.5f));
        }
        // entablature and pediment
        boxY(g, vec3(h.P(0.f, frontV + 3.6f), pz + colH + 0.55f), a, vec3(pw + 0.8f, 3.3f, 0.55f), rgbv(kStoneLight), M(MAT_STUCCO), true);
        pediment(g, h.P(0.f, frontV + 6.9f), a, f, pz + colH + 1.1f, 2.f * pw + 1.6f, 4.2f, 6.9f, rgbv(kStoneLight));
        if (detail) {
            wallName(g, kHallNames[h.name], h.P(0.f, frontV + 6.95f), f, pz + colH + 0.25f, 0.62f);
            // steps down to the quad
            for (int k = 0; k < 6; k++)
                boxY(g, vec3(h.P(0.f, frontV + 7.9f + k * 0.42f), z0 + 1.1f - k * 0.2f), a, vec3(pw - 2.f, 0.21f, 0.1f + (k == 5 ? 0.1f : 0.f)), stone,
                     M(MAT_MARBLE), false);
            // bronze doors
            quad(g, *g.m, vec3(h.P(-2.2f, frontV + 0.03f), pz), vec3(h.P(2.2f, frontV + 0.03f), pz), vec3(h.P(2.2f, frontV + 0.03f), pz + 5.f),
                 vec3(h.P(-2.2f, frontV + 0.03f), pz + 5.f), rgbv(kBronze * 0.8f), M(MAT_METAL_BRUSHED), vec3(f, 0.f));
            // reading-room lamps glowing behind the portico at night
            for (int k = -2; k <= 2; k++) light(g, vec3(h.P(k * 6.f, frontV + 4.f), pz + colH - 0.5f), vec3(1.f, 0.82f, 0.58f) * 2600.f, 11.f, 1);
            // floodlights on the colonnade
            for (int k = -1; k <= 1; k += 2) light(g, vec3(h.P(k * 10.f, frontV + 16.f), z0 + 0.3f), vec3(1.f, 0.9f, 0.75f) * 9000.f, 30.f, 1,
                                                   normalize(vec3(-f, 0.9f)), 0.2f);
        }
        collide(g, vec3(pc, z0 + 0.6f), a, vec3(pw + 1.f, 3.8f, 0.6f));
        // drum and dome
        vec2 dc = h.P(0.f, 0.f);
        float dr = Min(h.hd, 11.f);
        cyl(g, vec3(dc, top + 2.5f), dr, dr, 4.f, detail ? 28 : 12, rgbv(kStoneLight), M(MAT_STUCCO), false);
        if (detail)
            for (int k = 0; k < 16; k++) {
                float an = kTwoPi * k / 16.f;
                vec2 d(cosf(an), sinf(an));
                quad(g, *g.m, vec3(dc + d * (dr + 0.02f) - perp(d) * 0.7f, top + 3.f), vec3(dc + d * (dr + 0.02f) + perp(d) * 0.7f, top + 3.f),
                     vec3(dc + d * (dr + 0.02f) + perp(d) * 0.7f, top + 5.8f), vec3(dc + d * (dr + 0.02f) - perp(d) * 0.7f, top + 5.8f), rgb(0.12f, 0.15f, 0.17f),
                     M(MAT_GLASS), vec3(d, 0));
            }
        std::vector<vec2> dp;
        for (int k = 0; k <= 8; k++) {
            float t = kHalfPi * k / 8.f;
            dp.push_back(vec2(cosf(t) * (dr + 0.4f), sinf(t) * dr * 0.62f));
        }
        lathe(g, vec3(dc, top + 6.5f), dp, detail ? 28 : 12, rgb(0.42f, 0.66f, 0.56f), M(MAT_ROOF_METAL), false);
        cyl(g, vec3(dc, top + 6.5f), dr + 0.6f, dr + 0.6f, 0.35f, detail ? 28 : 12, trim, M(MAT_STUCCO), false);
        cyl(g, vec3(dc, top + 6.3f + dr * 0.62f), 1.2f, 1.f, 2.2f, 10, rgbv(kStoneLight), M(MAT_STUCCO), true);
        lathe(g, vec3(dc, top + 8.5f + dr * 0.62f), {vec2(1.3f, 0.f), vec2(0.9f, 0.8f), vec2(0.f, 1.6f)}, 10, rgb(0.42f, 0.66f, 0.56f), M(MAT_ROOF_METAL), false);
        light(g, vec3(dc + f * (dr + 6.f), top + 4.f), vec3(0.85f, 0.9f, 1.f) * 5000.f, 22.f, 1, normalize(vec3(-f, 0.6f)), 0.25f);
    } else if (h.loggia > 0.f) {
        // arcaded loggia along the front: piers, round arches, tile shed roof over it
        float L = h.hw * 2.f - 2.f;
        int bays = Max(3, (int)roundf(L / 4.2f));
        float lz = z0 + h.gh - 0.4f;
        vec2 lc = h.P(0.f, frontV + h.loggia * 0.5f);
        polyFlat(g, *g.m, rectPoly(lc, f, h.loggia * 0.5f, h.hw - 1.f), z0 + 0.02f, rgb(0.9f, 0.62f, 0.5f), M(MAT_TILE));
        for (int k = 0; k <= bays; k++) {
            float u = -L * 0.5f + L * k / bays;
            vec2 pp = h.P(u, frontV + h.loggia - 0.35f);
            boxY(g, vec3(pp, (z0 + lz) * 0.5f), a, vec3(0.35f, 0.35f, (lz - z0) * 0.5f), stone, M(MAT_STONE));
            if (detail) collide(g, vec3(pp, (z0 + lz) * 0.5f), a, vec3(0.35f, 0.35f, (lz - z0) * 0.5f));
            if (k < bays && detail) {
                float u1 = -L * 0.5f + L * (k + 1) / bays;
                vec2 np = h.P(u1, frontV + h.loggia - 0.35f);
                landmark_mesh::arch(g, vec3(pp + a * 0.35f, lz - 1.4f), vec3(np - a * 0.35f, lz - 1.4f), 1.3f, 0.3f, 0.7f, stone, M(MAT_STONE), vec3(f, 0.f), 6);
            }
        }
        boxY(g, vec3(h.P(0.f, frontV + h.loggia - 0.35f), lz + 0.4f), a, vec3(L * 0.5f + 0.35f, 0.4f, 0.4f), stone, M(MAT_STONE), true);
        // shed roof
        vec3 r0(h.P(-L * 0.5f - 0.5f, frontV), lz + 1.6f), r1(h.P(L * 0.5f + 0.5f, frontV), lz + 1.6f);
        vec3 r2(h.P(L * 0.5f + 0.5f, frontV + h.loggia + 0.4f), lz + 0.8f), r3(h.P(-L * 0.5f - 0.5f, frontV + h.loggia + 0.4f), lz + 0.8f);
        quad(g, *g.m, r3, r2, r1, r0, tile, M(MAT_ROOF_TILE), vec3(f * 0.6f, 1.f));
        quad(g, *g.m, r0, r1, r2, r3, trim, M(MAT_STUCCO), vec3(0, 0, -1));
        // central entrance: taller arch with the hall's name above
        if (detail) {
            vec2 ec = h.P(0.f, frontV + 0.04f);
            quad(g, *g.m, vec3(ec - a * 1.4f, z0), vec3(ec + a * 1.4f, z0), vec3(ec + a * 1.4f, z0 + 3.f), vec3(ec - a * 1.4f, z0 + 3.f), rgbv(kBronze * 0.9f),
                 M(MAT_WOOD), vec3(f, 0.f));
            wallName(g, kHallNames[h.name], h.P(0.f, frontV + h.loggia - 0.02f), f, lz + 0.05f, 0.5f);
            for (int k = -1; k <= 1; k += 2) light(g, vec3(h.P(k * L * 0.25f, frontV + h.loggia * 0.5f), lz - 0.3f), vec3(1.f, 0.82f, 0.6f) * 1500.f, 8.f, 1);
        }
    } else {
        // plain entrance with a canopy and the name (dorms, admin, union, field house)
        vec2 ec = h.P(0.f, frontV);
        boxY(g, vec3(ec + f * 1.3f, z0 + 3.3f), a, vec3(3.2f, 1.3f, 0.15f), trim, M(MAT_STUCCO), true);
        for (int k = -1; k <= 1; k += 2) {
            cyl(g, vec3(ec + f * 2.3f + a * (k * 2.9f), z0), 0.16f, 0.16f, 3.2f, 8, trim, M(MAT_STUCCO), false);
            if (detail) collide(g, vec3(ec + f * 2.3f + a * (k * 2.9f), z0 + 1.6f), a, vec3(0.16f, 0.16f, 1.6f));
        }
        quad(g, *g.m, vec3(ec - a * 1.6f + f * 0.03f, z0), vec3(ec + a * 1.6f + f * 0.03f, z0), vec3(ec + a * 1.6f + f * 0.03f, z0 + 2.8f),
             vec3(ec - a * 1.6f + f * 0.03f, z0 + 2.8f), rgb(0.18f, 0.22f, 0.24f), M(MAT_GLASS), vec3(f, 0.f));
        if (detail) {
            wallName(g, kHallNames[h.name], h.P(0.f, frontV + 2.62f), f, z0 + 3.1f, 0.34f);
            light(g, vec3(ec + f * 1.3f, z0 + 3.0f), vec3(1.f, 0.85f, 0.65f) * 1800.f, 8.f, 1, vec3(0, 0, -1), 0.3f);
        }
    }
    // bike racks and a bin by the entrance
    if (detail && h.kind != HK_LIBRARY) {
        float fv = frontV + (h.loggia > 0.f ? h.loggia + 1.6f : 3.6f);
        for (int s = -1; s <= 1; s += 2) prop(g, vec3(h.P(s * Min(h.hw - 3.f, 8.f), fv), z0), yawAlong(a), 1.f, PROP_BIKE_RACK);
        prop(g, vec3(h.P(5.f, fv - 0.5f), z0), 0.f, 1.f, PROP_BIN);
    }
    // dorms: balconies with railings on the river side (back), window AC units
    if (h.kind == HK_DORM && detail) {
        for (int fl = 1; fl < h.floors; fl++) {
            float zf = z0 + h.gh + (fl - 1) * h.fh;
            for (int k = -2; k <= 2; k++) {
                vec2 bc = h.P(k * (h.hw * 0.36f), -h.hd - 0.7f);
                boxY(g, vec3(bc, zf), a, vec3(1.6f, 0.7f, 0.08f), trim, M(MAT_CONCRETE), true);
                ironRailing(g, bc - f * 0.66f - a * 1.55f, bc - f * 0.66f + a * 1.55f, zf + 0.08f, 1.0f, 0.3f, rgbv(kBronze), false, false);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ bell tower
void genTower(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 f = e.ax, a = perp(f);
    float z0 = e.z, hs = e.hx, H = e.h;
    u32 stone = rgbv(kStone), trim = rgbv(kTrim);
    float shaftTop = z0 + H - 14.f;
    // shaft with corner buttresses
    prism(g, rectPoly(e.c, f, hs, hs), z0 - 0.5f, shaftTop, stone, M(MAT_STONE), stone, M(MAT_STONE), false);
    for (int k = 0; k < 4; k++) {
        vec2 cp = e.c + f * ((k & 1) ? hs : -hs) + a * ((k & 2) ? hs : -hs);
        boxY(g, vec3(cp, (z0 + shaftTop) * 0.5f), f, vec3(0.55f, 0.55f, (shaftTop - z0) * 0.5f + 0.2f), rgbv(kStone * 0.92f), M(MAT_STONE));
    }
    collide(g, vec3(e.c, (z0 + shaftTop) * 0.5f), f, vec3(hs + 0.5f, hs + 0.5f, (shaftTop - z0) * 0.5f));
    // door and slit windows
    quad(g, *g.m, vec3(e.c + f * (hs + 0.02f) - a * 1.1f, z0), vec3(e.c + f * (hs + 0.02f) + a * 1.1f, z0), vec3(e.c + f * (hs + 0.02f) + a * 1.1f, z0 + 3.2f),
         vec3(e.c + f * (hs + 0.02f) - a * 1.1f, z0 + 3.2f), rgbv(kBronze), M(MAT_WOOD), vec3(f, 0));
    if (detail)
        for (int k = 0; k < 4; k++) {
            vec2 n = rotate(f, kHalfPi * k), t = perp(n);
            for (int j = 0; j < 4; j++) {
                float zz = z0 + 7.f + j * 5.5f;
                quad(g, *g.m, vec3(e.c + n * (hs + 0.02f) - t * 0.3f, zz), vec3(e.c + n * (hs + 0.02f) + t * 0.3f, zz), vec3(e.c + n * (hs + 0.02f) + t * 0.3f, zz + 1.6f),
                     vec3(e.c + n * (hs + 0.02f) - t * 0.3f, zz + 1.6f), rgb(0.08f, 0.08f, 0.09f), M(MAT_GLASS), vec3(n, 0));
            }
        }
    // clock stage: projecting band, four clock faces
    boxY(g, vec3(e.c, shaftTop + 0.3f), f, vec3(hs + 0.5f, hs + 0.5f, 0.3f), trim, M(MAT_STUCCO), true);
    float clockZ = shaftTop - 3.2f;
    for (int k = 0; k < 4; k++) {
        vec2 n = rotate(f, kHalfPi * k), t = perp(n);
        vec3 cc(e.c + n * (hs + 0.56f), clockZ);
        boxY(g, vec3(e.c + n * (hs + 0.25f), clockZ), t, vec3(2.3f, 0.3f, 2.3f), trim, M(MAT_STUCCO), true);
        disc(g, cc + vec3(n * 0.01f, 0.f), vec3(n, 0.f), 1.9f, detail ? 24 : 10, rgb(1.f, 0.97f, 0.88f, 0.3f), emMat(EA_NIGHT));
        if (detail) {
            ring(g, cc, vec3(n, 0.f), 1.95f, 0.12f, 0.06f, 20, rgbv(kBronze), M(MAT_METAL_BRUSHED));
            for (int m = 0; m < 12; m++) {
                float an = kTwoPi * m / 12.f;
                vec3 d = vec3(t * cosf(an), sinf(an));
                beam(g, cc + d * 1.55f + vec3(n * 0.04f, 0.f), cc + d * 1.75f + vec3(n * 0.04f, 0.f), 0.08f, 0.03f, rgbv(kBronze), M(MAT_METAL_PAINTED), vec3(n, 0.f));
            }
            // hands at ten past ten
            beam(g, cc + vec3(n * 0.07f, 0.f), cc + vec3(t * (-0.9f), 0.65f) + vec3(n * 0.07f, 0.f), 0.12f, 0.04f, rgb(0.08f), M(MAT_METAL_PAINTED), vec3(n, 0.f));
            beam(g, cc + vec3(n * 0.09f, 0.f), cc + vec3(t * 1.2f, 1.f) + vec3(n * 0.09f, 0.f), 0.08f, 0.04f, rgb(0.08f), M(MAT_METAL_PAINTED), vec3(n, 0.f));
        }
    }
    // belfry: open arches on four piers, bells inside
    float bz0 = shaftTop + 0.6f, bz1 = bz0 + 6.5f;
    for (int k = 0; k < 4; k++) {
        vec2 cp = e.c + f * ((k & 1) ? hs - 0.7f : -hs + 0.7f) + a * ((k & 2) ? hs - 0.7f : -hs + 0.7f);
        boxY(g, vec3(cp, (bz0 + bz1) * 0.5f), f, vec3(0.7f, 0.7f, (bz1 - bz0) * 0.5f), stone, M(MAT_STONE));
    }
    for (int k = 0; k < 4; k++) {
        vec2 n = rotate(f, kHalfPi * k), t = perp(n);
        if (detail) landmark_mesh::arch(g, vec3(e.c + n * (hs - 0.35f) - t * (hs - 1.4f), bz1 - 2.2f), vec3(e.c + n * (hs - 0.35f) + t * (hs - 1.4f), bz1 - 2.2f), 1.8f,
                                        0.4f, 0.7f, stone, M(MAT_STONE), vec3(n, 0.f), 8);
        boxY(g, vec3(e.c + n * (hs - 0.35f), bz0 + 0.5f), t, vec3(hs, 0.35f, 0.5f), stone, M(MAT_STONE), true);   // parapet between the piers
    }
    boxY(g, vec3(e.c, bz1 + 0.3f), f, vec3(hs + 0.3f, hs + 0.3f, 0.3f), trim, M(MAT_STUCCO), true);
    if (detail) {
        lathe(g, vec3(e.c, bz1 - 3.2f), {vec2(0.1f, 0.f), vec2(0.9f, 0.4f), vec2(1.f, 1.2f), vec2(0.6f, 1.8f), vec2(0.f, 2.f)}, 12, rgb(0.72f, 0.55f, 0.25f),
              M(MAT_METAL_BRUSHED), false);
        light(g, vec3(e.c, bz0 + 3.f), vec3(1.f, 0.8f, 0.55f) * 3500.f, 12.f, 1);
    }
    // octagonal lantern and copper cupola, finial
    float lz = bz1 + 0.6f;
    cyl(g, vec3(e.c, lz), hs * 0.72f, hs * 0.68f, 3.2f, 8, trim, M(MAT_STUCCO), false);
    lathe(g, vec3(e.c, lz + 3.2f), {vec2(hs * 0.78f, 0.f), vec2(hs * 0.7f, 1.2f), vec2(hs * 0.42f, 3.f), vec2(0.18f, 4.6f), vec2(0.f, 4.9f)}, detail ? 16 : 8,
          rgb(0.42f, 0.66f, 0.56f), M(MAT_ROOF_METAL), false);
    cyl(g, vec3(e.c, lz + 7.9f), 0.06f, 0.03f, 2.4f, 5, rgb(0.8f, 0.65f, 0.3f), M(MAT_METAL_BRUSHED), false);
    lamp(g, vec3(e.c, lz + 10.4f), 0.3f, vec3(1.f, 0.1f, 0.05f), 0.9f, EA_BLINK, 40);
    // floodlights washing the shaft at night
    if (detail)
        for (int k = 0; k < 2; k++) {
            vec2 n = rotate(f, kPi * k);
            light(g, vec3(e.c + n * (hs + 7.f), z0 + 0.3f), vec3(1.f, 0.88f, 0.7f) * 12000.f, 40.f, 1, normalize(vec3(-n, 2.2f)), 0.12f);
        }
}

// ------------------------------------------------------------------------------------------------ grounds
// variant 0: the quad (mowing stripes, diagonals, perimeter walk, oaks, benches, lamps, pylons); 1: the gate and the
// avenue up to the quad; 2: the river walk in front of the residence halls; 3: the parking lot and the walk to the
// field; 4: the lawn under the whole campus and the walks between the halls (pts: segment pairs, p[0] width).
// c/hx/hy: the axis-aligned area; z: ground.
void genGrounds(const SiteElem& e, G& g) {
    bool detail = g.detail;
    const float z = e.z;
    const vec2 X(1, 0), Y(0, 1);
    float x0 = e.c.x - e.hx, x1 = e.c.x + e.hx, y0 = e.c.y - e.hy, y1 = e.c.y + e.hy;
    u32 lawnC = rgb(0.58f, 0.9f, 0.46f), walkC = rgb(0.95f, 0.88f, 0.8f);
    Rng r(e.seed ^ (u32)(g.cx * 73856093) ^ (u32)(g.cy * 19349663));
    auto lawn = [&](float ax0, float ay0, float ax1, float ay1, u32 c) {
        drapeRect(g, vec2((ax0 + ax1) * 0.5f, (ay0 + ay1) * 0.5f), X, (ax1 - ax0) * 0.5f, (ay1 - ay0) * 0.5f, 0.04f, c, M(MAT_GRASS), detail ? 8.f : 24.f, z);
    };
    auto walk = [&](vec2 a, vec2 b, float w) { pathStrip(g, {a, b}, w, 0.1f, walkC, M(MAT_PAVERS), z, true, true); };
    if (e.variant == 4) {
        lawn(x0, y0, x1, y1, lawnC);
        for (size_t k = 0; k + 1 < e.pts.size(); k += 2) walk(e.pts[k], e.pts[k + 1], e.p[0]);
        return;
    }
    if (e.variant == 0) {
        // mown quad: lighter stripes over the lawn, framed by a perimeter walk, crossed by the diagonals, the axis and the
        // cross walk
        vec2 c = e.c;
        float hx = e.hx, hy = e.hy;
        for (int k = 1; k < 10; k += 2) {
            float ya = y0 + 4.f + (y1 - y0 - 8.f) * k / 10.f, yb = y0 + 4.f + (y1 - y0 - 8.f) * (k + 1) / 10.f;
            drapeRect(g, vec2((x0 + x1) * 0.5f, (ya + yb) * 0.5f), X, (x1 - x0) * 0.5f - 4.f, (yb - ya) * 0.5f, 0.07f, rgb(0.66f, 1.f, 0.52f), M(MAT_GRASS),
                      detail ? 7.f : 20.f, z);
        }
        std::vector<vec2> per = {vec2(x0 + 2.f, y0 + 2.f), vec2(x1 - 2.f, y0 + 2.f), vec2(x1 - 2.f, y1 - 2.f), vec2(x0 + 2.f, y1 - 2.f), vec2(x0 + 2.f, y0 + 2.f)};
        for (int k = 0; k < 4; k++) walk(per[k], per[k + 1], 4.f);
        const float pr = 16.f;   // plaza round the fountain
        for (int k = 0; k < 4; k++) {
            vec2 corner = per[k];
            vec2 d = normalize(c - corner);
            walk(corner, c - d * pr, 3.2f);
        }
        walk(vec2(c.x, y0 + 2.f), vec2(c.x, c.y - pr), 5.f);
        walk(vec2(c.x, c.y + pr), vec2(c.x, y1 - 2.f), 5.f);
        walk(vec2(x0 + 2.f, c.y), vec2(c.x - pr, c.y), 3.2f);
        walk(vec2(c.x + pr, c.y), vec2(x1 - 2.f, c.y), 3.2f);
        if (g.owns(c)) {
            std::vector<vec2> disc = circleFP(c, pr + 1.f, detail ? 36 : 14);
            size_t v0 = g.m->verts.size();
            polyFlat(g, *g.m, disc, z + 0.1f, walkC, M(MAT_PAVERS));
            paverUV(g, *g.m, v0);
        }
        if (!detail) return;
        // live oaks round the perimeter, clear of the walks
        for (float t = 0.08f; t < 0.95f; t += 0.14f) {
            for (int side = 0; side < 4; side++) {
                vec2 a = per[side], b = per[side + 1];
                vec2 p = lerp(a, b, t) + normalize(c - lerp(a, b, 0.5f)) * 6.5f;
                if (!g.owns(p)) continue;
                prop(g, vec3(p, z), r.f() * kTwoPi, r.range(1.1f, 1.45f), PROP_TREE_OAK, (u8)r.irange(0, 3));
            }
        }
        // benches facing the lawn along the axis and the cross walk; lamps along every walk
        for (float s = -hy + 14.f; s < hy - 10.f; s += 18.f) {
            if (fabsf(s) < pr + 4.f) continue;
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 bp = vec2(c.x + sd * 4.2f, c.y + s);
                if (g.owns(bp)) prop(g, vec3(bp, z + 0.1f), yawFacing(X * (float)sd), 1.f, PROP_BENCH);
                vec2 lp = vec2(c.x + sd * 3.4f, c.y + s + 9.f);
                if (g.owns(lp)) campusLamp(g, lp, z + 0.1f);
            }
        }
        for (float s = -hx + 12.f; s < hx - 8.f; s += 16.f) {
            if (fabsf(s) < pr + 4.f) continue;
            vec2 lp = vec2(c.x + s, c.y + 2.6f);
            if (g.owns(lp)) campusLamp(g, lp, z + 0.1f);
        }
        for (int k = 0; k < 4; k++) {
            vec2 corner = per[k];
            vec2 d = normalize(c - corner), n = perp(d);
            float L = length(c - corner) - pr;
            for (float s = 10.f; s < L - 4.f; s += 20.f) {
                vec2 lp = corner + d * s + n * 2.4f;
                if (g.owns(lp)) campusLamp(g, lp, z + 0.1f);
                vec2 bp = corner + d * (s + 10.f) - n * 2.6f;
                if (g.owns(bp)) prop(g, vec3(bp, z + 0.1f), yawFacing(-n), 1.f, PROP_BENCH);
            }
        }
        // wayfinding pylons at the four corners of the fountain plaza
        const char* signs[4] = {">LIBRARY|>BELL TOWER|<GATE", ">TARPON FIELD|>RESIDENCE HALLS|<UNION", "<CASTELL HALL|>ADMINISTRATION|>PARKING",
                                "<LIBRARY|<SAWGRASS HALL|>STUDENT UNION"};
        for (int k = 0; k < 4; k++) {
            float an = kPi * 0.25f + kHalfPi * k;
            vec2 pp = c + vec2(cosf(an), sinf(an)) * (pr + 2.6f);
            if (g.owns(pp)) pylon(g, pp, z + 0.1f, normalize(c - pp), signs[k]);
        }
        // flower beds round the plaza edge
        for (int k = 0; k < 8; k++) {
            float an = kTwoPi * (k + 0.5f) / 8.f;
            vec2 bp = c + vec2(cosf(an), sinf(an)) * (pr + 6.f);
            if (!g.owns(bp)) continue;
            leafBlobEx(*g.m, g.org, vec3(bp, z + 0.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(1.4f, 1.4f, 0.45f), vec3(0.4f, 0.62f, 0.3f), r.next(), 0.08f, 10, 5,
                       (k & 1) ? vec3(0.95f, 0.35f, 0.5f) : vec3(1.f, 0.85f, 0.25f));
        }
    } else if (e.variant == 1) {
        // gate plaza on N 34th Street: pavers from the sidewalk to the quad, gate piers with iron gates and the arch sign,
        // lawns with palms either side, the monument sign at the corner
        float gx = e.p[0];   // the axis x
        {
            vec2 pc(gx, (y0 + y1) * 0.5f);
            if (g.owns(pc)) {
                std::vector<vec2> pz = rectPoly(pc, X, 14.f, e.hy);
                size_t v0 = g.m->verts.size();
                polyFlat(g, *g.m, pz, z + 0.1f, walkC, M(MAT_PAVERS));
                paverUV(g, *g.m, v0);
            }
        }
        vec2 gc(gx, y0 + 6.f);
        if (g.owns(gc)) {
            u32 stone = rgbv(kStone), bronze = rgbv(kBronze);
            // piers
            for (int s = -1; s <= 1; s += 2) {
                vec2 pp = gc + X * (s * 7.f);
                boxY(g, vec3(pp, z + 2.9f), X, vec3(1.f, 1.f, 2.9f), stone, M(MAT_STONE));
                boxY(g, vec3(pp, z + 5.95f), X, vec3(1.2f, 1.2f, 0.15f), rgbv(kTrim), M(MAT_STUCCO), true);
                lathe(g, vec3(pp, z + 6.1f), {vec2(0.5f, 0.f), vec2(0.55f, 0.4f), vec2(0.f, 1.f)}, 10, rgbv(kTile), M(MAT_ROOF_TILE), false);
                collide(g, vec3(pp, z + 2.9f), X, vec3(1.f, 1.f, 2.9f));
                // side piers and low walls running out to the fence line
                vec2 sp = gc + X * (s * 13.f);
                boxY(g, vec3(sp, z + 1.3f), X, vec3(0.6f, 0.6f, 1.3f), stone, M(MAT_STONE));
                copedWall(g, sp + X * (s * 0.6f), vec2(s > 0 ? x1 : x0, gc.y), z, 1.1f, 0.5f, stone, M(MAT_STONE), rgbv(kTrim), M(MAT_STUCCO), true);
                if (detail) {
                    // open iron gate leaves swung back against the piers
                    vec2 hinge = pp - X * (s * 1.f);
                    vec2 open = normalize(Y * 0.9f - X * (s * 0.35f));
                    for (int k = 0; k <= 10; k++) {
                        vec2 q = hinge + open * (k * 0.25f);
                        boxY(g, vec3(q, z + 1.6f), open, vec3(0.02f, 0.02f, 1.6f), bronze, M(MAT_METAL_PAINTED));
                        boxY(g, vec3(q, z + 3.25f + sinf(k / 10.f * kPi) * 0.3f), open, vec3(0.04f, 0.04f, 0.06f), bronze, M(MAT_METAL_PAINTED));
                    }
                    beam(g, vec3(hinge, z + 0.4f), vec3(hinge + open * 2.5f, z + 0.4f), 0.05f, 0.05f, bronze, M(MAT_METAL_PAINTED));
                    beam(g, vec3(hinge, z + 2.9f), vec3(hinge + open * 2.5f, z + 2.9f), 0.05f, 0.05f, bronze, M(MAT_METAL_PAINTED));
                }
            }
            // iron arch over the gate with the university's name in bronze letters and lanterns
            if (detail) {
                for (int k = 0; k < 12; k++) {
                    float t0 = k / 12.f, t1 = (k + 1) / 12.f;
                    vec3 p0(gc + X * Lerp(-6.f, 6.f, t0), z + 5.8f + sinf(t0 * kPi) * 1.4f), p1(gc + X * Lerp(-6.f, 6.f, t1), z + 5.8f + sinf(t1 * kPi) * 1.4f);
                    beam(g, p0, p1, 0.1f, 0.12f, bronze, M(MAT_METAL_PAINTED), vec3(0, 1, 0));
                    beam(g, p0 - vec3(0, 0, 0.9f), p1 - vec3(0, 0, 0.9f), 0.06f, 0.08f, bronze, M(MAT_METAL_PAINTED), vec3(0, 1, 0));
                }
                const char* nm = "PORTO SOL UNIVERSITY";
                float th = 0.5f;
                // cut-out letters in the arch (read from the street; mirrored from the campus side, like the real thing)
                plainLetters(g, nm, vec3(gc, z + 5.05f) - vec3(X * (textAdvance(nm, th, 0.3f) * 0.5f), 0.f), vec3(X, 0.f), vec3(0, 0, 1), th,
                             rgb(0.85f, 0.7f, 0.35f), M(MAT_METAL_BRUSHED), 0.05f, 0.14f);
                for (int s = -1; s <= 1; s += 2) {
                    vec2 lp = gc + X * (s * 7.f) - Y * 1.1f;
                    boxY(g, vec3(lp, z + 4.6f), X, vec3(0.22f, 0.22f, 0.3f), rgb(1.f, 0.86f, 0.62f, 0.5f), emMat(EA_NIGHT));
                    light(g, vec3(lp - Y * 0.5f, z + 4.4f), vec3(1.f, 0.85f, 0.62f) * 2200.f, 11.f, 1);
                }
            }
        }
        if (detail) {
            // royal palms flanking the plaza, the monument sign on the corner lawn
            for (float y = y0 + 12.f; y < e.p[1] - 4.f; y += 11.f)
                for (int s = -1; s <= 1; s += 2) {
                    vec2 pp(gx + s * 16.5f, y);
                    if (g.owns(pp)) prop(g, vec3(pp, z), y * 0.3f, 1.2f, PROP_PALM_TALL, (u8)((int)(y) & 1));
                }
            vec2 ms(x0 + 16.f, y0 + 9.f);
            if (g.owns(ms)) monumentSign(g, ms, -Y, z, 9.f, 2.2f, "PORTO SOL UNIVERSITY", "FOUNDED 1911  -  LUX ET MARE", rgbv(kStone), M(MAT_STONE),
                                         rgb(0.85f, 0.72f, 0.4f), true);
            for (int k = 0; k < 3; k++) {
                vec2 fpp(gx - 21.f + k * 3.5f, y0 + 14.f);
                if (g.owns(fpp)) flagPole(g, fpp, z, 12.f, X, k == 1 ? vec3(0.1f, 0.45f, 0.5f) : vec3(0.95f, 0.6f, 0.2f), vec3(0.97f));
            }
        }
    } else if (e.variant == 2) {
        // river walk along the residence halls, palms and benches toward Bayshore Boulevard
        walk(vec2(x0 + 4.f, e.c.y), vec2(x1 - 4.f, e.c.y), 3.2f);
        if (!detail) return;
        for (float x = x0 + 8.f; x < x1 - 6.f; x += 13.f) {
            vec2 pp(x, y1 - 4.f);
            if (g.owns(pp)) prop(g, vec3(pp, z), x, 1.1f, PROP_PALM, (u8)((int)x & 3));
            vec2 bp(x + 6.5f, e.c.y + 2.6f);
            if (g.owns(bp)) prop(g, vec3(bp, z + 0.1f), yawFacing(Y), 1.f, PROP_BENCH);
            vec2 lp(x + 3.f, e.c.y - 2.4f);
            if (g.owns(lp)) campusLamp(g, lp, z + 0.1f);
        }
    } else {
        // parking lot off N 34th Street (four rows of stalls on two aisles) and the walk to the field
        float lx0 = e.p[0], lx1 = e.p[1], ly0 = e.p[2], ly1 = e.p[3];   // parking lot rectangle
        vec2 lc((lx0 + lx1) * 0.5f, (ly0 + ly1) * 0.5f);
        if (g.owns(lc)) polyFlat(g, *g.m, rectPoly(lc, X, (lx1 - lx0) * 0.5f, (ly1 - ly0) * 0.5f), z + 0.08f, rgb(0.95f), M(MAT_ASPHALT_OLD));
        int stalls = (int)((lx1 - lx0 - 6.f) / 2.6f);
        const float rowY[4] = {ly0 + 0.5f, ly0 + 18.f, ly0 + 18.2f, ly1 - 0.5f};
        for (int row = 0; row < 4; row++) {
            vec2 face = (row & 1) ? -Y : Y;
            if (rowY[row] + (row & 1 ? -5.2f : 5.2f) > ly1 + 0.1f || rowY[row] - 5.2f * (row & 1) < ly0 - 0.1f) continue;
            stallRow(g, vec2((lx0 + lx1) * 0.5f, rowY[row]), X, face, z + 0.08f, stalls, 5.2f, 0.6f, e.seed + (u32)row * 101u);
        }
        if (detail) {
            for (float x = lx0 + 12.f; x < lx1 - 6.f; x += 24.f) {
                vec2 lp(x, ly0 + 18.1f);
                if (g.owns(lp)) decoLamp(g, lp, z + 0.08f, 7.5f, vec3(1.f, 0.85f, 0.6f), rgb(0.3f));
            }
            vec2 sp(lx1 - 3.f, ly0 - 3.f);
            if (g.owns(sp)) airport_mesh::signPanel(g, sp, -Y, z + 0.8f, 2.6f, 1.3f, rgbv(kBronze), rgb(0.95f, 0.9f, 0.75f), "VISITOR PARKING|PERMIT HOLDERS|>TARPON FIELD",
                                                  M(MAT_METAL_PAINTED), 0.2f);
            walk(vec2(x0 + 2.f, e.p[4]), vec2(x1 - 2.f, e.p[4]), 3.f);
        }
    }
}

// ------------------------------------------------------------------------------------------------ Tarpon Field
// Track (8 lanes) round a football field, bleachers on the west straight with a press box, scoreboard at the north
// end, four floodlight masts, fence. c: centre, ax: the track's long axis, hx/hy: half length / half width of the
// outer track edge, z: ground; p[0]: bleacher side (+1 east, -1 west)
void genField(const SiteElem& e, G& g) {
    bool detail = g.detail;
    vec2 L = e.ax, W = perp(e.ax);
    const float z = e.z;
    const float straight = 84.4f * 0.5f, rIn = 36.8f, lanes = 6.f, laneW = 1.22f, rOut = rIn + lanes * laneW;
    auto P = [&](float l, float w) { return e.c + L * l + W * w; };
    // point of the stadium oval at parameter t (0..1 round the track) and radius rr: the four sections (straight, bend,
    // straight, bend) take the same share of t at every radius, so points at one t line up across the lanes
    auto ovalPt = [&](float t, float rr) {
        const float rm = (rIn + rOut) * 0.5f;
        const float s1 = 2.f * straight, s2 = kPi * rm, perim = 2.f * (s1 + s2);
        float s = t * perim;
        if (s < s1) return P(-straight + s, -rr);
        s -= s1;
        if (s < s2) {
            float an = -kHalfPi + kPi * (s / s2);
            return P(straight + cosf(an) * rr, sinf(an) * rr);
        }
        s -= s2;
        if (s < s1) return P(straight - s, rr);
        s -= s1;
        float an = kHalfPi + kPi * (s / s2);
        return P(-straight + cosf(an) * rr, sinf(an) * rr);
    };
    // track surface: a ring of quads between the inner and outer edge (terracotta rubber), lane lines
    int seg = detail ? 96 : 40;
    u32 trackC = rgb(0.72f, 0.3f, 0.22f), infield = rgb(0.56f, 0.9f, 0.44f);
    for (int k = 0; k < seg; k++) {
        float t0 = (float)k / seg, t1 = (float)(k + 1) / seg;
        vec2 a0 = ovalPt(t0, rIn), a1 = ovalPt(t1, rIn), b0 = ovalPt(t0, rOut), b1 = ovalPt(t1, rOut);
        if (!g.owns((a0 + b1) * 0.5f)) continue;
        quad(g, *g.m, vec3(a0, z + 0.06f), vec3(a1, z + 0.06f), vec3(b1, z + 0.06f), vec3(b0, z + 0.06f), trackC, M(MAT_RUBBER), vec3(0, 0, 1));
        if (detail)
            for (int l = 1; l < (int)lanes; l++) {
                float rr = rIn + l * laneW;
                paintLine(g, ovalPt(t0, rr), ovalPt(t1, rr), 0.05f, z + 0.07f, kWhiteC, M(MAT_PAINT_WHITE));
            }
    }
    // infield grass (the straights' rectangle + two half discs) with mowing stripes and a football field
    for (int k = 0; k < 10; k++) {
        float l0 = -straight + 2.f * straight * k / 10.f, l1 = -straight + 2.f * straight * (k + 1) / 10.f;
        vec2 cc = P((l0 + l1) * 0.5f, 0.f);
        if (!g.owns(cc)) continue;
        polyFlat(g, *g.m, rectPoly(cc, L, (l1 - l0) * 0.5f, rIn), z + 0.05f, (k & 1) ? infield : rgb(0.62f, 0.96f, 0.5f), M(MAT_GRASS));
    }
    for (int s = -1; s <= 1; s += 2) {
        vec2 cc = P(s * straight, 0.f);
        if (!g.owns(cc)) continue;
        std::vector<vec2> hd;
        for (int k = 0; k <= 16; k++) {
            float an = -kHalfPi + kPi * k / 16.f;
            vec2 d = L * (cosf(an) * (float)s) + W * sinf(an);
            hd.push_back(cc + d * rIn);
        }
        if (s < 0) std::reverse(hd.begin(), hd.end());
        polyFlat(g, *g.m, hd, z + 0.05f, infield, M(MAT_GRASS));
    }
    if (detail) {
        // football markings (yard lines every 5 yards), end zones in the school colour, goal posts
        float fl = 50.f, fw = 24.4f;   // half length incl. end zones, half width
        for (int k = -10; k <= 10; k++) {
            float l = k * 4.572f;
            vec2 a = P(l, -fw), b = P(l, fw);
            if (g.owns((a + b) * 0.5f)) paintLine(g, a, b, 0.1f, z + 0.07f, kWhiteC, M(MAT_PAINT_WHITE));
        }
        for (int s = -1; s <= 1; s += 2) {
            vec2 ez = P(s * (fl - 4.57f), 0.f);
            if (g.owns(ez)) {
                paintRect(g, ez, L, 4.57f, fw, z + 0.068f, rgb(0.1f, 0.45f, 0.5f), M(MAT_PAINT_WHITE));
                plainLetters(g, "TARPONS", vec3(ez + W * (s * 7.f), z + 0.075f) - vec3(0, 0, 0), vec3(W * (float)-s, 0.f), vec3(L * (float)s, 0.f), 2.4f,
                             rgb(0.95f, 0.6f, 0.2f), M(MAT_PAINT_WHITE), 0.f, 0.14f);
                vec2 gp = P(s * fl, 0.f);
                cyl(g, vec3(gp, z), 0.1f, 0.1f, 3.05f, 6, rgb(1.f, 0.85f, 0.1f), M(MAT_METAL_PAINTED), false);
                beam(g, vec3(gp - W * 2.8f, z + 3.05f), vec3(gp + W * 2.8f, z + 3.05f), 0.1f, 0.1f, rgb(1.f, 0.85f, 0.1f), M(MAT_METAL_PAINTED));
                for (int q = -1; q <= 1; q += 2) cyl(g, vec3(gp + W * (q * 2.8f), z + 3.05f), 0.06f, 0.06f, 6.f, 6, rgb(1.f, 0.85f, 0.1f), M(MAT_METAL_PAINTED), false);
            }
        }
        for (int s = -1; s <= 1; s += 2) {
            vec2 a = P(-fl, s * fw), b = P(fl, s * fw);
            if (g.owns((a + b) * 0.5f)) paintLine(g, a, b, 0.12f, z + 0.07f, kWhiteC, M(MAT_PAINT_WHITE));
        }
    }
    // bleachers on one straight with a press box on top
    float side = e.p[0] >= 0.f ? 1.f : -1.f;
    vec2 face = W * -side;
    vec2 bc = P(0.f, side * (rOut + 3.f));
    if (g.owns(bc)) {
        const int rows = 10;
        bleachers(g, bc, L, face, z, 70.f, rows, 0.85f, 0.42f, rgb(0.15f, 0.5f, 0.55f), rgb(0.6f, 0.62f, 0.65f));
        // press box raised over the top rows on posts
        float topZ = z + 0.45f + (rows - 1) * 0.42f + 2.2f;
        vec2 pb = bc - face * ((rows - 1) * 0.85f - 1.6f);
        boxY(g, vec3(pb, topZ + 1.8f), L, vec3(14.f, 2.4f, 1.8f), rgbv(kStone), M(MAT_STUCCO), false);
        quad(g, *g.m, vec3(pb + face * 2.42f - L * 13.f, topZ + 0.9f), vec3(pb + face * 2.42f + L * 13.f, topZ + 0.9f), vec3(pb + face * 2.42f + L * 13.f, topZ + 2.9f),
             vec3(pb + face * 2.42f - L * 13.f, topZ + 2.9f), rgb(0.15f, 0.2f, 0.22f), M(MAT_GLASS), vec3(face, 0.f));
        landmark_mesh::hipRoof(g, pb, L, 14.6f, 3.f, topZ + 3.6f, 1.2f, rgbv(kTile), M(MAT_ROOF_TILE));
        for (int k = -1; k <= 1; k += 2) beam(g, vec3(pb + L * (k * 12.f), z), vec3(pb + L * (k * 12.f), topZ), 0.3f, 0.3f, rgb(0.6f), M(MAT_METAL_PAINTED));
        collide(g, vec3(pb, topZ + 1.8f), L, vec3(14.f, 2.4f, 1.8f));
        if (detail) {
            const char* t = "TARPON FIELD";
            plainLetters(g, t, vec3(pb + face * 2.45f, topZ + 3.f) - vec3(perp(face) * (textAdvance(t, 0.5f, 0.3f) * 0.5f), 0.f), vec3(perp(face), 0.f), vec3(0, 0, 1),
                         0.5f, rgb(0.95f, 0.6f, 0.2f), M(MAT_METAL_PAINTED), 0.03f, 0.14f);
        }
    }
    // scoreboard beyond the north bend
    vec2 sb = P(straight + rOut + 8.f, 0.f);
    if (g.owns(sb)) {
        for (int k = -1; k <= 1; k += 2) beam(g, vec3(sb + W * (k * 5.f), z), vec3(sb + W * (k * 5.f), z + 7.f), 0.4f, 0.4f, rgb(0.35f), M(MAT_METAL_PAINTED));
        boxY(g, vec3(sb, z + 9.f), W, vec3(7.f, 0.5f, 2.6f), rgb(0.08f, 0.1f, 0.12f), M(MAT_METAL_PAINTED), true);
        collide(g, vec3(sb, z + 5.f), W, vec3(7.f, 0.5f, 6.5f));
        vec2 fn = -L;
        vec2 rt = perp(fn);
        plainLetters(g, "HOME", vec3(sb + fn * 0.52f - rt * 5.8f, z + 10.2f), vec3(rt, 0.f), vec3(0, 0, 1), 0.8f, rgb(1.f, 0.8f, 0.3f, 0.3f), emMat(EA_NONE), 0.f);
        plainLetters(g, "GUEST", vec3(sb + fn * 0.52f + rt * 1.6f, z + 10.2f), vec3(rt, 0.f), vec3(0, 0, 1), 0.8f, rgb(1.f, 0.8f, 0.3f, 0.3f), emMat(EA_NONE), 0.f);
        plainLetters(g, "21", vec3(sb + fn * 0.52f - rt * 5.f, z + 7.6f), vec3(rt, 0.f), vec3(0, 0, 1), 1.8f, rgb(1.f, 0.25f, 0.1f, 0.4f), emMat(EA_NONE), 0.f, 0.18f);
        plainLetters(g, "14", vec3(sb + fn * 0.52f + rt * 2.4f, z + 7.6f), vec3(rt, 0.f), vec3(0, 0, 1), 1.8f, rgb(1.f, 0.25f, 0.1f, 0.4f), emMat(EA_NONE), 0.f, 0.18f);
        plainLetters(g, "TARPONS", vec3(sb + fn * 0.52f - rt * 3.9f, z + 11.9f), vec3(rt, 0.f), vec3(0, 0, 1), 0.7f, rgb(0.95f, 0.6f, 0.2f), M(MAT_METAL_PAINTED), 0.f);
    }
    // floodlight masts just outside the four bends and the perimeter fence
    for (int k = 0; k < 4; k++) {
        vec2 mp = P(((k & 1) ? 1.f : -1.f) * (straight + rOut * 0.78f), ((k & 2) ? 1.f : -1.f) * (rOut * 0.78f));
        if (!g.owns(mp)) continue;
        SiteElem m;
        m.c = mp;
        m.z = z;
        m.h = 26.f;
        airport_mesh::genFloodMast(m, g);
    }
    if (detail) {
        for (int k = 0; k < 48; k++) {
            float t0 = k / 48.f, t1 = (k + 1) / 48.f;
            vec2 a = ovalPt(t0, rOut + 1.2f), b = ovalPt(t1, rOut + 1.2f);
            if (!g.owns((a + b) * 0.5f)) continue;
            vec2 mid = (a + b) * 0.5f;
            if (dot(mid - bc, face) > -4.f && length(mid - bc) < 38.f) continue;   // open in front of the bleachers
            ironRailing(g, a, b, z, 1.1f, 2.5f, rgb(0.55f, 0.57f, 0.6f), false, true);
        }
    }
}

// ------------------------------------------------------------------------------------------------ layout
struct HallSpec {
    int kind, name, floors;
    vec2 c, front;
    float hw, hd, gh, fh, loggia;
};

void layout(SiteSet& S, WorldMap& map) {
    // ground: level the campus (the streets round it are built on the levelled ground)
    float zs = 0.f;
    int n = 0;
    for (float y = kY0 + 20.f; y < kY1 - 20.f; y += 20.f)
        for (float x = kX0 + 20.f; x < kX1 - 20.f; x += 20.f) {
            if (map.isWater(x, y) || map.regionAt(x, y) != REG_NORTH_CITY) {
                LOG("Places: campus ground at (%.0f, %.0f) is not North Porto Sol, campus left out", x, y);
                return;
            }
            zs += map.heightAt(x, y);
            n++;
        }
    const float zg = roundf(zs / Max(1, n) * 10.f) / 10.f;
    // (kept south of Bayshore Boulevard's far kerb so the river bank is not touched)
    map.flattenRect(vec2((kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f - 2.f), vec2(1, 0), (kX1 - kX0) * 0.5f + 4.f, (kY1 - kY0) * 0.5f + 2.f, zg, 10.f);
    const float z = zg + 0.4f;   // campus grounds at sidewalk level (streets: terrain + 0.25, sidewalks + 0.15)
    // no grid streets through the campus, no generic lots or scattered trees
    S.roadBlocks.push_back({vec2((kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f), vec2(1, 0), (kX1 - kX0) * 0.5f - 1.f, (kY1 - kY0) * 0.5f - 1.f});
    const float ux0 = kX0 + 13.4f + 4.5f + 0.5f, ux1 = kX1 - 8.8f - 3.5f - 0.5f;   // Sunrise Blvd / 27th Ave kerbs + sidewalks
    const float uy0 = kY0 + 5.6f + 3.f + 0.5f, uy1 = kY1 - 13.4f - 4.5f - 0.5f;    // N 34th St / Bayshore Blvd
    S.lotBlocks.push_back({vec2((ux0 + ux1) * 0.5f, (uy0 + uy1) * 0.5f), vec2(1, 0), (ux1 - ux0) * 0.5f + 3.f, (uy1 - uy0) * 0.5f + 3.f});
    S.vegBlocks.push_back({vec2((ux0 + ux1) * 0.5f, (uy0 + uy1) * 0.5f), vec2(1, 0), (ux1 - ux0) * 0.5f, (uy1 - uy0) * 0.5f});
    int placeIdx = (int)S.places.size();
    {
        NamedPlace np;
        np.name = "Porto Sol University";
        np.kind = PK_CAMPUS;
        np.pos = vec2((kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f);
        np.door = vec2(1905.f, uy0);
        np.radius = 260.f;
        S.places.push_back(np);
    }
    // ---------------------------------------------------------------- plan (x east, y north)
    const float qx0 = 1856.f, qx1 = 1944.f, qy0 = 3585.f, qy1 = 3765.f;   // the quad lawn
    const float axisX = (qx0 + qx1) * 0.5f;
    const vec2 N(0, 1), Sd(0, -1), E(1, 0), Wd(-1, 0);
    std::vector<HallSpec> halls = {
        {HK_LIBRARY, 0, 2, vec2(axisX, 3800.f), Sd, 30.f, 13.f, 6.f, 6.f, 0.f},
        {HK_LECTURE, 1, 3, vec2(1834.f, 3722.f), E, 27.f, 12.f, 4.6f, 4.f, 3.4f},
        {HK_LECTURE, 2, 3, vec2(1834.f, 3630.f), E, 27.f, 12.f, 4.6f, 4.f, 3.4f},
        {HK_LECTURE, 3, 3, vec2(1966.f, 3722.f), Wd, 27.f, 12.f, 4.6f, 4.f, 3.4f},
        {HK_LECTURE, 4, 3, vec2(1966.f, 3630.f), Wd, 27.f, 12.f, 4.6f, 4.f, 3.4f},
        {HK_UNION, 5, 2, vec2(1846.f, 3535.f), N, 22.f, 17.f, 5.2f, 4.4f, 0.f},
        {HK_ADMIN, 6, 3, vec2(1958.f, 3535.f), N, 20.f, 15.f, 4.6f, 4.f, 0.f},
        {HK_DORM, 7, 6, vec2(1858.f, 3888.f), Sd, 32.f, 9.f, 4.f, 3.1f, 0.f},
        {HK_DORM, 8, 6, vec2(1940.f, 3888.f), Sd, 32.f, 9.f, 4.f, 3.1f, 0.f},
        {HK_DORM, 9, 6, vec2(2032.f, 3888.f), Sd, 36.f, 9.f, 4.f, 3.1f, 0.f},
        {HK_FIELDHOUSE, 10, 2, vec2(2046.f, 3812.f), Sd, 22.f, 9.f, 4.4f, 4.f, 0.f},
    };
    for (const HallSpec& hs : halls) {
        SiteElem e;
        e.kind = SK_CAMPUS_HALL;
        e.variant = (u16)hs.kind;
        e.seed = hash32(0xCA4Bu + (u32)hs.name * 7919u);
        e.c = hs.c;
        e.ax = hs.front;
        e.hx = hs.hw;
        e.hy = hs.hd;
        e.z = z;
        e.p[0] = (float)hs.floors;
        e.p[1] = hs.gh;
        e.p[2] = hs.fh;
        e.p[3] = hs.loggia;
        e.p[4] = (float)hs.name;
        e.p[6] = -1.f;
        e.text = kHallNames[hs.name];
        e.h = hs.gh + (hs.floors - 1) * hs.fh + 8.f;
        int ei = (int)S.elems.size();
        S.elems.push_back(e);
        SiteBuildingReq q;
        q.c = hs.c;
        q.ax = perp(hs.front);
        q.hx = hs.hw;
        q.hy = hs.hd;
        q.style = hs.kind == HK_DORM ? BS_CONDO : BS_MIDRISE;
        q.roof = ROOF_HIP;
        q.floors = (u16)hs.floors;
        q.front = hs.front;
        q.baseZ = z - 0.15f;
        q.region = REG_NORTH_CITY;
        q.siteElem = ei;
        S.buildingReqs.push_back(q);
    }
    // bell tower beside the library, fountain at the crossing of the quad
    {
        SiteElem t;
        t.kind = SK_CAMPUS_TOWER;
        t.seed = 0xBE11u;
        t.c = vec2(1972.f, 3800.f);
        t.ax = Sd;
        t.hx = t.hy = 4.5f;
        t.z = z;
        t.h = 48.f;
        t.text = "BELL TOWER";
        S.elems.push_back(t);
        SiteElem f;
        f.kind = SK_FOUNTAIN;
        f.c = vec2(axisX, (qy0 + qy1) * 0.5f);
        f.ax = vec2(1, 0);
        f.hx = f.hy = 12.f;
        f.z = z + 0.1f;
        f.h = 6.f;
        f.seed = 0xF0F3u;
        f.p[0] = 9.f;
        f.variant = 1;
        S.elems.push_back(f);
    }
    // grounds: the quad, the gate plaza, the river lawn, the east walks with the parking lot
    auto grounds = [&](int variant, float x0, float y0, float x1, float y1) -> SiteElem& {
        SiteElem e;
        e.kind = SK_CAMPUS_GROUNDS;
        e.variant = (u16)variant;
        e.seed = hash32(0x6A0Du + (u32)variant);
        e.c = vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        e.ax = vec2(1, 0);
        e.hx = (x1 - x0) * 0.5f;
        e.hy = (y1 - y0) * 0.5f;
        e.z = z;
        e.h = 6.f;
        S.elems.push_back(e);
        return S.elems.back();
    };
    {
        // the lawn under everything and the walks between the halls (drawn first; the other grounds lie on top)
        SiteElem& base = grounds(4, ux0, uy0, ux1, uy1);
        base.p[0] = 3.f;
        base.pts = {vec2(ux0 + 4.f, 3850.f), vec2(2000.f, 3850.f),                         // behind the library: dorm doors to the field house
                    vec2(1858.f, 3850.f), vec2(1858.f, 3879.f), vec2(1940.f, 3850.f), vec2(1940.f, 3879.f), vec2(2032.f, 3850.f), vec2(2032.f, 3879.f),
                    vec2(2032.f, 3850.f), vec2(2046.f, 3803.f),                            // to the field house
                    vec2(1846.f, 3552.f), vec2(1846.f, 3575.f), vec2(1846.f, 3575.f), vec2(axisX - 14.f, 3575.f),   // union door to the avenue
                    vec2(1958.f, 3550.f), vec2(1958.f, 3575.f), vec2(1958.f, 3575.f), vec2(axisX + 14.f, 3575.f),   // admin door to the avenue
                    vec2(qx1 + 1.f, 3675.f), vec2(1994.f, 3675.f),                         // quad cross walk on to the stand
                    vec2(1985.f, 3560.f), vec2(1985.f, 3610.f)};                           // parking to the field
    }
    grounds(0, qx0, qy0, qx1, qy1);
    SiteElem& gate = grounds(1, ux0, uy0, 1990.f, 3514.f);
    gate.p[0] = axisX;
    gate.p[1] = qy0;
    // the avenue from the gate to the quad (the plaza pavers run on to the quad's south walk)
    gate.hy = (qy0 - uy0) * 0.5f;
    gate.c.y = (uy0 + qy0) * 0.5f;
    grounds(2, ux0, 3899.f, ux1, uy1);
    SiteElem& east = grounds(3, 1992.f, uy0, ux1, 3570.f);
    east.p[0] = 2000.f;
    east.p[1] = ux1 - 2.f;
    east.p[2] = uy0 + 30.f;
    east.p[3] = uy0 + 30.f + 36.f;
    east.p[4] = 3585.f;
    // Tarpon Field: long axis north-south in the east strip, bleachers on the west straight
    {
        SiteElem fe;
        fe.kind = SK_CAMPUS_FIELD;
        fe.seed = 0x7A2Fu;
        fe.c = vec2(2041.f, 3680.f);
        fe.ax = N;
        fe.hx = 42.2f + 44.2f + 10.f;
        fe.hy = 44.2f + 14.f;
        fe.z = z;
        fe.h = 30.f;
        fe.p[0] = 1.f;   // bleachers on the -x side: W = perp(N) = (-1, 0), side +1 -> west
        S.elems.push_back(fe);
        // (the track's grass infield and surface are walkable ground at the campus level)
    }
    // ---------------------------------------------------------------- people: students and staff
    u16 group = 1000;
    Rng rng(0x57D3u);
    // lawn sitters in small circles on the quad
    for (int k = 0; k < 10; k++) {
        vec2 c(rng.range(qx0 + 10.f, qx1 - 10.f), rng.range(qy0 + 10.f, qy1 - 10.f));
        if (length(c - vec2(axisX, (qy0 + qy1) * 0.5f)) < 24.f || fabsf(c.x - axisX) < 6.f) continue;
        int m = rng.irange(2, 4);
        for (int s = 0; s < m; s++) {
            float an = kTwoPi * s / m + rng.f();
            vec2 d(cosf(an), sinf(an));
            S.anchors.push_back({vec3(c + d * 1.1f, z), -d, PA_SIT_GROUND, (u8)placeIdx, group});
        }
        group++;
    }
    // standing groups by the library steps and the lecture hall loggias
    for (int k = -2; k <= 2; k++) {
        vec2 c(axisX + k * 6.f, 3800.f - 13.f - 12.f);
        for (int s = 0; s < 3; s++) {
            float an = kTwoPi * s / 3.f + k;
            vec2 d(cosf(an), sinf(an));
            S.anchors.push_back({vec3(c + d * 0.8f, z), -d, PA_STAND, (u8)placeIdx, group});
        }
        group++;
    }
    // walkers: the axis and the diagonals
    for (float y = qy0 - 30.f; y <= 3775.f; y += 15.f) S.anchors.push_back({vec3(axisX, y, z + 0.1f), N, PA_WAYPOINT, (u8)placeIdx, 1});
    // runners on the track (lane 2) and spectators in the bleachers
    const float trX = 2041.f, trY = 3680.f, lane2 = 36.8f + 1.83f, rOutT = 36.8f + 6.f * 1.22f;
    for (int k = 0; k < 12; k++) {
        float t = k / 12.f;
        float perim = 4.f * 42.2f + kTwoPi * lane2, s = t * perim;
        vec2 p, d;
        if (s < 84.4f) { p = vec2(trX + lane2, trY - 42.2f + s); d = N; }
        else if (s < 84.4f + kPi * lane2) {
            float an = (s - 84.4f) / lane2;
            p = vec2(trX, trY + 42.2f) + vec2(cosf(an), sinf(an)) * lane2;
            d = vec2(-sinf(an), cosf(an));
        } else if (s < 2.f * 84.4f + kPi * lane2) { p = vec2(trX - lane2, trY + 42.2f - (s - 84.4f - kPi * lane2)); d = Sd; }
        else {
            float an = kPi + (s - 2.f * 84.4f - kPi * lane2) / lane2;
            p = vec2(trX, trY - 42.2f) + vec2(cosf(an), sinf(an)) * lane2;
            d = vec2(-sinf(an), cosf(an));
        }
        S.anchors.push_back({vec3(p, z + 0.06f), d, PA_EXERCISE, (u8)placeIdx, 2});
    }
    for (int k = 0; k < 16; k++) {
        float l = -30.f + k * 4.f;
        int row = k % 6;
        vec2 p = vec2(trX - (rOutT + 3.f) - row * 0.85f, trY + l);
        S.anchors.push_back({vec3(p, z + row * 0.42f), E, PA_SIT, (u8)placeIdx, group++});
    }
    LOG("Places: Porto Sol University at (%.0f, %.0f), ground %.1f, %zu halls", (kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f, zg, halls.size());
}

void facades(SiteSet& S, BuildingSet& bs) {
    for (size_t bi = 0; bi < bs.buildings.size(); bi++) {
        Building& b = bs.buildings[bi];
        if (b.siteElem < 0 || b.siteElem >= (int)S.elems.size()) continue;
        SiteElem& e = S.elems[b.siteElem];
        if (e.kind != SK_CAMPUS_HALL) continue;
        Hall h = hallOf(e);
        FacadeGPU& f = bs.facades[b.facade];
        vec3 wall = kStone;
        MaterialId wm = MAT_STONE;
        f.floorH = h.fh;
        f.groundH = h.gh;
        f.style = 0.f;
        f.sillH = 1.f;
        f.winW = 0.46f;
        f.winH = 0.58f;
        f.bayW = 3.6f;
        f.flags = 16u;
        f.litFrac = 0.6f;
        switch (h.kind) {
            case HK_LIBRARY: f.bayW = 4.6f; f.winW = 0.42f; f.winH = 0.66f; f.sillH = 1.3f; wall = kStoneLight; break;
            case HK_LECTURE: f.bayW = 3.8f; f.winH = 0.6f; break;
            case HK_UNION: f.flags = 1u | 16u; wall = vec3(0.98f, 0.9f, 0.78f); wm = MAT_STUCCO; f.bayW = 3.4f; break;
            case HK_ADMIN: f.bayW = 3.4f; break;
            case HK_DORM: f.flags = 8u; f.bayW = 3.3f; f.winW = 0.42f; f.winH = 0.52f; f.sillH = 0.95f; wall = vec3(0.97f, 0.88f, 0.74f); wm = MAT_STUCCO;
                f.litFrac = 0.75f; break;
            default: f.flags = 0u; wm = MAT_STUCCO; wall = vec3(0.96f, 0.9f, 0.8f); break;
        }
        // whole bays on the long walls
        f.wallColor = packRGBA8(wall.x, wall.y, wall.z, 1.f);
        f.frameColor = packRGBA8(kBronze.x, kBronze.y, kBronze.z, 1.f);
        f.glassColor = packRGBA8(0.45f, 0.55f, 0.6f, 1.f);
        f.wallLayer = (float)wm;
        f.seed = e.seed;
        f.roomDepth = 6.f;
        b.height = h.H;
        b.baseZ = h.z;
        e.p[6] = (float)bi;
        e.p[7] = (float)b.facade;
    }
}

}  // namespace campus
}  // namespace World
