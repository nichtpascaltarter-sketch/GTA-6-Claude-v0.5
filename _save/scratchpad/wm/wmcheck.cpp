// wmcheck: loads a world directory with the world model and compiles its roads; prints the counts and every finding
// (city agent's scratch tool, not for the repo). Build from the tree root:
//   g++ -std=c++17 -O1 -I. wmcheck.cpp -o /tmp/city/wmcheck
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include "src/world/roadnet.cpp"
#include "src/world/worldmodel.cpp"
#include "src/world/worldmodel_roads.cpp"

using namespace World;
using namespace World::Hand;

int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        Model m;
        std::vector<Error> errors;
        const double t0 = TimeSeconds();
        load(argv[a], m, errors);
        const double t1 = TimeSeconds();
        RoadNetwork rn;
        RoadIndex ix;
        compileRoads(m, rn, ix, errors);
        const double t2 = TimeSeconds();
        size_t refs = 0, resolved = 0;
        auto count = [&](const Ref& r) {
            if (r.text.empty()) return;
            refs++;
            resolved += r.target >= 0;
        };
        for (const Node& n : m.nodes) {
            count(n.plan);
            for (const auto& x : n.approaches) count(x.second);
            for (const Ref& r : n.stops) count(r);
            for (const Ref& r : n.yields) count(r);
        }
        for (const Edge& e : m.edges) {
            count(e.from);
            count(e.to);
            for (const CurbCut& c : e.curbCuts) count(c.to);
        }
        for (const BusStop& b : m.busStops) {
            count(b.shelter);
            for (const Ref& r : b.routes) count(r);
        }
        int lights = 0;
        for (const RoadNode& n : rn.nodes) lights += n.control == 2;
        printf("%s: %zu files, %zu ids; %zu districts, %zu nodes, %zu edges, %zu junctions, %zu bus stops, %zu signal plans, %zu lines, %zu places, "
               "%zu anchors, %zu blocks, %zu parcels, %zu lots, %zu buildings, %zu library items; road refs %zu/%zu resolved; compiled %zu nodes, %zu "
               "edges, %d signalised (load %.3f s, compile %.3f s)\n",
               argv[a], m.files.size(), m.ids.size(), m.districts.size(), m.nodes.size(), m.edges.size(), m.junctions.size(), m.busStops.size(),
               m.signals.size(), m.transit.size(), m.places.size(), m.anchors.size(), m.blocks.size(), m.parcels.size(), m.lots.size(), m.buildings.size(),
               m.library.size(), resolved, refs, rn.nodes.size(), rn.edges.size(), lights, t1 - t0, t2 - t1);
        for (const Error& e : errors) printf("  %s: %s [%s]\n", m.where(e.src).c_str(), e.message.c_str(), e.code.c_str());
        for (size_t i = 0; i < rn.nodes.size(); i++)
            if (rn.nodes[i].edges.size() >= 3)
                printf("  junction %s: %zu legs, radius %.2f, control %d\n", m.nodes[(size_t)ix.nodeOf[i]].id.c_str(), rn.nodes[i].edges.size(), rn.nodes[i].radius,
                       rn.nodes[i].control);
        bad += !errors.empty();
    }
    return bad ? 1 : 0;
}
