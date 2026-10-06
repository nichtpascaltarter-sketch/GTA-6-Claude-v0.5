// Vehicle dynamics for every vehicle class: cars/vans/trucks (raycast wheels, engine + gearbox, tire model),
// motorbikes/scooters (lean + balance assist), boats/jetskis/airboats (buoyancy + planing hull + thrust),
// planes (lift/drag/control surfaces + landing gear) and helicopters (collective/cyclic/tail rotor + auto-stabilizer).
// Consumes VehicleModel metadata (vehicle_models.h) and collides with the static world through Phys::gCollision.
// Local vehicle frame: +X right, +Y forward, +Z up; body.pos is the model origin (ground contact plane at rest).
//
// Calling convention (see vehicle_sim.cpp for details):
//   per fixed 120 Hz tick: stepVehicle() for every simulated vehicle, then collideVehicles() for nearby pairs.
//   dt may jitter (stepVehicle substeps internally above 1/100 s). Event fields are cleared at the start of
//   stepVehicle, so read them after the tick (collideVehicles adds to them).
// The simulation integrates about the center of mass internally; body.pos/rot/vel/angVel stay authoritative between
// calls, so the game may teleport, push (body.vel / body.applyImpulse) or wake (sleeping = false) vehicles directly.
#pragma once
#include "physics.h"
#include "vehicle_models.h"

namespace Vehicles {

// Player/AI input in a class-independent form. The game maps pad/keyboard to these.
struct VehicleControls {
    float throttle = 0.f;   // 0..1. Cars/bikes/boats: drive. Planes: engine thrust target. Helis: unused.
    float brake = 0.f;      // 0..1. Cars/bikes/boats: brake; held while (nearly) stopped -> reverse.
                            // Planes: wheel brakes on the ground, air brake in flight.
    float steer = 0.f;      // -1 left .. +1 right (cars/bikes/boats; planes: nose-wheel steering on ground)
    bool handbrake = false; // cars: rear lock (drifts, 180s); bikes: rear brake; boats: none
    float pitch = 0.f;      // -1..1. Planes: +1 nose up (elevator). Helis: +1 = cyclic back (nose up, move backwards).
                            // Bikes: +1 rider leans back (wheelie), -1 leans forward (stoppie / tuck).
                            // Cars (airborne only): +1 nose up (GTA-style air control; map it like bikes).
    float roll = 0.f;       // -1..1. Planes/helis: +1 roll right. Cars: airborne roll / flip-back (steer is used when 0).
    float yaw = 0.f;        // -1..1. Planes: rudder. Helis: tail rotor (+1 nose right).
    float lift = 0.f;       // -1..1. Helis: collective (+1 climb). Planes: unused.
    bool engineOff = false; // request engine off (parked / abandoned vehicles)
    // --- added by the vehicle simulation ---
    bool hasDriver = true;  // false = nobody at the controls: bikes drop onto their kickstand, helis/planes lose the
                            // pilot assists (no auto-hover), cars/boats keep whatever brake/handbrake is passed.
};

constexpr int kMaxWheels = 10;
constexpr int kMaxFloats = 12;   // buoyancy sample points used per vehicle

struct WheelState {
    float compression = 0.f;   // suspension compression (m), 0 = fully extended
    float compressionVel = 0.f;
    float spinAngle = 0.f;     // accumulated rotation (rad) for rendering
    float spinVel = 0.f;       // rad/s
    float steerAngle = 0.f;    // rad, + = right
    bool contact = false;
    vec3 contactPos;           // relative to body.pos (world orientation)
    vec3 contactNormal = vec3(0, 0, 1);
    u8 surface = 0;            // Phys::SurfaceType
    float slip = 0.f;          // combined slip 0..1+ (skid audio, tire smoke, skid marks)
    float lateralSlip = 0.f;   // signed lateral slip velocity (m/s)
    float load = 0.f;          // normal force (N)
    bool burst = false;        // shot-out tire
};

// Per-vehicle tuning derived from the VehicleModel by initVehicle (internal to the simulation; do not edit).
struct VehicleTuning {
    vec3 com;                       // local center of mass
    vec3 boxC, boxH;                // collision box (local)
    float boundR = 3.f;             // bounding sphere radius around the COM
    float inertiaScale = 0.85f;
    // wheels
    float restComp[kMaxWheels];     // compression at rest (m)
    float springK[kMaxWheels];      // N/m
    float staticLoad[kMaxWheels];   // N at rest
    float dampBump[kMaxWheels], dampRebound[kMaxWheels];  // N*s/m
    float arbK[kMaxWheels];         // anti-roll bar rate (N/m of compression difference)
    signed char arbPair[kMaxWheels];
    float wheelI[kMaxWheels];       // kg*m^2
    float driveShare[kMaxWheels];   // fraction of axle torque
    float brakeT[kMaxWheels];       // peak service-brake torque (N*m)
    bool rear[kMaxWheels];          // handbrake wheels
    float wheelbase = 2.6f, track = 1.5f, frontY = 1.3f, rearY = -1.3f;
    float maxSteer = 0.6f;          // lock angle (rad)
    float rollArm = 0.4f;           // lateral tire force application height (0 contact .. 1 COM height)
    float esc = 1.f;                // stability assist strength
    float tcSlip = 0.25f;           // traction control slip ratio limit
    float kappaPeak = 0.1f, alphaPeak = 0.13f;
    // powertrain
    float ratio[12];                // overall ratios incl. final drive, [1..gears]; [0] = reverse
    int gears = 5;
    float idleRpm = 800.f, maxRpm = 6500.f;
    float peakTorque = 250.f, peakPowerW = 110000.f;
    float engineI = 0.2f;
    float shiftTime = 0.25f;
    float driveRadius = 0.33f;
    // aero
    float dragArea = 0.7f;          // Cd*A (m^2)
    float liftArea = 0.f;           // downforce coefficient * A (m^2)
    // water
    int floatCount = 0;
    vec3 floatPt[kMaxFloats];
    float floatDraft[kMaxFloats];   // design immersion of each point (m)
    float floatMax[kMaxFloats];     // immersion at which the point's hull section is fully submerged
    float floatK = 0.f, floatDamp = 0.f;
    vec3 propPos;                   // boats: prop / jet / fan position (local)
    float thrustStatic = 0.f;       // N (boats, planes)
    float hullDragX = 0.f, hullDragY = 0.f;  // quadratic hull drag (N/(m/s)^2): longitudinal, lateral
    float planeSpeed = 10.f;        // planing speed (m/s)
    // aircraft
    float wingArea = 16.f, liftSlope = 5.f, inducedK = 0.057f;
    float heliDragArea = 3.f;
    float rotorR = 5.f;
};

struct VehicleState {
    Phys::RigidBody body;
    const VehicleModel* model = nullptr;
    int modelIndex = -1;
    VehicleClass cls = VC_SEDAN;
    int wheelCount = 0;
    WheelState wheels[kMaxWheels];

    // Powertrain
    bool engineOn = true;
    float engineRpm = 800.f;
    int gear = 1;              // -1 reverse, 0 neutral, 1..gears
    float shiftTimer = 0.f;    // > 0 while a shift is in progress (torque cut)
    float throttleOut = 0.f;   // smoothed throttle actually applied (audio uses this)
    float engineLoad = 0.f;    // 0..1 (audio)
    // Aircraft / boats
    float rotorSpeed = 0.f;    // normalized 0..1 (heli rotor, propellers, boat props)
    float rotorAngle = 0.f, tailRotorAngle = 0.f;
    float gearDown = 1.f;      // planes: landing gear extension 0..1 (auto: retracts above 40 m AGL)
    // Condition
    float health = 1000.f;         // body health; <= 0 -> wrecked
    float engineHealth = 1000.f;   // < 300 smokes, < 100 fire risk, <= 0 dead engine
    float damageZones[6] = {};     // 0 front, 1 rear, 2 left, 3 right, 4 roof, 5 underside (0..1 dent amount)
    bool wrecked = false;
    // Environment state
    int wheelsOnGround = 0;
    bool inWater = false;
    float submerged = 0.f;     // 0..1 fraction of the body under water
    float airborneTime = 0.f;
    float upsideDownTime = 0.f;
    // Events produced during the last stepVehicle/collideVehicles call (cleared at the start of stepVehicle)
    float impactImpulse = 0.f; // largest impulse (N*s) this step
    vec3 impactPoint;          // relative to body.pos
    vec3 impactNormal;
    int impactCollider = -1;   // Phys collider index hit (-1 none/terrain)
    bool sleeping = false;     // at rest; stepVehicle is cheap while asleep

    // --- added by the vehicle simulation: events (cleared at the start of stepVehicle) ---
    bool ejectRider = false;       // bikes/scooters/jetskis: the rider is thrown off this step (crash, flip, big hit)
    int brokenCount = 0;           // breakable props (Collider.flags & 1) knocked down this step ...
    int brokenIds[4] = {-1, -1, -1, -1};  // ... their Phys collider ids (already removed with breakCollider)
    vec3 brokenPos[4];             // world position of the hit (prop base height), spawn debris here
    vec3 brokenVel[4];             // suggested initial debris velocity (world, m/s)
    float scrape = 0.f;            // body scraping ground/walls this step (0..1): sparks + scrape audio
    vec3 scrapePoint;              // relative to body.pos (world orientation)
    float splash = 0.f;            // water entry speed this step (m/s), 0 = none
    bool rotorStrike = false;      // helicopters: main rotor hit terrain/buildings this step
    bool shifted = false;          // gearbox engaged a new gear this step (audio)
    // --- added: persistent extras ---
    bool riderOff = false;         // bikes: rider was ejected; balance assist stays off until the game passes
                                   // hasDriver=false (rider gone) and then hasDriver=true again, or resetVehicle()
    bool engineFlooded = false;    // engine bay went under water: engine dead until resetVehicle (engineHealth untouched)
    float lean = 0.f;              // roll angle (rad, + = leaning right): bike rider / jetski animation
    float steerOut = 0.f;          // applied steering (-1..1) for steering wheel / handlebar animation
    float agl = 0.f;               // aircraft: height of the body origin above ground/water (m)
    float airspeed = 0.f;          // aircraft: true airspeed (m/s)
    float stall = 0.f;             // planes: 0..1 stall warning
    float waterDepth = 0.f;        // depth of the body origin below the water surface (m, 0 when dry)

    // --- internal simulation state (do not modify) ---
    VehicleTuning tune;
    float sleepTimer = 0.f, reverseTimer = 0.f, floodTimer = 0.f, intakeTimer = 0.f;
    float hbTimer = 0.f;           // time since the handbrake was released
    int pendingGear = 1;
    int wakeCheck = 0;
    float restZ = 0.f;             // ground height under the COM when the vehicle fell asleep
    float heliYawTarget = 0.f;
    bool prevHasDriver = true;
    bool wasInWater = false;
    float waterZPrev[kMaxFloats] = {};
    float aglTimer = 0.f;
    float ejectTimer = 0.f;
    float leanCmd = 0.f;           // bikes: rider lean target (slew-limited)

    vec3 forward() const { return rotate(body.rot, vec3(0, 1, 0)); }
    vec3 right() const { return rotate(body.rot, vec3(1, 0, 0)); }
    vec3 up() const { return rotate(body.rot, vec3(0, 0, 1)); }
    float speed() const { return length(body.vel); }
    float forwardSpeed() const { return dot(body.vel, forward()); }
};

// Place a vehicle at rest on the ground/water at pos (z is snapped to the surface), heading `yaw`
// (radians, 0 = facing +Y, positive = counter-clockwise).
void initVehicle(VehicleState& s, const VehicleModel& m, int modelIndex, dvec3 pos, float yaw);
// Advance the simulation by dt (the game calls this at a fixed 120 Hz substep).
void stepVehicle(VehicleState& s, const VehicleControls& c, float dt);
// Vehicle vs vehicle collision response. Returns true on contact (and fills the impact fields of both).
bool collideVehicles(VehicleState& a, VehicleState& b);
// Damage from bullets/explosions at a point (relative to body.pos, world orientation).
void applyDamage(VehicleState& s, float amount, vec3 pointRel, vec3 impulse);
// Wheel render transform in vehicle-local space (suspension travel, steering, spin).
// rot = steer (about +Z) * spin (about +X); left wheels are mirrored/rotated by the renderer on top of this.
void wheelLocalTransform(const VehicleState& s, int wheel, vec3& pos, quat& rot);
// Teleport / reset (keeps model), e.g. respawn upright. Clears motion, events, rider/flood state; keeps damage.
void resetVehicle(VehicleState& s, dvec3 pos, float yaw);

// --- added by the vehicle simulation ---
// Center of mass in world space (double precision).
dvec3 vehicleCenterOfMass(const VehicleState& s);
// Shoot out a tire (the game's weapons can call this): reduces grip on that wheel, rim scrapes.
void burstTire(VehicleState& s, int wheel);

}  // namespace Vehicles
