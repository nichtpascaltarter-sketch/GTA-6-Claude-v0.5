# AI agent: copreturn v2 - each round at its own place along the lane (round 1's car may still stand there: on nt_ai100
# round 2's car was put on top of it) and its own shots. Usage: copreturn2.py APP_CPP (after copreturn.py)
import sys
p = sys.argv[1]
s = open(p).read()
pairs = [
    ("""                round++;
                both = away = false;
                vec2 q = pl->pos.toVec3().xy();""",
     """                round++;
                both = away = false;
                shots = 0;
                float along = 10.f + 35.f * (float)(round - 1);   // (each round further along: the last car may still be there)
                vec2 q = pl->pos.toVec3().xy();"""),
    ("""                    if (L.u1 - uu < 30.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 20.f)) < 0.99f) continue;
                    lane = li;
                    u = uu;
                }
                int pm = game.findVehicleModel(Vehicles::VC_POLICE, (u32)round * 5u);""",
     """                    if (L.u1 - uu < along + 20.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + along + 10.f)) < 0.99f) continue;
                    lane = li;
                    u = uu;
                }
                int pm = game.findVehicleModel(Vehicles::VC_POLICE, (u32)round * 5u);"""),
    ("""                    vec3 c3 = game.laneGraph.lanePos(lane, u + 10.f);
                    vec2 tg = game.laneGraph.laneTangent(lane, u + 10.f);""",
     """                    vec3 c3 = game.laneGraph.lanePos(lane, u + along);
                    vec2 tg = game.laneGraph.laneTangent(lane, u + along);"""),
    ("""                if (shotT <= 0.f && shots < 45 && !away) {""",
     """                if (shotT <= 0.f && shots < 24 && !away) {"""),
]
for old, new in pairs:
    assert s.count(old) == 1, old[:80]
    s = s.replace(old, new, 1)
open(p, "w").write(s)
print("ok", p)
