// World data checks (native, no GPU): drivable lanes must be free of colliders and props, every road edge with a fall
// beside it (decks, embankments, approaches) must be guarded by a collision barrier, the ground the vehicle physics
// drives on must meet the lane surfaces without steps, no road profile hides a cliff, and roads that cross without a
// junction are grade separated.
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
#include <unordered_map>
#include <map>
#include <array>
#include <algorithm>

using namespace World;

namespace worldcheck {

// A collider as the physics layer builds it (src/sim/physics.cpp CollisionWorld::addCell)
struct Col {
    int kind;      // 0 box, 1 cylinder (base centre c, radius he.x, height he.z)
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
    if (c.kind == 1) {
        // cylinders are base-anchored in physics.h: base centre c, he = (radius, radius, height), z in [c.z, c.z + he.z]
        if (c.c.z + c.he.z < z0 || c.c.z > z1) return false;
        return length(p - c.c.xy()) < c.he.x + margin;
    }
    if (c.c.z + c.he.z < z0 || c.c.z - c.he.z > z1) return false;
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
    // trash bag piles: on the map, and round given points (FP_BAGS=x,y;x,y;...: within 60 and 90 m)
    if (const char* fb = getenv("FP_BAGS")) {
        int all = 0;
        for (const PropInstance& p : props) all += p.type == PROP_TRASH_BAGS;
        printf("  trash bag piles: %d on the map\n", all);
        const char* q = fb;
        while (*q) {
            float x = 0, y = 0;
            if (sscanf(q, "%f,%f", &x, &y) != 2) break;
            int n60 = 0, n90 = 0;
            for (const PropInstance& p : props) {
                if (p.type != PROP_TRASH_BAGS) continue;
                float d = length(vec2(p.pos.x, p.pos.y) - vec2(x, y));
                n60 += d < 60.f;
                n90 += d < 90.f;
            }
            printf("  trash bag piles round (%.0f, %.0f): %d within 60 m, %d within 90 m\n", x, y, n60, n90);
            const char* sc = strchr(q, ';');
            if (!sc) break;
            q = sc + 1;
        }
    }

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
    // dead-end turning circles, placed as the lane graph does (src/game/lanes.cpp): a half circle of radius
    // clamp(inner lane offset + 3.4, 4.8, 6.5) m around the dead end, pulled back toward the street in 1.5 m steps while a
    // building or a site structure stands within 4.2 m of it. None must be left without a placement, and the one chosen
    // must keep the car body (1.1 m either side of the path) clear of every collider (props, barriers)
    int turnBlocked = 0, turnPulled = 0, turnObstructed = 0;
    std::vector<vec2> turnList;
    std::vector<int> turnBy;   // what stands in the way of the placement (-1: no placement at all)
    for (size_t ni = 0; ni < roads.nodes.size(); ni++) {
        const RoadNode& nd = roads.nodes[ni];
        if (nd.edges.size() != 1) continue;
        const RoadEdge& e = roads.edges[nd.edges[0]];
        if ((e.flags & RF_ONEWAY) || e.lanesF == 0 || e.lanesB == 0 || e.pts.size() < 2) continue;
        const RoadClassInfo& ri = roadInfo(e.cls);
        bool atEnd = e.n1 == (int)ni;
        vec2 t0 = atEnd ? normalize(e.pts.back().xy() - e.pts[e.pts.size() - 2].xy()) : normalize(e.pts[0].xy() - e.pts[1].xy());
        float R = Clamp((ri.median > 0.f ? ri.median * 0.5f : 0.f) + ri.laneWidth * 0.5f + 3.4f, 4.8f, 6.5f);
        vec2 lf = vec2(-t0.y, t0.x);
        auto circle = [&](int attempt, int k) {
            float th = -kHalfPi + kPi * k / 12.f;
            return nd.p - t0 * (1.5f * attempt) + t0 * (R * cosf(th)) + lf * (R * sinf(th));
        };
        int placedAt = -1;
        for (int attempt = 0; attempt < 9 && placedAt < 0; attempt++) {
            bool blocked = false;
            for (int k = 0; k <= 12 && !blocked; k++) {
                vec2 q = circle(attempt, k);
                blocked = gBuildings->pointInBuilding(q, 4.2f) || siteColliderNear(vec3(q, nd.z), 4.2f, 2.5f);
            }
            if (!blocked) placedAt = attempt;
        }
        int by = -1;
        if (placedAt >= 0) {
            turnPulled += placedAt > 0;
            for (int k = 0; k <= 12 && by < 0; k++) {
                vec2 q = circle(placedAt, k);
                for (int gy = (int)floorf((q.y - 1.1f) / G); gy <= (int)floorf((q.y + 1.1f) / G) && by < 0; gy++)
                    for (int gx = (int)floorf((q.x - 1.1f) / G); gx <= (int)floorf((q.x + 1.1f) / G) && by < 0; gx++) {
                        auto it = grid.find(key(gx, gy));
                        if (it == grid.end()) continue;
                        for (int ci : it->second)
                            if (hits(cols[ci], q, nd.z + 0.3f, nd.z + 1.8f, 1.1f)) { by = ci; break; }
                    }
            }
            if (by < 0) continue;
            turnObstructed++;
        } else turnBlocked++;
        if ((int)turnList.size() < maxList) {
            turnList.push_back(nd.p);
            turnBy.push_back(by);
        }
    }
    printf("\n[lanes] colliders intruding into drivable lanes: %d, props standing in lanes: %d; dead-end turning circles without room: "
           "%d, obstructed: %d (%d pulled back to fit)\n", laneHits, propHits, turnBlocked, turnObstructed, turnPulled);
    for (size_t i = 0; i < turnList.size(); i++) {
        if (turnBy[i] < 0) {
            printf("  no room for a turning circle at dead end (%.1f, %.1f)\n", turnList[i].x, turnList[i].y);
            continue;
        }
        const Col& c = cols[turnBy[i]];
        printf("  turning circle at dead end (%.1f, %.1f) obstructed by %s at (%.1f, %.1f, %.1f) he (%.1f, %.1f, %.1f)\n", turnList[i].x, turnList[i].y,
               typeName(c.src), c.c.x, c.c.y, c.c.z, c.he.x, c.he.y, c.he.z);
    }
    for (auto& kv : laneHitsBySrc) printf("  colliders from %-16s %d\n", typeName(kv.first), kv.second);
    for (auto& kv : lanePropsByType) printf("  props of type %-18s %d\n", typeName(kv.first), kv.second);
    for (int i = 0; i < (int)laneList.size() && i < maxList; i++)
        printf("  at (%.1f, %.1f, %.1f): %s\n", laneList[i].first.x, laneList[i].first.y, laneList[i].first.z, typeName(laneList[i].second));

    // ---- 2. falls beside road edges (measured on the mesher's cross-sections: at a bend the edge and its rails follow the
    // mitre, not the perpendicular of one segment)
    float unguarded = 0.f, guarded = 0.f;
    std::vector<vec3> fallList;
    std::vector<Section> secs;
    for (const RoadEdge& e : roads.edges) {
        if (e.flags & RF_UNPAVED) continue;
        bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
        float edgeLat = e.halfWidth + (e.sidewalk > 0.f && !hwy ? e.sidewalk : 0.f);
        buildSections(e, secs);
        if (secs.size() < 2) continue;
        // the point `lat` metres out (right positive) at distance s, and the centre height there
        auto at = [&](float s, float lat, float* zc) {
            size_t k = 0;
            while (k + 2 < secs.size() && secs[k + 1].s < s) k++;
            const Section& a = secs[k];
            const Section& b = secs[k + 1];
            float t = Saturate((s - a.s) / Max(b.s - a.s, 1e-4f));
            if (zc) *zc = Lerp(a.c.z, b.c.z, t);
            return lerp(sectionPoint(a, lat), sectionPoint(b, lat), t).xy();
        };
        for (float s = e.cut0 + 2.f; s < e.length - e.cut1 - 2.f; s += 4.f) {
            float pz = 0.f;
            at(s, 0.f, &pz);
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 q = at(s, sd * (edgeLat + 2.0f), nullptr);
                float ground = map.heightAt(q.x, q.y), wl = map.waterAt(q.x, q.y);
                float drop = pz - ground;
                if (wl > kNoWater + 1.f) drop = Max(drop, pz - wl + 0.5f);
                if (drop < 1.2f) continue;
                // another road or pad continues beside the edge (merges, interchanges, plazas) or a lower road lies right
                // there within reach (a barrier would stand in its lanes): the generator leaves these open, so do we
                float zq;
                if (roads.surfaceHeight(q, &zq, pz + 1.f) && zq > pz - 1.2f) continue;
                vec2 bp = at(s, sd * (edgeLat + 0.3f), nullptr);
                // merge zone within +-6 m along the edge: the generator opens whole barrier sub-spans there
                bool mergeNear = false;
                for (float ds = -6.f; ds <= 6.f && !mergeNear; ds += 3.f) {
                    float s2 = Clamp(s + ds, 0.f, e.length), z2 = 0.f;
                    vec2 b2 = at(s2, sd * (edgeLat + 0.475f), &z2);
                    mergeNear = roads.onPavement(b2, z2 - 0.1f, 0.f, (int)(&e - &roads.edges[0]), 1.1f);
                }
                if (mergeNear) continue;
                bool ok = false;
                for (int gy = (int)floorf((bp.y - 0.2f) / G); gy <= (int)floorf((bp.y + 0.2f) / G) && !ok; gy++)
                    for (int gx = (int)floorf((bp.x - 0.2f) / G); gx <= (int)floorf((bp.x + 0.2f) / G) && !ok; gx++) {
                        auto it = grid.find(key(gx, gy));
                        if (it == grid.end()) continue;
                        for (int ci : it->second)
                            if (cols[ci].src == -1 && hits(cols[ci], bp, pz + 0.3f, pz + 0.7f, 0.2f)) { ok = true; break; }
                    }
                if (ok) guarded += 4.f;
                else {
                    unguarded += 4.f;
                    if ((int)fallList.size() < maxList) fallList.push_back(vec3(bp, pz));
                    if (verbose && (int)fallList.size() <= maxList)
                        printf("  fall: %s edge %d s %.0f/%.0f side %d drop %.1f edgeLat %.1f sidewalk %.1f flags %d\n", roadInfo(e.cls).name, (int)(&e - &roads.edges[0]), s,
                               e.length, sd, drop, edgeLat, e.sidewalk, (int)e.flags);
                }
            }
        }
    }
    printf("\n[falls] road edges with a fall of more than 1.2 m: guarded %.0f m, unguarded %.0f m\n", guarded, unguarded);
    for (vec3 p : fallList) printf("  unguarded at (%.1f, %.1f, %.1f)\n", p.x, p.y, p.z);

    // ---- 3. lane surface vs the ground the vehicle physics drives on (terrain + RoadNetwork::surfaceHeight, as
    // Phys::CollisionWorld::ground): a step of more than 0.45 m hangs a car up (junction discs vs ramps, buried roads)
    struct ZHit { float step; vec3 p; int edge; float s; float terrain; };
    std::vector<ZHit> zhits;
    long zsamples = 0;
    for (size_t ei = 0; ei < roads.edges.size(); ei++) {
        const RoadEdge& e = roads.edges[ei];
        if (e.flags & RF_UNPAVED) continue;
        const RoadClassInfo& ri = roadInfo(e.cls);
        bool twoWay = e.lanesF > 0 && e.lanesB > 0;
        float W = ri.laneWidth, st = twoWay && ri.median > 0.f ? ri.median * 0.5f : 0.f;
        std::vector<float> lats;
        if (twoWay) {
            for (int k = 0; k < e.lanesF; k++) lats.push_back(st + (k + 0.5f) * W);
            for (int k = 0; k < e.lanesB; k++) lats.push_back(-(st + (k + 0.5f) * W));
        } else {
            int n = Max((int)e.lanesF, (int)e.lanesB);
            for (int k = 0; k < n; k++) lats.push_back(-W * n * 0.5f + (k + 0.5f) * W);
        }
        for (float sd = e.cut0; sd <= e.length - e.cut1; sd += 2.f) {
            vec3 P = e.posAt(sd), T = e.tangentAt(sd);
            vec2 rt = normalize(vec2(T.y, -T.x));
            for (float lat : lats) {
                vec2 q = P.xy() + rt * lat;
                float th = map.heightAt(q.x, q.y), g = th, rz = th;
                if (roads.surfaceHeight(q, &rz, P.z + 2.5f) && rz > th - 0.5f) g = rz;
                zsamples++;
                float step = g - P.z;
                if (fabsf(step) > 0.45f) zhits.push_back({step, vec3(q, P.z), (int)ei, sd, th});
            }
        }
    }
    std::sort(zhits.begin(), zhits.end(), [](const ZHit& a, const ZHit& b) { return fabsf(a.step) > fabsf(b.step); });
    std::vector<ZHit> zreps;
    for (const ZHit& h : zhits) {
        bool dup = false;
        for (const ZHit& r : zreps)
            if (length(r.p.xy() - h.p.xy()) < 30.f) dup = true;
        if (!dup) zreps.push_back(h);
    }
    printf("\n[steps] lane surface vs physics ground: %ld samples, %zu off by more than 0.45 m at %zu places\n", zsamples, zhits.size(), zreps.size());
    // what the physics stands on at a spot: the highest road surface (edge / junction) or site pad below maxZ
    auto culprit = [&](vec2 q, float maxZ) {
        std::vector<int> cand;
        roads.edgesInRect(q - vec2(1.f), q + vec2(1.f), cand);
        float bz = -1e9f;
        std::string what = "terrain";
        for (int ei : cand) {
            const RoadEdge& e = roads.edges[ei];
            float bd = 1e30f, bze = 0.f, bs = 0.f;
            bool beyondEnd = false;
            for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                float t;
                float d = distPointSegment2D(q, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
                if (d < bd) {
                    bd = d;
                    bze = Lerp(e.pts[k].z, e.pts[k + 1].z, t);
                    bs = Lerp(e.dist[k], e.dist[k + 1], t);
                    beyondEnd = (k == 0 && t <= 0.f) || (k + 2 == e.pts.size() && t >= 1.f);
                }
            }
            bool inJunction = bs < e.cut0 || bs > e.length - e.cut1;
            if (bd <= e.halfWidth + ((inJunction || beyondEnd) ? 0.f : e.sidewalk)) {
                float zz = bze + (bd > e.halfWidth ? 0.15f : 0.f);
                if (zz <= maxZ && zz > bz) { bz = zz; what = StrFormat("%s edge %d%s", roadInfo(e.cls).name, ei, bd > e.halfWidth ? " (sidewalk)" : ""); }
            }
            for (int nn : {e.n0, e.n1}) {
                const RoadNode& n = roads.nodes[nn];
                float dn = length(q - n.p), br = roads.bulbRadius(n);
                if (br > 0.f && dn < br + e.sidewalk) {
                    float nz = roads.junctionZ(n, q) + (dn > br ? 0.15f : 0.f);
                    if (nz <= maxZ && nz > bz) { bz = nz; what = StrFormat("turning bulb %d", nn); }
                }
                if (n.radius <= 0 || dn >= n.radius) continue;
                float nz = roads.junctionZ(n, q);
                if (nz <= maxZ && nz > bz) { bz = nz; what = StrFormat("junction %d", nn); }
            }
        }
        float pz;
        if (gSites && gSites->padHeight(q, &pz, maxZ) && pz > bz) what = "site pad";
        return what;
    };
    for (size_t i = 0; i < zreps.size() && (int)i < maxList * 2; i++) {
        const ZHit& h = zreps[i];
        const RoadEdge& e = roads.edges[h.edge];
        printf("  step %+.2f m at (%.1f, %.1f) z %.2f (terrain %.2f): %s edge %d s %.0f of %.0f (cut %.0f/%.0f) nodes %d->%d; ground from %s\n", h.step, h.p.x,
               h.p.y, h.p.z, h.terrain, roadInfo(e.cls).name, h.edge, h.s, e.length, e.cut0, e.cut1, e.n0, e.n1, culprit(h.p.xy(), h.p.z + 2.5f).c_str());
    }
    // ---- 4. grades: no drivable edge climbs or drops more than 16% between two profile points (a cliff inside a road)
    int steep = 0;
    printf("\n[grades] road profile segments steeper than 16%%:\n");
    for (size_t ei = 0; ei < roads.edges.size(); ei++) {
        const RoadEdge& e = roads.edges[ei];
        if (e.flags & RF_UNPAVED) continue;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            float d = length(e.pts[k + 1].xy() - e.pts[k].xy());
            if (d < 0.5f) continue;
            float g = fabsf(e.pts[k + 1].z - e.pts[k].z) / d;
            if (g <= 0.16f) continue;
            if (steep++ < maxList)
                printf("  %.0f%% on %s edge %d at (%.1f, %.1f) z %.2f -> %.2f over %.1f m (s %.0f of %.0f)\n", g * 100.f, roadInfo(e.cls).name, (int)ei, e.pts[k].x,
                       e.pts[k].y, e.pts[k].z, e.pts[k + 1].z, d, e.dist[k], e.length);
        }
    }
    printf("  %d segments\n", steep);
    // ---- 5. crossings: two roads whose centrelines cross away from a shared junction must be grade separated (4.5 m or
    // more between the surfaces); anything closer is a flat crossing without a junction or a deck in the other's traffic
    int crossings = 0;
    {
        const float C = 64.f;
        std::unordered_map<long long, std::vector<std::pair<int, int>>> sgrid;
        for (size_t ei = 0; ei < roads.edges.size(); ei++) {
            const RoadEdge& e = roads.edges[ei];
            for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                vec2 a = e.pts[k].xy(), c = e.pts[k + 1].xy();
                int x0 = (int)floorf(Min(a.x, c.x) / C), x1 = (int)floorf(Max(a.x, c.x) / C);
                int y0 = (int)floorf(Min(a.y, c.y) / C), y1 = (int)floorf(Max(a.y, c.y) / C);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) sgrid[key(x, y)].push_back({(int)ei, (int)k});
            }
        }
        std::vector<vec3> seen;
        printf("\n[crossings] roads crossing without a junction less than 4.5 m apart:\n");
        for (auto& kv : sgrid) {
            const auto& v = kv.second;
            for (size_t i = 0; i < v.size(); i++)
                for (size_t j = i + 1; j < v.size(); j++) {
                    if (v[i].first == v[j].first) continue;
                    const RoadEdge& A = roads.edges[v[i].first];
                    const RoadEdge& B = roads.edges[v[j].first];
                    vec3 a0 = A.pts[v[i].second], a1 = A.pts[v[i].second + 1], b0 = B.pts[v[j].second], b1 = B.pts[v[j].second + 1];
                    float ta, tb;
                    if (!segmentIntersect2D(a0.xy(), a1.xy(), b0.xy(), b1.xy(), &ta, &tb)) continue;
                    vec2 da = a1.xy() - a0.xy(), db = b1.xy() - b0.xy();
                    if (fabsf(cross(da, db)) < 0.05f * length(da) * length(db)) continue;   // running along each other, not across
                    vec2 p = lerp(a0.xy(), a1.xy(), ta);
                    bool atNode = false;
                    for (int na : {A.n0, A.n1})
                        for (int nb : {B.n0, B.n1})
                            if (na == nb && length(roads.nodes[na].p - p) < roads.nodes[na].radius + 3.f) atNode = true;
                    if (atNode) continue;
                    float dz = fabsf(Lerp(a0.z, a1.z, ta) - Lerp(b0.z, b1.z, tb));
                    if (dz >= 4.5f) continue;
                    bool dup = false;
                    for (vec3 q : seen) dup |= length(q.xy() - p) < 1.f;
                    if (dup) continue;
                    seen.push_back(vec3(p, dz));
                    if (crossings++ < maxList)
                        printf("  %s edge %d x %s edge %d at (%.1f, %.1f): %.2f m apart\n", roadInfo(A.cls).name, v[i].first, roadInfo(B.cls).name, v[j].first, p.x, p.y, dz);
                }
        }
        printf("  %d crossings\n", crossings);
    }
    // ---- building footprints that overlap: every building's envelope and its own ground-standing collision boxes (the
    // body and its parts, garage wings, carports, porches - at least 1.5 m tall and 1 m^2, so not posts, rails or fences)
    // against the other buildings' (more than 0.3 m into each other, sideways and on both plan axes)
    int fpPairs = 0, fpEnvEnv = 0;
    {
        printf("\n[footprints] buildings whose footprints overlap (envelopes, wings, garages, carports, porches):\n");
        struct FB {
            int bi;
            vec2 c, ax;
            float hx, hy, z0, z1;
            bool env;
            int kind;   // 0 the envelope, 1 a house's garage wing or carport (beside the house on its garage side), 2 another part
        };
        const size_t nbld = bs.buildings.size();
        std::vector<std::vector<FB>> per(nbld);
        std::vector<float> pastSide(nbld, 0.f), pastDepth(nbld, 0.f);   // how far a building's boxes reach past its own lot
        std::vector<int> pastKind(nbld, 0);
        Jobs::parallelFor((int)nbld, [&](int i) {
            const Building& b = bs.buildings[i];
            std::vector<FB>& out = per[i];
            out.push_back({i, b.c, b.ax, b.hx, b.hy, b.baseZ, b.baseZ + Max(b.height, 2.f), true, 0});
            if (b.siteElem >= 0) return;
            MeshData m;
            std::vector<CollisionBox> col;
            buildBuildingMesh(b, bs.facades[b.facade], map, true, vec3(0.f), m, &col, nullptr, nullptr);
            const bool garage = (b.style == BS_HOUSE && (b.seed % 10u) < 7u) || b.style == BS_VILLA;
            const float gside = (b.seed & 64u) ? 1.f : -1.f;
            for (const CollisionBox& cb : col) {
                if (cb.he.z * 2.f < 1.5f || cb.he.x * cb.he.y * 4.f < 1.f) continue;
                if (cb.c.z - cb.he.z > b.baseZ + 4.f) continue;   // (on the roof)
                const float u = dot(cb.c.xy() - b.c, b.ax);
                const int kind = (garage && gside * u > b.hx + 0.5f) ? 1 : 2;
                out.push_back({i, cb.c.xy(), cb.ax, cb.he.x, cb.he.y, cb.c.z - cb.he.z, cb.c.z + cb.he.z, false, kind});
            }
            const vec2 ly = perp(b.ax);
            for (const FB& f : out) {
                const vec2 d = f.c - b.lotC;
                const float eu = fabsf(dot(f.ax, b.ax)) * f.hx + fabsf(dot(perp(f.ax), b.ax)) * f.hy;
                const float ev = fabsf(dot(f.ax, ly)) * f.hx + fabsf(dot(perp(f.ax), ly)) * f.hy;
                const float ps = fabsf(dot(d, b.ax)) + eu - b.lotHx, pd = fabsf(dot(d, ly)) + ev - b.lotHy;
                if (ps > pastSide[i]) { pastSide[i] = ps; pastKind[i] = f.kind; }
                pastDepth[i] = Max(pastDepth[i], pd);
            }
        });
        std::vector<FB> all;
        for (auto& v : per)
            for (const FB& f : v) all.push_back(f);
        const float G = 16.f;
        std::unordered_map<long long, std::vector<int>> grid;
        auto key = [](int gx, int gy) { return ((long long)gx << 32) ^ (unsigned)gy; };
        for (int k = 0; k < (int)all.size(); k++) {
            const FB& f = all[k];
            vec2 ay = perp(f.ax);
            float ex = fabsf(f.ax.x) * f.hx + fabsf(ay.x) * f.hy, ey = fabsf(f.ax.y) * f.hx + fabsf(ay.y) * f.hy;
            for (int gy = (int)floorf((f.c.y - ey) / G); gy <= (int)floorf((f.c.y + ey) / G); gy++)
                for (int gx = (int)floorf((f.c.x - ex) / G); gx <= (int)floorf((f.c.x + ex) / G); gx++) grid[key(gx, gy)].push_back(k);
        }
        // the depth one box reaches into another along the separating axes (SAT on the four box axes; <= 0: apart)
        auto depth = [](const FB& a, const FB& b) {
            const vec2 axes[4] = {a.ax, perp(a.ax), b.ax, perp(b.ax)};
            float best = 1e9f;
            for (const vec2& n : axes) {
                auto ext = [&](const FB& f) { return fabsf(dot(f.ax, n)) * f.hx + fabsf(dot(perp(f.ax), n)) * f.hy; };
                float d = fabsf(dot(a.c - b.c, n)), o = ext(a) + ext(b) - d;
                best = Min(best, o);
            }
            return best;
        };
        struct Hit {
            float d;
            int a, b;
            bool envA, envB;
            vec2 p;
            int kA, kB;
        };
        std::map<std::pair<int, int>, Hit> pairs;
        for (auto& kv : grid) {
            const std::vector<int>& v = kv.second;
            for (size_t i = 0; i < v.size(); i++)
                for (size_t j = i + 1; j < v.size(); j++) {
                    const FB &A = all[v[i]], &B = all[v[j]];
                    if (A.bi == B.bi) continue;
                    if (Min(A.z1, B.z1) - Max(A.z0, B.z0) < 0.5f) continue;
                    float d = depth(A, B);
                    if (d <= 0.3f) continue;
                    std::pair<int, int> pk(Min(A.bi, B.bi), Max(A.bi, B.bi));
                    bool swap = A.bi > B.bi;
                    Hit h{d, pk.first, pk.second, swap ? B.env : A.env, swap ? A.env : B.env, (A.c + B.c) * 0.5f, swap ? B.kind : A.kind, swap ? A.kind : B.kind};
                    auto it = pairs.find(pk);
                    if (it == pairs.end() || it->second.d < d) pairs[pk] = h;
                }
        }
        const char* styleName[] = {"tower", "midrise", "condo", "deco", "shops", "stripmall", "house", "villa", "warehouse", "factory", "farmhouse", "barn", "motel",
                                   "gasstation", "garage", "church", "shack"};
        std::map<std::string, int> byKind, byPart, byRegion;
        std::vector<Hit> hits;
        for (auto& kv : pairs) {
            const Hit& h = kv.second;
            const Building &A = bs.buildings[h.a], &B = bs.buildings[h.b];
            if (A.siteElem >= 0 && B.siteElem >= 0) {
                printf("  (hand-built, by its site: b%d x b%d at (%.1f, %.1f), %.2f m - not counted)\n", h.a, h.b, h.p.x, h.p.y, h.d);
                continue;
            }
            fpPairs++;
            if (h.envA && h.envB) fpEnvEnv++;
            std::string sa = A.style < BS_COUNT ? styleName[A.style] : "?", sb = B.style < BS_COUNT ? styleName[B.style] : "?";
            if (A.siteElem >= 0) sa += "(site)";
            if (B.siteElem >= 0) sb += "(site)";
            if (sa > sb) std::swap(sa, sb);
            byKind[sa + " / " + sb + (h.envA && h.envB ? " (envelopes)" : " (a wing, garage or porch)")]++;
            {
                const char* kn[3] = {"envelope", "garage wing", "other part"};
                std::string ka = std::string(A.style < BS_COUNT ? styleName[A.style] : "?") + " " + kn[h.kA], kb = std::string(B.style < BS_COUNT ? styleName[B.style] : "?") + " " + kn[h.kB];
                if (ka > kb) std::swap(ka, kb);
                byPart[ka + " x " + kb]++;
                byRegion[regionInfo((Region)A.region).name]++;
            }
            hits.push_back(h);
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.d > y.d; });
        for (int k = 0; k < (int)hits.size() && k < maxList; k++) {
            const Hit& h = hits[k];
            const Building &A = bs.buildings[h.a], &B = bs.buildings[h.b];
            printf("  b%d %s (%s) x b%d %s (%s) at (%.1f, %.1f): %.2f m into each other\n", h.a, A.style < BS_COUNT ? styleName[A.style] : "?", h.envA ? "envelope" : "part",
                   h.b, B.style < BS_COUNT ? styleName[B.style] : "?", h.envB ? "envelope" : "part", h.p.x, h.p.y, h.d);
        }
        for (auto& kv : byKind) printf("    %-60s %d\n", kv.first.c_str(), kv.second);
        printf("  by the parts that meet (deepest pair of boxes):\n");
        for (auto& kv : byPart) printf("    %-60s %d\n", kv.first.c_str(), kv.second);
        printf("  by district:\n");
        for (auto& kv : byRegion) printf("    %-60s %d\n", kv.first.c_str(), kv.second);
        // the buildings round a point, their boxes and their overlaps (FP_NEAR=x,y,r)
        if (const char* fn = getenv("FP_NEAR")) {
            float nx = 0, ny = 0, nr = 30;
            sscanf(fn, "%f,%f,%f", &nx, &ny, &nr);
            std::vector<char> in(nbld, 0);
            for (size_t i = 0; i < nbld; i++) {
                const Building& b = bs.buildings[i];
                if (length(b.c - vec2(nx, ny)) > nr) continue;
                in[i] = 1;
                printf("  near: b%zu %s %s arch %d seed%%10 %u side %+d c (%.1f, %.1f) ax (%.2f, %.2f) front (%.2f, %.2f) hx %.2f hy %.2f h %.1f lot (%.1f, %.1f) lotHx %.2f lotHy %.2f lat %.2f\n", i,
                       b.style < BS_COUNT ? styleName[b.style] : "?", regionInfo((Region)b.region).name, (int)b.arch, b.seed % 10u, (b.seed & 64u) ? 1 : -1, b.c.x, b.c.y,
                       b.ax.x, b.ax.y, b.front.x, b.front.y, b.hx, b.hy, b.height, b.lotC.x, b.lotC.y, b.lotHx, b.lotHy, dot(b.lotC - b.c, b.ax));
                for (const FB& f : per[i])
                    if (!f.env) printf("        box kind %d c (%.1f, %.1f) he %.2f x %.2f z %.1f..%.1f\n", f.kind, f.c.x, f.c.y, f.hx, f.hy, f.z0, f.z1);
                if (getenv("FP_ALLBOX") && b.siteElem < 0) {
                    // every collider the building's full-detail mesh makes (fences, walls, hedges too)
                    MeshData m;
                    std::vector<CollisionBox> col;
                    buildBuildingMesh(b, bs.facades[b.facade], map, true, vec3(0.f), m, &col, nullptr, nullptr);
                    for (const CollisionBox& cb : col)
                        printf("        col c (%.1f, %.1f, %.1f) ax (%.2f, %.2f) he (%.2f, %.2f, %.2f)\n", cb.c.x, cb.c.y, cb.c.z, cb.ax.x, cb.ax.y, cb.he.x, cb.he.y, cb.he.z);
                }
            }
            for (const Hit& h : hits)
                if (in[h.a] || in[h.b]) printf("  near: pair b%d (kind %d) x b%d (kind %d) at (%.1f, %.1f): %.2f m\n", h.a, h.kA, h.b, h.kB, h.p.x, h.p.y, h.d);
        }
        // street-level camera shots at the deepest overlap of each district (FP_SHOTS=1): from the street in front of the
        // first building of the pair, looking back at the pair (the same shots show the fix: the camera stays)
        if (getenv("FP_SHOTS")) {
            std::map<int, const Hit*> deepest;
            for (const Hit& h : hits) {
                int reg = bs.buildings[h.a].region;
                auto it = deepest.find(reg);
                if (it == deepest.end() || it->second->d < h.d) deepest[reg] = &h;
            }
            for (auto& kv : deepest) {
                const Hit& h = *kv.second;
                const Building& A = bs.buildings[h.a];
                vec2 F = A.front, p = h.p;
                float toFront = dot(A.lotC - p, F) + A.lotHy;
                vec2 cam = p + F * (toFront + 7.f) - A.ax * 4.f;
                vec2 look = normalize(p - cam);
                float yaw = atan2f(-look.x, look.y) * 57.29578f;
                printf("  shot: --shot %.2f,%.2f,%.2f,%.2f,-4.00,11.00,fp_%s_b%d  (%.2f m)\n", cam.x, cam.y, A.baseZ + 1.7f, yaw, regionInfo((Region)A.region).name, h.a, h.d);
            }
        }
        // storefront side walls that look onto a house's yard (not a street): street-level shots from in front of the house,
        // looking at the shop's side wall (FP_SFSHOTS=n: up to n per district), and the count of such walls
        if (const char* sv = getenv("FP_SFSHOTS")) {
            const int per = Max(1, atoi(sv));
            auto streetAhead = [&](vec2 m, vec2 n) {
                for (float d = 2.f; d <= 12.f; d += 2.5f) {
                    float s0, dd, side;
                    int e = roads.nearestEdge(m + n * d, 30.f, &s0, &dd, &side);
                    if (e < 0) continue;
                    const RoadEdge& ed = roads.edges[e];
                    if (ed.cls == RC_HIGHWAY || ed.cls == RC_RAMP) continue;
                    if (dd < ed.halfWidth + ed.sidewalk + 0.3f) return true;
                }
                return false;
            };
            std::map<int, int> shotsBy;
            int nSide = 0;
            for (size_t i = 0; i < nbld; i++) {
                const Building& sb = bs.buildings[i];
                if (sb.siteElem >= 0 || sb.face < 0 || !(bs.facades[sb.facade].flags & 1u)) continue;
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    const vec2 n = sb.ax * (float)sgn, m = sb.c + n * sb.hx;
                    if (streetAhead(m, n)) continue;
                    nSide++;
                    // a house beside it on the same street side
                    std::vector<int> nb;
                    bs.buildingsNear(m + n * 14.f, 14.f, nb);
                    for (int j : nb) {
                        const Building& h = bs.buildings[j];
                        if (h.style != BS_HOUSE || h.face != sb.face || dot(h.c - sb.c, n) < sb.hx + 4.f) continue;
                        if (shotsBy[sb.region] >= per) break;
                        shotsBy[sb.region]++;
                        vec2 cam = h.lotC + h.front * (h.lotHy + 5.f) + n * (h.lotHx * 0.6f);
                        vec2 look = normalize((m + sb.front * (sb.hy * 0.3f)) - cam);
                        float yaw = atan2f(-look.x, look.y) * 57.29578f;
                        printf("  sfshot: --shot %.2f,%.2f,%.2f,%.2f,2.00,11.00,sf_%d_b%zu  (%s, house b%d)\n", cam.x, cam.y, h.baseZ + 1.7f, yaw, sb.region, i,
                               regionInfo((Region)sb.region).name, j);
                        break;
                    }
                }
            }
            printf("  storefront side walls without a street in front: %d\n", nSide);
        }
        // the cause measured directly: the buildings whose ground boxes reach past their own lot line, sideways (into the
        // neighbour's lot) or front/back, by style and by the part that reaches furthest
        {
            std::map<std::string, std::array<int, 4>> st;   // sideways > 0.3, > 2 m; front/back > 0.3; buildings
            int nSide = 0, nSide2 = 0, nDepth = 0;
            float worst = 0.f;
            int worstB = -1;
            for (size_t i = 0; i < nbld; i++) {
                const Building& b = bs.buildings[i];
                if (b.siteElem >= 0 || b.lotHx <= 0.f) continue;
                const char* kn[3] = {"envelope", "garage wing", "other part"};
                std::string key = std::string(b.style < BS_COUNT ? styleName[b.style] : "?") + (pastSide[i] > 0.3f ? std::string(" (") + kn[pastKind[i]] + ")" : "");
                auto& r = st[key];
                r[3]++;
                if (pastSide[i] > 0.3f) { r[0]++; nSide++; }
                if (pastSide[i] > 2.f) { r[1]++; nSide2++; }
                if (pastDepth[i] > 0.3f) { r[2]++; nDepth++; }
                if (pastSide[i] > worst) { worst = pastSide[i]; worstB = (int)i; }
            }
            printf("  past their own lot line: %d buildings sideways (%d more than 2 m, the furthest %.2f m: b%d), %d front or back\n", nSide, nSide2, worst, worstB, nDepth);
            for (auto& kv : st)
                if (kv.second[0] || kv.second[2]) printf("    %-36s %6d buildings, %5d sideways, %5d more than 2 m, %5d front or back\n", kv.first.c_str(), kv.second[3], kv.second[0], kv.second[1], kv.second[2]);
        }
        // the houses' garages as built: a wing or carport (a box beside the house), a driveway strip, none
        {
            int nWing = 0, nStrip = 0, nNone = 0;
            for (size_t i = 0; i < nbld; i++) {
                const Building& b = bs.buildings[i];
                if (b.siteElem >= 0 || (b.style != BS_HOUSE && b.style != BS_VILLA)) continue;
                const bool rule = (b.style == BS_HOUSE && (b.seed % 10u) < 7u) || b.style == BS_VILLA;
                bool wing = false;
                for (const FB& f : per[i]) wing |= f.kind == 1;
                if (!rule) nNone++;
                else if (wing) nWing++;
                else nStrip++;
            }
            printf("  house garages: %d wings or carports, %d driveway strips (with the frame and raised houses'), %d houses without\n", nWing, nStrip, nNone);
        }
        // the interior hosts (to compare the picks across a change)
        for (size_t i = 0; i < nbld; i++)
            if (bs.buildings[i].interior >= 0)
                printf("  interior host b%zu %s interior %d at (%.1f, %.1f)\n", i, bs.buildings[i].style < BS_COUNT ? styleName[bs.buildings[i].style] : "?", (int)bs.buildings[i].interior,
                       bs.buildings[i].c.x, bs.buildings[i].c.y);
        printf("  %d overlapping building pairs (%d of them envelope into envelope)\n", fpPairs, fpEnvEnv);
    }
    Jobs::shutdown();
    bool fail = laneHits > 0 || propHits > 0 || turnBlocked > 0 || turnObstructed > 0 || unguarded > 0.f || !zhits.empty() || steep > 0 || crossings > 0;
    printf("\nworldcheck: %s\n", fail ? "FAILED" : "passed");
    return fail ? 1 : 0;
}
