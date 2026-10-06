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

// what a venue ped does at its spot (population.cpp lays the venues out, pedai.cpp plays them)
enum VenueMode : u8 {
    VM_STAND = 0,      // idling, glancing around now and then
    VM_GUARD,          // gate / door duty: looks round, waves vehicles through, points the way
    VM_SMOKE,          // smoke break
    VM_TALK,           // chatting (in a ring with the others of the group)
    VM_PHONE,          // on the phone
    VM_LEAN,           // leaning back on a wall or a vehicle
    VM_PACE,           // walks between two spots and idles at each (dock workers between the gate and the stacks)
    VM_SIT,            // sitting on the ground
    VM_WATCH,          // looking out over the water for long still spells (anglers)
    VM_SPOTTER,        // looking out and pointing things out (birders)
    VM_TRAVEL_IN,      // traveler from the curb into a terminal door (a stream, then gone inside)
    VM_TRAVEL_OUT,     // traveler out of a door to the curb: waits on the phone, then hails a taxi
    VM_FAREWELL,       // saying goodbye at the curb, then in through the doors
    VM_SEEOFF,         // ... the one seeing them off: a wave, then back into the car (or off along the sidewalk)
    VM_QUEUE,          // in line at a taxi rank: steps up as the head of the line leaves
    VM_BOARD,          // on the way to a vehicle's kerb-side door, then in (and the vehicle pulls away)
    VM_WORK,           // hands-on work at the spot: crouched over it a while (twist-locks, tackle), then up for a look round
    VM_MEET,           // at the curb beside a car, waiting for someone coming out of the terminal: a hug, then off together
    VM_ROUTE,          // out of one door along the walkways (zebras and all) to another, and in (a stream)
    VM_SEAT,           // on a seat (a cafe chair, a bench, a bleacher plank)
    VM_JOG,            // running laps round a ring of points (a track)
    VM_STROLL          // strolling a chain of points end to end (a park walk), then on as an ordinary walker
};

struct GameWorld;
// one frame of a venue ped at its spot (population.cpp): true when it has moved on to something else this frame
bool aiVenueStep(GameWorld& g, int id, float dt);

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
    ACT_ROADRAGE,      // driver who got out to yell at (and maybe fight) the player after a crash
    ACT_QUEUE,         // standing in line outside a club (population.cpp moves the line along)
    ACT_ERRAND,        // delivery driver: van double-parked, walks to a door, waits there, walks back and drives on
    ACT_CROSS,         // jaywalking straight across a quiet street (BRAIN_GOTO), back to the sidewalk graph on the far side
    ACT_DRIVE_OFF,     // walking to a car parked at the curb: gets in and pulls out into traffic (traffic.cpp)
    ACT_LEAVE_CAR,     // just parked at the curb: round the back of the car to the sidewalk, then into a building nearby
    ACT_VENUE,         // a place's own working crowd (population.cpp venues): gate guards, dock workers, taxi drivers,
                       // travelers at the terminal curb, anglers on the causeway - see VenueMode
    ACT_MEET,          // ran into someone they know on the sidewalk: a greeting, a few words, then on their way
                       // (pedai.cpp aiStreetMeets)
    ACT_HURT,          // down hurt after a knock-down at very low health: on the back, writhing (stance 24), calling for
                       // help until a medic has seen to them (or a long while passes), then up and off (pedai.cpp)
    ACT_AID,           // a passer-by helping someone down hurt: kneeling beside them, or standing by on the phone
    ACT_CUFFED,        // arrested (police.cpp escort): walked to a patrol car in front of the officer holding them and put
                       // in the back; sitting on the kerb while a car is on its way (pedai.cpp)
    ACT_STATEMENT,     // a witness telling an officer what they saw (police.cpp statements): waiting where they stood for
                       // the officer to walk up, then talking while the officer takes it down, a point at where it happened
    ACT_STOPPED,       // stopped on the sidewalk by officers on a foot beat (police.cpp police_stop): the ID handed over,
                       // a wait while it is checked, then on their way - or a warrant and the cuffs, or a run for it
    ACT_COP_BREAK,     // a patrol crew on a break (traffic.cpp): pulled over at the kerb outside a shop, coffee in hand by
                       // the car, a few minutes' talk, then back in and on patrol (pedai.cpp)
};

// Ambient speech categories (barks.cpp)
enum BarkKind : int {
    BK_GREET = 0, BK_BUMP, BK_INSULT, BK_PANIC, BK_FLEE, BK_COWER, BK_CALL_POLICE, BK_PHONE_REPORT, BK_HANDS_UP,
    BK_CARJACKED, BK_HONK, BK_CRASH, BK_THANKS, BK_HELP, BK_DRUNK, BK_MUSIC_PRAISE, BK_TOURIST, BK_FILMING, BK_GANG_WARN,
    BK_GANG_ATTACK, BK_GANG_TAUNT, BK_COP_FREEZE, BK_COP_GROUND, BK_COP_SPOTTED, BK_COP_LOST, BK_COP_CHATTER,
    BK_COP_ENGAGE, BK_COP_COVER, BK_COP_ARREST, BK_COP_DOWN, BK_MUGGER, BK_VICTIM, BK_ARGUE, BK_RACE, BK_MEDIC,
    BK_BREAKDOWN, BK_DIVE, BK_GUN_SEEN, BK_COP_SEARCH, BK_COP_BACKUP, BK_WITNESS_STOP, BK_JOG, BK_PHONE_CHAT, BK_BOUNCER, BK_ROAD_RAGE,
    BK_COP_MEGAPHONE, BK_NICE_CAR, BK_TICKET, BK_TICKETED,
    BK_REUNION, BK_SMALLTALK, BK_PARTING,   // two acquaintances running into each other (pedai.cpp aiStreetMeets)
    BK_ARRIVAL, BK_ARRIVED, BK_SENDOFF, BK_LEAVING,   // at the airport curb: the one waiting / the traveler, a pick-up and
                                                      // a drop-off (population.cpp)
    BK_HURT, BK_SAMARITAN,                            // someone down hurt, and a passer-by helping them (pedai.cpp)
    BK_ONLOOKER,                                      // watching the police make an arrest (pedai.cpp STIM_ARREST)
    BK_SUSPECT, BK_COP_ESCORT, BK_COP_TRANSPORT,      // in cuffs on the way to the car, the officer walking them, and
                                                      // the officer with no car calling one
    BK_BRAWL, BK_BRAWL_FRIEND,                        // squaring up on the sidewalk, and the friend trying to calm it
    BK_COP_STATEMENT, BK_COP_STATEMENT_END, BK_WITNESS,   // an officer taking a witness's statement (questions, the
                                                      // thanks at the end) and the witness telling it (police.cpp)
    BK_COP_STOP, BK_COP_STOP_ASK, BK_COP_STOP_OK,      // a beat officer stopping someone on the sidewalk (the call,
    BK_STOPPED, BK_STOPPED_END,                       // the questions, letting them go) and the one stopped (police_stop)
    BK_COP_BREAK,                                     // two officers on a coffee break by their car, shooting the breeze
    BK_COUNT
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
    u8 walkStance = 0;         // upper-body activity while walking (8 phone call, 10 smoking, 7 talking in a group)
    float walkStanceTimer = 0.f;
    // group walking
    int leader = -1;
    u32 leaderUid = 0;
    vec2 slot;                 // formation offset (leader frame)
    int handWith = -1;         // a couple walking hand in hand (pedai.cpp, set on both): the other one (peds.cpp holds the
    u32 handUid = 0;           // hands together: each one's grabTarget is the point between them) ...
    float handT = -1.f;        // ... last confirmed (game time; the companion renews it every frame they hold on)
    i8 couple = -1;            // a companion walking beside a leader: -1 not decided yet, 0 no, 1 a couple (hand in hand)
    // perception
    float fear = 0.f;
    vec2 threatPos;
    int threatPed = -1;
    float threatTime = -100.f;
    float linger = 0.f;        // gang: armed player loitering nearby
    bool provoked = false;
    // witness
    int report = -1;
    u8 panicDepth = 0;         // how many hand-offs the panic that made this ped flee went through (contagion limit)
    float panicEmit = 0.f;     // time until this fleeing ped spreads panic again
    // speech
    float barkCooldown = 0.f;
    // police on foot
    u8 tactic = 0;
    float tacticTimer = 0.f;
    vec2 tacticPos;
    int coverVeh = -1;
    float shoutTimer = 0.f;
    float tackleTimer = 0.f;   // foot chase: cooldown between tackle attempts
    bool k9Handler = false;    // works a police dog (police.cpp K9 unit)
    int searchSpot = -1;       // lost the suspect: the corner / doorway this officer is checking (police.cpp search plan)
    float searchT = 0.f;       // ... time spent getting there
    float searchLook = -1.f;   // ... the look round it once there (counts down; -1 not there yet)
    int escortPed = -1;        // walking an arrested suspect (ACT_CUFFED) to a patrol car (police.cpp escort) ...
    u32 escortUid = 0;
    int escortCar = -1;        // ... this one (-1: none close - one has been called) ...
    u32 escortCarUid = 0;
    u8 escortSeat = 0;         // ... to this seat (the back where there is one)
    float escortT = 0.f;       // ... for this long
    int stmtWith = -1;         // a statement (police.cpp statements): the officer's witness / the witness's officer ...
    u32 stmtUid = 0;
    float stmtT = 0.f;         // ... how long it has gone on (the officer's clock paces both)
    vec2 stmtScene;            // ... where it happened (the witness points there)
    int stopPed = -1;          // a sidewalk stop (police.cpp police_stop): the officers' stopped ped / the stopped ped's
    u32 stopUid = 0;           // officer (the one doing the talking) ...
    float stopT = 0.f;         // ... the clock (the talking officer's runs from being in front of them; it paces all three)
    u8 breakSeat = 0;          // ACT_COP_BREAK: the seat they got out of (0: back behind the wheel)
    u8 stopOutcome = 0;        // ... how it ends (the talking officer: 0 on their way, 1 a warrant, 2 they run for it)
    bool stopCover = false;    // ... this officer is the partner standing by
    // events / vehicles
    int eventId = -1;          // ambient event slot this ped belongs to (events.cpp), -1 none
    int aimAt = -1;            // ACT_EVENT: ped held at gunpoint (mugger)
    int targetVeh = -1;
    int homeVeh = -1;
    // movement bookkeeping
    vec2 lastPos;
    float stuckTimer = 0.f;
    float diveCooldown = 0.f;
    // venue crowd (ACT_VENUE): which venue slot holds this ped, what it does there, a second spot (pacing / door)
    int venue = -1;
    u8 venueMode = 0;
    bool venueDriver = false;   // VM_BOARD: takes the wheel (else a passenger seat)
    bool goInside = false;      // a walker headed for a door (walk.dest): gone once there (population.cpp)
    bool browsing = false;      // ACT_SCENARIO: stopped at a shop window (pedai.cpp)
    bool knockedDown = false;   // was down (a ragdoll, the get-up) since the brain last ran (ai.cpp)
    float hurtCare = 0.f;       // ACT_HURT: a medic has seen to them this long (-1: getting up)
    int aidPed = -1;            // ACT_AID: the one they are helping
    float sceneT = -100.f;      // when they last stopped to watch the police at work (STIM_ARREST: once per scene)
    int greetWith = -1;         // a greeting (CLIP_HUG / HANDSHAKE / CHEEK_KISS, started on both together): the partner,
                                // from stepping in until they part (peds.cpp: their chest / head for the hands) ...
    float greetT = 0.f;         // ... and the time the clip has left (> 0 while it plays)
    i16 routeAt = 0;            // VM_JOG / VM_STROLL: the point of the venue route they are heading for
    i8 routeDir = 1;            // ... and which way along it
    vec2 anchorB;
    float anchorBYaw = 0.f;
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
    float heldUp = 0.f;        // police on the way: stuck in traffic this long (police.cpp moves them on out of view)
    int abandonedBy = -1;
    // road rage after the player crashed into us: 0 none, 1 stopping, 2 driver out on foot
    u8 rage = 0;
    float rageTimer = 0.f;
    u8 pursuitMove = 0;        // police: 0 chase, 1 PIT run, 2 boxing slot (counted on entry)
    double escortHold = -1.0;  // police: an officer is bringing a prisoner to it - nobody drives off before this (game time)
    int transportFor = -1;     // PT_TRANSPORT: the officer holding a suspect who called for this car ...
    u32 transportUid = 0;
    u8 transportState = 0;     // ... 0 on the way, 1 pulling over there, 2 waiting at the kerb
    float megaphoneTimer = 0.f;   // police: next "pull over" order over the car loudspeaker
    float impactCd = 0.f;         // telemetry: one hard impact counted per crash
    float hungTime = 0.f;         // hung up on a ledge / kerb: wheels off the ground, going nowhere
    u8 errand = 0;                // delivery stop: 1 pulling over, 2 driver out at a door
    u8 copBreak = 0;              // a patrol's coffee break: 1 pulling over, 2 the crew out by the car, 3 time to go
    float errandTimer = 0.f;
    vec3 errandDoor;
    vec3 flyTgtPrev;              // helicopter autopilot: last target position and its smoothed velocity
    vec2 flyTgtVel;
    bool flyTgtInit = false;
    u8 pullOut = 0;               // leaving a parking spot: 1 blinker on, waiting for a gap, 2 steering out into the lane
    float pullTimer = 0.f;
    u8 parking = 0;               // arriving: 1 pulling into a free spot in the parking strip (the driver then walks off)
    float parkTimer = 0.f;
    int parkLane = -1;
    float parkLat = 0.f;          // lateral offset of the spot from the lane center
    vec3 parkDoor;                // where the driver is headed
    float alarmT = 0.f;           // how long a parked car's alarm has been going
    vec2 lastVel = vec2(0.f);     // horizontal velocity last frame (knocks judged from the change: ai.cpp)
    bool lastVelOk = false;
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
    dvec3 origin;      // the original danger (panic: where the people running away came from)
    u8 kind = 0;       // StimulusKind
    u8 depth = 0;      // panic hand-offs so far
    int source = -1;   // ped responsible (-1 unknown)
    float radius = 30.f;
    float time = 0.f;
    bool player = false;
};
enum StimulusKind : u8 {
    STIM_GUNFIRE = 0, STIM_EXPLOSION, STIM_FIGHT, STIM_BODY, STIM_CRASH, STIM_ARMED, STIM_SIREN, STIM_FIRE, STIM_PANIC, STIM_HORN,
    STIM_ARREST   // the police with someone at gunpoint, hands up, on the ground or in cuffs (police.cpp): people stop to watch
};

// A place that needs emergency services (injured/dead ped, burning car, NPC crime) or police attention.
struct Incident {
    dvec3 pos;
    u8 kind = 0;       // 0 medical, 1 fire, 2 NPC crime (police), 3 crash
    float time = 0.f;
    int unit = -1;     // vehicle responding
    int perp = -1;     // NPC criminal (kind 2)
    u32 perpUid = 0;
    bool active = false;
};

// Police unit tasks (VehAI::task for police vehicles) and on-foot tactics (PedAI::tactic)
enum PoliceTask : u8 {
    PT_NONE = 0, PT_PURSUE, PT_SEARCH, PT_ROADBLOCK, PT_RESPOND, PT_RETURN,
    PT_TRANSPORT   // called to take a prisoner: to the officer holding them, pulled over at the kerb there until they are in
};
enum PoliceTactic : u8 { FT_APPROACH = 0, FT_COVER, FT_FLANK, FT_ARREST, FT_SEARCH, FT_RETURN, FT_ENGAGE };

struct AIFrameStats {
    double msAI = 0, msTraffic = 0, msPeds = 0, msPop = 0, msPolice = 0, msEvents = 0;
    int peds = 0, cars = 0, dummies = 0, managed = 0;
    double avgMs = 0, maxMs = 0;
    int frames = 0;
    // cumulative behaviour counters (aiCensusText, autoplay logs)
    int panicSpread = 0, filming = 0, pitTries = 0, boxing = 0, roadblocks = 0, spikeHits = 0, tackles = 0, heliUnits = 0,
        unitsSent = 0, roadRage = 0, events = 0, arrests = 0, custody = 0, transports = 0, statements = 0,
        shelters = 0,   // (shelters: bystanders who ran in through a door from trouble, pedai.cpp)
        stops = 0, stopArrests = 0, stopRuns = 0,   // sidewalk stops by beat officers, the warrants among them, the runners
        copBreaks = 0;                              // patrols pulled over for a coffee break
    int hardImpacts = 0, impactsWithPlayer = 0;   // AI-driven cars: impulses > 3000 N s (sampled per frame)
    int unhung = 0;                               // cars lifted off a ledge back onto their lane
    int departures = 0, arrivals = 0;             // cars driven away from / parked at the curb by their owners
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
    float lifeBoost = 1.f;              // autoplay tests: cars park / owners drive off this many times as often
    int testCar[2] = {-1, -1};          // autoplay tests: the cars a scenario set up (app.cpp)
    vec3 testCam;                       // autoplay tests: scenario camera position
    bool forceBender = false;           // autoplay tests: every low-speed knock between two traffic cars becomes a scene
    int forceEvent = -1;                // autoplay tests: the ambient event type to stage next (events.cpp), soon and close;
                                        // a value past the last type: no ambient events at all
    int forceStop = -1;                 // autoplay tests: a beat officer close by stops the next passer-by at once, with this
                                        // outcome (police_stop: 0 on their way, 1 a warrant, 2 a run for it); -1 none forced
    double lastParkArrive = -1e9;       // last time a traffic car started pulling into a parking spot (global spacing)
    double lastCopBreak = -1e9;         // last time a patrol pulled over for a coffee break (one every few minutes)
    bool forceCopBreak = false;         // autoplay tests: the next patrol close by takes its break at once
    // the player giving up (police.cpp): wanted, on foot, nothing in hand - hold the phone key and the hands go up;
    // officers who see it hold their fire, close in with guns trained and cuff them: a lighter bust (weapons kept, half
    // the fine back after the release). Moving, drawing or firing breaks it.
    bool surrender = false;
    float surrenderHold = 0.f;          // phone key held this long
    bool surrenderCtl = false;          // the controls are ours while the hands are up
    bool forceSurrender = false;        // autoplay tests: hands up at once
    bool dispatchOff = false;           // autoplay tests: no response cars are sent (the K9 test: the dog team alone works
                                        // the trail)
    bool surrenderBust = false;         // the current bust came from a surrender
    bool bustWatch = false;             // (busted: waiting for the release to hand things back)
    long long bustMoney = 0;
    bool bustHas[16] = {};
    int bustAmmo[16] = {}, bustClip[16] = {};
    int bustWeapon = 0;
    // acquaintances who ran into each other on the sidewalk (pedai.cpp aiStreetMeets): a stop, a greeting, a chat
    struct StreetMeet {
        int a = -1, b = -1;             // (a noticed b)
        u32 ua = 0, ub = 0;
        i8 clip = -1;                   // the greeting (Anim::Clip)
        u8 phase = 0;                   // 0 stepping in, 1 greeting, 2 talking, 3 parting
        float t = 0.f;                  // time left in the phase
        float sayT = 0.f;               // until the next line
        u8 turn = 0;                    // who speaks next
        u8 lines = 0;                   // lines said so far
    };
    std::vector<StreetMeet> meets;
    float meetScan = 0.f;               // next look for two who know each other
    float meetGap = 15.f;               // no new meeting before this (a few a minute at most round the player)
    int meetsStarted = 0;
    float meetBoost = 1.f;              // autoplay tests: acquaintances meet this many times as often
    bool ready = false;
};

}  // namespace Game
