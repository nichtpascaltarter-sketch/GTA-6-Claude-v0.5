// Vehicle dynamics for every vehicle class: cars/vans/trucks (raycast wheels, engine + gearbox, tire model),
// motorbikes/scooters (lean + balance assist), boats/jetskis/airboats (buoyancy + planing hull + thrust),
// planes (lift/drag/control surfaces + landing gear) and helicopters (collective/cyclic/tail rotor + auto-stabilizer).
// Consumes VehicleModel metadata (vehicle_models.h) and collides with the static world through Phys::gCollision.
// Local vehicle frame: +X right, +Y forward, +Z up; body.pos is the model origin (ground contact plane at rest).
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
    float roll = 0.f;       // -1..1. Planes/helis: +1 roll right.
    float yaw = 0.f;        // -1..1. Planes: rudder. Helis: tail rotor (+1 nose right).
    float lift = 0.f;       // -1..1. Helis: collective (+1 climb). Planes: unused.
    bool engineOff = false; // request engine off (parked / abandoned vehicles)
};

constexpr int kMaxWheels = 10;

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
void wheelLocalTransform(const VehicleState& s, int wheel, vec3& pos, quat& rot);
// Teleport / reset (keeps model), e.g. respawn upright.
void resetVehicle(VehicleState& s, dvec3 pos, float yaw);

}  // namespace Vehicles
