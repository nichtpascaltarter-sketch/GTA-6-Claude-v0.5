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

// A movement at a node: from the leg the traffic arrives on to the leg it leaves on, with the lane graph's connectors
// that make it. A plan names it by its arrival leg's letter and its turn (n_l), the turn legTurn's (a connect record's
// where one joins the two edges).
struct Movement {
    int in = -1, out = -1;   // NodeLegs indices
    char turn = 's';
    std::vector<int> conns;
};

struct NodeView {
    const NodeLegs* legs = nullptr;
    const Junction* junction = nullptr;
};

int legOf(const NodeLegs& N, int roadEdge, bool outgoing) {
    for (size_t i = 0; i < N.legs.size(); i++)
        if (N.legs[i].road == roadEdge && N.legs[i].outgoing == outgoing) return (int)i;
    return -1;
}

std::string nameOf(const NodeView& v, const Movement& mv) { return std::string(1, v.legs->legs[(size_t)mv.in].letter) + "_" + mv.turn; }

// The turn that takes traffic from leg a into leg b, as a plan names it ('u' for the lane graph's U-turns: back the
// way it came, or back round past 140 degrees)
char turnName(const Model& m, const NodeView& v, int a, int b) {
    if (a == b) return 'u';
    const char t = turnChar(legTurn(m, *v.legs, v.legs->legs[(size_t)a], v.legs->legs[(size_t)b]));
    return t ? t : 'u';
}

// Whether the junction's records let traffic go from leg a into leg b: not banned, connected under turns=custom, no
// U-turn under turns=no_uturn
bool allowedPair(const NodeView& v, int a, int b, char turn) {
    const NodeLeg &A = v.legs->legs[(size_t)a], &B = v.legs->legs[(size_t)b];
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
    const NodeLeg& A = v.legs->legs[(size_t)a];
    std::string all;
    for (const Moves& mv : m.edges[(size_t)A.edge].moves)
        if (mv.atEnd == (A.outgoing ? 0 : 1))   // (the lanes arriving at the edge's to end are its fwd lanes: at=n1)
            for (const std::string& t : mv.lanes) all += t;
    return all;
}

NodeView viewOf(const Model& m, const std::vector<LegsAt>& legs, int node) {
    NodeView v;
    v.legs = &legs[(size_t)node].legs;
    v.junction = junctionOf(m, node);
    return v;
}

// The node's movements: the lane graph's connectors grouped by the legs they join, less what the junction's records
// and the moves records take away (the lane graph doesn't read them yet). The lane graph's U-turns (a leg back into
// itself: its way out of a dead end, or of a junction with no other) aren't among them: no plan names those.
std::vector<Movement> movementsAt(const Model& m, const AI::LaneGraph& G, const NodeView& v, int roadNode) {
    const AI::NodeInfo& N = G.nodes[(size_t)roadNode];
    std::map<std::pair<int, int>, Movement> byPair;
    for (int ci = N.firstConn; ci < N.firstConn + N.connCount; ci++) {
        const AI::Connector& c = G.conns[(size_t)ci];
        const AI::Approach &A = N.approaches[c.approach], &B = N.approaches[c.outApproach];
        const int a = legOf(*v.legs, A.edge, A.outgoing), b = legOf(*v.legs, B.edge, B.outgoing);
        if (a < 0 || b < 0 || a == b) continue;
        const char turn = turnName(m, v, a, b);
        if (!allowedPair(v, a, b, turn)) continue;
        const std::string turns = movesAt(m, v, a);
        if (!turns.empty() && turns.find(turn) == std::string::npos) continue;
        Movement& mv = byPair[std::make_pair(a, b)];
        mv.in = a;
        mv.out = b;
        mv.turn = turn;
        mv.conns.push_back(ci);
    }
    std::vector<Movement> out;
    for (auto& x : byPair) out.push_back(x.second);
    return out;
}

// road.lanes: every lane with traffic goes on somewhere at the node it runs into. (A lane whose edge has a moves record
// there gets exactly the turns it lists in the real build, and road.moves checks that those lead somewhere.)
void laneEnds(const Model& m, const AI::LaneGraph& G, const RoadIndex& idx, const std::vector<LegsAt>& legs, Out& o) {
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
            ways += b >= 0 && allowedPair(v, a, b, turnName(m, v, a, b));
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

bool crosses(const AI::LaneGraph& G, const Movement& a, const Movement& b) {
    for (int ca : a.conns)
        for (const AI::Conflict& k : G.conns[(size_t)ca].conflicts) {
            if (G.conns[(size_t)ca].grade || G.conns[(size_t)k.other].grade) continue;
            if (std::find(b.conns.begin(), b.conns.end(), k.other) != b.conns.end()) return true;
        }
    return false;
}

// signal.cover and signal.conflict: an authored plan, resolved to the node's edges (worldmodel.h resolvePlan), gives
// every movement and every crossing of its node a green or a walk, and no phase lets two movements whose paths meet go
// together unless one of them is a permissive turn (it yields), or both are lefts from opposite approaches (they pass
// each other). A walk goes with the movements that don't cross it, and with permissive turns.
void plans(const Model& m, const AI::LaneGraph& G, const RoadIndex& idx, const std::vector<LegsAt>& legs, Out& o) {
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        const Node& n = m.nodes[ni];
        if (n.plan.target < 0 || n.plan.cls != "signal" || n.control != Node::CTL_SIGNAL || !legs[ni].complete) continue;
        const SignalPlan& p = m.signals[(size_t)n.plan.target];
        const int rn = idx.roadNode[ni];
        const AI::NodeInfo& N = G.nodes[(size_t)rn];
        const NodeView v = viewOf(m, legs, (int)ni);
        const std::vector<NodeLeg>& L = v.legs->legs;
        const std::vector<Movement> moves = movementsAt(m, G, v, rn);
        ResolvedPlan plan;
        resolvePlan(m, *v.legs, p, plan);
        const auto inPhase = [&](const ResolvedPlan::Phase& ph, const Movement& mv, bool* prot) {
            for (const ResolvedPlan::Move& x : ph.moves)
                if (x.from == L[(size_t)mv.in].road && x.to == L[(size_t)mv.out].road) {
                    *prot = x.isProtected;
                    return true;
                }
            return false;
        };
        const Src at = n.plan.src.file >= 0 ? n.plan.src : n.src;
        const std::string what = "node " + n.id + ": plan " + p.id;
        // every movement and every crossing gets its time
        std::vector<std::string> noGreen, noWalk;
        for (const Movement& mv : moves) {
            bool green = false, prot = false;
            for (const ResolvedPlan::Phase& ph : plan.phases) green = green || inPhase(ph, mv, &prot);
            if (!green)
                noGreen.push_back(nameOf(v, mv) + " (" + m.edges[(size_t)L[(size_t)mv.in].edge].id + " into " + m.edges[(size_t)L[(size_t)mv.out].edge].id + ")");
        }
        for (const AI::Approach& A : N.approaches) {
            if (!A.crosswalk) continue;
            const int a = legOf(*v.legs, A.edge, A.outgoing);
            if (a < 0) continue;
            bool walk = false;
            for (const ResolvedPlan::Phase& ph : plan.phases) walk = walk || std::find(ph.walks.begin(), ph.walks.end(), A.edge) != ph.walks.end();
            if (!walk) noWalk.push_back(std::string(compassWord(L[(size_t)a].letter)) + " (" + m.edges[(size_t)L[(size_t)a].edge].id + ")");
        }
        if (!noGreen.empty()) o.error(at, "signal.cover", what + " never gives " + listed(noGreen) + " a green");
        if (!noWalk.empty())
            o.error(at, "signal.cover", what + " never gives the crossing" + (noWalk.size() == 1 ? " over the " : "s over the ") + listed(noWalk) +
                                            (noWalk.size() == 1 ? " leg" : " legs") + " a walk");
        // what each phase lets go together
        for (size_t pi = 0; pi < p.phases.size() && pi < plan.phases.size(); pi++) {
            const ResolvedPlan::Phase& ph = plan.phases[pi];
            std::vector<const Movement*> green;
            std::vector<bool> prot;
            for (const Movement& mv : moves) {
                bool pr = false;
                if (!inPhase(ph, mv, &pr)) continue;
                green.push_back(&mv);
                prot.push_back(pr);
            }
            const auto yields = [&](size_t i) { return green[i]->turn != 's' && !prot[i]; };
            const auto label = [&](size_t i) { return nameOf(v, *green[i]) + (prot[i] ? "p" : ""); };
            std::vector<std::string> pairs, walks;
            for (size_t i = 0; i < green.size(); i++)
                for (size_t k = i + 1; k < green.size(); k++) {
                    if (yields(i) || yields(k) || green[i]->in == green[k]->in) continue;
                    const bool opposingLefts = green[i]->turn == 'l' && green[k]->turn == 'l' && dot(L[(size_t)green[i]->in].dir, L[(size_t)green[k]->in].dir) < -0.7f;
                    if (opposingLefts) continue;
                    if (crosses(G, *green[i], *green[k]) || crosses(G, *green[k], *green[i])) pairs.push_back(label(i) + " with " + label(k));
                }
            for (int w : ph.walks)
                for (size_t i = 0; i < green.size(); i++) {
                    if (yields(i)) continue;
                    if (L[(size_t)green[i]->in].road != w && L[(size_t)green[i]->out].road != w) continue;
                    int crossed = -1;
                    for (size_t k = 0; k < L.size() && crossed < 0; k++)
                        if (L[k].road == w) crossed = (int)k;
                    walks.push_back(label(i) + " across the " + compassWord(crossed >= 0 ? L[(size_t)crossed].letter : 'n') + " leg's crossing");
                }
            if (!pairs.empty())
                o.error(at, "signal.conflict", what + ": phase " + p.phases[pi].id + " gives greens to movements whose paths meet, and neither yields: " + listed(pairs));
            if (!walks.empty())
                o.error(at, "signal.conflict", what + ": phase " + p.phases[pi].id + " gives a walk while traffic that doesn't yield crosses it: " + listed(walks));
        }
    }
}

}  // namespace lanes_check_detail

void checkLaneGraph(const Hand::Model& m, const RoadNetwork& net, const Hand::RoadIndex& idx, const RoadOverrides& ov, std::vector<ModelFinding>& out,
                    LaneGraphCounts& counts) {
    using namespace lanes_check_detail;
    counts = LaneGraphCounts();
    if (net.edges.empty()) return;
    Out o{out};
    AI::LaneGraph G;
    G.build(net, &ov);
    counts.built = true;
    counts.lanes = G.lanes.size();
    counts.connectors = G.conns.size();
    counts.walkNodes = G.walkNodes.size();
    counts.walkLinks = G.walkLinks.size();
    const std::vector<LegsAt> legs = legsOfNodes(m, net, idx);
    laneEnds(m, G, idx, legs, o);
    plans(m, G, idx, legs, o);
}

}  // namespace Validate
}  // namespace World
