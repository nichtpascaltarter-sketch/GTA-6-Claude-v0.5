// Public transit gameplay: the SkyLine metro loop (timetable, trains as scripted vehicles with doors, lights, sound and
// announcements, riders boarding and alighting, platform crowds, police following the player), riding as a passenger
// with a fare, skip-to-next-station and alighting at any station. City buses (transit_bus.cpp) and the bay ferries
// (transit_ferry.cpp) are included at the end of this file and share the helpers here.
//
// Every train's position is a pure function of the transit clock (GameWorld::time + a skip offset): far trains cost a
// table lookup, trains within kMaterialize of the player become scripted Vehicles (three cars each) so the passenger,
// camera, combat and rendering mechanics of the vehicle system apply to them.
#include "gameworld.h"
#include "../world/transit.h"
#include "transit_schedule.h"

namespace Game {
namespace Transit {

namespace tg {

using namespace World::transit_dims;
using namespace tsched;   // Profile, TrainState, buildProfile, stateAt (transit_schedule.h)

constexpr int kTrainsPerDir = 4;
constexpr float kMaterialize = 430.f, kDematerialize = 520.f, kFarDraw = 2600.f, kRiderRange = 150.f;
constexpr float kBogie = 6.3f, kAxle = 1.1f, kRailLength = 18.3f;
constexpr int kFare = 3;
const float kDoorY[3] = {-5.6f, 0.f, 5.6f};

struct Rider {
    int ped = -1;
    u32 uid = 0;
};

struct Train {
    int dir = 0, idx = 0;
    float phase = 0.f;
    TrainState st;
    int cars[kCarsPerTrain] = {-1, -1, -1};
    u32 carUid[kCarsPerTrain] = {0, 0, 0};
    int driver = -1;
    u32 driverUid = 0;
    std::vector<Rider> riders;     // seated NPC passengers
    std::vector<Rider> cops;       // police who followed the player on board
    Audio::EmitterHandle sndRoll = 0, sndMotor = 0;
    float lastQ = -1.f;
    // per-stop event latches (reset when a new stop begins)
    int eventStop = -1;
    bool evOpen = false, evChime = false, evClose = false, evAlight = false, evAnnArrive = false;
    int annNextFor = -1, annApproachFor = -1, annPlatformFor = -1, copsCalledFor = -1;
    bool materialized() const { return cars[0] >= 0; }
};

struct Walker {
    int ped = -1;
    u32 uid = 0;
    int mode = 0;                  // 0 leave the station, 1 arrive onto the platform, 2 board a train
    std::vector<vec2> path;
    int wp = 0;
    int train = -1, car = -1;
    int station = -1, side = 0;
    float timer = 0.f;
};

struct Waiter {
    int ped = -1;
    u32 uid = 0;
    int station = -1, side = 0;
    vec2 spot;
};

struct State {
    bool init = false, failed = false;
    double clockOffset = 0.0;
    Profile prof[2];
    std::vector<Train> trains;
    int assetCab = -1, assetMid = -1;
    Render::Model* doorLeaf = nullptr;
    // player
    int rideTrain = -1, rideCar = -1;
    float skipHold = 0.f;
    int skipStage = 0;             // 0 none, 1 fading out, 2 fading in
    int boardTrain = -1, boardCar = -1;
    float boardTimer = 0.f;
    vec2 boardPoint;
    float hintTimer = 0.f;
    int lastHintStation = -1;
    // people
    std::vector<Walker> walkers;
    std::vector<Waiter> waiters;
    float crowdTimer = 0.f;
    // blips
    std::vector<std::string> blipNames;
    float blipTimer = 0.f;
    double lastStationPa = -100.0;
    // stats (autoplay / log)
    int boardings = 0, alightings = 0, riderBoard = 0, riderAlight = 0;
    int savedView = -1;            // vehicle camera view before boarding (the train rides in the cab / saloon view)
};
State gS;

inline float nowClock(const GameWorld& g) { return (float)fmod(g.time + gS.clockOffset, 1.0e7); }

// ---------------------------------------------------------------------------------------------------- car placement
struct CarPose {
    dvec3 pos;
    quat rot;
    vec3 fwdTravel;    // unit travel direction
};

vec3 trackPoint(const Profile& P, float q) {
    const World::MetroLine& L = World::gTransit->metro;
    return L.railPoint(P.sOf(q), P.lateral());
}

CarPose carPose(const Profile& P, float qCenter, int car) {
    const World::MetroLine& L = World::gTransit->metro;
    float qc = qCenter + (1 - car) * kCarPitch;
    vec3 pf = trackPoint(P, qc + kBogie), pr = trackPoint(P, qc - kBogie);
    vec3 fwd = normalize(pf - pr);
    // banked up vector from the corridor frame at the car center
    vec2 pos, tan;
    float z, bk;
    float s = P.sOf(qc);
    L.frame(s, pos, tan, &z, &bk);
    vec3 t3 = normalize(vec3(tan, 0.f));
    vec3 r0 = normalize(cross(t3, vec3(0, 0, 1)));
    vec3 u0 = cross(r0, t3);
    vec3 up = normalize(u0 * cosf(bk) - r0 * sinf(bk));
    vec3 Y = car == kCarsPerTrain - 1 ? -fwd : fwd;
    vec3 Z = normalize(up - Y * dot(up, Y));
    vec3 X = cross(Y, Z);
    CarPose cp;
    cp.pos = dvec3((pf + pr) * 0.5f);
    cp.rot = normalize(quatFromMat3(mat3(X, Y, Z)));
    cp.fwdTravel = fwd;
    return cp;
}

// World point of a door on the platform side (car local x sign +1 for the forward-facing cars, -1 for the rear cab)
vec3 doorPoint(const GameWorld& g, int veh, int door, float out) {
    const Vehicle& v = g.vehicles[veh];
    float sx = 1.f;
    // the rear cab car of a consist faces backward: the platform (train's right) is its local -X
    for (const Train& t : gS.trains)
        if (t.cars[kCarsPerTrain - 1] == veh) sx = -1.f;
    vec3 local(sx * (1.45f + out), kDoorY[door], 1.12f);
    return v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, local);
}

int trainOfVehicle(int veh) {
    if (veh < 0) return -1;
    for (int i = 0; i < (int)gS.trains.size(); i++)
        for (int c = 0; c < kCarsPerTrain; c++)
            if (gS.trains[i].cars[c] == veh) return i;
    return -1;
}

bool pedValid(const GameWorld& g, int id, u32 uid) { return id >= 0 && id < (int)g.peds.size() && g.peds[id].used && g.peds[id].uid == uid; }

const World::MetroStation& stationOf(const Profile& P, int serviceIdx) { return World::gTransit->metro.stations[P.stIdx[serviceIdx]]; }

// Station platform frame helpers
vec3 platformPoint(const World::MetroStation& st, int side, float along, float lateral) {
    float S = side == 0 ? 1.f : -1.f;
    return st.local(along, S * lateral, st.platformZ());
}

// ---------------------------------------------------------------------------------------------------- assets
bool ensureAssets(GameWorld& g) {
    if (gS.assetCab >= 0) return true;
    Render::DynamicRenderer* dyn = g.renderer ? g.renderer->dynamic : nullptr;
    if (!dyn) return false;
    for (int k = 0; k < 2; k++) {
        bool cab = k == 0;
        VehicleAsset a;
        TransitModels::carSpec(cab, a.spec);
        MeshData lod[3];
        for (int l = 0; l < 3; l++) TransitModels::buildCar(cab, l, lod[l]);
        a.body = dyn->createModel(lod[0]);
        a.bodyLod[0] = dyn->createModel(lod[1]);
        a.bodyLod[1] = dyn->createModel(lod[2]);
        // vassets may reallocate: re-point every vehicle's model metadata afterwards
        g.vassets.push_back(a);
        if (cab) gS.assetCab = (int)g.vassets.size() - 1;
        else gS.assetMid = (int)g.vassets.size() - 1;
    }
    for (Vehicle& v : g.vehicles)
        if (v.used && v.model >= 0 && v.model < (int)g.vassets.size()) v.sim.model = &g.vassets[v.model].spec;
    MeshData leaf;
    TransitModels::buildDoorLeaf(leaf);
    gS.doorLeaf = dyn->createModel(leaf);
    LOG("Transit: SkyLine car models registered as vehicle assets %d / %d", gS.assetCab, gS.assetMid);
    return true;
}

// ---------------------------------------------------------------------------------------------------- materialization
void placeCars(GameWorld& g, Train& t, float dt) {
    const Profile& P = gS.prof[t.dir];
    for (int c = 0; c < kCarsPerTrain; c++) {
        int vi = t.cars[c];
        if (vi < 0 || !g.vehicles[vi].used || g.vehicles[vi].uid != t.carUid[c]) continue;
        Vehicle& v = g.vehicles[vi];
        CarPose cp = carPose(P, t.st.q, c);
        v.sim.body.pos = cp.pos;
        v.sim.body.rot = cp.rot;
        v.sim.body.vel = cp.fwdTravel * t.st.v;
        v.sim.body.angVel = vec3(0.f);
        v.sim.sleeping = true;
        // infrastructure: dents, fires and wrecks don't apply (windows still shatter)
        v.sim.health = 1000.f;
        v.sim.engineHealth = 1000.f;
        for (float& dz : v.sim.damageZones) dz = 0.f;
        v.sim.wrecked = false;
        v.fireTimer = 0.f;
        v.exploded = false;
        v.sim.impactImpulse = 0.f;
        v.sim.scrape = 0.f;
        v.sim.splash = 0.f;
        v.sim.brokenCount = 0;
        v.sim.engineOn = false;
        v.locked = true;
        v.scripted = true;
        v.persistent = true;
        v.renderFar = true;
        v.alarm = false;
        v.hornOn = false;
        // lamps: the leading cab shows white (reverse-lamp slot, needs the driver), the trailing cab red
        v.lightsOn = c == kCarsPerTrain - 1;
        v.sim.gear = c == 0 ? -1 : 1;
        v.ctl = Vehicles::VehicleControls();
        v.ctl.brake = t.st.a < -0.2f ? 1.f : 0.f;
        (void)dt;
    }
}

void spawnTrain(GameWorld& g, Train& t) {
    const Profile& P = gS.prof[t.dir];
    for (int c = 0; c < kCarsPerTrain; c++) {
        CarPose cp = carPose(P, t.st.q, c);
        vec3 f = rotate(cp.rot, vec3(0, 1, 0));
        int vi = g.spawnVehicle(c == 1 ? gS.assetMid : gS.assetCab, cp.pos, atan2f(-f.x, f.y), false);
        if (vi < 0) break;
        Vehicle& v = g.vehicles[vi];
        v.scripted = true;
        v.persistent = true;
        v.locked = true;
        v.renderFar = true;
        v.sim.engineOn = false;
        v.dirt = 0.05f + 0.1f * hashToFloat(hash32(v.uid));
        t.cars[c] = vi;
        t.carUid[c] = v.uid;
    }
    // operator in the leading cab (invisible behind the tinted windscreen at a distance, needed for the head lamps)
    if (t.cars[0] >= 0 && !g.chars.empty()) {
        int pid = g.spawnPed(g.randomCivilianChar(hash32(t.dir * 31u + t.idx * 7u) | 1u, 0), g.vehicles[t.cars[0]].sim.body.pos, 0.f, FAC_CIVILIAN);
        if (pid >= 0) {
            g.peds[pid].persistent = true;
            g.peds[pid].invincible = true;
            g.peds[pid].brain.type = BRAIN_NONE;
            g.warpPedIntoVehicle(pid, t.cars[0], 0);
            t.driver = pid;
            t.driverUid = g.peds[pid].uid;
        }
    }
    placeCars(g, t, 0.f);
}

void despawnTrain(GameWorld& g, Train& t) {
    for (Rider& r : t.riders)
        if (pedValid(g, r.ped, r.uid)) g.despawnPed(r.ped);
    t.riders.clear();
    for (Rider& r : t.cops)
        if (pedValid(g, r.ped, r.uid) && g.peds[r.ped].vehicle >= 0) g.removePedFromVehicle(r.ped, false);
    t.cops.clear();
    if (pedValid(g, t.driver, t.driverUid)) g.despawnPed(t.driver);
    t.driver = -1;
    for (int c = 0; c < kCarsPerTrain; c++) {
        int vi = t.cars[c];
        if (vi >= 0 && vi < (int)g.vehicles.size() && g.vehicles[vi].used && g.vehicles[vi].uid == t.carUid[c]) g.despawnVehicle(vi, true);
        t.cars[c] = -1;
    }
#ifdef HAVE_AUDIO
    if (t.sndRoll) Audio::destroyEmitter(t.sndRoll);
    if (t.sndMotor) Audio::destroyEmitter(t.sndMotor);
#endif
    t.sndRoll = t.sndMotor = 0;
}

bool carsValid(const GameWorld& g, const Train& t) {
    for (int c = 0; c < kCarsPerTrain; c++) {
        int vi = t.cars[c];
        if (vi < 0 || vi >= (int)g.vehicles.size() || !g.vehicles[vi].used || g.vehicles[vi].uid != t.carUid[c]) return false;
    }
    return true;
}


// ---------------------------------------------------------------------------------------------------- audio & PA
void speakPa(GameWorld& g, const std::string& line, vec3 pos, bool inside, bool female) {
#ifdef HAVE_AUDIO
    Speech::Persona ann = Speech::persona(female ? "announcer_female" : "announcer");
    std::string txt = "[pa][calm]" + line;
    if (inside) Audio::speak(txt.c_str(), ann.voice, 0.9f);
    else Audio::speakAt(txt.c_str(), ann.voice, pos, 1.f);
#else
    (void)pos;
    (void)inside;
    (void)female;
#endif
    if (g.settingsSubtitles && (inside || length(rel(g.rig.cam.pos, dvec3(pos))) < 45.f)) g.subtitle("SkyLine", line, 3.5f, 0xff4ad8e0);
}

std::string arrivalLine(const World::MetroStation& st) {
    std::string extra;
    if (st.name == "Airport") extra = " Change here for Porto Sol International terminals.";
    else if (st.name == "Civic Center") extra = " For City Hall and Tidewater Stadium.";
    else if (st.name == "Solaris") extra = " For Solaris One and the Financial District.";
    else if (st.name == "Canvas District") extra = " For Canvas Park and the galleries.";
    return st.name + "." + extra;
}

void trainAudio(GameWorld& g, Train& t, vec3 listener) {
#ifdef HAVE_AUDIO
    const Vehicle& mid = g.vehicles[t.cars[1]];
    vec3 c = mid.sim.body.pos.toVec3();
    float d = length(c - listener);
    bool audible = d < 280.f;
    if (audible) {
        if (!t.sndRoll) t.sndRoll = Audio::createEmitter(Audio::EMIT_WIND_RUSH);
        if (!t.sndMotor) t.sndMotor = Audio::createEmitter(Audio::EMIT_ENGINE);
        vec3 vel = mid.sim.body.vel;
        float v = t.st.v;
        Audio::setEmitter(t.sndRoll, c + vec3(0, 0, 0.6f), vel, v * 1.9f, 0, 0, 0, Saturate(v / 6.f) * 0.85f);
        float thr = t.st.a > 0.05f ? Saturate(t.st.a * 1.1f) : (t.st.a < -0.05f ? 0.35f : 0.05f);
        Audio::setEmitter(t.sndMotor, c, vel, Saturate(v / 22.f) * 0.85f + 0.08f, thr, Saturate(fabsf(t.st.a)), (float)Audio::ENGINE_ELECTRIC,
                          v > 0.3f || fabsf(t.st.a) > 0.05f ? 0.7f : 0.25f);
    } else {
        if (t.sndRoll) Audio::destroyEmitter(t.sndRoll);
        if (t.sndMotor) Audio::destroyEmitter(t.sndMotor);
        t.sndRoll = t.sndMotor = 0;
    }
    // wheelsets clattering over the rail joints (nearest two cars)
    if (d < 160.f && t.lastQ >= 0.f && t.st.v > 1.5f) {
        const Profile& P = gS.prof[t.dir];
        float dq = t.st.q - t.lastQ;
        if (dq < 0.f) dq += (float)P.len;
        if (dq > 0.f && dq < 8.f) {
            for (int c2 = 0; c2 < kCarsPerTrain; c2++) {
                vec3 cc = g.vehicles[t.cars[c2]].sim.body.pos.toVec3();
                if (length(cc - listener) > 120.f) continue;
                float qc = t.st.q + (1 - c2) * kCarPitch;
                for (int b = -1; b <= 1; b += 2)
                    for (int a = -1; a <= 1; a += 2) {
                        float qa = qc + b * kBogie + a * kAxle;
                        float prev = qa - dq;
                        if (floorf(qa / kRailLength) != floorf(prev / kRailLength)) {
                            vec3 p = trackPoint(P, qa);
                            Audio::play(Audio::SFX_RAIL_CLACK, p, 0.25f + Saturate(t.st.v / 18.f) * 0.55f, 0.85f + t.st.v * 0.012f);
                        }
                    }
            }
        }
    }
#else
    (void)g;
    (void)t;
    (void)listener;
#endif
}

// ---------------------------------------------------------------------------------------------------- riders
int freeCarSeat(const GameWorld& g, int veh) {
    const Vehicle& v = g.vehicles[veh];
    int ns = Min((int)g.vassets[v.model].spec.seats.size(), 8);
    for (int s = 1; s < ns; s++)
        if (v.seats[s] < 0) return s;
    return -1;
}

int spawnCivilian(GameWorld& g, u32 seed, vec3 pos, float yaw) {
    if (g.chars.empty()) return -1;
    World::Region reg = g.map->regionAt(pos.x, pos.y);
    int role = (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL) && (seed & 3) == 0 ? 3 : ((seed % 11) == 5 ? 4 : 0);
    int id = g.spawnPed(g.randomCivilianChar(seed, role), dvec3(pos), yaw, FAC_CIVILIAN);
    if (id < 0) return -1;
    g.peds[id].persistent = true;
    return id;
}

void seatRiders(GameWorld& g, Train& t, int want) {
    for (int c = 0; c < kCarsPerTrain && (int)t.riders.size() < want; c++) {
        int vi = t.cars[c];
        int per = want / kCarsPerTrain + ((t.idx + c) % 2);
        int have = 0;
        for (int s = 1; s < 8; s++)
            if (g.vehicles[vi].seats[s] >= 0) have++;
        for (int k = have; k < per; k++) {
            int seat = freeCarSeat(g, vi);
            if (seat < 0) break;
            u32 seed = hash32(g.vehicles[vi].uid * 131u + (u32)seat * 7919u + (u32)k);
            int id = spawnCivilian(g, seed, g.vehicles[vi].sim.body.pos.toVec3(), 0.f);
            if (id < 0) return;
            g.warpPedIntoVehicle(id, vi, seat);
            g.peds[id].brain.type = BRAIN_PASSENGER;
            t.riders.push_back({id, g.peds[id].uid});
        }
    }
}

// Ped leaves the train onto the platform in front of the door
void alightPed(GameWorld& g, int pid, int veh, int door) {
    g.removePedFromVehicle(pid, false);
    Ped& p = g.peds[pid];
    vec3 dp = doorPoint(g, veh, door, 0.55f);
    float gz = g.groundHeight(dp.x, dp.y, dp.z + 0.8f);
    p.pos = dvec3(dp.x, dp.y, gz);
    vec3 in = doorPoint(g, veh, door, -1.f);
    vec2 away = normalize(dp.xy() - in.xy());
    p.yaw = atan2f(-away.x, away.y);
    p.vel = vec3(0.f);
    p.grounded = true;
}

// Waypoints from a platform point to the street: through the fare gates at the exit end, down the stairs, onto the sidewalk
std::vector<vec2> exitPath(const World::MetroStation& st, int side, vec2 from) {
    float S = side == 0 ? 1.f : -1.f;
    int E = st.exitEnd[side];
    const float H = kPlatformHalfLen;
    std::vector<vec2> path;
    path.push_back(from);
    // through one of the fare gate lanes square on (the gaps between the cabinets are ~1 m wide)
    float gateA = E * (H - 3.3f + 0.65f + 1.3f * (float)(hash32((u32)(from.x * 10.f)) % 3u));
    path.push_back(st.local(gateA, S * 6.0f, 0.f).xy());
    path.push_back(st.local(gateA, S * 10.0f, 0.f).xy());
    path.push_back(st.local(E * (H - 3.0f), S * 9.4f, 0.f).xy());
    // stair foot: the stair descends toward the station center
    path.push_back(st.local(E * (H - 3.4f - 31.5f), S * 9.4f, 0.f).xy());
    path.push_back(st.local(E * (H - 3.4f - 36.f), S * 10.8f, 0.f).xy());
    return path;
}

void startWalker(GameWorld& g, int pid, int mode, std::vector<vec2> path, int station, int side, int train = -1, int car = -1) {
    Walker w;
    w.ped = pid;
    w.uid = g.peds[pid].uid;
    w.mode = mode;
    w.path = std::move(path);
    w.wp = 0;
    w.station = station;
    w.side = side;
    w.train = train;
    w.car = car;
    Ped& p = g.peds[pid];
    p.brain.type = BRAIN_GOTO;
    p.brain.speed = 1.35f + 0.2f * hashToFloat(hash32(p.uid));
    p.brain.goal = dvec3(w.path[0].x, w.path[0].y, p.pos.z);
    gS.walkers.push_back(w);
}

void releaseToCity(GameWorld& g, int pid) {
    Ped& p = g.peds[pid];
    p.persistent = false;
    p.brain.type = BRAIN_WANDER;
    p.brain.edge = -1;
    PedAI& pa = g.pedAI(pid);
    pa.activity = ACT_WALK;
    pa.navOk = false;
    pa.actTimer = 20.f;
}

void updateWalkers(GameWorld& g, float dt) {
    for (size_t i = 0; i < gS.walkers.size();) {
        Walker& w = gS.walkers[i];
        bool drop = !pedValid(g, w.ped, w.uid);
        Ped* pp = drop ? nullptr : &g.peds[w.ped];
        if (pp && (pp->state != PS_ONFOOT || pp->health <= 0.f || pp->brain.type != BRAIN_GOTO)) {
            // scared off, knocked down or taken over by the AI: the city takes over
            if (pp->health > 0.f && pp->state != PS_INVEHICLE) pp->persistent = false;
            drop = true;
        }
        if (drop) {
            gS.walkers[i] = gS.walkers.back();
            gS.walkers.pop_back();
            continue;
        }
        Ped& p = *pp;
        w.timer += dt;
        vec2 pos = p.pos.toVec3().xy();
        vec2 goal = w.path[w.wp];
        bool last = w.wp + 1 >= (int)w.path.size();
        float reach = last ? 0.7f : 0.55f;   // gate lanes are about a meter wide
        if (length(goal - pos) < reach || (w.timer > 12.f && !last)) {
            w.timer = 0.f;
            if (!last) {
                w.wp++;
                p.brain.goal = dvec3(w.path[w.wp].x, w.path[w.wp].y, p.pos.z);
            } else {
                bool done = true;
                if (w.mode == 0) {
                    releaseToCity(g, w.ped);
                } else if (w.mode == 1) {
                    // arrived on the platform: wait there
                    Waiter wt;
                    wt.ped = w.ped;
                    wt.uid = w.uid;
                    wt.station = w.station;
                    wt.side = w.side;
                    wt.spot = goal;
                    p.brain.type = BRAIN_WANDER;
                    p.brain.edge = -1;
                    PedAI& pa = g.pedAI(w.ped);
                    pa.activity = ACT_SCENARIO;
                    pa.anchor = goal;
                    const World::MetroStation& st = World::gTransit->metro.stations[w.station];
                    vec2 toTrack = -st.right() * (w.side == 0 ? 1.f : -1.f);
                    pa.anchorYaw = atan2f(-toTrack.x, toTrack.y);
                    pa.stance = 23;
                    pa.clip = -1;
                    pa.actTimer = 600.f;
                    gS.waiters.push_back(wt);
                } else if (w.mode == 2) {
                    // at the door: step in (seat if one is free, otherwise they stand inside out of sight)
                    Train* tr = w.train >= 0 && w.train < (int)gS.trains.size() ? &gS.trains[w.train] : nullptr;
                    int vi = tr && tr->materialized() ? tr->cars[Clamp(w.car, 0, kCarsPerTrain - 1)] : -1;
                    if (tr && vi >= 0 && tr->st.stop >= 0 && tr->st.doors > 0.6f) {
                        int seat = freeCarSeat(g, vi);
                        if (seat > 0) {
                            g.warpPedIntoVehicle(w.ped, vi, seat);
                            p.brain.type = BRAIN_PASSENGER;
                            tr->riders.push_back({w.ped, w.uid});
                        } else {
                            g.despawnPed(w.ped);
                        }
                        gS.riderBoard++;
                    } else {
                        // missed it: wait for the next one
                        releaseToCity(g, w.ped);
                        p.persistent = true;
                        Waiter wt;
                        wt.ped = w.ped;
                        wt.uid = w.uid;
                        wt.station = w.station;
                        wt.side = w.side;
                        wt.spot = pos;
                        PedAI& pa = g.pedAI(w.ped);
                        pa.activity = ACT_SCENARIO;
                        pa.anchor = pos;
                        pa.stance = 23;
                        pa.actTimer = 600.f;
                        gS.waiters.push_back(wt);
                    }
                }
                (void)done;
                gS.walkers[i] = gS.walkers.back();
                gS.walkers.pop_back();
                continue;
            }
        }
        i++;
    }
}

// Platform crowds around the stations near the player
void updateCrowds(GameWorld& g, float dt, vec3 pp) {
    const World::MetroLine& L = World::gTransit->metro;
    gS.crowdTimer -= dt;
    // keep the waiters waiting (the AI would send them strolling when their activity timer runs out)
    for (size_t i = 0; i < gS.waiters.size();) {
        Waiter& w = gS.waiters[i];
        bool ok = pedValid(g, w.ped, w.uid) && g.peds[w.ped].health > 0.f && g.peds[w.ped].state == PS_ONFOOT;
        if (ok) {
            PedAI& pa = g.pedAI(w.ped);
            if (g.peds[w.ped].brain.type != BRAIN_WANDER || pa.activity != ACT_SCENARIO) ok = false;   // fleeing / reacting
            else pa.actTimer = Max(pa.actTimer, 60.f);
        }
        const World::MetroStation& st = L.stations[w.station];
        float d = length(st.pos - pp.xy());
        if (ok && d > 260.f) {
            g.despawnPed(w.ped);
            ok = false;
        }
        if (!ok) {
            if (pedValid(g, w.ped, w.uid)) g.peds[w.ped].persistent = false;
            gS.waiters[i] = gS.waiters.back();
            gS.waiters.pop_back();
            continue;
        }
        i++;
    }
    if (gS.crowdTimer > 0.f || g.populationOff) return;
    gS.crowdTimer = 1.5f;
    float tod = g.env ? g.env->timeOfDay : 12.f;
    float busy = (tod > 6.5f && tod < 9.5f) || (tod > 16.f && tod < 19.5f) ? 1.f : ((tod > 22.5f || tod < 5.5f) ? 0.25f : 0.65f);
    for (int si = 0; si < (int)L.stations.size(); si++) {
        const World::MetroStation& st = L.stations[si];
        float d = length(st.pos - pp.xy());
        if (d > 190.f) continue;
        for (int side = 0; side < 2; side++) {
            int have = 0;
            for (const Waiter& w : gS.waiters)
                if (w.station == si && w.side == side) have++;
            for (const Walker& w : gS.walkers)
                if (w.station == si && w.side == side && w.mode == 1) have++;
            int want = (int)(busy * (4.f + 5.f * hashToFloat(hash32(st.seed + (u32)side * 77u)))) + 1;
            if (have >= want) continue;
            u32 h = hash32(st.seed * 31u + (u32)side * 7u + (u32)(g.time * 3.0) + (u32)have * 131u);
            float along = (hashToFloat(h) - 0.5f) * (kPlatformHalfLen * 2.f - 12.f);
            float lat = 4.6f + hashToFloat(hash32(h)) * 1.9f;
            vec3 spot = platformPoint(st, side, along, lat);
            bool visible = g.inCameraView(spot + vec3(0, 0, 1.f), 1.f) && length(spot - g.rig.cam.pos.toVec3()) < 120.f;
            float S = side == 0 ? 1.f : -1.f;
            if (!visible && g.populationWarmup <= 0.f && d < 190.f && (h & 1)) {
                // someone walks up from the street (spawned at the stair foot when that is out of view)
                int E = st.exitEnd[side];
                vec3 foot = st.local(E * (kPlatformHalfLen - 3.4f - 36.f), S * 10.8f, 0.f);
                foot.z = g.groundHeight(foot.x, foot.y, st.streetZ + 3.f);
                if (!g.inCameraView(foot + vec3(0, 0, 1.f), 2.f) || length(foot - g.rig.cam.pos.toVec3()) > 90.f) {
                    int id = spawnCivilian(g, h, foot, 0.f);
                    if (id >= 0) {
                        std::vector<vec2> path = exitPath(st, side, spot.xy());
                        std::reverse(path.begin(), path.end());
                        startWalker(g, id, 1, path, si, side);
                    }
                    continue;
                }
            }
            if (visible && g.populationWarmup <= 0.f) continue;
            int id = spawnCivilian(g, h, spot, 0.f);
            if (id < 0) continue;
            Ped& p = g.peds[id];
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
            PedAI& pa = g.pedAI(id);
            pa.activity = ACT_SCENARIO;
            pa.anchor = spot.xy();
            vec2 toTrack = -st.right() * S;
            pa.anchorYaw = atan2f(-toTrack.x, toTrack.y) + (hashToFloat(hash32(h * 3u)) - 0.5f) * 0.8f;
            p.yaw = pa.anchorYaw;
            pa.stance = (h >> 9) % 5 == 0 ? 8 : 23;   // on the phone or waiting
            pa.clip = -1;
            pa.actTimer = 600.f;
            Waiter w;
            w.ped = id;
            w.uid = p.uid;
            w.station = si;
            w.side = side;
            w.spot = spot.xy();
            gS.waiters.push_back(w);
        }
    }
}

// ---------------------------------------------------------------------------------------------------- station stop events
void stopEvents(GameWorld& g, int ti, vec3 listener, bool playerAboard) {
    Train& t = gS.trains[ti];
    const Profile& P = gS.prof[t.dir];
    if (t.st.stop < 0) return;
    if (t.eventStop != t.st.stop) {
        t.eventStop = t.st.stop;
        t.evOpen = t.evChime = t.evClose = t.evAlight = t.evAnnArrive = false;
    }
    const World::MetroStation& st = stationOf(P, t.st.stop);
    int side = t.dir == 0 ? 0 : 1;
    float D = t.st.dwell, tt = t.st.dwellT;
    vec3 mid = g.vehicles[t.cars[1]].sim.body.pos.toVec3();
    bool near_ = length(mid - listener) < 90.f;
    if (!t.evOpen && tt >= 1.2f) {
        t.evOpen = true;
#ifdef HAVE_AUDIO
        if (near_)
            for (int c = 0; c < kCarsPerTrain; c++) Audio::play(Audio::SFX_TRAIN_DOORS, doorPoint(g, t.cars[c], 1, 0.f), 0.8f);
#endif
        if (playerAboard && !t.evAnnArrive) {
            t.evAnnArrive = true;
            speakPa(g, arrivalLine(st), mid, true, true);
        }
    }
    if (!t.evAlight && tt >= 3.2f) {
        t.evAlight = true;
        // a few riders get off here and head for the exit
        for (size_t r = 0; r < t.riders.size();) {
            Rider rd = t.riders[r];
            bool ok = pedValid(g, rd.ped, rd.uid) && g.peds[rd.ped].vehicle >= 0;
            if (!ok) {
                t.riders.erase(t.riders.begin() + r);
                continue;
            }
            u32 h = hash32(rd.uid * 7919u + (u32)t.st.stop * 131u);
            if (h % 3 == 0) {
                int veh = g.peds[rd.ped].vehicle;
                int door = (int)((h >> 4) % 3);
                alightPed(g, rd.ped, veh, door);
                t.riders.erase(t.riders.begin() + r);
                startWalker(g, rd.ped, 0, exitPath(st, side, g.peds[rd.ped].pos.toVec3().xy()), P.stIdx[t.st.stop], side);
                gS.riderAlight++;
                continue;
            }
            r++;
        }
        // police who followed the player get off to keep chasing when the player does
        for (Rider& c : t.cops) {
            if (!pedValid(g, c.ped, c.uid) || g.peds[c.ped].vehicle < 0) continue;
            if (g.pinfo.wanted <= 0 || !playerAboard) {
                alightPed(g, c.ped, g.peds[c.ped].vehicle, 1);
            }
        }
        // waiting passengers on this platform walk to the nearest door
        for (size_t w = 0; w < gS.waiters.size();) {
            Waiter& wt = gS.waiters[w];
            if (wt.station != P.stIdx[t.st.stop] || wt.side != side || !pedValid(g, wt.ped, wt.uid)) {
                w++;
                continue;
            }
            if (hash32(wt.uid + (u32)t.idx * 17u) % 5 == 0) {   // waiting for the other line / a friend
                w++;
                continue;
            }
            int bestCar = 0, bestDoor = 0;
            float bd = 1e9f;
            vec2 wp = g.peds[wt.ped].pos.toVec3().xy();
            for (int c = 0; c < kCarsPerTrain; c++)
                for (int d = 0; d < 3; d++) {
                    float dd = length(doorPoint(g, t.cars[c], d, 0.6f).xy() - wp);
                    if (dd < bd) {
                        bd = dd;
                        bestCar = c;
                        bestDoor = d;
                    }
                }
            std::vector<vec2> path;
            path.push_back(doorPoint(g, t.cars[bestCar], bestDoor, 0.55f).xy());
            int pid = wt.ped;
            gS.waiters[w] = gS.waiters.back();
            gS.waiters.pop_back();
            startWalker(g, pid, 2, path, P.stIdx[t.st.stop], side, ti, bestCar);
        }
    }
    if (!t.evChime && tt >= D - 5.6f) {
        t.evChime = true;
#ifdef HAVE_AUDIO
        if (near_ || playerAboard) Audio::play(Audio::SFX_TRANSIT_CHIME, playerAboard ? listener : mid, 0.9f);
#endif
        if (playerAboard) g.subtitle("SkyLine", "Doors closing.", 2.f, 0xff4ad8e0);
    }
    if (!t.evClose && tt >= D - 4.3f) {
        t.evClose = true;
#ifdef HAVE_AUDIO
        if (near_)
            for (int c = 0; c < kCarsPerTrain; c++) Audio::play(Audio::SFX_TRAIN_DOORS, doorPoint(g, t.cars[c], 1, 0.f), 0.8f);
#endif
    }
}


// ---------------------------------------------------------------------------------------------------- player
// Steer the player (on foot) toward a world point by writing camera-relative movement input
void steerPlayer(GameWorld& g, vec2 target, float mag) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec2 d = target - pl->pos.toVec3().xy();
    float len = length(d);
    Controls& c = g.ctl;
    Controls keep = c;
    c = Controls();
    c.usingPad = keep.usingPad;
    c.look = keep.look;
    c.lookActive = keep.lookActive;
    c.pause = keep.pause;
    c.map = keep.map;
    if (len < 0.05f) return;
    d = d / len;
    float cy = g.rig.yaw;
    vec2 fwd(-sinf(cy), cosf(cy)), right(cosf(cy), sinf(cy));
    c.move = vec2(dot(d, right), dot(d, fwd)) * mag;
}

int nearestOpenDoor(GameWorld& g, vec2 p, float z, float maxDist, int& outTrain, int& outCar, int& outDoor) {
    float best = maxDist;
    int found = -1;
    for (int ti = 0; ti < (int)gS.trains.size(); ti++) {
        Train& t = gS.trains[ti];
        if (!t.materialized() || t.st.stop < 0 || t.st.doors < 0.7f) continue;
        for (int c = 0; c < kCarsPerTrain; c++)
            for (int d = 0; d < 3; d++) {
                vec3 dp = doorPoint(g, t.cars[c], d, 0.35f);
                if (fabsf(dp.z - z) > 1.6f) continue;
                float dd = length(dp.xy() - p);
                if (dd < best) {
                    best = dd;
                    outTrain = ti;
                    outCar = c;
                    outDoor = d;
                    found = ti;
                }
            }
    }
    return found;
}

void boardPlayer(GameWorld& g, int ti, int car) {
    Train& t = gS.trains[ti];
    int vi = t.cars[car];
    int seat = freeCarSeat(g, vi);
    for (int c = 0; c < kCarsPerTrain && seat < 0; c++)
        if (freeCarSeat(g, t.cars[c]) > 0) {
            vi = t.cars[c];
            seat = freeCarSeat(g, vi);
        }
    if (seat < 0) {
        // full: a rider gives up their seat (they stand further down the car, out of the simulation)
        for (size_t r = 0; r < t.riders.size(); r++)
            if (pedValid(g, t.riders[r].ped, t.riders[r].uid) && g.peds[t.riders[r].ped].vehicle == t.cars[car]) {
                seat = g.peds[t.riders[r].ped].seat;
                g.despawnPed(t.riders[r].ped);
                t.riders.erase(t.riders.begin() + r);
                vi = t.cars[car];
                break;
            }
    }
    if (seat < 0) return;
    g.warpPedIntoVehicle(g.player, vi, seat);
    g.pinfo.money = Max<long long>(0, g.pinfo.money - kFare);
    gS.boardings++;
    gS.rideTrain = ti;
    g.rig.cut = true;
    // a chase camera behind an 18 m car would sit inside the next car: ride in the interior view (cab or saloon)
    gS.savedView = g.rig.vehicleView;
    g.rig.vehicleView = 2;
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_PURCHASE, 0.35f);
#endif
    const Profile& P = gS.prof[t.dir];
    const World::MetroStation& nx = stationOf(P, t.st.next);
    g.notify("SkyLine", StrFormat("Loop %s - next stop %s. Fare $%d.", t.dir == 0 ? "A" : "B", nx.name.c_str(), kFare));
    g.help("Hold ~i:SPACE|A~ to skip to the next station. Press ~i:F|Y~ to get off when the doors open.", 7.f);
    // police on the player's heels follow onto the train
    if (g.pinfo.wanted > 0) {
        std::vector<int> near_;
        g.pedsNear(g.peds[g.player].pos.toVec3().xy(), 32.f, near_);
        int n = 0;
        for (int id : near_) {
            Ped& c = g.peds[id];
            if (c.faction != FAC_POLICE || c.state != PS_ONFOOT || c.health <= 0.f) continue;
            if (c.brain.type != BRAIN_COMBAT && c.brain.type != BRAIN_ARREST) continue;
            for (int k = 0; k < kCarsPerTrain && n < 3; k++) {
                int s2 = freeCarSeat(g, t.cars[k]);
                if (s2 < 0) continue;
                g.warpPedIntoVehicle(id, t.cars[k], s2);
                c.brain.type = BRAIN_COMBAT;
                c.brain.target = g.player;
                t.cops.push_back({id, c.uid});
                n++;
                break;
            }
        }
        if (n > 0) LOG("Transit: %d officers followed the player onto the train", n);
    }
    LOG("Transit: player boarded train %d/%d car %d seat %d at q %.0f", t.dir, t.idx, car, seat, t.st.q);
}

void alightPlayer(GameWorld& g, int ti) {
    Train& t = gS.trains[ti];
    Ped* pl = g.playerPed();
    int veh = pl->vehicle;
    int door = 0;
    float bd = 1e9f;
    for (int d = 0; d < 3; d++) {
        float dd = length(doorPoint(g, veh, d, 0.f) - pl->pos.toVec3());
        if (dd < bd) {
            bd = dd;
            door = d;
        }
    }
    alightPed(g, g.player, veh, door);
    pl->pendingAction = -1;
    g.rig.cut = true;
    if (gS.savedView >= 0) g.rig.vehicleView = gS.savedView;
    gS.savedView = -1;
    gS.rideTrain = -1;
    gS.alightings++;
    const World::MetroStation& st = stationOf(gS.prof[t.dir], t.st.stop);
    g.notify("SkyLine", st.name);
    // officers riding along get off with the player
    for (Rider& c : t.cops)
        if (pedValid(g, c.ped, c.uid) && g.peds[c.ped].vehicle >= 0) alightPed(g, c.ped, g.peds[c.ped].vehicle, (door + 1) % 3);
    t.cops.clear();
    LOG("Transit: player got off at %s", st.name.c_str());
}

void skipToNextStation(GameWorld& g) {
    if (gS.rideTrain < 0) return;
    Train& t = gS.trains[gS.rideTrain];
    float dt = t.st.toNext + 3.4f;
    gS.clockOffset += dt;
    if (g.env) {
        g.env->timeOfDay += dt / 120.f;   // 1 game minute = 2 real seconds
        if (g.env->timeOfDay >= 24.f) {
            g.env->timeOfDay -= 24.f;
            g.gameDay++;
        }
        g.env->gameSeconds += dt;
    }
    g.rig.cut = true;
    LOG("Transit: skipped %.0f s to %s", dt, stationOf(gS.prof[t.dir], t.st.next).name.c_str());
}

void playerLogic(GameWorld& g, float dt) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    int pv = pl->state == PS_INVEHICLE ? pl->vehicle : -1;
    int ti = trainOfVehicle(pv);
    gS.rideTrain = ti;
    if (ti < 0 && gS.savedView >= 0) {   // left the train some other way (warped out, respawned)
        g.rig.vehicleView = gS.savedView;
        gS.savedView = -1;
    }
    // ---- skip fade sequence
    if (gS.skipStage == 1 && g.fadedOut()) {
        skipToNextStation(g);
        gS.skipStage = 2;
        g.fadeIn(1.2f);
        return;
    }
    if (gS.skipStage == 2 && g.fadeAlpha <= 0.02f) gS.skipStage = 0;
    if (ti >= 0) {
        Train& t = gS.trains[ti];
        gS.boardTrain = -1;
        bool exitReq = g.ctl.enter.pressed || g.ctl.special.pressed;
        g.ctl.enter.pressed = g.ctl.special.pressed = false;   // the train handles its own doors
        if (exitReq && g.playerControl) {
            if (t.st.stop >= 0 && t.st.doors > 0.5f) {
                alightPlayer(g, ti);
                return;
            }
            g.help("The doors are closed. Wait for the next station, or hold ~i:SPACE|A~ to skip there.", 4.f);
        }
        if (g.ctl.skip.down && gS.skipStage == 0 && g.playerControl && g.pinfo.wanted == 0) {
            gS.skipHold += dt;
            if (gS.skipHold > 0.8f && t.st.toNext > 6.f) {
                gS.skipStage = 1;
                gS.skipHold = 0.f;
                g.fadeOut(2.2f);
                g.subtitle("SkyLine", "Skipping to " + stationOf(gS.prof[t.dir], t.st.next).name + "...", 2.f, 0xff4ad8e0);
            }
        } else {
            gS.skipHold = 0.f;
        }
        if (g.ctl.skip.down && g.pinfo.wanted > 0 && gS.skipStage == 0) g.help("You can't skip ahead while wanted.", 2.f);
        g.ctl.handbrake = Button();
        return;
    }
    // ---- boarding walk: auto-steer to the door, then take a seat
    if (gS.boardTrain >= 0) {
        Train* t = gS.boardTrain < (int)gS.trains.size() ? &gS.trains[gS.boardTrain] : nullptr;
        gS.boardTimer += dt;
        bool ok = t && t->materialized() && t->st.stop >= 0 && t->st.doors > 0.55f && pl->state == PS_ONFOOT && g.playerControl;
        if (!ok) {
            if (t && t->st.doors <= 0.55f) g.help("The doors closed. Wait for the next train.", 3.f);
            gS.boardTrain = -1;
        } else {
            vec2 p2 = pl->pos.toVec3().xy();
            if (length(p2 - gS.boardPoint) < 0.55f || gS.boardTimer > 2.6f) {
                int bt = gS.boardTrain;
                gS.boardTrain = -1;
                boardPlayer(g, bt, gS.boardCar);
            } else {
                steerPlayer(g, gS.boardPoint, 0.45f);
            }
            return;
        }
    }
    if (pl->state != PS_ONFOOT) return;
    // ---- near an open door: offer to board
    int bt = -1, bc = 0, bd = 0;
    vec3 pp = pl->pos.toVec3();
    if (nearestOpenDoor(g, pp.xy(), pp.z, 3.2f, bt, bc, bd) >= 0) {
        if (g.hudHelpTimer <= 0.2f) g.help(StrFormat("Press ~i:F|Y~ to board the SkyLine ($%d).", kFare), 0.5f);
        if ((g.ctl.enter.pressed || g.ctl.special.pressed) && g.playerControl) {
            g.ctl.enter.pressed = g.ctl.special.pressed = false;
            if (g.pinfo.money < kFare) {
                g.help(StrFormat("You need $%d for a SkyLine ticket.", kFare), 3.f);
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_UI_ERROR, 0.5f);
#endif
            } else {
                gS.boardTrain = bt;
                gS.boardCar = bc;
                gS.boardTimer = 0.f;
                gS.boardPoint = doorPoint(g, gS.trains[bt].cars[bc], bd, 0.25f).xy();
            }
        }
        return;
    }
    // ---- on a platform: timetable hint
    gS.hintTimer -= dt;
    const World::MetroLine& L = World::gTransit->metro;
    for (int si = 0; si < (int)L.stations.size(); si++) {
        const World::MetroStation& st = L.stations[si];
        vec2 d = pp.xy() - st.pos;
        if (fabsf(dot(d, st.dir)) > kPlatformHalfLen || fabsf(dot(d, st.right())) > 7.5f || fabsf(pp.z - st.platformZ()) > 1.5f) continue;
        if (gS.hintTimer > 0.f && gS.lastHintStation == si) break;
        gS.hintTimer = 25.f;
        gS.lastHintStation = si;
        // next arrival per direction at this station
        float next[2] = {1e9f, 1e9f};
        for (const Train& t : gS.trains) {
            const Profile& P = gS.prof[t.dir];
            int k = 0;
            for (; k < (int)P.stIdx.size(); k++)
                if (P.stIdx[k] == si) break;
            float tau = fmodf(nowClock(g) + t.phase, P.cycle);
            float wait = P.arr[k] - tau;
            if (wait < -(P.dep[k] - P.arr[k])) wait += P.cycle;
            next[t.dir] = Min(next[t.dir], Max(wait, 0.f));
        }
        auto fmt = [](float s) { return s < 1.f ? std::string("boarding") : (s < 60.f ? StrFormat("%d s", (int)s) : StrFormat("%d min", (int)(s / 60.f + 0.5f))); };
        g.help(StrFormat("SkyLine %s. Loop A (%s): %s. Loop B (%s): %s. Board with ~i:F|Y~ when the doors open ($%d).", st.name.c_str(),
                         L.stations[(si + 1) % L.stations.size()].name.c_str(), fmt(next[0]).c_str(),
                         L.stations[(si + L.stations.size() - 1) % L.stations.size()].name.c_str(), fmt(next[1]).c_str(), kFare),
               7.f);
        break;
    }
}

// ---------------------------------------------------------------------------------------------------- blips
void ensureBlips(GameWorld& g, float dt) {
    gS.blipTimer -= dt;
    if (gS.blipTimer > 0.f) return;
    gS.blipTimer = 2.f;
    const World::MetroLine& L = World::gTransit->metro;
    if (gS.blipNames.empty())
        for (const World::MetroStation& st : L.stations) gS.blipNames.push_back(st.name + " (SkyLine)");
    bool present = false;
    for (const UI::Blip& b : g.staticBlips)
        if (!gS.blipNames.empty() && b.label == gS.blipNames[0].c_str()) present = true;
    if (present) return;
    for (size_t i = 0; i < L.stations.size(); i++) {
        UI::Blip b;
        b.pos = L.stations[i].pos;
        b.icon = UI::BLIP_METRO;
        b.color = 0;
        b.scale = 1.f;
        b.shortRange = true;
        b.label = gS.blipNames[i].c_str();
        g.staticBlips.push_back(b);
    }
}

// Transit lines on the map and radar (UI keeps a copy): bus routes under the SkyLine loop, dashed ferry crossings
void publishMapLines() {
#ifdef HAVE_GAME_UI
    const World::TransitNet& N = *World::gTransit;
    std::vector<UI::MapLine> lines;
    for (const World::BusRoute& R : N.busRoutes) {
        UI::MapLine ml;
        for (size_t k = 0; k < R.line.size(); k += 2) ml.pts.push_back(R.line[k]);
        vec4 c = unpackRGBA8(R.colorSrgb);
        ml.color = UI::rgba(c.x, c.y, c.z, 0.9f);
        ml.width = 2.f;
        ml.closed = true;
        ml.radar = false;
        ml.name = "Bus " + R.number + " " + R.name;
        lines.push_back(ml);
    }
    for (const World::FerryRoute& F : N.ferries)
        for (size_t i = 0; i < F.legs.size(); i++) {
            UI::MapLine ml;
            for (size_t k = 0; k < F.legs[i].size(); k += 3) ml.pts.push_back(F.legs[i][k]);
            ml.pts.push_back(F.legs[i].back());
            ml.color = UI::rgba(0.35f, 0.75f, 1.f, 0.95f);
            ml.width = 2.5f;
            ml.dashed = true;
            if (i == 0) ml.name = F.name;
            lines.push_back(ml);
        }
    {
        const World::MetroLine& L = N.metro;
        UI::MapLine ml;
        for (int k = 0; k < L.count(); k += 5) ml.pts.push_back(L.p[k]);
        ml.color = UI::rgba(0.1f, 0.85f, 0.92f, 1.f);
        ml.width = 4.f;
        ml.closed = true;
        ml.name = "SkyLine";
        lines.push_back(ml);
    }
    UI::setMapLines(lines);
    LOG("Transit: %zu lines published to the map", lines.size());
#endif
}

// ---------------------------------------------------------------------------------------------------- per frame
void updateTrains(GameWorld& g, float dt) {
    Ped* pl = g.playerPed();
    vec3 pp = pl ? pl->pos.toVec3() : g.rig.cam.pos.toVec3();
    vec3 listener = g.rig.cam.pos.toVec3();
    float clock = nowClock(g);
    int playerVeh = g.playerVehicle();
    for (int ti = 0; ti < (int)gS.trains.size(); ti++) {
        Train& t = gS.trains[ti];
        const Profile& P = gS.prof[t.dir];
        t.lastQ = t.materialized() ? t.st.q : -1.f;
        t.st = stateAt(P, clock + t.phase);
        vec3 c = trackPoint(P, t.st.q);
        float d = length(c.xy() - pp.xy());
        bool aboard = false;
        for (int k = 0; k < kCarsPerTrain; k++)
            if (t.cars[k] >= 0 && t.cars[k] == playerVeh) aboard = true;
        if (t.materialized() && !carsValid(g, t)) {
            // a car was removed by someone else: rebuild the train cleanly
            despawnTrain(g, t);
        }
        if (!t.materialized() && d < kMaterialize) spawnTrain(g, t);
        else if (t.materialized() && d > kDematerialize && !aboard) despawnTrain(g, t);
        if (!t.materialized()) continue;
        placeCars(g, t, dt);
        // seated riders while the player is close enough to see them
        float riderWant = (g.env && (g.env->timeOfDay < 5.5f || g.env->timeOfDay > 23.f)) ? 3.f : 7.f;
        if (d < kRiderRange && !g.populationOff) {
            if ((int)t.riders.size() < (int)riderWant && t.st.stop < 0) seatRiders(g, t, (int)riderWant);
        } else if (d > kRiderRange + 40.f && !aboard) {
            for (Rider& r : t.riders)
                if (pedValid(g, r.ped, r.uid)) g.despawnPed(r.ped);
            t.riders.clear();
        }
        for (size_t r = 0; r < t.riders.size();) {
            if (!pedValid(g, t.riders[r].ped, t.riders[r].uid) || g.peds[t.riders[r].ped].vehicle < 0) t.riders.erase(t.riders.begin() + r);
            else r++;
        }
        trainAudio(g, t, listener);
        stopEvents(g, ti, listener, aboard);
        // announcements inside the player's train
        if (aboard && t.st.stop < 0) {
            const World::MetroStation& nx = stationOf(P, t.st.next);
            if (t.annNextFor != t.st.next && t.st.toNext > 30.f) {
                t.annNextFor = t.st.next;
                speakPa(g, "Next stop: " + nx.name + ".", c, true, true);
            }
            if (t.annApproachFor != t.st.next && t.st.toNext < 20.f) {
                t.annApproachFor = t.st.next;
                speakPa(g, "Now approaching " + nx.name + ". Doors will open on the right.", c, true, true);
            }
        }
        // platform announcement for a player waiting at the next station
        if (!aboard && t.st.stop < 0 && t.st.toNext < 32.f && t.annPlatformFor != t.st.next && pl && pl->state == PS_ONFOOT) {
            const World::MetroStation& nx = stationOf(P, t.st.next);
            vec2 dd = pp.xy() - nx.pos;
            if (fabsf(dot(dd, nx.dir)) < kPlatformHalfLen + 4.f && fabsf(dot(dd, nx.right())) < 12.f && fabsf(pp.z - nx.platformZ()) < 3.f &&
                g.time - gS.lastStationPa > 8.0) {
                t.annPlatformFor = t.st.next;
                gS.lastStationPa = g.time;
                const World::MetroStation& after = stationOf(P, (t.st.next + 1) % (int)P.stIdx.size());
                speakPa(g, "The train approaching platform " + std::string(t.dir == 0 ? "A" : "B") + " is a Loop service to " + after.name +
                               ". Please stand behind the yellow line.",
                        nx.local(0.f, 0.f, nx.platformZ() + 3.5f), false, false);
            }
        }
        // a welcome committee: wanted riders find officers waiting on the next platform
        if (aboard && g.pinfo.wanted >= 2 && t.st.stop < 0 && t.st.toNext < 22.f && t.copsCalledFor != t.st.next && !g.chars.empty()) {
            t.copsCalledFor = t.st.next;
            const World::MetroStation& nx = stationOf(P, t.st.next);
            int side = t.dir == 0 ? 0 : 1;
            for (int k = 0; k < 2; k++) {
                vec3 sp = platformPoint(nx, side, (k == 0 ? -10.f : 12.f), 5.6f);
                int id = g.spawnPed(g.randomCivilianChar(hash32((u32)(g.time * 10.0) + k), 1), dvec3(sp), 0.f, FAC_POLICE);
                if (id < 0) continue;
                g.giveWeapon(id, WPN_PISTOL, 60);
                g.peds[id].weapon = WPN_PISTOL;
                g.peds[id].brain.type = BRAIN_COMBAT;
                g.peds[id].brain.target = g.player;
                g.peds[id].brain.accuracy = 0.3f;
            }
            LOG("Transit: officers waiting at %s", nx.name.c_str());
        }
    }
}

std::string upperCase(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)toupper((unsigned char)c);
    return o;
}

}  // namespace tg

namespace tb {   // city buses (transit_bus.cpp, included at the end of this file)
void update(GameWorld& g, float dt);
bool playerOnBus();
}  // namespace tb
namespace tf {   // bay ferries (transit_ferry.cpp, included at the end of this file)
void update(GameWorld& g, float dt);
void submit(GameWorld& g);
bool playerOnFerry();
}  // namespace tf
namespace tt {   // --autoplay metro | bus | ferry (transit_test.cpp, included at the end of this file)
void update(GameWorld& g, float dt);
}  // namespace tt

using namespace tg;

void update(GameWorld& g, float dt) {
    if (gS.failed || !World::gTransit || !World::gTransit->ready || World::gTransit->metro.stations.empty()) return;
    if (!gS.init) {
        gS.init = true;
        double t0 = TimeSeconds();
        for (int d = 0; d < 2; d++) buildProfile(gS.prof[d], d);
        gS.trains.clear();
        for (int d = 0; d < 2; d++)
            for (int i = 0; i < kTrainsPerDir; i++) {
                Train t;
                t.dir = d;
                t.idx = i;
                t.phase = gS.prof[d].cycle * ((float)i / kTrainsPerDir + (d == 0 ? 0.f : 0.37f / kTrainsPerDir));
                gS.trains.push_back(t);
            }
        LOG("Transit: SkyLine timetable built (%.1f ms): loop %.1f / %.1f min, %d trains per direction, headway %.1f min", (TimeSeconds() - t0) * 1000.0,
            gS.prof[0].cycle / 60.f, gS.prof[1].cycle / 60.f, kTrainsPerDir, gS.prof[0].cycle / kTrainsPerDir / 60.f);
        publishMapLines();
    }
    if (!ensureAssets(g)) return;
    Ped* pl = g.playerPed();
    if (!pl) return;
    double t0 = TimeSeconds();
    tt::update(g, dt);
    playerLogic(g, dt);
    updateTrains(g, dt);
    updateWalkers(g, dt);
    updateCrowds(g, dt, pl->pos.toVec3());
    ensureBlips(g, dt);
    tb::update(g, dt);
    tf::update(g, dt);
    double ms = (TimeSeconds() - t0) * 1000.0;
    static double acc = 0.0;
    static int frames = 0;
    acc += ms;
    if (++frames == 600) {
        int mat = 0;
        for (const Train& t : gS.trains) mat += t.materialized();
        LOG("Transit: %.3f ms/frame avg, %d trains materialized, %zu waiters, %zu walkers, boardings %d/%d riders %d/%d", acc / frames, mat,
            gS.waiters.size(), gS.walkers.size(), gS.boardings, gS.alightings, gS.riderBoard, gS.riderAlight);
        acc = 0.0;
        frames = 0;
    }
}

void submit(GameWorld& g) {
    tf::submit(g);
    if (!gS.init || gS.assetCab < 0 || !g.renderer || !g.renderer->dynamic) return;
    Render::DynamicRenderer* dyn = g.renderer->dynamic;
    dvec3 cam = g.rig.cam.pos;
    vec3 camF = g.rig.cam.forward();
    bool night = g.env && (g.env->timeOfDay < 6.8f || g.env->timeOfDay > 19.2f);
    int lights = 0;
    for (const Train& t : gS.trains) {
        const Profile& P = gS.prof[t.dir];
        if (t.materialized()) {
            for (int c = 0; c < kCarsPerTrain; c++) {
                int vi = t.cars[c];
                if (vi < 0 || !g.vehicles[vi].used) continue;
                const Vehicle& v = g.vehicles[vi];
                float dist = v.visibleDist;
                // sliding door leaves (the bodies at LOD0/1 have open door frames)
                if (gS.doorLeaf && dist < 125.f) {
                    mat3 R = v.sim.body.rotMat();
                    float open = t.st.stop >= 0 ? t.st.doors : 0.f;
                    float platformSx = c == kCarsPerTrain - 1 ? -1.f : 1.f;
                    for (int sd = -1; sd <= 1; sd += 2) {
                        float o = (float)sd == platformSx ? open : 0.f;
                        float out = 0.03f + 0.05f * Saturate(o * 4.f);
                        for (int d = 0; d < 3; d++)
                            for (int l = -1; l <= 1; l += 2) {
                                vec3 local(sd * (1.43f + out), kDoorY[d] + l * (0.35f + o * 0.68f), 0.f);
                                Render::DrawItem di;
                                di.model = gS.doorLeaf;
                                di.pos = v.sim.body.pos + R * local;
                                di.rot = sd > 0 ? R : R * mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), kPi));
                                di.tint0 = vec4(v.color0, v.dirt);
                                di.tint1 = vec4(v.color1, 0.f);
                                di.id = 0x900000000ull | ((u64)v.uid << 4) | (u64)((d * 2 + (l > 0)) * 2 + (sd > 0));
                                di.castShadow = dist < 60.f;
                                di.drawGlass = !v.windowsBroken;
                                dyn->submit(di);
                            }
                    }
                }
                // head lamps of the leading cab, interior glow at night
                if (lights < 10 && dist < 320.f) {
                    mat3 R = v.sim.body.rotMat();
                    if (c == 0) {
                        Render::DynamicLight hl;
                        hl.pos = v.sim.body.pos + R * vec3(0.f, 9.4f, 1.3f);
                        hl.dir = normalize(R * vec3(0.f, 1.f, -0.06f));
                        hl.color = vec3(1.f, 0.95f, 0.86f) * (night ? 7000.f : 1500.f);
                        hl.radius = 60.f;
                        hl.spotCos = cosf(22.f * kDegToRad);
                        hl.spotInner = cosf(12.f * kDegToRad);
                        g.renderer->addLight(hl);
                        lights++;
                    }
                    if (night && dist < 140.f) {
                        Render::DynamicLight il;
                        il.pos = v.sim.body.pos + R * vec3(0.f, 0.f, 2.6f);
                        il.color = vec3(0.92f, 0.97f, 1.f) * 260.f;
                        il.radius = 9.f;
                        g.renderer->addLight(il);
                        lights++;
                    }
                }
            }
            continue;
        }
        // far trains: plain shells straight from the timetable
        vec3 c = trackPoint(P, t.st.q);
        vec3 toC = rel(dvec3(c), cam);
        float dist = length(toC);
        if (dist > kFarDraw || (dot(toC, camF) < -40.f && dist > 40.f)) continue;
        for (int k = 0; k < kCarsPerTrain; k++) {
            CarPose cp = carPose(P, t.st.q, k);
            const VehicleAsset& a = g.vassets[k == 1 ? gS.assetMid : gS.assetCab];
            Render::DrawItem di;
            di.model = a.bodyLod[1] ? a.bodyLod[1] : a.body;
            di.pos = cp.pos;
            di.rot = mat3FromQuat(cp.rot);
            di.tint0 = vec4(a.spec.liveryPrimary, 0.1f);
            di.tint1 = vec4(a.spec.liverySecondary, 0.f);
            di.lightBits = k == kCarsPerTrain - 1 ? 1u : (k == 0 ? 4u : 0u);
            di.id = 0xA00000000ull | (u64)((t.dir * 16 + t.idx) * 4 + k);
            di.castShadow = dist < 400.f;
            dyn->submit(di);
        }
    }
}

// For tests and the HUD: true while the player rides the SkyLine or a city bus
bool playerRiding() { return gS.rideTrain >= 0 || tb::playerOnBus() || tf::playerOnFerry(); }

}  // namespace Transit
}  // namespace Game

#include "transit_bus.cpp"
#include "transit_ferry.cpp"
#include "transit_test.cpp"
