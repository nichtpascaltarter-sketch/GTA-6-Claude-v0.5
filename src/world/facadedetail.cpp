// Street-level building detail for full-detail (LOD0) cells.
//   Moldings: plinths, ground-floor string courses, sill courses, cornices (four profiles), parapet coping, pilasters.
//   Openings: window sills/heads/jambs/keystones, shutters, storefront piers, mullions, transoms, bulkheads and doors,
//             house doors with stoops, porch roofs and villa porticos.
//   Street furniture on the walls: scalloped striped awnings, roll-down security gates, neon blade signs and window neon,
//             balconies with railings, fire escapes on brick midrises, window AC units, condensers and downpipes,
//             graffiti in Calle Luna and the Flats.
//   Style specials: art deco fins, speed lines and ziggurat crests; tower fins and spandrel ledges; condo unit dividers;
//             warehouse doors, dock bumpers and wall lights.
//   Gardens: lawns, hedges, flower beds, bougainvillea, villa garden walls, mailboxes.
// Openings follow the facade shader's window grid exactly (shaders/facade.hlsli): every wall starts on a bay boundary and
// stretches round(len / bayW) bays to its length (buildmesh.cpp facadeWalls), floors count up from the building base.
#include "buildings.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace facade_detail {

// ------------------------------------------------------------------------------------------------ shared helpers
// Mirrors of the shader hashes (common.hlsli) so geometry agrees with per-room randomness (industrial windows)
inline u32 shHashU(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
inline u32 shHash3u(u32 a, u32 b, u32 c) { return shHashU(a * 0x8da6b343u ^ b * 0xd8163841u ^ shHashU(c + 0x7f4a7c15u)); }
inline float shHashF(u32 x) { return (shHashU(x) >> 8) * (1.f / 16777216.f); }

inline vec3 rgbOf(u32 c) { return vec3((float)(c & 255u), (float)((c >> 8) & 255u), (float)((c >> 16) & 255u)) * (1.f / 255.f); }
inline u32 pk(vec3 c, float a = 1.f) { return packRGBA8(Clamp(c.x, 0.f, 1.f), Clamp(c.y, 0.f, 1.f), Clamp(c.z, 0.f, 1.f), a); }
inline u32 pk(float r, float g, float b, float a = 1.f) { return pk(vec3(r, g, b), a); }
inline u32 pk(float v) { return pk(vec3(v, v, v), 1.f); }
inline u32 MM(MaterialId id, u32 param = 0) { return makeMat(id, param); }
inline u32 neonMat(u32 anim, u32 phase) { return makeMat(MAT_EMISSIVE, anim == 0 ? 0u : (anim | ((phase & 255u) << 4))); }

enum : u32 { WF_FRONT = 1, WF_TOP = 2, WF_BOTTOM = 4, WF_START = 8, WF_END = 16, WF_BACK = 32, WF_BOX = 31, WF_ALL = 63,
             WF_LEDGE = WF_FRONT | WF_TOP | WF_BOTTOM,     // long horizontal molding (ends hidden at corners / in neighbours)
             WF_POST = WF_FRONT | WF_START | WF_END };     // vertical member

struct Sink {
    MeshData* m = nullptr;
    vec3 org;
    std::vector<CollisionBox>* col = nullptr;
    std::vector<PropInstance>* props = nullptr;
    std::vector<LightInstance>* lights = nullptr;
};

// A facade wall: outward normal n for CCW footprints, bay grid identical to facadeWalls()
struct Wall {
    vec2 a, b, t, n;
    float len = 0.f, bw = 1.f;
    int bays = 1, idx = 0, kStart = 0;
    float facing = 0.f;  // dot(n, building front)
};

Wall makeWall(const std::vector<vec2>& fp, int i, float bay, vec2 front) {
    Wall w;
    int n = (int)fp.size();
    w.a = fp[i];
    w.b = fp[(i + 1) % n];
    vec2 d = w.b - w.a;
    w.len = length(d);
    w.t = w.len > 1e-4f ? d / w.len : vec2(1, 0);
    w.n = vec2(w.t.y, -w.t.x);
    w.bays = (int)Max(1.f, roundf(w.len / bay));
    w.bw = w.len / (float)w.bays;
    w.idx = i;
    w.kStart = (int)floorf((float)i * 1000.f / bay);
    w.facing = dot(w.n, front);
    return w;
}

inline vec3 WP(const Sink& k, const Wall& w, float s, float z, float o) { return vec3(w.a + w.t * s + w.n * o, z) - k.org; }

// Box attached to a wall: s along the wall, z up, o out of the wall plane. Only the requested faces are emitted.
void wbox(Sink& k, const Wall& w, float s0, float s1, float z0, float z1, float o0, float o1, u32 col, u32 mat, u32 faces = WF_BOX) {
    if (s1 - s0 < 1e-3f || z1 - z0 < 1e-3f || o1 - o0 < 1e-4f) return;
    MeshData& m = *k.m;
    auto P = [&](float s, float z, float o) { return WP(k, w, s, z, o); };
    if (faces & WF_FRONT)
        m.quad(P(s0, z0, o1), P(s1, z0, o1), P(s1, z1, o1), P(s0, z1, o1), vec2(s0, z0), vec2(s1, z0), vec2(s1, z1), vec2(s0, z1), col, mat);
    if (faces & WF_TOP)
        m.quad(P(s0, z1, o1), P(s1, z1, o1), P(s1, z1, o0), P(s0, z1, o0), vec2(s0, o1), vec2(s1, o1), vec2(s1, o0), vec2(s0, o0), col, mat);
    if (faces & WF_BOTTOM)
        m.quad(P(s0, z0, o0), P(s1, z0, o0), P(s1, z0, o1), P(s0, z0, o1), vec2(s0, o0), vec2(s1, o0), vec2(s1, o1), vec2(s0, o1), col, mat);
    if (faces & WF_START)
        m.quad(P(s0, z0, o0), P(s0, z0, o1), P(s0, z1, o1), P(s0, z1, o0), vec2(o0, z0), vec2(o1, z0), vec2(o1, z1), vec2(o0, z1), col, mat);
    if (faces & WF_END)
        m.quad(P(s1, z0, o1), P(s1, z0, o0), P(s1, z1, o0), P(s1, z1, o1), vec2(o1, z0), vec2(o0, z0), vec2(o0, z1), vec2(o1, z1), col, mat);
    if (faces & WF_BACK)
        m.quad(P(s1, z0, o0), P(s0, z0, o0), P(s0, z1, o0), P(s1, z1, o0), vec2(s1, z0), vec2(s0, z0), vec2(s0, z1), vec2(s1, z1), col, mat);
}

// Flat quad on (or parallel to) the wall plane at offset o
void wquad(Sink& k, const Wall& w, float s0, float s1, float z0, float z1, float o, u32 col, u32 mat) {
    k.m->quad(WP(k, w, s0, z0, o), WP(k, w, s1, z0, o), WP(k, w, s1, z1, o), WP(k, w, s0, z1, o), vec2(s0, z0), vec2(s1, z0), vec2(s1, z1), vec2(s0, z1),
              col, mat);
}

// Arbitrary oriented box in world space (unit axes)
void obox(Sink& k, vec3 c, vec3 ax, vec3 ay, vec3 he, u32 col, u32 mat, bool bottom = true) {
    vec3 az = normalize(cross(ax, ay));
    k.m->box(c - k.org, ax, ay, az, he, col, mat, bottom);
}

void wcollide(Sink& k, const Wall& w, float s0, float s1, float z0, float z1, float o0, float o1) {
    if (!k.col) return;
    CollisionBox b;
    vec2 c = w.a + w.t * ((s0 + s1) * 0.5f) + w.n * ((o0 + o1) * 0.5f);
    b.c = vec3(c, (z0 + z1) * 0.5f);
    b.ax = w.t;
    b.he = vec3((s1 - s0) * 0.5f, (o1 - o0) * 0.5f, (z1 - z0) * 0.5f);
    k.col->push_back(b);
}

void addLight(Sink& k, vec3 p, vec3 color, float radius, u8 type, vec3 dir = vec3(0), float cone = 0.f) {
    if (!k.lights) return;
    LightInstance li;
    li.pos = p;
    li.color = color;
    li.radius = radius;
    li.dir = dir;
    li.cone = cone;
    li.type = type;
    k.lights->push_back(li);
}

// Floors of a mass, in facade v (meters above the building base)
struct FloorRow {
    float v0, h;
    int idx;  // shader floor index (0 = ground)
};
int massFloors(const FacadeGPU& f, float vLo, float vHi, FloorRow* out, int maxN) {
    int n = 0;
    for (int fi = 0; fi < 400 && n < maxN; fi++) {
        float v0 = fi == 0 ? 0.f : f.groundH + (fi - 1) * f.floorH;
        float h = fi == 0 ? f.groundH : f.floorH;
        if (v0 + h > vHi + 0.05f) break;
        if (v0 >= vLo - 0.05f) out[n++] = {v0, h, fi};
    }
    return n;
}

// Window rectangle of a floor as fractions of the bay (x0, w) and meters (sill, h), as the shader draws it
struct WinSpec {
    float x0, w, sill, h;
    bool store;
};
WinSpec winSpec(const FacadeGPU& f, bool ground) {
    float fh = ground ? f.groundH : f.floorH;
    int style = (int)f.style;
    WinSpec s;
    s.store = ground && (f.flags & 1u);
    float w = f.winW, h = fh * f.winH, sill = f.sillH;
    if (s.store) { w = 0.92f; sill = 0.35f; h = fh - 1.3f; }
    else if (style == 1) { w = 1.f - 0.12f / Max(f.bayW, 0.2f); h = fh - 0.1f; sill = 0.05f; }
    else if (style == 2) { w = 1.f; sill = fh * 0.38f; h = fh * 0.45f; }
    else if (style == 4) { h = fh * 0.25f; sill = fh * 0.6f; w = 0.5f; }
    s.w = w;
    s.x0 = (1.f - w) * 0.5f;
    s.sill = sill;
    s.h = h;
    return s;
}
bool industrialWindow(const FacadeGPU& f, const Wall& w, int bay, int floorIdx) {
    u32 room = shHash3u((u32)(w.kStart + bay + 1000), (u32)floorIdx, f.seed);
    return shHashF(room + 9u) >= 0.5f;
}

// Per-building context
struct FD {
    Sink k;
    const Building* b = nullptr;
    const FacadeGPU* f = nullptr;
    const WorldMap* map = nullptr;
    Rng r{1u};
    vec3 wallRGB;
    u32 wallTone = 0xffffffffu, wallMat = 0;
    u32 trim = 0xffffffffu, trimMat = 0, dark = 0xff808080u, frame = 0xff404040u, accent = 0xffffffffu;
    int reg = 0;
    bool old = false;      // older urban fabric: window AC units, security gates, fire escapes
    bool graffiti = false; // Calle Luna, the Flats
    size_t v0 = 0, budget = 9000;
    bool room() const { return k.m->verts.size() - v0 < budget; }
};

// ------------------------------------------------------------------------------------------------ moldings
enum Cornice { CO_BAND = 0, CO_CLASSIC, CO_SLAB, CO_STEPPED, CO_NONE };

void cornice(FD& d, const Wall& w, float H, int kind) {
    Sink& k = d.k;
    float e = 0.004f * (float)(w.idx & 1);  // keeps overlapping corner pieces from z-fighting
    switch (kind) {
        case CO_BAND: wbox(k, w, -0.12f, w.len + 0.12f, H - 0.34f + e, H + 0.04f + e, 0.f, 0.12f, d.trim, d.trimMat, WF_LEDGE); break;
        case CO_CLASSIC:
            wbox(k, w, -0.08f, w.len + 0.08f, H - 0.62f + e, H - 0.48f + e, 0.f, 0.08f, d.trim, d.trimMat, WF_LEDGE);
            wbox(k, w, -0.2f, w.len + 0.2f, H - 0.48f + e, H - 0.14f + e, 0.f, 0.2f, d.wallTone, d.wallMat, WF_LEDGE);
            wbox(k, w, -0.44f, w.len + 0.44f, H - 0.14f + e, H + 0.1f + e, 0.f, 0.44f, d.trim, d.trimMat, WF_LEDGE);
            break;
        case CO_SLAB: wbox(k, w, -0.8f, w.len + 0.8f, H - 0.12f + e, H + 0.14f + e, 0.f, 0.8f, d.trim, d.trimMat, WF_LEDGE); break;
        case CO_STEPPED:
            wbox(k, w, -0.1f, w.len + 0.1f, H - 0.42f + e, H - 0.27f + e, 0.f, 0.1f, d.accent, d.trimMat, WF_LEDGE);
            wbox(k, w, -0.2f, w.len + 0.2f, H - 0.27f + e, H - 0.12f + e, 0.f, 0.2f, d.trim, d.trimMat, WF_LEDGE);
            wbox(k, w, -0.3f, w.len + 0.3f, H - 0.12f + e, H + 0.04f + e, 0.f, 0.3f, d.trim, d.trimMat, WF_LEDGE);
            break;
        default: break;
    }
}

void coping(FD& d, const Wall& w, float H) {
    float e = 0.004f * (float)(w.idx & 1);
    wbox(d.k, w, -0.06f, w.len + 0.06f, H + 0.97f + e, H + 1.07f + e, -0.36f, 0.06f, d.trim, d.trimMat, WF_LEDGE);
}

void stringCourse(FD& d, const Wall& w, float z, float h, float out, u32 col, u32 mat) {
    float e = 0.004f * (float)(w.idx & 1);
    wbox(d.k, w, -out, w.len + out, z - h * 0.5f + e, z + h * 0.5f + e, 0.f, out, col, mat, WF_LEDGE);
}

// Pilasters at bay boundaries (corners always); `every` = spacing in bays (0 = corners only)
void pilasters(FD& d, const Wall& w, float z0, float z1, int every, float width, float out, u32 col, u32 mat) {
    const FacadeGPU& f = *d.f;
    float pier = w.bw * (1.f - f.winW);
    for (int i = 0; i <= w.bays; i++) {
        bool corner = i == 0 || i == w.bays;
        if (!corner && (every <= 0 || i % every != 0)) continue;
        float wd = corner ? width : Min(width, pier * 0.7f);
        if (wd < 0.18f) continue;
        float sb = i * w.bw;
        float s0 = corner ? (i == 0 ? 0.f : w.len - wd) : sb - wd * 0.5f, s1 = s0 + wd;
        wbox(d.k, w, s0, s1, z0, z1, 0.f, out, col, mat, WF_POST);
    }
}

// ------------------------------------------------------------------------------------------------ openings
// Window trim kinds: 0 sill only, 1 sill + head, 2 full frame, 3 frame + keystone head
void windowTrims(FD& d, const Wall& w, const FacadeMass& ms, int kind, bool shutters, u32 shutterCol, int bahama) {
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    int style = (int)f.style;
    if (style == 1 || style == 3) return;
    FloorRow rows[128];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 128);
    for (int ri = 0; ri < nr && d.room(); ri++) {
        const FloorRow& fr = rows[ri];
        WinSpec ws = winSpec(f, fr.idx == 0);
        if (ws.store) continue;
        float z0 = ms.vBase + fr.v0 + ws.sill, z1 = z0 + ws.h;
        if (style == 2) {
            // ribbon windows: continuous sill and head ledges across the wall
            wbox(k, w, 0.f, w.len, z0 - 0.1f, z0, 0.f, 0.12f, d.trim, d.trimMat, WF_LEDGE);
            wbox(k, w, 0.f, w.len, z1, z1 + 0.14f, 0.f, 0.08f, d.trim, d.trimMat, WF_FRONT | WF_BOTTOM);
            continue;
        }
        // full surrounds where people look (lowest four floors); sills alone keep the rhythm higher up
        int kf = fr.idx <= 4 ? kind : 0;
        for (int i = 0; i < w.bays; i++) {
            if (style == 4 && !industrialWindow(f, w, i, fr.idx)) continue;
            float s0 = (i + ws.x0) * w.bw, s1 = s0 + ws.w * w.bw, sc = (s0 + s1) * 0.5f;
            wbox(k, w, s0 - 0.08f, s1 + 0.08f, z0 - 0.09f, z0, 0.f, 0.11f, d.trim, d.trimMat, WF_LEDGE);
            if (kf >= 1) wbox(k, w, s0 - 0.11f, s1 + 0.11f, z1, z1 + (kf == 3 ? 0.24f : 0.15f), 0.f, 0.07f, d.trim, d.trimMat, WF_FRONT | WF_BOTTOM);
            if (kf >= 2) {
                wbox(k, w, s0 - 0.1f, s0, z0, z1, 0.f, 0.05f, d.trim, d.trimMat, WF_FRONT);
                wbox(k, w, s1, s1 + 0.1f, z0, z1, 0.f, 0.05f, d.trim, d.trimMat, WF_FRONT);
            }
            if (kf == 3) wbox(k, w, sc - 0.13f, sc + 0.13f, z1 - 0.06f, z1 + 0.3f, 0.f, 0.11f, d.trim, d.trimMat, WF_FRONT | WF_BOTTOM);
            if (shutters) {
                float pier = w.bw - (s1 - s0);
                float pw = Min((s1 - s0) * 0.5f, (pier - 0.16f) * 0.5f);
                if (pw > 0.25f) {
                    wbox(k, w, s0 - 0.03f - pw, s0 - 0.03f, z0 + 0.02f, z1 - 0.02f, 0.01f, 0.05f, shutterCol, MM(MAT_WOOD), WF_FRONT);
                    wbox(k, w, s1 + 0.03f, s1 + 0.03f + pw, z0 + 0.02f, z1 - 0.02f, 0.01f, 0.05f, shutterCol, MM(MAT_WOOD), WF_FRONT);
                }
            } else if (bahama) {
                // Bahama shutter: one panel hinged at the head, propped open at ~30 degrees
                float hh = (z1 - z0) + 0.1f;
                vec3 hinge = vec3(w.a + w.t * sc + w.n * 0.06f, z1 + 0.05f);
                vec3 dn = normalize(vec3(w.n * 0.5f, -0.866f));
                vec3 c = hinge + dn * (hh * 0.5f);
                obox(k, c, vec3(w.t, 0), dn, vec3((s1 - s0) * 0.5f + 0.06f, hh * 0.5f, 0.025f), shutterCol, MM(MAT_WOOD));
            }
        }
    }
}

// Storefront: piers, bulkheads, mullions, transom, head, door; sign band lip and cornice
void storefront(FD& d, const Wall& w, float vBase, int doorBay) {
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    float gH = f.groundH;
    float zg0 = vBase + 0.35f, zg1 = vBase + 0.35f + (gH - 1.3f);
    float glassH = zg1 - zg0;
    u32 fm = MM(MAT_METAL_PAINTED);
    for (int i = 0; i <= w.bays; i++) {
        float sb = i * w.bw, hw = 0.04f * w.bw + 0.05f;
        wbox(k, w, Max(0.f, sb - hw), Min(w.len, sb + hw), vBase - 0.3f, zg1 + 0.05f, 0.f, 0.12f, d.trim, d.trimMat, WF_POST);
    }
    int nm = Max(1, (int)floorf(f.bayW * 0.92f / 1.4f));
    for (int i = 0; i < w.bays; i++) {
        float s0 = (i + 0.04f) * w.bw, s1 = (i + 0.96f) * w.bw;
        wbox(k, w, s0, s1, vBase - 0.3f, zg0, 0.f, 0.08f, d.dark, MM(MAT_MARBLE), WF_FRONT | WF_TOP);
        wbox(k, w, s0, s1, zg1 - 0.07f, zg1 + 0.02f, 0.f, 0.06f, d.frame, fm, WF_FRONT | WF_BOTTOM);
        for (int j = 1; j < nm; j++) {
            float s = s0 + (s1 - s0) * j / nm;
            wbox(k, w, s - 0.035f, s + 0.035f, zg0, zg1, 0.f, 0.06f, d.frame, fm, WF_POST);
        }
        float zt = zg0 + Min(2.4f, glassH - 0.45f);
        if (glassH > 2.9f) wbox(k, w, s0, s1, zt - 0.045f, zt + 0.045f, 0.f, 0.06f, d.frame, fm, WF_FRONT | WF_TOP | WF_BOTTOM);
        if (i == doorBay) {
            float dc = (s0 + s1) * 0.5f, dw = Min(1.9f, (s1 - s0) * 0.8f);
            float zh = zg0 - 0.35f + Min(2.3f, glassH);
            wbox(k, w, dc - dw * 0.5f - 0.08f, dc - dw * 0.5f, vBase, zh, 0.f, 0.08f, d.frame, fm, WF_POST);
            wbox(k, w, dc + dw * 0.5f, dc + dw * 0.5f + 0.08f, vBase, zh, 0.f, 0.08f, d.frame, fm, WF_POST);
            wbox(k, w, dc - 0.04f, dc + 0.04f, vBase, zh, 0.f, 0.08f, d.frame, fm, WF_FRONT);
            wbox(k, w, dc - dw * 0.5f, dc + dw * 0.5f, zh - 0.08f, zh, 0.f, 0.08f, d.frame, fm, WF_FRONT | WF_BOTTOM);
            // push bars
            wbox(k, w, dc - dw * 0.42f, dc - 0.12f, vBase + 1.0f, vBase + 1.05f, 0.08f, 0.12f, pk(0.8f, 0.8f, 0.82f), MM(MAT_CHROME), WF_FRONT);
            wbox(k, w, dc + 0.12f, dc + dw * 0.42f, vBase + 1.0f, vBase + 1.05f, 0.08f, 0.12f, pk(0.8f, 0.8f, 0.82f), MM(MAT_CHROME), WF_FRONT);
        }
    }
    if (f.flags & 2u) {
        float zb = vBase + gH - 1.05f;
        wbox(k, w, 0.f, w.len, zb - 0.09f, zb, 0.f, 0.1f, d.trim, d.trimMat, WF_FRONT | WF_BOTTOM);
        float zc = vBase + gH - 0.15f;
        wbox(k, w, -0.05f, w.len + 0.05f, zc, zc + 0.22f, 0.f, 0.26f, d.trim, d.trimMat, WF_LEDGE | WF_START | WF_END);
    }
}

// Striped canvas awning with a scalloped valance, from s0 to s1 along the wall, attached at z, projecting `depth`
void awningStriped(FD& d, const Wall& w, float s0, float s1, float z, float depth, u32 colA, u32 colB, bool scallop) {
    Sink& k = d.k;
    MeshData& m = *k.m;
    float len = s1 - s0;
    if (len < 0.4f) return;
    int n = Max(1, (int)roundf(len / 0.75f));
    float drop = depth * 0.38f, val = 0.3f;
    u32 mat = MM(MAT_FABRIC);
    vec3 N3(w.n, 0.f), T3(w.t, 0.f);
    for (int i = 0; i < n; i++) {
        float a0 = s0 + len * i / n, a1 = s0 + len * (i + 1) / n;
        u32 c = (i & 1) ? colB : colA;
        vec3 A0 = WP(k, w, a0, z, 0.f), A1 = WP(k, w, a1, z, 0.f);
        vec3 B0 = WP(k, w, a0, z - drop, depth), B1 = WP(k, w, a1, z - drop, depth);
        m.quadFacing(A0, A1, B1, B0, vec2(a0, 0), vec2(a1, 0), vec2(a1, depth), vec2(a0, depth), c, mat, vec3(N3 * 0.4f) + vec3(0, 0, 1));
        m.quadFacing(A0, B0, B1, A1, vec2(a0, 0), vec2(a0, depth), vec2(a1, depth), vec2(a1, 0), c, mat, vec3(-N3 * 0.4f) - vec3(0, 0, 1));
        // valance: straight top, scalloped (or straight) bottom, double sided
        const int SEG = 3;
        vec3 pts[SEG + 1];
        for (int j = 0; j <= SEG; j++) {
            float u = (float)j / SEG;
            float dz = val + (scallop ? 0.1f * sinf(u * kPi) : 0.f);
            pts[j] = WP(k, w, Lerp(a0, a1, u), z - drop - dz, depth + 0.005f);
        }
        vec3 ctop = WP(k, w, (a0 + a1) * 0.5f, z - drop, depth + 0.005f);
        for (int side = 0; side < 2; side++) {
            vec3 nn = side == 0 ? N3 : -N3;
            u32 ic = m.addVertex(ctop, nn, T3, vec2((a0 + a1) * 0.5f, 0.f), c, mat);
            u32 it0 = m.addVertex(B0 + N3 * 0.005f, nn, T3, vec2(a0, 0.f), c, mat);
            u32 it1 = m.addVertex(B1 + N3 * 0.005f, nn, T3, vec2(a1, 0.f), c, mat);
            u32 ip[SEG + 1];
            for (int j = 0; j <= SEG; j++) ip[j] = m.addVertex(pts[j], nn, T3, vec2(Lerp(a0, a1, (float)j / SEG), val), c, mat);
            auto tri = [&](u32 a, u32 b, u32 cc) {
                if (side == 0) m.tri(a, b, cc);
                else m.tri(a, cc, b);
            };
            // fan: top-left corner, down the scallop, up to the top-right corner
            tri(ic, ip[0], it0);
            for (int j = 0; j < SEG; j++) tri(ic, ip[j + 1], ip[j]);
            tri(ic, it1, ip[SEG]);
        }
    }
    // side cheeks
    for (int e = 0; e < 2; e++) {
        float s = e == 0 ? s0 : s1;
        vec3 a = WP(k, w, s, z, 0.f), b = WP(k, w, s, z - drop, depth), c = WP(k, w, s, z - drop - val, depth);
        vec3 nn = e == 0 ? -T3 : T3;
        u32 cc = e == 0 ? colA : ((n & 1) ? colA : colB);
        u32 i0 = m.addVertex(a, nn, N3, vec2(0, 0), cc, mat), i1 = m.addVertex(b, nn, N3, vec2(depth, 0), cc, mat), i2 = m.addVertex(c, nn, N3, vec2(depth, val), cc, mat);
        u32 j0 = m.addVertex(a, -nn, N3, vec2(0, 0), cc, mat), j1 = m.addVertex(b, -nn, N3, vec2(depth, 0), cc, mat), j2 = m.addVertex(c, -nn, N3, vec2(depth, val), cc, mat);
        vec3 fn = cross(b - a, c - a);
        if (dot(fn, nn) >= 0.f) { m.tri(i0, i1, i2); m.tri(j0, j2, j1); }
        else { m.tri(i0, i2, i1); m.tri(j0, j1, j2); }
    }
    // brackets
    for (int e = 0; e < 2; e++) {
        float s = e == 0 ? s0 + 0.08f : s1 - 0.08f;
        vec3 a = vec3(w.a + w.t * s, z - 0.02f), b = vec3(w.a + w.t * s + w.n * depth, z - drop - 0.02f);
        vec3 ax = normalize(b - a);
        obox(k, (a + b) * 0.5f, ax, vec3(w.t, 0.f), vec3(length(b - a) * 0.5f, 0.015f, 0.015f), pk(0.2f), MM(MAT_METAL_PAINTED));
    }
}

// Roll-down security gate: housing at the top of the storefront glass; optionally pulled partly down
void securityGate(FD& d, const Wall& w, float s0, float s1, float zTop, float closedTo, u32 panelCol) {
    Sink& k = d.k;
    wbox(k, w, s0 - 0.04f, s1 + 0.04f, zTop - 0.34f, zTop, 0.f, 0.3f, pk(0.55f, 0.56f, 0.57f), MM(MAT_METAL_BRUSHED), WF_BOX | WF_BOTTOM);
    for (int e = 0; e < 2; e++) {
        float s = e == 0 ? s0 - 0.06f : s1 + 0.01f;
        wbox(k, w, s, s + 0.05f, closedTo < zTop ? d.b->baseZ : zTop - 0.34f, zTop - 0.34f, 0.f, 0.12f, pk(0.45f), MM(MAT_METAL_PAINTED), WF_FRONT);
    }
    if (closedTo < zTop - 0.4f) {
        wquad(k, w, s0, s1, closedTo, zTop - 0.34f, 0.09f, panelCol, MM(MAT_CORRUGATED));
        wbox(k, w, s0, s1, closedTo - 0.05f, closedTo + 0.02f, 0.f, 0.11f, pk(0.3f), MM(MAT_METAL_PAINTED), WF_FRONT | WF_BOTTOM);
    }
}

// ------------------------------------------------------------------------------------------------ neon and paint
const char* kShopWords[] = {"CAFE", "BAR", "PIZZA", "LIQUOR", "CUBANO", "TACOS", "DINER", "RADIO", "PAWN", "TATTOO", "SALSA", "MUSIC", "BODEGA",
                            "CLUB", "BARBER", "BOOKS", "BAKERY", "GRILL", "SUSHI", "DANCE", "JAZZ", "CIGARS", "LOUNGE", "ARCADE", "BILLAR",
                            "RECORDS", "HOTEL", "CAMBIO", "PHOTO", "SHOES"};
const char* kHotelWords[] = {"HERON", "LAGOON", "ORCHID", "PEARL", "SEAGRAPE", "AZURA", "SOLANA", "SEAFOAM", "TIDEWAY", "LUMINA", "CORALINE", "BRISA"};
const char* kTagSyll[] = {"ZE", "KA", "RO", "VY", "MI", "TOX", "NEO", "SUR", "QUE", "DRE", "LUX", "OZ", "KRA", "FE", "BO", "ZA",
                          "YU", "REK", "SPY", "NOX", "JAX", "KOI", "VEX", "ORB", "DUB", "SKE", "WAR", "EKO"};

vec3 neonHue(Rng& r) {
    const vec3 pal[] = {vec3(1.f, 0.15f, 0.45f), vec3(0.2f, 0.9f, 1.f), vec3(1.f, 0.45f, 0.1f), vec3(0.6f, 0.25f, 1.f), vec3(0.3f, 1.f, 0.35f),
                        vec3(1.f, 0.9f, 0.2f), vec3(1.f, 0.2f, 0.15f), vec3(0.2f, 0.45f, 1.f)};
    return pal[r.next() % ARRAY_COUNT(pal)];
}

// Neon text as extruded tube strokes. right/up span the text plane; the tubes stand out of the plane toward cross(right, up).
void neonText(Sink& k, const char* txt, vec3 origin, vec3 right, vec3 up, float h, vec3 col, u32 anim, u32 phase) {
    sitegeo::G g;
    g.m = k.m;
    g.org = k.org;
    sitegeo::strokeText(g, *k.m, txt, origin, right, up, h, h * 0.09f, pk(col, 0.85f), neonMat(anim, phase), 0.f, 0.3f);
}

// Projecting blade sign: panel perpendicular to the wall at s, vertical neon letters on both faces, tube border, brackets
void bladeSign(FD& d, const Wall& w, float s, float z0, float z1, float o0, float o1, const char* word, vec3 ncol, u32 panelCol, u32 anim) {
    Sink& k = d.k;
    float th = 0.09f;
    // panel spans o0..o1 out of the wall, thickness along t
    vec3 c = vec3(w.a + w.t * s + w.n * ((o0 + o1) * 0.5f), (z0 + z1) * 0.5f);
    obox(k, c, vec3(w.n, 0.f), vec3(w.t, 0.f), vec3((o1 - o0) * 0.5f, th, (z1 - z0) * 0.5f), panelCol, MM(MAT_METAL_PAINTED));
    for (int b2 = 0; b2 < 2; b2++) {
        float z = b2 == 0 ? z0 + 0.25f : z1 - 0.25f;
        wbox(k, w, s - 0.05f, s + 0.05f, z - 0.05f, z + 0.05f, 0.f, o0, pk(0.25f), MM(MAT_METAL_PAINTED), WF_TOP | WF_BOTTOM | WF_START | WF_END);
    }
    int n = (int)strlen(word);
    if (n == 0) return;
    float avail = z1 - z0 - 0.5f;
    float lh = Min((o1 - o0) * 0.62f, avail / (n * 1.18f));
    float lw = lh * 6.f / 9.f;
    for (int face = 0; face < 2; face++) {
        float sd = face == 0 ? 1.f : -1.f;           // face normal = sd * t
        vec2 fnrm = w.t * sd;
        vec2 rt = vec2(-fnrm.y, fnrm.x);             // viewer's right on this face
        vec3 base = vec3(w.a + w.t * (s + sd * (th + 0.005f)), 0.f);
        float oc = (o0 + o1) * 0.5f;
        // stacked letters, top to bottom
        for (int i = 0; i < n; i++) {
            char ch[2] = {word[i], 0};
            float zc = z1 - 0.25f - (i + 0.5f) * (avail / n);
            vec3 o = base + vec3(w.n * oc - rt * (lw * 0.5f), zc - lh * 0.5f);
            neonText(k, ch, o, vec3(rt, 0.f), vec3(0, 0, 1), lh, ncol, anim, (u32)i * 40u);
        }
        // border tube (flat strip on the panel face)
        float in = 0.1f, tw2 = 0.035f;
        u32 bc = pk(ncol * 0.7f + vec3(0.3f), 0.7f), bm = neonMat(anim == 0 ? 6u : anim, 17u);
        vec3 fN(fnrm, 0.f);
        auto P = [&](float o, float z) { return vec3(w.a + w.t * (s + sd * (th + 0.01f)) + w.n * o, z) - k.org; };
        float oa = o0 + in, ob = o1 - in, za = z0 + in, zb2 = z1 - in;
        k.m->quadFacing(P(oa - tw2, za - tw2), P(ob + tw2, za - tw2), P(ob + tw2, za + tw2), P(oa - tw2, za + tw2), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), bc, bm, fN);
        k.m->quadFacing(P(oa - tw2, zb2 - tw2), P(ob + tw2, zb2 - tw2), P(ob + tw2, zb2 + tw2), P(oa - tw2, zb2 + tw2), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), bc, bm, fN);
        k.m->quadFacing(P(oa - tw2, za), P(oa + tw2, za), P(oa + tw2, zb2), P(oa - tw2, zb2), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), bc, bm, fN);
        k.m->quadFacing(P(ob - tw2, za), P(ob + tw2, za), P(ob + tw2, zb2), P(ob - tw2, zb2), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), bc, bm, fN);
    }
    addLight(k, vec3(w.a + w.t * s + w.n * ((o0 + o1) * 0.5f), (z0 + z1) * 0.5f), ncol * 700.f, 9.f, 2);
}

// Small window neon (OPEN / word) floating just in front of storefront glass
void windowNeon(FD& d, const Wall& w, float sc, float z, const char* word, vec3 col, u32 anim) {
    Sink& k = d.k;
    float h = 0.26f;
    vec2 rt = vec2(-w.n.y, w.n.x);
    float tw = sitegeo::textAdvance(word, h, 0.3f);
    vec3 o = vec3(w.a + w.t * sc + w.n * 0.04f - rt * (tw * 0.5f), z);
    neonText(k, word, o, vec3(rt, 0.f), vec3(0, 0, 1), h, col, anim, 3u);
    // tube border
    float bw = tw * 0.5f + 0.12f;
    vec3 c = vec3(w.a + w.t * sc + w.n * 0.045f, z + h * 0.5f);
    u32 bc = pk(vec3(0.3f, 0.6f, 1.f), 0.7f);
    u32 bm = neonMat(6u, 0u);
    for (int e = -1; e <= 1; e += 2) {
        vec3 cc = c + vec3(0, 0, e * (h * 0.5f + 0.1f)) - k.org;
        vec3 R = vec3(rt, 0.f) * bw, U(0, 0, 0.018f);
        k.m->quadFacing(cc - R - U, cc + R - U, cc + R + U, cc - R + U, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), bc, bm, vec3(w.n, 0.f));
    }
}

// Graffiti piece: optional backing blob, dark outline, colored fill, drips (paint on the wall plane)
void graffiti(FD& d, const Wall& w, float sc, float zb, float h, Rng& r) {
    Sink& k = d.k;
    char word[12];
    int nsy = r.irange(2, 3);
    word[0] = 0;
    for (int i = 0; i < nsy; i++) strncat(word, kTagSyll[r.next() % ARRAY_COUNT(kTagSyll)], 3);
    if (strlen(word) > 6) word[6] = 0;
    vec2 rt = vec2(-w.n.y, w.n.x);  // = t
    float tw = sitegeo::textAdvance(word, h, 0.15f);
    if (tw > w.len - 0.6f) {
        h *= (w.len - 0.6f) / tw;
        tw = w.len - 0.6f;
    }
    if (h < 0.3f) return;
    float s0 = Clamp(sc - tw * 0.5f, 0.3f, Max(0.3f, w.len - 0.3f - tw));
    const vec3 fills[] = {vec3(0.95f, 0.2f, 0.55f), vec3(0.2f, 0.75f, 0.95f), vec3(1.f, 0.8f, 0.1f), vec3(0.4f, 0.95f, 0.3f), vec3(0.95f, 0.45f, 0.1f),
                          vec3(0.75f, 0.35f, 0.95f), vec3(0.95f, 0.95f, 0.95f), vec3(0.95f, 0.15f, 0.1f)};
    vec3 fill = fills[r.next() % ARRAY_COUNT(fills)];
    vec3 outline = r.chance(0.7f) ? vec3(0.05f, 0.05f, 0.07f) : vec3(0.95f);
    u32 paint = MM(MAT_PAINT_WHITE);
    sitegeo::G g;
    g.m = k.m;
    g.org = k.org;
    float tilt = r.range(-0.08f, 0.12f);
    vec3 R3 = normalize(vec3(rt, tilt)), U3 = normalize(vec3(-rt * tilt, 1.f));
    int kind = r.irange(0, 2);  // 0 tag, 1 throw-up, 2 piece
    if (kind == 2) {
        // backing blob: rounded rectangle behind the letters
        const int SEG = 14;
        std::vector<vec3> poly;
        vec3 cen = vec3(w.a + w.t * (s0 + tw * 0.5f) + w.n * 0.008f, zb + h * 0.5f);
        vec3 blobC = r.chance(0.5f) ? vec3(0.1f, 0.1f, 0.12f) : fills[r.next() % ARRAY_COUNT(fills)] * 0.6f;
        for (int i = 0; i < SEG; i++) {
            float a = kTwoPi * i / SEG;
            float rx = tw * 0.5f + h * 0.35f, rz = h * 0.75f;
            float wob = 1.f + 0.08f * sinf(a * 3.f + (float)(r.next() % 7));
            poly.push_back(cen + R3 * (cosf(a) * rx * wob) + vec3(0, 0, sinf(a) * rz * wob) - k.org);
        }
        k.m->polygon(poly, vec3(w.n, 0.f), pk(blobC), paint, 1.f);
    }
    vec3 o = vec3(w.a + w.t * s0, zb);
    if (kind >= 1) sitegeo::strokeText(g, *k.m, word, o + vec3(w.n * 0.012f, 0.f), R3, U3, h, h * 0.26f, pk(outline), paint, 0.f, 0.15f);
    sitegeo::strokeText(g, *k.m, word, o + vec3(w.n * 0.016f, 0.f), R3, U3, h, kind == 0 ? h * 0.08f : h * 0.15f, pk(fill), paint, 0.f, 0.15f);
    // drips
    int nd = kind == 0 ? r.irange(0, 2) : r.irange(2, 6);
    for (int i = 0; i < nd; i++) {
        float s = s0 + r.range(0.1f, tw - 0.1f);
        float len = r.range(0.08f, 0.45f);
        float zt = zb + r.range(0.f, h * 0.2f) + tilt * (s - s0);
        wquad(k, w, s - 0.012f, s + 0.012f, zt - len, zt, 0.017f, pk(fill), paint);
    }
}

// ------------------------------------------------------------------------------------------------ wall equipment
void acUnit(FD& d, const Wall& w, float sc, float zSill, float wWin) {
    Sink& k = d.k;
    float hw = Min(0.34f, wWin * 0.4f);
    u32 body = d.r.chance(0.7f) ? pk(0.88f, 0.87f, 0.82f) : pk(0.62f, 0.6f, 0.56f);
    wbox(k, w, sc - hw, sc + hw, zSill + 0.02f, zSill + 0.44f, -0.03f, 0.42f, body, MM(MAT_METAL_PAINTED), WF_BOX | WF_BOTTOM);
    wquad(k, w, sc - hw + 0.05f, sc + hw - 0.05f, zSill + 0.07f, zSill + 0.38f, 0.425f, pk(0.25f, 0.25f, 0.26f), MM(MAT_METAL_BRUSHED));
}

void downpipe(FD& d, const Wall& w, float s, float zTop, float zBot, u32 col) {
    Sink& k = d.k;
    wbox(k, w, s - 0.055f, s + 0.055f, zBot + 0.25f, zTop, 0.04f, 0.15f, col, MM(MAT_METAL_PAINTED), WF_POST);
    wbox(k, w, s - 0.07f, s + 0.07f, zBot, zBot + 0.25f, 0.03f, 0.3f, col, MM(MAT_METAL_PAINTED), WF_POST | WF_TOP);
    wbox(k, w, s - 0.1f, s + 0.1f, zTop - 0.02f, zTop + 0.22f, 0.f, 0.2f, col, MM(MAT_METAL_PAINTED), WF_POST | WF_BOTTOM);
}

// Fire escape on a brick midrise: platforms and railings at each upper floor, switchback stairs, drop ladder
void fireEscape(FD& d, const Wall& w, const FacadeMass& ms, int bay0, int nb) {
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    float s0 = bay0 * w.bw + 0.15f, s1 = (bay0 + nb) * w.bw - 0.15f;
    if (s1 - s0 < 2.4f) return;
    u32 iron = pk(0.12f, 0.12f, 0.13f), im = MM(MAT_METAL_PAINTED);
    const float dep = 1.25f;
    FloorRow rows[64];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
    float prevZ = -1.f;
    for (int ri = 0; ri < nr; ri++) {
        if (rows[ri].idx == 0) continue;
        float z = ms.vBase + rows[ri].v0;
        // grating platform, fascia
        wbox(k, w, s0, s1, z - 0.06f, z, 0.f, dep, iron, im, WF_LEDGE | WF_START | WF_END);
        // railing: top rail, mid rail, posts
        wbox(k, w, s0, s1, z + 0.95f, z + 1.0f, dep - 0.05f, dep, iron, im, WF_FRONT | WF_TOP);
        wbox(k, w, s0, s1, z + 0.45f, z + 0.48f, dep - 0.04f, dep, iron, im, WF_FRONT);
        for (int e = 0; e < 2; e++) {
            float se = e == 0 ? s0 : s1 - 0.05f;
            wbox(k, w, se, se + 0.05f, z + 0.95f, z + 1.0f, 0.f, dep, iron, im, e == 0 ? WF_START : WF_END);
        }
        int np = Max(2, (int)((s1 - s0) / 1.2f));
        for (int i = 0; i <= np; i++) {
            float s = Lerp(s0, s1 - 0.04f, (float)i / np);
            wbox(k, w, s, s + 0.04f, z, z + 0.95f, dep - 0.04f, dep, iron, im, WF_FRONT);
        }
        // stair down to the platform below (alternating direction), or the drop ladder at the first floor
        bool rightDown = (ri & 1) != 0;
        if (prevZ > 0.f) {
            float zt = z, zbt = prevZ;
            float sa = rightDown ? s0 + 0.3f : s1 - 0.3f, sb = rightDown ? s1 - 0.5f : s0 + 0.5f;
            vec3 A = vec3(w.a + w.t * sa + w.n * (dep * 0.5f), zt), B = vec3(w.a + w.t * sb + w.n * (dep * 0.5f), zbt);
            vec3 ax = normalize(B - A);
            vec3 ay(w.n, 0.f);
            float L = length(B - A);
            obox(k, (A + B) * 0.5f, ax, ay, vec3(L * 0.5f, 0.3f, 0.03f), iron, im);
            obox(k, (A + B) * 0.5f + vec3(w.n * 0.3f, 0.45f), ax, ay, vec3(L * 0.5f, 0.02f, 0.02f), iron, im);
        } else {
            float sl = rightDown ? s1 - 0.7f : s0 + 0.3f;
            float zl = z - 2.6f;
            wbox(k, w, sl, sl + 0.04f, zl, z, dep - 0.45f, dep - 0.41f, iron, im, WF_FRONT | WF_START | WF_END);
            wbox(k, w, sl + 0.4f, sl + 0.44f, zl, z, dep - 0.45f, dep - 0.41f, iron, im, WF_FRONT | WF_START | WF_END);
            for (float zr = zl + 0.3f; zr < z; zr += 0.35f) wbox(k, w, sl, sl + 0.44f, zr, zr + 0.03f, dep - 0.45f, dep - 0.42f, iron, im, WF_FRONT);
        }
        prevZ = z;
        if (!d.room()) break;
    }
}

// Balconies on a wall: slab + railing (metal) or solid stucco parapet, pattern 0 every bay, 1 alternate, 2 center pair
void balconies(FD& d, const Wall& w, const FacadeMass& ms, int pattern, bool solid) {
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    FloorRow rows[64];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
    u32 rail = solid ? d.wallTone : (d.r.chance(0.5f) ? pk(0.1f, 0.1f, 0.11f) : pk(0.92f, 0.92f, 0.9f));
    u32 rm = solid ? d.wallMat : MM(MAT_METAL_PAINTED);
    const float dep = 1.05f;
    for (int ri = 0; ri < nr && d.room(); ri++) {
        if (rows[ri].idx == 0) continue;
        float z = ms.vBase + rows[ri].v0;
        WinSpec ws = winSpec(f, false);
        for (int i = 0; i < w.bays; i++) {
            if (pattern == 1 && ((i + rows[ri].idx) & 1)) continue;
            if (pattern == 2 && (i < w.bays / 2 - 1 || i > w.bays / 2)) continue;
            float s0 = (i + ws.x0) * w.bw - 0.35f, s1 = (i + ws.x0 + ws.w) * w.bw + 0.35f;
            s0 = Max(s0, i * w.bw + 0.08f);
            s1 = Min(s1, (i + 1) * w.bw - 0.08f);
            wbox(k, w, s0, s1, z - 0.16f, z + 0.02f, 0.f, dep, d.trim, d.trimMat, WF_BOX | WF_BOTTOM);
            if (solid) {
                wbox(k, w, s0, s1, z + 0.02f, z + 1.0f, dep - 0.12f, dep, rail, rm, WF_FRONT | WF_TOP | WF_START | WF_END);
                wbox(k, w, s0, s0 + 0.12f, z + 0.02f, z + 1.0f, 0.f, dep - 0.12f, rail, rm, WF_TOP | WF_START | WF_END);
                wbox(k, w, s1 - 0.12f, s1, z + 0.02f, z + 1.0f, 0.f, dep - 0.12f, rail, rm, WF_TOP | WF_START | WF_END);
            } else {
                wbox(k, w, s0, s1, z + 0.98f, z + 1.03f, dep - 0.05f, dep, rail, rm, WF_FRONT | WF_TOP | WF_START | WF_END);
                wbox(k, w, s0, s0 + 0.04f, z + 0.98f, z + 1.03f, 0.f, dep - 0.05f, rail, rm, WF_TOP | WF_START);
                wbox(k, w, s1 - 0.04f, s1, z + 0.98f, z + 1.03f, 0.f, dep - 0.05f, rail, rm, WF_TOP | WF_END);
                int nb = Clamp((int)((s1 - s0) / 0.45f), 3, 7);
                for (int j = 0; j <= nb; j++) {
                    float s = Lerp(s0, s1 - 0.03f, (float)j / nb);
                    wbox(k, w, s, s + 0.03f, z + 0.02f, z + 0.98f, dep - 0.04f, dep - 0.01f, rail, rm, WF_FRONT);
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ style specials
// Art deco: fins at the shader's fin lines, corner speed lines, ziggurat crest, finial on the tower element
void decoFront(FD& d, const Wall& w, const FacadeMass& ms) {
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    float H = ms.z1, zf0 = ms.vBase + f.groundH;
    for (int i = 1; i < w.bays; i++) {
        if (((w.kStart + i) & 1) == 0) continue;  // shader fins sit at odd global bay boundaries
        float sb = i * w.bw, hw = Min(0.18f, w.bw * 0.06f);
        wbox(k, w, sb - hw, sb + hw, zf0, H + 0.9f, 0.f, 0.26f, d.accent, d.trimMat, WF_POST | WF_TOP);
    }
    // speed lines wrapping the corners at the top floor
    float zs = H - Min(1.6f, f.floorH * 0.55f);
    for (int j = 0; j < 3; j++) {
        float z = zs - j * 0.22f;
        wbox(k, w, -0.07f, Min(2.4f, w.len * 0.3f), z, z + 0.08f, 0.f, 0.07f, d.trim, d.trimMat, WF_LEDGE | WF_END);
        wbox(k, w, Max(w.len - 2.4f, w.len * 0.7f), w.len + 0.07f, z, z + 0.08f, 0.f, 0.07f, d.trim, d.trimMat, WF_LEDGE | WF_START);
    }
    // ziggurat crest centered above the parapet
    float cw = w.len * 0.36f;
    for (int t = 0; t < 3; t++) {
        float hw = cw * (1.f - t * 0.3f) * 0.5f;
        float zb = H + 1.0f + t * 0.7f;
        wbox(k, w, w.len * 0.5f - hw, w.len * 0.5f + hw, zb, zb + 0.7f, -0.3f, 0.02f, t == 1 ? d.accent : d.wallTone, t == 1 ? d.trimMat : d.wallMat, WF_BOX | WF_BACK);
    }
}

// Towers: vertical fins (curtain walls) or spandrel ledges (punched/ribbon), crown band
void towerTier(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls, int finMode) {
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    FloorRow rows[160];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 160);
    for (const Wall& w : walls) {
        if (w.len < 3.f || !d.room()) continue;
        if (finMode == 1) {
            for (int i = 0; i <= w.bays; i += 2) {
                float sb = i * w.bw;
                wbox(k, w, Max(0.f, sb - 0.06f), Min(w.len, sb + 0.06f), ms.z0 + 0.5f, ms.z1 + 0.6f, 0.f, 0.38f, d.frame, MM(MAT_METAL_BRUSHED), WF_POST);
            }
        } else if (finMode == 2) {
            int step = nr > 24 ? 2 : 1;
            for (int ri = 0; ri < nr; ri += step) {
                if (rows[ri].idx == 0) continue;
                float z = ms.vBase + rows[ri].v0;
                wbox(k, w, -0.1f, w.len + 0.1f, z - 0.08f, z + 0.1f, 0.f, 0.14f, d.trim, d.trimMat, WF_FRONT | WF_BOTTOM);
            }
        }
        cornice(d, w, ms.z1, CO_SLAB);
    }
}

// ------------------------------------------------------------------------------------------------ houses and gardens
// Lawn over the whole lot following the terrain (the house and pool deck sit on top)
void lawn(FD& d) {
    const Building& b = *d.b;
    Sink& k = d.k;
    if (b.lotHx < 2.f || b.lotHy < 2.f) return;
    vec2 ay = perp(b.ax);
    vec2 fr = b.front;
    int nx = Clamp((int)(b.lotHx * 2.f / 12.f), 1, 4), ny = Clamp((int)(b.lotHy * 2.f / 12.f), 1, 4);
    Rng lr(b.seed ^ 0x1A77u);
    vec3 tint = vec3(lr.range(0.85f, 1.1f), lr.range(0.95f, 1.2f), lr.range(0.75f, 0.95f));
    if (lr.chance(0.2f)) tint = vec3(1.15f, 1.1f, 0.7f);  // dry lawn
    u32 col = pk(tint), mat = MM(MAT_GRASS);
    // keep a 0.3 m margin to the sidewalk edge
    float hx = b.lotHx - 0.15f, hy = b.lotHy - 0.3f;
    vec2 c = b.lotC - fr * 0.15f;
    std::vector<vec3> grid((size_t)(nx + 1) * (ny + 1));
    for (int j = 0; j <= ny; j++)
        for (int i = 0; i <= nx; i++) {
            vec2 p = c + b.ax * (-hx + 2.f * hx * i / nx) + ay * (-hy + 2.f * hy * j / ny);
            grid[(size_t)j * (nx + 1) + i] = vec3(p, d.map->heightAt(p.x, p.y) + 0.035f) - k.org;
        }
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            vec3 a = grid[(size_t)j * (nx + 1) + i], bb = grid[(size_t)j * (nx + 1) + i + 1];
            vec3 cc = grid[(size_t)(j + 1) * (nx + 1) + i + 1], dd = grid[(size_t)(j + 1) * (nx + 1) + i];
            k.m->quadFacing(a, bb, cc, dd, vec2(a.x, a.y), vec2(bb.x, bb.y), vec2(cc.x, cc.y), vec2(dd.x, dd.y), col, mat, vec3(0, 0, 1));
        }
}

// Foliage (MAT_LEAVES) in world geometry reads its tint from the vertex color
inline u32 foliageTint(vec3 c) { return pk(c); }

// Hedge segment between two ground points, following the terrain in pieces
void hedge(FD& d, vec2 a, vec2 b, float h, float wd, vec3 tint, bool collide) {
    Sink& k = d.k;
    float L = length(b - a);
    if (L < 0.5f) return;
    vec2 t = (b - a) / L;
    int n = Max(1, (int)ceilf(L / 11.f));
    u32 mat = MM(MAT_LEAVES);
    for (int i = 0; i < n; i++) {
        vec2 p0 = a + t * (L * i / n), p1 = a + t * (L * (i + 1) / n);
        vec2 mc = (p0 + p1) * 0.5f;
        float gz = Min(d.map->heightAt(p0.x, p0.y), d.map->heightAt(p1.x, p1.y));
        float hh = h * (0.92f + 0.16f * d.r.f());
        vec3 tt = tint * (0.9f + 0.2f * d.r.f());
        obox(k, vec3(mc, gz - 0.1f + hh * 0.5f + 0.05f), vec3(t, 0.f), vec3(perp(t), 0.f), vec3(length(p1 - p0) * 0.5f + 0.05f, wd * 0.5f, hh * 0.5f + 0.05f),
             foliageTint(tt), mat, false);
        if (collide && k.col) {
            CollisionBox cb;
            cb.c = vec3(mc, gz + hh * 0.5f);
            cb.ax = t;
            cb.he = vec3(length(p1 - p0) * 0.5f, wd * 0.5f, hh * 0.5f);
            k.col->push_back(cb);
        }
    }
}

// Bougainvillea: magenta flowering clumps climbing a wall from the ground, over a span along the wall
void bougainvillea(FD& d, const Wall& w, float s0, float s1, float zg, float zTop) {
    Sink& k = d.k;
    u32 mat = MM(MAT_LEAVES);
    const vec3 blossom[] = {vec3(1.f, 0.25f, 0.75f), vec3(0.95f, 0.2f, 0.55f), vec3(0.85f, 0.25f, 0.95f), vec3(1.f, 0.45f, 0.35f)};
    vec3 bc = blossom[d.r.next() % ARRAY_COUNT(blossom)];
    int n = Clamp((int)((s1 - s0) * (zTop - zg) / 1.1f), 3, 12);
    for (int i = 0; i < n; i++) {
        float u = d.r.f();
        float hgt = (zTop - zg) * sqrtf(d.r.f());
        float s = Lerp(s0, s1, u);
        float sz = d.r.range(0.45f, 0.95f);
        bool green = d.r.chance(0.3f);
        vec3 col = green ? vec3(0.55f, 0.9f, 0.4f) : bc * d.r.range(0.85f, 1.1f);
        wbox(k, w, s - sz, s + sz * d.r.range(0.7f, 1.2f), zg + hgt - sz * 0.6f, zg + hgt + sz * 0.6f, 0.f, d.r.range(0.25f, 0.5f), foliageTint(col), mat,
             WF_FRONT | WF_TOP | WF_BOTTOM | WF_START | WF_END);
    }
    // woody stem
    wbox(k, w, s0 + 0.2f, s0 + 0.28f, zg - 0.2f, zg + (zTop - zg) * 0.6f, 0.02f, 0.1f, pk(0.35f, 0.25f, 0.18f), MM(MAT_BARK), WF_FRONT | WF_START | WF_END);
}

// House and villa: door, stoop, porch roof / portico, window trims and shutters, AC condenser, garden
void houseDetail(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls) {
    const Building& b = *d.b;
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    bool villa = b.style == BS_VILLA, shack = b.style == BS_SHACK, farm = b.style == BS_FARMHOUSE;
    // front wall = the wall whose normal is closest to the street direction
    int fi = 0;
    for (int i = 1; i < (int)walls.size(); i++)
        if (walls[i].facing > walls[fi].facing) fi = i;
    const Wall& fw = walls[fi];
    float zb = ms.vBase;
    // window trims and shutters
    int trimKind = villa ? 3 : d.r.irange(0, 2);
    bool shutters = !villa && !shack && d.r.chance(0.35f);
    int bahama = (!shutters && !villa && d.r.chance(0.18f)) ? 1 : 0;
    const vec3 shutterPal[] = {vec3(0.1f, 0.3f, 0.2f), vec3(0.1f, 0.18f, 0.35f), vec3(0.15f, 0.5f, 0.5f), vec3(0.95f), vec3(0.08f),
                               vec3(0.85f, 0.4f, 0.3f), vec3(0.4f, 0.6f, 0.75f), vec3(0.55f, 0.75f, 0.45f)};
    u32 shutterCol = pk(shutterPal[d.r.next() % ARRAY_COUNT(shutterPal)]);
    for (const Wall& w : walls)
        if (w.len > 2.f) windowTrims(d, w, ms, w.facing > 0.5f ? trimKind : 0, shutters && w.facing > -0.5f, shutterCol, bahama && w.facing > 0.5f);
    // front door at the pier nearest the middle of the front wall
    if (fw.bays >= 2) {
        int bi = fw.bays / 2;
        float sd = bi * fw.bw;
        float pier = fw.bw * (1.f - f.winW);
        float dw = villa ? Min(1.9f, pier - 0.4f) : Min(1.05f, pier - 0.35f);
        if (dw > 0.7f) {
            const vec3 doorPal[] = {vec3(0.35f, 0.2f, 0.1f), vec3(0.1f, 0.25f, 0.45f), vec3(0.7f, 0.15f, 0.12f), vec3(0.95f), vec3(0.15f, 0.4f, 0.35f),
                                    vec3(0.95f, 0.75f, 0.3f)};
            u32 dc = villa ? pk(0.4f, 0.24f, 0.12f) : pk(doorPal[d.r.next() % ARRAY_COUNT(doorPal)]);
            float dh = villa ? 2.6f : 2.15f;
            wbox(k, fw, sd - dw * 0.5f, sd + dw * 0.5f, zb, zb + dh, 0.f, 0.04f, dc, MM(MAT_WOOD), WF_FRONT);
            if (villa) wbox(k, fw, sd - 0.015f, sd + 0.015f, zb, zb + dh, 0.04f, 0.05f, pk(0.2f, 0.12f, 0.06f), MM(MAT_WOOD), WF_FRONT);
            wbox(k, fw, sd - dw * 0.5f - 0.13f, sd - dw * 0.5f, zb, zb + dh + 0.13f, 0.f, 0.08f, d.trim, d.trimMat, WF_POST);
            wbox(k, fw, sd + dw * 0.5f, sd + dw * 0.5f + 0.13f, zb, zb + dh + 0.13f, 0.f, 0.08f, d.trim, d.trimMat, WF_POST);
            wbox(k, fw, sd - dw * 0.5f - 0.13f, sd + dw * 0.5f + 0.13f, zb + dh, zb + dh + 0.18f, 0.f, 0.09f, d.trim, d.trimMat, WF_LEDGE | WF_START | WF_END);
            // handle
            wbox(k, fw, sd + dw * 0.5f - 0.18f, sd + dw * 0.5f - 0.12f, zb + 0.95f, zb + 1.05f, 0.04f, 0.09f, pk(0.8f, 0.7f, 0.35f), MM(MAT_CHROME), WF_FRONT);
            // stoop down to the ground
            vec2 sp = fw.a + fw.t * sd + fw.n * 0.8f;
            float gz = d.map->heightAt(sp.x, sp.y);
            wbox(k, fw, sd - dw * 0.5f - 0.5f, sd + dw * 0.5f + 0.5f, Min(gz, zb) - 0.3f, zb - 0.02f, 0.f, 1.3f, pk(0.82f, 0.8f, 0.76f), MM(MAT_CONCRETE), WF_BOX);
            if (zb - gz > 0.35f)
                wbox(k, fw, sd - dw * 0.5f - 0.3f, sd + dw * 0.5f + 0.3f, gz - 0.3f, (zb + gz) * 0.5f, 1.3f, 1.65f, pk(0.82f, 0.8f, 0.76f), MM(MAT_CONCRETE), WF_FRONT | WF_TOP | WF_START | WF_END);
            // porch roof (houses) or columned portico (villas)
            if (villa) {
                float pw = dw * 0.5f + 1.1f, pd = 2.4f, ph = Min(3.3f, f.groundH + 0.2f);
                for (int e = 0; e < 2; e++) {
                    vec2 cp = fw.a + fw.t * (sd + (e ? pw - 0.25f : -pw + 0.25f)) + fw.n * (pd - 0.25f);
                    k.m->cylinder(vec3(cp, zb) - k.org, 0.19f, 0.16f, ph, 10, d.trim, d.trimMat, false);
                    obox(k, vec3(cp, zb + 0.12f), vec3(fw.t, 0.f), vec3(fw.n, 0.f), vec3(0.28f, 0.28f, 0.12f), d.trim, d.trimMat);
                    if (k.col) {
                        CollisionBox cb;
                        cb.c = vec3(cp, zb + ph * 0.5f);
                        cb.ax = fw.t;
                        cb.he = vec3(0.2f, 0.2f, ph * 0.5f);
                        k.col->push_back(cb);
                    }
                }
                wbox(k, fw, sd - pw, sd + pw, zb + ph, zb + ph + 0.45f, 0.f, pd, d.trim, d.trimMat, WF_BOX | WF_BOTTOM);
                // low tiled hip over the portico
                vec2 rc = fw.a + fw.t * sd + fw.n * (pd * 0.5f);
                vec3 R0 = vec3(rc - fw.t * (pw + 0.15f) + fw.n * (pd * 0.5f + 0.15f), zb + ph + 0.45f), R1 = vec3(rc + fw.t * (pw + 0.15f) + fw.n * (pd * 0.5f + 0.15f), zb + ph + 0.45f);
                vec3 R2 = vec3(rc + fw.t * (pw * 0.6f) - fw.n * (pd * 0.5f), zb + ph + 1.2f), R3 = vec3(rc - fw.t * (pw * 0.6f) - fw.n * (pd * 0.5f), zb + ph + 1.2f);
                k.m->quadFacing(R0 - k.org, R1 - k.org, R2 - k.org, R3 - k.org, vec2(0, 0), vec2(2 * pw, 0), vec2(2 * pw, pd), vec2(0, pd), pk(0.95f, 0.9f, 0.9f),
                                MM(MAT_ROOF_TILE), vec3(fw.n, 1.f));
            } else if (!shack && d.r.chance(0.55f)) {
                float pw = dw * 0.5f + 0.7f;
                wbox(k, fw, sd - pw, sd + pw, zb + 2.55f, zb + 2.7f, 0.f, 1.35f, d.trim, d.trimMat, WF_BOX | WF_BOTTOM);
                for (int e = 0; e < 2; e++) {
                    float s = e ? sd + pw - 0.12f : sd - pw;
                    wbox(k, fw, s, s + 0.12f, zb, zb + 2.55f, 1.2f, 1.32f, d.trim, d.trimMat, WF_POST | WF_BACK);
                }
            }
            // wall lantern
            wbox(k, fw, sd + dw * 0.5f + 0.3f, sd + dw * 0.5f + 0.5f, zb + 1.9f, zb + 2.25f, 0.f, 0.16f, pk(1.f, 0.8f, 0.5f, 0.35f), neonMat(6u, 0u), WF_POST | WF_BOTTOM);
        }
    }
    if (shack || farm) return;
    // AC condenser on a pad beside the house (side wall, toward the back)
    for (const Wall& w : walls) {
        if (fabsf(w.facing) > 0.3f || !d.r.chance(0.75f)) continue;
        float s = w.len * d.r.range(0.2f, 0.4f);
        vec2 cp = w.a + w.t * s + w.n * 0.9f;
        float gz = d.map->heightAt(cp.x, cp.y);
        obox(k, vec3(cp, gz + 0.04f), vec3(w.t, 0.f), vec3(w.n, 0.f), vec3(0.6f, 0.55f, 0.06f), pk(0.75f), MM(MAT_CONCRETE));
        obox(k, vec3(cp, gz + 0.45f), vec3(w.t, 0.f), vec3(w.n, 0.f), vec3(0.42f, 0.42f, 0.36f), pk(0.82f, 0.82f, 0.78f), MM(MAT_METAL_PAINTED), false);
        {
            // fan grille on top
            vec3 c3 = vec3(cp, gz + 0.815f) - k.org, A(w.t * 0.32f, 0.f), B(w.n * 0.32f, 0.f);
            k.m->quadFacing(c3 - A - B, c3 + A - B, c3 + A + B, c3 - A + B, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), pk(0.12f), MM(MAT_METAL_BRUSHED), vec3(0, 0, 1));
        }
        // refrigerant line into the wall
        wbox(k, w, s - 0.03f, s + 0.03f, gz + 0.5f, gz + 1.4f, 0.02f, 0.08f, pk(0.9f), MM(MAT_METAL_PAINTED), WF_FRONT);
        break;
    }
    if (!d.room()) return;
    // --- garden
    lawn(d);
    vec2 ay = perp(b.ax);
    // garage wing position (same rule as buildmesh.cpp) so hedges and walls leave the driveway open
    bool garage = (b.style == BS_HOUSE && (b.seed % 10u) < 7u) || villa;
    float gside = (b.seed & 64u) ? 1.f : -1.f;
    vec2 gcen = b.c + b.ax * (gside * (b.hx + 3.2f));
    float frontY = dot(b.lotC - b.c, b.front) + b.lotHy;  // front lot line relative to the house center along front
    vec2 frontMid = b.c + b.front * (frontY - 0.35f);
    vec2 lotCenterAlong = b.lotC;
    float hx = b.lotHx - 0.4f;
    vec3 hedgeTint = vec3(0.75f + 0.2f * d.r.f(), 0.95f + 0.15f * d.r.f(), 0.6f);
    bool hedges = villa ? d.r.chance(0.85f) : d.r.chance(0.5f);
    bool gardenWall = villa && (b.region == REG_KEY_CORAL || b.region == REG_BAY_ISLAND || b.region == REG_GROVE) && d.r.chance(0.6f);
    // lot corners (front-left, front-right, back-left, back-right) in world space
    vec2 fl = lotCenterAlong - b.ax * hx + b.front * (b.lotHy - 0.4f), frt = lotCenterAlong + b.ax * hx + b.front * (b.lotHy - 0.4f);
    vec2 bl = lotCenterAlong - b.ax * hx - b.front * (b.lotHy - 0.4f), brt = lotCenterAlong + b.ax * hx - b.front * (b.lotHy - 0.4f);
    (void)ay;
    (void)frontMid;
    if (hedges) {
        float hh = villa ? 1.8f : d.r.range(1.1f, 1.6f);
        hedge(d, fl + b.front * -1.f, bl, hh, 0.9f, hedgeTint, true);
        hedge(d, frt + b.front * -1.f, brt, hh, 0.9f, hedgeTint, true);
        if (d.r.chance(0.6f)) hedge(d, bl, brt, hh, 0.9f, hedgeTint, true);
    }
    // front boundary: low hedge or stucco garden wall with gaps for the driveway and the front walk
    if ((hedges && d.r.chance(0.6f)) || gardenWall) {
        float dAlong = dot(gcen - lotCenterAlong, b.ax);
        float walkAlong = dot(fw.a + fw.t * (fw.bays / 2 * fw.bw) - lotCenterAlong, b.ax);
        struct Gap { float a0, a1; } gaps[2] = {{dAlong - 3.2f, dAlong + 3.2f}, {walkAlong - 0.9f, walkAlong + 0.9f}};
        int ng = garage ? 2 : 1;
        if (!garage) gaps[0] = gaps[1];
        // sort gaps
        if (ng == 2 && gaps[1].a0 < gaps[0].a0) std::swap(gaps[0], gaps[1]);
        float cur = -hx;
        for (int gi = 0; gi <= ng; gi++) {
            float end = gi < ng ? gaps[gi].a0 : hx;
            if (end - cur > 0.8f) {
                vec2 p0 = lotCenterAlong + b.ax * cur + b.front * (b.lotHy - 0.4f), p1 = lotCenterAlong + b.ax * end + b.front * (b.lotHy - 0.4f);
                if (gardenWall) {
                    float L = end - cur;
                    vec2 mc = (p0 + p1) * 0.5f;
                    float gz = Min(d.map->heightAt(p0.x, p0.y), d.map->heightAt(p1.x, p1.y));
                    obox(k, vec3(mc, gz + 0.7f), vec3(b.ax, 0.f), vec3(b.front, 0.f), vec3(L * 0.5f, 0.15f, 0.9f), d.wallTone, d.wallMat, false);
                    obox(k, vec3(mc, gz + 1.65f), vec3(b.ax, 0.f), vec3(b.front, 0.f), vec3(L * 0.5f + 0.05f, 0.2f, 0.06f), d.trim, d.trimMat, true);
                    for (float a = 0.f; a <= L + 0.01f; a += Max(2.f, L / Max(1.f, floorf(L / 4.f)))) {
                        vec2 pp = p0 + b.ax * a;
                        obox(k, vec3(pp, gz + 0.85f), vec3(b.ax, 0.f), vec3(b.front, 0.f), vec3(0.25f, 0.25f, 1.05f), d.trim, d.trimMat, false);
                    }
                    if (k.col) {
                        CollisionBox cb;
                        cb.c = vec3(mc, gz + 0.8f);
                        cb.ax = b.ax;
                        cb.he = vec3(L * 0.5f, 0.2f, 0.9f);
                        k.col->push_back(cb);
                    }
                } else {
                    hedge(d, p0, p1, 0.75f, 0.7f, hedgeTint, false);
                }
            }
            if (gi < ng) cur = gaps[gi].a1;
        }
    }
    // flower beds along the front wall, left and right of the door
    if (d.r.chance(villa ? 0.8f : 0.55f)) {
        const vec3 flowers[] = {vec3(1.f, 0.35f, 0.5f), vec3(1.f, 0.85f, 0.3f), vec3(0.95f, 0.95f, 0.9f), vec3(0.9f, 0.3f, 0.2f), vec3(0.7f, 0.45f, 1.f)};
        float sd = (fw.bays / 2) * fw.bw;
        for (int e = 0; e < 2; e++) {
            float s0 = e == 0 ? 0.4f : sd + 1.4f, s1 = e == 0 ? sd - 1.4f : fw.len - 0.4f;
            if (s1 - s0 < 1.f) continue;
            vec2 mp = fw.a + fw.t * ((s0 + s1) * 0.5f) + fw.n * 0.6f;
            float gz = d.map->heightAt(mp.x, mp.y);
            wbox(k, fw, s0, s1, gz - 0.1f, gz + 0.12f, 0.02f, 1.0f, pk(0.4f, 0.3f, 0.22f), MM(MAT_DIRT), WF_FRONT | WF_TOP);
            int nclump = Clamp((int)((s1 - s0) / 1.8f), 1, 3);
            for (int i = 0; i < nclump; i++) {
                float s = Lerp(s0 + 0.4f, s1 - 0.4f, (i + 0.5f) / nclump);
                vec3 fc = flowers[(d.r.next() + i) % ARRAY_COUNT(flowers)];
                bool green = (i & 1) == 1;
                float hgt = green ? d.r.range(0.5f, 0.9f) : d.r.range(0.3f, 0.55f);
                wbox(k, fw, s - 0.42f, s + 0.42f, gz + 0.1f, gz + 0.1f + hgt, 0.1f, 0.9f, foliageTint(green ? vec3(0.6f, 0.95f, 0.45f) : fc), MM(MAT_LEAVES),
                     WF_FRONT | WF_TOP | WF_START | WF_END);
            }
        }
    }
    // bougainvillea at a front corner (villas often, houses sometimes)
    if (d.r.chance(villa ? 0.6f : 0.15f)) {
        bool left = d.r.chance(0.5f);
        float s0 = left ? 0.1f : fw.len - 2.6f;
        vec2 mp = fw.a + fw.t * (s0 + 1.2f);
        bougainvillea(d, fw, s0, s0 + 2.5f, d.map->heightAt(mp.x, mp.y), zb + Min(f.groundH + 1.2f, ms.z1 - zb));
    }
    // mailbox at the front lot line next to the driveway (or the walk)
    {
        float dAlong = garage ? dot(gcen - lotCenterAlong, b.ax) + (gside > 0.f ? -3.6f : 3.6f) : dot(b.c - lotCenterAlong, b.ax) + 1.4f;
        vec2 mp = lotCenterAlong + b.ax * Clamp(dAlong, -hx, hx) + b.front * (b.lotHy - 0.6f);
        float gz = d.map->heightAt(mp.x, mp.y);
        k.m->cylinder(vec3(mp, gz) - k.org, 0.045f, 0.045f, 1.08f, 4, pk(0.95f), MM(MAT_WOOD), false);
        obox(k, vec3(mp, gz + 1.18f), vec3(b.front, 0.f), vec3(b.ax, 0.f), vec3(0.25f, 0.12f, 0.1f), villa ? pk(0.15f) : pk(0.2f, 0.25f, 0.45f), MM(MAT_METAL_PAINTED));
    }
}

// Warehouses and factories: personnel doors with canopies and wall packs, dock bumpers, downpipes
void warehouseDetail(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls) {
    const Building& b = *d.b;
    Sink& k = d.k;
    float z0 = ms.vBase;
    int fi = 0;
    for (int i = 1; i < (int)walls.size(); i++)
        if (walls[i].facing > walls[fi].facing) fi = i;
    const Wall& fw = walls[fi];
    // loading doors (buildmesh.cpp) are centered at u = -hx + (k + 0.5) * 2hx / doors along ax; convert to wall s
    int doors = Max(1, (int)(b.hx / 7.f));
    bool alongAx = fabsf(dot(fw.t, b.ax)) > 0.7f;
    for (int kd = 0; kd < doors && alongAx; kd++) {
        float u = -b.hx + (kd + 0.5f) * (2.f * b.hx / doors);
        vec2 dp = b.c + b.ax * u + b.front * b.hy;
        float s = dot(dp - fw.a, fw.t);
        // dock bumpers and a wall pack light above each door
        for (int e = -1; e <= 1; e += 2) wbox(k, fw, s + e * 1.95f - 0.15f, s + e * 1.95f + 0.15f, z0 + 0.2f, z0 + 0.75f, 0.f, 0.14f, pk(0.08f), MM(MAT_RUBBER), WF_POST | WF_TOP);
        wbox(k, fw, s - 0.2f, s + 0.2f, z0 + 4.5f, z0 + 4.75f, 0.f, 0.22f, pk(0.35f), MM(MAT_METAL_PAINTED), WF_POST | WF_TOP);
        wquad(k, fw, s - 0.16f, s + 0.16f, z0 + 4.52f, z0 + 4.6f, 0.225f, pk(1.f, 0.85f, 0.6f, 0.5f), neonMat(6u, 0u));
        if ((kd & 1) == 0) addLight(k, vec3(fw.a + fw.t * s + fw.n * 0.6f, z0 + 4.3f), vec3(1.f, 0.85f, 0.6f) * 1500.f, 14.f, 1, vec3(0, 0, -1), 0.35f);
    }
    // personnel door near one end of the front
    {
        float s = fw.len > 12.f ? 2.2f : fw.len * 0.5f;
        wbox(k, fw, s - 0.5f, s + 0.5f, z0, z0 + 2.2f, 0.f, 0.04f, pk(0.35f, 0.4f, 0.45f), MM(MAT_METAL_PAINTED), WF_FRONT);
        wbox(k, fw, s - 0.9f, s + 0.9f, z0 + 2.6f, z0 + 2.7f, 0.f, 1.1f, pk(0.6f), MM(MAT_METAL_PAINTED), WF_BOX | WF_BOTTOM);
        wbox(k, fw, s - 0.12f, s + 0.12f, z0 + 2.3f, z0 + 2.5f, 0.f, 0.12f, pk(1.f, 0.9f, 0.7f, 0.4f), neonMat(6u, 0u), WF_POST | WF_BOTTOM);
    }
    // downpipes along the long walls
    u32 pipeC = pk(0.55f, 0.56f, 0.55f);
    for (const Wall& w : walls) {
        if (w.len < 8.f) continue;
        int np = Max(1, (int)(w.len / 22.f));
        for (int i = 0; i <= np; i++) {
            float s = Lerp(0.6f, w.len - 0.6f, (float)i / np);
            if (&w == &fw && i > 0 && i < np) continue;  // keep the front clear between loading doors
            downpipe(d, w, s, ms.z1 - 0.1f, z0, pipeC);
        }
    }
    // graffiti on side and back walls in the Flats / Calle Luna / Fort Castell
    if (d.graffiti) {
        for (const Wall& w : walls) {
            if (&w == &fw || w.len < 6.f || !d.r.chance(0.55f)) continue;
            int np = d.r.irange(1, Clamp((int)(w.len / 12.f), 1, 3));
            for (int i = 0; i < np; i++) graffiti(d, w, w.len * (i + 0.5f) / np + d.r.range(-2.f, 2.f), z0 + d.r.range(0.5f, 1.1f), d.r.range(0.8f, 1.6f), d.r);
        }
    }
}

}  // namespace facade_detail

// ---------------------------------------------------------------------------------------------------------------
void buildFacadeDetail(const Building& b, const FacadeGPU& fac, const WorldMap& map, vec3 org, MeshData& m, std::vector<CollisionBox>* col,
                       std::vector<PropInstance>* props, std::vector<LightInstance>* lights, const std::vector<FacadeMass>& masses) {
    using namespace facade_detail;
    FD d;
    d.k.m = &m;
    d.k.org = org;
    d.k.col = col;
    d.k.props = props;
    d.k.lights = lights;
    d.b = &b;
    d.f = &fac;
    d.map = &map;
    d.r = Rng(b.seed ^ 0xFAC4DE71u);
    d.v0 = m.verts.size();
    d.reg = b.region;
    d.old = b.region == REG_CALLE_LUNA || b.region == REG_FLATS || b.region == REG_MIDTOWN || b.region == REG_NORTH_CITY || b.region == REG_FORT_CASTELL;
    d.graffiti = b.region == REG_CALLE_LUNA || b.region == REG_FLATS || b.region == REG_FORT_CASTELL;
    // colors: facade tone reproduced on the wall material, trims, accents
    d.wallRGB = rgbOf(fac.wallColor);
    MaterialId wm = (MaterialId)Clamp((int)fac.wallLayer, 0, (int)MAT_COUNT - 1);
    if (wm == MAT_FACADE) wm = MAT_STUCCO;
    d.wallMat = MM(wm);
    d.wallTone = pk(d.wallRGB * 1.55f);
    vec3 frameRGB = rgbOf(fac.frameColor);
    d.frame = pk(frameRGB);
    bool brick = wm == MAT_BRICK;
    float tr = d.r.f();
    vec3 trimRGB = tr < 0.55f ? vec3(0.97f, 0.95f, 0.9f) : (tr < 0.8f ? d.wallRGB * 1.35f : d.wallRGB * 0.95f + vec3(0.1f));
    if (brick) trimRGB = vec3(0.92f, 0.88f, 0.8f);
    if (b.style == BS_DECO) trimRGB = vec3(0.98f, 0.97f, 0.94f);
    d.trim = pk(trimRGB);
    d.trimMat = MM(brick ? MAT_STONE : MAT_PLASTER);
    d.dark = pk(d.wallRGB * 0.55f + vec3(0.05f));
    d.accent = b.style == BS_DECO ? pk(frameRGB) : pk(lerp(d.wallRGB, frameRGB, 0.6f));
    d.budget = b.style == BS_TOWER ? 6000 : (b.style == BS_HOUSE ? 1300 : (b.style == BS_VILLA ? 2200 : (b.style == BS_DECO ? 4500 : 3600)));

    thread_local std::vector<Wall> walls;
    const bool house = b.style == BS_HOUSE || b.style == BS_VILLA || b.style == BS_FARMHOUSE || b.style == BS_SHACK;
    const bool industrial = b.style == BS_WAREHOUSE || b.style == BS_FACTORY || b.style == BS_BARN;
    // per-building architectural scheme
    int corniceKind = d.r.irange(0, 3);
    if (b.style == BS_DECO) corniceKind = CO_STEPPED;
    if (b.style == BS_CONDO || b.style == BS_GARAGE || b.style == BS_STRIPMALL || b.style == BS_GASSTATION) corniceKind = CO_SLAB;
    if (industrial) corniceKind = CO_BAND;
    int trimKind = d.r.irange(0, 3);
    bool sillCourses = d.r.chance(0.35f);
    int pilasterEvery = d.r.chance(0.4f) ? d.r.irange(2, 3) : 0;
    bool plinth = d.r.chance(0.7f);

    for (const FacadeMass& ms : masses) {
        walls.clear();
        for (int i = 0; i < (int)ms.fp.size(); i++) walls.push_back(makeWall(ms.fp, i, fac.bayW, b.front));
        bool bottom = fabsf(ms.z0 - ms.vBase) < 0.6f;
        if (house && ms.kind == FM_HOUSE) {
            houseDetail(d, ms, walls);
            continue;
        }
        if (ms.kind == FM_DECO_TOWER) {
            // blade sign in front of the tower element (hotel name in vertical neon)
            int fi = 0;
            for (int i = 1; i < (int)walls.size(); i++)
                if (walls[i].facing > walls[fi].facing) fi = i;
            const Wall& w = walls[fi];
            float H = b.height;
            float z0s = b.baseZ + H * 0.5f + 3.f - H * 0.35f, z1s = b.baseZ + H * 0.5f + 3.f + H * 0.35f;
            vec3 nc = neonHue(d.r);
            u32 anim = d.r.chance(0.25f) ? (d.r.chance(0.5f) ? 2u : 7u) : 6u;
            bladeSign(d, w, w.len * 0.5f, Max(z0s, b.baseZ + fac.groundH + 0.3f), z1s, 0.15f, 1.95f, kHotelWords[d.r.next() % ARRAY_COUNT(kHotelWords)], nc,
                      pk(0.95f, 0.94f, 0.9f), anim);
            // finial on top of the tower element
            vec2 tc = (ms.fp[0] + ms.fp[2]) * 0.5f;
            m.cylinder(vec3(tc, ms.z1) - org, 0.35f, 0.05f, 4.5f, 8, d.trim, d.trimMat, false);
            m.cylinder(vec3(tc, ms.z1 + 4.4f) - org, 0.22f, 0.22f, 0.3f, 8, pk(nc, 0.8f), neonMat(6u, 0u), true);
            continue;
        }
        bool tower = b.style == BS_TOWER && (ms.kind == FM_TIER || (ms.kind == FM_MAIN && b.style == BS_TOWER));
        if (tower) {
            int finMode = (int)fac.style == 1 ? (d.r.chance(0.45f) ? 1 : 0) : 2;
            towerTier(d, ms, walls, finMode);
            if (ms.parapet)
                for (const Wall& w : walls)
                    if (w.len > 1.f) coping(d, w, ms.z1);
            continue;
        }
        // ---- flat-roof urban masses (main blocks, podiums), shops, motels, warehouses
        int fi = 0;
        for (int i = 1; i < (int)walls.size(); i++)
            if (walls[i].facing > walls[fi].facing) fi = i;
        bool store = (fac.flags & 1u) != 0;
        int floors = (int)((ms.z1 - ms.vBase - fac.groundH) / Max(fac.floorH, 1.f) + 1.01f);
        for (const Wall& w : walls) {
            if (w.len < 1.2f) continue;
            bool front = &w == &walls[fi];
            // base and ground-floor course
            if (bottom && plinth && !(front && store) && !industrial)
                wbox(d.k, w, -0.05f, w.len + 0.05f, ms.vBase - 0.5f, ms.vBase + 0.55f, 0.f, 0.05f, d.dark, d.wallMat, WF_FRONT | WF_TOP);
            if (bottom && floors >= 2 && !industrial) stringCourse(d, w, ms.vBase + fac.groundH, 0.22f, 0.12f, d.trim, d.trimMat);
            if (sillCourses && !industrial && (int)fac.style == 0) {
                FloorRow rows[64];
                int nr = massFloors(fac, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
                for (int ri = 0; ri < nr; ri++)
                    if (rows[ri].idx > 0) stringCourse(d, w, ms.vBase + rows[ri].v0 + fac.sillH - 0.05f, 0.1f, 0.08f, d.trim, d.trimMat);
            }
            if (ms.z1 - ms.vBase > 2.5f && b.roof == ROOF_FLAT) cornice(d, w, ms.z1, corniceKind);
            if (ms.parapet) coping(d, w, ms.z1);
            // front: pilasters, window trims, storefront; other walls: sills only (older fabric) for the silhouette
            if (front) {
                if (!industrial && (pilasterEvery > 0 || b.style == BS_SHOPS))
                    pilasters(d, w, ms.vBase + (store ? fac.groundH : 0.f), ms.z1 - (corniceKind == CO_CLASSIC ? 0.62f : 0.34f), b.style == BS_SHOPS ? 0 : pilasterEvery,
                              0.55f, 0.12f, d.trim, d.trimMat);
                if (!industrial) windowTrims(d, w, ms, trimKind, false, 0, 0);
                if (store && bottom) storefront(d, w, ms.vBase, w.bays / 2);
            } else if (d.old && !industrial && (int)fac.style == 0 && fabsf(w.facing) > 0.7f) {
                // back walls of the older fabric: one sill course per floor
                FloorRow rows[64];
                int nr = massFloors(fac, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
                for (int ri = 0; ri < nr; ri++)
                    if (rows[ri].idx > 0) stringCourse(d, w, ms.vBase + rows[ri].v0 + fac.sillH - 0.05f, 0.1f, 0.08f, d.trim, d.trimMat);
            }
        }
        if (!bottom) continue;
        const Wall& fw = walls[fi];
        // ---- shop fronts: awning or security gates, blade sign, window neon
        if (store && fw.len > 3.f && (b.style == BS_SHOPS || b.style == BS_MIDRISE || b.style == BS_DECO)) {
            float zTopGlass = ms.vBase + 0.35f + (fac.groundH - 1.3f);
            bool awn = b.style != BS_DECO && d.r.chance(0.55f);
            if (awn) {
                vec3 ac = hsvToRgb(d.r.f(), d.r.range(0.45f, 0.8f), d.r.range(0.55f, 0.9f));
                u32 cb = d.r.chance(0.6f) ? pk(0.96f, 0.95f, 0.9f) : pk(ac * 0.6f);
                bool perBay = d.r.chance(0.35f) && fw.bays >= 2;
                if (perBay) {
                    for (int i = 0; i < fw.bays; i++)
                        awningStriped(d, fw, (i + 0.06f) * fw.bw, (i + 0.94f) * fw.bw, zTopGlass + 0.02f, 1.3f, pk(ac), cb, true);
                } else {
                    awningStriped(d, fw, 0.25f, fw.len - 0.25f, zTopGlass + 0.02f, 1.8f, pk(ac), cb, d.r.chance(0.75f));
                }
            } else if (d.old && d.r.chance(0.65f)) {
                vec3 gc = d.r.chance(0.6f) ? vec3(0.62f, 0.63f, 0.64f) : hsvToRgb(d.r.f(), 0.4f, 0.6f);
                bool tagged = d.graffiti && d.r.chance(0.5f);
                for (int i = 0; i < fw.bays; i++) {
                    float s0 = (i + 0.04f) * fw.bw, s1 = (i + 0.96f) * fw.bw;
                    bool closed = d.r.chance(0.3f);
                    float closedTo = closed ? Lerp(ms.vBase + 0.35f, zTopGlass - 0.5f, d.r.f()) : zTopGlass + 1.f;
                    securityGate(d, fw, s0, s1, zTopGlass, closedTo, pk(gc));
                    if (closed && tagged && closedTo < zTopGlass - 1.3f) {
                        Rng gr(b.seed * 31u + (u32)i);
                        graffiti(d, fw, (s0 + s1) * 0.5f, closedTo + 0.15f, Min(0.9f, zTopGlass - closedTo - 0.6f), gr);
                    }
                }
            }
            if ((d.old || d.reg == REG_BEACH) && d.r.chance(0.4f) && fw.len > 4.f) {
                float s = d.r.chance(0.5f) ? 0.45f : fw.len - 0.45f;
                float z0s = ms.vBase + fac.groundH + 0.25f;
                float z1s = Min(z0s + d.r.range(2.2f, 3.4f), ms.z1 + 0.5f);
                if (z1s - z0s > 1.6f) {
                    vec3 nc = neonHue(d.r);
                    u32 anim = d.r.chance(0.2f) ? 1u : (d.r.chance(0.2f) ? 2u : 6u);
                    const char* wd = kShopWords[d.r.next() % ARRAY_COUNT(kShopWords)];
                    bladeSign(d, fw, s, z0s, z1s, 0.25f, 1.25f, wd, nc, d.r.chance(0.5f) ? pk(0.1f, 0.1f, 0.12f) : pk(0.92f, 0.9f, 0.85f), anim);
                }
            }
            if (d.r.chance(0.3f)) {
                const char* wds[] = {"OPEN", "OPEN", "OPEN", "BAR", "CAFE", "ATM", "LOTTO"};
                vec3 nc = d.r.chance(0.5f) ? vec3(1.f, 0.15f, 0.12f) : neonHue(d.r);
                int bi = fw.bays > 1 ? (fw.bays / 2 + 1) % fw.bays : 0;
                windowNeon(d, fw, (bi + 0.5f) * fw.bw, ms.vBase + 1.55f, wds[d.r.next() % ARRAY_COUNT(wds)], nc, d.r.chance(0.3f) ? 7u : 6u);
            }
        }
        // ---- balconies
        if ((b.style == BS_MIDRISE && (d.reg == REG_BEACH || d.reg == REG_NORTH_CITY || d.reg == REG_MIDTOWN || d.reg == REG_GROVE) && d.r.chance(0.3f)) ||
            (b.style == BS_DECO && d.r.chance(0.35f)))
            balconies(d, fw, ms, d.r.irange(0, 2), b.style == BS_DECO);
        // ---- condo unit dividers and metal top rails on the existing slabs
        if (b.style == BS_CONDO) {
            FloorRow rows[64];
            int nr = massFloors(fac, 0.f, ms.z1 - ms.vBase, rows, 64);
            for (const Wall& w : walls) {
                if (fabsf(w.facing) < 0.7f) continue;
                for (int ri = 0; ri < nr && d.room(); ri++) {
                    if (rows[ri].idx == 0) continue;
                    float z = ms.vBase + rows[ri].v0;
                    // glass railing top rail (railing boxes sit 0.75 m beyond the slab edge line in buildmesh)
                    wbox(d.k, w, w.len * 0.04f, w.len * 0.96f, z + 1.1f, z + 1.16f, 1.52f, 1.6f, pk(0.75f, 0.77f, 0.8f), MM(MAT_METAL_BRUSHED), WF_FRONT | WF_TOP);
                    for (int i = 2; i < w.bays - 1; i += 2) {
                        float sb = i * w.bw;
                        if (sb < w.len * 0.05f || sb > w.len * 0.95f) continue;
                        wbox(d.k, w, sb - 0.06f, sb + 0.06f, z + 0.12f, z + fac.floorH - 0.2f, 0.f, 1.55f, d.wallTone, d.wallMat, WF_POST);
                    }
                }
            }
        }
        // ---- fire escapes on brick walk-ups
        if (b.style == BS_MIDRISE && wm == MAT_BRICK && floors >= 3 && fw.bays >= 3 && d.r.chance(0.7f)) {
            int b0 = d.r.chance(0.5f) ? 1 : Max(1, fw.bays - 3);
            fireEscape(d, fw, ms, b0, 2);
        }
        // ---- window AC units (older fabric), downpipes at the back corners
        if (d.old && (b.style == BS_MIDRISE || b.style == BS_SHOPS || b.style == BS_MOTEL) && (int)fac.style == 0) {
            FloorRow rows[64];
            int nr = massFloors(fac, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
            float p = b.region == REG_CALLE_LUNA ? 0.16f : 0.1f;
            for (const Wall& w : walls) {
                if (w.len < 3.f || fabsf(w.facing) < 0.7f) continue;
                for (int ri = 0; ri < nr && d.room(); ri++) {
                    WinSpec ws = winSpec(fac, rows[ri].idx == 0);
                    if (ws.store) continue;
                    for (int i = 0; i < w.bays; i++) {
                        if (shHashF(b.seed * 977u + (u32)(w.idx * 131 + ri * 17 + i)) > p) continue;
                        float s0 = (i + ws.x0) * w.bw, s1 = s0 + ws.w * w.bw;
                        acUnit(d, w, (s0 + s1) * 0.5f, ms.vBase + rows[ri].v0 + ws.sill, s1 - s0);
                    }
                }
            }
        }
        if (!industrial && b.style != BS_GASSTATION) {
            int bi = (fi + 2) % (int)walls.size();
            const Wall& bw = walls[bi];
            if (bw.len > 3.f) {
                u32 pc = d.r.chance(0.5f) ? d.trim : pk(0.5f, 0.52f, 0.5f);
                downpipe(d, bw, 0.4f, ms.z1 + (ms.parapet ? 0.6f : 0.f), ms.vBase, pc);
                downpipe(d, bw, bw.len - 0.4f, ms.z1 + (ms.parapet ? 0.6f : 0.f), ms.vBase, pc);
            }
        }
        if (industrial) warehouseDetail(d, ms, walls);
        // ---- art deco front
        if (b.style == BS_DECO) decoFront(d, walls[fi], ms);
        // ---- motel doors along the ground floor
        if (b.style == BS_MOTEL) {
            const Wall& w = walls[fi];
            for (int i = 0; i < w.bays; i++) {
                float s = (i + 0.2f) * w.bw;
                vec3 dc = hsvToRgb(d.r.f(), 0.5f, 0.75f);
                wbox(d.k, w, s - 0.45f, s + 0.45f, ms.vBase, ms.vBase + 2.1f, 0.f, 0.04f, pk(dc), MM(MAT_WOOD), WF_FRONT);
                wbox(d.k, w, s - 0.58f, s + 0.58f, ms.vBase + 2.1f, ms.vBase + 2.22f, 0.f, 0.08f, d.trim, d.trimMat, WF_LEDGE | WF_START | WF_END);
                // through-wall AC unit under the window
                float wc = (i + 0.5f) * w.bw + 0.35f;
                wbox(d.k, w, wc - 0.4f, wc + 0.4f, ms.vBase + 0.35f, ms.vBase + 0.75f, 0.f, 0.14f, pk(0.85f, 0.84f, 0.8f), MM(MAT_METAL_PAINTED), WF_BOX);
            }
        }
    }
}

}  // namespace World
