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
        if (drv < 0 || g.peds[drv].isPlayer || g.peds[drv].brain.type != BRAIN_DRIVER || g.ai.veh[vi].task != PT_NONE || prisonerSeat(g, vi) < 0) continue;
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
    bool ready = carOk && (g.ai.veh[car].task != PT_TRANSPORT || g.ai.veh[car].transportState == 2);
    const Ped& sp = g.peds[s];
    vec2 pos = p.pos.toVec3().xy(), spos = sp.pos.toVec3().xy();
    if (floorf((float)g.time + id * 0.37f) != floorf((float)g.time - dt + id * 0.37f))
        g.aiStimulus(sp.pos, STIM_ARREST, id, 28.f, false);   // (people stop to watch: pedai.cpp)
    vec2 desired(0.f);
    float faceYaw = p.yaw;
    if (sp.state == PS_RAGDOLL || sp.state == PS_GETUP || !ready || g.ai.ped[s].clipTimer > 0.f) {
        // down after the tackle, waiting for the car, getting in: standing over them
        vec2 to = spos - pos;
        float d = length(to);
        if (d > 1.5f) desired = to / d * Min(2.6f, d * 1.5f);
        faceYaw = atan2f(-to.x, to.y);
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
        vec2 to = goal - pos;
        float d = length(to);
        float sv = length(sp.vel.xy());
        if (d > 0.06f) desired = to / d * Min(3.f, sv + d * 2.2f);
        faceYaw = d > 1.2f ? atan2f(-to.x, to.y) : sp.yaw;
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
                standDown(i);
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
            standDown(i);   // back to patrol
        }
    }
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
                if (prisoner) continue;
                float d = length(v.sim.body.pos.toVec3().xy() - ip);
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
            // there (or as near as it gets: held up within a short walk of them for longer than a red light lasts)
            va.stopTimer = v.sim.speed() < 1.f ? va.stopTimer + dt : 0.f;
            if (dist < 30.f || (dist < 85.f && va.stopTimer > 30.f)) {
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
        // every third unit drives to where the suspect is heading (cut-off / pincer), the others follow the trail
        vec2 goal = tp.xy();
        if (targetVeh >= 0 && (v.uid % 3) == 0 && length(tv.xy()) > 8.f) goal += tv.xy() * Clamp(dist / 28.f, 1.5f, 7.f);
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
        if (va.heldUp > 3.f && mySpeed < 1.f && targetVeh < 0 && tpReal && dist < 75.f) {
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
    // officers get out near a suspect on foot, or when the target vehicle is stopped
    if ((onFoot && dist < 26.f && fs < 3.f) || (!onFoot && tSpeed < 1.f && dist < 16.f && fs < 2.f)) {
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
        police_escort::escortStep(*this, id, dt);
        return;
    }
    // returning to the car / patrol
    if (b.type == BRAIN_GOTO && b.target == -2) {
        int hv = pa.homeVeh;
        if (hv >= 0 && hv < (int)vehicles.size() && vehicles[hv].used && !vehicles[hv].exploded) {
            vec3 vp3 = vehicles[hv].sim.body.pos.toVec3();
            vec2 tov = vp3.xy() - pos;
            float dv = length(tov);
            if (dv > 3.f) {
                desired = tov / dv * 2.6f;
                faceYaw = atan2f(-desired.x, desired.y);
            } else if (time < vehAI(hv).escortHold) {
                // a colleague is bringing a prisoner to it: wait by the car
                faceYaw = atan2f(-tov.x, tov.y) + kPi * 0.5f;
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
            desired = tov / dv * 5.2f;
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
            if (dist > 1.4f) desired = to / Max(dist, 1e-3f) * (dist > 6.f ? Max(5.6f, Min(runAway + 1.2f, 6.6f)) : Max(3.f, Min(runAway + 1.5f, 6.6f)));
            faceYaw = atan2f(-to.x, to.y);
            if (pa.shoutTimer <= 0.f && dist < 20.f) {
                aiSay(id, BK_COP_FREEZE, 1.f, true);
                pa.shoutTimer = 5.f;
            }
            if (dist < 1.5f && t.state == PS_ONFOOT) {
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
        if (arrest) pa.tactic = FT_ARREST;
        else if (!seen) pa.tactic = FT_SEARCH;
        else if (pa.coverVeh >= 0 && pa.coverVeh < (int)vehicles.size() && vehicles[pa.coverVeh].used && dist < 45.f && pa.tactic != FT_FLANK &&
                 (p.uid & 1))
            pa.tactic = FT_COVER;
        else if (pa.tactic == FT_COVER && dist > 50.f) pa.tactic = FT_APPROACH;
        else if (pa.tactic != FT_COVER && pa.tactic != FT_FLANK) pa.tactic = (p.uid % 3 == 0) ? FT_FLANK : FT_ENGAGE;
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
