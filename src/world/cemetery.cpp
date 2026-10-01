// Santa Marea Cemetery, Calle Luna: a walled Gulf-South "city of the dead" filling the block between 33rd and 31st
// Avenues and S 6th and S 8th Streets. The perimeter wall is a range of whitewashed wall vaults (three tiers of marble
// tablets facing in); the main gate on 33rd Avenue (stucco piers with urns and lanterns, open wrought-iron gates, the
// name in iron letters under the arch) opens on a shell avenue lined with Italian cypress that runs past the granite
// obelisk at the rond-point to the mission chapel at the east end. Rows of above-ground tombs face the aisles:
// whitewashed family tombs with pediment, stepped and temple fronts, sarcophagi, coping graves, small obelisks and
// headstones, with the granite mausoleums and the society tombs on the avenue frontage. Live oaks and bald cypress stand
// among the tombs; the caretaker's shed is in the south-east corner; pedestrian gates open on S 6th and S 8th Streets and
// a service gate on 31st Avenue. The chapel generator also builds the town churches of the churchyards (churchyard.cpp).
#include "places.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace cemetery {

using namespace sitegeo;
using namespace place_kit;

// Street centre lines that stay round the block (the grid lines inside are left out: roads.cpp checks the road blocks)
constexpr float kX0 = 1500.f, kX1 = 1700.f, kY0 = -850.f, kY1 = -650.f;
// Outer face of the perimeter wall at the far edge of each sidewalk: 33rd Avenue (half width 8.8 + sidewalk 3.5) on the
// west; 31st Avenue, S 6th and S 8th Streets (5.6 + 3.0)
constexpr float kWX0 = kX0 + 12.3f, kWX1 = kX1 - 8.6f, kWY0 = kY0 + 8.6f, kWY1 = kY1 - 8.6f;
constexpr float kWallT = 0.45f, kVaultD = 2.4f, kRangeT = kWallT + kVaultD, kVaultH = 3.1f;
// Inner face of the wall vault ranges
constexpr float kIX0 = kWX0 + kRangeT, kIX1 = kWX1 - kRangeT, kIY0 = kWY0 + kRangeT, kIY1 = kWY1 - kRangeT;
constexpr float kAvY = -750.f, kAvX = 1600.f;   // main avenue (east-west) and cross avenue (north-south) centre lines
constexpr float kAvHW = 3.5f, kVerge = 1.6f, kCrossHW = 2.5f, kRondR = 11.f, kPeriW = 4.f;
constexpr float kChapelX = 1667.f, kChapelHW = 5.5f, kChapelHL = 9.5f, kPlazaX0 = 1648.f, kPlazaHW = 9.f;
constexpr float kYardX0 = 1662.f, kYardY1 = -812.f, kServiceY = -823.f;
constexpr float kGateMainHW = 3.5f, kGatePedHW = 1.8f, kGateServiceHW = 2.2f;

// Whitewash and weathered stucco, a few pastel tombs
const vec3 kWash[6] = {vec3(0.95f, 0.94f, 0.9f), vec3(0.97f, 0.96f, 0.93f), vec3(0.93f, 0.9f, 0.82f),
                       vec3(0.85f, 0.85f, 0.83f), vec3(0.76f, 0.75f, 0.72f), vec3(0.91f, 0.89f, 0.86f)};
const vec3 kPastel[4] = {vec3(0.94f, 0.84f, 0.82f), vec3(0.82f, 0.87f, 0.93f), vec3(0.95f, 0.91f, 0.74f), vec3(0.85f, 0.91f, 0.84f)};
const char* const kSurnames[] = {"REYES",  "DUPRE",    "SALAZAR", "FONTENOT", "BRANCATO", "MARTEL", "ESPINOSA", "LANDRY",
                                 "ROMERO", "BERTRAND", "OLIVARES", "GUIDRY",  "VALLEJO",  "HEBERT", "SOLANO",   "MARINO",
                                 "CORDERO", "PICARD",  "ARCENEAUX", "TORRES", "CASTILLO", "LEBLANC", "QUINTANA", "AVILA"};
const char* const kSocieties[] = {"LA FRATERNAL", "MARINEROS UNIDOS", "LA ESPERANZA", "TORCEDORES", "HARBOR PILOTS", "LA CARIDAD"};

inline vec2 rightOf(vec2 f) { return vec2(f.y, -f.x); }

// ------------------------------------------------------------------------------------------------ plots
enum TombKind : u8 { TK_FAMILY = 0, TK_STEPPED, TK_TEMPLE, TK_SARCOPHAGUS, TK_COPING, TK_OBELISK, TK_HEADSTONE, TK_TREE, TK_SOCIETY, TK_MAUSOLEUM };

struct Plot {
    vec2 c, f;      // footprint centre; facing (toward the aisle or the avenue in front)
    float w, d;     // width along the row, depth
    u8 kind;
    bool frontage;  // on the main avenue
    u32 h;
};

// One quarter of the grounds between the avenues and the perimeter aisle: x0..x1; rows stacked from the avenue side (yAv)
// outward (ny = +1 north, -1 south) to the perimeter aisle (yEnd)
struct Section {
    float x0, x1, yAv, yEnd;
    int ny;
    u32 seed;
};
Section sectionOf(const SiteElem& e) { return {e.p[0], e.p[1], e.p[2], e.p[3], e.p[4] > 0.f ? 1 : -1, e.seed}; }

struct Row {
    float v0, v1;   // distance from the avenue side, outward
    int face;       // -1: faces the avenue side, +1: faces outward
    bool frontage;
};
struct Aisle {
    float v, w;
};

// The frontage row (mausoleums, society tombs) faces the avenue with a row backing onto it; then blocks of two rows back
// to back, each facing its aisle; the aisles take up the slack so the last row faces the perimeter aisle
void rowsOf(const Section& s, std::vector<Row>& rows, std::vector<Aisle>* aisles) {
    const float span = fabsf(s.yEnd - s.yAv), front = 5.2f, dep = 3.2f, gap = 0.8f;
    const float head = front + gap + dep, block = 2.f * dep + gap;
    int n = Max(0, (int)floorf((span - head) / (block + 4.f)));
    float aisle = n > 0 ? (span - head - n * block) / n : 0.f;
    float v = 0.f;
    rows.push_back({v, v + front, -1, true});
    v += front + gap;
    rows.push_back({v, v + dep, +1, false});
    v += dep;
    for (int k = 0; k < n; k++) {
        if (aisles) aisles->push_back({v + aisle * 0.5f, aisle});
        v += aisle;
        rows.push_back({v, v + dep, -1, false});
        v += dep + gap;
        rows.push_back({v, v + dep, +1, false});
        v += dep;
    }
}

// Cross alleys cut each row into segments
struct Segs {
    int n;
    float len, alley;
};
Segs segsOf(const Section& s) {
    Segs g;
    g.alley = 3.f;
    float W = s.x1 - s.x0;
    g.n = Max(1, (int)roundf(W / 26.f));
    g.len = (W - (g.n - 1) * g.alley) / g.n;
    return g;
}

// Ground kept clear of tombs: the rond-point, the chapel and its forecourt, the caretaker's yard (axis-aligned footprint)
bool plotBlocked(vec2 c, float hx, float hy) {
    vec2 q(Clamp(kAvX, c.x - hx, c.x + hx), Clamp(kAvY, c.y - hy, c.y + hy));
    if (length(q - vec2(kAvX, kAvY)) < kRondR + 1.2f) return true;
    if (c.x + hx > kPlazaX0 - 1.5f && fabsf(c.y - kAvY) - hy < kPlazaHW + 1.2f) return true;
    if (c.x + hx > kYardX0 - 1.f && c.y - hy < kYardY1 + 1.f) return true;
    return false;
}

void plotsOf(const Section& s, std::vector<Plot>& out) {
    std::vector<Row> rows;
    rowsOf(s, rows, nullptr);
    const Segs sg = segsOf(s);
    for (size_t ri = 0; ri < rows.size(); ri++) {
        const Row& rw = rows[ri];
        const vec2 f(0.f, (float)(rw.face * s.ny));
        for (int si = 0; si < sg.n; si++) {
            float sx0 = s.x0 + si * (sg.len + sg.alley);
            int np = Max(1, (int)floorf(sg.len / (rw.frontage ? 5.6f : 3.3f)));
            float pitch = sg.len / np;
            for (int k = 0; k < np; k++) {
                Plot p;
                p.h = hash32(s.seed * 7919u + (u32)ri * 131071u + (u32)(si * 64 + k) * 2654435761u);
                p.f = f;
                p.frontage = rw.frontage;
                float r = hashToFloat(p.h), r2 = hashToFloat(p.h >> 9);
                if (rw.frontage) {
                    p.kind = r < 0.42f ? TK_MAUSOLEUM : (r < 0.62f ? TK_SOCIETY : (r < 0.84f ? TK_TEMPLE : TK_FAMILY));
                    p.w = p.kind == TK_MAUSOLEUM ? 4.4f + r2 * 0.6f : (p.kind == TK_SOCIETY ? 5.2f : 3.1f);
                    p.d = p.kind == TK_MAUSOLEUM ? 4.8f : (p.kind == TK_SOCIETY ? 4.4f : 3.4f);
                } else {
                    p.kind = r < 0.34f ? TK_FAMILY
                                       : (r < 0.47f ? TK_STEPPED
                                                    : (r < 0.55f ? TK_TEMPLE
                                                                 : (r < 0.66f ? TK_SARCOPHAGUS
                                                                              : (r < 0.78f ? TK_COPING : (r < 0.84f ? TK_OBELISK : (r < 0.975f ? TK_HEADSTONE : TK_TREE))))));
                    switch (p.kind) {
                        case TK_FAMILY: p.w = Clamp(pitch - 0.8f, 2.f, 2.6f), p.d = 2.9f; break;
                        case TK_STEPPED: p.w = 2.3f, p.d = 2.8f; break;
                        case TK_TEMPLE: p.w = 2.5f, p.d = 2.95f; break;
                        case TK_SARCOPHAGUS: p.w = 1.4f, p.d = 2.5f; break;
                        case TK_COPING: p.w = r2 < 0.4f ? 2.3f : 1.3f, p.d = 2.8f; break;
                        case TK_OBELISK: p.w = 1.3f, p.d = 1.3f; break;
                        case TK_HEADSTONE: p.w = 2.4f, p.d = 2.6f; break;
                        default: p.w = 1.f, p.d = 1.f; break;
                    }
                }
                float x = sx0 + pitch * (k + 0.5f) + (rw.frontage ? 0.f : (r2 - 0.5f) * 0.2f);
                float v = rw.face < 0 ? rw.v0 + 0.25f + p.d * 0.5f : rw.v1 - 0.25f - p.d * 0.5f;
                p.c = vec2(x, s.yAv + s.ny * v);
                if (plotBlocked(p.c, p.w * 0.5f + 0.2f, p.d * 0.5f + 0.2f)) continue;
                out.push_back(p);
            }
        }
    }
}

// People standing at this tomb (layout anchors): about one tomb in 24
inline bool mournedAt(const Plot& p) { return p.kind != TK_TREE && ((p.h >> 11) % 24u) == 3u; }

// ------------------------------------------------------------------------------------------------ small pieces
// Carved lettering seen from a few metres: short dark lines on a tablet facing f (u0..u1 across, z0..z1)
void inscription(G& g, vec2 p0, vec2 R, vec2 f, float u0, float u1, float z0, float z1, int lines, u32 h) {
    if (!g.detail) return;
    u32 ink = rgb(0.16f, 0.15f, 0.14f);
    for (int k = 0; k < lines; k++) {
        float zc = z1 - (z1 - z0) * (k + 0.8f) / (lines + 0.6f);
        float half = (u1 - u0) * 0.5f * (k == 0 ? 0.7f : 0.4f + 0.35f * hashToFloat(h >> (k * 3 + 2)));
        float uc = (u0 + u1) * 0.5f, th = k == 0 ? 0.03f : 0.02f;
        vec2 a = p0 + R * (uc - half) + f * 0.004f, b = p0 + R * (uc + half) + f * 0.004f;
        quad(g, *g.m, V3(a, zc - th), V3(b, zc - th), V3(b, zc + th), V3(a, zc + th), ink, M(MAT_STONE), V3(f, 0.f));
    }
}

// Square frustum aligned to R / f (obelisk shafts, pyramidions): r0 at z0 to r1 at z0 + h, no caps
void sqFrustum(G& g, vec2 c, vec2 R, float z0, float r0, float r1, float h, u32 col, u32 mat) {
    vec2 F(-R.y, R.x);
    const vec2 dirs[4] = {R, F, -R, -F};
    for (int k = 0; k < 4; k++) {
        vec2 n = dirs[k], t = rightOf(n);
        vec3 a = V3(c + n * r0 - t * r0, z0), b = V3(c + n * r0 + t * r0, z0), cc = V3(c + n * r1 + t * r1, z0 + h), d = V3(c + n * r1 - t * r1, z0 + h);
        if (r1 < 0.005f) tri3(g, a, b, V3(c, z0 + h), col, mat, normalize(V3(n, 0.f) * h + vec3(0, 0, r0)));
        else quad(g, *g.m, a, b, cc, d, col, mat, normalize(V3(n, 0.f) * h + vec3(0, 0, r0 - r1)));
    }
}

// Half disc (arch head) in the plane right/up facing n, centred on its base line at c
void halfDisc(G& g, vec3 c, vec3 right, vec3 n, float r, int seg, u32 col, u32 mat) {
    MeshData& m = *g.m;
    const vec3 up(0, 0, 1);
    u32 b0 = m.addVertex(c - g.org, n, right, vec2(0, 0), col, mat);
    for (int k = 0; k <= seg; k++) {
        float a = kPi * k / seg;
        m.addVertex(c + (right * cosf(a) + up * sinf(a)) * r - g.org, n, right, vec2(cosf(a), sinf(a)) * r, col, mat);
    }
    for (int k = 0; k < seg; k++) {
        vec3 fn = cross(m.verts[b0 + 1 + k].pos - m.verts[b0].pos, m.verts[b0 + 2 + k].pos - m.verts[b0].pos);
        if (dot(fn, n) > 0.f) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
        else m.tri(b0, b0 + 2 + k, b0 + 1 + k);
    }
}

// Round-headed opening (door or window) on a wall plane: panel from z0 to the springing line zs, half disc above; the
// panel is lifted `off` off the wall along n
void archedPanel(G& g, vec2 c, vec2 right, vec2 n, float hw, float z0, float zs, float off, u32 col, u32 mat, int seg) {
    vec2 p = c + n * off;
    quad(g, *g.m, V3(p - right * hw, z0), V3(p + right * hw, z0), V3(p + right * hw, zs), V3(p - right * hw, zs), col, mat, V3(n, 0.f));
    halfDisc(g, V3(p, zs), V3(right, 0.f), V3(n, 0.f), hw, seg, col, mat);
}

// Wrought-iron gate leaf from its hinge along dir: frame, rails, pickets with spear tips (detail), a scroll crest
void gateLeaf(G& g, vec2 hinge, vec2 dir, float w, float h, float z, bool closed) {
    u32 iron = rgb(0.06f, 0.06f, 0.06f), im = M(MAT_METAL_PAINTED);
    vec2 b = hinge + dir * w;
    if (!g.detail) {
        panel2(g, V3(hinge, z + 0.1f), V3(b, z + 0.1f), V3(b, z + h), V3(hinge, z + h), rgb(0.1f, 0.1f, 0.1f), im);
        return;
    }
    for (float zz : {0.12f, 0.95f, h - 0.1f}) beam(g, V3(hinge, z + zz), V3(b, z + zz), 0.05f, 0.06f, iron, im);
    beam(g, V3(hinge, z + 0.08f), V3(hinge, z + h + 0.1f), 0.07f, 0.07f, iron, im, V3(perp(dir), 0.f));
    beam(g, V3(b, z + 0.08f), V3(b, z + h), 0.06f, 0.06f, iron, im, V3(perp(dir), 0.f));
    int n = Max(2, (int)(w / 0.13f));
    for (int k = 1; k < n; k++) {
        vec2 p = hinge + dir * (w * k / n);
        boxY(g, V3(p, z + h * 0.5f + 0.05f), dir, vec3(0.013f, 0.013f, h * 0.5f + 0.05f), iron, im);
        boxY(g, V3(p, z + h + 0.14f), dir, vec3(0.03f, 0.013f, 0.04f), iron, im);   // spear tip
    }
    // scroll crest over the middle of the leaf
    vec2 mid = hinge + dir * (w * 0.5f);
    ring(g, V3(mid, z + h * 0.55f), V3(perp(dir), 0.f), 0.28f, 0.03f, 0.03f, 8, iron, im);
    if (closed) collide(g, V3(mid, z + h * 0.5f), dir, vec3(w * 0.5f, 0.06f, h * 0.5f));
}

// ------------------------------------------------------------------------------------------------ tombs
void tomb(G& g, const Plot& p, float z) {
    Rng r(p.h);
    const bool detail = g.detail;
    const vec2 f = p.f, R = rightOf(f);
    const vec3 F3(f, 0.f);
    auto P = [&](float u, float v) { return p.c + R * u + f * v; };
    auto bx = [&](float u, float v, float z0, float hu, float hv, float hz, u32 col, u32 mat) {
        boxY(g, V3(P(u, v), z0 + hz), R, vec3(hu, hv, hz), col, mat);
    };
    auto face = [&](float v, float u0, float u1, float z0, float z1, u32 col, u32 mat) {
        quad(g, *g.m, V3(P(u0, v), z0), V3(P(u1, v), z0), V3(P(u1, v), z1), V3(P(u0, v), z1), col, mat, F3);
    };
    auto letters = [&](const char* txt, float v, float zBase, float h, u32 col, u32 mat) {
        if (!detail) return;
        vec3 right = V3(-R, 0.f);   // reading left to right for someone facing the tomb
        plainLetters(g, txt, centredOrigin(txt, V3(P(0.f, v), zBase), right, h), right, vec3(0, 0, 1), h, col, mat, 0.f, 0.13f);
    };
    const float hw = p.w * 0.5f, hd = p.d * 0.5f;
    vec3 base = hashToFloat(p.h >> 3) < 0.1f ? kPastel[(p.h >> 9) & 3] : kWash[(p.h >> 12) % 6];
    base = base * r.range(0.86f, 1.f);
    const u32 wall = tint(base), plinth = tint(base * 0.72f), trim = tint(base * 1.05f), stucco = M(MAT_STUCCO), marbleM = M(MAT_MARBLE);
    const u32 marble = r.chance(0.8f) ? rgb(0.93f, 0.93f, 0.9f) : rgb(0.66f, 0.67f, 0.68f), marble2 = r.chance(0.7f) ? marble : rgb(0.88f, 0.87f, 0.84f);
    const u32 iron = rgb(0.07f, 0.07f, 0.07f), ironM = M(MAT_METAL_PAINTED);
    float top = z + 1.f;
    if (!detail) {   // far: one block per tomb (the small graves and the trees drop out)
        float hh = 0.f, sx = hw, sy = hd;
        switch (p.kind) {
            case TK_FAMILY: case TK_TEMPLE: hh = 2.9f + (p.frontage ? 0.5f : 0.f); break;
            case TK_STEPPED: hh = 2.7f; break;
            case TK_SARCOPHAGUS: hh = 1.2f; break;
            case TK_OBELISK: hh = 3.8f, sx = sy = 0.35f; break;
            case TK_SOCIETY: hh = 4.9f; break;
            case TK_MAUSOLEUM: hh = 5.2f; break;
            default: return;
        }
        boxY(g, V3(p.c, z - 0.1f + hh * 0.5f), R, vec3(sx, sy, hh * 0.5f), wall, stucco);
        return;
    }
    switch (p.kind) {
        case TK_FAMILY:
        case TK_TEMPLE:
        case TK_STEPPED: {
            float H = r.range(1.9f, 2.4f) + (p.frontage ? 0.5f : 0.f);
            const float zb = z + 0.25f;
            if (p.frontage || r.chance(0.6f)) {
                bx(0.f, 0.f, z - 0.15f, hw + 0.12f, hd + 0.12f, 0.2f, plinth, stucco);
                bx(0.f, 0.f, zb, hw, hd, H * 0.5f, wall, stucco);
            } else {
                bx(0.f, 0.f, z - 0.15f, hw, hd, (H + 0.4f) * 0.5f, wall, stucco);
                face(hd + 0.006f, -hw, hw, z, z + 0.32f, plinth, stucco);
            }
            float zt = zb + H;
            if (p.kind == TK_STEPPED) {
                bx(0.f, 0.f, zt, hw - 0.12f, hd - 0.12f, 0.13f, trim, stucco);
                bx(0.f, 0.f, zt + 0.26f, hw - 0.4f, hd - 0.4f, 0.12f, wall, stucco);
                top = zt + 0.5f;
                float tw = Min(hw - 0.3f, 0.6f);
                face(hd + 0.012f, -tw, tw, zb + 0.25f, zb + H - 0.25f, marble, marbleM);
                inscription(g, P(0.f, hd + 0.012f), -R, f, -tw, tw, zb + H * 0.35f, zb + H - 0.35f, 3, p.h);
                if (r.chance(0.75f)) {
                    bool ironCross = r.chance(0.5f);
                    latinCross(g, P(0.f, 0.f), f, top, 1.1f, ironCross ? 0.07f : 0.12f, ironCross ? iron : trim, ironCross ? ironM : stucco);
                    top += 1.1f;
                }
            } else {
                if (p.frontage || r.chance(0.6f)) {
                    bx(0.f, 0.f, zt, hw + 0.07f, hd + 0.07f, 0.07f, trim, stucco);   // cornice
                    zt += 0.14f;
                }
                float rise = r.range(0.45f, 0.7f) * (p.w / 2.4f);
                gableRoof(g, p.c, f, p.w + 0.14f, p.d + 0.14f, zt, rise, 0.f, tint(base * 0.93f), stucco, trim, stucco);
                top = zt + rise;
                float tw = Min(hw - 0.28f, 0.62f);
                if (p.kind == TK_TEMPLE) {
                    for (int s = -1; s <= 1; s += 2) {
                        bx(s * (hw - 0.14f), hd + 0.06f, zb, 0.14f, 0.07f, H * 0.5f, trim, stucco);   // pilasters
                        if (p.frontage) urn(g, V3(P(s * (hw - 0.05f), hd - 0.05f), zt), 0.55f, trim, stucco);
                    }
                    tw = Min(hw - 0.45f, 0.62f);
                }
                // two vaults, one above the other: a marble closure tablet each
                face(hd + 0.012f, -tw, tw, zb + 0.15f, zb + H * 0.48f, marble, marbleM);
                face(hd + 0.012f, -tw, tw, zb + H * 0.54f, zb + H - 0.18f, marble2, marbleM);
                inscription(g, P(0.f, hd + 0.012f), -R, f, -tw, tw, zb + 0.3f, zb + H * 0.46f, 2, p.h);
                inscription(g, P(0.f, hd + 0.012f), -R, f, -tw, tw, zb + H * 0.56f, zb + H - 0.3f, 2, p.h >> 5);
                if (p.frontage) letters(kSurnames[(p.h >> 7) % 24], hd + 0.08f, zt - 0.11f, 0.12f, rgb(0.2f, 0.2f, 0.2f), M(MAT_STONE));
                float o = r.f();
                if (o < 0.45f) {
                    bool ironCross = r.chance(0.5f);
                    latinCross(g, P(0.f, hd - 0.02f), f, top - 0.06f, 0.85f, ironCross ? 0.06f : 0.1f, ironCross ? iron : trim, ironCross ? ironM : stucco);
                    top += 0.8f;
                } else if (o < 0.62f && detail) urn(g, V3(P(0.f, hd - 0.1f), top - 0.05f), 0.5f, trim, stucco);
            }
            break;
        }
        case TK_SARCOPHAGUS: {
            bx(0.f, 0.f, z - 0.15f, hw, hd, 0.28f, plinth, M(MAT_STONE));
            bx(0.f, 0.f, z + 0.41f, hw - 0.12f, hd - 0.15f, 0.3f, marble, marbleM);
            gableRoof(g, p.c, f, p.w - 0.1f, p.d - 0.2f, z + 1.01f, 0.2f, 0.06f, marble, marbleM, marble, marbleM);
            top = z + 1.25f;
            face(hd - 0.15f + 0.012f, -0.35f, 0.35f, z + 0.52f, z + 0.9f, rgb(0.84f, 0.83f, 0.8f), marbleM);
            inscription(g, P(0.f, hd - 0.138f), -R, f, -0.35f, 0.35f, z + 0.55f, z + 0.88f, 2, p.h);
            if (r.chance(0.06f)) {   // a low iron railing round the plot
                vec2 c0 = P(-hw - 0.35f, -hd - 0.35f), c1 = P(hw + 0.35f, -hd - 0.35f), c2 = P(hw + 0.35f, hd + 0.35f), c3 = P(-hw - 0.35f, hd + 0.35f);
                ironRailing(g, c0, c1, z, 0.8f, 0.45f, iron, false, false);
                ironRailing(g, c1, c2, z, 0.8f, 0.45f, iron, false, false);
                ironRailing(g, c3, c0, z, 0.8f, 0.45f, iron, false, false);
                ironRailing(g, c2, lerp(c2, c3, 0.3f), z, 0.8f, 0.45f, iron, false, false);
                ironRailing(g, lerp(c2, c3, 0.7f), c3, z, 0.8f, 0.45f, iron, false, false);
            }
            break;
        }
        case TK_COPING: {
            const float ch = 0.34f, ct = 0.14f;
            u32 cc = r.chance(0.6f) ? wall : rgb(0.7f, 0.7f, 0.69f), cm = r.chance(0.6f) ? stucco : M(MAT_CONCRETE);
            wallRun(g, P(-hw, hd - ct * 0.5f), P(hw, hd - ct * 0.5f), z - 0.1f, z + ch, ct, cc, cm, false);
            wallRun(g, P(-hw, -hd + ct * 0.5f), P(hw, -hd + ct * 0.5f), z - 0.1f, z + ch, ct, cc, cm, false);
            wallRun(g, P(-hw + ct * 0.5f, -hd + ct), P(-hw + ct * 0.5f, hd - ct), z - 0.1f, z + ch, ct, cc, cm, false);
            wallRun(g, P(hw - ct * 0.5f, -hd + ct), P(hw - ct * 0.5f, hd - ct), z - 0.1f, z + ch, ct, cc, cm, false);
            bool shellFill = r.chance(0.6f);
            quad(g, *g.m, V3(P(-hw + ct, -hd + ct), z + ch - 0.06f), V3(P(hw - ct, -hd + ct), z + ch - 0.06f), V3(P(hw - ct, hd - ct), z + ch - 0.06f),
                 V3(P(-hw + ct, hd - ct), z + ch - 0.06f), shellFill ? rgb(0.9f, 0.88f, 0.83f) : rgb(0.45f, 0.6f, 0.34f),
                 shellFill ? M(MAT_SAND) : M(MAT_GRASS), vec3(0, 0, 1));
            // headstone at the far end, reading toward the aisle
            float sw = Min(hw - 0.12f, 0.5f);
            bx(0.f, -hd + 0.3f, z - 0.2f, sw, 0.07f, 0.52f, marble, marbleM);
            bx(0.f, -hd + 0.3f, z + 0.84f, sw * 0.75f, 0.07f, 0.06f, marble, marbleM);
            inscription(g, P(0.f, -hd + 0.372f), -R, f, -sw * 0.8f, sw * 0.8f, z + 0.35f, z + 0.78f, 3, p.h);
            top = z + 0.96f;
            break;
        }
        case TK_OBELISK: {
            u32 gr = r.chance(0.6f) ? tint(vec3(0.6f, 0.6f, 0.62f) * r.range(0.9f, 1.05f)) : marble, gm = M(MAT_STONE);
            bx(0.f, 0.f, z - 0.15f, 0.62f, 0.62f, 0.2f, plinth, gm);
            bx(0.f, 0.f, z + 0.25f, 0.46f, 0.46f, 0.2f, gr, gm);
            bx(0.f, 0.f, z + 0.65f, 0.36f, 0.36f, 0.3f, gr, gm);
            float sh = r.range(1.8f, 3.f);
            sqFrustum(g, p.c, R, z + 1.25f, 0.27f, 0.17f, sh, gr, gm);
            sqFrustum(g, p.c, R, z + 1.25f + sh, 0.17f, 0.f, 0.3f, gr, gm);
            face(0.36f + 0.01f, -0.26f, 0.26f, z + 0.75f, z + 1.17f, rgb(0.34f, 0.27f, 0.17f), M(MAT_METAL_BRUSHED));
            top = z + 1.55f + sh;
            break;
        }
        case TK_HEADSTONE: {
            int n = (p.h >> 5) & 1 ? 2 : 1;
            u32 sc = r.chance(0.55f) ? marble : tint(vec3(0.55f, 0.55f, 0.57f) * r.range(0.9f, 1.1f));
            u32 sm = r.chance(0.55f) ? marbleM : M(MAT_STONE);
            for (int k = 0; k < n; k++) {
                float u = n == 2 ? (k ? 0.6f : -0.6f) : 0.f;
                float sw = r.range(0.26f, 0.34f), shh = r.range(0.45f, 0.6f);
                bx(u, hd - 0.35f, z - 0.25f, sw, 0.07f, shh, sc, sm);
                bx(u, hd - 0.35f, z - 0.25f + shh * 2.f, sw * 0.7f, 0.07f, 0.05f, sc, sm);
                inscription(g, P(u, hd - 0.35f + 0.074f), -R, f, -sw * 0.8f, sw * 0.8f, z + 0.2f, z - 0.3f + shh * 2.f, 3, p.h >> (k * 4));
                top = Max(top, z - 0.15f + shh * 2.f);
            }
            if (r.chance(0.5f)) bx(0.f, -0.35f, z - 0.1f, hw * 0.75f, hd - 0.75f, 0.08f, rgb(0.8f, 0.8f, 0.78f), marbleM);   // ledger over the grave
            break;
        }
        case TK_TREE: {
            bool oak = r.chance(0.5f);
            if (g.props) prop(g, V3(p.c, z), r.f() * kTwoPi, oak ? r.range(0.95f, 1.2f) : r.range(0.95f, 1.15f), oak ? PROP_TREE_OAK : PROP_CYPRESS, (u8)r.irange(0, 3));
            return;
        }
        case TK_SOCIETY: {
            const float H = 3.3f;
            bx(0.f, 0.f, z - 0.15f, hw + 0.15f, hd + 0.15f, 0.25f, plinth, stucco);
            const float zb = z + 0.35f;
            bx(0.f, 0.f, zb, hw, hd, H * 0.5f, wall, stucco);
            // the vaults of the members: three tiers of four
            const int cols = 4, tiers = 3;
            float cw = (p.w - 0.7f) / cols, th = (H - 0.5f) / tiers;
            for (int t = 0; t < tiers; t++)
                for (int c = 0; c < cols; c++) {
                    float u0 = -hw + 0.35f + c * cw + 0.07f, u1 = u0 + cw - 0.14f;
                    float z0 = zb + 0.25f + t * th + 0.06f, z1 = z0 + th - 0.12f;
                    face(hd + 0.012f, u0, u1, z0, z1, (c + t + (int)(p.h & 3)) % 4 ? marble : rgb(0.8f, 0.8f, 0.77f), marbleM);
                    inscription(g, P(0.f, hd + 0.012f), -R, f, u0, u1, z0 + 0.1f, z1 - 0.08f, 1, p.h >> (c + t * 4));
                }
            const float zt = zb + H;
            bx(0.f, 0.f, zt, hw + 0.1f, hd + 0.1f, 0.1f, trim, stucco);
            // parapet with the society's name, a raised centre and a cross or an urn
            bx(0.f, hd - 0.2f, zt + 0.2f, hw, 0.2f, 0.36f, wall, stucco);
            bx(0.f, hd - 0.2f, zt + 0.92f, hw * 0.36f, 0.2f, 0.3f, wall, stucco);
            bx(0.f, hd - 0.2f, zt + 1.52f, hw * 0.4f, 0.24f, 0.05f, trim, stucco);
            letters(kSocieties[(p.h >> 6) % 6], hd + 0.012f, zt + 0.42f, 0.24f, rgb(0.18f, 0.18f, 0.18f), M(MAT_STONE));
            top = zt + 1.62f;
            if (r.chance(0.6f)) {
                latinCross(g, P(0.f, hd - 0.2f), f, top, 1.2f, 0.1f, trim, stucco);
                top += 1.2f;
            } else if (detail) urn(g, V3(P(0.f, hd - 0.2f), top), 0.8f, trim, stucco);
            break;
        }
        case TK_MAUSOLEUM: {
            const bool granite = r.chance(0.6f);
            vec3 sc = granite ? (r.chance(0.5f) ? vec3(0.56f, 0.56f, 0.58f) : vec3(0.62f, 0.52f, 0.48f)) : vec3(0.92f, 0.91f, 0.88f);
            const u32 st = tint(sc), stD = tint(sc * 0.8f), stM = granite ? M(MAT_STONE) : marbleM;
            const float H = r.range(3.1f, 3.7f), pd = 1.3f;
            const float vFront = hd - 0.7f, vCella = vFront - pd;   // portico front line, cella front
            bx(0.f, hd - 0.35f, z - 0.15f, hw * 0.72f, 0.35f, 0.16f, stD, stM);                     // step
            bx(0.f, (vFront - hd) * 0.5f, z - 0.15f, hw, (vFront + hd) * 0.5f, 0.3f, stD, stM);   // podium
            const float zb = z + 0.45f;
            bx(0.f, (vCella - hd + 0.2f) * 0.5f, zb, hw - 0.2f, (vCella + hd - 0.2f) * 0.5f, H * 0.5f, st, stM);
            for (int s = -1; s <= 1; s += 2) {
                vec2 cp = P(s * (hw - 0.55f), vFront - 0.4f);
                cyl(g, V3(cp, zb), 0.25f, 0.25f, 0.14f, detail ? 10 : 6, stD, stM, true);
                cyl(g, V3(cp, zb + 0.14f), 0.2f, 0.17f, H - 0.3f, detail ? 10 : 6, st, stM, false);
                boxY(g, V3(cp, zb + H - 0.08f), R, vec3(0.26f, 0.26f, 0.08f), st, stM);
            }
            float ze = zb + H;
            bx(0.f, (vFront + 0.05f - hd) * 0.5f, ze, hw + 0.05f, (vFront + 0.05f + hd) * 0.5f, 0.3f, st, stM);   // entablature
            letters(kSurnames[(p.h >> 7) % 24], vFront + 0.06f, ze + 0.14f, 0.3f, granite ? rgb(0.85f, 0.8f, 0.62f) : rgb(0.2f, 0.2f, 0.2f),
                    granite ? M(MAT_METAL_BRUSHED) : M(MAT_STONE));
            ze += 0.6f;
            if (r.chance(0.3f)) {   // domed
                float dr = Min(hw, hd) * 0.62f;
                vec2 dc = P(0.f, (vFront - hd) * 0.5f);
                boxY(g, V3(dc, ze + 0.1f), R, vec3(hw, (vFront + hd) * 0.5f, 0.1f), st, stM);
                cyl(g, V3(dc, ze + 0.2f), dr, dr, 0.55f, detail ? 16 : 8, st, stM, false);
                lathe(g, V3(dc, ze + 0.75f),
                      {vec2(dr + 0.08f, 0.f), vec2(dr * 0.92f, dr * 0.4f), vec2(dr * 0.7f, dr * 0.72f), vec2(dr * 0.38f, dr * 0.93f), vec2(0.f, dr)},
                      detail ? 16 : 8, stD, stM, false);
                top = ze + 0.75f + dr;
                latinCross(g, dc, f, top, 0.9f, 0.08f, rgb(0.75f, 0.62f, 0.32f), M(MAT_METAL_BRUSHED));
                top += 0.9f;
            } else {
                gableRoof(g, P(0.f, (vFront + 0.05f - hd) * 0.5f), f, p.w + 0.1f, vFront + 0.05f + hd, ze, 0.95f, 0.08f, st, stM, st, stM);
                top = ze + 0.95f;
                if (detail)
                    for (int s = -1; s <= 1; s += 2) urn(g, V3(P(s * (hw - 0.1f), vFront - 0.1f), ze), 0.6f, st, stM);
            }
            // bronze double door with a transom grille
            face(vCella + 0.012f, -0.7f, 0.7f, zb, zb + 2.35f, rgb(0.3f, 0.24f, 0.15f), M(MAT_METAL_BRUSHED));
            if (detail) {
                bx(0.f, vCella + 0.03f, zb, 0.02f, 0.02f, 1.1f, rgb(0.2f, 0.16f, 0.1f), M(MAT_METAL_BRUSHED));
                for (int k = -2; k <= 2; k++) bx(k * 0.25f, vCella + 0.03f, zb + 1.85f, 0.015f, 0.02f, 0.22f, rgb(0.2f, 0.16f, 0.1f), M(MAT_METAL_BRUSHED));
            }
            break;
        }
        default: break;
    }
    collide(g, V3(p.c, (z + top) * 0.5f), R, vec3(hw + 0.12f, hd + 0.12f, (top - z) * 0.5f));
    // flowers in a vase at the foot of some tombs; a red votive glass that burns at night
    if (detail && p.kind != TK_OBELISK && ((p.h >> 20) % 6u) == 0u) {
        vec2 fp = P(r.range(-hw * 0.5f, hw * 0.5f), hd + 0.3f);
        cyl(g, V3(fp, z), 0.07f, 0.09f, 0.28f, 6, rgb(0.8f, 0.8f, 0.78f), marbleM, true);
        const vec3 bloom[4] = {vec3(0.85f, 0.12f, 0.15f), vec3(1.f, 0.55f, 0.75f), vec3(1.f, 0.85f, 0.2f), vec3(0.97f, 0.97f, 0.95f)};
        lathe(g, V3(fp, z + 0.26f), {vec2(0.05f, 0.f), vec2(0.2f, 0.14f), vec2(0.16f, 0.28f), vec2(0.f, 0.32f)}, 6, tint(bloom[(p.h >> 24) & 3]),
              M(MAT_LEAVES), false);
    }
    if (detail && ((p.h >> 17) % 11u) == 0u) {
        vec2 cp = P(r.range(-hw * 0.4f, hw * 0.4f), hd + 0.16f);
        boxY(g, V3(cp, z + 0.07f), R, vec3(0.035f, 0.035f, 0.07f), rgb(1.f, 0.3f, 0.12f, 0.35f), emMat(EA_NIGHT), true);
    }
}

// ------------------------------------------------------------------------------------------------ sections (SK_CEMETERY 0-3)
// A quarter of the tomb rows with its aisles and cross alleys (crushed shell). p[0..1]: x0..x1, p[2]: yAv, p[3]: yEnd,
// p[4]: +1 north / -1 south; z: ground.
void genSection(const SiteElem& e, G& g) {
    const Section s = sectionOf(e);
    const float z = e.z;
    std::vector<Row> rows;
    std::vector<Aisle> aisles;
    rowsOf(s, rows, &aisles);
    const Segs sg = segsOf(s);
    const u32 shell = rgb(0.9f, 0.88f, 0.82f), shellD = rgb(0.82f, 0.8f, 0.74f), sand = M(MAT_SAND);
    auto strip = [&](vec2 a, vec2 b, float w, u32 col, float lift) {
        float L = length(b - a);
        int n = Max(1, (int)ceilf(L / 20.f));
        for (int k = 0; k < n; k++) pathStrip(g, {lerp(a, b, (float)k / n), lerp(a, b, (float)(k + 1) / n)}, w, lift, col, sand, z, false, false);
    };
    // aisles run from the cross avenue to the perimeter aisle (short of the caretaker's yard)
    for (const Aisle& a : aisles) {
        float y = s.yAv + s.ny * a.v;
        float xa = s.x0 - 1.f, xb = s.x1 + 1.f;
        if (y < kYardY1 + 1.f && xb > kYardX0 - 1.f) xb = kYardX0 - 1.f;
        strip(vec2(xa, y), vec2(xb, y), a.w, shell, 0.035f);
    }
    // cross alleys through the rows
    for (int si = 0; si + 1 < sg.n; si++) {
        float x = s.x0 + (si + 1) * (sg.len + sg.alley) - sg.alley * 0.5f;
        float y0 = s.yAv, y1 = s.yEnd;
        if (x > kYardX0 - 2.f && s.ny < 0) y1 = kYardY1 + 1.f;
        strip(vec2(x, y0), vec2(x, y1), sg.alley - 0.2f, shellD, 0.03f);
    }
    std::vector<Plot> plots;
    plotsOf(s, plots);
    for (const Plot& p : plots)
        if (g.owns(p.c)) tomb(g, p, z);
}

// ------------------------------------------------------------------------------------------------ wall vaults (SK_CEMETERY_WALL)
// One side of the perimeter: a: start, b: end of the street face; p[0..1]: inward normal; p[2]: gate centre (metres from
// a), p[3]: gate half width, p[4]: gate kind (0 none, 1 pedestrian, 2 main, 3 service); variant 1: the corners belong to
// the crossing ranges (no vaults there); z: ground.
void genWall(const SiteElem& e, G& g) {
    const bool detail = g.detail;
    const vec2 A = e.a, B = e.b, D = normalize(B - A), inw(e.p[0], e.p[1]);
    const float L = length(B - A), z = e.z, T = kRangeT, H = kVaultH;
    const float gs = e.p[2], ghw = e.p[3];
    const int gk = (int)e.p[4];
    const float pierW = gk == 2 ? 1.4f : (gk == 1 ? 1.f : 0.8f);
    const float tab0 = e.variant == 1 ? T : 0.f, tab1 = e.variant == 1 ? L - T : L;
    const u32 wash = rgb(0.95f, 0.94f, 0.9f), cap = rgb(0.84f, 0.83f, 0.8f), stain = rgb(0.66f, 0.64f, 0.6f), stucco = M(MAT_STUCCO);
    auto at = [&](float s, float t) { return A + D * s + inw * t; };
    float spans[2][2] = {{0.f, L}, {L, L}};
    int ns = 1;
    if (gk) {
        spans[0][1] = gs - ghw - pierW;
        spans[1][0] = gs + ghw + pierW;
        ns = 2;
    }
    for (int si = 0; si < ns; si++) {
        const float s0 = spans[si][0], s1 = spans[si][1];
        if (s1 - s0 < 0.1f) continue;
        int np = Max(1, (int)ceilf((s1 - s0) / 16.f));
        for (int k = 0; k < np; k++) {
            float a0 = s0 + (s1 - s0) * k / np, a1 = s0 + (s1 - s0) * (k + 1) / np, hl = (a1 - a0) * 0.5f;
            vec2 mid = at((a0 + a1) * 0.5f, T * 0.5f);
            if (!g.owns(mid)) continue;
            boxY(g, V3(mid, z - 0.5f + (H + 0.5f) * 0.5f), D, vec3(hl, T * 0.5f, (H + 0.5f) * 0.5f), wash, stucco);
            boxY(g, V3(mid, z + H + 0.06f), D, vec3(hl + 0.01f, T * 0.5f + 0.1f, 0.06f), cap, stucco, true);
            collide(g, V3(mid, z + H * 0.5f), D, vec3(hl, T * 0.5f, H * 0.5f + 0.1f));
            if (!detail) continue;
            // rain-stained foot on the street face, ledges between the tiers inside
            vec2 o0 = at(a0, -0.012f), o1 = at(a1, -0.012f);
            quad(g, *g.m, V3(o0, z - 0.1f), V3(o1, z - 0.1f), V3(o1, z + 0.45f), V3(o0, z + 0.45f), stain, stucco, V3(-inw, 0.f));
            for (int t = 1; t < 3; t++) boxY(g, V3(at((a0 + a1) * 0.5f, T + 0.04f), z + 0.3f + t * 0.95f), D, vec3(hl, 0.045f, 0.035f), cap, stucco);
        }
        // the vaults: three tiers of closure tablets facing in, a column every 0.95 m
        if (!detail) continue;
        float c0 = Max(s0, tab0) + 0.25f, c1 = Min(s1, tab1) - 0.25f;
        int ncol = (int)floorf((c1 - c0) / 0.95f);
        for (int c = 0; c < ncol; c++) {
            float sc = c0 + (c1 - c0) * (c + 0.5f) / ncol;
            vec2 fp = at(sc, T + 0.012f);
            if (!g.owns(fp)) continue;
            u32 h = hash32(e.seed * 977u + (u32)c * 7919u + (u32)si * 104729u);
            for (int t = 0; t < 3; t++) {
                u32 ht = hash32(h + (u32)t * 0x9E37u);
                float zz0 = z + 0.36f + t * 0.95f, zz1 = zz0 + 0.8f;
                float rr = hashToFloat(ht);
                u32 col, mat = M(MAT_MARBLE);
                if (rr < 0.58f) col = rgb(0.93f, 0.93f, 0.9f);
                else if (rr < 0.74f) col = rgb(0.7f, 0.71f, 0.72f);
                else if (rr < 0.96f) {   // sealed with plaster
                    col = tint(vec3(0.9f, 0.88f, 0.83f) * (0.8f + 0.15f * hashToFloat(ht >> 5)));
                    mat = stucco;
                } else {   // an open, empty vault
                    col = rgb(0.05f);
                    mat = M(MAT_CONCRETE);
                }
                quad(g, *g.m, V3(fp - D * 0.39f, zz0), V3(fp + D * 0.39f, zz0), V3(fp + D * 0.39f, zz1), V3(fp - D * 0.39f, zz1), col, mat, V3(inw, 0.f));
                if (rr < 0.74f) inscription(g, fp, D, inw, -0.3f, 0.3f, zz0 + 0.12f, zz1 - 0.1f, 2, ht >> 3);
                if (((ht >> 12) % 17u) == 0u) {   // a vase of flowers on the ledge
                    vec2 vp = at(sc + 0.25f, T + 0.1f);
                    cyl(g, V3(vp, zz0 - 0.02f), 0.04f, 0.05f, 0.16f, 5, rgb(0.8f, 0.8f, 0.78f), M(MAT_MARBLE), true);
                    lathe(g, V3(vp, zz0 + 0.13f), {vec2(0.03f, 0.f), vec2(0.11f, 0.08f), vec2(0.f, 0.17f)}, 5,
                          (ht >> 20) & 1 ? rgb(0.9f, 0.15f, 0.2f) : rgb(1.f, 0.9f, 0.3f), M(MAT_LEAVES), false);
                }
            }
        }
    }
    if (!gk) return;
    // ---------------------------------------------------------------- the gate: piers, iron gates, (main) arch and lanterns
    const float ph = gk == 2 ? 4.4f : (gk == 1 ? 3.8f : 3.5f);
    const u32 iron = rgb(0.06f, 0.06f, 0.06f), ironM = M(MAT_METAL_PAINTED);
    vec2 gmid = at(gs, T * 0.5f);
    if (g.owns(gmid)) {
        // paving through the gateway
        std::vector<vec2> pv = {at(gs - ghw, -0.02f), at(gs + ghw, -0.02f), at(gs + ghw, T + 0.4f), at(gs - ghw, T + 0.4f)};
        if (D.x * inw.y - D.y * inw.x < 0.f) std::reverse(pv.begin(), pv.end());   // counter-clockwise from above
        size_t v0 = g.m->verts.size();
        polyFlat(g, *g.m, pv, z + 0.045f, paverTint(), M(MAT_PAVERS));
        paverUV(g, *g.m, v0);
    }
    for (int sd = -1; sd <= 1; sd += 2) {
        float ps = gs + sd * (ghw + pierW * 0.5f);
        vec2 pc = at(ps, T * 0.5f);
        if (!g.owns(pc)) continue;
        boxY(g, V3(pc, z - 0.5f + (ph + 0.5f) * 0.5f), D, vec3(pierW * 0.5f, T * 0.5f + 0.12f, (ph + 0.5f) * 0.5f), wash, stucco);
        boxY(g, V3(pc, z + ph + 0.08f), D, vec3(pierW * 0.5f + 0.1f, T * 0.5f + 0.22f, 0.08f), cap, stucco, true);
        collide(g, V3(pc, z + ph * 0.5f), D, vec3(pierW * 0.5f, T * 0.5f + 0.12f, ph * 0.5f));
        if (gk == 2) {
            urn(g, V3(pc, z + ph + 0.16f), 1.15f, cap, stucco);
            // lantern on a bracket on the street face
            vec2 lp = at(ps, -0.45f);
            if (detail) beam(g, V3(at(ps, -0.12f), z + 2.9f), V3(lp, z + 2.9f), 0.05f, 0.05f, iron, ironM);
            boxY(g, V3(lp, z + 2.72f), D, vec3(0.13f, 0.13f, 0.2f), rgb(1.f, 0.8f, 0.5f, 0.5f), emMat(EA_NIGHT), true);
            lathe(g, V3(lp, z + 2.92f), {vec2(0.2f, 0.f), vec2(0.04f, 0.14f), vec2(0.f, 0.2f)}, 4, iron, ironM, false, kPi * 0.25f);
            light(g, V3(lp, z + 2.7f), vec3(1.f, 0.78f, 0.5f) * 1400.f, 11.f, 1);
            if (detail && sd < 0)
                plainLetters(g, "1868", centredOrigin("1868", V3(at(ps, -0.13f), z + 1.6f), V3(rightOf(inw), 0.f), 0.18f), V3(rightOf(inw), 0.f),
                             vec3(0, 0, 1), 0.18f, rgb(0.25f, 0.25f, 0.25f), M(MAT_STONE), 0.015f, 0.12f);
        } else if (gk == 1) {
            lathe(g, V3(pc, z + ph + 0.16f), {vec2(0.12f, 0.f), vec2(0.26f, 0.12f), vec2(0.3f, 0.3f), vec2(0.22f, 0.5f), vec2(0.f, 0.58f)}, detail ? 10 : 6,
                  cap, stucco, false);
        }
    }
    // iron gates: open inward against the gateway sides (main, pedestrian), closed across the service gate
    vec2 gateOwner = at(gs, 0.3f);
    if (g.owns(gateOwner)) {
        const float gh = gk == 2 ? 2.9f : 2.2f, lw = ghw;
        for (int sd = -1; sd <= 1; sd += 2) {
            vec2 hinge = at(gs + sd * ghw, 0.3f);
            vec2 shut = D * (float)(-sd);
            vec2 dir = gk == 3 ? shut : normalize(shut * 0.12f + inw * 0.99f);
            gateLeaf(g, hinge, dir, lw - 0.02f, gh, z, gk == 3);
        }
        if (gk == 2) {
            // the arch over the gateway on the street face: a spring bar with the name on it, the iron arch above
            float zs = z + 4.f;
            vec2 l = at(gs - ghw, -0.05f), rr = at(gs + ghw, -0.05f);
            beam(g, V3(l, zs), V3(rr, zs), 0.08f, 0.1f, iron, ironM);
            landmark_mesh::arch(g, V3(l, zs), V3(rr, zs), 1.5f, 0.09f, 0.08f, iron, ironM, V3(-inw, 0.f), detail ? 12 : 6);
            if (detail) {
                landmark_mesh::arch(g, V3(at(gs - ghw + 0.35f, -0.05f), zs), V3(at(gs + ghw - 0.35f, -0.05f), zs), 1.1f, 0.05f, 0.05f, iron, ironM, V3(-inw, 0.f), 10);
                for (int k = -2; k <= 2; k++) {
                    float u = k * 1.3f, hh = 1.5f * sqrtf(Max(0.f, 1.f - (u / ghw) * (u / ghw)));
                    if (k == 0) continue;
                    beam(g, V3(at(gs + u, -0.05f), zs), V3(at(gs + u, -0.05f), zs + hh), 0.03f, 0.03f, iron, ironM, V3(D, 0.f));
                }
            }
            const vec3 right = V3(rightOf(inw), 0.f);   // reading left to right from the street
            plainLetters(g, "SANTA MAREA", centredOrigin("SANTA MAREA", V3(at(gs, -0.08f), zs + 0.1f), right, 0.46f), right, vec3(0, 0, 1), 0.46f, iron, ironM,
                         0.04f, 0.14f);
            latinCross(g, at(gs, -0.05f), -inw, zs + 1.52f, 1.f, 0.08f, iron, ironM);
        }
    }
}

// ------------------------------------------------------------------------------------------------ grounds (SK_CEMETERY 4)
// The lawn inside the vault ranges, the perimeter aisle, the main and cross avenues, the rond-point with its obelisk,
// the Italian cypress rows, lamps and benches, the chapel forecourt and the caretaker's yard. z: ground.
void genGrounds(const SiteElem& e, G& g) {
    const bool detail = g.detail;
    const float z = e.z;
    const u32 grass = rgb(0.5f, 0.64f, 0.36f), shell = rgb(0.9f, 0.88f, 0.82f), brick = rgb(0.62f, 0.36f, 0.28f), sand = M(MAT_SAND);
    const vec2 X(1, 0), Y(0, 1), oc(kAvX, kAvY);
    drapeRect(g, vec2((kIX0 + kIX1) * 0.5f, (kIY0 + kIY1) * 0.5f), X, (kIX1 - kIX0) * 0.5f, (kIY1 - kIY0) * 0.5f, 0.03f, grass, M(MAT_GRASS), detail ? 8.f : 24.f, z);
    auto strip = [&](vec2 a, vec2 b, float w, u32 col, u32 mat, float lift) {
        float L = length(b - a);
        int n = Max(1, (int)ceilf(L / 20.f));
        for (int k = 0; k < n; k++) pathStrip(g, {lerp(a, b, (float)k / n), lerp(a, b, (float)(k + 1) / n)}, w, lift, col, mat, z, false, false);
    };
    auto edging = [&](vec2 a, vec2 b) {   // brick edging along an avenue, in <= 16 m pieces
        if (!detail) return;
        float L = length(b - a);
        int n = Max(1, (int)ceilf(L / 16.f));
        vec2 d = (b - a) / L;
        for (int k = 0; k < n; k++) {
            vec2 m = a + d * (L * (k + 0.5f) / n);
            if (g.owns(m)) boxY(g, V3(m, z + 0.03f), d, vec3(L * 0.5f / n, 0.1f, 0.05f), brick, M(MAT_BRICK));
        }
    };
    // perimeter aisle
    const float px0 = kIX0 + kPeriW * 0.5f, px1 = kIX1 - kPeriW * 0.5f, py0 = kIY0 + kPeriW * 0.5f, py1 = kIY1 - kPeriW * 0.5f;
    strip(vec2(px0, kIY0), vec2(px0, kIY1), kPeriW, shell, sand, 0.035f);
    strip(vec2(px1, kIY0), vec2(px1, kIY1), kPeriW, shell, sand, 0.035f);
    strip(vec2(kIX0 + kPeriW, py0), vec2(kIX1 - kPeriW, py0), kPeriW, shell, sand, 0.035f);
    strip(vec2(kIX0 + kPeriW, py1), vec2(kIX1 - kPeriW, py1), kPeriW, shell, sand, 0.035f);
    // main avenue from the gate to the chapel forecourt, cross avenue from gate to gate, the rond-point
    strip(vec2(kIX0, kAvY), vec2(kPlazaX0, kAvY), kAvHW * 2.f, shell, sand, 0.05f);
    strip(vec2(kAvX, kIY0), vec2(kAvX, kIY1), kCrossHW * 2.f, shell, sand, 0.045f);
    for (int s = -1; s <= 1; s += 2) {
        edging(vec2(kIX0 + kPeriW, kAvY + s * kAvHW), vec2(kAvX - kRondR + 0.4f, kAvY + s * kAvHW));
        edging(vec2(kAvX + kRondR - 0.4f, kAvY + s * kAvHW), vec2(kPlazaX0, kAvY + s * kAvHW));
    }
    if (g.owns(oc)) {
        polyFlat(g, *g.m, circleFP(oc, kRondR, detail ? 40 : 16), z + 0.055f, shell, sand);
        if (detail) ring(g, vec3(oc, z + 0.04f), vec3(0, 0, 1), kRondR, 0.22f, 0.1f, 32, brick, M(MAT_BRICK));
        // the obelisk: stepped granite base, a die with bronze plaques, a tapering shaft
        const u32 gr = rgb(0.6f, 0.6f, 0.62f), grD = rgb(0.5f, 0.5f, 0.52f), gm = M(MAT_STONE);
        boxY(g, V3(oc, z + 0.1f), X, vec3(1.7f, 1.7f, 0.2f), grD, gm);
        boxY(g, V3(oc, z + 0.45f), X, vec3(1.35f, 1.35f, 0.15f), gr, gm);
        boxY(g, V3(oc, z + 0.75f), X, vec3(1.05f, 1.05f, 0.15f), grD, gm);
        boxY(g, V3(oc, z + 1.75f), X, vec3(0.8f, 0.8f, 0.85f), gr, gm);
        boxY(g, V3(oc, z + 2.68f), X, vec3(0.9f, 0.9f, 0.08f), grD, gm);
        sqFrustum(g, oc, X, z + 2.76f, 0.62f, 0.38f, 8.5f, gr, gm);
        sqFrustum(g, oc, X, z + 11.26f, 0.38f, 0.f, 0.7f, gr, gm);
        for (int k = 0; k < 4; k++) {
            vec2 n = rotate(X, kHalfPi * k), t = rightOf(n);
            vec2 pc = oc + n * 0.812f;
            quad(g, *g.m, V3(pc - t * 0.5f, z + 1.2f), V3(pc + t * 0.5f, z + 1.2f), V3(pc + t * 0.5f, z + 2.3f), V3(pc - t * 0.5f, z + 2.3f),
                 rgb(0.33f, 0.26f, 0.16f), M(MAT_METAL_BRUSHED), V3(n, 0.f));
            inscription(g, pc, -t, n, -0.4f, 0.4f, z + 1.3f, z + 2.2f, 5, 0x0BE1u + (u32)k);
            // uplights washing the shaft at night
            light(g, V3(oc + n * 2.8f, z + 0.3f), vec3(1.f, 0.9f, 0.75f) * 5000.f, 16.f, 1, normalize(vec3(-n, 3.f)), 0.1f);
        }
        collide(g, V3(oc, z + 6.f), X, vec3(1.7f, 1.7f, 6.f));
        if (detail) {   // low iron fence round the base
            for (int k = 0; k < 12; k++) {
                float a0 = kTwoPi * k / 12, a1 = kTwoPi * (k + 1) / 12;
                vec2 p0 = oc + vec2(cosf(a0), sinf(a0)) * 2.4f, p1 = oc + vec2(cosf(a1), sinf(a1)) * 2.4f;
                ironRailing(g, p0, p1, z, 0.9f, 0.32f, rgb(0.06f), true, false);
            }
            collide(g, V3(oc, z + 0.45f), X, vec3(2.3f, 2.3f, 0.45f));
        }
    }
    // Italian cypress along the avenue verges, at the rond-point and inside the side gates
    for (int s = -1; s <= 1; s += 2) {
        float y = kAvY + s * (kAvHW + kVerge * 0.5f);
        for (float x = kIX0 + kPeriW + 5.f; x < kAvX - kRondR - 3.f; x += 11.f)
            if (g.owns(vec2(x, y))) italianCypress(g, vec2(x, y), z, 8.5f + 1.5f * hashToFloat(hash32((u32)(x * 7.f) + (u32)(s + 5))), 0.85f);
        for (float x = kAvX + kRondR + 4.f; x < kPlazaX0 - 2.f; x += 11.f)
            if (g.owns(vec2(x, y))) italianCypress(g, vec2(x, y), z, 8.5f + 1.5f * hashToFloat(hash32((u32)(x * 7.f) + (u32)(s + 9))), 0.85f);
    }
    for (int k = 0; k < 4; k++) {
        vec2 d(k & 1 ? 1.f : -1.f, k & 2 ? 1.f : -1.f);
        vec2 p = oc + normalize(d) * (kRondR + 1.6f);
        if (g.owns(p)) italianCypress(g, p, z, 10.5f, 1.f);
        // benches round the obelisk, facing it
        vec2 bp = oc + normalize(d) * 9.3f;
        if (g.owns(bp)) prop(g, V3(bp, z + 0.05f), yawFacing(normalize(oc - bp)), 1.f, PROP_BENCH);
    }
    for (int s = -1; s <= 1; s += 2) {
        for (int k = -1; k <= 1; k += 2) {
            vec2 p(kAvX + k * (kCrossHW + 1.4f), s > 0 ? kIY1 - kPeriW - 1.4f : kIY0 + kPeriW + 1.4f);
            if (g.owns(p)) italianCypress(g, p, z, 8.f, 0.8f);
        }
    }
    // lamps along the avenue (alternating sides), round the rond-point and at the forecourt
    int li = 0;
    for (float x = kIX0 + kPeriW + 10.5f; x < kPlazaX0 - 4.f; x += 11.f, li++) {
        if (fabsf(x - kAvX) < kRondR + 3.f) continue;
        vec2 p(x, kAvY + ((li & 1) ? 1.f : -1.f) * (kAvHW + kVerge * 0.5f));
        if (g.owns(p)) lanternPost(g, p, z, 3.6f, vec3(1.f, 0.82f, 0.56f));
    }
    for (int k = 0; k < 4; k++) {
        vec2 p = oc + rotate(X, kHalfPi * k + kPi * 0.25f) * (kRondR - 0.5f);
        if (g.owns(p)) lanternPost(g, p, z, 3.6f, vec3(1.f, 0.82f, 0.56f));
    }
    // chapel forecourt: pavers, benches along its sides, a pair of cypress and lamps by the door
    const float fx1 = kChapelX - kChapelHL;
    vec2 fc((kPlazaX0 + fx1) * 0.5f, kAvY);
    if (g.owns(fc)) {
        std::vector<vec2> pv = rectPoly(fc, X, (fx1 - kPlazaX0) * 0.5f + 0.3f, kPlazaHW);
        size_t v0 = g.m->verts.size();
        polyFlat(g, *g.m, pv, z + 0.06f, paverTint(), M(MAT_PAVERS));
        paverUV(g, *g.m, v0);
        for (int s = -1; s <= 1; s += 2) {
            vec2 bp(fc.x - 1.f, kAvY + s * (kPlazaHW - 1.f));
            prop(g, V3(bp, z + 0.06f), yawFacing(vec2(0.f, -(float)s)), 1.f, PROP_BENCH);
            italianCypress(g, vec2(fx1 - 1.2f, kAvY + s * 4.2f), z, 9.f, 0.85f);
            lanternPost(g, vec2(kPlazaX0 + 1.f, kAvY + s * (kPlazaHW - 0.6f)), z, 3.6f, vec3(1.f, 0.82f, 0.56f));
        }
    }
    // caretaker's yard: tool shed, spare slabs, wheelbarrow, water barrel
    vec2 sc(kIX1 - kPeriW - 4.f, kIY0 + kPeriW + 6.f);
    if (g.owns(sc)) {
        const vec2 fr(-1, 0);
        std::vector<vec2> fp = rectPoly(sc, Y, 3.f, 2.2f);
        prism(g, fp, z - 0.2f, z + 2.6f, rgb(0.6f, 0.66f, 0.62f), M(MAT_WOOD_SIDING), rgb(0.5f), M(MAT_ROOF_METAL), false);
        gableRoof(g, sc, Y, 4.4f, 6.f, z + 2.6f, 0.9f, 0.3f, rgb(0.62f, 0.62f, 0.6f), M(MAT_CORRUGATED), rgb(0.6f, 0.66f, 0.62f), M(MAT_WOOD_SIDING), true);
        quad(g, *g.m, V3(sc + fr * 2.21f - Y * 0.6f, z), V3(sc + fr * 2.21f + Y * 0.6f, z), V3(sc + fr * 2.21f + Y * 0.6f, z + 2.1f),
             V3(sc + fr * 2.21f - Y * 0.6f, z + 2.1f), rgb(0.35f, 0.3f, 0.25f), M(MAT_WOOD), V3(fr, 0.f));
        collide(g, V3(sc, z + 1.5f), Y, vec3(3.f, 2.2f, 1.5f));
        if (detail) {
            for (int k = 0; k < 4; k++)
                boxY(g, V3(sc + fr * 3.4f + Y * 3.2f, z + 0.05f + k * 0.08f), Y, vec3(0.45f, 0.3f - k * 0.02f, 0.04f), rgb(0.92f - k * 0.04f), M(MAT_MARBLE), true);
            cyl(g, V3(sc + fr * 2.8f - Y * 2.6f, z), 0.3f, 0.3f, 0.9f, 10, rgb(0.25f, 0.35f, 0.5f), M(MAT_PLASTIC), true);
            vec2 wb = sc + fr * 3.8f - Y * 0.8f;
            boxY(g, V3(wb, z + 0.55f), X, vec3(0.45f, 0.3f, 0.15f), rgb(0.3f, 0.45f, 0.35f), M(MAT_METAL_PAINTED), true);
            cyl(g, V3(wb + X * 0.55f, z + 0.18f), 0.18f, 0.18f, 0.08f, 8, rgb(0.1f), M(MAT_RUBBER), true);
            lanternPost(g, sc + fr * 2.6f + Y * 1.4f, z, 3.2f, vec3(1.f, 0.85f, 0.6f));
        }
    }
}

// ------------------------------------------------------------------------------------------------ chapel (SK_CHAPEL)
// variant 0: the cemetery's mission chapel (stucco, clay tile roof, curved bell gable with two bells, oculus, arched door
// and windows, buttresses, sacristy); 1: white clapboard church with a steeple; 2: red brick church with a square tower
// (churchyards). c: centre; ax: front (the door side); hx: half width; hy: half length; z: ground.
void genChapel(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    const bool detail = g.detail;
    const vec2 f = e.ax, R = rightOf(f);
    const float hw = e.hx, hl = e.hy, z = e.z;
    auto P = [&](float u, float v) { return e.c + R * u + f * v; };
    auto bx = [&](float u, float v, float z0, float hu, float hv, float hz, u32 col, u32 mat) {
        boxY(g, V3(P(u, v), z0 + hz), R, vec3(hu, hv, hz), col, mat);
    };
    const u32 iron = rgb(0.06f, 0.06f, 0.06f), ironM = M(MAT_METAL_PAINTED), glassNight = rgb(1.f, 0.78f, 0.5f, 0.05f);
    const vec3 F3(f, 0.f), up(0, 0, 1);
    const int v = e.variant;
    const u32 wallC = v == 0 ? rgb(0.96f, 0.92f, 0.8f) : (v == 1 ? rgb(0.97f, 0.97f, 0.95f) : rgb(0.62f, 0.3f, 0.24f));
    const u32 wallM = v == 0 ? M(MAT_STUCCO) : (v == 1 ? M(MAT_WOOD_SIDING) : M(MAT_BRICK));
    const u32 trimC = v == 0 ? rgb(0.88f, 0.84f, 0.74f) : (v == 1 ? rgb(0.95f) : rgb(0.85f, 0.82f, 0.75f));
    const u32 trimM = v == 2 ? M(MAT_STONE) : (v == 1 ? M(MAT_WOOD) : M(MAT_STUCCO));
    const u32 roofC = v == 0 ? rgb(0.78f, 0.45f, 0.32f) : (v == 1 ? rgb(0.3f, 0.32f, 0.35f) : rgb(0.36f, 0.38f, 0.4f));
    const u32 roofM = v == 0 ? M(MAT_ROOF_TILE) : (v == 1 ? M(MAT_ROOF_SHINGLE) : M(MAT_ROOF_SHINGLE));
    const float wallH = v == 0 ? 6.8f : (v == 1 ? 5.6f : 7.2f), rise = v == 0 ? 3.f : hw * (v == 1 ? 0.9f : 1.05f);
    const float zf = z + (v == 1 ? 0.7f : 0.2f);   // floor (the clapboard church stands on piers)
    // base and nave
    bx(0.f, 0.f, z - 0.4f, hw + 0.2f, hl + 0.2f, (zf - z + 0.4f) * 0.5f, v == 1 ? rgb(0.55f, 0.52f, 0.5f) : rgb(0.6f, 0.6f, 0.58f), M(MAT_STONE));
    bx(0.f, 0.f, zf, hw, hl, wallH * 0.5f, wallC, wallM);
    const float zt = zf + wallH;
    if (v == 0) gableRoof(g, e.c - f * 0.225f, f, hw * 2.f, hl * 2.f - 0.45f, zt, rise, 0.45f, roofC, roofM, wallC, wallM, true);   // flush with the bell gable
    else gableRoof(g, e.c, f, hw * 2.f, hl * 2.f, zt, rise, 0.45f, roofC, roofM, wallC, wallM, true);
    float top = zt + rise;
    // door: steps, round-headed double door (pointed frame for the brick church) in a surround
    const float dw = 1.1f, dz1 = zf + 2.9f;
    const u32 stepC = rgb(0.62f, 0.61f, 0.58f);
    const int nsteps = v == 1 ? 3 : 1;
    for (int k = 0; k < nsteps; k++) {   // the lowest step reaches furthest
        float topz = z + (zf - z) * (k + 1) / nsteps, depth = (nsteps - k) * 0.4f + 0.1f;
        bx(0.f, hl + depth * 0.5f, z - 0.3f, 1.8f, depth * 0.5f, (topz - z + 0.3f) * 0.5f, stepC, M(MAT_STONE));
    }
    archedPanel(g, P(0.f, hl), R, f, dw, zf, dz1, 0.02f, rgb(0.32f, 0.2f, 0.12f), M(MAT_WOOD), detail ? 10 : 5);
    if (detail) {
        archedPanel(g, P(0.f, hl), R, f, dw + 0.22f, zf, dz1, 0.01f, trimC, trimM, 10);
        bx(0.f, hl + 0.03f, zf, 0.02f, 0.02f, (dz1 + dw - zf) * 0.5f - 0.05f, rgb(0.2f, 0.12f, 0.07f), M(MAT_WOOD));
        // lantern beside the door
        vec2 lp = P(dw + 0.75f, hl + 0.25f);
        boxY(g, V3(lp, zf + 2.6f), R, vec3(0.12f, 0.12f, 0.18f), rgb(1.f, 0.8f, 0.5f, 0.5f), emMat(EA_NIGHT), true);
        beam(g, V3(P(dw + 0.75f, hl), zf + 2.85f), V3(lp, zf + 2.85f), 0.04f, 0.04f, iron, ironM);
    }
    light(g, V3(P(dw + 0.75f, hl + 0.4f), zf + 2.6f), vec3(1.f, 0.78f, 0.52f) * 1600.f, 10.f, 1);
    // side windows: tall round-headed (pointed on the brick church), warm glass at night
    const int nwin = hl > 8.f ? 3 : 2;
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < nwin; k++) {
            float vv = -hl + (2.f * hl) * (k + 0.5f) / nwin;
            vec2 wc = P(s * hw, vv), n = R * (float)s;
            archedPanel(g, wc, f, n, 0.5f, zf + 2.2f, zf + wallH - 1.9f, 0.02f, glassNight, emMat(EA_NIGHT), detail ? 8 : 4);
            if (detail) archedPanel(g, wc, f, n, 0.64f, zf + 2.05f, zf + wallH - 1.9f, 0.01f, trimC, trimM, 8);
            // buttresses between the windows (mission and brick)
            if (v != 1 && k < nwin - 1) {
                float vb = -hl + (2.f * hl) * (k + 1.f) / nwin;
                bx(s * (hw + 0.35f), vb, z - 0.2f, 0.35f, 0.4f, (wallH * 0.75f + zf - z + 0.2f) * 0.5f, wallC, wallM);
                bx(s * (hw + 0.2f), vb, zf + wallH * 0.75f, 0.2f, 0.34f, 0.5f, wallC, wallM);
            }
        }
    collide(g, V3(e.c, (z + zt) * 0.5f), R, vec3(hw + 0.2f, hl + 0.2f, (zt - z) * 0.5f));
    if (v == 0) {
        // ---- curved bell gable (espadana) on the front: stepped shoulders, two bell openings, cornice and cross
        const float fz = zt, fd = 0.4f, fvv = hl - fd;
        bx(0.f, fvv, fz - 0.2f, hw + 0.2f, fd, 1.7f, wallC, wallM);   // to fz + 3.2 (covers the roof's front gable)
        for (int s = -1; s <= 1; s += 2) {
            bx(s * 3.35f, fvv, fz + 3.2f, 1.05f, fd, 0.4f, wallC, wallM);
            bx(s * 2.85f, fvv, fz + 4.f, 0.55f, fd, 0.35f, wallC, wallM);
            bx(s * 3.35f, fvv, fz + 4.f, 1.1f, fd + 0.05f, 0.05f, trimC, trimM);
            if (detail) disc(g, V3(P(s * 4.25f, hl + 0.01f), fz + 3.55f), F3, 0.3f, 10, trimC, trimM);
        }
        // central belfry: sill, three piers round two openings, arched heads, top block
        bx(0.f, fvv, fz + 3.2f, 2.35f, fd, 0.25f, wallC, wallM);
        for (int k = -1; k <= 1; k++) bx(k * 2.05f, fvv, fz + 3.7f, k == 0 ? 0.25f : 0.3f, fd, 0.95f, wallC, wallM);
        bx(0.f, fvv, fz + 5.6f, 2.35f, fd, 0.65f, wallC, wallM);
        for (int s = -1; s <= 1; s += 2) {
            // arch heads inside the openings (the top block's underside), bells hanging in them
            vec2 oc = P(s * 1.03f, hl + 0.01f);
            if (detail) halfDisc(g, V3(oc, fz + 5.6f - 0.78f), V3(R, 0.f), F3, 0.78f, 8, wallC, wallM);
            vec2 bc = P(s * 1.03f, fvv);
            lathe(g, V3(bc, fz + 4.35f), {vec2(0.42f, 0.f), vec2(0.38f, 0.1f), vec2(0.27f, 0.45f), vec2(0.22f, 0.7f), vec2(0.f, 0.78f)}, detail ? 12 : 6,
                  rgb(0.45f, 0.36f, 0.2f), M(MAT_METAL_BRUSHED), false);
            if (detail) beam(g, V3(P(s * 1.03f - 0.7f, fvv), fz + 5.2f), V3(P(s * 1.03f + 0.7f, fvv), fz + 5.2f), 0.08f, 0.08f, rgb(0.3f, 0.22f, 0.15f), M(MAT_WOOD));
        }
        bx(0.f, fvv, fz + 6.9f, 2.5f, fd + 0.08f, 0.07f, trimC, trimM);
        bx(0.f, fvv, fz + 7.04f, 1.f, fd, 0.35f, wallC, wallM);
        latinCross(g, P(0.f, fvv), f, fz + 7.74f, 1.5f, 0.1f, iron, ironM);
        top = fz + 9.3f;
        collide(g, V3(P(0.f, fvv), fz + 3.5f), R, vec3(hw + 0.2f, fd, 3.7f));
        // oculus over the door
        disc(g, V3(P(0.f, hl + 0.02f), zf + 4.7f), F3, 0.75f, detail ? 16 : 8, glassNight, emMat(EA_NIGHT));
        if (detail) ring(g, V3(P(0.f, hl), zf + 4.7f), F3, 0.82f, 0.14f, 0.08f, 16, trimC, trimM);
        // sacristy at the back under its own roof
        vec2 sc = P(0.f, -hl - 2.2f);
        boxY(g, V3(sc, z + 1.9f), R, vec3(3.f, 2.2f, 2.3f), wallC, wallM);
        gableRoof(g, sc, R, 4.4f, 6.f, z + 4.2f, 1.2f, 0.3f, roofC, roofM, wallC, wallM, true);
        collide(g, V3(sc, z + 2.2f), R, vec3(3.f, 2.2f, 2.2f));
        // floodlight washing the bell gable at night
        light(g, V3(P(0.f, hl + 9.f), z + 0.4f), vec3(1.f, 0.9f, 0.78f) * 9000.f, 30.f, 1, normalize(V3(-f, 0.f) * 9.f + up * 11.f), 0.14f);
    } else if (v == 1) {
        // ---- clapboard church: square steeple on the front, belfry with louvres, octagonal spire
        const float s0 = 1.7f;
        vec2 tc = P(0.f, hl - s0 + 0.3f);
        float tz0 = zt + rise * 0.3f, tz1 = zt + rise + 3.2f;
        boxY(g, V3(tc, (tz0 + tz1) * 0.5f), R, vec3(s0, s0, (tz1 - tz0) * 0.5f), wallC, wallM);
        // belfry stage with dark louvre openings
        boxY(g, V3(tc, tz1 + 1.2f), R, vec3(s0 - 0.2f, s0 - 0.2f, 1.2f), wallC, wallM);
        for (int k = 0; k < 4; k++) {
            vec2 n = rotate(f, kHalfPi * k), t = rightOf(n);
            vec2 wc = tc + n * (s0 - 0.19f);
            archedPanel(g, wc, t, n, 0.5f, tz1 + 0.45f, tz1 + 1.6f, 0.01f, rgb(0.15f, 0.15f, 0.16f), M(MAT_WOOD), detail ? 8 : 4);
        }
        boxY(g, V3(tc, tz1 + 2.46f), R, vec3(s0 + 0.05f, s0 + 0.05f, 0.06f), trimC, trimM);
        lathe(g, V3(tc, tz1 + 2.52f), {vec2(s0 * 1.2f, 0.f), vec2(0.f, 7.5f)}, 8, roofC, roofM, false, kPi / 8.f);
        top = tz1 + 10.f;
        latinCross(g, tc, f, tz1 + 9.9f, 1.4f, 0.1f, rgb(0.8f, 0.68f, 0.35f), M(MAT_METAL_BRUSHED));
        top += 1.4f;
        collide(g, V3(tc, (zt + tz1) * 0.5f + 1.f), R, vec3(s0, s0, (tz1 - zt) * 0.5f + 1.f));
        // gable window over the door
        archedPanel(g, P(0.f, hl), R, f, 0.45f, zf + 3.4f, zf + 4.3f, 0.02f, glassNight, emMat(EA_NIGHT), 6);
        light(g, V3(P(0.f, hl + 8.f), z + 0.4f), vec3(1.f, 0.92f, 0.8f) * 7000.f, 28.f, 1, normalize(V3(-f, 0.f) * 8.f + up * 12.f), 0.14f);
    } else {
        // ---- brick church: square corner tower with a crenellated top, stone quoins and a rose window
        vec2 tc = P(hw - 1.6f, hl - 1.2f);
        float th = wallH + rise + 5.f;
        boxY(g, V3(tc, z - 0.2f + (th + 0.2f) * 0.5f), R, vec3(2.f, 2.f, (th + 0.2f) * 0.5f), wallC, wallM);
        boxY(g, V3(tc, z + th + 0.08f), R, vec3(2.15f, 2.15f, 0.08f), trimC, trimM);
        for (int k = 0; k < 4; k++) {   // corner pinnacles
            vec2 cp = tc + R * ((k & 1) ? 1.8f : -1.8f) + f * ((k & 2) ? 1.8f : -1.8f);
            boxY(g, V3(cp, z + th + 0.6f), R, vec3(0.25f, 0.25f, 0.45f), trimC, trimM);
            lathe(g, V3(cp, z + th + 1.05f), {vec2(0.3f, 0.f), vec2(0.f, 0.9f)}, 4, trimC, trimM, false, kPi * 0.25f);
        }
        for (int k = 0; k < 4; k++) {   // belfry openings
            vec2 n = rotate(f, kHalfPi * k), t = rightOf(n);
            archedPanel(g, tc + n * 2.01f, t, n, 0.55f, z + th - 3.f, z + th - 1.5f, 0.01f, rgb(0.12f, 0.12f, 0.13f), M(MAT_WOOD), detail ? 8 : 4);
        }
        top = z + th + 2.f;
        collide(g, V3(tc, z + th * 0.5f), R, vec3(2.f, 2.f, th * 0.5f));
        // rose window in the front gable
        disc(g, V3(P(-0.8f, hl + 0.02f), zf + wallH + 0.9f), F3, 1.f, detail ? 16 : 8, glassNight, emMat(EA_NIGHT));
        if (detail) {
            ring(g, V3(P(-0.8f, hl), zf + wallH + 0.9f), F3, 1.08f, 0.16f, 0.1f, 16, trimC, trimM);
            for (int k = 0; k < 4; k++) {   // stone quoins at the front corners
                for (int s = -1; s <= 1; s += 2)
                    bx(s * (hw - 0.02f), hl - 0.02f, zf + 0.4f + k * 1.6f, 0.3f, 0.3f, 0.35f, trimC, trimM);
            }
        }
        light(g, V3(P(0.f, hl + 9.f), z + 0.4f), vec3(1.f, 0.9f, 0.78f) * 8000.f, 30.f, 1, normalize(V3(-f, 0.f) * 9.f + up * 12.f), 0.14f);
    }
    (void)top;
}

// ------------------------------------------------------------------------------------------------ layout
void layout(SiteSet& S, WorldMap& map) {
    // the block must be dry Calle Luna ground
    float zs = 0.f;
    int n = 0;
    for (float y = kY0 + 15.f; y < kY1 - 10.f; y += 20.f)
        for (float x = kX0 + 15.f; x < kX1 - 10.f; x += 20.f) {
            if (map.isWater(x, y) || map.regionAt(x, y) != REG_CALLE_LUNA) {
                LOG("Places: cemetery ground at (%.0f, %.0f) is not Calle Luna, cemetery left out", x, y);
                return;
            }
            zs += map.heightAt(x, y);
            n++;
        }
    const float zg = roundf(zs / Max(1, n) * 10.f) / 10.f;
    map.flattenRect(vec2((kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f), vec2(1, 0), (kX1 - kX0) * 0.5f + 4.f, (kY1 - kY0) * 0.5f + 4.f, zg, 10.f);
    const float z = zg + 0.4f;   // sidewalk level: the gates open flush on the sidewalks
    S.roadBlocks.push_back({vec2((kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f), vec2(1, 0), (kX1 - kX0) * 0.5f - 1.f, (kY1 - kY0) * 0.5f - 1.f});
    S.lotBlocks.push_back({vec2((kWX0 + kWX1) * 0.5f, (kWY0 + kWY1) * 0.5f), vec2(1, 0), (kWX1 - kWX0) * 0.5f + 3.f, (kWY1 - kWY0) * 0.5f + 3.f});
    S.vegBlocks.push_back({vec2((kWX0 + kWX1) * 0.5f, (kWY0 + kWY1) * 0.5f), vec2(1, 0), (kWX1 - kWX0) * 0.5f, (kWY1 - kWY0) * 0.5f});
    // no lamp posts, trees or street furniture on the sidewalks in front of the gates
    S.vegBlocks.push_back({vec2(kX0 + 8.8f + 1.75f, kAvY), vec2(1, 0), 2.5f, kGateMainHW + 3.5f});
    S.vegBlocks.push_back({vec2(kX1 - 5.6f - 1.5f, kServiceY), vec2(1, 0), 2.2f, kGateServiceHW + 2.5f});
    for (float sy : {kY0 + 5.6f + 1.5f, kY1 - 5.6f - 1.5f}) S.vegBlocks.push_back({vec2(kAvX, sy), vec2(1, 0), kGatePedHW + 2.5f, 2.2f});
    // the ground inside the walls (the lawn, the aisles and the tombs stand at sidewalk level over the levelled terrain)
    {
        Pad pd;
        pd.c = vec2((kWX0 + kWX1) * 0.5f, (kWY0 + kWY1) * 0.5f);
        pd.ax = vec2(1, 0);
        pd.hx = (kWX1 - kWX0) * 0.5f;
        pd.hy = (kWY1 - kWY0) * 0.5f;
        pd.z = z + 0.05f;
        pd.slope = 0.f;
        pd.kind = PAD_TURF;
        pd.drawn = 0;
        pd.skirt = 0;
        pd.flags = 0;
        pd.color = 0xffffffffu;
        S.pads.push_back(pd);
    }
    const int placeIdx = (int)S.places.size();
    {
        NamedPlace np;
        np.name = "Santa Marea Cemetery";
        np.kind = PK_CEMETERY;
        np.pos = vec2((kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f);
        np.door = vec2(kX0 + 8.8f + 1.75f, kAvY);
        np.radius = 130.f;
        S.places.push_back(np);
    }
    // ---------------------------------------------------------------- perimeter wall vault ranges with their gates
    auto wall = [&](vec2 a, vec2 b, vec2 in, int variant, float gate, float ghw, int kind, u32 seed) {
        SiteElem e;
        e.kind = SK_CEMETERY_WALL;
        e.variant = (u16)variant;
        e.seed = seed;
        e.a = a;
        e.b = b;
        e.ax = normalize(b - a);
        e.c = (a + b) * 0.5f + in * (kRangeT * 0.5f);
        e.hx = length(b - a) * 0.5f + 1.f;
        e.hy = kRangeT * 0.5f + 1.5f;
        e.z = z;
        e.h = 7.f;
        e.p[0] = in.x;
        e.p[1] = in.y;
        e.p[2] = gate;
        e.p[3] = ghw;
        e.p[4] = (float)kind;
        S.elems.push_back(e);
    };
    wall(vec2(kWX0, kWY0), vec2(kWX0, kWY1), vec2(1, 0), 1, kAvY - kWY0, kGateMainHW, 2, 0xCE01u);
    wall(vec2(kWX1, kWY0), vec2(kWX1, kWY1), vec2(-1, 0), 1, kServiceY - kWY0, kGateServiceHW, 3, 0xCE02u);
    wall(vec2(kWX0 + kRangeT, kWY0), vec2(kWX1 - kRangeT, kWY0), vec2(0, 1), 0, kAvX - kWX0 - kRangeT, kGatePedHW, 1, 0xCE03u);
    wall(vec2(kWX0 + kRangeT, kWY1), vec2(kWX1 - kRangeT, kWY1), vec2(0, -1), 0, kAvX - kWX0 - kRangeT, kGatePedHW, 1, 0xCE04u);
    // ---------------------------------------------------------------- grounds, the four quarters of tomb rows, the chapel
    {
        SiteElem e;
        e.kind = SK_CEMETERY;
        e.variant = 4;
        e.seed = 0xCE10u;
        e.c = vec2((kIX0 + kIX1) * 0.5f, (kIY0 + kIY1) * 0.5f);
        e.ax = vec2(1, 0);
        e.hx = (kIX1 - kIX0) * 0.5f;
        e.hy = (kIY1 - kIY0) * 0.5f;
        e.z = z;
        e.h = 13.f;
        S.elems.push_back(e);
    }
    std::vector<Section> secs;
    for (int q = 0; q < 4; q++) {
        bool east = q & 1, north = q & 2;
        Section s;
        s.x0 = east ? kAvX + kCrossHW + 1.f : kIX0 + kPeriW;
        s.x1 = east ? kIX1 - kPeriW : kAvX - kCrossHW - 1.f;
        s.ny = north ? 1 : -1;
        s.yAv = kAvY + s.ny * (kAvHW + kVerge);
        s.yEnd = north ? kIY1 - kPeriW : kIY0 + kPeriW;
        s.seed = 0xCE20u + (u32)q * 0x1F3u;
        secs.push_back(s);
        SiteElem e;
        e.kind = SK_CEMETERY;
        e.variant = (u16)q;
        e.seed = s.seed;
        e.c = vec2((s.x0 + s.x1) * 0.5f, (s.yAv + s.yEnd) * 0.5f);
        e.ax = vec2(1, 0);
        e.hx = (s.x1 - s.x0) * 0.5f + 1.f;
        e.hy = fabsf(s.yEnd - s.yAv) * 0.5f + 0.5f;
        e.z = z;
        e.h = 9.f;
        e.p[0] = s.x0;
        e.p[1] = s.x1;
        e.p[2] = s.yAv;
        e.p[3] = s.yEnd;
        e.p[4] = (float)s.ny;
        S.elems.push_back(e);
    }
    {
        SiteElem e;
        e.kind = SK_CHAPEL;
        e.variant = 0;
        e.seed = 0xCE30u;
        e.c = vec2(kChapelX, kAvY);
        e.ax = vec2(-1, 0);
        e.hx = kChapelHW;
        e.hy = kChapelHL;
        e.z = z;
        e.h = 18.f;
        S.elems.push_back(e);
    }
    // ---------------------------------------------------------------- walks for the pedestrian graph
    const float zw = z + 0.1f;
    auto walk = [&](vec2 a, vec2 b, float hw) { S.walks.push_back({vec3(a, zw), vec3(b, zw), hw, SW_PATH}); };
    auto chainY = [&](float x, std::vector<float> ys, float hw) {   // nodes along a north-south line
        std::sort(ys.begin(), ys.end());
        for (size_t k = 0; k + 1 < ys.size(); k++)
            if (ys[k + 1] - ys[k] > 0.3f) walk(vec2(x, ys[k]), vec2(x, ys[k + 1]), hw);
    };
    auto chainX = [&](float y, std::vector<float> xs, float hw) {
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k++)
            if (xs[k + 1] - xs[k] > 0.3f) walk(vec2(xs[k], y), vec2(xs[k + 1], y), hw);
    };
    const float pwx0 = kIX0 + kPeriW * 0.5f, pwx1 = kIX1 - kPeriW * 0.5f, pwy0 = kIY0 + kPeriW * 0.5f, pwy1 = kIY1 - kPeriW * 0.5f;
    const float ringR = 9.5f;
    std::vector<float> crossN = {kY1 - 5.6f - 1.5f, pwy1, kAvY + ringR}, crossS = {kY0 + 5.6f + 1.5f, pwy0, kAvY - ringR};
    std::vector<float> periW = {pwy0, kAvY, pwy1}, periE = {pwy0, kServiceY, pwy1};
    for (const Section& s : secs) {
        std::vector<Row> rows;
        std::vector<Aisle> aisles;
        rowsOf(s, rows, &aisles);
        bool east = s.x0 > kAvX;
        for (const Aisle& a : aisles) {
            float y = s.yAv + s.ny * a.v;
            float xp = east ? pwx1 : pwx0;
            bool yard = east && y < kYardY1 + 1.f;
            if (yard) xp = kYardX0 - 1.5f;
            walk(vec2(kAvX, y), vec2(xp, y), 1.6f);
            (s.ny > 0 ? crossN : crossS).push_back(y);
            if (!yard) (east ? periE : periW).push_back(y);
        }
    }
    chainY(kAvX, crossN, 2.f);
    chainY(kAvX, crossS, 2.f);
    chainY(pwx0, periW, 1.8f);
    chainY(pwx1, periE, 1.8f);
    chainX(pwy0, {pwx0, kAvX, pwx1}, 1.8f);
    chainX(pwy1, {pwx0, kAvX, pwx1}, 1.8f);
    // main avenue: sidewalk of 33rd Avenue, the perimeter aisle, the rond-point, the forecourt, the chapel door
    chainX(kAvY, {kX0 + 8.8f + 1.75f, pwx0, kAvX - ringR}, 3.f);
    chainX(kAvY, {kAvX + ringR, kPlazaX0 + 3.f, kChapelX - kChapelHL - 1.2f}, 3.f);
    const vec2 oc(kAvX, kAvY);
    const vec2 ringP[4] = {oc + vec2(-ringR, 0.f), oc + vec2(0.f, ringR), oc + vec2(ringR, 0.f), oc + vec2(0.f, -ringR)};
    for (int k = 0; k < 4; k++) walk(ringP[k], ringP[(k + 1) & 3], 1.2f);
    // service gate on 31st Avenue
    walk(vec2(kX1 - 5.6f - 1.5f, kServiceY), vec2(pwx1, kServiceY), 1.8f);
    // ---------------------------------------------------------------- people: mourners at the tombs, visitors, the caretaker
    u16 group = 1;   // group 0: the visitors' way from the gate to the chapel
    for (vec2 p : {vec2(kIX0 + 2.f, kAvY), vec2(kAvX - 30.f, kAvY + 1.5f), vec2(kAvX - ringR, kAvY), vec2(kAvX, kAvY + ringR), vec2(kAvX + ringR, kAvY),
                   vec2(kAvX + 30.f, kAvY - 1.f), vec2(kPlazaX0 + 3.f, kAvY), vec2(kChapelX - kChapelHL - 1.5f, kAvY)})
        S.anchors.push_back({vec3(p, zw), vec2(1, 0), PA_WAYPOINT, (u8)placeIdx, 0});
    int mourners = 0;
    for (const Section& s : secs) {
        std::vector<Plot> plots;
        plotsOf(s, plots);
        for (const Plot& p : plots) {
            if (!mournedAt(p)) continue;
            vec2 R = rightOf(p.f);
            int m = ((p.h >> 16) % 5u) < 2u ? 2 : 1;
            for (int k = 0; k < m; k++) {
                vec2 q = p.c + p.f * (p.d * 0.5f + (p.frontage ? 2.f : 1.1f)) + R * (m == 2 ? (k ? 0.45f : -0.45f) : 0.f);
                S.anchors.push_back({vec3(q, z + 0.05f), -p.f, PA_MOURN, (u8)placeIdx, group});
                mourners++;
            }
            group++;
        }
    }
    for (int k = 0; k < 4; k++) {   // benches round the obelisk
        vec2 d(k & 1 ? 1.f : -1.f, k & 2 ? 1.f : -1.f);
        vec2 bp = oc + normalize(d) * 9.3f;
        S.anchors.push_back({vec3(bp, z + 0.05f), normalize(oc - bp), PA_SIT, (u8)placeIdx, group++});
    }
    const float fx1 = kChapelX - kChapelHL;
    for (int s = -1; s <= 1; s += 2)
        S.anchors.push_back({vec3(vec2((kPlazaX0 + fx1) * 0.5f - 1.f, kAvY + s * (kPlazaHW - 1.f)), z + 0.06f), vec2(0.f, -(float)s), PA_SIT, (u8)placeIdx, group++});
    S.anchors.push_back({vec3(vec2(fx1 - 1.f, kAvY + 1.6f), z + 0.06f), vec2(-1, 0), PA_STAND, (u8)placeIdx, group++});   // at the chapel door
    S.anchors.push_back({vec3(vec2(kIX1 - kPeriW - 7.f, kIY0 + kPeriW + 5.2f), z + 0.05f), vec2(1, 0), PA_WORK, (u8)placeIdx, group++});   // caretaker
    S.anchors.push_back({vec3(vec2(pwx0, kAvY + 30.f), z + 0.05f), vec2(1, 0), PA_WORK, (u8)placeIdx, group++});   // groundskeeper on the aisle
    LOG("Places: Santa Marea Cemetery at (%.0f, %.0f), ground %.1f, %d mourners", (kX0 + kX1) * 0.5f, (kY0 + kY1) * 0.5f, zg, mourners);
}

}  // namespace cemetery

}  // namespace World
