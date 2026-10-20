# AI agent: the "taxi" test mode (a cab hailed at the kerb: the fare in through the door clip, the cab standing until they
# are in) - applied the same way to any copy of app.cpp. Usage: taxitest.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
# 1) the mode list
old1 = """            autoplay == "showoff" || autoplay == "crosswalk" || autoplay == "runby" || autoplay == "nightwalk") {"""
new1 = """            autoplay == "showoff" || autoplay == "crosswalk" || autoplay == "runby" || autoplay == "nightwalk" || autoplay == "taxi") {"""
assert s.count(old1) == 1
s = s.replace(old1, new1, 1)
# 2) the setup (applyAutoplay): the first "showoff" branch
old2 = """            } else if (autoplay == "showoff") {
                // the player's car show at a downtown crossing in the afternoon (applyAutoplay): donuts in the middle of it -"""
new2 = """            } else if (autoplay == "taxi") {
                // a cab hailed on a downtown street at midday, twice (applyAutoplay): it pulls over, the fare walks to its back
                // door on the kerb side and gets in through the door clip, the cab standing until they are in
                autoDuration = 130.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 12.f;
                game.ai.forceEvent = 99;   // (no ambient events running into it)
            } else if (autoplay == "showoff") {
                // the player's car show at a downtown crossing in the afternoon (applyAutoplay): donuts in the middle of it -"""
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)
# 3) the per-frame branch: before the last "showoff" branch
anchor = """        } else if (autoplay == "showoff") {"""
i = s.rindex(anchor)
branch = r'''        } else if (autoplay == "taxi") {
            // at 4 s and 70 s: somebody hailing a cab at the kerb of a town street near the player, and a cab put 70 m back in
            // that kerb lane coming their way. It sees them (traffic.cpp VR_TAXI), pulls over, and they walk to its kerb-side
            // back door and get in through the door clip (pedai.cpp, ai.cpp ai_board); the cab is to stand until they are in,
            // then drive off with them. The camera on the cab and the fare; the state every second
            static int round = 0, cab = -1, fare = -1;
            static u32 cabUid = 0, fareUid = 0;
            static float logT = 0.f, shotT = 1.f;
            static int shots = 0;
            Ped* pl = game.playerPed();
            if (pl && ((round == 0 && t > 4.f) || (round == 1 && t > 70.f))) {
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
                    if (L.u1 - uu < 95.f) continue;   // (room for the run up and the stop)
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 90.f)) < 0.99f) continue;
                    lane = li;
                    u = uu;
                }
                int model = game.findVehicleModel(Vehicles::VC_TAXI, (u32)round);
                if (lane >= 0 && model >= 0) {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    vec2 tg = game.laneGraph.laneTangent(lane, u + 70.f);
                    vec3 at = game.laneGraph.lanePos(lane, u + 70.f, L.width * 0.5f + 1.1f);   // (on the sidewalk, a step from the kerb)
                    int id = game.spawnPed(game.randomCivilianChar(hash32((u32)round * 53u + 9u), 0),
                                           dvec3(at.x, at.y, game.groundHeight(at.x, at.y, at.z + 1.5f)), AI::dirYaw(-AI::rightOf(tg)), FAC_CIVILIAN);
                    vec3 c3 = game.laneGraph.lanePos(lane, u);
                    int vid = id >= 0 ? game.spawnVehicle(model, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, u)), true, FAC_CIVILIAN) : -1;
                    if (id >= 0 && vid >= 0 && game.vehicles[vid].seats[0] >= 0) {
                        PedAI& qa = game.pedAI(id);
                        game.peds[id].brain.type = BRAIN_WANDER;
                        qa.activity = ACT_HAIL_TAXI;   // (as population.cpp's hailers: stood at the kerb, an arm out for a cab)
                        qa.anchor = at.xy();
                        qa.anchorYaw = AI::dirYaw(-AI::rightOf(tg));
                        qa.stance = 0;
                        qa.clip = -1;
                        qa.targetVeh = -1;
                        qa.actTimer = 0.f;
                        game.peds[game.vehicles[vid].seats[0]].brain.type = BRAIN_DRIVER;
                        game.vehicles[vid].persistent = true;   // (test prop: never recycled)
                        game.attachTraffic(vid, lane, u);
                        cab = vid;
                        cabUid = game.vehicles[vid].uid;
                        fare = id;
                        fareUid = game.peds[id].uid;
                        LOG("autoplay taxi: round %d at t=%.1f - ped %d hailing at %.0f %.0f, cab %d (model %d) 70 m back on lane %d", round, t, id, at.x, at.y,
                            vid, model, lane);
                    }
                } else {
                    LOG("autoplay taxi: round %d at t=%.1f - no town kerb lane with room near the player (%d) or no cab model (%d)", round, t, lane, model);
                }
            }
            bool cabOk = cab >= 0 && cab < (int)game.vehicles.size() && game.vehicles[cab].used && game.vehicles[cab].uid == cabUid;
            bool fareOk = fare >= 0 && fare < (int)game.peds.size() && game.peds[fare].used && game.peds[fare].uid == fareUid;
            if (cabOk) {
                const Vehicle& v = game.vehicles[cab];
                vec3 cp = v.sim.body.pos.toVec3();
                vec3 fp = fareOk ? game.peds[fare].pos.toVec3() : cp;
                vec3 mid = (cp + fp) * 0.5f;
                vec2 f = normalize(v.sim.forward().xy() + vec2(1e-4f, 0.f));
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(mid + vec3(-f * 8.f + AI::rightOf(f) * 6.f, 3.2f));
                game.rig.scriptTarget = dvec3(mid + vec3(0.f, 0.f, 0.8f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 40) {
                    shotT = 2.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_taxi_%02d_r%d", shots, round));
                    shots++;
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 1.f;
                std::string cs = "no cab";
                if (cabOk) {
                    const VehAI& va = game.vehAI(cab);
                    const AI::Driver* dr = game.traffic.get(cab);
                    cs = StrFormat("cab %d speed %.1f mode %d task %d scene %d fare %d", cab, game.vehicles[cab].sim.speed(), dr ? (int)dr->mode : -1, (int)va.task,
                                   va.scene, va.fare);
                }
                std::string fs = "no fare";
                if (fareOk) {
                    const Ped& q = game.peds[fare];
                    const PedAI& qa = game.ai.ped[fare];
                    fs = StrFormat("fare %d state %d act %d targetVeh %d boardVeh %d in %d seat %d, %.1f m from the cab", fare, (int)q.state, (int)qa.activity,
                                   qa.targetVeh, qa.boardVeh, q.vehicle, q.seat, cabOk ? length(rel(q.pos, game.vehicles[cab].sim.body.pos)) : -1.f);
                }
                LOG("autoplay taxi t=%.1f round %d | %s | %s", t, round, cs.c_str(), fs.c_str());
            }
'''
s = s[:i] + branch + s[i:]
open(p, "w").write(s)
print("ok", p)
