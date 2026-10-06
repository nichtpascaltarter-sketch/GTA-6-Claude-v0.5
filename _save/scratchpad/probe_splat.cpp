#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include <thread>
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    World::WorldMap m;
    m.generate();
    int res = World::kHeightRes;
    float half = World::kWorldHalf;
    for (int i = 1; i + 1 < argc; i += 2) {
        float x = atof(argv[i]), y = atof(argv[i + 1]);
        int px = (int)((x + half) / (2 * half) * res), py = (int)((y + half) / (2 * half) * res);
        u32 a = m.splat0[py * res + px], b = m.splat1[py * res + px];
        printf("(%g,%g) h=%.2f splat0 %d %d %d %d splat1 %d %d %d %d\n", x, y, m.heightAt(x, y), a & 255, (a >> 8) & 255, (a >> 16) & 255, a >> 24, b & 255, (b >> 8) & 255, (b >> 16) & 255, b >> 24);
    }
    _Exit(0);
}
