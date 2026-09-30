// Road network: generation, graph, spatial queries.
#pragma once
#include "worldmap.h"

namespace World {

enum RoadClass : u8 {
    RC_HIGHWAY = 0,   // grade-separated expressway, 3 lanes each way, median barrier
    RC_BOULEVARD,     // arterial, 2-3 lanes each way, median
    RC_AVENUE,        // 2 lanes each way
    RC_STREET,        // 1 lane each way, parking, sidewalks
    RC_LANE,          // narrow residential
    RC_RURAL,         // 1 lane each way, shoulders, no sidewalk
    RC_DIRT,          // farm / wetland track
    RC_RAMP,          // one-way highway ramp
    RC_COUNT
};

struct RoadClassInfo {
    const char* name;
    float laneWidth;
    int lanes;          // per direction
    float median;       // median width (0 = painted center line)
    float shoulder;     // paved shoulder / parking width per side
    float sidewalk;     // sidewalk width per side in town (0 = none)
    float speed;        // m/s
    bool gradeSeparated;
};
const RoadClassInfo& roadInfo(RoadClass c);

enum RoadFlags : u8 {
    RF_BRIDGE = 1,     // deck above water/ground somewhere along the edge
    RF_ONEWAY = 2,
    RF_ELEVATED = 4,   // on pillars (urban expressway)
    RF_NOSIDEWALK = 8,
    RF_UNPAVED = 16,
};

struct RoadNode {
    vec2 p;
    float z = 0;
    std::vector<int> edges;
    u8 control = 0;    // 0 none, 1 stop signs, 2 traffic lights
    bool highway = false;
    float radius = 0;  // intersection clearing radius
};

struct RoadEdge {
    int n0 = -1, n1 = -1;
    RoadClass cls = RC_STREET;
    u8 flags = 0;
    u8 lanesF = 1, lanesB = 1;  // lanes in direction n0->n1 / n1->n0
    float halfWidth = 4.f;      // paved half width (incl. shoulders/parking)
    float sidewalk = 0.f;
    std::vector<vec3> pts;      // centerline polyline (with z), pts.front() at n0, pts.back() at n1
    std::vector<float> dist;    // cumulative distance
    float length = 0;
    float cut0 = 0, cut1 = 0;   // distance cut back from each node for the intersection area
    u32 seed = 0;
    std::string name;
    // Evaluate position / tangent at distance s along the edge
    vec3 posAt(float s) const;
    vec3 tangentAt(float s) const;
};

struct RoadNetwork {
    std::vector<RoadNode> nodes;
    std::vector<RoadEdge> edges;
    // Spatial hash: 64 m cells -> edge indices
    static constexpr float kHashCell = 64.f;
    int hashRes = 0;
    std::vector<std::vector<int>> hash;

    void generate(WorldMap& map);
    // Nearest edge to p (xy). Returns -1 if none within maxDist.
    int nearestEdge(vec2 p, float maxDist, float* outS = nullptr, float* outDist = nullptr, float* outSide = nullptr) const;
    // Road surface height if p lies on a paved road/intersection/bridge deck; returns false otherwise.
    bool surfaceHeight(vec2 p, float* z, float maxZ = 1e9f) const;
    // True if p is within `margin` meters of any road surface or sidewalk.
    bool nearRoad(vec2 p, float margin) const;
    // True if p lies on the paved width (+ margin) of a road whose surface there is within 2.5 m of z (street furniture filter)
    bool onPavement(vec2 p, float z, float margin, int ignoreEdge = -1, float zTol = 2.5f) const;
    void edgesInRect(vec2 mn, vec2 mx, std::vector<int>& out) const;
    float totalLength(RoadClass c) const;

    void buildHash();
};

extern RoadNetwork* gRoads;

// True inside the closed parking strip of a road-works zone (roadmesh.cpp street dressing); parked-car spawns keep clear.
bool roadWorkZoneAt(vec2 p);

}  // namespace World
