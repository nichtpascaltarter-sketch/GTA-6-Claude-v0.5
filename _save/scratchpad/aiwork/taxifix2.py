# AI agent: the taxi test's lane pick relaxed (downtown blocks are shorter than 95 m), for any app.cpp with taxitest.py's
# mode. Usage: taxifix2.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
i = s.rindex('} else if (autoplay == "taxi") {')
j = s.index('} else if (autoplay == ', i + 10)
seg = s[i:j]
reps = [
 ("""                    if (L.u1 - uu < 95.f) continue;   // (room for the run up and the stop)
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 90.f)) < 0.99f) continue;""",
  """                    if (L.u1 - uu < 60.f) continue;   // (room for the run up and the stop: the cab sees a hailer 18-80 m ahead)
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 50.f)) < 0.99f) continue;"""),
 ("""                    vec2 tg = game.laneGraph.laneTangent(lane, u + 70.f);
                    vec3 at = game.laneGraph.lanePos(lane, u + 70.f, L.width * 0.5f + 1.1f);   // (on the sidewalk, a step from the kerb)""",
  """                    vec2 tg = game.laneGraph.laneTangent(lane, u + 45.f);
                    vec3 at = game.laneGraph.lanePos(lane, u + 45.f, L.width * 0.5f + 1.1f);   // (on the sidewalk, a step from the kerb)"""),
 ("""cab %d (model %d) 70 m back on lane %d""", """cab %d (model %d) 45 m back on lane %d"""),
 ("""near the player, and a cab put 70 m back in
            // that kerb lane coming their way.""", """near the player, and a cab put 45 m back in
            // that kerb lane coming their way."""),
]
for a, b in reps:
    assert seg.count(a) == 1, a[:60]
    seg = seg.replace(a, b, 1)
s = s[:i] + seg + s[j:]
open(p, "w").write(s)
print("ok", p)
