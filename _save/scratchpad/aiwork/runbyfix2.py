# AI agent: the runby test's night meetings (set 15 test): somebody alone put on the player's sidewalk 22 m ahead, coming
# their way, every 25 s - applied the same way to any copy of app.cpp that has runbyfix.py's change. Usage: runbyfix2.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
i = s.rindex('} else if (autoplay == "runby") {')   # (the per-frame branch: the last of them)
j = s.index('} else if (autoplay == ', i + 10)
seg = s[i:j]
old1 = """            static float logT = 0.f, shotT = 4.f, progT = 0.f;
            static vec2 progP(0.f);"""
new1 = """            static float logT = 0.f, shotT = 4.f, progT = 0.f, meetT = 66.f;
            static vec2 progP(0.f);"""
old2 = """                        LOG("autoplay runby t=%.1f: caught on something - on along the walk", t);
                    }
                }
            }
            shotT -= dt;"""
new2 = """                        LOG("autoplay runby t=%.1f: caught on something - on along the walk", t);
                    }
                }
                // (by night, every 25 s: somebody alone put on the player's sidewalk 22 m ahead, coming their way - the
                //  meeting head-on the wary step out of; on nt_ai86 the quiet street gave nobody in 85 s)
                if (night && init && w.link >= 0 && w.link < (int)game.laneGraph.walkLinks.size() && t > meetT) {
                    const AI::WalkLink& WL = game.laneGraph.walkLinks[w.link];
                    float xa = w.x + 22.f;
                    meetT = t + 5.f;   // (no room on this link: again in a moment)
                    if (WL.kind == AI::WL_SIDEWALK && xa < WL.length - 1.f) {
                        vec3 at = game.laneGraph.walkPos(w.link, xa, 0.f, w.fromA);
                        vec2 tg = game.laneGraph.walkTangent(w.link, xa, w.fromA);
                        int id = game.spawnPed(game.randomCivilianChar(hash32((u32)(t * 10.f) + 77u), 0),
                                               dvec3(at.x, at.y, game.groundHeight(at.x, at.y, at.z + 1.5f)), AI::dirYaw(-tg), FAC_CIVILIAN);
                        if (id >= 0) {
                            PedAI& qa = game.pedAI(id);
                            game.peds[id].brain.type = BRAIN_WANDER;
                            game.peds[id].brain.edge = w.link;
                            qa.activity = ACT_WALK;
                            qa.leader = -1;
                            qa.walk = AI::Walker();
                            qa.walk.link = w.link;
                            qa.walk.fromA = !w.fromA;   // (the other way along it: toward the player)
                            qa.walk.x = WL.length - xa;
                            qa.walk.seed = hash32((u32)id * 31u + 5u);
                            qa.navOk = true;
                            meetT = t + 25.f;
                            LOG("autoplay runby t=%.1f: ped %d put 22 m ahead on the player's sidewalk, coming their way", t, id);
                        }
                    }
                }
            }
            shotT -= dt;"""
for a, b in ((old1, new1), (old2, new2)):
    assert seg.count(a) == 1, a[:60]
    seg = seg.replace(a, b, 1)
s = s[:i] + seg + s[j:]
open(p, "w").write(s)
print("ok", p)
