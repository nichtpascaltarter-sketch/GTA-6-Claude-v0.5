// Port Isle container terminal meshes: quay walls, ship-to-shore gantry cranes, container stacks, straddle carriers,
// docked container ships (original designs), rail yard and lead track, gate complex.
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace port_mesh {

using namespace sitegeo;

// Shipping lines (original): name, box colour, emblem colour, emblem style
struct Line {
    const char* name;
    vec3 color, mark;
    int emblem;  // 0 wave, 1 diamond, 2 sun disc, 3 band, 4 chevron
};
const Line kLines[] = {
    {"TIDELINE", vec3(0.08f, 0.45f, 0.5f), vec3(0.95f), 0},     {"CORALCO", vec3(0.78f, 0.25f, 0.18f), vec3(0.95f), 1},
    {"SOLMAR", vec3(0.95f, 0.72f, 0.12f), vec3(0.2f, 0.15f, 0.1f), 2}, {"MERIDIAN", vec3(0.12f, 0.2f, 0.5f), vec3(0.95f), 3},
    {"GREENWAKE", vec3(0.2f, 0.45f, 0.22f), vec3(0.95f), 4},    {"HARBORLINK", vec3(0.55f, 0.57f, 0.58f), vec3(0.1f, 0.25f, 0.55f), 3},
    {"ORCALINE", vec3(0.92f, 0.45f, 0.1f), vec3(0.1f), 0},      {"BAYCRATE", vec3(0.45f, 0.12f, 0.14f), vec3(0.95f), 2},
    {"ATLAS BLUE", vec3(0.25f, 0.5f, 0.75f), vec3(0.95f), 1},   {"SUNSHIP", vec3(0.92f, 0.92f, 0.9f), vec3(0.85f, 0.2f, 0.1f), 2},
};
const int kLineCount = (int)(sizeof(kLines) / sizeof(kLines[0]));

// Emblem painted on a container side. o = face center, rt/up = face axes, n = outward normal
void emblem(G& g, int style, vec3 o, vec3 rt, vec3 up, vec3 n, vec3 col, float size) {
    MeshData& m = *g.m;
    u32 c = rgbv(col);
    u32 mat = M(MAT_METAL_PAINTED);
    vec3 off = n * 0.02f;
    switch (style) {
        case 0:  // wave: three strokes
            for (int k = 0; k < 3; k++) {
                vec3 a = o + rt * (-size + k * size * 0.66f) - up * (size * 0.1f), b = a + rt * (size * 0.33f) + up * (size * 0.35f),
                     c2 = b + rt * (size * 0.33f) - up * (size * 0.35f);
                for (int s = 0; s < 2; s++) {
                    vec3 p0 = s ? b : a, p1 = s ? c2 : b;
                    vec3 d = normalize(p1 - p0), w = cross(n, d) * (size * 0.06f);
                    m.quadFacing(p0 - w + off - g.org, p1 - w + off - g.org, p1 + w + off - g.org, p0 + w + off - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), c, mat, n);
                }
            }
            break;
        case 1: {  // diamond
            vec3 p0 = o - rt * size, p1 = o - up * (size * 0.6f), p2 = o + rt * size, p3 = o + up * (size * 0.6f);
            m.quadFacing(p0 + off - g.org, p1 + off - g.org, p2 + off - g.org, p3 + off - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), c, mat, n);
            break;
        }
        case 2: {  // sun disc
            u32 b0 = m.addVertex(o + off - g.org, n, rt, vec2(0, 0), c, mat);
            for (int k = 0; k <= 10; k++) {
                float a = kTwoPi * k / 10;
                m.addVertex(o + off + rt * (cosf(a) * size * 0.55f) + up * (sinf(a) * size * 0.55f) - g.org, n, rt, vec2(0, 0), c, mat);
            }
            for (int k = 0; k < 10; k++) {
                vec3 fn = cross(m.verts[b0 + 1 + k].pos - m.verts[b0].pos, m.verts[b0 + 2 + k].pos - m.verts[b0].pos);
                if (dot(fn, n) > 0) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
                else m.tri(b0, b0 + 2 + k, b0 + 1 + k);
            }
            break;
        }
        case 3: {  // band
            vec3 p0 = o - rt * (size * 2.6f) - up * (size * 0.15f), p1 = o + rt * (size * 2.6f) - up * (size * 0.15f);
            m.quadFacing(p0 + off - g.org, p1 + off - g.org, p1 + up * (size * 0.3f) + off - g.org, p0 + up * (size * 0.3f) + off - g.org, vec2(0, 0), vec2(1, 0),
                         vec2(1, 1), vec2(0, 1), c, mat, n);
            break;
        }
        default: {  // chevron
            for (int s = -1; s <= 1; s += 2) {
                vec3 a = o + rt * (s * size * 0.8f) + up * (size * 0.4f), b = o - up * (size * 0.4f);
                vec3 d = normalize(b - a), w = cross(n, d) * (size * 0.12f);
                m.quadFacing(a - w + off - g.org, b - w + off - g.org, b + w + off - g.org, a + w + off - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), c, mat, n);
            }
            break;
        }
    }
}

// One shipping container: box with corrugated long sides, door end, roof. faces bitmask: 1 -Y,2 +Y,4 -X,8 +X,16 top
void container(G& g, vec3 c, vec2 ax, float len, float wid, float hgt, vec3 col, u32 faces, bool doorsAtPlusX) {
    vec3 X(ax, 0), Y(perp(ax), 0), Z(0, 0, 1);
    float hx = len * 0.5f, hy = wid * 0.5f;
    MeshData& m = *g.m;
    u32 body = rgbv(col), ends = rgbv(col * 0.85f);
    u32 corr = M(MAT_CORRUGATED), paint = M(MAT_METAL_PAINTED);
    vec3 b = c - Z * (hgt * 0.5f);
    auto face = [&](vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 n, u32 cc, u32 mat, float ul, float vl) {
        m.quadFacing(p0 - g.org, p1 - g.org, p2 - g.org, p3 - g.org, vec2(0, 0), vec2(ul, 0), vec2(ul, vl), vec2(0, vl), cc, mat, n);
    };
    if (faces & 1) face(b - X * hx - Y * hy, b + X * hx - Y * hy, b + X * hx - Y * hy + Z * hgt, b - X * hx - Y * hy + Z * hgt, -Y, body, corr, len, hgt);
    if (faces & 2) face(b + X * hx + Y * hy, b - X * hx + Y * hy, b - X * hx + Y * hy + Z * hgt, b + X * hx + Y * hy + Z * hgt, Y, body, corr, len, hgt);
    if (faces & 4) face(b - X * hx + Y * hy, b - X * hx - Y * hy, b - X * hx - Y * hy + Z * hgt, b - X * hx + Y * hy + Z * hgt, -X, doorsAtPlusX ? body : ends, paint, wid, hgt);
    if (faces & 8) face(b + X * hx - Y * hy, b + X * hx + Y * hy, b + X * hx + Y * hy + Z * hgt, b + X * hx - Y * hy + Z * hgt, X, doorsAtPlusX ? ends : body, paint, wid, hgt);
    if (faces & 16) face(b - X * hx - Y * hy + Z * hgt, b + X * hx - Y * hy + Z * hgt, b + X * hx + Y * hy + Z * hgt, b - X * hx + Y * hy + Z * hgt, Z, rgbv(col * 0.92f), paint, len, wid);
    // door locking bars on the door end (near LOD)
    if (g.detail && (faces & (doorsAtPlusX ? 8 : 4))) {
        vec3 dn = doorsAtPlusX ? X : -X;
        vec3 ec = b + dn * (hx + 0.03f);
        for (int k = 0; k < 4; k++) {
            float yy = -hy + wid * (0.15f + k * 0.233f);
            vec3 p = ec + Y * yy;
            m.quadFacing(p - Y * 0.03f - g.org, p + Y * 0.03f - g.org, p + Y * 0.03f + Z * hgt - g.org, p - Y * 0.03f + Z * hgt - g.org, vec2(0, 0), vec2(1, 0),
                         vec2(1, 1), vec2(0, 1), rgb(0.3f, 0.3f, 0.3f), paint, dn);
        }
    }
}

// ------------------------------------------------------------------------------------------------ quay walls
void genQuay(const SiteElem& e, G& g) {
    vec2 a = e.a, b = e.b;
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    if (!clipSegment(a, b, mn, mx)) return;
    vec2 d = normalize(e.b - e.a), on(e.p[0], e.p[1]);
    float z = e.z;
    float L = length(b - a);
    if (L < 0.5f) return;
    // wall face: dry concrete, a dark tidal band and weed-covered concrete below the waterline (reads dark through the water),
    // capping beam, safety line
    quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(b, 0.9f), vec3(a, 0.9f), rgb(0.62f, 0.6f, 0.56f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a, 0.9f), vec3(b, 0.9f), vec3(b, -0.4f), vec3(a, -0.4f), rgb(0.27f, 0.26f, 0.21f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a, -0.4f), vec3(b, -0.4f), vec3(b, -10.f), vec3(a, -10.f), rgb(0.06f, 0.08f, 0.065f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a, z + 0.35f), vec3(b, z + 0.35f), vec3(b, z), vec3(a, z), rgb(0.85f, 0.84f, 0.8f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a - on * 0.8f, z + 0.35f), vec3(b - on * 0.8f, z + 0.35f), vec3(b, z + 0.35f), vec3(a, z + 0.35f), rgb(0.85f, 0.84f, 0.8f), M(MAT_CONCRETE),
         vec3(0, 0, 1));
    quad(g, *g.m, vec3(a - on * 0.8f, z), vec3(b - on * 0.8f, z), vec3(b - on * 0.8f, z + 0.35f), vec3(a - on * 0.8f, z + 0.35f), rgb(0.85f, 0.84f, 0.8f),
         M(MAT_CONCRETE), vec3(-on, 0));
    if (g.detail) {
        paintLine(g, a - on * 1.4f, b - on * 1.4f, 0.25f, z + 0.012f, rgb(1.f, 0.85f, 0.1f), M(MAT_PAINT_YELLOW));
        // bollards and (on berths) fenders every 18 m, placed on a global grid so neighbouring cells line up
        float s0 = ceilf(dot(a - e.a, d) / 18.f) * 18.f, s1 = dot(b - e.a, d);
        for (float s = s0; s <= s1; s += 18.f) {
            vec2 p = e.a + d * s - on * 0.45f;
            cyl(g, vec3(p, z + 0.35f), 0.28f, 0.22f, 0.6f, 8, rgb(0.15f, 0.15f, 0.16f), M(MAT_METAL_PAINTED), false);
            cyl(g, vec3(p, z + 0.95f), 0.36f, 0.36f, 0.12f, 8, rgb(0.15f, 0.15f, 0.16f), M(MAT_METAL_PAINTED), true);
            if (e.variant == 1) {
                vec2 fp = e.a + d * (s + 9.f) + on * 0.9f;
                if (dot(fp - e.a, d) <= s1) {
                    boxY(g, vec3(fp, z - 1.5f), d, vec3(1.1f, 0.9f, 2.4f), rgb(0.08f, 0.08f, 0.08f), M(MAT_RUBBER));
                }
            }
        }
        collide(g, vec3((a + b) * 0.5f - on * 0.4f, z + 0.17f), d, vec3(L * 0.5f, 0.4f, 0.17f));
    }
    if (e.variant == 1) {
        // crane rails on concrete runway beams (landside 45 m, seaside 15 m from the quay edge)
        for (float off : {15.f, 45.f}) {
            vec2 ra = a - on * off, rb = b - on * off;
            paintLine(g, ra, rb, 1.4f, z + 0.01f, rgb(0.7f, 0.68f, 0.64f), M(MAT_CONCRETE));
            beam(g, vec3(ra, z + 0.07f), vec3(rb, z + 0.07f), 0.16f, 0.14f, rgb(0.4f, 0.38f, 0.36f), M(MAT_METAL_BRUSHED));
        }
    }
}

// ------------------------------------------------------------------------------------------------ ship-to-shore crane
void genStsCrane(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 X2 = e.ax, Y2 = perp(e.ax);
    vec3 X(X2, 0), Y(Y2, 0), Z(0, 0, 1);
    vec3 o(e.c, e.z);
    auto P = [&](float x, float y, float z) { return o + X * x + Y * y + Z * z; };
    u32 red = e.variant == 0 ? rgb(0.78f, 0.12f, 0.08f) : rgb(0.93f, 0.93f, 0.92f);
    u32 white = e.variant == 0 ? rgb(0.93f, 0.93f, 0.92f) : rgb(0.78f, 0.12f, 0.08f);
    u32 steel = M(MAT_METAL_PAINTED);
    const float gauge = 15.f, legY = 9.f, portalZ = 46.f, girderZ = 49.f;
    // legs (tapering slightly) + sill beams + bogies
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            beam(g, P(sx * gauge, sy * legY, 2.2f), P(sx * gauge, sy * legY * 0.92f, portalZ), 1.7f, 1.7f, red, steel, X);
            boxY(g, P(sx * gauge, sy * legY, 1.1f), Y2, vec3(3.2f, 1.f, 0.9f), rgb(0.25f), steel);
            if (detail)
                for (int w = -1; w <= 1; w += 2) {
                    vec3 wc = P(sx * gauge, sy * legY + w * 2.2f, 0.45f);
                    rod(g, wc - X * 0.3f, wc + X * 0.3f, 0.42f, 8, rgb(0.15f), M(MAT_METAL_BRUSHED));
                }
            collide(g, P(sx * gauge, sy * legY, portalZ * 0.5f), X2, vec3(1.f, 1.f, portalZ * 0.5f));
        }
    for (int sx = -1; sx <= 1; sx += 2) {
        beam(g, P(sx * gauge, -legY, 12.f), P(sx * gauge, legY, 12.f), 1.2f, 1.6f, red, steel);   // sill / portal tie
        beam(g, P(sx * gauge, -legY, portalZ), P(sx * gauge, legY, portalZ), 1.6f, 2.2f, red, steel);
        if (detail) {
            beam(g, P(sx * gauge, -legY, 12.f), P(sx * gauge, legY * 0.92f, portalZ), 0.6f, 0.6f, red, steel);
            beam(g, P(sx * gauge, legY, 12.f), P(sx * gauge, -legY * 0.92f, portalZ), 0.6f, 0.6f, red, steel);
        }
    }
    for (int sy = -1; sy <= 1; sy += 2) beam(g, P(-gauge, sy * legY * 0.92f, portalZ + 0.8f), P(gauge, sy * legY * 0.92f, portalZ + 0.8f), 1.4f, 2.4f, red, steel);
    // fixed girder over the land side (backreach) with the machinery house
    const float backX = -38.f, outX = 80.f, hingeX = gauge + 1.f;
    for (int sy = -1; sy <= 1; sy += 2) beam(g, P(backX, sy * 3.2f, girderZ), P(hingeX, sy * 3.2f, girderZ), 1.3f, 3.2f, white, steel);
    boxY(g, P(-31.f, 0.f, girderZ + 4.2f), X2, vec3(6.5f, 5.f, 3.f), white, steel, true);
    if (detail) {
        float th = 1.6f;
        const char* t = "PORT ISLE";
        float tw = textAdvance(t, th, 0.3f);
        strokeText(g, *g.m, t, P(-31.f - tw * 0.5f, -5.05f, girderZ + 3.3f), X, Z, th, 0.25f, rgb(0.1f, 0.2f, 0.45f), steel, 0.f, 0.3f);
    }
    // boom: lowered (horizontal over the ship) or raised (about the hinge)
    bool raised = e.p[0] > 0.5f;
    float ang = raised ? 78.f * kDegToRad : 0.f;
    vec3 hinge = P(hingeX, 0.f, girderZ);
    vec3 bdir = X * cosf(ang) + Z * sinf(ang);
    vec3 bup = -X * sinf(ang) + Z * cosf(ang);
    float boomLen = outX - hingeX;
    for (int sy = -1; sy <= 1; sy += 2) {
        vec3 a = hinge + Y * (sy * 3.2f), b = a + bdir * boomLen;
        beam(g, a, b, 1.2f, 2.8f, white, steel, bup);
        if (detail)
            for (float s = 6.f; s < boomLen; s += 8.f)
                beam(g, hinge + bdir * s + Y * (sy * 3.2f) - bup * 1.4f, hinge + bdir * (s + 4.f) + Y * (sy * 3.2f) + bup * 1.4f, 0.25f, 0.25f, white, steel, Y);
    }
    if (detail)
        for (float s = 4.f; s < boomLen; s += 12.f) beam(g, hinge + bdir * s - Y * 3.2f, hinge + bdir * s + Y * 3.2f, 0.4f, 0.4f, white, steel);
    // A-frame apex with fore- and backstays
    vec3 apex = P(-4.f, 0.f, 80.f);
    for (int sy = -1; sy <= 1; sy += 2) {
        beam(g, P(-gauge, sy * 4.f, portalZ + 1.f), apex + Y * (sy * 1.2f), 1.3f, 1.3f, red, steel, X);
        beam(g, P(hingeX - 2.f, sy * 4.f, portalZ + 1.f), apex + Y * (sy * 1.2f), 1.3f, 1.3f, red, steel, X);
        vec3 fs = hinge + bdir * (boomLen * 0.62f) + Y * (sy * 3.2f) + bup * 1.4f;
        rod(g, apex + Y * (sy * 1.2f), fs, 0.18f, 6, rgb(0.3f), M(MAT_METAL_BRUSHED));
        rod(g, apex + Y * (sy * 1.2f), P(backX + 2.f, sy * 3.2f, girderZ + 1.6f), 0.18f, 6, rgb(0.3f), M(MAT_METAL_BRUSHED));
    }
    beam(g, apex - Y * 1.8f, apex + Y * 1.8f, 1.4f, 1.4f, red, steel);
    // trolley + operator cab + spreader (only when the boom is down)
    if (!raised) {
        float tx = Lerp(-20.f, outX - 6.f, Clamp(e.p[1], 0.f, 1.f));
        vec3 tp = P(tx, 0.f, girderZ - 1.9f);
        boxY(g, tp, X2, vec3(3.5f, 3.9f, 1.3f), rgb(0.95f, 0.8f, 0.1f), steel, true);
        boxY(g, tp + X * 1.5f - Z * 2.6f, X2, vec3(1.4f, 1.4f, 1.3f), rgb(0.9f), steel, true);
        quad(g, *g.m, tp + X * 2.92f - Z * 3.7f - Y * 1.3f, tp + X * 2.92f - Z * 3.7f + Y * 1.3f, tp + X * 2.92f - Z * 1.5f + Y * 1.3f, tp + X * 2.92f - Z * 1.5f - Y * 1.3f,
             rgb(0.2f, 0.3f, 0.35f), M(MAT_GLASS), X);
        float sz = tx > gauge + 3.f ? 22.f : 14.f;  // spreader above the ship deck or over the apron
        vec3 sp = P(tx, 0.f, sz);
        if (detail)
            for (int k = 0; k < 4; k++) {
                vec3 off = X * ((k & 1) ? 1.5f : -1.5f) + Y * ((k & 2) ? 1.f : -1.f);
                rod(g, tp - Z * 1.3f + off, sp + off + Z * 0.5f, 0.05f, 4, rgb(0.2f), M(MAT_METAL_BRUSHED));
            }
        boxY(g, sp, Y2, vec3(6.2f, 1.25f, 0.35f), rgb(0.95f, 0.75f, 0.1f), steel, true);
        if (e.p[2] > 0.5f) {
            const Line& ln = kLines[e.seed % kLineCount];
            container(g, sp - Z * 1.65f, Y2, 12.19f, 2.44f, 2.59f, ln.color, 31u, true);
        }
    }
    // Warning / aviation lights and apron floodlights under the girder
    vec3 tip = hinge + bdir * boomLen + Z * 1.6f;
    lamp(g, tip, 0.6f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, e.seed & 255u);
    lamp(g, apex + Z * 1.2f, 0.6f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, (e.seed + 60u) & 255u);
    lamp(g, P(backX, 0.f, girderZ + 2.f), 0.5f, vec3(1.f, 0.06f, 0.03f), 0.9f);
    light(g, apex + Z * 1.2f, vec3(1.f, 0.08f, 0.04f) * 900.f, 18.f, 4);
    for (int k = 0; k < 3; k++) {
        vec3 lp = P(-20.f + k * 22.f, 0.f, girderZ - 2.2f);
        light(g, lp, vec3(1.f, 0.9f, 0.75f) * 22000.f, 70.f, 1, vec3(0, 0, -1), 0.35f);
        if (detail) lamp(g, lp, 0.7f, vec3(1.f, 0.92f, 0.8f), 0.6f, EA_NIGHT);
    }
}

// ------------------------------------------------------------------------------------------------ container stacks
int stackHeight(u32 h, int variant) {
    float r = hashToFloat(h);
    switch (variant) {
        case 1: return r < 0.1f ? 0 : (r < 0.35f ? 1 : (r < 0.75f ? 2 : 3));
        case 2: return r < 0.05f ? 0 : (r < 0.3f ? 4 : (r < 0.7f ? 5 : 6));
        case 3: return r < 0.55f ? 0 : (r < 0.8f ? 1 : 2);
        default: return r < 0.08f ? 0 : (r < 0.22f ? 1 : (r < 0.45f ? 2 : (r < 0.8f ? 3 : 4)));
    }
}

void genContainerBlock(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    int bays = (int)e.p[0], rows = (int)e.p[1];
    vec2 X = e.ax, Y = perp(e.ax);
    const float pitchX = 12.4f, pitchY = 4.3f, CL = 12.19f, CW = 2.44f;
    vec2 origin = e.c - X * (bays * pitchX * 0.5f) - Y * (rows * pitchY * 0.5f);
    // Stack heights and line per slot
    std::vector<int> hgt((size_t)bays * rows);
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < bays; i++) hgt[(size_t)j * bays + i] = stackHeight(hash3i(i, j, (int)e.seed), e.variant);
    auto H = [&](int i, int j) { return (i < 0 || j < 0 || i >= bays || j >= rows) ? 0 : hgt[(size_t)j * bays + i]; };
    auto lineOf = [&](int i, int j, int t) {
        u32 h = hash3i(i + t * 31, j, (int)e.seed ^ 0x77);
        if (e.variant == 1) return -1;  // reefers: white
        if (e.variant == 2) return (int)((hash2i(j, (int)e.seed) >> 4) % kLineCount);  // empties grouped per row
        return (int)(h % kLineCount);
    };
    auto tierH = [&](int i, int j, int t) { return (hash3i(i, j * 7 + t, (int)e.seed) & 1) ? 2.9f : 2.59f; };
    if (!g.detail) {
        // far LOD: one box per run of equal stack height in each row
        for (int j = 0; j < rows; j++) {
            int i = 0;
            while (i < bays) {
                int h = H(i, j);
                int k = i;
                while (k + 1 < bays && H(k + 1, j) == h) k++;
                if (h > 0) {
                    float x0 = i * pitchX, x1 = k * pitchX + CL;
                    vec2 c = origin + X * ((x0 + x1) * 0.5f) + Y * (j * pitchY + CW * 0.5f + 0.9f);
                    int li = lineOf(i, j, 0);
                    vec3 col = li < 0 ? vec3(0.9f, 0.9f, 0.88f) : kLines[li].color;
                    boxY(g, vec3(c, e.z + h * 1.37f), X, vec3((x1 - x0) * 0.5f, CW * 0.5f, h * 1.37f), rgbv(col), M(MAT_CORRUGATED));
                }
                i = k + 1;
            }
        }
        return;
    }
    for (int j = 0; j < rows; j++) {
        for (int i = 0; i < bays; i++) {
            int h = H(i, j);
            if (h <= 0) continue;
            vec2 base = origin + X * (i * pitchX + CL * 0.5f) + Y * (j * pitchY + CW * 0.5f + 0.9f);
            float z = e.z;
            for (int t = 0; t < h; t++) {
                float ch = tierH(i, j, t);
                int li = lineOf(i, j, t);
                vec3 col = li < 0 ? vec3(0.92f, 0.92f, 0.9f) : kLines[li].color;
                float fade = 0.88f + 0.12f * hashToFloat(hash3i(i, j, t + 99));
                col = col * fade;
                u32 faces = 1u | 2u;  // long sides always (row gaps are walkable lanes for straddles)
                if (H(i - 1, j) <= t) faces |= 4u;
                if (H(i + 1, j) <= t) faces |= 8u;
                if (t == h - 1) faces |= 16u;
                bool doorsPlus = (hash3i(i, j, t) & 1) != 0;
                container(g, vec3(base, z + ch * 0.5f), X, CL, CW, ch, col, faces, doorsPlus);
                // company marks on the outer rows (visible from the lanes)
                bool outer = j == 0 || j == rows - 1;
                if (outer && li >= 0 && t < 3) {
                    int sd = j == 0 ? -1 : 1;
                    vec3 n(Y * (float)sd, 0);
                    vec3 fc(base + Y * (sd * CW * 0.5f), z + ch * 0.55f);
                    vec3 rt = sd < 0 ? vec3(X, 0) : vec3(-X, 0);
                    const Line& ln = kLines[li];
                    if ((hash3i(i, j, t + 7) % 5) == 0 && t == 0) {
                        float th = 0.62f;
                        float tw = textAdvance(ln.name, th, 0.28f);
                        strokeText(g, *g.m, ln.name, fc + n * 0.02f - rt * (tw * 0.5f) - vec3(0, 0, th * 0.5f), rt, vec3(0, 0, 1), th, 0.12f, rgbv(ln.mark),
                                   M(MAT_METAL_PAINTED), 0.f, 0.28f);
                    } else {
                        emblem(g, ln.emblem, fc, rt, vec3(0, 0, 1), n, ln.mark, 0.8f);
                    }
                }
                z += ch;
            }
        }
    }
    // Reefer power racks along reefer blocks
    if (e.variant == 1) {
        for (int j = 0; j <= rows; j += 2) {
            vec2 a = origin + Y * (j * pitchY + 0.4f), b = a + X * (bays * pitchX);
            beam(g, vec3(a, e.z + 3.2f), vec3(b, e.z + 3.2f), 0.4f, 0.6f, rgb(0.8f, 0.75f, 0.2f), M(MAT_METAL_PAINTED));
            for (int i = 0; i <= bays; i += 4) {
                vec2 p = a + X * (i * pitchX);
                boxY(g, vec3(p, e.z + 1.6f), X, vec3(0.15f, 0.15f, 1.6f), rgb(0.5f), M(MAT_METAL_PAINTED));
            }
        }
    }
    // collision: one box per run of equal height in each row (stack tops are walkable)
    for (int j = 0; j < rows; j++) {
        int i = 0;
        while (i < bays) {
            int h = H(i, j);
            int k = i;
            while (k + 1 < bays && H(k + 1, j) == h) k++;
            if (h > 0) {
                float x0 = i * pitchX, x1 = k * pitchX + CL;
                vec2 c = origin + X * ((x0 + x1) * 0.5f) + Y * (j * pitchY + CW * 0.5f + 0.9f);
                collide(g, vec3(c, e.z + h * 1.37f), X, vec3((x1 - x0) * 0.5f, CW * 0.5f, h * 1.37f));
            }
            i = k + 1;
        }
    }
}

// ------------------------------------------------------------------------------------------------ straddle carrier
void genStraddle(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 X = e.ax, Y = perp(e.ax);
    vec3 o(e.c, e.z);
    u32 frame = rgb(0.92f, 0.92f, 0.9f), acc = rgb(0.95f, 0.5f, 0.08f);
    float L = 4.6f, W = 2.3f, Hh = 14.5f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            vec3 base = o + vec3(X * (sx * L) + Y * (sy * W), 0);
            beam(g, base + vec3(0, 0, 1.2f), base + vec3(0, 0, Hh - 1.f), 0.5f, 0.5f, frame, M(MAT_METAL_PAINTED));
            if (g.detail) rod(g, base + vec3(0, 0, 0.7f) - vec3(Y * 0.3f, 0), base + vec3(0, 0, 0.7f) + vec3(Y * 0.3f, 0), 0.7f, 10, rgb(0.06f), M(MAT_RUBBER));
        }
    for (int sy = -1; sy <= 1; sy += 2) {
        beam(g, o + vec3(-X * L + Y * (sy * W), 1.4f), o + vec3(X * L + Y * (sy * W), 1.4f), 0.5f, 0.9f, acc, M(MAT_METAL_PAINTED));
        beam(g, o + vec3(-X * (L + 0.4f) + Y * (sy * W), Hh - 0.6f), o + vec3(X * (L + 0.4f) + Y * (sy * W), Hh - 0.6f), 0.8f, 1.4f, frame, M(MAT_METAL_PAINTED));
    }
    beam(g, o + vec3(-X * L, Hh - 0.4f), o + vec3(X * L, Hh - 0.4f), 2.f * W + 0.6f, 0.6f, frame, M(MAT_METAL_PAINTED));
    // cab on top at the front, engine housing at the back
    boxY(g, o + vec3(X * (L - 0.8f) + Y * (W * 0.55f), Hh + 1.f), X, vec3(0.9f, 0.9f, 1.f), rgb(0.95f), M(MAT_METAL_PAINTED));
    quad(g, *g.m, o + vec3(X * (L + 0.11f) + Y * (W * 0.55f - 0.8f), Hh + 0.4f), o + vec3(X * (L + 0.11f) + Y * (W * 0.55f + 0.8f), Hh + 0.4f),
         o + vec3(X * (L + 0.11f) + Y * (W * 0.55f + 0.8f), Hh + 1.7f), o + vec3(X * (L + 0.11f) + Y * (W * 0.55f - 0.8f), Hh + 1.7f), rgb(0.15f, 0.2f, 0.25f),
         M(MAT_GLASS), vec3(X, 0));
    boxY(g, o + vec3(-X * (L - 1.2f), Hh + 0.6f), X, vec3(1.2f, W * 0.8f, 0.6f), acc, M(MAT_METAL_PAINTED));
    lamp(g, o + vec3(X * (L - 0.8f) + Y * (W * 0.55f), Hh + 2.2f), 0.3f, vec3(1.f, 0.6f, 0.05f), 0.9f, EA_BLINK, e.seed & 255u);
    if (e.p[0] > 0.5f) container(g, o + vec3(0, 0, 4.2f), Y, 12.19f, 2.44f, 2.59f, kLines[e.seed % kLineCount].color, 31u, true);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) collide(g, o + vec3(X * (sx * L) + Y * (sy * W), Hh * 0.5f), X, vec3(0.4f, 0.4f, Hh * 0.5f));
}

// ------------------------------------------------------------------------------------------------ container ship
void genShip(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool big = e.variant == 0;
    bool detail = g.detail;
    vec2 F = e.ax, S = perp(e.ax);  // F bow direction, S port side
    float half = e.hx, beam0 = e.hy;
    float draft = big ? 12.f : 8.f, freeboard = big ? 13.5f : 9.5f;
    vec3 hullC = big ? vec3(0.08f, 0.14f, 0.3f) : vec3(0.12f, 0.35f, 0.2f);
    vec3 red(0.55f, 0.1f, 0.08f), boot(0.06f, 0.06f, 0.06f);
    // hull loft: stations along the length (x from stern -half to bow +half), half-width at waterline / deck
    struct St { float x, wl, dk; };
    std::vector<St> st;
    int nst = detail ? 18 : 9;
    for (int i = 0; i <= nst; i++) {
        float t = (float)i / nst;
        float x = Lerp(-half, half, t);
        float wl = beam0, dk = beam0;
        float bowStart = half - (big ? 55.f : 38.f);
        if (x > bowStart) {
            float u = (x - bowStart) / (half - bowStart);
            wl = beam0 * sqrtf(Max(0.f, 1.f - u * u)) * (1.f - 0.15f * u);
            dk = beam0 * sqrtf(Max(0.f, 1.f - powf(u, 3.f)));
        }
        float sternStart = -half + (big ? 30.f : 20.f);
        if (x < sternStart) {
            float u = (sternStart - x) / (big ? 30.f : 20.f);
            wl = beam0 * (1.f - 0.55f * u * u);
            dk = beam0 * (1.f - 0.12f * u);
        }
        st.push_back({x, Max(wl, 0.3f), Max(dk, 0.8f)});
    }
    MeshData& m = *g.m;
    vec3 o(e.c, 0.f);
    auto P = [&](float x, float y, float z) { return o + vec3(F * x + S * y, z); };
    // side profile points per station (bottom center -> bilge -> waterline -> boot top -> deck edge)
    const int np = 6;
    for (int side = -1; side <= 1; side += 2) {
        u32 b0 = (u32)m.verts.size();
        for (size_t i = 0; i < st.size(); i++) {
            float w = st[i].wl, d = st[i].dk;
            float yb = Max(0.f, w - 3.f);
            float pz[np] = {-draft, -draft, -draft + 2.5f, 0.f, 2.f, freeboard};
            float py[np] = {0.f, yb, w, w, Lerp(w, d, 0.15f), d};
            vec3 cols[np] = {red, red, red, boot, hullC, hullC};
            for (int k = 0; k < np; k++) {
                vec3 p = P(st[i].x, side * py[k], pz[k]);
                vec3 n = normalize(vec3(S * (float)side, k < 2 ? -1.f : 0.f));
                m.addVertex(p - g.org, n, vec3(F, 0), vec2(st[i].x, pz[k]), rgbv(cols[k]), M(MAT_METAL_PAINTED));
            }
        }
        for (size_t i = 0; i + 1 < st.size(); i++)
            for (int k = 0; k + 1 < np; k++) {
                u32 a = b0 + (u32)(i * np + k), b = a + np, c = b + 1, d = a + 1;
                vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[d].pos - m.verts[a].pos);
                vec3 want(S * (float)side, 0);
                if (dot(fn, want) >= 0) m.quadIdx(a, b, c, d);
                else m.quadIdx(a, d, c, b);
            }
    }
    // transom at the stern and a stem strip at the bow
    {
        float w = st[0].dk, wl = st[0].wl;
        std::vector<vec3> tr = {P(-half, -wl, -draft + 2.5f), P(-half, wl, -draft + 2.5f), P(-half, w, freeboard), P(-half, -w, freeboard)};
        quad(g, m, tr[0], tr[1], tr[2], tr[3], rgbv(hullC), M(MAT_METAL_PAINTED), vec3(-F, 0));
    }
    // deck
    {
        std::vector<vec2> deck;
        for (size_t i = 0; i < st.size(); i++) deck.push_back(e.c + F * st[i].x - S * st[i].dk);
        for (size_t i = st.size(); i-- > 0;) deck.push_back(e.c + F * st[i].x + S * st[i].dk);
        polyFlat(g, m, deck, freeboard, rgb(0.45f, 0.5f, 0.45f), M(MAT_METAL_PAINTED));
    }
    // name on the bow (both sides) and stern
    if (detail) {
        float th = big ? 2.4f : 1.8f;
        float tw = textAdvance(e.text.c_str(), th, 0.3f);
        for (int side = -1; side <= 1; side += 2) {
            float x = half - (big ? 30.f : 22.f);
            float y = side * (st[st.size() - 3].dk + 0.05f);
            vec3 out(S * (float)side, 0);
            vec3 rt(perp(S * (float)side), 0);  // viewer's right when facing this side of the hull
            vec3 org = side > 0 ? P(x, y, freeboard - th - 1.2f) : P(x - tw, y, freeboard - th - 1.2f);
            strokeText(g, m, e.text.c_str(), org + out * 0.03f, rt, vec3(0, 0, 1), th, th * 0.13f, rgb(0.95f, 0.95f, 0.95f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
        }
        const char* home = "PORTO SOL";
        float tw2 = textAdvance(home, th * 0.8f, 0.3f);
        vec2 sr = perp(-F);
        vec3 so = P(-half - 0.05f, 0.f, freeboard - th * 2.2f) - vec3(sr * (tw2 * 0.5f), 0);
        strokeText(g, m, home, so, vec3(sr, 0), vec3(0, 0, 1), th * 0.8f, th * 0.1f, rgb(0.95f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
    }
    // superstructure (accommodation + bridge) near the stern, funnel behind it
    float supX = -half + (big ? 52.f : 30.f);
    float supL = big ? 14.f : 11.f, supW = big ? 16.f : 11.f, supH = big ? 24.f : 17.f;
    vec3 supBase = P(supX, 0.f, freeboard);
    boxY(g, supBase + vec3(0, 0, supH * 0.5f), F, vec3(supL * 0.5f, supW, supH * 0.5f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED));
    int decks = (int)(supH / 2.9f);
    for (int k = 0; k < decks; k++) {
        float z = freeboard + 1.2f + k * 2.9f;
        for (int side = -1; side <= 1; side += 2) {
            vec3 out(S * (float)side, 0);
            vec3 c = supBase + out * (supW + 0.03f) + vec3(0, 0, z - freeboard + 0.6f);
            quad(g, m, c - vec3(F * (supL * 0.45f), 0), c + vec3(F * (supL * 0.45f), 0), c + vec3(F * (supL * 0.45f), 0.9f), c - vec3(F * (supL * 0.45f), 0.9f),
                 rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS), out);
            if ((k * 7 + side) % 3 != 0)
                quad(g, m, c - vec3(F * (supL * 0.3f), 0) + out * 0.02f, c + vec3(F * (supL * 0.1f), 0) + out * 0.02f, c + vec3(F * (supL * 0.1f), 0.9f) + out * 0.02f,
                     c - vec3(F * (supL * 0.3f), 0.9f) + out * 0.02f, rgb(1.f, 0.85f, 0.6f, 0.08f), emMat(EA_NIGHT), out);
        }
        vec3 fc = supBase + vec3(F * (supL * 0.5f + 0.03f), z - freeboard + 0.6f);
        quad(g, m, fc - vec3(S * (supW * 0.9f), 0), fc + vec3(S * (supW * 0.9f), 0), fc + vec3(S * (supW * 0.9f), 0.9f), fc - vec3(S * (supW * 0.9f), 0.9f),
             rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS), vec3(F, 0));
    }
    // bridge deck with wings to the full beam
    vec3 br = supBase + vec3(0, 0, supH + 1.5f);
    boxY(g, br, F, vec3(supL * 0.5f + 1.f, beam0 + 0.5f, 1.5f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED), true);
    quad(g, m, br + vec3(F * (supL * 0.5f + 1.02f) - S * (beam0 * 0.95f), -0.6f), br + vec3(F * (supL * 0.5f + 1.02f) + S * (beam0 * 0.95f), -0.6f),
         br + vec3(F * (supL * 0.5f + 1.02f) + S * (beam0 * 0.95f), 0.9f), br + vec3(F * (supL * 0.5f + 1.02f) - S * (beam0 * 0.95f), 0.9f), rgb(0.08f, 0.1f, 0.12f),
         M(MAT_GLASS), vec3(F, 0));
    cyl(g, br + vec3(0, 0, 1.5f), 0.4f, 0.25f, 9.f, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
    boxY(g, br + vec3(0, 0, 8.f), S, vec3(3.f, 0.25f, 0.2f), rgb(0.2f), M(MAT_METAL_PAINTED));
    lamp(g, br + vec3(0, 0, 10.8f), 0.35f, vec3(1.f, 0.95f, 0.85f), 0.9f);
    for (int side = -1; side <= 1; side += 2)
        lamp(g, br + vec3(S * (side * (beam0 + 0.3f)), 1.f), 0.35f, side > 0 ? vec3(1.f, 0.08f, 0.05f) : vec3(0.1f, 1.f, 0.3f), 0.9f);
    // funnel
    vec3 fun = P(supX - supL * 0.5f - 6.f, 0.f, freeboard + supH * 0.5f);
    boxY(g, fun + vec3(0, 0, supH * 0.35f), F, vec3(4.f, 3.4f, supH * 0.5f + 4.f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED), true);
    const Line& ln = kLines[e.seed % kLineCount];
    boxY(g, fun + vec3(0, 0, supH * 0.72f), F, vec3(4.05f, 3.45f, 1.8f), rgbv(ln.color), M(MAT_METAL_PAINTED), true);
    boxY(g, fun + vec3(0, 0, supH * 0.35f + supH * 0.5f + 4.2f), F, vec3(3.6f, 3.f, 0.3f), rgb(0.1f), M(MAT_METAL_PAINTED), true);
    // lifeboat
    if (detail)
        for (int side = -1; side <= 1; side += 2)
            boxY(g, supBase + vec3(S * (side * (supW + 1.6f)) - F * 2.f, supH * 0.45f), F, vec3(4.f, 1.2f, 1.1f), rgb(1.f, 0.45f, 0.05f), M(MAT_METAL_PAINTED), true);
    collide(g, supBase + vec3(0, 0, supH * 0.5f), F, vec3(supL * 0.5f, supW, supH * 0.5f));
    collide(g, vec3(e.c, (freeboard - draft) * 0.5f), F, vec3(half - 8.f, beam0, (freeboard + draft) * 0.5f));
    // container bays forward and aft of the superstructure
    int rows = big ? 16 : 10;
    const float bayL = 12.8f, CW = 2.44f;
    float rowSpan = rows * CW;
    int tiersMax = big ? 7 : 4;
    Rng r(e.seed);
    std::vector<float> bayX;
    for (float x = supX + supL * 0.5f + 8.f; x + bayL < half - (big ? 32.f : 24.f); x += bayL + 1.4f) bayX.push_back(x + bayL * 0.5f);
    for (float x = supX - supL * 0.5f - 16.f; x - bayL > -half + 8.f; x -= bayL + 1.4f) bayX.push_back(x - bayL * 0.5f);
    for (size_t bi = 0; bi < bayX.size(); bi++) {
        float bx = bayX[bi];
        float distBow = half - bx;
        int tiers = tiersMax - (distBow < 60.f ? 2 : (distBow < 90.f ? 1 : 0));
        if (bx < supX) tiers = Max(2, tiersMax - 2);
        // hatch cover
        vec3 hc = P(bx, 0.f, freeboard + 0.6f);
        boxY(g, hc, F, vec3(bayL * 0.5f, rowSpan * 0.5f + 0.4f, 0.6f), rgb(0.35f, 0.42f, 0.38f), M(MAT_METAL_PAINTED), false);
        if (!detail) {
            // far: per bay a box with a mid-grey tint mixing the lines
            vec3 col = kLines[(bi * 3 + e.seed) % kLineCount].color * 0.6f + vec3(0.25f);
            boxY(g, P(bx, 0.f, freeboard + 1.2f + tiers * 1.3f), F, vec3(bayL * 0.48f, rowSpan * 0.5f, tiers * 1.3f), rgbv(col), M(MAT_CORRUGATED));
            continue;
        }
        for (int j = 0; j < rows; j++) {
            float y = -rowSpan * 0.5f + (j + 0.5f) * CW;
            int th = tiers - ((j == 0 || j == rows - 1) && (r.next() & 1) ? 1 : 0);
            for (int t = 0; t < th; t++) {
                int li = (int)(hash3i((int)bi, j, t + (int)e.seed) % kLineCount);
                vec3 col = kLines[li].color * (0.9f + 0.1f * hashToFloat(hash3i(j, t, (int)bi)));
                u32 faces = 4u | 8u;
                if (j == 0) faces |= 1u;
                if (j == rows - 1) faces |= 2u;
                if (t == th - 1) faces |= 16u;
                vec3 cc = P(bx, y, freeboard + 1.2f + 2.6f * t + 1.3f);
                // container axis along the ship: build with ax = F (long side along F)
                container(g, cc, F, 12.19f, 2.44f, 2.59f, col, (faces & 16u) | ((faces & 1u) ? 1u : 0u) | ((faces & 2u) ? 2u : 0u) | 4u | 8u, (bi & 1) != 0);
            }
        }
        collide(g, P(bx, 0.f, freeboard + 1.2f + tiers * 1.3f), F, vec3(bayL * 0.5f, rowSpan * 0.5f, tiers * 1.3f));
        // lashing bridge between bays
        if (bi + 1 < bayX.size() && fabsf(bayX[bi + 1] - bx) < bayL + 2.f) {
            float lx = (bx + bayX[bi + 1]) * 0.5f;
            boxY(g, P(lx, 0.f, freeboard + 4.f), F, vec3(0.35f, rowSpan * 0.5f + 0.3f, 2.8f), rgb(0.85f, 0.8f, 0.3f), M(MAT_METAL_PAINTED));
        }
    }
    // feeder ships carry their own deck cranes
    if (!big) {
        for (int k = 0; k < 2; k++) {
            float cx = supX + 40.f + k * 55.f;
            vec3 pb = P(cx, beam0 * 0.55f, freeboard);
            cyl(g, pb, 1.4f, 1.2f, 9.f, detail ? 10 : 6, rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), true);
            boxY(g, pb + vec3(0, 0, 10.f), F, vec3(2.f, 1.6f, 1.2f), rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), true);
            vec3 jt = pb + vec3(F * 14.f - S * 8.f, 20.f);
            beam(g, pb + vec3(0, 0, 10.5f), jt, 0.8f, 0.8f, rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED));
        }
    }
    // mooring lines to the quay bollards
    if (detail) {
        vec2 quaySide = S;  // the quay lies toward whichever side is closer to the island
        if (e.c.x > kPortX1 && dot(S, vec2(-1, 0)) < 0) quaySide = -S;
        for (int k = 0; k < 4; k++) {
            float x = k < 2 ? half - 12.f - k * 8.f : -half + 10.f + (k - 2) * 8.f;
            vec3 a = P(x, 0.f, freeboard) + vec3(quaySide * st[k < 2 ? st.size() - 3 : 2].dk, 0.f);
            vec3 b = vec3(e.c + F * (x + (k < 2 ? 14.f : -14.f)) + quaySide * (beam0 + 3.4f), gSites->portZ + 0.9f);
            rod(g, a, b, 0.1f, 4, rgb(0.8f, 0.75f, 0.6f), M(MAT_FABRIC));
        }
    }
    // deck floodlights + mast lights
    for (int k = 0; k < 3; k++) light(g, P(supX + 20.f + k * (half * 0.5f), 0.f, freeboard + 18.f), vec3(1.f, 0.9f, 0.75f) * 14000.f, 60.f, 1, vec3(0, 0, -1), 0.35f);
    lamp(g, P(half - 8.f, 0.f, freeboard + 9.f), 0.35f, vec3(1.f, 0.95f, 0.85f), 0.9f);
    cyl(g, P(half - 8.f, 0.f, freeboard), 0.3f, 0.2f, 9.f, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
}

// ------------------------------------------------------------------------------------------------ rail
float railZ(vec2 p, float zP) {
    float t = gMap->heightAt(p.x, p.y);
    if (gMap->isWater(p.x, p.y) || (p.x > kPortX0 - 1.f && p.x < kPortX1 && p.y > kPortY0 && p.y < kPortY1)) return zP;
    return Max(t + 0.3f, zP - 0.4f);
}

void trackAlong(G& g, const std::vector<vec2>& pts, float zP, bool trestle) {
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        vec2 a = pts[i], b = pts[i + 1];
        vec2 mid = (a + b) * 0.5f;
        if (!g.owns(mid)) continue;
        vec2 d = normalize(b - a), n = perp(d);
        float za = railZ(a, zP), zb = railZ(b, zP);
        bool water = gMap->isWater(mid.x, mid.y);
        // ballast bed or trestle deck
        if (water || trestle) {
            quad(g, *g.m, vec3(a - n * 2.4f, za - 0.02f), vec3(b - n * 2.4f, zb - 0.02f), vec3(b + n * 2.4f, zb - 0.02f), vec3(a + n * 2.4f, za - 0.02f), rgb(0.7f),
                 M(MAT_CONCRETE), vec3(0, 0, 1));
            for (int s = -1; s <= 1; s += 2)
                quad(g, *g.m, vec3(a + n * (s * 2.4f), za - 0.02f), vec3(b + n * (s * 2.4f), zb - 0.02f), vec3(b + n * (s * 2.4f), zb - 1.1f),
                     vec3(a + n * (s * 2.4f), za - 1.1f), rgb(0.62f), M(MAT_CONCRETE), vec3(n * (float)s, 0));
            // pier every ~12 m
            float L = length(b - a);
            for (float t = 0.f; t < L; t += 12.f) {
                vec2 p = a + d * t;
                cyl(g, vec3(p, -4.f), 0.7f, 0.7f, Lerp(za, zb, t / Max(L, 1e-3f)) + 3.f, 8, rgb(0.6f), M(MAT_CONCRETE), false);
            }
        } else {
            quad(g, *g.m, vec3(a - n * 2.f, za - 0.05f), vec3(b - n * 2.f, zb - 0.05f), vec3(b + n * 2.f, zb - 0.05f), vec3(a + n * 2.f, za - 0.05f),
                 rgb(0.62f, 0.58f, 0.52f), M(MAT_STONE), vec3(0, 0, 1));
        }
        if (g.detail) {
            float L = length(b - a);
            for (float t = 0.35f; t < L; t += 0.7f) {
                vec2 p = a + d * t;
                float z = Lerp(za, zb, t / Max(L, 1e-3f));
                g.m->box(vec3(p, z + 0.05f) - g.org, vec3(n, 0), vec3(d, 0), vec3(0, 0, 1), vec3(1.3f, 0.12f, 0.06f), rgb(0.45f, 0.36f, 0.28f), M(MAT_WOOD), false);
            }
        }
        for (int s = -1; s <= 1; s += 2) beam(g, vec3(a + n * (s * 0.72f), za + 0.18f), vec3(b + n * (s * 0.72f), zb + 0.18f), 0.08f, 0.14f, rgb(0.45f, 0.42f, 0.4f),
                                             M(MAT_METAL_BRUSHED));
    }
}

void railcar(G& g, vec2 c, vec2 dir, float z, bool loco, u32 seed) {
    vec2 n = perp(dir);
    if (loco) {
        boxY(g, vec3(c, z + 2.4f), dir, vec3(11.f, 1.55f, 1.6f), rgb(0.95f, 0.75f, 0.1f), M(MAT_METAL_PAINTED));
        boxY(g, vec3(c + dir * 8.f, z + 4.3f), dir, vec3(2.4f, 1.5f, 0.6f), rgb(0.1f, 0.2f, 0.45f), M(MAT_METAL_PAINTED));
        quad(g, *g.m, vec3(c + dir * 10.42f - n * 1.2f, z + 3.9f), vec3(c + dir * 10.42f + n * 1.2f, z + 3.9f), vec3(c + dir * 10.42f + n * 1.2f, z + 4.7f),
             vec3(c + dir * 10.42f - n * 1.2f, z + 4.7f), rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS), vec3(dir, 0));
        boxY(g, vec3(c, z + 0.9f), dir, vec3(10.f, 1.4f, 0.35f), rgb(0.15f), M(MAT_METAL_PAINTED));
        if (g.detail) {
            const char* t = "SUNLINE";
            float th = 0.9f;
            float tw = textAdvance(t, th, 0.3f);
            for (int s = -1; s <= 1; s += 2) {
                vec3 out(n * (float)s, 0);
                vec3 rt = s > 0 ? vec3(-dir, 0) : vec3(dir, 0);
                vec3 o = vec3(c + n * (s * 1.57f), z + 2.1f) - rt * (tw * 0.5f);
                strokeText(g, *g.m, t, o, rt, vec3(0, 0, 1), th, 0.14f, rgb(0.1f, 0.2f, 0.45f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
            }
            lamp(g, vec3(c + dir * 11.05f, z + 3.2f), 0.3f, vec3(1.f, 0.95f, 0.8f), 0.8f, EA_NIGHT);
        }
        collide(g, vec3(c, z + 2.2f), dir, vec3(11.f, 1.6f, 2.2f));
        return;
    }
    // well car with a pair of stacked containers
    boxY(g, vec3(c, z + 1.f), dir, vec3(10.f, 1.4f, 0.35f), rgb(0.35f, 0.2f, 0.15f), M(MAT_METAL_PAINTED));
    for (int s = -1; s <= 1; s += 2) boxY(g, vec3(c + n * (s * 1.35f), z + 1.6f), dir, vec3(10.f, 0.08f, 0.6f), rgb(0.35f, 0.2f, 0.15f), M(MAT_METAL_PAINTED));
    for (int k = 0; k < 2; k++) {
        if ((hash32(seed + k) & 7) == 0) continue;
        vec3 col = kLines[hash32(seed * 3 + k) % kLineCount].color;
        container(g, vec3(c, z + 1.4f + 1.3f + k * 2.6f), dir, 12.19f, 2.44f, 2.59f, col, k ? 31u : 15u, true);
    }
    for (int s = -1; s <= 1; s += 2) boxY(g, vec3(c + dir * (s * 8.f), z + 0.5f), dir, vec3(1.2f, 1.2f, 0.45f), rgb(0.15f), M(MAT_METAL_PAINTED));
    collide(g, vec3(c, z + 2.6f), dir, vec3(10.f, 1.4f, 2.6f));
}

void genRailYard(const SiteElem& e, G& g) {
    int tracks = (int)e.p[0];
    vec2 X = e.ax, Y = perp(e.ax);
    for (int t = 0; t < tracks; t++) {
        float off = (t - (tracks - 1) * 0.5f) * 15.f;
        vec2 a = e.c - X * e.hx + Y * off, b = e.c + X * e.hx + Y * off;
        std::vector<vec2> pts;
        int n = (int)(e.hx * 2.f / 40.f);
        for (int k = 0; k <= n; k++) pts.push_back(lerp(a, b, (float)k / n));
        trackAlong(g, pts, e.z, false);
        // buffer stop at the east end
        if (g.owns(b)) {
            boxY(g, vec3(b - X * 1.f, e.z + 0.8f), X, vec3(0.6f, 1.5f, 0.8f), rgb(0.8f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
            lamp(g, vec3(b - X * 1.f, e.z + 1.8f), 0.25f, vec3(1.f, 0.1f, 0.05f), 0.8f);
        }
        // trains: well cars on the first two tracks, a locomotive on the third
        if (t < 2) {
            int cars = t == 0 ? 14 : 10;
            for (int k = 0; k < cars; k++) {
                vec2 cc = a + X * (30.f + k * 21.f + t * 20.f);
                if (g.owns(cc)) railcar(g, cc, X, e.z, false, e.seed * 31u + (u32)(t * 100 + k));
            }
        } else {
            vec2 cc = a + X * 70.f;
            if (g.owns(cc)) railcar(g, cc, X, e.z, true, e.seed);
            vec2 cc2 = a + X * 93.f;
            if (g.owns(cc2)) railcar(g, cc2, -X, e.z, true, e.seed + 1);
        }
    }
}

void genRail(const SiteElem& e, G& g) {
    if (e.variant == 1) {
        if (!g.owns(e.c)) return;
        vec3 p(e.c, e.z + 0.3f);
        boxY(g, p + vec3(0, 0, 0.7f), e.ax, vec3(0.5f, 1.6f, 0.7f), rgb(0.85f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
        for (int s = -1; s <= 1; s += 2) beam(g, p + vec3(perp(e.ax) * (s * 1.2f), 1.4f), p + vec3(-e.ax * 3.f + perp(e.ax) * (s * 1.2f), 0.f), 0.2f, 0.2f, rgb(0.3f),
                                             M(MAT_METAL_PAINTED));
        lamp(g, p + vec3(0, 0, 1.7f), 0.25f, vec3(1.f, 0.1f, 0.05f), 0.8f);
        return;
    }
    trackAlong(g, e.pts, gSites->portZ, e.p[0] > 0.5f);
}

// Rail-mounted gantry over the intermodal tracks
void genRmgCrane(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 span = e.ax, along = perp(e.ax);
    vec3 o(e.c, e.z);
    u32 blue = rgb(0.15f, 0.35f, 0.7f), white = rgb(0.93f);
    float halfSpan = 30.f, H = 24.f;
    for (int s = -1; s <= 1; s += 2)
        for (int a = -1; a <= 1; a += 2) {
            vec3 base = o + vec3(span * (s * halfSpan) + along * (a * 7.f), 0);
            beam(g, base + vec3(0, 0, 1.f), base + vec3(0, 0, H), 1.1f, 1.1f, blue, M(MAT_METAL_PAINTED));
            collide(g, base + vec3(0, 0, H * 0.5f), span, vec3(0.6f, 0.6f, H * 0.5f));
        }
    for (int a = -1; a <= 1; a += 2) beam(g, o + vec3(-span * (halfSpan + 6.f) + along * (a * 3.f), H + 1.f), o + vec3(span * (halfSpan + 6.f) + along * (a * 3.f), H + 1.f),
                                         1.2f, 2.4f, white, M(MAT_METAL_PAINTED));
    for (int s = -1; s <= 1; s += 2) beam(g, o + vec3(span * (s * halfSpan) - along * 7.f, H), o + vec3(span * (s * halfSpan) + along * 7.f, H), 1.f, 1.6f, blue, M(MAT_METAL_PAINTED));
    vec3 tr = o + vec3(span * 8.f, H + 2.4f);
    boxY(g, tr, span, vec3(3.f, 4.f, 1.2f), rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), true);
    boxY(g, tr - vec3(0, 0, 12.f), along, vec3(6.2f, 1.25f, 0.35f), rgb(0.95f, 0.75f, 0.1f), M(MAT_METAL_PAINTED), true);
    lamp(g, o + vec3(span * halfSpan, H + 2.6f), 0.4f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, e.seed & 255u);
    light(g, o + vec3(0, 0, H - 1.f), vec3(1.f, 0.9f, 0.75f) * 16000.f, 55.f, 1, vec3(0, 0, -1), 0.35f);
}

// ------------------------------------------------------------------------------------------------ gate complex
void genPortGate(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 road = e.ax, side = perp(e.ax);
    vec3 o(e.c, e.z);
    float H = 7.5f;
    boxY(g, o + vec3(0, 0, H + 0.5f), road, vec3(e.hx, e.hy, 0.55f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED), true);
    quad(g, *g.m, o + vec3(-road * (e.hx + 0.02f) - side * e.hy, H - 0.05f), o + vec3(-road * (e.hx + 0.02f) + side * e.hy, H - 0.05f),
         o + vec3(-road * (e.hx + 0.02f) + side * e.hy, H + 1.05f), o + vec3(-road * (e.hx + 0.02f) - side * e.hy, H + 1.05f), rgb(0.1f, 0.35f, 0.6f),
         M(MAT_METAL_PAINTED), vec3(-road, 0));
    {
        float th = 0.8f;
        float tw = textAdvance(e.text.c_str(), th, 0.3f);
        vec2 viewR = perp(-road);
        strokeText(g, *g.m, e.text.c_str(), o + vec3(-road * (e.hx + 0.05f) - viewR * (tw * 0.5f), H + 0.1f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.13f,
                   rgb(1.f, 1.f, 1.f, 0.4f), emMat(EA_NIGHT), 0.f, 0.3f);
    }
    for (int a = -1; a <= 1; a += 2)
        for (int s = -1; s <= 1; s += 2) {
            vec3 cp = o + vec3(road * (a * (e.hx - 3.f)) + side * (s * (e.hy - 1.f)), 0);
            cyl(g, cp, 0.4f, 0.4f, H, 8, rgb(0.9f), M(MAT_METAL_PAINTED), false);
            collide(g, cp + vec3(0, 0, H * 0.5f), road, vec3(0.4f, 0.4f, H * 0.5f));
        }
    // booths at the road edges and OCR portal
    for (int s = -1; s <= 1; s += 2) {
        vec3 bc = o + vec3(side * (s * (e.hy - 3.f)), 1.4f);
        boxY(g, bc, road, vec3(1.8f, 1.2f, 1.4f), rgb(0.9f, 0.88f, 0.85f), M(MAT_STUCCO));
        collide(g, bc, road, vec3(1.8f, 1.2f, 1.4f));
    }
    if (g.detail) {
        vec3 pc = o + vec3(-road * (e.hx + 12.f), 0);
        for (int s = -1; s <= 1; s += 2) beam(g, pc + vec3(side * (s * 11.f), 0.f), pc + vec3(side * (s * 11.f), 6.5f), 0.3f, 0.3f, rgb(0.4f), M(MAT_METAL_PAINTED));
        beam(g, pc + vec3(-side * 11.f, 6.5f), pc + vec3(side * 11.f, 6.5f), 0.4f, 0.6f, rgb(0.4f), M(MAT_METAL_PAINTED));
    }
    for (int k = -1; k <= 1; k++) light(g, o + vec3(road * (k * e.hx * 0.6f), H - 0.3f), vec3(1.f, 0.95f, 0.9f) * 9000.f, 30.f, 1, vec3(0, 0, -1), 0.3f);
}

}  // namespace port_mesh
}  // namespace World
