import sys
# quick steps on stairs no longer drag a toe or heel through a tread: (1) they keep over the higher of the ground they
# left and the ground where they land until 85% of the step (was 60%), and come down onto the ground probed where they
# land at the end; (2) an update with no foot held down (one foot quick-stepping while the other swings, or a run's
# flight) takes the root's own vertical move (AnimInput::rootMove.z) for the heights kept with the world, instead of
# extrapolating its vertical speed (on stairs that sank the stepping foot 5 cm an update into the tread it was over).
# usage: qsdrag2.py <animator.cpp>
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
rep("""            } else {
                dzRoot = Clamp(A.rootVz * dt, -0.05f, 0.05f);   // both feet off the ground (a run's flight): as it was going
            }""",
"""            } else if (in.rootMoveValid) {
                // (no foot held down - one stepping while the other swings, a run's flight: the root's own vertical move)
                dzRoot = Clamp(in.rootMove.z, -0.25f, 0.25f);
            } else {
                dzRoot = Clamp(A.rootVz * dt, -0.05f, 0.05f);   // (not given: as it was going)
            }""")
rep("""                // (a quick step on stairs - its ground probed where it lands - goes over the higher of that and the ground it
                // left until late in the step, then down onto it: not through a riser)
                const bool qs = A.stepT[s] >= 0.f && A.stairSeen < 1.5f;
                const float gi = qs && A.stepT[s] < 0.6f ? Max(gIn[s], *fz[s] - dzRoot) : gIn[s];""",
"""                // (a quick step on stairs - its ground probed where it lands - goes over the higher of that and the ground it
                // left until late in the step, its trailing toe or heel past the edge, then down onto it: not through a nose)
                const bool qs = A.stepT[s] >= 0.f && A.stairSeen < 1.5f;
                const float gi = qs && A.stepT[s] < 0.85f ? Max(gIn[s], *fz[s] - dzRoot) : gIn[s];""")
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
