// Shared geometry for the hand-built places (places.h): terrain drapes for lawns and paths, low walls and railings,
// neon tubes and letters, portholes, cafe furniture and umbrellas, lamp posts, flag poles, bleachers and monument signs.
// World-space helpers on the site geometry sink (sitegeo::G); included by sitecell.cpp before the place generators.
#include "places.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace place_kit {

using namespace sitegeo;

inline vec3 V3(vec2 p, float z) { return vec3(p.x, p.y, z); }
// Yaw of a prop whose local -y should face f (benches: the sitter looks along -y)
inline float yawFacing(vec2 f) { return atan2f(f.x, -f.y); }
// Yaw of a prop whose local +x should point along d
inline float yawAlong(vec2 d) { return atan2f(d.y, d.x); }
inline u32 tint(vec3 c, float a = 1.f) { return rgbv(vmin(c, vec3(1.f)), a); }

// ------------------------------------------------------------------------------------------------ ground
// Terrain-following quads over a rectangle (decal layer, lifted `lift` above the ground), only the part inside this cell.
// Cells overlap nothing: each quad whose centre lies in the cell is emitted there. zFixed > -1e8: flat at that height.
void drapeRect(G& g, vec2 c, vec2 ax, float hx, float hy, float lift, u32 col, u32 mat, float step, float zFixed = -1e9f, bool decal = true) {
    vec2 ay = perp(ax);
    int nx = Max(1, (int)ceilf(2.f * hx / step)), ny = Max(1, (int)ceilf(2.f * hy / step));
    MeshData& m = decal ? *g.d : *g.m;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float u0 = -hx + 2.f * hx * i / nx, u1 = -hx + 2.f * hx * (i + 1) / nx;
            float v0 = -hy + 2.f * hy * j / ny, v1 = -hy + 2.f * hy * (j + 1) / ny;
            vec2 mid = c + ax * ((u0 + u1) * 0.5f) + ay * ((v0 + v1) * 0.5f);
            if (!g.owns(mid)) continue;
            vec2 p[4] = {c + ax * u0 + ay * v0, c + ax * u1 + ay * v0, c + ax * u1 + ay * v1, c + ax * u0 + ay * v1};
            vec3 q[4];
            for (int k = 0; k < 4; k++) q[k] = V3(p[k], (zFixed > -1e8f ? zFixed : gMap->heightAt(p[k].x, p[k].y)) + lift);
            m.quadFacing(q[0] - g.org, q[1] - g.org, q[2] - g.org, q[3] - g.org, p[0], p[1], p[2], p[3], col, mat, vec3(0, 0, 1));
        }
}

// Paved strip along a polyline (width w), terrain following or flat at zFixed, with low kerb faces; pieces are emitted in
// the cell that owns their midpoint. uvScale: pavers (kPaverUV) or metres.
void pathStrip(G& g, const std::vector<vec2>& pts, float w, float lift, u32 col, u32 mat, float zFixed = -1e9f, bool kerb = true, bool pavers = true) {
    if (pts.size() < 2) return;
    float hw = w * 0.5f;
    // miter normals
    size_t n = pts.size();
    std::vector<vec2> nrm(n);
    for (size_t i = 0; i < n; i++) {
        vec2 d0 = i > 0 ? normalize(pts[i] - pts[i - 1]) : normalize(pts[1] - pts[0]);
        vec2 d1 = i + 1 < n ? normalize(pts[i + 1] - pts[i]) : d0;
        vec2 t = normalize(d0 + d1);
        if (length2(t) < 1e-6f) t = d0;
        vec2 nn = perp(t);
        float s = dot(nn, perp(d0));
        nrm[i] = nn / Max(s, 0.35f);
    }
    auto Z = [&](vec2 p) { return (zFixed > -1e8f ? zFixed : gMap->heightAt(p.x, p.y)) + lift; };
    for (size_t i = 0; i + 1 < n; i++) {
        vec2 a = pts[i], b = pts[i + 1];
        if (!g.owns((a + b) * 0.5f)) continue;
        vec2 a0 = a - nrm[i] * hw, a1 = a + nrm[i] * hw, b0 = b - nrm[i + 1] * hw, b1 = b + nrm[i + 1] * hw;
        size_t v0 = g.m->verts.size();
        quad(g, *g.m, V3(a0, Z(a0)), V3(b0, Z(b0)), V3(b1, Z(b1)), V3(a1, Z(a1)), col, mat, vec3(0, 0, 1));
        if (pavers) paverUV(g, *g.m, v0);
        if (kerb) {
            vec2 on = perp(normalize(b - a));
            quad(g, *g.m, V3(a1, Z(a1)), V3(b1, Z(b1)), V3(b1, Z(b1) - lift - 0.12f), V3(a1, Z(a1) - lift - 0.12f), col, M(MAT_CURB), vec3(on, 0));
            quad(g, *g.m, V3(b0, Z(b0)), V3(a0, Z(a0)), V3(a0, Z(a0) - lift - 0.12f), V3(b0, Z(b0) - lift - 0.12f), col, M(MAT_CURB), vec3(-on, 0));
        }
    }
}

// Polyline sampled from a smooth curve through control points (Catmull-Rom), about `step` metres apart
std::vector<vec2> smoothCurve(const std::vector<vec2>& c, float step) {
    std::vector<vec2> out;
    if (c.size() < 2) return c;
    for (size_t i = 0; i + 1 < c.size(); i++) {
        vec2 p0 = c[i > 0 ? i - 1 : 0], p1 = c[i], p2 = c[i + 1], p3 = c[i + 2 < c.size() ? i + 2 : c.size() - 1];
        int k = Max(1, (int)ceilf(length(p2 - p1) / step));
        for (int s = 0; s < k; s++) {
            float t = (float)s / k, t2 = t * t, t3 = t2 * t;
            out.push_back((p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f);
        }
    }
    out.push_back(c.back());
    return out;
}

// Box standing on the ground between two points (walls, kerbs, low walls): from z0 to z1, thickness th
void wallRun(G& g, vec2 a, vec2 b, float z0, float z1, float th, u32 col, u32 mat, bool coll = true) {
    vec2 d = b - a;
    float L = length(d);
    if (L < 0.02f) return;
    d = d / L;
    boxY(g, V3((a + b) * 0.5f, (z0 + z1) * 0.5f), d, vec3(L * 0.5f, th * 0.5f, (z1 - z0) * 0.5f), col, mat);
    if (coll) collide(g, V3((a + b) * 0.5f, (z0 + z1) * 0.5f), d, vec3(L * 0.5f, th * 0.5f, (z1 - z0) * 0.5f));
}

// Low wall with a projecting coping along a..b (garden walls, terrace walls, cemetery walls)
void copedWall(G& g, vec2 a, vec2 b, float z0, float h, float th, u32 wallCol, u32 wallMat, u32 capCol, u32 capMat, bool coll = true) {
    wallRun(g, a, b, z0, z0 + h, th, wallCol, wallMat, coll);
    vec2 d = normalize(b - a);
    float L = length(b - a) + 0.1f;
    boxY(g, V3((a + b) * 0.5f, z0 + h + 0.05f), d, vec3(L * 0.5f, th * 0.5f + 0.05f, 0.05f), capCol, capMat, true);
}

// Wrought-iron railing along a..b (pickets, rails, finials): h tall, pickets every `sp` metres
void ironRailing(G& g, vec2 a, vec2 b, float z0, float h, float sp, u32 col, bool finials = true, bool coll = true) {
    vec2 d = b - a;
    float L = length(d);
    if (L < 0.05f) return;
    d = d / L;
    u32 mat = M(MAT_METAL_PAINTED);
    beam(g, V3(a, z0 + 0.12f), V3(b, z0 + 0.12f), 0.035f, 0.035f, col, mat);
    beam(g, V3(a, z0 + h - 0.08f), V3(b, z0 + h - 0.08f), 0.035f, 0.035f, col, mat);
    int n = Max(1, (int)(L / sp));
    for (int k = 0; k <= n; k++) {
        vec2 p = a + d * (L * k / n);
        boxY(g, V3(p, z0 + h * 0.5f), d, vec3(0.012f, 0.012f, h * 0.5f), col, mat);
        if (finials && g.detail) boxY(g, V3(p, z0 + h + 0.05f), d, vec3(0.025f, 0.025f, 0.05f), col, mat);
    }
    if (coll) collide(g, V3((a + b) * 0.5f, z0 + h * 0.5f), d, vec3(L * 0.5f, 0.05f, h * 0.5f));
}

// ------------------------------------------------------------------------------------------------ neon and lettering
// Neon tube between two points (emissive round-ish box), glowing in the neon colour at night; by day a coloured glass tube
void neonTube(G& g, vec3 a, vec3 b, float r, vec3 col, u32 anim = EA_NIGHT, u32 phase = 0, float strength = 0.85f) {
    vec3 d = b - a;
    if (length2(d) < 1e-6f) return;
    vec3 up = fabsf(normalize(d).z) > 0.9f ? vec3(1, 0, 0) : vec3(0, 0, 1);
    beam(g, a, b, r * 2.f, r * 2.f, tint(col, strength), emMat(anim, phase), up);
}
// Neon letters (tube strokes) in the plane right/up at origin (bottom left), facing cross(right, up); returns the width
float neonLetters(G& g, const char* txt, vec3 origin, vec3 right, vec3 up, float h, vec3 col, u32 anim = EA_NIGHT, u32 phase = 0, float depth = 0.05f,
                  float strength = 0.85f) {
    return strokeText(g, *g.m, txt, origin, right, up, h, Max(0.035f, h * 0.085f), tint(col, strength), emMat(anim, phase), g.detail ? depth : 0.f, 0.3f);
}
// Painted / relief letters (non-emissive)
float plainLetters(G& g, const char* txt, vec3 origin, vec3 right, vec3 up, float h, u32 col, u32 mat, float depth = 0.03f, float weight = 0.12f) {
    return strokeText(g, *g.m, txt, origin, right, up, h, h * weight, col, mat, g.detail ? depth : 0.f, 0.3f);
}
// Centred text helper: returns origin so the text is centred on c (bottom at c.z)
inline vec3 centredOrigin(const char* txt, vec3 c, vec3 right, float h) { return c - right * (textAdvance(txt, h, 0.3f) * 0.5f); }

// Flat disc facing n (portholes, clock faces, relief roundels)
void disc(G& g, vec3 c, vec3 n, float r, int seg, u32 col, u32 mat) {
    vec3 t = normalize(anyPerp(n)), bt = cross(n, t);
    MeshData& m = *g.m;
    u32 b0 = m.addVertex(c - g.org, n, t, vec2(0, 0), col, mat);
    for (int k = 0; k <= seg; k++) {
        float a = kTwoPi * k / seg;
        m.addVertex(c + (t * cosf(a) + bt * sinf(a)) * r - g.org, n, t, vec2(cosf(a), sinf(a)) * r, col, mat);
    }
    for (int k = 0; k < seg; k++) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
}
// Ring of beams (porthole frames, wreaths, clock bezels) around c in the plane facing n
void ring(G& g, vec3 c, vec3 n, float r, float w, float depth, int seg, u32 col, u32 mat) {
    vec3 t = normalize(anyPerp(n)), bt = cross(n, t);
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec3 p0 = c + (t * cosf(a0) + bt * sinf(a0)) * r, p1 = c + (t * cosf(a1) + bt * sinf(a1)) * r;
        vec3 mid = (p0 + p1) * 0.5f, rad = normalize(mid - c);
        beam(g, p0 + n * (depth * 0.5f), p1 + n * (depth * 0.5f), w, depth, col, mat, rad);
    }
}
// Porthole window in a wall facing n: raised ring frame, dark glass that glows warm at night
void porthole(G& g, vec3 c, vec3 n, float r, u32 frameCol, bool lit) {
    disc(g, c + n * 0.02f, n, r, g.detail ? 16 : 8, lit ? rgb(1.f, 0.8f, 0.55f, 0.05f) : rgb(0.08f, 0.1f, 0.12f), lit ? emMat(EA_NIGHT) : M(MAT_GLASS));
    if (g.detail) {
        ring(g, c, n, r + 0.05f, 0.1f, 0.1f, 16, frameCol, M(MAT_PLASTER));
        // cross bars of the old ship-style porthole
        vec3 t = normalize(cross(n, vec3(0, 0, 1)));
        if (length2(t) < 0.5f) t = normalize(anyPerp(n));
        beam(g, c - t * r + n * 0.04f, c + t * r + n * 0.04f, 0.03f, 0.03f, frameCol, M(MAT_METAL_PAINTED), n);
    }
}

// Glass block panel in a wall facing n (right = along the wall): warm translucent blocks glowing at night, white joints
void glassBlock(G& g, vec3 bl, vec3 right, vec3 up, float w, float h, float blockSz) {
    vec3 n = normalize(cross(right, up));
    vec3 a = bl + n * 0.03f;
    // the blocks: one panel (emissive, faint by day, lit stairwell at night)
    quad(g, *g.m, a, a + right * w, a + right * w + up * h, a + up * h, rgb(0.82f, 0.93f, 0.95f, 0.035f), emMat(EA_NONE), n);
    if (!g.detail) return;
    int nx = Max(1, (int)roundf(w / blockSz)), ny = Max(1, (int)roundf(h / blockSz));
    u32 joint = rgb(0.95f, 0.95f, 0.93f), jm = M(MAT_PLASTER);
    // mortar joints: flat strips just proud of the blocks (seen from the street side only)
    const float jw = 0.018f;
    for (int i = 0; i <= nx; i++) {
        vec3 p = a + right * (w * i / nx) + n * 0.012f;
        quad(g, *g.m, p - right * jw, p + right * jw, p + right * jw + up * h, p - right * jw + up * h, joint, jm, n);
    }
    for (int j = 0; j <= ny; j++) {
        vec3 p = a + up * (h * j / ny) + n * 0.014f;
        quad(g, *g.m, p - up * jw, p + right * w - up * jw, p + right * w + up * jw, p + up * jw, joint, jm, n);
    }
}

// Sunburst relief (half disc of radiating ribs) centred at c on a wall facing n, radius r
void sunburst(G& g, vec3 c, vec3 right, vec3 n, float r, u32 col, int rays) {
    vec3 up(0, 0, 1);
    for (int k = 0; k <= rays; k++) {
        float a = kPi * k / rays;
        vec3 d = right * cosf(a) + up * sinf(a);
        beam(g, c + d * (r * 0.25f) + n * 0.04f, c + d * r + n * 0.04f, r * 0.07f, 0.08f, col, M(MAT_PLASTER), n);
    }
    // hub (half disc)
    MeshData& m = *g.m;
    u32 b0 = m.addVertex(c + n * 0.1f - g.org, n, right, vec2(0, 0), col, M(MAT_PLASTER));
    for (int k = 0; k <= 10; k++) {
        float a = kPi * k / 10;
        m.addVertex(c + (right * cosf(a) + up * sinf(a)) * (r * 0.22f) + n * 0.1f - g.org, n, right, vec2(0, 0), col, M(MAT_PLASTER));
    }
    for (int k = 0; k < 10; k++) {
        vec3 fn = cross(m.verts[b0 + 1 + k].pos - m.verts[b0].pos, m.verts[b0 + 2 + k].pos - m.verts[b0].pos);
        if (dot(fn, n) > 0) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
        else m.tri(b0, b0 + 2 + k, b0 + 1 + k);
    }
}

// ------------------------------------------------------------------------------------------------ furniture
// Bistro chair at p (floor z), sitter facing f (low poly: seat slab, raked back panel, two leg frames)
void bistroChair(G& g, vec2 p, float z, vec2 f, u32 col) {
    vec2 s = perp(f);
    u32 mat = M(MAT_METAL_PAINTED);
    boxY(g, V3(p, z + 0.45f), f, vec3(0.21f, 0.21f, 0.02f), col, mat, true);
    // back (behind the sitter), slightly raked: a double-sided panel
    vec2 bc = p - f * 0.2f;
    vec3 b0 = V3(bc - s * 0.2f, z + 0.47f), b1 = V3(bc + s * 0.2f, z + 0.47f), t1 = V3(bc + s * 0.2f - f * 0.05f, z + 0.88f), t0 = V3(bc - s * 0.2f - f * 0.05f, z + 0.88f);
    panel2(g, b0, b1, t1, t0, col, mat);
    if (!g.detail) return;
    // legs: two splayed frames (front pair, back pair) as thin double-sided strips
    u32 lc = rgb(0.15f);
    for (int k = -1; k <= 1; k += 2) {
        vec2 lp = p + f * (k * 0.17f);
        vec3 a0 = V3(lp - s * 0.19f, z), a1 = V3(lp + s * 0.19f, z), a2 = V3(lp + s * 0.17f, z + 0.44f), a3 = V3(lp - s * 0.17f, z + 0.44f);
        // only the leg edges: two slim quads per frame
        panel2(g, a0, a0 + V3(s * 0.03f, 0.f), a3 + V3(s * 0.03f, 0.f), a3, lc, mat);
        panel2(g, a1 - V3(s * 0.03f, 0.f), a1, a2, a2 - V3(s * 0.03f, 0.f), lc, mat);
    }
}

// Round cafe table at p (floor z), top radius r
void cafeTable(G& g, vec2 p, float z, float r, u32 topCol, u32 topMat) {
    cyl(g, V3(p, z + 0.72f), r, r, 0.03f, g.detail ? 10 : 6, topCol, topMat, true);
    cyl(g, V3(p, z), 0.03f, 0.03f, 0.72f, 4, rgb(0.15f), M(MAT_METAL_PAINTED), false);
    if (g.detail) cyl(g, V3(p, z), 0.24f, 0.2f, 0.04f, 6, rgb(0.15f), M(MAT_METAL_PAINTED), true);
}

// Square market umbrella (open) at p: pole, four sloped canopy panels in alternating colours, valance with optional text
void marketUmbrella(G& g, vec2 p, float z, float half, vec2 ax, u32 c0, u32 c1, const char* valance = nullptr, u32 textCol = 0) {
    vec2 ay = perp(ax);
    float zr = z + 2.35f, zt = z + 2.85f;
    cyl(g, V3(p, z), 0.025f, 0.025f, zt - z, 5, rgb(0.9f), M(MAT_METAL_PAINTED), false);
    vec2 cs[4] = {p - ax * half - ay * half, p + ax * half - ay * half, p + ax * half + ay * half, p - ax * half + ay * half};
    vec3 apex = V3(p, zt);
    for (int k = 0; k < 4; k++) {
        vec3 a = V3(cs[k], zr), b = V3(cs[(k + 1) % 4], zr);
        vec3 nn = normalize(cross(b - a, apex - a));
        if (nn.z < 0) nn = -nn;
        u32 cc = (k & 1) ? c1 : c0;
        MeshData& m = *g.m;
        for (int side = 0; side < 2; side++) {
            vec3 n2 = side ? -nn : nn;
            u32 i0 = m.addVertex(a - g.org, n2, normalize(b - a), vec2(0, 0), cc, M(MAT_FABRIC));
            u32 i1 = m.addVertex(b - g.org, n2, normalize(b - a), vec2(1, 0), cc, M(MAT_FABRIC));
            u32 i2 = m.addVertex(apex - g.org, n2, normalize(b - a), vec2(0.5f, 1), cc, M(MAT_FABRIC));
            bool ccw = dot(cross(b - a, apex - a), n2) > 0;
            if (ccw) m.tri(i0, i1, i2);
            else m.tri(i0, i2, i1);
        }
        if (g.detail) {
            // valance (hanging flap)
            vec2 on = normalize((cs[k] + cs[(k + 1) % 4]) * 0.5f - p);
            quad(g, *g.m, a, b, b - vec3(0, 0, 0.22f), a - vec3(0, 0, 0.22f), cc, M(MAT_FABRIC), V3(on, 0.f));
            quad(g, *g.m, b, a, a - vec3(0, 0, 0.22f), b - vec3(0, 0, 0.22f), cc, M(MAT_FABRIC), V3(-on, 0.f));
            if (valance && textCol) {
                vec2 rt = perp(on);
                float th = 0.13f;
                float tw = textAdvance(valance, th, 0.3f);
                if (tw < half * 1.8f) {
                    vec3 o = V3((cs[k] + cs[(k + 1) % 4]) * 0.5f + on * 0.012f, zr - 0.18f) - V3(rt * (tw * 0.5f), 0.f);
                    strokeText(g, *g.m, valance, o, V3(rt, 0.f), vec3(0, 0, 1), th, 0.02f, textCol, M(MAT_FABRIC), 0.f, 0.3f);
                }
            }
        }
    }
}

// Deco lamp post: fluted pole on a base, globe lantern (glowing at night) and its light
void decoLamp(G& g, vec2 p, float z, float h, vec3 glow, u32 poleCol) {
    cyl(g, V3(p, z), 0.2f, 0.17f, 0.5f, 8, poleCol, M(MAT_METAL_PAINTED), true);
    cyl(g, V3(p, z + 0.5f), 0.08f, 0.06f, h - 0.9f, 8, poleCol, M(MAT_METAL_PAINTED), false);
    cyl(g, V3(p, z + h - 0.45f), 0.12f, 0.12f, 0.08f, 8, poleCol, M(MAT_METAL_PAINTED), true);
    lathe(g, V3(p, z + h - 0.37f), {vec2(0.06f, 0.f), vec2(0.2f, 0.08f), vec2(0.24f, 0.25f), vec2(0.2f, 0.42f), vec2(0.f, 0.5f)}, g.detail ? 10 : 6,
          rgbv(glow, 0.5f), emMat(EA_NIGHT), false);
    light(g, V3(p, z + h - 0.1f), glow * 2200.f, 14.f, 0);
    collide(g, V3(p, z + h * 0.5f), vec2(1, 0), vec3(0.1f, 0.1f, h * 0.5f));
}

// Flag on a pole (fabric panel, two colours)
void flagPole(G& g, vec2 p, float z, float h, vec2 dir, vec3 c0, vec3 c1) {
    cyl(g, V3(p, z), 0.07f, 0.04f, h, 6, rgb(0.9f), M(MAT_METAL_BRUSHED), false);
    lathe(g, V3(p, z + h), {vec2(0.07f, 0.f), vec2(0.08f, 0.05f), vec2(0.f, 0.14f)}, 6, rgb(0.85f, 0.7f, 0.3f), M(MAT_METAL_BRUSHED), false);
    vec3 a = V3(p, z + h - 0.25f), b = V3(p + dir * 2.4f, z + h - 0.35f);
    panel2(g, a - vec3(0, 0, 1.4f), b - vec3(0, 0, 1.4f), b - vec3(0, 0, 0.7f), a - vec3(0, 0, 0.7f), rgbv(c1), M(MAT_FABRIC));
    panel2(g, a - vec3(0, 0, 0.7f), b - vec3(0, 0, 0.7f), b, a, rgbv(c0), M(MAT_FABRIC));
    collide(g, V3(p, z + h * 0.5f), vec2(1, 0), vec3(0.07f, 0.07f, h * 0.5f));
}

// Monument sign: low stone plinth, raised letters (two lines), optional night glow on the letters
void monumentSign(G& g, vec2 c, vec2 face, float z, float w, float h, const char* l1, const char* l2, u32 stoneCol, u32 stoneMat, u32 letterCol,
                  bool glow) {
    vec2 rt = perp(face);
    boxY(g, V3(c, z + h * 0.5f), rt, vec3(w * 0.5f, 0.45f, h * 0.5f), stoneCol, stoneMat, false);
    boxY(g, V3(c, z + h + 0.06f), rt, vec3(w * 0.5f + 0.1f, 0.55f, 0.06f), stoneCol, stoneMat, true);
    boxY(g, V3(c, z + 0.1f), rt, vec3(w * 0.5f + 0.15f, 0.6f, 0.1f), stoneCol, stoneMat, false);
    collide(g, V3(c, z + h * 0.5f), rt, vec3(w * 0.5f, 0.45f, h * 0.5f));
    float h1 = Min(h * 0.32f, (w - 0.6f) / Max(1.f, textAdvance(l1, 1.f, 0.3f)));
    vec3 fo = V3(c + face * 0.46f, 0.f);
    u32 lm = glow ? emMat(EA_NIGHT) : M(MAT_METAL_BRUSHED);
    u32 lc = glow ? (letterCol & 0x00ffffffu) | (90u << 24) : letterCol;
    float y1 = l2 && *l2 ? z + h * 0.52f : z + h * 0.35f;
    strokeText(g, *g.m, l1, fo + V3(-rt * (textAdvance(l1, h1, 0.3f) * 0.5f), y1), V3(rt, 0.f), vec3(0, 0, 1), h1, h1 * 0.13f, lc, lm,
               g.detail ? 0.04f : 0.f, 0.3f);
    if (l2 && *l2) {
        float h2 = Min(h * 0.18f, (w - 0.6f) / Max(1.f, textAdvance(l2, 1.f, 0.3f)));
        strokeText(g, *g.m, l2, fo + V3(-rt * (textAdvance(l2, h2, 0.3f) * 0.5f), z + h * 0.2f), V3(rt, 0.f), vec3(0, 0, 1), h2, h2 * 0.12f, lc, lm,
                   g.detail ? 0.03f : 0.f, 0.3f);
    }
    if (glow) light(g, V3(c + face * 2.5f, z + 0.3f), vec3(1.f, 0.9f, 0.75f) * 1800.f, 8.f, 1, normalize(vec3(-face, 0.5f)), 0.25f);
}

// Stepped bleacher block along `along` (rows rising away from the field, facing `face`): seat planks, risers, aisle,
// back rail; collides as a ramp of boxes
void bleachers(G& g, vec2 c, vec2 along, vec2 face, float z, float len, int rows, float rowD, float rowH, u32 seatCol, u32 frameCol) {
    vec2 back = -face;
    for (int r = 0; r < rows; r++) {
        vec2 rc = c + back * (r * rowD);
        float zt = z + 0.45f + r * rowH;
        boxY(g, V3(rc, zt), along, vec3(len * 0.5f, 0.2f, 0.03f), seatCol, M(MAT_METAL_PAINTED), true);
        boxY(g, V3(rc - face * 0.3f, zt - rowH * 0.5f + 0.02f), along, vec3(len * 0.5f, rowD * 0.35f, 0.02f), frameCol, M(MAT_METAL_BRUSHED), true);
        collide(g, V3(rc - face * 0.1f, (z + zt) * 0.5f), along, vec3(len * 0.5f, rowD * 0.5f, (zt - z) * 0.5f));
    }
    if (!g.detail) return;
    // frames under the rows
    int legs = Max(2, (int)(len / 3.f));
    for (int k = 0; k <= legs; k++) {
        vec2 lp = c + along * (-len * 0.5f + len * k / legs);
        vec2 top = lp + back * ((rows - 1) * rowD);
        beam(g, V3(lp, z), V3(top, z + 0.45f + (rows - 1) * rowH), 0.08f, 0.08f, frameCol, M(MAT_METAL_BRUSHED));
        beam(g, V3(top, z), V3(top, z + 0.45f + (rows - 1) * rowH + 1.f), 0.06f, 0.06f, frameCol, M(MAT_METAL_BRUSHED));
    }
    vec2 bt = c + back * ((rows - 1) * rowD + 0.25f);
    float zr = z + 0.45f + (rows - 1) * rowH + 1.f;
    beam(g, V3(bt - along * (len * 0.5f), zr), V3(bt + along * (len * 0.5f), zr), 0.05f, 0.05f, frameCol, M(MAT_METAL_BRUSHED));
}

// Parked car shim (airport_mesh has the model)
inline void car(G& g, vec2 c, vec2 fwd, float z, u32 col) { airport_mesh::parkedCar(g, c, fwd, z, col, g.detail); }

// Random paint colour for parked cars (muted most of the time)
inline u32 carColour(u32 h) {
    vec3 col = hsvToRgb(hashToFloat(h >> 4), hashToFloat(h >> 12) < 0.55f ? 0.05f : 0.6f, 0.2f + hashToFloat(h >> 16) * 0.72f);
    return rgbv(col);
}

// Row of parking stalls along `along` (stall width 2.6 m, depth d) in front of the kerb line at c (cars face `face`)
void stallRow(G& g, vec2 c, vec2 along, vec2 face, float z, int stalls, float d, float fill, u32 seed, bool lines = true) {
    for (int k = 0; k <= stalls; k++) {
        vec2 lp = c + along * ((k - stalls * 0.5f) * 2.6f);
        if (lines && g.detail && g.owns(lp)) paintLine(g, lp, lp + face * d, 0.1f, z + 0.02f, kWhiteC, M(MAT_PAINT_WHITE));
        if (k == stalls) break;
        vec2 sc = c + along * ((k + 0.5f - stalls * 0.5f) * 2.6f) + face * (d * 0.5f);
        u32 h = hash32(seed + (u32)k * 7919u);
        if (!g.owns(sc) || hashToFloat(h) > fill) continue;
        vec2 fwd = (h & 1) ? face : -face;
        car(g, sc, fwd, z, carColour(h));
        if (g.detail) collide(g, V3(sc, z + 0.75f), fwd, vec3(2.2f, 0.9f, 0.75f));
    }
}

// ------------------------------------------------------------------------------------------------ roofs, trees, small monuments
// Triangle with its normal turned toward `facing`
void tri3(G& g, vec3 a, vec3 b, vec3 c, u32 col, u32 mat, vec3 facing) {
    if (dot(cross(b - a, c - a), facing) < 0.f) std::swap(b, c);
    vec3 n = normalize(cross(b - a, c - a));
    vec3 t = normalize(b - a), bt = cross(n, t);
    MeshData& m = *g.m;
    u32 i0 = m.addVertex(a - g.org, n, t, vec2(0.f, 0.f), col, mat);
    u32 i1 = m.addVertex(b - g.org, n, t, vec2(dot(b - a, t), dot(b - a, bt)), col, mat);
    u32 i2 = m.addVertex(c - g.org, n, t, vec2(dot(c - a, t), dot(c - a, bt)), col, mat);
    m.tri(i0, i1, i2);
}

// Gable roof with its ridge along `f` over the w x d rectangle centred at c whose walls top out at z: both slopes pass
// through the wall heads and the ridge (rise over the centre line) and run on `over` past the side walls and the gable
// ends; soffits close the eaves from below; the gable triangles stand on the front and back wall planes
void gableRoof(G& g, vec2 c, vec2 f, float w, float d, float z, float rise, float over, u32 roofCol, u32 roofMat, u32 gableCol, u32 gableMat,
               bool soffit = false) {
    vec2 R(f.y, -f.x);
    float hw = w * 0.5f, hd = d * 0.5f + over, slope = rise / Max(hw, 0.01f);
    float ze = z - slope * over;
    const vec3 up(0, 0, 1);
    for (int s = -1; s <= 1; s += 2) {
        vec2 side = R * (float)s;
        vec3 e0 = V3(c + side * (hw + over) - f * hd, ze), e1 = V3(c + side * (hw + over) + f * hd, ze);
        vec3 r0 = V3(c - f * hd, z + rise), r1 = V3(c + f * hd, z + rise);
        quad(g, *g.m, e0, e1, r1, r0, roofCol, roofMat, normalize(V3(side, 0.f) * rise + up * hw));
        if (soffit && over > 0.f) quad(g, *g.m, e0, e1, V3(c + side * hw + f * hd, z), V3(c + side * hw - f * hd, z), roofCol, roofMat, -up);
    }
    for (int s = -1; s <= 1; s += 2) {
        vec2 fc = c + f * (d * 0.5f * (float)s);
        tri3(g, V3(fc - R * hw, z), V3(fc + R * hw, z), V3(fc, z + rise), gableCol, gableMat, V3(f * (float)s, 0.f));
    }
}

// Italian (columnar) cypress: a dark spindle of foliage on a short trunk, h tall, r at its widest
void italianCypress(G& g, vec2 p, float z, float h, float r) {
    if (g.detail) cyl(g, V3(p, z - 0.1f), 0.13f, 0.1f, 0.9f, 5, rgb(0.36f, 0.3f, 0.24f), M(MAT_BARK), false);
    lathe(g, V3(p, z + 0.5f),
          {vec2(r * 0.45f, 0.f), vec2(r * 0.9f, h * 0.14f), vec2(r, h * 0.34f), vec2(r * 0.88f, h * 0.58f), vec2(r * 0.55f, h * 0.8f),
           vec2(r * 0.2f, h * 0.93f - 0.5f), vec2(0.f, h - 0.5f)},
          g.detail ? 9 : 5, rgb(0.22f, 0.33f, 0.2f), M(MAT_LEAVES), false);
    if (g.detail) collide(g, V3(p, z + 1.f), vec2(1, 0), vec3(0.15f, 0.15f, 1.f));
}

// Latin cross standing on z (stone or iron), h tall, bar thickness t, arms facing `face`
void latinCross(G& g, vec2 p, vec2 face, float z, float h, float t, u32 col, u32 mat) {
    vec2 R(face.y, -face.x);
    boxY(g, V3(p, z + h * 0.5f), R, vec3(t * 0.5f, t * 0.5f, h * 0.5f), col, mat);
    boxY(g, V3(p, z + h * 0.7f), R, vec3(h * 0.27f, t * 0.5f, t * 0.5f), col, mat);
}

// Funerary urn on a small foot (gate piers, tomb corners, parapets), s = overall height
void urn(G& g, vec3 base, float s, u32 col, u32 mat) {
    lathe(g, base,
          {vec2(0.2f * s, 0.f), vec2(0.13f * s, 0.12f * s), vec2(0.3f * s, 0.34f * s), vec2(0.33f * s, 0.5f * s), vec2(0.2f * s, 0.7f * s),
           vec2(0.25f * s, 0.78f * s), vec2(0.08f * s, 0.9f * s), vec2(0.f, s)},
          g.detail ? 6 : 4, col, mat, false);
}

// Cast-iron lantern on a post (cemeteries, church yards, old squares): glows at night and lights its surroundings
void lanternPost(G& g, vec2 p, float z, float h, vec3 glow) {
    u32 iron = rgb(0.07f, 0.07f, 0.07f), im = M(MAT_METAL_PAINTED);
    cyl(g, V3(p, z), 0.16f, 0.12f, 0.55f, 8, iron, im, true);
    cyl(g, V3(p, z + 0.55f), 0.06f, 0.05f, h - 1.15f, 6, iron, im, false);
    boxY(g, V3(p, z + h - 0.6f), vec2(1, 0), vec3(0.14f, 0.14f, 0.03f), iron, im, true);
    boxY(g, V3(p, z + h - 0.33f), vec2(1, 0), vec3(0.12f, 0.12f, 0.24f), rgbv(glow, 0.5f), emMat(EA_NIGHT), false);
    lathe(g, V3(p, z + h - 0.09f), {vec2(0.24f, 0.f), vec2(0.06f, 0.16f), vec2(0.03f, 0.26f), vec2(0.f, 0.3f)}, 4, iron, im, false, kPi * 0.25f);
    light(g, V3(p, z + h - 0.35f), glow * 1500.f, 12.f, 0);
    collide(g, V3(p, z + h * 0.5f), vec2(1, 0), vec3(0.08f, 0.08f, h * 0.5f));
}

}  // namespace place_kit
}  // namespace World
