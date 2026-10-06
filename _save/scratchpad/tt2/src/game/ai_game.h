// Game-side AI state shared by the AI modules (ai.cpp, traffic.cpp, pedai.cpp, police.cpp, events.cpp, barks.cpp,
// population.cpp). Per-ped / per-vehicle records live in parallel arrays indexed like GameWorld::peds / vehicles and
// are re-initialized automatically when the entity's uid changes (pool slots are reused).
#pragma once
#include "ai_core.h"

namespace Game {

enum PedRole : u8 {
    PR_CIVILIAN = 0, PR_BUSINESS, PR_BEACH, PR_WORKER, PR_JOGGER, PR_GANG, PR_COP, PR_MEDIC, PR_TOURIST, PR_DRUNK,
    PR_MUSICIAN, PR_NIGHTLIFE, PR_SWAT, PR_FIREFIGHTER, PR_COUNT
};

enum PedActivity : u8 {
    ACT_WALK = 0,      // sidewalk wandering (with destinations)
    ACT_SCENARIO,      // standing / sitting activity at an anchor (bench, wall, phone, smoke, sunbathe, dance, talk)
    ACT_GROUP,         // walking with a group leader
    ACT_CALL_POLICE,   // witness phoning in a crime
    ACT_FILM,          // bystander filming a fight
    ACT_WATCH,         // watching a performer / event from a spot
    ACT_HANDS_UP,      // held at gunpoint
    ACT_JOG,           // jogging (faster walker, jog idle at crossings)
    ACT_WAIT_BUS,      // waiting at a bus stop for a bus
    ACT_HAIL_TAXI,     // waving for a taxi at the curb
    ACT_ENTER_VEH,     // walking to a vehicle door (taxi/bus) to board
    ACT_EVENT,         // scripted by an ambient event
    ACT_CONFRONT,      // gang member squaring up to the player
    ACT_INSPECT,       // walking to look at something (body, crash, event)
};

// Ambient speech categories (barks.cpp)
enum BarkKind : int {
    BK_GREET = 0, BK_BUMP, BK_INSULT, BK_PANIC, BK_FLEE, BK_COWER, BK_CALL_POLICE, BK_PHONE_REPORT, BK_HANDS_UP,
    BK_CARJACKED, BK_HONK, BK_CRASH, BK_THANKS, BK_HELP, BK_DRUNK, BK_MUSIC_PRAISE, BK_TOURIST, BK_FILMING, BK_GANG_WARN,
    BK_GANG_ATTACK, BK_GANG_TAUNT, BK_COP_FREEZE, BK_COP_GROUND, BK_COP_SPOTTED, BK_COP_LOST, BK_COP_CHATTER,
    BK_COP_ENGAGE, BK_COP_COVER, BK_COP_ARREST, BK_COP_DOWN, BK_MUGGER, BK_VICTIM, BK_ARGUE, BK_RACE, BK_MEDIC,
    BK_BREAKDOWN, BK_DIVE, BK_GUN_SEEN, BK_COP_SEARCH, BK_COP_BACKUP, BK_WITNESS_STOP, BK_JOG, BK_PHONE_CHAT, BK_COUNT
};

struct PedAI {
    u32 uid = 0;
    AI::Walker walk;
    bool navOk = false;
    u8 role = PR_CIVILIAN;
    u8 activity = ACT_WALK;
    u8 temper = 1;             // 0 timid, 1 normal, 2 bold
    float actTimer = 0.f;
    float think = 0.f;         // staggered decision timer
    // scenario
    vec2 anchor;
    float anchorYaw = 0.f;
    int stance = 0;
    int clip = -1;             // looping scenario clip re-issued when finished
    float clipTimer = 0.f;
    // group walking
    int leader = -1;
    u32 leaderUid = 0;
    vec2 slot;                 // formation offset (leader frame)
    // perception
    float fear = 0.f;
    vec2 threatPos;
    int threatPed = -1;
    float threatTime = -100.f;
    float linger = 0.f;        // gang: armed player loitering nearby
    bool provoked = false;
    // witness
    int report = -1;
    // speech
    float barkCooldown = 0.f;
    // police on foot
    u8 tactic = 0;
    float tacticTimer = 0.f;
    vec2 tacticPos;
    int coverVeh = -1;
    float shoutTimer = 0.f;
    // events / vehicles
    int eventId = -1;
    int targetVeh = -1;
    int homeVeh = -1;
    // movement bookkeeping
    vec2 lastPos;
    float stuckTimer = 0.f;
    float diveCooldown = 0.f;
};

enum VehRole : u8 {
    VR_TRAFFIC = 0, VR_BUS, VR_TAXI, VR_AMBULANCE, VR_FIRETRUCK, VR_POLICE, VR_POLICE_HELI, VR_POLICE_BOAT, VR_ROADBLOCK,
    VR_RACER, VR_EVENT, VR_BREAKDOWN, VR_SWAT
};

struct VehAI {
    u32 uid = 0;
    u8 role = VR_TRAFFIC;
    bool managed = false;      // the traffic core drives it
    float offView = 0.f;       // time continuously out of view (dummy conversion hysteresis)
    // police / emergency tasks
    u8 task = 0;
    float taskTimer = 0.f;
    vec2 taskPos;
    float repath = 0.f;
    float stuckTimer = 0.f;
    float reverseTimer = 0.f;
    // taxi / bus / emergency
    int fare = -1;
    vec2 dest;
    float stopTimer = 0.f;
    int scene = -1;
    // misc
    float barkTimer = 0.f;
    float fleeTimer = 0.f;
    int eventId = -1;
    float hornBarkTimer = 0.f;
    float sirenTimer = 0.f;
    int abandonedBy = -1;
};

// A crime the police do not know about yet: a witness is phoning it in.
struct PendingReport {
    int type = 0;
    dvec3 pos;
    int victim = -1;
    int caller = -1;
    u32 callerUid = 0;
    float timer = 0.f;
    bool active = false;
};

// Something scary happened (gunfire, explosion, crash, fight, body): peds and drivers nearby react.
struct Stimulus {
    dvec3 pos;
    u8 kind = 0;       // StimulusKind
    int source = -1;   // ped responsible (-1 unknown)
    float radius = 30.f;
    float time = 0.f;
    bool player = false;
};
enum StimulusKind : u8 { STIM_GUNFIRE = 0, STIM_EXPLOSION, STIM_FIGHT, STIM_BODY, STIM_CRASH, STIM_ARMED, STIM_SIREN, STIM_FIRE };

// A place that needs emergency services (injured/dead ped, burning car, NPC crime) or police attention.
struct Incident {
    dvec3 pos;
    u8 kind = 0;       // 0 medical, 1 fire, 2 NPC crime (police), 3 crash
    float time = 0.f;
    int unit = -1;     // vehicle responding
    int perp = -1;     // NPC criminal (kind 2)
    bool active = false;
};

struct AIFrameStats {
    double msAI = 0, msTraffic = 0, msPeds = 0, msPop = 0, msPolice = 0, msEvents = 0;
    int peds = 0, cars = 0, dummies = 0, managed = 0;
    double avgMs = 0, maxMs = 0;
    int frames = 0;
};

struct AIState {
    std::vector<PedAI> ped;
    std::vector<VehAI> veh;
    std::vector<PendingReport> reports;
    std::vector<Stimulus> stimuli;
    std::vector<Incident> incidents;
    int pedBody0 = 0;          // first ped body index in traffic.bodies (this tick)
    std::vector<int> pedBody;  // ped id -> body index
    std::vector<int> vehBody;  // vehicle id -> body index
    float barkGlobal = 0.f;    // global speech rate limiter
    float subtitleGlobal = 0.f;
    int playerLastExplosiveAmmo = 0;
    float playerExplosiveTime = -100.f;
    float playerLastShotTime = -100.f;
    AIFrameStats stats;
    bool ready = false;
};

}  // namespace Game
