// Native test for the public transit layout (src/world/transit.cpp) and the SkyLine timetable math
// (src/game/transit_schedule.h): generates the world, then checks the metro loop (continuity, level platforms, clearance
// over the roads, piers off the pavement), the bus routes (closed chains of drivable directed edges, stops on the curb
// side of their route legs, spacing, shelters off the pavement), the ferry legs (open water from berth to berth) and the
// train timetables (every station served with its dwell, speed and acceleration limits, doors only while stopped).
// Build: g++ -O2 -std=c++17 -I. tests/transit/test_transit.cpp -o /tmp/test_transit -lpthread
// Run:   /tmp/test_transit          (exit code 1 on failure)
#include "../../tools/native_stubs.cpp"
#include "../../src/core/math.cpp"
#include "../../src/core/noise.cpp"
#include "../../src/core/jobs.cpp"
#include "../../src/render/mesh.cpp"
#include "../../src/world/worldmap.cpp"
#include "../../src/world/sites.cpp"
#include "../../src/world/roads.cpp"
#include "../../src/world/buildings.cpp"
#include "../../src/world/transit.cpp"
#include "../../src/game/transit_schedule.h"
namespace World {
#ifndef WITH_INTERIORS
void planInteriors(WorldMap&, const RoadNetwork&, BuildingSet&) {}
#endif
}  // namespace World
using namespace World;

static int gFail = 0, gChecks = 0;
#define EXPECT(c, ...)                                  \
    do {                                                \
        gChecks++;                                      \
        if (!(c)) {                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            gFail++;                                    \
        }                                               \
    } while (0)

void checkMetro(const WorldMap& map, const RoadNetwork& net) {
    const MetroLine& L = gTransit->metro;
    EXPECT(L.stations.size() >= 6 && L.stations.size() <= 9, "%zu stations", L.stations.size());
    EXPECT(L.length > 8000.f && L.length < 12000.f, "loop length %.0f", L.length);
    // continuity of the centerline, heights and grades
    float maxGrade = 0.f, maxJump = 0.f;
    for (int i = 0; i < L.count(); i++) {
        int j = (i + 1) % L.count();
        maxJump = Max(maxJump, length(L.p[j] - L.p[i]));
        maxGrade = Max(maxGrade, fabsf(L.z[j] - L.z[i]) / L.ds);
    }
    EXPECT(maxJump < L.ds * 1.05f, "centerline gap %.2f m", maxJump);
    EXPECT(maxGrade < 0.04f, "grade %.3f", maxGrade);
    // level platforms, stations in increasing s
    for (size_t k = 0; k < L.stations.size(); k++) {
        const MetroStation& st = L.stations[k];
        for (float d = -transit_dims::kPlatformHalfLen; d <= transit_dims::kPlatformHalfLen; d += 2.f)
            EXPECT(fabsf(L.railZ(st.s + d) - st.railZ) < 0.02f, "%s platform not level at %+.0f (%.2f vs %.2f)", st.name.c_str(), d, L.railZ(st.s + d), st.railZ);
        if (k > 0) EXPECT(st.s > L.stations[k - 1].s, "station order");
        EXPECT(st.railZ - st.streetZ > 8.f, "%s rail only %.1f m above the street", st.name.c_str(), st.railZ - st.streetZ);
    }
    // clearance: the girder bottom stays above every road under the deck by the truck envelope
    int bad = 0;
    for (int i = 0; i < L.count(); i += 2) {
        vec2 rt(L.t[i].y, -L.t[i].x);
        for (int q = -1; q <= 1; q++) {
            float need = transit_layout::roadRequirement(net, map, L.p[i] + rt * (q * transit_dims::kDeckHalf));
            if (L.z[i] + 0.05f < need) bad++;
        }
    }
    EXPECT(bad == 0, "%d deck samples below the road clearance", bad);
    // piers: ~30 m spans, never on the pavement
    int onRoad = 0;
    float maxSpan = 0.f;
    for (size_t k = 0; k < L.piers.size(); k++) {
        const MetroPier& pr = L.piers[k];
        vec2 pos, tan;
        L.frame(pr.s, pos, tan);
        vec2 rt(tan.y, -tan.x);
        if (!pr.bent && net.onPavement(pos, pr.groundZ, 0.3f)) onRoad++;
        if (pr.bent && (net.onPavement(pos + rt * pr.lateral, pr.groundZ, 0.3f) || net.onPavement(pos - rt * pr.lateral, pr.groundZ, 0.3f))) onRoad++;
        float next = k + 1 < L.piers.size() ? L.piers[k + 1].s : L.piers[0].s + L.length;
        maxSpan = Max(maxSpan, next - pr.s);
    }
    EXPECT(onRoad == 0, "%d piers stand on a road", onRoad);
    EXPECT(maxSpan <= 91.f, "longest span %.0f m", maxSpan);
    printf("metro: %.2f km, %zu stations, %zu piers, max grade %.1f %%, longest span %.0f m\n", L.length / 1000.f, L.stations.size(), L.piers.size(), maxGrade * 100.f,
           maxSpan);
}

void checkBuses(const WorldMap& map, const RoadNetwork& net) {
    const TransitNet& N = *gTransit;
    EXPECT(N.busRoutes.size() >= 3 && N.busRoutes.size() <= 4, "%zu bus routes", N.busRoutes.size());
    for (size_t r = 0; r < N.busRoutes.size(); r++) {
        const BusRoute& R = N.busRoutes[r];
        EXPECT(!R.legs.empty() && R.stops.size() >= 8, "route %s: %zu legs %zu stops", R.number.c_str(), R.legs.size(), R.stops.size());
        // closed chain of drivable directed edges
        for (size_t k = 0; k < R.legs.size(); k++) {
            const RouteLeg& a = R.legs[k];
            const RouteLeg& b = R.legs[(k + 1) % R.legs.size()];
            const RoadEdge& ea = net.edges[a.edge];
            const RoadEdge& eb = net.edges[b.edge];
            int endA = a.dir > 0 ? ea.n1 : ea.n0, startB = b.dir > 0 ? eb.n0 : eb.n1;
            EXPECT(endA == startB, "route %s: leg %zu does not connect to %zu", R.number.c_str(), k, (k + 1) % R.legs.size());
            EXPECT(a.dir > 0 ? ea.lanesF > 0 : ea.lanesB > 0, "route %s: leg %zu drives against a one-way", R.number.c_str(), k);
            EXPECT(ea.cls != RC_HIGHWAY && ea.cls != RC_RAMP, "route %s: leg %zu on a highway", R.number.c_str(), k);
        }
        // stops: on a leg of the route, in service order, spaced
        float prev = -1.f;
        for (size_t k = 0; k < R.stops.size(); k++) {
            const BusStop& s = N.busStops[R.stops[k]];
            bool onLeg = false;
            for (const RouteLeg& l : R.legs) onLeg |= l.edge == s.edge && l.dir == s.dir;
            EXPECT(onLeg, "route %s: stop %s is not on the route", R.number.c_str(), s.name.c_str());
            EXPECT(s.routeMask & (1u << r), "route %s: stop %s mask", R.number.c_str(), s.name.c_str());
            float d = R.stopDist[k];
            if (k > 0) {
                float gap = R.wrap(d - prev);
                EXPECT(gap > 150.f && gap < 1700.f, "route %s: stop gap %.0f m before %s", R.number.c_str(), gap, s.name.c_str());
            }
            prev = d;
            // the shelter stands on the curb side, off the pavement
            const RoadEdge& e = net.edges[s.edge];
            vec3 c = transit_bus::edgePos(e, s.dir, s.u);
            vec2 al = transit_bus::edgeDir(e, s.dir, s.u);
            float side = dot(s.pos - c.xy(), vec2(al.y, -al.x));
            EXPECT(side > e.halfWidth, "route %s: stop %s shelter on the wrong side (%.1f)", R.number.c_str(), s.name.c_str(), side);
            EXPECT(!net.onPavement(s.pos, s.z, 0.5f), "route %s: stop %s shelter on the pavement", R.number.c_str(), s.name.c_str());
            EXPECT(!map.isWater(s.pos.x, s.pos.y), "route %s: stop %s in the water", R.number.c_str(), s.name.c_str());
        }
        // line samples follow the legs
        EXPECT(R.line.size() > 10 && fabsf(R.lineDist.back() - R.length) < 20.f, "route %s line", R.number.c_str());
        vec2 d0;
        vec2 p0 = R.pointAt(0.f, &d0), p1 = R.pointAt(R.length - 0.01f);
        EXPECT(length(p0 - p1) < 20.f, "route %s is not closed (%.1f m)", R.number.c_str(), length(p0 - p1));
        printf("bus %-3s %-12s %.2f km, %zu legs, %zu stops, %d buses, headway %.1f min\n", R.number.c_str(), R.name.c_str(), R.length / 1000.f, R.legs.size(),
               R.stops.size(), R.buses, R.headway / 60.f);
    }
    int own = 0;
    for (const BusStop& s : N.busStops) own += s.ownShelter;
    printf("bus stops: %zu (%d new shelters)\n", N.busStops.size(), own);
}

void checkFerries(const WorldMap& map) {
    const TransitNet& N = *gTransit;
    EXPECT(N.piers.size() == 3, "%zu ferry piers", N.piers.size());
    EXPECT(N.ferries.size() == 1, "%zu ferry routes", N.ferries.size());
    for (const FerryPier& fp : N.piers) {
        EXPECT(!map.isWater(fp.base.x, fp.base.y) || map.waterAt(fp.base.x, fp.base.y) - map.heightAt(fp.base.x, fp.base.y) < 0.6f, "%s pier root in deep water",
               fp.name.c_str());
        EXPECT(transit_ferry::depthAt(map, fp.berth) >= 2.1f, "%s berth depth %.2f", fp.name.c_str(), transit_ferry::depthAt(map, fp.berth));
        EXPECT(fp.length >= 20.f && fp.length <= 140.f, "%s pier length %.0f", fp.name.c_str(), fp.length);
    }
    if (N.ferries.empty()) return;
    const FerryRoute& F = N.ferries[0];
    for (size_t i = 0; i < F.legs.size(); i++) {
        const std::vector<vec2>& leg = F.legs[i];
        const FerryPier& A = N.piers[F.piers[i]];
        const FerryPier& B = N.piers[F.piers[(i + 1) % F.piers.size()]];
        EXPECT(leg.size() > 20, "leg %zu has %zu points", i, leg.size());
        EXPECT(length(leg.front() - A.berth) < 1.f && length(leg.back() - B.berth) < 1.f, "leg %zu does not run berth to berth", i);
        int dry = 0, turnAt = -1;
        float len = 0.f, maxTurn = 0.f;
        for (size_t k = 0; k < leg.size(); k++) {
            if (transit_ferry::depthAt(map, leg[k]) < 1.8f) dry++;
            if (k > 0) len += length(leg[k] - leg[k - 1]);
            if (k > 0 && k + 1 < leg.size()) {
                vec2 a = normalize(leg[k] - leg[k - 1]), b = normalize(leg[k + 1] - leg[k]);
                float tr = acosf(Clamp(dot(a, b), -1.f, 1.f)) * kRadToDeg;
                if (tr > maxTurn) {
                    maxTurn = tr;
                    turnAt = (int)k;
                }
            }
        }
        EXPECT(dry == 0, "leg %zu %s -> %s: %d points in shallow water", i, A.name.c_str(), B.name.c_str(), dry);
        EXPECT(maxTurn < 25.f, "leg %zu: kink of %.0f deg", i, maxTurn);
        printf("ferry leg %s -> %s: %.2f km, max turn %.1f deg per 5 m at point %d/%zu (%.0f, %.0f)\n", A.name.c_str(), B.name.c_str(), len / 1000.f, maxTurn, turnAt,
               leg.size(), turnAt >= 0 ? leg[turnAt].x : 0.f, turnAt >= 0 ? leg[turnAt].y : 0.f);
    }
}

void checkTimetable() {
    using namespace Game::Transit::tsched;
    const MetroLine& L = gTransit->metro;
    for (int dir = 0; dir < 2; dir++) {
        Profile P;
        buildProfile(P, dir);
        EXPECT(P.cycle > 300.f && P.cycle < 1800.f, "dir %d cycle %.0f s", dir, P.cycle);
        EXPECT(P.stQ.size() == L.stations.size(), "dir %d serves %zu stations", dir, P.stQ.size());
        // walk the cycle: q advances monotonically, stops at every station for its dwell, limits respected
        float prevQ = -1.f, prevV = 0.f, maxV = 0.f, maxA = 0.f;
        int stops = 0, lastStop = -1, doorWhileMoving = 0, backwards = 0;
        const float dt = 0.1f;
        for (float t = 0.f; t < P.cycle; t += dt) {
            TrainState s = stateAt(P, t);
            if (prevQ >= 0.f) {
                float dq = s.q - prevQ;
                if (dq < -P.len * 0.5f) dq += P.len;
                if (dq < -0.01f) backwards++;
                maxA = Max(maxA, fabsf(s.v - prevV) / dt);
            }
            maxV = Max(maxV, s.v);
            if (s.stop >= 0 && s.stop != lastStop) {
                stops++;
                lastStop = s.stop;
                const MetroStation& st = L.stations[P.stIdx[s.stop]];
                EXPECT(fabsf(s.dwell - st.dwell) < 0.01f, "dir %d %s dwell %.1f", dir, st.name.c_str(), s.dwell);
            }
            if (s.stop < 0 && s.doors > 0.f) doorWhileMoving++;
            prevQ = s.q;
            prevV = s.v;
        }
        EXPECT(stops == (int)L.stations.size(), "dir %d stopped %d times", dir, stops);
        EXPECT(backwards == 0, "dir %d moved backwards %d times", dir, backwards);
        EXPECT(maxV <= kLineSpeed + 0.01f, "dir %d top speed %.1f", dir, maxV);
        EXPECT(maxA < 1.6f, "dir %d acceleration %.2f m/s2", dir, maxA);
        EXPECT(doorWhileMoving == 0, "dir %d doors open while moving", dir);
        printf("timetable dir %d: loop %.1f min, top speed %.1f m/s, peak accel %.2f m/s2\n", dir, P.cycle / 60.f, maxV, maxA);
    }
}

int main() {
    Jobs::init(2);
    WorldMap m;
    m.generate();
    gMap = &m;
    RoadNetwork roads;
    roads.generate(m);
    gRoads = &roads;
    BuildingSet bset;
    bset.generate(m, roads);
    gBuildings = &bset;
    checkMetro(m, roads);
    checkBuses(m, roads);
    checkFerries(m);
    checkTimetable();
    printf("%d checks, %d failures\n", gChecks, gFail);
    Jobs::shutdown();
    return gFail ? 1 : 0;
}
