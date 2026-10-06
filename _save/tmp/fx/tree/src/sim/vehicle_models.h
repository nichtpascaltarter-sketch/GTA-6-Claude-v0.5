// Procedural vehicle models: geometry + physical/gameplay metadata.
// Local space: +X right, +Y forward, +Z up. Origin: on the ground plane (z = 0 where the tires touch the ground
// at rest), horizontally at the center of the wheelbase/track. Units: meters, kilograms, seconds, kW.
#pragma once
#include "../render/mesh.h"

namespace Vehicles {

enum VehicleClass : u8 {
    VC_COMPACT = 0, VC_SEDAN, VC_COUPE, VC_SUV, VC_PICKUP, VC_SPORTS, VC_SUPER, VC_MUSCLE, VC_VAN, VC_BUS, VC_TRUCK,
    VC_SERVICE,  // delivery vans, garbage truck, utility
    VC_POLICE, VC_TAXI, VC_AMBULANCE, VC_FIRETRUCK,
    VC_MOTORBIKE, VC_SCOOTER,
    VC_BOAT, VC_JETSKI, VC_AIRBOAT,
    VC_PLANE, VC_HELI,
    VC_COUNT
};

enum LightType : u8 { LT_HEAD = 0, LT_TAIL, LT_BRAKE, LT_REVERSE, LT_INDICATOR_L, LT_INDICATOR_R, LT_SIREN_RED, LT_SIREN_BLUE, LT_BEACON };

struct WheelSpec {
    vec3 pos;          // wheel center at rest (suspension at nominal compression)
    float radius;      // tire outer radius
    float width;
    bool steer;        // steered wheel
    bool drive;        // driven wheel
    bool left;         // on the left side (mesh must be mirrored)
};

struct SeatSpec {
    vec3 pos;          // hip position of the seated character
    bool driver;
    bool exitLeft;     // which side the occupant exits
    int door = -1;     // VehicleModel::doors entry the occupant gets in and out through (-1: no opening door)
};

// An opening side door (cars at full detail). `mesh` holds everything that swings with the door - outer skin, glass,
// window trim, handle, mirror, door card, armrest and the door's shut faces - in the vehicle's local frame at the
// closed position; the body has the matching opening (painted jambs, sill, pillars and seals) and no door, so a door
// must always be drawn with the body: with the body's transform turned about the hinge axis,
//   world = body * translate(hinge) * rotate(axis, angle) * translate(-hinge),   0 <= angle <= maxAngle,
// a positive angle swinging the door outwards (its rear edge away from the body).
struct DoorSpec {
    MeshData mesh;
    vec3 hinge;            // a point on the hinge axis (front edge of the door)
    vec3 axis;             // unit hinge axis, roughly vertical (leaning in with the body side)
    float maxAngle = 1.15f;
    bool left = false;     // on the left side (-X)
    bool front = true;     // front door (rear doors of four-door bodies: false)
    // Closed-door reference points for the occupant's hands and path (vehicle frame):
    vec3 handle;           // outer pull handle (where the fingers hook under it)
    vec3 handleIn;         // inner release / pull handle on the door card
    vec3 grip;             // top of the door near its rear edge (a hand holding the open door)
    vec3 outward;          // unit outward normal of the closed door (horizontal)
    // The opening (vehicle frame): along the length between yFront and yRear at the sill, sill top at sillZ reaching
    // out to sillX (|x| of the sill's outer edge), the roof rail's underside at roofZ over the seat
    float yFront = 0.f, yRear = 0.f, sillZ = 0.f, sillX = 0.f, roofZ = 0.f;
    std::vector<vec2> outline;   // the door's shut lines (y, z) round it, as seen from its side
    vec3 top[6];                 // the opening's top edge (roof rail, down the A-pillar) from its rear end to its front
};

struct LightSpec {
    vec3 pos;
    vec3 dir;
    LightType type;
};

struct VehicleModel {
    std::string name;       // original model name (e.g. "Vireo GT")
    std::string maker;      // original manufacturer
    VehicleClass cls = VC_SEDAN;
    // Geometry. Body uses world materials (render/mesh.h): MAT_CARPAINT (vertex color alpha 1 = primary paint,
    // alpha 0 = secondary paint), MAT_CAR_GLASS, MAT_CHROME, MAT_PLASTIC, MAT_RUBBER, MAT_INTERIOR, MAT_LEATHER,
    // MAT_LIGHT_HEAD, MAT_LIGHT_TAIL (vertex color green > 0.5 marks reverse lamps), MAT_LIGHT_INDICATOR (siren
    // lights use vertex color rgb as siren color), MAT_EMISSIVE, MAT_METAL_PAINTED, MAT_DECAL_TEXT...
    MeshData body;          // everything except wheels (and except rotor/propeller for aircraft)
    MeshData wheel;         // ONE wheel centered at the origin, rotation axle along +X, tire radius = wheels[0].radius
    MeshData rotor;         // helicopter main rotor / plane propeller / boat propeller (centered at origin, spin axis +Z
                            // for heli rotor, +Y for propellers); empty if none
    vec3 rotorPos;          // where the rotor mesh is attached
    MeshData tailRotor;     // helicopters: tail rotor (spin axis +X); empty otherwise
    vec3 tailRotorPos;
    MeshData caliper;       // brake caliper in the same local frame as `wheel` (left wheels turned the same way): draw it
                            // at every wheel with the wheel transform WITHOUT the spin (steer + suspension only), close
                            // range only (LOD0); empty if none
    // Steering wheel (cars, trucks, buses, boats with a wheel): rim, spokes and hub as a part of their own, centred at
    // the origin with the rim in the XY plane (+Y up the rim with the wheel straight, +X right) and +Z the column axis
    // towards the driver. Draw it at steerWheelPos with local +Z along steerWheelAxis and local +X along the vehicle's
    // +X, turned with the driver's hands: a right turn is clockwise as the driver sees it (-angle about +Z). Empty if
    // none; the column stays in the body, and distant levels keep a fixed wheel in the body.
    MeshData steerWheel;
    vec3 steerWheelPos, steerWheelAxis = vec3(0, -1, 0);
    float steerWheelRadius = 0.f;
    // Instrument needles over the dials (close range): `needle` is one needle pivoting at the origin, pointing +Y, with
    // its face normal +Z (towards the driver), sized for these dials
    struct Gauge {
        vec3 pos, normal, up;          // pivot on the dial face, face normal (towards the driver), the dial's 12 o'clock
        u8 kind = 0;                   // 0 road speed (m/s), 1 engine rpm, 2 fuel (0..1), 3 coolant temperature (0..1)
        float a0 = -2.36f, a1 = 2.36f; // needle angle clockwise from `up` as the driver sees it, at zero / full scale
        float full = 1.f;              // value at a1
    };
    MeshData needle;
    std::vector<Gauge> gauges;
    std::vector<WheelSpec> wheels;
    std::vector<SeatSpec> seats;
    std::vector<DoorSpec> doors;   // opening side doors (cars, full detail only); see DoorSpec
    std::vector<LightSpec> lights;
    // Collision: oriented box (center + half extents) in local space, plus a few extra spheres if useful
    vec3 boxCenter, boxHalf;
    // Physics / handling
    float mass = 1400.f;          // kg
    float power = 110.f;          // peak engine power kW
    float torque = 250.f;         // peak torque Nm
    float maxRpm = 6500.f;
    float topSpeed = 50.f;        // m/s (governed)
    int gears = 5;
    float driveFront = 0.f;       // fraction of drive torque to the front axle (0 RWD, 1 FWD, 0.4 AWD)
    float brakeForce = 9000.f;    // N per wheel peak
    float grip = 1.f;             // tire friction multiplier
    float suspensionTravel = 0.18f;
    float suspensionStiffness = 1.f;  // relative
    float dragCoef = 0.32f;
    float frontalArea = 2.2f;
    float downforce = 0.f;        // coefficient
    vec3 centerOfMass = vec3(0, 0, 0.5f);
    int engineSound = 0;          // Audio::EngineKind
    int sirenMode = -1;           // Audio siren mode or -1
    // Boats: buoyancy sample points (local), draft; planes: wing area, lift slope; helis: rotor radius
    std::vector<vec3> floatPoints;
    float wingArea = 0.f, liftSlope = 0.f, rotorRadius = 0.f;
    // Appearance
    std::vector<vec3> paletteColors;  // plausible factory colors (linear rgb) for random spawns
    bool fixedLivery = false;         // police/taxi/ambulance: tint colors fixed
    vec3 liveryPrimary, liverySecondary;
    float spawnWeight = 1.f;          // relative frequency in traffic
    int price = 20000;                // in-game price ($)
};

// Number of distinct models and their construction (deterministic).
int modelCount();
void buildModel(int index, VehicleModel& out);
// Convenience: find a model index by class (n-th of that class), -1 if none.
int findModel(VehicleClass cls, int n = 0);

// Distant levels of detail for model `index` (same materials and paint slots as buildModel's body):
//  lods[0] = LOD1 body, ~4-6.5k tris for cars (for ~40-120 m): shell at lower resolution with flush windows, flat
//            lamp/grille patches, seats + dash inside a closed cabin (door cards, headliner, firewall), no seams,
//            badges or small hardware. Draw it with `wheelLod1` at the usual wheel transforms (a 252-tri wheel; pass
//            nullptr to skip).
//  lods[1] = LOD2 body, ~1.3-1.6k tris for cars, ~2-3k for trucks/buses/boats/aircraft (beyond ~120 m): coarse shell,
//            dimmed windows over a closed cabin shell, lamp patches, and the wheels merged in as simple cylinders at
//            their rest positions (no wheel draws).
// Both keep MAT_CAR_WINDOW windows (put them in the glass pass as for LOD0). Rotors keep using the LOD0 rotor meshes.
// Thread-safe like buildModel.
void buildVehicleLods(int index, MeshData lods[2], MeshData* wheelLod1 = nullptr);

}  // namespace Vehicles
