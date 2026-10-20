// Special sites and landmarks: Porto Sol International airport, Port Isle container terminal, Key Coral,
// signature landmarks, billboards and countryside details. The layout is decided once at world generation
// (terrain shaping, drivable pads, lot/road exclusion zones, element list); meshes are built per streaming cell.
#pragma once
#include "roads.h"

namespace World {

// Airport fence rectangle (also defines REG_AIRPORT exactly) and main airfield lines.
constexpr float kAirportX0 = -1320.f, kAirportX1 = 960.f, kAirportY0 = 544.f, kAirportY1 = 2320.f;
// Port Isle quay outline: a man-made rectangular island (REG_PORT).
constexpr float kPortX0 = 3960.f, kPortX1 = 4600.f, kPortY0 = -1048.f, kPortY1 = 104.f;

// Flat (or evenly sloped) drivable surfaces that are not part of the road graph: runways, taxiways, aprons,
// container yards, plazas, piers. They feed RoadNetwork::surfaceHeight so vehicles, planes and peds stand on them.
enum PadKind : u8 {
    PAD_RUNWAY = 0, PAD_SHOULDER, PAD_TAXIWAY, PAD_APRON, PAD_SERVICE, PAD_PARKING, PAD_YARD, PAD_PLAZA, PAD_DECK,
    PAD_TURF, PAD_RAMP, PAD_SAND, PAD_COUNT
};

struct Pad {
    vec2 c, ax;        // center, unit axis
    float hx, hy;      // half extents along ax / perp(ax)
    float z;           // surface height at the center
    float slope;       // dz per meter along ax (boat ramps)
    u8 kind;
    u8 drawn;          // 1 = generic pad renderer draws the surface, 0 = owner element draws it
    u8 skirt;          // 1 = draw edge skirts down to the terrain
    u8 flags;          // 1 = walkable only (no vehicle surface), 2 = raised curb edge
    u32 color;         // vertex tint for the surface
    float heightAt(vec2 p) const { return z + dot(p - c, ax) * slope; }
    bool contains(vec2 p, float margin = 0.f) const {
        vec2 d = p - c;
        return fabsf(dot(d, ax)) <= hx + margin && fabsf(dot(d, perp(ax))) <= hy + margin;
    }
};

// Element kinds (generators live in airport.cpp, port.cpp, landmarks.cpp, keycoral.cpp).
enum SiteKind : u16 {
    // Airport
    SK_RUNWAY = 0, SK_TAXI_MARKS, SK_APRON_MARKS, SK_TERMINAL, SK_CONCOURSE, SK_CONTROL_TOWER, SK_HANGAR, SK_FUEL_FARM,
    SK_AIRLINER, SK_SMALL_PLANE, SK_HELICOPTER, SK_HELIPAD, SK_FENCE, SK_APPROACH_LIGHTS, SK_FLOOD_MAST, SK_WINDSOCK,
    SK_FIRE_STATION, SK_GSE, SK_AIRPORT_SIGN, SK_PARKING_MARKS, SK_GARAGE_RAMP, SK_FORECOURT,
    // Port
    SK_QUAY = 40, SK_CONTAINER_BLOCK, SK_STS_CRANE, SK_STRADDLE, SK_SHIP, SK_RAIL, SK_RAIL_YARD, SK_PORT_GATE, SK_YARD_MAST,
    SK_RMG_CRANE, SK_REEFER_RACK, SK_PORT_TRUCK, SK_REACH_STACKER, SK_PORT_DRESS,
    // Key Coral / marinas / beaches
    SK_MARINA = 80, SK_BOAT, SK_GOLF_HOLE, SK_GOLF_POND, SK_BEACH_CLUB, SK_CLUBHOUSE,
    // Landmarks
    SK_SOLARIS = 100, SK_STADIUM, SK_BEACH_PIER, SK_FERRIS_WHEEL, SK_COASTER, SK_DROP_TOWER, SK_CAROUSEL, SK_SWING_RIDE,
    SK_LIGHTHOUSE, SK_RADIO_MAST, SK_SUGAR_MILL, SK_PARK, SK_FOUNTAIN, SK_CITY_HALL, SK_BILLBOARD, SK_FISH_SHACK, SK_DOCK,
    SK_SILO, SK_WINDMILL, SK_BOARDWALK, SK_OBS_TOWER, SK_BOAT_RAMP, SK_RIVER_MARINA, SK_PIER_GAMES,
    // Public transit (transit.cpp / transitmesh.cpp)
    SK_METRO_VIADUCT = 160, SK_METRO_STATION, SK_BUS_STOP, SK_FERRY_PIER, SK_TRAM_TRACK, SK_TRAM_STOP,
    SK_COUNT_MAX
};

struct SiteElem {
    u16 kind = 0;
    u16 variant = 0;
    u32 seed = 0;
    vec2 c, ax = vec2(1, 0);   // anchor (owner cell = cell containing c) + orientation
    float hx = 1, hy = 1;      // half extents of the footprint (used for bounds)
    float z = 0;               // base height
    float h = 0;               // overall height
    vec2 a, b;                 // segment endpoints for linear elements (runways, fences, rails)
    float p[8] = {};           // kind-specific parameters
    std::vector<vec2> pts;     // polyline for path-like elements (rail lines, boardwalks)
    std::string text;          // names / signage
    float radius() const { return sqrtf(hx * hx + hy * hy); }
    bool isLine() const { return length2(b - a) > 1e-6f; }
};

// Axis-aligned or oriented exclusion rectangle
struct SiteRect {
    vec2 c, ax;
    float hx, hy;
    bool contains(vec2 p, float margin = 0.f) const {
        vec2 d = p - c;
        return fabsf(dot(d, ax)) <= hx + margin && fabsf(dot(d, perp(ax))) <= hy + margin;
    }
};

// A generic building the site layout asks the building system to create (garages, cargo sheds, offices, clubhouses)
struct SiteBuildingReq {
    vec2 c, ax;
    float hx, hy;
    u8 style;       // BuildingStyle
    u8 roof;        // RoofType
    u16 floors;
    vec2 front;     // unit direction the main facade faces
    float baseZ;    // < -100: use terrain
    u8 region;
};

// A road the site layout adds to the network (airport loop, perimeter road, port roads, Key Coral streets)
struct SiteRoad {
    std::vector<vec2> pts;
    u8 cls;          // RoadClass
    u8 flags;        // RoadFlags (RF_ONEWAY keeps the authored direction)
    int layer;       // 0 at grade
    std::string name;
};

struct SiteSet {
    std::vector<Pad> pads;
    std::vector<SiteElem> elems;
    std::vector<SiteRect> lotBlocks;     // no generic building lots
    std::vector<SiteRect> roadBlocks;    // no city grid roads
    std::vector<SiteRect> vegBlocks;     // no natural vegetation scatter
    std::vector<SiteBuildingReq> buildingReqs;
    std::vector<SiteRoad> roads;
    std::vector<std::vector<int>> cellElems;  // per streaming cell: elements whose bounds overlap it
    std::vector<int> farCells;                // cells holding tall landmarks (kept as far-LOD cells out to farRange)
    float farRange = 8000.f;
    float airportZ = 4.f, portZ = 3.f;
    // Named anchor points (missions, road generation)
    vec2 airportBoulevardEnd;                // west end of Airport Boulevard at the terminal loop
    bool generated = false;

    // Layout pass: at the end of WorldMap::generate (before roads). Shapes terrain, places pads and elements.
    void layout(WorldMap& map);
    // Facade records (procedural windows) for landmark buildings; indices stored in SiteElem::p[7]
    void makeFacades(struct BuildingSet& bs);
    // Final pass after roads and buildings exist: billboards along highways, farm extras; builds per-cell lists.
    void finalize(WorldMap& map, const RoadNetwork& roads, const struct BuildingSet& bs);

    bool padHeight(vec2 p, float* z, float maxZ) const;
    const Pad* padAt(vec2 p) const;
    // True if an open-air parking deck pad covers p at roof height roofZ (garage roofs finished by the site generator)
    bool roofDeckAt(vec2 p, float roofZ) const;
    bool blocksLots(vec2 p) const;
    bool blocksRoads(vec2 p) const;
    bool blocksVegetation(vec2 p) const;
    int addPad(const Pad& p);
    int addElem(const SiteElem& e);

    // Spatial hash for pads (64 m cells)
    static constexpr float kPadCell = 64.f;
    int padRes = 0;
    std::vector<std::vector<int>> padHash;
    void buildPadHash();
    std::vector<std::vector<int>> rectHash[3];
    void buildRectHash();
    bool rectHit(int which, vec2 p, float margin) const;
};

extern SiteSet* gSites;

struct CellGeometry;
// Mesh generation for all site elements touching a streaming cell (sitecell.cpp)
void buildSiteCell(int cx, int cy, bool detail, CellGeometry& out);
// Cells whose far LOD should stream beyond the normal far radius (skyline landmarks); range in meters
const std::vector<int>& siteFarCells(float* range);

}  // namespace World
