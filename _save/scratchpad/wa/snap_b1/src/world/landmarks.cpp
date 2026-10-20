// Signature city landmarks: Solaris One supertall, Tidewater Stadium, City Hall plaza, Canvas Park with its
// fountain, and the highway billboards with procedurally composed original ads.
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"
#include "interiors.h"

namespace World {

namespace landmark_mesh {

using namespace sitegeo;

// ------------------------------------------------------------------------------------------------ Solaris One
// A twisting glass supertall: chamfered-square floor plates rotate 58 degrees and taper from 54 m to 34 m over
// 410 m; a crown of four converging glass fins and a needle spire reach 492 m. Gold LED corner lines at night.
std::vector<vec2> solarisPlate(vec2 c, float half, float chamfer, float rot) {
    std::vector<vec2> p;
    vec2 corners[4] = {vec2(1, 1), vec2(-1, 1), vec2(-1, -1), vec2(1, -1)};
    for (int i = 0; i < 4; i++) {
        vec2 k = corners[i], prev = corners[(i + 3) % 4], next = corners[(i + 1) % 4];
        vec2 cp = k * half;
        vec2 a = cp + normalize(prev * half - cp) * chamfer, b = cp + normalize(next * half - cp) * chamfer;
        p.push_back(c + rotate(a, rot));
        p.push_back(c + rotate(b, rot));
    }
    // ensure CCW
    float area = polygonArea2D(p.data(), (int)p.size());
    if (area < 0) std::reverse(p.begin(), p.end());
    return p;
}

void genSolaris(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    float z0 = e.z;
    u32 fac = (u32)e.p[7];
    // podium (the enterable lobby behind its south face and the penthouse up top: interiors.cpp planSolaris)
    const int lobby = interiorForLandmark(IK_TOWER_LOBBY), pent = interiorForLandmark(IK_PENTHOUSE);
    std::vector<vec2> pod = solarisPlate(e.c, 36.f, 8.f, 0.f);
    if (detail && lobby >= 0) interiorFacadeRing(lobby, *g.m, g.org, pod, z0 - 1.f, z0 + 26.f, z0, fac, 1.6f, kWhiteC);
    else facadeRing(g, pod, z0 - 1.f, z0 + 26.f, z0, fac, 1.6f);
    polyFlat(g, *g.m, pod, z0 + 26.f, rgb(0.85f), M(MAT_ROOF_GRAVEL));
    if (!(lobby >= 0 && g.col && interiorShellCollision(lobby, e.c - vec2(0.f, 1.f), vec2(1, 0), 34.f, 35.f, z0, z0 + 26.f, *g.col)))
        collide(g, vec3(e.c, z0 + 13.f), vec2(1, 0), vec3(34.f, 34.f, 13.f));
    // tower segments
    const float towerTop = 468.f;
    int segs = detail ? 30 : 15;
    float segH = (towerTop - 26.f) / segs;
    for (int k = 0; k < segs; k++) {
        float t0 = (float)k / segs, t1 = (float)(k + 1) / segs;
        float za = z0 + 26.f + k * segH, zb = za + segH;
        float tm = (t0 + t1) * 0.5f;
        float half = Lerp(30.f, 18.f, powf(tm, 1.2f));
        float rot = tm * 58.f * kDegToRad;
        float ch = half * 0.3f;
        std::vector<vec2> fp = solarisPlate(e.c, half, ch, rot);
        const bool pentSeg = detail && pent >= 0 && k == segs - 1;
        if (pentSeg) interiorFacadeRing(pent, *g.m, g.org, fp, za, zb, z0, fac, 1.6f, kWhiteC);
        else facadeRing(g, fp, za, zb, z0, fac, 1.6f);
        // slab fascia with a thin light line at each joint
        std::vector<vec2> ring = solarisPlate(e.c, half + 0.35f, ch, rot);
        for (size_t i = 0; i < ring.size(); i++) {
            vec2 a = ring[i], b = ring[(i + 1) % ring.size()];
            vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
            quad(g, *g.m, vec3(a, zb - 0.35f), vec3(b, zb - 0.35f), vec3(b, zb + 0.25f), vec3(a, zb + 0.25f), rgb(0.75f, 0.77f, 0.8f), M(MAT_METAL_BRUSHED), vec3(on, 0));
            if (detail || (k & 1))
                quad(g, *g.m, vec3(a + on * 0.02f, zb - 0.3f), vec3(b + on * 0.02f, zb - 0.3f), vec3(b + on * 0.02f, zb - 0.18f), vec3(a + on * 0.02f, zb - 0.18f),
                     rgb(1.f, 0.78f, 0.4f, 0.5f), emMat(EA_NIGHT), vec3(on, 0));
        }
        // gold LED lines on the four chamfer faces
        for (int c = 0; c < 4; c++) {
            vec2 a = fp[(size_t)c * 2], b = fp[(size_t)c * 2 + 1];
            vec2 mid = (a + b) * 0.5f;
            vec2 on = normalize(mid - e.c);
            vec2 tan = perp(on);
            vec3 p0(mid + on * 0.05f - tan * 0.35f, za), p1(mid + on * 0.05f + tan * 0.35f, za);
            quad(g, *g.m, p0, p1, p1 + vec3(0, 0, segH), p0 + vec3(0, 0, segH), rgb(1.f, 0.72f, 0.3f, 0.75f), emMat(EA_NIGHT), vec3(on, 0));
        }
        if (!(pentSeg && g.col && interiorShellCollision(pent, e.c, vec2(cosf(rot), sinf(rot)), half, half, za, zb, *g.col)))
            collide(g, vec3(e.c, (za + zb) * 0.5f), vec2(cosf(rot), sinf(rot)), vec3(half, half, segH * 0.5f));
        if (k == segs - 1) polyFlat(g, *g.m, fp, zb, rgb(0.6f), M(MAT_ROOF_GRAVEL));
    }
    // crown: four glass fins converging toward the spire
    float topHalf = 18.f, topRot = 58.f * kDegToRad, zt = z0 + towerTop;
    for (int c = 0; c < 4; c++) {
        float a = topRot + kHalfPi * c + kPi * 0.25f;
        vec2 d(cosf(a), sinf(a));
        vec2 base = e.c + d * (topHalf * 1.1f);
        vec2 tip = e.c + d * 3.f;
        vec3 b0(base - perp(d) * 6.5f, zt), b1(base + perp(d) * 6.5f, zt), t0(tip, zt + 42.f);
        vec3 nrm = normalize(vec3(d, 0.35f));
        u32 i0 = (u32)g.m->verts.size();
        g.m->addVertex(b0 - g.org, nrm, vec3(perp(d), 0), vec2(0, 0), rgb(0.5f, 0.65f, 0.75f), M(MAT_GLASS));
        g.m->addVertex(b1 - g.org, nrm, vec3(perp(d), 0), vec2(1, 0), rgb(0.5f, 0.65f, 0.75f), M(MAT_GLASS));
        g.m->addVertex(t0 - g.org, nrm, vec3(perp(d), 0), vec2(0.5f, 1), rgb(0.5f, 0.65f, 0.75f), M(MAT_GLASS));
        vec3 fn = cross(b1 - b0, t0 - b0);
        if (dot(fn, nrm) > 0) g.m->tri(i0, i0 + 1, i0 + 2);
        else g.m->tri(i0, i0 + 2, i0 + 1);
        u32 i1 = (u32)g.m->verts.size();
        g.m->addVertex(b0 - g.org, -nrm, vec3(perp(d), 0), vec2(0, 0), rgb(0.4f, 0.5f, 0.6f), M(MAT_GLASS));
        g.m->addVertex(b1 - g.org, -nrm, vec3(perp(d), 0), vec2(1, 0), rgb(0.4f, 0.5f, 0.6f), M(MAT_GLASS));
        g.m->addVertex(t0 - g.org, -nrm, vec3(perp(d), 0), vec2(0.5f, 1), rgb(0.4f, 0.5f, 0.6f), M(MAT_GLASS));
        if (dot(fn, nrm) > 0) g.m->tri(i1, i1 + 2, i1 + 1);
        else g.m->tri(i1, i1 + 1, i1 + 2);
        // glowing fin edges
        beam(g, b0, t0, 0.35f, 0.35f, rgb(1.f, 0.7f, 0.25f, 0.8f), emMat(EA_PULSE, (u32)c * 20u));
        beam(g, b1, t0, 0.35f, 0.35f, rgb(1.f, 0.7f, 0.25f, 0.8f), emMat(EA_PULSE, (u32)c * 20u));
    }
    // spire
    cyl(g, vec3(e.c, zt), 2.6f, 0.25f, 96.f, detail ? 10 : 6, rgb(0.85f, 0.86f, 0.88f), M(MAT_METAL_BRUSHED), false);
    for (int k = 0; k < 3; k++) {
        float zz = zt + 36.f + k * 28.f;
        lamp(g, vec3(e.c, zz), 0.9f, vec3(1.f, 0.05f, 0.02f), 1.f, EA_BLINK, (u32)k * 30u);
        light(g, vec3(e.c, zz), vec3(1.f, 0.08f, 0.04f) * 1500.f, 25.f, 4);
    }
    lamp(g, vec3(e.c, zt + 96.5f), 1.1f, vec3(1.f, 0.05f, 0.02f), 1.f, EA_BLINK, 90);
    // name on the podium and entrance canopy (south)
    if (detail) {
        const char* t = e.text.c_str();
        float th = 3.4f;
        float tw = textAdvance(t, th, 0.35f);
        strokeText(g, *g.m, t, vec3(e.c + vec2(-tw * 0.5f, -35.4f), z0 + 19.f), vec3(1, 0, 0), vec3(0, 0, 1), th, 0.45f, rgb(1.f, 0.8f, 0.45f, 0.6f), emMat(EA_NIGHT),
                   0.35f, 0.35f);
        boxY(g, vec3(e.c + vec2(0, -41.f), z0 + 6.5f), vec2(1, 0), vec3(14.f, 5.f, 0.3f), rgb(0.85f, 0.87f, 0.9f), M(MAT_METAL_BRUSHED), true);
        for (int s = -1; s <= 1; s += 2) cyl(g, vec3(e.c + vec2(s * 12.f, -43.f), z0), 0.3f, 0.3f, 6.3f, 8, rgb(0.8f), M(MAT_METAL_BRUSHED), false);
        for (int k = -1; k <= 1; k++) light(g, vec3(e.c + vec2(k * 9.f, -41.f), z0 + 6.f), vec3(1.f, 0.85f, 0.6f) * 5000.f, 20.f, 1, vec3(0, 0, -1), 0.3f);
        // uplights washing the corner LED lines
        for (int c = 0; c < 4; c++) {
            float a = kHalfPi * c + kPi * 0.25f;
            light(g, vec3(e.c + vec2(cosf(a), sinf(a)) * 40.f, z0 + 27.f), vec3(1.f, 0.75f, 0.35f) * 9000.f, 45.f, 1, vec3(0, 0, 1), 0.3f);
        }
        // plaza palms
        for (int k = 0; k < 8; k++) {
            float a = kTwoPi * k / 8 + 0.2f;
            prop(g, vec3(e.c + vec2(cosf(a), sinf(a)) * 39.f, z0 + 0.35f), a, 1.1f, PROP_PALM_TALL, (u8)(k & 3));
        }
    }
}

// ------------------------------------------------------------------------------------------------ Tidewater Stadium
void genStadium(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 ax = e.ax, ay = perp(ax);
    float z0 = e.z;
    auto E = [&](float rx, float ry, float a) { return e.c + ax * (cosf(a) * rx) + ay * (sinf(a) * ry); };
    int seg = detail ? 72 : 32;
    MeshData& m = *g.m;
    // pitch with mowing stripes and markings
    {
        float px = 52.5f, py = 34.f;
        polyFlat(g, m, rectPoly(e.c, ax, px + 6.f, py + 6.f), z0 + 0.05f, rgb(0.65f, 0.9f, 0.55f), M(MAT_GRASS));
        for (int k = -5; k <= 5; k += 2) {
            vec2 c = e.c + ax * (k * px / 5.5f);
            polyFlat(g, m, rectPoly(c, ax, px / 11.f, py), z0 + 0.06f, rgb(0.8f, 1.05f, 0.7f), M(MAT_GRASS));
        }
        float z = z0 + 0.075f;
        u32 w = kWhiteC, pm = M(MAT_PAINT_WHITE);
        if (detail) {
            paintLine(g, e.c - ax * px - ay * py, e.c + ax * px - ay * py, 0.12f, z, w, pm);
            paintLine(g, e.c - ax * px + ay * py, e.c + ax * px + ay * py, 0.12f, z, w, pm);
            paintLine(g, e.c - ax * px - ay * py, e.c - ax * px + ay * py, 0.12f, z, w, pm);
            paintLine(g, e.c + ax * px - ay * py, e.c + ax * px + ay * py, 0.12f, z, w, pm);
            paintLine(g, e.c - ay * py, e.c + ay * py, 0.12f, z, w, pm);
            for (int k = 0; k < 24; k++) {
                float a0 = kTwoPi * k / 24, a1 = kTwoPi * (k + 1) / 24;
                paintLine(g, e.c + vec2(cosf(a0), sinf(a0)) * 9.15f, e.c + vec2(cosf(a1), sinf(a1)) * 9.15f, 0.12f, z, w, pm);
            }
            for (int s = -1; s <= 1; s += 2) {
                vec2 gl = e.c + ax * (s * px);
                paintLine(g, gl - ay * 20.2f, gl - ax * (s * 16.5f) - ay * 20.2f, 0.12f, z, w, pm);
                paintLine(g, gl + ay * 20.2f, gl - ax * (s * 16.5f) + ay * 20.2f, 0.12f, z, w, pm);
                paintLine(g, gl - ax * (s * 16.5f) - ay * 20.2f, gl - ax * (s * 16.5f) + ay * 20.2f, 0.12f, z, w, pm);
                // goals
                for (int t = -1; t <= 1; t += 2) cyl(g, vec3(gl + ay * (t * 3.66f), z0), 0.06f, 0.06f, 2.44f, 6, w, M(MAT_METAL_PAINTED), false);
                beam(g, vec3(gl - ay * 3.66f, z0 + 2.44f), vec3(gl + ay * 3.66f, z0 + 2.44f), 0.12f, 0.12f, w, M(MAT_METAL_PAINTED));
            }
        }
    }
    // seating bowl: lower and upper tiers as terraced elliptical bands
    struct Tier { float rx0, ry0, rx1, ry1, z0, z1; int steps; };
    Tier tiers[2] = {{62.f, 44.f, 84.f, 64.f, 1.2f, 14.f, detail ? 10 : 4}, {88.f, 68.f, 102.f, 82.f, 18.f, 34.f, detail ? 8 : 3}};
    for (int ti = 0; ti < 2; ti++) {
        const Tier& T = tiers[ti];
        for (int s = 0; s < T.steps; s++) {
            float t0 = (float)s / T.steps, t1 = (float)(s + 1) / T.steps;
            float rxa = Lerp(T.rx0, T.rx1, t0), rya = Lerp(T.ry0, T.ry1, t0), rxb = Lerp(T.rx0, T.rx1, t1), ryb = Lerp(T.ry0, T.ry1, t1);
            float za = z0 + Lerp(T.z0, T.z1, t0), zb = z0 + Lerp(T.z0, T.z1, t1);
            for (int k = 0; k < seg; k++) {
                float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
                // seat colours: teal blocks with a white "wave" pattern
                float wave = sinf(a0 * 3.f + t0 * 5.f);
                vec3 col = wave > 0.55f ? vec3(0.95f, 0.95f, 0.95f) : (ti == 0 ? vec3(0.05f, 0.5f, 0.55f) : vec3(0.1f, 0.35f, 0.6f));
                if ((k % (seg / 8)) == 0) col = vec3(0.55f, 0.55f, 0.55f);  // aisles
                vec2 p0 = E(rxa, rya, a0), p1 = E(rxa, rya, a1), q0 = E(rxb, ryb, a0), q1 = E(rxb, ryb, a1);
                vec2 outN = normalize((p0 + p1) * 0.5f - e.c);
                // riser (vertical) then tread (horizontal)
                quad(g, m, vec3(p1, za), vec3(p0, za), vec3(p0, zb), vec3(p1, zb), rgbv(col * 0.8f), M(MAT_CONCRETE), vec3(-outN, 0));
                quad(g, m, vec3(p0, zb), vec3(p1, zb), vec3(q1, zb), vec3(q0, zb), rgbv(col), M(MAT_PLASTER), vec3(0, 0, 1));
            }
        }
    }
    // concourse ring between the tiers and the outer skin with vertical ribs
    int ribs = detail ? 48 : 24;
    float Rx = 108.f, Ry = 88.f;
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec2 i0 = E(84.f, 64.f, a0), i1 = E(84.f, 64.f, a1), o0 = E(Rx - 2.f, Ry - 2.f, a0), o1 = E(Rx - 2.f, Ry - 2.f, a1);
        quad(g, m, vec3(i0, z0 + 16.f), vec3(i1, z0 + 16.f), vec3(o1, z0 + 16.f), vec3(o0, z0 + 16.f), rgb(0.7f), M(MAT_CONCRETE), vec3(0, 0, 1));
        vec2 s0 = E(Rx - 2.f, Ry - 2.f, a0), s1 = E(Rx - 2.f, Ry - 2.f, a1);
        vec2 outN = normalize((s0 + s1) * 0.5f - e.c);
        // lower glass level (facade), upper translucent membrane, LED band
        quad(g, m, vec3(s0, z0 + 14.f), vec3(s1, z0 + 14.f), vec3(s1, z0 + 42.f), vec3(s0, z0 + 42.f), rgb(0.93f, 0.95f, 0.97f), M(MAT_FABRIC), vec3(outN, 0));
        quad(g, m, vec3(s0 + outN * 0.05f, z0 + 29.f), vec3(s1 + outN * 0.05f, z0 + 29.f), vec3(s1 + outN * 0.05f, z0 + 31.f), vec3(s0 + outN * 0.05f, z0 + 31.f),
             rgb(0.2f, 0.9f, 0.95f, 0.8f), emMat(EA_HUE, (u32)(k * 256 / seg)), vec3(outN, 0));
    }
    {
        std::vector<vec2> ring;
        for (int k = 0; k < seg; k++) ring.push_back(E(Rx - 2.f, Ry - 2.f, kTwoPi * k / seg));
        facadeRing(g, ring, z0 - 1.f, z0 + 14.f, z0, (u32)e.p[7], 3.2f);
    }
    for (int k = 0; k < ribs; k++) {
        float a = kTwoPi * k / ribs;
        vec2 p = E(Rx, Ry, a);
        vec2 outN = normalize(p - e.c);
        vec3 bot(p, z0), top(E(Rx + 4.f, Ry + 4.f, a), z0 + 46.f);
        beam(g, bot, vec3(p + outN * 1.5f, z0 + 24.f), 0.9f, 1.8f, rgb(0.97f), M(MAT_METAL_PAINTED), vec3(outN, 0));
        beam(g, vec3(p + outN * 1.5f, z0 + 24.f), top, 0.9f, 1.8f, rgb(0.97f), M(MAT_METAL_PAINTED), vec3(outN, 0));
    }
    // cantilevered roof ring (membrane on radial trusses) with the floodlight catwalk on the inner edge
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec2 o0 = E(Rx + 4.f, Ry + 4.f, a0), o1 = E(Rx + 4.f, Ry + 4.f, a1), i0 = E(76.f, 58.f, a0), i1 = E(76.f, 58.f, a1);
        quad(g, m, vec3(o0, z0 + 46.f), vec3(o1, z0 + 46.f), vec3(i1, z0 + 43.f), vec3(i0, z0 + 43.f), rgb(0.96f, 0.96f, 0.95f), M(MAT_FABRIC), vec3(0, 0, 1));
        quad(g, m, vec3(o0, z0 + 45.6f), vec3(i0, z0 + 42.6f), vec3(i1, z0 + 42.6f), vec3(o1, z0 + 45.6f), rgb(0.75f), M(MAT_FABRIC), vec3(0, 0, -1));
        vec2 in0 = E(76.f, 58.f, a0), in1 = E(76.f, 58.f, a1);
        quad(g, m, vec3(in0, z0 + 42.4f), vec3(in1, z0 + 42.4f), vec3(in1, z0 + 43.4f), vec3(in0, z0 + 43.4f), rgb(1.f, 0.97f, 0.9f, 0.55f), emMat(EA_NIGHT),
             vec3(normalize(e.c - (in0 + in1) * 0.5f), 0));
    }
    for (int k = 0; k < 8; k++) {
        float a = kTwoPi * k / 8 + 0.2f;
        vec2 p = E(78.f, 60.f, a);
        light(g, vec3(p, z0 + 42.f), vec3(1.f, 0.97f, 0.9f) * 60000.f, 110.f, 1, normalize(vec3(e.c - p, -40.f)), 0.3f);
    }
    // name on the east (bay) and west (city) facades
    {
        const char* t = e.text.c_str();
        float th = 4.2f;
        float tw = textAdvance(t, th, 0.3f);
        for (int s = -1; s <= 1; s += 2) {
            vec2 face = ax * (float)s;
            vec2 viewR = perp(face);
            vec2 pos = e.c + face * (Rx + 5.5f);
            strokeText(g, m, t, vec3(pos - viewR * (tw * 0.5f), z0 + 34.f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.55f, rgb(0.1f, 0.55f, 0.6f, 0.5f), emMat(EA_NIGHT),
                       detail ? 0.4f : 0.f, 0.3f);
        }
    }
    // collision: outer ring segments with gaps at the four gates
    int cs = 24;
    for (int k = 0; k < cs; k++) {
        float a = kTwoPi * (k + 0.5f) / cs;
        if (k % 6 == 0) continue;
        vec2 p = E(Rx - 3.f, Ry - 3.f, a);
        vec2 tang = normalize(E(Rx, Ry, a + 0.02f) - E(Rx, Ry, a - 0.02f));
        collide(g, vec3(p, z0 + 20.f), tang, vec3(kTwoPi * Rx / cs * 0.55f, 6.f, 20.f));
    }
    if (detail)
        for (int k = 0; k < 16; k++) {
            float a = kTwoPi * k / 16 + 0.1f;
            vec2 p = E(Rx + 11.f, Ry + 11.f, a);
            prop(g, vec3(p, z0), a, 1.1f, PROP_PALM_TALL, (u8)(k & 3));
        }
}

// ------------------------------------------------------------------------------------------------ City Hall
void hipRoof(G& g, vec2 c, vec2 ax, float hx, float hy, float z, float rise, u32 col, u32 mat) {
    vec2 ay = perp(ax);
    bool alongX = hx >= hy;
    vec2 L = alongX ? ax : ay, S = alongX ? ay : ax;
    float hl = alongX ? hx : hy, hs = alongX ? hy : hx;
    float ridge = Max(0.f, hl - hs);
    vec3 e00(c - L * hl - S * hs, z), e10(c + L * hl - S * hs, z), e11(c + L * hl + S * hs, z), e01(c - L * hl + S * hs, z);
    vec3 r0(c - L * ridge, z + rise), r1(c + L * ridge, z + rise);
    quad(g, *g.m, e00, e10, r1, r0, col, mat, vec3(-S, 0.7f));
    quad(g, *g.m, e11, e01, r0, r1, col, mat, vec3(S, 0.7f));
    MeshData& m = *g.m;
    for (int k = 0; k < 2; k++) {
        vec3 a = k ? e10 : e01, b = k ? e11 : e00, r = k ? r1 : r0;
        vec3 want(k ? L : -L, 0.7f);
        vec3 n = normalize(cross(b - a, r - a));
        if (dot(n, want) < 0) n = -n;
        u32 i0 = m.addVertex(a - g.org, n, normalize(b - a), vec2(0, 0), col, mat), i1 = m.addVertex(b - g.org, n, normalize(b - a), vec2(length(b - a), 0), col, mat);
        u32 i2 = m.addVertex(r - g.org, n, normalize(b - a), vec2(length(b - a) * 0.5f, rise), col, mat);
        if (dot(cross(b - a, r - a), n) >= 0) m.tri(i0, i1, i2);
        else m.tri(i0, i2, i1);
    }
}

void arch(G& g, vec3 left, vec3 right, float rise, float thick, float depth, u32 col, u32 mat, vec3 out, int segs) {
    vec3 c = (left + right) * 0.5f;
    float r = length(right - left) * 0.5f;
    vec3 ax = normalize(right - left);
    for (int k = 0; k < segs; k++) {
        float a0 = kPi * k / segs, a1 = kPi * (k + 1) / segs;
        vec3 p0 = c - ax * (cosf(a0) * r) + vec3(0, 0, sinf(a0) * rise), p1 = c - ax * (cosf(a1) * r) + vec3(0, 0, sinf(a1) * rise);
        beam(g, p0, p1, depth, thick, col, mat, normalize(vec3(0, 0, 1) - out * 0.f));
    }
}

void genCityHall(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 face = e.ax, side = perp(face);
    float W = 56.f, D = 22.f, floors = 4;
    float fh = 5.2f, gh = 6.5f;
    float H = gh + (floors - 1) * fh;
    float z0 = e.z;
    u32 fac = (u32)e.p[7];
    u32 stucco = rgb(1.f, 0.96f, 0.88f), tile = rgb(0.95f, 0.85f, 0.8f), stone = rgb(0.9f, 0.85f, 0.75f);
    // main block + two rear wings
    std::vector<vec2> fp = rectPoly(e.c, side, W, D);
    facadeRing(g, fp, z0 - 1.f, z0 + H, z0, fac, 4.2f);
    hipRoof(g, e.c, side, W + 0.8f, D + 0.8f, z0 + H, 5.5f, tile, M(MAT_ROOF_TILE));
    collide(g, vec3(e.c, z0 + H * 0.5f), side, vec3(W, D, H * 0.5f));
    for (int s = -1; s <= 1; s += 2) {
        vec2 wc = e.c + side * (s * (W - 9.f)) - face * (D + 14.f);
        std::vector<vec2> wf = rectPoly(wc, side, 9.f, 14.f);
        facadeRing(g, wf, z0 - 1.f, z0 + H - fh, z0, fac, 4.2f);
        hipRoof(g, wc, side, 9.6f, 14.6f, z0 + H - fh, 4.f, tile, M(MAT_ROOF_TILE));
        collide(g, vec3(wc, z0 + (H - fh) * 0.5f), side, vec3(9.f, 14.f, (H - fh) * 0.5f));
    }
    // arcaded loggia along the front
    {
        vec2 lc = e.c + face * (D + 3.5f);
        boxY(g, vec3(lc, z0 + gh + 0.4f), side, vec3(W - 2.f, 3.8f, 0.45f), stucco, M(MAT_STUCCO), true);
        polyFlat(g, *g.m, rectPoly(lc, side, W - 2.f, 3.6f), z0 + 0.25f, stone, M(MAT_MARBLE));
        int bays = 14;
        for (int k = 0; k <= bays; k++) {
            float u = -W + 2.f + k * (2.f * (W - 2.f) / bays);
            vec2 cp = lc + face * 3.2f + side * u;
            boxY(g, vec3(cp, z0 + gh * 0.5f), side, vec3(0.45f, 0.45f, gh * 0.5f), stucco, M(MAT_STUCCO));
            if (k < bays && detail) {
                float u1 = -W + 2.f + (k + 1) * (2.f * (W - 2.f) / bays);
                vec2 np = lc + face * 3.2f + side * u1;
                arch(g, vec3(cp + side * 0.45f, z0 + gh - 2.2f), vec3(np - side * 0.45f, z0 + gh - 2.2f), 1.9f, 0.35f, 0.9f, stucco, M(MAT_STUCCO), vec3(face, 0), 6);
            }
            if (detail) collide(g, vec3(cp, z0 + gh * 0.5f), side, vec3(0.45f, 0.45f, gh * 0.5f));
        }
        // steps
        for (int k = 0; k < 4; k++)
            boxY(g, vec3(lc + face * (4.2f + k * 0.8f), z0 + 0.25f - k * 0.08f), side, vec3(16.f, 0.4f, 0.12f), stone, M(MAT_MARBLE), false);
    }
    // clock tower
    vec2 tc = e.c - face * 2.f;
    float tH = 62.f;
    std::vector<vec2> tf = rectPoly(tc, side, 6.5f, 6.5f);
    prism(g, tf, z0 + H, z0 + tH, stucco, M(MAT_STUCCO), stucco, M(MAT_STUCCO), false);
    collide(g, vec3(tc, z0 + tH * 0.5f), side, vec3(6.5f, 6.5f, tH * 0.5f));
    // belfry openings and clock faces on all four sides
    for (int k = 0; k < 4; k++) {
        float a = kHalfPi * k;
        vec2 n = rotate(face, a), t = perp(n);
        vec2 fc = tc + n * 6.52f;
        quad(g, *g.m, vec3(fc - t * 2.2f, z0 + tH - 8.f), vec3(fc + t * 2.2f, z0 + tH - 8.f), vec3(fc + t * 2.2f, z0 + tH - 2.5f), vec3(fc - t * 2.2f, z0 + tH - 2.5f),
             rgb(0.1f, 0.09f, 0.08f), M(MAT_STUCCO), vec3(n, 0));
        if (detail) arch(g, vec3(fc - t * 2.2f + n * 0.05f, z0 + tH - 3.4f), vec3(fc + t * 2.2f + n * 0.05f, z0 + tH - 3.4f), 1.8f, 0.3f, 0.3f, stucco, M(MAT_STUCCO), vec3(n, 0), 6);
        // clock
        vec3 cc(fc + n * 0.05f, z0 + tH - 14.f);
        u32 b0 = (u32)g.m->verts.size();
        g.m->addVertex(cc - g.org, vec3(n, 0), vec3(t, 0), vec2(0, 0), rgb(1.f, 0.97f, 0.85f, 0.35f), emMat(EA_NIGHT));
        for (int s = 0; s <= 20; s++) {
            float an = kTwoPi * s / 20;
            g.m->addVertex(cc + vec3(t * (cosf(an) * 2.3f), sinf(an) * 2.3f) - g.org, vec3(n, 0), vec3(t, 0), vec2(0, 0), rgb(1.f, 0.97f, 0.85f, 0.35f), emMat(EA_NIGHT));
        }
        for (int s = 0; s < 20; s++) {
            vec3 fn = cross(g.m->verts[b0 + 1 + s].pos - g.m->verts[b0].pos, g.m->verts[b0 + 2 + s].pos - g.m->verts[b0].pos);
            if (dot(fn, vec3(n, 0)) > 0) g.m->tri(b0, b0 + 1 + s, b0 + 2 + s);
            else g.m->tri(b0, b0 + 2 + s, b0 + 1 + s);
        }
        beam(g, cc + vec3(n * 0.08f, 0), cc + vec3(n * 0.08f + t * 0.9f, 1.2f), 0.14f, 0.05f, rgb(0.05f), M(MAT_METAL_PAINTED), vec3(n, 0));
        beam(g, cc + vec3(n * 0.1f, 0), cc + vec3(n * 0.1f - t * 1.6f, -0.3f), 0.1f, 0.05f, rgb(0.05f), M(MAT_METAL_PAINTED), vec3(n, 0));
    }
    hipRoof(g, tc, side, 7.3f, 7.3f, z0 + tH, 7.5f, tile, M(MAT_ROOF_TILE));
    lathe(g, vec3(tc, z0 + tH + 6.5f), {vec2(1.6f, 0.f), vec2(1.6f, 2.5f), vec2(1.3f, 3.4f), vec2(0.6f, 4.3f), vec2(0.f, 4.8f)}, 10, rgb(0.85f, 0.7f, 0.3f),
          M(MAT_METAL_BRUSHED), false);
    cyl(g, vec3(tc, z0 + tH + 11.2f), 0.08f, 0.04f, 3.f, 5, rgb(0.8f), M(MAT_METAL_BRUSHED), false);
    lamp(g, vec3(tc, z0 + tH + 14.3f), 0.35f, vec3(1.f, 0.08f, 0.04f), 1.f, EA_BLINK, 5);
    // formal lawns: four parterres flanking the central alley toward the fountain, hedged, with palms
    for (int qx = -1; qx <= 1; qx += 2)
        for (int qy = 0; qy < 2; qy++) {
            vec2 lc = e.c + face * (D + 22.f + qy * 44.f) + side * (qx * 38.f);
            std::vector<vec2> lawn = rectPoly(lc, side, 26.f, 17.f);
            polyFlat(g, *g.m, lawn, z0 + 0.06f, rgb(0.62f, 0.98f, 0.5f), M(MAT_GRASS));
            for (int k = 0; k < 4; k++) {
                vec2 a = lawn[k], b = lawn[(k + 1) % 4];
                vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
                if (dot(on, a - lc) < 0) on = -on;
                vec2 ha = a - on * 0.5f, hb = b - on * 0.5f;
                beam(g, vec3(ha, z0 + 0.45f), vec3(hb, z0 + 0.45f), 0.9f, 0.8f, rgb(0.35f, 0.6f, 0.28f), M(MAT_GRASS));
            }
            if (detail) prop(g, vec3(lc, z0), 0.4f * qx, 1.2f, PROP_TREE_OAK, (u8)(qy + (qx > 0 ? 2 : 0)));
        }
    // name over the entrance
    if (detail) {
        const char* t = e.text.c_str();
        float th = 1.3f;
        float tw = textAdvance(t, th, 0.3f);
        vec2 viewR = perp(face);
        vec3 o(e.c + face * (D + 7.35f) - viewR * (tw * 0.5f), z0 + gh + 0.05f);
        strokeText(g, *g.m, t, o, vec3(viewR, 0), vec3(0, 0, 1), th, 0.18f, rgb(0.7f, 0.55f, 0.3f), M(MAT_METAL_BRUSHED), 0.08f, 0.3f);
        // flagpoles
        for (int k = -1; k <= 1; k++) {
            vec2 fp2 = e.c + face * (D + 30.f) + side * (k * 10.f);
            cyl(g, vec3(fp2, z0), 0.12f, 0.06f, 16.f, 6, rgb(0.9f), M(MAT_METAL_BRUSHED), false);
            vec3 fc = k == 0 ? vec3(0.1f, 0.5f, 0.55f) : (k < 0 ? vec3(0.9f, 0.9f, 0.9f) : vec3(0.95f, 0.6f, 0.15f));
            panel2(g, vec3(fp2, z0 + 13.6f), vec3(fp2 + side * 3.2f, z0 + 13.4f), vec3(fp2 + side * 3.2f, z0 + 15.4f), vec3(fp2, z0 + 15.8f), rgbv(fc), M(MAT_FABRIC));
            collide(g, vec3(fp2, z0 + 8.f), side, vec3(0.12f, 0.12f, 8.f));
        }
        // facade uplights and plaza palms
        for (int k = -3; k <= 3; k++)
            light(g, vec3(e.c + face * (D + 9.f) + side * (k * 13.f), z0 + 0.5f), vec3(1.f, 0.85f, 0.6f) * 7000.f, 30.f, 1, normalize(vec3(-face, 1.2f)), 0.25f);
        light(g, vec3(tc + face * 9.f, z0 + H + 1.f), vec3(1.f, 0.85f, 0.6f) * 12000.f, 45.f, 1, normalize(vec3(-face, 1.6f)), 0.2f);
        for (int k = 0; k < 10; k++) {
            float u = -45.f + k * 10.f;
            for (int s = -1; s <= 1; s += 2) prop(g, vec3(e.c + face * (D + 12.f + (s + 1) * 26.f) + side * u, z0), 0.3f * k, 1.f, PROP_PALM_TALL, (u8)(k & 3));
        }
        for (int k = 0; k < 6; k++) prop(g, vec3(e.c + face * (D + 40.f) + side * (-25.f + k * 10.f), z0), kPi, 1.f, PROP_BENCH);
    }
}

// ------------------------------------------------------------------------------------------------ fountain
void genFountain(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    float R = e.p[0];
    float z = e.z;
    bool big = e.variant == 0;
    int seg = detail ? 40 : 16;
    u32 stone = rgb(0.95f, 0.93f, 0.88f), water = rgb(0.35f, 0.65f, 0.7f), foam = rgb(0.85f, 0.95f, 1.f);
    // basin wall, floor, water surface
    lathe(g, vec3(e.c, z - 0.2f), {vec2(R, 0.f), vec2(R, 0.9f), vec2(R - 0.7f, 0.9f), vec2(R - 0.7f, 0.2f)}, seg, stone, M(MAT_MARBLE), false);
    polyFlat(g, *g.m, circleFP(e.c, R - 0.7f, seg), z + 0.05f, rgb(0.5f, 0.8f, 0.85f), M(MAT_TILE_POOL));
    polyFlat(g, *g.m, circleFP(e.c, R - 0.7f, seg), z + 0.6f, water, M(MAT_GLASS));
    collide(g, vec3(e.c, z + 0.35f), vec2(1, 0), vec3(R * 0.72f, R * 0.72f, 0.45f));
    // tiered bowls on a pedestal
    float s = big ? 1.f : 0.6f;
    lathe(g, vec3(e.c, z), {vec2(1.6f * s, 0.f), vec2(1.2f * s, 3.2f * s), vec2(5.5f * s, 3.9f * s), vec2(6.f * s, 4.4f * s), vec2(0.8f * s, 4.4f * s),
                             vec2(0.8f * s, 6.5f * s), vec2(3.2f * s, 7.2f * s), vec2(3.5f * s, 7.6f * s), vec2(0.5f * s, 7.6f * s), vec2(0.4f * s, 9.4f * s),
                             vec2(0.9f * s, 9.9f * s), vec2(0.f, 10.5f * s)},
          detail ? 24 : 10, stone, M(MAT_MARBLE), false);
    polyFlat(g, *g.m, circleFP(e.c, 5.6f * s, 20), z + 4.3f * s, water, M(MAT_GLASS));
    polyFlat(g, *g.m, circleFP(e.c, 3.2f * s, 16), z + 7.5f * s, water, M(MAT_GLASS));
    // water curtains falling from the bowl rims (thin translucent-looking sheets)
    lathe(g, vec3(e.c, z + 0.6f), {vec2(6.1f * s, 0.f), vec2(6.05f * s, 3.8f * s)}, detail ? 32 : 12, foam, M(MAT_GLASS), false);
    lathe(g, vec3(e.c, z + 4.4f * s), {vec2(3.6f * s, 0.f), vec2(3.55f * s, 3.2f * s)}, detail ? 24 : 10, foam, M(MAT_GLASS), false);
    // central plume and an arcing jet ring
    lathe(g, vec3(e.c, z + 10.4f * s), {vec2(0.35f, 0.f), vec2(0.25f, big ? 5.f : 2.f), vec2(0.f, big ? 6.f : 2.5f)}, 8, foam, M(MAT_GLASS), false);
    if (detail) {
        int jets = big ? 16 : 10;
        for (int k = 0; k < jets; k++) {
            float a = kTwoPi * k / jets;
            vec2 d(cosf(a), sinf(a));
            vec3 p0(e.c + d * (R - 1.1f), z + 0.9f);
            vec3 prev = p0;
            for (int t = 1; t <= 6; t++) {
                float u = t / 6.f;
                vec3 p(e.c + d * Lerp(R - 1.1f, 6.8f * s, u), z + 0.9f + sinf(u * kPi) * (big ? 3.4f : 2.f) - u * 0.2f);
                rod(g, prev, p, 0.07f, 4, foam, M(MAT_GLASS));
                prev = p;
            }
        }
        // colour-cycling underwater lights
        for (int k = 0; k < 12; k++) {
            float a = kTwoPi * k / 12;
            lamp(g, vec3(e.c + vec2(cosf(a), sinf(a)) * (R - 1.4f), z + 0.12f), 0.35f, vec3(0.3f, 0.7f, 1.f), 0.9f, EA_HUE, (u32)k * 21u);
        }
        light(g, vec3(e.c, z + 2.f), vec3(0.4f, 0.8f, 1.f) * 5000.f, R + 12.f, 2);
        light(g, vec3(e.c, z + 12.f * s), vec3(1.f, 0.95f, 0.85f) * 4000.f, 22.f, 1);
    }
}

// ------------------------------------------------------------------------------------------------ Canvas Park
void genPark(const SiteElem& e, G& g) {
    bool detail = g.detail;
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    float x0 = e.c.x - e.hx, x1 = e.c.x + e.hx, y0 = e.c.y - e.hy, y1 = e.c.y + e.hy;
    // Lawn drape over the part of the park inside this cell (decal layer: no z-fighting with the terrain)
    float step = detail ? 6.f : 18.f;
    float cx0 = Max(x0, mn.x), cx1 = Min(x1, mx.x), cy0 = Max(y0, mn.y), cy1 = Min(y1, mx.y);
    if (cx1 > cx0 && cy1 > cy0) {
        for (float y = cy0; y < cy1 - 0.01f; y += step)
            for (float x = cx0; x < cx1 - 0.01f; x += step) {
                float xa = x, xb = Min(x + step, cx1), ya = y, yb = Min(y + step, cy1);
                float stripe = fmodf(floorf((x - x0) / 12.f), 2.f) < 1.f ? 1.f : 1.12f;
                u32 c = rgb(0.62f * stripe, 0.95f * stripe, 0.5f * stripe);
                auto H = [&](float px, float py) { return gMap->heightAt(px, py) + 0.04f; };
                g.d->quadFacing(vec3(xa, ya, H(xa, ya)) - g.org, vec3(xb, ya, H(xb, ya)) - g.org, vec3(xb, yb, H(xb, yb)) - g.org, vec3(xa, yb, H(xa, yb)) - g.org,
                                vec2(xa, ya), vec2(xb, ya), vec2(xb, yb), vec2(xa, yb), c, M(MAT_GRASS), vec3(0, 0, 1));
            }
    }
    // Paths: perimeter promenade and diagonals toward the fountain plaza (raised paver strips)
    auto path = [&](vec2 a, vec2 b, float w) {
        vec2 d = b - a;
        float L = length(d);
        d = d / L;
        vec2 n = perp(d);
        for (float s = 0.f; s < L; s += 8.f) {
            float s1 = Min(L, s + 8.f);
            vec2 p0 = a + d * s, p1 = a + d * s1;
            if (!g.owns((p0 + p1) * 0.5f)) continue;
            float za = gMap->heightAt(p0.x, p0.y) + 0.1f, zb = gMap->heightAt(p1.x, p1.y) + 0.1f;
            size_t v0 = g.m->verts.size();
            quad(g, *g.m, vec3(p0 - n * w, za), vec3(p1 - n * w, zb), vec3(p1 + n * w, zb), vec3(p0 + n * w, za), rgb(0.9f, 0.88f, 0.86f), M(MAT_PAVERS), vec3(0, 0, 1));
            paverUV(g, *g.m, v0);
            for (int sd = -1; sd <= 1; sd += 2)
                quad(g, *g.m, vec3(p0 + n * (sd * w), za), vec3(p1 + n * (sd * w), zb), vec3(p1 + n * (sd * w), zb - 0.25f), vec3(p0 + n * (sd * w), za - 0.25f),
                     rgb(0.9f), M(MAT_CURB), vec3(n * (float)sd, 0));
        }
    };
    float inset = 8.f;
    vec2 c00(x0 + inset, y0 + inset), c10(x1 - inset, y0 + inset), c11(x1 - inset, y1 - inset), c01(x0 + inset, y1 - inset);
    path(c00, c10, 2.5f);
    path(c10, c11, 2.5f);
    path(c11, c01, 2.5f);
    path(c01, c00, 2.5f);
    float pr = 27.f;
    for (int k = 0; k < 4; k++) {
        vec2 corner = k == 0 ? c00 : (k == 1 ? c10 : (k == 2 ? c11 : c01));
        vec2 d = normalize(e.c - corner);
        path(corner, e.c - d * pr, 2.f);
    }
    path(vec2(e.c.x, y0 + inset), vec2(e.c.x, e.c.y - pr), 2.f);
    path(vec2(e.c.x, e.c.y + pr), vec2(e.c.x, y1 - inset), 2.f);
    // fountain plaza disc
    if (g.owns(e.c)) {
        std::vector<vec2> disc = circleFP(e.c, pr, detail ? 40 : 16);
        size_t v0 = g.m->verts.size();
        polyFlat(g, *g.m, disc, e.z + 0.1f, rgb(0.92f, 0.9f, 0.88f), M(MAT_PAVERS));
        paverUV(g, *g.m, v0);
        lathe(g, vec3(e.c, e.z - 0.2f), {vec2(pr, 0.f), vec2(pr, 0.3f)}, detail ? 40 : 16, rgb(0.9f), M(MAT_CURB), false);
    }
    // Bandshell (north side), sculpture (south-east), playground (south-west)
    vec2 bs(e.c.x, y1 - 38.f);
    if (g.owns(bs)) {
        float r = 13.f;
        MeshData& m = *g.m;
        int seg = detail ? 16 : 8, rings = detail ? 8 : 4;
        u32 b0 = (u32)m.verts.size();
        for (int i = 0; i <= rings; i++) {
            float phi = kHalfPi * i / rings;
            for (int k = 0; k <= seg; k++) {
                float th = kPi * k / seg;  // half dome opening to the south
                vec3 dir(cosf(th) * cosf(phi), sinf(th) * cosf(phi), sinf(phi));
                m.addVertex(vec3(bs, e.z + 1.2f) + dir * r - g.org, dir, vec3(-sinf(th), cosf(th), 0), vec2(0, 0), rgb(0.97f, 0.97f, 0.95f), M(MAT_PLASTER));
            }
        }
        for (int i = 0; i < rings; i++)
            for (int k = 0; k < seg; k++) {
                u32 a = b0 + i * (seg + 1) + k, b = a + 1, c = a + seg + 1, d = c + 1;
                m.quadIdx(a, c, d, b);
                m.quadIdx(a, b, d, c);
            }
        polyFlat(g, m, circleFP(bs, r, 20), e.z + 1.2f, rgb(0.6f, 0.45f, 0.3f), M(MAT_WOOD));
        lathe(g, vec3(bs, e.z), {vec2(r, 0.f), vec2(r, 1.2f)}, 20, rgb(0.9f), M(MAT_CONCRETE), false);
        collide(g, vec3(bs, e.z + 0.6f), vec2(1, 0), vec3(r * 0.7f, r * 0.7f, 0.6f));
        for (int k = 0; k < 7; k++) {
            float th = kPi * (k + 0.5f) / 7;
            lamp(g, vec3(bs + vec2(cosf(th), sinf(th)) * (r * 0.92f), e.z + 1.2f + r * 0.35f), 0.3f, vec3(1.f, 0.8f, 0.5f), 0.8f, EA_NIGHT);
        }
        light(g, vec3(bs + vec2(0, -4.f), e.z + 8.f), vec3(1.f, 0.85f, 0.6f) * 6000.f, 24.f, 1, vec3(0, 0, -1), 0.3f);
    }
    vec2 sc(x1 - 40.f, y0 + 40.f);
    if (g.owns(sc)) {
        // "Tide Totem": stacked, twisting coloured slabs
        for (int k = 0; k < 9; k++) {
            float a = k * 0.35f;
            vec3 col = hsvToRgb(k / 9.f, 0.75f, 0.95f);
            boxY(g, vec3(sc, e.z + 0.6f + k * 1.1f), vec2(cosf(a), sinf(a)), vec3(2.2f - k * 0.12f, 0.8f, 0.5f), rgbv(col), M(MAT_METAL_PAINTED));
        }
        collide(g, vec3(sc, e.z + 5.f), vec2(1, 0), vec3(1.5f, 1.5f, 5.f));
        light(g, vec3(sc + vec2(3.f, -3.f), e.z + 0.5f), vec3(1.f, 0.6f, 0.9f) * 3000.f, 14.f, 2, normalize(vec3(-0.5f, 0.5f, 1.f)), 0.3f);
    }
    vec2 pg(x0 + 45.f, y0 + 45.f);
    if (g.owns(pg) && detail) {
        polyFlat(g, *g.m, rectPoly(pg, vec2(1, 0), 14.f, 11.f), gMap->heightAt(pg.x, pg.y) + 0.08f, rgb(0.9f, 0.4f, 0.3f), M(MAT_RUBBER));
        float z = gMap->heightAt(pg.x, pg.y);
        boxY(g, vec3(pg + vec2(-5.f, 2.f), z + 1.5f), vec2(1, 0), vec3(2.f, 2.f, 0.1f), rgb(0.2f, 0.6f, 0.9f), M(MAT_METAL_PAINTED), true);
        for (int k = 0; k < 4; k++) cyl(g, vec3(pg + vec2(-5.f + ((k & 1) ? 1.8f : -1.8f), 2.f + ((k & 2) ? 1.8f : -1.8f)), z), 0.1f, 0.1f, 3.2f, 6, rgb(0.95f, 0.8f, 0.1f),
                                        M(MAT_METAL_PAINTED), false);
        beam(g, vec3(pg + vec2(-3.f, 2.f), z + 1.5f), vec3(pg + vec2(3.f, 2.f), z + 0.1f), 1.f, 0.1f, rgb(0.95f, 0.3f, 0.2f), M(MAT_METAL_PAINTED));
        for (int s = 0; s < 2; s++) {
            vec2 sp = pg + vec2(4.f + s * 3.f, -4.f);
            beam(g, vec3(sp + vec2(-1.f, 0), z), vec3(sp, z + 2.6f), 0.1f, 0.1f, rgb(0.2f, 0.7f, 0.3f), M(MAT_METAL_PAINTED));
            beam(g, vec3(sp + vec2(1.f, 0), z), vec3(sp, z + 2.6f), 0.1f, 0.1f, rgb(0.2f, 0.7f, 0.3f), M(MAT_METAL_PAINTED));
        }
    }
    // Trees, benches and lamps (near LOD only; props need owner cells)
    if (!detail) return;
    Rng r(e.seed ^ (u32)(g.cx * 73856093) ^ (u32)(g.cy * 19349663));
    for (float t = 0.f; t < 1.f; t += 1.f / 28.f) {
        // royal palms along the perimeter promenade
        for (int side = 0; side < 4; side++) {
            vec2 a = side == 0 ? c00 : (side == 1 ? c10 : (side == 2 ? c11 : c01));
            vec2 b = side == 0 ? c10 : (side == 1 ? c11 : (side == 2 ? c01 : c00));
            vec2 p = lerp(a, b, t) + normalize(e.c - lerp(a, b, 0.5f)) * 5.f;
            if (g.owns(p)) prop(g, vec3(p, gMap->heightAt(p.x, p.y)), t * 20.f, 1.05f, PROP_PALM_TALL, (u8)(side & 3));
        }
    }
    for (int k = 0; k < 90; k++) {
        vec2 p(r.range(x0 + 16.f, x1 - 16.f), r.range(y0 + 16.f, y1 - 16.f));
        if (!g.owns(p)) continue;
        if (length(p - e.c) < pr + 8.f || length(p - bs) < 20.f || length(p - sc) < 8.f || length(p - pg) < 18.f) continue;
        // keep off the diagonal paths
        bool onPath = false;
        for (int c = 0; c < 4 && !onPath; c++) {
            vec2 corner = c == 0 ? c00 : (c == 1 ? c10 : (c == 2 ? c11 : c01));
            if (distPointSegment2D(p, corner, e.c) < 6.f) onPath = true;
        }
        if (fabsf(p.x - e.c.x) < 6.f) onPath = true;
        if (onPath) continue;
        PropType t = r.chance(0.45f) ? PROP_TREE_OAK : (r.chance(0.6f) ? PROP_PALM : PROP_BUSH);
        prop(g, vec3(p, gMap->heightAt(p.x, p.y)), r.f() * kTwoPi, r.range(0.9f, 1.3f), t, (u8)r.irange(0, 3));
    }
    // lamps and benches along the diagonals
    for (int c = 0; c < 4; c++) {
        vec2 corner = c == 0 ? c00 : (c == 1 ? c10 : (c == 2 ? c11 : c01));
        vec2 d = normalize(e.c - corner), n = perp(d);
        float L = length(e.c - corner) - pr;
        for (float s = 12.f; s < L; s += 24.f) {
            vec2 lp = corner + d * s + n * 3.2f;
            if (!g.owns(lp)) continue;
            float z = gMap->heightAt(lp.x, lp.y);
            cyl(g, vec3(lp, z), 0.09f, 0.07f, 4.2f, 6, rgb(0.15f, 0.2f, 0.18f), M(MAT_METAL_PAINTED), false);
            lathe(g, vec3(lp, z + 4.2f), {vec2(0.05f, 0.f), vec2(0.32f, 0.2f), vec2(0.3f, 0.55f), vec2(0.f, 0.7f)}, 8, rgb(1.f, 0.92f, 0.75f, 0.55f), emMat(EA_NIGHT), false);
            light(g, vec3(lp, z + 4.4f), vec3(1.f, 0.85f, 0.6f) * 2500.f, 16.f, 0);
            vec2 bp = corner + d * (s + 12.f) - n * 3.4f;
            if (g.owns(bp)) prop(g, vec3(bp, gMap->heightAt(bp.x, bp.y)), atan2f(n.y, n.x) + kHalfPi, 1.f, PROP_BENCH);
        }
    }
}

// ------------------------------------------------------------------------------------------------ billboards
struct Ad {
    const char* head;
    const char* sub;
    vec3 bg0, bg1, text;
    int graphic;  // 0 sun, 1 palm, 2 waves, 3 bottle, 4 plane, 5 car, 6 wave-note, 7 star
};
const Ad kAds[24] = {
    {"GATOR GLOW", "THE TASTE OF THE SWAMP", vec3(0.05f, 0.35f, 0.12f), vec3(0.1f, 0.6f, 0.2f), vec3(1.f, 0.9f, 0.15f), 3},
    {"SUNRAY CREDIT UNION", "BANKING WITH A TAN", vec3(0.08f, 0.25f, 0.55f), vec3(0.95f, 0.55f, 0.15f), vec3(1.f), 0},
    {"VISIT KEY SOLANO", "ONLY 40 MILES OF BRIDGES AWAY", vec3(0.1f, 0.65f, 0.7f), vec3(0.95f, 0.9f, 0.7f), vec3(1.f), 1},
    {"PELICAN PIZZA", "HOT SLICES. COLD WAVES.", vec3(0.8f, 0.1f, 0.08f), vec3(1.f, 0.95f, 0.85f), vec3(1.f), 2},
    {"TIDEWATER STADIUM", "SEASON TICKETS ON SALE", vec3(0.03f, 0.2f, 0.25f), vec3(0.1f, 0.55f, 0.6f), vec3(1.f), 7},
    {"LA ESQUINA CAFE", "A CUBANITO ON EVERY CORNER", vec3(0.35f, 0.2f, 0.1f), vec3(0.95f, 0.85f, 0.65f), vec3(1.f, 0.95f, 0.85f), 0},
    {"SOL ATLANTIC", "FLY SOUTH FOR LESS", vec3(0.85f, 0.18f, 0.12f), vec3(1.f, 0.78f, 0.1f), vec3(1.f), 4},
    {"PALMERA AIR", "NONSTOP TO EVERYWHERE", vec3(0.03f, 0.4f, 0.45f), vec3(0.95f, 0.95f, 0.95f), vec3(1.f, 0.75f, 0.2f), 4},
    {"SOLAR 104.5 FM", "THE SOUND OF PORTO SOL", vec3(0.45f, 0.05f, 0.45f), vec3(0.95f, 0.2f, 0.6f), vec3(1.f), 6},
    {"MANGROVE MOTORS", "DRIVE SOMETHING WILD", vec3(0.05f, 0.05f, 0.05f), vec3(0.5f, 0.85f, 0.1f), vec3(0.6f, 1.f, 0.2f), 5},
    {"CASA MAREA RESORT", "YOUR ROOM IS WAITING", vec3(0.95f, 0.5f, 0.55f), vec3(1.f, 0.85f, 0.75f), vec3(1.f), 1},
    {"BAYSIDE BANK", "WE NEVER CLOSE", vec3(0.05f, 0.1f, 0.3f), vec3(0.85f, 0.7f, 0.3f), vec3(0.95f, 0.85f, 0.5f), 7},
    {"HURRICANE", "CATEGORY 5 ENERGY DRINK", vec3(0.1f, 0.1f, 0.12f), vec3(0.1f, 0.5f, 1.f), vec3(0.3f, 0.85f, 1.f), 3},
    {"DR REYES DENTAL", "SMILE. YOU'RE IN PORTO SOL", vec3(0.95f, 0.97f, 1.f), vec3(0.1f, 0.6f, 0.65f), vec3(0.05f, 0.35f, 0.4f), 0},
    {"CASH CRAB", "MONEY FAST. QUESTIONS NEVER.", vec3(1.f, 0.85f, 0.1f), vec3(0.1f, 0.1f, 0.1f), vec3(0.1f, 0.1f, 0.1f), 7},
    {"FLAMINGO LAUNDRY", "FRESH AS A SEA BREEZE", vec3(1.f, 0.55f, 0.7f), vec3(1.f, 0.95f, 0.97f), vec3(1.f), 2},
    {"OKAHATCHEE BASS FEST", "THIS SATURDAY AT THE LAKE", vec3(0.1f, 0.4f, 0.2f), vec3(0.2f, 0.5f, 0.8f), vec3(1.f, 0.95f, 0.8f), 2},
    {"SOLARIS ONE", "PREMIUM OFFICES NOW LEASING", vec3(0.08f, 0.07f, 0.05f), vec3(0.85f, 0.6f, 0.2f), vec3(1.f, 0.8f, 0.4f), 0},
    {"PORT ISLE CRUISES", "SAIL AWAY TONIGHT", vec3(0.05f, 0.2f, 0.5f), vec3(0.9f, 0.95f, 1.f), vec3(1.f), 2},
    {"BURGER BAY", "DOUBLE TIDE MELT 4.99", vec3(0.85f, 0.1f, 0.05f), vec3(1.f, 0.8f, 0.1f), vec3(1.f, 0.95f, 0.3f), 7},
    {"GLADEHOPPER AIRBOATS", "MEET A GATOR TODAY", vec3(0.3f, 0.35f, 0.1f), vec3(0.95f, 0.55f, 0.1f), vec3(1.f), 1},
    {"HURT? CALL SAL", "LAW OFFICES OF SAL MORENO", vec3(0.05f, 0.15f, 0.45f), vec3(0.95f, 0.95f, 0.95f), vec3(1.f, 0.85f, 0.2f), 7},
    {"FRESHMART", "OPEN 24 HOURS", vec3(0.1f, 0.5f, 0.2f), vec3(0.95f, 0.97f, 0.95f), vec3(1.f), 0},
    {"NEON TIDE NIGHTS", "SOL BEACH - EVERY FRIDAY", vec3(0.2f, 0.05f, 0.35f), vec3(0.1f, 0.8f, 0.9f), vec3(1.f, 0.4f, 0.9f), 6},
};

// Ad graphic (sun, palm, waves, ...) centred at gc with size gs in the plane (rt, up), facing n; em = emissive strength.
void adGraphic(G& g, const Ad& ad, vec3 gc, float gs, vec3 rt, vec3 up, vec3 n, float em) {
    MeshData& m = *g.m;
    vec3 gcol = ad.graphic == 5 ? ad.text : vec3(1.f, 0.85f, 0.25f);
    auto disc = [&](vec3 c, float r, vec3 col, int segs) {
        u32 b0 = m.addVertex(c - g.org, n, rt, vec2(0, 0), rgbv(col, em), emMat(EA_NIGHT));
        for (int k = 0; k <= segs; k++) {
            float a = kTwoPi * k / segs;
            m.addVertex(c + rt * (cosf(a) * r) + up * (sinf(a) * r) - g.org, n, rt, vec2(0, 0), rgbv(col, em), emMat(EA_NIGHT));
        }
        for (int k = 0; k < segs; k++) {
            vec3 f2 = cross(m.verts[b0 + 1 + k].pos - m.verts[b0].pos, m.verts[b0 + 2 + k].pos - m.verts[b0].pos);
            if (dot(f2, n) > 0) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
            else m.tri(b0, b0 + 2 + k, b0 + 1 + k);
        }
    };
    auto stroke = [&](vec3 a, vec3 b, float w, vec3 col) {
        vec3 d = normalize(b - a);
        vec3 s = normalize(cross(n, d)) * (w * 0.5f);
        m.quadFacing(a - s - g.org, b - s - g.org, b + s - g.org, a + s - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(col, em), emMat(EA_NIGHT), n);
    };
    switch (ad.graphic) {
        case 0:  // sun with rays
            disc(gc, gs * 0.45f, vec3(1.f, 0.8f, 0.2f), 16);
            for (int k = 0; k < 10; k++) {
                float a = kTwoPi * k / 10;
                vec3 d = rt * cosf(a) + up * sinf(a);
                stroke(gc + d * (gs * 0.55f), gc + d * (gs * 0.8f), gs * 0.08f, vec3(1.f, 0.85f, 0.3f));
            }
            break;
        case 1:  // palm silhouette
            stroke(gc - up * (gs * 0.9f), gc + up * (gs * 0.4f) + rt * (gs * 0.12f), gs * 0.1f, vec3(0.1f, 0.1f, 0.08f));
            for (int k = 0; k < 6; k++) {
                float a = kPi * (0.1f + 0.8f * k / 5.f);
                vec3 d = rt * cosf(a) + up * (sinf(a) * 0.5f);
                vec3 top = gc + up * (gs * 0.4f) + rt * (gs * 0.12f);
                stroke(top, top + d * (gs * 0.7f) - up * (gs * 0.15f), gs * 0.09f, vec3(0.08f, 0.3f, 0.1f));
            }
            break;
        case 2:  // waves
            for (int r = 0; r < 3; r++)
                for (int k = 0; k < 6; k++) {
                    float x0 = -1.f + k / 3.f, x1 = x0 + 1.f / 3.f;
                    vec3 a = gc + rt * (x0 * gs) + up * ((r - 1) * gs * 0.35f + sinf(x0 * 6.f) * gs * 0.08f);
                    vec3 b = gc + rt * (x1 * gs) + up * ((r - 1) * gs * 0.35f + sinf(x1 * 6.f) * gs * 0.08f);
                    stroke(a, b, gs * 0.09f, vec3(0.85f, 0.95f, 1.f));
                }
            break;
        case 3:  // bottle
            stroke(gc - up * (gs * 0.8f), gc + up * (gs * 0.3f), gs * 0.45f, gcol);
            stroke(gc + up * (gs * 0.3f), gc + up * (gs * 0.75f), gs * 0.16f, gcol);
            break;
        case 4:  // airliner silhouette
            stroke(gc - rt * (gs * 0.9f), gc + rt * (gs * 0.9f), gs * 0.18f, vec3(1.f));
            stroke(gc + rt * (gs * 0.1f), gc - rt * (gs * 0.25f) + up * (gs * 0.65f), gs * 0.14f, vec3(1.f));
            stroke(gc + rt * (gs * 0.1f), gc - rt * (gs * 0.25f) - up * (gs * 0.65f), gs * 0.14f, vec3(1.f));
            stroke(gc - rt * (gs * 0.75f), gc - rt * (gs * 0.95f) + up * (gs * 0.35f), gs * 0.1f, vec3(1.f));
            break;
        case 5:  // car silhouette
            stroke(gc - rt * (gs * 0.9f) - up * (gs * 0.2f), gc + rt * (gs * 0.9f) - up * (gs * 0.2f), gs * 0.35f, gcol);
            stroke(gc - rt * (gs * 0.4f) + up * (gs * 0.12f), gc + rt * (gs * 0.3f) + up * (gs * 0.12f), gs * 0.3f, gcol);
            disc(gc - rt * (gs * 0.55f) - up * (gs * 0.42f), gs * 0.16f, vec3(0.1f), 10);
            disc(gc + rt * (gs * 0.55f) - up * (gs * 0.42f), gs * 0.16f, vec3(0.1f), 10);
            break;
        case 6:  // music notes
            for (int k = 0; k < 2; k++) {
                vec3 base = gc + rt * ((k - 0.5f) * gs * 0.8f) - up * (gs * 0.4f);
                disc(base, gs * 0.18f, vec3(1.f), 10);
                stroke(base + rt * (gs * 0.16f), base + rt * (gs * 0.16f) + up * (gs * 0.9f), gs * 0.07f, vec3(1.f));
            }
            stroke(gc + rt * (-gs * 0.24f) + up * (gs * 0.5f), gc + rt * (gs * 0.56f) + up * (gs * 0.5f), gs * 0.12f, vec3(1.f));
            break;
        default:  // star burst
            for (int k = 0; k < 5; k++) {
                float a = kHalfPi + kTwoPi * k / 5;
                stroke(gc, gc + (rt * cosf(a) + up * sinf(a)) * (gs * 0.8f), gs * 0.22f, gcol);
            }
            break;
    }
}


// One billboard face: background bands, graphic, headline and sub line. o = face center, rt/up axes, n = normal
void adFace(G& g, const Ad& ad, vec3 o, vec3 rt, vec3 up, vec3 n, float W, float H, bool detail) {
    MeshData& m = *g.m;
    // background: two-colour diagonal split (vertex colours) with a lower band
    vec3 bl = o - rt * (W * 0.5f) - up * (H * 0.5f), br = o + rt * (W * 0.5f) - up * (H * 0.5f);
    vec3 tr = o + rt * (W * 0.5f) + up * (H * 0.5f), tl = o - rt * (W * 0.5f) + up * (H * 0.5f);
    u32 i0 = m.addVertex(bl - g.org, n, rt, vec2(0, 0), rgbv(ad.bg0, 0.12f), emMat(EA_NIGHT));
    u32 i1 = m.addVertex(br - g.org, n, rt, vec2(1, 0), rgbv(ad.bg1, 0.12f), emMat(EA_NIGHT));
    u32 i2 = m.addVertex(tr - g.org, n, rt, vec2(1, 1), rgbv(ad.bg1, 0.12f), emMat(EA_NIGHT));
    u32 i3 = m.addVertex(tl - g.org, n, rt, vec2(0, 1), rgbv(ad.bg0, 0.12f), emMat(EA_NIGHT));
    vec3 fn = cross(m.verts[i1].pos - m.verts[i0].pos, m.verts[i3].pos - m.verts[i0].pos);
    if (dot(fn, n) > 0) m.quadIdx(i0, i1, i2, i3);
    else m.quadIdx(i0, i3, i2, i1);
    if (!detail) return;
    vec3 off = n * 0.03f;
    float lineW = W * 0.62f;
    // headline, auto-fitted to the text column
    float th = Min(H * 0.26f, lineW / Max(1.f, textAdvance(ad.head, 1.f, 0.3f)));
    float tw = textAdvance(ad.head, th, 0.3f);
    vec3 tc = o - rt * (W * 0.16f);
    strokeText(g, m, ad.head, tc + off - rt * (tw * 0.5f) + up * (H * 0.06f), rt, up, th, th * 0.16f, rgbv(ad.text, 0.35f), emMat(EA_NIGHT), 0.f, 0.3f);
    float sh = Min(H * 0.11f, lineW / Max(1.f, textAdvance(ad.sub, 1.f, 0.3f)));
    float sw = textAdvance(ad.sub, sh, 0.3f);
    strokeText(g, m, ad.sub, tc + off - rt * (sw * 0.5f) - up * (H * 0.28f), rt, up, sh, sh * 0.15f, rgbv(ad.text * 0.9f, 0.3f), emMat(EA_NIGHT), 0.f, 0.3f);
    // graphic in the right third
    adGraphic(g, ad, o + rt * (W * 0.33f) + off * 1.5f, H * 0.36f, rt, up, n, 0.3f);
}

void genBillboard(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    const Ad& ad = kAds[e.variant % 24];
    const Ad& ad2 = kAds[(e.variant + 7) % 24];
    float pole = e.p[0];
    float W = 14.6f, H = 4.9f;
    vec3 base(e.c, e.z);
    u32 steel = rgb(0.55f, 0.57f, 0.6f);
    cyl(g, base - vec3(0, 0, 0.5f), 0.65f, 0.5f, pole + 0.5f, detail ? 10 : 6, steel, M(MAT_METAL_PAINTED), false);
    collide(g, base + vec3(0, 0, pole * 0.5f), vec2(1, 0), vec3(0.6f, 0.6f, pole * 0.5f));
    vec2 face = e.ax;
    vec2 roadDir = e.pts.empty() ? perp(face) : e.pts[0];
    (void)roadDir;
    vec3 up(0, 0, 1);
    // back-to-back faces, each toed toward its traffic
    for (int f = 0; f < 2; f++) {
        vec2 n2 = f == 0 ? face : -face;
        vec2 viewR = perp(n2);
        vec3 n(n2, 0), rt(viewR, 0);
        vec3 c = base + vec3(n2 * 0.6f, pole + H * 0.5f + 0.4f);
        boxY(g, c - n * 0.25f, viewR, vec3(W * 0.5f + 0.2f, 0.2f, H * 0.5f + 0.2f), steel, M(MAT_METAL_PAINTED), true);
        adFace(g, f == 0 ? ad : ad2, c + n * 0.0f, rt, up, n, W, H, detail);
        // catwalk and lamps
        boxY(g, c + n * 0.5f - up * (H * 0.5f + 0.35f), viewR, vec3(W * 0.5f, 0.5f, 0.06f), steel, M(MAT_METAL_PAINTED), true);
        for (int k = -1; k <= 1; k++) {
            vec3 lp = c + rt * (k * W * 0.33f) + n * 1.4f - up * (H * 0.5f + 0.2f);
            if (detail) beam(g, lp - n * 1.2f, lp, 0.08f, 0.08f, steel, M(MAT_METAL_PAINTED));
            lamp(g, lp, 0.35f, vec3(1.f, 0.95f, 0.85f), 0.7f, EA_NIGHT);
            if (k == 0) light(g, lp + n * 0.3f, vec3(1.f, 0.95f, 0.85f) * 9000.f, 16.f, 1, normalize(vec3(-n2 * 0.5f, 1.f)), 0.2f);
        }
    }
}

// Portrait poster (bus-shelter lightbox, wall ad): vertical two-colour background, graphic on top, headline wrapped onto up
// to two lines, sub line at the bottom. Emissive strengths: bg = a, lettering/graphic = a * 1.6.
void splitLine(const char* txt, std::string& l0, std::string& l1) {
    std::string t(txt);
    l0 = t;
    l1.clear();
    if (t.size() <= 11) return;
    size_t best = std::string::npos;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] == ' ' && (best == std::string::npos || std::abs((int)i * 2 - (int)t.size()) < std::abs((int)best * 2 - (int)t.size()))) best = i;
    if (best == std::string::npos) return;
    l0 = t.substr(0, best);
    l1 = t.substr(best + 1);
}

void adPoster(G& g, const Ad& ad, vec3 o, vec3 rt, vec3 up, vec3 n, float W, float H, float a, u32 mat) {
    MeshData& m = *g.m;
    vec3 bl = o - rt * (W * 0.5f) - up * (H * 0.5f), br = o + rt * (W * 0.5f) - up * (H * 0.5f);
    vec3 tr = o + rt * (W * 0.5f) + up * (H * 0.5f), tl = o - rt * (W * 0.5f) + up * (H * 0.5f);
    vec3 ml = o - rt * (W * 0.5f) - up * (H * 0.05f), mr = o + rt * (W * 0.5f) - up * (H * 0.05f);
    // lower block (text area) in bg0, upper block (graphic) in bg1, blended across a narrow band
    vec3 blend = ad.bg0 * 0.6f + ad.bg1 * 0.4f;
    m.quadFacing(bl - g.org, br - g.org, mr - g.org, ml - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(ad.bg0, a), mat, n);
    u32 c0 = rgbv(blend, a), c1 = rgbv(ad.bg1, a);
    u32 i0 = m.addVertex(ml - g.org, n, rt, vec2(0, 0), c0, mat), i1 = m.addVertex(mr - g.org, n, rt, vec2(1, 0), c0, mat);
    u32 i2 = m.addVertex(tr - g.org, n, rt, vec2(1, 1), c1, mat), i3 = m.addVertex(tl - g.org, n, rt, vec2(0, 1), c1, mat);
    if (dot(cross(mr - ml, tl - ml), n) > 0) m.quadIdx(i0, i1, i2, i3);
    else m.quadIdx(i0, i3, i2, i1);
    vec3 off = n * Min(W * 0.01f, 0.025f);
    float ta = a * 1.6f;
    float lineW = W * 0.84f;
    std::string h0, h1, s0, s1;
    splitLine(ad.head, h0, h1);
    splitLine(ad.sub, s0, s1);
    float adv = Max(textAdvance(h0.c_str(), 1.f, 0.3f), h1.empty() ? 0.f : textAdvance(h1.c_str(), 1.f, 0.3f));
    float th = Min(H * 0.1f, lineW / Max(1.f, adv));
    float y = -H * 0.12f;
    for (int l = 0; l < 2; l++) {
        const std::string& t = l ? h1 : h0;
        if (t.empty()) continue;
        float tw = textAdvance(t.c_str(), th, 0.3f);
        strokeText(g, m, t.c_str(), o + off - rt * (tw * 0.5f) + up * (y - th * 0.5f), rt, up, th, th * 0.17f, rgbv(ad.text, ta), mat, 0.f, 0.3f);
        y -= th * 1.35f;
    }
    float sadv = Max(textAdvance(s0.c_str(), 1.f, 0.3f), s1.empty() ? 0.f : textAdvance(s1.c_str(), 1.f, 0.3f));
    float sh = Min(H * 0.045f, lineW / Max(1.f, sadv));
    float sy = -H * 0.5f + H * 0.06f + (s1.empty() ? 0.f : sh * 1.4f);
    for (int l = 0; l < 2; l++) {
        const std::string& t = l ? s1 : s0;
        if (t.empty()) continue;
        float sw = textAdvance(t.c_str(), sh, 0.3f);
        strokeText(g, m, t.c_str(), o + off - rt * (sw * 0.5f) + up * sy, rt, up, sh, sh * 0.16f, rgbv(ad.text * 0.9f, ta), mat, 0.f, 0.3f);
        sy -= sh * 1.4f;
    }
    size_t v0 = m.verts.size();
    adGraphic(g, ad, o + up * (H * 0.24f) + off * 1.5f, Min(W * 0.36f, H * 0.2f), rt, up, n, ta);
    for (size_t k = v0; k < m.verts.size(); k++) m.verts[k].mat = (m.verts[k].mat & 0x80000000u) | (mat & 0x7fffffffu);  // keep the bitangent sign
}

}  // namespace landmark_mesh

// Prop prototypes (propmesh.cpp) reuse the ad art and the stroke font. Prop emissive runs through props.hlsl
// (colour * a * 400 * (0.03 + night) * 6): glow ~0.02 gives a billboard-like lightbox at night and a faint glow by day.
void propAdPanel(MeshData& m, vec3 center, vec3 right, vec3 up, float w, float h, int ad, float glow) {
    sitegeo::G g;
    g.m = &m;
    g.d = &m;
    g.org = vec3(0.f);
    vec3 n = normalize(cross(right, up));
    int idx = ((ad % 24) + 24) % 24;
    landmark_mesh::adPoster(g, landmark_mesh::kAds[idx], center, right, up, n, w, h, glow, makeMat(MAT_EMISSIVE));
}

void propText(MeshData& m, const char* txt, vec3 origin, vec3 right, vec3 up, float h, u32 col, u32 mat) {
    sitegeo::G g;
    g.m = &m;
    g.d = &m;
    g.org = vec3(0.f);
    sitegeo::strokeText(g, m, txt, origin, right, up, h, h * 0.14f, col, mat, 0.f, 0.3f);
}

}  // namespace World
