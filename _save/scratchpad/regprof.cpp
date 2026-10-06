// Per-region LOD0/LOD1 cell generation cost (thread CPU time, best of 2) and vertex/triangle counts for all land cells.
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
#define buildFacadeDetail buildFacadeDetail_hook
#include "src/world/buildmesh.cpp"
#undef buildFacadeDetail
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
namespace World {
bool gFD = true;
void buildFacadeDetail_hook(const Building& b, const FacadeGPU& fac, const WorldMap& map, vec3 org, MeshData& m, std::vector<CollisionBox>* col,
                            std::vector<PropInstance>* props, std::vector<LightInstance>* lights, const std::vector<FacadeMass>& masses) {
    if (gFD) buildFacadeDetail(b, fac, map, org, m, col, props, lights, masses);
}
}
#include <thread>
#include <time.h>
#include <map>
using namespace World;
static double cpuNow() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
struct Agg { double b0 = 0, bv0 = 0; int n = 0; double t0 = 0, t0max = 0, t1 = 0, t1max = 0; double v0 = 0, v0max = 0, tri0 = 0, v1 = 0; int worst = -1; int props = 0; };
int main(int argc, char** argv) {
    Jobs::init(2);
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    int cps = kCellsPerSide;
    int stride = argc > 1 ? atoi(argv[1]) : 1;
    std::map<int, Agg> agg;
    for (int cy = 0; cy < cps; cy += 1)
        for (int cx = 0; cx < cps; cx += 1) {
            if (((cx + cy) % stride) != 0) continue;
            if (bs.cellLists[(size_t)cy * cps + cx].size() < 3) continue;
            vec2 c = cellOrigin(cx, cy) + vec2(128.f);
            int reg = (int)map.regionAt(c.x, c.y);
            Agg& a = agg[reg];
            double best[2] = {1e9, 1e9};
            size_t nv[2] = {0, 0}, ni0 = 0, np = 0;
            for (int d = 0; d < 2; d++)
                for (int rep = 0; rep < 2; rep++) {
                    CellGeometry geo;
                    double t = cpuNow();
                    generateCell(cx, cy, d == 0, geo);
                    best[d] = Min(best[d], cpuNow() - t);
                    nv[d] = geo.opaque.verts.size() + geo.decals.verts.size();
                    if (d == 0) { ni0 = geo.opaque.indices.size() + geo.decals.indices.size(); np = geo.props.size(); }
                }
            {
                gFD = false;
                double bb = 1e9; size_t bnv = 0;
                for (int rep = 0; rep < 2; rep++) {
                    CellGeometry geo;
                    double t = cpuNow();
                    generateCell(cx, cy, true, geo);
                    bb = Min(bb, cpuNow() - t);
                    bnv = geo.opaque.verts.size() + geo.decals.verts.size();
                }
                gFD = true;
                a.b0 += bb * 1000; a.bv0 += bnv;
            }
            a.n++;
            a.t0 += best[0] * 1000; a.t1 += best[1] * 1000;
            if (best[0] * 1000 > a.t0max) { a.t0max = best[0] * 1000; a.worst = cy * cps + cx; }
            a.t1max = Max(a.t1max, best[1] * 1000);
            a.v0 += nv[0]; a.v0max = Max(a.v0max, (double)nv[0]); a.tri0 += ni0 / 3; a.v1 += nv[1]; a.props += (int)np;
        }
    printf("%-14s %5s | %8s %8s %8s | %9s %9s %9s %9s | %8s %8s | %7s\n", "region", "cells", "base ms", "LOD0 ms", "max", "base vrt", "avg verts", "max verts", "avg tris", "LOD1 ms", "max", "LOD1 v");
    for (auto& kv : agg) {
        Agg& a = kv.second;
        if (!a.n) continue;
        vec2 w = a.worst >= 0 ? cellOrigin(a.worst % cps, a.worst / cps) + vec2(128.f) : vec2(0);
        printf("%-14s %5d | %8.2f %8.2f %8.2f | %9.0f %9.0f %9.0f %9.0f | %8.2f %8.2f | %7.0f  worst (%.0f,%.0f) props/cell %d\n", regionInfo((Region)kv.first).name, a.n,
               a.b0 / a.n, a.t0 / a.n, a.t0max, a.bv0 / a.n, a.v0 / a.n, a.v0max, a.tri0 / a.n, a.t1 / a.n, a.t1max, a.v1 / a.n, w.x, w.y, a.props / a.n);
    }
    Jobs::shutdown();
}
