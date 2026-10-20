// rfcheck: a census of the generated world for the road-overlap fix (city agent's scratch tool, not for the repo).
// Builds the world as the game does (map, roads, buildings with interiors, sites, transit) and prints the road network,
// every edge, every building, the bus stops, the site places and the story places (mission_util.cpp's resolvers,
// replicated here on the world data), one line each, for a diff between two roads.cpp versions.
// Build from a tree root: g++ -O2 -std=c++17 -I. -I src [-DRF_BASE] rfcheck.cpp -o rfcheck -lpthread
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#ifdef RF_BASE
#include "src/world/roads_base.cpp"
#else
#include "src/world/roads.cpp"
#endif
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sites.cpp"
#include "src/world/transit.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
#include "src/world/transitmesh.cpp"
#include "src/world/sitecell.cpp"
#include "src/world/facadedetail.cpp"
#include "src/world/interiorkit.cpp"
#include "src/world/interiorfurniture.cpp"
#include "src/world/interiorlayouts.cpp"
#include "src/world/interiorhomes.cpp"
#include "src/world/interiorvenues.cpp"
#include "src/world/interiorshops.cpp"
#include "src/world/interiorcivic.cpp"
#include "src/world/interiorindustrial.cpp"
#include "src/world/interiortower.cpp"
#include "src/world/interiorgarages.cpp"
#include "src/world/interiorresidences.cpp"
#include "src/world/interiors.cpp"
#include <thread>
#include <cstdio>

using namespace World;

// rfdump: the road network around given points (city agent's scratch tool): every edge with a point within r of (x, y),
// with its polyline, nodes, cut-backs and width, and the nodes there with their degree and control.
// Usage: rfdump x y r [x y r ...]
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    static WorldMap map;
    map.generate();
    gMap = &map;
    static RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    printf("N nodes %zu edges %zu\n", roads.nodes.size(), roads.edges.size());
    for (int a = 1; a + 2 < argc; a += 3) {
        vec2 c((float)atof(argv[a]), (float)atof(argv[a + 1]));
        float r = (float)atof(argv[a + 2]);
        printf("== around %.0f %.0f r %.0f\n", c.x, c.y, r);
        for (size_t i = 0; i < roads.nodes.size(); i++) {
            const RoadNode& n = roads.nodes[i];
            if (length(n.p - c) > r) continue;
            printf("V %zu %.1f %.1f z %.1f deg %zu ctl %d rad %.1f edges", i, n.p.x, n.p.y, n.z, n.edges.size(), (int)n.control, n.radius);
            for (int e : n.edges) printf(" %d", e);
            printf("\n");
        }
        for (size_t i = 0; i < roads.edges.size(); i++) {
            const RoadEdge& e = roads.edges[i];
            bool near = false;
            for (size_t k = 0; k + 1 < e.pts.size() && !near; k++) near = distPointSegment2D(c, e.pts[k].xy(), e.pts[k + 1].xy()) <= r;
            if (!near) continue;
            printf("E %zu %s|cls %d fl %x|n %d-%d|len %.1f cut %.1f %.1f|hw %.1f sw %.1f|lanes %d/%d|seed %08x|pts", i, e.name.c_str(), (int)e.cls, e.flags, e.n0, e.n1,
                   e.length, e.cut0, e.cut1, e.halfWidth, e.sidewalk, e.lanesF, e.lanesB, e.seed);
            for (const vec3& p : e.pts) printf(" %.1f,%.1f,%.1f", p.x, p.y, p.z);
            printf("\n");
        }
    }
    fflush(stdout);
    Jobs::shutdown();
    return 0;
}
