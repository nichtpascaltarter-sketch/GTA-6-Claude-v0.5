// Map data built from the world at hudInit (stylized base texture with water depth for analytic coastlines,
// simplified road polylines in a spatial grid, building grid, district label anchors) and the vector map renderer
// shared by the radar and the full-screen pause map.
#include "ui_internal.h"
#include "../world/buildings.h"
#include "../world/sites.h"

namespace UI {
namespace map_detail {

const float kGridCell = 128.f;
const int kGridRes = (int)(2.f * World::kWorldHalf / kGridCell);
const float kDepthMax = 48.f;   // water depth encoding range (m), must match ui.hlsl

struct MRoad {
    u32 fine0 = 0, fineN = 0, coarse0 = 0, coarseN = 0;
    u8 cls = 0, flags = 0;
    vec2 mn, mx;
    float halfWidth = 4.f;
};

std::vector<vec2> g_pts;
std::vector<MRoad> g_roads;
std::vector<std::vector<u32>> g_roadGrid;
std::vector<std::vector<u32>> g_bldGrid;
std::vector<u32> g_roadStamp;
u32 g_stamp = 0;
std::vector<u32> g_majorRoads;
gfx::Texture g_baseTex;
bool g_mapReady = false;
std::vector<uix::MapLabel> g_labels;
vec2 g_landMin(-9600, -9700), g_landMax(6200, 9800);
vec2 g_lakeCenter(-2400, 6600);
std::vector<u32> g_visRoads[World::RC_COUNT];
std::vector<vec2> g_scr;

// Special-site shapes (airport pads, port yards, piers, parks, golf, stadium, terminals...)
enum SiteShape : u8 { SS_RECT = 0, SS_ROUND, SS_CAPSULE };
struct MSite {
    vec2 c, ax;
    float hx, hy;
    u32 color;
    u8 shape;
    u8 layer;        // 0 ground (always visible), 1 structure (drawn with buildings)
    u8 runway;       // centerline dashes
};
std::vector<MSite> g_sites;
std::vector<std::vector<u32>> g_siteGrid;

inline int gridIdx(float v) { return Clamp((int)floorf((v + World::kWorldHalf) / kGridCell), 0, kGridRes - 1); }

void dpSimplify(const std::vector<vec3>& pts, int a, int b, float tol, std::vector<char>& keep) {
    float maxD = 0;
    int idx = -1;
    vec2 pa = pts[a].xy(), pb = pts[b].xy();
    for (int i = a + 1; i < b; i++) {
        float d = distPointSegment2D(pts[i].xy(), pa, pb);
        if (d > maxD) { maxD = d; idx = i; }
    }
    if (idx >= 0 && maxD > tol) {
        keep[idx] = 1;
        dpSimplify(pts, a, idx, tol, keep);
        dpSimplify(pts, idx, b, tol, keep);
    }
}

void buildRoads() {
    const World::RoadNetwork& net = *World::gRoads;
    g_roads.clear();
    g_pts.clear();
    g_roadGrid.assign((size_t)kGridRes * kGridRes, {});
    g_majorRoads.clear();
    std::vector<char> keep;
    for (size_t ei = 0; ei < net.edges.size(); ei++) {
        const World::RoadEdge& e = net.edges[ei];
        if (e.pts.size() < 2) continue;
        MRoad r;
        r.cls = e.cls;
        r.flags = e.flags;
        r.halfWidth = e.halfWidth;
        int n = (int)e.pts.size();
        // fine LOD
        keep.assign(n, 0);
        keep[0] = keep[n - 1] = 1;
        dpSimplify(e.pts, 0, n - 1, 0.6f, keep);
        r.fine0 = (u32)g_pts.size();
        vec2 mn(1e9f), mx(-1e9f);
        for (int i = 0; i < n; i++)
            if (keep[i]) {
                g_pts.push_back(e.pts[i].xy());
                mn = vmin(mn, e.pts[i].xy());
                mx = vmax(mx, e.pts[i].xy());
            }
        r.fineN = (u32)g_pts.size() - r.fine0;
        // coarse LOD
        keep.assign(n, 0);
        keep[0] = keep[n - 1] = 1;
        dpSimplify(e.pts, 0, n - 1, 7.f, keep);
        r.coarse0 = (u32)g_pts.size();
        for (int i = 0; i < n; i++)
            if (keep[i]) g_pts.push_back(e.pts[i].xy());
        r.coarseN = (u32)g_pts.size() - r.coarse0;
        r.mn = mn - vec2(r.halfWidth);
        r.mx = mx + vec2(r.halfWidth);
        u32 id = (u32)g_roads.size();
        g_roads.push_back(r);
        if (r.cls == World::RC_HIGHWAY || r.cls == World::RC_BOULEVARD || r.cls == World::RC_RAMP || r.cls == World::RC_RURAL ||
            r.cls == World::RC_AVENUE)
            g_majorRoads.push_back(id);
        // grid insertion per fine segment
        for (u32 i = 0; i + 1 < r.fineN; i++) {
            vec2 a = g_pts[r.fine0 + i], b = g_pts[r.fine0 + i + 1];
            vec2 smn = vmin(a, b) - vec2(r.halfWidth), smx = vmax(a, b) + vec2(r.halfWidth);
            int x0 = gridIdx(smn.x), x1 = gridIdx(smx.x), y0 = gridIdx(smn.y), y1 = gridIdx(smx.y);
            for (int gy = y0; gy <= y1; gy++)
                for (int gx = x0; gx <= x1; gx++) {
                    auto& cell = g_roadGrid[(size_t)gy * kGridRes + gx];
                    if (cell.empty() || cell.back() != id) cell.push_back(id);
                }
        }
    }
    g_roadStamp.assign(g_roads.size(), 0);
}

void buildSites() {
    using namespace uix;
    g_sites.clear();
    g_siteGrid.assign((size_t)kGridRes * kGridRes, {});
    if (!World::gSites || !World::gSites->generated) return;
    const World::SiteSet& S = *World::gSites;
    for (const World::Pad& p : S.pads) {
        MSite m;
        m.c = p.c;
        m.ax = p.ax;
        m.hx = p.hx;
        m.hy = p.hy;
        m.shape = SS_RECT;
        m.layer = 0;
        m.runway = p.kind == World::PAD_RUNWAY;
        switch (p.kind) {
        case World::PAD_RUNWAY: m.color = C(0.33f, 0.345f, 0.41f); break;
        case World::PAD_SHOULDER: m.color = C(0.27f, 0.28f, 0.33f); break;
        case World::PAD_TAXIWAY: m.color = C(0.30f, 0.31f, 0.37f); break;
        case World::PAD_YARD: m.color = C(0.25f, 0.255f, 0.29f); break;
        case World::PAD_PLAZA: m.color = C(0.28f, 0.285f, 0.33f); break;
        case World::PAD_DECK: m.color = C(0.34f, 0.30f, 0.25f); break;
        case World::PAD_TURF: m.color = C(0.16f, 0.29f, 0.21f); break;
        case World::PAD_SAND: m.color = C(0.40f, 0.37f, 0.30f); break;
        default: m.color = C(0.26f, 0.27f, 0.31f); break;
        }
        g_sites.push_back(m);
    }
    for (const World::SiteElem& e : S.elems) {
        MSite m;
        m.c = e.c;
        m.ax = length2(e.ax) > 1e-6f ? normalize(e.ax) : vec2(1, 0);
        m.hx = e.hx;
        m.hy = e.hy;
        m.shape = SS_RECT;
        m.layer = 1;
        m.runway = 0;
        switch (e.kind) {
        case World::SK_TERMINAL: case World::SK_CONCOURSE: case World::SK_HANGAR: case World::SK_FIRE_STATION: case World::SK_CLUBHOUSE:
        case World::SK_BEACH_CLUB: case World::SK_CITY_HALL: case World::SK_SUGAR_MILL: case World::SK_FISH_SHACK: case World::SK_PORT_GATE:
        case World::SK_CONTROL_TOWER: case World::SK_SOLARIS:
            m.color = C(0.30f, 0.315f, 0.41f);
            break;
        case World::SK_FUEL_FARM: m.color = C(0.30f, 0.30f, 0.33f); m.shape = SS_ROUND; break;
        case World::SK_STADIUM: m.color = C(0.33f, 0.34f, 0.44f); m.shape = SS_ROUND; break;
        case World::SK_CONTAINER_BLOCK: m.color = C(0.36f, 0.27f, 0.25f); break;
        case World::SK_QUAY: m.color = C(0.28f, 0.28f, 0.31f); m.layer = 0; break;
        case World::SK_PARK: m.color = C(0.14f, 0.28f, 0.20f); m.layer = 0; break;
        case World::SK_GOLF_HOLE: m.color = C(0.17f, 0.33f, 0.22f); m.shape = SS_CAPSULE; m.layer = 0; break;
        case World::SK_MARINA: case World::SK_RIVER_MARINA: case World::SK_DOCK: case World::SK_BEACH_PIER: case World::SK_BOARDWALK:
        case World::SK_BOAT_RAMP:
            m.color = C(0.36f, 0.32f, 0.26f);
            m.layer = 0;
            break;
        default: continue;
        }
        g_sites.push_back(m);
        if (e.kind == World::SK_STADIUM) {
            // playing field inside the bowl
            MSite f = m;
            f.hx *= 0.55f;
            f.hy *= 0.5f;
            f.color = C(0.16f, 0.36f, 0.22f);
            g_sites.push_back(f);
        }
    }
    for (size_t i = 0; i < g_sites.size(); i++) {
        const MSite& m = g_sites[i];
        float r = sqrtf(m.hx * m.hx + m.hy * m.hy);
        int x0 = gridIdx(m.c.x - r), x1 = gridIdx(m.c.x + r), y0 = gridIdx(m.c.y - r), y1 = gridIdx(m.c.y + r);
        for (int gy = y0; gy <= y1; gy++)
            for (int gx = x0; gx <= x1; gx++) g_siteGrid[(size_t)gy * kGridRes + gx].push_back((u32)i);
    }
}

void buildBuildings() {
    g_bldGrid.assign((size_t)kGridRes * kGridRes, {});
    if (!World::gBuildings) return;
    const auto& bs = World::gBuildings->buildings;
    for (size_t i = 0; i < bs.size(); i++) {
        const World::Building& b = bs[i];
        g_bldGrid[(size_t)gridIdx(b.c.y) * kGridRes + gridIdx(b.c.x)].push_back((u32)i);
    }
}

vec3 splatColor(u32 s0, u32 s1) {
    static const vec3 cols[World::TL_COUNT] = {
        vec3(0.40f, 0.37f, 0.30f),   // sand
        vec3(0.150f, 0.235f, 0.210f),  // grass
        vec3(0.235f, 0.215f, 0.180f),  // dirt
        vec3(0.270f, 0.270f, 0.290f),  // rock
        vec3(0.150f, 0.175f, 0.150f),  // mud
        vec3(0.195f, 0.235f, 0.160f),  // sawgrass
        vec3(0.095f, 0.185f, 0.150f),  // forest
        vec3(0.150f, 0.168f, 0.215f),  // urban
    };
    vec4 a = unpackRGBA8(s0), b = unpackRGBA8(s1);
    float w[8] = {a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w};
    vec3 c(0.f);
    float sum = 0.f;
    for (int k = 0; k < 8; k++) { c += cols[k] * w[k]; sum += w[k]; }
    return sum > 1e-3f ? c / sum : cols[1];
}

void buildBaseTexture() {
    const World::WorldMap& m = *World::gMap;
    const int R = World::kHeightRes;
    std::vector<u32> px((size_t)R * R);
    const vec3 L = normalize(vec3(-1.f, 1.f, 1.3f));
    Jobs::parallelFor(R, [&](int row) {
        int ty = R - 1 - row;
        for (int tx = 0; tx < R; tx++) {
            size_t i = (size_t)ty * R + tx;
            float h = m.height[i];
            float wl = m.waterLevel[i];
            if (wl <= World::kNoWater + 1.f) {
                // land without its own water level: use the highest neighbouring water surface for an exact contour
                float best = World::kNoWater;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = Clamp(tx + dx, 0, R - 1), ny = Clamp(ty + dy, 0, R - 1);
                        best = Max(best, m.waterLevel[(size_t)ny * R + nx]);
                    }
                wl = best > World::kNoWater + 1.f ? best : Min(0.f, h) - 0.5f;
            }
            float d = wl - h;   // > 0 under water
            float e = (d >= 0.f ? 1.f : -1.f) * sqrtf(Min(fabsf(d), kDepthMax) / kDepthMax);
            vec3 c = splatColor(m.splat0[i], m.splat1[i]);
            World::Region reg = (World::Region)m.region[i];
            float urban = World::regionInfo(reg).urban;
            if (reg == World::REG_AIRPORT || reg == World::REG_PORT) c = lerp(c, vec3(0.17f, 0.18f, 0.21f), 0.6f);
            else if (urban > 0.75f) c = lerp(c, vec3(0.155f, 0.160f, 0.215f), 0.35f);
            // district shading: a faint per-district hue and a soft darker seam where districts meet
            u32 rh = hash32((u32)reg * 0x9E3779B9u + 17u);
            vec3 tint(0.96f + 0.08f * hashToFloat(rh), 0.96f + 0.08f * hashToFloat(rh >> 7), 0.96f + 0.08f * hashToFloat(rh >> 14));
            c = c * tint;
            if (reg != World::REG_OCEAN && wl < h) {
                bool seam = false;
                for (int k = 0; k < 4 && !seam; k++) {
                    int nx = Clamp(tx + (k == 0 ? 1 : k == 1 ? -1 : 0), 0, R - 1), ny = Clamp(ty + (k == 2 ? 1 : k == 3 ? -1 : 0), 0, R - 1);
                    size_t ni = (size_t)ny * R + nx;
                    u8 nr = m.region[ni];
                    if (nr != (u8)reg && nr != World::REG_OCEAN && m.height[ni] > m.waterLevel[ni]) seam = true;
                }
                if (seam) c = c * 0.8f;
            }
            // hillshade + elevation tint
            int x0 = Max(tx - 1, 0), x1 = Min(tx + 1, R - 1), y0 = Max(ty - 1, 0), y1 = Min(ty + 1, R - 1);
            float dx = (m.height[(size_t)ty * R + x1] - m.height[(size_t)ty * R + x0]) / ((x1 - x0) * World::kHeightCell);
            float dy = (m.height[(size_t)y1 * R + tx] - m.height[(size_t)y0 * R + tx]) / ((y1 - y0) * World::kHeightCell);
            vec3 n = normalize(vec3(-dx * 1.6f, -dy * 1.6f, 1.f));
            float shade = 1.f + 1.3f * (dot(n, L) - L.z);
            c = c * Clamp(shade, 0.6f, 1.45f) * (1.f + 0.22f * Saturate(h / 140.f));
            // subtle large-scale variation so big areas do not look flat
            float var = 0.94f + 0.06f * hashToFloat(hash2i(tx / 24, ty / 24));
            c = c * var;
            px[(size_t)row * R + tx] = packRGBA8(Saturate(c.x), Saturate(c.y), Saturate(c.z), e * 0.5f + 0.5f);
        }
    }, 16);
    g_baseTex = gfx::createTexture2D(R, R, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_GENMIPS, 0, 1);
    gfx::uploadTexture2D(g_baseTex, 0, 0, px.data(), R * 4);
    gfx::ctx->GenerateMips(g_baseTex.srv);
}

// Label anchors: for each region the land point farthest from other regions (64 m grid)
void buildLabels() {
    const World::WorldMap& m = *World::gMap;
    const int G = 320;
    const float cell = 2.f * World::kWorldHalf / G;
    std::vector<u8> reg((size_t)G * G);
    std::vector<u8> land((size_t)G * G);
    vec2 lmn(1e9f), lmx(-1e9f);
    for (int y = 0; y < G; y++)
        for (int x = 0; x < G; x++) {
            float wx = -World::kWorldHalf + (x + 0.5f) * cell, wy = -World::kWorldHalf + (y + 0.5f) * cell;
            bool l = !m.isWater(wx, wy);
            land[(size_t)y * G + x] = l;
            reg[(size_t)y * G + x] = (u8)m.regionAt(wx, wy);
            if (l && m.regionAt(wx, wy) != World::REG_OCEAN) { lmn = vmin(lmn, vec2(wx, wy)); lmx = vmax(lmx, vec2(wx, wy)); }
        }
    g_landMin = lmn - vec2(300.f);
    g_landMax = lmx + vec2(300.f);
    struct Res { vec2 pos; float dist; };
    std::vector<Res> res(World::REG_COUNT, {vec2(0, 0), -1.f});
    Jobs::parallelFor(World::REG_COUNT, [&](int r) {
        if (r == World::REG_OCEAN) return;
        std::vector<float> g((size_t)G * G);
        bool any = false;
        for (size_t i = 0; i < g.size(); i++) {
            bool in = reg[i] == r && land[i];
            g[i] = in ? 1e20f : 0.f;
            any |= in;
        }
        if (!any) return;
        draw2d_detail::edt(g, G, G);
        float best = -1;
        int bi = 0;
        for (size_t i = 0; i < g.size(); i++)
            if (g[i] > best) { best = g[i]; bi = (int)i; }
        res[r].dist = sqrtf(best) * cell;
        res[r].pos = vec2(-World::kWorldHalf + (bi % G + 0.5f) * cell, -World::kWorldHalf + (bi / G + 0.5f) * cell);
    });
    g_labels.clear();
    for (int r = 1; r < World::REG_COUNT; r++) {
        if (res[r].dist <= 0.f) continue;
        World::Region rr = (World::Region)r;
        float imp;
        switch (rr) {
        case World::REG_SAWGRASS: case World::REG_FARMLAND: case World::REG_RIDGE: case World::REG_KEYS: imp = 3.f; break;
        case World::REG_FORT_CASTELL: case World::REG_LAKE_TOWN: case World::REG_HARLOW: case World::REG_GULF_TOWN:
        case World::REG_KEY_TOWN: case World::REG_REDLAND: case World::REG_SUBURBS: imp = 2.f; break;
        default: imp = 1.f; break;
        }
        g_labels.push_back({res[r].pos, World::regionInfo(rr).name, imp, false});
    }
    // Metropolis label + water bodies (only where the anchor really is water)
    g_labels.push_back({vec2(2700.f, 900.f), "PORTO SOL", 4.f, false});
    if (!m.lakePoly.empty()) {
        vec2 c(0.f);
        for (vec2 p : m.lakePoly) c += p;
        g_lakeCenter = c / (float)m.lakePoly.size();
        g_labels.push_back({g_lakeCenter, "Lake Okahatchee", 2.f, true});
    }
    struct W { vec2 p; const char* n; float imp; };
    W waters[] = {{vec2(8200.f, 1500.f), "Atlantic Ocean", 3.f},     {vec2(4420.f, 2500.f), "Porto Sol Bay", 1.f},
                  {vec2(-6600.f, -7800.f), "Gulf of Sombra", 3.f},  {vec2(-3200.f, -9300.f), "Solano Straits", 2.f},
                  {vec2(2200.f, -7600.f), "Coral Reef Passage", 2.f}};
    for (auto& w : waters) {
        vec2 p = w.p;
        if (!m.isWater(p.x, p.y)) {
            bool found = false;
            for (int k = 1; k <= 8 && !found; k++)
                for (int a = 0; a < 8 && !found; a++) {
                    vec2 q = p + vec2(cosf(a * kTwoPi / 8), sinf(a * kTwoPi / 8)) * (150.f * k);
                    if (m.isWater(q.x, q.y)) { p = q; found = true; }
                }
            if (!found) continue;
        }
        g_labels.push_back({p, w.n, w.imp, true});
    }
}

// Colors
u32 roadFillRadar(u8 cls) {
    using namespace uix;
    switch (cls) {
    case World::RC_HIGHWAY: return C(0.92f, 0.68f, 0.36f);
    case World::RC_RAMP: return C(0.84f, 0.62f, 0.34f);
    case World::RC_BOULEVARD: return C(0.70f, 0.73f, 0.81f);
    case World::RC_AVENUE: return C(0.62f, 0.65f, 0.74f);
    case World::RC_STREET: return C(0.52f, 0.56f, 0.65f);
    case World::RC_LANE: return C(0.46f, 0.50f, 0.59f);
    case World::RC_RURAL: return C(0.62f, 0.61f, 0.57f);
    default: return C(0.47f, 0.42f, 0.35f);
    }
}
u32 roadFill(u8 cls) {
    using namespace uix;
    switch (cls) {
    case World::RC_HIGHWAY: return C(1.00f, 0.74f, 0.38f);
    case World::RC_RAMP: return C(0.94f, 0.68f, 0.36f);
    case World::RC_BOULEVARD: return C(0.88f, 0.90f, 0.96f);
    case World::RC_AVENUE: return C(0.76f, 0.79f, 0.87f);
    case World::RC_STREET: return C(0.60f, 0.64f, 0.74f);
    case World::RC_LANE: return C(0.52f, 0.56f, 0.66f);
    case World::RC_RURAL: return C(0.78f, 0.76f, 0.70f);
    default: return C(0.55f, 0.49f, 0.40f);
    }
}
float roadMinPx(u8 cls, bool radar) {
    static const float radarMin[World::RC_COUNT] = {4.4f, 3.2f, 2.8f, 2.3f, 2.0f, 2.4f, 1.6f, 2.6f};
    static const float fullMin[World::RC_COUNT] = {2.6f, 2.0f, 1.6f, 1.1f, 0.9f, 1.5f, 0.9f, 1.5f};
    return radar ? radarMin[cls] : fullMin[cls];
}
const int kDrawOrder[World::RC_COUNT] = {World::RC_DIRT, World::RC_LANE, World::RC_STREET, World::RC_RURAL, World::RC_AVENUE,
                                         World::RC_BOULEVARD, World::RC_RAMP, World::RC_HIGHWAY};

}  // namespace map_detail

namespace uix {
using namespace map_detail;

bool mapReady() { return g_mapReady; }

void mapInit() {
    if (!World::gMap || !World::gRoads) return;
    double t0 = TimeSeconds();
    if (g_baseTex.srv) g_baseTex.release();
    buildBaseTexture();
    double t1 = TimeSeconds();
    buildRoads();
    buildBuildings();
    buildSites();
    double t2 = TimeSeconds();
    buildLabels();
    g_mapReady = true;
    LOG("UI map: base %dx%d in %.0f ms, %zu roads (%zu pts) in %.0f ms, %zu labels in %.0f ms", World::kHeightRes, World::kHeightRes,
        (t1 - t0) * 1000.0, g_roads.size(), g_pts.size(), (t2 - t1) * 1000.0, g_labels.size(), (TimeSeconds() - t2) * 1000.0);
}

const std::vector<MapLabel>& mapLabels() { return g_labels; }
void mapLandBounds(vec2& mn, vec2& mx) { mn = g_landMin; mx = g_landMax; }

std::string mapDistrictAt(vec2 p) {
    if (!World::gMap) return "";
    const World::WorldMap& m = *World::gMap;
    if (p.x < -World::kWorldHalf || p.y < -World::kWorldHalf || p.x > World::kWorldHalf || p.y > World::kWorldHalf) return "Atlantic Ocean";
    World::Region r = m.regionAt(p.x, p.y);
    if (m.isWater(p.x, p.y)) {
        if (fabsf(m.waterAt(p.x, p.y) - World::kLakeLevel) < 0.5f) return "Lake Okahatchee";
        if (r == World::REG_OCEAN) {
            if (p.x > 3700.f && p.x < 5000.f && p.y > -2800.f && p.y < 4600.f) return "Porto Sol Bay";
            if (p.x < -2500.f && p.y < -5000.f) return "Gulf of Sombra";
            return "Atlantic Ocean";
        }
    }
    return World::regionInfo(r).name;
}

std::string mapStreetAt(vec2 p, float maxDist) {
    if (!World::gRoads) return "";
    int e = World::gRoads->nearestEdge(p, maxDist);
    return e >= 0 ? World::gRoads->edges[e].name : std::string();
}

void drawMapBase(const MapView& v, const MapDrawOpts& o) {
    if (!g_mapReady) return;
    bool radar = o.style == MAPSTYLE_RADAR;
    // screen region to cover
    vec2 smn, smx;
    if (o.extentMax.x > o.extentMin.x) { smn = o.extentMin; smx = o.extentMax; }
    else { smn = v.screenCenter - vec2(o.extentPx); smx = v.screenCenter + vec2(o.extentPx); }
    vec2 corners[4] = {smn, vec2(smx.x, smn.y), smx, vec2(smn.x, smx.y)};
    vec2 uv[4];
    vec2 wmn(1e9f), wmx(-1e9f);
    const float K = World::kWorldHalf;
    for (int i = 0; i < 4; i++) {
        vec2 w = v.toWorld(corners[i]);
        wmn = vmin(wmn, w);
        wmx = vmax(wmx, w);
        uv[i] = vec2((w.x + K) / (2.f * K), 1.f - (w.y + K) / (2.f * K));
    }
    float a = o.alpha;
    u32 landTint = o.dim ? C(0.55f, 0.55f, 0.6f, a) : C(1.f, 1.f, 1.f, a);
    u32 waterTint = o.dim ? C(0.6f, 0.6f, 0.65f) : C(1.f, 1.f, 1.f);
    mapQuad(g_baseTex.srv, corners, uv, landTint, waterTint, radar ? 0.8f : 1.f);
    if (o.dim) return;
    float mpp = v.mpp;
    // world -> screen rotation for directions
    float cr = cosf(-v.rot), sr = sinf(-v.rot);
    auto dirToScreen = [&](vec2 d) { return vec2(cr * d.x - sr * d.y, -(sr * d.x + cr * d.y)); };
    // ---------------------------------------------------------------- special sites (pads, yards, parks, piers...)
    float bldFade = radar ? Saturate((3.4f - mpp) / 1.2f) : Saturate((2.6f - mpp) / 1.0f);
    if (!g_sites.empty()) {
        static std::vector<u32> vis;
        vis.clear();
        g_stamp++;
        static std::vector<u32> siteStamp;
        if (siteStamp.size() != g_sites.size()) siteStamp.assign(g_sites.size(), 0);
        int x0 = gridIdx(wmn.x - 64.f), x1 = gridIdx(wmx.x + 64.f), y0 = gridIdx(wmn.y - 64.f), y1 = gridIdx(wmx.y + 64.f);
        if ((x1 - x0 + 1) * (y1 - y0 + 1) > 4000) {
            for (u32 i = 0; i < (u32)g_sites.size(); i++) vis.push_back(i);
        } else {
            for (int gy = y0; gy <= y1; gy++)
                for (int gx = x0; gx <= x1; gx++)
                    for (u32 si : g_siteGrid[(size_t)gy * kGridRes + gx])
                        if (siteStamp[si] != g_stamp) { siteStamp[si] = g_stamp; vis.push_back(si); }
        }
        for (int layer = 0; layer < 2; layer++) {
            float la = layer == 0 ? a : a * bldFade;
            if (la <= 0.01f) continue;
            for (u32 si : vis) {
                const MSite& m = g_sites[si];
                if (m.layer != layer) continue;
                vec2 sp = v.toScreen(m.c);
                float rad = (m.hx + m.hy) / mpp;
                if (sp.x < smn.x - rad || sp.x > smx.x + rad || sp.y < smn.y - rad || sp.y > smx.y + rad) continue;
                vec2 ax = dirToScreen(m.ax);
                float hx = Max(m.hx / mpp, 0.6f), hy = Max(m.hy / mpp, 0.6f);
                float r = m.shape == SS_RECT ? Min(1.f, Min(hx, hy)) : Min(hx, hy);
                u32 col = withAlpha(m.color, la);
                if (m.shape == SS_ROUND && hx > 1.5f && hy > 1.5f) {
                    // ellipse-like: rounded rect with full radius on the short side plus an inner fill
                    roundRectRotated(sp, ax, hx, hy, Min(hx, hy) * 0.95f, col);
                } else {
                    roundRectRotated(sp, ax, hx, hy, r, col);
                }
                if (m.runway && mpp < 4.f && hx > 20.f) {
                    // centerline dashes and threshold bars
                    float dashM = 30.f, gapM = 30.f;
                    int n = (int)(m.hx * 2.f / (dashM + gapM));
                    u32 dc = C(0.92f, 0.94f, 1.f, 0.8f * la);
                    for (int k = 0; k < n; k++) {
                        float s0 = -m.hx + 60.f + k * (dashM + gapM);
                        if (s0 + dashM > m.hx - 60.f) break;
                        vec2 pa = v.toScreen(m.c + m.ax * s0), pb = v.toScreen(m.c + m.ax * (s0 + dashM));
                        capsule(pa.x, pa.y, pb.x, pb.y, Max(1.f, 1.2f / mpp), dc);
                    }
                    for (int end = -1; end <= 1; end += 2) {
                        vec2 pc = v.toScreen(m.c + m.ax * (end * (m.hx - 25.f)));
                        roundRectRotated(pc, ax, Max(0.8f, 12.f / mpp), Max(1.f, m.hy * 0.7f / mpp), 0.5f, dc);
                    }
                }
            }
        }
    }
    // ---------------------------------------------------------------- buildings
    if (o.buildings && bldFade > 0.01f && World::gBuildings) {
        const auto& bs = World::gBuildings->buildings;
        int x0 = gridIdx(wmn.x - 64.f), x1 = gridIdx(wmx.x + 64.f), y0 = gridIdx(wmn.y - 64.f), y1 = gridIdx(wmx.y + 64.f);
        u32 cBase = C(0.230f, 0.250f, 0.320f, a * bldFade);
        u32 cTall = C(0.285f, 0.300f, 0.395f, a * bldFade);
        u32 cHouse = C(0.215f, 0.225f, 0.280f, a * bldFade);
        u32 cInd = C(0.235f, 0.235f, 0.255f, a * bldFade);
        for (int gy = y0; gy <= y1; gy++)
            for (int gx = x0; gx <= x1; gx++)
                for (u32 bi : g_bldGrid[(size_t)gy * kGridRes + gx]) {
                    const World::Building& b = bs[bi];
                    vec2 sp = v.toScreen(b.c);
                    float rad = (b.hx + b.hy) / mpp;
                    if (sp.x < smn.x - rad || sp.x > smx.x + rad || sp.y < smn.y - rad || sp.y > smx.y + rad) continue;
                    u32 col = cBase;
                    if (b.style == World::BS_TOWER || b.height > 40.f) col = cTall;
                    else if (b.style == World::BS_HOUSE || b.style == World::BS_VILLA || b.style == World::BS_FARMHOUSE) col = cHouse;
                    else if (b.style == World::BS_WAREHOUSE || b.style == World::BS_FACTORY || b.style == World::BS_BARN) col = cInd;
                    vec2 ax = dirToScreen(b.ax);
                    float hx = Max(b.hx / mpp - 0.3f, 0.6f), hy = Max(b.hy / mpp - 0.3f, 0.6f);
                    roundRectRotated(sp, ax, hx, hy, Min(1.2f, Min(hx, hy)), col);
                }
    }
    // ---------------------------------------------------------------- roads
    for (auto& l : g_visRoads) l.clear();
    bool fine = mpp <= 3.5f;
    bool majorOnly = mpp > 9.f || (radar && mpp > 6.5f);
    float margin = 40.f;
    {
        int x0 = gridIdx(wmn.x - margin), x1 = gridIdx(wmx.x + margin), y0 = gridIdx(wmn.y - margin), y1 = gridIdx(wmx.y + margin);
        bool useGrid = (x1 - x0 + 1) * (y1 - y0 + 1) <= 900;
        auto isMinor = [](u8 c) { return c == World::RC_STREET || c == World::RC_LANE || c == World::RC_DIRT; };
        if (useGrid) {
            g_stamp++;
            for (int gy = y0; gy <= y1; gy++)
                for (int gx = x0; gx <= x1; gx++)
                    for (u32 ri : g_roadGrid[(size_t)gy * kGridRes + gx]) {
                        if (g_roadStamp[ri] == g_stamp) continue;
                        g_roadStamp[ri] = g_stamp;
                        const MRoad& r = g_roads[ri];
                        if (majorOnly && isMinor(r.cls)) continue;
                        g_visRoads[r.cls].push_back(ri);
                    }
        } else {
            auto consider = [&](u32 ri) {
                const MRoad& r = g_roads[ri];
                if (r.mx.x < wmn.x || r.mn.x > wmx.x || r.mx.y < wmn.y || r.mn.y > wmx.y) return;
                g_visRoads[r.cls].push_back(ri);
            };
            if (majorOnly)
                for (u32 ri : g_majorRoads) consider(ri);
            else
                for (u32 ri = 0; ri < (u32)g_roads.size(); ri++) consider(ri);
        }
    }
    float minorFade = radar ? Saturate((6.5f - mpp) / 2.f) : Saturate((12.f - mpp) / 5.f);
    float widthScale = radar ? 0.6f : 1.f;
    u32 casing = C(0.035f, 0.045f, 0.085f, 0.92f * a);
    float casingPx = radar ? (mpp < 4.f ? 1.0f : 0.f) : (mpp < 3.f ? 1.1f : 0.f);
    // two passes: casings, then fills (in class order so major roads sit on top)
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0 && casingPx <= 0.f) continue;
        for (int oi = 0; oi < World::RC_COUNT; oi++) {
            int cls = kDrawOrder[oi];
            if (g_visRoads[cls].empty()) continue;
            bool minor = cls == World::RC_STREET || cls == World::RC_LANE || cls == World::RC_DIRT;
            float fade = minor ? minorFade : 1.f;
            if (fade <= 0.01f) continue;
            if (!radar && pass == 0 && minor && mpp > 2.2f) continue;
            u32 fill = radar ? roadFillRadar((u8)cls) : roadFill((u8)cls);
            u32 col = pass == 0 ? withAlpha(casing, fade) : withAlpha(fill, a * fade);
            for (u32 ri : g_visRoads[cls]) {
                const MRoad& r = g_roads[ri];
                float w = Max(roadMinPx((u8)cls, radar), r.halfWidth * 2.f / mpp * widthScale);
                if (pass == 0) w += casingPx * 2.f;
                u32 p0 = fine ? r.fine0 : r.coarse0, n = fine ? r.fineN : r.coarseN;
                g_scr.resize(n);
                for (u32 i = 0; i < n; i++) g_scr[i] = v.toScreen(g_pts[p0 + i]);
                for (u32 i = 0; i + 1 < n; i++) {
                    vec2 pa = g_scr[i], pb = g_scr[i + 1];
                    if (Max(pa.x, pb.x) < smn.x - w || Min(pa.x, pb.x) > smx.x + w || Max(pa.y, pb.y) < smn.y - w ||
                        Min(pa.y, pb.y) > smx.y + w)
                        continue;
                    capsule(pa.x, pa.y, pb.x, pb.y, w, col);
                }
            }
        }
    }
}

void drawMapRoute(const MapView& v, const std::vector<vec2>& route, u32 color, float widthPx, float alpha, vec2 cullMin, vec2 cullMax) {
    if (route.size() < 2) return;
    static std::vector<vec2> scr;
    scr.resize(route.size());
    for (size_t i = 0; i < route.size(); i++) scr[i] = v.toScreen(route[i]);
    for (int pass = 0; pass < 2; pass++) {
        float w = pass == 0 ? widthPx + 3.f : widthPx;
        u32 c = pass == 0 ? C(0.03f, 0.03f, 0.08f, 0.75f * alpha) : withAlpha(color, alpha);
        for (size_t i = 0; i + 1 < scr.size(); i++) {
            vec2 a = scr[i], b = scr[i + 1];
            if (Max(a.x, b.x) < cullMin.x - w || Min(a.x, b.x) > cullMax.x + w || Max(a.y, b.y) < cullMin.y - w ||
                Min(a.y, b.y) > cullMax.y + w)
                continue;
            capsule(a.x, a.y, b.x, b.y, w, c);
        }
    }
}

void drawBlipGlyph(const Blip& b, vec2 p, float size, float alpha, float time, bool showHeight, bool onEdge) {
    if (b.icon >= BLIP_COUNT) return;
    BlipIcon ic = b.icon;
    bool semantic = ic == BLIP_ENEMY || ic == BLIP_FRIEND || ic == BLIP_POLICE || ic == BLIP_POLICE_HELI || ic == BLIP_OBJECTIVE ||
                    ic == BLIP_WAYPOINT || ic == BLIP_MISSION;
    u32 col = (b.color == 0 || (semantic && b.color == 0xffffffffu)) ? blipDefaultColor(ic) : b.color;
    if (ic == BLIP_POLICE || ic == BLIP_POLICE_HELI) col = fmodf(time * 2.5f, 1.f) < 0.5f ? kRed : kBlue;
    float a = alpha;
    if (b.flash) a *= 0.35f + 0.65f * (fmodf(time * 2.f, 1.f) < 0.6f ? 1.f : 0.f);
    if (onEdge) size *= 0.85f;
    size *= Clamp(b.scale, 0.3f, 3.f);
    u32 outline = C(0.02f, 0.03f, 0.07f, 0.9f * a);
    col = withAlpha(col, a * ((col >> 24) & 255) / 255.f);
    bool round = blipIsRound(ic);
    int heightDir = 0;
    if (showHeight && fabsf(b.heightDiff) > 4.f) heightDir = b.heightDiff > 0 ? 1 : -1;
    if (round) {
        float s = size * (ic == BLIP_DOT ? 0.62f : 0.8f);
        if (heightDir != 0) {
            drawIcon(ICO_ARROW_UP, p.x, p.y, s * 1.25f, col, 1.6f, outline, heightDir > 0 ? 0.f : kPi);
        } else {
            drawIcon(ic, p.x, p.y, s, col, 1.6f, outline);
        }
        return;
    }
    if (ic == BLIP_MISSION) {
        drawIcon(BLIP_MISSION, p.x, p.y, size, col, 1.8f, outline);
        if (b.letter) {
            char str[2] = {b.letter, 0};
            TextStyle st;
            st.font = FONT_HEADING;
            st.size = size * 0.46f;
            st.color = C(0.05f, 0.06f, 0.12f, a);
            st.align = ALIGN_CENTER;
            text(p.x, p.y - st.size * 0.55f, str, st);
        }
    } else if (ic == BLIP_WAYPOINT) {
        drawIcon(BLIP_WAYPOINT, p.x, p.y - size * 0.3f, size * 1.1f, col, 1.8f, outline);
    } else {
        drawIcon(ic, p.x, p.y, size, col, 1.8f, outline);
    }
    if (heightDir != 0) {
        float s = size * 0.42f;
        drawIcon(ICO_ARROW_UP, p.x + size * 0.42f, p.y - size * 0.34f, s, col, 1.4f, outline, heightDir > 0 ? 0.f : kPi);
    }
}

}  // namespace uix
}  // namespace UI
