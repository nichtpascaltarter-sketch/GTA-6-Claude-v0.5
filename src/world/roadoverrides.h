// What the hand-built roads author beyond the RoadNetwork, for the lane graph (docs/HANDBUILT_WORLD.md section 12,
// docs/NTW_FORMAT.md 9.4). tools/worldc.cpp compiles it from the world model with the roads (worldmodel.h
// compileRoadOverrides) into world.bin's "roads.overrides" (roadsbin.h); the game reads it back in hand mode
// (gRoadOverrides), and host tools read it with loadRoadsFile. The procedural world has none (nullptr), and the lane
// graph derives everything as it does today.
//
// It grows one list at a time, in the order agreed with the AI agent: EdgeExtra and NodeRule (now); then moves,
// connects, bans and stop lines; signal plans; the kerbside, bus stops, curb cuts and crossings; lane counts, lane uses
// and solid lines; roundabouts. The lists keep these conventions:
// - indices are the RoadNetwork's edges and nodes;
// - a distance along an edge is the RoadEdge's own s, the 3D arc length that e.dist and posAt use, not plan metres;
// - dir 0 is fwd (n0 to n1), 1 bwd; sides are seen looking fwd;
// - lanes count from the left of their direction of travel, from 0, at that end of the edge, after the stretch's
//   pockets and drops;
// - each list is sorted by edge (or node), then dir or side, then from, so world.bin is deterministic;
// - what the compiler can resolve, it resolves: plan moves and walks as edges and turn masks, hours as minutes.
#pragma once
#include "../core/base.h"
#include <vector>

namespace World {

enum RoadTurn : u8 { TURN_LEFT = 1, TURN_STRAIGHT = 2, TURN_RIGHT = 4, TURN_UTURN = 8 };   // masks

// One side's kerb line, its offset from the edge's shape (m): side -1 the left and +1 the right looking fwd. A two-way
// edge's lanes run out from the shape each way, a one-way edge's are centred on it, as the lane graph lays them out
// (src/game/lanes.cpp); beyond the lanes lies that side's kerb strip (shoulder and parking).
inline float roadKerbLine(float laneWidth, float median, int lanesF, int lanesB, float kerb, int side) {
    if (lanesF > 0 && lanesB > 0) return laneWidth * (float)(side > 0 ? lanesF : lanesB) + median * 0.5f + kerb;
    return laneWidth * (float)(lanesF + lanesB) * 0.5f + median * 0.5f + kerb;
}

struct RoadOverrides {
    // One per RoadNetwork edge, by edge index: its widths and limit as built, which RoadNetwork's symmetric halfWidth
    // and sidewalk can't hold. Every value is the one to use: as written in roads.ntw, else the class's (roadInfo).
    // RoadNetwork's halfWidth is the wider side's kerb line (roadKerbLine), and its sidewalk the wider of sidewalkL and
    // sidewalkR. Each side's own are the RoadEdge's halfWidthSide and sidewalkSide (roadsbin.h applyRoadSides).
    struct EdgeExtra {
        float speed = 0.f;                 // m/s
        float laneWidth = 0.f;             // m
        float median = 0.f;                // m; 0: a painted centre line
        float kerbL = 0.f, kerbR = 0.f;    // m of paved width beyond the outer lane on each side, seen looking fwd:
                                           // shoulder= plus parking= where either is written, else the class's shoulder
        float parkingL = -1.f, parkingR = -1.f;   // m of that kept for parking, along the kerb; 0: none (parking=none,
                                           // the other side's, or a shoulder= alone); -1: neither written (parkingOn)
        float sidewalkL = 0.f, sidewalkR = 0.f;   // m; 0: none
        u8 flags = 0;                      // EX_*
        // The parking along one side (m of the kerb strip, at the kerb): as written; where nothing is (-1), the whole
        // strip when it is 1.8 m or more, as the lane graph parks today
        float parkingOn(int side) const {
            const float p = side > 0 ? parkingR : parkingL, kerb = side > 0 ? kerbR : kerbL;
            return p >= 0.f ? p : (kerb >= 1.8f ? kerb : 0.f);
        }
    };
    enum : u8 {
        EX_MEDIAN_BARRIER = 1,             // median=W:barrier: no U-turns, no crossing it on foot, no jaywalking across
        EX_TUNNEL = 2,                     // flags=tunnel
    };

    // A node's rules as written in roads.ntw, one per node that writes control=, stops=, yield=, noredturn= or bulb=,
    // sorted by node. Where a node has none, the lane graph derives its control as today (RoadNode::control).
    enum : u8 { CTRL_AUTO = 0, CTRL_NONE, CTRL_STOP, CTRL_YIELD, CTRL_SIGNAL };   // NodeRule::control
    struct NodeRule {
        int node = -1;                     // RoadNetwork node
        u8 control = CTRL_AUTO;            // CTRL_*: control= as written
        u8 flags = 0;                      // NR_*
        float bulb = 0.f;                  // a dead end's turning-circle radius (m); 0: none written, so long vehicles
                                           // avoid the dead end
        std::vector<int> stops;            // the approaches (RoadNetwork edges, sorted) that stop; with NR_STOPS_ALL, all
        std::vector<int> yields;           // the approaches that give way
        std::vector<int> noRedTurn;        // the approaches where turning right on red isn't allowed
    };
    enum : u8 {
        NR_STOPS_ALL = 1,                  // stops=all, or control=stop without a list: every approach stops
    };

    std::vector<EdgeExtra> edges;          // empty, or one per RoadNetwork edge
    std::vector<NodeRule> nodes;

    bool empty() const { return edges.empty() && nodes.empty(); }
};

// The game's copy in hand mode, beside gRoads (nullptr in the procedural world). Inline, so whatever links the lane
// graph links it, with or without roadsbin.cpp
inline const RoadOverrides* gRoadOverrides = nullptr;

}  // namespace World
