# AI agent: the nearmiss test, second fix (after nearfix.py). On nt_ai101c the car (70 m short of the junction, the people past it)
# was held at the light by a pickup queued ahead in its lane, got round it over the sidewalk and passed the people slowly: no
# start back. Now the car sets off near the start of its kerb lane, the people stand along the last 40 m of the same lane, and the
# traffic queued ahead on it is sent away as they are put there: 30 m and more to get up to speed, no light or queue between.
# The route straight on through the junction stays (for after them). Usage: nearfix2.py APP_CPP...
import sys

def rep(s, old, new, what):
    assert s.count(old) == 1, "%s (%d)" % (what, s.count(old))
    return s.replace(old, new, 1)

for p in sys.argv[1:]:
    s = open(p, encoding="utf-8").read()
    s = rep(s, """                    // (nearmiss: the kerb lane of a city street - no highway, ramp or bridge: on nt_ai83 the pick was an elevated
                    //  highway - that runs straight on through the next junction into the street's next kerb lane, where the
                    //  people stand (ai_tests::straightOn). The core grid's lanes are a block, ~80 m, long: on nt_ai101 none had
                    //  the 115 m run once asked for, and the car was never placed. The car 40-70 m short of the junction, to be
                    //  up to speed by the people; a street with one lane each way first, any kerb lane in the second round)
                    if (nearmiss) {
                        if ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_NOTRAFFIC)) || L.right >= 0 || (k < 60 && L.count > 1))
                            continue;
                        uu = Max(L.u0 + 6.f, L.u1 - 70.f);
                        if (L.u1 - uu < 40.f || dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, L.u1)) < 0.995f ||
                            ai_tests::straightOn(game, li) < 0)
                            continue;""", """                    // (nearmiss: the kerb lane of a city street - no highway, ramp or bridge: on nt_ai83 the pick was an elevated
                    //  highway - the car setting off near its start, the people along its last 40 m (the core grid's lanes are a
                    //  block, ~80 m, long: on nt_ai101 none had the 115 m run once asked for, and the car was never placed), and
                    //  straight on through the junction after them (ai_tests::straightOn). A street with one lane each way first,
                    //  any kerb lane in the second round)
                    if (nearmiss) {
                        if ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_NOTRAFFIC)) || L.right >= 0 || (k < 60 && L.count > 1))
                            continue;
                        uu = L.u0 + 6.f;
                        if (L.u1 - uu < 68.f || dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, L.u1)) < 0.995f ||
                            ai_tests::straightOn(game, li) < 0)
                            continue;""", "setup pick")
    s = rep(s, """            // stuck for 10 s, back onto a lane further on. First, 2 s in: four people stood right at the kerb along the
            // street's next kerb lane, straight on through the junction ahead (the car's route set that way) - where a car
            // going by close at speed startles them: pedai.cpp""", """            // stuck for 10 s, back onto a lane further on. First, 2 s in: four people stood right at the kerb along the last
            // 40 m of the car's kerb lane, the traffic queued ahead on it sent away (on nt_ai101c a pickup at the light held the
            // car up) - where a car going by close at speed startles them: pedai.cpp - then straight on through the junction""",
            "branch comment")
    s = rep(s, """                const AI::Driver* d0 = game.traffic.get(pv);
                int sc = d0 ? ai_tests::straightOn(game, d0->path) : -1;
                if (sc < 0) LOG("autoplay nearmiss: no kerb lane straight on ahead of the car (path %d) - nobody stood at the kerb", d0 ? d0->path : -1);
                if (sc >= 0) {
                    int lane = game.laneGraph.conns[sc].to;
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    float toNext = game.laneGraph.lanes[d0->path].u1 - d0->u + game.laneGraph.conns[sc].length;   // (to the next lane's start)
                    // (out from the lane's middle by the car's half width and 1.2 m: past a dive's reach, within a start's)
                    float out = game.vassets[game.vehicles[pv].model].spec.boxHalf.x + 1.2f;
                    float step = Min(15.f, (L.u1 - L.u0 - 12.f) / 3.f);
                    for (int k = 0; k < 4; k++) {
                        // (not where a car is parked at the kerb: a little on, or back)
                        float u = L.u0 + 6.f + k * step;
                        vec3 at = game.laneGraph.lanePos(lane, u, out);
                        for (int tries = 0; tries < 5; tries++) {
                            float uTry = Clamp(L.u0 + 6.f + k * step + (tries & 1 ? 1.f : -1.f) * (float)((tries + 1) / 2) * 2.5f, L.u0 + 3.f, L.u1 - 3.f);""",
               """                const AI::Driver* d0 = game.traffic.get(pv);
                int lane = d0 ? d0->path : -1;
                if (lane < 0 || lane >= (int)game.laneGraph.lanes.size()) {
                    LOG("autoplay nearmiss: the car is not on a lane (path %d) - nobody stood at the kerb", lane);
                } else {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    // (the traffic queued ahead on the lane sent away: nothing between the car and the people)
                    int cleared = 0;
                    for (int vi = 0; vi < (int)game.vehicles.size(); vi++) {
                        if (vi == pv || !game.vehicles[vi].used || game.vehicles[vi].persistent) continue;
                        const AI::Driver* dv = game.traffic.get(vi);
                        if (dv && dv->path == lane && dv->u > d0->u) {
                            game.despawnVehicle(vi, true);
                            cleared++;
                        }
                    }
                    LOG("autoplay nearmiss: %d car(s) ahead on lane %d sent away", cleared, lane);
                    // (out from the lane's middle by the car's half width and 1.2 m: past a dive's reach, within a start's)
                    float out = game.vassets[game.vehicles[pv].model].spec.boxHalf.x + 1.2f;
                    for (int k = 0; k < 4; k++) {
                        // (8-38 m short of the lane's end, at least 15 m ahead of the car; not where a car is parked at the kerb: a
                        //  little on, or back)
                        float uk = L.u1 - 8.f - 10.f * (float)k;
                        if (uk < d0->u + 15.f) break;
                        float u = uk;
                        vec3 at = game.laneGraph.lanePos(lane, u, out);
                        for (int tries = 0; tries < 5; tries++) {
                            float uTry = Clamp(uk + (tries & 1 ? 1.f : -1.f) * (float)((tries + 1) / 2) * 2.5f, d0->u + 12.f, L.u1 - 3.f);""",
               "kerb peds")
    s = rep(s, """                        LOG("autoplay nearmiss: ped %d at the kerb %.0f m ahead (lane %d, past the junction; %.2f m out from its middle, the lane %.1f m wide)", id,
                            toNext + u - L.u0, lane, out, L.width);""", """                        LOG("autoplay nearmiss: ped %d at the kerb %.0f m ahead (lane %d, %.0f m from its end; %.2f m out from its middle, %.1f m wide)", id,
                            u - d0->u, lane, L.u1 - u, out, L.width);""", "kerb log")
    open(p, "w", encoding="utf-8", newline="").write(s)
    print("ok", p)
