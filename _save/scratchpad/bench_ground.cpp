#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roadmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/propmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/cellgen.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/sim/physics.cpp"
#include <thread>
#include <chrono>
int main() {
    Jobs::init(3);
    double t0 = TimeSeconds();
    static World::WorldMap m; m.generate(); World::gMap = &m;
    static World::RoadNetwork roads; roads.generate(m); World::gRoads = &roads;
    static World::BuildingSet bs; bs.generate(m, roads); World::gBuildings = &bs;
    printf("gen %.1f s\n", TimeSeconds() - t0);
    static Phys::CollisionWorld cw; Phys::gCollision = &cw;
    // pick a city road edge
    int ei = -1;
    // densest building cell
    int bestC = 0; size_t bestN = 0;
    for (size_t c = 0; c < bs.cellLists.size(); c++) if (bs.cellLists[c].size() > bestN) { bestN = bs.cellLists[c].size(); bestC = (int)c; }
    vec2 dc = World::cellOrigin(bestC % World::kCellsPerSide, bestC / World::kCellsPerSide) + vec2(128.f);
    printf("densest cell %d (%zu buildings) at %.0f %.0f\n", bestC, bestN, dc.x, dc.y);
    float bd = 1e9f;
    for (size_t i = 0; i < roads.edges.size(); i++) { auto& ee = roads.edges[i]; if (ee.cls > World::RC_STREET || ee.length < 150) continue; float d = length(ee.posAt(ee.length*0.5f).xy() - dc); if (d < bd) { bd = d; ei = (int)i; } }
    auto& e = roads.edges[ei];
    vec3 p0 = e.posAt(e.length * 0.5f);
    printf("edge %d cls %d len %.0f at %.1f %.1f %.1f\n", ei, e.cls, e.length, p0.x, p0.y, p0.z);
    t0 = TimeSeconds();
    int ccx = (int)((p0.x + World::kWorldHalf) / World::kCellSize), ccy = (int)((p0.y + World::kWorldHalf) / World::kCellSize);
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        World::CellGeometry g; World::generateCell(ccx + dx, ccy + dy, true, g);
        cw.addCell((ccy+dy) * 1000 + ccx + dx, g.collision, g.props);
    }
    printf("cells %.2f s, colliders %d\n", TimeSeconds() - t0, cw.colliderCount());
    // time ground queries near p0
    Rng r(1);
    std::vector<vec2> pts;
    for (int i = 0; i < 20000; i++) { float s = r.range(0.f, e.length); vec3 q = e.posAt(s); pts.push_back(q.xy() + vec2(r.range(-5.f,5.f), r.range(-5.f,5.f))); }
    auto c0 = std::chrono::high_resolution_clock::now();
    double acc = 0;
    for (int rep = 0; rep < 5; rep++)
    for (auto& q : pts) { Phys::GroundHit g = cw.ground(q.x, q.y, 50.f, 0.5f); acc += g.z; }
    auto c1 = std::chrono::high_resolution_clock::now();
    printf("ground(): %.3f us/call (acc %f)\n", std::chrono::duration<double, std::micro>(c1 - c0).count() / (5 * pts.size()), acc);
    c0 = std::chrono::high_resolution_clock::now();
    for (int rep = 0; rep < 5; rep++)
    for (auto& q : pts) { acc += m.heightAt(q.x, q.y); }
    c1 = std::chrono::high_resolution_clock::now();
    printf("heightAt(): %.3f us/call\n", std::chrono::duration<double, std::micro>(c1 - c0).count() / (5 * pts.size()));
    c0 = std::chrono::high_resolution_clock::now();
    for (int rep = 0; rep < 5; rep++)
    for (auto& q : pts) { float z; acc += roads.surfaceHeight(q, &z, 60.f) ? z : 0; }
    c1 = std::chrono::high_resolution_clock::now();
    printf("surfaceHeight(): %.3f us/call\n", std::chrono::duration<double, std::micro>(c1 - c0).count() / (5 * pts.size()));
    std::vector<Phys::CollisionWorld::Contact> cts;
    mat3 I;
    c0 = std::chrono::high_resolution_clock::now();
    for (int rep = 0; rep < 5; rep++)
    for (auto& q : pts) { cw.boxContacts(vec3(q, 1.f), I, vec3(0.9f, 2.3f, 0.7f), cts); acc += cts.size(); }
    c1 = std::chrono::high_resolution_clock::now();
    printf("boxContacts(): %.3f us/call\n", std::chrono::duration<double, std::micro>(c1 - c0).count() / (5 * pts.size()));
    c0 = std::chrono::high_resolution_clock::now();
    for (int rep = 0; rep < 5; rep++)
    for (auto& q : pts) { float z; acc += Phys::waterSurface(q.x, q.y, z) ? z : 0; }
    c1 = std::chrono::high_resolution_clock::now();
    printf("waterSurface(): %.3f us/call\n", std::chrono::duration<double, std::micro>(c1 - c0).count() / (5 * pts.size()));
    printf("%f\n", acc);
    Jobs::shutdown();
}
