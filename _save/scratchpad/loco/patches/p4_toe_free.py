import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''    vec3 fp[2], heel[2], ball[2];
    quat fq[2];
    float fyaw[2], wBall[2];''', '''    vec3 fp[2], heel[2], ball[2];
    quat fq[2];
    float fyaw[2], wBall[2], toeZ[2];''')
rep('''        ball[s] = fp[s] + rotate(fq[s], vec3(0.f, A.footBall, -A.footAnkleH));
        wBall[s] = Saturate((-pitch - 0.02f) / 0.07f);
    }''', '''        ball[s] = fp[s] + rotate(fq[s], vec3(0.f, A.footBall, -A.footAnkleH));
        wBall[s] = Saturate((-pitch - 0.02f) / 0.07f);
        // toe tip on the sole under the (bent) toe bone
        const int tb = s ? B_TOE_R : B_TOE_L;
        const vec3 toeJ = fp[s] + rotate(fq[s], sk.bindLocalPos[tb]);
        toeZ[s] = (toeJ + rotate(fq[s] * p.rot[tb], vec3(0.f, sk.boneLength[tb], -(A.footAnkleH + sk.bindLocalPos[tb].z)))).z;
    }''')
rep('''        // toe tip on the sole under the (bent) toe bone
        const int tb = s ? B_TOE_R : B_TOE_L;
        vec3 toeJ = fp[s] + rotate(fq[s], sk.bindLocalPos[tb]);
        vec3 toe = toeJ + rotate(fq[s] * p.rot[tb], vec3(0.f, sk.boneLength[tb], -(A.footAnkleH + sk.bindLocalPos[tb].z)));
        float low = Min(Min(heel[s].z, ball[s].z), toe.z);''', '''        float low = Min(Min(heel[s].z, ball[s].z), toeZ[s]);''')
rep('''                bool touching = Min(heel[s].z, ball[s].z) < 0.004f * scale;''', '''                bool touching = Min(Min(heel[s].z, ball[s].z), toeZ[s]) < 0.004f * scale;''')
rep('''        } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
            // released: the correction fades once the foot is off the ground (a toe still on it would slide)
            float k = expf(-dt / 0.07f);
            A.plantCorr[s] = A.plantCorr[s] * k;
            A.corrYaw[s] *= k;
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);
        }''', '''        } else {
            // let go: the correction fades once the foot is off the ground (a toe still on it would slide). One that comes
            // down onto the ground again before the gait plants it (a late swing grazing it, the walk blending out to a
            // stand) is held where it is shown
            float ph = A.phase - (s ? 0.5f : 0.f);
            ph -= floorf(ph);
            const bool late = ph > duty + 0.5f * (1.f - duty) || A.moveW < 0.95f;
            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) < 0.006f * scale) {
                const vec3 h = shownHeel(s);
                A.plantP[s] = vec3(h.x, h.y, 0.f);
                A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                A.footHold[s] = true;
            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                float k = expf(-dt / 0.07f);
                A.plantCorr[s] = A.plantCorr[s] * k;
                A.corrYaw[s] *= k;
            }
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);
        }''')
rep('''                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);''', '''                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);
                    else if (A.stepT[o] >= 0.f) A.stepDur[o] = Max(0.12f, A.stepDur[o] - dt * 0.6f);   // (the other is
                                                                                                       // stepping: hurried)''')
rep('''        A.stepDur[s] = Clamp(0.17f + 0.3f * dist / scale, 0.17f, 0.3f);
        A.stepLift[s] = Clamp(0.025f + 0.1f * dist / scale, 0.025f, 0.06f) * scale;''', '''        A.stepDur[s] = Clamp(0.15f + 0.22f * dist / scale, 0.15f, 0.26f);
        A.stepLift[s] = Clamp(0.025f + 0.1f * dist / scale, 0.025f, 0.06f) * scale;''')
open(p, 'w').write(s)
print("ok")
