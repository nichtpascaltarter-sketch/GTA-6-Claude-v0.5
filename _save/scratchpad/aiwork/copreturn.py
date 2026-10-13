# AI agent: the "copreturn" test mode - two officers back to their patrol car at the same moment (police.cpp FT_RETURN),
# from the same distance behind it: with ai_board's seat claims one takes the wheel and the other goes round to a passenger
# door - never both through the driver's door into the one seat (Codex P2 on PR #46) - then the car leaves with both.
# Applied to a copy of app.cpp that has the knock mode (knocktest.py). Usage: copreturn.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """            autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock") {"""
new1 = """            autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock" || autoplay == "copreturn") {"""
assert s.count(old1) == 1, "setup list"
s = s.replace(old1, new1, 1)
old2 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock") {"""
new2 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock" || autoplay == "copreturn") {"""
assert s.count(old2) == 1, "setup branch"
s = s.replace(old2, new2, 1)
anchor = """        } else if (autoplay == "knock") {"""
i = s.rindex(anchor)
branch = r'''        } else if (autoplay == "copreturn") {
            // two officers back to their patrol car at once (police.cpp FT_RETURN: BRAIN_GOTO -2), stood side by side 7 m
            // behind it: one to the wheel, the other round to a passenger door (ai_board's seat claims - not both through the
            // driver's door into the one seat), then away together. At 4 s and at 60 s; the camera on the car, the two every
            // half second, and any two door clips into one seat called out
            static int round = 0, car = -1;
            static int cop[2] = {-1, -1};
            static u32 carUid = 0;
            static float logT = 0.f, shotT = 1.f;
            static int shots = 0, overlaps = 0;
            static bool both = false, away = false;
            Ped* pl = game.playerPed();
            if (pl && ((round == 0 && t > 4.f) || (round == 1 && t > 60.f))) {
                round++;
                both = away = false;
                vec2 q = pl->pos.toVec3().xy();
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 60 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f + (float)round * 1.7f), sinf(k * 2.4f + (float)round * 1.7f)) * (8.f + k * 5.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    if ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_DIRT)) || L.right >= 0) continue;   // (a kerb lane in town)
                    uu = Max(uu, L.u0 + 4.f);
                    if (L.u1 - uu < 30.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 20.f)) < 0.99f) continue;
                    lane = li;
                    u = uu;
                }
                int pm = game.findVehicleModel(Vehicles::VC_POLICE, (u32)round * 5u);
                int vid = -1;
                if (lane >= 0 && pm >= 0) {
                    vec3 c3 = game.laneGraph.lanePos(lane, u + 10.f);
                    vec2 tg = game.laneGraph.laneTangent(lane, u + 10.f);
                    vid = game.spawnVehicle(pm, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(tg), true, FAC_POLICE);
                    if (vid >= 0 && game.vehicles[vid].seats[0] >= 0) {
                        Vehicle& pv = game.vehicles[vid];
                        pv.faction = FAC_POLICE;
                        pv.persistent = true;   // (test prop: never recycled)
                        game.vehAI(vid).role = VR_POLICE;
                        cop[0] = pv.seats[0];
                        cop[1] = game.spawnPed(game.randomCivilianChar(hash32((u32)vid * 7u + (u32)round), 1), pv.sim.body.pos, 0.f, FAC_POLICE);
                        game.removePedFromVehicle(cop[0], false);
                        vec2 back = c3.xy() - tg * 7.f, rt = AI::rightOf(tg);
                        for (int k = 0; k < 2; k++) {
                            int o = cop[k];
                            if (o < 0) continue;
                            vec2 at = back + rt * (k == 0 ? -0.8f : 0.8f);
                            Ped& op = game.peds[o];
                            op.pos = dvec3(at.x, at.y, game.groundHeight(at.x, at.y, c3.z + 1.5f));
                            op.vel = vec3(0.f);
                            op.yaw = AI::dirYaw(tg);
                            PedAI& oa = game.pedAI(o);
                            oa.activity = ACT_WALK;
                            oa.role = PR_COP;
                            oa.homeVeh = vid;
                            oa.tactic = FT_RETURN;
                            oa.doorWalkT = 0.f;
                            op.brain.type = BRAIN_GOTO;
                            op.brain.target = -2;
                        }
                        pv.parked = true;
                        game.traffic.detach(vid);
                        game.vehAI(vid).managed = false;
                        car = vid;
                        carUid = pv.uid;
                        LOG("autoplay copreturn: round %d at t=%.1f - patrol car %d (model %d) at %.0f %.0f on lane %d, officers %d and %d 7 m behind it", round,
                            t, vid, pm, c3.x, c3.y, lane, cop[0], cop[1]);
                    } else {
                        vid = -1;
                    }
                }
                if (vid < 0) LOG("autoplay copreturn: round %d at t=%.1f - no patrol car placed (lane %d, model %d)", round, t, lane, pm);
            }
            bool carOk = car >= 0 && car < (int)game.vehicles.size() && game.vehicles[car].used && game.vehicles[car].uid == carUid;
            if (carOk && cop[0] >= 0 && cop[1] >= 0) {
                const Vehicle& cv = game.vehicles[car];
                const Ped& A = game.peds[cop[0]];
                const Ped& B = game.peds[cop[1]];
                const PedAI& aa = game.ai.ped[cop[0]];
                const PedAI& ba = game.ai.ped[cop[1]];
                // (two door clips into one seat at once: what the claims are for)
                bool overlap = A.state == PS_ENTERING && B.state == PS_ENTERING && aa.boardVeh == car && ba.boardVeh == car && aa.boardSeat == ba.boardSeat;
                if (overlap) {
                    if (overlaps % 30 == 0) LOG("autoplay copreturn: OVERLAP at t=%.1f - officers %d and %d both through the door into seat %d of car %d", t, cop[0],
                                                cop[1], (int)aa.boardSeat, car);
                    overlaps++;
                }
                bool in2 = A.state == PS_INVEHICLE && B.state == PS_INVEHICLE && A.vehicle == car && B.vehicle == car;
                if (in2 && !both) {
                    both = true;
                    LOG("autoplay copreturn: both in at t=%.1f - officer %d seat %d, officer %d seat %d", t, cop[0], A.seat, cop[1], B.seat);
                }
                if (both && !away && cv.sim.speed() > 2.f) {
                    away = true;
                    LOG("autoplay copreturn: car %d away with both at t=%.1f", car, t);
                }
                vec3 cp = cv.sim.body.pos.toVec3();
                vec2 f = normalize(cv.sim.forward().xy() + vec2(1e-4f, 0.f));
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cp + vec3(-f * 9.f + AI::rightOf(f) * 6.f, 4.f));
                game.rig.scriptTarget = dvec3(cp + vec3(0.f, 0.f, 0.8f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 45 && !away) {
                    shotT = 1.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_copreturn_%02d_r%d", shots, round));
                    shots++;
                }
                logT -= dt;
                if (logT <= 0.f) {
                    logT = 0.5f;
                    LOG("autoplay copreturn t=%.1f round %d | car %d speed %.1f seats %d %d | officer %d state %d board %d/%d in %d seat %d | officer %d state %d "
                        "board %d/%d in %d seat %d | overlaps %d",
                        t, round, car, cv.sim.speed(), cv.seats[0], cv.seats[1], cop[0], (int)A.state, aa.boardVeh, (int)aa.boardSeat, A.vehicle, A.seat, cop[1],
                        (int)B.state, ba.boardVeh, (int)ba.boardSeat, B.vehicle, B.seat, overlaps);
                }
            } else {
                game.rig.scriptActive = false;
            }
'''
s = s[:i] + branch + s[i:]
open(p, "w").write(s)
print("ok", p)
