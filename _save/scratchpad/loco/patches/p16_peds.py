#!/usr/bin/env python3
# The stair gait's game side in a tree's src/game/peds.cpp (animatePed's foot IK): the foot probes' clamp from 0.3 to
# 0.45 m (a foot two risers above the root), and the ground scan the animator asks for. usage: p16_peds.py <tree>
import sys, os
p = os.path.join(sys.argv[1], 'src/game/peds.cpp')
s = open(p).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
rep('''    in.groundNormal = vec3(0, 0, 1);
    in.footProbes = false;
    if (p.visibleDist < 30.f && p.grounded && p.state == PS_ONFOOT) {''', '''    in.groundNormal = vec3(0, 0, 1);
    in.footProbes = in.groundScanValid = false;
    if (p.visibleDist < 30.f && p.grounded && p.state == PS_ONFOOT) {''')
rep('''        in.groundOffsetL = hl.z > -1e8f ? Clamp(hl.z - base.z, -0.3f, 0.3f) : 0.f;
        in.groundOffsetR = hr.z > -1e8f ? Clamp(hr.z - base.z, -0.3f, 0.3f) : 0.f;''', '''        // (up to two risers from the root's tread: a foot landing on stairs)
        in.groundOffsetL = hl.z > -1e8f ? Clamp(hl.z - base.z, -0.45f, 0.45f) : 0.f;
        in.groundOffsetR = hr.z > -1e8f ? Clamp(hr.z - base.z, -0.45f, 0.45f) : 0.f;''')
rep('''        in.groundNormal = vec3(dot(vec2(n.x, n.y), rightV), dot(vec2(n.x, n.y), fwd), n.z);
        in.footProbes = true;
    }''', '''        in.groundNormal = vec3(dot(vec2(n.x, n.y), rightV), dot(vec2(n.x, n.y), fwd), n.z);
        in.footProbes = true;
        // a swing about to land where the ground steps (stairs, a kerb): the ground along the way ahead it asked for
        // (Animator::wantsGroundScan, once a step at most), so it lands on one tread
        if (p.anim.wantsGroundScan()) {
            vec3 s0 = p.anim.groundScanFrom(), sd = p.anim.groundScanDir();
            for (int k = 0; k < Anim::kGroundScan; k++) {
                vec3 m = s0 + sd * (Anim::kGroundScanStep * (float)k), w = base + rotate(q, vec3(m.x, m.y, 0.f));
                Phys::GroundHit h = Phys::gCollision->ground(w.x, w.y, base.z + 0.5f, kStepUp);
                in.groundScan[k] = h.z > -1e8f ? Clamp(h.z - base.z, -1.f, 1.f) : 0.f;
            }
            in.groundScanValid = true;
        }
    }''')
# the hold's axis back to upright every update (the couple code sets the way ahead for held hands after this)
rep('''    // synced takedown: the attacker's choke arm finds the victim's actual neck (tall / short pairs still connect)
    in.grabWeight = 0.f;''', '''    // synced takedown: the attacker's choke arm finds the victim's actual neck (tall / short pairs still connect)
    in.grabWeight = 0.f;
    in.grabAxis = vec3(0.f, 0.f, 1.f);''')
open(p, 'w').write(s)
print('patched', p)
