// AI brains for non-player peds: wander, scenario, flee, cower, combat, arrest, follow, go-to, and a road-following
// driver. Population management (spawning traffic and pedestrians) lives in population.cpp.
#include "gameworld.h"

namespace Game {

namespace ai_detail {

float wrapAng(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

void faceTowards(Ped& p, vec2 dir, float rate, float dt) {
    if (length2(dir) < 1e-6f) return;
    float ty = atan2f(-dir.x, dir.y);
    float d = wrapAng(ty - p.yaw);
    p.yaw = wrapAng(p.yaw + Clamp(d, -rate * dt, rate * dt));
}

}  // namespace ai_detail

using namespace ai_detail;

void GameWorld::updateAI(float dt) {
    updatePopulation(dt);
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.isPlayer || p.brain.type == BRAIN_NONE) continue;
        if (p.state == PS_DEAD || p.state == PS_RAGDOLL || p.state == PS_GETUP) continue;
        updateBrain(i, dt);
    }
}

void GameWorld::updateBrain(int id, float dt) {
    Ped& p = peds[id];
    Brain& b = p.brain;
    b.timer += dt;
    b.thinkTimer -= dt;
    vec3 pos = p.pos.toVec3();
    // Drivers are handled by the vehicle AI (their vehicle's controls); passengers idle.
    if (p.state == PS_INVEHICLE) {
        if (b.type == BRAIN_DRIVER && p.seat == 0) driveVehicleAI(p.vehicle, dt);
        else if (b.type == BRAIN_FLEE && p.vehicle >= 0 && p.seat == 0) driveVehicleAI(p.vehicle, dt);
        else if (b.type == BRAIN_COMBAT && p.target_is_valid(*this)) {
            // shoot from the vehicle if armed
            const Ped& t = peds[b.target];
            if (p.weapon != WPN_FISTS && p.fireTimer <= 0.f && length(t.pos.toVec3() - pos) < 45.f) {
                vec3 head = pedHeadPos(p);
                vec3 d = normalize(pedChestPos(t) - head);
                if (lineOfSight(dvec3(head), dvec3(pedChestPos(t)), id, p.vehicle)) fireWeapon(id, dvec3(head + d * 0.6f), d);
            }
            if (p.seat == 0) driveVehicleAI(p.vehicle, dt);
        }
        return;
    }
    if (p.state == PS_ENTERING || p.state == PS_EXITING) {
        movePed(p, vec2(0, 0), dt, false);
        return;
    }
    vec2 desired(0, 0);
    bool jump = false;
    p.aiming = false;
    p.firing = false;
    switch (b.type) {
        case BRAIN_WANDER: {
            // follow the sidewalk along the current road edge; pick a new edge at the ends
            if (b.edge < 0 || b.edge >= (int)roads->edges.size()) {
                float s = 0, side = 0;
                b.edge = roads->nearestEdge(pos.xy(), 60.f, &s, nullptr, &side);
                b.edgeS = s;
                b.side = side >= 0 ? 1.f : -1.f;
                b.edgeDir = (hash32(p.uid) & 1) ? 1 : -1;
                if (b.edge < 0) {
                    b.type = BRAIN_SCENARIO;
                    break;
                }
            }
            const World::RoadEdge& e = roads->edges[b.edge];
            b.edgeS += b.speed * dt * (float)b.edgeDir;
            bool atEnd = b.edgeS < 0.f || b.edgeS > e.length;
            if (atEnd) {
                int node = b.edgeS < 0.f ? e.n0 : e.n1;
                const World::RoadNode& n = roads->nodes[node];
                int next = b.edge;
                if (!n.edges.empty()) {
                    int pick = (int)(hash32(p.uid + (u32)(time * 10.0)) % n.edges.size());
                    for (int k = 0; k < (int)n.edges.size(); k++) {
                        int cand = n.edges[(pick + k) % n.edges.size()];
                        if (cand != b.edge && roads->edges[cand].sidewalk > 0.f) {
                            next = cand;
                            break;
                        }
                    }
                }
                const World::RoadEdge& ne = roads->edges[next];
                if (next == b.edge) b.edgeDir = -b.edgeDir;
                else {
                    b.edge = next;
                    b.edgeDir = ne.n0 == node ? 1 : -1;
                }
                const World::RoadEdge& ce = roads->edges[b.edge];
                b.edgeS = b.edgeDir > 0 ? 0.5f : ce.length - 0.5f;
            }
            const World::RoadEdge& ce = roads->edges[b.edge];
            float s = Clamp(b.edgeS, 0.f, ce.length);
            vec3 c = ce.posAt(s);
            vec3 t = ce.tangentAt(s);
            vec2 n = normalize(vec2(-t.y, t.x));
            float off = ce.halfWidth + Max(ce.sidewalk, 1.2f) * 0.55f;
            vec2 target = c.xy() + n * (off * b.side) + vec2(t.x, t.y) * (2.5f * (float)b.edgeDir);
            vec2 to = target - pos.xy();
            float dist = length(to);
            if (dist > 0.3f) desired = to / dist * b.speed;
            faceTowards(p, desired, 5.f, dt);
            // idle pauses (phone, look around)
            if (b.thinkTimer <= 0.f) {
                b.thinkTimer = 8.f + (hash32(p.uid + (u32)time) % 20);
                if (hash32(p.uid * 3 + (u32)time) % 9 == 0) {
                    b.type = BRAIN_SCENARIO;
                    b.scenario = 8;  // phone
                    b.timer = 0.f;
                    b.sub = 1;       // resume wandering afterwards
                }
            }
            break;
        }
        case BRAIN_SCENARIO: {
            p.animIn.stance = b.scenario >= 0 ? b.scenario : 0;
            if (b.sub == 1 && b.timer > 10.f) {
                b.type = BRAIN_WANDER;
                p.animIn.stance = 0;
                b.timer = 0.f;
            }
            break;
        }
        case BRAIN_FLEE: {
            vec3 from = b.target >= 0 && peds[b.target].used ? peds[b.target].pos.toVec3() : b.goal.toVec3();
            vec2 away = pos.xy() - from.xy();
            float d = length(away);
            if (d < 1e-3f) away = vec2(1, 0);
            else away = away / d;
            // slight wobble so crowds scatter
            float wob = sinf((float)time * 1.3f + p.uid) * 0.4f;
            vec2 dir = normalize(away + vec2(-away.y, away.x) * wob);
            desired = dir * 6.2f;
            faceTowards(p, desired, 9.f, dt);
            p.animIn.stance = 0;
            if (b.timer > 18.f && d > 60.f) {
                b.type = BRAIN_WANDER;
                b.edge = -1;
                b.timer = 0.f;
                b.speed = 1.4f;
            }
            break;
        }
        case BRAIN_COWER: {
            p.animIn.stance = 4;
            if (b.timer > 12.f) {
                b.type = BRAIN_FLEE;
                b.timer = 0.f;
            }
            break;
        }
        case BRAIN_ARREST:
        case BRAIN_COMBAT: {
            if (!p.target_is_valid(*this)) {
                b.type = p.faction == FAC_POLICE ? BRAIN_WANDER : BRAIN_WANDER;
                b.edge = -1;
                p.animIn.stance = 0;
                break;
            }
            Ped& t = peds[b.target];
            vec3 tp = t.pos.toVec3();
            vec2 to = tp.xy() - pos.xy();
            float dist = length(to);
            const WeaponInfo& wi = weaponInfo(p.weapon);
            float prefer = wi.clipSize == 0 ? 1.1f : Clamp(wi.range * 0.35f, 6.f, 25.f);
            bool los = b.thinkTimer > 0.f ? b.alerted : lineOfSight(p.pos + dvec3(0, 0, 1.6), t.pos + dvec3(0, 0, 1.3), id, t.vehicle);
            if (b.thinkTimer <= 0.f) {
                b.thinkTimer = 0.25f;
                b.alerted = los;
            }
            if (dist > prefer * 1.2f || !los) {
                desired = to / Max(dist, 1e-3f) * (dist > 20.f ? 5.5f : 3.6f);
            } else if (dist < prefer * 0.5f && wi.clipSize > 0) {
                desired = -to / Max(dist, 1e-3f) * 2.5f;
            } else if (wi.clipSize > 0) {
                // strafe
                vec2 side(-to.y, to.x);
                side = normalize(side) * ((((u32)(time * 0.5) + p.uid) & 1) ? 1.8f : -1.8f);
                desired = side;
            }
            faceTowards(p, to, 8.f, dt);
            if (wi.clipSize > 0) {
                p.aiming = dist < wi.range && los;
                if (p.aiming && p.fireTimer <= 0.f && p.reloadTimer <= 0.f) {
                    float burst = fmodf((float)time * (0.6f + p.brain.aggression * 0.6f) + p.uid * 0.37f, 2.f);
                    if (burst < 1.1f) {
                        quat q = quatAxisAngle(vec3(0, 0, 1), p.yaw);
                        vec3 hand = pos + rotate(q, p.bones[Anim::B_HAND_R].c[3].xyz());
                        vec3 aimAt = pedChestPos(t) + vec3(0, 0, (hash32(p.uid + (u32)(time * 7)) % 100) * 0.004f - 0.2f);
                        vec3 d = normalize(aimAt - hand);
                        fireWeapon(id, dvec3(hand + d * 0.3f), d);
                    }
                }
                if (p.clip[p.weapon] <= 0 && p.ammo[p.weapon] <= 0) p.ammo[p.weapon] = weaponInfo(p.weapon).clipSize * 3;  // NPCs carry spare magazines
            } else if (dist < 1.4f && p.meleeTimer <= 0.f) {
                vec3 f(-sinf(p.yaw), cosf(p.yaw), 0);
                fireWeapon(id, p.pos + dvec3(0, 0, 1.2), f);
                p.meleeTimer = wi.fireInterval + 0.3f;
            }
            p.animIn.stance = 0;
            break;
        }
        case BRAIN_FOLLOW: {
            if (b.target < 0 || !peds[b.target].used) {
                b.type = BRAIN_NONE;
                break;
            }
            Ped& t = peds[b.target];
            if (t.state == PS_INVEHICLE && t.vehicle >= 0 && p.state == PS_ONFOOT) {
                int seat = freeSeat(t.vehicle, false);
                if (seat > 0) {
                    vec3 vp = vehicles[t.vehicle].sim.body.pos.toVec3();
                    if (length(vp - pos) < 4.f) warpPedIntoVehicle(id, t.vehicle, seat);
                    else desired = normalize(vp.xy() - pos.xy()) * 5.f;
                }
            } else {
                vec2 to = t.pos.toVec3().xy() - pos.xy();
                float dist = length(to);
                if (dist > 2.5f) desired = to / dist * (dist > 8.f ? 5.5f : (dist > 4.f ? 3.5f : 1.6f));
            }
            faceTowards(p, desired, 8.f, dt);
            break;
        }
        case BRAIN_GOTO: {
            vec2 to = b.goal.toVec3().xy() - pos.xy();
            float dist = length(to);
            if (dist > 0.6f) desired = to / dist * Min(b.speed, dist * 2.f + 0.5f);
            faceTowards(p, desired, 8.f, dt);
            break;
        }
        default: break;
    }
    movePed(p, desired, dt, jump);
}

bool Ped::target_is_valid(const GameWorld& g) const {
    return brain.target >= 0 && brain.target < (int)g.peds.size() && g.peds[brain.target].used && g.peds[brain.target].health > 0.f;
}

// ------------------------------------------------------------------------------------------------------------------
// Road-following driver: pure pursuit on the lane polyline, speed control, simple obstacle and signal handling.
void GameWorld::driveVehicleAI(int vi, float dt) {
    if (vi < 0 || !vehicles[vi].used) return;
    Vehicle& v = vehicles[vi];
    Vehicles::VehicleState& s = v.sim;
    Vehicles::VehicleControls& c = v.ctl;
    c = Vehicles::VehicleControls();
    vec3 pos = s.body.pos.toVec3();
    vec3 fwd = s.forward();
    int driver = v.seats[0];
    Brain* br = driver >= 0 ? &peds[driver].brain : nullptr;
    bool fleeing = br && (br->type == BRAIN_FLEE);
    if (br && br->type == BRAIN_COMBAT && driver >= 0 && peds[driver].target_is_valid(*this)) {
        // pursuit: drive straight at the (predicted) target, ignoring lanes
        const Ped& t = peds[br->target];
        vec3 tp = t.vehicle >= 0 ? vehicles[t.vehicle].sim.body.pos.toVec3() + vehicles[t.vehicle].sim.body.vel * 0.8f : t.pos.toVec3();
        vec2 to = tp.xy() - pos.xy();
        float dist = length(to);
        float headErr = wrapAng(atan2f(-to.x, to.y) - atan2f(-fwd.x, fwd.y));
        float fs = s.forwardSpeed();
        c.steer = Clamp(-headErr * 2.2f + s.body.angVel.z * 0.1f, -1.f, 1.f);
        float targetSpeed = dist > 60.f ? 40.f : (dist > 14.f ? dist * 0.7f : 0.f);
        targetSpeed *= 1.f - Saturate(fabsf(headErr) - 0.3f) * 0.6f;
        if (fabsf(headErr) > 1.9f && dist < 30.f) {
            // target behind: reverse-turn
            c.brake = 1.f;
            c.steer = -c.steer;
            return;
        }
        float err = targetSpeed - fs;
        if (err > 0.f) c.throttle = Saturate(err * 0.3f + 0.2f);
        else c.brake = Saturate(-err * 0.25f);
        if (dist < 14.f && fs < 1.f) c.brake = 1.f;
        v.laneEdge = -1;
        return;
    }
    if (v.laneEdge < 0) {
        float sEdge = 0, side = 0;
        int e = roads->nearestEdge(pos.xy(), 40.f, &sEdge, nullptr, &side);
        if (e < 0) {
            c.brake = 1.f;
            return;
        }
        const World::RoadEdge& ed = roads->edges[e];
        vec3 t = ed.tangentAt(sEdge);
        v.laneEdge = e;
        v.laneDir = dot(vec2(t.x, t.y), fwd.xy()) >= 0.f ? 1 : -1;
        v.laneS = sEdge;
        v.laneIndex = 0;
        v.nextEdge = -1;
    }
    const World::RoadEdge& e = roads->edges[v.laneEdge];
    const World::RoadClassInfo& info = World::roadInfo(e.cls);
    // progress along the lane: project position
    float sEdge = 0;
    {
        float bestD = 1e30f;
        float acc = 0.f;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), b = e.pts[k + 1].xy();
            float t;
            float d = distPointSegment2D(pos.xy(), a, b, &t);
            float segLen = length(b - a);
            if (d < bestD) {
                bestD = d;
                sEdge = acc + t * segLen;
            }
            acc += segLen;
        }
    }
    v.laneS = sEdge;
    int lanes = v.laneDir > 0 ? e.lanesF : e.lanesB;
    int lane = Clamp(v.laneIndex, 0, Max(lanes - 1, 0));
    float laneOff = info.median * 0.5f + info.laneWidth * (lane + 0.5f);
    if (e.flags & World::RF_ONEWAY) laneOff = -e.halfWidth + info.laneWidth * (lane + 0.5f) + info.shoulder;
    float spd = s.speed();
    float look = 5.f + spd * 0.55f;
    float sAhead = sEdge + look * (float)v.laneDir;
    vec2 target;
    bool nextSeg = sAhead < 0.f || sAhead > e.length;
    if (nextSeg) {
        // choose the next edge at the upcoming node
        int node = v.laneDir > 0 ? e.n1 : e.n0;
        if (v.nextEdge < 0) {
            const World::RoadNode& n = roads->nodes[node];
            int best = -1;
            u32 h = hash32(v.uid * 31u + (u32)v.laneEdge * 7u);
            int cnt = (int)n.edges.size();
            for (int k = 0; k < cnt; k++) {
                int cand = n.edges[(h + k) % cnt];
                if (cand == v.laneEdge) continue;
                const World::RoadEdge& ce = roads->edges[cand];
                int dir = ce.n0 == node ? 1 : -1;
                if ((ce.flags & World::RF_ONEWAY) && dir < 0) continue;
                if (ce.cls == World::RC_DIRT && e.cls != World::RC_DIRT) continue;
                best = cand;
                break;
            }
            if (best < 0) best = v.laneEdge;  // dead end: U-turn
            v.nextEdge = best;
            const World::RoadEdge& ne = roads->edges[best];
            v.nextDir = best == v.laneEdge ? -v.laneDir : (ne.n0 == node ? 1 : -1);
        }
        const World::RoadEdge& ne = roads->edges[v.nextEdge];
        float over = v.laneDir > 0 ? sAhead - e.length : -sAhead;
        float ns = v.nextDir > 0 ? Min(over, ne.length) : Max(ne.length - over, 0.f);
        vec3 c3 = ne.posAt(ns);
        vec3 t3 = ne.tangentAt(ns) * (float)v.nextDir;
        const World::RoadClassInfo& ni = World::roadInfo(ne.cls);
        float noff = ni.median * 0.5f + ni.laneWidth * 0.5f;
        if (ne.flags & World::RF_ONEWAY) noff = -ne.halfWidth + ni.laneWidth * 0.5f + ni.shoulder;
        target = c3.xy() + vec2(t3.y, -t3.x) * noff;
        // switch edges once past the node
        if ((v.laneDir > 0 && sEdge > e.length - 1.f) || (v.laneDir < 0 && sEdge < 1.f)) {
            v.laneEdge = v.nextEdge;
            v.laneDir = v.nextDir;
            v.nextEdge = -1;
            v.laneIndex = 0;
        }
    } else {
        vec3 c3 = e.posAt(sAhead);
        vec3 t3 = e.tangentAt(sAhead) * (float)v.laneDir;
        target = c3.xy() + vec2(t3.y, -t3.x) * laneOff;  // right-hand traffic: offset to the right of travel
    }
    // steering: heading error to the pursuit target
    vec2 to = target - pos.xy();
    float headErr = wrapAng(atan2f(-to.x, to.y) - atan2f(-fwd.x, fwd.y));
    float yawRate = s.body.angVel.z;
    c.steer = Clamp(-headErr * 2.4f + yawRate * 0.12f, -1.f, 1.f);
    // speed target
    float limit = Min(info.speed, v.cruiseSpeed * (e.cls == World::RC_HIGHWAY ? 2.2f : 1.f));
    if (fleeing) limit *= 1.6f;
    limit *= 1.f - Saturate(fabsf(headErr) - 0.15f) * 0.7f;
    // obstacles: vehicles and peds ahead in the path
    float obstacle = 1e9f;
    for (int oi = 0; oi < (int)vehicles.size(); oi++) {
        if (oi == vi || !vehicles[oi].used) continue;
        vec3 d = rel(vehicles[oi].sim.body.pos, s.body.pos);
        float along = dot(d, fwd);
        if (along < 0.f || along > 30.f) continue;
        float lat = fabsf(dot(d, s.right()));
        if (lat < 2.2f) obstacle = Min(obstacle, along - vassets[vehicles[oi].model].spec.boxHalf.y - vassets[v.model].spec.boxHalf.y);
    }
    for (int pi = 0; pi < (int)peds.size(); pi++) {
        const Ped& o = peds[pi];
        if (!o.used || o.state == PS_INVEHICLE) continue;
        vec3 d = o.pos.toVec3() - pos;
        float along = dot(d, fwd);
        if (along < 0.f || along > 18.f) continue;
        if (fabsf(dot(d, s.right())) < 1.6f && !fleeing) obstacle = Min(obstacle, along - vassets[v.model].spec.boxHalf.y - 1.f);
    }
    // traffic signals at the upcoming node
    if (!fleeing) {
        int node = v.laneDir > 0 ? e.n1 : e.n0;
        const World::RoadNode& n = roads->nodes[node];
        float distToNode = v.laneDir > 0 ? e.length - sEdge : sEdge;
        float stopAt = distToNode - (v.laneDir > 0 ? e.cut1 : e.cut0) - 1.5f;
        if (n.control == 2 && stopAt > -0.5f && stopAt < 45.f) {
            vec3 t = e.tangentAt(v.laneDir > 0 ? e.length : 0.f);
            if (!signalGreen(node, vec2(t.x, t.y))) obstacle = Min(obstacle, stopAt);
        }
    }
    float target_speed = limit;
    if (obstacle < 1e8f) target_speed = Min(target_speed, Max(0.f, (obstacle - 2.5f) * 0.7f));
    float fs = s.forwardSpeed();
    float err = target_speed - fs;
    if (err > 0.f) c.throttle = Saturate(err * 0.35f + 0.1f);
    else c.brake = Saturate(-err * 0.3f);
    if (target_speed < 0.3f && fs < 0.5f) {
        c.brake = 1.f;
        c.throttle = 0.f;
    }
    // stuck handling: reverse briefly
    if (c.throttle > 0.3f && fs < 0.3f) v.stuckTime += dt;
    else v.stuckTime = Max(0.f, v.stuckTime - dt);
    if (v.stuckTime > 3.f) {
        c.throttle = 0.f;
        c.brake = 1.f;
        c.steer = -c.steer;
        if (v.stuckTime > 5.f) v.stuckTime = 0.f;
    }
}

// Two-phase signal cycle per node: phase A serves approaches closer to the node's primary axis.
bool GameWorld::signalGreen(int node, vec2 approachDir) const {
    const World::RoadNode& n = roads->nodes[node];
    float cycle = 34.f;
    float off = (float)(hash32((u32)node * 2654435761u) % 3400) * 0.01f;
    float t = fmodf((float)time + off, cycle);
    // primary axis: direction of the node's first edge
    vec2 axis(1, 0);
    if (!n.edges.empty()) {
        const World::RoadEdge& e0 = roads->edges[n.edges[0]];
        vec2 a = e0.pts.front().xy(), b = e0.pts.back().xy();
        axis = normalize(b - a);
    }
    bool primary = fabsf(dot(normalize(approachDir), axis)) > 0.7071f;
    if (primary) return t < 14.5f;             // green 0..14.5, amber 14.5..17
    return t >= 17.f && t < 31.5f;             // other axis green 17..31.5, amber 31.5..34
}

int GameWorld::signalLampState(int node, vec2 approachDir) const {
    const World::RoadNode& n = roads->nodes[node];
    (void)n;
    float cycle = 34.f;
    float off = (float)(hash32((u32)node * 2654435761u) % 3400) * 0.01f;
    float t = fmodf((float)time + off, cycle);
    vec2 axis(1, 0);
    if (!roads->nodes[node].edges.empty()) {
        const World::RoadEdge& e0 = roads->edges[roads->nodes[node].edges[0]];
        axis = normalize(e0.pts.back().xy() - e0.pts.front().xy());
    }
    bool primary = fabsf(dot(normalize(approachDir), axis)) > 0.7071f;
    float lt = primary ? t : fmodf(t + cycle - 17.f, cycle);
    if (lt < 14.5f) return 2;   // green
    if (lt < 17.f) return 1;    // amber
    return 0;                   // red
}

}  // namespace Game
