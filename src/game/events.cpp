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
    EV_TAKEOVER,       // a street takeover: donuts in a crossing, cars across two approaches, a crowd filming and cheering;
                       // it breaks up when the police show (somebody always calls them) - the driver makes a run for it
    EV_BRAWL,          // a fight breaking out on the sidewalk: two men shouting in each other's faces (a friend trying to
                       // walk one away), mostly fists; the crowd backs off and films, somebody calls it in - the police
                       // come for the one who started it (run down, cuffed and taken away)
    EV_PROPOSAL,       // a proposal on the sidewalk: down on one knee with the ring, passers-by stopping to watch and film -
                       // mostly a yes (the hug, the crowd cheering, off hand in hand), now and then a no
    EV_PANHANDLER,     // somebody down on their luck sitting against a wall: up and over to the player walking by to ask for
                       // change - standing still by them gives $2 (the thanks), walking on is no hard feelings
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
    AI::DonutState donut;    // takeover: the driver doing donuts
    vec2 threat;             // takeover: what the crowd scatters from
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
    int ci = (f == FAC_GANG_CUERVOS || f == FAC_GANG_SAINTS) ? gangChar(g, seed, f) : wardrobeActorChar(g, seed, charRole, role, pos.xy());
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
    // (an officer on the way back to the car, walking a prisoner, taking a statement, stopping somebody or writing up a
    //  parked car carries on)
    if (p.brain.type == BRAIN_GOTO && !(p.faction == FAC_POLICE && p.brain.target <= -2 && p.brain.target >= -6)) {
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
        // (an officer the event had on foot - a fender bender's crew: back to the car, in and away)
        int hv = g.peds[id].faction == FAC_POLICE && id < (int)g.ai.ped.size() ? g.ai.ped[id].homeVeh : -1;
        if (hv >= 0 && hv < (int)g.vehicles.size() && g.vehicles[hv].used && !g.vehicles[hv].exploded && g.peds[id].brain.type == BRAIN_WANDER)
            police_scene::crewBack(g, id, hv);
    }
    for (int k = 0; k < e.nv; k++) {
        int id = liveVeh(g, e.veh[k]);
        if (id < 0) continue;
        police_scene::release(g, id);
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

// encounters.cpp (the missions agent's street encounters; compiled after this file): a staged encounter near p
bool encounterBusyNear(vec2 p, float radius);

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
    if (ai.forceEvent >= 0) gEv.timer = Min(gEv.timer, 2.f);
    int activeCount = 0;
    for (AmbientEvent& e : gEv.ev) activeCount += e.active;
    int maxActive = ai.forceEvent >= 0 && ai.forceEvent < EV_COUNT ? 3 : 2;   // (tests: a forced one even with two still playing out)
    if (gEv.timer <= 0.f && activeCount < maxActive && !populationOff) {
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
        w[EV_TAKEOVER] = !calmOnly && pinfo.wanted == 0 && urban ? ((tod > 19.5f || tod < 3.5f) ? 1.1f : 0.12f) : 0.f;
        w[EV_BRAWL] = !calmOnly && pinfo.wanted == 0 && urban ? (night ? (nightlife ? 1.4f : 0.6f) : 0.18f) : 0.f;
        w[EV_PROPOSAL] = (tod > 10.f && tod < 23.f) && pinfo.wanted == 0 && !missionActive() && (scenic || urban) ? (scenic ? 0.45f : 0.2f) : 0.f;
        w[EV_PANHANDLER] = (tod > 8.f && tod < 23.5f) && pinfo.wanted == 0 && !missionActive() && urban ? 0.5f : 0.f;
        for (int k = 0; k < EV_COUNT; k++) {
            if (time - gEv.lastOfType[k] < 150.0) w[k] = 0.f;
            for (AmbientEvent& e : gEv.ev)
                if (e.active && e.type == k) w[k] = 0.f;
        }
        // a street encounter staged around here (they come 90-250 m ahead of the player): no ambient event piled onto
        // the same streets this time round
        if (ai.forceEvent < 0 && encounterBusyNear(pp, 260.f))
            for (float& x : w) x = 0.f;
        if (ai.forceEvent >= 0 && ai.forceEvent < EV_COUNT) {   // (tests: that one, whatever the hour and the district)
            bool running = false;
            for (AmbientEvent& e : gEv.ev) running |= e.active && e.type == ai.forceEvent;
            for (int k = 0; k < EV_COUNT; k++) w[k] = k == ai.forceEvent && !running ? 1.f : 0.f;
        }
        if (ai.forceEvent >= EV_COUNT)
            for (float& x : w) x = 0.f;   // (a test that wants no other events running into its own)
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
            int why = 0;   // (a forced one that could not be staged: why the last try failed - logged for the tests)
            vec2 fwd = length(pvel) > 3.f ? normalize(pvel) : vec2(0);
            for (int attempt = 0; attempt < 6 && !ok; attempt++) {
                u32 ha = hash32(h + attempt * 7919u);
                switch (type) {
                    // ---------------------------------------------------------------- somebody asking for change
                    case EV_PANHANDLER: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 28.f, 55.f), 30.f, ws)) break;
                        vec3 spot = walkOffset(*this, ws, 0.f, ws.halfWidth * 0.85f);   // (against the building side)
                        if (!hiddenFrom(*this, spot, 22.f, warm)) break;
                        int id = spawnActor(*this, spot, AI::dirYaw(-ws.outward), ha, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        if (id < 0) break;
                        setActor(*this, id, evId, spot.xy(), AI::dirYaw(-ws.outward), 21, -1);   // (sitting on the ground)
                        peds[id].carry = CARRY_COFFEE;   // (a paper cup for the change)
                        e.ped[0] = refPed(*this, id);
                        e.np = 1;
                        e.pos = spot;
                        e.dir = -ws.outward;   // (facing the street)
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- a proposal
                    case EV_PROPOSAL: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 25.f, 60.f), 30.f, ws)) break;
                        if (ws.halfWidth < 1.3f) break;
                        vec3 apos = walkOffset(*this, ws, -0.7f, ws.halfWidth * 0.3f);
                        vec3 bpos = walkOffset(*this, ws, 0.7f, ws.halfWidth * 0.3f);
                        if (!hiddenFrom(*this, apos, 22.f, warm) || !hiddenFrom(*this, bpos, 22.f, warm)) break;
                        // (the one asking a man or a woman, the other mostly not the same: even seeds men, odd women)
                        bool aMan = (ha >> 13) & 1u, same = (ha >> 17) % 100u < 15u;
                        u32 sa = hash32(ha + 3u), sb = hash32(ha + 9u);
                        sa = aMan ? (sa & ~1u) : (sa | 1u);
                        sb = aMan != same ? (sb | 1u) : (sb & ~1u);
                        int a = spawnActor(*this, apos, 0.f, sa, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        int bb = a >= 0 ? spawnActor(*this, bpos, 0.f, sb, 0, FAC_CIVILIAN, PR_CIVILIAN, evId) : -1;
                        if (bb < 0) {
                            if (a >= 0) despawnPed(a);
                            break;
                        }
                        setActor(*this, a, evId, apos.xy(), yawTowards(apos.xy(), bpos.xy()), 7, -1);
                        setActor(*this, bb, evId, bpos.xy(), yawTowards(bpos.xy(), apos.xy()), 7, -1);
                        peds[a].yaw = yawTowards(apos.xy(), bpos.xy());
                        peds[bb].yaw = yawTowards(bpos.xy(), apos.xy());
                        e.ped[0] = refPed(*this, a);
                        e.ped[1] = refPed(*this, bb);
                        e.np = 2;
                        e.pos = (apos + bpos) * 0.5f;
                        e.dir = ws.t;
                        e.flag = ai.forceOutcome >= 0 ? (ai.forceOutcome ? 1 : 0) : ((ha >> 21) % 100u < 85u ? 1 : 0);   // (most say yes)
                        ai.forceOutcome = -1;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- a fight breaking out
                    case EV_BRAWL: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 40.f, 90.f), 30.f, ws)) break;
                        const AI::WalkLink& L = laneGraph.walkLinks[ws.link];
                        if (L.kind != AI::WL_SIDEWALK || L.length < 10.f) break;
                        vec3 apos = walkOffset(*this, ws, -0.62f, 0.f);
                        vec3 bpos = walkOffset(*this, ws, 0.62f, 0.f);
                        if (!hiddenFrom(*this, apos, 30.f, warm) || !hiddenFrom(*this, bpos, 30.f, warm)) break;
                        int a = spawnActor(*this, apos, 0.f, ha & ~1u, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);   // (even seeds: men)
                        int bb = a >= 0 ? spawnActor(*this, bpos, 0.f, hash32(ha + 3u) & ~1u, 0, FAC_CIVILIAN, PR_CIVILIAN, evId) : -1;
                        if (bb < 0) {
                            if (a >= 0) despawnPed(a);
                            break;
                        }
                        // a friend of the first, a step behind him, trying to walk him away
                        vec3 fpos = walkOffset(*this, ws, -1.5f, ws.halfWidth * 0.45f);
                        int fr = spawnActor(*this, fpos, 0.f, hash32(ha + 5u), 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        setActor(*this, a, evId, apos.xy(), yawTowards(apos.xy(), bpos.xy()), 7, -1);
                        setActor(*this, bb, evId, bpos.xy(), yawTowards(bpos.xy(), apos.xy()), 7, -1);
                        peds[a].yaw = yawTowards(apos.xy(), bpos.xy());
                        peds[bb].yaw = yawTowards(bpos.xy(), apos.xy());
                        pedAI(a).temper = 2;
                        pedAI(bb).temper = 2;
                        e.ped[0] = refPed(*this, a);
                        e.ped[1] = refPed(*this, bb);
                        e.np = 2;
                        if (fr >= 0) {
                            setActor(*this, fr, evId, fpos.xy(), yawTowards(fpos.xy(), apos.xy()), 0, -1);
                            peds[fr].yaw = yawTowards(fpos.xy(), apos.xy());
                            e.ped[2] = refPed(*this, fr);
                            e.np = 3;
                        }
                        e.pos = (apos + bpos) * 0.5f;
                        e.dir = ws.t;
                        e.flag = (ha >> 11) % 4u != 0u;   // three in four come to blows
                        e.barkT = 0.6f;
                        e.fxT = 3.f;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- mugging
                    case EV_MUGGING: {
                        WalkSpot ws;
                        why = 1;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 45.f, 95.f), 35.f, ws)) break;
                        vec3 vpos = walkOffset(*this, ws, 0.f, ws.halfWidth * 0.55f);
                        vec3 mpos = walkOffset(*this, ws, 0.3f, -ws.halfWidth * 0.35f);
                        why = 2;
                        if (!hiddenFrom(*this, vpos, 35.f, warm)) break;
                        why = 3;
                        int victim = spawnActor(*this, vpos, 0.f, ha, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        int mugger = victim >= 0 ? spawnActor(*this, mpos, 0.f, hash32(ha + 1u) | 0u, 0, FAC_CIVILIAN, PR_CIVILIAN, evId) : -1;
                        if (mugger < 0) {
                            if (victim >= 0) despawnPed(victim);
                            break;
                        }
                        why = 0;
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
                        // a patrol sent to it now and then (four in ten of the ones that stay words): pulled over behind,
                        // one officer takes the details, the other waves the traffic round (tests: ai.forceOutcome 1)
                        bool police = ai.forceOutcome >= 0 ? ai.forceOutcome == 1 : (e.flag == 0 && (ha >> 19) % 100u < 40u);
                        if (ai.forceOutcome == 1) e.flag = 0;
                        ai.forceOutcome = -1;
                        if (police) {
                            int unit = police_scene::callUnit(*this, pa3.xy() - ls.t * (hla + 8.f), ls.t);
                            if (unit >= 0) {
                                e.veh[2] = refVeh(*this, unit);
                                e.nv = 3;
                                e.amount = 1;
                            }
                        }
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
                    // ---------------------------------------------------------------- street takeover
                    case EV_TAKEOVER: {
                        // a proper crossing of two streets (signals or an all-way stop), a block or two away
                        vec2 probe = ringPoint(ha, pp, fwd, 120.f, 220.f);
                        int node = -1;
                        float bd = 110.f;
                        for (int n = 0; n < (int)laneGraph.nodes.size(); n++) {
                            const AI::NodeInfo& N = laneGraph.nodes[n];
                            if (N.approaches.size() != 4 || N.control == 0 || N.gradeSeparated) continue;
                            vec2 np = roads->nodes[n].p;
                            float dd = length(np - probe);
                            if (dd >= bd || length(np - pp) < 100.f) continue;
                            bool streets = true;
                            for (const AI::Approach& A : N.approaches) {
                                const World::RoadEdge& ed = roads->edges[A.edge];
                                if (ed.cls != World::RC_STREET && ed.cls != World::RC_AVENUE) streets = false;
                                if (A.inLanes.empty() || A.outLanes.empty()) streets = false;
                            }
                            if (!streets) continue;
                            bd = dd;
                            node = n;
                        }
                        if (node < 0) break;
                        const AI::NodeInfo& N = laneGraph.nodes[node];
                        vec2 c = roads->nodes[node].p;
                        float cz = groundHeight(c.x, c.y, roads->nodes[node].z + 3.f);
                        if (!hiddenFrom(*this, vec3(c, cz), 100.f, warm)) break;
                        std::vector<int> inBox;
                        vehiclesNear(c, 18.f, inBox);
                        if (!inBox.empty()) break;
                        int dm = findVehicleModel(Vehicles::VC_MUSCLE, ha);
                        if (dm < 0) dm = pickTrafficModel(*this, ha, true);
                        if (dm < 0) break;
                        float r0 = Max(roads->nodes[node].radius, 8.f);
                        // the star of the night, in the middle of the crossing
                        vec2 d0 = N.approaches[0].dir;
                        int car = spawnVehicle(dm, dvec3(c.x - d0.x * 1.5f, c.y - d0.y * 1.5f, cz + 0.4f), AI::dirYaw(AI::rightOf(d0)), true);
                        if (car < 0 || vehicles[car].seats[0] < 0) {
                            if (car >= 0) despawnVehicle(car, true);
                            break;
                        }
                        {
                            Vehicle& v = vehicles[car];
                            v.lightsOn = night;
                            const vec3 kSmoke[4] = {vec3(0.85f, 0.2f, 0.75f), vec3(0.25f, 0.7f, 1.f), vec3(1.f, 0.55f, 0.15f), vec3(0.95f)};
                            v.mods.smoke = kSmoke[(ha >> 4) % 4];   // coloured tire smoke
                            VehAI& vai = vehAI(car);
                            vai.role = VR_EVENT;
                            vai.eventId = evId;
                            int drv = v.seats[0];
                            peds[drv].brain.type = BRAIN_NONE;   // (the event drives: donuts)
                            pedAI(drv).eventId = evId;
                            e.ped[0] = refPed(*this, drv);
                            e.veh[0] = refVeh(*this, car);
                        }
                        e.np = 1;
                        e.nv = 1;
                        // two cars stopped across opposite approaches, hazards on: nobody gets through
                        for (int k = 0; k < 2; k++) {
                            const AI::Approach& A = N.approaches[k * 2];
                            vec2 in = -A.dir;   // travel direction arriving at the crossing
                            vec2 bp = c + A.dir * (r0 + 6.5f) + AI::rightOf(in) * 1.9f;
                            int bm = pickTrafficModel(*this, hash32(ha + 11u * (k + 1)), false);
                            if (bm < 0) continue;
                            float bz = groundHeight(bp.x, bp.y, cz + 3.f);
                            int bv = spawnVehicle(bm, dvec3(bp.x, bp.y, bz + 0.4f), AI::dirYaw(rotate(in, k ? 0.55f : -0.55f)), false);
                            if (bv < 0) continue;
                            Vehicle& v = vehicles[bv];
                            v.parked = true;
                            v.indicator = 2;
                            v.lightsOn = night;
                            VehAI& vai = vehAI(bv);
                            vai.role = VR_EVENT;
                            vai.eventId = evId;
                            e.veh[e.nv++] = refVeh(*this, bv);
                        }
                        // the crowd: in the mouths of the other two streets and on the corners, all eyes on the car
                        vec2 spots[7];
                        int ns = 0;
                        for (int k = 0; k < 2; k++) {
                            const AI::Approach& A = N.approaches[k * 2 + 1];
                            for (int s2 = -1; s2 <= 1; s2 += 2) spots[ns++] = c + A.dir * (r0 + 4.5f + (s2 > 0 ? 0.8f : 0.f)) + AI::rightOf(A.dir) * (1.6f * s2);
                        }
                        for (int k = 0; k < 3; k++) {
                            vec2 cd = normalize(N.approaches[k].dir + N.approaches[(k + 1) % 4].dir + vec2(1e-4f, 0.f));
                            spots[ns++] = c + cd * (r0 + 5.5f);
                        }
                        for (int k = 0; k < ns && e.np < 8; k++) {
                            vec2 sp = spots[k];
                            vec3 sp3(sp, groundHeight(sp.x, sp.y, cz + 3.f));
                            if (length(sp - pp) < 6.f || (World::gBuildings && World::gBuildings->pointInBuilding(sp, 0.5f))) continue;
                            u32 hk = hash32(ha * 31u + k * 977u);
                            int id = spawnActor(*this, sp3, 0.f, hk, (hk % 3 == 0) ? 4 : 0, FAC_CIVILIAN, PR_NIGHTLIFE, evId);
                            if (id < 0) continue;
                            // phones up filming, cheering, dancing to the music, pointing
                            const int kStance[5] = {8, 16, 9, 8, 17};
                            setActor(*this, id, evId, sp, yawTowards(sp, c), kStance[hk % 5], -1);
                            if (hk % 5 == 1) pedAI(id).activity = ACT_WATCH;   // (cheers now and then)
                            pedAI(id).clipTimer = 1.f + k;
                            e.ped[e.np++] = refPed(*this, id);
                        }
#ifdef HAVE_AUDIO
                        e.music = Audio::createEmitter(Audio::EMIT_RADIO_WORLD);
                        e.flag = 0;
                        for (int st = 0; st < Audio::radioStationCount(); st++) {
                            const char* genre = Audio::radioStationGenre(st);
                            if (genre && (strstr(genre, "Hip") || strstr(genre, "Rap") || strstr(genre, "Trap") || strstr(genre, "Reggaeton"))) {
                                e.flag = st;
                                break;
                            }
                        }
#endif
                        e.donut = AI::DonutState();
                        e.donut.dir = (ha & 1) ? 1 : -1;
                        e.donut.switchAt = 9.f + hashToFloat(hash32(ha + 5u)) * 6.f;
                        e.pos = vec3(c, cz);
                        e.dir = d0;
                        e.amount = ai.forceEvent == EV_TAKEOVER ? 25 : 55 + (int)(ha % 40u);   // until somebody's call brings the police (s)
                        e.hold = 0.f;
                        e.done = false;
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
                if (ai.forceEvent == type) LOG("events: staged the forced event %d at %.0f %.0f", type, e.pos.x, e.pos.y);
            } else {
                gEv.timer = 6.f;   // try again soon
                if (ai.forceEvent == type) LOG("events: the forced event %d not staged this time (last try: %d)", type, why);
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
                        // a bold passer-by close by gives chase now and then (a have-a-go hero: a tackle if he catches up)
                        if (e.np < 3 && hashToFloat(hash32(peds[thief].uid * 13u + 7u)) < 0.6f) {
                            int hero = -1;
                            float hd = 20.f;
                            std::vector<int> around;
                            pedsNear(vp.xy(), 20.f, around);
                            for (int i : around) {
                                if (i >= (int)ai.ped.size() || i == victim || i == thief) continue;
                                const Ped& q = peds[i];
                                const PedAI& qa = ai.ped[i];
                                if (q.isPlayer || q.persistent || q.female || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || q.brain.type != BRAIN_WANDER ||
                                    qa.uid != q.uid || qa.temper != 2 || qa.activity != ACT_WALK || qa.leader >= 0 || qa.eventId >= 0 || qa.role == PR_DRUNK)
                                    continue;
                                float dq = length(q.pos.toVec3().xy() - vp.xy());
                                if (dq < hd) {
                                    hd = dq;
                                    hero = i;
                                }
                            }
                            if (hero >= 0) {
                                e.ped[2] = refPed(*this, hero);
                                e.np = 3;
                                setActor(*this, hero, evId, peds[hero].pos.toVec3().xy(), peds[hero].yaw, 0, -1);
                                Brain& hb = peds[hero].brain;
                                hb.type = BRAIN_GOTO;
                                hb.goal = peds[thief].pos;
                                hb.speed = 5.8f;
                                hb.timer = 0.f;
                                aiSay(hero, BK_HERO, 1.f, true);
                                LOG("events: passer-by %d gives chase to purse snatcher %d (%.0f m)", hero, thief, hd);
                            }
                        }
                    } else if (e.t > 25.f) {
                        tb.type = BRAIN_WANDER;
                        tb.edge = -1;
                        over = true;
                    }
                } else if (e.stage == ST_B) {
                    // the have-a-go hero: after the thief, a tackle on catching up (the bag drops: below); out of breath
                    // and left behind after a while
                    int hero = e.np > 2 ? livePed(*this, e.ped[2]) : -1;
                    if (hero >= 0 && peds[hero].brain.type == BRAIN_GOTO && peds[hero].state == PS_ONFOOT) {
                        Brain& hb = peds[hero].brain;
                        vec2 hp = peds[hero].pos.toVec3().xy();
                        if (thief >= 0 && !tDown && hb.timer < 16.f) {
                            hb.goal = dvec3(peds[thief].pos.toVec3() + peds[thief].vel * 0.3f);
                            vec2 to = peds[thief].pos.toVec3().xy() - hp;
                            if (length(to) < 1.4f) {
                                knockDown(thief, vec3(normalize(to + vec2(1e-4f, 0.f)) * 200.f, 50.f), false);
                                setActor(*this, hero, evId, hp, yawTowards(hp, peds[thief].pos.toVec3().xy()), 0, -1);
                                aiSay(hero, BK_HERO, 1.f, true);
                                LOG("events: passer-by %d tackles purse snatcher %d", hero, thief);
                            }
                        } else {
                            // too fast for him (or it is over): on his way, still catching his breath
                            hb.type = BRAIN_WANDER;
                            hb.edge = -1;
                            PedAI& ha = pedAI(hero);
                            ha.activity = ACT_WALK;
                            ha.eventId = -1;
                            ha.navOk = false;
                            if (thief >= 0 && !tDown) aiSay(hero, BK_HERO_LOST, 1.f, true);
                            e.ped[2] = Ref();
                        }
                    }
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
                if (e.stage == ST_A && e.flag >= 2) {
                    // a real knock between two cars in traffic: both stop with the hazards on (flag 2), then the
                    // drivers get out and walk round their cars to the curb side between them (flag 3)
                    for (int c : {ca, cb}) {
                        if (vehicles[c].seats[0] >= 0) {
                            vehicles[c].ctl = Vehicles::VehicleControls();
                            vehicles[c].ctl.brake = 1.f;
                            vehicles[c].ctl.handbrake = true;
                        }
                        vehicles[c].indicator = 2;
                    }
                    vec2 pa2 = vehicles[ca].sim.body.pos.toVec3().xy(), pb2 = vehicles[cb].sim.body.pos.toVec3().xy();
                    vec2 dir = length2(pb2 - pa2) > 1e-4f ? normalize(pb2 - pa2) : e.dir;
                    float hw = Max(vassets[vehicles[ca].model].spec.boxHalf.x, vassets[vehicles[cb].model].spec.boxHalf.x);
                    vec2 mid = (pa2 + pb2) * 0.5f + AI::rightOf(e.dir) * (hw + 1.1f);
                    vec2 spot[2] = {mid - dir * 0.8f, mid + dir * 0.8f};
                    if (e.flag == 2) {
                        bool inA = da >= 0 && peds[da].state == PS_INVEHICLE && peds[da].vehicle == ca;
                        bool inB = db >= 0 && peds[db].state == PS_INVEHICLE && peds[db].vehicle == cb;
                        if (!inA || !inB) {   // someone drove off or was pulled out
                            for (int me : {da, db})
                                if (me >= 0 && peds[me].state == PS_INVEHICLE && peds[me].brain.type == BRAIN_NONE) peds[me].brain.type = BRAIN_DRIVER;
                            over = true;
                            break;
                        }
                        if (e.t > 2.2f && vehicles[ca].sim.speed() < 0.5f && vehicles[cb].sim.speed() < 0.5f) {
                            for (int k = 0; k < 2; k++) {
                                int me = k == 0 ? da : db, car = k == 0 ? ca : cb;
                                removePedFromVehicle(me, true);
                                vehicles[car].parked = true;
                                setActor(*this, me, evId, spot[k], yawTowards(spot[k], spot[1 - k]), 7, -1);
                                aiSay(me, BK_CRASH, 0.8f, plDist < 25.f);
                            }
                            e.flag = 3;
                            e.t = 0.f;
                        }
                        break;
                    }
                    // flag 3: round the nearer free end of the own car when getting out on the road side
                    bool there = true;
                    for (int k = 0; k < 2; k++) {
                        int me = k == 0 ? da : db, car = k == 0 ? ca : cb;
                        if (me < 0 || !calmActor(*this, me)) continue;
                        const Vehicle& cv = vehicles[car];
                        const Vehicles::VehicleModel& cs = vassets[cv.model].spec;
                        vec2 cc = cv.sim.body.pos.toVec3().xy(), cf = cv.sim.forward().xy();
                        cf = length2(cf) > 1e-6f ? normalize(cf) : vec2(0, 1);
                        vec2 lp = peds[me].pos.toVec3().xy() - cc;
                        float lx = dot(lp, AI::rightOf(cf)), ly = dot(lp, cf);
                        float spotSide = dot(spot[k] - cc, AI::rightOf(cf)) >= 0.f ? 1.f : -1.f;
                        PedAI& ma = pedAI(me);
                        if ((lx >= 0.f ? 1.f : -1.f) != spotSide && fabsf(lx) > cs.boxHalf.x * 0.5f) {
                            float endSign = aiCarEndToWalkRound(*this, car, dot(spot[k] - cc, cf) >= 0.f ? 1.f : -1.f);
                            float side = fabsf(ly) < cs.boxHalf.y + 0.6f || ly * endSign < 0.f ? -spotSide : spotSide;
                            ma.anchor = cc + cf * (endSign * (cs.boxHalf.y + 0.8f)) + AI::rightOf(cf) * (side * (cs.boxHalf.x + 0.6f));
                            there = false;
                        } else {
                            ma.anchor = spot[k];
                            if (length(peds[me].pos.toVec3().xy() - spot[k]) > 0.6f) there = false;
                        }
                    }
                    if (there || e.t > 9.f) {
                        e.flag = (int)(hash32((u32)(e.age * 100.f) + (u32)ca * 31u) % 5u == 0u);   // one in five comes to blows
                        e.barkT = 1.f;
                        setStage(e, ST_A);
                    }
                    break;
                }
                if (e.stage == ST_A) {
                    bool calmA = calmActor(*this, da), calmB = calmActor(*this, db);
                    int unit = e.nv > 2 ? liveVeh(*this, e.veh[2]) : -1;
                    int o1 = e.amount == 2 && e.np > 2 ? livePed(*this, e.ped[2]) : -1, o2 = e.amount == 2 && e.np > 3 ? livePed(*this, e.ped[3]) : -1;
                    if (!calmA || !calmB) {
                        for (int o : {o1, o2}) police_scene::crewBack(*this, o, unit);
                        police_scene::release(*this, unit);
                        e.amount = 3;
                        setStage(e, ST_C);
                        break;
                    }
                    // the police (amount 1 on the way, 2 there, 3 gone): pulled over behind it - the crew out, one to the
                    // two drivers for the details (a few questions, then the device), the other at the back of the car
                    // waving the traffic round; done in under half a minute, back in and away, and the drivers too
                    if (e.amount == 1 && (unit < 0 || vehAI(unit).task != PT_SCENE)) e.amount = 0;   // (never came)
                    if (e.amount == 1 && vehAI(unit).transportState == 2) {
                        Vehicle& uv = vehicles[unit];
                        const Vehicles::VehicleModel& us = vassets[uv.model].spec;
                        vec2 uf = uv.sim.forward().xy();
                        uf = length2(uf) > 1e-6f ? normalize(uf) : e.dir;
                        vec2 up = uv.sim.body.pos.toVec3().xy();
                        int crew[2] = {uv.seats[0], -1};
                        for (int st = 1; st < 8 && crew[1] < 0; st++)
                            if (uv.seats[st] >= 0 && peds[uv.seats[st]].faction == FAC_POLICE) crew[1] = uv.seats[st];
                        vec2 talk = e.pos.xy() + AI::rightOf(e.dir) * 1.4f;                          // (on the sidewalk side)
                        vec2 wave = up - uf * (us.boxHalf.y + 2.6f) - AI::rightOf(uf) * 0.7f;       // (behind the car)
                        for (int k = 0; k < 2; k++) {
                            int o = crew[k];
                            if (o < 0 || e.np >= 8) continue;
                            removePedFromVehicle(o, true);
                            vec2 sp = k == 0 ? talk : wave;
                            setActor(*this, o, evId, sp, k == 0 ? yawTowards(sp, e.pos.xy()) : AI::dirYaw(-uf), k == 0 ? 7 : 15, -1);
                            pedAI(o).homeVeh = unit;
                            e.ped[e.np++] = refPed(*this, o);
                        }
                        uv.parked = true;
                        e.amount = 2;
                        e.hold = 0.f;
                        e.fxT = 4.f;
                        ai.stats.crashScenes++;
                        LOG("events: the police at the fender bender at %.0f %.0f (unit %d)", e.pos.x, e.pos.y, unit);
                        break;
                    }
                    if (e.amount == 2) {
                        e.hold += dt;
                        // (to their spots round the end of the patrol car, not through it: out of the driver's side to the
                        //  sidewalk, out of the passenger's to the back of it)
                        if (unit >= 0) {
                            const Vehicle& cv = vehicles[unit];
                            const Vehicles::VehicleModel& cs = vassets[cv.model].spec;
                            vec2 cc = cv.sim.body.pos.toVec3().xy(), cf = cv.sim.forward().xy();
                            cf = length2(cf) > 1e-6f ? normalize(cf) : e.dir;
                            vec2 cr = AI::rightOf(cf);
                            vec2 spots[2] = {e.pos.xy() + AI::rightOf(e.dir) * 1.4f, cc - cf * (cs.boxHalf.y + 2.6f) - cr * 0.7f};
                            for (int k = 0; k < 2; k++) {
                                int o = k == 0 ? o1 : o2;
                                if (o < 0 || !calmActor(*this, o)) continue;
                                vec2 lp = peds[o].pos.toVec3().xy() - cc, lt = spots[k] - cc;
                                float lx = dot(lp, cr), ly = dot(lp, cf), tx = dot(lt, cr), ty = dot(lt, cf);
                                vec2 goal = spots[k];
                                Phys::Collider box;   // (the car's footprint: is it in the way of the straight walk?)
                                box.kind = Phys::COL_BOX;
                                box.c = vec3(cc, 0.f);
                                box.ax = cr;
                                box.he = vec3(cs.boxHalf.x, cs.boxHalf.y, 1.f);
                                float tIn = 0.f;
                                if (ai_walkround::crosses(box, peds[o].pos.toVec3().xy(), spots[k], 0.35f, tIn)) {
                                    float endSign = aiCarEndToWalkRound(*this, unit, ty >= 0.f ? 1.f : -1.f);
                                    float side = fabsf(ly) < cs.boxHalf.y + 0.6f || ly * endSign < 0.f ? (lx >= 0.f ? 1.f : -1.f) : (tx >= 0.f ? 1.f : -1.f);
                                    goal = cc + cf * (endSign * (cs.boxHalf.y + 0.8f)) + cr * (side * (cs.boxHalf.x + 0.6f));
                                }
                                pedAI(o).anchor = goal;
                            }
                        }
                        if (o1 >= 0 && calmActor(*this, o1)) {
                            PedAI& qa = pedAI(o1);
                            bool device = e.hold > 5.f && e.hold < 15.f;
                            peds[o1].phoneBrowse = device;
                            qa.stance = device ? 0 : 7;
                            if ((e.hold >= 0.6f && e.hold - dt < 0.6f) || (e.hold >= 15.5f && e.hold - dt < 15.5f)) aiSay(o1, BK_COP_CRASH, 1.f, plDist < 25.f);
                            for (int me : {da, db}) {   // (the drivers turned to the officer, giving their side of it)
                                PedAI& ma = pedAI(me);
                                ma.anchor = e.pos.xy() + e.dir * (me == da ? -0.8f : 0.8f);   // (back from the photos)
                                ma.anchorYaw = yawTowards(ma.anchor, peds[o1].pos.toVec3().xy());
                                ma.stance = 7;
                            }
                        }
                        if (o2 >= 0 && calmActor(*this, o2) && e.fxT <= 0.f) {
                            e.fxT = 7.f + hashToFloat(hash32((u32)(e.age * 10.f))) * 4.f;
                            if (plDist < 35.f) aiSay(o2, BK_COP_WAVE, 1.f);
                        }
                        if (e.hold > 24.f) {
                            for (int o : {o1, o2}) police_scene::crewBack(*this, o, unit);
                            police_scene::release(*this, unit);
                            e.amount = 3;
                            setStage(e, ST_C);
                        }
                        break;   // (no arguing in front of the officer)
                    }
                    // (one that stays words: after a while the one hit walks round to the damage and photographs it,
                    //  then back - the other one doing the talking meanwhile)
                    bool photo = e.flag == 0 && e.t > 11.f && e.t < 19.f;
                    {
                        PedAI& qb = pedAI(db);
                        const Vehicle& vb = vehicles[cb];
                        vec2 bf = vb.sim.forward().xy();
                        bf = length2(bf) > 1e-6f ? normalize(bf) : e.dir;
                        const Vehicles::VehicleModel& bs = vassets[vb.model].spec;
                        vec2 bumper = vb.sim.body.pos.toVec3().xy() - bf * bs.boxHalf.y;   // (where the two cars met)
                        vec2 dmg = bumper + bf * 0.4f + AI::rightOf(bf) * (bs.boxHalf.x + 0.75f);   // (at its corner, kerb side)
                        vec2 home = e.pos.xy() + e.dir * 0.8f;
                        if (photo) {
                            qb.anchor = dmg;
                            qb.anchorYaw = yawTowards(dmg, bumper);
                            qb.stance = 8;   // (the phone up: a picture of it)
                        } else if (e.flag == 0 && e.t >= 19.f && e.t - dt < 19.f) {
                            qb.anchor = home;
                            qb.anchorYaw = yawTowards(home, e.pos.xy() - e.dir * 0.8f);
                            qb.stance = 7;
                        }
                    }
                    if (e.barkT <= 0.f) {
                        e.barkT = 3.5f + hashToFloat(hash32((u32)(e.age * 10.f) + slot)) * 2.5f;
                        int speaker = photo ? da : (((int)(e.age / 4.f) & 1) ? da : db);
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
                    } else if (e.t > 30.f && !(e.amount == 1 && e.t < 110.f) && e.amount != 2) {
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
                        if (p.state == PS_ENTERING) continue;   // (in through the door: ai.cpp ai_board - away once in)
                        Vehicle& v = vehicles[car];
                        if (v.seats[0] >= 0 || v.sim.wrecked || v.exploded || (p.brain.type != BRAIN_WANDER && p.brain.type != BRAIN_GOTO)) {
                            drivingAway++;
                            continue;
                        }
                        const Vehicles::VehicleModel& spec = vassets[v.model].spec;
                        vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(-(spec.boxHalf.x + 0.6f), 0.3f, 0.f));
                        vec2 toD = door.xy() - p.pos.toVec3().xy();
                        if (length(toD) < 1.1f && ai_board::beginThen(*this, me, car, 0, 1)) continue;   // (the door clip, then away)
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
            // A: sitting against the wall until the player walks by close on foot (no gun out, not wanted); B: up and over
            // to them; C: the ask - standing still next to them for a moment gives $2 (the thanks), walking off is no hard
            // feelings; D: back to the wall and down again (once per player pass; gone when the player is far)
            case EV_PANHANDLER: {
                int me = livePed(*this, e.ped[0]);
                if (!calmActor(*this, me)) {
                    over = true;
                    break;
                }
                Ped& m = peds[me];
                PedAI& ma = pedAI(me);
                vec2 mp = m.pos.toVec3().xy();
                float toPl = length(pp - mp);
                bool approachable = pl->state == PS_ONFOOT && pinfo.wanted == 0 && !pl->aiming && weaponInfo(pl->weapon).animKind == 0;
                // sat by the wall: now and then somebody walking past stops, drops something in the cup, a word (and
                // his thanks)
                if ((e.stage == ST_A || e.stage == ST_D) && e.fxT <= 0.f && length(mp - e.pos.xy()) < 0.5f) {
                    e.fxT = 2.f;
                    std::vector<int> close;
                    pedsNear(mp, 4.f, close);
                    for (int o : close) {
                        if (o == me || o >= (int)ai.ped.size()) continue;
                        Ped& q = peds[o];
                        PedAI& qa = ai.ped[o];
                        if (!q.used || q.isPlayer || qa.uid != q.uid || q.state != PS_ONFOOT || q.faction != FAC_CIVILIAN || qa.activity != ACT_WALK ||
                            qa.leader >= 0 || qa.eventId >= 0 || q.brain.type != BRAIN_WANDER || length(q.vel.xy()) < 0.5f)
                            continue;
                        if (hashToFloat(hash32(q.uid * 7u + (u32)(e.age * 0.5f))) > 0.22f) continue;
                        vec2 qp = q.pos.toVec3().xy(), off = qp - mp;
                        vec2 dirOut = length(off) > 1e-3f ? off / length(off) : e.dir;   // (in front of him)
                        qa.activity = ACT_SCENARIO;
                        qa.anchor = mp + dirOut * 0.95f;
                        qa.anchorYaw = yawTowards(qa.anchor, mp);
                        qa.stance = 0;
                        qa.clip = -1;
                        qa.actTimer = 3.2f;
                        qa.reachAt = m.pos.toVec3() + vec3(AI::yawDir(m.yaw) * 0.32f, 0.45f);   // (the cup)
                        qa.reachT = time + 2.4;
                        aiSay(o, BK_GIVE_CHANGE, 0.8f);
                        ma.replyAt = (float)time + 2.2f;
                        ma.replyTo = o;
                        ma.replyKind = BK_PANHANDLE_THANKS;
                        ai.stats.passerChange++;
                        e.fxT = 25.f + hashToFloat(hash32(q.uid + (u32)e.age)) * 25.f;
                        LOG("events: ped %d drops something in panhandler %d's cup", o, me);
                        break;
                    }
                }
                if (e.stage == ST_A) {
                    ma.anchor = e.pos.xy();
                    ma.stance = 21;
                    if (!e.done && approachable && toPl < 11.f && e.t > 2.f) {
                        ma.stance = 0;   // (up)
                        setStage(e, ST_B);
                        LOG("events: panhandler %d gets up for the player (%.1f m)", me, toPl);
                    }
                    if (plDist > 140.f || e.age > 400.f) over = true;
                } else if (e.stage == ST_B) {
                    // over to the player, a step short of them
                    vec2 to = pp - mp;
                    float d = length(to);
                    ma.stance = 0;
                    ma.anchor = d > 1.6f ? pp - to / Max(d, 1e-3f) * 1.4f : mp;
                    ma.anchorYaw = yawTowards(mp, pp);
                    if (d < 1.9f) {
                        aiSay(me, BK_PANHANDLE, 1.f, true);
                        if (!e.asked) {
                            help("Stand still next to him to give $2", 4.f);
                            e.asked = true;
                        }
                        e.hold = 0.f;
                        setStage(e, ST_C);
                    } else if (!approachable || d > 16.f || e.t > 14.f) {
                        setStage(e, ST_D);   // (the player walked on)
                        LOG("events: panhandler %d - the player is off before he gets there (%.1f m)", me, d);
                    }
                } else if (e.stage == ST_C) {
                    ma.anchor = mp;
                    ma.anchorYaw = yawTowards(mp, pp);
                    ma.stance = 7;
                    if (approachable && toPl < 2.6f && length(pl->vel.xy()) < 0.5f) e.hold += dt;
                    if (e.hold > 1.5f) {
                        if (pinfo.money >= 2) {
                            pinfo.money -= 2;
                            notify("Gave some change", "-$2");
                            aiSay(me, BK_PANHANDLE_THANKS, 1.f, true);
                            m.pendingAction = Anim::CLIP_WAVE;
                            ai.stats.panhandled++;
                            LOG("events: panhandler %d got $2 from the player", me);
                        } else {
                            aiSay(me, BK_PANHANDLE_NO, 1.f, true);
                            LOG("events: panhandler %d - the player has no change", me);
                        }
                        e.done = true;
                        setStage(e, ST_D);
                    } else if (toPl > 5.f || e.t > 9.f || !approachable) {
                        aiSay(me, BK_PANHANDLE_NO, 1.f, toPl < 15.f);
                        e.done = true;
                        setStage(e, ST_D);
                        LOG("events: panhandler %d - the player walked on (%.1f m)", me, toPl);
                    }
                } else {
                    // back to the wall, and down
                    ma.anchor = e.pos.xy();
                    ma.anchorYaw = AI::dirYaw(e.dir);
                    ma.stance = length(mp - e.pos.xy()) < 0.5f ? 21 : 0;
                    if (length(mp - e.pos.xy()) < 0.5f && e.t > 2.f) {
                        // (again once the player is well away and comes back)
                        if (toPl > 30.f) e.done = false;
                        if (!e.done && approachable && toPl < 11.f) setStage(e, ST_B);
                    }
                    if (plDist > 140.f || e.age > 400.f) over = true;
                }
                break;
            }
            // A: a couple standing talking, face to face (until the player comes along), B: a word, then down on one knee
            // with the ring held up - passers-by stop, phones out; C: the answer (yes: arms up, the crowd cheers; no: a
            // step back and away, the crowd winces, the one asking left kneeling); D: the hug; E: off together hand in hand
            // (or alone)
            case EV_PROPOSAL: {
                int a = livePed(*this, e.ped[0]), bb = livePed(*this, e.ped[1]);
                bool yes = e.flag == 1;
                // (anything else happening to either of them - a scare, a knock, a crime - and it is off)
                if (!calmActor(*this, a) || (!calmActor(*this, bb) && !(e.stage >= ST_C && !yes))) {
                    over = true;
                    break;
                }
                Ped& A = peds[a];
                vec2 ap = A.pos.toVec3().xy();
                vec2 bp = bb >= 0 ? peds[bb].pos.toVec3().xy() : ap + e.dir;
                vec2 mid = (ap + bp) * 0.5f;
                if (e.stage == ST_A) {
                    if (e.t > 150.f && plDist > 60.f) {
                        over = true;
                        break;
                    }
                    // (it starts with the player close by or looking this way)
                    if ((plDist < 32.f || (plDist < 70.f && inCameraView(e.pos + vec3(0.f, 0.f, 1.f), 1.f))) && e.t > 3.f) {
                        aiSay(a, BK_PROPOSE_ASK, 1.f, true);
                        setStage(e, ST_B);
                    }
                } else if (e.stage == ST_B) {
                    if (e.t >= 2.5f && !e.asked) {
                        // down on one knee; the people passing stop to watch, most with their phones out
                        e.asked = true;
                        pedAI(a).stance = 18;
                        std::vector<int> around;
                        pedsNear(mid, 20.f, around);
                        for (int id : around) {
                            if (e.np >= 7) break;
                            if (id == a || id == bb || id >= (int)ai.ped.size()) continue;
                            const Ped& q = peds[id];
                            const PedAI& qa = ai.ped[id];
                            if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || qa.uid != q.uid ||
                                q.brain.type != BRAIN_WANDER || qa.activity != ACT_WALK || qa.eventId >= 0 || qa.leader >= 0 || qa.homeVeh >= 0 ||
                                qa.role == PR_JOGGER || qa.greetWith >= 0 || qa.stmtWith >= 0 || qa.stopPed >= 0)
                                continue;
                            vec2 off = q.pos.toVec3().xy() - mid;
                            float d = length(off);
                            if (d < 2.f) continue;
                            vec2 spot = mid + off / d * Clamp(d, 3.5f, 6.f);
                            u32 hq = hash32(q.uid * 31u + 7u);
                            setActor(*this, id, evId, spot, yawTowards(spot, mid), hq % 3u == 0u ? 0 : 8, -1);   // (two in three filming)
                            e.ped[e.np++] = refPed(*this, id);
                        }
                    }
                    if (e.t >= 3.5f && e.t - dt < 3.5f) aiSay(a, BK_PROPOSE, 1.f, true);
                    if (e.t >= 5.6f && e.t - dt < 5.6f) aiSay(bb, BK_PROPOSED, 1.f, true);
                    if (e.t >= 3.f) {
                        // the ring held up to them (peds.cpp holds the hand there)
                        PedAI& qa = pedAI(a);
                        vec2 dir = normalize(bp - ap + vec2(1e-4f, 0.f));
                        qa.reachAt = vec3(ap + dir * 0.42f, (float)A.pos.z + 0.95f);
                        qa.reachT = time + 0.2;
                    }
                    if (e.t >= 9.f) {
                        // the answer
                        if (yes) {
                            aiSay(bb, BK_PROPOSE_YES, 1.f, true);
                            pedAI(bb).stance = 16;   // (arms up)
                        } else {
                            aiSay(bb, BK_PROPOSE_NO, 1.f, true);
                            freeActor(*this, bb);   // (and walks away)
                        }
                        for (int k = 2; k < e.np; k++) {
                            int w = livePed(*this, e.ped[k]);
                            if (w >= 0 && calmActor(*this, w) && pedAI(w).stance == 0) pedAI(w).stance = yes ? 16 : 14;
                        }
                        LOG("events: a proposal at %.0f %.0f - %s (%d watching)", mid.x, mid.y, yes ? "yes" : "no", e.np - 2);
                        setStage(e, ST_C);
                    }
                } else if (e.stage == ST_C) {
                    // the crowd: a cheer or a wince, from one or two of them
                    for (int k = 0; k < 2; k++) {
                        float at = 0.8f + k * 1.1f;
                        if (e.np > 2 && e.t >= at && e.t - dt < at) {
                            int w = livePed(*this, e.ped[2 + (k + (int)e.age) % (e.np - 2)]);
                            if (w >= 0) aiSay(w, yes ? BK_CROWD_AWW : BK_CROWD_OOH, 1.f, true);
                        }
                    }
                    if (yes && e.t >= 2.6f) {
                        // up, and the hug (a kiss on the cheek where a hug does not fit the two of them)
                        PedAI& qa = pedAI(a);
                        qa.stance = 0;
                        pedAI(bb).stance = 0;
                        int clip = -1;
                        if (A.charIndex >= 0 && peds[bb].charIndex >= 0) {
                            const Anim::CharacterDesc& da = chars[A.charIndex].desc;
                            const Anim::CharacterDesc& db = chars[peds[bb].charIndex].desc;
                            clip = Anim::greetingFits(Anim::CLIP_HUG, da, db) ? (int)Anim::CLIP_HUG
                                                                              : (Anim::greetingFits(Anim::CLIP_CHEEK_KISS, da, db) ? (int)Anim::CLIP_CHEEK_KISS : -1);
                        }
                        e.amount = clip;
                        e.hold = 0.f;
                        if (clip >= 0) pop_detail::greetBegin(*this, a, bb, clip);
                        setStage(e, clip >= 0 ? ST_D : ST_E);
                    } else if (!yes) {
                        if (e.t >= 4.f && e.t - dt < 4.f) {
                            aiSay(a, BK_PROPOSE_SAD, 1.f, true);
                            pedAI(a).stance = 0;   // (up again, slowly)
                        }
                        if (e.t >= 6.5f) setStage(e, ST_E);
                    }
                } else if (e.stage == ST_D) {
                    // stepping in for the hug; it starts on both together, then they hold still while it plays
                    if (e.hold <= 0.f && (pop_detail::greetReady(*this, a, bb) || e.t > 3.5f)) {
                        e.hold = Max(pop_detail::greetStart(*this, a, bb, e.amount), 0.5f);
                        e.fxT = e.hold;
                    } else if (e.hold > 0.f && e.fxT <= 0.f) {
                        setStage(e, ST_E);
                    }
                    if (e.t > 12.f) setStage(e, ST_E);
                } else {
                    // off: together, hand in hand (pedai.cpp: the companion beside the leader, a couple) - or the one
                    // left behind on their own; the people watching go their way
                    for (int k = 2; k < e.np; k++) freeActor(*this, livePed(*this, e.ped[k]));
                    freeActor(*this, a);
                    if (yes && bb >= 0) {
                        freeActor(*this, bb);
                        PedAI& qa = pedAI(a);
                        PedAI& qb = pedAI(bb);
                        qa.greetWith = qb.greetWith = -1;
                        qa.leader = bb;
                        qa.leaderUid = peds[bb].uid;
                        qa.slot = vec2((A.uid & 1u) ? 0.85f : -0.85f, 0.f);
                        qa.couple = 1;
                        qa.actTimer = 1e4f;
                        qb.actTimer = 60.f + hashToFloat(hash32(peds[bb].uid)) * 60.f;   // (a while before any stop)
                    }
                    over = true;
                }
                break;
            }
            // stage A: the shouting, in each other's faces (pointing, a friend trying to walk one away); B: the fight
            // (fists - the crowd backs off and films; somebody calls it in); C: over - everybody goes their way, and the
            // police, called, come looking for the one who threw the first punch
            case EV_BRAWL: {
                int da = livePed(*this, e.ped[0]), db = livePed(*this, e.ped[1]), fr = e.np > 2 ? livePed(*this, e.ped[2]) : -1;
                if (da < 0 || db < 0) {
                    over = true;
                    break;
                }
                if (e.stage == ST_A) {
                    if (!calmActor(*this, da) || !calmActor(*this, db)) {   // (scared off, or the player broke it up)
                        setStage(e, ST_C);
                        break;
                    }
                    // closing in on each other as it heats up
                    vec2 pa2 = peds[da].pos.toVec3().xy(), pb2 = peds[db].pos.toVec3().xy();
                    vec2 mid = (pa2 + pb2) * 0.5f, ax = normalize(pb2 - pa2 + vec2(1e-4f, 0.f));
                    float gap = Max(0.95f, 1.25f - e.t * 0.03f);
                    pedAI(da).anchor = mid - ax * (gap * 0.5f);
                    pedAI(db).anchor = mid + ax * (gap * 0.5f);
                    pedAI(da).anchorYaw = yawTowards(pa2, pb2);
                    pedAI(db).anchorYaw = yawTowards(pb2, pa2);
                    if (e.barkT <= 0.f) {
                        u32 hb = hash32((u32)(e.age * 10.f) + slot * 77u);
                        e.barkT = 2.f + hashToFloat(hb) * 1.6f;
                        int speaker = ((int)(e.age / 2.4f) & 1) ? da : db;
                        aiSay(speaker, BK_BRAWL, 0.9f, plDist < 25.f);
                        if (hb % 3u != 0u && peds[speaker].pendingAction < 0) peds[speaker].pendingAction = Anim::CLIP_POINT;
                    }
                    if (fr >= 0 && calmActor(*this, fr)) {
                        // the friend at the first one's shoulder, talking him down
                        vec2 side = AI::rightOf(ax);
                        pedAI(fr).anchor = pa2 - ax * 0.75f + side * 0.55f;
                        pedAI(fr).anchorYaw = yawTowards(pedAI(fr).anchor, pa2);
                        if (e.fxT <= 0.f) {
                            e.fxT = 4.f + hashToFloat(hash32((u32)(e.age * 3.f))) * 3.f;
                            aiSay(fr, BK_BRAWL_FRIEND, 0.8f, plDist < 25.f);
                        }
                    }
                    // the raised voices turn heads (and get the phones out)
                    if (e.t > 2.f && fmodf(e.t, 2.f) < dt) aiStimulus(dvec3(e.pos), STIM_FIGHT, da, 16.f, false);
                    if (e.flag && e.t > 10.f + hashToFloat(hash32((u32)slot + (u32)(int)e.pos.x)) * 4.f) {
                        // a shove, and it is on
                        for (int k = 0; k < 2; k++) {
                            int me = k == 0 ? da : db, other = k == 0 ? db : da;
                            PedAI& ma = pedAI(me);
                            ma.activity = ACT_WALK;
                            ma.stance = 0;
                            peds[me].brain.type = BRAIN_COMBAT;
                            peds[me].brain.target = other;
                            peds[me].brain.timer = 0.f;
                        }
                        peds[db].pendingAction = Anim::CLIP_STAGGER;
                        aiStimulus(dvec3(e.pos), STIM_FIGHT, da, 26.f, false);
                        setStage(e, ST_B);
                    } else if (!e.flag && e.t > 18.f) {
                        setStage(e, ST_C);   // (they think better of it)
                    }
                } else if (e.stage == ST_B) {
                    if (fr >= 0 && calmActor(*this, fr) && e.fxT <= 0.f) {
                        e.fxT = 3.f;
                        aiSay(fr, BK_BRAWL_FRIEND, 0.7f, plDist < 25.f);
                    }
                    // somebody calls it in: the police come for the one who started it
                    if (!e.asked && e.t > 4.f) {
                        addCrimeIncident(*this, peds[da].pos, da);
                        e.asked = true;
                    }
                    bool aDown = isDown(peds[da]), bDown = isDown(peds[db]);
                    if (aDown || bDown || e.t > 13.f) {
                        for (int me : {da, db})
                            if (!isDown(peds[me]) && peds[me].brain.type == BRAIN_COMBAT) {
                                peds[me].brain.type = BRAIN_WANDER;
                                peds[me].brain.target = -1;
                                peds[me].brain.edge = -1;
                                pedAI(me).navOk = false;
                            }
                        setStage(e, ST_C);
                    }
                } else {
                    // over: they go their ways (the friend with the first one)
                    over = true;
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
                        knockDown(id, vec3(side * 70.f + AI::yawDir(p.yaw) * 40.f, 10.f), true);   // takes a tumble
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
                        if (p.vehicle == car) vehAI(car).role = VR_TRAFFIC;   // (in through the door: ai_board drove it off)
                        if (e.t > 12.f) over = true;
                        break;
                    }
                    if (p.state == PS_ENTERING) break;   // (getting in through the door: ai.cpp ai_board)
                    if (e.t < 2.f) break;   // cheering
                    vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(-(spec.boxHalf.x + 0.6f), 0.3f, 0.f));
                    vec2 toD = door.xy() - p.pos.toVec3().xy();
                    if (length(toD) < 1.2f && e.t <= 20.f && ai_board::beginThen(*this, drv, car, 0, 1)) break;   // (the door clip, then away)
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
                    if ((e.flag & 1) == 1 && length(opos - window) < 1.2f) {
                        // the driver's turn: the officer listens with a hand on the roof by the window (peds.cpp holds it)
                        PedAI& oa = pedAI(off);
                        oa.reachAt = vc.sim.body.pos.toVec3() + rotate(vc.sim.body.rot, vec3(side * Max(sa.boxHalf.x - 0.22f, 0.3f) + sa.boxCenter.x, 0.25f + sa.boxCenter.y,
                                                                                         sa.boxCenter.z + sa.boxHalf.z - 0.03f));
                        oa.reachT = time + 0.2;
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
                    // (in through the cruiser's door - ai.cpp ai_board, the door clip - and once in, both drive off)
                    bool in = o.state == PS_INVEHICLE && o.vehicle == cop;
                    if (!in && o.state == PS_ENTERING) break;
                    if (!in && length(opos - copDoor) < 1.2f && e.t <= 20.f && ai_board::beginThen(*this, off, cop, 0, 2)) break;
                    if (in || length(opos - copDoor) < 1.2f || e.t > 20.f) {
                        pedAI(off).eventId = -1;
                        pedAI(drv).eventId = -1;
                        if (!in) warpPedIntoVehicle(off, cop, 0);
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
            // stages: A the show (donuts, music, the crowd filming and cheering; a call brings the police), B scatter
            // (the driver floors it away from the police, two of the crowd run for the blocking cars, the rest run
            // or walk off), over once the street has cleared
            case EV_TAKEOVER: {
                int car = liveVeh(*this, e.veh[0]);
                int drv = livePed(*this, e.ped[0]);
                vec2 c = e.pos.xy();
                if (car < 0) {
                    over = true;
                    break;
                }
                Vehicle& v = vehicles[car];
                bool driverIn = drv >= 0 && peds[drv].state == PS_INVEHICLE && peds[drv].vehicle == car && v.seats[0] == drv;
                bool night = env && (env->timeOfDay > 19.5f || env->timeOfDay < 6.f);
#ifdef HAVE_AUDIO
                if (e.music)
                    Audio::setEmitter(e.music, vec3(c, e.pos.z + 1.2f), vec3(0.f), (float)e.flag, 0.f, 0.f, 0.f, e.stage == ST_A && plDist < 90.f ? 0.9f : 0.f);
#endif
                if (e.stage == ST_A) {
                    if (!driverIn) {   // somebody pulled the driver out, or took the car
                        e.threat = pp;
                        setStage(e, ST_B);
                        break;
                    }
                    v.ctl = AI::donutControls(v.sim, c, dt, e.donut);
                    v.lightsOn = night;
                    // the car sliding their way: the crowd steps back out of its path (and stays back)
                    {
                        vec2 vp2 = v.sim.body.pos.toVec3().xy();
                        float reach = vassets[v.model].spec.boxHalf.y + 2.6f;
                        for (int k = 1; k < e.np; k++) {
                            int id = livePed(*this, e.ped[k]);
                            if (id < 0 || peds[id].state != PS_ONFOOT || peds[id].brain.type != BRAIN_WANDER) continue;
                            PedAI& ca = pedAI(id);
                            if (ca.activity != ACT_EVENT && ca.activity != ACT_WATCH) continue;
                            vec2 pp2 = peds[id].pos.toVec3().xy();
                            vec2 away = pp2 - vp2;
                            float dcar = length(away);
                            if (dcar > reach) continue;
                            vec2 out = normalize(pp2 - c + vec2(1e-4f, 0.f));
                            if (dot(out, away) < 0.f) out = normalize(away + vec2(1e-4f, 0.f));
                            ca.anchor = pp2 + out * (reach - dcar + 1.5f);
                            ca.anchorYaw = yawTowards(ca.anchor, c);
                        }
                    }
                    // the crowd: a cheer here, a point there, the odd shout
                    if (e.fxT <= 0.f && e.np > 1) {
                        u32 hc = hash32((u32)(e.age * 17.f) + 3u);
                        e.fxT = 0.8f + hashToFloat(hc) * 2.2f;
                        int id = livePed(*this, e.ped[1 + (int)(hc % (u32)(e.np - 1))]);
                        if (id >= 0 && peds[id].state == PS_ONFOOT && peds[id].brain.type == BRAIN_WANDER && peds[id].pendingAction < 0) {
                            peds[id].pendingAction = (hc >> 3) % 3 ? Anim::CLIP_CHEER : Anim::CLIP_POINT;
                            if (e.barkT <= 0.f && plDist < 60.f && e.donut.smoke > 0.4f) {
                                aiSay(id, BK_NICE_CAR, 0.6f);
                                e.barkT = 3.f + hashToFloat(hash32(hc)) * 4.f;
                            }
                        }
                    }
                    // somebody always calls it in
                    if (!e.asked && e.t > (float)e.amount) {
                        addCrimeIncident(*this, peds[drv].pos, drv);
                        e.asked = true;
                    }
                    // the player joins in: donuts of their own in the ring - the crowd loves it (once)
                    if (pv >= 0 && !e.done && plDist < 18.f) {
                        const Vehicle& mine = vehicles[pv];
                        bool spinning = fabsf(mine.sim.body.angVel.z) > 1.1f && mine.sim.speed() > 2.f;
                        e.hold = spinning ? e.hold + dt : Max(0.f, e.hold - dt * 0.5f);
                        if (e.hold > 3.f) {
                            e.done = true;
                            payReward(*this, 150 + (int)(hash32(v.uid) % 150u), "The crowd went wild");
                            for (int k = 1; k < e.np; k++) {
                                int id = livePed(*this, e.ped[k]);
                                if (id >= 0 && peds[id].brain.type == BRAIN_WANDER) peds[id].pendingAction = Anim::CLIP_CHEER;
                            }
                        }
                    }
                    // what breaks it up: a patrol car close by, the player wanted near here, a knock from the player's
                    // car, shots (the crowd runs), or it has simply gone on long enough
                    bool scatter = false;
                    vec2 threat = c + e.dir * 80.f;
                    std::vector<int> nearCars;
                    vehiclesNear(c, 85.f, nearCars);
                    for (int vi : nearCars)
                        if (vehicles[vi].faction == FAC_POLICE && vi != car) {
                            scatter = true;
                            threat = vehicles[vi].sim.body.pos.toVec3().xy();
                        }
                    if (pinfo.wanted > 0 && plDist < 120.f) {
                        scatter = true;
                        threat = pp;
                    }
                    if (pv >= 0 && v.sim.impactImpulse > 1500.f && length(vehicles[pv].sim.body.pos.toVec3().xy() - v.sim.body.pos.toVec3().xy()) < 9.f) {
                        scatter = true;
                        threat = pp;
                    }
                    int running = 0;
                    for (int k = 1; k < e.np; k++) {
                        int id = livePed(*this, e.ped[k]);
                        running += id >= 0 && (peds[id].brain.type == BRAIN_FLEE || peds[id].brain.type == BRAIN_COWER);
                    }
                    if (running >= 2) {
                        scatter = true;
                        threat = pp;
                    }
                    if (e.t > (float)e.amount + 75.f) scatter = true;
                    if (scatter) {
                        e.threat = threat;
                        setStage(e, ST_B);
                    }
                } else if (e.stage == ST_B) {
                    if (e.flag >= 0 && e.t <= dt * 1.5f) {
                        // the driver floors it down whichever street the car points at, away from the threat
                        if (driverIn) {
                            vec2 f = v.sim.forward().xy();
                            f = length2(f) > 1e-6f ? normalize(f) : e.dir;
                            int node = -1;
                            float bd = 1e9f;
                            for (int n = 0; n < (int)laneGraph.nodes.size() && node < 0; n++)
                                if (length(roads->nodes[n].p - c) < 1.f) node = n;
                            (void)bd;
                            int lane = -1;
                            if (node >= 0) {
                                float best = -2.f;
                                for (const AI::Approach& A : laneGraph.nodes[node].approaches) {
                                    if (A.outLanes.empty()) continue;
                                    float sc = dot(A.dir, f) - 0.6f * dot(A.dir, normalize(e.threat - c + vec2(1e-4f, 0.f)));
                                    if (sc > best) {
                                        best = sc;
                                        lane = A.outLanes[0];
                                    }
                                }
                            }
                            Ped& d = peds[drv];
                            d.brain.type = BRAIN_FLEE;
                            d.brain.target = -1;
                            d.brain.goal = dvec3(vec3(e.threat, e.pos.z));
                            d.brain.timer = 0.f;
                            pedAI(drv).eventId = -1;
                            v.parked = false;
                            traffic.detach(car);
                            if (lane >= 0) attachTraffic(car, lane, laneGraph.lanes[lane].u0 + 1.f);
                            VehAI& vai = vehAI(car);
                            vai.role = VR_TRAFFIC;
                            vai.eventId = -1;
                            if (plDist < 70.f) aiSay(drv, BK_FLEE, 0.8f);
                        }
                        // two of the crowd run for the blocking cars, the rest run (close to the police) or walk off
                        int runners = 0;
                        for (int k = 1; k < e.np; k++) {
                            int id = livePed(*this, e.ped[k]);
                            if (id < 0 || isDown(peds[id]) || peds[id].state != PS_ONFOOT) continue;
                            Ped& p = peds[id];
                            PedAI& pa = pedAI(id);
                            int bvi = runners + 1 < e.nv ? liveVeh(*this, e.veh[runners + 1]) : -1;
                            if (bvi >= 0 && vehicles[bvi].seats[0] < 0) {
                                const Vehicle& bv = vehicles[bvi];
                                vec2 bf = bv.sim.forward().xy();
                                vec2 door = bv.sim.body.pos.toVec3().xy() - AI::rightOf(normalize(bf + vec2(1e-4f, 0.f))) * (vassets[bv.model].spec.boxHalf.x + 0.6f);
                                pa.activity = ACT_EVENT;
                                pa.targetVeh = bvi;
                                pa.stance = 0;
                                p.brain.type = BRAIN_GOTO;
                                p.brain.goal = dvec3(vec3(door, groundHeight(door.x, door.y, e.pos.z + 2.f)));
                                p.brain.speed = 4.5f;   // (running)
                                p.brain.timer = 0.f;
                                runners++;
                                continue;
                            }
                            bool close = length(p.pos.toVec3().xy() - e.threat) < 70.f || (hash32(p.uid) & 1);
                            freeActor(*this, id);
                            if (close) {
                                p.brain.type = BRAIN_FLEE;
                                p.brain.target = -1;
                                p.brain.goal = dvec3(vec3(e.threat, e.pos.z));
                                p.brain.timer = 0.f;
                            }
                        }
                        e.flag = -1;   // (the music is off; scatter handled)
                    }
                    // the runners jump into the blocking cars and are gone too
                    int waiting = 0;
                    for (int k = 1; k < e.np; k++) {
                        int id = livePed(*this, e.ped[k]);
                        if (id < 0 || peds[id].state != PS_ONFOOT) continue;
                        PedAI& pa = pedAI(id);
                        int bvi = pa.targetVeh;
                        if (pa.eventId != evId || bvi < 0 || bvi >= (int)vehicles.size() || !vehicles[bvi].used || peds[id].brain.type != BRAIN_GOTO) continue;
                        waiting++;
                        if (length(peds[id].pos.toVec3().xy() - peds[id].brain.goal.toVec3().xy()) < 1.4f && vehicles[bvi].seats[0] < 0) {
                            warpPedIntoVehicle(id, bvi, 0);
                            Vehicle& bv = vehicles[bvi];
                            bv.parked = false;
                            bv.indicator = 0;
                            VehAI& vai = vehAI(bvi);
                            vai.role = VR_TRAFFIC;
                            vai.eventId = -1;
                            peds[id].brain.type = BRAIN_FLEE;
                            peds[id].brain.target = -1;
                            peds[id].brain.goal = dvec3(vec3(e.threat, e.pos.z));
                            peds[id].brain.timer = 0.f;
                            pa.eventId = -1;
                            pa.targetVeh = -1;
                            pa.activity = ACT_WALK;
                        } else if (peds[id].brain.timer > 12.f) {
                            freeActor(*this, id);   // could not get to it: just runs
                            pa.targetVeh = -1;
                        }
                    }
                    if ((waiting == 0 && e.t > 3.f) || e.t > 25.f) over = true;
                }
                break;
            }
            default: over = true; break;
        }
        if (plDist > keepR) over = true;
        if (over && e.type == EV_TAKEOVER) {
            // cut short (the player left, the car was taken): nobody frozen in a seat, no car left spinning or
            // blinking across the street - out of sight they go, in sight they drive off / stay parked
            for (int k = 0; k < e.nv; k++) {
                int id = liveVeh(*this, e.veh[k]);
                if (id < 0) continue;
                Vehicle& vv = vehicles[id];
                int d0 = vv.seats[0];
                bool seen = inCameraView(vv.sim.body.pos.toVec3(), 3.f) && length(vv.sim.body.pos.toVec3().xy() - pp) < 150.f;
                if (d0 >= 0 && peds[d0].brain.type == BRAIN_NONE) {
                    if (!seen && !peds[d0].isPlayer) {
                        despawnVehicle(id, true);
                        continue;
                    }
                    peds[d0].brain.type = BRAIN_DRIVER;
                    vv.parked = false;
                }
                if (vv.indicator == 2) vv.indicator = 0;
                if (d0 < 0 && !seen && vehAI(id).role == VR_EVENT) despawnVehicle(id, true);
            }
        }
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


std::string GameWorld::aiEventsText(int want, int* stage, vec3* pos) const {
    static const char* const kNames[EV_COUNT] = {"mugging", "purse", "crash", "racers", "chase", "shootout", "drunk", "musician",
                                                 "tourists", "breakdown", "traffic stop", "takeover", "brawl", "proposal", "panhandler"};
    std::string out;
    if (stage) *stage = -1;
    for (const AmbientEvent& e : gEv.ev) {
        if (!e.active) continue;
        if (e.type == want) {
            if (stage) *stage = e.stage;
            if (pos) *pos = e.pos;
        }
        out += StrFormat("[%s stage %d t %.1f at %.0f %.0f |", kNames[e.type], (int)e.stage, e.t, e.pos.x, e.pos.y);
        for (int k = 0; k < e.np && k < 5; k++) {
            int id = livePed(*this, e.ped[k]);
            if (id < 0) {
                out += " -";
                continue;
            }
            const Ped& q = peds[id];
            out += StrFormat(" %d:s%d b%d a%d", id, (int)q.state, (int)q.brain.type, id < (int)ai.ped.size() ? (int)ai.ped[id].activity : -1);
        }
        out += StrFormat(" | asked %d done %d] ", (int)e.asked, (int)e.done);
    }
    return out.empty() ? std::string("events: none") : out;
}

// what drivers going past slow down to look at (ai.cpp ai_sights, traffic.cpp): a fender bender with the drivers out, a
// breakdown with the hood up, a fight, a traffic stop, a street takeover
namespace ev_sights {
void collect(const GameWorld& g, std::vector<vec3>& out) {
    (void)g;
    for (const AmbientEvent& e : gEv.ev) {
        if (!e.active) continue;
        if (e.type == EV_CRASH || e.type == EV_BREAKDOWN || e.type == EV_BRAWL || e.type == EV_TRAFFIC_STOP || e.type == EV_TAKEOVER) out.push_back(e.pos);
    }
}
}  // namespace ev_sights

std::string GameWorld::aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const {
    for (const AmbientEvent& e : gEv.ev) {
        if (!e.active || e.type != EV_BRAWL) continue;
        int a = livePed(*this, e.ped[0]), b = livePed(*this, e.ped[1]), f = e.np > 2 ? livePed(*this, e.ped[2]) : -1;
        if (stage) *stage = e.stage;
        if (pos) *pos = e.pos;
        if (starter && a >= 0) {
            *starter = a;
            if (starterUid) *starterUid = peds[a].uid;
        }
        return StrFormat("brawl stage %d t %.1f at %.0f %.0f | a %d (brain %d) b %d (brain %d) friend %d | police called %d", (int)e.stage, e.t, e.pos.x, e.pos.y,
                         a, a >= 0 ? (int)peds[a].brain.type : -1, b, b >= 0 ? (int)peds[b].brain.type : -1, f, (int)e.asked);
    }
    if (stage) *stage = -1;
    return "brawl: none";
}

std::string GameWorld::aiEventText(int* stage, vec3* pos, int* car) const {
    for (const AmbientEvent& e : gEv.ev) {
        if (!e.active || e.type != EV_TAKEOVER) continue;
        int c = liveVeh(*this, e.veh[0]);
        if (stage) *stage = e.stage;
        if (pos) *pos = e.pos;
        if (car) *car = c;
        int crowd = 0;
        for (int k = 1; k < e.np; k++) crowd += livePed(*this, e.ped[k]) >= 0;
        const Vehicle* v = c >= 0 ? &vehicles[c] : nullptr;
        return StrFormat("takeover stage %d t %.1f at %.0f %.0f | car %d speed %.1f yaw %.2f smoke %.2f phase %d dir %d drift %.1f | crowd %d blockers %d | "
                         "police called %d", (int)e.stage, e.t, e.pos.x, e.pos.y, c, v ? v->sim.speed() : 0.f, v ? v->sim.body.angVel.z : 0.f, e.donut.smoke,
                         e.donut.phase, e.donut.dir, e.donut.drift, crowd, e.nv - 1, (int)e.asked);
    }
    if (stage) *stage = -1;
    return "takeover: none";
}

}  // namespace Game
