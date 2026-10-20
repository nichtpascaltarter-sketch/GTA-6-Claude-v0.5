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
#include <time.h>
#include <map>
using namespace World;
static double cpuNow() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int heavyScan(const WorldMap& map, const RoadNetwork& roads, const BuildingSet& bs) {
    int cps = kCellsPerSide;
    struct H { size_t v; int cx, cy; size_t road, street, bld, bmax; int bmaxI; };
    std::vector<H> hs;
    for (int cy = 0; cy < cps; cy++)
        for (int cx = 0; cx < cps; cx++) {
            if (bs.cellLists[(size_t)cy * cps + cx].empty()) continue;
            CellGeometry g;
            generateCell(cx, cy, true, g);
            size_t v = g.opaque.verts.size() + g.decals.verts.size();
            if (v < 90000) continue;
            RoadCellOutput ro;
            buildRoadCell(roads, map, cx, cy, ro);
            size_t bsum = 0, bmax = 0;
            int bi = -1;
            vec3 org(cellOrigin(cx, cy), 0);
            for (int b : bs.cellLists[(size_t)cy * cps + cx]) {
                MeshData m;
                std::vector<CollisionBox> col;
                std::vector<PropInstance> pr;
                std::vector<LightInstance> li;
                buildBuildingMesh(bs.buildings[b], bs.facades[bs.buildings[b].facade], map, true, org, m, &col, &pr, &li);
                bsum += m.verts.size();
                if (m.verts.size() > bmax) { bmax = m.verts.size(); bi = b; }
            }
            hs.push_back({v, cx, cy, ro.road.verts.size() + ro.decals.verts.size(), ro.street.verts.size(), bsum, bmax, bi});
        }
    std::sort(hs.begin(), hs.end(), [](const H& a, const H& b) { return a.v > b.v; });
    for (size_t i = 0; i < hs.size() && i < 12; i++) {
        const H& h = hs[i];
        vec2 o = cellOrigin(h.cx, h.cy);
        const Building& B = bs.buildings[h.bmaxI];
        printf("cell (%d,%d) at %.0f,%.0f %s: %zu verts (roads %zu, street %zu, buildings %zu in %zu, worst %zu: style %d floors %d at %.0f,%.0f)\n", h.cx, h.cy, o.x, o.y,
               regionInfo(map.regionAt(o.x + 128, o.y + 128)).name, h.v, h.road, h.street, h.bld, bs.cellLists[(size_t)h.cy * cps + h.cx].size(), h.bmax, B.style,
               B.floors, B.c.x, B.c.y);
    }
    printf("%zu cells above 90k verts\n", hs.size());
    return 0;
}

int main(int argc, char** argv) {
    int perRegion = argc > 1 ? atoi(argv[1]) : 24;
    Jobs::init(2);
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    if (argc > 2) { heavyScan(map, roads, bs); Jobs::shutdown(); fflush(stdout); return 0; }
    int cps = kCellsPerSide;
    Region regs[] = {REG_DOWNTOWN, REG_FINANCIAL, REG_MIDTOWN, REG_NORTH_CITY, REG_CALLE_LUNA, REG_BEACH, REG_FLATS, REG_SUBURBS, REG_GROVE, REG_LAKE_TOWN};
    const char* names[] = {"downtown", "financial", "midtown", "north city", "calle luna", "beach", "flats", "suburbs", "grove", "lake town"};
    std::map<int, long long> typeTotals;
    printf("%-11s %4s | %7s %7s %8s %8s | %6s %6s %5s %5s | %6s %6s %6s\n", "region", "n", "t0 ms", "t0 max", "verts0", "vmax0", "props", "street", "lights", "coll", "t1 ms", "t1 max", "verts1");
    for (int ri = 0; ri < (int)(sizeof(regs) / sizeof(regs[0])); ri++) {
        std::vector<std::pair<int, int>> cellsR;
        for (int cy = 0; cy < cps; cy++)
            for (int cx = 0; cx < cps; cx++) {
                vec2 o = cellOrigin(cx, cy) + vec2(kCellSize * 0.5f);
                if (map.regionAt(o.x, o.y) == regs[ri]) cellsR.push_back({cx, cy});
            }
        if (cellsR.empty()) continue;
        int stride = Max(1, (int)cellsR.size() / perRegion);
        double tr0 = 0, tr1 = 0, t0 = 0, t0m = 0, t1 = 0, t1m = 0, v0 = 0, v0m = 0, v1 = 0, np = 0, nl = 0, nc = 0, ns = 0;
        int n = 0;
        for (size_t i = 0; i < cellsR.size(); i += stride) {
            int cx = cellsR[i].first, cy = cellsR[i].second;
            CellGeometry g0, g1;
            double a = cpuNow();
            generateCell(cx, cy, true, g0);
            double b = cpuNow();
            generateCell(cx, cy, false, g1);
            double c = cpuNow();
            RoadCellOutput ro, ro1;
            double r0 = cpuNow();
            buildRoadCell(roads, map, cx, cy, ro);
            double r1 = cpuNow();
            ro1.detail = false;
            buildRoadCell(roads, map, cx, cy, ro1);
            double r2 = cpuNow();
            tr0 += r1 - r0; tr1 += r2 - r1;
            ns += ro.street.verts.size();
            t0 += b - a; t0m = Max(t0m, b - a); t1 += c - b; t1m = Max(t1m, c - b);
            double vv = g0.opaque.verts.size() + g0.decals.verts.size();
            v0 += vv; v0m = Max(v0m, vv); v1 += g1.opaque.verts.size() + g1.decals.verts.size();
            np += g0.props.size(); nl += g0.lights.size(); nc += g0.collision.size();
            for (auto& p : g0.props) typeTotals[p.type]++;
            n++;
        }
        printf("   roads: detail %.2f ms, bare %.2f ms\n", tr0 / n * 1e3, tr1 / n * 1e3);
        printf("%-11s %4d | %7.2f %7.2f %8.0f %8.0f | %6.0f %6.0f %5.0f %5.0f | %6.2f %6.2f %6.0f\n", names[ri], n, t0 / n * 1e3, t0m * 1e3, v0 / n, v0m, np / n, ns / n, nl / n, nc / n,
               t1 / n * 1e3, t1m * 1e3, v1 / n);
    }
    printf("prop totals over the sampled cells:\n");
    for (auto& kv : typeTotals) printf("  type %2d: %lld\n", kv.first, kv.second);
    Jobs::shutdown();
    fflush(stdout);
    return 0;
}
