// World data checks (native, no GPU): drivable lanes must be free of colliders and props, and every road edge with a
// fall beside it (decks, embankments, approaches) must be guarded by a collision barrier.
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/worldcheck.cpp -o /tmp/worldcheck -lpthread
// Run from the repo root:    /tmp/worldcheck [max listed per category]     (exit code 1 when a check fails)
// Keep the include list in sync with the world section of src/main.cpp.
#include "native_stubs.cpp"
#include "../src/core/math.cpp"
#include "../src/core/noise.cpp"
#include "../src/core/jobs.cpp"
#include "../src/render/mesh.cpp"
#include "../src/world/worldmap.cpp"
#include "../src/world/sites.cpp"
#include "../src/world/roads.cpp"
#include "../src/world/roadmesh.cpp"
#include "../src/world/buildings.cpp"
#include "../src/world/buildmesh.cpp"
#include "../src/world/propmesh.cpp"
#include "../src/world/cellgen.cpp"
#include "../src/world/sitegeo.cpp"
#include "../src/world/airport.cpp"
#include "../src/world/port.cpp"
#include "../src/world/landmarks.cpp"
#include "../src/world/leisure.cpp"
#include "../src/world/rural.cpp"
#include "../src/world/transit.cpp"
#include "../src/world/transitmesh.cpp"
#include "../src/world/sitecell.cpp"
#include "../src/world/facadedetail.cpp"
#include "../src/world/interiorkit.cpp"
#include "../src/world/interiorfurniture.cpp"
#include "../src/world/interiorlayouts.cpp"
#include "../src/world/interiorhomes.cpp"
#include "../src/world/interiorvenues.cpp"
#include "../src/world/interiorshops.cpp"
#include "../src/world/interiorcivic.cpp"
#include "../src/world/interiorindustrial.cpp"
#include "../src/world/interiors.cpp"
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
                              "phone booth", "trash bags", "dumpster", "ac unit", "barrier", "highway sign", "planter", "umbrella", "lifeguard tower",
                              "bike rack", "cone", "mailbox", "street tree", "span signal", "work sign"};
    if (t < 0) return "static box";
    return t < (int)(sizeof(n) / sizeof(n[0])) ? n[t] : "prop";
}

}  // namespace worldcheck

int main(int argc, char** argv) {
    using namespace worldcheck;
    int maxList = argc > 1 ? atoi(argv[1]) : 12;
    bool verbose = argc > 2;
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap map;
    map.generate();
    gMap = &map;
    RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    BuildingSet bs;
    bs.generate(map, roads);
    gBuildings = &bs;
    // ---- gather colliders and props of every full-detail cell that holds roads, buildings or sites
    std::vector<Col> cols;
    std::vector<PropInstance> props;
    int cps = kCellsPerSide, cells = 0;
    for (int cy = 0; cy < cps; cy++)
        for (int cx = 0; cx < cps; cx++) {
            vec2 o = cellOrigin(cx, cy);
            std::vector<int> cand;
            roads.edgesInRect(o, o + vec2(kCellSize), cand);
            bool any = !cand.empty() || !bs.cellLists[(size_t)cy * cps + cx].empty() ||
                       (gSites && !gSites->cellElems.empty() && !gSites->cellElems[(size_t)cy * cps + cx].empty());
            if (!any) continue;
            CellGeometry geo;
            generateCell(cx, cy, true, geo);
            cells++;
            for (const CollisionBox& b : geo.collision) cols.push_back({0, b.c, b.ax, b.he, -1});
            for (const PropInstance& p : geo.props) {
                props.push_back(p);
                Col c;
                if (propCollider(p, c)) cols.push_back(c);
            }
        }
    // spatial hash (8 m)
    const float G = 8.f;
    auto key = [](int x, int y) { return (long long)(y + 100000) * 400000LL + (x + 100000); };
    std::unordered_map<long long, std::vector<int>> grid;
    for (size_t i = 0; i < cols.size(); i++) {
        const Col& c = cols[i];
        float r = c.kind == 1 ? c.he.x : sqrtf(c.he.x * c.he.x + c.he.y * c.he.y);
        int x0 = (int)floorf((c.c.x - r) / G), x1 = (int)floorf((c.c.x + r) / G), y0 = (int)floorf((c.c.y - r) / G), y1 = (int)floorf((c.c.y + r) / G);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) grid[key(x, y)].push_back((int)i);
    }
    std::unordered_map<long long, std::vector<int>> pgrid;
    for (size_t i = 0; i < props.size(); i++) pgrid[key((int)floorf(props[i].pos.x / G), (int)floorf(props[i].pos.y / G))].push_back((int)i);
    printf("worldcheck: %d cells, %zu colliders, %zu props\n", cells, cols.size(), props.size());

    // ---- 1. lanes
    std::unordered_map<int, int> laneHitsBySrc, lanePropsByType;
    std::vector<std::pair<vec3, int>> laneList;
    std::vector<char> seenCol(cols.size(), 0), seenProp(props.size(), 0);
    int laneHits = 0, propHits = 0;
    for (const RoadEdge& e : roads.edges) {
        if (e.flags & RF_UNPAVED) continue;
        const RoadClassInfo& ri = roadInfo(e.cls);
        bool twoWay = e.lanesF > 0 && e.lanesB > 0;
        float W = ri.laneWidth;
        std::vector<vec2> bands;
        if (twoWay) {
            float st = ri.median > 0.f ? ri.median * 0.5f : 0.f;
            bands.push_back(vec2(st, st + e.lanesF * W));
            bands.push_back(vec2(-(st + e.lanesB * W), -st));
        } else {
            int n = Max((int)e.lanesF, (int)e.lanesB);
            bands.push_back(vec2(-W * n * 0.5f, W * n * 0.5f));
        }
        for (float s = e.cut0 + 1.f; s < e.length - e.cut1 - 1.f; s += 2.f) {
            vec3 P = e.posAt(s), T = e.tangentAt(s);
            vec2 rt = normalize(vec2(T.y, -T.x));
            for (vec2 band : bands)
                for (float lat = band.x + 0.35f; lat <= band.y - 0.35f + 1e-3f; lat += 0.8f) {
                    vec2 p = P.xy() + rt * lat;
                    auto it = grid.find(key((int)floorf(p.x / G), (int)floorf(p.y / G)));
                    if (it != grid.end())
                        for (int ci : it->second) {
                            if (seenCol[ci] || !hits(cols[ci], p, P.z + 0.25f, P.z + 2.2f, 0.f)) continue;
                            seenCol[ci] = 1;
                            laneHits++;
                            if (verbose && cols[ci].src != -1 && laneHits <= maxList * 6)
                                printf("  %s collider at (%.1f, %.1f, %.1f) in a lane of %s edge %d (s %.0f, lat %.1f, edge z %.1f)\n", typeName(cols[ci].src), cols[ci].c.x,
                                       cols[ci].c.y, cols[ci].c.z, roadInfo(e.cls).name, (int)(&e - &roads.edges[0]), s, lat, P.z);
                            if (verbose && cols[ci].src == -1 && laneHits <= maxList * 3)
                                printf("  box c (%.1f, %.1f, %.1f) he (%.1f, %.1f, %.1f) in a lane of %s edge %d (s %.0f of %.0f, lat %.1f)\n", cols[ci].c.x, cols[ci].c.y,
                                       cols[ci].c.z, cols[ci].he.x, cols[ci].he.y, cols[ci].he.z, roadInfo(e.cls).name, (int)(&e - &roads.edges[0]), s, e.length, lat);
                            laneHitsBySrc[cols[ci].src]++;
                            if ((int)laneList.size() < maxList * 4) laneList.push_back({vec3(p, P.z), cols[ci].src});
                        }
                    auto pt = pgrid.find(key((int)floorf(p.x / G), (int)floorf(p.y / G)));
                    if (pt != pgrid.end())
                        for (int pi : pt->second) {
                            const PropInstance& pr = props[pi];
                            if (seenProp[pi] || fabsf(pr.pos.z - P.z) > 2.5f || length(pr.pos.xy() - p) > 0.6f) continue;
                            if (pr.type == PROP_SAWGRASS) continue;
                            seenProp[pi] = 1;
                            propHits++;
                            lanePropsByType[pr.type]++;
                            if (verbose && propHits <= maxList * 3)
                                printf("  prop %-14s at (%.1f, %.1f, %.1f) in a lane of %s edge %d (s %.0f of %.0f, cut %.0f/%.0f, lat %.1f, hw %.1f)\n", typeName(pr.type),
                                       pr.pos.x, pr.pos.y, pr.pos.z, roadInfo(e.cls).name, (int)(&e - &roads.edges[0]), s, e.length, e.cut0, e.cut1, lat, e.halfWidth);
                        }
                }
        }
    }
    printf("\n[lanes] colliders intruding into drivable lanes: %d, props standing in lanes: %d\n", laneHits, propHits);
    for (auto& kv : laneHitsBySrc) printf("  colliders from %-16s %d\n", typeName(kv.first), kv.second);
    for (auto& kv : lanePropsByType) printf("  props of type %-18s %d\n", typeName(kv.first), kv.second);
    for (int i = 0; i < (int)laneList.size() && i < maxList; i++)
        printf("  at (%.1f, %.1f, %.1f): %s\n", laneList[i].first.x, laneList[i].first.y, laneList[i].first.z, typeName(laneList[i].second));

    // ---- 2. falls beside road edges
    float unguarded = 0.f, guarded = 0.f;
    std::vector<vec3> fallList;
    for (const RoadEdge& e : roads.edges) {
        if (e.flags & RF_UNPAVED) continue;
        bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
        float edgeLat = e.halfWidth + (e.sidewalk > 0.f && !hwy ? e.sidewalk : 0.f);
        for (float s = e.cut0 + 2.f; s < e.length - e.cut1 - 2.f; s += 4.f) {
            vec3 P = e.posAt(s), T = e.tangentAt(s);
            vec2 rt = normalize(vec2(T.y, -T.x));
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 q = P.xy() + rt * (sd * (edgeLat + 2.0f));
                float ground = map.heightAt(q.x, q.y), wl = map.waterAt(q.x, q.y);
                float drop = P.z - ground;
                if (wl > kNoWater + 1.f) drop = Max(drop, P.z - wl + 0.5f);
                if (drop < 1.2f) continue;
                // another road or pad continues beside the edge (merges, interchanges, plazas) or a lower road lies right
                // there within reach (a barrier would stand in its lanes): the generator leaves these open, so do we
                float zq;
                if (roads.surfaceHeight(q, &zq, P.z + 1.f) && zq > P.z - 1.2f) continue;
                vec2 bp = P.xy() + rt * (sd * (edgeLat + 0.3f));
                // merge zone within +-6 m along the edge: the generator opens whole barrier sub-spans there
                bool mergeNear = false;
                for (float ds = -6.f; ds <= 6.f && !mergeNear; ds += 3.f) {
                    float s2 = Clamp(s + ds, 0.f, e.length);
                    vec3 P2 = e.posAt(s2), T2 = e.tangentAt(s2);
                    vec2 rt2 = normalize(vec2(T2.y, -T2.x));
                    vec2 b2 = P2.xy() + rt2 * (sd * (edgeLat + 0.475f));
                    mergeNear = roads.onPavement(b2, P2.z - 0.1f, 0.f, (int)(&e - &roads.edges[0]), 1.1f);
                }
                if (mergeNear) continue;
                bool ok = false;
                auto it = grid.find(key((int)floorf(bp.x / G), (int)floorf(bp.y / G)));
                if (it != grid.end())
                    for (int ci : it->second)
                        if (cols[ci].src == -1 && hits(cols[ci], bp, P.z + 0.3f, P.z + 0.7f, 0.2f)) { ok = true; break; }
                if (ok) guarded += 4.f;
                else {
                    unguarded += 4.f;
                    if ((int)fallList.size() < maxList) fallList.push_back(vec3(bp, P.z));
                    if (verbose && (int)fallList.size() <= maxList)
                        printf("  fall: %s edge %d s %.0f/%.0f side %d drop %.1f edgeLat %.1f sidewalk %.1f flags %d\n", roadInfo(e.cls).name, (int)(&e - &roads.edges[0]), s,
                               e.length, sd, drop, edgeLat, e.sidewalk, (int)e.flags);
                }
            }
        }
    }
    printf("\n[falls] road edges with a fall of more than 1.2 m: guarded %.0f m, unguarded %.0f m\n", guarded, unguarded);
    for (vec3 p : fallList) printf("  unguarded at (%.1f, %.1f, %.1f)\n", p.x, p.y, p.z);
    Jobs::shutdown();
    bool fail = laneHits > 0 || propHits > 0 || unguarded > 0.f;
    printf("\nworldcheck: %s\n", fail ? "FAILED" : "passed");
    return fail ? 1 : 0;
}
