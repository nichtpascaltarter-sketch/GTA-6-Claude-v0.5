// Tests for the world model (src/world/worldmodel.h):
// - discovery, and the loader on the format's samples (tests/world/fixtures/samples, each kind typed);
// - a two-district world (tests/world/fixtures/model, worldv-clean): the tables, their order, the references;
// - the findings on broken input (codes and lines), and files that don't parse or break the schema;
// - the roads compile: widths, heights, flags, junction radii, cut-backs and control, seeds, the spatial hash, and
//   the points a profile adds; the terrain graded under the roads (src/world/roadgrade.cpp), decks left alone;
// - the blocks: their records typed, the compile (ground, dressing, lamps, findings), the blocks section through
//   world.bin (src/world/blocksbin.h), a parcel's levelling (WorldMap::flattenPoly), the level ground that people and
//   vehicles stand on (HandBlocks::padHeight), and the grading written for a parcel that only grades the terrain
//   (Hand::placeOnTerrain); the sections for the AI's graphs (compileBlockSections): parcels, lots with their drive
//   graphs, the walk graph's inputs and the spots (written and implied), on the graded ground and through world.bin,
//   damaged ones refused, and the findings;
// - the roads through world.bin (src/world/roadsbin.h): back exactly as written, and damaged sections refused; with
//   what they author beyond the network (src/world/roadoverrides.h), each side's kerb line, sidewalk and parking
//   (applyRoadSides, RoadEdge::halfWidthSide), and the host loader (loadRoadsFile);
// - a node's legs, the turns between them and signal plans resolved on them (worldmodel.h nodeLegs, resolvePlan);
// - the rooms' light state (src/world/roomlight.h): fixed test vectors, which the renderer's HLSL copy must match too,
//   and the schedules' properties.
// Build and run from the repo root, as CI's GCC job does:
//   g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined
//       tests/world/worldmodel_test.cpp -o /tmp/neontide-worldmodel-test && /tmp/neontide-worldmodel-test
#include "../../tools/native_stubs.cpp"
#include "../../src/core/math.cpp"
#include "../../src/world/ntw.cpp"
#include "../../src/world/ntwschema.cpp"
#include "../../src/world/roadnet.cpp"
#include "../../src/world/terrainops.cpp"
#include "../../src/world/roadgrade.cpp"
#include "../../src/world/worldmodel.cpp"
#include "../../src/world/worldmodel_roads.cpp"
#include "../../src/world/lz.cpp"
#include "../../src/world/worldbin.cpp"
#include "../../src/world/gridcodec.cpp"
#include "../../src/world/roadsbin.cpp"
#include "../../src/world/blocksbin.cpp"
#include "../../src/world/worldmodel_blocks.cpp"
#include "../../src/world/roomlight.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace World;
using namespace World::Hand;
namespace fs = std::filesystem;

static int gFails = 0;
#define EXPECT(c, ...)                         \
    do {                                       \
        if (!(c)) {                            \
            printf("FAIL line %d: ", __LINE__); \
            printf(__VA_ARGS__);               \
            printf("\n");                      \
            gFails++;                          \
        }                                      \
    } while (0)

static const char* const kWorld = "tests/world/fixtures/model";
static const char* const kSamples = "tests/world/fixtures/samples";

static bool same(float a, float b, float tol = 0.005f) { return fabsf(a - b) <= tol; }

static void printErrors(const Model& m, const std::vector<Error>& errors) {
    for (const Error& e : errors) printf("  %s: %s [%s]\n", m.where(e.src).c_str(), e.message.c_str(), e.code.c_str());
}

static int countCode(const std::vector<Error>& errors, const char* code) {
    int n = 0;
    for (const Error& e : errors) n += e.code == code;
    return n;
}

static const Node* nodeOf(const Model& m, const char* id) {
    const int i = m.find("node", id);
    return i >= 0 ? &m.nodes[(size_t)i] : nullptr;
}
static const Edge* edgeOf(const Model& m, const char* id) {
    const int i = m.find("edge", id);
    return i >= 0 ? &m.edges[(size_t)i] : nullptr;
}

// A world in memory: (path, text) pairs, parsed and checked as worldv would, then typed
static bool loadTexts(const std::vector<std::pair<std::string, std::string>>& texts, Model& m, std::vector<Error>& errors) {
    std::vector<File> files;
    for (const auto& t : texts) {
        File f;
        f.path = t.first;
        std::vector<Ntw::Error> errs;
        const bool parsed = Ntw::parse(t.second.data(), t.second.size(), Ntw::schemaOpensBlock, f.doc, errs);
        if (parsed) Ntw::checkDocument(f.doc, errs);
        f.valid = parsed && errs.empty();
        for (const Ntw::Error& e : errs) printf("  (%s:%d: %s)\n", t.first.c_str(), e.line, e.message.c_str());
        files.push_back(std::move(f));
    }
    return loadParsed(std::move(files), m, errors);
}

// ------------------------------------------------------------------------------------------------- discovery, samples

static void testDiscover() {
    std::vector<Error> errors;
    const std::vector<std::string> paths = discover(kWorld, errors);
    EXPECT(errors.empty() && paths.size() == 11, "discover: %zu files, %zu errors", paths.size(), errors.size());
    EXPECT(std::is_sorted(paths.begin(), paths.end()), "discover: not sorted");
    if (paths.size() == 11) {
        EXPECT(paths.front() == std::string(kWorld) + "/districts/cl/blocks/b001/block.ntw", "discover: first %s", paths.front().c_str());
        EXPECT(paths.back() == std::string(kWorld) + "/world.ntw", "discover: last %s", paths.back().c_str());
    }
    errors.clear();
    const std::vector<std::string> none = discover("tests/world/fixtures/no_such_world", errors);
    EXPECT(none.empty() && errors.size() == 1 && errors[0].code == "file.read" && errors[0].src.file == -1, "discover: a missing directory");
}

static void testSamples() {
    Model m;
    std::vector<Error> errors;
    const bool ok = load(kSamples, m, errors);
    EXPECT(ok && errors.empty() && m.files.size() == 16, "samples: %zu files, %zu errors", m.files.size(), errors.size());
    if (!errors.empty()) printErrors(m, errors);
    for (const File& f : m.files) EXPECT(f.valid && !f.kind.empty(), "samples: %s not valid", f.path.c_str());

    // roads.ntw
    const Node* n12 = nodeOf(m, "cl.n0012");
    EXPECT(n12 && n12->control == Node::CTL_SIGNAL && n12->plan.text == "lib.signal.4way_proleft" && n12->plan.cls == "signal" && n12->plan.target == 0 &&
               same(n12->offset, 12.f) && same(n12->z, 3.4f) && n12->junction == 0 && n12->edges.size() == 1,
           "samples: node cl.n0012");
    const Node* n13 = nodeOf(m, "cl.n0013");
    EXPECT(n13 && n13->control == Node::CTL_STOP && !n13->stopsAll && n13->stops.size() == 2 && n13->stops[0].cls == "edge" && n13->stops[1].target == -1 &&
               n13->stops[1].classes == "edge" && n13->junction == -1,
           "samples: node cl.n0013 (cl.e0009 isn't in the samples)");
    const Edge* e7 = edgeOf(m, "cl.e0007");
    EXPECT(e7 && e7->cls == RC_STREET && e7->name == "SW 8th St" && e7->lanesF == 1 && e7->lanesB == 1 && same(e7->laneWidth, 3.2f) && same(e7->parkingL, 2.4f) &&
               same(e7->parkingR, 2.4f) && e7->shoulderL < 0.f && same(e7->sidewalkL, 3.f) && same(e7->speed, 13.4f) && e7->shape.size() == 3 &&
               e7->profile.size() == 2 && same(e7->profile[1].x, 115.5f) && same(e7->profile[1].y, 3.55f) && e7->median < 0.f,
           "samples: edge cl.e0007");
    if (e7) {
        EXPECT(e7->from.target == m.find("node", "cl.n0012") && e7->to.target == m.find("node", "cl.n0013"), "samples: cl.e0007's nodes");
        EXPECT(e7->laneCounts.size() == 1 && same(e7->laneCounts[0].from.s, 80.f) && e7->laneCounts[0].to.toEnd && e7->laneCounts[0].dir == DIR_FWD &&
                   e7->laneCounts[0].count == 2 && e7->laneCounts[0].add == SIDE_LEFT && e7->laneCounts[0].drop == -1 && same(e7->laneCounts[0].taper, 12.f),
               "samples: cl.e0007 lanes");
        EXPECT(e7->noSwitch.size() == 1 && e7->moves.size() == 1 && e7->moves[0].atEnd == 1 && e7->moves[0].lanes.size() == 2 && e7->moves[0].lanes[1] == "sr",
               "samples: cl.e0007 noswitch, moves");
        EXPECT(e7->crossings.size() == 2 && e7->crossings[1].kind == "zebra" && e7->kerb.size() == 3 && e7->kerb[0].type == "parkbay" && e7->kerb[0].meter == 1 &&
                   e7->kerb[1].kind == "junction" && e7->kerb[2].hours == "6.00-11.00" && e7->kerb[2].side == SIDE_LEFT,
               "samples: cl.e0007 crossings, kerb");
        EXPECT(e7->curbCuts.size() == 1 && e7->curbCuts[0].to.text == "cl.b014.lot1" && e7->curbCuts[0].to.cls == "lot" && e7->curbCuts[0].to.classes == "parcel|lot",
               "samples: cl.e0007 curbcut");
        EXPECT(e7->busStops.size() == 1, "samples: cl.e0007 busstop");
    }
    const int bs = m.find("busstop", "cl.bus.08");
    EXPECT(bs == 0 && m.busStops[0].edge == m.find("edge", "cl.e0007") && same(m.busStops[0].at, 40.f) && m.busStops[0].side == SIDE_RIGHT &&
               m.busStops[0].kind == "bus" && m.busStops[0].wait.size() == 1 && same(m.busStops[0].waitRadius, 3.f) && m.busStops[0].routes.size() == 1 &&
               m.busStops[0].routes[0].cls == "line" && m.busStops[0].shelter.text == "lib.prop.shelter_a" && m.busStops[0].shelter.target == -1,
           "samples: bus stop cl.bus.08");
    EXPECT(m.junctions.size() == 1 && m.junctions[0].node.cls == "node" && m.junctions[0].turns == "custom" && m.junctions[0].kind == "plain" &&
               m.junctions[0].stopLines.size() == 1 && same(m.junctions[0].stopLines[0].at, 6.5f) && m.junctions[0].connects.size() == 1 &&
               m.junctions[0].connects[0].fromEdge.cls == "edge" && m.junctions[0].connects[0].toEdge.text == "cl.e0011" && m.junctions[0].connects[0].toLane == 0 &&
               m.junctions[0].connects[0].turn == "left" && m.junctions[0].connects[0].isProtected,
           "samples: junction cl.n0012");

    // world.ntw and district.ntw
    EXPECT(m.districts.size() == 1, "samples: districts");
    if (!m.districts.empty()) {
        const District& d = m.districts[0];
        EXPECT(d.id == "cl" && d.name == "Calle Luna" && d.order == 1 && same(d.label.x, 1560.f) && d.file >= 0 && m.files[(size_t)d.file].kind == "district" &&
                   d.bounds.size() == 4 && d.zones.size() == 3 && d.trunkNodes.size() == 1 && d.trunkNodes[0].text == "tr.n00412" && d.trunkNodes[0].target == -1,
               "samples: district cl");
        if (d.zones.size() == 3)
            EXPECT(d.zones[2].id == "cl.ocho.nights" && d.zones[2].kind == "density" && d.zones[2].peds.size() == 24 && same(d.zones[2].peds[21], 2.4f) &&
                       d.zones[2].traffic.empty() && d.zones[2].days.size() == 2 && d.zones[0].tags.size() == 2,
                   "samples: zones");
    }

    // signal, transit, places
    EXPECT(m.signals.size() == 1 && m.signals[0].id == "lib.signal.4way_proleft" && same(m.signals[0].cycle, 90.f) && m.signals[0].legs == 4 &&
               m.signals[0].phases.size() == 3 && m.signals[0].phases[1].moves.size() == 6 && m.signals[0].phases[1].walk.size() == 2 &&
               same(m.signals[0].phases[1].walkTime, 7.f) && same(m.signals[0].phases[2].flash, 12.f) && same(m.signals[0].phases[0].green, 14.f),
           "samples: signal plan");
    EXPECT(m.transit.size() == 1 && m.transit[0].id == "bus.8" && m.transit[0].kind == "bus" && m.transit[0].name == "8 Calle Ocho" &&
               m.transit[0].routeEdges.size() == 2 && m.transit[0].routeEdges[0].target == -1 && m.transit[0].routeEdges[0].classes == "edge" &&
               m.transit[0].routeEdges[1].cls == "edge" && m.transit[0].routeEdges[1].target == m.find("edge", "cl.e0007") && m.transit[0].routeEdges[1].target >= 0 &&
               m.transit[0].stops.size() == 1 && m.transit[0].stops[0].busStop.target == bs && same(m.transit[0].headway, 600.f) && same(m.transit[0].last, 23.5f),
           "samples: transit bus.8");
    EXPECT(m.places.size() == 1 && m.places[0].id == "place.mari_apartment" && m.places[0].building.text == "cl.b014.m01" && m.places[0].building.target == -1 &&
               m.places[0].door == "f0.d1" && same(m.places[0].curb.z, 3.41f) && same(m.places[0].dir.y, 1.f),
           "samples: place");
    EXPECT(m.anchors.size() == 2 && m.anchors[0].kind == "police_station" && m.anchors[0].door == "f0.d1" && m.anchors[1].event == "busk" && same(m.anchors[1].yaw, 90.f),
           "samples: anchors");

    // block, building, library
    EXPECT(m.blocks.size() == 1 && m.blocks[0].id == "cl.b014" && m.blocks[0].district.cls == "district" && m.blocks[0].parcels.size() == 1 &&
               m.blocks[0].lots.size() == 1,
           "samples: block");
    EXPECT(m.parcels.size() == 1 && m.parcels[0].id == "cl.b014.p03" && m.parcels[0].block == 0 && m.parcels[0].use == "residential" &&
               m.parcels[0].poly.size() == 4 && m.parcels[0].front.cls == "edge" && m.parcels[0].frontSide == SIDE_RIGHT,
           "samples: parcel");
    EXPECT(m.lots.size() == 1 && m.lots[0].id == "cl.b014.lot1" && m.lots[0].levels == 1, "samples: lot");
    {
        const Parcel& p = m.parcels[0];
        EXPECT(p.graded && same(p.grade, 3.45f) && same(p.blend, 2.f) && p.ground == "lawn" && p.paints.size() == 1 && p.paints[0].kind == "paving" &&
                   p.paints[0].poly.size() == 4 && p.fences.size() == 1 && p.fences[0].kind.classes == "fence" &&
                   p.fences[0].kind.text == "lib.fence.picket_white" && p.fences[0].line.size() == 2 && p.fences[0].gates.size() == 1 &&
                   same(p.fences[0].gates[0].x, 6.f) && same(p.fences[0].gates[0].y, 1.f) && p.pads.empty(),
               "samples: the parcel's grade, ground, paint and fence (the samples' catalog has no pieces: unresolved)");
        EXPECT(m.plazas.size() == 1 && m.plazas[0].id == "cl.b014.pz1" && m.plazas[0].ground == "paving" && m.plazas[0].tags == std::vector<std::string>({"market"}) &&
                   m.blocks[0].plazas.size() == 1,
               "samples: plaza");
        const std::vector<Dress>& d = m.dressing;
        EXPECT(d.size() == 4 && m.blocks[0].dressing.size() == 4 && d[0].what == Dress::DRESS_TREE && d[0].kind.classes == "veg" &&
                   d[0].kind.text == "lib.veg.royal_palm" && d[0].kind.target == -1 && same(d[0].h, 11.5f) && d[0].hasYaw &&
                   same(d[0].yaw, 40.f) && d[1].what == Dress::DRESS_TREE && same(d[1].canopy, 8.f) && !d[1].hasYaw && d[2].what == Dress::DRESS_PROP &&
                   d[2].kind.classes == "prop" && !d[2].hasZ && same(d[2].yaw, 180.f) && d[3].what == Dress::DRESS_LIGHT && d[3].kind.classes == "light" && d[3].hasZ &&
                   same(d[3].z, 2.6f) && same(d[3].at.x, 1701.1f) && d[3].sched == "evening",
               "samples: the dressing");
    }
    EXPECT(m.buildings.size() == 1 && m.buildings[0].id == "cl.b014.h03" && m.buildings[0].block.target == 0 && m.buildings[0].parcel.cls == "parcel" &&
               m.buildings[0].plan.cls == "plan" && same(m.buildings[0].yaw, 90.f) && m.buildings[0].mirror && same(m.buildings[0].z, 3.45f),
           "samples: building");
    const int pal = m.find("palette", "lib.palette.bungalow.coral"), kit = m.find("kit", "lib.kit.sofa_3seat"), lay = m.find("layout", "lib.layout.house.bungalow_b02_f0");
    EXPECT(pal >= 0 && m.library[(size_t)pal].record == 0 && kit >= 0 && m.library[(size_t)kit].kind == "kit" && lay >= 0 && m.library[(size_t)lay].record == -1 &&
               m.find("set", "lib.set.living.cottage_01") >= 0 && m.find("facade", "lib.facade.bungalow_siding") >= 0 && m.find("plan", "lib.plan.house.bungalow_b02") >= 0,
           "samples: library items");
    EXPECT(m.find("edge", "cl.n0012") == -1 && m.find("node", "cl.nothing") == -1, "samples: find by the wrong class, or nothing");
}

// --------------------------------------------------------------------------------------------- the two-district world

static void testWorld() {
    Model m;
    std::vector<Error> errors;
    const bool ok = load(kWorld, m, errors);
    EXPECT(ok && errors.empty() && m.files.size() == 11, "world: %zu files, %zu errors", m.files.size(), errors.size());
    if (!errors.empty()) printErrors(m, errors);
    EXPECT(m.ids.size() == 33, "world: %zu ids (worldv counts 33)", m.ids.size());

    // tables in discovery order, records in file order; the districts in world.ntw's
    static const char* const kNodes[] = {"cl.n0001", "cl.n0003", "cl.n0004", "cl.n0005", "sb.n0001", "sb.n0002", "sb.n0003", "tr.n00001", "tr.n00002"};
    static const char* const kEdges[] = {"cl.e0001", "cl.e0002", "cl.e0003", "cl.e0004", "sb.e0001", "sb.e0002", "sb.e0003", "tr.e00001"};
    EXPECT(m.nodes.size() == 9 && m.edges.size() == 8, "world: %zu nodes, %zu edges", m.nodes.size(), m.edges.size());
    for (size_t i = 0; i < m.nodes.size() && i < 9; i++) EXPECT(m.nodes[i].id == kNodes[i] && m.find("node", kNodes[i]) == (int)i, "world: node %zu is %s", i, m.nodes[i].id.c_str());
    for (size_t i = 0; i < m.edges.size() && i < 8; i++) EXPECT(m.edges[i].id == kEdges[i], "world: edge %zu is %s", i, m.edges[i].id.c_str());
    EXPECT(m.districts.size() == 2 && m.districts[0].id == "cl" && m.districts[1].id == "sb", "world: districts");
    if (m.districts.size() == 2) {
        const District& cl = m.districts[0];
        EXPECT(cl.name == "Calle Luna" && same(cl.label.x, 180.f) && cl.labelText == "Little Havana" && cl.zones.size() == 2 && cl.trunkNodes.size() == 1 &&
                   cl.trunkNodes[0].target == 7 && m.files[(size_t)cl.file].path == std::string(kWorld) + "/districts/cl/district.ntw",
               "world: district cl");
        EXPECT(m.districts[1].trunkNodes.size() == 1 && m.districts[1].trunkNodes[0].target == 8 && same(m.districts[1].label.x, 800.f), "world: district sb");
    }
    if (m.nodes.size() == 9 && m.edges.size() == 8) {
        const Node& n1 = m.nodes[0];
        EXPECT(n1.edges == std::vector<int>({0, 1, 2, 3}) && n1.junction == 0 && n1.control == Node::CTL_SIGNAL && n1.plan.cls == "signal" &&
                   n1.approaches.size() == 4 && n1.approaches[0].first == "n" && n1.approaches[0].second.target == 2 && n1.approaches[3].first == "w" &&
                   n1.approaches[3].second.target == 1,
               "world: node cl.n0001");
        EXPECT(m.nodes[7].edges == std::vector<int>({0, 7}) && m.nodes[8].edges == std::vector<int>({4, 7}) && m.nodes[1].edges == std::vector<int>({1}) &&
                   same(m.nodes[1].bulb, 8.f) && m.nodes[2].junction == -1,
               "world: incident edges, bulb");
        EXPECT(m.nodes[4].control == Node::CTL_YIELD && m.nodes[4].yields.size() == 1 && m.nodes[4].yields[0].target == 6 && m.nodes[5].control == Node::CTL_AUTO,
               "world: node sb.n0001");
        EXPECT(m.where(n1.src) == std::string(kWorld) + "/districts/cl/roads.ntw:3" && m.where(m.edges[1].src) == std::string(kWorld) + "/districts/cl/roads.ntw:11" &&
                   m.where(Src()) == "(no file)" && m.where(Src{n1.src.file, 0}) == std::string(kWorld) + "/districts/cl/roads.ntw",
               "world: where() %s / %s", m.where(n1.src).c_str(), m.where(m.edges[1].src).c_str());
        const Edge& e2 = m.edges[1];
        EXPECT(e2.busStops.size() == 1 && m.busStops[0].shelter.cls == "prop" && m.busStops[0].shelter.target == m.find("prop", "lib.prop.shelter_a") &&
                   m.busStops[0].routes[0].target == 0 && e2.curbCuts.size() == 1 && e2.curbCuts[0].to.cls == "lot" && e2.curbCuts[0].to.target == 0 &&
                   e2.signs.size() == 1 && e2.signs[0].value == "SW 8th St" && e2.crossings.size() == 1 && same(e2.crossings[0].w, 4.f),
               "world: edge cl.e0002's records");
        const Edge& e3 = m.edges[2];
        EXPECT(same(e3.median, 1.5f) && e3.medianBarrier && e3.laneUses.size() == 1 && e3.laneUses[0].index == -1 && e3.laneUses[0].side == SIDE_RIGHT &&
                   e3.laneUses[0].use == "bike" && e3.laneUses[0].to.toEnd && e3.laneCounts.size() == 1 && e3.laneCounts[0].dir == DIR_BWD && e3.moves.size() == 1 &&
                   e3.moves[0].atEnd == 0,
               "world: edge cl.e0003");
        const Edge& e4 = m.edges[3];
        EXPECT(e4.lanesF == 1 && e4.lanesB == 0 && same(e4.sidewalkL, 1.8f) && same(e4.sidewalkR, 0.f), "world: edge cl.e0004");
        const Edge& t1 = m.edges[7];
        EXPECT(t1.cls == RC_BOULEVARD && same(t1.median, 4.f) && same(t1.shoulderL, 1.f) && same(t1.shoulderR, 1.f) && t1.decks.size() == 1 && t1.decks[0].kind == "bridge" &&
                   t1.profile.size() == 3 && t1.from.target == 7 && t1.to.target == 8,
               "world: edge tr.e00001");
        EXPECT(m.junctions.size() == 1 && m.junctions[0].bans.size() == 1 && m.junctions[0].bans[0].fromEdge.target == 3 && m.junctions[0].heads.size() == 1 &&
                   m.junctions[0].heads[0].dir == DIR_BWD && m.junctions[0].heads[0].edge.target == 0 && same(m.junctions[0].heads[0].at.z, 5.5f) &&
                   m.junctions[0].turns == "no_uturn",
               "world: junction cl.n0001");
    }
    EXPECT(m.parcels.size() == 1 && m.parcels[0].front.target == 1 && m.parcels[0].frontSide == SIDE_RIGHT && m.lots.size() == 1 && m.blocks.size() == 1 &&
               m.blocks[0].district.target == 0,
           "world: block cl.b001");
    EXPECT(m.transit.size() == 1 && m.transit[0].routeEdges.size() == 4 && m.transit[0].routeEdges[2].target == 7 && m.transit[0].stops.size() == 1 &&
               m.transit[0].stops[0].busStop.target == 0,
           "world: transit bus.1");
    EXPECT(m.anchors.size() == 2 && m.anchors[1].edge.target == 5 && m.anchors[1].cap == 4 && m.places.size() == 1 && m.places[0].water.size() == 2 &&
               m.places[0].building.text.empty(),
           "world: places");
    EXPECT(m.signals.size() == 1 && m.signals[0].phases.size() == 2, "world: signal plan");

    // the same files give the same model
    Model again;
    std::vector<Error> e2;
    load(kWorld, again, e2);
    bool sameOrder = again.nodes.size() == m.nodes.size() && again.edges.size() == m.edges.size() && again.ids.size() == m.ids.size();
    for (size_t i = 0; sameOrder && i < m.edges.size(); i++) sameOrder = again.edges[i].id == m.edges[i].id && again.edges[i].to.target == m.edges[i].to.target;
    EXPECT(sameOrder, "world: a second load differs");
}

// ------------------------------------------------------------------------------------------------------ the compile

static void testCompile() {
    Model m;
    std::vector<Error> errors;
    load(kWorld, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    const bool ok = compileRoads(m, rn, ix, errors);
    EXPECT(ok && errors.empty(), "compile: %zu errors", errors.size());
    if (!errors.empty()) printErrors(m, errors);
    EXPECT(rn.nodes.size() == 9 && rn.edges.size() == 8 && ix.nodeOf.size() == 9 && ix.roadNode.size() == 9 && ix.edgeOf.size() == 8 && ix.roadEdge.size() == 8,
           "compile: %zu nodes, %zu edges", rn.nodes.size(), rn.edges.size());
    if (rn.nodes.size() != 9 || rn.edges.size() != 8) return;
    for (int i = 0; i < 8; i++) EXPECT(ix.edgeOf[(size_t)i] == i && ix.roadEdge[(size_t)i] == i, "compile: edge index %d", i);

    // widths: as written, else the class's
    const RoadEdge& a1 = rn.edges[0];   // avenue 2/2, the class's shoulder, sidewalk=both:3.50
    EXPECT(same(a1.halfWidth, 3.3f * 2.f + 2.2f) && same(a1.sidewalk, 3.5f) && a1.lanesF == 2 && a1.lanesB == 2 && a1.flags == 0 && a1.name == "Calle Ocho",
           "compile: cl.e0001 half width %.3f, sidewalk %.2f, flags %d", a1.halfWidth, a1.sidewalk, a1.flags);
    const RoadEdge& s2 = rn.edges[1];   // street 1/1, parking=both:2.40
    EXPECT(same(s2.halfWidth, 3.2f + 2.4f) && same(s2.sidewalk, 3.f), "compile: cl.e0002 half width %.3f", s2.halfWidth);
    const RoadEdge& s3 = rn.edges[2];   // street 1/1 with a 1.5 m median
    EXPECT(same(s3.halfWidth, 3.2f + 0.75f + 2.4f), "compile: cl.e0003 half width %.3f", s3.halfWidth);
    const RoadEdge& l4 = rn.edges[3];   // lane 1/0, sidewalk=left:1.80
    EXPECT(same(l4.halfWidth, 1.5f + 1.f) && same(l4.sidewalk, 1.8f) && (l4.flags & RF_ONEWAY) && l4.lanesF == 1 && l4.lanesB == 0, "compile: cl.e0004");
    const RoadEdge& s7 = rn.edges[6];   // sidewalk=none
    EXPECT(same(s7.sidewalk, 0.f) && (s7.flags & RF_NOSIDEWALK), "compile: sb.e0003 has no sidewalk");
    const RoadEdge& t8 = rn.edges[7];   // boulevard 2/2, median 4, shoulder=both:1.00, a deck
    EXPECT(same(t8.halfWidth, 3.4f * 2.f + 2.f + 1.f) && same(t8.sidewalk, 4.5f) && (t8.flags & RF_BRIDGE) && !(t8.flags & RF_ONEWAY), "compile: tr.e00001");

    // heights: the profile, else straight; ends at their nodes' z
    EXPECT(s2.pts.size() == 3 && same(s2.pts[0].z, 2.8f) && same(s2.pts[1].z, 2.9f) && same(s2.pts[2].z, 3.f), "compile: cl.e0002 heights");
    EXPECT(a1.pts.size() == 2 && same(a1.pts[0].z, 3.f) && same(a1.pts[1].z, 3.2f), "compile: cl.e0001 heights");
    EXPECT(t8.pts.size() == 3 && same(t8.pts[1].z, 9.f) && same(t8.pts[2].z, 3.1f), "compile: tr.e00001 heights");
    const RoadEdge& s6 = rn.edges[5];
    EXPECT(s6.pts.size() == 3 && same(s6.pts[1].z, 3.05f, 0.001f), "compile: sb.e0002 heights by plan distance: %.4f", s6.pts.size() == 3 ? s6.pts[1].z : 0.f);
    const float tl = sqrtf(100.f * 100.f + 5.8f * 5.8f) + sqrtf(100.f * 100.f + 5.9f * 5.9f);
    EXPECT(same(t8.length, tl, 0.01f) && t8.dist.size() == 3 && same(t8.dist[2], t8.length) && same(t8.posAt(t8.dist[1]).z, 9.f), "compile: tr.e00001 length %.3f", t8.length);

    // junctions: radius from the widest approach, cut-backs, control derived or as written
    const float r1 = 3.3f * 2.f + 2.2f + 3.5f * 0.3f + 2.f;
    EXPECT(same(rn.nodes[0].radius, r1) && rn.nodes[0].control == 2 && rn.nodes[0].edges == std::vector<int>({0, 1, 2, 3}) && !rn.nodes[0].highway,
           "compile: cl.n0001 radius %.3f control %d", rn.nodes[0].radius, rn.nodes[0].control);
    EXPECT(same(rn.nodes[4].radius, 3.3f * 2.f + 2.2f + 3.5f * 0.3f + 2.f) && rn.nodes[4].control == 0, "compile: sb.n0001 (yield: priority)");
    EXPECT(same(rn.nodes[7].radius, 0.f) && rn.nodes[7].control == 0 && same(rn.nodes[1].radius, 0.f), "compile: degree 1 and 2 nodes");
    EXPECT(same(a1.cut0, r1) && same(a1.cut1, 0.f) && same(l4.cut1, r1) && same(l4.cut0, 0.f), "compile: cut-backs %.3f %.3f", a1.cut0, a1.cut1);
    EXPECT(same(rn.nodes[0].z, 3.f) && same(rn.nodes[0].p.x, 200.f), "compile: node position");

    // seeds follow the ids; the spatial hash
    EXPECT(a1.seed == worldmodel_roads_detail::idSeed("cl.e0001") && a1.seed != s2.seed, "compile: seeds");
    EXPECT(rn.hashRes == (int)(2.f * kWorldHalf / RoadNetwork::kHashCell), "compile: hash %d", rn.hashRes);
    std::vector<int> atJunction;
    rn.edgesInRect(vec2(195.f, 145.f), vec2(205.f, 155.f), atJunction);
    EXPECT(atJunction == std::vector<int>({0, 1, 2, 3}), "compile: edges at cl.n0001: %zu", atJunction.size());
    float s = 0.f, d = 0.f;
    EXPECT(rn.nearestEdge(vec2(300.f, 152.f), 10.f, &s, &d) == 0 && same(s, 100.f, 0.05f) && same(d, 2.f, 0.01f), "compile: nearestEdge");

    // an authored control wins over the derived one: none at a junction that would get stop signs
    Model m2 = m;
    m2.nodes[4].control = Node::CTL_NONE;
    RoadNetwork rn2;
    RoadIndex ix2;
    std::vector<Error> e2;
    compileRoads(m2, rn2, ix2, e2);
    EXPECT(e2.empty() && rn2.nodes.size() == 9 && rn2.nodes[4].control == 0, "compile: control=none");
    m2.nodes[4].control = Node::CTL_AUTO;
    compileRoads(m2, rn2, ix2, e2);
    EXPECT(rn2.nodes.size() == 9 && rn2.nodes[4].control == 1, "compile: control derived (one avenue at a T: stop signs)");
    // stop: signs where every approach stops; a two-way stop runs as priority
    m2.nodes[4].control = Node::CTL_STOP;
    m2.nodes[4].stops = m2.nodes[4].yields;   // sb.e0003 only
    compileRoads(m2, rn2, ix2, e2);
    EXPECT(rn2.nodes.size() == 9 && rn2.nodes[4].control == 0, "compile: a two-way stop");
    for (int ei : {4, 5}) {
        Ref r;
        r.text = m2.edges[(size_t)ei].id;
        r.cls = "edge";
        r.target = ei;
        m2.nodes[4].stops.push_back(r);
    }
    compileRoads(m2, rn2, ix2, e2);
    EXPECT(rn2.nodes.size() == 9 && rn2.nodes[4].control == 1, "compile: every leg listed");
    m2.nodes[4].stops.clear();
    compileRoads(m2, rn2, ix2, e2);
    EXPECT(rn2.nodes.size() == 9 && rn2.nodes[4].control == 1 && e2.empty(), "compile: stop with no list");
}

// --------------------------------------------------------------------------------------------------------- findings

// A profile that bends between two shape points puts a point there, so the polyline carries it: a bridge deck on a
// straight shape, and a break between the points of a longer shape (the one on a shape point adds none)
static void testProfilePoints() {
    const std::string roads =
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=4.00\n"
        "node cl.n0002 at=0.00,180.00 z=4.10\n"
        "node cl.n0003 at=200.00,0.00 z=3.00\n"
        "node cl.n0004 at=400.00,0.00 z=3.40\n"
        "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=boulevard lanes=2/2 shape=0.00,0.00;0.00,180.00\n"
        "  profile=0.00:4.00;40.00:5.50;140.00:5.50;180.00:4.10\n"
        "  deck from=30.00 to=150.00 kind=bridge\n"
        "end\n"
        "edge cl.e0002 from=cl.n0003 to=cl.n0004 class=street lanes=1/1 shape=200.00,0.00;300.00,0.00;400.00,0.00\n"
        "  profile=0.00:3.00;100.00:3.20;150.00:4.00;200.00:3.40\n"
        "end\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"roads.ntw", roads}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    const bool compiled = ok && compileRoads(m, rn, ix, errors) && rn.edges.size() == 2;
    EXPECT(compiled, "profile points: the test roads compile");
    if (!compiled) {
        printErrors(m, errors);
        return;
    }
    const RoadEdge& deck = rn.edges[0];
    EXPECT(deck.pts.size() == 4 && same(deck.pts[1].y, 40.f) && same(deck.pts[1].z, 5.5f) && same(deck.pts[2].y, 140.f) && same(deck.pts[2].z, 5.5f) &&
               same(deck.pts[3].z, 4.1f) && same(deck.dist[1], 40.03f, 0.01f) && (deck.flags & RF_BRIDGE),
           "profile points: the deck's %zu points", deck.pts.size());
    const RoadEdge& st = rn.edges[1];
    EXPECT(st.pts.size() == 4 && same(st.pts[1].x, 300.f) && same(st.pts[1].z, 3.2f) && same(st.pts[2].x, 350.f) && same(st.pts[2].z, 4.f) && same(st.pts[3].x, 400.f),
           "profile points: the street's %zu points", st.pts.size());
}

// The terrain graded under the roads (roadgrade.cpp), as worldc runs it, on a plateau a metre above them: the ground
// under a street is cut to just below it and stays the plateau beyond its verge; a bridge's ramps sit on graded ground,
// and its deck leaves the ground under it alone: a hill and a river
static void testGrade() {
    const std::string roads =
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=4.00\n"
        "node cl.n0002 at=200.00,0.00 z=4.00\n"
        "node cl.n0003 at=0.00,180.00 z=4.00\n"
        "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;200.00,0.00\n"
        "end\n"
        "edge cl.e0002 from=cl.n0001 to=cl.n0003 class=street lanes=1/1 shape=0.00,0.00;0.00,180.00\n"
        "  profile=0.00:4.00;40.00:5.00;140.00:5.00;180.00:4.00\n"
        "  deck from=40.00 to=140.00 kind=bridge\n"
        "end\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"roads.ntw", roads}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    if (!ok || !compileRoads(m, rn, ix, errors) || rn.edges.size() != 2) {
        EXPECT(false, "grade: the test roads compile");
        printErrors(m, errors);
        return;
    }
    std::vector<std::vector<vec2>> decks(rn.edges.size());
    for (size_t i = 0; i < ix.edgeOf.size(); i++)
        for (const Deck& d : m.edges[(size_t)ix.edgeOf[i]].decks) decks[i].push_back(vec2(d.from, d.to));
    WorldMap map;
    const size_t n = (size_t)kHeightRes * kHeightRes;
    map.height.assign(n, 5.f);
    map.waterLevel.assign(n, kNoWater);
    // under the deck, 36 m and more from its ends: a hill (9 m) and a river (bed -3 m, water at 0)
    for (int ty = (int)worldToTexel(60.f); ty <= (int)worldToTexel(130.f); ty++)
        for (int tx = (int)worldToTexel(-20.f); tx <= (int)worldToTexel(20.f); tx++) {
            const float x = texelToWorld(tx), y = texelToWorld(ty);
            if (fabsf(x) > 12.f) continue;
            const size_t i = (size_t)ty * kHeightRes + tx;
            if (y >= 76.f && y <= 92.f) map.height[i] = 9.f;
            if (y >= 100.f && y <= 116.f) {
                map.height[i] = -3.f;
                map.waterLevel[i] = 0.f;
            }
        }
    gradeTerrainForRoads(rn, map, &decks);
    const float under = map.heightAt(100.f, 0.f), beyond = map.heightAt(100.f, 40.f), face = map.heightAt(100.f, 20.f);
    EXPECT(same(under, 3.7f, 0.01f) && same(beyond, 5.f) && face > 3.75f && face < 5.f, "grade: the street: under it %.2f, its cut face %.2f, beyond %.2f", under, face, beyond);
    const float ramp = map.heightAt(0.f, 20.f), hill = map.heightAt(0.f, 84.f), river = map.heightAt(0.f, 108.f);
    EXPECT(same(ramp, 4.2f, 0.01f) && same(hill, 9.f) && same(river, -3.f), "grade: the bridge: under its ramp %.2f, the hill %.2f, the river %.2f", ramp, hill, river);
    // without the decks, today's rule grades under the deck: the hill goes
    WorldMap plain = map;
    for (int ty = (int)worldToTexel(60.f); ty <= (int)worldToTexel(130.f); ty++)
        for (int tx = (int)worldToTexel(-20.f); tx <= (int)worldToTexel(20.f); tx++) {
            const float x = texelToWorld(tx), y = texelToWorld(ty);
            if (fabsf(x) <= 12.f && y >= 76.f && y <= 92.f) plain.height[(size_t)ty * kHeightRes + tx] = 9.f;
        }
    gradeTerrainForRoads(rn, plain);
    EXPECT(plain.heightAt(0.f, 84.f) < 5.f, "grade: without the decks the hill stays (%.2f)", plain.heightAt(0.f, 84.f));
}

// A deck whose ends fall inside a segment (Codex's case on PR #57): a straight 200 m street with no other points and a deck
// from 90 to 110 m, on ground at 9 m. Before, the grading judged the whole segment by its midpoint, on the deck, and left
// the road buried for all 200 m. Now the ground is graded off the deck on both sides, and nothing under it changes.
static void testDeckEnds() {
    const std::string roads =
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=4.00\n"
        "node cl.n0002 at=200.00,0.00 z=4.00\n"
        "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;200.00,0.00\n"
        "  deck from=90.00 to=110.00 kind=bridge\n"
        "end\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"roads.ntw", roads}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    if (!ok || !compileRoads(m, rn, ix, errors) || rn.edges.size() != 1 || rn.edges[0].pts.size() != 2) {
        EXPECT(false, "deck ends: the test street compiles, one segment");
        printErrors(m, errors);
        return;
    }
    std::vector<std::vector<vec2>> decks(1);
    for (const Deck& d : m.edges[(size_t)ix.edgeOf[0]].decks) decks[0].push_back(vec2(d.from, d.to));
    WorldMap map;
    const size_t n = (size_t)kHeightRes * kHeightRes;
    map.height.assign(n, 9.f);
    map.waterLevel.assign(n, kNoWater);
    gradeTerrainForRoads(rn, map, &decks);
    const float at50 = map.heightAt(50.f, 0.f), at150 = map.heightAt(150.f, 0.f);
    EXPECT(same(at50, 3.7f, 0.01f) && same(at150, 3.7f, 0.01f), "deck ends: graded off the deck: %.2f at s=50, %.2f at s=150", at50, at150);
    // every texel whose centre is under the deck (90 to 110 m along it), across the street and its blends: untouched
    int changed = 0, under = 0;
    for (int ty = (int)worldToTexel(-40.f); ty <= (int)worldToTexel(40.f); ty++)
        for (int tx = (int)worldToTexel(80.f); tx <= (int)worldToTexel(120.f); tx++) {
            const float x = texelToWorld(tx);
            if (x < 90.f || x > 110.f) continue;
            under++;
            changed += map.height[(size_t)ty * kHeightRes + tx] != 9.f;
        }
    EXPECT(under > 0 && changed == 0, "deck ends: %d of %d texels under the deck changed", changed, under);
}

// The blocks compile, through world.bin, and a parcel's levelling
static void testBlocks() {
    const std::string catalog =
        "ntw 1 catalog\n"
        "piece lib.veg.palm code=PROP_PALM size=4.00x4.00x8.00 variants=4 collide=cylinder\n"
        "piece lib.prop.bench code=PROP_BENCH size=1.80x0.60x0.90 variants=1 collide=box\n"
        "piece lib.prop.mystery code=PROP_NOPE collide=none\n"
        "piece lib.light.street_cobra code=PROP_STREETLIGHT size=0.40x2.40x8.80 variants=2 collide=cylinder\n"
        "piece lib.light.porch_warm code=LIGHT_PORCH collide=none\n";
    const std::string block =
        "ntw 1 block id=cl.b001 district=cl\n"
        "parcel cl.b001.p01 poly=100.00,100.00;140.00,100.00;140.00,130.00;100.00,130.00 use=residential grade=3.50 blend=4.00\n"
        "  ground=lawn\n"
        "  paint kind=paving poly=100.00,100.00;104.00,100.00;104.00,120.00;100.00,120.00\n"
        "  paint kind=gravel poly=110.00,110.00;112.00,110.00\n"
        "  pad poly=120.00,110.00;130.00,110.00;130.00,120.00;120.00,120.00 z=0.60 kind=deck\n"
        "end\n"
        "plaza cl.b001.pz1 poly=150.00,100.00;170.00,100.00;160.00,120.00 tags=market\n"
        "tree kind=lib.veg.palm at=105.00,125.00 h=12.00 yaw=30.0\n"
        "tree kind=lib.veg.palm at=115.00,125.00\n"
        "prop kind=lib.prop.bench at=120.00,98.00 yaw=90.0\n"
        "prop kind=lib.prop.mystery at=125.00,98.00\n"
        "light kind=lib.light.street_cobra at=130.00,98.00\n"
        "light kind=lib.light.porch_warm at=135.00,128.00,2.60 sched=evening\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"lib/catalog.ntw", catalog}, {"block.ntw", block}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, m, errors);
    EXPECT(ok, "blocks: the test files load");
    if (!ok) printErrors(m, errors);
    BlocksOut out;
    const bool compiled = compileBlocks(m, out, errors);
    EXPECT(!compiled && errors.size() == 2 && countCode(errors, "block.kind") == 1 && countCode(errors, "block.shape") == 1, "blocks: the findings (%zu)", errors.size());
    if (errors.size() != 2) printErrors(m, errors);
    const HandBlocks& hb = out.blocks;
    // the ground: the parcel's lawn, its paving (the gravel has 2 points), its deck, the plaza
    EXPECT(out.pads.size() == 1 && same(out.pads[0].z, 3.5f) && same(out.pads[0].blend, 4.f) && hb.ground.size() == 4 && out.groundOnTerrain.size() == 4,
           "blocks: %zu ground polygons, %zu pads", hb.ground.size(), out.pads.size());
    if (hb.ground.size() == 4) {
        EXPECT(hb.ground[0].kind == GK_LAWN && hb.ground[0].flat && same(hb.ground[0].z, 3.53f) && hb.ground[1].kind == GK_PAVING && same(hb.ground[1].z, 3.55f) &&
                   hb.ground[2].kind == GK_DECK && hb.ground[2].flat && same(hb.ground[2].z, 4.16f) && !out.groundOnTerrain[2] && hb.ground[3].kind == GK_PAVING &&
                   !hb.ground[3].flat && same(hb.ground[3].z, 0.04f) && hb.ground[3].poly.size() == 3,
               "blocks: the ground's kinds and heights");
    }
    // the dressing: two palms, the bench, the street light's pole (the mystery piece left out)
    EXPECT(hb.props.size() == 4, "blocks: %zu props", hb.props.size());
    if (hb.props.size() == 4) {
        const PropInstance &p0 = hb.props[0], &p1 = hb.props[1], &bench = hb.props[2], &pole = hb.props[3];
        EXPECT(p0.type == PROP_PALM && same(p0.scale, 1.5f) && same(p0.yaw, 30.f * kDegToRad + kPi, 0.001f) && same(p0.pos.z, 0.f) && p1.type == PROP_PALM &&
                   same(p1.scale, 1.f) && p1.variant < 4 && bench.type == PROP_BENCH && same(bench.yaw, 90.f * kDegToRad + kPi, 0.001f) && pole.type == PROP_STREETLIGHT &&
                   same(pole.yaw, kPi * 0.5f, 0.001f) && same(pole.pos.x, 130.f) && same(pole.pos.y, 98.f),
               "blocks: the props' types, scales and yaws");
    }
    // the lamps: the street light's head 2.2 m along its front (+y at yaw 0), 8.6 m up; the porch light at its at=
    EXPECT(hb.lamps.size() == 2 && hb.lampSched.size() == 2 && out.lampGroundAt.size() == 2, "blocks: %zu lamps", hb.lamps.size());
    if (hb.lamps.size() == 2) {
        EXPECT(same(hb.lamps[0].pos.x, 130.f) && same(hb.lamps[0].pos.y, 100.2f) && same(hb.lamps[0].pos.z, 8.6f) && hb.lamps[0].type == 0 && hb.lampSched[0] == 11 &&
                   same(hb.lamps[1].pos.z, 2.6f) && hb.lamps[1].type == 1 && hb.lampSched[1] == 12 && same(out.lampGroundAt[1].x, 135.f),
               "blocks: the lamps' heads, types and schedules");
    }
    // through world.bin, exactly, and indexed by cell
    WorldBinWriter w;
    writeBlocks(hb, w);
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    std::string why;
    HandBlocks back;
    EXPECT(view.open(bin.data(), bin.size(), &why) && readBlocks(view, back, &why), "blocks: read back (%s)", why.c_str());
    bool same_ = back.ground.size() == hb.ground.size() && back.props.size() == hb.props.size() && back.lamps.size() == hb.lamps.size() && back.lampSched == hb.lampSched;
    for (size_t i = 0; same_ && i < hb.ground.size(); i++)
        same_ = back.ground[i].kind == hb.ground[i].kind && back.ground[i].flat == hb.ground[i].flat && back.ground[i].z == hb.ground[i].z &&
                back.ground[i].poly.size() == hb.ground[i].poly.size() && memcmp(back.ground[i].poly.data(), hb.ground[i].poly.data(), hb.ground[i].poly.size() * sizeof(vec2)) == 0;
    for (size_t i = 0; same_ && i < hb.props.size(); i++) {
        const PropInstance &a = back.props[i], &b = hb.props[i];
        same_ = a.pos == b.pos && a.yaw == b.yaw && a.scale == b.scale && a.type == b.type && a.variant == b.variant && a.flags == b.flags;
    }
    for (size_t i = 0; same_ && i < hb.lamps.size(); i++) {
        const LightInstance &a = back.lamps[i], &b = hb.lamps[i];
        same_ = a.pos == b.pos && a.color == b.color && a.radius == b.radius && a.dir == b.dir && a.cone == b.cone && a.type == b.type;
    }
    EXPECT(same_, "blocks: the section read back differs");
    const size_t cell = (size_t)(int)floorf((100.f + kWorldHalf) / kCellSize) * kCellsPerSide + (size_t)(int)floorf((100.f + kWorldHalf) / kCellSize);
    EXPECT(back.groundByCell.size() == (size_t)kCellsPerSide * kCellsPerSide && back.groundByCell[cell].size() == 4 && back.propsByCell[cell].size() == 4 &&
               back.lampsByCell[cell].size() == 2,
           "blocks: by cell");
    // the level ground people and vehicles stand on: the deck's top on the deck, the levelled lawn beside it, nothing
    // off the parcel (the terrain answers there); under a lower limit, the highest level below it
    float onDeck = 0.f, beside = 0.f, under = 0.f, off = 0.f;
    const bool deckHit = back.padHeight(vec2(125.f, 115.f), &onDeck, 100.f), lawnHit = back.padHeight(vec2(110.f, 125.f), &beside, 100.f),
               underHit = back.padHeight(vec2(125.f, 115.f), &under, 4.f), offHit = back.padHeight(vec2(95.f, 115.f), &off, 100.f);
    EXPECT(deckHit && same(onDeck, 4.16f) && lawnHit && same(beside, 3.53f) && underHit && same(under, 3.53f) && !offHit,
           "blocks: the level ground (%d %.2f on the deck, %d %.2f beside it, %d %.2f under 4 m, %d off the parcel)", deckHit, onDeck, lawnHit, beside, underHit, under,
           offHit);
    {   // a pad across a cell's edge (x = 256) answers on both sides of it, and a draped polygon never does
        HandBlocks edge;
        GroundPoly pad;
        pad.kind = GK_CONCRETE;
        pad.flat = 1;
        pad.z = 6.f;
        pad.poly = {vec2(250.f, 100.f), vec2(262.f, 100.f), vec2(262.f, 110.f), vec2(250.f, 110.f)};
        edge.ground.push_back(pad);
        GroundPoly draped = pad;
        draped.flat = 0;
        draped.z = 9.f;
        draped.poly = {vec2(300.f, 100.f), vec2(310.f, 100.f), vec2(310.f, 110.f)};
        edge.ground.push_back(draped);
        edge.index();
        float left = 0.f, right = 0.f, dz = 0.f;
        const bool l = edge.padHeight(vec2(255.f, 105.f), &left, 100.f), r = edge.padHeight(vec2(259.f, 105.f), &right, 100.f),
                   d = edge.padHeight(vec2(307.f, 102.f), &dz, 100.f);
        EXPECT(l && same(left, 6.f) && r && same(right, 6.f) && !d && edge.groundByCell.size() == edge.levelByCell.size(),
               "blocks: a pad across a cell's edge (%d %d), a draped polygon (%d)", l, r, d);
    }
    // damaged: refused with a reason, nothing read; none: empty
    std::vector<u8> payload;
    EXPECT(view.bytes("blocks", payload) && payload.size() > 40, "blocks: the section's bytes");
    for (int k = 0; k < 3; k++) {
        std::vector<u8> bad = payload;
        if (k == 0) bad.resize(bad.size() - 5);
        if (k == 1) bad.push_back(0);
        if (k == 2) bad[0] = 9;
        WorldBinWriter w2;
        w2.addCompressed("blocks", bad);
        const std::vector<u8> b2 = w2.finish();
        WorldBinView v2;
        HandBlocks hb2;
        why.clear();
        EXPECT(v2.open(b2.data(), b2.size(), &why) && !readBlocks(v2, hb2, &why) && !why.empty() && hb2.props.empty() && hb2.ground.empty(),
               "blocks: damaged section %d was read", k);
    }
    WorldBinWriter w3;
    w3.add("terrain", WSK_RAW, std::vector<u8>(8, 0));
    const std::vector<u8> b3 = w3.finish();
    WorldBinView v3;
    HandBlocks none;
    EXPECT(v3.open(b3.data(), b3.size(), &why) && readBlocks(v3, none, &why) && none.props.empty() && none.propsByCell.size() == (size_t)kCellsPerSide * kCellsPerSide,
           "blocks: a world.bin without them");
    // a parcel's levelling: inside at its grade, the terrain again beyond the blend, between them a blend
    WorldMap map;
    const size_t n = (size_t)kHeightRes * kHeightRes;
    map.height.assign(n, 3.f);
    map.waterLevel.assign(n, kNoWater);
    map.flattenPoly({vec2(-20.f, -20.f), vec2(20.f, -20.f), vec2(20.f, 20.f), vec2(-20.f, 20.f)}, 5.f, 16.f);
    const float in = map.heightAt(0.f, 0.f), out1 = map.heightAt(60.f, 0.f), mid = map.heightAt(28.f, 0.f);
    EXPECT(same(in, 5.f) && same(out1, 3.f) && mid > 3.05f && mid < 4.95f, "blocks: the parcel levelled %.2f inside, %.2f beyond, %.2f between", in, out1, mid);
}

// A world without roads whose one parcel only grades the terrain (grade= and blend=, without ground, paint, pads or
// dressing): its grading is still written ("terrain.grade"), the parcel at its grade and the terrain elsewhere as baked.
// With nothing at all, nothing is written.
static void testGradeOnly() {
    const std::string block =
        "ntw 1 block id=cl.b001 district=cl\n"
        "parcel cl.b001.p01 poly=100.00,100.00;140.00,100.00;140.00,130.00;100.00,130.00 use=vacant grade=5.00 blend=4.00\n"
        "end\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"block.ntw", block}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, m, errors);
    BlocksOut out;
    const bool compiled = ok && compileBlocks(m, out, errors);
    EXPECT(compiled && out.pads.size() == 1 && out.blocks.ground.empty() && out.blocks.props.empty() && out.blocks.lamps.empty(),
           "grade only: the parcel compiles to a pad alone (%zu pads, %zu ground)", out.pads.size(), out.blocks.ground.size());
    if (!compiled) printErrors(m, errors);
    BakedTerrain baked;   // the game's grid, level at 3 m, dry
    baked.ok = true;
    baked.res = kHeightRes;
    baked.cell = kHeightCell;
    baked.originX = baked.originY = -kWorldHalf;
    baked.heightUnit = 0.01f;
    baked.height.assign((size_t)kHeightRes * kHeightRes, (i16)300);
    baked.water.assign(baked.height.size(), (i16)0);
    const RoadNetwork none;
    WorldBinWriter w;
    std::string why, report;
    const bool placed = placeOnTerrain(baked, none, {}, out, w, &why, &report);
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    std::vector<i16> delta;
    GridInfo info;
    const WorldBinView::Section* grade = placed && view.open(bin.data(), bin.size(), &why) ? view.find("terrain.grade") : nullptr;
    const bool written = grade && grade->kind == WSK_GRID && decodeGrid(grade->data, grade->size, delta, &info) && info.width == kHeightRes &&
                         info.height == kHeightRes && delta.size() == baked.height.size();   // (as the game reads it: handworld.cpp)
    EXPECT(written && report.find("and parcels") != std::string::npos, "grade only: terrain.grade written (%s; %s)", why.c_str(), report.c_str());
    if (written) {
        const auto at = [&](float x, float y) {
            const int i = (int)floorf((x + kWorldHalf) / kHeightCell), j = (int)floorf((y + kWorldHalf) / kHeightCell);
            return (int)delta[(size_t)j * kHeightRes + (size_t)i];
        };
        EXPECT(at(120.f, 115.f) == 200 && at(-5000.f, 5000.f) == 0, "grade only: %d under the parcel (200: 2 m up to its grade), %d far from it", at(120.f, 115.f),
               at(-5000.f, 5000.f));
    }
    BlocksOut empty;
    WorldBinWriter w2;
    const bool nothing = placeOnTerrain(baked, none, {}, empty, w2, &why, &report);
    const std::vector<u8> bin2 = w2.finish();
    WorldBinView view2;
    EXPECT(nothing && view2.open(bin2.data(), bin2.size(), &why) && !view2.find("terrain.grade") && !view2.find("blocks") && report.empty(),
           "grade only: with nothing to grade, nothing written");
}

// The blocks' sections for the AI's graphs (worldmodel.h compileBlockSections, blocksbin.h): the parcels' uses, fronts
// and grades; three lots' drive graphs (two rows facing across one aisle, an angled row's one-way aisle with its far end
// joined so it can be left, the curb cuts' nodes behind the sidewalk, a ramp up to a second level, an island's loop),
// their bays on their links, their heights on the graded ground (placeOnTerrain), both sections through world.bin and
// damaged ones refused; and the findings
static bool sameLot(const HandLot& a, const HandLot& b) {
    const auto v3 = [](vec3 p, vec3 q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
    const auto cut = [](const HandLot::Cut& p, const HandLot::Cut& q) { return p.edge == q.edge && p.s == q.s && p.side == q.side; };
    bool ok = a.id == b.id && a.levels == b.levels && cut(a.entry, b.entry) && cut(a.exit, b.exit) && a.rows.size() == b.rows.size() &&
              a.nodes.size() == b.nodes.size() && a.links.size() == b.links.size() && a.bays.size() == b.bays.size();
    for (size_t i = 0; ok && i < a.rows.size(); i++) {
        const HandLot::Row &p = a.rows[i], &q = b.rows[i];
        ok = v3(p.a, q.a) && v3(p.b, q.b) && p.bays == q.bays && p.w == q.w && p.len == q.len && p.dir.x == q.dir.x && p.dir.y == q.dir.y && p.level == q.level &&
             p.aisle == q.aisle;
    }
    for (size_t i = 0; ok && i < a.nodes.size(); i++) ok = v3(a.nodes[i].p, b.nodes[i].p) && a.nodes[i].level == b.nodes[i].level && a.nodes[i].kind == b.nodes[i].kind;
    for (size_t i = 0; ok && i < a.links.size(); i++) {
        const HandLot::Link &p = a.links[i], &q = b.links[i];
        ok = p.a == q.a && p.b == q.b && p.width == q.width && p.oneway == q.oneway && p.kind == q.kind && p.pts.size() == q.pts.size();
        for (size_t k = 0; ok && k < p.pts.size(); k++) ok = v3(p.pts[k], q.pts[k]);
    }
    for (size_t i = 0; ok && i < a.bays.size(); i++) {
        const HandLot::Bay &p = a.bays[i], &q = b.bays[i];
        ok = v3(p.c, q.c) && p.dir.x == q.dir.x && p.dir.y == q.dir.y && p.row == q.row && p.link == q.link && p.s == q.s;
    }
    return ok;
}

static void testBlockSections() {
    const std::string world = "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n";
    const std::string roads =   // a street east along y = 0: its left (north) kerb 5.6 m out, its sidewalk 3 m beyond
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=4.00\n"
        "node cl.n0002 at=200.00,0.00 z=4.00\n"
        "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;200.00,0.00\n"
        "end\n";
    const std::string block =
        "ntw 1 block id=cl.b001 district=cl\n"
        "parcel cl.b001.p01 poly=40.00,30.00;140.00,30.00;140.00,80.00;40.00,80.00 use=parking grade=4.20 blend=4.00 front=cl.e0001:left\n"
        "end\n"
        "parcel cl.b001.p02 poly=150.00,30.00;190.00,30.00;190.00,60.00;150.00,60.00 use=commercial\n"
        "end\n"
        "lot cl.b001.lot1 entry=cl.e0001:60.00:left exit=cl.e0001:120.00:left\n"
        "  row line=50.00,50.00;130.00,50.00 bays=32\n"
        "  row line=130.00,39.00;50.00,39.00 bays=32\n"
        "  row line=50.00,65.00;130.00,65.00 bays=20 yaw=-30.0\n"
        "end\n"
        "lot cl.b001.lot2 entry=cl.e0001:170.00:left levels=2\n"
        "  row line=150.00,50.00;190.00,50.00 bays=16\n"
        "  row line=150.00,50.00;170.00,50.00 bays=8 level=1\n"
        "  ramp from=0 to=1 line=190.00,44.50;170.00,44.50\n"
        "end\n"
        "lot cl.b001.lot3 entry=cl.e0001:100.00:left\n"
        "  row line=64.00,105.00;96.00,105.00 bays=12 aisle=60.00,90.00;100.00,90.00;100.00,110.00;60.00,110.00;60.00,90.00\n"
        "end\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"roads.ntw", roads}, {"block.ntw", block}, {"world.ntw", world}}, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    if (!ok || !compileRoads(m, rn, ix, errors) || rn.edges.size() != 1) {
        EXPECT(false, "sections: the test files load and their roads compile");
        printErrors(m, errors);
        return;
    }
    RoadOverrides ov;   // (each side's own widths, as worldc compiles the blocks on them)
    compileRoadOverrides(m, ix, rn, ov);
    applyRoadSides(ov, rn);
    const float back = rn.edges[0].halfWidthSide(-1) + rn.edges[0].sidewalkSide(-1);   // the sidewalk's back, 8.6 m north
    // (the lots stand 35 m and more north of the street, clear of the terrain its grading blends out over)
    BlocksOut out;
    const bool compiled = compileBlocks(m, out, errors) && compileBlockSections(m, ix, rn, out, errors);
    EXPECT(compiled && errors.empty() && same(back, 8.6f), "sections: the compile (%zu findings, the sidewalk's back %.2f m out)", errors.size(), back);
    if (!errors.empty()) printErrors(m, errors);
    const HandBlocks& hb = out.blocks;
    // the parcels: the first fronts the street's left, levelled; the second follows the terrain
    EXPECT(hb.parcels.size() == 2, "sections: %zu parcels", hb.parcels.size());
    if (hb.parcels.size() == 2) {
        const HandParcel &p = hb.parcels[0], &q = hb.parcels[1];
        EXPECT(p.id == "cl.b001.p01" && p.use == PU_PARKING && p.frontEdge == 0 && p.frontSide == 0 && same(p.grade, 4.2f) && p.poly.size() == 4 &&
                   q.use == PU_COMMERCIAL && q.frontEdge == -1 && std::isnan(q.grade),
               "sections: the parcels' uses, fronts and grades");
    }
    EXPECT(hb.lots.size() == 3, "sections: %zu lots", hb.lots.size());
    if (hb.lots.size() != 3) return;
    const auto at = [](vec3 p, float x, float y, float z) { return same(p.x, x, 0.01f) && same(p.y, y, 0.01f) && same(p.z, z, 0.01f); };
    const auto link = [](const HandLot& l, int i, int a, int b, float width, int oneway, u8 kind) {
        if (i < 0 || i >= (int)l.links.size()) return false;
        const HandLot::Link& k = l.links[(size_t)i];
        return k.a == a && k.b == b && same(k.width, width) && k.oneway == oneway && k.kind == kind && k.pts.size() >= 2 &&
               k.pts.front() == l.nodes[(size_t)a].p && k.pts.back() == l.nodes[(size_t)b].p;
    };
    {   // lot 1: rows 0 and 1 face each other across one aisle (y 44.5); row 2's bays lean 30 degrees east, so its aisle
        // (4 m, 4.79 m south of them) runs one way east, and its far end is joined to the shared aisle's so a car can
        // leave. The cuts at s 60 and 120 drive one way in and out, splitting the shared aisle where they meet it.
        const HandLot& l = hb.lots[0];
        EXPECT(l.id == "cl.b001.lot1" && l.levels == 1 && l.entry.edge == 0 && same(l.entry.s, 60.f) && l.entry.side == 0 && l.exit.edge == 0 &&
                   same(l.exit.s, 120.f) && l.exit.side == 0 && l.rows.size() == 3 && l.nodes.size() == 8 && l.links.size() == 8 && l.bays.size() == 84,
               "lot 1: %zu rows, %zu nodes, %zu links, %zu bays", l.rows.size(), l.nodes.size(), l.links.size(), l.bays.size());
        if (l.nodes.size() == 8 && l.links.size() == 8 && l.bays.size() == 84) {
            const float c = 65.f - 0.5f * (5.f * cosf(30.f * kDegToRad) + 2.5f * sinf(30.f * kDegToRad) + 4.f);   // row 2's aisle: 60.21
            EXPECT(at(l.nodes[0].p, 50.f, 44.5f, 0.f) && at(l.nodes[1].p, 130.f, 44.5f, 0.f) && at(l.nodes[2].p, 50.f, c, 0.f) && at(l.nodes[3].p, 130.f, c, 0.f) &&
                       at(l.nodes[4].p, 60.f, back, 0.f) && l.nodes[4].kind == HandLot::LN_ENTRY && at(l.nodes[5].p, 60.f, 44.5f, 0.f) &&
                       at(l.nodes[6].p, 120.f, back, 0.f) && l.nodes[6].kind == HandLot::LN_EXIT && at(l.nodes[7].p, 120.f, 44.5f, 0.f) &&
                       l.nodes[0].kind == HandLot::LN_AISLE && l.nodes[3].kind == HandLot::LN_AISLE,
                   "lot 1: the nodes");
            EXPECT(link(l, 0, 0, 5, 6.f, 0, HandLot::LL_AISLE) && link(l, 1, 2, 3, 4.f, 1, HandLot::LL_AISLE) && link(l, 2, 5, 7, 6.f, 0, HandLot::LL_AISLE) &&
                       link(l, 3, 4, 5, 4.f, 1, HandLot::LL_DRIVE) && link(l, 4, 7, 1, 6.f, 0, HandLot::LL_AISLE) && link(l, 5, 7, 6, 4.f, 1, HandLot::LL_DRIVE) &&
                       link(l, 6, 0, 2, 6.f, 0, HandLot::LL_DRIVE) && link(l, 7, 3, 1, 6.f, 0, HandLot::LL_DRIVE),
                   "lot 1: the links (the aisles, the one-way in and out, the angled row's aisle joined at both ends)");
            const HandLot::Bay &b0 = l.bays[0], &b4 = l.bays[4], &b31 = l.bays[31], &b32 = l.bays[32], &b64 = l.bays[64];
            EXPECT(at(b0.c, 51.25f, 50.f, 0.f) && same(b0.dir.x, 0.f) && same(b0.dir.y, 1.f) && b0.row == 0 && b0.link == 0 && same(b0.s, 1.25f) && b4.link == 2 && same(b4.s, 1.25f) &&
                       b31.link == 4 && same(b31.s, 8.75f) && at(b32.c, 128.75f, 39.f, 0.f) && same(b32.dir.y, -1.f) && b32.row == 1 && b32.link == 4 &&
                       same(b32.s, 8.75f) && at(b64.c, 52.f, 65.f, 0.f) && same(b64.dir.x, 0.5f) && same(b64.dir.y, 0.866f) && b64.row == 2 && b64.link == 1 &&
                       same(b64.s, 2.f),
                   "lot 1: the bays on their links");
            EXPECT(l.rows[0].aisle == 0 && l.rows[1].aisle == 4 && l.rows[2].aisle == 1 && l.rows[1].bays == 32 && at(l.rows[1].a, 130.f, 39.f, 0.f) &&
                       same(l.rows[2].dir.x, 0.5f) && same(l.rows[0].w, 2.5f) && same(l.rows[0].len, 5.f),
                   "lot 1: the rows");
        }
    }
    {   // lot 2: a ramp from the ground's aisle (its east end) up to the second level's (its east end), 3 m up
        const HandLot& l = hb.lots[1];
        EXPECT(l.levels == 2 && l.exit.edge == -1 && l.nodes.size() == 6 && l.links.size() == 5 && l.bays.size() == 24, "lot 2: %zu nodes, %zu links, %zu bays",
               l.nodes.size(), l.links.size(), l.bays.size());
        if (l.nodes.size() == 6 && l.links.size() == 5 && l.bays.size() == 24) {
            EXPECT(at(l.nodes[1].p, 190.f, 44.5f, 0.f) && l.nodes[1].kind == HandLot::LN_RAMP && at(l.nodes[3].p, 170.f, 44.5f, 3.f) && l.nodes[3].level == 1 &&
                       l.nodes[3].kind == HandLot::LN_RAMP && link(l, 2, 1, 3, 6.f, 0, HandLot::LL_RAMP) && link(l, 4, 4, 5, 6.f, 0, HandLot::LL_DRIVE) &&
                       l.nodes[4].kind == HandLot::LN_ENTRY && at(l.nodes[5].p, 170.f, 44.5f, 0.f) && link(l, 0, 0, 5, 6.f, 0, HandLot::LL_AISLE) &&
                       link(l, 3, 5, 1, 6.f, 0, HandLot::LL_AISLE) && link(l, 1, 2, 3, 6.f, 0, HandLot::LL_AISLE),
                   "lot 2: the ramp and the graph");
            EXPECT(l.bays[7].link == 0 && l.bays[8].link == 3 && same(l.bays[8].s, 1.25f) && l.bays[16].link == 1 && same(l.bays[16].c.z, 3.f) &&
                       l.rows[1].level == 1 && same(l.rows[1].a.z, 3.f),
                   "lot 2: the bays on both levels");
        }
    }
    {   // lot 3: one row on an island, its aisle= the loop round it: two links halfway round, the drive in splitting
        // the first where the loop passes nearest; every bay opens onto the loop's north side
        const HandLot& l = hb.lots[2];
        EXPECT(l.nodes.size() == 4 && l.links.size() == 4 && l.bays.size() == 12, "lot 3: %zu nodes, %zu links, %zu bays", l.nodes.size(), l.links.size(),
               l.bays.size());
        if (l.nodes.size() == 4 && l.links.size() == 4 && l.bays.size() == 12) {
            EXPECT(at(l.nodes[0].p, 60.f, 90.f, 0.f) && at(l.nodes[1].p, 100.f, 110.f, 0.f) && l.nodes[2].kind == HandLot::LN_ENTRY && at(l.nodes[3].p, 100.f, 90.f, 0.f) &&
                       link(l, 0, 0, 3, 6.f, 0, HandLot::LL_AISLE) && link(l, 1, 1, 0, 6.f, 0, HandLot::LL_AISLE) && l.links[1].pts.size() == 3 &&
                       link(l, 2, 3, 1, 6.f, 0, HandLot::LL_AISLE) && link(l, 3, 2, 3, 6.f, 0, HandLot::LL_DRIVE),
                   "lot 3: the loop");
            bool north = true;
            for (const HandLot::Bay& b : l.bays) north = north && b.link == 1 && same(b.s, 100.f - b.c.x, 0.01f);
            EXPECT(north && same(l.bays[0].c.x, 64.f + 32.f / 24.f), "lot 3: the bays open onto the loop's north side");
        }
    }
    // on the graded ground: lot 1 on its parcel's grade (4.20), lot 2 on the terrain (3 m) and 3 m up on its second
    // level; then through world.bin, exactly
    BakedTerrain baked;
    baked.ok = true;
    baked.res = kHeightRes;
    baked.cell = kHeightCell;
    baked.originX = baked.originY = -kWorldHalf;
    baked.heightUnit = 0.01f;
    baked.height.assign((size_t)kHeightRes * kHeightRes, (i16)300);
    baked.water.assign(baked.height.size(), (i16)0);
    std::vector<HandLot> above = hb.lots;   // (the heights before)
    WorldBinWriter w;
    std::string why, report;
    EXPECT(placeOnTerrain(baked, rn, std::vector<std::vector<vec2>>(rn.edges.size()), out, w, &why, &report), "sections: placed (%s)", why.c_str());
    bool lifted = hb.lots.size() == 3 && hb.lots[0].nodes.size() == above[0].nodes.size();
    for (size_t i = 0; lifted && i < hb.lots[0].nodes.size(); i++)
        if (i != 4 && i != 6) lifted = same(hb.lots[0].nodes[i].p.z, 4.2f, 0.011f);
    lifted = lifted && same(hb.lots[0].bays[40].c.z, 4.2f, 0.011f) && same(hb.lots[0].links[1].pts[1].z, 4.2f, 0.011f);
    lifted = lifted && hb.lots[1].nodes.size() == 6 && same(hb.lots[1].nodes[0].p.z, 3.f, 0.011f) && same(hb.lots[1].nodes[3].p.z, 6.f, 0.011f) &&
             same(hb.lots[1].links[2].pts.back().z, 6.f, 0.011f) && same(hb.lots[1].bays[20].c.z, 6.f, 0.011f) && same(hb.lots[2].nodes[1].p.z, 3.f, 0.011f);
    EXPECT(lifted, "sections: the lots on the graded ground");
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    HandBlocks backIn;
    EXPECT(view.open(bin.data(), bin.size(), &why) && view.find("blocks.parcels") && view.find("blocks.lots") && readBlocks(view, backIn, &why, (int)rn.edges.size()),
           "sections: read back (%s)", why.c_str());
    bool same_ = backIn.parcels.size() == hb.parcels.size() && backIn.lots.size() == hb.lots.size();
    for (size_t i = 0; same_ && i < hb.parcels.size(); i++) {
        const HandParcel &a = backIn.parcels[i], &b = hb.parcels[i];
        same_ = a.id == b.id && a.use == b.use && a.frontEdge == b.frontEdge && a.frontSide == b.frontSide &&
                (a.grade == b.grade || (std::isnan(a.grade) && std::isnan(b.grade))) && a.poly.size() == b.poly.size() &&
                memcmp(a.poly.data(), b.poly.data(), a.poly.size() * sizeof(vec2)) == 0;
    }
    for (size_t i = 0; same_ && i < hb.lots.size(); i++) same_ = sameLot(backIn.lots[i], hb.lots[i]);
    EXPECT(same_, "sections: the parcels and lots read back differ");
    // damaged sections, an edge the network doesn't have: refused with a reason, nothing read
    for (const char* name : {"blocks.parcels", "blocks.lots"}) {
        std::vector<u8> payload;
        EXPECT(view.bytes(name, payload) && payload.size() > 40, "sections: %s's bytes", name);
        for (int k = 0; k < 4; k++) {
            std::vector<u8> bad = payload;
            if (k == 0) bad.resize(bad.size() - 5);
            if (k == 1) bad.push_back(0);
            if (k == 2) bad[0] = 9;
            if (k == 3) bad[4] = 0xff;   // (the count)
            WorldBinWriter w2;
            w2.addCompressed(name, bad);
            const std::vector<u8> b2 = w2.finish();
            WorldBinView v2;
            HandBlocks hb2;
            why.clear();
            EXPECT(v2.open(b2.data(), b2.size(), &why) && !readBlocks(v2, hb2, &why) && !why.empty() && hb2.parcels.empty() && hb2.lots.empty(),
                   "sections: damaged %s %d was read", name, k);
        }
    }
    HandBlocks noEdge;
    why.clear();
    EXPECT(!readBlocks(view, noEdge, &why, 0) && !why.empty() && noEdge.parcels.empty() && noEdge.lots.empty() && noEdge.props.empty(),
           "sections: a world.bin whose blocks name an edge its network doesn't have was read");

    // the findings: an entry past its edge's end, a lot without rows, rows of no bays, of one point, on a level the lot
    // hasn't, a ramp to one, nothing on the ground level for the entry, a level no ramp reaches; none of them compiled
    const std::string broken =
        "ntw 1 block id=cl.b002 district=cl\n"
        "lot cl.b002.lot_a entry=cl.e0001:250.00:left\n"
        "  row line=10.00,30.00;30.00,30.00 bays=8\n"
        "end\n"
        "lot cl.b002.lot_b entry=cl.e0001:20.00:left\n"
        "end\n"
        "lot cl.b002.lot_c entry=cl.e0001:20.00:left\n"
        "  row line=10.00,30.00;30.00,30.00 bays=0\n"
        "  row line=10.00,30.00 bays=4\n"
        "  row line=10.00,40.00;30.00,40.00 bays=4 level=2\n"
        "  ramp from=0 to=3 line=10.00,50.00;30.00,50.00\n"
        "end\n"
        "lot cl.b002.lot_d entry=cl.e0001:20.00:left levels=2\n"
        "  row line=10.00,30.00;30.00,30.00 bays=8 level=1\n"
        "end\n"
        "lot cl.b002.lot_e entry=cl.e0001:20.00:left levels=2\n"
        "  row line=10.00,30.00;30.00,30.00 bays=8\n"
        "  row line=10.00,30.00;30.00,30.00 bays=8 level=1\n"
        "end\n";
    Model m2;
    std::vector<Error> e2;
    const bool ok2 = loadTexts({{"roads.ntw", roads}, {"block.ntw", broken}, {"world.ntw", world}}, m2, e2);
    RoadNetwork rn2;
    RoadIndex ix2;
    const bool roads2 = ok2 && compileRoads(m2, rn2, ix2, e2);
    BlocksOut out2;
    const bool found = roads2 && !compileBlockSections(m2, ix2, rn2, out2, e2);
    EXPECT(found && e2.size() == 8 && countCode(e2, "block.lot") == 7 && countCode(e2, "block.shape") == 1 && out2.blocks.lots.empty(), "sections: the findings (%zu)",
           e2.size());
    if (e2.size() != 8) printErrors(m2, e2);
    bool pastAt = false;
    for (const Error& e : e2) pastAt = pastAt || (e.message.find("past the ends of cl.e0001") != std::string::npos && e.src.line == 2);
    EXPECT(pastAt, "sections: the entry past its edge's end, on its line");
}

// The walk graph's inputs and the spots (worldmodel.h compileBlockSections, blocksbin.h): a path on the ground and a
// footbridge on its profile, a plaza, two flights of steps (one up onto a pad, its treads derived; one as written);
// the spots as written (a point with its hours, ages and prop; a queue) and those the props and bus stops imply (a
// bench's seat, a bus shelter's wait for the stop beside it, a stop without one waiting by the kerb); their heights on
// the graded ground, both sections through world.bin, damaged ones refused; and the findings
static void testWalkAndSpots() {
    const std::string world = "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n";
    const std::string catalog =
        "ntw 1 catalog\n"
        "piece lib.prop.bench code=PROP_BENCH size=1.80x0.60x0.90 variants=1 collide=box\n"
        "piece lib.prop.shelter code=PROP_BUS_STOP size=3.40x1.90x2.50 collide=box\n"
        "piece lib.prop.bollard code=PROP_BOLLARD size=0.20x0.20x1.00 collide=cylinder\n";
    const std::string roads =
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=4.00\n"
        "node cl.n0002 at=200.00,0.00 z=4.00\n"
        "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;200.00,0.00\n"
        "  busstop cl.bus.01 at=50.00 side=left\n"
        "  busstop cl.bus.02 at=150.00 side=left wait=150.00,12.00:2.00\n"
        "end\n";
    const std::string block =
        "ntw 1 block id=cl.b001 district=cl\n"
        "parcel cl.b001.p01 poly=20.00,30.00;80.00,30.00;80.00,60.00;20.00,60.00 use=park\n"
        "  pad poly=40.00,40.00;50.00,40.00;50.00,50.00;40.00,50.00 z=0.60 kind=deck\n"
        "end\n"
        "plaza cl.b001.pz1 poly=100.00,30.00;140.00,30.00;140.00,60.00 tags=market,shade\n"
        "path cl.b001.pa1 line=30.00,20.00;30.00,40.00;60.00,40.00 w=2.00 kind=walk\n"
        "path cl.b001.pa2 line=90.00,20.00;90.00,60.00 w=3.00 kind=footbridge profile=0.00:4.00;10.00:9.00;30.00:9.00;40.00:4.00\n"
        "steps line=45.00,35.00;45.00,40.50 w=1.50\n"
        "steps line=60.00,35.00;60.00,38.00 w=1.20 treads=3 rise=0.15\n"
        "prop kind=lib.prop.bench at=30.00,50.00 yaw=90.0\n"
        "prop kind=lib.prop.shelter at=50.00,9.00 yaw=180.0\n"
        "prop kind=lib.prop.bollard at=70.00,50.00\n"
        "spot cl.b001.s1 kind=smoke at=70.50,50.00 yaw=0.0 cap=2 hours=8.00-18.00;22.00-2.00 roles=worker ages=18-65 prop=lib.prop.bollard pose=smoke_lean\n"
        "spot kind=queue line=120.00,40.00;120.00,46.00 for=cl.bus.02\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"lib/catalog.ntw", catalog}, {"roads.ntw", roads}, {"block.ntw", block}, {"world.ntw", world}}, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    if (!ok || !compileRoads(m, rn, ix, errors) || rn.edges.size() != 1) {
        EXPECT(false, "walk and spots: the test files load and their roads compile");
        printErrors(m, errors);
        return;
    }
    RoadOverrides ov;
    compileRoadOverrides(m, ix, rn, ov);
    applyRoadSides(ov, rn);
    BlocksOut out;
    const bool compiled = compileBlocks(m, out, errors) && compileBlockSections(m, ix, rn, out, errors);
    EXPECT(compiled && errors.empty(), "walk and spots: the compile (%zu findings)", errors.size());
    if (!errors.empty()) printErrors(m, errors);
    const HandBlocks& hb = out.blocks;
    EXPECT(hb.paths.size() == 2 && hb.plazas.size() == 1 && hb.steps.size() == 2 && hb.spots.size() == 5 && hb.props.size() == 3 && out.propDress.size() == 3,
           "walk and spots: %zu paths, %zu plazas, %zu flights, %zu spots, %zu props", hb.paths.size(), hb.plazas.size(), hb.steps.size(), hb.spots.size(),
           hb.props.size());
    if (hb.paths.size() != 2 || hb.plazas.size() != 1 || hb.steps.size() != 2 || hb.spots.size() != 5) return;
    const auto at = [](vec3 p, float x, float y, float z) { return same(p.x, x, 0.01f) && same(p.y, y, 0.01f) && same(p.z, z, 0.01f); };
    // the paths: the walk's points as written, on the ground; the footbridge's with its profile's, at their heights
    const HandPath &walk = hb.paths[0], &bridge = hb.paths[1];
    EXPECT(walk.id == "cl.b001.pa1" && walk.kind == WK_WALK && same(walk.w, 2.f) && walk.pts.size() == 3 && at(walk.pts[2], 60.f, 40.f, 0.f) &&
               bridge.kind == WK_FOOTBRIDGE && bridge.pts.size() == 4 && at(bridge.pts[0], 90.f, 20.f, 4.f) && at(bridge.pts[1], 90.f, 30.f, 9.f) &&
               at(bridge.pts[2], 90.f, 50.f, 9.f) && at(bridge.pts[3], 90.f, 60.f, 4.f) && out.pathOnTerrain == std::vector<u8>({1, 0}),
           "walk and spots: the paths");
    EXPECT(hb.plazas[0].id == "cl.b001.pz1" && std::isnan(hb.plazas[0].z) && hb.plazas[0].poly.size() == 3 && hb.plazas[0].tags == std::vector<std::string>({"market", "shade"}),
           "walk and spots: the plaza");
    // the spots as written: the smoker by the bollard (its prop, 0.5 m off), facing north, two of them, by day and
    // late evening; the queue, its head first, facing it, a place every 0.75 m
    const HandSpot &smoke = hb.spots[0], &queue = hb.spots[1], &bench = hb.spots[2], &shelter = hb.spots[3], &kerb = hb.spots[4];
    EXPECT(smoke.kind == SPOT_SMOKE && smoke.id == "cl.b001.s1" && smoke.flags == 0 && at(smoke.at, 70.5f, 50.f, 0.f) && same(smoke.dir.x, 0.f) && same(smoke.dir.y, 1.f) &&
               smoke.cap == 2 && smoke.hours == std::vector<u16>({480, 1080, 1320, 120}) && smoke.roles == std::vector<std::string>({"worker"}) && smoke.ageMin == 18 &&
               smoke.ageMax == 65 && smoke.prop == 2 && smoke.pose == "smoke_lean" && smoke.target.empty(),
           "walk and spots: the spot as written");
    EXPECT(queue.kind == SPOT_QUEUE && queue.line.size() == 2 && at(queue.at, 120.f, 40.f, 0.f) && same(queue.dir.y, -1.f) && queue.cap == 9 && queue.target == "cl.bus.02" &&
               queue.prop == -1 && queue.hours.empty() && queue.ageMin == 0 && queue.ageMax == 255,
           "walk and spots: the queue");
    // implied: the bench's seat (facing west, 0.48 m up, 3 places); the shelter's bench for the stop beside it (0.6 m
    // behind its front, under cover, 4 places); the other stop waiting at its wait=, facing the kerb
    EXPECT(bench.kind == SPOT_BENCH && bench.flags == HandSpot::IMPLIED && at(bench.at, 30.f, 50.f, 0.48f) && same(bench.dir.x, -1.f) && same(bench.dir.y, 0.f) &&
               bench.cap == 3 && bench.prop == 0,
           "walk and spots: the bench's seat");
    EXPECT(shelter.kind == SPOT_BUSSTOP_WAIT && shelter.flags == (HandSpot::IMPLIED | HandSpot::COVER) && at(shelter.at, 50.f, 9.6f, 0.48f) && same(shelter.dir.y, -1.f) &&
               shelter.cap == 4 && shelter.prop == 1 && shelter.target == "cl.bus.01",
           "walk and spots: the shelter's wait");
    EXPECT(kerb.kind == SPOT_BUSSTOP_WAIT && kerb.flags == HandSpot::IMPLIED && at(kerb.at, 150.f, 12.f, 0.f) && same(kerb.dir.y, -1.f) && kerb.cap == 6 &&
               kerb.target == "cl.bus.02" && kerb.prop == -1,
           "walk and spots: the stop without a shelter");
    // on the graded ground (the terrain at 3 m; the pad 0.6 m above it, its deck 0.06 m more): the walk on it, the
    // footbridge where its profile has it; the first flight from the ground up onto the pad, 4 treads of 0.165 m; the
    // second as written (3 of 0.15); the seats by their props' bases, the rest on the ground
    BakedTerrain baked;
    baked.ok = true;
    baked.res = kHeightRes;
    baked.cell = kHeightCell;
    baked.originX = baked.originY = -kWorldHalf;
    baked.heightUnit = 0.01f;
    baked.height.assign((size_t)kHeightRes * kHeightRes, (i16)300);
    baked.water.assign(baked.height.size(), (i16)0);
    WorldBinWriter w;
    std::string why, report;
    EXPECT(placeOnTerrain(baked, rn, std::vector<std::vector<vec2>>(rn.edges.size()), out, w, &why, &report), "walk and spots: placed (%s)", why.c_str());
    const HandSteps &up = hb.steps[0], &written = hb.steps[1];
    EXPECT(same(walk.pts[2].z, 3.f, 0.011f) && same(bridge.pts[1].z, 9.f) && same(up.bottom.z, 3.f, 0.011f) && same(up.top.z, 3.66f, 0.011f) && up.treads == 4 &&
               same(up.rise, 0.165f, 0.003f) && same(written.top.z, written.bottom.z + 0.45f) && written.treads == 3 && same(written.rise, 0.15f),
           "walk and spots: the paths and steps on the ground (%.2f %.2f %d %.3f)", up.bottom.z, up.top.z, up.treads, up.rise);
    EXPECT(same(bench.at.z, hb.props[0].pos.z + 0.48f) && same(bench.at.z, 3.48f, 0.011f) && same(shelter.at.z, hb.props[1].pos.z + 0.48f) &&
               same(smoke.at.z, hb.props[2].pos.z) && same(queue.line[1].z, 3.f, 0.011f) && kerb.at.z > 3.f && kerb.at.z < 3.71f,
           "walk and spots: the spots' heights (the kerb's wait %.2f, on the ground the street's grading lifts toward its 3.7)", kerb.at.z);
    // through world.bin, exactly
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    HandBlocks back;
    EXPECT(view.open(bin.data(), bin.size(), &why) && view.find("blocks.walk") && view.find("blocks.spots") && readBlocks(view, back, &why, (int)rn.edges.size()),
           "walk and spots: read back (%s)", why.c_str());
    const auto v3s = [](const std::vector<vec3>& a, const std::vector<vec3>& b) { return a.size() == b.size() && (a.empty() || memcmp(a.data(), b.data(), a.size() * sizeof(vec3)) == 0); };
    bool same_ = back.paths.size() == 2 && back.plazas.size() == 1 && back.steps.size() == 2 && back.spots.size() == 5;
    for (size_t i = 0; same_ && i < 2; i++)
        same_ = back.paths[i].id == hb.paths[i].id && back.paths[i].kind == hb.paths[i].kind && back.paths[i].w == hb.paths[i].w && v3s(back.paths[i].pts, hb.paths[i].pts);
    same_ = same_ && back.plazas[0].id == hb.plazas[0].id && std::isnan(back.plazas[0].z) && back.plazas[0].tags == hb.plazas[0].tags &&
            back.plazas[0].poly.size() == hb.plazas[0].poly.size();
    for (size_t i = 0; same_ && i < 2; i++)
        same_ = back.steps[i].bottom == hb.steps[i].bottom && back.steps[i].top == hb.steps[i].top && back.steps[i].w == hb.steps[i].w &&
                back.steps[i].treads == hb.steps[i].treads && back.steps[i].rise == hb.steps[i].rise;
    for (size_t i = 0; same_ && i < 5; i++) {
        const HandSpot &a = back.spots[i], &b = hb.spots[i];
        same_ = a.kind == b.kind && a.flags == b.flags && a.id == b.id && a.at == b.at && a.dir.x == b.dir.x && a.dir.y == b.dir.y && v3s(a.line, b.line) && a.cap == b.cap &&
                a.hours == b.hours && a.roles == b.roles && a.ageMin == b.ageMin && a.ageMax == b.ageMax && a.prop == b.prop && a.pose == b.pose && a.target == b.target;
    }
    EXPECT(same_, "walk and spots: the sections read back differ");
    // damaged: refused, nothing read; and a spot naming a prop the blocks don't have
    for (const char* name : {"blocks.walk", "blocks.spots"}) {
        std::vector<u8> payload;
        EXPECT(view.bytes(name, payload) && payload.size() > 40, "walk and spots: %s's bytes", name);
        for (int k = 0; k < 3; k++) {
            std::vector<u8> bad = payload;
            if (k == 0) bad.resize(bad.size() - 3);
            if (k == 1) bad.push_back(7);
            if (k == 2) bad[0] = 2;
            WorldBinWriter w2;
            w2.addCompressed(name, bad);
            const std::vector<u8> b2 = w2.finish();
            WorldBinView v2;
            HandBlocks hb2;
            why.clear();
            EXPECT(v2.open(b2.data(), b2.size(), &why) && !readBlocks(v2, hb2, &why) && !why.empty() && hb2.spots.empty() && hb2.paths.empty(),
                   "walk and spots: damaged %s %d was read", name, k);
        }
    }
    {
        std::vector<u8> spots;
        EXPECT(view.bytes("blocks.spots", spots), "walk and spots: the spots' bytes");
        WorldBinWriter w3;
        w3.addCompressed("blocks.spots", spots);   // (without "blocks": none of the props they name)
        const std::vector<u8> b3 = w3.finish();
        WorldBinView v3;
        HandBlocks hb3;
        why.clear();
        EXPECT(v3.open(b3.data(), b3.size(), &why) && !readBlocks(v3, hb3, &why) && why.find("prop") != std::string::npos && hb3.spots.empty(),
               "walk and spots: spots naming props that aren't there were read (%s)", why.c_str());
    }

    {   // spots as small as they come (an implied seat: no id, line, hours, roles, pose or for), many of them
        HandBlocks few;
        PropInstance pi;
        pi.type = PROP_BENCH;
        few.props.push_back(pi);
        HandSpot seat;
        seat.flags = HandSpot::IMPLIED;
        seat.prop = 0;
        few.spots.assign(100, seat);
        WorldBinWriter w4;
        writeBlocks(few, w4);
        const std::vector<u8> b4 = w4.finish();
        WorldBinView v4;
        HandBlocks back4;
        why.clear();
        EXPECT(v4.open(b4.data(), b4.size(), &why) && readBlocks(v4, back4, &why) && back4.spots.size() == 100 && back4.spots[99].prop == 0,
               "walk and spots: 100 of the smallest spots read back (%s)", why.c_str());
    }

    // the findings
    const std::string broken =
        "ntw 1 block id=cl.b002 district=cl\n"
        "path cl.b002.pa1 line=10.00,10.00;20.00,10.00 w=0.00 kind=walk\n"
        "path cl.b002.pa2 line=10.00,20.00;20.00,20.00 w=2.00 kind=underpass\n"
        "steps line=10.00,30.00 w=1.00\n"
        "spot kind=lean at=10.00,40.00 line=10.00,40.00;10.00,45.00\n"
        "spot kind=lean\n"
        "spot kind=lean at=10.00,50.00 hours=8.00-25.00\n"
        "spot kind=lean at=10.00,55.00 ages=70-20\n"
        "spot kind=lean at=10.00,60.00 prop=lib.prop.bench\n";
    Model m2;
    std::vector<Error> e2;
    const bool ok2 = loadTexts({{"lib/catalog.ntw", catalog}, {"roads.ntw", roads}, {"block.ntw", broken}, {"world.ntw", world}}, m2, e2);
    RoadNetwork rn2;
    RoadIndex ix2;
    const bool roads2 = ok2 && compileRoads(m2, rn2, ix2, e2);
    BlocksOut out2;
    const bool found = roads2 && compileBlocks(m2, out2, e2) && !compileBlockSections(m2, ix2, rn2, out2, e2);
    EXPECT(found && e2.size() == 8 && countCode(e2, "block.shape") == 2 && countCode(e2, "block.path") == 1 && countCode(e2, "block.spot") == 5 &&
               out2.blocks.paths.empty() && out2.blocks.steps.empty() && out2.blocks.spots.size() == 2,
           "walk and spots: the findings (%zu; %zu spots: the two bus stops' waits)", e2.size(), out2.blocks.spots.size());
    if (e2.size() != 8) printErrors(m2, e2);
}

static void testErrors() {
    const std::string roads =
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=1.00 approaches=n-cl.e0002\n"           // 2: ref.form
        "node cl.n0002 at=100.00,0.00 z=1.00\n"
        "node cl.n0001 at=5.00,5.00 z=9.00\n"                                 // 4: twice: the first stays
        "edge cl.e0001 from=cl.n0001 to=cl.n0099 class=street lanes=1/1 shape=0.00,0.00;100.00,0.00\n"   // 5: road.node
        "end\n"
        "edge cl.e0002 from=cl.n0001 to=cl.n0002 class=street lanes=1-1 shape=0.00,0.00;100.00,0.00\n"   // 7: value.form
        "end\n"
        "edge cl.e0003 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;90.00,0.00\n"    // 9: road.shape
        "end\n"
        "edge cl.e0004 from=cl.n0002 to=cl.n0002 class=street lanes=1/1 shape=100.00,0.00;100.00,50.00;100.00,0.00\n"   // 11: road.loop
        "end\n"
        "edge cl.e0005 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;100.00,0.00 profile=0.00:1.00;50.00:1.00\n"   // 13: road.profile
        "end\n"
        "edge cl.e0006 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 median=wide shape=0.00,0.00;100.00,0.00\n"   // 15: value.form
        "end\n"
        "edge cl.e0007 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;0.00,0.00;100.00,0.00\n"   // 17: road.shape
        "end\n"
        "junction cl.n0077\n"                                                  // 19: names nothing: no finding here
        "end\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"roads.ntw", roads}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, m, errors);
    const bool loadFound = !ok && errors.size() == 3 && countCode(errors, "ref.form") == 1 && countCode(errors, "value.form") == 2;
    EXPECT(loadFound, "errors: load found %zu", errors.size());
    if (!loadFound) printErrors(m, errors);
    for (const Error& e : errors) EXPECT(e.src.file == 0 && (e.src.line == 2 || e.src.line == 7 || e.src.line == 15), "errors: %s at line %d", e.code.c_str(), e.src.line);
    EXPECT(m.nodes.size() == 3 && m.find("node", "cl.n0001") == 0 && same(m.nodes[0].at.x, 0.f) && m.nodes[0].approaches.empty(), "errors: the first definition stays");
    EXPECT(m.junctions.size() == 1 && m.junctions[0].node.target == -1 && m.nodes[0].junction == -1 && m.nodes[1].junction == -1, "errors: a junction for no node");
    EXPECT(m.edges.size() == 7 && m.edges[1].lanesF == 1 && m.edges[1].lanesB == 1 && m.edges[5].median < 0.f, "errors: bad values keep the defaults");

    errors.clear();
    RoadNetwork rn;
    RoadIndex ix;
    EXPECT(!compileRoads(m, rn, ix, errors), "errors: the compile passed");
    const bool compileFound = errors.size() == 5 && countCode(errors, "road.node") == 1 && countCode(errors, "road.shape") == 2 &&
                              countCode(errors, "road.loop") == 1 && countCode(errors, "road.profile") == 1;
    EXPECT(compileFound, "errors: the compile found %zu", errors.size());
    if (!compileFound) printErrors(m, errors);
    EXPECT(rn.edges.size() == 2 && ix.edgeOf == std::vector<int>({1, 5}) && ix.roadEdge[0] == -1 && ix.roadEdge[1] == 0 && ix.roadEdge[5] == 1 &&
               rn.nodes.size() == 3 && rn.nodes[0].edges.size() == 2,
           "errors: the edges that compile still do");
    for (const Error& e : errors)
        if (e.code == "road.node") EXPECT(m.where(e.src) == "roads.ntw:5", "errors: road.node at %s", m.where(e.src).c_str());

    // load(): files that don't parse or break the schema keep their places, untyped
    const fs::path dir = fs::temp_directory_path() / ("neontide-worldmodel-test-" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir / "districts" / "cl", ec);
    {
        std::ofstream(dir / "world.ntw") << "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\" label=oops\n";
        std::ofstream(dir / "districts" / "cl" / "roads.ntw") << "ntw 1 roads district=cl\nnode cl.n0001 at=1.00,2.00 z=3.00\nend\n";   // (an end with no block open)
        std::ofstream(dir / "districts" / "cl" / "district.ntw") << "ntw 1 district district=cl\nbounds poly=0.00,0.00;1.00,0.00;1.00,1.00\n";
    }
    Model lm;
    errors.clear();
    EXPECT(!load(dir.generic_string(), lm, errors), "load: passed broken files");
    EXPECT(lm.files.size() == 3 && countCode(errors, "file.parse") >= 1 && countCode(errors, "file.schema") >= 1 && !lm.files[1].valid && !lm.files[2].valid &&
               lm.files[0].valid && lm.nodes.empty() && lm.districts.empty(),
           "load: %zu files, %zu errors", lm.files.size(), errors.size());
    for (const Error& e : errors) EXPECT(e.src.file >= 1 && e.src.line >= 1, "load: %s at %d:%d", e.code.c_str(), e.src.file, e.src.line);
    fs::remove_all(dir, ec);
}

// --------------------------------------------------------------------------------------------- the rooms' light state

static void testRoomLight() {
    // schedule, seed, windows, day, minute, night -> level. The renderer's HLSL copy of roomLight() must give the same:
    // change these only together with it.
    struct Vector {
        u32 schedule, seed, windows, day, minute, night, level;
    };
    static const Vector kVectors[] = {
        { 0, 2407, 1, 2, 1230, 255, 216},
        { 0, 4012, 1, 2,  420, 255, 240},
        { 0, 3024, 0, 2,   90, 255, 231},
        { 0, 1202, 1, 5,  600, 255, 207},
        { 0, 2071, 1, 5, 1410,  80, 200},
        { 0, 3553, 0, 3, 1230,   0, 244},
        { 0, 2835, 1, 0,   30, 255, 211},
        { 0, 1207, 1, 6,  125, 255, 198},
        { 1, 3721, 1, 2, 1230, 255,   0},
        { 1, 2247, 1, 2,  420, 255,   0},
        { 1, 3862, 0, 2,   90, 255,   0},
        { 1,  415, 1, 5,  600, 255,   0},
        { 1,  939, 1, 5, 1410,  80,   0},
        { 1,  255, 0, 3, 1230,   0,   0},
        { 1, 1977, 1, 0,   30, 255,   0},
        { 1, 4047, 1, 6,  125, 255,   0},
        { 2,  660, 1, 2, 1230, 255, 193},
        { 2, 2769, 1, 2,  420, 255, 174},
        { 2, 3383, 0, 2,   90, 255,   0},
        { 2,  253, 1, 5,  600, 255, 186},
        { 2, 3984, 1, 5, 1410,  80,   0},
        { 2, 2541, 0, 3, 1230,   0,   0},
        { 2, 3982, 1, 0,   30, 255,   0},
        { 2,  198, 1, 6,  125, 255,   0},
        // night owls: one on a weeknight at 1:00, one of Saturday night's extra ones (and on a weeknight), out by 3:00
        { 2, 1029, 1, 3,   60, 255, 150},
        { 2, 2030, 1, 6,   60, 255, 196},
        { 2, 2030, 1, 3,   60, 255,   0},
        { 2, 1029, 1, 3,  180, 255,   0},
        { 3, 3481, 1, 2, 1230, 255,   0},
        { 3,  122, 1, 2,  420, 255, 238},
        { 3, 1194, 0, 2,   90, 255,   0},
        { 3, 2957, 1, 5,  600, 255, 237},
        { 3, 3565, 1, 5, 1410,  80,   0},
        { 3,  966, 0, 3, 1230,   0,   0},
        { 3,  790, 1, 0,   30, 255,   0},
        { 3,  244, 1, 6,  125, 255,   0},
        { 4, 2956, 1, 2, 1230, 255,   0},
        { 4,  122, 1, 2,  420, 255,   0},
        { 4,  262, 0, 2,   90, 255,   0},
        { 4, 1209, 1, 5,  600, 255,   0},
        { 4,  501, 1, 5, 1410,  80,   0},
        { 4, 2561, 0, 3, 1230,   0,   0},
        { 4, 1087, 1, 0,   30, 255,   0},
        { 4,  533, 1, 6,  125, 255,   0},
        { 5, 1223, 1, 2, 1230, 255,   0},
        { 5, 3898, 1, 2,  420, 255,   0},
        { 5,  333, 0, 2,   90, 255,   0},
        { 5, 1419, 1, 5,  600, 255,   0},
        { 5,  502, 1, 5, 1410,  80,   0},
        { 5,  987, 0, 3, 1230,   0,   0},
        { 5, 3693, 1, 0,   30, 255,   0},
        { 5, 1951, 1, 6,  125, 255,   0},
        { 6, 1561, 1, 2, 1230, 255, 250},
        { 6, 3505, 1, 2,  420, 255,   0},
        { 6,  750, 0, 2,   90, 255,   0},
        { 6, 3314, 1, 5,  600, 255, 217},
        { 6, 2516, 1, 5, 1410,  80,   0},
        { 6, 2237, 0, 3, 1230,   0, 248},
        { 6, 1111, 1, 0,   30, 255,   0},
        { 6, 2446, 1, 6,  125, 255, 106},
        { 7, 3188, 1, 2, 1230, 255,   0},
        { 7, 2293, 1, 2,  420, 255,   0},
        { 7, 2476, 0, 2,   90, 255,   0},
        { 7, 3399, 1, 5,  600, 255,   0},
        { 7, 2788, 1, 5, 1410,  80,   0},
        { 7,  999, 0, 3, 1230,   0,   0},
        { 7, 2302, 1, 0,   30, 255,   0},
        { 7, 3564, 1, 6,  125, 255,   0},
        { 8, 1695, 1, 2, 1230, 255, 204},
        { 8, 1953, 1, 2,  420, 255,   0},
        { 8, 1263, 0, 2,   90, 255,   0},
        { 8, 2884, 1, 5,  600, 255, 252},
        { 8, 2456, 1, 5, 1410,  80, 238},
        { 8, 2214, 0, 3, 1230,   0, 206},
        { 8, 1774, 1, 0,   30, 255,   0},
        { 8, 3709, 1, 6,  125, 255,   0},
        { 9, 2932, 1, 2, 1230, 255, 200},
        { 9, 3929, 1, 2,  420, 255,   0},
        { 9, 2650, 0, 2,   90, 255, 224},
        { 9, 3780, 1, 5,  600, 255,   0},
        { 9, 1043, 1, 5, 1410,  80, 222},
        { 9, 1308, 0, 3, 1230,   0, 199},
        { 9,   39, 1, 0,   30, 255,   0},
        { 9, 1180, 1, 6,  125, 255, 205},
        {10,   48, 1, 2, 1230, 255,   0},
        {10, 2180, 1, 2,  420, 255, 231},
        {10, 2539, 0, 2,   90, 255,   0},
        {10, 2281, 1, 5,  600, 255,   0},
        {10, 2396, 1, 5, 1410,  80,   0},
        {10, 4036, 0, 3, 1230,   0,   0},
        {10,  662, 1, 0,   30, 255,   0},
        {10,  104, 1, 6,  125, 255,   0},
        {11, 1163, 1, 2, 1230, 255, 214},
        {11,  728, 1, 2,  420, 255, 201},
        {11, 1051, 0, 2,   90, 255, 237},
        {11,  662, 1, 5,  600, 255, 242},
        {11, 2277, 1, 5, 1410,  80,   0},
        {11,  833, 0, 3, 1230,   0, 244},
        {11, 1630, 1, 0,   30, 255, 204},
        {11, 2749, 1, 6,  125, 255, 198},
        {12, 1808, 1, 2, 1230, 255, 231},
        {12,  318, 1, 2,  420, 255,   0},
        {12, 2837, 0, 2,   90, 255,   0},
        {12,  293, 1, 5,  600, 255,   0},
        {12, 3906, 1, 5, 1410,  80,   0},
        {12, 1595, 0, 3, 1230,   0, 212},
        {12, 1213, 1, 0,   30, 255,   0},
        {12,  963, 1, 6,  125, 255,   0},
        // day types: Friday night's extra night owl, gone again on Sunday night; a bar at 3:30 after Friday and after
        // Thursday; an office on a Friday morning; a shop on a Sunday evening (weekend hours)
        { 2, 2030, 1, 5,   60, 255, 196},
        { 2, 2030, 1, 7,   60, 255,   0},
        { 9,  100, 0, 5,  210, 255, 252},
        { 9,  100, 0, 4,  210, 255,   0},
        { 7, 1502, 1, 4,  600,   0, 230},
        { 6, 2501, 1, 6, 1290, 255, 204},
    };
    int bad = 0;
    for (const Vector& v : kVectors) {
        const u32 got = roomLight(v.schedule, v.seed, v.windows, v.day, v.minute, v.night);
        if (got != v.level && bad++ < 5)
            printf("  roomLight(%u, %u, %u, %u, %u, %u) = %u, expected %u\n", v.schedule, v.seed, v.windows, v.day, v.minute, v.night, got, v.level);
    }
    EXPECT(bad == 0, "roomLight: %d of %zu test vectors differ", bad, sizeof(kVectors) / sizeof(kVectors[0]));
    EXPECT(RS_COUNT <= 16, "roomLight: %d classes don't fit the window record's 4 bits", (int)RS_COUNT);
    for (u32 c = 0; c < (u32)RS_COUNT; c++) EXPECT(roomScheduleName(c)[0] != 0, "roomLight: class %u has no name", c);
    EXPECT(roomScheduleName(RS_COUNT)[0] == 0 && roomLight(RS_COUNT, 1u, 0u, 2u, 720u, 255u) == 0u, "roomLight: a class past the table is dark");
    EXPECT(roomMinute(0.f) == 0u && roomMinute(0.01f) == 0u && roomMinute(7.25f) == 435u && roomMinute(23.999f) == 1439u &&
               roomMinute(24.f) == 1439u && roomMinute(-0.5f) == 0u && roomMinute(1e9f) == 1439u && roomMinute(std::nanf("")) == 0u,
           "roomMinute: the minute of the clock");

    // the classes' properties over many rooms
    int always = 0, never = 0, dayHome = 0, darkHome = 0, shopNoon = 0, shopNight = 0, shopNightChanged = 0, homeChanged = 0, lateBar = 0, porch = 0;
    int owlWeek = 0, owlSat = 0, owlFri = 0, owlSun = 0, owlMoved = 0, owlWeekOnly = 0, owlAt3 = 0, barThu = 0, barFri = 0, barSat = 0, barSun = 0;
    const int rooms = 2000;
    for (int r = 0; r < rooms; r++) {
        const u32 seed = roomSeed((u32)r * 7919u + 3u, (u32)r % 5u, (u32)r % 11u);
        for (u32 minute : {0u, 420u, 725u, 1230u, 1439u}) {
            always += roomLight(RS_ALWAYS, seed, 1u, 3u, minute, 0u) > 0u;
            never += roomLight(RS_NEVER, seed, 0u, 3u, minute, 255u) > 0u;
        }
        dayHome += roomLight(RS_HOME_LIVING, seed, 1u, 2u, 1230u, 0u) > 0u;           // windows, full daylight: off
        darkHome += roomLight(RS_HOME_LIVING, seed, 1u, 2u, 1230u, 255u) > 0u;        // 20:30 at night: mostly on
        shopNoon += roomLight(RS_SHOP, seed, 1u, 2u, 720u, 0u) > 0u;                  // shops stay lit in daylight
        const bool n2 = roomLight(RS_SHOP, seed, 1u, 2u, 180u, 255u) > 0u, n3 = roomLight(RS_SHOP, seed, 1u, 3u, 180u, 255u) > 0u;
        shopNight += n2;                                                               // the display lighting at 3:00
        shopNightChanged += n2 != n3;                                                  // ... the same shops every night
        homeChanged += (roomLight(RS_HOME_LIVING, seed, 1u, 2u, 1230u, 255u) > 0u) != (roomLight(RS_HOME_LIVING, seed, 1u, 3u, 1230u, 255u) > 0u);
        lateBar += roomLight(RS_NIGHTLIFE, seed, 0u, 3u, 60u, 255u) > 0u;             // 1:00, from the evening before
        porch += roomLight(RS_PORCH, seed, 1u, 2u, 1290u, 255u) > 0u && roomLight(RS_PORCH, seed, 1u, 2u, 180u, 255u) == 0u;
        // night owls (the living rooms' late lamps): the same rooms every night, a few more on Friday and Saturday nights,
        // all out by 3:00
        const bool wedNight = roomLight(RS_HOME_LIVING, seed, 1u, 3u, 60u, 255u) > 0u;   // Thursday 1:00
        const bool thuNight = roomLight(RS_HOME_LIVING, seed, 1u, 4u, 60u, 255u) > 0u;   // Friday 1:00
        const bool satNight = roomLight(RS_HOME_LIVING, seed, 1u, 6u, 60u, 255u) > 0u;   // Sunday 1:00
        owlWeek += wedNight;
        owlSat += satNight;
        owlFri += roomLight(RS_HOME_LIVING, seed, 1u, 5u, 60u, 255u) > 0u;                // Saturday 1:00
        owlSun += roomLight(RS_HOME_LIVING, seed, 1u, 7u, 60u, 255u) > 0u;                // Monday 1:00
        // the day types: bars at 3:30 after Thursday, Friday, Saturday and Sunday (late on Friday and Saturday nights)
        barThu += roomLight(RS_NIGHTLIFE, seed, 0u, 4u, 210u, 255u) > 0u;
        barFri += roomLight(RS_NIGHTLIFE, seed, 0u, 5u, 210u, 255u) > 0u;
        barSat += roomLight(RS_NIGHTLIFE, seed, 0u, 6u, 210u, 255u) > 0u;
        barSun += roomLight(RS_NIGHTLIFE, seed, 0u, 7u, 210u, 255u) > 0u;
        owlMoved += wedNight != thuNight;
        owlWeekOnly += wedNight && !satNight;
        owlAt3 += roomLight(RS_HOME_LIVING, seed, 1u, 3u, 180u, 255u) > 0u || roomLight(RS_HOME_LIVING, seed, 1u, 6u, 180u, 255u) > 0u;
    }
    EXPECT(always == rooms * 5 && never == 0, "roomLight: always %d of %d, never %d", always, rooms * 5, never);
    EXPECT(dayHome == 0 && darkHome > rooms * 7 / 10, "roomLight: homes at 20:30, by day %d, by night %d of %d", dayHome, darkHome, rooms);
    EXPECT(shopNoon > rooms * 9 / 10 && shopNight > rooms / 4 && shopNight < rooms / 2 && shopNightChanged == 0,
           "roomLight: shops at noon %d, at 3:00 %d (%d changed from one night to the next)", shopNoon, shopNight, shopNightChanged);
    EXPECT(homeChanged > rooms / 10, "roomLight: only %d of %d homes change from one evening to the next", homeChanged, rooms);
    EXPECT(lateBar > rooms * 8 / 10 && porch == rooms, "roomLight: bars at 1:00 %d, porch lights on at 21:30 and off at 3:00 %d of %d", lateBar, porch, rooms);
    EXPECT(owlWeek > rooms * 4 / 100 && owlWeek < rooms * 8 / 100 && owlSat > owlWeek && owlSat < rooms * 11 / 100 && owlFri == owlSat &&
               owlSun == owlWeek,
           "roomLight: living rooms lit at 1:00 on weeknights %d, Friday night %d, Saturday night %d, Sunday night %d of %d", owlWeek, owlFri, owlSat,
           owlSun, rooms);
    EXPECT(barFri > rooms * 7 / 10 && barSat == barFri && barThu < rooms / 4 && barSun == barThu,
           "roomLight: bars lit at 3:30 after Thursday %d, Friday %d, Saturday %d, Sunday %d of %d", barThu, barFri, barSat, barSun, rooms);
    EXPECT(owlMoved == 0 && owlWeekOnly == 0 && owlAt3 == 0, "roomLight: night owls who change from one night to the next %d, lit on weeknights only %d, at 3:00 %d",
           owlMoved, owlWeekOnly, owlAt3);
}

// ---------------------------------------------------------------------------------------- the roads through world.bin

static bool sameNetwork(const RoadNetwork& a, const RoadNetwork& b) {
    if (a.nodes.size() != b.nodes.size() || a.edges.size() != b.edges.size() || a.hashRes != b.hashRes || a.hash != b.hash) return false;
    for (size_t i = 0; i < a.nodes.size(); i++) {
        const RoadNode &x = a.nodes[i], &y = b.nodes[i];
        if (x.p.x != y.p.x || x.p.y != y.p.y || x.z != y.z || x.edges != y.edges || x.control != y.control || x.highway != y.highway || x.radius != y.radius) return false;
    }
    for (size_t i = 0; i < a.edges.size(); i++) {
        const RoadEdge &x = a.edges[i], &y = b.edges[i];
        if (x.n0 != y.n0 || x.n1 != y.n1 || x.cls != y.cls || x.flags != y.flags || x.lanesF != y.lanesF || x.lanesB != y.lanesB || x.halfWidth != y.halfWidth ||
            x.sidewalk != y.sidewalk || x.length != y.length || x.cut0 != y.cut0 || x.cut1 != y.cut1 || x.seed != y.seed || x.name != y.name || x.dist != y.dist ||
            x.pts.size() != y.pts.size())
            return false;
        for (size_t k = 0; k < x.pts.size(); k++)
            if (x.pts[k].x != y.pts[k].x || x.pts[k].y != y.pts[k].y || x.pts[k].z != y.pts[k].z) return false;
    }
    return true;
}

// A world.bin holding only a "roads" section with these bytes
static std::vector<u8> binWithRoads(const std::vector<u8>& payload) {
    WorldBinWriter w;
    w.add("roads", WSK_RAW, payload);
    return w.finish();
}

static void testRoadsSection() {
    Model m;
    std::vector<Error> errors;
    load(kWorld, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    compileRoads(m, rn, ix, errors);
    EXPECT(errors.empty() && rn.edges.size() == 8, "roads section: the test world compiles");
    std::vector<std::string> nodeIds, edgeIds;
    for (int mi : ix.nodeOf) nodeIds.push_back(m.nodes[(size_t)mi].id);
    for (int mi : ix.edgeOf) edgeIds.push_back(m.edges[(size_t)mi].id);

    // written, opened and read back: the same network to the bit, with its ids
    WorldBinWriter w;
    writeRoads(rn, &nodeIds, &edgeIds, w);
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    std::string why;
    EXPECT(view.open(bin.data(), bin.size(), &why) && view.find("roads") && view.find("roads.ids"), "roads section: world.bin opens (%s)", why.c_str());
    RoadNetwork back;
    std::vector<std::string> backNodes, backEdges;
    EXPECT(readRoads(view, back, &backNodes, &backEdges, &why), "roads section: read failed: %s", why.c_str());
    EXPECT(sameNetwork(rn, back), "roads section: the network read back differs");
    EXPECT(backNodes == nodeIds && backEdges == edgeIds && backEdges.size() == 8 && backEdges[7] == "tr.e00001", "roads section: the ids read back differ");
    float s = 0.f, d = 0.f;
    EXPECT(back.nearestEdge(vec2(300.f, 152.f), 10.f, &s, &d) == 0 && same(d, 2.f, 0.01f), "roads section: the hash works after reading");

    // without ids, and an empty network
    WorldBinWriter w2;
    writeRoads(RoadNetwork(), nullptr, nullptr, w2);
    const std::vector<u8> bin2 = w2.finish();
    WorldBinView view2;
    RoadNetwork empty;
    EXPECT(view2.open(bin2.data(), bin2.size(), &why) && !view2.find("roads.ids") && readRoads(view2, empty, &backNodes, &backEdges, &why) && empty.nodes.empty() &&
               empty.edges.empty() && backNodes.empty() && empty.hashRes > 0,
           "roads section: an empty network");

    // damaged sections are refused with a reason, never read half
    std::vector<u8> payload;
    EXPECT(view.bytes("roads", payload) && payload.size() > 100, "roads section: its bytes");
    struct Damage {
        const char* what;
        std::vector<u8> bytes;
    };
    std::vector<Damage> damages;
    damages.push_back({"truncated", std::vector<u8>(payload.begin(), payload.end() - 7)});
    damages.push_back({"one byte too many", payload});
    damages.back().bytes.push_back(0);
    damages.push_back({"another version", payload});
    damages.back().bytes[0] = 2;
    damages.push_back({"an edge naming node 1000", payload});
    const size_t firstEdge = 4 + 4 + rn.nodes.size() * 20 + 4;   // version, node count, the nodes, edge count
    damages.back().bytes[firstEdge] = 0xE8;                          // n0 = 1000 (0x3E8)
    damages.back().bytes[firstEdge + 1] = 0x03;
    damages.push_back({"empty", std::vector<u8>()});
    for (const Damage& dmg : damages) {
        const std::vector<u8> b = binWithRoads(dmg.bytes);
        WorldBinView v;
        RoadNetwork bad;
        why.clear();
        EXPECT(v.open(b.data(), b.size(), &why) && !readRoads(v, bad, nullptr, nullptr, &why) && !why.empty() && bad.edges.empty(), "roads section: %s was read", dmg.what);
    }
    WorldBinWriter w3;
    w3.add("terrain", WSK_RAW, std::vector<u8>(8, 0));
    const std::vector<u8> bin3 = w3.finish();
    WorldBinView view3;
    RoadNetwork none;
    EXPECT(view3.open(bin3.data(), bin3.size(), &why) && !readRoads(view3, none, nullptr, nullptr, &why), "roads section: a world.bin without one");
}

// What the roads author beyond the network (roadoverrides.h): each edge's widths and speed as written, else its
// class's, in step with the network's widths; through world.bin exactly; damaged sections refused; the host loader
static bool sameExtra(const RoadOverrides::EdgeExtra& a, const RoadOverrides::EdgeExtra& b) {
    return a.speed == b.speed && a.laneWidth == b.laneWidth && a.median == b.median && a.kerbL == b.kerbL && a.kerbR == b.kerbR && a.parkingL == b.parkingL &&
           a.parkingR == b.parkingR && a.sidewalkL == b.sidewalkL && a.sidewalkR == b.sidewalkR && a.flags == b.flags;
}

static bool sameRule(const RoadOverrides::NodeRule& a, const RoadOverrides::NodeRule& b) {
    return a.node == b.node && a.control == b.control && a.flags == b.flags && a.bulb == b.bulb && a.stops == b.stops && a.yields == b.yields &&
           a.noRedTurn == b.noRedTurn;
}

static void testRoadOverrides() {
    Model m;
    std::vector<Error> errors;
    load(kWorld, m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    compileRoads(m, rn, ix, errors);
    RoadOverrides ov;
    compileRoadOverrides(m, ix, rn, ov);
    EXPECT(errors.empty() && rn.edges.size() == 8 && ov.edges.size() == 8, "overrides: %zu edges for the test world's %zu", ov.edges.size(), rn.edges.size());
    if (ov.edges.size() != rn.edges.size()) return;
    // every edge: the network's symmetric widths are the wider side's
    bool inStep = true;
    for (size_t i = 0; i < rn.edges.size(); i++) {
        const RoadEdge& e = rn.edges[i];
        const RoadOverrides::EdgeExtra& x = ov.edges[i];
        inStep = inStep &&
                 same(e.halfWidth, Max(roadKerbLine(x.laneWidth, x.median, e.lanesF, e.lanesB, x.kerbL, -1), roadKerbLine(x.laneWidth, x.median, e.lanesF, e.lanesB, x.kerbR, 1))) &&
                 same(e.sidewalk, Max(x.sidewalkL, x.sidewalkR)) && x.speed > 0.f && x.laneWidth > 0.f;
    }
    EXPECT(inStep, "overrides: the edges' widths and the network's disagree");
    // each side's own (applyRoadSides): the kerb lines where the lane graph lays the lanes out, the wider side's exactly
    // the network's; and nothing without overrides
    RoadNetwork sided = rn;
    applyRoadSides(ov, sided);
    bool sides = true;
    for (size_t i = 0; i < sided.edges.size(); i++) {
        const RoadEdge& e = sided.edges[i];
        const RoadOverrides::EdgeExtra& x = ov.edges[i];
        for (int sd = -1; sd <= 1; sd += 2)
            sides = sides && same(e.halfWidthSide(sd), roadKerbLine(x.laneWidth, x.median, e.lanesF, e.lanesB, sd > 0 ? x.kerbR : x.kerbL, sd), 1e-4f) &&
                    same(e.sidewalkSide(sd), sd > 0 ? x.sidewalkR : x.sidewalkL, 1e-4f) && e.halfWidthSide(sd) <= e.halfWidth && e.sidewalkSide(sd) <= e.sidewalk;
        sides = sides && (e.halfWidthSide(-1) == e.halfWidth || e.halfWidthSide(1) == e.halfWidth) && (e.sidewalkSide(-1) == e.sidewalk || e.sidewalkSide(1) == e.sidewalk);
    }
    RoadNetwork plain = rn;
    applyRoadSides(RoadOverrides(), plain);
    for (const RoadEdge& e : plain.edges) sides = sides && e.kerbTrim[0] == 0.f && e.kerbTrim[1] == 0.f && e.walkTrim[0] == 0.f && e.walkTrim[1] == 0.f;
    EXPECT(sides, "overrides: each side's kerb line and sidewalk");
    {   // the procedural world's edges have no sides of their own: each is the network's, to the bit
        RoadEdge e;
        e.halfWidth = 5.3f;
        e.sidewalk = 3.1f;
        EXPECT(e.halfWidthSide(-1) == e.halfWidth && e.halfWidthSide(1) == e.halfWidth && e.sidewalkSide(-1) == e.sidewalk && e.sidewalkSide(1) == e.sidewalk,
               "overrides: an edge without sides of its own");
    }
    const auto extra = [&](const char* id) -> const RoadOverrides::EdgeExtra* {
        const int mi = m.find("edge", id);
        const int ri = mi >= 0 ? ix.roadEdge[(size_t)mi] : -1;
        return ri >= 0 ? &ov.edges[(size_t)ri] : nullptr;
    };
    const RoadClassInfo& street = roadInfo(RC_STREET);
    const RoadOverrides::EdgeExtra *e2 = extra("cl.e0002"), *e3 = extra("cl.e0003"), *e4 = extra("cl.e0004"), *sb3 = extra("sb.e0003"), *tr = extra("tr.e00001");
    EXPECT(e2 && same(e2->speed, 11.f) && same(e2->laneWidth, street.laneWidth) && same(e2->median, street.median) && same(e2->kerbL, 2.4f) && same(e2->kerbR, 2.4f) &&
               same(e2->parkingL, 2.4f) && same(e2->parkingR, 2.4f) && same(e2->sidewalkL, street.sidewalk) && e2->flags == 0,
           "overrides: cl.e0002 (parking both sides, its own speed)");
    EXPECT(e3 && same(e3->median, 1.5f) && e3->flags == RoadOverrides::EX_MEDIAN_BARRIER && same(e3->parkingL, -1.f) && same(e3->parkingR, -1.f) &&
               same(e3->kerbL, street.shoulder) && same(e3->speed, street.speed),
           "overrides: cl.e0003 (a median with its barrier; the class's kerbs)");
    EXPECT(e4 && same(e4->sidewalkL, 1.8f) && same(e4->sidewalkR, 0.f) && sb3 && same(sb3->sidewalkL, 0.f) && same(sb3->sidewalkR, 0.f) && tr && same(tr->median, 4.f) &&
               tr->flags == RoadOverrides::EX_MEDIAN_BARRIER,
           "overrides: one sidewalk, none, and the causeway's median");
    // a one-way edge's lanes are centred on its shape, so its kerb lines are too; its sidewalk is on its left only
    const int ri4 = m.find("edge", "cl.e0004") >= 0 ? ix.roadEdge[(size_t)m.find("edge", "cl.e0004")] : -1;
    const RoadEdge* lane4 = ri4 >= 0 ? &sided.edges[(size_t)ri4] : nullptr;
    const RoadClassInfo& laneCls = roadInfo(RC_LANE);
    EXPECT(lane4 && lane4->lanesF == 1 && lane4->lanesB == 0 && same(lane4->halfWidthSide(-1), laneCls.laneWidth * 0.5f + laneCls.shoulder) &&
               same(lane4->halfWidthSide(1), lane4->halfWidthSide(-1)) && same(lane4->sidewalkSide(-1), 1.8f) && same(lane4->sidewalkSide(1), 0.f),
           "overrides: a one-way lane's sides (centred kerb lines, a sidewalk on its left)");
    // the parking along each side: as written; where nothing is, the whole kerb strip when it is 1.8 m or more
    EXPECT(e2 && same(e2->parkingOn(-1), 2.4f) && same(e2->parkingOn(1), 2.4f) && e3 && same(e3->parkingOn(-1), street.shoulder) && same(e3->parkingOn(1), street.shoulder) &&
               e4 && same(e4->parkingOn(-1), 0.f) && same(e4->parkingOn(1), 0.f) && laneCls.shoulder < 1.8f && street.shoulder >= 1.8f,
           "overrides: the parking along each side");
    // the nodes that write a rule: cl.n0001's signal, cl.n0003's bulb, sb.n0001's yield, in network order
    const auto roadNodeOf = [&](const char* id) { const int mi = m.find("node", id); return mi >= 0 ? ix.roadNode[(size_t)mi] : -1; };
    const auto roadEdgeOf = [&](const char* id) { const int mi = m.find("edge", id); return mi >= 0 ? ix.roadEdge[(size_t)mi] : -1; };
    bool sorted = true;
    for (size_t i = 1; i < ov.nodes.size(); i++) sorted = sorted && ov.nodes[i - 1].node < ov.nodes[i].node;
    const RoadOverrides::NodeRule* sig = nullptr;
    const RoadOverrides::NodeRule* bulb = nullptr;
    const RoadOverrides::NodeRule* yld = nullptr;
    for (const RoadOverrides::NodeRule& r : ov.nodes) {
        if (r.node == roadNodeOf("cl.n0001")) sig = &r;
        if (r.node == roadNodeOf("cl.n0003")) bulb = &r;
        if (r.node == roadNodeOf("sb.n0001")) yld = &r;
    }
    EXPECT(ov.nodes.size() == 3 && sorted && sig && sig->control == RoadOverrides::CTRL_SIGNAL && sig->flags == 0 && sig->stops.empty() && bulb &&
               bulb->control == RoadOverrides::CTRL_AUTO && same(bulb->bulb, 8.f) && yld && yld->control == RoadOverrides::CTRL_YIELD &&
               yld->yields == std::vector<int>({roadEdgeOf("sb.e0003")}),
           "overrides: %zu node rules (a signal, a bulb, a yield)", ov.nodes.size());
    {   // a written turning circle is the dead end's bulb (RoadNetwork::bulbRadius: the mesher, the grading and the lane
        // graph read that one number), the surface queries reaching its rim; without one, the class's; and a bulb
        // written on a node that isn't a dead end changes nothing
        const int dead = roadNodeOf("cl.n0003"), cross4 = roadNodeOf("cl.n0001");
        const bool deadEnd = dead >= 0 && rn.nodes[(size_t)dead].edges.size() == 1;
        const RoadEdge* de = deadEnd ? &rn.edges[(size_t)rn.nodes[(size_t)dead].edges[0]] : nullptr;
        RoadNetwork classed = rn, written = rn, wide = rn;
        applyRoadSides(RoadOverrides(), classed);
        applyRoadSides(ov, written);
        RoadOverrides wideOv = ov;
        for (RoadOverrides::NodeRule& r : wideOv.nodes)
            if (r.node == dead) r.bulb = 18.f;
        RoadOverrides::NodeRule onCross;
        onCross.node = cross4;
        onCross.bulb = 12.f;
        wideOv.nodes.push_back(onCross);
        applyRoadSides(wideOv, wide);
        EXPECT(de && cross4 >= 0 && same(classed.bulbRadius(classed.nodes[(size_t)dead]), de->halfWidth + 4.5f) &&
                   same(written.bulbRadius(written.nodes[(size_t)dead]), 8.f) && same(wide.bulbRadius(wide.nodes[(size_t)dead]), 18.f) &&
                   wide.bulbRadius(wide.nodes[(size_t)cross4]) == 0.f,
               "overrides: a dead end's bulb, written (8 m, 18 m) or the class's");
        if (de) {
            // 9 m out beyond the dead end (the road runs east from it): the class's bulb paves it, the written 8 m one has
            // its sidewalk ring there; 20 m out only the 18 m one's ring reaches
            const vec2 n0 = rn.nodes[(size_t)dead].p, out9 = n0 + vec2(-9.f, 0.f), out20 = n0 + vec2(-20.f, 0.f);
            float zc = 0.f, zw = 0.f, zb = 0.f, z20 = 0.f;
            const bool c9 = classed.roadSurfaceHeight(out9, &zc), w9 = written.roadSurfaceHeight(out9, &zw), b20 = wide.roadSurfaceHeight(out20, &zb),
                       c20 = classed.roadSurfaceHeight(out20, &z20);
            EXPECT(c9 && w9 && same(zw - zc, 0.15f, 1e-3f) && b20 && !c20 && de->sidewalk > 2.f,
                   "overrides: the surface round a written bulb (9 m: class %d %.2f, written %d %.2f; 20 m: 18 m bulb %d, class %d)", (int)c9, zc, (int)w9, zw,
                   (int)b20, (int)c20);
            // the ring is open across the street's mouth, where the street's carriageway runs on into the disc, and the
            // street's own sidewalks stop where they meet the ring: inside the disc it is all road (as roadmesh draws it)
            const float hw = de->halfWidth, mid = 8.f + de->sidewalk * 0.5f;
            float zIn = 0.f, zMouth = 0.f, zStrip = 0.f, zRing = 0.f;
            const bool in = written.roadSurfaceHeight(n0 + vec2(2.f, 0.f), &zIn), mo = written.roadSurfaceHeight(n0 + vec2(mid, 0.f), &zMouth),
                       st = written.roadSurfaceHeight(n0 + vec2(1.f, hw + 0.5f), &zStrip), ri = written.roadSurfaceHeight(n0 + vec2(0.f, mid), &zRing);
            EXPECT(in && mo && st && ri && hw + 0.5f < 7.9f && same(zMouth, zIn, 0.05f) && same(zStrip, zIn, 0.05f) && same(zRing - zIn, 0.15f, 0.05f),
                   "overrides: a bulb's mouth and its street's sidewalks (road %.2f; mouth %+.2f, sidewalk inside the disc %+.2f, ring %+.2f)", zIn,
                   zMouth - zIn, zStrip - zIn, zRing - zIn);
        }
    }
    {   // a lane width, a shoulder without parking, and a tunnel
        const std::string roads =
            "ntw 1 roads district=cl\n"
            "node cl.n0001 at=0.00,0.00 z=4.00\n"
            "node cl.n0002 at=0.00,100.00 z=4.00\n"
            "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=avenue lanes=2/1 lanewidth=3.10 shoulder=right:1.00 flags=tunnel\n"
            "  shape=0.00,0.00;0.00,100.00\n"
            "end\n";
        Model t;
        std::vector<Error> terr;
        const bool ok = loadTexts({{"roads.ntw", roads}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}}, t, terr);
        RoadNetwork tn;
        RoadIndex tix;
        RoadOverrides tov;
        const bool compiled = ok && compileRoads(t, tn, tix, terr) && tn.edges.size() == 1;
        if (compiled) compileRoadOverrides(t, tix, tn, tov);
        if (!compiled) printErrors(t, terr);
        const RoadOverrides::EdgeExtra* x = tov.edges.empty() ? nullptr : &tov.edges[0];
        // two-way, 2/1: two lanes right of the shape and one left, each side's kerb beyond its own lanes; the network's
        // half width is the right side's
        const float med = roadInfo(RC_AVENUE).median * 0.5f;
        EXPECT(compiled && x && same(x->laneWidth, 3.1f) && same(x->kerbL, 0.f) && same(x->kerbR, 1.f) && same(x->parkingL, 0.f) && same(x->parkingR, 0.f) &&
                   x->flags == RoadOverrides::EX_TUNNEL && same(x->speed, roadInfo(RC_AVENUE).speed) && same(tn.edges[0].halfWidth, 3.1f * 2.f + med + 1.f),
               "overrides: a lane width, a shoulder on one side, a tunnel");
        if (compiled && x) {
            applyRoadSides(tov, tn);
            EXPECT(same(tn.edges[0].halfWidthSide(-1), 3.1f + med) && same(tn.edges[0].halfWidthSide(1), 3.1f * 2.f + med + 1.f) &&
                       tn.edges[0].halfWidthSide(1) == tn.edges[0].halfWidth && same(x->parkingOn(-1), 0.f) && same(x->parkingOn(1), 0.f),
                   "overrides: a 2/1 edge's sides (%.2f, %.2f), and no parking beside a shoulder= alone", tn.edges[0].halfWidthSide(-1), tn.edges[0].halfWidthSide(1));
            // the network's queries take the side a point is on (the edge runs north, so its right is east): the right
            // kerb at 7.2 m and its sidewalk to 10.7 m; the left kerb at 3.1 m and its sidewalk to 6.6 m
            tn.buildHash();
            const auto surface = [&](float x, float* z) { return tn.roadSurfaceHeight(vec2(x, 50.f), z, 100.f); };
            float zr = 0.f, zrw = 0.f, zl = 0.f, zlw = 0.f, zout = 0.f;
            const bool right = surface(6.9f, &zr), rightWalk = surface(7.7f, &zrw), beyondRight = surface(11.2f, &zout), left = surface(-2.9f, &zl),
                       leftWalk = surface(-3.6f, &zlw), beyondLeft = surface(-6.9f, &zout);
            EXPECT(right && same(zr, 4.f) && rightWalk && same(zrw, 4.15f) && !beyondRight && left && same(zl, 4.f) && leftWalk && same(zlw, 4.15f) && !beyondLeft,
                   "overrides: the surface on each side (%d %.2f, %d %.2f, %d; %d %.2f, %d %.2f, %d)", right, zr, rightWalk, zrw, beyondRight, left, zl, leftWalk,
                   zlw, beyondLeft);
            EXPECT(tn.onPavement(vec2(6.9f, 50.f), 4.f, 0.f, -1, 1.f) && !tn.onPavement(vec2(-3.6f, 50.f), 4.f, 0.f, -1, 1.f) && tn.nearRoad(vec2(10.5f, 50.f), 0.f) &&
                       !tn.nearRoad(vec2(-6.9f, 50.f), 0.f),
                   "overrides: the pavement and the road's reach on each side");
        }
    }

    // through world.bin, to the bit
    std::vector<std::string> nodeIds, edgeIds;
    for (int mi : ix.nodeOf) nodeIds.push_back(m.nodes[(size_t)mi].id);
    for (int mi : ix.edgeOf) edgeIds.push_back(m.edges[(size_t)mi].id);
    WorldBinWriter w;
    writeRoads(rn, &nodeIds, &edgeIds, w);
    writeRoadOverrides(ov, w);
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    std::string why;
    RoadNetwork backNet;
    RoadOverrides back;
    EXPECT(view.open(bin.data(), bin.size(), &why) && view.find("roads.overrides") && readRoads(view, backNet, nullptr, nullptr, &why) &&
               readRoadOverrides(view, backNet, back, &why),
           "overrides: read back (%s)", why.c_str());
    bool same_ = back.edges.size() == ov.edges.size() && back.nodes.size() == ov.nodes.size();
    for (size_t i = 0; same_ && i < ov.edges.size(); i++) same_ = sameExtra(back.edges[i], ov.edges[i]);
    for (size_t i = 0; same_ && i < ov.nodes.size(); i++) same_ = sameRule(back.nodes[i], ov.nodes[i]);
    EXPECT(same_, "overrides: the section read back differs");

    // none: empty; another network, damaged, truncated, over-long or another version: refused with a reason, nothing read
    WorldBinWriter w0;
    writeRoads(rn, nullptr, nullptr, w0);
    writeRoadOverrides(RoadOverrides(), w0);
    const std::vector<u8> bin0 = w0.finish();
    WorldBinView v0;
    RoadOverrides none;
    none.edges.resize(3);
    EXPECT(v0.open(bin0.data(), bin0.size(), &why) && !v0.find("roads.overrides") && readRoadOverrides(v0, rn, none, &why) && none.empty(),
           "overrides: a world.bin without them");
    RoadOverrides other;
    RoadNetwork shorter = rn;
    shorter.edges.pop_back();
    why.clear();
    EXPECT(!readRoadOverrides(view, shorter, other, &why) && !why.empty() && other.empty(), "overrides: read for another network");
    size_t expect = 8 + 40 * ov.edges.size() + 4;
    for (const RoadOverrides::NodeRule& r : ov.nodes) expect += 24 + 4 * (r.stops.size() + r.yields.size() + r.noRedTurn.size());
    std::vector<u8> payload;
    EXPECT(view.bytes("roads.overrides", payload) && payload.size() == expect, "overrides: the section's bytes (%zu, not %zu)", payload.size(), expect);
    for (int k = 0; k < 3; k++) {
        std::vector<u8> bad = payload;
        if (k == 0) bad.resize(bad.size() - 3);
        if (k == 1) bad.push_back(0);
        if (k == 2) bad[0] = 9;
        WorldBinWriter wb;
        wb.addCompressed("roads.overrides", bad);
        const std::vector<u8> b = wb.finish();
        WorldBinView vb;
        RoadOverrides hurt;
        why.clear();
        EXPECT(vb.open(b.data(), b.size(), &why) && !readRoadOverrides(vb, rn, hurt, &why) && !why.empty() && hurt.empty(),
               "overrides: damaged section %d was read", k);
    }

    // the host loader: a world.bin file read back whole, with its ids; a missing file and one that isn't a world.bin
    const std::string path = (std::filesystem::temp_directory_path() / "neontide_worldmodel_test_roads.bin").string();
    {
        std::ofstream f(path, std::ios::binary);
        f.write((const char*)bin.data(), (std::streamsize)bin.size());
    }
    RoadNetwork fileNet;
    RoadOverrides fileOv;
    std::vector<std::string> fileNodes, fileEdges;
    why.clear();
    const bool fileRead = loadRoadsFile(path.c_str(), fileNet, fileOv, &why, &fileNodes, &fileEdges);
    bool fileSides = fileNet.edges.size() == sided.edges.size();   // (with each side's widths applied)
    for (size_t i = 0; fileSides && i < sided.edges.size(); i++)
        for (int k = 0; k < 2; k++)
            fileSides = fileSides && fileNet.edges[i].kerbTrim[k] == sided.edges[i].kerbTrim[k] && fileNet.edges[i].walkTrim[k] == sided.edges[i].walkTrim[k];
    EXPECT(fileRead && sameNetwork(fileNet, rn) && fileSides && fileOv.edges.size() == ov.edges.size() && fileEdges == edgeIds && fileNodes == nodeIds,
           "overrides: loadRoadsFile (%s)", why.c_str());
    {
        std::ofstream f(path, std::ios::binary);
        f << "not a world.bin";
    }
    why.clear();
    EXPECT(!loadRoadsFile(path.c_str(), fileNet, fileOv, &why) && !why.empty() && fileNet.edges.empty() && fileOv.empty(), "overrides: a file that isn't a world.bin");
    std::filesystem::remove(path);
    why.clear();
    EXPECT(!loadRoadsFile(path.c_str(), fileNet, fileOv, &why) && !why.empty(), "overrides: a missing file");
}

// A node's legs (bearings, sectors, approaches=), the turns between them (a connect record's word winning), signal plans
// resolved at a cross and at a T, and the nodes' rules: control, stops, yields, no-red-turn approaches and bulbs
static void testLegsAndPlans() {
    const std::string roads =
        "ntw 1 roads district=cl\n"
        "node cl.n0001 at=0.00,0.00 z=3.00 control=signal plan=lib.signal.cross4\n"
        "node cl.n0002 at=0.00,100.00 z=3.00 control=yield yield=cl.e0001\n"
        "node cl.n0003 at=100.00,0.00 z=3.00 control=signal plan=lib.signal.cross4 noredturn=cl.e0005,cl.e0002\n"
        "node cl.n0004 at=0.00,-100.00 z=3.00 bulb=9.50\n"
        "node cl.n0005 at=-100.00,0.00 z=3.00 control=stop stops=cl.e0004\n"
        "node cl.n0006 at=100.00,100.00 z=3.00\n"
        "node cl.n0007 at=100.00,-100.00 z=3.00\n"
        "node cl.n0008 at=-100.00,-100.00 z=3.00 approaches=e:cl.e0007,x:cl.e0008,e:cl.e0008\n"
        "node cl.n0009 at=-50.00,-14.00 z=3.00\n"
        "node cl.n0010 at=-100.00,-200.00 z=3.00\n"
        "edge cl.e0001 from=cl.n0001 to=cl.n0002 class=street lanes=1/1 shape=0.00,0.00;0.00,100.00\n"
        "end\n"
        "edge cl.e0002 from=cl.n0001 to=cl.n0003 class=street lanes=1/1 shape=0.00,0.00;100.00,0.00\n"
        "end\n"
        "edge cl.e0003 from=cl.n0001 to=cl.n0004 class=street lanes=1/1 shape=0.00,0.00;0.00,-100.00\n"
        "end\n"
        "edge cl.e0004 from=cl.n0001 to=cl.n0005 class=street lanes=1/1 shape=0.00,0.00;-100.00,0.00\n"
        "end\n"
        "edge cl.e0005 from=cl.n0003 to=cl.n0006 class=street lanes=1/1 shape=100.00,0.00;100.00,100.00\n"
        "end\n"
        "edge cl.e0006 from=cl.n0003 to=cl.n0007 class=street lanes=1/1 shape=100.00,0.00;100.00,-100.00\n"
        "end\n"
        "edge cl.e0007 from=cl.n0008 to=cl.n0009 class=street lanes=1/1 shape=-100.00,-100.00;-50.00,-14.00\n"
        "end\n"
        "edge cl.e0008 from=cl.n0008 to=cl.n0010 class=street lanes=1/1 shape=-100.00,-100.00;-100.00,-200.00\n"
        "end\n"
        "edge cl.e0009 from=cl.n0005 to=cl.n0008 class=street lanes=1/1 shape=-100.00,0.00;-100.00,-100.00\n"
        "end\n"
        "junction cl.n0001\n"
        "  connect from=cl.e0002:0 to=cl.e0001:0 turn=left\n"
        "end\n";
    const std::string signal =
        "ntw 1 signal id=lib.signal.cross4\n"
        "plan cycle=60.0 legs=4\n"
        "phase p1 green=24.0 amber=4.0 allred=2.0 moves=n_s,n_l,n_r,s_s,s_lp walk=e,w walktime=7.0 flash=10.0\n"
        "phase p2 green=24.0 amber=4.0 allred=2.0 moves=e_s,e_l,w_s,w_r walk=n,s walktime=7.0 flash=10.0\n";
    Model m;
    std::vector<Error> errors;
    const bool ok = loadTexts({{"roads.ntw", roads}, {"lib/signals/cross4.ntw", signal}, {"world.ntw", "ntw 1 world\nworld palmera name=\"P\"\ndistrict cl name=\"C\"\n"}},
                              m, errors);
    RoadNetwork rn;
    RoadIndex ix;
    const bool compiled = ok && compileRoads(m, rn, ix, errors) && rn.edges.size() == 9 && m.signals.size() == 1;
    EXPECT(compiled, "legs: the test roads compile");
    if (!compiled) {
        printErrors(m, errors);
        return;
    }
    const auto E = [&](const char* id) { const int mi = m.find("edge", id); return mi >= 0 ? ix.roadEdge[(size_t)mi] : -1; };
    const auto N = [&](const char* id) { return m.find("node", id); };
    const auto letterOf = [](const NodeLegs& L, int road) {
        for (const NodeLeg& g : L.legs)
            if (g.road == road) return g.letter;
        return '?';
    };
    // the cross: one leg to a sector; the T: no east; approaches= gives the skewed leg (bearing 30, sector n) its e,
    // skips x, and keeps e's first use
    const NodeLegs cross = nodeLegs(m, ix, rn, N("cl.n0001")), tee = nodeLegs(m, ix, rn, N("cl.n0003")), skew = nodeLegs(m, ix, rn, N("cl.n0008"));
    EXPECT(cross.legs.size() == 4 && letterOf(cross, E("cl.e0001")) == 'n' && letterOf(cross, E("cl.e0002")) == 'e' && letterOf(cross, E("cl.e0003")) == 's' &&
               letterOf(cross, E("cl.e0004")) == 'w' && tee.legs.size() == 3 && letterOf(tee, E("cl.e0002")) == 'w' && letterOf(tee, E("cl.e0005")) == 'n' &&
               letterOf(tee, E("cl.e0006")) == 's',
           "legs: a cross and a T by their bearings");
    const NodeLeg* skewed = nullptr;
    for (const NodeLeg& g : skew.legs)
        if (g.road == E("cl.e0007")) skewed = &g;
    EXPECT(skewed && skewed->compass == 'n' && skewed->letter == 'e' && same(skewed->bearing, 30.2f, 0.5f) && letterOf(skew, E("cl.e0008")) == 's' &&
               letterOf(skew, E("cl.e0009")) == 'n',
           "legs: approaches= names the skewed leg (bearing %.1f)", skewed ? skewed->bearing : -1.f);
    // turns: the lane graph's classes, and the connect record's word where it joins two edges
    const NodeLeg *cn = nullptr, *ce = nullptr, *cs = nullptr, *cw = nullptr;
    for (const NodeLeg& g : cross.legs) {
        if (g.letter == 'n') cn = &g;
        if (g.letter == 'e') ce = &g;
        if (g.letter == 's') cs = &g;
        if (g.letter == 'w') cw = &g;
    }
    EXPECT(cn && ce && cs && cw && legTurn(m, cross, *cn, *cs) == TURN_STRAIGHT && legTurn(m, cross, *cn, *ce) == TURN_LEFT && legTurn(m, cross, *cn, *cw) == TURN_RIGHT &&
               legTurn(m, cross, *ce, *cn) == TURN_LEFT && legTurn(m, cross, *ce, *cs) == TURN_LEFT && legTurn(m, cross, *cw, *cs) == TURN_RIGHT,
           "legs: the turns (e into n is a right, which the connect record makes a left)");
    // the plan at the cross: everything resolves
    ResolvedPlan rp;
    resolvePlan(m, cross, m.signals[0], rp);
    const auto has = [](const ResolvedPlan::Phase& ph, int from, int to, u8 turn, bool prot) {
        for (const ResolvedPlan::Move& x : ph.moves)
            if (x.from == from && x.to == to && x.turn == turn && x.isProtected == prot) return true;
        return false;
    };
    EXPECT(rp.phases.size() == 2 && rp.unused.empty() && rp.phases[0].moves.size() == 5 && rp.phases[1].moves.size() == 5 &&
               has(rp.phases[0], E("cl.e0001"), E("cl.e0003"), TURN_STRAIGHT, false) && has(rp.phases[0], E("cl.e0001"), E("cl.e0002"), TURN_LEFT, false) &&
               has(rp.phases[0], E("cl.e0003"), E("cl.e0004"), TURN_LEFT, true) && has(rp.phases[1], E("cl.e0002"), E("cl.e0001"), TURN_LEFT, false) &&
               has(rp.phases[1], E("cl.e0002"), E("cl.e0003"), TURN_LEFT, false) && has(rp.phases[1], E("cl.e0004"), E("cl.e0003"), TURN_RIGHT, false) &&
               rp.phases[0].walks == std::vector<int>({E("cl.e0002"), E("cl.e0004")}) && rp.phases[1].walks == std::vector<int>({E("cl.e0001"), E("cl.e0003")}),
           "legs: the plan at the cross (%zu unused)", rp.unused.size());
    // and at the T: what names the missing east leg, or a turn the T hasn't, is unused
    ResolvedPlan rt;
    resolvePlan(m, tee, m.signals[0], rt);
    EXPECT(rt.phases.size() == 2 && rt.unused == std::vector<std::string>({"n_l", "walk e", "e_s", "e_l", "w_s"}) && rt.phases[0].moves.size() == 4 &&
               rt.phases[1].moves.size() == 1 && has(rt.phases[1], E("cl.e0002"), E("cl.e0006"), TURN_RIGHT, false) &&
               has(rt.phases[0], E("cl.e0006"), E("cl.e0002"), TURN_LEFT, true),
           "legs: the plan at the T (%zu unused)", rt.unused.size());

    // the nodes' rules, as written, in network order
    RoadOverrides ov;
    compileRoadOverrides(m, ix, rn, ov);
    const auto rule = [&](const char* id) -> const RoadOverrides::NodeRule* {
        const int rni = ix.roadNode[(size_t)N(id)];
        for (const RoadOverrides::NodeRule& r : ov.nodes)
            if (r.node == rni) return &r;
        return nullptr;
    };
    const RoadOverrides::NodeRule *r1 = rule("cl.n0001"), *r2 = rule("cl.n0002"), *r3 = rule("cl.n0003"), *r4 = rule("cl.n0004"), *r5 = rule("cl.n0005");
    std::vector<int> nored = {E("cl.e0005"), E("cl.e0002")};
    std::sort(nored.begin(), nored.end());
    EXPECT(ov.nodes.size() == 5 && r1 && r1->control == RoadOverrides::CTRL_SIGNAL && r2 && r2->control == RoadOverrides::CTRL_YIELD &&
               r2->yields == std::vector<int>({E("cl.e0001")}) && r3 && r3->noRedTurn == nored && r4 && r4->control == RoadOverrides::CTRL_AUTO &&
               same(r4->bulb, 9.5f) && r5 && r5->control == RoadOverrides::CTRL_STOP && r5->flags == 0 && r5->stops == std::vector<int>({E("cl.e0004")}) &&
               !rule("cl.n0006") && !rule("cl.n0008"),
           "legs: %zu node rules", ov.nodes.size());
    // through world.bin, and a rule naming a node that isn't there
    WorldBinWriter w;
    writeRoads(rn, nullptr, nullptr, w);
    writeRoadOverrides(ov, w);
    const std::vector<u8> bin = w.finish();
    WorldBinView view;
    std::string why;
    RoadOverrides back;
    bool same_ = view.open(bin.data(), bin.size(), &why) && readRoadOverrides(view, rn, back, &why) && back.nodes.size() == ov.nodes.size();
    for (size_t i = 0; same_ && i < ov.nodes.size(); i++) same_ = sameRule(back.nodes[i], ov.nodes[i]);
    EXPECT(same_, "legs: the node rules read back differ (%s)", why.c_str());
    RoadNetwork fewer = rn;
    fewer.nodes.resize(2);
    why.clear();
    EXPECT(!readRoadOverrides(view, fewer, back, &why) && !why.empty() && back.empty(), "legs: node rules read for a network without their nodes");
}

int main() {
    testDiscover();
    testSamples();
    testWorld();
    testCompile();
    testProfilePoints();
    testGrade();
    testDeckEnds();
    testBlocks();
    testGradeOnly();
    testBlockSections();
    testWalkAndSpots();
    testErrors();
    testRoadsSection();
    testRoadOverrides();
    testLegsAndPlans();
    testRoomLight();
    printf(gFails ? "worldmodel_test: %d FAILURES\n" : "worldmodel_test: all passed\n", gFails);
    return gFails ? 1 : 0;
}
