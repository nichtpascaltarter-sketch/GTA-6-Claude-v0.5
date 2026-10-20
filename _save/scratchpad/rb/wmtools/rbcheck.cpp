// rbcheck (city scratch, not for the repo): reads a world.bin's roads sections and compares them with compileRoads on a
// world directory: the worldc path end to end. Build from the tree root: g++ -std=c++17 -O1 -I. wmtools/rbcheck.cpp
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include "src/world/roadnet.cpp"
#include "src/world/worldmodel.cpp"
#include "src/world/worldmodel_roads.cpp"
#include "src/world/lz.cpp"
#include "src/world/worldbin.cpp"
#include "src/world/roadsbin.cpp"
using namespace World;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
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
    RoadNetwork back;
    std::vector<std::string> nodeIds, edgeIds;
    if (!readRoads(view, back, &nodeIds, &edgeIds, &why)) { printf("readRoads: %s\n", why.c_str()); return 1; }
    Hand::Model m;
    std::vector<Hand::Error> errs;
    // the kinds worldc compiles: load all, then keep the compile to what worldc does (it refuses the others)
    Hand::load(argv[2], m, errs);
    RoadNetwork rn;
    Hand::RoadIndex ix;
    Hand::compileRoads(m, rn, ix, errs);
    bool same = rn.nodes.size() == back.nodes.size() && rn.edges.size() == back.edges.size() && rn.hash == back.hash;
    for (size_t i = 0; same && i < rn.edges.size(); i++) {
        const RoadEdge &a = rn.edges[i], &b = back.edges[i];
        same = a.n0 == b.n0 && a.n1 == b.n1 && a.pts.size() == b.pts.size() && a.dist == b.dist && a.length == b.length && a.seed == b.seed && a.halfWidth == b.halfWidth &&
               a.cut0 == b.cut0 && a.cut1 == b.cut1 && a.name == b.name && a.flags == b.flags;
        for (size_t k = 0; same && k < a.pts.size(); k++) same = a.pts[k].x == b.pts[k].x && a.pts[k].y == b.pts[k].y && a.pts[k].z == b.pts[k].z;
    }
    for (size_t i = 0; same && i < rn.nodes.size(); i++)
        same = rn.nodes[i].radius == back.nodes[i].radius && rn.nodes[i].control == back.nodes[i].control && rn.nodes[i].edges == back.nodes[i].edges &&
               nodeIds[i] == m.nodes[(size_t)ix.nodeOf[i]].id;
    printf("world.bin %zu sections; roads %zu nodes, %zu edges, ids %zu/%zu; compileRoads on %s: %zu nodes, %zu edges; %s\n", view.sections.size(), back.nodes.size(),
           back.edges.size(), nodeIds.size(), edgeIds.size(), argv[2], rn.nodes.size(), rn.edges.size(), same ? "IDENTICAL" : "DIFFERENT");
    return same ? 0 : 1;
}
