# NPC-crime dispatch: the patrol with the shortest way in by road (not the nearest as the crow flies), and units sent
# from out of view only from where the way in is short (as the wanted dispatch and the transports)
p='/home/user/GTA-6-Claude-v0.5/src/game/police.cpp'
s=open(p).read()
a="""                bool prisoner = false;   // (a unit taking someone in is not sent to the next call)
                for (int s = 1; s < 8; s++) prisoner |= v.seats[s] >= 0 && peds[v.seats[s]].faction != FAC_POLICE;
                if (prisoner || (vi < (int)ai.veh.size() && ai.veh[vi].copBreak != 0)) continue;   // (nor one with its crew on a break)
                float d = length(v.sim.body.pos.toVec3().xy() - ip);
                if (d < bd) {
                    bd = d;
                    best = vi;
                }"""
b="""                bool prisoner = false;   // (a unit taking someone in is not sent to the next call)
                for (int s = 1; s < 8; s++) prisoner |= v.seats[s] >= 0 && peds[v.seats[s]].faction != FAC_POLICE;
                if (prisoner || (vi < (int)ai.veh.size() && ai.veh[vi].copBreak != 0)) continue;   // (nor one with its crew on a break)
                float d = length(v.sim.body.pos.toVec3().xy() - ip);
                if (d > bd) continue;
                // (by the way in on the roads where the car is on one: a car pointing the other way down a one-way
                //  street is further off than it looks)
                if (const AI::Driver* dv = traffic.get(vi))
                    if (dv->path >= 0 && laneGraph.isLane(dv->path)) {
                        float rl = aiRouteLength(dv->path, dv->u, ip);
                        if (rl >= 0.f) d = Max(d, rl);
                    }
                if (d < bd) {
                    bd = d;
                    best = vi;
                }"""
assert s.count(a)==1, 'select'
s=s.replace(a,b)
a="""                    vec3 c = laneGraph.lanePos(lane, u);
                    if (inCameraView(c, 8.f) || !traffic.laneFree(lane, u, 3.f, 6.f)) continue;
                    vec2 t = laneGraph.laneTangent(lane, u);
                    int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_POLICE);
                    if (vid < 0) break;
                    ai.stats.unitsSent++;
                    vehicles[vid].faction = FAC_POLICE;
                    attachTraffic(vid, lane, u);"""
b="""                    vec3 c = laneGraph.lanePos(lane, u);
                    if (inCameraView(c, 8.f) || !traffic.laneFree(lane, u, 3.f, 6.f)) continue;
                    if (attempt < 5) {   // (a way in at most 1.7 times the straight line, as the other units)
                        float rl = aiRouteLength(lane, u, ip);
                        if (rl < 0.f || rl > length(c.xy() - ip) * 1.7f + 40.f) continue;
                    }
                    vec2 t = laneGraph.laneTangent(lane, u);
                    int vid = spawnVehicle(model, dvec3(c.x, c.y, c.z + 0.3f), AI::dirYaw(t), true, FAC_POLICE);
                    if (vid < 0) break;
                    ai.stats.unitsSent++;
                    vehicles[vid].faction = FAC_POLICE;
                    attachTraffic(vid, lane, u);"""
assert s.count(a)==1, 'spawn'
s=s.replace(a,b)
open(p,'w').write(s)
print("applied npc_dispatch_route")
