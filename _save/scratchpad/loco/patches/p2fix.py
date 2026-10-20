import sys
# PR #47 Codex P2: when stairSeen crosses 1.5 s mid-step (u < 0.75), stepTo went back to the unfitted heel while the
# cached fit (stepFitLand / stepFitShift / stepFitBall) stayed, so the touchdown took the fitted branch for an unfitted
# landing. The cached fit is now dropped whenever the step's landing is not fitted (off the stairs by then, or a fit that
# fails), so it comes down on the ground probed where it lands.
# usage: p2fix.py <animator.cpp on 0818e30c>
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
rep("""    // a step's landing heel fitted so (the tread's height, the move and whether the heel overhangs a nose kept for its
    // landing; none if it cannot be fitted)
    auto fitStep = [&](int s, vec3 h) -> vec3 {
        A.stepFitLand[s] = -9.f;
        float sh = 0.f, land = -9.f;""",
"""    // a step's cached fit dropped (its landing not fitted to a tread): it comes down on the ground probed where it lands
    auto dropFit = [&](int s) {
        A.stepFitLand[s] = -9.f;
        A.stepFitShift[s] = 0.f;
        A.stepFitBall[s] = false;
    };
    // a step's landing heel fitted so (the tread's height, the move and whether the heel overhangs a nose kept for its
    // landing; none if it cannot be fitted)
    auto fitStep = [&](int s, vec3 h) -> vec3 {
        dropFit(s);
        float sh = 0.f, land = -9.f;""")
rep("""                if (A.stairSeen < 1.5f) A.stepTo[s] = fitStep(s, A.stepTo[s]);   // (on stairs: onto a tread)""",
"""                if (A.stairSeen < 1.5f) A.stepTo[s] = fitStep(s, A.stepTo[s]);   // (on stairs: onto a tread)
                else dropFit(s);   // (off them since: not onto a tread fitted earlier in the step)""")
open(fn, 'w').write(s)
