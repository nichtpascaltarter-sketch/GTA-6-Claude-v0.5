#!/usr/bin/env python3
# A foot down across a curb's or a stair's edge rests on the higher side: when a foot comes down where the landing
# ground and the ground probed under it disagree, the game is asked for one more probe per update, alternately under
# its toe tip and its heel (AnimInput::groundEdgeL/R at Animator::edgeProbe), for as long as it stays down there.
# usage: p14_edgeprobe.py <tree>   (src/anim/character.h, src/anim/animator.cpp)
import sys, os
tree = sys.argv[1]
ch = os.path.join(tree, 'src/anim/character.h')
an = os.path.join(tree, 'src/anim/animator.cpp')

s = open(ch).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
rep('''    vec3 passBy = vec3(0);
    vec2 passVel = vec2(0);
    float passWeight = 0;
''', '''    vec3 passBy = vec3(0);
    vec2 passVel = vec2(0);
    float passWeight = 0;
    // the ground under a foot's toe tip or heel where Animator::wantsEdgeProbe asked for it (at Animator::edgeProbe,
    // like groundOffsetL/R): a foot down across a curb's or a stair's edge rests on the higher side
    float groundEdgeL = 0.f, groundEdgeR = 0.f;
    bool edgeProbes = false;
''')
rep('''    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
''', '''    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
    vec3 edgeP[2];                            // a foot down near a step edge: where its toe tip / heel ground is wanted
    u8 edgeKind[2] = {0, 0};                  // the point asked for (0 none, 1 toe tip, 2 heel), alternating while down
    float groundToe[2] = {0.f, 0.f};          // the ground found there, above the slope through the foot's own (kept with
    float groundHeel[2] = {0.f, 0.f};         // the world; -1 not probed)
    float edgeLift[2] = {0.f, 0.f};           // the foot raised onto the higher side of an edge under it (m)
''')
rep('''    vec3 footProbe(int side) const { return probeP[side & 1]; }
''', '''    vec3 footProbe(int side) const { return probeP[side & 1]; }
    // A foot down near a step edge (a curb, a stair) also needs the ground at this model-space point before the next
    // update (AnimInput::groundEdgeL/R): under its toe tip and its heel in turn.
    bool wantsEdgeProbe(int side) const { return edgeKind[side & 1] != 0; }
    vec3 edgeProbe(int side) const { return edgeP[side & 1]; }
''')
open(ch, 'w').write(s)

s = open(an).read()
# 1. the probe results, kept with the world
rep('''            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = A.planted[s] && A.stepT[s] < 0.f;''',
    '''            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = A.planted[s] && A.stepT[s] < 0.f;
            // the edge probe asked for last update: the ground under the toe tip / heel, relative to the foot's own
            if (A.groundToe[s] > -0.5f) A.groundToe[s] -= dzRoot;
            if (A.groundHeel[s] > -0.5f) A.groundHeel[s] -= dzRoot;
            if (terrain && in.edgeProbes && A.edgeKind[s]) {
                const float g = Clamp(s ? in.groundEdgeR : in.groundEdgeL, -0.35f, 0.35f);
                if (A.edgeKind[s] == 1) A.groundToe[s] = g;
                else A.groundHeel[s] = g;
            }''')
# 2. the ground under a foot: raised onto the higher side of an edge
rep('''    float offs[2] = {A.footL, A.footR};''',
    '''    // a foot down across a step edge (its toe tip or heel over ground a curb's or a stair's height above the slope
    // through the foot): it rests on the higher side
    for (int s = 0; s < 2; s++) {
        float lift = 0.f;
        const bool down = (A.planted[s] || A.footHold[s]) && A.stepT[s] < 0.f;
        if (down && A.edgeKind[s]) {
            const float under = s ? A.footR : A.footL;
            for (int k = 0; k < 2; k++) {
                const float g = k ? A.groundHeel[s] : A.groundToe[s];
                if (g < -0.5f) continue;
                const vec3 e = A.edgeP[s] - pivotP(s, A.plantP[s], A.plantYaw[s]);
                const float rise = g - (near + e.x * slopeX + e.y * slopeY);
                if (rise > 0.04f && rise < 0.25f) lift = Max(lift, rise);
            }
        }
        A.edgeLift[s] += (lift - A.edgeLift[s]) * (lift > A.edgeLift[s] ? 1.f : 1.f - expf(-dt * 10.f));
    }
    float offs[2] = {A.footL + A.edgeLift[0], A.footR + A.edgeLift[1]};''')
# 3. the edge probes for the next update: a foot down where the ground it came down on and its landing ground disagree
rep('''        // (the game probes after its next move: carried back by one update's root motion, a planted foot's probe stays
        // under it)
        m = rotate(qBack, m) - d;
        A.probeP[s] = vec3(m.x, m.y, 0.f);
    }''', '''        // (the game probes after its next move: carried back by one update's root motion, a planted foot's probe stays
        // under it)
        m = rotate(qBack, m) - d;
        A.probeP[s] = vec3(m.x, m.y, 0.f);
        // a foot down near a step edge: its toe tip and heel in turn (asked for while the landing ground and the ground
        // under it disagreed when it came down, or an edge has been found under it)
        const bool down = (A.planted[s] || A.footHold[s]) && A.stepT[s] < 0.f && terrain && in.footProbes;
        if (!down) {
            A.edgeKind[s] = 0;
            A.groundToe[s] = A.groundHeel[s] = -1.f;
        } else {
            const float under = s ? A.footR : A.footL;
            const bool suspect = fabsf(A.groundAhead[s] - near) > 0.05f || A.edgeLift[s] > 1e-3f ||
                                 (A.groundToe[s] > -0.5f && fabsf(A.groundToe[s] - near) > 0.04f) ||
                                 (A.groundHeel[s] > -0.5f && fabsf(A.groundHeel[s] - near) > 0.04f);
            if (!suspect && A.edgeKind[s] == 0) {
                A.groundToe[s] = A.groundHeel[s] = -1.f;
            } else {
                A.edgeKind[s] = A.edgeKind[s] == 1 ? 2 : 1;
                const float y = A.plantYaw[s];
                vec3 e = A.edgeKind[s] == 1 ? A.plantP[s] + rotate(qz(y), vec3(0.f, L + sk.boneLength[s ? B_TOE_R : B_TOE_L] - 0.01f * scale, 0.f))
                                            : A.plantP[s] + rotate(qz(y), vec3(0.f, 0.01f * scale, 0.f));
                e = rotate(qBack, e) - d;
                A.edgeP[s] = vec3(e.x, e.y, 0.f);
                if (!suspect && A.groundToe[s] > -0.5f && A.groundHeel[s] > -0.5f) A.edgeKind[s] = 0;   // (both seen level)
            }
        }
    }''')
# 4. the landing ground is kept until the foot has been down for an update (the suspicion reads it)
rep('''                if (swingU[s] <= 0.f) A.groundAhead[s] = *fz[s];''',
    '''                if (swingU[s] <= 0.f && A.plantedPrev[s]) A.groundAhead[s] = *fz[s];''')
# 5. resets
rep('''        groundRaw[k] = groundAhead[k] = pivotW[k] = 0.f;''',
    '''        groundRaw[k] = groundAhead[k] = pivotW[k] = edgeLift[k] = 0.f;
        groundToe[k] = groundHeel[k] = -1.f;
        edgeKind[k] = 0;''')
rep('''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;''',
    '''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;
                edgeKind[s] = 0;
                edgeLift[s] = 0.f;''')
rep('''            A.footHold[s] = false;
            A.pivotW[s] = 0.f;
        }
    }''', '''            A.footHold[s] = false;
            A.pivotW[s] = 0.f;
            A.edgeKind[s] = 0;
            A.edgeLift[s] = 0.f;
        }
    }''')
open(an, 'w').write(s)
print('ok')
