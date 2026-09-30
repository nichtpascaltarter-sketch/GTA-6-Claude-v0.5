// Ambient random events around the player: muggings, purse snatching, fender benders with arguing drivers, street
// racers, the police chasing a criminal, gang shootouts, drunks, street musicians with an audience, tourists taking
// photos and stranded drivers waving for help. Events spawn out of view at a moderate rate (at most two at a time,
// the same kind not twice in a row) and some offer small choices and rewards: stop a mugger or a purse thief and let
// the victim recover the wallet (or pocket it yourself), tip a busker, take the tourists' photo, fix a broken-down car.
#include "gameworld.h"

namespace Game {

namespace events_detail {

enum EvType : u8 {
    EV_MUGGING = 0, EV_PURSE, EV_CRASH, EV_RACERS, EV_CHASE, EV_SHOOTOUT, EV_DRUNK, EV_MUSICIAN, EV_TOURISTS, EV_BREAKDOWN,
    EV_TRAFFIC_STOP,   // a cruiser has pulled a car over: the officer walks up to the window, has words, both drive off
    EV_COUNT
};

// shared stage ids (per event type the meaning is documented at its update function)
enum EvStage : u8 { ST_A = 0, ST_B, ST_C, ST_D, ST_E };

struct Ref {
    int id = -1;
    u32 uid = 0;
};

struct AmbientEvent {
    bool active = false;
    u8 type = 0;
    u8 stage = 0;
    float t = 0.f;           // time in the current stage
    float age = 0.f;
    vec3 pos;                // anchor
    vec2 dir = vec2(0, 1);   // street direction at the anchor
    Ref ped[8];
    int np = 0;
    Ref veh[3];
    int nv = 0;
    int amount = 0;          // money carried off / offered
    int pickup = -1;         // dropped wallet / purse
    dvec3 pickupPos;
    float hold = 0.f;        // player interaction timer (tip, photo, repair)
    bool done = false;       // interaction completed (one reward per event)
    bool asked = false;
    float barkT = 0.f, fxT = 0.f;
    int flag = 0;
    Audio::EmitterHandle music = 0;
};

struct EventSystem {
    AmbientEvent ev[3];
    float timer = 25.f;
    u32 counter = 1;
    double lastOfType[EV_COUNT];
    EventSystem() {
        for (double& d : lastOfType) d = -1e9;
    }
};
EventSystem gEv;

inline float yawTowards(vec2 from, vec2 to) {
    vec2 d = to - from;
    return atan2f(-d.x, d.y);
}

int livePed(const GameWorld& g, const Ref& r) {
    if (r.id < 0 || r.id >= (int)g.peds.size()) return -1;
    const Ped& p = g.peds[r.id];
    return p.used && p.uid == r.uid ? r.id : -1;
}

int liveVeh(const GameWorld& g, const Ref& r) {
    if (r.id < 0 || r.id >= (int)g.vehicles.size()) return -1;
    const Vehicle& v = g.vehicles[r.id];
    return v.used && v.uid == r.uid ? r.id : -1;
}

// (a ped being taken down by the player counts as down: melee.cpp drives it)
bool isDown(const Ped& p) { return p.health <= 0.f || p.state == PS_DEAD || p.state == PS_RAGDOLL || p.state == PS_GETUP || p.takedownT >= 0.f; }

// alive, on its feet and still doing what the event asked (not fleeing / cowering because of something else)
bool calmActor(GameWorld& g, int id) {
    if (id < 0) return false;
    const Ped& p = g.peds[id];
    if (isDown(p) || p.state != PS_ONFOOT) return false;
    return p.brain.type == BRAIN_WANDER && g.pedAI(id).activity == ACT_EVENT;
}

Ref refPed(const GameWorld& g, int id) {
    Ref r;
    if (id >= 0) {
        r.id = id;
        r.uid = g.peds[id].uid;
    }
    return r;
}

Ref refVeh(const GameWorld& g, int id) {
    Ref r;
    if (id >= 0) {
        r.id = id;
        r.uid = g.vehicles[id].uid;
    }
    return r;
}

int gangChar(GameWorld& g, u32 seed, Faction f) {
    const std::vector<int>& v = g.charsByRole[2];
    if (v.empty()) return g.randomCivilianChar(seed, 0);
    int half = (int)v.size() / 2;
    if (half == 0) return v[seed % v.size()];
    return f == FAC_GANG_SAINTS ? v[half + (int)(seed % (u32)(v.size() - half))] : v[seed % (u32)half];
}

int spawnActor(GameWorld& g, vec3 pos, float yaw, u32 seed, int charRole, Faction f, u8 role, int evId) {
    int ci = (f == FAC_GANG_CUERVOS || f == FAC_GANG_SAINTS) ? gangChar(g, seed, f) : g.randomCivilianChar(seed, charRole);
    if (ci < 0) return -1;
    int id = g.spawnPed(ci, dvec3(pos), yaw, f);
    if (id < 0) return -1;
    Ped& p = g.peds[id];
    p.brain.type = BRAIN_WANDER;
    p.brain.edge = -1;
    PedAI& pa = g.pedAI(id);
    pa.role = role;
    pa.eventId = evId;
    pa.activity = ACT_WALK;
    pa.navOk = false;
    return id;
}

void setActor(GameWorld& g, int id, int evId, vec2 anchor, float yaw, int stance, int clip) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    p.brain.type = BRAIN_WANDER;
    pa.activity = ACT_EVENT;
    pa.eventId = evId;
    pa.anchor = anchor;
    pa.anchorYaw = yaw;
    pa.stance = stance;
    pa.clip = clip;
    pa.actTimer = 1e5f;
    pa.homeVeh = -1;
    pa.aimAt = -1;
    pa.leader = -1;
}

void freeActor(GameWorld& g, int id) {
    if (id < 0) return;
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    if (pa.activity == ACT_EVENT || pa.activity == ACT_WATCH) {
        pa.activity = ACT_WALK;
        pa.clip = -1;
        pa.stance = 0;
        pa.actTimer = 10.f + hashToFloat(hash32(p.uid * 13u)) * 15.f;
    }
    pa.aimAt = -1;
    pa.eventId = -1;
    pa.navOk = false;
    if (p.brain.type == BRAIN_GOTO) {
        p.brain.type = BRAIN_WANDER;
        p.brain.edge = -1;
    }
}

int dropMoney(GameWorld& g, dvec3 pos, int amount) {
    Pickup pk;
    pk.used = true;
    pk.type = PICK_MONEY;
    pk.amount = amount;
    pk.pos = pos + dvec3(0.3, 0.2, 0.05);
    pk.life = 90.f;
    for (int i = 0; i < (int)g.pickups.size(); i++)
        if (!g.pickups[i].used) {
            g.pickups[i] = pk;
            return i;
        }
    g.pickups.push_back(pk);
    return (int)g.pickups.size() - 1;
}

bool pickupPresent(const GameWorld& g, int idx, dvec3 pos) {
    if (idx < 0 || idx >= (int)g.pickups.size()) return false;
    const Pickup& pk = g.pickups[idx];
    return pk.used && pk.type == PICK_MONEY && length(rel(pk.pos, pos)) < 0.5f;
}

void payReward(GameWorld& g, int amount, const char* why) {
    g.pinfo.money += amount;
    g.notify(why, StrFormat("+$%d", amount));
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_PICKUP_CASH, 0.7f);
#endif
}

void addCrimeIncident(GameWorld& g, dvec3 pos, int perp) {
    for (Incident& inc : g.ai.incidents)
        if (inc.active && inc.kind == 2 && inc.perp == perp) return;
    Incident inc;
    inc.pos = pos;
    inc.kind = 2;
    inc.time = (float)g.time;
    inc.perp = perp;
    inc.perpUid = perp >= 0 ? g.peds[perp].uid : 0;
    inc.active = true;
    for (Incident& o : g.ai.incidents)
        if (!o.active) {
            o = inc;
            return;
        }
    g.ai.incidents.push_back(inc);
}

// the player aims at `id` (within `range`)
bool playerAimsAt(GameWorld& g, int id, float range) {
    Ped* pl = g.playerPed();
    if (!pl || !pl->aiming || pl->weapon == WPN_FISTS || id < 0) return false;
    vec3 to = g.pedChestPos(g.peds[id]) - g.pedHeadPos(*pl);
    float along = dot(to, pl->aimDir);
    return along > 0.f && along < range && length(to - pl->aimDir * along) < 1.3f;
}

bool hurtByPlayer(const GameWorld& g, int id) {
    const Ped& p = g.peds[id];
    return p.lastAttacker == g.player && g.time - p.lastDamageTime < 1.5;
}

// ---- spawn locations
struct WalkSpot {
    int link = -1;
    float x = 0.f;
    vec3 pos;       // sidewalk center
    vec2 t;         // walking direction (link a -> b)
    vec2 outward;   // from the street toward the buildings
    float halfWidth = 1.f;
};

bool sidewalkSpot(const GameWorld& g, vec2 p, float maxDist, WalkSpot& out) {
    float x = 0.f, lat = 0.f;
    int link = g.laneGraph.nearestWalk(p, maxDist, &x, &lat);
    if (link < 0) return false;
    const AI::WalkLink& L = g.laneGraph.walkLinks[link];
    if (L.kind != AI::WL_SIDEWALK || L.length < 8.f || L.halfWidth < 0.9f) return false;
    x = Clamp(x, 3.f, L.length - 3.f);
    out.link = link;
    out.x = x;
    out.halfWidth = L.halfWidth;
    out.pos = g.laneGraph.walkPos(link, x, 0.f, true);
    out.t = g.laneGraph.walkTangent(link, x, true);
    float along = L.sb >= L.sa ? 1.f : -1.f;
    float outSign = (L.lat > 0.f ? 1.f : -1.f) * along;   // building side relative to the right of the walking direction
    out.outward = AI::rightOf(out.t) * outSign;
    return true;
}

// point on the sidewalk: `along` meters along the walking direction, `across` meters toward the buildings
vec3 walkOffset(const GameWorld& g, const WalkSpot& s, float along, float across) {
    vec2 p = s.pos.xy() + s.t * along + s.outward * across;
    return vec3(p, g.groundHeight(p.x, p.y, s.pos.z + 1.f));
}

struct LaneSpot {
    int lane = -1;
    float u = 0.f;
    vec3 pos;
    vec2 t;
};

bool laneSpot(const GameWorld& g, vec2 p, float maxDist, float margin, bool rightmost, LaneSpot& out) {
    float u = 0.f;
    int lane = g.laneGraph.nearestLane(p, vec2(0), maxDist, &u);
    if (lane < 0) return false;
    if (rightmost)
        for (int guard = 0; guard < 8 && g.laneGraph.lanes[lane].right >= 0; guard++) lane = g.laneGraph.lanes[lane].right;
    const AI::Lane& L = g.laneGraph.lanes[lane];
    if (L.flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC)) return false;
    if (L.u1 - L.u0 < margin * 2.f + 10.f) return false;
    out.lane = lane;
    out.u = Clamp(u, L.u0 + margin, L.u1 - margin);
    out.pos = g.laneGraph.lanePos(lane, out.u);
    out.t = g.laneGraph.laneTangent(lane, out.u);
    return true;
}

// a candidate point r0..r1 m from the player, biased ahead of the player's motion
vec2 ringPoint(u32 h, vec2 c, vec2 fwd, float r0, float r1) {
    float ang = hashToFloat(h) * kTwoPi;
    vec2 d(cosf(ang), sinf(ang));
    if (length2(fwd) > 0.01f) d = normalize(d + fwd * 1.2f);
    return c + d * (r0 + hashToFloat(hash32(h ^ 0x5bd1e995u)) * (r1 - r0));
}

bool hiddenFrom(GameWorld& g, vec3 p, float minDist, bool warm) {
    if (warm) return true;
    Ped* pl = g.playerPed();
    if (pl && length(p.xy() - pl->pos.toVec3().xy()) < minDist) return false;
    return !g.inCameraView(p, 4.f);
}

int pickTrafficModel(GameWorld& g, u32 seed, bool fast) {
    using namespace Vehicles;
    if (fast) {
        int m = g.findVehicleModel(VC_SPORTS, seed);
        if (m < 0) m = g.findVehicleModel(VC_SUPER, seed);
        if (m >= 0) return m;
    }
    static const VehicleClass kCommon[] = {VC_SEDAN, VC_COMPACT, VC_SUV, VC_COUPE, VC_PICKUP, VC_MUSCLE};
    for (int k = 0; k < 6; k++) {
        int m = g.findVehicleModel(kCommon[(seed + k) % 6], seed >> 3);
        if (m >= 0) return m;
    }
    return g.vassets.empty() ? -1 : (int)(seed % g.vassets.size());
}

// ---- release
void releaseEvent(GameWorld& g, AmbientEvent& e) {
    for (int k = 0; k < e.np; k++) {
        int id = livePed(g, e.ped[k]);
        if (id < 0) continue;
        freeActor(g, id);
    }
    for (int k = 0; k < e.nv; k++) {
        int id = liveVeh(g, e.veh[k]);
        if (id < 0) continue;
        VehAI& va = g.vehAI(id);
        va.eventId = -1;
        if (va.role == VR_RACER || va.role == VR_EVENT || va.role == VR_BREAKDOWN) va.role = VR_TRAFFIC;
    }
#ifdef HAVE_AUDIO
    if (e.music) Audio::destroyEmitter(e.music);
#endif
    e.music = 0;
    e.active = false;
}

void setStage(AmbientEvent& e, u8 s) {
    e.stage = s;
    e.t = 0.f;
}

// the victim of a theft walks to the dropped wallet/purse: the player can grab it or let them have it back
// (shared stage of muggings and purse snatching). Returns false when the event is over.
bool wallet(GameWorld& g, AmbientEvent& e, int victim, int evId, float dt) {
    Ped* pl = g.playerPed();
    bool present = pickupPresent(g, e.pickup, e.pickupPos);
    if (!present) {
        // someone took it
        bool byPlayer = pl && length(rel(pl->pos, e.pickupPos)) < 2.5f;
        if (byPlayer && victim >= 0 && !isDown(g.peds[victim]) && !e.done) {
            g.aiSay(victim, BK_INSULT, 1.f, true);
            freeActor(g, victim);
            e.done = true;
        }
        return false;
    }
    if (victim < 0 || isDown(g.peds[victim]) || g.peds[victim].brain.type != BRAIN_WANDER) return e.t < 60.f;
    PedAI& va = g.pedAI(victim);
    if (va.activity != ACT_EVENT) setActor(g, victim, evId, e.pickupPos.toVec3().xy(), 0.f, 0, -1);
    va.anchor = e.pickupPos.toVec3().xy();
    vec2 vp = g.peds[victim].pos.toVec3().xy();
    if (length(vp - va.anchor) < 0.9f) {
        g.pickups[e.pickup].used = false;
        g.peds[victim].pendingAction = Anim::CLIP_CROUCH_IDLE;
        g.aiSay(victim, BK_THANKS, 1.f, true);
        if (pl && length(rel(pl->pos, g.peds[victim].pos)) < 30.f && !e.done) payReward(g, 50 + e.amount / 4, "Returned stolen cash");
        e.done = true;
        freeActor(g, victim);
        return false;
    }
    (void)dt;
    return e.t < 60.f;
}

}  // namespace events_detail

using namespace events_detail;

// ------------------------------------------------------------------------------------------------------------------
// Two AI cars knocked into each other in traffic: turn it into a fender-bender scene (they stop, the drivers get out
// and argue, maybe a fight, then drive on - the EV_CRASH stages). Returns false if no event slot is free.
bool GameWorld::aiFenderBender(int ca, int cb) {
    if (ca < 0 || cb < 0 || ca == cb) return false;
    int slot = -1;
    for (int k = 0; k < 3; k++)
        if (!gEv.ev[k].active) slot = k;
    if (slot < 0 || time - gEv.lastOfType[EV_CRASH] < 45.0) return false;
    for (const AmbientEvent& o : gEv.ev)
        if (o.active && o.type == EV_CRASH) return false;
    int da = vehicles[ca].seats[0], db = vehicles[cb].seats[0];
    if (da < 0 || db < 0 || peds[da].isPlayer || peds[db].isPlayer) return false;
    AmbientEvent& e = gEv.ev[slot];
    e = AmbientEvent();
    e.type = EV_CRASH;
    e.stage = ST_A;
    e.flag = 2;
    e.ped[0] = refPed(*this, da);
    e.ped[1] = refPed(*this, db);
    e.np = 2;
    e.veh[0] = refVeh(*this, ca);
    e.veh[1] = refVeh(*this, cb);
    e.nv = 2;
    vec3 pa = vehicles[ca].sim.body.pos.toVec3(), pb = vehicles[cb].sim.body.pos.toVec3();
    e.pos = (pa + pb) * 0.5f;
    vec2 f = vehicles[ca].sim.forward().xy();
    e.dir = length2(f) > 1e-6f ? normalize(f) : vec2(0, 1);
    for (int k = 0; k < 2; k++) {
        int c = k == 0 ? ca : cb, d = k == 0 ? da : db;
        VehAI& va = vehAI(c);
        va.role = VR_EVENT;
        va.eventId = slot;
        peds[d].brain.type = BRAIN_NONE;   // (stays put in the seat until the event lets the driver out)
        pedAI(d).eventId = slot;
    }
    e.active = true;
    gEv.lastOfType[EV_CRASH] = time;
    ai.stats.events++;
    LOG("ai: fender bender between cars %d and %d at %.0f %.0f", ca, cb, e.pos.x, e.pos.y);
    return true;
}

void GameWorld::updateEvents(float dt) {
    Ped* pl = playerPed();
    if (!pl || !ai.ready) return;
    vec2 pp = pl->pos.toVec3().xy();
    int pv = playerVehicle();
    vec2 pvel = pv >= 0 ? vehicles[pv].sim.body.vel.xy() : pl->vel.xy();
    bool warm = populationWarmup > 0.f;
    // ------------------------------------------------------------------ spawning
    gEv.timer -= dt * (warm ? 6.f : 1.f);
    int activeCount = 0;
    for (AmbientEvent& e : gEv.ev) activeCount += e.active;
    if (gEv.timer <= 0.f && activeCount < 2 && !populationOff) {
        u32 h = hash32(gEv.counter++ * 2654435761u + (u32)(time * 3.0));
        gEv.timer = 35.f + hashToFloat(h) * 40.f;
        float tod = env ? env->timeOfDay : 12.f;
        bool night = tod > 20.5f || tod < 5.f;
        World::Region reg = map->regionAt(pp.x, pp.y);
        bool urban = reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_MIDTOWN || reg == World::REG_NORTH_CITY ||
                     reg == World::REG_CALLE_LUNA || reg == World::REG_FLATS || reg == World::REG_BEACH || reg == World::REG_GROVE || reg == World::REG_FORT_CASTELL;
        bool gangTurf = reg == World::REG_CALLE_LUNA || reg == World::REG_FLATS || reg == World::REG_PORT;
        bool nightlife = reg == World::REG_CALLE_LUNA || reg == World::REG_BEACH || reg == World::REG_DOWNTOWN;
        bool scenic = reg == World::REG_BEACH || reg == World::REG_DOWNTOWN || reg == World::REG_KEY_CORAL || reg == World::REG_FINANCIAL || reg == World::REG_BAY_ISLAND;
        bool calmOnly = missionActive() || pinfo.wanted >= 2 || warm;
        float w[EV_COUNT] = {};
        if (!calmOnly) {
            w[EV_MUGGING] = urban ? (night ? 2.2f : 0.8f) : 0.f;
            w[EV_PURSE] = urban ? (night ? 0.4f : 1.3f) : 0.f;
            w[EV_CRASH] = urban ? 1.0f : 0.5f;
            w[EV_RACERS] = night ? 1.6f : 0.35f;
            w[EV_CHASE] = pinfo.wanted == 0 ? (urban ? 0.9f : 0.5f) : 0.f;
            w[EV_SHOOTOUT] = gangTurf && pinfo.wanted == 0 ? 0.7f : 0.f;
        }
        w[EV_DRUNK] = night && nightlife ? 1.8f : (night && urban ? 0.6f : 0.f);
        w[EV_MUSICIAN] = (tod > 10.f && tod < 23.5f) && (urban || scenic) ? 1.2f : 0.f;
        w[EV_TOURISTS] = (tod > 8.5f && tod < 19.5f) && scenic ? 1.4f : 0.f;
        w[EV_BREAKDOWN] = (tod > 6.f && tod < 22.f) ? 0.8f : 0.3f;
        w[EV_TRAFFIC_STOP] = pinfo.wanted == 0 && !policeSuppressed ? (urban ? 1.0f : 0.4f) : 0.f;
        for (int k = 0; k < EV_COUNT; k++) {
            if (time - gEv.lastOfType[k] < 150.0) w[k] = 0.f;
            for (AmbientEvent& e : gEv.ev)
                if (e.active && e.type == k) w[k] = 0.f;
        }
        float sum = 0.f;
        for (float x : w) sum += x;
        int slot = -1;
        for (int k = 0; k < 3; k++)
            if (!gEv.ev[k].active) slot = k;
        if (sum > 0.f && slot >= 0) {
            float r = hashToFloat(hash32(h ^ 0xABCDu)) * sum;
            int type = 0;
            for (; type < EV_COUNT - 1; type++) {
                r -= w[type];
                if (r <= 0.f) break;
            }
            while (w[type] <= 0.f && type > 0) type--;
            AmbientEvent& e = gEv.ev[slot];
            e = AmbientEvent();
            e.type = (u8)type;
            int evId = slot;
            bool ok = false;
            vec2 fwd = length(pvel) > 3.f ? normalize(pvel) : vec2(0);
            for (int attempt = 0; attempt < 6 && !ok; attempt++) {
                u32 ha = hash32(h + attempt * 7919u);
                switch (type) {
                    // ---------------------------------------------------------------- mugging
                    case EV_MUGGING: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 45.f, 95.f), 35.f, ws)) break;
                        vec3 vpos = walkOffset(*this, ws, 0.f, ws.halfWidth * 0.55f);
                        vec3 mpos = walkOffset(*this, ws, 0.3f, -ws.halfWidth * 0.35f);
                        if (!hiddenFrom(*this, vpos, 35.f, warm)) break;
                        int victim = spawnActor(*this, vpos, 0.f, ha, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        int mugger = victim >= 0 ? spawnActor(*this, mpos, 0.f, hash32(ha + 1u) | 0u, 0, FAC_CIVILIAN, PR_CIVILIAN, evId) : -1;
                        if (mugger < 0) {
                            if (victim >= 0) despawnPed(victim);
                            break;
                        }
                        bool gun = (ha >> 8) % 3 != 0;
                        giveWeapon(mugger, gun ? WPN_PISTOL : WPN_KNIFE, gun ? 24 : 1);
                        peds[mugger].weapon = gun ? WPN_PISTOL : WPN_KNIFE;
                        setActor(*this, victim, evId, vpos.xy(), yawTowards(vpos.xy(), mpos.xy()), 5, -1);
                        setActor(*this, mugger, evId, mpos.xy(), yawTowards(mpos.xy(), vpos.xy()), 0, -1);
                        pedAI(mugger).aimAt = gun ? victim : -1;
                        pedAI(victim).temper = 0;
                        peds[victim].yaw = yawTowards(vpos.xy(), mpos.xy());
                        peds[mugger].yaw = yawTowards(mpos.xy(), vpos.xy());
                        e.ped[0] = refPed(*this, victim);
                        e.ped[1] = refPed(*this, mugger);
                        e.np = 2;
                        e.pos = vpos;
                        e.dir = ws.t;
                        e.amount = 40 + (int)(ha % 160u);
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- purse snatching
                    case EV_PURSE: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 35.f, 75.f), 30.f, ws)) break;
                        const AI::WalkLink& L = laneGraph.walkLinks[ws.link];
                        if (ws.x < 18.f && L.length - ws.x < 18.f) break;
                        float back = ws.x > 18.f ? -14.f : 14.f;   // thief comes from behind the victim's walking direction
                        vec3 vpos = walkOffset(*this, ws, 0.f, 0.f);
                        vec3 tpos = walkOffset(*this, ws, back, -ws.halfWidth * 0.3f);
                        if (!hiddenFrom(*this, vpos, 30.f, warm) || !hiddenFrom(*this, tpos, 30.f, warm)) break;
                        u32 hv = (ha & ~1u) | ((ha >> 5) % 10 < 7 ? 1u : 0u);   // mostly women (odd seeds)
                        int victim = spawnActor(*this, vpos, AI::dirYaw(back < 0.f ? ws.t : -ws.t), hv, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        int thief = victim >= 0 ? spawnActor(*this, tpos, AI::dirYaw(back < 0.f ? ws.t : -ws.t), (hash32(ha) & ~1u), 0, FAC_CIVILIAN, PR_CIVILIAN, evId) : -1;
                        if (thief < 0) {
                            if (victim >= 0) despawnPed(victim);
                            break;
                        }
                        // the victim walks along the sidewalk away from the thief
                        PedAI& va = pedAI(victim);
                        if (pedNav.place(va.walk, vpos.xy(), peds[victim].uid * 2654435761u + 7u, 10.f)) {
                            va.navOk = true;
                            peds[victim].brain.edge = va.walk.link;
                            va.walk.fromA = back < 0.f;
                            va.walk.x = back < 0.f ? ws.x : laneGraph.walkLinks[ws.link].length - ws.x;
                            va.walk.link = ws.link;
                            va.actTimer = 60.f;
                        }
                        Brain& tb = peds[thief].brain;
                        tb.type = BRAIN_GOTO;
                        tb.goal = dvec3(vpos);
                        tb.speed = 4.2f;
                        e.ped[0] = refPed(*this, victim);
                        e.ped[1] = refPed(*this, thief);
                        e.np = 2;
                        e.pos = vpos;
                        e.dir = ws.t;
                        e.amount = 60 + (int)(ha % 180u);
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- fender bender
                    case EV_CRASH: {
                        LaneSpot ls;
                        if (!laneSpot(*this, ringPoint(ha, pp, fwd, 70.f, 140.f), 40.f, 24.f, true, ls)) break;
                        const AI::Lane& L = laneGraph.lanes[ls.lane];
                        if (L.flags & AI::LF_HIGHWAY) break;
                        if (!hiddenFrom(*this, ls.pos, 60.f, warm)) break;
                        int ma = pickTrafficModel(*this, ha, false), mb = pickTrafficModel(*this, hash32(ha + 3u), false);
                        if (ma < 0 || mb < 0) break;
                        float hla = vassets[ma].spec.boxHalf.y, hlb = vassets[mb].spec.boxHalf.y;
                        float ub = ls.u + hla + hlb + 0.35f;
                        if (ub > L.u1 - 10.f || !traffic.laneFree(ls.lane, ls.u, hla, 6.f) || !traffic.laneFree(ls.lane, ub, hlb, 6.f)) break;
                        vec3 pa3 = laneGraph.lanePos(ls.lane, ls.u, 0.35f), pb3 = laneGraph.lanePos(ls.lane, ub, 0.2f);
                        vec2 ta = laneGraph.laneTangent(ls.lane, ls.u), tb = laneGraph.laneTangent(ls.lane, ub);
                        int ca = spawnVehicle(ma, dvec3(pa3.x, pa3.y, pa3.z + 0.35f), AI::dirYaw(ta) + 0.1f, true);
                        int cb = ca >= 0 ? spawnVehicle(mb, dvec3(pb3.x, pb3.y, pb3.z + 0.35f), AI::dirYaw(tb) - 0.06f, true) : -1;
                        if (cb < 0) {
                            if (ca >= 0) despawnVehicle(ca, true);
                            break;
                        }
                        int da = vehicles[ca].seats[0], db = vehicles[cb].seats[0];
                        if (da < 0 || db < 0) {
                            despawnVehicle(ca, true);
                            despawnVehicle(cb, true);
                            break;
                        }
                        // the drivers are out, standing between the cars on the curb side and arguing
                        vec2 rgt = AI::rightOf(ls.t);
                        vec2 mid = (pa3.xy() + pb3.xy()) * 0.5f + rgt * (Max(vassets[ma].spec.boxHalf.x, vassets[mb].spec.boxHalf.x) + 1.0f);
                        vec2 sa = mid - ls.t * 0.8f, sb = mid + ls.t * 0.8f;
                        for (int k = 0; k < 2; k++) {
                            int c = k == 0 ? ca : cb, d = k == 0 ? da : db;
                            vec2 s = k == 0 ? sa : sb;
                            Vehicle& v = vehicles[c];
                            removePedFromVehicle(d, false);
                            peds[d].pos = dvec3(s.x, s.y, groundHeight(s.x, s.y, pa3.z + 1.5f));
                            v.parked = true;
                            v.sim.engineOn = false;
                            v.lightsOn = env && (env->timeOfDay > 19.5f || env->timeOfDay < 6.5f);
                            VehAI& vai = vehAI(c);
                            vai.role = VR_EVENT;
                            vai.eventId = evId;
                            setActor(*this, d, evId, s, yawTowards(s, k == 0 ? sb : sa), 7, -1);
                            pedAI(d).temper = (u8)((ha >> (k + 3)) % 3);
                        }
                        damageVehicle(ca, 120.f, -1, vec3(0.f, vassets[ma].spec.boxHalf.y, 0.2f), vec3(0.f));
                        damageVehicle(cb, 90.f, -1, vec3(0.f, -vassets[mb].spec.boxHalf.y, 0.2f), vec3(0.f));
                        e.ped[0] = refPed(*this, da);
                        e.ped[1] = refPed(*this, db);
                        e.np = 2;
                        e.veh[0] = refVeh(*this, ca);
                        e.veh[1] = refVeh(*this, cb);
                        e.nv = 2;
                        e.pos = vec3(mid, pa3.z);
                        e.dir = ls.t;
                        e.flag = (ha >> 12) % 10 < 3 ? 1 : 0;   // escalates into a fist fight
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- street racers
                    case EV_RACERS: {
                        LaneSpot ls;
                        if (!laneSpot(*this, ringPoint(ha, pp, fwd, 160.f, 260.f), 50.f, 12.f, false, ls)) break;
                        const AI::Lane& L = laneGraph.lanes[ls.lane];
                        if (L.speed < 12.f && L.count < 2) break;
                        if (!hiddenFrom(*this, ls.pos, 140.f, warm)) break;
                        int lane2 = L.left >= 0 ? L.left : (L.right >= 0 ? L.right : ls.lane);
                        float u2 = lane2 == ls.lane ? ls.u - 11.f : ls.u;
                        if (u2 < laneGraph.lanes[lane2].u0 + 3.f) break;
                        int m1 = pickTrafficModel(*this, ha, true), m2 = pickTrafficModel(*this, hash32(ha * 5u), true);
                        if (m1 < 0 || m2 < 0) break;
                        if (!traffic.laneFree(ls.lane, ls.u, 3.f, 8.f) || !traffic.laneFree(lane2, u2, 3.f, 8.f)) break;
                        int cars[2] = {-1, -1};
                        int lanes2[2] = {ls.lane, lane2};
                        float us[2] = {ls.u, u2};
                        for (int k = 0; k < 2; k++) {
                            vec3 p3 = laneGraph.lanePos(lanes2[k], us[k]);
                            vec2 t = laneGraph.laneTangent(lanes2[k], us[k]);
                            cars[k] = spawnVehicle(k == 0 ? m1 : m2, dvec3(p3.x, p3.y, p3.z + 0.35f), AI::dirYaw(t), true);
                            if (cars[k] < 0) break;
                            Vehicle& v = vehicles[cars[k]];
                            if (v.seats[0] >= 0) peds[v.seats[0]].brain.type = BRAIN_DRIVER;
                            v.sim.body.vel = vec3(t * 12.f, 0.f);
                            v.lightsOn = night;
                            attachTraffic(cars[k], lanes2[k], us[k]);
                            if (AI::Driver* d = traffic.get(cars[k])) {
                                d->pers.temper = AI::TEMP_AGGRESSIVE;
                                d->pers.speedFactor = 1.75f;
                                d->pers.headway = 0.7f;
                                d->pers.minGap = 1.6f;
                                d->pers.accel = 4.5f;
                                d->pers.decel = 5.f;
                                d->pers.latAcc = 4.5f;
                                d->pers.runsAmber = true;
                                d->pers.overtake = 1.5f;
                                d->pers.gapTime = 2.4f;
                                d->pers.patience = 1.5f;
                                vec2 dest = ls.pos.xy() + t * 1100.f;
                                traffic.setDestination(*d, dest);
                                d->mode = AI::DM_ROUTE;
                            }
                            VehAI& vai = vehAI(cars[k]);
                            vai.role = VR_RACER;
                            vai.eventId = evId;
                            e.veh[k] = refVeh(*this, cars[k]);
                            e.ped[k] = refPed(*this, v.seats[0]);
                        }
                        if (cars[0] < 0 || cars[1] < 0) {
                            for (int k = 0; k < 2; k++)
                                if (cars[k] >= 0) despawnVehicle(cars[k], true);
                            break;
                        }
                        e.nv = 2;
                        e.np = 2;
                        e.pos = ls.pos;
                        e.dir = ls.t;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- police chasing a criminal
                    case EV_CHASE: {
                        LaneSpot ls;
                        if (!laneSpot(*this, ringPoint(ha, pp, fwd, 170.f, 260.f), 50.f, 30.f, false, ls)) break;
                        if (!hiddenFrom(*this, ls.pos, 150.f, warm)) break;
                        const AI::Lane& L = laneGraph.lanes[ls.lane];
                        float uc = ls.u - 22.f;
                        if (uc < L.u0 + 3.f) break;
                        int mc = pickTrafficModel(*this, ha, (ha >> 4) % 2 == 0);
                        int mp = findVehicleModel(Vehicles::VC_POLICE, ha >> 7);
                        if (mc < 0 || mp < 0) break;
                        if (!traffic.laneFree(ls.lane, ls.u, 3.f, 6.f) || !traffic.laneFree(ls.lane, uc, 3.f, 6.f)) break;
                        vec3 p1 = laneGraph.lanePos(ls.lane, ls.u), p2 = laneGraph.lanePos(ls.lane, uc);
                        vec2 t1 = laneGraph.laneTangent(ls.lane, ls.u), t2 = laneGraph.laneTangent(ls.lane, uc);
                        int crook = spawnVehicle(mc, dvec3(p1.x, p1.y, p1.z + 0.35f), AI::dirYaw(t1), true);
                        int cop = crook >= 0 ? spawnVehicle(mp, dvec3(p2.x, p2.y, p2.z + 0.35f), AI::dirYaw(t2), true, FAC_POLICE) : -1;
                        if (cop < 0 || vehicles[crook].seats[0] < 0 || vehicles[cop].seats[0] < 0) {
                            if (crook >= 0) despawnVehicle(crook, true);
                            if (cop >= 0) despawnVehicle(cop, true);
                            break;
                        }
                        int cd = vehicles[crook].seats[0], od = vehicles[cop].seats[0];
                        vehicles[crook].sim.body.vel = vec3(t1 * 15.f, 0.f);
                        vehicles[cop].sim.body.vel = vec3(t2 * 15.f, 0.f);
                        attachTraffic(crook, ls.lane, ls.u);
                        attachTraffic(cop, ls.lane, uc);
                        peds[cd].brain.type = BRAIN_FLEE;
                        peds[cd].brain.target = od;
                        peds[cd].brain.timer = 0.f;
                        if ((ha >> 9) % 3 == 0) {
                            giveWeapon(cd, WPN_PISTOL, 36);
                            peds[cd].weapon = WPN_PISTOL;
                        }
                        pedAI(cd).eventId = evId;
                        vehAI(crook).eventId = evId;
                        vehAI(crook).role = VR_EVENT;
                        Vehicle& pc = vehicles[cop];
                        pc.faction = FAC_POLICE;
                        pc.sirenOn = true;
                        pc.sirenSilent = false;
                        VehAI& pva = vehAI(cop);
                        pva.role = VR_POLICE;
                        pva.task = PT_PURSUE;
                        pva.eventId = evId;
                        int partner = spawnPed(randomCivilianChar(hash32(ha * 3u), 1), pc.sim.body.pos, 0.f, FAC_POLICE);
                        if (partner >= 0) {
                            int seat = freeSeat(cop, false);
                            if (seat > 0) warpPedIntoVehicle(partner, cop, seat);
                            else {
                                despawnPed(partner);
                                partner = -1;
                            }
                        }
                        for (int o : {od, partner}) {
                            if (o < 0) continue;
                            giveWeapon(o, WPN_PISTOL, 120);
                            peds[o].weapon = WPN_PISTOL;
                            peds[o].brain.type = BRAIN_COMBAT;
                            peds[o].brain.target = cd;
                            peds[o].brain.accuracy = 0.45f;
                            pedAI(o).homeVeh = cop;
                            pedAI(o).eventId = evId;
                        }
                        addCrimeIncident(*this, dvec3(p1), cd);
                        for (Incident& inc : ai.incidents)
                            if (inc.active && inc.kind == 2 && inc.perp == cd) inc.unit = cop;
                        e.veh[0] = refVeh(*this, crook);
                        e.veh[1] = refVeh(*this, cop);
                        e.nv = 2;
                        e.ped[0] = refPed(*this, cd);
                        e.ped[1] = refPed(*this, od);
                        e.np = 2;
                        if (partner >= 0) e.ped[e.np++] = refPed(*this, partner);
                        e.pos = p1;
                        e.dir = t1;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- gang shootout
                    case EV_SHOOTOUT: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 80.f, 130.f), 40.f, ws)) break;
                        const AI::WalkLink& L = laneGraph.walkLinks[ws.link];
                        if (L.length < 30.f) break;
                        float xa = Clamp(ws.x - 14.f, 2.f, L.length - 2.f), xb = Clamp(ws.x + 14.f, 2.f, L.length - 2.f);
                        if (xb - xa < 20.f) break;
                        vec3 ca3 = laneGraph.walkPos(ws.link, xa, 0.f, true), cb3 = laneGraph.walkPos(ws.link, xb, 0.f, true);
                        if (!hiddenFrom(*this, ca3, 60.f, warm) || !hiddenFrom(*this, cb3, 60.f, warm)) break;
                        int n = 0;
                        for (int side = 0; side < 2; side++) {
                            Faction f = side == 0 ? FAC_GANG_CUERVOS : FAC_GANG_SAINTS;
                            vec3 c = side == 0 ? ca3 : cb3;
                            for (int k = 0; k < 3; k++) {
                                u32 hk = hash32(ha * 31u + side * 7u + k * 131u);
                                vec2 off = ws.t * ((hashToFloat(hk) - 0.5f) * 5.f) + ws.outward * ((hashToFloat(hash32(hk)) - 0.5f) * ws.halfWidth * 1.6f);
                                vec3 p3(c.xy() + off, groundHeight(c.x + off.x, c.y + off.y, c.z + 1.f));
                                int id = spawnActor(*this, p3, yawTowards(p3.xy(), (side == 0 ? cb3 : ca3).xy()), hk, 2, f, PR_GANG, evId);
                                if (id < 0) continue;
                                static const WeaponType kW[4] = {WPN_PISTOL, WPN_SMG, WPN_PISTOL, WPN_SHOTGUN};
                                WeaponType wpn = kW[hk % 4];
                                giveWeapon(id, wpn, 150);
                                peds[id].weapon = wpn;
                                peds[id].brain.accuracy = 0.15f + hashToFloat(hash32(hk + 9u)) * 0.2f;   // thugs 0.15-0.35
                                peds[id].brain.aggression = 0.7f;
                                setActor(*this, id, evId, p3.xy(), yawTowards(p3.xy(), (side == 0 ? cb3 : ca3).xy()), 0, -1);
                                e.ped[n++] = refPed(*this, id);
                            }
                        }
                        e.np = n;
                        if (n < 4) {
                            for (int k = 0; k < n; k++)
                                if (livePed(*this, e.ped[k]) >= 0) despawnPed(e.ped[k].id);
                            e.np = 0;
                            break;
                        }
                        e.pos = (ca3 + cb3) * 0.5f;
                        e.dir = ws.t;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- drunk
                    case EV_DRUNK: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 30.f, 60.f), 30.f, ws)) break;
                        if (!hiddenFrom(*this, ws.pos, 25.f, warm)) break;
                        int id = spawnActor(*this, ws.pos, AI::dirYaw(ws.t), ha, 0, FAC_CIVILIAN, PR_DRUNK, evId);
                        if (id < 0) break;
                        PedAI& da = pedAI(id);
                        if (pedNav.place(da.walk, ws.pos.xy(), peds[id].uid * 2654435761u + 7u, 10.f)) {
                            da.navOk = true;
                            peds[id].brain.edge = da.walk.link;
                            da.walk.speed = 0.9f;
                            da.walk.jaywalker = true;
                        }
                        da.actTimer = 1e5f;   // no scenario stops: just stumbles along
                        e.ped[0] = refPed(*this, id);
                        e.np = 1;
                        e.pos = ws.pos;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- street musician
                    case EV_MUSICIAN: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 35.f, 75.f), 35.f, ws)) break;
                        if (ws.halfWidth < 1.4f) break;
                        vec3 mpos = walkOffset(*this, ws, 0.f, ws.halfWidth * 0.7f);
                        if (!hiddenFrom(*this, mpos, 30.f, warm)) break;
                        int mus = spawnActor(*this, mpos, AI::dirYaw(-ws.outward), ha, 0, FAC_CIVILIAN, PR_MUSICIAN, evId);
                        if (mus < 0) break;
                        setActor(*this, mus, evId, mpos.xy(), AI::dirYaw(-ws.outward), 9, -1);
                        e.ped[0] = refPed(*this, mus);
                        e.np = 1;
                        int audience = 2 + (int)((ha >> 6) % 3u);
                        for (int k = 0; k < audience; k++) {
                            float a = (k - (audience - 1) * 0.5f) * 0.55f;
                            vec2 dirOut = rotate(-ws.outward, a);
                            vec2 sp = mpos.xy() + dirOut * (2.6f + hashToFloat(hash32(ha + k)) * 0.8f);
                            vec3 sp3(sp, groundHeight(sp.x, sp.y, mpos.z + 1.f));
                            if (length(sp3.xy() - pp) < 5.f) continue;
                            int wid = spawnActor(*this, sp3, 0.f, hash32(ha * 17u + k), (k % 3 == 2) ? 4 : 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                            if (wid < 0) continue;
                            setActor(*this, wid, evId, sp, yawTowards(sp, mpos.xy()), k % 2 ? 7 : 0, -1);
                            pedAI(wid).activity = ACT_WATCH;
                            pedAI(wid).clipTimer = 2.f + k;
                            e.ped[e.np++] = refPed(*this, wid);
                        }
#ifdef HAVE_AUDIO
                        e.music = Audio::createEmitter(Audio::EMIT_RADIO_WORLD);
                        e.flag = 0;
                        for (int s = 0; s < Audio::radioStationCount(); s++) {
                            const char* genre = Audio::radioStationGenre(s);
                            if (genre && (strstr(genre, "Latin") || strstr(genre, "Acoustic") || strstr(genre, "Jazz") || strstr(genre, "Folk"))) {
                                e.flag = s;
                                break;
                            }
                        }
#endif
                        e.pos = mpos;
                        e.dir = ws.t;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- tourists taking photos
                    case EV_TOURISTS: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 30.f, 70.f), 35.f, ws)) break;
                        vec3 cam3 = walkOffset(*this, ws, 0.f, ws.halfWidth * 0.5f);
                        if (!hiddenFrom(*this, cam3, 25.f, warm)) break;
                        int n = 2 + (int)((ha >> 3) % 2u);
                        int photographer = spawnActor(*this, cam3, 0.f, ha, 4, FAC_CIVILIAN, PR_TOURIST, evId);
                        if (photographer < 0) break;
                        e.ped[0] = refPed(*this, photographer);
                        e.np = 1;
                        vec2 poseC = cam3.xy() + ws.t * 3.2f;
                        for (int k = 1; k < n; k++) {
                            vec2 sp = poseC + AI::rightOf(ws.t) * ((k - n * 0.5f) * 0.9f);
                            vec3 sp3(sp, groundHeight(sp.x, sp.y, cam3.z + 1.f));
                            int id = spawnActor(*this, sp3, 0.f, hash32(ha + k * 97u), 4, FAC_CIVILIAN, PR_TOURIST, evId);
                            if (id < 0) continue;
                            setActor(*this, id, evId, sp, yawTowards(sp, cam3.xy()), 0, -1);
                            e.ped[e.np++] = refPed(*this, id);
                        }
                        if (e.np < 2) {
                            despawnPed(photographer);
                            e.np = 0;
                            break;
                        }
                        setActor(*this, photographer, evId, cam3.xy(), yawTowards(cam3.xy(), poseC), 8, -1);
                        e.amount = 10 + (int)(ha % 21u);
                        e.pos = cam3;
                        e.dir = ws.t;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- broken-down car
                    case EV_BREAKDOWN: {
                        LaneSpot ls;
                        if (!laneSpot(*this, ringPoint(ha, pp, fwd, 60.f, 130.f), 40.f, 25.f, true, ls)) break;
                        const AI::Lane& L = laneGraph.lanes[ls.lane];
                        if (L.flags & AI::LF_HIGHWAY) break;
                        if (!hiddenFrom(*this, ls.pos, 50.f, warm)) break;
                        int m = pickTrafficModel(*this, ha, false);
                        if (m < 0 || !traffic.laneFree(ls.lane, ls.u, vassets[m].spec.boxHalf.y, 8.f)) break;
                        float curb = L.width * 0.5f - vassets[m].spec.boxHalf.x - 0.15f;
                        vec3 p3 = laneGraph.lanePos(ls.lane, ls.u, Max(curb, 0.f));
                        int car = spawnVehicle(m, dvec3(p3.x, p3.y, p3.z + 0.35f), AI::dirYaw(ls.t) + 0.03f, true);
                        if (car < 0) break;
                        int drv = vehicles[car].seats[0];
                        if (drv < 0) {
                            despawnVehicle(car, true);
                            break;
                        }
                        removePedFromVehicle(drv, false);
                        vec2 rgt = AI::rightOf(ls.t);
                        vec2 sp = p3.xy() + ls.t * (vassets[m].spec.boxHalf.y * 0.6f) + rgt * (vassets[m].spec.boxHalf.x + 1.1f);
                        peds[drv].pos = dvec3(sp.x, sp.y, groundHeight(sp.x, sp.y, p3.z + 1.5f));
                        Vehicle& v = vehicles[car];
                        v.parked = true;
                        v.sim.engineOn = false;
                        v.indicator = 2;   // hazard lights
                        v.lightsOn = env && (env->timeOfDay > 19.5f || env->timeOfDay < 6.5f);
                        VehAI& vai = vehAI(car);
                        vai.role = VR_BREAKDOWN;
                        vai.eventId = evId;
                        setActor(*this, drv, evId, sp, AI::dirYaw(-ls.t), 0, -1);
                        e.ped[0] = refPed(*this, drv);
                        e.np = 1;
                        e.veh[0] = refVeh(*this, car);
                        e.nv = 1;
                        e.pos = p3;
                        e.dir = ls.t;
                        e.amount = 40 + (int)(ha % 81u);
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- traffic stop
                    case EV_TRAFFIC_STOP: {
                        LaneSpot ls;
                        if (!laneSpot(*this, ringPoint(ha, pp, fwd, 60.f, 140.f), 40.f, 30.f, true, ls)) break;
                        const AI::Lane& L = laneGraph.lanes[ls.lane];
                        if (L.flags & AI::LF_HIGHWAY) break;
                        if (!hiddenFrom(*this, ls.pos, 50.f, warm)) break;
                        int m = pickTrafficModel(*this, ha, (ha >> 7) % 4 == 0);
                        int pm = findVehicleModel(Vehicles::VC_POLICE, ha >> 3);
                        if (m < 0 || pm < 0) break;
                        float hlA = vassets[m].spec.boxHalf.y, hlP = vassets[pm].spec.boxHalf.y;
                        float uP = ls.u - hlA - hlP - 3.f;
                        if (uP < L.u0 + 5.f || !traffic.laneFree(ls.lane, ls.u, hlA, 6.f) || !traffic.laneFree(ls.lane, uP, hlP, 6.f)) break;
                        vec3 a3 = laneGraph.lanePos(ls.lane, ls.u, Max(L.width * 0.5f - vassets[m].spec.boxHalf.x - 0.15f, 0.f));
                        vec3 c3 = laneGraph.lanePos(ls.lane, uP, Max(L.width * 0.5f - vassets[pm].spec.boxHalf.x - 0.15f, 0.f));
                        int car = spawnVehicle(m, dvec3(a3.x, a3.y, a3.z + 0.35f), AI::dirYaw(ls.t), true);
                        int cop = car >= 0 ? spawnVehicle(pm, dvec3(c3.x, c3.y, c3.z + 0.35f), AI::dirYaw(laneGraph.laneTangent(ls.lane, uP)), true, FAC_POLICE) : -1;
                        if (car < 0 || cop < 0 || vehicles[car].seats[0] < 0 || vehicles[cop].seats[0] < 0) {
                            if (car >= 0) despawnVehicle(car, true);
                            if (cop >= 0) despawnVehicle(cop, true);
                            break;
                        }
                        int drv = vehicles[car].seats[0], off = vehicles[cop].seats[0];
                        for (int vid : {car, cop}) {
                            Vehicle& v = vehicles[vid];
                            v.parked = true;
                            v.sim.engineOn = true;
                            v.lightsOn = env && (env->timeOfDay > 19.5f || env->timeOfDay < 6.5f);
                            VehAI& vai = vehAI(vid);
                            vai.role = vid == cop ? VR_POLICE : VR_EVENT;
                            vai.eventId = evId;
                        }
                        vehicles[cop].faction = FAC_POLICE;
                        vehicles[cop].sirenOn = true;
                        vehicles[cop].sirenSilent = true;   // light bar flashing, no siren
                        peds[drv].brain.type = BRAIN_NONE;   // both stay put in their seats until the stop is over
                        peds[off].brain.type = BRAIN_NONE;
                        pedAI(drv).eventId = evId;
                        pedAI(off).eventId = evId;
                        pedAI(off).role = PR_COP;
                        e.ped[0] = refPed(*this, drv);
                        e.ped[1] = refPed(*this, off);
                        e.np = 2;
                        e.veh[0] = refVeh(*this, car);
                        e.veh[1] = refVeh(*this, cop);
                        e.nv = 2;
                        e.pos = a3;
                        e.dir = ls.t;
                        e.amount = (int)(ha % 9u);
                        ok = true;
                        break;
                    }
                    default: break;
                }
            }
            if (ok) {
                e.active = true;
                gEv.lastOfType[type] = time;
                ai.stats.events++;
            } else {
                gEv.timer = 6.f;   // try again soon
            }
        }
    }
    // ------------------------------------------------------------------ update
    for (int slot = 0; slot < 3; slot++) {
        AmbientEvent& e = gEv.ev[slot];
        if (!e.active) continue;
        e.t += dt;
        e.age += dt;
        e.barkT -= dt;
        e.fxT -= dt;
        int evId = slot;
        float plDist = length(e.pos.xy() - pp);
        float keepR = (e.type == EV_RACERS || e.type == EV_CHASE) ? 480.f : 260.f;
        bool over = false;
        // follow moving participants with the event anchor
        if (e.type == EV_RACERS || e.type == EV_CHASE) {
            int v0 = liveVeh(*this, e.veh[0]);
            if (v0 >= 0) e.pos = vehicles[v0].sim.body.pos.toVec3();
        } else if (e.type == EV_DRUNK || e.type == EV_PURSE) {
            int p0 = livePed(*this, e.ped[e.type == EV_PURSE ? 1 : 0]);
            if (p0 >= 0) e.pos = peds[p0].pos.toVec3();
        }
        switch (e.type) {
            // stages: A hold-up, B escape with the money, C scared off / foiled, D wallet on the ground
            case EV_MUGGING: {
                int victim = livePed(*this, e.ped[0]), mugger = livePed(*this, e.ped[1]);
                bool mDown = mugger < 0 || isDown(peds[mugger]);
                if (e.stage == ST_A) {
                    bool vCalm = calmActor(*this, victim);
                    if (mugger >= 0 && !mDown && e.t > 0.4f && e.flag == 0) {
                        aiSay(mugger, BK_MUGGER, 1.f, plDist < 25.f);
                        e.flag = 1;
                    }
                    if (victim >= 0 && e.t > 2.8f && e.flag == 1) {
                        aiSay(victim, BK_VICTIM, 1.f, plDist < 25.f);
                        e.flag = 2;
                    }
                    if (mugger >= 0 && !mDown && e.barkT <= 0.f) {
                        e.barkT = 3.f;
                        aiStimulus(peds[mugger].pos, STIM_FIGHT, mugger, 12.f, false);
                        if (pedAI(mugger).aimAt < 0) peds[mugger].pendingAction = Anim::CLIP_POINT;
                    }
                    bool foiled = mDown || hurtByPlayer(*this, mugger) || playerAimsAt(*this, mugger, 30.f);
                    if (foiled) {
                        if (mugger >= 0 && !mDown) {
                            PedAI& ma = pedAI(mugger);
                            ma.activity = ACT_WALK;
                            ma.aimAt = -1;
                            Brain& mb = peds[mugger].brain;
                            bool fight = peds[mugger].weapon == WPN_PISTOL && ma.temper == 2 && hurtByPlayer(*this, mugger);
                            mb.type = fight ? BRAIN_COMBAT : BRAIN_FLEE;
                            mb.target = player;
                            mb.timer = 0.f;
                            aiSay(mugger, fight ? BK_INSULT : BK_FLEE, 0.9f, true);
                        }
                        setStage(e, ST_C);
                    } else if (!vCalm || mugger < 0) {
                        // the victim ran (gunfire nearby, the player...) - the mugger takes off too
                        if (mugger >= 0 && !mDown) {
                            pedAI(mugger).activity = ACT_WALK;
                            pedAI(mugger).aimAt = -1;
                            peds[mugger].brain.type = BRAIN_FLEE;
                            peds[mugger].brain.target = player;
                            peds[mugger].brain.timer = 0.f;
                        }
                        over = true;
                    } else if (e.t > 9.f) {
                        // robbery done: he runs with the money, the victim shouts for help and calls the police
                        PedAI& ma = pedAI(mugger);
                        ma.activity = ACT_WALK;
                        ma.aimAt = -1;
                        peds[mugger].brain.type = BRAIN_FLEE;
                        peds[mugger].brain.target = victim;
                        peds[mugger].brain.timer = 0.f;
                        peds[victim].pendingAction = Anim::CLIP_POINT;
                        aiSay(victim, BK_HELP, 1.f, true);
                        addCrimeIncident(*this, peds[mugger].pos, mugger);
                        setStage(e, ST_B);
                    }
                } else if (e.stage == ST_B) {
                    if (victim >= 0 && calmActor(*this, victim) && e.t > 3.f) {
                        PedAI& va = pedAI(victim);
                        va.activity = ACT_CALL_POLICE;
                        va.threatPos = mugger >= 0 ? peds[mugger].pos.toVec3().xy() : e.pos.xy();
                        va.actTimer = 12.f;
                    }
                    if (mugger >= 0 && mDown) {
                        e.pickupPos = peds[mugger].pos;
                        e.pickup = dropMoney(*this, e.pickupPos, e.amount);
                        e.pickupPos = pickups[e.pickup].pos;
                        if (victim >= 0 && !isDown(peds[victim])) {
                            peds[victim].brain.type = BRAIN_WANDER;
                            setActor(*this, victim, evId, e.pickupPos.toVec3().xy(), 0.f, 0, -1);
                        }
                        setStage(e, ST_D);
                    } else if (e.t > 35.f || mugger < 0) {
                        over = true;
                    }
                } else if (e.stage == ST_C) {
                    // scared off before he got anything: the victim thanks the player
                    if (victim < 0 || isDown(peds[victim]) || e.done) {
                        if (e.t > 6.f || victim < 0) over = true;
                        break;
                    }
                    if (peds[victim].brain.type != BRAIN_WANDER) {
                        over = e.t > 4.f;
                        break;
                    }
                    PedAI& va = pedAI(victim);
                    vec2 vp = peds[victim].pos.toVec3().xy();
                    if (plDist < 30.f && pl->state == PS_ONFOOT) {
                        if (va.activity != ACT_EVENT) setActor(*this, victim, evId, vp, 0.f, 0, -1);
                        vec2 toP = pp - vp;
                        float d = length(toP);
                        va.anchor = d > 2.2f ? pp - toP / d * 1.8f : vp;
                        va.anchorYaw = yawTowards(vp, pp);
                        va.stance = 0;
                        if (d < 2.6f) {
                            aiSay(victim, BK_THANKS, 1.f, true);
                            peds[victim].pendingAction = Anim::CLIP_WAVE;
                            payReward(*this, 25 + e.amount / 5, "Stopped a mugging");
                            e.done = true;
                            e.t = 0.f;
                        }
                    }
                    if (e.t > 25.f) over = true;
                } else if (e.stage == ST_D) {
                    if (!wallet(*this, e, victim, evId, dt)) over = true;
                }
                break;
            }
            // stages: A thief closes in, B running with the purse, D purse on the ground
            case EV_PURSE: {
                int victim = livePed(*this, e.ped[0]), thief = livePed(*this, e.ped[1]);
                bool tDown = thief < 0 || isDown(peds[thief]);
                if (e.stage == ST_A) {
                    if (victim < 0 || isDown(peds[victim]) || peds[victim].brain.type != BRAIN_WANDER || tDown || peds[thief].brain.type != BRAIN_GOTO) {
                        if (!tDown && thief >= 0 && peds[thief].brain.type == BRAIN_GOTO) {
                            peds[thief].brain.type = BRAIN_WANDER;
                            peds[thief].brain.edge = -1;
                        }
                        over = true;
                        break;
                    }
                    Ped& v = peds[victim];
                    Brain& tb = peds[thief].brain;
                    vec3 vp = v.pos.toVec3();
                    tb.goal = dvec3(vp + v.vel * 0.4f);
                    tb.speed = 4.4f;
                    float d = length(peds[thief].pos.toVec3().xy() - vp.xy());
                    if (d < 1.3f) {
                        v.pendingAction = Anim::CLIP_STAGGER;
                        aiSay(victim, BK_VICTIM, 1.f, true);
                        tb.type = BRAIN_FLEE;
                        tb.target = victim;
                        tb.timer = 0.f;
                        pedAI(thief).activity = ACT_WALK;
                        setActor(*this, victim, evId, vp.xy(), yawTowards(vp.xy(), peds[thief].pos.toVec3().xy()), 0, -1);
                        addCrimeIncident(*this, peds[thief].pos, thief);
                        setStage(e, ST_B);
                    } else if (e.t > 25.f) {
                        tb.type = BRAIN_WANDER;
                        tb.edge = -1;
                        over = true;
                    }
                } else if (e.stage == ST_B) {
                    if (victim >= 0 && calmActor(*this, victim)) {
                        PedAI& va = pedAI(victim);
                        if (thief >= 0) va.anchorYaw = yawTowards(peds[victim].pos.toVec3().xy(), peds[thief].pos.toVec3().xy());
                        if (e.t > 0.6f && e.flag == 0) {
                            peds[victim].pendingAction = Anim::CLIP_POINT;
                            aiSay(victim, BK_HELP, 1.f, plDist < 30.f);
                            e.flag = 1;
                        }
                        if (e.t > 4.f) {
                            va.activity = ACT_CALL_POLICE;
                            va.threatPos = thief >= 0 ? peds[thief].pos.toVec3().xy() : e.pos.xy();
                            va.actTimer = 12.f;
                        }
                    }
                    if (thief >= 0 && tDown) {
                        e.pickupPos = peds[thief].pos;
                        e.pickup = dropMoney(*this, e.pickupPos, e.amount);
                        e.pickupPos = pickups[e.pickup].pos;
                        if (victim >= 0 && !isDown(peds[victim])) {
                            peds[victim].brain.type = BRAIN_WANDER;
                            setActor(*this, victim, evId, e.pickupPos.toVec3().xy(), 0.f, 0, -1);
                        }
                        setStage(e, ST_D);
                    } else if (e.t > 40.f || thief < 0) {
                        over = true;
                    }
                } else if (e.stage == ST_D) {
                    if (!wallet(*this, e, victim, evId, dt)) over = true;
                }
                break;
            }
            // stages: A arguing, B fist fight, C back to the cars and drive off
            case EV_CRASH: {
                int da = livePed(*this, e.ped[0]), db = livePed(*this, e.ped[1]);
                int ca = liveVeh(*this, e.veh[0]), cb = liveVeh(*this, e.veh[1]);
                if (ca < 0 || cb < 0 || (da < 0 && db < 0)) {
                    over = true;
                    break;
                }
                if (e.stage == ST_A && e.flag == 2) {
                    // a real knock between two cars in traffic: both stop with the hazards on, then the drivers get
                    // out and meet between the cars on the curb side
                    for (int c : {ca, cb}) {
                        vehicles[c].ctl = Vehicles::VehicleControls();
                        vehicles[c].ctl.brake = 1.f;
                        vehicles[c].ctl.handbrake = true;
                        vehicles[c].indicator = 2;
                    }
                    bool inA = da >= 0 && peds[da].state == PS_INVEHICLE && peds[da].vehicle == ca;
                    bool inB = db >= 0 && peds[db].state == PS_INVEHICLE && peds[db].vehicle == cb;
                    if (!inA || !inB) {   // someone drove off or was pulled out
                        for (int k = 0; k < 2; k++) {
                            int me = k == 0 ? da : db;
                            if (me >= 0 && peds[me].state == PS_INVEHICLE && peds[me].brain.type == BRAIN_NONE) peds[me].brain.type = BRAIN_DRIVER;
                        }
                        over = true;
                        break;
                    }
                    if (e.t > 2.2f && vehicles[ca].sim.speed() < 0.5f && vehicles[cb].sim.speed() < 0.5f) {
                        vec2 pa2 = vehicles[ca].sim.body.pos.toVec3().xy(), pb2 = vehicles[cb].sim.body.pos.toVec3().xy();
                        vec2 dir = length2(pb2 - pa2) > 1e-4f ? normalize(pb2 - pa2) : e.dir;
                        float hw = Max(vassets[vehicles[ca].model].spec.boxHalf.x, vassets[vehicles[cb].model].spec.boxHalf.x);
                        vec2 side = AI::rightOf(e.dir);
                        vec2 mid = (pa2 + pb2) * 0.5f + side * (hw + 1.1f);
                        vec2 sa = mid - dir * 0.8f, sb = mid + dir * 0.8f;
                        for (int k = 0; k < 2; k++) {
                            int me = k == 0 ? da : db, car = k == 0 ? ca : cb;
                            vec2 st = k == 0 ? sa : sb;
                            removePedFromVehicle(me, true);
                            vehicles[car].parked = true;
                            setActor(*this, me, evId, st, yawTowards(st, k == 0 ? sb : sa), 7, -1);
                            aiSay(me, BK_CRASH, 0.8f, plDist < 25.f);
                        }
                        e.flag = (int)(hash32((u32)(e.age * 100.f) + (u32)ca * 31u) % 5u == 0u);   // one in five comes to blows
                        e.barkT = 2.5f;
                        setStage(e, ST_A);
                    }
                    break;
                }
                if (e.stage == ST_A) {
                    bool calmA = calmActor(*this, da), calmB = calmActor(*this, db);
                    if (!calmA || !calmB) {
                        setStage(e, ST_C);
                        break;
                    }
                    if (e.barkT <= 0.f) {
                        e.barkT = 3.5f + hashToFloat(hash32((u32)(e.age * 10.f) + slot)) * 2.5f;
                        int speaker = ((int)(e.age / 4.f) & 1) ? da : db;
                        aiSay(speaker, BK_ARGUE, 0.9f, plDist < 20.f);
                        if (hash32((u32)(e.age * 7.f)) % 2 == 0) peds[speaker].pendingAction = Anim::CLIP_POINT;
                    }
                    if (e.flag == 1 && e.t > 12.f) {
                        // it escalates
                        for (int k = 0; k < 2; k++) {
                            int me = k == 0 ? da : db, other = k == 0 ? db : da;
                            pedAI(me).activity = ACT_WALK;
                            peds[me].brain.type = BRAIN_COMBAT;
                            peds[me].brain.target = other;
                            peds[me].brain.timer = 0.f;
                        }
                        aiStimulus(dvec3(e.pos), STIM_FIGHT, da, 22.f, false);
                        setStage(e, ST_B);
                    } else if (e.t > 30.f) {
                        setStage(e, ST_C);
                    }
                } else if (e.stage == ST_B) {
                    bool aDown = da < 0 || isDown(peds[da]), bDown = db < 0 || isDown(peds[db]);
                    if (aDown || bDown || e.t > 14.f) {
                        for (int me : {da, db})
                            if (me >= 0 && !isDown(peds[me]) && peds[me].brain.type == BRAIN_COMBAT) {
                                peds[me].brain.type = BRAIN_WANDER;
                                peds[me].brain.target = -1;
                                peds[me].brain.edge = -1;
                            }
                        setStage(e, ST_C);
                    }
                } else if (e.stage == ST_C) {
                    // back to the cars (unless scared away or the car is taken)
                    int drivingAway = 0;
                    for (int k = 0; k < 2; k++) {
                        int me = k == 0 ? da : db, car = k == 0 ? ca : cb;
                        if (me < 0 || isDown(peds[me])) {
                            drivingAway++;
                            continue;
                        }
                        Ped& p = peds[me];
                        if (p.state == PS_INVEHICLE) {
                            drivingAway++;
                            continue;
                        }
                        Vehicle& v = vehicles[car];
                        if (v.seats[0] >= 0 || v.sim.wrecked || v.exploded || (p.brain.type != BRAIN_WANDER && p.brain.type != BRAIN_GOTO)) {
                            drivingAway++;
                            continue;
                        }
                        const Vehicles::VehicleModel& spec = vassets[v.model].spec;
                        vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(-(spec.boxHalf.x + 0.6f), 0.3f, 0.f));
                        vec2 toD = door.xy() - p.pos.toVec3().xy();
                        if (length(toD) < 1.1f || e.t > 25.f) {
                            warpPedIntoVehicle(me, car, 0);
                            p.brain.type = BRAIN_DRIVER;
                            v.parked = false;
                            v.sim.engineOn = true;
                            pedAI(me).activity = ACT_WALK;
                            attachTraffic(car);
                            drivingAway++;
                        } else {
                            // round the nearer free end of the car when coming from the curb side, then to the door
                            vec3 goal = door;
                            vec2 cc = v.sim.body.pos.toVec3().xy(), cf = v.sim.forward().xy();
                            cf = length2(cf) > 1e-6f ? normalize(cf) : vec2(0, 1);
                            vec2 lp = p.pos.toVec3().xy() - cc;
                            float lx = dot(lp, AI::rightOf(cf)), ly = dot(lp, cf);
                            if (lx > spec.boxHalf.x * 0.5f) {   // (the driver's door is on the left)
                                float endSign = aiCarEndToWalkRound(*this, car, ly >= 0.f ? 1.f : -1.f);
                                float side = fabsf(ly) < spec.boxHalf.y + 0.6f || ly * endSign < 0.f ? 1.f : -1.f;
                                vec2 wp = cc + cf * (endSign * (spec.boxHalf.y + 0.8f)) + AI::rightOf(cf) * (side * (spec.boxHalf.x + 0.6f));
                                goal = vec3(wp, door.z);
                            }
                            pedAI(me).activity = ACT_WALK;
                            p.brain.type = BRAIN_GOTO;
                            p.brain.goal = dvec3(goal);
                            p.brain.speed = 1.5f;
                        }
                    }
                    if (drivingAway >= 2 || e.t > 40.f) over = true;
                }
                break;
            }
            // stage A: racing to a far destination
            case EV_RACERS: {
                int alive = 0;
                for (int k = 0; k < 2; k++) {
                    int v = liveVeh(*this, e.veh[k]);
                    if (v < 0) continue;
                    int drv = vehicles[v].seats[0];
                    if (drv < 0 || peds[drv].isPlayer) continue;
                    alive++;
                    float d = length(rel(vehicles[v].sim.body.pos, pl->pos));
                    if (d < 22.f && e.barkT <= 0.f && vehicles[v].sim.speed() > 15.f) {
                        aiSay(drv, BK_RACE, 0.8f);
                        e.barkT = 6.f;
                    }
                    AI::Driver* dr = traffic.get(v);
                    if (dr && dr->mode == AI::DM_NORMAL && !dr->hasDest && e.t < 90.f) {
                        vec2 dest = vehicles[v].sim.body.pos.toVec3().xy() + normalize(vehicles[v].sim.forward().xy() + vec2(1e-4f, 0.f)) * 900.f;
                        traffic.setDestination(*dr, dest);
                        dr->mode = AI::DM_ROUTE;
                    }
                }
                if (alive == 0 || e.t > 110.f || plDist > keepR) over = true;
                break;
            }
            // stage A: vehicle chase, B on foot / arrested
            case EV_CHASE: {
                int crook = livePed(*this, e.ped[0]);
                int copCar = liveVeh(*this, e.veh[1]);
                if (crook < 0 || peds[crook].health <= 0.f) {
                    over = e.t > 8.f || crook < 0;
                    if (e.stage != ST_C) setStage(e, ST_C);
                    break;
                }
                Ped& c = peds[crook];
                if (e.stage == ST_A) {
                    int cv = c.vehicle;
                    // the getaway car is stuck or wrecked: run for it
                    if (cv >= 0) {
                        Vehicle& v = vehicles[cv];
                        if (v.sim.speed() < 1.5f) e.hold += dt;
                        else e.hold = Max(0.f, e.hold - dt);
                        if (e.hold > 4.f || v.sim.wrecked || v.sim.health < 250.f) {
                            removePedFromVehicle(crook, true);
                            traffic.detach(cv);
                            vehAI(cv).managed = false;
                        }
                    }
                    if (c.state == PS_ONFOOT) {
                        c.brain.type = c.weapon == WPN_PISTOL && (e.age > 50.f || hashToFloat(hash32(c.uid)) < 0.35f) ? BRAIN_COMBAT : BRAIN_FLEE;
                        int nearestCop = -1;
                        float bd = 1e9f;
                        for (int k = 1; k < e.np; k++) {
                            int o = livePed(*this, e.ped[k]);
                            if (o < 0 || peds[o].health <= 0.f) continue;
                            float d = length(rel(peds[o].pos, c.pos));
                            if (d < bd) {
                                bd = d;
                                nearestCop = o;
                            }
                        }
                        c.brain.target = nearestCop;
                        c.brain.timer = 0.f;
                        pedAI(crook).activity = ACT_WALK;
                        aiSay(crook, BK_FLEE, 0.8f);
                        setStage(e, ST_B);
                    }
                    if (e.t > 150.f) over = true;
                } else if (e.stage == ST_B) {
                    if (c.brain.type == BRAIN_COWER || c.vehicle >= 0) setStage(e, ST_C);   // arrested (police.cpp) / taken away
                    if (e.t > 90.f) over = true;
                } else {
                    // arrested: put in the back of the cruiser once an officer is next to them
                    if (c.state == PS_ONFOOT && c.brain.type == BRAIN_COWER && copCar >= 0 && e.t > 6.f && vehicles[copCar].sim.speed() < 1.f) {
                        int seat = freeSeat(copCar, false);
                        if (seat > 0 && length(rel(c.pos, vehicles[copCar].sim.body.pos)) < 25.f) {
                            warpPedIntoVehicle(crook, copCar, seat);
                            c.brain.type = BRAIN_PASSENGER;
                        }
                    }
                    if (e.t > 45.f) over = true;
                }
                if (plDist > keepR) over = true;
                break;
            }
            // stage A: taunting, B shooting
            case EV_SHOOTOUT: {
                int alive[2] = {0, 0};
                for (int k = 0; k < e.np; k++) {
                    int id = livePed(*this, e.ped[k]);
                    if (id < 0 || isDown(peds[id]) || peds[id].brain.type == BRAIN_FLEE) continue;
                    alive[peds[id].faction == FAC_GANG_SAINTS ? 1 : 0]++;
                }
                if (e.stage == ST_A) {
                    if (e.barkT <= 0.f) {
                        e.barkT = 1.5f;
                        int id = livePed(*this, e.ped[(int)(e.t * 2.f) % e.np]);
                        if (id >= 0) {
                            aiSay(id, BK_GANG_TAUNT, 1.f, plDist < 40.f);
                            peds[id].pendingAction = Anim::CLIP_POINT;
                        }
                    }
                    if (e.t > 4.f || plDist < 30.f) setStage(e, ST_B);
                } else {
                    // (re)target: everyone shoots at the nearest living rival
                    if (e.fxT <= 0.f) {
                        e.fxT = 1.f;
                        for (int k = 0; k < e.np; k++) {
                            int id = livePed(*this, e.ped[k]);
                            if (id < 0 || isDown(peds[id]) || peds[id].brain.type == BRAIN_FLEE) continue;
                            Ped& p = peds[id];
                            bool tOk = p.brain.type == BRAIN_COMBAT && p.target_is_valid(*this) &&
                                       (peds[p.brain.target].faction != p.faction || p.brain.target == player);
                            if (tOk) continue;
                            int best = -1;
                            float bd = 1e9f;
                            for (int j = 0; j < e.np; j++) {
                                int o = livePed(*this, e.ped[j]);
                                if (o < 0 || isDown(peds[o]) || peds[o].faction == p.faction) continue;
                                float d = length(rel(peds[o].pos, p.pos));
                                if (d < bd) {
                                    bd = d;
                                    best = o;
                                }
                            }
                            if (best >= 0) {
                                pedAI(id).activity = ACT_WALK;
                                p.brain.type = BRAIN_COMBAT;
                                p.brain.target = best;
                                p.brain.timer = 0.f;
                            }
                        }
                        // the first shots bring the police
                        if (e.flag == 0 && e.t > 3.f) {
                            int perp = livePed(*this, e.ped[0]);
                            if (perp >= 0) addCrimeIncident(*this, peds[perp].pos, perp);
                            e.flag = 1;
                        }
                    }
                    if (alive[0] == 0 || alive[1] == 0 || e.t > 70.f) {
                        // survivors scatter
                        for (int k = 0; k < e.np; k++) {
                            int id = livePed(*this, e.ped[k]);
                            if (id < 0 || isDown(peds[id]) || peds[id].brain.target == player) continue;
                            peds[id].brain.type = BRAIN_FLEE;
                            peds[id].brain.goal = dvec3(e.pos);
                            peds[id].brain.target = -1;
                            peds[id].brain.timer = 6.f;
                        }
                        over = true;
                    }
                }
                break;
            }
            // stage A: stumbling along
            case EV_DRUNK: {
                int id = livePed(*this, e.ped[0]);
                if (id < 0 || peds[id].health <= 0.f) {
                    over = true;
                    break;
                }
                Ped& p = peds[id];
                if (e.fxT <= 0.f && p.state == PS_ONFOOT && p.brain.type == BRAIN_WANDER) {
                    u32 hh = hash32(p.uid + (u32)(e.age * 3.f));
                    e.fxT = 10.f + hashToFloat(hh) * 16.f;
                    float r = hashToFloat(hash32(hh));
                    if (r < 0.22f && length(p.vel.xy()) > 0.4f) {
                        vec2 side = AI::rightOf(AI::yawDir(p.yaw)) * (hh & 1 ? 1.f : -1.f);
                        knockDown(id, vec3(side * 70.f + AI::yawDir(p.yaw) * 40.f, 10.f));   // takes a tumble
                    } else if (r < 0.6f) {
                        p.pendingAction = Anim::CLIP_STAGGER;
                    } else {
                        aiSay(id, BK_DRUNK, 0.8f);
                    }
                }
                if (e.age > 300.f || plDist > 170.f) over = true;
                break;
            }
            // stage A: performing (tips from the player)
            case EV_MUSICIAN: {
                int mus = livePed(*this, e.ped[0]);
                if (!calmActor(*this, mus)) {
                    over = true;
                    break;
                }
                Ped& m = peds[mus];
#ifdef HAVE_AUDIO
                if (e.music) Audio::setEmitter(e.music, m.pos.toVec3() + vec3(0, 0, 1.2f), vec3(0.f), (float)e.flag, 0.f, 0.f, 0.f, plDist < 60.f ? 0.8f : 0.f);
#endif
                if (e.fxT <= 0.f) {
                    e.fxT = 6.f + hashToFloat(hash32(m.uid + (u32)e.age)) * 6.f;
                    if (hash32((u32)(e.age * 13.f)) % 3 == 0) m.pendingAction = Anim::CLIP_CHEER;
                }
                // the player tips by standing next to the performer for a moment
                if (!e.done && pl->state == PS_ONFOOT && plDist < 2.6f && !pl->aiming && length(pl->vel.xy()) < 0.6f) {
                    e.hold += dt;
                    if (e.hold > 1.8f) {
                        if (pinfo.money >= 5) {
                            pinfo.money -= 5;
                            notify("Tipped the musician", "-$5");
                            aiSay(mus, BK_THANKS, 1.f, true);
                            m.pendingAction = Anim::CLIP_WAVE;
                        }
                        e.done = true;
                    }
                } else if (!e.done && plDist < 7.f && pl->state == PS_ONFOOT && e.hold == 0.f && !e.asked) {
                    help("Stand next to the musician to leave a $5 tip", 4.f);
                    e.asked = true;
                } else if (plDist > 3.5f) {
                    e.hold = 0.f;
                }
                // the audience disperses one by one after a while
                for (int k = 1; k < e.np; k++) {
                    int w = livePed(*this, e.ped[k]);
                    if (w < 0) continue;
                    if (e.age > 60.f + k * 25.f && pedAI(w).activity == ACT_WATCH) freeActor(*this, w);
                }
                if (e.age > 360.f || plDist > 170.f) over = true;
                break;
            }
            // stages: A posing for photos, B walking on as a group
            case EV_TOURISTS: {
                int ph = livePed(*this, e.ped[0]);
                if (e.stage == ST_A) {
                    if (!calmActor(*this, ph)) {
                        over = true;
                        break;
                    }
                    Ped& cam = peds[ph];
                    if (e.fxT <= 0.f) {
                        e.fxT = 4.f + hashToFloat(hash32(cam.uid + (u32)e.age)) * 3.f;
#ifdef HAVE_AUDIO
                        if (plDist < 25.f) Audio::play(Audio::SFX_CAMERA_SHUTTER, pedHeadPos(cam), 0.5f);
#endif
                        for (int k = 1; k < e.np; k++) {
                            int id = livePed(*this, e.ped[k]);
                            if (id < 0 || !calmActor(*this, id)) continue;
                            static const int kPose[3] = {Anim::CLIP_WAVE, Anim::CLIP_POINT, Anim::CLIP_CHEER};
                            peds[id].pendingAction = kPose[hash32(peds[id].uid + (u32)e.age) % 3];
                        }
                        if (plDist < 20.f) aiSay(livePed(*this, e.ped[1 + (int)e.age % Max(e.np - 1, 1)]), BK_TOURIST, 0.4f);
                    }
                    // they ask the player to take the picture
                    if (!e.done && pl->state == PS_ONFOOT && plDist < 4.5f && !pl->aiming) {
                        if (!e.asked) {
                            aiSay(ph, BK_TOURIST, 1.f, true);
                            help("Stand still near the tourists to take their photo", 4.f);
                            e.asked = true;
                        }
                        if (length(pl->vel.xy()) < 0.5f) e.hold += dt;
                        if (e.hold > 3.f) {
#ifdef HAVE_AUDIO
                            Audio::play2D(Audio::SFX_CAMERA_SHUTTER, 0.8f);
#endif
                            for (int k = 0; k < e.np; k++) {
                                int id = livePed(*this, e.ped[k]);
                                if (id >= 0 && calmActor(*this, id)) peds[id].pendingAction = Anim::CLIP_CHEER;
                            }
                            aiSay(ph, BK_THANKS, 1.f, true);
                            payReward(*this, e.amount, "Tourists tipped you");
                            e.done = true;
                            e.t = Max(e.t, 20.f);
                        }
                    } else {
                        e.hold = 0.f;
                    }
                    if (e.t > 28.f) {
                        // walk on together: the photographer leads
                        freeActor(*this, ph);
                        pedAI(ph).eventId = evId;
                        pedAI(ph).actTimer = 40.f;
                        for (int k = 1; k < e.np; k++) {
                            int id = livePed(*this, e.ped[k]);
                            if (id < 0 || !calmActor(*this, id)) continue;
                            freeActor(*this, id);
                            PedAI& fa = pedAI(id);
                            fa.eventId = evId;
                            fa.leader = ph;
                            fa.leaderUid = peds[ph].uid;
                            fa.slot = vec2(k % 2 ? 0.9f : -0.9f, -0.8f * ((k + 1) / 2));
                        }
                        setStage(e, ST_B);
                    }
                } else {
                    if (e.t > 8.f) over = true;
                }
                break;
            }
            // stages: A waving for help, B fixed and driving off
            case EV_BREAKDOWN: {
                int drv = livePed(*this, e.ped[0]);
                int car = liveVeh(*this, e.veh[0]);
                if (car < 0 || (vehicles[car].seats[0] >= 0 && (drv < 0 || vehicles[car].seats[0] != drv))) {
                    over = true;   // someone took the car
                    break;
                }
                Vehicle& v = vehicles[car];
                const Vehicles::VehicleModel& spec = vassets[v.model].spec;
                vec3 hood = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(0.f, spec.boxHalf.y * 0.75f, spec.boxHalf.z * 0.6f));
                if (e.stage == ST_A) {
                    if (!calmActor(*this, drv)) {
                        over = true;
                        break;
                    }
                    Ped& p = peds[drv];
                    // smoke from under the hood
                    if (e.fxT <= 0.f) {
                        e.fxT = 0.35f;
                        if (plDist < 120.f) spawnFx(FX_SMOKE, dvec3(hood) + dvec3(0, 0, 0.3), vec3(0, 0, 1), 1, 0.6f, vec3(0.8f));
                    }
                    // wave at passing traffic and the player
                    if (e.barkT <= 0.f) {
                        e.barkT = 4.f + hashToFloat(hash32(p.uid + (u32)e.age)) * 3.f;
                        p.pendingAction = Anim::CLIP_WAVE;
                        if (plDist < 18.f) aiSay(drv, BK_BREAKDOWN, 0.9f, true);
                    }
                    PedAI& da = pedAI(drv);
                    da.anchorYaw = plDist < 25.f ? yawTowards(p.pos.toVec3().xy(), pp) : AI::dirYaw(-e.dir);
                    // the player helps by working on the engine for a few seconds
                    float hoodD = length(pp - hood.xy());
                    if (!e.done && pl->state == PS_ONFOOT && hoodD < 2.4f && !pl->aiming) {
                        if (!e.asked) {
                            help("Stay by the hood to help fix the car", 4.f);
                            e.asked = true;
                        }
                        e.hold += dt;
                        if ((int)(e.hold * 3.f) != (int)((e.hold - dt) * 3.f)) spawnFx(FX_SPARKS, dvec3(hood), vec3(0, 0, 1), 3, 0.4f);
                        if (e.hold > 4.f) {
#ifdef HAVE_AUDIO
                            Audio::play(Audio::SFX_ENGINE_START, hood, 0.9f);
#endif
                            aiSay(drv, BK_THANKS, 1.f, true);
                            p.pendingAction = Anim::CLIP_CHEER;
                            payReward(*this, e.amount, "Helped a stranded driver");
                            e.done = true;
                            setStage(e, ST_B);
                        }
                    } else if (hoodD > 3.f) {
                        e.hold = Max(0.f, e.hold - dt);
                    }
                    if (e.age > 300.f) over = true;
                } else {
                    // get back in and drive away
                    if (drv < 0 || isDown(peds[drv])) {
                        over = true;
                        break;
                    }
                    Ped& p = peds[drv];
                    if (p.state == PS_INVEHICLE) {
                        if (e.t > 12.f) over = true;
                        break;
                    }
                    if (e.t < 2.f) break;   // cheering
                    vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(-(spec.boxHalf.x + 0.6f), 0.3f, 0.f));
                    vec2 toD = door.xy() - p.pos.toVec3().xy();
                    if (length(toD) < 1.2f || e.t > 20.f) {
                        warpPedIntoVehicle(drv, car, 0);
                        p.brain.type = BRAIN_DRIVER;
                        v.parked = false;
                        v.sim.engineOn = true;
                        pedAI(drv).activity = ACT_WALK;
                        attachTraffic(car);
                        vehAI(car).role = VR_TRAFFIC;
                    } else {
                        pedAI(drv).activity = ACT_WALK;
                        p.brain.type = BRAIN_GOTO;
                        p.brain.goal = dvec3(door);
                        p.brain.speed = 1.5f;
                    }
                }
                break;
            }
            case EV_TRAFFIC_STOP: {
                // A: running the plate in the cruiser, B: walking up to the window, C: words at the window,
                // D: back to the cruiser, E: both drive off
                int drv = livePed(*this, e.ped[0]), off = livePed(*this, e.ped[1]);
                int car = liveVeh(*this, e.veh[0]), cop = liveVeh(*this, e.veh[1]);
                if (car < 0 || cop < 0 || drv < 0 || off < 0 || isDown(peds[off]) || isDown(peds[drv]) || pinfo.wanted > 0) {
                    over = true;
                    break;
                }
                Vehicle& vc = vehicles[car];
                Vehicle& vp = vehicles[cop];
                if (e.stage < ST_E && (vc.seats[0] != drv || vp.sim.speed() > 1.f || vc.sim.speed() > 1.f)) {
                    over = true;   // somebody drove off / took a car
                    break;
                }
                const Vehicles::VehicleModel& sa = vassets[vc.model].spec;
                float side = !sa.seats.empty() && sa.seats[0].pos.x > 0.f ? 1.f : -1.f;
                vec2 window = (vc.sim.body.pos.toVec3() + rotate(vc.sim.body.rot, vec3(side * (sa.boxHalf.x + 0.6f), 0.4f, 0.f))).xy();
                const Vehicles::VehicleModel& sp = vassets[vp.model].spec;
                float sideP = !sp.seats.empty() && sp.seats[0].pos.x > 0.f ? 1.f : -1.f;
                vec2 copDoor = (vp.sim.body.pos.toVec3() + rotate(vp.sim.body.rot, vec3(sideP * (sp.boxHalf.x + 0.55f), 0.3f, 0.f))).xy();
                Ped& o = peds[off];
                vec2 opos = o.pos.toVec3().xy();
                if (e.stage == ST_A) {
                    if (e.t > 4.f + (float)(e.amount % 3)) {
                        removePedFromVehicle(off, true);
                        setActor(*this, off, evId, window, yawTowards(window, vc.sim.body.pos.toVec3().xy()), 0, -1);
                        setStage(e, ST_B);
                    }
                } else if (e.stage == ST_B) {
                    if (!calmActor(*this, off)) {
                        over = true;
                        break;
                    }
                    if (length(opos - window) < 0.8f || e.t > 25.f) {
                        pedAI(off).stance = 7;
                        e.barkT = 0.f;
                        e.flag = 0;
                        setStage(e, ST_C);
                    }
                } else if (e.stage == ST_C) {
                    if (!calmActor(*this, off)) {
                        over = true;
                        break;
                    }
                    if (e.barkT <= 0.f && plDist < 45.f) {
                        // alternate: officer, driver, officer...
                        bool officer = (e.flag & 1) == 0;
                        aiSay(officer ? off : drv, officer ? BK_TICKET : BK_TICKETED, 1.f, plDist < 20.f);
                        if (officer && o.pendingAction < 0 && (e.flag % 4) == 2) o.pendingAction = Anim::CLIP_POINT;
                        e.flag++;
                        e.barkT = 3.5f + hashToFloat(hash32(o.uid + (u32)e.flag)) * 2.5f;
                    }
                    if (e.t > 16.f + (float)e.amount) {
                        PedAI& oa = pedAI(off);
                        oa.anchor = copDoor;
                        oa.anchorYaw = yawTowards(copDoor, vp.sim.body.pos.toVec3().xy());
                        oa.stance = 0;
                        setStage(e, ST_D);
                    }
                } else if (e.stage == ST_D) {
                    if (length(opos - copDoor) < 1.2f || e.t > 20.f) {
                        pedAI(off).eventId = -1;
                        pedAI(drv).eventId = -1;
                        warpPedIntoVehicle(off, cop, 0);
                        o.brain.type = BRAIN_DRIVER;
                        peds[drv].brain.type = BRAIN_DRIVER;
                        vc.parked = vp.parked = false;
                        vp.sirenOn = false;
                        vp.sirenSilent = false;
                        attachTraffic(car);
                        attachTraffic(cop);
                        vehAI(car).role = VR_TRAFFIC;
                        vehAI(cop).role = VR_POLICE;
                        setStage(e, ST_E);
                    }
                } else if (e.t > 8.f) {
                    over = true;
                }
                break;
            }
            default: over = true; break;
        }
        if (plDist > keepR) over = true;
        if (over && e.type == EV_CRASH) {
            // nobody may stay frozen in a seat (a knock in traffic cut short), no hazards left blinking
            for (int k = 0; k < e.np; k++) {
                int id = livePed(*this, e.ped[k]);
                if (id >= 0 && peds[id].state == PS_INVEHICLE && peds[id].brain.type == BRAIN_NONE) peds[id].brain.type = BRAIN_DRIVER;
            }
            for (int k = 0; k < e.nv; k++) {
                int id = liveVeh(*this, e.veh[k]);
                if (id >= 0) {
                    if (vehicles[id].seats[0] >= 0) vehicles[id].parked = false;
                    if (vehicles[id].indicator == 2) vehicles[id].indicator = 0;
                }
            }
        }
        if (over && e.type == EV_TRAFFIC_STOP && e.stage < ST_E) {
            // cut short: nobody may stay frozen in a seat (brain NONE) or parked forever
            for (int k = 0; k < e.np; k++) {
                int id = livePed(*this, e.ped[k]);
                if (id >= 0 && peds[id].state == PS_INVEHICLE && peds[id].brain.type == BRAIN_NONE) peds[id].brain.type = BRAIN_DRIVER;
            }
            for (int k = 0; k < e.nv; k++) {
                int id = liveVeh(*this, e.veh[k]);
                if (id >= 0) {
                    vehicles[id].parked = vehicles[id].seats[0] < 0;
                    if (vehicles[id].indicator == 2) vehicles[id].indicator = 0;
                    if (vehicles[id].sirenSilent) vehicles[id].sirenOn = vehicles[id].sirenSilent = false;
                }
            }
        }
        if (over) releaseEvent(*this, e);
    }
}

}  // namespace Game
