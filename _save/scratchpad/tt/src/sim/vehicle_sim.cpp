// Vehicle dynamics for every vehicle class (see vehicle_sim.h).
//
// Structure (unity build: every helper lives in the named namespace Vehicles::vsim):
//   vehicle_sim.cpp          shared step machinery: COM body, contact set + sequential-impulse solver, public API
//   vehicle_sim_setup.cpp    tuning derived from VehicleModel metadata, init/reset/placement
//   vehicle_sim_collide.cpp  contact generation (chassis vs ground, buildings, props, other vehicles), damage
//   vehicle_sim_wheels.cpp   raycast wheels, suspension, tire model, engine/gearbox, car + bike control
//   vehicle_sim_water.cpp    buoyancy (waves), planing hulls, props/jets/fans, cars sinking
//   vehicle_sim_air.cpp      fixed-wing aerodynamics + landing gear, helicopter rotor + stabilizer
//
// Integration scheme per (sub)step, all about the center of mass:
//   forces (gravity, suspension, tires, aero, buoyancy, thrust, assists) -> semi-implicit velocity update ->
//   contacts (speculative, with a velocity solve and a split-impulse position solve) -> pose update.
// Tire forces are capped by the force that would cancel the slip velocity within the step, and the wheel spin uses a
// linearly implicit update, so the model stays stable at 120 Hz and at standstill.
#include "vehicle_sim.h"
#include "waves.h"

namespace Vehicles {
namespace vsim {

constexpr float kGrav = 9.81f;
constexpr float kRhoAir = 1.225f;
constexpr float kRhoWater = 1025.f;
constexpr int kMaxContacts = 48;

inline bool isBikeClass(VehicleClass c) { return c == VC_MOTORBIKE || c == VC_SCOOTER; }
inline bool isBoatClass(VehicleClass c) { return c == VC_BOAT || c == VC_JETSKI || c == VC_AIRBOAT; }
inline bool isAirClass(VehicleClass c) { return c == VC_PLANE || c == VC_HELI; }
inline bool isRoadClass(VehicleClass c) { return c <= VC_FIRETRUCK; }
inline bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Rigid body about the center of mass, used during a step (VehicleState::body keeps the model origin).
struct Body {
    dvec3 pos;          // center of mass (world)
    quat rot;
    mat3 R;             // rotation matrix of rot
    vec3 vel, angVel;   // COM velocity, angular velocity (world)
    float mass = 1.f, invMass = 1.f;
    vec3 invI;          // local diagonal inverse inertia
    vec3 force, torque;
    vec3 local(vec3 w) const { return vec3(dot(R.c[0], w), dot(R.c[1], w), dot(R.c[2], w)); }
    vec3 invIw(vec3 v) const {
        vec3 l = local(v) * invI;
        return R.c[0] * l.x + R.c[1] * l.y + R.c[2] * l.z;
    }
    vec3 Iw(vec3 v) const {
        vec3 l = local(v);
        l = vec3(l.x / invI.x, l.y / invI.y, l.z / invI.z);
        return R.c[0] * l.x + R.c[1] * l.y + R.c[2] * l.z;
    }
    vec3 velAt(vec3 r) const { return vel + cross(angVel, r); }
    void addForce(vec3 f, vec3 r) { force += f; torque += cross(r, f); }
    void impulse(vec3 j, vec3 r) { vel += j * invMass; angVel += invIw(cross(r, j)); }
    float effMass(vec3 r, vec3 n) const {
        vec3 rn = cross(r, n);
        return 1.f / Max(invMass + dot(rn, invIw(rn)), 1e-9f);
    }
    // torque producing the angular acceleration `acc` (world) about the COM
    vec3 torqueFor(vec3 acc) const { return Iw(acc); }
};

enum ContactKind : u8 { CK_GROUND = 0, CK_STATIC, CK_BUMP, CK_STAND, CK_ROTOR };

struct Contact {
    vec3 r, n;          // point relative to the COM, normal pointing toward the vehicle
    float depth;        // > 0 penetration, < 0 speculative gap
    float mu, e;
    int collider;       // Phys collider id or -1
    u8 kind, surface;
    // solver scratch
    vec3 t1, t2;
    float mN, mT1, mT2, target, jn, jt1, jt2, jp;
};

struct ContactSet {
    Contact c[kMaxContacts];
    int n = 0;
    Contact* add(vec3 r, vec3 nrm, float depth, float mu, float e, int collider, u8 kind, u8 surface) {
        if (n >= kMaxContacts) {
            // replace the shallowest contact if this one is deeper
            int w = 0;
            for (int i = 1; i < n; i++)
                if (c[i].depth < c[w].depth) w = i;
            if (c[w].depth >= depth) return nullptr;
            n--;
            c[w] = c[n];
        }
        Contact& k = c[n++];
        k.r = r;
        k.n = nrm;
        k.depth = depth;
        k.mu = mu;
        k.e = e;
        k.collider = collider;
        k.kind = kind;
        k.surface = surface;
        return &k;
    }
};

// Sequential impulses (accumulated, clamped) against static geometry with Coulomb friction.
void solveVelocities(Body& b, ContactSet& cs, float dt, int iters) {
    for (int i = 0; i < cs.n; i++) {
        Contact& c = cs.c[i];
        c.t1 = normalize(anyPerp(c.n));
        c.t2 = cross(c.n, c.t1);
        c.mN = b.effMass(c.r, c.n);
        c.mT1 = b.effMass(c.r, c.t1);
        c.mT2 = b.effMass(c.r, c.t2);
        float vn = dot(b.velAt(c.r), c.n);
        c.target = 0.f;
        if (c.depth < 0.f) c.target = c.depth / dt;            // speculative: may close the gap this step
        else if (vn < -1.5f) c.target = -c.e * vn;             // bounce
        c.jn = c.jt1 = c.jt2 = c.jp = 0.f;
    }
    for (int it = 0; it < iters; it++) {
        for (int i = 0; i < cs.n; i++) {
            Contact& c = cs.c[i];
            float vn = dot(b.velAt(c.r), c.n);
            float jn = Max(c.jn + (c.target - vn) * c.mN, 0.f);
            float dj = jn - c.jn;
            c.jn = jn;
            if (dj != 0.f) b.impulse(c.n * dj, c.r);
            if (c.mu > 0.f && c.jn > 0.f) {
                vec3 v = b.velAt(c.r);
                float jt1 = c.jt1 - dot(v, c.t1) * c.mT1;
                float jt2 = c.jt2 - dot(v, c.t2) * c.mT2;
                float lim = c.mu * c.jn;
                float l2 = jt1 * jt1 + jt2 * jt2;
                if (l2 > lim * lim) {
                    float sc = lim / sqrtf(l2);
                    jt1 *= sc;
                    jt2 *= sc;
                }
                vec3 d = c.t1 * (jt1 - c.jt1) + c.t2 * (jt2 - c.jt2);
                c.jt1 = jt1;
                c.jt2 = jt2;
                b.impulse(d, c.r);
            }
        }
    }
}

// Split-impulse position correction: pseudo velocities that remove penetration without adding energy.
void solvePositions(Body& b, ContactSet& cs, float dt, int iters, vec3& dPos, vec3& dRot) {
    vec3 pv(0.f), pw(0.f);
    const float slop = 0.004f, beta = 0.3f;
    bool any = false;
    for (int i = 0; i < cs.n; i++)
        if (cs.c[i].depth > slop) any = true;
    if (any) {
        for (int it = 0; it < iters; it++)
            for (int i = 0; i < cs.n; i++) {
                Contact& c = cs.c[i];
                if (c.depth <= slop) continue;
                float vn = dot(pv + cross(pw, c.r), c.n);
                float target = beta * Min(c.depth - slop, 0.5f) / dt;
                float jp = Max(c.jp + (target - vn) * c.mN, 0.f);
                float dj = jp - c.jp;
                c.jp = jp;
                pv += c.n * (dj * b.invMass);
                pw += b.invIw(cross(c.r, c.n * dj));
            }
    }
    dPos = pv * dt;
    dRot = pw * dt;
}

inline quat integrateRot(quat q, vec3 w) {
    float a = length(w);
    if (a < 1e-9f) return q;
    return normalize(quatAxisAngle(w / a, a) * q);
}

// Surface properties
inline float surfaceGrip(u8 s) {
    switch (s) {
        case Phys::SURF_ASPHALT: return 1.0f;
        case Phys::SURF_CONCRETE: return 0.95f;
        case Phys::SURF_GRASS: return 0.6f;
        case Phys::SURF_DIRT: return 0.7f;
        case Phys::SURF_SAND: return 0.5f;
        case Phys::SURF_MUD: return 0.4f;
        case Phys::SURF_WOOD: case Phys::SURF_METAL: return 0.85f;
        case Phys::SURF_WATER: return 0.3f;
        default: return 0.8f;
    }
}
inline float surfaceRolling(u8 s) {
    switch (s) {
        case Phys::SURF_ASPHALT: case Phys::SURF_CONCRETE: return 0.013f;
        case Phys::SURF_WOOD: case Phys::SURF_METAL: return 0.016f;
        case Phys::SURF_GRASS: return 0.05f;
        case Phys::SURF_DIRT: return 0.035f;
        case Phys::SURF_SAND: return 0.13f;
        case Phys::SURF_MUD: return 0.16f;
        case Phys::SURF_WATER: return 0.2f;
        default: return 0.03f;
    }
}
// Body (metal) sliding friction on a surface
inline float surfaceScrape(u8 s) {
    switch (s) {
        case Phys::SURF_ASPHALT: case Phys::SURF_CONCRETE: return 0.5f;
        case Phys::SURF_GRASS: return 0.45f;
        case Phys::SURF_DIRT: return 0.55f;
        case Phys::SURF_SAND: return 0.6f;
        case Phys::SURF_MUD: return 0.5f;
        default: return 0.45f;
    }
}

// Per-step context shared by the class modules.
struct StepCtx {
    VehicleState* s;
    const VehicleModel* m;
    const VehicleControls* c;
    float dt;
    Body b;
    vec3 comW;              // COM world (float, for queries)
    vec3 fwd, right, up;    // body axes (world)
    vec3 vLocal;            // COM velocity in body axes
    float speed, vFwd;
    ContactSet cs;
    int wheelsInContact = 0;
    vec3 groundP, groundN;  // reference ground plane under the vehicle (from wheels or one query)
    bool groundValid = false;
    float maxStaticImpulse = 0.f;
    bool handbrake = false;
    vec3 toWorld(vec3 localPt) const { return b.R * (localPt - s->tune.com); }  // local model point -> COM-relative world
};

void clearEvents(VehicleState& s) {
    s.impactImpulse = 0.f;
    s.impactPoint = vec3(0.f);
    s.impactNormal = vec3(0.f);
    s.impactCollider = -1;
    s.ejectRider = false;
    s.brokenCount = 0;
    for (int i = 0; i < 4; i++) s.brokenIds[i] = -1;
    s.scrape = 0.f;
    s.splash = 0.f;
    s.rotorStrike = false;
    s.shifted = false;
}

}  // namespace vsim
}  // namespace Vehicles

#include "vehicle_sim_setup.cpp"
#include "vehicle_sim_collide.cpp"
#include "vehicle_sim_wheels.cpp"
#include "vehicle_sim_water.cpp"
#include "vehicle_sim_air.cpp"

namespace Vehicles {
namespace vsim {

bool wantsToMove(const VehicleState& s, const VehicleControls& c) {
    if (c.throttle > 0.02f) return true;
    if (c.hasDriver && (fabsf(c.steer) > 0.05f || fabsf(c.roll) > 0.05f)) return true;
    if (c.brake > 0.02f && !c.handbrake && c.hasDriver) return true;
    if (s.cls == VC_HELI) return s.rotorSpeed > 0.01f || (!c.engineOff && c.hasDriver && s.engineOn);
    if (s.cls == VC_PLANE) return s.throttleOut > 0.01f || fabsf(c.pitch) + fabsf(c.roll) + fabsf(c.yaw) > 0.05f;
    if (isBikeClass(s.cls) && c.hasDriver != s.prevHasDriver) return true;
    if (isBoatClass(s.cls) && fabsf(c.steer) > 0.05f && s.inWater) return true;
    if (isBikeClass(s.cls) && c.hasDriver && !s.riderOff && fabsf(s.lean) > 0.08f) return true;  // pick the bike up
    return false;
}

void stepOnce(VehicleState& s, const VehicleControls& c, float dt) {
    const VehicleModel& m = *s.model;
    VehicleTuning& t = s.tune;
    StepCtx x;
    x.s = &s;
    x.m = &m;
    x.c = &c;
    x.dt = dt;
    Body& b = x.b;
    b.rot = normalize(s.body.rot);
    b.R = mat3FromQuat(b.rot);
    vec3 comOff = b.R * t.com;
    b.pos = s.body.pos + comOff;
    b.angVel = s.body.angVel;
    b.vel = s.body.vel + cross(b.angVel, comOff);
    b.mass = s.body.mass;
    b.invMass = s.body.invMass;
    b.invI = s.body.invInertiaLocal;
    b.force = vec3(0.f, 0.f, -kGrav * b.mass);
    b.torque = vec3(0.f);
    x.comW = b.pos.toVec3();
    x.right = b.R.c[0];
    x.fwd = b.R.c[1];
    x.up = b.R.c[2];
    x.vLocal = b.local(b.vel);
    x.speed = length(b.vel);
    x.vFwd = x.vLocal.y;
    x.handbrake = c.handbrake;
    s.upsideDownTime = x.up.z < 0.f ? s.upsideDownTime + dt : 0.f;
    s.lean = atan2f(-x.right.z, x.up.z);

    // ---- forces by class ----
    if (isBoatClass(s.cls)) {
        boatForces(x);
        wheelForces(x);  // trailers / wheeled amphibious models (usually none)
    } else if (s.cls == VC_PLANE) {
        planeForces(x);
        wheelForces(x);
    } else if (s.cls == VC_HELI) {
        heliForces(x);
        wheelForces(x);
    } else {
        wheelForces(x);  // cars + bikes (powertrain, suspension, tires, assists)
    }
    if (!isBoatClass(s.cls)) swampForces(x);  // cars/aircraft in water: buoyancy, drag, flooding

    // ---- velocity update ----
    b.vel += b.force * (b.invMass * dt);
    b.angVel += b.invIw(b.torque) * dt;
    float wmax = isBikeClass(s.cls) ? 30.f : 22.f;
    float wl = length(b.angVel);
    if (wl > wmax) b.angVel *= wmax / wl;
    float vl = length(b.vel);
    if (vl > 170.f) b.vel *= 170.f / vl;

    // ---- contacts ----
    chassisGroundContacts(x);
    staticContacts(x);
    if (x.cs.n > 0) {
        solveVelocities(b, x.cs, dt, 8);
        vec3 dp, dr;
        solvePositions(b, x.cs, dt, 6, dp, dr);
        contactEvents(x);
        b.pos = b.pos + dp;
        b.rot = integrateRot(b.rot, dr);
    }

    // ---- pose update ----
    b.pos = b.pos + b.vel * dt;
    b.rot = integrateRot(b.rot, b.angVel * dt);
    b.R = mat3FromQuat(b.rot);

    // ---- write back (model origin) ----
    vec3 comOff2 = b.R * t.com;
    s.body.rot = b.rot;
    s.body.pos = b.pos - comOff2;
    s.body.angVel = b.angVel;
    s.body.vel = b.vel - cross(b.angVel, comOff2);
    s.body.force = vec3(0.f);
    s.body.torque = vec3(0.f);
    s.wheelsOnGround = x.wheelsInContact;
    s.airborneTime = (x.wheelsInContact == 0 && x.cs.n == 0 && !s.inWater) ? s.airborneTime + dt : 0.f;

    // ---- sleep ----
    bool calm = length2(b.vel) < 0.08f * 0.08f && length2(b.angVel) < 0.06f * 0.06f && !wantsToMove(s, c) && !s.inWater &&
                (x.wheelsInContact > 0 || x.cs.n > 0);
    s.sleepTimer = calm ? s.sleepTimer + dt : 0.f;
    if (s.sleepTimer > 0.8f) {
        s.sleeping = true;
        s.body.vel = vec3(0.f);
        s.body.angVel = vec3(0.f);
        s.throttleOut = 0.f;
        s.engineLoad = 0.f;
        Phys::GroundHit g = Phys::gCollision->ground(x.comW.x, x.comW.y, x.comW.z + 0.5f, 0.f);
        s.restZ = g.z;
        s.wakeCheck = 0;
        for (int i = 0; i < s.wheelCount; i++) {
            s.wheels[i].spinVel = 0.f;
            s.wheels[i].slip = 0.f;
            s.wheels[i].compressionVel = 0.f;
        }
    }
}

bool stateFinite(const VehicleState& s) {
    return std::isfinite(s.body.pos.x) && std::isfinite(s.body.pos.y) && std::isfinite(s.body.pos.z) && finite3(s.body.vel) &&
           finite3(s.body.angVel) && std::isfinite(s.body.rot.x) && std::isfinite(s.body.rot.y) && std::isfinite(s.body.rot.z) &&
           std::isfinite(s.body.rot.w);
}

}  // namespace vsim


dvec3 vehicleCenterOfMass(const VehicleState& s) { return s.body.pos + rotate(s.body.rot, s.tune.com); }

void stepVehicle(VehicleState& s, const VehicleControls& c, float dt) {
    vsim::clearEvents(s);
    if (!s.model || !(dt > 0.f) || !Phys::gCollision || !World::gMap) return;
    dt = Min(dt, 0.1f);
    if (s.sleeping) {
        bool wake = vsim::wantsToMove(s, c) || length2(s.body.vel) > 0.05f * 0.05f || length2(s.body.angVel) > 0.05f * 0.05f;
        if (!wake && ++s.wakeCheck >= 120) {
            // the ground under a sleeping vehicle can disappear (streamed-out roof, broken prop): re-check once a second
            s.wakeCheck = 0;
            vec3 com = vehicleCenterOfMass(s).toVec3();
            Phys::GroundHit g = Phys::gCollision->ground(com.x, com.y, com.z + 0.5f, 0.f);
            if (fabsf(g.z - s.restZ) > 0.05f || g.water) wake = true;
        }
        if (s.engineOn && c.engineOff) s.engineOn = false;
        s.engineRpm = s.engineOn ? s.tune.idleRpm : Max(0.f, s.engineRpm - 3000.f * dt);
        s.prevHasDriver = c.hasDriver;
        if (!wake) return;
        s.sleeping = false;
        s.sleepTimer = 0.f;
    }
    // backup for NaN recovery
    dvec3 p0 = s.body.pos;
    quat q0 = s.body.rot;
    // substeps: keep h <= 1/100 s and the motion per substep below ~0.5 m (fast vehicles vs thin props)
    int n = (int)ceilf(dt * 100.f - 1e-3f);
    n = Max(n, (int)ceilf(s.speed() * dt / 0.5f));
    n = Clamp(n, 1, 8);
    float h = dt / n;
    for (int i = 0; i < n && !s.sleeping; i++) vsim::stepOnce(s, c, h);
    s.prevHasDriver = c.hasDriver;
    if (!vsim::stateFinite(s)) {
        s.body.pos = p0;
        s.body.rot = normalize(q0);
        if (!std::isfinite(s.body.rot.w)) s.body.rot = quat();
        s.body.vel = vec3(0.f);
        s.body.angVel = vec3(0.f);
        for (int i = 0; i < s.wheelCount; i++) {
            s.wheels[i].spinVel = 0.f;
            s.wheels[i].compressionVel = 0.f;
        }
    }
}

void wheelLocalTransform(const VehicleState& s, int wheel, vec3& pos, quat& rot) {
    if (!s.model || wheel < 0 || wheel >= s.wheelCount) {
        pos = vec3(0.f);
        rot = quat();
        return;
    }
    const WheelSpec& ws = s.model->wheels[wheel];
    const WheelState& w = s.wheels[wheel];
    float travel = vsim::wheelTravel(s, wheel);
    float off = w.compression - s.tune.restComp[wheel];
    // retracted landing gear folds up into the body
    if (s.cls == VC_PLANE && s.gearDown < 1.f) off += (1.f - s.gearDown) * (ws.radius * 1.2f + travel);
    pos = ws.pos + vec3(0.f, 0.f, off);
    rot = quatAxisAngle(vec3(0, 0, 1), -w.steerAngle) * quatAxisAngle(vec3(1, 0, 0), -w.spinAngle);
}

void burstTire(VehicleState& s, int wheel) {
    if (wheel < 0 || wheel >= s.wheelCount) return;
    s.wheels[wheel].burst = true;
    s.sleeping = false;
}

}  // namespace Vehicles
