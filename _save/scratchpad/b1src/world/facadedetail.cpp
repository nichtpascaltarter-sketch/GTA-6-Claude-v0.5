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
#include "interiors.h"
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

// Development statistics: vertices emitted per feature (compiled in only with -DNT_FACADE_STATS, used by profiling tools)
#ifdef NT_FACADE_STATS
enum FdStat { FS_TRIMS, FS_STORE, FS_AWNING, FS_GATE, FS_BLADE, FS_WNEON, FS_GRAFFITI, FS_AC, FS_PIPE, FS_FIRE, FS_BALC, FS_DECO, FS_TOWER, FS_LAWN,
              FS_HEDGE, FS_VINE, FS_HOUSE, FS_WARE, FS_CORNICE, FS_COPING, FS_COURSE, FS_PILASTER, FS_COUNT };
long long gFacadeStat[FS_COUNT];
struct FdStatScope {
    MeshData* m;
    int id;
    size_t v0;
    FdStatScope(MeshData* mm, int i) : m(mm), id(i), v0(mm->verts.size()) {}
    ~FdStatScope() { gFacadeStat[id] += (long long)(m->verts.size() - v0); }
};
#define FD_STAT(mesh, id) FdStatScope fdStatScope((mesh), (id))
#else
#define FD_STAT(mesh, id)
#endif

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
    int doorBay = -1;      // front bay holding an enterable interior's entrance (interiors.h): kept clear
    bool room() const { return k.m->verts.size() - v0 < budget; }
};

// ------------------------------------------------------------------------------------------------ moldings
enum Cornice { CO_BAND = 0, CO_CLASSIC, CO_SLAB, CO_STEPPED, CO_NONE };

void cornice(FD& d, const Wall& w, float H, int kind) {
    FD_STAT(d.k.m, FS_CORNICE);
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
    FD_STAT(d.k.m, FS_COPING);
    float e = 0.004f * (float)(w.idx & 1);
    wbox(d.k, w, -0.06f, w.len + 0.06f, H + 0.97f + e, H + 1.07f + e, -0.36f, 0.06f, d.trim, d.trimMat, WF_LEDGE);
}

void stringCourse(FD& d, const Wall& w, float z, float h, float out, u32 col, u32 mat) {
    FD_STAT(d.k.m, FS_COURSE);
    float e = 0.004f * (float)(w.idx & 1);
    wbox(d.k, w, -out, w.len + out, z - h * 0.5f + e, z + h * 0.5f + e, 0.f, out, col, mat, WF_LEDGE);
}

// Pilasters at bay boundaries (corners always); `every` = spacing in bays (0 = corners only)
void pilasters(FD& d, const Wall& w, float z0, float z1, int every, float width, float out, u32 col, u32 mat) {
    FD_STAT(d.k.m, FS_PILASTER);
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
    FD_STAT(d.k.m, FS_TRIMS);
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
            if (fr.idx == 0 && i == d.doorBay && w.facing > 0.9f) continue;   // interior entrance bay
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
    FD_STAT(d.k.m, FS_STORE);
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
        // (an enterable interior's entrance keeps its doorway clear: bulkhead and mullions stop at the door jambs)
        bool walkIn = i == doorBay && d.doorBay == doorBay;
        float wdc = (s0 + s1) * 0.5f, wdw = Min(1.9f, (s1 - s0) * 0.8f) * 0.5f + 0.08f;
        if (walkIn) {
            wbox(k, w, s0, wdc - wdw, vBase - 0.3f, zg0, 0.f, 0.08f, d.dark, MM(MAT_MARBLE), WF_FRONT | WF_TOP | WF_END);
            wbox(k, w, wdc + wdw, s1, vBase - 0.3f, zg0, 0.f, 0.08f, d.dark, MM(MAT_MARBLE), WF_FRONT | WF_TOP | WF_START);
        } else {
            wbox(k, w, s0, s1, vBase - 0.3f, zg0, 0.f, 0.08f, d.dark, MM(MAT_MARBLE), WF_FRONT | WF_TOP);
        }
        wbox(k, w, s0, s1, zg1 - 0.07f, zg1 + 0.02f, 0.f, 0.06f, d.frame, fm, WF_FRONT | WF_BOTTOM);
        for (int j = 1; j < nm; j++) {
            float s = s0 + (s1 - s0) * j / nm;
            if (walkIn && fabsf(s - wdc) < wdw + 0.04f) continue;
            wbox(k, w, s - 0.035f, s + 0.035f, zg0, zg1, 0.f, 0.06f, d.frame, fm, WF_POST);
        }
        float zt = zg0 + Min(2.4f, glassH - 0.45f);
        if (glassH > 2.9f) wbox(k, w, s0, s1, zt - 0.045f, zt + 0.045f, 0.f, 0.06f, d.frame, fm, WF_FRONT | WF_TOP | WF_BOTTOM);
        if (i == doorBay) {
            float dc = (s0 + s1) * 0.5f, dw = Min(1.9f, (s1 - s0) * 0.8f);
            float zh = zg0 - 0.35f + Min(2.3f, glassH);
            wbox(k, w, dc - dw * 0.5f - 0.08f, dc - dw * 0.5f, vBase, zh, 0.f, 0.08f, d.frame, fm, WF_POST);
            wbox(k, w, dc + dw * 0.5f, dc + dw * 0.5f + 0.08f, vBase, zh, 0.f, 0.08f, d.frame, fm, WF_POST);
            wbox(k, w, dc - dw * 0.5f, dc + dw * 0.5f, zh - 0.08f, zh, 0.f, 0.08f, d.frame, fm, WF_FRONT | WF_BOTTOM);
            if (!walkIn) {   // painted-on doors (a walk-in interior has real leaves)
                wbox(k, w, dc - 0.04f, dc + 0.04f, vBase, zh, 0.f, 0.08f, d.frame, fm, WF_FRONT);
                // push bars
                wbox(k, w, dc - dw * 0.42f, dc - 0.12f, vBase + 1.0f, vBase + 1.05f, 0.08f, 0.12f, pk(0.8f, 0.8f, 0.82f), MM(MAT_CHROME), WF_FRONT);
                wbox(k, w, dc + 0.12f, dc + dw * 0.42f, vBase + 1.0f, vBase + 1.05f, 0.08f, 0.12f, pk(0.8f, 0.8f, 0.82f), MM(MAT_CHROME), WF_FRONT);
            }
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
    FD_STAT(d.k.m, FS_AWNING);
    Sink& k = d.k;
    MeshData& m = *k.m;
    float len = s1 - s0;
    if (len < 0.4f) return;
    int n = Max(1, (int)roundf(len / 1.0f));
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
        const int SEG = 2;
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
    // brackets (long awnings only)
    for (int e = 0; e < 2 && len > 4.f; e++) {
        float s = e == 0 ? s0 + 0.08f : s1 - 0.08f;
        vec3 a = vec3(w.a + w.t * s, z - 0.02f), b = vec3(w.a + w.t * s + w.n * depth, z - drop - 0.02f);
        vec3 ax = normalize(b - a);
        obox(k, (a + b) * 0.5f, ax, vec3(w.t, 0.f), vec3(length(b - a) * 0.5f, 0.015f, 0.015f), pk(0.2f), MM(MAT_METAL_PAINTED));
    }
}

// Older walk-ups and shop parades (Calle Luna, the Flats, Midtown, North City, Fort Castell): what makes one old block differ
// from the next. A painted board hanging from an iron bracket over the shop door, little fabric awnings over some upper
// windows, window boxes in flower, and a faded painted sign high on a side wall.
void oldFabricDetail(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls, int fi, int floors, bool store) {
    const Building& b = *d.b;
    const FacadeGPU& f = *d.f;
    Sink& k = d.k;
    const Wall& fw = walls[fi];
    Rng r(b.seed ^ 0x01DFAB1Cu);
    sitegeo::G g;
    g.m = k.m;
    g.d = k.m;
    g.org = k.org;
    g.detail = true;
    const u32 paint = MM(MAT_METAL_PAINTED), iron = pk(0.08f);
    // projecting painted sign on a scrolled bracket
    if (store && fw.len > 5.f && r.chance(0.55f)) {
        static const char* kBoards[] = {"BAKERY", "BARBER", "BOOKS", "TAILOR", "HARDWARE", "PHARMACY", "CAFE", "SHOES", "FLOWERS", "REPAIRS", "LAUNDRY", "DELI"};
        float s = r.chance(0.5f) ? 1.1f : fw.len - 1.1f;
        float zb = ms.vBase + f.groundH + 0.35f;
        wbox(k, fw, s - 0.03f, s + 0.03f, zb + 0.55f, zb + 0.61f, 0.f, 1.2f, iron, paint, WF_FRONT | WF_TOP | WF_BOTTOM | WF_START | WF_END);
        wbox(k, fw, s - 0.02f, s + 0.02f, zb + 0.1f, zb + 0.6f, 0.f, 0.06f, iron, paint, WF_POST);
        {
            // brace from the wall up to the arm's tip
            vec3 a = vec3(fw.a + fw.t * s, zb + 0.15f), c = vec3(fw.a + fw.t * s + fw.n * 0.9f, zb + 0.58f);
            obox(k, (a + c) * 0.5f, normalize(c - a), vec3(fw.t, 0.f), vec3(length(c - a) * 0.5f, 0.012f, 0.012f), iron, paint);
        }
        const vec3 boardPal[] = {vec3(0.1f, 0.25f, 0.18f), vec3(0.42f, 0.08f, 0.1f), vec3(0.1f, 0.14f, 0.3f), vec3(0.9f, 0.86f, 0.74f), vec3(0.05f)};
        int pi = (int)(r.next() % 5u);
        vec3 bc = boardPal[pi], tc = pi == 3 ? vec3(0.15f, 0.1f, 0.06f) : vec3(0.95f, 0.88f, 0.6f);
        float bw = 0.95f, bh = 0.62f, o = 0.72f;
        vec3 top = vec3(fw.a + fw.t * s + fw.n * o, zb + 0.55f);
        // chains, then the board (two faces toward +t and -t, the sign reads along the street)
        for (int e = -1; e <= 1; e += 2) obox(k, top + vec3(fw.n * (e * bw * 0.38f), -0.1f), vec3(0, 0, 1), vec3(fw.t, 0.f), vec3(0.1f, 0.006f, 0.006f), iron, paint);
        vec3 bcen = top + vec3(0, 0, -0.2f - bh * 0.5f);
        obox(k, bcen, vec3(fw.n, 0.f), vec3(fw.t, 0.f), vec3(bw * 0.5f, 0.025f, bh * 0.5f), pk(bc), MM(MAT_WOOD));
        const char* word = kBoards[r.next() % ARRAY_COUNT(kBoards)];
        for (int side = -1; side <= 1; side += 2) {
            vec3 face = vec3(fw.t * (float)side, 0.f);
            vec3 right = vec3(fw.n * (float)side, 0.f) * -1.f;   // reading direction on this face
            float th = 0.16f, tw = sitegeo::textAdvance(word, th, 0.25f);
            if (tw > bw * 0.85f) th *= bw * 0.85f / tw, tw = bw * 0.85f;
            sitegeo::strokeText(g, *k.m, word, bcen + face * 0.028f - right * (tw * 0.5f) - vec3(0, 0, th * 0.5f), right, vec3(0, 0, 1), th, th * 0.16f, pk(tc), paint, 0.f,
                                0.25f);
            // painted border
            for (int e = -1; e <= 1; e += 2)
                sitegeo::quad(g, *k.m, bcen + face * 0.027f + right * (bw * 0.44f * e) + vec3(0, 0, -bh * 0.42f), bcen + face * 0.027f + right * (bw * 0.44f * e + 0.03f * e) + vec3(0, 0, -bh * 0.42f),
                              bcen + face * 0.027f + right * (bw * 0.44f * e + 0.03f * e) + vec3(0, 0, bh * 0.42f), bcen + face * 0.027f + right * (bw * 0.44f * e) + vec3(0, 0, bh * 0.42f),
                              pk(tc), paint, face);
        }
    }
    if (floors < 2 || (int)f.style != 0) return;
    FloorRow rows[64];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
    // little awnings over upper windows (one scheme per building)
    bool winAwn = r.chance(d.reg == REG_CALLE_LUNA ? 0.4f : 0.22f);
    vec3 ac = hsvToRgb(r.f(), r.range(0.45f, 0.75f), r.range(0.45f, 0.8f));
    bool stripes = r.chance(0.5f);
    // window boxes in flower (Calle Luna and Midtown walk-ups)
    bool boxes = (d.reg == REG_CALLE_LUNA || d.reg == REG_MIDTOWN || d.reg == REG_NORTH_CITY) && r.chance(0.45f);
    const vec3 blooms[] = {vec3(0.95f, 0.25f, 0.4f), vec3(1.f, 0.75f, 0.2f), vec3(0.95f, 0.95f, 0.9f), vec3(0.85f, 0.2f, 0.15f), vec3(0.75f, 0.4f, 0.95f)};
    for (int ri = 0; ri < nr && (winAwn || boxes); ri++) {
        if (rows[ri].idx == 0 || !d.room()) continue;
        WinSpec ws = winSpec(f, false);
        for (int i = 0; i < fw.bays; i++) {
            float s0 = (i + ws.x0) * fw.bw, s1 = s0 + ws.w * fw.bw;
            float zs = ms.vBase + rows[ri].v0 + ws.sill, zh = zs + ws.h;
            u32 h = hash3i((int)(b.seed & 0xffffu), ri, i);
            if (winAwn && rows[ri].idx <= 2 && (h % 5u) < 3u)
                awningStriped(d, fw, s0 - 0.08f, s1 + 0.08f, zh + 0.32f, 0.65f, pk(ac), stripes ? pk(0.95f, 0.94f, 0.9f) : pk(ac * 0.8f), true);
            if (boxes && ((h >> 3) % 3u) == 0u) {
                wbox(k, fw, s0 + 0.05f, s1 - 0.05f, zs - 0.22f, zs - 0.02f, 0.f, 0.24f, pk(0.55f, 0.32f, 0.2f), MM(MAT_WOOD), WF_BOX | WF_BOTTOM);
                vec3 c = vec3(fw.a + fw.t * ((s0 + s1) * 0.5f) + fw.n * 0.14f, zs + 0.05f);
                leafBlobEx(*k.m, k.org, c, vec3(fw.t, 0.f), vec3(perp(fw.t), 0.f), vec3((s1 - s0) * 0.46f, 0.16f, 0.16f), vec3(0.6f, 0.95f, 0.45f), h, 0.03f, 8, 4,
                           blooms[(h >> 7) % 5u]);
            }
        }
    }
    // faded painted sign high on a side wall (the old trade of the building)
    if (r.chance(0.35f)) {
        static const char* kGhost[] = {"DRY GOODS", "FURNITURE", "ICE CO", "HOTEL", "COLD SODA", "GROCERY", "TOBACCO", "CIGARS", "HARDWARE", "LAUNDRY"};
        for (const Wall& w : walls) {
            if (fabsf(w.facing) > 0.3f || w.len < 6.f) continue;
            float H = ms.z1 - ms.vBase;
            if (H < 7.f) break;
            const char* word = kGhost[r.next() % ARRAY_COUNT(kGhost)];
            float th = Min(1.1f, H * 0.14f), tw = sitegeo::textAdvance(word, th, 0.3f);
            if (tw > w.len * 0.85f) th *= w.len * 0.85f / tw, tw = w.len * 0.85f;
            vec3 right(w.t, 0.f);
            vec3 o = vec3(w.a + w.t * ((w.len - tw) * 0.5f) + w.n * 0.03f, ms.z1 - th * 1.9f);
            vec3 faded = lerp(d.wallRGB * 1.5f, vec3(0.92f, 0.88f, 0.78f), 0.55f);
            // a painted band behind the letters, then the letters
            sitegeo::quad(g, *k.m, o - right * 0.3f - vec3(0, 0, th * 0.35f), o + right * (tw + 0.3f) - vec3(0, 0, th * 0.35f), o + right * (tw + 0.3f) + vec3(0, 0, th * 1.35f),
                          o - right * 0.3f + vec3(0, 0, th * 1.35f), pk(lerp(d.wallRGB * 1.3f, vec3(0.35f, 0.18f, 0.12f), 0.35f)), MM(MAT_PAINT_WHITE), vec3(w.n, 0.f));
            sitegeo::strokeText(g, *k.m, word, o + vec3(w.n * 0.004f, 0.f), right, vec3(0, 0, 1), th, th * 0.15f, pk(faded), MM(MAT_PAINT_WHITE), 0.f, 0.3f);
            break;
        }
    }
}

// Roll-down security gate: housing at the top of the storefront glass; optionally pulled partly down
void securityGate(FD& d, const Wall& w, float s0, float s1, float zTop, float closedTo, u32 panelCol) {
    FD_STAT(d.k.m, FS_GATE);
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
    FD_STAT(d.k.m, FS_BLADE);
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
    FD_STAT(d.k.m, FS_WNEON);
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
    FD_STAT(d.k.m, FS_GRAFFITI);
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
    FD_STAT(d.k.m, FS_AC);
    Sink& k = d.k;
    float hw = Min(0.34f, wWin * 0.4f);
    u32 body = d.r.chance(0.7f) ? pk(0.88f, 0.87f, 0.82f) : pk(0.62f, 0.6f, 0.56f);
    wbox(k, w, sc - hw, sc + hw, zSill + 0.02f, zSill + 0.44f, -0.03f, 0.42f, body, MM(MAT_METAL_PAINTED), WF_BOX | WF_BOTTOM);
    wquad(k, w, sc - hw + 0.05f, sc + hw - 0.05f, zSill + 0.07f, zSill + 0.38f, 0.425f, pk(0.25f, 0.25f, 0.26f), MM(MAT_METAL_BRUSHED));
}

void downpipe(FD& d, const Wall& w, float s, float zTop, float zBot, u32 col) {
    FD_STAT(d.k.m, FS_PIPE);
    Sink& k = d.k;
    wbox(k, w, s - 0.055f, s + 0.055f, zBot + 0.25f, zTop, 0.04f, 0.15f, col, MM(MAT_METAL_PAINTED), WF_POST);
    wbox(k, w, s - 0.07f, s + 0.07f, zBot, zBot + 0.25f, 0.03f, 0.3f, col, MM(MAT_METAL_PAINTED), WF_FRONT | WF_TOP);
}

// Fire escape on a brick midrise: platforms and railings at each upper floor, switchback stairs, drop ladder
void fireEscape(FD& d, const Wall& w, const FacadeMass& ms, int bay0, int nb) {
    FD_STAT(d.k.m, FS_FIRE);
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
    FD_STAT(d.k.m, FS_BALC);
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
    FD_STAT(d.k.m, FS_DECO);
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
    FD_STAT(d.k.m, FS_TOWER);
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
    FD_STAT(d.k.m, FS_LAWN);
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

// Hedge between two ground points: a leafy swept mass following the terrain (propmesh.cpp hedgeMesh), colliding in pieces
// The stretches of a lot boundary a..b that keep out of the turning room at nearby dead ends: the lane graph turns cars
// round in a half circle of up to 6.5 m radius ahead of the dead end (pulled back along the street if something stands
// in the way), and the car body needs another metre either side. Appends [t0, t1] parameter ranges of a..b.
void clearOfTurnarounds(vec2 a, vec2 b, float halfThick, std::vector<vec2>& keep) {
    keep.clear();
    float L = length(b - a);
    std::vector<vec4> caps;   // turning room: capsule p..q, radius in z
    if (gRoads && L > 1e-3f) {
        std::vector<int> cand;
        gRoads->edgesInRect(vmin(a, b) - vec2(20.f), vmax(a, b) + vec2(20.f), cand);
        for (int ei : cand) {
            const RoadEdge& e = gRoads->edges[ei];
            if (e.pts.size() < 2 || (e.flags & RF_ONEWAY) || e.lanesF == 0 || e.lanesB == 0) continue;
            for (int end = 0; end < 2; end++) {
                int nn = end ? e.n1 : e.n0;
                const RoadNode& nd = gRoads->nodes[nn];
                if (nd.edges.size() != 1) continue;
                vec2 inward = normalize((end ? e.pts[e.pts.size() - 2] : e.pts[1]).xy() - nd.p);
                const RoadClassInfo& ri = roadInfo(e.cls);
                float R = Clamp((ri.median > 0.f ? ri.median * 0.5f : 0.f) + ri.laneWidth * 0.5f + 3.4f, 4.8f, 6.5f);
                caps.push_back(vec4(nd.p.x, nd.p.y, R + 1.1f + 0.4f + halfThick, 0.f));
                caps.back().w = atan2f(inward.y, inward.x);
            }
        }
    }
    if (caps.empty()) {
        keep.push_back(vec2(0.f, 1.f));
        return;
    }
    int n = Max(2, (int)ceilf(L / 0.4f));
    float run = -1.f;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        vec2 p = lerp(a, b, t);
        bool clear = true;
        for (const vec4& c : caps) {
            vec2 c0(c.x, c.y), c1 = c0 + vec2(cosf(c.w), sinf(c.w)) * 6.f;   // the room and its pulled-back positions
            if (distPointSegment2D(p, c0, c1) < c.z) { clear = false; break; }
        }
        if (clear && run < 0.f) run = t;
        if ((!clear || i == n) && run >= 0.f) {
            float t1 = clear ? t : (float)(i - 1) / n;
            if ((t1 - run) * L > 0.6f) keep.push_back(vec2(run, t1));
            run = -1.f;
        }
    }
}

void hedgePiece(FD& d, vec2 a, vec2 b, float h, float wd, vec3 tint, bool collide, int style, u32 seed);

// Hedge along a..b (collide: with colliders), left open where it would reach into a dead end's turning room
void hedge(FD& d, vec2 a, vec2 b, float h, float wd, vec3 tint, bool collide, int style) {
    FD_STAT(d.k.m, FS_HEDGE);
    float L = length(b - a);
    if (L < 0.5f) return;
    u32 seed = d.r.next();
    std::vector<vec2> keep;
    clearOfTurnarounds(a, b, wd * 0.5f, keep);
    for (size_t i = 0; i < keep.size(); i++) {
        vec2 pa = lerp(a, b, keep[i].x), pb = lerp(a, b, keep[i].y);
        if (length(pb - pa) >= 0.5f) hedgePiece(d, pa, pb, h, wd, tint, collide, style, i == 0 ? seed : hash32(seed + (u32)i));
    }
}

void hedgePiece(FD& d, vec2 a, vec2 b, float h, float wd, vec3 tint, bool collide, int style, u32 seed) {
    Sink& k = d.k;
    float L = length(b - a);
    hedgeMesh(*k.m, k.org, *d.map, a, b, h, wd, style, tint, seed);
    if (!collide || !k.col) return;
    vec2 t = (b - a) / L;
    int n = Max(1, (int)ceilf(L / 11.f));
    for (int i = 0; i < n; i++) {
        vec2 p0 = a + t * (L * i / n), p1 = a + t * (L * (i + 1) / n);
        vec2 mc = (p0 + p1) * 0.5f;
        float gz = Min(d.map->heightAt(p0.x, p0.y), d.map->heightAt(p1.x, p1.y));
        CollisionBox cb;
        cb.c = vec3(mc, gz + h * 0.5f);
        cb.ax = t;
        cb.he = vec3(length(p1 - p0) * 0.5f, wd * 0.5f, h * 0.5f);
        k.col->push_back(cb);
    }
}

// Clipped topiary on the ground at p (propmesh.cpp topiaryMesh), with a small collider
void topiary(FD& d, vec2 p, int kind, float size, vec3 tint) {
    FD_STAT(d.k.m, FS_HEDGE);
    Sink& k = d.k;
    float gz = d.map->heightAt(p.x, p.y);
    topiaryMesh(*k.m, k.org, vec3(p, gz), kind, size, tint, d.r.next());
    if (k.col) {
        CollisionBox cb;
        cb.c = vec3(p, gz + size * 0.4f);
        cb.ax = vec2(1, 0);
        cb.he = vec3(size * 0.25f, size * 0.25f, size * 0.4f);
        k.col->push_back(cb);
    }
}

// Bougainvillea: magenta flowering clumps climbing a wall from the ground, over a span along the wall
void bougainvillea(FD& d, const Wall& w, float s0, float s1, float zg, float zTop) {
    FD_STAT(d.k.m, FS_VINE);
    Sink& k = d.k;
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
        // a leafy clump flattened against the wall (flowers over most of it)
        float s1c = s + sz * d.r.range(0.7f, 1.2f), dep = d.r.range(0.25f, 0.5f);
        vec3 c = vec3(w.a + w.t * ((s - sz + s1c) * 0.5f) + w.n * (dep * 0.45f), zg + hgt);
        vec3 ax(w.t, 0.f), ay(perp(w.t), 0.f);
        leafBlobEx(*k.m, k.org, c, ax, ay, vec3((s1c - s + sz) * 0.5f, dep * 0.6f, sz * 0.62f), vec3(0.55f, 0.9f, 0.4f), d.r.next(), 0.06f, 8, 5,
                   green ? vec3(-1.f) : col);
    }
    // woody stem
    wbox(k, w, s0 + 0.2f, s0 + 0.28f, zg - 0.2f, zg + (zTop - zg) * 0.6f, 0.02f, 0.1f, pk(0.35f, 0.25f, 0.18f), MM(MAT_BARK), WF_FRONT | WF_START | WF_END);
}

// House and villa: door, stoop, porch roof / portico, window trims and shutters, AC condenser, garden
void houseDetail(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls) {
    FD_STAT(d.k.m, FS_HOUSE);
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
        if (w.len > 2.f && fabsf(w.facing) > 0.5f) windowTrims(d, w, ms, w.facing > 0.5f ? trimKind : 0, shutters && w.facing > 0.5f, shutterCol, bahama && w.facing > 0.5f);
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
            // porch roof (houses) or columned portico (villas); modern villas get a flat canopy
            if (villa && b.arch == AR_VILLA_MODERN) {
                float pw = dw * 0.5f + 1.4f;
                wbox(k, fw, sd - pw, sd + pw, zb + 2.9f, zb + 3.15f, 0.f, 2.2f, pk(0.96f), MM(MAT_PLASTER), WF_BOX | WF_BOTTOM);
            } else if (villa) {
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
    // hedge style by district: box-clipped formal hedges round the island villas, clipped privet in the suburbs, untrimmed
    // sea-grape and hibiscus screens in the Grove and out in the country
    int hstyle = HEDGE_CLIPPED;
    {
        float wr = d.r.f();
        switch (b.region) {
            case REG_KEY_CORAL:
            case REG_BAY_ISLAND: hstyle = villa ? (wr < 0.6f ? HEDGE_FORMAL : HEDGE_CLIPPED) : (wr < 0.2f ? HEDGE_WILD : HEDGE_CLIPPED); break;
            case REG_GROVE: hstyle = wr < 0.45f ? HEDGE_WILD : (villa && wr > 0.8f ? HEDGE_FORMAL : HEDGE_CLIPPED); break;
            case REG_FARMLAND:
            case REG_REDLAND:
            case REG_RIDGE:
            case REG_HARLOW:
            case REG_LAKE_TOWN:
            case REG_SAWGRASS:
            case REG_GULF_TOWN: hstyle = wr < 0.7f ? HEDGE_WILD : HEDGE_CLIPPED; break;
            default: hstyle = wr < 0.18f ? HEDGE_WILD : HEDGE_CLIPPED; break;
        }
        if (hstyle == HEDGE_FORMAL) hedgeTint = vec3(0.62f, 0.86f, 0.5f) * (0.92f + 0.14f * d.r.f());
        if (hstyle == HEDGE_WILD) hedgeTint = vec3(0.72f + 0.3f * d.r.f(), 0.88f + 0.2f * d.r.f(), 0.45f + 0.25f * d.r.f());
    }
    bool topiaries = hstyle != HEDGE_WILD && (villa ? d.r.chance(0.8f) : d.r.chance(0.25f));
    const vec3 topiaryTint = vec3(0.66f, 0.92f, 0.52f);
    bool gardenWall = villa && (b.region == REG_KEY_CORAL || b.region == REG_BAY_ISLAND || b.region == REG_GROVE) && d.r.chance(0.6f);
    // lot corners (front-left, front-right, back-left, back-right) in world space
    vec2 fl = lotCenterAlong - b.ax * hx + b.front * (b.lotHy - 0.4f), frt = lotCenterAlong + b.ax * hx + b.front * (b.lotHy - 0.4f);
    vec2 bl = lotCenterAlong - b.ax * hx - b.front * (b.lotHy - 0.4f), brt = lotCenterAlong + b.ax * hx - b.front * (b.lotHy - 0.4f);
    (void)ay;
    (void)frontMid;
    if (hedges) {
        float hh = villa ? 1.8f : d.r.range(1.1f, 1.6f);
        // (no hedge on the garage side: the garage wing and its driveway can reach the lot line)
        float hw = hstyle == HEDGE_WILD ? 1.2f : 0.9f;
        if (!(garage && gside < 0.f)) hedge(d, fl + b.front * -1.f, bl, hh, hw, hedgeTint, true, hstyle);
        if (!(garage && gside > 0.f)) hedge(d, frt + b.front * -1.f, brt, hh, hw, hedgeTint, true, hstyle);
        if (d.r.chance(0.6f)) hedge(d, bl, brt, hh, hw, hedgeTint, true, hstyle);
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
        // clipped balls or cones mark the openings in a front hedge (the hedge stops short of them)
        int tkind = villa ? (int)(d.r.next() % 3u) : 0;
        float tsize = tkind == 1 ? 1.5f : (tkind == 2 ? 1.6f : 0.9f);
        bool gateTopiary = topiaries && !gardenWall;
        auto lotFront = [&](float along) { return lotCenterAlong + b.ax * along + b.front * (b.lotHy - 0.4f); };
        for (int gi = 0; gi <= ng; gi++) {
            float end = gi < ng ? gaps[gi].a0 : hx;
            if (gateTopiary && !gardenWall) {
                float c0 = cur, c1 = end;
                if (gi > 0 && c1 - c0 > 1.6f) {
                    topiary(d, lotFront(c0 + 0.3f), tkind, tsize, topiaryTint);
                    cur = c0 + 0.7f;
                }
                if (gi < ng && c1 - cur > 1.6f) {
                    topiary(d, lotFront(c1 - 0.3f), tkind, tsize, topiaryTint);
                    end = c1 - 0.7f;
                }
            }
            if (end - cur > 0.8f) {
                vec2 p0 = lotFront(cur), p1 = lotFront(end);
                if (gardenWall) {
                    // (left open where a dead end's turning room reaches the lot line)
                    std::vector<vec2> keep;
                    clearOfTurnarounds(p0, p1, 0.25f, keep);
                    for (const vec2& kp : keep) {
                        vec2 q0 = lerp(p0, p1, kp.x), q1 = lerp(p0, p1, kp.y);
                        float L = length(q1 - q0);
                        vec2 mc = (q0 + q1) * 0.5f;
                        float gz = Min(d.map->heightAt(q0.x, q0.y), d.map->heightAt(q1.x, q1.y));
                        obox(k, vec3(mc, gz + 0.7f), vec3(b.ax, 0.f), vec3(b.front, 0.f), vec3(L * 0.5f, 0.15f, 0.9f), d.wallTone, d.wallMat, false);
                        obox(k, vec3(mc, gz + 1.65f), vec3(b.ax, 0.f), vec3(b.front, 0.f), vec3(L * 0.5f + 0.05f, 0.2f, 0.06f), d.trim, d.trimMat, true);
                        for (float a = 0.f; a <= L + 0.01f; a += Max(2.f, L / Max(1.f, floorf(L / 4.f)))) {
                            vec2 pp = q0 + b.ax * a;
                            obox(k, vec3(pp, gz + 0.85f), vec3(b.ax, 0.f), vec3(b.front, 0.f), vec3(0.25f, 0.25f, 1.05f), d.trim, d.trimMat, false);
                        }
                        if (k.col) {
                            CollisionBox cb;
                            cb.c = vec3(mc, gz + 0.8f);
                            cb.ax = b.ax;
                            cb.he = vec3(L * 0.5f, 0.2f, 0.9f);
                            k.col->push_back(cb);
                        }
                    }
                } else {
                    hedge(d, p0, p1, hstyle == HEDGE_WILD ? 0.95f : 0.75f, hstyle == HEDGE_WILD ? 0.95f : 0.7f, hedgeTint, false, hstyle);
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
                // rounded shrub (flowering ones carry their blossom over the top)
                vec3 c = vec3(fw.a + fw.t * s + fw.n * 0.5f, gz + 0.1f + hgt * 0.45f);
                leafBlobEx(*k.m, k.org, c, vec3(fw.t, 0.f), vec3(perp(fw.t), 0.f), vec3(0.46f, 0.42f, hgt * 0.55f), vec3(0.6f, 0.95f, 0.45f), d.r.next(),
                           0.05f, 9, 5, green ? vec3(-1.f) : fc);
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
    FD_STAT(d.k.m, FS_WARE);
    const Building& b = *d.b;
    Sink& k = d.k;
    float z0 = ms.vBase;
    int fi = 0;
    for (int i = 1; i < (int)walls.size(); i++)
        if (walls[i].facing > walls[fi].facing) fi = i;
    const Wall& fw = walls[fi];
    // loading doors (buildmesh.cpp) are centered at u = -hx + (k + 0.5) * 2hx / doors along ax; convert to wall s
    // (archetype warehouses: massing.cpp archLoadingDoors)
    float doorU[8];
    int doors = Max(1, (int)(b.hx / 7.f));
    if (b.arch != AR_NONE) doors = massing::archLoadingDoors(b, doorU, 8);
    else
        for (int kd = 0; kd < doors && kd < 8; kd++) doorU[kd] = -b.hx + (kd + 0.5f) * (2.f * b.hx / doors);
    doors = Min(doors, 8);
    bool alongAx = fabsf(dot(fw.t, b.ax)) > 0.7f && fw.facing > 0.9f;
    for (int kd = 0; kd < doors && alongAx; kd++) {
        float u = doorU[kd];
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


// ------------------------------------------------------------------------------------------------ yards, billboards, wall ads
// Service yard behind commercial and industrial buildings: a dumpster against the back wall, trash bags, AC condensers.
void serviceYard(FD& d) {
    const Building& b = *d.b;
    if (!d.k.props) return;
    bool commercial = b.style == BS_SHOPS || b.style == BS_MIDRISE || b.style == BS_STRIPMALL || b.style == BS_MOTEL || b.style == BS_GASSTATION ||
                      b.style == BS_DECO || b.style == BS_CONDO;
    bool industrial = b.style == BS_WAREHOUSE || b.style == BS_FACTORY;
    if (!commercial && !industrial) return;
    Rng r(b.seed ^ 0x5E4F1CE5u);
    float back = dot(b.c - b.lotC, b.front) - b.hy + b.lotHy;   // lot depth left behind the building
    if (back < 2.6f) return;
    vec2 placed[6];
    int np = 0;
    auto ok = [&](vec2 p, float rad) {
        for (int i = 0; i < np; i++)
            if (length(placed[i] - p) < rad + 0.9f) return false;
        if (gRoads && gRoads->nearRoad(p, rad + 0.3f)) return false;
        if (gBuildings && gBuildings->pointInBuilding(p, rad)) return false;
        if (gSites && gSites->blocksVegetation(p)) return false;
        return !d.map->isWater(p.x, p.y);
    };
    auto put = [&](PropType t, vec2 p, float yaw, int var, float scale) {
        PropInstance pi;
        pi.pos = vec3(p, d.map->heightAt(p.x, p.y));
        pi.yaw = yaw;
        pi.scale = scale;
        pi.type = (u8)t;
        pi.variant = (u8)var;
        pi.flags = 0;
        d.k.props->push_back(pi);
        if (np < 6) placed[np++] = p;
    };
    float wallYaw = atan2f(-b.front.x, b.front.y) + kPi;   // local -y (lid side) away from the back wall
    if (r.chance(industrial ? 0.85f : 0.7f)) {
        vec2 p = b.c - b.front * (b.hy + 1.2f) + b.ax * r.range(-b.hx * 0.6f, b.hx * 0.6f);
        if (ok(p, 1.1f)) {
            put(PROP_DUMPSTER, p, wallYaw, (int)(r.next() & 1u), 1.f);
            if (r.chance(0.55f)) {
                vec2 q = p + b.ax * (r.chance(0.5f) ? 1.75f : -1.75f) - b.front * 0.3f;
                if (ok(q, 0.5f)) put(PROP_TRASH_BAGS, q, r.f() * kTwoPi, 0, r.range(0.8f, 1.1f));
            }
        }
    }
    int nAC = commercial ? r.irange(0, 2) : r.irange(0, 1);
    for (int i = 0; i < nAC; i++) {
        vec2 q = b.c - b.front * (b.hy + 0.8f) + b.ax * r.range(-b.hx * 0.85f, b.hx * 0.85f);
        if (!ok(q, 0.6f)) continue;
        put(PROP_AC_UNIT, q, atan2f(b.ax.y, b.ax.x), 0, 1.f);
        if (d.k.col) {
            CollisionBox cb;
            cb.c = vec3(q, d.map->heightAt(q.x, q.y) + 0.45f);
            cb.ax = b.ax;
            cb.he = vec3(0.5f, 0.5f, 0.45f);
            d.k.col->push_back(cb);
        }
    }
}

// Rooftop billboard over a low shop on a main road, facing the street (lit like the highway boards at night)
void rooftopBillboard(FD& d, const FacadeMass& ms, const Wall& fw) {
    const Building& b = *d.b;
    if (b.style != BS_SHOPS || b.floors > 2 || b.roof != ROOF_FLAT || !gRoads) return;
    bool reg = d.reg == REG_CALLE_LUNA || d.reg == REG_FLATS || d.reg == REG_NORTH_CITY || d.reg == REG_MIDTOWN || d.reg == REG_SUBURBS ||
               d.reg == REG_FORT_CASTELL || d.reg == REG_LAKE_TOWN || d.reg == REG_HARLOW || d.reg == REG_KEY_TOWN;
    Rng r(b.seed ^ 0xB111B0A2u);
    if (!reg || !r.chance(0.16f) || fw.len < 7.f) return;
    float ds, dd, side;
    int ne = gRoads->nearestEdge(b.c + b.front * (b.hy + 7.f), 14.f, &ds, &dd, &side);
    if (ne < 0 || gRoads->edges[ne].cls > RC_AVENUE) return;
    Sink& k = d.k;
    float W = Min(fw.len * 0.85f, 12.f), H = W * 0.34f;
    vec2 n2 = fw.n, t2 = fw.t;
    vec3 n(n2, 0), rt(t2, 0), up(0, 0, 1);
    vec2 base = fw.a + t2 * (fw.len * 0.5f) - n2 * Min(3.f, b.hy * 0.5f);
    float zr = ms.z1, zb = zr + 3.2f;
    vec3 o(base, zb + H * 0.5f);
    const u32 steel = pk(0.3f, 0.31f, 0.33f), metal = MM(MAT_METAL_PAINTED);
    // I-beam posts with a knee brace, back frame, catwalk
    for (int s = -1; s <= 1; s += 2) {
        vec2 pp = base + t2 * (s * W * 0.3f) - n2 * 0.25f;
        obox(k, vec3(pp, (zr + zb + H) * 0.5f), vec3(t2, 0), vec3(n2, 0), vec3(0.12f, 0.12f, (zb + H - zr) * 0.5f), steel, metal);
        obox(k, vec3(pp - n2 * 0.9f, zr + 1.2f), vec3(t2, 0), normalize(vec3(n2, 1.2f)), vec3(0.06f, 0.06f, 1.35f), steel, metal);
    }
    obox(k, o - n * 0.14f, rt, n, vec3(W * 0.5f + 0.12f, 0.1f, H * 0.5f + 0.12f), steel, metal);
    obox(k, vec3(base + n2 * 0.45f, zb - 0.25f), vec3(t2, 0), vec3(n2, 0), vec3(W * 0.5f, 0.45f, 0.04f), steel, metal);
    sitegeo::G g;
    g.m = k.m;
    g.d = k.m;
    g.org = k.org;
    g.detail = true;
    landmark_mesh::adFace(g, landmark_mesh::kAds[r.next() % 24u], o + n * 0.02f, rt, up, n, W, H, true);
    for (int i = -1; i <= 1; i++) {
        vec3 lp = vec3(base + t2 * (i * W * 0.33f) + n2 * 1.1f, zb - 0.15f);
        obox(k, lp, rt, n, vec3(0.16f, 0.12f, 0.08f), steel, metal);
    }
}

// Painted wall ad on a blank side wall, above whatever the neighbour hides
void wallAd(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls) {
    const Building& b = *d.b;
    if (!(b.style == BS_MIDRISE || b.style == BS_SHOPS || b.style == BS_WAREHOUSE || b.style == BS_FACTORY) || !gBuildings) return;
    float p = d.reg == REG_CALLE_LUNA || d.reg == REG_MIDTOWN ? 0.3f : (d.reg == REG_FLATS || d.reg == REG_NORTH_CITY || d.reg == REG_FORT_CASTELL ? 0.2f : 0.08f);
    Rng r(b.seed ^ 0xAD5A11u);
    if (!r.chance(p)) return;
    int start = (int)(r.next() % (u32)Max(1, (int)walls.size()));
    for (int j = 0; j < (int)walls.size(); j++) {
        const Wall& w = walls[(start + j) % walls.size()];
        if (fabsf(w.facing) > 0.3f || w.len < 7.f) continue;
        float cover = ms.vBase;
        for (int q = 1; q <= 3; q++) {
            float top;
            vec2 qp = w.a + w.t * (w.len * q * 0.25f) + w.n * 1.6f;
            if (gBuildings->pointInBuilding(qp, 0.8f, &top)) cover = Max(cover, top + 1.2f);
        }
        float z0 = Max(cover + 0.5f, ms.vBase + 2.8f), z1 = ms.z1 - 0.5f;
        if (z1 - z0 < 3.5f) continue;
        float H = Min(z1 - z0, 9.f), W = Min(w.len * 0.8f, H * 1.5f);
        if (W < 4.f) continue;
        vec3 c = vec3(w.a + w.t * (w.len * 0.5f) + w.n * 0.035f, z0 + H * 0.5f);
        sitegeo::G g;
        g.m = d.k.m;
        g.d = d.k.m;
        g.org = d.k.org;
        g.detail = true;
        landmark_mesh::adPoster(g, landmark_mesh::kAds[r.next() % 24u], c, vec3(w.t, 0), vec3(0, 0, 1), vec3(w.n, 0), W, H, 1.f, MM(MAT_PAINT_WHITE));
        return;
    }
}

// MiMo garden apartments: open walkways along the court-side walls on every upper floor - a slab on thin steel columns,
// a solid spandrel band (the breeze-block balustrade) and a steel top rail
void mimoWalkways(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls, bool interiorOnly = false) {
    const Building& b = *d.b;
    const FacadeGPU& f = *d.f;
    FloorRow rows[32];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 32);
    if (nr < 2) return;
    vec2 ay = perp(b.ax);
    u32 rail = (b.archFlags & ABF_ACCENT) ? d.frame : pk(0.95f);
    u32 band = (b.archFlags & ABF_ACCENT) ? pk(rgbOf(d.frame) * 0.9f + vec3(0.08f)) : d.wallTone;
    const float dep = 1.45f;
    float zTopWalk = -1.f;
    for (int ri = 0; ri < nr; ri++)
        if (rows[ri].idx > 0) zTopWalk = ms.vBase + rows[ri].v0;
    for (const Wall& w : walls) {
        if (w.len < 5.f || !d.room()) continue;
        if (interiorOnly) {
            vec2 mid = (w.a + w.b) * 0.5f - b.c;
            if (fabsf(dot(mid, b.ax)) > b.hx - 0.4f || fabsf(dot(mid, ay)) > b.hy - 0.4f) continue;
        }
        for (int ri = 0; ri < nr && d.room(); ri++) {
            if (rows[ri].idx == 0) continue;
            float z = ms.vBase + rows[ri].v0;
            wbox(d.k, w, 0.25f, w.len - 0.25f, z - 0.2f, z, 0.f, dep, d.trim, d.trimMat, WF_BOX | WF_BOTTOM);
            wbox(d.k, w, 0.25f, w.len - 0.25f, z, z + 0.5f, dep - 0.08f, dep, band, d.wallMat, WF_FRONT | WF_TOP | WF_START | WF_END);
            wbox(d.k, w, 0.25f, w.len - 0.25f, z + 0.92f, z + 0.98f, dep - 0.06f, dep, rail, MM(MAT_METAL_PAINTED), WF_FRONT | WF_TOP | WF_BOTTOM);
            for (float s = 0.3f; s < w.len - 0.25f; s += 1.5f) wbox(d.k, w, s, s + 0.04f, z + 0.5f, z + 0.92f, dep - 0.05f, dep - 0.01f, rail, MM(MAT_METAL_PAINTED), WF_FRONT);
        }
        // slim columns at the walkway edge, ground to the top walkway
        if (zTopWalk > 0.f && fabsf(ms.z0 - ms.vBase) < 0.6f) {
            int nc = Max(2, (int)(w.len / 4.8f) + 1);
            for (int k = 0; k < nc; k++) {
                float s = Lerp(0.5f, w.len - 0.5f, (float)k / (nc - 1));
                wbox(d.k, w, s - 0.07f, s + 0.07f, ms.vBase - 0.2f, zTopWalk - 0.2f, dep - 0.35f, dep - 0.21f, rail, MM(MAT_METAL_PAINTED), WF_POST | WF_BACK);
                wcollide(d.k, w, s - 0.08f, s + 0.08f, ms.vBase, zTopWalk, dep - 0.36f, dep - 0.2f);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ murals
// A painted polygon on the wall plane: pts in (s along the wall, z up), clipped to the mural's rectangle
void wallPaint(FD& d, const Wall& w, std::vector<vec2> pts, vec2 mn, vec2 mx, float o, vec3 col) {
    pts = sitegeo::clipConvex(pts, mn, mx);
    if (pts.size() < 3) return;
    // convex: a fan in the wall plane, wound to face out of the wall (MeshData::polygon triangulates in plan only)
    MeshData& m = *d.k.m;
    vec3 n(w.n, 0.f), t(w.t, 0.f);
    u32 base = (u32)m.verts.size();
    u32 c = pk(col), mat = MM(MAT_PAINT_WHITE);
    for (const vec2& p : pts) m.addVertex(WP(d.k, w, p.x, p.y, o), n, t, p, c, mat);
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        u32 a = base, b = base + (u32)i, e = base + (u32)i + 1;
        vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[e].pos - m.verts[a].pos);
        if (dot(fn, n) < 0.f) m.tri(a, e, b);
        else m.tri(a, b, e);
    }
}
std::vector<vec2> discPts(vec2 c, float rx, float rz, int seg, float a0 = 0.f, float a1 = kTwoPi) {
    std::vector<vec2> p;
    bool full = a1 - a0 >= kTwoPi - 1e-3f;
    if (!full) p.push_back(c);
    int n = full ? seg : seg + 1;
    for (int i = 0; i < n; i++) {
        float a = a0 + (a1 - a0) * i / (full ? seg : seg);
        p.push_back(c + vec2(cosf(a) * rx, sinf(a) * rz));
    }
    return p;
}

// Mural on an exposed wall from s0..s1, z0..z1: a sunset with palms, waves, a geometric field or a big word over
// discs, in one of four palettes (Canvas District, Calle Luna, the Flats)
void mural(FD& d, const Wall& w, float s0, float s1, float z0, float z1, Rng& r) {
    float W = s1 - s0, H = z1 - z0;
    if (W < 3.f || H < 2.4f) return;
    static const vec3 pal[4][5] = {
        {vec3(0.98f, 0.5f, 0.22f), vec3(0.95f, 0.28f, 0.45f), vec3(0.45f, 0.22f, 0.62f), vec3(1.f, 0.82f, 0.28f), vec3(0.14f, 0.1f, 0.22f)},
        {vec3(0.1f, 0.45f, 0.75f), vec3(0.15f, 0.75f, 0.8f), vec3(0.95f, 0.95f, 0.9f), vec3(0.06f, 0.22f, 0.45f), vec3(1.f, 0.72f, 0.3f)},
        {vec3(0.95f, 0.22f, 0.4f), vec3(0.2f, 0.7f, 0.95f), vec3(1.f, 0.85f, 0.12f), vec3(0.3f, 0.85f, 0.45f), vec3(0.08f, 0.08f, 0.1f)},
        {vec3(0.55f, 0.3f, 0.85f), vec3(0.95f, 0.45f, 0.75f), vec3(0.3f, 0.9f, 0.8f), vec3(0.98f, 0.95f, 0.9f), vec3(0.2f, 0.15f, 0.35f)},
    };
    const vec3* P = pal[r.next() % 4];
    vec2 mn(s0, z0), mx(s1, z1);
    float o = 0.03f;
    auto rect = [&](float a0, float a1, float b0, float b1, vec3 c, float oo) { wallPaint(d, w, {vec2(a0, b0), vec2(a1, b0), vec2(a1, b1), vec2(a0, b1)}, mn, mx, oo, c); };
    int kind = r.irange(0, 3);
    switch (kind) {
        case 0: {
            // sunset: graded bands, a half sun on the horizon, palm silhouettes
            int nb = 5;
            for (int i = 0; i < nb; i++) {
                float t = (float)i / (nb - 1);
                vec3 c = lerp(P[0], P[2], t);
                rect(s0, s1, z0 + H * (0.35f + 0.65f * i / nb), z0 + H * (0.35f + 0.65f * (i + 1) / nb), c, o);
            }
            rect(s0, s1, z0, z0 + H * 0.35f, P[4], o);
            float sr = Min(W * 0.22f, H * 0.3f);
            wallPaint(d, w, discPts(vec2(s0 + W * r.range(0.35f, 0.65f), z0 + H * 0.35f), sr, sr, 16, 0.f, kPi), mn, mx, o + 0.005f, P[3]);
            int np = r.irange(1, 3);
            for (int k = 0; k < np; k++) {
                float sx = s0 + W * r.range(0.1f, 0.9f), th = H * r.range(0.45f, 0.75f);
                wallPaint(d, w, {vec2(sx - 0.12f, z0), vec2(sx + 0.12f, z0), vec2(sx + 0.35f, z0 + th), vec2(sx + 0.15f, z0 + th)}, mn, mx, o + 0.01f, P[4]);
                for (int f = 0; f < 5; f++) {
                    float a = kPi * (0.15f + 0.7f * f / 4.f);
                    vec2 top(sx + 0.25f, z0 + th), dir(cosf(a), sinf(a) * 0.55f - 0.25f);
                    float L = H * 0.18f;
                    vec2 nn = vec2(-dir.y, dir.x) * 0.18f;
                    wallPaint(d, w, {top, top + dir * (L * 0.5f) + nn, top + dir * L, top + dir * (L * 0.5f) - nn}, mn, mx, o + 0.012f, P[4]);
                }
            }
            break;
        }
        case 1: {
            // waves: stacked swells, a foam line on each
            rect(s0, s1, z0, z1, P[2] * 0.95f, o);
            int nw = r.irange(3, 5);
            for (int i = 0; i < nw; i++) {
                float zb = z0 + H * (0.75f - 0.18f * i), amp = H * 0.06f, ph = r.f() * kTwoPi;
                vec3 c = lerp(P[1], P[3], (float)i / Max(1, nw - 1));
                const int seg = 12;
                for (int k = 0; k < seg; k++) {
                    float a0 = s0 + W * k / seg, a1 = s0 + W * (k + 1) / seg;
                    float h0 = zb + amp * sinf(ph + 6.f * k / seg * 2.f), h1 = zb + amp * sinf(ph + 6.f * (k + 1) / seg * 2.f);
                    wallPaint(d, w, {vec2(a0, z0), vec2(a1, z0), vec2(a1, h1), vec2(a0, h0)}, mn, mx, o + 0.003f * (i + 1), c);
                    wallPaint(d, w, {vec2(a0, h0 - 0.08f), vec2(a1, h1 - 0.08f), vec2(a1, h1), vec2(a0, h0)}, mn, mx, o + 0.003f * (i + 1) + 0.001f, P[2]);
                }
            }
            break;
        }
        case 2: {
            // geometric: diagonal stripes over a field, a few discs and triangles
            rect(s0, s1, z0, z1, P[4], o);
            float sw = r.range(0.8f, 1.6f), slope = r.chance(0.5f) ? 1.f : -1.f;
            int ci = 0;
            for (float a = s0 - H; a < s1 + H; a += sw * 2.f) {
                vec3 c = P[ci++ % 4];
                wallPaint(d, w, {vec2(a, z0), vec2(a + sw, z0), vec2(a + sw + slope * H, z1), vec2(a + slope * H, z1)}, mn, mx, o + 0.004f, c);
            }
            int nd = r.irange(2, 4);
            for (int k = 0; k < nd; k++) {
                float rr = Min(W, H) * r.range(0.12f, 0.24f);
                vec2 c(s0 + r.range(rr, W - rr), z0 + r.range(rr, H - rr));
                wallPaint(d, w, discPts(c, rr, rr, 14), mn, mx, o + 0.008f + 0.001f * k, P[(k + 2) % 5]);
            }
            break;
        }
        default: {
            // a big word over coloured discs
            rect(s0, s1, z0, z1, P[r.next() % 4] * 0.9f, o);
            int nd = r.irange(3, 6);
            for (int k = 0; k < nd; k++) {
                float rr = Min(W, H) * r.range(0.15f, 0.35f);
                vec2 c(s0 + r.range(0.f, W), z0 + r.range(0.f, H));
                wallPaint(d, w, discPts(c, rr, rr, 14), mn, mx, o + 0.003f + 0.001f * k, P[(k + 1) % 4]);
            }
            static const char* kWords[] = {"LUNA", "AMOR", "SOL", "VIDA", "MAMBO", "SALSA", "PALMERA", "TIDE", "CANVAS", "RITMO", "ISLA", "FUEGO"};
            const char* word = kWords[r.next() % ARRAY_COUNT(kWords)];
            float th = Min(H * 0.42f, 3.f);
            float tw = sitegeo::textAdvance(word, th, 0.2f);
            if (tw > W * 0.9f) th *= W * 0.9f / tw, tw = W * 0.9f;
            vec3 right(w.t, 0.f);
            vec3 org3 = vec3(w.a + w.t * (s0 + (W - tw) * 0.5f) + w.n * (o + 0.02f), z0 + (H - th) * 0.5f);
            sitegeo::G g;
            g.m = d.k.m;
            g.org = d.k.org;
            sitegeo::strokeText(g, *d.k.m, word, org3 - vec3(0, 0, th * 0.06f) + right * (th * 0.05f), right, vec3(0, 0, 1), th, th * 0.2f, pk(P[4]), MM(MAT_PAINT_WHITE), 0.f, 0.2f);
            sitegeo::strokeText(g, *d.k.m, word, org3 + vec3(w.n * 0.005f, 0.f), right, vec3(0, 0, 1), th, th * 0.13f, pk(vec3(0.98f, 0.97f, 0.92f)), MM(MAT_PAINT_WHITE),
                                0.f, 0.2f);
            break;
        }
    }
}

// The wall a mural goes on: the cross-street side of a corner lot, else the side wall that rises furthest above its
// neighbour. Returns false when nothing is exposed enough.
bool muralWall(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls, int& wi, float& z0, float& z1) {
    const Building& b = *d.b;
    wi = -1;
    float bestArea = 0.f;
    bool cornerLot = (b.archFlags & ABF_CORNER) != 0;
    float cs = (b.archFlags & ABF_CORNER_LEFT) ? -1.f : 1.f;
    for (int i = 0; i < (int)walls.size(); i++) {
        const Wall& w = walls[i];
        if (fabsf(w.facing) > 0.3f || w.len < 5.f) continue;
        float side = dot(w.n, b.ax);
        float cover = ms.vBase;
        if (!(cornerLot && side * cs > 0.7f) && gBuildings)
            for (int q = 1; q <= 3; q++) {
                float top;
                vec2 qp = w.a + w.t * (w.len * q * 0.25f) + w.n * 1.6f;
                if (gBuildings->pointInBuilding(qp, 0.8f, &top)) cover = Max(cover, top + 0.6f);
            }
        float a0 = Max(cover + 0.3f, ms.vBase + 0.6f), a1 = ms.z1 - 0.4f;
        float area = (a1 - a0) * w.len * ((cornerLot && side * cs > 0.7f) ? 3.f : 1.f);
        if (a1 - a0 > 2.4f && area > bestArea) bestArea = area, wi = i, z0 = a0, z1 = a1;
    }
    return wi >= 0;
}

// ------------------------------------------------------------------------------------------------ arches
// Fan from a corner of the wall plane through arc points (s, z): the spandrel over an arch (star-shaped from the corner)
void wallFan(FD& d, const Wall& w, vec2 corner, const vec2* arc, int n, float o, u32 col, u32 mat) {
    MeshData& m = *d.k.m;
    vec3 nn(w.n, 0.f), t(w.t, 0.f);
    u32 base = (u32)m.verts.size();
    m.addVertex(WP(d.k, w, corner.x, corner.y, o), nn, t, corner, col, mat);
    for (int i = 0; i < n; i++) m.addVertex(WP(d.k, w, arc[i].x, arc[i].y, o), nn, t, arc[i], col, mat);
    for (int i = 0; i + 1 < n; i++) {
        u32 a = base, b = base + 1 + i, c = base + 2 + i;
        vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[c].pos - m.verts[a].pos);
        if (dot(fn, nn) < 0.f) m.tri(a, c, b);
        else m.tri(a, b, c);
    }
}
// A round arch between s0 and s1 springing at zs: spandrels up to zTop (wall colour), an archivolt band on the curve
void roundArch(FD& d, const Wall& w, float s0, float s1, float zs, float zTop, float o, u32 wallCol, u32 wallMat, bool spandrels) {
    float R = (s1 - s0) * 0.5f, sc = (s0 + s1) * 0.5f;
    const int seg = 8;
    vec2 arc[seg + 1];
    for (int k = 0; k <= seg; k++) {
        float a = kPi * k / seg;   // 0: right springing, pi: left springing
        arc[k] = vec2(sc + cosf(a) * R, zs + sinf(a) * R);
    }
    if (spandrels && zTop > zs + R + 0.02f) {
        // right half from the top-right corner, left half from the top-left corner
        wallFan(d, w, vec2(s1, zTop), arc, seg / 2 + 1, o, wallCol, wallMat);
        wallFan(d, w, vec2(s0, zTop), arc + seg / 2, seg / 2 + 1, o, wallCol, wallMat);
        // (the crown to the top: covered by both fans' top edges meeting at sc)
    }
    // archivolt: a band along the curve, standing a little proud
    for (int k = 0; k < seg; k++) {
        vec2 a0 = arc[k], a1 = arc[k + 1];
        vec2 r0 = normalize(a0 - vec2(sc, zs)), r1 = normalize(a1 - vec2(sc, zs));
        vec2 b0 = a0 + r0 * 0.16f, b1 = a1 + r1 * 0.16f;
        d.k.m->quadFacing(WP(d.k, w, a0.x, a0.y, o + 0.05f), WP(d.k, w, a1.x, a1.y, o + 0.05f), WP(d.k, w, b1.x, b1.y, o + 0.05f), WP(d.k, w, b0.x, b0.y, o + 0.05f),
                          vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), d.trim, d.trimMat, vec3(w.n, 0.f));
        // soffit of the arch (the curve's underside) on the arcade face
        if (spandrels)
            d.k.m->quadFacing(WP(d.k, w, a0.x, a0.y, o + 0.06f), WP(d.k, w, a1.x, a1.y, o + 0.06f), WP(d.k, w, a1.x, a1.y, o - 0.55f), WP(d.k, w, a0.x, a0.y, o - 0.55f),
                              vec2(0), vec2(1, 0), vec2(1), vec2(0, 1), wallCol, wallMat, -(vec3(w.t * (r0.x + r1.x), r0.y + r1.y)));
    }
}

// Arcade (AR_SHOP_ARCADE): round arches between the piers on the street line under the upper floors
void arcadeArches(FD& d, const Wall& fw, const FacadeMass& upper) {
    const Building& b = *d.b;
    const FacadeGPU& f = *d.f;
    float zg = upper.z0;
    int bays = Max(1, (int)roundf(2.f * b.hx / Max(f.bayW, 0.5f)));
    float bw = fw.len / bays;
    for (int k = 0; k < bays && d.room(); k++) {
        float s0 = k * bw + 0.3f, s1 = (k + 1) * bw - 0.3f;
        if (k == 0) s0 = 0.6f;
        if (k == bays - 1) s1 = fw.len - 0.6f;
        if (s1 - s0 < 1.2f) continue;
        float R = (s1 - s0) * 0.5f;
        float zs = Max(upper.vBase + 2.3f, zg - 0.25f - R);
        roundArch(d, fw, s0, s1, zs, zg, 0.f, d.wallTone, d.wallMat, true);
        // impost blocks on the piers
        wbox(d.k, fw, s0 - 0.36f, s0 + 0.02f, zs - 0.18f, zs, 0.f, 0.08f, d.trim, d.trimMat, WF_BOX);
        wbox(d.k, fw, s1 - 0.02f, s1 + 0.36f, zs - 0.18f, zs, 0.f, 0.08f, d.trim, d.trimMat, WF_BOX);
    }
}

// Mediterranean revival: round-arched heads over the upper windows of the street front (a dark fanlight in the arch,
// a stucco archivolt around it)
void archedHeads(FD& d, const Wall& w, const FacadeMass& ms) {
    const FacadeGPU& f = *d.f;
    if ((int)f.style != 0 && (int)f.style != 5) return;
    FloorRow rows[16];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 16);
    vec3 gl = rgbOf(f.glassColor) * 0.12f + vec3(0.02f);
    for (int ri = 0; ri < nr && d.room(); ri++) {
        WinSpec ws = winSpec(f, rows[ri].idx == 0);
        if (ws.store) continue;
        float z1 = ms.vBase + rows[ri].v0 + ws.sill + ws.h;
        for (int i = 0; i < w.bays; i++) {
            if (rows[ri].idx == 0 && i == d.doorBay && w.facing > 0.9f) continue;
            float s0 = (i + ws.x0) * w.bw, s1 = s0 + ws.w * w.bw;
            float R = (s1 - s0) * 0.5f;
            if (z1 + R > ms.vBase + rows[ri].v0 + (rows[ri].idx == 0 ? f.groundH : f.floorH) - 0.05f) continue;   // no room under the floor above
            // fanlight (half disc of glass) and the archivolt
            std::vector<vec2> pts = discPts(vec2((s0 + s1) * 0.5f, z1), R, R, 8, 0.f, kPi);
            wallPaint(d, w, pts, vec2(s0 - 1.f, z1 - 1.f), vec2(s1 + 1.f, z1 + R + 1.f), 0.015f, gl);
            roundArch(d, w, s0, s1, z1, z1 + R, 0.f, d.wallTone, d.wallMat, false);
        }
    }
}

// Streamline moderne (AR_DECO_STREAMLINE): continuous eyebrow ledges wrapping the street walls and the rounded corners
// at every floor, racing stripes under the roof, and a vertical fin with the hotel's name in neon at the corner
void streamlineFront(FD& d, const FacadeMass& ms, const std::vector<Wall>& walls) {
    const Building& b = *d.b;
    const FacadeGPU& f = *d.f;
    FloorRow rows[16];
    int nr = massFloors(f, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 16);
    u32 stripe = pk(rgbOf(d.frame) * 0.95f);
    for (const Wall& w : walls) {
        if (w.facing < -0.3f || w.len < 0.3f || !d.room()) continue;
        for (int ri = 0; ri < nr; ri++) {
            float fh = rows[ri].idx == 0 ? f.groundH : f.floorH;
            float z = ms.vBase + rows[ri].v0 + fh - 0.25f;
            wbox(d.k, w, -0.03f, w.len + 0.03f, z - 0.07f, z, 0.f, 0.55f, d.trim, d.trimMat, WF_FRONT | WF_TOP | WF_BOTTOM);
        }
        for (int j = 0; j < 3; j++) {
            float z = ms.z1 - 0.35f - j * 0.24f;
            wbox(d.k, w, -0.02f, w.len + 0.02f, z - 0.06f, z, 0.f, 0.04f, stripe, MM(MAT_PLASTER), WF_FRONT);
        }
    }
    // corner fin: on the rounded corner nearest the cross street (or the middle of the front on mid-block lots)
    int fi = -1;
    float best = -1e9f;
    bool cornerLot = (b.archFlags & ABF_CORNER) != 0;
    float cs = (b.archFlags & ABF_CORNER_LEFT) ? -1.f : 1.f;
    for (int i = 0; i < (int)walls.size(); i++) {
        const Wall& w = walls[i];
        if (w.facing < 0.25f || w.facing > 0.97f) continue;   // the arc segments of the rounded corners
        float sc = dot((w.a + w.b) * 0.5f - b.c, b.ax) * (cornerLot ? cs : 1.f) + w.facing * 0.1f;
        if (fabsf(w.facing - 0.707f) < 0.2f) sc += 50.f;      // the segment at 45 degrees
        if (sc > best) best = sc, fi = i;
    }
    float zTop = ms.z1 + 2.6f;
    const char* name = kHotelWords[d.r.next() % ARRAY_COUNT(kHotelWords)];
    vec3 nc = neonHue(d.r);
    if (fi >= 0) {
        const Wall& w = walls[fi];
        // a fin standing out of the curve: a blade sign perpendicular to the wall at its middle
        bladeSign(d, w, w.len * 0.5f, ms.vBase + f.groundH + 0.2f, zTop, 0.05f, 1.35f, name, nc, pk(0.97f, 0.96f, 0.93f), d.r.chance(0.3f) ? 2u : 6u);
    } else {
        int fw = 0;
        for (int i = 1; i < (int)walls.size(); i++)
            if (walls[i].facing > walls[fw].facing) fw = i;
        bladeSign(d, walls[fw], walls[fw].len * 0.5f, ms.vBase + f.groundH + 0.2f, zTop, 0.05f, 1.35f, name, nc, pk(0.97f, 0.96f, 0.93f), 6u);
    }
}

}  // namespace facade_detail

// ---------------------------------------------------------------------------------------------------------------
void buildFacadeDetail(const Building& b, const FacadeGPU& fac0, const WorldMap& map, vec3 org, MeshData& m, std::vector<CollisionBox>* col,
                       std::vector<PropInstance>* props, std::vector<LightInstance>* lights, const std::vector<FacadeMass>& masses) {
    using namespace facade_detail;
    FD d;
    d.k.m = &m;
    d.k.org = org;
    d.k.col = col;
    d.k.props = props;
    d.k.lights = lights;
    d.b = &b;
    d.doorBay = interiorDoorBay(b);
    d.f = &fac0;
    d.map = &map;
    d.r = Rng(b.seed ^ 0xFAC4DE71u);
    d.v0 = m.verts.size();
    d.reg = b.region;
    d.old = b.region == REG_CALLE_LUNA || b.region == REG_FLATS || b.region == REG_MIDTOWN || b.region == REG_NORTH_CITY || b.region == REG_FORT_CASTELL;
    if (b.arch != AR_NONE) d.old = (b.archFlags & ABF_OLD) != 0;   // archetypes (blockstyle.cpp) carry their own age
    d.graffiti = b.region == REG_CALLE_LUNA || b.region == REG_FLATS || b.region == REG_FORT_CASTELL;
    // colors: facade tone reproduced on the wall material, trims, accents
    MaterialId wm = MAT_STUCCO;
    vec3 frameRGB;
    auto scheme = [&](const FacadeGPU& f) {
        d.wallRGB = rgbOf(f.wallColor);
        wm = (MaterialId)Clamp((int)f.wallLayer, 0, (int)MAT_COUNT - 1);
        if (wm == MAT_FACADE) wm = MAT_STUCCO;
        d.wallMat = MM(wm);
        d.wallTone = pk(d.wallRGB * 1.55f);
        frameRGB = rgbOf(f.frameColor);
        d.frame = pk(frameRGB);
        d.dark = pk(d.wallRGB * 0.55f + vec3(0.05f));
    };
    scheme(fac0);
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
    switch (b.arch) {
        case AR_SHOP_1920: case AR_MID_WALKUP: corniceKind = d.r.chance(0.7f) ? CO_CLASSIC : CO_STEPPED; break;
        case AR_SHOP_TAXPAYER: case AR_SHOP_BODEGA: corniceKind = d.r.chance(0.6f) ? CO_BAND : CO_STEPPED; break;
        case AR_SHOP_MED: case AR_MID_MED: case AR_SHOP_ARCADE: corniceKind = CO_BAND; break;
        case AR_MID_MODERN: case AR_MID_OFFICE60: case AR_CONDO_GLASS: case AR_CONDO_PODIUM: case AR_CONDO_SLAB: corniceKind = CO_SLAB; break;
        case AR_MID_LOFT: corniceKind = d.r.chance(0.5f) ? CO_BAND : CO_CLASSIC; break;
        default: break;
    }
    // roofs that end in an eave, a pitched roof or a tile pent carry no cornice of their own
    bool noCornice = b.arch != AR_NONE && (b.roofForm == RFM_EAVE || b.roofForm == RFM_TILE_HIP || b.roofForm == RFM_METAL_GABLE || b.roofForm == RFM_SAWTOOTH ||
                                           b.roofForm == RFM_BUTTERFLY);
    if (b.style == BS_DECO) corniceKind = CO_STEPPED;
    if (b.style == BS_CONDO || b.style == BS_GARAGE || b.style == BS_STRIPMALL || b.style == BS_GASSTATION) corniceKind = CO_SLAB;
    if (industrial) corniceKind = CO_BAND;
    int trimKind = d.r.irange(0, 3);
    bool sillCourses = d.r.chance(0.35f);
    int pilasterEvery = d.r.chance(0.4f) ? d.r.irange(2, 3) : 0;
    bool plinth = d.r.chance(0.7f);

    bool muralDone = false;
    for (const FacadeMass& ms : masses) {
        // mixed cladding: a mass with its own facade record (podium, base band) uses that grid and colour scheme
        bool own = ms.facade != 0xffffffffu && ms.facade != b.facade && gBuildings && ms.facade < gBuildings->facades.size();
        const FacadeGPU& fac = own ? gBuildings->facades[ms.facade] : fac0;
        d.f = &fac;
        scheme(fac);
        if (!own) d.accent = b.style == BS_DECO ? pk(frameRGB) : pk(lerp(d.wallRGB, frameRGB, 0.6f));
        walls.clear();
        for (int i = 0; i < (int)ms.fp.size(); i++) walls.push_back(makeWall(ms.fp, i, fac.bayW, b.front));
        bool bottom = fabsf(ms.z0 - ms.vBase) < 0.6f;
        if (house && ms.kind == FM_HOUSE) {
            houseDetail(d, ms, walls);
            continue;
        }
        if (ms.kind == FM_WING) {
            // secondary volumes (house wings, corner towers, courtyard walls): sills and heads, a cornice band on flat tops
            int tk = house ? 1 : trimKind;
            for (const Wall& w : walls) {
                if (w.len < 1.5f || !d.room()) continue;
                windowTrims(d, w, ms, tk, false, 0, 0);
                if (!house && ms.parapet) coping(d, w, ms.z1);
            }
            if (b.arch == AR_MID_MIMO && !house && d.room()) mimoWalkways(d, ms, walls);
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
            if (bottom && (fac.flags & 1u)) {
                // towers without a podium: lobby storefront and a band over the ground floor on the street side
                int fi = 0;
                for (int i = 1; i < (int)walls.size(); i++)
                    if (walls[i].facing > walls[fi].facing) fi = i;
                if (walls[fi].len > 3.f) {
                    storefront(d, walls[fi], ms.vBase, walls[fi].bays / 2);
                    stringCourse(d, walls[fi], ms.vBase + fac.groundH, 0.3f, 0.2f, d.trim, d.trimMat);
                }
            }
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
        // arcade arches hang under the upper floors' street wall; Mediterranean fronts get arched window heads
        if (b.arch == AR_SHOP_ARCADE && ms.kind == FM_TIER && fabsf(ms.z0 - (ms.vBase + fac.groundH)) < 0.3f && walls[fi].facing > 0.9f) arcadeArches(d, walls[fi], ms);
        bool medFront = b.arch == AR_SHOP_MED || b.arch == AR_MID_MED || b.arch == AR_DECO_MED || b.arch == AR_SHOP_ARCADE;
        if (medFront && walls[fi].facing > 0.9f && d.room()) archedHeads(d, walls[fi], ms);
        for (const Wall& w : walls) {
            if (w.len < 1.2f) continue;
            bool front = &w == &walls[fi];
            // base and ground-floor course
            if (bottom && plinth && !(front && (store || d.doorBay >= 0)) && !industrial)
                wbox(d.k, w, -0.05f, w.len + 0.05f, ms.vBase - 0.5f, ms.vBase + 0.55f, 0.f, 0.05f, d.dark, d.wallMat, WF_FRONT | WF_TOP);
            if (bottom && floors >= 2 && !industrial) stringCourse(d, w, ms.vBase + fac.groundH, 0.22f, 0.12f, d.trim, d.trimMat);
            if (sillCourses && !industrial && (int)fac.style == 0) {
                FloorRow rows[64];
                int nr = massFloors(fac, ms.z0 - ms.vBase, ms.z1 - ms.vBase, rows, 64);
                for (int ri = 0; ri < nr; ri++)
                    if (rows[ri].idx > 0) stringCourse(d, w, ms.vBase + rows[ri].v0 + fac.sillH - 0.05f, 0.1f, 0.08f, d.trim, d.trimMat);
            }
            if (ms.z1 - ms.vBase > 2.5f && (b.roof == ROOF_FLAT || b.arch != AR_NONE) && !noCornice) cornice(d, w, ms.z1, corniceKind);
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
                    bool closed = d.r.chance(0.3f) && d.doorBay < 0;   // an enterable shop is open
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
        // ---- older walk-ups and shop parades: hanging painted boards, window awnings and boxes, faded side-wall signs
        if (d.old && (b.style == BS_SHOPS || b.style == BS_MIDRISE) && d.room()) oldFabricDetail(d, ms, walls, fi, floors, store);
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
                    if (rows[ri].idx == 0 && d.doorBay >= 0 && w.facing > 0.9f) continue;   // interior windows stay clear
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
                downpipe(d, bw, (b.seed & 1u) ? 0.4f : bw.len - 0.4f, ms.z1 + (ms.parapet ? 0.6f : 0.f), ms.vBase, pc);
            }
        }
        if (industrial) warehouseDetail(d, ms, walls);
        // ---- art deco front (the classic deco hotel); streamline moderne
        if (b.style == BS_DECO && b.arch == AR_NONE) decoFront(d, walls[fi], ms);
        if (b.arch == AR_DECO_STREAMLINE && d.room()) streamlineFront(d, ms, walls);
        // ---- a mural on an exposed side wall (bodegas, lofts, body shops, sheds: blockstyle.cpp ABF_MURAL)
        if ((b.archFlags & ABF_MURAL) && !muralDone && d.room()) {
            int wi;
            float mz0, mz1;
            if (muralWall(d, ms, walls, wi, mz0, mz1)) {
                Rng mr(b.seed ^ 0x3A11A1u);
                const Wall& w = walls[wi];
                float inset = Min(0.6f, w.len * 0.06f);
                mural(d, w, inset, w.len - inset, mz0, mz1, mr);
                muralDone = true;
            }
        }
        // ---- MiMo garden apartments: walkways along the walls of the court (L, U)
        if (b.arch == AR_MID_MIMO && d.room()) mimoWalkways(d, ms, walls, true);
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
    // yards, rooftop billboards and painted wall ads (once per building, on its first street-level mass)
    serviceYard(d);
    for (const FacadeMass& ms : masses) {
        if (fabsf(ms.z0 - ms.vBase) > 0.6f || ms.kind == FM_HOUSE || ms.kind == FM_DECO_TOWER) continue;
        const FacadeGPU& fac = (ms.facade != 0xffffffffu && ms.facade != b.facade && gBuildings && ms.facade < gBuildings->facades.size()) ? gBuildings->facades[ms.facade] : fac0;
        walls.clear();
        for (int i = 0; i < (int)ms.fp.size(); i++) walls.push_back(makeWall(ms.fp, i, fac.bayW, b.front));
        int fi = 0;
        for (int i = 1; i < (int)walls.size(); i++)
            if (walls[i].facing > walls[fi].facing) fi = i;
        // billboards stand on (and wall ads reach up to) the roof of the full-footprint body
        const FacadeMass* body = &ms;
        for (const FacadeMass& o : masses)
            if ((o.kind == FM_MAIN || o.kind == FM_PODIUM) && o.z1 > body->z1) body = &o;
        rooftopBillboard(d, *body, walls[fi]);
        if (!muralDone) wallAd(d, *body, walls);
        break;
    }
}

}  // namespace World
