#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
#include "src/world/transit.cpp"
#include "src/world/transitmesh.cpp"
#include "src/world/sitecell.cpp"
#include "src/world/facadedetail.cpp"
#include "src/world/interiorkit.cpp"
#include "src/world/interiorfurniture.cpp"
#include "src/world/interiorlayouts.cpp"
#include "src/world/interiorhomes.cpp"
#include "src/world/interiorvenues.cpp"
#include "src/world/interiorshops.cpp"
#include "src/world/interiorcivic.cpp"
#include "src/world/interiorindustrial.cpp"
#include "src/world/interiortower.cpp"
#include "src/world/interiorgarages.cpp"
#include "src/world/interiorresidences.cpp"
#include "src/world/interiors.cpp"
#include <thread>
#include <unordered_map>
#include <map>
#include <array>
#include <algorithm>

// PROTOTYPE for docs/HANDBUILT_WORLD.md: bake the terrain to data files and measure what the special sites (landforms and
// pads) and the roads (grading) change, to check that the terrain separates from the rest. Not part of the game.
using namespace World;
namespace World { extern bool gTerrainBakeNatural; }

static void writeFile(const char* path, const void* p, size_t n) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    fwrite(p, 1, n, f);
    fclose(f);
}

struct Diff { size_t heightChanged = 0, toWater = 0, toLand = 0; double maxUp = 0, maxDown = 0; };
static Diff diffMaps(const std::vector<float>& h0, const std::vector<float>& w0, const std::vector<float>& h1, const std::vector<float>& w1) {
    Diff d;
    for (size_t i = 0; i < h0.size(); i++) {
        const bool wet0 = w0[i] > kNoWater + 1.f && w0[i] > h0[i], wet1 = w1[i] > kNoWater + 1.f && w1[i] > h1[i];
        const double dh = (double)h1[i] - h0[i];
        if (fabs(dh) > 0.25) d.heightChanged++;
        if (!wet0 && wet1) d.toWater++;
        if (wet0 && !wet1) d.toLand++;
        if (dh > d.maxUp) d.maxUp = dh;
        if (dh < d.maxDown) d.maxDown = dh;
    }
    return d;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: nothing is lost if the run dies
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    const double texelKm2 = (double)kHeightCell * kHeightCell / 1e6;
    // 1. the natural terrain: coast, rivers, canals, the lake and its dike, hills; no site landforms, no pads, no roads
    WorldMap* nat = new WorldMap;
    gTerrainBakeNatural = true;
    double t0 = TimeSeconds();
    nat->generate();
    printf("natural terrain generated in %.2f s\n", TimeSeconds() - t0);
    // 2. today's terrain after the special sites (port island fill, basins, marinas, quays, the airfield and every site pad)
    gTerrainBakeNatural = false;
    WorldMap* map = new WorldMap;
    t0 = TimeSeconds();
    map->generate();
    gMap = map;
    printf("terrain with the sites generated in %.2f s\n", TimeSeconds() - t0);
    std::vector<float> hSites = map->height, wSites = map->waterLevel;
    // 3. the road network grades it (flattenAlong / lowerAlong under every road and junction)
    RoadNetwork* roads = new RoadNetwork;
    t0 = TimeSeconds();
    roads->generate(*map);
    gRoads = roads;
    printf("roads generated (and graded) in %.2f s: %zu nodes, %zu edges\n", TimeSeconds() - t0, roads->nodes.size(), roads->edges.size());
    // land extent and areas (natural terrain)
    size_t land = 0, water = 0;
    int x0 = kHeightRes, x1 = -1, y0 = kHeightRes, y1 = -1;
    std::array<size_t, REG_COUNT> regLand{};
    for (int y = 0; y < kHeightRes; y++)
        for (int x = 0; x < kHeightRes; x++) {
            const size_t i = (size_t)y * kHeightRes + x;
            const bool wet = nat->waterLevel[i] > kNoWater + 1.f && nat->waterLevel[i] > nat->height[i];
            if (wet) { water++; continue; }
            land++;
            regLand[nat->region[i]]++;
            x0 = Min(x0, x); x1 = Max(x1, x); y0 = Min(y0, y); y1 = Max(y1, y);
        }
    printf("map %.2f km square (%d x %d texels of %.0f m); natural land %.1f km2, water %.1f km2; land spans x %.0f..%.0f, y %.0f..%.0f (%.1f x %.1f km)\n",
           2.f * kWorldHalf / 1000.f, kHeightRes, kHeightRes, kHeightCell, land * texelKm2, water * texelKm2, texelToWorld(x0), texelToWorld(x1),
           texelToWorld(y0), texelToWorld(y1), (x1 - x0 + 1) * kHeightCell / 1000.f, (y1 - y0 + 1) * kHeightCell / 1000.f);
    for (int r = 0; r < REG_COUNT; r++)
        if (regLand[(size_t)r]) printf("  region %-14s land %6.2f km2\n", regionInfo((Region)r).name, regLand[(size_t)r] * texelKm2);
    // what the sites and the roads change
    Diff ds = diffMaps(nat->height, nat->waterLevel, hSites, wSites);
    Diff dr = diffMaps(hSites, wSites, map->height, map->waterLevel);
    printf("sites vs natural: height changed (>0.25 m) on %.2f km2, land->water %.3f km2, water->land %.3f km2, max +%.1f / %.1f m\n", ds.heightChanged * texelKm2,
           ds.toWater * texelKm2, ds.toLand * texelKm2, ds.maxUp, ds.maxDown);
    printf("roads vs sites:   height changed (>0.25 m) on %.2f km2, land->water %.3f km2, water->land %.3f km2, max +%.1f / %.1f m\n", dr.heightChanged * texelKm2,
           dr.toWater * texelKm2, dr.toLand * texelKm2, dr.maxUp, dr.maxDown);
    // the bake: raw files for a size check (compression is measured outside with gzip -9 as a stand-in for the game's codec)
    const size_t n = (size_t)kHeightRes * kHeightRes;
    writeFile("tb_height_natural.f32", nat->height.data(), n * 4);
    writeFile("tb_height_sites.f32", hSites.data(), n * 4);
    writeFile("tb_height_final.f32", map->height.data(), n * 4);
    writeFile("tb_water_sites.f32", wSites.data(), n * 4);
    // quantised: height in cm (int16 over -327..327 m) and the water as a 1-byte class (0 dry, 1 sea level, 2 lake level)
    std::vector<int16_t> q(n), qd(n);
    std::vector<u8> wq(n);
    for (size_t i = 0; i < n; i++) {
        q[i] = (int16_t)Clamp(lrintf(hSites[i] * 100.f), -32767L, 32767L);
        const bool wet = wSites[i] > kNoWater + 1.f && wSites[i] > hSites[i];
        wq[i] = !wet ? 0 : (fabsf(wSites[i]) < 0.01f ? 1 : 2);
    }
    for (int y = 0; y < kHeightRes; y++)   // row deltas (smooth ground packs better)
        for (int x = 0; x < kHeightRes; x++) {
            const size_t i = (size_t)y * kHeightRes + x;
            qd[i] = (int16_t)(x ? q[i] - q[i - 1] : q[i]);
        }
    writeFile("tb_height_sites_cm.i16", q.data(), n * 2);
    writeFile("tb_height_sites_cm_delta.i16", qd.data(), n * 2);
    writeFile("tb_water_sites_class.u8", wq.data(), n);
    printf("wrote the bake files\n");
    Jobs::shutdown();
    return 0;
}
