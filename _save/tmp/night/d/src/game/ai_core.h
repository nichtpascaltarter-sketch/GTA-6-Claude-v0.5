// Engine-independent core of the ambient AI (no renderer/audio/animation dependencies, so the native test harness
// tools/traffic_sim.cpp runs exactly this code on the real world with the real vehicle physics):
//   - LaneGraph: lanes per road edge and direction with right-hand-traffic offsets, Bezier connectors through every
//     node (legal turn lanes, U-turns only at dead ends), conflict sets, signal plans (protected left arrows),
//     all-way stops (first come first served), priority/yield rules (T-junctions, highway merges), bus stops,
//     and the pedestrian sidewalk graph (sidewalks, corners, crosswalks with walk signals).
//   - TrafficCore: the driver model producing Vehicles::VehicleControls for the real vehicle simulation (pure pursuit
//     lane keeping, Gipps/IDM-like speed planning, stop lines, intersection gating, lane changes, overtaking,
//     personalities, flee/emergency/pursuit modes, stuck recovery) and a kinematic "dummy" mode for far vehicles.
//   - PedCore: sidewalk navigation for pedestrians (wandering with destinations, crossing at crosswalks honoring the
//     walk signal or traffic gaps, local avoidance) producing a desired velocity for the host's character controller.
#pragma once
#include "../world/roads.h"
#include "../sim/vehicle_sim.h"
#include "../core/rng.h"

namespace AI {

// =====================================================================================================================
// Lane graph
// =====================================================================================================================
enum TurnKind : u8 { TK_STRAIGHT = 0, TK_RIGHT, TK_LEFT, TK_UTURN, TK_MERGE };
enum LaneFlags : u8 { LF_HIGHWAY = 1, LF_RAMP = 2, LF_DIRT = 4, LF_ONEWAY = 8, LF_BRIDGE = 16, LF_NOTRAFFIC = 32 };
enum SignalState : u8 { SIG_RED = 0, SIG_AMBER, SIG_GREEN, SIG_ARROW, SIG_NONE };
enum PedSignal : u8 { PED_DONT = 0, PED_FLASH, PED_WALK, PED_UNCONTROLLED };
enum PhaseKind : u8 { PH_LEFT = 0, PH_LEFT_AMBER, PH_GREEN, PH_AMBER, PH_ALLRED };

struct LaneZebra {            // a zebra crossing over a lane
    float u = 0.f;            // lane coordinate where the crossing's near edge meets the lane centre
    int link = -1;            // the WL_ZEBRA walk link
};

struct Lane {
    int edge = -1;
    i8 dir = 1;               // +1 travels n0->n1, -1 travels n1->n0
    u8 index = 0;             // 0 = leftmost lane (median / center line side)
    u8 count = 1;             // lanes in this direction
    u8 cls = 0;               // World::RoadClass
    u8 flags = 0;             // LaneFlags
    float offset = 0.f;       // lateral offset of the lane center from the edge centerline (+ = right of the n0->n1 tangent)
    float width = 3.2f;
    float u0 = 0.f, u1 = 0.f; // usable range in lane coordinates (u = distance along the edge from the start node)
    float stopU = -1.f;       // stop line at the end node (controlled intersections), -1 = none (yield at u1)
    float speed = 12.f;       // speed limit (m/s)
    int fromNode = -1, toNode = -1;
    int left = -1, right = -1;  // neighbor lanes in the same direction
    int group = -1;           // edge * 2 + (dir < 0): all lanes of one road direction
    int approachIn = -1;      // approach index at toNode (incoming), -1 when the node has no approach table
    std::vector<int> out;     // connectors leaving this lane's end
    std::vector<float> busStops;  // u of bus stops on this lane's curb side (rightmost lanes only)
    std::vector<LaneZebra> zebras;  // mid-block zebra crossings over this lane, by u
};

struct Conflict {
    int other;              // connector id at the same node
    float s, sOther;        // conflict zone entry: arc length along this connector / the other where the paths come close
    float sEnd, sOtherEnd;  // conflict zone exit (a vehicle has cleared the conflict when its rear passes it)
    u8 merge;               // 1 = both end in the same lane
    u8 yield;               // 1 = this connector yields to the other when both have the right to go
};

struct Connector {
    int from = -1, to = -1;   // lanes
    int node = -1;
    u8 turn = TK_STRAIGHT;
    u8 approach = 0;          // incoming approach index at the node
    u8 outApproach = 0;
    i8 prio = 0;              // priority class for yield decisions (uncontrolled / permissive movements)
    bool grade = false;       // grade-separated crossing (highway flyover): no crossing conflicts
    float length = 0.f;
    float maxSpeed = 30.f;    // curvature limited (comfort lateral acceleration 3 m/s^2)
    float holdS = 0.f;        // where a yielding vehicle waits inside the box (permissive left), 0 = at the entry
    std::vector<vec3> pts;    // samples (~1 m)
    std::vector<float> s;     // cumulative arc length
    std::vector<float> curv;  // signed curvature (1/m, + = turning left) per sample
    std::vector<Conflict> conflicts;
    std::vector<Conflict> wide;   // extra conflicts that only matter when a long vehicle sweeps wider than its path
    float minRadius = 1e9f;       // tightest radius along the path (long vehicles avoid turns tighter than they can follow)
};

struct Approach {
    int edge = -1;
    bool outgoing = false;    // the edge starts at this node
    vec2 dir;                 // unit, from the node outward along the edge
    float ang = 0.f;
    u8 axis = 0;              // signal axis
    u8 rank = 0;              // priority rank at uncontrolled nodes (1 = main road)
    bool crosswalk = false;
    float cut = 0.f;          // intersection box cut-back along the edge
    float crossS = 0.f;       // distance from the node (along the edge) of the crosswalk center line
    float hw = 0.f, sw = 0.f; // paved half width, sidewalk width
    std::vector<int> inLanes, outLanes;
};

struct Phase {
    u8 axis, kind;
    float t0, t1;             // start / end within the cycle
};

struct NodeInfo {
    u8 control = 0;           // 0 uncontrolled (priority rules), 1 all-way stop, 2 signals
    u8 axisCount = 0;
    bool deadEnd = false;
    bool uturnBlocked = false;    // dead end whose turning circle is obstructed (routing avoids it)
    bool gradeSeparated = false;
    int firstConn = 0, connCount = 0;
    std::vector<Approach> approaches;
    std::vector<Phase> phases;
    float cycle = 0.f, offset = 0.f;
    u8 leftPhase[3] = {0, 0, 0};  // axis has a protected left phase
};

// ---- Pedestrian sidewalk graph ----
// WL_PATH: a walkway off the street network (plazas, forecourts: World::SiteSet::walks), straight between its nodes;
// WL_ZEBRA: a mid-block zebra over a site road - no signal, the traffic stops for anyone on it
enum WalkLinkKind : u8 { WL_SIDEWALK = 0, WL_CROSSWALK, WL_CORNER, WL_PATH, WL_ZEBRA };

struct WalkNode {
    vec3 p;
    int roadNode = -1;
    int approach = -1;
    i8 side = 0;                // -1 clockwise side of the approach, +1 counter-clockwise side
    std::vector<int> links;
};

struct WalkLink {
    int a = -1, b = -1;
    u8 kind = WL_SIDEWALK;
    int edge = -1;              // sidewalk: road edge
    float sa = 0.f, sb = 0.f;   // sidewalk: edge s at a and b
    float lat = 0.f;            // sidewalk: signed lateral offset of the sidewalk center (edge frame, + right)
    float halfWidth = 1.f;      // walkable half width
    int node = -1, approach = -1;   // crosswalk: road node / approach (walk signal)
    vec2 ctrl;                  // corner: quadratic control point
    float length = 0.f;
};

// Places where pedestrians do things (from the deterministic street furniture layout)
enum ScenarioPointKind : u8 { SP_BENCH = 0, SP_BUS_STOP };
struct ScenarioPoint {
    vec3 pos;          // where the ped stands / sits (on the sidewalk)
    vec2 face;         // facing direction (toward the street)
    u8 kind = SP_BENCH;
    int edge = -1;
    int lane = -1;     // bus stops: the lane the bus stops on
    float u = 0.f;     // bus stops: lane coordinate of the stop
};

struct LaneGraph {
    const World::RoadNetwork* roads = nullptr;
    std::vector<Lane> lanes;
    std::vector<Connector> conns;
    std::vector<NodeInfo> nodes;
    std::vector<int> groupFirst;          // per group (edge*2+dirIdx): first lane id, -1 if none
    std::vector<u8> groupCount;
    // smoothed edge geometry: per-vertex miter normals (right of n0->n1) and signed curvature (+ = left)
    std::vector<std::vector<vec2>> edgeNormal;
    std::vector<std::vector<float>> edgeCurv;
    // pedestrians
    std::vector<WalkNode> walkNodes;
    std::vector<WalkLink> walkLinks;
    std::vector<ScenarioPoint> spots;
    std::vector<std::vector<int>> spotHash;
    // spatial hash of lanes and walk links (64 m cells, same layout as the road hash)
    int hashRes = 0;
    std::vector<std::vector<int>> laneHash, walkHash;
    double buildSeconds = 0.0;

    void build(const World::RoadNetwork& rn);
    int pathCount() const { return (int)lanes.size() + (int)conns.size(); }
    bool isLane(int path) const { return path >= 0 && path < (int)lanes.size(); }
    const Connector& conn(int path) const { return conns[path - (int)lanes.size()]; }
    int connPath(int c) const { return c + (int)lanes.size(); }
    float pathLength(int path) const;
    float pathSpeed(int path) const;
    // lane geometry: position (with z) at lane coordinate u, shifted `lateral` meters to the right of travel
    vec3 lanePos(int lane, float u, float lateral = 0.f) const;
    vec2 laneTangent(int lane, float u) const;   // travel direction
    float laneCurv(int lane, float u) const;     // signed curvature in travel frame (+ = turning left)
    // generic path geometry (lanes and connectors)
    vec3 pathPos(int path, float u, float lateral = 0.f) const;
    vec2 pathTangent(int path, float u) const;
    float pathCurv(int path, float u) const;
    // projection of a point onto a path near uHint (returns u, lateral error right of travel)
    float projectPath(int path, vec2 p, float uHint, float* lateral) const;
    // nearest lane to p with travel direction roughly along `heading` (use vec2(0) for any), -1 if none in range
    int nearestLane(vec2 p, vec2 heading, float maxDist, float* uOut, float* latOut = nullptr, u8 excludeFlags = LF_NOTRAFFIC) const;
    int laneOf(int edge, int dir, int index) const;
    // signals
    SignalState movementSignal(int node, int approach, int turn, double time) const;
    PedSignal pedSignal(int node, int approach, double time) const;
    int approachForDir(int node, vec2 approachDir) const;  // incoming approach whose traffic travels along approachDir
    // traffic-light prop lamp (0 red, 1 amber, 2 green) for the signal head facing traffic on `edge` near propPos
    int lampStateForEdge(int edge, vec2 propPos, double time) const;
    // pedestrians
    vec3 walkPos(int link, float x, float lateral, bool fromA) const;  // x meters from the link start (a or b)
    vec2 walkTangent(int link, float x, bool fromA) const;
    int nearestWalk(vec2 p, float maxDist, float* xOut, float* latOut = nullptr) const;
    void lanesNear(vec2 mn, vec2 mx, std::vector<int>& out) const;
    void spotsNear(vec2 p, float r, std::vector<int>& out) const;
    void walksNear(vec2 mn, vec2 mx, std::vector<int>& out) const;
};

// =====================================================================================================================
// Perception proxies
// =====================================================================================================================
enum BodyKind : u8 { BK_CAR = 0, BK_PED };
enum BodyFlags : u16 {
    BF_PLAYER = 1, BF_SIREN = 2, BF_POLICE = 4, BF_PARKED = 8, BF_WRECK = 16, BF_AI = 32, BF_DUMMY = 64,
    BF_BIKE = 128, BF_ARMED = 256, BF_FLEEING = 512, BF_CROSSING = 1024, BF_BIG = 2048
};

struct Body {
    vec2 pos, vel, fwd;
    float z = 0.f;
    float halfLen = 0.3f, halfWid = 0.3f;
    float speed = 0.f;
    int host = -1;       // host id (vehicle or ped index)
    int driver = -1;     // TrafficCore driver slot for AI vehicles
    u8 kind = BK_CAR;
    u16 flags = 0;
    int next = -1;       // spatial hash chain
    int cx = 0, cy = 0;
};

struct BodyHash {
    static constexpr float kCell = 12.f;
    static constexpr int kSlots = 2048;
    int head[kSlots];
    BodyHash() { clear(); }   // empty chains until the first build (a query before the first tick must find nothing)
    void clear();
    static u32 slot(int cx, int cy) { return (hash2i(cx, cy) & (kSlots - 1)); }
    void build(std::vector<Body>& bodies);
    // calls f(bodyIndex) for bodies whose cell overlaps the rect
    template <typename F>
    void query(const std::vector<Body>& bodies, vec2 mn, vec2 mx, F f) const {
        int x0 = (int)floorf(mn.x / kCell), x1 = (int)floorf(mx.x / kCell);
        int y0 = (int)floorf(mn.y / kCell), y1 = (int)floorf(mx.y / kCell);
        if ((x1 - x0) * (y1 - y0) > 400) {  // huge query: fall back to a scan
            for (int i = 0; i < (int)bodies.size(); i++) {
                const Body& b = bodies[i];
                if (b.pos.x >= mn.x - kCell && b.pos.x <= mx.x + kCell && b.pos.y >= mn.y - kCell && b.pos.y <= mx.y + kCell) f(i);
            }
            return;
        }
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                for (int i = head[slot(x, y)]; i >= 0; i = bodies[i].next)
                    if (bodies[i].cx == x && bodies[i].cy == y) f(i);
    }
};

// =====================================================================================================================
// Traffic driver model
// =====================================================================================================================
enum DriveMode : u8 {
    DM_NORMAL = 0,   // lane following, rules obeyed
    DM_FLEE,         // drive away from a threat fast, ignoring signals
    DM_EMERGENCY,    // siren run to a destination: slows at red lights, crosses when clear
    DM_PURSUIT,      // host-steered (police chase): the core only provides avoidance helpers
    DM_HOLD,         // stay stopped (bus stop, taxi pickup, parked with driver, scene)
    DM_PULLOVER,     // pull over to the curb and stop (yield to sirens, taxi/bus stops)
    DM_ROUTE,        // lane following toward a destination (taxi, bus, police response, emergency without siren)
};

enum Temperament : u8 { TEMP_CAUTIOUS = 0, TEMP_NORMAL, TEMP_AGGRESSIVE };

struct Personality {
    u8 temper = TEMP_NORMAL;
    float speedFactor = 1.f;    // x speed limit
    float headway = 1.4f;       // s
    float minGap = 2.2f;        // m
    float accel = 2.0f;         // comfortable acceleration m/s^2
    float decel = 3.0f;         // comfortable deceleration m/s^2
    float latAcc = 3.0f;        // comfortable lateral acceleration m/s^2
    float gapTime = 3.5f;       // gap acceptance (s) when yielding
    float patience = 4.f;       // s before honking at a blocker
    float overtake = 4.f;       // speed deficit (m/s) that triggers a discretionary lane change
    bool runsAmber = false;
    bool rightOnRed = true;
    static Personality make(u32 seed, bool forceCautious = false);
};

struct VehicleInfo {         // static per-vehicle data the core needs (from Vehicles::VehicleModel / tuning)
    float halfLen = 2.3f, halfWid = 0.9f;
    float frontLen = 2.3f;      // model origin to the front bumper
    float rearLen = 2.3f;
    float wheelbase = 2.8f;
    float rearAxleY = -1.4f;    // local y of the rear axle
    float frontAxleY = 1.4f;    // local y of the front axle
    float maxSteer = 0.6f;
    float alphaPeak = 0.13f;
    float grip = 1.f;
    float topSpeed = 50.f;
    float mass = 1500.f;        // kg
    float powerW = 120000.f;    // peak engine power (W)
    float dragK = 3e-4f;        // aerodynamic deceleration coefficient (1/m): a = dragK * v^2
    float maxLean = 0.8f;       // bikes: the rider's full lean (rad) - at speed the steer input is a share of it
    bool bus = false, bike = false, big = false;
};

VehicleInfo makeVehicleInfo(const Vehicles::VehicleModel& m, const Vehicles::VehicleState& s);

// A driver doing donuts round a spot (street takeovers): full lock, the rear kicked loose with a tug of the handbrake
// and kept sliding on the throttle; when the circle has wandered off the spot it straightens out, rolls back over it
// and goes round the other way (a figure of eight over the crossing)
struct DonutState {
    int dir = 1;               // +1 clockwise (steering right), -1 anticlockwise
    float hbT = 0.f;           // handbrake tug left (s)
    float kickCd = 0.f;        // until the next tug is allowed (s)
    float spinT = 0.f;         // time spent going round the current way (s)
    float switchAt = 14.f;     // ... when to straighten out and go round the other way
    int phase = 0;             // 0 donut, 1 letting the spin die, 2 lining up past the spot for the next one, 3 backing off
    float phaseT = 0.f;
    float drift = 0.f;         // how far the middle of the circle has wandered off the spot (m, smoothed)
    float smoke = 0.f;         // how hard the rear tires are going (0..1) - for the crowd and the sound
    float blockT = 0.f;        // lining up / going round: pushing against something this long
    float backSteer = 0.f;     // backing off: the lock held while reversing
    float backDist = 0.f;      // ... and how far it has gone
};
Vehicles::VehicleControls donutControls(const Vehicles::VehicleState& s, vec2 spot, float dt, DonutState& st);

struct DriveOut {
    Vehicles::VehicleControls ctl;
    int indicator = 0;          // -1 left, 1 right
    bool horn = false;
    bool brakeLights = false;
    bool wantsAbandon = false;  // flee mode blocked: host should make the driver bail out
    bool stuck = false;
    int blocker = -1;           // body index of the obstacle ahead (when blocked)
};

struct Driver {
    bool active = false;
    u32 uid = 0;
    int vehicle = -1;           // host vehicle id
    Personality pers;
    VehicleInfo info;
    u8 mode = DM_NORMAL;
    bool dummy = false;
    // position on the graph
    int path = -1;              // lane id or lanes.size()+connector id
    float u = 0.f;              // position along the path (lane coordinate / connector arc length) of the model origin
    float latErr = 0.f;         // measured lateral error (right of travel) to the path + planned offset
    static constexpr int kRouteMax = 8;
    int route[kRouteMax];
    int routeLen = 0;
    // lateral plan
    float lat = 0.f;            // current planned lateral offset from the path center (lane changes, nudges, pull over)
    int lcLane = -1;            // lane change target
    float lcFrom = 0.f, lcTo = 0.f, lcU0 = 0.f, lcLen = 30.f;
    float nudge = 0.f, nudgeTarget = 0.f, nudgeTimer = 0.f;
    float lcCooldown = 0.f;
    // longitudinal plan
    float vTarget = 0.f;        // speed setpoint from the planner
    float vDummy = 0.f;         // kinematic speed (dummy mode)
    float stopDist = 1e9f;      // distance (front bumper) to the binding stop point
    float obstDist = 1e9f, obstSpeed = 0.f;
    int obstBody = -1;
    bool obstBacking = false;   // the vehicle ahead is backing up towards us (close): brake and lean on the horn
    bool sweepCar = false;      // long vehicle: a stopped car in the space its body is about to sweep through
    float sweepGap = 1e9f;      // ... and how far ahead of the front bumper it is
    float sweepHold = 0.f;      // time spent stopped short of it (lagging a turn) - after a while the crawl resumes
    bool kturnSweep = false;    // the three-point turn under way is one to bring the nose round past such a car
    float headFirst = 0.f;      // after it: steer for the heading first, the lateral error second (s left)
    float frontErr = 0.f, headErr = 0.f;   // front-axle tracking: lateral error (+ right of the path), heading error (rad)
    float curveSpeed = 99.f;
    float integ = 0.f;          // speed controller integral
    float planTimer = 0.f;
    // intersections
    int gateConn = -1;          // connector being gated (next or current)
    int gateNode = -1;
    bool stopDone = false;      // all-way stop / right-on-red: came to a complete stop at the line
    bool committed = false;     // passed the point of no return for gateConn
    bool amberGo = false;
    int amberDecided = -1;      // connector for which the amber decision was taken
    float arrival = 0.f;        // time of arrival at an all-way stop
    float waitTime = 0.f;       // time spent waiting at a hold point
    bool redViolation = false;
    float enterTime = 0.f;      // time the current connector was entered (in-box tie breaks)
    // misc state
    float stuckTime = 0.f, blockedTime = 0.f, honkTimer = 0.f, hornHold = 0.f, recoverTimer = 0.f, offRouteTime = 0.f;
    float flipTime = 0.f, lostTime = 0.f, mutualTime = 0.f;
    int recoverDir = 0;
    vec2 stuckAnchor = vec2(1e9f);  // where the car stood when it last made progress while wanting to move
    float pedCreep = 0.f;       // inching forward at a pedestrian dawdling in front of the bumper (s left)
    int stuckRepeats = 0;       // stuck recoveries at the same spot in a row (the host may lift the car past, out of view)
    vec2 stuckAt = vec2(1e9f);  // where the last one happened (and on which path, before the recovery relocalized)
    int stuckPath = -1;
    float stuckU = 0.f;
    int kturns = 0;             // three-point-turn back-ups on the current path
    bool kturn = false;         // the running recovery is a three-point-turn back-up (keeps the path)
    float kturnT = 0.f;         // time running wide at full lock (three-point-turn trigger)
    vec2 threat;                // flee / pull-over reference
    float modeTimer = 0.f;
    vec2 dest;                  // DM_ROUTE / DM_EMERGENCY destination
    bool hasDest = false;
    std::vector<int> destEdges; // node-level route (edges) toward dest
    std::vector<int> destNodes;
    int destIndex = 0;
    float destRecalc = 0.f;
    float holdTimer = 0.f;      // DM_HOLD / DM_PULLOVER duration (-1 = until changed)
    float busStopCooldown = 0.f;
    int indicator = 0;
    float indicatorTimer = 0.f;
    // lane-center error statistics (harness)
    double statErr2 = 0.0;
    int statErrN = 0;
    float speedCap = 1e9f;      // this tick's cap (plan(): finding a gap to merge, a sweep hold; and the host's below)
    float hostCap = 1e9f;       // a cap the host imposes for hostCapT s (renewed while it holds: rubbernecking past a
    float hostCapT = 0.f;       // scene, traffic.cpp)
    int stopPath = -1;          // host-requested stop point (taxi pickup, scene arrival): path + u of the front bumper
    float stopU = 0.f;
    float lastDriveTime = -1.f; // time of the last drive() call (stale drivers are relocalized)
    float yieldHold = 0.f;      // stay put after backing off to let a blocked vehicle through (s)
    float diag[6] = {};         // controller diagnostics for the test harness (front error, heading error, feed-forward, command, path, u)
};

struct TrafficStats {
    long redViolations = 0, stopSignViolations = 0, stuckEvents = 0, recoveries = 0, relocalizations = 0, deadlockBreaks = 0;
    long kTurns = 0;
};

class TrafficCore {
public:
    const LaneGraph* g = nullptr;
    std::vector<Driver> drivers;      // indexed by host vehicle id
    std::vector<Body> bodies;         // filled by the host every tick (vehicles + pedestrians)
    BodyHash hash;
    double time = 0.0;
    u32 frame = 0;
    TrafficStats stats;
    int sirens = 0;                   // cars with the siren going this tick (crossings hold for them: gate)
    // per-node registry (rebuilt every tick): drivers in the box and approaching
    struct NodeEntry {
        int driver;
        int conn;
        float dist;     // distance of the front bumper to the connector entry (negative = inside, value = -s)
        float speed;
        float eta;
    };
    std::vector<std::vector<NodeEntry>> nodeReg;
    std::vector<int> touchedNodes;
    struct StopQueue {
        int node;
        std::vector<std::pair<int, float>> q;   // driver, arrival time
    };
    std::vector<StopQueue> stopQueues;
    std::vector<int> bodyOfDriver;    // driver slot -> body index (this tick)

    void init(const LaneGraph* graph) { g = graph; nodeReg.assign(graph->nodes.size(), {}); }
    Driver& attach(int vid, u32 uid, u32 seed, const VehicleInfo& info, int lane, float u, bool forceCautious = false);
    void detach(int vid);
    Driver* get(int vid) { return vid >= 0 && vid < (int)drivers.size() && drivers[vid].active ? &drivers[vid] : nullptr; }
    // Host calls once per AI tick after filling `bodies`: rebuilds the hash and the node registry.
    void beginTick(double t);
    // Computes controls for an AI vehicle (physics mode) or advances it kinematically (dummy mode).
    void drive(int vid, Vehicles::VehicleState& s, float dt, DriveOut& out);
    // Dummy conversion
    void toDummy(int vid, Vehicles::VehicleState& s);
    void toPhysics(int vid, Vehicles::VehicleState& s);
    // Relocalize onto the nearest compatible lane (after teleports, off-road excursions)
    bool relocalize(Driver& d, vec2 pos, vec2 heading, float maxDist = 25.f);
    // Route helpers
    void setDestination(Driver& d, vec2 dest);
    void clearRoute(Driver& d);
    // Spawning support: true if a vehicle of half length hl fits at (lane, u) with `gap` meters of free space
    bool laneFree(int lane, float u, float hl, float gap) const;
    bool zebraBusy(int link) const;   // somebody on a zebra crossing (walk link)
    bool rearClear(const Driver& d, vec2 pos, vec2 fwd, float dist) const;   // nothing within dist behind the rear bumper
    // `ahead` meters on from where the driver last got stuck (following its route): where to lift a hopelessly stuck car
    bool liftPoint(const Driver& d, float ahead, int& pathOut, float& uOut) const;
    // Signals and ped crossing support
    SignalState signalFor(const Driver& d) const;

    // internals
    void planRoute(Driver& d);
    int chooseConnector(Driver& d, int lane, bool fromCurrentLane);
    void localize(Driver& d, const Vehicles::VehicleState& s);
    void advancePath(Driver& d);
    void plan(Driver& d, const Vehicles::VehicleState& s, vec2 pos, vec2 fwd, float v, float dt);
    float gate(Driver& d, int conn, float distToEntry, float v, bool inBox);
    bool conflictsClear(const Driver& d, const Connector& c, int connId, float distToEntry, float v, bool permissiveOnly, float margin) const;
    bool stopGrant(Driver& d, int conn);
    void laneChangeLogic(Driver& d, float v, float distToEnd);
    bool gapOk(const Driver& d, int lane, float v, float extra) const;
    void control(Driver& d, const Vehicles::VehicleState& s, vec2 pos, vec2 fwd, float v, float dt, DriveOut& out);
    void dummyStep(Driver& d, Vehicles::VehicleState& s, float dt);
    int driverBody(int drv) const { return drv >= 0 && drv < (int)bodyOfDriver.size() ? bodyOfDriver[drv] : -1; }
    float maxSteerAt(const Driver& d, float v) const;
};

// =====================================================================================================================
// Pedestrian sidewalk navigation
// =====================================================================================================================
enum WalkerState : u8 { WS_WALK = 0, WS_WAIT_CROSS, WS_CROSSING, WS_IDLE, WS_OFFGRAPH };

struct Walker {
    int link = -1;
    bool fromA = true;          // walking from link.a to link.b
    float x = 0.f;              // meters along the link from the start end
    float lat = 0.f;            // lateral preference within the sidewalk (m, right of walking direction)
    float speed = 1.35f;
    u8 state = WS_WALK;
    float waitTimer = 0.f;
    float stuckTimer = 0.f;
    float lastProgress = 0.f;
    float flipCd = 0.f;         // no second about-turn this soon after one (no dithering in front of a waiting car)
    int prevNode = -1;
    bool hasDest = false;
    vec2 dest;
    bool jaywalker = false;
    bool avoidCrossing = false;
    u32 seed = 1;
    float hurry = 1.f;          // speed multiplier (crossing late, fleeing)
    vec2 lastDir = vec2(0, 1);
    float goT = -1.f;           // waited at a light and the little man lit: stepping off when this runs out (-1 none)
};

class PedCore {
public:
    const LaneGraph* g = nullptr;
    const TrafficCore* traffic = nullptr;   // bodies for traffic gaps and neighbor avoidance
    double time = 0.0;
    void init(const LaneGraph* graph, const TrafficCore* tc) { g = graph; traffic = tc; }
    // place a walker on the graph near p; returns false if no sidewalk nearby
    bool place(Walker& w, vec2 p, u32 seed, float maxDist = 60.f);
    // desired horizontal velocity for the walker at position pos; selfBody = the walker's body index (or -1)
    // (ignore: bodies the walker does not keep its distance from - the company walking beside it)
    vec2 step(Walker& w, vec2 pos, float dt, int selfBody, float* faceYaw = nullptr, const int* ignore = nullptr, int nIgnore = 0);
    // pick the next link at the end of the current one
    void chooseNext(Walker& w, int nodeId);
    bool crossingClear(const Walker& w, int link) const;
    vec2 target(const Walker& w, float ahead) const;
    int endNode(const Walker& w) const;
    float linkRemaining(const Walker& w) const;
};

// Utility used by both cores
float wrapPi(float a);
inline vec2 rightOf(vec2 t) { return vec2(t.y, -t.x); }             // right of a direction (x east, y north)
inline vec2 yawDir(float yaw) { return vec2(-sinf(yaw), cosf(yaw)); }   // game convention: 0 = +Y, CCW positive
inline float dirYaw(vec2 d) { return atan2f(-d.x, d.y); }

}  // namespace AI
