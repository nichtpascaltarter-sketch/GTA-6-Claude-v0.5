// Police: crime reporting (police eyewitnesses, civilian witnesses phoning it in with a delay - the call can be
// stopped), the wanted level (heat -> stars, busted, evasion outside the search area), dispatch by star level (patrol
// cars converging, roadblocks with spike strips, a helicopter with searchlight and sniper, SWAT units, boats on the
// water), pursuit driving (intercept through the road graph, PIT maneuvers, boxing in, catch-up), officers on foot
// (arrest attempts, warnings, cover behind their cars, flanking, search patterns) and responses to NPC crimes.
#include "gameworld.h"

#include "wildlife.h"

namespace Game {

namespace police_detail {

// crime types: 0 assault, 1 gunfire, 2 hit pedestrian with vehicle, 3 ram vehicle, 4 damage police vehicle,
// 5 assault officer, 6 kill officer, 7 murder, 8 explosion, 9 carjacking, 10 vehicle theft, 11 armed robbery (store hold-ups)
const float kCrimeHeat[] = {0.35f, 0.5f, 0.6f, 0.15f, 0.9f, 1.2f, 2.2f, 1.2f, 1.4f, 0.45f, 0.25f, 2.8f};
const bool kNeedsWitness[] = {true, false, true, true, false, false, false, true, false, true, true, true};
const float kStarHeat[6] = {0.f, 0.3f, 2.0f, 5.0f, 9.0f, 15.0f};


struct Roadblock {
    bool active = false;
    vec2 pos, dir;
    int cars[2] = {-1, -1};
    u32 uids[2] = {0, 0};
    bool spikes = false;
    vec3 spikeA, spikeB;   // strip end points
    float life = 0.f;
};

struct Dispatch {
    float spawnTimer = 0.f, chatterTimer = 5.f, roadblockTimer = 20.f, heliTimer = 0.f, boatTimer = 0.f, lostBarkTimer = 0.f;
    int heli = -1;
    u32 heliUid = 0;
    bool wasSeen = false;
    Roadblock blocks[2];
    Render::Model* spikeModel = nullptr;
    float bustTimer = 0.f;
    int lastWanted = 0;
    u32 counter = 1;
    vec3 lightAim;          // helicopter searchlight: where the operator is pointing (follows with a lag)
    bool lightAimSet = false;
    vec3 lastSeenVel;       // the suspect's velocity when last seen (units follow the radioed heading for a while)
};
Dispatch gD;

float searchRadiusFor(int wanted) { return 120.f + wanted * 90.f; }

// The lane position `dist` meters behind p on the road it is travelling (the lane graph walked backwards, straight-on
// predecessors first): pursuit units come up from behind on the same road, not on a parallel street or a road below.
bool laneBehind(const AI::LaneGraph& G, vec2 p, vec2 fwd, float dist, int& outLane, float& outU) {
    float u = 0.f;
    int cur = G.nearestLane(p, fwd, 20.f, &u);
    float back = 0.f;
    for (int guard = 0; guard < 14 && cur >= 0; guard++) {
        const AI::Lane& L = G.lanes[cur];
        back += Max(0.f, u - L.u0);
        if (back >= dist) {
            outLane = cur;
            outU = L.u0 + (back - dist);
            return true;
        }
        int prev = -1;
        if (L.fromNode >= 0 && L.fromNode < (int)G.nodes.size()) {
            const AI::NodeInfo& N = G.nodes[L.fromNode];
            for (int ci = N.firstConn; ci < N.firstConn + N.connCount; ci++) {
                const AI::Connector& cn = G.conns[ci];
                if (cn.to != cur || cn.from < 0) continue;
                if (prev < 0 || cn.turn == AI::TK_STRAIGHT) prev = cn.from;
            }
        }
        cur = prev;
        if (cur >= 0) u = G.lanes[cur].u1;
    }
    return false;
}

bool isCop(const Ped& p) { return p.used && p.faction == FAC_POLICE && p.health > 0.f; }

// ---- the search once the suspect is lost: the places worth a look round the last-seen position - the corners of the
// buildings (a peek along the far facade) and their doorways - handed out one per officer, nearest to them first and
// away from what the others are checking, so the units on foot fan out instead of converging on one point
struct SearchSpot {
    vec2 p;          // where to stand
    vec2 look;       // what to look along (the torch / the gun follows it)
    int by = -1;     // officer checking it
    u32 byUid = 0;
    bool checked = false;
};
struct SearchPlan {
    vec2 center;
    double made = -1e9;
    int wanted = 0;
    std::vector<SearchSpot> spots;
};
SearchPlan gS;

void buildSearchPlan(const GameWorld& g, vec2 center, int wanted) {
    gS.center = center;
    gS.made = g.time;
    gS.wanted = wanted;
    gS.spots.clear();
    const World::BuildingSet* bs = g.buildings ? g.buildings : World::gBuildings;
    if (!bs) return;
    float R = Min(searchRadiusFor(Max(wanted, 1)) * 0.5f, 75.f);
    std::vector<int> nb;
    bs->buildingsNear(center, R, nb);
    struct Cand {
        SearchSpot s;
        float d;
    };
    std::vector<Cand> cands;
    auto ok = [&](vec2 p) {
        float d = length(p - center);
        return d > 5.f && d < R && !bs->pointInBuilding(p, 0.35f) && !g.map->isWater(p.x, p.y);
    };
    for (int bi : nb) {
        const World::Building& b = bs->buildings[bi];
        vec2 X = b.ax, Y = perp(b.ax);
        // corners: stand just off the corner, look along whichever facade leads away from the last-seen point
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                vec2 corner = b.c + X * (b.hx * sx) + Y * (b.hy * sy);
                vec2 out = normalize(X * (float)sx + Y * (float)sy);
                vec2 p = corner + out * 1.8f;
                if (!ok(p)) continue;
                vec2 alongX = -X * (float)sx, alongY = -Y * (float)sy;   // the two facades meeting at the corner
                vec2 away = normalize(corner - center + vec2(1e-4f, 0.f));
                vec2 look = dot(alongX, away) > dot(alongY, away) ? alongX : alongY;
                cands.push_back({{p, look}, length(p - center)});
            }
        // the doorway on the street side (not sheds and warehouses: their fronts are loading bays)
        u8 st = b.style;
        if (st == World::BS_WAREHOUSE || st == World::BS_FACTORY || st == World::BS_BARN || st == World::BS_SHACK) continue;
        vec2 door = b.c + b.front * (b.hy + 0.45f);
        vec2 p = door + b.front * 1.6f;
        if (ok(p)) cands.push_back({{p, -b.front}, length(p - center)});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (const Cand& c : cands) {
        bool dup = false;
        for (const SearchSpot& s : gS.spots) dup |= length(s.p - c.s.p) < 4.f;
        if (!dup) gS.spots.push_back(c.s);
        if (gS.spots.size() >= 28) break;
    }
}

// ---- K9: the scent trail - where the player went on foot after the police last had eyes on them (a point every 2 m) -
// and the dog unit sent to follow it at two stars and up: the handler walks the trail behind the dog tracking ahead of
// them; close enough, the dog finds the suspect (barks: the police know where they are again); a suspect who runs for
// it within reach has the dog sent after them, which takes them to the ground
struct ScentPt {
    vec2 p;
    float z;
};
std::vector<ScentPt> gTrail;
bool gTrailEnds = false;   // the player got into a vehicle: the trail stops there
struct K9Unit {
    int handler = -1;
    u32 handlerUid = 0;
    int dog = -1;
    u32 dogUid = 0;
    int next = 0;            // trail point the handler is walking to
    float cooldown = 30.f;   // until the next unit may be sent
    float foundT = 0.f;      // the dog has had the suspect in its nose this long
    float lostT = 0.f;       // at the end of a trail that goes nowhere (drove off)
    float sentT = -1.f;      // released on the suspect this long ago (-1: on the leash)
    float workT = 0.f;       // on the trail this long (the trail goes cold after a while)
};
K9Unit gK9;

bool k9Active(const GameWorld& g) {
    return gK9.handler >= 0 && gK9.handler < (int)g.peds.size() && g.peds[gK9.handler].used && g.peds[gK9.handler].uid == gK9.handlerUid &&
           g.peds[gK9.handler].health > 0.f;
}

// K9 unit bookkeeping (every frame from updateWanted): stand the unit down when the heat is off, send one when a
// suspect on foot has been lost for a while at two stars or more (a handler with a German shepherd, on a sidewalk out of
// sight 25-45 m from where the trail starts, with a clear line to it)
void updateK9Unit(GameWorld& g, float dt, bool seen) {
    gK9.cooldown -= dt;
    Ped* pl = g.playerPed();
    bool active = k9Active(g);
    if (active && (g.pinfo.wanted <= 0 || !pl)) {
        if (Wildlife::k9Alive(gK9.dog, gK9.dogUid)) Wildlife::k9Dismiss(g, gK9.dog, gK9.dogUid);
        Ped& h = g.peds[gK9.handler];
        h.brain.type = BRAIN_GOTO;
        h.brain.target = -2;   // (no car: walks off, the dog on its leash)
        g.pedAI(gK9.handler).k9Handler = false;
        LOG("police: K9 unit stood down");
        gK9 = K9Unit();
        gK9.cooldown = 60.f;
        return;
    }
    if (active) {
        gK9.workT += dt;
        return;
    }
    if (gK9.handler >= 0) {   // the handler is down or gone: the unit is over
        if (Wildlife::k9Alive(gK9.dog, gK9.dogUid)) Wildlife::k9Dismiss(g, gK9.dog, gK9.dogUid);
        gK9 = K9Unit();
        gK9.cooldown = 45.f;
        return;
    }
    if (!pl || g.pinfo.wanted < 2 || seen || gK9.cooldown > 0.f || g.time - g.pinfo.lastSeenTime < 6.0 || gTrail.size() < 2 || g.policeSuppressed) return;
    gK9.cooldown = 5.f;   // (no spot this time: try again shortly)
    vec2 start = gTrail[0].p;
    float sz = gTrail[0].z;
    vec2 plp = pl->pos.toVec3().xy();
    for (int k = 0; k < 12; k++) {
        u32 h = hash32((u32)(g.time * 10.0) + (u32)k * 7919u);
        float ang = hashToFloat(h) * kTwoPi, r = 25.f + hashToFloat(hash32(h)) * 20.f;
        vec2 q = start + vec2(cosf(ang), sinf(ang)) * r;
        float x = 0.f;
        int wl = g.laneGraph.nearestWalk(q, 20.f, &x);
        if (wl < 0) continue;
        vec3 sp = g.laneGraph.walkPos(wl, x, 0.f, true);
        if (length(sp.xy() - plp) < 40.f || g.inCameraView(sp + vec3(0, 0, 1.f), 2.f)) continue;
        if (!g.lineOfSight(dvec3(sp + vec3(0, 0, 1.f)), dvec3(vec3(start, sz + 1.f)), -1, -1)) continue;
        vec2 to = start - sp.xy();
        int id = g.spawnPed(g.randomCivilianChar(h, 1), dvec3(sp), atan2f(-to.x, to.y), FAC_POLICE);
        if (id < 0) return;
        g.giveWeapon(id, WPN_PISTOL, 60);
        g.peds[id].weapon = WPN_PISTOL;
        g.peds[id].brain.type = BRAIN_COMBAT;
        g.peds[id].brain.target = g.player;
        PedAI& pa = g.pedAI(id);
        pa.role = PR_COP;
        pa.k9Handler = true;
        pa.homeVeh = -1;
        u32 duid = 0;
        int dog = Wildlife::spawnK9(g, id, &duid);
        if (dog < 0) {
            g.despawnPed(id);
            gK9.cooldown = 20.f;
            return;
        }
        gK9 = K9Unit();
        gK9.handler = id;
        gK9.handlerUid = g.peds[id].uid;
        gK9.dog = dog;
        gK9.dogUid = duid;
        gK9.cooldown = 60.f;
        // the dog picks the scent up where the unit meets the trail (the nearest point of it), and follows it on from there
        {
            float bd = 1e9f;
            for (int k = 0; k < (int)gTrail.size(); k++) {
                float d = length(gTrail[k].p - sp.xy());
                if (d < bd) {
                    bd = d;
                    gK9.next = k;
                }
            }
        }
        g.ai.stats.unitsSent++;
        LOG("police: K9 unit sent (officer %d, dog group %d) at %.0f %.0f, the trail %d points from %.0f %.0f", id, dog, sp.x, sp.y, (int)gTrail.size(), start.x, start.y);
        return;
    }
}

// the next place for officer `id` to check: close to them, not taken, not right next to another officer's
int claimSearchSpot(const GameWorld& g, int id) {
    const Ped& p = g.peds[id];
    vec2 pos = p.pos.toVec3().xy();
    int best = -1;
    float bestScore = 1e9f;
    for (int k = 0; k < (int)gS.spots.size(); k++) {
        const SearchSpot& s = gS.spots[k];
        if (s.checked) continue;
        bool taken = s.by >= 0 && s.by < (int)g.peds.size() && g.peds[s.by].used && g.peds[s.by].uid == s.byUid && s.by != id && isCop(g.peds[s.by]);
        if (taken) continue;
        float score = length(s.p - pos) + 0.5f * length(s.p - gS.center);
        for (const SearchSpot& o : gS.spots)
            if (&o != &s && o.by >= 0 && o.by != id && !o.checked && length(o.p - s.p) < 12.f) score += 18.f;
        if (score < bestScore) {
            bestScore = score;
            best = k;
        }
    }
    if (best >= 0) {
        gS.spots[best].by = id;
        gS.spots[best].byUid = p.uid;
    }
    return best;
}

// Where a sight line to a ped should end: the head on foot; for someone in a vehicle, a point just outside the
// vehicle's body on the observer's side (a ray to the seat would hit the car's own shell and never see them).
dvec3 sightPoint(const GameWorld& g, const Ped& t, vec3 from) {
    if (t.vehicle < 0 || t.vehicle >= (int)g.vehicles.size() || !g.vehicles[t.vehicle].used) return t.pos + dvec3(0, 0, 1.2);
    const Vehicle& v = g.vehicles[t.vehicle];
    vec3 c = v.sim.body.pos.toVec3();
    vec3 to = from - c;
    to.z = 0.f;
    vec3 dir = length(to) > 0.1f ? normalize(to) : vec3(0, 1, 0);
    float r = length(g.vassets[v.model].spec.boxHalf.xy()) + 0.35f;
    return dvec3(c + dir * r + vec3(0, 0, 0.9f));
}

}  // namespace police_detail

// How far a car starting on `lane` at u would drive to reach goal by the road network (the planner's route, the first
// and last roads counted half): units are sent from where the way in is short, not round a block of one-way streets.
float GameWorld::aiRouteLength(int lane, float u, vec2 goal) {
    AI::Driver tmp;
    tmp.path = lane;
    tmp.u = u;
    traffic.setDestination(tmp, goal);
    if (tmp.destEdges.empty() || !roads) return -1.f;
    float L = 0.f;
    for (int e : tmp.destEdges) L += roads->edges[e].length;
    L -= 0.5f * roads->edges[tmp.destEdges.front()].length;
    if (tmp.destEdges.size() >= 2) L -= 0.5f * roads->edges[tmp.destEdges.back()].length;
    return Max(L, 0.f);
}

using namespace police_detail;

// ---- an arrest made: the suspect in cuffs is walked to a patrol car and put in the back, the officer a step behind
//      with a hand on their arm; with no car close (an officer on a foot beat), one is called and the suspect sits on
//      the kerb until it pulls over next to them; the car then drives off with them
float aiCarEndToWalkRound(const GameWorld& g, int veh, float pref);   // ai.cpp: the end of a car to walk round (no car parked there)
vec2 aiWalkRound(GameWorld& g, int id, vec2 goal, float want, float dt);   // ai.cpp: round a bench / planter in the way
namespace police_statement {
bool begin(GameWorld& g, int cop, vec2 scene);   // (below: a witness's statement once the suspect is in the car)
}

namespace police_escort {

// the seat a prisoner goes in: the back where the car has one, else the front passenger's (-1: none free)
int prisonerSeat(const GameWorld& g, int vi) {
    const Vehicle& v = g.vehicles[vi];
    int n = Min((int)g.vassets[v.model].spec.seats.size(), 8);
    const int order[3] = {3, 2, 1};
    for (int k : order)
        if (k < n && v.seats[k] < 0) return k;
    return -1;
}

// arrested: in cuffs on the way to a car, in the back of a patrol car, or (an older arrest) left lying on the ground
bool inCustody(const GameWorld& g, int i) {
    const Ped& t = g.peds[i];
    if (t.brain.type == BRAIN_COWER) return true;
    if (i < (int)g.ai.ped.size() && g.ai.ped[i].uid == t.uid && g.ai.ped[i].activity == ACT_CUFFED) return true;
    return t.state == PS_INVEHICLE && t.vehicle >= 0 && t.vehicle < (int)g.vehicles.size() && g.vehicles[t.vehicle].faction == FAC_POLICE && t.faction != FAC_POLICE;
}

bool escorting(const Ped& p) { return p.brain.type == BRAIN_GOTO && p.brain.target == -3; }

// a car standing by that a prisoner can be put in: the officer's own (a seat free), else one parked by colleagues close by
int escortCarFor(const GameWorld& g, int officer) {
    const PedAI& pa = g.ai.ped[officer];
    vec2 at = g.peds[officer].pos.toVec3().xy();
    auto usable = [&](int vi) {
        if (vi < 0 || vi >= (int)g.vehicles.size() || vi >= (int)g.ai.veh.size()) return false;
        const Vehicle& v = g.vehicles[vi];
        if (!v.used || v.faction != FAC_POLICE || v.exploded || v.sim.wrecked || v.playerUsed || v.persistent || g.isAircraft(vi) || g.isBoat(vi)) return false;
        if (v.sim.speed() > 1.f || v.sim.up().z < 0.8f) return false;
        int drv = v.seats[0];
        if (drv >= 0 && (g.peds[drv].isPlayer || g.peds[drv].faction != FAC_POLICE)) return false;
        if (g.ai.veh[vi].task == PT_TRANSPORT && g.ai.veh[vi].transportFor != officer) return false;
        return prisonerSeat(g, vi) >= 0;
    };
    if (usable(pa.homeVeh) && length(g.vehicles[pa.homeVeh].sim.body.pos.toVec3().xy() - at) < 160.f) return pa.homeVeh;
    int best = -1;
    float bd = 70.f;
    for (int vi = 0; vi < (int)g.vehicles.size(); vi++) {
        if (!usable(vi) || g.vehicles[vi].seats[0] >= 0) continue;   // (one with its driver in is out on patrol: see callTransport)
        float d = length(g.vehicles[vi].sim.body.pos.toVec3().xy() - at);
        if (d < bd) {
            bd = d;
            best = vi;
        }
    }
    return best;
}

// a car for a prisoner: the nearest patrol out without a job (within 300 m) turns round for it, else one comes from
// out of view - no siren, there is no hurry
int callTransport(GameWorld& g, int officer) {
    vec2 at = g.peds[officer].pos.toVec3().xy();
    int best = -1;
    float bd = 300.f;
    for (int vi = 0; vi < (int)g.vehicles.size() && vi < (int)g.ai.veh.size(); vi++) {
        const Vehicle& v = g.vehicles[vi];
        if (!v.used || v.faction != FAC_POLICE || v.exploded || v.persistent || v.playerUsed || g.isAircraft(vi) || g.isBoat(vi)) continue;
        int drv = v.seats[0];
        if (drv < 0 || g.peds[drv].isPlayer || g.peds[drv].brain.type != BRAIN_DRIVER || g.ai.veh[vi].task != PT_NONE || g.ai.veh[vi].copBreak != 0 ||
            prisonerSeat(g, vi) < 0)
            continue;
        float d = length(v.sim.body.pos.toVec3().xy() - at);
        if (d < bd) {
            bd = d;
            best = vi;
        }
    }
    bool sent = false;
    if (best < 0) {
        int model = g.findVehicleModel(Vehicles::VC_POLICE, hash32(g.peds[officer].uid * 31u + (u32)g.time));
        for (int attempt = 0; attempt < 8 && best < 0 && model >= 0; attempt++) {
            u32 h = hash32(g.peds[officer].uid * 97u + attempt * 131u + (u32)(g.time * 3.0));
            float ang = hashToFloat(h) * kTwoPi;
            vec2 probe = at + vec2(cosf(ang), sinf(ang)) * (130.f + hashToFloat(hash32(h)) * 70.f);
            float u = 0.f;
            int lane = g.laneGraph.nearestLane(probe, vec2(0), 50.f, &u);
            if (lane < 0 || (g.laneGraph.lanes[lane].flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC | AI::LF_HIGHWAY))) continue;
            const AI::Lane& L = g.laneGraph.lanes[lane];
            u = Clamp(u, L.u0 + 4.f, L.u1 - 8.f);
            vec3 c = g.laneGraph.lanePos(lane, u);
            if (g.inCameraView(c, 8.f) || !g.traffic.laneFree(lane, u, 3.f, 6.f)) continue;
            if (attempt < 6) {   // (a way in at most 1.7 times the straight line, as the other units)
                float rl = g.aiRouteLength(lane, u, at);
                if (rl < 0.f || rl > length(c.xy() - at) * 1.7f + 40.f) continue;
            }
            vec2 t = g.laneGraph.laneTangent(lane, u);
            int vid = g.spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_POLICE);
            if (vid < 0) break;
            g.vehicles[vid].faction = FAC_POLICE;
            g.attachTraffic(vid, lane, u);
            int drv = g.vehicles[vid].seats[0];
            if (drv >= 0) g.peds[drv].brain.type = BRAIN_DRIVER;
            best = vid;
            sent = true;
        }
    }
    if (best < 0) return -1;
    VehAI& va = g.vehAI(best);
    va.role = VR_POLICE;
    va.task = PT_TRANSPORT;
    va.transportFor = officer;
    va.transportUid = g.peds[officer].uid;
    va.transportState = 0;
    va.taskTimer = 0.f;
    g.vehicles[best].sirenOn = false;
    g.vehicles[best].sirenSilent = false;
    g.ai.stats.transports++;
    LOG("police: transport for officer %d - unit %d %s, %.0f m off", officer, best, sent ? "sent from out of view" : "on patrol turns round",
        length(g.vehicles[best].sim.body.pos.toVec3().xy() - at));
    return best;
}

// the cuffs are on (police.cpp NPC arrests): the suspect becomes the officer's prisoner (false: no escort for this one)
bool startEscort(GameWorld& g, int officer, int suspect) {
    Ped& t = g.peds[suspect];
    if (t.persistent || t.isPlayer || suspect >= (int)g.ai.ped.size() || officer >= (int)g.ai.ped.size()) return false;
    PedAI& ta = g.pedAI(suspect);
    PedAI& pa = g.pedAI(officer);
    for (Incident& inc : g.ai.incidents)
        if (inc.active && inc.kind == 2 && inc.perp == suspect) inc.active = false;   // (the case is closed: no more units)
    ta.activity = ACT_CUFFED;
    ta.actTimer = 240.f;   // (nobody has come for them in four minutes: let go)
    ta.clipTimer = -1.f;
    ta.stance = 0;
    ta.leader = -1;
    ta.shoutTimer = 2.5f + hashToFloat(hash32(t.uid * 5u)) * 2.f;
    t.weapon = WPN_FISTS;
    t.aiming = t.firing = false;
    t.brain.type = BRAIN_WANDER;
    t.brain.target = officer;
    t.brain.edge = -1;
    pa.escortPed = suspect;
    pa.escortUid = t.uid;
    pa.escortT = 0.f;
    pa.stmtScene = t.pos.toVec3().xy();   // (where it went down: witnesses round here, police_statement)
    pa.escortCar = escortCarFor(g, officer);
    if (pa.escortCar < 0) pa.escortCar = callTransport(g, officer);
    pa.escortCarUid = pa.escortCar >= 0 ? g.vehicles[pa.escortCar].uid : 0u;
    pa.shoutTimer = 4.f;
    Ped& op = g.peds[officer];
    op.weapon = WPN_FISTS;   // (the gun holstered: the hands for the prisoner - drawn again if it comes to it)
    op.aiming = op.firing = false;
    Brain& b = op.brain;
    b.type = BRAIN_GOTO;
    b.target = -3;
    vec3 sp = t.pos.toVec3();
    LOG("police: officer %d cuffs suspect %d at %.0f %.0f - to %s %d", officer, suspect, sp.x, sp.y,
        pa.escortCar < 0 ? "no car yet" : (g.ai.veh[pa.escortCar].task == PT_TRANSPORT ? "the transport" : "the patrol car"), pa.escortCar);
    return true;
}

// let a transport go back to its patrol
void releaseTransport(GameWorld& g, int vi, int officer) {
    if (vi < 0 || vi >= (int)g.vehicles.size() || vi >= (int)g.ai.veh.size() || !g.vehicles[vi].used) return;
    VehAI& va = g.ai.veh[vi];
    if (va.task != PT_TRANSPORT || va.transportFor != officer) return;
    va.task = PT_NONE;
    va.transportFor = -1;
    va.transportState = 0;
    Vehicle& v = g.vehicles[vi];
    v.parked = false;
    v.sirenOn = v.sirenSilent = false;
    v.indicator = 0;
    if (AI::Driver* d = g.traffic.get(vi)) {
        d->mode = AI::DM_NORMAL;
        d->hasDest = false;
        d->holdTimer = 0.f;
    }
}

// the officer walking their prisoner to the car (brain GOTO -3): a step behind them and a little to the side, a hand
// on the arm, at their pace; standing over them while they are down after the tackle or sitting on the kerb waiting
// for the car; once the door is shut on them, into the front and away (or, the car having its own crew, back to the beat)
void escortStep(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    Brain& b = p.brain;
    pa.escortT += dt;
    int s = pa.escortPed;
    bool ok = s >= 0 && s < (int)g.peds.size() && s < (int)g.ai.ped.size() && g.peds[s].used && g.peds[s].uid == pa.escortUid && g.peds[s].health > 0.f &&
              g.peds[s].state != PS_DEAD;
    int car = pa.escortCar;
    bool carOk = car >= 0 && car < (int)g.vehicles.size() && car < (int)g.ai.veh.size() && g.vehicles[car].used && g.vehicles[car].uid == pa.escortCarUid &&
                 !g.vehicles[car].exploded && g.vehicles[car].faction == FAC_POLICE &&
                 (g.ai.veh[car].task == PT_TRANSPORT ? g.ai.veh[car].transportFor == id : g.vehicles[car].sim.speed() < 1.5f);   // (not one driving off)
    if (ok && g.peds[s].state == PS_INVEHICLE) {
        // in, the door shut on them
        int in = g.peds[s].vehicle;
        bool ownCrew = in >= 0 && in < (int)g.vehicles.size() && g.vehicles[in].seats[0] >= 0 && g.vehicles[in].seats[0] != id;
        LOG("police: officer %d has suspect %d in unit %d (%.0f s after the cuffs)%s", id, s, in, pa.escortT, ownCrew ? " - back to the beat" : "");
        g.aiSay(id, BK_COP_ESCORT, 0.6f);
        pa.escortPed = -1;
        pa.escortCar = -1;
        if (ownCrew && pa.homeVeh != in) {
            // a transport with its own crew: it takes them, this officer goes back to walking the beat
            releaseTransport(g, in, id);
            b.type = BRAIN_WANDER;
            b.target = -1;
            b.edge = -1;
            pa.navOk = false;
            pa.activity = ACT_WALK;
        } else {
            pa.homeVeh = in;
            b.type = BRAIN_GOTO;
            b.target = -2;
            pa.tactic = FT_RETURN;
        }
        police_statement::begin(g, id, pa.stmtScene);   // (first a word with somebody who saw it, if nobody has yet)
        return;
    }
    if (ok && (g.ai.ped[s].activity != ACT_CUFFED || g.ai.ped[s].uid != g.peds[s].uid)) ok = false;
    if (!ok || pa.escortT > 260.f) {
        // lost them (ran for it, hurt, gone) or nothing came: back to the car / the beat
        if (carOk) releaseTransport(g, car, id);
        pa.escortPed = -1;
        pa.escortCar = -1;
        b.type = BRAIN_GOTO;
        b.target = -2;
        pa.tactic = FT_RETURN;
        return;
    }
    if (!carOk) {
        // no car (yet), or the one coming was taken off it: another every 15 s
        pa.escortCar = -1;
        if (fmodf(pa.escortT, 15.f) < dt) {
            int c = escortCarFor(g, id);
            if (c < 0) c = callTransport(g, id);
            pa.escortCar = c;
            pa.escortCarUid = c >= 0 ? g.vehicles[c].uid : 0u;
        }
    } else {
        g.ai.veh[car].escortHold = g.time + 2.0;   // (the crew waits by it for them)
    }
    const Ped& sp = g.peds[s];
    vec2 pos = p.pos.toVec3().xy(), spos = sp.pos.toVec3().xy();
    // the car a long walk off, a crewmate going back to it (a statement taken first, or on the way): wait here with the
    // prisoner on the kerb - the crewmate brings it round (aiPoliceBrain: back at the car, held for this officer)
    pa.escortWait = false;
    if (carOk && g.ai.veh[car].task != PT_TRANSPORT && pa.escortT < 150.f && g.vehicles[car].seats[0] < 0 && !g.vehicles[car].playerUsed &&
        !g.isAircraft(car) && !g.isBoat(car) && length(g.vehicles[car].sim.body.pos.toVec3().xy() - spos) > 45.f)
        for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size() && !pa.escortWait; i++) {
            const Ped& c = g.peds[i];
            const PedAI& ca = g.ai.ped[i];
            pa.escortWait = i != id && c.used && ca.uid == c.uid && c.faction == FAC_POLICE && c.state == PS_ONFOOT && c.health > 0.f && ca.homeVeh == car &&
                            c.brain.type == BRAIN_GOTO && (c.brain.target == -2 || c.brain.target == -4);
        }
    bool ready = carOk && !pa.escortWait && (g.ai.veh[car].task != PT_TRANSPORT || g.ai.veh[car].transportState == 2);
    if (floorf((float)g.time + id * 0.37f) != floorf((float)g.time - dt + id * 0.37f))
        g.aiStimulus(sp.pos, STIM_ARREST, id, 28.f, false);   // (people stop to watch: pedai.cpp)
    vec2 desired(0.f);
    float faceYaw = p.yaw;
    if (sp.state == PS_RAGDOLL || sp.state == PS_GETUP || !ready || g.ai.ped[s].clipTimer > 0.f) {
        // down after the tackle, waiting for the car, getting in: standing over them (a hand on their head as they duck
        // in - peds.cpp holds the reach)
        vec2 to = spos - pos;
        float d = length(to);
        if (d > 1.5f) desired = to / d * Min(2.6f, d * 1.5f);
        faceYaw = atan2f(-to.x, to.y);
        if (g.ai.ped[s].clipTimer > 0.f && d < 1.6f && sp.state == PS_ONFOOT && !sp.ragdoll) {
            pa.reachAt = g.pedHeadPos(sp) + vec3(0.f, 0.f, 0.12f);
            pa.reachT = g.time + 0.2;
        }
        if (!ready && pa.shoutTimer <= 0.f && sp.state == PS_ONFOOT) {
            g.aiSay(id, BK_COP_TRANSPORT, 0.5f);   // (on the radio for the car; a word to the prisoner)
            pa.shoutTimer = 12.f + hashToFloat(hash32(p.uid + (u32)g.time)) * 6.f;
        }
    } else {
        // behind them, a little to the right: the left hand on their right upper arm (peds.cpp feeds the animator's hold;
        // 0.56 m back and 0.3 m over keeps them just outside the ped separation)
        vec2 sf = AI::yawDir(sp.yaw);
        vec2 goal = spos - sf * 0.56f + AI::rightOf(sf) * 0.3f;
        // at the car (round its end to the back door): at their back on the outside, never pinned against the bodywork -
        // and round the car's end, not through it, when it stands between
        const Vehicles::VehicleModel& cs = g.vassets[g.vehicles[car].model].spec;
        vec2 cp = g.vehicles[car].sim.body.pos.toVec3().xy();
        if (length(spos - cp) < cs.boxHalf.y + 2.5f) {
            goal = spos + normalize(spos - cp + vec2(1e-4f, 0.f)) * 0.75f;
            vec2 cf = normalize(g.vehicles[car].sim.forward().xy() + vec2(1e-4f, 0.f)), cr = AI::rightOf(cf);
            vec2 a = pos - cp, bq = goal - cp;
            float ax = dot(a, cr), bx = dot(bq, cr), ay = dot(a, cf);
            if (ax * bx < 0.f && fabsf(ax) > cs.boxHalf.x * 0.5f) {
                float endSign = aiCarEndToWalkRound(g, car, ay >= 0.f ? 1.f : -1.f);
                float side = fabsf(ay) < cs.boxHalf.y + 0.6f || ay * endSign < 0.f ? (ax >= 0.f ? 1.f : -1.f) : (bx >= 0.f ? 1.f : -1.f);
                goal = cp + cf * (endSign * (cs.boxHalf.y + 0.8f)) + cr * (side * (cs.boxHalf.x + 0.6f));
            }
        }
        float d = length(goal - pos);
        float sv = length(sp.vel.xy());
        // (round a bench or a planter corner they have just gone round, not into it: ai.cpp)
        vec2 to = aiWalkRound(g, id, goal, d > 0.5f ? Min(3.f, sv + d * 2.2f) : 0.f, dt) - pos;
        float ds = length(to);
        if (ds > 0.06f) desired = to / ds * Min(3.f, sv + Max(d, ds) * 2.2f);
        faceYaw = d > 1.2f || ds > d + 0.3f ? atan2f(-to.x, to.y) : sp.yaw;
        if (pa.shoutTimer <= 0.f) {
            g.aiSay(id, BK_COP_ESCORT, 0.45f);
            pa.shoutTimer = 8.f + hashToFloat(hash32(p.uid + (u32)g.time)) * 6.f;
        }
    }
    p.aiming = false;
    p.firing = false;
    p.animIn.stance = 0;
    p.animIn.crouch = false;
    float dy = AI::wrapPi(faceYaw - p.yaw);
    p.yaw = AI::wrapPi(p.yaw + Clamp(dy, -7.f * dt, 7.f * dt));
    g.movePed(p, desired, dt, false);
}

}  // namespace police_escort

// ---- a statement: with the suspect in cuffs, an officer who is not walking them (the partner, or the one who made the
//      arrest once the prisoner is in the car) takes a statement from somebody who saw it - walks up to them, asks, takes
//      the answer down (the notepad held at the chest), a point at where it happened, a word of thanks - then back to
//      the car, which waits for them (VehAI::escortHold). One per scene.
namespace police_statement {

struct Scene {
    vec2 pos;
    double t;
};
std::vector<Scene> gScenes;   // statements taken lately

bool taking(const Ped& p) { return p.brain.type == BRAIN_GOTO && p.brain.target == -4; }

// still standing there for this officer
bool witnessOk(const GameWorld& g, int w, u32 uid, int cop) {
    if (w < 0 || w >= (int)g.peds.size() || w >= (int)g.ai.ped.size()) return false;
    const Ped& q = g.peds[w];
    const PedAI& qa = g.ai.ped[w];
    return q.used && q.uid == uid && qa.uid == q.uid && q.health > 0.f && q.state == PS_ONFOOT && q.brain.type == BRAIN_WANDER &&
           qa.activity == ACT_STATEMENT && qa.stmtWith == cop;
}

// somebody close to where it happened with nothing better to do - the ones who stopped to watch it first
int pickWitness(const GameWorld& g, int cop, vec2 scene) {
    vec2 at = g.peds[cop].pos.toVec3().xy();
    int best = -1;
    float bs = 1e9f;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& q = g.peds[i];
        const PedAI& qa = g.ai.ped[i];
        if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || q.health <= 0.f || qa.uid != q.uid) continue;
        if (q.brain.type != BRAIN_WANDER || qa.eventId >= 0 || qa.stmtWith >= 0 || qa.leader >= 0 || qa.role == PR_DRUNK) continue;
        if (qa.activity != ACT_WALK && qa.activity != ACT_SCENARIO && qa.activity != ACT_FILM && qa.activity != ACT_INSPECT && qa.activity != ACT_WATCH) continue;
        if (qa.activity == ACT_WALK && qa.walk.state != AI::WS_WALK && qa.walk.state != AI::WS_WAIT_CROSS && qa.walk.state != AI::WS_IDLE) continue;
        vec2 qp = q.pos.toVec3().xy();
        float ds = length(qp - scene), dc = length(qp - at);
        if (ds > 30.f || dc > 40.f) continue;
        float u = 0.f, lat = 0.f;   // (not one standing out in the road: they would stop there)
        int ln = g.laneGraph.nearestLane(qp, vec2(0.f), 4.f, &u, &lat);
        if (ln >= 0 && fabsf(lat) < g.laneGraph.lanes[ln].width * 0.5f + 0.4f) continue;
        bool saw = g.time - qa.sceneT < 120.0;   // (stopped to watch the arrest: pedai.cpp STIM_ARREST)
        float score = dc + ds * 0.5f - (saw ? 25.f : 0.f);
        if (score < bs) {
            bs = score;
            best = i;
        }
    }
    return best;
}

// an officer on foot asks round (false: nobody there, or a statement taken here already)
bool begin(GameWorld& g, int cop, vec2 scene) {
    if (cop < 0 || cop >= (int)g.peds.size() || cop >= (int)g.ai.ped.size()) return false;
    Ped& p = g.peds[cop];
    PedAI& pa = g.pedAI(cop);
    if (p.persistent || p.isPlayer || p.state != PS_ONFOOT || p.health <= 0.f || pa.k9Handler || g.pinfo.wanted > 0) return false;
    if (scene.x > 1e8f) return false;   // (no scene to ask about: a warrant turned up at a stop)
    for (size_t k = 0; k < gScenes.size();) {
        if (g.time - gScenes[k].t > 150.0 || g.time < gScenes[k].t) gScenes.erase(gScenes.begin() + k);
        else k++;
    }
    for (const Scene& s : gScenes)
        if (length(s.pos - scene) < 45.f) return false;
    int w = pickWitness(g, cop, scene);
    if (w < 0) return false;
    gScenes.push_back({scene, g.time});
    Ped& q = g.peds[w];
    PedAI& qa = g.pedAI(w);
    pa.stmtWith = w;
    pa.stmtUid = q.uid;
    pa.stmtT = 0.f;
    pa.stmtScene = scene;
    qa.stmtWith = cop;
    qa.stmtUid = p.uid;
    qa.stmtT = 0.f;
    qa.stmtScene = scene;
    qa.activity = ACT_STATEMENT;
    qa.actTimer = 95.f;   // (the officer never got there: on their way)
    qa.anchor = q.pos.toVec3().xy();
    qa.anchorYaw = q.yaw;
    qa.stance = 0;
    qa.clip = -1;
    qa.browsing = false;
    p.weapon = WPN_FISTS;   // (holstered; drawn again if it comes to it)
    p.aiming = p.firing = false;
    p.brain.type = BRAIN_GOTO;
    p.brain.target = -4;
    g.ai.stats.statements++;
    float d = length(q.pos.toVec3().xy() - p.pos.toVec3().xy());
    if (d > 6.f) g.aiSay(cop, BK_COP_STATEMENT, 0.7f);   // (a call across: a moment, please)
    LOG("police: officer %d takes a statement from %d (%.0f m off, %.0f m from the scene%s)", cop, w, d, length(q.pos.toVec3().xy() - scene),
        g.time - qa.sceneT < 120.0 ? ", watched it" : "");
    return true;
}

// done (or the witness went): the witness on their way, the officer back to the car - or the beat
void finish(GameWorld& g, int cop) {
    Ped& p = g.peds[cop];
    PedAI& pa = g.pedAI(cop);
    int w = pa.stmtWith;
    if (witnessOk(g, w, pa.stmtUid, cop)) {
        PedAI& qa = g.pedAI(w);
        qa.activity = ACT_WALK;
        qa.actTimer = 15.f + hashToFloat(hash32(g.peds[w].uid + (u32)g.time)) * 15.f;
        qa.stmtWith = -1;
        qa.stance = 0;
        qa.sceneT = (float)g.time;   // (seen enough of this one)
    }
    pa.stmtWith = -1;
    p.phoneBrowse = false;
    p.animIn.stance = 0;
    Brain& b = p.brain;
    int hv = pa.homeVeh;
    if (hv >= 0 && hv < (int)g.vehicles.size() && g.vehicles[hv].used && !g.vehicles[hv].exploded && g.vehicles[hv].faction == FAC_POLICE) {
        b.type = BRAIN_GOTO;
        b.target = -2;
        pa.tactic = FT_RETURN;
    } else {
        b.type = BRAIN_WANDER;
        b.target = -1;
        b.edge = -1;
        pa.navOk = false;
        pa.activity = ACT_WALK;
    }
}

// the officer (brain GOTO -4): up to the witness, then the questions and the notes; the witness's clock (stmtT) runs
// from the moment the officer is in front of them and paces both (pedai.cpp ACT_STATEMENT)
void step(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    pa.stmtT += dt;
    int w = pa.stmtWith;
    if (!witnessOk(g, w, pa.stmtUid, id) || pa.stmtT > 80.f || g.pinfo.wanted > 0) {
        finish(g, id);
        return;
    }
    int hv = pa.homeVeh;
    if (hv >= 0 && hv < (int)g.vehicles.size() && hv < (int)g.ai.veh.size() && g.vehicles[hv].used)
        g.ai.veh[hv].escortHold = Max(g.ai.veh[hv].escortHold, g.time + 2.0);   // (the car waits for this officer)
    const Ped& q = g.peds[w];
    PedAI& qa = g.pedAI(w);
    vec2 pos = p.pos.toVec3().xy(), qp = q.pos.toVec3().xy();
    vec2 to = qp - pos;
    float d = length(to);
    vec2 desired(0.f);
    float faceYaw = d > 0.05f ? atan2f(-to.x, to.y) : p.yaw;
    int stance = 0;
    bool notes = false;
    float len = 15.f + hashToFloat(hash32(p.uid * 7u + q.uid)) * 7.f;   // (how long it takes)
    if (qa.stmtT <= 0.f && (d > 1.5f || pa.stmtT < 0.2f)) {
        // walking up (stopping at a conversational distance; round a bench or a planter on the way)
        if (d > 1.3f) {
            float spd = d > 8.f ? 1.9f : Clamp((d - 1.2f) * 1.6f + 0.6f, 0.6f, 1.5f);
            vec2 st = aiWalkRound(g, id, qp, spd, dt) - pos;
            float ls = length(st);
            if (ls > 1e-3f) desired = st / ls * spd;
            if (d > 3.f && ls > 1e-3f) faceYaw = atan2f(-st.x, st.y);
        }
        if (pa.stmtT > 45.f) {   // (never got there)
            LOG("police: officer %d never got to witness %d (%.0f m off)", id, w, d);
            finish(g, id);
            return;
        }
    } else {
        qa.stmtT += dt;
        float T = qa.stmtT;
        if (d > 1.9f) desired = to / d * 0.8f;   // (closing up if they shuffled back)
        // a question, the answer taken down; a second question halfway; the thanks at the end
        bool asking = T < 3.f || (T > len * 0.55f && T < len * 0.55f + 2.6f);
        if (asking) {
            stance = 7;
            if ((T - dt < 0.3f && T >= 0.3f) || (T - dt < len * 0.55f + 0.2f && T >= len * 0.55f + 0.2f)) g.aiSay(id, BK_COP_STATEMENT, 0.9f, true);
        } else {
            notes = true;
        }
        if (T >= len) {
            g.aiSay(id, BK_COP_STATEMENT_END, 1.f, true);
            LOG("police: officer %d done with witness %d after %.0f s", id, w, pa.stmtT);
            finish(g, id);
            return;
        }
    }
    p.phoneBrowse = notes;
    p.aiming = false;
    p.firing = false;
    p.animIn.stance = stance;
    p.animIn.crouch = false;
    float dy = AI::wrapPi(faceYaw - p.yaw);
    p.yaw = AI::wrapPi(p.yaw + Clamp(dy, -6.f * dt, 6.f * dt));
    g.movePed(p, desired, dt, false);
}

}  // namespace police_statement

// ---- a sidewalk stop: officers walking a beat stop somebody coming their way now and then - a word, the ID handed over
//      and run on the officer's device while the partner stands off to the side - then on their way. Now and then the
//      check turns up a warrant (the cuffs and a car called: police_escort), or the one stopped bolts while the officer
//      looks down at the screen (the chase on foot and the tackle, as for any suspect on the run).
namespace police_stop {

struct State {
    float next = 15.f;          // s to the next look round for somebody to stop
    double lastStop = -1e9;     // when the last one began (one every couple of minutes at most)
};
State gStop;

bool stopping(const Ped& p) { return p.brain.type == BRAIN_GOTO && p.brain.target == -5; }

// still standing there for this officer (the one doing the talking)
bool subjectOk(const GameWorld& g, int s, u32 uid, int cop) {
    if (s < 0 || s >= (int)g.peds.size() || s >= (int)g.ai.ped.size()) return false;
    const Ped& q = g.peds[s];
    const PedAI& qa = g.ai.ped[s];
    return q.used && q.uid == uid && qa.uid == q.uid && q.health > 0.f && q.state == PS_ONFOOT && q.brain.type == BRAIN_WANDER &&
           qa.activity == ACT_STOPPED && qa.stopPed == cop;
}

// an officer walking a beat (the pair's lead: nobody it follows), free for it
bool beatLead(const GameWorld& g, int i) {
    const Ped& p = g.peds[i];
    if (!p.used || p.isPlayer || p.persistent || p.faction != FAC_POLICE || p.state != PS_ONFOOT || p.health <= 0.f || i >= (int)g.ai.ped.size()) return false;
    const PedAI& pa = g.ai.ped[i];
    return pa.uid == p.uid && p.brain.type == BRAIN_WANDER && pa.role == PR_COP && pa.activity == ACT_WALK && pa.leader < 0 && !pa.k9Handler && pa.navOk;
}

// the partner walking the beat with this officer (its follower), -1 none
int partnerOf(const GameWorld& g, int cop) {
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& q = g.peds[i];
        const PedAI& qa = g.ai.ped[i];
        if (q.used && i != cop && q.faction == FAC_POLICE && q.health > 0.f && q.state == PS_ONFOOT && qa.uid == q.uid && qa.leader == cop &&
            qa.leaderUid == g.peds[cop].uid && q.brain.type == BRAIN_WANDER)
            return i;
    }
    return -1;
}

// somebody on their own walking the sidewalk a few metres ahead of this officer
int pickSubject(const GameWorld& g, int cop, float range) {
    const Ped& c = g.peds[cop];
    vec2 cp = c.pos.toVec3().xy(), cf = AI::yawDir(c.yaw);
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& q = g.peds[i];
        const PedAI& qa = g.ai.ped[i];
        if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || q.health <= 0.f || qa.uid != q.uid) continue;
        if (q.brain.type != BRAIN_WANDER || qa.activity != ACT_WALK || qa.walk.state != AI::WS_WALK || qa.leader >= 0 || qa.eventId >= 0 ||
            qa.stmtWith >= 0 || qa.goInside || qa.greetWith >= 0 || qa.role == PR_JOGGER || qa.role == PR_TOURIST || qa.role == PR_BUSINESS)
            continue;
        vec2 to = q.pos.toVec3().xy() - cp;
        float d = length(to);
        if (d < 3.f || d > range || dot(to, cf) < d * 0.5f || d >= bd) continue;   // (ahead of the officer)
        float u = 0.f, lat = 0.f;   // (on the sidewalk, not out in the road)
        int ln = g.laneGraph.nearestLane(q.pos.toVec3().xy(), vec2(0.f), 4.f, &u, &lat);
        if (ln >= 0 && fabsf(lat) < g.laneGraph.lanes[ln].width * 0.5f + 0.4f) continue;
        bool across = false;   // (and on the officer's side of the street: no road - and its parked cars - between them)
        for (int k = 1; k <= 3 && !across; k++) {
            vec2 m = cp + to * (k * 0.25f);
            int lm = g.laneGraph.nearestLane(m, vec2(0.f), 4.f, &u, &lat);
            across = lm >= 0 && fabsf(lat) < g.laneGraph.lanes[lm].width * 0.5f;
        }
        if (across) continue;
        bool leads = false;   // (not one with company following them)
        for (int j = 0; j < (int)g.ai.ped.size() && j < (int)g.peds.size() && !leads; j++)
            leads = g.peds[j].used && g.ai.ped[j].leader == i && g.ai.ped[j].leaderUid == q.uid;
        if (leads) continue;
        bd = d;
        best = i;
    }
    return best;
}

void begin(GameWorld& g, int cop, int s, int outcome) {
    Ped& c = g.peds[cop];
    PedAI& ca = g.pedAI(cop);
    Ped& q = g.peds[s];
    PedAI& qa = g.pedAI(s);
    int partner = partnerOf(g, cop);
    ca.stopPed = s;
    ca.stopUid = q.uid;
    ca.stopT = 0.f;
    ca.stopOutcome = (u8)Clamp(outcome, 0, 2);
    ca.stopCover = false;
    c.brain.type = BRAIN_GOTO;
    c.brain.target = -5;
    c.weapon = WPN_FISTS;
    c.aiming = c.firing = false;
    if (partner >= 0) {
        PedAI& pa = g.pedAI(partner);
        pa.stopPed = s;
        pa.stopUid = q.uid;
        pa.stopT = 0.f;
        pa.stopCover = true;
        g.peds[partner].brain.type = BRAIN_GOTO;
        g.peds[partner].brain.target = -5;   // (the pair's slot is kept: walking on together afterwards)
    }
    qa.activity = ACT_STOPPED;
    qa.stopPed = cop;
    qa.stopUid = c.uid;
    qa.stopT = 0.f;
    qa.actTimer = 90.f;   // (the officer never got to them: on their way)
    qa.stance = 0;
    qa.clip = -1;
    gStop.lastStop = g.time;
    g.ai.stats.stops++;
    g.aiSay(cop, BK_COP_STOP, 0.9f, true);   // (a call: a moment, please)
    LOG("police: officer %d stops %d on the sidewalk (%.0f m off, partner %d, %s)", cop, s, length(rel(q.pos, c.pos)), partner,
        outcome == 1 ? "a warrant" : (outcome == 2 ? "will run" : "nothing on them"));
}

// over: the one stopped on their way (when still there and `subjectToo`), the partner back at the officer's side, both on
// along the beat
void release(GameWorld& g, int cop, bool subjectToo) {
    Ped& c = g.peds[cop];
    PedAI& ca = g.pedAI(cop);
    int s = ca.stopPed;
    if (subjectToo && subjectOk(g, s, ca.stopUid, cop)) {
        PedAI& qa = g.pedAI(s);
        qa.activity = ACT_WALK;
        qa.stopPed = -1;
        qa.stance = 0;
        qa.actTimer = 15.f + hashToFloat(hash32(g.peds[s].uid + (u32)g.time)) * 15.f;
        g.peds[s].phoneBrowse = false;
    }
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        Ped& o = g.peds[i];
        PedAI& oa = g.ai.ped[i];
        if (!o.used || i == cop || oa.uid != o.uid || !stopping(o) || !oa.stopCover || oa.stopPed != s || oa.stopUid != ca.stopUid) continue;
        o.brain.type = BRAIN_WANDER;
        o.brain.target = -1;
        o.brain.edge = -1;
        oa.stopPed = -1;
        oa.stopCover = false;
        oa.navOk = false;
        oa.activity = ACT_WALK;
        o.animIn.stance = 0;
    }
    ca.stopPed = -1;
    c.phoneBrowse = false;
    c.animIn.stance = 0;
    c.brain.type = BRAIN_WANDER;
    c.brain.target = -1;
    c.brain.edge = -1;
    ca.navOk = false;
    ca.activity = ACT_WALK;
}

// the partner (stopCover): off to the side of the one stopped, square to the officer talking, watching them
void coverStep(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    int s = pa.stopPed;
    bool ok = s >= 0 && s < (int)g.peds.size() && s < (int)g.ai.ped.size() && g.peds[s].used && g.peds[s].uid == pa.stopUid &&
              g.ai.ped[s].uid == g.peds[s].uid && g.ai.ped[s].activity == ACT_STOPPED;
    int lead = ok ? g.ai.ped[s].stopPed : -1;
    ok = ok && lead >= 0 && lead < (int)g.peds.size() && g.peds[lead].used && g.peds[lead].uid == g.ai.ped[s].stopUid && stopping(g.peds[lead]);
    if (!ok || g.pinfo.wanted > 0) {
        p.brain.type = BRAIN_WANDER;
        p.brain.target = -1;
        p.brain.edge = -1;
        pa.stopPed = -1;
        pa.stopCover = false;
        pa.navOk = false;
        pa.activity = ACT_WALK;
        p.animIn.stance = 0;
        return;
    }
    vec2 pos = p.pos.toVec3().xy(), sp = g.peds[s].pos.toVec3().xy(), lp = g.peds[lead].pos.toVec3().xy();
    vec2 fromLead = sp - lp;
    fromLead = length2(fromLead) > 1e-4f ? normalize(fromLead) : AI::yawDir(g.peds[s].yaw);
    vec2 side = AI::rightOf(fromLead) * ((p.uid & 1) ? 1.f : -1.f);
    vec2 goal = sp + side * 2.1f - fromLead * 0.9f;   // (to the side and a little back toward the officer talking)
    vec2 to = goal - pos;
    float d = length(to);
    vec2 desired(0.f);
    if (d > 0.4f) desired = to / d * Clamp(d * 1.4f, 0.5f, 1.6f);
    vec2 look = sp - pos;
    float faceYaw = d > 1.5f ? atan2f(-desired.x, desired.y) : atan2f(-look.x, look.y);
    p.aiming = p.firing = false;
    p.animIn.stance = 0;
    p.animIn.crouch = false;
    float dy = AI::wrapPi(faceYaw - p.yaw);
    p.yaw = AI::wrapPi(p.yaw + Clamp(dy, -6.f * dt, 6.f * dt));
    g.movePed(p, desired, dt, false);
}

// the officer doing the talking (brain GOTO -5): up to them, the questions, the ID run on the device; the stopped ped's
// clock (stopT) runs from the moment the officer is in front of them and paces all three (pedai.cpp ACT_STOPPED)
void step(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    if (pa.stopCover) {
        coverStep(g, id, dt);
        return;
    }
    pa.stopT += dt;
    int s = pa.stopPed;
    if (!subjectOk(g, s, pa.stopUid, id) || pa.stopT > 75.f || g.pinfo.wanted > 0) {
        bool there = s >= 0 && s < (int)g.peds.size() && s < (int)g.ai.ped.size() && g.peds[s].used && g.peds[s].uid == pa.stopUid;
        LOG("police: the stop of %d by officer %d is off after %.0f s (%s)", s, id, pa.stopT,
            g.pinfo.wanted > 0 ? "a wanted player" : (!there ? "they are gone" : (pa.stopT > 75.f ? "too long" : StrFormat("they are busy: activity %d brain %d state %d", (int)g.ai.ped[s].activity, (int)g.peds[s].brain.type, (int)g.peds[s].state).c_str())));
        release(g, id, true);
        return;
    }
    Ped& q = g.peds[s];
    PedAI& qa = g.pedAI(s);
    vec2 pos = p.pos.toVec3().xy(), qp = q.pos.toVec3().xy();
    vec2 to = qp - pos;
    float d = length(to);
    vec2 desired(0.f);
    float faceYaw = d > 0.05f ? atan2f(-to.x, to.y) : p.yaw;
    int stance = 0;
    bool device = false;
    if (qa.stopT <= 0.f && (d > 1.45f || pa.stopT < 0.2f)) {
        // walking up to them (they have stopped and turned round; round a bench or a planter on the way)
        if (d > 1.25f) {
            float spd = d > 6.f ? 1.6f : Clamp((d - 1.15f) * 1.6f + 0.5f, 0.5f, 1.4f);
            vec2 st = aiWalkRound(g, id, qp, spd, dt) - pos;
            float ls = length(st);
            if (ls > 1e-3f) desired = st / ls * spd;
            if (d > 3.f && ls > 1e-3f) faceYaw = atan2f(-st.x, st.y);
        }
        if (pa.stopT > 25.f) {
            LOG("police: officer %d never got to %d for the stop (%.0f m off)", id, s, d);
            release(g, id, true);
            return;
        }
    } else {
        qa.stopT += dt;
        float T = qa.stopT;
        if (d > 1.9f) desired = to / d * 0.8f;
        if (T < 3.f) {
            stance = 7;   // where are you headed, ID please
            if (T - dt < 0.3f && T >= 0.3f) g.aiSay(id, BK_COP_STOP_ASK, 1.f, true);
        } else if (T >= 4.6f && T < 12.6f) {
            device = true;   // running it
        }
        if (pa.stopOutcome == 2 && T >= 9.5f) {
            // they bolt while the officer looks down at the screen: after them, both
            LOG("police: %d runs from the stop (officer %d)", s, id);
            g.ai.stats.stopRuns++;
            q.phoneBrowse = false;
            qa.activity = ACT_WALK;
            qa.stopPed = -1;
            q.brain.type = BRAIN_FLEE;
            q.brain.target = id;
            q.brain.timer = 0.f;
            g.aiSay(s, BK_FLEE, 1.f, true);
            int partner = -1;
            for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++)
                if (g.peds[i].used && i != id && g.ai.ped[i].uid == g.peds[i].uid && stopping(g.peds[i]) && g.ai.ped[i].stopCover && g.ai.ped[i].stopPed == s)
                    partner = i;
            for (int o : {id, partner}) {
                if (o < 0) continue;
                PedAI& oa = g.pedAI(o);
                oa.stopPed = -1;
                oa.stopCover = false;
                g.peds[o].phoneBrowse = false;
                g.peds[o].animIn.stance = 0;
                g.peds[o].brain.type = BRAIN_COMBAT;
                g.peds[o].brain.target = s;
                g.peds[o].brain.timer = 0.f;
                g.peds[o].brain.accuracy = 0.4f;
            }
            g.aiSay(id, BK_COP_FREEZE, 1.f, true);
            return;
        }
        if (T >= 12.6f) {
            if (pa.stopOutcome == 1) {
                // a warrant: the cuffs (a car is called for them - police_escort); the partner walks on
                LOG("police: the stop of %d turns up a warrant (officer %d)", s, id);
                g.ai.stats.stopArrests++;
                q.phoneBrowse = false;
                p.phoneBrowse = false;
                for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
                    Ped& o = g.peds[i];
                    PedAI& oa = g.ai.ped[i];
                    if (!o.used || i == id || oa.uid != o.uid || !stopping(o) || !oa.stopCover || oa.stopPed != s) continue;
                    o.brain.type = BRAIN_WANDER;
                    o.brain.target = -1;
                    o.brain.edge = -1;
                    oa.stopPed = -1;
                    oa.stopCover = false;
                    oa.navOk = false;
                    oa.activity = ACT_WALK;
                }
                pa.stopPed = -1;
                qa.stopPed = -1;
                g.aiSay(id, BK_COP_ARREST, 1.f, true);
                if (!police_escort::startEscort(g, id, s)) {
                    qa.activity = ACT_WALK;
                    release(g, id, false);
                } else {
                    pa.stmtScene = vec2(1e9f);   // (nothing anybody saw to take a statement about)
                }
                return;
            }
            stance = T < 15.f ? 7 : 0;   // you're good, have a nice day
            if (T - dt < 12.8f && T >= 12.8f) g.aiSay(id, BK_COP_STOP_OK, 1.f, true);
            if (T >= 16.f) {
                LOG("police: officer %d lets %d go after %.0f s", id, s, pa.stopT);
                release(g, id, true);
                return;
            }
        }
    }
    p.phoneBrowse = device;
    p.aiming = p.firing = false;
    p.animIn.stance = stance;
    p.animIn.crouch = false;
    float dy = AI::wrapPi(faceYaw - p.yaw);
    p.yaw = AI::wrapPi(p.yaw + Clamp(dy, -6.f * dt, 6.f * dt));
    g.movePed(p, desired, dt, false);
}

// now and then (one every couple of minutes at most, where the player is): an officer walking a beat stops somebody
// coming their way - most have nothing on them, one in eight a warrant, one in twelve runs (tests: ai.forceStop)
void consider(GameWorld& g, float dt) {
    gStop.next -= dt;
    bool forced = g.ai.forceStop >= 0;
    if (gStop.next > 0.f && !forced) return;
    gStop.next = 4.f;
    Ped* pl = g.playerPed();
    if (!pl || g.pinfo.wanted > 0 || g.populationOff) return;
    if (!forced && g.time - gStop.lastStop < 100.0) return;
    for (const Ped& p : g.peds)
        if (p.used && p.faction == FAC_POLICE && stopping(p)) return;   // (one at a time)
    vec2 pp = pl->pos.toVec3().xy();
    for (int i = 0; i < (int)g.peds.size(); i++) {
        if (!beatLead(g, i) || length(g.peds[i].pos.toVec3().xy() - pp) > (forced ? 200.f : 90.f)) continue;
        int s = pickSubject(g, i, forced ? 22.f : 14.f);
        if (s < 0) continue;
        float r = hashToFloat(hash32(g.peds[i].uid * 977u + (u32)(g.time * 3.0)));
        if (!forced && r > 0.3f) continue;
        float r2 = hashToFloat(hash32(g.peds[s].uid * 31u + (u32)g.time));
        int outcome = forced ? g.ai.forceStop : (r2 < 0.12f ? 1 : (r2 < 0.2f ? 2 : 0));
        begin(g, i, s, outcome);
        if (forced) g.ai.forceStop = -1;
        return;
    }
}

}  // namespace police_stop

// ---- a parking ticket: an officer walking a beat comes past a car parked at the kerb and writes it up - round to the
//      windscreen, a look at it (the meter, the sign, the plate), the ticket on the device, then under the wiper; the
//      partner waits a step back on the sidewalk (pedai.cpp). Now and then the owner comes hurrying (pedai.cpp
//      ACT_TICKET_RUSH), has a word with the officer - to no avail - and drives off.
namespace police_ticket {

struct State {
    float next = 25.f;          // s to the next look round for a car to write up
    double lastTicket = -1e9;   // when the last one began (one every couple of minutes at most)
};
State gTicket;

bool writing(const Ped& p) { return p.brain.type == BRAIN_GOTO && p.brain.target == -6; }

// a car parked along a lane's kerb (the parking strip), nobody in it, nobody's scene, not written up lately
bool ticketable(const GameWorld& g, int v) {
    if (v < 0 || v >= (int)g.vehicles.size()) return false;
    const Vehicle& c = g.vehicles[v];
    static const VehAI kFresh;   // (a car the AI has not looked at yet: nothing on record)
    const VehAI& va = v < (int)g.ai.veh.size() && g.ai.veh[v].uid == c.uid ? g.ai.veh[v] : kFresh;
    // (the player's own car too, left in the street a minute or more with the player well away - not on a mission)
    bool mine = c.playerUsed && g.player >= 0 && g.peds[g.player].vehicle != v && g.time - va.plLeftT > 60.0 && !g.missionActive() &&
                length(rel(c.sim.body.pos, g.peds[g.player].pos)) > 25.f;
    if (!c.used || (!c.parked && !mine) || (c.persistent && !mine) || (c.playerUsed && !mine) || c.exploded || c.sim.wrecked || c.faction != FAC_CIVILIAN ||
        g.isBike(v) || g.isBoat(v) || g.isAircraft(v) || c.sim.speed() > 0.3f)
        return false;
    if (va.eventId >= 0 || va.pullOut || va.parking || va.alarmT > 0.f || g.time - va.ticketed < 600.0) return false;
    for (int s = 0; s < 8; s++)
        if (c.seats[s] >= 0) return false;
    vec3 cp = c.sim.body.pos.toVec3();
    vec2 cf = c.sim.forward().xy();
    cf = length2(cf) > 1e-6f ? normalize(cf) : vec2(0, 1);
    float lu = 0.f, llat = 0.f;
    int lane = g.laneGraph.nearestLane(cp.xy(), cf, 7.f, &lu, &llat);
    return lane >= 0 && llat > 1.2f && llat < 5.f;
}

// where the officer stands to write it up - beside the front wing on the sidewalk side, facing the glass - and the point
// at the foot of the windscreen the ticket goes under (world)
void spot(const GameWorld& g, int v, vec2 officer, vec2& at, float& yaw, vec3& wiper) {
    const Vehicle& c = g.vehicles[v];
    const Vehicles::VehicleModel& s = g.vassets[c.model].spec;
    vec3 cp = c.sim.body.pos.toVec3();
    vec2 f = c.sim.forward().xy();
    f = length2(f) > 1e-6f ? normalize(f) : vec2(0, 1);
    vec2 r = AI::rightOf(f);
    vec2 cc = cp.xy() + f * s.boxCenter.y + r * s.boxCenter.x;
    float side = dot(officer - cc, r) >= 0.f ? 1.f : -1.f;
    vec2 glass = cc + f * (s.boxHalf.y * 0.3f) + r * (side * Max(s.boxHalf.x - 0.18f, 0.2f));
    at = cc + f * (s.boxHalf.y * 0.42f) + r * (side * (s.boxHalf.x + 0.42f));
    vec2 look = glass - at;
    yaw = atan2f(-look.x, look.y);
    float bottom = cp.z + s.boxCenter.z - s.boxHalf.z;
    wiper = vec3(glass, bottom + s.boxHalf.z * 2.f * 0.62f);
}

// the owner hurrying over to this officer (pedai.cpp ACT_TICKET_RUSH), -1 none; `here`: next to the officer already
int owner(const GameWorld& g, int cop, bool* here) {
    if (here) *here = false;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& q = g.peds[i];
        const PedAI& qa = g.ai.ped[i];
        if (!q.used || qa.uid != q.uid || qa.activity != ACT_TICKET_RUSH || qa.ticketCop != cop || qa.ticketCopUid != g.peds[cop].uid || q.state != PS_ONFOOT)
            continue;
        if (here) *here = length(rel(q.pos, g.peds[cop].pos)) < 2.6f;
        return i;
    }
    return -1;
}

// room ahead of a car at the kerb to swing out into the lane (nothing parked within a few metres of its nose)
bool roomToPullOut(const GameWorld& g, int v) {
    const Vehicle& c = g.vehicles[v];
    const Vehicles::VehicleModel& spec = g.vassets[c.model].spec;
    vec2 cp = c.sim.body.pos.toVec3().xy();
    vec2 cf = c.sim.forward().xy();
    cf = length2(cf) > 1e-6f ? normalize(cf) : vec2(0, 1);
    int self = v < (int)g.ai.vehBody.size() ? g.ai.vehBody[v] : -1;
    bool ok = true;
    g.traffic.hash.query(g.traffic.bodies, cp - vec2(14.f), cp + vec2(14.f), [&](int bi) {
        if (!ok || bi == self) return;
        const AI::Body& ob = g.traffic.bodies[bi];
        if (ob.kind != AI::BK_CAR) return;
        vec2 rl = ob.pos - cp;
        float al = dot(rl, cf), lt = dot(rl, AI::rightOf(cf));
        if (al > 0.f && al - ob.halfLen - spec.boxHalf.y < 6.5f && fabsf(lt) < 2.2f) ok = false;
    });
    return ok;
}

// the owner: somebody on their own on the sidewalk a little way off (they come hurrying from there), -1 none
int pickOwner(const GameWorld& g, int cop, vec2 carPos) {
    int best = -1;
    float bs = 1e9f;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& q = g.peds[i];
        const PedAI& qa = g.ai.ped[i];
        if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || q.health <= 0.f || qa.uid != q.uid) continue;
        if (q.brain.type != BRAIN_WANDER || qa.activity != ACT_WALK || qa.leader >= 0 || qa.eventId >= 0 || qa.homeVeh >= 0 || qa.goInside ||
            qa.greetWith >= 0 || qa.stmtWith >= 0 || qa.stopPed >= 0 ||
            (qa.role != PR_CIVILIAN && qa.role != PR_BUSINESS && qa.role != PR_NIGHTLIFE && qa.role != PR_BEACH && qa.role != PR_WORKER))
            continue;
        float d = length(q.pos.toVec3().xy() - carPos);
        if (d < 7.f || d > 45.f) continue;
        bool leads = false;   // (not one with company following them)
        for (int j = 0; j < (int)g.ai.ped.size() && j < (int)g.peds.size() && !leads; j++)
            leads = g.peds[j].used && g.ai.ped[j].leader == i && g.ai.ped[j].leaderUid == q.uid;
        if (leads) continue;
        float score = fabsf(d - 18.f);   // (far enough to be seen coming, near enough to make it in time)
        if (score < bs) {
            bs = score;
            best = i;
        }
    }
    return best;
}

void begin(GameWorld& g, int cop, int v, bool rush) {
    Ped& c = g.peds[cop];
    PedAI& ca = g.pedAI(cop);
    ca.ticketVeh = v;
    ca.ticketVehUid = g.vehicles[v].uid;
    ca.ticketT = 0.f;
    ca.ticketAt = -1.f;
    ca.ticketRush = rush ? 1 : 0;
    c.brain.type = BRAIN_GOTO;
    c.brain.target = -6;
    c.weapon = WPN_FISTS;
    c.aiming = c.firing = false;
    gTicket.lastTicket = g.time;
    g.ai.stats.tickets++;
    LOG("police: officer %d writes up car %d parked at the kerb (%.0f m off, partner %d%s)", cop, v, length(rel(g.vehicles[v].sim.body.pos, c.pos)),
        police_stop::partnerOf(g, cop), rush ? ", the owner will come" : "");
}

// done (or called off): on along the beat - the partner falls in again (pedai.cpp), the owner goes to the car
void finish(GameWorld& g, int id, bool done) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    if (done) LOG("police: officer %d leaves a ticket on car %d (%.0f s)", id, pa.ticketVeh, pa.ticketT);
    pa.ticketVeh = -1;
    pa.ticketAt = -1.f;
    pa.ticketRush = 0;
    pa.reachT = -1.0;
    p.phoneBrowse = false;
    p.animIn.stance = 0;
    p.brain.type = BRAIN_WANDER;
    p.brain.target = -1;
    p.brain.edge = -1;
    pa.navOk = false;
    pa.activity = ACT_WALK;
}

// the officer writing it up (brain GOTO -6)
void step(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    pa.ticketT += dt;
    int v = pa.ticketVeh;
    bool ok = v >= 0 && v < (int)g.vehicles.size() && g.vehicles[v].used && g.vehicles[v].uid == pa.ticketVehUid && !g.vehicles[v].exploded &&
              g.vehicles[v].seats[0] < 0 && g.vehicles[v].sim.speed() < 0.5f;
    if (!ok || g.pinfo.wanted > 0 || pa.ticketT > 70.f || (pa.ticketAt < 0.f && pa.ticketT > 25.f)) {
        LOG("police: officer %d leaves car %d be after %.0f s (%s)", id, v, pa.ticketT,
            !ok ? "it is gone or moving" : (g.pinfo.wanted > 0 ? "a wanted player" : (pa.ticketAt < 0.f ? "never got to it" : "too long")));
        finish(g, id, false);
        return;
    }
    vec2 pos = p.pos.toVec3().xy();
    vec2 at;
    float yaw;
    vec3 wiper;
    spot(g, v, pos, at, yaw, wiper);
    vec2 to = at - pos;
    float d = length(to);
    vec2 desired(0.f);
    float faceYaw = yaw;
    int stance = 0;
    bool device = false;
    if (pa.ticketAt < 0.f) {
        // round to the windscreen (round a bench or a bin on the way)
        float spd = Clamp(d * 1.5f, 0.6f, 1.45f);
        vec2 st = aiWalkRound(g, id, at, spd, dt) - pos;
        float ls = length(st);
        if (ls > 1e-3f && d > 0.12f) desired = st / ls * spd;
        faceYaw = d > 0.8f && ls > 1e-3f ? atan2f(-st.x, st.y) : yaw;
        if (d < 0.3f || (d < 0.9f && pa.ticketT > 4.f && length(p.vel.xy()) < 0.1f)) pa.ticketAt = 0.f;   // (there, or as near as it gets)
    } else {
        pa.ticketAt += dt;
        float T = pa.ticketAt;
        if (d > 0.25f) desired = to / d * Min(d * 2.f, 0.8f);   // (kept at the spot)
        if (T < 2.4f) {
            stance = 14;   // a look at it: the meter, the sign, the plate
        } else if (T < 10.f) {
            device = true;   // writing it up
        } else if (T < 11.6f) {
            // under the wiper
            pa.reachAt = wiper;
            pa.reachT = g.time + 0.25;
            if (T - dt < 10.3f && T >= 10.3f) g.aiSay(id, BK_COP_PARKING, 0.8f);
            if (T - dt < 10.8f && T >= 10.8f) {
                g.vehAI(v).ticketed = g.time;
                if (g.vehicles[v].playerUsed) g.vehAI(v).plTicket = true;   // (the player's: paid on getting back in - ai.cpp)
            }
        } else {
            bool here = false;
            int o = owner(g, id, &here);
            if (o >= 0 && here) {
                // the owner having their word: an answer, then on
                vec2 lo = g.peds[o].pos.toVec3().xy() - pos;
                faceYaw = atan2f(-lo.x, lo.y);
                if (pa.ticketRush != 3) {
                    // (the word starts now: the clock from 11.6 again)
                    pa.ticketRush = 3;
                    pa.ticketT = Min(pa.ticketT, 60.f);
                    pa.ticketAt = 11.6f;
                }
                stance = 7;
                float since = pa.ticketAt - 11.6f;
                if (since - dt < 2.6f && since >= 2.6f) g.aiSay(id, BK_COP_PARKING_REPLY, 1.f, true);
                if (since >= 5.2f) {
                    finish(g, id, true);
                    return;
                }
            } else if (T >= 12.4f && !(o >= 0 && T < 24.f)) {
                // (one hurrying over: the officer waits for them)
                finish(g, id, true);
                return;
            } else if (o >= 0) {
                vec2 lo = g.peds[o].pos.toVec3().xy() - pos;
                faceYaw = atan2f(-lo.x, lo.y);
            }
        }
        if (pa.ticketRush == 1 && T >= 3.5f) {
            // the owner sees it from up the street: picked as the writing starts, they come hurrying (pedai.cpp)
            vec2 cpos = g.vehicles[v].sim.body.pos.toVec3().xy();
            bool room = roomToPullOut(g, v);
            int o = room ? pickOwner(g, id, cpos) : -1;   // (one boxed in by the car in front: nobody comes for it)
            pa.ticketRush = o >= 0 ? 2 : 0;
            if (o < 0) LOG("police: nobody comes for car %d (%s)", v, room ? "nobody about on their own" : "boxed in by the car in front");
            if (o >= 0) {
                PedAI& qa = g.pedAI(o);
                Ped& q = g.peds[o];
                qa.activity = ACT_TICKET_RUSH;
                qa.ticketStep = 0;
                qa.ticketCop = id;
                qa.ticketCopUid = p.uid;
                qa.ticketVeh = v;
                qa.ticketVehUid = g.vehicles[v].uid;
                qa.ticketT = 0.f;
                qa.actTimer = 70.f;
                qa.walkStance = 0;
                qa.stance = 0;
                qa.clip = -1;
                q.phoneBrowse = false;
                g.ai.stats.ticketRushes++;
                g.aiSay(o, BK_PARKING_OWNER, 1.f, true);
                LOG("police: %d comes hurrying to car %d (%.0f m off) as officer %d writes it up", o, v, length(q.pos.toVec3().xy() - cpos), id);
            }
        }
    }
    p.phoneBrowse = device;
    p.aiming = p.firing = false;
    p.animIn.stance = stance;
    p.animIn.crouch = false;
    float dy = AI::wrapPi(faceYaw - p.yaw);
    p.yaw = AI::wrapPi(p.yaw + Clamp(dy, -6.f * dt, 6.f * dt));
    g.movePed(p, desired, dt, false);
}

// now and then (every couple of minutes at most, where the player is): an officer walking a beat comes past a car at
// the kerb a few steps ahead on the sidewalk side - three in ten of those get a ticket, the owner hurrying over to three
// in ten of those (tests: ai.forceTicket)
void consider(GameWorld& g, float dt) {
    gTicket.next -= dt;
    bool forced = g.ai.forceTicket >= 0;
    if (gTicket.next > 0.f && !forced) return;
    gTicket.next = 5.f;
    Ped* pl = g.playerPed();
    if (!pl || g.pinfo.wanted > 0 || g.populationOff) return;
    if (!forced && g.time - gTicket.lastTicket < 120.0) return;
    for (const Ped& p : g.peds)
        if (p.used && p.faction == FAC_POLICE && writing(p)) return;   // (one at a time)
    vec2 pp = pl->pos.toVec3().xy();
    thread_local std::vector<int> cars;
    for (int i = 0; i < (int)g.peds.size(); i++) {
        if (!police_stop::beatLead(g, i) || length(g.peds[i].pos.toVec3().xy() - pp) > (forced ? 200.f : 100.f)) continue;
        const Ped& c = g.peds[i];
        vec2 cp = c.pos.toVec3().xy(), cf = AI::yawDir(c.yaw);
        g.vehiclesNear(cp, forced ? 30.f : 16.f, cars);
        int best = -1;
        float bd = 1e9f;
        // (whether its owner is to come hurrying is settled first: then only a car that can pull out will do)
        u32 h = hash32(c.uid * 613u + (u32)(g.time * 2.0));
        bool rush = forced ? g.ai.forceTicket == 1 : hashToFloat(hash32(h)) < 0.3f;
        if (g.ai.forcePlTicket) rush = false;
        for (int v : cars) {
            if (!ticketable(g, v) || (rush && !roomToPullOut(g, v))) continue;
            if (g.vehicles[v].playerUsed && rush) continue;              // (nobody comes hurrying for the player's car)
            if (g.ai.forcePlTicket && !g.vehicles[v].playerUsed) continue;
            const Vehicle& car = g.vehicles[v];
            vec2 vp = car.sim.body.pos.toVec3().xy();
            vec2 to = vp - cp;
            float d = length(to);
            if (d < 2.f || d >= bd || (!forced && dot(to, cf) < d * 0.3f)) continue;   // (ahead)
            vec2 vf = car.sim.forward().xy();
            vf = length2(vf) > 1e-6f ? normalize(vf) : vec2(0, 1);
            if (dot(cp - vp, AI::rightOf(vf)) < g.vassets[car.model].spec.boxHalf.x + 0.3f) continue;   // (the officer on its kerb side)
            bd = d;
            best = v;
        }
        if (best < 0) continue;
        if (!forced && hashToFloat(h) > 0.3f) continue;
        begin(g, i, best, rush);
        if (forced) g.ai.forceTicket = -1;
        return;
    }
}

}  // namespace police_ticket

// ---- a unit for a scene with no crime in it (events.cpp: a fender bender): sent from out of view, a way in at most 1.7
//      times the straight line, at an easy pace (PT_SCENE, aiPoliceDrive); pulled over behind it with the light bar
//      going, the crew out when the event says so, back in and away once it is done (GOTO -2)
namespace police_scene {

int callUnit(GameWorld& g, vec2 at, vec2 dir) {
    if (g.policeSuppressed || g.pinfo.wanted > 0) return -1;
    int model = g.findVehicleModel(Vehicles::VC_POLICE, (u32)(g.time * 7.0));
    if (model < 0) return -1;
    for (int attempt = 0; attempt < 12; attempt++) {
        u32 h = hash32((u32)(g.time * 13.0) + attempt * 977u);
        float u = 0.f;
        int lane = -1;
        if (attempt < 3) {
            // first the same street, a way back along it (so it comes up behind the scene and pulls over there)
            laneBehind(g.laneGraph, at, dir, 110.f + attempt * 30.f, lane, u);
        } else {
            float ang = hashToFloat(h) * kTwoPi;
            vec2 probe = at + vec2(cosf(ang), sinf(ang)) * (120.f + hashToFloat(hash32(h)) * 70.f);
            lane = g.laneGraph.nearestLane(probe, vec2(0), 50.f, &u);
        }
        if (lane < 0 || (g.laneGraph.lanes[lane].flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC | AI::LF_HIGHWAY))) continue;
        const AI::Lane& L = g.laneGraph.lanes[lane];
        u = Clamp(u, L.u0 + 4.f, L.u1 - 8.f);
        vec3 c = g.laneGraph.lanePos(lane, u);
        if (g.inCameraView(c, 8.f) || !g.traffic.laneFree(lane, u, 3.f, 6.f)) continue;
        float rl = g.aiRouteLength(lane, u, at);
        if (rl < 0.f || rl > length(c.xy() - at) * 1.7f + 40.f) continue;
        vec2 t = g.laneGraph.laneTangent(lane, u);
        int vid = g.spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_POLICE);
        if (vid < 0) return -1;
        g.vehicles[vid].faction = FAC_POLICE;
        g.attachTraffic(vid, lane, u);
        int crew[2] = {g.vehicles[vid].seats[0], -1};
        int seat = g.freeSeat(vid, false);
        if (seat > 0) {
            crew[1] = g.spawnPed(g.randomCivilianChar(h >> 5, 1), g.vehicles[vid].sim.body.pos, 0.f, FAC_POLICE);
            if (crew[1] >= 0) g.warpPedIntoVehicle(crew[1], vid, seat);
        }
        for (int k = 0; k < 2; k++) {
            int o = crew[k];
            if (o < 0) continue;
            g.peds[o].brain.type = k == 0 ? BRAIN_DRIVER : BRAIN_PASSENGER;
            g.giveWeapon(o, WPN_PISTOL, 60);
            g.peds[o].weapon = WPN_FISTS;
            g.pedAI(o).homeVeh = vid;
        }
        VehAI& va = g.vehAI(vid);
        va.role = VR_POLICE;
        va.task = PT_SCENE;
        va.taskPos = at;
        va.transportState = 0;
        va.taskTimer = 0.f;
        va.stopTimer = 0.f;
        LOG("police: unit %d sent to a fender bender at %.0f %.0f (%.0f m off)", vid, at.x, at.y, length(c.xy() - at));
        return vid;
    }
    return -1;
}

// the scene done (or called off): the car back to ordinary driving once its crew is in (they walk back to it: GOTO -2)
void release(GameWorld& g, int vi) {
    if (vi < 0 || vi >= (int)g.vehicles.size() || vi >= (int)g.ai.veh.size() || !g.vehicles[vi].used) return;
    VehAI& va = g.ai.veh[vi];
    if (va.task != PT_SCENE) return;
    va.task = PT_NONE;
    va.transportState = 0;
    g.vehicles[vi].indicator = 0;
    if (AI::Driver* d = g.traffic.get(vi)) {
        d->mode = AI::DM_NORMAL;
        d->hasDest = false;
        d->holdTimer = 0.f;
    }
}

// officers of a scene on foot back to their car (they get in and drive on: aiPoliceBrain GOTO -2)
void crewBack(GameWorld& g, int o, int vi) {
    if (o < 0 || o >= (int)g.peds.size() || !g.peds[o].used || g.peds[o].faction != FAC_POLICE || g.peds[o].state != PS_ONFOOT) return;
    Ped& p = g.peds[o];
    PedAI& pa = g.pedAI(o);
    p.phoneBrowse = false;
    p.animIn.stance = 0;
    pa.activity = ACT_WALK;
    pa.stance = 0;
    pa.eventId = -1;
    pa.homeVeh = vi;
    pa.tactic = FT_RETURN;
    p.brain.type = BRAIN_GOTO;
    p.brain.target = -2;
}

}  // namespace police_scene

// ------------------------------------------------------------------------------------------------------------------
// Tip-offs: the wanted player out of the police's sight a while, an officer on foot close to somebody who saw them since
// - the witness calls out to the officer and points the way the player went, or at where they still are (pedai.cpp
// ACT_TIP_OFF). As they point, the police learn where the player was then and which way they were going (give: the
// search moves there, the units follow the heading). Now and then: never the timid, half the rest, nobody with the
// player right by them, each witness once in a long while.
namespace police_tip {

// an officer on foot after the player (or on the beat), free to listen
bool officer(const GameWorld& g, int i) {
    const Ped& p = g.peds[i];
    if (!isCop(p) || p.isPlayer || p.state != PS_ONFOOT || p.ragdoll || i >= (int)g.ai.ped.size() || g.ai.ped[i].uid != p.uid) return false;
    const PedAI& q = g.ai.ped[i];
    if (q.escortPed >= 0 || q.stmtWith >= 0 || q.stopPed >= 0 || q.ticketVeh >= 0 || q.activity == ACT_AID || q.k9Handler) return false;
    bool hunting = (p.brain.type == BRAIN_COMBAT || p.brain.type == BRAIN_ARREST) && p.brain.target == g.player;
    return hunting || (p.brain.type == BRAIN_WANDER && q.role == PR_COP && q.activity == ACT_WALK);
}

// up on their feet: walking, stopped for a look, or stood somewhere (not sat, lying or leaning)
bool upright(const PedAI& q) {
    switch (q.activity) {
        case ACT_WALK:
        case ACT_INSPECT:
        case ACT_FILM: return true;
        case ACT_WATCH: return !q.plHelper;
        case ACT_SCENARIO:
        case ACT_WAIT_BUS: return q.stance == 0 || q.stance == 7 || q.stance == 8 || q.stance == 10 || q.stance == 14 || q.stance == 23;
        default: return false;
    }
}

// somebody who saw the player since the police last did, calm and willing to say so
bool witness(const GameWorld& g, int i, vec2 ppos) {
    const Ped& p = g.peds[i];
    if (!p.used || p.isPlayer || p.persistent || p.faction != FAC_CIVILIAN || p.state != PS_ONFOOT || p.ragdoll || p.health <= 0.f) return false;
    if (i >= (int)g.ai.ped.size() || g.ai.ped[i].uid != p.uid || (p.brain.type != BRAIN_WANDER && p.brain.type != BRAIN_SCENARIO)) return false;
    const PedAI& q = g.ai.ped[i];
    if (!upright(q) || q.leader >= 0 || q.eventId >= 0 || q.greetWith >= 0 || q.role == PR_DRUNK || q.temper == 0) return false;
    if (q.sawPlT <= g.pinfo.lastSeenTime + 1.f || g.time - q.sawPlT > 30.0 || g.time - q.tipT < 120.0) return false;
    if (length(p.pos.toVec3().xy() - ppos) < 5.f) return false;   // (not with the player right by them)
    return g.ai.forceTip || q.temper == 2 || (hash32(p.uid * 97u + 13u) & 1u) != 0u;
}

// what they point at: the player still in their sight - the player; else a few strides on from where they saw them,
// the way they were going
vec3 wayPoint(const GameWorld& g, const PedAI& q, vec2 from) {
    const Ped* pl = g.player >= 0 && g.player < (int)g.peds.size() && g.peds[g.player].used ? &g.peds[g.player] : nullptr;
    if (q.tipHere && pl && g.time - q.sawPlT < 3.0) return pl->pos.toVec3() + vec3(0.f, 0.f, 1.f);
    vec2 v = q.sawPlVel;
    float sp = length(v);
    vec2 at = q.sawPlAt.xy() + (sp > 0.5f ? v / sp * Clamp(sp * 2.f, 4.f, 25.f) : vec2(0.f));
    if (length(at - from) < 2.f) at = from + (length(q.sawPlAt.xy() - from) > 0.5f ? normalize(q.sawPlAt.xy() - from) : vec2(0.f, 1.f)) * 6.f;
    return vec3(at, q.sawPlAt.z + 1.f);
}

void consider(GameWorld& g, float dt, bool seen) {
    g.ai.tipGap -= dt;
    g.ai.copAskGap -= dt;
    const Ped* pl = g.playerPed();
    if (!pl || g.pinfo.wanted <= 0 || seen || g.ai.tipGap > 0.f || g.pinfo.busted || g.ai.surrender) return;
    g.ai.tipGap = 0.5f;   // (a look twice a second)
    if (g.time - g.pinfo.lastSeenTime < 4.0 && !g.ai.forceTip) return;   // (they have only just lost them)
    vec2 ppos = pl->pos.toVec3().xy();
    int bw = -1, bc = -1;
    float bt = -1e9f;
    std::vector<int> around;
    for (int c = 0; c < (int)g.peds.size(); c++) {
        if (!officer(g, c)) continue;
        vec2 cp = g.peds[c].pos.toVec3().xy();
        if (length(cp - ppos) > 250.f) continue;
        around.clear();
        g.pedsNear(cp, 16.f, around);
        for (int w : around) {
            if (w == c || !witness(g, w, ppos) || g.ai.ped[w].sawPlT <= bt) continue;
            if (length(g.peds[w].pos.toVec3().xy() - cp) < 1.5f) continue;
            if (!g.lineOfSight(g.peds[w].pos + dvec3(0, 0, 1.6), g.peds[c].pos + dvec3(0, 0, 1.6), w, -1)) continue;
            bt = g.ai.ped[w].sawPlT;
            bw = w;
            bc = c;
        }
    }
    if (bw < 0) return;
    PedAI& q = g.pedAI(bw);
    vec2 wp = g.peds[bw].pos.toVec3().xy();
    q.activity = ACT_TIP_OFF;
    q.tipCop = bc;
    q.tipCopUid = g.peds[bc].uid;
    q.tipStep = 0;
    q.tipStepT = 0.f;
    q.tipT = (float)g.time;
    q.tipHere = g.time - q.sawPlT < 1.0;
    q.tipAsked = false;
    q.anchor = wp;
    q.stance = 0;
    q.clip = -1;
    q.walkStance = 0;
    g.ai.tipGap = g.ai.forceTip ? 6.f : 25.f + hashToFloat(hash32(g.peds[bw].uid * 5u + 1u)) * 20.f;
    LOG("police: witness %d calls to officer %d (%.1f m off) - the player seen %.1f s ago %.0f m from them%s", bw, bc,
        length(g.peds[bc].pos.toVec3().xy() - wp), (float)g.time - q.sawPlT, length(q.sawPlAt.xy() - wp), q.tipHere ? ", still in sight" : "");
}

// the witness pointing it out: the police know where the player was when they saw them, and which way they were going
void give(GameWorld& g, int w) {
    PedAI& q = g.pedAI(w);
    if (g.pinfo.wanted <= 0 || q.sawPlT <= g.pinfo.lastSeenTime) return;   // (nothing they do not know by now)
    g.pinfo.lastSeenPos = dvec3(q.sawPlAt);
    g.pinfo.lastSeenTime = q.sawPlT;
    gD.lastSeenVel = vec3(q.sawPlVel, 0.f);
    g.pinfo.wantedCooldown = Max(0.f, g.pinfo.wantedCooldown - 0.2f);   // (the search goes on a while longer)
    int c = q.tipCop;
    if (c >= 0 && c < (int)g.ai.ped.size() && g.ai.ped[c].uid == q.tipCopUid) g.ai.ped[c].tacticTimer = 0.f;   // (off to look there now)
    g.ai.stats.tips++;
    LOG("police: witness %d points officer %d the way - the player seen %.1f s ago at %.0f %.0f (going %.1f m/s)", w, c, (float)g.time - q.sawPlT,
        q.sawPlAt.x, q.sawPlAt.y, length(q.sawPlVel));
}

// an officer searching on foot, the suspect lost a while: now and then a word with somebody calm close by - "you see a
// guy run through here?" - stood by them for the question and the answer (the passer-by: pedai.cpp ACT_TIP_OFF with
// tipAsked - the way they saw him go, pointed out, or "no, sorry"). True while it goes on (the officer stays with them)
bool asking(GameWorld& g, int id, float dt, vec2& desired, float& faceYaw) {
    PedAI& pa = g.pedAI(id);
    const Ped& p = g.peds[id];
    vec2 pos = p.pos.toVec3().xy();
    if (pa.copAsk < 0) {
        if (g.ai.copAskGap > 0.f || g.pinfo.wanted <= 0 || g.pinfo.policeSeesPlayer || g.time - g.pinfo.lastSeenTime < 8.0) return false;
        std::vector<int> around;
        g.pedsNear(pos, 7.f, around);
        int best = -1;
        float bd = 1e9f;
        vec2 fwd = AI::yawDir(p.yaw);
        for (int w : around) {
            if (w == id || w >= (int)g.ai.ped.size()) continue;
            const Ped& q = g.peds[w];
            const PedAI& qa = g.ai.ped[w];
            if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || q.ragdoll || q.health <= 0.f || qa.uid != q.uid)
                continue;
            if ((q.brain.type != BRAIN_WANDER && q.brain.type != BRAIN_SCENARIO) || !upright(qa) || qa.leader >= 0 || qa.eventId >= 0 || qa.greetWith >= 0 ||
                qa.role == PR_DRUNK || g.time - qa.tipT < 120.0)
                continue;
            vec2 to = q.pos.toVec3().xy() - pos;
            float d = length(to);
            if (d >= bd || d < 1.f || dot(to, fwd) < -0.3f * d) continue;   // (not somebody behind them)
            if (!g.lineOfSight(p.pos + dvec3(0, 0, 1.6), q.pos + dvec3(0, 0, 1.6), id, -1)) continue;
            best = w;
            bd = d;
        }
        if (best < 0) return false;
        PedAI& qa = g.pedAI(best);
        pa.copAsk = best;
        pa.copAskUid = g.peds[best].uid;
        pa.copAskT = 0.f;
        pa.copAskSaid = false;
        qa.activity = ACT_TIP_OFF;
        qa.tipCop = id;
        qa.tipCopUid = p.uid;
        qa.tipStep = 0;
        qa.tipStepT = 0.f;
        qa.tipT = (float)g.time;
        qa.tipAsked = true;
        qa.tipHere = g.time - qa.sawPlT < 1.0;
        qa.anchor = g.peds[best].pos.toVec3().xy();
        qa.stance = 0;
        qa.clip = -1;
        qa.walkStance = 0;
        g.ai.copAskGap = g.ai.forceTip ? 8.f : 30.f + hashToFloat(hash32(p.uid * 3u + (u32)g.time)) * 25.f;
        g.ai.stats.copAsks++;
        LOG("police: officer %d asks ped %d about the suspect (%.1f m off; they saw him %.0f s ago)", id, best, bd,
            qa.sawPlT > 0.f ? (float)g.time - qa.sawPlT : -1.f);
    }
    int w = pa.copAsk;
    bool ok = w >= 0 && w < (int)g.peds.size() && w < (int)g.ai.ped.size() && g.peds[w].used && g.peds[w].uid == pa.copAskUid &&
              g.ai.ped[w].uid == pa.copAskUid && g.ai.ped[w].activity == ACT_TIP_OFF && g.ai.ped[w].tipCop == id && g.ai.ped[w].tipStep != 2;
    pa.copAskT += dt;
    if (!ok || pa.copAskT > 14.f) {
        pa.copAsk = -1;
        return false;
    }
    vec2 to = g.peds[w].pos.toVec3().xy() - pos;
    float d = length(to);
    if (d > 1.9f) {   // (up to them - round a parked car on the way)
        float spd = Clamp(d * 1.2f, 0.6f, 1.6f);
        vec2 st = aiWalkRound(g, id, g.peds[w].pos.toVec3().xy(), spd, dt) - pos;
        desired = (length(st) > 1e-3f ? normalize(st) : to / d) * spd;
    }
    faceYaw = d > 3.f && length2(desired) > 1e-4f ? atan2f(-desired.x, desired.y) : atan2f(-to.x, to.y);
    if (!pa.copAskSaid && (d < 3.f || pa.copAskT > 3.f)) {
        // (the question, once by them; the answer is theirs: pedai.cpp)
        pa.copAskSaid = true;
        pa.barkCooldown = 0.f;
        g.aiSay(id, BK_COP_ASK_SEEN, 1.f, true);
        g.ai.ped[w].tipStepT = 0.f;   // (they answer once it is said)
    }
    return true;
}

}  // namespace police_tip

// ------------------------------------------------------------------------------------------------------------------
// Crime reports from gameplay code (combat.cpp, vehicles.cpp, player.cpp)
void GameWorld::reportCrime(int type, dvec3 pos, int victim) {
    if (type < 0 || type > 11) return;
    Ped* pl = playerPed();
    // who did it?  Gunfire is reported at the shooter's position; everything else is only reported for the player.
    int perp = player;
    if (type == 1) {
        perp = -1;
        for (int i = 0; i < (int)peds.size(); i++)
            if (peds[i].used && peds[i].pos.x == pos.x && peds[i].pos.y == pos.y && peds[i].pos.z == pos.z) {
                perp = i;
                break;
            }
    } else if (type == 8) {
        // explosions: the player's if they recently used explosives or shot around here
        perp = -1;
        if (pl && (time - ai.playerExplosiveTime < 14.0 || (time - ai.playerLastShotTime < 8.0 && length(rel(pos, pl->pos)) < 90.f))) perp = player;
    }
    bool byPlayer = perp >= 0 && perp == player;
    if (byPlayer && type == 1) ai.playerLastShotTime = (float)time;
    // scare the neighborhood
    if (type == 1) aiStimulus(pos, STIM_GUNFIRE, perp, 48.f, byPlayer);
    else if (type == 8) aiStimulus(pos, STIM_EXPLOSION, perp, 65.f, byPlayer);
    else if (type == 0 || type == 5) aiStimulus(pos, STIM_FIGHT, perp, 22.f, byPlayer);
    else if (type == 2 || type == 3) aiStimulus(pos, STIM_CRASH, perp, 18.f, byPlayer);
    if (type == 3 && byPlayer && victim >= 0 && victim < (int)vehicles.size() && vehicles[victim].used) {
        // the player rammed a car: its driver may get out and have words (road rage), or honk and shout
        Vehicle& vv = vehicles[victim];
        int drv = vv.seats[0];
        if (drv >= 0 && !peds[drv].isPlayer && peds[drv].brain.type == BRAIN_DRIVER && vv.faction == FAC_CIVILIAN && !isAircraft(victim) && !isBoat(victim)) {
            VehAI& va = vehAI(victim);
            PedAI& da = pedAI(drv);
            float r = hashToFloat(hash32(vv.uid * 31u + (u32)(time * 2.0)));
            if (va.rage == 0 && (da.temper == 2 || (da.temper == 1 && r < 0.35f))) {
                va.rage = 1;
                va.rageTimer = 0.f;
            } else {
                vv.hornOn = true;
                aiSay(drv, BK_CRASH, 0.8f, true);
            }
        }
    }
    if (type == 9 && victim >= 0 && victim < (int)peds.size() && peds[victim].used && !peds[victim].isPlayer) {
        // pulled out of their car: bold drivers get up and fight for it, the rest run
        Ped& vp = peds[victim];
        PedAI& va = pedAI(victim);
        bool fight = vp.faction == FAC_CIVILIAN && va.temper == 2 && hashToFloat(hash32(vp.uid * 7u + 3u)) < 0.6f;
        if (fight) {
            vp.brain.type = BRAIN_COMBAT;
            vp.brain.target = perp >= 0 ? perp : player;
            vp.brain.timer = 0.f;
            va.activity = ACT_WALK;
        }
        aiSay(victim, fight ? BK_INSULT : BK_CARJACKED, 1.f, true);
    }
    // NPC crimes: police nearby respond to the perpetrator
    if (!byPlayer) {
        if (perp >= 0 && peds[perp].faction != FAC_POLICE) {
            bool dup = false;
            for (Incident& inc : ai.incidents)
                if (inc.active && inc.kind == 2 && inc.perp == perp) {
                    inc.time = (float)time;
                    dup = true;
                }
            if (!dup) {
                Incident inc;
                inc.pos = pos;
                inc.kind = 2;
                inc.time = (float)time;
                inc.perp = perp;
                inc.perpUid = peds[perp].uid;
                inc.active = true;
                ai.incidents.push_back(inc);
            }
        }
        return;
    }
    if (!pl || policeSuppressed) return;
    CrimeEvent e;
    e.type = type;
    e.pos = pos;
    e.victim = victim;
    crimes.push_back(e);
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updateWanted(float dt) {
    Ped* pl = playerPed();
    if (!pl) {
        crimes.clear();
        return;
    }
    vec3 ppos = pl->pos.toVec3();
    // explosive use bookkeeping (attribution of explosions)
    {
        int ex = pl->ammo[WPN_GRENADE] + pl->ammo[WPN_MOLOTOV] + pl->ammo[WPN_RPG];
        if (ex < ai.playerLastExplosiveAmmo) ai.playerExplosiveTime = (float)time;
        ai.playerLastExplosiveAmmo = ex;
    }
    // ---- police visibility of the player
    bool seen = false;
    int seer = -1;
    for (int i = 0; i < (int)peds.size() && !seen; i++) {
        const Ped& p = peds[i];
        if (!isCop(p)) continue;
        vec3 d = ppos - p.pos.toVec3();
        float dist = length(d);
        bool air = p.vehicle >= 0 && isAircraft(p.vehicle);
        float range = air ? 260.f : (p.state == PS_INVEHICLE ? (pinfo.wanted >= 3 ? 125.f : 95.f) : 75.f);   // pursuit crews watch the road far ahead
        if (dist > range) continue;
        if (!air && p.state != PS_INVEHICLE && dist > 12.f) {
            vec3 f(-sinf(p.yaw), cosf(p.yaw), 0);
            if (dot(normalize(vec3(d.x, d.y, 0)), f) < 0.1f) continue;
        }
        int ignoreVeh = p.vehicle;
        dvec3 eye = p.pos + dvec3(0, 0, air ? -1.0 : 1.6);
        if (lineOfSight(eye, sightPoint(*this, *pl, eye.toVec3()), i, ignoreVeh)) {
            seen = true;
            seer = i;
        }
    }
    pinfo.policeSeesPlayer = seen;
    if (seen) {
        pinfo.lastSeenPos = pl->pos;
        pinfo.lastSeenTime = (float)time;
        int spv = pl->vehicle;
        gD.lastSeenVel = spv >= 0 && spv < (int)vehicles.size() ? vehicles[spv].sim.body.vel : pl->vel;
    }
    // ---- surrender: wanted, on foot, empty-handed, no call on - holding the phone key a moment puts the hands up (and
    // keeps them up); stepping off, drawing, firing, jumping or a second press takes them down again
    {
        bool can = pinfo.wanted > 0 && pl->state == PS_ONFOOT && pl->health > 0.f && !pinfo.busted && pl->weapon == WPN_FISTS &&
                   phone.call == UI::CALL_NONE && !mInCutscene();
        if (!ai.surrender) {
            ai.surrenderHold = can && ctl.phone.down ? ai.surrenderHold + dt : 0.f;
            if (can && (ai.surrenderHold > 0.35f || ai.forceSurrender)) {
                ai.surrender = true;
                ai.forceSurrender = false;
                ai.surrenderHold = 0.f;
                if (playerControl) {
                    playerControl = false;
                    ai.surrenderCtl = true;
                }
                phone.open = false;   // (the press that started it)
                pl->aiming = pl->firing = false;
                pl->pendingAction = Anim::CLIP_HANDS_UP;
                LOG("police: the player surrenders (wanted %d)", pinfo.wanted);
            }
        } else {
            bool quit = !can || length(ctl.move) > 0.35f || ctl.sprint.down || ctl.jump.pressed || ctl.attack.down || ctl.aim.down || ctl.enter.pressed ||
                        (ctl.phone.pressed && ai.surrenderHold > 0.8f);
            ai.surrenderHold += dt;
            if (quit) {
                ai.surrender = false;
                ai.surrenderHold = 0.f;
                if (ai.surrenderCtl) {
                    playerControl = true;
                    ai.surrenderCtl = false;
                }
                pl->animIn.stance = 0;
                // walking away from a surrender is resisting
                if (pinfo.wanted > 0 && !pinfo.busted) {
                    pinfo.wantedHeat += 0.3f;
                    LOG("police: the player broke off the surrender");
                }
            } else {
                pl->animIn.stance = 5;   // hands up, standing still (the controls are ours meanwhile)
                pl->vel = vec3(0.f, 0.f, pl->vel.z);
            }
        }
    }
    // ---- the scent trail from the last-seen point (K9), and the K9 unit
    if (pinfo.wanted <= 0) {
        gTrail.clear();
        gTrailEnds = false;
    } else if (seen) {
        gTrail.clear();
        gTrail.push_back({ppos.xy(), ppos.z});
        gTrailEnds = false;
    } else if (pl->vehicle >= 0) {
        gTrailEnds = true;
    } else if (!gTrailEnds && gTrail.size() < 400 && (gTrail.empty() || length(gTrail.back().p - ppos.xy()) > 2.f)) {
        // (a jump no one walks - a ride, a cut-scene move: the scent stops there)
        if (!gTrail.empty() && length(gTrail.back().p - ppos.xy()) > 15.f) gTrailEnds = true;
        else gTrail.push_back({ppos.xy(), ppos.z});
    }
    updateK9Unit(*this, dt, seen);
    // ---- crimes: witnessed by police -> immediate; otherwise a civilian may phone it in
    for (const CrimeEvent& e : crimes) {
        if (policeSuppressed) break;
        float heat = kCrimeHeat[e.type];
        bool copWitness = seen && (!kNeedsWitness[e.type] || length(rel(e.pos, pl->pos)) < 60.f);
        if (!copWitness) {
            // an officer who can see the crime scene itself
            for (int i = 0; i < (int)peds.size() && !copWitness; i++) {
                const Ped& p = peds[i];
                if (!isCop(p) || length(rel(p.pos, e.pos)) > 60.f) continue;
                if (lineOfSight(p.pos + dvec3(0, 0, 1.6), e.pos + dvec3(0, 0, 1.0), i, p.vehicle)) copWitness = true;
            }
        }
        if (copWitness) {
            pinfo.wantedHeat += heat;
            pinfo.wantedCooldown = 0.f;
            pinfo.lastSeenPos = pl->pos;
            pinfo.lastSeenTime = (float)time;
            continue;
        }
        // loud crimes near people get noticed; quiet ones need an eyewitness with a line of sight
        std::vector<int> w;
        pedsNear(vec2((float)e.pos.x, (float)e.pos.y), e.type == 1 || e.type == 8 ? 55.f : 32.f, w);
        int caller = -1;
        float bestD = 1e9f;
        for (int i : w) {
            const Ped& q = peds[i];
            if (q.isPlayer || q.faction != FAC_CIVILIAN || q.health <= 0.f || i == e.victim || q.state != PS_ONFOOT || q.persistent) continue;
            PedAI& qa = pedAI(i);
            if (qa.activity == ACT_CALL_POLICE) {
                caller = -2;   // someone is already on the phone about it
                break;
            }
            float d = length(rel(q.pos, e.pos));
            if (d < bestD && (e.type == 1 || e.type == 8 || lineOfSight(q.pos + dvec3(0, 0, 1.6), e.pos + dvec3(0, 0, 1.0), i, -1))) {
                bestD = d;
                caller = i;
            }
        }
        if (caller == -2) {
            for (PendingReport& r : ai.reports)
                if (r.active) r.timer = Min(r.timer, 3.f);   // more to tell
            continue;
        }
        if (caller < 0) {
            // nobody saw it: noise alone raises a small amount of heat if it happened in the city
            if ((e.type == 1 || e.type == 8) && pinfo.wanted > 0) pinfo.wantedHeat += heat * 0.25f;
            continue;
        }
        PendingReport r;
        r.type = e.type;
        r.pos = e.pos;
        r.victim = e.victim;
        r.caller = caller;
        r.callerUid = peds[caller].uid;
        r.timer = 6.f + hashToFloat(hash32(peds[caller].uid + (u32)(time * 10.0))) * 5.f;
        r.active = true;
        bool merged = false;
        for (PendingReport& o : ai.reports)
            if (!o.active) {
                o = r;
                merged = true;
                break;
            }
        if (!merged) ai.reports.push_back(r);
        PedAI& ca = pedAI(caller);
        ca.activity = ACT_CALL_POLICE;
        ca.threatPos = e.pos.toVec3().xy();
        ca.actTimer = r.timer + 3.f;
        peds[caller].brain.type = BRAIN_WANDER;
        aiSay(caller, BK_CALL_POLICE, 0.8f, true);
    }
    crimes.clear();
    // ---- witnesses on the phone: the call goes through unless they are stopped
    for (PendingReport& r : ai.reports) {
        if (!r.active) continue;
        bool alive = r.caller >= 0 && r.caller < (int)peds.size() && peds[r.caller].used && peds[r.caller].uid == r.callerUid && peds[r.caller].health > 0.f;
        bool stopped = !alive || peds[r.caller].state == PS_RAGDOLL || peds[r.caller].state == PS_DEAD;
        if (alive && !stopped) {
            PedAI& ca = pedAI(r.caller);
            if (ca.activity != ACT_CALL_POLICE) stopped = true;
            // threatened by the player: hangs up
            float d = length(rel(peds[r.caller].pos, pl->pos));
            if (d < 9.f && pl->aiming) {
                vec3 to = pedChestPos(peds[r.caller]) - pedHeadPos(*pl);
                float along = dot(to, pl->aimDir);
                if (along > 0.f && length(to - pl->aimDir * along) < 1.5f) {
                    stopped = true;
                    aiSay(r.caller, BK_WITNESS_STOP, 1.f, true);
                    ca.activity = ACT_HANDS_UP;
                    ca.actTimer = 3.f;
                }
            }
        }
        if (stopped) {
            r.active = false;
            continue;
        }
        if (r.timer > 2.f && r.timer - dt <= 2.f) aiSay(r.caller, BK_PHONE_REPORT, 1.f, false);
        r.timer -= dt;
        if (r.timer <= 0.f) {
            r.active = false;
            pinfo.wantedHeat += kCrimeHeat[r.type] * 0.75f;
            pinfo.wantedCooldown = 0.f;
            if (pinfo.wanted == 0 || time - pinfo.lastSeenTime > 5.0) {
                pinfo.lastSeenPos = r.pos;
                pinfo.lastSeenTime = (float)time;
            }
            pedAI(r.caller).actTimer = 4.f;
        }
    }
    // ---- heat -> stars (never decreases while seen)
    int target = 0;
    for (int s = 5; s >= 1; s--)
        if (pinfo.wantedHeat >= kStarHeat[s]) {
            target = s;
            break;
        }
    if (target > pinfo.wanted) {
        pinfo.wanted = target;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_WANTED_UP, 0.7f);
#endif
    }
    pinfo.maxWanted = Max(pinfo.maxWanted, (float)pinfo.wanted);
    // ---- busted: low wanted level, an officer right next to a slow, non-shooting player on foot (or stopped in a car)
    bool bustable = false;
    if (pinfo.wanted > 0 && (pinfo.wanted <= 2 || ai.surrender) && pl->health > 0.f && !pinfo.busted) {
        int pv = pl->vehicle;
        bool downed = pl->state == PS_RAGDOLL || pl->state == PS_GETUP;   // tackled
        float spd = pv >= 0 ? vehicles[pv].sim.speed() : (downed ? 0.f : length(vec2(pl->vel.x, pl->vel.y)));
        bool calm = spd < 1.2f && !pl->firing && !(pv < 0 && pl->aiming);
        if (calm) {
            for (int i = 0; i < (int)peds.size(); i++) {
                const Ped& p = peds[i];
                if (!isCop(p) || p.state != PS_ONFOOT) continue;
                if (length(p.pos.toVec3() - ppos) < (pv >= 0 ? 3.2f : 2.2f)) {
                    bustable = true;
                    if (gD.bustTimer < 0.1f) aiSay(i, BK_COP_ARREST, 1.f, true);
                    break;
                }
            }
        }
    }
    gD.bustTimer = bustable ? gD.bustTimer + dt : 0.f;
    // (a tackled player is cuffed the moment they are back on their feet: never switch states mid-ragdoll)
    if (gD.bustTimer > (ai.surrender ? 1.6f : 2.2f) && pl->state != PS_RAGDOLL && pl->state != PS_GETUP) {
        gD.bustTimer = 0.f;
        pinfo.busted = true;
        pinfo.arrests++;
        ai.stats.arrests++;
        // gave themselves up: a lighter booking - the weapons come back with the release, and half the fine
        ai.surrenderBust = ai.surrender;
        ai.bustWatch = true;
        ai.bustMoney = pinfo.money;
        for (int w = 0; w < WPN_COUNT && w < 16; w++) {
            ai.bustHas[w] = pl->hasWeapon[w];
            ai.bustAmmo[w] = pl->ammo[w];
            ai.bustClip[w] = pl->clip[w];
        }
        ai.bustWeapon = pl->weapon;
        if (ai.surrender) {
            ai.surrender = false;
            ai.surrenderCtl = false;   // (the busted flow owns the controls now)
            LOG("police: surrender -> cuffed");
        }
        bigMessage("BUSTED", ai.surrenderBust ? "You gave yourself up" : "", 0xffffcc33u);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_BUSTED, 0.9f);
#endif
        if (pl->vehicle >= 0) removePedFromVehicle(player, false);
        pl->pendingAction = Anim::CLIP_HANDS_UP;
        playerControl = false;
        pinfo.deathTimer = 0.001f;   // reuse the respawn flow (police station release)
    }
    if (ai.bustWatch && !pinfo.busted) {
        ai.bustWatch = false;
        if (ai.surrenderBust) {
            for (int w = 0; w < WPN_COUNT && w < 16; w++) {
                if (!ai.bustHas[w]) continue;
                pl->hasWeapon[w] = true;
                pl->ammo[w] = ai.bustAmmo[w];
                pl->clip[w] = ai.bustClip[w];
            }
            long long fine = ai.bustMoney - pinfo.money;
            if (fine > 0) pinfo.money += fine / 2;
            LOG("police: released after a surrender - weapons returned, %lld of the %lld fine refunded", fine > 0 ? fine / 2 : 0ll, fine);
        }
        ai.surrenderBust = false;
    }
    // ---- witnesses pointing an officer on foot the way the player went (police_tip)
    police_tip::consider(*this, dt, seen);
    // ---- evasion: out of sight and outside the search area around the last seen position (not while a K9 unit is
    //      working a live trail: the dog is on them until the trail goes cold or ends at a kerb)
    if (pinfo.wanted > 0) {
        float searchR = searchRadiusFor(pinfo.wanted);
        float distFromLast = length(rel(pl->pos, pinfo.lastSeenPos));
        bool k9Hot = k9Active(*this) && !gTrailEnds && gK9.workT < 75.f && pl->vehicle < 0;
        if (!seen && !k9Hot && (time - pinfo.lastSeenTime > 3.0 || distFromLast > searchR)) {
            float need = 8.f + pinfo.wanted * 6.f;
            pinfo.wantedCooldown += dt / need * (distFromLast > searchR ? 1.6f : 0.6f);
            if (pinfo.wantedCooldown >= 1.f) {
                pinfo.wanted = 0;
                pinfo.wantedHeat = 0.f;
                pinfo.wantedCooldown = 0.f;
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_WANTED_LOST, 0.7f);
#endif
            }
        } else if (seen) {
            pinfo.wantedCooldown = 0.f;
        }
        pinfo.wantedHeat = Max(kStarHeat[pinfo.wanted], pinfo.wantedHeat - dt * 0.02f);
    } else {
        pinfo.wantedHeat = Max(0.f, pinfo.wantedHeat - dt * 0.05f);
    }
    // radio: spotted / lost
    gD.lostBarkTimer -= dt;
    if (pinfo.wanted > 0 && seen != gD.wasSeen && gD.lostBarkTimer <= 0.f) {
        int speaker = seen ? seer : -1;
        if (!seen)
            for (int i = 0; i < (int)peds.size(); i++)
                if (isCop(peds[i]) && length(rel(peds[i].pos, pl->pos)) < 90.f) {
                    speaker = i;
                    break;
                }
        if (speaker >= 0) aiSay(speaker, seen ? BK_COP_SPOTTED : BK_COP_LOST, 0.8f, true);
        gD.lostBarkTimer = 8.f;
    }
    gD.wasSeen = seen;
    (void)seer;
}

// ------------------------------------------------------------------------------------------------------------------
// Dispatch: unit counts by wanted level, spawning ahead / around the player out of view, roadblocks, helicopter, boats.
void GameWorld::updateDispatch(float dt) {
    Ped* pl = playerPed();
    if (!pl || !ai.ready) return;
    int wanted = pinfo.wanted;
    vec3 pp = pl->pos.toVec3();
    int pv = playerVehicle();
    vec2 pvel = pv >= 0 ? vehicles[pv].sim.body.vel.xy() : pl->vel.xy();
    gD.counter++;
    gD.spawnTimer -= dt;
    // ---- count and (re)assign units
    auto standDown = [&](int i) {
        Ped& p = peds[i];
        bool inCar = p.state == PS_INVEHICLE && p.vehicle >= 0;
        p.brain.target = -1;
        p.aiming = false;
        if (inCar && p.seat == 0) {
            p.brain.type = BRAIN_DRIVER;
            vehicles[p.vehicle].sirenOn = false;
            vehicles[p.vehicle].sirenSilent = false;
            VehAI& va = vehAI(p.vehicle);
            va.task = PT_NONE;
            if (AI::Driver* d = traffic.get(p.vehicle)) {
                d->mode = AI::DM_NORMAL;
                d->hasDest = false;
                d->destEdges.clear();
                d->pers = AI::Personality::make(d->uid);   // back to patrol manners
            }
        } else if (inCar) {
            p.brain.type = BRAIN_PASSENGER;
        } else {
            PedAI& pa = pedAI(i);
            pa.tactic = FT_RETURN;
            p.brain.type = BRAIN_GOTO;
            p.brain.target = -2;
        }
    };
    int carUnits = 0, footUnits = 0, npcUnits = 0;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!isCop(p) || p.persistent) continue;
        // walking a prisoner to a car: on that, unless the player starts trouble right there
        if (police_escort::escorting(p) && !(wanted > 0 && length(rel(p.pos, pl->pos)) < 40.f)) continue;
        bool inCar = p.state == PS_INVEHICLE && p.vehicle >= 0;
        bool hunting = p.brain.type == BRAIN_COMBAT || p.brain.type == BRAIN_ARREST;
        bool npcTarget = hunting && p.brain.target >= 0 && p.brain.target != player;
        if (npcTarget) {
            // chasing an NPC criminal: give up when they are dead, arrested (cowering), gone or far away
            const Ped* t = p.brain.target < (int)peds.size() && peds[p.brain.target].used ? &peds[p.brain.target] : nullptr;
            // (both on foot is a chase on foot - the vehicles only rule out a suspect sitting in this officer's own car)
            bool valid = t && t->health > 0.f && !police_escort::inCustody(*this, p.brain.target) && (t->vehicle < 0 || t->vehicle != p.vehicle) &&
                         length(rel(t->pos, p.pos)) < 350.f;
            if (!valid && !(wanted > 0 && length(rel(p.pos, pl->pos)) < 400.f)) {
                // (in cuffs: an officer on foot takes a statement from somebody who saw it before heading back)
                bool cuffed = t && t->health > 0.f && police_escort::inCustody(*this, p.brain.target);
                vec2 scene = t ? t->pos.toVec3().xy() : vec2(0.f);
                standDown(i);
                if (cuffed && !inCar && wanted == 0) police_statement::begin(*this, i, scene);
                continue;
            }
            if (valid && wanted == 0) {
                if (inCar && p.seat == 0) npcUnits++;
                continue;
            }
        }
        if (wanted > 0) {
            if (!hunting || npcTarget) {
                // everyone within 400 m joins (the player's crimes take precedence over NPC business)
                if (length(rel(p.pos, pl->pos)) < 400.f) {
                    p.brain.type = wanted <= 1 ? BRAIN_ARREST : BRAIN_COMBAT;
                    p.brain.target = player;
                    p.brain.timer = 0.f;
                    if (p.weapon == WPN_FISTS) {
                        giveWeapon(i, wanted >= 3 ? WPN_RIFLE : WPN_PISTOL, 150);
                        p.weapon = wanted >= 3 ? WPN_RIFLE : WPN_PISTOL;
                    }
                    if (inCar && p.seat == 0) vehAI(p.vehicle).task = PT_PURSUE;
                }
            } else if (wanted >= 2 && p.brain.type == BRAIN_ARREST) {
                p.brain.type = BRAIN_COMBAT;   // escalation
            }
            if (inCar && p.seat == 0 && !isAircraft(p.vehicle)) {
                carUnits++;
                vehicles[p.vehicle].sirenOn = true;
                vehicles[p.vehicle].sirenSilent = false;
            } else if (!inCar) {
                footUnits++;
            }
        } else if (hunting && p.brain.target == player) {
            standDown(i);   // back to patrol - an officer on foot asking round first, where somebody saw something
            if (!inCar) police_statement::begin(*this, i, p.pos.toVec3().xy());
        }
    }
    // ---- officers walking a beat stop somebody now and then (police_stop)
    if (wanted == 0) police_stop::consider(*this, dt);
    if (wanted == 0) police_ticket::consider(*this, dt);
    // ---- NPC crimes (muggings, shootouts, fleeing criminals): the nearest free patrol responds, or one is sent
    if (wanted == 0) {
        for (Incident& inc : ai.incidents) {
            if (!inc.active || inc.kind != 2) continue;
            bool perpOk = inc.perp >= 0 && inc.perp < (int)peds.size() && peds[inc.perp].used && peds[inc.perp].uid == inc.perpUid &&
                          peds[inc.perp].health > 0.f && !police_escort::inCustody(*this, inc.perp);
            if (!perpOk || time - inc.time > 150.0) {
                inc.active = false;
                continue;
            }
            bool covered = inc.unit >= 0 && inc.unit < (int)vehicles.size() && vehicles[inc.unit].used && vehicles[inc.unit].faction == FAC_POLICE &&
                           !vehicles[inc.unit].exploded;
            if (covered) {
                int drv = vehicles[inc.unit].seats[0];
                covered = drv < 0 || peds[drv].brain.target == inc.perp;   // officers already out on foot, or still on it
            }
            if (covered) continue;
            inc.unit = -1;
            vec2 ip = peds[inc.perp].pos.toVec3().xy();
            int best = -1;
            float bd = 450.f;
            for (int vi = 0; vi < (int)vehicles.size(); vi++) {
                const Vehicle& v = vehicles[vi];
                if (!v.used || v.faction != FAC_POLICE || isAircraft(vi) || isBoat(vi) || v.exploded || v.persistent) continue;
                int drv = v.seats[0];
                if (drv < 0 || peds[drv].isPlayer || peds[drv].brain.type != BRAIN_DRIVER) continue;
                bool prisoner = false;   // (a unit taking someone in is not sent to the next call)
                for (int s = 1; s < 8; s++) prisoner |= v.seats[s] >= 0 && peds[v.seats[s]].faction != FAC_POLICE;
                if (prisoner || (vi < (int)ai.veh.size() && ai.veh[vi].copBreak != 0)) continue;   // (nor one with its crew on a break)
                float d = length(v.sim.body.pos.toVec3().xy() - ip);
                if (d > bd) continue;
                // (by the way in on the roads where the car is on one: a car pointing the other way down a one-way
                //  street is further off than it looks)
                if (const AI::Driver* dv = traffic.get(vi))
                    if (dv->path >= 0 && laneGraph.isLane(dv->path)) {
                        float rl = aiRouteLength(dv->path, dv->u, ip);
                        if (rl >= 0.f) d = Max(d, rl);
                    }
                if (d < bd) {
                    bd = d;
                    best = vi;
                }
            }
            if (best < 0 && npcUnits < 2 && gD.spawnTimer <= 0.f) {
                // send one from out of view
                gD.spawnTimer = 6.f;
                int model = findVehicleModel(Vehicles::VC_POLICE, gD.counter);
                for (int attempt = 0; attempt < 6 && best < 0 && model >= 0; attempt++) {
                    u32 h = hash32(gD.counter * 97u + attempt * 131u);
                    float ang = hashToFloat(h) * kTwoPi;
                    vec2 probe = ip + vec2(cosf(ang), sinf(ang)) * (140.f + hashToFloat(hash32(h)) * 80.f);
                    float u = 0.f;
                    int lane = laneGraph.nearestLane(probe, vec2(0), 50.f, &u);
                    if (lane < 0 || (laneGraph.lanes[lane].flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC))) continue;
                    const AI::Lane& L = laneGraph.lanes[lane];
                    u = Clamp(u, L.u0 + 4.f, L.u1 - 8.f);
                    vec3 c = laneGraph.lanePos(lane, u);
                    if (inCameraView(c, 8.f) || !traffic.laneFree(lane, u, 3.f, 6.f)) continue;
                    if (attempt < 5) {   // (a way in at most 1.7 times the straight line, as the other units)
                        float rl = aiRouteLength(lane, u, ip);
                        if (rl < 0.f || rl > length(c.xy() - ip) * 1.7f + 40.f) continue;
                    }
                    vec2 t = laneGraph.laneTangent(lane, u);
                    int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_POLICE);
                    if (vid < 0) break;
                    ai.stats.unitsSent++;
                    vehicles[vid].faction = FAC_POLICE;
                    attachTraffic(vid, lane, u);
                    int drv = vehicles[vid].seats[0];
                    if (drv >= 0) peds[drv].brain.type = BRAIN_DRIVER;
                    int partner = spawnPed(randomCivilianChar(hash32(h * 3u), 1), vehicles[vid].sim.body.pos, 0.f, FAC_POLICE);
                    if (partner >= 0) {
                        int seat = freeSeat(vid, false);
                        if (seat > 0) {
                            warpPedIntoVehicle(partner, vid, seat);
                            peds[partner].brain.type = BRAIN_PASSENGER;
                        } else {
                            despawnPed(partner);
                        }
                    }
                    best = vid;
                }
            }
            if (best < 0) continue;
            Vehicle& v = vehicles[best];
            v.sirenOn = true;
            v.sirenSilent = false;
            VehAI& va = vehAI(best);
            va.role = VR_POLICE;
            va.task = PT_PURSUE;
            for (int s = 0; s < 8; s++) {
                int o = v.seats[s];
                if (o < 0 || peds[o].isPlayer) continue;
                Ped& op = peds[o];
                if (op.weapon == WPN_FISTS) {
                    giveWeapon(o, WPN_PISTOL, 120);
                    op.weapon = WPN_PISTOL;
                }
                op.brain.type = BRAIN_COMBAT;
                op.brain.target = inc.perp;
                op.brain.accuracy = 0.45f;
                pedAI(o).homeVeh = best;
            }
            inc.unit = best;
            LOG("police: unit %d sent to an NPC crime (perp %d%s) %.0f m off", best, inc.perp, peds[inc.perp].state == PS_INVEHICLE ? ", in a vehicle" : "",
                length(v.sim.body.pos.toVec3().xy() - ip));
            if (v.seats[0] >= 0) aiSay(v.seats[0], BK_COP_CHATTER, 0.6f);
        }
    }
    // ---- helicopter (4+ stars)
    Vehicle* heli = gD.heli >= 0 && gD.heli < (int)vehicles.size() && vehicles[gD.heli].used && vehicles[gD.heli].uid == gD.heliUid ? &vehicles[gD.heli] : nullptr;
    if (!heli) gD.heli = -1;
    gD.heliTimer -= dt;
    if (wanted >= 4 && !heli && gD.heliTimer <= 0.f) {
        gD.heliTimer = 25.f;
        int model = -1;
        for (int m = 0; m < (int)vassets.size(); m++)
            if (vassets[m].spec.cls == Vehicles::VC_HELI && (vassets[m].spec.fixedLivery || vassets[m].spec.name.find("Patrol") != std::string::npos)) model = m;
        if (model < 0) model = findVehicleModel(Vehicles::VC_HELI, gD.counter);
        if (model >= 0) {
            float ang = hashToFloat(hash32(gD.counter * 7u)) * kTwoPi;
            vec3 sp = pp + vec3(cosf(ang) * 260.f, sinf(ang) * 260.f, 0.f);
            sp.z = Max(map->heightAt(sp.x, sp.y), 0.f) + 90.f;
            int vid = spawnVehicle(model, dvec3(sp), ang + kPi, true, FAC_POLICE);
            if (vid >= 0) {
                Vehicle& h = vehicles[vid];
                h.faction = FAC_POLICE;
                h.sim.engineOn = true;
                h.sim.rotorSpeed = 1.f;
                h.sim.body.vel = vec3(0, 0, 0);
                h.lightsOn = true;
                VehAI& va = vehAI(vid);
                va.role = VR_POLICE_HELI;
                va.task = PT_PURSUE;
                gD.heli = vid;
                gD.heliUid = h.uid;
                ai.stats.heliUnits++;
                int drv = h.seats[0];
                if (drv >= 0) {
                    peds[drv].brain.type = BRAIN_COMBAT;
                    peds[drv].brain.target = player;
                }
                // sharpshooter in the back
                int sniper = spawnPed(randomCivilianChar(hash32(gD.counter), 1), dvec3(sp), 0.f, FAC_POLICE);
                if (sniper >= 0) {
                    int seat = freeSeat(vid, false);
                    if (seat > 0) {
                        warpPedIntoVehicle(sniper, vid, seat);
                        giveWeapon(sniper, WPN_SNIPER, 60);
                        peds[sniper].weapon = WPN_SNIPER;
                        peds[sniper].brain.type = BRAIN_COMBAT;
                        peds[sniper].brain.target = player;
                        peds[sniper].brain.accuracy = 0.5f + wanted * 0.04f;   // 0.66-0.7: marksman
                    } else {
                        despawnPed(sniper);
                    }
                }
                aiSay(drv, BK_COP_BACKUP, 1.f, true);
            }
        }
    }
    if (heli && wanted < 3) {
        // leave the area and let the population system despawn it far away
        vehAI(gD.heli).task = PT_RETURN;
    }
    // ---- police boats when the player takes to the water
    gD.boatTimer -= dt;
    if (wanted >= 1 && pv >= 0 && isBoat(pv) && gD.boatTimer <= 0.f) {
        gD.boatTimer = 15.f;
        int boats = 0;
        for (int vi = 0; vi < (int)vehicles.size(); vi++)
            if (vehicles[vi].used && vehicles[vi].faction == FAC_POLICE && isBoat(vi) && vehicles[vi].seats[0] >= 0) boats++;
        int model = -1;
        float bestLen = 0.f;
        for (int m = 0; m < (int)vassets.size(); m++)
            if (vassets[m].spec.cls == Vehicles::VC_BOAT && vassets[m].spec.boxHalf.y > bestLen) {
                bestLen = vassets[m].spec.boxHalf.y;
                model = m;
            }
        if (boats < (wanted >= 3 ? 2 : 1) && model >= 0) {
            for (int attempt = 0; attempt < 10; attempt++) {
                u32 h = hash32(gD.counter * 131u + attempt * 977u);
                float ang = hashToFloat(h) * kTwoPi;
                vec2 sp = pp.xy() + vec2(cosf(ang), sinf(ang)) * (150.f + hashToFloat(hash32(h)) * 90.f);
                bool open = true;
                for (int k = 0; k < 5 && open; k++) {
                    vec2 q = sp + vec2(cosf(k * 1.3f), sinf(k * 1.3f)) * 10.f;
                    open = map->isWater(q.x, q.y) && map->waterAt(q.x, q.y) - map->heightAt(q.x, q.y) > 1.5f;
                }
                float wl = map->waterAt(sp.x, sp.y);
                if (!open || inCameraView(vec3(sp, wl), 10.f)) continue;
                int vid = spawnVehicle(model, dvec3(sp.x, sp.y, wl + 0.4f), AI::dirYaw(normalize(pp.xy() - sp)), true, FAC_POLICE);
                if (vid < 0) break;
                Vehicle& v = vehicles[vid];
                v.faction = FAC_POLICE;
                v.color0 = vec3(0.9f, 0.91f, 0.92f);
                v.color1 = vec3(0.05f, 0.14f, 0.45f);
                v.sirenOn = true;
                v.sirenSilent = false;
                v.lightsOn = true;
                VehAI& va = vehAI(vid);
                va.role = VR_POLICE_BOAT;
                va.task = PT_PURSUE;
                int partner = spawnPed(randomCivilianChar(hash32(h * 3u), 1), v.sim.body.pos, 0.f, FAC_POLICE);
                if (partner >= 0) {
                    int seat = freeSeat(vid, false);
                    if (seat > 0) warpPedIntoVehicle(partner, vid, seat);
                    else {
                        despawnPed(partner);
                        partner = -1;
                    }
                }
                for (int o : {v.seats[0], partner}) {
                    if (o < 0) continue;
                    WeaponType wpn = o == partner ? WPN_RIFLE : WPN_PISTOL;
                    giveWeapon(o, wpn, 200);
                    peds[o].weapon = wpn;
                    peds[o].brain.type = BRAIN_COMBAT;
                    peds[o].brain.target = player;
                    peds[o].brain.accuracy = 0.45f;
                    pedAI(o).homeVeh = vid;
                }
                break;
            }
        }
    }
    // ---- searchlight (dusk to dawn, or always when searching)
    if (heli && renderer) {
        bool dark = env->timeOfDay < 7.2f || env->timeOfDay > 18.6f;
        if (dark) {
            vec3 hp = heli->sim.body.pos.toVec3();
            // on the suspect while in sight (the operator trails a moving target a little), otherwise sweeping a
            // figure-eight over the last known position
            vec3 want = pinfo.policeSeesPlayer ? pp : pinfo.lastSeenPos.toVec3() + vec3(sinf((float)time * 0.7f) * 18.f, sinf((float)time * 1.4f) * 9.f, 0.f);
            if (!gD.lightAimSet || length(gD.lightAim - want) > 120.f) gD.lightAim = want;
            gD.lightAimSet = true;
            gD.lightAim += (want - gD.lightAim) * expDecay(pinfo.policeSeesPlayer ? 3.2f : 1.4f, dt);
            vec3 aim = gD.lightAim;
            Render::DynamicLight dl;
            dl.pos = heli->sim.body.pos + dvec3(heli->sim.forward() * 2.f) - dvec3(0, 0, 1.2);
            dl.dir = normalize(aim - hp);
            dl.color = vec3(1.f, 0.97f, 0.9f) * 60000.f;
            dl.radius = 170.f;
            dl.spotCos = cosf(9.f * kDegToRad);
            dl.spotInner = cosf(5.f * kDegToRad);
            renderer->addLight(dl);
        }
    }
    // ---- roadblocks (3+ stars, player driving): two cruisers across the road ahead, spikes at 5 stars
    gD.roadblockTimer -= dt;
    for (Roadblock& rb : gD.blocks) {
        if (!rb.active) continue;
        rb.life += dt;
        bool anyAlive = false;
        for (int k = 0; k < 2; k++)
            if (rb.cars[k] >= 0 && rb.cars[k] < (int)vehicles.size() && vehicles[rb.cars[k]].used && vehicles[rb.cars[k]].uid == rb.uids[k]) anyAlive = true;
        vec2 toRb = rb.pos - pp.xy();
        bool behind = dot(toRb, rb.dir) > 0.f && length(toRb) > 60.f && rb.life > 8.f;   // player passed it
        if (!anyAlive || length(toRb) > 450.f || wanted < 3 || (behind && length(toRb) > 150.f)) {
            for (int k = 0; k < 2; k++)
                if (rb.cars[k] >= 0 && rb.cars[k] < (int)vehicles.size() && vehicles[rb.cars[k]].used && vehicles[rb.cars[k]].uid == rb.uids[k]) {
                    vehicles[rb.cars[k]].persistent = false;
                    vehAI(rb.cars[k]).role = VR_POLICE;
                }
            rb.active = false;
            continue;
        }
        // spike strip
        if (rb.spikes && pv >= 0) {
            Vehicle& v = vehicles[pv];
            vec2 a = rb.spikeA.xy(), b = rb.spikeB.xy();
            for (int w = 0; w < v.sim.wheelCount; w++) {
                if (v.sim.wheels[w].burst || !v.sim.wheels[w].contact) continue;
                vec3 wp = v.sim.body.pos.toVec3() + v.sim.wheels[w].contactPos;
                float t;
                if (distPointSegment2D(wp.xy(), a, b, &t) < 0.35f && fabsf(wp.z - rb.spikeA.z) < 1.f) {
                    Vehicles::burstTire(v.sim, w);
                    ai.stats.spikeHits++;
#ifdef HAVE_AUDIO
                    Audio::play(Audio::SFX_TIRE_POP, wp, 1.f);
#endif
                }
            }
        }
        if (rb.spikes && renderer && renderer->dynamic) {
            if (!gD.spikeModel) {
                MeshData md;
                u32 dark = packRGBA8(0.08f, 0.08f, 0.09f, 1.f), metal = makeMat(MAT_METAL_PAINTED);
                md.box(vec3(0, 0, 0.03f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.5f, 0.12f, 0.03f), dark, metal, false);
                for (int k = -4; k <= 4; k++) md.box(vec3(k * 0.11f, 0, 0.07f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.012f, 0.012f, 0.05f), packRGBA8(0.6f, 0.6f, 0.62f, 1.f), metal, false);
                gD.spikeModel = renderer->dynamic->createModel(md);
            }
            vec2 a = rb.spikeA.xy(), b = rb.spikeB.xy();
            float len = length(b - a);
            vec2 dir = (b - a) / Max(len, 0.01f);
            int n = Max(1, (int)(len / 1.0f));
            for (int k = 0; k < n; k++) {
                Render::DrawItem di;
                di.model = gD.spikeModel;
                vec2 c = a + dir * ((k + 0.5f) * len / n);
                di.pos = dvec3(c.x, c.y, rb.spikeA.z);
                di.rot = mat3(vec3(dir, 0.f), vec3(-dir.y, dir.x, 0.f), vec3(0, 0, 1));
                di.id = 0x700000000ull | (u64)((&rb - &gD.blocks[0]) * 64 + k);
                di.castShadow = false;
                renderer->dynamic->submit(di);
            }
        }
    }
    if (wanted >= 3 && pv >= 0 && gD.roadblockTimer <= 0.f && length(pvel) > 12.f) {
        gD.roadblockTimer = 35.f - wanted * 3.f;
        Roadblock* rb = nullptr;
        for (Roadblock& b : gD.blocks)
            if (!b.active) rb = &b;
        int model = findVehicleModel(Vehicles::VC_POLICE, gD.counter);
        // project the player's route ahead along the lane graph (the road they are following)
        float pu = 0.f;
        int lane = laneGraph.nearestLane(pp.xy(), normalize(pvel), 20.f, &pu);
        if (rb && model >= 0 && lane >= 0) {
            float ahead = 0.f;
            int cur = lane;
            float u = pu;
            float want = Clamp(length(pvel) * 9.f, 160.f, 320.f);
            // walk forward, preferring straight connectors
            for (int guard = 0; guard < 12 && ahead < want; guard++) {
                const AI::Lane& L = laneGraph.lanes[cur];
                ahead += L.u1 - u;
                if (ahead >= want) {
                    u = L.u1 - (ahead - want);
                    break;
                }
                int next = -1;
                for (int c : L.out)
                    if (laneGraph.conns[c].turn == AI::TK_STRAIGHT || laneGraph.conns[c].turn == AI::TK_MERGE) next = laneGraph.conns[c].to;
                if (next < 0 && !L.out.empty()) next = laneGraph.conns[L.out[0]].to;
                if (next < 0) break;
                cur = next;
                u = laneGraph.lanes[cur].u0;
            }
            const AI::Lane& L = laneGraph.lanes[cur];
            u = Clamp(u, L.u0 + 5.f, L.u1 - 5.f);
            vec3 c = laneGraph.lanePos(cur, u);
            vec2 t = laneGraph.laneTangent(cur, u);
            vec2 r = AI::rightOf(t);
            float rbDist = length(c.xy() - pp.xy());
            if ((!inCameraView(c, 10.f) && rbDist > 120.f) || rbDist > 230.f) {   // (a long straight: set up far down the road)
                // two cruisers angled across all lanes of this direction (a V with narrow gaps), officers behind them
                float span = L.count * L.width;
                float latC = ((L.count - 1) * 0.5f - (float)L.index) * L.width;   // carriageway center, right of this lane
                rb->active = true;
                ai.stats.roadblocks++;
                rb->pos = c.xy();
                rb->dir = t;
                rb->life = 0.f;
                rb->spikes = wanted >= 5;
                for (int k = 0; k < 2; k++) {
                    float lat = latC + (k == 0 ? -0.27f : 0.27f) * span;
                    vec2 cp = c.xy() + r * lat + t * (k * 2.5f);
                    float yaw = AI::dirYaw(t) + (k == 0 ? 1.15f : -1.15f);
                    int vid = spawnVehicle(model, dvec3(cp.x, cp.y, c.z + 0.4f), yaw, true, FAC_POLICE);
                    rb->cars[k] = vid;
                    if (vid < 0) continue;
                    Vehicle& v = vehicles[vid];
                    rb->uids[k] = v.uid;
                    v.faction = FAC_POLICE;
                    v.sirenOn = true;
                    v.sirenSilent = true;   // parked across the road: light bar only
                    v.persistent = true;
                    v.parked = true;
                    VehAI& va = vehAI(vid);
                    va.role = VR_ROADBLOCK;
                    va.task = PT_ROADBLOCK;
                    int drv = v.seats[0];
                    if (drv >= 0) {
                        removePedFromVehicle(drv, false);
                        Ped& o = peds[drv];
                        vec2 behindCar = cp + t * 3.5f;
                        o.pos = dvec3(behindCar.x, behindCar.y, groundHeight(behindCar.x, behindCar.y, c.z + 2.f));
                        giveWeapon(drv, wanted >= 4 ? WPN_RIFLE : WPN_SHOTGUN, 200);
                        o.weapon = wanted >= 4 ? WPN_RIFLE : WPN_SHOTGUN;
                        o.brain.type = BRAIN_COMBAT;
                        o.brain.target = player;
                        o.brain.accuracy = 0.4f + wanted * 0.04f;   // 0.52-0.6
                        PedAI& oa = pedAI(drv);
                        oa.homeVeh = vid;
                        oa.tactic = FT_COVER;
                        oa.coverVeh = vid;
                    }
                }
                if (rb->spikes) {
                    // spike strip across the carriageway 14 m before the cars
                    vec2 mid = c.xy() - t * 14.f + r * latC;
                    float halfSpan = span * 0.5f + 0.3f;
                    rb->spikeA = vec3(mid - r * halfSpan, c.z + 0.02f);
                    rb->spikeB = vec3(mid + r * halfSpan, c.z + 0.02f);
                }
            }
        }
    }
    // ---- spawn pursuit / response units
    if (wanted <= 0 || ai.dispatchOff) return;
    int wantCars = wanted == 1 ? 2 : (wanted == 2 ? 4 : (wanted == 3 ? 6 : (wanted == 4 ? 7 : 8)));
    if (carUnits >= wantCars || gD.spawnTimer > 0.f) return;
    gD.spawnTimer = Max(2.f, 8.f - wanted * 1.2f);
    bool swat = wanted >= 5 && (gD.counter % 3 == 0);
    int model = -1;
    if (swat) {
        model = findVehicleModel(Vehicles::VC_SUV, gD.counter);
        if (model < 0) model = findVehicleModel(Vehicles::VC_VAN, gD.counter);
    }
    if (model < 0) model = findVehicleModel(Vehicles::VC_POLICE, gD.counter);
    if (model < 0) return;
    // spawn on a lane 150-280 m away, out of view, ahead of the player's motion or near where they were last seen
    // (dispatch has a fair idea where a fresh getaway is: units come in around the suspect for a while after contact is
    // lost, then around the last sighting)
    vec2 around = pinfo.policeSeesPlayer || time - pinfo.lastSeenTime < 10.0 ? pp.xy() : pinfo.lastSeenPos.toVec3().xy();
    vec2 fwd = length(pvel) > 4.f ? normalize(pvel) : vec2(0, 0);
    float pspeed = length(pvel);
    // a fast getaway: most units come up from behind at speed (out of view behind the chase camera), the rest
    // wait ahead on the road facing the suspect (interceptors)
    bool fast = pspeed > 12.f && pv >= 0;
    bool fromBehind = fast && (gD.counter % 5u) < 3u;
    // behind the player on their own road: walk the lane graph backwards (straight-on predecessors first)
    int behindLane = -1;
    float behindU = 0.f;
    if (fromBehind) laneBehind(laneGraph, pp.xy(), fwd, 110.f + hashToFloat(hash32(gD.counter * 31u)) * 60.f, behindLane, behindU);
    for (int attempt = 0; attempt < 8; attempt++) {
        u32 h = hash32(gD.counter * 2246822519u + attempt * 97u);
        float ang = hashToFloat(h) * kTwoPi;
        vec2 dir(cosf(ang), sinf(ang));
        if (length2(fwd) > 0.f && attempt < 5) dir = normalize(fwd * (fromBehind ? -2.5f : 1.2f) + dir * (fast ? 0.5f : 1.f));   // prefer ahead
        float r = fromBehind ? 110.f + hashToFloat(hash32(h)) * 60.f : 150.f + hashToFloat(hash32(h)) * 130.f;
        vec2 probe = around + dir * r;
        float u = 0.f;
        vec2 wantHeading = fast && attempt < 5 ? (fromBehind ? fwd : -fwd) : vec2(0.f);
        int lane = attempt == 0 && behindLane >= 0 ? behindLane : laneGraph.nearestLane(probe, wantHeading, 50.f, &u);
        if (attempt == 0 && behindLane >= 0) u = behindU;
        if (lane < 0) continue;
        const AI::Lane& L = laneGraph.lanes[lane];
        if (L.flags & (AI::LF_DIRT | AI::LF_NOTRAFFIC)) continue;
        u = Clamp(u, L.u0 + 4.f, L.u1 - 8.f);
        vec3 c = laneGraph.lanePos(lane, u);
        if (inCameraView(c, 8.f) && length(c.xy() - pp.xy()) < 220.f) continue;
        if (!traffic.laneFree(lane, u, 3.f, 6.f)) continue;
        // (a unit that would have to go round the block first - one-way streets, no turn that way - comes from
        //  elsewhere: the first six tries want a way in at most 1.7 times as long as the straight line)
        if (!fast && attempt < 6) {
            float rl = aiRouteLength(lane, u, around);
            if (rl < 0.f || rl > length(c.xy() - around) * 1.7f + 40.f) continue;
        }
        vec2 t = laneGraph.laneTangent(lane, u);
        int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_POLICE);
        if (vid < 0) return;
        ai.stats.unitsSent++;
        LOG("police unit %d: %s at %.0f %.0f, %.0f m from the suspect (lane %d, attempt %d, suspect %.1f m/s)", vid,
            attempt == 0 && behindLane >= 0 ? "behind on the suspect's road" : (fast ? (fromBehind ? "behind (probe)" : "ahead") : "around"), c.x, c.y,
            length(c.xy() - pp.xy()), lane, attempt, pspeed);
        Vehicle& v = vehicles[vid];
        v.faction = FAC_POLICE;
        v.sirenOn = true;
        v.sirenSilent = false;
        if (swat) {
            v.color0 = vec3(0.03f, 0.03f, 0.035f);
            v.color1 = vec3(0.02f);
        }
        VehAI& va = vehAI(vid);
        va.role = swat ? VR_SWAT : VR_POLICE;
        va.task = PT_PURSUE;
        attachTraffic(vid, lane, u);
        v.sim.body.vel = vec3(t * (fromBehind ? Min(pspeed + 2.f, 40.f) : 10.f), 0.f);
        int drv = v.seats[0];
        WeaponType wpn = swat ? WPN_RIFLE : (wanted >= 3 ? (h & 1 ? WPN_SHOTGUN : WPN_RIFLE) : WPN_PISTOL);
        auto arm = [&](int pid) {
            giveWeapon(pid, wpn, 240);
            peds[pid].weapon = wpn;
            peds[pid].brain.type = wanted <= 1 ? BRAIN_ARREST : BRAIN_COMBAT;
            peds[pid].brain.target = player;
            peds[pid].brain.accuracy = swat ? 0.72f : 0.4f + wanted * 0.04f;   // patrol 0.44-0.6, SWAT 0.72
            if (swat) {
                peds[pid].armor = 100.f;
                peds[pid].maxHealth = peds[pid].health = 140.f;
            }
            pedAI(pid).homeVeh = vid;
        };
        if (drv >= 0) arm(drv);
        int crew = swat ? 3 : 1;
        for (int k = 0; k < crew; k++) {
            int seat = freeSeat(vid, false);
            if (seat <= 0) break;
            int ci = randomCivilianChar(h >> (5 + k), 1);
            int partner = spawnPed(ci, v.sim.body.pos, 0.f, FAC_POLICE);
            if (partner < 0) break;
            warpPedIntoVehicle(partner, vid, seat);
            arm(partner);
        }
        return;
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Pursuit / response driving for police cars (and hostile drivers chasing a target)
void GameWorld::aiPoliceDrive(int vi, float dt) {
    Vehicle& v = vehicles[vi];
    VehAI& va = vehAI(vi);
    int drv = v.seats[0];
    if (drv < 0) return;
    Ped& dp = peds[drv];
    Brain& b = dp.brain;
    Ped* pl = playerPed();
    va.repath -= dt;
    va.barkTimer -= dt;
    // ---- helicopter: orbit the target (or search the last seen area)
    if (isAircraft(vi)) {
        if (va.task == PT_RETURN || !pl) {
            vec3 p = v.sim.body.pos.toVec3();
            vec2 away = normalize(p.xy() - (pl ? pl->pos.toVec3().xy() : vec2(0)) + vec2(1e-3f, 0.f));
            aiFlyHeli(vi, dt, dvec3(vec3(p.xy() + away * 400.f, 0.f)), 140.f, 0.f, false);
            if (pl && length(p.xy() - pl->pos.toVec3().xy()) > 700.f) despawnVehicle(vi, true);
            return;
        }
        bool seen = pinfo.policeSeesPlayer;
        vec3 tgt = seen ? pl->pos.toVec3() : pinfo.lastSeenPos.toVec3();
        float alt = tgt.z + 48.f;
        aiFlyHeli(vi, dt, dvec3(tgt), alt, seen ? 55.f : 95.f, true);
        return;
    }
    // ---- boats: close in fast, then hold a firing position beside the target
    if (isBoat(vi)) {
        vec3 me = v.sim.body.pos.toVec3();
        vec3 tgt = pl ? pl->pos.toVec3() : me;
        vec2 to = tgt.xy() - me.xy();
        float dist = length(to);
        vec2 side = AI::rightOf(to / Max(dist, 1e-3f)) * ((v.uid & 1) ? 14.f : -14.f);
        vec3 goal = dist > 45.f ? tgt : vec3(tgt.xy() + side, tgt.z);
        aiDriveBoat(vi, dt, dvec3(goal), dist > 45.f ? 24.f : 12.f);
        return;
    }
    AI::Driver* d = traffic.get(vi);
    if (!d) {
        attachTraffic(vi);
        d = traffic.get(vi);
    }
    // ---- prisoner transport (police_escort::callTransport): to the officer holding a suspect, no siren; pulled over
    //      at the kerb next to them, the light bar going, until the prisoner is in the back - then back on patrol
    if (va.task == PT_TRANSPORT) {
        int cop = va.transportFor;
        bool copOk = cop >= 0 && cop < (int)peds.size() && cop < (int)ai.ped.size() && peds[cop].used && peds[cop].uid == va.transportUid &&
                     police_escort::escorting(peds[cop]) && ai.ped[cop].escortCar == vi;
        if (!copOk || !d) {
            police_escort::releaseTransport(*this, vi, cop);
            va.task = PT_NONE;
            return;
        }
        if (d->dummy) traffic.toPhysics(vi, v.sim);
        vec2 goal = peds[cop].pos.toVec3().xy();
        float dist = length(goal - v.sim.body.pos.toVec3().xy());
        va.taskTimer += dt;
        if (va.transportState == 0) {
            if (d->mode != AI::DM_NORMAL) d->mode = AI::DM_NORMAL;
            if (!d->hasDest || length(d->dest - goal) > 25.f || d->destRecalc <= 0.f) traffic.setDestination(*d, goal);
            // there (or as near as it gets: held up within a short walk of them for longer than a red light lasts - a
            // stop line close ahead, a light that will change, is given longer)
            va.stopTimer = v.sim.speed() < 1.f ? va.stopTimer + dt : 0.f;
            // (a short walk off and stuck in a jam: pulled over where it is - they walk the prisoner to it)
            float patience = dist < 60.f ? 12.f : (d->stopDist < 45.f ? 75.f : 30.f);
            if (dist < 30.f || (dist < 85.f && va.stopTimer > patience)) {
                d->mode = AI::DM_PULLOVER;
                d->holdTimer = -1.f;
                va.transportState = 1;
                va.taskTimer = 0.f;
            }
        } else if (va.transportState == 1) {
            if (d->mode != AI::DM_PULLOVER || (va.taskTimer > 20.f && dist > 70.f)) {
                va.transportState = 0;   // (pushed out of it, or ran on past them: round again)
                d->mode = AI::DM_NORMAL;
            } else if (v.sim.speed() < 0.3f && va.taskTimer > 1.5f) {
                va.transportState = 2;
                va.taskTimer = 0.f;
                v.sirenOn = true;
                v.sirenSilent = true;
                LOG("police: transport %d pulled over %.0f m from officer %d", vi, dist, cop);
            }
        }
        AI::DriveOut out;
        traffic.drive(vi, v.sim, dt, out);
        v.ctl = out.ctl;
        v.indicator = va.transportState == 2 ? 2 : out.indicator;
        v.hornOn = false;
        if (va.transportState == 2) {
            v.ctl = Vehicles::VehicleControls();
            v.ctl.hasDriver = true;
            v.ctl.brake = 1.f;
            v.ctl.handbrake = true;   // (the brake pedal alone at a standstill is reverse)
        }
        return;
    }
    // ---- sent to a scene with nothing to chase (police_scene: a fender bender): there at an easy pace, pulled over
    //      behind it, the light bar going, until the crew is let go (events.cpp)
    if (va.task == PT_SCENE) {
        if (!d) {
            va.task = PT_NONE;
            return;
        }
        if (d->dummy) traffic.toPhysics(vi, v.sim);
        vec2 goal = va.taskPos;
        float dist = length(goal - v.sim.body.pos.toVec3().xy());
        va.taskTimer += dt;
        if (va.transportState == 0) {
            if (d->mode != AI::DM_NORMAL) d->mode = AI::DM_NORMAL;
            if (!d->hasDest || length(d->dest - goal) > 25.f || d->destRecalc <= 0.f) traffic.setDestination(*d, goal);
            va.stopTimer = v.sim.speed() < 1.f ? va.stopTimer + dt : 0.f;
            if (dist < 28.f || (dist < 70.f && va.stopTimer > 12.f)) {
                d->mode = AI::DM_PULLOVER;
                d->holdTimer = -1.f;
                va.transportState = 1;
                va.taskTimer = 0.f;
            }
        } else if (va.transportState == 1) {
            if (d->mode != AI::DM_PULLOVER || (va.taskTimer > 20.f && dist > 60.f)) {
                va.transportState = 0;   // (pushed out of it, or ran on past: round again)
                d->mode = AI::DM_NORMAL;
            } else if (v.sim.speed() < 0.3f && va.taskTimer > 1.5f) {
                va.transportState = 2;
                va.taskTimer = 0.f;
                v.sirenOn = true;
                v.sirenSilent = true;
                LOG("police: unit %d pulled over at the scene, %.0f m off", vi, dist);
            }
        }
        if (va.transportState != 2 && va.taskTimer > 180.f) {   // (never got there)
            va.task = PT_NONE;
            return;
        }
        AI::DriveOut out;
        traffic.drive(vi, v.sim, dt, out);
        v.ctl = out.ctl;
        v.indicator = va.transportState == 2 ? 2 : out.indicator;
        v.hornOn = false;
        if (va.transportState == 2) {
            v.ctl = Vehicles::VehicleControls();
            v.ctl.hasDriver = true;
            v.ctl.brake = 1.f;
            v.ctl.handbrake = true;
        }
        return;
    }
    // ---- target
    int target = b.target >= 0 && b.target < (int)peds.size() && peds[b.target].used ? b.target : -1;
    bool chasingPlayer = target >= 0 && target == player;
    vec3 tp;
    vec3 tv(0.f);
    int targetVeh = -1;
    bool tpReal = false;   // tp is the target itself (not a search point / extrapolated heading)
    if (target >= 0) {
        tpReal = true;
        const Ped& t = peds[target];
        targetVeh = t.vehicle;
        if (targetVeh >= 0) {
            tp = vehicles[targetVeh].sim.body.pos.toVec3();
            tv = vehicles[targetVeh].sim.body.vel;
        } else {
            tp = t.pos.toVec3();
            tv = t.vel;
        }
        float lostFor = (float)(time - pinfo.lastSeenTime);
        if (chasingPlayer && !pinfo.policeSeesPlayer && lostFor > 2.5f && lostFor < 9.f && length(gD.lastSeenVel.xy()) > 6.f) {
            // just lost sight of a fast getaway: follow the radioed heading before fanning out to search
            tp = pinfo.lastSeenPos.toVec3() + gD.lastSeenVel * lostFor;
            tv = gD.lastSeenVel;
            va.task = PT_PURSUE;
            tpReal = false;
        } else if (chasingPlayer && !pinfo.policeSeesPlayer && lostFor > 2.5f) {
            // lost sight: search the last known area
            tp = pinfo.lastSeenPos.toVec3();
            tv = vec3(0.f);
            tpReal = false;
            if (va.task != PT_SEARCH || length(v.sim.body.pos.toVec3().xy() - va.taskPos) < 20.f || va.taskTimer <= 0.f) {
                va.task = PT_SEARCH;
                float R = searchRadiusFor(Max(pinfo.wanted, 1)) * 0.8f;
                u32 h = hash32(v.uid * 131u + (u32)(time * 0.5));
                float ang = hashToFloat(h) * kTwoPi, rr = sqrtf(hashToFloat(hash32(h))) * R;
                va.taskPos = tp.xy() + vec2(cosf(ang), sinf(ang)) * rr;
                va.taskTimer = 25.f;
                if (d) traffic.setDestination(*d, va.taskPos);
                if (va.barkTimer <= 0.f) {
                    aiSay(drv, BK_COP_SEARCH, 0.4f);
                    va.barkTimer = 20.f;
                }
            }
            va.taskTimer -= dt;
            tp = vec3(va.taskPos, tp.z);
        } else if (chasingPlayer) {
            va.task = PT_PURSUE;
        }
    } else if (va.task == PT_RESPOND) {
        tp = vec3(va.taskPos, v.sim.body.pos.toVec3().z);
    } else {
        // nothing to chase: normal traffic
        if (d) {
            d->mode = AI::DM_NORMAL;
            d->hasDest = false;
        }
        va.task = PT_NONE;
        AI::DriveOut out;
        traffic.drive(vi, v.sim, dt, out);
        v.ctl = out.ctl;
        v.indicator = out.indicator;
        return;
    }
    vec3 vp = v.sim.body.pos.toVec3();
    vec2 to = tp.xy() - vp.xy();
    float dist = length(to);
    float mySpeed = v.sim.speed();
    // ---- catch-up: a unit far behind and out of view is moved closer along the roads
    if (chasingPlayer && va.task == PT_PURSUE && dist > 230.f && !inCameraView(vp, 10.f) && va.repath <= 0.f && d) {   // (not while searching)
        va.repath = 6.f;
        vec2 back = length2(tv.xy()) > 4.f ? -normalize(tv.xy()) : normalize(vp.xy() - tp.xy());
        float u = 0.f;
        int lane = -1;
        if (length2(tv.xy()) > 16.f) laneBehind(laneGraph, tp.xy(), normalize(tv.xy()), 140.f, lane, u);
        if (lane < 0) lane = laneGraph.nearestLane(tp.xy() + back * 140.f, length2(tv.xy()) > 4.f ? normalize(tv.xy()) : vec2(0), 60.f, &u);
        if (lane >= 0) {
            vec3 c = laneGraph.lanePos(lane, u);
            if (!inCameraView(c, 10.f) && traffic.laneFree(lane, u, 3.f, 5.f)) {
                vec2 t = laneGraph.laneTangent(lane, u);
                Vehicles::resetVehicle(v.sim, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t));
                v.sim.body.vel = vec3(t * Min(length(tv) + 2.f, 40.f), 0.f);   // arrives at chase speed
                traffic.toPhysics(vi, v.sim);
                d->path = lane;
                d->u = u;
                traffic.clearRoute(*d);
                vp = c;
                to = tp.xy() - vp.xy();
                dist = length(to);
            }
        }
    }
    if (d && d->dummy) traffic.toPhysics(vi, v.sim);
    // ---- far or no line of sight: navigate the road graph with the siren (emergency rules)
    bool direct = dist < 65.f && (targetVeh < 0 || dist < 45.f);
    if (direct && va.repath <= 0.f) {
        va.repath = 0.4f;
        dvec3 eye = v.sim.body.pos + dvec3(0, 0, 1.3);
        dvec3 aimPt = tpReal ? sightPoint(*this, peds[target], eye.toVec3()) : dvec3(tp) + dvec3(0, 0, 1.0);
        va.taskTimer = lineOfSight(eye, aimPt, drv, vi) ? 1.f : 0.f;
    }
    if (direct && va.taskTimer < 0.5f && dist > 20.f) direct = false;
    if (!direct && d) {
        // the last stretch to a suspect on foot: no more emergency driving (through the zebras at 1.35 times the limit,
        // past people waiting for the lights) - the siren on, but with an eye on the people crossing; held up there,
        // the officers go the rest of the way on foot (below)
        AI::DriveMode want = targetVeh < 0 && tpReal && dist < 90.f ? AI::DM_NORMAL : AI::DM_EMERGENCY;
        if (d->mode != want) d->mode = want;
        if (chasingPlayer) {
            // pursuit driving: well over the limit when the suspect is fleeing fast (reset on stand-down)
            float tsp = length(tv.xy());
            d->pers.speedFactor = Clamp(1.1f + tsp / 25.f, 1.2f, 1.9f);
            d->pers.accel = Max(d->pers.accel, 3.6f);
            d->pers.latAcc = Max(d->pers.latAcc, 4.2f);
        }
        // every third unit drives to where the suspect is heading (cut-off / pincer), the others follow the trail - not
        // while the car is spinning (donuts in a takeover, a skid): where it points then changes by the second, and so
        // would the route
        vec2 goal = tp.xy();
        if (targetVeh >= 0 && (v.uid % 3) == 0 && length(tv.xy()) > 8.f && fabsf(vehicles[targetVeh].sim.body.angVel.z) < 0.6f)
            goal += tv.xy() * Clamp(dist / 28.f, 1.5f, 7.f);
        if (!d->hasDest || length(d->dest - goal) > 40.f || d->destRecalc <= 0.f) traffic.setDestination(*d, goal);
        v.sirenOn = true;
        v.sirenSilent = false;
        AI::DriveOut out;
        traffic.drive(vi, v.sim, dt, out);
        v.ctl = out.ctl;
        v.indicator = 0;
        v.hornOn = out.horn;
        // held up in traffic out of the player's sight (a queue at a red light, a box jammed by cross traffic): on past
        // it along the route, as a unit with the siren going would have got round it on the wrong side of the road;
        // stuck within a short run of a suspect on foot: out of the car and the rest of the way on foot
        // (held up: stopped or crawling in stop-and-go traffic - the time builds while slower than 3 m/s and wears off
        //  while moving faster)
        va.heldUp = Clamp(va.heldUp + (mySpeed < 3.f ? dt : -dt), 0.f, 10.f);
        // (stopped there for a few seconds within a short run of them, or crawling for longer a bit further off: people
        //  in the road, a queue creeping along - a wait at a light further out is sat out in the car)
        if (((va.heldUp > 3.f && mySpeed < 1.f && dist < 45.f) || (va.heldUp > 8.f && dist < 75.f)) && targetVeh < 0 && tpReal) {
            for (int s = 0; s < 8; s++) {
                int o = v.seats[s];
                if (o < 0 || peds[o].isPlayer || peds[o].faction != FAC_POLICE) continue;   // (a prisoner in the back stays there)
                removePedFromVehicle(o, true);
                PedAI& oa = pedAI(o);
                oa.homeVeh = vi;
                oa.coverVeh = -1;   // (too far from the scene to take cover behind it)
                oa.tactic = s % 2 ? FT_FLANK : FT_APPROACH;
                oa.tacticTimer = 0.f;
            }
            traffic.detach(vi);
            va.managed = false;
            v.sirenOn = true;
            v.sirenSilent = true;
            v.ctl = Vehicles::VehicleControls();
            v.ctl.brake = 1.f;
            LOG("police unit %d: stuck in traffic %.0f m out, the officers go on foot", vi, dist);
            return;
        }
        if (va.heldUp > 5.f && !d->dummy && pl && length(rel(v.sim.body.pos, pl->pos)) > 60.f && !inCameraView(vp, 12.f)) {
            va.heldUp = 0.f;
            float hl = vassets[v.model].spec.boxHalf.y;
            for (float ahead = 12.f; ahead <= 66.f; ahead += 6.f) {
                int pth = -1;
                float uu = 0.f;
                if (!traffic.liftPoint(*d, ahead, pth, uu) || !laneGraph.isLane(pth)) continue;
                if (!traffic.laneFree(pth, uu, hl + 1.f, 4.f)) continue;
                vec3 c = laneGraph.lanePos(pth, uu);
                if (inCameraView(c, 12.f) || length(c.xy() - pl->pos.toVec3().xy()) < 50.f) break;
                vec2 t = laneGraph.laneTangent(pth, uu);
                Vehicles::resetVehicle(v.sim, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t));
                v.sim.body.vel = vec3(t * 6.f, 0.f);
                traffic.toPhysics(vi, v.sim);
                d->path = pth;
                d->u = uu;
                traffic.clearRoute(*d);
                LOG("police unit %d: past a hold-up in traffic, %.0f m on (%.0f m from the suspect)", vi, ahead, length(c.xy() - pl->pos.toVec3().xy()));
                break;
            }
        }
        return;
    }
    // ---- loudspeaker: order the driver to pull over while a low-level (1-2 star) pursuit is close
    va.megaphoneTimer -= dt;
    if (chasingPlayer && targetVeh >= 0 && pinfo.wanted <= 2 && dist < 40.f && va.megaphoneTimer <= 0.f) {
        int spk = v.seats[1] >= 0 && !peds[v.seats[1]].isPlayer ? v.seats[1] : drv;
        if (spk >= 0) aiSay(spk, BK_COP_MEGAPHONE, 1.f, true);
        va.megaphoneTimer = 8.f + hashToFloat(hash32(v.uid * 13u + (u32)time)) * 4.f;
    }
    // ---- direct pursuit
    Vehicles::VehicleControls& c = v.ctl;
    c = Vehicles::VehicleControls();
    vec3 fwd3 = v.sim.forward();
    vec2 fwd = normalize(fwd3.xy() + vec2(1e-5f, 0.f));
    vec2 rgt = AI::rightOf(fwd);
    float tSpeed = length(tv.xy());
    vec2 aim = tp.xy();
    bool onFoot = targetVeh < 0;
    int wanted = chasingPlayer ? pinfo.wanted : 3;
    if (!onFoot) {
        // lead the target
        float lead = Clamp(dist / Max(mySpeed, 8.f), 0.f, 1.6f);
        aim += tv.xy() * lead;
        // PIT: from behind at speed, aim at the rear quarter of the target
        vec2 tf = tSpeed > 1.f ? normalize(tv.xy()) : fwd;
        float behind = dot(vp.xy() - tp.xy(), tf);
        u8 move = 0;
        if (wanted >= 2 && tSpeed > 9.f && behind < -2.f && dist < 18.f) {
            float side = dot(vp.xy() - tp.xy(), AI::rightOf(tf)) >= 0.f ? 1.f : -1.f;
            aim = tp.xy() - tf * 1.6f + AI::rightOf(tf) * (side * 0.6f);
            move = 1;
        }
        // boxing in a slow target: take positions around it
        if (tSpeed < 4.f && dist < 30.f) {
            int slot = (int)(v.uid % 3);
            vec2 tf2 = length2(tv.xy()) > 0.25f ? tf : normalize(tp.xy() - vp.xy() + vec2(1e-3f, 0.f));
            vec2 offs[3] = {tf2 * 6.5f + AI::rightOf(tf2) * 1.5f, -tf2 * 6.5f, AI::rightOf(tf2) * -3.2f};
            aim = tp.xy() + offs[slot];
            move = 2;
        }
        if (move != va.pursuitMove) {
            if (move == 1) ai.stats.pitTries++;
            if (move == 2) ai.stats.boxing++;
            va.pursuitMove = move;
        }
    }
    vec2 toAim = aim - vp.xy();
    float aimDist = length(toAim);
    // avoid bodies in the way (other than the target): steer around the nearest one ahead
    vec2 wantDir = aimDist > 0.1f ? toAim / aimDist : fwd;
    {
        int self = vi < (int)ai.vehBody.size() ? ai.vehBody[vi] : -1;
        int tb = targetVeh >= 0 && targetVeh < (int)ai.vehBody.size() ? ai.vehBody[targetVeh] : (target >= 0 && target < (int)ai.pedBody.size() ? ai.pedBody[target] : -1);
        float look = Clamp(mySpeed * 1.8f + 6.f, 8.f, 45.f);
        float bestAlong = 1e9f;
        float steerAway = 0.f;
        traffic.hash.query(traffic.bodies, vp.xy() - vec2(look), vp.xy() + vec2(look), [&](int bi) {
            if (bi == self || bi == tb) return;
            const AI::Body& ob = traffic.bodies[bi];
            if (fabsf(ob.z - vp.z) > 3.f) return;
            vec2 rp = ob.pos - vp.xy();
            float along = dot(rp, wantDir), lat = dot(rp, AI::rightOf(wantDir));
            float half = 1.1f + ob.halfWid + (ob.kind == AI::BK_PED ? 0.6f : 0.3f);
            if (along < 0.f || along > look || fabsf(lat) > half) return;
            if (along < bestAlong) {
                bestAlong = along;
                steerAway = lat >= 0.f ? -1.f : 1.f;   // pass on the other side
            }
        });
        if (bestAlong < 1e8f) wantDir = normalize(wantDir + AI::rightOf(wantDir) * (steerAway * Clamp(2.2f - bestAlong / look * 2.f, 0.4f, 1.6f)));
    }
    float err = atan2f(cross(fwd, wantDir), dot(fwd, wantDir));   // + = left
    float fs = v.sim.forwardSpeed();
    // turn-around when the target is behind
    if (fabsf(err) > 2.2f && dist < 40.f && fs < 6.f) {
        va.reverseTimer = 1.2f;
    }
    if (va.reverseTimer > 0.f) {
        va.reverseTimer -= dt;
        c.brake = 0.8f;
        c.steer = err > 0.f ? 1.f : -1.f;   // reversing: opposite lock swings the nose toward the target
        v.sirenOn = true;
        v.sirenSilent = false;
        return;
    }
    c.steer = Clamp(-err * 2.0f + v.sim.body.angVel.z * 0.08f, -1.f, 1.f);
    float targetSpeed;
    if (onFoot) {
        // stop short of the suspect, officers get out (and nobody under the wheels - the suspect included: anyone on
        // foot just ahead in the car's path, brake)
        targetSpeed = Clamp((dist - 12.f) * 0.6f, 0.f, 16.f);
        float stopFor = Max(fs, 0.f) * 1.1f + 5.f, halfW = vassets[v.model].spec.boxHalf.x + 0.9f;
        traffic.hash.query(traffic.bodies, vp.xy() - vec2(stopFor), vp.xy() + vec2(stopFor), [&](int bi) {
            const AI::Body& ob = traffic.bodies[bi];
            if (ob.kind != AI::BK_PED || fabsf(ob.z - vp.z) > 3.f) return;
            vec2 rp = ob.pos - vp.xy();
            float along = dot(rp, fwd), lat = dot(rp, rgt);
            if (along > 0.f && along < stopFor && fabsf(lat) < halfW) targetSpeed = 0.f;
        });
    } else {
        float follow = tSpeed + Clamp((dist - 9.f) * 0.5f, -4.f, 14.f);
        targetSpeed = Clamp(follow, 4.f, 55.f);
        if (tSpeed < 4.f && dist < 30.f) targetSpeed = Clamp((aimDist - 1.5f) * 0.9f, 0.f, 10.f);
    }
    targetSpeed *= 1.f - Saturate(fabsf(err) - 0.35f) * 0.55f;
    float e2 = targetSpeed - fs;
    if (e2 > 0.f) c.throttle = Saturate(e2 * 0.25f + 0.2f);
    else c.brake = Saturate(-e2 * 0.22f);
    if (targetSpeed < 0.5f && fs < 1.f) {
        c.brake = 1.f;
        c.handbrake = true;
        c.throttle = 0.f;
    }
    // stuck against something: reverse and retry
    if (c.throttle > 0.3f && fabsf(fs) < 0.5f) va.stuckTimer += dt;
    else va.stuckTimer = Max(0.f, va.stuckTimer - dt);
    if (va.stuckTimer > 2.5f) {
        va.stuckTimer = 0.f;
        va.reverseTimer = 1.4f;
    }
    v.sirenOn = true;
    v.sirenSilent = false;
    // chatter
    gD.chatterTimer -= dt;
    if (chasingPlayer && gD.chatterTimer <= 0.f && dist < 120.f) {
        gD.chatterTimer = 14.f + hashToFloat(hash32(gD.counter)) * 10.f;
        aiSay(drv, BK_COP_CHATTER, 0.9f);
    }
    // officers get out near a suspect on foot, or when the target vehicle is stopped - or, held up short of one on foot
    // (people in the way, the car boxed in), out and the rest of the way on foot
    va.heldUp = Clamp(va.heldUp + (fs < 1.f ? dt : -dt), 0.f, 10.f);
    bool heldShort = onFoot && dist < 75.f && va.heldUp > 3.f && fs < 1.f;
    if ((onFoot && dist < 26.f && fs < 3.f) || heldShort || (!onFoot && tSpeed < 1.f && dist < 16.f && fs < 2.f)) {
        if (heldShort && dist >= 26.f) LOG("police unit %d: held up %.0f m short of the suspect, the officers go on foot", vi, dist);
        for (int s = 0; s < 8; s++) {
            int o = v.seats[s];
            if (o < 0 || peds[o].isPlayer || peds[o].faction != FAC_POLICE) continue;   // (a prisoner in the back stays there)
            removePedFromVehicle(o, true);
            PedAI& oa = pedAI(o);
            oa.homeVeh = vi;
            oa.coverVeh = vi;
            oa.tactic = s == 0 ? FT_COVER : (s % 2 ? FT_FLANK : FT_APPROACH);
            oa.tacticTimer = 0.f;
        }
        if (d) traffic.detach(vi);
        vehAI(vi).managed = false;
        v.sirenOn = true;
        v.sirenSilent = true;   // left at the scene: light bar only
        if (chasingPlayer) aiSay(drv, pinfo.wanted <= 1 ? BK_COP_FREEZE : BK_COP_ENGAGE, 1.f, true);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Officers on foot: arrest (low wanted), cover behind their car, flanking, engaging, searching, returning to the car.
std::string GameWorld::aiK9Text(vec3* dogPos) const {
    bool active = k9Active(*this);
    bool dog = active && Wildlife::k9Alive(gK9.dog, gK9.dogUid);
    vec3 dp = dog ? Wildlife::k9Pos(gK9.dog) : vec3(0.f);
    if (dogPos) *dogPos = dp;
    if (!active) return StrFormat("k9: none (trail %d%s, cooldown %.0f)", (int)gTrail.size(), gTrailEnds ? " ends" : "", gK9.cooldown);
    vec3 hp = peds[gK9.handler].pos.toVec3();
    return StrFormat("k9: handler %d at %.1f %.1f dog %d at %.1f %.1f%s | trail %d/%d%s | found %.1f lost %.1f sent %.1f", gK9.handler, hp.x, hp.y, dog ? gK9.dog : -1, dp.x,
                     dp.y, dog && Wildlife::k9Loose(gK9.dog) ? " loose" : "", gK9.next, (int)gTrail.size(), gTrailEnds ? " ends" : "", gK9.foundT, gK9.lostT, gK9.sentT);
}

void GameWorld::aiPoliceBrain(int id, float dt) {
    Ped& p = peds[id];
    Brain& b = p.brain;
    PedAI& pa = pedAI(id);
    vec2 pos = p.pos.toVec3().xy();
    pa.tacticTimer -= dt;
    pa.shoutTimer -= dt;
    Ped* pl = playerPed();
    vec2 desired(0, 0);
    float faceYaw = p.yaw;
    int stance = 0;
    p.aiming = false;
    p.firing = false;
    // walking a prisoner to the car (police_escort)
    if (police_escort::escorting(p)) {
        p.phoneBrowse = false;
        police_escort::escortStep(*this, id, dt);
        return;
    }
    // taking a witness's statement (police_statement)
    if (police_statement::taking(p)) {
        police_statement::step(*this, id, dt);
        return;
    }
    // stopping somebody on the sidewalk (police_stop)
    if (police_stop::stopping(p)) {
        police_stop::step(*this, id, dt);
        return;
    }
    // writing up a parked car (police_ticket)
    if (police_ticket::writing(p)) {
        police_ticket::step(*this, id, dt);
        return;
    }
    p.phoneBrowse = false;   // (the device away: called off a statement or a stop)
    if (pa.activity == ACT_COP_BREAK) {   // (called off a coffee break: the cup gone, on duty)
        pa.activity = ACT_WALK;
        p.carry = CARRY_NONE;
    }
    // returning to the car / patrol
    if (b.type == BRAIN_GOTO && b.target == -2) {
        int hv = pa.homeVeh;
        if (hv >= 0 && hv < (int)vehicles.size() && vehicles[hv].used && !vehicles[hv].exploded) {
            vec3 vp3 = vehicles[hv].sim.body.pos.toVec3();
            vec2 tov = vp3.xy() - pos;
            float dv = length(tov);
            if (dv > 3.f) {
                vec2 st = aiWalkRound(*this, id, vp3.xy(), 2.6f, dt) - pos;   // (round a bench or a planter on the way)
                desired = st / Max(length(st), 1e-3f) * 2.6f;
                faceYaw = atan2f(-desired.x, desired.y);
                // (the crew waits for this one: nobody drives off with an officer still walking back - one getting
                //  nowhere, held up against something, is not waited for)
                if (dv < 150.f && !vehicles[hv].playerUsed && length(p.vel.xy()) > 0.6f) vehAI(hv).escortHold = Max(vehAI(hv).escortHold, time + 2.0);
            } else if (time < vehAI(hv).escortHold) {
                // a colleague is bringing a prisoner to it (or still on the way back): wait by the car - now and then a
                // word on the radio. One walking a prisoner here from a long way off: behind the wheel and round to them
                // instead (the car pulls over by them as a transport would: police_escort waits for it, the prisoner
                // on the kerb)
                int esc = -1;
                if (vehicles[hv].seats[0] < 0 && vehAI(hv).task != PT_TRANSPORT && !vehicles[hv].playerUsed && !isAircraft(hv) && !isBoat(hv))
                    for (int i = 0; i < (int)peds.size() && i < (int)ai.ped.size() && esc < 0; i++) {
                        const Ped& c = peds[i];
                        const PedAI& ca = ai.ped[i];
                        if (i == id || !c.used || ca.uid != c.uid || !police_escort::escorting(c) || ca.escortCar != hv || ca.escortCarUid != vehicles[hv].uid)
                            continue;
                        int s = ca.escortPed;
                        bool walking = s >= 0 && s < (int)peds.size() && peds[s].used && peds[s].state == PS_ONFOOT;
                        if (walking && length(rel(peds[s].pos, vehicles[hv].sim.body.pos)) > 32.f) esc = i;
                    }
                if (esc >= 0) {
                    warpPedIntoVehicle(id, hv, 0);
                    b.type = BRAIN_DRIVER;
                    b.target = -1;
                    Vehicle& hvv = vehicles[hv];
                    hvv.parked = false;
                    hvv.sirenOn = false;
                    hvv.sirenSilent = false;
                    VehAI& hva = vehAI(hv);
                    hva.role = VR_POLICE;
                    hva.task = PT_TRANSPORT;
                    hva.transportFor = esc;
                    hva.transportUid = peds[esc].uid;
                    hva.transportState = 0;
                    hva.taskTimer = 0.f;
                    hva.stopTimer = 0.f;
                    LOG("police: officer %d brings unit %d round to officer %d and the prisoner (%.0f m)", id, hv, esc,
                        length(rel(peds[esc].pos, hvv.sim.body.pos)));
                    return;
                }
                faceYaw = atan2f(-tov.x, tov.y) + kPi * 0.5f;
                if (pa.shoutTimer <= 0.f) {
                    aiSay(id, BK_COP_RADIO, 0.35f);
                    pa.shoutTimer = 9.f + hashToFloat(hash32(p.uid + (u32)time)) * 8.f;
                }
            } else {
                bool driverFree = vehicles[hv].seats[0] < 0;
                int seat = driverFree ? 0 : freeSeat(hv, false);
                if (seat >= 0) {
                    warpPedIntoVehicle(id, hv, seat);
                    b.type = seat == 0 ? BRAIN_DRIVER : BRAIN_PASSENGER;
                    b.target = -1;
                    vehicles[hv].sirenOn = false;
                    vehicles[hv].sirenSilent = false;
                    vehicles[hv].parked = false;
                    vehAI(hv).task = PT_NONE;
                    vehAI(hv).role = VR_POLICE;
                    vehAI(hv).copBreak = 0;
                    return;
                }
            }
        } else {
            b.type = BRAIN_WANDER;
            b.edge = -1;
            b.target = -1;
            pedAI(id).navOk = false;
            return;
        }
        float d = AI::wrapPi(faceYaw - p.yaw);
        p.yaw = AI::wrapPi(p.yaw + Clamp(d, -8.f * dt, 8.f * dt));
        movePed(p, desired, dt, false);
        return;
    }
    if (!p.target_is_valid(*this)) {
        b.type = BRAIN_GOTO;
        b.target = -2;
        return;
    }
    Ped& t = peds[b.target];
    bool targetIsPlayer = b.target == player;
    vec2 tp = t.pos.toVec3().xy();
    bool seen = !targetIsPlayer || pinfo.policeSeesPlayer || time - pinfo.lastSeenTime < 2.0;
    vec2 aimAt = seen ? tp : pinfo.lastSeenPos.toVec3().xy();
    vec2 to = aimAt - pos;
    float dist = length(to);
    const WeaponInfo& wi = weaponInfo(p.weapon);
    int wanted = targetIsPlayer ? pinfo.wanted : 3;
    // the suspect drove off: back to the car to continue the chase
    if (t.state == PS_INVEHICLE && t.vehicle >= 0 && dist > 30.f && pa.homeVeh >= 0 && pa.homeVeh < (int)vehicles.size() && vehicles[pa.homeVeh].used &&
        !vehicles[pa.homeVeh].exploded) {
        vec3 vp3 = vehicles[pa.homeVeh].sim.body.pos.toVec3();
        vec2 tov = vp3.xy() - pos;
        float dv = length(tov);
        if (dv > 3.f) {
            vec2 st = aiWalkRound(*this, id, vp3.xy(), 5.2f, dt) - pos;
            desired = st / Max(length(st), 1e-3f) * 5.2f;
            faceYaw = atan2f(-desired.x, desired.y);
        } else {
            Vehicle& hv = vehicles[pa.homeVeh];
            int seat = hv.seats[0] < 0 ? 0 : freeSeat(pa.homeVeh, false);
            if (seat >= 0) {
                warpPedIntoVehicle(id, pa.homeVeh, seat);
                hv.parked = false;
                hv.persistent = false;
                if (seat == 0) vehAI(pa.homeVeh).task = PT_PURSUE;
                return;
            }
        }
        float d = AI::wrapPi(faceYaw - p.yaw);
        p.yaw = AI::wrapPi(p.yaw + Clamp(d, -9.f * dt, 9.f * dt));
        movePed(p, desired, dt, false);
        return;
    }
    // line of sight (staggered)
    if (b.thinkTimer <= 0.f) {
        b.thinkTimer = 0.3f + hashToFloat(hash32(p.uid + (u32)(time * 3.0))) * 0.2f;
        b.alerted = seen && lineOfSight(p.pos + dvec3(0, 0, 1.6), t.pos + dvec3(0, 0, 1.3), id, t.vehicle);
    }
    bool los = b.alerted;
    // NPC suspects: unarmed ones are run down and cuffed rather than shot
    bool suspectArmed = t.weapon != WPN_FISTS && (weaponInfo(t.weapon).clipSize > 0 || t.brain.type == BRAIN_COMBAT);
    // an armed one who is hurt, or has three officers on them, may throw the gun down and put the hands up instead
    if (!targetIsPlayer && suspectArmed && !t.firing && t.state == PS_ONFOOT && los && dist < 30.f && !t.isPlayer && !t.persistent) {
        int onThem = 0;
        for (int i = 0; i < (int)peds.size() && onThem < 3; i++)
            onThem += isCop(peds[i]) && peds[i].state == PS_ONFOOT && length(rel(peds[i].pos, t.pos)) < 22.f;
        bool hurt = t.health < t.maxHealth * 0.55f;
        if ((hurt || onThem >= 3) && hashToFloat(hash32(t.uid * 31u + (u32)(time * 0.5))) < 0.3f) {
            Ped& tm = peds[b.target];
            PedAI& ta = pedAI(b.target);
            tm.weapon = WPN_FISTS;
            tm.aiming = tm.firing = false;
            tm.brain.type = BRAIN_WANDER;
            tm.brain.target = id;
            tm.brain.edge = -1;
            ta.activity = ACT_HANDS_UP;
            ta.actTimer = 90.f;   // (not cuffed by then: runs for it)
            aiSay(b.target, BK_HANDS_UP, 1.f, true);
            suspectArmed = false;
        }
    }
    if (!targetIsPlayer && !suspectArmed) {
        if (dist < 14.f && t.state != PS_INVEHICLE && floorf((float)time + id * 0.37f) != floorf((float)time - dt + id * 0.37f))
            aiStimulus(t.pos, STIM_ARREST, id, 30.f, false);   // (onlookers, as below)
        if (t.state == PS_INVEHICLE) {
            // stay by the car until the suspect gets out (or the unit gives up)
            faceYaw = atan2f(-to.x, to.y);
        } else {
            // (flat out while they run, closing the last metres faster than they go; a walk up to one who has stopped)
            float runAway = length(t.vel.xy());
            // out of sight round a corner: to where they were last seen first (they went that way), not into the wall
            vec2 run = to;
            float runD = dist;
            if (los || dist < 4.f) pa.chaseCrumb = tp;
            else if (pa.chaseCrumb.x < 1e8f && length(pa.chaseCrumb - pos) > 1.2f && length(pa.chaseCrumb - tp) < 60.f) {   // (not an old chase's)
                run = pa.chaseCrumb - pos;
                runD = length(run);
            }
            // (a moment to react when somebody bolts from right in front of them: a stop gone wrong)
            bool startled = b.timer < 0.6f;
            if (dist > 1.4f && !startled) desired = run / Max(runD, 1e-3f) * (dist > 6.f ? Max(5.6f, Min(runAway + 1.2f, 6.6f)) : Max(3.f, Min(runAway + 1.5f, 6.6f)));
            faceYaw = atan2f(-run.x, run.y);
            if (pa.shoutTimer <= 0.f && dist < 20.f) {
                aiSay(id, BK_COP_FREEZE, 1.f, true);
                pa.shoutTimer = 5.f;
            }
            if (dist < 1.5f && t.state == PS_ONFOOT && b.timer > 0.9f) {
                // tackle and cuff (a suspect standing there with the hands up is just cuffed); then walked to a patrol
                // car and put in the back (police_escort) - or, for one that cannot be (a story character), left down
                // there while this officer heads back to the car
                if (pedAI(b.target).activity != ACT_HANDS_UP) {
                    knockDown(b.target, vec3(to / Max(dist, 1e-3f) * 160.f, 30.f), true);
                    ai.stats.tackles++;
                }
                if (police_escort::startEscort(*this, id, b.target)) {
                    aiSay(id, BK_COP_ARREST, 1.f, true);
                    movePed(p, vec2(0.f), dt, false);
                    return;
                }
                pedAI(b.target).activity = ACT_WALK;
                Brain& tb = peds[b.target].brain;
                tb.type = BRAIN_COWER;
                tb.target = id;
                tb.timer = -120.f;
                aiSay(id, BK_COP_ARREST, 1.f, true);
                b.type = BRAIN_GOTO;
                b.target = -2;
                pa.tactic = FT_RETURN;
            }
        }
        p.animIn.stance = 0;
        p.animIn.crouch = false;
        float dyaw = AI::wrapPi(faceYaw - p.yaw);
        p.yaw = AI::wrapPi(p.yaw + Clamp(dyaw, -9.f * dt, 9.f * dt));
        movePed(p, desired, dt, false);
        return;
    }
    // ---- K9 handler: with the suspect lost, walk the scent trail behind the dog tracking ahead; the dog finds them
    //      (stands barking: the police know where they are again) or is sent after one who runs for it
    if (pa.k9Handler && targetIsPlayer && gK9.handler == id && k9Active(*this)) {
        bool dogOk = Wildlife::k9Alive(gK9.dog, gK9.dogUid);
        float runSpeed = t.state == PS_ONFOOT ? length(t.vel.xy()) : 0.f;
        vec2 dogP = dogOk ? Wildlife::k9Pos(gK9.dog).xy() : pos;
        float dogD = length(tp - dogP), realD = length(tp - pos);
        if (dogOk && gK9.sentT < 0.f && t.state == PS_ONFOOT && runSpeed > 3.f && realD < 28.f && (los || dogD < 12.f) && !ai.surrender) {
            Wildlife::k9Command(*this, gK9.dog, gK9.dogUid, Wildlife::K9_ATTACK, t.pos.toVec3(), b.target);
            gK9.sentT = 0.f;
            aiSay(id, BK_COP_FREEZE, 1.f, true);
            LOG("police: K9 released on the player (%.1f m, running %.1f m/s)", realD, runSpeed);
        }
        if (gK9.sentT >= 0.f) {
            gK9.sentT += dt;
            bool down = t.state == PS_RAGDOLL || t.state == PS_GETUP;
            if (gK9.sentT > 20.f || (down && gK9.sentT > 5.f) || !dogOk || ai.surrender) {
                if (dogOk) Wildlife::k9Command(*this, gK9.dog, gK9.dogUid, Wildlife::K9_HEEL, vec3(0.f), -1);
                gK9.sentT = -1.f;
            }
        } else if (dogOk && !seen) {
            vec2 stand(0.f);
            bool moved = false;
            if (dogD < 10.f || realD < 9.f) {
                // found them: the dog barks, the handler covers them - and everyone knows where they are
                gK9.foundT += dt;
                Wildlife::k9Command(*this, gK9.dog, gK9.dogUid, Wildlife::K9_ALERT, t.pos.toVec3(), -1);
                pinfo.lastSeenPos = t.pos;
                pinfo.lastSeenTime = (float)time;
                pinfo.wantedCooldown = 0.f;
                if (pa.shoutTimer <= 0.f) {
                    aiSay(id, BK_COP_FREEZE, 1.f, true);
                    pa.shoutTimer = 4.f;
                }
                faceYaw = atan2f(-(tp - pos).x, (tp - pos).y);
                p.aiming = wi.clipSize > 0;
                p.aimDir = normalize(pedChestPos(t) - pedHeadPos(p));
            } else {
                int n = (int)gTrail.size();
                gK9.next = Clamp(gK9.next, 0, Max(n - 1, 0));
                while (gK9.next + 1 < n && length(gTrail[gK9.next].p - pos) < 2.2f) gK9.next++;
                vec2 goal = n > 0 ? gTrail[gK9.next].p : pos;
                vec2 tt = goal - pos;
                float dd = length(tt);
                if (gK9.next >= n - 1 && dd < 2.5f && gTrailEnds) {
                    // the trail stops at the kerb (they drove off): the dog casts about, barks at the spot, gives up
                    gK9.lostT += dt;
                    Wildlife::k9Command(*this, gK9.dog, gK9.dogUid, gK9.lostT < 5.f ? Wildlife::K9_ALERT : Wildlife::K9_HEEL, vec3(goal, gTrail[n - 1].z), -1);
                } else {
                    int ahead = Min(gK9.next + 2, n - 1);
                    Wildlife::k9Command(*this, gK9.dog, gK9.dogUid, Wildlife::K9_TRACK, vec3(gTrail[ahead].p, gTrail[ahead].z), -1);
                    if (dd > 0.5f) {
                        stand = tt / dd * 2.7f;   // a jog behind the dog
                        moved = true;
                    }
                    faceYaw = atan2f(-tt.x, tt.y);
                    if (pa.shoutTimer <= 0.f) {
                        aiSay(id, BK_COP_SEARCH, 0.4f);
                        pa.shoutTimer = 15.f;
                    }
                }
            }
            p.animIn.stance = 0;
            p.animIn.crouch = false;
            float dy2 = AI::wrapPi(faceYaw - p.yaw);
            p.yaw = AI::wrapPi(p.yaw + Clamp(dy2, -8.f * dt, 8.f * dt));
            movePed(p, moved ? stand : vec2(0.f), dt, false);
            return;
        } else if (dogOk) {
            // they are in view: the dog barks at them from the handler's side (close: as good as found), or heels when
            // they are further off
            gK9.foundT = realD < 16.f ? gK9.foundT + dt : 0.f;
            Wildlife::k9Command(*this, gK9.dog, gK9.dogUid, realD < 16.f ? Wildlife::K9_ALERT : Wildlife::K9_HEEL, t.pos.toVec3(), -1);
        }
    }
    bool arrest = b.type == BRAIN_ARREST && wanted <= 1 && targetIsPlayer && !t.firing && !(t.aiming && t.weapon != WPN_FISTS);
    if (b.type == BRAIN_ARREST && !arrest) b.type = BRAIN_COMBAT;
    // hands up where an officer can see it (or close by): hold fire, close in with the gun on them, cuff them
    if (targetIsPlayer && ai.surrender && (los || dist < 25.f)) {
        arrest = true;
        pa.tactic = FT_ARREST;
        if (pa.searchSpot >= 0 && pa.searchSpot < (int)gS.spots.size() && gS.spots[pa.searchSpot].by == id) gS.spots[pa.searchSpot].by = -1;
        pa.searchSpot = -1;
    }
    // an arrest going down draws onlookers (pedai.cpp STIM_ARREST): about once a second while this officer is close in
    // on a suspect being taken in (hands up, gun on them, the cuffs)
    if (arrest && seen && dist < 14.f && t.state != PS_INVEHICLE && floorf((float)time + id * 0.37f) != floorf((float)time - dt + id * 0.37f))
        aiStimulus(t.pos, STIM_ARREST, id, 30.f, targetIsPlayer);
    // tactics
    if (pa.tacticTimer <= 0.f) {
        pa.tacticTimer = 2.5f + hashToFloat(hash32(p.uid + (u32)(time * 2.0))) * 2.5f;
        u8 was = pa.tactic;
        if (arrest) pa.tactic = FT_ARREST;
        else if (!seen) pa.tactic = FT_SEARCH;
        else if (pa.coverVeh >= 0 && pa.coverVeh < (int)vehicles.size() && vehicles[pa.coverVeh].used && dist < 45.f && pa.tactic != FT_FLANK &&
                 (p.uid & 1))
            pa.tactic = FT_COVER;
        else if (pa.tactic == FT_COVER && dist > 50.f) pa.tactic = FT_APPROACH;
        else if (pa.tactic != FT_COVER && pa.tactic != FT_FLANK) pa.tactic = (p.uid % 3 == 0) ? FT_FLANK : FT_ENGAGE;
        // (calling it out to the others: into cover, round the side)
        if (pa.tactic != was && (pa.tactic == FT_COVER || pa.tactic == FT_FLANK) && seen && pa.shoutTimer <= 0.f) {
            aiSay(id, BK_COP_COVER, 0.7f, true);
            pa.shoutTimer = 3.f;
        }
        // search point: a corner or doorway from the search plan (a fresh plan when the suspect was last seen elsewhere);
        // with nothing left to check there, a random point in the search area
        if (pa.tactic == FT_SEARCH) {
            vec2 last = pinfo.lastSeenPos.toVec3().xy();
            if (!targetIsPlayer) last = tp;
            if (length(gS.center - last) > 20.f || time - gS.made > 150.0) {
                buildSearchPlan(*this, last, wanted);
                for (int i = 0; i < (int)peds.size() && i < (int)ai.ped.size(); i++) ai.ped[i].searchSpot = -1;
            }
            bool have = pa.searchSpot >= 0 && pa.searchSpot < (int)gS.spots.size() && gS.spots[pa.searchSpot].by == id && !gS.spots[pa.searchSpot].checked;
            if (!have) {
                pa.searchSpot = claimSearchSpot(*this, id);
                pa.searchT = 0.f;
                pa.searchLook = -1.f;
            }
            if (pa.searchSpot >= 0) {
                pa.tacticPos = gS.spots[pa.searchSpot].p;
            } else {
                float R = searchRadiusFor(Max(wanted, 1)) * 0.5f;
                u32 h = hash32(p.uid * 131u + (u32)(time * 0.3));
                float ang = hashToFloat(h) * kTwoPi;
                pa.tacticPos = last + vec2(cosf(ang), sinf(ang)) * (sqrtf(hashToFloat(hash32(h))) * R);
            }
        } else if (pa.searchSpot >= 0) {
            if (pa.searchSpot < (int)gS.spots.size() && gS.spots[pa.searchSpot].by == id) gS.spots[pa.searchSpot].by = -1;   // (found them: let it go)
            pa.searchSpot = -1;
        }
    }
    float prefer = wi.clipSize > 0 ? Clamp(wi.range * 0.3f, 8.f, 22.f) : 1.2f;
    // the dog handler closes in on a suspect on foot (the dog barking at them from a few metres: as good as found)
    if (pa.k9Handler && gK9.handler == id && t.state == PS_ONFOOT && t.weapon == WPN_FISTS) prefer = Min(prefer, 9.f);
    switch (pa.tactic) {
        case FT_ARREST: {
            // (lost them: a word with somebody about now and then, as in the search - police_tip::asking)
            if (targetIsPlayer && !seen && police_tip::asking(*this, id, dt, desired, faceYaw)) break;
            // gun drawn, close in, shout; a suspect who runs for it is chased down on foot and tackled
            pa.tackleTimer -= dt;
            bool running = t.state == PS_ONFOOT && length(t.vel.xy()) > 3.f && seen;
            faceYaw = atan2f(-to.x, to.y);
            if (running) {
                desired = to / Max(dist, 1e-3f) * (dist > 3.f ? 6.6f : 5.2f);   // holstered, flat-out sprint
                if (pa.shoutTimer <= 0.f && dist < 30.f) {
                    aiSay(id, BK_COP_FREEZE, 1.f, true);
                    pa.shoutTimer = 4.f;
                }
                if (dist < 1.7f && pa.tackleTimer <= 0.f) {
                    bool success = hashToFloat(hash32(p.uid * 977u + (u32)(time * 5.0))) < 0.65f;
                    if (success) {
                        knockDown(b.target, vec3(to / Max(dist, 1e-3f) * 150.f, 25.f), true);
                        ai.stats.tackles++;
                        aiSay(id, BK_COP_GROUND, 1.f, true);
                        pa.tackleTimer = 10.f;
                        pa.shoutTimer = 3.f;
                    } else {
                        p.pendingAction = Anim::CLIP_STAGGER;   // grabbed at air
                        pa.tackleTimer = 3.5f;
                    }
                }
                break;
            }
            float stopAt = 1.9f;
            if (targetIsPlayer && ai.surrender) {
                // one officer goes in for the cuffs, the rest cover from a few metres off
                bool nearest = true;
                for (int i = 0; i < (int)peds.size() && nearest; i++)
                    if (i != id && isCop(peds[i]) && peds[i].state == PS_ONFOOT && length(rel(peds[i].pos, t.pos)) < dist - 0.3f) nearest = false;
                if (!nearest) stopAt = 4.5f + (float)(p.uid % 3u);
            }
            if (dist > stopAt) desired = to / dist * (dist > 10.f ? 4.5f : 1.6f);
            p.aiming = wi.clipSize > 0 && dist < 25.f;
            if (p.aiming) p.aimDir = normalize(pedChestPos(t) - pedHeadPos(p));
            if (pa.shoutTimer <= 0.f && dist < 25.f) {
                aiSay(id, dist < 8.f ? BK_COP_GROUND : BK_COP_FREEZE, 1.f, true);
                pa.shoutTimer = 5.f;
            }
            break;
        }
        case FT_SEARCH: {
            // a word with somebody about first, now and then (police_tip::asking: stood by them for the question and the answer)
            if (targetIsPlayer && police_tip::asking(*this, id, dt, desired, faceYaw)) break;
            vec2 tt = pa.tacticPos - pos;
            float d = length(tt);
            bool spot = pa.searchSpot >= 0 && pa.searchSpot < (int)gS.spots.size() && gS.spots[pa.searchSpot].by == id;
            pa.searchT += dt;
            if (spot && d > 1.2f && pa.searchT > 40.f) {
                gS.spots[pa.searchSpot].checked = true;   // (could not get there: walled off, fenced)
                gS.spots[pa.searchSpot].by = -1;
                pa.searchSpot = claimSearchSpot(*this, id);
                pa.searchT = 0.f;
                pa.searchLook = -1.f;
                if (pa.searchSpot >= 0) pa.tacticPos = gS.spots[pa.searchSpot].p;
                spot = pa.searchSpot >= 0;
                tt = pa.tacticPos - pos;
                d = length(tt);
            }
            if (d > 1.2f) {
                // on the way (a brisk walk, a jog when it is further off)
                desired = tt / d * (d > 25.f ? 3.6f : 2.4f);
                faceYaw = atan2f(-tt.x, tt.y);
            } else if (spot) {
                // there: gun up, a slow sweep along the facade / into the doorway (the torch follows at night), then on
                SearchSpot& S = gS.spots[pa.searchSpot];
                float look = atan2f(-S.look.x, S.look.y) + sinf((float)time * 1.1f + p.uid * 0.7f) * 0.8f;
                faceYaw = look;
                p.aiming = wi.clipSize > 0;
                p.aimDir = normalize(vec3(-sinf(look), cosf(look), -0.12f));
                if (pa.searchLook < 0.f) pa.searchLook = 3.5f + hashToFloat(hash32(p.uid + (u32)time)) * 3.f;
                pa.searchLook -= dt;
                if (pa.searchLook <= 0.f) {
                    S.checked = true;
                    S.by = -1;
                    pa.searchSpot = claimSearchSpot(*this, id);
                    pa.searchT = 0.f;
                    pa.searchLook = -1.f;
                    if (pa.searchSpot >= 0) pa.tacticPos = gS.spots[pa.searchSpot].p;
                    if (pa.shoutTimer <= 0.f && hashToFloat(hash32(p.uid * 7u + (u32)time)) < 0.35f) {
                        aiSay(id, BK_COP_SEARCH, 0.35f);   // ("clear!")
                        pa.shoutTimer = 12.f;
                    }
                }
            } else {
                faceYaw = p.yaw + sinf((float)time * 0.8f + p.uid) * 0.03f;
            }
            if (pa.shoutTimer <= 0.f) {
                aiSay(id, BK_COP_SEARCH, 0.3f);
                pa.shoutTimer = 18.f;
            }
            break;
        }
        case FT_COVER: {
            // crouch behind the cover car on the side away from the suspect, pop up to shoot
            int cv = pa.coverVeh;
            if (cv < 0 || cv >= (int)vehicles.size() || !vehicles[cv].used) {
                pa.tactic = FT_ENGAGE;
                break;
            }
            const Vehicle& car = vehicles[cv];
            vec2 cp = car.sim.body.pos.toVec3().xy();
            vec2 away = normalize(cp - tp + vec2(1e-3f, 0.f));
            float r = length(vassets[car.model].spec.boxHalf.xy()) + 0.9f;
            vec2 spot = cp + away * r + AI::rightOf(away) * ((p.uid & 2) ? 1.2f : -1.2f);
            vec2 tt = spot - pos;
            float d = length(tt);
            if (d > 0.6f) {
                desired = tt / d * (d > 4.f ? 4.8f : 2.f);
                faceYaw = atan2f(-desired.x, desired.y);
            } else {
                faceYaw = atan2f(-to.x, to.y);
                bool popUp = fmodf((float)time * 0.5f + p.uid * 0.21f, 2.f) < 1.1f;
                p.animIn.crouch = !popUp;
                p.aiming = popUp && los;
            }
            break;
        }
        case FT_FLANK: {
            // circle around the suspect at mid range
            vec2 side = AI::rightOf(to / Max(dist, 1e-3f)) * ((p.uid & 1) ? 1.f : -1.f);
            vec2 goal = aimAt - to / Max(dist, 1e-3f) * prefer + side * prefer * 0.8f;
            vec2 tt = goal - pos;
            float d = length(tt);
            if (d > 1.f) desired = tt / d * 4.2f;
            faceYaw = atan2f(-to.x, to.y);
            p.aiming = los && dist < wi.range;
            if (d < 2.f) pa.tactic = FT_ENGAGE;
            break;
        }
        case FT_APPROACH:
        case FT_ENGAGE:
        default: {
            if (dist > prefer * 1.2f || !los) desired = to / Max(dist, 1e-3f) * (dist > 20.f ? 5.2f : 3.2f);
            else if (dist < prefer * 0.45f && wi.clipSize > 0) desired = -to / Max(dist, 1e-3f) * 2.2f;
            else {
                vec2 side = AI::rightOf(to / Max(dist, 1e-3f)) * ((((u32)(time * 0.4) + p.uid) & 1) ? 1.6f : -1.6f);
                desired = side;
            }
            faceYaw = atan2f(-to.x, to.y);
            p.aiming = wi.clipSize > 0 && dist < wi.range && los;
            break;
        }
    }
    if (pa.tactic != FT_COVER) p.animIn.crouch = false;
    // warnings before shooting at low wanted levels
    if (wanted <= 1 && pa.shoutTimer <= 0.f && dist < 30.f && los) {
        aiSay(id, BK_COP_FREEZE, 0.8f, true);
        pa.shoutTimer = 6.f;
    }
    // fire
    bool mayShoot = !arrest && (targetIsPlayer ? (wanted >= 2 || (t.aiming && t.weapon != WPN_FISTS) || t.firing) : suspectArmed);
    if (p.aiming && mayShoot && wi.clipSize > 0 && p.fireTimer <= 0.f && p.reloadTimer <= 0.f) {
        float burst = fmodf((float)time * (0.55f + b.aggression * 0.5f) + p.uid * 0.37f, 2.f);
        if (burst < 1.0f) {
            quat q = quatAxisAngle(vec3(0, 0, 1), p.yaw);
            vec3 hand = p.pos.toVec3() + rotate(q, p.bones[Anim::B_HAND_R].c[3].xyz());
            vec3 target3 = pedChestPos(t) + vec3(0, 0, (hash32(p.uid + (u32)(time * 7)) % 100) * 0.004f - 0.2f);
            vec3 dir = normalize(target3 - hand);
            p.aimDir = dir;
            fireWeapon(id, dvec3(hand + dir * 0.3f), dir);
        }
    }
    if (p.clip[p.weapon] <= 0 && p.ammo[p.weapon] <= 0 && wi.clipSize > 0) p.ammo[p.weapon] = wi.clipSize * 4;
    if (!p.aiming && wi.clipSize == 0 && dist < 1.4f && p.meleeTimer <= 0.f && !arrest) {
        vec3 f(-sinf(p.yaw), cosf(p.yaw), 0);
        fireWeapon(id, p.pos + dvec3(0, 0, 1.2), f);
        p.meleeTimer = wi.fireInterval + 0.3f;
    }
    // officer down nearby
    if (pa.shoutTimer <= 0.f) {
        for (int i = 0; i < (int)peds.size(); i++) {
            const Ped& q = peds[i];
            if (q.used && q.faction == FAC_POLICE && q.state == PS_DEAD && q.stateTime < 3.f && length(rel(q.pos, p.pos)) < 30.f) {
                aiSay(id, BK_COP_DOWN, 1.f, true);
                pa.shoutTimer = 8.f;
                break;
            }
        }
    }
    bool meleeEngaged = p.meleeTarget >= 0 && p.meleeTarget < (int)peds.size() && peds[p.meleeTarget].used && peds[p.meleeTarget].health > 0.f &&
                        length(rel(peds[p.meleeTarget].pos, p.pos)) < 4.f && (p.animIn.stance == 19 || p.animIn.stance == 20);
    if (!meleeEngaged) p.animIn.stance = 0;   // (melee.cpp owns the fighting guard while squared up)
    float dy = AI::wrapPi(faceYaw - p.yaw);
    p.yaw = AI::wrapPi(p.yaw + Clamp(dy, -9.f * dt, 9.f * dt));
    movePed(p, desired, dt, false);
    (void)pl;
    (void)stance;
}

}  // namespace Game
