#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
namespace World {
bool siteColliderNear(vec3, float, float) { return false; }
void transitLayout(SiteSet&, WorldMap&) {}
void transitFinalize(SiteSet&, WorldMap&, const RoadNetwork&, const BuildingSet&) {}
bool BuildingSet::pointInBuilding(vec2, float, float*) const { return false; }
}
using namespace World;
int main() {
    Jobs::init(3);
    WorldMap map; map.generate(); gMap = &map;
    double best = 1e9;
    for (int k = 0; k < 3; k++) {
        WorldMap m2 = map;
        RoadNetwork roads;
        double t0 = TimeSeconds();
        roads.generate(m2);
        double t = TimeSeconds() - t0;
        best = t < best ? t : best;
        printf("run %d: %.2f s (%zu edges)\n", k, t, roads.edges.size());
        fflush(stdout);
    }
    printf("best %.2f s\n", best);
    fflush(stdout);
    Jobs::shutdown();
}
