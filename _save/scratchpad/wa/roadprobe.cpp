// Road-node probe: prints nodes / edges around given nodes and the props near given points (dev aid)
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
#include "src/world/transit.cpp"
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
using namespace World;
static const char* cn(int c) { return roadInfo((RoadClass)c).name; }
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    for (int a = 1; a < argc; a++) {
        if (argv[a][0] == 'n') {
            int ni = atoi(argv[a] + 1);
            const RoadNode& n = roads.nodes[ni];
            printf("node %d at (%.1f, %.1f, %.2f) control %d highway %d radius %.1f, %zu edges\n", ni, n.p.x, n.p.y, n.z, n.control, n.highway, n.radius, n.edges.size());
            for (int ei : n.edges) {
                const RoadEdge& e = roads.edges[ei];
                int other = e.n0 == ni ? e.n1 : e.n0;
                vec3 far = e.n0 == ni ? e.posAt(Min(20.f, e.length)) : e.posAt(Max(0.f, e.length - 20.f));
                printf("  edge %d %s '%s' n0 %d n1 %d len %.1f lanes F%d B%d hw %.1f sw %.1f cut %.1f/%.1f flags %d -> node %d at (%.1f,%.1f); dir from node (%.2f,%.2f)\n", ei,
                       cn(e.cls), e.name.c_str(), e.n0, e.n1, e.length, e.lanesF, e.lanesB, e.halfWidth, e.sidewalk, e.cut0, e.cut1, e.flags, other,
                       roads.nodes[other].p.x, roads.nodes[other].p.y, normalize(far.xy() - n.p).x, normalize(far.xy() - n.p).y);
            }
        } else if (argv[a][0] == 'q') {
            // nodes near a point
            float x, y, r;
            sscanf(argv[a] + 1, "%f,%f,%f", &x, &y, &r);
            for (size_t ni = 0; ni < roads.nodes.size(); ni++)
                if (length(roads.nodes[ni].p - vec2(x, y)) < r) {
                    const RoadNode& n = roads.nodes[ni];
                    printf("node %zu at (%.1f, %.1f) edges %zu control %d\n", ni, n.p.x, n.p.y, n.edges.size(), n.control);
                    for (int ei : n.edges) {
                        const RoadEdge& e = roads.edges[ei];
                        printf("   edge %d %s '%s' len %.1f\n", ei, cn(e.cls), e.name.c_str(), e.length);
                    }
                }
        } else if (argv[a][0] == 'd') {
            // dead ends within a distance of the coast (roads running out onto the beach)
            float maxD = (float)atof(argv[a] + 1);
            int cnt = 0;
            for (size_t ni = 0; ni < roads.nodes.size(); ni++) {
                const RoadNode& n = roads.nodes[ni];
                if (n.edges.size() != 1) continue;
                float cd = map.coastDistance(n.p.x, n.p.y);
                if (cd > maxD) continue;
                const RoadEdge& e = roads.edges[n.edges[0]];
                Region reg = map.regionAt(n.p.x, n.p.y);
                float beachW = (reg == REG_BEACH || reg == REG_KEY_CORAL || reg == REG_KEYS || reg == REG_KEY_TOWN) ? 95.f : 30.f;
                if (regionInfo(reg).urban > 0.7f && reg != REG_BEACH) beachW = 8.f;
                if (reg == REG_SAWGRASS) beachW = 0.f;
                float sand = cd > -40.f ? SmoothStep(beachW + 10.f, beachW * 0.5f, cd) : 0.f;
                cnt++;
                printf("deadend %zu at (%.1f, %.1f, z %.2f) coast %.1f sand %.2f region %d edge %d %s '%s' len %.1f bulb %.1f\n", ni, n.p.x, n.p.y, n.z, cd, sand,
                       (int)reg, n.edges[0], cn(e.cls), e.name.c_str(), e.length, roads.bulbRadius(n));
            }
            printf("dead ends within %.0f m of the coast: %d\n", maxD, cnt);
        } else if (argv[a][0] == 'p') {
            float x, y, r;
            sscanf(argv[a] + 1, "%f,%f,%f", &x, &y, &r);
            vec2 p(x, y);
            int cx = (int)floorf((x + kWorldHalf) / kCellSize), cy = (int)floorf((y + kWorldHalf) / kCellSize);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    CellGeometry geo;
                    generateCell(cx + dx, cy + dy, true, geo);
                    for (auto& pr : geo.props)
                        if (length(pr.pos.xy() - p) < r) printf("  prop type %d at (%.1f, %.1f, %.1f)\n", pr.type, pr.pos.x, pr.pos.y, pr.pos.z);
                    for (auto& c : geo.collision)
                        if (length(c.c.xy() - p) < r + Max(c.he.x, c.he.y)) printf("  box c (%.1f, %.1f, %.1f) ax (%.2f,%.2f) he (%.1f, %.1f, %.1f)\n", c.c.x, c.c.y, c.c.z, c.ax.x, c.ax.y, c.he.x, c.he.y, c.he.z);
                }
        }
    }
    Jobs::shutdown();
    return 0;
}
