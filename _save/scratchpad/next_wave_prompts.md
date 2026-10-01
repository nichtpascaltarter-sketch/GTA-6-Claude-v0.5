# Next agent wave (launch when CPU frees up)

## Interiors agent
Seamless enterable interiors (GTA V style) for gameplay-relevant buildings: safehouses (Mari's apartment in Calle
Luna, Dex's place in the Flats), Mama Lucha's diner, a weapon shop, a convenience store, a clothes store, the
nightclub on Sol Beach, police station lobby, hospital lobby, a garage/chop shop, Sandoval's penthouse office
(Solaris One top floors) and a warehouse. Requirements: building shells become hollow where interiors exist (collision
as wall boxes with door openings), doors (swinging or automatic), interior meshes with furniture props, lighting
(local lights; renderer "interior" flag so sky/sun ambient is suppressed inside and window light comes in), windows
showing the outside, NPC scenarios inside (clerks, patrons), gameplay hooks for shops (story agent's shop UI),
mission use. Files: new src/world/interiors.cpp (+ buildings.cpp flags), renderer tweak for interior ambient.

## Wildlife + transit agent
Birds (seagull/pelican flocks at the coast, herons/egrets in the Sawgrass, pigeons downtown), alligators in the
Sawgrass (dangerous when approached), dolphins/fish near boats, dogs with owners; an elevated metro line (Porto Sol
"SkyLine") with stations, trains running on a schedule that the player can ride; trams on the beach promenade.

## Phone + photo mode (UI agent follow-up)
Phone: contacts (call story contacts to start missions/hang out), messages (mission texts), camera app with
filters (photo mode with free camera, DOF, time freeze), map, quick save, a social feed ("Tidegram") where NPCs
post about the player's recent actions (car chase at X, explosion at Y) using generated text.

## World agent follow-up: street-level building detail + vegetation
Geometric facade detail for all building styles at LOD0 (window recesses with frames/sills/lintels, mullions on
storefront glazing, cornices/parapet caps/string courses, pilasters, balconies with railings, fire escapes on midrise
brick, AC units and pipes, rooftop water tanks, awnings with scalloped edges, neon signs with tubes, graffiti decals in
Calle Luna/the Flats, shutters, security gates, stucco bands on deco), plus better palms/trees/bushes (more leaves,
trunk detail, seasonal color variation, flowering bougainvillea on walls, hedges along lots, lawns with sprinklers).
Keep LOD1 cheap. Measure per-cell generation time and triangle counts.

## Transit agent (queued; launch when capacity allows)
Elevated metro "SkyLine" through downtown/Midtown/airport (viaduct + stations with stairs/escalators/platforms,
announcements), trains on a timetable (doors, dwell), player can board/ride (fast travel feel) and fight on them;
beach promenade tram; city buses on routes with stops (bus stop props, peds waiting/boarding); ferries between
Port Isle/Sol Beach/Keys. Files: src/world/transit.cpp (geometry), src/game/transit_game.cpp (vehicles, schedule,
boarding), coordinate road crossings with the AI agent (lane graph level crossings / signals).

## Vehicle customization (queued)
Mod shop ("Tide Customs"): paint (primary/secondary/pearl/matte/chrome), wheels (rim designs), body kits
(spoilers/bumpers/skirts from the vehicle models agent), performance (engine/brakes/suspension/turbo via
VehicleTuning), window tint (MAT_CAR_WINDOW clarity), neon underglow, horns, liveries. Models agent: visual variants;
story agent: shop UI/economy; vehicle sim agent: tuning stats.
