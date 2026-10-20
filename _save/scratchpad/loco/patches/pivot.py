import sys
# A foot on the ground twisted past what the leg turns pivots where it touches. Its heel comes up about the ball as
# before, but the pose is rolled so before the foot's contact, its pin onto the ground and the IK are worked out (the
# roll ran after the pin, inside the IK: a foot whose pin had put its heel on the ground - the toes still up, or the pose
# pitched otherwise - came off the ground altogether, 2-8 cm, its leg hanging). The roll counts the pose's own pitch (up
# to 0.2 rad heel-up in all; none on a heel strike, the toes up: such a foot turns on its heel), and a let-go foot's roll
# only eases out (no toes brought down for its landing). The footprint turns about the contact - the heel, the ball, or
# the toe tip where that is lowest - not always the ball, which swept a toe or heel on the ground round it.
# usage: pivot.py <animator.cpp>
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
rep("""    float fyaw[2], wBall[2], toeZ[2];
    // pelvis model transform (root -> pelvis), the legs from it""",
"""    float fyaw[2], wBall[2], toeZ[2];
    float roll[2] = {0.f, 0.f}, contactD[2] = {0.f, 0.f};   // (twisted on the ground: the heel's roll up about the ball; the contact's place along the foot from the heel)
    bool onHeel[2] = {false, false};                        // (a heel strike, the toes up: it turns on its heel)
    // pelvis model transform (root -> pelvis), the legs from it""")
rep("""        fq[s] = qcalf[s] * p.rot[ends[s]];
        vec3 F = rotate(fq[s], vec3(0, 1, 0));""",
"""        fq[s] = qcalf[s] * p.rot[ends[s]];
        // (a foot on the ground twisted past what the leg turns pivots where it touches: on its heel at a heel strike, the
        // toes up; else on the ball, the heel up 0.2 rad - the pose's own heel lift counted, and one still flat or a little
        // toes-up rolled down onto its ball first. The pose is rolled so before its contact, its pin onto the ground and the
        // IK are worked out, so they all hold the contact down. Let go, the roll eases out: no toes brought down to land)
        {
            const float pw = A.pivotW[s] * A.plantOn;
            const vec3 F0 = rotate(fq[s], vec3(0, 1, 0));
            const float pitch0 = asinf(Clamp(F0.z, -1.f, 1.f));
            const vec3 h0 = fp[s] + rotate(fq[s], vec3(0.f, -A.footHeel, -A.footAnkleH));
            const vec3 b0 = fp[s] + rotate(fq[s], vec3(0.f, A.footBall, -A.footAnkleH));
            onHeel[s] = b0.z - h0.z > 0.015f * scale;
            if (pw > 1e-3f && !onHeel[s]) {
                const float pt = A.planted[s] || A.footHold[s] ? pitch0 : Min(pitch0, 0.f);
                roll[s] = pt > 0.f ? pw * (0.2f + pt) : Max(0.f, 0.2f * pw + pt);
                if (roll[s] > 1e-4f) {
                    const quat rp = quatAxisAngle(rotate(fq[s], vec3(1.f, 0.f, 0.f)), -roll[s]);
                    fp[s] = b0 + rotate(rp, fp[s] - b0);
                    fq[s] = normalize(rp * fq[s]);
                }
            }
        }
        vec3 F = rotate(fq[s], vec3(0, 1, 0));""")
rep("""        toeZ[s] = (toeJ + rotate(fq[s] * p.rot[tb], vec3(0.f, sk.boneLength[tb], -(A.footAnkleH + sk.bindLocalPos[tb].z)))).z;
    }""",
"""        // (rolled: the toes stay flat - the toe bone turns back by the roll)
        const vec3 toeTip = toeJ + rotate(fq[s] * qx(roll[s]) * p.rot[tb], vec3(0.f, sk.boneLength[tb], -(A.footAnkleH + sk.bindLocalPos[tb].z)));
        toeZ[s] = toeTip.z;
        contactD[s] = onHeel[s] ? 0.f : (toeTip.z < ball[s].z - 0.003f * scale ? L + sk.boneLength[tb] : L);
    }""")
rep("""            // on the ground (planted, or let go at toe-off while the pose still has it down): it stays where it is. Twisted
            // further than the leg turns comfortably (a quick turn while walking), it pivots on its ball, which stays put,
            // the heel coming up a little
            const float tw = wrapAngle(A.plantYaw[s] - fyaw[s]), allow = walking ? 0.45f : 1.f;
            bool piv = false;
            if (fabsf(tw) > allow) {
                const float ex = tw - Clamp(tw, -allow, allow);
                const vec3 bw = A.plantP[s] + rotate(qz(A.plantYaw[s]), vec3(0.f, L, 0.f));
                A.plantYaw[s] = wrapAngle(A.plantYaw[s] - ex);
                A.plantP[s] = bw - rotate(qz(A.plantYaw[s]), vec3(0.f, L, 0.f));
                piv = true;
            }""",
"""            // on the ground (planted, or let go at toe-off while the pose still has it down): it stays where it is. Twisted
            // further than the leg turns comfortably (a quick turn while walking), it pivots where it touches, which stays
            // put: on its heel at a heel strike, else on the ball (or the toe tip, rolled onto it), the heel coming up
            const float tw = wrapAngle(A.plantYaw[s] - fyaw[s]), allow = walking ? 0.45f : 1.f;
            bool piv = false;
            if (fabsf(tw) > allow) {
                const float ex = tw - Clamp(tw, -allow, allow);
                const vec3 cw = A.plantP[s] + rotate(qz(A.plantYaw[s]), vec3(0.f, contactD[s], 0.f));
                A.plantYaw[s] = wrapAngle(A.plantYaw[s] - ex);
                A.plantP[s] = cw - rotate(qz(A.plantYaw[s]), vec3(0.f, contactD[s], 0.f));
                piv = !onHeel[s];
            }""")
rep("""        // pivoting on the ball: the heel comes up about it
        if (A.pivotW[s] * w > 1e-3f) {
            const quat rp = quatAxisAngle(rotate(want, vec3(1.f, 0.f, 0.f)), -0.2f * A.pivotW[s] * w);
            const vec3 bp = target + rotate(want, vec3(0.f, A.footBall, -A.footAnkleH));
            target = bp + rotate(rp, target - bp);
            want = normalize(rp * want);
        }
""", "")
rep("""        // (pivoting: the toes stay flat on the ground)
        if (A.pivotW[s] * w > 1e-3f) {
            const int tb = s ? B_TOE_R : B_TOE_L;
            p.rot[tb] = normalize(qx(0.2f * A.pivotW[s] * w) * p.rot[tb]);
        }""",
"""        // (pivoting, the heel rolled up: the toes stay flat on the ground)
        if (roll[s] > 1e-4f) {
            const int tb = s ? B_TOE_R : B_TOE_L;
            p.rot[tb] = normalize(qx(roll[s]) * p.rot[tb]);
        }""")
open(fn, 'w').write(s)
print('ok')
