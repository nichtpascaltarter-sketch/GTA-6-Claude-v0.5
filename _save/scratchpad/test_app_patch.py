# test-only: the cardoor autoplay's rear-door variants (--doorseat N, --npcrear) for a scratch build of app.cpp
import sys
p=sys.argv[1]; s=open(p).read()
def rep(old,new,cnt=1):
    global s
    assert s.count(old)==cnt, (s.count(old), old[:80])
    s=s.replace(old,new)
rep("""    int doorShot = 0;   // --autoplay cardoor: the next of its shots""",
"""    int doorShot = 0;   // --autoplay cardoor: the next of its shots
    int doorNpc = -1;   // (test: --npcrear, a passenger in the rear seat getting out first)""")
rep("""            autoDuration = 23.f;""","""            autoDuration = Platform::argValue("npcrear") ? 17.f : (Platform::argValue("doorseat") ? 16.f : 23.f);""")
rep("""                vec3 sw = game.laneGraph.lanePos(lane, u, off + 1.9f);""","""                if (Platform::argValue("npcrear") && game.ai.testCar[0] >= 0 && !game.charsMaleCivil.empty()) {
                    int ci = game.charsMaleCivil[game.charsMaleCivil.size() / 2];
                    doorNpc = game.spawnPed(ci, dvec3(a3.x, a3.y, a3.z + 1.f), yawL, FAC_CIVILIAN);
                    if (doorNpc >= 0) {
                        game.peds[doorNpc].persistent = true;
                        game.peds[doorNpc].brain.type = BRAIN_NONE;
                        game.warpPedIntoVehicle(doorNpc, game.ai.testCar[0], 2);
                        LOG("autoplay cardoor: rear passenger %d (%.2f m) in car %d", doorNpc, game.chars[ci].desc.height, game.ai.testCar[0]);
                    }
                }
                vec3 sw = game.laneGraph.lanePos(lane, u, off + 1.9f);""")
rep("""            c.enter.pressed = (t >= 1.f && t - dt < 1.f) || (t >= 12.f && t - dt < 12.f);
            const int a = game.ai.testCar[0], b = game.ai.testCar[1];
            const int carCam = t < 19.f ? a : b;
            if (carCam >= 0 && game.vehicles[carCam].used) {
                const Vehicle& v = game.vehicles[carCam];
                const Vehicles::SeatSpec& ss = game.vassets[v.model].spec.seats[0];""","""            static const int doorSeat = Platform::argValue("doorseat") ? atoi(Platform::argValue("doorseat")) : 0;
            const bool npcFirst = doorNpc >= 0;   // (the rear passenger out at 1 s; the player in at 6 s, out at 13 s)
            const float tIn = npcFirst ? 6.f : 1.f, tOut = npcFirst ? 13.f : 12.f;
            if (npcFirst && t >= 1.f && t - dt < 1.f && game.peds[doorNpc].vehicle >= 0) {
                game.removePedFromVehicle(doorNpc, true);
                LOG("autoplay cardoor: rear passenger %d getting out, state %d door %d", doorNpc, (int)game.peds[doorNpc].state, game.peds[doorNpc].doorVehicle);
            }
            if (npcFirst && t >= 4.5f && t - dt < 4.5f && game.peds[doorNpc].used && game.peds[doorNpc].vehicle < 0) {
                // (out: off along the sidewalk, out of the player's way to the same door)
                const Vehicle& va = game.vehicles[game.ai.testCar[0]];
                vec3 away = va.sim.body.pos.toVec3() + rotate(va.sim.body.rot, vec3(-2.4f, -6.f, 0.f));
                mu::setGoto(game, doorNpc, away, 1.4f);
            }
            c.enter.pressed = (doorSeat == 0 && t >= tIn && t - dt < tIn) || (t >= tOut && t - dt < tOut);
            c.special.pressed = doorSeat != 0 && t >= tIn && t - dt < tIn;
            const int a = game.ai.testCar[0], b = game.ai.testCar[1];
            const int carCam = t < 19.f ? a : b;
            if (carCam >= 0 && game.vehicles[carCam].used) {
                const Vehicle& v = game.vehicles[carCam];
                const Vehicles::SeatSpec& ss = game.vassets[v.model].spec.seats[t < 19.f ? doorSeat : 0];""")
rep("""            static const float kShotAt[] = {1.6f, 2.2f, 2.6f, 3.0f, 3.4f, 3.8f, 4.2f, 4.6f, 5.0f, 5.6f, 7.5f,
                                            12.4f, 12.8f, 13.2f, 13.6f, 14.0f, 14.4f, 14.8f, 15.4f, 21.f, 22.f};
            if (doorShot < (int)(sizeof(kShotAt) / sizeof(kShotAt[0])) && t >= kShotAt[doorShot] && game.requestScreenshot.empty()) {""",
"""            static const float kShotRear[] = {1.6f, 2.2f, 2.6f, 3.0f, 3.3f, 3.6f, 3.9f, 4.2f, 4.6f, 5.4f,
                                              12.6f, 12.9f, 13.2f, 13.5f, 13.8f, 14.1f, 14.5f, 15.2f};
            static const float kShotNpc[] = {1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.2f, 6.6f, 7.2f, 7.8f, 8.4f, 9.0f, 10.0f, 13.6f, 14.2f, 14.8f, 15.4f, 16.2f};
            const float* kShotAt = npcFirst ? kShotNpc : kShotRear;
            const int nShots = npcFirst ? (int)(sizeof(kShotNpc) / sizeof(kShotNpc[0])) : (int)(sizeof(kShotRear) / sizeof(kShotRear[0]));
            if (doorShot < nShots && t >= kShotAt[doorShot] && game.requestScreenshot.empty()) {""")
open(p,'w').write(s)
