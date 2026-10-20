# AI agent: the "gotofollow" test mode (change 2) - a BRAIN_GOTO walker behind a slow one on the same line of a sidewalk:
# it keeps their pace a little way back (ai.cpp ai_pace::behind) instead of pressing into their back; when the slow one
# stops, it stops behind them, then steps round (aiWalkRound) and walks on. Applied to a copy of app.cpp that has the
# copreturn mode (copreturn.py). Usage: gotofollow.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """autoplay == "knock" || autoplay == "copreturn") {"""
new1 = """autoplay == "knock" || autoplay == "copreturn" || autoplay == "gotofollow") {"""
assert s.count(old1) == 2, "setup lists (%d)" % s.count(old1)
s = s.replace(old1, new1)
anchor = """        } else if (autoplay == "copreturn") {"""
i = s.rindex(anchor)
branch = r'''        } else if (autoplay == "gotofollow") {
            // a BRAIN_GOTO walker 3 m behind a slow one (0.6 m/s) on the same line along a sidewalk, both for the same point
            // 30 m on: it keeps their pace a little way back (ai.cpp ai_pace::behind), not pressing into their back; 22 s into a
            // round the slow one stops - it stops behind them, steps round (aiWalkRound) and walks on. At 4 s and at 70 s; the camera
            // on them, the gap and the paces every half second; any gap under 0.5 m called out
            static int round = 0, lead = -1, foll = -1;
            static u32 leadUid = 0, follUid = 0;
            static float logT = 0.f, shotT = 1.f, minGap = 99.f, roundT = 0.f;
            static int shots = 0, presses = 0;
            static bool stopped = false, passed = false;
            static vec2 line, goal;
            Ped* pl = game.playerPed();
            if (pl && ((round == 0 && t > 4.f) || (round == 1 && t > 70.f))) {
                round++;
                roundT = t;
                stopped = passed = false;
                minGap = 99.f;
                presses = 0;
                shots = 0;
                vec2 q = pl->pos.toVec3().xy();
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 60 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f + (float)round * 1.3f), sinf(k * 2.4f + (float)round * 1.3f)) * (8.f + k * 5.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    if ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_DIRT)) || L.right >= 0 ||
                        World::roadInfo((World::RoadClass)L.cls).sidewalk < 2.5f)
                        continue;   // (a kerb lane in town, a sidewalk beside it)
                    uu = Max(uu, L.u0 + 4.f);
                    if (L.u1 - uu < 40.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 35.f)) < 0.99f) continue;
                    lane = li;
                    u = uu;
                }
                if (lane >= 0) {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    const World::RoadClassInfo& ri = World::roadInfo((World::RoadClass)L.cls);
                    float off = L.width * 0.5f + ri.shoulder + ri.sidewalk * 0.5f;   // (along the sidewalk's middle)
                    vec3 a3 = game.laneGraph.lanePos(lane, u + 3.f, off), b3 = game.laneGraph.lanePos(lane, u, off),
                         g3 = game.laneGraph.lanePos(lane, u + 33.f, off);
                    line = normalize((g3 - b3).xy() + vec2(1e-4f, 0.f));
                    goal = g3.xy();
                    lead = game.spawnPed(game.randomCivilianChar(hash32((u32)round * 31u + 5u), 0), dvec3(a3.x, a3.y, game.groundHeight(a3.x, a3.y, a3.z + 1.5f)),
                                         AI::dirYaw(line), FAC_CIVILIAN);
                    foll = game.spawnPed(game.randomCivilianChar(hash32((u32)round * 37u + 11u), 0), dvec3(b3.x, b3.y, game.groundHeight(b3.x, b3.y, b3.z + 1.5f)),
                                         AI::dirYaw(line), FAC_CIVILIAN);
                    if (lead >= 0 && foll >= 0) {
                        leadUid = game.peds[lead].uid;
                        follUid = game.peds[foll].uid;
                        for (int k = 0; k < 2; k++) {
                            Ped& w = game.peds[k == 0 ? lead : foll];
                            w.persistent = true;   // (test props: never recycled)
                            w.brain.type = BRAIN_GOTO;
                            w.brain.target = -1;
                            w.brain.goal = dvec3(goal.x, goal.y, (double)g3.z);
                            w.brain.speed = k == 0 ? 0.6f : 1.5f;
                        }
                        LOG("autoplay gotofollow: round %d at t=%.1f - slow ped %d (0.6 m/s) 3 m ahead of ped %d (1.5 m/s), both for %.0f %.0f along lane %d", round,
                            t, lead, foll, goal.x, goal.y, lane);
                    } else {
                        LOG("autoplay gotofollow: round %d at t=%.1f - the walkers could not be spawned", round, t);
                    }
                } else {
                    LOG("autoplay gotofollow: round %d at t=%.1f - no town kerb lane with room near the player", round, t);
                }
            }
            bool ok = lead >= 0 && foll >= 0 && lead < (int)game.peds.size() && foll < (int)game.peds.size() && game.peds[lead].used &&
                      game.peds[foll].used && game.peds[lead].uid == leadUid && game.peds[foll].uid == follUid;
            if (ok) {
                Ped& A = game.peds[lead];
                Ped& B = game.peds[foll];
                // (the slow one stops 22 s into the round: stood where it is)
                if (!stopped && t - roundT > 22.f) {
                    stopped = true;
                    A.brain.goal = A.pos;
                    LOG("autoplay gotofollow: round %d - the slow one stops at t=%.1f", round, t);
                }
                vec2 rel = A.pos.toVec3().xy() - B.pos.toVec3().xy();
                float gap = dot(rel, line), side = dot(rel, AI::rightOf(line));
                float d = length(rel);
                if (gap > -0.5f && fabsf(side) < 0.8f) minGap = Min(minGap, d);
                if (d < 0.5f && gap > 0.f) {
                    if (presses % 20 == 0) LOG("autoplay gotofollow: PRESS at t=%.1f - %.2f m behind the slow one", t, d);
                    presses++;
                }
                if (!passed && gap < -1.f) {
                    passed = true;
                    LOG("autoplay gotofollow: round %d - past the slow one at t=%.1f (%.1f s after it stopped)", round, t, stopped ? t - roundT - 22.f : -1.f);
                }
                vec3 mid = (A.pos.toVec3() + B.pos.toVec3()) * 0.5f;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(mid + vec3(-line * 4.f + AI::rightOf(line) * 6.f, 3.f));
                game.rig.scriptTarget = dvec3(mid + vec3(0.f, 0.f, 0.9f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 22) {
                    shotT = 1.5f;
                    game.requestScreenshot = shotPath(StrFormat("auto_gotofollow_%02d_r%d", shots, round));
                    shots++;
                }
                logT -= dt;
                if (logT <= 0.f) {
                    logT = 0.5f;
                    LOG("autoplay gotofollow t=%.1f round %d | gap %.2f m (side %+.2f) | slow %.2f m/s, follower %.2f m/s along | min gap %.2f | presses %d%s", t,
                        round, gap, side, dot(A.vel.xy(), line), dot(B.vel.xy(), line), minGap, presses, passed ? " | past it" : "");
                }
            } else {
                game.rig.scriptActive = false;
            }
'''
s = s[:i] + branch + s[i:]
open(p, "w").write(s)
print("ok", p)
