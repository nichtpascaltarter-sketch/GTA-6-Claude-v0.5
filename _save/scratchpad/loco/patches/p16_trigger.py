#!/usr/bin/env python3
# The scan's trigger against the slope: a landing's ground off the ground near the foot (or under the other foot) by more
# than the slope under the feet accounts for. usage: p16_trigger.py <tree>...
import sys
for tree in sys.argv[1:]:
    p = tree + '/src/anim/animator.cpp'
    s = open(p).read()
    a = '''        if (swinging && terrain && in.footProbes && !A.scanDone[s] && !A.scanWant && swingU[s] > 0.12f && swingU[s] < 0.6f &&
            (A.stairSeen < 1.5f || fabsf(A.groundAhead[s] - (s ? A.footR : A.footL)) > 0.04f ||
             (A.planted[1 - s] && fabsf(A.groundAhead[s] - (s ? A.footL : A.footR)) > 0.1f))) {'''
    assert s.count(a) == 1, tree
    b = '''        bool stepsAhead = false;
        if (swinging && terrain && in.footProbes && !A.scanDone[s] && !A.scanWant && swingU[s] > 0.12f && swingU[s] < 0.6f) {
            // (the heights against the slope under the feet: on a hill the landing is that much higher or lower anyway)
            const vec2 mdv(md.x, md.y);
            const vec3 sm = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), (heel[s] + ball[s]) * 0.5f - pa);
            const float slopeA = slopeX * md.x + slopeY * md.y;
            const float landA = cycle * ((1.f - swingU[s]) * (1.f - duty) + A.strikeLead) + A.stairShift[s] + 0.4f * L;
            const float nearA = dot(vec2(sm.x, sm.y), mdv) + Min(0.15f, spd * 0.2f);
            const int o = 1 - s;
            const float otherA = dot(vec2(A.plantP[o].x, A.plantP[o].y), mdv) + (A.ballProbe[o] ? L : 0.5f * L);
            stepsAhead = A.stairSeen < 1.5f || fabsf(A.groundAhead[s] - (s ? A.footR : A.footL) - slopeA * (landA - nearA)) > 0.04f ||
                         (A.planted[o] && fabsf(A.groundAhead[s] - (o ? A.footR : A.footL) - slopeA * (landA - otherA)) > 0.1f);
        }
        if (stepsAhead) {'''
    s = s.replace(a, b)
    open(p, 'w').write(s)
    print('patched', p)
