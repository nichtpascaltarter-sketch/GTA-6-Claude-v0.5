// Tests for the world model (src/world/worldmodel.h):
// - discovery, and the loader on the format's samples (tests/world/fixtures/samples, each kind typed);
// - a two-district world (tests/world/fixtures/model, worldv-clean): the tables, their order, the references;
// - the findings on broken input (codes and lines), and files that don't parse or break the schema;
// - the roads compile: widths, heights, flags, junction radii, cut-backs and control, seeds, the spatial hash, and
//   the points a profile adds; the terrain graded under the roads (src/world/roadgrade.cpp), decks left alone;
// - the blocks: their records typed, the compile (ground, dressing, lamps, findings), the blocks section through
//   world.bin (src/world/blocksbin.h), a parcel's levelling (WorldMap::flattenPoly), the level ground that people and
//   vehicles stand on (HandBlocks::padHeight), and the grading written for a parcel that only grades the terrain
//   (Hand::placeOnTerrain);
// - the roads through world.bin (src/world/roadsbin.h): back exactly as written, and damaged sections refused; with
//   what they author beyond the network (src/world/roadoverrides.h), and the host loader (loadRoadsFile);
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
    }
    EXPECT(always == rooms * 5 && never == 0, "roomLight: always %d of %d, never %d", always, rooms * 5, never);
    EXPECT(dayHome == 0 && darkHome > rooms * 7 / 10, "roomLight: homes at 20:30, by day %d, by night %d of %d", dayHome, darkHome, rooms);
    EXPECT(shopNoon > rooms * 9 / 10 && shopNight > rooms / 4 && shopNight < rooms / 2 && shopNightChanged == 0,
           "roomLight: shops at noon %d, at 3:00 %d (%d changed from one night to the next)", shopNoon, shopNight, shopNightChanged);
    EXPECT(homeChanged > rooms / 10, "roomLight: only %d of %d homes change from one evening to the next", homeChanged, rooms);
    EXPECT(lateBar > rooms * 8 / 10 && porch == rooms, "roomLight: bars at 1:00 %d, porch lights on at 21:30 and off at 3:00 %d of %d", lateBar, porch, rooms);
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
        inStep = inStep && same(e.halfWidth, x.laneWidth * (float)(e.lanesF + e.lanesB) * 0.5f + x.median * 0.5f + Max(x.kerbL, x.kerbR)) &&
                 same(e.sidewalk, Max(x.sidewalkL, x.sidewalkR)) && x.speed > 0.f && x.laneWidth > 0.f;
    }
    EXPECT(inStep, "overrides: the edges' widths and the network's disagree");
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
        EXPECT(compiled && x && same(x->laneWidth, 3.1f) && same(x->kerbL, 0.f) && same(x->kerbR, 1.f) && same(x->parkingL, 0.f) && same(x->parkingR, 0.f) &&
                   x->flags == RoadOverrides::EX_TUNNEL && same(x->speed, roadInfo(RC_AVENUE).speed) && same(tn.edges[0].halfWidth, 3.1f * 1.5f + roadInfo(RC_AVENUE).median * 0.5f + 1.f),
               "overrides: a lane width, a shoulder on one side, a tunnel");
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
    EXPECT(loadRoadsFile(path.c_str(), fileNet, fileOv, &why, &fileNodes, &fileEdges) && sameNetwork(fileNet, rn) && fileOv.edges.size() == ov.edges.size() &&
               fileEdges == edgeIds && fileNodes == nodeIds,
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
    testErrors();
    testRoadsSection();
    testRoadOverrides();
    testLegsAndPlans();
    testRoomLight();
    printf(gFails ? "worldmodel_test: %d FAILURES\n" : "worldmodel_test: all passed\n", gFails);
    return gFails ? 1 : 0;
}
