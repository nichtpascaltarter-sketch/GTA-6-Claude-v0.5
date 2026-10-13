# AI agent: the car show's start/end fix (set 14), applied the same way to any copy of ai.cpp. Usage: showfix.py AI_CPP
import sys
p = sys.argv[1]
s = open(p).read()
old1 = """        if (!ai.showOn && allowed && ai.showT > 2.f) {"""
new1 = """        if (!ai.showOn && allowed && active && ai.showT > 2.f) {"""
old2 = """            bool gone = sv != ai.showVeh || !carOk || length(cp - ai.showAt) > 40.f;
            if (gone || !allowed || time - ai.showLastT > 4.0) {
                ai.showOn = false;
                ai.showEndT = time;"""
new2 = """            bool gone = sv != ai.showVeh || !carOk || length(cp - ai.showAt) > 40.f;
            // (a lull between the donuts - the car still going round close by - is not the end yet: 9 s of that, 4 s
            //  stood still; on nt_ai86 a lull ended the show and the next turn began another, the crowd breaking up and
            //  forming again)
            bool circling = !gone && vehicles[sv].sim.speed() > 1.5f && length(cp - ai.showAt) < 25.f;
            if (gone || !allowed || time - ai.showLastT > (circling ? 9.0 : 4.0)) {
                ai.showOn = false;
                ai.showEndT = time;
                ai.showT = 0.f;   // (another show takes a fresh build-up: on nt_ai86 one began again at once, the car stood)"""
assert s.count(old1) == 1 and s.count(old2) == 1
s = s.replace(old1, new1, 1).replace(old2, new2, 1)
open(p, "w").write(s)
print("ok", p)
