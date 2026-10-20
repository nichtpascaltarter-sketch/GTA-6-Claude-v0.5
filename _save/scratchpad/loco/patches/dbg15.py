#!/usr/bin/env python3
# dbg15.py <animator.cpp>: debug prints (harness only) on the candidate - Anim::gAnimDbg set by the harness for the traced
# run: SCAN (each swing scan's fit and profile), PLANT (each gait plant: fit, atFit, the shown heel vs the fitted heel along
# the scan), HOLD (graze holds), QSTEP (quick steps), STEPEND (a step's landing)
import sys
def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count: raise SystemExit('expected %d, found %d: %r' % (count, n, old[:100]))
    return s.replace(old, new)
p = sys.argv[1]
s = open(p).read()
s = sub(s, '#include "anim_internal.h"\n\nnamespace Anim {', '#include "anim_internal.h"\n#define ANIM_DBG_HOOK 1\n\nnamespace Anim {\nbool gAnimDbg = false;   // DEBUG (harness: the traced run)')
s = sub(s, """    const Skeleton& sk = *A.skel;
    const bool walking = A.moveW > 0.3f && duty > 0.05f;""", """    const Skeleton& sk = *A.skel;
    A.dbgFrame++;   // DEBUG
    const bool walking = A.moveW > 0.3f && duty > 0.05f;""")
s = sub(s, """        stepped = stepped && bestCost < 1e8f;
        A.stairShift[s] = stepped ? bestShift : 0.f;""", """        stepped = stepped && bestCost < 1e8f;
        if (gAnimDbg) {   // DEBUG
            fprintf(stderr, "SCAN f%d foot %d u %.2f st %d sh %.3f land %.3f ball %d rear %.3f gFoot %.3f uHeel %.3f |", A.dbgFrame, s, swingU[s], (int)stepped, bestShift, bestG, (int)bestBall, bestRear, gFoot, uHeel);
            for (int k = 0; k < kGroundScan; k++) fprintf(stderr, " %.2f", g[k]);
            fprintf(stderr, "\\n");
        }
        A.stairShift[s] = stepped ? bestShift : 0.f;""")
s = sub(s, """                    if (!A.footHold[s] && A.stairLand[s] > -8.f && atFit(s)) {
                        // (down on the tread its swing was fitted to""", """                    if (gAnimDbg) {   // DEBUG
                        const vec3 dir = A.stairDir[s];
                        const vec3 hf = A.scanLand[s] + dir * (A.stairShift[s] - 0.5f * (A.footHeel + A.footBall));
                        const vec3 h = shownHeel(s);
                        fprintf(stderr, "PLANT f%d foot %d held %d fit %d atFit %d overNose %d d %.3f across %.3f rear %.3f land %.3f fz %.3f ball %d swingU %.2f ph %.3f fy %.1f sdir %.1f\\n", A.dbgFrame, s, (int)A.footHold[s],
                                (int)(A.stairLand[s] > -8.f), A.stairLand[s] > -8.f ? (int)atFit(s) : -1, A.stairLand[s] > -8.f ? (int)overNose(s) : -1,
                                dot(vec2(h.x - hf.x, h.y - hf.y), vec2(dir.x, dir.y)), dot(vec2(h.x - hf.x, h.y - hf.y), vec2(-dir.y, dir.x)), A.stairRear[s], A.stairLand[s],
                                s ? A.footR : A.footL, (int)A.stairBall[s], swingU[s], ph, fyaw[s] * 57.3f, atan2f(-dir.x, dir.y) * 57.3f);
                    }
                    if (!A.footHold[s] && A.stairLand[s] > -8.f && atFit(s)) {
                        // (down on the tread its swing was fitted to""")
s = sub(s, """                const bool fit = A.stairLand[s] > -8.f && atFit(s);
                A.ballProbe[s] = fit && (A.stairBall[s] || overNose(s));""", """                const bool fit = A.stairLand[s] > -8.f && atFit(s);
                if (gAnimDbg) fprintf(stderr, "HOLD f%d foot %d fit %d swingU %.2f stairLand %.3f\\n", A.dbgFrame, s, (int)fit, swingU[s], A.stairLand[s]);   // DEBUG
                A.ballProbe[s] = fit && (A.stairBall[s] || overNose(s));""")
s = sub(s, """    auto quickStep = [&](int s) {
        vec3 e = A.plantP[s] - heel[s];
        float dist = length(vec2(e.x, e.y));""", """    auto quickStep = [&](int s) {
        vec3 e = A.plantP[s] - heel[s];
        float dist = length(vec2(e.x, e.y));
        if (gAnimDbg) fprintf(stderr, "QSTEP f%d foot %d dist %.3f corr %.3f cy %.2f\\n", A.dbgFrame, s, dist, length(A.plantCorr[s]), A.corrYaw[s]);   // DEBUG""")
s = sub(s, """                const bool fitted = A.stepFitLand[s] > -8.f;
                if (fitted) {""", """                const bool fitted = A.stepFitLand[s] > -8.f;
                if (gAnimDbg) fprintf(stderr, "STEPEND f%d foot %d fitted %d land %.3f shift %.3f ball %d\\n", A.dbgFrame, s, (int)fitted, A.stepFitLand[s], A.stepFitShift[s], (int)A.stepFitBall[s]);   // DEBUG
                if (fitted) {""")
open(p, 'w').write(s)
