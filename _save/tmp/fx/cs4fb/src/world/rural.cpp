// Countryside and coastal landmarks: Cayo Faro lighthouse, the SolTV 9 mast on Cypress Ridge, Palmera Sugar Mill,
// farm silos and windpumps, the Sawgrass boardwalk and observation tower, the Okahatchee boat ramp.
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace rural_mesh {

using namespace sitegeo;

// Lathe with a colour per profile segment (banded towers, stacks)
void bandedLathe(G& g, vec3 base, const std::vector<vec2>& prof, const std::vector<u32>& cols, int seg, u32 mat) {
    MeshData& m = *g.m;
    for (size_t i = 0; i + 1 < prof.size(); i++) {
        u32 b0 = (u32)m.verts.size();
        vec2 d = prof[i + 1] - prof[i];
        vec2 pn = normalize(vec2(d.y, -d.x));
        u32 c = cols[i % cols.size()];
        for (int r = 0; r < 2; r++)
            for (int k = 0; k <= seg; k++) {
                float an = kTwoPi * k / seg;
                vec3 rad(cosf(an), sinf(an), 0);
                vec2 p = prof[i + r];
                m.addVertex(base + rad * p.x + vec3(0, 0, p.y) - g.org, normalize(rad * pn.x + vec3(0, 0, pn.y)), vec3(-sinf(an), cosf(an), 0),
                            vec2((float)k / seg * kTwoPi * p.x, p.y), c, mat);
            }
        for (int k = 0; k < seg; k++) {
            u32 a = b0 + k, b = a + 1, cc = a + seg + 1, dd = cc + 1;
            m.quadIdx(a, b, dd, cc);
        }
    }
}

// ------------------------------------------------------------------------------------------------ lighthouse
void genLighthouse(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    int seg = detail ? 20 : 10;
    vec3 b(e.c, e.z);
    // octagonal stone plinth
    lathe(g, b - vec3(0, 0, 0.5f), {vec2(6.2f, 0.f), vec2(6.2f, 2.f), vec2(5.4f, 2.4f)}, 8, rgb(0.8f, 0.76f, 0.68f), M(MAT_STONE), true, kPi / 8.f);
    // banded conical tower (black / white)
    std::vector<vec2> prof;
    std::vector<u32> cols;
    int bands = 8;
    for (int k = 0; k <= bands; k++) {
        float t = (float)k / bands;
        prof.push_back(vec2(Lerp(4.4f, 2.5f, t), 2.4f + t * 33.6f));
        cols.push_back((k & 1) ? rgb(0.08f, 0.08f, 0.09f) : rgb(0.96f, 0.96f, 0.94f));
    }
    bandedLathe(g, b, prof, cols, seg, M(MAT_PLASTER));
    collide(g, b + vec3(0, 0, 18.f), e.ax, vec3(3.3f, 3.3f, 18.f));
    // gallery, lantern room, dome
    float zg = 36.f;
    lathe(g, b + vec3(0, 0, zg), {vec2(2.6f, 0.f), vec2(3.8f, 0.3f), vec2(3.8f, 0.6f), vec2(0.f, 0.6f)}, seg, rgb(0.15f), M(MAT_METAL_PAINTED), false);
    if (detail) {
        for (int k = 0; k < 16; k++) {
            float a0 = kTwoPi * k / 16, a1 = kTwoPi * (k + 1) / 16;
            rod(g, b + vec3(cosf(a0) * 3.7f, sinf(a0) * 3.7f, zg + 1.6f), b + vec3(cosf(a1) * 3.7f, sinf(a1) * 3.7f, zg + 1.6f), 0.04f, 4, rgb(0.15f), M(MAT_METAL_PAINTED));
            cyl(g, b + vec3(cosf(a0) * 3.7f, sinf(a0) * 3.7f, zg + 0.6f), 0.03f, 0.03f, 1.f, 4, rgb(0.15f), M(MAT_METAL_PAINTED), false);
        }
    }
    cyl(g, b + vec3(0, 0, zg + 0.6f), 2.f, 2.f, 3.4f, seg, rgb(0.55f, 0.7f, 0.72f), M(MAT_GLASS), false);
    if (detail)
        for (int k = 0; k < 8; k++) {
            float a = kTwoPi * k / 8;
            beam(g, b + vec3(cosf(a) * 2.02f, sinf(a) * 2.02f, zg + 0.6f), b + vec3(cosf(a) * 2.02f, sinf(a) * 2.02f, zg + 4.f), 0.08f, 0.08f, rgb(0.12f), M(MAT_METAL_PAINTED));
        }
    lathe(g, b + vec3(0, 0, zg + 4.f), {vec2(2.3f, 0.f), vec2(2.1f, 0.8f), vec2(1.2f, 2.f), vec2(0.4f, 2.5f), vec2(0.f, 2.6f)}, seg, rgb(0.12f), M(MAT_METAL_PAINTED), false);
    cyl(g, b + vec3(0, 0, zg + 6.5f), 0.05f, 0.02f, 2.f, 4, rgb(0.2f), M(MAT_METAL_BRUSHED), false);
    // the light: a bright lens that pulses like a sweeping beam, plus an actual light for the gallery
    lamp(g, b + vec3(0, 0, zg + 2.2f), detail ? 1.4f : 2.2f, vec3(1.f, 0.92f, 0.7f), 1.f, EA_PULSE, 0);
    light(g, b + vec3(0, 0, zg + 2.2f), vec3(1.f, 0.9f, 0.7f) * 30000.f, 60.f, 2);
    // keeper's path and a small sign
    if (detail) {
        const char* t = e.text.c_str();
        float th = 0.55f;
        float tw = textAdvance(t, th, 0.3f);
        vec2 sp = e.c - e.ax * 9.f;
        float z = gMap->heightAt(sp.x, sp.y);
        vec2 face = -e.ax, viewR = perp(face);
        boxY(g, vec3(sp, z + 0.9f), viewR, vec3(2.4f, 0.12f, 0.5f), rgb(0.35f, 0.25f, 0.15f), M(MAT_WOOD), true);
        strokeText(g, *g.m, t, vec3(sp + face * 0.14f - viewR * (tw * 0.5f), z + 0.7f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.08f, rgb(0.95f, 0.9f, 0.75f), M(MAT_PLASTER));
    }
}

// ------------------------------------------------------------------------------------------------ radio / TV mast
void genRadioMast(const SiteElem& e, G& g) {
    bool detail = g.detail;
    vec3 b(e.c, e.z);
    float H = e.h - 12.f;
    const float side = 2.6f;
    vec2 cr[3];
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3 + 0.3f;
        cr[k] = e.c + vec2(cosf(a), sinf(a)) * (side * 0.577f);
    }
    auto band = [&](float z) { return ((int)(z / (H / 7.f)) & 1) ? rgb(0.95f, 0.95f, 0.95f) : rgb(0.85f, 0.15f, 0.08f); };
    if (g.owns(e.c)) {
        if (detail) {
            float step = 5.f;
            for (float z = 0.f; z < H; z += step) {
                u32 c = band(z + step * 0.5f);
                for (int k = 0; k < 3; k++) {
                    vec2 p0 = cr[k], p1 = cr[(k + 1) % 3];
                    rod(g, vec3(p0, e.z + z), vec3(p0, e.z + z + step), 0.13f, 4, c, M(MAT_METAL_PAINTED));
                    rod(g, vec3(p0, e.z + z + step), vec3(p1, e.z + z + step), 0.05f, 3, c, M(MAT_METAL_PAINTED));
                    rod(g, vec3(p0, e.z + z), vec3(p1, e.z + z + step), 0.04f, 3, c, M(MAT_METAL_PAINTED));
                }
            }
        } else {
            // far: three thin faces per band
            for (int s = 0; s < 7; s++) {
                float z0 = H * s / 7.f, z1 = H * (s + 1) / 7.f;
                u32 c = band((z0 + z1) * 0.5f);
                for (int k = 0; k < 3; k++) {
                    vec2 p0 = cr[k], p1 = cr[(k + 1) % 3];
                    vec2 on = normalize(vec2(p1.y - p0.y, p0.x - p1.x));
                    if (dot(on, (p0 + p1) * 0.5f - e.c) < 0) on = -on;
                    quad(g, *g.m, vec3(p0, e.z + z0), vec3(p1, e.z + z0), vec3(p1, e.z + z1), vec3(p0, e.z + z1), c, M(MAT_METAL_PAINTED), vec3(on, 0));
                }
            }
        }
        // antenna section and beacons
        cyl(g, b + vec3(0, 0, H), 0.9f, 0.6f, 12.f, detail ? 8 : 5, rgb(0.9f), M(MAT_METAL_PAINTED), true);
        for (int k = 1; k <= 4; k++) {
            float z = H * k / 4.f;
            lamp(g, b + vec3(0, 0, z + (k == 4 ? 12.5f : 0.3f)), detail ? 0.6f : 2.5f, vec3(1.f, 0.05f, 0.02f), 1.f, k == 4 ? EA_BLINK : EA_SLOWBLINK, (u32)k * 40u);
            light(g, b + vec3(0, 0, z + (k == 4 ? 12.5f : 0.3f)), vec3(1.f, 0.08f, 0.04f) * 2000.f, 30.f, 4);
        }
        collide(g, b + vec3(0, 0, H * 0.5f), vec2(1, 0), vec3(1.5f, 1.5f, H * 0.5f));
        // transmitter building with the station call sign
        vec2 hc = e.c + vec2(14.f, -10.f);
        float hz = gMap->heightAt(hc.x, hc.y);
        prism(g, rectPoly(hc, vec2(1, 0), 7.f, 4.5f), hz - 0.5f, hz + 3.8f, rgb(0.85f, 0.83f, 0.78f), M(MAT_CONCRETE_PANEL), rgb(0.6f), M(MAT_ROOF_GRAVEL));
        collide(g, vec3(hc, hz + 1.9f), vec2(1, 0), vec3(7.f, 4.5f, 1.9f));
        if (detail) {
            const char* t = e.text.c_str();
            float th = 1.1f;
            float tw = textAdvance(t, th, 0.3f);
            strokeText(g, *g.m, t, vec3(hc + vec2(-tw * 0.5f, -4.55f), hz + 1.6f), vec3(1, 0, 0), vec3(0, 0, 1), th, 0.17f, rgb(0.1f, 0.2f, 0.5f), M(MAT_METAL_PAINTED));
            light(g, vec3(hc + vec2(0, -6.f), hz + 3.5f), vec3(1.f, 0.85f, 0.6f) * 2500.f, 14.f, 1, vec3(0, 0, -1), 0.3f);
            // satellite dishes
            for (int k = 0; k < 2; k++) {
                vec2 dc = hc + vec2(-9.f + k * 4.f, 7.f);
                float dz = gMap->heightAt(dc.x, dc.y);
                cyl(g, vec3(dc, dz), 0.15f, 0.15f, 1.6f, 5, rgb(0.6f), M(MAT_METAL_PAINTED), false);
                lathe(g, vec3(dc, dz + 1.4f), {vec2(0.f, 0.f), vec2(1.f, 0.25f), vec2(1.6f, 0.6f)}, 12, rgb(0.95f), M(MAT_METAL_PAINTED), false);
            }
        }
    }
    // guy wires and anchors: three directions, four levels (each anchor owned by its own cell)
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3 + 0.3f + kPi / 3.f;
        vec2 dir(cosf(a), sinf(a));
        vec2 anchor = e.c + dir * 85.f;
        float az = gMap->heightAt(anchor.x, anchor.y);
        if (g.owns(anchor)) {
            boxY(g, vec3(anchor, az + 0.5f), dir, vec3(2.f, 2.f, 0.9f), rgb(0.7f), M(MAT_CONCRETE));
            collide(g, vec3(anchor, az + 0.5f), dir, vec3(2.f, 2.f, 0.9f));
        }
        if (!g.owns(e.c)) continue;
        for (int l = 1; l <= 4; l++) {
            float z = H * l / 4.f - 2.f;
            rod(g, vec3(e.c + dir * 1.f, e.z + z), vec3(anchor, az + 1.f), detail ? 0.035f : 0.08f, 3, rgb(0.35f), M(MAT_METAL_BRUSHED));
        }
    }
}

// ------------------------------------------------------------------------------------------------ sugar mill
void genSugarMill(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 X = e.ax, Y = perp(e.ax);
    float z = e.z;
    auto P = [&](float x, float y) { return e.c + X * x + Y * y; };
    u32 rust = rgb(0.62f, 0.3f, 0.22f), metal = rgb(0.75f, 0.76f, 0.78f), brick = rgb(1.f, 1.f, 1.f);
    // boiler house (brick) + mill house (corrugated, sawtooth roof)
    std::vector<vec2> boiler = rectPoly(P(-25.f, 20.f), X, 32.f, 20.f);
    prism(g, boiler, z - 0.5f, z + 26.f, brick, M(MAT_BRICK), rgb(0.5f), M(MAT_ROOF_METAL));
    collide(g, vec3(P(-25.f, 20.f), z + 13.f), X, vec3(32.f, 20.f, 13.f));
    std::vector<vec2> mill = rectPoly(P(40.f, 15.f), X, 28.f, 16.f);
    prism(g, mill, z - 0.5f, z + 16.f, rust, M(MAT_CORRUGATED), rgb(0.6f), M(MAT_ROOF_METAL), false);
    for (int k = 0; k < 5; k++) {
        float x0 = 40.f - 28.f + k * 11.2f;
        vec3 a(P(x0, -16.f), z + 16.f), b(P(x0, 16.f), z + 16.f), c(P(x0 + 11.2f, 16.f), z + 20.f), d(P(x0 + 11.2f, -16.f), z + 20.f);
        quad(g, *g.m, a, b, c, d, metal, M(MAT_ROOF_METAL), normalize(vec3(-X, 2.8f)));
        quad(g, *g.m, vec3(P(x0 + 11.2f, -16.f), z + 16.f), vec3(P(x0 + 11.2f, 16.f), z + 16.f), c, d, rgb(0.3f, 0.35f, 0.38f), M(MAT_GLASS), vec3(X, 0));
    }
    collide(g, vec3(P(40.f, 15.f), z + 9.f), X, vec3(28.f, 16.f, 9.f));
    // furnace glow windows on the boiler house
    for (int k = 0; k < 6; k++) {
        vec2 wc = P(-50.f + k * 10.f, 20.f - 20.05f);
        quad(g, *g.m, vec3(wc - X * 2.f, z + 5.f), vec3(wc + X * 2.f, z + 5.f), vec3(wc + X * 2.f, z + 10.f), vec3(wc - X * 2.f, z + 10.f), rgb(1.f, 0.55f, 0.2f, 0.35f),
             emMat(EA_PULSE, (u32)k * 30u), vec3(-Y, 0));
    }
    // three brick smokestacks with banded tops
    for (int k = 0; k < 3; k++) {
        vec2 sc = P(-45.f + k * 14.f, 48.f);
        std::vector<vec2> prof = {vec2(3.6f, 0.f), vec2(2.4f, 62.f), vec2(2.8f, 64.f), vec2(2.8f, 68.f), vec2(2.5f, 70.f)};
        bandedLathe(g, vec3(sc, z), prof, {brick, rgb(0.95f, 0.95f, 0.95f), rgb(0.12f), rgb(0.12f)}, detail ? 16 : 8, M(MAT_BRICK));
        collide(g, vec3(sc, z + 34.f), X, vec3(3.f, 3.f, 34.f));
        lamp(g, vec3(sc, z + 70.5f), detail ? 0.5f : 2.f, vec3(1.f, 0.05f, 0.02f), 1.f, EA_SLOWBLINK, (u32)k * 60u);
        light(g, vec3(sc, z + 70.5f), vec3(1.f, 0.08f, 0.04f) * 1500.f, 20.f, 4);
    }
    // four concrete sugar silos
    for (int k = 0; k < 4; k++) {
        vec2 sc = P(55.f + (k & 1) * 14.f, -40.f - (k >> 1) * 14.f);
        lathe(g, vec3(sc, z), {vec2(6.2f, 0.f), vec2(6.2f, 30.f), vec2(4.f, 32.5f), vec2(0.f, 33.5f)}, detail ? 18 : 8, rgb(0.9f, 0.88f, 0.84f), M(MAT_CONCRETE), false);
        collide(g, vec3(sc, z + 16.f), X, vec3(5.5f, 5.5f, 16.f));
    }
    // conveyor from the cane yard up into the mill
    beam(g, vec3(P(15.f, -40.f), z + 2.f), vec3(P(20.f, 0.f), z + 14.f), 2.2f, 1.2f, rgb(0.55f, 0.55f, 0.58f), M(MAT_METAL_PAINTED));
    if (detail)
        for (int k = 0; k < 4; k++) {
            vec3 p = lerp(vec3(P(15.f, -40.f), z + 2.f), vec3(P(20.f, 0.f), z + 14.f), (k + 0.5f) / 4.f);
            beam(g, vec3(p.xy(), z), p - vec3(0, 0, 0.6f), 0.4f, 0.4f, rgb(0.5f), M(MAT_METAL_PAINTED));
        }
    // cane yard: piles of harvested cane, a gantry unloader and trucks
    for (int k = 0; k < 3; k++) {
        vec2 pc = P(-40.f + k * 28.f, -55.f);
        std::vector<vec2> pile = ellipseFP(pc, X, 12.f, 7.f, 12);
        std::vector<vec2> top = ellipseFP(pc, X, 8.f, 4.f, 12);
        for (size_t i = 0; i < pile.size(); i++) {
            vec2 a = pile[i], b = pile[(i + 1) % pile.size()], c = top[(i + 1) % top.size()], d = top[i];
            vec2 on = normalize((a + b) * 0.5f - pc);
            quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(c, z + 3.5f), vec3(d, z + 3.5f), rgb(0.55f, 0.5f, 0.3f), M(MAT_WOOD),
                 vec3(on, 0.6f));
        }
        polyFlat(g, *g.m, top, z + 3.5f, rgb(0.5f, 0.55f, 0.28f), M(MAT_WOOD));
    }
    for (int s = -1; s <= 1; s += 2) beam(g, vec3(P(-60.f, -55.f + s * 10.f), z), vec3(P(-60.f, -55.f + s * 10.f), z + 14.f), 0.8f, 0.8f, rgb(0.9f, 0.7f, 0.1f),
                                         M(MAT_METAL_PAINTED));
    beam(g, vec3(P(-60.f, -66.f), z + 14.f), vec3(P(30.f, -66.f), z + 14.f), 1.2f, 1.6f, rgb(0.9f, 0.7f, 0.1f), M(MAT_METAL_PAINTED));
    beam(g, vec3(P(-60.f, -44.f), z + 14.f), vec3(P(30.f, -44.f), z + 14.f), 1.2f, 1.6f, rgb(0.9f, 0.7f, 0.1f), M(MAT_METAL_PAINTED));
    if (detail)
        for (int k = 0; k < 3; k++) {
            vec2 tp = P(-70.f + k * 18.f, -85.f);
            boxY(g, vec3(tp, z + 1.6f), X, vec3(3.f, 1.2f, 1.1f), rgb(0.2f, 0.35f, 0.6f), M(MAT_METAL_PAINTED));
            boxY(g, vec3(tp - X * 7.f, z + 2.f), X, vec3(4.5f, 1.3f, 1.3f), rgb(0.35f, 0.3f, 0.2f), M(MAT_METAL_PAINTED));
            collide(g, vec3(tp - X * 2.f, z + 1.6f), X, vec3(8.f, 1.3f, 1.6f));
        }
    // water tower with the company name
    vec2 wt = P(-80.f, 30.f);
    for (int k = 0; k < 4; k++) {
        vec2 lp = wt + X * ((k & 1) ? 3.f : -3.f) + Y * ((k & 2) ? 3.f : -3.f);
        beam(g, vec3(lp, z), vec3(wt + (lp - wt) * 0.7f, z + 22.f), 0.3f, 0.3f, rgb(0.5f), M(MAT_METAL_PAINTED));
    }
    lathe(g, vec3(wt, z + 22.f), {vec2(0.f, 0.f), vec2(4.5f, 1.5f), vec2(4.5f, 6.5f), vec2(3.f, 8.f), vec2(0.f, 8.6f)}, detail ? 16 : 8, rgb(0.9f, 0.9f, 0.88f),
          M(MAT_METAL_PAINTED), false);
    if (detail) {
        const char* t = e.text.c_str();
        float th = 1.1f;
        float tw = textAdvance(t, th, 0.2f);
        // wrap the name around the tank facing the road (south)
        vec2 face = -Y, viewR = perp(face);
        strokeText(g, *g.m, t, vec3(wt + face * 4.55f - viewR * (tw * 0.5f), z + 25.5f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.18f, rgb(0.6f, 0.15f, 0.1f), M(MAT_METAL_PAINTED),
                   0.f, 0.2f);
        // office and yard lights
        std::vector<vec2> off = rectPoly(P(-70.f, -20.f), X, 9.f, 6.f);
        prism(g, off, z - 0.5f, z + 4.f, rgb(0.95f, 0.93f, 0.88f), M(MAT_STUCCO), rgb(0.6f), M(MAT_ROOF_GRAVEL));
        collide(g, vec3(P(-70.f, -20.f), z + 2.f), X, vec3(9.f, 6.f, 2.f));
        for (int k = 0; k < 4; k++) {
            vec2 lp = P(-60.f + k * 35.f, -30.f);
            cyl(g, vec3(lp, z), 0.2f, 0.15f, 12.f, 6, rgb(0.5f), M(MAT_METAL_PAINTED), false);
            lamp(g, vec3(lp, z + 12.2f), 0.6f, vec3(1.f, 0.8f, 0.5f), 0.7f, EA_NIGHT);
            light(g, vec3(lp, z + 12.f), vec3(1.f, 0.75f, 0.45f) * 14000.f, 45.f, 1, vec3(0, 0, -1), 0.35f);
        }
    }
}

// ------------------------------------------------------------------------------------------------ silos and windmills
void genSilo(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    float r = e.p[0], h = e.p[1];
    int seg = g.detail ? 16 : 8;
    vec3 b(e.c, e.z);
    switch (e.variant) {
        case 0:  // concrete stave silo with a dome
            lathe(g, b, {vec2(r, -0.5f), vec2(r, h), vec2(r * 0.8f, h + r * 0.45f), vec2(r * 0.4f, h + r * 0.75f), vec2(0.f, h + r * 0.82f)}, seg, rgb(0.85f, 0.83f, 0.78f),
                  M(MAT_CONCRETE), false);
            break;
        case 1:  // glass-lined blue silo
            lathe(g, b, {vec2(r, -0.5f), vec2(r, h), vec2(r * 0.7f, h + r * 0.5f), vec2(0.f, h + r * 0.7f)}, seg, rgb(0.12f, 0.2f, 0.45f), M(MAT_METAL_PAINTED), false);
            break;
        default:  // corrugated grain bin with a conical roof
            lathe(g, b, {vec2(r * 1.2f, -0.5f), vec2(r * 1.2f, h * 0.6f), vec2(r * 0.25f, h * 0.6f + r * 0.8f), vec2(0.f, h * 0.6f + r * 0.85f)}, seg,
                  rgb(0.78f, 0.8f, 0.82f), M(MAT_CORRUGATED), false);
            break;
    }
    if (g.detail) {
        vec2 lad = e.c + e.ax * (r + 0.1f);
        for (int s = -1; s <= 1; s += 2) beam(g, vec3(lad + perp(e.ax) * (s * 0.25f), e.z), vec3(lad + perp(e.ax) * (s * 0.25f), e.z + h), 0.05f, 0.05f, rgb(0.4f),
                                             M(MAT_METAL_PAINTED));
    }
    collide(g, vec3(e.c, e.z + h * 0.5f), e.ax, vec3(r * 0.9f, r * 0.9f, h * 0.5f));
}

void genWindmill(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    float H = e.p[0];
    vec3 b(e.c, e.z);
    u32 steel = rgb(0.6f, 0.6f, 0.62f);
    vec3 top = b + vec3(0, 0, H);
    for (int k = 0; k < 4; k++) {
        float a = kHalfPi * k + kPi * 0.25f;
        vec2 foot = e.c + vec2(cosf(a), sinf(a)) * 2.4f;
        beam(g, vec3(foot, e.z - 0.3f), top + vec3(cosf(a) * 0.35f, sinf(a) * 0.35f, 0), 0.12f, 0.12f, steel, M(MAT_METAL_PAINTED));
        if (detail)
            for (int l = 1; l < 4; l++) {
                float t = l / 4.f;
                float a2 = a + kHalfPi;
                vec3 p0 = lerp(vec3(foot, e.z), top + vec3(cosf(a) * 0.35f, sinf(a) * 0.35f, 0), t);
                vec3 p1 = lerp(vec3(e.c + vec2(cosf(a2), sinf(a2)) * 2.4f, e.z), top + vec3(cosf(a2) * 0.35f, sinf(a2) * 0.35f, 0), t);
                beam(g, p0, p1, 0.06f, 0.06f, steel, M(MAT_METAL_PAINTED));
            }
    }
    // wheel facing the wind (ax) with many blades, tail vane behind
    vec3 hub = top + vec3(e.ax * 0.8f, 0.6f);
    vec3 fwd(e.ax, 0), side(perp(e.ax), 0);
    int blades = detail ? 18 : 8;
    for (int k = 0; k < blades; k++) {
        float a = kTwoPi * k / blades;
        vec3 dir = side * cosf(a) + vec3(0, 0, sinf(a));
        vec3 p0 = hub + dir * 0.5f, p1 = hub + dir * 2.4f;
        beam(g, p0, p1, 0.35f, 0.03f, rgb(0.85f, 0.85f, 0.83f), M(MAT_METAL_PAINTED), fwd);
    }
    if (detail) {
        for (int k = 0; k < 16; k++) {
            float a0 = kTwoPi * k / 16, a1 = kTwoPi * (k + 1) / 16;
            rod(g, hub + (side * cosf(a0) + vec3(0, 0, sinf(a0))) * 2.4f, hub + (side * cosf(a1) + vec3(0, 0, sinf(a1))) * 2.4f, 0.03f, 3, steel, M(MAT_METAL_PAINTED));
        }
    }
    beam(g, hub, hub - fwd * 3.2f, 0.12f, 0.12f, steel, M(MAT_METAL_PAINTED));
    quad(g, *g.m, hub - fwd * 2.2f - vec3(0, 0, 0.6f), hub - fwd * 3.6f - vec3(0, 0, 0.5f), hub - fwd * 3.6f + vec3(0, 0, 0.7f), hub - fwd * 2.2f + vec3(0, 0, 0.4f),
         rgb(0.85f, 0.2f, 0.15f), M(MAT_METAL_PAINTED), side);
    quad(g, *g.m, hub - fwd * 2.2f - vec3(0, 0, 0.6f), hub - fwd * 3.6f - vec3(0, 0, 0.5f), hub - fwd * 3.6f + vec3(0, 0, 0.7f), hub - fwd * 2.2f + vec3(0, 0, 0.4f),
         rgb(0.85f, 0.2f, 0.15f), M(MAT_METAL_PAINTED), -side);
    // stock tank at the foot
    if (detail) cyl(g, vec3(e.c + e.ax * 4.f, e.z), 1.8f, 1.8f, 0.8f, 12, rgb(0.6f, 0.62f, 0.64f), M(MAT_CORRUGATED), true);
    collide(g, vec3(e.c, e.z + H * 0.5f), e.ax, vec3(1.5f, 1.5f, H * 0.5f));
}

// ------------------------------------------------------------------------------------------------ Sawgrass boardwalk and tower
void genBoardwalk(const SiteElem& e, G& g) {
    bool detail = g.detail;
    const auto& P = e.pts;
    u32 wood = rgb(0.6f, 0.52f, 0.42f);
    for (size_t i = 0; i + 1 < P.size(); i++) {
        vec2 a = P[i], b = P[i + 1];
        if (!g.owns((a + b) * 0.5f)) continue;
        float z = Max(gMap->heightAt(a.x, a.y), Max(gMap->heightAt(b.x, b.y), 0.f)) + 1.1f;
        vec2 d = normalize(b - a), n = perp(d);
        float L = length(b - a) + 0.6f;
        vec2 aa = a - d * 0.3f, bb = aa + d * L;
        quad(g, *g.m, vec3(aa - n * 1.25f, z), vec3(bb - n * 1.25f, z), vec3(bb + n * 1.25f, z), vec3(aa + n * 1.25f, z), wood, M(MAT_WOOD), vec3(0, 0, 1));
        for (int s = -1; s <= 1; s += 2) {
            quad(g, *g.m, vec3(aa + n * (s * 1.25f), z), vec3(bb + n * (s * 1.25f), z), vec3(bb + n * (s * 1.25f), z - 0.3f), vec3(aa + n * (s * 1.25f), z - 0.3f), wood,
                 M(MAT_WOOD), vec3(n * (float)s, 0));
            if (detail) {
                beam(g, vec3(aa + n * (s * 1.15f), z + 1.f), vec3(bb + n * (s * 1.15f), z + 1.f), 0.08f, 0.08f, wood, M(MAT_WOOD));
                for (float t = 0.f; t < L; t += 3.f) {
                    vec2 p = aa + d * t + n * (s * 1.15f);
                    float gz = gMap->heightAt(p.x, p.y);
                    cyl(g, vec3(p, Min(gz, 0.f) - 1.f), 0.09f, 0.09f, z + 1.f - (Min(gz, 0.f) - 1.f), 5, wood, M(MAT_WOOD), false);
                }
            }
        }
    }
}

void genObsTower(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec3 b(e.c, e.z);
    u32 wood = rgb(0.55f, 0.45f, 0.35f);
    float H = e.h;
    for (int k = 0; k < 4; k++) {
        vec2 p = e.c + vec2((k & 1) ? 2.6f : -2.6f, (k & 2) ? 2.6f : -2.6f);
        beam(g, vec3(p, e.z - 0.5f), vec3(p, e.z + H), 0.3f, 0.3f, wood, M(MAT_WOOD));
    }
    for (int l = 1; l <= 3; l++) {
        float z = e.z + l * (H - 2.f) / 3.f;
        boxY(g, vec3(e.c, z), vec2(1, 0), vec3(2.9f, 2.9f, 0.1f), wood, M(MAT_WOOD), true);
        if (detail)
            for (int k = 0; k < 4; k++) {
                vec2 a = e.c + rotate(vec2(2.8f, -2.8f), kHalfPi * k), c = e.c + rotate(vec2(2.8f, 2.8f), kHalfPi * k);
                beam(g, vec3(a, z + 1.f), vec3(c, z + 1.f), 0.07f, 0.07f, wood, M(MAT_WOOD));
            }
        // stair flight to the next level
        if (detail && l < 3) beam(g, vec3(e.c + vec2(-2.f, -2.2f), z - (H - 2.f) / 3.f + 0.1f), vec3(e.c + vec2(2.f, -2.2f), z), 0.9f, 0.08f, wood, M(MAT_WOOD));
    }
    // pyramid roof
    vec3 apex = b + vec3(0, 0, H + 2.6f);
    for (int k = 0; k < 4; k++) {
        vec2 a = e.c + rotate(vec2(3.2f, -3.2f), kHalfPi * k), c = e.c + rotate(vec2(3.2f, 3.2f), kHalfPi * k);
        MeshData& m = *g.m;
        vec3 p0(a, e.z + H + 0.2f), p1(c, e.z + H + 0.2f);
        vec3 nn = normalize(cross(p1 - p0, apex - p0));
        if (nn.z < 0) nn = -nn;
        u32 i0 = m.addVertex(p0 - g.org, nn, normalize(p1 - p0), vec2(0, 0), rgb(0.35f, 0.5f, 0.45f), M(MAT_ROOF_METAL));
        u32 i1 = m.addVertex(p1 - g.org, nn, normalize(p1 - p0), vec2(6, 0), rgb(0.35f, 0.5f, 0.45f), M(MAT_ROOF_METAL));
        u32 i2 = m.addVertex(apex - g.org, nn, normalize(p1 - p0), vec2(3, 4), rgb(0.35f, 0.5f, 0.45f), M(MAT_ROOF_METAL));
        if (dot(cross(p1 - p0, apex - p0), nn) > 0) m.tri(i0, i1, i2);
        else m.tri(i0, i2, i1);
    }
    collide(g, b + vec3(0, 0, H * 0.5f), vec2(1, 0), vec3(2.9f, 2.9f, H * 0.5f));
}

// ------------------------------------------------------------------------------------------------ boat ramp
void genBoatRamp(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 d = e.ax, n = perp(d);
    const Pad* rp = gSites->padAt(e.c + d * 2.f);
    // ramp slab following the sloped pad, with grooves
    vec2 a = e.c + d * 2.f - d * 18.f, b = e.c + d * 2.f + d * 18.f;
    float za = rp ? rp->heightAt(a) : e.z, zb = rp ? rp->heightAt(b) : e.z - 2.f;
    quad(g, *g.m, vec3(a - n * 5.f, za + 0.02f), vec3(b - n * 5.f, zb + 0.02f), vec3(b + n * 5.f, zb + 0.02f), vec3(a + n * 5.f, za + 0.02f), rgb(0.8f), M(MAT_CONCRETE),
         vec3(0, 0, 1));
    if (detail)
        for (float t = 0.05f; t < 1.f; t += 0.08f) {
            vec2 p = lerp(a, b, t);
            paintRect(g, p, n, 4.6f, 0.08f, Lerp(za, zb, t) + 0.035f, rgb(0.5f), M(MAT_CONCRETE));
        }
    // courtesy dock alongside
    vec2 dk0 = e.c + n * 7.f - d * 4.f, dk1 = dk0 + d * 26.f;
    vec2 dd = normalize(dk1 - dk0);
    float zd = kLakeLevel + 0.6f;
    quad(g, *g.m, vec3(dk0 - perp(dd) * 1.1f, zd), vec3(dk1 - perp(dd) * 1.1f, zd), vec3(dk1 + perp(dd) * 1.1f, zd), vec3(dk0 + perp(dd) * 1.1f, zd), rgb(0.7f, 0.6f, 0.48f),
         M(MAT_WOOD), vec3(0, 0, 1));
    if (detail)
        for (float t = 0.f; t <= 26.f; t += 4.f)
            for (int s = -1; s <= 1; s += 2) cyl(g, vec3(dk0 + dd * t + perp(dd) * (s * 1.f), kLakeLevel - 2.f), 0.12f, 0.12f, 3.f, 5, rgb(0.45f, 0.38f, 0.3f), M(MAT_WOOD), true);
    // parking lot markings and trucks with trailers
    vec2 lot = e.c - d * 36.f;
    const Pad* lp = gSites->padAt(lot);
    float lz = lp ? lp->z : gMap->heightAt(lot.x, lot.y);
    if (detail) {
        for (int k = -4; k <= 4; k++) paintRect(g, lot + n * (k * 4.2f) - d * 8.f, d, 5.5f, 0.06f, lz + 0.022f, kWhiteC, M(MAT_PAINT_WHITE));
        for (int k = 0; k < 3; k++) {
            vec2 tp = lot + n * (-12.f + k * 8.4f) - d * 6.f;
            vec3 col = hsvToRgb(0.1f * k + 0.55f, 0.5f, 0.6f);
            boxY(g, vec3(tp - d * 2.f, lz + 0.9f), -d, vec3(2.6f, 1.f, 0.6f), rgbv(col), M(MAT_METAL_PAINTED));
            boxY(g, vec3(tp - d * 2.6f, lz + 1.7f), -d, vec3(1.3f, 0.95f, 0.45f), rgb(0.1f, 0.12f, 0.14f), M(MAT_GLASS));
            boxY(g, vec3(tp + d * 5.f, lz + 0.5f), d, vec3(3.f, 1.f, 0.15f), rgb(0.3f), M(MAT_METAL_PAINTED));
            collide(g, vec3(tp + d * 1.5f, lz + 0.9f), d, vec3(6.5f, 1.1f, 0.9f));
        }
        // bait shop kiosk + sign
        vec2 kc = lot + n * 22.f;
        prism(g, rectPoly(kc, d, 4.f, 3.f), lz - 0.3f, lz + 3.f, rgb(0.9f, 0.85f, 0.7f), M(MAT_WOOD_SIDING), rgb(0.35f, 0.3f, 0.25f), M(MAT_ROOF_METAL));
        collide(g, vec3(kc, lz + 1.5f), d, vec3(4.f, 3.f, 1.5f));
        const char* t = e.text.c_str();
        float th = 0.5f;
        float tw = textAdvance(t, th, 0.3f);
        vec2 sp = lot - d * 22.f;
        vec2 face = -d, viewR = perp(face);
        float sz = gMap->heightAt(sp.x, sp.y);
        for (int s = -1; s <= 1; s += 2) cyl(g, vec3(sp + viewR * (s * (tw * 0.5f + 0.3f)), sz), 0.08f, 0.08f, 2.2f, 5, rgb(0.4f, 0.3f, 0.2f), M(MAT_WOOD), false);
        boxY(g, vec3(sp, sz + 1.8f), viewR, vec3(tw * 0.5f + 0.4f, 0.08f, 0.45f), rgb(0.1f, 0.3f, 0.2f), M(MAT_WOOD), true);
        strokeText(g, *g.m, t, vec3(sp + face * 0.1f - viewR * (tw * 0.5f), sz + 1.55f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.07f, rgb(0.95f, 0.9f, 0.7f), M(MAT_PLASTER));
        for (int k = -1; k <= 1; k += 2) {
            vec2 lpp = lot + n * (k * 14.f);
            cyl(g, vec3(lpp, lz), 0.12f, 0.09f, 7.f, 6, rgb(0.5f), M(MAT_METAL_PAINTED), false);
            lamp(g, vec3(lpp, lz + 7.1f), 0.35f, vec3(1.f, 0.8f, 0.5f), 0.7f, EA_NIGHT);
            light(g, vec3(lpp, lz + 7.f), vec3(1.f, 0.8f, 0.55f) * 5000.f, 24.f, 0, vec3(0, 0, -1), 0.3f);
        }
    }
}

}  // namespace rural_mesh
}  // namespace World
