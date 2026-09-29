// The game world: owns every dynamic entity and runs the simulation step. Implementation is split across
// assets.cpp, peds.cpp, ragdoll.cpp, vehicles.cpp, combat.cpp, camera.cpp, player.cpp and the AI modules.
#pragma once
#include "game.h"
#include "../ui/hud.h"
#include "../audio/speech.h"

namespace Game {

struct DialogueLine;
struct CutsceneShot;

struct WorldHit {
    float t = 1e30f;
    dvec3 pos;
    vec3 normal = vec3(0, 0, 1);
    int ped = -1, vehicle = -1, collider = -1;
    int bone = -1;
    u8 surface = 0;
};

// Visual effects requested by gameplay (mapped to the renderer's particle/decal system in fx.cpp)
enum FxType : u8 {
    FX_SMOKE = 0, FX_DARK_SMOKE, FX_DUST, FX_SPARKS, FX_FIRE, FX_EXPLOSION, FX_BLOOD, FX_WATER_SPLASH, FX_WAKE_SPRAY,
    FX_TIRE_SMOKE, FX_EXHAUST, FX_MUZZLE_FLASH, FX_GLASS, FX_DEBRIS, FX_LEAVES, FX_COUNT
};
enum DecalKind : u8 { DECAL_BULLET_CONCRETE = 0, DECAL_BULLET_METAL, DECAL_BULLET_GLASS, DECAL_BLOOD, DECAL_SCORCH };
void spawnFx(FxType t, dvec3 pos, vec3 dir, int count, float scale, vec3 tint = vec3(1));
void spawnDecal(DecalKind k, dvec3 pos, vec3 normal, float size);
void spawnTracer(dvec3 from, dvec3 to);
void spawnSkid(int& track, dvec3 pos, vec3 normal, float width, float intensity);
void spawnLight(dvec3 pos, vec3 color, float radius);

// Camera controller state
enum CamMode : u8 { CAM_ONFOOT = 0, CAM_AIM, CAM_VEHICLE, CAM_SCRIPTED, CAM_DEATH, CAM_FREE };

struct CameraRig {
    CamMode mode = CAM_ONFOOT;
    float yaw = 0.f, pitch = -0.15f;      // orbit angles (world)
    float dist = 3.6f;
    float curDist = 3.6f;
    vec3 pivotSmooth;
    dvec3 pivotWorld;
    float fov = 60.f * kDegToRad;
    float fovTarget = 60.f * kDegToRad;
    float noInputTime = 0.f;
    int vehicleView = 1;                  // 0 near chase, 1 far chase, 2 hood/first person
    float shake = 0.f;
    float recoil = 0.f;
    float aimBlend = 0.f;
    bool cut = false;
    vec3 velSmooth;
    float lookBehind = 0.f;
    // scripted
    dvec3 scriptPos, scriptTarget;
    float scriptFov = 50.f;
    bool scriptActive = false;
    Render::Camera cam;
};

struct PlayerInfo {
    long long money = 2500;
    int wanted = 0;
    float wantedHeat = 0.f;       // accumulates crimes -> stars
    float wantedCooldown = 0.f;   // time out of sight
    bool policeSeesPlayer = false;
    dvec3 lastSeenPos;
    float lastSeenTime = -100.f;
    float stamina = 1.f;
    float breath = 1.f;
    float deathTimer = 0.f;
    bool busted = false;
    int lastVehicle = -1;
    int radioStation = 0;
    bool walkMode = false;
    bool weaponWheel = false;
    int wheelSel = -1;
    float hitMarker = 0.f;
    bool killMarker = false;
    std::vector<float> damageDirs;
    std::vector<float> damageDirTimes;
    // stats
    double distanceWalked = 0, distanceDriven = 0;
    int kills = 0, copsKilled = 0, vehiclesStolen = 0, headshots = 0, shotsFired = 0, shotsHit = 0;
    int deaths = 0, arrests = 0;
    int collectiblesFound = 0;
    std::vector<u8> collectibleFlags;   // per collectible id
    float maxWanted = 0;
    double playTime = 0;
};

struct GameWorld {
    // External systems
    Render::Renderer* renderer = nullptr;
    Render::Environment* env = nullptr;
    World::WorldMap* map = nullptr;
    World::RoadNetwork* roads = nullptr;
    World::BuildingSet* buildings = nullptr;

    // Assets
    std::vector<CharEntry> chars;
    std::vector<VehicleAsset> vassets;
    std::vector<int> charsByRole[8];
    std::vector<int> charsMaleCivil, charsFemaleCivil;
    int protagonistChar[2] = {-1, -1};
    Render::Model* weaponModels[WPN_COUNT] = {};
    Render::Model* pickupModels[6] = {};
    Render::Model* parachuteModel = nullptr;

    // Entities
    std::vector<Ped> peds;
    std::vector<Vehicle> vehicles;
    std::vector<Pickup> pickups;
    std::vector<Projectile> projectiles;
    std::vector<Fire> fires;
    int player = -1;
    u32 nextUid = 1;
    PlayerInfo pinfo;
    CameraRig rig;
    Controls ctl;
    double time = 0.0;          // simulation time (s)
    float timeScale = 1.f;
    float dtLast = 1.f / 60.f;
    bool paused = false;
    bool playerControl = true;  // false during cutscenes
    bool hudVisible = true;
    float hudRadioTimer = 0.f;
    // Screen fades and cutscene letterbox (drawn by the app over the frame)
    float fadeAlpha = 0.f, fadeTarget = 0.f, fadeSpeed = 2.5f;
    float letterbox = 0.f;
    void fadeOut(float speed = 2.5f) {
        fadeTarget = 1.f;
        fadeSpeed = speed;
    }
    void fadeIn(float speed = 2.5f) {
        fadeTarget = 0.f;
        fadeSpeed = speed;
    }
    bool fadedOut() const { return fadeAlpha >= 0.999f; }
    // Controller rumble (decays each frame; applied by updateRumble)
    float rumbleLow = 0.f, rumbleHigh = 0.f;
    bool vibration = true;
    void rumble(float low, float high) {
        rumbleLow = Max(rumbleLow, low);
        rumbleHigh = Max(rumbleHigh, high);
    }
    void updateRumble(float dt);
    // CPU timings of the last update (ms), shown in the F1 debug overlay
    float profPlayer = 0, profAI = 0, profVehicles = 0, profPeds = 0, profMisc = 0, profMissions = 0, profCamera = 0;
    void sanitizeEntities();
    // HUD messages (filled by gameplay/missions, read by the HUD bridge)
    std::string hudNote, hudNoteTitle, hudHelp, hudObjective;
    float hudNoteTimer = 0.f, hudHelpTimer = 0.f;
    std::string hudBig, hudBigSub;
    u32 hudBigColor = 0xff33ccff;
    float hudBigTime = -1.f;
    std::string subSpeaker, subText;
    u32 subColor = 0xff66ccff;
    float subTimer = 0.f;
    long long moneyShown = 0;
    int shellCount = 0;
    int missionPackagesCollected = 0;
    bool hasWaypoint = false;
    vec2 waypoint;
    std::vector<vec2> gpsRoute, missionRoute;
    float gpsRecalcTimer = 0.f;
    std::vector<UI::Blip> staticBlips, missionBlips;
    float missionTimerHud = -1.f;
    std::string missionCounterLabel;
    int missionCounter = 0, missionCounterMax = 0;
    int gameDay = 1;
    bool missionTargetActive = false;
    vec2 missionTarget;
    bool settingsSubtitles = true, settingsRadar = true, settingsMetric = true;
    std::vector<CrimeEvent> crimes;

    // Setup
    void init(Render::Renderer* r, Render::Environment* e, World::WorldMap* m, World::RoadNetwork* rn, World::BuildingSet* b);
    void buildAssets();         // characters, vehicles, weapons (jobs + GPU upload)
    void shutdown();

    // Entities
    int spawnPed(int charIndex, dvec3 pos, float yaw, Faction f);
    void despawnPed(int id);
    int spawnVehicle(int model, dvec3 pos, float yaw, bool withDriver = false, Faction driverFaction = FAC_CIVILIAN);
    void despawnVehicle(int id, bool includeOccupants = true);
    int randomCivilianChar(u32 seed, int role = 0);
    // Named characters (story cast): builds skeleton + mesh on first use (~10-40 ms), cached by `key`.
    int namedCharacter(const std::string& key, const Anim::CharacterDesc& desc);
    std::unordered_map<std::string, int> namedChars;
    int findVehicleModel(Vehicles::VehicleClass cls, u32 seed);
    void warpPedIntoVehicle(int ped, int veh, int seat);
    void removePedFromVehicle(int ped, bool exitAnim);
    int freeSeat(int veh, bool driver) const;
    int driverOf(int veh) const { return (veh >= 0 && vehicles[veh].used) ? vehicles[veh].seats[0] : -1; }
    bool isAircraft(int veh) const;
    bool isBoat(int veh) const;
    bool isBike(int veh) const;

    // Damage & combat
    void damagePed(int ped, float amount, DamageType type, int attacker, vec3 dir, int bone = -1);
    void killPed(int ped, int attacker, vec3 dir, DamageType type);
    void knockDown(int ped, vec3 impulse);
    void damageVehicle(int veh, float amount, int attacker, vec3 pointRel, vec3 impulse);
    void explode(dvec3 pos, float radius, float damage, int owner);
    void startFire(dvec3 pos, float radius, float life);
    bool raycast(dvec3 from, vec3 dir, float maxDist, WorldHit& hit, int ignorePed = -1, int ignoreVeh = -1, bool peds = true,
                 bool vehicles = true) const;
    bool lineOfSight(dvec3 a, dvec3 b, int ignorePed, int ignoreVeh) const;
    void fireWeapon(int ped, dvec3 muzzle, vec3 dir);
    void giveWeapon(int ped, WeaponType w, int ammo);
    void reportCrime(int type, dvec3 pos, int victim);
    void onPickupCollected(const Pickup& pk);
    void placeWorldPickups();
    void notify(const std::string& title, const std::string& text);
    void help(const std::string& text, float seconds = 6.f);
    void bigMessage(const std::string& text, const std::string& sub, u32 color);
    void subtitle(const std::string& speaker, const std::string& text, float seconds, u32 color = 0xff66ccff);

    // Per-frame
    void update(float dt);
    void updatePeds(float dt);
    void updatePed(int id, float dt);
    void movePed(Ped& p, vec2 desiredVel, float dt, bool jump);
    // Obstacle probe in front of a ped: returns obstacle top height above the feet and its depth along `dir`
    // (0 when nothing within reach). Used for vaulting, climbing and cover.
    bool probeObstacle(const Ped& p, vec3 dir, float reach, float& topZ, float& thickness, vec3& hitPos, vec3& hitNormal) const;
    bool tryTraverse(Ped& p, vec3 dir);   // starts a vault/climb when a suitable obstacle is ahead
    void updateTraverse(Ped& p, float dt);
    void updateParachute(Ped& p, float dt);
    bool stealthTakedown(Ped& p);
    bool sprintKick(Ped& p);
    void animatePed(Ped& p, float dt);
    void updateVehicles(float dt);
    void updateVehicleFx(Vehicle& v, float dt);
    void handleVehicleEvents(int veh);
    struct BrokenProp {
        int collider;
        float time;
    };
    std::vector<BrokenProp> brokenProps;   // knocked-down street furniture (renderer hides these instances)
    void updateProjectiles(float dt);
    void updatePickups(float dt);
    void updateFires(float dt);
    void updatePlayer(float dt);
    void updatePlayerOnFoot(Ped& p, float dt);
    void updatePlayerVehicle(Ped& p, float dt);
    void updateCamera(float dt);
    void submitRender();
    void updateAudioListener(float dt);
    void fillHud(UI::HudState& h, float dt);
    // Road routing (A*): polyline from -> to along the road network (driving respects one-way streets)
    bool computeRoute(vec2 from, vec2 to, bool driving, std::vector<vec2>& out) const;
    void updateGps(float dt);
    // Save / load (savegame.cpp)
    bool saveGame(int slot, const std::string& title);
    bool loadGame(int slot);
    bool readSlotInfo(int slot, UI::SaveSlotInfo& info) const;
    std::string storyTitle = "Prologue";
    std::vector<int> storyFlags;        // mission progress flags (missions module)
    std::vector<int> ownedVehicleModels;   // garage
    float completion() const;
    int protagonistIndex = 0;          // 0 Mari, 1 Dex
    void resetWorldForLoad();
    void spawnPlayer(dvec3 pos, float yaw);
    void newGame();
    // Missions (missions.cpp): scripting helpers used by mission scripts
    bool missionActive() const;
    std::string missionBrief() const;
    std::string storyBriefText;
    void updateMissions(float dt);
    bool missionAvailable(int def) const;
    void startMission(int def);
    void mEnd(bool passed, const std::string& reason);
    int mPed(int charIndex, dvec3 pos, float yaw, Faction f);
    int mVehicle(int model, dvec3 pos, float yaw);
    void mObjective(const std::string& text);
    void mTarget(vec2 p, UI::BlipIcon icon = UI::BLIP_OBJECTIVE);
    void mClearTarget();
    void mBlipPed(int ped, UI::BlipIcon icon = UI::BLIP_ENEMY);
    void mBlipVehicle(int veh, UI::BlipIcon icon = UI::BLIP_VEHICLE);
    void mClearBlips();
    void mMarker(dvec3 pos, float radius, vec3 color = vec3(1.f, 0.85f, 0.2f));
    void mClearMarkers();
    void mSay(const DialogueLine& l);
    void mSay(const std::string& speaker, const std::string& text, int ped = -1, u32 color = 0xff66ccff);
    bool mTalking() const;
    void mCutscene(const std::vector<CutsceneShot>& shots, bool skippable = true);
    bool mInCutscene() const;
    bool playerAt(vec2 p, float r) const;
    bool playerInVehicle(int veh) const;
    void updateAI(float dt);       // population + police + mission AI brains (ai.cpp)
    void updateBrain(int ped, float dt);
    void driveVehicleAI(int veh, float dt);
    bool signalGreen(int node, vec2 approachDir) const;
    int signalLampState(int node, vec2 approachDir) const;   // 0 red, 1 amber, 2 green
    void updatePopulation(float dt);
    void updateDispatch(float dt);
    float trafficWeight(const Vehicles::VehicleModel& m, World::Region reg) const;
    float parkedWeight(const Vehicles::VehicleModel& m) const;
    bool populationOff = false;
    float pedDensityScale = 1.f, trafficDensityScale = 1.f;
    void updateWanted(float dt);   // wanted level (police.cpp)

    // Helpers
    Ped* playerPed() { return player >= 0 && peds[player].used ? &peds[player] : nullptr; }
    int playerVehicle() const { return player >= 0 && peds[player].used ? peds[player].vehicle : -1; }
    vec3 pedHeadPos(const Ped& p) const;   // world (relative to origin as float)
    vec3 pedChestPos(const Ped& p) const;
    void pedsNear(vec2 p, float r, std::vector<int>& out) const;
    void vehiclesNear(vec2 p, float r, std::vector<int>& out) const;
    float groundHeight(float x, float y, float zRef) const;

    // ---- Missions/economy additions (story module: missions.cpp, story*.cpp, activities/shops/economy) ----
    bool requestSaveMenu = false;       // set when the player steps into a safehouse save marker: the app opens MENU_SAVE
    std::string requestScreenshot;      // test automation: the app saves the next finished frame (after UI) here, clears it
    bool policeSuppressed = false;      // an active mission keeps the police out (crimes are not reported while set)
};

extern GameWorld* gGame;
void GameWorld_updateRagdoll(GameWorld& g, Ped& p, float dt);
void freeRagdoll(Ragdoll*& r);
void GameWorld_respawnPlayer(GameWorld& g);

}  // namespace Game
