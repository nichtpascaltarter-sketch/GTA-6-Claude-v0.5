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
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap m; m.generate(); gMap = &m;
    int res = kHeightRes;
    float half = kWorldHalf;
    for (int i = 1; i + 1 < argc; i += 2) {
        float x = atof(argv[i]), y = atof(argv[i + 1]);
        int px = (int)((x + half) / (2 * half) * res), py = (int)((y + half) / (2 * half) * res);
        u32 a = m.splat0[py * res + px], b = m.splat1[py * res + px];
        printf("(%g,%g) h=%.2f splat0 %d %d %d %d splat1 %d %d %d %d\n", x, y, m.heightAt(x, y), a & 255, (a >> 8) & 255, (a >> 16) & 255, a >> 24, b & 255, (b >> 8) & 255, (b >> 16) & 255, b >> 24);
    }
    fflush(stdout);
    _exit(0);
}
