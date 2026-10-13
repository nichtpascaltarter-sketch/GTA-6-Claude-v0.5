import sys
# quick steps on stairs no longer drag a toe or heel through a nose: they keep over the higher of the ground they left
# and the ground where they land until 85% of the step (was 60%), and come down onto the ground probed where they land at
# the end. And the root's vertical move is also read under a foot held where it is after toe-off, so an update with one
# foot quick-stepping and the other held down no longer extrapolates it. usage: qsdrag.py <animator.cpp>
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
rep("""    // ground under the feet and the slope plane. The heights are kept with the world: the root's vertical motion this
    // update (read off the ground under a foot that stayed planted, which does not move) comes out of them first, so a
    // planted foot keeps its height exactly (no lag on a slope, a curb or a stair). A swinging foot's probe alternates
    // between just ahead of it and where it will land; it swings over the higher of the two (up onto a curb or a stair
    // early enough to clear its edge, down off one only once past the edge)""",
"""    // ground under the feet and the slope plane. The heights are kept with the world: the root's vertical motion this
    // update (read off the ground under a foot that stayed down - planted, or held where it is after its toe-off - which
    // does not move) comes out of them first, so a planted foot keeps its height exactly (no lag on a slope, a curb or a
    // stair). A swinging foot's probe alternates between just ahead of it and where it will land; it swings over the
    // higher of the two (up onto a curb or a stair early enough to clear its edge, down off one only once past the edge)""")
rep("""            for (int s = 0; s < 2; s++)
                if (A.planted[s] && A.plantedPrev[s] && A.stepT[s] < 0.f) {""",
"""            for (int s = 0; s < 2; s++)
                if ((A.planted[s] || A.footHold[s]) && A.plantedPrev[s] && A.stepT[s] < 0.f) {""")
rep("""                // (a quick step on stairs - its ground probed where it lands - goes over the higher of that and the ground it
                // left until late in the step, then down onto it: not through a riser)
                const bool qs = A.stepT[s] >= 0.f && A.stairSeen < 1.5f;
                const float gi = qs && A.stepT[s] < 0.6f ? Max(gIn[s], *fz[s] - dzRoot) : gIn[s];""",
"""                // (a quick step on stairs - its ground probed where it lands - goes over the higher of that and the ground it
                // left until late in the step, its trailing toe or heel past the edge, then down onto it: not through a nose)
                const bool qs = A.stepT[s] >= 0.f && A.stairSeen < 1.5f;
                const float gi = qs && A.stepT[s] < 0.85f ? Max(gIn[s], *fz[s] - dzRoot) : gIn[s];""")
rep("""            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = A.planted[s] && A.stepT[s] < 0.f;""",
"""            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = (A.planted[s] || A.footHold[s]) && A.stepT[s] < 0.f;""")
rep("""            if (A.stepT[s] >= 1.f) {
                A.stepT[s] = -1.f;
                A.planted[s] = true;
                A.footHold[s] = false;
                A.plantSlack[s] = 0.f;
                A.plantP[s] = A.stepTo[s];""",
"""            if (A.stepT[s] >= 1.f) {
                // (on stairs: down on the ground probed where it lands, whatever of the way down is left)
                if (A.stairSeen < 1.5f && terrain && in.footProbes) (s ? A.footR : A.footL) = A.groundRaw[s];
                A.stepT[s] = -1.f;
                A.planted[s] = true;
                A.footHold[s] = false;
                A.plantSlack[s] = 0.f;
                A.plantP[s] = A.stepTo[s];""")
open(fn, 'w').write(s)
print('patched', fn)
