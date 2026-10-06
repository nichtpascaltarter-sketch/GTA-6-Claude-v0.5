// Churchyards beside the town churches: in Okahatchee a white clapboard church with a steeple and its graveyard inside a
// picket fence, in Fort Castell a red brick church with a square tower and its graveyard behind a low brick wall with iron
// railings. Each takes a straight street frontage near the town centre once the lots exist (the houses there make way):
// the church stands at one end, set back behind a brick path from the sidewalk; beside it rows of headstones (rounded,
// pointed, cross-topped, slabs), table tombs, ledgers, family plots inside low railings and an obelisk, under a live oak
// and cedars; a gate (lychgate or iron gate between piers) opens on the centre path, and a sign board by the sidewalk
// gives the church's name and service time. The church itself is the chapel generator's clapboard or brick variant.
#include "places.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace churchyard {

using namespace sitegeo;
using namespace place_kit;

// Plot frame: u along the street (c.x..), v away from it from the lot line (0) to the back (D)
struct Frame {
    vec2 c0, along, out;   // c0: middle of the lot line on the street side
    vec2 P(float u, float v) const { return c0 + along * u + out * v; }
};
Frame frameOf(const SiteElem& e) {
    Frame f;
    f.along = e.ax;
    f.out = vec2(e.p[6], e.p[7]);
    f.c0 = e.c - f.out * e.hy;
    return f;
}

// Graveyard extent inside the plot (u0..u1, v0..v1) and the centre path's u
struct Yard {
    float u0, u1, v0, v1, pathU;
};
Yard yardOf(const SiteElem& e) { return {e.p[1], e.p[2], 3.2f, e.hy * 2.f - 2.5f, (e.p[1] + e.p[2]) * 0.5f}; }

enum GraveKind : u8 { GK_ROUNDED = 0, GK_POINTED, GK_CROSS, GK_SLAB, GK_TABLE, GK_LEDGER, GK_OBELISK, GK_FAMILY };
struct Grave {
    float u, v;   // head end (the stone), the grave runs on toward +v
    u8 kind;
    u32 h;
};

// Rows of graves facing the street, both sides of the centre path; some plots left empty
void gravesOf(const SiteElem& e, std::vector<Grave>& out) {
    const Yard y = yardOf(e);
    const float pitchU = 1.9f, pitchV = 3.3f;
    int row = 0;
    for (float v = y.v0 + 1.2f; v < y.v1 - 2.3f; v += pitchV, row++)
        for (int side = -1; side <= 1; side += 2) {
            float ua = side < 0 ? y.u0 + 1.2f : y.pathU + 1.6f, ub = side < 0 ? y.pathU - 1.6f : y.u1 - 1.2f;
            int n = Max(0, (int)floorf((ub - ua) / pitchU));
            for (int k = 0; k < n; k++) {
                Grave g;
                g.h = hash32(e.seed * 7919u + (u32)row * 131u + (u32)(k * 2 + (side > 0)) * 2654435761u);
                float r = hashToFloat(g.h);
                if (r < 0.13f) continue;   // empty plot
                g.u = ua + pitchU * (k + 0.5f);
                g.v = v;
                float r2 = hashToFloat(g.h >> 8);
                g.kind = r2 < 0.3f ? GK_ROUNDED
                                   : (r2 < 0.5f ? GK_POINTED
                                                : (r2 < 0.6f ? GK_CROSS : (r2 < 0.75f ? GK_SLAB : (r2 < 0.83f ? GK_TABLE : (r2 < 0.93f ? GK_LEDGER : (r2 < 0.96f ? GK_OBELISK : GK_FAMILY))))));
                out.push_back(g);
            }
        }
}

// ------------------------------------------------------------------------------------------------ mesh
void grave(G& g, const Frame& F, const Grave& gv, int style) {
    const vec2 f = -F.out, R = F.along;   // stones face the street; R: across
    const vec2 p = F.P(gv.u, gv.v);
    if (!g.owns(p)) return;
    Rng r(gv.h);
    auto zAt = [&](vec2 q) { return gMap->heightAt(q.x, q.y); };
    const float z = zAt(p);
    const bool detail = g.detail;
    vec3 sc = r.chance(0.5f) ? vec3(0.86f, 0.86f, 0.83f) : (r.chance(0.5f) ? vec3(0.62f, 0.62f, 0.63f) : vec3(0.7f, 0.66f, 0.6f));
    if (style == 1 && r.chance(0.3f)) sc = vec3(0.45f, 0.44f, 0.46f);   // darker granite in the mill town
    sc = sc * r.range(0.88f, 1.05f);
    const u32 st = tint(sc), sm = r.chance(0.6f) ? M(MAT_STONE) : M(MAT_MARBLE), ink = rgb(0.18f, 0.17f, 0.16f);
    const float lean = detail && (gv.kind == GK_SLAB || gv.kind == GK_FAMILY) ? r.range(-0.035f, 0.035f) : 0.f;   // old slabs lean a little
    auto stone = [&](float w, float h, float t) {   // upright slab with its face toward the street
        vec3 up = normalize(vec3(0, 0, 1) + V3(f, 0.f) * lean);
        g.m->box(V3(p, z - 0.25f) + up * ((h + 0.25f) * 0.5f) - g.org, V3(R, 0.f), V3(f, 0.f), up, vec3(w * 0.5f, t * 0.5f, (h + 0.25f) * 0.5f), st, sm, false);
        if (detail) {
            for (int k = 0; k < 3; k++) {
                float zc = z + h * (0.72f - k * 0.15f), hw = w * (k == 0 ? 0.32f : 0.24f);
                vec2 fp = p + f * (t * 0.5f + 0.004f + lean * (zc - z + 0.25f));
                quad(g, *g.m, V3(fp - R * hw, zc - 0.018f), V3(fp + R * hw, zc - 0.018f), V3(fp + R * hw, zc + 0.018f), V3(fp - R * hw, zc + 0.018f), ink,
                     M(MAT_STONE), V3(f, 0.f));
            }
        }
        collide(g, V3(p, z + h * 0.5f), R, vec3(w * 0.5f, t * 0.5f + 0.02f, h * 0.5f));
    };
    auto mound = [&](float len) {   // the grave itself: a low grassy mound or a gravel bed behind the stone
        if (!detail) return;
        vec2 a = p - f * 0.2f, b = p - f * (len + 0.2f);
        bool gravel = r.chance(0.3f);
        float zm = z + 0.05f;
        quad(g, *g.m, V3(a - R * 0.45f, zm), V3(a + R * 0.45f, zm), V3(b + R * 0.45f, zm), V3(b - R * 0.45f, zm), gravel ? rgb(0.82f, 0.8f, 0.76f) : rgb(0.56f, 0.8f, 0.4f),
             gravel ? M(MAT_PLASTER) : M(MAT_GRASS), vec3(0, 0, 1));
    };
    switch (gv.kind) {
        case GK_ROUNDED: {
            float w = r.range(0.5f, 0.7f), h = r.range(0.65f, 0.95f), t = 0.1f, rr = w * 0.5f;
            stone(w, h - rr, t);
            // round head: half discs on both faces and the curved edge between them
            const float zs = z + h - rr;
            const int seg = detail ? 7 : 3;
            for (int sd = -1; sd <= 1; sd += 2)
                cemetery::halfDisc(g, V3(p + f * (sd * t * 0.5f), zs), V3(R, 0.f), V3(f * (float)sd, 0.f), rr, seg, st, sm);
            for (int k = 0; k < seg; k++) {
                float a0 = kPi * k / seg, a1 = kPi * (k + 1) / seg;
                vec3 d0 = V3(R * cosf(a0), sinf(a0)), d1 = V3(R * cosf(a1), sinf(a1));
                vec3 c = V3(p, zs);
                quad(g, *g.m, c + d0 * rr + V3(f * 0.05f, 0.f), c + d1 * rr + V3(f * 0.05f, 0.f), c + d1 * rr - V3(f * 0.05f, 0.f), c + d0 * rr - V3(f * 0.05f, 0.f), st, sm,
                     normalize(d0 + d1));
            }
            mound(1.9f);
            break;
        }
        case GK_POINTED: {
            float w = r.range(0.45f, 0.6f), h = r.range(0.7f, 1.f);
            stone(w, h - 0.18f, 0.1f);
            tri3(g, V3(p + f * 0.051f - R * (w * 0.5f), z + h - 0.18f), V3(p + f * 0.051f + R * (w * 0.5f), z + h - 0.18f), V3(p + f * 0.051f, z + h), st, sm, V3(f, 0.f));
            tri3(g, V3(p - f * 0.051f - R * (w * 0.5f), z + h - 0.18f), V3(p - f * 0.051f + R * (w * 0.5f), z + h - 0.18f), V3(p - f * 0.051f, z + h), st, sm, V3(-f, 0.f));
            for (int s = -1; s <= 1; s += 2) {   // the sloping edges of the point
                vec3 a0 = V3(p + R * (s * w * 0.5f) + f * 0.051f, z + h - 0.18f), a1 = V3(p + R * (s * w * 0.5f) - f * 0.051f, z + h - 0.18f);
                vec3 t0 = V3(p + f * 0.051f, z + h), t1 = V3(p - f * 0.051f, z + h);
                quad(g, *g.m, a0, a1, t1, t0, st, sm, normalize(V3(R * (float)s, 0.f) * 0.18f + vec3(0, 0, w * 0.5f)));
            }
            mound(1.9f);
            break;
        }
        case GK_CROSS: {
            boxY(g, V3(p, z + 0.1f), R, vec3(0.3f, 0.2f, 0.12f), st, sm);
            latinCross(g, p, f, z + 0.2f, r.range(0.9f, 1.3f), 0.11f, st, sm);
            collide(g, V3(p, z + 0.6f), R, vec3(0.3f, 0.2f, 0.6f));
            mound(1.9f);
            break;
        }
        case GK_SLAB: {
            stone(r.range(0.7f, 0.95f), r.range(0.45f, 0.65f), 0.14f);
            mound(1.9f);
            break;
        }
        case GK_TABLE: {   // table tomb: a slab on four short legs over a low chest
            vec2 c = p - f * 1.05f;
            boxY(g, V3(c, z + 0.18f), R, vec3(0.42f, 0.95f, 0.26f), st, sm);
            boxY(g, V3(c, z + 0.52f), R, vec3(0.55f, 1.08f, 0.06f), st, sm);
            if (detail)
                for (int k = 0; k < 4; k++) boxY(g, V3(c + R * ((k & 1) ? 0.42f : -0.42f) + f * ((k & 2) ? 0.9f : -0.9f), z + 0.3f), R, vec3(0.06f, 0.06f, 0.16f), st, sm);
            collide(g, V3(c, z + 0.3f), R, vec3(0.55f, 1.08f, 0.3f));
            break;
        }
        case GK_LEDGER: {
            vec2 c = p - f * 1.f;
            boxY(g, V3(c, z + 0.05f), R, vec3(0.45f, 1.f, 0.12f), st, sm);
            if (detail)
                for (int k = 0; k < 2; k++) {
                    vec2 lc = c + f * (0.45f - k * 0.2f);
                    quad(g, *g.m, V3(lc - R * 0.22f - f * 0.02f, z + 0.171f), V3(lc + R * 0.22f - f * 0.02f, z + 0.171f), V3(lc + R * 0.22f + f * 0.02f, z + 0.171f),
                         V3(lc - R * 0.22f + f * 0.02f, z + 0.171f), ink, M(MAT_STONE), vec3(0, 0, 1));
                }
            break;
        }
        case GK_OBELISK: {
            boxY(g, V3(p, z + 0.1f), R, vec3(0.45f, 0.45f, 0.25f), st, sm);
            boxY(g, V3(p, z + 0.55f), R, vec3(0.3f, 0.3f, 0.2f), st, sm);
            cyl(g, V3(p, z + 0.75f), 0.3f, 0.18f, 1.8f, 4, st, sm, false);
            cyl(g, V3(p, z + 2.55f), 0.18f, 0.f, 0.25f, 4, st, sm, false);
            collide(g, V3(p, z + 1.4f), R, vec3(0.45f, 0.45f, 1.4f));
            mound(1.9f);
            break;
        }
        case GK_FAMILY: {   // two stones inside a low iron railing
            stone(0.55f, 0.75f, 0.1f);
            if (detail) {
                vec2 q0 = p + f * 0.35f - R * 0.85f, q1 = p + f * 0.35f + R * 0.85f, q2 = p - f * 2.3f + R * 0.85f, q3 = p - f * 2.3f - R * 0.85f;
                u32 iron = rgb(0.07f);
                ironRailing(g, q0, q1, z, 0.6f, 0.3f, iron, false, false);
                ironRailing(g, q1, q2, z, 0.6f, 0.3f, iron, false, false);
                ironRailing(g, q2, q3, z, 0.6f, 0.3f, iron, false, false);
                ironRailing(g, q3, q0, z, 0.6f, 0.3f, iron, false, false);
            }
            mound(1.9f);
            break;
        }
        default: break;
    }
    // flowers left at a few graves
    if (detail && ((gv.h >> 21) % 7u) == 0u) {
        vec2 fp = p + f * 0.25f + R * r.range(-0.2f, 0.2f);
        const vec3 bloom[4] = {vec3(0.85f, 0.12f, 0.15f), vec3(1.f, 0.55f, 0.75f), vec3(1.f, 0.85f, 0.2f), vec3(0.97f, 0.97f, 0.95f)};
        lathe(g, V3(fp, z), {vec2(0.05f, 0.f), vec2(0.18f, 0.12f), vec2(0.14f, 0.26f), vec2(0.f, 0.3f)}, 6, tint(bloom[(gv.h >> 25) & 3]), M(MAT_PLASTER), false);
    }
}

// SK_CHURCHYARD: c / ax / hx / hy: the plot (ax along the street); p[0]: style (0 picket fence, 1 brick wall); p[1..2]:
// graveyard u range; p[3]: church door u; p[4]: church front v; p[5]: sidewalk centre line (v, negative); p[6..7]: out
void genChurchyard(const SiteElem& e, G& g) {
    const bool detail = g.detail;
    const Frame F = frameOf(e);
    const Yard y = yardOf(e);
    const int style = (int)e.p[0];
    const float W = e.hx * 2.f, D = e.hy * 2.f, doorU = e.p[3], frontV = e.p[4];
    auto zAt = [&](vec2 q) { return gMap->heightAt(q.x, q.y); };
    const u32 white = rgb(0.96f, 0.96f, 0.94f), brick = rgb(0.6f, 0.3f, 0.24f), capC = rgb(0.8f, 0.78f, 0.72f), iron = rgb(0.07f);
    // ---- the grounds: mown grass over the plot, the brick path to the church door, the gravel path through the graveyard
    drapeRect(g, F.P(0.f, D * 0.5f), F.along, W * 0.5f - 0.3f, D * 0.5f - 0.3f, 0.04f, rgb(0.6f, 0.84f, 0.44f), M(MAT_GRASS), detail ? 6.f : 20.f);
    pathStrip(g, {F.P(doorU, -0.55f), F.P(doorU, frontV - 1.2f)}, 2.2f, 0.06f, rgb(0.8f, 0.62f, 0.52f), M(MAT_PAVERS), -1e9f, true, true);
    pathStrip(g, {F.P(y.pathU, -0.55f), F.P(y.pathU, y.v1)}, 1.8f, 0.05f, rgb(0.84f, 0.82f, 0.77f), M(MAT_PLASTER), -1e9f, false, false);
    pathStrip(g, {F.P(doorU + 1.1f, frontV - 2.f), F.P(y.pathU - 0.9f, frontV - 2.f)}, 1.6f, 0.05f, rgb(0.84f, 0.82f, 0.77f), M(MAT_PLASTER), -1e9f, false, false);
    // ---- the boundary: picket fence or low brick wall with railings, gaps for the two paths
    auto fenceRun = [&](float ua, float va, float ub, float vb) {
        vec2 a = F.P(ua, va), b = F.P(ub, vb);
        float L = length(b - a);
        if (L < 0.3f) return;
        int n = Max(1, (int)ceilf(L / 12.f));
        for (int k = 0; k < n; k++) {
            vec2 p0 = lerp(a, b, (float)k / n), p1 = lerp(a, b, (float)(k + 1) / n);
            vec2 m = (p0 + p1) * 0.5f;
            if (!g.owns(m)) continue;
            float z0 = Min(zAt(p0), zAt(p1));
            if (style == 0) {
                // white pickets on two rails
                vec2 d = normalize(p1 - p0);
                float l = length(p1 - p0);
                beam(g, V3(p0, z0 + 0.35f), V3(p1, z0 + 0.35f), 0.04f, 0.08f, white, M(MAT_PLASTER));
                beam(g, V3(p0, z0 + 0.85f), V3(p1, z0 + 0.85f), 0.04f, 0.08f, white, M(MAT_PLASTER));
                if (detail) {
                    int np = Max(1, (int)(l / 0.2f));
                    for (int q = 0; q <= np; q++) {   // pickets: flat boards with pointed heads, seen from both sides
                        vec2 pp = p0 + d * (l * q / np) + perp(d) * 0.03f;
                        vec3 b0 = V3(pp - d * 0.04f, z0 - 0.05f), b1 = V3(pp + d * 0.04f, z0 - 0.05f);
                        vec3 t0 = V3(pp - d * 0.04f, z0 + 0.98f), t1 = V3(pp + d * 0.04f, z0 + 0.98f);
                        panel2(g, b0, b1, t1, t0, white, M(MAT_PLASTER));
                        tri3(g, t0, t1, V3(pp, z0 + 1.07f), white, M(MAT_PLASTER), V3(perp(d), 0.f));
                        tri3(g, t0, t1, V3(pp, z0 + 1.07f), white, M(MAT_PLASTER), V3(-perp(d), 0.f));
                    }
                } else quad(g, *g.m, V3(p0, z0), V3(p1, z0), V3(p1, z0 + 1.05f), V3(p0, z0 + 1.05f), white, M(MAT_PLASTER), V3(perp(d), 0.f));
                collide(g, V3(m, z0 + 0.55f), d, vec3(l * 0.5f, 0.06f, 0.55f));
            } else {
                copedWall(g, p0, p1, z0 - 0.2f, 0.85f, 0.36f, brick, M(MAT_BRICK), capC, M(MAT_STONE), true);
                if (detail) {   // iron railing on the coping: two rails, flat bars
                    vec2 d = normalize(p1 - p0);
                    float l = length(p1 - p0);
                    for (float zz : {0.8f, 1.5f}) beam(g, V3(p0, z0 + zz), V3(p1, z0 + zz), 0.03f, 0.03f, iron, M(MAT_METAL_PAINTED));
                    int np = Max(1, (int)(l / 0.24f));
                    for (int q = 0; q <= np; q++) {
                        vec2 pp = p0 + d * (l * q / np);
                        panel2(g, V3(pp - d * 0.012f, z0 + 0.72f), V3(pp + d * 0.012f, z0 + 0.72f), V3(pp + d * 0.012f, z0 + 1.62f), V3(pp - d * 0.012f, z0 + 1.62f), iron,
                               M(MAT_METAL_PAINTED));
                    }
                }
            }
        }
    };
    const float gateHW = 1.3f, doorHW = 1.4f;
    // street side (v = 0): gaps at the church path and the graveyard gate
    std::vector<float> cuts = {doorU - doorHW, doorU + doorHW, y.pathU - gateHW, y.pathU + gateHW};
    std::sort(cuts.begin(), cuts.end());
    float ua = -W * 0.5f;
    for (size_t k = 0; k < cuts.size(); k += 2) {
        fenceRun(ua, 0.3f, cuts[k], 0.3f);
        ua = cuts[k + 1];
    }
    fenceRun(ua, 0.3f, W * 0.5f, 0.3f);
    fenceRun(W * 0.5f, 0.3f, W * 0.5f, D - 0.3f);
    fenceRun(W * 0.5f, D - 0.3f, -W * 0.5f, D - 0.3f);
    fenceRun(-W * 0.5f, D - 0.3f, -W * 0.5f, 0.3f);
    // ---- the graveyard gate: lychgate (roofed) or iron gate between brick piers
    vec2 gc = F.P(y.pathU, 0.3f);
    if (g.owns(gc)) {
        float z0 = zAt(gc);
        if (style == 0) {
            for (int s = -1; s <= 1; s += 2)
                for (int t = -1; t <= 1; t += 2) {
                    vec2 pp = F.P(y.pathU + s * (gateHW + 0.1f), 0.3f + t * 0.8f);
                    boxY(g, V3(pp, z0 + 1.2f), F.along, vec3(0.09f, 0.09f, 1.25f), white, M(MAT_PLASTER));
                }
            gableRoof(g, gc, F.along, 1.8f, gateHW * 2.f + 0.5f, z0 + 2.45f, 0.9f, 0.25f, rgb(0.32f, 0.33f, 0.36f), M(MAT_ROOF_SHINGLE), white, M(MAT_PLASTER), true);
            collide(g, V3(F.P(y.pathU - gateHW - 0.1f, 0.3f), z0 + 1.2f), F.along, vec3(0.12f, 0.9f, 1.2f));
            collide(g, V3(F.P(y.pathU + gateHW + 0.1f, 0.3f), z0 + 1.2f), F.along, vec3(0.12f, 0.9f, 1.2f));
        } else {
            for (int s = -1; s <= 1; s += 2) {
                vec2 pp = F.P(y.pathU + s * (gateHW + 0.3f), 0.3f);
                boxY(g, V3(pp, z0 + 0.9f), F.along, vec3(0.3f, 0.3f, 1.1f), brick, M(MAT_BRICK));
                boxY(g, V3(pp, z0 + 2.06f), F.along, vec3(0.36f, 0.36f, 0.06f), capC, M(MAT_STONE));
                lathe(g, V3(pp, z0 + 2.12f), {vec2(0.12f, 0.f), vec2(0.22f, 0.1f), vec2(0.18f, 0.28f), vec2(0.f, 0.34f)}, 8, capC, M(MAT_STONE), false);
                collide(g, V3(pp, z0 + 1.f), F.along, vec3(0.3f, 0.3f, 1.f));
                // leaves standing open
                vec2 hinge = F.P(y.pathU + s * gateHW, 0.3f);
                vec2 dir = normalize(F.along * (float)(-s) * 0.15f + F.out);
                vec2 b = hinge + dir * (gateHW - 0.05f);
                if (detail) {
                    for (float zz : {0.15f, 1.55f}) beam(g, V3(hinge, z0 + zz), V3(b, z0 + zz), 0.04f, 0.05f, iron, M(MAT_METAL_PAINTED));
                    for (int q = 0; q <= 9; q++) {
                        vec2 pp2 = lerp(hinge, b, q / 9.f);
                        boxY(g, V3(pp2, z0 + 0.85f), dir, vec3(0.012f, 0.012f, 0.8f), iron, M(MAT_METAL_PAINTED));
                    }
                }
            }
        }
    }
    // ---- sign board by the church path: the church's name and the service time, a lamp over it at night
    vec2 sb = F.P(doorU - doorHW - 2.6f, 1.4f);
    if (g.owns(sb)) {
        float z0 = zAt(sb);
        vec2 fr = -F.out;
        for (int s = -1; s <= 1; s += 2) boxY(g, V3(sb + F.along * (s * 1.05f), z0 + 0.8f), F.along, vec3(0.06f, 0.06f, 0.85f), white, M(MAT_PLASTER));
        boxY(g, V3(sb, z0 + 1.35f), F.along, vec3(1.15f, 0.06f, 0.5f), style == 0 ? white : rgb(0.18f, 0.22f, 0.3f), M(MAT_PLASTER));
        const u32 tc = style == 0 ? rgb(0.12f, 0.2f, 0.32f) : rgb(0.95f, 0.93f, 0.85f);
        const vec3 right = V3(vec2(F.out.y, -F.out.x), 0.f);   // reading left to right from the sidewalk
        const vec3 face = V3(sb + fr * 0.065f, 0.f);
        const char* l1 = e.text.c_str();
        float h1 = Min(0.2f, 2.1f / Max(1.f, textAdvance(l1, 1.f, 0.3f)));
        plainLetters(g, l1, centredOrigin(l1, face + vec3(0, 0, z0 + 1.52f), right, h1), right, vec3(0, 0, 1), h1, tc, M(MAT_PAINT_WHITE), 0.f, 0.14f);
        const char* l2 = style == 0 ? "SUNDAY SERVICE 10 AM" : "SUNDAY MASS 9 AND 11";
        plainLetters(g, l2, centredOrigin(l2, face + vec3(0, 0, z0 + 1.1f), right, 0.11f), right, vec3(0, 0, 1), 0.11f, tc, M(MAT_PAINT_WHITE), 0.f, 0.14f);
        collide(g, V3(sb, z0 + 1.f), F.along, vec3(1.2f, 0.1f, 1.f));
        light(g, V3(sb + fr * 1.2f, z0 + 2.4f), vec3(1.f, 0.92f, 0.8f) * 900.f, 5.f, 1, normalize(vec3(-fr, -1.f)), 0.2f);
    }
    // ---- lamps along the church path, benches in the graveyard, trees
    for (float v : {3.5f, frontV - 1.5f}) {
        vec2 lp = F.P(doorU + doorHW + 0.6f, v);
        if (g.owns(lp)) lanternPost(g, lp, zAt(lp), 3.4f, vec3(1.f, 0.84f, 0.6f));
    }
    for (int s = -1; s <= 1; s += 2) {
        vec2 bp = F.P(y.pathU + s * 1.6f, y.v1 - 1.5f);
        if (g.owns(bp)) prop(g, V3(bp, zAt(bp)), yawFacing(F.along * (float)(-s)), 1.f, PROP_BENCH);
    }
    Rng tr(e.seed ^ 0x7EE5u);
    vec2 oak = F.P(Lerp(y.u0, y.u1, 0.72f), y.v1 - 5.f);
    if (g.owns(oak)) prop(g, V3(oak, zAt(oak)), tr.f() * kTwoPi, 1.5f, PROP_TREE_OAK, 1);
    const float chw = y.u0 - 4.5f - doorU;   // the church's half width (the graveyard starts 4.5 m past its side)
    for (int k = 0; k < 3; k++) {
        vec2 cp = F.P(k == 0 ? y.u0 + 1.f : (k == 1 ? y.u1 - 1.f : doorU - chw - 1.4f), k < 2 ? y.v0 + 0.6f : frontV + 1.f);
        if (g.owns(cp)) italianCypress(g, cp, zAt(cp), 7.5f + k, 0.75f);
    }
    // ---- the graves
    std::vector<Grave> graves;
    gravesOf(e, graves);
    for (const Grave& gv : graves) {
        if (length(F.P(gv.u, gv.v) - oak) < 1.6f) continue;
        grave(g, F, gv, style);
    }
}

// ------------------------------------------------------------------------------------------------ placement
// Place one churchyard near `want` in region `reg`; the houses on the plot make way
void placeOne(SiteSet& S, const WorldMap& map, const RoadNetwork& roads, std::vector<Building>& buildings, vec2 want, Region reg, int style, const char* name,
              u32 seed) {
    const float W = 62.f, D = 46.f;
    Frontage fr;
    if (!findFrontage(map, roads, want, 420.f, W, D, reg, (1u << RC_STREET) | (1u << RC_LANE), true, &fr)) {
        LOG("Places: no street frontage for %s near (%.0f, %.0f)", name, want.x, want.y);
        return;
    }
    const size_t removed = clearPlot(buildings, fr.c, fr.along, W * 0.5f, D * 0.5f);
    S.vegBlocks.push_back({fr.c, fr.along, W * 0.5f, D * 0.5f});
    S.lotBlocks.push_back({fr.c, fr.along, W * 0.5f, D * 0.5f});
    claimedPlots.push_back({fr.c, fr.along, W * 0.5f, D * 0.5f});
    // the church at the left end of the frontage, the graveyard beside it
    const int variant = style == 0 ? 1 : 2;
    const float chw = style == 0 ? 5.f : 6.f, chl = style == 0 ? 9.5f : 12.f;
    const float churchU = -W * 0.5f + 3.5f + chw + 1.f, churchV = 7.f + chl;
    const vec2 c0 = fr.c - fr.out * (D * 0.5f);
    auto P = [&](float u, float v) { return c0 + fr.along * u + fr.out * v; };
    float cz = 1e9f;
    for (int k = 0; k < 9; k++) {
        vec2 q = P(churchU + ((k % 3) - 1) * chw, churchV + ((k / 3) - 1) * chl);
        cz = Min(cz, map.heightAt(q.x, q.y));
    }
    const float zChurch = cz + 0.15f;
    {
        SiteElem ch;
        ch.kind = SK_CHAPEL;
        ch.variant = (u16)variant;
        ch.seed = seed ^ 0xC4u;
        ch.c = P(churchU, churchV);
        ch.ax = -fr.out;
        ch.hx = chw;
        ch.hy = chl;
        ch.z = zChurch;
        ch.h = 26.f;
        S.elems.push_back(ch);
    }
    SiteElem y;
    y.kind = SK_CHURCHYARD;
    y.variant = (u16)style;
    y.seed = seed;
    y.c = fr.c;
    y.ax = fr.along;
    y.hx = W * 0.5f;
    y.hy = D * 0.5f;
    y.z = map.heightAt(fr.c.x, fr.c.y);
    y.h = 12.f;
    y.p[0] = (float)style;
    y.p[1] = churchU + chw + 4.5f;
    y.p[2] = W * 0.5f - 1.5f;
    y.p[3] = churchU;
    y.p[4] = churchV - chl;
    y.p[5] = -(fr.lotLine - fr.sidewalkC);
    y.p[6] = fr.out.x;
    y.p[7] = fr.out.y;
    y.text = name;
    for (char& ch : y.text) ch = (char)toupper((unsigned char)ch);   // the sign board spells it in capitals
    S.elems.push_back(y);
    // registry, walks, people
    const int placeIdx = (int)S.places.size();
    const Yard yd = yardOf(y);
    {
        NamedPlace np;
        np.name = name;
        np.kind = PK_CHURCHYARD;
        np.pos = fr.c;
        np.door = P(churchU, y.p[5]);
        np.radius = 40.f;
        S.places.push_back(np);
    }
    auto zAt = [&](vec2 q) { return map.heightAt(q.x, q.y) + 0.1f; };
    auto walk = [&](vec2 a, vec2 b, float hw) { S.walks.push_back({vec3(a, zAt(a)), vec3(b, zAt(b)), hw, SW_PATH}); };
    const float doorV = churchV - chl - 1.2f;
    walk(P(churchU, y.p[5]), P(churchU, doorV - 1.f), 1.1f);
    walk(P(churchU, doorV - 1.f), P(churchU, doorV), 1.1f);
    walk(P(yd.pathU, y.p[5]), P(yd.pathU, churchV - chl - 2.f), 0.9f);
    walk(P(yd.pathU, churchV - chl - 2.f), P(yd.pathU, yd.v1), 0.9f);
    walk(P(churchU, churchV - chl - 2.f), P(yd.pathU, churchV - chl - 2.f), 0.8f);
    // the church path and the graveyard gate stay clear of lamp posts and bus stops
    for (float u : {churchU, yd.pathU}) keepClear(S, roads, P(u, y.p[5]), fr.along, 2.5f, fr.lotLine - fr.sidewalkC + 0.5f);
    u16 group = 1;
    S.anchors.push_back({vec3(P(churchU, y.p[5]), zAt(P(churchU, y.p[5]))), fr.out, PA_WAYPOINT, (u8)placeIdx, 0});
    S.anchors.push_back({vec3(P(churchU, doorV), zAt(P(churchU, doorV))), fr.out, PA_WAYPOINT, (u8)placeIdx, 0});
    S.anchors.push_back({vec3(P(churchU + 1.3f, doorV - 0.6f), zAt(P(churchU + 1.3f, doorV - 0.6f)) - 0.1f), -fr.out, PA_STAND, (u8)placeIdx, group++});
    for (int s = -1; s <= 1; s += 2) {
        vec2 bp = P(yd.pathU + s * 1.6f, yd.v1 - 1.5f);
        S.anchors.push_back({vec3(bp, map.heightAt(bp.x, bp.y)), fr.along * (float)(-s), PA_SIT, (u8)placeIdx, group++});
    }
    std::vector<Grave> graves;
    gravesOf(y, graves);
    int mourners = 0;
    for (const Grave& gv : graves) {
        if (((gv.h >> 13) % 9u) != 4u) continue;
        vec2 q = P(gv.u, gv.v) - fr.out * 0.9f;   // in front of the stone, facing it
        S.anchors.push_back({vec3(q, map.heightAt(q.x, q.y)), fr.out, PA_MOURN, (u8)placeIdx, group++});
        mourners++;
    }
    LOG("Places: %s at (%.0f, %.0f) on edge %d, %zu buildings made way, %zu graves, %d mourners", name, fr.c.x, fr.c.y, fr.edge, removed, graves.size(), mourners);
}

// Town centres: the middle of the shops of the region (the main streets), else of all its buildings
vec2 townCentre(const std::vector<Building>& buildings, Region reg) {
    vec2 s(0.f), sAll(0.f);
    int n = 0, nAll = 0;
    for (const Building& b : buildings) {
        if (b.region != reg) continue;
        sAll += b.c;
        nAll++;
        if (b.style == BS_SHOPS) {
            s += b.c;
            n++;
        }
    }
    if (n > 0) return s / (float)n;
    return nAll > 0 ? sAll / (float)nAll : vec2(0.f);
}

void place(SiteSet& S, const WorldMap& map, const RoadNetwork& roads, std::vector<Building>& buildings) {
    vec2 oka = townCentre(buildings, REG_LAKE_TOWN), fort = townCentre(buildings, REG_FORT_CASTELL);
    if (length(oka) > 1.f) placeOne(S, map, roads, buildings, oka, REG_LAKE_TOWN, 0, "Okahatchee Union Church", 0xC401u);
    if (length(fort) > 1.f) placeOne(S, map, roads, buildings, fort, REG_FORT_CASTELL, 1, "Old Fort Church", 0xC402u);
}

}  // namespace churchyard

}  // namespace World
