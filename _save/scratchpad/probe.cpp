#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include <thread>
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    World::WorldMap m;
    m.generate();
    World::RoadNetwork roads;
    roads.generate(m);
    for (int i = 1; i + 1 < argc; i += 2) {
        float x = atof(argv[i]), y = atof(argv[i + 1]);
        float z = 0; bool onRoad = roads.surfaceHeight(vec2(x, y), &z);
        float s, d, side; int e = roads.nearestEdge(vec2(x, y), 200.f, &s, &d, &side);
        printf("(%g,%g) h=%.2f water=%.2f region=%s road=%d z=%.2f nearestEdge=%d dist=%.1f cls=%d name=%s\n", x, y, m.heightAt(x, y), m.waterAt(x, y),
               World::regionInfo(m.regionAt(x, y)).name, onRoad, z, e, d, e >= 0 ? roads.edges[e].cls : -1, e >= 0 ? roads.edges[e].name.c_str() : "");
        fflush(stdout);
    }

    fflush(stdout);
    _Exit(0);
}
