// Sol Beach Streetcar: trams on the one-way loop laid out by world/transit_tram.cpp. Far trams run on a simple kinematic
// timetable (line speed, curve limits, stops); within ~360 m of the player a tram becomes three scripted vehicles (front
// cab module, centre module, rear module) placed on the rails by their bogie pivots, and drives like a tram: it obeys the
// signals and stop signs of the lane graph, gives way at minor junctions, never blocks a junction box, brakes for
// anything on the rails (ringing its bell), and serves every stop with a dwell, doors, a chime and announcements. Waiting
// people board, riders get off; the player rides for a fare, skips to the next stop or gets off at any stop; police
// on the player's heels follow aboard. Included by transit_game.cpp.

namespace Game {
namespace Transit {
namespace tr {

using namespace tg;
namespace td = World::tram_dims;

constexpr float kTramMat = 360.f, kTramDemat = 450.f, kTramDraw = 1600.f;
constexpr float kLineSpeed = 11.5f;          // m/s (41 km/h)
constexpr float kTAccel = 1.0f, kTDecel = 1.15f, kTHardDecel = 2.8f;
constexpr float kCurveLat = 0.9f;            // lateral comfort in curves (m/s^2)
constexpr float kStopDwell = 15.f;
constexpr int kTramFare = 2;
const float kPivotInset[3] = {1.9f, 1.3f, 1.9f};

struct TWalker {
    int ped = -1;
    u32 uid = 0;
    int mode = 0;          // 0 walk away from the stop, 1 board a tram (despawn at the door)
    vec2 target;
    float timer = 0.f;
};

struct Tram {
    int idx = 0;
    float s = 0.f;         // track position of the front
    float v = 0.f, a = 0.f;
    int next = 0;          // next stop
    int phase = 0;         // 0 running, 1 at a stop
    float dwell = 0.f;
    float doors = 0.f;     // 0 shut .. 1 open
    int sec[3] = {-1, -1, -1};
    u32 secUid[3] = {0, 0, 0};
    int driver = -1;
    u32 driverUid = 0;
    std::vector<Rider> cops;
    float bell = 0.f;
    int dings = 0;         // more dings to come
    float dingT = 0.f;
    float held = 0.f;      // seconds held by something on the rails
    int annFor = -1;       // "next stop" given for this stop
    int arrFor = -1;       // arrival events run for this stop
    int lineJ = -1;        // junction whose stop line the tram waits at
    float lineStop = 0.f;  // time stopped at that line (stop signs)
    int passedJ = -1;      // last junction committed to
    Audio::EmitterHandle sndRoll = 0, sndMotor = 0;
    bool materialized() const { return sec[0] >= 0; }
};

struct TState {
    bool init = false, failed = false;
    std::vector<Tram> trams;
    int asset[3] = {-1, -1, -1};
    Render::Model* doorLeaf = nullptr;
    std::vector<float> vLim;     // braking-aware curve speed limit per track sample (for the front)
    std::vector<int> jConn;      // lane graph connector per junction (signals), -1 unknown
    int ride = -1;               // tram the player rides
    int paidTram = -1;
    int skipStage = 0;
    float skipHold = 0.f;
    float hintTimer = 0.f;
    int hintStop = -1;
    std::vector<TWalker> walkers;
    std::vector<Waiter> waiters;   // station = stop index
    float crowdTimer = 0.f;
    std::vector<std::string> blipNames;
    float blipTimer = 0.f;
    int savedView = -1;
    int boardings = 0, stopsServed = 0, riderOn = 0, riderOff = 0, bells = 0;
};
TState gT2;

const World::TramLine& line() { return World::gTransit->trams[0]; }

bool tramValid(const GameWorld& g, const Tram& t) {
    for (int k = 0; k < 3; k++) {
        int vi = t.sec[k];
        if (vi < 0 || vi >= (int)g.vehicles.size() || !g.vehicles[vi].used || g.vehicles[vi].uid != t.secUid[k]) return false;
    }
    return true;
}

int tramOfVehicle(int veh) {
    if (veh < 0) return -1;
    for (int i = 0; i < (int)gT2.trams.size(); i++)
        for (int k = 0; k < 3; k++)
            if (gT2.trams[i].sec[k] == veh) return i;
    return -1;
}

// front end of module k (track position)
float moduleFront(const Tram& t, int k) {
    float s = t.s;
    for (int i = 0; i < k; i++) s -= td::kSectionLen[i] + td::kSectionGap;
    return s;
}

float brakeSpeed(float d, float decel) { return sqrtf(2.f * decel * Max(d, 0.f)); }

// ---------------------------------------------------------------------------------------------------------------- setup
void buildProfile() {
    const World::TramLine& L = line();
    int n = (int)L.p.size();
    std::vector<float> curve(n, kLineSpeed);
    for (int i = 0; i < n; i++) {
        vec2 a = L.p[(i + n - 3) % n].xy(), b = L.p[i].xy(), c = L.p[(i + 3) % n].xy();
        vec2 u = b - a, w = c - b;
        float lu = length(u), lw = length(w);
        if (lu < 1e-3f || lw < 1e-3f) continue;
        float ang = fabsf(atan2f(cross(u, w), dot(u, w)));
        float k = ang / (0.5f * (lu + lw));
        if (k > 1e-4f) curve[i] = Min(kLineSpeed, Max(2.5f, sqrtf(kCurveLat / k)));
    }
    // a curve limits the whole tram until its rear has left it: the front's limit looks back one tram length
    int back = (int)ceilf(td::kLength / L.ds);
    std::vector<float> lim(n, kLineSpeed);
    for (int i = 0; i < n; i++) {
        float m = kLineSpeed;
        for (int k = 0; k <= back; k++) m = Min(m, curve[(i - k + n * 4) % n]);
        lim[i] = m;
    }
    // braking into the limits (backward pass, twice around the loop)
    for (int pass = 0; pass < 2; pass++)
        for (int i = n - 1; i >= 0; i--) {
            int j = (i + 1) % n;
            lim[i] = Min(lim[i], sqrtf(lim[j] * lim[j] + 2.f * kTDecel * L.ds));
        }
    gT2.vLim = lim;
}

float vLimAt(float s) {
    const World::TramLine& L = line();
    int n = (int)gT2.vLim.size();
    if (n == 0) return kLineSpeed;
    int i = (int)(L.wrap(s) / L.ds) % n;
    return gT2.vLim[i];
}

int findConn(const AI::LaneGraph& G, const World::TramJunction& J) {
    int grp = J.fromEdge * 2 + (J.fromDir < 0 ? 1 : 0);
    if (grp < 0 || grp >= (int)G.groupFirst.size() || G.groupFirst[grp] < 0) return -1;
    int first = G.groupFirst[grp], cnt = G.groupCount[grp];
    for (int k = cnt - 1; k >= 0; k--)
        for (int c : G.lanes[first + k].out) {
            const AI::Connector& C = G.conns[c];
            if (G.lanes[C.to].edge == J.toEdge && G.lanes[C.to].dir == J.toDir) return c;
        }
    return -1;
}

bool ensureAssets(GameWorld& g) {
    if (gT2.asset[0] >= 0) return true;
    Render::DynamicRenderer* dyn = g.renderer ? g.renderer->dynamic : nullptr;
    if (!dyn) return false;
    for (int k = 0; k < 3; k++) {
        VehicleAsset a;
        TransitModels::tramSpec(k, a.spec);
        MeshData lod[3];
        for (int l = 0; l < 3; l++) TransitModels::buildTram(k, l, lod[l]);
        a.body = dyn->createModel(lod[0]);
        a.bodyLod[0] = dyn->createModel(lod[1]);
        a.bodyLod[1] = dyn->createModel(lod[2]);
        g.vassets.push_back(a);
        gT2.asset[k] = (int)g.vassets.size() - 1;
    }
    for (Vehicle& v : g.vehicles)
        if (v.used && v.model >= 0 && v.model < (int)g.vassets.size()) v.sim.model = &g.vassets[v.model].spec;
    MeshData leaf;
    TransitModels::buildTramDoorLeaf(leaf);
    gT2.doorLeaf = dyn->createModel(leaf);
    LOG("Transit: streetcar modules registered as vehicle assets %d %d %d", gT2.asset[0], gT2.asset[1], gT2.asset[2]);
    return true;
}

// Stops keep the curb lane clear of parked cars (lane bus stops) and give waiting people a place (scenario spots)
void registerStops(GameWorld& g) {
    AI::LaneGraph& G = g.laneGraph;
    const World::TramLine& L = line();
    int added = 0;
    for (const World::TramStop& st : L.stops) {
        int grp = st.edge * 2 + (st.dir < 0 ? 1 : 0);
        if (grp < 0 || grp >= (int)G.groupFirst.size() || G.groupFirst[grp] < 0) continue;
        int lane = G.groupFirst[grp] + G.groupCount[grp] - 1;
        AI::Lane& Ln = G.lanes[lane];
        float u = st.u - td::kLength * 0.5f;   // middle of the platform
        if (u > Ln.u0 + 4.f && u < Ln.u1 - 6.f) {
            Ln.busStops.insert(std::upper_bound(Ln.busStops.begin(), Ln.busStops.end(), u), u);
            added++;
        }
    }
    LOG("Transit: %zu streetcar stops registered (%d curb lanes kept clear)", L.stops.size(), added);
}

void initTrams(GameWorld& g) {
    const World::TramLine& L = line();
    gT2.trams.clear();
    for (int i = 0; i < L.trams; i++) {
        Tram t;
        t.idx = i;
        t.s = L.wrap(L.length * (float)i / L.trams + 40.f);
        t.next = 0;
        for (int k = 0; k < (int)L.stops.size(); k++)
            if (L.ahead(t.s, L.stops[k].s) < L.ahead(t.s, L.stops[t.next].s)) t.next = k;
        t.v = kLineSpeed * 0.6f;
        gT2.trams.push_back(t);
    }
    gT2.jConn.assign(L.junctions.size(), -1);
    for (size_t j = 0; j < L.junctions.size(); j++) gT2.jConn[j] = findConn(g.laneGraph, L.junctions[j]);
}

// ---------------------------------------------------------------------------------------------------------------- vehicles
void modulePose(const Tram& t, int k, dvec3& pos, quat& rot, vec3& fwd) {
    const World::TramLine& L = line();
    float sf = moduleFront(t, k), len = td::kSectionLen[k];
    vec3 pf = L.at(sf - kPivotInset[k]), pr = L.at(sf - len + kPivotInset[k]);
    fwd = pf - pr;
    fwd = length2(fwd) > 1e-6f ? normalize(fwd) : vec3(L.dirAt(sf), 0.f);
    vec3 up(0, 0, 1);
    vec3 X = normalize(cross(fwd, up));
    vec3 Z = cross(X, fwd);
    pos = dvec3((pf + pr) * 0.5f);
    rot = normalize(quatFromMat3(mat3(X, fwd, Z)));
}

void placeTram(GameWorld& g, Tram& t) {
    float tod = g.env ? g.env->timeOfDay : 12.f;
    bool dark = tod > 18.9f || tod < 6.9f;
    for (int k = 0; k < 3; k++) {
        Vehicle& v = g.vehicles[t.sec[k]];
        dvec3 pos;
        quat rot;
        vec3 fwd;
        modulePose(t, k, pos, rot, fwd);
        v.sim.body.pos = pos;
        v.sim.body.rot = rot;
        v.sim.body.vel = fwd * t.v;
        v.sim.body.angVel = vec3(0.f);
        v.sim.sleeping = true;
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
        v.sim.engineOn = true;
        v.scripted = true;
        v.persistent = true;
        v.locked = true;
        v.renderFar = true;
        v.alarm = false;
        v.hornOn = false;
        v.lightsOn = dark || k == 0;   // day running lamps up front
        v.sim.gear = 1;
        v.ctl = Vehicles::VehicleControls();
        v.ctl.brake = t.a < -0.25f || t.v < 0.05f ? 1.f : 0.f;
        v.indicator = 0;
    }
}

void spawnTram(GameWorld& g, Tram& t) {
    for (int k = 0; k < 3; k++) {
        dvec3 pos;
        quat rot;
        vec3 fwd;
        modulePose(t, k, pos, rot, fwd);
        int vi = g.spawnVehicle(gT2.asset[k], pos, atan2f(-fwd.x, fwd.y), false);
        if (vi < 0) {
            for (int j = 0; j < k; j++)
                if (t.sec[j] >= 0) g.despawnVehicle(t.sec[j], true);
            for (int j = 0; j < 3; j++) t.sec[j] = -1;
            return;
        }
        Vehicle& v = g.vehicles[vi];
        v.scripted = true;
        v.persistent = true;
        v.locked = true;
        v.renderFar = true;
        v.dirt = 0.04f + 0.08f * hashToFloat(hash32(v.uid));
        t.sec[k] = vi;
        t.secUid[k] = v.uid;
    }
    if (!g.chars.empty()) {
        int pid = g.spawnPed(g.randomCivilianChar(hash32(0x7A11u + (u32)t.idx * 31u) | 1u, 0), g.vehicles[t.sec[0]].sim.body.pos, 0.f, FAC_CIVILIAN);
        if (pid >= 0) {
            g.peds[pid].persistent = true;
            g.peds[pid].invincible = true;
            g.peds[pid].brain.type = BRAIN_NONE;
            g.warpPedIntoVehicle(pid, t.sec[0], 0);
            t.driver = pid;
            t.driverUid = g.peds[pid].uid;
        }
    }
    t.held = 0.f;
    t.lineJ = -1;
    placeTram(g, t);
    // passengers aboard (a few in each module; the rear module keeps room for the player and company)
    if (!g.chars.empty() && !g.populationOff) {
        float tod = g.env ? g.env->timeOfDay : 12.f;
        float busy = tod > 23.f || tod < 6.f ? 0.3f : (tod > 7.f && tod < 20.f ? 1.f : 0.6f);
        const int cap[3] = {3, 2, 2};
        for (int k = 0; k < 3; k++) {
            int vi = t.sec[k];
            int want = (int)(cap[k] * busy * (0.4f + 0.6f * hashToFloat(hash32(t.secUid[k] * 7u + 11u))) + 0.5f);
            for (int n = 0; n < want; n++) {
                int seat = freeCarSeat(g, vi);
                if (seat < 0) break;
                int id = spawnCivilian(g, hash32(g.vehicles[vi].uid * 131u + (u32)seat * 7919u + (u32)n), g.vehicles[vi].sim.body.pos.toVec3(), 0.f);
                if (id < 0) break;
                g.warpPedIntoVehicle(id, vi, seat);
                g.peds[id].brain.type = BRAIN_PASSENGER;
            }
        }
    }
}

void despawnTram(GameWorld& g, Tram& t) {
    for (Rider& r : t.cops)
        if (pedValid(g, r.ped, r.uid) && g.peds[r.ped].vehicle >= 0) g.removePedFromVehicle(r.ped, false);
    t.cops.clear();
    // seated riders go with the tram
    for (int k = 0; k < 3; k++) {
        int vi = t.sec[k];
        if (vi < 0 || vi >= (int)g.vehicles.size() || !g.vehicles[vi].used || g.vehicles[vi].uid != t.secUid[k]) continue;
        for (int s = 1; s < 8; s++) {
            int pid = g.vehicles[vi].seats[s];
            if (pid >= 0 && pid < (int)g.peds.size() && !g.peds[pid].isPlayer) g.despawnPed(pid);
        }
    }
    if (pedValid(g, t.driver, t.driverUid)) g.despawnPed(t.driver);
    t.driver = -1;
    for (int k = 0; k < 3; k++) {
        int vi = t.sec[k];
        if (vi >= 0 && vi < (int)g.vehicles.size() && g.vehicles[vi].used && g.vehicles[vi].uid == t.secUid[k]) g.despawnVehicle(vi, true);
        t.sec[k] = -1;
    }
#ifdef HAVE_AUDIO
    if (t.sndRoll) Audio::destroyEmitter(t.sndRoll);
    if (t.sndMotor) Audio::destroyEmitter(t.sndMotor);
#endif
    t.sndRoll = t.sndMotor = 0;
}

// Door point on the curb side of module k, door d (out = metres outside the body)
vec3 tramDoorPoint(const GameWorld& g, int veh, int k, int d, float out) {
    std::vector<float> doors;
    TransitModels::trm::tDoorsOf(k, doors);
    const Vehicle& v = g.vehicles[veh];
    float y = d < (int)doors.size() ? doors[d] : 0.f;
    vec3 local(TransitModels::trm::kTW + out, y, TransitModels::trm::kTFloor + 0.1f);
    return v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, local);
}

// nearest open door to p: returns the module (-1 none), fills the door index and distance
int nearestDoor(const GameWorld& g, const Tram& t, vec2 p, int& door, float& dist) {
    int best = -1;
    dist = 1e9f;
    for (int k = 0; k < 3; k++) {
        std::vector<float> doors;
        TransitModels::trm::tDoorsOf(k, doors);
        for (int d = 0; d < (int)doors.size(); d++) {
            float dd = length(tramDoorPoint(g, t.sec[k], k, d, 0.5f).xy() - p);
            if (dd < dist) {
                dist = dd;
                best = k;
                door = d;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------------- audio
void speakTram(GameWorld& g, const std::string& line_, vec3 pos, bool inside) {
#ifdef HAVE_AUDIO
    Speech::Persona ann = Speech::persona("announcer_female");
    std::string txt = "[pa][calm]" + line_;
    if (inside) Audio::speak(txt.c_str(), ann.voice, 0.85f);
    else Audio::speakAt(txt.c_str(), ann.voice, pos, 0.9f);
#else
    (void)pos;
#endif
    if (g.settingsSubtitles && inside) g.subtitle("Streetcar", line_, 3.2f, 0xffff6a4d);
}

// the gong: a bright, short ding (the big bell pitched up), repeated after a beat for "ding ding"
void dingNow(GameWorld& g, Tram& t, float pitch) {
#ifdef HAVE_AUDIO
    vec3 p = g.vehicles[t.sec[0]].sim.body.pos.toVec3() + vec3(0, 0, 2.2f);
    Audio::play(Audio::SFX_BELL, p, 0.8f, pitch);
#else
    (void)g;
    (void)t;
    (void)pitch;
#endif
}
void ringBell(GameWorld& g, Tram& t, int times) {
    dingNow(g, t, 2.75f);
    t.dings = times - 1;
    t.dingT = 0.32f;
    t.bell = 4.5f;
    gT2.bells++;
}

void tramAudio(GameWorld& g, Tram& t, vec3 listener) {
#ifdef HAVE_AUDIO
    const Vehicle& mid = g.vehicles[t.sec[1]];
    vec3 c = mid.sim.body.pos.toVec3();
    float d = length(c - listener);
    if (d < 200.f) {
        if (!t.sndRoll) t.sndRoll = Audio::createEmitter(Audio::EMIT_WIND_RUSH);
        if (!t.sndMotor) t.sndMotor = Audio::createEmitter(Audio::EMIT_ENGINE);
        vec3 vel = mid.sim.body.vel;
        Audio::setEmitter(t.sndRoll, c + vec3(0, 0, 0.3f), vel, t.v * 1.4f, 0, 0, 0, Saturate(t.v / 5.f) * 0.55f);
        float thr = t.a > 0.05f ? Saturate(t.a * 1.2f) : (t.a < -0.05f ? 0.3f : 0.05f);
        Audio::setEmitter(t.sndMotor, c, vel, Saturate(t.v / 16.f) * 0.8f + 0.1f, thr, Saturate(fabsf(t.a)), (float)Audio::ENGINE_ELECTRIC,
                          t.v > 0.3f || fabsf(t.a) > 0.05f ? 0.6f : 0.2f);
    } else {
        if (t.sndRoll) Audio::destroyEmitter(t.sndRoll);
        if (t.sndMotor) Audio::destroyEmitter(t.sndMotor);
        t.sndRoll = t.sndMotor = 0;
    }
#else
    (void)g;
    (void)t;
    (void)listener;
#endif
}

std::string arrivalLine(const World::TramStop& st) {
    std::string extra;
    if (st.name == "Ferry Terminal") extra = " Change here for the Bay Ferry to Port Isle and Key Coral.";
    else if (st.name.find("Causeway") != std::string::npos) extra = " Change here for bus 9 to the Civic Center.";
    return st.name + "." + extra;
}

// ---------------------------------------------------------------------------------------------------------------- people
void startTWalker(GameWorld& g, int pid, int mode, vec2 target) {
    TWalker w;
    w.ped = pid;
    w.uid = g.peds[pid].uid;
    w.mode = mode;
    w.target = target;
    Ped& p = g.peds[pid];
    p.brain.type = BRAIN_GOTO;
    p.brain.speed = 1.3f + 0.25f * hashToFloat(hash32(p.uid));
    p.brain.goal = dvec3(target.x, target.y, p.pos.z);
    gT2.walkers.push_back(w);
}

void updateTWalkers(GameWorld& g, float dt) {
    for (size_t i = 0; i < gT2.walkers.size();) {
        TWalker& w = gT2.walkers[i];
        bool drop = !pedValid(g, w.ped, w.uid);
        Ped* pp = drop ? nullptr : &g.peds[w.ped];
        if (pp && (pp->state != PS_ONFOOT || pp->health <= 0.f || pp->brain.type != BRAIN_GOTO)) {
            if (pp->health > 0.f) pp->persistent = false;
            drop = true;
        }
        if (!drop) {
            w.timer += dt;
            float d = length(pp->pos.toVec3().xy() - w.target);
            if (d < (w.mode == 1 ? 0.9f : 1.5f) || w.timer > 20.f) {
                if (w.mode == 1) {
                    g.despawnPed(w.ped);   // stepped aboard
                    gT2.riderOn++;
                } else {
                    releaseToCity(g, w.ped);
                }
                drop = true;
            }
        }
        if (drop) {
            gT2.walkers[i] = gT2.walkers.back();
            gT2.walkers.pop_back();
            continue;
        }
        i++;
    }
}

// people waiting at the stops near the player (standing by the shelter, some on the bench)
void updateStopCrowds(GameWorld& g, float dt, vec3 pp) {
    const World::TramLine& L = line();
    for (size_t i = 0; i < gT2.waiters.size();) {
        Waiter& w = gT2.waiters[i];
        bool ok = pedValid(g, w.ped, w.uid) && g.peds[w.ped].health > 0.f && g.peds[w.ped].state == PS_ONFOOT;
        if (ok) {
            PedAI& pa = g.pedAI(w.ped);
            if (g.peds[w.ped].brain.type != BRAIN_WANDER || pa.activity != ACT_SCENARIO) ok = false;
            else pa.actTimer = Max(pa.actTimer, 60.f);
        }
        if (ok && length(L.stops[w.station].pos - pp.xy()) > 280.f) {
            g.despawnPed(w.ped);
            ok = false;
        }
        if (!ok) {
            if (pedValid(g, w.ped, w.uid)) g.peds[w.ped].persistent = false;
            gT2.waiters[i] = gT2.waiters.back();
            gT2.waiters.pop_back();
            continue;
        }
        i++;
    }
    gT2.crowdTimer -= dt;
    if (gT2.crowdTimer > 0.f || g.populationOff || g.chars.empty()) return;
    gT2.crowdTimer = 1.8f;
    float tod = g.env ? g.env->timeOfDay : 12.f;
    float busy = tod > 23.f || tod < 5.5f ? 0.25f : (tod > 7.f && tod < 20.f ? 1.f : 0.6f);
    for (int si = 0; si < (int)L.stops.size(); si++) {
        const World::TramStop& st = L.stops[si];
        if (length(st.pos - pp.xy()) > 200.f) continue;
        int have = 0;
        for (const Waiter& w : gT2.waiters) have += w.station == si;
        int want = (int)(busy * (2.f + 4.f * hashToFloat(hash32(st.seed)))) + 1;
        if (have >= want) continue;
        u32 h = hash32(st.seed * 31u + (u32)(g.time * 2.0) + (u32)have * 97u);
        // along the platform in front of the shelter, some on the bench
        bool bench = (h >> 9) % 4 == 0 && have < 2;
        vec2 spot = bench ? st.pos - st.face * 0.45f + st.along * ((hashToFloat(h) - 0.5f) * 2.4f)
                          : st.pos + st.face * (1.2f + hashToFloat(hash32(h)) * 1.3f) + st.along * ((hashToFloat(h) - 0.5f) * 16.f);
        vec3 sp(spot, st.z);
        if (g.inCameraView(sp + vec3(0, 0, 1.f), 1.f) && length(sp - g.rig.cam.pos.toVec3()) < 80.f && g.populationWarmup <= 0.f) continue;
        int id = spawnCivilian(g, h, sp, 0.f);
        if (id < 0) continue;
        Ped& p = g.peds[id];
        p.brain.type = BRAIN_WANDER;
        p.brain.edge = -1;
        PedAI& pa = g.pedAI(id);
        pa.activity = ACT_SCENARIO;
        pa.anchor = spot;
        // facing up the street toward the arriving tram, or the street
        vec2 look = bench ? st.face : normalize(-st.along * 0.8f + st.face * 0.6f);
        pa.anchorYaw = atan2f(-look.x, look.y) + (hashToFloat(hash32(h * 5u)) - 0.5f) * 0.6f;
        p.yaw = pa.anchorYaw;
        pa.stance = bench ? 6 : waitStance(h);   // seated on the bench, or waiting by the curb
        pa.clip = -1;
        pa.actTimer = 600.f;
        Waiter w;
        w.ped = id;
        w.uid = p.uid;
        w.station = si;
        w.spot = spot;
        gT2.waiters.push_back(w);
    }
}

// ---------------------------------------------------------------------------------------------------------------- driving
// What stands in the way along the rails ahead: returns the gap from the front to the nearest body (1e9 none)
float scanAhead(GameWorld& g, const Tram& t, float look, int* hitBody, bool* hitPed) {
    const World::TramLine& L = line();
    AI::TrafficCore& T = g.traffic;
    *hitBody = -1;
    *hitPed = false;
    if (T.bodies.empty()) return 1e9f;
    // samples along the track from the front (2 m)
    const int maxS = 48;
    vec3 smp[maxS];
    int ns = Clamp((int)(look / 2.f) + 1, 2, maxS);
    vec2 mn(1e9f), mx(-1e9f);
    for (int i = 0; i < ns; i++) {
        smp[i] = L.at(t.s + i * 2.f);
        mn = vmin(mn, smp[i].xy());
        mx = vmax(mx, smp[i].xy());
    }
    float best = 1e9f;
    int self[3] = {t.sec[0], t.sec[1], t.sec[2]};
    T.hash.query(T.bodies, mn - vec2(6.f), mx + vec2(6.f), [&](int bi) {
        const AI::Body& b = T.bodies[bi];
        if (b.kind == AI::BK_CAR && (b.host == self[0] || b.host == self[1] || b.host == self[2])) return;
        bool ped = b.kind == AI::BK_PED;
        // nearest sample segment
        float bestD = 1e9f, along = 0.f, zAt = 0.f;
        vec2 tan(0, 1);
        for (int i = 0; i + 1 < ns; i++) {
            vec2 a = smp[i].xy(), c = smp[i + 1].xy();
            float u;
            float d = distPointSegment2D(b.pos, a, c, &u);
            if (d < bestD) {
                bestD = d;
                along = (i + u) * 2.f;
                zAt = Lerp(smp[i].z, smp[i + 1].z, u);
                tan = normalize(c - a);
            }
        }
        if (bestD > 1e8f || fabsf(b.z - zAt) > 3.f) return;
        vec2 bf = length2(b.fwd) > 0.5f ? b.fwd : vec2(0, 1);
        vec2 nrm(tan.y, -tan.x);
        vec2 br(bf.y, -bf.x);
        float extN = b.halfLen * fabsf(dot(bf, nrm)) + b.halfWid * fabsf(dot(br, nrm));
        float extT = b.halfLen * fabsf(dot(bf, tan)) + b.halfWid * fabsf(dot(br, tan));
        float half = td::kHalfWidth + extN + (ped ? 0.55f : 0.3f);
        if (bestD > half) {
            // crossing in front: will it be on the rails when we get there?
            if (!ped && b.speed < 1.f) return;
            vec2 side = b.pos - (smp[0].xy() + tan * along);
            float lat = length(side);
            float vn = lat > 1e-3f ? -dot(b.vel, side / lat) : 0.f;
            if (vn < (ped ? 0.3f : 0.8f)) return;
            float te = (bestD - half) / vn, ta = along / Max(t.v, 2.f);
            if (te > ta + 1.5f || te > 4.f) return;
        }
        float gap = Max(along - extT, 0.f);
        if (along < 0.5f && !ped) return;   // beside the nose (a car alongside)
        if (gap < best) {
            best = gap;
            *hitBody = bi;
            *hitPed = ped;
        }
    });
    return best;
}

// May the tram's front cross junction j's stop line now?
bool junctionClear(GameWorld& g, Tram& t, int j, float dLine, float dt) {
    const World::TramLine& L = line();
    const World::TramJunction& J = L.junctions[j];
    const AI::LaneGraph& G = g.laneGraph;
    bool atLine = dLine < 3.f && t.v < 0.3f;
    // what the box looks like: vehicles inside the node area (other than ours), approaching ones
    auto boxBusy = [&]() {
        const World::RoadNode& nd = World::gRoads->nodes[J.node];
        float r = Max(nd.radius, 8.f) + 2.f;
        bool busy = false;
        AI::TrafficCore& T = g.traffic;
        T.hash.query(T.bodies, nd.p - vec2(r + 25.f), nd.p + vec2(r + 25.f), [&](int bi) {
            if (busy) return;
            const AI::Body& b = T.bodies[bi];
            if (b.kind != AI::BK_CAR || (b.flags & AI::BF_PARKED)) return;
            if (b.host == t.sec[0] || b.host == t.sec[1] || b.host == t.sec[2]) return;
            vec2 rel = b.pos - nd.p;
            float d = length(rel);
            if (d < r) busy = true;   // in the box
            else if (d < r + 25.f && b.speed > 2.f && dot(b.vel, -rel) > 0.6f * b.speed * d) {
                // heading into the box, arriving within ~3 s
                if ((d - r) / b.speed < 3.f) busy = true;
            }
        });
        return busy;
    };
    int ci = j < (int)gT2.jConn.size() ? gT2.jConn[j] : -1;
    AI::SignalState sig = AI::SIG_NONE;
    if (J.control == 2 && ci >= 0) {
        const AI::Connector& C = G.conns[ci];
        sig = G.movementSignal(C.node, C.approach, C.turn, g.traffic.time);
    }
    if (sig == AI::SIG_GREEN || sig == AI::SIG_ARROW) return true;
    if (sig == AI::SIG_AMBER) return dLine < t.v * t.v / (2.f * kTDecel) + 1.f;   // too close to stop: clear on amber
    if (sig == AI::SIG_RED) return false;
    // stop sign: full stop at the line, then go when the box is free
    if (J.control == 1 || J.minor) {
        if (J.control == 1) {
            if (!atLine) return false;
            t.lineStop += dt;
            if (t.lineStop < 1.2f) return false;
        } else if (dLine > 6.f) {
            return true;   // give way: decide at the line
        }
        return !boxBusy();
    }
    return true;
}

void arriveAt(GameWorld& g, Tram& t, bool playerAboard, vec3 listener) {
    const World::TramLine& L = line();
    const World::TramStop& st = L.stops[t.next];
    t.phase = 1;
    t.dwell = kStopDwell + hashToFloat(hash32((u32)t.idx * 131u + (u32)t.next + (u32)gT2.stopsServed)) * 6.f;
    t.v = 0.f;
    t.a = 0.f;
    gT2.stopsServed++;
    if (!t.materialized()) return;
    vec3 c = g.vehicles[t.sec[1]].sim.body.pos.toVec3();
    if (playerAboard) speakTram(g, arrivalLine(st), c, true);
    // riders get off at the doors and walk away along the sidewalk
    if (length(c - listener) < 120.f && !g.chars.empty() && !g.populationOff) {
        int n = (int)(hash32(t.secUid[0] + (u32)gT2.stopsServed) % 4u);
        for (int k = 0; k < n; k++) {
            int mod = k % 3;
            std::vector<float> doors;
            TransitModels::trm::tDoorsOf(mod, doors);
            vec3 dp = tramDoorPoint(g, t.sec[mod], mod, k % (int)doors.size(), 0.8f);
            dp.z = g.groundHeight(dp.x, dp.y, dp.z + 1.f);
            int id = spawnCivilian(g, hash32(t.secUid[1] * 17u + (u32)k + (u32)gT2.stopsServed * 5u), dp, atan2f(st.face.x, -st.face.y));
            if (id < 0) break;
            vec2 away = st.pos + st.along * ((k & 1) ? 14.f : -14.f) - st.face * 1.2f;
            startTWalker(g, id, 0, away);
            gT2.riderOff++;
        }
    }
}

void departFrom(GameWorld& g, Tram& t, bool playerAboard) {
    const World::TramLine& L = line();
    t.phase = 0;
    t.next = (t.next + 1) % (int)L.stops.size();
    t.annFor = -1;
    if (t.materialized()) {
        ringBell(g, t, 2);
        (void)playerAboard;
    }
}

void driveTram(GameWorld& g, Tram& t, float dt, bool playerAboard, vec3 listener) {
    const World::TramLine& L = line();
    const World::TramStop& st = L.stops[t.next];
    t.bell = Max(0.f, t.bell - dt);
    if (t.dings > 0) {
        t.dingT -= dt;
        if (t.dingT <= 0.f) {
            dingNow(g, t, 2.62f);
            t.dings--;
            t.dingT = 0.32f;
        }
    }
    if (t.phase == 1) {
        // doors and dwell
        t.dwell -= dt;
        bool wait = false;
        Ped* pl = g.playerPed();
        if (pl && pl->state == PS_ONFOOT) {
            int door;
            float dd;
            if (nearestDoor(g, t, pl->pos.toVec3().xy(), door, dd) >= 0 && dd < 2.5f) wait = true;   // someone at the door
        }
        for (const TWalker& w : gT2.walkers)
            if (w.mode == 1 && pedValid(g, w.ped, w.uid) && length(g.peds[w.ped].pos.toVec3().xy() - w.target) < 12.f) wait = true;
        bool closing = t.dwell < 2.5f && !(wait && t.dwell > -10.f);
        float wantDoors = closing ? 0.f : 1.f;
        float prevDoors = t.doors;
        t.doors = approach(t.doors, wantDoors, dt / 1.6f);
#ifdef HAVE_AUDIO
        vec3 c = g.vehicles[t.sec[1]].sim.body.pos.toVec3();
        if (prevDoors <= 0.001f && t.doors > 0.001f && length(c - listener) < 70.f) Audio::play(Audio::SFX_TRAIN_DOORS, c, 0.5f, 1.15f);
        if (prevDoors >= 0.999f && t.doors < 0.999f && length(c - listener) < 70.f) Audio::play(Audio::SFX_TRANSIT_CHIME, c + vec3(0, 0, 2.f), 0.45f, 1.2f);
#else
        (void)prevDoors;
        (void)listener;
#endif
        // the waiting crowd boards once the doors are open
        if (t.doors > 0.9f && t.arrFor != t.next) {
            t.arrFor = t.next;
            for (size_t i = 0; i < gT2.waiters.size();) {
                Waiter& w = gT2.waiters[i];
                if (w.station != t.next || !pedValid(g, w.ped, w.uid) || hash32(w.uid) % 5 == 0) {
                    i++;
                    continue;
                }
                int pid = w.ped;
                gT2.waiters[i] = gT2.waiters.back();
                gT2.waiters.pop_back();
                int door;
                float dd;
                int mod = nearestDoor(g, t, g.peds[pid].pos.toVec3().xy(), door, dd);
                if (mod < 0) continue;
                startTWalker(g, pid, 1, tramDoorPoint(g, t.sec[mod], mod, door, 0.15f).xy());
            }
        }
        if (t.dwell <= 0.f && t.doors <= 0.001f && !wait) departFrom(g, t, playerAboard);
        t.v = 0.f;
        t.a = 0.f;
        return;
    }
    // ---- running: the binding constraint of speed limit, curves, the next stop, signals and obstacles
    float vT = Min(kLineSpeed, vLimAt(t.s));
    float dStop = L.ahead(t.s, st.s);
    if (dStop > L.length - 30.f) dStop = 0.f;   // just past: treat as reached
    vT = Min(vT, brakeSpeed(dStop - 0.2f, kTDecel));
    // the next junction ahead that is not committed yet
    int jNext = -1;
    float dLine = 1e9f;
    for (int j = 0; j < (int)L.junctions.size(); j++) {
        float d = L.ahead(t.s, L.junctions[j].sIn - 1.6f);
        if (d > L.length - 5.f) continue;
        if (d < dLine) {
            dLine = d;
            jNext = j;
        }
    }
    bool holdLine = false;
    if (jNext >= 0 && dLine < 60.f && jNext != t.passedJ) {
        if (t.lineJ != jNext) {
            t.lineJ = jNext;
            t.lineStop = 0.f;
        }
        bool go = junctionClear(g, t, jNext, dLine, dt);
        // never enter unless the whole tram fits beyond the box
        int hb;
        bool hp;
        float look = dLine + (L.junctions[jNext].sOut - L.junctions[jNext].sIn) + td::kLength + 4.f;
        float gapBeyond = scanAhead(g, t, Min(look, 94.f), &hb, &hp);
        if (go && hb >= 0 && !hp && gapBeyond < look - 2.f && gapBeyond > dLine + 1.f) {
            const AI::Body& b = g.traffic.bodies[hb];
            if (b.speed < 2.f) go = false;
        }
        if (!go) {
            holdLine = true;
            vT = Min(vT, brakeSpeed(dLine - 0.3f, kTDecel));
            if (dLine < 1.2f) vT = 0.f;
        } else if (dLine < 1.5f) {
            t.passedJ = jNext;   // committed
        }
    }
    if (jNext >= 0 && dLine > 60.f && t.passedJ == jNext) t.passedJ = -1;
    // obstacles on the rails
    int hb = -1;
    bool hp = false;
    float look = Clamp(t.v * t.v / (2.f * kTDecel) + 14.f, 16.f, 90.f);
    float gap = scanAhead(g, t, look, &hb, &hp);
    if (gap < 1e8f) {
        vT = Min(vT, brakeSpeed(gap - 2.5f, kTDecel));
        if (gap < 3.f) vT = 0.f;
        // bell: people on the rails, or a car sitting in the way
        if (t.bell <= 0.f && gap < 30.f && (hp || (hb >= 0 && g.traffic.bodies[hb].speed < 1.f))) ringBell(g, t, hp ? 2 : 1);
    }
    t.held = (t.v < 0.3f && vT < 0.3f && !holdLine && dStop > 2.f) ? t.held + dt : 0.f;
    // a car left on the rails for a long time, out of sight: towed away
    if (t.held > 30.f && hb >= 0 && !hp) {
        const AI::Body& b = g.traffic.bodies[hb];
        int ov = b.host;
        if (ov >= 0 && ov < (int)g.vehicles.size() && g.vehicles[ov].used && !g.vehicles[ov].persistent && g.playerVehicle() != ov &&
            !g.inCameraView(g.vehicles[ov].sim.body.pos.toVec3() + vec3(0, 0, 0.8f), 3.f)) {
            LOG("Transit: streetcar %d blocked by vehicle %d for %.0f s, cleared", t.idx, ov, t.held);
            g.despawnVehicle(ov, true);
            t.held = 0.f;
        }
    }
    // integrate
    float acc = vT > t.v ? kTAccel : -(vT < t.v - 3.f || gap < 6.f ? kTHardDecel : kTDecel * 1.2f);
    float nv = approach(t.v, vT, fabsf(acc) * dt);
    t.a = (nv - t.v) / Max(dt, 1e-4f);
    t.v = nv;
    t.s = L.wrap(t.s + t.v * dt);
    // announcement for a rider: next stop, once under way
    if (playerAboard && t.annFor != t.next && dStop > 60.f && t.v > 3.f) {
        t.annFor = t.next;
        speakTram(g, "Next stop: " + st.name + ".", g.vehicles[t.sec[1]].sim.body.pos.toVec3(), true);
    }
    if (dStop < 0.6f && t.v < 0.25f) arriveAt(g, t, playerAboard, listener);
}

// Far trams: kinematic timetable (speed limits and stops only)
void advanceVirtual(Tram& t, float dt) {
    const World::TramLine& L = line();
    if (t.phase == 1) {
        t.dwell -= dt;
        t.doors = 0.f;
        if (t.dwell <= 0.f) {
            t.phase = 0;
            t.next = (t.next + 1) % (int)L.stops.size();
        }
        return;
    }
    const World::TramStop& st = L.stops[t.next];
    float dStop = L.ahead(t.s, st.s);
    if (dStop > L.length - 30.f) dStop = 0.f;
    float vT = Min(Min(kLineSpeed * 0.9f, vLimAt(t.s)), brakeSpeed(dStop - 0.2f, kTDecel));
    t.v = approach(t.v, vT, (vT > t.v ? kTAccel : kTDecel * 1.3f) * dt);
    t.s = L.wrap(t.s + t.v * dt);
    if (dStop < 0.6f && t.v < 0.3f) {
        t.phase = 1;
        t.v = 0.f;
        t.dwell = kStopDwell + 8.f;   // virtual trams also absorb the signal delays of a real run
    }
}

// ---------------------------------------------------------------------------------------------------------------- player
void boardTram(GameWorld& g, int ti) {
    Tram& t = gT2.trams[ti];
    int seat = freeCarSeat(g, t.sec[2]);
    int mod = 2;
    if (seat < 0) {
        for (int k = 1; k >= 0 && seat < 0; k--) {
            seat = freeCarSeat(g, t.sec[k]);
            mod = k;
        }
    }
    if (seat < 0) return;
    gT2.savedView = g.rig.vehicleView;
    g.warpPedIntoVehicle(g.player, t.sec[mod], seat);
    g.rig.cut = true;
    if (gT2.paidTram != ti) {
        g.pinfo.money = Max<long long>(0, g.pinfo.money - kTramFare);
        gT2.paidTram = ti;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PURCHASE, 0.3f);
#endif
    }
    gT2.ride = ti;
    gT2.boardings++;
    const World::TramLine& L = line();
    g.notify("Sol Beach Streetcar", StrFormat("Next stop %s. Fare $%d.", L.stops[t.next % (int)L.stops.size()].name.c_str(), kTramFare));
    g.help("Hold ~i:SPACE|A~ to skip to the next stop. Press ~i:F|Y~ at a stop to get off.", 7.f);
    // officers on the player's heels board too
    if (g.pinfo.wanted > 0) {
        std::vector<int> near_;
        g.pedsNear(g.peds[g.player].pos.toVec3().xy(), 30.f, near_);
        int n = 0;
        for (int id : near_) {
            Ped& c = g.peds[id];
            if (c.faction != FAC_POLICE || c.state != PS_ONFOOT || c.health <= 0.f || n >= 3) continue;
            if (c.brain.type != BRAIN_COMBAT && c.brain.type != BRAIN_ARREST) continue;
            int m2 = (int)(hash32(c.uid) % 3u);
            int s2 = freeCarSeat(g, t.sec[m2]);
            if (s2 < 0) continue;
            g.warpPedIntoVehicle(id, t.sec[m2], s2);
            c.brain.type = BRAIN_COMBAT;
            c.brain.target = g.player;
            t.cops.push_back({id, c.uid});
            n++;
        }
    }
    LOG("Transit: player boarded streetcar %d (module %d seat %d)", ti, mod, seat);
}

void alightPlayer(GameWorld& g, Tram& t) {
    Ped* pl = g.playerPed();
    int mod = -1;
    for (int k = 0; k < 3; k++)
        if (t.sec[k] == pl->vehicle) mod = k;
    if (mod < 0) mod = 2;
    g.removePedFromVehicle(g.player, false);
    const World::TramLine& L = line();
    const World::TramStop& st = L.stops[t.next];
    vec3 dp = tramDoorPoint(g, t.sec[mod], mod, 0, 1.1f);
    dp.z = g.groundHeight(dp.x, dp.y, dp.z + 1.2f);
    pl->pos = dvec3(dp);
    pl->yaw = atan2f(st.face.x, -st.face.y);
    pl->vel = vec3(0.f);
    pl->grounded = true;
    pl->pendingAction = -1;
    g.rig.cut = true;
    if (gT2.savedView >= 0) g.rig.vehicleView = gT2.savedView;
    gT2.savedView = -1;
    gT2.ride = -1;
    gT2.paidTram = -1;
    g.notify("Sol Beach Streetcar", st.name);
    for (Rider& c : t.cops)
        if (pedValid(g, c.ped, c.uid) && g.peds[c.ped].vehicle >= 0) {
            g.removePedFromVehicle(c.ped, false);
            vec3 cp = dp + vec3(st.along * 1.5f, 0.f);
            g.peds[c.ped].pos = dvec3(cp);
        }
    t.cops.clear();
    LOG("Transit: player got off the streetcar at %s", st.name.c_str());
}

void skipToNext(GameWorld& g, Tram& t) {
    const World::TramLine& L = line();
    const World::TramStop& st = L.stops[t.next];
    float dist = L.ahead(t.s, st.s);
    t.s = st.s;
    t.v = 0.f;
    t.passedJ = -1;
    t.lineJ = -1;
    // clear the stop of traffic in the way
    for (int k = 0; k < 3; k++) {
        vec3 c = L.at(st.s - 5.f - 9.f * k);
        std::vector<int> near_;
        g.vehiclesNear(c.xy(), 7.f, near_);
        for (int o : near_)
            if (o != t.sec[0] && o != t.sec[1] && o != t.sec[2] && !g.vehicles[o].persistent && g.playerVehicle() != o) g.despawnVehicle(o, true);
    }
    placeTram(g, t);
    arriveAt(g, t, true, g.rig.cam.pos.toVec3());
    float dtSkip = dist / 6.f;
    if (g.env) {
        g.env->timeOfDay += dtSkip / 120.f;
        if (g.env->timeOfDay >= 24.f) {
            g.env->timeOfDay -= 24.f;
            g.gameDay++;
        }
        g.env->gameSeconds += dtSkip;
    }
    gS.clockOffset += dtSkip;
    g.rig.cut = true;
    LOG("Transit: streetcar skip %.0f m (%.0f s) to %s", dist, dtSkip, st.name.c_str());
}

void playerLogic(GameWorld& g, float dt) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    const World::TramLine& L = line();
    int pv = pl->state == PS_INVEHICLE ? pl->vehicle : -1;
    int ride = tramOfVehicle(pv);
    if (ride >= 0 && !gT2.trams[ride].materialized()) ride = -1;
    if (gT2.ride >= 0 && ride < 0) {
        // left the tram some other way (thrown out, mission)
        if (gT2.savedView >= 0) g.rig.vehicleView = gT2.savedView;
        gT2.savedView = -1;
        gT2.paidTram = -1;
    }
    gT2.ride = ride;
    if (gT2.skipStage == 1 && g.fadedOut()) {
        if (ride >= 0) skipToNext(g, gT2.trams[ride]);
        gT2.skipStage = 2;
        g.fadeIn(1.2f);
        return;
    }
    if (gT2.skipStage == 2 && g.fadeAlpha <= 0.02f) gT2.skipStage = 0;
    if (ride >= 0) {
        Tram& t = gT2.trams[ride];
        bool exitReq = g.ctl.enter.pressed || g.ctl.special.pressed;
        g.ctl.enter.pressed = g.ctl.special.pressed = false;
        if (exitReq && g.playerControl) {
            if (t.phase == 1 && t.doors > 0.6f) {
                alightPlayer(g, t);
                return;
            }
            g.help("The doors open at the next stop. Hold ~i:SPACE|A~ to skip there.", 4.f);
        }
        if (g.ctl.skip.down && gT2.skipStage == 0 && g.playerControl && g.pinfo.wanted == 0 && t.phase == 0) {
            gT2.skipHold += dt;
            if (gT2.skipHold > 0.8f) {
                gT2.skipStage = 1;
                gT2.skipHold = 0.f;
                g.fadeOut(2.2f);
                g.subtitle("Streetcar", "Riding to " + L.stops[t.next].name + "...", 2.f, 0xffff6a4d);
            }
        } else {
            gT2.skipHold = 0.f;
        }
        g.ctl.handbrake = Button();
        return;
    }
    if (pl->state != PS_ONFOOT) return;
    vec3 pp = pl->pos.toVec3();
    // at an open door of a tram at the stop
    for (int i = 0; i < (int)gT2.trams.size(); i++) {
        Tram& t = gT2.trams[i];
        if (!t.materialized() || t.phase != 1 || t.doors < 0.6f) continue;
        int door;
        float dd;
        if (nearestDoor(g, t, pp.xy(), door, dd) < 0 || dd > 2.2f) continue;
        if (g.hudHelpTimer <= 0.2f) g.help(StrFormat("Press ~i:F|Y~ to board the streetcar ($%d).", kTramFare), 0.5f);
        if ((g.ctl.enter.pressed || g.ctl.special.pressed) && g.playerControl) {
            g.ctl.enter.pressed = g.ctl.special.pressed = false;
            if (g.pinfo.money < kTramFare) g.help(StrFormat("You need $%d for the streetcar.", kTramFare), 3.f);
            else boardTram(g, i);
        }
        return;
    }
    // at a stop: when the next streetcar comes
    gT2.hintTimer -= dt;
    for (int si = 0; si < (int)L.stops.size(); si++) {
        const World::TramStop& st = L.stops[si];
        if (length(st.pos - pp.xy()) > 4.5f) continue;
        if (gT2.hintTimer > 0.f && gT2.hintStop == si) break;
        gT2.hintTimer = 20.f;
        gT2.hintStop = si;
        float best = 1e9f;
        for (const Tram& t : gT2.trams) {
            float d = L.ahead(t.s, st.s);
            int between = 0;
            for (int k = 0; k < (int)L.stops.size(); k++)
                if (L.ahead(t.s, L.stops[k].s) < d) between++;
            best = Min(best, d / 7.f + between * 22.f + (t.phase == 1 ? t.dwell : 0.f));
        }
        std::string eta = best < 40.f ? "arriving" : StrFormat("in %d min", (int)(best / 60.f + 0.5f));
        g.help(StrFormat("Streetcar stop: %s. Next streetcar %s. Board at any door with ~i:F|Y~ ($%d).", st.name.c_str(), eta.c_str(), kTramFare), 7.f);
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------- blips
void ensureBlips(GameWorld& g, float dt) {
    gT2.blipTimer -= dt;
    if (gT2.blipTimer > 0.f) return;
    gT2.blipTimer = 2.5f;
    const World::TramLine& L = line();
    if (gT2.blipNames.empty())
        for (const World::TramStop& st : L.stops) gT2.blipNames.push_back(st.name + " (streetcar)");
    if (gT2.blipNames.empty()) return;
    for (const UI::Blip& b : g.staticBlips)
        if (b.label == gT2.blipNames[0].c_str()) return;
    for (size_t i = 0; i < L.stops.size(); i++) {
        UI::Blip b;
        b.pos = L.stops[i].pos;
        b.icon = UI::BLIP_METRO;
        b.color = 0;
        b.scale = 0.7f;
        b.shortRange = true;
        b.edge = false;
        b.label = gT2.blipNames[i].c_str();
        g.staticBlips.push_back(b);
    }
}

// ---------------------------------------------------------------------------------------------------------------- update
void update(GameWorld& g, float dt) {
    const World::TransitNet& N = *World::gTransit;
    if (gT2.failed || N.trams.empty() || line().stops.empty() || !g.ai.ready || g.laneGraph.lanes.empty()) return;
    if (!gT2.init) {
        gT2.init = true;
        if (!ensureAssets(g)) {
            gT2.init = false;
            return;
        }
        buildProfile();
        registerStops(g);
        initTrams(g);
        const World::TramLine& L = line();
        int sig = 0;
        for (int c : gT2.jConn) sig += c >= 0;
        LOG("Transit: %d streetcars on the %.2f km loop, %zu stops, %d/%zu junction movements found", L.trams, L.length / 1000.f, L.stops.size(), sig,
            L.junctions.size());
    }
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    vec3 listener = g.rig.cam.pos.toVec3();
    const World::TramLine& L = line();
    playerLogic(g, dt);
    updateTWalkers(g, dt);
    updateStopCrowds(g, dt, pp);
    for (int i = 0; i < (int)gT2.trams.size(); i++) {
        Tram& t = gT2.trams[i];
        vec3 mid = L.at(t.s - td::kLength * 0.5f);
        float dist = length(mid.xy() - pp.xy());
        bool aboard = gT2.ride == i;
        if (t.materialized()) {
            if (!tramValid(g, t)) {
                despawnTram(g, t);
                continue;
            }
            if (!aboard && dist > kTramDemat) {
                despawnTram(g, t);
                continue;
            }
            driveTram(g, t, dt, aboard, listener);
            placeTram(g, t);
            tramAudio(g, t, listener);
            continue;
        }
        advanceVirtual(t, dt);
        if (dist < kTramMat && !g.populationOff) spawnTram(g, t);
    }
    ensureBlips(g, dt);
}

void submit(GameWorld& g) {
    if (!gT2.init || gT2.asset[0] < 0 || !g.renderer || !g.renderer->dynamic) return;
    Render::DynamicRenderer* dyn = g.renderer->dynamic;
    dvec3 cam = g.rig.cam.pos;
    vec3 camF = g.rig.cam.forward();
    bool night = g.env && (g.env->timeOfDay < 6.8f || g.env->timeOfDay > 19.2f);
    const World::TramLine& L = line();
    int lights = 0;
    for (const Tram& t : gT2.trams) {
        if (t.materialized()) {
            for (int k = 0; k < 3; k++) {
                int vi = t.sec[k];
                if (vi < 0 || !g.vehicles[vi].used) continue;
                const Vehicle& v = g.vehicles[vi];
                float dist = v.visibleDist;
                mat3 R = v.sim.body.rotMat();
                if (gT2.doorLeaf && dist < 110.f) {
                    std::vector<float> doors;
                    TransitModels::trm::tDoorsOf(k, doors);
                    float open = t.doors;
                    float out = 0.02f + 0.06f * Saturate(open * 4.f);
                    for (int d = 0; d < (int)doors.size(); d++)
                        for (int l = -1; l <= 1; l += 2) {
                            vec3 local(TransitModels::trm::kTW + out, doors[d] + l * (0.33f + open * 0.6f), 0.f);
                            Render::DrawItem di;
                            di.model = gT2.doorLeaf;
                            di.pos = v.sim.body.pos + R * local;
                            di.rot = R;
                            di.tint0 = vec4(v.color0, v.dirt);
                            di.tint1 = vec4(v.color1, 0.f);
                            di.id = 0xB00000000ull | ((u64)v.uid << 4) | (u64)(d * 2 + (l > 0));
                            di.castShadow = dist < 60.f;
                            di.drawGlass = !v.windowsBroken;
                            dyn->submit(di);
                        }
                }
                if (lights < 8 && dist < 260.f) {
                    if (k == 0) {
                        Render::DynamicLight hl;
                        hl.pos = v.sim.body.pos + R * vec3(0.f, td::kSectionLen[0] * 0.5f + 0.2f, 0.9f);
                        hl.dir = normalize(R * vec3(0.f, 1.f, -0.08f));
                        hl.color = vec3(1.f, 0.95f, 0.86f) * (night ? 5000.f : 900.f);
                        hl.radius = 45.f;
                        hl.spotCos = cosf(24.f * kDegToRad);
                        hl.spotInner = cosf(12.f * kDegToRad);
                        g.renderer->addLight(hl);
                        lights++;
                    }
                    if (night && dist < 120.f) {
                        Render::DynamicLight il;
                        il.pos = v.sim.body.pos + R * vec3(0.f, 0.f, 2.3f);
                        il.color = vec3(1.f, 0.95f, 0.88f) * 200.f;
                        il.radius = 7.f;
                        g.renderer->addLight(il);
                        lights++;
                    }
                }
            }
            continue;
        }
        // far trams: shells straight from the kinematic run
        vec3 c = L.at(t.s - td::kLength * 0.5f);
        vec3 toC = rel(dvec3(c), cam);
        float dist = length(toC);
        if (dist > kTramDraw || (dot(toC, camF) < -30.f && dist > 30.f)) continue;
        for (int k = 0; k < 3; k++) {
            dvec3 pos;
            quat rot;
            vec3 fwd;
            modulePose(t, k, pos, rot, fwd);
            const VehicleAsset& a = g.vassets[gT2.asset[k]];
            Render::DrawItem di;
            di.model = a.bodyLod[1] ? a.bodyLod[1] : a.body;
            di.pos = pos;
            di.rot = mat3FromQuat(rot);
            di.tint0 = vec4(a.spec.liveryPrimary, 0.06f);
            di.tint1 = vec4(a.spec.liverySecondary, 0.f);
            di.lightBits = night ? 1u : 0u;
            di.id = 0xB80000000ull | (u64)(t.idx * 4 + k);
            di.castShadow = dist < 300.f;
            dyn->submit(di);
        }
    }
}

bool playerOnTram() { return gT2.ride >= 0; }

// One line on what a tram is doing (tests): position, speed, stop, the junction it deals with, what is on the rails
std::string tramDiag(GameWorld& g, Tram& t) {
    const World::TramLine& L = line();
    std::string s = StrFormat("mat %d s %.1f v %.2f a %.2f phase %d next %d dwell %.1f doors %.2f held %.1f lineJ %d passedJ %d lineStop %.1f", (int)t.materialized(),
                              t.s, t.v, t.a, t.phase, t.next, t.dwell, t.doors, t.held, t.lineJ, t.passedJ, t.lineStop);
    if (t.lineJ >= 0 && t.lineJ < (int)L.junctions.size()) {
        const World::TramJunction& J = L.junctions[t.lineJ];
        int ci = t.lineJ < (int)gT2.jConn.size() ? gT2.jConn[t.lineJ] : -1;
        int sig = -1;
        if (ci >= 0) {
            const AI::Connector& C = g.laneGraph.conns[ci];
            sig = (int)g.laneGraph.movementSignal(C.node, C.approach, C.turn, g.traffic.time);
        }
        s += StrFormat(" | J node %d control %d minor %d turn %d line in %.1f m sig %d", J.node, (int)J.control, (int)J.minor, J.turn, L.ahead(t.s, J.sIn - 1.6f), sig);
    }
    if (t.materialized()) {
        int hb;
        bool hp;
        float gap = scanAhead(g, t, 40.f, &hb, &hp);
        if (hb >= 0) {
            const AI::Body& b = g.traffic.bodies[hb];
            s += StrFormat(" | on the rails: %s %d at %.1f m (%.1f, %.1f) v %.1f fl %d", hp ? "ped" : "car", b.host, gap, b.pos.x, b.pos.y, b.speed, (int)b.flags);
        }
    }
    return s;
}

}  // namespace tr
}  // namespace Transit
}  // namespace Game
