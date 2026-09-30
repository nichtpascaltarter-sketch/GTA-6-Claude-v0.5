// Ambient population around the player. Pedestrians are placed on the sidewalk graph (lanes.cpp) with roles and
// activities that depend on the district and the time of day: business people downtown by day, nightlife crowds on
// Sol Beach and in Calle Luna at night, joggers on the promenade in the morning and evening, sunbathers on the sand,
// port workers, Cuervos / Saints hanging out on their turf, groups walking or chatting, people on benches, at bus
// stops or hailing taxis. Traffic spawns on lanes (taxis, buses, police patrols by district), cars park along the
// curbs, and ambulances / fire trucks are dispatched to bodies and fires. Spawning happens out of view (except during
// populationWarmup, when the screen is faded and the surroundings are filled at once) and far/unseen entities are
// recycled. Event participants, mission entities and active police units are never recycled here.
#include "gameworld.h"

namespace Game {

namespace pop_detail {

struct Density {
    float peds, traffic, parked;
};

Density densityFor(World::Region r) {
    switch (r) {
        case World::REG_DOWNTOWN: return {44, 30, 14};
        case World::REG_FINANCIAL: return {38, 28, 10};
        case World::REG_MIDTOWN: return {34, 24, 16};
        case World::REG_NORTH_CITY: return {26, 22, 16};
        case World::REG_CALLE_LUNA: return {36, 20, 18};
        case World::REG_BEACH: return {42, 20, 16};
        case World::REG_BAY_ISLAND: return {8, 6, 8};
        case World::REG_KEY_CORAL: return {14, 10, 10};
        case World::REG_PORT: return {14, 16, 8};
        case World::REG_GROVE: return {18, 14, 14};
        case World::REG_AIRPORT: return {10, 16, 12};
        case World::REG_FLATS: return {18, 18, 14};
        case World::REG_SUBURBS: return {12, 14, 16};
        case World::REG_REDLAND: return {6, 8, 6};
        case World::REG_SAWGRASS: return {1, 5, 1};
        case World::REG_GULF_TOWN: return {10, 6, 6};
        case World::REG_FARMLAND: return {2, 6, 3};
        case World::REG_LAKE_TOWN: return {14, 10, 10};
        case World::REG_HARLOW: return {10, 8, 8};
        case World::REG_RIDGE: return {2, 6, 2};
        case World::REG_FORT_CASTELL: return {14, 12, 12};
        case World::REG_KEYS: return {4, 6, 4};
        case World::REG_KEY_TOWN: return {18, 10, 10};
        default: return {0, 0, 0};
    }
}

bool nightlifeArea(World::Region r) { return r == World::REG_BEACH || r == World::REG_CALLE_LUNA; }
bool isNight(float tod) { return tod >= 20.f || tod < 4.5f; }

float pedTimeFactor(World::Region r, float tod) {
    float f;
    if (tod < 5.f) f = 0.25f;
    else if (tod < 7.f) f = 0.25f + (tod - 5.f) * 0.35f;
    else if (tod < 20.f) f = 1.f;
    else if (tod < 23.f) f = 1.f - (tod - 20.f) * 0.2f;
    else f = 0.35f;
    if (isNight(tod) && nightlifeArea(r)) f = Max(f, 0.95f);            // clubs, bars, the beach front
    if (r == World::REG_FINANCIAL && (tod < 7.f || tod > 20.f)) f *= 0.5f; // offices empty at night
    if (r == World::REG_PORT && (tod < 6.f || tod > 19.f)) f *= 0.4f;      // shifts end
    return f;
}

float trafficTimeFactor(float tod) {
    if (tod < 5.f) return 0.35f;
    if (tod < 7.f) return 0.35f + (tod - 5.f) * 0.33f;
    if (tod < 22.f) return 1.f;
    return 0.6f;
}

enum PedSpawnKind : u8 {
    PK_WALKER = 0, PK_GROUP, PK_CHAT, PK_SPOT, PK_HAIL, PK_WALL, PK_JOGGER, PK_SUNBATHER, PK_GANG, PK_WORKER, PK_NIGHTLIFE,
    PK_BUSINESS, PK_QUEUE, PK_BEAT, PK_COUNT
};

struct PopState {
    float pedTimer = 0.f, carTimer = 0.f, parkTimer = 0.f, incidentTimer = 0.f;
    u32 counter = 1;
};
PopState gPop;

constexpr int kMaxPeds = 60;
constexpr int kMaxTraffic = 34;
constexpr int kMaxParked = 18;

inline u32 nextSeed() { return hash32(gPop.counter++ * 2654435761u + 0x51ED27u); }

int gangCharFor(GameWorld& g, u32 seed, Faction f) {
    const std::vector<int>& v = g.charsByRole[2];
    if (v.empty()) return g.randomCivilianChar(seed, 0);
    int half = (int)v.size() / 2;
    if (half == 0) return v[seed % v.size()];
    return f == FAC_GANG_SAINTS ? v[half + (int)(seed % (u32)(v.size() - half))] : v[seed % (u32)half];
}

Faction turfOwner(World::Region r) {
    if (r == World::REG_CALLE_LUNA || r == World::REG_FLATS) return FAC_GANG_CUERVOS;
    if (r == World::REG_PORT) return FAC_GANG_SAINTS;
    return FAC_CIVILIAN;
}

// a sidewalk position near `probe`
struct Side {
    int link = -1;
    float x = 0.f;
    vec3 pos;
    vec2 t, outward;
    float halfWidth = 1.f;
};

bool sidewalkAt(const GameWorld& g, vec2 probe, float maxDist, Side& s) {
    float x = 0.f, lat = 0.f;
    int link = g.laneGraph.nearestWalk(probe, maxDist, &x, &lat);
    if (link < 0) return false;
    const AI::WalkLink& L = g.laneGraph.walkLinks[link];
    if (L.kind == AI::WL_CROSSWALK || L.length < 3.f) return false;
    s.link = link;
    s.x = Clamp(x, 0.8f, L.length - 0.8f);
    s.halfWidth = L.kind == AI::WL_SIDEWALK ? L.halfWidth : 0.8f;
    s.pos = g.laneGraph.walkPos(link, s.x, 0.f, true);
    s.t = g.laneGraph.walkTangent(link, s.x, true);
    if (L.kind == AI::WL_SIDEWALK) {
        float along = L.sb >= L.sa ? 1.f : -1.f;
        s.outward = AI::rightOf(s.t) * ((L.lat > 0.f ? 1.f : -1.f) * along);
    } else {
        s.outward = AI::rightOf(s.t);
    }
    return true;
}

vec3 sideOffset(const GameWorld& g, const Side& s, float along, float across) {
    vec2 p = s.pos.xy() + s.t * along + s.outward * across;
    return vec3(p, g.groundHeight(p.x, p.y, s.pos.z + 1.2f));
}

bool freeStandingSpot(const GameWorld& g, vec3 p) {
    vec3 push, nrm;
    if (Phys::gCollision && Phys::gCollision->capsuleOverlap(p + vec3(0, 0, 0.05f), 0.3f, 1.7f, push, nrm)) return false;
    return true;
}

// ---- nightlife: lines outside the clubs of Sol Beach and Calle Luna, a bouncer at the door
struct ClubQueue {
    bool active = false;
    vec2 door;         // head of the line (next to the door)
    vec2 along;        // direction the line grows away from the door
    vec2 outward;      // from the street toward the building
    float z = 0.f;
    int ids[8];
    u32 uids[8];
    int n = 0;
    int bouncer = -1;
    u32 bouncerUid = 0;
    float timer = 20.f;
    float barkT = 0.f;
};
ClubQueue gQueues[2];

bool queuePedOk(const GameWorld& g, int id, u32 uid) {
    if (id < 0 || id >= (int)g.peds.size()) return false;
    const Ped& p = g.peds[id];
    return p.used && p.uid == uid && p.health > 0.f && p.state == PS_ONFOOT && p.brain.type == BRAIN_WANDER && p.takedownT < 0.f;
}

vec2 queueSlot(const ClubQueue& q, int k) { return q.door + q.along * (1.1f + k * 0.85f) + q.outward * 0.15f; }

void releaseQueue(GameWorld& g, ClubQueue& q) {
    for (int k = 0; k < q.n; k++)
        if (queuePedOk(g, q.ids[k], q.uids[k])) {
            PedAI& pa = g.pedAI(q.ids[k]);
            pa.activity = ACT_WALK;
            pa.navOk = false;
            pa.actTimer = 20.f;
        }
    if (queuePedOk(g, q.bouncer, q.bouncerUid)) {
        PedAI& ba = g.pedAI(q.bouncer);
        ba.activity = ACT_WALK;
        ba.navOk = false;
    }
    q.active = false;
    q.n = 0;
}

void updateQueues(GameWorld& g, float dt, vec2 pp, bool night, bool warm) {
    for (ClubQueue& q : gQueues) {
        if (!q.active) continue;
        if (length(q.door - pp) > 220.f || !night) {
            releaseQueue(g, q);
            continue;
        }
        // drop members who left the line (scared off, fighting...) and close the gaps
        int w = 0;
        for (int k = 0; k < q.n; k++) {
            bool ok = queuePedOk(g, q.ids[k], q.uids[k]) && g.pedAI(q.ids[k]).activity == ACT_QUEUE;
            if (ok) {
                q.ids[w] = q.ids[k];
                q.uids[w] = q.uids[k];
                w++;
            }
        }
        q.n = w;
        for (int k = 0; k < q.n; k++) {
            PedAI& pa = g.pedAI(q.ids[k]);
            pa.anchor = queueSlot(q, k);
            pa.anchorYaw = AI::dirYaw(-q.along);
        }
        // the bouncer lets the next one in every so often; newcomers join at the back
        q.timer -= dt;
        if (q.timer <= 0.f) {
            u32 h = hash32((u32)(g.time * 10.0) + (u32)q.door.x);
            q.timer = 15.f + hashToFloat(h) * 20.f;
            if (q.n > 0) {
                int id = q.ids[0];
                PedAI& pa = g.pedAI(id);
                // walks in through the door (and out of the simulation)
                pa.activity = ACT_ENTER_VEH;   // reuse: walk to a point and vanish
                pa.targetVeh = -1;
                g.peds[id].brain.type = BRAIN_GOTO;
                g.peds[id].brain.goal = dvec3(vec3(q.door + q.outward * 0.4f, q.z));
                g.peds[id].brain.speed = 1.2f;
                g.peds[id].brain.timer = 0.f;
                for (int k = 1; k < q.n; k++) {
                    q.ids[k - 1] = q.ids[k];
                    q.uids[k - 1] = q.uids[k];
                }
                q.n--;
                if (queuePedOk(g, q.bouncer, q.bouncerUid)) g.peds[q.bouncer].pendingAction = Anim::CLIP_WAVE;
            }
        }
        if (queuePedOk(g, q.bouncer, q.bouncerUid)) {
            Ped* pl = g.playerPed();
            q.barkT -= dt;
            if (pl && q.barkT <= 0.f && length(pl->pos.toVec3().xy() - q.door) < 4.f) {
                g.aiSay(q.bouncer, BK_BOUNCER, 1.f, true);
                q.barkT = 8.f;
            }
        }
        (void)warm;
    }
    // walkers who reached the door disappear inside
    for (int i = 0; i < (int)g.peds.size(); i++) {
        Ped& p = g.peds[i];
        if (!p.used || p.persistent || p.brain.type != BRAIN_GOTO || p.faction != FAC_CIVILIAN || i >= (int)g.ai.ped.size()) continue;
        if (g.ai.ped[i].uid != p.uid || g.ai.ped[i].activity != ACT_ENTER_VEH || g.ai.ped[i].targetVeh >= 0) continue;
        if (length(p.pos.toVec3().xy() - p.brain.goal.toVec3().xy()) < 1.1f) {
            g.despawnPed(i);
        } else if (p.brain.timer > 20.f) {
            // could not get there: vanish if nobody sees it, else carry on walking
            if (!g.inCameraView(p.pos.toVec3() + vec3(0, 0, 1.f), 1.f)) {
                g.despawnPed(i);
            } else {
                p.brain.type = BRAIN_WANDER;
                p.brain.edge = -1;
                g.ai.ped[i].activity = ACT_WALK;
                g.ai.ped[i].navOk = false;
            }
        }
    }
}

}  // namespace pop_detail

using namespace pop_detail;

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updatePopulation(float dt) {
    Ped* pl = playerPed();
    if (!pl || populationOff || !ai.ready) return;
    bool warm = populationWarmup > 0.f;
    vec3 pp = pl->pos.toVec3();
    int pv = playerVehicle();
    vec3 pvel = pv >= 0 ? vehicles[pv].sim.body.vel : pl->vel;
    float speed = length(pvel.xy());
    vec3 center = pp + (pv >= 0 ? pvel * 2.f : vec3(0));   // lead the player when driving fast
    World::Region reg = map->regionAt(center.x, center.y);
    float tod = env ? env->timeOfDay : 12.f;
    float rain = env ? env->rain : 0.f;
    Density den = densityFor(reg);
    int wantPeds = Min((int)(den.peds * pedTimeFactor(reg, tod) * (rain > 0.4f ? 0.45f : 1.f) * pedDensityScale), kMaxPeds);
    int wantTraffic = Min((int)(den.traffic * trafficTimeFactor(tod) * trafficDensityScale), kMaxTraffic);
    int wantParked = Min((int)(den.parked * trafficDensityScale), kMaxParked);
    // ------------------------------------------------------------------ count and recycle
    int nPeds = 0, nTraffic = 0, nParked = 0, nGang = 0, nEms = 0, nBeat = 0;
    const float pedDespawn = 150.f, carDespawn = 340.f;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.isPlayer || p.persistent || p.vehicle >= 0) continue;
        float d = length(p.pos.toVec3().xy() - pp.xy());
        bool dead = p.state == PS_DEAD;
        PedAI& pa = pedAI(i);
        bool busy = pa.eventId >= 0 || (p.faction == FAC_POLICE && (p.brain.type == BRAIN_COMBAT || p.brain.type == BRAIN_ARREST) && d < 400.f) ||
                    pa.homeVeh >= 0;
        bool unseen = !inCameraView(p.pos.toVec3() + vec3(0, 0, 1.f), 1.f);
        if (d > (busy ? 420.f : pedDespawn) || (dead && p.stateTime > 60.f && d > 40.f && unseen) ||
            (!busy && d > 80.f && unseen && nPeds > wantPeds + 4)) {
            despawnPed(i);
            continue;
        }
        if (!dead) {
            nPeds++;
            if ((p.faction == FAC_GANG_CUERVOS || p.faction == FAC_GANG_SAINTS) && d < 120.f) nGang++;
            if (p.faction == FAC_POLICE && pa.homeVeh < 0 && d < 200.f) nBeat++;
        }
    }
    for (int i = 0; i < (int)vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.used || v.persistent || v.scripted || i == pv) continue;
        bool playerInside = false;
        for (int s = 0; s < 8; s++)
            if (v.seats[s] >= 0 && peds[v.seats[s]].isPlayer) playerInside = true;
        if (playerInside) continue;
        float d = length(v.sim.body.pos.toVec3().xy() - pp.xy());
        VehAI& va = vehAI(i);
        int drv = v.seats[0];
        bool pursuing = v.faction == FAC_POLICE && drv >= 0 && (peds[drv].brain.type == BRAIN_COMBAT || peds[drv].brain.type == BRAIN_ARREST);
        bool busy = va.eventId >= 0 || pursuing || ((va.role == VR_AMBULANCE || va.role == VR_FIRETRUCK) && va.scene >= 0 && va.task < 2);
        // crew out on foot: keep the vehicle while they are around
        for (int k = 0; k < (int)peds.size() && !busy && drv < 0 && (v.faction == FAC_POLICE || v.faction == FAC_MEDIC); k++)
            if (peds[k].used && k < (int)ai.ped.size() && ai.ped[k].uid == peds[k].uid && ai.ped[k].homeVeh == i && peds[k].health > 0.f) busy = true;
        float limit = v.playerUsed ? 600.f : (busy ? 520.f : (v.parked || drv < 0 ? 220.f : carDespawn));
        bool unseen = !inCameraView(v.sim.body.pos.toVec3(), 3.f);
        if (d > limit || ((v.exploded || v.sim.wrecked) && v.wreckTime > 90.f && d > 60.f && unseen)) {
            despawnVehicle(i, true);
            continue;
        }
        if (va.role == VR_AMBULANCE || va.role == VR_FIRETRUCK) nEms += va.scene >= 0 && va.task < 2;
        if (v.parked || drv < 0) nParked++;
        else if (!peds[drv].isPlayer && !isAircraft(i) && !isBoat(i)) nTraffic++;
    }
    updateQueues(*this, dt, pp.xy(), isNight(tod), warm);
    // ------------------------------------------------------------------ emergency services
    gPop.incidentTimer -= dt;
    if (gPop.incidentTimer <= 0.f) {
        gPop.incidentTimer = 2.f;
        // bodies -> ambulance, burning cars / fires -> fire truck
        auto addIncident = [&](dvec3 pos, u8 kind) {
            for (Incident& inc : ai.incidents)
                if (inc.active && inc.kind == kind && length(rel(inc.pos, pos)) < 25.f) {
                    inc.time = (float)time;
                    return;
                }
            Incident inc;
            inc.pos = pos;
            inc.kind = kind;
            inc.time = (float)time;
            inc.active = true;
            for (Incident& o : ai.incidents)
                if (!o.active) {
                    o = inc;
                    return;
                }
            ai.incidents.push_back(inc);
        };
        for (int i = 0; i < (int)peds.size(); i++) {
            const Ped& p = peds[i];
            if (!p.used || p.isPlayer || p.state != PS_DEAD || p.stateTime < 6.f || p.stateTime > 100.f) continue;
            if (length(p.pos.toVec3().xy() - pp.xy()) > 180.f) continue;
            addIncident(p.pos, 0);
        }
        for (const Fire& f : fires)
            if (f.used && f.life > 8.f && length(f.pos.toVec3().xy() - pp.xy()) < 200.f) addIncident(f.pos, 1);
        for (int i = 0; i < (int)vehicles.size(); i++)
            if (vehicles[i].used && vehicles[i].fireTimer > 0.f && !vehicles[i].exploded && length(vehicles[i].sim.body.pos.toVec3().xy() - pp.xy()) < 200.f)
                addIncident(vehicles[i].sim.body.pos, 1);
        // expire and dispatch
        for (int k = 0; k < (int)ai.incidents.size(); k++) {
            Incident& inc = ai.incidents[k];
            if (!inc.active || inc.kind > 1) continue;
            if (time - inc.time > 150.0 || length(inc.pos.toVec3().xy() - pp.xy()) > 400.f) {
                inc.active = false;
                continue;
            }
            bool covered = inc.unit >= 0 && inc.unit < (int)vehicles.size() && vehicles[inc.unit].used && vehAI(inc.unit).scene == k;
            if (covered || nEms >= 2 || pinfo.wanted >= 3) continue;
            inc.unit = -1;
            int model = findVehicleModel(inc.kind == 0 ? Vehicles::VC_AMBULANCE : Vehicles::VC_FIRETRUCK, nextSeed());
            if (model < 0) continue;
            vec2 ip = inc.pos.toVec3().xy();
            for (int attempt = 0; attempt < 6; attempt++) {
                u32 h = nextSeed();
                float ang = hashToFloat(h) * kTwoPi;
                vec2 probe = ip + vec2(cosf(ang), sinf(ang)) * (170.f + hashToFloat(hash32(h)) * 90.f);
                float u = 0.f;
                int lane = laneGraph.nearestLane(probe, vec2(0), 50.f, &u);
                if (lane < 0 || (laneGraph.lanes[lane].flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC))) continue;
                const AI::Lane& L = laneGraph.lanes[lane];
                u = Clamp(u, L.u0 + 5.f, L.u1 - 10.f);
                vec3 c = laneGraph.lanePos(lane, u);
                if ((inCameraView(c, 8.f) && length(c.xy() - pp.xy()) < 250.f) || !traffic.laneFree(lane, u, vassets[model].spec.boxHalf.y, 8.f)) continue;
                vec2 t = laneGraph.laneTangent(lane, u);
                int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_MEDIC);
                if (vid < 0) break;
                Vehicle& v = vehicles[vid];
                v.faction = FAC_MEDIC;
                if (v.seats[0] >= 0) peds[v.seats[0]].brain.type = BRAIN_DRIVER;
                int crew = spawnPed(randomCivilianChar(nextSeed(), 6), v.sim.body.pos, 0.f, FAC_MEDIC);
                if (crew >= 0) {
                    int seat = freeSeat(vid, false);
                    if (seat > 0) {
                        warpPedIntoVehicle(crew, vid, seat);
                        peds[crew].brain.type = BRAIN_PASSENGER;
                    } else {
                        despawnPed(crew);
                    }
                }
                attachTraffic(vid, lane, u);
                v.sim.body.vel = vec3(t * 8.f, 0.f);
                VehAI& va = vehAI(vid);
                va.role = inc.kind == 0 ? VR_AMBULANCE : VR_FIRETRUCK;
                va.scene = k;
                va.task = 0;
                inc.unit = vid;
                nEms++;
                break;
            }
        }
    }
    // fire crews put out fires next to them
    for (Fire& f : fires) {
        if (!f.used) continue;
        std::vector<int> crewNear;
        pedsNear(f.pos.toVec3().xy(), 7.f, crewNear);
        for (int c : crewNear) {
            if (peds[c].faction != FAC_MEDIC || peds[c].state != PS_ONFOOT || c >= (int)ai.ped.size() || ai.ped[c].uid != peds[c].uid) continue;
            int hv = ai.ped[c].homeVeh;
            if (hv < 0 || hv >= (int)vehicles.size() || !vehicles[hv].used || vehAI(hv).role != VR_FIRETRUCK) continue;
            f.life -= dt * 4.f;
            if (((int)(time * 8.0) & 3) == 0) spawnFx(FX_WATER_SPLASH, f.pos, vec3(0, 0, 1), 2, 0.8f);
        }
    }
    for (int i = 0; i < (int)vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.used || v.fireTimer <= 0.f) continue;
        std::vector<int> crewNear;
        pedsNear(v.sim.body.pos.toVec3().xy(), 7.f, crewNear);
        for (int c : crewNear)
            if (peds[c].faction == FAC_MEDIC && peds[c].state == PS_ONFOOT && c < (int)ai.ped.size() && ai.ped[c].uid == peds[c].uid && ai.ped[c].homeVeh >= 0 &&
                ai.ped[c].homeVeh < (int)vehicles.size() && vehicles[ai.ped[c].homeVeh].used && vehAI(ai.ped[c].homeVeh).role == VR_FIRETRUCK) {
                v.fireTimer = Max(0.f, v.fireTimer - dt * 3.f);
                if (((int)(time * 8.0) & 3) == 0) spawnFx(FX_WATER_SPLASH, v.sim.body.pos, vec3(0, 0, 1), 2, 0.8f);
            }
    }
    // ------------------------------------------------------------------ pedestrians
    Faction turf = turfOwner(reg);
    bool night = isNight(tod);
    int pedBudget = warm ? 8 : 0;
    if (!warm) {
        gPop.pedTimer -= dt;
        if (gPop.pedTimer <= 0.f) {
            gPop.pedTimer = 0.1f;
            pedBudget = 1;
        }
    }
    for (int spawnIter = 0; spawnIter < pedBudget && nPeds < wantPeds && !chars.empty(); spawnIter++) {
        u32 h = nextSeed();
        // pick a kind by district and time of day
        float w[PK_COUNT] = {};
        bool business = (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL) && tod > 7.f && tod < 19.5f;
        bool beach = reg == World::REG_BEACH || reg == World::REG_KEY_CORAL;
        w[PK_WALKER] = 1.f;
        w[PK_GROUP] = night && nightlifeArea(reg) ? 0.45f : 0.25f;
        w[PK_CHAT] = 0.14f;
        w[PK_SPOT] = 0.2f;
        w[PK_HAIL] = (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_MIDTOWN || reg == World::REG_BEACH) && !night ? 0.05f : 0.f;
        w[PK_WALL] = 0.14f;
        w[PK_JOGGER] = ((tod > 6.f && tod < 10.f) || (tod > 17.f && tod < 20.f)) ? (beach ? 0.6f : (reg == World::REG_GROVE || reg == World::REG_SUBURBS ? 0.25f : 0.06f)) : 0.f;
        w[PK_SUNBATHER] = reg == World::REG_BEACH && tod > 9.f && tod < 18.f && rain < 0.2f ? 0.8f : 0.f;
        w[PK_GANG] = turf != FAC_CIVILIAN && nGang < 8 ? (night ? 0.22f : 0.12f) : 0.f;
        w[PK_WORKER] = (reg == World::REG_PORT && tod > 6.f && tod < 18.5f) ? 0.9f : ((reg == World::REG_FLATS || reg == World::REG_FORT_CASTELL) && !night ? 0.15f : 0.f);
        w[PK_NIGHTLIFE] = night && nightlifeArea(reg) ? 0.8f : 0.f;
        w[PK_BUSINESS] = business ? 0.7f : 0.f;
        int freeQueue = -1;
        for (int k = 0; k < 2; k++)
            if (!gQueues[k].active) freeQueue = k;
        w[PK_QUEUE] = night && nightlifeArea(reg) && freeQueue >= 0 && nPeds + 8 < wantPeds + 4 ? 0.35f : 0.f;
        // officers walking a beat in pairs through the busy districts (day and evening), at most one pair around
        bool beatArea = reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_MIDTOWN || reg == World::REG_BEACH ||
                        reg == World::REG_CALLE_LUNA;
        w[PK_BEAT] = beatArea && tod > 7.f && tod < 23.f && nBeat < 2 && pinfo.wanted == 0 ? 0.05f : 0.f;
        float sum = 0.f;
        for (float x : w) sum += x;
        float r = hashToFloat(h) * sum;
        int kind = 0;
        for (; kind < PK_COUNT - 1; kind++) {
            r -= w[kind];
            if (r <= 0.f) break;
        }
        while (w[kind] <= 0.f && kind > 0) kind--;
        // location: a ring around the center; in view only while warming up or beyond 90 m
        float ang = hashToFloat(hash32(h ^ 0x1234567u)) * kTwoPi;
        float rMin = warm ? 6.f : 40.f, rMax = warm ? 105.f : 115.f;
        float rad = rMin + hashToFloat(hash32(h ^ 0xBEEFu)) * (rMax - rMin);
        vec2 probe = center.xy() + vec2(cosf(ang), sinf(ang)) * rad;
        auto visibleNear = [&](vec3 p) { return !warm && inCameraView(p + vec3(0, 0, 1.f), 1.5f) && length(p.xy() - pp.xy()) < 90.f; };
        auto tooClose = [&](vec3 p) { return length(p.xy() - pp.xy()) < (warm ? 4.f : 25.f); };
        int spawned = 0;
        switch (kind) {
            case PK_SUNBATHER: {
                // on the sand between the water line and the promenade
                bool ok = false;
                vec2 sp;
                for (int k = 0; k < 6 && !ok; k++) {
                    u32 hk = hash32(h + k * 7919u);
                    float a2 = hashToFloat(hk) * kTwoPi, r2 = rMin + hashToFloat(hash32(hk)) * (rMax - rMin);
                    sp = center.xy() + vec2(cosf(a2), sinf(a2)) * r2;
                    if (map->regionAt(sp.x, sp.y) != World::REG_BEACH || map->isWater(sp.x, sp.y)) continue;
                    float cd = map->coastDistance(sp.x, sp.y);
                    if (cd < 10.f || cd > 60.f || roads->nearRoad(sp, 4.f)) continue;
                    ok = true;
                }
                if (!ok) break;
                vec3 p3(sp, groundHeight(sp.x, sp.y, map->heightAt(sp.x, sp.y) + 2.f));
                if (visibleNear(p3) || tooClose(p3) || !freeStandingSpot(*this, p3)) break;
                int id = spawnPed(randomCivilianChar(h >> 3, 4), dvec3(p3), hashToFloat(hash32(h)) * kTwoPi, FAC_CIVILIAN);
                if (id < 0) break;
                Ped& p = peds[id];
                p.brain.type = BRAIN_WANDER;
                p.brain.edge = -1;
                PedAI& pa = pedAI(id);
                pa.role = PR_BEACH;
                pa.activity = ACT_SCENARIO;
                pa.anchor = sp;
                pa.anchorYaw = p.yaw;
                pa.stance = 12;   // lying in the sun (looping stance)
                pa.clip = -1;
                pa.actTimer = 90.f + hashToFloat(hash32(h * 3u)) * 200.f;
                spawned = 1;
                break;
            }
            case PK_QUEUE: {
                // a line outside a club door on the building side of the sidewalk, with a bouncer
                Side sd;
                if (!sidewalkAt(*this, probe, 40.f, sd) || laneGraph.walkLinks[sd.link].kind != AI::WL_SIDEWALK || sd.halfWidth < 1.3f) break;
                const AI::WalkLink& L = laneGraph.walkLinks[sd.link];
                if (sd.x < 3.f || L.length - sd.x < 9.f) break;
                vec3 door3 = sideOffset(*this, sd, 0.f, sd.halfWidth * 0.75f);
                if (visibleNear(door3) || tooClose(door3)) break;
                bool clash = false;
                for (const ClubQueue& o : gQueues)
                    if (o.active && length(o.door - door3.xy()) < 40.f) clash = true;
                if (clash || freeQueue < 0) break;
                ClubQueue& q = gQueues[freeQueue];
                q = ClubQueue();
                q.door = door3.xy();
                q.along = sd.t;
                q.outward = sd.outward;
                q.z = door3.z;
                q.timer = 12.f + hashToFloat(h) * 12.f;
                vec2 bp = q.door - q.along * 0.9f;
                int bid = spawnPed(randomCivilianChar(hash32(h * 3u), 5), dvec3(vec3(bp, door3.z)), AI::dirYaw(-q.outward), FAC_CIVILIAN);
                if (bid < 0) break;
                peds[bid].brain.type = BRAIN_WANDER;
                peds[bid].brain.edge = -1;
                PedAI& ba = pedAI(bid);
                ba.role = PR_WORKER;
                ba.activity = ACT_QUEUE;   // stays put at his post
                ba.anchor = bp;
                ba.anchorYaw = AI::dirYaw(-q.outward);
                ba.stance = 0;
                ba.clip = -1;
                ba.temper = 2;
                q.bouncer = bid;
                q.bouncerUid = peds[bid].uid;
                int want = 4 + (int)(h % 4u);
                for (int k = 0; k < want; k++) {
                    vec2 sp = queueSlot(q, k);
                    vec3 sp3(sp, groundHeight(sp.x, sp.y, door3.z + 1.f));
                    if (!freeStandingSpot(*this, sp3)) break;
                    u32 hk = hash32(h * 17u + k * 131u);
                    int id = spawnPed(randomCivilianChar(hk >> 2, (hk & 3) == 0 ? 4 : 0), dvec3(sp3), AI::dirYaw(-q.along), FAC_CIVILIAN);
                    if (id < 0) break;
                    peds[id].brain.type = BRAIN_WANDER;
                    peds[id].brain.edge = -1;
                    PedAI& pa = pedAI(id);
                    pa.role = PR_NIGHTLIFE;
                    pa.activity = ACT_QUEUE;
                    pa.anchor = sp;
                    pa.anchorYaw = AI::dirYaw(-q.along);
                    float qr = hashToFloat(hash32(hk));
                    pa.stance = qr < 0.4f ? 7 : (qr < 0.65f ? 8 : 0);
                    pa.clip = -1;
                    pa.clipTimer = 3.f + qr * 8.f;
                    q.ids[q.n] = id;
                    q.uids[q.n] = peds[id].uid;
                    q.n++;
                    nPeds++;
                    spawned++;
                }
                q.active = true;
                nPeds++;
                break;
            }
            case PK_SPOT: {
                // a free bench or bus stop nearby
                std::vector<int> spots;
                laneGraph.spotsNear(probe, 35.f, spots);
                int pick = -1;
                for (int si : spots) {
                    vec3 sp3 = laneGraph.spots[si].pos;
                    if (visibleNear(sp3) || tooClose(sp3)) continue;
                    bool taken = false;
                    for (int k = 0; k < (int)ai.ped.size() && k < (int)peds.size() && !taken; k++)
                        if (peds[k].used && ai.ped[k].uid == peds[k].uid && ai.ped[k].activity != ACT_WALK && length(ai.ped[k].anchor - sp3.xy()) < 1.0f) taken = true;
                    if (!taken) {
                        pick = si;
                        break;
                    }
                }
                if (pick < 0) break;
                const AI::ScenarioPoint& sp = laneGraph.spots[pick];
                bool bench = sp.kind == AI::SP_BENCH;
                int role = business && (h & 3) == 0 ? 3 : 0;
                int id = spawnPed(randomCivilianChar(h >> 3, role), dvec3(sp.pos), AI::dirYaw(sp.face), FAC_CIVILIAN);
                if (id < 0) break;
                Ped& p = peds[id];
                p.brain.type = BRAIN_WANDER;
                p.brain.edge = -1;
                PedAI& pa = pedAI(id);
                pa.role = role == 3 ? PR_BUSINESS : PR_CIVILIAN;
                pa.activity = bench ? ACT_SCENARIO : ACT_WAIT_BUS;
                pa.anchor = sp.pos.xy();
                pa.anchorYaw = AI::dirYaw(sp.face);
                pa.stance = bench ? 6 : ((h >> 7) % 3 == 0 ? 8 : 0);
                pa.clip = -1;
                pa.actTimer = 30.f + hashToFloat(hash32(h * 5u)) * 90.f;
                spawned = 1;
                break;
            }
            default: {
                Side s;
                if (!sidewalkAt(*this, probe, 40.f, s)) break;
                const AI::WalkLink& L = laneGraph.walkLinks[s.link];
                bool sidewalk = L.kind == AI::WL_SIDEWALK;
                if (!sidewalk && kind != PK_WALKER && kind != PK_JOGGER && kind != PK_BUSINESS && kind != PK_GROUP) break;
                if (kind == PK_BEAT && (!sidewalk || s.halfWidth < 0.9f)) break;
                World::Region sreg = map->regionAt(s.pos.x, s.pos.y);
                // groups standing at the building side: chatting, gang hangouts, nightlife, workers on a break
                bool standingGroup = kind == PK_CHAT || kind == PK_GANG || kind == PK_NIGHTLIFE || (kind == PK_WORKER && (h >> 9) % 2 == 0);
                if (standingGroup) {
                    if (s.halfWidth < 1.1f) break;
                    int n = 2 + (int)((h >> 5) % (kind == PK_GANG || kind == PK_NIGHTLIFE ? 3u : 2u));
                    vec3 c3 = sideOffset(*this, s, 0.f, s.halfWidth * 0.35f);
                    if (visibleNear(c3) || tooClose(c3)) break;
                    Faction f = kind == PK_GANG ? (turfOwner(sreg) != FAC_CIVILIAN ? turfOwner(sreg) : turf) : FAC_CIVILIAN;
                    int charRole = kind == PK_WORKER ? 5 : (kind == PK_NIGHTLIFE ? ((h >> 3) % 3 == 0 ? 4 : 0) : (business ? 3 : 0));
                    float base = hashToFloat(hash32(h * 11u)) * kTwoPi;
                    for (int k = 0; k < n && nPeds < wantPeds + 2; k++) {
                        float a = base + k * kTwoPi / n;
                        vec2 off(cosf(a) * 0.8f, sinf(a) * 0.8f);
                        vec3 p3(c3.xy() + off, groundHeight(c3.x + off.x, c3.y + off.y, c3.z + 1.f));
                        if (!freeStandingSpot(*this, p3)) continue;
                        u32 hk = hash32(h * 31u + k * 977u);
                        int ci = f != FAC_CIVILIAN ? gangCharFor(*this, hk, f) : randomCivilianChar(hk >> 2, charRole);
                        int id = spawnPed(ci, dvec3(p3), 0.f, f);
                        if (id < 0) continue;
                        Ped& p = peds[id];
                        p.brain.type = BRAIN_WANDER;
                        p.brain.edge = -1;
                        PedAI& pa = pedAI(id);
                        pa.role = kind == PK_GANG ? PR_GANG : (kind == PK_WORKER ? PR_WORKER : (kind == PK_NIGHTLIFE ? PR_NIGHTLIFE : PR_CIVILIAN));
                        pa.activity = ACT_SCENARIO;
                        pa.anchor = p3.xy();
                        pa.anchorYaw = atan2f(off.x, -off.y);   // facing the circle center
                        p.yaw = pa.anchorYaw;
                        float q = hashToFloat(hash32(hk));
                        if (kind == PK_NIGHTLIFE) {
                            pa.stance = q < 0.5f ? 9 : 7;
                            pa.clip = -1;
                        } else if (q < 0.55f) {
                            pa.stance = 7;
                            pa.clip = -1;
                        } else if (q < 0.8f) {
                            pa.stance = 10;   // smoking
                            pa.clip = -1;
                        } else {
                            pa.stance = 8;
                            pa.clip = -1;
                        }
                        pa.actTimer = (kind == PK_GANG ? 150.f : 25.f) + hashToFloat(hash32(hk * 3u)) * (kind == PK_GANG ? 200.f : 50.f);
                        if (kind == PK_GANG) {
                            WeaponType wpn = hk % 10 < 7 ? WPN_PISTOL : (hk % 10 < 9 ? WPN_SMG : WPN_BAT);
                            giveWeapon(id, wpn, wpn == WPN_BAT ? 1 : 80);
                            p.weapon = wpn;
                            p.brain.accuracy = 0.15f + hashToFloat(hash32(hk * 7u)) * 0.2f;   // thugs 0.15-0.35
                            p.brain.aggression = 0.8f;
                            pa.temper = 2;
                        }
                        nPeds++;
                        spawned++;
                    }
                    break;
                }
                // one ped (or a walking group) on the sidewalk
                float lat = (hashToFloat(hash32(h * 13u)) - 0.5f) * s.halfWidth * 1.2f;
                vec3 p3 = sideOffset(*this, s, 0.f, lat);
                if (kind == PK_WALL) p3 = sideOffset(*this, s, 0.f, s.halfWidth * 0.8f);
                if (kind == PK_HAIL) p3 = sideOffset(*this, s, 0.f, -s.halfWidth * 0.75f);
                // now and then someone steps out of a building door instead (fine in view: they just came out)
                vec3 door;
                bool fromDoor = !warm && sidewalk && (kind == PK_WALKER || kind == PK_BUSINESS) && hashToFloat(hash32(h * 41u)) < 0.3f &&
                                aiBuildingDoorNear(*this, p3.xy(), 16.f, h, door) && length(door.xy() - pp.xy()) > 12.f && freeStandingSpot(*this, door);
                if (!fromDoor && (visibleNear(p3) || tooClose(p3) || !freeStandingSpot(*this, p3))) break;
                int charRole = 0;
                u8 role = PR_CIVILIAN;
                if (kind == PK_BUSINESS) {
                    charRole = 3;
                    role = PR_BUSINESS;
                } else if (kind == PK_WORKER) {
                    charRole = 5;
                    role = PR_WORKER;
                } else if (kind == PK_JOGGER) {
                    charRole = (sreg == World::REG_BEACH || sreg == World::REG_KEY_CORAL) ? 4 : 0;
                    role = PR_JOGGER;
                } else if (sreg == World::REG_BEACH || sreg == World::REG_KEY_CORAL || sreg == World::REG_KEYS) {
                    charRole = (h % 3 == 0) ? 4 : 0;
                    role = charRole == 4 ? PR_BEACH : PR_CIVILIAN;
                } else if (business && (h % 4 == 0)) {
                    charRole = 3;
                    role = PR_BUSINESS;
                } else if ((sreg == World::REG_PORT || sreg == World::REG_FLATS) && h % 4 == 0) {
                    charRole = 5;
                    role = PR_WORKER;
                }
                if (night && nightlifeArea(sreg) && role == PR_CIVILIAN && h % 2 == 0) role = PR_NIGHTLIFE;
                // sightseeing groups by day on the beach front, downtown and in the marina districts
                bool scenic = sreg == World::REG_BEACH || sreg == World::REG_DOWNTOWN || sreg == World::REG_KEY_CORAL || sreg == World::REG_BAY_ISLAND;
                if (kind == PK_GROUP && scenic && !night && (h >> 17) % 2 == 0) {
                    role = PR_TOURIST;
                    charRole = 4;
                }
                bool beat = kind == PK_BEAT;
                Faction fac = beat ? FAC_POLICE : FAC_CIVILIAN;
                if (beat) {
                    role = PR_COP;
                    charRole = 1;
                }
                auto equipCop = [&](int pid) {   // sidearm holstered until needed; police accuracy 0.4-0.6
                    giveWeapon(pid, WPN_PISTOL, 60);
                    peds[pid].weapon = WPN_FISTS;
                    peds[pid].brain.accuracy = 0.42f + hashToFloat(hash32(peds[pid].uid * 7u)) * 0.14f;
                    pedAI(pid).temper = 2;
                };
                bool walkDir = (h >> 11) & 1;
                vec2 heading = walkDir ? s.t : -s.t;
                int id = spawnPed(randomCivilianChar(h >> 3, charRole), dvec3(fromDoor ? door : p3), fromDoor ? AI::dirYaw(normalize(p3.xy() - door.xy() + vec2(1e-3f, 0.f))) : AI::dirYaw(heading), fac);
                if (id < 0) break;
                Ped& p = peds[id];
                p.brain.type = BRAIN_WANDER;
                p.brain.edge = -1;
                PedAI& pa = pedAI(id);
                pa.role = role;
                pa.activity = ACT_WALK;
                if (beat) equipCop(id);
                pa.actTimer = 8.f + hashToFloat(hash32(h * 17u)) * 25.f;
                if (kind == PK_WALL) {
                    pa.activity = ACT_SCENARIO;
                    pa.anchor = p3.xy();
                    pa.anchorYaw = AI::dirYaw(-s.outward);
                    p.yaw = pa.anchorYaw;
                    float q = hashToFloat(hash32(h * 19u));
                    pa.stance = q < 0.45f ? 8 : (q < 0.75f ? 10 : 11);   // phone / smoke / lean on the wall
                    pa.clip = -1;
                    pa.actTimer = 15.f + hashToFloat(hash32(h * 23u)) * 30.f;
                } else if (kind == PK_HAIL) {
                    pa.activity = ACT_HAIL_TAXI;
                    pa.anchor = p3.xy();
                    pa.anchorYaw = AI::dirYaw(-s.outward);
                    p.yaw = pa.anchorYaw;
                    pa.stance = 0;
                    pa.clip = -1;
                    pa.targetVeh = -1;
                    pa.actTimer = 0.f;
                } else {
                    // start walking right away along the sidewalk
                    if (pedNav.place(pa.walk, p3.xy(), p.uid * 2654435761u + 7u, 6.f)) {
                        pa.navOk = true;
                        p.brain.edge = pa.walk.link;
                        if (pa.walk.link == s.link) {
                            pa.walk.fromA = walkDir;
                            pa.walk.x = walkDir ? s.x : L.length - s.x;
                        }
                        if (role == PR_JOGGER) {
                            pa.walk.speed = 2.8f + hashToFloat(hash32(p.uid)) * 0.6f;
                            pa.activity = ACT_JOG;
                        } else if (role == PR_BUSINESS) {
                            pa.walk.speed = 1.45f;
                        } else if (beat) {
                            pa.walk.speed = 1.15f;   // an unhurried patrol pace
                        }
                    }
                    // walking group: one or two companions keep a slot beside/behind the leader
                    if ((kind == PK_GROUP || beat) && s.halfWidth > 0.9f) {
                        int n = beat ? 1 : 1 + (int)((h >> 13) % 2u);
                        for (int k = 0; k < n; k++) {
                            vec2 slot(k == 0 ? 0.85f : -0.85f, k == 0 ? 0.f : -0.7f);
                            vec2 fp = p3.xy() + AI::rightOf(heading) * slot.x + heading * slot.y;
                            vec3 f3(fp, groundHeight(fp.x, fp.y, p3.z + 1.f));
                            if (!freeStandingSpot(*this, f3)) continue;
                            int fid = spawnPed(randomCivilianChar(hash32(h + k * 31u) >> 3, charRole), dvec3(f3), p.yaw, fac);
                            if (fid < 0) continue;
                            if (beat) equipCop(fid);
                            peds[fid].brain.type = BRAIN_WANDER;
                            peds[fid].brain.edge = -1;
                            PedAI& fa = pedAI(fid);
                            fa.role = role;
                            fa.activity = ACT_WALK;
                            fa.leader = id;
                            fa.leaderUid = p.uid;
                            fa.slot = slot;
                            fa.actTimer = 1e4f;
                            nPeds++;
                            spawned++;
                        }
                    }
                }
                nPeds++;
                spawned++;
                break;
            }
        }
        (void)spawned;
    }
    // ------------------------------------------------------------------ traffic
    int carBudget = warm ? 4 : 0;
    if (!warm) {
        gPop.carTimer -= dt;
        if (gPop.carTimer <= 0.f) {
            gPop.carTimer = 0.15f;
            carBudget = 1;
        }
    }
    vec2 fwd = speed > 4.f ? normalize(pvel.xy()) : vec2(0);
    for (int iter = 0; iter < carBudget && nTraffic < wantTraffic && !vassets.empty(); iter++) {
        u32 h = nextSeed();
        float rMin = warm ? 25.f : (speed > 20.f ? 170.f : 95.f), rMax = warm ? 230.f : 270.f;
        float ang = hashToFloat(h) * kTwoPi;
        vec2 dir(cosf(ang), sinf(ang));
        if (length2(fwd) > 0.f && hashToFloat(hash32(h ^ 77u)) < 0.6f) dir = normalize(dir + fwd * 1.5f);   // mostly ahead when driving
        vec2 probe = center.xy() + dir * (rMin + hashToFloat(hash32(h)) * (rMax - rMin));
        float u = 0.f;
        int lane = laneGraph.nearestLane(probe, vec2(0), 40.f, &u);
        if (lane < 0) continue;
        const AI::Lane& L = laneGraph.lanes[lane];
        if ((L.flags & AI::LF_NOTRAFFIC) || ((L.flags & AI::LF_DIRT) && (h & 3) != 0)) continue;
        if (L.u1 - L.u0 < 12.f) continue;
        // keep room to stop before the end of the lane (a red light may be just ahead)
        u = Clamp(u, L.u0 + 3.f, Max(L.u0 + 3.f, L.u1 - Min(25.f, (L.u1 - L.u0) * 0.5f)));
        vec3 c = laneGraph.lanePos(lane, u);
        float d = length(c.xy() - pp.xy());
        if (!warm && inCameraView(c + vec3(0, 0, 1.f), 6.f) && d < 210.f) continue;
        if (d < (warm ? 18.f : 60.f)) continue;
        World::Region creg = map->regionAt(c.x, c.y);
        float total = 0.f;
        for (auto& a : vassets) total += trafficWeight(a.spec, creg);
        if (total <= 0.f) continue;
        float pick = hashToFloat(hash32(h * 7u)) * total;
        int model = -1;
        for (int i = 0; i < (int)vassets.size(); i++) {
            pick -= trafficWeight(vassets[i].spec, creg);
            if (pick <= 0.f) {
                model = i;
                break;
            }
        }
        if (model < 0) continue;
        const Vehicles::VehicleModel& spec = vassets[model].spec;
        if (!traffic.laneFree(lane, u, spec.boxHalf.y, 5.f)) continue;
        if (pv >= 0 && length(vehicles[pv].sim.body.pos.toVec3().xy() - c.xy()) < 10.f) continue;
        bool cop = spec.cls == Vehicles::VC_POLICE;
        bool medic = spec.cls == Vehicles::VC_AMBULANCE || spec.cls == Vehicles::VC_FIRETRUCK;
        Faction fac = cop ? FAC_POLICE : (medic ? FAC_MEDIC : FAC_CIVILIAN);
        vec2 t = laneGraph.laneTangent(lane, u);
        int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.25f), AI::dirYaw(t), true, fac);
        if (vid < 0) continue;
        Vehicle& v = vehicles[vid];
        v.faction = fac;
        v.lightsOn = tod > 19.2f || tod < 6.6f || rain > 0.5f;
        int drv = v.seats[0];
        if (drv < 0) {
            despawnVehicle(vid, true);
            continue;
        }
        Ped& dp = peds[drv];
        dp.brain.type = BRAIN_DRIVER;
        if (cop) {
            giveWeapon(drv, WPN_PISTOL, 90);
            dp.weapon = WPN_PISTOL;
            dp.brain.accuracy = 0.5f;
            if ((h >> 5) % 2 == 0) {
                int partner = spawnPed(randomCivilianChar(hash32(h * 3u), 1), v.sim.body.pos, 0.f, FAC_POLICE);
                if (partner >= 0) {
                    int seat = freeSeat(vid, false);
                    if (seat > 0) {
                        warpPedIntoVehicle(partner, vid, seat);
                        peds[partner].brain.type = BRAIN_PASSENGER;
                        giveWeapon(partner, WPN_PISTOL, 90);
                        peds[partner].weapon = WPN_PISTOL;
                        peds[partner].brain.accuracy = 0.5f;
                    } else {
                        despawnPed(partner);
                    }
                }
            }
        }
        if (!attachTraffic(vid, lane, u)) {
            despawnVehicle(vid, true);
            continue;
        }
        VehAI& va = vehAI(vid);
        if (cop) va.role = VR_POLICE;
        else if (spec.cls == Vehicles::VC_AMBULANCE) va.role = VR_AMBULANCE;
        else if (spec.cls == Vehicles::VC_FIRETRUCK) va.role = VR_FIRETRUCK;
        float v0 = Min(L.speed, 16.f) * (0.55f + hashToFloat(hash32(h * 11u)) * 0.3f);
        v0 = Min(v0, sqrtf(2.f * 3.f * Max(L.u1 - u - 8.f, 1.f)));   // able to stop comfortably at the lane end
        v.sim.body.vel = vec3(t * v0, 0.f);
        // far and unseen: start as a kinematic dummy right away (no physics until it comes near)
        if (d > 215.f && !inCameraView(c, 4.f)) traffic.toDummy(vid, v.sim);
        nTraffic++;
    }
    // ------------------------------------------------------------------ parked cars along the curbs
    int parkBudget = warm ? 3 : 0;
    if (!warm) {
        gPop.parkTimer -= dt;
        if (gPop.parkTimer <= 0.f) {
            gPop.parkTimer = 0.3f;
            parkBudget = 1;
        }
    }
    for (int iter = 0; iter < parkBudget && nParked < wantParked && !vassets.empty(); iter++) {
        u32 h = nextSeed();
        float ang = hashToFloat(h) * kTwoPi;
        float rMin = warm ? 15.f : 60.f, rMax = warm ? 150.f : 180.f;
        vec2 probe = center.xy() + vec2(cosf(ang), sinf(ang)) * (rMin + hashToFloat(hash32(h)) * (rMax - rMin));
        float u = 0.f;
        int lane = laneGraph.nearestLane(probe, vec2(0), 40.f, &u);
        if (lane < 0) continue;
        for (int guard = 0; guard < 6 && laneGraph.lanes[lane].right >= 0; guard++) lane = laneGraph.lanes[lane].right;
        const AI::Lane& L = laneGraph.lanes[lane];
        if (L.cls != World::RC_STREET && L.cls != World::RC_AVENUE && L.cls != World::RC_LANE) continue;
        const World::RoadClassInfo& info = World::roadInfo((World::RoadClass)L.cls);
        if (info.shoulder < 1.8f || L.u1 - L.u0 < 30.f) continue;
        u = Clamp(u, L.u0 + 9.f, L.u1 - 12.f);
        bool nearStop = false;
        for (float bs : L.busStops)
            if (fabsf(bs - u) < 18.f) nearStop = true;
        if (nearStop) continue;
        float lat = L.width * 0.5f + info.shoulder * 0.5f;
        vec3 c = laneGraph.lanePos(lane, u, lat);
        if (!warm && inCameraView(c + vec3(0, 0, 1.f), 4.f) && length(c.xy() - pp.xy()) < 110.f) continue;
        if (length(c.xy() - pp.xy()) < (warm ? 10.f : 45.f)) continue;
        std::vector<int> close;
        vehiclesNear(c.xy(), 6.5f, close);
        if (!close.empty()) continue;
        float total = 0.f;
        for (auto& a : vassets) total += parkedWeight(a.spec);
        if (total <= 0.f) continue;
        float pick = hashToFloat(hash32(h * 5u)) * total;
        int model = -1;
        for (int i = 0; i < (int)vassets.size(); i++) {
            pick -= parkedWeight(vassets[i].spec);
            if (pick <= 0.f) {
                model = i;
                break;
            }
        }
        if (model < 0 || vassets[model].spec.boxHalf.x > info.shoulder * 0.5f + 0.5f) continue;
        vec2 t = laneGraph.laneTangent(lane, u);
        int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.25f), AI::dirYaw(t) + (hashToFloat(hash32(h * 9u)) - 0.5f) * 0.05f, false);
        if (vid < 0) continue;
        vehicles[vid].parked = true;
        vehicles[vid].sim.engineOn = false;
        nParked++;
    }
}

float GameWorld::trafficWeight(const Vehicles::VehicleModel& m, World::Region reg) const {
    using namespace Vehicles;
    float w = m.spawnWeight;
    switch (m.cls) {
        case VC_BOAT: case VC_JETSKI: case VC_AIRBOAT: case VC_PLANE: case VC_HELI: return 0.f;
        case VC_AMBULANCE: case VC_FIRETRUCK: return w * 0.1f;
        case VC_POLICE: return w * 0.35f;
        case VC_TAXI: return (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_BEACH || reg == World::REG_MIDTOWN) ? w * 1.5f : w * 0.1f;
        case VC_BUS: return (reg == World::REG_DOWNTOWN || reg == World::REG_MIDTOWN || reg == World::REG_BEACH) ? w * 0.6f : w * 0.1f;
        case VC_TRUCK: case VC_SERVICE: return (reg == World::REG_PORT || reg == World::REG_FLATS || reg == World::REG_FORT_CASTELL) ? w * 2.f : w * 0.6f;
        case VC_PICKUP: return (reg == World::REG_FARMLAND || reg == World::REG_RIDGE || reg == World::REG_HARLOW || reg == World::REG_SAWGRASS) ? w * 3.f : w;
        case VC_SUPER: case VC_SPORTS: return (reg == World::REG_BEACH || reg == World::REG_BAY_ISLAND || reg == World::REG_KEY_CORAL || reg == World::REG_FINANCIAL) ? w * 2.5f : w * 0.6f;
        default: return w;
    }
}

float GameWorld::parkedWeight(const Vehicles::VehicleModel& m) const {
    using namespace Vehicles;
    switch (m.cls) {
        case VC_BOAT: case VC_JETSKI: case VC_AIRBOAT: case VC_PLANE: case VC_HELI: case VC_BUS: case VC_FIRETRUCK: case VC_AMBULANCE: case VC_POLICE:
        case VC_TRUCK: return 0.f;
        default: return m.spawnWeight;
    }
}

}  // namespace Game
