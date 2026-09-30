// City buses: the Porto Sol Transit routes laid out by world/transit.cpp. Every bus runs on a virtual timetable along
// its route; within ~330 m of the player it becomes a real Civitas Boulevard bus (route livery and destination sign)
// driven by the traffic core in DM_ROUTE along the route's own chain of road edges (the host sets Driver::destEdges),
// stopping at each stop (stopPath/stopU, then DM_HOLD) where the ambient AI's waiting passengers board (traffic.cpp,
// VR_BUS) and riders get off. The player rides as a passenger (G, fare charged), skips ahead to the next stop (hold
// SPACE) or hijacks the bus (F) like any other vehicle. Included by transit_game.cpp (uses its tg helpers).

namespace Game {
namespace Transit {
namespace tb {

using namespace tg;

constexpr float kBusMat = 330.f, kBusDemat = 430.f;
constexpr float kVirtSpeed = 7.f;       // average running speed of a bus between stops (m/s)
constexpr float kStopAhead = 4.2f;      // front bumper past the stop point (front door at the flag pole)
constexpr int kBusFare = 2;

struct Bus {
    int route = 0, idx = 0;
    float d = 0.f;          // route distance of the door point (virtual position; tracked while the bus is driven)
    int next = 0;           // service-order index of the next stop
    float dwell = 0.f;      // virtual dwell remaining at a stop
    int veh = -1;
    u32 vehUid = 0;
    int driver = -1;
    u32 driverUid = 0;
    int phase = 0;          // 0 driving to the next stop, 2 dwelling at it
    float timer = 0.f;
    int leg = -1;           // route leg the bus is on (while driven)
    float offRoute = 0.f;
    float stuck = 0.f;
    float refresh = 0.f;
    float cooldown = 0.f;   // after a hijack / despawn: keep virtual for a while
    int annFor = -1;        // next-stop announcement given for this stop index
    bool materialized() const { return veh >= 0; }
};

struct BState {
    bool init = false, failed = false, registered = false;
    std::vector<Bus> buses;
    std::vector<int> asset;          // vassets index per route
    int rideBus = -1;                // bus the player rides as a passenger
    int paidBus = -1;
    u32 paidUid = 0;
    int skipStage = 0;
    float skipHold = 0.f;
    float hintTimer = 0.f;
    int hintStop = -1;
    std::vector<std::string> blipNames;
    float blipTimer = 0.f;
    int spawned = 0, stopsServed = 0, riders = 0;
};
BState gB;

const World::BusRoute& routeOf(const Bus& b) { return World::gTransit->busRoutes[b.route]; }
const World::BusStop& stopOf(const World::BusRoute& R, int k) { return World::gTransit->busStops[R.stops[k]]; }

int curbLane(const GameWorld& g, int edge, int dir) {
    const AI::LaneGraph& G = g.laneGraph;
    int grp = edge * 2 + (dir < 0 ? 1 : 0);
    if (grp < 0 || grp >= (int)G.groupFirst.size() || G.groupFirst[grp] < 0) return -1;
    return G.groupFirst[grp] + G.groupCount[grp] - 1;
}

bool busValid(const GameWorld& g, const Bus& b) {
    return b.veh >= 0 && b.veh < (int)g.vehicles.size() && g.vehicles[b.veh].used && g.vehicles[b.veh].uid == b.vehUid;
}

float frontLen(const GameWorld& g, int veh) {
    const Vehicles::VehicleModel& m = g.vassets[g.vehicles[veh].model].spec;
    return m.boxCenter.y + m.boxHalf.y;
}

float stopDwell(const World::BusStop& s, u32 seed) { return s.terminus ? 32.f + hashToFloat(seed) * 10.f : 12.f + hashToFloat(seed) * 7.f; }

// distance ahead along the route from a to b (0..length)
float ahead(const World::BusRoute& R, float a, float b) { return R.wrap(b - a); }

// ---------------------------------------------------------------------------------------------------------------- setup
// Route assets: the stock city bus with the route's destination sign and livery band colour
bool buildAssets(GameWorld& g) {
    Render::DynamicRenderer* dyn = g.renderer ? g.renderer->dynamic : nullptr;
    if (!dyn) return false;
    int stock = -1;
    for (int i = 0; i < (int)g.vassets.size(); i++)
        if (g.vassets[i].spec.cls == Vehicles::VC_BUS && g.vassets[i].spec.spawnWeight > 0.f && g.vassets[i].spec.name == "Boulevard") stock = i;
    if (stock < 0)
        for (int i = 0; i < (int)g.vassets.size(); i++)
            if (g.vassets[i].spec.cls == Vehicles::VC_BUS && g.vassets[i].spec.spawnWeight > 0.f) stock = i;
    if (stock < 0) return false;
    const std::vector<World::BusRoute>& routes = World::gTransit->busRoutes;
    double t0 = TimeSeconds();
    for (const World::BusRoute& R : routes) {
        VehicleAsset a = g.vassets[stock];
        std::string sign = R.number + " " + upperCase(R.name);
        MeshData body;
        if (stock < Vehicles::modelCount() && TransitModels::busBody(stock, sign, body)) a.body = dyn->createModel(body);
        a.spec.spawnWeight = 0.f;
        a.spec.price = 0;
        a.spec.liverySecondary = R.color;
        a.spec.fixedLivery = true;
        g.vassets.push_back(a);
        gB.asset.push_back((int)g.vassets.size() - 1);
    }
    // vassets may have reallocated: re-point the vehicles' model metadata
    for (Vehicle& v : g.vehicles)
        if (v.used && v.model >= 0 && v.model < (int)g.vassets.size()) v.sim.model = &g.vassets[v.model].spec;
    LOG("Transit: %zu route bus models (destination signs) in %.0f ms", routes.size(), (TimeSeconds() - t0) * 1000.0);
    return true;
}

// Stops become part of the AI's world: lane bus stops (parked cars keep clear, ambient buses stop too) and waiting spots
// (population / pedai put people there, stance 23, and traffic.cpp boards them onto a bus holding at the stop)
void registerStops(GameWorld& g) {
    AI::LaneGraph& G = g.laneGraph;
    const World::TransitNet& N = *World::gTransit;
    int lanesAdded = 0, spots = 0;
    auto addSpot = [&](vec3 pos, vec2 face, int edge, int lane, float u) {
        AI::ScenarioPoint sp;
        sp.pos = pos;
        sp.face = face;
        sp.kind = AI::SP_BUS_STOP;
        sp.edge = edge;
        sp.lane = lane;
        sp.u = u;
        int si = (int)G.spots.size();
        G.spots.push_back(sp);
        if (G.hashRes > 0 && !G.spotHash.empty()) {
            const float C = World::RoadNetwork::kHashCell;
            int x = Clamp((int)((pos.x + World::kWorldHalf) / C), 0, G.hashRes - 1), y = Clamp((int)((pos.y + World::kWorldHalf) / C), 0, G.hashRes - 1);
            G.spotHash[(size_t)y * G.hashRes + x].push_back(si);
        }
        spots++;
    };
    for (const World::BusStop& b : N.busStops) {
        int lane = curbLane(g, b.edge, b.dir);
        if (lane < 0) continue;
        AI::Lane& L = G.lanes[lane];
        if (b.ownShelter) {
            bool have = false;
            for (float u : L.busStops)
                if (fabsf(u - b.u) < 4.f) have = true;
            if (!have && b.u > L.u0 + 4.f && b.u < L.u1 - 6.f) {
                L.busStops.insert(std::upper_bound(L.busStops.begin(), L.busStops.end(), b.u), b.u);
                lanesAdded++;
            }
            addSpot(vec3(b.pos + b.face * 0.3f, b.z), b.face, b.edge, lane, b.u);
        }
        // room for a few more around the shelter and at the flag
        addSpot(vec3(b.pos + b.along * 1.35f + b.face * 0.55f, b.z), b.face, b.edge, lane, b.u);
        addSpot(vec3(b.pos - b.along * 1.45f + b.face * 0.6f, b.z), b.face, b.edge, lane, b.u);
        addSpot(vec3(b.flag - b.face * 0.9f - b.along * 0.8f, b.z), b.face, b.edge, lane, b.u);
    }
    LOG("Transit: %zu bus stops registered with the AI (%d lane stops added, %d waiting spots)", N.busStops.size(), lanesAdded, spots);
}

void initBuses(GameWorld& g) {
    const std::vector<World::BusRoute>& routes = World::gTransit->busRoutes;
    for (int r = 0; r < (int)routes.size(); r++) {
        const World::BusRoute& R = routes[r];
        if (R.stops.empty() || R.legs.empty()) continue;
        for (int i = 0; i < R.buses; i++) {
            Bus b;
            b.route = r;
            b.idx = i;
            b.d = R.wrap(R.stopDist[0] + R.length * (float)i / R.buses + 30.f);
            b.next = 0;
            for (int k = 0; k < (int)R.stops.size(); k++)
                if (ahead(R, b.d, R.stopDist[k]) < ahead(R, b.d, R.stopDist[b.next])) b.next = k;
            gB.buses.push_back(b);
        }
    }
    (void)g;
}

// ---------------------------------------------------------------------------------------------------------------- driving
// Host route for the traffic driver: the route's edges from the bus's leg to the next stop's leg (and one beyond)
void setRoute(GameWorld& g, Bus& b, AI::Driver& d) {
    const World::BusRoute& R = routeOf(b);
    int n = (int)R.legs.size();
    int target = R.legAt(R.stopDist[b.next]);
    int li = b.leg >= 0 ? b.leg : R.legAt(b.d);
    d.destEdges.clear();
    d.destNodes.clear();
    for (int k = li, guard = 0; guard <= n; k = (k + 1) % n, guard++) {
        d.destEdges.push_back(R.legs[k].edge);
        if (k == target && guard > 0) break;
        if (k == target && ahead(R, b.d, R.stopDist[b.next]) < R.length * 0.5f) break;
    }
    d.destEdges.push_back(R.legs[(target + 1) % n].edge);
    d.dest = stopOf(R, b.next).flag;
    d.hasDest = true;
    d.destRecalc = 1e6f;
    d.mode = AI::DM_ROUTE;
    (void)g;
}

// Off the chain (a forced turn, a detour around a blockage): search back onto the route toward the next stop
void rejoin(GameWorld& g, Bus& b, AI::Driver& d, int edge, int dir) {
    const World::BusRoute& R = routeOf(b);
    const World::BusStop& s = stopOf(R, b.next);
    std::vector<World::RouteLeg> path;
    std::vector<const char*> prefer;
    if (!World::transit_bus::legPath(*World::gRoads, edge, dir, s.edge, s.dir, prefer, path)) return;
    d.destEdges.clear();
    d.destNodes.clear();
    for (const World::RouteLeg& l : path) d.destEdges.push_back(l.edge);
    int target = R.legAt(R.stopDist[b.next]);
    d.destEdges.push_back(R.legs[(target + 1) % R.legs.size()].edge);
    d.destRecalc = 1e6f;
    d.hasDest = true;
    d.mode = AI::DM_ROUTE;
    g.traffic.clearRoute(d);
}

void startLaneChange(const AI::LaneGraph& G, AI::Driver& d, int target, float v) {
    const AI::Lane& L = G.lanes[d.path];
    d.lcLane = target;
    d.lcFrom = d.lat;
    d.lcTo = (G.lanes[target].offset - L.offset) * (float)L.dir;
    d.lcU0 = d.u;
    d.lcLen = Clamp(v * 3.2f, 16.f, 60.f);
    d.indicator = d.lcTo > 0.f ? 1 : -1;
    d.indicatorTimer = 0.f;
}

bool spawnBus(GameWorld& g, Bus& b, bool atStop) {
    const World::BusRoute& R = routeOf(b);
    int model = gB.asset[b.route];
    const Vehicles::VehicleModel& spec = g.vassets[model].spec;
    float front = spec.boxCenter.y + spec.boxHalf.y;
    float df = R.wrap(b.d + kStopAhead);
    int li = R.legAt(df);
    const World::RouteLeg& Lg = R.legs[li];
    int lane = curbLane(g, Lg.edge, Lg.dir);
    if (lane < 0) return false;
    const AI::Lane& L = g.laneGraph.lanes[lane];
    float u = (df - Lg.d0) - front;
    if (u < L.u0 + 1.f || u > L.u1 - 3.f) return false;   // at a junction: try again in a moment
    if (!g.traffic.laneFree(lane, u, spec.boxHalf.y, 6.f)) return false;
    vec3 p = g.laneGraph.lanePos(lane, u);
    vec2 t = g.laneGraph.laneTangent(lane, u);
    int vi = g.spawnVehicle(model, dvec3(p.x, p.y, p.z + 0.3f), AI::dirYaw(t), true, FAC_CIVILIAN);
    if (vi < 0) return false;
    Vehicle& v = g.vehicles[vi];
    int drv = v.seats[0];
    if (drv < 0 || !g.attachTraffic(vi, lane, u)) {
        g.despawnVehicle(vi, true);
        return false;
    }
    v.persistent = true;
    v.color0 = spec.liveryPrimary;
    v.color1 = R.color;
    v.dirt = 0.05f + 0.15f * hashToFloat(hash32(v.uid));
    float tod = g.env ? g.env->timeOfDay : 12.f;
    v.lightsOn = tod > 18.9f || tod < 6.9f;
    Ped& dp = g.peds[drv];
    dp.persistent = true;
    dp.brain.type = BRAIN_DRIVER;
    b.veh = vi;
    b.vehUid = v.uid;
    b.driver = drv;
    b.driverUid = dp.uid;
    b.leg = li;
    b.offRoute = 0.f;
    b.stuck = 0.f;
    b.refresh = 0.f;
    AI::Driver* d = g.traffic.get(vi);
    if (!d) return true;
    if (atStop) {
        const World::BusStop& s = stopOf(R, b.next);
        b.phase = 2;
        b.timer = Max(b.dwell, 4.f);
        d->mode = AI::DM_HOLD;
        d->holdTimer = -1.f;
        d->stopPath = curbLane(g, s.edge, s.dir);
        d->stopU = s.u + kStopAhead;
        v.sim.body.vel = vec3(0.f);
    } else {
        b.phase = 0;
        setRoute(g, b, *d);
        float v0 = Min(L.speed * 0.6f, 8.f);
        v0 = Min(v0, sqrtf(2.f * 2.f * Max(L.u1 - u - 10.f, 1.f)));
        v.sim.body.vel = vec3(t * v0, 0.f);
    }
    gB.spawned++;
    return true;
}

void despawnBus(GameWorld& g, Bus& b) {
    if (busValid(g, b)) g.despawnVehicle(b.veh, true);
    b.veh = -1;
    b.driver = -1;
    if (b.phase == 2) b.dwell = Max(b.timer, 0.f);
    b.phase = 0;
}

// Give the bus up to the city (hijacked, driver gone): it keeps its looks but leaves the timetable
void releaseBus(GameWorld& g, Bus& b, const char* why) {
    if (busValid(g, b)) {
        Vehicle& v = g.vehicles[b.veh];
        v.persistent = false;
        AI::Driver* d = g.traffic.get(b.veh);
        if (d && d->mode == AI::DM_ROUTE) {
            d->mode = AI::DM_NORMAL;
            d->hasDest = false;
            d->stopPath = -1;
        }
    }
    if (b.driver >= 0 && b.driver < (int)g.peds.size() && g.peds[b.driver].used && g.peds[b.driver].uid == b.driverUid) g.peds[b.driver].persistent = false;
    LOG("Transit: bus %s/%d left the route (%s)", routeOf(b).number.c_str(), b.idx, why);
    b.veh = -1;
    b.driver = -1;
    b.phase = 0;
    b.cooldown = 90.f;
    b.dwell = 0.f;
}

void speakBus(GameWorld& g, const std::string& line, vec3 pos, bool inside) {
#ifdef HAVE_AUDIO
    Speech::Persona ann = Speech::persona("newsreader_female");
    std::string txt = "[pa][calm]" + line;
    if (inside) Audio::speak(txt.c_str(), ann.voice, 0.8f);
    else Audio::speakAt(txt.c_str(), ann.voice, pos, 0.9f);
#else
    (void)pos;
#endif
    if (g.settingsSubtitles && inside) g.subtitle("Bus", line, 3.2f, 0xfff2b620);
}

std::string stopLine(const World::BusStop& s) {
    std::string extra;
    if (s.name.find("SkyLine") != std::string::npos) extra = " Change here for the SkyLine.";
    else if (s.name.find("Ferry") != std::string::npos) extra = " Change here for the bay ferry.";
    else if (s.name.find("Airport") != std::string::npos) extra = " Porto Sol International, all terminals.";
    return s.name + "." + extra;
}

// Riders get off at the middle door and walk away along the sidewalk
void alightRiders(GameWorld& g, Bus& b, int n) {
    if (g.chars.empty() || g.populationOff) return;
    const Vehicle& v = g.vehicles[b.veh];
    const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
    for (int k = 0; k < n; k++) {
        vec3 local(spec.boxHalf.x + 0.9f + 0.4f * k, -0.3f - 0.8f * k, 0.f);
        vec3 p = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, local);
        p.z = g.groundHeight(p.x, p.y, p.z + 1.5f);
        u32 seed = hash32(v.uid * 977u + (u32)gB.stopsServed * 31u + (u32)k);
        vec3 f = v.sim.forward();
        int id = spawnCivilian(g, seed, p, atan2f(-f.x, f.y));
        if (id < 0) return;
        releaseToCity(g, id);
        g.peds[id].persistent = false;
        gB.riders++;
    }
}

void arrive(GameWorld& g, Bus& b, AI::Driver& d, bool playerAboard, float plDist) {
    const World::BusRoute& R = routeOf(b);
    const World::BusStop& s = stopOf(R, b.next);
    b.phase = 2;
    b.timer = stopDwell(s, hash32(b.vehUid + (u32)b.next * 7u));
    d.mode = AI::DM_HOLD;
    d.holdTimer = -1.f;
    gB.stopsServed++;
    Vehicle& v = g.vehicles[b.veh];
#ifdef HAVE_AUDIO
    if (plDist < 60.f) Audio::play(Audio::SFX_TRAIN_DOORS, v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(1.3f, 3.5f, 1.2f)), 0.55f, 0.8f);
#endif
    if (plDist < 110.f) alightRiders(g, b, (int)(hash32(v.uid + (u32)gB.stopsServed) % 3u));
    if (playerAboard) speakBus(g, stopLine(s), v.sim.body.pos.toVec3(), true);
}

void depart(GameWorld& g, Bus& b, AI::Driver& d) {
    const World::BusRoute& R = routeOf(b);
    b.next = (b.next + 1) % (int)R.stops.size();
    b.phase = 0;
    d.stopPath = -1;
    setRoute(g, b, d);
    g.traffic.clearRoute(d);
    d.indicator = -1;
    d.indicatorTimer = 0.f;
}

void driveBus(GameWorld& g, Bus& b, float dt, bool playerAboard, float plDist) {
    Vehicle& v = g.vehicles[b.veh];
    AI::Driver* dp = g.traffic.get(b.veh);
    if (!dp) {
        if (!g.attachTraffic(b.veh)) return;
        dp = g.traffic.get(b.veh);
        if (!dp) return;
        setRoute(g, b, *dp);
    }
    AI::Driver& d = *dp;
    const AI::LaneGraph& G = g.laneGraph;
    const World::BusRoute& R = routeOf(b);
    const World::BusStop& s = stopOf(R, b.next);
    float front = frontLen(g, b.veh);
    float speed = v.sim.speed();
    Ped& drv = g.peds[b.driver];
    // ---- where are we on the route?
    if (G.isLane(d.path)) {
        const AI::Lane& L = G.lanes[d.path];
        int n = (int)R.legs.size(), found = -1;
        for (int k = 0; k < 10 && found < 0; k++) {
            int li = ((b.leg < 0 ? 0 : b.leg) + k) % n;
            if (R.legs[li].edge == L.edge && R.legs[li].dir == L.dir) found = li;
        }
        if (found >= 0) {
            b.leg = found;
            b.d = R.wrap(R.legs[found].d0 + d.u + front - kStopAhead);
            b.offRoute = 0.f;
        } else if (b.phase == 0) {
            b.offRoute += dt;
            if (b.offRoute > 1.f && b.offRoute - dt <= 1.f) rejoin(g, b, d, L.edge, L.dir);
            if (b.offRoute > 12.f) {
                // truly lost: find the leg by position
                for (int k = 0; k < n; k++)
                    if (R.legs[k].edge == L.edge && R.legs[k].dir == L.dir) {
                        b.leg = k;
                        b.offRoute = 0.f;
                        setRoute(g, b, d);
                        g.traffic.clearRoute(d);
                        break;
                    }
            }
        }
    }
    // the driver was scared off the route (flee) or had words with someone: take the route up again when calm
    if (b.phase == 0 && drv.brain.type == BRAIN_DRIVER && d.mode == AI::DM_NORMAL) {
        setRoute(g, b, d);
        g.traffic.clearRoute(d);
    }
    b.refresh -= dt;
    if (b.refresh <= 0.f) {
        b.refresh = 2.f;
        d.destRecalc = 1e6f;
    }
    if (b.phase == 0) {
        int stopLeg = R.legAt(R.stopDist[b.next]);
        float toStop = ahead(R, b.d, R.stopDist[b.next]);
        if (toStop > R.length - 25.f) toStop -= R.length;   // just past it
        int cl = curbLane(g, s.edge, s.dir);
        bool onStopLeg = b.leg == stopLeg;
        if (onStopLeg && d.stopPath < 0 && cl >= 0) {
            d.stopPath = cl;
            d.stopU = s.u + kStopAhead;
        }
        // keep to the curb lane on the stop's block (no turns left before the stop)
        if (onStopLeg && G.isLane(d.path) && d.lcLane < 0 && G.lanes[d.path].right >= 0 && toStop > 22.f && d.mode == AI::DM_ROUTE) {
            int t = G.lanes[d.path].right;
            if (g.traffic.gapOk(d, t, speed, 2.f)) startLaneChange(G, d, t, speed);
        }
        if (onStopLeg && G.isLane(d.path) && d.path != d.stopPath && d.lcLane < 0 && toStop < 20.f && toStop > 0.f) {
            d.stopPath = d.path;   // could not get over: stop in this lane
            d.stopU = s.u + kStopAhead;
        }
        // announcement for a rider: next stop
        if (playerAboard && b.annFor != b.next && toStop < 260.f && toStop > 60.f) {
            b.annFor = b.next;
            speakBus(g, "Next stop: " + s.name + ".", v.sim.body.pos.toVec3(), true);
        }
        if (onStopLeg && d.stopPath == d.path && fabsf(d.stopU - (d.u + front)) < 3.f && speed < 0.45f) {
            arrive(g, b, d, playerAboard, plDist);
        } else if (toStop < -12.f || (onStopLeg && d.path == d.stopPath && d.u + front > d.stopU + 8.f)) {
            // overshot (blocked stop, forced lane): carry on to the next one
            depart(g, b, d);
        }
        // stuck behind something for a long time far from the player: let the timetable take over again
        b.stuck = speed < 0.3f ? b.stuck + dt : 0.f;
    } else {
        b.timer -= dt;
        d.mode = AI::DM_HOLD;
        d.holdTimer = -1.f;
        v.indicator = 1;
        bool wait = false;
        // someone is still getting on (the player or an AI passenger walking to the door)
        Ped* pl = g.playerPed();
        if (pl && pl->state == PS_ENTERING && pl->targetVehicle == b.veh) wait = true;
        if (b.timer < 0.f && b.timer > -8.f) {
            vec3 vp = v.sim.body.pos.toVec3();
            std::vector<int> near_;
            g.pedsNear(vp.xy(), 16.f, near_);
            for (int pi : near_)
                if (pi < (int)g.ai.ped.size() && g.ai.ped[pi].uid == g.peds[pi].uid && g.ai.ped[pi].activity == ACT_ENTER_VEH && g.ai.ped[pi].targetVeh == b.veh)
                    wait = true;
        }
        if (b.timer <= 0.f && !wait) {
            v.indicator = -1;
            depart(g, b, d);
        }
        b.stuck = 0.f;
    }
}

// ---------------------------------------------------------------------------------------------------------------- player
void skipToNextStop(GameWorld& g, Bus& b) {
    if (!busValid(g, b)) return;
    const World::BusRoute& R = routeOf(b);
    const World::BusStop& s = stopOf(R, b.next);
    int lane = curbLane(g, s.edge, s.dir);
    if (lane < 0) return;
    Vehicle& v = g.vehicles[b.veh];
    float front = frontLen(g, b.veh);
    float u = s.u + kStopAhead - front;
    const AI::Lane& L = g.laneGraph.lanes[lane];
    u = Clamp(u, L.u0 + 1.f, L.u1 - 2.f);
    float dist = ahead(R, b.d, R.stopDist[b.next]);
    vec3 p = g.laneGraph.lanePos(lane, u);
    vec2 t = g.laneGraph.laneTangent(lane, u);
    // clear the stop of traffic in the way
    std::vector<int> near_;
    g.vehiclesNear(p.xy(), 9.f, near_);
    for (int o : near_)
        if (o != b.veh && !g.vehicles[o].persistent && g.playerVehicle() != o) g.despawnVehicle(o, true);
    v.sim.body.pos = dvec3(p.x, p.y, p.z + 0.25f);
    v.sim.body.rot = quatAxisAngle(vec3(0, 0, 1), AI::dirYaw(t));
    v.sim.body.vel = vec3(0.f);
    v.sim.body.angVel = vec3(0.f);
    g.traffic.detach(b.veh);
    g.attachTraffic(b.veh, lane, u);
    AI::Driver* d = g.traffic.get(b.veh);
    b.leg = R.legAt(R.stopDist[b.next]);
    b.d = R.stopDist[b.next];
    if (d) {
        d->stopPath = lane;
        d->stopU = s.u + kStopAhead;
        arrive(g, b, *d, true, 0.f);
    }
    float dt = dist / kVirtSpeed;
    if (g.env) {
        g.env->timeOfDay += dt / 120.f;
        if (g.env->timeOfDay >= 24.f) {
            g.env->timeOfDay -= 24.f;
            g.gameDay++;
        }
        g.env->gameSeconds += dt;
    }
    gS.clockOffset += dt;   // trains keep their timetable in step with the skipped time
    g.rig.cut = true;
    LOG("Transit: bus skip %.0f m (%.0f s) to %s", dist, dt, s.name.c_str());
}

void playerLogic(GameWorld& g, float dt) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    int pv = pl->state == PS_INVEHICLE ? pl->vehicle : -1;
    int ride = -1;
    for (int i = 0; i < (int)gB.buses.size(); i++)
        if (gB.buses[i].veh >= 0 && gB.buses[i].veh == pv && busValid(g, gB.buses[i])) ride = i;
    bool passenger = ride >= 0 && pl->seat > 0;
    // skip sequence
    if (gB.skipStage == 1 && g.fadedOut()) {
        if (passenger) skipToNextStop(g, gB.buses[ride]);
        gB.skipStage = 2;
        g.fadeIn(1.2f);
        return;
    }
    if (gB.skipStage == 2 && g.fadeAlpha <= 0.02f) gB.skipStage = 0;
    if (!passenger) {
        gB.rideBus = -1;
        gB.skipHold = 0.f;
        // at a stop: next buses
        if (pl->state == PS_ONFOOT) {
            gB.hintTimer -= dt;
            const World::TransitNet& N = *World::gTransit;
            vec2 p2 = pl->pos.toVec3().xy();
            for (int si = 0; si < (int)N.busStops.size(); si++) {
                const World::BusStop& s = N.busStops[si];
                if (length(s.pos - p2) > 5.f && length(s.flag - p2) > 3.5f) continue;
                if (gB.hintTimer > 0.f && gB.hintStop == si) break;
                gB.hintTimer = 20.f;
                gB.hintStop = si;
                std::string msg = "Bus stop: " + s.name + ".";
                for (int r = 0; r < (int)N.busRoutes.size(); r++) {
                    if (!(s.routeMask & (1u << r))) continue;
                    const World::BusRoute& R = N.busRoutes[r];
                    int k = (int)(std::find(R.stops.begin(), R.stops.end(), si) - R.stops.begin());
                    if (k >= (int)R.stops.size()) continue;
                    float best = 1e9f;
                    for (const Bus& b : gB.buses) {
                        if (b.route != r || b.cooldown > 0.f) continue;
                        float dd = ahead(R, b.d, R.stopDist[k]);
                        int between = 0;
                        for (int q = 0; q < (int)R.stops.size(); q++)
                            if (ahead(R, b.d, R.stopDist[q]) < dd) between++;
                        best = Min(best, dd / kVirtSpeed + between * 15.f + (b.phase == 2 ? b.timer : b.dwell));
                    }
                    std::string eta = best < 45.f ? "arriving" : StrFormat("%d min", (int)(best / 60.f + 0.5f));
                    msg += StrFormat(" %s %s: %s.", R.number.c_str(), R.name.c_str(), eta.c_str());
                }
                msg += StrFormat(" Press ~i:G|UP~ at the front door to ride ($%d).", kBusFare);
                g.help(msg, 7.f);
                break;
            }
        }
        return;
    }
    Bus& b = gB.buses[ride];
    const World::BusRoute& R = routeOf(b);
    if (gB.rideBus != ride) {
        gB.rideBus = ride;
        // fare, once per boarding
        if (gB.paidBus != ride || gB.paidUid != b.vehUid) {
            gB.paidBus = ride;
            gB.paidUid = b.vehUid;
            if (g.pinfo.money >= kBusFare) {
                g.pinfo.money -= kBusFare;
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_PURCHASE, 0.3f);
#endif
            }
            const World::BusStop& s = stopOf(R, b.next);
            g.notify("Porto Sol Transit", StrFormat("Route %s %s - next stop %s. Fare $%d.", R.number.c_str(), R.name.c_str(), s.name.c_str(), kBusFare));
            g.help("Hold ~i:SPACE|A~ to skip to the next stop. Press ~i:F|Y~ to get off.", 6.f);
            LOG("Transit: player boarded bus %s/%d", R.number.c_str(), b.idx);
        }
    }
    if (g.ctl.skip.down && gB.skipStage == 0 && g.playerControl && g.pinfo.wanted == 0 && b.phase == 0) {
        gB.skipHold += dt;
        if (gB.skipHold > 0.8f) {
            gB.skipStage = 1;
            gB.skipHold = 0.f;
            g.fadeOut(2.2f);
            g.subtitle("Bus", "Riding to " + stopOf(R, b.next).name + "...", 2.f, 0xfff2b620);
        }
    } else {
        gB.skipHold = 0.f;
    }
}

// ---------------------------------------------------------------------------------------------------------------- blips
void ensureBlips(GameWorld& g, float dt) {
    gB.blipTimer -= dt;
    if (gB.blipTimer > 0.f) return;
    gB.blipTimer = 2.5f;
    const World::TransitNet& N = *World::gTransit;
    if (gB.blipNames.empty()) {
        gB.blipNames.reserve(N.busStops.size());
        for (const World::BusStop& s : N.busStops) {
            std::string routes;
            for (int r = 0; r < (int)N.busRoutes.size(); r++)
                if (s.routeMask & (1u << r)) routes += (routes.empty() ? "" : ", ") + N.busRoutes[r].number;
            gB.blipNames.push_back(s.name + " (bus " + routes + ")");
        }
    }
    if (gB.blipNames.empty()) return;
    for (const UI::Blip& b : g.staticBlips)
        if (b.label == gB.blipNames[0].c_str()) return;
    for (size_t i = 0; i < N.busStops.size(); i++) {
        const World::BusStop& s = N.busStops[i];
        UI::Blip b;
        b.pos = s.pos;
        b.icon = UI::BLIP_BUS;
        b.color = 0;
        b.scale = 0.75f;
        b.shortRange = true;
        b.edge = false;
        b.label = gB.blipNames[i].c_str();
        g.staticBlips.push_back(b);
    }
}

// ---------------------------------------------------------------------------------------------------------------- update
void update(GameWorld& g, float dt) {
    const World::TransitNet& N = *World::gTransit;
    if (gB.failed || N.busRoutes.empty() || !g.ai.ready || g.laneGraph.lanes.empty()) return;
    if (!gB.init) {
        gB.init = true;
        if (!buildAssets(g)) {
            gB.failed = true;
            LOG("Transit: no city bus model, buses disabled");
            return;
        }
        registerStops(g);
        initBuses(g);
        LOG("Transit: %zu buses on %zu routes", gB.buses.size(), N.busRoutes.size());
    }
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    int pv = g.playerVehicle();
    playerLogic(g, dt);
    int mat = 0;
    for (const Bus& b : gB.buses) mat += b.materialized();
    for (Bus& b : gB.buses) {
        const World::BusRoute& R = routeOf(b);
        b.cooldown = Max(0.f, b.cooldown - dt);
        if (b.materialized()) {
            if (!busValid(g, b)) {
                b.veh = -1;
                b.cooldown = 30.f;
                continue;
            }
            Vehicle& v = g.vehicles[b.veh];
            int seat0 = v.seats[0];
            bool driverOk = seat0 >= 0 && seat0 == b.driver && g.peds[seat0].used && g.peds[seat0].uid == b.driverUid && g.peds[seat0].health > 0.f &&
                            !g.peds[seat0].isPlayer;
            if (!driverOk || v.exploded || v.sim.wrecked) {
                releaseBus(g, b, seat0 >= 0 && g.peds[seat0].isPlayer ? "hijacked" : "no driver");
                continue;
            }
            bool aboard = pv == b.veh;
            float dist = length(v.sim.body.pos.toVec3().xy() - pp.xy());
            bool seen = g.inCameraView(v.sim.body.pos.toVec3() + vec3(0, 0, 1.5f), 6.f);
            if (!aboard && ((dist > kBusDemat && !seen) || dist > 900.f || (b.stuck > 90.f && dist > 150.f && !seen))) {
                despawnBus(g, b);
                continue;
            }
            driveBus(g, b, dt, aboard && pl->seat > 0, dist);
            continue;
        }
        // ---- virtual: advance on the timetable
        if (b.dwell > 0.f) {
            b.dwell -= dt;
        } else {
            float step = kVirtSpeed * dt;
            float toStop = ahead(R, b.d, R.stopDist[b.next]);
            if (toStop <= step) {
                b.d = R.stopDist[b.next];
                b.dwell = stopDwell(stopOf(R, b.next), hash32((u32)b.idx * 131u + (u32)b.next));
                b.next = (b.next + 1) % (int)R.stops.size();
            } else {
                b.d = R.wrap(b.d + step);
            }
        }
        if (b.cooldown > 0.f || mat >= 8 || g.populationOff) continue;
        vec2 bp = R.pointAt(b.d + kStopAhead);
        float dist = length(bp - pp.xy());
        if (dist > kBusMat) continue;
        vec3 bp3(bp, pp.z + 1.5f);
        bool seen = g.inCameraView(bp3, 8.f) && dist < 260.f;
        if (seen && g.populationWarmup <= 0.f) continue;
        // a bus dwelling at a stop appears there with the stop as its current one
        bool atStop = b.dwell > 0.f;
        if (atStop) b.next = (b.next + (int)R.stops.size() - 1) % (int)R.stops.size();
        if (spawnBus(g, b, atStop)) mat++;
        else if (atStop) b.next = (b.next + 1) % (int)R.stops.size();
    }
    ensureBlips(g, dt);
}

bool playerOnBus() { return gB.rideBus >= 0; }

}  // namespace tb
}  // namespace Transit
}  // namespace Game
