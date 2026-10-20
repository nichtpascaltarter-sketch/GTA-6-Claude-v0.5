// rbdump (city scratch, not for the repo): prints a world.bin's road network as the game reads it (roadsbin.h), for the
// pilot's check map. Build from the tree root: g++ -std=c++17 -O1 -I. wmtools/rbdump.cpp
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/world/roadnet.cpp"
#include "src/world/lz.cpp"
#include "src/world/worldbin.cpp"
#include "src/world/roadsbin.cpp"
using namespace World;
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 3;
    std::vector<u8> bin;
    u8 buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bin.insert(bin.end(), buf, buf + n);
    fclose(f);
    WorldBinView view;
    std::string why;
    if (!view.open(bin.data(), bin.size(), &why)) { printf("open: %s\n", why.c_str()); return 1; }
    RoadNetwork rn;
    std::vector<std::string> nodeIds, edgeIds;
    if (!readRoads(view, rn, &nodeIds, &edgeIds, &why)) { printf("readRoads: %s\n", why.c_str()); return 1; }
    for (size_t i = 0; i < rn.nodes.size(); i++) {
        const RoadNode& nd = rn.nodes[i];
        printf("N %s %.2f %.2f %.2f %.2f %d %d %zu\n", nodeIds.empty() ? "-" : nodeIds[i].c_str(), nd.p.x, nd.p.y, nd.z, nd.radius, (int)nd.control, nd.highway ? 1 : 0,
               nd.edges.size());
    }
    for (size_t i = 0; i < rn.edges.size(); i++) {
        const RoadEdge& e = rn.edges[i];
        printf("E %s %d %d %d %d %d %d %.2f %.2f %.2f %.2f %.2f |%s|", edgeIds.empty() ? "-" : edgeIds[i].c_str(), e.n0, e.n1, (int)e.cls, (int)e.flags, (int)e.lanesF,
               (int)e.lanesB, e.halfWidth, e.sidewalk, e.cut0, e.cut1, e.length, e.name.c_str());
        for (const vec3& p : e.pts) printf(" %.2f,%.2f,%.2f", p.x, p.y, p.z);
        printf("\n");
    }
    return 0;
}
