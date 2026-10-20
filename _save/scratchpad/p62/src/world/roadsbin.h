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
#pragma once
#include "roads.h"
#include "worldbin.h"
#include <string>
#include <vector>

namespace World {

constexpr u32 kRoadsSectionVersion = 1;

// Adds "roads" and, when the ids are given (one per node and one per edge), "roads.ids"
void writeRoads(const RoadNetwork& rn, const std::vector<std::string>* nodeIds, const std::vector<std::string>* edgeIds, WorldBinWriter& out);

// Reads "roads" into `out` (replacing it), and "roads.ids" into nodeIds/edgeIds when they aren't nullptr (left empty
// when the section is missing). False, with a reason, when "roads" is missing, damaged, of another version or
// inconsistent (an edge naming a node that isn't there, fewer than 2 points).
bool readRoads(const WorldBinView& bin, RoadNetwork& out, std::vector<std::string>* nodeIds, std::vector<std::string>* edgeIds, std::string* error);

}  // namespace World
