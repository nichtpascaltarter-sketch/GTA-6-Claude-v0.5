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
        // busy city sidewalks and beach crowds, streets lined with parked cars (tuned against the GTA 6 trailers)
        case World::REG_DOWNTOWN: return {64, 36, 30};
        case World::REG_FINANCIAL: return {56, 34, 22};
        case World::REG_MIDTOWN: return {48, 30, 28};
        case World::REG_NORTH_CITY: return {34, 26, 26};
        case World::REG_CALLE_LUNA: return {54, 24, 32};
        case World::REG_BEACH: return {72, 24, 26};
        case World::REG_BAY_ISLAND: return {10, 8, 12};
        case World::REG_KEY_CORAL: return {22, 12, 16};
        case World::REG_PORT: return {16, 20, 10};
        case World::REG_GROVE: return {24, 16, 22};
        case World::REG_AIRPORT: return {14, 20, 16};
        case World::REG_FLATS: return {24, 20, 22};
        case World::REG_SUBURBS: return {14, 16, 24};
        case World::REG_REDLAND: return {7, 9, 8};
        case World::REG_SAWGRASS: return {1, 5, 1};
        case World::REG_GULF_TOWN: return {12, 7, 8};
        case World::REG_FARMLAND: return {2, 6, 3};
        case World::REG_LAKE_TOWN: return {18, 12, 14};
        case World::REG_HARLOW: return {12, 9, 10};
        case World::REG_RIDGE: return {2, 6, 2};
        case World::REG_FORT_CASTELL: return {18, 14, 16};
        case World::REG_KEYS: return {5, 7, 5};
        case World::REG_KEY_TOWN: return {24, 12, 14};
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
    bool offices = r == World::REG_DOWNTOWN || r == World::REG_FINANCIAL || r == World::REG_MIDTOWN;
    if (offices && ((tod > 7.5f && tod < 9.5f) || (tod > 12.f && tod < 14.f) || (tod > 17.f && tod < 19.f))) f *= 1.15f;   // commute, lunch
    if ((r == World::REG_BEACH || r == World::REG_KEY_CORAL) && tod > 11.f && tod < 17.5f) f *= 1.15f;                   // beach afternoon
    if (r == World::REG_PORT && (tod < 6.f || tod > 19.f)) f *= 0.4f;      // shifts end
    return f;
}

float trafficTimeFactor(float tod) {
    if (tod < 5.f) return 0.35f;
    if (tod < 7.f) return 0.35f + (tod - 5.f) * 0.33f;
    if (tod < 9.5f) return 1.25f;              // morning rush hour
    if (tod < 16.5f) return 0.95f;
    if (tod < 19.f) return 1.25f;              // evening rush hour
    if (tod < 22.f) return 0.85f;
    return 0.6f;
}

enum PedSpawnKind : u8 {
    PK_WALKER = 0, PK_GROUP, PK_CHAT, PK_SPOT, PK_HAIL, PK_WALL, PK_JOGGER, PK_SUNBATHER, PK_GANG, PK_WORKER, PK_NIGHTLIFE,
    PK_BUSINESS, PK_QUEUE, PK_BEAT, PK_BATHER, PK_COUNT
};

struct PopState {
    float pedTimer = 0.f, carTimer = 0.f, parkTimer = 0.f, incidentTimer = 0.f, departTimer = 8.f;
    u32 counter = 1;
};
PopState gPop;

constexpr int kMaxPeds = 90;
constexpr int kMaxTraffic = 44;
constexpr int kMaxParked = 36;

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
    // jaywalkers who made it across rejoin the sidewalk graph
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        Ped& p = g.peds[i];
        if (!p.used || p.brain.type != BRAIN_GOTO || g.ai.ped[i].uid != p.uid || g.ai.ped[i].activity != ACT_CROSS) continue;
        if (length(p.pos.toVec3().xy() - p.brain.goal.toVec3().xy()) < 0.9f || p.brain.timer > 20.f) {
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
            g.ai.ped[i].activity = ACT_WALK;
            g.ai.ped[i].navOk = false;
            g.ai.ped[i].actTimer = 15.f;
        }
    }
    // walkers who reached the door disappear inside (travelers into the terminal too)
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

// ---- venues: places with a working crowd of their own - the Port Isle gate, the airport forecourt, the Sawgrass
// causeway and its airboat landing. Scenario slots laid out in the frame of the place (a site element or a road
// junction), each with its people, what they do there and their hours. A venue fills when the player comes near (out
// of sight, or at once during a warmup fade) and empties again once they are far away. Nobody stands on a live lane:
// every slot is checked against the lane graph, buildings and water when the venue is laid out.
enum VenueLook : u8 { VL_WORKER = 0, VL_CIVIL, VL_BUSINESS, VL_BEACH, VL_TRAVELER };
enum VenueProp : u8 { VP_NONE = 0, VP_TRUCK, VP_TAXI, VP_AIRBOAT };

struct VenueSlot {
    vec2 pos;             // where they stand (or start)
    float yaw = 0.f;      // facing there
    vec2 pos2;            // pacing: the other spot / travelers: the terminal door / farewells: where they go after
    float yaw2 = 0.f;
    u8 mode = VM_STAND, look = VL_CIVIL, prop = VP_NONE;
    float h0 = 0.f, h1 = 24.f;   // hours present (h0 > h1 wraps midnight)
    float chance = 1.f;          // filled on a given visit
    float every = 0.f;           // > 0: a stream (travelers) - a new one about this often (s) while the venue is near
    vec2 propPos;                // the vehicle that goes with the slot, parked, nobody in it
    float propYaw = 0.f;
    vec2 propOff;                // ... and the ped beside it: x to the vehicle's right, y forward (from its box)
    bool follows = false;        // only with the slot before it filled (the other half of a pair)
    // runtime
    int ped = -1;
    u32 pedUid = 0;
    int veh = -1;
    u32 vehUid = 0;
    float cooldown = 0.f;
};

struct Venue {
    const char* name = "";
    vec2 c;
    float fillR = 200.f, releaseR = 310.f;
    std::vector<VenueSlot> slots;
    bool active = false;
    u32 visits = 0;
};

struct VenueSet {
    bool built = false;
    std::vector<Venue> v;
};
VenueSet gVenues;

bool venueHours(float tod, float h0, float h1) { return h0 <= h1 ? (tod >= h0 && tod < h1) : (tod >= h0 || tod < h1); }

// a place a person can stand: on the ground (not in water, not inside a building), clear of every traffic lane
bool venueSpotOk(const GameWorld& g, vec2 p, bool curb = false) {
    if (g.map->isWater(p.x, p.y)) {
        float z = 0.f;
        if (!g.roads->surfaceHeight(p, &z, 1e9f) && !(World::gSites && World::gSites->padHeight(p, &z, 1e9f))) return false;   // decks, piers
    }
    if (World::gBuildings && World::gBuildings->pointInBuilding(p, 0.4f)) return false;
    float u = 0.f, lat = 0.f;
    int ln = g.laneGraph.nearestLane(p, vec2(0.f), 10.f, &u, &lat);
    if (ln >= 0) {
        const AI::Lane& L = g.laneGraph.lanes[ln];
        bool within = u > L.u0 - 1.f && u < L.u1 + 1.f;
        if (within && fabsf(lat) < L.width * 0.5f + (curb ? 0.35f : 0.9f)) return false;
    }
    return true;
}

VenueSlot mkSlot(vec2 pos, vec2 face, u8 mode, u8 look, float h0, float h1, float chance) {
    VenueSlot s;
    s.pos = s.pos2 = pos;
    s.yaw = s.yaw2 = AI::dirYaw(length2(face) > 1e-6f ? normalize(face) : vec2(0.f, 1.f));
    s.mode = mode;
    s.look = look;
    s.h0 = h0;
    s.h1 = h1;
    s.chance = chance;
    return s;
}

void buildVenues(GameWorld& g) {
    gVenues.built = true;
    gVenues.v.clear();
    if (!World::gSites) return;
    const World::SiteSet& S = *World::gSites;
    auto findElem = [&](u16 kind, vec2 around, float maxD) -> const World::SiteElem* {
        const World::SiteElem* best = nullptr;
        float bd = maxD;
        for (const World::SiteElem& e : S.elems)
            if (e.kind == kind && length(e.c - around) < bd) {
                bd = length(e.c - around);
                best = &e;
            }
        return best;
    };
    int kept = 0, dropped = 0;
    auto finish = [&](Venue& V) {
        std::vector<VenueSlot> ok;
        for (VenueSlot& s : V.slots) {
            bool curb = s.mode == VM_TRAVEL_IN || s.mode == VM_TRAVEL_OUT || s.mode == VM_FAREWELL;
            bool good = s.prop != VP_NONE || (venueSpotOk(g, s.pos, curb) && venueSpotOk(g, s.pos2, curb));
            if (good && (s.prop == VP_TRUCK || s.prop == VP_TAXI) && !venueSpotOk(g, s.propPos)) good = false;
            if (good && s.prop == VP_AIRBOAT && (!g.map->isWater(s.propPos.x, s.propPos.y) || !venueSpotOk(g, s.pos))) good = false;
            if (good) ok.push_back(s);
            (good ? kept : dropped)++;
        }
        V.slots.swap(ok);
        if (!V.slots.empty()) gVenues.v.push_back(V);
    };
    // ---------------------------------------------------------------- Port Isle gate (frame: x along the gate road into
    // the port, y to its left): guards at the booths, a smoke break behind the south booth, truckers at rigs parked
    // south of the gate, dock workers between the gate and the stacks north of the road, a checker at the OCR portal
    if (const World::SiteElem* gate = findElem(World::SK_PORT_GATE, vec2(4008.f, -160.f), 400.f)) {
        Venue V;
        V.name = "port gate";
        vec2 o = gate->c, ax = gate->ax, ay = perp(ax);
        auto F = [&](float x, float y) { return o + ax * x + ay * y; };
        auto D = [&](float x, float y) { return ax * x + ay * y; };
        float by = gate->hy - 3.f;   // booth line
        V.c = F(10.f, 0.f);
        V.slots.push_back(mkSlot(F(2.9f, by - 1.6f), D(0.f, -1.f), VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(F(-2.9f, -by + 1.6f), D(0.f, 1.f), VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(F(-3.2f, by + 1.9f), D(1.f, 0.f), VM_PHONE, VL_WORKER, 6.f, 22.f, 0.6f));
        for (int k = 0; k < 3; k++) {
            float a = 0.6f + k * kTwoPi / 3.f;
            vec2 off(cosf(a) * 0.85f, sinf(a) * 0.85f);
            V.slots.push_back(mkSlot(F(4.5f + off.x, -by - 3.4f + off.y), -D(off.x, off.y), k == 1 ? VM_TALK : VM_SMOKE, VL_WORKER, 7.f, 18.f, 0.85f));
        }
        for (int k = 0; k < 2; k++) {
            VenueSlot s = mkSlot(F(6.f + k * 20.f, -30.f), D(0.f, 1.f), k == 0 ? VM_LEAN : VM_PHONE, VL_CIVIL, 5.f, 21.f, 0.85f);
            s.prop = VP_TRUCK;
            s.propPos = s.pos;
            s.propYaw = AI::dirYaw(ax);
            s.propOff = vec2(-1.f, 0.3f);   // (by the driver's door on the left, towards the front: scaled by the box)
            V.slots.push_back(s);
        }
        const float paces[3][4] = {{12.f, 20.f, 63.f, 24.f}, {16.f, 30.f, 62.f, 15.f}, {-10.f, -21.f, -30.f, -34.f}};
        for (int k = 0; k < 3; k++) {
            VenueSlot s = mkSlot(F(paces[k][0], paces[k][1]), D(paces[k][2] - paces[k][0], paces[k][3] - paces[k][1]), VM_PACE, VL_WORKER, 6.f, 18.5f, 0.9f);
            s.pos2 = F(paces[k][2], paces[k][3]);
            s.yaw2 = AI::dirYaw(normalize(s.pos - s.pos2));
            V.slots.push_back(s);
        }
        V.slots.push_back(mkSlot(F(-gate->hx - 12.f, by + 1.f), D(0.f, -1.f), VM_STAND, VL_WORKER, 6.f, 20.f, 0.7f));
        finish(V);
    }
    // ---------------------------------------------------------------- airport forecourt: travelers between the curb and
    // the doors (both ways), farewells at the curb, security by the doors, taxi drivers at the rank on the lot's edge
    if (const World::SiteElem* term = findElem(World::SK_TERMINAL, vec2(627.5f, 1440.f), 600.f)) {
        Venue V;
        V.name = "airport forecourt";
        float fx = term->c.x + term->hy;   // landside facade (the element's axis runs along y)
        float curbX = fx + 18.5f;          // curb edge of the raised walk under the canopy
        float y0 = term->c.y - term->hx + 30.f, y1 = Min(term->c.y + term->hx - 30.f, 1480.f);
        V.c = vec2(curbX + 20.f, (y0 + y1) * 0.5f);
        V.fillR = 260.f;
        V.releaseR = 360.f;
        const vec2 east(1.f, 0.f), west(-1.f, 0.f), north(0.f, 1.f), south(0.f, -1.f);
        for (int k = 0; k < 7; k++) {
            float y = Lerp(y0, y1, (k + 0.5f) / 7.f);
            float door = y + ((k & 1) ? 6.f : -6.f);
            VenueSlot in = mkSlot(vec2(curbX - 0.8f, y + 3.f), west, VM_TRAVEL_IN, VL_TRAVELER, 5.f, 23.5f, 1.f);
            in.pos2 = vec2(fx + 0.8f, door);
            in.every = 16.f + k * 3.f;
            V.slots.push_back(in);
            if (k % 2 == 0) {
                VenueSlot out = mkSlot(vec2(curbX - 1.2f, y - 4.f), east, VM_TRAVEL_OUT, VL_TRAVELER, 5.f, 23.5f, 0.9f);
                out.pos2 = vec2(fx + 0.8f, door + 3.f);
                out.every = 24.f + k * 4.f;
                V.slots.push_back(out);
            }
        }
        for (int k = 0; k < 2; k++) {
            float y = Lerp(y0, y1, 0.3f + k * 0.35f);
            VenueSlot a = mkSlot(vec2(curbX - 2.4f, y), north, VM_FAREWELL, VL_TRAVELER, 6.f, 22.f, 0.8f);
            a.pos2 = vec2(fx + 0.8f, y + 2.f);        // the traveler goes in through the doors
            VenueSlot b = mkSlot(vec2(curbX - 2.4f, y + 0.75f), south, VM_SEEOFF, VL_CIVIL, 6.f, 22.f, 1.f);
            b.follows = true;                          // (the one who came to see them off)
            V.slots.push_back(a);
            V.slots.push_back(b);
        }
        V.slots.push_back(mkSlot(vec2(fx + 6.f, (y0 + y1) * 0.5f), east, VM_GUARD, VL_WORKER, 0.f, 24.f, 1.f));
        V.slots.push_back(mkSlot(vec2(fx + 7.f, y0 + 14.f), east, VM_GUARD, VL_WORKER, 0.f, 24.f, 0.7f));
        for (int k = 0; k < 3; k++) {
            VenueSlot s = mkSlot(vec2(fx + 83.f + k * 6.5f, 1146.f), west, k == 1 ? VM_TALK : VM_LEAN, VL_CIVIL, 0.f, 24.f, 0.9f);
            s.prop = VP_TAXI;
            s.propPos = s.pos;
            s.propYaw = AI::dirYaw(north);
            s.propOff = vec2(-1.f, 0.1f);
            V.slots.push_back(s);
        }
        finish(V);
    }
    // ---------------------------------------------------------------- Sawgrass: anglers along the causeway shoulders
    // either side of the junction, birders round the observation tower at dawn, an airboat operator at the landing
    {
        vec2 marsh(-5000.f, 100.f);
        int node = -1;
        float bd = 400.f;
        for (int n = 0; n < (int)g.roads->nodes.size(); n++) {
            float d = length(g.roads->nodes[n].p - marsh);
            if (d < bd && g.map->regionAt(g.roads->nodes[n].p.x, g.roads->nodes[n].p.y) == World::REG_SAWGRASS) {
                bd = d;
                node = n;
            }
        }
        Venue V;
        V.name = "sawgrass causeway";
        V.c = node >= 0 ? g.roads->nodes[node].p : marsh;
        V.fillR = 230.f;
        V.releaseR = 340.f;
        if (node >= 0) {
            int placed = 0;
            for (int ei : g.roads->nodes[node].edges) {
                const World::RoadEdge& e = g.roads->edges[ei];
                if (e.cls == World::RC_DIRT || e.length < 150.f) continue;
                bool fromN0 = e.n0 == node;
                for (float sAt : {34.f, 61.f, 97.f, 128.f}) {
                    if (placed >= 7) break;
                    float at = fromN0 ? sAt : e.length - sAt;
                    vec3 c3 = e.posAt(at);
                    vec2 t = normalize(e.tangentAt(at).xy() + vec2(1e-5f, 0.f));
                    vec2 r = AI::rightOf(t);
                    int side = 0;   // the side with open water close by
                    for (int sd = -1; sd <= 1 && !side; sd += 2)
                        if (g.map->isWater(c3.x + r.x * sd * 14.f, c3.y + r.y * sd * 14.f)) side = sd;
                    if (!side) continue;
                    // on the bank past the shoulder where it is dry, else at the railing of the deck
                    vec2 bank = c3.xy() + r * ((float)side * (e.halfWidth + 1.4f));
                    vec2 rail = c3.xy() + r * ((float)side * (e.halfWidth - 0.35f));
                    vec2 spot = !g.map->isWater(bank.x, bank.y) ? bank : rail;
                    V.slots.push_back(mkSlot(spot, r * (float)side, placed % 3 == 2 ? VM_SIT : VM_WATCH, placed % 2 ? VL_BEACH : VL_CIVIL, 5.f, 19.5f, 0.8f));
                    placed++;
                }
            }
        }
        if (const World::SiteElem* tower = findElem(World::SK_OBS_TOWER, V.c, 700.f)) {
            for (int k = 0; k < 3; k++) {
                float a = 2.4f + k * 0.9f;
                vec2 off(cosf(a), sinf(a));
                V.slots.push_back(mkSlot(tower->c + off * (tower->hx + 2.5f + k * 1.2f), off, VM_SPOTTER, k == 1 ? VL_CIVIL : VL_BEACH, 5.3f, 10.5f, 0.85f));
            }
        }
        if (const World::SiteElem* dock = findElem(World::SK_DOCK, V.c, 400.f)) {
            vec2 ax = dock->ax, ay = perp(ax);
            vec2 end = dock->c + ax * (dock->hx - 3.f);
            VenueSlot op = mkSlot(end + ay * (dock->hy - 1.5f), ay, VM_STAND, VL_CIVIL, 6.5f, 18.5f, 0.95f);
            op.prop = VP_AIRBOAT;
            op.propPos = end + ay * (dock->hy + 3.2f);
            op.propYaw = AI::dirYaw(ax);
            V.slots.push_back(op);
            for (int k = 0; k < 2; k++)
                V.slots.push_back(mkSlot(dock->c + ax * (k * 1.1f - 2.f) + ay * (0.5f + k * 0.6f), k ? -ay : ay, k ? VM_PHONE : VM_TALK, VL_BEACH, 8.f, 17.5f, 0.6f));
        }
        finish(V);
    }
    LOG("population: venues laid out: %d (%d slots kept, %d dropped on lanes / water / buildings)", (int)gVenues.v.size(), kept, dropped);
}

bool venuePedLive(const GameWorld& g, const VenueSlot& s) {
    if (s.ped < 0 || s.ped >= (int)g.peds.size()) return false;
    const Ped& p = g.peds[s.ped];
    return p.used && p.uid == s.pedUid;
}

int venueChar(GameWorld& g, u8 look, u32 seed) {
    switch (look) {
        case VL_WORKER: return g.randomCivilianChar(seed, 5);
        case VL_BUSINESS: return g.randomCivilianChar(seed, 3);
        case VL_BEACH: return g.randomCivilianChar(seed, 4);
        case VL_TRAVELER: return g.randomCivilianChar(seed >> 2, (seed % 5 == 0) ? 3 : ((seed % 5 == 1) ? 4 : 0));
        default: return g.randomCivilianChar(seed, 0);
    }
}

void releaseVenueSlot(GameWorld& g, VenueSlot& s, bool despawn) {
    if (venuePedLive(g, s)) {
        Ped& p = g.peds[s.ped];
        PedAI& pa = g.pedAI(s.ped);
        if (despawn) {
            g.despawnPed(s.ped);
        } else if (pa.activity == ACT_VENUE) {
            pa.activity = ACT_WALK;   // an ordinary pedestrian from now on
            pa.navOk = false;
            pa.stance = 0;
            pa.venue = -1;
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
        } else {
            pa.venue = -1;
        }
    }
    s.ped = -1;
    if (s.veh >= 0 && s.veh < (int)g.vehicles.size() && g.vehicles[s.veh].used && g.vehicles[s.veh].uid == s.vehUid) {
        Vehicle& v = g.vehicles[s.veh];
        bool occupied = false;
        for (int k = 0; k < 8; k++) occupied |= v.seats[k] >= 0;
        if (despawn && !occupied) g.despawnVehicle(s.veh, true);
        else v.persistent = false;   // the population recycles it like any parked car
    }
    s.veh = -1;
}

void updateVenues(GameWorld& g, vec3 pp, float dt, bool warm, float tod) {
    if (!gVenues.built) buildVenues(g);
    for (int vi = 0; vi < (int)gVenues.v.size(); vi++) {
        Venue& V = gVenues.v[vi];
        float d = length(V.c - pp.xy());
        if (!V.active && d < V.fillR) {
            V.active = true;
            V.visits++;
        }
        if (V.active && d > V.releaseR) {
            for (VenueSlot& s : V.slots) {
                bool seen = venuePedLive(g, s) && g.inCameraView(g.peds[s.ped].pos.toVec3() + vec3(0, 0, 1.f), 1.f);
                releaseVenueSlot(g, s, !seen);
                s.cooldown = 0.f;
            }
            V.active = false;
        }
        if (!V.active) continue;
        int budget = warm ? 40 : 1;
        for (int si = 0; si < (int)V.slots.size(); si++) {
            VenueSlot& s = V.slots[si];
            s.cooldown -= dt;
            if (s.ped >= 0) {
                // still ours? (fled from gunfire, got knocked down, walked off: let the slot go, a new face later)
                bool live = venuePedLive(g, s);
                const PedAI* pa = live ? &g.pedAI(s.ped) : nullptr;
                if (!live || g.peds[s.ped].health <= 0.f || !pa || pa->venue != vi * 64 + si) {
                    releaseVenueSlot(g, s, false);
                    s.cooldown = 50.f + hashToFloat(hash32(V.visits * 131u + si * 7u + (u32)g.time)) * 50.f;
                }
                continue;
            }
            if (s.cooldown > 0.f || budget <= 0 || !venueHours(tod, s.h0, s.h1)) continue;
            if (s.follows && (si == 0 || V.slots[si - 1].ped < 0)) continue;
            u32 h = hash32(V.visits * 2654435761u + (u32)si * 40503u + (s.every > 0.f ? (u32)(g.time / Max(s.every, 1.f)) : 0u));
            if (hashToFloat(h) > s.chance) {
                s.cooldown = s.every > 0.f ? s.every : 1e9f;   // not this visit (streams: not this time round)
                continue;
            }
            // the vehicle that goes with the slot (a rig, a cab, the airboat), parked with nobody in it - out of sight
            if (s.prop != VP_NONE && (s.veh < 0 || s.veh >= (int)g.vehicles.size() || !g.vehicles[s.veh].used || g.vehicles[s.veh].uid != s.vehUid)) {
                Vehicles::VehicleClass cls = s.prop == VP_TRUCK ? Vehicles::VC_TRUCK : (s.prop == VP_TAXI ? Vehicles::VC_TAXI : Vehicles::VC_AIRBOAT);
                int model = g.findVehicleModel(cls, h >> 3);
                s.veh = -1;
                if (model >= 0) {
                    float vz = s.prop == VP_AIRBOAT ? 0.4f : g.groundHeight(s.propPos.x, s.propPos.y, pp.z + 20.f) + 0.4f;
                    bool vSeen = !warm && g.inCameraView(vec3(s.propPos, vz), 4.f) && length(s.propPos - pp.xy()) < 160.f;
                    if (!vSeen) {
                        int vid = g.spawnVehicle(model, dvec3(s.propPos.x, s.propPos.y, vz), s.propYaw, false);
                        if (vid >= 0) {
                            Vehicle& v = g.vehicles[vid];
                            v.parked = true;
                            v.persistent = true;   // (the venue lets it go when the player leaves)
                            v.sim.engineOn = false;
                            s.veh = vid;
                            s.vehUid = v.uid;
                        }
                    }
                }
                if (s.veh < 0) {
                    s.cooldown = 3.f;
                    continue;
                }
            }
            // where they appear: travelers heading out come through a door (fine in view), everyone else out of sight
            bool out = s.mode == VM_TRAVEL_OUT;
            vec2 at = out ? s.pos2 : s.pos;
            float faceYaw = s.yaw;
            if (s.mode == VM_TRAVEL_IN && warm) at = s.pos + (s.pos2 - s.pos) * (hashToFloat(hash32(h + 5u)) * 0.85f);   // mid-way at a fade-in
            if (s.veh >= 0) {
                // beside the vehicle: its driver's side (propOff), leaning back on it or facing along it
                const Vehicle& v = g.vehicles[s.veh];
                vec3 bh = g.vassets[v.model].spec.boxHalf;
                vec2 vf = v.sim.forward().xy();
                vf = length2(vf) > 1e-6f ? normalize(vf) : vec2(0.f, 1.f);
                vec2 vr = AI::rightOf(vf);
                vec2 side = vr * (s.propOff.x >= 0.f ? 1.f : -1.f);
                at = v.sim.body.pos.toVec3().xy() + side * (bh.x + 0.5f) + vf * (s.propOff.y * bh.y);
                faceYaw = AI::dirYaw(s.mode == VM_LEAN ? side : vf);
            }
            float z = g.groundHeight(at.x, at.y, pp.z + 20.f);
            vec3 p3(at.x, at.y, z);
            bool seen = !warm && g.inCameraView(p3 + vec3(0, 0, 1.f), 1.5f) && length(at - pp.xy()) < 120.f;
            if ((seen && !out) || (!warm && length(at - pp.xy()) < 10.f) || !freeStandingSpot(g, p3)) {
                s.cooldown = 2.f;
                continue;
            }
            int ci = venueChar(g, s.look, h >> 5);
            if (ci < 0) continue;
            int id = g.spawnPed(ci, dvec3(p3), out ? AI::dirYaw(normalize(s.pos - s.pos2 + vec2(1e-4f, 0.f))) : faceYaw, FAC_CIVILIAN);
            if (id < 0) continue;
            budget--;
            Ped& p = g.peds[id];
            PedAI& pa = g.pedAI(id);
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
            pa.role = s.look == VL_WORKER ? PR_WORKER : (s.look == VL_BUSINESS ? PR_BUSINESS : (s.look == VL_BEACH ? PR_BEACH : PR_CIVILIAN));
            if (s.mode == VM_TRAVEL_IN) {
                // off to the door and inside (population's door walkers)
                pa.activity = ACT_ENTER_VEH;
                pa.targetVeh = -1;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = dvec3(vec3(s.pos2, g.groundHeight(s.pos2.x, s.pos2.y, z + 2.f)));
                p.brain.speed = 1.25f + hashToFloat(hash32(h + 9u)) * 0.35f;
                p.brain.timer = 0.f;
                p.yaw = AI::dirYaw(normalize(s.pos2 - at + vec2(1e-4f, 0.f)));
                s.cooldown = s.every * (0.7f + hashToFloat(hash32(h + 3u)) * 0.6f);
                continue;   // (a stream: nothing to hold on to)
            }
            pa.activity = ACT_VENUE;
            pa.venue = vi * 64 + si;
            pa.venueMode = s.mode;
            pa.anchor = s.veh >= 0 ? at : s.pos;
            pa.anchorYaw = faceYaw;
            pa.anchorB = s.pos2;
            pa.anchorBYaw = s.yaw2;
            pa.clip = -1;
            pa.clipTimer = 2.f + hashToFloat(hash32(h + 17u)) * 6.f;
            switch (s.mode) {
                case VM_SMOKE: pa.stance = 10; break;
                case VM_TALK: pa.stance = 7; break;
                case VM_PHONE: pa.stance = 8; break;
                case VM_LEAN: pa.stance = 11; break;
                case VM_SIT: pa.stance = 21; break;
                case VM_TRAVEL_OUT: pa.stance = 8; break;
                case VM_FAREWELL:
                case VM_SEEOFF: pa.stance = 7; break;
                default: pa.stance = 0; break;
            }
            // how long before the next move: pacing legs, the wait at the curb, a goodbye
            pa.actTimer = s.mode == VM_PACE ? 8.f + hashToFloat(hash32(h + 21u)) * 18.f
                                            : (s.mode == VM_TRAVEL_OUT ? 25.f + hashToFloat(hash32(h + 21u)) * 40.f
                                                                       : (s.mode == VM_FAREWELL ? 14.f + hashToFloat(hash32(h + 21u)) * 16.f : 1e5f));
            if (s.follows && si > 0 && venuePedLive(g, V.slots[si - 1])) {
                // the other half of a pair: face them, and part a moment after they go
                const VenueSlot& o = V.slots[si - 1];
                pa.actTimer = g.pedAI(o.ped).actTimer + 1.5f;
                pa.anchorYaw = AI::dirYaw(normalize(o.pos - s.pos + vec2(1e-4f, 0.f)));
                p.yaw = pa.anchorYaw;
            }
            if (s.mode == VM_PACE && (h & 1)) {   // half of them start at the far end
                std::swap(pa.anchor, pa.anchorB);
                std::swap(pa.anchorYaw, pa.anchorBYaw);
            }
            if (!out) p.yaw = faceYaw;
            s.ped = id;
            s.pedUid = p.uid;
            if (s.every > 0.f) s.cooldown = s.every;
        }
    }
}

// the venue ped count near the player (census / autoplay)
int venuePedsNear(const GameWorld& g, vec2 at, float r) {
    int n = 0;
    for (int i = 0; i < (int)g.peds.size() && i < (int)g.ai.ped.size(); i++) {
        const Ped& p = g.peds[i];
        if (!p.used || g.ai.ped[i].uid != p.uid || g.ai.ped[i].venue < 0) continue;
        n += length(p.pos.toVec3().xy() - at) < r;
    }
    return n;
}

}  // namespace pop_detail

using namespace pop_detail;

bool aiVenueStep(GameWorld& g, int id, float dt) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    (void)dt;
    vec2 pos = p.pos.toVec3().xy();
    bool there = length(pa.anchor - pos) <= 0.35f;
    bool idle = there && pa.clipTimer <= 0.f && p.pendingAction < 0;
    u32 hq = hash32(p.uid * 5u + (u32)(g.time * 2.0));
    // a flashy car rolling by slowly: a point and a word - without leaving the post
    const Ped* pl = g.playerPed();
    if (pl && there && pl->state == PS_INVEHICLE && pl->vehicle >= 0 && p.pendingAction < 0 && pa.barkCooldown <= 0.f && pa.venueMode != VM_SIT) {
        const Vehicle& pv = g.vehicles[pl->vehicle];
        Vehicles::VehicleClass cls = g.vassets[pv.model].spec.cls;
        bool flashy = cls == Vehicles::VC_SUPER || cls == Vehicles::VC_SPORTS || cls == Vehicles::VC_MUSCLE;
        if (flashy && pv.sim.speed() < 11.f && length(pv.sim.body.pos.toVec3().xy() - pos) < 14.f &&
            hashToFloat(hash32(p.uid * 131u + (u32)(g.time * 0.5))) < 0.05f) {
            p.pendingAction = Anim::CLIP_POINT;
            pa.clipTimer = 6.f;
            g.aiSay(id, BK_NICE_CAR, 0.7f);
        }
    }
    switch (pa.venueMode) {
        case VM_PACE:
            // over to the other end once the break here is over (the walk there counts against the next one)
            if (there && pa.actTimer <= 0.f) {
                std::swap(pa.anchor, pa.anchorB);
                std::swap(pa.anchorYaw, pa.anchorBYaw);
                pa.actTimer = 10.f + hashToFloat(hq) * 22.f + length(pa.anchor - pos) / 1.3f;
            }
            if (idle) {
                p.pendingAction = Anim::CLIP_IDLE_LOOK;
                pa.clipTimer = 5.f + hashToFloat(hq) * 7.f;
            }
            break;
        case VM_GUARD:
            if (idle) {
                // a vehicle rolling up slowly: wave it through; otherwise a look round, now and then pointing the way
                bool rolling = false;
                std::vector<int> close;
                g.vehiclesNear(pos, 18.f, close);
                for (int vi : close) {
                    const Vehicle& v = g.vehicles[vi];
                    float sp = v.sim.speed();
                    vec2 vp = v.sim.body.pos.toVec3().xy(), vf = v.sim.forward().xy();
                    if (sp > 0.8f && sp < 10.f && dot(pos - vp, vf) > 0.f) rolling = true;
                }
                p.pendingAction = rolling ? Anim::CLIP_WAVE : (hq % 5 == 0 ? Anim::CLIP_POINT : Anim::CLIP_IDLE_LOOK);
                pa.clipTimer = rolling ? 6.f : 6.f + hashToFloat(hq) * 8.f;
            }
            break;
        case VM_STAND:
        case VM_WATCH:
        case VM_SPOTTER:
            if (idle) {
                // anglers hold still for long spells; birders point things out to each other
                p.pendingAction = pa.venueMode == VM_SPOTTER && hq % 3 != 0 ? Anim::CLIP_POINT : Anim::CLIP_IDLE_LOOK;
                pa.clipTimer = (pa.venueMode == VM_WATCH ? 14.f : 7.f) + hashToFloat(hq) * 10.f;
            }
            break;
        case VM_TRAVEL_OUT:
            if (there && pa.actTimer <= 0.f) {
                // done waiting on the phone: flag down a cab (the taxi logic takes it from here)
                pa.activity = ACT_HAIL_TAXI;
                pa.targetVeh = -1;
                pa.stance = 0;
                pa.venue = -1;
                pa.actTimer = 0.f;
                pa.clipTimer = 0.f;
                return true;
            }
            break;
        case VM_FAREWELL:
            if (pa.actTimer <= 0.f) {
                // goodbyes said: in through the doors (and out of the simulation, population.cpp)
                pa.activity = ACT_ENTER_VEH;
                pa.targetVeh = -1;
                pa.venue = -1;
                pa.stance = 0;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = dvec3(vec3(pa.anchorB, g.groundHeight(pa.anchorB.x, pa.anchorB.y, p.pos.toVec3().z + 2.f)));
                p.brain.speed = 1.3f;
                p.brain.timer = 0.f;
                return true;
            }
            break;
        case VM_SEEOFF:
            if (pa.actTimer <= 0.f) {
                if (pa.clipTimer > -50.f) {
                    p.pendingAction = Anim::CLIP_WAVE;   // a wave after them
                    pa.clipTimer = -100.f;
                } else if (pa.actTimer < -2.f) {
                    pa.activity = ACT_WALK;              // and off along the sidewalk
                    pa.navOk = false;
                    pa.venue = -1;
                    pa.stance = 0;
                    p.brain.type = BRAIN_WANDER;
                    p.brain.edge = -1;
                    return true;
                }
            }
            break;
        default:
            break;   // (stances with loops of their own: smoke, phone, talk, lean, sit)
    }
    return false;
}

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
                    pa.homeVeh >= 0 || pa.venue >= 0;   // (venue crowds come and go with their venue)
        bool unseen = !inCameraView(p.pos.toVec3() + vec3(0, 0, 1.f), 1.f);
        if (d > (busy ? 420.f : pedDespawn) || (dead && p.stateTime > 60.f && d > 40.f && unseen) ||
            (!busy && d > 80.f && unseen && nPeds > wantPeds + 4)) {
            despawnPed(i);
            continue;
        }
        if (!dead && pa.venue < 0) {
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
            if (v.faction == FAC_POLICE && pinfo.wanted > 0)
                LOG("population: police vehicle %d recycled at %.0f m (limit %.0f, driver %d brain %d, wrecked %d)", i, d, limit, drv,
                    drv >= 0 ? (int)peds[drv].brain.type : -1, (int)(v.exploded || v.sim.wrecked));
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
    // ------------------------------------------------------------------ venue crowds (on top of the district's)
    updateVenues(*this, pp, dt, warm, tod);
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
        // company: friends, couples and families walk together (more so on the beach front and in the lively districts)
        bool lively = reg == World::REG_DOWNTOWN || reg == World::REG_BEACH || reg == World::REG_CALLE_LUNA || reg == World::REG_MIDTOWN ||
                      reg == World::REG_KEY_CORAL;
        w[PK_GROUP] = night && nightlifeArea(reg) ? 0.55f : (lively ? 0.4f : 0.25f);
        w[PK_CHAT] = 0.14f;
        w[PK_SPOT] = 0.2f;
        w[PK_HAIL] = (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_MIDTOWN || reg == World::REG_BEACH) && !night ? 0.05f : 0.f;
        w[PK_WALL] = 0.14f;
        // joggers: mornings and evenings, most on the beach front and along the water, some through the neighbourhoods
        bool jogHours = (tod > 6.f && tod < 10.f) || (tod > 17.f && tod < 20.5f);
        bool waterfront = map->coastDistance(center.x, center.y) < 90.f;
        w[PK_JOGGER] = jogHours ? (beach ? 0.6f : (waterfront ? 0.35f : (reg == World::REG_GROVE || reg == World::REG_SUBURBS ? 0.25f : 0.12f))) : 0.f;
        w[PK_SUNBATHER] = reg == World::REG_BEACH && tod > 9.f && tod < 18.f && rain < 0.2f ? 0.8f : 0.f;
        w[PK_BATHER] = beach && tod > 9.5f && tod < 18.5f && rain < 0.2f && map->coastDistance(center.x, center.y) < 160.f ? 0.5f : 0.f;
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
                float sq = hashToFloat(hash32(h * 13u));
                pa.stance = sq < 0.45f ? 12 : (sq < 0.75f ? 22 : 21);   // on the back / face down / sitting up on the towel
                pa.clip = -1;
                pa.actTimer = 90.f + hashToFloat(hash32(h * 3u)) * 200.f;
                spawned = 1;
                break;
            }
            case PK_BATHER: {
                // in the sea off the beach: waders standing in the shallows, swimmers a little further out
                bool ok = false;
                vec2 sp;
                float wz = 0.f, depth = 0.f;
                for (int k = 0; k < 8 && !ok; k++) {
                    u32 hk = hash32(h + k * 7919u);
                    float a2 = hashToFloat(hk) * kTwoPi, r2 = rMin + hashToFloat(hash32(hk)) * (rMax - rMin);
                    sp = center.xy() + vec2(cosf(a2), sinf(a2)) * r2;
                    World::Region wr = map->regionAt(sp.x, sp.y);
                    if (wr != World::REG_BEACH && wr != World::REG_KEY_CORAL) continue;
                    wz = map->waterAt(sp.x, sp.y);
                    if (wz < World::kNoWater + 1.f) continue;
                    depth = wz - map->heightAt(sp.x, sp.y);
                    float cd = map->coastDistance(sp.x, sp.y);
                    if (depth < 0.45f || depth > 2.4f || cd > -2.f || cd < -35.f) continue;
                    ok = true;
                }
                if (!ok) break;
                bool swim = depth > 1.45f;
                float gz = map->heightAt(sp.x, sp.y);
                vec3 p3(sp, swim ? wz - 1.3f : gz);
                if (visibleNear(p3) || tooClose(p3)) break;
                vec2 toShore = center.xy() - sp;
                float faceShore = AI::dirYaw(normalize(toShore + vec2(1e-3f, 0.f)));
                int n = (h >> 9) % 3 == 0 ? 2 : 1;   // now and then a pair
                for (int k = 0; k < n; k++) {
                    vec2 q = sp + AI::rightOf(AI::yawDir(faceShore)) * (k * 1.3f);
                    int id = spawnPed(randomCivilianChar(hash32(h + k * 57u) >> 3, 4), dvec3(q.x, q.y, p3.z), faceShore + (k ? kPi * 0.5f : 0.f), FAC_CIVILIAN);
                    if (id < 0) break;
                    Ped& p = peds[id];
                    p.brain.type = BRAIN_WANDER;
                    p.brain.edge = -1;
                    PedAI& pa = pedAI(id);
                    pa.role = PR_BEACH;
                    pa.activity = ACT_SCENARIO;
                    pa.anchor = q;
                    pa.anchorYaw = n == 2 ? faceShore + (k ? -1.4f : 1.4f) : faceShore + kPi * ((h >> 12) & 1);   // facing each other / the waves
                    pa.stance = !swim && n == 2 ? 7 : 0;
                    pa.clip = -1;
                    pa.actTimer = 60.f + hashToFloat(hash32(h * 3u + k)) * 120.f;
                    nPeds++;
                    spawned++;
                }
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
                    pa.stance = qr < 0.35f ? 7 : (qr < 0.5f ? 8 : 23);   // chatting / on the phone / waiting in line
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
                pa.stance = bench ? 6 : ((h >> 7) % 3 == 0 ? 8 : 23);   // seated / on the phone / waiting
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
        // cul-de-sacs see little through traffic, and nothing too long to swing round the turning circle
        if (L.toNode >= 0 && laneGraph.nodes[L.toNode].deadEnd) {
            float yMin = 1e9f, yMax = -1e9f;
            for (const Vehicles::WheelSpec& w : spec.wheels) {
                yMin = Min(yMin, w.pos.y);
                yMax = Max(yMax, w.pos.y);
            }
            if (laneGraph.nodes[L.toNode].uturnBlocked || yMax - yMin > 3.3f || (h >> 9) % 4 != 0) continue;
        }
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
        if (World::roadWorkZoneAt(c.xy())) continue;   // the parking strip is fenced off for road works
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
    // ------------------------------------------------------------------ owners coming back to their parked cars
    // (someone walks up - or steps out of a door - gets in, blinks and pulls out into the traffic: traffic.cpp)
    gPop.departTimer -= dt;
    if (!warm && gPop.departTimer <= 0.f && nTraffic < wantTraffic + 4) {
        u32 h = nextSeed();
        gPop.departTimer = (20.f + hashToFloat(h) * 25.f) / Max(ai.lifeBoost, 0.1f);   // a car or two a minute pulls out
        int car = -1;
        float bestR = 2.f;
        for (int i = 0; i < (int)vehicles.size(); i++) {
            const Vehicle& v = vehicles[i];
            if (!v.used || !v.parked || v.seats[0] >= 0 || v.playerUsed || v.persistent || v.scripted || v.exploded || v.sim.wrecked || v.locked) continue;
            if (vassets[v.model].spec.cls > Vehicles::VC_MUSCLE) continue;
            const VehAI& va = vehAI(i);
            if (va.eventId >= 0 || va.errand != 0 || va.pullTimer < 0.f) continue;   // (pullTimer < 0: its driver just walked off)
            float d = length(v.sim.body.pos.toVec3().xy() - pp.xy());
            if (d < 25.f || d > 110.f) continue;
            float r = hashToFloat(hash32(h + v.uid * 7u));
            if (r < bestR) {
                bestR = r;
                car = i;
            }
        }
        if (car >= 0) {
            Vehicle& v = vehicles[car];
            const Vehicles::VehicleModel& spec = vassets[v.model].spec;
            vec3 cp = v.sim.body.pos.toVec3();
            vec2 cf = v.sim.forward().xy();
            cf = length2(cf) > 1e-6f ? normalize(cf) : vec2(0, 1);
            float lu = 0.f, llat = 0.f;
            int lane = laneGraph.nearestLane(cp.xy(), cf, 7.f, &lu, &llat);
            // parked along a lane's curb (to its right) with room ahead to swing out
            bool ok = lane >= 0 && llat > 1.2f && llat < 5.f;
            int self = car < (int)ai.vehBody.size() ? ai.vehBody[car] : -1;
            if (ok)
                traffic.hash.query(traffic.bodies, cp.xy() - vec2(14.f), cp.xy() + vec2(14.f), [&](int bi) {
                    if (!ok || bi == self) return;
                    const AI::Body& ob = traffic.bodies[bi];
                    if (ob.kind != AI::BK_CAR) return;
                    vec2 rl = ob.pos - cp.xy();
                    float al = dot(rl, cf), lt = dot(rl, AI::rightOf(cf));
                    if (al > 0.f && al - ob.halfLen - spec.boxHalf.y < 6.5f && fabsf(lt) < 2.2f) ok = false;
                });
            int who = -1;
            if (ok) {
                // a passer-by nearby who "owns" it...
                float bd = 32.f;
                for (int i = 0; i < (int)peds.size(); i++) {
                    const Ped& p = peds[i];
                    if (!p.used || p.isPlayer || p.persistent || p.state != PS_ONFOOT || p.faction != FAC_CIVILIAN || p.brain.type != BRAIN_WANDER) continue;
                    if (i >= (int)ai.ped.size() || ai.ped[i].uid != p.uid) continue;
                    const PedAI& pa = ai.ped[i];
                    if (pa.activity != ACT_WALK || pa.leader >= 0 || pa.eventId >= 0 || pa.homeVeh >= 0 ||
                        (pa.role != PR_CIVILIAN && pa.role != PR_BUSINESS && pa.role != PR_TOURIST && pa.role != PR_NIGHTLIFE))
                        continue;
                    vec2 rp = p.pos.toVec3().xy() - cp.xy();
                    float d = length(rp);
                    if (dot(rp, AI::rightOf(cf)) < 1.f) continue;   // on the sidewalk side of the car, not across the road
                    if (d < bd) {
                        bd = d;
                        who = i;
                    }
                }
                // ...or someone stepping out of a door close by
                vec3 door;
                if (who < 0 && aiBuildingDoorNear(*this, cp.xy(), 24.f, h, door) && length(door.xy() - pp.xy()) > 14.f && freeStandingSpot(*this, door)) {
                    World::Region creg = map->regionAt(cp.x, cp.y);
                    int charRole = (creg == World::REG_FINANCIAL || creg == World::REG_DOWNTOWN) && (h >> 7) % 3 == 0 ? 3 : 0;
                    who = spawnPed(randomCivilianChar(h >> 3, charRole), dvec3(door), AI::dirYaw(normalize(cp.xy() - door.xy() + vec2(1e-3f, 0.f))), FAC_CIVILIAN);
                    if (who >= 0) {
                        peds[who].brain.type = BRAIN_WANDER;
                        peds[who].brain.edge = -1;
                        pedAI(who).role = charRole == 3 ? PR_BUSINESS : PR_CIVILIAN;
                    }
                }
            }
            if (who >= 0) {
                PedAI& pa = pedAI(who);
                pa.activity = ACT_DRIVE_OFF;
                pa.homeVeh = car;
                pa.actTimer = 45.f;
                pa.clipTimer = -1.f;
                pa.walkStance = 0;
                vehAI(car).pullOut = 0;
                vehAI(car).role = VR_TRAFFIC;
            }
        }
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
