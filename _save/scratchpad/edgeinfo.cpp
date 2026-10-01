#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/buildings.cpp"
using namespace World;
int main(int argc, char** argv) {
    Jobs::init(2);
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map);
    for (int i = 1; i < argc; i++) {
        int ei = atoi(argv[i]);
        const RoadEdge& e = roads.edges[ei];
        printf("edge %d %s '%s' len %.0f hw %.1f sw %.1f flags %d cut %.0f/%.0f\n", ei, roadInfo(e.cls).name, e.name.c_str(), e.length, e.halfWidth, e.sidewalk, e.flags, e.cut0, e.cut1);
        for (float s = 0; s <= e.length; s += 20.f) {
            vec3 p = e.posAt(s);
            printf("  s %4.0f (%.1f, %.1f, %.1f) ground %.1f water %.1f\n", s, p.x, p.y, p.z, map.heightAt(p.x, p.y), map.waterAt(p.x, p.y));
        }
    }
    Jobs::shutdown();
}
