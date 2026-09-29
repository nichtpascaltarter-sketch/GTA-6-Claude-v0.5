// Mission AI: scripted drivers that follow road routes (lanes, curve speeds, obstacle avoidance, rubber banding),
// water routes (boats) and kinematic flight/cruise paths; buddies that ride along and fight; stealth guards with
// vision cones and a suspicion meter; tail/eavesdrop distance checks.
#include "missions.h"
#include <queue>

namespace Game {
namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Road routing over edges (A*), producing a lane-offset polyline with per-point speed limits.
struct RoutePath {
    std::vector<vec3> pts;
    std::vector<float> limit;   // road speed limit at the point (m/s)
    std::vector<float> cum;     // cumulative length
    float length() const { return cum.empty() ? 0.f : cum.back(); }
    void finish() {
        cum.assign(pts.size(), 0.f);
        for (size_t i = 1; i < pts.size(); i++) cum[i] = cum[i - 1] + ::length(pts[i].xy() - pts[i - 1].xy());
    }
    vec3 at(float s, float* limOut = nullptr, vec2* tanOut = nullptr) const {
        if (pts.empty()) return vec3(0);
        if (pts.size() == 1 || s <= 0.f) {
            if (limOut) *limOut = limit.empty() ? 30.f : limit[0];
            if (tanOut) *tanOut = pts.size() > 1 ? normalize(pts[1].xy() - pts[0].xy()) : vec2(0, 1);
            return pts[0];
        }
        if (s >= cum.back()) {
            if (limOut) *limOut = limit.back();
            if (tanOut) *tanOut = normalize(pts.back().xy() - pts[pts.size() - 2].xy());
            return pts.back();
        }
        size_t i = std::upper_bound(cum.begin(), cum.end(), s) - cum.begin();
        if (i == 0) i = 1;
        float seg = cum[i] - cum[i - 1];
        float t = seg > 1e-4f ? (s - cum[i - 1]) / seg : 0.f;
        if (limOut) *limOut = Lerp(limit[i - 1], limit[i], t);
        if (tanOut) *tanOut = normalize(pts[i].xy() - pts[i - 1].xy());
        return lerp(pts[i - 1], pts[i], t);
    }
    // Projection of p onto the path, searching around a hint distance.
    float project(vec2 p, float hint, float window = 1e9f) const {
        float best = 1e30f, bestS = 0.f;
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            if (window < 1e8f && (cum[i + 1] < hint - window || cum[i] > hint + window)) continue;
            float t;
            float d = distPointSegment2D(p, pts[i].xy(), pts[i + 1].xy(), &t);
            if (d < best) {
                best = d;
                bestS = cum[i] + t * (cum[i + 1] - cum[i]);
            }
        }
        return bestS;
    }
};

float laneOffsetFor(const World::RoadEdge& e) {
    const World::RoadClassInfo& info = World::roadInfo(e.cls);
    if (e.flags & World::RF_ONEWAY) return 0.f;
    return info.median * 0.5f + info.laneWidth * 0.5f;
}

// A* over the road graph (driving rules: one-way edges only forward). Returns the edge chain with directions.
bool routeEdges(GameWorld& g, vec2 from, vec2 to, std::vector<int>& edges, std::vector<int>& dirs, float* sA = nullptr, float* sB = nullptr,
                bool allowDirt = true) {
    edges.clear();
    dirs.clear();
    float a0 = 0, b0 = 0;
    int eA = g.roads->nearestEdge(from, 400.f, &a0);
    int eB = g.roads->nearestEdge(to, 800.f, &b0);
    if (sA) *sA = a0;
    if (sB) *sB = b0;
    if (eA < 0 || eB < 0) return false;
    const World::RoadNetwork& R = *g.roads;
    if (eA == eB) {
        edges.push_back(eA);
        dirs.push_back(b0 >= a0 ? 1 : -1);
        return true;
    }
    int N = (int)R.nodes.size();
    std::vector<float> cost(N, 1e30f);
    std::vector<int> prevEdge(N, -1), prevNode(N, -2);
    std::vector<u8> closed(N, 0);
    typedef std::pair<float, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    vec2 goal = R.edges[eB].posAt(b0).xy();
    auto h = [&](int n) { return ::length(R.nodes[n].p - goal) * 0.62f; };
    const World::RoadEdge& A = R.edges[eA];
    int ends[2] = {A.n0, A.n1};
    float c0[2] = {a0, A.length - a0};
    for (int i = 0; i < 2; i++) {
        if ((A.flags & World::RF_ONEWAY) && i == 0) continue;
        int n = ends[i];
        if (c0[i] < cost[n]) {
            cost[n] = c0[i];
            prevEdge[n] = eA;
            prevNode[n] = -1;
            open.push(QE(cost[n] + h(n), n));
        }
    }
    const World::RoadEdge& B = R.edges[eB];
    int goalNode = -1;
    float best = 1e30f;
    int expanded = 0;
    while (!open.empty() && expanded < 80000) {
        QE top = open.top();
        open.pop();
        int n = top.second;
        if (closed[n]) continue;
        closed[n] = 1;
        expanded++;
        if (top.first >= best) break;
        if (n == B.n0 || n == B.n1) {
            float rest = n == B.n0 ? b0 : B.length - b0;
            bool ok = !(B.flags & World::RF_ONEWAY) || n == B.n0;
            if (ok && cost[n] + rest < best) {
                best = cost[n] + rest;
                goalNode = n;
            }
        }
        for (int ei : R.nodes[n].edges) {
            const World::RoadEdge& e = R.edges[ei];
            if ((e.flags & World::RF_ONEWAY) && e.n0 != n) continue;
            if (!allowDirt && e.cls == World::RC_DIRT) continue;
            int m = e.n0 == n ? e.n1 : e.n0;
            float w = e.cls == World::RC_HIGHWAY ? 0.55f : (e.cls == World::RC_DIRT ? 2.2f : (e.cls == World::RC_LANE ? 1.3f : 1.f));
            float c = cost[n] + e.length * w;
            if (c < cost[m]) {
                cost[m] = c;
                prevEdge[m] = ei;
                prevNode[m] = n;
                open.push(QE(c + h(m), m));
            }
        }
    }
    if (goalNode < 0) return false;
    std::vector<int> chainE, chainN;
    for (int n = goalNode; n >= 0 && prevNode[n] >= 0; n = prevNode[n]) {
        chainE.push_back(prevEdge[n]);
        chainN.push_back(n);
    }
    std::reverse(chainE.begin(), chainE.end());
    std::reverse(chainN.begin(), chainN.end());
    // start edge: direction toward the first node of the chain (or the goal node)
    int firstNode = chainN.empty() ? goalNode : prevNode[chainN.front()];
    if (firstNode < 0) firstNode = goalNode;
    edges.push_back(eA);
    dirs.push_back(A.n1 == firstNode ? 1 : -1);
    for (size_t i = 0; i < chainE.size(); i++) {
        const World::RoadEdge& e = R.edges[chainE[i]];
        edges.push_back(chainE[i]);
        dirs.push_back(e.n1 == chainN[i] ? 1 : -1);
    }
    edges.push_back(eB);
    dirs.push_back(B.n0 == goalNode ? 1 : -1);
    return true;
}

// Builds a lane-offset polyline from `from` to `to` along the road network. Returns false (straight line) if no route.
bool buildRoadPath(GameWorld& g, vec2 from, vec2 to, RoutePath& out, float laneScale = 1.f, bool allowDirt = true) {
    out = RoutePath();
    std::vector<int> edges, dirs;
    float sA = 0, sB = 0;
    if (!routeEdges(g, from, to, edges, dirs, &sA, &sB, allowDirt)) {
        out.pts = {vec3(from, groundAt(g, from.x, from.y)), vec3(to, groundAt(g, to.x, to.y))};
        out.limit = {20.f, 20.f};
        out.finish();
        return false;
    }
    const World::RoadNetwork& R = *g.roads;
    for (size_t k = 0; k < edges.size(); k++) {
        const World::RoadEdge& e = R.edges[edges[k]];
        int dir = dirs[k];
        float s0 = 0.f, s1 = e.length;
        if (k == 0) {
            if (dir > 0) s0 = sA;
            else s1 = sA;
        }
        if (k + 1 == edges.size()) {
            if (dir > 0) s1 = edges.size() == 1 ? Max(sB, s0) : sB;
            else s0 = edges.size() == 1 ? Min(sB, s1) : sB;
        }
        if (edges.size() == 1) {
            s0 = Min(sA, sB);
            s1 = Max(sA, sB);
        }
        float off = laneOffsetFor(e) * laneScale;
        float lim = World::roadInfo(e.cls).speed;
        // sample along the edge every ~6 m (and at its polyline vertices)
        std::vector<float> ss;
        for (size_t i = 0; i < e.pts.size(); i++)
            if (e.dist[i] > s0 && e.dist[i] < s1) ss.push_back(e.dist[i]);
        for (float s = s0; s < s1; s += 6.f) ss.push_back(s);
        ss.push_back(s0);
        ss.push_back(s1);
        std::sort(ss.begin(), ss.end());
        if (dir < 0) std::reverse(ss.begin(), ss.end());
        for (float s : ss) {
            vec3 c = e.posAt(s);
            vec3 t = e.tangentAt(s) * (float)dir;
            vec2 right(t.y, -t.x);
            vec2 p = c.xy() + normalize(right) * off;
            if (!out.pts.empty() && length2(out.pts.back().xy() - p) < 1.0f) continue;
            out.pts.push_back(vec3(p, c.z));
            out.limit.push_back(lim);
        }
    }
    out.pts.push_back(vec3(to, groundAt(g, to.x, to.y)));
    out.limit.push_back(out.limit.empty() ? 12.f : out.limit.back());
    out.finish();
    return true;
}

// Path through explicit waypoints (water / air / off-road), optionally smoothed with Catmull-Rom.
void buildWaypointPath(const std::vector<vec3>& wps, RoutePath& out, float limit, bool smooth = true, bool closed = false) {
    out = RoutePath();
    int n = (int)wps.size();
    if (n == 0) return;
    if (!smooth || n < 3) {
        out.pts = wps;
        if (closed) out.pts.push_back(wps[0]);
    } else {
        int segs = closed ? n : n - 1;
        for (int i = 0; i < segs; i++) {
            vec3 p0 = wps[closed ? (i - 1 + n) % n : Max(i - 1, 0)];
            vec3 p1 = wps[i];
            vec3 p2 = wps[(i + 1) % n];
            vec3 p3 = wps[closed ? (i + 2) % n : Min(i + 2, n - 1)];
            float len = ::length(p2 - p1);
            int steps = Max(2, (int)(len / 10.f));
            for (int k = 0; k < steps; k++) {
                float t = (float)k / steps, t2 = t * t, t3 = t2 * t;
                vec3 p = (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
                out.pts.push_back(p);
            }
        }
        out.pts.push_back(closed ? wps[0] : wps.back());
    }
    out.limit.assign(out.pts.size(), limit);
    out.finish();
}

// ------------------------------------------------------------------------------------------------------------------
// Scripted drivers
enum DriverMode : int { DRV_ROAD = 0, DRV_WATER, DRV_KINEMATIC, DRV_HOLD };

struct ScriptDriver {
    int veh = -1;
    int mode = DRV_ROAD;
    RoutePath path;
    float along = 0.f;
    float cruise = 20.f;
    bool loop = false;
    bool aggressive = true;     // swerves around traffic instead of queueing
    bool obeyLimits = false;    // cap at road speed limits (civil driving)
    bool done = false;
    float rubberGap = 0.f;      // flee targets: keep roughly this far ahead of rubberPed along the path (0 off)
    bool racer = false;         // races: mild rubber band around the player
    int rubberPed = -1;
    float stuck = 0.f, reverse = 0.f, unstick = 0.f;
    int scope = 0;              // 0 mission, 1 activity
    float kinSpeed = 0.f;       // kinematic speed (m/s)
    float bank = 0.f;
    float avoid = 0.f;          // current lateral avoidance offset
    float speedScale = 1.f;     // script-controlled multiplier
    bool stopAtEnd = true;
    float playerAlong = 0.f;
    bool valid = true;
};

std::vector<ScriptDriver> gDrivers;

ScriptDriver* driverFor(int veh) {
    for (auto& d : gDrivers)
        if (d.valid && d.veh == veh) return &d;
    return nullptr;
}

void releaseDriver(GameWorld& g, int veh) {
    for (auto& d : gDrivers)
        if (d.valid && d.veh == veh) {
            d.valid = false;
            if (veh >= 0 && g.vehicles[veh].used) {
                g.vehicles[veh].ctl = Vehicles::VehicleControls();
                g.vehicles[veh].ctl.brake = 1.f;
                if (d.mode == DRV_KINEMATIC) {
                    g.vehicles[veh].scripted = false;
                    g.vehicles[veh].sim.body.vel = vec3(0);
                }
            }
        }
}

void clearDrivers(GameWorld& g, int scope) {
    for (auto& d : gDrivers)
        if (d.valid && d.scope == scope) {
            d.valid = false;
            if (d.veh >= 0 && d.veh < (int)g.vehicles.size() && g.vehicles[d.veh].used) {
                g.vehicles[d.veh].ctl = Vehicles::VehicleControls();
                g.vehicles[d.veh].ctl.brake = 1.f;
                if (d.mode == DRV_KINEMATIC) g.vehicles[d.veh].scripted = false;
                int drv = g.driverOf(d.veh);
                // hand the car back to the ambient traffic brain
                if (drv >= 0 && !g.peds[drv].isPlayer && g.peds[drv].brain.type == BRAIN_NONE && g.peds[drv].health > 0.f) g.peds[drv].brain.type = BRAIN_DRIVER;
            }
        }
    gDrivers.erase(std::remove_if(gDrivers.begin(), gDrivers.end(), [](const ScriptDriver& d) { return !d.valid; }), gDrivers.end());
}

ScriptDriver& addDriver(GameWorld& g, int veh, const RoutePath& path, float cruise, int mode = DRV_ROAD, int scope = 0) {
    releaseDriver(g, veh);
    ScriptDriver d;
    d.veh = veh;
    d.mode = mode;
    d.path = path;
    d.cruise = cruise;
    d.scope = scope;
    d.kinSpeed = cruise;
    if (veh >= 0 && g.vehicles[veh].used) {
        d.along = d.path.project(g.vehicles[veh].sim.body.pos.toVec3().xy(), 0.f);
        // the script owns the controls: no AI brain may drive this car (pursuit/flee brains would overwrite them)
        int drv = g.driverOf(veh);
        if (drv >= 0 && !g.peds[drv].isPlayer) g.peds[drv].brain.type = BRAIN_NONE;
        g.vehicles[veh].parked = false;
        g.vehicles[veh].sim.engineOn = true;
        g.vehicles[veh].sim.sleeping = false;
    }
    gDrivers.push_back(d);
    return gDrivers.back();
}

ScriptDriver& driveRoad(GameWorld& g, int veh, vec2 to, float cruise, bool aggressive = true, int scope = 0) {
    RoutePath p;
    buildRoadPath(g, vehPos(g, veh).xy(), to, p);
    ScriptDriver& d = addDriver(g, veh, p, cruise, DRV_ROAD, scope);
    d.aggressive = aggressive;
    d.obeyLimits = !aggressive;
    return d;
}

float turnAngle(vec2 a, vec2 b) { return fabsf(atan2f(cross(a, b), dot(a, b))); }

// Speed that still allows taking the curves ahead (lateral grip + braking distance).
float curveSpeedLimit(const RoutePath& p, float along, float lat, float brake, float maxLook) {
    float lim = 1e9f;
    for (float d = 6.f; d <= maxLook; d += (d < 30.f ? 6.f : 12.f)) {
        vec2 t0, t1;
        p.at(along + d - 6.f, nullptr, &t0);
        p.at(along + d + 6.f, nullptr, &t1);
        float ang = turnAngle(t0, t1);
        if (ang < 0.05f) continue;
        float r = 12.f / ang;
        float vc = sqrtf(lat * r);
        float allowed = sqrtf(vc * vc + 2.f * brake * Max(d - 4.f, 0.f));
        lim = Min(lim, allowed);
    }
    return lim;
}

void updateDriver(GameWorld& g, ScriptDriver& d, float dt) {
    if (d.veh < 0 || d.veh >= (int)g.vehicles.size() || !g.vehicles[d.veh].used) {
        d.valid = false;
        return;
    }
    Vehicle& v = g.vehicles[d.veh];
    Vehicles::VehicleState& s = v.sim;
    int drv = v.seats[0];
    bool hasDriver = drv >= 0 && g.peds[drv].health > 0.f && !g.peds[drv].isPlayer;
    if (d.mode != DRV_KINEMATIC && (!hasDriver || s.wrecked || v.exploded)) {
        if (!hasDriver && drv >= 0 && g.peds[drv].isPlayer) d.valid = false;   // the player took the wheel
        return;
    }
    if (hasDriver && g.peds[drv].brain.type != BRAIN_NONE) g.peds[drv].brain.type = BRAIN_NONE;   // threat reactions must not steal the wheel
    if (d.path.pts.size() < 2) {
        v.ctl = Vehicles::VehicleControls();
        v.ctl.brake = 1.f;
        return;
    }
    vec3 pos = s.body.pos.toVec3();
    float total = d.path.length();
    // ---- kinematic flight/cruise along the path (helicopters, planes, scripted boats)
    if (d.mode == DRV_KINEMATIC) {
        float target = d.cruise * d.speedScale;
        d.kinSpeed = approach(d.kinSpeed, target, dt * 6.f);
        d.along += d.kinSpeed * dt;
        if (d.along >= total) {
            if (d.loop) d.along -= total;
            else {
                d.along = total;
                d.done = true;
                d.kinSpeed = 0.f;
            }
        }
        vec2 tan;
        vec3 p = d.path.at(d.along, nullptr, &tan);
        vec2 tan2;
        d.path.at(d.along + 20.f, nullptr, &tan2);
        float turn = atan2f(cross(tan, tan2), dot(tan, tan2));
        d.bank = Lerp(d.bank, Clamp(-turn * 1.2f, -0.6f, 0.6f), Saturate(dt * 2.f));
        float yaw = atan2f(-tan.x, tan.y);
        quat q = quatAxisAngle(vec3(0, 0, 1), yaw) * quatAxisAngle(vec3(0, 1, 0), d.bank) * quatAxisAngle(vec3(1, 0, 0), -0.08f * Saturate(d.kinSpeed / 30.f));
        vec3 prev = s.body.pos.toVec3();
        s.body.pos = dvec3(p);
        s.body.rot = q;
        s.body.vel = dt > 0.f ? (p - prev) / dt : vec3(0);
        if (length(s.body.vel) > 120.f) s.body.vel = vec3(tan * d.kinSpeed, 0.f);
        s.body.angVel = vec3(0);
        s.sleeping = false;
        s.rotorSpeed = 1.f;
        s.engineOn = true;
        s.rotorAngle = wrapAngle(s.rotorAngle + dt * 38.f);
        s.tailRotorAngle = wrapAngle(s.tailRotorAngle + dt * 70.f);
        v.scripted = true;   // the vehicle simulation leaves kinematic vehicles alone
        v.ctl = Vehicles::VehicleControls();
        return;
    }
    // ---- progress along the path
    float window = 60.f + s.speed() * 2.f;
    d.along = d.path.project(pos.xy(), d.along, window);
    if (d.along >= total - (d.mode == DRV_WATER ? 12.f : 6.f)) {
        if (d.loop) d.along = 0.f;
        else d.done = true;
    }
    Vehicles::VehicleControls c;
    vec3 fwd = s.forward();
    float spd = s.speed();
    float fs = s.forwardSpeed();
    if (d.done && d.stopAtEnd) {
        c.brake = 1.f;
        c.handbrake = spd < 3.f;
        v.ctl = c;
        return;
    }
    if (d.mode == DRV_HOLD) {
        c.brake = 1.f;
        v.ctl = c;
        return;
    }
    bool water = d.mode == DRV_WATER;
    float look = water ? Clamp(10.f + spd * 0.8f, 12.f, 45.f) : Clamp(4.5f + spd * 0.42f, 6.f, 26.f);
    float lim = 30.f;
    vec2 tan;
    vec3 target = d.path.at(d.along + look, &lim, &tan);
    // ---- obstacles: vehicles ahead in the corridor
    float obstacle = 1e9f;
    float sideBias = 0.f;
    if (!water) {
        vec3 right = s.right();
        for (int oi = 0; oi < (int)g.vehicles.size(); oi++) {
            if (oi == d.veh || !g.vehicles[oi].used) continue;
            vec3 dd = rel(g.vehicles[oi].sim.body.pos, s.body.pos);
            float al = dot(dd, fwd);
            if (al < 0.f || al > 28.f) continue;
            float lat = dot(dd, right);
            if (fabsf(lat) > 2.6f) continue;
            float gap = al - g.vassets[g.vehicles[oi].model].spec.boxHalf.y - g.vassets[v.model].spec.boxHalf.y;
            // slower vehicles ahead only
            float closing = fs - dot(g.vehicles[oi].sim.body.vel, fwd);
            if (closing < 0.5f && gap > 4.f) continue;
            if (gap < obstacle) {
                obstacle = gap;
                sideBias = lat >= 0.f ? -1.f : 1.f;
            }
        }
        for (int pi = 0; pi < (int)g.peds.size(); pi++) {
            const Ped& o = g.peds[pi];
            if (!o.used || o.state == PS_INVEHICLE || o.health <= 0.f) continue;
            vec3 dd = o.pos.toVec3() - pos;
            float al = dot(dd, fwd);
            if (al < 0.f || al > 16.f || fabsf(dot(dd, right)) > 1.8f) continue;
            if (!d.aggressive || o.isPlayer) obstacle = Min(obstacle, al - g.vassets[v.model].spec.boxHalf.y - 1.2f);
        }
    }
    float wantAvoid = 0.f;
    if (d.aggressive && obstacle < 18.f) wantAvoid = sideBias * 3.f;
    d.avoid = approach(d.avoid, wantAvoid, dt * 4.f);
    if (fabsf(d.avoid) > 0.05f) {
        vec2 r(tan.y, -tan.x);
        target = target + vec3(r * d.avoid, 0.f);
    }
    // ---- steering
    vec2 to = target.xy() - pos.xy();
    float headErr = wrapAngle(atan2f(-to.x, to.y) - atan2f(-fwd.x, fwd.y));
    float yawRate = s.body.angVel.z;
    c.steer = Clamp(-headErr * (water ? 1.8f : 2.5f) + yawRate * 0.12f, -1.f, 1.f);
    // ---- speed target
    float target_speed = d.cruise * d.speedScale;
    if (d.obeyLimits) target_speed = Min(target_speed, lim * 1.1f);
    if (!water) target_speed = Min(target_speed, curveSpeedLimit(d.path, d.along, d.aggressive ? 8.5f : 5.5f, 7.f, 80.f));
    else target_speed = Min(target_speed, curveSpeedLimit(d.path, d.along, 5.f, 3.f, 90.f));
    target_speed *= 1.f - Saturate(fabsf(headErr) - 0.35f) * 0.6f;
    if (d.rubberPed >= 0 && pedAlive(g, d.rubberPed)) {
        vec3 rp = pedPos(g, d.rubberPed);
        int rv = g.peds[d.rubberPed].vehicle;
        if (rv >= 0) rp = vehPos(g, rv);
        d.playerAlong = d.path.project(rp.xy(), d.playerAlong, 400.f);
        float gap = d.along - d.playerAlong;
        if (d.racer) {
            if (gap > 60.f) target_speed *= Lerp(1.f, 0.82f, Saturate((gap - 60.f) / 120.f));
            else if (gap < -40.f) target_speed *= Lerp(1.f, 1.22f, Saturate((-gap - 40.f) / 120.f));
        } else if (d.rubberGap > 0.f) {
            if (gap > d.rubberGap * 1.5f) target_speed *= Lerp(1.f, 0.7f, Saturate((gap - d.rubberGap * 1.5f) / (d.rubberGap * 2.f)));
            else if (gap < d.rubberGap * 0.6f) target_speed *= 1.18f;
        }
    }
    if (obstacle < 1e8f && (!d.aggressive || obstacle < 4.f)) target_speed = Min(target_speed, Max(0.f, (obstacle - 2.f) * 0.8f));
    float err = target_speed - fs;
    if (err > 0.f) c.throttle = Saturate(err * 0.35f + 0.25f);
    else c.brake = Saturate(-err * 0.3f);
    if (water && c.brake > 0.f) {
        c.throttle = 0.f;
        c.brake = Min(c.brake, 0.6f);
    }
    // ---- stuck recovery: reverse out, then nudge along the path when unseen
    if (c.throttle > 0.35f && spd < 0.8f) d.stuck += dt;
    else d.stuck = Max(0.f, d.stuck - dt * 0.5f);
    if (d.stuck > 2.2f && d.reverse <= 0.f && !water) {
        d.reverse = 1.4f;
        d.unstick += 1.f;
    }
    if (d.reverse > 0.f) {
        d.reverse -= dt;
        c.throttle = 0.f;
        c.brake = 1.f;   // held while stopped -> reverse
        c.steer = -c.steer;
        if (d.reverse <= 0.f) d.stuck = 0.f;
    }
    if ((d.unstick >= 3.f || (water && d.stuck > 4.f)) && v.visibleDist > 70.f) {
        vec2 t2;
        vec3 np = d.path.at(d.along + 15.f, nullptr, &t2);
        float z = water ? np.z : groundAt(g, np.x, np.y, np.z + 3.f);
        teleportVehicle(g, d.veh, vec3(np.x, np.y, z), atan2f(-t2.x, t2.y));
        g.vehicles[d.veh].sim.body.vel = vec3(t2 * Min(d.cruise, 12.f), 0.f);
        d.unstick = 0.f;
        d.stuck = 0.f;
    }
    v.ctl = c;
    v.parked = false;
}

void updateDrivers(GameWorld& g, float dt) {
    for (auto& d : gDrivers)
        if (d.valid) updateDriver(g, d, dt);
    gDrivers.erase(std::remove_if(gDrivers.begin(), gDrivers.end(), [](const ScriptDriver& d) { return !d.valid; }), gDrivers.end());
}

// ------------------------------------------------------------------------------------------------------------------
// Buddies: follow the player in and out of vehicles, fight enemies in range.
void buddyUpdate(GameWorld& g, int b, const std::vector<int>* enemies, float engage = 35.f) {
    if (!pedAlive(g, b)) return;
    Ped& bp = g.peds[b];
    Ped* pl = g.playerPed();
    if (!pl) return;
    int pv = pl->vehicle;
    if (pv < 0 && bp.vehicle >= 0 && !g.vehicles[bp.vehicle].used) bp.vehicle = -1;
    if (pv < 0 && bp.state == PS_INVEHICLE && bp.vehicle >= 0 && g.driverOf(bp.vehicle) != b) {
        // the player got out: follow suit
        g.removePedFromVehicle(b, true);
        setFollow(g, b, g.player);
        return;
    }
    if (bp.state == PS_INVEHICLE) {
        if (enemies && bp.weapon != WPN_FISTS) {
            float dist;
            int e = nearestAlive(g, *enemies, bp.pos.toVec3(), &dist);
            if (e >= 0 && dist < 40.f) {
                bp.brain.type = BRAIN_COMBAT;
                bp.brain.target = e;
            } else if (bp.brain.type == BRAIN_COMBAT) {
                bp.brain.type = BRAIN_FOLLOW;
                bp.brain.target = g.player;
            }
        }
        return;
    }
    if (pv >= 0) {
        if (bp.brain.type != BRAIN_FOLLOW || bp.brain.target != g.player) setFollow(g, b, g.player);
        return;
    }
    if (enemies) {
        float dist;
        int e = nearestAlive(g, *enemies, bp.pos.toVec3(), &dist);
        float pd = ::length(bp.pos.toVec3() - pl->pos.toVec3());
        if (e >= 0 && dist < engage && pd < 45.f) {
            if (bp.brain.type != BRAIN_COMBAT || bp.brain.target != e) setCombat(g, b, e);
            return;
        }
    }
    if (bp.brain.type != BRAIN_FOLLOW) setFollow(g, b, g.player);
}

// ------------------------------------------------------------------------------------------------------------------
// Stealth guards: patrol routes, vision cones and a shared suspicion meter.
struct Guard {
    int ped = -1;
    std::vector<vec3> patrol;
    int wp = 0;
    float wait = 0.f;
    float suspicion = 0.f;
    float range = 17.f;
    float fovCos = 0.45f;
    bool stationary = false;
    float homeYaw = 0.f;
};

struct StealthGroup {
    std::vector<Guard> guards;
    bool alarm = false;
    float meter = 0.f;      // max suspicion (0..1) for the HUD
    int spotter = -1;
    WeaponType alarmWeapon = WPN_PISTOL;

    void add(GameWorld& g, int ped, std::vector<vec3> patrol, float range = 17.f) {
        Guard gd;
        gd.ped = ped;
        gd.patrol = patrol;
        gd.range = range;
        gd.stationary = patrol.size() < 2;
        gd.homeYaw = ped >= 0 ? g.peds[ped].yaw : 0.f;
        guards.push_back(gd);
    }

    void raise(GameWorld& g) {
        if (alarm) return;
        alarm = true;
        for (Guard& gd : guards)
            if (pedAlive(g, gd.ped)) setCombat(g, gd.ped, g.player);
    }

    // Returns true when the alarm is raised this frame.
    bool update(GameWorld& g, float dt, float playerNoise = 1.f) {
        if (alarm) return false;
        Ped* pl = g.playerPed();
        if (!pl) return false;
        vec3 pp = pl->pos.toVec3();
        bool crouched = pl->animIn.crouch;
        float plSpeed = ::length(vec2(pl->vel.x, pl->vel.y));
        int pv = pl->vehicle;
        meter = 0.f;
        for (Guard& gd : guards) {
            if (!pedAlive(g, gd.ped)) {
                // a guard found dead nearby by another guard raises the alarm
                continue;
            }
            Ped& gp = g.peds[gd.ped];
            // alerted by damage (shot at)
            if (gp.lastAttacker == g.player && g.time - gp.lastDamageTime < 1.0) {
                spotter = gd.ped;
                raise(g);
                return true;
            }
            // patrol
            if (!gd.stationary && gp.brain.type != BRAIN_COMBAT) {
                vec3 goal = gd.patrol[gd.wp % gd.patrol.size()];
                float dist = ::length(goal.xy() - gp.pos.toVec3().xy());
                if (dist < 1.2f) {
                    if (gd.wait <= 0.f) setIdle(g, gd.ped, 0);
                    gd.wait += dt;
                    if (gd.wait > 3.5f) {
                        gd.wait = 0.f;
                        gd.wp = (gd.wp + 1) % (int)gd.patrol.size();
                    }
                } else if (gp.brain.type != BRAIN_GOTO) {
                    setGoto(g, gd.ped, goal, 1.3f);
                } else {
                    gp.brain.goal = dvec3(goal);
                }
            } else if (gd.stationary && gp.brain.type == BRAIN_NONE) {
                // look around slowly
                gp.yaw = gd.homeYaw + sinf((float)g.time * 0.35f + gd.ped) * 0.8f;
            }
            // vision
            vec3 eye = gp.pos.toVec3() + vec3(0, 0, 1.6f);
            vec3 d = pp + vec3(0, 0, 1.f) - eye;
            float dist = ::length(d);
            float range = gd.range * (crouched ? 0.6f : 1.f) * (pv >= 0 ? 1.4f : 1.f) * playerNoise;
            bool heard = dist < (plSpeed > 5.f ? 7.f : (crouched ? 1.5f : 3.f)) * playerNoise;
            bool seen = false;
            if (dist < range) {
                vec2 f(-sinf(gp.yaw), cosf(gp.yaw));
                float cs = dot(normalize(d.xy()), f);
                if (cs > gd.fovCos && g.lineOfSight(dvec3(eye), pl->pos + dvec3(0, 0, 1.2), gd.ped, pv)) seen = true;
            }
            if (seen || heard) {
                float rate = (seen ? 1.2f : 0.6f) * (1.f + Saturate((range - dist) / Max(range, 1.f)) * 2.f) * (plSpeed > 4.f ? 1.5f : 1.f);
                gd.suspicion += dt * rate;
                if (gd.suspicion > 0.35f && gp.brain.type != BRAIN_COMBAT) facePed(g, gd.ped, pp);
            } else {
                gd.suspicion = Max(0.f, gd.suspicion - dt * 0.25f);
            }
            meter = Max(meter, Saturate(gd.suspicion));
            if (gd.suspicion >= 1.f) {
                spotter = gd.ped;
                raise(g);
                return true;
            }
        }
        // bodies: a living guard close to a dead one gets suspicious fast
        for (Guard& a : guards) {
            if (!pedAlive(g, a.ped)) continue;
            for (Guard& b : guards) {
                if (&a == &b || pedAlive(g, b.ped) || b.ped < 0 || !g.peds[b.ped].used) continue;
                float dd = ::length(pedPos(g, a.ped) - pedPos(g, b.ped));
                if (dd < 9.f && g.lineOfSight(g.peds[a.ped].pos + dvec3(0, 0, 1.6), g.peds[b.ped].pos + dvec3(0, 0, 0.3), a.ped, -1)) {
                    a.suspicion += dt * 0.9f;
                    if (a.suspicion >= 1.f) {
                        spotter = a.ped;
                        raise(g);
                        return true;
                    }
                }
            }
        }
        return false;
    }
};

// Shows the detection meter in the mission counter HUD element.
void showMeter(GameWorld& g, const char* label, float v) {
    if (v <= 0.01f) {
        if (g.missionCounterLabel == label) g.missionCounterLabel.clear();
        return;
    }
    g.missionCounterLabel = label;
    g.missionCounter = (int)(Saturate(v) * 100.f);
    g.missionCounterMax = 100;
}

// ------------------------------------------------------------------------------------------------------------------
// Tailing: keep a target in a distance band. Returns 0 ok, 1 lost (too far too long), 2 spotted (too close too long).
struct TailState {
    float farTime = 0.f, closeTime = 0.f;
    int update(GameWorld& g, vec3 target, float minDist, float maxDist, float dt, float farLimit = 10.f, float closeLimit = 5.f) {
        float d = ::length(playerPos(g) - target);
        if (d > maxDist) farTime += dt;
        else farTime = Max(0.f, farTime - dt * 2.f);
        if (d < minDist) closeTime += dt;
        else closeTime = Max(0.f, closeTime - dt);
        if (farTime > farLimit) return 1;
        if (closeTime > closeLimit) return 2;
        if (farTime > 1.f && g.hudHelpTimer <= 0.f) g.help("You're falling behind. ~y~Get closer~s~.", 1.5f);
        if (closeTime > 1.f && g.hudHelpTimer <= 0.f) g.help("Too close! ~r~Back off~s~ or you'll be spotted.", 1.5f);
        return 0;
    }
};

}  // namespace mu
}  // namespace Game
