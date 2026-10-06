// scratch tool: terrain heights at the vehicle viewer slots (after roads/buildings flatten the terrain)
#include "native_stubs.cpp"
#include "../src/core/math.cpp"
#include "../src/core/noise.cpp"
#include "../src/core/jobs.cpp"
#include "../src/world/worldmap.cpp"
#include "../src/world/roads.cpp"
#include "../src/world/buildings.cpp"
#include <thread>
#include <unistd.h>
int main() {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    World::WorldMap m;
    m.generate();
    World::RoadNetwork roads;
    roads.generate(m);
    World::BuildingSet bset;
    bset.generate(m, roads);
    for (int i = 0; i < 48; i++) printf("%d %.3f\n", i, m.heightAt(-300.f + 8.f * i, 1500.f));
    fflush(stdout);
    _exit(0);
}
