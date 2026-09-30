// World data checks (native, no GPU): drivable lanes must be free of colliders and props, every road edge with a fall
// beside it (decks, embankments, approaches) must be guarded by a collision barrier, the ground the vehicle physics
// drives on must meet the lane surfaces without steps, no road profile hides a cliff, and roads that cross without a
// junction are grade separated.
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
#include "../src/world/interiortower.cpp"
#include "../src/world/interiorgarages.cpp"
#include "../src/world/interiorresidences.cpp"
#include "../src/world/interiors.cpp"
#include <thread>
#include <unordered_map>

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
            for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                float t;
                float d = distPointSegment2D(q, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
                if (d < bd) { bd = d; bze = Lerp(e.pts[k].z, e.pts[k + 1].z, t); bs = Lerp(e.dist[k], e.dist[k + 1], t); }
            }
            bool inJunction = bs < e.cut0 || bs > e.length - e.cut1;
            if (bd <= e.halfWidth + (inJunction ? 0.f : e.sidewalk)) {
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
    Jobs::shutdown();
    bool fail = laneHits > 0 || propHits > 0 || turnBlocked > 0 || turnObstructed > 0 || unguarded > 0.f || !zhits.empty() || steep > 0 || crossings > 0;
    printf("\nworldcheck: %s\n", fail ? "FAILED" : "passed");
    return fail ? 1 : 0;
}
