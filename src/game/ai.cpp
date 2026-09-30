// AI orchestration: builds the lane/sidewalk graphs, fills the perception proxies for the tick, runs population,
// police dispatch, ambient events, pedestrian brains and traffic drivers, and exposes the signal phases to gameplay and
// rendering. The engine-independent cores live in lanes.cpp / traffic_core.cpp / pednav.cpp (ai_core.h).
#include "gameworld.h"
#include <functional>

namespace Game {

namespace ai_detail {

float wrapAng(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

void faceTowards(Ped& p, vec2 dir, float rate, float dt) {
    if (length2(dir) < 1e-6f) return;
    float ty = atan2f(-dir.x, dir.y);
    float d = wrapAng(ty - p.yaw);
    p.yaw = wrapAng(p.yaw + Clamp(d, -rate * dt, rate * dt));
}

// Connects the renderer's traffic-light lamps to the AI signal phases when the renderer exposes a
// `std::function<int(int edge, vec2 propPos)> signalLampFn` member (see the AI integration notes); otherwise no-op.
template <typename R>
auto connectSignalLamps(R* r, std::function<int(int, vec2)> fn, int) -> decltype(r->signalLampFn = fn, void()) {
    r->signalLampFn = fn;
}
template <typename R>
void connectSignalLamps(R*, std::function<int(int, vec2)>, long) {}

}  // namespace ai_detail

using namespace ai_detail;

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::initAI() {
    if (ai.ready || !roads) return;
    laneGraph.build(*roads);
    traffic.init(&laneGraph);
    pedNav.init(&laneGraph, &traffic);
    ai.ready = true;
    if (renderer) {
        GameWorld* self = this;
        connectSignalLamps(renderer, std::function<int(int, vec2)>([self](int edge, vec2 pos) { return self->laneGraph.lampStateForEdge(edge, pos, self->time); }), 0);
    }
}

PedAI& GameWorld::pedAI(int id) {
    if (id >= (int)ai.ped.size()) ai.ped.resize(id + 32);
    PedAI& a = ai.ped[id];
    if (a.uid != peds[id].uid) {
        a = PedAI();
        a.uid = peds[id].uid;
        u32 h = hash32(a.uid * 0x9E3779B9u + 11u);
        float r = hashToFloat(h);
        a.temper = r < 0.3f ? 0 : (r < 0.8f ? 1 : 2);
        a.think = hashToFloat(hash32(h)) * 0.5f;
        a.lastPos = peds[id].pos.toVec3().xy();
    }
    return a;
}

VehAI& GameWorld::vehAI(int id) {
    if (id >= (int)ai.veh.size()) ai.veh.resize(id + 32);
    VehAI& a = ai.veh[id];
    if (a.uid != vehicles[id].uid) {
        a = VehAI();
        a.uid = vehicles[id].uid;
        a.barkTimer = hashToFloat(hash32(a.uid)) * 5.f;
    }
    return a;
}

bool GameWorld::inCameraView(vec3 p, float margin) const {
    const Render::Camera& cam = rig.cam;
    vec3 d = rel(p, cam.pos);
    float l = length(d);
    if (l < 2.f + margin) return true;
    vec3 f = cam.forward();
    float along = dot(d, f);
    if (along < -margin) return false;
    float halfV = cam.fovY * 0.5f;
    float halfH = atanf(tanf(halfV) * 1.8f);   // wide screens
    float ang = acosf(Clamp(along / l, -1.f, 1.f));
    float slack = atanf(margin / Max(l, 1.f));
    return ang < Max(halfH, halfV) + slack + 0.08f;
}

// ------------------------------------------------------------------------------------------------------------------
// Perception proxies for this tick
void GameWorld::aiBuildBodies() {
    std::vector<AI::Body>& B = traffic.bodies;
    B.clear();
    ai.vehBody.assign(vehicles.size(), -1);
    ai.pedBody.assign(peds.size(), -1);
    int pv = playerVehicle();
    for (int vi = 0; vi < (int)vehicles.size(); vi++) {
        Vehicle& v = vehicles[vi];
        if (!v.used) continue;
        if (isAircraft(vi) && v.sim.agl > 4.f) continue;
        const Vehicles::VehicleModel& m = vassets[v.model].spec;
        AI::Body b;
        mat3 R = v.sim.body.rotMat();
        vec3 ctr = v.sim.body.pos.toVec3() + R * m.boxCenter;
        b.pos = ctr.xy();
        b.z = (float)v.sim.body.pos.z;
        vec3 f = v.sim.forward();
        b.fwd = length2(f.xy()) > 1e-6f ? normalize(f.xy()) : vec2(0, 1);
        AI::Driver* d = traffic.get(vi);
        if (d && v.seats[0] < 0) {
            // nobody at the wheel any more (driver bailed out, was pulled out or killed): no longer lane traffic
            traffic.detach(vi);
            vehAI(vi).managed = false;
            d = nullptr;
        }
        // the player's car keeps a driver slot after autoplay: it only counts as AI while it is being driven by it
        if (d && vi == pv && time - d->lastDriveTime > 0.5) d = nullptr;
        if (d && d->dummy) b.vel = b.fwd * d->vDummy;
        else b.vel = v.sim.body.vel.xy();
        b.speed = length(b.vel);
        b.halfLen = m.boxHalf.y;
        b.halfWid = m.boxHalf.x;
        b.host = vi;
        b.driver = d ? vi : -1;
        b.kind = AI::BK_CAR;
        u16 fl = 0;
        if (vi == pv) fl |= AI::BF_PLAYER;
        if (v.sirenOn) fl |= AI::BF_SIREN;
        if (v.faction == FAC_POLICE) fl |= AI::BF_POLICE;
        if (v.seats[0] < 0 || v.parked) fl |= AI::BF_PARKED;
        if (v.sim.wrecked || v.exploded) fl |= AI::BF_WRECK;
        if (d) fl |= AI::BF_AI;
        if (d && d->dummy) fl |= AI::BF_DUMMY;
        if (isBike(vi)) fl |= AI::BF_BIKE;
        if (m.boxHalf.y > 3.5f) fl |= AI::BF_BIG;
        b.flags = fl;
        ai.vehBody[vi] = (int)B.size();
        B.push_back(b);
    }
    ai.pedBody0 = (int)B.size();
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.state == PS_INVEHICLE) continue;
        AI::Body b;
        b.pos = p.pos.toVec3().xy();
        b.z = (float)p.pos.z;
        b.vel = p.vel.xy();
        b.speed = length(b.vel);
        b.fwd = AI::yawDir(p.yaw);
        b.halfLen = b.halfWid = (p.state == PS_DEAD || p.state == PS_RAGDOLL) ? 0.9f : 0.3f;
        b.host = i;
        b.kind = AI::BK_PED;
        u16 fl = 0;
        if (p.isPlayer) fl |= AI::BF_PLAYER;
        if (p.weapon != WPN_FISTS && weaponInfo(p.weapon).clipSize > 0) fl |= AI::BF_ARMED;
        if (p.faction == FAC_POLICE) fl |= AI::BF_POLICE;
        if (!p.isPlayer && i < (int)ai.ped.size() && ai.ped[i].uid == p.uid && ai.ped[i].walk.state == AI::WS_CROSSING) fl |= AI::BF_CROSSING;
        b.flags = fl;
        ai.pedBody[i] = (int)B.size();
        B.push_back(b);
    }
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updateAI(float dt) {
    double t0 = TimeSeconds();
    if (!ai.ready) initAI();
    if (!ai.ready) return;
    ai.barkGlobal = Max(0.f, ai.barkGlobal - dt);
    for (auto& pa : ai.ped) pa.barkCooldown = Max(0.f, pa.barkCooldown - dt);
    aiBuildBodies();
    traffic.beginTick(time);
    pedNav.time = time;
    double t1 = TimeSeconds();
    updatePopulation(dt);
    double t2 = TimeSeconds();
    updateDispatch(dt);
    double t3 = TimeSeconds();
    updateEvents(dt);
    aiUpdateThreats(dt);
    double t4 = TimeSeconds();
    double tTraffic = 0.0;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.isPlayer || p.brain.type == BRAIN_NONE) continue;
        if (p.state == PS_DEAD || p.state == PS_RAGDOLL || p.state == PS_GETUP) continue;
        if (p.takedownT >= 0.f) continue;   // a synced stealth takedown (melee.cpp) drives both bodies
        if (p.state == PS_INVEHICLE && p.seat == 0) {
            double a = TimeSeconds();
            updateBrain(i, dt);
            tTraffic += TimeSeconds() - a;
        } else {
            updateBrain(i, dt);
        }
    }
    double t5 = TimeSeconds();
    // statistics (shown in the debug overlay / autoplay telemetry)
    AIFrameStats& st = ai.stats;
    double total = (t5 - t0) * 1000.0;
    st.msAI = total;
    st.msPop = (t2 - t1) * 1000.0;
    st.msPolice = (t3 - t2) * 1000.0;
    st.msEvents = (t4 - t3) * 1000.0;
    st.msTraffic = tTraffic * 1000.0;
    st.msPeds = (t5 - t4) * 1000.0 - st.msTraffic;
    st.frames++;
    st.avgMs = st.avgMs * 0.98 + total * 0.02;
    st.maxMs = Max(st.maxMs * 0.995, total);
    int np = 0, nc = 0, nd = 0, nm = 0;
    for (auto& p : peds) np += p.used && !p.isPlayer;
    for (int i = 0; i < (int)vehicles.size(); i++)
        if (vehicles[i].used) {
            nc++;
            AI::Driver* d = traffic.get(i);
            if (d) {
                nm++;
                nd += d->dummy;
            }
        }
    st.peds = np;
    st.cars = nc;
    st.dummies = nd;
    st.managed = nm;
}

std::string GameWorld::aiDebugText() const {
    const AIFrameStats& s = ai.stats;
    return StrFormat("AI %.2f ms (avg %.2f, max %.2f): traffic %.2f peds %.2f pop %.2f police %.2f events %.2f | peds %d cars %d (AI %d, dummy %d)",
                     s.msAI, s.avgMs, s.maxMs, s.msTraffic, s.msPeds, s.msPop, s.msPolice, s.msEvents, s.peds, s.cars, s.managed, s.dummies);
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updateBrain(int id, float dt) {
    Ped& p = peds[id];
    Brain& b = p.brain;
    b.timer += dt;
    b.thinkTimer -= dt;
    if (p.state == PS_INVEHICLE) {
        if (p.vehicle < 0 || !vehicles[p.vehicle].used) return;
        if (p.seat == 0) {
            if (b.type == BRAIN_DRIVER || b.type == BRAIN_FLEE || b.type == BRAIN_COMBAT || b.type == BRAIN_ARREST || b.type == BRAIN_GOTO) driveVehicleAI(p.vehicle, dt);
        }
        // armed occupants shoot at their combat target
        if (b.type == BRAIN_COMBAT && p.target_is_valid(*this) && p.weapon != WPN_FISTS && weaponInfo(p.weapon).clipSize > 0) {
            const Ped& t = peds[b.target];
            vec3 head = pedHeadPos(p);
            float dist = length(t.pos.toVec3() - p.pos.toVec3());
            float range = p.weapon == WPN_SNIPER ? 170.f : (p.weapon == WPN_RIFLE ? 70.f : 45.f);   // helicopter marksman
            if (p.fireTimer <= 0.f && p.reloadTimer <= 0.f && dist < range && (p.seat != 0 || vehicles[p.vehicle].sim.speed() < 12.f)) {
                vec3 aim = pedChestPos(t);
                vec3 d = normalize(aim - head);
                float burst = fmodf((float)time * 0.8f + p.uid * 0.37f, 2.f);
                if (burst < 1.0f && lineOfSight(dvec3(head), dvec3(aim), id, p.vehicle)) {
                    p.aiming = true;
                    fireWeapon(id, dvec3(head + d * 0.6f), d);
                }
            }
            if (p.clip[p.weapon] <= 0 && p.ammo[p.weapon] <= 0) p.ammo[p.weapon] = weaponInfo(p.weapon).clipSize * 3;
        }
        return;
    }
    if (p.state == PS_ENTERING || p.state == PS_EXITING) {
        movePed(p, vec2(0, 0), dt, false);
        return;
    }
    if (p.moveMode == 2 || p.moveMode == 3) return;   // vaulting/climbing handled by the character controller
    if (p.faction == FAC_POLICE && (((b.type == BRAIN_COMBAT || b.type == BRAIN_ARREST || b.type == BRAIN_GOTO) && b.target >= 0) || (b.type == BRAIN_GOTO && b.target == -2))) {
        aiPoliceBrain(id, dt);
        return;
    }
    switch (b.type) {
        case BRAIN_FOLLOW: {
            vec2 desired(0, 0);
            if (b.target < 0 || !peds[b.target].used) {
                b.type = BRAIN_NONE;
                break;
            }
            Ped& t = peds[b.target];
            vec3 pos = p.pos.toVec3();
            if (t.state == PS_INVEHICLE && t.vehicle >= 0 && p.state == PS_ONFOOT) {
                int seat = freeSeat(t.vehicle, false);
                if (seat > 0) {
                    vec3 vp = vehicles[t.vehicle].sim.body.pos.toVec3();
                    if (length(vp - pos) < 4.f) warpPedIntoVehicle(id, t.vehicle, seat);
                    else desired = normalize(vp.xy() - pos.xy()) * 5.f;
                }
            } else {
                vec2 to = t.pos.toVec3().xy() - pos.xy();
                float dist = length(to);
                if (dist > 2.5f) desired = to / dist * (dist > 8.f ? 5.5f : (dist > 4.f ? 3.5f : 1.6f));
            }
            faceTowards(p, desired, 8.f, dt);
            movePed(p, desired, dt, false);
            break;
        }
        case BRAIN_GOTO: {
            vec2 desired(0, 0);
            vec2 to = b.goal.toVec3().xy() - p.pos.toVec3().xy();
            float dist = length(to);
            if (dist > 0.6f) desired = to / dist * Min(b.speed, dist * 2.f + 0.5f);
            faceTowards(p, desired, 8.f, dt);
            movePed(p, desired, dt, false);
            break;
        }
        default:
            aiCivilianBrain(id, dt);
            break;
    }
}

bool Ped::target_is_valid(const GameWorld& g) const {
    return brain.target >= 0 && brain.target < (int)g.peds.size() && g.peds[brain.target].used && g.peds[brain.target].health > 0.f;
}

// ------------------------------------------------------------------------------------------------------------------
// Signal phases for gameplay / rendering (approachDir = travel direction of the approaching traffic)
bool GameWorld::signalGreen(int node, vec2 approachDir) const {
    if (!ai.ready || node < 0 || node >= (int)laneGraph.nodes.size()) return true;
    int a = laneGraph.approachForDir(node, approachDir);
    AI::SignalState s = laneGraph.movementSignal(node, a, AI::TK_STRAIGHT, time);
    return s == AI::SIG_GREEN || s == AI::SIG_ARROW || s == AI::SIG_NONE;
}

int GameWorld::signalLampState(int node, vec2 approachDir) const {
    if (!ai.ready || node < 0 || node >= (int)laneGraph.nodes.size()) return 2;
    int a = laneGraph.approachForDir(node, approachDir);
    AI::SignalState s = laneGraph.movementSignal(node, a, AI::TK_STRAIGHT, time);
    return s == AI::SIG_AMBER ? 1 : (s == AI::SIG_RED ? 0 : 2);
}

}  // namespace Game
