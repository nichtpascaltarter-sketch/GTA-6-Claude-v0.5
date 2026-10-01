// AI orchestration: builds the lane/sidewalk graphs, fills the perception proxies for the tick, runs population,
// police dispatch, ambient events, pedestrian brains and traffic drivers, and exposes the signal phases to gameplay and
// rendering. The engine-independent cores live in lanes.cpp / traffic_core.cpp / pednav.cpp (ai_core.h).
#include "gameworld.h"
#include <functional>

namespace Game {

namespace ai_detail {

float wrapAng(float a) {
    // remainder instead of repeated subtraction: a huge or infinite angle must not spin forever
    if (a >= -kPi && a <= kPi) return a;
    a = remainderf(a, kTwoPi);
    return a == a ? a : 0.f;
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

// Which end of a car (+1 front, -1 rear) to walk round, preferring `pref` unless another vehicle stands right there
// (parked cars can be bumper to bumper). Used by people walking round cars to a door or to the curb.
float aiCarEndToWalkRound(const GameWorld& g, int veh, float pref) {
    const Vehicle& v = g.vehicles[veh];
    const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
    vec2 cc = v.sim.body.pos.toVec3().xy();
    vec2 f = v.sim.forward().xy();
    f = length2(f) > 1e-6f ? normalize(f) : vec2(0, 1);
    thread_local std::vector<int> around;
    for (int k = 0; k < 2; k++) {
        float e = k == 0 ? pref : -pref;
        vec2 q = cc + f * (e * (spec.boxHalf.y + 0.8f));
        around.clear();
        g.vehiclesNear(q, 8.f, around);
        bool blocked = false;
        for (int o : around) {
            if (o == veh || blocked) continue;
            const Vehicle& ov = g.vehicles[o];
            const Vehicles::VehicleModel& os = g.vassets[ov.model].spec;
            vec2 of = ov.sim.forward().xy();
            of = length2(of) > 1e-6f ? normalize(of) : vec2(0, 1);
            vec2 d = q - ov.sim.body.pos.toVec3().xy();
            if (fabsf(dot(d, of)) < os.boxHalf.y + 0.4f && fabsf(dot(d, AI::rightOf(of))) < os.boxHalf.x + 0.4f) blocked = true;
        }
        if (!blocked) return e;
    }
    return pref;
}

namespace ai_walkround {

// Does the walk a -> b come within `grow` of a static collider's footprint (box, or a cylinder taken as its square)?
// The entry distance along the walk in tEnter. Starting inside the grown footprint (already against it) counts only
// when the walk heads in through the face the walker stands at.
bool crosses(const Phys::Collider& c, vec2 a, vec2 b, float grow, float& tEnter) {
    bool box = c.kind == Phys::COL_BOX;
    vec2 ax = box ? c.ax : vec2(1.f, 0.f), ay(-ax.y, ax.x);
    vec2 o = a - c.c.xy(), d = b - a;
    float len = length(d);
    if (len < 1e-4f) return false;
    d = d / len;
    float lp[2] = {dot(o, ax), dot(o, ay)}, ld[2] = {dot(d, ax), dot(d, ay)}, he[2] = {c.he.x, c.he.y};
    float t0 = 0.f, t1 = len;
    for (int k = 0; k < 2; k++) {
        float h = he[k] + grow;
        if (fabsf(ld[k]) < 1e-5f) {
            if (fabsf(lp[k]) > h) return false;
            continue;
        }
        float ta = (-h - lp[k]) / ld[k], tb = (h - lp[k]) / ld[k];
        if (ta > tb) std::swap(ta, tb);
        t0 = Max(t0, ta);
        t1 = Min(t1, tb);
        if (t0 > t1) return false;
    }
    if (fabsf(lp[0]) <= he[0] + grow && fabsf(lp[1]) <= he[1] + grow) {
        int f = fabsf(lp[0]) - he[0] > fabsf(lp[1]) - he[1] ? 0 : 1;
        if (lp[f] * ld[f] >= 0.f) return false;
    }
    tEnter = t0;
    return true;
}

}  // namespace ai_walkround

// A way round a static obstacle (a bench, a planter, a bin, a bollard, a kiosk) standing in the next few metres of a
// straight walk from pos towards goal: the corner of its footprint, a body's width and a little room out, that is in
// plain view and makes the shorter way round. False when nothing stands in the way. Building walls are left to the
// walk network.
bool aiStaticDetour(vec2 pos, float z, vec2 goal, vec2& via, float ahead) {
    if (!Phys::gCollision) return false;
    vec2 to = goal - pos;
    float dist = length(to);
    if (dist < 0.4f) return false;
    vec2 dir = to / dist;
    float look = Min(dist, ahead);
    thread_local std::vector<int> ids;
    Phys::gCollision->collidersNear(pos + dir * (look * 0.5f), look * 0.5f + 2.f, ids);
    const float body = 0.36f;   // (a hair over the ped radius: what the capsule catches on)
    int best = -1;
    float bestT = 1e9f;
    for (int ci : ids) {
        const Phys::Collider& c = Phys::gCollision->collider(ci);
        if (c.flags & 2) continue;
        float z0 = c.kind == Phys::COL_BOX ? c.c.z - c.he.z : c.c.z, z1 = c.kind == Phys::COL_BOX ? c.c.z + c.he.z : c.c.z + c.he.z;
        if (z1 < z + 0.3f || z0 > z + 1.7f) continue;   // (kerb-high: stepped over; overhead)
        vec2 cax = c.kind == Phys::COL_BOX ? c.ax : vec2(1.f, 0.f), og = goal - c.c.xy();
        if (fabsf(dot(og, cax)) < c.he.x + body && fabsf(dot(og, vec2(-cax.y, cax.x))) < c.he.y + body) continue;   // (the goal is at it: no way round helps)
        float t;
        if (ai_walkround::crosses(c, pos, pos + dir * look, body, t) && t < bestT) {
            bestT = t;
            best = ci;
        }
    }
    if (best < 0) return false;
    const Phys::Collider& c = Phys::gCollision->collider(best);
    vec2 ax = c.kind == Phys::COL_BOX ? c.ax : vec2(1.f, 0.f), ay(-ax.y, ax.x), cc = c.c.xy();
    const float room = 0.7f;
    float bestCost = 1e9f;
    for (int k = 0; k < 4; k++) {
        vec2 q = cc + ax * ((k & 1 ? 1.f : -1.f) * (c.he.x + room)) + ay * ((k & 2 ? 1.f : -1.f) * (c.he.y + room));
        float t;
        if (length(q - pos) < 0.4f || ai_walkround::crosses(c, pos, q, 0.15f, t)) continue;   // (here already / round the far side)
        vec3 push, nrm;
        if (Phys::gCollision->capsuleOverlap(vec3(q, z + 0.05f), 0.3f, 1.7f, push, nrm) && length(push) > 0.05f) continue;   // (a wall, the next bench)
        float cost = length(q - pos) + length(goal - q);
        if (cost < bestCost) {
            bestCost = cost;
            via = q;
        }
    }
    return bestCost < 1e8f;
}

// The point a walker going straight for `goal` (no walk-network path: a prisoner walked to the car, the officer at
// their arm, somebody making for a door) steers at: round a bench, a planter or a bin on the line (looked for a few
// times a second, at once when held up against something), a step aside when held up by somebody, else the goal.
// `want` is the speed asked for (0: standing, no hold-up to notice).
vec2 aiWalkRound(GameWorld& g, int id, vec2 goal, float want, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    vec2 pos = p.pos.toVec3().xy();
    float z = (float)p.pos.z;
    if (want > 0.3f && length(p.vel.xy()) < 0.15f) pa.stuckTimer += dt;
    else pa.stuckTimer = Max(0.f, pa.stuckTimer - dt);
    vec2 via;
    float ahead = Max(3.5f, want * 1.1f);   // (further ahead at a run)
    if (pa.detourT > 0.f && length(goal - pa.detourGoal) > 4.f) pa.detourT = 0.f;   // (a corner on the way somewhere else)
    pa.detourGoal = goal;
    if (pa.stuckTimer > 0.9f) {
        // held up: what is in the way of the walk being made (the corner being made for, or the goal)
        pa.stuckTimer = 0.f;
        vec2 aim = pa.detourT > 0.f ? pa.detour : goal;
        if (aiStaticDetour(pos, z, aim, via, ahead)) {
            pa.detour = via;
            pa.detourT = 4.f;
        } else {
            // nothing standing there: somebody is - a step to the side (the same side while it keeps happening, the
            // other one when a wall or a post is there)
            vec2 dir = normalize(aim - pos + vec2(1e-4f, 0.f));
            float side = (hash32(p.uid + (u32)(g.time * 0.2)) & 1u) ? 1.f : -1.f;
            vec3 push, nrm;
            vec2 q = pos + AI::rightOf(dir) * (side * 1.1f) + dir * 0.3f;
            if (Phys::gCollision && Phys::gCollision->capsuleOverlap(vec3(q, z + 0.05f), 0.3f, 1.7f, push, nrm) && length(push) > 0.05f)
                q = pos - AI::rightOf(dir) * (side * 1.1f) + dir * 0.3f;
            pa.detour = q;
            pa.detourT = 1.5f;
        }
    } else if (pa.detourT > 0.f) {
        pa.detourT -= dt;
        if (length(pa.detour - pos) < 0.4f) {
            // round that corner: on for the goal, or on to the next corner while the obstacle is still in the way
            if (aiStaticDetour(pos, z, goal, via, ahead)) {
                pa.detour = via;
                pa.detourT = 4.f;
            } else {
                pa.detourT = 0.f;
            }
        }
    } else if (want > 0.f && floorf((float)g.time * 2.5f + id * 0.37f) != floorf((float)(g.time - dt) * 2.5f + id * 0.37f) &&
               aiStaticDetour(pos, z, goal, via, ahead)) {
        pa.detour = via;
        pa.detourT = 4.f;
    }
    return pa.detourT > 0.f ? pa.detour : goal;
}

// A street door of a building near p: the middle of the street facade at ground level, on the side p is on, with a
// clear straight walk to p. Used by peds stepping out of and walking into buildings (population.cpp, pedai.cpp).
// Industrial blocks, garages, sheds and buildings hosting an enterable interior (interiors_game.cpp) are skipped.
bool aiBuildingDoorNear(const GameWorld& g, vec2 p, float r, u32 seed, vec3& out) {
    const World::BuildingSet* bs = g.buildings ? g.buildings : World::gBuildings;
    if (!bs || bs->buildings.empty()) return false;
    thread_local std::vector<int> nb;
    nb.clear();
    bs->buildingsNear(p, r + 40.f, nb);
    float bestScore = 1e9f;
    vec2 best;
    float bestZ = 0.f;
    for (int i : nb) {
        const World::Building& b = bs->buildings[i];
        if (b.interior >= 0) continue;
        u8 st = b.style;
        if (st == World::BS_WAREHOUSE || st == World::BS_FACTORY || st == World::BS_BARN || st == World::BS_GARAGE || st == World::BS_SHACK) continue;
        vec2 door = b.c + b.front * (b.hy + 0.45f);
        vec2 rp = p - door;
        float d = length(rp);
        if (d > r || d < 2.f || dot(rp, b.front) < 1.f) continue;   // on the street side, not right on top of it
        if (bs->pointInBuilding(door, 0.2f)) continue;              // podium or neighbour in front of the facade
        bool clear = true;
        for (int k = 1; k < 8 && clear; k++)
            if (bs->pointInBuilding((door + b.front * 0.6f) + (p - door - b.front * 0.6f) * (k / 8.f), 0.25f)) clear = false;
        if (!clear) continue;
        float score = d + hashToFloat(hash32(seed + (u32)i * 131u)) * 8.f;
        if (score < bestScore) {
            bestScore = score;
            best = door;
            bestZ = b.baseZ;
        }
    }
    if (bestScore > 1e8f) return false;
    out = vec3(best, g.groundHeight(best.x, best.y, bestZ + 2.f));
    return true;
}

// A door to dash in through, running from trouble at `threat` (pedai.cpp BRAIN_FLEE): the nearest within r on the far
// side of p from it - never back toward it or past it - with a clear run to it (as aiBuildingDoorNear)
bool aiBuildingDoorAway(const GameWorld& g, vec2 p, vec2 threat, float r, vec3& out) {
    const World::BuildingSet* bs = g.buildings ? g.buildings : World::gBuildings;
    if (!bs || bs->buildings.empty()) return false;
    thread_local std::vector<int> nb;
    nb.clear();
    bs->buildingsNear(p, r + 40.f, nb);
    vec2 aw = p - threat;
    float tdist = length(aw);
    aw = tdist > 1e-3f ? aw / tdist : vec2(0.f, 1.f);
    float best = 1e9f;
    vec2 at;
    float atZ = 0.f;
    for (int i : nb) {
        const World::Building& b = bs->buildings[i];
        if (b.interior >= 0) continue;
        u8 st = b.style;
        if (st == World::BS_WAREHOUSE || st == World::BS_FACTORY || st == World::BS_BARN || st == World::BS_GARAGE || st == World::BS_SHACK) continue;
        vec2 door = b.c + b.front * (b.hy + 0.45f);
        vec2 run = door - p;
        float d = length(run);
        if (d > r || d < 1.5f || dot(p - door, b.front) < 0.5f) continue;   // (from the street side)
        if (dot(run, aw) < d * 0.25f || length(door - threat) < tdist + 1.5f) continue;   // (away from it, not past it)
        if (bs->pointInBuilding(door, 0.2f)) continue;
        bool clear = true;
        for (int k = 1; k < 8 && clear; k++)
            if (bs->pointInBuilding((door + b.front * 0.6f) + (p - door - b.front * 0.6f) * (k / 8.f), 0.25f)) clear = false;
        if (!clear || d >= best) continue;
        best = d;
        at = door;
        atZ = b.baseZ;
    }
    if (best > 1e8f) return false;
    out = vec3(at, g.groundHeight(at.x, at.y, atZ + 2.f));
    return true;
}

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
        if (v.sirenOn && !(v.sirenSilent && b.speed < 2.f)) fl |= AI::BF_SIREN;   // (lights only, standing at a scene: no right of way)
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

// the ambient events drivers going past slow down for (events.cpp)
namespace ev_sights {
void collect(const GameWorld& g, std::vector<vec3>& out);
}

// what drivers going past slow down to look at (traffic.cpp rubbernecking), round the player: a wreck or a car burning,
// a police car / ambulance / fire truck stopped with its lights going, somebody lying in the street or down hurt, a
// fender bender, a breakdown, a fight, a traffic stop (events.cpp)
namespace ai_sights {
void collect(GameWorld& g) {
    std::vector<vec3>& out = g.ai.sights;
    out.clear();
    const Ped* pl = g.playerPed();
    if (!pl) return;
    vec2 c = pl->pos.toVec3().xy();
    for (int i = 0; i < (int)g.vehicles.size() && out.size() < 24; i++) {
        const Vehicle& v = g.vehicles[i];
        if (!v.used) continue;
        vec3 p = v.sim.body.pos.toVec3();
        if (length(p.xy() - c) > 320.f) continue;
        u8 role = i < (int)g.ai.veh.size() && g.ai.veh[i].uid == v.uid ? g.ai.veh[i].role : (u8)VR_TRAFFIC;
        bool service = v.faction == FAC_POLICE || role == VR_AMBULANCE || role == VR_FIRETRUCK;
        bool lightsStopped = service && v.sirenOn && v.sim.speed() < 1.f && v.seats[0] < 0;   // (crew out at a scene)
        if (v.sim.wrecked || v.exploded || v.fireTimer > 0.f || lightsStopped) out.push_back(p);
    }
    for (int i = 0; i < (int)g.peds.size() && out.size() < 24; i++) {
        const Ped& q = g.peds[i];
        if (!q.used || q.isPlayer) continue;
        bool down = q.state == PS_DEAD || (q.state == PS_ONFOOT && i < (int)g.ai.ped.size() && g.ai.ped[i].uid == q.uid && g.ai.ped[i].activity == ACT_HURT);
        if (!down) continue;
        vec3 p = q.pos.toVec3();
        if (length(p.xy() - c) < 320.f) out.push_back(p);
    }
    ev_sights::collect(g, out);
    if (out.size() > 32) out.resize(32);
}
}  // namespace ai_sights

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updateAI(float dt) {
    double t0 = TimeSeconds();
    if (!ai.ready) initAI();
    if (!ai.ready) return;
    ai.barkGlobal = Max(0.f, ai.barkGlobal - dt);
    for (auto& pa : ai.ped) pa.barkCooldown = Max(0.f, pa.barkCooldown - dt);
    {   // (the rain coming on: pedai.cpp has the ones without an umbrella dash for it)
        float r = env ? env->rain : 0.f;
        if (r > 0.45f && ai.rainPrev <= 0.45f) ai.rainStartT = time;
        ai.rainPrev = r;
    }
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
    aiStreetMeets(dt);
    ai.sightsT -= dt;
    if (ai.sightsT <= 0.f) {
        ai.sightsT = 0.5f;
        ai_sights::collect(*this);
    }
    double t4 = TimeSeconds();
    double tTraffic = 0.0;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.isPlayer || p.brain.type == BRAIN_NONE) continue;
        if (p.state == PS_DEAD || p.state == PS_RAGDOLL || p.state == PS_GETUP) {
            // (knocked down: the brain sees it once they are back on their feet - pedai.cpp ACT_HURT)
            if (p.state != PS_DEAD && i < (int)ai.ped.size() && ai.ped[i].uid == p.uid) ai.ped[i].knockedDown = true;
            continue;
        }
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
    // crash telemetry: hard impacts of AI-driven cars (one per crash), those involving the player counted apart
    int pv = playerVehicle();
    for (int i = 0; i < (int)vehicles.size() && i < (int)ai.veh.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.used || ai.veh[i].uid != v.uid) continue;
        VehAI& va = ai.veh[i];
        va.impactCd = Max(0.f, va.impactCd - dt);
        // a parked car's alarm (set off by a knock) wails for half a minute or so, then gives up
        if (v.alarm && v.parked && v.seats[0] < 0) {
            va.alarmT += dt;
            if (va.alarmT > 25.f + hashToFloat(hash32(v.uid * 5u)) * 20.f) {
                v.alarm = false;
                va.alarmT = 0.f;
            }
        } else {
            va.alarmT = 0.f;
        }
        // this frame's knock: the physics keeps only the largest contact impulse of its last substep (one in an earlier
        // substep is cleared again), so also judge it from the change in horizontal velocity since the last frame -
        // braking never takes more than a few tenths of a m/s off in one
        vec2 hv = v.sim.body.vel.xy();
        const AI::Driver* kd = traffic.get(i);
        bool kinematic = kd && kd->dummy;   // (a dummy moves on rails: its velocity jumps when it turns physical again)
        float knockEst = va.lastVelOk && !kinematic && !v.sim.sleeping ? length(hv - va.lastVel) * v.sim.body.mass : 0.f;
        if (knockEst < v.sim.body.mass * 1.f) knockEst = 0.f;
        va.lastVel = hv;
        va.lastVelOk = !kinematic;
        if (i == pv || !traffic.get(i) || va.impactCd > 0.f || (v.sim.impactImpulse < 1800.f && knockEst < 1800.f)) continue;
        va.impactCd = 2.f;
        bool withPlayer = pv >= 0 && length(rel(v.sim.body.pos, vehicles[pv].sim.body.pos)) < 9.f;
        if (v.sim.impactImpulse >= 3000.f) (withPlayer ? st.impactsWithPlayer : st.hardImpacts)++;
        // two ordinary cars knocked into each other (not the player's doing): both stop and the drivers have words
        Ped* pl = playerPed();
        float plD = pl ? length(rel(v.sim.body.pos, pl->pos)) : 1e9f;
        if (withPlayer || v.sim.impactCollider >= 0 || va.role != VR_TRAFFIC || plD < 18.f || plD > 170.f || v.sim.speed() > 16.f) continue;
        int drvA = v.seats[0];
        if (drvA < 0 || peds[drvA].brain.type != BRAIN_DRIVER) continue;
        const Vehicles::VehicleModel& sa = vassets[v.model].spec;
        vec2 ca = v.sim.body.pos.toVec3().xy(), fa = v.sim.forward().xy();
        fa = length2(fa) > 1e-6f ? normalize(fa) : vec2(0, 1);
        std::vector<int> around;
        vehiclesNear(ca, 9.f, around);
        for (int j : around) {
            if (j == i || j == pv || j >= (int)ai.veh.size() || ai.veh[j].uid != vehicles[j].uid) continue;
            const Vehicle& o = vehicles[j];
            if (ai.veh[j].role != VR_TRAFFIC || !traffic.get(j) || o.seats[0] < 0 || peds[o.seats[0]].brain.type != BRAIN_DRIVER || o.sim.speed() > 16.f) continue;
            const Vehicles::VehicleModel& sb = vassets[o.model].spec;
            vec2 cb = o.sim.body.pos.toVec3().xy(), fb = o.sim.forward().xy();
            fb = length2(fb) > 1e-6f ? normalize(fb) : vec2(0, 1);
            // the two bodies (a hand's width of slack) touch
            vec2 axes[4] = {fa, AI::rightOf(fa), fb, AI::rightOf(fb)};
            bool touch = true;
            for (const vec2& ax : axes) {
                float ea = sa.boxHalf.y * fabsf(dot(fa, ax)) + sa.boxHalf.x * fabsf(dot(AI::rightOf(fa), ax)) + 0.15f;
                float eb = sb.boxHalf.y * fabsf(dot(fb, ax)) + sb.boxHalf.x * fabsf(dot(AI::rightOf(fb), ax)) + 0.15f;
                if (fabsf(dot(cb - ca, ax)) > ea + eb) touch = false;
            }
            if (!touch) continue;
            if (ai.forceBender || hashToFloat(hash32(v.uid * 7u + o.uid * 13u + (u32)(time * 2.0))) < 0.6f) aiFenderBender(i, j);
            break;
        }
    }
}

std::string GameWorld::aiTrafficHealthText() const {
    int stuck = 0, blocked = 0, waiting = 0, rolled = 0, wrecked = 0, holding = 0;
    std::string rolledTxt;
    float worst = 0.f;
    int worstId = -1;
    for (int i = 0; i < (int)vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.used) continue;
        if (v.sim.wrecked || v.exploded) wrecked++;
        vec3 up = rotate(v.sim.body.rot, vec3(0, 0, 1));
        if (up.z < 0.3f && !isAircraft(i) && !isBoat(i)) {
            if (rolled == 0) {
                vec3 rp = v.sim.body.pos.toVec3();
                rolledTxt = StrFormat(" (car %d %s at %.0f %.0f, %s)", i, vassets[v.model].spec.name.c_str(), rp.x, rp.y,
                                      v.seats[0] >= 0 ? "driven" : (v.parked ? "parked" : "empty"));
            }
            rolled++;
        }
        if (i >= (int)traffic.drivers.size() || !traffic.drivers[i].active) continue;
        const AI::Driver& d = traffic.drivers[i];
        if (d.dummy) continue;
        if (d.mode == AI::DM_HOLD || d.mode == AI::DM_PULLOVER) {
            holding++;
            continue;
        }
        stuck += d.stuckTime > 30.f;
        blocked += d.blockedTime > 45.f;
        waiting += d.waitTime > 90.f;
        float w = Max(Max(d.stuckTime, d.blockedTime), d.waitTime);
        if (w > worst) {
            worst = w;
            worstId = i;
        }
    }
    const AI::TrafficStats& ts = traffic.stats;
    std::string worstTxt = "-";
    if (worstId >= 0) {
        const AI::Driver& d = traffic.drivers[worstId];
        vec3 p = vehicles[worstId].sim.body.pos.toVec3();
        worstTxt = StrFormat("car %d at %.0f %.0f %s %d u %.1f mode %d stuck %.0f blocked %.0f wait %.0f", worstId, p.x, p.y,
                             d.path < (int)laneGraph.lanes.size() ? "lane" : "conn", d.path, d.u, (int)d.mode, d.stuckTime, d.blockedTime, d.waitTime);
        // what it is waiting for: the obstacle ahead (and what that is doing) or the stop point
        if (d.obstBody >= 0 && d.obstBody < (int)traffic.bodies.size() && d.obstDist < 40.f) {
            const AI::Body& ob = traffic.bodies[d.obstBody];
            std::string what;
            if (ob.kind == AI::BK_PED) {
                what = (ob.flags & AI::BF_PLAYER) ? "player" : StrFormat("ped %d%s", ob.host, (ob.flags & AI::BF_CROSSING) ? " crossing" : "");
            } else if (ob.host >= 0 && ob.host < (int)vehicles.size() && vehicles[ob.host].used) {
                const Vehicle& o = vehicles[ob.host];
                const AI::Driver* od = ob.host < (int)traffic.drivers.size() && traffic.drivers[ob.host].active ? &traffic.drivers[ob.host] : nullptr;
                what = StrFormat("car %d %s%s%s", ob.host, vassets[o.model].spec.name.c_str(), (ob.flags & AI::BF_PLAYER) ? " player" : "",
                                 (ob.flags & AI::BF_PARKED) ? " parked" : "");
                if (od) what += StrFormat(" (mode %d stuck %.0f blocked %.0f wait %.0f)", (int)od->mode, od->stuckTime, od->blockedTime, od->waitTime);
                else if (o.seats[0] < 0) what += " empty";
                if (ob.host < (int)ai.veh.size()) what += StrFormat(" role %d", (int)ai.veh[ob.host].role);
            } else {
                what = "body";
            }
            worstTxt += StrFormat(", obstacle %.1f m: %s v %.1f", d.obstDist, what.c_str(), ob.speed);
        } else if (d.stopDist < 40.f) {
            worstTxt += StrFormat(", stop point %.1f m (gate conn %d node %d)", d.stopDist, d.gateConn, d.gateNode);
        }
    }
    return StrFormat("traffic health: stuck>30s %d blocked>45s %d wait>90s %d holding %d rolled %d%s wrecked %d unhung %d | impacts %d (with player %d) | "
                     "core red %ld stopsign %ld stuckEv %ld recov %ld reloc %ld deadlockBreaks %ld kturns %ld | worst: %s",
                     stuck, blocked, waiting, holding, rolled, rolledTxt.c_str(), wrecked, ai.stats.unhung, ai.stats.hardImpacts, ai.stats.impactsWithPlayer, ts.redViolations,
                     ts.stopSignViolations, ts.stuckEvents, ts.recoveries, ts.relocalizations, ts.deadlockBreaks, ts.kTurns, worstTxt.c_str());
}

std::string GameWorld::aiDebugText() const {
    const AIFrameStats& s = ai.stats;
    return StrFormat("AI %.2f ms (avg %.2f, max %.2f): traffic %.2f peds %.2f pop %.2f police %.2f events %.2f | peds %d cars %d (AI %d, dummy %d)",
                     s.msAI, s.avgMs, s.maxMs, s.msTraffic, s.msPeds, s.msPop, s.msPolice, s.msEvents, s.peds, s.cars, s.managed, s.dummies);
}

std::string GameWorld::aiCensusText(float radius) const {
    const Ped* pl = player >= 0 && player < (int)peds.size() ? &peds[player] : nullptr;
    if (!pl) return "census: no player";
    vec2 c = pl->pos.toVec3().xy();
    // on foot: what the crowd is doing
    int total = 0, walk = 0, group = 0, jog = 0, wPhone = 0, wSmoke = 0, wTalk = 0, sit = 0, talk = 0, phone = 0, dance = 0, smoke = 0, lean = 0,
        sun = 0, queue = 0, watch = 0, busStop = 0, taxi = 0, event = 0, venue = 0, vGuard = 0, vPace = 0, vTravel = 0, vOut = 0, vBoard = 0, vGreet = 0, meet = 0, browse = 0, hurt = 0, aid = 0, cuffed = 0, escorts = 0, witness = 0, taking = 0, stopped = 0, holding = 0, onBreak = 0, ticketing = 0, rushing = 0, wStill = 0;
    int tourist = 0, business = 0, beach = 0, night = 0, gang = 0, worker = 0;
    int flee = 0, cower = 0, film = 0, inspect = 0, call = 0, hands = 0, rage = 0, fight = 0;
    int copFoot = 0, cover = 0, flank = 0, arrest = 0, search = 0, engage = 0, approach = 0, inWater = 0;
    for (int i = 0; i < (int)peds.size(); i++) {
        const Ped& p = peds[i];
        if (!p.used || p.isPlayer || p.health <= 0.f || (p.state != PS_ONFOOT && p.state != PS_SWIM)) continue;
        if (length(p.pos.toVec3().xy() - c) > radius) continue;
        total++;
        float wz = map ? map->waterAt(p.pos.toVec3().x, p.pos.toVec3().y) : World::kNoWater;
        inWater += p.state == PS_SWIM || wz > (float)p.pos.z + 0.3f;
        const PedAI* pa = i < (int)ai.ped.size() && ai.ped[i].uid == p.uid ? &ai.ped[i] : nullptr;
        if (p.faction == FAC_POLICE) {
            copFoot++;
            if (pa) {
                cover += pa->tactic == FT_COVER;
                flank += pa->tactic == FT_FLANK;
                arrest += pa->tactic == FT_ARREST;
                search += pa->tactic == FT_SEARCH;
                engage += pa->tactic == FT_ENGAGE;
                approach += pa->tactic == FT_APPROACH;
            }
            escorts += p.brain.type == BRAIN_GOTO && p.brain.target == -3;
            taking += p.brain.type == BRAIN_GOTO && p.brain.target == -4;
            onBreak += pa && pa->activity == ACT_COP_BREAK;
            ticketing += p.brain.type == BRAIN_GOTO && p.brain.target == -6;
            continue;
        }
        if (p.brain.type == BRAIN_FLEE) flee++;
        else if (p.brain.type == BRAIN_COWER) cower++;
        else if (p.brain.type == BRAIN_COMBAT) fight++;
        if (!pa) continue;
        tourist += pa->role == PR_TOURIST;
        business += pa->role == PR_BUSINESS;
        beach += pa->role == PR_BEACH;
        night += pa->role == PR_NIGHTLIFE;
        gang += pa->role == PR_GANG;
        worker += pa->role == PR_WORKER;
        if (p.brain.type == BRAIN_FLEE || p.brain.type == BRAIN_COWER || p.brain.type == BRAIN_COMBAT) continue;
        switch (pa->activity) {
            case ACT_WALK:
            case ACT_GROUP:
                if (pa->leader >= 0 || pa->activity == ACT_GROUP) {   // walking with company
                    group++;
                    wTalk += p.animIn.stance == 7;   // (chatting on the way: pedai.cpp sets the talk stance)
                    break;
                }
                walk++;
                wPhone += pa->walkStance == 8;
                wSmoke += pa->walkStance == 10;
                // (on a sidewalk, meaning to walk, standing still: a hold-up for a moment - a lasting crowd of these is a
                //  sign of people stuck)
                wStill += pa->navOk && pa->walk.state == AI::WS_WALK && length(p.vel.xy()) < 0.15f;
                break;
            case ACT_JOG: jog++; break;
            case ACT_SCENARIO:
                browse += pa->browsing;
                sit += pa->stance == 6;
                talk += pa->stance == 7;
                phone += pa->stance == 8;
                dance += pa->stance == 9;
                smoke += pa->stance == 10;
                lean += pa->stance == 11;
                sun += pa->stance == 12 || pa->stance == 21 || pa->stance == 22;
                break;
            case ACT_VENUE:
                venue++;
                vGuard += pa->venueMode == VM_GUARD;
                vPace += pa->venueMode == VM_PACE;
                vTravel += pa->venueMode == VM_TRAVEL_OUT || pa->venueMode == VM_FAREWELL || pa->venueMode == VM_SEEOFF || pa->venueMode == VM_MEET;
                vOut += pa->venueMode == VM_WATCH || pa->venueMode == VM_SPOTTER || pa->venueMode == VM_SIT || pa->venueMode == VM_WORK;
                queue += pa->venueMode == VM_QUEUE;
                vBoard += pa->venueMode == VM_BOARD;
                vGreet += pa->greetT > 0.f;
                break;
            case ACT_ENTER_VEH: vTravel += pa->targetVeh < 0; break;   // (heading in through a door)
            case ACT_QUEUE: queue++; break;
            case ACT_WATCH: watch++; break;
            case ACT_WAIT_BUS: busStop++; break;
            case ACT_HAIL_TAXI: taxi++; break;
            case ACT_EVENT: event++; break;
            case ACT_FILM: film++; break;
            case ACT_INSPECT: inspect++; break;
            case ACT_CALL_POLICE: call++; break;
            case ACT_HANDS_UP: hands++; break;
            case ACT_ROADRAGE: rage++; break;
            case ACT_MEET: meet++; break;
            case ACT_HURT: hurt++; break;
            case ACT_AID: aid++; break;
            case ACT_CUFFED: cuffed++; break;
            case ACT_STATEMENT: witness++; break;
            case ACT_STOPPED: stopped++; break;
            case ACT_TICKET_RUSH: rushing++; break;
            default: break;
        }
        holding += pa && pa->leader >= 0 && pa->handWith >= 0 && time - pa->handT < 0.3;   // (couples hand in hand, counted once)
    }
    // vehicles: traffic, police units by kind
    int traffic = 0, parked = 0, copCars = 0, heli = 0, boats = 0, blocks = 0, swat = 0, ems = 0, honking = 0;
    for (int i = 0; i < (int)vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.used || length(v.sim.body.pos.toVec3().xy() - c) > radius * 2.f) continue;
        const VehAI* va = i < (int)ai.veh.size() && ai.veh[i].uid == v.uid ? &ai.veh[i] : nullptr;
        u8 role = va ? va->role : (u8)VR_TRAFFIC;
        if (role == VR_POLICE_HELI) heli++;
        else if (role == VR_POLICE_BOAT) boats++;
        else if (role == VR_ROADBLOCK) blocks++;
        else if (role == VR_SWAT) swat++;
        else if (role == VR_AMBULANCE || role == VR_FIRETRUCK) ems++;
        else if (v.faction == FAC_POLICE) copCars++;
        else if (v.parked || v.seats[0] < 0) parked++;
        else traffic++;
        honking += v.hornOn;
    }
    const AIFrameStats& s = ai.stats;
    return StrFormat("census r%.0f: %d on foot (in the water %d) | walk %d (phone %d smoke %d still %d) group %d (talking %d) jog %d | sit %d talk %d phone %d dance %d smoke %d "
                     "lean %d sun %d queue %d watch %d bus %d taxi %d event %d meet %d (so far %d) window %d hurt %d (helped by %d) cuffed %d (escorts %d) statement %d (officers %d) stopped %d hand in hand %d cops on a break %d ticket %d (owner %d) | venue %d (guard %d pace %d outlook %d boarding %d greeting %d) travelers %d | tourist %d business %d beach %d night %d gang %d worker %d | "
                     "react flee %d cower %d film %d inspect %d call %d hands %d rage %d fight %d | cops on foot %d (approach %d cover %d flank %d "
                     "arrest %d search %d engage %d) | cars %d parked %d police %d swat %d heli %d boat %d roadblock %d ems %d horn %d | "
                     "totals panic %d film %d pit %d box %d rb %d spikes %d tackle %d heli %d units %d rage %d events %d arrests %d custody %d transports %d statements %d shelters %d stops %d (warrants %d runs %d) breaks %d tickets %d (owners %d) greetings %d chats %d buttons %d lost %d laces %d rain shelters %d asked okay %d cop aid %d crash scenes %d bus runs %d (made %d missed %d) change %d (passers-by %d) gawks %d near misses %d jaywalk calls %d crowded %d (left %d) followed %d (sharp %d) cut offs %d depart %d arrive %d",
                     radius, total, inWater, walk, wPhone, wSmoke, wStill, group, wTalk, jog, sit, talk, phone, dance, smoke, lean, sun, queue, watch, busStop, taxi, event, meet, ai.meetsStarted, browse, hurt, aid, cuffed, escorts, witness, taking, stopped, holding, onBreak, ticketing, rushing,
                     venue, vGuard, vPace, vOut, vBoard, vGreet, vTravel, tourist, business, beach, night, gang, worker, flee, cower, film, inspect, call, hands, rage, fight, copFoot, approach, cover, flank,
                     arrest, search, engage, traffic, parked, copCars, swat, heli, boats, blocks, ems, honking, s.panicSpread, s.filming, s.pitTries,
                     s.boxing, s.roadblocks, s.spikeHits, s.tackles, s.heliUnits, s.unitsSent, s.roadRage, s.events, s.arrests, s.custody, s.transports, s.statements, s.shelters, s.stops, s.stopArrests, s.stopRuns, s.copBreaks, s.tickets, s.ticketRushes, s.greetings, s.chats, s.buttons, s.lost, s.laces, s.rainShelters, s.askOkay, s.copAid, s.crashScenes, s.busRuns, s.busCaught, s.busMissed, s.panhandled, s.passerChange, s.gawks, s.nearMisses, s.jaywalkCalls, s.crowded, s.crowdedLeft, s.followed, s.followedSharp, s.cutOffs, s.departures, s.arrivals);
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
        // (not at someone standing there with their hands up: police hold their fire at a surrender)
        bool heldFire = b.target == player && ai.surrender && p.faction == FAC_POLICE;
        if (b.type == BRAIN_COMBAT && p.target_is_valid(*this) && p.weapon != WPN_FISTS && weaponInfo(p.weapon).clipSize > 0 && !heldFire) {
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
    // (police on foot: after a suspect, -2 back to the car, -3 walking a prisoner, -4 taking a statement, -5 a sidewalk stop,
    //  -6 writing up a parked car)
    if (p.faction == FAC_POLICE && (((b.type == BRAIN_COMBAT || b.type == BRAIN_ARREST || b.type == BRAIN_GOTO) && b.target >= 0) ||
                                    (b.type == BRAIN_GOTO && b.target <= -2 && b.target >= -6))) {
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
            vec2 goal = b.goal.toVec3().xy();
            float dist = length(goal - p.pos.toVec3().xy());
            if (dist > 0.6f) {
                // (round a bench or a planter on the line, a step aside when somebody holds them up)
                float spd = Min(b.speed, dist * 2.f + 0.5f);
                vec2 to = aiWalkRound(*this, id, goal, spd, dt) - p.pos.toVec3().xy();
                float ls = length(to);
                if (ls > 1e-3f) desired = to / ls * spd;
            }
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
