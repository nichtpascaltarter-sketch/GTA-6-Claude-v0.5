// Native (Linux) test harness for the vehicle dynamics (src/sim/vehicle_sim*.cpp).
// Build: g++ -O2 -std=c++17 -I src tests/vehicle/test_vehicle.cpp -o /tmp/test_vehicle -lpthread
// Usage: test_vehicle [--synthetic] [--real] [--class NAME] [--models] [--perf] [--out FILE]
//   --synthetic  proving-ground telemetry for every class (stand-in models unless --models)
//   --models     use the real models from src/sim/vehicle_models.cpp (when present)
//   --real       drive on the generated world (roads, curbs, bridges, buildings)
#include "../../tools/native_stubs.cpp"
#include "../../src/core/math.cpp"
#include "../../src/core/noise.cpp"
#include "../../src/core/jobs.cpp"
#include "../../src/render/mesh.cpp"
#include "../../src/world/worldmap.cpp"
#include "../../src/world/roads.cpp"
#include "../../src/world/roadmesh.cpp"
#include "../../src/world/buildings.cpp"
#include "../../src/world/buildmesh.cpp"
#include "../../src/world/propmesh.cpp"
#include "../../src/world/cellgen.cpp"
#include "../../src/sim/physics.cpp"
#if __has_include("../../src/sim/vehicle_models.cpp")
#include "../../src/sim/vehicle_models.cpp"
#define HAVE_MODELS 1
#endif
#include "../../src/sim/vehicle_sim.cpp"
#include "harness.h"
#include <thread>

using namespace VT;

namespace VT {

FILE* gOut = stdout;
std::vector<std::string> gIssues;

void issue(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gIssues.push_back(buf);
}

const vec3 kPadStart(-4800.f, 0.f, 0.f);

// ---------------------------------------------------------------------------------------------------------------
// Car / bike tests
struct CarReport {
    std::string name;
    VehicleClass cls;
    float t100 = -1, topSpeed = 0, brake100 = -1, latG = 0, laneChangeBeta = 0, laneOk = 0, hb180 = -1, hbTime = -1;
    float jumpPitch = 0, jumpRoll = 0, jumpBounces = 0, jumpUpright = 0, jumpHeadingErr = 0;
    float wallRebound = 0, wallPen = 0, wallHealth = 0, wallEngine = 0, wallStopDist = 0;
    float rollMax = 0, rollLift = 0, rolledOver = 0;
    float curbSlow = 0, curbAngle = 0;
    float propBrokeFast = 0, propDv = 0, propBrokeSlow = 0;
    float flipRest = 0, flipBack = -1;
    float sinkTime = -1, engineDeadTime = -1, floatTime = 0;
    float tunnel = 0;
    float jitterT100 = -1;
    float sleepTime = -1;
    double usPerStep = 0, usSleep = 0;
    // bikes
    float lean10 = 0, lean20 = 0, lean30 = 0, radius20 = 0, wheelieMax = 0, wheelieTime = 0, stoppieMax = 0, crashEject = 0, parkedLean = 0;
};

float accelTo(Runner& r, float v, float tmax) {
    return r.run(tmax, [](VehicleControls& c, Runner&) { c.throttle = 1.f; },
                 [&](Runner& q) { return q.s.forwardSpeed() >= v; });
}

int gTrace = 0;
bool gWheelTrace = false;
const char* gTraceScenario = nullptr;
struct ScenarioTrace {
    Runner& r;
    ScenarioTrace(Runner& rr, const char* name) : r(rr) {
        r.scenario = name;
        r.traceEvery = (gTrace && (!gTraceScenario || !strcmp(gTraceScenario, name))) ? gTrace : 0;
        r.wheelTrace = gWheelTrace;
    }
};

void testCar(const VehicleModel& m, CarReport& rep) {
    bool bike = m.cls == VC_MOTORBIKE || m.cls == VC_SCOOTER;
    Runner r;
    // ---- 0-100 and top speed ----
    ScenarioTrace st_accel(r, "accel");
    r.init(m, kPadStart, -kHalfPi);  // heading +x
    rep.t100 = accelTo(r, 27.78f, 40.f);
    float vmax = 0.f;
    float lastV = 0.f, stableT = 0.f;
    r.run(90.f, [](VehicleControls& c, Runner&) { c.throttle = 1.f; },
          [&](Runner& q) {
              float v = q.s.forwardSpeed();
              vmax = Max(vmax, v);
              stableT = fabsf(v - lastV) < 0.002f ? stableT + 1.f / 120.f : 0.f;
              lastV = v;
              return q.pos().x > 2800.f || stableT > 3.f;
          });
    rep.topSpeed = vmax;
    // ---- braking 100-0 ----
    ScenarioTrace st_brake(r, "brake");
    r.init(m, kPadStart, -kHalfPi);
    if (accelTo(r, 27.9f, 40.f) > 0.f) {
        vec3 p0 = r.pos();
        float tb = r.run(20.f, [](VehicleControls& c, Runner&) { c.brake = 1.f; }, [](Runner& q) { return q.s.forwardSpeed() < 0.3f; });
        rep.brake100 = tb > 0.f ? length(r.pos() - p0) : -1.f;
    }
    // ---- skidpad (R = 45 m circle, speed ramps until the car cannot hold the line) ----
    ScenarioTrace st_skidpad(r, "skidpad");
    {
        float R0 = 45.f;
        vec2 C(-4000.f, 80.f);
        r.init(m, vec3(C.x + R0, C.y, 0.f), 0.f);  // on the circle, heading +y (counter-clockwise)
        float vT = 9.f, best = 0.f, acc = 0.f;
        int n = 0;
        r.run(80.f,
              [&](VehicleControls& c, Runner& q) {
                  vec2 p = q.pos().xy() - C;
                  float ang = atan2f(p.y, p.x) + 0.35f;
                  vec2 tgt = C + vec2(cosf(ang), sinf(ang)) * R0;
                  c.steer = steerTo(q, tgt, 2.5f);
                  holdSpeed(c, q, vT);
                  vT += 0.35f / 120.f;
              },
              [&](Runner& q) {
                  vec2 p = q.pos().xy() - C;
                  float err = fabsf(length(p) - R0);
                  float v = q.s.speed();
                  float a = fabsf(v * q.s.body.angVel.z) / 9.81f;
                  if (err < 2.5f) {
                      acc += a;
                      n++;
                      if (n >= 120) {
                          best = Max(best, acc / n);
                          acc = 0.f;
                          n = 0;
                      }
                  }
                  return err > 6.f;
              });
        rep.latG = best;
    }
    // ---- lane change at 90 km/h (bikes 80): 3.5 m offset over 25 m and back ----
    ScenarioTrace st_lane(r, "lane");
    {
        r.init(m, kPadStart + vec3(0, -40.f, 0), -kHalfPi);
        float vT = bike ? 22.f : 25.f;
        accelTo(r, vT, 40.f);
        float x0 = r.pos().x, betaMax = 0.f;
        bool spun = false;
        r.run(8.f,
              [&](VehicleControls& c, Runner& q) {
                  float dx = q.pos().x - x0;
                  float yOff = dx < 20.f ? 0.f : (dx < 45.f ? 3.5f * SmoothStep(20.f, 45.f, dx) : (dx < 70.f ? 3.5f : 3.5f * (1.f - SmoothStep(70.f, 95.f, dx))));
                  c.steer = steerTo(q, vec2(q.pos().x + 18.f, -40.f + yOff), 3.f);
                  holdSpeed(c, q, vT);
              },
              [&](Runner& q) {
                  vec3 v = q.s.body.vel;
                  float beta = fabsf(atan2f(dot(v, q.s.right()), dot(v, q.s.forward())));
                  betaMax = Max(betaMax, beta);
                  if (beta > 0.5f) spun = true;
                  return false;
              });
        rep.laneChangeBeta = betaMax * kRadToDeg;
        // harsh driver error: full left 0.45 s, full right 0.45 s at 100 km/h
        r.init(m, kPadStart + vec3(0, 40.f, 0), -kHalfPi);
        accelTo(r, bike ? 25.f : 27.8f, 40.f);
        float t0 = r.t;
        r.run(6.f,
              [&](VehicleControls& c, Runner& q) {
                  float tt = q.t - t0;
                  c.steer = tt < 0.45f ? -1.f : (tt < 0.9f ? 1.f : 0.f);
                  c.throttle = 0.3f;
              },
              [&](Runner& q) {
                  vec3 v = q.s.body.vel;
                  float beta = fabsf(atan2f(dot(v, q.s.right()), dot(v, q.s.forward())));
                  if (beta > 0.6f && q.s.speed() > 5.f) spun = true;
                  return false;
              });
        rep.laneOk = spun ? 0.f : 1.f;
    }
    // ---- handbrake 180 at 60 km/h (cars) ----
    ScenarioTrace st_hb180(r, "hb180");
    if (!bike) {
        r.init(m, kPadStart + vec3(0, 100.f, 0), -kHalfPi);
        accelTo(r, 16.7f, 30.f);
        float h0 = r.heading(), t0 = r.t, maxTurn = 0.f;
        float tDone = r.run(6.f,
                            [&](VehicleControls& c, Runner& q) {
                                float tt = q.t - t0;
                                c.steer = tt < 1.4f ? -1.f : 0.f;
                                c.handbrake = tt < 1.2f;
                            },
                            [&](Runner& q) {
                                float d = fabsf(wrapPi(q.heading() - h0));
                                maxTurn = Max(maxTurn, d);
                                return d > 150.f * kDegToRad;
                            });
        rep.hb180 = maxTurn * kRadToDeg;
        rep.hbTime = tDone;
    }
    // ---- 2 m ramp at 90 km/h ----
    ScenarioTrace st_jump(r, "jump");
    {
        float vJ = 25.f;
        r.init(m, vec3(600.f, 60.f, 0.f), -kHalfPi);
        r.run(40.f,
              [&](VehicleControls& c, Runner& q) {
                  c.steer = steerTo(q, vec2(q.pos().x + 25.f, 60.f), 2.f);
                  holdSpeed(c, q, vJ);
                  if (q.pos().x > 960.f) c.steer = steerTo(q, vec2(q.pos().x + 25.f, 60.f), 1.f);
              },
              [&](Runner& q) { return q.pos().x > 1019.f; });
        float h0 = r.heading();
        float pMax = 0.f, rMax = 0.f;
        int bounces = 0;
        bool landed = false;
        int lastW = r.s.wheelsOnGround;
        float tLand = -1.f;
        r.run(6.f,
              [&](VehicleControls& c, Runner& q) { holdSpeed(c, q, vJ); },
              [&](Runner& q) {
                  pMax = Max(pMax, fabsf(q.pitch()));
                  rMax = Max(rMax, fabsf(q.roll()));
                  int w = q.s.wheelsOnGround;
                  if (!landed && w > 0 && q.pos().x > 1030.f) {
                      landed = true;
                      tLand = q.t;
                  }
                  if (landed && lastW == q.s.wheelsOnGround && false) bounces++;
                  if (landed && lastW > 0 && w == 0) bounces++;
                  lastW = w;
                  return landed && q.t - tLand > 3.f;
              });
        rep.jumpPitch = pMax * kRadToDeg;
        rep.jumpRoll = rMax * kRadToDeg;
        rep.jumpBounces = (float)bounces;
        rep.jumpUpright = r.s.up().z;
        rep.jumpHeadingErr = fabsf(wrapPi(r.heading() - h0)) * kRadToDeg;
    }
    // ---- wall at 60 km/h ----
    ScenarioTrace st_wall(r, "wall");
    {
        r.init(m, vec3(1700.f, 20.f, 0.f), -kHalfPi);
        float vW = 16.7f;
        r.run(30.f,
              [&](VehicleControls& c, Runner& q) {
                  c.steer = steerTo(q, vec2(q.pos().x + 20.f, 20.f), 2.f);
                  holdSpeed(c, q, vW);
              },
              [&](Runner& q) { return q.pos().x > 1990.f - m.boxHalf.y - 3.f; });
        float front0 = 0.f, maxPen = 0.f, minV = 1e9f, reb = 0.f;
        float h0 = r.s.health;
        r.run(3.f,
              [&](VehicleControls& c, Runner& q) { c.throttle = q.t < 0.f ? 1.f : 0.f; },
              [&](Runner& q) {
                  vec3 f = q.s.forward();
                  vec3 frontPt = q.pos() + rotate(q.s.body.rot, m.boxCenter + vec3(0, m.boxHalf.y, 0));
                  (void)f;
                  maxPen = Max(maxPen, frontPt.x - 2000.f);
                  minV = Min(minV, q.s.body.vel.x);
                  return false;
              });
        (void)front0;
        reb = -minV;
        rep.wallRebound = reb;
        rep.wallPen = maxPen;
        rep.wallHealth = r.s.health;
        rep.wallEngine = r.s.engineHealth;
        (void)h0;
    }
    // ---- 60 m/s wall hit: no tunnelling ----
    ScenarioTrace st_tunnel(r, "tunnel");
    {
        r.init(m, vec3(1900.f, -20.f, 0.f), -kHalfPi);
        r.s.body.vel = vec3(60.f, 0.f, 0.f);
        for (int i = 0; i < r.s.wheelCount; i++) r.s.wheels[i].spinVel = 60.f / m.wheels[i].radius;
        r.run(3.f, [](VehicleControls& c, Runner&) { c.throttle = 1.f; }, [](Runner&) { return false; });
        rep.tunnel = r.pos().x > 2000.f ? 1.f : 0.f;
    }
    // ---- rollover: step steer at 100 km/h (cars) ----
    ScenarioTrace st_rollover(r, "rollover");
    if (!bike) {
        r.init(m, kPadStart + vec3(0, -100.f, 0), -kHalfPi);
        accelTo(r, 27.8f, 40.f);
        float t0 = r.t, rollMax = 0.f;
        int liftMax = 0;
        bool over = false;
        r.run(4.f,
              [&](VehicleControls& c, Runner& q) {
                  c.steer = 1.f;
                  c.throttle = 0.5f;
              },
              [&](Runner& q) {
                  rollMax = Max(rollMax, fabsf(q.roll()));
                  int off = 0;
                  for (int i = 0; i < q.s.wheelCount; i++) off += !q.s.wheels[i].contact;
                  if (q.t - t0 > 0.2f) liftMax = Max(liftMax, off);
                  if (q.s.up().z < 0.3f) over = true;
                  return false;
              });
        rep.rollMax = rollMax * kRadToDeg;
        rep.rollLift = (float)liftMax;
        rep.rolledOver = over ? 1.f : 0.f;
        // harsher: handbrake + full lock at 100 km/h
        r.init(m, kPadStart + vec3(0, -130.f, 0), -kHalfPi);
        accelTo(r, 27.8f, 40.f);
        t0 = r.t;
        r.run(4.f,
              [&](VehicleControls& c, Runner& q) {
                  c.steer = 1.f;
                  c.handbrake = q.t - t0 < 1.f;
              },
              [&](Runner& q) {
                  if (q.s.up().z < 0.3f) over = true;
                  rollMax = Max(rollMax, fabsf(q.roll()));
                  return false;
              });
        if (over) rep.rolledOver = 1.f;
    }
    // ---- curb: cross a street with 0.15 m sidewalks (up, down, up, down) at 3 m/s straight and 10 m/s at 30 deg ----
    ScenarioTrace st_curb(r, "curb");
    {
        r.init(m, vec3(0.f, -120.f, 0.f), 0.f);  // heading north across the street at y = -100
        float zMax = 0.f, z0 = (float)r.s.body.pos.z;
        float tc = r.run(20.f,
              [&](VehicleControls& c, Runner& q) {
                  holdSpeed(c, q, 3.f);
                  c.steer = steerTo(q, vec2(0.f, q.pos().y + 10.f));
              },
              [&](Runner& q) {
                  zMax = Max(zMax, (float)q.s.body.pos.z - z0);
                  return q.pos().y > -85.f;
              });
        rep.curbSlow = tc > 0.f ? zMax : -1.f;
        r.init(m, vec3(-40.f, -125.f, 0.f), -kPi / 3.f);  // 30 degrees to the curb line
        zMax = 0.f;
        z0 = (float)r.s.body.pos.z;
        tc = r.run(15.f,
              [&](VehicleControls& c, Runner& q) { holdSpeed(c, q, 10.f); },
              [&](Runner& q) {
                  zMax = Max(zMax, (float)q.s.body.pos.z - z0);
                  return q.pos().y > -85.f;
              });
        rep.curbAngle = tc > 0.f ? zMax : -1.f;
    }
    // ---- breakable props: streetlight at 15 m/s breaks, at 2 m/s it holds ----
    ScenarioTrace st_props(r, "props");
    {
        Phys::gCollision->removeCell(1);
        // re-add props (fresh, unbroken): new cell key each time
        std::vector<World::CollisionBox> boxes;
        std::vector<World::PropInstance> props;
        World::PropInstance p{};
        p.pos = vec3(1500.f, 0.f, 0.f);
        p.scale = 1.f;
        p.type = World::PROP_STREETLIGHT;
        props.push_back(p);
        p.pos = vec3(1500.f, 30.f, 0.f);
        props.push_back(p);
        World::CollisionBox wall;
        wall.c = vec3(2010.f, 0.f, 10.f);
        wall.ax = vec2(1.f, 0.f);
        wall.he = vec3(10.f, 60.f, 13.f);
        boxes.push_back(wall);
        static int key = 100;
        Phys::gCollision->addCell(key++, boxes, props);
        r.init(m, vec3(1420.f, 0.f, 0.f), -kHalfPi);
        float v0 = 0.f, v1 = 0.f;
        r.run(20.f, [&](VehicleControls& c, Runner& q) { holdSpeed(c, q, 15.f); c.steer = steerTo(q, vec2(1600.f, 0.f)); },
              [&](Runner& q) { return q.pos().x > 1500.f - m.boxHalf.y - 1.5f; });
        v0 = r.s.forwardSpeed();
        int broke = 0;
        r.run(1.f, [&](VehicleControls& c, Runner&) {}, [&](Runner& q) {
            broke += q.s.brokenCount;
            return false;
        });
        v1 = r.s.forwardSpeed();
        rep.propBrokeFast = (float)broke;
        rep.propDv = v0 - v1;
        r.init(m, vec3(1480.f, 30.f, 0.f), -kHalfPi);
        broke = 0;
        r.run(8.f, [&](VehicleControls& c, Runner& q) { holdSpeed(c, q, 2.f); c.steer = steerTo(q, vec2(1600.f, 30.f)); },
              [&](Runner& q) {
                  broke += q.s.brokenCount;
                  return false;
              });
        rep.propBrokeSlow = (float)broke + (r.pos().x > 1500.f ? 10.f : 0.f);
    }
    // ---- upside down: rests on the roof, flip-back with input (cars) ----
    ScenarioTrace st_flip(r, "flip");
    if (!bike) {
        r.init(m, vec3(-3000.f, -60.f, 0.f), 0.f);
        r.s.body.rot = quatAxisAngle(vec3(0, 1, 0), kPi) * r.s.body.rot;
        r.s.body.pos.z += 2.5;
        r.run(4.f, [](VehicleControls&, Runner&) {}, [](Runner&) { return false; });
        float jit = length(r.s.body.vel) + length(r.s.body.angVel);
        rep.flipRest = r.s.up().z < -0.8f && jit < 0.05f ? 1.f : 0.f;
        float tf = r.run(8.f, [](VehicleControls& c, Runner&) { c.steer = 1.f; }, [](Runner& q) { return q.s.up().z > 0.9f && q.s.wheelsOnGround >= 3; });
        rep.flipBack = tf;
    }
    // ---- drive off the pier into deep water: floats, floods, engine dies ----
    ScenarioTrace st_lake(r, "lake");
    {
        r.init(m, vec3(2800.f, -600.f, 0.f), -kHalfPi);
        float tIn = -1.f;
        r.run(60.f,
              [&](VehicleControls& c, Runner& q) {
                  holdSpeed(c, q, 12.f);
                  c.steer = steerTo(q, vec2(q.pos().x + 20.f, -600.f));
              },
              [&](Runner& q) {
                  if (tIn < 0.f && q.s.inWater) tIn = q.t;
                  float roofDepth = q.s.waterDepth - (m.boxCenter.z + m.boxHalf.z);
                  if (tIn >= 0.f && roofDepth < 0.f) rep.floatTime = q.t - tIn;
                  if (rep.engineDeadTime < 0.f && q.s.engineFlooded && tIn >= 0.f) rep.engineDeadTime = q.t - tIn;
                  if (tIn >= 0.f && roofDepth > 0.3f && rep.sinkTime < 0.f) rep.sinkTime = q.t - tIn;
                  return rep.sinkTime > 0.f && q.t - tIn > rep.sinkTime + 1.f;
              });
    }
    // ---- dt jitter ----
    ScenarioTrace st_jitter(r, "jitter");
    {
        Rng rng(7);
        r.jitter = &rng;
        r.init(m, kPadStart + vec3(0, 20.f, 0), -kHalfPi);
        rep.jitterT100 = accelTo(r, 27.78f, 40.f);
        r.run(3.f, [](VehicleControls& c, Runner&) { c.steer = 1.f; c.throttle = 1.f; }, [](Runner&) { return false; });
        r.jitter = nullptr;
    }
    // ---- sleep + perf ----
    ScenarioTrace st_perf(r, "perf");
    {
        r.init(m, vec3(-3500.f, 50.f, 0.f), 0.3f);
        rep.sleepTime = r.run(5.f, [](VehicleControls& c, Runner&) { c.handbrake = true; c.brake = 1.f; }, [](Runner& q) { return q.s.sleeping; });
        r.stepUs = 0;
        r.steps = 0;
        r.run(1.f, [](VehicleControls& c, Runner&) { c.handbrake = true; c.brake = 1.f; }, [](Runner&) { return false; });
        rep.usSleep = r.stepUs / Max(r.steps, 1L);
        // driving perf: circle at 20 m/s
        r.init(m, vec3(-2500.f, 50.f, 0.f), -kHalfPi);
        accelTo(r, 15.f, 20.f);
        r.stepUs = 0;
        r.steps = 0;
        r.run(10.f, [](VehicleControls& c, Runner& q) { c.steer = 0.4f; holdSpeed(c, q, 15.f); }, [](Runner&) { return false; });
        rep.usPerStep = r.stepUs / Max(r.steps, 1L);
    }
    // ---- bikes: lean vs speed, wheelie, stoppie, crash, parking ----
    ScenarioTrace st_bike(r, "bike");
    if (bike) {
        auto leanAt = [&](float v, float& radius) {
            r.init(m, vec3(-3000.f, 0.f, 0.f), -kHalfPi);
            accelTo(r, v, 30.f);
            float leanAcc = 0.f, radAcc = 0.f;
            int n = 0;
            float t0 = r.t;
            r.run(5.f, [&](VehicleControls& c, Runner& q) { c.steer = 1.f; holdSpeed(c, q, v); },
                  [&](Runner& q) {
                      if (q.t - t0 > 3.f) {
                          leanAcc += q.roll();
                          radAcc += q.s.speed() / Max(fabsf(q.s.body.angVel.z), 1e-3f);
                          n++;
                      }
                      return false;
                  });
            radius = n ? radAcc / n : 0.f;
            return n ? leanAcc / n * kRadToDeg : 0.f;
        };
        float rad;
        rep.lean10 = leanAt(10.f, rad);
        rep.lean20 = leanAt(20.f, rad);
        rep.radius20 = rad;
        rep.lean30 = m.topSpeed > 32.f ? leanAt(30.f, rad) : 0.f;
        // wheelie: from 6 m/s, full throttle + lean back
        r.init(m, kPadStart, -kHalfPi);
        accelTo(r, 6.f, 10.f);
        float pMax = 0.f, tW = 0.f;
        r.run(6.f, [](VehicleControls& c, Runner&) { c.throttle = 1.f; c.pitch = 1.f; },
              [&](Runner& q) {
                  pMax = Max(pMax, q.pitch());
                  if (!q.s.wheels[0].contact && q.s.wheels[1].contact) tW += 1.f / 120.f;
                  return false;
              });
        rep.wheelieMax = pMax * kRadToDeg;
        rep.wheelieTime = tW;
        // stoppie from 15 m/s
        r.init(m, kPadStart, -kHalfPi);
        accelTo(r, 15.f, 20.f);
        pMax = 0.f;
        r.run(4.f, [](VehicleControls& c, Runner&) { c.brake = 1.f; c.pitch = -1.f; }, [&](Runner& q) {
            pMax = Min(pMax, q.pitch());
            return q.s.speed() < 0.5f;
        });
        rep.stoppieMax = pMax * kRadToDeg;
        // crash into the wall at 15 m/s: rider ejected
        r.init(m, vec3(1900.f, 20.f, 0.f), -kHalfPi);
        bool ej = false;
        r.run(15.f, [](VehicleControls& c, Runner& q) { holdSpeed(c, q, 15.f); }, [&](Runner& q) {
            ej = ej || q.s.ejectRider;
            return ej;
        });
        rep.crashEject = ej ? 1.f : 0.f;
        // parked, no rider: rests on the kickstand
        r.init(m, vec3(-3000.f, -50.f, 0.f), 0.f);
        r.run(5.f, [](VehicleControls& c, Runner&) { c.hasDriver = false; c.handbrake = true; c.brake = 1.f; }, [](Runner&) { return false; });
        rep.parkedLean = r.roll() * kRadToDeg;
    }
}

void printCarTable(const std::vector<CarReport>& reps) {
    fprintf(gOut, "\n### Road vehicles (proving ground)\n\n");
    fprintf(gOut, "| model | class | 0-100 s | top km/h | 100-0 m | lat g | lanechg beta deg | no spin | hb turn deg / s | jump pitch/roll deg | bounces | upright | wall rebound m/s | wall pen m | health/engine | roll deg | wheels lifted | rolled | curb 3/10 m/s dz | prop fast/dv/slow | roof rest / flip s | sink s / engine s | 60m/s tunnel | jitter 0-100 | sleep s | us/step | us sleep |\n");
    fprintf(gOut, "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (auto& r : reps) {
        if (r.cls == VC_MOTORBIKE || r.cls == VC_SCOOTER) continue;
        fprintf(gOut, "| %s | %s | %.1f | %.0f | %.1f | %.2f | %.1f | %s | %.0f / %.1f | %.0f / %.0f | %.0f | %.2f | %.1f | %.2f | %.0f / %.0f | %.1f | %.0f | %s | %.2f / %.2f | %.0f / %.1f / %.0f | %s / %.1f | %.1f / %.1f | %s | %.1f | %.1f | %.1f | %.2f |\n",
                r.name.c_str(), className(r.cls), r.t100, r.topSpeed * 3.6f, r.brake100, r.latG, r.laneChangeBeta, r.laneOk > 0 ? "yes" : "NO",
                r.hb180, r.hbTime, r.jumpPitch, r.jumpRoll, r.jumpBounces, r.jumpUpright, r.wallRebound, r.wallPen, r.wallHealth, r.wallEngine,
                r.rollMax, r.rollLift, r.rolledOver > 0 ? "YES" : "no", r.curbSlow, r.curbAngle, r.propBrokeFast, r.propDv, r.propBrokeSlow,
                r.flipRest > 0 ? "yes" : "NO", r.flipBack, r.sinkTime, r.engineDeadTime, r.tunnel > 0 ? "YES" : "no", r.jitterT100, r.sleepTime,
                r.usPerStep, r.usSleep);
    }
}

void printBikeTable(const std::vector<CarReport>& reps) {
    fprintf(gOut, "\n### Bikes (proving ground)\n\n");
    fprintf(gOut, "| model | class | 0-100 s | top km/h | 100-0 m | lat g | lanechg beta | no spin | lean 10/20/30 m/s deg | radius@20 m | wheelie deg / s | stoppie deg | wall eject | parked lean deg | jump pitch/roll | upright | curb dz | 60m/s tunnel | us/step |\n");
    fprintf(gOut, "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (auto& r : reps) {
        if (r.cls != VC_MOTORBIKE && r.cls != VC_SCOOTER) continue;
        fprintf(gOut, "| %s | %s | %.1f | %.0f | %.1f | %.2f | %.1f | %s | %.0f / %.0f / %.0f | %.0f | %.0f / %.1f | %.0f | %s | %.1f | %.0f / %.0f | %.2f | %.2f | %s | %.1f |\n",
                r.name.c_str(), className(r.cls), r.t100, r.topSpeed * 3.6f, r.brake100, r.latG, r.laneChangeBeta, r.laneOk > 0 ? "yes" : "NO",
                r.lean10, r.lean20, r.lean30, r.radius20, r.wheelieMax, r.wheelieTime, r.stoppieMax, r.crashEject > 0 ? "yes" : "NO",
                r.parkedLean, r.jumpPitch, r.jumpRoll, r.jumpUpright, r.curbSlow, r.tunnel > 0 ? "YES" : "no", r.usPerStep);
    }
}

}  // namespace VT

int main(int argc, char** argv) {
    bool synthetic = true, real = false, useModels = false;
    const char* only = nullptr;
    const char* outPath = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--real")) real = true;
        else if (!strcmp(argv[i], "--models")) useModels = true;
        else if (!strcmp(argv[i], "--class") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!strcmp(argv[i], "--nosynthetic")) synthetic = false;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) gTrace = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) gTraceScenario = argv[++i];
        else if (!strcmp(argv[i], "--wheels")) gWheelTrace = true;
    }
    if (outPath) gOut = fopen(outPath, "w");
    static ProvingGround pg;
    pg.build();
    std::vector<VehicleModel> models;
#ifdef HAVE_MODELS
    if (useModels) {
        int n = modelCount();
        models.resize(n);
        for (int i = 0; i < n; i++) buildModel(i, models[i]);
    }
#endif
    if (models.empty()) {
        for (int c = 0; c < VC_COUNT; c++) {
            VehicleClass cls = (VehicleClass)c;
            if (cls == VC_TAXI || cls == VC_AMBULANCE || cls == VC_FIRETRUCK || cls == VC_SERVICE) continue;
            models.push_back(makeTestModel(cls));
            models.back().name = std::string("std-") + className(cls);
        }
    }
    if (synthetic) {
        std::vector<CarReport> cars;
        for (auto& m : models) {
            if (only && strcmp(only, className(m.cls)) && strcmp(only, m.name.c_str())) continue;
            if (m.cls >= VC_BOAT) continue;
            CarReport rep;
            rep.name = m.name;
            rep.cls = m.cls;
            testCar(m, rep);
            cars.push_back(rep);
            fprintf(stderr, "done %s\n", m.name.c_str());
        }
        printCarTable(cars);
        printBikeTable(cars);
    }
    if (!gIssues.empty()) {
        fprintf(gOut, "\n### Issues\n");
        for (auto& s : gIssues) fprintf(gOut, "- %s\n", s.c_str());
    }
    if (gOut != stdout) fclose(gOut);
    return 0;
}
