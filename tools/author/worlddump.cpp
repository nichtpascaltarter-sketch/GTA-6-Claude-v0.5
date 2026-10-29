// The inputs of tools/author/dress.py, from the compiled world and the baked terrain: a host tool, as the authoring
// scripts are (it never ships). Build from the tree root: g++ -std=c++17 -O1 -I. tools/author/worlddump.cpp -o worlddump
//   worlddump roads <world.bin>              the road network as the hand world reads it (roadsbin.h, with each side's
//                                            kerb line and sidewalk from the overrides): an N line per node (id, x, y, z,
//                                            radius, control, highway, degree, its dead end's turning circle) and an E
//                                            line per edge (id, nodes, class, flags, lanes, half width, sidewalk, cut-backs,
//                                            length, each side's kerb line and sidewalk, left first looking along it,
//                                            |name|, its points)
//   worlddump grids <terrain dir> <out dir>  the baked height, water and region grids (world/terrain/*.ntg) as raw
//                                            little-endian i16 files, for the river and the banks
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/world/roadnet.cpp"
#include "src/world/lz.cpp"
#include "src/world/worldbin.cpp"
#include "src/world/roadsbin.cpp"
#include "src/world/gridcodec.cpp"
#include <cstdio>
#include <string>
#include <vector>
using namespace World;

static bool readAll(const std::string& p, std::vector<u8>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    u8 buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

static int roads(const char* path) {
    std::vector<u8> bin;
    if (!readAll(path, bin)) return fprintf(stderr, "worlddump: can't read %s\n", path), 1;
    WorldBinView view;
    std::string why;
    if (!view.open(bin.data(), bin.size(), &why)) return fprintf(stderr, "worlddump: %s\n", why.c_str()), 1;
    static RoadNetwork rn;
    static RoadOverrides ov;
    std::vector<std::string> nodeIds, edgeIds;
    if (!readRoads(view, rn, &nodeIds, &edgeIds, &why) || !readRoadOverrides(view, rn, ov, &why)) return fprintf(stderr, "worlddump: %s\n", why.c_str()), 1;
    applyRoadSides(ov, rn);
    for (size_t i = 0; i < rn.nodes.size(); i++) {
        const RoadNode& nd = rn.nodes[i];
        printf("N %s %.2f %.2f %.2f %.2f %d %d %zu %.2f\n", nodeIds.empty() ? "-" : nodeIds[i].c_str(), nd.p.x, nd.p.y, nd.z, nd.radius, (int)nd.control,
               nd.highway ? 1 : 0, nd.edges.size(), rn.bulbRadius(nd));
    }
    for (size_t i = 0; i < rn.edges.size(); i++) {
        const RoadEdge& e = rn.edges[i];
        printf("E %s %d %d %d %d %d %d %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f |%s|", edgeIds.empty() ? "-" : edgeIds[i].c_str(), e.n0, e.n1, (int)e.cls,
               (int)e.flags, (int)e.lanesF, (int)e.lanesB, e.halfWidth, e.sidewalk, e.cut0, e.cut1, e.length, e.halfWidthSide(-1), e.halfWidthSide(1),
               e.sidewalkSide(-1), e.sidewalkSide(1), e.name.c_str());
        for (const vec3& p : e.pts) printf(" %.2f,%.2f,%.2f", p.x, p.y, p.z);
        printf("\n");
    }
    return 0;
}

static int grids(const char* dir, const char* outDir) {
    for (const char* name : {"height", "water", "region"}) {
        std::vector<u8> bytes;
        std::vector<i16> v;
        GridInfo gi;
        if (!readAll(std::string(dir) + "/" + name + ".ntg", bytes) || !decodeGrid(bytes.data(), bytes.size(), v, &gi))
            return fprintf(stderr, "worlddump: %s/%s.ntg doesn't decode\n", dir, name), 1;
        FILE* o = fopen((std::string(outDir) + "/" + name + ".i16").c_str(), "wb");
        if (!o || fwrite(v.data(), 2, v.size(), o) != v.size()) return fprintf(stderr, "worlddump: can't write %s.i16\n", name), 1;
        fclose(o);
        printf("%s %dx%d\n", name, gi.width, gi.height);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "roads") return roads(argv[2]);
    if (argc >= 4 && std::string(argv[1]) == "grids") return grids(argv[2], argv[3]);
    fprintf(stderr, "usage: worlddump roads <world.bin> | worlddump grids <terrain dir> <out dir>\n");
    return 2;
}
