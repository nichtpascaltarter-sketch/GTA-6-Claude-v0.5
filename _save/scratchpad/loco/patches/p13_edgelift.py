#!/usr/bin/env python3
# A foot that comes down across a step edge (a curb, a stair) rests on the higher side: the ground where the swing was
# landing (groundAhead) against the ground probed under the foot, kept for the stance. usage: p13_edgelift.py <tree>
import sys, os
tree = sys.argv[1]
ch = os.path.join(tree, 'src/anim/character.h')
an = os.path.join(tree, 'src/anim/animator.cpp')

s = open(ch).read()
old = '''    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
'''
new = old + '''    float edgeLift[2] = {0.f, 0.f};           // a foot down across a step edge rests on the higher side: m above the
                                              // ground probed under it
'''
assert s.count(old) == 1
s = s.replace(old, new)
open(ch, 'w').write(s)

s = open(an).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)

# the landing ground is kept until the foot has been down for an update (the plant reads it)
rep('''                if (swingU[s] <= 0.f) A.groundAhead[s] = *fz[s];''',
    '''                if (swingU[s] <= 0.f && A.plantedPrev[s]) A.groundAhead[s] = *fz[s];''')

# a helper: the step edge under a foot coming down (the higher side, up to a stair's rise)
rep('''    auto pivotA = [&](int s) { return lerp(heel[s], ball[s], wBall[s]); };''',
    '''    auto pivotA = [&](int s) { return lerp(heel[s], ball[s], wBall[s]); };
    // a foot coming down where its swing was landing on higher ground than is probed under it (across a curb's or a
    // stair's edge): it rests on the higher side, not with its toes in the riser
    auto edgeLiftAt = [&](int s) {
        const float e = A.groundAhead[s] - (s ? A.footR : A.footL);
        return e > 0.03f && e < 0.22f ? e : 0.f;
    };''')

# gait plant
rep('''                if (contact && !A.planted[s]) {
                    A.planted[s] = true;
                    A.footHold[s] = false;
                    vec3 h = shownHeel(s);''',
    '''                if (contact && !A.planted[s]) {
                    if (!A.footHold[s]) A.edgeLift[s] = edgeLiftAt(s);
                    A.planted[s] = true;
                    A.footHold[s] = false;
                    vec3 h = shownHeel(s);''')

# standing plant and stepping: no edge
rep('''            } else if (A.stepT[s] < 0.f && !A.planted[s] && nearGround) {
                // standing: a foot that comes down stays down where it is shown
                A.planted[s] = true;''',
    '''            } else if (A.stepT[s] < 0.f && !A.planted[s] && nearGround) {
                // standing: a foot that comes down stays down where it is shown
                if (!A.footHold[s]) A.edgeLift[s] = 0.f;
                A.planted[s] = true;''')
rep('''        if (A.stepT[s] >= 0.f) {
            A.stepT[s] += dt / A.stepDur[s];''',
    '''        if (A.stepT[s] >= 0.f) {
            A.edgeLift[s] = 0.f;
            A.stepT[s] += dt / A.stepDur[s];''')

# a free foot held where it came down: the same; let go: none
rep('''            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale) {
                const vec3 h = shownHeel(s);''',
    '''            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale) {
                A.edgeLift[s] = edgeLiftAt(s);
                const vec3 h = shownHeel(s);''')
rep('''            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                float k = expf(-dt / 0.07f);''',
    '''            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                A.edgeLift[s] = 0.f;
                float k = expf(-dt / 0.07f);''')

# the ground under each foot
rep('''    float offs[2] = {A.footL, A.footR};''',
    '''    float offs[2] = {A.footL + A.edgeLift[0], A.footR + A.edgeLift[1]};''')

# resets
rep('''        groundRaw[k] = groundAhead[k] = pivotW[k] = 0.f;''',
    '''        groundRaw[k] = groundAhead[k] = pivotW[k] = edgeLift[k] = 0.f;''')
rep('''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;''',
    '''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;
                edgeLift[s] = 0.f;''')
rep('''            A.footHold[s] = false;
            A.pivotW[s] = 0.f;
        }
    }''',
    '''            A.footHold[s] = false;
            A.pivotW[s] = 0.f;
            A.edgeLift[s] = 0.f;
        }
    }''')
open(an, 'w').write(s)
print('ok')
