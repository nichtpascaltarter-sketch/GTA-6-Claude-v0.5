// Hand-built places: the Ocean Promenade hotel row (decostrip.cpp), Porto Sol University (campus.cpp), Santa Marea
// Cemetery and the town churchyards (cemetery.cpp), the hospitals (hospital.cpp), the state prison and the speedway
// (outskirts.cpp). Each place reserves its land and shapes the terrain in the site layout pass (before the roads), asks
// the building system for the buildings it owns (SiteBuildingReq::siteElem: the place builds their meshes), and adds the
// site elements that stream with the cells. Places in towns and suburbs are fitted to the finished road network instead
// (placesAfterLots). The .cpp files are included by sitecell.cpp, so every build of the site cells (game, worldcheck,
// traffic_sim) has them.
#pragma once
#include "sites.h"
#include "buildings.h"

namespace World {

// Site layout pass (SiteSet::layout, before the roads): reserve land, flatten, pads, buildings and elements
void placesLayout(SiteSet& S, WorldMap& map);
// After the lots are placed (BuildingSet::generate, before the per-cell lists): places fitted to the real streets
// (churchyards, hospitals) clear the generic buildings on their ground and add their own
void placesAfterLots(WorldMap& map, const RoadNetwork& roads, BuildingSet& bs);
// Facade records of the buildings the places build themselves (SiteSet::makeFacades): the element learns its building
void placesFacades(SiteSet& S, BuildingSet& bs);
// Final pass (SiteSet::finalize, before the per-cell element lists): details that need the roads (beach crossings)
void placesFinalize(SiteSet& S, WorldMap& map, const RoadNetwork& roads, const BuildingSet& bs);

}  // namespace World
