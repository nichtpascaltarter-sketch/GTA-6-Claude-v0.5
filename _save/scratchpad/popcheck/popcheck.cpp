#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include <thread>
int main() {
    Jobs::init(2);
    World::WorldMap m; m.generate(); World::gMap = &m;
    World::RoadNetwork roads; roads.generate(m);
    vec2 centers[3] = {vec2(1520, 2320), vec2(1780, 160), vec2(3200, 300)};
    for (vec2 center : centers) {
        int ok = 0, noEdge = 0, noSidewalk = 0, badS = 0;
        u32 cnt = 1;
        for (int i = 0; i < 400; i++) {
            u32 h = hash32(cnt++ * 2654435761u);
            float ang = hashToFloat(h) * kTwoPi;
            float r = 40.f + hashToFloat(hash32(h)) * 75.f;
            vec2 probe = center + vec2(cosf(ang), sinf(ang)) * r;
            float s = 0, side = 0;
            int e = roads.nearestEdge(probe, 40.f, &s, nullptr, &side);
            if (e < 0) { noEdge++; continue; }
            const World::RoadEdge& ed = roads.edges[e];
            if (ed.sidewalk <= 0.f) { noSidewalk++; continue; }
            if (s < ed.cut0 + 2.f || s > ed.length - ed.cut1 - 2.f) { badS++; continue; }
            ok++;
        }
        printf("center %.0f,%.0f region %s: ok %d noEdge %d noSidewalk %d badS %d\n", center.x, center.y,
               World::regionInfo(m.regionAt(center.x, center.y)).name, ok, noEdge, noSidewalk, badS); fflush(stdout);
    }
    Jobs::shutdown();
}
