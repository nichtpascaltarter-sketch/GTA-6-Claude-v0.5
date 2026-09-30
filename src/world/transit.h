// Public transit network of Porto Sol: the SkyLine elevated metro loop, city bus routes and the bay ferries.
// The layout is decided at world generation through the site system (sites.h): the viaduct corridor and the station
// footprints are reserved before roads and buildings exist (transitLayout), the vertical profile, piers, bus stops and
// ferry piers are resolved once roads and buildings exist (transitFinalize), and every piece of geometry is built per
// streaming cell (transitmesh.cpp). Gameplay (trains, buses, ferries, riders) lives in game/transit_game.cpp.
//
// Corridor frame: s = arc length along the loop centerline (counter-clockwise), right = perp(tangent) rotated -90
// degrees (the outside of the loop). Tracks run at lateral +-kTrackOffset: the outer track (right, lateral > 0) carries
// the counter-clockwise service, the inner track (left) the clockwise one (right-hand running).
#pragma once
#include "sites.h"
#include "buildings.h"

namespace World {

namespace transit_dims {
constexpr float kTrackOffset = 2.0f;      // track centerline lateral offset from the corridor centerline
constexpr float kGauge = 1.435f;          // rail spacing
constexpr float kDeckHalf = 4.8f;         // viaduct deck top half width
constexpr float kDeckTopBelowRail = 0.55f;  // deck top surface below the top of rail
constexpr float kGirderDepth = 2.0f;      // box girder depth below the deck top
constexpr float kParapetH = 1.1f;         // parapet height above the deck top
constexpr float kPlatformAboveRail = 1.05f;   // platform surface above the top of rail
constexpr float kPlatformEdge = 3.52f;    // lateral offset of the platform edge (car half width 1.45 + gap)
constexpr float kPlatformOuter = 7.3f;    // lateral offset of the platform's back wall
constexpr float kStationHalf = 7.6f;      // station deck half width
constexpr float kClearance = 13.4f;       // top of rail above street level on normal spans
constexpr float kCarLength = 18.0f;       // car body length
constexpr float kCarPitch = 18.6f;        // coupler to coupler
constexpr int kCarsPerTrain = 3;
constexpr float kPlatformHalfLen = 38.f;  // platform half length (3 cars = 55.8 m + margins)
}  // namespace transit_dims

struct MetroStation {
    std::string name;       // display name ("Civic Center")
    std::string code;       // short code for signs ("CVC")
    float s = 0.f;          // corridor arc length of the platform center
    vec2 pos, dir;          // platform center (xy) and corridor tangent there
    float railZ = 0.f;      // top of rail (flat along the platform)
    float streetZ = 0.f;    // mean street level around the station
    // exits: per platform side (0 = outer / right, 1 = inner / left) the platform end whose stair descends to the street
    // (-1 = toward lower s, +1 = toward higher s)
    int exitEnd[2] = {-1, 1};
    float dwell = 20.f;     // scheduled dwell (s)
    u32 seed = 0;
    // world position of a point in the station frame (along = s offset from the center, lateral = right offset, z absolute)
    vec3 local(float along, float lateral, float z) const { return vec3(pos + dir * along + vec2(dir.y, -dir.x) * lateral, z); }
    vec2 right() const { return vec2(dir.y, -dir.x); }
    float platformZ() const { return railZ + transit_dims::kPlatformAboveRail; }
};

// A pier (column + cap) or a straddle bent carrying the viaduct
struct MetroPier {
    float s = 0.f;
    float groundZ = 0.f;
    float lateral = 0.f;    // column offset from the centerline (single columns) / half span between the legs (bents)
    bool bent = false;      // two legs outside a road with a cross beam
    bool station = false;   // wide station pier
};

struct MetroLine {
    std::string name = "SkyLine";
    float ds = 2.f;                    // sample spacing (m)
    float length = 0.f;                // loop length
    std::vector<vec2> p;               // centerline samples (xy), p[i] at s = i * ds, closed loop (p[n] == p[0] implied)
    std::vector<vec2> t;               // unit tangents
    std::vector<float> k;              // signed curvature (1/m, + = turning left)
    std::vector<float> z;              // top of rail
    std::vector<float> bank;           // superelevation (rad, + = right / outer side raised)
    std::vector<float> ground;         // street level under the centerline
    std::vector<MetroStation> stations;   // in increasing s
    std::vector<MetroPier> piers;         // in increasing s
    int count() const { return (int)p.size(); }
    float wrap(float s) const {
        if (length <= 0.f) return 0.f;
        s = fmodf(s, length);
        return s < 0.f ? s + length : s;
    }
    // Interpolated centerline frame at arc length s (wraps around the loop)
    void frame(float s, vec2& pos, vec2& tan, float* zOut = nullptr, float* bankOut = nullptr, float* curvOut = nullptr) const;
    vec3 center(float s) const;
    // Top-of-rail point at lateral offset (banked deck plane)
    vec3 railPoint(float s, float lateral) const;
    float railZ(float s) const;
    // Station whose platform zone contains s (margin extends the zone), -1 if none
    int stationAt(float s, float margin = 0.f) const;
    // Signed distance along the loop from a to b in (-L/2, L/2]
    float delta(float a, float b) const {
        float d = wrap(b - a);
        return d > length * 0.5f ? d - length : d;
    }
    // Closest point on the centerline to p (xy): returns s, optionally the signed lateral offset (+ right)
    float project(vec2 q, float* lateral = nullptr, float* dist = nullptr) const;
};

// ---- buses ---------------------------------------------------------------------------------------------------------
struct BusStop {
    std::string name;
    vec2 pos;               // shelter center (on the sidewalk)
    vec2 face;              // unit direction from the shelter toward the curb
    vec2 along;             // unit direction of travel of the buses serving it
    vec2 curb;              // point on the curb lane where the bus doors stop
    float z = 0.f;          // sidewalk level
    int edge = -1;          // road edge it stands on
    float edgeS = 0.f;
    u32 routeMask = 0;      // routes serving it
    u32 seed = 0;
};

struct BusRoute {
    std::string number;     // "12"
    std::string name;       // "Bayshore"
    vec3 color;             // route colour (linear)
    std::vector<int> stops; // stop indices in service order (loop: the last stop connects back to the first)
    float headway = 360.f;  // seconds between buses
    int buses = 3;
};

// ---- ferries -------------------------------------------------------------------------------------------------------
struct FerryPier {
    std::string name;
    vec2 base;              // landward end of the pier (on the shore)
    vec2 dir;               // unit direction from the shore out to the berth
    float length = 40.f;    // pier length
    float deckZ = 2.2f;     // pier deck height
    vec2 berth;             // where the ferry's bow door / side gate stops
    float berthYaw = 0.f;   // ferry heading while docked (radians, 0 = +Y)
    float groundZ = 0.f;
    u32 seed = 0;
};

struct FerryRoute {
    std::string name;
    std::vector<int> piers;                 // service order (the route runs back and forth)
    std::vector<std::vector<vec2>> legs;    // water path from piers[i] to piers[i + 1] (dense, ~20 m)
    float cruise = 11.f;                    // m/s
    float dwell = 25.f;
};

struct TransitNet {
    MetroLine metro;
    std::vector<BusStop> busStops;
    std::vector<BusRoute> busRoutes;
    std::vector<FerryPier> piers;
    std::vector<FerryRoute> ferries;
    // corridor reservation (xy): capsules along the centerline + station footprints, used to keep buildings out
    std::vector<vec2> corridor;             // coarse centerline polyline (closed)
    float corridorHalf = 7.f;
    bool laidOut = false, ready = false;
};

extern TransitNet* gTransit;

// ---- world generation hooks ----------------------------------------------------------------------------------------
// SiteSet::layout (before roads): corridor, stations, lot / vegetation reservations
void transitLayout(SiteSet& S, WorldMap& map);
// SiteSet::finalize (roads and buildings exist, before the per-cell element lists): vertical profile, piers, bus stops,
// ferry piers and the site elements that build the geometry
void transitFinalize(SiteSet& S, WorldMap& map, const RoadNetwork& net, const BuildingSet& bs);
// BuildingSet::generate (before its per-cell lists): removes buildings whose footprint reaches into the corridor
void transitPruneBuildings(std::vector<Building>& buildings);

}  // namespace World
