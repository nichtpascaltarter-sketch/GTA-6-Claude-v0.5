import sys
# the fast-turn hover: a planted foot left beyond the leg's reach while the other foot is off the ground (a quick turn
# while walking, a shove) waited for the other to come down, its leg stretched and the foot hanging 4-16 cm in the air.
# Once the footprint is 0.4 m (x foot scale) from the pose's, it steps over at once all the same.
# usage: turn.py <animator.cpp>
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
rep("""                // too far from the pose for the leg (a quick turn, a shove): walking, it steps over at once if the other foot
                // is down (else it waits for it, the leg stretched, and hurries its step); standing, it is let go for its
                // step (standing feet wait longer; a held foot is about to be lifted by the gait anyway). A foot moved onto
                // a stair's tread as it landed may stray that much further""",
"""                // too far from the pose for the leg (a quick turn, a shove): walking, it steps over at once if the other foot
                // is down (else it waits for it, the leg stretched, and hurries its step - but no longer than the leg still
                // reaches the footprint: past 0.4 m it steps over all the same, as a foot hanging in the air reads worse than
                // both off the ground for a moment); standing, it is let go for its step (standing feet wait longer; a held
                // foot is about to be lifted by the gait anyway). A foot moved onto a stair's tread as it landed may stray
                // that much further""")
rep("""                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);
                    else if (A.stepT[o] >= 0.f) A.stepDur[o] = Max(0.12f, A.stepDur[o] - dt * 0.6f);""",
"""                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);
                    else {
                        if (A.stepT[o] >= 0.f) A.stepDur[o] = Max(0.12f, A.stepDur[o] - dt * 0.6f);
                        if (!A.footHold[s] && length2(A.plantCorr[s]) > 0.16f * scale * scale) quickStep(s);
                    }""")
open(fn, 'w').write(s)
