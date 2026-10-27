// Native traffic/pedestrian AI harness: builds the real world (terrain, roads, buildings, collision cells), the AI
// lane graph and sidewalk graph, then runs the traffic driver model on real vehicle physics at 120 Hz with kinematic
// pedestrians, reporting metrics and rendering top-down plots.
// Build (single TU): g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/traffic_sim -lpthread
// Faster iteration (the world/sim half rarely changes):
//   g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_WORLD -c tools/traffic_sim.cpp -o /tmp/ts_world.o
//   g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_AI -c tools/traffic_sim.cpp -o /tmp/ts_ai.o
//   g++ /tmp/ts_world.o /tmp/ts_ai.o -o /tmp/traffic_sim -lpthread
// Usage: traffic_sim [--worldbin world.bin] [--lanes X Y R out.ppm] [--sim X Y R] [--cars N] [--peds N] [--minutes M]
//        [--plot out.ppm]
// --worldbin (first): the hand-built world's roads from a world.bin (build/gen/world.bin after build.sh) with what they
// author beyond the network (roadoverrides.h), on the procedural terrain the bake came from, with no buildings or sites
// (as the game's --world=hand); everything else as usual
#if __has_include("../src/world/roadoverrides.h")
#define TS_HAND 1   // (a tree with the host road loader, World::loadRoadsFile)
#endif
#if !defined(TS_SPLIT) || defined(TS_PART_WORLD)
#include "native_stubs.cpp"
#include "../src/core/math.cpp"
#include "../src/core/noise.cpp"
#include "../src/core/jobs.cpp"
#include "../src/render/mesh.cpp"
#include "../src/world/worldmap.cpp"
#ifdef TS_ROADS_CPP
#include TS_ROADS_CPP   // harness option: world roads.cpp copy with local fixes (see the harness build script)
#else
#include "../src/world/roads.cpp"
#endif
#include "../src/world/roadmesh.cpp"
#include "../src/world/buildings.cpp"
#include "../src/world/buildmesh.cpp"
#include "../src/world/propmesh.cpp"
#include "../src/world/cellgen.cpp"
#if __has_include("../src/world/sites.cpp")
#include "../src/world/sites.cpp"
#endif
#if __has_include("../src/world/transit.cpp")
#include "../src/world/transit.cpp"   // SkyLine corridor reservations (called from sites.cpp / buildings.cpp)
#endif
#if __has_include("../src/world/sitecell.cpp")
// site geometry (airport, port, landmarks, leisure, rural) streamed with the cells like in the game
#include "../src/world/sitegeo.cpp"
#include "../src/world/airport.cpp"
#include "../src/world/port.cpp"
#include "../src/world/landmarks.cpp"
#include "../src/world/leisure.cpp"
#include "../src/world/rural.cpp"
#if __has_include("../src/world/transitmesh.cpp")
#include "../src/world/transitmesh.cpp"
#endif
#include "../src/world/sitecell.cpp"
#endif
#if __has_include("../src/world/facadedetail.cpp")
#include "../src/world/facadedetail.cpp"
#endif
// enterable interiors (planned with the buildings; same order as src/main.cpp)
#if __has_include("../src/world/interiorkit.cpp")
#include "../src/world/interiorkit.cpp"
#endif
#if __has_include("../src/world/interiorfurniture.cpp")
#include "../src/world/interiorfurniture.cpp"
#endif
#if __has_include("../src/world/interiorlayouts.cpp")
#include "../src/world/interiorlayouts.cpp"
#endif
#if __has_include("../src/world/interiorhomes.cpp")
#include "../src/world/interiorhomes.cpp"
#endif
#if __has_include("../src/world/interiorvenues.cpp")
#include "../src/world/interiorvenues.cpp"
#endif
#if __has_include("../src/world/interiorshops.cpp")
#include "../src/world/interiorshops.cpp"
#endif
#if __has_include("../src/world/interiorcivic.cpp")
#include "../src/world/interiorcivic.cpp"
#endif
#if __has_include("../src/world/interiorindustrial.cpp")
#include "../src/world/interiorindustrial.cpp"
#endif
#if __has_include("../src/world/interiortower.cpp")
#include "../src/world/interiortower.cpp"
#endif
#if __has_include("../src/world/interiorgarages.cpp")
#include "../src/world/interiorgarages.cpp"
#endif
#if __has_include("../src/world/interiorresidences.cpp")
#include "../src/world/interiorresidences.cpp"
#endif
#if __has_include("../src/world/interiors.cpp")
#include "../src/world/interiors.cpp"
#endif
#include "../src/sim/physics.cpp"
#include "../src/sim/vehicle_models.cpp"
#include "../src/sim/vehicle_sim.cpp"
#ifdef TS_HAND
#include "../src/world/lz.cpp"
#include "../src/world/worldbin.cpp"
#include "../src/world/roadsbin.cpp"
#endif
// generate detailed collision cells (buildings, street furniture) around a point
void tsLoadCells(Phys::CollisionWorld& cw, vec2 c, float r) {
    int cx0 = (int)floorf((c.x - r + World::kWorldHalf) / World::kCellSize), cx1 = (int)floorf((c.x + r + World::kWorldHalf) / World::kCellSize);
    int cy0 = (int)floorf((c.y - r + World::kWorldHalf) / World::kCellSize), cy1 = (int)floorf((c.y + r + World::kWorldHalf) / World::kCellSize);
    std::vector<std::pair<int, int>> list;
    for (int y = cy0; y <= cy1; y++)
        for (int x = cx0; x <= cx1; x++) list.push_back({x, y});
    std::vector<World::CellGeometry> geo(list.size());
    Jobs::parallelFor((int)list.size(), [&](int i) { World::generateCell(list[i].first, list[i].second, true, geo[i]); });
    for (size_t i = 0; i < list.size(); i++) {
        int key = list[i].second * World::kCellsPerSide + list[i].first;
        cw.addCell(key, geo[i].collision, geo[i].props);
    }
    LOG("Loaded %zu collision cells", list.size());
}
#endif

#if !defined(TS_SPLIT) || defined(TS_PART_AI)
#ifdef TS_SPLIT
#include "../src/world/buildings.h"
#include "../src/core/jobs.h"
#include "../src/sim/physics.h"
#include "../src/sim/vehicle_models.h"
#include "../src/sim/vehicle_sim.h"
#include "../src/sim/waves.h"
#if __has_include("../src/world/sites.h")
#include "../src/world/sites.h"
#endif
void tsLoadCells(Phys::CollisionWorld& cw, vec2 c, float r);
#endif
#ifdef TS_HAND
#include "../src/world/roadsbin.h"
#include "../src/world/handworld.h"
#endif
#include "../src/game/lanes.cpp"
#include "../src/game/traffic_core.cpp"
#include "../src/game/pednav.cpp"
#include <thread>
#include <chrono>
#include <map>
#include <time.h>

namespace TS {

// ------------------------------------------------------------------------------------------------------------------
// Tiny raster canvas (PPM) for top-down plots
struct Canvas {
    int W = 0, H = 0;
    vec2 mn, mx;
    float scale = 1.f;
    std::vector<unsigned char> px;
    void init(int w, vec2 center, float radius) {
        W = H = w;
        mn = center - vec2(radius);
        mx = center + vec2(radius);
        scale = w / (2.f * radius);
        px.assign((size_t)W * H * 3, 0);
        for (size_t i = 0; i < px.size(); i += 3) {
            px[i] = 26;
            px[i + 1] = 28;
            px[i + 2] = 32;
        }
    }
    bool toPx(vec2 p, int& x, int& y) const {
        x = (int)((p.x - mn.x) * scale);
        y = H - 1 - (int)((p.y - mn.y) * scale);
        return x >= 0 && y >= 0 && x < W && y < H;
    }
    void dot(int x, int y, vec3 c, float a = 1.f) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        unsigned char* d = &px[((size_t)y * W + x) * 3];
        for (int k = 0; k < 3; k++) d[k] = (unsigned char)Clamp((1.f - a) * d[k] + a * Saturate(c[k]) * 255.f, 0.f, 255.f);
    }
    void point(vec2 p, vec3 c, int r = 0, float a = 1.f) {
        int x, y;
        toPx(p, x, y);
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r + r) dot(x + dx, y + dy, c, a);
    }
    void line(vec2 a, vec2 b, vec3 c, float a0 = 1.f, int r = 0) {
        float len = length(b - a) * scale;
        int n = Max(1, (int)(len * 1.5f));
        for (int k = 0; k <= n; k++) point(lerp(a, b, (float)k / n), c, r, a0);
    }
    void cross(vec2 p, vec3 c, int s = 4) {
        int x, y;
        toPx(p, x, y);
        for (int k = -s; k <= s; k++) {
            dot(x + k, y + k, c);
            dot(x + k, y - k, c);
            dot(x + k + 1, y + k, c);
            dot(x + k + 1, y - k, c);
        }
    }
    void save(const char* path) {
        FILE* f = fopen(path, "wb");
        if (!f) return;
        fprintf(f, "P6 %d %d 255\n", W, H);
        fwrite(px.data(), 1, px.size(), f);
        fclose(f);
    }
};

struct World3 {
    World::WorldMap map;
    World::RoadNetwork roads;
    World::BuildingSet buildings;
    Phys::CollisionWorld cw;
    AI::LaneGraph lg;
    void build() {
        map.generate();
        World::gMap = &map;
        roads.generate(map);
        World::gRoads = &roads;
        buildings.generate(map, roads);
        World::gBuildings = &buildings;
        Phys::gCollision = &cw;
        lg.build(roads);
    }
#ifdef TS_HAND
    // (a lane graph that reads the overrides gets them; an older one, a base build for comparison, builds without)
    template <typename G>
    static auto buildLanes(G& g, const World::RoadNetwork& r, const World::RoadOverrides* o, int) -> decltype(g.build(r, o), void()) {
        g.build(r, o);
    }
    template <typename G>
    static void buildLanes(G& g, const World::RoadNetwork& r, const World::RoadOverrides*, long) {
        g.build(r);
    }
    World::RoadOverrides ov;
    // the hand-built world (--worldbin): its roads and overrides read from a world.bin, the terrain the bake came from,
    // no sites (the generator laid out the procedural world's: plaza walks, pads, structures) and an empty building set
    // with its cell lists, as the game's hand mode sets them up
    // test rules written over the world's (--stopsigns: every uncontrolled junction a two-way stop, its main road the most
    // collinear pair of edges; --noredturn: no right on red at any signal)
    bool testStops = false, testNoRed = false;
    void testRules() {
        using RO = World::RoadOverrides;
        std::vector<int> at(roads.nodes.size(), -1);
        for (int r = 0; r < (int)ov.nodes.size(); r++)
            if (ov.nodes[r].node >= 0 && ov.nodes[r].node < (int)at.size()) at[ov.nodes[r].node] = r;
        int stops = 0, noRed = 0;
        for (int ni = 0; ni < (int)roads.nodes.size(); ni++) {
            const World::RoadNode& n = roads.nodes[ni];
            if (n.edges.size() < 3) continue;
            if (at[ni] < 0) {
                at[ni] = (int)ov.nodes.size();
                RO::NodeRule nr;
                nr.node = ni;
                ov.nodes.push_back(nr);
            }
            RO::NodeRule& R = ov.nodes[at[ni]];
            if (testNoRed && n.control == 2) {
                R.noRedTurn = n.edges;
                noRed++;
            }
            if (testStops && n.control == 0) {
                // the main road: the most collinear pair of edges out of the node
                std::vector<vec2> dir;
                for (int ei : n.edges) {
                    const World::RoadEdge& e = roads.edges[ei];
                    vec2 q = e.n0 == ni ? e.pts[Min((size_t)1, e.pts.size() - 1)].xy() : e.pts[e.pts.size() >= 2 ? e.pts.size() - 2 : 0].xy();
                    vec2 d = q - n.p;
                    dir.push_back(length2(d) > 1e-6f ? normalize(d) : vec2(1, 0));
                }
                int bi = 0, bj = 1;
                float best = 2.f;
                for (int i = 0; i < (int)dir.size(); i++)
                    for (int j = i + 1; j < (int)dir.size(); j++)
                        if (dot(dir[i], dir[j]) < best) {
                            best = dot(dir[i], dir[j]);
                            bi = i;
                            bj = j;
                        }
                R.control = RO::CTRL_STOP;
                R.flags = 0;
                R.stops.clear();
                for (int k = 0; k < (int)n.edges.size(); k++)
                    if (k != bi && k != bj) R.stops.push_back(n.edges[k]);
                stops++;
            }
        }
        LOG("--worldbin test rules: %d junctions made two-way stops, %d signals with no right on red", stops, noRed);
    }
    bool buildHand(const char* path) {
        map.generate();
        World::gMap = &map;
#if __has_include("../src/world/sites.h")
        if (World::gSites) *World::gSites = World::SiteSet();
#endif
        std::string err;
        std::vector<std::string> nodeIds, edgeIds;
        if (!World::loadRoadsFile(path, roads, ov, &err, &nodeIds, &edgeIds)) {
            LOG("--worldbin %s: %s", path, err.c_str());
            return false;
        }
        World::gWorldKind = World::WorldKind::Hand;
        World::gRoads = &roads;
        World::gRoadOverrides = &ov;
        buildings.cellLists.assign((size_t)World::kCellsPerSide * World::kCellsPerSide, {});
        buildings.openCellLists.assign((size_t)World::kCellsPerSide * World::kCellsPerSide, {});
        World::gBuildings = &buildings;
        Phys::gCollision = &cw;
        LOG("--worldbin %s: %zu road nodes, %zu edges (%zu with overrides), %zu node rules", path, roads.nodes.size(), roads.edges.size(), ov.edges.size(),
            ov.nodes.size());
        if (testStops || testNoRed) testRules();
        buildLanes(lg, roads, &ov, 0);
        return true;
    }
#endif
    void loadCells(vec2 c, float r) { tsLoadCells(cw, c, r); }
};

void drawRoads(Canvas& cv, const World3& w) {
    const World::RoadNetwork& rn = w.roads;
    std::vector<int> cand;
    rn.edgesInRect(cv.mn, cv.mx, cand);
    for (int ei : cand) {
        const World::RoadEdge& e = rn.edges[ei];
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), b = e.pts[k + 1].xy();
            vec2 t = normalize(b - a), n(t.y, -t.x);
            const float hwL = e.halfWidthSide(-1), hwR = e.halfWidthSide(1);   // (each side's kerb line)
            // asphalt band
            int steps = Max(1, (int)((hwL + hwR) * cv.scale));
            for (int s = 0; s <= steps; s++) {
                float o = -hwL + (hwL + hwR) * s / steps;
                cv.line(a + n * o, b + n * o, vec3(0.2f, 0.2f, 0.22f), 1.f);
            }
            for (int sd = -1; sd <= 1; sd += 2) {
                const float hw = sd > 0 ? hwR : hwL, sw = e.sidewalkSide(sd);
                if (sw > 0.f) cv.line(a + n * (sd * (hw + sw * 0.5f)), b + n * (sd * (hw + sw * 0.5f)), vec3(0.3f, 0.3f, 0.3f), 0.6f);
            }
        }
    }
}

void drawLanes(Canvas& cv, const World3& w, bool walk) {
    const AI::LaneGraph& g = w.lg;
    std::vector<int> cand;
    g.lanesNear(cv.mn, cv.mx, cand);
    for (int li : cand) {
        const AI::Lane& l = g.lanes[li];
        vec3 col = l.dir > 0 ? vec3(0.35f, 0.55f, 1.f) : vec3(1.f, 0.6f, 0.3f);
        if (l.flags & AI::LF_HIGHWAY) col = col * 0.8f + vec3(0.2f);
        float len = l.u1 - l.u0;
        int n = Max(2, (int)(len / 1.5f));
        vec2 prev = g.lanePos(li, l.u0).xy();
        for (int k = 1; k <= n; k++) {
            vec2 p = g.lanePos(li, l.u0 + len * k / n).xy();
            cv.line(prev, p, col, 0.9f);
            prev = p;
        }
        // direction arrow at the end
        vec2 e = g.lanePos(li, l.u1).xy(), t = g.laneTangent(li, l.u1);
        cv.line(e, e - t * 1.5f + vec2(t.y, -t.x) * 0.8f, col);
        cv.line(e, e - t * 1.5f - vec2(t.y, -t.x) * 0.8f, col);
        if (l.stopU >= 0.f) {
            vec2 sp = g.lanePos(li, l.stopU).xy(), st = g.laneTangent(li, l.stopU);
            cv.line(sp + vec2(st.y, -st.x) * 1.4f, sp - vec2(st.y, -st.x) * 1.4f, vec3(1, 1, 1));
        }
        for (float bu : l.busStops) cv.point(g.lanePos(li, bu, 2.f).xy(), vec3(1.f, 0.9f, 0.1f), 2);
    }
    for (const AI::Connector& c : g.conns) {
        if (c.pts.empty()) continue;
        vec2 p0 = c.pts[0].xy();
        if (p0.x < cv.mn.x || p0.y < cv.mn.y || p0.x > cv.mx.x || p0.y > cv.mx.y) continue;
        vec3 col = c.turn == AI::TK_LEFT ? vec3(1.f, 0.3f, 0.3f) : (c.turn == AI::TK_RIGHT ? vec3(0.3f, 1.f, 0.4f) : (c.turn == AI::TK_UTURN ? vec3(1.f, 0.2f, 1.f) : (c.turn == AI::TK_MERGE ? vec3(1.f, 1.f, 0.2f) : vec3(0.8f, 0.8f, 0.8f))));
        for (size_t k = 0; k + 1 < c.pts.size(); k++) cv.line(c.pts[k].xy(), c.pts[k + 1].xy(), col, 0.7f);
        if (c.holdS > 0.f) cv.point(g.pathPos(g.connPath((int)(&c - &g.conns[0])), c.holdS).xy(), vec3(1, 0, 0), 1);
    }
    if (walk) {
        for (const AI::WalkLink& L : g.walkLinks) {
            vec2 a = g.walkNodes[L.a].p.xy();
            if (a.x < cv.mn.x || a.y < cv.mn.y || a.x > cv.mx.x || a.y > cv.mx.y) continue;
            vec3 col = L.kind == AI::WL_CROSSWALK ? vec3(0.2f, 1.f, 1.f)
                     : (L.kind == AI::WL_ZEBRA ? vec3(1.f, 1.f, 0.2f)
                     : (L.kind == AI::WL_PATH ? vec3(1.f, 0.6f, 0.2f) : (L.kind == AI::WL_CORNER ? vec3(0.6f, 1.f, 0.6f) : vec3(0.4f, 0.8f, 0.4f))));
            int n = Max(2, (int)(L.length / 1.f));
            vec2 prev = g.walkPos((int)(&L - &g.walkLinks[0]), 0.f, 0.f, true).xy();
            for (int k = 1; k <= n; k++) {
                vec2 p = g.walkPos((int)(&L - &g.walkLinks[0]), L.length * k / n, 0.f, true).xy();
                cv.line(prev, p, col, 0.8f);
                prev = p;
            }
        }
    }
}


// ------------------------------------------------------------------------------------------------------------------
// Traffic + pedestrian simulation on real physics
struct SimCar {
    bool used = false;
    int model = -1;
    u32 uid = 0;
    Vehicles::VehicleState s;
    Vehicles::VehicleControls ctl;
    AI::VehicleInfo info;
    float trailTimer = 0.f;
    int trailId = -1;
    bool deadlockFlag = false;
    float lifeTime = 0.f;
    float latLogT = -100.f;
    float hungTime = 0.f;      // same ledge recovery as the game host (traffic.cpp)
    bool parked = false;       // --parked: standing in the parking strip, nobody at the wheel (no driver slot)
    bool parkBlockFlag = false;
};

struct SimPed {
    bool used = false;
    AI::Walker w;
    vec2 pos, vel;
    float z = 0.f;
    float yaw = 0.f;
    float hitCooldown = 0.f;
};

struct Event {
    vec2 p;
    int kind;   // 0 collision, 1 red light, 2 stuck, 3 ped hit, 4 deadlock, 5 static impact
    double t;
};

struct Trail {
    std::vector<vec2> pts;
    std::vector<float> spd;
};

struct Sim {
    World3* w = nullptr;
    AI::TrafficCore tc;
    AI::PedCore pc;
    std::vector<Vehicles::VehicleModel> models;
    std::vector<float> weights;
    std::vector<SimCar> cars;
    std::vector<SimPed> peds;
    std::vector<Event> events;
    std::vector<Trail> trails;
    vec2 center;
    float radius = 300.f;
    float dummyRadius = -1.f;
    double time = 0.0;
    Rng rng{12345};
    u32 uidNext = 1;
    // metrics
    long collisions = 0, hardCollisions = 0, pedHits = 0, staticImpacts = 0, deadlocks = 0, respawns = 0, propHits = 0, unhung = 0;
    int nParked = 0;
    long parkBlocks = 0;   // waits of over 30 s with a parked car as the obstacle
    // highway lane keeping above 15 m/s (lanes and the links between them; [0] cars, [1] wheelbase over 3.3 m): ticks,
    // squared error, ticks over 0.5 m off; in the lane next to the median, how far left of the planned line (max) and
    // the ticks over 0.3 m left of it
    // ([k][1]: during a lane change)
    long hwyN[2][2] = {}, hwyOver[2][2] = {}, hwyLeftN[2][2] = {}, hwyLeftOver[2][2] = {};
    double hwyE2[2][2] = {}, hwyMax[2][2] = {}, hwyLeftMax[2][2] = {};
    double vehSeconds = 0.0;
    double clsSpeedSum[World::RC_COUNT] = {}, clsTime[World::RC_COUNT] = {};
    double aiTime = 0.0, aiMax = 0.0, physTime = 0.0, pedTime = 0.0;
    long aiTicks = 0, physSteps = 0;
    std::vector<double> aiSamples;
    std::map<long long, double> pairLast;
    long redBefore = 0;
    bool verbose = false;
    int watchCar = -1;
    // --overshoot: every car coming up to a written stop sign (--worldbin's, --stopsigns) or an all-way stop, its stop not
    // made yet and none close ahead, is made to roll on over its line - at 2-3 m/s, a rolling stop gone long - until its
    // front is 0.8 m past it; then the AI has it again. Counted after, by kind: whether it stops before its front is 4 m
    // past the line (and how long it then waits to go), or runs on; at an all-way stop, whether it then went before an
    // earlier arrival still waiting there with a movement across its own (its turn not waited for)
    bool overshoot = false;
    struct Overshoot {
        int conn = -1, lane = -1, phase = 0;   // phase 1: being run past, 2: released, 3: done
        int kind = 0;                          // 0 a stop sign, 1 an all-way stop
        float stopAt = 0.f, stoppedT = -1.f, vRelease = 0.f, vAt2 = -1.f;
    };
    std::vector<Overshoot> overshoots;
    long ovForced[2] = {0, 0}, ovStopped[2] = {0, 0}, ovRan[2] = {0, 0}, ovAhead = 0;
    double ovWait[2] = {0.0, 0.0}, ovStopPast[2] = {0.0, 0.0};
    void overshootTick(int i, SimCar& c, const AI::Driver& d) {
        const AI::LaneGraph& G = w->lg;
        const int NL = (int)G.lanes.size();
        if ((int)overshoots.size() <= i) overshoots.resize(i + 1);
        Overshoot& o = overshoots[i];
        // the front bumper past the stop line of the lane it came up on (on that lane or the connector after it)
        auto past = [&]() -> float {
            const AI::Lane& L = G.lanes[o.lane];
            if (d.path == o.lane) return d.u + d.info.frontLen - L.stopU;
            if (d.path == G.connPath(o.conn)) return L.u1 - L.stopU + d.u + d.info.frontLen;
            return 1e9f;
        };
        if (o.phase == 0 || o.phase == 3) {
            if (d.path >= NL || d.routeLen < 1 || d.route[0] < NL) return;
            const int cn = d.route[0] - NL;
            if (o.phase == 3 && o.conn == cn) return;
            const AI::Connector& cc = G.conns[cn];
            const AI::Lane& L = G.lanes[d.path];
            const bool sign = G.nodes[cc.node].approaches[cc.approach].stopSign, allWay = G.nodes[cc.node].control == 1;
            if (!(sign || allWay) || L.stopU < 0.f || d.mode != AI::DM_NORMAL) return;
            const float x0 = d.u + d.info.frontLen - L.stopU;
            if (x0 > -6.f && x0 < -0.5f && c.s.speed() > 1.f && c.s.speed() < 4.f && d.obstDist > 12.f && !d.stopDone) {   // (slowing for the line, none close ahead)
                o = Overshoot();
                o.conn = cn;
                o.lane = d.path;
                o.phase = 1;
                o.kind = allWay ? 1 : 0;
            }
            return;
        }
        const float x = past();
        if (x > 1e8f) {   // (off that way: a lane change, a respawn)
            o.phase = 3;
            return;
        }
        if (o.phase == 1) {
            if (d.obstDist < 6.f) {   // (one close ahead after all: not run into it - left out)
                o.phase = 3;
                return;
            }
            if (x < 0.8f) {
                const float v = c.s.speed();
                c.ctl.handbrake = false;
                c.ctl.throttle = v < 2.f ? 0.4f : 0.f;
                c.ctl.brake = v > 3.f ? 0.3f : 0.f;
            } else {
                o.phase = 2;
                o.vRelease = c.s.speed();
                ovForced[o.kind]++;
            }
            return;
        }
        if (o.vAt2 < 0.f && x >= 2.f) o.vAt2 = c.s.speed();
        // released
        if (o.stoppedT < 0.f) {
            if (c.s.speed() < 0.5f && x < 4.f) {
                o.stoppedT = (float)time;
                o.stopAt = x;
                ovStopped[o.kind]++;
                ovStopPast[o.kind] += x;
            } else if (x >= 4.f) {
                ovRan[o.kind]++;
                o.phase = 3;
                if (verbose)
                    LOG("OVERSHOOT ran on: car %d t=%.1f released at %.1f m/s, %.1f m/s 2 m past its line, stop made %d, committed %d", i, time, o.vRelease, o.vAt2, (int)d.stopDone,
                        (int)d.committed);
            }
        } else if (c.s.speed() > 1.f) {
            ovWait[o.kind] += time - o.stoppedT;
            o.phase = 3;
            if (o.kind == 1) {
                // going now: anyone at a line here who stopped before us, still waiting, with a movement across ours?
                const AI::Connector& cc = G.conns[o.conn];
                bool ahead = false;
                for (int j = 0; j < (int)tc.drivers.size() && !ahead; j++) {
                    const AI::Driver& q = tc.drivers[j];
                    if (j == i || !q.active || q.path >= NL || q.routeLen < 1 || q.route[0] < NL || !q.stopDone || q.committed) continue;
                    const int qc = q.route[0] - NL;
                    if (G.conns[qc].node != cc.node || q.gateConn != qc || q.arrival > d.arrival - 0.05f) continue;
                    for (const AI::Conflict& xc : cc.conflicts) ahead |= xc.other == qc;
                }
                ovAhead += ahead;
            }
        }
    }
    float watchT0 = 0.f, watchT1 = 0.f;
    float bikeScale = 1.f;   // --bikes: more (or fewer) motorbikes and scooters in the mix
    // --respond x y t0: at t0 the car nearest to 150 m from (x, y) answers a call there with the siren on (the police
    // response driving: DM_EMERGENCY to the destination, as police.cpp sets it up); its progress every 2 s and the time
    // it takes to get within 26 m (where officers get out for a suspect on foot), up to three calls one after another
    vec2 respondGoal;
    float respondAt = -1.f;
    int respondCar = -1, respondCalls = 0;
    double respondStart = 0.0, respondLog = 0.0;

    void init(World3* world, vec2 c, float r, int ncars, int npeds) {
        w = world;
        center = c;
        radius = r;
        tc.init(&w->lg);
        pc.init(&w->lg, &tc);
        int n = Vehicles::modelCount();
        models.resize(n);
        for (int i = 0; i < n; i++) Vehicles::buildModel(i, models[i]);
        for (int i = 0; i < n; i++) {
            float wt = models[i].spawnWeight;
            if (models[i].cls >= Vehicles::VC_BOAT) wt = 0.f;
            if (models[i].cls == Vehicles::VC_POLICE) wt *= 0.3f;
            if (models[i].cls == Vehicles::VC_MOTORBIKE || models[i].cls == Vehicles::VC_SCOOTER) wt *= bikeScale;
            weights.push_back(wt);
        }
        cars.resize(ncars);
        tc.drivers.resize(ncars);
        for (int i = 0; i < ncars; i++) spawnCar(i, true);
        peds.resize(npeds);
        for (int i = 0; i < npeds; i++) spawnPed(i);
    }

    int pickModel() {
        float total = 0.f;
        for (float x : weights) total += x;
        float r = rng.f() * total;
        for (int i = 0; i < (int)weights.size(); i++) {
            r -= weights[i];
            if (r <= 0.f) return i;
        }
        return 0;
    }

    bool spawnCar(int i, bool initial) {
        SimCar& c = cars[i];
        for (int attempt = 0; attempt < 60; attempt++) {
            vec2 p = center + rng.inCircle() * radius;
            float u = 0.f;
            int lane = w->lg.nearestLane(p, vec2(0, 0), 25.f, &u);
            if (lane < 0) continue;
            const AI::Lane& L = w->lg.lanes[lane];
            if (L.flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC)) continue;
            if (L.u1 - L.u0 < 40.f) continue;
            u = Clamp(u, L.u0 + 6.f, L.u1 - 30.f);   // room to stop for a red light ahead
            if (u < L.u0 || u > L.u1) continue;
            vec3 pos = w->lg.lanePos(lane, u);
            vec2 tl = w->lg.laneTangent(lane, u);
            // (motorways and ramps: in at most of the lane's speed; elsewhere a gentle start)
            bool fast = (L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP)) != 0;
            float v0 = fast ? L.speed * 0.8f : Min(L.speed * 0.6f, 9.f);
            bool ok = true;
            for (int k = 0; k < (int)cars.size() && ok; k++) {
                if (k == i || !cars[k].used) continue;
                vec3 q = cars[k].s.body.pos.toVec3();
                if (length(q.xy() - pos.xy()) < 11.f && fabsf(q.z - pos.z) < 3.f) ok = false;
                // not in front of a faster car coming up the same lane (it could not stop for a car dropped in its path), nor
                // at speed behind a slower one (the new car could not): the gap either needs is its braking distance at a
                // firm 4 m/s2, a second of reaction and a car length
                // (a car on the same lane by its lane position - exact round a curve, where a queue further on is well off
                //  the straight line; others by the straight line ahead / behind)
                vec2 rel = q.xy() - pos.xy();
                float along = dot(rel, tl), vk = dot(cars[k].s.body.vel.xy(), tl);
                const AI::Driver* dk = tc.get(k);
                bool sameLane = dk && dk->path == lane;
                if (sameLane) {
                    along = dk->u - u;
                    vk = cars[k].s.speed();
                }
                if (fabsf(along) < 150.f && (sameLane || (fabsf(dot(rel, AI::rightOf(tl))) < 2.5f && fabsf(q.z - pos.z) < 3.f))) {
                    float vBack = along < 0.f ? vk : v0, vFront = along < 0.f ? v0 : vk;
                    if (vBack > vFront + 1.f && fabsf(along) < (vBack * vBack - Max(vFront, 0.f) * Max(vFront, 0.f)) / 8.f + vBack + 8.f) ok = false;
                }
            }
            if (!ok) continue;
            if (!initial && length(pos.xy() - center) < radius * 0.5f) continue;
            int m = pickModel();
            c = SimCar();
            c.used = true;
            c.model = m;
            c.uid = uidNext++;
            vec2 t = tl;
            Vehicles::initVehicle(c.s, models[m], m, dvec3(pos.x, pos.y, pos.z + 0.3), AI::dirYaw(t));
            c.info = AI::makeVehicleInfo(models[m], c.s);
            c.s.body.vel = vec3(t * v0, 0.f);
            for (int k = 0; k < c.s.wheelCount; k++) c.s.wheels[k].spinVel = v0 / Max(models[m].wheels[k].radius, 0.2f);
            tc.attach(i, c.uid, hash32(c.uid * 7919u), c.info, lane, u);
            c.trailId = (int)trails.size();
            trails.push_back(Trail());
            return true;
        }
        c.used = false;
        tc.detach(i);
        return false;
    }

    void spawnPed(int i) {
        SimPed& p = peds[i];
        for (int attempt = 0; attempt < 40; attempt++) {
            vec2 q = center + rng.inCircle() * radius * 0.9f;
            AI::Walker wk;
            if (!pc.place(wk, q, rng.next(), 40.f)) continue;
            p = SimPed();
            p.used = true;
            p.w = wk;
            vec3 wp = w->lg.walkPos(wk.link, wk.x, wk.lat, wk.fromA);
            p.pos = wp.xy();
            p.z = wp.z;
            return;
        }
        p.used = false;
    }

    // --parked N: cars standing in the parking strips the way the game's population lays them out (population.cpp, the
    // parked cars round the player): the kerb lane of a street, avenue or lane with a strip of 1.8 m or more, centred in
    // the strip, a hair off the lane's heading, any model the game parks (no buses, trucks, police...) narrow enough for
    // the strip with half a metre to spare. Nobody at the wheel: no driver slot, never respawned. On nt_ai102c's soak a
    // Compactor parked like this held a car in a one-lane street for 145 s
    void spawnParked(int want) {
        std::vector<float> pw(models.size(), 0.f);
        float total = 0.f;
        for (int i = 0; i < (int)models.size(); i++) {
            switch (models[i].cls) {
                case Vehicles::VC_BOAT: case Vehicles::VC_JETSKI: case Vehicles::VC_AIRBOAT: case Vehicles::VC_PLANE: case Vehicles::VC_HELI: case Vehicles::VC_BUS:
                case Vehicles::VC_FIRETRUCK: case Vehicles::VC_AMBULANCE: case Vehicles::VC_POLICE: case Vehicles::VC_TRUCK: break;
                default: pw[i] = models[i].spawnWeight;
            }
            total += pw[i];
        }
        for (int iter = 0; iter < want * 40 && nParked < want && total > 0.f; iter++) {
            vec2 probe = center + rng.inCircle() * radius;
            float u = 0.f;
            int lane = w->lg.nearestLane(probe, vec2(0), 40.f, &u);
            if (lane < 0) continue;
            for (int guard = 0; guard < 6 && w->lg.lanes[lane].right >= 0; guard++) lane = w->lg.lanes[lane].right;
            const AI::Lane& L = w->lg.lanes[lane];
            if (L.cls != World::RC_STREET && L.cls != World::RC_AVENUE && L.cls != World::RC_LANE) continue;
#ifndef TS_NO_SECTIONS
            // (population.cpp's rule: the parking along this lane's kerb - the class's shoulder, or as authored)
            const float parkW = w->lg.parkWidth(lane), parkLat = w->lg.parkLat(lane);
#else
            const World::RoadClassInfo& info = World::roadInfo((World::RoadClass)L.cls);
            const float parkW = info.shoulder, parkLat = L.width * 0.5f + info.shoulder * 0.5f;
#endif
            if (parkW < 1.8f || L.u1 - L.u0 < 30.f) continue;
            u = Clamp(u, L.u0 + 9.f, L.u1 - 12.f);
            bool nearStop = false;
            for (float bs : L.busStops)
                if (fabsf(bs - u) < 18.f) nearStop = true;
            if (nearStop) continue;
            float lat = parkLat;
            vec3 c = w->lg.lanePos(lane, u, lat);
            if (World::roadWorkZoneAt(c.xy())) continue;
            bool close = false;
            for (const SimCar& o : cars)
                if (o.used && length(o.s.body.pos.toVec3().xy() - c.xy()) < 6.5f) close = true;
            if (close) continue;
            float pick = rng.f() * total;
            int m = -1;
            for (int i = 0; i < (int)pw.size(); i++) {
                pick -= pw[i];
                if (pick <= 0.f && pw[i] > 0.f) {
                    m = i;
                    break;
                }
            }
            if (m < 0 || models[m].boxHalf.x > parkW * 0.5f + 0.1f) continue;
            vec2 t = w->lg.laneTangent(lane, u);
            SimCar pc;
            pc.used = true;
            pc.parked = true;
            pc.model = m;
            pc.uid = uidNext++;
            Vehicles::initVehicle(pc.s, models[m], m, dvec3(c.x, c.y, c.z + 0.25), AI::dirYaw(t) + (rng.f() - 0.5f) * 0.05f);
            pc.info = AI::makeVehicleInfo(models[m], pc.s);
            pc.ctl.handbrake = true;
            cars.push_back(pc);
            nParked++;
        }
        tc.drivers.resize(cars.size());   // (the parked cars' slots stay inactive: tc.get() gives nullptr)
    }


    int snapCount = 0;
    int snapLimit = 0;
    std::string snapDir;
    void snapshot(const char* tag, vec2 at) {
        if (snapCount >= snapLimit) return;
        Canvas cv;
        cv.init(700, at, 28.f);
        drawRoads(cv, *w);
        drawLanes(cv, *w, true);
        const AI::LaneGraph& G = w->lg;
        for (int i = 0; i < (int)cars.size(); i++) {
            SimCar& c = cars[i];
            if (!c.used) continue;
            vec2 p = c.s.body.pos.toVec3().xy();
            if (length(p - at) > 45.f) continue;
            const Vehicles::VehicleModel& m = models[c.model];
            mat3 R = c.s.body.rotMat();
            vec3 ctr3 = c.s.body.pos.toVec3() + R * m.boxCenter;
            vec2 f = normalize(c.s.forward().xy()), r = AI::rightOf(f);
            vec2 ctr = ctr3.xy();
            vec2 q[4] = {ctr + f * m.boxHalf.y + r * m.boxHalf.x, ctr + f * m.boxHalf.y - r * m.boxHalf.x, ctr - f * m.boxHalf.y - r * m.boxHalf.x,
                         ctr - f * m.boxHalf.y + r * m.boxHalf.x};
            float v = c.s.speed();
            vec3 col = v < 0.3f ? vec3(1.f, 0.3f, 0.3f) : lerp(vec3(0.4f, 0.6f, 1.f), vec3(1.f, 1.f, 0.3f), Saturate(v / 12.f));
            for (int k = 0; k < 4; k++) cv.line(q[k], q[(k + 1) % 4], col, 1.f, 1);
            cv.line(ctr, ctr + f * (m.boxHalf.y + 1.2f), col, 1.f);
            AI::Driver* d = tc.get(i);
            if (!d) continue;
            // route ahead
            vec2 prev = G.pathPos(d->path, d->u).xy();
            float acc = 0.f;
            int path = d->path;
            float u = d->u;
            int ri = -1;
            while (acc < 30.f) {
                float end = G.pathLength(path);
                for (float uu = u; uu < end && acc < 30.f; uu += 1.5f, acc += 1.5f) {
                    vec2 pp = G.pathPos(path, uu).xy();
                    cv.line(prev, pp, vec3(0.f, 1.f, 1.f), 0.6f);
                    prev = pp;
                }
                ri++;
                if (ri >= d->routeLen) break;
                path = d->route[ri];
                u = G.isLane(path) ? G.lanes[path].u0 : 0.f;
            }
            if (d->obstBody >= 0 && d->obstBody < (int)tc.bodies.size() && d->obstDist < 30.f) cv.line(ctr, tc.bodies[d->obstBody].pos, vec3(1.f, 0.6f, 0.f), 1.f);
        }
        for (auto& pd : peds)
            if (pd.used && length(pd.pos - at) < 45.f) cv.point(pd.pos, vec3(0.2f, 1.f, 0.3f), 2);
        std::string path = snapDir + StrFormat("snap_%02d_%s_%.0f.ppm", snapCount, tag, time);
        cv.save(path.c_str());
        snapCount++;
    }

    void fillBodies() {
        tc.bodies.clear();
        for (int i = 0; i < (int)cars.size(); i++) {
            SimCar& c = cars[i];
            if (!c.used) continue;
            const Vehicles::VehicleModel& m = models[c.model];
            AI::Body b;
            mat3 R = c.s.body.rotMat();
            vec3 ctr = c.s.body.pos.toVec3() + R * m.boxCenter;
            b.pos = ctr.xy();
            b.z = (float)c.s.body.pos.z;
            vec3 f = c.s.forward();
            b.fwd = normalize(vec2(f.x, f.y) + vec2(1e-6f, 0.f));
            AI::Driver* d = tc.get(i);
            if (d && d->dummy) b.vel = b.fwd * d->vDummy;
            else b.vel = c.s.body.vel.xy();
            b.speed = length(b.vel);
            b.halfLen = m.boxHalf.y;
            b.halfWid = m.boxHalf.x;
            b.host = i;
            b.driver = c.parked ? -1 : i;
            b.kind = AI::BK_CAR;
            b.flags = c.parked ? AI::BF_PARKED : AI::BF_AI | (d && d->dummy ? AI::BF_DUMMY : 0) | (i == respondCar ? AI::BF_SIREN | AI::BF_POLICE : 0);
            tc.bodies.push_back(b);
        }
        for (int i = 0; i < (int)peds.size(); i++) {
            SimPed& p = peds[i];
            if (!p.used) continue;
            AI::Body b;
            b.pos = p.pos;
            b.z = p.z;
            b.vel = p.vel;
            b.speed = length(p.vel);
            b.fwd = AI::yawDir(p.yaw);
            b.halfLen = b.halfWid = 0.3f;
            b.host = i;
            b.kind = AI::BK_PED;
            b.flags = p.w.state == AI::WS_CROSSING ? AI::BF_CROSSING : 0;
            tc.bodies.push_back(b);
        }
    }

    static double cpuMs() {
        timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return ts.tv_sec * 1000.0 + ts.tv_nsec * 1e-6;
    }
    void respondTick() {
        if (respondAt < 0.f || time < respondAt) return;
        if (respondCar < 0) {
            if (respondCalls >= 3) return;
            // (as the game's dispatch: a car 120-180 m off with a short way in - at most 1.7 times the straight line -
            //  else the one nearest 150 m)
            float best = 1e9f;
            int fallback = -1;
            float fbBest = 1e9f;
            for (int i = 0; i < (int)cars.size(); i++) {
                AI::Driver* d = tc.get(i);
                if (!cars[i].used || !d || d->dummy || cars[i].info.bike || cars[i].s.speed() < 2.f) continue;   // (a car on the move)
                float dist = length(cars[i].s.body.pos.toVec3().xy() - respondGoal);
                float e = fabsf(dist - 150.f);
                if (e < fbBest) {
                    fbBest = e;
                    fallback = i;
                }
                if (e > 30.f) continue;
                AI::Driver tmp;
                tmp.path = d->path;
                tmp.u = d->u;
                tc.setDestination(tmp, respondGoal);
                if (tmp.destEdges.empty()) continue;
                float rl = 0.f;
                for (int ed : tmp.destEdges) rl += w->roads.edges[ed].length;
                rl -= 0.5f * w->roads.edges[tmp.destEdges.front()].length;
                if (tmp.destEdges.size() >= 2) rl -= 0.5f * w->roads.edges[tmp.destEdges.back()].length;
                if (rl > dist * 1.7f + 40.f) continue;
                if (rl < best) {
                    best = rl;
                    respondCar = i;
                }
            }
            if (respondCar < 0) respondCar = fallback;
            if (respondCar < 0) return;
            AI::Driver* d = tc.get(respondCar);
            d->mode = AI::DM_EMERGENCY;
            d->pers.speedFactor = 1.2f;
            d->pers.accel = Max(d->pers.accel, 3.6f);
            d->pers.latAcc = Max(d->pers.latAcc, 4.2f);
            tc.setDestination(*d, respondGoal);
            respondStart = time;
            respondLog = time;
            respondCalls++;
            LOG("RESPOND call %d: car %d answers from %.0f m (%zu edges planned)", respondCalls, respondCar,
                length(cars[respondCar].s.body.pos.toVec3().xy() - respondGoal), d->destEdges.size());
            return;
        }
        SimCar& c = cars[respondCar];
        AI::Driver* d = tc.get(respondCar);
        if (!c.used || !d) {
            respondCar = -1;
            return;
        }
        if (d->mode != AI::DM_EMERGENCY) d->mode = AI::DM_EMERGENCY;
        if (!d->hasDest || length(d->dest - respondGoal) > 40.f || d->destRecalc <= 0.f) tc.setDestination(*d, respondGoal);
        float dist = length(c.s.body.pos.toVec3().xy() - respondGoal);
        if (time - respondLog >= 2.0) {
            respondLog = time;
            const AI::LaneGraph& G = w->lg;
            LOG("RESPOND t=%.0f car %d: %.0f m to go, %.1f m/s (target %.1f), stop %.1f obst %.1f, stuck %.1f, %s %d", time - respondStart, respondCar, dist, c.s.speed(),
                d->vTarget, d->stopDist, d->obstDist, d->stuckTime, G.isLane(d->path) ? "lane" : "conn", G.isLane(d->path) ? d->path : d->path - (int)G.lanes.size());
        }
        if (dist < 26.f || time - respondStart > 150.0) {
            LOG("RESPOND call %d: %s after %.1f s", respondCalls, dist < 26.f ? "there" : "NOT THERE", time - respondStart);
            d->mode = AI::DM_NORMAL;
            d->hasDest = false;
            respondCar = -1;
            respondAt = (float)time + 5.f;
        }
    }
    void aiTick(float dt) {
        double c0 = cpuMs();
        respondTick();
        fillBodies();
        tc.beginTick(time);
        pc.time = time;
        for (int i = 0; i < (int)cars.size(); i++) {
            SimCar& c = cars[i];
            if (!c.used) continue;
            AI::Driver* d = tc.get(i);
            if (!d) continue;
            if (dummyRadius > 0.f) {
                float dist = length(c.s.body.pos.toVec3().xy() - center);
                bool inBox = d->path >= (int)w->lg.lanes.size();
                if (!d->dummy && dist > dummyRadius + 15.f && !inBox && d->recoverTimer <= 0.f) tc.toDummy(i, c.s);
                else if (d->dummy && dist < dummyRadius) tc.toPhysics(i, c.s);
            }
            long redB = tc.stats.redViolations, stuckB = tc.stats.stuckEvents;
            AI::DriveOut out;
            tc.drive(i, c.s, dt, out);
            c.ctl = out.ctl;
            if (overshoot) overshootTick(i, c, *d);
            if (i == watchCar && time >= watchT0 && time <= watchT1 && ((int)(time * 60.0) % 6) == 0) {
                const AI::LaneGraph& G = w->lg;
                std::string where = G.isLane(d->path) ? StrFormat("lane %d u %.1f/%.1f stopU %.1f", d->path, d->u, G.lanes[d->path].u1, G.lanes[d->path].stopU)
                                                       : StrFormat("conn %d (turn %d node %d) u %.1f/%.1f", d->path - (int)G.lanes.size(), G.conn(d->path).turn, G.conn(d->path).node, d->u, G.conn(d->path).length);
                where += StrFormat(" fp %.0f fu %.2f lat %.2f nudge %.2f lc %d (%.2f->%.2f from u %.1f len %.1f)", d->diag[4], d->diag[5], d->lat, d->nudge, d->lcLane, d->lcFrom, d->lcTo, d->lcU0, d->lcLen);
                where += StrFormat(" pos (%.1f %.1f) latErr %.2f diag %.2f %.2f %.2f %.2f steer %.2f vF %.2f yawRate %.2f heading %.3f pathDir %.3f steerOut %.2f imp %.0f", c.s.body.pos.x, c.s.body.pos.y, d->latErr, d->diag[0], d->diag[1], d->diag[2], d->diag[3], c.ctl.steer,
                                   c.s.forwardSpeed(), c.s.body.angVel.z, atan2f(c.s.forward().y, c.s.forward().x), atan2f(G.pathTangent(d->path, d->u).y, G.pathTangent(d->path, d->u).x), c.s.steerOut, c.s.impactImpulse);
                std::string nxt = d->routeLen > 0 && !G.isLane(d->route[0]) ? StrFormat("next conn %d sig %d", d->route[0] - (int)G.lanes.size(), (int)G.movementSignal(G.conn(d->route[0]).node, G.conn(d->route[0]).approach, G.conn(d->route[0]).turn, time)) : std::string("next -");
                if (d->obstBody >= 0 && d->obstBody < (int)tc.bodies.size() && tc.bodies[d->obstBody].kind == AI::BK_PED) {
                    const SimPed& sp = peds[tc.bodies[d->obstBody].host];
                    nxt += StrFormat(" [ped %d state %d link %d kind %d x %.2f/%.2f fromA %d pos %.2f %.2f vel %.2f %.2f]", tc.bodies[d->obstBody].host, sp.w.state, sp.w.link,
                                     sp.w.link >= 0 ? G.walkLinks[sp.w.link].kind : -1, sp.w.x, sp.w.link >= 0 ? G.walkLinks[sp.w.link].length : 0.f, (int)sp.w.fromA, sp.pos.x, sp.pos.y, sp.vel.x, sp.vel.y);
                }
                LOG("WATCH t=%.2f car %d %s %s v %.1f vT %.1f stop %.1f obst %.1f(%d) gateConn %d committed %d amberGo %d stopDone %d mode %d thr %.2f brk %.2f stk %.2f rec %.2f col %d [%s wb %.2f maxSteer %.2f]", time, i, where.c_str(), nxt.c_str(), c.s.speed(), d->vTarget, d->stopDist,
                    d->obstDist, d->obstBody, d->gateConn, (int)d->committed, (int)d->amberGo, (int)d->stopDone, d->mode, c.ctl.throttle, c.ctl.brake, d->stuckTime, d->recoverTimer, c.s.impactCollider,
                    models[c.model].name.c_str(), c.info.wheelbase, c.info.maxSteer);
            }
            if (tc.stats.redViolations > redB) {
                events.push_back({c.s.body.pos.toVec3().xy(), 1, time});
                if (verbose) {
                    const AI::Driver& dd = *tc.get(i);
                    std::string hist;
                    if (dd.routeLen > 0 && !w->lg.isLane(dd.route[0])) {
                        const AI::Connector& cc = w->lg.conn(dd.route[0]);
                        for (float back = 0.f; back <= 5.f; back += 0.5f) hist += StrFormat("%d", (int)w->lg.movementSignal(cc.node, cc.approach, cc.turn, time - back));
                        hist += StrFormat(" turn %d node %d ctl %d", cc.turn, cc.node, w->lg.nodes[cc.node].control);
                    }
                    LOG("RED t=%.1f car %d at %.1f %.1f v=%.1f path %d u %.1f stopDist %.1f vT %.1f amberGo %d decided %d committed %d stopDone %d mode %d runsAmber %d sig(now..-5s) %s", time, i, c.s.body.pos.x, c.s.body.pos.y, c.s.speed(), dd.path, dd.u, dd.stopDist, dd.vTarget,
                        (int)dd.amberGo, dd.amberDecided, (int)dd.committed, (int)dd.stopDone, dd.mode, (int)dd.pers.runsAmber, hist.c_str());
                }
            }
            if (tc.stats.stuckEvents > stuckB) {
                events.push_back({c.s.body.pos.toVec3().xy(), 2, time});
                if (verbose) {
                    vec3 cp = c.s.body.pos.toVec3();
                    float laneZ = w->lg.pathPos(d->path, d->u).z;
                    Phys::GroundHit gh = Phys::gCollision->ground(cp.x, cp.y, cp.z + 2.f, 3.f);
                    std::string wh;
                    for (int k = 0; k < c.s.wheelCount; k++) {
                        vec3 wp = cp + c.s.wheels[k].contactPos;
                        Phys::GroundHit wg = Phys::gCollision->ground(wp.x, wp.y, cp.z + 2.f, 3.f);
                        wh += StrFormat(" w%d(%s z %.2f g %.2f)", k, c.s.wheels[k].contact ? "on" : "off", wp.z, wg.z);
                    }
                    LOG("STUCK t=%.1f car %d at %.1f %.1f z %.2f laneZ %.2f groundZ %.2f path %d u %.1f lat %.2f vT %.1f stop %.1f obst %.1f(%d) up %.2f gear %d wheels %d sleep %d imp %.0f col %d thr %.2f |%s", time, i, cp.x, cp.y, cp.z, laneZ, gh.z, d->path, d->u, d->latErr, d->vTarget, d->stopDist, d->obstDist, d->obstBody, c.s.up().z,
                        c.s.gear, c.s.wheelsOnGround, (int)c.s.sleeping, c.s.impactImpulse, c.s.impactCollider, c.ctl.throttle, wh.c_str());
                    if (c.s.impactCollider >= 0) {
                        const Phys::Collider& cl = w->cw.collider(c.s.impactCollider);
                        vec3 f = c.s.forward();
                        LOG("   collider %d kind %d prop %d c (%.2f %.2f %.2f) he (%.2f %.2f %.2f) ax (%.2f %.2f) | car heading (%.2f %.2f) %s", c.s.impactCollider, cl.kind, cl.propIndex, cl.c.x, cl.c.y, cl.c.z,
                            cl.he.x, cl.he.y, cl.he.z, cl.ax.x, cl.ax.y, f.x, f.y, models[c.model].name.c_str());
                    }
                }
            }
            if (d->waitTime > 60.f && !c.deadlockFlag) {
                c.deadlockFlag = true;
                deadlocks++;
                events.push_back({c.s.body.pos.toVec3().xy(), 4, time});
                snapshot("deadlock", c.s.body.pos.toVec3().xy());
                if (verbose) {
                    int node = d->gateNode;
                    const AI::LaneGraph& G = w->lg;
                    std::string where;
                    if (G.isLane(d->path)) where = StrFormat("lane e%d d%d i%d u %.1f/%.1f stopU %.1f next %s", G.lanes[d->path].edge, G.lanes[d->path].dir, G.lanes[d->path].index, d->u, G.lanes[d->path].u1, G.lanes[d->path].stopU,
                                                                d->routeLen > 0 && !G.isLane(d->route[0]) ? StrFormat("conn %d turn %d ctl %d", d->route[0] - (int)G.lanes.size(), G.conn(d->route[0]).turn, G.nodes[G.conn(d->route[0]).node].control).c_str() : "-");
                    else where = StrFormat("conn %d turn %d appr %d node %d ctl %d u %.1f/%.1f hold %.1f committed %d", d->path - (int)G.lanes.size(), G.conn(d->path).turn, G.conn(d->path).approach, G.conn(d->path).node, G.nodes[G.conn(d->path).node].control, d->u, G.conn(d->path).length, G.conn(d->path).holdS, d->committed);
                    std::string ob;
                    if (d->obstBody >= 0 && d->obstBody < (int)tc.bodies.size() && tc.bodies[d->obstBody].kind == AI::BK_PED) {
                        const SimPed& sp = peds[tc.bodies[d->obstBody].host];
                        ob = StrFormat(" [ped state %d link kind %d x %.1f/%.1f pos %.1f %.1f vel %.2f]", sp.w.state, sp.w.link >= 0 ? G.walkLinks[sp.w.link].kind : -1, sp.w.x,
                                       sp.w.link >= 0 ? G.walkLinks[sp.w.link].length : 0.f, sp.pos.x, sp.pos.y, length(sp.vel));
                    }
                    LOG("DEADLOCK t=%.1f car %d at %.1f %.1f %s gateNode %d stopDist %.1f obst %.1f (body %d)%s sig %d", time, i, c.s.body.pos.x, c.s.body.pos.y, where.c_str(), node, d->stopDist, d->obstDist, d->obstBody, ob.c_str(), (int)tc.signalFor(*d));
                    // follow the chain of blockers
                    int cur = d->obstBody;
                    for (int hop = 0; hop < 5 && cur >= 0 && cur < (int)tc.bodies.size() && tc.bodies[cur].kind == AI::BK_CAR; hop++) {
                        int ci = tc.bodies[cur].host;
                        AI::Driver* cd = tc.get(ci);
                        if (!cd) break;
                        std::string w2 = G.isLane(cd->path) ? StrFormat("lane e%d d%d i%d u %.1f/%.1f", G.lanes[cd->path].edge, G.lanes[cd->path].dir, G.lanes[cd->path].index, cd->u, G.lanes[cd->path].u1)
                                                            : StrFormat("conn %d turn %d node %d u %.1f/%.1f hold %.1f committed %d", cd->path - (int)G.lanes.size(), G.conn(cd->path).turn, G.conn(cd->path).node, cd->u, G.conn(cd->path).length, G.conn(cd->path).holdS, cd->committed);
                        LOG("   -> car %d %s v=%.1f vT=%.1f stop %.1f obst %.1f(%d) wait %.1f gateConn %d gateNode %d stopDone %d lat %.2f", ci, w2.c_str(), cars[ci].s.speed(), cd->vTarget, cd->stopDist, cd->obstDist, cd->obstBody, cd->waitTime, cd->gateConn, cd->gateNode, (int)cd->stopDone, cd->latErr);
                        if (cd->obstBody == cur) break;
                        cur = cd->obstBody;
                    }
                }
            }
            if (d->waitTime < 5.f) c.deadlockFlag = false;
            // controller sanity: the Stanley heading error must match the real heading error on straight lanes
            if (verbose && w->lg.isLane(d->path) && (int)d->diag[4] == d->path && c.s.speed() > 1.f && time - c.latLogT > 5.0) {
                vec2 tf = w->lg.pathTangent(d->path, d->diag[5]);
                vec2 f2 = normalize(c.s.forward().xy());
                float realPsi = atan2f(f2.x * tf.y - f2.y * tf.x, dot(f2, tf));
                if (fabsf(realPsi - d->diag[1]) > 0.25f) {
                    c.latLogT = (float)time;
                    LOG("PSIJUMP t=%.1f car %d psi %.2f real %.2f fp %.0f fu %.2f u %.2f lcLane %d lcU0 %.1f lcLen %.1f lcFrom %.2f lcTo %.2f lat %.2f nudge %.2f", time, i, d->diag[1], realPsi, d->diag[4], d->diag[5], d->u, d->lcLane, d->lcU0,
                        d->lcLen, d->lcFrom, d->lcTo, d->lat, d->nudge);
                }
            }
            // highway lane keeping (the I-38: trucks drifting toward the median barrier at speed)
            if (!d->dummy && c.s.speed() > 15.f && d->recoverTimer <= 0.f && d->path >= 0) {
                const AI::LaneGraph& G = w->lg;
                const int lp = G.isLane(d->path) ? d->path : G.conn(d->path).from;
                if (G.lanes[lp].flags & AI::LF_HIGHWAY) {
                    const int k = d->info.wheelbase > 3.3f ? 1 : 0, q = d->lcLane >= 0 ? 1 : 0;
                    const double e = d->latErr;
                    hwyN[k][q]++;
                    hwyE2[k][q] += e * e;
                    hwyOver[k][q] += fabs(e) > 0.5;
                    hwyMax[k][q] = Max(hwyMax[k][q], fabs(e));
                    if (G.lanes[lp].left < 0 && G.lanes[lp].count >= 2) {
                        hwyLeftN[k][q]++;
                        hwyLeftOver[k][q] += e < -0.3;
                        hwyLeftMax[k][q] = Max(hwyLeftMax[k][q], -e);
                    }
                }
            }
            // large lateral deviation while plainly following a lane (no lane change / nudge / recovery)
            if (verbose && w->lg.isLane(d->path) && d->lcLane < 0 && fabsf(d->nudge) < 0.05f && fabsf(d->lat) < 0.05f && d->recoverTimer <= 0.f && fabsf(d->latErr) > 0.9f &&
                c.s.speed() > 3.f && time - c.latLogT > 10.0) {
                c.latLogT = (float)time;
                LOG("LATDEV t=%.1f car %d (%s) at %.1f %.1f lane %d u %.1f latErr %.2f v %.1f vT %.1f steer %.2f diag e %.2f psi %.2f ff %.2f st %.2f", time, i, models[c.model].name.c_str(), c.s.body.pos.x, c.s.body.pos.y, d->path, d->u,
                    d->latErr, c.s.speed(), d->vTarget, c.ctl.steer, d->diag[0], d->diag[1], d->diag[2], d->diag[3]);
            }
            // held up for long behind a parked car (--parked): where it stands across our path, and the overtake state
            const AI::Body* pob = d->obstBody >= 0 && d->obstBody < (int)tc.bodies.size() ? &tc.bodies[d->obstBody] : nullptr;
            if (pob && (pob->flags & AI::BF_PARKED) && d->obstDist < 12.f && d->waitTime > 30.f && !c.parkBlockFlag) {
                c.parkBlockFlag = true;
                parkBlocks++;
                if (verbose) {
                    const AI::LaneGraph& G = w->lg;
                    float plat = 0.f;
                    G.projectPath(d->path, pob->pos, d->u, &plat);
                    float pathHalf = G.isLane(d->path) ? G.lanes[d->path].width * 0.5f : 0.f;
                    const SimCar& pcar = cars[pob->host];
                    LOG("PARKBLOCK t=%.1f car %d (%s, half width %.2f) at %.1f %.1f %s %d u %.1f v %.1f wait %.1f blocked %.1f | parked car %d (%s) %.1f m ahead: half width %.2f "
                        "half length %.2f, centre %.2f m %s of our path (lane half width %.2f), heading off the path %.3f | nudge %.2f -> %.2f (timer %.1f) lc %d stop %.1f",
                        time, i, models[c.model].name.c_str(), c.info.halfWid, c.s.body.pos.x, c.s.body.pos.y, G.isLane(d->path) ? "lane" : "conn", d->path, d->u, c.s.speed(),
                        d->waitTime, d->blockedTime, pob->host, models[pcar.model].name.c_str(), d->obstDist, pob->halfWid, pob->halfLen, fabsf(plat), plat >= 0.f ? "right" : "left",
                        pathHalf, fabsf(dot(pob->fwd, AI::rightOf(G.pathTangent(d->path, d->u)))), d->nudge, d->nudgeTarget, d->nudgeTimer, d->lcLane, d->stopDist);
                }
            }
            if (d->waitTime < 5.f) c.parkBlockFlag = false;
        }
        double c1 = cpuMs();
        for (int i = 0; i < (int)peds.size(); i++) {
            SimPed& p = peds[i];
            if (!p.used) continue;
            float face = p.yaw;
            int self = -1;
            vec2 desired = pc.step(p.w, p.pos, dt, self, &face);
            vec2 dv = desired - p.vel;
            float maxDv = 4.f * dt;
            if (length(dv) > maxDv) dv = normalize(dv) * maxDv;
            p.vel += dv;
            p.yaw = face;
        }
        double c2 = cpuMs();
        double ms = c1 - c0;
        double pms = c2 - c1;
        aiTime += ms;
        pedTime += pms;
        aiMax = Max(aiMax, ms + pms);
        aiSamples.push_back(ms + pms);
        aiTicks++;
    }

    void physStep(float h) {
        double p0 = cpuMs();
        for (auto& c : cars)
            if (c.used) Vehicles::stepVehicle(c.s, c.ctl, h);
        // vehicle-vehicle contacts
        for (int i = 0; i < (int)cars.size(); i++) {
            SimCar& a = cars[i];
            if (!a.used) continue;
            float ra = length(models[a.model].boxHalf);
            for (int j = i + 1; j < (int)cars.size(); j++) {
                SimCar& b = cars[j];
                if (!b.used) continue;
                if (a.s.sleeping && b.s.sleeping) continue;
                float rb = length(models[b.model].boxHalf);
                vec3 d = rel(b.s.body.pos, a.s.body.pos);
                if (length2(d) > (ra + rb) * (ra + rb)) continue;
                if (Vehicles::collideVehicles(a.s, b.s)) {
                    long long key = (long long)Min(a.uid, b.uid) * 1000000LL + Max(a.uid, b.uid);
                    auto it = pairLast.find(key);
                    bool fresh = it == pairLast.end() || time - it->second > 4.0;
                    pairLast[key] = time;
                    if (fresh) {
                        float imp = Max(a.s.impactImpulse, b.s.impactImpulse);
                        collisions++;
                        if (imp > 3000.f) hardCollisions++;
                        vec2 p = (a.s.body.pos.toVec3().xy() + b.s.body.pos.toVec3().xy()) * 0.5f;
                        events.push_back({p, 0, time});
                        snapshot("collision", p);
                        if (verbose) {
                            const AI::Driver* da = tc.get(i);
                            const AI::Driver* db = tc.get(j);
                            auto desc = [&](const AI::Driver* dd, SimCar& c) {
                                const AI::LaneGraph& G = w->lg;
                                std::string s = StrFormat("%s v=%.1f vT=%.1f", models[c.model].name.c_str(), c.s.speed(), dd ? dd->vTarget : 0.f);
                                if (dd) {
                                    if (G.isLane(dd->path)) s += StrFormat(" lane %d(e%d d%d i%d) u=%.1f/%.1f", dd->path, G.lanes[dd->path].edge, G.lanes[dd->path].dir, G.lanes[dd->path].index, dd->u, G.lanes[dd->path].u1);
                                    else {
                                        const AI::Connector& cc = G.conn(dd->path);
                                        s += StrFormat(" conn %d node %d turn %d ctl %d u=%.1f/%.1f", dd->path - (int)G.lanes.size(), cc.node, cc.turn, G.nodes[cc.node].control, dd->u, cc.length);
                                    }
                                    s += StrFormat(" lc %d lat %.2f nudge %.2f obst %.1f(%d) stop %.1f dummy %d", dd->lcLane, dd->lat, dd->nudge, dd->obstDist, dd->obstBody, dd->stopDist, dd->dummy);
                                }
                                return s;
                            };
                            LOG("COLLISION t=%.2f imp %.0f at %.1f %.1f\n   A %d: %s\n   B %d: %s", time, imp, p.x, p.y, i, desc(da, a).c_str(), j, desc(db, b).c_str());
                            const AI::LaneGraph& G = w->lg;
                            if (da && db && !G.isLane(da->path) && !G.isLane(db->path)) {
                                int ca = da->path - (int)G.lanes.size(), cb = db->path - (int)G.lanes.size();
                                const AI::Connector& A = G.conns[ca];
                                std::string cf = "none";
                                for (const AI::Conflict& x : A.conflicts)
                                    if (x.other == cb) cf = StrFormat("s %.1f-%.1f other %.1f-%.1f merge %d yield %d", x.s, x.sEnd, x.sOther, x.sOtherEnd, x.merge, x.yield);
                                LOG("   conflict A->B: %s | appr A %d B %d to A %d B %d | enterTime A %.2f B %.2f committed %d %d", cf.c_str(), A.approach, G.conns[cb].approach, A.to, G.conns[cb].to, da->enterTime, db->enterTime, da->committed, db->committed);
                            }
                        }
                    }
                }
            }
        }
        physTime += cpuMs() - p0;
        physSteps++;
        // statistics, static impacts, respawn
        for (int i = 0; i < (int)cars.size(); i++) {
            SimCar& c = cars[i];
            if (!c.used || c.parked) continue;
            AI::Driver* d = tc.get(i);
            c.lifeTime += h;
            vehSeconds += h;
            float v = c.s.speed();
            if (d && d->path >= 0 && d->path < (int)w->lg.lanes.size()) {
                int cls = w->lg.lanes[d->path].cls;
                clsSpeedSum[cls] += v * h;
                clsTime[cls] += h;
            }
            if (c.s.impactCollider >= 0 && c.s.impactImpulse > 2500.f) {
                staticImpacts++;
                events.push_back({c.s.body.pos.toVec3().xy(), 5, time});
                if (verbose && d) {
                    std::string cs = "-";
                    if (c.s.impactCollider >= 0) {
                        const Phys::Collider& cl = w->cw.collider(c.s.impactCollider);
                        cs = StrFormat("collider %d kind %d prop %d c (%.1f %.1f %.1f) he (%.2f %.2f %.2f) ax (%.2f %.2f)", c.s.impactCollider, cl.kind, cl.propIndex, cl.c.x, cl.c.y, cl.c.z, cl.he.x,
                                       cl.he.y, cl.he.z, cl.ax.x, cl.ax.y);
                    }
                    LOG("STATIC t=%.1f car %d at %.1f %.1f v=%.1f imp %.0f path %d u %.1f lat %.2f nudge %.2f %s | %s", time, i, c.s.body.pos.x, c.s.body.pos.y, v, c.s.impactImpulse, d->path, d->u,
                        d->latErr, d->nudge, models[c.model].name.c_str(), cs.c_str());
                }
            }
            if (c.s.brokenCount > 0) propHits += c.s.brokenCount;
            c.trailTimer -= h;
            if (c.trailTimer <= 0.f) {
                c.trailTimer = 0.4f;
                trails[c.trailId].pts.push_back(c.s.body.pos.toVec3().xy());
                trails[c.trailId].spd.push_back(v);
            }
            // hung on a ledge / kerb: back onto the lane a few meters on (game: out of view, or after 20 s)
            bool hung = d && !d->dummy && d->vTarget > 1.f && v < 0.5f && (c.s.up().z < 0.94f || c.s.wheelsOnGround < Min(3, c.s.wheelCount)) && d->path >= 0;
            c.hungTime = hung ? c.hungTime + h : 0.f;
            if (c.hungTime > 5.f) {
                float u = Min(d->u + 4.f, w->lg.pathLength(d->path) - 0.5f);
                vec3 p = w->lg.pathPos(d->path, u);
                vec2 t = w->lg.pathTangent(d->path, u);
                Vehicles::resetVehicle(c.s, dvec3(p.x, p.y, p.z + 0.35f), AI::dirYaw(t));
                c.s.body.vel = vec3(t * 2.f, 0.f);
                d->u = u;
                d->stuckTime = 0.f;
                d->recoverTimer = 0.f;
                c.hungTime = 0.f;
                unhung++;
                if (verbose) LOG("UNHUNG t=%.1f car %d at %.1f %.1f path %d u %.1f", time, i, p.x, p.y, d->path, u);
            }
            // stuck again and again at one spot (the game does this out of view): a few meters on along the route
            int pth = -1;
            float uu = 0.f;
            if (d && !d->dummy && d->stuckRepeats >= 3 && d->recoverTimer <= 0.f && tc.liftPoint(*d, 9.f, pth, uu)) {
                vec3 p = w->lg.pathPos(pth, uu);
                vec2 t = w->lg.pathTangent(pth, uu);
                if (verbose) LOG("LIFTED t=%.1f car %d from %.1f %.1f path %d u %.1f to path %d u %.1f", time, i, c.s.body.pos.x, c.s.body.pos.y, d->path, d->u, pth, uu);
                Vehicles::resetVehicle(c.s, dvec3(p.x, p.y, p.z + 0.35f), AI::dirYaw(t));
                c.s.body.vel = vec3(t * 2.f, 0.f);
                tc.relocalize(*d, p.xy(), t, 10.f);
                d->stuckRepeats = 0;
                d->stuckPath = -1;
                d->stuckAt = vec2(1e9f);
                d->stuckTime = 0.f;
                unhung++;
            }
            float dist = length(c.s.body.pos.toVec3().xy() - center);
            bool lost = d && (d->lostTime > 5.f || d->flipTime > 6.f);
            if ((dist > radius + 80.f && i != respondCar) || lost || c.s.body.pos.z < -20.0) {
                respawns++;
                spawnCar(i, false);
            }
        }
        // pedestrians
        for (int i = 0; i < (int)peds.size(); i++) {
            SimPed& p = peds[i];
            if (!p.used) continue;
            p.pos += p.vel * h;
            if (p.w.link >= 0) p.z = w->lg.walkPos(p.w.link, p.w.x, 0.f, p.w.fromA).z;
            p.hitCooldown = Max(0.f, p.hitCooldown - h);
            for (int k = 0; k < (int)cars.size() && p.hitCooldown <= 0.f; k++) {
                SimCar& c = cars[k];
                if (!c.used) continue;
                const Vehicles::VehicleModel& m = models[c.model];
                vec3 d3 = rel(dvec3(vec3(p.pos, p.z)), c.s.body.pos);
                if (fabsf(d3.z) > 2.5f || length2(d3.xy()) > 64.f) continue;
                mat3 R = c.s.body.rotMat();
                vec3 l = transpose(R) * d3 - m.boxCenter;
                if (fabsf(l.x) < m.boxHalf.x + 0.25f && fabsf(l.y) < m.boxHalf.y + 0.25f) {
                    // as in the game (vehicles.cpp): closing faster than 3.2 m/s along the contact normal knocks the
                    // person down, anything slower shoves them out of the way
                    float px = m.boxHalf.x + 0.25f - fabsf(l.x), py = m.boxHalf.y + 0.25f - fabsf(l.y);
                    vec3 nl = px < py ? vec3(l.x >= 0.f ? 1.f : -1.f, 0.f, 0.f) : vec3(0.f, l.y >= 0.f ? 1.f : -1.f, 0.f);
                    vec3 n3 = R * nl;
                    vec2 n2 = length2(n3.xy()) > 1e-8f ? normalize(n3.xy()) : vec2(1.f, 0.f);
                    float relv = dot(c.s.body.vel.xy() - p.vel, n2);
                    if (relv <= 3.2f) {
                        p.pos += n2 * Min(px, py);
                        continue;
                    }
                    {
                        pedHits++;
                        events.push_back({p.pos, 3, time});
                        snapshot("pedhit", p.pos);
                        if (verbose) {
                            const AI::Driver* dd = tc.get(k);
                            LOG("PEDHIT t=%.1f car %d v=%.1f ped state %d link kind %d at %.1f %.1f obst %.1f body %d stop %.1f", time, k, c.s.speed(), p.w.state,
                                p.w.link >= 0 ? w->lg.walkLinks[p.w.link].kind : -1, p.pos.x, p.pos.y, dd ? dd->obstDist : -1.f, dd ? dd->obstBody : -1, dd ? dd->stopDist : -1.f);
                        }
                        p.hitCooldown = 5.f;
                        spawnPed(i);
                        break;
                    }
                }
            }
            if (length(p.pos - center) > radius + 40.f) spawnPed(i);
        }
    }

    // Route following check: one car (kinematic dummy mode, no other traffic) routed from a to b with DM_ROUTE.
    // Prints the planned edge count and progress; returns the arrival time or -1.
    float routeTest(vec2 a, vec2 b, bool quiet) {
        cars.assign(1, SimCar());
        float u = 0.f;
        int lane = w->lg.nearestLane(a, vec2(0), 60.f, &u);
        if (lane < 0) {
            if (!quiet) printf("route: no lane near (%.0f, %.0f)\n", a.x, a.y);
            return -2.f;
        }
        const AI::Lane& L = w->lg.lanes[lane];
        u = Clamp(u, L.u0 + 1.f, L.u1 - 1.f);
        SimCar& c = cars[0];
        c = SimCar();
        c.used = true;
        c.model = pickModel();
        c.uid = uidNext++;
        vec3 pos = w->lg.lanePos(lane, u);
        vec2 t = w->lg.laneTangent(lane, u);
        Vehicles::initVehicle(c.s, models[c.model], c.model, dvec3(pos.x, pos.y, pos.z + 0.3), AI::dirYaw(t));
        c.info = AI::makeVehicleInfo(models[c.model], c.s);
        tc.detach(0);
        tc.attach(0, c.uid, 1234u, c.info, lane, u);
        AI::Driver* d = tc.get(0);
        d->mode = AI::DM_ROUTE;
        tc.setDestination(*d, b);
        if (!quiet) printf("route (%.0f, %.0f) -> (%.0f, %.0f): start lane %d (edge %d), %zu edges planned\n", a.x, a.y, b.x, b.y, lane, L.edge, d->destEdges.size());
        tc.toDummy(0, c.s);
        // arrival: at the point on the goal road nearest to b (b itself may lie off the road)
        float gs = 0.f;
        int ge = w->roads.nearestEdge(b, 400.f, &gs);
        if (ge >= 0) b = w->roads.edges[ge].posAt(gs).xy();
        float best = 1e9f;
        int lastLane = lane, deviations = 0;
        std::vector<int> planAtLastLane = d->destEdges;
        for (int st = 0; st < 18000; st++) {   // 0.1 s steps, 30 minutes
            time += 0.1;
            aiTick(0.1f);
            // deviation check: entering a lane whose edge is not the one the plan wanted after the previous lane's edge
            if (w->lg.isLane(d->path) && d->path != lastLane) {
                int prevEdge = w->lg.lanes[lastLane].edge, newEdge = w->lg.lanes[d->path].edge;
                int want = -1;
                for (size_t k = 0; k + 1 < planAtLastLane.size(); k++)
                    if (planAtLastLane[k] == prevEdge) {
                        want = planAtLastLane[k + 1];
                        break;
                    }
                if (want >= 0 && newEdge != want && newEdge != prevEdge) {
                    deviations++;
                    if (!quiet) {
                        const AI::Lane& PL = w->lg.lanes[lastLane];
                        std::string opts;
                        int gf = w->lg.groupFirst[PL.group], gc = w->lg.groupCount[PL.group];
                        for (int k = 0; k < gc; k++)
                            for (int cc : w->lg.lanes[gf + k].out)
                                opts += StrFormat(" [lane idx %d -> edge %d turn %d]", k, w->lg.lanes[w->lg.conns[cc].to].edge, w->lg.conns[cc].turn);
                        printf("  DEVIATION t %.0f at node %d: from edge %d (lane idx %d/%d, lane len %.0f) wanted edge %d, took edge %d; options:%s\n", st * 0.1f,
                               PL.toNode, prevEdge, PL.index, PL.count, PL.u1 - PL.u0, want, newEdge, opts.c_str());
                    }
                }
                lastLane = d->path;
                planAtLastLane = d->destEdges;
            }
            vec3 p = w->lg.pathPos(d->path, d->u);
            float dist = length(p.xy() - b);
            best = Min(best, dist);
            if (!quiet && st % 300 == 0)
                printf("  t %5.0f s at (%6.0f, %6.0f) %s %d dist %.0f (best %.0f) edges %zu mode %d\n", st * 0.1f, p.x, p.y, w->lg.isLane(d->path) ? "lane" : "conn",
                       d->path, dist, best, d->destEdges.size(), d->mode);
            if (dist < 60.f) {
                if (!quiet) printf("  arrived after %.0f s\n", st * 0.1f);
                return st * 0.1f;
            }
        }
        if (!quiet) printf("  NOT arrived (closest %.0f m)\n", best);
        return -1.f;
    }

    void run(float seconds) {
        const float h = 1.f / 120.f;
        long steps = (long)(seconds / h);
        double wall0 = TimeSeconds();
        for (long st = 0; st < steps; st++) {
            if (st % 2 == 0) aiTick(2.f * h);
            physStep(h);
            time += h;
            if (st % (120 * 60) == 0 && st > 0)
                LOG("  sim %.0f s: collisions %ld (hard %ld), red %ld, stuck %ld, deadlocks %ld, ped hits %ld, respawns %ld (wall %.0f s)", time, collisions,
                    hardCollisions, tc.stats.redViolations, tc.stats.stuckEvents, deadlocks, pedHits, respawns, TimeSeconds() - wall0);
        }
    }

    void report() {
        double vh = vehSeconds / 3600.0;
        printf("\n==== Traffic metrics (%.0f s simulated, %d cars, %d peds, %.2f vehicle-hours) ====\n", time, (int)cars.size(), (int)peds.size(), vh);
        printf("collisions: %ld (%.2f per vehicle-hour), hard (>3000 Ns): %ld (%.2f/veh-h)\n", collisions, collisions / Max(vh, 1e-6), hardCollisions, hardCollisions / Max(vh, 1e-6));
        printf("static impacts (walls/props >2500 Ns): %ld, props broken: %ld\n", staticImpacts, propHits);
        printf("red-light violations: %ld, stop-sign violations: %ld\n", tc.stats.redViolations, tc.stats.stopSignViolations);
        if (overshoot) {
            for (int k = 0; k < 2; k++)
                printf("overshoot test, %s: %ld cars rolled 0.8 m past the line; %ld then stopped before 4 m past it (at %.1f m on average, then waited %.1f s to go), %ld ran on%s\n",
                       k ? "all-way stops" : "written stop signs", ovForced[k], ovStopped[k], ovStopped[k] ? ovStopPast[k] / ovStopped[k] : 0.0,
                       ovStopped[k] ? ovWait[k] / ovStopped[k] : 0.0, ovRan[k], k ? StrFormat("; %ld went before an earlier arrival waiting with a movement across theirs", ovAhead).c_str() : "");
        }
#ifndef TS_NO_RULES
        if (tc.stats.writtenStops > 0) printf("stops made at written stop signs: %ld\n", tc.stats.writtenStops);
#endif
#ifndef TS_NO_SWEEP
        if (tc.stats.stopsPast > 0) printf("stops made over 0.4 m past the line at all-way stops and stop signs: %ld\n", tc.stats.stopsPast);
#endif
#ifndef TS_NO_SWEEP
        if (tc.stats.sweepHolds + tc.stats.sweepYields + tc.stats.sweepRanOut > 0)
            printf("long vehicles held for a car in their turn's sweep: %ld (gone on after the longest hold with one still there: %ld), cars stopped short of one turning: %ld\n",
                   tc.stats.sweepHolds, tc.stats.sweepRanOut, tc.stats.sweepYields);
#endif
        printf("stuck recoveries: %ld, three-point turns: %ld, relocalizations: %ld, deadlocks (>60 s waits): %ld, respawns: %ld, lifted off ledges: %ld\n", tc.stats.stuckEvents, tc.stats.kTurns, tc.stats.relocalizations, deadlocks, respawns, unhung);
        printf("pedestrians hit: %ld\n", pedHits);
        if (nParked > 0) printf("parked cars: %d, waits over 30 s behind one: %ld\n", nParked, parkBlocks);
        const char* names[] = {"highway", "boulevard", "avenue", "street", "lane", "rural", "dirt", "ramp"};
        printf("average speed by road class (km/h):");
        for (int k = 0; k < World::RC_COUNT; k++)
            if (clsTime[k] > 1.0) printf("  %s %.1f (%.0f s)", names[k], clsSpeedSum[k] / clsTime[k] * 3.6, clsTime[k]);
        printf("\n");
        double e2 = 0;
        long en = 0;
        for (auto& d : tc.drivers) {
            e2 += d.statErr2;
            en += d.statErrN;
        }
        printf("lane-center error (RMS, lanes, v>3 m/s): %.3f m\n", en ? sqrt(e2 / en) : 0.0);
        for (int k = 0; k < 2; k++)
            for (int q = 0; q < 2; q++)
                if (hwyN[k][q] > 0)
                    printf("highway lane keeping, %s, %s (v>15 m/s, %.0f s): RMS %.3f m, over 0.5 m %.2f%%, max %.2f m; median lane (%.0f s): over 0.3 m left %.2f%%, max left %.2f m\n",
                           k ? "long vehicles" : "cars", q ? "changing lanes" : "in lane", hwyN[k][q] / 60.0, sqrt(hwyE2[k][q] / hwyN[k][q]), 100.0 * hwyOver[k][q] / hwyN[k][q],
                           hwyMax[k][q], hwyLeftN[k][q] / 60.0, hwyLeftN[k][q] ? 100.0 * hwyLeftOver[k][q] / hwyLeftN[k][q] : 0.0, hwyLeftMax[k][q]);
        std::vector<double> s = aiSamples;
        std::sort(s.begin(), s.end());
        double p50 = s.empty() ? 0 : s[s.size() / 2], p99 = s.empty() ? 0 : s[(size_t)(s.size() * 0.99)];
        printf("AI cost per 60 Hz tick: traffic %.3f ms + peds %.3f ms (avg), p50 %.3f ms, p99 %.3f ms, max %.3f ms\n", aiTime / Max(aiTicks, 1L),
               pedTime / Max(aiTicks, 1L), p50, p99, aiMax);
        printf("physics per 120 Hz step: %.3f ms (%d cars)\n", physTime / Max(physSteps, 1L), (int)cars.size());
    }

    // Single-vehicle dead-end test: every model round the turning circle of every dead end within `radius` of `at` - from
    // 30 m up its street round the circle until its front axle is on the way back out and lined up with it (or past that
    // lane) - for whether it gets round within 60 s, the time it takes, the three-point turns it needs, the stuck
    // recoveries, and how far its front axle strays from the way (in-lane, circle or exit lane, whichever is nearest). The
    // Calle Luna run: a pickup's three-point turns at node 4870 walked it 10 m off the road and into the marina.
    // UT_TRACE=conn prints that circle's runs twice a second.
    void uturnTest(vec2 at, float radius, const char* onlyModel) {
        AI::LaneGraph& G = w->lg;
        const int traceU = getenv("UT_TRACE") ? atoi(getenv("UT_TRACE")) : -1;
        struct Res {
            int model, node, conn;
            bool done;
            float secs, maxOff;
            int kturns, stucks;
            float wb, maxSteer;
        };
        std::vector<Res> all;
        const float h = 1.f / 120.f;
        int nodes = 0;
        for (int n = 0; n < (int)G.nodes.size(); n++) {
            if (!G.nodes[n].deadEnd || G.nodes[n].approaches.empty() || length(w->roads.nodes[n].p - at) > radius) continue;
            nodes++;
            w->loadCells(w->roads.nodes[n].p, 70.f);
            for (int m = 0; m < (int)models.size(); m++) {
                if (models[m].cls >= Vehicles::VC_BOAT) continue;
                if (onlyModel) {
                    // (a comma list of names: exact names)
                    bool any = false;
                    std::string list = onlyModel;
                    for (size_t a = 0; a <= list.size();) {
                        size_t b = list.find(',', a);
                        if (b == std::string::npos) b = list.size();
                        if (list.substr(a, b - a) == models[m].name || (list.find(',') == std::string::npos && models[m].name.find(list) != std::string::npos)) any = true;
                        a = b + 1;
                    }
                    if (!any) continue;
                }
                for (const AI::Approach& A : G.nodes[n].approaches)
                    for (int l : A.inLanes)
                        for (int cn : G.lanes[l].out) {
                            const AI::Connector& C = G.conns[cn];
                            if (C.turn != AI::TK_UTURN) continue;
                            cars.assign(1, SimCar());
                            tc.drivers.clear();
                            tc.drivers.resize(1);
                            SimCar& c = cars[0];
                            c.used = true;
                            c.model = m;
                            c.uid = uidNext++;
                            const AI::Lane& L = G.lanes[l];
                            float u = Max(L.u0 + 2.f, L.u1 - 30.f);
                            vec3 pos = G.lanePos(l, u);
                            vec2 t = G.laneTangent(l, u);
                            Vehicles::initVehicle(c.s, models[m], m, dvec3(pos.x, pos.y, pos.z + 0.3), AI::dirYaw(t));
                            c.info = AI::makeVehicleInfo(models[m], c.s);
                            c.s.body.vel = vec3(t * 6.f, 0.f);
                            for (int k = 0; k < c.s.wheelCount; k++) c.s.wheels[k].spinVel = 6.f / Max(models[m].wheels[k].radius, 0.2f);
                            AI::Driver& d = tc.attach(0, c.uid, hash32(c.uid * 7919u), c.info, l, u);
                            d.pers = AI::Personality();
                            d.route[0] = G.connPath(cn);
                            d.route[1] = C.to;
                            d.routeLen = 2;
                            const int exitLane = C.to, connPath = G.connPath(cn);
                            const long stuck0 = tc.stats.stuckEvents;
                            Res r{m, n, cn, false, 0.f, 0.f, 0, 0, c.info.wheelbase, c.info.maxSteer};
                            int kt = 0;
                            bool wasConn = false;
                            for (int st = 0; st < 60 * 120; st++) {
                                if (st % 2 == 0) {
                                    fillBodies();
                                    tc.beginTick(time);
                                    AI::DriveOut out;
                                    tc.drive(0, c.s, 2.f * h, out);
                                    c.ctl = out.ctl;
                                }
                                Vehicles::stepVehicle(c.s, c.ctl, h);
                                time += h;
                                kt = Max(kt, d.kturns);
                                wasConn |= d.path == connPath;
                                vec2 f = normalize(c.s.forward().xy());
                                vec2 front = c.s.body.pos.toVec3().xy() + f * c.info.frontAxleY;
                                // round: on past the exit lane, or its front axle on that lane and lined up with it (an exit
                                // lane can be a few metres to a stop line)
                                const bool onMove = d.path == l || d.path == connPath || d.path == exitLane;
                                {
                                    const AI::Lane& E = G.lanes[exitLane];
                                    float la = 0.f;
                                    float ue = G.projectPath(exitLane, front, E.u0 + 1.f, &la);
                                    vec2 te = G.laneTangent(exitLane, Clamp(ue, E.u0, E.u1));
                                    if (wasConn && (!onMove || (ue > E.u0 + 1.5f && ue < E.u1 && fabsf(la) < 1.2f && dot(f, te) > 0.92f))) {
                                        r.done = true;
                                        r.secs = st * h;
                                        break;
                                    }
                                }
                                if (!onMove) break;
                                // the front axle's distance from the way: in-lane, circle or exit lane, whichever is nearest
                                float off = 1e9f;
                                const int paths[3] = {l, connPath, exitLane};
                                for (int P : paths) {
                                    float la = 0.f;
                                    float lo = G.isLane(P) ? G.lanes[P].u0 : 0.f, hi = G.isLane(P) ? G.lanes[P].u1 : G.pathLength(P);
                                    float uu = G.projectPath(P, front, P == d.path ? d.u : (lo + hi) * 0.5f, &la);
                                    float over = uu < lo ? lo - uu : (uu > hi ? uu - hi : 0.f);
                                    off = Min(off, sqrtf(la * la + over * over));
                                }
                                r.maxOff = Max(r.maxOff, off);
                                if (traceU == cn && st % 60 == 0)
                                    printf("  t %.1f path %d u %.1f v %.1f steer %.2f off %.2f kturns %d rec %.1f latErr %.2f headErr %.2f\n", st * h, d.path, d.u, c.s.speed(),
                                           c.ctl.steer, off, d.kturns, d.recoverTimer, d.latErr, d.headErr);
                                if (off > 12.f) break;   // (well off the road: lost)
                            }
                            r.kturns = kt;
                            r.stucks = (int)(tc.stats.stuckEvents - stuck0);
                            all.push_back(r);
                        }
            }
        }
        // per model: circles got round, the slowest, the most three-point turns, the furthest strayed
        int fails = 0, runs = 0;
        for (int m = 0; m < (int)models.size(); m++) {
            int n = 0, done = 0, kmax = 0, kn = 0, stk = 0;
            float slow = 0.f, off = 0.f, wb = 0.f, ms = 0.f;
            for (const Res& r : all)
                if (r.model == m) {
                    n++;
                    wb = r.wb;
                    ms = r.maxSteer;
                    done += r.done;
                    if (r.done) slow = Max(slow, r.secs);
                    kmax = Max(kmax, r.kturns);
                    kn += r.kturns > 0;
                    stk += r.stucks;
                    off = Max(off, r.maxOff);
                }
            if (!n) continue;
            runs += n;
            fails += n - done;
            printf("%-22s wb %.2f lock %.2f: round %d/%d, slowest %.1f s, with three-point turns %d (most %d), stuck %d, strayed up to %.1f m\n",
                   models[m].name.c_str(), wb, ms, done, n, slow, kn, kmax, stk, off);
        }
        std::sort(all.begin(), all.end(), [](const Res& a, const Res& b) { return a.done != b.done ? !a.done : a.maxOff > b.maxOff; });
        printf("worst runs:\n");
        for (int k = 0; k < (int)all.size() && k < 15; k++) {
            const Res& r = all[k];
            const World::RoadNode& rn = w->roads.nodes[r.node];
            printf("  %-22s node %d (%.0f, %.0f) conn %d minRadius %.1f: %s %.1f s, three-point turns %d, stuck %d, strayed %.1f m\n", models[r.model].name.c_str(), r.node,
                   rn.p.x, rn.p.y, r.conn, G.conns[r.conn].minRadius, r.done ? "round in" : "NOT round after", r.done ? r.secs : 60.f, r.kturns, r.stucks, r.maxOff);
        }
        printf("dead-end test: %d circles, %d runs, %d not round\n", nodes, runs, fails);
    }

    // Meeting test: two vehicles (models A and B) coming at each other on the two lanes of edge `edge`, started 25 m either
    // side of a meeting point that steps along the edge every 2 m, at 7 m/s, each on its way on past the edge. For each
    // meeting point: the least gap between their bodies (m; < 0: they touched) and the speeds then. The Sol Beach run: a fire
    // engine and a box truck on edge 2907's S-bend, both in their lanes, 98748 Ns. MT_TRACE=k prints meeting point k.
    void meetTest(int edge, const char* nameA, const char* nameB) {
        AI::LaneGraph& G = w->lg;
        int la = -1, lb = -1;
        for (int i = 0; i < (int)G.lanes.size(); i++)
            if (G.lanes[i].edge == edge && G.lanes[i].right < 0) (G.lanes[i].dir > 0 ? la : lb) = i;
        int ma = -1, mb = -1;
        for (int m = 0; m < (int)models.size(); m++) {
            if (models[m].name == nameA) ma = m;
            if (models[m].name == nameB) mb = m;
        }
        if (la < 0 || lb < 0 || ma < 0 || mb < 0) {
            printf("meet test: edge %d lanes %d / %d, models %d / %d - not found\n", edge, la, lb, ma, mb);
            return;
        }
        const int traceK = getenv("MT_TRACE") ? atoi(getenv("MT_TRACE")) : -1;
        const float h = 1.f / 120.f;
        const AI::Lane& LA = G.lanes[la];
        w->loadCells(G.lanePos(la, (LA.u0 + LA.u1) * 0.5f).xy(), 160.f);
        printf("meet test: edge %d, %s on lane %d against %s on lane %d\n", edge, nameA, la, nameB, lb);
        int k = 0, touched = 0, standoffs = 0;
        float worst = 1e9f;
        for (float uP = LA.u0 + 4.f; uP < LA.u1 - 4.f; uP += 2.f, k++) {
            vec3 P = G.lanePos(la, uP);
            float lat = 0.f;
            float uPb = G.projectPath(lb, P.xy(), (G.lanes[lb].u0 + G.lanes[lb].u1) * 0.5f, &lat);
            cars.assign(2, SimCar());
            tc.drivers.clear();
            tc.drivers.resize(2);
            const int lanesOf[2] = {la, lb}, modelOf[2] = {ma, mb};
            const float uMeet[2] = {uP, uPb};
            bool ok = true;
            for (int q = 0; q < 2; q++) {
                SimCar& c = cars[q];
                c.used = true;
                c.model = modelOf[q];
                c.uid = uidNext++;
                const int l = lanesOf[q];
                Vehicles::VehicleState tmp;
                Vehicles::initVehicle(tmp, models[c.model], c.model, dvec3(0.0, 0.0, 0.0), 0.f);
                AI::VehicleInfo vi = AI::makeVehicleInfo(models[c.model], tmp);
                // its front 25 m short of the meeting point, on its lane
                float u = uMeet[q] - 25.f - vi.frontLen;
                if (u < G.lanes[l].u0) {
                    ok = false;
                    break;
                }
                vec3 pos = G.lanePos(l, u);
                vec2 t = G.laneTangent(l, u);
                Vehicles::initVehicle(c.s, models[c.model], c.model, dvec3(pos.x, pos.y, pos.z + 0.3), AI::dirYaw(t));
                c.info = AI::makeVehicleInfo(models[c.model], c.s);
                c.s.body.vel = vec3(t * 7.f, 0.f);
                for (int wv = 0; wv < c.s.wheelCount; wv++) c.s.wheels[wv].spinVel = 7.f / Max(models[c.model].wheels[wv].radius, 0.2f);
                AI::Driver& d = tc.attach(q, c.uid, hash32(c.uid * 7919u), c.info, l, u);
                d.pers = AI::Personality();
            }
            if (!ok) continue;
            float least = 1e9f, vA = 0.f, vB = 0.f, passedAt = -1.f;
            const vec2 Pxy = P.xy();
            const vec2 dirA = G.laneTangent(la, uP);
            for (int st = 0; st < 30 * 120; st++) {
                if (st % 2 == 0) {
                    fillBodies();
                    tc.beginTick(time);
                    for (int q = 0; q < 2; q++) {
                        AI::DriveOut out;
                        tc.drive(q, cars[q].s, 2.f * h, out);
                        cars[q].ctl = out.ctl;
                    }
                }
                for (int q = 0; q < 2; q++) Vehicles::stepVehicle(cars[q].s, cars[q].ctl, h);
                time += h;
                // the gap between the two body boxes: their separation along the box axes (the most of the four)
                vec2 cc[2], ax[2][2];
                vec2 he[2];
                for (int q = 0; q < 2; q++) {
                    const Vehicles::VehicleModel& md = models[cars[q].model];
                    mat3 R = cars[q].s.body.rotMat();
                    cc[q] = (cars[q].s.body.pos.toVec3() + R * md.boxCenter).xy();
                    vec2 f = normalize(cars[q].s.forward().xy());
                    ax[q][0] = AI::rightOf(f);
                    ax[q][1] = f;
                    he[q] = vec2(md.boxHalf.x, md.boxHalf.y);
                }
                float sep = -1e9f;
                for (int q = 0; q < 2; q++)
                    for (int a = 0; a < 2; a++) {
                        vec2 n = ax[q][a];
                        float ra = he[0].x * fabsf(dot(ax[0][0], n)) + he[0].y * fabsf(dot(ax[0][1], n));
                        float rb = he[1].x * fabsf(dot(ax[1][0], n)) + he[1].y * fabsf(dot(ax[1][1], n));
                        sep = Max(sep, fabsf(dot(cc[1] - cc[0], n)) - ra - rb);
                    }
                if (sep < least) {
                    least = sep;
                    vA = cars[0].s.speed();
                    vB = cars[1].s.speed();
                }
                // past each other: A beyond the meeting point and B on its way back past it (along A's way there)
                if (passedAt < 0.f && dot(cars[0].s.body.pos.toVec3().xy() - Pxy, dirA) > 6.f && dot(cars[1].s.body.pos.toVec3().xy() - Pxy, dirA) < -6.f) {
                    passedAt = st * h;
                    break;
                }
                if (traceK == k && st % 30 == 0) {
                    const AI::Driver& dA = tc.drivers[0];
                    const AI::Driver& dB = tc.drivers[1];
                    printf("  t %.2f gap %.2f | A path %d u %.1f v %.1f latErr %.2f nudge %.2f | B path %d u %.1f v %.1f latErr %.2f nudge %.2f\n", st * h, sep, dA.path,
                           dA.u, cars[0].s.speed(), dA.latErr, dA.nudge, dB.path, dB.u, cars[1].s.speed(), dB.latErr, dB.nudge);
                }
            }
            worst = Min(worst, least);
            touched += least < 0.f;
            standoffs += passedAt < 0.f;
            printf("  meeting point %2d at u %.1f (%.1f, %.1f) curvature %+.4f: least gap %5.2f m (A %.1f m/s, B %.1f m/s), %s%s\n", k, uP, P.x, P.y,
                   G.pathCurv(la, uP), least, vA, vB, passedAt >= 0.f ? StrFormat("past each other in %.1f s", passedAt).c_str() : "NOT PAST in 30 s", least < 0.f ? "  TOUCHED" : "");
        }
        printf("meet test: %d meeting points, %d touched, %d not past each other, least gap %.2f m\n", k, touched, standoffs, worst);
    }

    // Single-vehicle turning test: every model through every movement of the node nearest `at` (made uncontrolled),
    // measuring how far the body strays outside its lane (left = toward opposing traffic) on the connector and on the
    // first 15 m of the exit lane, and the front-axle tracking error.
    int traceConn = -1;
#ifndef TS_NO_SWEEP
    // (the sweep zones' clip, as lanes.cpp: the least u of a quad in a lane-end frame - x = u, y = lat - within |lat| <= m)
    static float zoneClipMinU(const vec2* q, float m) {
        vec2 a[12], b[12];
        int na = 4;
        for (int i = 0; i < 4; i++) a[i] = q[i];
        for (int side = 0; side < 2; side++) {
            const float sg = side == 0 ? 1.f : -1.f;
            int nb = 0;
            for (int i = 0; i < na; i++) {
                const vec2 pa = a[i], pb = a[(i + 1) % na];
                const float da = sg * pa.y - m, db = sg * pb.y - m;
                if (da <= 0.f) b[nb++] = pa;
                if ((da <= 0.f) != (db <= 0.f)) b[nb++] = pa + (pb - pa) * (da / (da - db));
            }
            na = nb;
            if (na == 0) return 1e9f;
            for (int i = 0; i < na; i++) a[i] = b[i];
        }
        float mn = 1e9f;
        for (int i = 0; i < na; i++) mn = Min(mn, a[i].x);
        return mn;
    }
    // lanes.cpp's sweep-zone run (ensureSweeps) for one connector and one lane, with the vehicle given: how far behind
    // the lane's line its body reaches within `band` of the lane's middle (-1e9: not at all) - for calibrating the
    // design vehicles against the real bodies (the turn test's zone check)
    static float designDepth(const AI::LaneGraph& G, int ci, int lane, float W, float maxSteer, float fOver, float rOver, float hw, float band, float marginK = 0.f,
                             float marginMax = 0.f, float latGain = 0.3f) {
        const AI::Connector& c = G.conns[ci];
        const AI::Lane& L = G.lanes[lane];
        const vec2 p1 = G.lanePos(lane, L.u1).xy(), t1 = G.laneTangent(lane, L.u1), r1 = AI::rightOf(t1);
        const float wait = L.stopU >= 0.f ? L.stopU : L.u1 - 0.6f;
        std::vector<vec2> poly;
        const AI::Lane& Lin = G.lanes[c.from];
        const AI::Lane& Lout = G.lanes[c.to];
        for (float u = Max(Lin.u0, Lin.u1 - 10.f); u < Lin.u1 - 0.25f; u += 0.5f) poly.push_back(G.lanePos(c.from, u).xy());
        const size_t laneInEnd = poly.size();
        for (const vec3& q : c.pts) poly.push_back(q.xy());
        for (float u = Lout.u0 + 0.5f; u <= Min(Lout.u1, Lout.u0 + 20.f); u += 0.5f) poly.push_back(G.lanePos(c.to, u).xy());
        if (poly.size() < 4 || length2(poly[1] - poly[0]) < 1e-6f) return -1e9f;
        float minU = 1e9f;
        vec2 h0 = normalize(poly[1] - poly[0]);
        vec2 R = poly[0];
        float th = atan2f(h0.y, h0.x);
        size_t idx = 0;
        for (int step = 0; step < 300; step++) {
            const vec2 hd(cosf(th), sinf(th));
            const vec2 F = R + hd * W;
            float best = 1e18f;
            size_t bi = idx;
            for (size_t j = idx; j + 1 < poly.size() && j < idx + 10; j++) {
                float dd = distPointSegment2D(F, poly[j], poly[j + 1]);
                if (dd < best) {
                    best = dd;
                    bi = j;
                }
            }
            idx = bi;
            if (idx + 2 >= poly.size()) break;
            const vec2 tp = normalize(poly[idx + 1] - poly[idx] + vec2(1e-6f, 0.f));
            const float e = cross(tp, F - poly[idx]);
            const float psi = AI::wrapPi(atan2f(tp.y, tp.x) - th);
            const float delta = Clamp(psi - atanf(latGain * e), -maxSteer, maxSteer);
            const vec2 n(-hd.y, hd.x);
            const vec2 fb = R + hd * (W + fOver), rb = R - hd * rOver;
            const vec2 body[4] = {fb + n * hw, fb - n * hw, rb - n * hw, rb + n * hw};
            static const int dtrace = getenv("TT_DTRACE") ? atoi(getenv("TT_DTRACE")) : -1;
            if (dtrace == ci && band == AI::kSweepBand)
                printf("    design W %.2f step %d idx %zu/%zu (lane in %zu) e %.2f psi %.2f delta %.2f\n", W, step, idx, poly.size(), laneInEnd, e, psi, delta);
            if (idx + 1 >= laneInEnd && length2((fb + rb) * 0.5f - p1) <= 400.f) {
                vec2 q[4];
                for (int i = 0; i < 4; i++) {
                    const vec2 dq = body[i] - p1;
                    q[i] = vec2(L.u1 + dot(dq, t1), dot(dq, r1));
                }
                const float mu = zoneClipMinU(q, band + Clamp(fabsf(psi) * marginK, 0.f, marginMax));   // (wider while it is off its heading)
                if (mu < wait + 0.3f) minU = Min(minU, Max(mu, wait - 18.f));
            }
            R += hd * 0.8f;
            th += 0.8f * tanf(delta) / W;
        }
        return minU > 1e8f ? -1e9f : wait - minU;
    }
#endif
    void turnTest(vec2 at, const char* onlyModel) {
        AI::LaneGraph& G = w->lg;
        if (getenv("TT_TRACE")) traceConn = atoi(getenv("TT_TRACE"));
        int node = -1;
        float bd = 1e9f;
        for (int n = 0; n < (int)G.nodes.size(); n++) {
            if (G.nodes[n].approaches.size() < 3) continue;
            float d = length(w->roads.nodes[n].p - at);
            if (d < bd) {
                bd = d;
                node = n;
            }
        }
        if (node < 0) return;
        G.nodes[node].control = 0;
        LOG("turn test at node %d (%.0f, %.0f), %zu approaches", node, w->roads.nodes[node].p.x, w->roads.nodes[node].p.y, G.nodes[node].approaches.size());
        struct Res {
            int model, conn;
            float leftBox, leftExit, rightOut, err;
            bool done;
            float secs;
            float wb, maxSteer;
            float leftFar;   // beyond 4.5 m into the exit lane: where cars wait at the opposing stop line
        };
        std::vector<Res> all;
#ifndef TS_NO_SWEEP
        struct ZoneCheck { int model, conn, lane; float real, realWide, zone, zoneWide; int cls; };
        std::vector<ZoneCheck> zoneChecks;
#endif
        const float h = 1.f / 120.f;
        for (int m = 0; m < (int)models.size(); m++) {
            if (models[m].cls >= Vehicles::VC_BOAT) continue;
            if (onlyModel && models[m].name.find(onlyModel) == std::string::npos) continue;
            for (const AI::Approach& A : G.nodes[node].approaches)
                for (int l : A.inLanes)
                    for (int cn : G.lanes[l].out) {
                        const AI::Connector& C = G.conns[cn];
                        if (C.turn == AI::TK_UTURN) continue;
                        cars.assign(1, SimCar());
                        tc.drivers.clear();
                        tc.drivers.resize(1);
                        SimCar& c = cars[0];
                        c.used = true;
                        c.model = m;
                        c.uid = uidNext++;
                        const AI::Lane& L = G.lanes[l];
                        float u = Max(L.u0 + 2.f, L.u1 - 30.f);
                        vec3 pos = G.lanePos(l, u);
                        vec2 t = G.laneTangent(l, u);
                        Vehicles::initVehicle(c.s, models[m], m, dvec3(pos.x, pos.y, pos.z + 0.3), AI::dirYaw(t));
                        c.info = AI::makeVehicleInfo(models[m], c.s);
                        c.s.body.vel = vec3(t * 6.f, 0.f);
                        for (int k = 0; k < c.s.wheelCount; k++) c.s.wheels[k].spinVel = 6.f / Max(models[m].wheels[k].radius, 0.2f);
                        AI::Driver& d = tc.attach(0, c.uid, hash32(c.uid * 7919u), c.info, l, u);
                        d.pers = AI::Personality();
                        d.route[0] = G.connPath(cn);
                        d.route[1] = C.to;
                        d.routeLen = 2;
                        Res r{m, cn, 0.f, 0.f, 0.f, 0.f, false, 0.f, c.info.wheelbase, c.info.maxSteer, 0.f};
#ifndef TS_NO_SWEEP
                        // the waiting places the real body crosses (the sweep zones' test, lanes.cpp, on this run)
                        struct ZoneCand { int lane; vec2 p1, t1, r1; float u1, wait, minU, minUW; };
                        std::vector<ZoneCand> zc;
                        if ((C.turn == AI::TK_LEFT || C.turn == AI::TK_RIGHT) && C.minRadius <= 25.f) {
                            const vec2 cMid = C.pts[C.pts.size() / 2].xy();
                            for (int ai = 0; ai < (int)G.nodes[node].approaches.size(); ai++) {
                                if (ai == C.approach) continue;
                                for (int l2 : G.nodes[node].approaches[ai].inLanes) {
                                    const AI::Lane& L2 = G.lanes[l2];
                                    const vec2 p1 = G.lanePos(l2, L2.u1).xy();
                                    if (length2(p1 - cMid) > 35.f * 35.f) continue;
                                    const vec2 t1 = G.laneTangent(l2, L2.u1);
                                    zc.push_back({l2, p1, t1, AI::rightOf(t1), L2.u1, L2.stopU >= 0.f ? L2.stopU : L2.u1 - 0.6f, 1e9f, 1e9f});
                                }
                            }
                        }
#endif
                        int exitLane = C.to, connPath = G.connPath(cn);
                        const Vehicles::VehicleModel& md = models[m];
                        for (int st = 0; st < 30 * 120; st++) {
                            if (st % 2 == 0) {
                                fillBodies();
                                tc.beginTick(time);
                                AI::DriveOut out;
                                tc.drive(0, c.s, 2.f * h, out);
                                c.ctl = out.ctl;
                            }
                            Vehicles::stepVehicle(c.s, c.ctl, h);
                            time += h;
                            bool onConn = d.path == connPath;
                            bool onExit = d.path == exitLane && d.u < G.lanes[exitLane].u0 + 15.f;
                            if (d.path == exitLane && d.u > G.lanes[exitLane].u0 + 20.f) {
                                r.done = true;
                                r.secs = st * h;
                                break;
                            }
                            if (d.path != l && !onConn && d.path != exitLane) break;
                            bool approach = d.path == l && d.u > G.lanes[l].u1 - 8.f;
                            if (!onConn && !onExit && !(approach && traceConn == cn)) continue;
                            mat3 R = c.s.body.rotMat();
                            vec3 ctr3 = c.s.body.pos.toVec3() + R * md.boxCenter;
                            vec2 f = normalize(c.s.forward().xy()), rr = AI::rightOf(f);
                            vec2 ctr = ctr3.xy();
                            vec2 q[4] = {ctr + f * md.boxHalf.y + rr * md.boxHalf.x, ctr + f * md.boxHalf.y - rr * md.boxHalf.x, ctr - f * md.boxHalf.y - rr * md.boxHalf.x,
                                         ctr - f * md.boxHalf.y + rr * md.boxHalf.x};
                            float half = G.lanes[exitLane].width * 0.5f;
#ifndef TS_NO_SWEEP
                            if (onConn || onExit)
                                for (ZoneCand& z : zc) {
                                    vec2 qq[4];
                                    for (int i = 0; i < 4; i++) {
                                        const vec2 dq = q[i] - z.p1;
                                        qq[i] = vec2(z.u1 + dot(dq, z.t1), dot(dq, z.r1));
                                    }
                                    const float mu = zoneClipMinU(qq, 1.2f), muW = zoneClipMinU(qq, 1.45f);   // (a car waiting there; a van, truck or bus)
                                    if (mu < z.wait + 0.3f) z.minU = Min(z.minU, Max(mu, z.wait - 18.f));
                                    if (muW < z.wait + 0.3f) z.minUW = Min(z.minUW, Max(muW, z.wait - 18.f));
                                }
#endif
                            // lateral offset of a point relative to the movement (in-lane, connector or exit lane,
                            // whichever contains its projection)
                            auto latOf = [&](vec2 pt) {
                                float best = 1e9f;
                                int paths[3] = {l, connPath, exitLane};
                                for (int pi = 0; pi < 3; pi++) {
                                    int P = paths[pi];
                                    float lo = G.isLane(P) ? G.lanes[P].u0 : 0.f, hi = G.isLane(P) ? G.lanes[P].u1 : G.pathLength(P);
                                    float la = 0.f;
                                    float hint = pi == 0 ? hi - 2.f : (pi == 1 ? Clamp(d.u, lo, hi) : (d.path == exitLane ? d.u : lo + 2.f));
                                    float uu = G.projectPath(P, pt, hint, &la);
                                    if (uu <= lo + 0.02f || uu >= hi - 0.02f) continue;
                                    if (fabsf(la) < fabsf(best)) best = la;
                                }
                                return best > 1e8f ? 0.f : best;
                            };
                            for (int k = 0; k < 4; k++) {
                                float lat = latOf(q[k]);
                                float lo = -lat - half, ro = lat - half;
                                if (approach) continue;
                                if (onConn) r.leftBox = Max(r.leftBox, lo);
                                else r.leftExit = Max(r.leftExit, lo);
                                {
                                    float la2 = 0.f;
                                    float ue = G.projectPath(exitLane, q[k], d.path == exitLane ? d.u : G.lanes[exitLane].u0 + 2.f, &la2);
                                    if (ue > G.lanes[exitLane].u0 + 4.5f && ue < G.lanes[exitLane].u1 - 0.1f) r.leftFar = Max(r.leftFar, -la2 - half);
                                }
                                r.rightOut = Max(r.rightOut, ro);
                            }
                            vec2 front = c.s.body.pos.toVec3().xy() + f * c.info.frontAxleY;
                            if (!approach) r.err = Max(r.err, fabsf(latOf(front)));
                            if (traceConn == cn && st % 12 == 0) {
                                vec2 rear = c.s.body.pos.toVec3().xy() + f * c.info.rearAxleY;
                                float lb = 1e9f;
                                for (int k = 0; k < 4; k++) lb = Min(lb, latOf(q[k]));
                                printf("  t %.2f %s u %.1f v %.1f steerCmd %.2f steerOut %.2f (%.3f rad) latF %.2f latR %.2f minCornerLat %.2f k %.3f yawRate %.2f | e %.2f psi %.2f ff %.2f st %.2f\n", st * h,
                                       onConn ? "conn" : (approach ? "appr" : "exit"), d.u, c.s.speed(), c.ctl.steer, c.s.steerOut, c.s.steerOut * c.s.tune.maxSteer, latOf(front), latOf(rear), lb,
                                       G.pathCurv(d.path, d.u), c.s.body.angVel.z, d.diag[0], d.diag[1], d.diag[2], d.diag[3]);
                            }
                        }
                        all.push_back(r);
#ifndef TS_NO_SWEEP
                        G.ensureSweeps(node);
                        const int sc = AI::sweepClass(c.info);
                        for (const ZoneCand& z : zc) {
                            if (z.minU > 1e8f && z.minUW > 1e8f) continue;
                            float zone = -1e9f, zoneWide = -1e9f;
                            for (const AI::SweepZone& sz : C.sweep)
                                if (sz.lane == z.lane && sc >= 0) {
                                    if (sz.u[sc] < 1e8f) zone = z.wait - sz.u[sc];
                                    if (sz.uWide[sc] < 1e8f) zoneWide = z.wait - sz.uWide[sc];
                                }
                            // (for calibrating the zones, AI::kSweepBand / kSweepWidePerRad / kSweepWideMax: the class's design
                            //  vehicle as lanes.cpp drives it, at bands of 1.2-2.2 m, then with the wide margin, then with the
                            //  stiffer lateral gain of 0.75 - "zonecal class conn lane real realWide d1.2 d1.45 d1.7 d1.95 d2.2
                            //  wide(1.7+2.5/0.5) g0.75(1.7)")
                            if (getenv("TT_ZONECAL") && sc >= 0) {
                                static const float kD[3][5] = {{4.0f, 0.60f, 1.0f, 1.9f, 1.2f}, {5.0f, 0.58f, 1.6f, 2.3f, 1.25f}, {6.1f, 0.60f, 2.7f, 3.8f, 1.30f}};
                                auto f2 = [](float x) { return x > -1e8f ? StrFormat("%.2f", x) : std::string("-"); };
                                const float* D = kD[sc];
                                std::string ds;
                                for (float band : {1.2f, 1.45f, 1.7f, 1.95f, 2.2f}) ds += " " + f2(designDepth(G, cn, z.lane, D[0], D[1], D[2], D[3], D[4], band));
                                ds += " " + f2(designDepth(G, cn, z.lane, D[0], D[1], D[2], D[3], D[4], AI::kSweepBand, AI::kSweepWidePerRad, AI::kSweepWideMax));
                                ds += " " + f2(designDepth(G, cn, z.lane, D[0], D[1], D[2], D[3], D[4], AI::kSweepBand, 0.f, 0.f, 0.75f));
                                printf("  zonecal %d %d %d %s %s%s\n", sc, cn, z.lane, f2(z.minU > 1e8f ? -1e9f : z.wait - z.minU).c_str(),
                                       f2(z.minUW > 1e8f ? -1e9f : z.wait - z.minUW).c_str(), ds.c_str());
                            }
                            zoneChecks.push_back({m, cn, z.lane, z.minU > 1e8f ? -1e9f : z.wait - z.minU, z.minUW > 1e8f ? -1e9f : z.wait - z.minUW, zone, zoneWide, sc});
                        }
#endif
                    }
        }
        const char* tn[] = {"straight", "right", "left", "uturn", "merge"};
        // per model worst
        for (int m = 0; m < (int)models.size(); m++) {
            float lb = 0, le = 0, ro = 0, er = 0, wb = 0, ms = 0, lf = 0;
            int n = 0, fail = 0;
            for (auto& r : all)
                if (r.model == m) {
                    n++;
                    wb = r.wb;
                    ms = r.maxSteer;
                    lf = Max(lf, r.leftFar);
                    fail += !r.done;
                    lb = Max(lb, r.leftBox);
                    le = Max(le, r.leftExit);
                    ro = Max(ro, r.rightOut);
                    er = Max(er, r.err);
                }
            if (n) printf("%-22s wb %.2f steer %.2f len %.1f: left(box) %.2f left(exit) %.2f (beyond 4.5 m %.2f) right %.2f frontErr %.2f fail %d/%d\n", models[m].name.c_str(), wb, ms, models[m].boxHalf.y * 2.f, lb, le, lf, ro, er, fail, n);
        }
        std::sort(all.begin(), all.end(), [](const Res& a, const Res& b) { return Max(a.leftBox, a.leftExit) > Max(b.leftBox, b.leftExit); });
        printf("worst movements:\n");
        for (int k = 0; k < (int)all.size() && k < 12; k++) {
            const Res& r = all[k];
            printf("  %-22s conn %d %s: left(box) %.2f left(exit) %.2f right %.2f err %.2f done %d %.1fs\n", models[r.model].name.c_str(), r.conn, tn[G.conns[r.conn].turn], r.leftBox, r.leftExit, r.rightOut, r.err, (int)r.done, r.secs);
        }
#ifndef TS_NO_SWEEP
        // the real bodies' sweep of the waiting places against the zones of their sweepClass (lanes.cpp's design vehicles)
        int covered = 0, longOnes = 0, coveredW = 0, longW = 0;
        for (const ZoneCheck& z : zoneChecks) {
            const bool ok = z.real < -1e8f || z.zone >= z.real - 0.3f, okW = z.realWide < -1e8f || z.zoneWide >= z.realWide - 0.3f;
            longOnes += z.cls >= 0 && z.real > -1e8f;
            covered += z.cls >= 0 && z.real > -1e8f && ok;
            longW += z.cls >= 0 && z.realWide > -1e8f;
            coveredW += z.cls >= 0 && z.realWide > -1e8f && okW;
            auto f1 = [](float x) { return x > -1e8f ? StrFormat("%.1f", x) : std::string("-"); };
            printf("  sweep %-22s conn %d (%s) lane %d: a car waiting there %s m behind the line, zone %s%s; a wide one %s m, zone %s%s\n", models[z.model].name.c_str(), z.conn,
                   tn[G.conns[z.conn].turn], z.lane, f1(z.real).c_str(), z.cls < 0 ? "- (keeps to its path)" : f1(z.zone).c_str(), z.cls >= 0 && !ok ? " MISSED" : "",
                   f1(z.realWide).c_str(), z.cls < 0 ? "-" : f1(z.zoneWide).c_str(), z.cls >= 0 && !okW ? " MISSED" : "");
        }
        printf("sweep zones against the real bodies: %d of %d long-body sweeps of a waiting car covered, %d of %d of a wide one; %d sweeps by cars\n", covered, longOnes, coveredW, longW,
               (int)std::count_if(zoneChecks.begin(), zoneChecks.end(), [](const ZoneCheck& z) { return z.cls < 0; }));
#endif
    }

    // street-takeover donuts: one car per model (or the named one) on the crossing nearest `at`, driven by
    // AI::donutControls for `secs` seconds; how much of the time it spins, how far it wanders, how hard the tires go
    void donutTest(vec2 at, const char* onlyModel, float secs) {
        AI::LaneGraph& G = w->lg;
        int node = -1;
        float bd = 1e9f;
        for (int n = 0; n < (int)G.nodes.size(); n++) {
            if (G.nodes[n].approaches.size() < 3) continue;
            float d = length(w->roads.nodes[n].p - at);
            if (d < bd) {
                bd = d;
                node = n;
            }
        }
        if (node < 0) return;
        vec2 spot = w->roads.nodes[node].p;
        printf("donut test on node %d (%.0f, %.0f)\n", node, spot.x, spot.y);
        const float h = 1.f / 120.f;
        for (int m = 0; m < (int)models.size(); m++) {
            const Vehicles::VehicleModel& md = models[m];
            if (md.cls >= Vehicles::VC_BOAT || md.cls == Vehicles::VC_MOTORBIKE || md.cls == Vehicles::VC_SCOOTER || md.cls == Vehicles::VC_BUS ||
                md.cls == Vehicles::VC_TRUCK)
                continue;
            if (onlyModel && md.name.find(onlyModel) == std::string::npos) continue;
            cars.assign(1, SimCar());
            SimCar& c = cars[0];
            c.used = true;
            c.model = m;
            float z = 0.f;
            if (!w->roads.surfaceHeight(spot, &z, 1e9f)) z = w->roads.nodes[node].z;
            Vehicles::initVehicle(c.s, md, m, dvec3(spot.x, spot.y, z + 0.3), 0.f);
            AI::DonutState ds;
            ds.dir = (m & 1) ? 1 : -1;
            float spinning = 0.f, maxDist = 0.f, smokeSum = 0.f, yawSum = 0.f, maxV = 0.f;
            int flips = 0, returns = 0;
            bool wasReturning = false;
            for (int st = 0; st < (int)(secs * 120.f); st++) {
                if (st % 2 == 0) c.ctl = AI::donutControls(c.s, spot, 2.f * h, ds);
                Vehicles::stepVehicle(c.s, c.ctl, h);
                float yr = fabsf(c.s.body.angVel.z);
                if (yr > 1.2f) spinning += h;
                yawSum += yr * h;
                smokeSum += ds.smoke * h;
                vec2 p = c.s.body.pos.toVec3().xy();
                maxDist = Max(maxDist, length(p - spot));
                maxV = Max(maxV, c.s.speed());
                if (c.s.up().z < 0.3f) flips++;
                if (ds.phase != 0 && !wasReturning) returns++;
                wasReturning = ds.phase != 0;
                if (verbose && st % 60 == 0)
                    printf("  %-18s t %5.1f off %5.1f %5.1f v %4.1f vF %5.1f yaw %5.2f thr %.2f hb %d steer %5.2f smoke %.2f ph %d dir %d gear %d\n", md.name.c_str(), st * h, p.x - spot.x,
                           p.y - spot.y, c.s.speed(), c.s.forwardSpeed(), c.s.body.angVel.z, c.ctl.throttle, (int)c.ctl.handbrake, c.ctl.steer, ds.smoke, ds.phase, ds.dir, c.s.gear);
            }
            printf("%-22s cls %2d mass %5.0f kW %4.0f: spinning %4.1f%% mean yaw %.2f rad/s smoke %.2f maxDist %5.1f maxV %4.1f returns %d rolled %d\n", md.name.c_str(), (int)md.cls,
                   md.mass, md.power, spinning / secs * 100.f, yawSum / secs, smokeSum / secs, maxDist, maxV, returns, flips > 0);
        }
    }

    void plot(const char* path, float viewR, vec2 at = vec2(1e9f)) {
        Canvas cv;
        cv.init(1600, at.x < 1e8f ? at : center, viewR);
        drawRoads(cv, *w);
        drawLanes(cv, *w, false);
        for (auto& tr : trails)
            for (size_t k = 0; k + 1 < tr.pts.size(); k++) {
                if (length(tr.pts[k + 1] - tr.pts[k]) > 30.f) continue;
                float f = Saturate(tr.spd[k] / 20.f);
                vec3 col = lerp(vec3(0.2f, 0.4f, 1.f), vec3(1.f, 0.9f, 0.1f), f);
                if (tr.spd[k] < 0.5f) col = vec3(0.6f, 0.2f, 0.9f);
                cv.line(tr.pts[k], tr.pts[k + 1], col, viewR < 150.f ? 0.5f : 0.8f, viewR < 150.f ? 0 : 1);
            }
        for (auto& c : cars)
            if (c.used) cv.point(c.s.body.pos.toVec3().xy(), c.parked ? vec3(0.55f, 0.55f, 0.55f) : vec3(1, 1, 1), 2);
        for (auto& p : peds)
            if (p.used) cv.point(p.pos, vec3(0.2f, 1.f, 0.4f), 1);
        for (auto& e : events) {
            vec3 col = e.kind == 0 ? vec3(1, 0.1f, 0.1f) : e.kind == 1 ? vec3(1, 0.2f, 1) : e.kind == 2 ? vec3(1, 1, 0) : e.kind == 3 ? vec3(1, 0.5f, 0) : e.kind == 4 ? vec3(0, 1, 1) : vec3(0.7f, 0.7f, 0.7f);
            cv.cross(e.p, col, e.kind == 0 || e.kind == 3 ? 6 : 4);
        }
        cv.save(path);
    }
};

}  // namespace TS

using namespace TS;

int main(int argc, char** argv) {
    Jobs::init(Max(1, Min(3, (int)std::thread::hardware_concurrency() - 1)));
    static World3 w;
    int firstArg = 1;
#ifdef TS_HAND
    if (argc > 2 && !strcmp(argv[1], "--worldbin")) {
        for (int k = 3; k < argc; k++) {
            if (!strcmp(argv[k], "--stopsigns")) w.testStops = true;
            if (!strcmp(argv[k], "--noredturn")) w.testNoRed = true;
        }
        if (!w.buildHand(argv[2])) return 1;
        firstArg = 3;
    } else
#endif
        w.build();
    for (int i = firstArg; i < argc; i++) {
        if (!strcmp(argv[i], "--probe") && i + 2 < argc) {
            vec2 p((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            printf("pointInBuilding(%.1f, %.1f): margin 0 -> %d, margin 1.8 -> %d\n", p.x, p.y, (int)World::gBuildings->pointInBuilding(p, 0.f), (int)World::gBuildings->pointInBuilding(p, 1.8f));
            {
                std::vector<int> nb;
                World::gBuildings->buildingsNear(p, 30.f, nb);
                for (int bi : nb) {
                    const World::Building& b = World::gBuildings->buildings[bi];
                    if (length(b.c - p) < 30.f) printf("  building %d c (%.1f %.1f) hx %.1f hy %.1f ax (%.2f %.2f)\n", bi, b.c.x, b.c.y, b.hx, b.hy, b.ax.x, b.ax.y);
                }
            }
            for (int n = 0; n < (int)w.lg.nodes.size(); n++)
                if (length(w.roads.nodes[n].p - p) < 20.f && w.lg.nodes[n].deadEnd) {
                    printf("  dead end node %d uturnBlocked %d conns %d\n", n, (int)w.lg.nodes[n].uturnBlocked, w.lg.nodes[n].connCount);
                    for (int c = w.lg.nodes[n].firstConn; c < w.lg.nodes[n].firstConn + w.lg.nodes[n].connCount; c++) {
                        const AI::Connector& cc = w.lg.conns[c];
                        float maxY = -1e9f, farD = 0.f;
                        for (auto& q : cc.pts) farD = Max(farD, length(q.xy() - w.roads.nodes[n].p));
                        for (auto& q : cc.pts) maxY = Max(maxY, q.y);
                        printf("    conn %d (path %d) len %.1f pts %zu maxY %.1f farthest from node %.1f\n", c, (int)w.lg.lanes.size() + c, cc.length, cc.pts.size(), maxY, farD);
                    }
                }
            std::vector<int> cand;
            w.lg.lanesNear(p - vec2(8.f), p + vec2(8.f), cand);
            for (int li : cand) {
                const AI::Lane& l = w.lg.lanes[li];
                float lat;
                float u = w.lg.projectPath(li, p, (l.u0 + l.u1) * 0.5f, &lat);
                if (fabsf(lat) > 6.f) continue;
                const World::RoadEdge& e = w.roads.edges[l.edge];
                printf("lane %d edge %d cls %d dir %d idx %d/%d off %.2f u0 %.1f u1 %.1f len %.1f stopU %.1f from %d to %d (deg %zu,%zu) proj u %.1f lat %.2f outs %zu\n", li, l.edge, l.cls, l.dir, l.index, l.count,
                       l.offset, l.u0, l.u1, e.length, l.stopU, l.fromNode, l.toNode, w.roads.nodes[l.fromNode].edges.size(), w.roads.nodes[l.toNode].edges.size(), u, lat, l.out.size());
                const World::RoadNode& tn = w.roads.nodes[l.toNode];
                printf("    to node %d at (%.1f, %.1f) z %.2f radius %.2f deadEnd %d control %d\n", l.toNode, tn.p.x, tn.p.y, tn.z, tn.radius, (int)w.lg.nodes[l.toNode].deadEnd, w.lg.nodes[l.toNode].control);
                // lane surface vs collision ground along the lane (steps / ledges the physics cars can hang on)
                w.loadCells(p, 80.f);
                for (float uu = Max(l.u0, u - 30.f); uu <= Min(l.u1, u + 30.f); uu += 3.f) {
                    vec3 lp = w.lg.pathPos(li, uu);
                    Phys::GroundHit gh = w.cw.ground(lp.x, lp.y, lp.z + 3.f, 6.f);
                    printf("      u %.1f lane z %.2f ground z %.2f%s\n", uu, lp.z, gh.z, fabsf(gh.z - lp.z) > 0.4f ? "  <-- MISMATCH" : "");
                }
            }
            i += 2;
        }
        if (!strcmp(argv[i], "--zcheck")) {
            // lane surface vs the ground the vehicle physics drives on (terrain + road decks): steps where a car can
            // get hung up (a flat node disc next to a steep ramp, a deck edge) are listed per node, worst first
            struct Hit { float step; int lane; float u; vec3 p; };
            std::vector<Hit> hits;
            long samples = 0;
            for (int li = 0; li < (int)w.lg.lanes.size(); li++) {
                const AI::Lane& l = w.lg.lanes[li];
                for (float uu = l.u0; uu <= l.u1; uu += 2.f) {
                    vec3 lp = w.lg.pathPos(li, uu);
                    float th = World::gMap->heightAt(lp.x, lp.y), rz = th;
                    float g = th;
                    if (World::gRoads->surfaceHeight(lp.xy(), &rz, lp.z + 2.5f) && rz > th - 0.5f) g = rz;
                    samples++;
                    float step = g - lp.z;
                    if (fabsf(step) > 0.45f) hits.push_back({step, li, uu, lp});
                }
            }
            // cluster by location
            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return fabsf(a.step) > fabsf(b.step); });
            std::vector<Hit> reps;
            for (const Hit& hh : hits) {
                bool near_ = false;
                for (const Hit& r : reps)
                    if (length(r.p.xy() - hh.p.xy()) < 30.f) near_ = true;
                if (!near_) reps.push_back(hh);
            }
            printf("zcheck: %ld samples, %zu off by > 0.45 m, %zu places\n", samples, hits.size(), reps.size());
            for (const Hit& r : reps) {
                const AI::Lane& l = w.lg.lanes[r.lane];
                printf("  step %+.2f m at (%.1f, %.1f) z %.2f lane %d edge %d cls %d u %.1f (u0 %.1f u1 %.1f) nodes %d->%d\n", r.step, r.p.x, r.p.y, r.p.z, r.lane, l.edge,
                       l.cls, r.u, l.u0, l.u1, l.fromNode, l.toNode);
            }
            continue;
        }
        if (!strcmp(argv[i], "--route") && i + 4 < argc) {
            vec2 a((float)atof(argv[i + 1]), (float)atof(argv[i + 2])), b((float)atof(argv[i + 3]), (float)atof(argv[i + 4]));
            static Sim sim;
            sim.init(&w, a, 50.f, 0, 0);
            sim.routeTest(a, b, false);
            i += 4;
            continue;
        }
        if (!strcmp(argv[i], "--routes") && i + 1 < argc) {
            // n random city-to-city routes (deterministic): arrival rate and time
            int n = atoi(argv[i + 1]);
            static Sim sim;
            sim.init(&w, vec2(2700, 700), 50.f, 0, 0);
            u32 hsh = 0x1234567u;
            int ok = 0, tried = 0;
            double tsum = 0.0;
            for (int k = 0; k < n; k++) {
                hsh = hash32(hsh + 17u);
                vec2 a(800.f + hashToFloat(hsh) * 4800.f, -1500.f + hashToFloat(hash32(hsh)) * 5000.f);
                hsh = hash32(hsh + 29u);
                vec2 b(800.f + hashToFloat(hsh) * 4800.f, -1500.f + hashToFloat(hash32(hsh)) * 5000.f);
                float t = sim.routeTest(a, b, true);
                if (t == -2.f) continue;   // no road near the start point
                tried++;
                if (t >= 0.f) {
                    ok++;
                    tsum += t;
                } else {
                    printf("  route %d FAILED (%.0f, %.0f) -> (%.0f, %.0f)\n", k, a.x, a.y, b.x, b.y);
                }
            }
            printf("routes: %d / %d arrived, mean %.0f s\n", ok, tried, ok ? tsum / ok : 0.0);
            i += 1;
            continue;
        }
        if (!strcmp(argv[i], "--deadends")) {
            bool all = i + 1 < argc && !strcmp(argv[i + 1], "all");
            int total = 0;
            for (int n = 0; n < (int)w.lg.nodes.size(); n++) {
                if (!w.lg.nodes[n].deadEnd) continue;
                total++;
                if (!w.lg.nodes[n].uturnBlocked && !all) continue;
                const World::RoadNode& rn = w.roads.nodes[n];
                int e = rn.edges.empty() ? -1 : rn.edges[0];
                printf("%s dead end node %d at (%.1f, %.1f) edge %d cls %d\n", w.lg.nodes[n].uturnBlocked ? "obstructed" : "open", n, rn.p.x, rn.p.y, e,
                       e >= 0 ? (int)w.roads.edges[e].cls : -1);
            }
            printf("dead ends: %d\n", total);
            if (all) i++;
            continue;
        }
        if (!strcmp(argv[i], "--overlaps")) {
            // two road edges laid over the same strip of road: along each edge (junction ends left out), the stretch where
            // another edge's centreline runs within most of a road width of it, the same way or the opposite one. Their
            // lanes then sit on top of each other and the cars on them block each other with nothing to resolve it - on
            // the harness's Sol Beach run a causeway coming in along a street a block before their junction (node 2478)
            // left queues waiting a minute and more at the junction (18 waits over 60 s in 6 minutes)
            const World::RoadNetwork& R = w.roads;
            const int NE = (int)R.edges.size();
            struct Run {
                long long key;
                float m;
                vec2 at;
            };
            std::vector<Run> runs;   // (edge pair, overlapping metres, where it starts)
            std::vector<int> nbE;
            for (int a = 0; a < NE; a++) {
                const World::RoadEdge& A = R.edges[a];
                if (A.pts.size() < 2 || A.length < 8.f) continue;
                for (float s = A.cut0 + 2.f; s < A.length - A.cut1 - 2.f; s += 4.f) {
                    vec3 p3 = A.posAt(s), t3 = A.tangentAt(s);
                    vec2 p = p3.xy(), t = normalize(t3.xy() + vec2(1e-6f, 0.f));
                    nbE.clear();
                    R.edgesInRect(p - vec2(A.halfWidth + 12.f), p + vec2(A.halfWidth + 12.f), nbE);
                    for (int b : nbE) {
                        if (b <= a) continue;
                        const World::RoadEdge& B = R.edges[b];
                        if (B.pts.size() < 2) continue;
                        // nearest point of B's centreline (outside its own junction ends)
                        float best = 1e9f, bz = 0.f, bs = 0.f;
                        vec2 bt(0.f);
                        for (size_t k = 0; k + 1 < B.pts.size(); k++) {
                            vec2 q0 = B.pts[k].xy(), q1 = B.pts[k + 1].xy(), d = q1 - q0;
                            float l2 = Max(dot(d, d), 1e-6f), f = Clamp(dot(p - q0, d) / l2, 0.f, 1.f);
                            float dd = length(q0 + d * f - p);
                            if (dd < best) {
                                best = dd;
                                bt = d / sqrtf(l2);
                                bz = Lerp(B.pts[k].z, B.pts[k + 1].z, f);
                                bs = B.dist.size() > k + 1 ? Lerp(B.dist[k], B.dist[k + 1], f) : 0.f;
                            }
                        }
                        if (bs < B.cut0 + 1.f || bs > B.length - B.cut1 - 1.f) continue;
                        if (best > 0.8f * Max(A.halfWidth, B.halfWidth) || fabsf(dot(bt, t)) < 0.85f || fabsf(bz - p3.z) > 3.f) continue;
                        long long key = (long long)a * NE + b;
                        bool found = false;
                        for (size_t r = runs.size(); r-- > 0 && !found;)
                            if (runs[r].key == key) {
                                runs[r].m += 4.f;
                                found = true;
                            }
                        if (!found) runs.push_back({key, 4.f, p});
                    }
                }
            }
            std::sort(runs.begin(), runs.end(), [](const Run& x, const Run& y) { return x.m > y.m; });
            int shown = 0;
            for (const Run& r : runs) {
                if (r.m < 12.f) break;
                int a = (int)(r.key / NE), b = (int)(r.key % NE);
                const World::RoadEdge &A = R.edges[a], &B = R.edges[b];
                printf("overlap %4.0f m from (%.0f, %.0f): edge %d (%s, cls %d, %d/%d lanes, nodes %d-%d) and edge %d (%s, cls %d, %d/%d lanes, nodes %d-%d)\n",
                       r.m, r.at.x, r.at.y, a, A.name.c_str(), (int)A.cls, A.lanesF, A.lanesB, A.n0, A.n1, b, B.name.c_str(), (int)B.cls, B.lanesF, B.lanesB, B.n0, B.n1);
                if (++shown >= 60) break;
            }
            int over = 0;
            for (const Run& r : runs) over += r.m >= 12.f;
            printf("overlapping edge pairs (12 m or more): %d\n", over);
            continue;
        }
        if (!strcmp(argv[i], "--walkcheck")) {
            const bool listAll = i + 1 < argc && !strcmp(argv[i + 1], "all");   // every hit, not just the 40 longest
            if (listAll) i++;
            const size_t listMax = listAll ? (size_t)1 << 30 : 40;
            // walk links that would take a pedestrian through a wall or along a carriageway: along each link's centre
            // line in 1 m steps, the metres inside a building footprint (shrunk by 0.3 m, so a sidewalk brushing a facade
            // passes) and, for links that are not crossings, the metres on another road's carriageway (0.8 m in from its
            // edge, within 2 m of its height, outside its junction ends)
            const AI::LaneGraph& G = w.lg;
            const World::RoadNetwork& R = w.roads;
            struct Hit {
                int link, other;   // the walk link; the building or road edge it runs into
                float m;
                vec2 at;
            };
            std::vector<Hit> inBuilding, onRoad;
            float metres[2][5] = {{0.f}};
            int links[2][5] = {{0}};
            std::vector<int> nbE, nbB;
            for (int li = 0; li < (int)G.walkLinks.size(); li++) {
                const AI::WalkLink& L = G.walkLinks[li];
                if (L.length < 0.5f || L.kind > AI::WL_ZEBRA) continue;
                const int steps = Max(1, (int)ceilf(L.length));
                const float stepM = L.length / (float)steps;
                Hit hb = {li, -1, 0.f, vec2(0.f)}, he = {li, -1, 0.f, vec2(0.f)};
                for (int k = 0; k <= steps; k++) {
                    vec3 q3 = G.walkPos(li, L.length * (float)k / (float)steps, 0.f, true);
                    vec2 q = q3.xy();
                    if (World::gBuildings) {
                        nbB.clear();
                        World::gBuildings->buildingsNear(q, 1.f, nbB);
                        for (int bi : nbB) {
                            const World::Building& b = World::gBuildings->buildings[bi];
                            vec2 d = q - b.c, side(-b.ax.y, b.ax.x);
                            if (fabsf(dot(d, b.ax)) < b.hx - 0.3f && fabsf(dot(d, side)) < b.hy - 0.3f && q3.z > b.baseZ - 3.f && q3.z < b.baseZ + b.height) {
                                if (hb.other < 0) {
                                    hb.other = bi;
                                    hb.at = q;
                                }
                                hb.m += stepM;
                                break;
                            }
                        }
                    }
                    if (L.kind == AI::WL_CROSSWALK || L.kind == AI::WL_ZEBRA) continue;
                    nbE.clear();
                    R.edgesInRect(q - vec2(12.f), q + vec2(12.f), nbE);
                    for (int ei : nbE) {
                        const World::RoadEdge& E = R.edges[ei];
                        if (E.pts.size() < 2) continue;
                        float best = 1e9f, bz = 0.f, bs = 0.f;
                        bool bright = false;   // (the side of the edge it is on: the carriageway reaches that side's kerb)
                        for (size_t s = 0; s + 1 < E.pts.size(); s++) {
                            vec2 q0 = E.pts[s].xy(), dq = E.pts[s + 1].xy() - q0;
                            float l2 = Max(dot(dq, dq), 1e-6f), f = Clamp(dot(q - q0, dq) / l2, 0.f, 1.f);
                            float dd = length(q0 + dq * f - q);
                            if (dd < best) {
                                best = dd;
                                bz = Lerp(E.pts[s].z, E.pts[s + 1].z, f);
                                bs = E.dist.size() > s + 1 ? Lerp(E.dist[s], E.dist[s + 1], f) : 0.f;
                                bright = cross(dq, q - q0) < 0.f;
                            }
                        }
                        const float kerbHere = E.halfWidthSide(bright ? 1 : -1);
                        if (bs < E.cut0 || bs > E.length - E.cut1 || best > kerbHere - 0.8f || fabsf(bz - q3.z) > 2.f) continue;
                        if (he.other < 0) {
                            he.other = ei;
                            he.at = q;
                        }
                        he.m += stepM;
                        break;
                    }
                }
                if (hb.m >= 1.f) {
                    inBuilding.push_back(hb);
                    links[0][L.kind]++;
                    metres[0][L.kind] += hb.m;
                }
                if (he.m >= 1.f) {
                    onRoad.push_back(he);
                    links[1][L.kind]++;
                    metres[1][L.kind] += he.m;
                }
            }
            static const char* kKindName[5] = {"sidewalk", "crosswalk", "corner", "path", "zebra"};
            auto byMetres = [](const Hit& x, const Hit& y) { return x.m > y.m; };
            std::sort(inBuilding.begin(), inBuilding.end(), byMetres);
            std::sort(onRoad.begin(), onRoad.end(), byMetres);
            for (size_t k = 0; k < inBuilding.size() && k < listMax; k++) {
                const Hit& h = inBuilding[k];
                const AI::WalkLink& L = G.walkLinks[h.link];
                const World::Building& b = World::gBuildings->buildings[h.other];
                printf("in building %4.1f m from (%.1f, %.1f): walk link %d (%s, %.1f m, edge %d) through building %d (style %d, %.1f x %.1f m, interior %d)\n", h.m, h.at.x, h.at.y,
                       h.link, kKindName[L.kind], L.length, L.edge, h.other, (int)b.style, b.hx * 2.f, b.hy * 2.f, (int)b.interior);
            }
            for (size_t k = 0; k < onRoad.size() && k < listMax; k++) {
                const Hit& h = onRoad[k];
                const AI::WalkLink& L = G.walkLinks[h.link];
                const World::RoadEdge& E = R.edges[h.other];
                printf("on road %4.1f m from (%.1f, %.1f): walk link %d (%s, %.1f m, edge %d, junction %d) on edge %d (%s, cls %d, half width %.1f, nodes %d-%d)\n", h.m, h.at.x,
                       h.at.y, h.link, kKindName[L.kind], L.length, L.edge, G.walkNodes[L.a].roadNode, h.other, E.name.c_str(), (int)E.cls, E.halfWidth, E.n0, E.n1);
            }
            for (int t = 0; t < 2; t++) {
                printf("%s:", t ? "walk links on a carriageway" : "walk links through buildings");
                for (int k = 0; k < 5; k++) printf(" %s %d (%.0f m)", kKindName[k], links[t][k], metres[t][k]);
                printf("\n");
            }
            printf("walk links checked: %d\n", (int)G.walkLinks.size());
            continue;
        }
#ifndef TS_NO_SWEEP
        if (!strcmp(argv[i], "--sweeps")) {
            // the long vehicles' sweep zones (lanes.cpp): how far behind each lane's line the design van's, truck's and
            // bus's body reaches, by depth, and the deepest ones; with a node, every zone of its turns
            const int only = i + 1 < argc && argv[i + 1][0] != '-' ? atoi(argv[++i]) : -1;
            const char* kCls[3] = {"van", "truck", "bus"};
            const AI::LaneGraph& G = w.lg;
            const double t0 = TimeSeconds();
            for (int n = 0; n < (int)G.nodes.size(); n++) G.ensureSweeps(n);
            printf("sweep zones of every junction worked out in %.3f s (in the game: each on first use)\n", TimeSeconds() - t0);
            const float edges[6] = {0.5f, 1.5f, 3.f, 6.f, 10.f, 1e9f};
            int hist[3][6] = {{0}}, histW[3][6] = {{0}};
            struct Deep { float d; int conn, lane, cls; };
            std::vector<Deep> deep;
            int turns = 0;
            for (int ci = 0; ci < (int)G.conns.size(); ci++) {
                const AI::Connector& c = G.conns[ci];
                turns += (c.turn == AI::TK_LEFT || c.turn == AI::TK_RIGHT) && c.minRadius <= 25.f;
                for (const AI::SweepZone& z : c.sweep) {
                    const AI::Lane& L = G.lanes[z.lane];
                    const float wait = L.stopU >= 0.f ? L.stopU : L.u1 - 0.6f;
                    for (int k = 0; k < 3; k++) {
                        if (z.uWide[k] < 1e8f) {
                            int b = 0;
                            while (wait - z.uWide[k] > edges[b]) b++;
                            histW[k][b]++;
                        }
                        if (z.u[k] > 1e8f) continue;
                        const float dpt = wait - z.u[k];
                        int b = 0;
                        while (dpt > edges[b]) b++;
                        hist[k][b]++;
                        deep.push_back({dpt, ci, z.lane, k});
                        if (c.node == only)
                            printf("  node %d conn %d (%s, lane %d -> %d, radius %.1f): %s %.1f m behind the line of lane %d\n", only, ci, c.turn == AI::TK_LEFT ? "left" : "right",
                                   c.from, c.to, c.minRadius, kCls[k], dpt, z.lane);
                    }
                }
            }
            std::sort(deep.begin(), deep.end(), [](const Deep& a, const Deep& b) { return a.d > b.d; });
            printf("sweep zones over %d tight turns (radius 25 m or less):\n", turns);
            for (int k = 0; k < 3; k++)
                printf("  %s: behind the line by <0.5 m %d, 0.5-1.5 %d, 1.5-3 %d, 3-6 %d, 6-10 %d, 10+ %d | a wide one waiting: %d %d %d %d %d %d\n", kCls[k], hist[k][0], hist[k][1],
                       hist[k][2], hist[k][3], hist[k][4], hist[k][5], histW[k][0], histW[k][1], histW[k][2], histW[k][3], histW[k][4], histW[k][5]);
            for (size_t k = 0; k < deep.size() && k < 12; k++) {
                const AI::Connector& c = G.conns[deep[k].conn];
                vec2 np = w.roads.nodes[c.node].p;
                printf("  deep: %s %.1f m on lane %d, conn %d (%s, node %d at %.0f %.0f, radius %.1f)\n", kCls[deep[k].cls], deep[k].d, deep[k].lane, deep[k].conn,
                       c.turn == AI::TK_LEFT ? "left" : "right", c.node, np.x, np.y, c.minRadius);
            }
            continue;
        }
#endif
#ifndef TS_NO_SECTIONS
        if (!strcmp(argv[i], "--sections")) {
            // each edge's cross-section as the lane graph has it (authored in hand mode: listed where its sides differ or
            // its lane width, median or limit is not the class's), and the walk graph and parking against it: sidewalk
            // links off their side's sidewalk centre line, the kerb lanes that park by side of the edge
            const AI::LaneGraph& G = w.lg;
            int authored = 0, listed = 0, offLink = 0, sidewalkLinks = 0, parkLanes[2] = {0, 0}, kerbLanes[2] = {0, 0};
            for (int ei = 0; ei < (int)G.sections.size() && ei < (int)w.roads.edges.size(); ei++) {
                const AI::EdgeSection& S = G.sections[ei];
                const World::RoadEdge& E = w.roads.edges[ei];
                const World::RoadClassInfo& ri = World::roadInfo(E.cls);
                authored += S.authored;
                const bool sides = fabsf(S.kerb[0] - S.kerb[1]) > 0.01f || fabsf(S.walk[0] - S.walk[1]) > 0.01f || fabsf(S.park[0] - S.park[1]) > 0.01f;
                const bool own = fabsf(S.laneWidth - ri.laneWidth) > 0.01f || fabsf(S.speed - ri.speed) > 0.01f || S.median > 0.f || S.flags;
                if (S.authored && (sides || own)) {
                    listed++;
                    if (sides) {
                        // (where its sidewalks run, at the middle of the edge: for placing a test there)
                        const vec3 mp = E.posAt(E.length * 0.5f);
                        const vec3 mt = E.tangentAt(E.length * 0.5f);
                        const vec2 rt = normalize(vec2(mt.y, -mt.x));
                        const vec2 sl = mp.xy() - rt * (S.kerb[0] + S.walk[0] * 0.5f), sr = mp.xy() + rt * (S.kerb[1] + S.walk[1] * 0.5f);
                        printf("  edge %d middle (%.1f, %.1f): left sidewalk (%.1f, %.1f), right sidewalk (%.1f, %.1f)\n", ei, mp.x, mp.y, sl.x, sl.y, sr.x, sr.y);
                    }
                    printf("edge %d (%s, %s, %d+%d lanes): lane %.2f median %.2f limit %.1f flags %d | kerb line L %.2f R %.2f (half width %.2f) | sidewalk L %.2f R %.2f (%.2f) | parking L %.2f R %.2f\n",
                           ei, E.name.c_str(), ri.name, (int)E.lanesF, (int)E.lanesB, S.laneWidth, S.median, S.speed, (int)S.flags, S.kerb[0], S.kerb[1], E.halfWidth,
                           S.walk[0], S.walk[1], E.sidewalk, S.park[0], S.park[1]);
                }
            }
            for (const AI::WalkLink& L : G.walkLinks) {
                if (L.kind != AI::WL_SIDEWALK || L.edge < 0 || L.edge >= (int)G.sections.size()) continue;
                const AI::EdgeSection& S = G.sections[L.edge];
                const int es = L.lat > 0.f ? 1 : 0;
                const float want = (es ? 1.f : -1.f) * (S.kerb[es] + S.walk[es] * 0.5f);
                sidewalkLinks++;
                if (fabsf(L.lat - want) > 0.01f) offLink++;
            }
            for (int li = 0; li < (int)G.lanes.size(); li++) {
                const AI::Lane& L = G.lanes[li];
                if (L.right >= 0 || (L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP))) continue;
                const int es = L.dir > 0 ? 1 : 0;   // (the edge's side the lane's kerb is on)
                kerbLanes[es]++;
                if (G.parkWidth(li) >= 1.8f) parkLanes[es]++;
            }
            printf("sections: %d edges, %d authored, %d listed; sidewalk links %d, off their side's centre line %d; kerb lanes parking: left %d of %d, right %d of %d\n",
                   (int)G.sections.size(), authored, listed, sidewalkLinks, offLink, parkLanes[0], kerbLanes[0], parkLanes[1], kerbLanes[1]);
            continue;
        }
#endif
        if (!strcmp(argv[i], "--lanetime")) {
            // the lane graph's build time on this world: rebuilt N times (default 5) after the first, each time and the median
            const int n = i + 1 < argc && argv[i + 1][0] != '-' ? Max(1, atoi(argv[++i])) : 5;
            std::vector<double> ts;
            for (int k = 0; k < n; k++) {
#ifdef TS_HAND
                if (World::gWorldKind == World::WorldKind::Hand) World3::buildLanes(w.lg, w.roads, &w.ov, 0);
                else
#endif
                    w.lg.build(w.roads);
                ts.push_back(w.lg.buildSeconds);
            }
            std::vector<double> sorted = ts;
            std::sort(sorted.begin(), sorted.end());
            printf("lane graph build:");
            for (double t : ts) printf(" %.3f", t);
            printf(" s; median %.3f s\n", sorted[sorted.size() / 2]);
            continue;
        }
        if (!strcmp(argv[i], "--sites") && i + 3 < argc) {
            // site elements near a point (venue anchors for the population: gates, terminals, docks, ramps)
            vec2 p((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            i += 3;
            if (World::gSites)
                for (size_t k = 0; k < World::gSites->elems.size(); k++) {
                    const World::SiteElem& e = World::gSites->elems[k];
                    if (length(e.c - p) > r) continue;
                    printf("elem %zu kind %d var %d c (%.1f %.1f) ax (%.2f %.2f) hx %.1f hy %.1f z %.1f h %.1f text '%s' p %.1f %.1f %.1f %.1f\n", k, e.kind, e.variant, e.c.x, e.c.y,
                           e.ax.x, e.ax.y, e.hx, e.hy, e.z, e.h, e.text.c_str(), e.p[0], e.p[1], e.p[2], e.p[3]);
                }
            std::vector<int> cand;
            w.roads.edgesInRect(p - vec2(r), p + vec2(r), cand);
            for (int ei : cand) {
                const World::RoadEdge& e = w.roads.edges[ei];
                printf("edge %d cls %d n0 %d (%.0f %.0f) n1 %d (%.0f %.0f) len %.0f hw %.1f sw %.1f lanes %d/%d flags %d\n", ei, e.cls, e.n0, w.roads.nodes[e.n0].p.x, w.roads.nodes[e.n0].p.y, e.n1,
                       w.roads.nodes[e.n1].p.x, w.roads.nodes[e.n1].p.y, e.length, e.halfWidth, e.sidewalk, e.lanesF, e.lanesB, e.flags);
            }
            continue;
        }
        if (!strcmp(argv[i], "--profile") && i + 5 < argc) {
            // a cross-section from a to b (n samples): water, terrain, collision ground, road surface, region, lanes -
            // where a person can stand beside a road (causeway shoulders, curbs, quays)
            vec2 a((float)atof(argv[i + 1]), (float)atof(argv[i + 2])), b((float)atof(argv[i + 3]), (float)atof(argv[i + 4]));
            int n = Max(2, atoi(argv[i + 5]));
            i += 5;
            w.loadCells((a + b) * 0.5f, length(b - a) * 0.5f + 40.f);
            for (int k = 0; k < n; k++) {
                vec2 q = a + (b - a) * (k / (float)(n - 1));
                Phys::GroundHit gh = w.cw.ground(q.x, q.y, 30.f, 0.f);
                float rz = 0.f, pz = 0.f;
                bool on = w.roads.surfaceHeight(q, &rz, 1e9f);
                bool pad = World::gSites && World::gSites->padHeight(q, &pz, 1e9f);
                float u = 0.f, lat = 0.f;
                int ln = w.lg.nearestLane(q, vec2(0.f), 10.f, &u, &lat);
                printf("(%.1f, %.1f) water %d terrain %.2f ground %.2f surf %d road %d %.2f pad %d %.2f region %d lane %d lat %.2f width %.2f\n", q.x, q.y, (int)w.map.isWater(q.x, q.y),
                       w.map.heightAt(q.x, q.y), gh.z, gh.surface, (int)on, rz, (int)pad, pz, (int)w.map.regionAt(q.x, q.y), ln, lat, ln >= 0 ? w.lg.lanes[ln].width : 0.f);
            }
            continue;
        }
        if (!strcmp(argv[i], "--nearlane") && i + 4 < argc) {
            // the lane nearest to a point for a heading (what a kerb check / a spawn would pick), with its neighbours
            vec2 p((float)atof(argv[i + 1]), (float)atof(argv[i + 2])), f((float)atof(argv[i + 3]), (float)atof(argv[i + 4]));
            i += 4;
            float u = 0.f, lat = 0.f;
            int ln = w.lg.nearestLane(p, f, 6.f, &u, &lat);
            if (ln >= 0) {
                const AI::Lane& L = w.lg.lanes[ln];
                vec2 t = w.lg.laneTangent(ln, u);
                printf("nearest lane %d u %.1f (%.1f..%.1f) lat %.2f width %.2f left %d right %d tangent (%.2f %.2f)\n", ln, u, L.u0, L.u1, lat, L.width, L.left, L.right, t.x, t.y);
            } else {
                printf("no lane within 6 m\n");
            }
            continue;
        }
        if (!strcmp(argv[i], "--crossings") && i + 3 < argc) {
            // the crossings a street takeover can use (events.cpp): four approaches of streets / avenues, signals or an
            // all-way stop, lanes in and out of each - with the district, so a test knows where to stand
            vec2 p((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            i += 3;
            int found = 0;
            for (int n = 0; n < (int)w.lg.nodes.size(); n++) {
                const AI::NodeInfo& N = w.lg.nodes[n];
                const World::RoadNode& rn = w.roads.nodes[n];
                if (length(rn.p - p) > r || N.approaches.size() != 4 || N.control == 0 || N.gradeSeparated) continue;
                bool streets = true;
                for (const AI::Approach& A : N.approaches) {
                    const World::RoadEdge& ed = w.roads.edges[A.edge];
                    if (ed.cls != World::RC_STREET && ed.cls != World::RC_AVENUE) streets = false;
                    if (A.inLanes.empty() || A.outLanes.empty()) streets = false;
                }
                if (!streets) continue;
                found++;
                printf("crossing node %d at (%.1f, %.1f) z %.1f radius %.1f control %d region %d\n", n, rn.p.x, rn.p.y, rn.z, rn.radius, N.control, (int)w.map.regionAt(rn.p.x, rn.p.y));
            }
            printf("takeover crossings within %.0f m: %d\n", r, found);
            continue;
        }
        if (!strcmp(argv[i], "--places")) {
            // named places and their people anchors (sites.cpp): counts by kind and group, a few samples each
            if (!World::gSites) continue;
            const World::SiteSet& S = *World::gSites;
            for (size_t pi = 0; pi < S.places.size(); pi++) {
                const World::NamedPlace& P = S.places[pi];
                int byKind[World::PA_COUNT] = {};
                int groups = 0, last = -1;
                for (const World::PlaceAnchor& a : S.anchors)
                    if (a.place == pi) {
                        byKind[a.kind]++;
                        if (a.group != last) groups++;
                        last = a.group;
                    }
                printf("place %zu '%s' kind %d at (%.0f %.0f) door (%.0f %.0f) r %.0f | sit %d stand %d waypoint %d guard %d mourn %d work %d exercise %d ground %d | groups ~%d\n", pi,
                       P.name.c_str(), P.kind, P.pos.x, P.pos.y, P.door.x, P.door.y, P.radius, byKind[0], byKind[1], byKind[2], byKind[3], byKind[4], byKind[5], byKind[6], byKind[7], groups);
                int shown[World::PA_COUNT] = {};
                for (const World::PlaceAnchor& a : S.anchors)
                    if (a.place == pi && shown[a.kind]++ < 3)
                        printf("   anchor kind %d group %d at (%.1f %.1f %.2f) face (%.2f %.2f)\n", a.kind, a.group, a.pos.x, a.pos.y, a.pos.z, a.face.x, a.face.y);
            }
            printf("walks %zu\n", S.walks.size());
            for (const World::SiteWalk& w : S.walks)
                printf("   walk kind %d (%.1f %.1f %.2f) -> (%.1f %.1f %.2f) hw %.1f\n", w.kind, w.a.x, w.a.y, w.a.z, w.b.x, w.b.y, w.b.z, w.halfWidth);
            continue;
        }
        if (!strcmp(argv[i], "--place") && i + 1 < argc) {
            // every people anchor of the places whose name has this in it (for aiming test cameras)
            const char* key = argv[++i];
            if (!World::gSites) continue;
            const World::SiteSet& S = *World::gSites;
            for (size_t pi = 0; pi < S.places.size(); pi++) {
                const World::NamedPlace& P = S.places[pi];
                if (!strstr(P.name.c_str(), key)) continue;
                printf("place %zu '%s' at (%.0f %.0f) r %.0f\n", pi, P.name.c_str(), P.pos.x, P.pos.y, P.radius);
                for (const World::PlaceAnchor& a : S.anchors)
                    if (a.place == pi)
                        printf("   kind %d group %d at (%.1f %.1f %.2f) face (%.2f %.2f)\n", a.kind, a.group, a.pos.x, a.pos.y, a.pos.z, a.face.x, a.face.y);
            }
            continue;
        }
        if (!strcmp(argv[i], "--ramps")) {
            // interchange ramps, clustered (for picking harness hotspots): where they are and how many lanes each has
            const AI::LaneGraph& G = w.lg;
            std::vector<vec2> centers;
            std::vector<int> counts;
            for (int l = 0; l < (int)G.lanes.size(); l++) {
                if (!(G.lanes[l].flags & AI::LF_RAMP)) continue;
                vec2 p = G.lanePos(l, 0.5f * (G.lanes[l].u0 + G.lanes[l].u1)).xy();
                int k = 0;
                for (; k < (int)centers.size(); k++)
                    if (length(centers[k] - p) < 300.f) break;
                if (k == (int)centers.size()) {
                    centers.push_back(p);
                    counts.push_back(0);
                }
                counts[k]++;
            }
            for (size_t k = 0; k < centers.size(); k++) printf("interchange ~(%.0f, %.0f): %d ramp lanes\n", centers[k].x, centers[k].y, counts[k]);
            continue;
        }
        if (!strcmp(argv[i], "--stairprobe") && i + 3 < argc) {
            // --stairprobe Z X,Y X,Y ...: the AI's stair-pace test (ai.cpp ai_pace::stairs) at each point, 16 ways round:
            // the ground 0.3 m back, under it and 0.3 / 0.6 / 0.9 m on, each from the one before; a flight = two or more
            // steps of 10-40 cm all the same way and nothing over 40 cm
            float z0 = (float)atof(argv[i + 1]);
            int flights = 0, tests = 0;
            for (int a = i + 2; a < argc && argv[a][0] != '-'; a++) {
                vec2 p(0.f);
                if (sscanf(argv[a], "%f,%f", &p.x, &p.y) != 2) continue;
                w.loadCells(p, 30.f);
                std::string ways;
                for (int k = 0; k < 16; k++) {
                    float an = kTwoPi * k / 16.f;
                    vec2 d(cosf(an), sinf(an));
                    float h[5];
                    h[1] = w.cw.ground(p.x, p.y, z0 + 0.1f, 0.55f).z;
                    h[0] = w.cw.ground(p.x - d.x * 0.3f, p.y - d.y * 0.3f, h[1], 0.55f).z;
                    for (int q = 2; q < 5; q++) h[q] = w.cw.ground(p.x + d.x * 0.3f * (q - 1), p.y + d.y * 0.3f * (q - 1), h[q - 1], 0.55f).z;
                    int ups = 0, downs = 0, other = 0;
                    for (int q = 0; q < 4; q++) {
                        float dh = h[q + 1] - h[q];
                        if (fabsf(dh) >= 0.1f && fabsf(dh) < 0.4f) (dh > 0.f ? ups : downs)++;
                        else if (fabsf(dh) >= 0.4f) other++;
                    }
                    bool flight = other == 0 && ((ups >= 2 && downs == 0) || (downs >= 2 && ups == 0));
                    tests++;
                    flights += flight;
                    ways += flight ? (ups ? "^" : "v") : ".";
                }
                printf("  (%.1f, %.1f) ground %.2f: %s\n", p.x, p.y, w.cw.ground(p.x, p.y, z0 + 0.1f, 0.55f).z, ways.c_str());
            }
            printf("stair probe: %d of %d ways read as a flight\n", flights, tests);
            break;
        }
        if (!strcmp(argv[i], "--groundline") && i + 5 < argc) {
            // --groundline X0 Y0 X1 Y1 Z: the ground every 0.1 m along a line, each query from the height before it (a step
            // up at most 0.55 m, as the AI's stair probe and the ped controller ask)
            vec2 a((float)atof(argv[i + 1]), (float)atof(argv[i + 2])), b((float)atof(argv[i + 3]), (float)atof(argv[i + 4]));
            float z = (float)atof(argv[i + 5]);
            w.loadCells((a + b) * 0.5f, length(b - a) * 0.5f + 40.f);
            int n = (int)(length(b - a) / 0.1f);
            for (int k = 0; k <= n; k++) {
                vec2 p = a + (b - a) * ((float)k / Max(n, 1));
                Phys::GroundHit gh = w.cw.ground(p.x, p.y, z, 0.55f);
                printf("  %.2f (%.2f, %.2f) ground %.3f surf %d%s\n", k * 0.1f, p.x, p.y, gh.z, gh.surface, k > 0 && fabsf(gh.z - z) >= 0.05f ? "  <- step" : "");
                z = gh.z;
            }
            break;
        }
        if (!strcmp(argv[i], "--meettest") && i + 3 < argc) {
            // --meettest EDGE MODEL_A MODEL_B: two vehicles meeting on the two lanes of an edge (see Sim::meetTest)
            int e = atoi(argv[i + 1]);
            static Sim sim;
            vec2 c = w.roads.edges[e].pts.empty() ? vec2(0.f) : w.roads.edges[e].pts[w.roads.edges[e].pts.size() / 2].xy();
            w.loadCells(c, 160.f);
            sim.init(&w, c, 50.f, 0, 0);
            sim.meetTest(e, argv[i + 2], argv[i + 3]);
            break;
        }
        if (!strcmp(argv[i], "--uturntest") && i + 3 < argc) {
            // --uturntest X Y R [MODEL or A,B,...]: every model (or those named) round every dead end's turning circle
            // within R m of (X, Y)
            vec2 c((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            const char* only = i + 4 < argc && argv[i + 4][0] != '-' ? argv[i + 4] : nullptr;
            w.loadCells(c, 80.f);
            static Sim sim;
            sim.init(&w, c, 50.f, 0, 0);
            sim.uturnTest(c, r, only);
            break;
        }
        if (!strcmp(argv[i], "--turntest") && i + 2 < argc) {
            vec2 c((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            const char* only = i + 3 < argc && argv[i + 3][0] != '-' ? argv[i + 3] : nullptr;
            w.loadCells(c, 120.f);
            static Sim sim;
            sim.init(&w, c, 50.f, 0, 0);
            sim.turnTest(c, only);
            break;
        }
        if (!strcmp(argv[i], "--donut") && i + 2 < argc) {
            vec2 c((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            const char* only = i + 3 < argc && argv[i + 3][0] != '-' ? argv[i + 3] : nullptr;
            float secs = 40.f;
            bool verbose = false;
            for (int k = i + 3; k < argc; k++) {
                if (!strcmp(argv[k], "--secs") && k + 1 < argc) secs = (float)atof(argv[++k]);
                else if (!strcmp(argv[k], "-v")) verbose = true;
            }
            w.loadCells(c, 120.f);
            static Sim sim;
            sim.verbose = verbose;
            sim.init(&w, c, 50.f, 0, 0);
            sim.donutTest(c, only, secs);
            break;
        }
        if (!strcmp(argv[i], "--sim") && i + 3 < argc) {
            vec2 c((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            i += 3;
            int ncars = 60, npeds = 40, nparked = 0;
            float minutes = 3.f, dummyR = -1.f, detailOff = 0.f;
            int snapLimit = 0;
            vec2 detailAt(1e9f);
            const char* plotPath = nullptr;
            bool verbose = false;
            static Sim sim;
            for (int k = i + 1; k < argc; k++) {
                if (!strcmp(argv[k], "--cars") && k + 1 < argc) ncars = atoi(argv[++k]);
                else if (!strcmp(argv[k], "--peds") && k + 1 < argc) npeds = atoi(argv[++k]);
                else if (!strcmp(argv[k], "--minutes") && k + 1 < argc) minutes = (float)atof(argv[++k]);
                else if (!strcmp(argv[k], "--plot") && k + 1 < argc) plotPath = argv[++k];
                else if (!strcmp(argv[k], "--dummy") && k + 1 < argc) dummyR = (float)atof(argv[++k]);
                else if (!strcmp(argv[k], "-v")) verbose = true;
                else if (!strcmp(argv[k], "--seed") && k + 1 < argc) sim.rng = Rng((u64)strtoull(argv[++k], nullptr, 10));   // another run of the same area
                else if (!strcmp(argv[k], "--snaps") && k + 1 < argc) snapLimit = atoi(argv[++k]);
                else if (!strcmp(argv[k], "--bikes") && k + 1 < argc) sim.bikeScale = (float)atof(argv[++k]);
                else if (!strcmp(argv[k], "--parked") && k + 1 < argc) nparked = atoi(argv[++k]);
                else if (!strcmp(argv[k], "--respond") && k + 3 < argc) {
                    sim.respondGoal.x = (float)atof(argv[++k]);
                    sim.respondGoal.y = (float)atof(argv[++k]);
                    sim.respondAt = (float)atof(argv[++k]);
                }
                else if (!strcmp(argv[k], "--overshoot")) sim.overshoot = true;
                else if (!strcmp(argv[k], "--watch") && k + 3 < argc) {
                    sim.watchCar = atoi(argv[++k]);
                    sim.watchT0 = (float)atof(argv[++k]);
                    sim.watchT1 = (float)atof(argv[++k]);
                }
                else if (!strcmp(argv[k], "--detail") && k + 1 < argc) detailOff = (float)atof(argv[++k]);
                else if (!strcmp(argv[k], "--detailat") && k + 2 < argc) {
                    detailAt.x = (float)atof(argv[++k]);
                    detailAt.y = (float)atof(argv[++k]);
                }
            }
            w.loadCells(c, r + 120.f);
            sim.verbose = verbose;
            sim.snapLimit = snapLimit;
            sim.snapDir = plotPath ? std::string(plotPath).substr(0, std::string(plotPath).find_last_of('/') + 1) : std::string();
            sim.dummyRadius = dummyR;
            sim.init(&w, c, r, ncars, npeds);
            if (nparked > 0) sim.spawnParked(nparked);
            LOG("Simulating %.1f minutes around (%.0f, %.0f) r=%.0f with %d cars, %d peds, %d parked", minutes, c.x, c.y, r, ncars, npeds, sim.nParked);
            sim.run(minutes * 60.f);
            sim.report();
            if (plotPath) {
                sim.plot(plotPath, r + 40.f);
                // detail view around the most visited intersection near the center
                std::string dp = std::string(plotPath) + ".detail.ppm";
                sim.plot(dp.c_str(), 60.f, detailAt.x < 1e8f ? detailAt : c + vec2(detailOff, detailOff));
            }
            break;
        }
        if (!strcmp(argv[i], "--lane") && i + 3 < argc) {
            int id = atoi(argv[i + 1]);
            float a = (float)atof(argv[i + 2]), b = (float)atof(argv[i + 3]);
            i += 3;
            const AI::LaneGraph& G = w.lg;
            if (id < 0 || id >= (int)G.lanes.size()) continue;
            const AI::Lane& L = G.lanes[id];
            printf("lane %d edge %d dir %d idx %d/%d left %d right %d cls %d flags %d u %.1f..%.1f stopU %.1f width %.2f from node %d to node %d speed %.1f out", id, L.edge, L.dir,
                   L.index, L.count, L.left, L.right, L.cls, L.flags, L.u0, L.u1, L.stopU, L.width, L.fromNode, L.toNode, L.speed);
            for (int c : L.out) printf(" %d(to lane %d turn %d)", c, G.conns[c].to, G.conns[c].turn);
            printf("\n");
            for (float u = Max(a, L.u0); u <= Min(b, L.u1) + 0.01f; u += 2.f) {
                vec3 q = G.lanePos(id, u);
                vec2 t = G.laneTangent(id, u);
                printf("  u %7.1f (%.2f %.2f %.2f) t (%.3f %.3f) curv %+.4f\n", u, q.x, q.y, q.z, t.x, t.y, G.pathCurv(id, u));
            }
            continue;
        }
        if (!strcmp(argv[i], "--node") && i + 1 < argc) {
            int id = atoi(argv[++i]);
            const AI::LaneGraph& G = w.lg;
            if (id < 0 || id >= (int)G.nodes.size()) continue;
            const AI::NodeInfo& N = G.nodes[id];
            printf("node %d at (%.1f %.1f) control %d axes %d deadEnd %d cycle %.1f offset %.1f phases %zu leftPhase %d %d %d\n", id, w.roads.nodes[id].p.x, w.roads.nodes[id].p.y, N.control,
                   N.axisCount, (int)N.deadEnd, N.cycle, N.offset, N.phases.size(), N.leftPhase[0], N.leftPhase[1], N.leftPhase[2]);
            for (const AI::Phase& ph : N.phases) printf("  phase axis %d kind %d t %.1f..%.1f\n", ph.axis, ph.kind, ph.t0, ph.t1);
            for (size_t a = 0; a < N.approaches.size(); a++) {
                const AI::Approach& A = N.approaches[a];
                std::string tl;
                for (int t = 0; t < 120; t += 2) tl += "RAGa-N"[Min((int)G.movementSignal(id, (int)a, AI::TK_STRAIGHT, t), 5)];
                printf("  appr %zu edge %d out %d axis %d rank %d dir (%.2f %.2f) in %zu out %zu | %s\n", a, A.edge, (int)A.outgoing, A.axis, A.rank, A.dir.x, A.dir.y, A.inLanes.size(),
                       A.outLanes.size(), tl.c_str());
            }
            continue;
        }
        if (!strcmp(argv[i], "--conn") && i + 1 < argc) {
            int id = atoi(argv[++i]);
            const AI::LaneGraph& G = w.lg;
            if (id < 0 || id >= (int)G.conns.size()) continue;
            const AI::Connector& C = G.conns[id];
            w.loadCells(C.pts.empty() ? vec2(0.f) : C.pts[0].xy(), 60.f);
            printf("conn %d node %d turn %d appr %d->%d from lane %d to lane %d len %.1f maxSpeed %.1f minRadius %.1f holdS %.1f conflicts %zu\n", id, C.node, C.turn, C.approach, C.outApproach, C.from,
                   C.to, C.length, C.maxSpeed, C.minRadius, C.holdS, C.conflicts.size());
            for (size_t k = 0; k < C.pts.size(); k++)
                printf("  s %5.1f (%.2f %.2f %.2f) curv %+.3f\n", C.s[k], C.pts[k].x, C.pts[k].y, C.pts[k].z, C.curv[k]);
            for (int ln : {C.from, C.to}) {
                const AI::Lane& L = G.lanes[ln];
                vec3 a = G.lanePos(ln, L.u0), b = G.lanePos(ln, L.u1);
                printf("lane %d u %.1f..%.1f (%.1f %.1f)->(%.1f %.1f) width %.2f idx %d/%d cls %d\n", ln, L.u0, L.u1, a.x, a.y, b.x, b.y, L.width, L.index, L.count, L.cls);
            }
            continue;
        }
        if (!strcmp(argv[i], "--colliders") && i + 3 < argc) {
            vec2 p((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            i += 3;
            w.loadCells(p, 60.f);
            std::vector<int> ids;
            w.cw.collidersNear(p, r, ids);
            for (int id : ids) {
                const Phys::Collider& c = w.cw.collider(id);
                printf("collider %d kind %d flags %d surf %d c (%.2f %.2f %.2f) he (%.2f %.2f %.2f) ax (%.2f %.2f) prop %d\n", id, c.kind, c.flags, c.surface, c.c.x, c.c.y, c.c.z,
                       c.he.x, c.he.y, c.he.z, c.ax.x, c.ax.y, c.propIndex);
            }
            for (float dx = -2.f; dx <= 2.f; dx += 1.f) {
                Phys::GroundHit gh = w.cw.ground(p.x + dx, p.y, 10.f, 0.f);
                float rz = 0.f;
                bool on = w.roads.surfaceHeight(vec2(p.x + dx, p.y), &rz);
                printf("ground at (%.1f, %.1f): %.2f surf %d road %d %.2f terrain %.2f\n", p.x + dx, p.y, gh.z, gh.surface, (int)on, rz, w.map.heightAt(p.x + dx, p.y));
                std::vector<int> cand;
                vec2 q(p.x + dx, p.y);
                w.roads.edgesInRect(q - vec2(1.f), q + vec2(1.f), cand);
                for (int ei : cand) {
                    const World::RoadEdge& e = w.roads.edges[ei];
                    for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                        float t;
                        float d = distPointSegment2D(q, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
                        if (d <= e.halfWidth + e.sidewalk) printf("    edge %d cls %d seg %zu d %.2f hw %.2f sw %.2f z %.2f (%.2f..%.2f) n0 %d n1 %d\n", ei, e.cls, k, d, e.halfWidth, e.sidewalk, Lerp(e.pts[k].z, e.pts[k + 1].z, t) + (d > e.halfWidth ? 0.15f : 0.f), e.pts[k].z, e.pts[k+1].z, e.n0, e.n1);
                    }
                }
                for (int ni : {1403, 1404, 1405}) (void)ni;
            }
            continue;
        }
        if (!strcmp(argv[i], "--lanes") && i + 4 < argc) {
            vec2 c((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            Canvas cv;
            cv.init(1400, c, r);
            drawRoads(cv, w);
            drawLanes(cv, w, true);
            cv.save(argv[i + 4]);
            i += 4;
        }
    }
    Jobs::shutdown();
    return 0;
}
#endif  // TS_PART_AI
