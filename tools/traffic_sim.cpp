// Native traffic/pedestrian AI harness: builds the real world (terrain, roads, buildings, collision cells), the AI
// lane graph and sidewalk graph, then runs the traffic driver model on real vehicle physics at 120 Hz with kinematic
// pedestrians, reporting metrics and rendering top-down plots.
// Build (single TU): g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/traffic_sim -lpthread
// Faster iteration (the world/sim half rarely changes):
//   g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_WORLD -c tools/traffic_sim.cpp -o /tmp/ts_world.o
//   g++ -O2 -std=c++17 -I src -DTS_SPLIT -DTS_PART_AI -c tools/traffic_sim.cpp -o /tmp/ts_ai.o
//   g++ /tmp/ts_world.o /tmp/ts_ai.o -o /tmp/traffic_sim -lpthread
// Usage: traffic_sim [--lanes X Y R out.ppm] [--sim X Y R] [--cars N] [--peds N] [--minutes M] [--plot out.ppm]
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
#include "../src/sim/physics.cpp"
#include "../src/sim/vehicle_models.cpp"
#include "../src/sim/vehicle_sim.cpp"
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
void tsLoadCells(Phys::CollisionWorld& cw, vec2 c, float r);
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
            float hw = e.halfWidth;
            // asphalt band
            int steps = Max(1, (int)(hw * 2.f * cv.scale));
            for (int s = 0; s <= steps; s++) {
                float o = -hw + 2.f * hw * s / steps;
                cv.line(a + n * o, b + n * o, vec3(0.2f, 0.2f, 0.22f), 1.f);
            }
            if (e.sidewalk > 0.f)
                for (int sd = -1; sd <= 1; sd += 2) cv.line(a + n * (sd * (hw + e.sidewalk * 0.5f)), b + n * (sd * (hw + e.sidewalk * 0.5f)), vec3(0.3f, 0.3f, 0.3f), 0.6f);
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
            vec3 col = L.kind == AI::WL_CROSSWALK ? vec3(0.2f, 1.f, 1.f) : (L.kind == AI::WL_CORNER ? vec3(0.6f, 1.f, 0.6f) : vec3(0.4f, 0.8f, 0.4f));
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
    double vehSeconds = 0.0;
    double clsSpeedSum[World::RC_COUNT] = {}, clsTime[World::RC_COUNT] = {};
    double aiTime = 0.0, aiMax = 0.0, physTime = 0.0, pedTime = 0.0;
    long aiTicks = 0, physSteps = 0;
    std::vector<double> aiSamples;
    std::map<long long, double> pairLast;
    long redBefore = 0;
    bool verbose = false;
    int watchCar = -1;
    float watchT0 = 0.f, watchT1 = 0.f;

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
            bool ok = true;
            for (int k = 0; k < (int)cars.size() && ok; k++) {
                if (k == i || !cars[k].used) continue;
                vec3 q = cars[k].s.body.pos.toVec3();
                if (length(q.xy() - pos.xy()) < 11.f && fabsf(q.z - pos.z) < 3.f) ok = false;
            }
            if (!ok) continue;
            if (!initial && length(pos.xy() - center) < radius * 0.5f) continue;
            int m = pickModel();
            c = SimCar();
            c.used = true;
            c.model = m;
            c.uid = uidNext++;
            vec2 t = w->lg.laneTangent(lane, u);
            Vehicles::initVehicle(c.s, models[m], m, dvec3(pos.x, pos.y, pos.z + 0.3), AI::dirYaw(t));
            c.info = AI::makeVehicleInfo(models[m], c.s);
            float v0 = Min(L.speed * 0.6f, 9.f);
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
            b.driver = i;
            b.kind = AI::BK_CAR;
            b.flags = AI::BF_AI | (d && d->dummy ? AI::BF_DUMMY : 0);
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
    void aiTick(float dt) {
        double c0 = cpuMs();
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
            if (i == watchCar && time >= watchT0 && time <= watchT1 && ((int)(time * 60.0) % 6) == 0) {
                const AI::LaneGraph& G = w->lg;
                std::string where = G.isLane(d->path) ? StrFormat("lane %d u %.1f/%.1f stopU %.1f", d->path, d->u, G.lanes[d->path].u1, G.lanes[d->path].stopU)
                                                       : StrFormat("conn %d (turn %d node %d) u %.1f/%.1f", d->path - (int)G.lanes.size(), G.conn(d->path).turn, G.conn(d->path).node, d->u, G.conn(d->path).length);
                where += StrFormat(" fp %.0f fu %.2f", d->diag[4], d->diag[5]);
                where += StrFormat(" pos (%.1f %.1f) latErr %.2f diag %.2f %.2f %.2f %.2f steer %.2f vF %.2f yawRate %.2f heading %.3f pathDir %.3f steerOut %.2f imp %.0f", c.s.body.pos.x, c.s.body.pos.y, d->latErr, d->diag[0], d->diag[1], d->diag[2], d->diag[3], c.ctl.steer,
                                   c.s.forwardSpeed(), c.s.body.angVel.z, atan2f(c.s.forward().y, c.s.forward().x), atan2f(G.pathTangent(d->path, d->u).y, G.pathTangent(d->path, d->u).x), c.s.steerOut, c.s.impactImpulse);
                std::string nxt = d->routeLen > 0 && !G.isLane(d->route[0]) ? StrFormat("next conn %d sig %d", d->route[0] - (int)G.lanes.size(), (int)G.movementSignal(G.conn(d->route[0]).node, G.conn(d->route[0]).approach, G.conn(d->route[0]).turn, time)) : std::string("next -");
                LOG("WATCH t=%.2f car %d %s %s v %.1f vT %.1f stop %.1f obst %.1f(%d) gateConn %d committed %d amberGo %d stopDone %d mode %d thr %.2f brk %.2f", time, i, where.c_str(), nxt.c_str(), c.s.speed(), d->vTarget, d->stopDist,
                    d->obstDist, d->obstBody, d->gateConn, (int)d->committed, (int)d->amberGo, (int)d->stopDone, d->mode, c.ctl.throttle, c.ctl.brake);
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
            // large lateral deviation while plainly following a lane (no lane change / nudge / recovery)
            if (verbose && w->lg.isLane(d->path) && d->lcLane < 0 && fabsf(d->nudge) < 0.05f && fabsf(d->lat) < 0.05f && d->recoverTimer <= 0.f && fabsf(d->latErr) > 0.9f &&
                c.s.speed() > 3.f && time - c.latLogT > 10.0) {
                c.latLogT = (float)time;
                LOG("LATDEV t=%.1f car %d (%s) at %.1f %.1f lane %d u %.1f latErr %.2f v %.1f vT %.1f steer %.2f diag e %.2f psi %.2f ff %.2f st %.2f", time, i, models[c.model].name.c_str(), c.s.body.pos.x, c.s.body.pos.y, d->path, d->u,
                    d->latErr, c.s.speed(), d->vTarget, c.ctl.steer, d->diag[0], d->diag[1], d->diag[2], d->diag[3]);
            }
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
            if (!c.used) continue;
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
                if (verbose && d) LOG("STATIC t=%.1f car %d at %.1f %.1f v=%.1f imp %.0f path %d u %.1f lat %.2f", time, i, c.s.body.pos.x, c.s.body.pos.y, v, c.s.impactImpulse, d->path, d->u, d->latErr);
            }
            if (c.s.brokenCount > 0) propHits += c.s.brokenCount;
            c.trailTimer -= h;
            if (c.trailTimer <= 0.f) {
                c.trailTimer = 0.4f;
                trails[c.trailId].pts.push_back(c.s.body.pos.toVec3().xy());
                trails[c.trailId].spd.push_back(v);
            }
            // hung on a ledge / kerb: back onto the lane a few meters on (game: out of view, or after 20 s)
            bool hung = d && !d->dummy && d->vTarget > 1.f && v < 0.5f && (c.s.up().z < 0.94f || c.s.wheelsOnGround < 3) && d->path >= 0;
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
            float dist = length(c.s.body.pos.toVec3().xy() - center);
            bool lost = d && (d->lostTime > 5.f || d->flipTime > 6.f);
            if (dist > radius + 80.f || lost || c.s.body.pos.z < -20.0) {
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
                    float relv = length(c.s.body.vel.xy() - p.vel);
                    if (relv > 1.5f) {
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
        printf("stuck recoveries: %ld, relocalizations: %ld, deadlocks (>60 s waits): %ld, respawns: %ld, lifted off ledges: %ld\n", tc.stats.stuckEvents, tc.stats.relocalizations, deadlocks, respawns, unhung);
        printf("pedestrians hit: %ld\n", pedHits);
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
        std::vector<double> s = aiSamples;
        std::sort(s.begin(), s.end());
        double p50 = s.empty() ? 0 : s[s.size() / 2], p99 = s.empty() ? 0 : s[(size_t)(s.size() * 0.99)];
        printf("AI cost per 60 Hz tick: traffic %.3f ms + peds %.3f ms (avg), p50 %.3f ms, p99 %.3f ms, max %.3f ms\n", aiTime / Max(aiTicks, 1L),
               pedTime / Max(aiTicks, 1L), p50, p99, aiMax);
        printf("physics per 120 Hz step: %.3f ms (%d cars)\n", physTime / Max(physSteps, 1L), (int)cars.size());
    }

    // Single-vehicle turning test: every model through every movement of the node nearest `at` (made uncontrolled),
    // measuring how far the body strays outside its lane (left = toward opposing traffic) on the connector and on the
    // first 15 m of the exit lane, and the front-axle tracking error.
    int traceConn = -1;
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
            if (c.used) cv.point(c.s.body.pos.toVec3().xy(), vec3(1, 1, 1), 2);
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
    w.build();
    for (int i = 1; i < argc; i++) {
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
                        float maxY = -1e9f, far = 0.f;
                        for (auto& q : cc.pts) far = Max(far, length(q.xy() - w.roads.nodes[n].p));
                        for (auto& q : cc.pts) maxY = Max(maxY, q.y);
                        printf("    conn %d (path %d) len %.1f pts %zu maxY %.1f farthest from node %.1f\n", c, (int)w.lg.lanes.size() + c, cc.length, cc.pts.size(), maxY, far);
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
        if (!strcmp(argv[i], "--deadends")) {
            for (int n = 0; n < (int)w.lg.nodes.size(); n++)
                if (w.lg.nodes[n].uturnBlocked) {
                    const World::RoadNode& rn = w.roads.nodes[n];
                    int e = rn.edges.empty() ? -1 : rn.edges[0];
                    printf("obstructed dead end node %d at (%.1f, %.1f) edge %d cls %d\n", n, rn.p.x, rn.p.y, e, e >= 0 ? (int)w.roads.edges[e].cls : -1);
                }
            continue;
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
        if (!strcmp(argv[i], "--sim") && i + 3 < argc) {
            vec2 c((float)atof(argv[i + 1]), (float)atof(argv[i + 2]));
            float r = (float)atof(argv[i + 3]);
            i += 3;
            int ncars = 60, npeds = 40;
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
                else if (!strcmp(argv[k], "--snaps") && k + 1 < argc) snapLimit = atoi(argv[++k]);
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
            LOG("Simulating %.1f minutes around (%.0f, %.0f) r=%.0f with %d cars, %d peds", minutes, c.x, c.y, r, ncars, npeds);
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
