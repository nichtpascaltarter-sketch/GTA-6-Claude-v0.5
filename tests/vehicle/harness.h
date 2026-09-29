// Native test harness for the vehicle simulation: synthetic proving ground, test models, run helpers.
// Included by test_vehicle.cpp (single translation unit with the engine sources).
#pragma once
#include <chrono>
#include <map>
#include <functional>

namespace VT {

using namespace Vehicles;

// ------------------------------------------------------------------------------------------------------------------
// Synthetic proving ground (all on the real WorldMap / RoadNetwork / CollisionWorld code paths)
//   pad:     asphalt road x in [-5000, 3000], |y| <= 150, z = 0 (acceleration, braking, skidpad, runway)
//   ramp:    road deck along +x at y = 60 from x = 1000 (z 0) to x = 1020 (z 2), ends in the air
//   curb:    street with 3 m sidewalks (+0.15 m) centered at y = -100, x in [-200, 200]
//   wall:    building box face at x = 2000 for |y| < 60 (20 m high)
//   props:   streetlights (breakable) at x = 1500, y = -40..40 step 8; hydrants at x = 1400; palms at x = 1300
//   lake:    x in [3100, 6600], |y| < 1600, bottom -10 m, water 0, beach slope from x = 3100 (40 m wide)
//   pier:    road deck at y = -600 rising to 1 m, ending over 10 m deep water at x = 3300
//   mud:     x in [3500, 6500], y in [1700, 2600] shallow water over mud; y in [2600, 3400] dry mud
struct ProvingGround {
    World::WorldMap map;
    World::RoadNetwork roads;
    Phys::CollisionWorld cw;
    int nextCell = 1;

    static int tex(float w) { return Clamp((int)floorf((w + World::kWorldHalf) / World::kHeightCell), 0, World::kHeightRes - 1); }

    void fillRect(float x0, float y0, float x1, float y1, const std::function<void(size_t, float, float)>& f) {
        for (int ty = tex(y0); ty <= tex(y1); ty++)
            for (int tx = tex(x0); tx <= tex(x1); tx++) {
                size_t i = (size_t)ty * World::kHeightRes + tx;
                f(i, World::texelToWorld(tx), World::texelToWorld(ty));
            }
    }

    int addNode(vec2 p, float z) {
        World::RoadNode n;
        n.p = p;
        n.z = z;
        n.radius = 0.f;
        roads.nodes.push_back(n);
        return (int)roads.nodes.size() - 1;
    }
    void addRoad(const std::vector<vec3>& pts, float halfWidth, float sidewalk) {
        World::RoadEdge e;
        e.n0 = addNode(pts.front().xy(), pts.front().z);
        e.n1 = addNode(pts.back().xy(), pts.back().z);
        e.cls = World::RC_STREET;
        e.halfWidth = halfWidth;
        e.sidewalk = sidewalk;
        e.pts = pts;
        e.dist.push_back(0.f);
        for (size_t i = 1; i < pts.size(); i++) e.dist.push_back(e.dist.back() + length(pts[i].xy() - pts[i - 1].xy()));
        e.length = e.dist.back();
        roads.edges.push_back(e);
    }

    void build() {
        const size_t R = World::kHeightRes;
        map.height.assign(R * R, 0.f);
        map.waterLevel.assign(R * R, World::kNoWater);
        map.region.assign(R * R, 0);
        map.splat0.assign(R * R, packRGBA8(0.f, 1.f, 0.f, 0.f));  // grass
        map.splat1.assign(R * R, packRGBA8(0.f, 0.f, 0.f, 0.f));
        // lake with a beach slope
        fillRect(3000.f, -1700.f, 6700.f, 1700.f, [&](size_t i, float x, float y) {
            float d = Min(Min(x - 3100.f, 6600.f - x), Min(y + 1600.f, 1600.f - y));  // distance inside the lake box
            float z = -Clamp(d / 40.f, 0.f, 1.f) * 10.f;  // 40 m beach slope down to -10
            map.height[i] = z;
            if (z < -0.05f) map.waterLevel[i] = 0.f;
            map.splat0[i] = packRGBA8(1.f, 0.f, 0.f, 0.f);  // sand
        });
        // mud flats north of the lake: shallow water, then dry mud
        fillRect(3500.f, 1600.f, 6500.f, 3400.f, [&](size_t i, float x, float y) {
            if (y < 1700.f) return;
            map.height[i] = y < 2600.f ? 0.1f : 0.35f;
            map.waterLevel[i] = y < 2600.f ? 0.28f : World::kNoWater;
            map.splat0[i] = packRGBA8(0.f, 0.f, 0.f, 0.f);
            map.splat1[i] = packRGBA8(1.f, 0.f, 0.f, 0.f);  // mud
        });
        // pad + test roads
        addRoad({vec3(-5000.f, 0.f, 0.f), vec3(3000.f, 0.f, 0.f)}, 150.f, 0.f);
        addRoad({vec3(990.f, 60.f, 0.f), vec3(1000.f, 60.f, 0.02f), vec3(1020.f, 60.f, 2.0f)}, 4.f, 0.f);
        addRoad({vec3(-200.f, -100.f, 0.f), vec3(200.f, -100.f, 0.f)}, 5.f, 3.f);
        // pier: rises to 1 m over the beach and ends above 10 m deep water at x = 3300
        addRoad({vec3(2990.f, -600.f, 0.f), vec3(3000.f, -600.f, 0.f), vec3(3100.f, -600.f, 1.0f), vec3(3300.f, -600.f, 1.0f)}, 4.f, 0.f);
        roads.buildHash();
        World::gMap = &map;
        World::gRoads = &roads;
        Phys::gCollision = &cw;
        resetColliders();
    }

    // (Re)creates the static colliders (wall, tower, breakable props) under a fresh cell key so every test run
    // starts with unbroken props (CollisionWorld remembers broken props per cell key).
    int currentCell = -1;
    void resetColliders() {
        if (currentCell >= 0) cw.removeCell(currentCell);
        currentCell = nextCell++;
        std::vector<World::CollisionBox> boxes;
        World::CollisionBox wall;
        wall.c = vec3(2010.f, 0.f, 10.f);
        wall.ax = vec2(1.f, 0.f);
        wall.he = vec3(10.f, 60.f, 13.f);
        boxes.push_back(wall);
        // a small building for rotor strike tests
        World::CollisionBox tower;
        tower.c = vec3(-2000.f, 400.f, 20.f);
        tower.ax = vec2(1.f, 0.f);
        tower.he = vec3(10.f, 10.f, 20.f);
        boxes.push_back(tower);
        std::vector<World::PropInstance> props;
        for (int k = -5; k <= 5; k++) {
            World::PropInstance p{};
            p.pos = vec3(1500.f, k * 8.f, 0.f);
            p.yaw = 0.f;
            p.scale = 1.f;
            p.type = World::PROP_STREETLIGHT;
            props.push_back(p);
            p.pos = vec3(1400.f, k * 8.f, 0.f);
            p.type = World::PROP_HYDRANT;
            props.push_back(p);
            p.pos = vec3(1300.f, k * 8.f, 0.f);
            p.type = World::PROP_PALM;
            props.push_back(p);
        }
        cw.addCell(currentCell, boxes, props);
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Plausible stand-in models per class (used until src/sim/vehicle_models.cpp exists, and to compare against it)
inline void addCarWheels(VehicleModel& m, float r, float w, float xf, float yf, float xr, float yr, bool fwd, bool rwd) {
    m.wheels.clear();
    m.wheels.push_back({vec3(-xf, yf, r), r, w, true, fwd, true});
    m.wheels.push_back({vec3(xf, yf, r), r, w, true, fwd, false});
    m.wheels.push_back({vec3(-xr, yr, r), r, w, false, rwd, true});
    m.wheels.push_back({vec3(xr, yr, r), r, w, false, rwd, false});
}

inline VehicleModel makeTestModel(VehicleClass cls) {
    VehicleModel m;
    m.cls = cls;
    m.name = "test";
    m.seats.push_back({vec3(-0.4f, 0.f, 0.7f), true, true});
    switch (cls) {
        case VC_COMPACT:
            m.mass = 1150; m.power = 78; m.torque = 145; m.maxRpm = 6500; m.topSpeed = 48; m.gears = 5; m.driveFront = 1.f;
            m.brakeForce = 7500; m.grip = 0.95f; m.suspensionTravel = 0.17f; m.dragCoef = 0.32f; m.frontalArea = 2.0f;
            m.centerOfMass = vec3(0, 0.15f, 0.5f); m.boxCenter = vec3(0, 0.05f, 0.75f); m.boxHalf = vec3(0.85f, 1.95f, 0.6f);
            addCarWheels(m, 0.3f, 0.2f, 0.74f, 1.25f, 0.74f, -1.2f, true, false);
            break;
        case VC_SEDAN: case VC_TAXI: case VC_POLICE:
            m.mass = 1500; m.power = cls == VC_POLICE ? 220.f : 125.f; m.torque = cls == VC_POLICE ? 400.f : 250.f; m.maxRpm = 6300;
            m.topSpeed = cls == VC_POLICE ? 64.f : 52.f; m.gears = 5; m.driveFront = 0.f; m.brakeForce = 9500; m.grip = 1.0f;
            m.suspensionTravel = 0.18f; m.dragCoef = 0.31f; m.frontalArea = 2.2f;
            m.centerOfMass = vec3(0, 0.1f, 0.52f); m.boxCenter = vec3(0, 0, 0.78f); m.boxHalf = vec3(0.9f, 2.4f, 0.62f);
            addCarWheels(m, 0.33f, 0.23f, 0.79f, 1.42f, 0.79f, -1.42f, false, true);
            break;
        case VC_COUPE:
            m.mass = 1400; m.power = 180; m.torque = 320; m.maxRpm = 7000; m.topSpeed = 62; m.gears = 6; m.driveFront = 0.f;
            m.brakeForce = 10000; m.grip = 1.05f; m.suspensionTravel = 0.15f; m.suspensionStiffness = 1.2f; m.dragCoef = 0.31f; m.frontalArea = 2.05f;
            m.centerOfMass = vec3(0, 0.05f, 0.48f); m.boxCenter = vec3(0, 0, 0.7f); m.boxHalf = vec3(0.9f, 2.25f, 0.56f);
            addCarWheels(m, 0.33f, 0.24f, 0.8f, 1.35f, 0.8f, -1.35f, false, true);
            break;
        case VC_SPORTS:
            m.mass = 1400; m.power = 330; m.torque = 520; m.maxRpm = 7600; m.topSpeed = 80; m.gears = 6; m.driveFront = 0.f;
            m.brakeForce = 12500; m.grip = 1.15f; m.suspensionTravel = 0.13f; m.suspensionStiffness = 1.4f; m.dragCoef = 0.33f;
            m.frontalArea = 2.0f; m.downforce = 0.3f;
            m.centerOfMass = vec3(0, -0.1f, 0.45f); m.boxCenter = vec3(0, 0, 0.62f); m.boxHalf = vec3(0.95f, 2.25f, 0.52f);
            addCarWheels(m, 0.34f, 0.27f, 0.82f, 1.35f, 0.84f, -1.3f, false, true);
            break;
        case VC_SUPER:
            m.mass = 1450; m.power = 540; m.torque = 720; m.maxRpm = 8500; m.topSpeed = 92; m.gears = 7; m.driveFront = 0.35f;
            m.brakeForce = 14000; m.grip = 1.25f; m.suspensionTravel = 0.12f; m.suspensionStiffness = 1.6f; m.dragCoef = 0.35f;
            m.frontalArea = 2.0f; m.downforce = 0.6f;
            m.centerOfMass = vec3(0, -0.2f, 0.42f); m.boxCenter = vec3(0, 0, 0.58f); m.boxHalf = vec3(1.0f, 2.3f, 0.5f);
            addCarWheels(m, 0.34f, 0.3f, 0.85f, 1.35f, 0.87f, -1.35f, true, true);
            break;
        case VC_MUSCLE:
            m.mass = 1700; m.power = 340; m.torque = 620; m.maxRpm = 6000; m.topSpeed = 72; m.gears = 5; m.driveFront = 0.f;
            m.brakeForce = 10500; m.grip = 1.0f; m.suspensionTravel = 0.17f; m.dragCoef = 0.36f; m.frontalArea = 2.2f;
            m.centerOfMass = vec3(0, 0.15f, 0.5f); m.boxCenter = vec3(0, 0, 0.72f); m.boxHalf = vec3(0.95f, 2.45f, 0.6f);
            addCarWheels(m, 0.34f, 0.26f, 0.8f, 1.45f, 0.8f, -1.4f, false, true);
            break;
        case VC_SUV:
            m.mass = 2200; m.power = 220; m.torque = 450; m.maxRpm = 6000; m.topSpeed = 55; m.gears = 6; m.driveFront = 0.4f;
            m.brakeForce = 11500; m.grip = 0.95f; m.suspensionTravel = 0.24f; m.dragCoef = 0.38f; m.frontalArea = 2.9f;
            m.centerOfMass = vec3(0, 0.1f, 0.78f); m.boxCenter = vec3(0, 0, 1.0f); m.boxHalf = vec3(0.98f, 2.4f, 0.85f);
            addCarWheels(m, 0.38f, 0.26f, 0.84f, 1.45f, 0.84f, -1.45f, true, true);
            break;
        case VC_PICKUP:
            m.mass = 2150; m.power = 230; m.torque = 520; m.maxRpm = 5600; m.topSpeed = 52; m.gears = 6; m.driveFront = 0.f;
            m.brakeForce = 11000; m.grip = 0.95f; m.suspensionTravel = 0.25f; m.dragCoef = 0.4f; m.frontalArea = 2.9f;
            m.centerOfMass = vec3(0, 0.35f, 0.75f); m.boxCenter = vec3(0, 0, 1.0f); m.boxHalf = vec3(1.0f, 2.75f, 0.85f);
            addCarWheels(m, 0.39f, 0.27f, 0.86f, 1.75f, 0.86f, -1.75f, false, true);
            break;
        case VC_VAN: case VC_AMBULANCE: case VC_SERVICE:
            m.mass = 2600; m.power = 140; m.torque = 340; m.maxRpm = 5500; m.topSpeed = 44; m.gears = 5; m.driveFront = 0.f;
            m.brakeForce = 12000; m.grip = 0.92f; m.suspensionTravel = 0.2f; m.dragCoef = 0.38f; m.frontalArea = 3.6f;
            m.centerOfMass = vec3(0, 0.3f, 0.88f); m.boxCenter = vec3(0, 0, 1.2f); m.boxHalf = vec3(1.0f, 2.6f, 1.1f);
            addCarWheels(m, 0.36f, 0.24f, 0.86f, 1.65f, 0.86f, -1.6f, false, true);
            break;
        case VC_BUS:
            m.mass = 12000; m.power = 220; m.torque = 1200; m.maxRpm = 2400; m.topSpeed = 28; m.gears = 6; m.driveFront = 0.f;
            m.brakeForce = 32000; m.grip = 0.9f; m.suspensionTravel = 0.2f; m.dragCoef = 0.6f; m.frontalArea = 7.5f;
            m.centerOfMass = vec3(0, -0.4f, 1.15f); m.boxCenter = vec3(0, 0, 1.7f); m.boxHalf = vec3(1.27f, 6.0f, 1.55f);
            addCarWheels(m, 0.5f, 0.3f, 1.05f, 3.8f, 1.05f, -2.8f, false, true);
            break;
        case VC_TRUCK: case VC_FIRETRUCK:
            m.mass = 9500; m.power = 260; m.torque = 1600; m.maxRpm = 2500; m.topSpeed = 30; m.gears = 8; m.driveFront = 0.f;
            m.brakeForce = 26000; m.grip = 0.9f; m.suspensionTravel = 0.2f; m.dragCoef = 0.7f; m.frontalArea = 7.5f;
            m.centerOfMass = vec3(0, 0.4f, 1.2f); m.boxCenter = vec3(0, 0, 1.75f); m.boxHalf = vec3(1.25f, 4.2f, 1.6f);
            m.wheels.clear();
            m.wheels.push_back({vec3(-1.02f, 2.9f, 0.52f), 0.52f, 0.32f, true, false, true});
            m.wheels.push_back({vec3(1.02f, 2.9f, 0.52f), 0.52f, 0.32f, true, false, false});
            m.wheels.push_back({vec3(-1.02f, -1.5f, 0.52f), 0.52f, 0.5f, false, true, true});
            m.wheels.push_back({vec3(1.02f, -1.5f, 0.52f), 0.52f, 0.5f, false, true, false});
            m.wheels.push_back({vec3(-1.02f, -2.85f, 0.52f), 0.52f, 0.5f, false, true, true});
            m.wheels.push_back({vec3(1.02f, -2.85f, 0.52f), 0.52f, 0.5f, false, true, false});
            break;
        case VC_MOTORBIKE:
            m.mass = 210; m.power = 110; m.torque = 110; m.maxRpm = 11500; m.topSpeed = 75; m.gears = 6; m.driveFront = 0.f;
            m.brakeForce = 3500; m.grip = 1.1f; m.suspensionTravel = 0.13f; m.dragCoef = 0.6f; m.frontalArea = 0.75f;
            m.centerOfMass = vec3(0, -0.05f, 0.58f); m.boxCenter = vec3(0, 0, 0.62f); m.boxHalf = vec3(0.36f, 1.05f, 0.52f);
            m.wheels.clear();
            m.wheels.push_back({vec3(0, 0.72f, 0.31f), 0.31f, 0.12f, true, false, false});
            m.wheels.push_back({vec3(0, -0.72f, 0.31f), 0.31f, 0.18f, false, true, false});
            m.seats[0] = {vec3(0, -0.2f, 0.85f), true, true};
            break;
        case VC_SCOOTER:
            m.mass = 115; m.power = 9; m.torque = 14; m.maxRpm = 8000; m.topSpeed = 24; m.gears = 1; m.driveFront = 0.f;
            m.brakeForce = 1800; m.grip = 0.95f; m.suspensionTravel = 0.09f; m.dragCoef = 0.7f; m.frontalArea = 0.7f;
            m.centerOfMass = vec3(0, -0.05f, 0.45f); m.boxCenter = vec3(0, 0, 0.55f); m.boxHalf = vec3(0.33f, 0.85f, 0.5f);
            m.wheels.clear();
            m.wheels.push_back({vec3(0, 0.62f, 0.22f), 0.22f, 0.1f, true, false, false});
            m.wheels.push_back({vec3(0, -0.6f, 0.22f), 0.22f, 0.12f, false, true, false});
            m.seats[0] = {vec3(0, -0.2f, 0.75f), true, true};
            break;
        case VC_BOAT:
            m.mass = 1900; m.power = 260; m.torque = 450; m.maxRpm = 5800; m.topSpeed = 24; m.gears = 1;
            m.dragCoef = 0.4f; m.frontalArea = 3.f;
            m.centerOfMass = vec3(0, -0.4f, 0.55f); m.boxCenter = vec3(0, 0, 0.65f); m.boxHalf = vec3(1.15f, 3.5f, 0.65f);
            m.floatPoints = {vec3(-0.8f, 2.3f, 0.12f), vec3(0.8f, 2.3f, 0.12f), vec3(-0.95f, 0.f, 0.02f), vec3(0.95f, 0.f, 0.02f),
                             vec3(-0.95f, -2.8f, 0.0f), vec3(0.95f, -2.8f, 0.0f), vec3(0, 3.2f, 0.35f), vec3(0, -3.3f, 0.02f)};
            m.wheels.clear();
            break;
        case VC_JETSKI:
            m.mass = 360; m.power = 90; m.torque = 110; m.maxRpm = 7500; m.topSpeed = 22; m.gears = 1;
            m.dragCoef = 0.5f; m.frontalArea = 1.f;
            m.centerOfMass = vec3(0, -0.1f, 0.35f); m.boxCenter = vec3(0, 0, 0.45f); m.boxHalf = vec3(0.55f, 1.55f, 0.45f);
            m.floatPoints = {vec3(-0.4f, 1.0f, 0.05f), vec3(0.4f, 1.0f, 0.05f), vec3(-0.45f, -1.2f, 0.f), vec3(0.45f, -1.2f, 0.f)};
            m.seats[0] = {vec3(0, -0.3f, 0.75f), true, true};
            m.wheels.clear();
            break;
        case VC_AIRBOAT:
            m.mass = 950; m.power = 260; m.torque = 500; m.maxRpm = 3200; m.topSpeed = 25; m.gears = 1;
            m.dragCoef = 0.6f; m.frontalArea = 3.2f;
            m.centerOfMass = vec3(0, -0.5f, 0.55f); m.boxCenter = vec3(0, 0, 0.35f); m.boxHalf = vec3(1.15f, 2.7f, 0.35f);
            m.floatPoints = {vec3(-0.9f, 2.f, 0.02f), vec3(0.9f, 2.f, 0.02f), vec3(-1.f, 0.f, 0.f), vec3(1.f, 0.f, 0.f),
                             vec3(-1.f, -2.3f, 0.f), vec3(1.f, -2.3f, 0.f)};
            m.rotorPos = vec3(0, -2.1f, 1.9f);
            m.wheels.clear();
            break;
        case VC_PLANE:
            m.mass = 1150; m.power = 140; m.torque = 400; m.maxRpm = 2700; m.topSpeed = 65; m.gears = 1;
            m.brakeForce = 4000; m.grip = 0.9f; m.suspensionTravel = 0.15f; m.dragCoef = 0.3f; m.frontalArea = 2.2f;
            m.wingArea = 16.2f; m.liftSlope = 5.0f;
            m.centerOfMass = vec3(0, 0.1f, 1.25f); m.boxCenter = vec3(0, -0.6f, 1.4f); m.boxHalf = vec3(5.5f, 4.1f, 1.2f);
            m.wheels.clear();
            m.wheels.push_back({vec3(0, 1.75f, 0.2f), 0.2f, 0.12f, true, false, false});
            m.wheels.push_back({vec3(-1.25f, -0.15f, 0.28f), 0.28f, 0.15f, false, false, true});
            m.wheels.push_back({vec3(1.25f, -0.15f, 0.28f), 0.28f, 0.15f, false, false, false});
            m.rotorPos = vec3(0, 3.6f, 1.3f);
            break;
        case VC_HELI:
            m.mass = 2300; m.power = 650; m.torque = 1500; m.maxRpm = 6000; m.topSpeed = 70; m.gears = 1;
            m.dragCoef = 0.4f; m.frontalArea = 3.f; m.rotorRadius = 5.4f;
            m.centerOfMass = vec3(0, 0.f, 1.3f); m.boxCenter = vec3(0, -1.0f, 1.45f); m.boxHalf = vec3(1.1f, 4.8f, 1.25f);
            m.rotorPos = vec3(0, 0.f, 3.0f);
            m.wheels.clear();
            break;
        default: break;
    }
    return m;
}

inline const char* className(VehicleClass c) {
    static const char* n[] = {"compact", "sedan", "coupe", "suv", "pickup", "sports", "super", "muscle", "van", "bus", "truck",
                              "service", "police", "taxi", "ambulance", "firetruck", "motorbike", "scooter", "boat", "jetski",
                              "airboat", "plane", "heli"};
    return c < VC_COUNT ? n[c] : "?";
}

// ------------------------------------------------------------------------------------------------------------------
// Run helpers
struct Sample {
    float t;
    vec3 pos, vel, fwd, up;
    float speed, vFwd, yawRate, lean;
    int wheels;
};

struct Runner {
    VehicleState s;
    const VehicleModel* m = nullptr;
    float t = 0.f;
    double stepUs = 0.0;
    long steps = 0;
    std::vector<Sample> log;
    bool record = false;
    float dtFixed = 1.f / 120.f;
    Rng* jitter = nullptr;
    int traceEvery = 0;             // print telemetry every N steps (0 = off)
    const char* scenario = "";

    void init(const VehicleModel& model, vec3 pos, float yaw) {
        m = &model;
        initVehicle(s, model, 0, dvec3(pos), yaw);
        t = 0.f;
    }
    // Runs until `until` returns true or tmax seconds pass. ctl fills the controls each step.
    template <typename Ctl, typename Stop>
    float run(float tmax, Ctl ctl, Stop until) {
        float t0 = t;
        while (t - t0 < tmax) {
            VehicleControls c;
            ctl(c, *this);
            float dt = jitter ? jitter->range(1.f / 150.f, 1.f / 60.f) : dtFixed;
            auto a = std::chrono::high_resolution_clock::now();
            stepVehicle(s, c, dt);
            auto b = std::chrono::high_resolution_clock::now();
            stepUs += std::chrono::duration<double, std::micro>(b - a).count();
            steps++;
            t += dt;
            if (record) log.push_back(sample());
            if (traceEvery > 0 && steps % traceEvery == 0) trace(c);
            if (until(*this)) return t - t0;
        }
        return -1.f;
    }
    Sample sample() const {
        Sample q;
        q.t = t;
        q.pos = s.body.pos.toVec3();
        q.vel = s.body.vel;
        q.fwd = s.forward();
        q.up = s.up();
        q.speed = s.speed();
        q.vFwd = s.forwardSpeed();
        q.yawRate = s.body.angVel.z;
        q.lean = s.lean;
        q.wheels = s.wheelsOnGround;
        return q;
    }
    void trace(const VehicleControls& c) const {
        vec3 p = pos(), v = s.body.vel;
        float slipMax = 0.f;
        for (int i = 0; i < s.wheelCount; i++) slipMax = Max(slipMax, s.wheels[i].slip);
        printf("[%s] t=%6.2f p=(%8.2f %8.2f %6.2f) v=%6.2f vf=%6.2f vz=%5.2f yawr=%5.2f roll=%5.1f pitch=%5.1f hdg=%6.1f w=%d g=%d rpm=%5.0f thr=%.2f slip=%.2f sub=%.2f up=%.2f hp=%.0f imp=%.0f/%d ctl(t%.1f b%.1f s%.2f h%d p%.1f)\n",
               scenario, t, p.x, p.y, p.z, length(v), s.forwardSpeed(), v.z, s.body.angVel.z, s.lean * kRadToDeg,
               asinf(Clamp(s.forward().z, -1.f, 1.f)) * kRadToDeg, heading() * kRadToDeg, s.wheelsOnGround, s.gear, s.engineRpm,
               s.throttleOut, slipMax, s.submerged, s.up().z, s.health, s.impactImpulse, s.impactCollider, c.throttle, c.brake, c.steer, (int)c.handbrake, c.pitch);
        if (wheelTrace)
            for (int i = 0; i < s.wheelCount; i++) {
                const WheelState& w = s.wheels[i];
                printf("    w%d c=%d load=%6.0f comp=%.3f slip=%5.2f lat=%6.2f spinv=%7.2f (surf %.2f) steer=%5.2f\n", i, (int)w.contact, w.load,
                       w.compression, w.slip, w.lateralSlip, w.spinVel, w.spinVel * m->wheels[i].radius, w.steerAngle);
            }
    }
    bool wheelTrace = false;
    vec3 pos() const { return s.body.pos.toVec3(); }
    float heading() const { vec3 f = s.forward(); return atan2f(f.y, f.x); }
    float pitch() const { return asinf(Clamp(s.forward().z, -1.f, 1.f)); }
    float roll() const { return s.lean; }
};

// Steering toward a target point (pure pursuit). Returns steer in [-1, 1] (+ right).
inline float steerTo(const Runner& r, vec2 target, float gain = 2.f) {
    vec3 f = r.s.forward();
    vec2 h = normalize(vec2(f.x, f.y));
    vec2 d = normalize(target - r.pos().xy());
    float ang = atan2f(cross(h, d), dot(h, d));  // + = target to the left
    return Clamp(-ang * gain, -1.f, 1.f);
}
// Throttle/brake to hold a speed
inline void holdSpeed(VehicleControls& c, const Runner& r, float v) {
    float e = v - r.s.forwardSpeed();
    c.throttle = Saturate(e * 0.5f + 0.25f * (v > 1.f));
    c.brake = e < -1.5f ? Saturate(-e * 0.3f) : 0.f;
}

inline float wrapPi(float a) { return wrapAngle(a); }

}  // namespace VT
