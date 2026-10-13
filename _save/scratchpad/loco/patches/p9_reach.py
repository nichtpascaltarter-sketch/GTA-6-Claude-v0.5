import sys
p = sys.argv[1]
s = open(p).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
rep('''                // (a foot still on the ground just after the gait's toe-off stays planted until it actually lifts:
                // blended gaits, e.g. a diagonal walk, lift a little later than their mixed duty)
                bool contact = (ph > 0.004f && ph < duty - 0.01f && nearGround) || (A.planted[s] && ph <= 0.004f) ||
                               (swingU[s] > 0.7f && touching) || (A.planted[s] && touching && swingU[s] < 0.25f);''',
'''                // (a foot still on the ground just after the gait's toe-off stays planted until it actually lifts:
                // blended gaits, e.g. a diagonal walk, lift a little later than their mixed duty; one put down early, late
                // in its swing, stays down)
                bool contact = (ph > 0.004f && ph < duty - 0.01f && nearGround) || (A.planted[s] && ph <= 0.004f) ||
                               (swingU[s] > 0.7f && touching) || (A.planted[s] && touching && swingU[s] < 0.25f) ||
                               (A.planted[s] && swingU[s] > 0.6f);''')
rep('''        p.rootOffset.z -= A.legSink;
        pp = pp - rotate(qr, vec3(0.f, 0.f, A.legSink));
    }
''', '''        p.rootOffset.z -= A.legSink;
        pp = pp - rotate(qr, vec3(0.f, 0.f, A.legSink));
    }
    // a planted foot out of reach even so (a sharp turn carries the hips away from it faster than a gait can follow) is
    // not left hanging in the air: the other foot, about to land, comes down now and this one steps over; with the other
    // foot not down yet, the footprint is drawn along the ground to the edge of the reach
    if (walking && w > 0.5f)
        for (int s = 0; s < 2; s++) {
            if ((!A.planted[s] && !A.footHold[s]) || A.stepT[s] >= 0.f) continue;
            const vec3 dd = targets[s] - (pp + rotate(qp, sk.bindLocalPos[ups[s]]));
            const float reach = (length(sk.bindLocalPos[lows[s]]) + length(sk.bindLocalPos[ends[s]])) * 0.994f;
            if (length2(dd) <= reach * reach) continue;
            const int o = 1 - s;
            if (A.stepT[o] < 0.f && !A.planted[o] && !A.footHold[o] && swingU[o] > 0.6f &&
                Min(Min(heel[o].z, ball[o].z), toeZ[o]) + lift[o] * w < 0.035f * scale) {
                A.planted[o] = true;
                const vec3 h = shownHeel(o);
                A.plantP[o] = vec3(h.x, h.y, 0.f);
                A.plantYaw[o] = wrapAngle(fyaw[o] + A.corrYaw[o]);
                A.footEvents |= 1u << o;
            }
            if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) {
                quickStep(s);
                continue;
            }
            const float r = length(vec2(dd.x, dd.y)), rMax = sqrtf(Max(reach * reach - dd.z * dd.z, 0.f));
            if (r > rMax + 1e-4f) {
                const vec2 sh = vec2(dd.x, dd.y) * ((r - rMax) / r);
                targets[s].x -= sh.x;
                targets[s].y -= sh.y;
                A.plantP[s] = A.plantP[s] - vec3(sh.x, sh.y, 0.f);
            }
        }
''')
open(p, 'w').write(s)
print("ok")
