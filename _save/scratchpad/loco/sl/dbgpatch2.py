import sys
# harness-only debug for the strike-lead work: plant kinds (as the old work copy), and a landing record each time a
# swinging foot's landing spot is fixed (gait plant of a free foot, or a held re-plant): seq, plan, shift, residual, phase
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        sys.exit('count %d != %d for: %s' % (n, cnt, old[:80]))
    s = s.replace(old, new)

# 1. gait plant
rep("""                    if (!A.footHold[s] && A.stairLand[s] > -8.f) {
                        // (down on the tread its swing was fitted to""",
"""                    A.dbgFzPre[s] = s ? A.footR : A.footL;
                    A.dbgPlanLand[s] = A.stairLand[s];
                    A.dbgPlantKind[s] = A.footHold[s] ? 3 : (A.stairLand[s] > -8.f ? 1 : 2);
                    if (!A.footHold[s]) {
                        A.dbgLandSeq[s]++;
                        A.dbgLandPlan[s] = A.stairLand[s] > -8.f;
                        A.dbgLandShift[s] = A.stairShift[s];
                        A.dbgLandResid[s] = A.stairLand[s] > -8.f ? A.stairShift[s] - dot(vec2(A.plantCorr[s].x, A.plantCorr[s].y), md) : 0.f;
                        A.dbgLandPh[s] = swingU[s] > 0.f ? -(1.f - swingU[s]) * (1.f - duty) : ph;
                        A.dbgLandKind[s] = 1;
                    }
                    if (!A.footHold[s] && A.stairLand[s] > -8.f) {
                        // (down on the tread its swing was fitted to""")
# 2. standing plant
rep("""                // standing: a foot that comes down stays down where it is shown
                A.ballProbe[s] = false;""",
"""                // standing: a foot that comes down stays down where it is shown
                A.dbgPlantKind[s] = 5;
                A.ballProbe[s] = false;""")
# 3. quick step end
rep("""            if (A.stepT[s] >= 1.f) {
                A.stepT[s] = -1.f;
                A.planted[s] = true;""",
"""            if (A.stepT[s] >= 1.f) {
                A.dbgFzPre[s] = s ? A.footR : A.footL;
                A.dbgPlanLand[s] = -9.f;
                A.dbgPlantKind[s] = 4;
                A.stepT[s] = -1.f;
                A.planted[s] = true;""")
# 4. held re-plant
rep("""                A.plantSlack[s] = A.stairLand[s] > -8.f ? fabsf(A.stairShift[s]) : 0.f;
                const vec3 h = shownHeel(s);""",
"""                A.plantSlack[s] = A.stairLand[s] > -8.f ? fabsf(A.stairShift[s]) : 0.f;
                A.dbgFzPre[s] = s ? A.footR : A.footL;
                A.dbgPlanLand[s] = A.stairLand[s];
                A.dbgPlantKind[s] = 3;
                A.dbgLandSeq[s]++;
                A.dbgLandPlan[s] = A.stairLand[s] > -8.f;
                A.dbgLandShift[s] = A.stairShift[s];
                A.dbgLandResid[s] = A.stairLand[s] > -8.f ? A.stairShift[s] - dot(vec2(A.plantCorr[s].x, A.plantCorr[s].y), md) : 0.f;
                {
                    float phh = A.phase - (s ? 0.5f : 0.f);
                    phh -= floorf(phh);
                    A.dbgLandPh[s] = swingU[s] > 0.f ? -(1.f - swingU[s]) * (1.f - duty) : phh;
                }
                A.dbgLandKind[s] = 3;
                const vec3 h = shownHeel(s);""")
# 5. drift / prediction
rep("""            const bool again = A.scanDone[s] && A.stairSeen < 1.5f && fabsf(dot(vec2(lm.x, lm.y) - vec2(A.scanLand[s].x, A.scanLand[s].y), mdv)) > 0.03f;""",
"""            const bool again = A.scanDone[s] && A.stairSeen < 1.5f && fabsf(dot(vec2(lm.x, lm.y) - vec2(A.scanLand[s].x, A.scanLand[s].y), mdv)) > 0.03f;
            A.dbgDrift[s] = dot(vec2(lm.x, lm.y) - vec2(A.scanLand[s].x, A.scanLand[s].y), mdv);
            A.dbgPred[s] = dot(vec2(lm.x, lm.y), mdv);""")
import os
if os.environ.get("NO6"): open(fn, "w").write(s); print("patched (no 6)", fn); sys.exit(0)
# 6. strike lead: LM_SLFIX=1 adds the phase since the strike (stance plants)
rep("""                        const float obs = dot(vec2(mp.x, mp.y), md) / cycle - (swingU[s] > 0.f ? (1.f - swingU[s]) * (1.f - duty) : 0.f);""",
"""                        static const int kSlFix = getenv("LM_SLFIX") ? atoi(getenv("LM_SLFIX")) : 0;   // DEBUG
                        const float obs = dot(vec2(mp.x, mp.y), md) / cycle - (swingU[s] > 0.f ? (1.f - swingU[s]) * (1.f - duty) : (kSlFix ? -ph : 0.f));""")
open(fn, 'w').write(s)
print('patched', fn)
