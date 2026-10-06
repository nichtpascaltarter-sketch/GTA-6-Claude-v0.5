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
#include "/home/user/GTA-6-Claude-v0.5/src/world/transit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transitmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitecell.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/facadedetail.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorkit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorfurniture.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorlayouts.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorhomes.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorvenues.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorshops.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorcivic.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorindustrial.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiortower.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorgarages.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorresidences.cpp"
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
    vec2 p((float)atof(argv[1]), (float)atof(argv[2]));
    float r = (float)atof(argv[3]);
    int cx0 = (int)floorf((p.x + kWorldHalf) / kCellSize), cy0 = (int)floorf((p.y + kWorldHalf) / kCellSize);
    for (int cy = cy0 - 1; cy <= cy0 + 1; cy++)
        for (int cx = cx0 - 1; cx <= cx0 + 1; cx++) {
            CellGeometry g;
            generateCell(cx, cy, true, g);
            for (const LightInstance& li : g.lights) {
                float d = length(vec2(li.pos.x, li.pos.y) - p);
                if (d > r) continue;
                printf("cell %d,%d type %d pos (%.1f, %.1f, %.2f) col (%.2f, %.2f, %.2f) rad %.1f dir (%.2f,%.2f,%.2f) cone %.2f dist %.1f water %.2f ground %.2f\n", cx, cy, li.type,
                       li.pos.x, li.pos.y, li.pos.z, li.color.x, li.color.y, li.color.z, li.radius, li.dir.x, li.dir.y, li.dir.z, li.cone, d, m.waterAt(li.pos.x, li.pos.y), m.heightAt(li.pos.x, li.pos.y));
            }
        }
    fflush(stdout);
    _exit(0);
}
