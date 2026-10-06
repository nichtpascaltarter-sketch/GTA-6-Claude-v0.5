// Building lots and specifications for the whole map (generated at startup; meshes per cell).
#pragma once
#include "roads.h"
#include <functional>

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

// Architectural archetype of an ordinary building (blockstyle.cpp): a district palette picks one per lot, within the
// building's style (BuildingStyle stays what gameplay reads: shops, homes, warehouses). It drives the massing, roof form,
// facade record and street-level details. AR_NONE keeps the plain generator (interior hosts, site buildings).
enum BuildingArch : u8 {
    AR_NONE = 0,
    // shops and main streets
    AR_SHOP_TAXPAYER,     // one-storey row of shops, stepped parapet, awnings
    AR_SHOP_1920,         // 1920s two-storey commercial block: cornice, upper windows, transoms
    AR_SHOP_MED,          // Mediterranean revival: cream / ochre stucco, barrel-tile pent roof, arched openings
    AR_SHOP_MIMO,         // Miami Modern: cantilevered canopy slab, angled fascia, accent colours
    AR_SHOP_BODEGA,       // corner bodega: one storey, mural on the side wall, gates, signs
    AR_SHOP_ARCADE,       // two-storey with a colonnaded arcade over the sidewalk frontage
    AR_SHOP_GALLERY,      // island town: wood two-storey with a covered upper gallery (Key Solano, Ten Palms)
    // apartments and offices
    AR_MID_WALKUP,        // brick / painted masonry walk-up, fire escapes, cornice, water tank
    AR_MID_MIMO,          // MiMo garden apartments: L / U around a courtyard, open walkways, eave slab
    AR_MID_MED,           // Mediterranean apartments: tile hip roof, a taller corner tower
    AR_MID_OFFICE60,      // 1960s office slab: ribbon windows, recessed ground floor on pilotis, fins
    AR_MID_MODERN,        // contemporary mixed use: podium, balconies, terraced setback top
    AR_MID_LOFT,          // warehouse loft: brick, big windows, glass rooftop addition
    // condos and hotels
    AR_CONDO_SLAB,        // 1970s slab: continuous balconies, service core on the roof
    AR_CONDO_GLASS,       // 2000s glass condo: rounded ends, glass railings, parking podium
    AR_CONDO_MIMO,        // 1950s resort hotel: curved / stepped slab, entrance canopy, rooftop sign
    AR_CONDO_PODIUM,      // tower on a screened parking podium
    // houses
    AR_HOUSE_RANCH,       // long low ranch house, low hip roof
    AR_HOUSE_BUNGALOW,    // small house with a front-gabled porch
    AR_HOUSE_MED,         // Mediterranean house: tile roof, arched entry, small tower
    AR_HOUSE_TWO,         // two-storey colonial box
    AR_HOUSE_MIMO,        // flat / butterfly roof, wide eaves, carport screen
    AR_HOUSE_SPLIT,       // split level: a two-storey wing beside a one-storey wing
    AR_HOUSE_CONCH,       // island conch house: wood siding, metal roof, deep porch
    AR_VILLA_MODERN,      // white modernist villa: stacked flat boxes, glass
    // Sol Beach hotels and apartments (BS_DECO)
    AR_DECO_STREAMLINE,   // streamline moderne: rounded corners, continuous eyebrows, a corner fin with the hotel's name
    AR_DECO_MED,          // Mediterranean-revival small hotel: tile roof, arched loggia, a little tower
    AR_DECO_MIMO,         // 1950s MiMo hotel: eave slab, entrance canopy, jalousie ribbons, accent panels
    // industry
    AR_WARE_SAWTOOTH,     // sawtooth north-light roof
    AR_WARE_OFFICE,       // shed with a two-storey office block in front
    AR_WARE_BODYSHOP,     // low auto body shop: roll-up doors, painted, signs
    // behind the street fronts (blockstyle.cpp infill, BuildingStyle BS_GARAGE so gameplay leaves them alone)
    AR_REAR_COTTAGE,      // back house / granny flat in the yard
    AR_REAR_GARAGE,       // a row of lock-up garages on the alley
    AR_REAR_SHED,         // a workshop or storage shed behind shops and sheds
    // villas and the island towns
    AR_VILLA_MED,         // Mediterranean-revival villa: barrel-tile hip roofs, a tower, a loggia portico
    AR_VILLA_COLONIAL,    // white two-storey colonial villa: hip roof, two-storey columned portico with a pediment
    AR_SHACK_STILT,       // Ten Palms / Keys stilt house: deck, stairs, porch roof, metal roof (gable, shed or hip)
    // motels and strip malls (the arterials, Key Solano's side streets)
    AR_MOTEL_MIMO,        // 1950s motor court: eave slab, steel walkway, accent panels, a boomerang pylon sign
    AR_MOTEL_MED,         // Mediterranean motel: barrel-tile hip roof, stucco piers under the walkway, a monument sign
    AR_MOTEL_KEYS,        // Keys motel: wood siding, metal roof, a two-level wooden gallery, a painted post sign
    AR_MOTEL_INN,         // 1970s motor inn: shingled mansard, brick-red or beige, a pole sign
    AR_STRIP_MISSION,     // mission-revival strip: tile pent roof, arcaded walkway on stucco piers, an entry tower
    AR_STRIP_MIMO,        // 1960s strip: folded-plate canopy on pipe columns, a pylon sign
    AR_STRIP_MODERN,      // power center: tall parapet, an anchor end, a metal canopy, stone and stucco
    // farms (Redland, the Palmera farmlands): the farmhouses take the house types (cracker = conch, I-house = two-storey,
    // ranch); the barns their own
    AR_BARN_GAMBREL,      // gambrel-roofed barn: the big door and a hay door in the gable end, white trim
    AR_BARN_POLE,         // open pole barn / machine shed: posts along the front, a corrugated back and end
    AR_BARN_GABLE,        // steep gable barn with a cupola, a lean-to shed along one side
    // churches (the suburbs' boulevards, the small towns)
    AR_CHURCH_MISSION,    // mission revival: stucco, low tile gable, a stepped front gable, a corner bell tower
    AR_CHURCH_CLAPBOARD,  // white board church: steep gable, a central steeple with a belfry and a spire
    AR_CHURCH_BRICK,      // brick Gothic revival: steep slate gable, a square corner tower with pinnacles
    AR_CHURCH_AFRAME,     // 1960s A-frame: the roof down to the ground, a glass gable, a free-standing bell pylon
    // houses of the working neighbourhoods and the old towns
    AR_HOUSE_CBS,         // 1950s Florida block house: pastel stucco, low hip roof, aluminium window awnings, a carport
    AR_HOUSE_VICTORIAN,   // folk Victorian: two-storey frame cross gable, a porch with turned posts and brackets, a gable truss
    AR_HOUSE_RAISED,      // Keys house raised on concrete piers (the flood code): parking and storage under, a front deck and stair
    AR_COUNT
};

// Massing variants (buildmesh.cpp, blockstyle.cpp): how the archetype's volume is carved out of the envelope
enum MassingKind : u8 {
    MK_BOX = 0, MK_L, MK_U, MK_COURT, MK_STEP_FRONT, MK_STEP_BACK, MK_SPLIT, MK_CORNER_TOWER, MK_PODIUM_SLAB, MK_CURVE, MK_WINGS, MK_ROUNDED,
    MK_CHAMFER,   // the street corner cut at 45 degrees (corner shops: the entrance on the cut)
    MK_COUNT
};

// Roof forms beyond RoofType (blockstyle.cpp picks one per archetype)
enum RoofForm : u8 { RFM_PARAPET = 0, RFM_EAVE, RFM_TILE_HIP, RFM_TILE_PENT, RFM_TERRACE, RFM_METAL_GABLE, RFM_SAWTOOTH, RFM_BUTTERFLY, RFM_MANSARD, RFM_COUNT };

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
    u32 facade2 = 0xffffffffu;  // secondary cladding (tower podium, midrise base band), same floor grid; ~0 = none
    int siteElem = -1;          // >= 0: hand-built by this site element (places.h): buildmesh.cpp and facade detail skip it
    bool siteHost = false;      // a hand-built building that may host an enterable interior (its element cuts the openings)
    // Architecture (blockstyle.cpp, after the interiors are planned): archetype, massing and roof form inside the
    // envelope above (c, ax, hx, hy, baseZ .. baseZ + height), which gameplay queries keep using unchanged
    u8 arch = AR_NONE;
    u8 massing = MK_BOX;
    u8 roofForm = RFM_PARAPET;
    u8 archFlags = 0;           // ABF_* (corner lot, mural wall side, ...)
    u32 roofTint = 0xffffffffu; // tile / shingle / metal roof colour of pitched archetype roofs
    int face = -1;              // street side the lot fronts (road edge * 2 + side), -1 for site buildings
};

enum ArchFlags : u8 { ABF_CORNER = 1, ABF_MURAL = 2, ABF_CORNER_LEFT = 4, ABF_OLD = 8, ABF_ACCENT = 16 };

// A facade-covered mass of a building mesh (main block, podium, tower tier, deco tower, house body), recorded by
// buildmesh.cpp so the street-level detail pass (facadedetail.cpp) can align moldings with the shader's window grid.
// FM_WING: a secondary volume of an archetype house or a corner tower (window trims only, no garden, door or storefront)
enum FacadeMassKind : u8 { FM_MAIN = 0, FM_PODIUM, FM_TIER, FM_DECO_TOWER, FM_HOUSE, FM_WING };
struct FacadeMass {
    std::vector<vec2> fp;  // CCW footprint
    float z0, z1;          // facade wall span (z1 = roof level, parapet excluded)
    float vBase;           // facade v origin (building base)
    u8 kind;
    bool parapet;
    u32 facade = 0xffffffffu;  // facade record of this mass when it differs from the building's (mixed cladding)
};

// Open ground the blocks keep (blockstyle.cpp infill): surface parking behind the street fronts, vacant lots, yards.
// Drawn by the cells (cellgen.cpp); not buildings, so walkable and invisible to the building queries.
enum OpenLotKind : u8 { OL_PARKING = 0, OL_VACANT, OL_YARD, OL_SERVICE };
struct OpenLot {
    vec2 c, ax;      // centre, unit axis
    float hx, hy;    // half extents along ax / perp(ax)
    vec2 front;      // toward the street the block faces
    float z;         // ground level at the centre
    u8 kind;
    u8 region;
    u32 seed;
    u8 home = 0;     // a yard of a house (lawn, wood fence) rather than of a shop or a shed (paving, block wall)
};

struct BuildingSet {
    std::vector<Building> buildings;
    std::vector<OpenLot> openLots;
    std::vector<std::vector<int>> openCellLists;   // per streaming cell: open lot indices (by centre)
    std::vector<FacadeGPU> facades;
    std::vector<std::vector<int>> cellLists;  // per streaming cell: building indices (by center)
    std::vector<std::string> signNames;       // shop sign texts (index = sign slot)
    void generate(WorldMap& map, const RoadNetwork& roads);
    void addSiteBuilding(WorldMap& map, const struct SiteBuildingReq& q, u32 seed);
    // Architecture of the ordinary buildings by district palette (blockstyle.cpp; after the interiors are planned)
    void restyleBlocks(WorldMap& map, const RoadNetwork& roads);
    // Debug log: share of neighbours within 150 m that repeat a building's signature, per district (blockstyle.cpp)
    void logRepetition() const;
    // Paved ground for the renderer's ground cover (grass, flowers): true on rear parking lots and paved service yards
    // (openLots OL_PARKING / OL_SERVICE), every building footprint (street, back and site buildings), a house's garage
    // wing and driveway, a strip mall's or gas station's forecourt. Per-cell lists: a few dozen rectangle tests.
    bool pavedAt(vec2 p) const;
    // The same for a whole streaming cell at once: out[iy * n + ix] = 1 where the centre of square (ix, iy) of an n x n
    // grid over cell (cx, cy) (origin cellOrigin(cx, cy), side kCellSize) is paved, else 0; returns the paved count.
    int pavedGrid(int cx, int cy, int n, std::vector<u8>& out) const;
    // Behind the street fronts: back houses, garage rows, sheds, rear parking and yards in the empty middles of the
    // blocks (blockstyle.cpp). free(c, ax, hx, hy) tells whether a rectangle is clear of lots, roads and reserved
    // ground; claim marks it taken. Appends buildings (after the interiors: no index changes) and open lots.
    // nearStreet(p): p is within a frontage's reach of a street (back buildings stay out of it: a story place looks for
    // a facade up to 12 m behind the sidewalk, mission_util.cpp resolveFrontage)
    void infillBlocks(WorldMap& map, const std::function<bool(vec2, vec2, float, float)>& free, const std::function<void(vec2, vec2, float, float)>& claim,
                      const std::function<bool(vec2)>& nearStreet);
    // Collision query: returns true if p (xy) is inside any building footprint (with margin)
    bool pointInBuilding(vec2 p, float margin, float* topZ = nullptr) const;
    void buildingsNear(vec2 p, float r, std::vector<int>& out) const;
};

extern BuildingSet* gBuildings;

}  // namespace World
