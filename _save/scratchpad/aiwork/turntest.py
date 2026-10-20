# AI agent: the "turntest" test mode (change 3, ai_turn) - a BRAIN_GOTO walker's turns along a sidewalk's middle: 6 m along,
# a right angle 2 m across, along again, a 135-degree turn back across, along, then turned round (180) twice; at a full walk
# (1.5 m/s) from 4 s, at a stroll (1.0 m/s) from 70 s. Per corner: the most turn rate at over 0.8 m/s, the least speed, the
# most crab, the time to face the new leg and the swing off its line; every 0.1 s in the first 2.5 s after a corner the
# rate, speed and crab. Applied after gotofollow.py. Usage: turntest.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """            autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock" || autoplay == "copreturn" || autoplay == "gotofollow") {"""
new1 = """            autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock" || autoplay == "copreturn" || autoplay == "gotofollow" ||
            autoplay == "turntest") {"""
assert s.count(old1) == 1, "scenario list (%d)" % s.count(old1)
s = s.replace(old1, new1)
old1 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock" || autoplay == "copreturn" || autoplay == "gotofollow") {"""
new1 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown" || autoplay == "knock" || autoplay == "copreturn" ||
                       autoplay == "gotofollow" || autoplay == "turntest") {"""
assert s.count(old1) == 1, "setup branch (%d)" % s.count(old1)
s = s.replace(old1, new1)
anchor = """        } else if (autoplay == "gotofollow") {"""
assert s.count(anchor) == 1
branch = r'''        } else if (autoplay == "turntest") {
            // change 3 (ai_turn): a BRAIN_GOTO walker's turns along a sidewalk's middle - 6 m along it, a right angle 2 m
            // across, along again, a 135-degree turn back across, along, then turned round twice (180) - at a full walk
            // (1.5 m/s) from 4 s, at a stroll (1.0 m/s) from 70 s, the camera beside it. Per corner: the most turn rate at
            // over 0.8 m/s, the least speed, the most crab (the way it goes off the way it faces, at over 0.3 m/s), the time
            // to face the new leg (within 10 degrees) and the swing off its line; every 0.1 s for 2.5 s after a corner, the
            // rate, the speed and the crab
            static int round = 0, w = -1, leg = 0, shots = 0;
            static u32 wUid = 0;
            static vec2 pts[8];
            static float legT = 0.f, sampT = 0.f, shotT = 1.f, lastYaw = 0.f, maxRate = 0.f, minSpd = 99.f, maxCrab = 0.f, faceT = -1.f,
                         swing = 0.f, roundT = 0.f;
            static bool summed = true;
            static vec3 camSide;
            const int kTurn[8] = {0, 0, 90, 90, 135, 135, 180, 180};
            Ped* pl = game.playerPed();
            if (pl && ((round == 0 && t > 4.f) || (round == 1 && t > 70.f))) {
                round++;
                roundT = t;
                w = -1;
                shots = 0;
                vec2 q = pl->pos.toVec3().xy();
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 60 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f + (float)round * 0.7f), sinf(k * 2.4f + (float)round * 0.7f)) * (8.f + k * 5.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    if ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_DIRT)) || L.right >= 0 ||
                        World::roadInfo((World::RoadClass)L.cls).sidewalk < 3.f)
                        continue;   // (a kerb lane in town, a sidewalk 3 m wide or more beside it)
                    uu = Max(uu, L.u0 + 6.f);
                    if (L.u1 - uu < 26.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 20.f)) < 0.995f) continue;
                    lane = li;
                    u = uu;
                }
                if (lane >= 0) {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    const World::RoadClassInfo& ri = World::roadInfo((World::RoadClass)L.cls);
                    float off = L.width * 0.5f + ri.shoulder + ri.sidewalk * 0.5f;   // (the sidewalk's middle)
                    const float legPts[8][2] = {{0.f, -1.f}, {6.f, -1.f}, {6.f, 1.f}, {12.f, 1.f}, {10.f, -1.f}, {16.f, -1.f}, {10.f, -1.f}, {16.f, -1.f}};
                    for (int k = 0; k < 8; k++) pts[k] = game.laneGraph.lanePos(lane, u + legPts[k][0], off + legPts[k][1]).xy();
                    vec3 a3 = game.laneGraph.lanePos(lane, u, off - 1.f);
                    vec2 tg = game.laneGraph.laneTangent(lane, u);
                    camSide = vec3(AI::rightOf(tg) * -6.f + tg * 8.f, 3.2f);   // (from the kerb side, ahead of the middle)
                    w = game.spawnPed(game.randomCivilianChar(hash32((u32)round * 41u + 3u), 0), dvec3(a3.x, a3.y, game.groundHeight(a3.x, a3.y, a3.z + 1.5f)),
                                      AI::dirYaw(tg), FAC_CIVILIAN);
                    if (w >= 0) {
                        Ped& W = game.peds[w];
                        wUid = W.uid;
                        W.persistent = true;   // (a test prop: never recycled)
                        W.brain.type = BRAIN_GOTO;
                        W.brain.target = -1;
                        W.brain.speed = round == 1 ? 1.5f : 1.f;
                        leg = 1;
                        W.brain.goal = dvec3(pts[1].x, pts[1].y, W.pos.z);
                        legT = t;
                        lastYaw = W.yaw;
                        summed = true;
                        LOG("autoplay turntest: round %d at t=%.1f - ped %d at %.1f m/s along lane %d's sidewalk (%.1f m wide, its middle %.1f m out)", round, t, w,
                            W.brain.speed, lane, ri.sidewalk, off);
                    }
                } else {
                    LOG("autoplay turntest: round %d at t=%.1f - no kerb lane with a wide sidewalk near the player", round, t);
                }
            }
            bool ok = w >= 0 && w < (int)game.peds.size() && game.peds[w].used && game.peds[w].uid == wUid && game.peds[w].state == PS_ONFOOT;
            if (ok) {
                Ped& W = game.peds[w];
                vec2 pos = W.pos.toVec3().xy();
                float spd = length(W.vel.xy());
                float rate = dt > 0.f ? AI::wrapPi(W.yaw - lastYaw) / dt : 0.f;
                lastYaw = W.yaw;
                float crab = spd > 0.3f ? fabsf(AI::wrapPi(atan2f(-W.vel.x, W.vel.y) - W.yaw)) : 0.f;
                vec2 legDir = normalize(pts[leg] - pts[leg - 1] + vec2(1e-4f, 0.f));
                float legYaw = AI::dirYaw(legDir);
                if (leg >= 2 && t - legT < 6.f) {
                    if (spd > 0.8f) maxRate = Max(maxRate, fabsf(rate));
                    minSpd = Min(minSpd, spd);
                    maxCrab = Max(maxCrab, crab);
                    swing = Max(swing, fabsf(dot(pos - pts[leg - 1], AI::rightOf(legDir))));
                    if (faceT < 0.f && fabsf(AI::wrapPi(legYaw - W.yaw)) < 0.17f) faceT = t - legT;
                    sampT -= dt;
                    if (t - legT < 2.5f && sampT <= 0.f) {
                        sampT = 0.1f;
                        LOG("autoplay turntest t=%.2f corner %d (%d deg) +%.1f s | rate %+.2f rad/s | speed %.2f | crab %.0f deg | off its line %.2f m", t,
                            leg - 1, kTurn[leg], t - legT, rate, spd, crab * 57.3f, dot(pos - pts[leg - 1], AI::rightOf(legDir)));
                    }
                }
                // the corner summary: once the walker is on its way along the new leg (or at the next corner)
                bool next = length(pts[leg] - pos) < 0.9f;
                if (leg >= 2 && !summed && (next || t - legT > 6.f)) {
                    summed = true;
                    LOG("autoplay turntest: round %d corner %d (%d deg) - most rate %.2f rad/s over 0.8 m/s, least speed %.2f m/s, "
                        "most crab %.0f deg, faced the new leg after %.2f s, swing %.2f m",
                        round, leg - 1, kTurn[leg], maxRate, minSpd, maxCrab * 57.3f, faceT, swing);
                }
                if (next && leg < 7) {
                    leg++;
                    W.brain.goal = dvec3(pts[leg].x, pts[leg].y, W.pos.z);
                    legT = t;
                    sampT = 0.f;
                    maxRate = maxCrab = swing = 0.f;
                    minSpd = 99.f;
                    faceT = -1.f;
                    summed = false;
                } else if (next && leg == 7 && W.brain.type == BRAIN_GOTO) {
                    LOG("autoplay turntest: round %d done at t=%.1f (%.1f s)", round, t, t - roundT);
                    W.brain.type = BRAIN_WANDER;   // (on its way)
                    W.persistent = false;
                    w = -1;
                }
                vec3 mid = vec3((pts[2] + pts[5]) * 0.5f, W.pos.toVec3().z);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(mid + camSide);
                game.rig.scriptTarget = dvec3(W.pos.toVec3() + vec3(0.f, 0.f, 0.9f));
                game.rig.scriptFov = 50.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 24) {
                    shotT = 1.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_turntest_%02d_r%d", shots, round));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
'''
s = s.replace(anchor, branch + anchor, 1)
open(p, "w").write(s)
print("ok", p)
