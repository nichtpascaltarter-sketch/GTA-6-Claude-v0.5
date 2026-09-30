// Gameplay core: entities (peds, vehicles, pickups, projectiles), the game world that owns and updates them, and the
// interfaces used by the player controller, AI, missions and UI.
#pragma once
#include "../world/worldmap.h"
#include "../world/roads.h"
#include "../world/buildings.h"
#include "../render/renderer.h"
#include "../sim/physics.h"
#include "../sim/waves.h"
#include "../sim/vehicle_models.h"
#include "../sim/vehicle_sim.h"
#include "../anim/character.h"
#include "../audio/audio.h"
#include "../audio/speech_ext.h"
#include "input.h"

namespace Game {

// ------------------------------------------------------------------------------------------------------------------
enum Faction : u8 {
    FAC_CIVILIAN = 0, FAC_PLAYER, FAC_POLICE, FAC_GANG_CUERVOS, FAC_GANG_SAINTS, FAC_MEDIC, FAC_SECURITY,
    FAC_ENEMY,    // mission hostiles
    FAC_FRIEND,   // mission allies / buddies
    FAC_COUNT
};

enum WeaponType : u8 {
    WPN_FISTS = 0, WPN_KNIFE, WPN_BAT, WPN_PISTOL, WPN_REVOLVER, WPN_SMG, WPN_RIFLE, WPN_SHOTGUN, WPN_SNIPER, WPN_RPG,
    WPN_GRENADE, WPN_MOLOTOV, WPN_COUNT
};

struct WeaponInfo {
    const char* name;
    int hudIcon;          // UI::HudState::weaponIcon
    int animKind;         // Anim::AnimInput::weaponKind (0 none, 1 pistol, 2 rifle, 3 melee, 4 thrown)
    int slot;             // weapon wheel slot 0..7
    float damage;         // per bullet / hit
    float range;          // m
    float fireInterval;   // s between shots
    int clipSize;         // 0 = no ammo (melee)
    float reloadTime;
    float spread;         // radians (hip), aiming halves it
    int pellets;
    bool automatic;
    int sfx;              // Audio::Sfx
    int price;            // shop price
    int ammoPrice;        // per clip
    float recoil;         // camera kick (rad)
};
const WeaponInfo& weaponInfo(WeaponType w);

enum PedState : u8 {
    PS_ONFOOT = 0,
    PS_ENTERING,      // walking to / opening the door / sitting in
    PS_INVEHICLE,
    PS_EXITING,
    PS_RAGDOLL,       // knocked down (car hit, explosion, fall) -> get up or die
    PS_GETUP,
    PS_DEAD,
    PS_SWIM,
};

struct Ragdoll;

// AI brain types (population, police, mission)
enum BrainType : u8 {
    BRAIN_NONE = 0,     // player or scripted
    BRAIN_WANDER,       // pedestrians strolling sidewalks
    BRAIN_SCENARIO,     // standing/sitting idle activity at a spot
    BRAIN_FLEE,
    BRAIN_COWER,
    BRAIN_COMBAT,       // attack a target (gangs, cops, mission enemies)
    BRAIN_ARREST,       // police on foot approaching the player
    BRAIN_DRIVER,       // driving a vehicle (traffic / police / mission)
    BRAIN_PASSENGER,
    BRAIN_FOLLOW,       // follow a leader (buddies)
    BRAIN_GOTO,         // scripted go-to position
};

struct Brain {
    BrainType type = BRAIN_NONE;
    int target = -1;          // ped id (combat/flee from/follow)
    dvec3 goal;               // go-to / flee-from position
    float timer = 0.f;        // generic state timer
    float thinkTimer = 0.f;   // time until next decision
    int scenario = -1;        // stance/clip for scenario brains
    int path = -1;            // sidewalk/lane path state (population modules)
    int sub = 0;              // sub-state
    float aggression = 0.5f;
    float accuracy = 0.5f;
    float bravery = 0.5f;
    bool alerted = false;
    vec2 wanderDir;
    int edge = -1;            // current road edge (wander along sidewalks / drive along lanes)
    float edgeS = 0.f;
    int edgeDir = 1;
    float side = 1.f;         // sidewalk side (+1 right of edge direction, -1 left)
    float speed = 1.4f;
};

struct GameWorld;

struct Ped {
    bool target_is_valid(const GameWorld& g) const;
    bool used = false;
    u32 uid = 0;
    int charIndex = -1;           // character cache entry
    Anim::Animator anim;
    Anim::AnimInput animIn;
    mat4 skin[Anim::B_COUNT];
    mat4 bones[Anim::B_COUNT];    // model-space bone transforms (attachments, hit tests)
    dvec3 pos;                    // feet position
    float yaw = 0.f;              // facing, 0 = +Y, CCW positive
    vec3 vel;
    bool grounded = true;
    float groundZ = 0.f;
    float airTime = 0.f;
    float fallStartZ = 0.f;
    PedState state = PS_ONFOOT;
    float stateTime = 0.f;
    float health = 100.f, maxHealth = 100.f, armor = 0.f;
    Faction faction = FAC_CIVILIAN;
    bool isPlayer = false;
    bool persistent = false;      // mission/story peds are never despawned by population
    bool invincible = false;
    bool female = false;
    // vehicle
    int vehicle = -1, seat = -1;
    int targetVehicle = -1, targetSeat = -1;
    bool jacking = false;
    // weapons
    int ammo[WPN_COUNT] = {};     // total ammo (incl. clip)
    int clip[WPN_COUNT] = {};
    bool hasWeapon[WPN_COUNT] = {};
    WeaponType weapon = WPN_FISTS;
    float fireTimer = 0.f, reloadTimer = 0.f, meleeTimer = 0.f;
    bool aiming = false, firing = false;
    vec3 aimDir = vec3(0, 1, 0);
    float aimPitch = 0.f;
    float spreadHeat = 0.f;
    // damage / reactions
    float hitReactTimer = 0.f;
    int lastAttacker = -1;
    float lastDamageTime = -100.f;
    double lastGunfireReport = -100.0, lastCrimeReport = -100.0;
    Ragdoll* ragdoll = nullptr;
    // AI
    Brain brain;
    // voice
    Audio::VoiceParams voice;
    float speechCooldown = 0.f;
    // footsteps
    float stepPhase = 0.f;
    // animation extras
    float turnRate = 0.f;
    int pendingAction = -1;
    // traversal (vault/climb) and cover
    int moveMode = 0;             // 0 normal, 1 in cover, 2 vaulting, 3 climbing
    vec3 coverNormal = vec3(0, 1, 0);
    bool coverLow = false;
    float traverseT = 0.f, traverseDur = 0.f;
    vec3 traverseFrom, traverseMid, traverseTo;
    float diveDepth = 0.f;        // swimming: meters below the surface (0 = at the surface)
    vec4 wounds[4];               // bind-pose wound centers + radius (blood on skin/clothes)
    float woundAge[4] = {};
    int woundNext = 0;
    bool hasParachute = false;
    float chuteOpen = 0.f;        // 0 closed .. 1 fully deployed (moveMode 4)
    float visibleDist = 0.f;      // distance to camera (LOD)
    bool shadow = true;
    // melee (melee.cpp): current move with wind-up / contact / recovery, combos, blocking, dodging, staggers
    int meleeMove = -1;           // MeleeMoveId in progress, -1 none
    float meleeT = 0.f;           // time into the current move
    bool meleeHitDone = false;
    int meleeCombo = 0;           // index of the last light attack in the combo chain
    int meleeQueued = 0;          // chained input while a move plays: 0 none, 1 light, 2 heavy
    double meleeLastEnd = -100.0; // when the last move ended (combo window)
    int meleeTarget = -1;         // opponent the move (or the player's lock-on) aims at
    u32 meleeSerial = 0;          // increments per attack (defenders react once per attack)
    bool blocking = false;
    double blockStart = -100.0, blockUntil = -100.0;   // block start (perfect-block window) / AI hold time
    double counterUntil = -100.0; // a perfect block opens a counter-attack window
    float meleeStagger = 0.f;     // can't attack/block while > 0
    float dodgeT = -1.f;          // dodge progress (s), -1 none
    vec2 dodgeDir;
    int dodgeClip = -1;           // CLIP_DODGE_* playing (its root motion moves the capsule)
    float dodgeYaw = 0.f;         // facing when the dodge started (root motion frame)
    float takedownT = -1.f;       // synced stealth takedown in progress (s), -1 none
    int takedownPartner = -1;     // the other ped of the takedown
    bool takedownVictim = false;  // this ped is the victim
    float takedownYaw = 0.f;
    bool silentDeath = false;     // no death cry (stealth kills)
    int meleeReact = 0;           // AI defence scheduled: 0 none, 1 block, 2 dodge
    double meleeReactAt = 0.0;
    vec2 forcedVel;               // movement override (dodge / lunge / knock-back) while forcedT > 0
    float forcedT = 0.f;
    float legInjury = 0.f;        // s of limping left after a leg wound (caps the speed)
    // lip sync of the line being spoken (Speech::lipSync keys on the synthesizer's timeline, real-time clock)
    std::vector<Speech::VisemeKey> lipKeys;
    std::vector<Speech::StyleSpan> lipStyles;   // emotion over the line (facial expression)
    std::vector<Speech::AccentCue> lipAccents;  // stressed syllables (brow raises, nods)
    double lipStart = -1.0;
    int lipIdx = 0;
    // conversation: listening to another ped's line (crossed arms / nods, gaze at the speaker), passing glances
    double listenUntil = -1.0;
    int listenTo = -1;
    int lookPed = -1;             // ped being glanced at / faced while talking (-1 none)
    float lookT = 0.f;            // s left on that glance
    float glanceNext = 0.f;       // s until the next check for someone worth glancing at
    bool phoneCall = false;       // on the phone (held to the ear); set by the phone UI for the player, by AI for NPCs
    bool phoneBrowse = false;     // looking at the phone held in front (the player while the phone UI is open)
};

struct Vehicle {
    bool used = false;
    u32 uid = 0;
    int model = -1;
    Vehicles::VehicleState sim;
    Vehicles::VehicleControls ctl;
    vec3 color0 = vec3(0.6f), color1 = vec3(0.1f);
    float dirt = 0.1f;
    int seats[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bool lightsOn = false;
    bool sirenOn = false, hornOn = false;
    int indicator = 0;            // -1 left, 1 right
    bool alarm = false;
    bool persistent = false;      // player-owned / mission vehicles
    bool locked = false;
    bool parked = false;
    bool playerUsed = false;      // the player drove it (keeps it around longer)
    bool exploded = false;
    float fireTimer = 0.f;
    float wreckTime = 0.f;
    float idleTime = 0.f;
    int radio = -1;               // remembered station
    Faction faction = FAC_CIVILIAN;
    Brain* driverBrain = nullptr; // convenience (driver ped's brain)
    // AI lane following state (traffic module)
    int laneEdge = -1, laneDir = 1, laneIndex = 0;
    float laneS = 0.f;
    int nextEdge = -1, nextDir = 1;
    float cruiseSpeed = 12.f;
    float stuckTime = 0.f;
    // audio
    Audio::EmitterHandle sndEngine = 0, sndSiren = 0, sndSkid = 0, sndHorn = 0, sndExtra = 0, sndAlarm = 0;
    // fx
    int skidTrack[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    float exhaustTimer = 0.f;
    float smokeTimer = 0.f;
    float lastImpactSfx = 0.f;
    float visibleDist = 0.f;
    bool scripted = false;        // moved kinematically by gameplay (ambient air/sea traffic, cutscenes)
    bool renderFar = false;       // drawn up to the horizon (aircraft)
    bool windowsBroken = false;   // shattered by gunfire or a hard crash (glass no longer drawn)
    bool shiftLatch = false;      // a gear change happened in one of this frame's physics substeps
    int glassHits = 0;
    // customization (mod shop): visual parts are read by the render/FX code, handling parts by applyVehicleMods
    struct Mods {
        u8 finish = 0;            // paint finish: 0 gloss, 1 metallic, 2 pearl, 3 matte, 4 chrome
        u8 engine = 0, brakes = 0, transmission = 0, suspension = 0;   // upgrade levels 0..3
        u8 armor = 0;             // 0..5 (-15% body damage per level)
        bool turbo = false;
        u8 tint = 0;              // window tint 0 stock, 1 light, 2 dark, 3 limo
        vec3 neon = vec3(0.f);    // underglow colour (black = none)
        vec3 smoke = vec3(1.f);   // tire smoke colour
    } mods;
};

enum PickupType : u8 { PICK_MONEY = 0, PICK_HEALTH, PICK_ARMOR, PICK_WEAPON, PICK_COLLECTIBLE, PICK_PACKAGE };

struct Pickup {
    bool used = false;
    PickupType type = PICK_MONEY;
    int amount = 0;
    WeaponType weapon = WPN_PISTOL;
    dvec3 pos;
    float respawn = -1.f;     // seconds until respawn after pickup (-1 never)
    float timer = 0.f;
    int collectibleId = -1;
    bool mission = false;
    float life = -1.f;        // despawn timer for dropped pickups
};

enum ProjectileType : u8 { PROJ_GRENADE = 0, PROJ_MOLOTOV, PROJ_ROCKET };

struct Projectile {
    bool used = false;
    ProjectileType type = PROJ_GRENADE;
    dvec3 pos;
    vec3 vel;
    float fuse = 0.f;
    int owner = -1;
};

struct Fire {
    bool used = false;
    dvec3 pos;
    float radius = 1.f;
    float life = 0.f;
    Audio::EmitterHandle snd = 0;
};

// ------------------------------------------------------------------------------------------------------------------
// Character & vehicle asset caches (procedurally generated at load time)
struct CharEntry {
    Anim::CharacterDesc desc;
    Anim::Skeleton skel;
    Render::Model* model = nullptr;        // LOD0 (~16k tris)
    Render::Model* lods[2] = {nullptr, nullptr};   // LOD1 (~4.5k, 15-40 m), LOD2 (~1.5k, beyond)
    int role = 0;
};

struct VehicleAsset {
    Vehicles::VehicleModel spec;
    Render::Model* body = nullptr;
    Render::Model* bodyLod[2] = {nullptr, nullptr};   // LOD1 (40-120 m, wheels = wheelLod1), LOD2 (beyond, wheels merged)
    Render::Model* wheelLod1 = nullptr;
    Render::Model* wheel = nullptr;
    Render::Model* rotor = nullptr;
    Render::Model* tailRotor = nullptr;
};

// Damage event types
enum DamageType : u8 { DMG_BULLET = 0, DMG_MELEE, DMG_EXPLOSION, DMG_VEHICLE, DMG_FALL, DMG_FIRE, DMG_DROWN };

struct CrimeEvent {
    int type;         // see police.cpp
    dvec3 pos;
    int victim;
};

}  // namespace Game
