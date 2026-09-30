// Enterable interiors (seamless, no loading screens): the planner picks real buildings at world generation (story
// places, shops, businesses), cuts door and window openings into their facade shells and hollows their collision
// (buildmesh.cpp hooks), and describes each interior as rooms, openings, doors, ambient volumes, daylight portals,
// NPC scenario points and gameplay markers. Room geometry, furniture, collision and lights are built on demand in
// the interior's own frame (interiorkit.cpp, interiorlayouts.cpp); gameplay streams, renders and animates them
// (game/interiors_game.cpp).
//
// Interior frame ("model space"): x = right when facing the building from the street, y = into the building
// (y = 0 is the front facade plane), z = up from the ground floor level. Furniture helpers use a local frame whose
// +y is the item's front (the side a user faces it from).
#pragma once
#include "buildings.h"
#include "worldtypes.h"
#include "../render/mesh.h"

namespace World {

enum InteriorKind : u8 {
    IK_APARTMENT = 0,  // Mari's apartment in Calle Luna (safehouse)
    IK_TRAILER,        // Dex's trailer in the Flats (safehouse, whole structure)
    IK_DINER,          // Mama Lucha's diner
    IK_CHOPSHOP,       // Rook's garage / chop shop
    IK_CLUB,           // Club Riptide, open-air beach club on Sol Beach (whole structure)
    IK_POLICE,         // police HQ lobby
    IK_HOSPITAL,       // hospital lobby
    IK_GUNSHOP,        // Palmetto Arms / Northside Arms
    IK_CONVENIENCE,    // corner stores and gas station marts
    IK_CLOTHES,        // Threads clothing store
    IK_TOWER_LOBBY,    // Solaris One lobby
    IK_PENTHOUSE,      // Sandoval's penthouse office on top of Solaris One
    IK_WAREHOUSE,      // Port Isle warehouse
    IK_BAR,            // neighborhood bar
    IK_GYM,
    IK_BARBER,
    IK_TATTOO,
    IK_DEALERSHIP,     // Palm Motors showroom
    IK_MODSHOP,        // Tide Customs body and paint shops
    IK_CARWASH,        // Sunwash Car Wash
    IK_COUNT
};

// A hole cut into a facade wall of the building shell (world space).
enum OpeningKind : u8 { OP_DOOR = 0, OP_GLASS, OP_ROLLUP, OP_OPEN };
struct InteriorOpening {
    vec2 a, b;        // bottom edge on the facade plane, a -> b along the wall
    float z0, z1;     // world heights
    u8 kind;
    int room = -1;    // room the opening leads into
};

// Doors are separate dynamic parts: leaves swing (hinged), slide apart or roll up. Model space.
enum DoorKind : u8 { DK_HINGED = 0, DK_HINGED_PAIR, DK_SLIDING_PAIR, DK_ROLLUP, DK_ELEVATOR };
struct InteriorDoor {
    vec3 c;           // doorway center at floor level
    vec2 t;           // unit direction along the wall (leaf closing direction for single hinged doors: hinge at c - t*w/2)
    vec2 n;           // unit normal pointing into the room the leaves open into
    float w, h;       // doorway width / height
    float depth;      // plane offset of the closed leaves along n from c
    u8 kind;
    u8 style;         // 0 wood panel, 1 glass shopfront, 2 steel, 3 wood + glass, 4 slatted/saloon, 5 elevator steel
    bool exterior;    // in the building shell
    u32 color;
};

// Rooms: axis-aligned boxes in model space. Each becomes an ambient volume for the lighting pass.
struct InteriorRoom {
    vec3 mn, mx;
    vec3 lampAmbient;       // ambient radiance while the lights are on (irradiance / PI, like the sky SH)
    float dayBounce;        // fraction of the sky irradiance bounced around inside by daylight
    u8 schedule;            // LS_ALWAYS, LS_EVENING (homes), LS_BUSINESS (shops, open late), LS_NIGHTLIFE
    bool outdoor = false;   // open-air area (no ambient override)
};
enum LightSchedule : u8 { LS_ALWAYS = 0, LS_EVENING, LS_BUSINESS, LS_NIGHTLIFE };

// Daylight portal: an opening rectangle p0 + s*u + t*v (s, t in 0..1); cross(u, v) points into the room.
struct InteriorPortal {
    vec3 p0, u, v;
    int room;
    int door = -1;          // door that closes this portal (transmission follows the door), -1 none
    float transmission;     // glass 0.8, open doorway 1
};

// NPC scenario point: where a ped stands/sits/dances and with which stance (Anim::AnimInput::stance).
enum ScenarioRole : u8 {
    SR_PATRON = 0, SR_CLERK, SR_COOK, SR_WAITER, SR_BARTENDER, SR_DANCER, SR_DJ, SR_BOUNCER, SR_COP, SR_NURSE,
    SR_DOCTOR, SR_GUARD, SR_MECHANIC, SR_RECEPTION, SR_WORKER, SR_VIP, SR_SHOPPER, SR_PATIENT, SR_BARBER, SR_ARTIST,
    SR_ATHLETE, SR_COUNT
};
enum ScenarioFlags : u8 { SF_NIGHT = 1, SF_DAY = 2, SF_OPTIONAL = 4, SF_STAFF = 8 };
struct InteriorScenario {
    vec3 pos;         // floor point under the ped origin (model space)
    float yaw;        // model-space facing (0 = +y, counter-clockwise)
    u8 stance;        // 0 idle, 6 sit, 7 talk, 9 dance, 10 smoke, 11 lean, 14 look around, 16 cheer, 19 guard
    u8 role;
    u8 flags;
    float lift;       // ped origin above the floor (bar stools: seat hip height - 0.5)
};

// Gameplay markers inside (shop counter, bed, wardrobe, elevator buttons...)
// IM_CAR / IM_CAR_STRIPPED: display vehicle (pos = ground contact center under the body, yaw = heading), drawn by
// gameplay with a real vehicle model (stripped: no wheels, parked on stands)
enum InteriorMarkerKind : u8 { IM_COUNTER = 0, IM_BED, IM_WARDROBE, IM_ELEVATOR, IM_ELEVATOR_TOP, IM_SNACKS, IM_ENTRY, IM_DOOR_OUT, IM_MIRROR,
                               IM_CAR, IM_CAR_STRIPPED, IM_COUNT };
struct InteriorMarker {
    u8 kind;
    vec3 pos;         // model space
    float yaw;
};

// Local light (model space). anim: 0 steady, 1 flicker, 2 club sweep, 3 club strobe, 4 TV, 5 neon pulse, 6 fire
struct InteriorLight {
    vec3 pos, color;  // color = luminous intensity (cd) * rgb
    float radius;
    vec3 dir;         // spot direction (unused for point lights)
    float cosOuter = -2.f, cosInner = -1.f;
    u8 anim = 0, phase = 0;
    u8 room = 255;    // room index (lights only light their own volume)
};

struct InteriorDef {
    u8 kind = 0;
    int building = -1;            // -1 for free-standing structures (club)
    u32 seed = 0;
    std::string name;             // binding key for gameplay (shop / safehouse / business name)
    std::string alias;            // second binding key (business asset name), may be empty
    vec3 origin;                  // world position of the frame origin (front facade center at ground floor level)
    vec2 ax = vec2(1, 0), ay = vec2(0, 1);   // world directions of the frame x / y axes
    float x0 = -5.f, x1 = 5.f, depth = 10.f; // hollow region: x in [x0, x1], y in [0, depth]
    float ceil = 3.f;             // ceiling height of the main rooms
    float shellTop = 3.4f;        // hollow part of the shell (collision) above the floor
    float bw = 3.f;               // front facade bay width (physical) and grid origin (x of bay 0 start)
    float bayX0 = 0.f;
    int bays = 1;
    int doorBay = -1;             // front bay with the main entrance
    bool storefront = false;      // ground floor storefront facade
    bool ownShell = false;        // the interior builds the whole structure (trailer, club)
    float signZ0 = 0.f, signZ1 = 0.f;   // storefront sign band of the facade (heights above the floor, 0 = none)
    float radius = 20.f;          // bounding radius around the region center (streaming, culling)
    int link = -1;                // interior at the other end of the express elevator (IM_ELEVATOR <-> IM_ELEVATOR_TOP)
    std::vector<InteriorOpening> openings;
    std::vector<InteriorDoor> doors;
    std::vector<InteriorRoom> rooms;
    std::vector<InteriorPortal> portals;
    std::vector<InteriorScenario> scenarios;
    std::vector<InteriorMarker> markers;
    vec3 toWorld(vec3 p) const { return vec3(origin.x + ax.x * p.x + ay.x * p.y, origin.y + ax.y * p.x + ay.y * p.y, origin.z + p.z); }
    vec2 dirToWorld(vec2 d) const { return ax * d.x + ay * d.y; }
    vec3 toLocal(vec3 w) const {
        vec2 d = w.xy() - origin.xy();
        return vec3(dot(d, ax), dot(d, ay), w.z - origin.z);
    }
    // engine yaw convention: 0 faces +Y (north), counter-clockwise positive
    float yawToWorld(float yawLocal) const {
        vec2 f = dirToWorld(vec2(-sinf(yawLocal), cosf(yawLocal)));
        return atan2f(-f.x, f.y);
    }
    vec3 center() const { return toWorld(vec3((x0 + x1) * 0.5f, depth * 0.5f, ceil * 0.5f)); }
    const InteriorMarker* marker(u8 k) const {
        for (const InteriorMarker& m : markers)
            if (m.kind == k) return &m;
        return nullptr;
    }
    // room containing a model-space point (-1 none)
    int roomAt(vec3 p, float margin = 0.02f) const {
        for (size_t i = 0; i < rooms.size(); i++) {
            const InteriorRoom& r = rooms[i];
            if (p.x >= r.mn.x - margin && p.x <= r.mx.x + margin && p.y >= r.mn.y - margin && p.y <= r.mx.y + margin && p.z >= r.mn.z - margin &&
                p.z <= r.mx.z + margin)
                return (int)i;
        }
        return -1;
    }
};

struct InteriorSet {
    std::vector<InteriorDef> defs;
    // Interior (and room) containing a world point, -1 if none
    int at(vec3 p, int* room = nullptr) const;
    int byName(const char* name) const;
    int byKind(u8 kind, int nth = 0) const;
};
extern InteriorSet* gInteriors;

// World generation: after the buildings exist (BuildingSet::generate). Marks Building::interior.
void planInteriors(WorldMap& map, const RoadNetwork& roads, BuildingSet& bs);

// ---- building shell hooks (buildmesh.cpp) ----
// Facade wall a -> b (z0..z1, facade uv u0..u0+uLen, v = z - vBase) of an interior building with its openings cut
// out. Returns false when no opening lies on that wall (the caller emits the plain wall).
bool interiorFacadeWall(int interior, MeshData& m, vec3 org, vec2 a, vec2 b, float z0, float z1, float u0, float uLen, float vBase, u32 color, u32 mat);
// Collision for a mass box (c, ax, hx, hy, z0..z1) of an interior building: returns false if the box does not hold the
// interior; otherwise emits the hollow version (floor slab, walls with door gaps, solid remainder) and returns true.
bool interiorShellCollision(int interior, vec2 c, vec2 ax, float hx, float hy, float z0, float z1, std::vector<CollisionBox>& out);
// Whole-structure interiors replace the building mesh (the structure itself streams with the interior).
bool interiorOwnsShell(int interior);
// ---- landmark hooks (landmarks.cpp genSolaris) ----
// Interior of a landmark kind (IK_TOWER_LOBBY, IK_PENTHOUSE), -1 if none was planned
int interiorForLandmark(u8 kind);
// sitegeo::facadeRing (facade quads of a footprint ring z0..z1, bay grid per wall) with the interior's openings cut
void interiorFacadeRing(int interior, MeshData& m, vec3 org, const std::vector<vec2>& fp, float z0, float z1, float vBase, u32 facadeId, float bay, u32 col);
// Front bay holding an exterior door of an interior building (facadedetail.cpp keeps trims and gates out of it), -1 none
int interiorDoorBay(const Building& b);
// Warehouse loading door at footprint coordinate u (along Building::ax) replaced by a real roll-up door of the
// building's interior (buildmesh.cpp skips the painted door, facadedetail.cpp its bumpers)
bool interiorHidesLoadingDoor(const Building& b, float u);

// ---- on-demand geometry (worker threads) ----
enum InteriorPart : u8 { IP_SHELL = 0, IP_FURNITURE, IP_DETAIL, IP_COUNT };
struct InteriorDoorLeaf {
    int door;         // index into InteriorDef::doors
    int side;         // 0 left / single leaf, 1 right leaf
    MeshData mesh;    // leaf frame: pivot at the origin (hinge / slide center), x along the closed leaf, y = door normal
};
struct InteriorMesh {
    MeshData parts[IP_COUNT];            // model space
    std::vector<InteriorDoorLeaf> leaves;
    std::vector<CollisionBox> col;       // world space (walls, counters, furniture)
    std::vector<InteriorLight> lights;
    int triangles = 0;
    double ms = 0.0;
};
void buildInterior(int index, InteriorMesh& out);

// Shop / safehouse marker inside the named interior (world space). Returns false if there is no such interior.
bool interiorMarkerWorld(const char* name, u8 kind, vec3& outPos, float* outYaw = nullptr);
// Just outside the main entrance of the interior of a given kind (story "door" points), world space.
bool interiorDoorOutside(u8 kind, int nth, vec3& outPos, float* outYaw = nullptr);

}  // namespace World
