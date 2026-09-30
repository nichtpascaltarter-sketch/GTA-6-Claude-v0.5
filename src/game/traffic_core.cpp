// Traffic driver model: localization on the lane graph, route choice, perception of the corridor ahead, intersection
// gating (signals with protected/permissive lefts, all-way stops first come first served, priority/yield and merges),
// lane changes (mandatory for turns, discretionary overtaking, keep-right), speed planning (limits, curvature,
// Gipps-style safe following with time headway, exact stops at stop lines) and the low-level controller producing
// Vehicles::VehicleControls (pure pursuit steering from the rear axle, PI speed control, standstill hold without
// triggering the automatic reverse). Far vehicles run a kinematic "dummy" mode with the physics asleep.
#include "ai_core.h"
#include <queue>

namespace AI {

namespace tc_detail {

constexpr float kPlanInterval = 0.05f;
constexpr float kDummyPlanInterval = 0.2f;
constexpr float kMaxBodyExtent = 7.f;   // largest body half-extent (buses) for hash queries

float timeToCover(float dist, float v, float a, float vmax) {
    if (dist <= 0.f) return 0.f;
    v = Max(v, 0.f);
    vmax = Max(vmax, v + 0.1f);
    float tAcc = (vmax - v) / a;
    float dAcc = v * tAcc + 0.5f * a * tAcc * tAcc;
    if (dAcc >= dist) return (-v + sqrtf(v * v + 2.f * a * dist)) / a;
    return tAcc + (dist - dAcc) / vmax;
}

// Gipps-style safe speed behind a leader: v^2/(2b) + T v <= gap - s0 + vl^2/(2b)
float safeSpeed(float gap, float vl, float T, float b, float s0) {
    float x = gap - s0 + Max(vl, 0.f) * Max(vl, 0.f) / (2.f * b);
    if (x <= 0.f) return 0.f;
    float bt = b * T;
    return -bt + sqrtf(bt * bt + 2.f * b * x);
}

struct Sample {
    vec2 p, t;
    float z;
    float shift;  // lateral shift of the corridor center from the planned path (maneuvers)
    float x;      // distance from the vehicle origin along the route
    int path;
};

}  // namespace tc_detail

using namespace tc_detail;

// ---------------------------------------------------------------------------------------------------------------------
Personality Personality::make(u32 seed, bool forceCautious) {
    Personality p;
    float r = hashToFloat(hash32(seed * 7u + 1u));
    float j = hashToFloat(hash32(seed * 13u + 5u));
    p.temper = forceCautious ? TEMP_CAUTIOUS : (r < 0.25f ? TEMP_CAUTIOUS : (r < 0.8f ? TEMP_NORMAL : TEMP_AGGRESSIVE));
    switch (p.temper) {
        case TEMP_CAUTIOUS:
            p.speedFactor = 0.86f + 0.08f * j;
            p.headway = 1.8f + 0.4f * j;
            p.minGap = 2.8f;
            p.accel = 1.5f;
            p.decel = 2.6f;
            p.latAcc = 2.4f;
            p.gapTime = 4.5f;
            p.patience = 7.f;
            p.overtake = 99.f;
            p.runsAmber = false;
            p.rightOnRed = false;
            break;
        case TEMP_NORMAL:
            p.speedFactor = 0.95f + 0.12f * j;
            p.headway = 1.3f + 0.3f * j;
            p.minGap = 2.2f;
            p.accel = 2.1f;
            p.decel = 3.0f;
            p.latAcc = 3.0f;
            p.gapTime = 3.6f;
            p.patience = 4.f;
            p.overtake = 4.f;
            p.runsAmber = j > 0.8f;
            p.rightOnRed = true;
            break;
        default:
            p.speedFactor = 1.08f + 0.17f * j;
            p.headway = 0.85f + 0.3f * j;
            p.minGap = 1.7f;
            p.accel = 3.0f;
            p.decel = 3.8f;
            p.latAcc = 3.7f;
            p.gapTime = 2.7f;
            p.patience = 1.8f;
            p.overtake = 2.f;
            p.runsAmber = true;
            p.rightOnRed = true;
            break;
    }
    return p;
}

void BodyHash::clear() {
    for (int& h : head) h = -1;
}

void BodyHash::build(std::vector<Body>& bodies) {
    clear();
    for (int i = 0; i < (int)bodies.size(); i++) {
        Body& b = bodies[i];
        b.cx = (int)floorf(b.pos.x / kCell);
        b.cy = (int)floorf(b.pos.y / kCell);
        u32 s = slot(b.cx, b.cy);
        b.next = head[s];
        head[s] = i;
    }
}

VehicleInfo makeVehicleInfo(const Vehicles::VehicleModel& m, const Vehicles::VehicleState& s) {
    VehicleInfo vi;
    vi.halfLen = m.boxHalf.y;
    vi.halfWid = m.boxHalf.x;
    vi.frontLen = m.boxCenter.y + m.boxHalf.y;
    vi.rearLen = m.boxHalf.y - m.boxCenter.y;
    vi.wheelbase = Max(s.tune.wheelbase, 1.f);
    vi.rearAxleY = s.tune.rearY;
    vi.frontAxleY = s.tune.frontY;
    vi.maxSteer = s.tune.maxSteer;
    vi.alphaPeak = s.tune.alphaPeak;
    vi.grip = m.grip;
    vi.topSpeed = m.topSpeed;
    vi.mass = Max(m.mass, 100.f);
    vi.powerW = Max(m.power, 10.f) * 1000.f;
    vi.dragK = 0.5f * 1.225f * m.dragCoef * m.frontalArea / vi.mass;
    vi.bus = m.cls == Vehicles::VC_BUS;
    vi.bike = m.cls == Vehicles::VC_MOTORBIKE || m.cls == Vehicles::VC_SCOOTER;
    vi.big = m.cls == Vehicles::VC_BUS || m.cls == Vehicles::VC_TRUCK || m.cls == Vehicles::VC_FIRETRUCK;
    return vi;
}

// ---------------------------------------------------------------------------------------------------------------------
Driver& TrafficCore::attach(int vid, u32 uid, u32 seed, const VehicleInfo& info, int lane, float u, bool forceCautious) {
    if (vid >= (int)drivers.size()) drivers.resize(vid + 1);
    Driver& d = drivers[vid];
    d = Driver();
    d.active = true;
    d.uid = uid;
    d.vehicle = vid;
    d.info = info;
    d.pers = Personality::make(seed, forceCautious);
    d.path = lane;
    d.u = lane >= 0 ? Clamp(u, g->lanes[lane].u0, g->lanes[lane].u1) : 0.f;
    d.planTimer = hashToFloat(hash32(seed + 99u)) * kPlanInterval;
    d.honkTimer = hashToFloat(hash32(seed + 7u)) * 2.f;
    if (lane >= 0) planRoute(d);
    return d;
}

void TrafficCore::detach(int vid) {
    if (vid >= 0 && vid < (int)drivers.size()) drivers[vid].active = false;
}

void TrafficCore::clearRoute(Driver& d) {
    d.routeLen = 0;
    d.gateConn = -1;
    d.committed = false;
    d.stopDone = false;
    d.lcLane = -1;
}

void TrafficCore::beginTick(double t) {
    time = t;
    frame++;
    hash.build(bodies);
    for (int n : touchedNodes) nodeReg[n].clear();
    touchedNodes.clear();
    bodyOfDriver.assign(drivers.size(), -1);
    for (int i = 0; i < (int)bodies.size(); i++)
        if (bodies[i].driver >= 0 && bodies[i].driver < (int)drivers.size()) bodyOfDriver[bodies[i].driver] = i;
    const int NL = (int)g->lanes.size();
    for (int i = 0; i < (int)drivers.size(); i++) {
        const Driver& d = drivers[i];
        if (!d.active || d.path < 0) continue;
        int bi = driverBody(i);
        float v = bi >= 0 ? bodies[bi].speed : d.vDummy;
        if (d.path >= NL) {
            const Connector& c = g->conn(d.path);
            if (nodeReg[c.node].empty()) touchedNodes.push_back(c.node);
            nodeReg[c.node].push_back({i, d.path - NL, d.u + d.info.frontLen, v, 0.f});
        } else if (d.routeLen > 0 && d.route[0] >= NL) {
            const Lane& L = g->lanes[d.path];
            float dist = L.u1 - (d.u + d.info.frontLen);
            if (dist < Max(90.f, v * 9.f)) {
                const Connector& c = g->conn(d.route[0]);
                if (nodeReg[c.node].empty()) touchedNodes.push_back(c.node);
                nodeReg[c.node].push_back({i, d.route[0] - NL, -dist, v, 0.f});
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Route choice
int TrafficCore::chooseConnector(Driver& d, int lane, bool fromCurrentLane) {
    const LaneGraph& G = *g;
    const Lane& L = G.lanes[lane];
    int gf = G.groupFirst[L.group];
    int gc = G.groupCount[L.group];
    // lane changes possible on this lane?  (short links: stay in lane)
    float room = L.u1 - (fromCurrentLane ? d.u : L.u0);
    bool routed = (d.mode == DM_ROUTE || d.mode == DM_EMERGENCY || d.hasDest) && !d.destEdges.empty();
    int maxShift = room > 150.f ? 3 : (room > 90.f ? 2 : (room > 45.f ? 1 : 0));
    if (routed) maxShift = room > 100.f ? 3 : (room > 55.f ? 2 : (room > 25.f ? 1 : 0));   // a turn to make: change lanes sooner
    if (d.mode == DM_FLEE || d.mode == DM_EMERGENCY) maxShift = Min(maxShift, room > 60.f ? 1 : 0);
    struct Opt {
        int conn;
        float w;
    };
    Opt opts[48];
    int n = 0;
    u32 h = hash32(d.uid * 2654435761u + (u32)lane * 40503u + (u32)(time * 3.0));
    // destination routing: desired next edge
    int wantEdge = -1;
    if ((d.mode == DM_ROUTE || d.mode == DM_EMERGENCY || d.hasDest) && !d.destEdges.empty()) {
        for (size_t k = 0; k + 1 < d.destEdges.size(); k++)
            if (d.destEdges[k] == L.edge) {
                wantEdge = d.destEdges[k + 1];
                break;
            }
    }
    for (int k = 0; k < gc && n < 48; k++) {
        int li = gf + k;
        if (abs(k - (int)L.index) > maxShift) continue;
        for (int c : G.lanes[li].out) {
            if (n >= 48) break;
            const Connector& C = G.conns[c];
            const Lane& T = G.lanes[C.to];
            float w;
            switch (C.turn) {
                case TK_STRAIGHT: w = 1.f; break;
                case TK_MERGE: w = 0.9f; break;
                case TK_RIGHT: w = 0.42f; break;
                case TK_LEFT: w = 0.36f; break;
                default: w = 0.02f; break;
            }
            if ((T.flags & LF_DIRT) && !(L.flags & LF_DIRT)) w *= 0.08f;
            if ((L.flags & LF_HIGHWAY) && !(T.flags & LF_HIGHWAY)) w *= 0.25f;       // mostly stay on the highway
            if (!(L.flags & (LF_HIGHWAY | LF_RAMP)) && (T.flags & LF_RAMP)) w *= 0.5f;
            // dead ends: only a turning circle at the end (blocked ones and long vehicles: avoid)
            if (G.nodes[T.toNode].deadEnd) w *= (G.nodes[T.toNode].uturnBlocked || d.info.wheelbase > 3.3f) ? 0.001f : 0.2f;
            if (T.flags & LF_NOTRAFFIC) w *= 0.01f;
            // long vehicles avoid turns tighter than they can follow (the body would sweep into oncoming lanes)
            if (d.info.wheelbase > 3.3f && C.minRadius < d.info.wheelbase / Max(sinf(d.info.maxSteer), 0.3f) * 1.05f) w *= 0.03f;
            // prefer connectors from the lane we are in
            int shift = abs(k - (int)L.index);
            w *= shift == 0 ? 1.f : (shift == 1 ? 0.55f : 0.25f);
            if (wantEdge >= 0) w = T.edge == wantEdge ? w * 100.f + 10.f : w * 0.01f;
            if (d.mode == DM_FLEE) {
                vec2 endP = G.lanePos(C.to, T.u1).xy();
                float dd = length(endP - d.threat);
                w = (w + 0.2f) * (0.2f + dd * dd * 1e-4f);
            }
            opts[n++] = {c, w};
        }
    }
    if (n == 0) return -1;
    float total = 0.f;
    for (int i = 0; i < n; i++) total += opts[i].w;
    float r = hashToFloat(h) * total;
    for (int i = 0; i < n; i++) {
        r -= opts[i].w;
        if (r <= 0.f) return opts[i].conn;
    }
    return opts[n - 1].conn;
}

void TrafficCore::planRoute(Driver& d) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    if (d.path < 0) return;
    // destination routing: recompute the route every few seconds from where the car is
    if (d.hasDest && (d.destEdges.empty() || d.destRecalc <= 0.f)) setDestination(d, d.dest);
    // planned turns that leave the route (chosen before the plan changed) are re-chosen, unless it is the next junction
    // and the car is committed to it or too close to change lanes
    if (d.hasDest && !d.destEdges.empty() && d.routeLen > 0) {
        for (int k = 0; k < d.routeLen; k++) {
            int p = d.route[k];
            if (p < NL) continue;
            const Connector& C = G.conn(p);
            int fromEdge = G.lanes[C.from].edge, want = -1;
            for (size_t q = 0; q + 1 < d.destEdges.size(); q++)
                if (d.destEdges[q] == fromEdge) {
                    want = d.destEdges[q + 1];
                    break;
                }
            if (want < 0 || G.lanes[C.to].edge == want) continue;
            bool nextJunction = d.path < NL && k == 0;
            float toEnd = d.path < NL ? G.lanes[d.path].u1 - d.u : 0.f;
            if (d.path >= NL && k == 0) continue;                                   // already crossing it
            if (nextJunction && (d.committed || toEnd < 12.f)) continue;
            d.routeLen = k;
            if (nextJunction) {
                d.gateConn = -1;
                d.stopDone = false;
                d.lcLane = -1;
            }
            break;
        }
    }
    int guard = 0;
    while (d.routeLen < Driver::kRouteMax - 1 && guard++ < 12) {
        int last = d.routeLen > 0 ? d.route[d.routeLen - 1] : d.path;
        if (last >= NL) {
            d.route[d.routeLen++] = G.conn(last).to;
        } else {
            int c = chooseConnector(d, last, d.routeLen == 0);
            if (c < 0) break;   // dead end without a way out
            d.route[d.routeLen++] = NL + c;
        }
    }
}

void TrafficCore::setDestination(Driver& d, vec2 dest) {
    d.dest = dest;
    d.hasDest = true;
    d.destRecalc = 4.f;
    d.destEdges.clear();
    d.destNodes.clear();
    const LaneGraph& G = *g;
    const World::RoadNetwork& R = *G.roads;
    if (d.path < 0) return;
    int startNode = G.isLane(d.path) ? G.lanes[d.path].toNode : G.lanes[G.conn(d.path).to].toNode;
    int startEdge = G.isLane(d.path) ? G.lanes[d.path].edge : G.lanes[G.conn(d.path).to].edge;
    float sB = 0.f;
    int goalEdge = R.nearestEdge(dest, 400.f, &sB);
    if (goalEdge < 0) return;
    // A* over directed road edges: a state is "travelled edge e in direction dir, now at its far node". Which turns
    // exist depends on the edge we arrive on (turn lanes, grade-separated crossings that share a road-network node but
    // have no connectors), so the arrival edge is part of the state.
    const int E = (int)R.edges.size();
    const int NS = E * 2;
    thread_local std::vector<float> gcost;
    thread_local std::vector<int> prevState;
    thread_local std::vector<u32> stamp;
    thread_local u32 curStamp = 0;
    if ((int)gcost.size() != NS) {
        gcost.assign(NS, 0.f);
        prevState.assign(NS, -1);
        stamp.assign(NS, 0);
    }
    curStamp++;
    auto touch = [&](int st) {
        if (stamp[st] != curStamp) {
            stamp[st] = curStamp;
            gcost[st] = 1e30f;
            prevState[st] = -1;
        }
    };
    auto endNode = [&](int st) { const World::RoadEdge& e = R.edges[st >> 1]; return (st & 1) ? e.n0 : e.n1; };
    // a move ea -> eb at node n needs a connector from a lane of ea arriving there to a lane of eb
    auto canTurn = [&](int n, int ea, int eb) -> bool {
        const NodeInfo& NI = G.nodes[n];
        if (NI.connCount <= 0) return true;
        for (int ci = NI.firstConn; ci < NI.firstConn + NI.connCount; ci++) {
            const Connector& cn = G.conns[ci];
            if (cn.from >= 0 && cn.to >= 0 && G.lanes[cn.from].edge == ea && G.lanes[cn.to].edge == eb) return true;
        }
        return false;
    };
    const World::RoadEdge& GE = R.edges[goalEdge];
    vec2 goalP = GE.posAt(sB).xy();
    typedef std::pair<float, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    int startDir = G.isLane(d.path) ? G.lanes[d.path].dir : G.lanes[G.conn(d.path).to].dir;
    int startState = startEdge * 2 + (startDir < 0 ? 1 : 0);
    touch(startState);
    gcost[startState] = 0.f;
    open.push(QE(length(R.nodes[startNode].p - goalP) * 0.4f, startState));
    int reached = -1;
    int expanded = 0;
    while (!open.empty() && expanded < 30000) {
        QE top = open.top();
        open.pop();
        int st = top.second;
        if (stamp[st] != curStamp) continue;
        int n = endNode(st);
        if (top.first > gcost[st] + length(R.nodes[n].p - goalP) * 0.4f + 0.01f) continue;
        expanded++;
        if ((st >> 1) == goalEdge) {   // travelling on the goal edge (the last turn onto it was a real one)
            reached = st;
            break;
        }
        for (int ei : R.nodes[n].edges) {
            const World::RoadEdge& e = R.edges[ei];
            int dir = e.n0 == n ? 1 : -1;
            if (G.groupFirst[ei * 2 + (dir < 0 ? 1 : 0)] < 0) continue;   // no lanes that way (one-way)
            if (e.cls == World::RC_DIRT && d.mode != DM_FLEE) continue;
            if (!canTurn(n, st >> 1, ei)) continue;
            int ns = ei * 2 + (dir < 0 ? 1 : 0);
            float c = gcost[st] + e.length / Max(World::roadInfo(e.cls).speed, 5.f) * 12.f;
            touch(ns);
            if (c < gcost[ns]) {
                gcost[ns] = c;
                prevState[ns] = st;
                int m = dir > 0 ? e.n1 : e.n0;
                open.push(QE(c + length(R.nodes[m].p - goalP) * 0.4f, ns));
            }
        }
    }
    if (reached < 0) return;
    std::vector<int> edges;
    for (int st = reached; st >= 0; st = prevState[st]) {
        edges.push_back(st >> 1);
        d.destNodes.push_back(endNode(st));
        if (st == startState) break;
    }
    std::reverse(edges.begin(), edges.end());
    std::reverse(d.destNodes.begin(), d.destNodes.end());
    if (edges.back() != goalEdge) edges.push_back(goalEdge);
    d.destEdges = edges;
    // re-plan the turns ahead onto the new route: everything when the car is still well before the next junction
    // (room to get into the right lane), otherwise keep the next connector (it may be committed to it) and drop the rest
    if (d.routeLen > 0) {
        bool onLane = G.isLane(d.path);
        float toEnd = onLane ? G.lanes[d.path].u1 - d.u : 0.f;
        if (onLane && toEnd > 35.f && !d.committed && d.gateConn < 0) {
            d.routeLen = 0;
            d.lcLane = -1;
        } else {
            const int NL = (int)G.lanes.size();
            for (int k = 0; k < d.routeLen; k++)
                if (d.route[k] >= NL) {
                    d.routeLen = Min(d.routeLen, k + 2);
                    break;
                }
        }
    }
}

bool TrafficCore::relocalize(Driver& d, vec2 pos, vec2 heading, float maxDist) {
    float u = 0.f, lat = 0.f;
    int l = g->nearestLane(pos, heading, maxDist, &u, &lat);
    stats.relocalizations++;
    if (l < 0) return false;
    const Lane& L = g->lanes[l];
    // inside an intersection box (before the lane starts / past its end): pick up the connector we are on instead,
    // so the box rules (conflicts, holds) keep applying
    if (u < L.u0 - 0.5f || u > L.u1 + 0.5f) {
        int node = u < L.u0 ? L.fromNode : L.toNode;
        const NodeInfo& N = g->nodes[node];
        vec2 hd = length2(heading) > 1e-6f ? normalize(heading) : vec2(0, 1);
        int best = -1;
        float bestScore = 1e9f, bestU = 0.f;
        for (int c = N.firstConn; c < N.firstConn + N.connCount; c++) {
            int path = g->connPath(c);
            float cl = 0.f;
            float cu = g->projectPath(path, pos, g->conns[c].length * 0.5f, &cl);
            float align = dot(g->pathTangent(path, cu), hd);
            if (align < 0.5f || fabsf(cl) > 3.f) continue;
            float score = fabsf(cl) + (1.f - align) * 6.f;
            if (score < bestScore) {
                bestScore = score;
                best = c;
                bestU = cu;
            }
        }
        if (best >= 0) {
            d.path = g->connPath(best);
            d.u = bestU;
            d.lat = 0.f;
            d.nudge = d.nudgeTarget = 0.f;
            clearRoute(d);
            planRoute(d);
            d.committed = true;   // already in the box: finish the movement
            d.gateConn = best;
            d.gateNode = node;
            d.enterTime = (float)time;
            return true;
        }
    }
    d.path = l;
    d.u = u;
    d.lat = 0.f;
    d.nudge = d.nudgeTarget = 0.f;
    clearRoute(d);
    planRoute(d);
    return true;
}

bool TrafficCore::laneFree(int lane, float u, float hl, float gap) const {
    vec3 p = g->lanePos(lane, u);
    vec2 t = g->laneTangent(lane, u);
    bool ok = true;
    float r = hl + gap + kMaxBodyExtent;
    hash.query(bodies, p.xy() - vec2(r), p.xy() + vec2(r), [&](int bi) {
        const Body& b = bodies[bi];
        if (fabsf(b.z - p.z) > 3.2f) return;
        vec2 rel = b.pos - p.xy();
        float along = dot(rel, t), lat = dot(rel, rightOf(t));
        if (fabsf(lat) < 2.6f && fabsf(along) < hl + b.halfLen + gap) ok = false;
    });
    return ok;
}

bool TrafficCore::rearClear(const Driver& d, vec2 pos, vec2 fwd, float dist) const {
    vec2 rgt = rightOf(fwd);
    vec2 rb = pos - fwd * d.info.rearLen;   // rear bumper
    int self = driverBody(d.vehicle);
    float z = self >= 0 ? bodies[self].z : 0.f;
    bool ok = true;
    float r = dist + kMaxBodyExtent + d.info.halfWid;
    vec2 mid = rb - fwd * (dist * 0.5f);
    hash.query(bodies, mid - vec2(r), mid + vec2(r), [&](int bi) {
        if (!ok || bi == self) return;
        const Body& b = bodies[bi];
        if (self >= 0 && fabsf(b.z - z) > 3.f) return;
        vec2 bR = rightOf(b.fwd);
        float extA = fabsf(dot(b.fwd, fwd)) * b.halfLen + fabsf(dot(bR, fwd)) * b.halfWid;   // its extent along our axes
        float extL = fabsf(dot(b.fwd, rgt)) * b.halfLen + fabsf(dot(bR, rgt)) * b.halfWid;
        vec2 rel = b.pos - rb;
        float behind = -dot(rel, fwd), lat = dot(rel, rgt);
        if (behind + extA > -0.2f && behind - extA < dist && fabsf(lat) < d.info.halfWid + extL + 0.25f) ok = false;
    });
    return ok;
}

SignalState TrafficCore::signalFor(const Driver& d) const {
    const int NL = (int)g->lanes.size();
    int c = -1;
    if (d.path >= NL) c = d.path - NL;
    else if (d.routeLen > 0 && d.route[0] >= NL) c = d.route[0] - NL;
    if (c < 0) return SIG_NONE;
    const Connector& C = g->conns[c];
    return g->movementSignal(C.node, C.approach, C.turn, time);
}

// ---------------------------------------------------------------------------------------------------------------------
// Localization along the route
void TrafficCore::advancePath(Driver& d) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    float len = G.pathLength(d.path);
    if (d.routeLen == 0) {
        d.u = Min(d.u, len);
        return;
    }
    float over = d.u - len;
    int next = d.route[0];
    for (int i = 1; i < d.routeLen; i++) d.route[i - 1] = d.route[i];
    d.routeLen--;
    if (next >= NL) {
        const Connector& c = G.conn(next);
        // lane change not completed: the connector starts on the lane we are actually in only if they match
        if (c.from != d.path) d.lat = 0.f;
        d.lcLane = -1;
        d.lat = Clamp(d.lat, -1.f, 1.f);
    } else {
        d.lat = 0.f;
    }
    d.path = next;
    d.u = (next < NL ? G.lanes[next].u0 : 0.f) + Max(over, 0.f);
    d.kturns = 0;
    d.enterTime = (float)time;
    d.committed = false;
    d.stopDone = false;
    d.amberDecided = -1;
    d.waitTime = 0.f;
    d.nudge = d.nudgeTarget = 0.f;
    if (d.routeLen < 4) planRoute(d);
}

void TrafficCore::localize(Driver& d, const Vehicles::VehicleState& s) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    vec2 pos = s.body.pos.toVec3().xy();
    for (int iter = 0; iter < 3; iter++) {
        float lat = 0.f;
        float u = G.projectPath(d.path, pos, d.u, &lat);
        float len = G.pathLength(d.path);
        d.latErr = lat - d.lat - d.nudge;
        if (d.path < NL) {
            // stop-line crossing bookkeeping (red light violations)
            const Lane& L = G.lanes[d.path];
            if (L.stopU >= 0.f && d.routeLen > 0 && d.route[0] >= NL) {
                float f0 = d.u + d.info.frontLen, f1 = u + d.info.frontLen;
                if (f0 < L.stopU && f1 >= L.stopU && d.mode == DM_NORMAL) {
                    const Connector& c = G.conn(d.route[0]);
                    SignalState sig = G.movementSignal(c.node, c.approach, c.turn, time);
                    if (sig == SIG_RED && !d.stopDone) {
                        stats.redViolations++;
                        d.redViolation = true;
                    }
                    if (G.nodes[c.node].control == 1 && !d.stopDone) stats.stopSignViolations++;
                }
            }
            d.u = u;
            if (u > L.u1) {
                advancePath(d);
                continue;
            }
        } else {
            d.u = u;
            if (u >= len - 0.25f && d.routeLen > 0) {
                // check whether the next lane already contains us
                int nx = d.route[0];
                float lat2 = 0.f;
                float u2 = G.projectPath(nx, pos, nx < NL ? G.lanes[nx].u0 : 0.f, &lat2);
                float start = nx < NL ? G.lanes[nx].u0 : 0.f;
                if (u2 >= start - 0.05f) {
                    d.u = len + (u2 - start);
                    advancePath(d);
                    continue;
                }
            }
        }
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Intersection gating
bool TrafficCore::conflictsClear(const Driver& d, const Connector& c, int connId, float distToEntry, float v, bool permissiveOnly,
                                 float margin) const {
    const LaneGraph& G = *g;
    const std::vector<NodeEntry>& reg = nodeReg[c.node];
    if (reg.empty()) return true;
    int self = d.vehicle;
    float myPos = -distToEntry;  // front bumper position along my connector
    float aStart = Max(d.pers.accel, 1.5f);
    float vmaxC = Max(Min(c.maxSpeed, G.lanes[c.to].speed), 4.f);
    const NodeInfo& N = G.nodes[c.node];
    const bool meLong = d.info.wheelbase > 3.8f;
    const int myBody = driverBody(self);
    // returns false when this conflict (with the vehicles on its other movement) blocks us
    auto clearOf = [&](const Conflict& x, bool wideOnly) -> bool {
        if (myPos - d.info.halfLen * 2.f > x.sEnd + 0.5f) return true;   // we already cleared this conflict zone
        float myArrive = timeToCover(x.s - myPos, v, aStart, vmaxC);
        float myClear = timeToCover(x.sEnd - myPos + d.info.halfLen * 2.f + 1.5f, v, aStart, vmaxC);
        const Connector& o = G.conns[x.other];
        for (const NodeEntry& e : reg) {
            if (e.conn != x.other || e.driver == self) continue;
            const Driver& od = drivers[e.driver];
            if (wideOnly && !meLong && od.info.wheelbase <= 3.8f) continue;   // only long bodies sweep that far
            // stopped right behind our own body: it cannot move before we do, so it is not what we are waiting for
            // (breaks box deadlocks: a left-turner at its hold point blocking the opposing left-turner it yields to)
            if (e.speed < 0.5f && myBody >= 0 && od.obstBody == myBody && od.obstDist < 8.f) continue;
            float oRear = e.dist - od.info.halfLen * 2.f;
            if (oRear > x.sOtherEnd + 0.5f) continue;  // already cleared the conflict zone
            bool theyYield = o.prio < c.prio && !(N.control == 2 && G.nodes[c.node].approaches[o.approach].axis != G.nodes[c.node].approaches[c.approach].axis);
            if (N.control == 1) theyYield = false;
            if (e.dist > -0.5f) {
                // inside the box: it has claimed the conflict unless it is waiting for us before the conflict point
                if (x.yield == 0 && theyYield && e.speed < 1.2f && e.dist < x.sOther - 2.f) continue;
                // both inside with equal rights: the later entrant waits (ties by id)
                bool meInside = myPos > 0.f;
                if (meInside && x.yield == 0 && !theyYield && e.dist < x.sOther - 1.5f) {
                    bool meFirst = d.enterTime < od.enterTime - 0.05f || (fabsf(d.enterTime - od.enterTime) <= 0.05f && self < e.driver);
                    if (meFirst) continue;
                }
                return false;
            }
            // approaching
            if (N.control == 1) continue;                         // all-way stops: handled by the queue
            if (N.control == 2) {
                SignalState os = G.movementSignal(c.node, o.approach, o.turn, time);
                if (os == SIG_RED && -e.dist > 2.5f && !od.committed) continue;
                if (os == SIG_AMBER && -e.dist > e.speed * 1.2f + 4.f && od.mode == DM_NORMAL && !od.pers.runsAmber) continue;
            }
            bool mustCheck = x.yield || (!permissiveOnly && !theyYield);
            if (!mustCheck) continue;
            float tOther = timeToCover(x.sOther - e.dist, e.speed, 2.2f, Max(e.speed, 12.f));
            if (e.speed < 0.3f && -e.dist > 2.f) tOther += 1.5f;   // stopped: needs time to start
            if (x.yield) {
                if (tOther < myClear + margin) return false;
            } else if (!theyYield) {
                // equal priority: first to arrive goes; ties by id
                if (tOther + 0.4f < myArrive || (fabsf(tOther - myArrive) <= 0.4f && e.driver < self)) return false;
            }
        }
        return true;
    };
    for (const Conflict& x : c.conflicts)
        if (!clearOf(x, false)) return false;
    for (const Conflict& x : c.wide)
        if (!clearOf(x, true)) return false;
    return true;
}

bool TrafficCore::stopGrant(Driver& d, int conn) {
    const Connector& c = g->conns[conn];
    StopQueue* Q = nullptr;
    for (auto& q : stopQueues)
        if (q.node == c.node) Q = &q;
    if (!Q) {
        // recycle an empty queue
        for (auto& q : stopQueues)
            if (q.q.empty()) {
                Q = &q;
                q.node = c.node;
                break;
            }
        if (!Q) {
            stopQueues.push_back(StopQueue());
            Q = &stopQueues.back();
            Q->node = c.node;
        }
    }
    const int NL = (int)g->lanes.size();
    // purge stale entries (drivers that left the stop line or the intersection)
    for (size_t i = 0; i < Q->q.size();) {
        int di = Q->q[i].first;
        const Driver& o = drivers[di];
        bool valid = o.active && o.path >= 0 && o.path < NL && o.routeLen > 0 && o.route[0] >= NL && g->conn(o.route[0]).node == c.node && o.stopDone;
        if (!valid && di != d.vehicle) Q->q.erase(Q->q.begin() + i);
        else i++;
    }
    bool present = false;
    for (auto& e : Q->q)
        if (e.first == d.vehicle) present = true;
    if (!present) {
        Q->q.push_back({d.vehicle, d.arrival});
        std::stable_sort(Q->q.begin(), Q->q.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.second < b.second; });
    }
    for (auto& e : Q->q) {
        if (e.first == d.vehicle) break;
        if (e.second > d.arrival - 0.05f && e.first > d.vehicle) continue;
        const Driver& o = drivers[e.first];
        int oc = o.route[0] - NL;
        // earlier arrival with a conflicting movement goes first (long vehicles: also the wider swept conflicts)
        bool conflict = false;
        for (const Conflict& x : c.conflicts)
            if (x.other == oc) conflict = true;
        if (d.info.wheelbase > 3.8f || o.info.wheelbase > 3.8f)
            for (const Conflict& x : c.wide)
                if (x.other == oc) conflict = true;
        if (conflict) return false;
    }
    if (!conflictsClear(d, c, conn, 0.f, 0.f, true, 0.5f)) return false;
    return true;
}

// Returns the distance from the front bumper to where the vehicle must stop (1e9 = free).
float TrafficCore::gate(Driver& d, int conn, float distToEntry, float v, bool inBox) {
    const LaneGraph& G = *g;
    const Connector& c = G.conns[conn];
    const NodeInfo& N = G.nodes[c.node];
    const Lane& L = G.lanes[c.from];
    const float FREE = 1e9f;
    if (d.mode == DM_FLEE || d.mode == DM_PURSUIT) return FREE;
    float lineDist = L.stopU >= 0.f ? distToEntry - (L.u1 - L.stopU) : distToEntry - 0.6f;
    bool emergency = d.mode == DM_EMERGENCY;
    if (inBox) {
        // permissive left turners wait inside the box before the conflict point
        if (c.holdS > 0.f && !d.committed) {
            float pos = -distToEntry;  // front bumper along the connector
            if (conflictsClear(d, c, conn, distToEntry, v, true, d.pers.gapTime - 2.6f)) {
                d.committed = true;
                return FREE;
            }
            if (pos > c.holdS + 1.5f) {
                // already past the hold point: finish the turn but only when nothing is in the conflict zone
                return conflictsClear(d, c, conn, distToEntry, v, true, -1.f) ? FREE : Max(0.f, c.holdS + 4.f - pos);
            }
            return Max(0.f, c.holdS - pos);
        }
        return FREE;
    }
    if (d.committed && d.gateConn == conn) return FREE;
    if (d.gateConn != conn) {
        d.gateConn = conn;
        d.gateNode = c.node;
        d.committed = false;
        d.stopDone = false;
        d.waitTime = 0.f;
    }
    // past the stop line: committed
    if (lineDist < -0.4f && (N.control != 1 || d.stopDone)) {
        if (N.control != 2 || d.amberGo || G.movementSignal(c.node, c.approach, c.turn, time) != SIG_RED || lineDist < -2.f) {
            d.committed = true;
            return FREE;
        }
    }
    bool atLine = lineDist < 2.2f && v < 0.5f;
    if (atLine && !d.stopDone) {
        d.stopDone = true;
        d.arrival = (float)time;
    }
    // box blocking: never enter unless the exit has room for us
    auto exitBlocked = [&]() -> bool {
        if (d.obstDist > 1e8f) return false;
        float needed = distToEntry + c.length + d.info.halfLen * 2.f + 1.5f;
        return d.obstDist < needed && d.obstSpeed < 1.5f && d.obstDist > distToEntry + 1.f;
    };
    float impatience = d.waitTime > 30.f ? 1.f : 0.f;
    switch (N.control) {
        case 2: {
            SignalState sig = G.movementSignal(c.node, c.approach, c.turn, time);
            if (emergency) {
                // siren: treat red as a yield, cross carefully
                if (sig == SIG_RED || sig == SIG_AMBER) {
                    if (v > 6.f && lineDist < 25.f) return Max(lineDist + 6.f, 0.f);   // slow approach
                    if (!conflictsClear(d, c, conn, distToEntry, v, false, 1.0f)) return lineDist;
                }
                return FREE;
            }
            if (sig == SIG_AMBER) {
                if (d.amberDecided != conn) {
                    d.amberDecided = conn;
                    // go when a comfortable stop is no longer possible and the line is reached before red; when the
                    // line cannot be reached in time but a hard stop still works, brake hard (dilemma zone)
                    float left = 0.f;
                    while (left < 6.f && G.movementSignal(c.node, c.approach, c.turn, time + left + 0.25f) == SIG_AMBER) left += 0.25f;
                    float need = v * v / (2.f * d.pers.decel * 1.25f) + v * 0.25f;
                    float tLine = lineDist / Max(v, 0.5f);
                    bool hardStopOk = v * v / (2.f * 6.5f) + v * 0.1f < lineDist;
                    d.amberGo = (lineDist < need && (tLine < left + 0.2f || !hardStopOk)) ||
                                (d.pers.runsAmber && lineDist < v * 2.2f && tLine < left + 0.6f);
                }
                if (!d.amberGo) return lineDist;
                return FREE;
            }
            // committed on amber: keep going only if a hard stop is no longer possible
            if (sig == SIG_RED && d.amberGo && d.amberDecided == conn && v > 2.f && v * v / (2.f * 6.5f) + v * 0.1f > lineDist) return FREE;
            d.amberDecided = -1;
            d.amberGo = false;
            if (sig == SIG_RED) {
                // too close to stop at the line (signal changed while very close at speed, or just spawned there):
                // stop hard before the box if at all possible, only a car that cannot even do that carries on
                if (lineDist < 1.f && v * v / (2.f * 7.f) > lineDist + 1.f) {
                    if (v * v / (2.f * 8.5f) < distToEntry - 0.3f) return Max(distToEntry - 0.3f, 0.f);
                    return FREE;
                }
                bool rightOnRed = c.turn == TK_RIGHT && d.pers.rightOnRed && L.right < 0 && !(L.flags & LF_HIGHWAY);
                if (rightOnRed && d.stopDone && d.waitTime > 1.2f && !exitBlocked() &&
                    conflictsClear(d, c, conn, distToEntry, v, false, d.pers.gapTime)) {
                    // yield to everything: also no conflicting traffic with green
                    bool clear = true;
                    for (const Conflict& x : c.conflicts) {
                        for (const NodeEntry& e : nodeReg[c.node])
                            if (e.conn == x.other && e.driver != d.vehicle) {
                                const Connector& o = G.conns[e.conn];
                                SignalState os = G.movementSignal(c.node, o.approach, o.turn, time);
                                if (os != SIG_RED && e.dist > -60.f) clear = false;
                            }
                    }
                    if (clear) {
                        d.committed = true;
                        return FREE;
                    }
                }
                return lineDist;
            }
            // green / arrow
            if (exitBlocked() && impatience < 1.f) return lineDist;
            if (sig == SIG_GREEN && (c.turn == TK_LEFT || c.turn == TK_UTURN)) {
                if (conflictsClear(d, c, conn, distToEntry, v, true, d.pers.gapTime - 2.6f)) return FREE;
                // creep into the box up to the hold point
                if (c.holdS > 0.f) return distToEntry + c.holdS;
                return lineDist;
            }
            if (!conflictsClear(d, c, conn, distToEntry, v, true, 0.2f)) return Max(lineDist, 0.f);
            return FREE;
        }
        case 1: {
            if (!d.stopDone) return Max(lineDist, 0.f);
            if (exitBlocked() && impatience < 1.f) return lineDist;
            if (stopGrant(d, conn) || d.waitTime > 25.f) {
                d.committed = true;
                return FREE;
            }
            return lineDist;
        }
        default: {
            bool yields = false;
            for (const Conflict& x : c.conflicts) yields |= x.yield != 0 || x.merge != 0;
            if (!yields && c.conflicts.empty()) return FREE;
            if (exitBlocked() && impatience < 1.f && c.turn != TK_STRAIGHT) return Max(lineDist, 0.f);
            float margin = d.pers.gapTime - 2.f;
            if (conflictsClear(d, c, conn, distToEntry, v, false, margin) || d.waitTime > 20.f) return FREE;
            return Max(lineDist, 0.f);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Lane changes
bool TrafficCore::gapOk(const Driver& d, int lane, float v, float extra) const {
    const LaneGraph& G = *g;
    int bi = driverBody(d.vehicle);
    vec2 pos = bi >= 0 ? bodies[bi].pos : G.lanePos(d.path, d.u).xy();
    float myZ = bi >= 0 ? bodies[bi].z : G.lanePos(d.path, d.u).z;
    vec2 t = G.laneTangent(d.path, d.u);
    vec2 r = rightOf(t);
    float shift = (G.lanes[lane].offset - G.lanes[d.path].offset) * (float)G.lanes[d.path].dir;
    bool ok = true;
    float range = 60.f + v * 2.f;
    hash.query(bodies, pos - vec2(range), pos + vec2(range), [&](int k) {
        if (!ok || k == bi) return;
        const Body& b = bodies[k];
        if (fabsf(b.z - myZ) > 3.2f) return;
        vec2 rel = b.pos - pos;
        float along = dot(rel, t);
        float lat = dot(rel, r) - shift;
        if (fabsf(lat) > b.halfWid + d.info.halfWid + 0.7f) return;
        if (fabsf(along) > range) return;
        float vb = dot(b.vel, t);
        float len = b.halfLen + d.info.halfLen;
        if (along >= 0.f) {
            float gap = along - len;
            if (gap < Max(3.f, v * 0.55f + (v - vb) * 1.1f) + extra) ok = false;
        } else {
            float gap = -along - len;
            if (gap < Max(4.f, vb * 0.75f + (vb - v) * 1.6f) + extra) ok = false;
        }
    });
    return ok;
}

void TrafficCore::laneChangeLogic(Driver& d, float v, float distToEnd) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    if (d.path < 0 || d.path >= NL || d.lcLane >= 0 || d.nudge != 0.f || d.nudgeTarget != 0.f) return;
    const Lane& L = G.lanes[d.path];
    int need = -1;
    if (d.routeLen > 0 && d.route[0] >= NL) {
        int from = G.conn(d.route[0]).from;
        if (from != d.path && G.lanes[from].group == L.group) need = from;
    }
    auto start = [&](int target) {
        d.lcLane = target;
        d.lcFrom = d.lat;
        d.lcTo = (G.lanes[target].offset - L.offset) * (float)L.dir;
        d.lcU0 = d.u;
        d.lcLen = Clamp(v * 3.2f, 16.f, 60.f);
        d.indicator = d.lcTo > 0.f ? 1 : -1;
        d.indicatorTimer = 0.f;
    };
    if (need >= 0) {
        int step = G.lanes[need].index > L.index ? 1 : -1;
        int target = step > 0 ? L.right : L.left;
        int count = abs((int)G.lanes[need].index - (int)L.index);
        if (target < 0) return;
        d.indicator = step;
        if (distToEnd < 10.f + v * 0.5f) {
            // too late: take a movement available from this lane instead
            int c = chooseConnector(d, d.path, true);
            int cl = -1;
            for (int k = 0; k < 6 && (c < 0 || G.conns[c].from != d.path); k++) c = chooseConnector(d, d.path, true);
            if (c >= 0 && G.conns[c].from == d.path) cl = c;
            if (cl < 0 && !L.out.empty()) cl = L.out[hash32(d.uid + (u32)time) % L.out.size()];
            if (cl >= 0) {
                d.routeLen = 0;
                d.route[d.routeLen++] = NL + cl;
                planRoute(d);
                d.indicator = 0;
            }
            return;
        }
        if (d.lcCooldown > 0.f) return;
        if (gapOk(d, target, v, 0.f)) start(target);
        else if (distToEnd < count * Max(22.f, v * 2.5f) + 15.f) d.speedCap = Min(d.speedCap, Max(v - 1.5f, 5.f));  // slow to find a gap
        return;
    }
    if (d.lcCooldown > 0.f || L.count < 2 || distToEnd < 70.f || d.mode == DM_HOLD || d.mode == DM_PULLOVER) return;
    // discretionary: overtake a slow leader
    float desired = G.lanes[d.path].speed * d.pers.speedFactor;
    if (d.obstDist < 45.f && d.obstSpeed < desired - d.pers.overtake && d.obstBody >= 0 && bodies[d.obstBody].kind == BK_CAR) {
        int cand[2] = {L.left, L.right};
        for (int k = 0; k < 2; k++) {
            int t = cand[k];
            if (t < 0) continue;
            // stay within reach of the lane our next movement needs
            if (d.routeLen > 0 && d.route[0] >= NL) {
                int from = G.conn(d.route[0]).from;
                if (abs((int)G.lanes[from].index - (int)G.lanes[t].index) * 60.f > distToEnd - 40.f) continue;
            }
            if (gapOk(d, t, v, 4.f)) {
                start(t);
                d.lcCooldown = 6.f;
                return;
            }
        }
    }
    // keep right on multi-lane roads when the right lane is free
    if ((L.flags & LF_HIGHWAY) && L.right >= 0 && d.obstDist > 80.f && hash32(d.uid + (u32)(time * 0.2)) % 7 == 0) {
        int t = L.right;
        if (d.routeLen > 0 && d.route[0] >= NL) {
            int from = G.conn(d.route[0]).from;
            if (G.lanes[from].index < G.lanes[t].index) return;
        }
        if (gapOk(d, t, v, 8.f)) {
            start(t);
            d.lcCooldown = 12.f;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Planning: corridor perception, stops, speed target
void TrafficCore::plan(Driver& d, const Vehicles::VehicleState& s, vec2 pos, vec2 fwd, float v, float dt) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    const Personality& P = d.pers;
    if (d.routeLen < 4) planRoute(d);
    float front = d.info.frontLen;
    d.speedCap = 1e9f;
    // ---- free speed
    float modeK = d.mode == DM_FLEE ? 1.45f : (d.mode == DM_EMERGENCY ? 1.35f : 1.f);
    float limit = G.pathSpeed(d.path) * P.speedFactor * modeK + (d.mode == DM_FLEE ? 4.f : 0.f);
    limit = Min(limit, d.info.topSpeed * 0.9f);
    float latAcc = P.latAcc * (d.mode == DM_FLEE ? 1.35f : 1.f);
    float lookDist = Clamp(v * v / (2.f * P.decel) + v * 2.5f + 30.f, 40.f, 260.f);
    const float kCar = sinf(d.info.maxSteer) / Max(d.info.wheelbase, 1.f);   // tightest front-axle path at full lock
    // ---- walk the route: curvature, limits, corridor samples
    float vCurve = 99.f, vLimAhead = 99.f;
    Sample smp[96];
    int ns = 0;
    {
        float acc = 0.f;
        int path = d.path;
        float u = d.u;
        int ri = -1;
        float nextSample = 0.f;
        while (acc < lookDist) {
            bool lane = path < NL;
            float end = lane ? G.lanes[path].u1 : G.conn(path).length;
            if (end > u) {
                float lim = G.pathSpeed(path) * P.speedFactor * modeK;
                if (ri >= 0) vLimAhead = Min(vLimAhead, sqrtf(lim * lim + 2.f * P.decel * Max(0.f, acc - front)));
                float step = lane ? 3.f : 1.5f;
                int nStep = (int)Clamp((end + 0.01f - u) / step, 0.f, 400.f);   // integer stepping: bounded for any u
                for (int si = 0; si <= nStep; si++) {
                    float uu = u + step * (float)si;
                    float x = acc + (uu - u);
                    if (x > lookDist) break;
                    float k = fabsf(G.pathCurv(path, uu));
                    if (k > 2e-4f) {
                        float vc = sqrtf(latAcc / k);
                        // near the tightest path this vehicle can steer: crawl (little tyre slip, time to correct)
                        if (k > 0.6f * kCar) vc = Min(vc, Lerp(5.f, 2.2f, Saturate((k / kCar - 0.6f) * 2.5f)));
                        vCurve = Min(vCurve, sqrtf(vc * vc + 2.f * P.decel * Max(0.f, x - front * 0.5f)));
                    }
                    if (x >= nextSample && ns < 96) {
                        float lat = path == d.path ? d.lat + d.nudge : 0.f;
                        // during a lateral maneuver (lane change, nudge) the corridor near the car starts at its
                        // actual lateral position so vehicles it is still beside are not missed
                        bool maneuver = d.lcLane >= 0 || fabsf(d.nudge - d.nudgeTarget) > 0.05f || fabsf(d.nudge) > 0.1f;
                        float planned = lat;
                        if (maneuver) {
                            float actual = d.latErr + d.lat + d.nudge;
                            float bw = SmoothStep(4.f, 22.f, x);
                            lat = Lerp(actual, path == d.path ? lat : 0.f, bw);
                        }
                        smp[ns].shift = lat - planned;
                        vec3 p = G.pathPos(path, uu, lat);
                        smp[ns].p = p.xy();
                        smp[ns].z = p.z;
                        smp[ns].t = G.pathTangent(path, uu);
                        smp[ns].x = x;
                        smp[ns].path = path;
                        ns++;
                        nextSample = x + (x < 50.f ? 2.5f : 6.f);
                    }
                }
                acc += end - u;
            }
            ri++;
            if (ri >= d.routeLen) break;
            path = d.route[ri];
            u = path < NL ? G.lanes[path].u0 : 0.f;
        }
    }
    // ---- corridor perception
    int selfBody = driverBody(d.vehicle);
    float obstGap = 1e9f, obstV = 0.f;
    int obstB = -1;
    bool obstPed = false;
    thread_local std::vector<u32> seen;
    thread_local u32 seenStamp = 0;
    if (seen.size() < bodies.size()) seen.resize(bodies.size() + 64, 0);
    seenStamp++;
    float myHW = d.info.halfWid;
    for (int i0 = 0; i0 < ns; i0 += 4) {
        int i1 = Min(ns - 1, i0 + 4);
        vec2 mn = vmin(smp[i0].p, smp[i1].p), mx = vmax(smp[i0].p, smp[i1].p);
        for (int k = i0; k <= i1; k++) {
            mn = vmin(mn, smp[k].p);
            mx = vmax(mx, smp[k].p);
        }
        float ex = kMaxBodyExtent + 4.f;
        hash.query(bodies, mn - vec2(ex), mx + vec2(ex), [&](int bi) {
            if (bi == selfBody || seen[bi] == seenStamp) return;
            seen[bi] = seenStamp;
            const Body& b = bodies[bi];
            // nearest sample segment
            float bestD = 1e9f, bestX = 0.f, bestLat = 0.f, bestZ = 0.f, bestShift = 0.f;
            int bestPath = -1;
            vec2 bestT(0, 1);
            for (int k = 0; k + 1 < ns; k++) {
                vec2 a = smp[k].p, c = smp[k + 1].p;
                vec2 ab = c - a;
                float l2 = length2(ab);
                float t = l2 > 1e-6f ? Clamp(dot(b.pos - a, ab) / l2, 0.f, 1.f) : 0.f;
                vec2 q = a + ab * t;
                float dd = length2(b.pos - q);
                if (dd < bestD) {
                    bestD = dd;
                    bestX = Lerp(smp[k].x, smp[k + 1].x, t);
                    vec2 tt = l2 > 1e-6f ? ab / sqrtf(l2) : smp[k].t;
                    bestT = tt;
                    bestLat = dot(b.pos - q, rightOf(tt));
                    bestZ = Lerp(smp[k].z, smp[k + 1].z, t);
                    bestShift = Lerp(smp[k].shift, smp[k + 1].shift, t);
                    bestPath = smp[k].path;
                }
            }
            if (bestD > 1e8f) return;
            if (fabsf(b.z - bestZ) > 3.2f) return;   // different level (bridge / elevated highway)
            // bodies whose center is behind our origin are followers / beside us: never obstacles
            {
                float along0 = dot(b.pos - smp[0].p, smp[0].t);
                if (bestX <= smp[0].x + 1e-3f && along0 < 0.f) return;
                if (bestX < 0.5f) return;
            }
            vec2 bt = bestT, bn = rightOf(bt);
            vec2 bf = length2(b.fwd) > 0.5f ? b.fwd : vec2(0, 1);
            vec2 br = rightOf(bf);
            float extN = b.halfLen * fabsf(dot(bf, bn)) + b.halfWid * fabsf(dot(br, bn));
            float extT = b.halfLen * fabsf(dot(bf, bt)) + b.halfWid * fabsf(dot(br, bt));
            bool ped = b.kind == BK_PED;
            float margin = ped ? 0.75f : 0.3f + Min(v * 0.015f, 0.35f);
            float half = myHW + extN + margin;
            // a car parked in the parking strip (inner edge beyond the lane edge) is not in the way of a vehicle that
            // fits its lane - buses and trucks pass them daily with half a meter to spare
            if (!ped && (b.flags & BF_PARKED) && b.speed < 0.3f && bestPath >= 0) {
                int lp = bestPath < NL ? bestPath : G.conn(bestPath).to;
                float laneHalf = G.lanes[lp].width * 0.5f;
                if (fabsf(bestLat) - extN > laneHalf - 0.15f && myHW < laneHalf + 0.35f) half = myHW + extN + 0.08f;
            }
            // oncoming vehicles: judged against the planned path with a tight margin (they keep to their lane)
            if (!ped && dot(bf, bt) < -0.5f) {
                bestLat += bestShift;
                half = myHW + extN + 0.12f;
            }
            float gap = Max(bestX - extT - front, 0.f);
            float vl = dot(b.vel, bt);
            bool inside = fabsf(bestLat) < half;
            if (!inside) {
                // crossing prediction: will it be in our corridor when we get there?
                float vn = -dot(b.vel, bn) * (bestLat > 0.f ? 1.f : -1.f);
                if (vn < (ped ? 0.35f : 0.8f)) return;
                float te = (fabsf(bestLat) - half) / vn;
                float ta = gap / Max(v, 2.f);
                float window = ped ? 1.6f : 1.2f;
                if (te > ta + window || te > (ped ? 4.f : 3.f)) return;
                // it will pass before we arrive?
                float tLeave = (fabsf(bestLat) + half) / vn;
                if (tLeave < ta - 0.6f) return;
                vl = 0.f;
            }
            // oncoming traffic encroaching our lane: react as to a stopped obstacle
            if (vl < -1.f) vl = 0.f;
            // AI vehicles that yield to us inside an intersection do not block the approach
            if (gap < obstGap) {
                obstGap = gap;
                obstV = vl;
                obstB = bi;
                obstPed = ped;
            }
        });
    }
    d.obstDist = obstGap;
    d.obstSpeed = obstV;
    d.obstBody = obstB;
    // ---- stops
    float stopDist = 1e9f;
    if (d.path < NL) {
        const Lane& L = G.lanes[d.path];
        float distToEnd = L.u1 - (d.u + front);
        if (d.routeLen > 0 && d.route[0] >= NL) {
            int cid = d.route[0] - NL;
            if (distToEnd < lookDist + 10.f) stopDist = Min(stopDist, gate(d, cid, distToEnd, v, false));
            // preview the following intersection when the link in between is short
            if (d.routeLen > 2 && d.route[2] >= NL) {
                const Connector& c1 = G.conns[cid];
                int nextLane = c1.to;
                float d2 = distToEnd + c1.length + (G.lanes[nextLane].u1 - G.lanes[nextLane].u0);
                if (d2 < lookDist && d.mode == DM_NORMAL) {
                    const Connector& c2 = G.conn(d.route[2]);
                    SignalState s2 = G.movementSignal(c2.node, c2.approach, c2.turn, time);
                    const Lane& L2 = G.lanes[c2.from];
                    float line2 = d2 - (L2.stopU >= 0.f ? L2.u1 - L2.stopU : 0.6f);
                    if (s2 == SIG_RED || (G.nodes[c2.node].control == 1)) stopDist = Min(stopDist, line2 + (G.nodes[c2.node].control == 1 ? 0.f : 0.f));
                }
            }
        } else if (d.routeLen == 0) {
            stopDist = Min(stopDist, distToEnd - 0.5f);
        }
        laneChangeLogic(d, v, L.stopU >= 0.f ? distToEnd - (L.u1 - L.stopU) : distToEnd);
        // bus stops
        if (d.info.bus && d.mode == DM_NORMAL && d.busStopCooldown <= 0.f && L.right < 0) {
            for (float bu : L.busStops) {
                float dd = bu - (d.u + front) + 2.f;
                if (dd > -1.f && dd < lookDist) {
                    stopDist = Min(stopDist, dd);
                    if (dd < 1.5f && v < 0.4f) {
                        d.mode = DM_HOLD;
                        d.holdTimer = 9.f + hashToFloat(hash32(d.uid + (u32)bu)) * 8.f;
                        d.busStopCooldown = 40.f;
                    }
                    break;
                }
            }
        }
    } else {
        stopDist = Min(stopDist, gate(d, d.path - NL, -(d.u + front), v, true));
    }
    if (d.mode == DM_HOLD) stopDist = Min(stopDist, 0.f);
    // host-requested stop point on the route
    if (d.stopPath >= 0) {
        float acc = 0.f;
        if (d.stopPath == d.path) stopDist = Min(stopDist, d.stopU - (d.u + front));
        else {
            float rem = G.pathLength(d.path) - d.u;
            acc = rem;
            for (int i = 0; i < d.routeLen && acc < lookDist; i++) {
                int pth = d.route[i];
                float start = pth < NL ? G.lanes[pth].u0 : 0.f;
                if (pth == d.stopPath) {
                    stopDist = Min(stopDist, acc + (d.stopU - start) - front);
                    break;
                }
                acc += G.pathLength(pth) - start;
            }
        }
    }
    if (d.mode == DM_PULLOVER) {
        float remaining = fabsf(d.nudgeTarget - d.nudge);
        stopDist = Min(stopDist, Max(0.f, remaining * 4.f + v * 0.5f));
    }
    // ---- sirens behind: slow down and move over
    if (d.mode == DM_NORMAL) {
        bool siren = false;
        float r = 45.f;
        hash.query(bodies, pos - vec2(r), pos + vec2(r), [&](int bi) {
            if (bi == selfBody) return;
            const Body& b = bodies[bi];
            if (!(b.flags & BF_SIREN)) return;
            if (selfBody >= 0 && fabsf(b.z - bodies[selfBody].z) > 3.2f) return;
            vec2 rel = b.pos - pos;
            if (dot(rel, fwd) > -3.f || length2(rel) > r * r) return;
            if (dot(b.fwd, fwd) < 0.6f || b.speed < 3.f) return;
            siren = true;
        });
        if (siren && d.path < NL) {
            const Lane& L = G.lanes[d.path];
            if (L.right < 0) d.nudgeTarget = Min(1.6f, L.width * 0.5f);
            limit = Min(limit, 5.f);
            d.nudgeTimer = 3.f;
        }
    }
    // ---- speed target
    float vObst = 99.f;
    if (obstGap < 1e8f) {
        if (obstPed) {
            vObst = safeSpeed(obstGap, 0.f, 0.6f, P.decel * 1.2f, 2.2f);
            // someone dawdling in front of the bumper (not crossing at a crosswalk, not the player): after a honk,
            // inch forward - people step aside for a car that creeps at them
            const Body& pb = bodies[obstB];
            if (!(pb.flags & (BF_CROSSING | BF_PLAYER)) && obstGap < 3.f && v < 1.5f) {
                if (d.blockedTime > P.patience + 1.f && d.pedCreep <= 0.f) d.pedCreep = 3.f;
                if (d.pedCreep > 0.f) vObst = Max(vObst, 0.9f);
            }
        } else {
            vObst = safeSpeed(obstGap, obstV, P.headway, P.decel, P.minGap);
        }
    }
    float vStop = stopDist < 1e8f ? safeSpeed(stopDist, 0.f, 0.25f, P.decel * 1.1f, 0.35f) : 99.f;
    d.stopDist = stopDist;
    d.curveSpeed = vCurve;
    float vt = Min(Min(limit, vCurve), Min(vLimAhead, Min(vObst, vStop)));
    vt = Min(vt, d.speedCap);
    d.vTarget = Max(0.f, vt);
    // waiting bookkeeping (deadlock handling)
    if (v < 0.5f && d.vTarget < 0.5f) d.waitTime += kPlanInterval;
    else if (v > 2.f) d.waitTime = 0.f;
    (void)s;
    (void)dt;
}

float TrafficCore::maxSteerAt(const Driver& d, float v) const {
    float aLat = 1.25f * Clamp(d.info.grip, 0.5f, 1.6f) * 9.81f;
    float geo = atanf(d.info.wheelbase * aLat / Max(v * v, 1e-3f));
    return Min(d.info.maxSteer, geo + d.info.alphaPeak * 1.1f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Low-level control
void TrafficCore::control(Driver& d, const Vehicles::VehicleState& s, vec2 pos, vec2 fwd, float v, float dt, DriveOut& out) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    Vehicles::VehicleControls& c = out.ctl;
    c = Vehicles::VehicleControls();
    float vF = s.forwardSpeed();
    // ---- lane change / nudge lateral offsets
    if (d.lcLane >= 0 && d.path < NL) {
        float f = Saturate((d.u - d.lcU0) / Max(d.lcLen, 1.f));
        float w = f * f * (3.f - 2.f * f);
        d.lat = Lerp(d.lcFrom, d.lcTo, w);
        if (f >= 0.5f && d.path != d.lcLane && G.lanes[d.lcLane].group == G.lanes[d.path].group) {
            d.path = d.lcLane;
            d.lcFrom -= d.lcTo;
            d.lat -= d.lcTo;
            d.lcTo = 0.f;
            // re-evaluate the route from the new lane
            if (d.routeLen > 0 && d.route[0] >= NL && G.conn(d.route[0]).from != d.path) {
                bool reachable = G.lanes[G.conn(d.route[0]).from].group == G.lanes[d.path].group;
                if (!reachable) {
                    d.routeLen = 0;
                    planRoute(d);
                }
            }
        }
        if (f >= 1.f) {
            d.lat = 0.f;
            d.lcLane = -1;
            d.lcCooldown = 2.5f;
            d.indicator = 0;
        }
    }
    d.nudgeTimer -= dt;
    if (d.nudgeTimer <= 0.f && d.mode != DM_PULLOVER) d.nudgeTarget = 0.f;
    d.nudge = approach(d.nudge, d.nudgeTarget, dt * Clamp(v * 0.25f, 0.6f, 2.0f));
    // planned lateral offset (lane change profile) at lane coordinate uq of the current path, and its slope d(lat)/du
    auto plannedLat = [&](float uq, float* slope) -> float {
        if (d.lcLane < 0 || d.path >= NL) {
            if (slope) *slope = 0.f;
            return d.lat;
        }
        float len = Max(d.lcLen, 1.f);
        float f = Saturate((uq - d.lcU0) / len);
        if (slope) *slope = (f > 0.f && f < 1.f) ? (d.lcTo - d.lcFrom) * 6.f * f * (1.f - f) / len : 0.f;
        return Lerp(d.lcFrom, d.lcTo, f * f * (3.f - 2.f * f));
    };
    // ---- steering: pure pursuit from the rear axle
    float Ld = Clamp(2.6f + 0.36f * v, 4.f, 24.f);
    float ahead = Ld + d.info.rearAxleY;  // rear axle is behind the origin (rearAxleY < 0)
    vec2 target;
    {
        float acc = 0.f;
        int path = d.path;
        float u = d.u;
        int ri = -1;
        target = G.pathPos(path, u + ahead, d.lat + d.nudge).xy();
        while (true) {
            float end = path < NL ? G.lanes[path].u1 : G.conn(path).length;
            float rem = end - u;
            if (acc + rem >= ahead || ri + 1 >= d.routeLen) {
                float uu = u + (ahead - acc);
                float lat = path == d.path ? plannedLat(uu, nullptr) + d.nudge : 0.f;
                if (path < NL) uu = Min(uu, G.lanes[path].u1 + 6.f);
                target = G.pathPos(path, uu, lat).xy();
                break;
            }
            acc += Max(rem, 0.f);
            ri++;
            path = d.route[ri];
            u = path < NL ? G.lanes[path].u0 : 0.f;
        }
    }
    vec2 rear = pos + fwd * d.info.rearAxleY;
    vec2 rgt = rightOf(fwd);
    vec2 dl = target - rear;
    float lx = dot(dl, rgt), ly = dot(dl, fwd);
    float L2 = Max(lx * lx + ly * ly, 1.f);
    float kappa = 2.f * lx / L2;                 // + = curve to the right
    float delta = atanf(d.info.wheelbase * kappa);
    // Stanley tracking of the front axle: keeps the nose on the path through tight turns (the rear cuts inside, toward
    // the curb) instead of swinging the front wide into oncoming lanes. Long wheelbases use it at every speed, cars
    // blend back to pure pursuit (smoother on lane changes and fast curves) above ~10 m/s.
    bool longWB = d.info.wheelbase > 3.3f;
    float wSt = d.info.bike ? 0.f : (longWB ? 1.f : 1.f - SmoothStep(9.f, 13.f, vF));
    if (wSt > 0.f) {
        // path position `dist` meters ahead of the model origin along the planned route
        auto along = [&](float dist, int& pOut, float& uOut) {
            int path = d.path;
            float u = d.u + dist;
            for (int ri = 0; ri < d.routeLen; ri++) {
                float end = G.pathLength(path);
                if (u <= end) break;
                u -= end;
                path = d.route[ri];
                u += path < NL ? G.lanes[path].u0 : 0.f;
            }
            pOut = path;
            uOut = u;
        };
        int fp;
        float fu;
        along(d.info.frontAxleY, fp, fu);
        vec2 front = pos + fwd * d.info.frontAxleY;
        // closest path point to the front axle (the arc-length guess drifts in tight turns)
        float latRaw = 0.f;
        float fuP = G.projectPath(fp, front, fu, &latRaw);
        {
            float lo = fp < NL ? G.lanes[fp].u0 : 0.f, hi = G.pathLength(fp);
            if (fp == d.path && fuP >= hi - 0.01f && d.routeLen > 0) {
                int np = d.route[0];
                float l2 = 0.f;
                float u2 = G.projectPath(np, front, np < NL ? G.lanes[np].u0 : 0.f, &l2);
                fp = np;
                fuP = u2;
                latRaw = l2;
            } else if (fp != d.path && fuP <= lo + 0.01f) {
                float l2 = 0.f;
                float u2 = G.projectPath(d.path, front, G.pathLength(d.path), &l2);
                fp = d.path;
                fuP = u2;
                latRaw = l2;
            }
        }
        float slope = 0.f;
        float latF = fp == d.path ? plannedLat(fuP, &slope) + d.nudge : 0.f;
        float e = latRaw - latF;                                  // + = front right of the planned line
        vec2 tf = G.pathTangent(fp, fuP);
        // desired heading: the path tangent turned by the lane-change slope (+ lateral = right = clockwise)
        float psi = atan2f(cross(fwd, tf), dot(fwd, tf)) - atanf(slope);   // + = planned line heads left of us
        float vs = Max(vF, 0.f);
        // curvature feed-forward with a short preview (steering actuator lag), full steady-state front-axle angle
        int pp;
        float pu;
        along(d.info.frontAxleY + Clamp(vs * 0.22f, 0.3f, 3.f), pp, pu);
        float kPrev = G.pathCurv(pp, pu);                         // + = path turns left
        float kPath = G.pathCurv(fp, fuP);
        // (the heading term already carries the steady-state angle: the path tangent at the front axle is rotated by
        //  the steering angle relative to the body; the feed-forward only covers the actuator lag at turn entry)
        float ff = asinf(Clamp(d.info.wheelbase * kPrev, -0.95f, 0.95f)) - asinf(Clamp(d.info.wheelbase * G.pathCurv(fp, fuP), -0.95f, 0.95f));
        float st = -(psi + atanf(2.2f * e / (vs + 1.5f))) - ff;
        // yaw damping against oscillation at speed
        st += (s.body.angVel.z - vs * kPath) * 0.06f;
        d.diag[0] = e;
        d.diag[1] = psi;
        d.diag[2] = ff;
        d.diag[3] = st;
        d.diag[4] = (float)fp;
        d.diag[5] = fuP;
        delta = Lerp(delta, st, wSt);
    }
    if (ly < 0.f && vF > -0.5f && d.info.wheelbase <= 3.3f) delta = lx > 0.f ? d.info.maxSteer : -d.info.maxSteer;  // target behind: full lock toward it
    float lim = maxSteerAt(d, fabsf(vF));
    c.steer = Clamp(delta / Max(lim, 0.05f), -1.f, 1.f);
    // ---- longitudinal
    // dead-reckon the binding distances between plans
    float vt = d.vTarget;
    if (d.stopDist < 1e8f) {
        float sd = d.stopDist;
        vt = Min(vt, safeSpeed(sd, 0.f, 0.25f, d.pers.decel * 1.1f, 0.35f));
    }
    float err = vt - v;
    float aCmd = Clamp(err * 1.6f, -8.5f, d.pers.accel * (d.mode == DM_FLEE ? 2.f : 1.f));
    // required deceleration for the stop point
    if (d.stopDist < 1e8f && d.stopDist < v * v / (2.f * d.pers.decel) + 3.f) {
        float req = v * v / (2.f * Max(d.stopDist - 0.3f, 0.15f));
        aCmd = Min(aCmd, -req);
    }
    if (d.obstDist < 1e8f && v > d.obstSpeed) {
        float gapNow = d.obstDist - d.pers.minGap * 0.6f;
        float dv = v - Max(d.obstSpeed, 0.f);
        if (gapNow < dv * dv / (2.f * d.pers.decel) + 1.f) aCmd = Min(aCmd, -dv * dv / (2.f * Max(gapNow, 0.3f)));
    }
    bool hold = vt < 0.25f && v < 0.9f;
    if (hold) {
        c.handbrake = true;
        c.brake = 1.f;
        c.throttle = 0.f;
        d.integ = 0.f;
    } else if (aCmd > 0.05f) {
        // throttle from the wanted acceleration relative to what this car can do (power- or grip-limited),
        // plus drag/rolling resistance; the integral term absorbs gearing, slopes and model differences
        float aCap = Min(d.info.powerW * 0.75f / (d.info.mass * Max(v, 3.f)), 0.8f * 9.81f * Clamp(d.info.grip, 0.5f, 1.5f));
        float resist = 0.12f + d.info.dragK * v * v;
        // (pulling away, a weak launch - a scooter's clutch, a loaded van uphill - may take a much firmer foot)
        d.integ = Clamp(d.integ + err * (v < 3.f ? 0.1f : 0.05f) * dt, -0.15f, Lerp(0.7f, 0.35f, Saturate((v - 2.f) * 0.33f)));
        c.throttle = Clamp((aCmd + resist) / Max(aCap, 0.5f) + d.integ, 0.f, 1.f);
        // traction assist: back off when the driven wheels spin
        float slip = 0.f;
        for (int w = 0; w < s.wheelCount; w++)
            if (s.wheels[w].contact) slip = Max(slip, s.wheels[w].slip);
        if (slip > 0.3f) c.throttle *= Clamp(1.3f - slip, 0.3f, 1.f);
        if (vF < -0.5f) {  // rolling backwards while wanting to go forward
            c.throttle = 0.f;
            c.handbrake = true;
            c.brake = 1.f;
        }
    } else if (aCmd < -0.35f) {
        d.integ = Max(0.f, d.integ - dt * 0.5f);
        c.brake = Clamp(-aCmd / 8.f, 0.f, 1.f);
        if (v < 1.2f) c.handbrake = true;     // never let the brake pedal engage reverse
    } else {
        d.integ *= 1.f - dt;
    }
    out.brakeLights = c.brake > 0.1f || (hold && v < 0.2f);
    // ---- stuck detection and recovery (reverse a little, then replan)
    // (also when a few meters short of a stop line: a car wedged against another after a scrape never gets there)
    bool wants = d.vTarget > 1.f && d.obstDist > 4.f && d.stopDist > 2.f;
    if (d.recoverTimer > 0.f) {
        d.recoverTimer -= dt;
        c = Vehicles::VehicleControls();
        if (d.recoverTimer > 0.4f) {
            // reverse: brake pedal engages reverse at standstill and then drives backwards
            c.brake = 0.7f;
            c.steer = (float)d.recoverDir;
            // something (or someone) close behind: stop backing up
            if (vF < 0.3f && !rearClear(d, pos, fwd, 1.2f)) d.recoverTimer = 0.4f;
        } else {
            c.handbrake = true;   // stop and shift back to drive
            c.brake = 1.f;
        }
        if (d.recoverTimer <= 0.f) {
            if (!d.kturn) relocalize(d, pos, fwd, 20.f);   // a three-point turn keeps its path
            d.kturn = false;
            d.stuckTime = 0.f;
            d.stuckAnchor = pos;
        }
        return;
    }
    // three-point turn: on a turn tighter than this vehicle can steer (a dead-end turning circle for a long wheelbase,
    // a hairpin entered too fast) the nose runs wide at full lock; stop, back up on opposite lock, then go on
    if (d.path >= NL && fabsf(c.steer) > 0.97f && vF < 5.f && vF > -0.5f && d.vTarget > 0.5f && d.obstDist > 3.f && d.kturns < 5) {
        float kP = G.pathCurv(d.path, d.u);
        float outward = kP > 0.03f ? d.latErr : (kP < -0.03f ? -d.latErr : 0.f);   // + = outside of the turn
        d.kturnT = outward > 1.2f ? d.kturnT + dt : 0.f;
        if (d.kturnT > 0.35f && rearClear(d, pos, fwd, 2.5f)) {
            d.recoverTimer = 2.4f;
            d.recoverDir = c.steer > 0.f ? -1 : 1;   // opposite lock: backing up keeps turning the nose the same way
            d.kturn = true;
            d.kturns++;
            d.kturnT = 0.f;
            stats.kTurns++;
            return;
        }
    } else {
        d.kturnT = 0.f;
    }
    // stuck: wanting to go and pushing, but not getting anywhere (measured by displacement: a car wedged against a
    // kerb or a bollard jitters, and its wheels spin, so speed and throttle alone flicker in and out of the test)
    if (wants && c.throttle > 0.2f) {
        if (length2(pos - d.stuckAnchor) > 0.8f * 0.8f) {
            d.stuckAnchor = pos;
            d.stuckTime = Max(0.f, d.stuckTime - 1.f);
        } else {
            d.stuckTime += dt;
        }
    } else if (!wants) {
        d.stuckAnchor = pos;
        d.stuckTime = Max(0.f, d.stuckTime - dt * 2.f);
    }
    if (d.stuckTime > 3.5f) {
        d.recoverTimer = 2.2f;
        d.recoverDir = c.steer > 0.f ? -1 : 1;
        d.kturn = false;
        d.stuckTime = 0.f;
        d.stuckAnchor = pos;
        stats.stuckEvents++;
        out.stuck = true;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
void TrafficCore::drive(int vid, Vehicles::VehicleState& s, float dt, DriveOut& out) {
    out = DriveOut();
    Driver* dp = get(vid);
    if (!dp) return;
    Driver& d = *dp;
    // stale driver (the vehicle was driven by a script or the player meanwhile): relocalize
    if (d.lastDriveTime >= 0.f && time - d.lastDriveTime > 1.0 && !d.dummy) {
        relocalize(d, s.body.pos.toVec3().xy(), s.forward().xy(), 30.f);
        d.recoverTimer = 0.f;
        d.stuckTime = 0.f;
    }
    d.lastDriveTime = (float)time;
    d.lcCooldown = Max(0.f, d.lcCooldown - dt);
    d.pedCreep = Max(0.f, d.pedCreep - dt);
    d.busStopCooldown = Max(0.f, d.busStopCooldown - dt);
    d.destRecalc -= dt;
    if (d.path < 0) {
        vec2 fwd0 = s.forward().xy();
        if (!relocalize(d, s.body.pos.toVec3().xy(), fwd0, 30.f)) {
            out.ctl.handbrake = true;
            out.ctl.brake = 1.f;
            d.lostTime += dt;
            return;
        }
    }
    if (d.dummy) {
        dummyStep(d, s, dt);
        out.ctl = Vehicles::VehicleControls();
        out.ctl.handbrake = true;
        out.indicator = 0;
        return;
    }
    vec3 p3 = s.body.pos.toVec3();
    vec2 pos = p3.xy();
    vec2 fwd = s.forward().xy();
    if (length2(fwd) < 1e-4f) fwd = vec2(0, 1);
    fwd = normalize(fwd);
    float v = Max(s.forwardSpeed(), 0.f);
    localize(d, s);
    // off-route detection (pushed away, spun, jumped the curb)
    float headingDot = dot(fwd, g->pathTangent(d.path, d.u));
    if (fabsf(d.latErr) > 6.f || (headingDot < 0.2f && v > 2.f)) d.offRouteTime += dt;
    else d.offRouteTime = 0.f;
    if (d.offRouteTime > 1.5f) {
        d.offRouteTime = 0.f;
        if (!relocalize(d, pos, fwd, 25.f)) d.lostTime += 1.5f;
    }
    // flipped / wrecked
    if (s.up().z < 0.35f) d.flipTime += dt;
    else d.flipTime = 0.f;
    if (d.flipTime > 3.f) out.stuck = true;
    // hold / pull-over timers
    if (d.mode == DM_HOLD || d.mode == DM_PULLOVER) {
        if (d.holdTimer > 0.f) {
            d.holdTimer -= dt;
            if (d.holdTimer <= 0.f) {
                d.mode = DM_NORMAL;
                d.nudgeTarget = 0.f;
            }
        }
    }
    if (d.mode == DM_PULLOVER && d.path < (int)g->lanes.size()) {
        const Lane& L = g->lanes[d.path];
        d.nudgeTarget = L.right < 0 ? L.width * 0.5f + 0.6f : 0.f;
        d.nudgeTimer = 1.f;
    }
    d.planTimer -= dt;
    if (d.planTimer <= 0.f) {
        d.planTimer += kPlanInterval;
        if (d.planTimer < 0.f) d.planTimer = kPlanInterval;
        plan(d, s, pos, fwd, v, dt);
    } else {
        // dead reckoning of the binding distances between plans
        if (d.stopDist < 1e8f) d.stopDist -= v * dt;
        if (d.obstDist < 1e8f) d.obstDist -= (v - d.obstSpeed) * dt;
    }
    if (d.yieldHold > 0.f) {
        // backed off from a nose-to-nose block: wait for the other vehicle to get through
        d.yieldHold -= dt;
        d.vTarget = 0.f;
    }
    control(d, s, pos, fwd, v, dt, out);
    // statistics: lane-center error on straight-ish lane driving
    if (d.path < (int)g->lanes.size() && d.lcLane < 0 && d.nudge == 0.f && v > 3.f && d.recoverTimer <= 0.f) {
        d.statErr2 += (double)d.latErr * d.latErr;
        d.statErrN++;
    }
    // indicators: lane changes and upcoming turns
    int ind = d.indicator;
    if (d.lcLane < 0 && d.path < (int)g->lanes.size() && d.routeLen > 0 && d.route[0] >= (int)g->lanes.size()) {
        const Lane& L = g->lanes[d.path];
        const Connector& c = g->conn(d.route[0]);
        float dist = L.u1 - d.u;
        if (dist < 40.f && (c.turn == TK_LEFT || c.turn == TK_UTURN)) ind = -1;
        else if (dist < 40.f && c.turn == TK_RIGHT) ind = 1;
        else if (d.indicator != 0 && d.lcLane < 0) ind = d.indicator;
        else ind = 0;
    } else if (d.path >= (int)g->lanes.size()) {
        const Connector& c = g->conn(d.path);
        ind = c.turn == TK_LEFT || c.turn == TK_UTURN ? -1 : (c.turn == TK_RIGHT ? 1 : 0);
    }
    if (d.mode == DM_PULLOVER) ind = 1;
    out.indicator = ind;
    if (d.lcLane < 0 && ind == 0) d.indicator = 0;
    // blocked by a stopped obstacle (not a queue at a light): honk / overtake
    out.blocker = -1;
    if (d.obstBody >= 0 && d.obstDist < 12.f && d.obstSpeed < 0.5f && v < 0.5f && d.stopDist > d.obstDist + 2.f) {
        const Body& b = bodies[d.obstBody];
        bool queue = b.driver >= 0 && b.driver < (int)drivers.size() && drivers[b.driver].active &&
                     (drivers[b.driver].vTarget < 0.5f && (drivers[b.driver].stopDist < 30.f || drivers[b.driver].obstDist < 12.f)) && !(b.flags & BF_PARKED);
        if (!queue) {
            d.blockedTime += dt;
            out.blocker = d.obstBody;
        } else d.blockedTime = Max(0.f, d.blockedTime - dt);
    } else {
        d.blockedTime = Max(0.f, d.blockedTime - dt * 2.f);
    }
    // overtake a static blocker through the adjacent/oncoming lane
    bool staticBlocker = false;
    if (d.obstBody >= 0) {
        const Body& ob = bodies[d.obstBody];
        if (ob.flags & (BF_PARKED | BF_WRECK | BF_PLAYER)) staticBlocker = true;
        else if (ob.driver >= 0 && ob.driver < (int)drivers.size() && drivers[ob.driver].active) {
            const Driver& od = drivers[ob.driver];
            // an AI car that is itself blocked by something static (breakdown, flipped, abandoned) - not a queue
            staticBlocker = od.vTarget < 0.3f && od.stopDist > 40.f && od.obstDist > 25.f && ob.speed < 0.2f;
        } else if (ob.kind == BK_CAR && ob.driver < 0) staticBlocker = true;   // unmanaged vehicle (abandoned)
    }
    float toEnd = d.path < (int)g->lanes.size() ? g->lanes[d.path].u1 - d.u : 0.f;
    if (staticBlocker && toEnd > 30.f && d.blockedTime > d.pers.patience + 1.5f && d.nudgeTarget == 0.f && d.path < (int)g->lanes.size() && d.lcLane < 0) {
        const Lane& L = g->lanes[d.path];
        int side = L.left >= 0 ? -1 : (L.right >= 0 ? 1 : -1);
        float shift = L.width * (float)side;
        // is the space beside the blocker free (including oncoming traffic)?
        bool clear = true;
        vec2 t = g->laneTangent(d.path, d.u), rn = rightOf(t);
        float range = 70.f;
        int selfBody = driverBody(vid);
        hash.query(bodies, pos - vec2(range), pos + vec2(range), [&](int bi) {
            if (bi == selfBody || bi == d.obstBody || !clear) return;
            const Body& b = bodies[bi];
            if (fabsf(b.z - p3.z) > 3.2f) return;
            vec2 rel = b.pos - pos;
            float along = dot(rel, t), lat = dot(rel, rn) - shift;
            if (fabsf(lat) > b.halfWid + d.info.halfWid + 0.6f) {
                // cars in our own lane just beyond the blocker: no room to merge back
                float lat0 = dot(rel, rn);
                if (fabsf(lat0) < 1.6f && along > d.obstDist + 2.f && along < d.obstDist + 22.f) clear = false;
                return;
            }
            if (along > -8.f && along < 30.f) clear = false;
            if (along >= 30.f && along < range && dot(b.vel, t) < -1.f) clear = false;  // oncoming
        });
        if (clear) {
            d.nudgeTarget = shift;
            d.nudgeTimer = 7.f;
            d.blockedTime = 0.f;
        }
    }
    // on a junction connector (no lane to borrow): squeeze past a parked car sticking out just beyond the junction
    if (d.path >= (int)g->lanes.size() && d.obstBody >= 0 && (bodies[d.obstBody].flags & BF_PARKED) && d.blockedTime > d.pers.patience + 1.f &&
        d.nudgeTarget == 0.f) {
        const Body& ob = bodies[d.obstBody];
        vec3 cp = g->pathPos(d.path, d.u);
        vec2 ct = g->pathTangent(d.path, d.u);
        float lat = dot(ob.pos - cp.xy(), rightOf(ct));
        float need = d.info.halfWid + ob.halfWid + 0.2f - fabsf(lat);
        if (need > 0.f && need < 1.0f) {
            d.nudgeTarget = (lat > 0.f ? -1.f : 1.f) * need;
            d.nudgeTimer = 5.f;
            d.blockedTime = 0.f;
        }
    }
    // honking: blocked by something that is not a normal queue
    d.honkTimer -= dt;
    if (d.hornHold > 0.f) {
        d.hornHold -= dt;
        out.horn = true;
    }
    if (out.blocker >= 0 && d.blockedTime > d.pers.patience && d.honkTimer <= 0.f) {
        d.hornHold = 0.35f + hashToFloat(hash32(d.uid + frame)) * 0.6f;
        d.honkTimer = 2.5f + hashToFloat(hash32(d.uid * 3u + frame)) * 4.f;
    }
    if (d.mode == DM_FLEE && d.blockedTime > 2.5f) out.wantsAbandon = true;
    // two vehicles blocking each other (nose to nose after a wide turn): the later/higher-id one backs up
    bool mutual = false;
    if (d.obstBody >= 0 && d.obstDist < 3.f && v < 0.4f && d.recoverTimer <= 0.f) {
        const Body& ob = bodies[d.obstBody];
        if (ob.driver >= 0 && ob.driver < (int)drivers.size() && drivers[ob.driver].active) {
            const Driver& od = drivers[ob.driver];
            int myBody = driverBody(vid);
            mutual = od.obstBody == myBody && od.obstDist < 3.f && ob.speed < 0.4f;
        }
    }
    d.mutualTime = mutual ? d.mutualTime + dt : 0.f;
    if (d.mutualTime > 3.f) {
        const Body& ob = bodies[d.obstBody];
        const Driver& od = drivers[ob.driver];
        bool iYield = od.mutualTime < d.mutualTime - 0.5f ? false : d.uid > od.uid;
        if (iYield || d.mutualTime > 8.f) {
            // is there room behind?
            bool room = true;
            int myBody = driverBody(vid);
            hash.query(bodies, pos - vec2(12.f), pos + vec2(12.f), [&](int bi) {
                if (bi == myBody || !room) return;
                const Body& b = bodies[bi];
                vec2 rel = b.pos - pos;
                float along = dot(rel, fwd), lat = dot(rel, rightOf(fwd));
                if (along < -d.info.halfLen - 0.5f && along > -d.info.halfLen - b.halfLen - 6.5f && fabsf(lat) < d.info.halfWid + b.halfWid + 0.3f) room = false;
            });
            if (room) {
                // back off properly (not just a car length) and stay back until the other one has gone through
                d.recoverTimer = 2.8f;
                d.recoverDir = 0;
                d.mutualTime = 0.f;
                d.yieldHold = 6.8f;   // (counts down during the 2.8 s reverse as well)
                stats.deadlockBreaks++;
            } else if (d.mutualTime > 8.f && (d.uid > od.uid || d.mutualTime > 14.f) && d.nudgeTimer <= 0.f) {
                // boxed in from behind as well: squeeze past the other car on its free side
                float side = dot(ob.pos - pos, rightOf(fwd)) >= 0.f ? -1.f : 1.f;
                d.nudgeTarget = side * Min(d.info.halfWid + ob.halfWid + 0.5f, 2.6f);
                d.nudgeTimer = 5.f;
                d.mutualTime = 0.f;
                stats.deadlockBreaks++;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Kinematic dummy mode
void TrafficCore::dummyStep(Driver& d, Vehicles::VehicleState& s, float dt) {
    const LaneGraph& G = *g;
    const int NL = (int)G.lanes.size();
    d.planTimer -= dt;
    if (d.planTimer <= 0.f) {
        d.planTimer += kDummyPlanInterval;
        if (d.planTimer < 0.f) d.planTimer = kDummyPlanInterval;
        // mandatory lane changes are instantaneous for dummies
        if (d.path < NL && d.routeLen > 0 && d.route[0] >= NL) {
            int from = G.conn(d.route[0]).from;
            if (from != d.path && G.lanes[from].group == G.lanes[d.path].group) d.path = from;
        }
        d.lcLane = -1;
        d.lat = 0.f;
        d.nudge = d.nudgeTarget = 0.f;
        vec3 p = G.pathPos(d.path, d.u);
        vec2 t = G.pathTangent(d.path, d.u);
        plan(d, s, p.xy(), t, d.vDummy, dt);
    } else {
        if (d.stopDist < 1e8f) d.stopDist -= d.vDummy * dt;
        if (d.obstDist < 1e8f) d.obstDist -= (d.vDummy - d.obstSpeed) * dt;
    }
    float vt = d.vTarget;
    if (d.stopDist < 1e8f) vt = Min(vt, safeSpeed(d.stopDist, 0.f, 0.25f, d.pers.decel, 0.35f));
    if (d.obstDist < 1e8f) vt = Min(vt, safeSpeed(d.obstDist, d.obstSpeed, d.pers.headway, d.pers.decel, d.pers.minGap));
    float a = vt > d.vDummy ? d.pers.accel : d.pers.decel * 1.5f;
    d.vDummy = approach(d.vDummy, vt, a * dt);
    d.u += d.vDummy * dt;
    for (int k = 0; k < 3; k++) {
        float len = d.path < NL ? G.lanes[d.path].u1 : G.conn(d.path).length;
        if (d.u <= len) break;
        if (d.routeLen == 0) {
            d.u = len;
            d.vDummy = 0.f;
            break;
        }
        advancePath(d);
    }
    vec3 p = G.pathPos(d.path, d.u);
    vec2 t = G.pathTangent(d.path, d.u);
    s.body.pos = dvec3(p.x, p.y, p.z);
    s.body.rot = quatAxisAngle(vec3(0, 0, 1), dirYaw(t));
    s.body.vel = vec3(0.f);
    s.body.angVel = vec3(0.f);
    s.sleeping = true;
    s.sleepTimer = 0.f;
    s.wakeCheck = 0;
    s.restZ = p.z;
}

void TrafficCore::toDummy(int vid, Vehicles::VehicleState& s) {
    Driver* d = get(vid);
    if (!d || d->dummy) return;
    d->dummy = true;
    d->vDummy = Max(s.forwardSpeed(), 0.f);
    d->lcLane = -1;
    d->lat = 0.f;
    d->nudge = d->nudgeTarget = 0.f;
    d->recoverTimer = 0.f;
    d->planTimer = 0.f;
}

void TrafficCore::toPhysics(int vid, Vehicles::VehicleState& s) {
    Driver* d = get(vid);
    if (!d || !d->dummy) return;
    d->dummy = false;
    vec3 p = g->pathPos(d->path, d->u);
    vec2 t = g->pathTangent(d->path, d->u);
    Vehicles::resetVehicle(s, dvec3(p.x, p.y, p.z + 0.2f), dirYaw(t));
    float v = d->vDummy;
    s.body.vel = vec3(t * v, 0.f);
    for (int w = 0; w < s.wheelCount; w++) {
        float r = s.model && w < (int)s.model->wheels.size() ? s.model->wheels[w].radius : 0.33f;
        s.wheels[w].spinVel = v / Max(r, 0.1f);
    }
    // pick a sensible gear for the current speed
    const Vehicles::VehicleTuning& tn = s.tune;
    int gear = 1;
    for (int gi = 1; gi <= tn.gears; gi++) {
        float rpm = v / Max(tn.driveRadius, 0.1f) * tn.ratio[gi] * 60.f / kTwoPi;
        gear = gi;
        if (rpm < tn.maxRpm * 0.62f) break;
    }
    s.gear = gear;
    s.pendingGear = gear;
    d->planTimer = 0.f;
    d->integ = 0.1f;
}

}  // namespace AI
