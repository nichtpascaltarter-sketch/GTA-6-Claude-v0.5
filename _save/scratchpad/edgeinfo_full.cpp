// World data checks (native, no GPU): drivable lanes must be free of colliders and props, and every road edge with a
// fall beside it (decks, embankments, approaches) must be guarded by a collision barrier.
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/worldcheck.cpp -o /tmp/worldcheck -lpthread
// Run from the repo root:    /tmp/worldcheck [max listed per category]     (exit code 1 when a check fails)
// Keep the include list in sync with the world section of src/main.cpp.
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
#include "src/world/interiors.cpp"
#include <thread>
#include <unordered_map>

using namespace World;

namespace worldcheck {

// A collider as the physics layer builds it (src/sim/physics.cpp CollisionWorld::addCell)
struct Col {
    int kind;      // 0 box, 1 cylinder (center c, radius he.x, half height he.z)
    vec3 c;
    vec2 ax;
    vec3 he;
    int src;       // -1 static box, else prop type
};

bool propCollider(const PropInstance& p, Col& c) {
    c.src = p.type;
    c.ax = vec2(cosf(p.yaw), sinf(p.yaw));
    switch (p.type) {
        case PROP_STREETLIGHT: case PROP_STREETLIGHT_DOUBLE: case PROP_TRAFFIC_LIGHT: case PROP_POWER_POLE:
            c.kind = 1; c.c = p.pos; c.he = vec3(0.17f * p.scale, 0.17f * p.scale, 8.f * p.scale); return true;
        case PROP_STOP_SIGN: case PROP_HYDRANT: case PROP_PARKING_METER:
            c.kind = 1; c.c = p.pos; c.he = vec3(0.12f, 0.12f, 1.f); return true;
        case PROP_PALM: case PROP_PALM_TALL: case PROP_TREE_OAK: case PROP_TREE_PINE: case PROP_CYPRESS:
            c.kind = 1; c.c = p.pos; c.he = vec3(0.26f * p.scale, 0.26f * p.scale, 6.f * p.scale); return true;
        case PROP_MANGROVE: c.kind = 1; c.c = p.pos; c.he = vec3(0.8f * p.scale, 0.8f * p.scale, 2.f); return true;
        case PROP_BENCH: case PROP_BIN: case PROP_DUMPSTER: case PROP_NEWS_BOX:
            c.kind = 0;
            c.he = p.type == PROP_DUMPSTER ? vec3(0.9f, 0.6f, 0.65f) : (p.type == PROP_BENCH ? vec3(0.9f, 0.25f, 0.45f) : vec3(0.3f, 0.3f, 0.5f));
            c.c = p.pos + vec3(0, 0, c.he.z);
            return true;
        case PROP_BUS_STOP: c.kind = 0; c.he = vec3(1.8f, 0.3f, 1.25f); c.c = p.pos + vec3(0, 0.9f, 1.25f); return true;
        case PROP_LIFEGUARD_TOWER: c.kind = 0; c.he = vec3(1.3f, 1.3f, 2.1f); c.c = p.pos + vec3(0, 0, 2.1f); return true;
        default: return false;
    }
}

bool hits(const Col& c, vec2 p, float z0, float z1, float margin) {
    if (c.c.z + c.he.z < z0 || c.c.z - c.he.z > z1) return false;
    if (c.kind == 1) return length(p - c.c.xy()) < c.he.x + margin;
    vec2 d = p - c.c.xy();
    return fabsf(dot(d, c.ax)) <= c.he.x + margin && fabsf(dot(d, perp(c.ax))) <= c.he.y + margin;
}

const char* typeName(int t) {
    static const char* n[] = {"streetlight", "streetlight2", "traffic light", "stop sign", "palm", "tall palm", "oak", "pine", "bush", "bench", "bin",
                              "hydrant", "bus stop", "bollard", "power pole", "mangrove", "cypress", "sawgrass", "parking meter", "news box",
                              "phone booth", "trash bags", "dumpster", "ac unit", "barrier", "highway sign", "planter", "umbrella", "lifeguard tower"};
    if (t < 0) return "static box";
    return t < (int)(sizeof(n) / sizeof(n[0])) ? n[t] : "prop";
}

}  // namespace worldcheck

int main(int argc, char** argv) {
    Jobs::init(2);
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == 'p') break;
        int ei = atoi(argv[i]);
        const RoadEdge& e = roads.edges[ei];
        printf("edge %d %s '%s' len %.0f hw %.1f sw %.1f flags %d cut %.0f/%.0f\n", ei, roadInfo(e.cls).name, e.name.c_str(), e.length, e.halfWidth, e.sidewalk, e.flags, e.cut0, e.cut1);
        for (float s = 0; s <= e.length; s += 20.f) {
            vec3 p = e.posAt(s);
            printf("  s %4.0f (%.1f, %.1f, %.1f) ground %.1f water %.1f\n", s, p.x, p.y, p.z, map.heightAt(p.x, p.y), map.waterAt(p.x, p.y));
        }
    }
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    if (argc > 3 && argv[argc - 3][0] == 'p') {
        float px = atof(argv[argc - 2]), py = atof(argv[argc - 1]);
        int cx = (int)floorf((px + kWorldHalf) / kCellSize), cy = (int)floorf((py + kWorldHalf) / kCellSize);
        CellGeometry geo;
        generateCell(cx, cy, true, geo);
        for (auto& b : geo.collision)
            if (length(b.c.xy() - vec2(px, py)) < 30.f)
                printf("  box c (%.1f, %.1f, %.1f) ax (%.2f, %.2f) he (%.1f, %.2f, %.2f)\n", b.c.x, b.c.y, b.c.z, b.ax.x, b.ax.y, b.he.x, b.he.y, b.he.z);
    }
    Jobs::shutdown();
}
