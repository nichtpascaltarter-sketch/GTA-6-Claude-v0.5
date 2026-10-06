#!/usr/bin/env python3
# Landing short of a step up under the toes / past a step down under the heel (curbs, stairs), from the swinging foot's
# existing look-ahead probe (late in a swing every other one goes to the landing foot's toes or heel). Usage:
# p11_edges.py <tree>   (patches <tree>/src/anim/character.h and animator.cpp)
import sys, os
tree = sys.argv[1]
ch = os.path.join(tree, 'src/anim/character.h')
an = os.path.join(tree, 'src/anim/animator.cpp')

s = open(ch).read()
old = '''    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
'''
new = old + '''    u8 probeKind[2] = {0, 0};                 // a swinging foot's last look-ahead probe: 0 where it lands, 1 its toes there, 2 its heel
    u8 edgeSeq[2] = {0, 0};                   // (the order of those, late in the swing)
    float groundToe[2] = {0.f, 0.f};          // the ground under the landing foot's toes and heel (kept with the world)
    float groundHeel[2] = {0.f, 0.f};
    float edgeShift[2] = {0.f, 0.f};          // a landing moved back from a step up under the toes (+), on past a step down
                                              // under the heel (-), m
'''
assert s.count(old) == 1
s = s.replace(old, new)
open(ch, 'w').write(s)

s = open(an).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)

# the probe results: the landing foot's toes / heel
rep('''        for (int s = 0; s < 2; s++) {
            A.groundAhead[s] -= dzRoot;
            if (A.probeAhead[s]) {
                A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * ks;
                *fz[s] -= dzRoot;
            } else {
                *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * (held[s] ? 1.f : (swingU[s] > 0.f ? ks : kg)) - dzRoot;
                if (swingU[s] <= 0.f) A.groundAhead[s] = *fz[s];
            }''', '''        for (int s = 0; s < 2; s++) {
            A.groundAhead[s] -= dzRoot;
            A.groundToe[s] -= dzRoot;
            A.groundHeel[s] -= dzRoot;
            if (A.probeAhead[s]) {
                if (A.probeKind[s] == 1) A.groundToe[s] = gIn[s];
                else if (A.probeKind[s] == 2) A.groundHeel[s] = gIn[s];
                else A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * ks;
                *fz[s] -= dzRoot;
            } else {
                *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * (held[s] ? 1.f : (swingU[s] > 0.f ? ks : kg)) - dzRoot;
                if (swingU[s] <= 0.f) A.groundAhead[s] = A.groundToe[s] = A.groundHeel[s] = *fz[s];
            }''')

# the stepping and ground branches: no landing shift
rep('''        if (A.stepT[s] >= 0.f) {
            A.stepT[s] += dt / A.stepDur[s];''', '''        if (A.stepT[s] >= 0.f) {
            A.edgeShift[s] = 0.f;
            A.stepT[s] += dt / A.stepDur[s];''')
rep('''            A.pivotW[s] = approach(A.pivotW[s], piv ? 1.f : 0.f, dt * (piv ? 10.f : 4.f));''',
    '''            A.pivotW[s] = approach(A.pivotW[s], piv ? 1.f : 0.f, dt * (piv ? 10.f : 4.f));
            A.edgeShift[s] = 0.f;''')

# the free foot: lands short of a step up under its toes, past a step down under its heel
rep('''            const bool late = swingU[s] > 0.5f || A.moveW < 0.95f;
            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale) {
                const vec3 h = shownHeel(s);
                A.plantP[s] = vec3(h.x, h.y, 0.f);
                A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                A.footHold[s] = true;
            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                float k = expf(-dt / 0.07f);
                A.plantCorr[s] = A.plantCorr[s] * k;
                A.corrYaw[s] *= k;
            }''', '''            const bool late = swingU[s] > 0.5f || A.moveW < 0.95f;
            // a step up (a curb, a stair) under where its toes come down: the step lands short of it; a step down under its
            // heel: the step goes on past the edge (a foot never lands straddling one, the toes in a riser)
            float shT = 0.f;
            if (walking && swingU[s] > 0.f) {
                const float up = A.groundToe[s] - A.groundAhead[s], down = A.groundHeel[s] - A.groundAhead[s];
                if (up > 0.04f && up < 0.45f && down < 0.04f) shT = toeReach + 0.02f * scale;
                else if (down > 0.04f && down < 0.45f && up < 0.04f) shT = -(heelReach + 0.02f * scale);
            }
            A.edgeShift[s] = approach(A.edgeShift[s], shT, dt * 1.2f);
            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale) {
                const vec3 h = shownHeel(s);
                A.plantP[s] = vec3(h.x, h.y, 0.f);
                A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                A.footHold[s] = true;
            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                float k = expf(-dt / 0.07f);
                const vec3 to = vec3(-md.x, -md.y, 0.f) * A.edgeShift[s];
                A.plantCorr[s] = to + (A.plantCorr[s] - to) * k;
                A.corrYaw[s] *= k;
            }''')

# reach of the toes and heel from the middle of the foot (declared with the foot sizes)
rep('''    const float scale = L / 0.197f;            // foot size relative to the male reference''',
    '''    const float scale = L / 0.197f;            // foot size relative to the male reference
    // from the middle of the sole: to the toe tip, to the heel point
    const float toeReach = 0.5f * L + sk.boneLength[B_TOE_L], heelReach = 0.5f * L;''')

# the probes: late in a swing every other look-ahead goes to the landing foot's toes or heel
rep('''        A.probeAhead[s] = swinging && !A.probeAhead[s];
        if (A.probeAhead[s]) m = m + vec3(md.x, md.y, 0.f) * Clamp((1.f - swingU[s]) * cycle, 0.05f, 1.2f);''',
    '''        A.probeAhead[s] = swinging && !A.probeAhead[s];
        A.probeKind[s] = 0;
        if (A.probeAhead[s]) {
            m = m + vec3(md.x, md.y, 0.f) * Clamp((1.f - swingU[s]) * cycle, 0.05f, 1.2f);
            // (late in the swing every other one goes to the landing foot's toes or heel: a curb or a stair edge under it)
            if (swingU[s] > 0.4f) {
                const int q = A.edgeSeq[s]++ & 3;
                const vec3 F = rotate(qz(fyaw[s] + A.corrYaw[s]), vec3(0.f, 1.f, 0.f));
                if (q == 1) A.probeKind[s] = 1, m = m + F * toeReach;
                else if (q == 3) A.probeKind[s] = 2, m = m - F * heelReach;
            }
        }''')

# resets
rep('''        groundRaw[k] = groundAhead[k] = pivotW[k] = 0.f;''',
    '''        groundRaw[k] = groundAhead[k] = pivotW[k] = groundToe[k] = groundHeel[k] = edgeShift[k] = 0.f;
        probeKind[k] = edgeSeq[k] = 0;''')
rep('''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;''',
    '''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;
                probeKind[s] = 0;
                edgeShift[s] = 0.f;''')
open(an, 'w').write(s)
print('ok')
