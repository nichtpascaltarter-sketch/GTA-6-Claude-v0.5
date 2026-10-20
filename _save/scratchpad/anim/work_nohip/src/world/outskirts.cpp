// Places out in the Palmera farmlands. Palmera State Penitentiary north of County Road 755: a concrete perimeter wall with
// seven guard towers (glass cabs, catwalks, searchlights) inside a razor-wire fence, the sally port (gatehouse, outer and
// inner gates, the vehicle trap between), four cell blocks, the chow hall and the infirmary round the exercise yard
// (track, basketball court, weight pile, bleachers) under floodlight masts, the administration building and the car parks
// outside. Palmera Speedway north of County Road 735: a paved oval with its outer wall and catch fence, the grandstand on
// the front stretch with the race control tower on top, pit road with numbered boxes and the garages in the infield, the
// flag stand over the start / finish line, flags along the back stretch, the scoreboard pylon, light towers for night
// racing and the car park.
#include "places.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace outskirts {

using namespace sitegeo;
using namespace place_kit;

inline vec2 rightOf(vec2 f) { return vec2(f.y, -f.x); }

// ------------------------------------------------------------------------------------------------ Palmera State Penitentiary
constexpr float kPrX = 350.f, kPrY = 9075.f;       // compound centre
constexpr float kPrHX = 115.f, kPrHY = 100.f;      // main wall half extents (x 235..465, y 8975..9175)
constexpr float kPrFence = 12.f;                   // the outer fence runs this far outside the wall
constexpr float kPrWallH = 6.5f, kPrWallT = 0.7f;
constexpr float kPrRoadY = 8800.f;                 // County Road 755
constexpr float kPrLaneHW = 3.5f;                  // sally port lane half width
constexpr float kPrGateY0 = kPrY - kPrHY - kPrFence - 6.f, kPrGateY1 = kPrY - kPrHY + 14.f;   // gatehouse from the apron to the inner gate
struct Block {
    float x0, y0, x1, y1;
    int floors;
    u8 kind;   // 0 cell block, 1 chow hall, 2 infirmary, 3 administration
};
const Block kPrBlocks[] = {
    {250.f, 9130.f, 330.f, 9152.f, 3, 0}, {370.f, 9130.f, 450.f, 9152.f, 3, 0}, {250.f, 9000.f, 330.f, 9022.f, 3, 0}, {370.f, 9000.f, 450.f, 9022.f, 3, 0},
    {250.f, 9050.f, 296.f, 9090.f, 2, 1},   {404.f, 9050.f, 450.f, 9090.f, 2, 2},   {375.f, 8915.f, 425.f, 8937.f, 2, 3},
};
constexpr float kYardX0 = 312.f, kYardX1 = 388.f, kYardY0 = 9036.f, kYardY1 = 9112.f;
const vec2 kTowers[] = {vec2(235.f, 8975.f), vec2(465.f, 8975.f), vec2(465.f, 9175.f), vec2(235.f, 9175.f), vec2(235.f, 9075.f), vec2(465.f, 9075.f), vec2(350.f, 9175.f)};

// Chain-link fence with razor wire, posts every 3.5 m leaning their tops inward (inw); detail only
void razorFence(G& g, vec2 a, vec2 b, float z, float h, vec2 inw) {
    if (!g.detail) return;
    vec2 d = b - a;
    float L = length(d);
    if (L < 0.5f) return;
    d = d / L;
    const u32 post = rgb(0.55f, 0.57f, 0.6f), wire = rgb(0.5f, 0.52f, 0.55f), im = M(MAT_METAL_PAINTED);
    int n = Max(1, (int)(L / 3.5f));
    for (int k = 0; k <= n; k++) {
        vec2 p = a + d * (L * k / n);
        vec2 mid = p + d * (L * 0.5f / n);
        if (g.owns(p)) {
            boxY(g, V3(p, z + h * 0.5f - 0.2f), d, vec3(0.05f, 0.05f, h * 0.5f + 0.2f), post, im);
            beam(g, V3(p, z + h), V3(p + inw * 0.6f, z + h + 0.55f), 0.05f, 0.05f, post, im);
        }
        if (k == n || !g.owns(mid)) continue;
        vec2 q = p + d * (L / n);
        // wires (flat ribbons in the fence plane), two diagonals, the razor coil on the arms as a zigzag
        for (float zz : {0.12f, h * 0.5f, h - 0.05f}) panel2(g, V3(p, z + zz - 0.015f), V3(q, z + zz - 0.015f), V3(q, z + zz + 0.015f), V3(p, z + zz + 0.015f), wire, im);
        panel2(g, V3(p, z + 0.12f), V3(p + d * 0.02f, z + 0.12f), V3(q + d * 0.02f, z + h), V3(q, z + h), wire, im);
        panel2(g, V3(q, z + 0.12f), V3(q - d * 0.02f, z + 0.12f), V3(p - d * 0.02f, z + h), V3(p, z + h), wire, im);
        vec2 c0 = p + inw * 0.3f, c1 = q + inw * 0.3f;
        for (int s = 0; s < 4; s++) {
            float t0 = s / 4.f, t1 = (s + 1) / 4.f;
            vec3 u = V3(lerp(c0, c1, t0), z + h + ((s & 1) ? 0.1f : 0.5f)), w = V3(lerp(c0, c1, t1), z + h + ((s & 1) ? 0.5f : 0.1f));
            beam(g, u, w, 0.025f, 0.025f, rgb(0.72f, 0.74f, 0.76f), M(MAT_METAL_BRUSHED));
        }
        collide(g, V3((p + q) * 0.5f, z + h * 0.5f), d, vec3(L * 0.5f / n, 0.08f, h * 0.5f + 0.4f));
    }
}

// Guard tower: concrete shaft, glass cab with a flat roof, catwalk and railing, searchlight; floor at z + 10
void guardTower(G& g, vec2 c, float z, vec2 look) {
    if (!g.owns(c)) return;
    const bool detail = g.detail;
    const float fz = z + 10.f;
    const u32 conc = rgb(0.8f, 0.79f, 0.76f), dark = rgb(0.2f), cm = M(MAT_CONCRETE_PANEL);
    const vec2 R = rightOf(look);
    boxY(g, V3(c, z - 0.5f + (fz - z + 0.5f) * 0.5f), R, vec3(1.6f, 1.6f, (fz - z + 0.5f) * 0.5f), conc, cm);
    boxY(g, V3(c, fz - 0.15f), R, vec3(2.6f, 2.6f, 0.15f), conc, cm, true);   // floor slab and catwalk
    // glass cab: dark glass all round on a low wall, lit at night
    boxY(g, V3(c, fz + 0.5f), R, vec3(2.f, 2.f, 0.5f), conc, cm);
    for (int k = 0; k < 4; k++) {
        vec2 n = rotate(R, kHalfPi * k), t = rightOf(n);
        vec2 p = c + n * 2.01f;
        quad(g, *g.m, V3(p - t * 2.f, fz + 1.f), V3(p + t * 2.f, fz + 1.f), V3(p + t * 2.f, fz + 2.8f), V3(p - t * 2.f, fz + 2.8f), rgb(0.95f, 0.85f, 0.6f, 0.05f),
             emMat(EA_NIGHT), V3(n, 0.f));
    }
    boxY(g, V3(c, fz + 2.95f), R, vec3(2.4f, 2.4f, 0.15f), conc, cm, true);   // roof
    if (detail) {
        for (int k = 0; k < 4; k++) {   // catwalk railing
            vec2 n = rotate(R, kHalfPi * k), t = rightOf(n);
            ironRailing(g, c + n * 2.55f - t * 2.55f, c + n * 2.55f + t * 2.55f, fz, 1.05f, 0.9f, dark, false, true);
        }
        // ladder on the shaft
        for (int k = 0; k < 16; k++) boxY(g, V3(c - look * 1.65f, z + 0.6f + k * 0.6f), R, vec3(0.25f, 0.03f, 0.02f), dark, M(MAT_METAL_PAINTED));
    }
    // searchlight on the roof, aimed out along `look`
    vec2 sp = c + look * 1.6f;
    cyl(g, V3(sp, fz + 3.1f), 0.12f, 0.12f, 0.4f, 6, dark, M(MAT_METAL_PAINTED), false);
    boxY(g, V3(sp, fz + 3.7f), look, vec3(0.35f, 0.3f, 0.3f), dark, M(MAT_METAL_PAINTED));
    disc(g, V3(sp + look * 0.36f, fz + 3.7f), V3(look, 0.f), 0.25f, detail ? 12 : 6, rgb(1.f, 0.97f, 0.88f, 0.7f), emMat(EA_NIGHT));
    light(g, V3(sp + look * 0.5f, fz + 3.7f), vec3(1.f, 0.96f, 0.85f) * 26000.f, 70.f, 1, normalize(V3(look, -0.35f)), 0.06f);
    light(g, V3(c, fz + 2.f), vec3(1.f, 0.85f, 0.6f) * 300.f, 5.f, 1);
    collide(g, V3(c, (z + fz + 3.1f) * 0.5f), R, vec3(1.6f, 1.6f, (fz + 3.1f - z) * 0.5f));
}

// SK_PRISON_WALL: the main wall between two towers (a, b), the outer fence alongside it; p[0..1]: inward normal, p[2]:
// gap centre (metres from a) or -1, p[3]: gap half width; z: ground
void genPrisonWall(const SiteElem& e, G& g) {
    const vec2 a = e.a, b = e.b, inw(e.p[0], e.p[1]);
    const vec2 d = normalize(b - a);
    const float L = length(b - a), z = e.z;
    const float gc = e.p[2], ghw = e.p[3];
    const u32 conc = rgb(0.78f, 0.77f, 0.74f), cap = rgb(0.7f, 0.69f, 0.66f), stain = rgb(0.6f, 0.58f, 0.55f), cm = M(MAT_CONCRETE_PANEL);
    auto run = [&](float s0, float s1) {
        if (s1 - s0 < 0.2f) return;
        int np = Max(1, (int)ceilf((s1 - s0) / 16.f));
        for (int k = 0; k < np; k++) {
            float a0 = s0 + (s1 - s0) * k / np, a1 = s0 + (s1 - s0) * (k + 1) / np, hl = (a1 - a0) * 0.5f;
            vec2 mid = a + d * ((a0 + a1) * 0.5f);
            if (!g.owns(mid)) continue;
            boxY(g, V3(mid, z - 0.5f + (kPrWallH + 0.5f) * 0.5f), d, vec3(hl, kPrWallT * 0.5f, (kPrWallH + 0.5f) * 0.5f), conc, cm);
            boxY(g, V3(mid, z + kPrWallH + 0.1f), d, vec3(hl, kPrWallT * 0.5f + 0.12f, 0.1f), cap, cm, true);
            collide(g, V3(mid, z + kPrWallH * 0.5f), d, vec3(hl, kPrWallT * 0.5f, kPrWallH * 0.5f + 0.2f));
            if (g.detail) {
                for (int s = -1; s <= 1; s += 2) {   // weathered foot on both faces
                    vec2 o = inw * (s * (kPrWallT * 0.5f + 0.01f));
                    quad(g, *g.m, V3(a + d * a0 + o, z - 0.1f), V3(a + d * a1 + o, z - 0.1f), V3(a + d * a1 + o, z + 0.6f), V3(a + d * a0 + o, z + 0.6f), stain, cm,
                         V3(inw * (float)s, 0.f));
                }
                // razor wire along the top of the wall
                vec2 c0 = a + d * a0, c1 = a + d * a1;
                int nz = Max(2, (int)((a1 - a0) / 1.2f));
                for (int q = 0; q < nz; q++) {
                    vec3 u = V3(lerp(c0, c1, (float)q / nz), z + kPrWallH + ((q & 1) ? 0.25f : 0.65f)), w = V3(lerp(c0, c1, (float)(q + 1) / nz), z + kPrWallH + ((q & 1) ? 0.65f : 0.25f));
                    beam(g, u, w, 0.03f, 0.03f, rgb(0.72f, 0.74f, 0.76f), M(MAT_METAL_BRUSHED));
                }
            }
        }
    };
    // the wall stops short of the towers at both ends (the tower shafts stand in the corners)
    if (gc > 0.f) {
        run(1.6f, gc - ghw);
        run(gc + ghw, L - 1.6f);
    } else run(1.6f, L - 1.6f);
    // the outer fence, kPrFence outside, with the same gap; it runs on round the corners (p[4], p[5]: a / b is a corner)
    const float ea = e.p[4] > 0.5f ? kPrFence : 0.f, eb = e.p[5] > 0.5f ? kPrFence : 0.f;
    vec2 fa = a - inw * kPrFence - d * ea, fb = b - inw * kPrFence + d * eb;
    if (gc > 0.f) {
        razorFence(g, fa, fa + d * (gc + ea - ghw), z, 4.8f, inw);
        razorFence(g, fa + d * (gc + ea + ghw), fb, z, 4.8f, inw);
    } else razorFence(g, fa, fb, z, 4.8f, inw);
}

// SK_PRISON: variant 0-3: the buildings (cell block, chow hall, infirmary, administration; c / hx / hy / ax = front, z,
// p[0]: floors, p[1] ground floor, p[2] floor height, p[6] building, p[7] facade); 4: the sally port; 5: the grounds
// (yard, surfaces, signs)
void genPrisonBlock(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    const bool detail = g.detail;
    const vec2 f = e.ax, R = rightOf(f);
    const float hw = e.hx, hd = e.hy, z = e.z;
    const float top = z + e.p[1] + (e.p[0] - 1.f) * e.p[2];
    const u32 fac = (u32)e.p[7];
    const float bay = gBuildings && e.p[6] >= 0.f ? gBuildings->facades[fac].bayW : 2.4f;
    auto P = [&](float u, float v) { return e.c + R * u + f * v; };
    std::vector<vec2> fp = {P(-hw, -hd), P(hw, -hd), P(hw, hd), P(-hw, hd)};
    if (dot(cross(vec3(fp[1] - fp[0], 0.f), vec3(fp[2] - fp[1], 0.f)), vec3(0, 0, 1)) < 0.f) std::reverse(fp.begin(), fp.end());
    facadeRing(g, fp, z - 0.8f, top, z, fac, bay);
    collide(g, V3(e.c, (z - 0.8f + top) * 0.5f), R, vec3(hw, hd, (top + 0.8f - z) * 0.5f));
    polyFlat(g, *g.m, fp, top, rgb(0.72f), M(MAT_ROOF_GRAVEL));
    const u32 conc = rgb(0.78f, 0.77f, 0.74f), cm = M(MAT_CONCRETE_PANEL);
    for (int k = 0; k < 4; k++) {
        vec2 a = fp[k], b = fp[(k + 1) & 3];
        vec2 d = normalize(b - a), on = perp(d);
        if (dot(on, (a + b) * 0.5f - e.c) < 0.f) on = -on;
        boxY(g, V3((a + b) * 0.5f + on * 0.12f, top + 0.45f), d, vec3(length(b - a) * 0.5f + 0.24f, 0.24f, 0.45f), conc, cm);
    }
    Rng r(e.seed);
    if (detail)
        for (int k = 0; k < 3; k++) {
            vec2 u = P(r.range(-hw + 3.f, hw - 3.f), r.range(-hd + 2.5f, hd - 2.5f));
            boxY(g, V3(u, top + 0.8f), R, vec3(1.2f, 0.9f, 0.8f), rgb(0.7f), M(MAT_METAL_PAINTED));
        }
    // a steel door with a small canopy at the front, the block's name
    vec2 dp = P(0.f, hd + 0.02f);
    quad(g, *g.m, V3(dp - R * 1.f, z), V3(dp + R * 1.f, z), V3(dp + R * 1.f, z + 2.4f), V3(dp - R * 1.f, z + 2.4f), rgb(0.3f, 0.34f, 0.38f), M(MAT_METAL_PAINTED),
         V3(f, 0.f));
    boxY(g, V3(P(0.f, hd + 0.8f), z + 2.9f), R, vec3(1.8f, 0.8f, 0.1f), conc, cm, true);
    boxY(g, V3(P(0.f, hd + 0.3f), z + 2.7f), R, vec3(0.3f, 0.12f, 0.1f), rgb(1.f, 0.95f, 0.8f, 0.5f), emMat(EA_NIGHT), true);
    light(g, V3(P(0.f, hd + 1.f), z + 2.6f), vec3(1.f, 0.92f, 0.75f) * 1200.f, 9.f, 1, vec3(0, 0, -1), 0.3f);
    const vec3 readRight = V3(-R, 0.f);
    const char* nm = e.text.c_str();
    if (*nm) {
        float th = e.variant == 3 ? 0.55f : 0.9f;
        plainLetters(g, nm, centredOrigin(nm, V3(P(0.f, hd + 0.04f), top - th - 0.5f), readRight, th), readRight, vec3(0, 0, 1), th, rgb(0.22f, 0.24f, 0.26f),
                     M(MAT_METAL_PAINTED), 0.f, 0.14f);
    }
}

// Sally port: two guardhouses flanking the lane under a steel canopy, the outer and inner sliding gates, a booth with a
// barrier arm on the apron, warning signs; c: lane centre at the wall line, ax: out (toward the road), z
void genSallyPort(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    const bool detail = g.detail;
    const vec2 out = e.ax, R = rightOf(out);
    const float z = e.z;
    auto P = [&](float u, float v) { return e.c + R * u + out * v; };   // v: toward the road
    const u32 conc = rgb(0.8f, 0.79f, 0.76f), cm = M(MAT_CONCRETE_PANEL), steel = rgb(0.35f, 0.37f, 0.4f), dark = rgb(0.18f);
    const float vIn = -14.f, vOut = kPrFence + 6.f;   // inner gate (inside the wall), outer gate (outside the fence)
    // guardhouses both sides of the lane
    for (int s = -1; s <= 1; s += 2) {
        vec2 hc = P(s * (kPrLaneHW + 4.f), (vIn + vOut) * 0.5f);
        float hl = (vOut - vIn) * 0.5f;
        boxY(g, V3(hc, z - 0.5f + 4.f), R, vec3(4.f, hl, 4.5f), conc, cm);
        boxY(g, V3(hc, z + 8.65f), R, vec3(4.2f, hl + 0.2f, 0.15f), rgb(0.7f), cm, true);
        collide(g, V3(hc, z + 4.f), R, vec3(4.f, hl, 4.f));
        // window band facing the lane
        vec2 wp = hc - R * (s * 4.01f);
        quad(g, *g.m, V3(wp - out * (hl - 2.f), z + 1.2f), V3(wp + out * (hl - 2.f), z + 1.2f), V3(wp + out * (hl - 2.f), z + 2.4f), V3(wp - out * (hl - 2.f), z + 2.4f),
             rgb(0.95f, 0.88f, 0.65f, 0.05f), emMat(EA_NIGHT), V3(-R * (float)s, 0.f));
    }
    // canopy over the vehicle trap
    boxY(g, V3(P(0.f, (vIn + vOut) * 0.5f), z + 6.2f), R, vec3(kPrLaneHW + 0.2f, (vOut - vIn) * 0.5f, 0.2f), steel, M(MAT_METAL_PAINTED), true);
    for (int k = 0; k < 3; k++) {
        vec2 lp = P(0.f, Lerp(vIn + 3.f, vOut - 3.f, k / 2.f));
        boxY(g, V3(lp, z + 5.97f), R, vec3(0.5f, 0.25f, 0.03f), rgb(1.f, 0.97f, 0.9f, 0.6f), emMat(EA_NIGHT), true);
    }
    light(g, V3(P(0.f, (vIn + vOut) * 0.5f), z + 5.8f), vec3(1.f, 0.97f, 0.9f) * 9000.f, 18.f, 1, vec3(0, 0, -1), 0.3f);
    // sliding gates: heavy frames with vertical bars, closed across the lane
    for (float v : {vIn, vOut}) {
        vec2 a = P(-kPrLaneHW, v), b = P(kPrLaneHW, v);
        beam(g, V3(a, z + 0.2f), V3(b, z + 0.2f), 0.12f, 0.15f, steel, M(MAT_METAL_PAINTED));
        beam(g, V3(a, z + 4.4f), V3(b, z + 4.4f), 0.12f, 0.15f, steel, M(MAT_METAL_PAINTED));
        if (detail)
            for (int k = 0; k <= 20; k++) {
                vec2 p = lerp(a, b, k / 20.f);
                boxY(g, V3(p, z + 2.3f), R, vec3(0.025f, 0.025f, 2.1f), steel, M(MAT_METAL_PAINTED));
            }
        else panel2(g, V3(a, z + 0.2f), V3(b, z + 0.2f), V3(b, z + 4.4f), V3(a, z + 4.4f), steel, M(MAT_METAL_PAINTED));
        collide(g, V3(P(0.f, v), z + 2.3f), R, vec3(kPrLaneHW, 0.1f, 2.3f));
        // red / green gate lights
        for (int s = -1; s <= 1; s += 2)
            boxY(g, V3(P(s * (kPrLaneHW + 0.2f), v + (v > 0.f ? 0.3f : -0.3f)), z + 5.f), R, vec3(0.12f, 0.12f, 0.12f),
                 s < 0 ? rgb(1.f, 0.1f, 0.05f, 0.7f) : rgb(0.1f, 1.f, 0.3f, 0.5f), emMat(EA_NIGHT), true);
    }
    // booth on the apron with a barrier arm across the incoming lane, stop line, warning sign
    vec2 bc = P(kPrLaneHW + 2.2f, vOut + 7.f);
    boxY(g, V3(bc, z + 1.4f), R, vec3(1.3f, 1.3f, 1.4f), conc, cm);
    boxY(g, V3(bc, z + 2.9f), R, vec3(1.6f, 1.6f, 0.1f), rgb(0.7f), cm, true);
    quad(g, *g.m, V3(bc - R * 1.31f - out * 1.f, z + 1.1f), V3(bc - R * 1.31f + out * 1.f, z + 1.1f), V3(bc - R * 1.31f + out * 1.f, z + 2.3f),
         V3(bc - R * 1.31f - out * 1.f, z + 2.3f), rgb(0.95f, 0.88f, 0.65f, 0.05f), emMat(EA_NIGHT), V3(-R, 0.f));
    collide(g, V3(bc, z + 1.4f), R, vec3(1.3f, 1.3f, 1.4f));
    vec2 arm0 = P(kPrLaneHW + 0.6f, vOut + 5.5f);
    boxY(g, V3(arm0, z + 0.55f), R, vec3(0.2f, 0.2f, 0.55f), rgb(0.9f, 0.8f, 0.1f), M(MAT_METAL_PAINTED));
    for (int k = 0; k < 7; k++) {
        vec2 p0 = arm0 - R * (k * 1.f), p1 = arm0 - R * ((k + 1) * 1.f);
        beam(g, V3(p0, z + 1.05f), V3(p1, z + 1.05f), 0.1f, 0.1f, (k & 1) ? rgb(0.95f, 0.1f, 0.08f) : rgb(0.97f), M(MAT_METAL_PAINTED));
    }
    paintLine(g, P(-kPrLaneHW, vOut + 4.f), P(kPrLaneHW, vOut + 4.f), 0.45f, z + 0.06f, kWhiteC, M(MAT_PAINT_WHITE));
    if (detail) {
        vec2 sp = P(-kPrLaneHW - 2.4f, vOut + 8.f);
        cyl(g, V3(sp, z), 0.06f, 0.06f, 2.6f, 6, rgb(0.8f), M(MAT_METAL_BRUSHED), false);
        airport_mesh::signPanel(g, sp + out * 0.06f, out, z + 1.3f, 1.8f, 1.2f, rgb(0.95f), rgb(0.7f, 0.05f, 0.05f), "STOP|ALL VEHICLES|SUBJECT TO SEARCH",
                                M(MAT_METAL_PAINTED), 0.2f);
    }
    (void)dark;
}

// The grounds: gravel inside the wall, the exercise yard (fence, dirt track, basketball court, weight pile, bleachers),
// the car parks and the entrance sign
void genPrisonGrounds(const SiteElem& e, G& g) {
    const bool detail = g.detail;
    const float z = e.z;
    const vec2 X(1, 0), Y(0, 1);
    const u32 gravel = rgb(0.8f, 0.76f, 0.68f), dirt = rgb(0.72f, 0.55f, 0.4f);
    // compound floor (inside the wall) and the yard
    drapeRect(g, vec2(kPrX, kPrY), X, kPrHX - 0.5f, kPrHY - 0.5f, 0.03f, gravel, M(MAT_PLASTER), detail ? 10.f : 30.f, z);
    const vec2 yc((kYardX0 + kYardX1) * 0.5f, (kYardY0 + kYardY1) * 0.5f);
    const float yhx = (kYardX1 - kYardX0) * 0.5f, yhy = (kYardY1 - kYardY0) * 0.5f;
    drapeRect(g, yc, X, yhx, yhy, 0.05f, rgb(0.52f, 0.66f, 0.38f), M(MAT_GRASS), detail ? 8.f : 24.f, z);
    // running track round the yard (dirt oval)
    const float trA = yhx - 6.f, trB = yhy - 6.f;
    int seg = detail ? 40 : 16;
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec2 i0 = yc + vec2(cosf(a0) * (trA - 1.6f), sinf(a0) * (trB - 1.6f)), i1 = yc + vec2(cosf(a1) * (trA - 1.6f), sinf(a1) * (trB - 1.6f));
        vec2 o0 = yc + vec2(cosf(a0) * (trA + 1.6f), sinf(a0) * (trB + 1.6f)), o1 = yc + vec2(cosf(a1) * (trA + 1.6f), sinf(a1) * (trB + 1.6f));
        if (!g.owns((i0 + o1) * 0.5f)) continue;
        quad(g, *g.d, V3(i0, z + 0.07f), V3(i1, z + 0.07f), V3(o1, z + 0.07f), V3(o0, z + 0.07f), dirt, M(MAT_DIRT), vec3(0, 0, 1));
    }
    // yard fence with a gate on the south side
    if (detail) {
        vec2 c0(kYardX0, kYardY0), c1(kYardX1, kYardY0), c2(kYardX1, kYardY1), c3(kYardX0, kYardY1);
        razorFence(g, c1, c2, z, 3.6f, vec2(-1, 0));
        razorFence(g, c2, c3, z, 3.6f, vec2(0, -1));
        razorFence(g, c3, c0, z, 3.6f, vec2(1, 0));
        razorFence(g, c0, vec2(yc.x - 2.5f, kYardY0), z, 3.6f, vec2(0, 1));
        razorFence(g, vec2(yc.x + 2.5f, kYardY0), c1, z, 3.6f, vec2(0, 1));
    }
    // basketball court (concrete, lines, two hoops) and the weight pile under a shade roof
    vec2 bc = yc + vec2(0.f, 10.f);
    if (g.owns(bc)) {
        std::vector<vec2> ct = rectPoly(bc, X, 14.f, 7.5f);
        polyFlat(g, *g.m, ct, z + 0.09f, rgb(0.75f), M(MAT_CONCRETE));
        if (detail) {
            paintRect(g, bc, X, 13.8f, 7.3f, z + 0.1f, rgb(0.95f), M(MAT_PAINT_WHITE));
            paintLine(g, bc - Y * 7.3f, bc + Y * 7.3f, 0.08f, z + 0.105f, rgb(0.95f), M(MAT_PAINT_WHITE));
        }
        for (int s = -1; s <= 1; s += 2) {
            vec2 hp = bc + X * (s * 14.6f);
            cyl(g, V3(hp, z), 0.08f, 0.08f, 3.3f, 6, rgb(0.3f), M(MAT_METAL_PAINTED), false);
            boxY(g, V3(hp - X * (s * 0.6f), z + 3.3f), Y, vec3(0.9f, 0.03f, 0.55f), rgb(0.9f), M(MAT_PLASTIC));
            ring(g, V3(hp - X * (s * 0.9f), z + 3.05f), vec3(0, 0, 1), 0.23f, 0.02f, 0.02f, 8, rgb(0.9f, 0.4f, 0.1f), M(MAT_METAL_PAINTED));
            collide(g, V3(hp, z + 1.6f), X, vec3(0.1f, 0.1f, 1.6f));
        }
    }
    vec2 wc = yc + vec2(-16.f, -14.f);
    if (g.owns(wc)) {
        polyFlat(g, *g.m, rectPoly(wc, X, 5.f, 4.f), z + 0.09f, rgb(0.62f), M(MAT_CONCRETE));
        for (int k = 0; k < 4; k++) cyl(g, V3(wc + vec2((k & 1) ? 4.5f : -4.5f, (k & 2) ? 3.5f : -3.5f), z), 0.08f, 0.08f, 3.f, 6, rgb(0.4f), M(MAT_METAL_PAINTED), false);
        boxY(g, V3(wc, z + 3.05f), X, vec3(5.2f, 4.2f, 0.05f), rgb(0.55f, 0.57f, 0.6f), M(MAT_CORRUGATED), true);
        if (detail)
            for (int k = 0; k < 3; k++) {   // benches with bars and plates
                vec2 p = wc + vec2(-3.f + k * 3.f, 0.f);
                boxY(g, V3(p, z + 0.45f), Y, vec3(0.2f, 0.7f, 0.05f), rgb(0.15f), M(MAT_LEATHER));
                beam(g, V3(p - X * 0.9f, z + 1.1f), V3(p + X * 0.9f, z + 1.1f), 0.04f, 0.04f, rgb(0.7f), M(MAT_METAL_BRUSHED));
                for (int s = -1; s <= 1; s += 2) cyl(g, V3(p + X * (s * 0.7f), z + 0.85f), 0.22f, 0.22f, 0.5f, 10, rgb(0.08f), M(MAT_RUBBER), true);
            }
    }
    vec2 bl = yc + vec2(16.f, -14.f);
    if (g.owns(bl)) bleachers(g, bl, X, Y, z, 12.f, 4, 0.8f, 0.42f, rgb(0.6f, 0.62f, 0.65f), rgb(0.4f));
    // car parks outside: visitors west of the road, staff east
    for (int s = -1; s <= 1; s += 2) {
        float x0 = s < 0 ? 255.f : 365.f, x1 = s < 0 ? 335.f : 445.f;
        float y0 = 8845.f, y1 = s < 0 ? 8915.f : 8900.f;
        for (float y = y0 + 8.f; y + 3.f <= y1; y += 16.f) {
            int stalls = (int)((x1 - x0 - 2.f) / 2.6f);
            vec2 c((x0 + x1) * 0.5f, y);
            stallRow(g, c - Y * 3.f, X, -Y, z + 0.03f, stalls, 5.f, s < 0 ? 0.45f : 0.7f, hash32(e.seed + (u32)y * 7u + (u32)(s + 3)));
            if (y + 8.f <= y1) stallRow(g, c + Y * 3.f, X, Y, z + 0.03f, stalls, 5.f, s < 0 ? 0.45f : 0.7f, hash32(e.seed + (u32)y * 11u + (u32)(s + 5)));
        }
    }
    // entrance sign at the road
    vec2 ms(338.f, kPrRoadY + 16.f);
    if (g.owns(ms)) monumentSign(g, ms, vec2(0, -1), z, 7.f, 1.8f, "PALMERA STATE", "PENITENTIARY", rgb(0.72f, 0.7f, 0.66f), M(MAT_CONCRETE_PANEL), rgb(0.2f), false);
    vec2 ws(362.f, kPrRoadY + 22.f);
    if (g.owns(ws) && detail) {
        cyl(g, V3(ws, z), 0.06f, 0.06f, 2.6f, 6, rgb(0.8f), M(MAT_METAL_BRUSHED), false);
        airport_mesh::signPanel(g, ws + vec2(0, -0.06f), vec2(0, -1), z + 1.3f, 2.f, 1.2f, rgb(0.95f), rgb(0.7f, 0.05f, 0.05f), "STATE PROPERTY|NO TRESPASSING|NO HITCHHIKERS",
                                M(MAT_METAL_PAINTED), 0.2f);
    }
}

// ------------------------------------------------------------------------------------------------ Palmera Speedway
constexpr float kSwX = 400.f, kSwY = 5790.f;       // oval centre
constexpr float kSwHalfStraight = 125.f, kSwR = 70.f, kSwTrackW = 16.f;
constexpr float kSwRoadY = 5600.f;                 // County Road 735
// grandstand on the front stretch (south), pit road inside it
constexpr float kSwStandX0 = 300.f, kSwStandX1 = 500.f;
inline float swIn() { return kSwR - kSwTrackW * 0.5f; }
inline float swOut() { return kSwR + kSwTrackW * 0.5f; }
// point on the oval at parameter t (0..1, counter-clockwise from the start of the front stretch) and radius rr; the
// sections share t the same way at every radius so points at one t line up across the track
vec2 ovalAt(float t, float rr) {
    const float s1 = 2.f * kSwHalfStraight, s2 = kPi * kSwR, per = 2.f * (s1 + s2);
    float s = t * per;
    const vec2 c(kSwX, kSwY);
    if (s < s1) return c + vec2(-kSwHalfStraight + s, -rr);
    s -= s1;
    if (s < s2) {
        float an = -kHalfPi + kPi * (s / s2);
        return c + vec2(kSwHalfStraight + cosf(an) * rr, sinf(an) * rr);
    }
    s -= s2;
    if (s < s1) return c + vec2(kSwHalfStraight - s, rr);
    s -= s1;
    float an = kHalfPi + kPi * (s / s2);
    return c + vec2(-kSwHalfStraight + cosf(an) * rr, sinf(an) * rr);
}

// SK_SPEEDWAY: variant 0: track, walls, catch fence, lights, flags (the whole oval); 1: infield (pit road, boxes, garages,
// scoreboard, haulers); 2: car park, entrance arch, ticket booths; z: ground
void genSpeedway(const SiteElem& e, G& g) {
    const bool detail = g.detail;
    const float z = e.z;
    const vec2 X(1, 0), Y(0, 1);
    if (e.variant == 0) {
        // asphalt oval with white edge lines, the checkered start / finish line at the middle of the front stretch
        int seg = detail ? 120 : 48;
        const float ri = swIn(), ro = swOut();
        for (int k = 0; k < seg; k++) {
            float t0 = (float)k / seg, t1 = (float)(k + 1) / seg;
            vec2 i0 = ovalAt(t0, ri), i1 = ovalAt(t1, ri), o0 = ovalAt(t0, ro), o1 = ovalAt(t1, ro);
            if (!g.owns((i0 + o1) * 0.5f)) continue;
            quad(g, *g.m, V3(i0, z + 0.08f), V3(i1, z + 0.08f), V3(o1, z + 0.08f), V3(o0, z + 0.08f), rgb(0.85f), M(MAT_ASPHALT), vec3(0, 0, 1));
            // apron (flat, lighter) inside the track
            vec2 a0 = ovalAt(t0, ri - 5.f), a1 = ovalAt(t1, ri - 5.f);
            quad(g, *g.m, V3(a0, z + 0.07f), V3(a1, z + 0.07f), V3(i1, z + 0.07f), V3(i0, z + 0.07f), rgb(0.95f), M(MAT_ASPHALT_OLD), vec3(0, 0, 1));
            if (detail) {
                paintLine(g, ovalAt(t0, ri + 0.4f), ovalAt(t1, ri + 0.4f), 0.2f, z + 0.09f, kWhiteC, M(MAT_PAINT_WHITE));
                paintLine(g, ovalAt(t0, ro - 0.4f), ovalAt(t1, ro - 0.4f), 0.2f, z + 0.09f, kWhiteC, M(MAT_PAINT_WHITE));
                if ((k & 1) == 0) paintLine(g, ovalAt(t0, ri - 4.5f), ovalAt(t1, ri - 4.5f), 0.25f, z + 0.08f, rgb(0.95f, 0.8f, 0.1f), M(MAT_PAINT_YELLOW));
            }
            // outer wall (SAFER barrier) and catch fence on it
            vec2 w0 = ovalAt(t0, ro + 0.5f), w1 = ovalAt(t1, ro + 0.5f);
            vec2 d = normalize(w1 - w0);
            boxY(g, V3((w0 + w1) * 0.5f, z + 0.6f), d, vec3(length(w1 - w0) * 0.5f + 0.02f, 0.3f, 0.6f), rgb(0.95f), M(MAT_CONCRETE_PANEL));
            collide(g, V3((w0 + w1) * 0.5f, z + 2.5f), d, vec3(length(w1 - w0) * 0.5f, 0.3f, 2.5f));
            if (detail) {
                vec2 n = perp(d);
                boxY(g, V3(w0, z + 3.1f), d, vec3(0.06f, 0.06f, 1.9f), rgb(0.5f), M(MAT_METAL_PAINTED));
                for (float zz : {1.4f, 3.f, 4.9f}) panel2(g, V3(w0, z + zz - 0.02f), V3(w1, z + zz - 0.02f), V3(w1, z + zz + 0.02f), V3(w0, z + zz + 0.02f), rgb(0.5f), M(MAT_METAL_PAINTED));
                beam(g, V3(w0, z + 4.9f), V3(w0 - n * 0.9f, z + 5.6f), 0.05f, 0.05f, rgb(0.5f), M(MAT_METAL_PAINTED));
            }
        }
        // start / finish: a checkered band across the track, the flag stand over it on the outside wall
        vec2 sf = vec2(kSwX, kSwY - kSwR);
        if (g.owns(sf) && detail)
            for (int i = 0; i < 16; i++)
                for (int j = 0; j < 2; j++) {
                    if ((i + j) & 1) continue;
                    vec2 c = vec2(kSwX - 0.5f + j * 1.f, kSwY - ro + 0.5f + i * 1.f);
                    paintRect(g, c, X, 0.5f, 0.5f, z + 0.095f, rgb(0.05f), M(MAT_PAINT_WHITE));
                }
        vec2 fs(kSwX, kSwY - ro - 1.5f);
        if (g.owns(fs)) {
            for (int s = -1; s <= 1; s += 2) cyl(g, V3(fs + X * (s * 1.6f) - Y * 1.f, z), 0.15f, 0.15f, 6.5f, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
            boxY(g, V3(fs, z + 6.6f), X, vec3(2.f, 1.6f, 0.1f), rgb(0.9f), M(MAT_METAL_PAINTED), true);
            boxY(g, V3(fs, z + 8.2f), X, vec3(2.f, 1.6f, 0.08f), rgb(0.1f, 0.2f, 0.5f), M(MAT_METAL_PAINTED), true);
            ironRailing(g, fs + X * 2.f + Y * 1.6f, fs - X * 2.f + Y * 1.6f, z + 6.7f, 1.f, 0.8f, rgb(0.9f), false, false);
            for (int s = -1; s <= 1; s += 2) cyl(g, V3(fs + X * (s * 1.9f) + Y * 1.5f, z + 6.7f), 0.05f, 0.05f, 1.5f, 5, rgb(0.9f), M(MAT_METAL_PAINTED), false);
            // the flags in their holders: green, yellow, white, checkered
            const vec3 fc[4] = {vec3(0.1f, 0.7f, 0.2f), vec3(1.f, 0.85f, 0.1f), vec3(0.97f), vec3(0.1f)};
            for (int k = 0; k < 4; k++) {
                vec2 fp = fs - X * 1.5f + X * (k * 1.f) + Y * 1.3f;
                beam(g, V3(fp, z + 6.8f), V3(fp, z + 8.f), 0.03f, 0.03f, rgb(0.7f), M(MAT_WOOD));
                panel2(g, V3(fp, z + 7.5f), V3(fp + X * 0.6f, z + 7.5f), V3(fp + X * 0.6f, z + 8.f), V3(fp, z + 8.f), rgbv(fc[k]), M(MAT_FABRIC));
            }
            collide(g, V3(fs, z + 6.6f), X, vec3(2.f, 1.6f, 0.2f));
        }
        // light towers round the outside for night racing, flags along the back stretch
        for (int k = 0; k < 16; k++) {
            float t = (k + 0.5f) / 16.f;
            vec2 p = ovalAt(t, ro + 9.f);
            if (!g.owns(p)) continue;
            vec2 nrm = normalize(p - ovalAt(t, ro));
            cyl(g, V3(p, z), 0.35f, 0.22f, 26.f, detail ? 8 : 5, rgb(0.6f, 0.62f, 0.65f), M(MAT_METAL_PAINTED), false);
            boxY(g, V3(p - nrm * 0.6f, z + 26.4f), rightOf(nrm), vec3(3.f, 0.5f, 1.f), rgb(0.3f), M(MAT_METAL_PAINTED), true);
            quad(g, *g.m, V3(p - nrm * 1.11f + rightOf(nrm) * 2.8f, z + 25.6f), V3(p - nrm * 1.11f - rightOf(nrm) * 2.8f, z + 25.6f),
                 V3(p - nrm * 1.11f - rightOf(nrm) * 2.8f, z + 27.2f), V3(p - nrm * 1.11f + rightOf(nrm) * 2.8f, z + 27.2f), rgb(1.f, 0.96f, 0.88f, 0.7f), emMat(EA_NIGHT),
                 V3(-nrm, 0.f));
            light(g, V3(p - nrm * 2.f, z + 26.f), vec3(1.f, 0.95f, 0.85f) * 40000.f, 110.f, 1, normalize(V3(-nrm, -0.45f)), 0.35f);
            collide(g, V3(p, z + 13.f), X, vec3(0.35f, 0.35f, 13.f));
        }
        if (detail)
            for (int k = 0; k < 13; k++) {
                vec2 p = vec2(kSwX - 120.f + k * 20.f, kSwY + ro + 2.2f);
                if (!g.owns(p)) continue;
                const vec3 cols[5] = {vec3(0.9f, 0.1f, 0.1f), vec3(0.1f, 0.3f, 0.85f), vec3(1.f, 0.8f, 0.1f), vec3(0.1f, 0.6f, 0.25f), vec3(0.97f)};
                flagPole(g, p, z, 9.f, vec2(-0.92f, -0.3f), cols[k % 5], cols[(k + 2) % 5]);
            }
        return;
    }
    if (e.variant == 1) {
        // pit road inside the front stretch: pit wall, numbered boxes, the garages behind, the scoreboard pylon, haulers
        const float ri = swIn();
        const float pv0 = kSwY - ri + 5.f, pv1 = pv0 + 12.f;   // pit road between the apron and the boxes
        auto owned = [&](vec2 p) { return g.owns(p); };
        for (float x = kSwStandX0; x < kSwStandX1; x += 20.f) {
            vec2 c(x + 10.f, (pv0 + pv1) * 0.5f);
            if (!owned(c)) continue;
            polyFlat(g, *g.m, rectPoly(c, X, 10.f, (pv1 - pv0) * 0.5f), z + 0.075f, rgb(0.8f), M(MAT_ASPHALT_OLD));
            boxY(g, V3(vec2(x + 10.f, pv0 - 0.3f), z + 0.55f), X, vec3(10.f, 0.3f, 0.55f), rgb(0.95f), M(MAT_CONCRETE_PANEL));   // pit wall
            collide(g, V3(vec2(x + 10.f, pv0 - 0.3f), z + 0.55f), X, vec3(10.f, 0.3f, 0.55f));
        }
        for (int k = 0; k < 10; k++) {
            float x = kSwStandX0 + 10.f + k * 20.f;
            vec2 bc(x, pv1 + 3.f);
            if (!owned(bc)) continue;
            // box: painted rectangle, number, tool cart, crew stand on the pit wall
            if (detail) {
                paintRect(g, bc, X, 8.f, 3.f, z + 0.08f, rgb(0.95f, 0.8f, 0.1f), M(MAT_PAINT_YELLOW));
                char num[8];
                snprintf(num, sizeof(num), "%d", k * 7 % 90 + 3);
                strokeText(g, *g.d, num, V3(bc - X * 0.9f - Y * 1.6f, z + 0.09f), V3(X, 0.f), V3(Y, 0.f), 1.6f, 0.2f, rgb(0.95f), M(MAT_PAINT_WHITE), 0.f, 0.3f);
            }
            boxY(g, V3(bc + X * 5.f + Y * 1.5f, z + 0.5f), X, vec3(0.6f, 0.35f, 0.5f), rgb(0.85f, 0.1f, 0.1f), M(MAT_METAL_PAINTED));
            collide(g, V3(bc + X * 5.f + Y * 1.5f, z + 0.5f), X, vec3(0.6f, 0.35f, 0.5f));
            vec2 cs(x, pv0 - 0.3f);
            boxY(g, V3(cs, z + 1.6f), X, vec3(1.2f, 0.9f, 0.08f), rgb(0.3f), M(MAT_METAL_PAINTED), true);
            boxY(g, V3(cs, z + 2.8f), X, vec3(1.3f, 1.f, 0.05f), rgb(0.9f, 0.9f, 0.9f), M(MAT_FABRIC), true);
        }
        // the garage row behind the boxes: roll-up doors under a long flat roof
        const float gv = pv1 + 10.f;
        for (float x = kSwStandX0; x < kSwStandX1; x += 25.f) {
            vec2 c(x + 12.5f, gv + 5.f);
            if (!owned(c)) continue;
            boxY(g, V3(c, z + 2.4f), X, vec3(12.5f, 5.f, 2.6f), rgb(0.88f, 0.88f, 0.86f), M(MAT_CORRUGATED));
            boxY(g, V3(c, z + 5.1f), X, vec3(12.8f, 5.3f, 0.1f), rgb(0.6f), M(MAT_ROOF_METAL), true);
            collide(g, V3(c, z + 2.5f), X, vec3(12.5f, 5.f, 2.5f));
            for (int d = 0; d < 4; d++) {
                vec2 dp = vec2(x + 3.4f + d * 6.1f, gv - 0.02f);
                quad(g, *g.m, V3(dp - X * 2.5f, z), V3(dp + X * 2.5f, z), V3(dp + X * 2.5f, z + 3.6f), V3(dp - X * 2.5f, z + 3.6f), rgb(0.4f, 0.42f, 0.46f), M(MAT_CORRUGATED),
                     vec3(0, -1, 0));
            }
        }
        // haulers parked behind the garages
        for (int k = 0; k < 5; k++) {
            vec2 hc(kSwStandX0 + 20.f + k * 40.f, gv + 20.f);
            if (!owned(hc)) continue;
            vec3 col = hsvToRgb(hashToFloat(hash32(0x4A11u + (u32)k)), 0.7f, 0.85f);
            boxY(g, V3(hc, z + 2.2f), X, vec3(8.f, 1.3f, 1.6f), rgbv(col), M(MAT_CARPAINT));
            boxY(g, V3(hc + X * 9.4f, z + 1.9f), X, vec3(1.4f, 1.25f, 1.3f), rgbv(col * 0.8f), M(MAT_CARPAINT));
            for (int w = 0; w < 3; w++)
                for (int s = -1; s <= 1; s += 2) boxY(g, V3(hc + X * (-6.f + w * 5.6f) + Y * (s * 1.1f), z + 0.5f), X, vec3(0.5f, 0.15f, 0.5f), rgb(0.06f), M(MAT_TIRE));
            collide(g, V3(hc + X * 1.f, z + 1.9f), X, vec3(9.5f, 1.3f, 1.9f));
        }
        // scoreboard pylon in the middle of the infield: running order, lit at night
        vec2 sb(kSwX, kSwY + 12.f);
        if (owned(sb)) {
            boxY(g, V3(sb, z + 9.f), X, vec3(0.8f, 0.8f, 9.f), rgb(0.3f), M(MAT_METAL_PAINTED));
            boxY(g, V3(sb, z + 20.f), X, vec3(1.3f, 1.3f, 5.f), rgb(0.12f), M(MAT_METAL_PAINTED));
            for (int s = -1; s <= 1; s += 2)
                for (int k = 0; k < 8; k++) {
                    char num[4];
                    snprintf(num, sizeof(num), "%d", (k * 13 + 5) % 90 + 1);
                    vec2 f = Y * (float)s, rd = -rightOf(f);   // reading left to right for someone facing this side
                    vec3 o = V3(sb + f * 1.31f - rd * 0.7f, z + 23.9f - k * 1.15f);
                    strokeText(g, *g.m, num, o, V3(rd, 0.f), vec3(0, 0, 1), 0.8f, 0.12f, rgb(1.f, 0.85f, 0.2f, 0.8f), emMat(EA_NIGHT), 0.f, 0.3f);
                }
            collide(g, V3(sb, z + 12.f), X, vec3(1.3f, 1.3f, 12.f));
        }
        // infield grass
        drapeRect(g, vec2(kSwX, kSwY + 8.f), X, kSwHalfStraight, swIn() - 22.f, 0.05f, rgb(0.55f, 0.72f, 0.4f), M(MAT_GRASS), detail ? 10.f : 30.f, z);
        (void)owned;
        return;
    }
    // variant 2: grandstand on the front stretch, race control tower, car park, entrance arch and ticket booths
    const float ro = swOut();
    const float sv0 = kSwY - ro - 6.f;   // front of the stands (the track side)
    vec2 sc((kSwStandX0 + kSwStandX1) * 0.5f, sv0);
    for (float x = kSwStandX0; x < kSwStandX1; x += 40.f) {
        vec2 c(x + 20.f, sv0);
        if (!g.owns(c)) continue;
        bleachers(g, c, X, Y, z, 40.f, 24, 0.9f, 0.55f, rgb(0.2f, 0.35f, 0.7f), rgb(0.62f, 0.64f, 0.67f));
        // the back wall of the stand with the sponsors' boards (plain colours)
        float zt = z + 0.45f + 23.f * 0.55f;
        boxY(g, V3(c - Y * (23.f * 0.9f + 1.2f), (z + zt + 1.5f) * 0.5f), X, vec3(20.f, 0.2f, (zt + 1.5f - z) * 0.5f), rgb(0.7f, 0.72f, 0.75f), M(MAT_METAL_PAINTED));
        collide(g, V3(c - Y * (23.f * 0.9f + 1.2f), (z + zt + 1.5f) * 0.5f), X, vec3(20.f, 0.2f, (zt + 1.5f - z) * 0.5f));
    }
    // race control tower on top of the stand, in the middle
    vec2 tc(kSwX, sv0 - 23.f * 0.9f - 3.f);
    if (g.owns(tc)) {
        const float tz = z + 0.45f + 23.f * 0.55f + 1.5f;
        for (int k = 0; k < 4; k++)
            boxY(g, V3(tc + X * ((k & 1) ? 7.f : -7.f) + Y * ((k & 2) ? 2.5f : -2.5f), (z + tz) * 0.5f), X, vec3(0.3f, 0.3f, (tz - z) * 0.5f), rgb(0.6f),
                 M(MAT_METAL_PAINTED));
        boxY(g, V3(tc, tz + 2.f), X, vec3(8.f, 3.f, 2.f), rgb(0.92f), M(MAT_CONCRETE_PANEL));
        quad(g, *g.m, V3(tc + Y * 3.01f - X * 7.5f, tz + 0.8f), V3(tc + Y * 3.01f + X * 7.5f, tz + 0.8f), V3(tc + Y * 3.01f + X * 7.5f, tz + 3.4f),
             V3(tc + Y * 3.01f - X * 7.5f, tz + 3.4f), rgb(0.95f, 0.9f, 0.7f, 0.06f), emMat(EA_NIGHT), vec3(0, 1, 0));
        boxY(g, V3(tc, tz + 4.15f), X, vec3(8.4f, 3.4f, 0.15f), rgb(0.35f), M(MAT_METAL_PAINTED), true);
        const char* nm = "PALMERA SPEEDWAY";
        plainLetters(g, nm, centredOrigin(nm, V3(tc - Y * 3.02f, tz + 1.2f), vec3(1, 0, 0), 0.9f), vec3(1, 0, 0), vec3(0, 0, 1), 0.9f,
                     rgb(0.95f, 0.1f, 0.1f, 0.6f), emMat(EA_NIGHT), 0.04f, 0.15f);
        collide(g, V3(tc, tz + 2.f), X, vec3(8.f, 3.f, 2.f));
    }
    // car park south of the stands, entrance arch over the road from County Road 735, ticket booths
    const float py0 = kSwRoadY + 22.f, py1 = sv0 - 23.f * 0.9f - 8.f;
    for (float y = py0 + 8.f; y + 3.f <= py1; y += 16.f)
        for (int s = -1; s <= 1; s += 2) {
            float x0 = s < 0 ? kSwX - 150.f : kSwX + 8.f, x1 = s < 0 ? kSwX - 8.f : kSwX + 150.f;
            int stalls = (int)((x1 - x0) / 2.6f);
            vec2 c((x0 + x1) * 0.5f, y);
            stallRow(g, c - Y * 3.f, X, -Y, z + 0.03f, stalls, 5.f, 0.35f, hash32(e.seed + (u32)y * 5u + (u32)(s + 3)));
            if (y + 8.f <= py1) stallRow(g, c + Y * 3.f, X, Y, z + 0.03f, stalls, 5.f, 0.35f, hash32(e.seed + (u32)y * 9u + (u32)(s + 5)));
        }
    vec2 ac(kSwX, kSwRoadY + 18.f);
    if (g.owns(ac)) {
        for (int s = -1; s <= 1; s += 2) {
            vec2 p = ac + X * (s * 7.5f);
            boxY(g, V3(p, z + 3.5f), X, vec3(0.5f, 0.5f, 3.5f), rgb(0.85f, 0.1f, 0.1f), M(MAT_METAL_PAINTED));
            collide(g, V3(p, z + 3.5f), X, vec3(0.5f, 0.5f, 3.5f));
            // ticket booth
            vec2 bp = ac + X * (s * 11.f) + Y * 4.f;
            boxY(g, V3(bp, z + 1.3f), X, vec3(1.3f, 1.1f, 1.3f), rgb(0.95f), M(MAT_METAL_PAINTED));
            boxY(g, V3(bp, z + 2.7f), X, vec3(1.6f, 1.4f, 0.1f), rgb(0.85f, 0.1f, 0.1f), M(MAT_METAL_PAINTED), true);
            collide(g, V3(bp, z + 1.3f), X, vec3(1.3f, 1.1f, 1.3f));
        }
        boxY(g, V3(ac, z + 7.6f), X, vec3(8.2f, 0.4f, 0.8f), rgb(0.1f), M(MAT_METAL_PAINTED));
        const char* nm = "PALMERA SPEEDWAY";
        plainLetters(g, nm, centredOrigin(nm, V3(ac - Y * 0.42f, z + 7.1f), vec3(1, 0, 0), 0.95f), vec3(1, 0, 0), vec3(0, 0, 1), 0.95f, rgb(1.f, 0.85f, 0.2f, 0.7f),
                     emMat(EA_NIGHT), 0.f, 0.15f);
        // checkered band under the name
        if (detail)
            for (int k = 0; k < 16; k++)
                for (int j = 0; j < 2; j++) {
                    if ((k + j) & 1) continue;
                    vec2 c = ac - Y * 0.42f + X * (-7.5f + k * 1.f);
                    quad(g, *g.m, V3(c, z + 6.85f - j * 0.4f), V3(c + X * 1.f, z + 6.85f - j * 0.4f), V3(c + X * 1.f, z + 7.05f - j * 0.4f), V3(c, z + 7.05f - j * 0.4f), rgb(0.95f),
                         M(MAT_PAINT_WHITE), vec3(0, -1, 0));
                }
    }
}

// ------------------------------------------------------------------------------------------------ layout
// dry farmland at the site, its mean height
bool siteGround(WorldMap& map, vec2 c, float hx, float hy, float* zm) {
    float zs = 0.f;
    int n = 0;
    for (float y = c.y - hy; y <= c.y + hy; y += 20.f)
        for (float x = c.x - hx; x <= c.x + hx; x += 20.f) {
            Region r = map.regionAt(x, y);
            if (map.isWater(x, y) || (r != REG_FARMLAND && r != REG_LAKE_TOWN)) return false;
            zs += map.heightAt(x, y);
            n++;
        }
    *zm = roundf(zs / Max(1, n) * 10.f) / 10.f;
    return true;
}

SiteBuildingReq blockReq(const Block& b, float z, int ei, u8 region) {
    SiteBuildingReq q;
    q.c = vec2((b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f);
    q.ax = vec2(1, 0);
    q.hx = (b.x1 - b.x0) * 0.5f;
    q.hy = (b.y1 - b.y0) * 0.5f;
    q.style = BS_MIDRISE;
    q.roof = ROOF_FLAT;
    q.floors = (u16)b.floors;
    q.front = vec2(0, b.y0 > kPrY ? -1.f : (b.kind == 3 ? -1.f : 1.f));
    q.baseZ = z - 0.15f;
    q.region = region;
    q.siteElem = ei;
    return q;
}

void layoutPrison(SiteSet& S, WorldMap& map) {
    const vec2 c(kPrX, (kPrRoadY + 12.f + kPrY + kPrHY + kPrFence + 10.f) * 0.5f);
    const float hx = kPrHX + kPrFence + 18.f, hy = (kPrY + kPrHY + kPrFence + 10.f - kPrRoadY - 12.f) * 0.5f;
    float zg;
    if (!siteGround(map, c, hx, hy, &zg)) {
        LOG("Places: prison site at (%.0f, %.0f) is not dry farmland, prison left out", c.x, c.y);
        return;
    }
    map.flattenRect(c, vec2(1, 0), hx, hy, zg, 20.f);
    const float z = zg;
    S.roadBlocks.push_back({c, vec2(1, 0), hx, hy});
    S.lotBlocks.push_back({c, vec2(1, 0), hx + 5.f, hy + 5.f});
    S.vegBlocks.push_back({c, vec2(1, 0), hx, hy});
    // the access road from County Road 755 up to the apron in front of the sally port, the apron
    {
        SiteRoad r;
        r.pts = {vec2(kPrX, kPrRoadY + 2.f), vec2(kPrX, kPrRoadY + 60.f), vec2(kPrX, kPrGateY0 - 22.f)};
        r.cls = RC_STREET;
        r.flags = RF_NOSIDEWALK;
        r.layer = 0;
        r.name = "Penitentiary Road";
        S.roads.push_back(r);
        Pad pd;
        pd.c = vec2(kPrX, (kPrGateY0 - 22.f + kPrGateY0 + 2.f) * 0.5f);
        pd.ax = vec2(1, 0);
        pd.hx = 12.f;
        pd.hy = (24.f) * 0.5f;
        pd.z = z + 0.25f;
        pd.slope = 0.f;
        pd.kind = PAD_SERVICE;
        pd.drawn = 1;
        pd.skirt = 1;
        pd.flags = 0;
        pd.color = 0xffffffffu;
        S.pads.push_back(pd);
        // car parks either side of the road
        for (int s = -1; s <= 1; s += 2) {
            Pad pk;
            float x0 = s < 0 ? 255.f : 365.f, x1 = s < 0 ? 335.f : 445.f, y0 = 8845.f, y1 = s < 0 ? 8915.f : 8900.f;
            pk.c = vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
            pk.ax = vec2(1, 0);
            pk.hx = (x1 - x0) * 0.5f;
            pk.hy = (y1 - y0) * 0.5f;
            pk.z = z + 0.1f;
            pk.slope = 0.f;
            pk.kind = PAD_PARKING;
            pk.drawn = 1;
            pk.skirt = 1;
            pk.flags = 0;
            pk.color = 0xffffffffu;
            S.pads.push_back(pk);
            Pad ln = pk;   // lane from the road into the lot
            ln.c = vec2(s < 0 ? 339.f : 361.f, (y0 + y1) * 0.5f);
            ln.hx = s < 0 ? 4.f : 4.f;
            ln.hy = 4.f;
            S.pads.push_back(ln);
        }
    }
    const int placeIdx = (int)S.places.size();
    {
        NamedPlace np;
        np.name = "Palmera State Penitentiary";
        np.kind = PK_PRISON;
        np.pos = vec2(kPrX, kPrY);
        np.door = vec2(kPrX, kPrGateY0 - 8.f);
        np.radius = 170.f;
        S.places.push_back(np);
    }
    const u8 region = (u8)map.regionAt(kPrX, kPrY);
    // walls with the towers at their ends, the gap for the sally port in the south wall
    const vec2 cw[4] = {vec2(kPrX - kPrHX, kPrY - kPrHY), vec2(kPrX + kPrHX, kPrY - kPrHY), vec2(kPrX + kPrHX, kPrY + kPrHY), vec2(kPrX - kPrHX, kPrY + kPrHY)};
    for (int k = 0; k < 4; k++) {
        vec2 a = cw[k], b = cw[(k + 1) & 3];
        vec2 d = normalize(b - a);
        vec2 inw = perp(d);   // walls run counter-clockwise: the inside is on the left
        // east and west walls have a tower half way: split them there
        std::vector<vec2> pts = {a};
        if (k == 1 || k == 3) pts.push_back((a + b) * 0.5f);
        if (k == 2) pts.push_back((a + b) * 0.5f);
        pts.push_back(b);
        for (size_t j = 0; j + 1 < pts.size(); j++) {
            SiteElem e;
            e.kind = SK_PRISON_WALL;
            e.seed = hash32(0x9A11u + (u32)k * 16u + (u32)j);
            e.a = pts[j];
            e.b = pts[j + 1];
            e.ax = d;
            e.c = (e.a + e.b) * 0.5f;
            e.hx = length(e.b - e.a) * 0.5f + kPrFence;
            e.hy = kPrFence + 2.f;
            e.z = z;
            e.h = 8.f;
            e.p[0] = inw.x;
            e.p[1] = inw.y;
            e.p[2] = (k == 0) ? kPrHX : -1.f;
            e.p[3] = (k == 0) ? kPrLaneHW + 8.f : 0.f;
            e.p[4] = j == 0 ? 1.f : 0.f;                   // the wall's first piece starts at a corner
            e.p[5] = j + 2 == pts.size() ? 1.f : 0.f;      // its last piece ends at one
            S.elems.push_back(e);
        }
    }
    // towers (on the grounds element's list: one element each so they stream with their cell)
    for (size_t k = 0; k < sizeof(kTowers) / sizeof(kTowers[0]); k++) {
        SiteElem e;
        e.kind = SK_PRISON;
        e.variant = 6;
        e.seed = 0x7033u + (u32)k;
        e.c = kTowers[k];
        e.ax = normalize(kTowers[k] - vec2(kPrX, kPrY));
        e.hx = e.hy = 3.f;
        e.z = z;
        e.h = 15.f;
        S.elems.push_back(e);
        S.anchors.push_back({vec3(kTowers[k], z + 10.05f), e.ax, PA_GUARD, (u8)placeIdx, (u16)(1 + k)});
    }
    // the buildings
    const char* blockNames[] = {"BLOCK A", "BLOCK B", "BLOCK C", "BLOCK D", "DINING HALL", "INFIRMARY", "ADMINISTRATION"};
    for (size_t k = 0; k < sizeof(kPrBlocks) / sizeof(kPrBlocks[0]); k++) {
        const Block& b = kPrBlocks[k];
        SiteElem e;
        e.kind = SK_PRISON;
        e.variant = b.kind;
        e.seed = hash32(0xB10Cu + (u32)k);
        e.c = vec2((b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f);
        SiteBuildingReq q = blockReq(b, z, (int)S.elems.size(), region);
        e.ax = q.front;
        // footprint in the element's frame: hx across the front, hy deep
        e.hx = q.hx;
        e.hy = q.hy;
        e.z = z;
        e.p[0] = (float)b.floors;
        e.p[1] = b.kind == 1 ? 6.f : 4.f;
        e.p[2] = 3.4f;
        e.p[6] = -1.f;
        e.text = blockNames[k];
        e.h = e.p[1] + (b.floors - 1) * 3.4f + 2.f;
        S.elems.push_back(e);
        S.buildingReqs.push_back(q);
    }
    // sally port and the grounds
    {
        SiteElem e;
        e.kind = SK_PRISON;
        e.variant = 4;
        e.seed = 0x5A11u;
        e.c = vec2(kPrX, kPrY - kPrHY);
        e.ax = vec2(0, -1);
        e.hx = e.hy = 24.f;
        e.z = z;
        e.h = 9.f;
        S.elems.push_back(e);
        SiteElem gr;
        gr.kind = SK_PRISON;
        gr.variant = 5;
        gr.seed = 0x6A11u;
        gr.c = vec2(kPrX, (kPrRoadY + kPrY + kPrHY) * 0.5f);
        gr.ax = vec2(1, 0);
        gr.hx = kPrHX + 2.f;
        gr.hy = (kPrY + kPrHY - kPrRoadY) * 0.5f;
        gr.z = z;
        gr.h = 6.f;
        S.elems.push_back(gr);
    }
    // floodlight masts over the compound
    const vec2 masts[] = {vec2(290.f, 9030.f), vec2(410.f, 9030.f), vec2(290.f, 9110.f), vec2(410.f, 9110.f), vec2(350.f, 9160.f), vec2(350.f, 8990.f)};
    for (size_t k = 0; k < sizeof(masts) / sizeof(masts[0]); k++) {
        SiteElem m;
        m.kind = SK_FLOOD_MAST;
        m.seed = 0xF100u + (u32)k;
        m.c = masts[k];
        m.hx = m.hy = 2.f;
        m.z = z;
        m.h = 24.f;
        S.elems.push_back(m);
    }
    // people: guards at the sally port and the yard gate, inmates running the yard track, groups, the weight pile, bleachers
    u16 group = 20;
    S.anchors.push_back({vec3(kPrX + kPrLaneHW + 2.2f, kPrGateY0 - 7.f - 1.8f, z + 0.3f), vec2(-1, 0), PA_GUARD, (u8)placeIdx, group++});
    S.anchors.push_back({vec3(kPrX - kPrLaneHW - 1.5f, kPrY - kPrHY - 16.f, z + 0.05f), vec2(1, 0), PA_GUARD, (u8)placeIdx, group++});
    S.anchors.push_back({vec3((kYardX0 + kYardX1) * 0.5f + 4.f, kYardY0 - 2.f, z + 0.05f), vec2(0, 1), PA_GUARD, (u8)placeIdx, group++});
    const vec2 yc((kYardX0 + kYardX1) * 0.5f, (kYardY0 + kYardY1) * 0.5f);
    const float trA = (kYardX1 - kYardX0) * 0.5f - 6.f, trB = (kYardY1 - kYardY0) * 0.5f - 6.f;
    for (int k = 0; k < 12; k++) {
        float an = kTwoPi * k / 12.f;
        vec2 p = yc + vec2(cosf(an) * trA, sinf(an) * trB);
        vec2 dir = normalize(vec2(-sinf(an) * trA, cosf(an) * trB));
        S.anchors.push_back({vec3(p, z + 0.07f), dir, PA_EXERCISE, (u8)placeIdx, 60});
    }
    for (int gi = 0; gi < 4; gi++) {
        vec2 gc = yc + vec2(gi & 1 ? 12.f : -12.f, gi & 2 ? 22.f : -2.f);
        for (int s = 0; s < 3; s++) {
            float an = kTwoPi * s / 3.f + gi;
            vec2 d(cosf(an), sinf(an));
            S.anchors.push_back({vec3(gc + d * 0.9f, z + 0.05f), -d, PA_STAND, (u8)placeIdx, group});
        }
        group++;
    }
    for (int k = 0; k < 2; k++) S.anchors.push_back({vec3(yc + vec2(-16.f - 3.f + k * 3.f, -14.f + 1.2f), z + 0.1f), vec2(0, -1), PA_WORK, (u8)placeIdx, group++});
    for (int k = 0; k < 6; k++) S.anchors.push_back({vec3(yc + vec2(16.f - 5.f + k * 2.f, -14.f - (k % 4) * 0.8f), z + (k % 4) * 0.42f), vec2(0, 1), PA_SIT, (u8)placeIdx, group++});
    LOG("Places: Palmera State Penitentiary at (%.0f, %.0f), ground %.1f", kPrX, kPrY, zg);
}

void layoutSpeedway(SiteSet& S, WorldMap& map) {
    const float ro = swOut();
    const vec2 c(kSwX, (kSwRoadY + 12.f + kSwY + ro + 14.f) * 0.5f);
    const float hx = kSwHalfStraight + ro + 16.f, hy = (kSwY + ro + 14.f - kSwRoadY - 12.f) * 0.5f;
    float zg;
    if (!siteGround(map, c, hx, hy, &zg)) {
        LOG("Places: speedway site at (%.0f, %.0f) is not dry farmland, speedway left out", c.x, c.y);
        return;
    }
    map.flattenRect(c, vec2(1, 0), hx, hy, zg, 20.f);
    const float z = zg;
    S.roadBlocks.push_back({c, vec2(1, 0), hx, hy});
    S.lotBlocks.push_back({c, vec2(1, 0), hx + 5.f, hy + 5.f});
    S.vegBlocks.push_back({c, vec2(1, 0), hx, hy});
    // access road from County Road 735 through the entrance arch to the car park, the car park
    {
        SiteRoad r;
        r.pts = {vec2(kSwX, kSwRoadY + 2.f), vec2(kSwX, kSwRoadY + 40.f)};
        r.cls = RC_STREET;
        r.flags = RF_NOSIDEWALK;
        r.layer = 0;
        r.name = "Speedway Drive";
        S.roads.push_back(r);
        Pad pk;
        const float py0 = kSwRoadY + 22.f, py1 = kSwY - ro - 6.f - 23.f * 0.9f - 8.f;
        pk.c = vec2(kSwX, (py0 + py1) * 0.5f + 2.f);
        pk.ax = vec2(1, 0);
        pk.hx = 152.f;
        pk.hy = (py1 - py0) * 0.5f + 2.f;
        pk.z = z + 0.2f;
        pk.slope = 0.f;
        pk.kind = PAD_PARKING;
        pk.drawn = 1;
        pk.skirt = 1;
        pk.flags = 0;
        pk.color = 0xffffffffu;
        S.pads.push_back(pk);
    }
    const int placeIdx = (int)S.places.size();
    {
        NamedPlace np;
        np.name = "Palmera Speedway";
        np.kind = PK_SPEEDWAY;
        np.pos = vec2(kSwX, kSwY);
        np.door = vec2(kSwX, kSwRoadY + 24.f);
        np.radius = 230.f;
        S.places.push_back(np);
    }
    for (int v = 0; v < 3; v++) {
        SiteElem e;
        e.kind = SK_SPEEDWAY;
        e.variant = (u16)v;
        e.seed = 0x5EEDu + (u32)v;
        e.c = v == 2 ? vec2(kSwX, (kSwRoadY + kSwY - ro) * 0.5f) : vec2(kSwX, kSwY);
        e.ax = vec2(1, 0);
        e.hx = v == 2 ? 155.f : kSwHalfStraight + ro + 12.f;
        e.hy = v == 2 ? (kSwY - ro - kSwRoadY) * 0.5f + 4.f : ro + 12.f;
        e.z = z;
        e.h = v == 0 ? 28.f : 26.f;
        S.elems.push_back(e);
    }
    // people: pit crews in the boxes, the flagman, spectators in the stands, runners on the track apron
    u16 group = 1;
    const float pv1 = kSwY - swIn() + 5.f + 12.f;
    for (int k = 0; k < 10; k += 2) {
        float x = kSwStandX0 + 10.f + k * 20.f;
        for (int m = 0; m < 3; m++) S.anchors.push_back({vec3(x - 3.f + m * 2.5f, pv1 + 2.f, z + 0.1f), vec2(0, -1), PA_WORK, (u8)placeIdx, group});
        group++;
    }
    S.anchors.push_back({vec3(kSwX, kSwY - ro - 1.5f + 0.8f, z + 6.75f), vec2(0, 1), PA_WORK, (u8)placeIdx, group++});
    for (int k = 0; k < 40; k++) {
        float x = kSwStandX0 + 6.f + (k * 37 % 188);
        int row = (k * 7) % 22;
        S.anchors.push_back({vec3(x, kSwY - ro - 6.f - row * 0.9f, z + row * 0.55f), vec2(0, 1), PA_SIT, (u8)placeIdx, group++});
    }
    LOG("Places: Palmera Speedway at (%.0f, %.0f), ground %.1f", kSwX, kSwY, zg);
}

void layout(SiteSet& S, WorldMap& map) {
    layoutPrison(S, map);
    layoutSpeedway(S, map);
}

void facades(SiteSet& S, BuildingSet& bs) {
    for (size_t bi = 0; bi < bs.buildings.size(); bi++) {
        Building& b = bs.buildings[bi];
        if (b.siteElem < 0 || b.siteElem >= (int)S.elems.size()) continue;
        SiteElem& e = S.elems[b.siteElem];
        if (e.kind != SK_PRISON || e.variant > 3) continue;
        FacadeGPU& f = bs.facades[b.facade];
        f.floorH = e.p[2];
        f.groundH = e.p[1];
        f.style = 2.f;
        const bool cells = e.variant == 0;
        f.bayW = cells ? 2.2f : 3.4f;
        f.winW = cells ? 0.14f : 0.5f;
        f.winH = cells ? 0.62f : 0.5f;
        f.sillH = cells ? 1.f : 0.9f;
        f.flags = e.variant == 3 ? 16u : 0u;
        f.litFrac = cells ? 0.55f : 0.7f;
        f.wallColor = packRGBA8(0.8f, 0.79f, 0.75f, 1.f);
        f.frameColor = packRGBA8(0.3f, 0.32f, 0.34f, 1.f);
        f.glassColor = packRGBA8(0.25f, 0.3f, 0.32f, 1.f);
        f.wallLayer = (float)MAT_CONCRETE_PANEL;
        f.roomDepth = 4.f;
        b.height = e.p[1] + (e.p[0] - 1.f) * e.p[2];
        b.baseZ = e.z;
        e.p[6] = (float)bi;
        e.p[7] = (float)b.facade;
    }
}

}  // namespace outskirts

}  // namespace World
