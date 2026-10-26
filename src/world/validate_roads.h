// worldv's checks on the world model (worldmodel.h): the roads, their signal plans and the transit lines on them
// (docs/HANDBUILT_WORLD.md 9.1 and 12.6). validate.cpp runs them in two passes:
// - one file's own records, on a model typed from that file alone: cached with the file's other results, and run in a
//   file run too;
// - the network across files, on the whole world's model and the RoadNetwork that compileRoads() makes of it: world runs
//   only.
// Host tools only, like the rest of worldv.
#pragma once
#include "validate.h"
#include "worldmodel.h"
#include <string>
#include <utility>
#include <vector>

namespace World {
namespace Validate {

// A finding at a place in the model's files (Src::file indexes Model::files)
struct ModelFinding {
    Hand::Src src;
    Severity severity = SEV_ERROR;
    const char* check = "";
    std::string message;
};

// The file kinds each pass types: a file's own checks read districts, roads, signal plans, transit and blocks (the kinds
// with compound values); the network's read the world, the districts, the roads, the signal plans and transit
bool fileModelKind(const std::string& kind);
bool worldModelKind(const std::string& kind);

// One file's checks, on a model loaded from it alone (its references to other files stay unresolved), with what
// loadParsed() reported for it: value.form (the loader's), road.along, road.moves (each record against its own edge's
// lanes), parking.clash, busstop.kerb and signal.plan
void checkFileModel(const Hand::Model& m, const std::vector<Hand::Error>& loaderErrors, std::vector<ModelFinding>& out);

// What the roads summary line counts
struct RoadCounts {
    size_t nodes = 0, edges = 0, junctions = 0, plans = 0, lines = 0;
    double km = 0.0;                                         // the edges that compiled
    std::vector<std::pair<std::string, size_t>> authored;    // record type -> count (only those written), in the format's order
};

// The checks across the world's road network, on the whole world's model: road.loop, road.shape and road.profile (the
// roads compile's), road.overlap, road.moves (the turns each lane names), road.sector, road.legs, road.connected,
// signal.node, transit.route and transit.stop
void checkWorldModel(const Hand::Model& m, std::vector<ModelFinding>& out, RoadCounts& counts);

// worldv roads: 10 nodes, 11 edges (1.21 km), 1 junction, 1 signal plan, 1 transit line; authored: lanes 1, moves 1, ...
std::string formatRoadCounts(const RoadCounts& c);

}  // namespace Validate
}  // namespace World
