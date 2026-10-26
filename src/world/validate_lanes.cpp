// worldv's checks on the lane graph (validate_lanes.h). A build includes it after validate_roads.cpp, whose helpers it
// uses, and after src/game/lanes.cpp.
#include "validate_lanes.h"
#include "../game/ai_core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace World {
namespace Validate {

namespace lanes_check_detail {

using namespace World::Hand;
using namespace roads_detail;

// A movement at a node: from the edge the traffic arrives on to the edge it leaves on (model edges), with the lane
// graph's connectors that make it. A signal plan names it by its arrival leg's letter and its turn (n_l): the turn is a
// connect record's where one joins the two edges ("connect overrides them for odd geometry"), else the lane graph's
// class of the turn between the legs. Until the compiler resolves plans to edges itself, this is the stand-in.
struct Movement {
    int in = -1, out = -1;   // NodeLegs indices
    char from = 'n', turn = 's';
    std::vector<int> conns;
    std::string name() const { return std::string(1, from) + "_" + turn; }
};

struct NodeView {
    const NodeLegs* legs = nullptr;
    std::vector<char> letters;              // per leg
    const Junction* junction = nullptr;
    std::map<std::pair<int, int>, char> names;   // (in leg, out leg) -> its turn, for the pairs that may be movements
};

int legOf(const NodeLegs& N, int roadEdge, bool outgoing) {
    for (size_t i = 0; i < N.legs.size(); i++)
        if (N.legs[i].road == roadEdge && N.legs[i].outgoing == outgoing) return (int)i;
    return -1;
}

// The turn that takes traffic from leg a into leg b, as a plan names it
char turnName(const NodeView& v, int a, int b) {
    const Leg &A = v.legs->legs[(size_t)a], &B = v.legs->legs[(size_t)b];
    if (v.junction)
        for (const Junction::Connect& c : v.junction->connects)
            if (c.fromEdge.target == A.edge && c.toEdge.target == B.edge) return turnLetter(c.turn);
    if (a == b) return 'u';
    const char t = turnClass(*v.legs, A, B);
    return t ? t : 'u';   // (past 140 degrees: back round, a U-turn into the other road)
}

// Whether the junction's records let traffic go from leg a into leg b: not banned, connected under turns=custom, no
// U-turn under turns=no_uturn
bool allowedPair(const NodeView& v, int a, int b, char turn) {
    const Leg &A = v.legs->legs[(size_t)a], &B = v.legs->legs[(size_t)b];
    const Junction* j = v.junction;
    if (banned(j, A.edge, B.edge)) return false;
    if (j && j->turns == "custom") {
        bool connected = false;
        for (const Junction::Connect& c : j->connects) connected = connected || (c.fromEdge.target == A.edge && c.toEdge.target == B.edge);
        if (!connected) return false;
    }
    return !(j && j->turns == "no_uturn" && turn == 'u');
}

// The turns the moves record of the lanes arriving on leg a lists, all lanes together; empty without one
std::string movesAt(const Model& m, const NodeView& v, int a) {
    const Leg& A = v.legs->legs[(size_t)a];
    std::string all;
    for (const Moves& mv : m.edges[(size_t)A.edge].moves)
        if (mv.atEnd == (A.outgoing ? 0 : 1))   // (the lanes arriving at the edge's to end are its fwd lanes: at=n1)
            for (const std::string& t : mv.lanes) all += t;
    return all;
}

NodeView viewOf(const Model& m, const std::vector<NodeLegs>& legs, int node) {
    NodeView v;
    v.legs = &legs[(size_t)node];
    v.letters = legLetters(m.nodes[(size_t)node], *v.legs);
    v.junction = junctionOf(m, node);
    return v;
}

// The node's movements: the lane graph's connectors grouped by the edges they join, less what the junction's records
// and the moves records take away (the lane graph doesn't read them yet)
std::vector<Movement> movementsAt(const Model& m, const AI::LaneGraph& G, NodeView& v, int roadNode) {
    const AI::NodeInfo& N = G.nodes[(size_t)roadNode];
    std::map<std::pair<int, int>, Movement> byPair;
    for (int ci = N.firstConn; ci < N.firstConn + N.connCount; ci++) {
        const AI::Connector& c = G.conns[(size_t)ci];
        const AI::Approach &A = N.approaches[c.approach], &B = N.approaches[c.outApproach];
        const int a = legOf(*v.legs, A.edge, A.outgoing), b = legOf(*v.legs, B.edge, B.outgoing);
        if (a < 0 || b < 0) continue;
        const char turn = turnName(v, a, b);
        if (!allowedPair(v, a, b, turn)) continue;
        const std::string turns = movesAt(m, v, a);
        if (!turns.empty() && turns.find(turn) == std::string::npos) continue;
        Movement& mv = byPair[std::make_pair(a, b)];
        mv.in = a;
        mv.out = b;
        mv.from = v.letters[(size_t)a];
        mv.turn = turn;
        mv.conns.push_back(ci);
        v.names[std::make_pair(a, b)] = turn;
    }
    std::vector<Movement> out;
    for (auto& x : byPair) out.push_back(x.second);
    return out;
}

// road.lanes: every lane with traffic goes on somewhere at the node it runs into. (A lane whose edge has a moves record
// there gets exactly the turns it lists in the real build, and road.moves checks that those lead somewhere.)
void laneEnds(const Model& m, const AI::LaneGraph& G, const RoadIndex& idx, const std::vector<NodeLegs>& legs, Out& o) {
    struct Stuck {
        std::vector<int> lanes;   // 1-based, from the left
        int count = 0;
        int node = -1;
        bool byRecords = false;   // the lane graph has a way on, and the junction's records take it away
    };
    std::map<std::pair<int, int>, Stuck> stuck;   // (model edge, 0 fwd / 1 bwd)
    for (const AI::Lane& L : G.lanes) {
        if ((L.flags & AI::LF_NOTRAFFIC) || L.toNode < 0) continue;
        const int node = idx.nodeOf[(size_t)L.toNode];
        if (!legs[(size_t)node].complete) continue;
        const NodeView v = viewOf(m, legs, node);
        const AI::NodeInfo& N = G.nodes[(size_t)L.toNode];
        const int a = legOf(*v.legs, L.edge, L.dir < 0);   // (a lane running n1 -> n0 arrives at the edge's from end)
        if (a < 0 || !movesAt(m, v, a).empty()) continue;
        int ways = 0;
        for (int ci : L.out) {
            const AI::Connector& c = G.conns[(size_t)ci];
            const AI::Approach& B = N.approaches[c.outApproach];
            const int b = legOf(*v.legs, B.edge, B.outgoing);
            ways += b >= 0 && allowedPair(v, a, b, turnName(v, a, b));
        }
        if (ways > 0) continue;
        Stuck& s = stuck[std::make_pair(idx.edgeOf[(size_t)L.edge], L.dir > 0 ? 0 : 1)];
        s.lanes.push_back(L.index + 1);
        s.count = L.count;
        s.node = node;
        s.byRecords = s.byRecords || !L.out.empty();
    }
    for (auto& x : stuck) {
        const Edge& e = m.edges[(size_t)x.first.first];
        Stuck& s = x.second;
        std::sort(s.lanes.begin(), s.lanes.end());
        std::vector<std::string> ln;
        for (int l : s.lanes) ln.push_back(std::to_string(l));
        const char* dir = x.first.second ? "bwd" : "fwd";
        const std::string which = (int)s.lanes.size() == s.count
                                      ? std::string(s.count == 1 ? "its " : "all its ") + dir + (s.count == 1 ? " lane" : " lanes")
                                      : std::string("its ") + dir + (s.lanes.size() == 1 ? " lane " : " lanes ") + listed(ln) + " of " + std::to_string(s.count);
        const std::string& node = m.nodes[(size_t)s.node].id;
        o.error(e.src, "road.lanes", "edge " + e.id + ": " + which + (s.lanes.size() == 1 ? " leads" : " lead") + " nowhere at " + node + ": " +
                                         (s.byRecords ? "junction " + node + "'s records (ban, connect under turns=custom, no_uturn) leave no movement"
                                                      : "no road with lanes leads on from there"));
    }
}

// What a plan's name covers: the movements from the leg its letter names that turn as it says
bool covers(const std::string& x, const Movement& mv, bool* isProtected) {
    const bool prot = x.size() == 4 && x[3] == 'p';
    if ((prot ? x.substr(0, 3) : x) != mv.name()) return false;
    *isProtected = prot;
    return true;
}

bool crosses(const AI::LaneGraph& G, const Movement& a, const Movement& b) {
    for (int ca : a.conns)
        for (const AI::Conflict& k : G.conns[(size_t)ca].conflicts) {
            if (G.conns[(size_t)ca].grade || G.conns[(size_t)k.other].grade) continue;
            if (std::find(b.conns.begin(), b.conns.end(), k.other) != b.conns.end()) return true;
        }
    return false;
}

// signal.cover and signal.conflict: an authored plan gives every movement and every crossing of its node a green or a
// walk, and no phase lets two movements whose paths meet go together unless one of them is a permissive turn (it
// yields), or both are lefts from opposite approaches (they pass each other). A walk goes with the movements that don't
// cross it, and with permissive turns.
void plans(const Model& m, const AI::LaneGraph& G, const RoadIndex& idx, const std::vector<NodeLegs>& legs, Out& o) {
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        const Node& n = m.nodes[ni];
        if (n.plan.target < 0 || n.plan.cls != "signal" || n.control != Node::CTL_SIGNAL || !legs[ni].complete) continue;
        const SignalPlan& p = m.signals[(size_t)n.plan.target];
        const int rn = idx.roadNode[ni];
        const AI::NodeInfo& N = G.nodes[(size_t)rn];
        NodeView v = viewOf(m, legs, (int)ni);
        const std::vector<Movement> moves = movementsAt(m, G, v, rn);
        const Src at = n.plan.src.file >= 0 ? n.plan.src : n.src;
        const std::string what = "node " + n.id + ": plan " + p.id;
        // every movement and every crossing gets its time
        std::vector<std::string> noGreen, noWalk;
        for (const Movement& mv : moves) {
            bool green = false, prot = false;
            for (const Phase& ph : p.phases)
                for (const std::string& x : ph.moves) green = green || covers(x, mv, &prot);
            if (!green) noGreen.push_back(mv.name() + " (" + m.edges[(size_t)v.legs->legs[(size_t)mv.in].edge].id + " into " + m.edges[(size_t)v.legs->legs[(size_t)mv.out].edge].id + ")");
        }
        for (const AI::Approach& A : N.approaches) {
            if (!A.crosswalk) continue;
            const int a = legOf(*v.legs, A.edge, A.outgoing);
            if (a < 0) continue;
            bool walk = false;
            for (const Phase& ph : p.phases)
                for (const std::string& w : ph.walk) walk = walk || w == std::string(1, v.letters[(size_t)a]);
            if (!walk) noWalk.push_back(std::string(compassWord(v.letters[(size_t)a])) + " (" + m.edges[(size_t)v.legs->legs[(size_t)a].edge].id + ")");
        }
        if (!noGreen.empty()) o.error(at, "signal.cover", what + " never gives " + listed(noGreen) + " a green");
        if (!noWalk.empty())
            o.error(at, "signal.cover", what + " never gives the crossing" + (noWalk.size() == 1 ? " over the " : "s over the ") + listed(noWalk) +
                                            (noWalk.size() == 1 ? " leg" : " legs") + " a walk");
        // what each phase lets go together
        for (const Phase& ph : p.phases) {
            std::vector<const Movement*> green;
            std::vector<bool> prot;
            for (const Movement& mv : moves)
                for (const std::string& x : ph.moves) {
                    bool pr = false;
                    if (!covers(x, mv, &pr)) continue;
                    const auto it = std::find(green.begin(), green.end(), &mv);
                    if (it == green.end()) {
                        green.push_back(&mv);
                        prot.push_back(pr);
                    } else if (pr) {
                        prot[(size_t)(it - green.begin())] = true;
                    }
                }
            const auto yields = [&](size_t i) { return green[i]->turn != 's' && !prot[i]; };
            const auto label = [&](size_t i) { return green[i]->name() + (prot[i] ? "p" : ""); };
            std::vector<std::string> pairs, walks;
            for (size_t i = 0; i < green.size(); i++)
                for (size_t k = i + 1; k < green.size(); k++) {
                    if (yields(i) || yields(k) || green[i]->in == green[k]->in) continue;
                    const bool opposingLefts = green[i]->turn == 'l' && green[k]->turn == 'l' &&
                                               dot(v.legs->legs[(size_t)green[i]->in].dir, v.legs->legs[(size_t)green[k]->in].dir) < -0.7f;
                    if (opposingLefts) continue;
                    if (crosses(G, *green[i], *green[k]) || crosses(G, *green[k], *green[i])) pairs.push_back(label(i) + " with " + label(k));
                }
            for (const std::string& w : ph.walk)
                for (size_t i = 0; i < green.size(); i++) {
                    if (w.size() != 1 || yields(i)) continue;
                    const char in = v.letters[(size_t)green[i]->in], out = v.letters[(size_t)green[i]->out];
                    if (in == w[0] || out == w[0]) walks.push_back(label(i) + " across the " + compassWord(w[0]) + " leg's crossing");
                }
            if (!pairs.empty())
                o.error(at, "signal.conflict", what + ": phase " + ph.id + " gives greens to movements whose paths meet, and neither yields: " + listed(pairs));
            if (!walks.empty()) o.error(at, "signal.conflict", what + ": phase " + ph.id + " gives a walk while traffic that doesn't yield crosses it: " + listed(walks));
        }
    }
}

}  // namespace lanes_check_detail

void checkLaneGraph(const Hand::Model& m, const RoadNetwork& net, const Hand::RoadIndex& idx, std::vector<ModelFinding>& out, LaneGraphCounts& counts) {
    using namespace lanes_check_detail;
    counts = LaneGraphCounts();
    if (net.edges.empty()) return;
    Out o{out};
    AI::LaneGraph G;
    G.build(net);
    counts.built = true;
    counts.lanes = G.lanes.size();
    counts.connectors = G.conns.size();
    counts.walkNodes = G.walkNodes.size();
    counts.walkLinks = G.walkLinks.size();
    const std::vector<NodeLegs> legs = nodeLegs(m, net, idx);
    laneEnds(m, G, idx, legs, o);
    plans(m, G, idx, legs, o);
}

}  // namespace Validate
}  // namespace World
