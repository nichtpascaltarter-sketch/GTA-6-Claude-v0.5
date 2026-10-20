// Native probe: buildings around a point (style, footprint, front) after the full world generation.
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roadmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/propmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/cellgen.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitegeo.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/airport.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/port.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/landmarks.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/leisure.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/rural.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitecell.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/facadedetail.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorkit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorlayouts.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiors.cpp"
#include <thread>
#include <unistd.h>
#include <cstring>
using namespace World;
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap m; m.generate(); gMap = &m;
    RoadNetwork roads; roads.generate(m); gRoads = &roads;
    BuildingSet bset; bset.generate(m, roads); gBuildings = &bset;
    static const char* names[] = {"TOWER","MIDRISE","CONDO","DECO","SHOPS","STRIPMALL","HOUSE","VILLA","WAREHOUSE","FACTORY","FARMHOUSE","BARN","MOTEL","GAS","GARAGE","CHURCH","SHACK"};
    for (int a = 1; a + 2 < argc; a += 3) {
        vec2 p((float)atof(argv[a]), (float)atof(argv[a + 1]));
        float r = (float)atof(argv[a + 2]);
        std::vector<int> nb;
        bset.buildingsNear(p, r, nb);
        printf("== (%.0f, %.0f) r %.0f: %d buildings, region %d\n", p.x, p.y, r, (int)nb.size(), (int)m.regionAt(p.x, p.y));
        for (int i : nb) {
            const Building& b = bset.buildings[i];
            vec2 fc = b.c + b.front * b.hy;
            printf("  #%d %-9s c (%.0f, %.0f) half %.0fx%.0f h %.0f front (%.2f, %.2f) facade (%.0f, %.0f) dist %.0f\n", i, names[b.style % 17], b.c.x, b.c.y, b.hx, b.hy,
                   b.height, b.front.x, b.front.y, fc.x, fc.y, length(fc - p));
        }
    }
    fflush(stdout);
    _exit(0);
}
