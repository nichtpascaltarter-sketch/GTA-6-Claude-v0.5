// worldv's checks on the world model (validate_roads.h): the roads, their signal plans and the transit lines.
#include "validate_roads.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace World {
namespace Validate {

namespace roads_detail {

using namespace World::Hand;

const float kTol = 0.05f;          // m: positions along an edge are written to the cm
const float kOverlapRun = 10.f;    // m of carriageways on each other before it's an error (HANDBUILT_WORLD 12.2.12)
const float kGrade = 3.f;          // m: a smaller height difference is at grade (as tools/traffic_sim --overlaps)
const float kRadToDeg = 57.2957795f;

std::string fmt(float v, int places = 2) {
    char buf[48];
    const float limit = places >= 2 ? 0.005f : 0.05f;
    snprintf(buf, sizeof(buf), "%.*f", places, (double)(fabsf(v) < limit ? 0.f : v));
    return buf;
}
std::string xy(vec2 p) { return fmt(p.x) + "," + fmt(p.y); }
std::string count(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
std::string listed(const std::vector<std::string>& items) {   // a, a and b, a, b and c
    std::string s;
    for (size_t i = 0; i < items.size(); i++) s += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
    return s;
}

const char* sideWord(Side s) { return s == SIDE_LEFT ? "left" : s == SIDE_RIGHT ? "right" : "both"; }
const char* dirWord(Dir d) { return d == DIR_FWD ? "fwd" : "bwd"; }
const char* className(RoadClass c) {
    static const char* const kNames[RC_COUNT] = {"highway", "boulevard", "avenue", "street", "lane", "rural", "dirt", "ramp"};
    return c < RC_COUNT ? kNames[c] : "street";
}

struct Out {
    std::vector<ModelFinding>& v;
    void add(const Src& s, Severity sev, const char* check, const std::string& msg) {
        ModelFinding f;
        f.src = s;
        f.severity = sev;
        f.check = check;
        f.message = msg;
        v.push_back(std::move(f));
    }
    void error(const Src& s, const char* check, const std::string& msg) { add(s, SEV_ERROR, check, msg); }
    void warning(const Src& s, const char* check, const std::string& msg) { add(s, SEV_WARNING, check, msg); }
};

// ---------------------------------------------------------------------------------------------- along one edge

float planLength(const Edge& e) {
    float l = 0.f;
    for (size_t k = 1; k < e.shape.size(); k++) l += length(e.shape[k] - e.shape[k - 1]);
    return l;
}

float atS(const EdgeAt& p, float L) { return p.toEnd ? L : p.s; }
std::string atText(const EdgeAt& p) { return p.toEnd ? std::string("end") : fmt(p.s); }
std::string lanesText(const LaneCount& c) { return "lanes from=" + atText(c.from) + " to=" + atText(c.to) + " dir=" + dirWord(c.dir) + " count=" + std::to_string(c.count); }

// The lanes record of one direction that covers s (plan metres from the edge's from node), or none
const LaneCount* laneRecordAt(const Edge& e, Dir d, float s, float L) {
    for (const LaneCount& c : e.laneCounts)
        if (c.dir == d && s >= atS(c.from, L) - kTol && s <= atS(c.to, L) + kTol) return &c;
    return nullptr;
}

// One direction's lanes at s: the lanes record's count there, else lanes=F/B
int lanesAt(const Edge& e, Dir d, float s, float L) {
    const LaneCount* c = laneRecordAt(e, d, s, L);
    return c ? c->count : d == DIR_FWD ? e.lanesF : e.lanesB;
}

bool overlapping(float a0, float a1, float b0, float b1) { return Min(a1, b1) - Max(a0, b0) > kTol; }

// road.along: every position along the edge on it, every stretch running forward, one lane count per stretch
void alongChecks(const Model& m, const Edge& e, float L, Out& o) {
    const std::string what = "edge " + e.id + ": ";
    const auto past = [&](const Src& s, const std::string& rec, float lo, float hi) {
        if (lo < -kTol) o.error(s, "road.along", what + rec + " starts before the edge does: distances along it run from 0.00 at its from node");
        else if (hi > L + kTol) o.error(s, "road.along", what + rec + " runs past the edge's end: the edge is " + fmt(L) + " m long");
        else return false;
        return true;
    };
    const auto point = [&](const Src& s, const std::string& rec, float v) { past(s, rec, v, v); };
    const auto stretch = [&](const Src& s, const std::string& rec, float a, float b) {
        if (!past(s, rec, a, b) && b <= a + kTol) o.error(s, "road.along", what + rec + " runs backwards or has no length: to= should be past from=");
    };
    for (const LaneCount& c : e.laneCounts) stretch(c.src, lanesText(c), atS(c.from, L), atS(c.to, L));
    for (const LaneUse& u : e.laneUses) stretch(u.src, "lane from=" + atText(u.from) + " to=" + atText(u.to), atS(u.from, L), atS(u.to, L));
    for (const NoSwitch& n : e.noSwitch) stretch(n.src, "noswitch from=" + atText(n.from) + " to=" + atText(n.to), atS(n.from, L), atS(n.to, L));
    for (const Crossing& c : e.crossings) point(c.src, "crossing at=" + fmt(c.at), c.at);
    for (int bi : e.busStops) {
        const BusStop& b = m.busStops[(size_t)bi];
        point(b.src, "busstop " + b.id + " at=" + fmt(b.at), b.at);
    }
    for (const KerbStretch& k : e.kerb) stretch(k.src, k.type + " from=" + fmt(k.from) + " to=" + fmt(k.to), k.from, k.to);
    for (const CurbCut& c : e.curbCuts) past(c.src, "curbcut at=" + fmt(c.at) + " w=" + fmt(c.w), c.at - c.w * 0.5f, c.at + c.w * 0.5f);
    for (const Deck& d : e.decks) stretch(d.src, "deck from=" + fmt(d.from) + " to=" + fmt(d.to), d.from, d.to);
    for (const Sign& s : e.signs) point(s.src, "sign at=" + fmt(s.at), s.at);
    // two counts for the same lanes
    for (size_t j = 0; j < e.laneCounts.size(); j++)
        for (size_t i = 0; i < j; i++) {
            const LaneCount &a = e.laneCounts[i], &b = e.laneCounts[j];
            if (a.dir != b.dir || !overlapping(atS(a.from, L), atS(a.to, L), atS(b.from, L), atS(b.to, L))) continue;
            o.error(b.src, "road.along", what + lanesText(b) + " overlaps the " + dirWord(a.dir) + " lanes record at line " + std::to_string(a.src.line) + " (from=" +
                                             atText(a.from) + " to=" + atText(a.to) + "): one count per stretch of a direction's lanes");
            break;
        }
}

const char* const kTurnLetters = "lsru";

// road.moves on its own edge: a record per end, a turn list per lane arriving there, letters l, s, r and u
void movesChecks(const Edge& e, float L, Out& o) {
    for (size_t i = 0; i < e.moves.size(); i++) {
        const Moves& mv = e.moves[i];
        const std::string what = "edge " + e.id + ": moves at=" + (mv.atEnd ? "n1" : "n0");
        bool twice = false;
        for (size_t k = 0; k < i && !twice; k++)
            if (e.moves[k].atEnd == mv.atEnd) {
                o.error(mv.src, "road.moves", what + " is the second moves record for that end (the first is at line " + std::to_string(e.moves[k].src.line) + ")");
                twice = true;
            }
        if (twice) continue;
        const Dir d = mv.atEnd ? DIR_FWD : DIR_BWD;   // (the fwd lanes arrive at n1)
        const float s = mv.atEnd ? L : 0.f;
        const LaneCount* rec = laneRecordAt(e, d, s, L);
        const int lanes = rec ? rec->count : d == DIR_FWD ? e.lanesF : e.lanesB;
        const std::string why = rec ? "the lanes record at line " + std::to_string(rec->src.line) + " gives " + std::to_string(lanes)
                                    : "lanes=" + std::to_string(e.lanesF) + "/" + std::to_string(e.lanesB);
        if (lanes == 0) {
            o.error(mv.src, "road.moves", what + ": no lanes arrive at " + (mv.atEnd ? "n1" : "n0") + " (" + why + ")");
            continue;
        }
        if ((int)mv.lanes.size() != lanes)
            o.error(mv.src, "road.moves", what + " gives the turns of " + count(mv.lanes.size(), "lane", "lanes") + ", but " + std::to_string(lanes) + " arrive there (" + why +
                                              "): one entry per lane, from the left");
        for (size_t k = 0; k < mv.lanes.size(); k++) {
            const std::string& t = mv.lanes[k];
            bool ok = !t.empty();
            for (size_t c = 0; c < t.size() && ok; c++) ok = strchr(kTurnLetters, t[c]) != nullptr && t.find(t[c]) == c;
            if (!ok)
                o.error(mv.src, "road.moves", what + ": lane " + std::to_string(k + 1) + "'s turns are '" + t +
                                                  "': each lane's turns are the letters l, s, r and u (left, straight, right, U-turn), each once");
        }
    }
}

// parking.clash: a parking bay over a nostop, a curb cut or another bay on its side of the edge
void parkingChecks(const Edge& e, Out& o) {
    std::vector<const KerbStretch*> bays, nostops;
    for (const KerbStretch& k : e.kerb) {
        if (k.type == "parkbay") bays.push_back(&k);
        else if (k.type == "nostop") nostops.push_back(&k);
    }
    for (size_t i = 0; i < bays.size(); i++) {
        const KerbStretch& p = *bays[i];
        const std::string rec = "edge " + e.id + ": parkbay from=" + fmt(p.from) + " to=" + fmt(p.to) + " (" + sideWord(p.side) + ")";
        for (const KerbStretch* n : nostops)
            if (n->side == p.side && overlapping(p.from, p.to, n->from, n->to))
                o.error(p.src, "parking.clash", rec + " runs over the nostop at line " + std::to_string(n->src.line) + " (from=" + fmt(n->from) + " to=" + fmt(n->to) +
                                                    (n->kind.empty() ? std::string() : ", why=" + n->kind) + ")");
        for (const CurbCut& c : e.curbCuts)
            if (c.side == p.side && overlapping(p.from, p.to, c.at - c.w * 0.5f, c.at + c.w * 0.5f))
                o.error(p.src, "parking.clash", rec + " runs over the curb cut at line " + std::to_string(c.src.line) + " (at=" + fmt(c.at) + ", " + fmt(c.w) +
                                                    " m wide): a curb cut implies a nostop across it");
        for (size_t j = 0; j < i; j++)
            if (bays[j]->side == p.side && overlapping(p.from, p.to, bays[j]->from, bays[j]->to))
                o.error(p.src, "parking.clash", rec + " overlaps the parkbay at line " + std::to_string(bays[j]->src.line) + " (from=" + fmt(bays[j]->from) + " to=" +
                                                    fmt(bays[j]->to) + ")");
    }
}

// The sidewalk on one side: as written, else the class's
float sidewalkOn(const Edge& e, Side s) {
    const float w = s == SIDE_LEFT ? e.sidewalkL : e.sidewalkR;
    return w >= 0.f ? w : roadInfo(e.cls).sidewalk;
}

// busstop.kerb: a stop where a bus can pull in and people wait: off the highways and ramps, at a kerb with lanes along it
// that have the kerb on their right, and with a sidewalk there (or its own wait= area)
void busStopChecks(const Model& m, const Edge& e, float L, Out& o) {
    for (int bi : e.busStops) {
        const BusStop& b = m.busStops[(size_t)bi];
        const std::string what = "bus stop " + b.id + " (" + sideWord(b.side) + " side of " + e.id + ")";
        if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP) {
            o.error(b.src, "busstop.kerb", what + " is on a " + className(e.cls) + ": buses stop at a kerb of a street");
            continue;
        }
        const Dir d = b.side == SIDE_LEFT ? DIR_BWD : DIR_FWD;   // the lanes with this kerb on their right
        if (lanesAt(e, d, Clamp(b.at, 0.f, L), L) == 0)
            o.error(b.src, "busstop.kerb", what + ": no lanes run along that kerb with it on their right (no " + dirWord(d) + " lanes there), so no bus pulls in to it");
        if (b.wait.empty() && sidewalkOn(e, b.side) <= 0.f)
            o.error(b.src, "busstop.kerb", what + ": the edge has no sidewalk on that side to wait on, and the stop gives no wait= area");
    }
}

// A movement's name in a signal plan: <approach>_<turn>, with p for protected (n_lp)
bool moveName(const std::string& s) {
    return (s.size() == 3 || (s.size() == 4 && s[3] == 'p')) && strchr("nesw", s[0]) && s[1] == '_' && strchr(kTurnLetters, s[2]);
}

// signal.plan: the cycle the sum of its phases, the movements and crossings named as the format names them, each walk
// over before its phase is
void planChecks(const Model& m, Out& o) {
    for (const SignalPlan& p : m.signals) {
        int planLine = 0;
        if (p.src.file >= 0)
            for (const Ntw::Record& r : m.files[(size_t)p.src.file].doc.records)
                if (r.type == "plan" && planLine == 0) planLine = r.line;
        const Src at{p.src.file, planLine > 0 ? planLine : 1};
        const std::string what = "plan " + p.id + ": ";
        if (planLine == 0) o.error(at, "signal.plan", what + "the file has no plan record, so no cycle");
        float sum = 0.f;
        for (const Phase& ph : p.phases) sum += ph.green + ph.amber + ph.allred;
        if (p.phases.empty()) o.error(at, "signal.plan", what + "it has no phases");
        else if (planLine > 0 && fabsf(sum - p.cycle) > 0.05f)
            o.error(at, "signal.plan", what + "its cycle is " + fmt(p.cycle, 1) + " s, but its phases' green, amber and all-red add up to " + fmt(sum, 1) + " s");
        if (p.legs != 0 && p.legs != 3 && p.legs != 4) o.error(at, "signal.plan", what + "legs=" + std::to_string(p.legs) + ": a plan suits junctions of 3 or 4 legs");
        for (const Phase& ph : p.phases) {
            const std::string pw = "phase " + ph.id + ": ";
            std::set<std::string> seen;
            for (const std::string& mv : ph.moves) {
                if (!moveName(mv)) {
                    o.error(ph.src, "signal.plan", pw + "moves= names " + mv + ": a movement is <approach>_<turn>, the approach n, e, s or w and the turn l, s, r or u, " +
                                                       "with a p after a protected turn (n_lp)");
                    continue;
                }
                if (!seen.insert(mv).second) {
                    o.error(ph.src, "signal.plan", pw + "moves= names " + mv + " twice");
                    continue;
                }
                const std::string base = mv.substr(0, 3);
                if (seen.count(base) && seen.count(base + "p"))
                    o.error(ph.src, "signal.plan", pw + "moves= names both " + base + " and " + base + "p: a turn is protected or permissive in a phase, not both");
            }
            std::set<std::string> walks;
            for (const std::string& w : ph.walk) {
                if (w.size() != 1 || !strchr("nesw", w[0])) o.error(ph.src, "signal.plan", pw + "walk= names " + w + ": a crossing is named by the leg it crosses, n, e, s or w");
                else if (!walks.insert(w).second) o.error(ph.src, "signal.plan", pw + "walk= names " + w + " twice");
            }
            const float total = ph.green + ph.amber + ph.allred;
            if (!ph.walk.empty() && ph.walkTime + ph.flash > total + 0.05f)
                o.error(ph.src, "signal.plan", pw + "its walk (" + fmt(ph.walkTime, 1) + " s) and flashing don't-walk (" + fmt(ph.flash, 1) + " s) outlast the phase's " +
                                                   fmt(total, 1) + " s of green, amber and all-red");
        }
    }
}

// ------------------------------------------------------------------------------------------------- the network

// A leg of a node: an edge at it, seen from the node the way the lane graph sees it (lanes.cpp: its direction at
// max(cut-back, 4 m) along the edge)
struct Leg {
    int road = -1;          // RoadNetwork edge
    int edge = -1;          // Model edge
    bool outgoing = false;  // the edge starts here: its fwd lanes leave, its bwd lanes arrive
    vec2 dir = vec2(0.f);
    float bearing = 0.f;    // degrees clockwise from north
    char compass = 'n';     // its sector: n, e, s, w
    int in = 0, out = 0;    // lanes arriving at the node along it, and leaving
};

struct NodeLegs {
    std::vector<Leg> legs;
    bool complete = true;   // every edge at the node compiled (otherwise the checks of its legs wait for the fix)
    bool interchange = false, gradeSeparated = false;
};

char compassOf(float bearing) { return bearing >= 315.f || bearing < 45.f ? 'n' : bearing < 135.f ? 'e' : bearing < 225.f ? 's' : 'w'; }

std::vector<NodeLegs> nodeLegs(const Model& m, const RoadNetwork& net, const RoadIndex& idx) {
    std::vector<NodeLegs> out(m.nodes.size());
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        NodeLegs& N = out[ni];
        for (int me : m.nodes[ni].edges) N.complete = N.complete && idx.roadEdge[(size_t)me] >= 0;
        const int rni = idx.roadNode[ni];
        const RoadNode& rn = net.nodes[(size_t)rni];
        int highways = 0, ramps = 0;
        for (int re : rn.edges) {
            const RoadEdge& e = net.edges[(size_t)re];
            Leg g;
            g.road = re;
            g.edge = idx.edgeOf[(size_t)re];
            g.outgoing = e.n0 == rni;
            const float cut = g.outgoing ? e.cut0 : e.cut1;
            const float probe = Clamp(Max(cut, 4.f), 0.f, e.length * 0.5f);
            vec2 d = e.posAt(g.outgoing ? probe : e.length - probe).xy() - rn.p;
            if (length2(d) < 1e-4f) d = (g.outgoing ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - rn.p;
            g.dir = normalize(d);
            g.bearing = atan2f(g.dir.x, g.dir.y) * kRadToDeg;
            if (g.bearing < 0.f) g.bearing += 360.f;
            g.compass = compassOf(g.bearing);
            const Edge& me = m.edges[(size_t)g.edge];
            const float L = planLength(me);
            g.in = g.outgoing ? lanesAt(me, DIR_BWD, 0.f, L) : lanesAt(me, DIR_FWD, L, L);
            g.out = g.outgoing ? lanesAt(me, DIR_FWD, 0.f, L) : lanesAt(me, DIR_BWD, L, L);
            highways += e.cls == RC_HIGHWAY;
            ramps += e.cls == RC_RAMP;
            N.legs.push_back(g);
        }
        N.interchange = highways > 0 && ramps > 0;
        N.gradeSeparated = N.legs.size() >= 4 && highways == (int)N.legs.size();
    }
    return out;
}

// The turn from leg a into leg b, as the lane graph classes it (lanes.cpp): 'l', 's', 'r', or 0 where it makes no
// movement (back round through more than 140 degrees; across a motorway's carriageways)
char turnClass(const NodeLegs& N, const Leg& a, const Leg& b) {
    const vec2 tin = -a.dir, tout = b.dir;
    const float deg = atan2f(cross(tin, tout), dot(tin, tout)) * kRadToDeg;
    char t = 's';
    if (N.legs.size() == 2) t = 's';
    else if (fabsf(deg) > 140.f) return 0;
    else if (deg > 35.f) t = 'l';
    else if (deg < -35.f) t = 'r';
    if (N.interchange) {
        if (fabsf(deg) > 60.f) return 0;
        t = 's';   // (a merge or a diverge)
    }
    if (N.gradeSeparated && t != 's') return 0;
    return t;
}

char turnLetter(const std::string& word) { return word == "left" ? 'l' : word == "right" ? 'r' : word == "uturn" ? 'u' : 's'; }
const char* turnWords(char t) { return t == 'l' ? "to the left" : t == 'r' ? "to the right" : t == 'u' ? "back (a U-turn)" : "straight on"; }

bool banned(const Junction* j, int fromEdge, int toEdge) {
    if (!j) return false;
    for (const Junction::Ban& b : j->bans)
        if (b.fromEdge.target == fromEdge && b.toEdge.target == toEdge) return true;
    return false;
}

const Junction* junctionOf(const Model& m, int node) {
    const int j = node >= 0 ? m.nodes[(size_t)node].junction : -1;
    return j >= 0 ? &m.junctions[(size_t)j] : nullptr;
}

std::string lanesList(const std::vector<int>& lanes) {
    std::vector<std::string> n;
    for (int l : lanes) n.push_back(std::to_string(l));
    return (lanes.size() == 1 ? "lane " : "lanes ") + listed(n);
}

// road.moves at the node: each turn a lane names has somewhere to go there
void moveTurns(const Model& m, const std::vector<NodeLegs>& legs, const RoadIndex& idx, Out& o) {
    for (size_t ei = 0; ei < m.edges.size(); ei++) {
        const Edge& e = m.edges[ei];
        if (e.moves.empty() || idx.roadEdge[ei] < 0) continue;
        for (size_t mi = 0; mi < e.moves.size(); mi++) {
            const Moves& mv = e.moves[mi];
            bool first = true;   // (a second record for the end has its own finding)
            for (size_t k = 0; k < mi; k++) first = first && e.moves[k].atEnd != mv.atEnd;
            const int node = mv.atEnd ? e.to.target : e.from.target;
            if (!first || node < 0 || !legs[(size_t)node].complete) continue;
            const NodeLegs& N = legs[(size_t)node];
            const Node& n = m.nodes[(size_t)node];
            const Junction* j = junctionOf(m, node);
            const Leg* a = nullptr;
            for (const Leg& g : N.legs)
                if (g.edge == (int)ei && g.outgoing == (mv.atEnd == 0)) a = &g;
            if (!a) continue;
            const bool custom = j && j->turns == "custom";
            bool has[128] = {};
            if (!custom)
                for (const Leg& b : N.legs) {
                    if (&b == a || b.out == 0 || banned(j, a->edge, b.edge)) continue;
                    if (const char t = turnClass(N, *a, b)) has[(int)t] = true;
                }
            has['u'] = !custom && !(j && j->turns == "no_uturn");
            if (j)
                for (const Junction::Connect& c : j->connects)
                    if (c.fromEdge.target == (int)ei) has[(int)turnLetter(c.turn)] = true;
            for (const char* t = kTurnLetters; *t; t++) {
                std::vector<int> lanes;
                for (size_t k = 0; k < mv.lanes.size(); k++)
                    if (mv.lanes[k].find(*t) != std::string::npos) lanes.push_back((int)k + 1);
                if (lanes.empty() || has[(int)*t]) continue;
                std::string why;
                if (custom) why = "junction " + n.id + " takes only its connect records (turns=custom), and none from " + e.id + " turns " + turnWords(*t);
                else if (*t == 'u') why = "junction " + n.id + " bans U-turns (turns=no_uturn)";
                else if (N.legs.size() == 1) why = n.id + " is a dead end";
                else why = "no road with lanes leaves " + n.id + " " + turnWords(*t);
                o.error(mv.src, "road.moves", "edge " + e.id + ": moves at=" + (mv.atEnd ? "n1" : "n0") + ": " + lanesList(lanes) + (lanes.size() == 1 ? " turns " : " turn ") +
                                                  std::string(1, *t) + ", but " + why);
            }
        }
    }
}

// The compass letters of a node's legs: approaches= where it names them, else by bearing. approaches= is read as
// road.sector reads it: a letter that isn't n, e, s or w is skipped, and so are a letter's second use and an edge's.
std::vector<char> legLetters(const Node& n, const NodeLegs& N) {
    std::set<char> given;
    std::map<int, char> named;   // model edge -> its letter
    for (const auto& a : n.approaches) {
        if (a.first.size() != 1 || !strchr("nesw", a.first[0]) || !given.insert(a.first[0]).second) continue;
        if (a.second.target >= 0 && !named.count(a.second.target)) named[a.second.target] = a.first[0];
    }
    std::vector<char> out;
    for (const Leg& g : N.legs) {
        const auto it = named.find(g.edge);
        out.push_back(it != named.end() ? it->second : g.compass);
    }
    return out;
}

const char* compassWord(char c) { return c == 'n' ? "north" : c == 'e' ? "east" : c == 's' ? "south" : "west"; }

std::string bearingText(const Model& m, const Leg& g) { return m.edges[(size_t)g.edge].id + " (bearing " + fmt(g.bearing, 0) + ")"; }

// road.sector: the legs of a junction, or of any signalled node, one to a compass sector unless approaches= names them
void sectors(const Model& m, const std::vector<NodeLegs>& legs, Out& o) {
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        const Node& n = m.nodes[ni];
        const NodeLegs& N = legs[ni];
        if (!N.complete || (N.legs.size() < 3 && n.control != Node::CTL_SIGNAL)) continue;
        const std::string what = "node " + n.id + ": ";
        std::set<char> given;
        std::map<char, int> named;   // letter -> model edge
        for (const auto& a : n.approaches) {
            if (a.first.size() != 1 || !strchr("nesw", a.first[0])) {
                o.error(n.src, "road.sector", what + "approaches= gives " + a.first + ":" + a.second.text + ": a leg's letter is n, e, s or w");
                continue;
            }
            if (!given.insert(a.first[0]).second) {
                o.error(n.src, "road.sector", what + "approaches= gives " + a.first + " twice");
                continue;
            }
            if (a.second.target < 0) continue;   // (worldv's reference checks report it)
            bool again = false;
            for (const auto& x : named) again = again || x.second == a.second.target;
            if (again) {
                o.error(n.src, "road.sector", what + "approaches= names " + a.second.text + " twice");
                continue;
            }
            named[a.first[0]] = a.second.target;
        }
        std::map<char, const Leg*> byBearing;
        for (const Leg& g : N.legs) {
            bool isNamed = false;
            for (const auto& x : named) isNamed = isNamed || x.second == g.edge;
            if (isNamed) continue;
            const auto taken = named.find(g.compass);
            if (taken != named.end()) {
                o.error(n.src, "road.sector", what + "leg " + bearingText(m, g) + " is in the " + compassWord(g.compass) + " sector, which approaches= gives " +
                                                  (taken->second >= 0 ? m.edges[(size_t)taken->second].id : std::string("another edge")) + ": name this leg too");
                continue;
            }
            const auto other = byBearing.find(g.compass);
            if (other != byBearing.end())
                o.error(n.src, "road.sector", what + "legs " + bearingText(m, *other->second) + " and " + bearingText(m, g) + " are both in the " + compassWord(g.compass) +
                                                  " sector: name them with approaches=");
            else byBearing[g.compass] = &g;
        }
    }
}

// The lanes of an edge at one of its ends: arriving there, or leaving
int lanesAtEnd(const Edge& e, bool atTo, bool arriving) {
    const float L = planLength(e);
    const Dir d = atTo == arriving ? DIR_FWD : DIR_BWD;   // (the fwd lanes arrive at the to end and leave the from end)
    return lanesAt(e, d, atTo ? L : 0.f, L);
}

// road.legs: what a node's fields and its junction's records name meets the node, and has the lanes they say
void legRefs(const Model& m, Out& o) {
    const auto meets = [&m](const Ref& r, int node) {
        if (r.target < 0) return true;   // (worldv's reference checks report it)
        const Edge& e = m.edges[(size_t)r.target];
        return e.from.target < 0 || e.to.target < 0 || e.from.target == node || e.to.target == node;   // (an end that names no node: as above)
    };
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        const Node& n = m.nodes[ni];
        const auto field = [&](const Ref& r, const char* key) {
            if (r.target >= 0 && r.cls == "edge" && !meets(r, (int)ni))
                o.error(r.src.file >= 0 ? r.src : n.src, "road.legs", "node " + n.id + ": " + key + "= names " + r.text + ", which doesn't meet it");
        };
        for (const auto& a : n.approaches) field(a.second, "approaches");
        for (const Ref& r : n.stops) field(r, "stops");
        for (const Ref& r : n.yields) field(r, "yield");
    }
    for (const Junction& j : m.junctions) {
        const int node = j.node.target;
        if (node < 0) continue;
        const std::string what = "junction " + m.nodes[(size_t)node].id + ": ";
        const auto edgeAt = [&](const Ref& r, const Src& s, const std::string& rec) {
            if (r.target < 0 || r.cls != "edge") return (const Edge*)nullptr;
            if (!meets(r, node)) {
                o.error(s, "road.legs", what + rec + " names " + r.text + ", which doesn't meet " + m.nodes[(size_t)node].id);
                return (const Edge*)nullptr;
            }
            return &m.edges[(size_t)r.target];
        };
        for (const Junction::StopLine& x : j.stopLines) {
            const std::string rec = "stopline edge=" + x.edge.text + " at=" + fmt(x.at);
            const Edge* e = edgeAt(x.edge, x.src, rec);
            if (!e) continue;
            const bool atTo = e->to.target == node;
            if (lanesAtEnd(*e, atTo, true) == 0) o.error(x.src, "road.legs", what + rec + ": no lanes of " + e->id + " arrive at the junction to stop at it");
            if (x.at < -kTol || x.at > planLength(*e) + kTol)
                o.error(x.src, "road.legs", what + rec + ": the stop line is off the edge, which is " + fmt(planLength(*e)) + " m long");
        }
        for (const Junction::Connect& c : j.connects) {
            const std::string rec = "connect from=" + c.fromEdge.text + ":" + std::to_string(c.fromLane) + " to=" + c.toEdge.text + ":" + std::to_string(c.toLane);
            const Edge* from = edgeAt(c.fromEdge, c.src, rec);
            const Edge* to = edgeAt(c.toEdge, c.src, rec);
            if (from) {
                const int in = lanesAtEnd(*from, from->to.target == node, true);
                if (c.fromLane >= in)
                    o.error(c.src, "road.legs", what + rec + ": " + from->id + " has " + count((size_t)in, "lane", "lanes") + " arriving here (lanes count from 0, from the left)");
            }
            if (to) {
                const int out = lanesAtEnd(*to, to->to.target == node, false);
                if (c.toLane >= out)
                    o.error(c.src, "road.legs", what + rec + ": " + to->id + " has " + count((size_t)out, "lane", "lanes") + " leaving here (lanes count from 0, from the left)");
            }
        }
        for (const Junction::Ban& b : j.bans) {
            const std::string rec = "ban from=" + b.fromEdge.text + " to=" + b.toEdge.text;
            edgeAt(b.fromEdge, b.src, rec);
            edgeAt(b.toEdge, b.src, rec);
        }
        for (const Junction::Head& h : j.heads) {
            const std::string rec = "head faces=" + h.edge.text + ":" + dirWord(h.dir);
            const Edge* e = edgeAt(h.edge, h.src, rec);
            if (!e) continue;
            // the head faces the traffic arriving at the junction: the fwd lanes arrive at the edge's to end
            const bool arrives = (h.dir == DIR_FWD) == (e->to.target == node);
            const float L = planLength(*e);
            if (!arrives || lanesAt(*e, h.dir, h.dir == DIR_FWD ? L : 0.f, L) == 0)
                o.error(h.src, "road.legs", what + rec + ": no " + dirWord(h.dir) + " lanes of " + e->id + " arrive at the junction for the head to face");
        }
    }
}

// road.connected: one road network, every piece of it joined to the rest (a node with no edges at all: a warning, except
// the trunk nodes a district's roads may join). The pieces are the model's: an edge that didn't compile still joins its
// nodes, so its own finding doesn't split the network too.
void connected(const Model& m, Out& o) {
    std::vector<int> parent(m.nodes.size());
    for (size_t i = 0; i < parent.size(); i++) parent[i] = (int)i;
    const auto root = [&parent](int x) {
        while (parent[(size_t)x] != x) x = parent[(size_t)x] = parent[(size_t)parent[(size_t)x]];
        return x;
    };
    for (const Edge& e : m.edges) {
        if (e.from.target < 0 || e.to.target < 0) continue;
        const int a = root(e.from.target), b = root(e.to.target);
        if (a != b) parent[(size_t)Max(a, b)] = Min(a, b);
    }
    std::map<int, std::vector<int>> pieces;   // root -> its edges (in the model's order)
    for (size_t ei = 0; ei < m.edges.size(); ei++)
        if (m.edges[ei].from.target >= 0 && m.edges[ei].to.target >= 0) pieces[root(m.edges[ei].from.target)].push_back((int)ei);
    int main = -1;
    size_t most = 0;
    for (const auto& p : pieces)
        if (p.second.size() > most || (p.second.size() == most && main >= 0 && p.second.front() < pieces[main].front())) {
            most = p.second.size();
            main = p.first;
        }
    for (auto& p : pieces) {
        if (p.first == main) continue;
        const Edge& first = m.edges[(size_t)p.second.front()];
        o.error(first.src, "road.connected", "edge " + first.id + (p.second.size() > 1 ? " and " + count(p.second.size() - 1, "other edge", "other edges") : std::string()) +
                                                 " make a piece of road network of their own, joined to none of the rest (whose largest piece has " +
                                                 count(most, "edge", "edges") + ")");
    }
    std::set<int> joins;   // the trunk nodes the districts' roads may join
    for (const District& d : m.districts)
        for (const Ref& r : d.trunkNodes)
            if (r.target >= 0) joins.insert(r.target);
    for (size_t ni = 0; ni < m.nodes.size(); ni++)
        if (m.nodes[ni].edges.empty() && !joins.count((int)ni))
            o.warning(m.nodes[ni].src, "road.connected", "node " + m.nodes[ni].id + " has no edges");
}

// signal.node: an authored plan fits its node: a signalled node, its legs, the approaches its phases name, its offset
void signalNodes(const Model& m, const std::vector<NodeLegs>& legs, Out& o) {
    for (size_t ni = 0; ni < m.nodes.size(); ni++) {
        const Node& n = m.nodes[ni];
        if (n.plan.target < 0 || n.plan.cls != "signal") continue;
        const SignalPlan& p = m.signals[(size_t)n.plan.target];
        const Src at = n.plan.src.file >= 0 ? n.plan.src : n.src;
        const std::string what = "node " + n.id + ": ";
        if (n.control != Node::CTL_SIGNAL) {
            static const char* const kControl[] = {"isn't written", "is none", "is stop", "is yield", "is signal"};
            o.error(at, "signal.node", what + "plan=" + p.id + " is for a node with control=signal, and its control= " + kControl[n.control]);
            continue;
        }
        if (n.offset < 0.f || (p.cycle > 0.f && n.offset >= p.cycle))
            o.error(n.src, "signal.node", what + "offset=" + fmt(n.offset, 1) + " should be within plan " + p.id + "'s cycle of " + fmt(p.cycle, 1) + " s");
        const NodeLegs& N = legs[ni];
        if (!N.complete) continue;
        const int nodeLegs = (int)N.legs.size();
        const std::vector<char> letters = legLetters(n, N);
        std::vector<std::string> missing;   // the legs the plan's phases name and the node doesn't have
        for (const char* c = "nesw"; *c; c++) {
            if (std::find(letters.begin(), letters.end(), *c) != letters.end()) continue;
            bool named = false;
            for (const Phase& ph : p.phases) {
                for (const std::string& mv : ph.moves) named = named || (!mv.empty() && mv[0] == *c);
                for (const std::string& w : ph.walk) named = named || w == std::string(1, *c);
            }
            if (named) missing.push_back(compassWord(*c));
        }
        std::vector<std::string> have;
        for (const char* c = "nesw"; *c; c++)
            for (char l : letters)
                if (l == *c) have.push_back(std::string(1, l));
        const std::string unused = missing.empty() ? std::string()
                                                   : ": its greens and walks for the " + listed(missing) + (missing.size() == 1 ? " leg" : " legs") + " go unused";
        if (p.legs > 0 && nodeLegs > p.legs) {
            o.error(at, "signal.node", what + "plan " + p.id + " suits junctions of " + std::to_string(p.legs) + " legs (legs=" + std::to_string(p.legs) + "), and this one has " +
                                           std::to_string(nodeLegs) + ": a leg it has no phases for gets no green");
        } else if ((p.legs > 0 && nodeLegs < p.legs) || (p.legs == 0 && nodeLegs < 4 && !missing.empty())) {
            // A warning, not an error: a T junction can run a 4-way plan, whose greens for the absent leg never come, and
            // every movement it has still gets its green. The stage-1 pilot runs its signalled T junctions on the 4-way
            // lib.signal.cl_twophase: cl.n0003, n0004 and n0006 on Bougainvillea Boulevard until stage 2 gives them their
            // west legs, and cl.n0039, a T for good. A plan for fewer legs than the junction has stays an error (above).
            o.warning(at, "signal.node", what + "it has " + std::to_string(nodeLegs) + " legs (" + listed(have) + "), and plan " + p.id +
                                             (p.legs > 0 ? " suits junctions of " + std::to_string(p.legs) : std::string(" names more")) + unused);
        } else if (!missing.empty()) {
            o.error(at, "signal.node", what + "plan " + p.id + " gives greens or walks to the " + listed(missing) + (missing.size() == 1 ? " leg" : " legs") +
                                           ", which this node doesn't have (its legs: " + listed(have) + ")");
        }
    }
}

// transit.route and transit.stop: a line drives its route edge to edge, the right way along each, through turns its
// junctions allow, and stops at the kerb on its right
void transitChecks(const Model& m, Out& o) {
    for (const TransitLine& t : m.transit) {
        if (t.routeEdges.empty()) continue;
        const Src routeAt = t.routeEdges.front().src;
        const std::string what = "line " + t.id + ": ";
        // The direction of each pass: +1 fwd (from node to to node), -1 bwd, 0 unknown. The route is walked edge to edge:
        // once it is known where the route enters an edge, it leaves by the other end, and the next edge must start
        // there; a direction once known is never changed. Where it isn't known (the first edge, after a break), the node
        // an edge shares with the next one says where the route leaves it: one shared node does, two (two edges between
        // the same nodes) leave it open until the next pair says.
        std::vector<int> dir(t.routeEdges.size(), 0);
        const auto turnCheck = [&](int a, int b, int node) {
            if (a == b) return;   // (a U-turn on one edge)
            const Junction* j = junctionOf(m, node);
            const std::string turn = "the route turns from " + m.edges[(size_t)a].id + " into " + m.edges[(size_t)b].id + " at " + m.nodes[(size_t)node].id;
            if (banned(j, a, b)) o.error(routeAt, "transit.route", what + turn + ", which its junction bans");
            else if (j && j->turns == "custom") {
                bool connect = false;
                for (const Junction::Connect& c : j->connects) connect = connect || (c.fromEdge.target == a && c.toEdge.target == b);
                if (!connect) o.error(routeAt, "transit.route", what + turn + ", and the junction takes only its connect records (turns=custom), none of them that turn");
            }
        };
        int at = -1;   // the node the route has reached, -1: not known
        for (size_t i = 0; i < t.routeEdges.size(); i++) {
            const int a = t.routeEdges[i].target;
            if (a < 0) {
                at = -1;
                continue;
            }
            const Edge& ea = m.edges[(size_t)a];
            if (at >= 0) {
                const int prev = t.routeEdges[i - 1].target;   // (at >= 0: the edge before is known)
                if (ea.from.target == at || ea.to.target == at) {
                    dir[i] = ea.from.target == at ? 1 : -1;
                    turnCheck(prev, a, at);
                    at = ea.from.target == at ? ea.to.target : ea.from.target;
                    continue;
                }
                o.error(routeAt, "transit.route", what + "the route leaves " + m.edges[(size_t)prev].id + " at " + m.nodes[(size_t)at].id + ", where " + ea.id + " doesn't start");
                at = -1;   // (and this edge's way is read afresh from the next one)
            }
            const int b = i + 1 < t.routeEdges.size() ? t.routeEdges[i + 1].target : -1;
            if (b < 0 || b == a) continue;
            const Edge& eb = m.edges[(size_t)b];
            const bool leaveTo = ea.to.target >= 0 && (ea.to.target == eb.from.target || ea.to.target == eb.to.target);
            const bool leaveFrom = ea.from.target >= 0 && (ea.from.target == eb.from.target || ea.from.target == eb.to.target);
            if (!leaveTo && !leaveFrom) {
                o.error(routeAt, "transit.route", what + "the route goes from " + ea.id + " to " + eb.id + ", which don't meet");
                continue;
            }
            if (leaveTo && leaveFrom) continue;   // (two edges between the same nodes: either way, for now)
            dir[i] = leaveTo ? 1 : -1;
            at = leaveTo ? ea.to.target : ea.from.target;
            // the edge before, if its way was left open (it and this one join the same two nodes): it ends where this
            // one starts
            const int entry = leaveTo ? ea.from.target : ea.to.target;
            const int prev = i > 0 ? t.routeEdges[i - 1].target : -1;
            if (prev >= 0 && prev != a && dir[i - 1] == 0) {
                const Edge& ep = m.edges[(size_t)prev];
                if (ep.from.target == entry || ep.to.target == entry) {
                    dir[i - 1] = ep.to.target == entry ? 1 : -1;
                    turnCheck(prev, a, entry);
                }
            }
        }
        for (size_t i = 0; i < t.routeEdges.size(); i++) {
            const int a = t.routeEdges[i].target;
            if (a < 0 || dir[i] == 0) continue;
            const Edge& e = m.edges[(size_t)a];
            if ((dir[i] > 0 ? e.lanesF : e.lanesB) == 0)
                o.error(routeAt, "transit.route", what + "the route runs " + e.id + " from " + (dir[i] > 0 ? e.from.text : e.to.text) + " to " + (dir[i] > 0 ? e.to.text : e.from.text) +
                                                      ", against its one-way lanes (lanes=" + std::to_string(e.lanesF) + "/" + std::to_string(e.lanesB) + ")");
        }
        for (const TransitLine::Stop& s : t.stops) {
            if (s.busStop.target < 0 || s.busStop.cls != "busstop") continue;
            const BusStop& b = m.busStops[(size_t)s.busStop.target];
            const Edge& e = m.edges[(size_t)b.edge];
            const std::string sw = "stop " + s.id + ": ";
            bool onRoute = false, rightSide = false, known = false;
            for (size_t i = 0; i < t.routeEdges.size(); i++) {
                if (t.routeEdges[i].target != b.edge) continue;
                onRoute = true;
                if (dir[i] == 0) continue;
                known = true;
                rightSide = rightSide || (dir[i] > 0) == (b.side == SIDE_RIGHT);
            }
            if (!onRoute) o.error(s.src, "transit.stop", sw + "bus stop " + b.id + " is on " + e.id + ", which the route doesn't take");
            else if (known && !rightSide)
                o.error(s.src, "transit.stop", sw + "bus stop " + b.id + " is on " + e.id + "'s " + sideWord(b.side) + " kerb, and the line runs along " + e.id + " only from " +
                                                   (b.side == SIDE_RIGHT ? e.to.text + " to " + e.from.text : e.from.text + " to " + e.to.text) +
                                                   ", with that kerb on its left");
            bool listed = b.routes.empty();
            for (const Ref& r : b.routes) listed = listed || r.text == t.id;
            if (!listed) o.warning(s.src, "transit.stop", sw + "bus stop " + b.id + "'s routes= doesn't list " + t.id);
        }
    }
    // a stop's routes= names lines that stop there
    for (const BusStop& b : m.busStops)
        for (const Ref& r : b.routes) {
            if (r.target < 0 || r.cls != "line") continue;
            bool stops = false;
            for (const TransitLine::Stop& s : m.transit[(size_t)r.target].stops) stops = stops || s.busStop.target == m.find("busstop", b.id);
            if (!stops) o.warning(r.src.file >= 0 ? r.src : b.src, "transit.stop", "bus stop " + b.id + ": routes= lists " + r.text + ", which has no stop here");
        }
}

bool inPolygon(vec2 p, const std::vector<vec2>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    return in;
}

// road.overlap: two carriageways on each other outside the junctions, the way tools/traffic_sim --overlaps finds them
// (along A, outside its cut-backs, the nearest point of B's centreline outside its own: within 0.8 of the wider half
// width, running the same way or the opposite one, at grade), or crossing at grade with no node where they cross
void overlaps(const Model& m, const RoadNetwork& net, const RoadIndex& idx, Out& o) {
    std::vector<const std::vector<vec2>*> box(net.nodes.size(), nullptr);   // authored junction boxes
    for (const Junction& j : m.junctions)
        if (j.node.target >= 0 && j.box.size() >= 3) box[(size_t)idx.roadNode[(size_t)j.node.target]] = &j.box;
    const auto inBox = [&](const RoadEdge& e, vec2 p) { return (box[(size_t)e.n0] && inPolygon(p, *box[(size_t)e.n0])) || (box[(size_t)e.n1] && inPolygon(p, *box[(size_t)e.n1])); };
    float widest = 0.f;
    for (const RoadEdge& e : net.edges) widest = Max(widest, e.halfWidth);
    std::vector<vec2> lo(net.edges.size()), hi(net.edges.size());
    for (size_t i = 0; i < net.edges.size(); i++) {
        lo[i] = vec2(1e9f);
        hi[i] = vec2(-1e9f);
        for (const vec3& p : net.edges[i].pts) {
            lo[i] = vmin(lo[i], p.xy());
            hi[i] = vmax(hi[i], p.xy());
        }
    }
    std::vector<int> near;
    for (size_t a = 0; a < net.edges.size(); a++) {
        const RoadEdge& A = net.edges[a];
        net.edgesInRect(lo[a] - vec2(A.halfWidth + widest + 1.f), hi[a] + vec2(A.halfWidth + widest + 1.f), near);
        for (int bi : near) {
            const size_t b = (size_t)bi;
            if (b <= a) continue;
            const RoadEdge& B = net.edges[b];
            const float reach = Max(A.halfWidth, B.halfWidth);
            if (lo[a].x > hi[b].x + reach || lo[b].x > hi[a].x + reach || lo[a].y > hi[b].y + reach || lo[b].y > hi[a].y + reach) continue;
            const Edge &ma = m.edges[(size_t)idx.edgeOf[a]], &mb = m.edges[(size_t)idx.edgeOf[b]];
            float run = 0.f;
            vec2 from(0.f);
            for (float s = A.cut0 + 2.f; s < A.length - A.cut1 - 2.f; s += 1.f) {
                const vec3 p3 = A.posAt(s);
                const vec2 p = p3.xy(), t = normalize(A.tangentAt(s).xy() + vec2(1e-6f, 0.f));
                float best = 1e9f, bz = 0.f, bs = 0.f;
                vec2 bt(0.f), q(0.f);
                for (size_t k = 0; k + 1 < B.pts.size(); k++) {
                    const vec2 q0 = B.pts[k].xy(), q1 = B.pts[k + 1].xy(), d = q1 - q0;
                    const float l2 = Max(dot(d, d), 1e-6f), f = Clamp(dot(p - q0, d) / l2, 0.f, 1.f);
                    const float dd = length(q0 + d * f - p);
                    if (dd < best) {
                        best = dd;
                        bt = d / sqrtf(l2);
                        bz = Lerp(B.pts[k].z, B.pts[k + 1].z, f);
                        bs = Lerp(B.dist[k], B.dist[k + 1], f);
                        q = q0 + d * f;
                    }
                }
                if (bs < B.cut0 + 1.f || bs > B.length - B.cut1 - 1.f) continue;
                if (best > 0.8f * reach || fabsf(dot(bt, t)) < 0.85f || fabsf(bz - p3.z) > kGrade) continue;
                if (inBox(A, p) || inBox(B, q)) continue;
                if (run == 0.f) from = p;
                run += 1.f;
            }
            if (run > kOverlapRun) {
                o.error(ma.src, "road.overlap", "edge " + ma.id + " and edge " + mb.id + " (" + m.where(mb.src) + ") lie on each other for " + fmt(run, 0) + " m from " + xy(from) +
                                                    ": their carriageways overlap outside the junctions");
                continue;
            }
            // centrelines that cross at grade, away from any node they share
            bool crossed = false;
            for (size_t i = 0; i + 1 < A.pts.size() && !crossed; i++)
                for (size_t k = 0; k + 1 < B.pts.size() && !crossed; k++) {
                    const vec2 p0 = A.pts[i].xy(), p1 = A.pts[i + 1].xy(), q0 = B.pts[k].xy(), q1 = B.pts[k + 1].xy();
                    const vec2 r = p1 - p0, sv = q1 - q0;
                    const float den = cross(r, sv);
                    if (fabsf(den) < 1e-9f) continue;
                    const float u = cross(q0 - p0, sv) / den, w = cross(q0 - p0, r) / den;
                    if (u < 0.f || u > 1.f || w < 0.f || w > 1.f) continue;
                    const vec2 x = p0 + r * u;
                    bool shared = false;
                    for (int na : {A.n0, A.n1})
                        if ((na == B.n0 || na == B.n1) && length(net.nodes[(size_t)na].p - x) < 0.5f) shared = true;
                    if (shared) continue;
                    const float za = Lerp(A.pts[i].z, A.pts[i + 1].z, u), zb = Lerp(B.pts[k].z, B.pts[k + 1].z, w);
                    if (fabsf(za - zb) > kGrade) continue;
                    crossed = true;
                    o.error(ma.src, "road.overlap", "edge " + ma.id + " crosses edge " + mb.id + " (" + m.where(mb.src) + ") at " + xy(x) + ", at grade (z " + fmt(za) + " and " +
                                                        fmt(zb) + "), with no node there");
                }
        }
    }
}

void countAuthored(const Model& m, RoadCounts& c) {
    static const char* const kTypes[] = {"lanes",    "lane",    "noswitch", "moves", "crossing", "busstop",  "parkbay", "nostop", "loading", "taxirank",
                                         "curbcut",  "nojaywalk", "deck",   "sign",  "junction", "stopline", "connect", "ban",    "head"};
    std::map<std::string, size_t> n;
    for (const Edge& e : m.edges) {
        n["lanes"] += e.laneCounts.size();
        n["lane"] += e.laneUses.size();
        n["noswitch"] += e.noSwitch.size();
        n["moves"] += e.moves.size();
        n["crossing"] += e.crossings.size();
        n["busstop"] += e.busStops.size();
        for (const KerbStretch& k : e.kerb) n[k.type]++;
        n["curbcut"] += e.curbCuts.size();
        n["deck"] += e.decks.size();
        n["sign"] += e.signs.size();
    }
    for (const Junction& j : m.junctions) {
        n["junction"]++;
        n["stopline"] += j.stopLines.size();
        n["connect"] += j.connects.size();
        n["ban"] += j.bans.size();
        n["head"] += j.heads.size();
    }
    for (const char* t : kTypes)
        if (n[t] > 0) c.authored.push_back(std::make_pair(std::string(t), n[t]));
}

}  // namespace roads_detail

bool fileModelKind(const std::string& kind) { return kind == "district" || kind == "roads" || kind == "signal" || kind == "transit" || kind == "block"; }
bool worldModelKind(const std::string& kind) { return kind == "world" || kind == "district" || kind == "roads" || kind == "signal" || kind == "transit"; }

void checkFileModel(const Hand::Model& m, const std::vector<Hand::Error>& loaderErrors, std::vector<ModelFinding>& out) {
    using namespace roads_detail;
    Out o{out};
    for (const Error& e : loaderErrors)
        if (e.code == "value.form") o.error(e.src, "value.form", e.message);   // (ref.form: worldv's reference checks report it)
    for (const Edge& e : m.edges) {
        if (e.shape.size() < 2) continue;   // (road.shape, in a world run)
        const float L = planLength(e);
        alongChecks(m, e, L, o);
        movesChecks(e, L, o);
        parkingChecks(e, o);
        busStopChecks(m, e, L, o);
    }
    planChecks(m, o);
}

void checkWorldModel(const Hand::Model& m, std::vector<ModelFinding>& out, RoadCounts& counts) {
    using namespace roads_detail;
    Out o{out};
    counts = RoadCounts();
    RoadNetwork net;
    RoadIndex idx;
    std::vector<Error> errors;
    compileRoads(m, net, idx, errors);
    for (const Error& e : errors)   // (road.node: worldv's ref.missing)
        if (e.code == "road.loop" || e.code == "road.shape" || e.code == "road.profile") o.error(e.src, e.code == "road.loop" ? "road.loop" : e.code == "road.shape" ? "road.shape" : "road.profile", e.message);
    const std::vector<NodeLegs> legs = nodeLegs(m, net, idx);
    overlaps(m, net, idx, o);
    moveTurns(m, legs, idx, o);
    sectors(m, legs, o);
    legRefs(m, o);
    connected(m, o);
    signalNodes(m, legs, o);
    transitChecks(m, o);
    counts.nodes = m.nodes.size();
    counts.edges = m.edges.size();
    counts.junctions = m.junctions.size();
    counts.plans = m.signals.size();
    counts.lines = m.transit.size();
    for (const RoadEdge& e : net.edges) counts.km += e.length / 1000.0;
    countAuthored(m, counts);
}

std::string formatRoadCounts(const RoadCounts& c) {
    using namespace roads_detail;
    char km[32];
    snprintf(km, sizeof(km), "%.2f", c.km);
    std::string s = "worldv roads: " + count(c.nodes, "node", "nodes") + ", " + count(c.edges, "edge", "edges") + " (" + km + " km), " +
                    count(c.junctions, "junction", "junctions") + ", " + count(c.plans, "signal plan", "signal plans") + ", " + count(c.lines, "transit line", "transit lines");
    if (!c.authored.empty()) {
        s += "; authored:";
        for (size_t i = 0; i < c.authored.size(); i++) s += (i ? ", " : " ") + c.authored[i].first + " " + std::to_string(c.authored[i].second);
    }
    return s;
}

}  // namespace Validate
}  // namespace World
