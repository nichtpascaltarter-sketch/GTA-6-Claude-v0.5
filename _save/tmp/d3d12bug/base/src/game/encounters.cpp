// Street encounters: short events that happen near the player in free roam (a scooter thief, a store stick-up, a runaway
// bride, a hitchhiker with a secret, a crashed getaway van, a street race challenge, a carjacking, an armored van under
// attack, a runaway boat and a pop star chased by photographers). The director stages one out of view ahead of the
// player every few minutes; a blip appears once the player has seen the scene. Each has two ways to end (help or cash
// in, catch or let go), small rewards, voiced lines and a cooldown. They are not missions: shops, the phone and saves
// stay available, and a mission starting (or a switch, a death, an arrest) quietly ends the encounter.
// Spawning uses GameWorld::spawnPed / spawnVehicle with persistent entities (the population leaves them alone), the
// mission scripted drivers (scope 2) and GameWorld::attachTraffic when a car goes back to the ambient traffic.
#include "missions.h"

namespace Game {
namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Encounter kinds (the order is saved: EX_ENC_DONE bits)
enum EncKind : int {
    ENC_SNATCH = 0, ENC_STICKUP, ENC_BRIDE, ENC_HITCHHIKER, ENC_GETAWAY, ENC_CHALLENGE, ENC_CARJACK, ENC_ARMORED, ENC_BOAT,
    ENC_PAPARAZZI, ENC_COUNT
};

struct EncKindInfo {
    const char* id;     // --missiontest id
    const char* name;   // stats / logs
    int good;           // outcomes (bit 1 << outcome) that count as helping someone
};
const EncKindInfo kEncKinds[ENC_COUNT] = {
    {"enc_snatch", "Scooter Snatch", 2},     {"enc_stickup", "Stick-up", 2},          {"enc_bride", "Runaway Bride", 6},
    {"enc_hitchhiker", "Thumb Out", 4},      {"enc_getaway", "Crash Landing", 2},     {"enc_challenge", "Red Light Challenge", 0},
    {"enc_carjack", "Hot Seat", 2},          {"enc_armored", "Cash in Transit", 2},   {"enc_boat", "Adrift", 2},
    {"enc_paparazzi", "Flashbulbs", 2},
};

const u32 kColStreet = 0xffe8c890u;
const int kEncScope = 2;   // scripted drivers owned by encounters (ScriptDriver::scope)

struct EncRef {
    int id = -1;
    u32 uid = 0;
};

int encLive(GameWorld& g, const EncRef& r) {
    if (r.id < 0 || r.id >= (int)g.peds.size()) return -1;
    const Ped& p = g.peds[r.id];
    return p.used && p.uid == r.uid ? r.id : -1;
}

int encLiveVeh(GameWorld& g, const EncRef& r) {
    if (r.id < 0 || r.id >= (int)g.vehicles.size()) return -1;
    const Vehicle& v = g.vehicles[r.id];
    return v.used && v.uid == r.uid && !v.exploded ? r.id : -1;
}

bool encAlive(GameWorld& g, const EncRef& r) {
    int id = encLive(g, r);
    return id >= 0 && g.peds[id].health > 0.f;
}

// dead, gone, or on the ground (knocked down, getting up)
bool encDown(GameWorld& g, const EncRef& r) {
    int id = encLive(g, r);
    if (id < 0) return true;
    const Ped& p = g.peds[id];
    return p.health <= 0.f || p.state == PS_DEAD || p.state == PS_RAGDOLL || p.state == PS_GETUP;
}

int encGangChar(GameWorld& g, u32 seed) {
    const std::vector<int>& v = g.charsByRole[2];
    return v.empty() ? g.randomCivilianChar(seed & ~1u, 0) : v[seed % (u32)v.size()];
}

// Who stands where: a street place ahead of the player (along the motion, or the camera when still), near..far away,
// on a local street, on dry ground outside the buildings and (unless testing) out of the camera's view.
bool encStreetSpot(GameWorld& g, u32 seed, float nearR, float farR, bool test, Place& out) {
    vec3 pp = playerPos(g);
    int pv = g.playerVehicle();
    vec2 vel = pv >= 0 ? g.vehicles[pv].sim.body.vel.xy() : g.peds[g.player].vel.xy();
    vec2 fwd = length(vel) > 2.f ? normalize(vel) : dirFromYaw(g.rig.yaw);
    for (int k = 0; k < 14; k++) {
        u32 h = hash32(seed + (u32)k * 7919u);
        float ang = (hashToFloat(h) - 0.5f) * (k < 7 ? 1.4f : 3.2f);
        vec2 dir(fwd.x * cosf(ang) - fwd.y * sinf(ang), fwd.x * sinf(ang) + fwd.y * cosf(ang));
        float r = Lerp(nearR, farR, hashToFloat(h >> 9));
        Place p = resolvePlace(g, pp.xy() + dir * r);
        if (p.edge < 0) continue;
        float d = ::length(p.pos.xy() - pp.xy());
        if (d < nearR * 0.75f || d > farR * 1.35f) continue;
        if (g.map->isWater(p.pos.x, p.pos.y) || g.map->isWater(p.curb.x, p.curb.y)) continue;
        if (g.buildings && g.buildings->pointInBuilding(p.pos.xy(), 0.4f)) continue;
        if (World::gInteriors && World::gInteriors->at(p.pos + vec3(0.f, 0.f, 1.f)) >= 0) continue;
        if (fabsf(p.pos.z - pp.z) > 25.f) continue;
        if (!test && d < 170.f && g.inCameraView(p.pos + vec3(0.f, 0.f, 1.2f), 4.f)) continue;
        out = p;
        return true;
    }
    return false;
}

// The player can see this spot: close, or in the camera's view with a clear line to it.
bool encSees(GameWorld& g, vec3 p, float range = 85.f) {
    vec3 pp = playerPos(g);
    float d = ::length(p - pp);
    if (d < 28.f) return true;
    if (d > range) return false;
    if (!g.inCameraView(p + vec3(0.f, 0.f, 1.f), 2.f)) return false;
    return g.lineOfSight(g.rig.cam.pos, dvec3(p + vec3(0.f, 0.f, 1.3f)), g.player, g.playerVehicle());
}

// A voice for a stranger: a stock voice of the right gender, varied by the seed (the style tags colour it)
Audio::VoiceParams encVoice(bool female, u32 seed) { return Speech::presetVoice(female, seed); }

// ------------------------------------------------------------------------------------------------------------------
// One running encounter: its entities, blips, markers and objective (released when it ends).
class EncScript {
public:
    virtual ~EncScript() {}
    int kind = 0;
    int stage = 0;
    float stageT = 0.f, age = 0.f;
    bool seen = false;         // the player has seen the scene: its blips are on
    bool engaged = false;      // the player got involved: walking away now ends it as a failure
    int result = 0;            // 0 running, 1 / 2 the outcome, -1 failed (endText says why), -2 over without the player
    std::string endText;
    vec3 anchor;               // where the scene is (seen checks, walking away); scripts move it with the action
    u32 seed = 1;
    bool test = false;         // --missiontest drives the player's part
    int want = 1;              // ... toward this outcome
    std::vector<EncRef> peds, vehs;
    std::vector<int> pickups;  // package pickups placed by the encounter
    std::vector<Marker> markers;
    struct BlipReq {
        EncRef ped, veh;
        vec2 pos;
        UI::BlipIcon icon = UI::BLIP_FRIEND;
        u32 color = 0;
        const char* label = nullptr;
    };
    std::vector<BlipReq> blips;
    bool gpsOn = false;
    std::string objective;

    virtual bool setup(GameWorld& g) = 0;       // stages the scene near the player; false when there is no good spot
    virtual void update(GameWorld& g, float dt) = 0;
    virtual void autotest(GameWorld& g, MissionTest& t, float dt) = 0;
    virtual float leaveRange() const { return engaged ? 600.f : 290.f; }

    void setStage(int s) {
        stage = s;
        stageT = 0.f;
    }
    void next() { setStage(stage + 1); }

    EncRef addPed(GameWorld& g, int ci, vec3 pos, float yaw, Faction f) {
        EncRef r;
        if (ci < 0) return r;
        int id = g.spawnPed(ci, dvec3(pos), yaw, f);
        if (id < 0) return r;
        Ped& p = g.peds[id];
        p.persistent = true;
        p.brain.type = BRAIN_NONE;
        p.voice = encVoice(p.female, p.uid * 2654435761u + seed);
        r.id = id;
        r.uid = p.uid;
        peds.push_back(r);
        return r;
    }

    EncRef addVeh(GameWorld& g, int model, vec3 pos, float yaw, vec3 color = vec3(-1.f)) {
        EncRef r;
        if (model < 0) return r;
        bool water = g.vassets[model].spec.cls == Vehicles::VC_BOAT || g.vassets[model].spec.cls == Vehicles::VC_JETSKI ||
                     g.vassets[model].spec.cls == Vehicles::VC_AIRBOAT;
        int id = g.spawnVehicle(model, dvec3(pos + vec3(0.f, 0.f, water ? 0.3f : 0.35f)), yaw, false);
        if (id < 0) return r;
        Vehicle& v = g.vehicles[id];
        v.persistent = true;
        if (color.x >= 0.f) v.color0 = color;
        r.id = id;
        r.uid = v.uid;
        vehs.push_back(r);
        return r;
    }

    // a line from one of the encounter's people (positional, lip-synced); tags colour the stock voice
    void say(GameWorld& g, const EncRef& who, const char* name, const std::string& text, const char* tags = "", float pause = 0.25f) {
        int id = encLive(g, who);
        if (id < 0 || g.peds[id].health <= 0.f) return;
        DialogueLine l = line(name, text, id, kColStreet);
        l.hasVoice = true;
        l.voice = g.peds[id].voice;
        l.spoken = std::string(tags) + speakableText(text);
        l.pause = pause;
        g.mSay(l);
    }

    void obj(GameWorld& g, const std::string& t) {
        objective = t;
        g.mObjective(t);
    }

    void gps(GameWorld& g, vec2 p) {
        gpsOn = true;
        g.missionTargetActive = true;
        g.missionTarget = p;
        g.gpsRecalcTimer = 0.f;
    }

    void noGps(GameWorld& g) {
        if (gpsOn && !gMissions.active) {
            g.missionTargetActive = false;
            g.missionRoute.clear();
        }
        gpsOn = false;
    }

    void blipPed(const EncRef& p, UI::BlipIcon icon, const char* label = nullptr) {
        BlipReq b;
        b.ped = p;
        b.icon = icon;
        b.label = label;
        blips.push_back(b);
    }

    void blipVeh(const EncRef& v, UI::BlipIcon icon, const char* label = nullptr) {
        BlipReq b;
        b.veh = v;
        b.icon = icon;
        b.label = label;
        blips.push_back(b);
    }

    void blipAt(vec2 p, UI::BlipIcon icon, const char* label = nullptr, u32 color = 0) {
        BlipReq b;
        b.pos = p;
        b.icon = icon;
        b.label = label;
        b.color = color;
        blips.push_back(b);
    }

    void marker(vec3 p, float r, vec3 col = vec3(1.f, 0.85f, 0.2f)) {
        Marker m;
        m.pos = dvec3(p);
        m.radius = r;
        m.color = col;
        markers.push_back(m);
    }

    // the outcome: reward (or loss), a banner, the saved tallies
    void win(GameWorld& g, int outcome, long long cash, const char* title) {
        money(g, cash);
        if (cash > 0) setFlag(g, EX_ENC_EARNED, flag(g, EX_ENC_EARNED) + (int)cash);
        g.bigMessage(title, cash >= 0 ? StrFormat("$%lld", cash) : StrFormat("-$%lld", -cash), cash >= 0 ? 0xff33ccffu : 0xff3030ffu);
#ifdef HAVE_AUDIO
        Audio::play2D(cash >= 0 ? Audio::SFX_PICKUP_CASH : Audio::SFX_UI_ERROR, 0.8f);
#endif
        result = outcome;
    }

    void fail(const char* why) {
        if (result != 0) return;
        result = -1;
        endText = why;
    }

    // the player is in a vehicle within r of p, nearly stopped
    bool stoppedAt(GameWorld& g, vec3 p, float r) {
        int pv = g.playerVehicle();
        if (pv < 0) return false;
        return ::length(vehPos(g, pv).xy() - p.xy()) < r && vehicleSpeed(g, pv) < 3.f;
    }

    // a passenger of the player's current vehicle
    bool riding(GameWorld& g, const EncRef& who) {
        int id = encLive(g, who), pv = g.playerVehicle();
        return id >= 0 && pv >= 0 && g.peds[id].vehicle == pv;
    }

    // test: seat a ped in the player's vehicle
    void seatWithPlayer(GameWorld& g, const EncRef& who) {
        int id = encLive(g, who), pv = g.playerVehicle();
        if (id < 0 || pv < 0) return;
        int s = g.freeSeat(pv, false);
        if (s > 0) g.warpPedIntoVehicle(id, pv, s);
    }

    // test: kill every living ped of a list as the player
    void testKill(GameWorld& g, MissionTest& t, std::initializer_list<EncRef> list) {
        for (const EncRef& r : list) {
            int id = encLive(g, r);
            if (id >= 0 && g.peds[id].health > 0.f) t.shoot(id);
        }
    }
};

// A vehicle rolling to a stop by the curb with its hazard lights on
void encPark(GameWorld& g, int v) {
    if (v < 0) return;
    Vehicle& car = g.vehicles[v];
    car.ctl = Vehicles::VehicleControls();
    car.ctl.brake = 1.f;
    car.ctl.handbrake = true;
}

// A car the encounter no longer needs: a living driver takes it back into the traffic, else it stays parked
void encReleaseVeh(GameWorld& g, int v) {
    if (v < 0) return;
    releaseDriver(g, v);
    Vehicle& car = g.vehicles[v];
    car.persistent = false;
    car.indicator = 0;
    int d = car.seats[0];
    if (d >= 0 && !g.peds[d].isPlayer && g.peds[d].health > 0.f && !g.isBoat(v) && !g.isAircraft(v)) {
        g.peds[d].brain.type = BRAIN_DRIVER;
        g.attachTraffic(v);
    }
}

// ==================================================================================================================
// "Scooter Snatch": a thief on a scooter grabs a tourist's camera bag and rides off. Knock him off it, then give the
// bag back or keep the camera.
class EncSnatch : public EncScript {
public:
    EncRef victim, thief, scooter;
    Place P;
    vec2 T = vec2(0.f, 1.f);   // the scooter's direction past the victim
    int bag = -1;
    float bagDist0 = 0.f, stopT = 0.f;
    const char* vTags = "[accent:british:0.75]";

    float leaveRange() const override { return stage >= 4 ? 1e9f : EncScript::leaveRange(); }

    bool setup(GameWorld& g) override {
        if (!encStreetSpot(g, seed, test ? 55.f : 95.f, test ? 85.f : 150.f, test, P)) return false;
        int sm = pickModel(g, {Vehicles::VC_SCOOTER, Vehicles::VC_MOTORBIKE}, seed);
        if (sm < 0) return false;
        vec3 vp = placeOffset(g, P, 0.f, 0.3f);
        victim = addPed(g, g.randomCivilianChar(seed | 1u, 0), vp, yawTo(vp.xy(), P.curb.xy()), FAC_CIVILIAN);
        float yaw = 0.f;
        vec3 sp = approachSpot(g, P, 85.f * P.side, &yaw);
        scooter = addVeh(g, sm, sp, yaw, lin(0.9f, 0.82f, 0.18f));
        thief = addPed(g, encGangChar(g, seed >> 3), sp, yaw, FAC_CIVILIAN);
        if (victim.id < 0 || scooter.id < 0 || thief.id < 0) return false;
        g.warpPedIntoVehicle(thief.id, scooter.id, 0);
        encPark(g, scooter.id);
        g.peds[victim.id].animIn.stance = 14;   // looking around, map in hand
        T = normalize(P.pos.xy() - sp.xy() + vec2(1e-3f, 0.f));
        anchor = vp;
        blipPed(victim, UI::BLIP_FRIEND, "Tourist");
        return true;
    }

    void snatch(GameWorld& g) {
        int v = encLive(g, victim), sc = encLiveVeh(g, scooter);
        if (v >= 0) {
            g.peds[v].pendingAction = Anim::CLIP_STAGGER;
            g.peds[v].animIn.stance = 4;
        }
        say(g, victim, "Tourist", "[scared]Hey! My bag! He took my camera bag! Somebody stop him!", vTags);
        say(g, thief, "Thief", "[happy]Thanks for the camera, tourist!", "[accent:latino:0.5]");
        ScriptDriver& d = driveRoad(g, sc, P.pos.xy() + T * 760.f, 17.f, true, kEncScope);
        d.rubberPed = g.player;
        d.rubberGap = 42.f;
        engaged = true;
        blips.clear();
        blipPed(thief, UI::BLIP_ENEMY, "Bag thief");
        obj(g, "Stop the ~r~bag thief~s~.");
        score(SC_CHASE, 0.75f, 5);
        setStage(2);
    }

    void update(GameWorld& g, float dt) override {
        int v = encLive(g, victim), th = encLive(g, thief), sc = encLiveVeh(g, scooter);
        if (v < 0 || g.peds[v].health <= 0.f) {
            if (engaged) fail("The tourist got hurt.");
            else result = -2;
            return;
        }
        switch (stage) {
            case 0:
                anchor = pedPos(g, v);
                if (sc < 0 || th < 0) {
                    result = -2;
                    break;
                }
                if (g.playerAt(anchor.xy(), 62.f) || (test && stageT > 1.f)) {
                    driveRoad(g, sc, P.pos.xy() + T * 70.f, 11.f, false, kEncScope);
                    next();
                }
                break;
            case 1: {
                if (sc < 0 || th < 0 || g.peds[th].health <= 0.f) {
                    result = -2;
                    break;
                }
                vec3 spos = vehPos(g, sc);
                if (stageT > (test ? 4.f : 16.f) && ::length(spos.xy() - anchor.xy()) > 24.f) {
                    float yaw = 0.f;
                    vec3 p = approachSpot(g, P, 18.f * P.side, &yaw);
                    teleportVehicle(g, sc, p, yaw);
                    g.vehicles[sc].sim.body.vel = vec3(dirFromYaw(yaw) * 9.f, 0.f);
                }
                if (::length(spos.xy() - anchor.xy()) < 8.f || stageT > 30.f) snatch(g);
                break;
            }
            case 2: {
                if (th >= 0) anchor = pedPos(g, th);
                bool thiefDown = encDown(g, thief);
                bool offBike = th >= 0 && g.peds[th].vehicle != sc;
                bool bikeDead = sc < 0 || vehicleDisabled(g, sc);
                ScriptDriver* d = sc >= 0 ? driverFor(sc) : nullptr;
                stopT = sc >= 0 && vehicleSpeed(g, sc) < 1.5f && stageT > 4.f ? stopT + dt : 0.f;
                float dist = ::length(anchor - playerPos(g));
                if (d && d->done && dist > 90.f) {
                    fail("The thief got away with the camera bag.");
                    break;
                }
                if (thiefDown || offBike || bikeDead || stopT > 3.f || (d && d->done)) {
                    if (sc >= 0) releaseDriver(g, sc);
                    vec3 at = th >= 0 ? pedPos(g, th) : vehPos(g, sc);
                    if (th >= 0 && g.peds[th].health > 0.f) {
                        if (g.peds[th].vehicle >= 0) g.removePedFromVehicle(th, true);
                        setFlee(g, th, g.player);
                        say(g, thief, "Thief", "[scared]Take it! Take the stupid camera bag!", "[accent:latino:0.5]");
                    }
                    bag = spawnPackage(g, at + vec3(0.9f, 0.5f, 0.3f));
                    pickups.push_back(bag);
                    blips.clear();
                    blipAt(at.xy(), UI::BLIP_OBJECTIVE, "Camera bag");
                    marker(at + vec3(0.9f, 0.5f, 0.f), 0.9f, vec3(0.3f, 1.f, 0.4f));
                    obj(g, "Pick up the ~g~camera bag~s~.");
                    setStage(3);
                    break;
                }
                if (dist > 380.f || stageT > 200.f) fail("The thief got away with the camera bag.");
                break;
            }
            case 3:
                if (bag >= 0 && (packageTaken(g, bag) || grabNear(g, bag, 2.2f))) {
                    bagDist0 = ::length(playerPos(g).xy() - pedPos(g, v).xy());
                    markers.clear();
                    blips.clear();
                    blipPed(victim, UI::BLIP_FRIEND, "Tourist");
                    gps(g, pedPos(g, v).xy());
                    setIdle(g, v, 14);
                    obj(g, "Give the camera bag back to the ~b~tourist~s~, or keep it.");
                    next();
                } else if (stageT > 120.f) {
                    fail("Somebody else walked off with the camera bag.");
                }
                break;
            case 4: {
                vec3 vp = pedPos(g, v);
                anchor = vp;
                float d = ::length(playerPos(g).xy() - vp.xy());
                int pv = g.playerVehicle();
                if (d < 22.f && d > 2.5f && pv < 0) setGoto(g, v, playerPos(g), 1.5f);
                else if (g.peds[v].brain.type == BRAIN_GOTO) setIdle(g, v, 0);
                bool close = pv < 0 ? d < 3.2f : (d < 7.f && vehicleSpeed(g, pv) < 2.f);
                if (close) {
                    facePed(g, v, playerPos(g));
                    say(g, victim, "Tourist", "[happy]You got it back! Every photo from my trip is on that card. Please, take this.", vTags);
                    win(g, 1, 250, "CAMERA BAG RETURNED");
                } else if (d > Max(bagDist0 + 220.f, 300.f)) {
                    sayMe(g, "[calm]Nice camera. The pawn shop on Ninth is going to love it.");
                    win(g, 2, 600, "CAMERA BAG KEPT");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        int v = encLive(g, victim), sc = encLiveVeh(g, scooter);
        switch (stage) {
            case 2:
                if (stageT > 1.5f && sc >= 0) g.vehicles[sc].sim.engineHealth = 0.f;   // the scooter dies under him
                break;
            case 3:
                if (stageT > 0.6f && bag >= 0 && !packageTaken(g, bag)) t.teleport(g.pickups[bag].pos.toVec3() + vec3(0.5f, 0.f, 0.f), 0.f);
                break;
            case 4:
                if (stageT > 0.8f && v >= 0) {
                    if (want == 2) t.teleportNear(pedPos(g, v).xy(), 340.f);
                    else t.teleport(pedPos(g, v) + vec3(1.6f, 0.f, 0.f), 0.f);
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Stick-up": a masked robber runs out of a corner store with the till and a pistol, into a waiting car. Stop them,
// then take the cash back to the clerk or keep it (the clerk calls the police).
class EncStickup : public EncScript {
public:
    EncRef robber, clerk, driver, car;
    Place P;
    vec3 door;
    std::string store;
    int bag = -1;
    float bagDist0 = 0.f;
    const char* rTags = "[accent:latino:0.6][gravelly]";
    const char* cTags = "[accent:caribbean:0.5]";

    float leaveRange() const override { return stage >= 4 ? 1e9f : (engaged ? 700.f : 320.f); }

    bool setup(GameWorld& g) override {
        if (!World::gInteriors) return false;
        vec3 pp = playerPos(g);
        int best = -1;
        float bestD = 1e9f;
        for (int n = 0; n < 16; n++) {
            if (World::gInteriors->byKind(World::IK_CONVENIENCE, n) < 0) break;
            Place pl;
            if (!placeFromInterior(g, nullptr, World::IK_CONVENIENCE, n, pl)) continue;
            float d = ::length(pl.door.xy() - pp.xy());
            if (d < (test ? 30.f : 140.f) || d > (test ? 420.f : 520.f)) continue;
            if (!test && d < 220.f && g.inCameraView(pl.door + vec3(0.f, 0.f, 1.f), 3.f)) continue;
            if (d < bestD) {
                bestD = d;
                best = n;
                P = pl;
            }
        }
        if (best < 0) return false;
        store = World::gInteriors->defs[World::gInteriors->byKind(World::IK_CONVENIENCE, best)].name;
        door = P.door;
        float yaw = P.curbYaw;
        vec3 cp = curbOffset(g, P, 15.f, &yaw);
        car = addVeh(g, pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_COMPACT}, seed), cp, yaw, lin(0.07f, 0.07f, 0.08f));
        driver = addPed(g, encGangChar(g, seed), cp, yaw, FAC_CIVILIAN);
        if (car.id < 0 || driver.id < 0) return false;
        g.warpPedIntoVehicle(driver.id, car.id, 0);
        encPark(g, car.id);
        anchor = door;
        return true;
    }

    void dropBag(GameWorld& g, vec3 at) {
        if (bag >= 0) return;
        bag = spawnPackage(g, at + vec3(0.6f, 0.4f, 0.3f));
        pickups.push_back(bag);
        blips.clear();
        blipAt(at.xy(), UI::BLIP_OBJECTIVE, "Stolen cash");
        marker(at + vec3(0.6f, 0.4f, 0.f), 0.9f, vec3(0.3f, 1.f, 0.4f));
        obj(g, "Pick up the ~g~stolen cash~s~.");
        int d = encLive(g, driver);
        if (d >= 0 && g.peds[d].health > 0.f) {
            if (g.peds[d].vehicle >= 0) g.removePedFromVehicle(d, true);
            setFlee(g, d, g.player);
        }
        setStage(3);
    }

    void update(GameWorld& g, float dt) override {
        (void)dt;
        int r = encLive(g, robber), c = encLive(g, clerk), cv = encLiveVeh(g, car);
        switch (stage) {
            case 0: {
                vec3 pp = playerPos(g);
                bool inside = World::gInteriors && World::gInteriors->at(pp + vec3(0.f, 0.f, 1.f)) >= 0;
                if ((g.playerAt(door.xy(), 72.f) && !inside) || (test && stageT > 1.f)) {
                    vec3 out = door - vec3(P.outward * 0.8f, 0.f);
                    robber = addPed(g, encGangChar(g, seed * 3u + 1u), out, yawTo(out.xy(), vehPos(g, cv).xy()), FAC_CIVILIAN);
                    r = robber.id;
                    if (r < 0 || cv < 0) {
                        result = -2;
                        break;
                    }
                    arm(g, r, WPN_PISTOL, 4);
                    g.peds[r].brain.accuracy = 0.18f;
                    vec3 side = vehPos(g, cv) + g.vehicles[cv].sim.right() * 1.7f;
                    setGoto(g, r, side, 5.2f);
                    say(g, robber, "Robber", "[shout]Nobody move! Nobody follow me!", rTags);
                    engaged = true;
                    blipPed(robber, UI::BLIP_ENEMY, "Robber");
                    obj(g, "Stop the ~r~robber~s~.");
                    score(SC_CHASE, 0.8f, 6);
                    next();
                }
                break;
            }
            case 1:   // the robber runs to the car; the clerk comes out after him
                if (stageT > 2.2f && clerk.id < 0) {
                    clerk = addPed(g, g.randomCivilianChar(seed * 5u + 2u, 0), door, yawTo(door.xy(), pedPos(g, r).xy()), FAC_CIVILIAN);
                    c = clerk.id;
                    if (c >= 0) g.peds[c].animIn.stance = 17;
                    say(g, clerk, "Clerk", "[shout]Stop! He robbed me! He's got the whole till!", cTags);
                }
                if (r < 0 || encDown(g, robber)) {
                    dropBag(g, r >= 0 ? pedPos(g, r) : door);
                    break;
                }
                anchor = pedPos(g, r);
                if (cv >= 0 && (::length(pedPos(g, r).xy() - vehPos(g, cv).xy()) < 2.8f || stageT > 7.f)) {
                    g.warpPedIntoVehicle(r, cv, 1);
                    setCombat(g, r, g.player, 0.18f);
                    ScriptDriver& d = driveRoad(g, cv, P.curb.xy() + P.streetDir * 820.f, 21.f, true, kEncScope);
                    d.rubberPed = g.player;
                    d.rubberGap = 55.f;
                    blips.clear();
                    blipVeh(car, UI::BLIP_ENEMY, "Getaway car");
                    obj(g, "Stop the ~r~getaway car~s~.");
                    next();
                }
                break;
            case 2: {   // the car chase
                if (cv < 0) {
                    dropBag(g, anchor);
                    break;
                }
                anchor = vehPos(g, cv);
                int d = g.driverOf(cv);
                bool stopped = vehicleDisabled(g, cv) || g.vehicles[cv].sim.health < 380.f || d < 0 || g.peds[d].health <= 0.f;
                ScriptDriver* sd = driverFor(cv);
                float dist = ::length(anchor - playerPos(g));
                if (sd && sd->done && dist > 110.f) {
                    fail("The robbers got away.");
                    break;
                }
                if (stopped || (sd && sd->done)) {
                    releaseDriver(g, cv);
                    if (r >= 0 && g.peds[r].health > 0.f && g.peds[r].vehicle >= 0) {
                        g.removePedFromVehicle(r, true);
                        setCombat(g, r, g.player, 0.2f);
                        say(g, robber, "Robber", "[angry]You want it? Come and take it!", rTags);
                        setStage(5);
                    } else {
                        dropBag(g, anchor + g.vehicles[cv].sim.right() * 2.2f);
                    }
                    int dr = encLive(g, driver);
                    if (dr >= 0 && g.peds[dr].health > 0.f) {
                        if (g.peds[dr].vehicle >= 0) g.removePedFromVehicle(dr, true);
                        setFlee(g, dr, g.player);
                    }
                    break;
                }
                if (dist > 460.f || stageT > 220.f) fail("The robbers got away.");
                break;
            }
            case 5:   // the robber fights on foot
                if (r >= 0) anchor = pedPos(g, r);
                if (encDown(g, robber)) dropBag(g, anchor);
                else if (::length(anchor - playerPos(g)) > 300.f) fail("The robber got away.");
                break;
            case 3:
                if (bag >= 0 && (packageTaken(g, bag) || grabNear(g, bag, 2.2f))) {
                    vec3 cp = c >= 0 ? pedPos(g, c) : door;
                    bagDist0 = ::length(playerPos(g).xy() - cp.xy());
                    markers.clear();
                    blips.clear();
                    if (c < 0) {
                        clerk = addPed(g, g.randomCivilianChar(seed * 5u + 2u, 0), door, 0.f, FAC_CIVILIAN);
                        c = clerk.id;
                    }
                    blipPed(clerk, UI::BLIP_FRIEND, "Clerk");
                    gps(g, cp.xy());
                    if (c >= 0) setIdle(g, c, 0);
                    obj(g, StrFormat("Take the cash back to the ~b~clerk~s~ at %s, or keep it.", store.c_str()));
                    setStage(4);
                } else if (stageT > 120.f) {
                    fail("Somebody else picked up the cash.");
                }
                break;
            case 4: {
                vec3 cp = c >= 0 ? pedPos(g, c) : door;
                anchor = cp;
                float d = ::length(playerPos(g).xy() - cp.xy());
                bool close = g.playerVehicle() < 0 ? d < 3.5f : stoppedAt(g, cp, 8.f);
                if (c >= 0 && d < 30.f) facePed(g, c, playerPos(g));
                if (close) {
                    say(g, clerk, "Clerk", "[happy]You got it back? Every dollar! Coffee's on the house. For a month. Maybe two.", cTags);
                    g.socialReport(UI::TE_ROBBERY, dvec3(door), store.c_str(), 900.f);
                    win(g, 1, 350, "TILL RETURNED");
                } else if (d > Max(bagDist0 + 220.f, 300.f)) {
                    sayMe(g, "[calm]Finders keepers.");
                    setWanted(g, 1);
                    win(g, 2, 900, "TILL KEPT");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        int cv = encLiveVeh(g, car), c = encLive(g, clerk);
        switch (stage) {
            case 2:
                if (stageT > 2.f && cv >= 0) {
                    g.vehicles[cv].sim.engineHealth = 0.f;
                    g.vehicles[cv].sim.health = 300.f;
                }
                break;
            case 5:
                if (stageT > 1.f) testKill(g, t, {robber});
                break;
            case 3:
                if (stageT > 0.6f && bag >= 0 && !packageTaken(g, bag)) t.teleport(g.pickups[bag].pos.toVec3() + vec3(0.5f, 0.f, 0.f), 0.f);
                break;
            case 4:
                if (stageT > 0.8f) {
                    vec3 cp = c >= 0 ? pedPos(g, c) : door;
                    if (want == 2) t.teleportNear(cp.xy(), 340.f);
                    else t.teleport(cp - vec3(P.outward * 1.8f, 0.f), 0.f);
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Runaway Bride": her car died on the way to her wedding. Halfway there she is not so sure: drive her to the chapel
// before the ceremony, or to the airport.
int brideChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0xB41DEu;
    d.gender = Anim::FEMALE;
    d.height = 1.66f;
    d.weight = 0.32f;
    d.muscle = 0.25f;
    d.age = 0.14f;
    d.skinTone = vec3(0.64f, 0.46f, 0.34f);
    d.ancestry = 0;
    d.hairStyle = 6;
    d.hairColor = vec3(0.05f, 0.035f, 0.025f);
    d.top = 12;   // sundress, bridal white
    d.topColor = lin(0.97f, 0.96f, 0.93f);
    d.bottom = 4;
    d.bottomColor = lin(0.97f, 0.96f, 0.93f);
    d.shoes = 5;
    d.shoeColor = lin(0.95f, 0.94f, 0.9f);
    d.bag = 2;
    d.bagColor = lin(0.9f, 0.88f, 0.84f);
    return g.namedCharacter("enc_bride", d);
}

class EncBride : public EncScript {
public:
    EncRef bride, car;
    Place P, chapel, airport;
    std::string chapelName;
    float clock = 0.f, dist0 = 1.f;
    bool chapelOpen = true, asked = false, onFootWarned = false;
    float outT = 0.f;
    const char* bTags = "[accent:latino:0.55][bright]";

    float leaveRange() const override { return stage >= 1 ? 1e9f : 280.f; }

    bool setup(GameWorld& g) override {
        if (g.playerVehicle() < 0 && !test) return false;
        if (!encStreetSpot(g, seed, test ? 55.f : 120.f, test ? 90.f : 200.f, test, P)) return false;
        // the chapel: a church 700 m to 2.4 km from her
        const World::Building* best = nullptr;
        float bestScore = 1e9f;
        if (g.buildings)
            for (const World::Building& b : g.buildings->buildings) {
                if (b.style != World::BS_CHURCH) continue;
                float d = ::length(b.c - P.pos.xy());
                if (d < 700.f || d > 2400.f) continue;
                float s = fabsf(d - 1300.f);
                if (s < bestScore) {
                    bestScore = s;
                    best = &b;
                }
            }
        if (best) {
            chapel = resolvePlace(g, best->c + best->front * (best->hy + 6.f));
            chapelName = "the chapel";
        } else {
            chapel = gPlaces.midtownPark;
            chapelName = "the Midtown Park gazebo";
        }
        airport = gPlaces.airport;
        if (chapel.edge < 0) return false;
        float yaw = P.curbYaw;
        vec3 cp = P.curb;
        car = addVeh(g, pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SEDAN}, seed), cp, yaw, lin(0.92f, 0.9f, 0.84f));
        vec3 bp = placeOffset(g, P, 1.2f, 0.2f);
        bride = addPed(g, brideChar(g), bp, yawTo(bp.xy(), P.curb.xy()), FAC_CIVILIAN);
        if (car.id < 0 || bride.id < 0) return false;
        Vehicle& v = g.vehicles[car.id];
        v.indicator = 2;
        v.sim.engineHealth = 160.f;   // smoking
        v.sim.engineOn = false;
        v.parked = true;
        encPark(g, car.id);
        anchor = bp;
        blipPed(bride, UI::BLIP_FRIEND, "Bride");
        return true;
    }

    void showChoice(GameWorld& g) {
        markers.clear();
        blips.clear();
        if (chapelOpen) {
            marker(chapel.curb, 3.f);
            blipAt(chapel.curb.xy(), UI::BLIP_OBJECTIVE, "Chapel");
        }
        marker(airport.curb, 3.f, vec3(0.3f, 0.7f, 1.f));
        blipAt(airport.curb.xy(), UI::BLIP_AIRPORT, "Airport");
        gps(g, (chapelOpen ? chapel : airport).curb.xy());
    }

    void update(GameWorld& g, float dt) override {
        int b = encLive(g, bride), pv = g.playerVehicle();
        if (b < 0 || g.peds[b].health <= 0.f) {
            if (engaged) fail("The bride got hurt.");
            else result = -2;
            return;
        }
        if (stage >= 1 && stage <= 3) {
            if (chapelOpen) {
                clock -= dt;
                g.missionTimerHud = Max(0.f, clock);
                if (clock <= 0.f) {
                    chapelOpen = false;
                    g.missionTimerHud = -1.f;
                    say(g, bride, "Bride", "[calm]Well. That settles it, doesn't it? The ceremony started without me. The airport, then.", bTags);
                    showChoice(g);
                    obj(g, "Take the bride to the ~b~airport~s~.");
                    if (stage == 1) setStage(2);
                }
            }
            if (!riding(g, bride)) {
                outT += dt;
                if (g.peds[b].vehicle < 0 && g.peds[b].brain.type != BRAIN_FOLLOW) setFollow(g, b, g.player);
                if (outT > 25.f && ::length(pedPos(g, b) - playerPos(g)) > 50.f) fail("The bride found another ride.");
            } else {
                outT = 0.f;
            }
            if (pv >= 0 && g.vehicles[pv].sim.health < 250.f) {
                fail("You scared the bride half to death. She'd rather walk.");
                return;
            }
        }
        switch (stage) {
            case 0: {
                vec3 bp = pedPos(g, b);
                anchor = bp;
                float d = ::length(playerPos(g).xy() - bp.xy());
                if (d < 55.f && g.peds[b].animIn.stance != 15 && g.peds[b].brain.type == BRAIN_NONE) g.peds[b].animIn.stance = 15;   // waving
                if (pv < 0 && d < 9.f && !onFootWarned) {
                    onFootWarned = true;
                    say(g, bride, "Bride", "[scared]Unless you can carry me four miles in these shoes, I need a car. Please, find a car!", bTags);
                }
                if (pv >= 0 && d < 15.f && vehicleSpeed(g, pv) < 3.f && !asked) {
                    asked = true;
                    say(g, bride, "Bride", "[scared]Oh thank God. My car just died and I'm getting married in twenty minutes!", bTags);
                    say(g, bride, "Bride", StrFormat("[scared:0.6]Please, can you take me to %s? I'll pay, I swear.", chapelName.c_str()), bTags);
                    setFollow(g, b, g.player);
                    engaged = true;
                }
                if (riding(g, bride)) {
                    dist0 = Max(200.f, ::length(chapel.curb.xy() - playerPos(g).xy()));
                    clock = dist0 / 12.f + 55.f;
                    marker(chapel.curb, 3.f);
                    blips.clear();
                    blipAt(chapel.curb.xy(), UI::BLIP_OBJECTIVE, "Chapel");
                    gps(g, chapel.curb.xy());
                    obj(g, StrFormat("Get the bride to ~y~%s~s~ before the ceremony.", chapelName.c_str()));
                    score(SC_CHASE, 0.45f, 7);
                    next();
                }
                break;
            }
            case 1: {   // on the way, until the doubts
                anchor = playerPos(g);
                float left = ::length(chapel.curb.xy() - playerPos(g).xy());
                if (left < dist0 * 0.62f || stageT > 32.f) {
                    say(g, bride, "Bride", "[sad]Can I tell you something? I don't think I love him. I think I love his mother's house in Key Coral.", bTags);
                    sayMe(g, "[calm]That's... a lot to unpack in a borrowed car.");
                    say(g, bride, "Bride", "[scared:0.4]The airport. Take me to the airport. No. I don't know. You decide. Please.", bTags);
                    showChoice(g);
                    obj(g, "The ~y~chapel~s~ or the ~b~airport~s~? Your call.");
                    next();
                }
                break;
            }
            case 2: {   // either destination
                anchor = playerPos(g);
                if (!riding(g, bride)) break;
                if (chapelOpen && stoppedAt(g, chapel.curb, 12.f)) {
                    g.missionTimerHud = -1.f;
                    g.removePedFromVehicle(b, true);
                    setGoto(g, b, chapel.door, 2.4f);
                    say(g, bride, "Bride", "[happy]I'm doing it. I'm really doing it! Here, from the gift envelope. Don't tell anyone.", bTags);
                    win(g, 1, 700, "WEDDING SAVED");
                } else if (stoppedAt(g, airport.curb, 16.f)) {
                    g.missionTimerHud = -1.f;
                    g.removePedFromVehicle(b, true);
                    setGoto(g, b, airport.door, 1.8f);
                    say(g, bride, "Bride", "[happy:0.6]Tell Eduardo I'm sorry. Actually, don't tell him anything. Here, my honeymoon money. I won't need it.", bTags);
                    win(g, 2, 1400, "BRIDE ON THE RUN");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        int b = encLive(g, bride), pv = g.playerVehicle();
        switch (stage) {
            case 0:
                if (stageT > 0.8f && pv >= 0 && b >= 0 && !riding(g, bride)) {
                    vec3 bp = pedPos(g, b);
                    t.teleport(vec3(P.curb.xy() - P.streetDir * 9.f, P.curb.z), P.curbYaw);
                    t.stopVehicle();
                    if (stageT > 2.5f) seatWithPlayer(g, bride);
                    (void)bp;
                }
                break;
            case 1:
                stageT += 30.f;   // skip the drive to the doubts
                break;
            case 2:
                if (stageT > 1.f) {
                    const Place& to = want == 2 ? airport : chapel;
                    t.teleport(to.curb, to.curbYaw);
                    t.stopVehicle();
                }
                break;
            default: break;
        }
    }

};

// ==================================================================================================================
// "Thumb Out": a hitchhiker on a country road. The radio news mentions an inmate who walked off a road crew in an
// orange shirt; drive him to his cousin's place and he pays, or drive him to the sheriff and catch him when he runs.
int hitchChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0x41C4u;
    d.gender = Anim::MALE;
    d.height = 1.83f;
    d.weight = 0.4f;
    d.muscle = 0.6f;
    d.age = 0.3f;
    d.skinTone = vec3(0.8f, 0.62f, 0.5f);
    d.ancestry = 2;
    d.hairStyle = 2;
    d.hairColor = vec3(0.35f, 0.24f, 0.13f);
    d.facialHair = 0;
    d.top = 0;
    d.topColor = lin(0.95f, 0.45f, 0.08f);   // prison-issue orange under the jacket
    d.outer = 3;
    d.outerColor = lin(0.28f, 0.33f, 0.42f);
    d.bottom = 2;
    d.bottomColor = lin(0.38f, 0.36f, 0.28f);
    d.shoes = 2;
    d.shoeColor = lin(0.25f, 0.18f, 0.12f);
    d.hat = 0;
    d.bag = 0;
    d.bagColor = lin(0.2f, 0.22f, 0.18f);
    return g.namedCharacter("enc_hitchhiker", d);
}

class EncHitchhiker : public EncScript {
public:
    EncRef wade;
    Place P, dest;
    vec2 sheriff;
    const char* sheriffName = "Sheriff's office";
    float dist0 = 1.f, outT = 0.f, caughtT = 0.f;
    bool asked = false;
    const char* wTags = "[accent:south:0.8][gravelly]";

    float leaveRange() const override { return stage >= 1 ? 1e9f : 300.f; }

    bool setup(GameWorld& g) override {
        if (g.playerVehicle() < 0 && !test) return false;
        if (!encStreetSpot(g, seed, test ? 60.f : 150.f, test ? 100.f : 250.f, test, P)) return false;
        // his cousin's place: a road 1.2-2.2 km on
        bool found = false;
        for (int k = 0; k < 12 && !found; k++) {
            u32 h = hash32(seed * 31u + (u32)k);
            float ang = hashToFloat(h) * kTwoPi, r = 1200.f + hashToFloat(h >> 8) * 1000.f;
            vec2 q = P.pos.xy() + vec2(cosf(ang), sinf(ang)) * r;
            if (q.x < -World::kWorldHalf + 400.f || q.x > World::kWorldHalf - 400.f || q.y < -World::kWorldHalf + 400.f || q.y > World::kWorldHalf - 400.f) continue;
            Place d = resolvePlace(g, q);
            if (d.edge < 0 || g.map->isWater(d.pos.x, d.pos.y) || ::length(d.pos.xy() - P.pos.xy()) < 900.f) continue;
            dest = d;
            found = true;
        }
        if (!found) return false;
        // the nearest sheriff / police station on the map
        float bd = 1e9f;
        sheriff = gPlaces.policeHq.curb.xy();
        for (const UI::Blip& b : g.staticBlips)
            if (b.icon == UI::BLIP_POLICE_STATION) {
                float d = ::length(b.pos - P.pos.xy());
                if (d < bd) {
                    bd = d;
                    sheriff = b.pos;
                    sheriffName = b.label ? b.label : "Sheriff's office";
                }
            }
        vec3 wp = placeOffset(g, P, 0.f, -0.2f);
        wade = addPed(g, hitchChar(g), wp, yawTo(wp.xy(), P.curb.xy()), FAC_CIVILIAN);
        if (wade.id < 0) return false;
        anchor = wp;
        blipPed(wade, UI::BLIP_FRIEND, "Hitchhiker");
        return true;
    }

    void runsFor(GameWorld& g) {
        int w = encLive(g, wade);
        if (w < 0) return;
        if (g.peds[w].vehicle >= 0) g.removePedFromVehicle(w, true);
        setFlee(g, w, g.player);
        g.peds[w].brain.speed = 5.f;
        say(g, wade, "Wade", "[shout]The sheriff? No! No, no, no! I'm not going back!", wTags);
        markers.clear();
        blips.clear();
        noGps(g);
        blipPed(wade, UI::BLIP_ENEMY, "Wade");
        obj(g, "Stop ~r~Wade~s~ before he gets away. Knock him down or hold him at gunpoint.");
        score(SC_CHASE, 0.85f, 8);
        setStage(3);
    }

    void update(GameWorld& g, float dt) override {
        int w = encLive(g, wade), pv = g.playerVehicle();
        if (w < 0 || (g.peds[w].health <= 0.f && stage < 3)) {
            if (engaged) fail("Wade didn't make it.");
            else result = -2;
            return;
        }
        if (stage == 1 || stage == 2) {
            if (!riding(g, wade)) {
                outT += dt;
                if (g.peds[w].vehicle < 0 && g.peds[w].brain.type != BRAIN_FOLLOW) setFollow(g, w, g.player);
                if (outT > 30.f) {
                    fail("Wade got tired of waiting and hitched another ride.");
                    return;
                }
            } else {
                outT = 0.f;
            }
        }
        switch (stage) {
            case 0: {
                vec3 wp = pedPos(g, w);
                anchor = wp;
                float d = ::length(playerPos(g).xy() - wp.xy());
                if (d < 60.f && g.peds[w].brain.type == BRAIN_NONE) g.peds[w].animIn.stance = 15;   // thumb out, waving
                if (pv >= 0 && d < 14.f && vehicleSpeed(g, pv) < 3.f && !asked) {
                    asked = true;
                    say(g, wade, "Wade", "[happy:0.3]Appreciate you stopping, friend. My cousin's got a place up the road a ways. Name's Wade.", wTags);
                    setFollow(g, w, g.player);
                    engaged = true;
                }
                if (riding(g, wade)) {
                    dist0 = Max(300.f, ::length(dest.curb.xy() - playerPos(g).xy()));
                    marker(dest.curb, 3.f);
                    blips.clear();
                    blipAt(dest.curb.xy(), UI::BLIP_OBJECTIVE, "Wade's cousin");
                    gps(g, dest.curb.xy());
                    obj(g, "Drive ~b~Wade~s~ to his cousin's place.");
                    next();
                }
                break;
            }
            case 1: {   // the radio news
                anchor = playerPos(g);
                float left = ::length(dest.curb.xy() - playerPos(g).xy());
                if (left < dist0 * 0.7f || stageT > 34.f) {
                    narrator(g, "Radio news",
                             "Palmera deputies are searching for an inmate who walked away from a road crew near Cypress Ridge this morning. "
                             "He was last seen in an orange work shirt. Motorists are asked not to pick up hitchhikers.",
                             "newsreader_female", "[news]");
                    sayMe(g, "[calm]Orange shirt, huh.");
                    say(g, wade, "Wade", "[calm]Lot of folks wear orange. Hunters. Traffic cones. Just keep driving, and we both have a nice day.", wTags);
                    arm(g, w, WPN_KNIFE, 1);
                    blipAt(sheriff, UI::BLIP_POLICE_STATION, sheriffName);
                    marker(vec3(sheriff, groundAt(g, sheriff.x, sheriff.y)), 4.f, vec3(0.3f, 0.6f, 1.f));
                    obj(g, "Drive Wade to ~y~his cousin's place~s~, or to the ~b~sheriff~s~.");
                    score(SC_STEALTH, 0.5f, 8);
                    next();
                }
                break;
            }
            case 2:
                anchor = playerPos(g);
                if (!riding(g, wade)) break;
                if (stoppedAt(g, dest.curb, 15.f)) {
                    g.removePedFromVehicle(w, true);
                    setGoto(g, w, placeOffset(g, dest, 30.f, 6.f), 1.4f);
                    say(g, wade, "Wade", "[calm]You never saw me. Here, for the gas. And your trouble.", wTags);
                    sayMe(g, "[calm]Saw who?");
                    win(g, 1, 400, "WADE DROPPED OFF");
                } else if (::length(playerPos(g).xy() - sheriff) < 110.f) {
                    runsFor(g);
                }
                break;
            case 3: {   // the chase on foot
                vec3 wp = pedPos(g, w);
                anchor = wp;
                Ped* pl = g.playerPed();
                float d = ::length(playerPos(g) - wp);
                bool aimed = false;
                if (pl && pl->aiming && pl->weapon != WPN_FISTS && d < 16.f) {
                    vec3 to = g.pedChestPos(g.peds[w]) - g.pedHeadPos(*pl);
                    float along = dot(to, pl->aimDir);
                    aimed = along > 0.f && ::length(to - pl->aimDir * along) < 1.6f;
                }
                bool caught = encDown(g, wade) || aimed || (d < 1.8f && pl && pl->state == PS_ONFOOT);
                if (caught && g.peds[w].health > 0.f && g.peds[w].brain.type != BRAIN_NONE) {
                    setIdle(g, w, 5);   // hands up
                    say(g, wade, "Wade", "[scared]Okay! Okay. I'm done. I just wanted to see my kid.", wTags);
                }
                caughtT = caught || g.peds[w].brain.type == BRAIN_NONE ? caughtT + dt : 0.f;
                if (caughtT > 2.5f) {
                    if (g.peds[w].health > 0.f) setIdle(g, w, 21);   // sits down to wait for the deputies
                    if (g.pinfo.wanted <= 2) {
                        g.pinfo.wanted = 0;
                        g.pinfo.wantedHeat = 0.f;
                    }
                    g.notify("PALMERA SHERIFF", "Deputies are on their way to pick him up. The bounty is yours.");
                    win(g, 2, 1500, "BOUNTY COLLECTED");
                } else if (d > 160.f) {
                    fail("Wade got away.");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        int w = encLive(g, wade), pv = g.playerVehicle();
        switch (stage) {
            case 0:
                if (stageT > 0.8f && pv >= 0 && w >= 0 && !riding(g, wade)) {
                    t.teleport(vec3(P.curb.xy() - P.streetDir * 9.f, P.curb.z), P.curbYaw);
                    t.stopVehicle();
                    if (stageT > 2.5f) seatWithPlayer(g, wade);
                }
                break;
            case 1: stageT += 40.f; break;
            case 2:
                if (stageT > 1.f) {
                    if (want == 2) {
                        Place s = resolvePlace(g, sheriff);
                        t.teleport(s.curb, s.curbYaw);
                    } else {
                        t.teleport(dest.curb, dest.curbYaw);
                    }
                    t.stopVehicle();
                }
                break;
            case 3:
                if (stageT > 1.f && w >= 0 && g.peds[w].brain.type != BRAIN_NONE) {
                    t.exitVehicle();
                    t.teleport(pedPos(g, w) + vec3(2.f, 0.f, 0.f), 0.f);
                    g.knockDown(w, vec3(0.f, 60.f, 20.f), true);
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Crash Landing": a getaway van has piled into the curb, cash bags on the road, two dazed robbers. Hold them until the
// police arrive (a reward), or grab the cash and run (the neighbors call it in).
class EncGetaway : public EncScript {
public:
    EncRef van, rob[2], cruiser, cop[2];
    Place P;
    int bags[3] = {-1, -1, -1};
    dvec3 bagPos[3];
    int bagsTaken = 0;
    bool hostile = false, copsSent = false, copsHere = false, looted = false;
    float copT = 0.f, heat = 0.f;
    const char* rTags = "[accent:newyork:0.6]";

    float leaveRange() const override { return looted ? 1e9f : (engaged ? 500.f : 290.f); }

    bool setup(GameWorld& g) override {
        if (!encStreetSpot(g, seed, test ? 55.f : 120.f, test ? 90.f : 200.f, test, P)) return false;
        float yaw = P.curbYaw + 0.45f * (hashToFloat(seed) > 0.5f ? 1.f : -1.f);
        vec3 vp = curbOffset(g, P, 0.f) + vec3(-P.outward * 0.6f, 0.f);
        van = addVeh(g, pickModel(g, {Vehicles::VC_VAN, Vehicles::VC_SERVICE}, seed), vp, yaw, lin(0.55f, 0.55f, 0.52f));
        if (van.id < 0) return false;
        Vehicle& v = g.vehicles[van.id];
        v.sim.engineHealth = 170.f;
        v.sim.health = 420.f;
        v.sim.damageZones[0] = 0.85f;
        v.sim.damageZones[2] = 0.4f;
        v.sim.engineOn = false;
        v.indicator = 2;
        g.breakVehicleWindows(van.id, vec3(dirFromYaw(yaw), 0.f));
        encPark(g, van.id);
        vec3 back = vp - vec3(dirFromYaw(yaw) * 4.f, 0.f);
        for (int k = 0; k < 3; k++) {
            vec2 off = P.streetDir * (-2.5f - 2.2f * k) + vec2(-P.outward.x, -P.outward.y) * (0.8f * (k - 1));
            vec3 bp = vec3(back.xy() + off, groundAt(g, back.x + off.x, back.y + off.y, back.z + 2.f) + 0.1f);
            Pickup pk;
            pk.used = true;
            pk.type = PICK_MONEY;
            pk.amount = 600 + 150 * k + (int)(hashToFloat(seed >> k) * 200.f);
            pk.pos = dvec3(bp);
            pk.life = -1.f;
            int idx = -1;
            for (int i = 0; i < (int)g.pickups.size() && idx < 0; i++)
                if (!g.pickups[i].used) idx = i;
            if (idx < 0) {
                idx = (int)g.pickups.size();
                g.pickups.push_back(pk);
            } else {
                g.pickups[idx] = pk;
            }
            bags[k] = idx;
            bagPos[k] = pk.pos;
        }
        vec3 r0 = placeOffset(g, P, -3.f, -0.4f), r1 = vp + vec3(P.outward * 2.2f, 0.f) - vec3(P.streetDir * 1.5f, 0.f);
        rob[0] = addPed(g, encGangChar(g, seed), r0, yawTo(r0.xy(), vp.xy()), FAC_CIVILIAN);
        rob[1] = addPed(g, encGangChar(g, seed * 7u + 3u), r1, yawTo(r1.xy(), P.pos.xy()), FAC_CIVILIAN);
        if (rob[0].id < 0 || rob[1].id < 0) return false;
        g.peds[rob[0].id].animIn.stance = 21;   // sitting dazed on the curb
        g.peds[rob[1].id].animIn.stance = 14;
        for (EncRef& r : rob) g.giveWeapon(r.id, WPN_PISTOL, 36);
        spawnFx(FX_SMOKE, dvec3(vp + vec3(dirFromYaw(yaw) * 2.2f, 1.f)), vec3(0.f, 0.f, 1.f), 12, 1.2f);
        anchor = vp;
        blipVeh(van, UI::BLIP_FRIEND, "Crashed van");
        return true;
    }

    bool bagPresent(GameWorld& g, int k) const {
        int i = bags[k];
        if (i < 0 || i >= (int)g.pickups.size()) return false;
        const Pickup& pk = g.pickups[i];
        return pk.used && pk.type == PICK_MONEY && ::length(rel(pk.pos, bagPos[k])) < 0.5f;
    }

    void goHostile(GameWorld& g) {
        if (hostile) return;
        hostile = true;
        engaged = true;
        for (int k = 0; k < 2; k++) {
            int r = encLive(g, rob[k]);
            if (r < 0 || g.peds[r].health <= 0.f) continue;
            g.peds[r].faction = FAC_ENEMY;
            g.peds[r].weapon = WPN_PISTOL;
            setCombat(g, r, g.player, 0.2f);
        }
        say(g, rob[1], "Robber", "[shout]Back off! That money's ours! We bled for it!", rTags);
        blips.clear();
        blipPed(rob[0], UI::BLIP_ENEMY, "Robber");
        blipPed(rob[1], UI::BLIP_ENEMY, "Robber");
        obj(g, "Take down the ~r~robbers~s~ and hold the scene for the police, or grab the cash.");
        score(SC_CHASE, 0.8f, 9);
    }

    void sendCops(GameWorld& g) {
        if (copsSent) return;
        copsSent = true;
        float yaw = 0.f;
        vec3 sp = approachSpot(g, P, 170.f * (hashToFloat(seed >> 5) > 0.5f ? 1.f : -1.f), &yaw);
        cruiser = addVeh(g, pickModel(g, {Vehicles::VC_POLICE}, seed), sp, yaw);
        if (cruiser.id < 0) return;
        g.vehicles[cruiser.id].sirenOn = true;
        for (int k = 0; k < 2; k++) {
            cop[k] = addPed(g, g.randomCivilianChar(seed + (u32)k * 13u, 1), sp, yaw, FAC_POLICE);
            if (cop[k].id >= 0) g.warpPedIntoVehicle(cop[k].id, cruiser.id, k);
        }
        ScriptDriver& d = driveRoad(g, cruiser.id, curbOffset(g, P, -14.f).xy(), 17.f, true, kEncScope);
        d.stopAtEnd = true;
    }

    void update(GameWorld& g, float dt) override {
        int vv = encLiveVeh(g, van);
        if (vv >= 0 && fmodf(age, 0.6f) < dt) spawnFx(FX_SMOKE, dvec3(vehPos(g, vv) + vec3(g.vehicles[vv].sim.forward().xy() * 2.2f, 1.1f)), vec3(0.f, 0.f, 1.f), 3, 1.f);
        int taken = 0;
        for (int k = 0; k < 3; k++) taken += bagPresent(g, k) ? 0 : 1;
        if (taken > bagsTaken) {
            bagsTaken = taken;
            if (!looted) {
                looted = true;
                engaged = true;
                goHostile(g);
                setWanted(g, 2);
                sayMe(g, "[happy:0.4]Finders keepers.");
                blips.clear();
                obj(g, "Grab what you can and get away before the police arrive.");
            }
        }
        vec3 pp = playerPos(g);
        float d = ::length(pp.xy() - anchor.xy());
        if (!hostile && d < 22.f) goHostile(g);
        if (seen && !copsSent && stageT > 0.f) {
            heat += dt;
            if (heat > 35.f) sendCops(g);
        }
        if (looted) {
            // the cash is the player's once they are away with it
            if (d > 170.f || (bagsTaken >= 3 && d > 60.f)) {
                long long got = 0;
                for (int k = 0; k < 3; k++)
                    if (!bagPresent(g, k)) got += bags[k] >= 0 ? g.pickups[bags[k]].amount : 0;
                g.bigMessage("LOOT GRABBED", StrFormat("$%lld", got), 0xff33ccffu);
                setFlag(g, EX_ENC_EARNED, flag(g, EX_ENC_EARNED) + (int)got);
                result = 2;
            }
            return;
        }
        bool robbersDown = encDown(g, rob[0]) && encDown(g, rob[1]);
        if (robbersDown && hostile && !copsSent) {
            sendCops(g);
            obj(g, "Hold the scene until the ~b~police~s~ arrive.");
            blips.clear();
            blipVeh(cruiser, UI::BLIP_POLICE, "Police");
        }
        int cv = encLiveVeh(g, cruiser);
        if (cv >= 0 && !copsHere) {
            ScriptDriver* sd = driverFor(cv);
            if ((sd && sd->done) || ::length(vehPos(g, cv).xy() - anchor.xy()) < 18.f) {
                copsHere = true;
                releaseDriver(g, cv);
                g.vehicles[cv].sirenSilent = true;
                for (int k = 0; k < 2; k++) {
                    int c = encLive(g, cop[k]);
                    if (c < 0) continue;
                    g.removePedFromVehicle(c, true);
                    setGoto(g, c, anchor + vec3(P.streetDir * (k ? 2.f : -2.f), 0.f), 1.6f);
                }
                copT = 0.f;
            }
        }
        if (copsHere) {
            copT += dt;
            if (copT > 4.f) {
                if (robbersDown && hostile) {
                    say(g, cop[0], "Officer", "[calm]We'll take it from here. Nice work, citizen. The bank put up a reward for this crew.", "[accent:south:0.4]");
                    if (g.pinfo.wanted <= 2) {
                        g.pinfo.wanted = 0;
                        g.pinfo.wantedHeat = 0.f;
                    }
                    for (int k = 0; k < 3; k++)
                        if (bagPresent(g, k)) g.pickups[bags[k]].used = false;   // evidence
                    win(g, 1, 750, "CITIZEN'S REWARD");
                } else if (copT > 12.f && !engaged) {
                    result = -2;   // the police have it now
                }
            }
        }
        if (hostile && !robbersDown && d > 260.f) fail("The robbers got away.");
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        if (want == 2) {
            if (stageT > 1.f && bagsTaken < 3) {
                for (int k = 0; k < 3; k++)
                    if (bagPresent(g, k)) {
                        t.teleport(bagPos[k].toVec3(), 0.f);
                        return;
                    }
            }
            if (bagsTaken >= 3 && stageT > 3.f) t.teleportNear(anchor.xy(), 200.f);
            return;
        }
        if (stageT > 0.8f && !hostile) t.teleport(anchor + vec3(P.outward * 1.f, 0.f) + vec3(P.streetDir * 12.f, 0.f), 0.f);
        if (hostile && stageT > 2.f) testKill(g, t, {rob[0], rob[1]});
        int cv = encLiveVeh(g, cruiser);
        if (cv >= 0 && !copsHere && stageT > 5.f) {
            vec3 cp = curbOffset(g, P, -14.f);
            teleportVehicle(g, cv, cp, P.curbYaw);
        }
    }
};

// ==================================================================================================================
// "Red Light Challenge": a local in a tuned car pulls up at the curb and bets $500 on a sprint to a landmark. Honk to
// accept: win the pot or pay up.
int racerChar(GameWorld& g, u32 seed) {
    Anim::CharacterDesc d;
    d.seed = 0x7ACE0u + (seed & 3u);
    d.gender = (seed & 4u) ? Anim::FEMALE : Anim::MALE;
    d.height = d.gender == Anim::FEMALE ? 1.66f : 1.77f;
    d.weight = 0.3f;
    d.muscle = 0.45f;
    d.age = 0.08f;
    d.skinTone = (seed & 8u) ? vec3(0.42f, 0.28f, 0.2f) : vec3(0.72f, 0.54f, 0.42f);
    d.hairStyle = d.gender == Anim::FEMALE ? 9 : 2;
    d.hairColor = vec3(0.04f);
    d.top = 5;
    d.topColor = lin(0.08f, 0.08f, 0.1f);
    d.outer = -1;
    d.bottom = 0;
    d.bottomColor = lin(0.2f, 0.22f, 0.3f);
    d.shoes = 0;
    d.shoeColor = lin(0.95f, 0.95f, 0.95f);
    d.hat = 1;
    return g.namedCharacter(StrFormat("enc_racer_%u", seed & 15u), d);
}

class EncChallenge : public EncScript {
public:
    EncRef racer, rcar;
    Place P;
    RoutePath route;
    std::vector<vec3> cps;
    int nextCp = 0;
    std::string destName;
    bool offered = false, accepted = false, rivalDone = false;
    float behindT = 0.f;
    int count = -1;
    const char* rTags = "[accent:latino:0.35][bright]";

    float leaveRange() const override { return accepted ? 1e9f : 260.f; }

    bool setup(GameWorld& g) override {
        int pv = g.playerVehicle();
        if (pv < 0 || g.isBoat(pv) || g.isAircraft(pv)) return false;
        if (!encStreetSpot(g, seed, test ? 45.f : 90.f, test ? 70.f : 150.f, test, P)) return false;
        struct Dest {
            const Place* p;
            const char* name;
        };
        const Places& L = gPlaces;
        const Dest dests[] = {{&L.beachPier, "the Sol Beach Pier"}, {&L.solarisOne, "Solaris One"}, {&L.stadium, "the stadium"},
                              {&L.midtownPark, "Midtown Park"},     {&L.pulseFm, "Pulse FM"},        {&L.northCity, "North City"},
                              {&L.grove, "the Grove"},              {&L.cafeBeach, "the beach cafes"}, {&L.taxiDepot, "the Sol Cabs depot"},
                              {&L.harlow, "Harlow"},                {&L.lakeTown, "Lake Town"},       {&L.fortCastell, "Fort Castell"}};
        float bestS = 1e9f;
        int best = -1;
        for (int i = 0; i < (int)ARRAY_COUNT(dests); i++) {
            float d = ::length(dests[i].p->curb.xy() - P.curb.xy());
            if (d < 900.f || d > 2200.f) continue;
            float s = fabsf(d - 1400.f) + hashToFloat(seed + (u32)i) * 300.f;
            if (s < bestS) {
                bestS = s;
                best = i;
            }
        }
        if (best < 0) return false;
        destName = dests[best].name;
        buildRoadPath(g, P.curb.xy(), dests[best].p->curb.xy(), route, 1.f);
        float len = route.length();
        if (route.pts.size() < 2 || len < 700.f || len > 3200.f) return false;
        for (float s = 330.f; s < len - 150.f; s += 330.f) cps.push_back(route.at(s));
        cps.push_back(route.pts.back());
        float yaw = P.curbYaw;
        rcar = addVeh(g, pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_MUSCLE, Vehicles::VC_COUPE}, seed), P.curb, yaw,
                      (seed & 1u) ? lin(0.1f, 0.85f, 0.35f) : lin(0.95f, 0.35f, 0.05f));
        racer = addPed(g, racerChar(g, seed), P.curb, yaw, FAC_CIVILIAN);
        if (rcar.id < 0 || racer.id < 0) return false;
        g.warpPedIntoVehicle(racer.id, rcar.id, 0);
        g.vehicles[rcar.id].mods.neon = (seed & 2u) ? vec3(0.2f, 0.6f, 1.f) : vec3(1.f, 0.2f, 0.7f);
        encPark(g, rcar.id);
        anchor = P.curb;
        blipVeh(rcar, UI::BLIP_RACE, "Street racer");
        return true;
    }

    void showCp(GameWorld& g) {
        markers.clear();
        blips.clear();
        if (nextCp < (int)cps.size()) {
            bool last = nextCp + 1 == (int)cps.size();
            marker(cps[nextCp], 5.f, last ? vec3(1.f, 0.3f, 0.3f) : vec3(1.f, 0.85f, 0.2f));
            blipAt(cps[nextCp].xy(), UI::BLIP_OBJECTIVE, last ? "Finish" : "Checkpoint");
            gps(g, cps[nextCp].xy());
        }
        blipVeh(rcar, UI::BLIP_ENEMY, "Rival");
    }

    void update(GameWorld& g, float dt) override {
        int rc = encLiveVeh(g, rcar), pv = g.playerVehicle();
        if (rc < 0 || !encAlive(g, racer)) {
            if (accepted) fail("The race is off.");
            else result = -2;
            return;
        }
        switch (stage) {
            case 0: {
                anchor = vehPos(g, rc);
                float d = ::length(playerPos(g).xy() - anchor.xy());
                if (pv >= 0 && d < 20.f && vehicleSpeed(g, pv) < 6.f && !offered) {
                    offered = true;
                    g.vehicles[rc].hornOn = true;
                    say(g, racer, "Street racer", StrFormat("[happy]Nice ride. Five hundred says I beat you to %s. Honk if you're in.", destName.c_str()), rTags);
                    next();
                }
                break;
            }
            case 1: {   // waiting for the honk
                anchor = vehPos(g, rc);
                g.vehicles[rc].hornOn = false;
                float d = ::length(playerPos(g).xy() - anchor.xy());
                if (pv >= 0 && d < 40.f && g.hudHelpTimer <= 0.f) g.help("Honk (~i:E|LS~) to race for $500.", 1.f);
                if ((pv >= 0 && d < 45.f && g.ctl.horn.pressed) || (test && stageT > 1.f)) {
                    accepted = true;
                    engaged = true;
                    say(g, racer, "Street racer", "[happy]That's what I like to hear. On three.", rTags);
                    count = 3;
                    g.bigMessage("3", "", 0xffffffffu);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_COUNTDOWN, 0.8f);
#endif
                    next();
                } else if (d > 140.f) {
                    say(g, racer, "Street racer", "[scoffs]Figures.", rTags);
                    result = -2;
                }
                break;
            }
            case 2: {   // countdown
                int c = 3 - (int)stageT;
                if (c != count && c >= 0) {
                    count = c;
                    g.bigMessage(c > 0 ? StrFormat("%d", c) : std::string("GO!"), "", c > 0 ? 0xffffffffu : 0xff33ccffu);
#ifdef HAVE_AUDIO
                    Audio::play2D(c > 0 ? Audio::SFX_RACE_COUNTDOWN : Audio::SFX_RACE_GO, 0.8f);
#endif
                }
                if (stageT >= 3.f) {
                    ScriptDriver& d = addDriver(g, rc, route, 32.f, DRV_ROAD, kEncScope);
                    d.racer = true;
                    d.aggressive = true;
                    d.rubberPed = g.player;
                    nextCp = 0;
                    showCp(g);
                    obj(g, StrFormat("Beat the ~r~street racer~s~ to ~y~%s~s~.", destName.c_str()));
                    score(SC_CHASE, 0.9f, 10);
                    next();
                }
                break;
            }
            case 3: {   // the race
                anchor = playerPos(g);
                ScriptDriver* sd = driverFor(rc);
                if (!rivalDone && ((sd && sd->done) || ::length(vehPos(g, rc).xy() - cps.back().xy()) < 12.f)) rivalDone = true;
                if (nextCp < (int)cps.size() && ::length(playerPos(g).xy() - cps[nextCp].xy()) < 16.f) {
                    nextCp++;
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_CHECKPOINT, 0.7f);
#endif
                    if (nextCp >= (int)cps.size()) {
                        if (!rivalDone) {
                            say(g, racer, "Street racer", "[angry]Rematch. Some other night. I'll be ready.", rTags);
                            g.socialReport(UI::TE_RACE_WON, dvec3(playerPos(g)));
                            win(g, 1, 500, "RACE WON");
                        }
                    } else {
                        showCp(g);
                    }
                }
                if (result == 0 && rivalDone) {
                    say(g, racer, "Street racer", "[happy]Pay up, slowpoke. Nice try, though.", rTags);
                    long long pay = Min<long long>(500, g.pinfo.money);
                    win(g, 2, -pay, "RACE LOST");
                }
                float gap = ::length(vehPos(g, rc).xy() - playerPos(g).xy());
                behindT = gap > 450.f ? behindT + dt : 0.f;
                if (result == 0 && (behindT > 8.f || stageT > 240.f)) {
                    say(g, racer, "Street racer", "[happy]Where'd you go? That's five hundred, my friend.", rTags);
                    long long pay = Min<long long>(500, g.pinfo.money);
                    win(g, 2, -pay, "RACE LOST");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        int rc = encLiveVeh(g, rcar);
        switch (stage) {
            case 0:
                if (stageT > 0.8f && rc >= 0) {
                    t.teleport(vehPos(g, rc) - vec3(dirFromYaw(P.curbYaw) * 10.f, 0.f), P.curbYaw);
                    t.stopVehicle();
                }
                break;
            case 3:
                if (want == 1) {
                    // through the checkpoints ahead of the racer (a teleport per checkpoint, stopped on each)
                    if (stageT > 0.5f && nextCp < (int)cps.size() && fmodf(stageT, 0.4f) < dt) {
                        vec3 c = cps[nextCp], from = nextCp > 0 ? cps[nextCp - 1] : playerPos(g);
                        vec2 d = normalize(c.xy() - from.xy() + vec2(1e-3f, 0.f));
                        t.teleport(c - vec3(d * 3.f, 0.f), atan2f(-d.x, d.y));
                        t.stopVehicle();
                    }
                } else if (rc >= 0) {
                    fastForwardDriver(g, rc, 60.f, dt);
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Hot Seat": a carjacker shoves a woman out of her car at the curb and drives off with it. Get it back to her, or
// sell it to Rook's chop shop.
class EncCarjack : public EncScript {
public:
    EncRef owner, jacker, car;
    Place P;
    Place buyer;
    const char* buyerName = "Rook's";
    bool jacked = false;
    float stopT = 0.f, awayT = 0.f;
    const char* oTags = "[accent:caribbean:0.5]";

    float leaveRange() const override { return stage >= 3 ? 1e9f : (engaged ? 650.f : 300.f); }

    bool setup(GameWorld& g) override {
        if (!encStreetSpot(g, seed, test ? 55.f : 100.f, test ? 85.f : 170.f, test, P)) return false;
        buyer = gPlaces.rookShop;
        buyerName = storyDone(g, SF_REPO_MAN) ? "Rook's garage" : "the chop shop in the Flats";
        float yaw = P.curbYaw;
        car = addVeh(g, pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_COMPACT, Vehicles::VC_SUV}, seed), P.curb, yaw,
                     (seed & 1u) ? lin(0.55f, 0.72f, 0.82f) : lin(0.62f, 0.12f, 0.12f));
        if (car.id < 0) return false;
        encPark(g, car.id);
        g.vehicles[car.id].sim.engineOn = false;
        // she stands on the sidewalk by the curb-side door (not out in the traffic lane)
        vec3 ov = vehPos(g, car.id) + vec3(P.outward * 1.6f, 0.f);
        ov.z = groundAt(g, ov.x, ov.y, ov.z + 2.f);
        owner = addPed(g, g.randomCivilianChar(seed | 1u, 0), ov, yaw + kPi * 0.5f, FAC_CIVILIAN);
        vec3 jp = placeOffset(g, P, -16.f * (hashToFloat(seed >> 2) > 0.5f ? 1.f : -1.f), 0.1f);
        jacker = addPed(g, encGangChar(g, seed * 11u), jp, yawTo(jp.xy(), ov.xy()), FAC_CIVILIAN);
        if (owner.id < 0 || jacker.id < 0) return false;
        g.peds[jacker.id].animIn.stance = 10;   // a cigarette, watching
        anchor = ov;
        blipPed(owner, UI::BLIP_FRIEND, "Driver");
        return true;
    }

    void update(GameWorld& g, float dt) override {
        int o = encLive(g, owner), j = encLive(g, jacker), cv = encLiveVeh(g, car);
        if (cv < 0 || !vehicleAlive(g, cv)) {
            if (engaged) fail("The car was destroyed.");
            else result = -2;
            return;
        }
        if (o < 0 || g.peds[o].health <= 0.f) {
            if (engaged && stage >= 3) fail("The owner got hurt.");
            else if (!engaged) result = -2;
        }
        switch (stage) {
            case 0:
                anchor = o >= 0 ? pedPos(g, o) : vehPos(g, cv);
                if ((seen && g.playerAt(anchor.xy(), 58.f)) || (test && stageT > 1.f)) {
                    if (j >= 0) {
                        g.peds[j].animIn.stance = 0;
                        setGoto(g, j, anchor, 5.4f);
                    }
                    next();
                }
                break;
            case 1:
                if (j < 0 || g.peds[j].health <= 0.f) {
                    result = -2;   // somebody stopped him before he got to her
                    break;
                }
                if (encDown(g, jacker)) break;   // knocked over on the way (a bike, a bump): he gets up and carries on
                if (g.peds[j].brain.type != BRAIN_GOTO && fmodf(stageT, 1.f) < dt) setGoto(g, j, anchor, 5.4f);
                if (::length(pedPos(g, j).xy() - anchor.xy()) < 1.8f || stageT > 7.f) {
                    if (o >= 0) g.knockDown(o, vec3(P.outward * 110.f, 25.f), true);
                    g.warpPedIntoVehicle(j, cv, 0);
                    ScriptDriver& d = driveRoad(g, cv, P.curb.xy() + P.streetDir * 850.f, 22.f, true, kEncScope);
                    d.rubberPed = g.player;
                    d.rubberGap = 48.f;
                    say(g, jacker, "Carjacker", "[shout]Thanks for the ride, lady!", "[accent:newyork:0.6]");
                    say(g, owner, "Driver", "[scared]My car! He took my car! My daughter's car seat is in there!", oTags);
                    engaged = true;
                    jacked = true;
                    blips.clear();
                    blipVeh(car, UI::BLIP_ENEMY, "Stolen car");
                    obj(g, "Get the ~r~stolen car~s~ back.");
                    score(SC_CHASE, 0.8f, 11);
                    next();
                }
                break;
            case 2: {   // the chase
                anchor = vehPos(g, cv);
                int d = g.driverOf(cv);
                bool jackerOut = d != j || encDown(g, jacker);
                stopT = vehicleSpeed(g, cv) < 1.f && stageT > 4.f ? stopT + dt : 0.f;
                ScriptDriver* sd = driverFor(cv);
                float dist = ::length(anchor - playerPos(g));
                if (jackerOut || g.vehicles[cv].sim.health < 450.f || stopT > 4.f || (sd && sd->done && dist < 100.f)) {
                    releaseDriver(g, cv);
                    encPark(g, cv);
                    if (j >= 0 && g.peds[j].health > 0.f) {
                        if (g.peds[j].vehicle == cv) g.removePedFromVehicle(j, true);
                        setFlee(g, j, g.player);
                        say(g, jacker, "Carjacker", "[scared]Forget it! It's all yours!", "[accent:newyork:0.6]");
                    }
                    blips.clear();
                    blipPed(owner, UI::BLIP_FRIEND, "Owner");
                    blipAt(buyer.curb.xy(), UI::BLIP_GARAGE, "Chop shop");
                    marker(buyer.curb, 3.f);
                    gps(g, pedPos(g, o).xy());
                    obj(g, StrFormat("Return the car to its ~b~owner~s~, or sell it at ~y~%s~s~.", buyerName));
                    next();
                } else if ((sd && sd->done) || dist > 420.f || stageT > 220.f) {
                    fail("The carjacker got away.");
                }
                break;
            }
            case 3: {   // bring it back, or sell it
                if (g.playerVehicle() != cv) {
                    if (g.hudHelpTimer <= 0.f) g.help("Get in the ~b~car~s~.", 1.5f);
                    awayT = ::length(playerPos(g) - vehPos(g, cv)) > 350.f ? awayT + dt : 0.f;
                    if (awayT > 5.f) fail("You left the car behind.");
                    anchor = vehPos(g, cv);
                    break;
                }
                anchor = playerPos(g);
                if (vehicleDisabled(g, cv)) {
                    fail("The car is wrecked.");
                    break;
                }
                vec3 op = o >= 0 ? pedPos(g, o) : P.pos;
                if (o >= 0 && g.peds[o].health > 0.f && stoppedAt(g, op, 11.f)) {
                    facePed(g, o, playerPos(g));
                    say(g, owner, "Driver", "[happy]Oh thank God. Thank you! You have no idea. Here, it's all I've got on me.", oTags);
                    win(g, 1, 400, "CAR RETURNED");
                } else if (stoppedAt(g, buyer.curb, 10.f)) {
                    float cond = Saturate(g.vehicles[cv].sim.health / 1000.f);
                    long long pay = 500 + (long long)(1000.f * cond);
                    if (storyDone(g, SF_REPO_MAN)) phoneLine(g, CAST_ROOK, "[calm]Nice color. Somebody's going to miss it. Not my problem. I'll wire you the money.");
                    g.removePedFromVehicle(g.player, false);
                    win(g, 2, pay, "CAR SOLD");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        int cv = encLiveVeh(g, car), o = encLive(g, owner);
        switch (stage) {
            case 2: {
                // (toward the chop shop, let him get clear of the owner first: a car stopped by her goes straight back)
                float away = cv >= 0 && o >= 0 ? ::length(vehPos(g, cv).xy() - pedPos(g, o).xy()) : 1e9f;
                if (stageT > 1.5f && (want == 1 || away > 60.f || stageT > 15.f)) {
                    t.teleport(vehPos(g, cv) - vec3(g.vehicles[cv].sim.forward().xy() * 8.f, 0.f), 0.f);
                    testKill(g, t, {jacker});
                }
                break;
            }
            case 3:
                if (stageT > 0.8f && cv >= 0) {
                    if (g.playerVehicle() != cv) t.enter(cv);
                    else if (stageT > 1.5f) {
                        if (want == 2) t.teleport(buyer.curb, buyer.curbYaw);
                        else if (o >= 0) t.teleport(curbOffset(g, P, 0.f), P.curbYaw);
                        t.stopVehicle();
                    }
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Cash in Transit": robbers hit a Sol Secure armored van at the curb. Help the guards (a company reward), or grab the
// cash case in the confusion (three stars).
int guardChar(GameWorld& g, int k) {
    Anim::CharacterDesc d;
    d.seed = 0x5EC0u + (u32)k;
    d.gender = k == 1 ? Anim::FEMALE : Anim::MALE;
    d.height = k == 1 ? 1.7f : 1.84f;
    d.weight = 0.55f;
    d.muscle = 0.6f;
    d.age = 0.35f + 0.15f * k;
    d.skinTone = k == 1 ? vec3(0.4f, 0.27f, 0.19f) : vec3(0.78f, 0.6f, 0.47f);
    d.hairStyle = k == 1 ? 6 : 1;
    d.hairColor = vec3(0.06f);
    d.top = 7;   // uniform shirt, company grey-green
    d.topColor = lin(0.36f, 0.4f, 0.33f);
    d.bottom = 7;
    d.bottomColor = lin(0.12f, 0.13f, 0.12f);
    d.shoes = 2;
    d.shoeColor = vec3(0.02f);
    d.hat = 0;
    d.role = 3;
    return g.namedCharacter(StrFormat("enc_guard_%d", k), d);
}

class EncArmored : public EncScript {
public:
    EncRef van, guard[2], rob[3], rcar;
    Place P;
    int caseIdx = -1;
    vec3 casePos;
    bool fight = false, grabbed = false;
    float allGuardsDownT = 0.f;
    const char* gTags = "[accent:general:1]";

    bool setup(GameWorld& g) override {
        if (!encStreetSpot(g, seed, test ? 55.f : 110.f, test ? 90.f : 170.f, test, P)) return false;
        float yaw = P.curbYaw;
        van = addVeh(g, pickModel(g, {Vehicles::VC_VAN, Vehicles::VC_SERVICE, Vehicles::VC_TRUCK}, seed ^ 5u), P.curb, yaw, lin(0.16f, 0.24f, 0.18f));
        if (van.id < 0) return false;
        g.vehicles[van.id].color1 = lin(0.8f, 0.62f, 0.2f);
        g.vehicles[van.id].indicator = 2;
        encPark(g, van.id);
        vec3 f(dirFromYaw(yaw), 0.f);
        vec3 rear = P.curb - f * 4.2f;
        casePos = vec3(rear.xy(), groundAt(g, rear.x, rear.y, rear.z + 2.f));
        for (int k = 0; k < 2; k++) {
            vec3 gp = rear + vec3(P.outward * (0.6f + 1.2f * k), 0.f) - f * (0.8f * k);
            gp.z = groundAt(g, gp.x, gp.y, gp.z + 2.f);
            guard[k] = addPed(g, guardChar(g, k), gp, yaw + kPi, FAC_SECURITY);
            if (guard[k].id < 0) return false;
            arm(g, guard[k].id, WPN_PISTOL, 6);
            g.peds[guard[k].id].maxHealth = g.peds[guard[k].id].health = 160.f;
            g.peds[guard[k].id].brain.accuracy = 0.3f;
        }
        anchor = rear;
        blipVeh(van, UI::BLIP_FRIEND, "Armored van");
        return true;
    }

    void startFight(GameWorld& g) {
        if (fight) return;
        fight = true;
        engaged = true;
        int rc = encLiveVeh(g, rcar);
        for (int k = 0; k < 3; k++) {
            int r = encLive(g, rob[k]);
            if (r < 0) continue;
            if (g.peds[r].vehicle >= 0) g.removePedFromVehicle(r, true);
            g.peds[r].faction = FAC_ENEMY;
            int target = encLive(g, guard[k & 1]);
            setCombat(g, r, target >= 0 ? target : g.player, 0.24f);
        }
        if (rc >= 0) releaseDriver(g, rc);
        for (int k = 0; k < 2; k++) {
            int gd = encLive(g, guard[k]), r = encLive(g, rob[k]);
            if (gd >= 0 && r >= 0) setCombat(g, gd, r, 0.3f);
        }
        caseIdx = spawnPackage(g, casePos + vec3(0.f, 0.f, 0.25f));
        pickups.push_back(caseIdx);
        say(g, guard[0], "Guard", "[shout]Sol Secure! Shots fired, shots fired! Protect the case!", gTags);
        say(g, rob[0], "Robber", "[shout]Get the case! Go, go, go!", "[accent:latino:0.6]");
        blips.clear();
        for (int k = 0; k < 3; k++) blipPed(rob[k], UI::BLIP_ENEMY, "Robber");
        for (int k = 0; k < 2; k++) blipPed(guard[k], UI::BLIP_FRIEND, "Guard");
        blipAt(casePos.xy(), UI::BLIP_OBJECTIVE, "Cash case");
        obj(g, "Help the ~b~guards~s~ fight off the ~r~robbers~s~, or grab the ~g~cash case~s~ yourself.");
        score(SC_CHASE, 1.f, 12);
        setStage(2);
    }

    void update(GameWorld& g, float dt) override {
        switch (stage) {
            case 0:
                if ((seen && g.playerAt(anchor.xy(), 75.f)) || (test && stageT > 1.f)) {
                    float yaw = 0.f;
                    vec3 sp = approachSpot(g, P, -110.f * P.side, &yaw);
                    rcar = addVeh(g, pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_SUV}, seed * 3u), sp, yaw, lin(0.06f, 0.06f, 0.07f));
                    if (rcar.id < 0) {
                        result = -2;
                        break;
                    }
                    for (int k = 0; k < 3; k++) {
                        rob[k] = addPed(g, encGangChar(g, seed * 17u + (u32)k), sp, yaw, FAC_CIVILIAN);
                        if (rob[k].id < 0) continue;
                        arm(g, rob[k].id, k == 0 ? WPN_SMG : WPN_PISTOL, 5);
                        g.warpPedIntoVehicle(rob[k].id, rcar.id, k);
                    }
                    ScriptDriver& d = driveRoad(g, rcar.id, (P.curb - vec3(dirFromYaw(P.curbYaw) * 11.f, 0.f)).xy(), 16.f, true, kEncScope);
                    d.stopAtEnd = true;
                    next();
                }
                break;
            case 1: {   // the robbers roll up
                int rc = encLiveVeh(g, rcar);
                if (rc < 0) {
                    result = -2;
                    break;
                }
                ScriptDriver* sd = driverFor(rc);
                if ((sd && sd->done) || ::length(vehPos(g, rc).xy() - anchor.xy()) < 15.f || stageT > 16.f) {
                    if (::length(vehPos(g, rc).xy() - anchor.xy()) > 40.f) teleportVehicle(g, rc, P.curb - vec3(dirFromYaw(P.curbYaw) * 12.f, 0.f), P.curbYaw);
                    startFight(g);
                }
                break;
            }
            case 2: {   // the fight
                int alive = 0, guards = 0;
                for (const EncRef& r : rob) alive += encDown(g, r) ? 0 : 1;
                for (const EncRef& gd : guard) guards += encAlive(g, gd) ? 1 : 0;
                // keep every living fighter busy with a living opponent
                for (int k = 0; k < 3; k++) {
                    int r = encLive(g, rob[k]);
                    if (r < 0 || g.peds[r].health <= 0.f || grabbed) continue;
                    int t = g.peds[r].brain.target;
                    if (t < 0 || !g.peds[t].used || g.peds[t].health <= 0.f) {
                        int ng = encLive(g, guard[(k + 1) & 1]);
                        if (ng < 0 || g.peds[ng].health <= 0.f) ng = encLive(g, guard[k & 1]);
                        setCombat(g, r, ng >= 0 && g.peds[ng].health > 0.f ? ng : g.player, 0.24f);
                    }
                }
                for (int k = 0; k < 2; k++) {
                    int gd = encLive(g, guard[k]);
                    if (gd < 0 || g.peds[gd].health <= 0.f || grabbed) continue;
                    int t = g.peds[gd].brain.target;
                    if (t < 0 || !g.peds[t].used || g.peds[t].health <= 0.f || t == g.player) {
                        for (int r = 0; r < 3; r++) {
                            int rr = encLive(g, rob[(r + k) % 3]);
                            if (rr >= 0 && g.peds[rr].health > 0.f) {
                                setCombat(g, gd, rr, 0.3f);
                                break;
                            }
                        }
                    }
                }
                if (caseIdx >= 0 && packageTaken(g, caseIdx) && !grabbed) {
                    grabbed = true;
                    setWanted(g, 3);
                    for (const EncRef& gd : guard) {
                        int id = encLive(g, gd);
                        if (id >= 0 && g.peds[id].health > 0.f) setCombat(g, id, g.player, 0.3f);
                    }
                    say(g, guard[1], "Guard", "[shout]Hey! Drop that case! Drop it!", gTags);
                    win(g, 2, 4000, "CASE GRABBED");
                    break;
                }
                if (alive == 0 && guards > 0) {
                    say(g, guard[0], "Guard", "[calm]That was close. Sol Secure looks after its friends. Here, a company reward. And keep your head down.", gTags);
                    if (g.pinfo.wanted <= 2) {
                        g.pinfo.wanted = 0;
                        g.pinfo.wantedHeat = 0.f;
                    }
                    if (caseIdx >= 0 && !packageTaken(g, caseIdx)) g.pickups[caseIdx].used = false;   // back in the van
                    win(g, 1, 1000, "GUARDS SAVED");
                    break;
                }
                allGuardsDownT = guards == 0 ? allGuardsDownT + dt : 0.f;
                if (allGuardsDownT > 6.f) {
                    if (caseIdx >= 0 && !packageTaken(g, caseIdx)) g.pickups[caseIdx].used = false;
                    for (const EncRef& r : rob) {
                        int id = encLive(g, r);
                        if (id >= 0 && g.peds[id].health > 0.f) setFlee(g, id, g.player);
                    }
                    fail("The robbers made off with the case.");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        if (stage == 0 && stageT > 0.8f) t.teleport(anchor + vec3(P.outward * 1.f, 0.f) + vec3(dirFromYaw(P.curbYaw) * 25.f, 0.f), P.curbYaw + kPi);
        if (stage == 2 && stageT > 1.5f) {
            if (want == 2) {
                if (caseIdx >= 0 && !packageTaken(g, caseIdx)) t.teleport(casePos, 0.f);
            } else {
                testKill(g, t, {rob[0], rob[1], rob[2]});
            }
        }
    }
};

// ==================================================================================================================
// "Adrift": a runaway boat circles off a marina with its throttle jammed; the owner shouts from the dock. Pull
// alongside (a jet ski is tied up at the dock) to jump aboard, then bring it back, or sell it at the Ortega boatyard.
class EncBoat : public EncScript {
public:
    EncRef owner, boat, ski;
    vec3 dock, dockWater, center;
    vec3 yard;
    bool aboard = false;
    const char* oTags = "[accent:south:0.5]";

    float leaveRange() const override { return aboard ? 1e9f : (engaged ? 700.f : 330.f); }

    // a marina or dock near the player: owner's spot on land by the water, the circle's center out on open water
    bool findSpot(GameWorld& g) {
        vec3 pp = playerPos(g);
        std::vector<vec2> cands;
        if (World::gSites)
            for (const World::SiteElem& e : World::gSites->elems) {
                if (e.kind != World::SK_MARINA && e.kind != World::SK_RIVER_MARINA && e.kind != World::SK_DOCK && e.kind != World::SK_BOAT_RAMP &&
                    e.kind != World::SK_FISH_SHACK)
                    continue;
                float d = ::length(e.c - pp.xy());
                if (d < (test ? 20.f : 110.f) || d > 650.f) continue;
                cands.push_back(e.c);
            }
        const Places& L = gPlaces;
        for (vec3 w : {L.keyCoralMarina.pos, L.riverLaunch, L.keySolanoDock, L.sawgrassDock}) {
            float d = ::length(w.xy() - pp.xy());
            if (d > (test ? 20.f : 110.f) && d < 650.f) cands.push_back(w.xy());
        }
        for (int k = 0; k < (int)cands.size(); k++) {
            vec2 c = cands[(k + (int)(seed % 7u)) % cands.size()];
            vec3 w;
            if (!findWater(g, c, 2.4f, w, 170.f)) continue;
            // open water around the circle
            bool open = true;
            for (int i = 0; i < 12 && open; i++) {
                float a = kTwoPi * i / 12.f;
                open = isWaterAt(g, w.xy() + vec2(cosf(a), sinf(a)) * 26.f, 1.6f);
            }
            if (!open) {
                vec3 w2;
                if (!findWater(g, w.xy() + normalize(w.xy() - c + vec2(1e-3f, 0.f)) * 30.f, 2.4f, w2, 60.f)) continue;
                open = true;
                for (int i = 0; i < 12 && open; i++) {
                    float a = kTwoPi * i / 12.f;
                    open = isWaterAt(g, w2.xy() + vec2(cosf(a), sinf(a)) * 26.f, 1.6f);
                }
                if (!open) continue;
                w = w2;
            }
            // the shore: walk from the water back toward the dock until dry ground
            vec2 dir = normalize(c - w.xy() + vec2(1e-3f, 0.f));
            vec2 land = w.xy();
            int steps = 0;
            while (steps++ < 120 && g.map->isWater(land.x, land.y)) land += dir * 1.5f;
            if (steps >= 120) continue;
            vec2 stand = land + dir * 2.f;
            float z = groundAt(g, stand.x, stand.y);
            if (z < -0.5f || z > 6.f) continue;
            if (g.buildings && g.buildings->pointInBuilding(stand, 0.3f)) continue;
            dock = vec3(stand, z);
            vec3 dw;
            if (!findWater(g, land - dir * 7.f, 1.2f, dw, 25.f)) dw = w + vec3((stand - w.xy()) * 0.55f, 0.f);
            dockWater = dw;
            center = w;
            return true;
        }
        return false;
    }

    bool setup(GameWorld& g) override {
        int bm = pickModel(g, {Vehicles::VC_BOAT}, seed);
        if (bm < 0 || !findSpot(g)) return false;
        yard = gPlaces.riverLaunch;
        std::vector<vec3> wps;
        for (int i = 0; i < 16; i++) {
            float a = kTwoPi * i / 16.f;
            wps.push_back(vec3(center.xy() + vec2(cosf(a), sinf(a)) * 22.f, center.z + 0.3f));
        }
        RoutePath path;
        buildWaypointPath(wps, path, 12.f, true, true);
        boat = addVeh(g, bm, wps[0], yawTo(wps[0].xy(), wps[1].xy()), lin(0.95f, 0.95f, 0.97f));
        if (boat.id < 0) return false;
        ScriptDriver& d = addDriver(g, boat.id, path, 6.5f, DRV_KINEMATIC, kEncScope);
        d.loop = true;
        d.kinSpeed = 6.5f;
        owner = addPed(g, g.randomCivilianChar((seed & ~1u) + 2u, 0), dock, yawTo(dock.xy(), center.xy()), FAC_CIVILIAN);
        if (owner.id < 0) return false;
        g.peds[owner.id].voice = Speech::persona("old_man").voice;
        int jm = pickModel(g, {Vehicles::VC_JETSKI, Vehicles::VC_BOAT}, seed);
        if (jm >= 0 && isWaterAt(g, dockWater.xy(), 0.8f)) ski = addVeh(g, jm, dockWater, yawTo(dockWater.xy(), center.xy()));
        anchor = dock;
        blipPed(owner, UI::BLIP_FRIEND, "Boat owner");
        return true;
    }

    void board(GameWorld& g) {
        int b = encLiveVeh(g, boat);
        if (b < 0) return;
        releaseDriver(g, b);
        if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
        g.warpPedIntoVehicle(g.player, b, 0);
        g.vehicles[b].playerUsed = true;
        aboard = true;
        markers.clear();
        blips.clear();
        marker(dockWater, 4.f);
        blipAt(dock.xy(), UI::BLIP_OBJECTIVE, "Dock");
        marker(yard, 5.f, vec3(1.f, 0.6f, 0.2f));
        blipAt(yard.xy(), UI::BLIP_BOAT, "Ortega Boatyard");
        gps(g, dock.xy());
        obj(g, "Bring the boat back to the ~y~dock~s~, or sell it at the ~o~Ortega boatyard~s~.");
        sayMe(g, "[calm]Got her. Throttle's off.");
        setStage(2);
    }

    void update(GameWorld& g, float dt) override {
        (void)dt;
        int o = encLive(g, owner), b = encLiveVeh(g, boat);
        if (b < 0 || !vehicleAlive(g, b)) {
            if (engaged) fail("The boat is lost.");
            else result = -2;
            return;
        }
        switch (stage) {
            case 0:
                anchor = dock;
                if ((seen && g.playerAt(dock.xy(), 130.f)) || (test && stageT > 1.f)) {
                    if (o >= 0) g.peds[o].animIn.stance = 17;
                    say(g, owner, "Boat owner", "[scared]My boat! The throttle jammed and she threw me right off! That's my whole retirement out there!", oTags);
                    if (encLiveVeh(g, ski) >= 0) say(g, owner, "Boat owner", "[shout]Take the jet ski! Pull alongside and jump!", oTags);
                    engaged = true;
                    blips.clear();
                    blipVeh(boat, UI::BLIP_BOAT, "Runaway boat");
                    if (encLiveVeh(g, ski) >= 0) blipVeh(ski, UI::BLIP_VEHICLE, "Jet ski");
                    obj(g, "Get aboard the ~b~runaway boat~s~.");
                    score(SC_CHASE, 0.5f, 13);
                    next();
                }
                break;
            case 1: {   // catching it
                anchor = vehPos(g, b);
                vec3 pp = playerPos(g);
                float d = ::length(pp.xy() - anchor.xy());
                Ped* pl = g.playerPed();
                bool inWater = pl && (pl->state == PS_SWIM || (g.playerVehicle() >= 0 && g.isBoat(g.playerVehicle())));
                if (inWater && d < 5.5f) {
                    board(g);
                    break;
                }
                if (inWater && d < 14.f && g.hudHelpTimer <= 0.f) g.help("Pull alongside to jump aboard.", 1.f);
                break;
            }
            case 2: {   // aboard: the dock or the yard
                anchor = playerPos(g);
                if (g.playerVehicle() != b) {
                    if (g.hudHelpTimer <= 0.f) g.help("Get back on the ~b~boat~s~.", 1.5f);
                    if (::length(playerPos(g) - vehPos(g, b)) > 300.f) fail("You abandoned the boat.");
                    break;
                }
                if (stoppedAt(g, dockWater, 16.f)) {
                    if (o >= 0) {
                        g.peds[o].animIn.stance = 16;   // cheering
                        say(g, owner, "Boat owner", "[happy]You caught her! You beautiful lunatic. Here. Don't argue, just take it.", oTags);
                    }
                    win(g, 1, 500, "BOAT RETURNED");
                } else if (stoppedAt(g, yard, 24.f)) {
                    if (storyDone(g, SF_DRY_DOCK))
                        phoneLine(g, CAST_TOMAS, "[happy:0.4]Nice boat. Whose is it? Actually, don't tell me. I'll have it repainted by Tuesday.");
                    win(g, 2, 1500, "BOAT SOLD");
                }
                break;
            }
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        switch (stage) {
            case 1:
                if (stageT > 1.f) board(g);
                break;
            case 2:
                if (stageT > 1.f) {
                    vec3 to = want == 2 ? yard : dockWater;
                    t.teleport(to, 0.f);
                    t.stopVehicle();
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Flashbulbs": pop star Vivi Lark walks out of a club into a wall of camera flashes and jumps into the player's car.
// Lose the photographers and drop her at her hotel, or stop and let them get the shot (they pay better).
int viviChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0x7171u;
    d.gender = Anim::FEMALE;
    d.height = 1.69f;
    d.weight = 0.26f;
    d.muscle = 0.3f;
    d.age = 0.12f;
    d.skinTone = vec3(0.52f, 0.36f, 0.26f);
    d.ancestry = 1;
    d.hairStyle = 4;
    d.hairColor = lin(0.92f, 0.88f, 0.8f);
    d.top = 15;
    d.topColor = lin(0.85f, 0.7f, 0.25f);
    d.outer = 3;
    d.outerColor = lin(0.05f, 0.05f, 0.06f);
    d.bottom = 4;
    d.bottomColor = lin(0.05f, 0.05f, 0.06f);
    d.shoes = 1;
    d.shoeColor = lin(0.8f, 0.65f, 0.25f);
    d.glasses = 0;
    d.role = 3;
    return g.namedCharacter("enc_vivi", d);
}

class EncPaparazzi : public EncScript {
public:
    EncRef vivi, pap[2], pcar;
    Place P, hotel;
    float flashT = 0.f, lostT = 0.f, shotT = 0.f, repath = 0.f, outT = 0.f;
    bool asked = false;
    const char* vTags = "[accent:caribbean:0.35][husky]";
    const char* pTags = "[accent:newyork:0.7]";

    float leaveRange() const override { return stage >= 1 ? 1e9f : 280.f; }

    bool setup(GameWorld& g) override {
        if (g.playerVehicle() < 0 && !test) return false;
        Place spot;
        if (!encStreetSpot(g, seed, test ? 50.f : 90.f, test ? 80.f : 150.f, test, spot)) return false;
        P = spot;
        hotel = gPlaces.beachCondo;
        if (::length(hotel.curb.xy() - P.curb.xy()) < 500.f) hotel = gPlaces.downtownPenthouse;
        vec3 vp = placeOffset(g, P, 0.f, 1.2f);
        vivi = addPed(g, viviChar(g), vp, yawTo(vp.xy(), P.curb.xy()), FAC_CIVILIAN);
        float yaw = P.curbYaw;
        vec3 cp = curbOffset(g, P, -13.f, &yaw);
        pcar = addVeh(g, pickModel(g, {Vehicles::VC_COMPACT, Vehicles::VC_SEDAN}, seed), cp, yaw, lin(0.3f, 0.32f, 0.34f));
        for (int k = 0; k < 2; k++) {
            vec3 pp = placeOffset(g, P, k ? 2.2f : -2.4f, -0.3f);
            pap[k] = addPed(g, g.randomCivilianChar(seed * 3u + (u32)k * 2u, 0), pp, yawTo(pp.xy(), vp.xy()), FAC_CIVILIAN);
        }
        if (vivi.id < 0 || pcar.id < 0 || pap[0].id < 0 || pap[1].id < 0) return false;
        g.peds[vivi.id].voice.pitch = 205.f;
        encPark(g, pcar.id);
        for (EncRef& p : pap) g.peds[p.id].animIn.stance = 17;
        anchor = vp;
        blipPed(vivi, UI::BLIP_FRIEND, "Vivi Lark");
        return true;
    }

    void flashes(GameWorld& g, float dt) {
        flashT -= dt;
        if (flashT > 0.f) return;
        flashT = 0.35f + hashToFloat(hash32((u32)(age * 100.f) + seed)) * 0.7f;
        int k = hashToFloat(hash32((u32)(age * 31.f))) > 0.5f ? 1 : 0;
        int p = encLive(g, pap[k]);
        if (p < 0 || g.peds[p].health <= 0.f) return;
        vec3 at = g.peds[p].state == PS_INVEHICLE && g.peds[p].vehicle >= 0 ? vehPos(g, g.peds[p].vehicle) + vec3(0.f, 0.f, 1.3f) : g.pedHeadPos(g.peds[p]);
        spawnLight(dvec3(at + vec3(0.f, 0.f, 0.1f)), vec3(1.f, 0.97f, 0.92f) * 900.f, 9.f);
#ifdef HAVE_AUDIO
        Audio::play(Audio::SFX_CAMERA_SHUTTER, at, 0.7f);
#endif
    }

    void update(GameWorld& g, float dt) override {
        int v = encLive(g, vivi), pv = g.playerVehicle(), pc = encLiveVeh(g, pcar);
        if (v < 0 || g.peds[v].health <= 0.f) {
            if (engaged) fail("Vivi got hurt.");
            else result = -2;
            return;
        }
        if (stage <= 2) flashes(g, dt);
        if (stage >= 1 && stage <= 3) {
            if (!riding(g, vivi) && stage >= 2) {
                outT += dt;
                if (outT > 12.f) {
                    fail("Vivi called her own car.");
                    return;
                }
            } else {
                outT = 0.f;
            }
            if (pv >= 0 && g.vehicles[pv].sim.health < 250.f) {
                fail("Vivi is not dying in a car crash for a photo. She's out.");
                return;
            }
        }
        switch (stage) {
            case 0: {
                anchor = pedPos(g, v);
                float d = ::length(playerPos(g).xy() - anchor.xy());
                if (d < 50.f && fmodf(age, 5.f) < dt) say(g, pap[0], "Photographer", "[shout]Vivi! Vivi, over here! Who are you wearing?", pTags);
                if (pv >= 0 && d < 16.f && vehicleSpeed(g, pv) < 3.f && !asked) {
                    asked = true;
                    engaged = true;
                    say(g, vivi, "Vivi Lark", "[scared]You! Drive! Please, just get me away from these vultures!", vTags);
                    setFollow(g, v, g.player);
                    next();
                }
                break;
            }
            case 1:   // she gets in; the photographers run for their car
                anchor = playerPos(g);
                if (riding(g, vivi)) {
                    for (int k = 0; k < 2; k++) {
                        int p = encLive(g, pap[k]);
                        if (p >= 0 && pc >= 0) g.warpPedIntoVehicle(p, pc, k);
                    }
                    if (pc >= 0) {
                        ScriptDriver& d = driveRoad(g, pc, playerPos(g).xy(), 24.f, true, kEncScope);
                        d.stopAtEnd = false;
                    }
                    say(g, pap[1], "Photographer", "[shout]She's getting in a car! Go, go! Follow them!", pTags);
                    blips.clear();
                    blipVeh(pcar, UI::BLIP_ENEMY, "Paparazzi");
                    obj(g, "Lose the ~r~paparazzi~s~.");
                    g.help("Or stop and let them get their shot. They pay well for it.", 6.f);
                    score(SC_CHASE, 0.8f, 14);
                    next();
                }
                break;
            case 2: {   // the chase
                anchor = playerPos(g);
                if (!riding(g, vivi)) break;
                repath -= dt;
                if (pc >= 0 && repath <= 0.f && !vehicleDisabled(g, pc)) {
                    repath = 3.f;
                    ScriptDriver* sd = driverFor(pc);
                    bool stale = !sd || sd->path.pts.empty() || ::length(sd->path.pts.back().xy() - playerPos(g).xy()) > 35.f;
                    if (stale) {
                        ScriptDriver& d = driveRoad(g, pc, playerPos(g).xy(), 25.f, true, kEncScope);
                        d.stopAtEnd = true;
                    }
                }
                float gap = pc >= 0 ? ::length(vehPos(g, pc).xy() - playerPos(g).xy()) : 1e9f;
                bool papsOut = pc < 0 || vehicleDisabled(g, pc) || !encAlive(g, pap[0]);
                lostT = gap > 170.f || papsOut ? lostT + dt : 0.f;
                shotT = pv >= 0 && vehicleSpeed(g, pv) < 1.2f && gap < 20.f && !papsOut ? shotT + dt : 0.f;
                if (shotT > 4.f) {
                    say(g, pap[1], "Photographer", "[happy]The money shot! Pleasure doing business, pal. Here's a little something for the tip.", pTags);
                    g.removePedFromVehicle(v, true);
                    setGoto(g, v, placeOffset(g, resolvePlace(g, playerPos(g).xy()), 12.f, 3.f), 1.6f);
                    say(g, vivi, "Vivi Lark", "[angry]Unbelievable. I hope it was worth it. Don't answer that.", vTags);
                    win(g, 2, 1500, "SOLD THE SHOT");
                } else if (lostT > 6.f) {
                    if (pc >= 0) releaseDriver(g, pc);
                    say(g, vivi, "Vivi Lark", "[happy:0.5]I think we lost them. Take me to my hotel? The lobby is private. They can't follow me in there.", vTags);
                    blips.clear();
                    marker(hotel.curb, 3.f);
                    blipAt(hotel.curb.xy(), UI::BLIP_OBJECTIVE, "Hotel");
                    gps(g, hotel.curb.xy());
                    obj(g, "Drop ~b~Vivi~s~ at her ~y~hotel~s~.");
                    next();
                }
                break;
            }
            case 3:
                anchor = playerPos(g);
                if (riding(g, vivi) && stoppedAt(g, hotel.curb, 12.f)) {
                    g.removePedFromVehicle(v, true);
                    setGoto(g, v, hotel.door, 1.5f);
                    say(g, vivi, "Vivi Lark", "[happy]You're sweet. You're going in my next video. As a mysterious stranger. Ciao, driver.", vTags);
                    win(g, 1, 900, "VIVI DELIVERED");
                }
                break;
        }
    }

    void autotest(GameWorld& g, MissionTest& t, float dt) override {
        (void)dt;
        int v = encLive(g, vivi), pc = encLiveVeh(g, pcar);
        switch (stage) {
            case 0:
                if (stageT > 0.8f && v >= 0) {
                    t.teleport(vec3(P.curb.xy() + P.streetDir * 6.f, P.curb.z), P.curbYaw);
                    t.stopVehicle();
                }
                break;
            case 1:
                if (stageT > 1.5f) seatWithPlayer(g, vivi);
                break;
            case 2:
                if (stageT > 1.f) {
                    if (want == 2) {
                        t.stopVehicle();
                        if (pc >= 0 && ::length(vehPos(g, pc) - playerPos(g)) > 15.f) teleportVehicle(g, pc, playerPos(g) - vec3(dirFromYaw(g.peds[g.player].yaw) * 10.f, 0.f), 0.f);
                    } else if (pc >= 0 && ::length(vehPos(g, pc) - playerPos(g)) < 200.f) {
                        releaseDriver(g, pc);
                        g.vehicles[pc].sim.engineHealth = 0.f;   // their engine gives out
                    }
                }
                break;
            case 3:
                if (stageT > 1.f) {
                    t.teleport(hotel.curb, hotel.curbYaw);
                    t.stopVehicle();
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// The director: picks an encounter that fits the place, the hour and how the player travels, stages it out of view
// ahead, runs it, and keeps them a few minutes apart.
EncScript* encCreate(int kind) {
    switch (kind) {
        case ENC_SNATCH: return new EncSnatch();
        case ENC_STICKUP: return new EncStickup();
        case ENC_BRIDE: return new EncBride();
        case ENC_HITCHHIKER: return new EncHitchhiker();
        case ENC_GETAWAY: return new EncGetaway();
        case ENC_CHALLENGE: return new EncChallenge();
        case ENC_CARJACK: return new EncCarjack();
        case ENC_ARMORED: return new EncArmored();
        case ENC_BOAT: return new EncBoat();
        case ENC_PAPARAZZI: return new EncPaparazzi();
        default: return nullptr;
    }
}

struct EncDirector {
    EncScript* cur = nullptr;
    float timer = 90.f;            // until the next attempt to stage one
    double lastEnd = -1e9;
    int recent[3] = {-1, -1, -1};
    u32 attempts = 1;
    u32 playerUid = 0;
    bool testMode = false;
    int lastKind = -1, lastResult = 0;   // the last encounter's kind and how it ended (tests)
    std::string lastText;
    bool hinted = false;
};
EncDirector gEnc;

const float kEncGap = 210.f;   // seconds between encounters

int encKindById(const std::string& id) {
    for (int k = 0; k < ENC_COUNT; k++)
        if (id == kEncKinds[k].id) return k;
    return -1;
}

void encEnd(GameWorld& g) {
    EncScript* e = gEnc.cur;
    if (!e) return;
    gEnc.lastKind = e->kind;
    gEnc.lastResult = e->result;
    gEnc.lastText = e->endText;
    // entities go back to the city
    for (const EncRef& r : e->vehs) {
        int v = encLiveVeh(g, r);
        if (v < 0) continue;
        if (g.playerVehicle() == v) {
            g.vehicles[v].persistent = false;
            releaseDriver(g, v);
            continue;
        }
        encReleaseVeh(g, v);
    }
    for (const EncRef& r : e->peds) {
        int id = encLive(g, r);
        if (id < 0 || g.peds[id].isPlayer) continue;
        Ped& p = g.peds[id];
        p.persistent = false;
        if (p.health <= 0.f) continue;
        if (p.vehicle >= 0 && p.vehicle == g.playerVehicle()) g.removePedFromVehicle(id, true);
        if (p.faction == FAC_ENEMY) {
            setFlee(g, id, g.player);
        } else if (p.state == PS_ONFOOT && (p.brain.type == BRAIN_NONE || p.brain.type == BRAIN_FOLLOW || p.brain.type == BRAIN_GOTO)) {
            if (p.animIn.stance == 21 || p.animIn.stance == 5) continue;   // sitting / hands up: stays put until recycled
            p.brain.type = BRAIN_WANDER;
            p.brain.edge = -1;
            p.animIn.stance = 0;
        }
    }
    for (int i : e->pickups)
        if (i >= 0 && i < (int)g.pickups.size() && g.pickups[i].used && g.pickups[i].type == PICK_PACKAGE) g.pickups[i].used = false;
    clearDrivers(g, kEncScope);
    e->noGps(g);
    if (!e->objective.empty() && g.hudObjective == e->objective) g.hudObjective.clear();
    if (g.missionTimerHud >= 0.f && !gMissions.active) g.missionTimerHud = -1.f;
#ifdef HAVE_AUDIO
    if (!gMissions.active && e->engaged) Audio::setScore(0, 0.f);
#endif
    delete e;
    gEnc.cur = nullptr;
    gEnc.lastEnd = g.time;
    gEnc.timer = 40.f;
}

// Tallies of a finished encounter and the "every kind" completion
void encFinish(GameWorld& g) {
    EncScript& e = *gEnc.cur;
    if (e.result > 0) {
        int done = flag(g, EX_ENC_DONE) | (1 << e.kind);
        setFlag(g, EX_ENC_COUNT, flag(g, EX_ENC_COUNT) + 1);
        setFlag(g, EX_ENC_DONE, done);
        if (kEncKinds[e.kind].good & (1 << e.result)) setFlag(g, EX_ENC_GOOD, flag(g, EX_ENC_GOOD) + 1);
        if (done == (1 << ENC_COUNT) - 1 && !flag(g, SIDE_ENCOUNTERS_ALL)) {
            setFlag(g, SIDE_ENCOUNTERS_ALL, 1);
            money(g, 5000);
            g.notify("STREET SMART", "Every kind of street encounter seen through. +$5,000");
        }
        LOG("encounter: %s ended with outcome %d after %.0f s", kEncKinds[e.kind].name, e.result, e.age);
    } else if (e.result == -1) {
        if (!e.endText.empty()) g.notify("ENCOUNTER", e.endText);
        LOG("encounter: %s failed after %.0f s (%s)", kEncKinds[e.kind].name, e.age, e.endText.c_str());
    } else {
        LOG("encounter: %s over without the player after %.0f s", kEncKinds[e.kind].name, e.age);
    }
    encEnd(g);
}

// blips (once seen) and markers of the running encounter
void encPresent(GameWorld& g, EncScript& e) {
    drawMarkers(g, e.markers);
    if (!e.seen) return;
    Ped* pl = g.playerPed();
    for (const EncScript::BlipReq& r : e.blips) {
        UI::Blip b;
        b.icon = r.icon;
        b.color = r.color ? r.color : 0xffffffffu;
        b.label = r.label;
        b.flash = !e.engaged;
        if (r.ped.id >= 0) {
            int id = encLive(g, r.ped);
            if (id < 0 || g.peds[id].health <= 0.f) continue;
            b.pos = g.peds[id].pos.toVec3().xy();
            b.heightDiff = pl ? (float)(g.peds[id].pos.z - pl->pos.z) : 0.f;
        } else if (r.veh.id >= 0) {
            int id = encLiveVeh(g, r.veh);
            if (id < 0) continue;
            b.pos = vehPos(g, id).xy();
        } else {
            b.pos = r.pos;
        }
        g.missionBlips.push_back(b);
    }
}

// Is this a good moment to stage something?
bool encCanStart(GameWorld& g) {
    Ped* pl = g.playerPed();
    if (!pl || pl->health <= 0.f || g.pinfo.deathTimer > 0.f || g.pinfo.busted || g.pinfo.wanted > 0) return false;
    if (gMissions.active || g.mInCutscene() || !g.playerControl || openWorldBusy() || gSwitching) return false;
    if (gMissions.retry.pending || gMissions.retry.timer > 0.f || g.phone.open) return false;
    if (World::gInteriors && World::gInteriors->at(pl->pos.toVec3() + vec3(0.f, 0.f, 1.f)) >= 0) return false;
    int pv = g.playerVehicle();
    if (pv >= 0 && (g.isAircraft(pv) || g.vehicles[pv].sim.speed() > 40.f)) return false;
    if (pl->state == PS_SWIM) return false;
    return true;
}

float encWeight(GameWorld& g, int kind) {
    vec3 pp = playerPos(g);
    World::Region reg = g.map->regionAt(pp.x, pp.y);
    float urban = World::regionInfo(reg).urban;
    bool city = urban >= 0.45f, suburb = urban >= 0.18f && urban < 0.45f, rural = urban < 0.18f;
    float tod = g.env ? g.env->timeOfDay : 12.f;
    bool night = tod >= 20.f || tod < 5.f, day = tod >= 8.f && tod < 19.f;
    int pv = g.playerVehicle();
    bool car = pv >= 0 && !g.isBoat(pv) && !g.isAircraft(pv) && !g.isBike(pv);
    bool nightlife = reg == World::REG_BEACH || reg == World::REG_DOWNTOWN || reg == World::REG_CALLE_LUNA || reg == World::REG_FINANCIAL || reg == World::REG_MIDTOWN;
    bool business = reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_MIDTOWN || reg == World::REG_NORTH_CITY || reg == World::REG_CALLE_LUNA;
    switch (kind) {
        case ENC_SNATCH: return city && tod >= 8.f && tod < 22.f ? 1.f : 0.f;
        case ENC_STICKUP: return city ? (night ? 1.2f : 0.6f) : (suburb ? 0.3f : 0.f);
        case ENC_BRIDE: return (city || suburb) && tod >= 9.f && tod < 18.f && car ? 0.9f : 0.f;
        case ENC_HITCHHIKER: return rural && tod >= 7.f && tod < 20.f && car ? 1.5f : 0.f;
        case ENC_GETAWAY: return city || suburb ? 0.8f : 0.15f;
        case ENC_CHALLENGE: return (city || suburb) && (car || (pv >= 0 && g.isBike(pv))) ? (night ? 1.4f : 0.35f) : 0.f;
        case ENC_CARJACK: return city ? 0.9f : (suburb ? 0.3f : 0.f);
        case ENC_ARMORED: return business && day ? 0.8f : 0.f;
        case ENC_BOAT: {
            if (tod < 7.f || tod >= 19.f) return 0.f;
            vec3 w;
            return findWater(g, pp.xy(), 2.f, w, 350.f) ? 1.4f : 0.f;
        }
        case ENC_PAPARAZZI: return nightlife && (tod >= 18.f || tod < 3.f) && car ? 1.f : 0.f;
        default: return 0.f;
    }
}

void encountersUpdate(GameWorld& g, float dt) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    if (pl->uid != gEnc.playerUid) {   // new game, a loaded save or a character switch
        if (gEnc.cur) {
            gEnc.cur->result = -2;
            encEnd(g);
        }
        gEnc.playerUid = pl->uid;
    }
    if (EncScript* e = gEnc.cur) {
        bool abort = gMissions.active != nullptr || pl->health <= 0.f || g.pinfo.deathTimer > 0.f || g.pinfo.busted;
        if (abort) {
            if (e->result == 0) e->result = -2;
            encEnd(g);
            return;
        }
        e->age += dt;
        e->stageT += dt;
        e->update(g, dt);
        if (!e->seen && e->result == 0 && (encSees(g, e->anchor) || e->test)) {
            e->seen = true;
            g.hudBigTime = -1.f;   // a new scene: whatever banner is still up (an earlier result) belongs to something else
            if (!gEnc.hinted && flag(g, EX_ENC_COUNT) == 0 && !e->test) {
                gEnc.hinted = true;
                g.help("Something is happening nearby. Get involved, or look the other way: it's your call.", 6.f);
            }
        }
        if (e->result == 0) {
            float d = ::length(playerPos(g).xy() - e->anchor.xy());
            if (d > e->leaveRange()) {
                e->result = e->engaged ? -1 : -2;
                if (e->engaged && e->endText.empty()) e->endText = "You left them to it.";
            }
            if (e->age > 900.f) e->result = -2;
        }
        if (e->result != 0) {
            encFinish(g);
            return;
        }
        encPresent(g, *e);
        return;
    }
    if (gEnc.testMode) return;
    if (!storyDone(g, SF_LOW_TIDE) || Platform::argValue("missiontest") || Platform::argValue("mission")) return;
    gEnc.timer -= dt;
    if (gEnc.timer > 0.f) return;
    u32 h = hash32(gEnc.attempts++ * 2654435761u + (u32)(g.time * 7.0));
    gEnc.timer = 25.f + hashToFloat(h) * 25.f;
    if (g.time - gEnc.lastEnd < kEncGap || !encCanStart(g)) return;
    if (hashToFloat(h >> 5) > 0.6f) return;
    float w[ENC_COUNT], sum = 0.f;
    int done = flag(g, EX_ENC_DONE);
    for (int k = 0; k < ENC_COUNT; k++) {
        w[k] = encWeight(g, k);
        for (int r : gEnc.recent)
            if (r == k) w[k] = 0.f;
        if ((done >> k) & 1) w[k] *= 0.7f;   // new kinds first
        sum += w[k];
    }
    if (sum <= 0.f) return;
    float r = hashToFloat(h >> 11) * sum;
    int kind = 0;
    for (; kind < ENC_COUNT - 1; kind++) {
        r -= w[kind];
        if (r <= 0.f && w[kind] > 0.f) break;
    }
    if (w[kind] <= 0.f) return;
    EncScript* e = encCreate(kind);
    if (!e) return;
    e->kind = kind;
    e->seed = hash32(h ^ 0xE7C0u) | 2u;
    gEnc.cur = e;
    if (!e->setup(g)) {
        LOG("encounter: no spot for %s here", kEncKinds[kind].name);
        e->result = -2;
        encEnd(g);
        gEnc.lastEnd = -1e9;   // (nothing happened: try again soon)
        gEnc.timer = 20.f;
        return;
    }
    gEnc.recent[2] = gEnc.recent[1];
    gEnc.recent[1] = gEnc.recent[0];
    gEnc.recent[0] = kind;
    vec3 a = e->anchor;
    LOG("encounter: %s staged at %.0f %.0f (%.0f m from the player)", kEncKinds[kind].name, a.x, a.y, ::length(a - playerPos(g)));
}

// ---- --missiontest hooks
// Stage an encounter of `kind` next to the player now (the test placed the player where it fits), steering toward
// outcome `want` in the autotest.
bool encTestStart(GameWorld& g, int kind, int want) {
    gEnc.testMode = true;
    if (gEnc.cur) {
        gEnc.cur->result = -2;
        encEnd(g);
    }
    EncScript* e = encCreate(kind);
    if (!e) return false;
    e->kind = kind;
    e->seed = hash32((u32)kind * 7919u + 17u) | 2u;
    e->test = true;
    e->want = want;
    gEnc.cur = e;
    gEnc.lastResult = 0;
    if (!e->setup(g)) {
        LOG("encounter test: %s found no spot", kEncKinds[kind].name);
        e->result = -2;
        encEnd(g);
        return false;
    }
    LOG("encounter test: %s staged at %.0f %.0f", kEncKinds[kind].name, e->anchor.x, e->anchor.y);
    return true;
}

bool encTestRunning() { return gEnc.cur != nullptr; }
int encTestStage() { return gEnc.cur ? gEnc.cur->stage : -1; }
int encTestResult() { return gEnc.cur ? 0 : gEnc.lastResult; }
const std::string& encTestText() { return gEnc.lastText; }

void encTestStep(GameWorld& g, MissionTest& t) {
    if (gEnc.cur && gEnc.cur->result == 0) gEnc.cur->autotest(g, t, g.dtLast);
}

void encTestAbort(GameWorld& g) {
    if (gEnc.cur) {
        gEnc.cur->result = -2;
        encEnd(g);
    }
}

// Where the test puts the player for an encounter kind: the place, heading, hour, and whether in a car
void encTestSpot(GameWorld& g, int kind, vec3& pos, float& yaw, float& hour, bool& car) {
    const Places& L = gPlaces;
    const Place* p = &L.pulseFm;
    hour = 11.f;
    car = false;
    switch (kind) {
        case ENC_SNATCH: p = &L.midtownPark; break;
        case ENC_STICKUP: {
            hour = 22.f;
            Place st;
            if (placeFromInterior(g, "Bodega La Luna", World::IK_CONVENIENCE, 1, st)) {
                Place q = resolvePlace(g, st.pos.xy() + st.streetDir * 90.f);
                pos = q.pos;
                yaw = q.yaw;
                return;
            }
            p = &L.diner;
            break;
        }
        case ENC_BRIDE: p = &L.northCity; car = true; break;
        case ENC_HITCHHIKER: p = &L.redland; car = true; hour = 10.f; break;
        case ENC_GETAWAY: p = &L.grove; break;
        case ENC_CHALLENGE: p = &L.taxiDepot; car = true; hour = 22.5f; break;
        case ENC_CARJACK: p = &L.pulseFm; break;
        case ENC_ARMORED: p = &L.solarisOne; hour = 13.f; break;
        case ENC_BOAT: p = &L.keyCoralMarina; hour = 10.f; break;
        case ENC_PAPARAZZI: p = &L.cafeBeach; car = true; hour = 22.f; break;
        default: break;
    }
    pos = car ? p->curb : p->pos;
    yaw = car ? p->curbYaw : p->yaw;
}

// Pause menu stats (app.cpp): street encounters, fishing and the airboat tours
std::vector<std::pair<std::string, std::string>> encounterStats(GameWorld& g) {
    std::vector<std::pair<std::string, std::string>> out;
    int done = flag(g, EX_ENC_DONE), kinds = 0;
    for (int k = 0; k < ENC_COUNT; k++) kinds += (done >> k) & 1;
    out.push_back({"Street encounters", StrFormat("%d (%d / %d kinds)", flag(g, EX_ENC_COUNT), kinds, (int)ENC_COUNT)});
    out.push_back({"Good deeds", StrFormat("%d", flag(g, EX_ENC_GOOD))});
    return out;
}

// Every pastime's lines for the pause menu's stats page (app.cpp)
std::vector<std::pair<std::string, std::string>> fishingStats(GameWorld& g);   // fishing.cpp
std::vector<std::pair<std::string, std::string>> tourStats(GameWorld& g);      // airboat_tours.cpp
std::vector<std::pair<std::string, std::string>> activityStats(GameWorld& g) {
    std::vector<std::pair<std::string, std::string>> out = encounterStats(g);
    for (auto& s : fishingStats(g)) out.push_back(s);
    for (auto& s : tourStats(g)) out.push_back(s);
    return out;
}

}  // namespace mu

// For the ambient events (events.cpp, the AI agent's): true while a street encounter is staged within `radius` of p, so
// an ambient event does not pile onto the same corner. (events.cpp is compiled first: declare it there.)
bool encounterBusyNear(vec2 p, float radius) {
    const mu::EncScript* e = mu::gEnc.cur;
    return e && e->result == 0 && length(e->anchor.xy() - p) < radius;
}

}  // namespace Game
