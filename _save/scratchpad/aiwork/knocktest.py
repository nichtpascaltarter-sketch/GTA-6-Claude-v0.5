# AI agent: the "knock" test mode - a real fender bender in traffic (events.cpp aiFenderBender, the EV_CRASH flag 2/3
# path: both stop, the drivers out through the door clip, round their cars to the kerb, words, back in and away).
# Applied to a copy of app.cpp that has the trafficstop/breakdown modes (evdoortest.py). Usage: knocktest.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """            autoplay == "trafficstop" || autoplay == "breakdown") {"""
new1 = """            autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock") {"""
assert s.count(old1) == 1, "setup list"
s = s.replace(old1, new1, 1)
old2 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown") {"""
new2 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock") {"""
assert s.count(old2) == 1, "setup branch"
s = s.replace(old2, new2, 1)
anchor = """        } else if (autoplay == "trafficstop" || autoplay == "breakdown") {"""
i = s.rindex(anchor)
branch = r'''        } else if (autoplay == "knock") {
            // a real fender bender (events.cpp aiFenderBender - as ai.cpp starts one when two traffic cars knock together):
            // two cars put nose to tail in a town kerb lane near the player, at 4 s and again at 80 s. Both stop with the
            // hazards on, the drivers get out through the door clip, round their cars to the kerb between them, have words
            // (maybe a fight), then back in through the door and away. The camera on it; the event's state every 2 s
            static int round = 0, carA = -1, carB = -1;
            static float logT = 0.f, shotT = 2.f;
            static int shots = 0;
            Ped* pl = game.playerPed();
            int stage = -1;
            vec3 at;
            std::string st = game.aiEventsText(2, &stage, &at);   // (EV_CRASH)
            if (pl && ((round == 0 && t > 4.f) || (round == 1 && t > 80.f && stage < 0))) {
                round++;
                vec2 q = pl->pos.toVec3().xy();
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 60 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f + (float)round), sinf(k * 2.4f + (float)round)) * (8.f + k * 5.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    if ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_DIRT)) || L.right >= 0) continue;   // (a kerb lane in town)
                    uu = Max(uu, L.u0 + 4.f);
                    if (L.u1 - uu < 30.f) continue;   // (room for both cars and the walk round them)
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 20.f)) < 0.99f) continue;
                    lane = li;
                    u = uu;
                }
                int ma = game.findVehicleModel(round == 1 ? Vehicles::VC_SEDAN : Vehicles::VC_SUV, (u32)round * 7u);
                int mb = game.findVehicleModel(round == 1 ? Vehicles::VC_COMPACT : Vehicles::VC_SEDAN, (u32)round * 13u + 1u);
                if (lane >= 0 && ma >= 0 && mb >= 0) {
                    // (the back one, then the one it ran into a metre ahead of it - the knock just over)
                    float gap = game.vassets[ma].spec.boxHalf.y + game.vassets[mb].spec.boxHalf.y + 1.f;
                    int ids[2] = {-1, -1};
                    for (int k = 0; k < 2; k++) {
                        float uk = u + (k == 0 ? 0.f : gap);
                        vec3 c3 = game.laneGraph.lanePos(lane, uk);
                        int vid = game.spawnVehicle(k == 0 ? ma : mb, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, uk)), true,
                                                    FAC_CIVILIAN);
                        if (vid < 0 || game.vehicles[vid].seats[0] < 0) continue;
                        game.peds[game.vehicles[vid].seats[0]].brain.type = BRAIN_DRIVER;
                        game.vehicles[vid].persistent = true;   // (test prop: never recycled)
                        game.attachTraffic(vid, lane, uk);
                        ids[k] = vid;
                    }
                    carA = ids[0];
                    carB = ids[1];
                    bool ok = carA >= 0 && carB >= 0 && game.aiFenderBender(carA, carB);
                    LOG("autoplay knock: round %d at t=%.1f - cars %d and %d (models %d, %d) %.1f m apart on lane %d: %s", round, t, carA, carB, ma, mb, gap,
                        lane, ok ? "a fender bender" : "NOT started");
                } else {
                    LOG("autoplay knock: round %d at t=%.1f - no town kerb lane with room near the player (%d) or no models (%d %d)", round, t, lane, ma, mb);
                }
            }
            if (stage >= 0) {
                game.rig.scriptActive = true;
                vec3 side = carA >= 0 && carA < (int)game.vehicles.size() && game.vehicles[carA].used
                                ? vec3(AI::rightOf(normalize(game.vehicles[carA].sim.forward().xy() + vec2(1e-4f, 0.f))), 0.f)
                                : vec3(1.f, 0.f, 0.f);
                game.rig.scriptPos = dvec3(at + side * 9.f + vec3(0.f, 0.f, 4.5f));
                game.rig.scriptTarget = dvec3(at + vec3(0.f, 0.f, 0.8f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 45) {
                    shotT = 3.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_knock_%02d_r%d_s%d", shots, round, stage));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                LOG("autoplay knock t=%.1f round %d | %s", t, round, st.empty() ? "no event" : st.c_str());
            }
'''
s = s[:i] + branch + s[i:]
open(p, "w").write(s)
print("ok", p)
