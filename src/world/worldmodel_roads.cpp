// The world model's roads compile (worldmodel.h): the model's nodes and edges to today's RoadNetwork.
#include "worldmodel.h"
#include <cstdio>
#include <cstring>

namespace World {
namespace Hand {

namespace worldmodel_roads_detail {

// FNV-1a over the id, then the engine's hash: an edge's seed follows its id, not its place in the files
u32 idSeed(const std::string& id) {
    u32 h = 2166136261u;
    for (unsigned char c : id) h = (h ^ c) * 16777619u;
    return hash32(h);
}

std::string xy(vec2 p) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f,%.2f", p.x, p.y);
    return buf;
}

std::string metres(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// The paved width beyond the lanes on one side: shoulder and parking as written (they add up), else the class's
float kerbSide(float shoulder, float parking, bool written, float classWidth) {
    return written ? Max(shoulder, 0.f) + Max(parking, 0.f) : classWidth;
}

float profileAt(const std::vector<vec2>& p, float s) {
    if (s <= p.front().x) return p.front().y;
    for (size_t k = 1; k < p.size(); k++)
        if (s <= p[k].x) return Lerp(p[k - 1].y, p[k].y, (s - p[k - 1].x) / (p[k].x - p[k - 1].x));
    return p.back().y;
}

}  // namespace worldmodel_roads_detail

bool compileRoads(const Model& model, RoadNetwork& out, RoadIndex& index, std::vector<Error>& errors) {
    using namespace worldmodel_roads_detail;
    const size_t errorsBefore = errors.size();
    out = RoadNetwork();
    index = RoadIndex();
    const auto fail = [&errors](const Src& s, const char* code, const std::string& msg) { errors.push_back(Error{s, code, msg}); };

    out.nodes.resize(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); i++) {
        out.nodes[i].p = model.nodes[i].at;
        out.nodes[i].z = model.nodes[i].z;
        index.nodeOf.push_back((int)i);
        index.roadNode.push_back((int)i);
    }
    index.roadEdge.assign(model.edges.size(), -1);
    for (size_t i = 0; i < model.edges.size(); i++) {
        const Edge& m = model.edges[i];
        const std::string what = "edge " + m.id;
        if (m.from.target < 0 || m.to.target < 0) {
            const Ref& r = m.from.target < 0 ? m.from : m.to;
            fail(r.src.file >= 0 ? r.src : m.src, "road.node", what + ": " + (m.from.target < 0 ? "from=" : "to=") + r.text + " names no node");
            continue;
        }
        if (m.from.target == m.to.target) {
            fail(m.src, "road.loop", what + ": from= and to= are the same node; a loop needs a node of its own along it");
            continue;
        }
        const Node& a = model.nodes[(size_t)m.from.target];
        const Node& b = model.nodes[(size_t)m.to.target];
        bool ok = m.shape.size() >= 2;
        for (size_t k = 1; ok && k < m.shape.size(); k++) ok = length(m.shape[k] - m.shape[k - 1]) > 0.005f;
        if (!ok) {
            fail(m.src, "road.shape", what + ": its shape needs at least 2 points, no two of them in one place");
            continue;
        }
        if (length(m.shape.front() - a.at) > 0.02f || length(m.shape.back() - b.at) > 0.02f) {
            fail(m.src, "road.shape", what + ": its shape runs from " + xy(m.shape.front()) + " to " + xy(m.shape.back()) + ", not from " + a.id + " (" + xy(a.at) +
                                          ") to " + b.id + " (" + xy(b.at) + ")");
            continue;
        }
        // heights by the distance along the shape in plan: the profile's, else straight from node to node. The points are
        // the shape's, and one more wherever the profile bends between two of them, so that the polyline carries the
        // profile (the road mesher and the grading run straight from point to point).
        std::vector<float> s(m.shape.size(), 0.f);
        for (size_t k = 1; k < m.shape.size(); k++) s[k] = s[k - 1] + length(m.shape[k] - m.shape[k - 1]);
        const float planLength = s.back();
        if (!m.profile.empty()) {
            bool rising = m.profile.size() >= 2;
            for (size_t k = 1; rising && k < m.profile.size(); k++) rising = m.profile[k].x > m.profile[k - 1].x;
            if (!rising || fabsf(m.profile.front().x) > 0.01f || fabsf(m.profile.back().x - planLength) > 0.5f) {
                fail(m.src, "road.profile", what + ": its profile's s should rise from 0 to the shape's length, " + metres(planLength) + " m");
                continue;
            }
            if (fabsf(m.profile.front().y - a.z) > 0.05f || fabsf(m.profile.back().y - b.z) > 0.05f) {
                fail(m.src, "road.profile", what + ": its profile should start at " + a.id + "'s z (" + metres(a.z) + ") and end at " + b.id + "'s (" + metres(b.z) + "), not " +
                                                metres(m.profile.front().y) + " and " + metres(m.profile.back().y));
                continue;
            }
        }
        std::vector<vec2> plan;
        std::vector<float> planS;
        for (size_t k = 0; k < m.shape.size(); k++) {
            plan.push_back(m.shape[k]);
            planS.push_back(s[k]);
            if (k + 1 == m.shape.size()) break;
            for (const vec2& pr : m.profile)
                if (pr.x > s[k] + 0.05f && pr.x < s[k + 1] - 0.05f) {
                    plan.push_back(lerp(m.shape[k], m.shape[k + 1], (pr.x - s[k]) / (s[k + 1] - s[k])));
                    planS.push_back(pr.x);
                }
        }
        std::vector<float> z(plan.size());
        for (size_t k = 0; k < z.size(); k++) z[k] = m.profile.empty() ? Lerp(a.z, b.z, planS[k] / planLength) : profileAt(m.profile, planS[k]);
        z.front() = a.z;   // (every edge meets its nodes at their height)
        z.back() = b.z;

        RoadEdge e;
        e.n0 = m.from.target;
        e.n1 = m.to.target;
        e.cls = m.cls;
        e.flags = m.flags;
        if (!m.decks.empty()) e.flags |= RF_BRIDGE;
        if (m.lanesF == 0 || m.lanesB == 0) e.flags |= RF_ONEWAY;
        if (m.cls == RC_DIRT) e.flags |= RF_UNPAVED;
        e.lanesF = (u8)m.lanesF;
        e.lanesB = (u8)m.lanesB;
        const RoadClassInfo& ri = roadInfo(m.cls);
        const float laneW = m.laneWidth > 0.f ? m.laneWidth : ri.laneWidth;
        const float median = m.median >= 0.f ? m.median : ri.median;
        const bool kerbWritten = m.shoulderL >= 0.f || m.parkingL >= 0.f;   // (a written compound sets both sides)
        const float kerbW = Max(kerbSide(m.shoulderL, m.parkingL, kerbWritten, ri.shoulder), kerbSide(m.shoulderR, m.parkingR, kerbWritten, ri.shoulder));
        e.halfWidth = laneW * (e.lanesF + e.lanesB) * 0.5f + median * 0.5f + kerbW;
        e.sidewalk = m.sidewalkL >= 0.f ? Max(m.sidewalkL, m.sidewalkR) : ri.sidewalk;
        if (e.sidewalk <= 0.f) e.flags |= RF_NOSIDEWALK;
        for (size_t k = 0; k < plan.size(); k++) e.pts.push_back(vec3(plan[k].x, plan[k].y, z[k]));
        e.dist.assign(e.pts.size(), 0.f);
        for (size_t k = 1; k < e.pts.size(); k++) e.dist[k] = e.dist[k - 1] + length(e.pts[k] - e.pts[k - 1]);
        e.length = e.dist.back();
        e.seed = idSeed(m.id);
        e.name = m.name;
        index.roadEdge[i] = (int)out.edges.size();
        index.edgeOf.push_back((int)i);
        out.edges.push_back(e);
    }
    for (size_t i = 0; i < out.edges.size(); i++) {
        out.nodes[(size_t)out.edges[i].n0].edges.push_back((int)i);
        out.nodes[(size_t)out.edges[i].n1].edges.push_back((int)i);
        if (out.edges[i].cls == RC_HIGHWAY) out.nodes[(size_t)out.edges[i].n0].highway = out.nodes[(size_t)out.edges[i].n1].highway = true;
    }
    out.finishJunctions();
    // control= where written. The lane graph knows three: priority by road class (0), stop signs on every approach (1)
    // and signals (2). A yield, or a stop on only some approaches, is priority until it reads the node's lists.
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const Node& n = model.nodes[i];
        RoadNode& rnode = out.nodes[i];
        if (n.control == Node::CTL_NONE || n.control == Node::CTL_YIELD) {
            rnode.control = 0;
        } else if (n.control == Node::CTL_SIGNAL) {
            rnode.control = 2;
        } else if (n.control == Node::CTL_STOP) {
            bool every = true;   // stops=all, no list, or every leg in it
            if (!n.stopsAll && !n.stops.empty())
                for (int ei : rnode.edges) {
                    bool listed = false;
                    for (const Ref& r : n.stops) listed = listed || r.target == index.edgeOf[(size_t)ei];
                    every = every && listed;
                }
            rnode.control = every ? 1 : 0;
        }
    }
    out.buildHash();
    return errors.size() == errorsBefore;
}

void compileRoadOverrides(const Model& model, const RoadIndex& index, const RoadNetwork& net, RoadOverrides& out) {
    using namespace worldmodel_roads_detail;
    out = RoadOverrides();
    if (net.edges.empty() || index.edgeOf.size() != net.edges.size()) return;
    out.edges.resize(net.edges.size());
    for (size_t i = 0; i < net.edges.size(); i++) {
        const Edge& m = model.edges[(size_t)index.edgeOf[i]];
        const RoadClassInfo& ri = roadInfo(m.cls);
        RoadOverrides::EdgeExtra& x = out.edges[i];
        x.speed = m.speed > 0.f ? m.speed : ri.speed;
        x.laneWidth = m.laneWidth > 0.f ? m.laneWidth : ri.laneWidth;
        x.median = m.median >= 0.f ? m.median : ri.median;
        // as compileRoads sizes the carriageway: a written shoulder= or parking= sets both sides, and then the parking
        // is written too (0 where the compound leaves it out)
        const bool kerbWritten = m.shoulderL >= 0.f || m.parkingL >= 0.f;
        x.kerbL = kerbSide(m.shoulderL, m.parkingL, kerbWritten, ri.shoulder);
        x.kerbR = kerbSide(m.shoulderR, m.parkingR, kerbWritten, ri.shoulder);
        x.parkingL = kerbWritten ? Max(m.parkingL, 0.f) : -1.f;
        x.parkingR = kerbWritten ? Max(m.parkingR, 0.f) : -1.f;
        x.sidewalkL = m.sidewalkL >= 0.f ? m.sidewalkL : ri.sidewalk;
        x.sidewalkR = m.sidewalkR >= 0.f ? m.sidewalkR : ri.sidewalk;
        x.flags = (u8)((m.medianBarrier ? RoadOverrides::EX_MEDIAN_BARRIER : 0) | (m.tunnel ? RoadOverrides::EX_TUNNEL : 0));
    }
    // the nodes' rules as written (the model's nodes are the network's, in order)
    static_assert((int)Node::CTL_AUTO == (int)RoadOverrides::CTRL_AUTO && (int)Node::CTL_NONE == (int)RoadOverrides::CTRL_NONE &&
                      (int)Node::CTL_STOP == (int)RoadOverrides::CTRL_STOP && (int)Node::CTL_YIELD == (int)RoadOverrides::CTRL_YIELD &&
                      (int)Node::CTL_SIGNAL == (int)RoadOverrides::CTRL_SIGNAL,
                  "a node's control= and RoadOverrides::NodeRule's are numbered alike");
    const auto roadEdges = [&](const std::vector<Ref>& refs, std::vector<int>& list) {
        for (const Ref& x : refs) {
            const int re = x.target >= 0 && (size_t)x.target < index.roadEdge.size() ? index.roadEdge[(size_t)x.target] : -1;
            if (re >= 0) list.push_back(re);
        }
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    };
    for (size_t i = 0; i < model.nodes.size() && i < index.roadNode.size(); i++) {
        const Node& n = model.nodes[i];
        if (n.control == Node::CTL_AUTO && !n.stopsAll && n.stops.empty() && n.yields.empty() && n.noRedTurn.empty() && n.bulb <= 0.f) continue;
        if (index.roadNode[i] < 0) continue;
        RoadOverrides::NodeRule r;
        r.node = index.roadNode[i];
        r.control = (u8)n.control;
        r.flags = (u8)(n.stopsAll || (n.control == Node::CTL_STOP && n.stops.empty()) ? RoadOverrides::NR_STOPS_ALL : 0);
        r.bulb = Max(n.bulb, 0.f);
        roadEdges(n.stops, r.stops);
        roadEdges(n.yields, r.yields);
        roadEdges(n.noRedTurn, r.noRedTurn);
        out.nodes.push_back(r);
    }
}

namespace worldmodel_roads_detail {

char compassOf(float bearing) { return bearing >= 315.f || bearing < 45.f ? 'n' : bearing < 135.f ? 'e' : bearing < 225.f ? 's' : 'w'; }

u8 turnBit(char t) { return t == 'l' ? TURN_LEFT : t == 's' ? TURN_STRAIGHT : t == 'r' ? TURN_RIGHT : t == 'u' ? TURN_UTURN : 0; }

}  // namespace worldmodel_roads_detail

NodeLegs nodeLegs(const Model& model, const RoadIndex& index, const RoadNetwork& net, int node) {
    using namespace worldmodel_roads_detail;
    NodeLegs N;
    N.node = node;
    if (node < 0 || (size_t)node >= model.nodes.size() || (size_t)node >= index.roadNode.size()) return N;
    const int rni = index.roadNode[(size_t)node];
    if (rni < 0 || (size_t)rni >= net.nodes.size()) return N;
    const RoadNode& rn = net.nodes[(size_t)rni];
    int highways = 0, ramps = 0;
    for (size_t k = 0; k < rn.edges.size(); k++) {
        const int re = rn.edges[k];
        const RoadEdge& e = net.edges[(size_t)re];
        int prior = 0;   // (a loop edge would be there twice: its start, then its end)
        for (size_t q = 0; q < k; q++) prior += rn.edges[q] == re;
        NodeLeg g;
        g.road = re;
        g.edge = (size_t)re < index.edgeOf.size() ? index.edgeOf[(size_t)re] : -1;
        g.outgoing = e.n0 == rni && (e.n1 != rni || prior == 0);
        g.in = g.outgoing ? e.lanesB : e.lanesF;
        g.out = g.outgoing ? e.lanesF : e.lanesB;
        const float cut = g.outgoing ? e.cut0 : e.cut1;
        const float probe = Clamp(Max(cut, 4.f), 0.f, e.length * 0.5f);
        vec2 d = e.posAt(g.outgoing ? probe : e.length - probe).xy() - rn.p;
        if (length2(d) < 1e-4f) d = (g.outgoing ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - rn.p;
        g.dir = normalize(d);
        g.bearing = atan2f(g.dir.x, g.dir.y) * kRadToDeg;
        if (g.bearing < 0.f) g.bearing += 360.f;
        g.compass = compassOf(g.bearing);
        g.letter = g.compass;
        highways += e.cls == RC_HIGHWAY;
        ramps += e.cls == RC_RAMP;
        N.legs.push_back(g);
    }
    N.interchange = highways > 0 && ramps > 0;
    N.gradeSeparated = N.legs.size() >= 4 && highways == (int)N.legs.size();
    // approaches=: a letter's first use and an edge's
    std::string given;
    std::vector<int> named;
    for (const auto& a : model.nodes[(size_t)node].approaches) {
        if (a.first.size() != 1 || !strchr("nesw", a.first[0]) || given.find(a.first[0]) != std::string::npos) continue;
        given += a.first[0];
        if (a.second.target < 0 || std::find(named.begin(), named.end(), a.second.target) != named.end()) continue;
        named.push_back(a.second.target);
        for (NodeLeg& g : N.legs)
            if (g.edge == a.second.target) g.letter = a.first[0];
    }
    return N;
}

u8 legTurn(const Model& model, const NodeLegs& N, const NodeLeg& a, const NodeLeg& b) {
    // a connect record joining the two edges gives its turn
    if (N.node >= 0 && (size_t)N.node < model.nodes.size() && model.nodes[(size_t)N.node].junction >= 0) {
        const Junction& j = model.junctions[(size_t)model.nodes[(size_t)N.node].junction];
        for (const Junction::Connect& c : j.connects)
            if (c.fromEdge.target == a.edge && c.toEdge.target == b.edge && a.edge >= 0)
                return c.turn == "left" ? TURN_LEFT : c.turn == "right" ? TURN_RIGHT : c.turn == "uturn" ? TURN_UTURN : TURN_STRAIGHT;
    }
    // as the lane graph classes it
    const vec2 tin = -a.dir, tout = b.dir;
    const float deg = atan2f(cross(tin, tout), dot(tin, tout)) * kRadToDeg;
    u8 t = TURN_STRAIGHT;
    if (N.legs.size() == 2) t = TURN_STRAIGHT;
    else if (fabsf(deg) > 140.f) return 0;
    else if (deg > 35.f) t = TURN_LEFT;
    else if (deg < -35.f) t = TURN_RIGHT;
    if (N.interchange) {
        if (fabsf(deg) > 60.f) return 0;
        t = TURN_STRAIGHT;   // (a merge or a diverge)
    }
    if (N.gradeSeparated && t != TURN_STRAIGHT) return 0;
    return t;
}

void resolvePlan(const Model& model, const NodeLegs& N, const SignalPlan& plan, ResolvedPlan& out) {
    using namespace worldmodel_roads_detail;
    out = ResolvedPlan();
    for (const Phase& ph : plan.phases) {
        ResolvedPlan::Phase rp;
        for (const std::string& mv : ph.moves) {
            const bool form = (mv.size() == 3 || (mv.size() == 4 && mv[3] == 'p')) && mv[1] == '_' && strchr("nesw", mv[0]) && turnBit(mv[2]);
            const u8 want = form ? turnBit(mv[2]) : 0;
            bool any = false;
            for (size_t ai = 0; form && ai < N.legs.size(); ai++) {
                const NodeLeg& a = N.legs[ai];
                if (a.letter != mv[0] || a.in <= 0) continue;
                for (size_t bi = 0; bi < N.legs.size(); bi++) {
                    const NodeLeg& b = N.legs[bi];
                    if (bi == ai || b.out <= 0 || legTurn(model, N, a, b) != want) continue;
                    ResolvedPlan::Move m;
                    m.from = a.road;
                    m.to = b.road;
                    m.turn = want;
                    m.isProtected = mv.size() == 4;
                    rp.moves.push_back(m);
                    any = true;
                }
            }
            if (!any) out.unused.push_back(mv);
        }
        for (const std::string& w : ph.walk) {
            bool any = false;
            for (const NodeLeg& g : N.legs)
                if (w.size() == 1 && g.letter == w[0]) {
                    rp.walks.push_back(g.road);
                    any = true;
                }
            if (!any) out.unused.push_back("walk " + w);
        }
        // one entry per movement (a protected one where any says so) and per crossing
        std::sort(rp.moves.begin(), rp.moves.end(), [](const ResolvedPlan::Move& x, const ResolvedPlan::Move& y) {
            return x.from != y.from ? x.from < y.from : x.to != y.to ? x.to < y.to : x.isProtected > y.isProtected;
        });
        rp.moves.erase(std::unique(rp.moves.begin(), rp.moves.end(), [](const ResolvedPlan::Move& x, const ResolvedPlan::Move& y) { return x.from == y.from && x.to == y.to; }),
                       rp.moves.end());
        std::sort(rp.walks.begin(), rp.walks.end());
        rp.walks.erase(std::unique(rp.walks.begin(), rp.walks.end()), rp.walks.end());
        out.phases.push_back(rp);
    }
}

}  // namespace Hand
}  // namespace World
