// worldv's checks on the lane graph that LaneGraph::build (src/game/lanes.cpp) makes of the hand-built roads, as the
// game builds it (docs/HANDBUILT_WORLD.md 9.1 and 12.6): lanes that lead nowhere, and each authored signal plan against
// the movements and crossings its node has. Until the lane graph reads the authored overrides itself, the junction
// records (ban, connect under turns=custom, turns=no_uturn) and the edges' moves are applied to its movements here.
// Host tools only. The build links lanes.cpp, with tools/worldv_host.cpp for the world's globals it reads.
#pragma once
#include "validate_roads.h"

namespace World {
namespace Validate {

// road.lanes, signal.cover and signal.conflict, on the world's model and the RoadNetwork compileRoads() made of it
void checkLaneGraph(const Hand::Model& m, const RoadNetwork& net, const Hand::RoadIndex& idx, std::vector<ModelFinding>& out, LaneGraphCounts& counts);

}  // namespace Validate
}  // namespace World
