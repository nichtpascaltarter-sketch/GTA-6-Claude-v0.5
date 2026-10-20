// Waterfront leisure: Sol Beach Pier with the Ferris wheel and rides, marinas and moored boats (original designs),
// docks, beach clubs and the Key Coral golf links.
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace leisure_mesh {

using namespace sitegeo;

// ------------------------------------------------------------------------------------------------ boats
// type: 0 center console, 1 motor yacht, 2 sailboat, 3 catamaran, 4 airboat, 5 superyacht, 6 fishing skiff
void boat(G& g, vec3 pos, vec2 dir, int type, float len, u32 seed) {
    bool detail = g.detail;
    vec2 n = perp(dir);
    Rng r(seed);
    MeshData& m = *g.m;
    vec3 hullCol = type == 5 ? vec3(0.08f, 0.1f, 0.18f) : (r.chance(0.2f) ? vec3(0.1f, 0.2f, 0.4f) : vec3(0.95f, 0.95f, 0.94f));
    if (type == 6) hullCol = hsvToRgb(r.f(), 0.35f, 0.6f);
    if (type == 4) hullCol = vec3(0.35f, 0.4f, 0.3f);
    float beamW = type == 3 ? len * 0.45f : (type == 5 ? len * 0.18f : (type == 4 ? len * 0.42f : len * 0.3f));
    float fb = type == 5 ? 3.2f : (type == 1 ? 1.6f : 0.9f);
    float dr = type == 5 ? 2.5f : 0.6f;
    auto hull = [&](vec3 c, float L, float B) {
        const int st = detail ? 7 : 4;
        u32 b0 = (u32)m.verts.size();
        for (int i = 0; i <= st; i++) {
            float t = (float)i / st;
            float x = Lerp(-L * 0.5f, L * 0.5f, t);
            float w = B * 0.5f * (t > 0.6f ? sqrtf(Max(0.f, 1.f - powf((t - 0.6f) / 0.4f, 2.f))) : (1.f - 0.12f * (1.f - t / 0.6f)));
            float sheer = fb + (t > 0.7f ? (t - 0.7f) * 1.5f : 0.f);
            vec3 pts[5] = {vec3(0, 0, -dr), vec3(w * 0.55f, 0, -dr * 0.4f), vec3(w, 0, 0.1f), vec3(w, 0, sheer), vec3(w * 0.96f, 0, sheer + 0.05f)};
            for (int s = -1; s <= 1; s += 2)
                for (int k = 0; k < 5; k++) {
                    vec3 lp = pts[k];
                    vec3 wp = c + vec3(dir * x + n * (s * lp.x), lp.z);
                    vec3 col = k < 2 ? vec3(0.55f, 0.12f, 0.1f) * (type == 5 ? 0.3f : 1.f) + vec3(type == 5 ? 0.02f : 0.f) : hullCol;
                    m.addVertex(wp - g.org, normalize(vec3(n * (float)s, k == 0 ? -1.f : 0.f)), vec3(dir, 0), vec2(x, lp.z), rgbv(col), M(MAT_METAL_PAINTED));
                }
        }
        for (int i = 0; i < st; i++)
            for (int s = 0; s < 2; s++)
                for (int k = 0; k < 4; k++) {
                    u32 a = b0 + (u32)(i * 10 + s * 5 + k), b = a + 10, c2 = b + 1, d = a + 1;
                    vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[d].pos - m.verts[a].pos);
                    vec3 want(n * (s ? 1.f : -1.f), 0);
                    if (dot(fn, want) >= 0) m.quadIdx(a, b, c2, d);
                    else m.quadIdx(a, d, c2, b);
                }
        // deck
        std::vector<vec2> deck;
        for (int i = 0; i <= st; i++) {
            float t = (float)i / st;
            float w = B * 0.5f * (t > 0.6f ? sqrtf(Max(0.f, 1.f - powf((t - 0.6f) / 0.4f, 2.f))) : (1.f - 0.12f * (1.f - t / 0.6f))) * 0.96f;
            deck.push_back(c.xy() + dir * Lerp(-L * 0.5f, L * 0.5f, t) - n * w);
        }
        for (int i = st; i >= 0; i--) {
            float t = (float)i / st;
            float w = B * 0.5f * (t > 0.6f ? sqrtf(Max(0.f, 1.f - powf((t - 0.6f) / 0.4f, 2.f))) : (1.f - 0.12f * (1.f - t / 0.6f))) * 0.96f;
            deck.push_back(c.xy() + dir * Lerp(-L * 0.5f, L * 0.5f, t) + n * w);
        }
        polyFlat(g, m, deck, c.z + fb, type == 6 || type == 0 ? rgb(0.85f, 0.85f, 0.82f) : rgb(0.7f, 0.55f, 0.38f), M(type == 0 || type == 6 ? MAT_PLASTER : MAT_WOOD));
        // transom
        vec3 tl = c + vec3(dir * (-L * 0.5f) - n * (B * 0.44f), fb), tr = c + vec3(dir * (-L * 0.5f) + n * (B * 0.44f), fb);
        quad(g, m, tl - vec3(0, 0, fb + dr * 0.4f), tr - vec3(0, 0, fb + dr * 0.4f), tr, tl, rgbv(hullCol), M(MAT_METAL_PAINTED), vec3(-dir, 0));
    };
    if (type == 3) {
        for (int s = -1; s <= 1; s += 2) hull(pos + vec3(n * (s * beamW * 0.38f), 0), len, beamW * 0.24f);
        boxY(g, pos + vec3(-dir * (len * 0.05f), fb + 0.2f), dir, vec3(len * 0.4f, beamW * 0.48f, 0.2f), rgb(0.95f), M(MAT_PLASTER), true);
    } else {
        hull(pos, len, beamW);
    }
    vec3 deckC = pos + vec3(0, 0, fb);
    switch (type) {
        case 0:
        case 6: {
            boxY(g, deckC + vec3(dir * (len * 0.05f), 0.55f), dir, vec3(0.45f, 0.5f, 0.55f), rgb(0.9f), M(MAT_PLASTER));
            if (type == 0) {
                beam(g, deckC + vec3(dir * (len * 0.05f) - n * 0.7f, 1.1f), deckC + vec3(dir * (len * 0.05f) - n * 0.7f, 2.1f), 0.05f, 0.05f, rgb(0.8f), M(MAT_METAL_BRUSHED));
                beam(g, deckC + vec3(dir * (len * 0.05f) + n * 0.7f, 1.1f), deckC + vec3(dir * (len * 0.05f) + n * 0.7f, 2.1f), 0.05f, 0.05f, rgb(0.8f), M(MAT_METAL_BRUSHED));
                boxY(g, deckC + vec3(dir * (len * 0.05f), 2.15f), dir, vec3(1.f, 0.9f, 0.05f), rgb(0.2f, 0.3f, 0.5f), M(MAT_FABRIC), true);
            }
            boxY(g, deckC + vec3(-dir * (len * 0.5f + 0.2f), 0.1f), dir, vec3(0.25f, 0.3f, 0.6f), rgb(0.1f), M(MAT_METAL_PAINTED));
            break;
        }
        case 1: {
            boxY(g, deckC + vec3(-dir * (len * 0.05f), 1.f), dir, vec3(len * 0.28f, beamW * 0.4f, 1.f), rgb(0.97f), M(MAT_PLASTER));
            boxY(g, deckC + vec3(-dir * (len * 0.1f), 2.3f), dir, vec3(len * 0.18f, beamW * 0.36f, 0.3f), rgb(0.97f), M(MAT_PLASTER));
            for (int s = -1; s <= 1; s += 2)
                quad(g, m, deckC + vec3(-dir * (len * 0.3f) + n * (s * beamW * 0.405f), 0.8f), deckC + vec3(dir * (len * 0.2f) + n * (s * beamW * 0.405f), 0.8f),
                     deckC + vec3(dir * (len * 0.15f) + n * (s * beamW * 0.405f), 1.6f), deckC + vec3(-dir * (len * 0.3f) + n * (s * beamW * 0.405f), 1.6f),
                     rgb(0.08f, 0.1f, 0.12f), M(MAT_GLASS), vec3(n * (float)s, 0));
            quad(g, m, deckC + vec3(dir * (len * 0.23f) - n * (beamW * 0.36f), 0.9f), deckC + vec3(dir * (len * 0.23f) + n * (beamW * 0.36f), 0.9f),
                 deckC + vec3(dir * (len * 0.15f) + n * (beamW * 0.36f), 1.9f), deckC + vec3(dir * (len * 0.15f) - n * (beamW * 0.36f), 1.9f), rgb(0.08f, 0.1f, 0.12f),
                 M(MAT_GLASS), normalize(vec3(dir, 0.6f)));
            if (detail) {
                beam(g, deckC + vec3(-dir * (len * 0.12f) - n * (beamW * 0.3f), 2.6f), deckC + vec3(-dir * (len * 0.16f), 3.6f), 0.12f, 0.12f, rgb(0.95f), M(MAT_METAL_PAINTED));
                beam(g, deckC + vec3(-dir * (len * 0.12f) + n * (beamW * 0.3f), 2.6f), deckC + vec3(-dir * (len * 0.16f), 3.6f), 0.12f, 0.12f, rgb(0.95f), M(MAT_METAL_PAINTED));
                lamp(g, deckC + vec3(-dir * (len * 0.16f), 3.75f), 0.18f, vec3(1.f, 0.95f, 0.85f), 0.8f, EA_NIGHT);
                quad(g, m, deckC + vec3(-dir * (len * 0.25f) + n * (beamW * 0.41f), 1.7f), deckC + vec3(dir * (len * 0.1f) + n * (beamW * 0.41f), 1.7f),
                     deckC + vec3(dir * (len * 0.1f) + n * (beamW * 0.41f), 1.85f), deckC + vec3(-dir * (len * 0.25f) + n * (beamW * 0.41f), 1.85f),
                     rgb(1.f, 0.8f, 0.55f, 0.1f), emMat(EA_NIGHT), vec3(n, 0));
            }
            break;
        }
        case 2: {
            boxY(g, deckC + vec3(-dir * (len * 0.05f), 0.4f), dir, vec3(len * 0.2f, beamW * 0.3f, 0.4f), rgb(0.95f), M(MAT_PLASTER));
            float mastH = len * 1.35f;
            vec3 mb = deckC + vec3(dir * (len * 0.12f), 0.f);
            cyl(g, mb, 0.1f, 0.07f, mastH, 6, rgb(0.85f), M(MAT_METAL_BRUSHED), false);
            beam(g, mb + vec3(0, 0, 1.6f), mb + vec3(-dir * (len * 0.4f), 1.7f), 0.12f, 0.12f, rgb(0.85f), M(MAT_METAL_BRUSHED));
            // furled mainsail cover on the boom
            beam(g, mb + vec3(-dir * 0.3f, 1.85f), mb + vec3(-dir * (len * 0.38f), 1.9f), 0.35f, 0.35f, r.chance(0.5f) ? rgb(0.1f, 0.2f, 0.45f) : rgb(0.9f), M(MAT_FABRIC));
            if (detail) {
                rod(g, mb + vec3(0, 0, mastH), deckC + vec3(dir * (len * 0.5f), 0.1f), 0.015f, 3, rgb(0.6f), M(MAT_METAL_BRUSHED));
                rod(g, mb + vec3(0, 0, mastH), deckC + vec3(-dir * (len * 0.5f), 0.1f), 0.015f, 3, rgb(0.6f), M(MAT_METAL_BRUSHED));
            }
            lamp(g, mb + vec3(0, 0, mastH + 0.15f), 0.16f, vec3(1.f, 0.95f, 0.85f), 0.9f, EA_NIGHT);
            break;
        }
        case 3: {
            boxY(g, deckC + vec3(-dir * (len * 0.05f), 1.3f), dir, vec3(len * 0.22f, beamW * 0.35f, 0.9f), rgb(0.97f), M(MAT_PLASTER));
            cyl(g, deckC + vec3(dir * (len * 0.05f), 0.4f), 0.12f, 0.08f, len * 1.2f, 6, rgb(0.85f), M(MAT_METAL_BRUSHED), false);
            break;
        }
        case 4: {
            boxY(g, deckC + vec3(-dir * (len * 0.1f), 1.2f), dir, vec3(0.4f, 0.5f, 0.4f), rgb(0.3f), M(MAT_METAL_PAINTED));
            vec3 cage = deckC + vec3(-dir * (len * 0.42f), 1.7f);
            for (int k = 0; k < 10; k++) {
                float a0 = kTwoPi * k / 10, a1 = kTwoPi * (k + 1) / 10;
                rod(g, cage + vec3(n * (cosf(a0) * 1.1f), sinf(a0) * 1.1f), cage + vec3(n * (cosf(a1) * 1.1f), sinf(a1) * 1.1f), 0.03f, 3, rgb(0.8f), M(MAT_METAL_BRUSHED));
            }
            beam(g, cage - vec3(n * 1.f, 0), cage + vec3(n * 1.f, 0), 0.15f, 0.06f, rgb(0.15f), M(MAT_WOOD), vec3(dir, 0));
            break;
        }
        case 5: {
            for (int k = 0; k < 3; k++) {
                float L2 = len * (0.62f - k * 0.14f), W2 = beamW * (0.44f - k * 0.05f);
                vec3 dc = deckC + vec3(-dir * (len * (0.05f + k * 0.03f)), 1.2f + k * 2.4f);
                boxY(g, dc, dir, vec3(L2 * 0.5f, W2, 1.2f), rgb(0.97f), M(MAT_PLASTER), true);
                for (int s = -1; s <= 1; s += 2)
                    quad(g, m, dc + vec3(-dir * (L2 * 0.45f) + n * (s * (W2 + 0.02f)), -0.4f), dc + vec3(dir * (L2 * 0.45f) + n * (s * (W2 + 0.02f)), -0.4f),
                         dc + vec3(dir * (L2 * 0.4f) + n * (s * (W2 + 0.02f)), 0.6f), dc + vec3(-dir * (L2 * 0.45f) + n * (s * (W2 + 0.02f)), 0.6f),
                         rgb(0.05f, 0.07f, 0.1f), M(MAT_GLASS), vec3(n * (float)s, 0));
                if (detail)
                    for (int s = -1; s <= 1; s += 2)
                        quad(g, m, dc + vec3(-dir * (L2 * 0.4f) + n * (s * (W2 + 0.04f)), -0.3f), dc + vec3(dir * (L2 * 0.3f) + n * (s * (W2 + 0.04f)), -0.3f),
                             dc + vec3(dir * (L2 * 0.3f) + n * (s * (W2 + 0.04f)), -0.15f), dc + vec3(-dir * (L2 * 0.4f) + n * (s * (W2 + 0.04f)), -0.15f),
                             rgb(0.6f, 0.8f, 1.f, 0.15f), emMat(EA_NIGHT), vec3(n * (float)s, 0));
            }
            vec3 radar = deckC + vec3(-dir * (len * 0.12f), 8.f);
            cyl(g, radar, 0.3f, 0.2f, 2.f, 6, rgb(0.95f), M(MAT_PLASTER), false);
            lamp(g, radar + vec3(0, 0, 2.2f), 0.2f, vec3(1.f), 0.9f, EA_NIGHT);
            break;
        }
        default: break;
    }
    if (detail) collide(g, pos + vec3(0, 0, fb * 0.5f), dir, vec3(len * 0.5f, beamW * 0.5f, fb * 0.5f + dr * 0.5f));
}

// Wooden deck strip with piles (docks, piers, boardwalks). a->b centerline, w half width, deck top z
void deckStrip(G& g, vec2 a, vec2 b, float w, float z, float pileBottom, float pileStep, bool rail, u32 col) {
    vec2 d = b - a;
    float L = length(d);
    if (L < 0.05f) return;
    d = d / L;
    vec2 n = perp(d);
    quad(g, *g.m, vec3(a - n * w, z), vec3(b - n * w, z), vec3(b + n * w, z), vec3(a + n * w, z), col, M(MAT_WOOD), vec3(0, 0, 1));
    for (int s = -1; s <= 1; s += 2)
        quad(g, *g.m, vec3(a + n * (s * w), z), vec3(b + n * (s * w), z), vec3(b + n * (s * w), z - 0.35f), vec3(a + n * (s * w), z - 0.35f), col, M(MAT_WOOD),
             vec3(n * (float)s, 0));
    if (!g.detail) return;
    for (float t = 0.f; t <= L + 0.01f; t += pileStep) {
        vec2 p = a + d * Min(t, L);
        for (int s = -1; s <= 1; s += 2) {
            vec2 q = p + n * (s * (w - 0.15f));
            cyl(g, vec3(q, pileBottom), 0.14f, 0.14f, z - pileBottom + (rail ? 1.05f : 0.f), 6, rgb(0.45f, 0.38f, 0.3f), M(MAT_WOOD), rail);
        }
    }
    if (rail)
        for (int s = -1; s <= 1; s += 2) {
            beam(g, vec3(a + n * (s * (w - 0.15f)), z + 1.f), vec3(b + n * (s * (w - 0.15f)), z + 1.f), 0.1f, 0.1f, rgb(0.9f, 0.88f, 0.85f), M(MAT_WOOD));
            beam(g, vec3(a + n * (s * (w - 0.15f)), z + 0.5f), vec3(b + n * (s * (w - 0.15f)), z + 0.5f), 0.06f, 0.06f, rgb(0.9f, 0.88f, 0.85f), M(MAT_WOOD));
        }
}

// ------------------------------------------------------------------------------------------------ Sol Beach Pier
void genBeachPier(const SiteElem& e, G& g) {
    bool detail = g.detail;
    vec2 d = normalize(e.b - e.a), n = perp(d);
    float z = e.z;
    u32 wood = rgb(0.72f, 0.6f, 0.46f), white = rgb(0.95f, 0.94f, 0.9f);
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    if (e.variant == 1) {
        // access ramp from the promenade up to the deck
        float z0 = e.p[1], z1 = e.p[2], w = e.p[0];
        vec2 a = e.a, b = e.b;
        if (!g.owns((a + b) * 0.5f)) return;
        quad(g, *g.m, vec3(a - n * w, z0), vec3(b - n * w, z1), vec3(b + n * w, z1), vec3(a + n * w, z0), wood, M(MAT_WOOD), vec3(0, 0, 1));
        for (int s = -1; s <= 1; s += 2) {
            quad(g, *g.m, vec3(a + n * (s * w), z0), vec3(b + n * (s * w), z1), vec3(b + n * (s * w), z1 - 0.8f), vec3(a + n * (s * w), z0 - 0.8f), white, M(MAT_WOOD),
                 vec3(n * (float)s, 0));
            beam(g, vec3(a + n * (s * (w - 0.1f)), z0 + 1.f), vec3(b + n * (s * (w - 0.1f)), z1 + 1.f), 0.12f, 0.12f, white, M(MAT_WOOD));
        }
        for (float t = 0.f; t <= 1.f; t += 0.25f) {
            vec2 p = lerp(a, b, t);
            for (int s = -1; s <= 1; s += 2) cyl(g, vec3(p + n * (s * (w - 0.1f)), gMap->heightAt(p.x, p.y) - 0.5f), 0.15f, 0.15f, Lerp(z0, z1, t) - gMap->heightAt(p.x, p.y) + 1.5f,
                                                   6, white, M(MAT_WOOD), true);
        }
        return;
    }
    float w = e.p[0], ps = e.p[1], pe = e.p[2], pw = e.p[3];
    float x0 = dot(e.a, d), x1 = dot(e.b, d);
    // walkway pieces (clipped to the cell by their centers, 20 m long)
    for (float s = 0.f; s < x1 - x0; s += 20.f) {
        float s1 = Min(x1 - x0, s + 20.f);
        vec2 a = e.a + d * s, b = e.a + d * s1;
        vec2 mid = (a + b) * 0.5f;
        if (!g.owns(mid)) continue;
        float along = dot(mid, d);
        bool onPlatform = along > ps && along < pe;
        if (!onPlatform) deckStrip(g, a, b, w, z, -6.f, 5.f, true, wood);
        // lamp posts along the walkway
        if (detail && !onPlatform)
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 lp = mid + n * (sd * (w - 0.5f));
                cyl(g, vec3(lp, z), 0.1f, 0.08f, 4.2f, 6, rgb(0.1f, 0.25f, 0.3f), M(MAT_METAL_PAINTED), false);
                lathe(g, vec3(lp, z + 4.2f), {vec2(0.05f, 0.f), vec2(0.3f, 0.25f), vec2(0.28f, 0.6f), vec2(0.f, 0.75f)}, 8, rgb(1.f, 0.9f, 0.7f, 0.6f), emMat(EA_NIGHT), false);
                light(g, vec3(lp, z + 4.5f), vec3(1.f, 0.85f, 0.6f) * 2600.f, 15.f, 0);
            }
        // piles under the walkway
    }
    // amusement platform: deck clipped to this cell with piles on a grid
    {
        vec2 pc = n * dot(e.a, n) + d * ((ps + pe) * 0.5f);
        std::vector<vec2> rect = rectPoly(pc, d, (pe - ps) * 0.5f, pw);
        std::vector<vec2> poly = clipConvex(rect, mn, mx);
        if (poly.size() >= 3) {
            polyFlat(g, *g.m, poly, z, wood, M(MAT_WOOD));
            if (detail) {
                for (float s = ps + 4.f; s < pe; s += 8.f)
                    for (float t = -pw + 3.f; t < pw; t += 8.f) {
                        vec2 p = pc + d * (s - (ps + pe) * 0.5f) + n * t;
                        if (g.owns(p)) cyl(g, vec3(p, -6.f), 0.35f, 0.35f, z + 5.7f, 6, rgb(0.5f, 0.45f, 0.38f), M(MAT_CONCRETE), false);
                    }
            }
            for (int k = 0; k < 4; k++) {
                vec2 a = rect[k], b = rect[(k + 1) % 4];
                if (!clipSegment(a, b, mn, mx)) continue;
                vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
                quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(b, z - 0.6f), vec3(a, z - 0.6f), white, M(MAT_WOOD), vec3(on, 0));
                if (detail) {
                    beam(g, vec3(a - on * 0.2f, z + 1.05f), vec3(b - on * 0.2f, z + 1.05f), 0.12f, 0.1f, white, M(MAT_WOOD));
                    float L = length(b - a);
                    for (float t = 0.f; t <= L; t += 3.f) {
                        vec2 p = a + normalize(b - a) * t - on * 0.2f;
                        boxY(g, vec3(p, z + 0.52f), normalize(b - a), vec3(0.06f, 0.06f, 0.52f), white, M(MAT_WOOD));
                    }
                }
            }
        }
    }
    // pile rows under the narrow walkway (per cell)
    if (detail)
        for (float s = 0.f; s < x1 - x0; s += 8.f) {
            vec2 p = e.a + d * s;
            float along = dot(p, d);
            if (along > ps && along < pe) continue;
            if (!g.owns(p)) continue;
            for (int sd = -1; sd <= 1; sd += 2) cyl(g, vec3(p + n * (sd * (w - 1.5f)), -6.f), 0.3f, 0.3f, z + 5.7f, 6, rgb(0.5f, 0.45f, 0.38f), M(MAT_CONCRETE), false);
        }
    // entrance arch with neon lettering
    vec2 ea = e.a + d * 3.f;
    if (g.owns(ea)) {
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 pp = ea + n * (sd * (w + 0.6f));
            boxY(g, vec3(pp, z + 4.f), d, vec3(0.6f, 0.6f, 4.f), rgb(0.95f, 0.9f, 0.8f), M(MAT_STUCCO));
            collide(g, vec3(pp, z + 4.f), d, vec3(0.6f, 0.6f, 4.f));
        }
        // arched sign band
        int segs = 12;
        for (int k = 0; k < segs; k++) {
            float t0 = (float)k / segs, t1 = (float)(k + 1) / segs;
            vec2 p0 = ea + n * Lerp(-(w + 0.6f), w + 0.6f, t0), p1 = ea + n * Lerp(-(w + 0.6f), w + 0.6f, t1);
            float h0 = z + 8.f + sinf(t0 * kPi) * 3.f, h1 = z + 8.f + sinf(t1 * kPi) * 3.f;
            beam(g, vec3(p0, h0), vec3(p1, h1), 0.5f, 1.6f, rgb(0.1f, 0.35f, 0.5f), M(MAT_METAL_PAINTED), vec3(0, 0, 1));
            beam(g, vec3(p0, h0 + 0.9f) - vec3(d, 0) * 0.3f, vec3(p1, h1 + 0.9f) - vec3(d, 0) * 0.3f, 0.12f, 0.12f, rgb(1.f, 0.3f, 0.7f, 0.9f), emMat(EA_CHASE, (u32)k * 20u),
                 vec3(0, 0, 1));
        }
        const char* t = e.text.c_str();
        float th = 1.25f;
        float tw = textAdvance(t, th, 0.25f);
        vec2 viewR = perp(-d);
        vec3 o(ea - d * 0.3f - viewR * (tw * 0.5f), z + 8.5f);
        strokeText(g, *g.m, t, o, vec3(viewR, 0), vec3(0, 0, 1), th, 0.2f, rgb(0.3f, 0.95f, 1.f, 0.9f), emMat(EA_PULSE, 30), detail ? 0.1f : 0.f, 0.25f);
        light(g, vec3(ea - d * 3.f, z + 9.f), vec3(0.4f, 0.9f, 1.f) * 5000.f, 20.f, 2);
    }
    // restaurant at the far end
    vec2 rc = e.b - d * 12.f;
    if (g.owns(rc)) {
        std::vector<vec2> fp = rectPoly(rc, d, 10.f, w + 3.f);
        polyFlat(g, *g.m, rectPoly(rc, d, 14.f, w + 5.f), z, wood, M(MAT_WOOD));
        facadeRing(g, fp, z, z + 5.f, z, (u32)e.p[7], 3.4f);
        std::vector<vec2> rf = rectPoly(rc, d, 11.f, w + 4.f);
        polyFlat(g, *g.m, rf, z + 5.f, rgb(0.9f, 0.55f, 0.35f), M(MAT_ROOF_TILE));
        const char* t = "PIER 9 GRILL";
        float th = 1.1f;
        float tw = textAdvance(t, th, 0.3f);
        vec2 viewR = perp(-d);
        strokeText(g, *g.m, t, vec3(rc - d * 10.1f - viewR * (tw * 0.5f), z + 5.3f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.18f, rgb(1.f, 0.4f, 0.2f, 0.8f), emMat(EA_NIGHT),
                   0.1f, 0.3f);
        collide(g, vec3(rc, z + 2.5f), d, vec3(10.f, w + 3.f, 2.5f));
        light(g, vec3(rc - d * 12.f, z + 4.f), vec3(1.f, 0.75f, 0.5f) * 5000.f, 20.f, 1);
        deckStrip(g, e.b - d * 26.f, e.b, w + 5.f, z - 0.001f, -6.f, 6.f, true, wood);
    }
}

// ------------------------------------------------------------------------------------------------ rides
void genFerrisWheel(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 X2 = e.ax;
    vec3 X(X2, 0), Y(perp(X2), 0), Z(0, 0, 1);
    float R = e.p[0];
    vec3 hub(e.c, e.z + R + 4.5f);
    int spokes = detail ? 24 : 12;
    u32 steel = M(MAT_METAL_PAINTED);
    // two rim rings with cross ties
    for (int side = -1; side <= 1; side += 2) {
        vec3 off = Y * (side * 1.4f);
        for (int k = 0; k < spokes * 2; k++) {
            float a0 = kTwoPi * k / (spokes * 2), a1 = kTwoPi * (k + 1) / (spokes * 2);
            vec3 p0 = hub + off + X * (cosf(a0) * R) + Z * (sinf(a0) * R), p1 = hub + off + X * (cosf(a1) * R) + Z * (sinf(a1) * R);
            beam(g, p0, p1, 0.35f, 0.35f, rgb(0.95f, 0.95f, 0.95f), steel, Y);
            // neon rim light (hue cycling)
            beam(g, p0 + off * 0.12f, p1 + off * 0.12f, 0.12f, 0.12f, rgb(1.f, 0.4f, 0.8f, 0.9f), emMat(EA_HUE, (u32)(k * 256 / (spokes * 2))), Y);
        }
        for (int k = 0; k < spokes; k++) {
            float a = kTwoPi * k / spokes;
            vec3 rim = hub + off + X * (cosf(a) * R) + Z * (sinf(a) * R);
            rod(g, hub + off * 0.6f, rim, 0.09f, 4, rgb(0.9f), steel);
            // chase lights running outward along the spokes
            beam(g, hub + off * 0.7f + (rim - hub - off) * 0.1f, rim - (rim - hub - off) * 0.05f, 0.08f, 0.08f, rgb(0.4f, 0.9f, 1.f, 0.9f), emMat(EA_CHASE, (u32)(k * 256 / spokes)), Y);
        }
    }
    if (detail)
        for (int k = 0; k < spokes; k++) {
            float a = kTwoPi * k / spokes;
            vec3 rim = hub + X * (cosf(a) * R) + Z * (sinf(a) * R);
            beam(g, rim - Y * 1.4f, rim + Y * 1.4f, 0.25f, 0.25f, rgb(0.9f), steel);
        }
    // hub + axle
    rod(g, hub - Y * 2.2f, hub + Y * 2.2f, 1.1f, 10, rgb(0.8f), M(MAT_METAL_BRUSHED));
    lamp(g, hub + Y * 2.3f, 0.9f, vec3(1.f, 0.9f, 0.5f), 0.8f, EA_PULSE, 0);
    // A-frame legs on both sides
    for (int side = -1; side <= 1; side += 2) {
        vec3 top = hub + Y * (side * 2.3f);
        for (int s = -1; s <= 1; s += 2) {
            vec3 foot(e.c + X2 * (s * R * 0.55f) + perp(X2) * (side * 5.5f), e.z);
            beam(g, foot, top, 1.f, 1.f, rgb(0.95f, 0.95f, 0.95f), steel, X);
            collide(g, lerp(foot, top, 0.15f), X2, vec3(0.8f, 0.8f, 3.f));
        }
    }
    // gondolas hanging below the rim
    for (int k = 0; k < spokes; k++) {
        float a = kTwoPi * k / spokes;
        vec3 pivot = hub + X * (cosf(a) * R) + Z * (sinf(a) * R);
        vec3 col = hsvToRgb((float)k / spokes, 0.65f, 0.95f);
        vec3 cab = pivot - Z * 2.3f;
        if (detail) rod(g, pivot, cab + Z * 1.1f, 0.05f, 4, rgb(0.8f), M(MAT_METAL_BRUSHED));
        boxY(g, cab, X2, vec3(1.1f, 1.3f, 1.05f), rgbv(col), steel, true);
        if (detail) {
            boxY(g, cab + Z * 1.15f, X2, vec3(1.25f, 1.45f, 0.1f), rgb(0.95f), steel, true);
            for (int s = -1; s <= 1; s += 2)
                quad(g, *g.m, cab + X * (s * 1.12f) - Y * 1.1f + Z * 0.f, cab + X * (s * 1.12f) + Y * 1.1f, cab + X * (s * 1.12f) + Y * 1.1f + Z * 0.8f,
                     cab + X * (s * 1.12f) - Y * 1.1f + Z * 0.8f, rgb(0.2f, 0.3f, 0.35f), M(MAT_GLASS), X * (float)s);
        }
    }
    // base platform + booth
    boxY(g, vec3(e.c, e.z + 0.4f), X2, vec3(R * 0.62f, 7.f, 0.4f), rgb(0.85f, 0.2f, 0.3f), M(MAT_METAL_PAINTED), false);
    for (int k = 0; k < 4; k++) light(g, hub + X * ((k - 1.5f) * 12.f) - Z * (R * 0.5f), hsvToRgb(k * 0.25f, 0.6f, 1.f) * 6000.f, 30.f, 2);
}

void coasterTrack(const SiteElem& e, float t, vec3& pos, vec3& tan) {
    // closed figure-eight-ish loop in plan with a lift hill and drops
    float a = t * kTwoPi;
    float rx = e.p[0], ry = e.p[1];
    vec2 X = e.ax, Y = perp(e.ax);
    vec2 p = e.c + X * (sinf(a) * rx) + Y * (sinf(2.f * a) * ry * 0.9f);
    float h = 3.f + 15.f * powf(Max(0.f, sinf(a * 1.f + 0.4f)), 2.f) + 4.f * Max(0.f, sinf(a * 3.f + 1.2f));
    pos = vec3(p, e.z + h);
    float a2 = (t + 0.002f) * kTwoPi;
    vec2 p2 = e.c + X * (sinf(a2) * rx) + Y * (sinf(2.f * a2) * ry * 0.9f);
    float h2 = 3.f + 15.f * powf(Max(0.f, sinf(a2 * 1.f + 0.4f)), 2.f) + 4.f * Max(0.f, sinf(a2 * 3.f + 1.2f));
    tan = normalize(vec3(p2, e.z + h2) - pos);
}

void genCoaster(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    int n = detail ? 160 : 48;
    vec3 prevP, prevT;
    coasterTrack(e, 0.f, prevP, prevT);
    for (int k = 1; k <= n; k++) {
        vec3 p, t;
        coasterTrack(e, (float)k / n, p, t);
        vec3 side = normalize(cross(t, vec3(0, 0, 1)));
        for (int s = -1; s <= 1; s += 2) rod(g, prevP + side * (s * 0.6f), p + side * (s * 0.6f), 0.12f, 5, rgb(0.9f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
        rod(g, prevP - vec3(0, 0, 0.5f), p - vec3(0, 0, 0.5f), 0.18f, 5, rgb(0.9f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
        beam(g, prevP - vec3(0, 0, 0.75f), p - vec3(0, 0, 0.75f), 0.12f, 0.12f, rgb(1.f, 0.9f, 0.3f, 0.8f), emMat(EA_CHASE, (u32)(k * 256 / n)));
        if (k % (detail ? 6 : 4) == 0) {
            cyl(g, vec3(p.x, p.y, e.z), 0.25f, 0.22f, p.z - e.z - 0.6f, 6, rgb(0.95f), M(MAT_METAL_PAINTED), false);
            if (detail) collide(g, vec3(p.x, p.y, (e.z + p.z) * 0.5f), vec2(1, 0), vec3(0.3f, 0.3f, (p.z - e.z) * 0.5f));
        }
        if (detail && (k % 3) == 0) beam(g, p - side * 0.7f - vec3(0, 0, 0.1f), p + side * 0.7f - vec3(0, 0, 0.1f), 0.12f, 0.12f, rgb(0.9f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
        prevP = p;
        prevT = t;
    }
    // a parked train of four cars at the station
    for (int c = 0; c < 4; c++) {
        vec3 p, t;
        coasterTrack(e, 0.02f + c * 0.012f, p, t);
        boxY(g, p + vec3(0, 0, 0.6f), normalize(t.xy()), vec3(1.2f, 0.8f, 0.5f), rgb(0.1f, 0.3f, 0.8f), M(MAT_METAL_PAINTED), true);
    }
    light(g, vec3(e.c, e.z + 10.f), vec3(1.f, 0.6f, 0.3f) * 6000.f, 35.f, 2);
}

void genDropTower(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    float H = e.h - 6.f;
    vec3 b(e.c, e.z);
    boxY(g, b + vec3(0, 0, H * 0.5f), vec2(1, 0), vec3(1.6f, 1.6f, H * 0.5f), rgb(0.95f, 0.95f, 0.95f), M(MAT_METAL_PAINTED));
    for (int k = 0; k < 4; k++) {
        float a = kHalfPi * k + kPi * 0.25f;
        vec2 c = e.c + vec2(cosf(a), sinf(a)) * 1.7f;
        beam(g, vec3(c, e.z + 2.f), vec3(c, e.z + H), 0.15f, 0.15f, rgb(0.2f, 0.9f, 1.f, 0.9f), emMat(EA_CHASE, (u32)k * 64u));
    }
    lathe(g, b + vec3(0, 0, H), {vec2(3.2f, 0.f), vec2(3.2f, 1.5f), vec2(1.f, 3.5f), vec2(0.f, 4.f)}, 12, rgb(0.9f, 0.2f, 0.25f), M(MAT_METAL_PAINTED), false);
    lamp(g, b + vec3(0, 0, H + 4.2f), 0.7f, vec3(1.f, 0.1f, 0.05f), 1.f, EA_BLINK, 3);
    // seat ring parked high on the tower
    float rz = e.z + H * 0.72f;
    lathe(g, vec3(e.c, rz), {vec2(2.2f, 0.f), vec2(4.2f, 0.4f), vec2(4.2f, 1.6f), vec2(2.2f, 1.9f)}, 16, rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), false);
    lathe(g, vec3(e.c, e.z), {vec2(5.f, 0.f), vec2(5.f, 0.5f)}, 16, rgb(0.3f), M(MAT_METAL_PAINTED), true);
    collide(g, b + vec3(0, 0, H * 0.5f), vec2(1, 0), vec3(1.7f, 1.7f, H * 0.5f));
    light(g, b + vec3(0, 0, 8.f), vec3(0.3f, 0.8f, 1.f) * 5000.f, 25.f, 2);
}

void genCarousel(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    float R = e.hx - 1.f;
    vec3 b(e.c, e.z);
    lathe(g, b, {vec2(R, 0.f), vec2(R, 0.8f), vec2(0.f, 0.8f)}, 20, rgb(0.85f, 0.75f, 0.55f), M(MAT_WOOD), false);
    cyl(g, b + vec3(0, 0, 0.8f), 1.2f, 1.2f, 4.2f, 12, rgb(0.95f, 0.85f, 0.4f), M(MAT_METAL_BRUSHED), false);
    // striped conical roof
    int seg = 16;
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec3 p0 = b + vec3(cosf(a0) * (R + 0.6f), sinf(a0) * (R + 0.6f), 5.f), p1 = b + vec3(cosf(a1) * (R + 0.6f), sinf(a1) * (R + 0.6f), 5.f), tip = b + vec3(0, 0, 8.5f);
        u32 c = (k & 1) ? rgb(0.95f, 0.95f, 0.92f) : rgb(0.85f, 0.15f, 0.2f);
        MeshData& m = *g.m;
        vec3 nn = normalize(cross(p1 - p0, tip - p0));
        if (nn.z < 0) nn = -nn;
        u32 i0 = m.addVertex(p0 - g.org, nn, normalize(p1 - p0), vec2(0, 0), c, M(MAT_FABRIC));
        u32 i1 = m.addVertex(p1 - g.org, nn, normalize(p1 - p0), vec2(1, 0), c, M(MAT_FABRIC));
        u32 i2 = m.addVertex(tip - g.org, nn, normalize(p1 - p0), vec2(0.5f, 1), c, M(MAT_FABRIC));
        if (dot(cross(p1 - p0, tip - p0), nn) > 0) m.tri(i0, i1, i2);
        else m.tri(i0, i2, i1);
        // valance with bulbs
        quad(g, m, p0, p1, p1 - vec3(0, 0, 0.8f), p0 - vec3(0, 0, 0.8f), c, M(MAT_FABRIC), normalize(vec3((p0 + p1).xy() * 0.5f - e.c, 0)));
        lamp(g, p0 - vec3(0, 0, 0.9f), 0.14f, vec3(1.f, 0.85f, 0.45f), 0.9f, EA_CHASE, (u32)(k * 16));
    }
    lamp(g, b + vec3(0, 0, 8.7f), 0.3f, vec3(1.f, 0.85f, 0.4f), 0.9f, EA_PULSE, 7);
    if (detail)
        for (int k = 0; k < 12; k++) {
            float a = kTwoPi * k / 12;
            vec2 p = e.c + vec2(cosf(a), sinf(a)) * (R * 0.72f);
            rod(g, vec3(p, e.z + 0.8f), vec3(p, e.z + 5.f), 0.04f, 4, rgb(0.95f, 0.85f, 0.4f), M(MAT_METAL_BRUSHED));
            boxY(g, vec3(p, e.z + 1.9f + (k & 1) * 0.4f), vec2(-sinf(a), cosf(a)), vec3(0.8f, 0.2f, 0.4f), rgbv(hsvToRgb(k / 12.f, 0.3f, 0.95f)), M(MAT_PLASTER), true);
        }
    collide(g, b + vec3(0, 0, 2.5f), vec2(1, 0), vec3(R * 0.7f, R * 0.7f, 2.5f));
    light(g, b + vec3(0, 0, 4.f), vec3(1.f, 0.8f, 0.45f) * 4000.f, 18.f, 2);
}

void genSwingRide(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec3 b(e.c, e.z);
    float H = 16.f;
    cyl(g, b, 1.f, 0.7f, H, 12, rgb(0.95f, 0.95f, 0.95f), M(MAT_METAL_PAINTED), false);
    // tilted canopy (as if spinning) with chairs flung outward
    vec3 top = b + vec3(0, 0, H);
    float tilt = 0.12f;
    int seg = 16;
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec3 p0 = top + vec3(cosf(a0) * 7.f, sinf(a0) * 7.f, cosf(a0) * 7.f * tilt), p1 = top + vec3(cosf(a1) * 7.f, sinf(a1) * 7.f, cosf(a1) * 7.f * tilt);
        vec3 tip = top + vec3(0, 0, 2.6f);
        u32 c = (k & 1) ? rgb(0.2f, 0.6f, 0.95f) : rgb(0.95f, 0.85f, 0.2f);
        MeshData& m = *g.m;
        vec3 nn = normalize(cross(p1 - p0, tip - p0));
        if (nn.z < 0) nn = -nn;
        u32 i0 = m.addVertex(p0 - g.org, nn, normalize(p1 - p0), vec2(0, 0), c, M(MAT_FABRIC));
        u32 i1 = m.addVertex(p1 - g.org, nn, normalize(p1 - p0), vec2(1, 0), c, M(MAT_FABRIC));
        u32 i2 = m.addVertex(tip - g.org, nn, normalize(p1 - p0), vec2(0.5f, 1), c, M(MAT_FABRIC));
        if (dot(cross(p1 - p0, tip - p0), nn) > 0) m.tri(i0, i1, i2);
        else m.tri(i0, i2, i1);
        lamp(g, p0 - vec3(0, 0, 0.2f), 0.15f, vec3(1.f, 0.9f, 0.5f), 0.9f, EA_CHASE, (u32)(k * 16));
        if (detail && (k & 1) == 0) {
            vec3 chair = p0 + vec3(cosf(a0) * 3.f, sinf(a0) * 3.f, -6.f);
            rod(g, p0, chair + vec3(0, 0, 0.5f), 0.03f, 3, rgb(0.8f), M(MAT_METAL_BRUSHED));
            boxY(g, chair, vec2(-sinf(a0), cosf(a0)), vec3(0.35f, 0.3f, 0.3f), rgb(0.9f, 0.2f, 0.2f), M(MAT_PLASTER), true);
        }
    }
    collide(g, b + vec3(0, 0, H * 0.5f), vec2(1, 0), vec3(1.f, 1.f, H * 0.5f));
    light(g, top, vec3(0.5f, 0.8f, 1.f) * 4000.f, 22.f, 2);
}

void genPierGames(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 X = e.ax;
    std::vector<vec2> fp = rectPoly(e.c, X, e.hx, e.hy);
    facadeRing(g, fp, e.z, e.z + 6.f, e.z, (u32)e.p[7], 3.4f);
    polyFlat(g, *g.m, fp, e.z + 6.f, rgb(0.9f), M(MAT_ROOF_GRAVEL));
    collide(g, vec3(e.c, e.z + 3.f), X, vec3(e.hx, e.hy, 3.f));
    const char* t = e.text.c_str();
    float th = 2.2f;
    float tw = textAdvance(t, th, 0.3f);
    vec2 face = perp(X);
    vec2 viewR = perp(face);
    strokeText(g, *g.m, t, vec3(e.c + face * (e.hy + 0.3f) - viewR * (tw * 0.5f), e.z + 6.4f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.35f, rgb(1.f, 0.3f, 0.9f, 0.9f),
               emMat(EA_CHASE, 40), g.detail ? 0.2f : 0.f, 0.3f);
    // striped game booths in front
    if (g.detail)
        for (int k = 0; k < 4; k++) {
            vec2 bc = e.c + face * (e.hy + 8.f) + X * (-e.hx + 4.f + k * 8.f);
            boxY(g, vec3(bc, e.z + 1.1f), X, vec3(1.6f, 1.4f, 1.1f), rgb(0.95f), M(MAT_WOOD));
            for (int s = 0; s < 4; s++)
                boxY(g, vec3(bc + X * (-1.35f + s * 0.9f), e.z + 2.6f), X, vec3(0.45f, 1.6f, 0.08f), (s & 1) ? rgb(0.9f, 0.1f, 0.15f) : rgb(0.95f), M(MAT_FABRIC), true);
            lamp(g, vec3(bc + face * 1.5f, e.z + 2.4f), 0.15f, hsvToRgb(k * 0.2f, 0.6f, 1.f), 0.9f, EA_BLINK, (u32)k * 50u);
        }
    light(g, vec3(e.c + face * (e.hy + 4.f), e.z + 5.f), vec3(1.f, 0.4f, 0.9f) * 5000.f, 20.f, 2);
}

// ------------------------------------------------------------------------------------------------ marinas, docks
void genMarina(const SiteElem& e, G& g) {
    bool detail = g.detail;
    vec2 d = e.ax, n = perp(d);
    float L = e.p[0], zDeck = e.p[1];
    vec2 start = e.c - d * 6.f;
    u32 wood = rgb(0.75f, 0.65f, 0.52f);
    // main pier in 25 m pieces
    for (float s = 0.f; s < L; s += 25.f) {
        vec2 a = start + d * s, b = start + d * Min(L, s + 25.f);
        if (g.owns((a + b) * 0.5f)) deckStrip(g, a, b, 2.2f, zDeck, -3.f, 6.f, false, wood);
    }
    Rng r(e.seed);
    int k = 0;
    for (float s = 14.f; s < L - 6.f; s += 13.f, k++) {
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 root = start + d * s + n * (sd * 2.2f);
            vec2 tip = root + n * (sd * 14.f);
            if (g.owns((root + tip) * 0.5f)) deckStrip(g, root, tip, 0.8f, zDeck - 0.6f, -3.f, 7.f, false, wood);
            u32 h = hash3i(k, sd, (int)e.seed);
            if (hashToFloat(h) < 0.25f) continue;
            vec2 bc = root + n * (sd * 8.f) + d * 3.6f;
            if (!g.owns(bc)) continue;
            int type = hashToFloat(h >> 8) < 0.45f ? 1 : (hashToFloat(h >> 8) < 0.8f ? 2 : 3);
            float len = type == 1 ? 12.f + (h % 7) : (type == 2 ? 10.f + (h % 5) : 11.f);
            boat(g, vec3(bc, 0.f), n * (float)sd, type, len, h);
        }
        if (detail && (k % 2) == 0) {
            vec2 lp = start + d * s;
            if (g.owns(lp)) {
                cyl(g, vec3(lp + n * 1.8f, zDeck), 0.08f, 0.06f, 3.2f, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
                lamp(g, vec3(lp + n * 1.8f, zDeck + 3.3f), 0.25f, vec3(1.f, 0.9f, 0.7f), 0.8f, EA_NIGHT);
                light(g, vec3(lp + n * 1.8f, zDeck + 3.3f), vec3(1.f, 0.85f, 0.6f) * 1800.f, 12.f, 0);
            }
        }
    }
    // T-head with a superyacht and the fuel dock / dockmaster hut
    vec2 th = start + d * L;
    if (g.owns(th)) {
        deckStrip(g, th - n * 30.f, th + n * 30.f, 2.5f, zDeck, -3.f, 6.f, false, wood);
        boat(g, vec3(th + d * 6.5f, 0.f), n, 5, 42.f, e.seed * 7u);
        boxY(g, vec3(th - n * 20.f, zDeck + 1.4f), d, vec3(2.f, 2.5f, 1.4f), rgb(0.95f, 0.92f, 0.85f), M(MAT_WOOD_SIDING));
        boxY(g, vec3(th - n * 20.f, zDeck + 2.95f), d, vec3(2.4f, 2.9f, 0.15f), rgb(0.3f, 0.5f, 0.55f), M(MAT_ROOF_METAL), true);
        collide(g, vec3(th - n * 20.f, zDeck + 1.4f), d, vec3(2.f, 2.5f, 1.4f));
        light(g, vec3(th, zDeck + 4.f), vec3(1.f, 0.9f, 0.7f) * 4000.f, 25.f, 1);
    }
    // club sign at the landing
    vec2 sp = e.c - d * 12.f + n * 6.f;
    if (g.owns(sp) && detail) {
        float z = gMap->heightAt(sp.x, sp.y);
        boxY(g, vec3(sp, z + 1.1f), n, vec3(3.2f, 0.25f, 0.9f), rgb(0.1f, 0.25f, 0.45f), M(MAT_WOOD), true);
        const char* t = e.text.c_str();
        float tht = 0.42f;
        float tw = textAdvance(t, tht, 0.3f);
        vec2 face = -d;
        vec2 viewR = perp(face);
        strokeText(g, *g.m, t, vec3(sp + face * 0.27f - viewR * (tw * 0.5f), z + 0.9f), vec3(viewR, 0), vec3(0, 0, 1), tht, 0.07f, rgb(1.f, 0.9f, 0.6f, 0.4f), emMat(EA_NIGHT),
                   0.f, 0.3f);
    }
}

void genRiverMarina(const SiteElem& e, G& g) {
    bool detail = g.detail;
    vec2 t = e.ax, n = perp(t);
    float bank = e.p[0], zDeck = e.p[1];
    float halfLen = e.hx;
    vec2 dockC = e.c + n * (bank - 7.f);
    u32 wood = rgb(0.7f, 0.62f, 0.5f);
    for (float s = -halfLen; s < halfLen; s += 20.f) {
        vec2 a = dockC + t * s, b = dockC + t * Min(halfLen, s + 20.f);
        if (g.owns((a + b) * 0.5f)) deckStrip(g, a, b, 1.5f, zDeck, -2.5f, 5.f, false, wood);
    }
    // gangways to the bank
    for (int k = -1; k <= 1; k += 2) {
        vec2 a = dockC + t * (k * halfLen * 0.6f), b = e.c + n * (bank + 2.5f) + t * (k * halfLen * 0.6f);
        if (g.owns((a + b) * 0.5f)) {
            float zb = gMap->heightAt(b.x, b.y) + 0.2f;
            vec2 d = normalize(b - a), nn = perp(d);
            quad(g, *g.m, vec3(a - nn * 0.8f, zDeck), vec3(b - nn * 0.8f, zb), vec3(b + nn * 0.8f, zb), vec3(a + nn * 0.8f, zDeck), rgb(0.6f), M(MAT_METAL_BRUSHED), vec3(0, 0, 1));
            for (int s = -1; s <= 1; s += 2) beam(g, vec3(a + nn * (s * 0.8f), zDeck + 1.f), vec3(b + nn * (s * 0.8f), zb + 1.f), 0.06f, 0.06f, rgb(0.8f), M(MAT_METAL_BRUSHED));
        }
    }
    // finger slips toward the channel with sport boats
    int k = 0;
    for (float s = -halfLen + 6.f; s < halfLen - 4.f; s += 9.f, k++) {
        vec2 root = dockC + t * s - n * 1.5f;
        vec2 tip = root - n * 8.f;
        if (g.owns((root + tip) * 0.5f)) deckStrip(g, root, tip, 0.6f, zDeck - 0.3f, -2.5f, 8.f, false, wood);
        u32 h = hash3i(k, 3, (int)e.seed);
        if (hashToFloat(h) < 0.2f) continue;
        vec2 bc = root - n * 4.5f + t * 4.4f;
        if (!g.owns(bc)) continue;
        float r = hashToFloat(h >> 6);
        int type = r < 0.55f ? 0 : (r < 0.8f ? 1 : 6);
        boat(g, vec3(bc, 0.f), -n, type, type == 1 ? 11.f : 7.f + (h % 3), h);
    }
    // bait & tackle shop on the bank and the sign
    vec2 shop = e.c + n * (bank + 16.f) - t * (halfLen * 0.2f);
    {
        // slide along the bank to the first spot clear of streets (deterministic: every cell finds the same one)
        const float offs[] = {-0.2f, 0.f, -0.4f, 0.2f, -0.6f, 0.4f};
        bool found = false;
        for (float o : offs) {
            vec2 q = e.c + n * (bank + 16.f) + t * (halfLen * o);
            if (gRoads && gRoads->nearRoad(q, 11.5f)) continue;
            shop = q;
            found = true;
            break;
        }
        if (!found) shop = vec2(1e9f);
    }
    if (g.owns(shop)) {
        float z = gMap->heightAt(shop.x, shop.y);
        std::vector<vec2> fp = rectPoly(shop, t, 9.f, 6.f);
        facadeRing(g, fp, z - 0.5f, z + 4.4f, z, (u32)e.p[7], 3.4f);
        polyFlat(g, *g.m, fp, z + 4.4f, rgb(0.85f), M(MAT_ROOF_GRAVEL));
        collide(g, vec3(shop, z + 2.2f), t, vec3(9.f, 6.f, 2.2f));
        const char* tx = e.text.c_str();
        float th = 0.9f;
        float tw = textAdvance(tx, th, 0.3f);
        vec2 face = -n;
        vec2 viewR = perp(face);
        strokeText(g, *g.m, tx, vec3(shop + face * 6.3f - viewR * (tw * 0.5f), z + 4.6f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.15f, rgb(0.2f, 0.9f, 1.f, 0.7f), emMat(EA_NIGHT),
                   detail ? 0.1f : 0.f, 0.3f);
        light(g, vec3(shop + face * 8.f, z + 4.f), vec3(1.f, 0.85f, 0.6f) * 3000.f, 18.f, 1);
    }
    if (detail)
        for (float s = -halfLen; s < halfLen; s += 30.f) {
            vec2 lp = dockC + t * s + n * 1.2f;
            if (!g.owns(lp)) continue;
            cyl(g, vec3(lp, zDeck), 0.07f, 0.05f, 2.6f, 6, rgb(0.85f), M(MAT_METAL_PAINTED), false);
            lamp(g, vec3(lp, zDeck + 2.7f), 0.22f, vec3(1.f, 0.9f, 0.7f), 0.8f, EA_NIGHT);
            light(g, vec3(lp, zDeck + 2.7f), vec3(1.f, 0.85f, 0.6f) * 1500.f, 11.f, 0);
        }
}

void genDock(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 d = e.ax, n = perp(d);
    float water = e.z;
    float L = e.p[0];
    float zDeck = water + 1.1f;
    u32 wood = e.variant == 0 ? rgb(0.55f, 0.5f, 0.45f) : rgb(0.72f, 0.62f, 0.5f);
    vec2 a = e.c - d * 4.f, b = e.c + d * L;
    float zLand = Max(gMap->heightAt(a.x, a.y) + 0.15f, zDeck);
    // short ramp from the shore
    vec2 an = perp(d);
    quad(g, *g.m, vec3(a - an * 1.2f, zLand), vec3(e.c - an * 1.2f, zDeck), vec3(e.c + an * 1.2f, zDeck), vec3(a + an * 1.2f, zLand), wood, M(MAT_WOOD), vec3(0, 0, 1));
    float w = e.variant == 2 ? 1.6f : 1.2f;
    deckStrip(g, e.c, b, w, zDeck, water - 3.f, 3.f, e.variant == 2, wood);
    if (e.variant == 2) deckStrip(g, b - n * 10.f, b + n * 10.f, 1.8f, zDeck, water - 3.f, 3.f, true, wood);
    if (e.variant == 3) {
        for (float s = 5.f; s < L; s += 8.f)
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 root = e.c + d * s + n * (sd * w);
                deckStrip(g, root, root + n * (sd * 7.f), 0.5f, zDeck - 0.2f, water - 3.f, 7.f, false, wood);
                u32 h = hash3i((int)s, sd, (int)e.seed);
                if (hashToFloat(h) < 0.3f) continue;
                boat(g, vec3(e.c + d * (s + 3.8f) + n * (sd * (w + 4.f)), water), n * (float)sd, hashToFloat(h >> 5) < 0.6f ? 0 : 6, 6.5f + (h % 3), h);
            }
    }
    if (e.p[1] > 0.5f && e.variant != 3) {
        int type = e.variant == 1 ? 1 : (e.variant == 4 ? 4 : 6);
        float len = e.variant == 1 ? 14.f : (e.variant == 4 ? 5.5f : 5.5f);
        boat(g, vec3(e.c + d * (L * 0.55f) + n * (w + (type == 1 ? 2.8f : 1.4f)), water), d, type, len, e.seed);
        if (e.variant == 4) boat(g, vec3(e.c + d * (L * 0.55f) - n * (w + 1.6f), water), d, 4, 5.5f, e.seed + 1u);
    }
    if (g.detail) {
        vec2 lp = b - d * 0.5f + n * (w - 0.1f);
        cyl(g, vec3(lp, zDeck), 0.08f, 0.06f, 2.4f, 6, rgb(0.8f), M(MAT_METAL_PAINTED), false);
        lamp(g, vec3(lp, zDeck + 2.5f), 0.2f, vec3(1.f, 0.85f, 0.6f), 0.7f, EA_NIGHT);
        light(g, vec3(lp, zDeck + 2.5f), vec3(1.f, 0.85f, 0.6f) * 900.f, 9.f, 0);
    }
}

// ------------------------------------------------------------------------------------------------ beach clubs
void genBeachClub(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 face = e.ax, side = perp(face);
    float z = gMap->heightAt(e.c.x, e.c.y) + 0.45f;
    u32 deckC = rgb(0.95f, 0.92f, 0.88f);
    // raised deck
    std::vector<vec2> deck = rectPoly(e.c, face, e.hx * 0.8f, e.hy * 0.9f);
    size_t dv0 = g.m->verts.size();
    polyFlat(g, *g.m, deck, z, deckC, M(MAT_PAVERS));
    paverUV(g, *g.m, dv0);
    for (int k = 0; k < 4; k++) {
        vec2 a = deck[k], b = deck[(k + 1) % 4];
        vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
        quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(b, z - 1.2f), vec3(a, z - 1.2f), rgb(0.9f, 0.85f, 0.75f), M(MAT_STUCCO), vec3(on, 0));
    }
    collide(g, vec3(e.c, z - 0.6f), face, vec3(e.hx * 0.8f, e.hy * 0.9f, 0.6f));
    // infinity pool facing the ocean
    vec2 pc = e.c + face * (e.hx * 0.25f);
    std::vector<vec2> pool = rectPoly(pc, face, 6.f, e.hy * 0.55f);
    polyFlat(g, *g.m, pool, z - 1.4f, kWhiteC, M(MAT_TILE_POOL));
    for (int k = 0; k < 4; k++) {
        vec2 a = pool[k], b = pool[(k + 1) % 4];
        vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
        quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(b, z - 1.4f), vec3(a, z - 1.4f), kWhiteC, M(MAT_TILE_POOL), vec3(-on, 0));
    }
    polyFlat(g, *g.m, pool, z - 0.12f, rgb(0.35f, 0.75f, 0.85f), M(MAT_GLASS));
    // bar pavilion with a thatched hip roof
    vec2 bc = e.c - face * (e.hx * 0.45f);
    for (int k = 0; k < 4; k++) {
        vec2 cp = bc + face * ((k & 1) ? 5.f : -5.f) + side * ((k & 2) ? 5.f : -5.f);
        cyl(g, vec3(cp, z), 0.18f, 0.16f, 3.2f, 6, rgb(0.5f, 0.38f, 0.25f), M(MAT_WOOD), false);
    }
    {
        float hs = 7.f;
        vec3 apex(bc, z + 6.5f);
        for (int k = 0; k < 4; k++) {
            float a0 = kHalfPi * k + kPi * 0.25f, a1 = a0 + kHalfPi;
            vec3 p0(bc + vec2(cosf(a0), sinf(a0)) * (hs * 1.41f), z + 3.f), p1(bc + vec2(cosf(a1), sinf(a1)) * (hs * 1.41f), z + 3.f);
            MeshData& m = *g.m;
            vec3 nn = normalize(cross(p1 - p0, apex - p0));
            if (nn.z < 0) nn = -nn;
            u32 i0 = m.addVertex(p0 - g.org, nn, normalize(p1 - p0), vec2(0, 0), rgb(0.85f, 0.7f, 0.45f), M(MAT_ROOF_SHINGLE));
            u32 i1 = m.addVertex(p1 - g.org, nn, normalize(p1 - p0), vec2(4, 0), rgb(0.85f, 0.7f, 0.45f), M(MAT_ROOF_SHINGLE));
            u32 i2 = m.addVertex(apex - g.org, nn, normalize(p1 - p0), vec2(2, 3), rgb(0.85f, 0.7f, 0.45f), M(MAT_ROOF_SHINGLE));
            if (dot(cross(p1 - p0, apex - p0), nn) > 0) m.tri(i0, i1, i2);
            else m.tri(i0, i2, i1);
        }
        boxY(g, vec3(bc, z + 0.55f), face, vec3(3.f, 3.f, 0.55f), rgb(0.55f, 0.4f, 0.28f), M(MAT_WOOD));
        collide(g, vec3(bc, z + 0.55f), face, vec3(3.f, 3.f, 0.55f));
    }
    // cabanas along the pool and umbrellas on the sand
    for (int k = 0; k < 6; k++) {
        vec2 cc = pc + side * ((k < 3 ? -1.f : 1.f) * (e.hy * 0.55f + 4.f)) + face * (-6.f + (k % 3) * 6.f);
        for (int c = 0; c < 4; c++) cyl(g, vec3(cc + face * ((c & 1) ? 1.4f : -1.4f) + side * ((c & 2) ? 1.4f : -1.4f), z), 0.06f, 0.06f, 2.4f, 5, kWhiteC, M(MAT_WOOD), false);
        boxY(g, vec3(cc, z + 2.5f), face, vec3(1.6f, 1.6f, 0.12f), rgb(0.97f, 0.97f, 0.95f), M(MAT_FABRIC), true);
        if (detail) boxY(g, vec3(cc, z + 0.35f), face, vec3(1.f, 0.6f, 0.2f), e.variant ? rgb(0.95f, 0.5f, 0.35f) : rgb(0.2f, 0.55f, 0.65f), M(MAT_FABRIC), true);
    }
    if (detail) {
        for (int k = 0; k < 10; k++) {
            vec2 up = e.c + face * (e.hx * 0.95f + (k & 1) * 6.f) + side * (-e.hy + k * (e.hy * 0.2f));
            prop(g, vec3(up, gMap->heightAt(up.x, up.y)), 0.f, 1.f, PROP_UMBRELLA, (u8)k);
        }
        for (int k = 0; k < 6; k++) {
            vec2 pp = e.c - face * (e.hx * 0.8f + 3.f) + side * (-e.hy * 0.8f + k * e.hy * 0.32f);
            prop(g, vec3(pp, gMap->heightAt(pp.x, pp.y)), (float)k, 1.1f, PROP_PALM, (u8)k);
        }
        // string lights between posts around the deck
        for (int k = 0; k < 16; k++) {
            float t = k / 16.f;
            vec2 p = lerp(deck[0], deck[1], t);
            lamp(g, vec3(p, z + 3.f - sinf(fmodf(t * 4.f, 1.f) * kPi) * 0.4f), 0.12f, vec3(1.f, 0.8f, 0.5f), 0.8f, EA_NIGHT);
            vec2 p2 = lerp(deck[2], deck[3], t);
            lamp(g, vec3(p2, z + 3.f - sinf(fmodf(t * 4.f, 1.f) * kPi) * 0.4f), 0.12f, vec3(1.f, 0.8f, 0.5f), 0.8f, EA_NIGHT);
        }
        for (int k = 0; k < 4; k++) cyl(g, vec3(deck[k], z), 0.1f, 0.08f, 3.3f, 6, rgb(0.5f, 0.38f, 0.25f), M(MAT_WOOD), false);
        const char* t = e.text.c_str();
        float th = 0.9f;
        float tw = textAdvance(t, th, 0.3f);
        vec2 f2 = -face;
        vec2 viewR = perp(f2);
        strokeText(g, *g.m, t, vec3(bc + f2 * 7.2f - viewR * (tw * 0.5f), z + 3.4f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.15f, rgb(1.f, 0.6f, 0.3f, 0.7f), emMat(EA_NIGHT),
                   0.08f, 0.3f);
    }
    light(g, vec3(bc, z + 3.f), vec3(1.f, 0.75f, 0.45f) * 5000.f, 22.f, 1);
    light(g, vec3(pc, z + 0.2f), vec3(0.3f, 0.8f, 1.f) * 3000.f, 16.f, 2);
}

// ------------------------------------------------------------------------------------------------ golf
void drapeDisc(G& g, vec2 c, vec2 ax, float rx, float ry, u32 col, u32 mat, float lift) {
    int seg = g.detail ? 20 : 10;
    vec2 ay = perp(ax);
    MeshData& m = *g.d;
    u32 b0 = m.addVertex(vec3(c, gMap->heightAt(c.x, c.y) + lift) - g.org, vec3(0, 0, 1), vec3(1, 0, 0), c, col, mat);
    for (int k = 0; k <= seg; k++) {
        float a = kTwoPi * k / seg;
        vec2 p = c + ax * (cosf(a) * rx) + ay * (sinf(a) * ry);
        m.addVertex(vec3(p, gMap->heightAt(p.x, p.y) + lift) - g.org, vec3(0, 0, 1), vec3(1, 0, 0), p, col, mat);
    }
    for (int k = 0; k < seg; k++) {
        vec3 fn = cross(m.verts[b0 + 1 + k].pos - m.verts[b0].pos, m.verts[b0 + 2 + k].pos - m.verts[b0].pos);
        if (fn.z > 0) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
        else m.tri(b0, b0 + 2 + k, b0 + 1 + k);
    }
}

void genGolfHole(const SiteElem& e, G& g) {
    vec2 a = e.a, b = e.b;
    vec2 d = normalize(b - a), n = perp(d);
    float L = length(b - a), hw = e.p[0];
    Rng r(e.seed);
    // fairway drape in 10 m strips with mowing stripes (only strips centered in this cell)
    float step = g.detail ? 10.f : 20.f;
    for (float s = 14.f; s < L - 12.f; s += step) {
        float s1 = Min(L - 12.f, s + step);
        vec2 mid = a + d * ((s + s1) * 0.5f);
        if (!g.owns(mid)) continue;
        float w0 = hw * (0.75f + 0.25f * sinf(s / L * kPi)), w1 = hw * (0.75f + 0.25f * sinf(s1 / L * kPi));
        int across = g.detail ? 4 : 2;
        for (int k = 0; k < across; k++) {
            float t0 = -1.f + 2.f * k / across, t1 = -1.f + 2.f * (k + 1) / across;
            vec2 p00 = a + d * s + n * (t0 * w0), p10 = a + d * s1 + n * (t0 * w1), p11 = a + d * s1 + n * (t1 * w1), p01 = a + d * s + n * (t1 * w0);
            bool stripe = ((int)(s / step) & 1) != 0;
            u32 c = stripe ? rgb(0.72f, 1.1f, 0.55f) : rgb(0.62f, 0.98f, 0.48f);
            auto H = [&](vec2 p) { return gMap->heightAt(p.x, p.y) + 0.05f; };
            g.d->quadFacing(vec3(p00, H(p00)) - g.org, vec3(p10, H(p10)) - g.org, vec3(p11, H(p11)) - g.org, vec3(p01, H(p01)) - g.org, p00, p10, p11, p01, c,
                            M(MAT_GRASS), vec3(0, 0, 1));
        }
    }
    // green, tee box, bunkers, flagstick
    if (g.owns(b)) {
        drapeDisc(g, b, d, 15.f, 12.f, rgb(0.55f, 1.15f, 0.5f), M(MAT_GRASS), 0.07f);
        for (int k = 0; k < 2; k++) {
            vec2 bc = b + rotate(d, (k ? 1.f : -1.f) * r.range(1.8f, 2.4f)) * 17.f;
            drapeDisc(g, bc, normalize(vec2(r.range(-1.f, 1.f), r.range(-1.f, 1.f))), r.range(5.f, 7.f), r.range(3.f, 4.5f), rgb(1.1f, 1.05f, 0.95f), M(MAT_SAND), 0.08f);
        }
        float z = gMap->heightAt(b.x, b.y);
        cyl(g, vec3(b + n * 2.f, z), 0.03f, 0.03f, 2.3f, 4, kWhiteC, M(MAT_METAL_PAINTED), false);
        panel2(g, vec3(b + n * 2.f, z + 2.3f), vec3(b + n * 2.f + d * 0.6f, z + 2.28f), vec3(b + n * 2.f + d * 0.6f, z + 1.85f), vec3(b + n * 2.f, z + 1.88f),
               rgb(0.95f, 0.1f, 0.1f), M(MAT_FABRIC));
    }
    if (g.owns(a)) {
        vec2 tc = a + d * 3.f;
        std::vector<vec2> tee = rectPoly(tc, d, 6.f, 4.f);
        drapeDisc(g, tc, d, 6.5f, 4.5f, rgb(0.6f, 1.1f, 0.55f), M(MAT_GRASS), 0.07f);
        if (g.detail)
            for (int s = -1; s <= 1; s += 2) {
                vec2 mk = tc + n * (s * 2.5f);
                cyl(g, vec3(mk, gMap->heightAt(mk.x, mk.y)), 0.1f, 0.1f, 0.25f, 6, rgb(0.9f, 0.9f, 0.2f), M(MAT_PLASTER), true);
            }
        (void)tee;
    }
    vec2 fb = a + d * (L * r.range(0.5f, 0.65f)) + n * (hw * (r.chance(0.5f) ? 1.f : -1.f));
    if (g.owns(fb)) drapeDisc(g, fb, d, r.range(7.f, 10.f), r.range(3.5f, 5.f), rgb(1.1f, 1.05f, 0.95f), M(MAT_SAND), 0.09f);
}

void genGolfPond(const SiteElem& e, G& g) {
    if (!g.owns(e.c) || !g.detail) return;
    for (int k = 0; k < 18; k++) {
        float a = kTwoPi * k / 18;
        vec2 p = e.c + e.ax * (cosf(a) * (e.hx - 1.f)) + perp(e.ax) * (sinf(a) * (e.hy - 1.f));
        prop(g, vec3(p, gMap->heightAt(p.x, p.y)), a, 0.8f, PROP_SAWGRASS, (u8)k);
    }
}

// ------------------------------------------------------------------------------------------------ beach access
// Where a street stops short of the sand (RoadNetwork::beachEnds): a low boardwalk of weathered planks laid on the sand
// from the sidewalk to a few metres short of the water, bollards across its mouth, a sign with the street's name and an
// outdoor shower with its foot tap on a small slab beside the entrance
void genBeachAccess(const SiteElem& e, G& g) {
    vec2 d = normalize(e.b - e.a), n = perp(d);
    float L = length(e.b - e.a);
    const float W = 1.25f, lift = 0.1f;
    const u32 wood = M(MAT_WOOD);
    auto ground = [&](vec2 p) { return gMap->heightAt(p.x, p.y); };
    // boardwalk: 1.8 m panels following the sand, each a deck, two edge boards and a dark joint strip where panels meet
    int np = Max(1, (int)ceilf(L / 1.8f));
    for (int k = 0; k < np; k++) {
        float s0 = L * k / np, s1 = L * (k + 1) / np;
        vec2 p0 = e.a + d * s0, p1 = e.a + d * s1;
        if (!g.owns((p0 + p1) * 0.5f)) continue;
        float z0 = ground(p0) + lift, z1 = ground(p1) + lift;
        u32 h = hash2i(k, (int)(e.seed & 0x7fffffu));
        float tone = 0.78f + 0.2f * hashToFloat(h);
        u32 c = rgb(0.82f * tone, 0.74f * tone, 0.63f * tone);   // sun-greyed boards
        MeshData& m = *g.m;
        u32 b = (u32)m.verts.size();
        const vec2 q[4] = {p0 - n * W, p1 - n * W, p1 + n * W, p0 + n * W};
        const float zq[4] = {z0, z1, z1, z0};
        vec3 nrm = normalize(cross(vec3(d * (s1 - s0), z1 - z0), vec3(n, 0.f)));
        for (int i = 0; i < 4; i++)
            m.addVertex(vec3(q[i], zq[i]) - g.org, nrm, vec3(n, 0.f), vec2(dot(q[i], n), dot(q[i], d) * 0.25f), c, wood);
        m.quadIdx(b, b + 1, b + 2, b + 3);
        for (int s = -1; s <= 1; s += 2) {
            vec2 a0 = p0 + n * (s * W), a1 = p1 + n * (s * W);
            quad(g, *g.m, vec3(a0, z0 - lift - 0.06f), vec3(a1, z1 - lift - 0.06f), vec3(a1, z1), vec3(a0, z0), rgb(0.5f * tone, 0.44f * tone, 0.37f * tone), wood,
                 vec3(n * (float)s, 0.f));
        }
        if (g.detail && k > 0)
            quad(g, *g.m, vec3(p0 - n * W + d * 0.012f, z0 + 0.004f), vec3(p0 - n * W - d * 0.012f, z0 + 0.004f), vec3(p0 + n * W - d * 0.012f, z0 + 0.004f),
                 vec3(p0 + n * W + d * 0.012f, z0 + 0.004f), rgb(0.12f), wood, vec3(0, 0, 1));
    }
    if (!g.detail || !g.owns(e.a)) return;
    // three bollards across the mouth
    for (int k = -1; k <= 1; k++) {
        vec2 p = e.a - d * 0.3f + n * (k * 1.15f);
        prop(g, vec3(p, ground(p)), 0.f, 1.f, PROP_BOLLARD);
    }
    // sign: blue panel on a post, the street's name over BEACH ACCESS, reading from the street
    {
        vec2 sp = e.a - d * 0.2f + n * (W + 0.9f);
        float z = ground(sp);
        cyl(g, vec3(sp, z), 0.045f, 0.04f, 2.6f, 6, rgb(0.62f), M(MAT_METAL_BRUSHED), true);
        vec2 face = -d, rt = perp(face);   // the reader's right as they face the sign from the street
        vec3 c(sp + face * 0.06f, z + 2.15f);
        const float hw = 0.55f, hh = 0.36f;
        quad(g, *g.m, c - vec3(rt * hw, hh), c + vec3(rt * hw, -hh), c + vec3(rt * hw, hh), c + vec3(-rt * hw, hh), rgb(0.1f, 0.3f, 0.62f), M(MAT_METAL_PAINTED),
             vec3(face, 0.f));
        quad(g, *g.m, c - vec3(rt * hw, hh) - vec3(face * 0.02f, 0.f), c + vec3(rt * hw, -hh) - vec3(face * 0.02f, 0.f), c + vec3(rt * hw, hh) - vec3(face * 0.02f, 0.f),
             c + vec3(-rt * hw, hh) - vec3(face * 0.02f, 0.f), rgb(0.55f), M(MAT_METAL_PAINTED), vec3(-face, 0.f));
        std::string top = e.text;
        for (char& ch : top) ch = (char)toupper((unsigned char)ch);
        const char* lines[2] = {top.c_str(), "BEACH ACCESS"};
        for (int li = 0; li < 2; li++) {
            float th = li == 0 ? 0.15f : 0.13f;
            float tw = textAdvance(lines[li], th, 0.3f);
            float sc = tw > hw * 1.8f ? hw * 1.8f / tw : 1.f;
            float tz = c.z + (li == 0 ? 0.06f : -0.2f);
            strokeText(g, *g.m, lines[li], vec3(sp + face * 0.07f - rt * (tw * sc * 0.5f), tz), vec3(rt, 0.f), vec3(0, 0, 1), th * sc, th * sc * 0.15f, rgb(0.95f),
                       M(MAT_METAL_PAINTED), 0.f, 0.3f);
        }
        collide(g, vec3(sp, z + 1.3f), d, vec3(0.05f, 0.05f, 1.3f));
    }
    // outdoor shower: steel post, arm and rose, a foot tap low down, on a slab with a drain
    {
        vec2 sp = e.a + d * 1.6f - n * (W + 1.3f);
        float z = ground(sp);
        boxY(g, vec3(sp, z + 0.05f), d, vec3(0.7f, 0.7f, 0.05f), rgb(0.72f, 0.7f, 0.66f), M(MAT_CONCRETE), true);
        cyl(g, vec3(sp, z + 0.1f), 0.05f, 0.05f, 2.25f, 8, rgb(0.8f), M(MAT_METAL_BRUSHED), true);
        vec3 top(sp, z + 2.3f);
        beam(g, top - vec3(0, 0, 0.05f), top + vec3(d * 0.35f, -0.05f), 0.035f, 0.035f, rgb(0.8f), M(MAT_METAL_BRUSHED));
        cyl(g, top + vec3(d * 0.35f, -0.16f), 0.09f, 0.05f, 0.08f, 8, rgb(0.75f), M(MAT_METAL_BRUSHED), true);
        beam(g, vec3(sp, z + 0.55f), vec3(sp + d * 0.18f, z + 0.55f), 0.03f, 0.03f, rgb(0.8f), M(MAT_METAL_BRUSHED));
        cyl(g, vec3(sp + d * 0.3f, z + 0.1f), 0.09f, 0.09f, 0.005f, 8, rgb(0.15f), M(MAT_METAL_BRUSHED), true);
        collide(g, vec3(sp, z + 1.15f), d, vec3(0.06f, 0.06f, 1.15f));
    }
}

}  // namespace leisure_mesh
}  // namespace World
