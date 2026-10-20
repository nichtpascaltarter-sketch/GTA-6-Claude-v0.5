# AI agent: the nearmiss test's lane pick, fixed (set 17). On nt_ai101 the pick (a kerb lane with a 150 / 115 m run, no highway,
# ramp or bridge) found nothing - the core grid's lanes are a block, ~80 m, long - so the player stayed on foot at the spawn and
# the test never ran. Now: a kerb lane that runs straight on through the next junction into the street's next kerb lane
# (ai_tests::straightOn), the car 40-70 m short of the junction, its route set straight on, the kerb people along the next lane
# (not where a car is parked). Usage: nearfix.py APP_CPP
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

def rep(old, new, what):
    global s
    assert s.count(old) == 1, "%s (%d)" % (what, s.count(old))
    s = s.replace(old, new, 1)

# 1. the helper, in ai_tests
rep("""    return fallback;
}
}  // namespace ai_tests""", """    return fallback;
}

// The connector straight on from a kerb lane's end into the street's next kerb lane, that one straight along 50 m or more
// (the nearmiss test's run: the city grid's lanes are a block long), or -1
int straightOn(const GameWorld& g, int lane) {
    const AI::LaneGraph& G = g.laneGraph;
    if (lane < 0 || lane >= (int)G.lanes.size()) return -1;
    const AI::Lane& L = G.lanes[lane];
    for (int ci : L.out) {
        const AI::Connector& C = G.conns[ci];
        const AI::Lane& T = G.lanes[C.to];
        if (C.turn != AI::TK_STRAIGHT || T.right >= 0 || (T.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE | AI::LF_DIRT | AI::LF_NOTRAFFIC)))
            continue;
        if (T.u1 - T.u0 < 50.f || dot(G.laneTangent(lane, L.u1), G.laneTangent(C.to, T.u0)) < 0.995f ||
            dot(G.laneTangent(C.to, T.u0), G.laneTangent(C.to, T.u1)) < 0.995f)
            continue;
        return ci;
    }
    return -1;
}
}  // namespace ai_tests""", "helper")

# 2. the setup's lane pick
rep("""                    if (k >= 60 && !nearmiss) break;   // (nearmiss: a second round, a shorter run, when no long one is about)""",
    """                    if (k >= 60 && !nearmiss) break;   // (nearmiss: a second round, any kerb lane, when no one-lane street is about)""",
    "round comment")
rep("""                    // (nearmiss: the kerb lane of a city street, a long run - no highway, ramp or bridge: on nt_ai83 the pick
                    //  was an elevated highway)
                    if (nearmiss && ((L.flags & (AI::LF_HIGHWAY | AI::LF_RAMP | AI::LF_BRIDGE)) || L.right >= 0 ||
                                     L.u1 - Max(uu, L.u0 + 6.f) < (k < 60 ? 150.f : 115.f)))
                        continue;
""", """                    // (nearmiss: the kerb lane of a city street - no highway, ramp or bridge: on nt_ai83 the pick was an elevated
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
                            continue;
                        lane = li;
                        u = uu;
                        continue;
                    }
""", "setup pick")

# 3. the kerb people, along the next lane
rep("""            // stuck for 10 s, back onto a lane further on. First, 2 s in: four people stood right at the kerb along the
            // car's kerb lane 60-105 m ahead (where a car going by close at speed startles them: pedai.cpp)""",
    """            // stuck for 10 s, back onto a lane further on. First, 2 s in: four people stood right at the kerb along the
            // street's next kerb lane, straight on through the junction ahead (the car's route set that way) - where a car
            // going by close at speed startles them: pedai.cpp""", "branch comment")
rep("""                const AI::Driver* d0 = game.traffic.get(pv);
                int lane = d0 ? d0->path : -1;
                if (lane >= 0 && lane < (int)game.laneGraph.lanes.size()) {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    // (out from the lane's middle by the car's half width and 1.2 m: past a dive's reach, within a start's)
                    float out = game.vassets[game.vehicles[pv].model].spec.boxHalf.x + 1.2f;
                    for (int k = 0; k < 4; k++) {
                        float u = d0->u + 60.f + k * 15.f;
                        if (u > L.u1 - 5.f) break;
                        vec3 at = game.laneGraph.lanePos(lane, u, out);
""", """                const AI::Driver* d0 = game.traffic.get(pv);
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
                            float uTry = Clamp(L.u0 + 6.f + k * step + (tries & 1 ? 1.f : -1.f) * (float)((tries + 1) / 2) * 2.5f, L.u0 + 3.f, L.u1 - 3.f);
                            vec3 a3 = game.laneGraph.lanePos(lane, uTry, out);
                            bool parked = false;
                            for (const Vehicle& pk : game.vehicles)
                                if (pk.used && length(pk.sim.body.pos.toVec3().xy() - a3.xy()) < 3.2f) parked = true;
                            if (!parked) {
                                u = uTry;
                                at = a3;
                                break;
                            }
                        }
""", "kerb peds")
rep("""                        LOG("autoplay nearmiss: ped %d at the kerb %.0f m ahead (lane %d, %.2f m out from its middle, the lane %.1f m wide)", id, u - d0->u, lane, out, L.width);""",
    """                        LOG("autoplay nearmiss: ped %d at the kerb %.0f m ahead (lane %d, past the junction; %.2f m out from its middle, the lane %.1f m wide)", id,
                            toNext + u - L.u0, lane, out, L.width);""", "kerb log")

# 4. the route straight on at the switch to flee mode
rep("""                    if (d->mode != AI::DM_FLEE) {
                        d->mode = AI::DM_FLEE;
                        d->routeLen = 0;
                    }
                    d->threat = vp - f * 60.f;""", """                    if (d->mode != AI::DM_FLEE) {
                        d->mode = AI::DM_FLEE;
                        d->routeLen = 0;
                        // (straight on through the junction ahead, past the people at the kerb; on from there as flee mode picks)
                        int sc = ai_tests::straightOn(game, d->path);
                        if (sc >= 0) {
                            d->route[0] = (int)game.laneGraph.lanes.size() + sc;
                            d->route[1] = game.laneGraph.conns[sc].to;
                            d->routeLen = 2;
                            d->gateConn = -1;
                            d->stopDone = false;
                            LOG("autoplay nearmiss t=%.1f: car %d on lane %d, %.0f m short of the junction, routed straight on into lane %d (%.0f m)", t, pv,
                                d->path, game.laneGraph.lanes[d->path].u1 - d->u, d->route[1],
                                game.laneGraph.lanes[d->route[1]].u1 - game.laneGraph.lanes[d->route[1]].u0);
                        }
                    }
                    d->threat = vp - f * 60.f;""", "flee route")
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", p)
