#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/buildings.cpp"
#include <thread>
#include <map>
using namespace World;
int main() {
    Jobs::init(2);
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    std::map<int, int> cnt;
    for (auto& e : gSites->elems) cnt[e.kind]++;
    for (auto& kv : cnt) printf("kind %d: %d\n", kv.first, kv.second);
    printf("pads %zu, farCells %zu\n", gSites->pads.size(), gSites->farCells.size());
    for (auto& e : gSites->elems) if (e.kind == SK_BOAT || e.kind == SK_MARINA || e.kind == SK_GOLF_HOLE || e.kind == SK_CLUBHOUSE || e.kind == SK_RIVER_MARINA) printf("k%d at (%.0f,%.0f) p0 %.1f p1 %.1f text %s\n", e.kind, e.c.x, e.c.y, e.p[0], e.p[1], e.text.c_str());
    Jobs::shutdown();
}
