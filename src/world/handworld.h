// The hand-built world at runtime (docs/HANDBUILT_WORLD.md, section 7). --world=hand selects it and today's procedural
// generator stays the default. It holds the baked terrain, and the roads and blocks where world/ has them, read from
// the world.bin that the build embeds in the exe (tools/worldc.cpp writes it from world/).
#pragma once
#include "worldmap.h"
#include "worldbin.h"
#include <string>

namespace World {

struct RoadNetwork;
struct BuildingSet;
struct HandBuildings;

enum class WorldKind : u8 { Procedural = 0, Hand = 1 };
inline WorldKind gWorldKind = WorldKind::Procedural;
inline bool isHandWorld() { return gWorldKind == WorldKind::Hand; }

// --world=<value>: "hand" selects the hand-built world, "procedural" (or no value at all) the generator. Anything else
// is an error, and false leaves the procedural world selected.
bool selectWorldKind(const char* value, std::string* error);

// The world.bin the build embedded (the RCDATA resource NT_WORLD); nullptr in host tools and in exes built without one
const u8* embeddedWorldBin(size_t* size);

// Fills `map` from world.bin's terrain sections (heights, water, regions, coast distance, outlines and channels), and
// the grading under the roads where worldc wrote one ("terrain.grade"), then recomputes the splat. False with a
// reason on anything missing or malformed.
bool loadHandTerrain(const WorldBinView& bin, WorldMap& map, std::string* error);

// The hand-built buildings at load (handshells.cpp): each placement's streaming cell, and a facade entry for each
// window and glass door, appended to `bs.facades` (the renderer uploads them with the building set's): flagged a
// hand-built window with the schedule class of the room behind it (roomlight.h handWindowFlags)
void prepareHandBuildings(HandBuildings& hb, BuildingSet& bs);

// The load on the loading thread: world.bin from `worldFile` when one is given (development: --worldfile=<path>), else
// the embedded one; the terrain into `map`; its roads sections into `roads` (roadsbin.h), or an empty network when it
// has none, and what they author beyond it (roadoverrides.h) into the game's copy; its blocks (blocksbin.h) and its
// buildings (buildingsbin.h) into theirs; `buildings`, the generated world's building set, empty but for the hand-built
// windows' facade entries. Sets gMap, gRoads, gRoadOverrides, gHandBlocks, gHandBuildings and gBuildings.
bool loadHandWorld(const char* worldFile, WorldMap& map, RoadNetwork& roads, BuildingSet& buildings, std::string* error);

}  // namespace World
