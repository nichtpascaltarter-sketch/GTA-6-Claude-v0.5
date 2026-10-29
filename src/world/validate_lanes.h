// worldv's checks on the lane graph that LaneGraph::build (src/game/lanes.cpp) makes of the hand-built roads, as the
// game builds it (docs/HANDBUILT_WORLD.md 9.1 and 12.6): lanes that lead nowhere, and each authored signal plan against
// the movements and crossings its node has. The plans are resolved to edges as the compiler resolves them (worldmodel.h
// resolvePlan). The lane graph reads the roads' overrides as far as they go (roadoverrides.h); the junction records it
// doesn't read yet (ban, connect under turns=custom, turns=no_uturn) and the edges' moves are applied to its movements
// here. Host tools only. The build links lanes.cpp, with tools/worldv_host.cpp for the world's globals it reads.
#pragma once
#include "validate_roads.h"
#include "roadoverrides.h"

namespace World {
namespace Validate {

// road.lanes, signal.cover and signal.conflict, on the world's model, the RoadNetwork compileRoads() made of it (each
// side's widths applied: roadsbin.h applyRoadSides) and what the roads author beyond it (compileRoadOverrides), all of
// which the lane graph is built from
void checkLaneGraph(const Hand::Model& m, const RoadNetwork& net, const Hand::RoadIndex& idx, const RoadOverrides& ov, std::vector<ModelFinding>& out,
                    LaneGraphCounts& counts);

}  // namespace Validate
}  // namespace World
