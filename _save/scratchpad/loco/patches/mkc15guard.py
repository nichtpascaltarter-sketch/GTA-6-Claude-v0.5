#!/usr/bin/env python3
# mkc15guard.py: patches/wallguard_c15.py from patches/wallguard.py - the same wall guard, its anchors and lines adapted to
# c15's code (the bend's lateral landing offset stairLat: reset with the fit, composed with the pull)
L='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/'
s=open(L+'patches/wallguard.py').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:100]))
    return s.replace(old,new)
s=sub(s, '''s = sub(s, """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairShift[s] = 0.f;''', '''s = sub(s, """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairLat[s] = vec3(0);
            A.stairShift[s] = 0.f;''')
s=sub(s, '''""", """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairShift[s] = 0.f;''', '''""", """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairLat[s] = vec3(0);
            A.stairShift[s] = 0.f;''')
s=sub(s, '''                const vec3 to = A.stairDir[s] * A.stairShift[s];""", """''', '''                const vec3 to = A.stairDir[s] * A.stairShift[s] + A.stairLat[s];""", """''')
s=sub(s, '''                const vec3 to = A.stairDir[s] * A.stairShift[s] + A.wallPull[s];""")''', '''                const vec3 to = A.stairDir[s] * A.stairShift[s] + A.stairLat[s] + A.wallPull[s];""")''')
s=sub(s, '''            m = landAhead(m, sT, leadM + A.stairShift[s] + 0.65f * L);
        }""", """''', '''            m = landAhead(m, sT, leadM + A.stairShift[s] + 0.65f * L) + A.stairLat[s];
        }""", """''')
s=sub(s, '''+ (A.wallCheck[s] ? vec3(0.f) : A.wallPull[s]);
        }""")''', '''+ (A.wallCheck[s] ? vec3(0.f) : A.wallPull[s]) + A.stairLat[s];
        }""")''')
s=sub(s, '''s = sub(s, """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;""", """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;''', '''s = sub(s, """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairLat[k] = vec3(0);
        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;""", """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairLat[k] = vec3(0);
        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;''')
s=sub(s, '''            lm = landAhead(lm, sT, leadM) + A.wallPull[s];
            const bool fresh =''', '''            lm = landAhead(lm, sT, leadM) + A.wallPull[s] + A.stairLat[s];
            const bool fresh =''')
open(L+'patches/wallguard_c15.py','w').write(s); print('wallguard_c15.py written from wallguard.py')
