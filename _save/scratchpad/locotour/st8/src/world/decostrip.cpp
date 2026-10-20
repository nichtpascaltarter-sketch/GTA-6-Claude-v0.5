// Ocean Promenade, Sol Beach: the art deco hotel row between 37th and 41st Street and the beachfront park across the
// avenue. Every hotel is built by hand from one of four symmetrical designs (central tower with a vertical neon blade,
// stepped ziggurat crest with the name across it, streamline with rounded corners and wrap-around eyebrows, twin fins
// with the name on a crossbar) in its own pastel scheme: eyebrow ledges over the windows, fins and speed lines, glass
// block, portholes, a sunburst over the door, a raised terrace with cafe tables under market umbrellas, and neon that
// lights the strip at night. The hotels are real buildings (map, collision queries, night lighting), meshed here.
#include "places.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace deco_strip {

using namespace sitegeo;
using namespace place_kit;

struct Palette {
    vec3 wall, trim, accent, neon;
    vec3 block;   // the contrasting colour block: centre bay, tower, fins, corner piers, rounded ends, parapet band
};
// Body colours are real pastels (they read near white in full sun if they are any paler); each scheme pairs its body with
// a contrasting block colour: pink with aqua, mint with rose, lavender with gold, peach and lemon with sky
// blue, white and coral with teal
const Palette kPal[8] = {
    {vec3(0.99f, 0.7f, 0.77f), vec3(0.99f, 0.98f, 0.96f), vec3(0.93f, 0.42f, 0.58f), vec3(1.f, 0.2f, 0.62f), vec3(0.5f, 0.82f, 0.78f)},       // flamingo
    {vec3(0.66f, 0.94f, 0.81f), vec3(0.99f, 0.99f, 0.97f), vec3(0.18f, 0.66f, 0.58f), vec3(0.2f, 1.f, 0.85f), vec3(0.98f, 0.64f, 0.66f)},     // mint
    {vec3(0.8f, 0.74f, 0.99f), vec3(0.98f, 0.97f, 1.f), vec3(0.52f, 0.38f, 0.84f), vec3(0.72f, 0.35f, 1.f), vec3(1.f, 0.86f, 0.52f)},        // lavender
    {vec3(1.f, 0.8f, 0.62f), vec3(1.f, 0.97f, 0.9f), vec3(0.93f, 0.48f, 0.28f), vec3(1.f, 0.55f, 0.15f), vec3(0.5f, 0.76f, 0.9f)},          // peach
    {vec3(1.f, 0.93f, 0.56f), vec3(0.99f, 0.99f, 0.96f), vec3(0.3f, 0.6f, 0.74f), vec3(0.25f, 0.6f, 1.f), vec3(0.54f, 0.77f, 0.94f)},        // lemon
    {vec3(0.64f, 0.84f, 0.99f), vec3(0.99f, 0.99f, 1.f), vec3(0.94f, 0.52f, 0.36f), vec3(1.f, 0.28f, 0.38f), vec3(0.97f, 0.62f, 0.5f)},      // sky
    {vec3(0.97f, 0.96f, 0.93f), vec3(0.97f, 0.97f, 0.95f), vec3(0.16f, 0.58f, 0.68f), vec3(0.25f, 0.95f, 1.f), vec3(0.32f, 0.7f, 0.78f)},    // white
    {vec3(1.f, 0.68f, 0.58f), vec3(1.f, 0.96f, 0.9f), vec3(0.16f, 0.52f, 0.54f), vec3(0.35f, 1.f, 0.5f), vec3(0.26f, 0.62f, 0.62f)},         // coral
};
const char* kNames[12] = {"THE CORALINE", "HOTEL MARISOL", "THE AZURELLE", "LA PERLITA", "THE LUNARIA",  "THE SEAWARD",
                          "THE PALOMITA", "HOTEL CIELITO", "THE ZEPHYRINE", "THE SEAFOAM", "THE ROSALIND", "BELLAMAR"};
const char* kWords[12] = {"CORALINE", "MARISOL", "AZURELLE", "PERLITA", "LUNARIA", "SEAWARD", "PALOMITA", "CIELITO", "ZEPHYRINE", "SEAFOAM", "ROSALIND",
                          "BELLAMAR"};

enum Tpl { T_TOWER = 0, T_ZIGGURAT, T_STREAMLINE, T_TWINFIN, T_COUNT };

// Everything the layout and the mesh agree on, derived from the element
struct Hotel {
    vec2 c, f, a;         // main mass centre, front (to the avenue), along the avenue (a = perp(f))
    float W, D, T;        // width along the avenue, depth, terrace depth
    float z0, zt;         // sidewalk level, terrace / ground floor level
    int F;                // floors
    float gh, fh, bay;    // ground floor height, upper floor height, bay width
    int nb;               // bays across the front (odd)
    int tpl, pal, name;
    int corners;          // bit0: the -a end faces a cross street, bit1: the +a end
    float H;              // roof height above zt
    float towerH;         // tower / crest / fin height above the roof
    float cw, cp;         // centre bay width and projection
    float R;              // rounded front corners (streamline), 0 = square
    float sill, winH;     // window sill and height (m) on the upper floors
    u32 seed;
    vec2 P(float u, float v) const { return c + a * u + f * v; }
    vec3 P3(float u, float v, float z) const { return vec3(c + a * u + f * v, z); }
};

Hotel hotelOf(const SiteElem& e) {
    Hotel h;
    h.c = e.c;
    h.f = e.ax;
    h.a = perp(e.ax);
    h.W = e.hx * 2.f;
    h.D = e.hy * 2.f;
    h.F = (int)e.p[0];
    h.T = e.p[1];
    h.towerH = e.p[2];
    h.pal = (int)e.p[3] & 7;
    h.name = (int)e.p[4] % 12;
    h.corners = (int)e.p[5];
    h.tpl = e.variant % T_COUNT;
    h.seed = e.seed;
    h.z0 = e.z;
    h.zt = e.z + 0.45f;
    h.gh = 4.3f;
    h.fh = 3.2f;
    h.H = h.gh + (h.F - 1) * h.fh;
    h.R = h.tpl == T_STREAMLINE ? Min(4.5f, h.W * 0.16f) : 0.f;
    float front = h.W - 2.f * h.R;
    h.nb = Max(3, (int)roundf(front / 3.05f));
    if ((h.nb & 1) == 0) h.nb += (front / h.nb > 3.05f) ? 1 : -1;
    h.bay = front / h.nb;
    h.cw = h.bay * (h.tpl == T_TOWER || h.tpl == T_TWINFIN ? 1.f : 3.f);
    if (h.cw < 3.f) h.cw = h.bay * 3.f;
    h.cp = h.tpl == T_STREAMLINE ? 0.6f : 1.1f;
    h.sill = 0.85f;
    h.winH = 0.56f * h.fh;
    return h;
}

// Cafe tables on the terrace: u along, v out from the facade, seats
struct TableSpot {
    vec2 uv;
    int seats;
};
void terraceTables(const Hotel& h, std::vector<TableSpot>& out) {
    out.clear();
    float clear = h.cw * 0.5f + 1.3f;   // walkway to the door
    int rows = h.T >= 5.5f ? 2 : 1;
    int perSide = 0;
    for (float u = clear + 1.2f; u < h.W * 0.5f - 1.3f; u += 3.1f) perSide++;
    perSide = Min(perSide, 4);   // wide fronts get planters between the tables instead
    for (int r = 0; r < rows; r++) {
        float v = h.D * 0.5f + h.cp + 1.3f + r * 2.3f;
        if (v > h.D * 0.5f + h.T - 1.1f) break;
        for (int sgn = -1; sgn <= 1; sgn += 2)
            for (int i = 0; i < perSide; i++) {
                float u = clear + 1.2f + i * 3.1f;
                u32 hs = hash32(h.seed + (u32)(u * 10.f) * 31u + (u32)r * 7u + (sgn > 0 ? 1000u : 0u));
                out.push_back({vec2(u * sgn, v), 2 + (int)(hs % 3)});
            }
    }
}
// Chair positions (world) around a table and the direction each sitter faces
void tableChairs(const Hotel& h, const TableSpot& t, vec2* pos, vec2* face) {
    for (int k = 0; k < t.seats; k++) {
        float ang = kTwoPi * k / t.seats + (t.seats == 2 ? 0.f : 0.4f);
        vec2 d = h.a * cosf(ang) + h.f * sinf(ang);
        pos[k] = h.P(t.uv.x, t.uv.y) + d * 0.62f;
        face[k] = -d;
    }
}

// ------------------------------------------------------------------------------------------------ mesh
// Eyebrow ledge along the wall line p0..p1 (outward normal n), projecting `dep`, thickness th; optional neon on its edge.
// lipH > 0 hangs a fascia of that depth below the front edge in lipCol: from the street the ledge then reads as a bold
// coloured band over a deep shadow line rather than a thin white stroke.
void eyebrow(G& g, vec2 p0, vec2 p1, vec2 n, float z, float dep, float th, u32 col, bool neon, vec3 ncol, u32 anim, u32 lipCol = 0,
             float lipH = 0.f) {
    vec2 d = normalize(p1 - p0);
    float L = length(p1 - p0);
    vec2 c = (p0 + p1) * 0.5f + n * (dep * 0.5f);
    boxY(g, vec3(c, z), d, vec3(L * 0.5f, dep * 0.5f, th * 0.5f), col, M(MAT_PLASTER), true);
    float zb = z - th * 0.5f;   // lowest edge at the front
    if (lipH > 0.f) {
        float lt = 0.045f, lz1 = z + th * 0.5f + 0.02f;
        zb -= lipH;
        boxY(g, vec3((p0 + p1) * 0.5f + n * (dep + lt * 0.5f - 0.01f), (lz1 + zb) * 0.5f), d, vec3(L * 0.5f, lt * 0.5f, (lz1 - zb) * 0.5f), lipCol, M(MAT_PLASTER),
             true);
    }
    if (neon && g.detail) neonTube(g, vec3(p0 + n * (dep + 0.07f), zb + 0.03f), vec3(p1 + n * (dep + 0.07f), zb + 0.03f), 0.025f, ncol, anim);
    else if (neon) neonTube(g, vec3(p0 + n * (dep + 0.07f), zb + 0.05f), vec3(p1 + n * (dep + 0.07f), zb + 0.05f), 0.06f, ncol, anim);
}

// Quarter-round corner (streamline): plain stucco arc wall from angle a0 to a1 around centre cc, radius R
void arcWall(G& g, vec2 cc, float R, float a0, float a1, float z0, float z1, u32 col, int seg) {
    for (int k = 0; k < seg; k++) {
        float t0 = Lerp(a0, a1, (float)k / seg), t1 = Lerp(a0, a1, (float)(k + 1) / seg);
        vec2 p0 = cc + vec2(cosf(t0), sinf(t0)) * R, p1 = cc + vec2(cosf(t1), sinf(t1)) * R;
        vec2 on = normalize((p0 + p1) * 0.5f - cc);
        quad(g, *g.m, vec3(p0, z0), vec3(p1, z0), vec3(p1, z1), vec3(p0, z1), col, M(MAT_STUCCO), vec3(on, 0));
    }
}
void arcBand(G& g, vec2 cc, float R, float a0, float a1, float z, float dep, float th, u32 col, int seg, bool neon, vec3 ncol, u32 anim, u32 lipCol = 0,
             float lipH = 0.f) {
    for (int k = 0; k < seg; k++) {
        float t0 = Lerp(a0, a1, (float)k / seg), t1 = Lerp(a0, a1, (float)(k + 1) / seg);
        vec2 p0 = cc + vec2(cosf(t0), sinf(t0)) * R, p1 = cc + vec2(cosf(t1), sinf(t1)) * R;
        vec2 on = normalize((p0 + p1) * 0.5f - cc);
        // widen each chord a little so the segments' outer corners meet round the curve
        vec2 dd = normalize(p1 - p0);
        float grow = (dep + 0.05f) * tanf(fabsf(t1 - t0) * 0.5f);
        eyebrow(g, p0 - dd * grow, p1 + dd * grow, on, z, dep, th, col, neon, ncol, anim, lipCol, lipH);
    }
}

void genHotel(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    const Hotel h = hotelOf(e);
    const Palette& pl = kPal[h.pal];
    bool detail = g.detail;
    const u32 fac = (u32)e.p[7];
    const u32 wallC = rgbv(pl.wall), trimC = rgbv(pl.trim), accC = rgbv(pl.accent), blockC = rgbv(pl.block);
    const u32 stucco = M(MAT_STUCCO), plaster = M(MAT_PLASTER);
    const float zt = h.zt, H = h.H, top = zt + H;
    // eyebrow ledges: deep enough to throw a real shadow line over the window heads, with a coloured fascia
    const float browDep = 0.85f, browTh = 0.15f, browLip = 0.17f;
    const float zBot = h.z0 - 0.8f;   // walls start below the levelled ground (sidewalk level - 0.8)
    const vec2 f = h.f, a = h.a;
    Rng r(h.seed ^ 0xDEC0u);
    const vec3 ncol = pl.neon;
    const u32 anim = EA_NIGHT;   // neon lights up at dusk; by day the tubes read as coloured glass
    const char* word = kWords[h.name];
    const float hw = h.W * 0.5f, hd = h.D * 0.5f;
    // ---------------------------------------------------------------- main mass (procedural windows)
    std::vector<vec2> fp;
    if (h.R > 0.f) {
        // rounded front corners: the facade walls run between the arcs, the arcs are plain stucco
        fp = {h.P(-hw, -hd), h.P(-hw, hd - h.R), h.P(-hw + h.R, hd), h.P(hw - h.R, hd), h.P(hw, hd - h.R), h.P(hw, -hd)};
        // CCW check (walls face outward)
        float area = polygonArea2D(fp.data(), (int)fp.size());
        if (area < 0) std::reverse(fp.begin(), fp.end());
        // facade walls only on the straight runs; replace the diagonal chords by arcs
        int n = (int)fp.size();
        for (int i = 0; i < n; i++) {
            vec2 p0 = fp[i], p1 = fp[(i + 1) % n];
            float len = length(p1 - p0);
            bool chord = fabsf(dot(normalize(p1 - p0), f)) > 0.3f && fabsf(dot(normalize(p1 - p0), a)) > 0.3f;
            if (chord) continue;
            std::vector<vec2> seg = {p0, p1};
            // one-wall facade ring (u0 from the wall index keeps the room hashes apart)
            float bays = Max(1.f, roundf(len / h.bay));
            vec2 on = normalize(vec2(p1.y - p0.y, p0.x - p1.x));
            g.m->quadFacing(vec3(p0, zBot) - g.org, vec3(p1, zBot) - g.org, vec3(p1, top) - g.org, vec3(p0, top) - g.org,
                            vec2(i * 1000.f, zBot - zt), vec2(i * 1000.f + bays * h.bay, zBot - zt), vec2(i * 1000.f + bays * h.bay, H), vec2(i * 1000.f, H), kWhiteC,
                            makeMat(MAT_FACADE, fac), vec3(on, 0));
        }
        int seg = detail ? 8 : 3;
        for (int s = -1; s <= 1; s += 2) {
            vec2 cc = h.P(s * (hw - h.R), hd - h.R);
            float base = atan2f(f.y, f.x);
            float side = atan2f(a.y * s, a.x * s);
            // arc from the side direction to the front direction
            float a0 = side, a1 = base;
            float dlt = a1 - a0;
            while (dlt > kPi) dlt -= kTwoPi;
            while (dlt < -kPi) dlt += kTwoPi;
            arcWall(g, cc, h.R, a0, a0 + dlt, zBot, top, blockC, seg);   // the rounded ends are the colour blocks
        }
        polyFlat(g, *g.m, fp, top, rgb(0.82f), M(MAT_ROOF_GRAVEL));
    } else {
        fp = rectPoly(h.c, f, hd, hw);
        facadeRing(g, fp, zBot, top, zt, fac, h.bay);
        polyFlat(g, *g.m, fp, top, rgb(0.82f), M(MAT_ROOF_GRAVEL));
    }
    collide(g, vec3(h.c, (zBot + top) * 0.5f), f, vec3(hd, hw, (top - zBot) * 0.5f));
    // parapet with coping all round (stepped centre on the front for the ziggurat and tower designs)
    {
        float ph = 0.9f;
        int n = (int)fp.size();
        for (int i = 0; i < n; i++) {
            vec2 p0 = fp[i], p1 = fp[(i + 1) % n];
            vec2 d = normalize(p1 - p0), on = perp(d) * -1.f;
            if (h.R > 0.f && fabsf(dot(d, f)) > 0.3f && fabsf(dot(d, a)) > 0.3f) continue;   // rounded corner (arc parapet below)
            if (dot(on, (p0 + p1) * 0.5f - h.c) < 0) on = -on;
            float L = length(p1 - p0);
            // the ziggurat's front parapet is a colour band running between its corner piers
            u32 pc = h.tpl == T_ZIGGURAT && dot(on, f) > 0.9f ? blockC : wallC;
            boxY(g, vec3((p0 + p1) * 0.5f - on * 0.12f, top + ph * 0.5f), d, vec3(L * 0.5f, 0.12f, ph * 0.5f), pc, stucco);
            boxY(g, vec3((p0 + p1) * 0.5f - on * 0.1f, top + ph + 0.05f), d, vec3(L * 0.5f + 0.06f, 0.2f, 0.05f), trimC, plaster, true);
        }
        if (h.R > 0.f && detail)
            for (int s = -1; s <= 1; s += 2) {
                vec2 cc = h.P(s * (hw - h.R), hd - h.R);
                float side = atan2f(a.y * s, a.x * s), base = atan2f(f.y, f.x), dlt = base - side;
                while (dlt > kPi) dlt -= kTwoPi;
                while (dlt < -kPi) dlt += kTwoPi;
                for (int k = 0; k < 6; k++) {
                    float t0 = side + dlt * k / 6.f, t1 = side + dlt * (k + 1) / 6.f;
                    vec2 q0 = cc + vec2(cosf(t0), sinf(t0)) * (h.R - 0.12f), q1 = cc + vec2(cosf(t1), sinf(t1)) * (h.R - 0.12f);
                    boxY(g, vec3((q0 + q1) * 0.5f, top + ph * 0.5f), normalize(q1 - q0), vec3(length(q1 - q0) * 0.5f + 0.02f, 0.12f, ph * 0.5f), blockC, stucco);
                }
            }
    }
    // ---------------------------------------------------------------- floor lines: eyebrows, canopy over the ground floor
    const float frontV = hd;            // front wall plane (v)
    const float cwh = h.cw * 0.5f;      // half width of the centre bay
    const float wingIn = cwh + (h.tpl == T_STREAMLINE ? 0.f : 0.f);
    // corner piers on the square designs: colour blocks framing the front (see below); the ledges stop against them
    const float pierW = h.R > 0.f ? 0.f : 0.62f, pierOut = 0.3f, pierTop = top + 1.55f;
    for (int fl = 1; fl < h.F; fl++) {
        float zHead = zt + h.gh + (fl - 1) * h.fh + h.sill + h.winH;
        float z = zHead + 0.09f;
        bool neon = h.tpl != T_ZIGGURAT;
        if (h.tpl == T_ZIGGURAT) {
            // individual hoods over each window (classic deco), stepped ends
            for (int bi = 0; bi < h.nb; bi++) {
                float u = -hw + h.R + (bi + 0.5f) * h.bay;
                if (fabsf(u) < cwh) continue;
                float ww = h.bay * 0.62f;
                eyebrow(g, h.P(u - ww * 0.5f, frontV), h.P(u + ww * 0.5f, frontV), f, z, 0.64f, 0.13f, trimC, false, ncol, anim, accC, 0.14f);
                if (detail) boxY(g, h.P3(u, frontV + 0.05f, z + 0.17f), a, vec3(ww * 0.32f, 0.05f, 0.05f), accC, plaster);
            }
        } else {
            // continuous bands across each wing, wrapping round the corners (streamline) or stopping at them
            for (int s = -1; s <= 1; s += 2) {
                float u0 = s * (wingIn + 0.05f), u1 = s * (hw - h.R - pierW);
                eyebrow(g, h.P(Min(u0, u1), frontV), h.P(Max(u0, u1), frontV), f, z, browDep, browTh, trimC, neon, ncol, anim, accC, browLip);
                if (h.R > 0.f) {
                    vec2 cc = h.P(s * (hw - h.R), hd - h.R);
                    float side = atan2f(a.y * s, a.x * s), base = atan2f(f.y, f.x), dlt = base - side;
                    while (dlt > kPi) dlt -= kTwoPi;
                    while (dlt < -kPi) dlt += kTwoPi;
                    arcBand(g, cc, h.R, side, side + dlt, z, browDep, browTh, trimC, detail ? 6 : 2, neon, ncol, anim, accC, browLip);
                    // and on round the side wall for a couple of bays
                    eyebrow(g, h.P(s * hw, hd - h.R), h.P(s * hw, hd - h.R - 3.2f), a * (float)s, z, browDep, browTh, trimC, false, ncol, anim, accC, browLip);
                } else if ((h.corners >> (s > 0 ? 1 : 0)) & 1) {
                    // corner hotel: the band turns the corner onto the cross-street facade
                    eyebrow(g, h.P(s * hw, frontV - pierW), h.P(s * hw, frontV - 4.6f), a * (float)s, z, browDep, browTh, trimC, neon, ncol, anim, accC, browLip);
                }
            }
        }
    }
    // ground floor canopy over the storefront (lobby, cafe) across the wings
    {
        float z = zt + h.gh - 0.62f;
        for (int s = -1; s <= 1; s += 2) {
            float u0 = s * (cwh + 0.1f), u1 = s * (hw - h.R - (pierW > 0.f ? pierW + 0.02f : 0.1f));
            eyebrow(g, h.P(Min(u0, u1), frontV), h.P(Max(u0, u1), frontV), f, z, 1.25f, 0.16f, accC, true, ncol, EA_NIGHT);
        }
        if (h.R > 0.f)
            for (int s = -1; s <= 1; s += 2) {
                vec2 cc = h.P(s * (hw - h.R), hd - h.R);
                float side = atan2f(a.y * s, a.x * s), base = atan2f(f.y, f.x), dlt = base - side;
                while (dlt > kPi) dlt -= kTwoPi;
                while (dlt < -kPi) dlt += kTwoPi;
                arcBand(g, cc, h.R, side, side + dlt, z, 1.25f, 0.16f, accC, detail ? 6 : 2, true, ncol, EA_NIGHT);
            }
    }
    // corner piers (square designs): full-height colour blocks at the front corners, a step above the parapet, crowned by
    // three speed-line bands; proud of the side wall only where it faces a cross street
    if (pierW > 0.f)
        for (int s = -1; s <= 1; s += 2) {
            bool street = (h.corners >> (s > 0 ? 1 : 0)) & 1;
            float uIn = hw - pierW, uOut = hw + (street ? pierOut : 0.03f);
            float vBack = frontV - (street ? pierW : pierOut), vFront = frontV + pierOut;
            vec2 pc = h.P(s * (uIn + uOut) * 0.5f, (vBack + vFront) * 0.5f);
            vec3 he((vFront - vBack) * 0.5f, (uOut - uIn) * 0.5f, (pierTop - zBot) * 0.5f);
            boxY(g, vec3(pc, (pierTop + zBot) * 0.5f), f, he, blockC, stucco);
            collide(g, vec3(pc, (pierTop + zt) * 0.5f), f, vec3(he.x, he.y, (pierTop - zt) * 0.5f));
            boxY(g, vec3(pc, pierTop + 0.07f), f, vec3(he.x + 0.07f, he.y + 0.07f, 0.07f), trimC, plaster, true);
            if (detail)
                for (int j = 0; j < 3; j++) boxY(g, vec3(pc, pierTop - 0.32f - j * 0.26f), f, vec3(he.x + 0.03f, he.y + 0.03f, 0.05f), trimC, plaster);
        }
    if (detail && h.tpl == T_STREAMLINE) {
        // racing stripes: three white bands round each rounded end on the parapet, running on along the side wall
        for (int s = -1; s <= 1; s += 2) {
            vec2 cc = h.P(s * (hw - h.R), hd - h.R);
            float side = atan2f(a.y * s, a.x * s), base = atan2f(f.y, f.x), dlt = base - side;
            while (dlt > kPi) dlt -= kTwoPi;
            while (dlt < -kPi) dlt += kTwoPi;
            for (int j = 0; j < 3; j++) {
                float z = top + 0.2f + j * 0.24f;
                arcBand(g, cc, h.R, side, side + dlt, z, 0.08f, 0.1f, trimC, 6, false, ncol, anim);
                eyebrow(g, h.P(s * hw, hd - h.R), h.P(s * hw, hd - h.R - 4.f), a * (float)s, z, 0.08f, 0.1f, trimC, false, ncol, anim);
            }
        }
    }
    // ---------------------------------------------------------------- centre bay (projects from the front wall)
    // (the ziggurat crest and the streamline name sit on the parapet above the centre bay, so that bay stops below them)
    const float cTop = top + (h.tpl == T_STREAMLINE ? 0.f : (h.tpl == T_TOWER ? h.towerH * 0.45f : (h.tpl == T_ZIGGURAT ? 0.9f : 1.6f)));
    {
        std::vector<vec2> cb = rectPoly(h.P(0.f, frontV + h.cp * 0.5f - 0.3f), f, h.cp * 0.5f + 0.3f, cwh);
        // the centre bay is a colour block (between the twin fins it keeps the body colour so the fins stand out)
        prism(g, cb, zBot, cTop, h.tpl == T_TWINFIN ? wallC : blockC, stucco, trimC, plaster, true);
        collide(g, vec3(h.P(0.f, frontV + h.cp * 0.5f - 0.3f), (zt + cTop) * 0.5f), f, vec3(h.cp * 0.5f + 0.3f, cwh, (cTop - zt) * 0.5f));
        // entrance: glass doors with a transom, framed
        vec2 dc = h.P(0.f, frontV + h.cp + 0.02f);
        float dw = Min(cwh - 0.35f, 1.5f);
        quad(g, *g.m, vec3(dc - a * dw, zt), vec3(dc + a * dw, zt), vec3(dc + a * dw, zt + 2.7f), vec3(dc - a * dw, zt + 2.7f), rgb(0.2f, 0.26f, 0.28f),
             M(MAT_GLASS), vec3(f, 0));
        if (detail) {
            for (int k = -1; k <= 1; k++) boxY(g, vec3(dc + a * (k * dw), zt + 1.35f), a, vec3(0.05f, 0.05f, 1.35f), accC, M(MAT_METAL_BRUSHED));
            boxY(g, vec3(dc, zt + 2.2f), a, vec3(dw, 0.05f, 0.04f), accC, M(MAT_METAL_BRUSHED));
            // lobby light behind the doors at night
            quad(g, *g.m, vec3(dc - a * (dw - 0.1f) - f * 0.01f, zt + 2.25f), vec3(dc + a * (dw - 0.1f) - f * 0.01f, zt + 2.25f),
                 vec3(dc + a * (dw - 0.1f) - f * 0.01f, zt + 2.65f), vec3(dc - a * (dw - 0.1f) - f * 0.01f, zt + 2.65f), rgb(1.f, 0.85f, 0.6f, 0.05f), emMat(EA_NIGHT),
                 vec3(f, 0));
        }
        // marquee canopy over the steps: slab with a neon edge and the name on its fascia
        vec2 mc = h.P(0.f, frontV + h.cp + 1.4f);
        float mw = cwh + 0.9f, zc = zt + 3.25f;
        boxY(g, vec3(mc, zc), a, vec3(mw, 1.45f, 0.14f), trimC, plaster, true);
        boxY(g, vec3(mc + f * 1.45f, zc + 0.05f), a, vec3(mw, 0.06f, 0.3f), accC, plaster, true);
        neonTube(g, vec3(mc + f * 1.53f - a * mw, zc - 0.22f), vec3(mc + f * 1.53f + a * mw, zc - 0.22f), 0.025f, ncol, EA_NIGHT);
        if (detail) {
            float th = Min(0.36f, (mw * 2.f - 0.4f) / Max(1.f, textAdvance(word, 1.f, 0.3f)));
            vec3 o = vec3(mc + f * 1.52f, zc - th * 0.5f + 0.05f) - vec3(a * (textAdvance(word, th, 0.3f) * 0.5f), 0.f);
            neonLetters(g, word, o, vec3(a, 0.f), vec3(0, 0, 1), th, pl.trim, EA_NIGHT, 0, 0.02f, 0.6f);
            for (int s = -1; s <= 1; s += 2) {
                // canopy tie rods to the wall
                beam(g, vec3(mc + f * 1.3f + a * (s * (mw - 0.2f)), zc + 0.1f), vec3(h.P(s * (mw - 0.2f), frontV + h.cp), zc + 1.9f), 0.04f, 0.04f,
                     rgb(0.25f), M(MAT_METAL_PAINTED));
            }
            light(g, vec3(mc, zc - 0.3f), vec3(1.f, 0.82f, 0.6f) * 2400.f, 9.f, 1, vec3(0, 0, -1), 0.3f);
        }
        // sunburst relief over the door (under the first upper floor)
        if (detail && h.F > 1) sunburst(g, h.P3(0.f, frontV + h.cp, zt + h.gh + 0.15f), vec3(a, 0.f), vec3(f, 0.f), Min(cwh * 0.8f, 1.5f), h.tpl == T_TWINFIN ? accC : trimC, 9);
        // upper floors of the centre bay: glass block strip (tower, twin fin, ziggurat) or porthole stack (streamline)
        float zb0 = zt + h.gh + 0.9f, zb1 = top - 0.6f;
        if (h.F > 1) {
            if (h.tpl == T_STREAMLINE) {
                for (int fl = 1; fl < h.F; fl++)
                    porthole(g, h.P3(0.f, frontV + h.cp, zt + h.gh + (fl - 1) * h.fh + h.fh * 0.55f), vec3(f, 0.f), 0.48f, trimC, true);
            } else {
                float gw = Min(1.4f, h.cw - 1.2f);
                glassBlock(g, h.P3(-gw * 0.5f, frontV + h.cp, zb0), vec3(a, 0.f), vec3(0, 0, 1), gw, zb1 - zb0, 0.2f);
                // flanking pilasters (fluted)
                if (detail)
                    for (int s = -1; s <= 1; s += 2)
                        for (int k = 0; k < 3; k++)
                            boxY(g, h.P3(s * (gw * 0.5f + 0.18f + k * 0.14f), frontV + h.cp + 0.05f, (zb0 + cTop) * 0.5f), a,
                                 vec3(0.05f, 0.05f + (k == 1 ? 0.03f : 0.f), (cTop - zb0) * 0.5f), trimC, plaster);
            }
        }
    }
    // ---------------------------------------------------------------- template crowns and signs
    const float bladeOut = 1.7f;
    if (h.tpl == T_TOWER) {
        // stepped tower rising from the centre bay: two setbacks and a finial
        float t0 = cTop, t1 = top + h.towerH * 0.75f, t2 = top + h.towerH;
        vec2 tc = h.P(0.f, frontV - 1.2f);
        std::vector<vec2> s1 = rectPoly(tc, f, 2.2f, cwh - 0.35f), s2 = rectPoly(tc, f, 1.6f, cwh - 0.8f);
        prism(g, rectPoly(tc, f, 2.8f, cwh), top, t0, blockC, stucco, trimC, plaster, true);
        prism(g, s1, t0, t1, blockC, stucco, trimC, plaster, true);
        prism(g, s2, t1, t2, trimC, stucco, accC, plaster, true);
        collide(g, vec3(tc, (top + t2) * 0.5f), f, vec3(2.8f, cwh, (t2 - top) * 0.5f));
        cyl(g, vec3(tc, t2), 0.35f, 0.05f, 3.6f, detail ? 8 : 5, accC, M(MAT_METAL_PAINTED), false);
        lathe(g, vec3(tc, t2 + 3.3f), {vec2(0.f, 0.f), vec2(0.28f, 0.2f), vec2(0.28f, 0.4f), vec2(0.f, 0.6f)}, 8, rgbv(ncol, 0.8f), emMat(EA_NIGHT), false);
        if (detail) {
            // portholes on the tower's front and sides
            porthole(g, vec3(tc + f * 2.21f, t1 - 1.6f), vec3(f, 0.f), 0.42f, trimC, true);
            for (int s = -1; s <= 1; s += 2) porthole(g, vec3(tc + a * (s * (cwh - 0.34f)), (t0 + t1) * 0.5f), vec3(a * (float)s, 0.f), 0.36f, trimC, true);
            // neon outline of the setbacks
            for (int s = -1; s <= 1; s += 2) {
                neonTube(g, vec3(tc + f * 2.23f + a * (s * (cwh - 0.35f)), t0), vec3(tc + f * 2.23f + a * (s * (cwh - 0.35f)), t1), 0.03f, ncol, anim, 20);
                neonTube(g, vec3(tc + f * 1.63f + a * (s * (cwh - 0.8f)), t1), vec3(tc + f * 1.63f + a * (s * (cwh - 0.8f)), t2), 0.03f, ncol, anim, 40);
            }
        }
        // vertical blade sign on the centre bay: the name reads down both faces
        float z0s = zt + h.gh + 0.4f, z1s = Min(top + h.towerH * 0.5f, z0s + 1.25f * (float)strlen(word) + 0.8f);
        vec2 bc = h.P(0.f, frontV + h.cp);
        boxY(g, vec3(bc + f * (bladeOut * 0.5f), (z0s + z1s) * 0.5f), f, vec3(bladeOut * 0.5f, 0.1f, (z1s - z0s) * 0.5f), trimC, M(MAT_METAL_PAINTED), true);
        collide(g, vec3(bc + f * (bladeOut * 0.5f), (z0s + z1s) * 0.5f), f, vec3(bladeOut * 0.5f, 0.1f, (z1s - z0s) * 0.5f));
        int nl = (int)strlen(word);
        float avail = z1s - z0s - 0.5f;
        float lh = Min(bladeOut * 0.58f, avail / (nl * 1.16f)), lw = lh * 6.f / 9.f;
        for (int face = 0; face < 2; face++) {
            float sd = face == 0 ? 1.f : -1.f;
            vec2 fn = a * sd;
            vec2 rt = perp(fn);
            for (int i = 0; i < nl; i++) {
                char ch[2] = {word[i], 0};
                float zc = z1s - 0.25f - (i + 0.5f) * (avail / nl);
                vec3 o = vec3(bc + f * (bladeOut * 0.5f) + fn * 0.11f - rt * (lw * 0.5f), zc - lh * 0.5f);
                neonLetters(g, ch, o, vec3(rt, 0.f), vec3(0, 0, 1), lh, ncol, anim, (u32)i * 30u, 0.05f);
            }
            if (detail) {
                vec3 b0 = vec3(bc + fn * 0.115f, z0s + 0.12f), b1 = vec3(bc + f * bladeOut + fn * 0.115f, z0s + 0.12f);
                vec3 u0 = vec3(bc + fn * 0.115f, z1s - 0.12f), u1 = vec3(bc + f * bladeOut + fn * 0.115f, z1s - 0.12f);
                neonTube(g, b0 + vec3(f * 0.12f, 0.f), b1 - vec3(f * 0.12f, 0.f), 0.022f, pl.trim, EA_NIGHT);
                neonTube(g, u0 + vec3(f * 0.12f, 0.f), u1 - vec3(f * 0.12f, 0.f), 0.022f, pl.trim, EA_NIGHT);
                neonTube(g, b1 - vec3(f * 0.12f, 0.f), u1 - vec3(f * 0.12f, 0.f), 0.022f, pl.trim, EA_NIGHT);
            }
        }
        light(g, vec3(bc + f * (bladeOut + 1.5f), (z0s + z1s) * 0.5f), ncol * 5200.f, 16.f, 2);
    } else if (h.tpl == T_ZIGGURAT) {
        // stepped crest across the centre third, the name across its face in big neon letters
        float cwid = Max(h.W * 0.62f, h.cw + 4.f);
        float z = top + 0.9f;
        for (int t = 0; t < 3; t++) {
            float ww = cwid * (1.f - t * 0.24f) * 0.5f, hh = t == 0 ? 1.6f : 0.9f;
            // white face behind the name, then a colour-block tier and an accent cap
            boxY(g, vec3(h.P(0.f, frontV - 0.25f), z + hh * 0.5f), a, vec3(ww, 0.25f, hh * 0.5f), t == 0 ? trimC : (t == 1 ? blockC : accC), stucco, false);
            boxY(g, vec3(h.P(0.f, frontV - 0.25f), z + hh + 0.04f), a, vec3(ww + 0.08f, 0.32f, 0.04f), t == 0 ? blockC : trimC, plaster, true);
            z += hh + 0.08f;
        }
        const char* nm = kNames[h.name];
        float th = Min(1.25f, (cwid - 1.2f) / Max(1.f, textAdvance(nm, 1.f, 0.3f)));
        vec3 o = vec3(h.P(0.f, frontV + 0.02f), top + 1.05f) - vec3(a * (textAdvance(nm, th, 0.3f) * 0.5f), 0.f);
        neonLetters(g, nm, o, vec3(a, 0.f), vec3(0, 0, 1), th, ncol, anim, 0, 0.06f);
        // fluted pilasters between the centre bays up the front
        if (detail)
            for (int k = -1; k <= 1; k += 2) {
                vec2 pp = h.P(k * (cwh + 0.25f), frontV + 0.15f);
                boxY(g, vec3(pp, (zt + h.gh + top + 0.9f) * 0.5f), a, vec3(0.22f, 0.15f, (top + 0.9f - zt - h.gh) * 0.5f), blockC, plaster);
                for (int q = -1; q <= 1; q++)
                    boxY(g, vec3(pp + a * (q * 0.12f) + f * 0.16f, (zt + h.gh + top + 0.9f) * 0.5f), a, vec3(0.03f, 0.03f, (top + 0.9f - zt - h.gh) * 0.5f),
                         trimC, plaster);
            }
        light(g, vec3(h.P(0.f, frontV + 6.f), top + 1.5f), ncol * 6000.f, 18.f, 2);
    } else if (h.tpl == T_STREAMLINE) {
        // name across the top band in neon, flag pole on the roof, porthole column already on the centre bay
        const char* nm = kNames[h.name];
        float th = Min(1.05f, (h.W - 2.f * h.R - 1.f) / Max(1.f, textAdvance(nm, 1.f, 0.3f)));
        vec3 o = vec3(h.P(0.f, frontV + 0.06f), top - 0.35f - th) - vec3(a * (textAdvance(nm, th, 0.3f) * 0.5f), 0.f);
        // letters sit below the racing stripes (the stripes are at top - 0.55 .. - 1.03): place them in the parapet instead
        o.z = top + 0.02f;
        neonLetters(g, nm, o + vec3(f * 0.03f, 0.f), vec3(a, 0.f), vec3(0, 0, 1), Min(th, 0.8f), ncol, anim, 0, 0.05f);
        if (detail) flagPole(g, h.P(0.f, -hd * 0.3f), top, 6.f, -f, pl.accent, pl.trim);
        light(g, vec3(h.P(0.f, frontV + 5.f), top), ncol * 4200.f, 15.f, 2);
    } else {
        // twin fins flanking the centre bay, rising above the roof, a crossbar carrying the name
        float fz1 = top + h.towerH;
        for (int s = -1; s <= 1; s += 2) {
            vec2 fc = h.P(s * (cwh + 0.2f), frontV + h.cp * 0.5f + 0.2f);
            boxY(g, vec3(fc, (zt + h.gh + fz1) * 0.5f), f, vec3(h.cp * 0.5f + 0.9f, 0.18f, (fz1 - zt - h.gh) * 0.5f), blockC, stucco, true);
            collide(g, vec3(fc, (zt + h.gh + fz1) * 0.5f), f, vec3(h.cp * 0.5f + 0.9f, 0.18f, (fz1 - zt - h.gh) * 0.5f));
            // stepped fin tops
            boxY(g, vec3(fc - f * 0.3f, fz1 + 0.35f), f, vec3(h.cp * 0.5f + 0.5f, 0.18f, 0.35f), trimC, plaster, true);
            if (detail) neonTube(g, vec3(fc + f * (h.cp * 0.5f + 0.93f), zt + h.gh), vec3(fc + f * (h.cp * 0.5f + 0.93f), fz1), 0.03f, ncol, anim, (u32)(s + 1) * 60u);
        }
        float zb = fz1 - 1.9f;
        vec2 bc = h.P(0.f, frontV + h.cp * 0.5f + 0.9f);
        boxY(g, vec3(bc, zb), a, vec3(cwh + 0.35f, 0.14f, 0.65f), accC, plaster, true);
        const char* nm = word;
        float th = Min(0.9f, (h.cw + 0.2f) / Max(1.f, textAdvance(nm, 1.f, 0.3f)));
        // too wide for the crossbar: the name climbs the left fin instead
        if (th < 0.45f) {
            int nl = (int)strlen(nm);
            float avail = top + h.towerH - (zt + h.gh) - 2.6f;
            float lh = Min(0.85f, avail / (nl * 1.15f)), lw = lh * 6.f / 9.f;
            vec2 fc = h.P(-(cwh + 0.2f), frontV + h.cp + 1.12f);
            for (int i = 0; i < nl; i++) {
                char ch[2] = {nm[i], 0};
                float zc = fz1 - 2.8f - (i + 0.5f) * (avail / nl);
                neonLetters(g, ch, vec3(fc - a * (lw * 0.5f), zc - lh * 0.5f), vec3(a, 0.f), vec3(0, 0, 1), lh, ncol, anim, (u32)i * 25u, 0.05f);
            }
        } else {
            vec3 o = vec3(bc + f * 0.15f, zb - th * 0.5f) - vec3(a * (textAdvance(nm, th, 0.3f) * 0.5f), 0.f);
            neonLetters(g, nm, o, vec3(a, 0.f), vec3(0, 0, 1), th, ncol, anim, 0, 0.05f);
        }
        light(g, vec3(bc + f * 4.f, zb), ncol * 5000.f, 16.f, 2);
    }
    // ---------------------------------------------------------------- rooftop
    if (detail) {
        vec2 rc = h.P(r.range(-hw * 0.4f, hw * 0.4f), -hd * 0.45f);
        boxY(g, vec3(rc, top + 1.3f), f, vec3(1.6f, 1.4f, 1.3f), wallC, stucco, false);
        boxY(g, vec3(rc, top + 2.65f), f, vec3(1.75f, 1.55f, 0.06f), trimC, plaster, false);
        if (r.chance(0.6f)) {
            vec2 tp = h.P(r.range(-hw * 0.5f, hw * 0.5f), -hd * 0.1f);
            for (int k = 0; k < 4; k++) beam(g, vec3(tp + vec2((k & 1) ? 0.9f : -0.9f, (k & 2) ? 0.9f : -0.9f), top), vec3(tp + vec2((k & 1) ? 0.7f : -0.7f, (k & 2) ? 0.7f : -0.7f), top + 2.4f),
                                             0.1f, 0.1f, rgb(0.4f), M(MAT_METAL_PAINTED));
            cyl(g, vec3(tp, top + 2.4f), 1.2f, 1.2f, 2.2f, 10, rgb(0.55f, 0.5f, 0.45f), M(MAT_WOOD), true);
        }
        for (int k = 0; k < 3; k++) {
            vec2 ap = h.P(r.range(-hw + 2.f, hw - 2.f), r.range(-hd + 2.f, 0.f));
            boxY(g, vec3(ap, top + 0.5f), f, vec3(0.8f, 0.6f, 0.5f), rgb(0.72f), M(MAT_METAL_PAINTED), false);
        }
    }
    // ---------------------------------------------------------------- terrace (raised porch with the cafe)
    {
        float v0 = frontV + (h.tpl == T_STREAMLINE ? 0.f : 0.f), v1 = frontV + h.T;
        vec2 tc = h.P(0.f, (v0 + v1) * 0.5f);
        float thd = (v1 - v0) * 0.5f;
        std::vector<vec2> tp = rectPoly(tc, f, thd, hw);
        size_t v0i = g.m->verts.size();
        u32 floorC = tint(lerp(pl.trim, pl.accent, 0.18f));
        polyFlat(g, *g.m, tp, zt, floorC, M(MAT_TILE));
        (void)v0i;
        // skirts down to the sidewalk
        for (int k = 0; k < 4; k++) {
            vec2 p0 = tp[k], p1 = tp[(k + 1) % 4];
            vec2 on = normalize(vec2(p1.y - p0.y, p0.x - p1.x));
            if (dot(on, f) < -0.5f) continue;   // against the building
            quad(g, *g.m, vec3(p0, zt), vec3(p1, zt), vec3(p1, h.z0 - 0.3f), vec3(p0, h.z0 - 0.3f), rgbv(pl.wall * 0.85f), stucco, vec3(on, 0));
        }
        // front wall with the steps in the middle, side walls
        float stepHalf = cwh + 0.9f;
        float wz = zt, wh = 0.62f;
        for (int s = -1; s <= 1; s += 2) {
            vec2 w0 = h.P(s * stepHalf, v1 - 0.15f), w1 = h.P(s * (hw - 0.12f), v1 - 0.15f);
            copedWall(g, w0, w1, wz, wh, 0.26f, wallC, stucco, trimC, plaster, true);
            vec2 s0 = h.P(s * (hw - 0.12f), v1 - 0.15f), s1 = h.P(s * (hw - 0.12f), v0 + 0.2f);
            copedWall(g, s0, s1, wz, wh, 0.24f, wallC, stucco, trimC, plaster, true);
            // accent stripe on the wall face
            if (detail) boxY(g, vec3((w0 + w1) * 0.5f + f * 0.14f, wz + wh * 0.55f), a, vec3(length(w1 - w0) * 0.5f, 0.02f, 0.05f), accC, plaster);
            // lamp posts on the wall ends at the steps
            if (detail) {
                vec2 lp = h.P(s * (stepHalf + 0.2f), v1 - 0.15f);
                cyl(g, vec3(lp, wz + wh), 0.06f, 0.05f, 0.9f, 6, accC, M(MAT_METAL_PAINTED), false);
                lathe(g, vec3(lp, wz + wh + 0.9f), {vec2(0.f, 0.f), vec2(0.17f, 0.12f), vec2(0.17f, 0.3f), vec2(0.f, 0.42f)}, 8, rgb(1.f, 0.9f, 0.72f, 0.45f),
                      emMat(EA_NIGHT), false);
            }
            prop(g, vec3(h.P(s * (hw - 1.f), v1 - 1.f), zt), 0.f, 0.9f, PROP_PLANTER, 1);
        }
        // steps down to the sidewalk
        for (int k = 0; k < 3; k++) {
            float zs = zt - (k + 1) * 0.15f;
            boxY(g, vec3(h.P(0.f, v1 - 0.35f + k * 0.3f), zs + 0.075f - (k == 2 ? 0.1f : 0.f)), a, vec3(stepHalf, 0.16f, 0.075f + (k == 2 ? 0.1f : 0.f)),
                 trimC, M(MAT_MARBLE), false);
        }
        collide(g, vec3(tc, (h.z0 + zt) * 0.5f - 0.05f), f, vec3(thd, hw, (zt - h.z0) * 0.5f));
        if (detail) {
            // cafe: tables, chairs and umbrellas
            std::vector<TableSpot> tabs;
            terraceTables(h, tabs);
            u32 chairC = (h.seed & 1) ? rgb(0.95f) : accC;
            u32 topC = (h.seed & 2) ? rgb(0.92f, 0.9f, 0.86f) : rgb(0.25f, 0.24f, 0.22f);
            vec3 u0 = pl.accent, u1 = vec3(0.97f, 0.96f, 0.93f);
            for (size_t ti = 0; ti < tabs.size(); ti++) {
                const TableSpot& t = tabs[ti];
                vec2 tpw = h.P(t.uv.x, t.uv.y);
                cafeTable(g, tpw, zt, 0.36f, topC, (h.seed & 2) ? M(MAT_MARBLE) : M(MAT_METAL_PAINTED));
                vec2 cp[4], cf[4];
                tableChairs(h, t, cp, cf);
                for (int k = 0; k < t.seats; k++) bistroChair(g, cp[k], zt, cf[k], chairC);
                bool alt = (ti & 1) != 0;
                marketUmbrella(g, tpw, zt, 1.25f, a, rgbv(alt ? u1 : u0), rgbv(alt ? u0 : u1), (ti % 3) == 0 ? word : nullptr, rgbv(alt ? u0 : u1));
                // table lamp (a little glow at night)
                lamp(g, vec3(tpw, zt + 0.82f), 0.07f, vec3(1.f, 0.7f, 0.4f), 0.5f, EA_NIGHT);
            }
            // wide fronts: potted palms along the terrace wall beyond the last tables
            {
                int perSide = 0;
                for (float u = h.cw * 0.5f + 2.5f; u < hw - 1.3f; u += 3.1f) perSide++;
                if (perSide > 4)
                    for (int s = -1; s <= 1; s += 2)
                        for (float u = h.cw * 0.5f + 2.5f + 4 * 3.1f; u < hw - 2.2f; u += 2.6f)
                            prop(g, vec3(h.P(s * u, v1 - 0.95f), zt), (float)s, 0.85f, PROP_PLANTER, (u8)((int)(u * 7.f) & 1));
            }
            // menu board and host stand at the steps
            vec2 hs = h.P(stepHalf + 0.8f, v1 - 0.9f);
            boxY(g, vec3(hs, zt + 0.55f), a, vec3(0.28f, 0.2f, 0.55f), rgb(0.25f, 0.18f, 0.12f), M(MAT_WOOD), false);
            vec2 mb = h.P(-stepHalf - 0.9f, v1 + 0.9f);
            beam(g, vec3(mb, h.z0), vec3(mb + a * 0.1f, h.z0 + 1.05f), 0.55f, 0.04f, rgb(0.1f, 0.1f, 0.1f), M(MAT_WOOD), vec3(f, 0.f));
            // lights over the terrace
            for (int s = -1; s <= 1; s += 2) light(g, vec3(h.P(s * hw * 0.55f, (v0 + v1) * 0.5f), zt + 2.6f), vec3(1.f, 0.78f, 0.5f) * 1800.f, 8.f, 1);
            // colour wash under the eyebrows (neon glow on the facade)
            light(g, vec3(h.P(0.f, frontV + 2.5f), zt + h.gh + 1.f), ncol * 2600.f, 14.f, 1, normalize(vec3(-f * 0.4f, 1.f)), 0.2f);
        }
    }
    // uplights at the base washing the pastel front (warm white)
    if (detail)
        for (int s = -1; s <= 1; s += 2)
            light(g, vec3(h.P(s * hw * 0.7f, frontV + 0.6f), zt + 0.2f), vec3(1.f, 0.82f, 0.62f) * 3500.f, 16.f, 1, normalize(vec3(f * 0.25f, 1.f)), 0.15f);
}

// ------------------------------------------------------------------------------------------------ beachfront park
// Park strip between the avenue's sidewalk and the sand: lawns under royal palms, a serpentine promenade walk, a low
// coquina wall on the beach side with openings, beach crossings at each street, deco lamp posts, benches facing the
// ocean, a beach pavilion kiosk, a lifeguard stand and showers. p[0] = sidewalk edge x offset along e.ax (m from c),
// hx: half length along the avenue, hy: half width; e.ax points to the sea; e.z: ground level; p[1] = crossing flags,
// p[2..4]: crossing positions (u, along) of up to three streets; variant 1: kiosk; e.text: the sign.
struct Park {
    vec2 c, s, a;    // centre, to the sea, along the avenue
    float L, Wd, z;
};
Park parkOf(const SiteElem& e) {
    Park p;
    p.c = e.c;
    p.s = e.ax;
    p.a = perp(e.ax);
    p.L = e.hx;
    p.Wd = e.hy;
    p.z = e.z;
    return p;
}
// Serpentine walk centreline (v across the strip, from -Wd .. Wd) at along-position u
inline float walkV(const Park& p, float u) { return -p.Wd * 0.18f + sinf((u + p.c.y * 0.37f) / 23.f) * p.Wd * 0.24f; }

void genDecoPark(const SiteElem& e, G& g) {
    Park p = parkOf(e);
    bool detail = g.detail;
    auto P = [&](float u, float v) { return p.c + p.a * u + p.s * v; };
    const float z = p.z;
    // lawn (terrain drape; the ground was levelled under the park)
    drapeRect(g, p.c, p.a, p.L, p.Wd, 0.05f, rgb(0.6f, 0.93f, 0.5f), M(MAT_GRASS), detail ? 6.f : 18.f);
    // serpentine promenade walk
    std::vector<vec2> walk;
    for (float u = -p.L; u <= p.L + 0.01f; u += 3.f) walk.push_back(P(u, walkV(p, u)));
    u32 walkC = rgb(1.f, 0.9f, 0.85f);
    pathStrip(g, walk, 4.2f, 0.1f, walkC, M(MAT_PAVERS), -1e9f, true, true);
    // crossings: from the sidewalk edge across the lawn to the sand at each street end
    int nc = Clamp((int)e.p[1], 0, 3);
    for (int k = 0; k < nc; k++) {
        float u = e.p[2 + k];
        std::vector<vec2> cr = {P(u, -p.Wd), P(u, p.Wd + 3.f)};
        pathStrip(g, cr, 3.f, 0.1f, walkC, M(MAT_PAVERS), -1e9f, true, true);
        // dune boardwalk onto the sand
        vec2 b0 = P(u, p.Wd + 3.f), b1 = P(u, p.Wd + 14.f);
        if (g.owns((b0 + b1) * 0.5f)) {
            float zb = gMap->heightAt(b0.x, b0.y) + 0.35f;
            for (float t = 0.f; t < 11.f; t += 0.6f) {
                vec2 q = b0 + p.s * t;
                float zq = Max(gMap->heightAt(q.x, q.y) + 0.3f, zb - t * 0.03f);
                boxY(g, vec3(q + p.s * 0.25f, zq), p.a, vec3(1.3f, 0.26f, 0.03f), rgb(0.62f, 0.5f, 0.38f), M(MAT_WOOD), false);
            }
            if (detail) {
                // beach shower post at the edge of the walk
                vec2 sp = P(u + 2.3f, p.Wd + 2.4f);
                cyl(g, vec3(sp, z), 0.06f, 0.06f, 2.4f, 6, rgb(0.85f), M(MAT_METAL_BRUSHED), false);
                beam(g, vec3(sp, z + 2.35f), vec3(sp - p.s * 0.45f, z + 2.35f), 0.05f, 0.05f, rgb(0.85f), M(MAT_METAL_BRUSHED));
                cyl(g, vec3(sp - p.s * 0.45f, z + 2.2f), 0.09f, 0.06f, 0.12f, 6, rgb(0.8f), M(MAT_CHROME), true);
                polyFlat(g, *g.m, rectPoly(sp - p.s * 0.45f, p.a, 0.7f, 0.7f), z + 0.06f, rgb(0.6f, 0.55f, 0.5f), M(MAT_WOOD));
            }
        }
    }
    // walk to Club Riptide's entrance steps (no boardwalk: the club's own deck starts there)
    if (e.p[6] < 9000.f) pathStrip(g, {P(e.p[6], walkV(p, e.p[6])), P(e.p[6], p.Wd + 1.2f)}, 3.4f, 0.1f, walkC, M(MAT_PAVERS), -1e9f, true, true);
    // coquina seawall along the sand side, with openings at the crossings
    {
        float v = p.Wd - 0.4f;
        std::vector<float> gaps;
        for (int k = 0; k < nc; k++) gaps.push_back(e.p[2 + k]);
        if (e.p[6] < 9000.f) gaps.push_back(e.p[6]);
        float u = -p.L;
        while (u < p.L) {
            float u1 = Min(p.L, u + 6.f);
            bool gap = false;
            for (float gu : gaps)
                if (u1 > gu - 2.2f && u < gu + 2.2f) gap = true;
            if (!gap) {
                vec2 a0 = P(u, v + sinf(u * 0.09f) * 0.5f), a1 = P(u1, v + sinf(u1 * 0.09f) * 0.5f);
                if (g.owns((a0 + a1) * 0.5f)) {
                    float za = gMap->heightAt(a0.x, a0.y), zb = gMap->heightAt(a1.x, a1.y);
                    copedWall(g, a0, a1, Min(za, zb) - 0.2f, 0.75f + fabsf(za - zb), 0.45f, rgb(0.93f, 0.86f, 0.72f), M(MAT_STONE), rgb(0.96f, 0.93f, 0.85f),
                              M(MAT_CONCRETE), true);
                }
                u = u1;
            } else u = u1 + 0.01f;
        }
    }
    if (!detail) {
        // far LOD: palms read as trunks with a crown blob are handled by props only up close; keep the lamp glows
        for (float u = -p.L + 10.f; u < p.L; u += 20.f) {
            vec2 lp = P(u, walkV(p, u) + 2.6f);
            if (g.owns(lp)) lamp(g, vec3(lp, z + 4.f), 0.4f, vec3(1.f, 0.9f, 0.7f), 0.6f, EA_NIGHT);
        }
        return;
    }
    // royal palms in two rows, sea grape and lawn palms, deco lamps and benches along the walk
    Rng r(e.seed);
    for (float u = -p.L + 5.f; u < p.L - 2.f; u += 9.5f) {
        for (int row = 0; row < 2; row++) {
            float v = row == 0 ? -p.Wd + 2.2f : p.Wd - 3.4f;
            vec2 pp = P(u + (row ? 4.7f : 0.f), v);
            bool nearCross = false;
            for (int k = 0; k < nc; k++)
                if (fabsf(u + (row ? 4.7f : 0.f) - e.p[2 + k]) < 3.f) nearCross = true;
            if (nearCross || !g.owns(pp)) continue;
            prop(g, vec3(pp, gMap->heightAt(pp.x, pp.y)), r.f() * kTwoPi, r.range(1.05f, 1.25f), PROP_PALM_TALL, (u8)r.irange(0, 2));
        }
    }
    for (float u = -p.L + 12.f; u < p.L - 6.f; u += 16.f) {
        float wv = walkV(p, u);
        vec2 lp = P(u, wv + 2.6f);
        if (g.owns(lp)) decoLamp(g, lp, gMap->heightAt(lp.x, lp.y), 4.6f, vec3(1.f, 0.88f, 0.68f), rgb(0.16f, 0.36f, 0.36f));
        vec2 bp = P(u + 6.f, walkV(p, u + 6.f) + 2.9f);
        bool nearCross = false;
        for (int k = 0; k < nc; k++)
            if (fabsf(u + 6.f - e.p[2 + k]) < 3.5f) nearCross = true;
        if (!nearCross && g.owns(bp)) prop(g, vec3(bp, gMap->heightAt(bp.x, bp.y)), yawFacing(p.s), 1.f, PROP_BENCH);
        // sea grape shrubs on the lawn toward the wall
        vec2 sg = P(u + r.range(-4.f, 4.f), p.Wd - 5.5f + r.range(-1.f, 1.f));
        if (g.owns(sg) && r.chance(0.7f)) {
            float zz = gMap->heightAt(sg.x, sg.y);
            leafBlob(*g.m, g.org, vec3(sg, zz + 0.7f), r.range(1.2f, 1.8f), 0.8f, vec3(0.42f, 0.66f, 0.32f), r.next(), 0.14f);
        }
    }
    // pavilion kiosk on the walk (juice / gelato / beach rentals) with a neon word
    if (e.variant == 1) {
        vec2 kc = P(e.p[5], walkV(p, e.p[5]) - 5.5f);
        if (g.owns(kc)) {
            float kz = gMap->heightAt(kc.x, kc.y) + 0.1f;
            polyFlat(g, *g.m, circleFP(kc, 4.6f, 24), kz, rgb(0.98f, 0.9f, 0.85f), M(MAT_PAVERS));
            cyl(g, vec3(kc, kz), 2.4f, 2.4f, 3.2f, 20, rgb(0.97f, 0.95f, 0.92f), M(MAT_STUCCO), false);
            // service windows (glass) round the drum
            for (int k = 0; k < 4; k++) {
                float an = kHalfPi * k + 0.4f;
                vec2 d(cosf(an), sinf(an));
                vec2 t = perp(d);
                vec2 w0 = kc + d * 2.42f - t * 0.8f, w1 = kc + d * 2.42f + t * 0.8f;
                quad(g, *g.m, vec3(w0, kz + 1.f), vec3(w1, kz + 1.f), vec3(w1, kz + 2.3f), vec3(w0, kz + 2.3f), rgb(0.3f, 0.36f, 0.38f), M(MAT_GLASS), vec3(d, 0));
                quad(g, *g.m, vec3(w0 + d * 0.01f, kz + 2.3f), vec3(w1 + d * 0.01f, kz + 2.3f), vec3(w1 + d * 0.01f, kz + 2.5f), vec3(w0 + d * 0.01f, kz + 2.5f),
                     rgb(1.f, 0.85f, 0.6f, 0.08f), emMat(EA_NIGHT), vec3(d, 0));
            }
            // flat round roof with a deep rim and a neon ring
            cyl(g, vec3(kc, kz + 3.2f), 3.6f, 3.6f, 0.28f, 24, rgb(0.3f, 0.75f, 0.75f), M(MAT_PLASTER), true);
            for (int k = 0; k < 24; k++) {
                float a0 = kTwoPi * k / 24.f, a1 = kTwoPi * (k + 1) / 24.f;
                neonTube(g, vec3(kc + vec2(cosf(a0), sinf(a0)) * 3.63f, kz + 3.28f), vec3(kc + vec2(cosf(a1), sinf(a1)) * 3.63f, kz + 3.28f), 0.025f,
                         vec3(1.f, 0.3f, 0.65f), EA_NIGHT);
            }
            cyl(g, vec3(kc, kz + 3.48f), 0.9f, 0.5f, 1.2f, 12, rgb(0.97f), M(MAT_STUCCO), true);
            const char* kw = e.text.empty() ? "GELATO" : e.text.c_str();
            for (int face = 0; face < 2; face++) {
                vec2 d = face ? -p.a : p.a;
                vec2 fn = face ? p.s : -p.s;
                (void)d;
                vec2 rt = perp(fn);
                float th = 0.55f;
                vec3 o = vec3(kc + fn * 3.66f, kz + 3.55f) - vec3(rt * (textAdvance(kw, th, 0.3f) * 0.5f), 0.f);
                neonLetters(g, kw, o, vec3(rt, 0.f), vec3(0, 0, 1), th, vec3(0.3f, 0.95f, 1.f), EA_NIGHT, 0, 0.04f);
            }
            collide(g, vec3(kc, kz + 1.6f), vec2(1, 0), vec3(1.7f, 1.7f, 1.6f));
            light(g, vec3(kc, kz + 2.9f), vec3(1.f, 0.4f, 0.75f) * 3000.f, 10.f, 2);
            // two parasol tables beside it
            for (int s = -1; s <= 1; s += 2) {
                vec2 tp = kc + p.a * (s * 5.4f) - p.s * 0.5f;
                cafeTable(g, tp, kz, 0.4f, rgb(0.95f), M(MAT_METAL_PAINTED));
                for (int q = 0; q < 2; q++) bistroChair(g, tp + p.a * ((q ? 0.6f : -0.6f)), kz, p.a * (q ? -1.f : 1.f), rgb(0.95f, 0.5f, 0.6f));
                marketUmbrella(g, tp, kz, 1.2f, p.a, rgb(1.f, 0.55f, 0.7f), rgb(0.98f), nullptr, 0);
            }
        }
    }
    // lifeguard stand on the sand opposite the middle of the park
    if (e.variant == 2) {
        vec2 lg = P(0.f, p.Wd + 22.f);
        if (g.owns(lg) && !gMap->isWater(lg.x, lg.y)) prop(g, vec3(lg, gMap->heightAt(lg.x, lg.y)), yawFacing(p.s) + kPi, 1.f, PROP_LIFEGUARD_TOWER);
    }
    // the promenade's sign at the south end of the first park block
    if (!e.text.empty() && e.variant != 1) {
        vec2 sc = P(-p.L + 6.f, -p.Wd + 3.5f);
        if (g.owns(sc)) {
            float sz = gMap->heightAt(sc.x, sc.y);
            // deco pylon: tall slab with fins, name vertical, neon edges
            vec2 fn = -p.s;
            boxY(g, vec3(sc, sz + 2.6f), p.a, vec3(0.9f, 0.35f, 2.6f), rgb(0.98f, 0.93f, 0.85f), M(MAT_STUCCO), false);
            for (int s = -1; s <= 1; s += 2) boxY(g, vec3(sc + p.a * (s * 1.0f), sz + 2.1f), p.a, vec3(0.1f, 0.5f, 2.1f), rgb(0.3f, 0.72f, 0.72f), M(MAT_PLASTER), false);
            collide(g, vec3(sc, sz + 2.6f), p.a, vec3(1.1f, 0.5f, 2.6f));
            const char* t1 = "OCEAN";
            const char* t2 = "PROMENADE";
            vec2 rt = perp(fn);
            neonLetters(g, t1, vec3(sc + fn * 0.36f, sz + 3.7f) - vec3(rt * (textAdvance(t1, 0.42f, 0.3f) * 0.5f), 0.f), vec3(rt, 0.f), vec3(0, 0, 1), 0.42f,
                        vec3(1.f, 0.3f, 0.6f), EA_NIGHT, 0, 0.04f);
            neonLetters(g, t2, vec3(sc + fn * 0.36f, sz + 3.05f) - vec3(rt * (textAdvance(t2, 0.26f, 0.3f) * 0.5f), 0.f), vec3(rt, 0.f), vec3(0, 0, 1), 0.26f,
                        vec3(0.3f, 0.95f, 1.f), EA_NIGHT, 0, 0.03f);
            plainLetters(g, "SOL BEACH", vec3(sc + fn * 0.36f, sz + 1.2f) - vec3(rt * (textAdvance("SOL BEACH", 0.3f, 0.3f) * 0.5f), 0.f), vec3(rt, 0.f),
                         vec3(0, 0, 1), 0.3f, rgb(0.2f, 0.5f, 0.52f), M(MAT_METAL_PAINTED));
            light(g, vec3(sc + fn * 2.f, sz + 0.3f), vec3(1.f, 0.9f, 0.8f) * 2500.f, 9.f, 1, normalize(vec3(-fn, 1.2f)), 0.2f);
        }
    }
}

// ------------------------------------------------------------------------------------------------ layout
void layout(SiteSet& S, WorldMap& map) {
    // The avenue lies 75 m inland of the first beach texel west of x = 5600 (roads.cpp, Sol Beach avenues): sample it the
    // same way and keep the blocks where it runs straight.
    auto shoreX = [&](float y) {
        for (float x = 5600.f; x > 4700.f; x -= 8.f)
            if (!map.isWater(x, y) && map.regionAt(x, y) == REG_BEACH) return x;
        return 5500.f;
    };
    const float kOffset = 75.f;
    const float yS[5] = {680.f, 775.f, 870.f, 965.f, 1060.f};   // 37th .. 41st Street (every 95 m from y = -2550)
    const float kHW = 5.6f, kSW = 3.f;                           // street half width and sidewalk
    float promX = -1.f;
    bool straight = true;
    // (the curve's control points 25 m apart: the first one inside a straight run can still lean off by half a metre)
    for (float y = -2600.f; y <= 4400.f; y += 25.f) {
        if (y < yS[0] - 10.f || y > yS[4] + 20.f) continue;
        float x = shoreX(y) - kOffset;
        if (promX < 0.f) promX = x;
        else if (fabsf(x - promX) > 0.5f) straight = false;
    }
    if (!straight || promX < 0.f) {
        std::string dbg;
        for (float y = -2600.f; y <= 4400.f; y += 25.f)
            if (y >= yS[0] - 10.f && y <= yS[4] + 20.f) dbg += StrFormat(" %.0f:%.0f", y, shoreX(y) - kOffset);
        LOG("Places: Ocean Promenade not straight between 37th and 41st Street, hotel row left out:%s", dbg.c_str());
        return;
    }
    // level ground under the hotel row, the avenue and the park
    float z0 = 0.f;
    {
        float s = 0.f;
        int n = 0;
        for (float y = yS[0]; y <= yS[4]; y += 10.f)
            for (float x = promX - 55.f; x <= promX + 50.f; x += 10.f) {
                s += map.heightAt(x, y);
                n++;
            }
        z0 = roundf(Clamp(s / Max(1, n), 0.9f, 4.f) * 10.f) / 10.f;
    }
    // (kept 17 m inside the first beach texel the avenue is measured from, so the levelling cannot move the avenue)
    map.flattenRect(vec2(promX - 5.f, (yS[0] + yS[4]) * 0.5f), vec2(1, 0), 55.f, (yS[4] - yS[0]) * 0.5f + 12.f, z0, 12.f);
    const float zRoad = z0 + 0.25f, zWalk = zRoad + 0.15f;   // the avenue's surface and sidewalk (roads.cpp: terrain + 0.25)
    const float swEdge = promX - kHW - kSW;                   // hotel-side sidewalk edge
    // reserve the land: no generic lots, no scattered vegetation, no beach access boardwalks across the park
    S.lotBlocks.push_back({vec2((promX - 62.f + swEdge) * 0.5f, (yS[0] + yS[4]) * 0.5f), vec2(1, 0), (swEdge - (promX - 62.f)) * 0.5f, (yS[4] - yS[0]) * 0.5f - 1.f});
    S.vegBlocks.push_back({vec2((promX - 62.f + swEdge) * 0.5f, (yS[0] + yS[4]) * 0.5f), vec2(1, 0), (swEdge - (promX - 62.f)) * 0.5f, (yS[4] - yS[0]) * 0.5f});
    float parkX0 = promX + kHW + kSW + 0.1f, parkX1 = promX + 55.f;
    float parkY0 = yS[0] + 1.f, parkY1 = yS[4] - kHW - kSW - 1.f;
    S.lotBlocks.push_back({vec2((parkX0 + parkX1 + 30.f) * 0.5f, (parkY0 + parkY1) * 0.5f), vec2(1, 0), (parkX1 + 30.f - parkX0) * 0.5f, (parkY1 - parkY0) * 0.5f});
    S.vegBlocks.push_back({vec2((parkX0 + parkX1) * 0.5f, (parkY0 + parkY1) * 0.5f), vec2(1, 0), (parkX1 - parkX0) * 0.5f, (parkY1 - parkY0) * 0.5f});
    // ---------------------------------------------------------------- hotels
    const vec2 front(1, 0);          // the hotels face east across the avenue to the park and the sea
    const float terrace = 6.2f;      // porch depth in front of the facade
    const float frontPlane = swEdge - 0.3f - terrace;
    int placeIdx = (int)S.places.size();
    {
        NamedPlace np;
        np.name = "Ocean Promenade";
        np.kind = PK_HOTEL_ROW;
        np.pos = vec2(promX, (yS[0] + yS[4]) * 0.5f);
        np.door = vec2(promX, yS[0]);
        np.radius = (yS[4] - yS[0]) * 0.5f + 40.f;
        S.places.push_back(np);
    }
    Rng rng(0xDEC05EA5u);
    int nameIdx = 0, prevPal = -1, prevTpl = -1;
    u16 group = 1;   // group 0: the strollers' chain along the park walk
    int built = 0;
    for (int b = 0; b < 4; b++) {
        float y0 = yS[b] + kHW + kSW + 1.f, y1 = yS[b + 1] - kHW - kSW - 1.f;
        float span = y1 - y0;
        // two or three hotels per block, one of them the wide flagship
        int n = (b == 1) ? 2 : 3;
        float w[3];
        if (n == 2) { w[0] = span * 0.56f; w[1] = span - w[0] - 1.2f; }
        else {
            float mid = span * rng.range(0.36f, 0.42f);
            w[1] = mid;
            w[0] = (span - mid - 2.4f) * rng.range(0.45f, 0.55f);
            w[2] = span - mid - 2.4f - w[0];
        }
        float y = y0;
        for (int k = 0; k < n; k++) {
            float W = w[k];
            float yc = y + W * 0.5f;
            y += W + 1.2f;
            int tpl = (built * 3 + b) % T_COUNT;
            if (W > 30.f && tpl == T_TWINFIN) tpl = T_ZIGGURAT;
            if (tpl == prevTpl) tpl = (tpl + 1) % T_COUNT;
            int pal = (built * 5 + 3) % 8;
            if (pal == prevPal) pal = (pal + 1) % 8;
            prevTpl = tpl;
            prevPal = pal;
            int floors = W > 36.f ? 5 : (W > 30.f ? 4 : (rng.chance(0.5f) ? 4 : 3));
            float depth = rng.range(27.f, 33.f);
            int corners = (k == 0 ? 1 : 0) | (k == n - 1 ? 2 : 0);
            // along axis a = perp(front) = (0, 1): the -a end is the south end
            SiteElem e;
            e.kind = SK_DECO_HOTEL;
            e.variant = (u16)tpl;
            e.seed = hash32(0xDEC0u + (u32)built * 7919u);
            e.ax = front;
            e.hx = W * 0.5f;
            e.hy = depth * 0.5f;
            e.c = vec2(frontPlane - depth * 0.5f, yc);
            e.z = zWalk;
            e.p[0] = (float)floors;
            e.p[1] = terrace;
            e.p[2] = tpl == T_TOWER ? rng.range(7.f, 9.5f) : (tpl == T_TWINFIN ? rng.range(4.5f, 6.f) : 0.f);
            e.p[3] = (float)pal;
            e.p[4] = (float)(nameIdx++ % 12);
            e.p[5] = (float)corners;
            e.text = kNames[(int)e.p[4]];
            const Hotel h = hotelOf(e);
            e.h = h.H + 0.45f + Max(e.p[2], 2.5f) + 4.f;
            int ei = (int)S.elems.size();
            S.elems.push_back(e);
            SiteBuildingReq q;
            q.c = e.c;
            q.ax = perp(front);
            q.hx = W * 0.5f;
            q.hy = depth * 0.5f;
            q.style = BS_DECO;
            q.roof = ROOF_FLAT;
            q.floors = (u16)floors;
            q.front = front;
            q.baseZ = zWalk + 0.45f - 0.15f;   // addSiteBuilding adds 0.15: the ground floor sits on the terrace
            q.region = REG_BEACH;
            q.siteElem = ei;
            S.buildingReqs.push_back(q);
            // walkable terrace
            {
                Pad pd;
                pd.c = h.P(0.f, h.D * 0.5f + terrace * 0.5f);
                pd.ax = front;
                pd.hx = terrace * 0.5f;
                pd.hy = W * 0.5f - 0.1f;
                pd.z = h.zt;
                pd.slope = 0.f;
                pd.kind = PAD_PLAZA;
                pd.drawn = 0;
                pd.skirt = 0;
                pd.flags = 1;
                pd.color = 0xffffffffu;
                S.pads.push_back(pd);
            }
            // cafe patrons: every chair a seat, one table one group; a waiter's spot by the host stand
            std::vector<TableSpot> tabs;
            terraceTables(h, tabs);
            for (const TableSpot& t : tabs) {
                vec2 cp[4], cf[4];
                tableChairs(h, t, cp, cf);
                for (int s = 0; s < t.seats; s++) S.anchors.push_back({vec3(cp[s], h.zt), cf[s], PA_SIT, (u8)placeIdx, group});
                group++;
            }
            S.anchors.push_back({h.P3(h.cw * 0.5f + 1.7f, h.D * 0.5f + terrace - 1.4f, h.zt), -h.f, PA_WORK, (u8)placeIdx, group++});
            built++;
        }
    }
    // ---------------------------------------------------------------- park blocks across the avenue
    const vec2 sea(1, 0);
    float pw = (parkX1 - parkX0) * 0.5f;
    for (int b = 0; b < 4; b++) {
        float y0 = b == 0 ? parkY0 : yS[b], y1 = b == 3 ? parkY1 : yS[b + 1];
        SiteElem e;
        e.kind = SK_DECO_PARK;
        e.variant = (u16)(b == 1 ? 1 : (b == 2 ? 2 : 0));
        e.seed = hash32(0x9A2Cu + (u32)b);
        e.ax = sea;
        e.c = vec2((parkX0 + parkX1) * 0.5f, (y0 + y1) * 0.5f);
        e.hx = (y1 - y0) * 0.5f;
        e.hy = pw;
        e.z = z0;
        e.h = 8.f;
        // crossings at the streets that end at the avenue inside this block (along: u = y - centre; a = perp(sea) = +y)
        int nc = 0;
        for (int k = 0; k < 5 && nc < 3; k++) {
            float u = yS[k] - e.c.y;
            if (fabsf(u) < e.hx - 2.f) e.p[2 + nc++] = u;
        }
        e.p[1] = (float)nc;
        // Club Riptide's gate (interiors.cpp planClub: its entrance steps face the avenue at y = 900) gets its own walk
        e.p[6] = (900.f > y0 + 3.f && 900.f < y1 - 3.f) ? 900.f - e.c.y : 9999.f;
        e.p[5] = 0.f;
        if (e.variant == 1) {
            e.p[5] = -e.hx * 0.35f;
            e.text = "GELATO";
        }
        if (b == 0) e.text = "OCEAN PROMENADE";
        S.elems.push_back(e);
        // benches and the lawn for the ambient crowd; the walk's waypoints for strollers
        for (float u = -e.hx + 12.f; u < e.hx - 6.f; u += 16.f) {
            vec2 bp = e.c + perp(sea) * (u + 6.f) + sea * (walkV(parkOf(e), u + 6.f) + 2.9f);
            bool benchHere = true;   // genDecoPark leaves the bench out beside a crossing
            for (int k = 0; k < nc; k++)
                if (fabsf(u + 6.f - e.p[2 + k]) < 3.5f) benchHere = false;
            if (benchHere) S.anchors.push_back({vec3(bp, map.heightAt(bp.x, bp.y)), sea, PA_SIT, (u8)placeIdx, group++});
            vec2 wp = e.c + perp(sea) * u + sea * walkV(parkOf(e), u);
            S.anchors.push_back({vec3(wp, map.heightAt(wp.x, wp.y) + 0.1f), perp(sea), PA_WAYPOINT, (u8)placeIdx, 0});
            vec2 lp = e.c + perp(sea) * (u + 8.f) + sea * (e.hy - 7.f);   // on the lawn between the sea grapes
            if (u + 8.f < e.hx - 3.f) S.anchors.push_back({vec3(lp, map.heightAt(lp.x, lp.y)), sea, PA_SIT_GROUND, (u8)placeIdx, group++});
        }
    }
    LOG("Places: Ocean Promenade hotel row, %d hotels at x %.0f (ground %.1f), park %.0f..%.0f", built, promX, z0, parkY0, parkY1);
}

// Facade records: pastel stucco with deco eyebrows (shader style 6), storefront lobby and cafe on the ground floor
void facades(SiteSet& S, BuildingSet& bs) {
    for (size_t bi = 0; bi < bs.buildings.size(); bi++) {
        Building& b = bs.buildings[bi];
        if (b.siteElem < 0 || b.siteElem >= (int)S.elems.size()) continue;
        SiteElem& e = S.elems[b.siteElem];
        if (e.kind != SK_DECO_HOTEL) continue;
        Hotel h = hotelOf(e);
        const Palette& pl = kPal[h.pal];
        FacadeGPU& f = bs.facades[b.facade];
        f.floorH = h.fh;
        f.groundH = h.gh;
        f.bayW = h.bay;
        f.winW = 0.56f;
        f.winH = h.winH / h.fh;
        f.sillH = h.sill;
        f.roomDepth = 5.5f;
        f.style = 6.f;
        f.wallColor = packRGBA8(pl.wall.x, pl.wall.y, pl.wall.z, 1.f);
        f.frameColor = packRGBA8(pl.accent.x, pl.accent.y, pl.accent.z, 1.f);
        f.glassColor = packRGBA8(0.55f, 0.72f, 0.78f, 1.f);
        f.flags = 1u;   // storefront ground floor (lobby and cafe behind glass); the neon is modelled
        f.litFrac = 0.72f;
        f.wallLayer = (float)MAT_STUCCO;
        f.seed = e.seed;
        b.height = h.H;
        b.baseZ = h.zt;
        e.p[6] = (float)bi;
        e.p[7] = (float)b.facade;
    }
}

}  // namespace deco_strip
}  // namespace World
