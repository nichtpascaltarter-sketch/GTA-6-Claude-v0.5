// Per-cell generation timings (thread CPU time, best of 3) for every streaming cell that holds site content.
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
#include "src/world/interiors.cpp"
#include <thread>
#include <time.h>
#include <algorithm>

using namespace World;

static double cpuNow() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

struct Rec { int cx, cy; double t[2]; double site[2]; size_t v[2]; size_t nl, nc; const char* area; };

int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    int cps = kCellsPerSide;
    std::vector<Rec> recs;
    for (int cy = 0; cy < cps; cy++)
        for (int cx = 0; cx < cps; cx++) {
            if (gSites->cellElems[(size_t)cy * cps + cx].empty()) continue;
            Rec r = {};
            r.cx = cx; r.cy = cy;
            vec2 c = cellOrigin(cx, cy) + vec2(128.f);
            r.area = "landmarks/rural";
            if (c.x > kAirportX0 - 130 && c.x < kAirportX1 + 130 && c.y > kAirportY0 - 130 && c.y < kAirportY1 + 130) r.area = "airport";
            else if (c.x > kPortX0 - 130 && c.x < kPortX1 + 260 && c.y > kPortY0 - 130 && c.y < kPortY1 + 130) r.area = "port";
            else if (c.x > 3900 && c.x < 5000 && c.y > -5200 && c.y < -2900) r.area = "key coral";
            for (int d = 0; d < 2; d++) {
                double best = 1e9, bestS = 1e9;
                for (int rep = 0; rep < 3; rep++) {
                    CellGeometry geo;
                    double t0 = cpuNow();
                    generateCell(cx, cy, d == 0, geo);
                    best = Min(best, cpuNow() - t0);
                    r.v[d] = geo.opaque.verts.size() + geo.decals.verts.size();
                    if (d == 0) { r.nl = geo.lights.size(); r.nc = geo.collision.size(); }
                    CellGeometry g2;
                    t0 = cpuNow();
                    buildSiteCell(cx, cy, d == 0, g2);
                    bestS = Min(bestS, cpuNow() - t0);
                }
                r.t[d] = best * 1000.0;
                r.site[d] = bestS * 1000.0;
            }
            recs.push_back(r);
        }
    const char* areas[4] = {"airport", "port", "key coral", "landmarks/rural"};
    for (const char* a : areas) {
        double mx[2] = {0, 0}, sum[2] = {0, 0}, smx[2] = {0, 0};
        size_t vmx[2] = {0, 0};
        int n = 0;
        for (auto& r : recs) {
            if (strcmp(r.area, a)) continue;
            n++;
            for (int d = 0; d < 2; d++) { mx[d] = Max(mx[d], r.t[d]); sum[d] += r.t[d]; smx[d] = Max(smx[d], r.site[d]); vmx[d] = Max(vmx[d], r.v[d]); }
        }
        printf("%-16s %3d cells | LOD0 full cell avg %.2f max %.2f ms (sites max %.2f ms, max %zu verts) | LOD1 avg %.2f max %.2f ms (sites max %.2f, max %zu verts)\n", a, n,
               n ? sum[0] / n : 0, mx[0], smx[0], vmx[0], n ? sum[1] / n : 0, mx[1], smx[1], vmx[1]);
    }
    std::sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) { return a.t[0] > b.t[0]; });
    printf("worst LOD0 cells:\n");
    for (size_t i = 0; i < recs.size() && i < 8; i++) {
        auto& r = recs[i];
        vec2 c = cellOrigin(r.cx, r.cy) + vec2(128.f);
        printf("  (%d,%d) center (%.0f,%.0f) %s: LOD0 %.2f ms (sites %.2f) %zu verts %zu lights %zu boxes | LOD1 %.2f ms (sites %.2f) %zu verts\n", r.cx, r.cy, c.x, c.y,
               r.area, r.t[0], r.site[0], r.v[0], r.nl, r.nc, r.t[1], r.site[1], r.v[1]);
    }
    std::sort(recs.begin(), recs.end(), [](const Rec& a, const Rec& b) { return a.t[1] > b.t[1]; });
    printf("worst LOD1 cells:\n");
    for (size_t i = 0; i < recs.size() && i < 5; i++) {
        auto& r = recs[i];
        vec2 c = cellOrigin(r.cx, r.cy) + vec2(128.f);
        printf("  (%d,%d) center (%.0f,%.0f) %s: LOD1 %.2f ms (sites %.2f) %zu verts\n", r.cx, r.cy, c.x, c.y, r.area, r.t[1], r.site[1], r.v[1]);
    }
    Jobs::shutdown();
}
