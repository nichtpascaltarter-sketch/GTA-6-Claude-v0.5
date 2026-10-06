// Story framework shared by the three acts: story/side/extended flag indices, the StoryMission base class with the
// common stage building blocks (go-to targets, vehicle objectives, enemy groups, ally checks, checkpoints) and the
// few extra story locations that are not on the road network (Port Isle yard, Key Coral villa, swamp hammock).
#include "missions.h"

namespace Game {
namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Flags. Story missions set storyFlags[storyIndex].
enum StoryFlag : int {
    SF_LOW_TIDE = 0, SF_REPO_MAN, SF_DRY_DOCK, SF_PRESSURE, SF_COLLATERAL, SF_PINK_SLIPS, SF_LAST_CALL,
    SF_DEAD_AIR, SF_VELVET_ROPE, SF_BAGMAN, SF_SAWGRASS_RUN, SF_RIPTIDE, SF_HEAVY_LIFT, SF_FIREWORKS,
    SF_SECOND_CHANCE, SF_PAPER_TRAIL, SF_BLUEPRINTS, SF_DRESS_REHEARSAL, SF_SOLARIS_ONE, SF_OVERSEAS, SF_SIGNAL,
    SF_STORY_COUNT
};

// Side-activity completion flags (count toward 100%).
enum SideFlag : int {
    SIDE_TAXI = kSideBase, SIDE_COURIER, SIDE_VIGILANTE, SIDE_PARAMEDIC,
    SIDE_RACE_CALLE, SIDE_RACE_BEACH, SIDE_RACE_HIGHWAY, SIDE_RACE_GROVE, SIDE_RACE_KEYS,
    SIDE_BOAT_BAY, SIDE_BOAT_RIVER, SIDE_FLIGHT_1, SIDE_FLIGHT_2, SIDE_FLIGHT_3, SIDE_RANGE,
    SIDE_STUNTS_ALL, SIDE_JAMMERS_ALL, SIDE_BUSINESSES_ALL, SIDE_SAFEHOUSES_ALL, SIDE_OUTFITS,
    SIDE_TAXI_ALL, SIDE_VIGILANTE_ALL, SIDE_PARAMEDIC_ALL, SIDE_COURIER_ALL,
    SIDE_COUNT
};

// Extended integer state (economy, collectibles, switching, records).
enum ExtFlag : int {
    EX_VERSION = kExtBase,          // 128
    EX_INTRO_DONE = 129,
    EX_BUSINESS = 130,              // 130..139: 1 when owned
    EX_BUSINESS_DAY = 140,          // 140..149: last game day paid
    EX_SAFEHOUSE = 150,             // 150..159: 1 when owned
    EX_OUTFIT_MARI = 160, EX_OUTFIT_DEX = 161, EX_OUTFITS_MARI = 162, EX_OUTFITS_DEX = 163,
    EX_OTHER_VALID = 164, EX_OTHER_X, EX_OTHER_Y, EX_OTHER_Z, EX_OTHER_YAW, EX_OTHER_HEALTH, EX_OTHER_ARMOR, EX_OTHER_WEAPONS,
    EX_OTHER_WEAPON,                // 164..172 (the protagonist not being played)
    EX_OTHER_AMMO = 176,            // 176..187
    EX_JAMMERS = 190,               // bitmask of destroyed signal jammers (30)
    EX_JAMMER_COUNT = 191,
    EX_STUNTS = 192,                // bitmask of unique stunt jumps completed
    EX_STUNT_COUNT = 193,
    EX_INSANE_BEST = 194,           // best freestyle stunt score
    EX_RACE_BEST = 200,             // 200..215: best times (centiseconds) per race
    EX_TAXI_FARES = 220, EX_TAXI_BEST, EX_COURIER_LEVEL, EX_VIGILANTE_LEVEL, EX_PARAMEDIC_LEVEL, EX_RANGE_BEST,
    EX_TAXI_EARNED = 226,
    EX_ENDING = 230,                // 1 broadcast, 2 leverage
    EX_HEIST_APPROACH = 231,        // 1 quiet, 2 loud
    EX_HEIST_TAKE = 232,            // thousands of dollars secured
    EX_PHONE_STEP = 240,            // story phone calls delivered (bitmask per story mission)
    EX_PHONE_STEP2 = 241,
    EX_INCOME_TOTAL = 244,          // business income received (hundreds of dollars)
    EX_LAST_PAYDAY = 245,
    EX_SWITCH_TIP = 246,
    EX_VEHICLE_PAINT = 260,         // 260..299: paint (packed RGB565) of owned vehicles by garage slot
    EX_VEHICLE_MODS = 384,          // 384..503: Tide Customs parts of owned vehicles, 3 ints per garage slot (see shops.cpp)
};

const int kStoryMissionCount = SF_STORY_COUNT;

bool storyDone(GameWorld& g, int sf) { return flag(g, sf) != 0; }

// Places that are not on the road network (raw positions snapped to the ground)
vec3 rawSpot(GameWorld& g, vec2 p) { return vec3(p, groundAt(g, p.x, p.y)); }

// ------------------------------------------------------------------------------------------------------------------
// Mission items (cash bags, ledgers, crates): pickups flagged as mission items, removed when the mission ends.
int spawnPackage(GameWorld& g, vec3 pos) {
    Pickup pk;
    pk.used = true;
    pk.type = PICK_PACKAGE;
    pk.mission = true;
    pk.amount = 1;
    pk.pos = dvec3(pos);
    for (int i = 0; i < (int)g.pickups.size(); i++)
        if (!g.pickups[i].used) {
            g.pickups[i] = pk;
            return i;
        }
    g.pickups.push_back(pk);
    return (int)g.pickups.size() - 1;
}

bool packageTaken(GameWorld& g, int idx) { return idx < 0 || idx >= (int)g.pickups.size() || !g.pickups[idx].used || !g.pickups[idx].mission; }

// Collect a mission item by proximity (works from boats and cars too).
bool grabNear(GameWorld& g, int idx, float radius) {
    if (packageTaken(g, idx)) return true;
    vec3 d = playerPos(g) - g.pickups[idx].pos.toVec3();
    if (length(vec2(d.x, d.y)) < radius && fabsf(d.z) < 4.f) {
        g.pickups[idx].used = false;
        g.missionPackagesCollected++;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PICKUP_COLLECTIBLE, 0.9f);
#endif
        return true;
    }
    return false;
}

void clearMissionPickups(GameWorld& g) {
    for (auto& pk : g.pickups)
        if (pk.used && pk.mission) pk.used = false;
}

// Hold the interact button (F / Y) near a spot for `need` seconds (cracking safes, planting bugs).
struct HoldAction {
    float t = 0.f;
    bool update(GameWorld& g, vec3 pos, float radius, float need, const char* label, float dt) {
        Ped* pl = g.playerPed();
        bool inRange = pl && pl->state == PS_ONFOOT && length(pl->pos.toVec3().xy() - pos.xy()) < radius && fabsf(pl->pos.toVec3().z - pos.z) < 3.f;
        if (!inRange) {
            t = Max(0.f, t - dt * 2.f);
            return false;
        }
        if (g.ctl.enter.down || gMissions.test.active) t += dt;
        else t = Max(0.f, t - dt);
        int pct = (int)(Saturate(t / need) * 100.f);
        std::string bar;
        for (int i = 0; i < 10; i++) bar += i < pct / 10 ? "|" : ".";
        g.help(StrFormat("Hold ~i:F|Y~ to %s  ~g~%s~s~ %d%%", label, bar.c_str(), pct), 0.2f);
        return t >= need;
    }
};

// Fireworks burst in the sky (particles, colored light, bang).
void fireworksBurst(GameWorld& g, vec3 center, u32 seed) {
    float a = hashToFloat(seed) * kTwoPi;
    vec3 p = center + vec3(cosf(a) * 40.f * hashToFloat(seed * 3u), sinf(a) * 40.f * hashToFloat(seed * 5u), 60.f + 40.f * hashToFloat(seed * 7u));
    const vec3 cols[5] = {vec3(1.f, 0.3f, 0.6f), vec3(0.3f, 0.8f, 1.f), vec3(1.f, 0.85f, 0.3f), vec3(0.5f, 1.f, 0.4f), vec3(1.f, 0.4f, 0.2f)};
    vec3 c = cols[seed % 5];
    spawnFx(FX_SPARKS, dvec3(p), vec3(0, 0, 1.f), 40, 3.f, c);
    spawnFx(FX_SPARKS, dvec3(p), vec3(0, 0, -1.f), 30, 2.5f, c);
    spawnLight(dvec3(p), c * 40000.f, 120.f);
#ifdef HAVE_AUDIO
    Audio::play(Audio::SFX_EXPLOSION_SMALL, p, 0.35f, 1.4f);
#endif
}

// Boats: spawned on the water surface.
int spawnBoat(GameWorld& g, vec3 waterPos, float yaw, vec3 color = vec3(-1.f), u32 seed = 0) {
    int model = pickModel(g, {Vehicles::VC_BOAT, Vehicles::VC_JETSKI, Vehicles::VC_AIRBOAT}, seed);
    if (model < 0) return -1;
    int v = g.mVehicle(model, dvec3(waterPos + vec3(0, 0, 0.3f)), yaw);
    if (v >= 0 && color.x >= 0.f) g.vehicles[v].color0 = color;
    return v;
}

int spawnAirboat(GameWorld& g, vec3 waterPos, float yaw, vec3 color = vec3(-1.f), u32 seed = 0) {
    int model = pickModel(g, {Vehicles::VC_AIRBOAT, Vehicles::VC_BOAT, Vehicles::VC_JETSKI}, seed);
    if (model < 0) return -1;
    int v = g.mVehicle(model, dvec3(waterPos + vec3(0, 0, 0.3f)), yaw);
    if (v >= 0 && color.x >= 0.f) g.vehicles[v].color0 = color;
    return v;
}

// Water route: a loop of water points around a center (for boat chases in the swamp / bay).
std::vector<vec3> waterLoop(GameWorld& g, vec3 center, float radius, int n, float minDepth, float phase = 0.f) {
    std::vector<vec3> pts;
    for (int i = 0; i < n; i++) {
        float a = phase + kTwoPi * i / n;
        vec2 p = center.xy() + vec2(cosf(a), sinf(a)) * radius * (0.8f + 0.4f * hashToFloat(hash32((u32)i * 77u + 3u)));
        vec3 w;
        if (findWater(g, p, minDepth, w, radius * 0.6f)) pts.push_back(w);
    }
    return pts;
}

// Helicopters / planes: spawned at a ground position.
int spawnAircraft(GameWorld& g, bool heli, vec3 pos, float yaw, vec3 color = vec3(-1.f)) {
    int model = heli ? pickModel(g, {Vehicles::VC_HELI}, 0) : pickModel(g, {Vehicles::VC_PLANE}, 0);
    if (model < 0) return -1;
    int v = g.mVehicle(model, dvec3(pos + vec3(0, 0, 0.5f)), yaw);
    if (v >= 0 && color.x >= 0.f) g.vehicles[v].color0 = color;
    return v;
}

// Wanted level set by the story (police show up and chase).
void setWanted(GameWorld& g, int stars) {
    const float thr[6] = {0.f, 0.3f, 2.0f, 5.0f, 9.0f, 15.0f};
    stars = Clamp(stars, 0, 5);
    g.pinfo.wanted = stars;
    g.pinfo.wantedHeat = thr[stars] + 0.1f;
    g.pinfo.wantedCooldown = 0.f;
    Ped* pl = g.playerPed();
    if (pl) g.pinfo.lastSeenPos = pl->pos;
    g.pinfo.lastSeenTime = (float)g.time;
    gMissions.suppressPolice = false;
#ifdef HAVE_AUDIO
    if (stars > 0) Audio::play2D(Audio::SFX_WANTED_UP, 0.7f);
#endif
}

// ------------------------------------------------------------------------------------------------------------------
// Base class for story missions: common state and stage helpers.
class StoryMission : public Mission {
public:
    std::vector<int> enemies;       // hostiles for kill objectives
    std::vector<int> enemyCars;
    int buddy = -1;                 // main ally riding along / fighting
    const char* buddyName = "";
    vec3 goal;                      // current go-to target
    float goalR = 4.f;
    bool goalVehicleStop = false;
    float timer = 0.f;
    int counter = 0;
    int playerCar = -1;             // vehicle given to the player for the mission
    int rideCar = -1;               // the protagonist's own car parked near the start (blipped until the player drives)
    TailState tail;

    // Missions that open with a drive park the protagonist's own car at the curb nearby: Mari's red coupe, Dex's pickup.
    void provideRide(GameWorld& g, const Place& spot, float along) {
        if (g.playerVehicle() >= 0) return;
        bool isDex = g.protagonistIndex == 1;
        int model = isDex ? pickModel(g, {Vehicles::VC_PICKUP, Vehicles::VC_SUV}, 2) : pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SEDAN}, 3);
        float yaw = spot.curbYaw;
        vec3 p = curbOffset(g, spot, along, &yaw);
        rideCar = spawnCar(g, model, p, yaw, isDex ? lin(0.18f, 0.2f, 0.22f) : lin(0.55f, 0.06f, 0.2f));
        if (rideCar < 0) return;
        g.mBlipVehicle(rideCar, UI::BLIP_GARAGE);
        if (playerCar < 0) playerCar = rideCar;
    }

    void preUpdate(GameWorld& g, float dt) override {
        (void)dt;
        // the ride's blip goes once the player drives anything
        if (rideCar >= 0 && g.playerVehicle() >= 0) {
            unblipVehicle(rideCar);
            rideCar = -1;
        }
    }

    // checkpoint reached (retry restarts from here)
    void cp(GameWorld& g, int n) {
        checkpoint = n;
        gMissions.checkpoint = n;
        (void)g;
    }

    MissionStatus fail(const char* why) {
        failReason = why;
        return MS_FAILED;
    }

    // go-to objective with blip, GPS and marker
    void goTo(GameWorld& g, vec3 p, float radius, const std::string& objective, bool vehicleStop = false, bool marker = true,
              vec3 color = vec3(1.f, 0.85f, 0.2f)) {
        goal = p;
        goalR = radius;
        goalVehicleStop = vehicleStop;
        g.mClearMarkers();
        g.mTarget(p.xy());
        if (marker) g.mMarker(dvec3(p), Min(radius, vehicleStop ? 3.5f : 2.2f), color);
        if (!objective.empty()) g.mObjective(objective);
    }

    // true once the player reached the current goal (in a vehicle: slowed down when vehicleStop)
    bool arrived(GameWorld& g) {
        int pv = g.playerVehicle();
        float r = goalR + (pv >= 0 ? 3.f : 0.f);
        if (!g.playerAt(goal.xy(), r)) return false;
        if (fabsf(playerPos(g).z - goal.z) > 8.f) return false;
        if (goalVehicleStop && pv >= 0 && g.vehicles[pv].sim.speed() > 3.f) {
            if (g.hudHelpTimer <= 0.f) g.help("Slow down to stop here.", 1.2f);
            return false;
        }
        return true;
    }

    void clearGoal(GameWorld& g) {
        g.mClearMarkers();
        g.mClearTarget();
    }

    bool allyDown(GameWorld& g, int p, const char* who) {
        if (p < 0) return false;
        if (!g.peds[p].used || g.peds[p].health <= 0.f) {
            failReason = std::string(who) + " died.";
            return true;
        }
        return false;
    }

    bool vehicleLost(GameWorld& g, int v, const char* what) {
        if (v < 0) return false;
        if (!vehicleAlive(g, v)) {
            failReason = std::string("The ") + what + " was destroyed.";
            return true;
        }
        return false;
    }

    // The player walked away from a vehicle the mission needs.
    bool abandoned(GameWorld& g, int v, float dist, const char* what) {
        if (v < 0 || g.playerVehicle() == v) return false;
        if (::length(playerPos(g) - vehPos(g, v)) > dist) {
            failReason = std::string("You abandoned the ") + what + ".";
            return true;
        }
        return false;
    }

    int aliveEnemies(GameWorld& g) { return countAlive(g, enemies); }

    void blipEnemies(GameWorld& g) {
        for (int e : enemies)
            if (pedAlive(g, e)) g.mBlipPed(e, UI::BLIP_ENEMY);
    }

    // Enemy gunman on foot (Cuervo / guard / cop look) attacking `target`.
    int gunman(GameWorld& g, int cast, vec3 pos, float yaw, WeaponType w, float accuracy, int target = -2) {
        int p = spawnCast(g, cast, pos, yaw, FAC_ENEMY);
        if (p < 0) return -1;
        arm(g, p, w);
        g.peds[p].brain.accuracy = accuracy;
        g.peds[p].brain.aggression = 0.6f;
        setCombat(g, p, target == -2 ? g.player : target, accuracy);
        enemies.push_back(p);
        return p;
    }

    // Enemy car rolling in with armed occupants who get out and fight near `stopAt`.
    int attackCar(GameWorld& g, int model, vec3 from, float yaw, int n, WeaponType w, float accuracy, u32 seed) {
        std::vector<int> crew;
        int thugs[4] = {CAST_THUG_A, CAST_THUG_B, CAST_THUG_C, CAST_THUG_D};
        int v = spawnCrewCar(g, model, from, yaw, castChar(g, thugs[seed % 4]), n - 1, castChar(g, thugs[(seed + 1) % 4]), FAC_ENEMY, w, accuracy, &crew);
        if (v < 0) return -1;
        for (int p : crew) {
            g.peds[p].voice = castVoice(CAST_THUG_A + (int)(seed % 4));
            enemies.push_back(p);
        }
        g.vehicles[v].color0 = seed & 1 ? lin(0.05f, 0.05f, 0.06f) : lin(0.45f, 0.04f, 0.05f);
        enemyCars.push_back(v);
        return v;
    }

    // Occupants of an enemy car pile out and attack once it stopped near the player (or the goal).
    void dismountNear(GameWorld& g, int v, vec3 where, float radius, int target = -2) {
        if (v < 0 || !g.vehicles[v].used) return;
        Vehicle& car = g.vehicles[v];
        bool close = ::length(car.sim.body.pos.toVec3() - where) < radius;
        bool stopped = car.sim.speed() < 2.5f;
        bool dead = !vehicleAlive(g, v);
        if (!(close && stopped) && !dead) return;
        for (int s = 0; s < 8; s++) {
            int p = car.seats[s];
            if (p < 0 || !pedAlive(g, p) || g.peds[p].isPlayer) continue;
            g.removePedFromVehicle(p, true);
            setCombat(g, p, target == -2 ? g.player : target, g.peds[p].brain.accuracy);
        }
        releaseDriver(g, v);
    }

    // Buddy support: follow/fight each frame.
    void updateBuddy(GameWorld& g, float engage = 35.f) {
        if (buddy >= 0) buddyUpdate(g, buddy, &enemies, engage);
    }

    // Spawn the other protagonist as a buddy (uses their current outfit).
    int spawnPartner(GameWorld& g, int who, vec3 pos, float yaw, WeaponType w = WPN_PISTOL) {
        int ci = g.protagonistChar[who];
        if (ci < 0) return -1;
        int p = g.mPed(ci, dvec3(pos), yaw, FAC_FRIEND);
        if (p < 0) return -1;
        g.peds[p].voice = protagonistVoice(who);
        g.peds[p].maxHealth = g.peds[p].health = 450.f;
        g.peds[p].brain.accuracy = 0.55f;
        arm(g, p, w, 20);
        return p;
    }

    // Other protagonist (0/1) relative to the player
    int other(GameWorld& g) const { return g.protagonistIndex == 0 ? 1 : 0; }

    // Autotest helper used by most missions: approach the current goal and wait.
    void testGoal(GameWorld& g, MissionTest& t, float dt, float speed = 45.f) {
        (void)g;
        if (t.stageTime < 0.8f) return;
        if (g.playerVehicle() >= 0) t.driveToward(goal.xy(), speed, dt);
        else if (t.stageTime > 1.2f) t.teleportNear(goal.xy(), 1.f);
    }
};

}  // namespace mu
}  // namespace Game
