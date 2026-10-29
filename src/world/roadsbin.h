// The road network's sections of world.bin (docs/HANDBUILT_WORLD.md section 6). tools/worldc.cpp writes them from the
// hand-built roads it compiled (worldmodel.h compileRoads), and the game reads them back in hand mode into the
// RoadNetwork everything else uses (handworld.cpp). The tests check that a network comes back exactly as written.
//
// Section "roads" (WSK_LZ or WSK_RAW), little-endian:
//   version u32 (kRoadsSectionVersion)
//   node count u32, then per node: x, y, z, radius (f32), control | highway << 8 (u32)
//   edge count u32, then per edge: n0, n1 (u32), cls | flags << 8 | lanesF << 16 | lanesB << 24 (u32), halfWidth,
//     sidewalk, cut0, cut1 (f32), seed (u32), name (u32 length, bytes), point count (u32), then x, y, z per point (f32)
// Section "roads.ids": node count u32 and each node's id (u32 length, bytes), then the same for the edges, in network
// order: missions, saves and tools name roads by id.
// Reading rebuilds what follows from these: each node's edge list (in edge order, n0 before n1), the edges' cumulative
// distances and lengths, and the spatial hash.
// Section "roads.overrides" (WSK_LZ), what the roads author beyond the network (roadoverrides.h):
//   version u32 (kRoadOverridesSectionVersion)
//   edge count u32 (0, or the roads section's), then per edge: speed, laneWidth, median, kerbL, kerbR, parkingL,
//     parkingR, sidewalkL, sidewalkR (f32), flags (u32)
//   node rule count u32, then per rule: node (u32), control | flags << 8 (u32), bulb (f32), then the stops, the yields
//     and the no-red-turn approaches, each a count (u32) and that many edges (u32)
#pragma once
#include "roads.h"
#include "roadoverrides.h"
#include "worldbin.h"
#include <string>
#include <vector>

namespace World {

constexpr u32 kRoadsSectionVersion = 1;
constexpr u32 kRoadOverridesSectionVersion = 2;

// Adds "roads" and, when the ids are given (one per node and one per edge), "roads.ids"
void writeRoads(const RoadNetwork& rn, const std::vector<std::string>* nodeIds, const std::vector<std::string>* edgeIds, WorldBinWriter& out);

// Reads "roads" into `out` (replacing it), and "roads.ids" into nodeIds/edgeIds when they aren't nullptr (left empty
// when the section is missing). False, with a reason, when "roads" is missing, damaged, of another version or
// inconsistent (an edge naming a node that isn't there, fewer than 2 points).
bool readRoads(const WorldBinView& bin, RoadNetwork& out, std::vector<std::string>* nodeIds, std::vector<std::string>* edgeIds, std::string* error);

// Adds "roads.overrides" (nothing when `ov` is empty)
void writeRoadOverrides(const RoadOverrides& ov, WorldBinWriter& out);

// Reads "roads.overrides" into `out` (replacing it), for the network `roads` read from the same world.bin. False, with
// a reason and `out` left empty, when the section is damaged, truncated, over-long, of another version or for another
// network (another edge count, a node or edge that isn't there); true with `out` empty when there is none.
bool readRoadOverrides(const WorldBinView& bin, const RoadNetwork& roads, RoadOverrides& out, std::string* error);

// Each side's own kerb line and sidewalk from the overrides into the network's edges (RoadEdge::halfWidthSide and
// sidewalkSide): the wider side's are halfWidth and sidewalk already, so only the narrower side is trimmed. And each
// dead end's turning circle where one is written (RoadNode::bulb, which RoadNetwork::bulbRadius returns). Nothing
// changes where `ov` has no edges and no bulbs (the procedural world, or a world.bin without overrides). The game
// (handworld.cpp), the compiler (worldc) and loadRoadsFile apply it as they read the roads.
void applyRoadSides(const RoadOverrides& ov, RoadNetwork& roads);

// The roads of a world.bin file, read on the host without the game (tools/traffic_sim on the hand-built streets): its
// "roads" into `roads` (hashed, ready for queries, with each side's widths applied: applyRoadSides), "roads.overrides"
// into `ov`, and "roads.ids" into the ids when they aren't nullptr. False, with a reason and everything left empty,
// when the file can't be read, isn't a world.bin, or a section is missing or damaged. Link worldbin.cpp, lz.cpp and
// roadsbin.cpp, with roadnet.cpp (or roads.cpp).
bool loadRoadsFile(const char* path, RoadNetwork& roads, RoadOverrides& ov, std::string* error, std::vector<std::string>* nodeIds = nullptr,
                   std::vector<std::string>* edgeIds = nullptr);

}  // namespace World
