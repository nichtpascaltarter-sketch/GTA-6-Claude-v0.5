# AI agent: the runby test's day run (set 15 test), applied the same way to any copy of app.cpp. Usage: runbyfix.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """            static AI::Walker w;
            static bool init = false, night = false;
            static float logT = 0.f, shotT = 4.f, stuckT = 0.f;
            static int shots = 0;"""
new1 = """            static AI::Walker w;
            static bool init = false, night = false;
            static float logT = 0.f, shotT = 4.f, progT = 0.f;
            static vec2 progP(0.f);
            static int shots = 0;"""
old2 = """                    init = game.pedNav.place(w, pp, 0x5eedu + (night ? 7u : 0u), 40.f);
                    w.speed = night ? 1.35f : 5.f;"""
new2 = """                    init = game.pedNav.place(w, pp, 0x5eedu + (night ? 7u : 0u), 40.f);
                    w.speed = night ? 1.35f : 6.5f;   // (by day the walker keeps pace with the sprint, as k9's run)"""
old3 = """                if (init && w.link >= 0) {
                    float fy = pl->yaw;
                    vec2 v = game.pedNav.step(w, pp, dt, -1, &fy);"""
new3 = """                if (init && w.link >= 0) {
                    if (!night) w.avoidCrossing = true;   // (by day round the blocks: not stood at a red light)
                    float fy = pl->yaw;
                    vec2 v = game.pedNav.step(w, pp, dt, -1, &fy);"""
old4 = """                    stuckT = length(v) > 0.2f && length(pl->vel.xy()) < 0.25f ? stuckT + dt : 0.f;
                    if (stuckT > 3.f && w.link < (int)game.laneGraph.walkLinks.size()) {
                        stuckT = 0.f;
                        w.x = Min(w.x + 3.f, game.laneGraph.walkLinks[w.link].length - 0.1f);
                        vec3 q = game.laneGraph.walkPos(w.link, w.x, w.lat, w.fromA);"""
new4 = """                    // (caught on something - judged by the ground made over 2 s, not the speed: on nt_ai86 the runner
                    //  jittered at 0.2-0.9 m/s between a parked car and a bin at the kerb for 40 s) - on along the walk
                    progT += dt;
                    bool caught = false;
                    if (progT > 2.f) {
                        caught = length(v) > 0.2f && length(pp - progP) < (night ? 0.8f : 2.5f);
                        progT = 0.f;
                        progP = pp;
                    }
                    if (caught && w.link < (int)game.laneGraph.walkLinks.size()) {
                        w.x = Min(w.x + 4.f, game.laneGraph.walkLinks[w.link].length - 0.1f);
                        w.lat *= 0.3f;   // (toward the middle of the sidewalk, off the kerb)
                        vec3 q = game.laneGraph.walkPos(w.link, w.x, w.lat, w.fromA);"""
i = s.rindex('} else if (autoplay == "runby") {')   # (the per-frame branch: the last of them)
j = s.index('} else if (autoplay == ', i + 10)
seg = s[i:j]
for a, b in ((old1, new1), (old2, new2), (old3, new3), (old4, new4)):
    assert seg.count(a) == 1, a[:60]
    seg = seg.replace(a, b, 1)
s = s[:i] + seg + s[j:]
open(p, "w").write(s)
print("ok", p)
