# AI agent: the taxi test's cab put on the lane already moving (8 m/s), as a cab cruising by - from a standstill a pull-over
# never moves off (traffic_core.cpp stops a pulling-over car once it is at the kerb). Usage: taxifix3.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
i = s.rindex('} else if (autoplay == "taxi") {')
j = s.index('} else if (autoplay == ', i + 10)
seg = s[i:j]
old = """                        game.attachTraffic(vid, lane, u);
                        cab = vid;"""
new = """                        game.attachTraffic(vid, lane, u);
                        // (already cruising, as a cab coming by: from a standstill a pull-over never moves off - on nt_ai94b the
                        //  cab stood 38 m back and gave up after 25 s as the fare walked all the way to it)
                        game.vehicles[vid].sim.body.vel = vec3(game.laneGraph.laneTangent(lane, u) * 8.f, 0.f);
                        game.vehicles[vid].sim.sleeping = false;
                        cab = vid;"""
assert seg.count(old) == 1
seg = seg.replace(old, new, 1)
s = s[:i] + seg + s[j:]
open(p, "w").write(s)
print("ok", p)
