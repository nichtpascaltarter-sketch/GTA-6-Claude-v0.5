# AI agent: the taxi test mode, part 4 - once the fare is in, a short ride (the destination put about 150 m on down the
# street), so the drop-off is seen too: the cab pulls in at the kerb near it (DM_PULLOVER without a stop point, as before
# the stop-point change in traffic_core.cpp) and the fare gets out through the door. Usage: taxidrop.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old = """            bool fareOk = fare >= 0 && fare < (int)game.peds.size() && game.peds[fare].used && game.peds[fare].uid == fareUid;
            if (cabOk) {"""
new = """            bool fareOk = fare >= 0 && fare < (int)game.peds.size() && game.peds[fare].used && game.peds[fare].uid == fareUid;
            // (the fare in: a short ride - the destination about 150 m on down the street - so the drop-off is seen as well:
            //  in at the kerb near it at once, the plain pull-over, and the fare out through the door)
            static u32 rideFor = 0u;
            if (cabOk && fareOk && rideFor != fareUid && game.peds[fare].state == PS_INVEHICLE && game.peds[fare].vehicle == cab) {
                VehAI& rv = game.vehAI(cab);
                AI::Driver* rd = game.traffic.get(cab);
                if (rd && rv.fare == fare) {
                    rideFor = fareUid;
                    vec2 f2 = normalize(game.vehicles[cab].sim.forward().xy() + vec2(1e-4f, 0.f));
                    vec2 want = game.vehicles[cab].sim.body.pos.toVec3().xy() + f2 * 150.f;
                    float uu = 0.f;
                    int ln = game.laneGraph.nearestLane(want, f2, 40.f, &uu);
                    if (ln >= 0) {
                        rv.dest = game.laneGraph.lanePos(ln, uu).xy();
                        rd->hasDest = false;
                    }
                    LOG("autoplay taxi: the fare in at t=%.1f - a short ride to %.0f %.0f (%.0f m on)", t, rv.dest.x, rv.dest.y,
                        length(rv.dest - game.vehicles[cab].sim.body.pos.toVec3().xy()));
                }
            }
            if (cabOk) {"""
assert s.count(old) == 1, "anchor"
s = s.replace(old, new, 1)
open(p, "w").write(s)
print("ok", p)
