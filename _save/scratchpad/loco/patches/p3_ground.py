import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''        } else if (A.planted[s]) {
            // twisted further than the leg turns comfortably (a quick turn while walking): the foot pivots on its ball,
            // which stays where it is, the heel coming up a little
            const float tw''', '''        } else if (A.planted[s] || A.footHold[s]) {
            // on the ground (planted, or let go at toe-off while the pose still has it down): it stays where it is. Twisted
            // further than the leg turns comfortably (a quick turn while walking), it pivots on its ball, which stays put,
            // the heel coming up a little
            const float tw''')
rep('''            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            // too far from the pose (a fast turn, a shove): walking, a quick step over (the other foot down); standing, let
            // go for its step (standing feet wait longer)
            float lim = walking ? 0.3f : (A.staggerT >= 0.f ? 0.55f : 0.38f), limY = walking ? 0.75f : 1.15f;
            if (length2(A.plantCorr[s]) > lim * lim * scale * scale || fabsf(A.corrYaw[s]) > limY) {
                if (walking && A.stepT[1 - s] < 0.f) quickStep(s);
                else A.planted[s] = false;
            }
        } else if (A.footHold[s]) {
            // let go at toe-off, the pose still has it on the ground: it stays where it is until the pose lifts it clear
            // (or the leg would stretch too far: then it steps over)
            vec3 pp = pivotP(s, A.plantP[s], A.plantYaw[s]);
            A.plantCorr[s] = vec3(pp.x - pa.x, pp.y - pa.y, 0.f);
            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);
            if (Min(heel[s].z, ball[s].z) > 0.012f * scale) A.footHold[s] = false;
            else if (length2(A.plantCorr[s]) > 0.3f * 0.3f * scale * scale || fabsf(A.corrYaw[s]) > 0.75f) {
                if (A.stepT[1 - s] < 0.f) quickStep(s);
                else A.footHold[s] = false;
            }
        } else if''', '''            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            if (A.footHold[s] && Min(heel[s].z, ball[s].z) > 0.012f * scale) {
                A.footHold[s] = false;   // a held foot goes once the pose has lifted it clear
            } else {
                // too far from the pose for the leg (a quick turn, a shove): walking, it steps over at once if the other foot
                // is down (else it waits for it, the leg stretched); standing, it is let go for its step (standing feet wait
                // longer)
                const float lim = walking ? 0.3f : (A.staggerT >= 0.f ? 0.55f : 0.38f), limY = walking ? 0.75f : 1.15f;
                const int o = 1 - s;
                if (length2(A.plantCorr[s]) > lim * lim * scale * scale || fabsf(A.corrYaw[s]) > limY) {
                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);
                }
            }
        } else if''')
rep('''        p.rot[ends[s]] = normalize(conj(qcalf[s]) * want);   // the IK keeps the foot's model rotation
        legIK(sk, p, ups[s], lows[s], ends[s], qp, pp, target, pole);
    }''', '''        p.rot[ends[s]] = normalize(conj(qcalf[s]) * want);   // the IK keeps the foot's model rotation
        legIK(sk, p, ups[s], lows[s], ends[s], qp, pp, target, pole);
        // (pivoting: the toes stay flat on the ground)
        if (A.pivotW[s] * w > 1e-3f) {
            const int tb = s ? B_TOE_R : B_TOE_L;
            p.rot[tb] = normalize(qx(0.2f * A.pivotW[s] * w) * p.rot[tb]);
        }
    }''')
open(p, 'w').write(s)
print("ok")
