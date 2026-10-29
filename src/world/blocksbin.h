// The hand-built world's blocks as the game uses them (docs/HANDBUILT_WORLD.md section 4.5): the ground's paint and
// pads, and the dressing (trees, plants, street furniture and lights). tools/worldc.cpp compiles them from the block
// files into world.bin's "blocks" section (worldmodel.h compileBlocks), with every height already on the graded terrain;
// the game reads them back in hand mode (handworld.cpp), generateCell draws each cell's share (cellgen.cpp), and
// RoadNetwork::surfaceHeight stands people and vehicles on the level ground (padHeight).
//
// Section "blocks" (WSK_LZ), little-endian:
//   version u32 (kBlocksSectionVersion)
//   ground count u32, then per polygon: kind | flat << 8 (u32), z (f32), point count (u32), then x, y per point (f32)
//   prop count u32, then per prop: type | variant << 8 | flags << 16 (u32), x, y, z, yaw, scale (f32)
//   lamp count u32, then per lamp: type | sched << 8 (u32), x, y, z, r, g, b (colour times intensity), radius,
//     dx, dy, dz (its direction, 0 for a point light), cone (cos of the outer angle) (f32)
//
// Beside it, what the AI's graphs read (docs/HANDBUILT_WORLD.md 12.2-12.4), with the road overrides' conventions: edges
// and nodes are the RoadNetwork's, a distance along an edge is the RoadEdge's own s, sides are seen looking fwd (n0 to
// n1, 0 left, 1 right), positions are world xyz with z on the graded ground, and every list is in block-file order.
// Section "blocks.parcels" (WSK_LZ):
//   version u32 (kBlockParcelsVersion), count u32, then per parcel: id (string), use | frontSide << 8 (u32), frontEdge
//   (u32; 0xffffffff: none), grade (f32; NaN where it follows the terrain), its polygon (points)
// Section "blocks.lots" (WSK_LZ): version u32 (kBlockLotsVersion), count u32, then per lot:
//   id (string), levels (u32), entry and exit, each edge (u32; 0xffffffff: none, the exit where the entry serves both),
//   s (f32), side (u32);
//   rows: count u32, then per row: a, b (xyz: its bays' centreline, first bay to last), bays (u32), w, len (f32), dir
//     (xy: the way a car parked nose-in faces), level (u32), aisle (u32: its link);
//   nodes: count u32, then per node: x, y, z (f32), level | kind << 8 (u32);
//   links: count u32, then per link: a, b (u32: nodes), width (f32), oneway | kind << 8 (u32), then its points (u32
//     count, then xyz), from a to b;
//   bays: count u32, then per bay: x, y, z (its centre), dx, dy (f32), row, link (u32), s (f32: plan metres along its
//     link from its a)
// Section "blocks.walk" (WSK_LZ): version u32 (kBlockWalkVersion), then
//   paths: count u32, then per path: id (string), kind (u32), w (f32), its points (u32 count, then xyz);
//   plazas: count u32, then per plaza: id (string; empty without one), z (f32; NaN where it lies on the ground), its
//     polygon (points), tags (u32 count, then strings);
//   steps: count u32, then per flight: bottom, top (xyz), w (f32), treads (u32), rise (f32)
// Section "blocks.spots" (WSK_LZ): version u32 (kBlockSpotsVersion), count u32, then per spot: kind | flags << 8 (u32),
//   id (string), x, y, z, dx, dy (f32: where, and the way the one using it faces; 0, 0: any way), its queue's points
//   (u32 count, then xyz, its head first), cap (u32), hours (u32 count of ranges, then from, to per range: u32 minutes
//   of the day; none: always), roles (u32 count, then strings), ages min | max << 8 (u32), prop (u32: an index into
//   "blocks"' props; 0xffffffff: none), pose, for (strings)
// Section "blocks.colliders" (WSK_LZ): version u32 (kBlockCollidersVersion), count u32, then per collider: kind (u32:
//   0 box, 1 cylinder), prop (u32: its index in "blocks"' props), x, y (its centre), z0, z1 (its bottom and top), hx,
//   hy (a box's half width and half depth; a cylinder's radius, twice), ax, ay (a box's width axis, unit) (f32)
#pragma once
#include "worldtypes.h"
#include "worldbin.h"
#include <limits>
#include <string>
#include <vector>

namespace World {

constexpr u32 kBlocksSectionVersion = 1;
constexpr u32 kBlockParcelsVersion = 1;
constexpr u32 kBlockLotsVersion = 1;
constexpr u32 kBlockWalkVersion = 1;
constexpr u32 kBlockSpotsVersion = 1;
constexpr u32 kBlockCollidersVersion = 1;

// The ground kinds of parcels, paint, plazas and pads
enum GroundKind : u8 { GK_LAWN = 0, GK_PAVING, GK_GRAVEL, GK_DIRT, GK_SAND, GK_PLANTING, GK_CONCRETE, GK_DECK, GK_COUNT };
GroundKind groundKind(const std::string& word, bool* known = nullptr);   // lawn ... deck; GK_LAWN with *known false

struct GroundPoly {
    u8 kind = GK_LAWN;
    u8 flat = 0;                       // 1: level at z (a pad); 0: on the graded terrain, z above it
    float z = 0.f;
    std::vector<vec2> poly;            // a simple polygon, either winding
};

// A parcel as block files author it (docs/NTW_FORMAT.md 9.5): frontage, curb cuts and later the buildings name it by
// its index
enum ParcelUse : u8 { PU_RESIDENTIAL = 0, PU_COMMERCIAL, PU_MIXED, PU_INDUSTRIAL, PU_CIVIC, PU_PARK, PU_PARKING, PU_VACANT, PU_COUNT };
ParcelUse parcelUse(const std::string& word, bool* known = nullptr);   // residential ... vacant; PU_VACANT with *known false
struct HandParcel {
    std::string id;                    // cl.b014.p03
    u8 use = PU_VACANT;
    int frontEdge = -1;                // the RoadNetwork edge it faces (front=); -1: none
    u8 frontSide = 1;                  // which side of it: 0 its left, 1 its right, looking fwd
    float grade = std::numeric_limits<float>::quiet_NaN();   // a levelled parcel's ground (absolute z); NaN where it
                                                             // follows the terrain
    std::vector<vec2> poly;
};

// A car park (a block file's lot) with its drive graph: worldc derives the aisles from the rows (an authored aisle=
// overrides a row's), merges the aisles of rows that face each other, and joins the aisle ends to the entry, the exit
// and the ramps, so the graph is connected; each bay opens onto a link at an s along it.
struct HandLot {
    struct Cut {                       // a curb cut on a street
        int edge = -1;                 // RoadNetwork edge; -1: none
        float s = 0.f;                 // its own s
        u8 side = 1;                   // 0 left, 1 right, looking fwd
    };
    enum : u8 { LN_AISLE = 0, LN_ENTRY, LN_EXIT, LN_RAMP };   // Node::kind
    enum : u8 { LL_AISLE = 0, LL_DRIVE, LL_RAMP };            // Link::kind: an aisle, a drive (to the entry or exit,
                                                               // or between aisles), a ramp between levels
    struct Node {
        vec3 p = vec3(0.f);
        int level = 0;
        u8 kind = LN_AISLE;
    };
    struct Link {
        int a = -1, b = -1;            // nodes
        float width = 0.f;
        u8 oneway = 0;                 // 1: from a to b only
        u8 kind = LL_AISLE;
        std::vector<vec3> pts;         // from a to b
    };
    struct Row {
        vec3 a = vec3(0.f), b = vec3(0.f);   // its bays' centreline, from the first bay's outer edge to the last's
        int bays = 0;
        float w = 0.f, len = 0.f;      // a bay's width and length
        vec2 dir = vec2(0.f);          // the way a car parked nose-in faces (unit)
        int level = 0;
        int aisle = -1;                // the link its bays open onto
    };
    struct Bay {
        vec3 c = vec3(0.f);            // the bay's centre
        vec2 dir = vec2(0.f);          // as its row's
        int row = -1, link = -1;
        float s = 0.f;                 // where it opens onto the link: plan metres along its points from a
    };
    std::string id;                    // cl.b014.lot1
    int levels = 1;
    Cut entry, exit;                   // exit.edge -1: the entry serves both ways
    std::vector<Row> rows;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Bay> bays;
};
constexpr float kLotLevelHeight = 3.f;   // a car park's levels, above its ground (until parking garages are buildings)

// The walk graph's authored inputs (docs/HANDBUILT_WORLD.md 12.3): the AI joins them to the sidewalks
enum PathKind : u8 { WK_WALK = 0, WK_PLAZA, WK_ALLEY, WK_TRAIL, WK_BOARDWALK, WK_FOOTBRIDGE, WK_UNDERPASS, WK_COUNT };
PathKind pathKind(const std::string& word, bool* known = nullptr);   // walk ... underpass; WK_WALK with *known false
struct HandPath {
    std::string id;
    u8 kind = WK_WALK;
    float w = 0.f;
    std::vector<vec3> pts;             // on the ground (a sidewalk where it runs on one); a footbridge's or underpass's
                                       // as its profile gives them
};
struct HandPlaza {
    std::string id;                    // empty: none written
    float z = std::numeric_limits<float>::quiet_NaN();   // NaN: on the ground
    std::vector<vec2> poly;
    std::vector<std::string> tags;
};
struct HandSteps {
    vec3 bottom = vec3(0.f), top = vec3(0.f);
    float w = 0.f;
    int treads = 0;
    float rise = 0.f;                  // treads * rise is top.z - bottom.z
};

// A scenario point (docs/NTW_FORMAT.md 9.5's spot kinds, outdoors and in)
enum SpotKind : u8 {
    SPOT_BENCH = 0, SPOT_WALL_SIT, SPOT_STEPS_SIT, SPOT_LEAN, SPOT_SMOKE, SPOT_PHONE, SPOT_BUSSTOP_WAIT, SPOT_QUEUE, SPOT_WINDOW, SPOT_ATM, SPOT_VENDING, SPOT_CAFE_SEAT,
    SPOT_BAR_STOOL, SPOT_BUSK, SPOT_VENDOR, SPOT_GUARD, SPOT_DOOR_STAFF, SPOT_WATCH_VIEW, SPOT_FISH, SPOT_EXERCISE, SPOT_SUNBATHE, SPOT_SLEEP_ROUGH, SPOT_TALK_RING, SPOT_COVER,
    SPOT_DESK, SPOT_COUNTER_SERVE, SPOT_COUNTER_QUEUE, SPOT_BROWSE, SPOT_BED, SPOT_SOFA, SPOT_KITCHEN, SPOT_WASHROOM, SPOT_BAR, SPOT_DANCE, SPOT_COUNT
};
SpotKind spotKind(const std::string& word, bool* known = nullptr);   // bench ... dance; SPOT_BENCH with *known false
const char* spotKindName(int kind);
struct HandSpot {
    enum : u8 { IMPLIED = 1, COVER = 2 };   // flags: derived from a prop or a bus stop, not written; sheltered (rain)
    u8 kind = SPOT_BENCH;
    u8 flags = 0;
    std::string id;                    // empty: none written (and every implied one)
    vec3 at = vec3(0.f);               // a seat's centre at its height; a queue's head
    vec2 dir = vec2(0.f);              // the way the one using it faces (unit); 0, 0: any way
    std::vector<vec3> line;            // a queue, its head first
    int cap = 1;
    std::vector<u16> hours;            // minutes of the day, from and to per range (to before from: past midnight)
    std::vector<std::string> roles;
    u8 ageMin = 0, ageMax = 255;
    int prop = -1;                     // the prop it uses: an index into the blocks' props
    std::string pose;
    std::string target;                // for=: what it serves (a bus stop's id; a door or a counter, as written)
};

// A prop's collider as the catalog gives it (collide= box or cylinder, at the piece's body= or else size=, times its
// scale), for the AI's queries (World::siteColliderNear): what walkers steer round and what turning circles keep clear
// of. The physics builds its own from the prop types.
struct HandCollider {
    enum : u8 { BOX = 0, CYLINDER };
    u8 kind = BOX;
    int prop = -1;                     // the prop it is
    vec2 c = vec2(0.f);
    float z0 = 0.f, z1 = 0.f;          // its bottom and top
    vec2 he = vec2(0.f);               // a box's half width (along ax) and half depth; a cylinder's radius in both
    vec2 ax = vec2(1.f, 0.f);          // a box's width axis
};

struct HandBlocks {
    std::vector<GroundPoly> ground;    // in drawing order: a parcel's ground, then its paint and pads over it
    std::vector<PropInstance> props;   // absolute positions; yaw in the game's prop frame
    std::vector<LightInstance> lamps;
    std::vector<u8> lampSched;         // per lamp: its RoomSchedule (roomlight.h), for the renderer's switching later
    std::vector<HandParcel> parcels;   // "blocks.parcels"
    std::vector<HandLot> lots;         // "blocks.lots"
    std::vector<HandPath> paths;       // "blocks.walk"
    std::vector<HandPlaza> plazas;
    std::vector<HandSteps> steps;
    std::vector<HandSpot> spots;       // "blocks.spots"
    std::vector<HandCollider> colliders;   // "blocks.colliders"

    // Each cell's share (index cy * kCellsPerSide + cx): a polygon by its centroid, a prop or a lamp by its position
    std::vector<std::vector<int>> groundByCell, propsByCell, lampsByCell;
    std::vector<std::vector<int>> collidersByCell;   // (in every cell a collider's footprint reaches)
    // The level ground (the flat polygons: pads, decks, levelled parcels and their paint, the junctions' corner slabs)
    // for padHeight, by tile over the box of all of it (levelOrg, levelNx by levelNy tiles of levelTile metres: 8 m,
    // unless the level ground spans more than 4 km): a polygon in every tile its outline's box reaches, a little past its
    // edges, in the polygons' order
    std::vector<std::vector<int>> levelByTile;
    vec2 levelOrg = vec2(0.f);
    float levelTile = 8.f;
    int levelNx = 0, levelNy = 0;
    void index();

    // Whether a collider comes within `margin` of p in plan and overlaps p.z to p.z + height in z, as
    // World::siteColliderNear asks of the sites' structures (inline: the tools that build cells link without this file)
    bool colliderNear(vec3 p, float margin, float height) const {
        if (collidersByCell.empty()) return false;
        const int x0 = Max((int)floorf((p.x - margin + kWorldHalf) / kCellSize), 0), x1 = Min((int)floorf((p.x + margin + kWorldHalf) / kCellSize), kCellsPerSide - 1);
        const int y0 = Max((int)floorf((p.y - margin + kWorldHalf) / kCellSize), 0), y1 = Min((int)floorf((p.y + margin + kWorldHalf) / kCellSize), kCellsPerSide - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                for (int ci : collidersByCell[(size_t)y * kCellsPerSide + x]) {
                    const HandCollider& k = colliders[(size_t)ci];
                    if (k.z1 < p.z || k.z0 > p.z + height) continue;
                    const vec2 d = p.xy() - k.c;
                    if (k.kind == HandCollider::CYLINDER) {
                        if (length(d) <= k.he.x + margin) return true;
                        continue;
                    }
                    const float ex = Max(0.f, fabsf(dot(d, k.ax)) - k.he.x), ey = Max(0.f, fabsf(dot(d, vec2(-k.ax.y, k.ax.x))) - k.he.y);
                    if (ex * ex + ey * ey <= margin * margin) return true;
                }
        return false;
    }

    // The level ground's top at p: the highest flat polygon covering p whose z is at or below maxZ. False where none
    // does (the terrain, the roads and the sites answer there). A raised pad then has a step at its edge, which people
    // and vehicles climb or are stopped by as at a kerb.
    bool padHeight(vec2 p, float* z, float maxZ) const {
        const int tx = (int)floorf((p.x - levelOrg.x) / levelTile), ty = (int)floorf((p.y - levelOrg.y) / levelTile);
        if (tx < 0 || ty < 0 || tx >= levelNx || ty >= levelNy) return false;
        bool found = false;
        float best = -1e9f;
        for (int gi : levelByTile[(size_t)ty * (size_t)levelNx + (size_t)tx]) {
            const GroundPoly& g = ground[(size_t)gi];
            if (g.z > maxZ || g.z <= best) continue;
            bool in = false;   // (even-odd)
            for (size_t i = 0, j = g.poly.size() - 1; i < g.poly.size(); j = i++) {
                const vec2 a = g.poly[i], b = g.poly[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
            }
            if (in) {
                best = g.z;
                found = true;
            }
        }
        if (found) *z = best;
        return found;
    }
};

// Adds "blocks", "blocks.parcels", "blocks.lots", "blocks.walk", "blocks.spots" and "blocks.colliders" (each only when
// it has something)
void writeBlocks(const HandBlocks& blocks, WorldBinWriter& out);

// Reads "blocks" and the sections beside it into `out` (replacing it) and indexes it by cell. False, with a reason and
// `out` left empty, when a section is damaged, truncated, over-long, of another version, or names an edge past
// `roadEdges` (the network's edge count; -1: unchecked) or a prop the blocks don't have (a spot's, a collider's); true
// with the lists empty
// where there are none.
bool readBlocks(const WorldBinView& bin, HandBlocks& out, std::string* error, int roadEdges = -1);

// The game's copy in hand mode (nullptr in the procedural world). Inline, so the tools that build cells without the
// hand-built world (traffic_sim, worldcheck, terrainbake) link without blocksbin.cpp
inline HandBlocks* gHandBlocks = nullptr;

}  // namespace World
