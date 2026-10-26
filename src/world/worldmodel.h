// The world model (World::Hand): the hand-built world's .ntw files read into typed records (docs/NTW_FORMAT.md,
// docs/HANDBUILT_WORLD.md section 4). One model serves the host tools: worldc compiles world.bin from it, and worldv
// checks it. Each record keeps the file and line it came from, so every finding and every compiled road leads back to
// its text. The game never reads it: it loads the compiled world.bin.
//
// v0 is the loader and the roads compile, with the signal plans, transit and places typed. Blocks, buildings and the
// library items are typed as far as their headers, their parcels and lots, and the records that place things; their
// world-frame geometry (footprints, entrances, colliders) comes in v1, behind the same Model.
//
// Host tools only. Link: ntw.cpp, ntwschema.cpp and worldmodel.cpp for the loader; compileRoads() adds
// worldmodel_roads.cpp, roadnet.cpp (or roads.cpp, which includes it) and src/core/math.cpp; compileBlocks()
// worldmodel_blocks.cpp and blocksbin.cpp.
#pragma once
#include "ntw.h"
#include "roads.h"
#include "roadoverrides.h"
#include "blocksbin.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace World {
namespace Hand {

// ---------------------------------------------------------------------------------------------------- where things are

// A place in the text: the file (an index into Model::files) and the line. file -1: none (made by the compiler, or the
// world directory as a whole); line 0: the file as a whole.
struct Src {
    int file = -1;
    int line = 0;
};

struct File {
    std::string path;                  // as found: world/districts/cl/roads.ntw
    std::string kind;                  // the header's kind
    Ntw::Document doc;                 // the parsed text, kept for the checks that read fields the model doesn't type
    bool valid = false;                // parsed, and clean against the schema: only valid files are typed
};

// A reference as written, and what it names once resolved. The schema's reference table (ntwschema.h schemaRefs) says
// which fields name records and what they may name; load() resolves them by the same rules as worldv's ref checks: the
// id defined first in the world, if its class is one of `classes`.
struct Ref {
    std::string text;                  // the id it names
    std::string classes;               // what it may name, from the schema: "node", "parcel|lot"
    std::string cls;                   // what it names, once resolved; empty: unresolved
    int target = -1;                   // the record's index in that class's table (Model::find); -1: unresolved
    Src src;                           // the field's line
};

// A distance along an edge from its `from` node; `toEnd` for to=end
struct EdgeAt {
    float s = 0.f;
    bool toEnd = false;
};

enum Side : u8 { SIDE_LEFT = 0, SIDE_RIGHT, SIDE_BOTH };
enum Dir : u8 { DIR_FWD = 0, DIR_BWD };

// ------------------------------------------------------------------------------------------------- world and districts

struct Zone {
    std::string id;
    Src src;
    std::string kind;                  // district, use, turf, density, audio, habitat, police, map
    std::vector<vec2> poly;
    std::vector<std::string> tags;
    std::vector<float> peds, traffic;  // density zones: 24 factors by hour (empty: none given)
    std::vector<std::string> days;     // mon ... sun (empty: every day)
};

struct District {
    std::string id;
    Src src;                           // its record in world.ntw
    std::string name;
    vec2 label = vec2(0.f);            // where the map writes its name (the district file's label at= when it has one)
    std::string labelText;             // the label's text, if not the name
    int order = 0;
    int file = -1;                     // its district.ntw (-1: none)
    std::vector<vec2> bounds;
    std::vector<Zone> zones;
    std::vector<Ref> trunkNodes;       // the trunk nodes its roads may join
};

// ------------------------------------------------------------------------------------------------------------- roads

struct Node {
    std::string id;
    Src src;
    int file = -1;                     // its roads.ntw
    vec2 at = vec2(0.f);
    float z = 0.f;                     // absolute
    enum Control : u8 { CTL_AUTO = 0, CTL_NONE, CTL_STOP, CTL_YIELD, CTL_SIGNAL } control = CTL_AUTO;   // unwritten: AUTO
    Ref plan;                          // a signal plan; empty text: auto (derived)
    float offset = 0.f;                // s into the plan's cycle
    std::vector<std::pair<std::string, Ref>> approaches;   // compass letter -> edge
    bool stopsAll = false;
    std::vector<Ref> stops, yields;    // the approaches that stop / give way
    std::vector<Ref> noRedTurn;        // the approaches where turning right on red isn't allowed
    float bulb = 0.f;                  // a dead end's turning-circle radius (0: none written)
    std::vector<int> edges;            // the edges at it (Model::edges, in their order)
    int junction = -1;                 // its junction record (Model::junctions), -1: none
};

// The records inside an edge's block, typed (NTW_FORMAT 9.4)
struct LaneCount {                     // lanes: one direction's count over a stretch
    Src src;
    EdgeAt from, to;
    Dir dir = DIR_FWD;
    int count = 0;
    int add = -1, drop = -1;           // the Side that gains or loses the lane, -1: none
    float taper = 0.f;
};
struct LaneUse {                       // lane: a lane kept for one use
    Src src;
    Dir dir = DIR_FWD;
    int index = -1;                    // counted from the left of its direction, from 0; -1: the outermost on `side`
    Side side = SIDE_LEFT;             // with index -1: index=left or index=right
    std::string use;                   // bus, streetcar, bike, general
    EdgeAt from, to;
};
struct NoSwitch {
    Src src;
    Dir dir = DIR_FWD;
    EdgeAt from, to;
};
struct Moves {
    Src src;
    int atEnd = 1;                     // the end the lanes arrive at: 0 n0 (the bwd lanes), 1 n1 (the fwd lanes)
    std::vector<std::string> lanes;    // per lane from the left: "l", "s", "sr"
};
struct Crossing {
    Src src;
    float at = 0.f;
    std::string kind;                  // signal, zebra, plain, unmarked
    float w = 0.f;                     // 0: derived
};
struct BusStop {                       // ids are world-wide: transit stops refer to them
    std::string id;
    Src src;
    int edge = -1;                     // index in Model::edges
    float at = 0.f;
    Side side = SIDE_RIGHT;
    std::string kind;                  // bus, streetcar
    Ref shelter;
    std::vector<vec2> wait;            // the waiting area (empty: derived)
    float waitRadius = 0.f;            // x,y:r: one point and a radius
    std::vector<Ref> routes;
    float clear = 0.f;
};
struct KerbStretch {                   // parkbay, nostop, loading, taxirank, nojaywalk: a stretch of one kerb
    Src src;
    std::string type;                  // the record type
    float from = 0.f, to = 0.f;
    Side side = SIDE_RIGHT;            // nojaywalk without side=: both
    std::string kind;                  // parkbay: parallel, angle, perp; nostop: its why= (empty: none given)
    int meter = 0, disabled = 0, cap = 0;
    std::string hours;                 // loading (empty: always)
};
struct CurbCut {
    Src src;
    float at = 0.f, w = 0.f;
    Side side = SIDE_RIGHT;
    Ref to;                            // the parcel or lot it serves
};
struct Deck {
    Src src;
    float from = 0.f, to = 0.f;
    std::string kind;                  // bridge, viaduct, overpass
};
struct Sign {
    Src src;
    float at = 0.f;
    Side side = SIDE_RIGHT;
    std::string kind, value;
};

struct Edge {
    std::string id;
    Src src;
    int file = -1;
    Ref from, to;                      // nodes
    RoadClass cls = RC_STREET;
    std::string name;
    int lanesF = 1, lanesB = 1;        // lanes=F/B
    float laneWidth = 0.f;             // 0: the class's
    float median = -1.f;               // -1: the class's
    bool medianBarrier = false;
    // shoulder=, parking=, sidewalk=: none, left:W, right:W or both:W set both sides (the side not named: 0); -1 when
    // the field isn't written: the class's shoulder (unless parking= is written) and sidewalk
    float shoulderL = -1.f, shoulderR = -1.f;
    float parkingL = -1.f, parkingR = -1.f;
    float sidewalkL = -1.f, sidewalkR = -1.f;
    float speed = 0.f;                 // m/s; 0: the class's
    u8 flags = 0;                      // RF_BRIDGE, RF_ELEVATED, RF_UNPAVED as written
    bool tunnel = false;
    std::vector<vec2> shape;           // the centreline, from the `from` node's point to the `to` node's
    std::vector<vec2> profile;         // (s, z) pairs, absolute z; empty: straight from node to node
    std::vector<LaneCount> laneCounts;
    std::vector<LaneUse> laneUses;
    std::vector<NoSwitch> noSwitch;
    std::vector<Moves> moves;
    std::vector<Crossing> crossings;
    std::vector<int> busStops;         // indices in Model::busStops
    std::vector<KerbStretch> kerb;
    std::vector<CurbCut> curbCuts;
    std::vector<Deck> decks;
    std::vector<Sign> signs;
};

struct Junction {                      // its id is its node's
    Src src;
    Ref node;
    std::string kind;                  // plain, roundabout
    std::string turns;                 // all, no_uturn, custom
    std::vector<vec2> box;             // empty: derived from the cut-backs
    struct StopLine { Src src; Ref edge; float at = 0.f; };
    struct Connect { Src src; Ref fromEdge, toEdge; int fromLane = 0, toLane = 0; std::string turn; bool isProtected = false; };
    struct Ban { Src src; Ref fromEdge, toEdge; };
    struct Head { Src src; vec3 at = vec3(0.f); Ref edge; Dir dir = DIR_FWD; };
    std::vector<StopLine> stopLines;
    std::vector<Connect> connects;
    std::vector<Ban> bans;
    std::vector<Head> heads;
};

// ------------------------------------------------------------------------------------------------------- signal plans

struct Phase {
    std::string id;
    Src src;
    float green = 0.f, amber = 0.f, allred = 0.f;
    std::vector<std::string> moves;    // <approach>_<turn>[p]: n_s, e_lp
    std::vector<std::string> walk;     // crossings by the leg they cross: e
    float walkTime = 0.f, flash = 0.f;
};

struct SignalPlan {
    std::string id;                    // lib.signal.<name>
    Src src;
    float cycle = 0.f;
    int legs = 0;                      // 0: not given
    std::vector<Phase> phases;
};

// ---------------------------------------------------------------------------------------------- transit and places

struct TransitLine {
    std::string id;                    // the header's line=
    Src src;
    std::string kind;                  // skyline, bus, ferry, streetcar
    std::string name;
    std::vector<Ref> routeEdges;       // bus, streetcar
    std::vector<vec2> routePts;        // skyline, ferry
    std::vector<vec2> routeProfile;    // (s, z)
    struct Station { std::string id; Src src; vec2 at = vec2(0.f); float z = 0.f; std::string name, side; };
    struct Stop { std::string id; Src src; Ref busStop; };
    struct Pier { std::string id; Src src; vec2 at = vec2(0.f); float yaw = 0.f; std::string name; };
    struct Pole { Src src; vec2 at = vec2(0.f); float h = 0.f; };
    std::vector<Station> stations;
    std::vector<Stop> stops;
    std::vector<Pier> piers;
    std::vector<Pole> poles;
    float headway = 0.f, first = 0.f, last = 0.f;
};

struct Place {
    std::string id;
    Src src;
    Ref building;
    std::string door;                  // <floor>.<door> of the building's plan
    vec2 walk = vec2(0.f);
    vec3 curb = vec3(0.f);
    vec2 dir = vec2(0.f);
    std::vector<vec2> water;
    std::string note;
};

struct Anchor {
    std::string id;
    Src src;
    std::string kind, event;
    vec2 at = vec2(0.f);
    float z = 0.f, yaw = 0.f;
    std::vector<vec2> poly, line;
    Ref building, edge;
    std::string door;
    int cap = 0;
    std::vector<std::string> tags;
};

// ------------------------------------------------------------- blocks, buildings and the library (typed further in v1)

// A parcel's records: its ground's paint, its pads and its fences
struct GroundPaint {
    Src src;
    std::string kind;                  // lawn, paving, gravel, dirt, sand, planting
    std::vector<vec2> poly;
};

struct Pad {                           // a level slab
    Src src;
    std::vector<vec2> poly;
    float z = 0.f;                     // above the parcel's grade; without one, above the terrain under its centre
    std::string kind;                  // paving, concrete, deck
};

struct Fence {
    Src src;
    Ref kind;                          // lib.fence.*
    std::vector<vec2> line;
    std::vector<vec2> gates;           // (distance along the line, width)
};

struct Parcel {
    std::string id;                    // cl.b014.p03
    Src src;
    int block = -1;                    // index in Model::blocks
    std::vector<vec2> poly;
    std::string use;
    Ref front;                         // front=<edge>:<side>: the street it faces
    Side frontSide = SIDE_RIGHT;
    bool graded = false;               // grade= written: the ground is levelled to it, blending out over `blend` m
    float grade = 0.f, blend = 0.f;
    std::string ground;                // its ground (lawn, paving, ...); empty: the terrain's own
    std::vector<GroundPaint> paints;
    std::vector<Pad> pads;
    std::vector<Fence> fences;
};

struct Plaza {                         // an open paved area
    std::string id;                    // cl.b014.pz1 (optional)
    Src src;
    int block = -1;
    std::vector<vec2> poly;
    std::string ground;                // empty: paving
    std::vector<std::string> tags;
};

// The dressing of a block: its trees, plants, props and lights. They have no ids: nothing refers to one.
struct Dress {
    enum Kind : u8 { DRESS_TREE, DRESS_PLANT, DRESS_PROP, DRESS_LIGHT };
    Kind what = DRESS_PROP;
    Src src;
    int block = -1;
    Ref kind;                          // lib.veg.* (trees and plants), lib.prop.*, lib.light.*
    vec2 at = vec2(0.f);
    bool hasZ = false;                 // a prop's z=, a light's at=x,y,z: the height above the graded ground (0 without)
    float z = 0.f;
    bool hasYaw = false;
    float yaw = 0.f;                   // degrees, as written
    float h = 0.f, canopy = 0.f;       // trees: height and canopy width; 0: the piece's own
    float scale = 1.f;                 // plants
    int look = 0;                      // props: the piece's mesh variant
    std::string sched;                 // lights: dusk, evening, night, always, never (empty: dusk)
};

struct Lot {
    std::string id;                    // cl.b014.lot1
    Src src;
    int block = -1;
    int levels = 1;
};

struct Block {
    std::string id;                    // cl.b014
    Src src;
    int file = -1;
    Ref district;
    std::vector<int> parcels, lots;    // indices in Model::parcels and Model::lots, in file order
    std::vector<int> plazas, dressing; // indices in Model::plazas and Model::dressing, in file order
};

struct Building {
    std::string id;                    // cl.b014.h03
    Src src;
    int file = -1;
    Ref block, parcel;
    Ref plan;                          // place plan=
    vec2 at = vec2(0.f);
    float z = 0.f, yaw = 0.f;          // z absolute: the ground floor's height
    bool mirror = false;
};

// A library item: plan, layout, unit, fitout, set, facade, a palette, a catalog piece. v0 keeps its records in
// File::doc; `file` and `record` lead there.
struct LibItem {
    std::string id;
    std::string kind;                  // its class: the file kind, or a catalog piece's category (kit, prop, veg,
                                       // light, fence; piece when its id names none)
    Src src;
    int file = -1;
    int record = -1;                   // the record in the file (palettes and catalog pieces); -1: the whole file
    // catalog pieces: what builds them, as the catalog writes it
    std::string code;                  // the C++ enum (PROP_PALM) or function
    vec3 size = vec3(0.f);             // size=WxDxH; 0: unknown
    int variants = 0;
    std::string collide;               // box, cylinder, none
};

// ------------------------------------------------------------------------------------------------------------ the model

// A finding of the loader or a compiler. `code` is short and stable, for worldv to map onto its checks:
// - file.read, file.parse, file.schema: load() only (loadParsed takes the documents parsed and checked);
// - ref.form: a field that names records doesn't have the shape the schema's reference table gives it;
// - value.form: a compound value doesn't have its shape (lanes=F/B, median=W[:barrier], shoulder=both:W,
//   profile=s:z;..., wait=x,y:r, peds= 24 numbers, ...);
// - road.node, road.shape, road.profile, road.loop: compileRoads (below).
// References that name nothing are not findings here: their Ref stays unresolved, and worldv reports them.
struct Error {
    Src src;
    std::string code;
    std::string message;
};

// What an id names: its class and its index in that class's table
struct Def {
    std::string cls;
    int index = -1;
};

// Read-only once loaded: the const lookups may run on several threads at once.
struct Model {
    std::vector<File> files;           // in discovery order (sorted paths); Src::file indexes it
    // the tables: files in discovery order, records in file order
    std::vector<District> districts;   // world.ntw's order
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Junction> junctions;
    std::vector<BusStop> busStops;
    std::vector<SignalPlan> signals;
    std::vector<TransitLine> transit;
    std::vector<Place> places;
    std::vector<Anchor> anchors;
    std::vector<Block> blocks;
    std::vector<Parcel> parcels;
    std::vector<Plaza> plazas;
    std::vector<Dress> dressing;
    std::vector<Lot> lots;
    std::vector<Building> buildings;
    std::vector<LibItem> library;

    // Every world-wide id the model types, defined first where it is defined twice (worldv reports the second): the
    // classes of the schema's reference table (ntwschema.h), with place and anchor
    std::unordered_map<std::string, Def> ids;

    // The index of `id` in the table of class `cls` (district, node, edge, busstop, block, parcel, lot, building, line,
    // signal, place, anchor; plan, layout, unit, fitout, set, facade, palette and the catalog's categories in
    // `library`), or -1
    int find(const char* cls, const std::string& id) const;
    std::string where(const Src& s) const;   // world/districts/cl/roads.ntw:12; the path alone for line 0; "(no file)"
};

// Every .ntw file under `dir` as worldc finds them: recursively, regular files only, the paths as found ('/' between
// parts) and sorted. A directory that can't be listed (or part of it) is an Error file.read with no file.
std::vector<std::string> discover(const std::string& dir, std::vector<Error>& errors);

// Types the valid documents' records, indexes the ids and resolves the references; the files keep their order, so the
// caller's file indices are the model's Src::file. False when it reported anything.
bool loadParsed(std::vector<File>&& files, Model& out, std::vector<Error>& errors);

// discover(), then each file read, parsed and checked against the schema (file.read, file.parse, file.schema), then
// loadParsed(). A file with errors keeps its place, untyped. False when anything was wrong.
bool load(const std::string& dir, Model& out, std::vector<Error>& errors);

// ------------------------------------------------------------------------------------------------------- roads compile

// What the compiled RoadNetwork's indices came from
struct RoadIndex {
    std::vector<int> nodeOf;           // road node index -> Model::nodes index (every model node is one road node)
    std::vector<int> edgeOf;           // road edge index -> Model::edges index
    std::vector<int> roadNode;         // Model::nodes index -> road node index
    std::vector<int> roadEdge;         // Model::edges index -> road edge index; -1: an edge that didn't compile
};

// Compiles the model's roads to today's RoadNetwork (src/world/roads.h), for LaneGraph::build (src/game/lanes.cpp, as
// tools/traffic_sim.cpp calls it) and later for worldc's world.bin:
// - a road node per node, a road edge per edge, in the model's order;
// - the centreline is the shape; its heights come from the profile, else straight from node to node (heights are
//   absolute, so the compile reads no terrain), and every edge meets its nodes at their z;
// - widths: lanes from lanes=F/B, the lane width, median, shoulder or parking and sidewalk as written, else the class's
//   (RoadNetwork's widths are the same both sides: the wider side counts); flags as written, RF_BRIDGE where the edge
//   has a deck, RF_ONEWAY where a direction has no lanes, RF_UNPAVED for dirt, RF_NOSIDEWALK without one;
// - each edge's seed hashes its id, so adding an edge somewhere changes no other edge's;
// - junction radii, cut-backs and control as generate() derives them (RoadNetwork::finishJunctions), then a node's
//   control= where written: none and yield 0 (priority by road class), stop 1 (stop signs) where every approach stops,
//   else 0, signal 2; the spatial hash.
// The authored overrides (lane counts and uses, moves, kerb stretches, junction records, plans, the yield and stop
// lists, bulbs, speeds, the per-side widths) stay on the Model's edges, nodes and junctions, reached through `index`;
// compileRoadOverrides gives the lane graph those it reads so far.
// Errors:
// - road.node: the edge's from= or to= names no node (worldv's ref.missing);
// - road.loop: from= and to= are one node;
// - road.shape: fewer than 2 points, two points in one place, or ends that miss their nodes by more than 2 cm;
// - road.profile: its s doesn't rise from 0 to the shape's length (within 0.5 m), or its ends are more than 5 cm off
//   their nodes' z.
// An edge with an error is left out, and the rest compiles. False when it reported anything.
bool compileRoads(const Model& model, RoadNetwork& out, RoadIndex& index, std::vector<Error>& errors);

// What the compiled roads author beyond the network, for the lane graph (roadoverrides.h), one list per step as it
// grows: so far each edge's EdgeExtra (its widths and speed as written, else its class's) and each node's NodeRule
// (control, stops, yields, no-red-turn approaches and bulb, as written). Reads what compileRoads compiled (`net` and
// `index`); references that name nothing are left out (worldv reports them); reports nothing itself.
void compileRoadOverrides(const Model& model, const RoadIndex& index, const RoadNetwork& net, RoadOverrides& out);

// ------------------------------------------------------------------------------------------ legs and signal plans

// A node's legs as the lane graph sees them (src/game/lanes.cpp), one implementation for worldc and worldv: each road
// edge at the node, in the road node's edge order, with
// - its direction from the node to the point max(cut-back, 4 m) along it (at most half its length), in its own s;
// - its bearing, degrees clockwise from north (y), and compass sector: n [315, 45), e [45, 135), s [135, 225),
//   w [225, 315), a bearing on a boundary going to the clockwise sector;
// - its letter: the one approaches= gives it (a letter's first use and an edge's; a letter other than n, e, s or w is
//   skipped), else its sector.
struct NodeLeg {
    int road = -1;                     // RoadNetwork edge
    int edge = -1;                     // Model edge
    bool outgoing = false;             // the edge starts at the node: its fwd lanes leave, its bwd lanes arrive
    int in = 0, out = 0;               // the network's lanes arriving at the node along it, and leaving
    vec2 dir = vec2(0.f);              // unit, from the node out along the edge
    float bearing = 0.f;
    char compass = 'n', letter = 'n';
};
struct NodeLegs {
    int node = -1;                     // Model node
    std::vector<NodeLeg> legs;
    bool interchange = false;          // highway and ramp legs meet: only movements along the carriageway
    bool gradeSeparated = false;       // 4 legs or more, all highway: no turns
};
NodeLegs nodeLegs(const Model& model, const RoadIndex& index, const RoadNetwork& net, int node);

// The turn from leg a (arriving) into leg b (leaving), as the lane graph classes it: TURN_LEFT, TURN_STRAIGHT or
// TURN_RIGHT past 35 degrees either way, or 0 where it makes no movement: back round through more than 140 degrees,
// across a motorway's carriageways, or a turn at a grade-separated node. At a node of 2 legs every movement is straight.
// A junction's connect record joining the two edges gives its turn instead (TURN_UTURN for uturn).
u8 legTurn(const Model& model, const NodeLegs& legs, const NodeLeg& a, const NodeLeg& b);

// A signal plan resolved at a node: each phase's movements, `<letter>_<turn>[p]`, as every movement from the leg with
// that letter (lanes arriving) into a leg with lanes leaving whose turn (legTurn) is the one written; and its walks as
// the legs they cross. What names no leg there (a 4-leg plan's west at a T) goes to `unused`.
struct ResolvedPlan {
    struct Move {
        int from = -1, to = -1;            // RoadNetwork edges: arriving along `from`, leaving along `to`
        u8 turn = 0;                       // RoadTurn
        bool isProtected = false;
    };
    struct Phase {
        std::vector<Move> moves;           // sorted by from, then to
        std::vector<int> walks;            // the RoadNetwork edges crossed, sorted
    };
    std::vector<Phase> phases;             // the plan's, in order
    std::vector<std::string> unused;       // "w_s", "walk w": as written, in the plan's order
};
void resolvePlan(const Model& model, const NodeLegs& legs, const SignalPlan& plan, ResolvedPlan& out);

// ------------------------------------------------------------------------------------------------------ blocks compile

struct BlockPad {                      // a level the terrain is graded to: a parcel's grade=
    std::vector<vec2> poly;
    float z = 0.f;                     // absolute
    float blend = 0.f;                 // m beyond the polygon back to the terrain
};

// What compileBlocks makes, for worldc. Heights in `blocks` are still above the ground wherever its records stand on
// it: worldc grades the terrain (the pads, then the roads), then adds the ground's height (or a sidewalk's, where one
// runs) to every prop, at its position, and to every lamp, at its fixture's (lampGroundAt), and to the pads that
// stand on the terrain (groundOnTerrain: at their centroid).
struct BlocksOut {
    HandBlocks blocks;
    std::vector<BlockPad> pads;
    std::vector<vec2> lampGroundAt;    // per lamp
    std::vector<u8> groundOnTerrain;   // per ground polygon: 1 for a level one whose z is above the terrain
};

// Compiles the blocks' parcels, plazas and dressing into the game's blocks (blocksbin.h):
// - the ground: each parcel's ground= over its polygon, then its paint over that, level on its grade= when it has one
//   (a pad for the grading, blending out over blend= m), else on the terrain; its pads as level polygons, z above the
//   graded ground; each plaza (paving without ground=);
// - the dressing: a prop for each tree, plant and prop, of its piece's code= (a PropType's name: catalogcodes.h),
//   trees scaled to h= by the piece's size= height, plants by scale=; trees and plants without yaw= turn, and vary
//   among the piece's variants=, by their position; each light its fixture's prop and lamp (catalogcodes.h
//   lampSpec), lit at dusk unless sched= says evening (the porch timer), always or never;
// - yaw turned from the catalog's frame (+y forward) to the game's (catalogcodes.h propYawOffset).
// Findings: block.shape (a polygon with fewer than 3 points), block.kind (a code= the game doesn't build, a ground
// kind it doesn't know), block.size (a tree scaled below 0.2 or above 5 of its piece). Records naming nothing are
// worldv's to report, and left out here. False when it reported anything.
bool compileBlocks(const Model& model, BlocksOut& out, std::vector<Error>& errors);

// The baked terrain as worldc reads it from world/terrain: the heights and water the roads and parcels are graded into
struct BakedTerrain {
    bool ok = false;
    int res = 0;
    float cell = 0.f, originX = 0.f, originY = 0.f, heightUnit = 0.f;
    std::vector<i16> height, water;    // the grids' values
    std::vector<float> levels;         // water class c's level is levels[c - 1]
};

// The terrain graded under the parcels and the roads (docs/HANDBUILT_WORLD.md section 3.3), as worldc writes it: the
// parcels' grades first, then today's road grading (src/world/roadgrade.cpp), so the roads' cut still wins at the kerb;
// stored as a delta on the bake in the height layer's unit, an i16 grid like the height layer ("terrain.grade"). The
// game adds it to the baked heights at load, and computes the splat from the result. Then the blocks go on the graded
// ground: every prop and lamp at the ground's height, or a sidewalk's or road's where one runs (within 3 m above it),
// and the level polygons that stand on the terrain; and into the blocks section. `report` gets worldc's line on the
// grading. With no roads and nothing in the blocks (no ground, dressing, lamps or graded parcels) there is nothing to
// do: true, nothing written. False, with a reason, when the baked terrain isn't the game's grid or the grading doesn't
// encode.
bool placeOnTerrain(const BakedTerrain& baked, const RoadNetwork& roads, const std::vector<std::vector<vec2>>& decks, BlocksOut& blocks,
                    WorldBinWriter& out, std::string* error, std::string* report);

}  // namespace Hand
}  // namespace World
