// Hospitals at the hospital blips. The two city hospitals (Tidewater General on Flamingo Boulevard, whose lobby is the
// story's enterable hospital, and Midtown General) fill a superblock frontage: a three-storey podium with the main
// entrance canopy, a ward tower with the name and a lit H on its crown and a helipad on its roof (deck, touchdown circle,
// edge lights, windsock), the emergency wing with its drive-through ambulance canopy (EMERGENCY in red, bays, parked
// ambulances), a car park and landscaping. The community hospitals (Westbrook, Harlow County, Okahatchee, Fort Castell)
// and the clinics (Sol Beach, Redland) are smaller versions with a ground helipad and windsocks. All are fitted to the
// finished streets near their blips (placesAfterLots); the blips of the towns' hospitals were in the sea, the swamp or
// the fields, so those stand in their towns.
#include "places.h"
#include "interiors.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace hospital {

using namespace sitegeo;
using namespace place_kit;

enum HospKind : u8 { HO_CITY = 0, HO_COMMUNITY, HO_CLINIC };

struct Spec {
    const char* name;   // place name (map label)
    const char* sign;   // lettering on the building
    vec2 want;          // the blip, or the middle of the town the blip belongs to
    Region reg;         // region the plot must lie in (REG_COUNT: any)
    u8 kind;
    bool host;          // front on the street the story's hospital lobby resolves to (interiors.cpp, IK_HOSPITAL)
};
const Spec kSpecs[] = {
    {"Tidewater General Hospital", "TIDEWATER GENERAL", vec2(1650.f, 1050.f), REG_COUNT, HO_CITY, true},
    {"Midtown General", "MIDTOWN GENERAL", vec2(3650.f, 3200.f), REG_COUNT, HO_CITY, false},
    {"Westbrook Hospital", "WESTBROOK HOSPITAL", vec2(-2400.f, 1800.f), REG_SUBURBS, HO_COMMUNITY, false},
    {"Sol Beach Clinic", "SOL BEACH CLINIC", vec2(5200.f, -900.f), REG_BEACH, HO_CLINIC, false},
    {"Harlow County Hospital", "HARLOW COUNTY HOSPITAL", vec2(-5000.f, 4860.f), REG_HARLOW, HO_COMMUNITY, false},
    {"Okahatchee Medical", "OKAHATCHEE MEDICAL", vec2(300.f, 6650.f), REG_LAKE_TOWN, HO_COMMUNITY, false},
    {"Redland Health", "REDLAND HEALTH", vec2(-220.f, -5300.f), REG_REDLAND, HO_CLINIC, false},
    {"Fort Castell Hospital", "FORT CASTELL HOSPITAL", vec2(4550.f, 7950.f), REG_FORT_CASTELL, HO_COMMUNITY, false},
};
constexpr int kSpecCount = (int)(sizeof(kSpecs) / sizeof(kSpecs[0]));

// Plot layout in plot coordinates: u along the street (0 = plot centre), v from the front lot line into the plot
struct Rect {
    float u0, u1, v0, v1;
    float cu() const { return (u0 + u1) * 0.5f; }
    float cv() const { return (v0 + v1) * 0.5f; }
    float hu() const { return (u1 - u0) * 0.5f; }
    float hv() const { return (v1 - v0) * 0.5f; }
};
struct Layout {
    float W, D;
    Rect main, tower, er, erCanopy, entry, park;
    float mainFloors, groundH, floorH, towerFloors;
    float driveIn, driveOut;   // u of the ER drive's in and out lanes (from the street to the canopy)
    float parkLane;            // u of the car park's entrance lane
    bool towerHeli, groundHeli;
    vec2 heli;                 // ground helipad (u, v)
    float heliR;
};

Layout layoutOf(int kind, float W, float D) {
    Layout L;
    L.W = W;
    L.D = D;
    if (kind == HO_CITY) {
        L.main = {-40.f, 30.f, 5.f, 35.f};
        L.tower = {-26.f, 14.f, 13.f, 33.f};
        L.er = {40.f, 68.f, 25.f, 50.f};
        L.erCanopy = {38.f, 70.f, 10.f, 25.f};
        L.entry = {-14.f, 4.f, 0.5f, 5.f};
        L.park = {-W * 0.5f + 1.f, -42.f, 2.f, D - 2.f};
        L.mainFloors = 3.f;
        L.groundH = 5.f;
        L.floorH = 4.2f;
        L.towerFloors = 9.f;
        L.driveIn = 42.5f;
        L.driveOut = 65.5f;
        L.parkLane = -50.f;
        L.towerHeli = true;
        L.groundHeli = false;
    } else if (kind == HO_COMMUNITY) {
        L.main = {-36.f, 5.f, 5.f, 27.f};
        L.tower = {0.f, 0.f, 0.f, 0.f};
        L.er = {13.f, 36.f, 21.f, 38.f};
        L.erCanopy = {12.f, 37.f, 8.f, 21.f};
        L.entry = {-22.f, -8.f, 0.5f, 5.f};
        L.park = {-36.f, 11.3f, 32.f, D - 2.f};
        L.mainFloors = 3.f;
        L.groundH = 4.6f;
        L.floorH = 3.8f;
        L.towerFloors = 0.f;
        L.driveIn = 15.f;
        L.driveOut = 34.f;
        L.parkLane = 8.5f;
        L.towerHeli = false;
        L.groundHeli = true;
        L.heli = vec2(24.5f, D - 10.f);
        L.heliR = 8.f;
    } else {
        L.main = {-26.f, 4.f, 5.f, 22.f};
        L.tower = {0.f, 0.f, 0.f, 0.f};
        L.er = {10.f, 25.f, 18.f, 30.f};
        L.erCanopy = {9.f, 26.f, 7.f, 18.f};
        L.entry = {-17.f, -5.f, 0.5f, 5.f};
        L.park = {-26.f, 9.8f, 27.f, D - 2.f};
        L.mainFloors = 2.f;
        L.groundH = 4.4f;
        L.floorH = 3.6f;
        L.towerFloors = 0.f;
        L.driveIn = 11.5f;
        L.driveOut = 23.5f;
        L.parkLane = 7.f;
        L.towerHeli = false;
        L.groundHeli = false;
        L.heliR = 0.f;
    }
    return L;
}
inline float heightOf(const Layout& L) { return L.groundH + (L.mainFloors - 1.f) * L.floorH; }

// Plot frame from a hospital element: c: plot centre, ax: along the street, p[0]: side sign (out = perp(ax) * p[0]),
// hx / hy: half width / depth, p[1]: kind, z: ground
struct Frame {
    vec2 c0, along, out;
    float z;
    vec2 P(float u, float v) const { return c0 + along * u + out * v; }
};
Frame frameOf(const SiteElem& e) {
    Frame F;
    F.along = e.ax;
    F.out = perp(e.ax) * e.p[0];
    F.c0 = e.c - F.out * e.hy;
    F.z = e.z;
    return F;
}

const vec3 kWall(0.93f, 0.94f, 0.93f), kBand(0.36f, 0.62f, 0.74f), kRed(0.9f, 0.06f, 0.08f);

// Simple parked ambulance (box van: cab, box body with the stripe and the light bar); fwd: nose direction
void ambulance(G& g, vec2 c, vec2 fwd, float z) {
    vec2 R(fwd.y, -fwd.x);
    const u32 white = rgb(0.95f, 0.95f, 0.94f), red = rgb(0.85f, 0.08f, 0.08f), dark = rgb(0.06f, 0.07f, 0.08f);
    boxY(g, V3(c - fwd * 0.8f, z + 1.55f), R, vec3(1.1f, 1.95f, 1.2f), white, M(MAT_CARPAINT), false);   // box body
    boxY(g, V3(c + fwd * 1.85f, z + 1.1f), R, vec3(1.f, 0.75f, 0.75f), white, M(MAT_CARPAINT), false);   // cab
    boxY(g, V3(c + fwd * 2.05f, z + 1.55f), R, vec3(0.95f, 0.45f, 0.3f), dark, M(MAT_CAR_GLASS), false);  // windscreen
    for (int s = -1; s <= 1; s += 2) {
        vec2 sp = c - fwd * 0.8f + R * (s * 1.105f);
        quad(g, *g.m, V3(sp - fwd * 1.9f, z + 1.2f), V3(sp + fwd * 1.9f, z + 1.2f), V3(sp + fwd * 1.9f, z + 1.45f), V3(sp - fwd * 1.9f, z + 1.45f), red, M(MAT_CARPAINT),
             V3(R * (float)s, 0.f));
        for (int w = 0; w < 2; w++) {
            vec2 wp = c + fwd * (w ? 1.8f : -1.9f) + R * (s * 0.95f);
            boxY(g, V3(wp, z + 0.38f), fwd, vec3(0.38f, 0.14f, 0.38f), dark, M(MAT_TIRE));
        }
    }
    boxY(g, V3(c + fwd * 1.3f, z + 2.82f), R, vec3(0.8f, 0.14f, 0.07f), rgb(1.f, 0.2f, 0.15f, 0.3f), emMat(EA_BLINK, 3), true);   // light bar
    collide(g, V3(c, z + 1.4f), R, vec3(1.1f, 2.9f, 1.4f));
}

// The helipad marking: a white touchdown circle with an H, a yellow edge, lights round it
void helipadMarks(G& g, vec2 c, vec2 ax, float z, float r, bool detail) {
    vec2 ay = perp(ax);
    ring(g, V3(c, z), vec3(0, 0, 1), r * 0.62f, 0.35f, 0.02f, detail ? 32 : 12, rgb(0.95f), M(MAT_PAINT_WHITE));
    ring(g, V3(c, z), vec3(0, 0, 1), r - 0.3f, 0.3f, 0.02f, detail ? 40 : 16, rgb(0.95f, 0.8f, 0.1f), M(MAT_PAINT_YELLOW));
    const float hh = r * 0.36f, hw = r * 0.24f, t = 0.45f;
    auto bar = [&](vec2 a, vec2 b, float w) {
        vec2 d = normalize(b - a), n = perp(d) * (w * 0.5f);
        quad(g, *g.m, V3(a - n, z + 0.025f), V3(b - n, z + 0.025f), V3(b + n, z + 0.025f), V3(a + n, z + 0.025f), rgb(0.95f), M(MAT_PAINT_WHITE), vec3(0, 0, 1));
    };
    bar(c - ax * hw - ay * hh, c - ax * hw + ay * hh, t);
    bar(c + ax * hw - ay * hh, c + ax * hw + ay * hh, t);
    bar(c - ax * hw, c + ax * hw, t);
    int nl = detail ? 12 : 6;
    for (int k = 0; k < nl; k++) {
        float an = kTwoPi * k / nl;
        vec2 p = c + (ax * cosf(an) + ay * sinf(an)) * (r + 0.25f);
        boxY(g, V3(p, z + 0.08f), ax, vec3(0.1f, 0.1f, 0.08f), rgb(0.2f, 1.f, 0.3f, 0.6f), emMat(EA_NIGHT), true);
    }
}

// Windsock on a mast (orange and white), standing at p on z
void windsock(G& g, vec2 p, float z, float h) {
    cyl(g, V3(p, z), 0.07f, 0.05f, h, 6, rgb(0.92f), M(MAT_METAL_PAINTED), false);
    boxY(g, V3(p, z + h + 0.05f), vec2(1, 0), vec3(0.06f, 0.06f, 0.06f), rgb(1.f, 0.15f, 0.1f, 0.5f), emMat(EA_SLOWBLINK), true);
    vec2 wind = normalize(vec2(-0.92f, -0.3f));   // the prevailing easterly: the sock streams west
    vec3 mouth = V3(p, z + h - 0.25f);
    for (int k = 0; k < 4; k++) {
        float t0 = k / 4.f, t1 = (k + 1) / 4.f;
        vec3 c0 = mouth + V3(wind * (t0 * 2.4f), -t0 * t0 * 0.4f), c1 = mouth + V3(wind * (t1 * 2.4f), -t1 * t1 * 0.4f);
        float r0 = Lerp(0.3f, 0.14f, t0), r1 = Lerp(0.3f, 0.14f, t1);
        u32 c = (k & 1) ? rgb(0.95f) : rgb(1.f, 0.4f, 0.05f);
        vec3 ax3 = normalize(c1 - c0);
        vec3 n1 = normalize(anyPerp(ax3)), n2 = cross(ax3, n1);
        MeshData& m = *g.m;
        u32 b = (u32)m.verts.size();
        for (int s = 0; s <= 6; s++) {
            float a = kTwoPi * s / 6;
            vec3 dd = n1 * cosf(a) + n2 * sinf(a);
            m.addVertex(c0 + dd * r0 - g.org, dd, ax3, vec2(0, 0), c, M(MAT_FABRIC));
            m.addVertex(c1 + dd * r1 - g.org, dd, ax3, vec2(0, 0), c, M(MAT_FABRIC));
        }
        for (int s = 0; s < 6; s++) {
            u32 i0 = b + s * 2;
            m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
            m.quadIdx(i0, i0 + 1, i0 + 3, i0 + 2);
        }
    }
    collide(g, V3(p, z + h * 0.5f), vec2(1, 0), vec3(0.08f, 0.08f, h * 0.5f));
}

// ------------------------------------------------------------------------------------------------ buildings (SK_HOSPITAL)
// variant 0: the main block (podium; the city hospitals' lobby host), 1: the ward tower (roof helipad), 2: the emergency
// wing. c / hx / hy: footprint (hx along the street), ax: front (toward the street), z: ground; p[0]: floors, p[1]:
// ground floor height, p[2]: floor height, p[3]: facade base (tower: the podium roof), p[4]: kind, p[5]: helipad on the
// roof; p[6]: building, p[7]: facade (set by facades()); text: the hospital's lettering.
void genBlock(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    const bool detail = g.detail;
    const vec2 f = e.ax, R(f.y, -f.x);
    const float hw = e.hx, hd = e.hy, z = e.z, zb = e.p[3];
    const float top = zb + e.p[1] + (e.p[0] - 1.f) * e.p[2];
    const u32 fac = (u32)e.p[7];
    const float bay = gBuildings && e.p[6] >= 0.f ? gBuildings->facades[fac].bayW : 3.2f;
    auto P = [&](float u, float v) { return e.c + R * u + f * v; };
    std::vector<vec2> fp = {P(-hw, -hd), P(hw, -hd), P(hw, hd), P(-hw, hd)};
    if (dot(cross(vec3(fp[1] - fp[0], 0.f), vec3(fp[2] - fp[1], 0.f)), vec3(0, 0, 1)) < 0.f) std::reverse(fp.begin(), fp.end());
    const int host = (e.variant == 0 && gBuildings && e.p[6] >= 0.f) ? gBuildings->buildings[(int)e.p[6]].interior : -1;
    const float z0 = e.variant == 1 ? zb : z - 0.8f;
    if (detail && host >= 0) interiorFacadeRing(host, *g.m, g.org, fp, z0, top, zb, fac, bay, kWhiteC);
    else facadeRing(g, fp, z0, top, zb, fac, bay);
    if (!(host >= 0 && g.col && interiorShellCollision(host, e.c, R, hw, hd, z, top, *g.col))) collide(g, V3(e.c, (z0 + top) * 0.5f), R, vec3(hw, hd, (top - z0) * 0.5f));
    // roof: gravel, parapet, plant
    polyFlat(g, *g.m, fp, top, rgb(0.8f), M(MAT_ROOF_GRAVEL));
    const u32 trim = rgbv(kWall), band = rgbv(kBand);
    for (int k = 0; k < 4; k++) {
        vec2 a = fp[k], b = fp[(k + 1) & 3];
        vec2 d = normalize(b - a), on = perp(d);
        if (dot(on, (a + b) * 0.5f - e.c) < 0.f) on = -on;
        boxY(g, V3((a + b) * 0.5f + on * 0.15f, top + 0.55f), d, vec3(length(b - a) * 0.5f + 0.3f, 0.3f, 0.55f), trim, M(MAT_CONCRETE_PANEL));
        if (detail) boxY(g, V3((a + b) * 0.5f + on * 0.32f, top + 0.3f), d, vec3(length(b - a) * 0.5f + 0.3f, 0.02f, 0.18f), band, M(MAT_METAL_PAINTED));
    }
    Rng r(e.seed);
    if (detail && e.p[5] < 0.5f)
        for (int k = 0; k < 3; k++) {   // rooftop air handlers
            vec2 u = P(r.range(-hw + 3.f, hw - 3.f), r.range(-hd + 3.f, hd - 3.f));
            boxY(g, V3(u, top + 0.9f), R, vec3(1.4f, 1.f, 0.9f), rgb(0.72f), M(MAT_METAL_PAINTED));
        }
    // lettering
    const char* nm = e.text.c_str();
    const vec3 readRight = V3(-R, 0.f);   // left to right for someone facing the front
    if (e.variant == 1) {
        // the tower crown: the name on the front, a lit H sign on each side
        float th = Min(1.6f, (hw * 2.f - 4.f) / Max(1.f, textAdvance(nm, 1.f, 0.3f)));
        plainLetters(g, nm, centredOrigin(nm, V3(P(0.f, hd + 0.08f), top - th - 1.2f), readRight, th), readRight, vec3(0, 0, 1), th, rgb(0.95f, 0.97f, 1.f, 0.6f),
                     emMat(EA_NIGHT), 0.06f, 0.13f);
        for (int s = -1; s <= 1; s += 2) {
            vec2 sc = P(s * (hw + 0.1f), hd - 5.f);
            vec2 n = R * (float)s, t(n.y, -n.x);
            boxY(g, V3(sc, top - 3.2f), n, vec3(0.1f, 1.9f, 1.9f), rgb(0.08f, 0.35f, 0.8f), M(MAT_METAL_PAINTED));
            vec2 hc = sc + n * 0.11f;
            for (int q = 0; q < 3; q++) {
                vec3 a = q < 2 ? V3(hc + t * (q ? 0.8f : -0.8f), top - 4.4f) : V3(hc - t * 0.8f, top - 3.2f);
                vec3 b = q < 2 ? V3(hc + t * (q ? 0.8f : -0.8f), top - 2.f) : V3(hc + t * 0.8f, top - 3.2f);
                beam(g, a, b, 0.4f, 0.05f, rgb(1.f, 1.f, 1.f, 0.7f), emMat(EA_NIGHT), V3(n, 0.f));
            }
        }
        // roof helipad: steel deck on legs, touchdown circle, lights, windsock, stairs
        if (e.p[5] > 0.5f) {
            float dz = top + 1.6f, dh = Min(hw, hd) - 1.f;
            boxY(g, V3(e.c, dz - 0.15f), R, vec3(dh, dh, 0.15f), rgb(0.35f, 0.37f, 0.4f), M(MAT_CONCRETE));
            for (int k = 0; k < 4; k++) {
                vec2 lp = e.c + R * ((k & 1) ? dh - 1.f : 1.f - dh) + f * ((k & 2) ? dh - 1.f : 1.f - dh);
                boxY(g, V3(lp, top + 0.72f), R, vec3(0.2f, 0.2f, 0.72f), rgb(0.5f), M(MAT_METAL_PAINTED));
            }
            helipadMarks(g, e.c, R, dz + 0.01f, dh - 0.6f, detail);
            if (detail)
                for (int k = 0; k < 4; k++) {   // safety net frames round the deck
                    vec2 a = e.c + rotate(R, kHalfPi * k) * dh + rotate(R, kHalfPi * (k + 1)) * dh, b = e.c + rotate(R, kHalfPi * (k + 1)) * dh + rotate(R, kHalfPi * (k + 2)) * dh;
                    vec2 o = normalize((a + b) * 0.5f - e.c);
                    beam(g, V3(a + o * 0.9f, dz - 0.35f), V3(b + o * 0.9f, dz - 0.35f), 0.05f, 0.05f, rgb(0.9f, 0.8f, 0.1f), M(MAT_METAL_PAINTED));
                }
            collide(g, V3(e.c, dz - 0.15f), R, vec3(dh, dh, 0.15f));
            windsock(g, P(hw - 1.2f, -hd + 1.2f), top + 1.1f, 4.5f);
            light(g, V3(e.c, dz + 6.f), vec3(0.9f, 0.95f, 1.f) * 6000.f, 22.f, 1, vec3(0, 0, -1), 0.2f);
        }
    } else if (e.variant == 0) {
        // podium: the name over the main entrance
        float th = 0.9f;
        plainLetters(g, nm, centredOrigin(nm, V3(P(0.f, hd + 0.06f), top - 1.5f), readRight, th), readRight, vec3(0, 0, 1), th,
                     rgb(0.2f, 0.45f, 0.62f), M(MAT_METAL_PAINTED), 0.05f, 0.14f);
    } else {
        // emergency wing: EMERGENCY along its front, lit red at night
        const char* em = "EMERGENCY";
        plainLetters(g, em, centredOrigin(em, V3(P(0.f, hd + 0.08f), top - 1.4f), readRight, 0.9f), readRight, vec3(0, 0, 1), 0.9f, rgb(1.f, 0.12f, 0.1f, 0.8f),
                     emMat(EA_NIGHT), 0.06f, 0.15f);
    }
}

// ------------------------------------------------------------------------------------------------ grounds (SK_HOSPITAL_GROUNDS)
// Entrance plaza and canopy, the ER drive with its ambulance canopy and bays, the car park, lawns and palms, signs, the
// ground helipad of the smaller hospitals. c / ax / hx / hy / p[0]: the plot frame, p[1]: kind, z: ground; text: name.
void genGrounds(const SiteElem& e, G& g) {
    const bool detail = g.detail;
    const Frame F = frameOf(e);
    const int kind = (int)e.p[1];
    const Layout L = layoutOf(kind, e.hx * 2.f, e.hy * 2.f);
    const float z = F.z, h = heightOf(L);
    const vec2 f = -F.out, R = F.along;   // the buildings face the street (f); R along it
    const vec2 readR(F.out.y, -F.out.x);   // left to right for someone on the sidewalk facing the hospital
    const vec3 readRight = V3(readR, 0.f);
    Rng r(e.seed ^ (u32)(g.cx * 73856093) ^ (u32)(g.cy * 19349663));
    auto rect = [&](const Rect& q, float lift, u32 col, u32 mat, bool pav) {   // flat surface in <= 20 m tiles
        int nu = Max(1, (int)ceilf((q.u1 - q.u0) / 20.f)), nv = Max(1, (int)ceilf((q.v1 - q.v0) / 20.f));
        for (int j = 0; j < nv; j++)
            for (int i = 0; i < nu; i++) {
                float u0 = Lerp(q.u0, q.u1, (float)i / nu), u1 = Lerp(q.u0, q.u1, (float)(i + 1) / nu);
                float v0 = Lerp(q.v0, q.v1, (float)j / nv), v1 = Lerp(q.v0, q.v1, (float)(j + 1) / nv);
                vec2 m = F.P((u0 + u1) * 0.5f, (v0 + v1) * 0.5f);
                if (!g.owns(m)) continue;
                std::vector<vec2> poly = {F.P(u0, v0), F.P(u1, v0), F.P(u1, v1), F.P(u0, v1)};
                if (F.along.x * F.out.y - F.along.y * F.out.x < 0.f) std::reverse(poly.begin(), poly.end());
                size_t v0i = g.d->verts.size();
                polyFlat(g, *g.d, poly, z + lift, col, mat);
                if (pav) paverUV(g, *g.d, v0i);
            }
    };
    const u32 asphalt = rgb(0.9f), conc = rgb(0.95f, 0.95f, 0.93f), white = rgb(0.95f), yellow = rgb(0.95f, 0.78f, 0.1f);
    // ---- the ER drive: in and out lanes from the street, the apron under the canopy
    const Rect apron = {L.erCanopy.u0, L.erCanopy.u1, L.erCanopy.v0, L.erCanopy.v1};
    rect({L.driveIn - 3.f, L.driveIn + 3.f, -0.6f, L.erCanopy.v0 + 0.1f}, 0.03f, asphalt, M(MAT_ASPHALT), false);
    rect({L.driveOut - 3.f, L.driveOut + 3.f, -0.6f, L.erCanopy.v0 + 0.1f}, 0.03f, asphalt, M(MAT_ASPHALT), false);
    rect(apron, 0.035f, conc, M(MAT_CONCRETE), false);
    // car park and its entrance lane
    rect(L.park, 0.03f, asphalt, M(MAT_ASPHALT_OLD), false);
    if (kind != HO_CITY) rect({L.parkLane - 2.8f, L.parkLane + 2.8f, -0.6f, L.park.v0 + 0.1f}, 0.03f, asphalt, M(MAT_ASPHALT), false);
    else rect({L.parkLane - 3.f, L.parkLane + 3.f, -0.6f, L.park.v0 + 0.1f}, 0.031f, asphalt, M(MAT_ASPHALT), false);
    // entrance plaza (pavers) between the sidewalk and the main doors
    rect({L.entry.u0 - 6.f, L.entry.u1 + 6.f, -0.6f, L.main.v0}, 0.04f, paverTint(), M(MAT_PAVERS), true);
    // walk from the sidewalk to the ER doors beside the drive
    rect({L.erCanopy.u0 - 2.4f, L.erCanopy.u0 - 0.4f, -0.6f, L.er.v0}, 0.04f, conc, M(MAT_SIDEWALK), false);
    // ---- markings: bays under the canopy, arrows on the lanes, stalls in the car park
    if (detail) {
        int bays = kind == HO_CITY ? 3 : 2;
        for (int k = 0; k <= bays; k++) {
            float u = Lerp(apron.u0 + 3.f, apron.u1 - 3.f, (float)k / bays);
            vec2 a = F.P(u, apron.v1 - 0.3f), b = F.P(u, apron.v1 - 7.f);
            if (g.owns((a + b) * 0.5f)) paintLine(g, a, b, 0.15f, z + 0.045f, yellow, M(MAT_PAINT_YELLOW));
        }
        vec2 tc = F.P(apron.cu(), apron.v0 + 2.f);
        if (g.owns(tc)) {
            const char* t = "AMBULANCE ONLY";
            float th = 0.8f;
            // painted on the ground, reading for a driver coming in from the street
            strokeText(g, *g.d, t, V3(tc - readR * (textAdvance(t, th, 0.3f) * 0.5f), z + 0.05f), readRight, V3(F.out, 0.f), th, th * 0.13f, yellow,
                       M(MAT_PAINT_YELLOW), 0.f, 0.3f);
        }
    }
    // car park: pairs of stall rows round 6 m aisles; the city lots' aisles run in from the street (the first one is the
    // entrance lane), the smaller lots' run across behind the building, reached by the lane beside it
    if (kind == HO_CITY) {
        for (float u = L.parkLane; u - 8.f >= L.park.u0 - 0.1f; u -= 16.f) {
            float len = L.park.v1 - L.park.v0 - 7.f;
            int stalls = Max(1, (int)(len / 2.6f));
            vec2 c = F.P(u, L.park.v0 + 7.f + len * 0.5f);
            stallRow(g, c + R * 3.f, F.out, R, z + 0.04f, stalls, 5.f, 0.6f, hash32(e.seed + (u32)(u * 3.f)));
            stallRow(g, c - R * 3.f, F.out, -R, z + 0.04f, stalls, 5.f, 0.6f, hash32(e.seed + (u32)(u * 3.f) + 7u));
        }
    } else {
        float len = (L.parkLane - 3.f) - L.park.u0 - 1.f;
        int stalls = Max(1, (int)(len / 2.6f));
        float uc = L.park.u0 + 1.f + len * 0.5f;
        for (float v = L.park.v0 + 8.f; v + 3.f <= L.park.v1; v += 16.f) {
            stallRow(g, F.P(uc, v - 3.f), R, -F.out, z + 0.04f, stalls, 5.f, 0.6f, hash32(e.seed + (u32)(v * 3.f)));
            if (v + 8.f <= L.park.v1 + 0.5f) stallRow(g, F.P(uc, v + 3.f), R, F.out, z + 0.04f, stalls, 5.f, 0.6f, hash32(e.seed + (u32)(v * 3.f) + 7u));
        }
    }
    // ---- canopies: main entrance (flat roof on four columns) and the ER drive-through canopy
    auto canopy = [&](const Rect& q, float hgt, float thick, u32 col, bool ems) {
        vec2 c = F.P(q.cu(), q.cv());
        if (!g.owns(c)) return;
        boxY(g, V3(c, z + hgt + thick * 0.5f), R, vec3(q.hu(), q.hv(), thick * 0.5f), col, M(MAT_CONCRETE_PANEL), true);
        for (int k = 0; k < 4; k++) {
            vec2 cp = F.P((k & 1) ? q.u1 - 0.5f : q.u0 + 0.5f, (k & 2) ? q.v1 - 0.5f : q.v0 + 0.8f);
            if ((k & 2) && ems) continue;   // the ER canopy's back rests on the wing
            cyl(g, V3(cp, z), 0.22f, 0.22f, hgt, detail ? 10 : 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
            collide(g, V3(cp, z + hgt * 0.5f), R, vec3(0.22f, 0.22f, hgt * 0.5f));
        }
        // downlights under the canopy
        int nl = detail ? Max(2, (int)(q.hu() / 3.f)) : 0;
        for (int k = 0; k < nl; k++) {
            vec2 lp = F.P(Lerp(q.u0 + 1.5f, q.u1 - 1.5f, (float)k / Max(1, nl - 1)), q.cv());
            boxY(g, V3(lp, z + hgt - 0.03f), R, vec3(0.3f, 0.3f, 0.03f), rgb(1.f, 0.98f, 0.92f, 0.5f), emMat(EA_NIGHT), true);
        }
        light(g, V3(c, z + hgt - 0.3f), vec3(1.f, 0.97f, 0.92f) * (ems ? 9000.f : 5000.f), ems ? 16.f : 12.f, 1, vec3(0, 0, -1), 0.3f);
    };
    canopy(L.entry, 4.2f, 0.45f, rgbv(kWall), false);
    canopy(apron, 4.6f, 0.6f, rgbv(kWall), true);
    {
        // EMERGENCY on the ER canopy fascia facing the street, red and lit at night
        vec2 ec = F.P(apron.cu(), apron.v0 - 0.02f);
        if (g.owns(F.P(apron.cu(), apron.cv()))) {
            const char* em = "EMERGENCY";
            float th = Min(0.5f, (apron.hu() * 2.f - 2.f) / textAdvance(em, 1.f, 0.3f));
            plainLetters(g, em, centredOrigin(em, V3(ec + f * 0.02f, z + 4.62f), readRight, th), readRight, vec3(0, 0, 1), th, rgb(1.f, 0.1f, 0.08f, 0.85f), emMat(EA_NIGHT),
                         0.04f, 0.16f);
            // parked ambulances in the bays
            int na = kind == HO_CITY ? 2 : 1;
            for (int k = 0; k < na; k++) {
                float u = Lerp(apron.u0 + 3.f, apron.u1 - 3.f, (k + 0.5f) / (kind == HO_CITY ? 3.f : 2.f));
                ambulance(g, F.P(u, apron.v1 - 3.8f), -f, z + 0.035f);
            }
        }
    }
    // ---- signs: a monument sign at the corner of the plaza, an EMERGENCY pointer by the ER drive, the H road sign
    vec2 ms = F.P(L.entry.u0 - 9.f, 1.8f);
    if (g.owns(ms)) monumentSign(g, ms, f, z, 5.2f, 1.5f, e.text.c_str(), kind == HO_CITY ? "HOSPITAL" : "EMERGENCY 24 HOURS", rgb(0.9f, 0.9f, 0.88f), M(MAT_STONE),
                                  rgb(0.15f, 0.35f, 0.6f), true);
    vec2 es = F.P(L.driveIn - 4.2f, 1.2f);
    if (g.owns(es)) {
        cyl(g, V3(es, z), 0.06f, 0.06f, 2.6f, 6, rgb(0.8f), M(MAT_METAL_BRUSHED), false);
        airport_mesh::signPanel(g, es + f * 0.06f, f, z + 1.7f, 1.6f, 0.8f, rgb(0.85f, 0.08f, 0.08f), rgb(1.f), kind == HO_CITY ? ">EMERGENCY|>AMBULANCE" : ">EMERGENCY",
                                M(MAT_METAL_PAINTED), 0.2f);
        collide(g, V3(es, z + 1.3f), R, vec3(0.1f, 0.1f, 1.3f));
    }
    // ---- palms along the front, planters and benches by the entrance, lamps in the car park
    for (float u = -e.hx + 3.f; u < e.hx - 2.f; u += 9.f) {
        bool clear = fabsf(u - L.driveIn) > 5.f && fabsf(u - L.driveOut) > 5.f && fabsf(u - L.parkLane) > 5.f && (u < L.entry.u0 - 7.f || u > L.entry.u1 + 7.f) &&
                     fabsf(u - (L.entry.u0 - 9.f)) > 4.f && (u < L.erCanopy.u0 - 3.f || u > L.erCanopy.u1 + 1.f);
        vec2 p = F.P(u, 2.4f);
        if (clear && g.owns(p)) prop(g, V3(p, z), r.f() * kTwoPi, r.range(0.9f, 1.15f), PROP_PALM_TALL, (u8)r.irange(0, 2));
    }
    for (int s = -1; s <= 1; s += 2) {
        vec2 pp = F.P(s < 0 ? L.entry.u0 - 3.f : L.entry.u1 + 3.f, 3.2f);
        if (g.owns(pp)) {
            boxY(g, V3(pp, z + 0.3f), R, vec3(1.5f, 0.6f, 0.3f), rgb(0.85f), M(MAT_CONCRETE_PANEL));
            leafBlobEx(*g.m, g.org, V3(pp, z + 0.75f), V3(R, 0.f), V3(F.out, 0.f), vec3(1.4f, 0.55f, 0.35f), vec3(0.4f, 0.62f, 0.3f), r.next(), 0.08f, 8, 4,
                       vec3(0.95f, 0.4f, 0.55f));
            collide(g, V3(pp, z + 0.3f), R, vec3(1.5f, 0.6f, 0.3f));
            prop(g, V3(F.P(s < 0 ? L.entry.u0 - 3.f : L.entry.u1 + 3.f, 4.4f), z + 0.04f), yawFacing(-F.out), 1.f, PROP_BENCH);
        }
    }
    for (float u = L.park.u0 + 4.f; u < L.park.u1 - 2.f; u += 18.f)
        for (float v = L.park.v0 + 16.f; v < L.park.v1 - 4.f; v += 32.f) {
            vec2 lp = F.P(u, v);
            if (g.owns(lp)) prop(g, V3(lp, z), yawAlong(R), 1.f, PROP_STREETLIGHT);
        }
    // ---- the ground helipad (community hospitals): pad, marks, lights, windsocks at two corners
    if (L.groundHeli) {
        vec2 hc = F.P(L.heli.x, L.heli.y);
        if (g.owns(hc)) {
            polyFlat(g, *g.d, circleFP(hc, L.heliR + 0.8f, detail ? 32 : 14), z + 0.05f, rgb(0.62f), M(MAT_CONCRETE));
            helipadMarks(g, hc, R, z + 0.06f, L.heliR, detail);
            windsock(g, hc + R * (L.heliR + 3.f) + F.out * (L.heliR - 1.f), z, 5.f);
            windsock(g, hc - R * (L.heliR + 1.5f) - F.out * (L.heliR - 1.f), z, 5.f);
            light(g, V3(hc, z + 7.f), vec3(0.9f, 0.95f, 1.f) * 5000.f, 18.f, 1, vec3(0, 0, -1), 0.25f);
        }
    }
    (void)h;
}

// ------------------------------------------------------------------------------------------------ placement
// Plot size for a kind (along the street, deep)
void plotSize(int kind, float* W, float* D) {
    *W = kind == HO_CITY ? 150.f : (kind == HO_COMMUNITY ? 76.f : 56.f);
    *D = kind == HO_CITY ? 64.f : (kind == HO_COMMUNITY ? 58.f : 44.f);
}

// The street the story's hospital lobby resolves to (interiors.cpp nearestStreet: the nearest drivable edge off the
// bridges) and the side of it the hint lies on
int lobbyStreet(const RoadNetwork& roads, vec2 hint, int* side) {
    std::vector<int> cand;
    roads.edgesInRect(hint - vec2(260.f), hint + vec2(260.f), cand);
    int best = -1;
    float bestD = 260.f;
    for (int ei : cand) {
        const RoadEdge& e = roads.edges[ei];
        if ((e.flags & RF_ELEVATED) || e.cls == RC_HIGHWAY || e.cls == RC_RAMP || (e.flags & RF_BRIDGE)) continue;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), b = e.pts[k + 1].xy();
            float t, d = distPointSegment2D(hint, a, b, &t);
            if (d < bestD) {
                bestD = d;
                best = ei;
                vec2 dir = normalize(b - a);
                // interiors.cpp: side +1 when cross(dir, p - a) >= 0, the side toward perp(dir)
                *side = (dir.x * (hint.y - a.y) - dir.y * (hint.x - a.x)) >= 0.f ? 1 : -1;
            }
        }
    }
    return best;
}

void place(SiteSet& S, WorldMap& map, const RoadNetwork& roads, BuildingSet& bs) {
    u32 anyStreet = (1u << RC_BOULEVARD) | (1u << RC_AVENUE) | (1u << RC_STREET) | (1u << RC_LANE) | (1u << RC_RURAL);
    for (int si = 0; si < kSpecCount; si++) {
        const Spec& sp = kSpecs[si];
        float W, D;
        plotSize(sp.kind, &W, &D);
        Frontage fr;
        bool ok = false;
        if (sp.host) {
            int side = 1;
            int edge = lobbyStreet(roads, sp.want, &side);
            // interiors.cpp's side is toward perp(dir); the kit's side +1 is toward (dir.y, -dir.x) = -perp(dir)
            if (edge >= 0) ok = findFrontage(map, roads, sp.want, 400.f, W, D, sp.reg, anyStreet, true, &fr, edge, -side);
        }
        for (int attempt = 0; !ok && attempt < 3; attempt++) {
            float w = W * (1.f - 0.1f * attempt), d = D * (1.f - 0.08f * attempt);
            ok = findFrontage(map, roads, sp.want, sp.kind == HO_CITY ? 300.f : 900.f, w, d, sp.reg, anyStreet, sp.reg != REG_REDLAND, &fr);
            if (ok) {
                W = w;
                D = d;
            }
        }
        if (!ok) {
            LOG("Places: no plot for %s near (%.0f, %.0f)", sp.name, sp.want.x, sp.want.y);
            continue;
        }
        const Layout L = layoutOf(sp.kind, W, D);
        const RoadEdge& ed = roads.edges[fr.edge];
        const float zs = fr.z + (ed.sidewalk > 0.5f ? 0.15f : 0.05f) + 0.02f;
        const size_t removed = clearPlot(bs.buildings, fr.c, fr.along, W * 0.5f, D * 0.5f);
        // level ground over the plot, a walkable and drivable surface at the sidewalk
        map.flattenRect(fr.c, fr.along, W * 0.5f - 2.f, D * 0.5f - 2.f, zs - 0.12f, 2.f);
        S.lotBlocks.push_back({fr.c, fr.along, W * 0.5f, D * 0.5f});
        S.vegBlocks.push_back({fr.c, fr.along, W * 0.5f, D * 0.5f});
        claimedPlots.push_back({fr.c, fr.along, W * 0.5f, D * 0.5f});
        {
            Pad pd;
            pd.c = fr.c;
            pd.ax = fr.along;
            pd.hx = W * 0.5f;
            pd.hy = D * 0.5f;
            pd.z = zs;
            pd.slope = 0.f;
            pd.kind = PAD_TURF;
            pd.drawn = 1;
            pd.skirt = 1;
            pd.flags = 0;
            pd.color = rgb(0.62f, 0.86f, 0.46f);
            S.pads.push_back(pd);
        }
        const vec2 c0 = fr.c - fr.out * (D * 0.5f);
        auto P = [&](float u, float v) { return c0 + fr.along * u + fr.out * v; };
        const float side = (perp(fr.along).x * fr.out.x + perp(fr.along).y * fr.out.y) > 0.f ? 1.f : -1.f;
        // grounds element
        {
            SiteElem e;
            e.kind = SK_HOSPITAL_GROUNDS;
            e.variant = sp.kind;
            e.seed = hash32(0x405Bu + (u32)si * 7919u);
            e.c = fr.c;
            e.ax = fr.along;
            e.hx = W * 0.5f;
            e.hy = D * 0.5f;
            e.z = zs;
            e.h = 8.f;
            e.p[0] = side;
            e.p[1] = (float)sp.kind;
            e.text = sp.sign;
            S.elems.push_back(e);
        }
        // the buildings: main block (lobby host), tower, emergency wing
        const float podTop = zs + heightOf(L);
        auto block = [&](int variant, const Rect& q, float floors, float groundH, float floorH, float base, bool heli, bool hostIt) {
            SiteElem e;
            e.kind = SK_HOSPITAL;
            e.variant = (u16)variant;
            e.seed = hash32(0x4050u + (u32)si * 131u + (u32)variant);
            e.c = P(q.cu(), q.cv());
            e.ax = -fr.out;
            e.hx = q.hu();
            e.hy = q.hv();
            e.z = zs;
            e.h = base - zs + groundH + (floors - 1.f) * floorH + (heli ? 8.f : 2.f);
            e.p[0] = floors;
            e.p[1] = groundH;
            e.p[2] = floorH;
            e.p[3] = base;
            e.p[4] = (float)sp.kind;
            e.p[5] = heli ? 1.f : 0.f;
            e.p[6] = -1.f;
            e.text = sp.sign;
            int ei = (int)S.elems.size();
            S.elems.push_back(e);
            SiteBuildingReq q2;
            q2.c = e.c;
            q2.ax = fr.along;
            q2.hx = q.hu();
            q2.hy = q.hv();
            q2.style = BS_MIDRISE;
            q2.roof = ROOF_FLAT;
            q2.floors = (u16)(variant == 1 ? floors + L.mainFloors : floors);
            q2.front = -fr.out;
            q2.baseZ = zs - 0.15f;
            q2.region = map.regionAt(e.c.x, e.c.y);
            q2.siteElem = ei;
            q2.siteHost = hostIt;
            bs.addSiteBuilding(map, q2, hash32(0x4051u + (u32)si * 977u + (u32)variant));
        };
        block(0, L.main, L.mainFloors, L.groundH, L.floorH, zs, false, sp.host);
        if (L.towerFloors > 0.f) block(1, L.tower, L.towerFloors, 3.6f, 3.6f, podTop, L.towerHeli, false);
        block(2, L.er, 1.f, 5.5f, 4.f, zs, false, false);
        // registry: the emergency entrance is the place's door
        const int placeIdx = (int)S.places.size();
        vec2 erDoor = P(L.erCanopy.cu(), L.erCanopy.v1 - 1.f);
        {
            NamedPlace np;
            np.name = sp.name;
            np.kind = PK_HOSPITAL;
            np.pos = fr.c;
            np.door = erDoor;
            np.radius = Max(W, D) * 0.6f;
            S.places.push_back(np);
        }
        // the curb cuts of the drives stay clear of lamp posts, street trees and bus stops
        for (float u : {L.driveIn, L.driveOut, L.parkLane}) keepClear(S, roads, P(u, -(fr.lotLine - fr.sidewalkC)), fr.along, 4.f, (fr.lotLine - fr.sidewalkC) + 0.5f);
        // walks: the sidewalk to the main doors and to the ER doors
        const float swV = -(fr.lotLine - fr.sidewalkC), zw = zs + 0.1f;
        auto walk = [&](vec2 a, vec2 b, float hw) { S.walks.push_back({vec3(a, zw), vec3(b, zw), hw, SW_PATH}); };
        walk(P(L.entry.cu(), swV), P(L.entry.cu(), L.main.v0 - 0.5f), 2.f);
        walk(P(L.erCanopy.u0 - 1.4f, swV), P(L.erCanopy.u0 - 1.4f, L.er.v0 - 0.8f), 1.f);
        walk(P(L.erCanopy.u0 - 1.4f, L.er.v0 - 0.8f), erDoor, 1.f);
        // people: paramedics and a nurse at the ambulance bay, a guard at the main doors, a smoker's corner, the benches
        u16 group = 1;
        for (int k = 0; k < 3; k++) {
            vec2 q = P(L.erCanopy.u0 + 4.f + k * 2.2f, L.erCanopy.v1 - 1.2f);
            S.anchors.push_back({vec3(q, zs + 0.04f), k == 1 ? fr.out : -fr.out, PA_WORK, (u8)placeIdx, group});
        }
        group++;
        S.anchors.push_back({vec3(P(L.entry.u1 - 1.f, L.main.v0 - 1.2f), zs + 0.04f), -fr.out, PA_GUARD, (u8)placeIdx, group++});
        for (int k = 0; k < 2; k++)
            S.anchors.push_back({vec3(P(L.main.u1 - 2.5f + k * 0.9f, L.main.v0 - 1.6f), zs + 0.04f), k ? -fr.along : fr.along, PA_STAND, (u8)placeIdx, group});
        group++;
        for (int s = -1; s <= 1; s += 2)
            S.anchors.push_back({vec3(P(s < 0 ? L.entry.u0 - 3.f : L.entry.u1 + 3.f, 4.4f), zs + 0.04f), -fr.out, PA_SIT, (u8)placeIdx, group++});
        LOG("Places: %s (%s) at (%.0f, %.0f) on edge %d, %zu buildings made way", sp.name, sp.kind == HO_CITY ? "city" : (sp.kind == HO_COMMUNITY ? "community" : "clinic"),
            fr.c.x, fr.c.y, fr.edge, removed);
    }
}

// Facade records: white panels with a teal band, blue-green glass, lit round the clock
void facades(SiteSet& S, BuildingSet& bs) {
    for (size_t bi = 0; bi < bs.buildings.size(); bi++) {
        Building& b = bs.buildings[bi];
        if (b.siteElem < 0 || b.siteElem >= (int)S.elems.size()) continue;
        SiteElem& e = S.elems[b.siteElem];
        if (e.kind != SK_HOSPITAL) continue;
        FacadeGPU& f = bs.facades[b.facade];
        f.floorH = e.p[2];
        f.groundH = e.p[1];
        f.style = 2.f;
        f.bayW = e.variant == 2 ? 4.f : 3.2f;
        f.winW = e.variant == 1 ? 0.72f : 0.62f;
        f.winH = 0.6f;
        f.sillH = 0.85f;
        f.flags = e.variant == 1 ? 16u : (1u | 16u);   // storefront glass on the ground floors, lit offices above
        f.litFrac = 0.88f;
        f.wallColor = packRGBA8(kWall.x, kWall.y, kWall.z, 1.f);
        f.frameColor = packRGBA8(kBand.x, kBand.y, kBand.z, 1.f);
        f.glassColor = packRGBA8(0.32f, 0.5f, 0.56f, 1.f);
        f.wallLayer = (float)MAT_CONCRETE_PANEL;
        f.roomDepth = 6.f;
        b.height = e.p[3] - e.z + e.p[1] + (e.p[0] - 1.f) * e.p[2];
        b.baseZ = e.z;
        e.p[6] = (float)bi;
        e.p[7] = (float)b.facade;
    }
}

}  // namespace hospital

}  // namespace World
