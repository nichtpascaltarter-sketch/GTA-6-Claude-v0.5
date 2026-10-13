# AI agent: the crosswalk test's radio off over the stripes and on the sidewalk (set 14 test), for any copy of app.cpp
# that does not have it yet. Usage: xwalkfix.py APP_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """                if (putCar(overAt, overYaw)) {
                    stage = 1;
                    LOG("autoplay crosswalk: car %d moved up over the crosswalk at t=%.1f (the radio off)", car, t);"""
new1 = """                if (putCar(overAt, overYaw)) {
                    stage = 1;
                    game.vehicles[car].radio = -1;   // (the radio off: a new car comes with a station on - on nt_ai86 it was on)
                    LOG("autoplay crosswalk: car %d moved up over the crosswalk at t=%.1f (the radio off)", car, t);"""
old2 = """                    if (putCar(at.xy(), AI::dirYaw(tg))) {
                        stage = 2;
                        camP = vec3(at.xy() + tg * 9.f + AI::rightOf(tg) * 1.5f, at.z + 1.9f);"""
new2 = """                    if (putCar(at.xy(), AI::dirYaw(tg))) {
                        stage = 2;
                        game.vehicles[car].radio = -1;   // (the radio off)
                        camP = vec3(at.xy() + tg * 9.f + AI::rightOf(tg) * 1.5f, at.z + 1.9f);"""
assert s.count(old1) == 1 and s.count(old2) == 1
s = s.replace(old1, new1, 1).replace(old2, new2, 1)
open(p, "w").write(s)
print("ok", p)
