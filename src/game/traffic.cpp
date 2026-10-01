// Traffic integration: attaches AI drivers to vehicles, runs the driver model (traffic_core.cpp) with the kinematic
// dummy mode for far, unseen vehicles, and handles the gameplay side of driving: fleeing and abandoning cars, stuck and
// flipped recovery, honking and shouting at blockers, buses serving stops, taxis picking up and dropping off fares,
// ambulances/fire trucks running to incidents with sirens, plus boat and helicopter autopilots.
// Police pursuit driving lives in police.cpp.
#include "gameworld.h"

namespace Game {

namespace traffic_detail {

constexpr float kDummyFar = 205.f;     // physics -> dummy when farther than this from camera and player (and unseen)
constexpr float kDummyNear = 180.f;    // dummy -> physics when closer than this or in view

float yawOfVehicle(const Vehicles::VehicleState& s) {
    vec3 f = s.forward();
    return atan2f(-f.x, f.y);
}

// Highest roof within r meters of q (helicopter terrain following over the towers).
float skylineAt(const GameWorld& g, vec2 q, float r) {
    const World::BuildingSet* bs = g.buildings ? g.buildings : World::gBuildings;
    if (!bs) return -1e9f;
    thread_local std::vector<int> nb;
    nb.clear();
    bs->buildingsNear(q, r + 60.f, nb);
    float top = -1e9f;
    for (int i : nb) {
        const World::Building& b = bs->buildings[i];
        vec2 dd = q - b.c;
        float ex = fabsf(dot(dd, b.ax)) - b.hx, ey = fabsf(dot(dd, vec2(-b.ax.y, b.ax.x))) - b.hy;
        if (Max(ex, ey) < r) top = Max(top, b.baseZ + b.height);
    }
    return top;
}

// Leaving a parking spot at the curb: blinker on, wait for a gap in the lane (nothing beside, nothing coming up fast
// behind), then steer out at a shallow angle and hand over to the traffic core once in the lane. Returns false when
// done (or given up): the caller then attaches the car to the lane traffic.
bool pullOutStep(GameWorld& g, int vi, float dt) {
    Vehicle& v = g.vehicles[vi];
    VehAI& va = g.vehAI(vi);
    const AI::LaneGraph& G = g.laneGraph;
    const AI::TrafficCore& T = g.traffic;
    const Vehicles::VehicleModel& m = g.vassets[v.model].spec;
    vec3 p3 = v.sim.body.pos.toVec3();
    vec2 fwd = v.sim.forward().xy();
    fwd = length2(fwd) > 1e-6f ? normalize(fwd) : vec2(0, 1);
    float u = 0.f, lat = 0.f;
    int lane = G.nearestLane(p3.xy(), fwd, 9.f, &u, &lat, AI::LF_NOTRAFFIC);
    va.pullTimer += dt;
    if (lane < 0 || va.pullTimer > 45.f) {
        va.pullOut = 0;
        v.parked = false;
        v.indicator = 0;
        return false;
    }
    vec2 lt = G.laneTangent(lane, u), rn = AI::rightOf(lt);
    vec2 lp = G.lanePos(lane, u).xy();
    float psi = atan2f(cross(lt, fwd), dot(lt, fwd));   // + = the nose points left of the lane
    float speed = v.sim.forwardSpeed();
    int self = vi < (int)g.ai.vehBody.size() ? g.ai.vehBody[vi] : -1;
    // traffic in the lane we join, and anything right in front of our bumper
    bool laneBusy = false, frontBlocked = false;
    float r = 70.f;
    T.hash.query(T.bodies, lp - vec2(r), lp + vec2(r), [&](int bi) {
        if (bi == self) return;
        const AI::Body& b = T.bodies[bi];
        if (fabsf(b.z - p3.z) > 3.f) return;
        vec2 rel = b.pos - lp;
        float along = dot(rel, lt), latb = dot(rel, rn);
        if (b.kind == AI::BK_CAR && fabsf(latb) < 2.3f) {
            float vb = dot(b.vel, lt);
            if (along > -10.f && along < 10.f) laneBusy = true;                                   // beside us
            else if (along <= -10.f && vb > 0.5f && -along - 8.f < vb * 3.5f + 5.f) laneBusy = true;   // coming up
        }
        vec2 relC = b.pos - p3.xy();
        float ahead = dot(relC, fwd), side = dot(relC, AI::rightOf(fwd));
        if (ahead > 0.f && ahead - b.halfLen - m.boxHalf.y < (b.kind == AI::BK_PED ? 2.5f : 1.6f) && fabsf(side) < m.boxHalf.x + b.halfWid + 0.3f) frontBlocked = true;
    });
    Vehicles::VehicleControls c;
    v.indicator = -1;
    if (va.pullOut == 1) {
        // blinker on for a moment, then go at the first safe gap
        c.brake = 1.f;
        c.handbrake = true;
        if (va.pullTimer > 1.6f && !laneBusy && !frontBlocked) {
            va.pullOut = 2;
            va.pullTimer = 0.f;
            v.parked = false;
        }
        v.ctl = c;
        return true;
    }
    // steering out: head left of the lane while right of it, straighten up as we get there
    float want = Clamp(lat * 0.32f, -0.42f, 0.42f);
    c.steer = Clamp((psi - want) * 2.2f, -1.f, 1.f);
    float vWant = frontBlocked ? 0.f : 3.f;
    if (speed < vWant - 0.3f) c.throttle = Clamp(0.25f + (vWant - speed) * 0.12f, 0.f, 0.6f);
    else if (speed > vWant + 0.8f || frontBlocked) c.brake = frontBlocked ? 1.f : 0.4f;
    if (frontBlocked && fabsf(speed) < 0.3f) c.handbrake = true;
    v.ctl = c;
    bool inLane = fabsf(lat) < 0.8f && fabsf(psi) < 0.25f;
    if (inLane || va.pullTimer > 10.f) {
        va.pullOut = 0;
        v.indicator = 0;
        g.attachTraffic(vi, lane, u);
        return false;
    }
    return true;
}

}  // namespace traffic_detail

using namespace traffic_detail;

bool GameWorld::attachTraffic(int vi, int lane, float u, bool cautious) {
    if (!ai.ready || vi < 0 || vi >= (int)vehicles.size() || !vehicles[vi].used) return false;
    Vehicle& v = vehicles[vi];
    const Vehicles::VehicleModel& m = vassets[v.model].spec;
    AI::VehicleInfo info = AI::makeVehicleInfo(m, v.sim);
    if (lane < 0) {
        vec2 pos = v.sim.body.pos.toVec3().xy();
        vec3 f = v.sim.forward();
        lane = laneGraph.nearestLane(pos, f.xy(), 30.f, &u);
        if (lane < 0) return false;
    }
    traffic.attach(vi, v.uid, hash32(v.uid * 7919u + 3u), info, lane, u, cautious);
    VehAI& va = vehAI(vi);
    va.managed = true;
    if (m.cls == Vehicles::VC_BUS) va.role = VR_BUS;
    else if (m.cls == Vehicles::VC_TAXI && va.role == VR_TRAFFIC) va.role = VR_TAXI;
    return true;
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::driveVehicleAI(int vi, float dt) {
    if (vi < 0 || vi >= (int)vehicles.size() || !vehicles[vi].used) return;
    Vehicle& v = vehicles[vi];
    if (v.scripted) return;
    int drv = v.seats[0];
    if (drv < 0) return;
    Ped& dp = peds[drv];
    Brain& b = dp.brain;
    VehAI& va = vehAI(vi);
    const Vehicles::VehicleModel& spec = vassets[v.model].spec;
    va.barkTimer -= dt;
    va.hornBarkTimer -= dt;
    // ---- the player's own car (autoplay / driving assist): plain lane following along the road it is on
    if (dp.isPlayer) {
        AI::Driver* d = traffic.get(vi);
        if (!d) {
            if (!attachTraffic(vi)) {
                v.ctl = Vehicles::VehicleControls();
                v.ctl.brake = 1.f;
                v.ctl.handbrake = true;
                return;
            }
            d = traffic.get(vi);
            va.role = VR_TRAFFIC;
        }
        if (d->dummy) traffic.toPhysics(vi, v.sim);
        // normal or routed driving; flee mode (runs lights) when the caller set it, e.g. the chase autoplay
        if (d->mode != AI::DM_NORMAL && d->mode != AI::DM_ROUTE && d->mode != AI::DM_FLEE) d->mode = AI::DM_NORMAL;
        AI::DriveOut out;
        traffic.drive(vi, v.sim, dt, out);
        v.ctl = out.ctl;
        v.indicator = out.indicator;
        v.hornOn = out.horn;
        // autopilot hung on a ledge (see the AI car recovery below): back onto the lane a few meters on
        bool hung = !d->dummy && d->vTarget > 1.f && v.sim.speed() < 0.5f && (v.sim.up().z < 0.94f || v.sim.wheelsOnGround < Min(3, v.sim.wheelCount)) && d->path >= 0;
        va.hungTime = hung ? va.hungTime + dt : 0.f;
        if (va.hungTime > 5.f) {
            float u = Min(d->u + 4.f, laneGraph.pathLength(d->path) - 0.5f);
            vec3 p = laneGraph.pathPos(d->path, u);
            vec2 t = laneGraph.pathTangent(d->path, u);
            Vehicles::resetVehicle(v.sim, dvec3(p.x, p.y, p.z + 0.35f), AI::dirYaw(t));
            v.sim.body.vel = vec3(t * 2.f, 0.f);
            d->u = u;
            va.hungTime = 0.f;
            ai.stats.unhung++;
        }
        return;
    }
    // ---- boats and aircraft have their own autopilots
    if (isBoat(vi)) {
        if (v.faction == FAC_POLICE && pinfo.wanted > 0) {
            aiPoliceDrive(vi, dt);
            return;
        }
        // wander on open water: pick a far water point in a straight line, re-pick on arrival
        if (length(va.taskPos) < 1.f || length(v.sim.body.pos.toVec3().xy() - va.taskPos) < 40.f || va.taskTimer <= 0.f) {
            vec2 p = v.sim.body.pos.toVec3().xy();
            for (int k = 0; k < 12; k++) {
                u32 h = hash32(v.uid * 31u + (u32)(time * 7.0) + k * 977u);
                float ang = hashToFloat(h) * kTwoPi, r = 200.f + hashToFloat(hash32(h)) * 400.f;
                vec2 q = p + vec2(cosf(ang), sinf(ang)) * r;
                bool water = true;
                for (int s = 1; s <= 8 && water; s++) water = map->isWater(Lerp(p.x, q.x, s / 8.f), Lerp(p.y, q.y, s / 8.f));
                if (water) {
                    va.taskPos = q;
                    break;
                }
            }
            va.taskTimer = 90.f;
        }
        va.taskTimer -= dt;
        aiDriveBoat(vi, dt, dvec3(va.taskPos.x, va.taskPos.y, 0.0), b.type == BRAIN_FLEE ? 22.f : 11.f);
        return;
    }
    if (isAircraft(vi)) {
        if (v.faction == FAC_POLICE) {
            aiPoliceDrive(vi, dt);
            return;
        }
        // civilian helicopter with an AI pilot (no scripted path): hover-orbit where it is
        vec3 p = v.sim.body.pos.toVec3();
        aiFlyHeli(vi, dt, dvec3(p.x, p.y, 0.0), Max(p.z, 60.f), 150.f, true);
        return;
    }
    // ---- a patrol car an officer is bringing a prisoner to (police.cpp escort): it stays put until they are in
    if (v.faction == FAC_POLICE && time < va.escortHold && va.task != PT_TRANSPORT && b.type == BRAIN_DRIVER) {
        v.ctl = Vehicles::VehicleControls();
        v.ctl.brake = 1.f;
        v.ctl.handbrake = true;   // (the brake pedal alone at a standstill is reverse)
        v.hornOn = false;
        return;
    }
    // ---- police in pursuit / responding: their own driving logic
    if (v.faction == FAC_POLICE && (b.type == BRAIN_COMBAT || b.type == BRAIN_ARREST || b.type == BRAIN_GOTO || va.task != 0)) {
        aiPoliceDrive(vi, dt);
        return;
    }
    // hostile drivers (gang drive-bys, mission hostiles set to combat) chase their target like pursuers
    if (b.type == BRAIN_COMBAT && dp.target_is_valid(*this)) {
        aiPoliceDrive(vi, dt);
        return;
    }
    // ---- pulling out of a parking spot (owner just got in)
    if (va.pullOut > 0 && b.type == BRAIN_DRIVER && pullOutStep(*this, vi, dt)) return;
    // ---- lane traffic
    AI::Driver* d = traffic.get(vi);
    if (!d) {
        if (!attachTraffic(vi)) {
            v.ctl = Vehicles::VehicleControls();
            v.ctl.handbrake = true;
            v.ctl.brake = 1.f;
            return;
        }
        d = traffic.get(vi);
    }
    // mode from the driver's brain and role
    if (b.type == BRAIN_FLEE) {
        if (d->mode != AI::DM_FLEE) {
            d->mode = AI::DM_FLEE;
            d->routeLen = 0;
            traffic.planRoute(*d);
        }
        vec3 from = b.target >= 0 && b.target < (int)peds.size() && peds[b.target].used ? peds[b.target].pos.toVec3() : b.goal.toVec3();
        d->threat = from.xy();
        va.fleeTimer += dt;
        // calm down after a while far from the threat
        if (va.fleeTimer > 25.f && length(v.sim.body.pos.toVec3().xy() - d->threat) > 250.f) {
            b.type = BRAIN_DRIVER;
            d->mode = AI::DM_NORMAL;
            va.fleeTimer = 0.f;
        }
    } else if (d->mode == AI::DM_FLEE) {
        d->mode = AI::DM_NORMAL;
    }
    vec3 vp = v.sim.body.pos.toVec3();
    Ped* pl = playerPed();
    float plD = pl ? length(rel(v.sim.body.pos, pl->pos)) : 1e9f;
    float camD = length(rel(v.sim.body.pos, rig.cam.pos));
    bool inView = inCameraView(vp, 4.f);
    // ---- road rage: the player crashed into us; stop, then get out and have words
    if (va.rage == 1 && b.type == BRAIN_DRIVER) {
        va.rageTimer += dt;
        d->mode = AI::DM_HOLD;
        d->holdTimer = -1.f;
        v.hornOn = va.rageTimer < 1.2f;
        AI::DriveOut out;
        traffic.drive(vi, v.sim, dt, out);
        v.ctl = out.ctl;
        if (va.rageTimer > 1.6f && v.sim.speed() < 0.8f) {
            if (pl && plD < 35.f) {
                removePedFromVehicle(drv, true);
                PedAI& da = pedAI(drv);
                da.activity = ACT_ROADRAGE;
                ai.stats.roadRage++;
                da.homeVeh = vi;
                da.actTimer = 10.f + hashToFloat(hash32(v.uid)) * 6.f;
                da.shoutTimer = 0.8f;
                da.linger = 0.f;
                dp.brain.type = BRAIN_WANDER;
                v.parked = true;
                va.rage = 2;
                aiSay(drv, BK_CRASH, 1.f, true);
            } else {
                va.rage = 0;   // the culprit is gone: drive on
                d->mode = AI::DM_NORMAL;
            }
        }
        return;
    }
    if (va.rage == 2 && dp.state == PS_INVEHICLE) {
        va.rage = 0;
        d->mode = AI::DM_NORMAL;
    }
    // ---- deliveries: a van or service truck double-parks on a multi-lane street, the driver takes something to a
    // door, comes back and drives on (traffic changes lanes around it)
    if (va.role == VR_TRAFFIC && !d->dummy && b.type == BRAIN_DRIVER) {
        Vehicles::VehicleClass cls = spec.cls;
        bool van = cls == Vehicles::VC_VAN || cls == Vehicles::VC_SERVICE;
        va.errandTimer += dt;
        if (van && va.errand == 0 && va.errandTimer > 20.f) {
            va.errandTimer = 0.f;
            bool onLane = d->path < (int)laneGraph.lanes.size() && d->lcLane < 0;
            u32 he = hash32(v.uid * 77u + (u32)(time * 0.1));
            if (onLane && hashToFloat(he) < 0.3f && plD > 45.f && plD < 170.f && d->mode == AI::DM_NORMAL) {
                const AI::Lane& L = laneGraph.lanes[d->path];
                bool curbLane = L.right < 0 && L.left >= 0;   // rightmost of two or more lanes this way
                if (curbLane && d->u > L.u0 + 20.f && d->u < L.u1 - 40.f && !(L.flags & AI::LF_DIRT)) {
                    vec2 t = laneGraph.laneTangent(d->path, d->u + 12.f);
                    vec3 door;
                    vec2 curb = laneGraph.lanePos(d->path, d->u + 12.f, L.width * 0.5f + 2.5f).xy();
                    if (aiBuildingDoorNear(*this, curb, 22.f, he, door) && dot(door.xy() - curb, AI::rightOf(t)) > -1.f) {
                        va.errand = 1;
                        va.errandDoor = door;
                        d->mode = AI::DM_PULLOVER;
                        d->holdTimer = -1.f;
                    }
                }
            }
        }
        if (va.errand == 1) {
            if (d->mode != AI::DM_PULLOVER) {
                va.errand = 0;   // interrupted (sirens, a threat...)
            } else if (v.sim.speed() < 0.3f && va.errandTimer > 2.5f) {
                // parked: out with the parcel
                va.errand = 2;
                va.errandTimer = 0.f;
                v.parked = true;
                v.indicator = 2;   // hazard lights while double-parked
                removePedFromVehicle(drv, true);
                PedAI& da = pedAI(drv);
                da.activity = ACT_ERRAND;
                da.homeVeh = vi;
                da.anchor = va.errandDoor.xy();
                da.actTimer = 60.f;
                da.clipTimer = 7.f + hashToFloat(hash32(v.uid * 5u)) * 9.f;   // time at the door
                da.linger = 0.f;
                dp.brain.type = BRAIN_WANDER;
                dp.brain.edge = -1;
                return;
            }
        }
    }
    // ---- arriving: an ordinary car pulls into a free stretch of the parking strip, the driver gets out and walks off
    // into a building close by (the car stays behind, parked - population.cpp counts it with the parked cars)
    if (va.role == VR_TRAFFIC && !d->dummy && b.type == BRAIN_DRIVER && va.errand == 0 && va.pullOut == 0) {
        const int NL = (int)laneGraph.lanes.size();
        if (va.parking == 0 && spec.cls <= Vehicles::VC_MUSCLE && d->mode == AI::DM_NORMAL && d->lcLane < 0 && d->path < NL && d->stopPath < 0) {
            va.parkTimer += dt;
            if (va.parkTimer > 12.f) {
                va.parkTimer = 0.f;
                u32 hp = hash32(v.uid * 131u + (u32)(time * 0.1));
                const AI::Lane& L = laneGraph.lanes[d->path];
                bool street = L.cls == World::RC_STREET || L.cls == World::RC_AVENUE || L.cls == World::RC_LANE;
                const World::RoadClassInfo& info = World::roadInfo((World::RoadClass)L.cls);
                bool tryHere = hashToFloat(hp) < 0.2f * ai.lifeBoost && time - ai.lastParkArrive > 20.0 / Max(ai.lifeBoost, 0.1f) && plD > 40.f &&
                               plD < 160.f && street && L.right < 0 && info.shoulder >= 1.8f && !(L.flags & (AI::LF_DIRT | AI::LF_HIGHWAY | AI::LF_RAMP));
                // the first free gap in the parking strip a little way ahead (a driver looking for a space)
                for (int cand = 0; cand < 3 && tryHere && va.parking == 0; cand++) {
                    float spotU = d->u + d->info.frontLen + 30.f + cand * 13.f;   // front bumper of the parked car
                    if (spotU < L.u1 - 14.f &&
                        dot(laneGraph.laneTangent(d->path, d->u), laneGraph.laneTangent(d->path, spotU)) > 0.97f) {
                        float plat = L.width * 0.5f + info.shoulder * 0.5f;
                        vec3 spot = laneGraph.lanePos(d->path, spotU - spec.boxHalf.y, plat);
                        bool ok = !World::roadWorkZoneAt(spot.xy());
                        for (float bs : L.busStops)
                            if (fabsf(bs - spotU) < 22.f) ok = false;
                        // the strip has to be empty where the car eases over into it (16 m, plus its own length) and at the spot
                        if (ok) {
                            vec3 base = laneGraph.lanePos(d->path, d->u);
                            vec2 t0 = laneGraph.laneTangent(d->path, d->u), r0 = AI::rightOf(t0);
                            float span = spotU - d->u;
                            vec2 mid = base.xy() + t0 * (span * 0.5f);
                            float qr = span * 0.5f + 12.f;
                            int self = vi < (int)ai.vehBody.size() ? ai.vehBody[vi] : -1;
                            traffic.hash.query(traffic.bodies, mid - vec2(qr), mid + vec2(qr), [&](int bi) {
                                if (!ok || bi == self) return;
                                const AI::Body& ob = traffic.bodies[bi];
                                if (ob.kind != AI::BK_CAR || fabsf(ob.z - base.z) > 3.f) return;
                                vec2 rl = ob.pos - base.xy();
                                float al = dot(rl, t0), lt = dot(rl, r0);
                                if (al > Max(3.f, span - 16.f - spec.boxHalf.y * 2.f - ob.halfLen) && al < span + 3.f + ob.halfLen && lt > L.width * 0.5f - 0.4f &&
                                    lt < L.width * 0.5f + info.shoulder + 1.f)
                                    ok = false;
                            });
                        }
                        vec3 door;
                        if (ok && aiBuildingDoorNear(*this, spot.xy(), 30.f, hp, door)) {
                            va.parking = 1;
                            ai.lastParkArrive = time;
                            va.parkLane = d->path;
                            va.parkLat = plat;
                            va.parkDoor = door;
                            va.parkTimer = 0.f;
                            va.pullTimer = 0.f;
                            d->stopPath = d->path;
                            d->stopU = spotU;
                        }
                    }
                }
            }
        }
        if (va.parking == 1) {
            bool valid = d->mode == AI::DM_NORMAL && d->path == va.parkLane && d->stopPath == va.parkLane;
            va.parkTimer += dt;
            if (!valid || va.parkTimer > 50.f) {
                va.parking = 0;
                va.parkTimer = 0.f;
                if (d->stopPath == va.parkLane) d->stopPath = -1;
            } else if (d->stopU - (d->u + d->info.frontLen) < 16.f) {
                d->nudgeTarget = va.parkLat;   // ease over into the strip on the way to the spot
                d->nudgeTimer = 0.5f;
            }
        }
    }
    // ---- role behaviors (may change modes / stop points)
    switch (va.role) {
        case VR_TAXI: {
            if (va.fare >= 0) {
                bool fareValid = va.fare < (int)peds.size() && peds[va.fare].used && peds[va.fare].vehicle == vi;
                if (!fareValid) {
                    va.fare = -1;
                    d->hasDest = false;
                    d->mode = AI::DM_NORMAL;
                    break;
                }
                float toDest = length(vp.xy() - va.dest);
                if (d->mode == AI::DM_NORMAL || d->mode == AI::DM_ROUTE) {
                    if (!d->hasDest) {
                        traffic.setDestination(*d, va.dest);
                        d->mode = AI::DM_ROUTE;
                    }
                    if (toDest < 45.f) {
                        d->mode = AI::DM_PULLOVER;
                        d->holdTimer = -1.f;
                        va.stopTimer = 0.f;
                    }
                } else if (d->mode == AI::DM_PULLOVER) {
                    if (v.sim.speed() < 0.4f) va.stopTimer += dt;
                    if (va.stopTimer > 1.2f) {
                        int fare = va.fare;
                        removePedFromVehicle(fare, true);
                        peds[fare].brain.type = BRAIN_WANDER;
                        peds[fare].brain.edge = -1;
                        pedAI(fare).activity = ACT_WALK;
                        pedAI(fare).navOk = false;
                        va.fare = -1;
                        d->hasDest = false;
                        d->destEdges.clear();
                        d->mode = AI::DM_NORMAL;
                        d->nudgeTarget = 0.f;
                        va.taskTimer = 25.f;   // no new fare for a while
                    }
                }
            } else if (d->mode == AI::DM_PULLOVER && va.task == 1) {
                // waiting for a hailing ped to board
                va.stopTimer += dt;
                int hail = va.scene;
                bool valid = hail >= 0 && hail < (int)peds.size() && peds[hail].used && peds[hail].state == PS_ONFOOT && pedAI(hail).targetVeh == vi;
                if (!valid || va.stopTimer > 25.f) {
                    d->mode = AI::DM_NORMAL;
                    d->stopPath = -1;
                    va.task = 0;
                    va.scene = -1;
                } else if (peds[hail].state == PS_INVEHICLE) {
                    va.task = 0;
                }
            } else {
                va.taskTimer -= dt;
                // look for a pedestrian hailing on our side of the street ahead (checked twice a second)
                if (va.taskTimer <= 0.f && d->path >= 0 && d->path < (int)laneGraph.lanes.size() && !d->dummy) {
                    va.taskTimer = 0.5f;
                    const AI::Lane& L = laneGraph.lanes[d->path];
                    vec2 fwd = laneGraph.laneTangent(d->path, d->u);
                    vec2 rgt = AI::rightOf(fwd);
                    int best = -1;
                    float bestA = 1e9f;
                    for (int pi = 0; pi < (int)peds.size() && pi < (int)ai.ped.size(); pi++) {
                        const Ped& q = peds[pi];
                        if (!q.used || q.state != PS_ONFOOT || ai.ped[pi].uid != q.uid || ai.ped[pi].activity != ACT_HAIL_TAXI || ai.ped[pi].targetVeh >= 0) continue;
                        vec2 rel2 = q.pos.toVec3().xy() - vp.xy();
                        float along = dot(rel2, fwd), side = dot(rel2, rgt);
                        if (along < 18.f || along > 80.f || side < 0.f || side > L.width * 0.5f + 12.f) continue;
                        if (along < bestA) {
                            bestA = along;
                            best = pi;
                        }
                    }
                    if (best >= 0 && L.right < 0) {
                        float u = 0.f;
                        vec2 qp = peds[best].pos.toVec3().xy();
                        u = laneGraph.projectPath(d->path, qp, d->u + bestA, nullptr);
                        if (u < L.u1 - 6.f) {
                            d->mode = AI::DM_PULLOVER;
                            d->holdTimer = -1.f;
                            d->stopPath = d->path;
                            d->stopU = u + 2.f;
                            va.task = 1;
                            va.scene = best;
                            va.stopTimer = 0.f;
                            pedAI(best).targetVeh = vi;
                        }
                    }
                }
            }
            break;
        }
        case VR_BUS: {
            // boarding/alighting while held at a stop
            if (d->mode == AI::DM_HOLD) {
                va.stopTimer += dt;
                if (va.stopTimer > 1.0f && va.stopTimer - dt <= 1.0f) {
                    // waiting passengers board, one or two get off
                    std::vector<int> near_;
                    pedsNear(vp.xy(), 16.f, near_);
                    for (int pi : near_) {
                        if (pi >= (int)ai.ped.size() || ai.ped[pi].uid != peds[pi].uid) continue;
                        if (ai.ped[pi].activity == ACT_WAIT_BUS && !peds[pi].persistent) {
                            ai.ped[pi].activity = ACT_ENTER_VEH;
                            ai.ped[pi].targetVeh = vi;
                        }
                    }
                }
            } else {
                va.stopTimer = 0.f;
            }
            break;
        }
        case VR_AMBULANCE:
        case VR_FIRETRUCK: {
            // run to the incident with the siren, then stop and let the crew work
            Incident* inc = va.scene >= 0 && va.scene < (int)ai.incidents.size() && ai.incidents[va.scene].active ? &ai.incidents[va.scene] : nullptr;
            if (va.task == 0 && inc) {
                if (d->mode != AI::DM_EMERGENCY) {
                    d->mode = AI::DM_EMERGENCY;
                    traffic.setDestination(*d, inc->pos.toVec3().xy());
                }
                v.sirenOn = true;
                v.sirenSilent = false;
                // held up in traffic: within a short run of the scene the crew goes the rest of the way on foot; further
                // off and out of the player's sight, on past the hold-up along the route (as with the siren going it
                // would have got round it)
                float toScene = length(vp.xy() - inc->pos.toVec3().xy());
                // (held up: stopped or crawling in stop-and-go traffic - the time builds while slower than 3 m/s and wears
                //  off while moving faster)
                va.heldUp = Clamp(va.heldUp + (v.sim.speed() < 3.f ? dt : -dt), 0.f, 10.f);
                if (va.heldUp > 3.f && v.sim.speed() < 1.f && toScene < 85.f) {
                    va.heldUp = 0.f;
                    d->mode = AI::DM_PULLOVER;
                    d->holdTimer = -1.f;
                    va.task = 1;
                    va.taskTimer = 0.f;
                    va.stopTimer = 0.f;
                    LOG("population: %s %d held up %.0f m from the scene, the crew goes on foot", va.role == VR_AMBULANCE ? "ambulance" : "fire truck", vi, toScene);
                } else if (va.heldUp > 5.f && !d->dummy && !inView && plD > 60.f) {
                    va.heldUp = 0.f;
                    float hl = vassets[v.model].spec.boxHalf.y;
                    for (float ahead = 12.f; ahead <= 66.f; ahead += 6.f) {
                        int pth = -1;
                        float uu = 0.f;
                        if (!traffic.liftPoint(*d, ahead, pth, uu) || !laneGraph.isLane(pth)) continue;
                        if (!traffic.laneFree(pth, uu, hl + 1.f, 4.f)) continue;
                        vec3 c = laneGraph.lanePos(pth, uu);
                        if (inCameraView(c, 12.f) || (pl && length(c.xy() - pl->pos.toVec3().xy()) < 50.f)) break;
                        vec2 t = laneGraph.laneTangent(pth, uu);
                        Vehicles::resetVehicle(v.sim, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t));
                        v.sim.body.vel = vec3(t * 6.f, 0.f);
                        traffic.toPhysics(vi, v.sim);
                        d->path = pth;
                        d->u = uu;
                        traffic.clearRoute(*d);
                        LOG("population: %s %d past a hold-up in traffic, %.0f m on (%.0f m from the scene)", va.role == VR_AMBULANCE ? "ambulance" : "fire truck", vi, ahead,
                            length(c.xy() - inc->pos.toVec3().xy()));
                        break;
                    }
                }
                if (va.task == 0 && toScene < 32.f) {
                    d->mode = AI::DM_PULLOVER;
                    d->holdTimer = -1.f;
                    va.task = 1;
                    va.taskTimer = 0.f;
                    va.stopTimer = 0.f;
                }
            } else if (va.task == 1) {
                va.taskTimer += dt;
                v.sirenOn = true;
                v.sirenSilent = va.taskTimer > 6.f;   // on scene: the light bar keeps flashing, the siren stops
                if (v.sim.speed() < 0.5f && va.taskTimer > 1.5f && va.stopTimer < 1.f) {
                    // crew gets out and attends the scene (once stopped)
                    va.stopTimer = 1.f;
                    for (int s = 0; s < 8; s++) {
                        int c = v.seats[s];
                        if (c < 0 || s == 0) continue;
                        removePedFromVehicle(c, true);
                        PedAI& ca = pedAI(c);
                        ca.activity = ACT_EVENT;
                        ca.homeVeh = vi;
                        ca.anchor = inc ? inc->pos.toVec3().xy() : vp.xy();
                        ca.actTimer = 14.f + hashToFloat(hash32(peds[c].uid)) * 6.f + length(ca.anchor - vp.xy()) / 2.4f;   // (+ the run there)
                        peds[c].brain.type = BRAIN_WANDER;
                    }
                }
                // everyone back in (and the patient they are taking in): leave
                bool crewOut = false;
                for (int pi = 0; pi < (int)ai.ped.size() && pi < (int)peds.size(); pi++) {
                    if (!peds[pi].used || ai.ped[pi].uid != peds[pi].uid || peds[pi].health <= 0.f) continue;
                    if (ai.ped[pi].homeVeh == vi && peds[pi].vehicle != vi) crewOut = true;
                    if (ai.ped[pi].activity == ACT_ENTER_VEH && ai.ped[pi].targetVeh == vi) crewOut = true;
                }
                if (va.taskTimer > 30.f && (!crewOut || va.taskTimer > 100.f)) {   // (not for ever)
                    va.task = 2;
                    v.sirenOn = false;
                    v.sirenSilent = false;
                    d->mode = AI::DM_NORMAL;
                    d->hasDest = false;
                    if (inc) inc->active = false;
                }
            } else {
                v.sirenOn = false;
                if (d->mode == AI::DM_EMERGENCY) d->mode = AI::DM_NORMAL;
            }
            break;
        }
        default: break;
    }
    // ---- dummy mode switching (far, unseen, calm)
    bool calm = d->mode == AI::DM_NORMAL || d->mode == AI::DM_ROUTE;
    if (!d->dummy) {
        bool onLane = d->path >= 0 && d->path < (int)laneGraph.lanes.size();
        bool can = !inView && camD > kDummyFar && plD > kDummyFar && calm && onLane && d->recoverTimer <= 0.f && !v.sirenOn && v.sim.up().z > 0.8f &&
                   v.sim.speed() < 40.f && v.sim.health > 300.f;
        va.offView = can ? va.offView + dt : 0.f;
        if (va.offView > 0.8f) {
            traffic.toDummy(vi, v.sim);
            va.offView = 0.f;
        }
    } else {
        if (inView || camD < kDummyNear || plD < kDummyNear || !calm) traffic.toPhysics(vi, v.sim);
    }
    // ---- drive
    AI::DriveOut out;
    traffic.drive(vi, v.sim, dt, out);
    v.ctl = out.ctl;
    v.indicator = out.indicator;
    v.hornOn = out.horn || (d->mode == AI::DM_FLEE && ((int)(time * 3.0 + v.uid) % 4 == 0) && v.sim.speed() > 3.f);
    // ---- parking: blinker while easing in; stopped at the spot -> engine off, out, round the car and away
    if (va.parking == 1) {
        v.indicator = 1;
        bool atSpot = d->stopU - (d->u + d->info.frontLen) < 3.f && v.sim.speed() < 0.3f;
        va.pullTimer = atSpot ? va.pullTimer + dt : 0.f;
        if (va.pullTimer > 0.8f) {
            va.parking = 0;
            va.pullTimer = -1.f;   // (population.cpp: nobody else drives this one off)
            d->stopPath = -1;
            v.parked = true;
            v.sim.engineOn = false;
            v.lightsOn = false;
            v.indicator = 0;
            v.ctl = Vehicles::VehicleControls();
            v.ctl.handbrake = true;
            removePedFromVehicle(drv, true);
            traffic.detach(vi);
            va.managed = false;
            PedAI& da = pedAI(drv);
            da.activity = ACT_LEAVE_CAR;
            da.homeVeh = vi;
            da.anchor = va.parkDoor.xy();
            da.actTimer = 40.f;
            da.navOk = false;
            dp.brain.type = BRAIN_WANDER;
            dp.brain.edge = -1;
            ai.stats.arrivals++;
            return;
        }
    }
    // ---- fleeing driver boxed in: bail out and run
    if (out.wantsAbandon && b.type == BRAIN_FLEE) {
        removePedFromVehicle(drv, true);
        b.type = BRAIN_FLEE;
        b.timer = 0.f;
        aiSay(drv, BK_FLEE, 0.8f);
        traffic.detach(vi);
        va.managed = false;
        v.ctl = Vehicles::VehicleControls();
        return;
    }
    // ---- hung up on a ledge or kerb (a wheel pair off the ground, nose tilted) and going nowhere although it wants to:
    // back onto the lane a few meters on, out of view, or after a long while even in view (better than a jam)
    bool hung = !d->dummy && d->vTarget > 1.f && v.sim.speed() < 0.5f && (v.sim.up().z < 0.94f || v.sim.wheelsOnGround < Min(3, v.sim.wheelCount)) && d->path >= 0;
    va.hungTime = hung ? va.hungTime + dt : 0.f;
    if (va.hungTime > 5.f && (!inView || camD > 70.f || va.hungTime > 20.f)) {
        float u = Min(d->u + 4.f, laneGraph.pathLength(d->path) - 0.5f);
        vec3 p = laneGraph.pathPos(d->path, u);
        vec2 t = laneGraph.pathTangent(d->path, u);
        Vehicles::resetVehicle(v.sim, dvec3(p.x, p.y, p.z + 0.35f), AI::dirYaw(t));
        v.sim.body.vel = vec3(t * 2.f, 0.f);
        d->u = u;
        d->stuckTime = 0.f;
        d->recoverTimer = 0.f;
        va.hungTime = 0.f;
        ai.stats.unhung++;
    }
    // ---- stuck again and again at the same spot (a wall or post the path runs too close to): out of view, lift it
    // a few meters on along its route
    int pth = -1;
    float uu = 0.f;
    if (d->stuckRepeats >= 3 && d->recoverTimer <= 0.f && (!inView || camD > 70.f) && traffic.liftPoint(*d, 9.f, pth, uu)) {
        vec3 p = laneGraph.pathPos(pth, uu);
        vec2 t = laneGraph.pathTangent(pth, uu);
        Vehicles::resetVehicle(v.sim, dvec3(p.x, p.y, p.z + 0.35f), AI::dirYaw(t));
        v.sim.body.vel = vec3(t * 2.f, 0.f);
        traffic.relocalize(*d, p.xy(), t, 10.f);
        d->stuckRepeats = 0;
        d->stuckPath = -1;
        d->stuckAt = vec2(1e9f);
        d->stuckTime = 0.f;
        ai.stats.unhung++;
    }
    // ---- flipped or hopelessly stuck
    if (d->flipTime > 3.f || d->lostTime > 6.f) {
        if (!inView && camD > 50.f && d->path >= 0) {
            vec3 p = laneGraph.pathPos(d->path, d->u);
            vec2 t = laneGraph.pathTangent(d->path, d->u);
            Vehicles::resetVehicle(v.sim, dvec3(p.x, p.y, p.z + 0.3f), AI::dirYaw(t));
            d->flipTime = 0.f;
            d->lostTime = 0.f;
            d->recoverTimer = 0.f;
        } else if (d->flipTime > 3.f) {
            // visible: the driver climbs out and walks away
            removePedFromVehicle(drv, true);
            dp.brain.type = BRAIN_WANDER;
            dp.brain.edge = -1;
            aiSay(drv, BK_CRASH, 0.7f);
            traffic.detach(vi);
            va.managed = false;
            return;
        }
    }
    // ---- honking / shouting at whoever blocks the road
    if (out.blocker >= 0 && out.blocker < (int)traffic.bodies.size()) {
        const AI::Body& bl = traffic.bodies[out.blocker];
        bool playerBlock = (bl.flags & AI::BF_PLAYER) != 0;
        if (playerBlock && out.horn && va.hornBarkTimer <= 0.f && plD < 25.f) {
            aiSay(drv, BK_HONK, 0.6f);
            va.hornBarkTimer = 8.f;
        }
    }
    (void)spec;
}

// ------------------------------------------------------------------------------------------------------------------
// Boat helm: steer toward a target over water, keep off the shore, throttle by distance.
void GameWorld::aiDriveBoat(int vi, float dt, dvec3 target, float speed) {
    Vehicle& v = vehicles[vi];
    Vehicles::VehicleControls& c = v.ctl;
    c = Vehicles::VehicleControls();
    vec3 p = v.sim.body.pos.toVec3();
    vec3 f = v.sim.forward();
    vec2 fwd = normalize(f.xy() + vec2(1e-5f, 0.f));
    vec2 to = target.toVec3().xy() - p.xy();
    float dist = length(to);
    vec2 want = dist > 1e-3f ? to / dist : fwd;
    // shore avoidance: probe fan ahead, steer toward the open side
    float spd = v.sim.speed();
    float probe = 18.f + spd * 2.5f;
    auto land = [&](vec2 dir, float r) {
        for (int k = 1; k <= 4; k++) {
            vec2 q = p.xy() + dir * (r * k / 4.f);
            if (!map->isWater(q.x, q.y) || map->waterAt(q.x, q.y) - map->heightAt(q.x, q.y) < 1.2f) return true;
        }
        return false;
    };
    if (land(fwd, probe)) {
        vec2 l = rotate(fwd, 0.7f), r = rotate(fwd, -0.7f);
        bool ll = land(l, probe * 0.8f), rr = land(r, probe * 0.8f);
        want = !ll ? l : (!rr ? r : -fwd);
    }
    float err = AI::wrapPi(atan2f(want.y, want.x) - atan2f(fwd.y, fwd.x));   // + = target to the left
    c.steer = Clamp(-err * 1.6f, -1.f, 1.f);
    float targetSpeed = Min(speed, dist * 0.25f + 2.f);
    if (fabsf(err) > 1.4f) targetSpeed = Min(targetSpeed, 4.f);
    float fs = v.sim.forwardSpeed();
    if (fs < targetSpeed) c.throttle = Saturate((targetSpeed - fs) * 0.3f + 0.25f);
    else if (fs > targetSpeed + 3.f) c.brake = Saturate((fs - targetSpeed) * 0.15f);
    (void)dt;
}

// Helicopter autopilot: altitude hold with the collective, cyclic tilt toward the desired velocity (orbit or go-to),
// yaw to face the target. Works with the flight model's attitude targets (vehicle_sim_air.cpp).
void GameWorld::aiFlyHeli(int vi, float dt, dvec3 target, float altitude, float orbitRadius, bool orbit) {
    Vehicle& v = vehicles[vi];
    Vehicles::VehicleControls& c = v.ctl;
    c = Vehicles::VehicleControls();
    const Vehicles::VehicleState& s = v.sim;
    vec3 p = s.body.pos.toVec3();
    vec3 tp = target.toVec3();
    vec2 to = tp.xy() - p.xy();
    float dist = length(to);
    vec2 dir = dist > 1e-3f ? to / dist : vec2(0, 1);
    // the target's ground velocity (smoothed): flown along with, so a fast car chase does not leave us behind
    VehAI& va = vehAI(vi);
    if (!va.flyTgtInit || length(tp - va.flyTgtPrev) > 80.f) {
        va.flyTgtPrev = tp;
        va.flyTgtVel = vec2(0.f);
        va.flyTgtInit = true;
    }
    if (dt > 1e-4f) {
        vec2 inst = (tp.xy() - va.flyTgtPrev.xy()) / dt;
        va.flyTgtVel += (inst - va.flyTgtVel) * expDecay(2.5f, dt);
        if (length(va.flyTgtVel) > 60.f) va.flyTgtVel = normalize(va.flyTgtVel) * 60.f;
    }
    va.flyTgtPrev = tp;
    vec2 vd;
    if (orbit && dist < orbitRadius * 2.5f) {
        // tangential flight around the target, pulled onto the circle (the circle moves with the target)
        vec2 tang(-dir.y, dir.x);
        float radial = dist - orbitRadius;
        vd = tang * 14.f + dir * Clamp(radial * 0.25f, -8.f, 12.f) + va.flyTgtVel;
    } else {
        float sp = Clamp(dist * 0.35f, 0.f, 45.f);
        vd = dir * sp + (orbit ? va.flyTgtVel : vec2(0.f));
    }
    if (length(vd) > 62.f) vd = normalize(vd) * 62.f;
    vec3 fw = s.forward();
    vec2 fh = normalize(fw.xy() + vec2(1e-5f, 0.f));
    vec2 rh = AI::rightOf(fh);
    vec2 vel = s.body.vel.xy();
    float aF = Clamp((dot(vd, fh) - dot(vel, fh)) * 0.6f, -6.f, 6.f);
    float aR = Clamp((dot(vd, rh) - dot(vel, rh)) * 0.6f, -6.f, 6.f);
    const float maxTilt = 0.52f;
    c.pitch = Clamp(atanf(-aF / 9.81f) / maxTilt, -1.f, 1.f);
    c.roll = Clamp(atanf(aR / 9.81f) / maxTilt, -1.f, 1.f);
    if (fabsf(c.pitch) < 0.05f) c.pitch = c.pitch < 0.f ? -0.05f : 0.05f;
    // altitude: keep above the ground and the roofs under us and along the way (fast pursuits over downtown)
    float ground = map->heightAt(p.x, p.y);
    float alt = Max(altitude, ground + 35.f);
    for (int k = 0; k < 3; k++) {
        vec2 q = p.xy() + s.body.vel.xy() * (1.5f * k);
        alt = Max(alt, skylineAt(*this, q, 15.f) + 25.f);
    }
    float vz = Clamp((alt - p.z) * 0.5f, -5.f, 7.f);
    c.lift = vz > 0.f ? vz / 8.f : vz / 6.f;
    c.lift = Clamp(c.lift + 0.02f, -1.f, 1.f);
    // face the target (the searchlight / sniper look at it)
    float wantYaw = atan2f(-to.x, to.y);
    float err = AI::wrapPi(wantYaw - atan2f(-fh.x, fh.y));
    c.yaw = Clamp(-err * 1.2f, -1.f, 1.f);
    c.throttle = 0.f;
    (void)dt;
}

}  // namespace Game
