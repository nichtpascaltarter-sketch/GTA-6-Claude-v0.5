## 12. Road graph and navigation requirements (AI agent)

The AI agent's requirements, folded in on 2026-10-19. One rule runs through them: **derive by default, author the
overrides.** The compiler and `LaneGraph::build` derive everything they derive today from the authored roads, paths and
buildings. A record is written only where a derivation would be wrong, or where nothing can derive the fact. The records
below are the ones docs/NTW_FORMAT.md freezes at v1, and src/world/ntwschema.cpp checks them.

### 12.1 What the AI reads today, and what replaces it

| Reader | Reads today | Reads in the hand world |
|---|---|---|
| `LaneGraph::build` (lanes.cpp) | `RoadNetwork` nodes (z, incident edges, control, highway flag, clearing radius) and edges (centreline with z, class and its `roadInfo`, flags, lanes per direction, half width, sidewalk width, cut-backs, name); dead-end bulbs; building and site colliders, which turn curves and sidewalk corners keep clear of; `SiteSet::walks` | the same `RoadNetwork`, compiled from the roads (4.4), and the block files' `path` records (4.5). Its derivations stay the defaults: lanes, turn connectors with conflicts and yield priority, signal plans, stop lines, crosswalks with ped signals, bus stops and benches, the walk graph and zebras |
| traffic_core.cpp | lanes, connectors, conflicts and signals; A* over directed edges | unchanged |
| traffic.cpp | implicit kerbside parking (the rightmost lane wherever the parking strip is 1.8 m or more, except road works and 22 m round a bus stop); roof heights for the helicopter; water and terrain | `parkbay` and `nostop` records; building tops from the compiled buildings; the terrain bake |
| pednav.cpp | the walk graph (sidewalk, crosswalk, corner, path and zebra links, walkable half width); the walk signal per approach | unchanged, built as today |
| pedai.cpp, ai.cpp | districts (`regionAt`: roles, behaviours, nightlife, turf, local lines); shop windows from the building style; street doors at the facade middle; the jaywalking rule; benches and bus stops, the only outdoor scenario points; avoidance of primitive colliders | zones (12.4), frontage and doors (12.3, 12.5), spots (12.4) and prop colliders |
| police.cpp | lanes (dispatch, roadblocks, pull-overs), edge lengths, building corners (the search plan), water, the walk graph (beats, foot chases) | the same graphs, plus police anchors (12.4) |
| events.cpp | all 15 kinds search the lane and walk graphs for a place out of sight | unchanged, on the new graphs. Authored event anchors come on top |
| barks.cpp | districts | zones |
| population.cpp, transit.cpp, interiors_game.cpp (other owners) | districts, site elements and venues, how lived-in a street is, the coast, lanes, the walk graph; transit stops registered with the AI (64 bus stops, 240 waiting spots, 16 streetcar stops with kerb lanes kept clear); NPCs pinned at `InteriorScenario` points | zones, block spots and venues, `places.ntw`, `transit/*.ntw`, layout spots (4.8) |

### 12.2 Roads beyond the baseline

The baseline: nodes and edges with class, lanes per direction, lane width, medians, shoulders, parking, sidewalks and
speed; junction control; turn bans and lane connections; signalled and zebra crossings; bus stops, parking bays and curb
cuts; grade separation. Beyond it (4.4 shows them in a file):

1. **Lanes that change along an edge.** `lanes` gives one direction's count over a stretch, with the side that gains or
   loses the lane and the taper: turn pockets, merges, acceleration and deceleration lanes. `lane ... use=bus` keeps a
   lane for buses, streetcars or bikes. `noswitch` marks solid lines.
2. **Movements per lane.** `moves at=n1 lanes=l,s,sr` gives each lane's turns, from the left. The connectors are still
   generated from lane end to lane end, and `connect` overrides them for odd geometry.
3. **Priority without signals.** By road class, as today. Authored: `control=yield yield=<edges>`, `control=stop` with
   `stops=<edges>` for a two-way stop (all approaches without it), and `junction ... kind=roundabout` (yield at entry,
   circulating ring lanes).
4. **The junction box.** `stopline edge=... at=...` per approach, or the box polygon. The compiler turns either into
   today's cut-backs, and lanes start and end at the box, for the conflict test and "don't block the box".
5. **Signal plans** (kind `signal`, lib/signals). Phases by movement and by crossing, with green, amber and all-red,
   protected or permissive turns, and walk and flash times. A movement is the approach leg by compass and the turn
   (`n_l`; `n_lp` is a protected left); the compiler names the legs by bearing, and the node's `approaches` fixes skewed
   ones. `plan=auto`, the default, keeps today's planner. Signal heads are derived from the stop lines, and `head`
   overrides them.
6. **Highways.** Ramps are edges (`class=ramp`) with lane adds and drops. `shoulder=both:3.00` for breakdowns and
   police stops. `median=4.00:barrier`: no U-turns, no crossing on foot. Grade separation from `profile` and `deck`.
7. **Kerbside.** `parkbay` (parallel, angle or perpendicular, metered, disabled), `nostop` with its reason, `loading`
   with hours, `taxirank` with a capacity. Hydrants imply 5 m of nostop and curb cuts imply it across them. Police and
   cabs pull over on any kerb lane outside a nostop, with no record.
8. **Bus and streetcar stops.** `busstop` adds the waiting area (`wait=`), its `routes` and, for streetcars, the kerb
   lane kept clear (`clear=`).
9. **Off-street parking.** A `lot` block in the block file: entered by a curb cut, with bay `row`s and their aisles,
   `ramp`s between levels and a level per row.
10. **Dead ends.** `bulb=<radius>` on the node, or none, in which case long vehicles avoid it.
11. **z everywhere.** Every lane, connector and stop takes its z from the edge `profile` and `deck`. Lanes on a deck
    don't conflict with the lanes under it.

### 12.3 Sidewalks, crossings, jaywalking, frontage

1. **The walk graph** is derived from the sidewalks plus `path` records (7.5). Block files add `plaza` areas, where
   people mill about off the paths, and through-block passages (`path ... kind=alley`).
2. **Kerb side and building side.** Each sidewalk link keeps both, as the derived graph does today: panhandlers sit
   against the building side, shop windows face it, and people step back toward it from a car.
3. **Crossings** add `kind=unmarked` (no paint: people cross in gaps), and footbridges and underpasses as paths with a
   profile. Which phase gives a signal crossing its walk is derived from its approach, as today.
4. **Jaywalking** keeps today's rule (narrow, slow, at least 12 m from a junction) on the authored widths and classes.
   `nojaywalk` marks fences and guard rails, and a median barrier implies it.
5. **Obstacles** (bins, lamps, planters, hydrants, newspaper boxes, signal poles, benches). Each catalog piece carries a
   simple collider (`collide=box|cylinder`), and walkers steer round them as today. The sidewalk's clear width is
   derived: its width less the furniture strip the placed props take.
6. **Frontage** is derived per placement by the compiler: windows with their unit's kind from the fit-out (bakery, bar,
   clothes), entrances with their public hours, and café terraces, kiosks, ATMs, vending machines and phone booths as
   spots.

### 12.4 Spots, zones and anchors

1. **Spots** are scenario points, outdoors in block files and indoors in layouts (4.5, 4.8): a kind, a position and yaw
   or a queue line, capacity, hours, shelter (`cover`, used in rain), who may use them (`roles`, `ages`), the prop and
   the pose. A queue's `for` names the door or counter it leads to. The kinds are listed in docs/NTW_FORMAT.md 9.5.
   Props imply the obvious ones (a bench prop is a bench spot), and the population's venues (3,955 laid out from site
   elements today) become spots and venues per block.
2. **Zones** replace `World::Region` for the AI: tagged polygons in the district file for the region (roles, density
   tables, local lines, the event mix, patrol density), gang turf, use (nightlife, beach, business, industrial,
   residential), and pedestrian and traffic density by hour and day. The audio and wildlife zones of 7.7 are the same
   set.
3. **Police anchors** in places.ntw: stations with their bays, garage exit, booking door and release point; K9 and SWAT
   bases; patrol loops, foot beats and break spots; helipads and boat launches; roadblock sites, which are derived at
   chokepoints as today, and authored where those read badly. Patrol density comes from police zones.
4. **Event anchors.** Every event kind keeps finding its own places on the graphs: a kerb or shoulder stretch of 12 m for
   crashes, breakdowns and traffic stops; two lanes or a 12 m/s road with a kilometre ahead for racers; a 4-way crossing
   of two ordinary streets with lights or all-way stops for takeovers; a sidewalk or plaza point for the street events;
   a 30 m sidewalk run for a shootout; a building-side wall for the panhandler; alleys, yards and car-park corners for
   the "wanted" kinds. `anchor kind=event` adds a place by hand.
5. **Emergency services:** ambulance bays and ER doors, fire stations and tow yards, as anchors.

### 12.5 Interiors (beyond 4.8, 7.5 and 7.6)

1. **Levels.** Every walkable surface has a level: outside, or a building and floor. The compiler assigns them
   (sidewalks, plazas, decks, garage levels, building floors), so none lacks one. A ped's level is then known without ray
   casts: the AI agent adds the level filter to its proximity queries (`pedsNear`, `vehiclesNear`, walk-graph lookups),
   and height queries answer per level.
2. **Stairs** are off-mesh links with rise, run and tread count (`stair ... treads=16 rise=0.18`; outdoor `steps` the
   same). Walkers climb a tread per step (about 0.5 m/s up and 0.55 m/s down) once the animator's slow-pace fix lands.
3. **Doors** carry their kind for the clip (hinged, pair, sliding, revolving, roll-up), lock rules with public hours,
   and a runtime state (locked, open, broken). Exterior doors join the walk graph (7.5), so today's street-door logic
   uses the real doors.
4. **Lifts** are off-mesh links with a car capacity, a call point and the stops. NPCs call, wait, ride and get out on
   their floor.
5. **Rooms** have a type and a capacity. Indoor spots take the outdoor kinds plus desk, counter service and queue,
   browse, bed, sofa, kitchen, washroom, bar and dance.
6. **Police and crime indoors.** Entry points are the exterior doors; exits and roof routes follow the doors, stairs and
   fire entrances; each shop's `counter` and back room (`room type=store`) serve hold-ups. The room-clearing order is
   derived from the door graph, and cover from the furniture, with `spot kind=cover` where that falls short.
7. **Occupancy** (7.6) also exposes the next transitions (who leaves for work at 8, who comes home at 6), so doors let
   people out and in at the right times, and per-building "in" and "out" counts for the crowds at entrances. Both are
   runtime calls, not data.

### 12.6 Validation

The validator (9.1) checks the AI's data:
- lanes that lead nowhere, and approaches with no movement out;
- moves that don't match the lane count;
- signal plans that never give a movement or a crossing its green;
- crossings with no walk phase;
- a bus stop on a lane with no kerb;
- parking bays overlapping a nostop or a curb cut;
- walk links that end in walls, and sidewalks with no crossing for over 250 m;
- spots inside geometry or on a lane;
- doors that join one room only;
- stairs and lifts that miss a floor;
- walkable surfaces without a level.

It ends with a summary like today's load log (`Lane graph: 25496 lanes, 56760 connectors, 40687 walk nodes, 56441 walk
links`), plus the count of authored overrides by record type, so a district's coverage shows at a glance.

### 12.7 What is derived, deferred or runtime

Nothing the AI asked for is dropped. These asks are met other than by a record:

| Ask | How it is met |
|---|---|
| Lot bays as `bays=row:...` on one line | a `lot` block with `row` and `ramp` records: the same data, one fact per line |
| Multi-level garages | in the format (levels, ramps). The pilot builds surface lots; garage interiors with ramps follow it |
| `moves dir=` | implied by `at`: the lanes arriving at `n1` are the `fwd` ones |
| Signal heads | derived from the stop lines; `head` where they don't suit |
| Signal movement names | `<approach>_<turn>`, compass approaches by bearing, `approaches=` for skewed junctions |
| Kerb side and building side of sidewalk links | derived, as today |
| Sidewalk clear width | derived from the furniture strip the props take |
| Frontage (windows, entrances, terraces) | derived from fit-outs, doors and spots |
| Zones in the pilot | the AI reads the baked region grid until zones are authored; density profiles are optional and default to today's tables |
| Event anchors and roadblock sites | derived, as today; anchors only as extras |
| Spot wardrobe filters | after the pilot (`roles` and `ages` are in v1) |
| Cover points and room-clearing order | derived from the furniture and the door graph; `spot kind=cover` overrides |
| Level ids | assigned by the compiler, never written |
| Door state, occupancy transitions, in and out counts | runtime calls (7.6), not data |
| A navmesh outdoors | not planned: outdoors stays the walk graph, and props are colliders |

