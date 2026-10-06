#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include <thread>
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    World::WorldMap m;
    double t0 = TimeSeconds();
    m.generate();
    World::RoadNetwork roads;
    roads.generate(m);
    World::BuildingSet bset;
    bset.generate(m, roads);
    printf("gen %.2f s\n", TimeSeconds() - t0);
    size_t pts = 0; double len[World::RC_COUNT] = {}; int cnt[World::RC_COUNT] = {};
    int named = 0;
    for (auto& e : roads.edges) { pts += e.pts.size(); len[e.cls] += e.length; cnt[e.cls]++; if (!e.name.empty()) named++; }
    printf("nodes %zu edges %zu pts %zu named %d\n", roads.nodes.size(), roads.edges.size(), pts, named);
    for (int c = 0; c < World::RC_COUNT; c++) printf("  %s: %d edges, %.1f km\n", World::roadInfo((World::RoadClass)c).name, cnt[c], len[c] / 1000.0);
    printf("buildings %zu\n", bset.buildings.size());
    printf("mainland %zu solBeach %zu keyCoral %zu portIsle %zu islands %zu lake %zu\n", m.mainland.size(), m.solBeach.size(), m.keyCoral.size(), m.portIsle.size(), m.smallIslands.size(), m.lakePoly.size());
    // region bounding boxes / centroids
    double sx[World::REG_COUNT] = {}, sy[World::REG_COUNT] = {}; int n[World::REG_COUNT] = {};
    float mnx[World::REG_COUNT], mny[World::REG_COUNT], mxx[World::REG_COUNT], mxy[World::REG_COUNT];
    for (int r = 0; r < World::REG_COUNT; r++) { mnx[r] = mny[r] = 1e9; mxx[r] = mxy[r] = -1e9; }
    for (int y = 0; y < World::kHeightRes; y += 4) for (int x = 0; x < World::kHeightRes; x += 4) {
        int r = m.region[y * World::kHeightRes + x];
        float wx = World::texelToWorld(x), wy = World::texelToWorld(y);
        if (m.isWater(wx, wy)) continue;
        sx[r] += wx; sy[r] += wy; n[r]++;
        mnx[r] = Min(mnx[r], wx); mny[r] = Min(mny[r], wy); mxx[r] = Max(mxx[r], wx); mxy[r] = Max(mxy[r], wy);
    }
    for (int r = 0; r < World::REG_COUNT; r++) if (n[r]) printf("  region %2d %-26s c=(%6.0f,%6.0f) area=%.1f km2 bbox=(%.0f,%.0f)-(%.0f,%.0f)\n", r, World::regionInfo((World::Region)r).name, sx[r]/n[r], sy[r]/n[r], n[r]*32.0*32.0/1e6, mnx[r], mny[r], mxx[r], mxy[r]);
    // height stats
    float hmin = 1e9, hmax = -1e9; float wmin = 1e9, wmax=-1e9; int noW = 0;
    for (size_t i = 0; i < m.height.size(); i++) { hmin = Min(hmin, m.height[i]); hmax = Max(hmax, m.height[i]); if (m.waterLevel[i] <= World::kNoWater + 1) noW++; else { wmin = Min(wmin, m.waterLevel[i]); wmax = Max(wmax, m.waterLevel[i]); } }
    printf("height %.1f..%.1f water %.1f..%.1f noWater %d of %zu\n", hmin, hmax, wmin, wmax, noW, m.height.size());
    // name samples
    int k = 0; for (auto& e : roads.edges) { if (k++ % 800 == 0) printf("  edge %d %s cls %d flags %d len %.0f pts %zu hw %.1f\n", k, e.name.c_str(), e.cls, e.flags, e.length, e.pts.size(), e.halfWidth); }
    Jobs::shutdown();
}
