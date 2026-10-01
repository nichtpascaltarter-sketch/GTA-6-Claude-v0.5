// The game world: owns every dynamic entity and runs the simulation step. Implementation is split across
// assets.cpp, peds.cpp, ragdoll.cpp, vehicles.cpp, combat.cpp, camera.cpp, player.cpp and the AI modules.
#pragma once
#include "game.h"
#include "../ui/hud.h"
#include "../audio/speech.h"
#include "ai_game.h"   // AI additions: lane graph, traffic/pedestrian cores and game-side AI state

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
    // cinematic vehicle camera (hold the camera button)
    float camHold = 0.f;
    bool cineActive = false, cineUsed = false;
    dvec3 cinePos;
    // on-foot first person (toggled with the camera button while walking around)
    bool footFirstPerson = false;
    bool fpActive = false;                // this frame renders from the eyes (the toggle can be overridden)
    dvec3 fpEye;                          // smoothed eye position
    // blend from the last scripted (cutscene) shot back to the gameplay camera: set scriptBlend = scriptBlendTotal
    // (seconds) when a cutscene ends instead of cutting
    float scriptBlend = 0.f, scriptBlendTotal = 0.f;
    Render::Camera scriptFrom;
    // player preferences (Settings > Camera)
    float shakeScale = 1.f;               // 0..1 of all camera shake
    bool vehicleAutoCenter = true;        // swing back behind the vehicle after looking around
    bool headBob = true;                  // first-person vertical bob
    bool fpVehicleDefault = false;        // vehicles start in the first-person view
    bool wasInVehicle = false, lookedInVehicle = false;
    float fpEyeZ = 0.f;                   // eye height with the bob filtered out (headBob off)
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
    u32 hintsShown = 0;         // one-time tutorial hints already displayed (bit per hint)
    float focus = 1.f;          // ability meter 0..1
    bool focusActive = false;
    std::vector<u8> collectibleFlags;   // per collectible id
    float maxWanted = 0;
    double playTime = 0;
    // weapon components: bought (owned) and fitted (equipped) WeaponComp bits, tint per weapon (0 stock)
    u8 wpnCompOwned[WPN_COUNT] = {}, wpnCompFitted[WPN_COUNT] = {}, wpnTint[WPN_COUNT] = {}, wpnTintOwned[WPN_COUNT] = {};
    bool flashlightOn = true;
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
    Render::Model* weaponTintModels[WPN_COUNT][kWeaponTints] = {};    // [w][0] == weaponModels[w]
    // magazine-fed guns split for first-person reloads: the gun without its magazine, and the magazine alone
    // carried props (carry.cpp): two looks each; the umbrella's canopy with its shaft drawn apart, a furled one
    Render::Model* carryModels[CARRY_COUNT] = {};
    Render::Model* carryAlt[CARRY_COUNT] = {};
    Render::Model* umbrellaShaft = nullptr;
    Render::Model* umbrellaFurled = nullptr;
    void loadCarryModels();
    // a prop for someone at a place (0 street, 1 airport traveler, 2 shopping, 3 office, 4 beach, 5 angler, 6 birder)
    u8 pickCarry(u32 uid, int context) const;
    void submitCarry(int pedIndex, const Render::DrawItem& body);
    u8 effectiveCarry(const Ped& p) const;   // the prop in hand this frame (rain umbrellas, street defaults, busy hands)
    bool umbrellaWeather() const;
    Render::Model* weaponBodyModels[WPN_COUNT][kWeaponTints] = {};
    Render::Model* weaponMagModels[WPN_COUNT][kWeaponTints] = {};
    Render::Model* weaponCompModels[WPN_COUNT][kWeaponCompCount] = {}; // attachment meshes in the weapon's frame
    Render::Model* pickupModels[6] = {};
    Render::Model* parachuteModel = nullptr;
    Render::Model* phoneModel = nullptr;      // smartphone prop (calls, idle scrolling)

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
    // Screen effects (renderer PostFxControls) driven by gameplay state
    float fxDamage = 0.f, fxFlash = 0.f, fxChroma = 0.f;
    vec3 fxFlashColor = vec3(1.f);
    void updatePostFx(float realDt);
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
    bool reduceFlashing = false;   // accessibility: dimmer muzzle flashes (the renderer dampens lightning / strobes)
    // first person: the gun held in front of the eyes, hands on its grips (fpweapon.cpp)
    struct FpWeapon {
        bool active = false, hideWeapon = false;   // hideWeapon: a scope sight picture covers the view
        float w = 0.f;                             // hold weight (hands IK'd onto the gun, drawn at pos / rot)
        float ads = 0.f, kick = 0.f, sprintW = 0.f, reloadW = 0.f, block = 0.f;
        float scope = 0.f, redDot = 0.f;           // HUD sight pictures (0..1)
        int scopeKind = 0;
        WeaponType weapon = WPN_FISTS;
        vec2 sway = vec2(0.f, 0.f), swayVel = vec2(0.f, 0.f);
        float lastYaw = 0.f, lastPitch = 0.f;
        dvec3 pos;                                 // weapon origin (world) and axes (x right, y barrel, z up)
        mat3 rot;
        bool magInHand = false;                    // reloading: the magazine is out, carried by the support hand
        vec3 magOffset = vec3(0.f);                // ... drawn at the weapon's transform moved by this (world)
        float meleeW = 0.f, meleeBlock = 0.f;      // first-person fists: guard weight, block raise
        float meleeSeen = -1e9f;                   // last time the player was fighting (the guard stays up a while)
    } fpw;
    bool fpWeaponUsable(const Ped& p) const;
    bool fpWeaponMuzzle(dvec3& out) const;
    void updateFirstPersonWeapon(float dt);
    void updateFirstPersonMelee(float dt);   // fpweapon.cpp: the fists' guard and strikes in front of the eyes
    void updateCameraFades(float realDt);   // camera.cpp: peds covering the player / touching the lens dither out
    void resetCameraRig();                  // keeps the Settings > Camera preferences
    bool weaponShowcase = false;   // test: a rack of every gun (stock / all components + tints) at showcasePos
    dvec3 showcasePos;
    void submitWeaponShowcase();
    // autosave (slot 8): after every passed mission and every 10 minutes of calm free roam (off in automated runs)
    bool autosaveEnabled = true, autosaveRequested = false;
    float autosaveTimer = 0.f;
    void updateAutosave(float realDt);
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
    void addWound(int ped, dvec3 worldPos, int bone, float radius);
    // Over into the ragdoll. brace: a push slow enough to see coming (a shove, a tackle, a trip, a kick, a dive): a ped
    // still on its feet braces for the fall first (the arms out, the chin tucked, tipping into the push) and the ragdoll
    // takes it from that pose a moment later (updatePed); without it (a shot, a blast, a car) it goes over at once.
    void knockDown(int ped, vec3 impulse, bool brace = false);
    void knockDownNow(int ped, vec3 impulse);
    void breakVehicleWindows(int vehicle, vec3 dir);
    // Tidegram social feed (social.cpp): UI::TideEvent ev at pos
    void socialReport(int ev, dvec3 pos, const char* subject = nullptr, float magnitude = 0.f);
    void updateSocial(float dt);
    void updatePublicAddress(float dt);   // airport PA announcements (ambient.cpp)
    void socialCrash(int vehicle, float impulse);
    void damageVehicle(int veh, float amount, int attacker, vec3 pointRel, vec3 impulse);
    void explode(dvec3 pos, float radius, float damage, int owner);
    void startFire(dvec3 pos, float radius, float life);
    bool raycast(dvec3 from, vec3 dir, float maxDist, WorldHit& hit, int ignorePed = -1, int ignoreVeh = -1, bool peds = true,
                 bool vehicles = true) const;
    bool lineOfSight(dvec3 a, dvec3 b, int ignorePed, int ignoreVeh) const;
    void fireWeapon(int ped, dvec3 muzzle, vec3 dir);
    void giveWeapon(int ped, WeaponType w, int ammo);
    u8 weaponComps(const Ped& p, WeaponType w) const;   // fitted components (player only; NPCs carry stock guns)
    int clipCapacity(const Ped& p, WeaponType w) const;  // magazine size with an extended mag
    void reportCrime(int type, dvec3 pos, int victim);
    void onPickupCollected(const Pickup& pk);
    void placeWorldPickups();
    void placeStaticBlips();
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
    void updateFocus(float realDt);
    void tutorialHint(int id, const char* text);
    void updateTutorialHints(float dt);
    bool focusActiveApplied = false;
    bool stealthTakedown(Ped& p);
    bool sprintKick(Ped& p);
    // melee combat (melee.cpp)
    bool meleeStart(int pid, bool heavy);                // light/heavy attack (chains combos while a move plays)
    void meleeBlock(int pid, bool on);
    bool meleeDodge(int pid, vec2 worldDir);
    void updateMelee(int pid, float dt);                 // per ped per frame (timers, contact, AI defence)
    int meleeAutoTarget(const Ped& p, float maxDist, float minCos, vec2 fwd = vec2(0.f, 0.f)) const;   // fwd 0 = facing
    void meleeContact(int pid);
    void meleeHit(int attacker, int target, int move);
    bool startTakedown(int attacker, int victim);        // synced rear choke (CLIP_TAKEDOWN_ATTACKER / _VICTIM)
    void updateTakedown(int pid, float dt);
    // mouth animation for a line the ped starts speaking now (exact text + voice passed to Audio::speak*)
    void startLipSync(int pid, const char* spokenText, const Audio::VoiceParams& voice);
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
    void updateCameraRig(float dt);             // camera.cpp: the rig itself (updateCamera adds cutscene blends)
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
    std::string missionTitle() const;
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
    void updateAmbientTraffic(float dt);   // ambient.cpp: boats, helicopters, planes
    void updateDispatch(float dt);
    float trafficWeight(const Vehicles::VehicleModel& m, World::Region reg) const;
    float parkedWeight(const Vehicles::VehicleModel& m) const;
    bool populationOff = false;
    float populationWarmup = 0.f;   // > 0: screen is faded, populate the surroundings instantly (ignore view checks)
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
    UI::PhoneState phone;               // the player's phone (UI::Phone, filled by the app + story layer each frame)
    bool hidePlayerModel = false;       // photo mode "hide player"
    Render::Camera renderCam;           // the camera the last frame was rendered with (HUD projections)
    bool requestSaveMenu = false;       // set when the player steps into a safehouse save marker: the app opens MENU_SAVE
    std::string requestScreenshot;      // test automation: the app saves the next finished frame (after UI) here, clears it
    bool policeSuppressed = false;      // an active mission keeps the police out (crimes are not reported while set)
    bool speakerShot(int speaker, const struct CutsceneShot* prev, float lineTime, struct CutsceneShot& out);   // cutscene framing (missions.cpp)

    // ---- AI additions (lanes.cpp, traffic_core.cpp, pednav.cpp, ai.cpp, traffic.cpp, pedai.cpp, police.cpp,
    //      events.cpp, barks.cpp, population.cpp) ----
    AI::LaneGraph laneGraph;            // lanes, connectors, signal plans, sidewalk graph (built once by initAI)
    AI::TrafficCore traffic;            // traffic driver model (driver slots indexed by vehicle id)
    AI::PedCore pedNav;                 // sidewalk navigation for pedestrians
    AIState ai;                         // per-ped / per-vehicle AI records, witnesses, stimuli, incidents, timings
    void initAI();                      // builds the graphs; called lazily by updateAI if the app did not call it
    PedAI& pedAI(int ped);
    VehAI& vehAI(int veh);
    bool inCameraView(vec3 p, float margin) const;
    void aiSay(int ped, int bark, float chance = 1.f, bool important = false);   // barks.cpp (TTS + close subtitles)
    void aiStimulus(dvec3 pos, int kind, int source, float radius, bool byPlayer);  // scare / alert nearby peds
    bool attachTraffic(int veh, int lane = -1, float u = 0.f, bool cautious = false);  // traffic.cpp
    void aiDriveBoat(int veh, float dt, dvec3 target, float speed);
    void aiFlyHeli(int veh, float dt, dvec3 target, float altitude, float orbitRadius, bool orbit);
    void aiCivilianBrain(int ped, float dt);    // pedai.cpp
    void aiPoliceBrain(int ped, float dt);      // police.cpp (officers on foot)
    void aiPoliceDrive(int veh, float dt);      // police.cpp (pursuit / response driving)
    void updateEvents(float dt);                // events.cpp (ambient random events)
    void aiBuildBodies();                       // perception proxies for this tick
    void aiUpdateThreats(float dt);
    void aiStreetMeets(float dt);               // pedai.cpp: acquaintances running into each other on the sidewalk
    float aiRouteLength(int lane, float u, vec2 goal);   // police.cpp: how far a car on this lane would drive to goal (-1: no route)
    std::string aiDebugText() const;
    std::string aiCensusText(float radius) const;   // who is around the player and what they are doing (tests)
    std::string aiTrafficHealthText() const;         // stuck / blocked / rolled cars, impacts, core counters (soak tests)
    bool aiFenderBender(int carA, int carB);         // events.cpp: two AI cars knocked together -> stop, argue, drive on
    std::string aiEventText(int* stage = nullptr, vec3* pos = nullptr, int* car = nullptr) const;   // events.cpp: the active takeover (tests)
    std::string aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const;              // events.cpp: the active fight (tests)
    std::string aiEventsText(int want, int* stage, vec3* pos) const;   // events.cpp: the active events (tests); stage/pos of the `want` type
    std::string aiK9Text(vec3* dogPos = nullptr) const;   // police.cpp: the K9 unit and the scent trail (tests)
};

extern GameWorld* gGame;
void GameWorld_updateRagdoll(GameWorld& g, Ped& p, float dt);
void freeRagdoll(Ragdoll*& r);
void GameWorld_respawnPlayer(GameWorld& g);

}  // namespace Game
