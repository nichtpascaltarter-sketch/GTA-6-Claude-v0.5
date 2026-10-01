// Entry points of the hand-built places (places.h): the site layout, the pass after the lots, the facade records and
// the final pass call into each place in turn. Included by sitecell.cpp after the place generators.
#include "places.h"

namespace World {

void placesLayout(SiteSet& S, WorldMap& map) {
    double t0 = TimeSeconds();
    S.places.clear();
    S.anchors.clear();
    deco_strip::layout(S, map);
    campus::layout(S, map);
    cemetery::layout(S, map);
    LOG("Places: layout %.3f s, %zu places, %zu anchors", TimeSeconds() - t0, S.places.size(), S.anchors.size());
}

void placesAfterLots(WorldMap& map, const RoadNetwork& roads, std::vector<Building>& buildings, std::vector<FacadeGPU>& facades) {
    (void)facades;
    if (!gSites) return;
    double t0 = TimeSeconds();
    churchyard::place(*gSites, map, roads, buildings);
    LOG("Places: after the lots %.3f s", TimeSeconds() - t0);
}

void placesFacades(SiteSet& S, BuildingSet& bs) {
    deco_strip::facades(S, bs);
    campus::facades(S, bs);
}

void placesFinalize(SiteSet& S, WorldMap& map, const RoadNetwork& roads, const BuildingSet& bs) {
    (void)S;
    (void)map;
    (void)roads;
    (void)bs;
}

}  // namespace World
