// DRAFT for the AI agent's review (city agent): what the hand-built roads author beyond RoadNetwork, for
// LaneGraph::build to read. Everything here overrides a derivation where it is written, or adds what nothing derives
// (docs/NTW_FORMAT.md 9.4, docs/HANDBUILT_WORLD.md section 12). worldc compiles it from the world model with the roads
// and writes it to world.bin ("roads.overrides"); the game reads it back in hand mode beside the RoadNetwork. An empty
// RoadOverrides is today's behaviour, which is what the procedural world passes.
//
// Indices are the RoadNetwork's (edges, nodes). Distances along an edge are metres from its n0, along its centreline
// in plan (as authored). Directions: 0 fwd (n0 -> n1), 1 bwd. Sides: 0 left, 1 right, 2 both, seen looking fwd. Lanes
// are counted from the left of their direction of travel, from 0.
#pragma once
#include "roads.h"
#include <string>
#include <vector>

namespace World {

enum RoadTurn : u8 { TURN_LEFT = 1, TURN_STRAIGHT = 2, TURN_RIGHT = 4, TURN_UTURN = 8 };   // masks

struct RoadOverrides {
    // ---------------------------------------------------------------------------------------------- per edge
    struct LaneCount {                 // lanes: one direction's count over a stretch (pockets, merges, drops)
        int edge = -1;
        u8 dir = 0;
        u8 count = 0;
        i8 add = -1, drop = -1;        // the side that gains or loses the lane over the taper, -1: none
        float from = 0.f, to = -1.f;   // to -1: the end
        float taper = 0.f;
    };
    struct LaneUse {                   // lane: kept for one use over a stretch
        int edge = -1;
        u8 dir = 0;
        i8 index = -1;                 // -1: the outermost lane on `side`
        u8 side = 0;
        u8 use = 0;                    // 0 general, 1 bus, 2 streetcar, 3 bike
        float from = 0.f, to = -1.f;
    };
    struct NoSwitch {                  // solid lines: no lane changes
        int edge = -1;
        u8 dir = 0;
        float from = 0.f, to = -1.f;
    };
    struct Moves {                     // the turns each arriving lane may make, from the left
        int edge = -1;
        u8 atEnd = 1;                  // 1: the lanes arriving at n1 (fwd); 0: at n0 (bwd)
        std::vector<u8> lanes;         // RoadTurn masks
    };
    struct Crossing {
        int edge = -1;
        float at = 0.f;
        u8 kind = 0;                   // 0 signal, 1 zebra, 2 plain, 3 unmarked
        float w = 0.f;                 // 0: derived
    };
    struct BusStop {
        int edge = -1;
        float at = 0.f;
        u8 side = 1;
        u8 kind = 0;                   // 0 bus, 1 streetcar
        std::string id;                // world-wide: transit stops name it
        std::vector<vec2> wait;        // the waiting area (empty: derived)
        float waitRadius = 0.f;        // one wait point with a radius
        float clear = 0.f;             // streetcars: the kerb lane kept clear, m
    };
    struct Kerb {                      // a stretch of one kerb
        int edge = -1;
        u8 type = 0;                   // 0 parkbay, 1 nostop, 2 loading, 3 taxirank, 4 nojaywalk
        u8 side = 1;                   // nojaywalk may be 2 (both)
        u8 kind = 0;                   // parkbay: 0 parallel, 1 angle, 2 perp; nostop: why (0 other, 1 junction, 2 crossing,
                                       // 3 hydrant, 4 busstop, 5 driveway, 6 bridge)
        u8 meter = 0, disabled = 0;
        u16 cap = 0;                   // taxirank
        float from = 0.f, to = 0.f;
        std::string hours;             // loading: "6.00-11.00" (empty: always)
    };
    struct CurbCut {                   // a driveway: implies 5 m of nostop across it
        int edge = -1;
        float at = 0.f, w = 0.f;
        u8 side = 1;
    };
    struct Sign {
        int edge = -1;
        float at = 0.f;
        u8 side = 1;
        u8 kind = 0;                   // 0 stop, 1 yield, 2 speed, 3 oneway, 4 noentry, 5 name
        std::string value;
    };
    struct EdgeExtra {                 // what RoadNetwork's symmetric widths and class defaults leave out; one per edge
        float speed = 0.f;             // m/s; 0: the class's
        float laneWidth = 0.f;         // 0: the class's
        float median = -1.f;           // -1: the class's
        bool medianBarrier = false;    // stops U-turns, crossing on foot and jaywalking
        float kerbL = -1.f, kerbR = -1.f;           // paved width beyond the lanes per side (shoulder + parking); -1 the class's
        float sidewalkL = -1.f, sidewalkR = -1.f;   // -1: the class's
        bool tunnel = false;
    };

    // ---------------------------------------------------------------------------------------------- per node
    struct NodeRule {                  // one per node that has any of these
        int node = -1;
        u8 control = 0;                // as written: 0 auto (derived), 1 none, 2 stop, 3 yield, 4 signal
        int plan = -1;                 // signal plan index in `plans`; -1: derived (auto)
        float offset = 0.f;            // s into the plan's cycle
        std::vector<std::pair<char, int>> approaches;   // compass letter -> edge (where the bearings leave doubt)
        bool stopsAll = false;
        std::vector<int> stops, yields;   // the approaches (edges) that stop / give way
        float bulb = 0.f;              // a dead end's turning-circle radius; 0: none (long vehicles avoid it)
    };
    struct Junction {                  // a junction record: its node's
        int node = -1;
        u8 kind = 0;                   // 0 plain, 1 roundabout (yield at entry, ring lanes circulate)
        u8 turns = 0;                  // 0 all, 1 no_uturn, 2 custom (only the connects)
        std::vector<vec2> box;         // empty: derived from the cut-backs
        struct StopLine { int edge = -1; float at = 0.f; };   // metres back from the junction
        struct Connect { int fromEdge = -1, fromLane = 0, toEdge = -1, toLane = 0; u8 turn = 0; bool isProtected = false; };
        struct Ban { int fromEdge = -1, toEdge = -1; };
        struct Head { vec3 at = vec3(0.f); int edge = -1; u8 dir = 0; };   // a signal head facing that edge's traffic
        std::vector<StopLine> stopLines;
        std::vector<Connect> connects;
        std::vector<Ban> bans;
        std::vector<Head> heads;
    };

    // ---------------------------------------------------------------------------------------------- signal plans
    struct Phase {
        float green = 0.f, amber = 0.f, allred = 0.f;
        std::vector<std::string> moves;   // as written: <approach>_<turn>[p] (n_s, e_lp: p protected)
        std::vector<std::string> walk;    // crossings by the leg they cross (e)
        float walkTime = 0.f, flash = 0.f;
    };
    struct Plan {
        std::string id;                // lib.signal.<name>
        float cycle = 0.f;
        int legs = 0;
        std::vector<Phase> phases;
    };

    std::vector<LaneCount> laneCounts;
    std::vector<LaneUse> laneUses;
    std::vector<NoSwitch> noSwitch;
    std::vector<Moves> moves;
    std::vector<Crossing> crossings;
    std::vector<BusStop> busStops;
    std::vector<Kerb> kerb;
    std::vector<CurbCut> curbCuts;
    std::vector<Sign> signs;
    std::vector<EdgeExtra> edges;      // empty, or one per RoadNetwork edge
    std::vector<NodeRule> nodes;
    std::vector<Junction> junctions;
    std::vector<Plan> plans;

    bool empty() const {
        return laneCounts.empty() && laneUses.empty() && noSwitch.empty() && moves.empty() && crossings.empty() && busStops.empty() && kerb.empty() &&
               curbCuts.empty() && signs.empty() && edges.empty() && nodes.empty() && junctions.empty() && plans.empty();
    }
};

// The game's copy in hand mode (nullptr in the procedural world), beside gRoads
extern const RoadOverrides* gRoadOverrides;

// For the AI agent to decide: the entry point. A suggestion: LaneGraph::build(const RoadNetwork& rn, const RoadOverrides*
// ov = nullptr), each list read where it overrides a derivation.

}  // namespace World
