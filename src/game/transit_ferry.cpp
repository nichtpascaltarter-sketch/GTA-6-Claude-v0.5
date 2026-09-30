// Bay ferries: two "Sol Ferry" catamarans on the Bay Ferry loop (Sol Beach -> Port Isle -> Key Coral), laid out by
// world/transit.cpp. Like the SkyLine trains, each ferry's position is a pure function of the transit clock: far
// ferries are drawn straight from the timetable, near ones become scripted boats (VC_BOAT) the player can ride. Docking
// alongside the T-head, a horn on departure, engine and wake sound, announcements, pier crowds boarding and getting
// off, a fare, skip-to-next-pier and police following the player aboard. Included by transit_game.cpp.

namespace Game {
namespace Transit {
namespace tf {

using namespace tg;

constexpr int kFerries = 2;
constexpr float kCruise = 8.5f, kFAccel = 0.22f, kFDecel = 0.16f, kLatAcc = 0.35f;
constexpr float kFerryMat = 800.f, kFerryDemat = 950.f, kFerryDraw = 4000.f;
constexpr float kPierDwell = 40.f;
constexpr int kFerryFare = 4;

struct LegProf {
    std::vector<vec2> p;          // samples (from the world leg, ~5 m)
    std::vector<float> s, v, t;   // arc length, speed, time at each sample
    float len = 0.f, dur = 0.f;
};

struct FWalker {
    int ped = -1;
    u32 uid = 0;
    int mode = 0;                 // 0 leave the pier, 1 board the ferry at the gate
    std::vector<vec2> path;
    int wp = 0;
    float timer = 0.f;
};

struct Ferry {
    float phase = 0.f;
    int veh = -1;
    u32 vehUid = 0;
    int master = -1;
    u32 masterUid = 0;
    int dockLatch = -1;           // pier of the dock events already run (-1 none)
    bool hornLatch = false;
    int annLatch = -1;
    std::vector<Rider> cops;
    bool materialized() const { return veh >= 0; }
};

struct FerryPose {
    vec2 pos, fwd;
    float speed = 0.f, accel = 0.f, turn = 0.f;
    int docked = -1;              // pier index while docked
    float dockT = 0.f;            // seconds since docking
    int leg = -1;                 // leg while under way
    int nextPier = 0;
    float toNext = 0.f;           // seconds to the next arrival
};

struct FState {
    bool init = false, failed = false;
    std::vector<LegProf> legs;
    std::vector<float> legStart;  // cycle time the dwell before leg i begins
    float cycle = 0.f;
    int asset = -1;
    Render::Model* farModel = nullptr;
    Ferry ferries[kFerries];
    int ride = -1;
    int skipStage = 0;
    float skipHold = 0.f;
    float hintTimer = 0.f;
    std::vector<FWalker> walkers;
    std::vector<Waiter> waiters;
    float crowdTimer = 0.f;
    std::vector<std::string> blipNames;
    float blipTimer = 0.f;
    int boardings = 0;
};
FState gF;

const World::FerryRoute& route() { return World::gTransit->ferries[0]; }
const World::FerryPier& pierOf(int legOrPier) {
    const World::FerryRoute& R = route();
    return World::gTransit->piers[R.piers[legOrPier % (int)R.piers.size()]];
}

void buildProfiles() {
    const World::FerryRoute& R = route();
    gF.legs.clear();
    gF.legStart.clear();
    float T = 0.f;
    for (const std::vector<vec2>& pts : R.legs) {
        LegProf L;
        L.p = pts;
        int n = (int)pts.size();
        L.s.assign(n, 0.f);
        for (int i = 1; i < n; i++) L.s[i] = L.s[i - 1] + length(pts[i] - pts[i - 1]);
        L.len = n ? L.s[n - 1] : 0.f;
        // speed limits: cruise, curvature (gentle lateral acceleration), then accel / decel passes
        L.v.assign(n, kCruise);
        for (int i = 1; i + 1 < n; i++) {
            vec2 a = normalize(pts[i] - pts[i - 1]), b = normalize(pts[i + 1] - pts[i]);
            float ds = Max(L.s[i + 1] - L.s[i - 1], 0.1f) * 0.5f;
            float k = fabsf(cross(a, b)) / ds;
            if (k > 1e-4f) L.v[i] = Min(L.v[i], sqrtf(kLatAcc / k));
        }
        if (n) L.v[0] = L.v[n - 1] = 0.f;
        for (int i = 1; i < n; i++) L.v[i] = Min(L.v[i], sqrtf(L.v[i - 1] * L.v[i - 1] + 2.f * kFAccel * (L.s[i] - L.s[i - 1])));
        for (int i = n - 2; i >= 0; i--) L.v[i] = Min(L.v[i], sqrtf(L.v[i + 1] * L.v[i + 1] + 2.f * kFDecel * (L.s[i + 1] - L.s[i])));
        L.v[0] = 0.f;
        L.t.assign(n, 0.f);
        for (int i = 1; i < n; i++) L.t[i] = L.t[i - 1] + 2.f * (L.s[i] - L.s[i - 1]) / Max(L.v[i] + L.v[i - 1], 0.05f);
        L.dur = n ? L.t[n - 1] : 0.f;
        gF.legStart.push_back(T);
        T += kPierDwell + L.dur;
        gF.legs.push_back(L);
    }
    gF.cycle = Max(T, 1.f);
}

FerryPose poseAt(float tau) {
    FerryPose P;
    tau = fmodf(tau, gF.cycle);
    if (tau < 0.f) tau += gF.cycle;
    int nl = (int)gF.legs.size();
    int i = nl - 1;
    for (int k = 0; k < nl; k++)
        if (tau >= gF.legStart[k] && (k + 1 >= nl || tau < gF.legStart[k + 1])) i = k;
    float local = tau - gF.legStart[i];
    const LegProf& L = gF.legs[i];
    if (local < kPierDwell) {
        const World::FerryPier& fp = pierOf(i);
        P.pos = fp.berth;
        P.fwd = vec2(-sinf(fp.berthYaw), cosf(fp.berthYaw));
        P.docked = i;
        P.dockT = local;
        P.nextPier = (i + 1) % nl;
        P.toNext = kPierDwell - local + L.dur;
        return P;
    }
    float t = local - kPierDwell;
    int lo = 0, hi = (int)L.t.size() - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (L.t[mid] <= t) lo = mid;
        else hi = mid;
    }
    float f = Saturate((t - L.t[lo]) / Max(L.t[hi] - L.t[lo], 1e-4f));
    P.pos = lerp(L.p[lo], L.p[hi], f);
    P.speed = Lerp(L.v[lo], L.v[hi], f);
    P.accel = (L.v[hi] - L.v[lo]) / Max(L.t[hi] - L.t[lo], 1e-3f);
    // heading from a chord around the position (smooth), settling onto the berth heading at the ends
    int a = Max(0, lo - 2), b = Min((int)L.p.size() - 1, hi + 2);
    vec2 ch = L.p[b] - L.p[a];
    P.fwd = length2(ch) > 1e-4f ? normalize(ch) : vec2(0, 1);
    if (a > 0 && b + 1 < (int)L.p.size()) {
        vec2 c0 = normalize(L.p[a] - L.p[a - 1]), c1 = normalize(L.p[b + 1] - L.p[b]);
        P.turn = cross(c0, c1) / Max(L.s[b + 1] - L.s[a - 1], 1.f);
    }
    P.leg = i;
    P.nextPier = (i + 1) % nl;
    P.toNext = L.dur - t;
    return P;
}

inline float nowF(const GameWorld& g) { return (float)fmod(g.time + gS.clockOffset, 1.0e7); }

bool ferryValid(const GameWorld& g, const Ferry& f) {
    return f.veh >= 0 && f.veh < (int)g.vehicles.size() && g.vehicles[f.veh].used && g.vehicles[f.veh].uid == f.vehUid;
}

// World transform of a pose: on the water surface with a little heave, roll into turns and pitch with acceleration
void poseTransform(const FerryPose& P, float time, dvec3& pos, quat& rot) {
    float wz = 0.f;
    if (!Phys::waterSurface(P.pos.x, P.pos.y, wz)) wz = 0.f;
    float heave = 0.05f * sinf(time * 0.9f) + 0.03f * sinf(time * 1.7f + 1.3f);
    pos = dvec3(P.pos.x, P.pos.y, wz * 0.35f + heave);   // the hulls average out the small bay chop
    float yaw = atan2f(-P.fwd.x, P.fwd.y);
    float roll = Clamp(P.turn * P.speed * P.speed * 0.9f, -0.05f, 0.05f) + 0.012f * sinf(time * 0.7f);
    float pitch = Clamp(-P.accel * 0.04f, -0.02f, 0.02f) + 0.008f * sinf(time * 0.55f + 0.4f);
    rot = normalize(quatAxisAngle(vec3(0, 0, 1), yaw) * quatAxisAngle(vec3(1, 0, 0), pitch) * quatAxisAngle(vec3(0, 1, 0), roll));
}

// ---------------------------------------------------------------------------------------------------------------- assets
bool ensureAssets(GameWorld& g) {
    if (gF.asset >= 0) return true;
    Render::DynamicRenderer* dyn = g.renderer ? g.renderer->dynamic : nullptr;
    if (!dyn) return false;
    VehicleAsset a;
    TransitModels::ferrySpec(a.spec);
    MeshData lod[3];
    for (int l = 0; l < 3; l++) TransitModels::buildFerry(l, lod[l]);
    a.body = dyn->createModel(lod[0]);
    a.bodyLod[0] = dyn->createModel(lod[1]);
    a.bodyLod[1] = dyn->createModel(lod[2]);
    gF.farModel = a.bodyLod[1];
    g.vassets.push_back(a);
    gF.asset = (int)g.vassets.size() - 1;
    for (Vehicle& v : g.vehicles)
        if (v.used && v.model >= 0 && v.model < (int)g.vassets.size()) v.sim.model = &g.vassets[v.model].spec;
    LOG("Transit: Sol Ferry model registered as vehicle asset %d", gF.asset);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------- vessels
void placeFerry(GameWorld& g, Ferry& f, const FerryPose& P) {
    Vehicle& v = g.vehicles[f.veh];
    dvec3 pos;
    quat rot;
    poseTransform(P, (float)g.time + f.phase * 0.37f, pos, rot);
    v.sim.body.pos = pos;
    v.sim.body.rot = rot;
    v.sim.body.vel = vec3(P.fwd * P.speed, 0.f);
    v.sim.body.angVel = vec3(0.f);
    v.sim.sleeping = true;
    v.sim.inWater = true;
    v.sim.engineOn = true;
    const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
    v.sim.engineRpm = 700.f + (spec.maxRpm - 700.f) * (P.docked >= 0 ? 0.08f : 0.25f + 0.7f * Saturate(P.speed / kCruise));
    v.sim.throttleOut = P.docked >= 0 ? 0.05f : (P.accel > 0.02f ? 0.9f : (P.accel < -0.02f ? 0.15f : 0.55f));
    v.sim.health = 1000.f;
    v.sim.engineHealth = 1000.f;
    for (float& dz : v.sim.damageZones) dz = 0.f;
    v.sim.wrecked = false;
    v.fireTimer = 0.f;
    v.exploded = false;
    v.sim.impactImpulse = 0.f;
    v.sim.scrape = 0.f;
    v.sim.splash = 0.f;
    v.scripted = true;
    v.persistent = true;
    v.locked = true;
    v.renderFar = true;
    float tod = g.env ? g.env->timeOfDay : 12.f;
    v.lightsOn = tod > 18.8f || tod < 6.9f;
    v.ctl = Vehicles::VehicleControls();
}

void spawnFerry(GameWorld& g, Ferry& f, const FerryPose& P) {
    dvec3 pos;
    quat rot;
    poseTransform(P, (float)g.time, pos, rot);
    int vi = g.spawnVehicle(gF.asset, pos, atan2f(-P.fwd.x, P.fwd.y), false);
    if (vi < 0) return;
    Vehicle& v = g.vehicles[vi];
    v.scripted = true;
    v.persistent = true;
    v.locked = true;
    v.renderFar = true;
    v.dirt = 0.08f;
    f.veh = vi;
    f.vehUid = v.uid;
    if (!g.chars.empty()) {
        int pid = g.spawnPed(g.randomCivilianChar(hash32(0xFE11u + (u32)(&f - gF.ferries)), 0), pos, 0.f, FAC_CIVILIAN);
        if (pid >= 0) {
            g.peds[pid].persistent = true;
            g.peds[pid].invincible = true;
            g.peds[pid].brain.type = BRAIN_NONE;
            g.warpPedIntoVehicle(pid, vi, 0);
            f.master = pid;
            f.masterUid = g.peds[pid].uid;
        }
    }
    placeFerry(g, f, P);
}

void despawnFerry(GameWorld& g, Ferry& f) {
    for (Rider& r : f.cops)
        if (pedValid(g, r.ped, r.uid) && g.peds[r.ped].vehicle >= 0) g.removePedFromVehicle(r.ped, false);
    f.cops.clear();
    if (pedValid(g, f.master, f.masterUid)) g.despawnPed(f.master);
    f.master = -1;
    if (ferryValid(g, f)) g.despawnVehicle(f.veh, true);
    f.veh = -1;
}

void speakFerry(GameWorld& g, const std::string& line, vec3 pos, bool inside) {
#ifdef HAVE_AUDIO
    Speech::Persona ann = Speech::persona("announcer");
    std::string txt = "[pa][calm]" + line;
    if (inside) Audio::speak(txt.c_str(), ann.voice, 0.85f);
    else Audio::speakAt(txt.c_str(), ann.voice, pos, 1.f);
#else
    (void)pos;
#endif
    if (g.settingsSubtitles && inside) g.subtitle("Bay Ferry", line, 3.5f, 0xff4ad8e0);
}

// Gate on the pier head and the ferry's starboard door beside it
vec3 gatePoint(const World::FerryPier& fp, float back) { return vec3(fp.gate - fp.dir * back, fp.deckZ); }

// ---------------------------------------------------------------------------------------------------------------- people
std::vector<vec2> pierExitPath(const World::FerryPier& fp, vec2 from) {
    std::vector<vec2> p;
    p.push_back(from);
    p.push_back(fp.head() - fp.dir * (World::transit_ferry::kHeadDepth * 0.5f));
    p.push_back(fp.base + fp.dir * 4.f);
    p.push_back(fp.base - fp.dir * 18.f);
    return p;
}

void startFWalker(GameWorld& g, int pid, int mode, std::vector<vec2> path) {
    FWalker w;
    w.ped = pid;
    w.uid = g.peds[pid].uid;
    w.mode = mode;
    w.path = std::move(path);
    Ped& p = g.peds[pid];
    p.brain.type = BRAIN_GOTO;
    p.brain.speed = 1.3f + 0.2f * hashToFloat(hash32(p.uid));
    p.brain.goal = dvec3(w.path[0].x, w.path[0].y, p.pos.z);
    gF.walkers.push_back(w);
}

void updateFWalkers(GameWorld& g, float dt) {
    for (size_t i = 0; i < gF.walkers.size();) {
        FWalker& w = gF.walkers[i];
        bool drop = !pedValid(g, w.ped, w.uid);
        Ped* pp = drop ? nullptr : &g.peds[w.ped];
        if (pp && (pp->state != PS_ONFOOT || pp->health <= 0.f || pp->brain.type != BRAIN_GOTO)) {
            if (pp->health > 0.f) pp->persistent = false;
            drop = true;
        }
        if (drop) {
            gF.walkers[i] = gF.walkers.back();
            gF.walkers.pop_back();
            continue;
        }
        Ped& p = *pp;
        w.timer += dt;
        vec2 pos = p.pos.toVec3().xy();
        bool last = w.wp + 1 >= (int)w.path.size();
        if (length(w.path[w.wp] - pos) < (last ? 0.8f : 1.2f) || (w.timer > 25.f && !last)) {
            w.timer = 0.f;
            if (!last) {
                w.wp++;
                p.brain.goal = dvec3(w.path[w.wp].x, w.path[w.wp].y, p.pos.z);
            } else {
                if (w.mode == 1) g.despawnPed(w.ped);   // stepped aboard (inside the saloon)
                else releaseToCity(g, w.ped);
                gF.walkers[i] = gF.walkers.back();
                gF.walkers.pop_back();
                continue;
            }
        }
        i++;
    }
}

// A handful of people wait on the T-head of the piers near the player
void updatePierCrowds(GameWorld& g, float dt, vec3 pp) {
    const World::TransitNet& N = *World::gTransit;
    for (size_t i = 0; i < gF.waiters.size();) {
        Waiter& w = gF.waiters[i];
        bool ok = pedValid(g, w.ped, w.uid) && g.peds[w.ped].health > 0.f && g.peds[w.ped].state == PS_ONFOOT;
        if (ok) {
            PedAI& pa = g.pedAI(w.ped);
            if (g.peds[w.ped].brain.type != BRAIN_WANDER || pa.activity != ACT_SCENARIO) ok = false;
            else pa.actTimer = Max(pa.actTimer, 60.f);
        }
        if (ok && length(N.piers[w.station].base - pp.xy()) > 300.f) {
            g.despawnPed(w.ped);
            ok = false;
        }
        if (!ok) {
            if (pedValid(g, w.ped, w.uid)) g.peds[w.ped].persistent = false;
            gF.waiters[i] = gF.waiters.back();
            gF.waiters.pop_back();
            continue;
        }
        i++;
    }
    gF.crowdTimer -= dt;
    if (gF.crowdTimer > 0.f || g.populationOff || g.chars.empty()) return;
    gF.crowdTimer = 2.f;
    float tod = g.env ? g.env->timeOfDay : 12.f;
    float busy = tod > 22.5f || tod < 6.f ? 0.3f : 1.f;
    for (int pi = 0; pi < (int)N.piers.size(); pi++) {
        const World::FerryPier& fp = N.piers[pi];
        float d = length(fp.base - pp.xy());
        if (d > 220.f) continue;
        int have = 0;
        for (const Waiter& w : gF.waiters) have += w.station == pi;
        int want = (int)(busy * (3.f + 3.f * hashToFloat(hash32(fp.seed)))) + 1;
        if (have >= want) continue;
        u32 h = hash32(fp.seed * 31u + (u32)(g.time * 2.0) + (u32)have * 97u);
        vec2 rt = fp.right();
        vec2 spot = fp.head() - fp.dir * (2.f + hashToFloat(h) * 4.f) + rt * (-(3.f + hashToFloat(hash32(h)) * 12.f));
        vec3 sp(spot, fp.deckZ);
        if (g.inCameraView(sp + vec3(0, 0, 1.f), 1.f) && length(sp - g.rig.cam.pos.toVec3()) < 90.f && g.populationWarmup <= 0.f) continue;
        int id = spawnCivilian(g, h, sp, 0.f);
        if (id < 0) continue;
        Ped& p = g.peds[id];
        p.brain.type = BRAIN_WANDER;
        p.brain.edge = -1;
        PedAI& pa = g.pedAI(id);
        pa.activity = ACT_SCENARIO;
        pa.anchor = spot;
        pa.anchorYaw = atan2f(-fp.dir.x, fp.dir.y) + (hashToFloat(hash32(h * 5u)) - 0.5f) * 1.2f;   // looking out over the water
        p.yaw = pa.anchorYaw;
        pa.stance = (h >> 11) % 4 == 0 ? 8 : 23;
        pa.clip = -1;
        pa.actTimer = 600.f;
        Waiter w;
        w.ped = id;
        w.uid = p.uid;
        w.station = pi;
        w.spot = spot;
        gF.waiters.push_back(w);
    }
}

// Docking: passengers get off and walk ashore, the waiting ones board
void dockEvents(GameWorld& g, Ferry& f, const FerryPose& P, bool playerAboard, vec3 listener) {
    const World::FerryPier& fp = pierOf(P.docked);
    int pierIdx = route().piers[P.docked % (int)route().piers.size()];
    vec3 fpos = g.vehicles[f.veh].sim.body.pos.toVec3();
    bool near_ = length(fpos - listener) < 160.f;
    if (f.dockLatch != P.docked) {
        f.dockLatch = P.docked;
        f.hornLatch = false;
        if (playerAboard) speakFerry(g, "Now arriving at " + fp.name + ". Please mind your step on the gangway.", fpos, true);
        // riders ashore
        if (near_ && !g.chars.empty() && !g.populationOff) {
            int n = 1 + (int)(hash32(f.vehUid + (u32)g.time) % 3u);
            for (int k = 0; k < n; k++) {
                vec3 gp = gatePoint(fp, 0.8f + 0.5f * k);
                gp += vec3(fp.right() * (0.6f * (k - 1)), 0.f);
                int id = spawnCivilian(g, hash32(f.vehUid * 13u + (u32)k + (u32)(g.time * 3.0)), gp, atan2f(fp.dir.x, -fp.dir.y));
                if (id < 0) break;
                startFWalker(g, id, 0, pierExitPath(fp, gp.xy()));
            }
        }
    }
    // after a few seconds the pier crowd boards
    if (P.dockT > 5.f && P.dockT < kPierDwell - 8.f) {
        for (size_t i = 0; i < gF.waiters.size();) {
            Waiter& w = gF.waiters[i];
            if (w.station != pierIdx || !pedValid(g, w.ped, w.uid) || hash32(w.uid) % 4 == 0) {
                i++;
                continue;
            }
            int pid = w.ped;
            gF.waiters[i] = gF.waiters.back();
            gF.waiters.pop_back();
            std::vector<vec2> path;
            path.push_back(gatePoint(fp, 1.5f).xy());
            path.push_back(gatePoint(fp, -0.6f).xy());
            startFWalker(g, pid, 1, path);
        }
    }
    // horn a few seconds before casting off
    if (!f.hornLatch && P.dockT > kPierDwell - 6.f) {
        f.hornLatch = true;
#ifdef HAVE_AUDIO
        if (length(fpos - listener) < 1500.f) Audio::play(Audio::SFX_SHIP_HORN, fpos + vec3(0, 0, 7.f), 1.f, 0.97f);
#endif
        if (playerAboard) {
            const World::FerryPier& nx = pierOf(P.nextPier);
            speakFerry(g, "This is the Bay Ferry to " + nx.name + ". We are departing now.", fpos, true);
        }
    }
    (void)f;
}

// ---------------------------------------------------------------------------------------------------------------- player
void boardFerry(GameWorld& g, int fi) {
    Ferry& f = gF.ferries[fi];
    int seat = freeCarSeat(g, f.veh);
    if (seat < 0) return;
    g.warpPedIntoVehicle(g.player, f.veh, seat);
    g.pinfo.money = Max<long long>(0, g.pinfo.money - kFerryFare);
    g.rig.cut = true;
    gF.ride = fi;
    gF.boardings++;
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_PURCHASE, 0.35f);
#endif
    FerryPose P = poseAt(nowF(g) + f.phase);
    g.notify("Bay Ferry", StrFormat("Next stop %s. Fare $%d.", pierOf(P.nextPier).name.c_str(), kFerryFare));
    g.help("Hold ~i:SPACE|A~ to skip to the next pier. Press ~i:F|Y~ to go ashore while docked.", 7.f);
    // officers on the player's heels come aboard
    if (g.pinfo.wanted > 0) {
        std::vector<int> near_;
        g.pedsNear(g.peds[g.player].pos.toVec3().xy(), 30.f, near_);
        int n = 0;
        for (int id : near_) {
            Ped& c = g.peds[id];
            if (c.faction != FAC_POLICE || c.state != PS_ONFOOT || c.health <= 0.f || n >= 3) continue;
            if (c.brain.type != BRAIN_COMBAT && c.brain.type != BRAIN_ARREST) continue;
            int s2 = freeCarSeat(g, f.veh);
            if (s2 < 0) break;
            g.warpPedIntoVehicle(id, f.veh, s2);
            c.brain.type = BRAIN_COMBAT;
            c.brain.target = g.player;
            f.cops.push_back({id, c.uid});
            n++;
        }
    }
    LOG("Transit: player boarded the ferry %d", fi);
}

void goAshore(GameWorld& g, int fi, int pierLeg) {
    Ferry& f = gF.ferries[fi];
    const World::FerryPier& fp = pierOf(pierLeg);
    Ped* pl = g.playerPed();
    g.removePedFromVehicle(g.player, false);
    vec3 gp = gatePoint(fp, 1.4f);
    pl->pos = dvec3(gp.x, gp.y, gp.z + 0.05f);
    pl->yaw = atan2f(fp.dir.x, -fp.dir.y);
    pl->vel = vec3(0.f);
    pl->grounded = true;
    pl->pendingAction = -1;
    g.rig.cut = true;
    gF.ride = -1;
    g.notify("Bay Ferry", fp.name);
    for (Rider& c : f.cops)
        if (pedValid(g, c.ped, c.uid) && g.peds[c.ped].vehicle >= 0) {
            g.removePedFromVehicle(c.ped, false);
            vec3 cp = gatePoint(fp, 2.4f);
            g.peds[c.ped].pos = dvec3(cp.x + 0.8f, cp.y, cp.z + 0.05f);
        }
    f.cops.clear();
    LOG("Transit: player went ashore at %s", fp.name.c_str());
}

void playerLogic(GameWorld& g, float dt) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    int pv = pl->state == PS_INVEHICLE ? pl->vehicle : -1;
    int ride = -1;
    for (int i = 0; i < kFerries; i++)
        if (gF.ferries[i].veh >= 0 && gF.ferries[i].veh == pv) ride = i;
    gF.ride = ride;
    if (gF.skipStage == 1 && g.fadedOut()) {
        if (ride >= 0) {
            FerryPose P = poseAt(nowF(g) + gF.ferries[ride].phase);
            float dtSkip = P.toNext + 2.f;
            gS.clockOffset += dtSkip;
            if (g.env) {
                g.env->timeOfDay += dtSkip / 120.f;
                if (g.env->timeOfDay >= 24.f) {
                    g.env->timeOfDay -= 24.f;
                    g.gameDay++;
                }
                g.env->gameSeconds += dtSkip;
            }
            g.rig.cut = true;
            LOG("Transit: ferry skip %.0f s", dtSkip);
        }
        gF.skipStage = 2;
        g.fadeIn(1.2f);
        return;
    }
    if (gF.skipStage == 2 && g.fadeAlpha <= 0.02f) gF.skipStage = 0;
    if (ride >= 0) {
        Ferry& f = gF.ferries[ride];
        FerryPose P = poseAt(nowF(g) + f.phase);
        bool exitReq = g.ctl.enter.pressed || g.ctl.special.pressed;
        g.ctl.enter.pressed = g.ctl.special.pressed = false;
        if (exitReq && g.playerControl) {
            if (P.docked >= 0 && P.dockT > 3.f && P.dockT < kPierDwell - 3.f) {
                goAshore(g, ride, P.docked);
                return;
            }
            g.help("We're under way. Wait for the next pier, or hold ~i:SPACE|A~ to skip there.", 4.f);
        }
        if (g.ctl.skip.down && gF.skipStage == 0 && g.playerControl && g.pinfo.wanted == 0 && P.docked < 0 && P.toNext > 15.f) {
            gF.skipHold += dt;
            if (gF.skipHold > 0.8f) {
                gF.skipStage = 1;
                gF.skipHold = 0.f;
                g.fadeOut(2.2f);
                g.subtitle("Bay Ferry", "Crossing to " + pierOf(P.nextPier).name + "...", 2.f, 0xff4ad8e0);
            }
        } else {
            gF.skipHold = 0.f;
        }
        g.ctl.handbrake = Button();
        return;
    }
    if (pl->state != PS_ONFOOT) return;
    // at a gate with a ferry alongside
    vec3 pp = pl->pos.toVec3();
    for (int i = 0; i < kFerries; i++) {
        Ferry& f = gF.ferries[i];
        if (!f.materialized()) continue;
        FerryPose P = poseAt(nowF(g) + f.phase);
        if (P.docked < 0 || P.dockT < 4.f || P.dockT > kPierDwell - 6.f) continue;
        const World::FerryPier& fp = pierOf(P.docked);
        vec3 gp = gatePoint(fp, 0.6f);
        if (length(gp.xy() - pp.xy()) > 3.4f || fabsf(pp.z - gp.z) > 1.8f) continue;
        if (g.hudHelpTimer <= 0.2f) g.help(StrFormat("Press ~i:F|Y~ to board the ferry to %s ($%d).", pierOf(P.nextPier).name.c_str(), kFerryFare), 0.5f);
        if ((g.ctl.enter.pressed || g.ctl.special.pressed) && g.playerControl) {
            g.ctl.enter.pressed = g.ctl.special.pressed = false;
            if (g.pinfo.money < kFerryFare) g.help(StrFormat("You need $%d for the ferry.", kFerryFare), 3.f);
            else boardFerry(g, i);
        }
        return;
    }
    // on a pier: timetable
    gF.hintTimer -= dt;
    const World::TransitNet& N = *World::gTransit;
    for (int pi = 0; pi < (int)N.piers.size(); pi++) {
        const World::FerryPier& fp = N.piers[pi];
        vec2 d = pp.xy() - fp.head();
        if (fabsf(dot(d, fp.dir)) > 10.f || fabsf(dot(d, fp.right())) > World::transit_ferry::kHeadHalf + 1.f || fabsf(pp.z - fp.deckZ) > 1.5f) continue;
        if (gF.hintTimer > 0.f) break;
        gF.hintTimer = 25.f;
        int legIdx = -1;
        const World::FerryRoute& R = route();
        for (int k = 0; k < (int)R.piers.size(); k++)
            if (R.piers[k] == pi) legIdx = k;
        float best = 1e9f;
        for (int i = 0; i < kFerries; i++) {
            float tau = fmodf(nowF(g) + gF.ferries[i].phase, gF.cycle);
            float wait = gF.legStart[legIdx] - tau;
            if (wait < -kPierDwell) wait += gF.cycle;
            best = Min(best, Max(wait, 0.f));
        }
        std::string eta = best < 1.f ? "boarding now" : (best < 60.f ? StrFormat("in %d s", (int)best) : StrFormat("in %d min", (int)(best / 60.f + 0.5f)));
        g.help(StrFormat("%s ferry pier. Next ferry to %s %s. Board at the gate with ~i:F|Y~ ($%d).", fp.name.c_str(), pierOf(legIdx + 1).name.c_str(), eta.c_str(), kFerryFare),
               7.f);
        break;
    }
}

// ---------------------------------------------------------------------------------------------------------------- blips
void ensureBlips(GameWorld& g, float dt) {
    gF.blipTimer -= dt;
    if (gF.blipTimer > 0.f) return;
    gF.blipTimer = 3.f;
    const World::TransitNet& N = *World::gTransit;
    if (gF.blipNames.empty()) {
        gF.blipNames.reserve(N.piers.size());
        for (const World::FerryPier& fp : N.piers) gF.blipNames.push_back(fp.name + " Ferry Terminal");
    }
    if (gF.blipNames.empty()) return;
    for (const UI::Blip& b : g.staticBlips)
        if (b.label == gF.blipNames[0].c_str()) return;
    for (size_t i = 0; i < N.piers.size(); i++) {
        UI::Blip b;
        b.pos = N.piers[i].base;
        b.icon = UI::BLIP_FERRY;
        b.color = 0;
        b.scale = 1.f;
        b.shortRange = true;
        b.label = gF.blipNames[i].c_str();
        g.staticBlips.push_back(b);
    }
}

// ---------------------------------------------------------------------------------------------------------------- frame
void update(GameWorld& g, float dt) {
    const World::TransitNet& N = *World::gTransit;
    if (gF.failed || N.ferries.empty() || N.ferries[0].legs.empty()) return;
    if (!gF.init) {
        gF.init = true;
        buildProfiles();
        for (int i = 0; i < kFerries; i++) gF.ferries[i].phase = gF.cycle * (float)i / kFerries;
        LOG("Transit: Bay Ferry timetable: loop %.1f min, %d ferries, headway %.1f min", gF.cycle / 60.f, kFerries, gF.cycle / kFerries / 60.f);
    }
    if (!ensureAssets(g)) return;
    Ped* pl = g.playerPed();
    if (!pl) return;
    playerLogic(g, dt);
    vec3 pp = pl->pos.toVec3();
    vec3 listener = g.rig.cam.pos.toVec3();
    int pv = g.playerVehicle();
    float clock = nowF(g);
    for (int i = 0; i < kFerries; i++) {
        Ferry& f = gF.ferries[i];
        FerryPose P = poseAt(clock + f.phase);
        float d = length(P.pos - pp.xy());
        bool aboard = f.veh >= 0 && pv == f.veh;
        if (f.materialized() && !ferryValid(g, f)) {
            f.veh = -1;
            f.master = -1;
        }
        if (!f.materialized() && d < kFerryMat) spawnFerry(g, f, P);
        else if (f.materialized() && d > kFerryDemat && !aboard) despawnFerry(g, f);
        if (!f.materialized()) {
            f.dockLatch = P.docked;   // no dock events for an unseen ferry
            continue;
        }
        placeFerry(g, f, P);
        if (P.docked >= 0) dockEvents(g, f, P, aboard, listener);
        else {
            f.dockLatch = -1;
            if (aboard && f.annLatch != P.nextPier && P.toNext < 45.f) {
                f.annLatch = P.nextPier;
                speakFerry(g, "We will shortly be arriving at " + pierOf(P.nextPier).name + ".", g.vehicles[f.veh].sim.body.pos.toVec3(), true);
            }
        }
        for (size_t r = 0; r < f.cops.size();) {
            if (!pedValid(g, f.cops[r].ped, f.cops[r].uid) || g.peds[f.cops[r].ped].vehicle != f.veh) f.cops.erase(f.cops.begin() + r);
            else r++;
        }
    }
    updateFWalkers(g, dt);
    updatePierCrowds(g, dt, pp);
    ensureBlips(g, dt);
}

void submit(GameWorld& g) {
    if (!gF.init || gF.asset < 0 || !gF.farModel || !g.renderer || !g.renderer->dynamic) return;
    Render::DynamicRenderer* dyn = g.renderer->dynamic;
    dvec3 cam = g.rig.cam.pos;
    vec3 camF = g.rig.cam.forward();
    float clock = nowF(g);
    bool night = g.env && (g.env->timeOfDay < 6.9f || g.env->timeOfDay > 18.8f);
    const VehicleAsset& a = g.vassets[gF.asset];
    for (int i = 0; i < kFerries; i++) {
        const Ferry& f = gF.ferries[i];
        FerryPose P = poseAt(clock + f.phase);
        dvec3 pos;
        quat rot;
        poseTransform(P, (float)g.time + f.phase * 0.37f, pos, rot);
        vec3 toC = rel(pos, cam);
        float dist = length(toC);
        if (!f.materialized()) {
            // far ferry straight from the timetable
            if (dist > kFerryDraw || (dot(toC, camF) < -60.f && dist > 60.f)) continue;
            Render::DrawItem di;
            di.model = gF.farModel;
            di.pos = pos;
            di.rot = mat3FromQuat(rot);
            di.tint0 = vec4(a.spec.liveryPrimary, 0.08f);
            di.tint1 = vec4(a.spec.liverySecondary, 0.f);
            di.id = 0xB00000000ull | (u64)i;
            di.castShadow = dist < 600.f;
            dyn->submit(di);
        }
        // deck floodlights and cabin glow at night
        if (night && dist < 700.f) {
            mat3 R = mat3FromQuat(rot);
            Render::DynamicLight il;
            il.pos = pos + R * vec3(0.f, -1.f, 3.4f);
            il.color = vec3(1.f, 0.96f, 0.9f) * 900.f;
            il.radius = 16.f;
            g.renderer->addLight(il);
            Render::DynamicLight nl;
            nl.pos = pos + R * vec3(0.f, 5.f, 9.f);
            nl.color = vec3(1.f, 0.95f, 0.85f) * 400.f;
            nl.radius = 12.f;
            g.renderer->addLight(nl);
        }
    }
}

bool playerOnFerry() { return gF.ride >= 0; }

}  // namespace tf
}  // namespace Transit
}  // namespace Game
