#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/buildings.cpp"
#include <thread>
#include <map>
int main(int argc, char** argv) {
    Jobs::init(4);
    World::WorldMap m; m.generate();
    World::RoadNetwork roads; roads.generate(m);
    std::map<std::string, std::pair<int,float>> st;
    for (auto& e : roads.edges) {
        const char* want[] = {"Port", "Terminal", "Airport", "Departures", "Arrivals", "Perimeter", "Sol Expressway", "Coral", "Tarpon", "Ocean Drive", "Bayview", "Mill", "Tower Road", "Yacht", "Sea Grape"};
        for (auto w : want) if (e.name.find(w) != std::string::npos) { st[e.name].first++; st[e.name].second += e.length; }
    }
    for (auto& kv : st) printf("%-28s edges %3d  len %7.1f\n", kv.first.c_str(), kv.second.first, kv.second.second);
    for (auto& r : World::gSites->roads) printf("site road %-24s pts %zu first (%.0f,%.0f) last (%.0f,%.0f)\n", r.name.c_str(), r.pts.size(), r.pts.front().x, r.pts.front().y, r.pts.back().x, r.pts.back().y);
    Jobs::shutdown();
}
