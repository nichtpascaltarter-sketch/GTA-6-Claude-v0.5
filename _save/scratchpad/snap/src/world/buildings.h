// Building lots and specifications for the whole map (generated at startup; meshes per cell).
#pragma once
#include "roads.h"

namespace World {

enum BuildingStyle : u8 {
    BS_TOWER = 0,     // podium + tower + crown
    BS_MIDRISE,       // 4-12 floors, punched windows
    BS_CONDO,         // balconies
    BS_DECO,          // art deco hotel / apartments with neon
    BS_SHOPS,         // 1-3 floor retail with storefront + signs
    BS_STRIPMALL,     // long low retail set back behind parking
    BS_HOUSE,         // suburban house with pitched roof
    BS_VILLA,         // Mediterranean mansion
    BS_WAREHOUSE,     // big industrial box
    BS_FACTORY,       // industrial with chimneys/tanks
    BS_FARMHOUSE,     // rural house + metal roof
    BS_BARN,
    BS_MOTEL,         // two-storey motel with exterior walkway
    BS_GASSTATION,
    BS_GARAGE,        // parking garage
    BS_CHURCH,
    BS_SHACK,         // stilt / fishing shack
    BS_COUNT
};

enum RoofType : u8 { ROOF_FLAT = 0, ROOF_HIP, ROOF_GABLE, ROOF_SHED, ROOF_BARREL };

// GPU facade record (matches FacadeGPU in shaders/facade.hlsli)
struct FacadeGPU {
    float floorH, groundH, bayW, winW;
    float winH, sillH, roomDepth, style;
    u32 wallColor, frameColor, glassColor, flags;
    float wallLayer, litFrac;
    u32 seed;
    float signIndex;
};

struct Building {
    vec2 c;          // footprint center
    vec2 ax;         // unit axis along the street (footprint local x)
    float hx, hy;    // half extents (x along street, y depth)
    float baseZ;     // ground floor level
    float height;    // main mass height
    u16 floors;
    u8 style;
    u8 roof;
    u32 seed;
    u32 facade;      // facade record index
    vec2 front;      // unit direction toward the street
    u8 region;
    u8 lotKind;      // 0 normal, 1 corner
    float lotHy;     // lot depth half-extent (for yards/parking)
    vec2 lotC;       // lot center
    u32 roofColor;
    float lotHx = 0; // lot half-width along ax (gardens, hedges)
    i16 interior = -1;  // enterable interior hosted on the ground floor (world/interiors.h), -1 none
};

// A facade-covered mass of a building mesh (main block, podium, tower tier, deco tower, house body), recorded by
// buildmesh.cpp so the street-level detail pass (facadedetail.cpp) can align moldings with the shader's window grid.
enum FacadeMassKind : u8 { FM_MAIN = 0, FM_PODIUM, FM_TIER, FM_DECO_TOWER, FM_HOUSE };
struct FacadeMass {
    std::vector<vec2> fp;  // CCW footprint
    float z0, z1;          // facade wall span (z1 = roof level, parapet excluded)
    float vBase;           // facade v origin (building base)
    u8 kind;
    bool parapet;
};

struct BuildingSet {
    std::vector<Building> buildings;
    std::vector<FacadeGPU> facades;
    std::vector<std::vector<int>> cellLists;  // per streaming cell: building indices (by center)
    std::vector<std::string> signNames;       // shop sign texts (index = sign slot)
    void generate(WorldMap& map, const RoadNetwork& roads);
    void addSiteBuilding(WorldMap& map, const struct SiteBuildingReq& q, u32 seed);
    // Collision query: returns true if p (xy) is inside any building footprint (with margin)
    bool pointInBuilding(vec2 p, float margin, float* topZ = nullptr) const;
    void buildingsNear(vec2 p, float r, std::vector<int>& out) const;
};

extern BuildingSet* gBuildings;

}  // namespace World
