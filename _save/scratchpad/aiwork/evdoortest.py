# AI agent: the "trafficstop" and "breakdown" test modes (set 17: the events' people back into their cars through the
# door) - applied to a copy of app.cpp that has the taxi mode (taxitest.py). Usage: evdoortest.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """autoplay == "nightwalk" || autoplay == "taxi") {"""
new1 = """autoplay == "nightwalk" || autoplay == "taxi" ||
            autoplay == "trafficstop" || autoplay == "breakdown") {"""
assert s.count(old1) == 1
s = s.replace(old1, new1, 1)
old2 = """            } else if (autoplay == "taxi") {
                // a cab hailed on a downtown street at midday, twice (applyAutoplay): it pulls over, the fare walks to its back"""
new2 = """            } else if (autoplay == "trafficstop" || autoplay == "breakdown") {
                // a traffic stop, or a stranded driver, on the downtown streets at midday (applyAutoplay): forced at 3 s and
                // watched to the end - the officer, or the driver once the player has fixed the car, back in through the door
                autoDuration = 150.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 12.f;
                game.ai.forceEvent = 99;   // (nothing else running into it)
            } else if (autoplay == "taxi") {
                // a cab hailed on a downtown street at midday, twice (applyAutoplay): it pulls over, the fare walks to its back"""
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)
anchor = """        } else if (autoplay == "taxi") {"""
i = s.rindex(anchor)
branch = r'''        } else if (autoplay == "trafficstop" || autoplay == "breakdown") {
            // the events' people back into their cars through the door (ai.cpp ai_board::beginThen): a traffic stop - the
            // officer back to the cruiser, in, then both away - or a breakdown, fixed by the player stood at the hood (the
            // driver cheers, back in through the door and off; the player then steps onto the sidewalk out of its way).
            // Forced at 3 s; the camera on it; the state every 2 s
            bool stop = autoplay == "trafficstop";
            int type = stop ? 10 : 9;   // (EV_TRAFFIC_STOP, EV_BREAKDOWN)
            static float logT = 0.f, shotT = 2.f;
            static int shots = 0, car = -1, lastStage = -1;
            static bool forced = false;
            if (!forced && t > 3.f) {
                forced = true;
                game.ai.forceEvent = type;
                LOG("autoplay %s: forcing the event (t=%.1f)", autoplay.c_str(), t);
            }
            int stage = -1;
            vec3 at;
            std::string st = game.aiEventsText(type, &stage, &at);
            Ped* pl = game.playerPed();
            if (stage >= 0) {
                game.ai.forceEvent = 99;
                if (!stop && pl) {
                    if (car < 0) {
                        // (the broken-down car: the stood one nearest the event with nobody at the wheel)
                        float bd = 12.f;
                        for (int v = 0; v < (int)game.vehicles.size(); v++) {
                            const Vehicle& cv = game.vehicles[v];
                            if (!cv.used || cv.seats[0] >= 0 || cv.sim.speed() > 0.5f || game.isAircraft(v) || game.isBoat(v)) continue;
                            float d = length(cv.sim.body.pos.toVec3().xy() - at.xy());
                            if (d < bd) {
                                bd = d;
                                car = v;
                            }
                        }
                        if (car >= 0) LOG("autoplay breakdown: the car is %d, %.1f m from the event (t=%.1f)", car, bd, t);
                    }
                    if (car >= 0 && car < (int)game.vehicles.size() && game.vehicles[car].used) {
                        const Vehicle& cv = game.vehicles[car];
                        const Vehicles::VehicleModel& cs = game.vassets[cv.model].spec;
                        if (stage == 0) {
                            // (at the hood, stood still, until it is fixed)
                            vec3 hood = cv.sim.body.pos.toVec3() + rotate(cv.sim.body.rot, vec3(0.f, cs.boxHalf.y + 0.9f, 0.f));
                            if (length(pl->pos.toVec3().xy() - hood.xy()) > 1.2f) {
                                pl->pos = dvec3(hood.x, hood.y, game.groundHeight(hood.x, hood.y, hood.z + 1.5f));
                                pl->vel = vec3(0.f);
                            }
                        } else if (lastStage == 0) {
                            // (fixed: onto the kerb side, out of the car's way)
                            vec3 kerb = cv.sim.body.pos.toVec3() + rotate(cv.sim.body.rot, vec3(cs.boxHalf.x + 3.f, 0.f, 0.f));
                            pl->pos = dvec3(kerb.x, kerb.y, game.groundHeight(kerb.x, kerb.y, kerb.z + 1.5f));
                            pl->vel = vec3(0.f);
                            LOG("autoplay breakdown: fixed at t=%.1f - the player steps out of the way", t);
                        }
                    }
                }
                lastStage = stage;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(at + vec3(7.f, -7.f, 4.f));
                game.rig.scriptTarget = dvec3(at + vec3(0.f, 0.f, 0.8f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 45) {
                    shotT = 3.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_%s_%02d_s%d", autoplay.c_str(), shots, stage));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                LOG("autoplay %s t=%.1f | %s", autoplay.c_str(), t, st.empty() ? "no event" : st.c_str());
            }
'''
s = s[:i] + branch + s[i:]
open(p, "w").write(s)
print("ok", p)
