// Automated end-to-end transit tests (--autoplay metro | bus | ferry | tram): the player walks to a SkyLine platform,
// boards, rides two stations and gets off; waits at a bus stop, rides a route bus, skips a stop and gets off; boards the
// bay ferry at a pier, crosses and goes ashore; waits at a streetcar stop, boards, rides, skips a stop and gets off.
// Scripted camera shots at each step (--shotdir), progress in the log; the run quits when the test is done.
// app.cpp zeroes the controls for unknown autoplay modes; the test writes GameWorld::ctl from Transit::update, which
// runs before the player update. Included by transit_game.cpp.

namespace Game {
namespace Transit {
namespace tt {

using namespace tg;

struct Test {
    std::string mode;
    bool checked = false;
    int stage = 0;
    float t = 0.f, stageT = 0.f;
    int shot = 0;
    // metro
    int station = -1, side = 0, target = -1;
    std::vector<vec2> path;
    int wp = 0;
    float wpT = 0.f;
    int train = -1, stopsSeen = 0, lastStop = -1;
    // bus
    int bus = -1, busStop = -1, busStopsRidden = 0;
    // ferry
    int pier = -1, ferry = -1;
    // streetcar
    int tram = -1, tramStop = -1, tramStops = 0, lastPhase = -1;
    bool skipped = false;
    bool done = false;
    // shots are taken a moment after the camera is placed (exposure and temporal effects settle)
    std::string pendName;
    float pendT = 0.f;
    // teleports wait for the destination to stream in, then put the player back on the (now solid) ground
    bool tpPending = false;
    vec3 tpTarget;
    float tpT = 0.f;
    float diagT = 0.f;
    float stuckT = 0.f;
    bool stuckShot = false;
    float quitT = 0.f;
    bool quitSent = false;
};
Test gT;

std::string shotName(const char* name) {
    const char* dir = Platform::argValue("shotdir");
    std::string d = dir ? dir : "Z:\\tmp\\";
    if (!d.empty() && d.back() != '\\' && d.back() != '/') d += "\\";
    return d + StrFormat("%s_%02d_%s.bmp", gT.mode.c_str(), gT.shot++, name);
}

void snap(GameWorld& g, const char* name) {
    (void)g;
    if (!gT.pendName.empty()) return;   // one shot at a time
    gT.pendName = name;
    gT.pendT = 1.4f;
}

void flushShot(GameWorld& g, float dt) {
    if (gT.pendName.empty()) return;
    gT.pendT -= dt;
    if (gT.pendT > 0.f || !g.requestScreenshot.empty()) return;
    g.requestScreenshot = shotName(gT.pendName.c_str());
    LOG("Transit test [%s]: shot %s at t=%.1f", gT.mode.c_str(), gT.pendName.c_str(), gT.t);
    gT.pendName.clear();
}

void scriptCam(GameWorld& g, vec3 from, vec3 at, float fov = 55.f) {
    // a new shot position: fresh exposure and temporal history (the gameplay camera only cuts for its own jumps)
    bool jump = !g.rig.scriptActive || length((g.rig.scriptPos - dvec3(from)).toVec3()) > 4.f;
    if (jump && g.renderer) g.renderer->cameraCut = true;
    g.rig.scriptActive = true;
    g.rig.scriptPos = dvec3(from);
    g.rig.scriptTarget = dvec3(at);
    g.rig.scriptFov = fov;
}
void releaseCam(GameWorld& g) {
    if (!gT.pendName.empty()) return;   // hold the shot's camera until it is taken
    if (g.rig.scriptActive) {
        g.rig.scriptActive = false;
        g.rig.cut = true;
    }
}

void next(int stage) {
    gT.stage = stage;
    gT.stageT = 0.f;
    LOG("Transit test [%s]: stage %d at t=%.1f", gT.mode.c_str(), stage, gT.t);
}

void teleport(GameWorld& g, vec3 p, float yaw) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    if (pl->vehicle >= 0) g.removePedFromVehicle(g.player, false);
    pl->pos = dvec3(p.x, p.y, g.groundHeight(p.x, p.y, p.z + 1.5f));
    pl->yaw = yaw;
    pl->vel = vec3(0.f);
    pl->state = PS_ONFOOT;
    g.rig.yaw = yaw;
    g.rig.cut = true;
    g.populationWarmup = Max(g.populationWarmup, 1.5f);
    gT.tpPending = true;
    gT.tpTarget = p;
    gT.tpT = 0.f;
}

// Follow a waypoint path on foot; returns true at the end. Stuck for too long at a waypoint: hop to it.
bool followPath(GameWorld& g, float dt, float mag = 0.8f) {
    Ped* pl = g.playerPed();
    if (!pl || gT.wp >= (int)gT.path.size()) return true;
    vec2 p = pl->pos.toVec3().xy();
    vec2 goal = gT.path[gT.wp];
    gT.wpT += dt;
    if (length(goal - p) < 0.45f || gT.wpT > 14.f) {
        if (gT.wpT > 14.f) {
            LOG("Transit test [%s]: stuck before waypoint %d, hopping", gT.mode.c_str(), gT.wp);
            pl->pos = dvec3(goal.x, goal.y, g.groundHeight(goal.x, goal.y, pl->pos.z + 2.f));
        }
        gT.wp++;
        gT.wpT = 0.f;
        if (gT.wp >= (int)gT.path.size()) return true;
        goal = gT.path[gT.wp];
    }
    // face the goal with the camera so camera-relative input walks straight
    vec2 d = goal - p;
    if (length2(d) > 1e-4f) g.rig.yaw = atan2f(-d.x, d.y);
    steerPlayer(g, goal, mag);
    return false;
}

// ---------------------------------------------------------------------------------------------------------------- metro
void metro(GameWorld& g, float dt) {
    const World::MetroLine& L = World::gTransit->metro;
    Ped* pl = g.playerPed();
    switch (gT.stage) {
        case 0: {
            gT.station = 2;   // Civic Center, outer platform (counter-clockwise: Midtown, Canvas District)
            gT.side = 0;
            gT.target = 4;
            const World::MetroStation& st = L.stations[gT.station];
            vec2 spot = platformPoint(st, gT.side, 4.f, 5.4f).xy();
            gT.path = exitPath(st, gT.side, spot);
            std::reverse(gT.path.begin(), gT.path.end());
            vec2 start = gT.path[0];
            vec2 d = gT.path[1] - start;
            teleport(g, vec3(start, st.streetZ), atan2f(-d.x, d.y));
            gT.wp = 1;
            gT.wpT = 0.f;
            next(1);
            break;
        }
        case 1: {
            const World::MetroStation& st = L.stations[gT.station];
            if (gT.stageT > 2.f && gT.stageT - dt <= 2.f) {
                vec3 foot = st.local(st.exitEnd[gT.side] * (kPlatformHalfLen - 40.f), 24.f, st.streetZ + 1.7f);
                scriptCam(g, foot, st.local(0.f, 0.f, st.platformZ() + 1.f), 60.f);
                snap(g, "street");
            }
            if (gT.stageT < 3.f) return;
            releaseCam(g);
            if (followPath(g, dt)) {
                // on the platform: bring the next counter-clockwise train to ~20 s away (keeps the automated run short)
                const Profile& P = gS.prof[0];
                int k = 0;
                while (k < (int)P.stIdx.size() && P.stIdx[k] != gT.station) k++;
                float best = 1e9f;
                for (const Train& t : gS.trains) {
                    if (t.dir != 0) continue;
                    float tau = fmodf(nowClock(g) + t.phase, P.cycle);
                    float wait = P.arr[k] - tau;
                    while (wait < 0.f) wait += P.cycle;
                    best = Min(best, wait);
                }
                if (best > 25.f && best < 1e8f) gS.clockOffset += best - 20.f;
                LOG("Transit test [metro]: on the platform, next train in %.0f s (shifted %.0f s)", Min(best, 20.f), best > 25.f ? best - 20.f : 0.f);
                next(2);
            }
            break;
        }
        case 2: {
            // wait on the platform for the counter-clockwise train with its doors open
            const World::MetroStation& st = L.stations[gT.station];
            g.rig.yaw = atan2f(st.right().x, -st.right().y);   // look across the track
            for (int ti = 0; ti < (int)gS.trains.size(); ti++) {
                const Train& t = gS.trains[ti];
                if (t.dir != 0 || !t.materialized() || t.st.stop < 0 || gS.prof[0].stIdx[t.st.stop] != gT.station) continue;
                if (t.st.dwellT > 2.f && gT.train != ti) {
                    gT.train = ti;
                    vec3 cam = platformPoint(st, gT.side, -30.f, 5.8f) + vec3(0, 0, 1.7f);
                    scriptCam(g, cam, platformPoint(st, gT.side, 10.f, 2.f) + vec3(0, 0, 1.6f), 62.f);
                    snap(g, "platform_train");
                }
                if (t.st.doors > 0.8f && gT.train == ti) {
                    int bt = -1, bc = 0, bd = 0;
                    vec3 pp = pl->pos.toVec3();
                    if (nearestOpenDoor(g, pp.xy(), pp.z, 30.f, bt, bc, bd) >= 0 && bt == ti) {
                        gT.path.clear();
                        gT.path.push_back(doorPoint(g, t.cars[bc], bd, 1.2f).xy());
                        gT.wp = 0;
                        gT.wpT = 0.f;
                        next(3);
                    }
                }
            }
            if (gT.stageT > 400.f) {
                LOG("Transit test [metro]: FAILED no train came");
                gT.done = true;
            }
            break;
        }
        case 3: {
            releaseCam(g);
            Train& t = gS.trains[gT.train];
            if (t.st.stop < 0 || t.st.doors < 0.6f) {
                LOG("Transit test [metro]: missed the doors, waiting for the next train");
                gT.train = -1;
                next(2);
                return;
            }
            if (followPath(g, dt, 0.6f) || gT.stageT > 4.f) {
                g.ctl.enter.pressed = true;   // board
                next(4);
            }
            break;
        }
        case 4: {
            if (gS.rideTrain >= 0) {
                LOG("Transit test [metro]: boarded, money now %lld", g.pinfo.money);
                gT.lastStop = -1;
                gT.stopsSeen = 0;
                next(5);
            } else if (gS.boardTrain < 0 && gT.stageT > 1.f) {
                if (gT.stageT > 6.f) {
                    LOG("Transit test [metro]: boarding did not start, retrying");
                    next(2);
                } else {
                    int bt = -1, bc = 0, bd = 0;
                    vec3 pp = pl->pos.toVec3();
                    if (nearestOpenDoor(g, pp.xy(), pp.z, 3.2f, bt, bc, bd) >= 0) g.ctl.enter.pressed = true;
                }
            }
            break;
        }
        case 5: {
            // riding: count station stops, screenshot from outside on the way
            if (gS.rideTrain < 0) {
                LOG("Transit test [metro]: FAILED fell off the train");
                gT.done = true;
                return;
            }
            Train& t = gS.trains[gS.rideTrain];
            if (gT.stageT > 3.f && gT.stageT - dt <= 3.f) snap(g, "aboard");
            if (t.st.stop >= 0 && t.st.stop != gT.lastStop) {
                gT.lastStop = t.st.stop;
                gT.stopsSeen++;
                LOG("Transit test [metro]: stopped at %s (%d)", stationOf(gS.prof[t.dir], t.st.stop).name.c_str(), gT.stopsSeen);
            }
            if (t.st.stop < 0 && t.st.v > 12.f && gT.shot == 3) {
                vec3 c = g.vehicles[t.cars[1]].sim.body.pos.toVec3();
                vec3 f = g.vehicles[t.cars[1]].sim.forward();
                vec3 side = normalize(cross(f, vec3(0, 0, 1)));
                scriptCam(g, c + side * 35.f + f * 30.f + vec3(0, 0, -6.f), c + vec3(0, 0, 1.f), 50.f);
                snap(g, "riding_outside");
            } else if (gT.shot >= 4 && g.rig.scriptActive && g.requestScreenshot.empty()) {
                releaseCam(g);
            }
            // after the first stop, hold SPACE to skip to the next station
            if (gT.stopsSeen >= 1 && !gT.skipped && t.st.stop < 0 && t.st.toNext > 8.f) {
                g.ctl.skip.down = true;
                if (gS.skipStage == 1) {
                    gT.skipped = true;
                    LOG("Transit test [metro]: skipping to the next station");
                }
            }
            bool atTarget = t.st.stop >= 0 && gS.prof[t.dir].stIdx[t.st.stop] == gT.target;
            if (atTarget && t.st.doors > 0.8f) {
                releaseCam(g);
                g.ctl.enter.pressed = true;   // get off
                next(6);
            }
            if (gT.stageT > 420.f) {
                LOG("Transit test [metro]: FAILED never reached the target");
                gT.done = true;
            }
            break;
        }
        case 6: {
            if (gS.rideTrain >= 0) {
                if (gT.stageT > 2.f) g.ctl.enter.pressed = true;
                return;
            }
            if (gT.stageT > 2.5f && gT.stageT - dt <= 2.5f) {
                const World::MetroStation& st = L.stations[gT.target];
                vec3 cam = platformPoint(st, 0, 26.f, 6.5f) + vec3(0, 0, 1.8f);
                scriptCam(g, cam, pl->pos.toVec3() + vec3(0, 0, 1.2f), 60.f);
                snap(g, "alighted");
                LOG("Transit test [metro]: PASSED got off at %s (boardings %d alightings %d riders on/off %d/%d)", st.name.c_str(), gS.boardings, gS.alightings,
                    gS.riderBoard, gS.riderAlight);
            }
            if (gT.stageT > 4.f) {
                releaseCam(g);
                const World::MetroStation& st = L.stations[gT.target];
                gT.path = exitPath(st, 0, pl->pos.toVec3().xy());
                gT.wp = 1;
                gT.wpT = 0.f;
                next(7);
            }
            break;
        }
        case 7: {
            if (followPath(g, dt) || gT.stageT > 60.f) {
                const World::MetroStation& st = L.stations[gT.target];
                scriptCam(g, pl->pos.toVec3() + vec3(st.right() * 18.f, 3.f) - vec3(st.dir * 12.f, 0.f), st.local(0.f, 0.f, st.platformZ() + 3.f), 62.f);
                snap(g, "back_on_street");
                next(8);
            }
            break;
        }
        default:
            if (gT.stageT > 2.f) gT.done = true;
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------- bus
// One line on what an AI-driven vehicle is doing: what it plans, what it waits for, what is in its way
std::string bodyDesc(GameWorld& g, int bi) {
    if (bi < 0 || bi >= (int)g.traffic.bodies.size()) return "none";
    const AI::Body& B = g.traffic.bodies[bi];
    if (B.kind == AI::BK_CAR && B.host >= 0 && B.host < (int)g.vehicles.size()) {
        const Vehicle& ov = g.vehicles[B.host];
        return StrFormat("car %d '%s' (%.1f, %.1f, %.1f) v %.1f fl %d parked %d seat0 %d", B.host, g.vassets[ov.model].spec.name.c_str(), B.pos.x, B.pos.y, B.z, B.speed,
                         (int)B.flags, (int)ov.parked, ov.seats[0]);
    }
    if (B.kind == AI::BK_PED && B.host >= 0 && B.host < (int)g.peds.size()) {
        int act = B.host < (int)g.ai.ped.size() ? (int)g.ai.ped[B.host].activity : -1;
        return StrFormat("ped %d (%.1f, %.1f, %.1f) v %.1f state %d act %d fl %d", B.host, B.pos.x, B.pos.y, B.z, B.speed, (int)g.peds[B.host].state, act, (int)B.flags);
    }
    return "?";
}
std::string driverDiag(GameWorld& g, int veh) {
    const AI::Driver* d = g.traffic.get(veh);
    if (!d) return "no driver";
    const AI::LaneGraph& G = g.laneGraph;
    std::string route;
    for (int k = 0; k < d->routeLen && k < 4; k++) route += StrFormat("%s%d", k ? "," : "", d->route[k]);
    std::string dest;
    for (size_t k = 0; k < d->destEdges.size() && k < 4; k++) dest += StrFormat("%s%d", k ? "," : "", d->destEdges[k]);
    std::string gate = "-";
    if (d->gateConn >= 0 && d->gateConn < (int)G.conns.size()) {
        const AI::Connector& C = G.conns[d->gateConn];
        gate = StrFormat("%d(node %d turn %d sig %d)", d->gateConn, C.node, (int)C.turn, (int)G.movementSignal(C.node, C.approach, C.turn, g.traffic.time));
    }
    const VehAI& va = g.vehAI(veh);
    int edge = G.isLane(d->path) ? G.lanes[d->path].edge : -1;
    return StrFormat("mode %d dummy %d path %d%s(e%d) u %.1f route [%s] dest [%s] vT %.1f stopD %.1f obstD %.1f obstV %.1f gate %s commit %d stopDone %d wait %.1f "
                     "blocked %.1f recover %.1f yield %.1f cap %.1f lc %d nudge %.2f/%.2f stopPath %d/%.1f role %d rage %d | obst %s",
                     (int)d->mode, (int)d->dummy, d->path, G.isLane(d->path) ? "L" : "C", edge, d->u, route.c_str(), dest.c_str(), d->vTarget, d->stopDist, d->obstDist,
                     d->obstSpeed, gate.c_str(), (int)d->committed, (int)d->stopDone, d->waitTime, d->blockedTime, d->recoverTimer, d->yieldHold, d->speedCap, d->lcLane,
                     d->nudge, d->nudgeTarget, d->stopPath, d->stopU, (int)va.role, (int)va.rage, bodyDesc(g, d->obstBody).c_str());
}

void bus(GameWorld& g, float dt) {
    const World::TransitNet& N = *World::gTransit;
    Ped* pl = g.playerPed();
    if (!tb::gB.init || N.busRoutes.empty()) return;
    switch (gT.stage) {
        case 0: {
            // route 9's terminus (Civic Center) or the first route's first stop
            int r = 0;
            for (int k = 0; k < (int)N.busRoutes.size(); k++)
                if (N.busRoutes[k].number == "9") r = k;
            const World::BusRoute& R = N.busRoutes[r];
            gT.busStop = 0;
            const World::BusStop& s = N.busStops[R.stops[0]];
            teleport(g, vec3(s.pos + s.face * 0.8f, s.z), atan2f(-s.face.x, s.face.y));
            // bring the nearest bus of the route 70 m before the stop
            int best = -1;
            float bd = 1e9f;
            for (int i = 0; i < (int)tb::gB.buses.size(); i++) {
                tb::Bus& b = tb::gB.buses[i];
                if (b.route != r) continue;
                float a = tb::ahead(R, b.d, R.stopDist[0]);
                if (a < bd) {
                    bd = a;
                    best = i;
                }
            }
            if (best >= 0) {
                tb::Bus& b = tb::gB.buses[best];
                if (b.materialized()) tb::despawnBus(g, b);
                b.d = R.wrap(R.stopDist[0] - 80.f);
                b.next = 0;
                b.dwell = 0.f;
                b.cooldown = 0.f;
                gT.bus = best;
            }
            next(1);
            break;
        }
        case 1: {
            tb::Bus& b = tb::gB.buses[gT.bus];
            const World::BusRoute& R = tb::routeOf(b);
            const World::BusStop& s = N.busStops[R.stops[0]];
            g.rig.yaw = atan2f(s.along.x, -s.along.y);   // look up the street toward the arriving bus
            if (b.materialized() && gT.stageT > 1.f) {
                vec3 bp = g.vehicles[b.veh].sim.body.pos.toVec3();
                if (gT.shot == 0 && length(bp.xy() - s.flag) < 45.f) {
                    // at the curb ahead of the stop, looking back down the street at the bus pulling in
                    scriptCam(g, vec3(s.flag + s.along * 18.f + s.face * 0.6f, s.z + 2.1f), bp + vec3(0, 0, 1.5f), 55.f);
                    snap(g, "bus_arriving");
                }
            }
            if (b.materialized() && b.phase == 2 && b.next == 0) {
                releaseCam(g);
                next(2);
            }
            if (gT.stageT > 150.f) {
                LOG("Transit test [bus]: FAILED the bus never arrived (materialized %d phase %d next %d d %.0f)", (int)b.materialized(), b.phase, b.next, b.d);
                gT.done = true;
            }
            break;
        }
        case 2: {
            tb::Bus& b = tb::gB.buses[gT.bus];
            if (!b.materialized()) {
                next(1);
                return;
            }
            const Vehicle& v = g.vehicles[b.veh];
            const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
            vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(spec.boxHalf.x + 0.7f, spec.boxCenter.y + spec.boxHalf.y - 1.2f, 0.f));
            if (gT.stageT > 1.f && gT.stageT - dt <= 1.f) {
                // three-quarter front view from the sidewalk ahead of the bus: sign, doors and the shelter behind
                vec3 mid = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(0.f, spec.boxCenter.y, spec.boxHalf.z));
                scriptCam(g, door + rotate(v.sim.body.rot, vec3(1.6f, 12.f, 1.9f)), mid, 55.f);
                snap(g, "bus_at_stop");
            }
            if (gT.stageT < 2.f) return;
            releaseCam(g);
            gT.path.assign(1, door.xy());
            gT.wp = 0;
            if (followPath(g, dt, 0.6f) || length(pl->pos.toVec3().xy() - door.xy()) < 1.6f || gT.stageT > 10.f) {
                g.ctl.special.pressed = true;   // ride as a passenger
                next(3);
            }
            break;
        }
        case 3: {
            if (pl->state == PS_INVEHICLE && tb::gB.rideBus >= 0) {
                LOG("Transit test [bus]: boarded as a passenger (seat %d), money %lld", pl->seat, g.pinfo.money);
                gT.busStopsRidden = 0;
                next(4);
            } else if (gT.stageT > 8.f) {
                LOG("Transit test [bus]: boarding failed (state %d), retrying", (int)pl->state);
                next(2);
            } else if (pl->state == PS_ONFOOT && gT.stageT > 1.5f && fmodf(gT.stageT, 1.5f) < dt) {
                g.ctl.special.pressed = true;
            }
            break;
        }
        case 4: {
            tb::Bus& b = tb::gB.buses[gT.bus];
            if (tb::gB.rideBus < 0) {
                LOG("Transit test [bus]: FAILED no longer riding");
                gT.done = true;
                return;
            }
            if (gT.stageT > 4.f && gT.stageT - dt <= 4.f) snap(g, "riding");
            gT.diagT -= dt;
            if (gT.diagT <= 0.f && b.materialized()) {
                gT.diagT = 5.f;
                const AI::Driver* d = g.traffic.get(b.veh);
                const Vehicle& v = g.vehicles[b.veh];
                if (d) {
                    LOG("Transit test [bus]: phase %d next %d leg %d off %.1f | mode %d path %d(%s) u %.1f speed %.1f vT %.1f stopD %.1f obstD %.1f body %d gate %d wait %.1f "
                        "route %d dest %zu lc %d dummy %d stopPath %d stopU %.1f",
                        b.phase, b.next, b.leg, b.offRoute, (int)d->mode, d->path, g.laneGraph.isLane(d->path) ? "lane" : "conn", d->u, v.sim.speed(), d->vTarget,
                        d->stopDist, d->obstDist, d->obstBody, d->gateConn, d->waitTime, d->routeLen, d->destEdges.size(), d->lcLane, (int)d->dummy, d->stopPath,
                        d->stopU);
                    // what holds the bus up: the obstacle it reacts to and the movement it waits for
                    vec3 bp = v.sim.body.pos.toVec3();
                    std::string ob = "none";
                    if (d->obstBody >= 0 && d->obstBody < (int)g.traffic.bodies.size()) {
                        const AI::Body& B = g.traffic.bodies[d->obstBody];
                        if (B.kind == AI::BK_CAR && B.host >= 0 && B.host < (int)g.vehicles.size()) {
                            const Vehicle& ov = g.vehicles[B.host];
                            ob = StrFormat("car %d '%s' at (%.1f, %.1f, %.1f) v %.1f flags %d parked %d seat0 %d persistent %d", B.host, g.vassets[ov.model].spec.name.c_str(),
                                           B.pos.x, B.pos.y, B.z, B.speed, (int)B.flags, (int)ov.parked, ov.seats[0], (int)ov.persistent);
                        } else if (B.kind == AI::BK_PED && B.host >= 0 && B.host < (int)g.peds.size()) {
                            int act = B.host < (int)g.ai.ped.size() ? (int)g.ai.ped[B.host].activity : -1;
                            ob = StrFormat("ped %d at (%.1f, %.1f, %.1f) v %.1f state %d act %d flags %d", B.host, B.pos.x, B.pos.y, B.z, B.speed, (int)g.peds[B.host].state, act,
                                           (int)B.flags);
                        }
                    }
                    std::string gate = "none";
                    if (d->gateConn >= 0 && d->gateConn < (int)g.laneGraph.conns.size()) {
                        const AI::Connector& C = g.laneGraph.conns[d->gateConn];
                        gate = StrFormat("node %d turn %d signal %d", C.node, (int)C.turn, (int)g.laneGraph.movementSignal(C.node, C.approach, C.turn, g.traffic.time));
                    }
                    LOG("Transit test [bus]: bus at (%.1f, %.1f) | obstacle %s | gate %s", bp.x, bp.y, ob.c_str(), gate.c_str());
                    // every route bus on the street nearby, with what its driver is doing
                    for (const tb::Bus& o : tb::gB.buses) {
                        if (!o.materialized() || !tb::busValid(g, o)) continue;
                        const Vehicle& ov = g.vehicles[o.veh];
                        vec3 op = ov.sim.body.pos.toVec3();
                        if (length(op.xy() - bp.xy()) > 250.f) continue;
                        LOG("Transit test [bus]: route bus %s/%d veh %d (%.1f, %.1f) phase %d timer %.1f next %d leg %d v %.1f | %s", tb::routeOf(o).number.c_str(), o.idx, o.veh,
                            op.x, op.y, o.phase, o.timer, o.next, o.leg, ov.sim.speed(), driverDiag(g, o.veh).c_str());
                    }
                }
            }
            // held up for long: a look at the street ahead from above the bus
            gT.stuckT = (b.materialized() && b.phase == 0 && g.vehicles[b.veh].sim.speed() < 0.3f) ? gT.stuckT + dt : 0.f;
            if (gT.stuckT > 25.f && !gT.stuckShot && gT.pendName.empty()) {
                const Vehicle& v = g.vehicles[b.veh];
                vec3 bp = v.sim.body.pos.toVec3(), f = v.sim.forward();
                vec3 side = normalize(cross(f, vec3(0, 0, 1)));
                scriptCam(g, bp - f * 9.f + side * 5.f + vec3(0, 0, 13.f), bp + f * 14.f, 60.f);
                snap(g, "stuck");
                gT.stuckShot = true;
            } else if (gT.stuckShot && g.rig.scriptActive && gT.pendName.empty() && g.requestScreenshot.empty()) {
                releaseCam(g);
            }
            static int lastPhase = 2;
            if (b.phase == 2 && lastPhase == 0) {
                gT.busStopsRidden++;
                LOG("Transit test [bus]: stopped at %s", tb::stopOf(tb::routeOf(b), b.next).name.c_str());
            }
            lastPhase = b.phase;
            if (gT.busStopsRidden == 1 && !gT.skipped && b.phase == 0 && gT.stageT > 10.f) {
                g.ctl.skip.down = true;   // hold to skip to the next stop
                if (tb::gB.skipStage == 1) {
                    gT.skipped = true;
                    LOG("Transit test [bus]: skipping ahead");
                }
            }
            if (gT.skipped && b.phase == 2 && tb::gB.skipStage == 0) {
                snap(g, "after_skip");
                next(5);
            }
            if (gT.stageT > 360.f) {
                LOG("Transit test [bus]: FAILED ride timeout");
                gT.done = true;
            }
            break;
        }
        case 5: {
            tb::Bus& b = tb::gB.buses[gT.bus];
            if (tb::gB.rideBus >= 0 && b.phase == 2 && gT.stageT > 2.f && fmodf(gT.stageT, 1.5f) < dt) g.ctl.enter.pressed = true;   // get off
            if (pl->state == PS_ONFOOT && gT.stageT > 3.f) {
                vec3 pp = pl->pos.toVec3();
                vec3 bp = b.materialized() ? g.vehicles[b.veh].sim.body.pos.toVec3() : pp;
                scriptCam(g, pp + normalize(vec3((pp - bp).xy(), 0.f) + vec3(0.01f, 0, 0)) * 9.f + vec3(0, 0, 2.5f), bp + vec3(0, 0, 1.3f), 60.f);
                snap(g, "got_off");
                LOG("Transit test [bus]: PASSED rode, skipped and got off (stops served %d, riders off %d)", tb::gB.stopsServed, tb::gB.riders);
                next(6);
            }
            if (gT.stageT > 60.f) {
                LOG("Transit test [bus]: FAILED could not get off");
                gT.done = true;
            }
            break;
        }
        default:
            if (gT.stageT > 2.f) gT.done = true;
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------- ferry
void ferry(GameWorld& g, float dt) {
    const World::TransitNet& N = *World::gTransit;
    Ped* pl = g.playerPed();
    if (!tf::gF.init || N.ferries.empty()) return;
    switch (gT.stage) {
        case 0: {
            gT.pier = 0;   // Sol Beach
            const World::FerryPier& fp = tf::pierOf(gT.pier);
            vec3 gp = tf::gatePoint(fp, 3.f);
            teleport(g, vec3(gp.x, gp.y, fp.deckZ), atan2f(-fp.dir.x, fp.dir.y));
            // the next ferry arrives in ~35 s: shift the transit clock
            float tau = fmodf(tf::nowF(g) + tf::gF.ferries[0].phase, tf::gF.cycle);
            float want = tf::gF.legStart[gT.pier] - 35.f;
            float shift = fmodf(want - tau + tf::gF.cycle * 4.f, tf::gF.cycle);
            gS.clockOffset += shift;
            gT.ferry = 0;
            next(1);
            break;
        }
        case 1: {
            tf::Ferry& f = tf::gF.ferries[gT.ferry];
            tf::FerryPose P = tf::poseAt(tf::nowF(g) + f.phase);
            const World::FerryPier& fp = tf::pierOf(gT.pier);
            if (f.materialized() && P.docked < 0 && gT.shot == 0 && P.toNext < 22.f) {
                vec3 c = g.vehicles[f.veh].sim.body.pos.toVec3();
                scriptCam(g, vec3(fp.head() - fp.dir * 2.f - fp.right() * 12.f, fp.deckZ + 1.7f), c + vec3(0, 0, 3.f), 55.f);
                snap(g, "ferry_approach");
            }
            if (P.docked == gT.pier && P.dockT > 6.f) {
                vec3 c = g.vehicles[f.veh].sim.body.pos.toVec3();
                scriptCam(g, vec3(fp.base + fp.dir * (fp.length * 0.35f) + fp.right() * 30.f, fp.deckZ + 6.f), c + vec3(0, 0, 2.f), 55.f);
                snap(g, "ferry_docked");
                next(2);
            }
            if (gT.stageT > 200.f) {
                LOG("Transit test [ferry]: FAILED the ferry never docked");
                gT.done = true;
            }
            break;
        }
        case 2: {
            if (gT.stageT < 1.f) return;
            releaseCam(g);
            const World::FerryPier& fp = tf::pierOf(gT.pier);
            gT.path.assign(1, tf::gatePoint(fp, 0.8f).xy());
            gT.wp = 0;
            if (followPath(g, dt, 0.6f) || gT.stageT > 10.f) {
                g.ctl.enter.pressed = true;
                next(3);
            }
            break;
        }
        case 3: {
            if (tf::gF.ride >= 0) {
                LOG("Transit test [ferry]: boarded, money %lld", g.pinfo.money);
                next(4);
            } else if (gT.stageT > 3.f) {
                g.ctl.enter.pressed = true;
                if (gT.stageT > 12.f) {
                    LOG("Transit test [ferry]: FAILED could not board");
                    gT.done = true;
                }
            }
            break;
        }
        case 4: {
            if (tf::gF.ride < 0) {
                LOG("Transit test [ferry]: FAILED not aboard");
                gT.done = true;
                return;
            }
            tf::Ferry& f = tf::gF.ferries[tf::gF.ride];
            tf::FerryPose P = tf::poseAt(tf::nowF(g) + f.phase);
            if (P.docked < 0 && P.speed > 6.f && gT.shot < 3) {
                vec3 c = g.vehicles[f.veh].sim.body.pos.toVec3();
                vec3 fw(P.fwd, 0.f), side = normalize(cross(fw, vec3(0, 0, 1)));
                scriptCam(g, c + side * 45.f + fw * 40.f + vec3(0, 0, 8.f), c + vec3(0, 0, 2.f), 50.f);
                snap(g, "ferry_underway");
            } else if (gT.shot >= 3 && g.rig.scriptActive && g.requestScreenshot.empty()) {
                releaseCam(g);
            }
            if (gT.shot >= 3 && !gT.skipped && P.docked < 0 && P.toNext > 20.f) {
                g.ctl.skip.down = true;
                if (tf::gF.skipStage == 1) {
                    gT.skipped = true;
                    LOG("Transit test [ferry]: skipping to the next pier");
                }
            }
            if (gT.skipped && P.docked >= 0 && P.dockT > 5.f && tf::gF.skipStage == 0) {
                g.ctl.enter.pressed = true;   // ashore
                next(5);
            }
            if (gT.stageT > 400.f) {
                LOG("Transit test [ferry]: FAILED crossing timeout");
                gT.done = true;
            }
            break;
        }
        case 5: {
            if (tf::gF.ride >= 0) {
                if (gT.stageT > 1.f && fmodf(gT.stageT, 1.f) < dt) g.ctl.enter.pressed = true;
                return;
            }
            if (gT.stageT > 2.f && gT.stageT - dt <= 2.f) {
                vec3 pp = pl->pos.toVec3();
                scriptCam(g, pp + vec3(6.f, -6.f, 3.f), pp + vec3(0, 0, 1.f), 60.f);
                snap(g, "ashore");
                LOG("Transit test [ferry]: PASSED crossed and went ashore at (%.0f, %.0f)", pp.x, pp.y);
                next(6);
            }
            break;
        }
        default:
            if (gT.stageT > 2.f) gT.done = true;
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------- streetcar
void tram(GameWorld& g, float dt) {
    const World::TransitNet& N = *World::gTransit;
    Ped* pl = g.playerPed();
    if (!tr::gT2.init || N.trams.empty()) return;
    const World::TramLine& L = N.trams[0];
    switch (gT.stage) {
        case 0: {
            // the stop two after the ferry terminal (a straight block on Bay Road)
            int si = 0;
            for (int k = 0; k < (int)L.stops.size(); k++)
                if (L.stops[k].name == "Ferry Terminal") si = (k + 2) % (int)L.stops.size();
            gT.tramStop = si;
            const World::TramStop& st = L.stops[si];
            teleport(g, vec3(st.pos + st.face * 1.1f + st.along * 2.f, st.z), atan2f(st.face.x, -st.face.y));
            // the tram due next comes ~130 m before the stop
            int best = -1;
            float bd = 1e9f;
            for (int i = 0; i < (int)tr::gT2.trams.size(); i++) {
                float a = L.ahead(tr::gT2.trams[i].s, st.s);
                if (a < bd) {
                    bd = a;
                    best = i;
                }
            }
            if (best >= 0) {
                tr::Tram& t = tr::gT2.trams[best];
                if (t.materialized()) tr::despawnTram(g, t);
                t.s = L.wrap(st.s - 130.f);
                t.v = 8.f;
                t.phase = 0;
                t.next = si;
                t.passedJ = -1;
                t.lineJ = -1;
                gT.tram = best;
            }
            next(1);
            break;
        }
        case 1: {
            tr::Tram& t = tr::gT2.trams[gT.tram];
            const World::TramStop& st = L.stops[gT.tramStop];
            g.rig.yaw = atan2f(st.along.x, -st.along.y);   // look up the street toward the coming tram
            if (t.materialized() && gT.shot == 0 && L.ahead(t.s, st.s) < 50.f && gT.stageT > 1.f) {
                vec3 tp = g.vehicles[t.sec[0]].sim.body.pos.toVec3();
                scriptCam(g, vec3(st.pos + st.along * 16.f + st.face * 0.4f, st.z + 1.9f), tp + vec3(0, 0, 1.6f), 55.f);
                snap(g, "tram_arriving");
            }
            if (t.materialized() && t.phase == 1 && t.next == gT.tramStop && t.doors > 0.9f) {
                releaseCam(g);
                next(2);
            }
            if (gT.stageT > 240.f) {
                LOG("Transit test [tram]: FAILED the streetcar never arrived (materialized %d phase %d next %d s %.0f v %.1f held %.1f)", (int)t.materialized(), t.phase,
                    t.next, t.s, t.v, t.held);
                gT.done = true;
            }
            break;
        }
        case 2: {
            tr::Tram& t = tr::gT2.trams[gT.tram];
            if (!t.materialized() || t.phase != 1) {
                next(1);
                return;
            }
            const World::TramStop& st = L.stops[gT.tramStop];
            if (gT.stageT > 0.5f && gT.stageT - dt <= 0.5f) {
                vec3 mid = g.vehicles[t.sec[1]].sim.body.pos.toVec3();
                scriptCam(g, vec3(st.pos - st.along * 14.f + st.face * 1.4f, st.z + 2.2f), mid + vec3(0, 0, 1.5f), 58.f);
                snap(g, "tram_at_stop");
            }
            if (gT.stageT < 2.f) return;
            releaseCam(g);
            int door;
            float dd;
            int mod = tr::nearestDoor(g, t, pl->pos.toVec3().xy(), door, dd);
            if (mod < 0) return;
            gT.path.assign(1, tr::tramDoorPoint(g, t.sec[mod], mod, door, 0.9f).xy());
            gT.wp = 0;
            if (followPath(g, dt, 0.6f) || dd < 1.6f || gT.stageT > 10.f) {
                g.ctl.enter.pressed = true;
                next(3);
            }
            break;
        }
        case 3: {
            if (tr::gT2.ride >= 0) {
                LOG("Transit test [tram]: boarded, money %lld", g.pinfo.money);
                gT.tramStops = 0;
                gT.lastPhase = 1;
                next(4);
            } else if (gT.stageT > 8.f) {
                LOG("Transit test [tram]: boarding failed (state %d), retrying", (int)pl->state);
                next(2);
            } else if (gT.stageT > 1.f && fmodf(gT.stageT, 1.5f) < dt) {
                g.ctl.enter.pressed = true;
            }
            break;
        }
        case 4: {
            if (tr::gT2.ride < 0) {
                LOG("Transit test [tram]: FAILED no longer aboard");
                gT.done = true;
                return;
            }
            tr::Tram& t = tr::gT2.trams[tr::gT2.ride];
            gT.diagT -= dt;
            if (gT.diagT <= 0.f) {
                gT.diagT = 5.f;
                LOG("Transit test [tram]: s %.0f v %.1f a %.2f phase %d next %d (%s) doors %.2f held %.1f lineJ %d passedJ %d", t.s, t.v, t.a, t.phase, t.next,
                    L.stops[t.next].name.c_str(), t.doors, t.held, t.lineJ, t.passedJ);
            }
            if (t.phase == 1 && gT.lastPhase == 0) {
                gT.tramStops++;
                LOG("Transit test [tram]: stopped at %s", L.stops[t.next].name.c_str());
            }
            gT.lastPhase = t.phase;
            // a look at the tram from the sidewalk while it runs
            if (t.phase == 0 && t.v > 6.f && gT.shot == 2) {
                vec3 c = g.vehicles[t.sec[1]].sim.body.pos.toVec3();
                vec3 f = g.vehicles[t.sec[1]].sim.forward();
                vec3 side = normalize(cross(f, vec3(0, 0, 1)));
                scriptCam(g, c + side * 9.f + f * 22.f + vec3(0, 0, 3.f), c + vec3(0, 0, 1.4f), 55.f);
                snap(g, "tram_riding");
            } else if (gT.shot >= 3 && g.rig.scriptActive && gT.pendName.empty() && g.requestScreenshot.empty()) {
                releaseCam(g);
            }
            if (gT.tramStops >= 1 && !gT.skipped && t.phase == 0 && t.v > 2.f && gT.shot >= 3) {
                g.ctl.skip.down = true;
                if (tr::gT2.skipStage == 1) {
                    gT.skipped = true;
                    LOG("Transit test [tram]: skipping to the next stop");
                }
            }
            if (gT.skipped && t.phase == 1 && tr::gT2.skipStage == 0 && t.doors > 0.8f) {
                snap(g, "after_skip");
                next(5);
            }
            if (gT.stageT > 420.f) {
                LOG("Transit test [tram]: FAILED ride timeout");
                gT.done = true;
            }
            break;
        }
        case 5: {
            if (tr::gT2.ride >= 0) {
                if (gT.stageT > 1.5f && fmodf(gT.stageT, 1.5f) < dt) g.ctl.enter.pressed = true;
                if (gT.stageT > 30.f) {
                    LOG("Transit test [tram]: FAILED could not get off");
                    gT.done = true;
                }
                return;
            }
            if (gT.stageT > 3.f && gT.stageT - dt <= 3.f) {
                vec3 pp = pl->pos.toVec3();
                scriptCam(g, pp + vec3(6.f, -6.f, 3.f), pp + vec3(0, 0, 1.f), 60.f);
                snap(g, "tram_got_off");
                LOG("Transit test [tram]: PASSED rode, skipped and got off at (%.0f, %.0f) (stops served %d, riders on/off %d/%d, bells %d)", pp.x, pp.y,
                    tr::gT2.stopsServed, tr::gT2.riderOn, tr::gT2.riderOff, tr::gT2.bells);
                next(6);
            }
            break;
        }
        default:
            if (gT.stageT > 2.f) gT.done = true;
            break;
    }
}

void update(GameWorld& g, float dt) {
    if (!gT.checked) {
        gT.checked = true;
        const char* a = Platform::argValue("autoplay");
        if (a && (strcmp(a, "metro") == 0 || strcmp(a, "bus") == 0 || strcmp(a, "ferry") == 0 || strcmp(a, "tram") == 0)) {
            gT.mode = a;
            LOG("Transit test [%s]: start", a);
            mu::setFlag(g, mu::EX_INTRO_DONE, 1);   // no opening shots or prologue call during the test
            if (g.renderer) g.renderer->settings.motionBlur = false;   // sharp shots: scripted cameras track moving vehicles
            // --transithour H: run the test at that time of day (night lighting checks)
            if (const char* h = Platform::argValue("transithour"))
                if (g.env) g.env->timeOfDay = (float)atof(h);
        }
    }
    if (gT.mode.empty()) return;
    if (gT.done) {
        // let the last shot land, then end the automated run
        gT.quitT += dt;
        flushShot(g, dt);
        if (gT.quitT > 4.f && gT.pendName.empty() && g.requestScreenshot.empty() && !gT.quitSent) {
            gT.quitSent = true;
            LOG("Transit test [%s]: finished, quitting", gT.mode.c_str());
            PostQuitMessage(0);
        }
        return;
    }
    gT.t += dt;
    flushShot(g, dt);
    if (gT.t < 1.f) return;   // let the world settle
    g.pinfo.wanted = 0;
    if (gT.tpPending) {
        // hold the player at the destination until its cells (and their collision) are in
        gT.tpT += dt;
        Ped* pl = g.playerPed();
        int pending = g.renderer && g.renderer->world ? g.renderer->world->pendingCount() : 0;
        if (pl && ((pending == 0 && gT.tpT > 1.5f) || gT.tpT > 12.f)) {
            pl->pos = dvec3(gT.tpTarget.x, gT.tpTarget.y, g.groundHeight(gT.tpTarget.x, gT.tpTarget.y, gT.tpTarget.z + 1.5f));
            pl->vel = vec3(0.f);
            pl->state = PS_ONFOOT;
            gT.tpPending = false;
            LOG("Transit test [%s]: arrived at (%.1f, %.1f, %.1f) after %.1f s of streaming", gT.mode.c_str(), gT.tpTarget.x, gT.tpTarget.y, pl->pos.z, gT.tpT);
        } else if (pl) {
            pl->pos = dvec3(gT.tpTarget.x, gT.tpTarget.y, gT.tpTarget.z + 0.3f);
            pl->vel = vec3(0.f);
        }
        return;
    }
    gT.stageT += dt;
    if (gT.mode == "metro") metro(g, dt);
    else if (gT.mode == "bus") bus(g, dt);
    else if (gT.mode == "ferry") ferry(g, dt);
    else if (gT.mode == "tram") tram(g, dt);
    if (gT.done) {
        releaseCam(g);
        LOG("Transit test [%s]: done at t=%.1f", gT.mode.c_str(), gT.t);
    }
}

}  // namespace tt
}  // namespace Transit
}  // namespace Game
