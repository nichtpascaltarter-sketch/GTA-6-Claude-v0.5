// The hand-built world's blocks as the game uses them (docs/HANDBUILT_WORLD.md section 4.5): the ground's paint and
// pads, and the dressing (trees, plants, street furniture and lights). tools/worldc.cpp compiles them from the block
// files into world.bin's "blocks" section (worldmodel.h compileBlocks), with every height already on the graded terrain;
// the game reads them back in hand mode (handworld.cpp), and generateCell draws each cell's share (cellgen.cpp).
//
// Section "blocks" (WSK_LZ), little-endian:
//   version u32 (kBlocksSectionVersion)
//   ground count u32, then per polygon: kind | flat << 8 (u32), z (f32), point count (u32), then x, y per point (f32)
//   prop count u32, then per prop: type | variant << 8 | flags << 16 (u32), x, y, z, yaw, scale (f32)
//   lamp count u32, then per lamp: type | sched << 8 (u32), x, y, z, r, g, b (colour times intensity), radius,
//     dx, dy, dz (its direction, 0 for a point light), cone (cos of the outer angle) (f32)
#pragma once
#include "worldtypes.h"
#include "worldbin.h"
#include <string>
#include <vector>

namespace World {

constexpr u32 kBlocksSectionVersion = 1;

// The ground kinds of parcels, paint, plazas and pads
enum GroundKind : u8 { GK_LAWN = 0, GK_PAVING, GK_GRAVEL, GK_DIRT, GK_SAND, GK_PLANTING, GK_CONCRETE, GK_DECK, GK_COUNT };
GroundKind groundKind(const std::string& word, bool* known = nullptr);   // lawn ... deck; GK_LAWN with *known false

struct GroundPoly {
    u8 kind = GK_LAWN;
    u8 flat = 0;                       // 1: level at z (a pad); 0: on the graded terrain, z above it
    float z = 0.f;
    std::vector<vec2> poly;            // a simple polygon, either winding
};

struct HandBlocks {
    std::vector<GroundPoly> ground;    // in drawing order: a parcel's ground, then its paint and pads over it
    std::vector<PropInstance> props;   // absolute positions; yaw in the game's prop frame
    std::vector<LightInstance> lamps;
    std::vector<u8> lampSched;         // per lamp: its RoomSchedule (roomlight.h), for the renderer's switching later

    // Each cell's share (index cy * kCellsPerSide + cx): a polygon by its centroid, a prop or a lamp by its position
    std::vector<std::vector<int>> groundByCell, propsByCell, lampsByCell;
    void index();
};

// Adds "blocks" (nothing when `blocks` is empty)
void writeBlocks(const HandBlocks& blocks, WorldBinWriter& out);

// Reads "blocks" into `out` (replacing it) and indexes it by cell. False, with a reason and `out` left empty, when the
// section is damaged, truncated, over-long or of another version; true with `out` empty when there is none.
bool readBlocks(const WorldBinView& bin, HandBlocks& out, std::string* error);

// The game's copy in hand mode (nullptr in the procedural world)
extern HandBlocks* gHandBlocks;

}  // namespace World
