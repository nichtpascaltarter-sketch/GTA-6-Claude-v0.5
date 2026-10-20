#!/usr/bin/env python3
# runlead.py <animator.cpp in> <animator.cpp out> <character.h in> <character.h out>
# On the stair-pace fix (animator 63a9f73e, character.h 9dfb632f):
#  - a jog's or a run's landings are predicted with its own heel-strike lead, learned in metres (about 0.3 m at any run
#    pace; the walk's lead is 0.3 of a stride, which at a run put the landings 0.4-0.6 m too far and off their treads)
#  - on stairs and their landings, a foot whose stance is over but which the slow walk's low swing still keeps down, and
#    which the turning body has left at the leg's reach, goes to the gait's swing (its landing scanned and fitted to a
#    tread) instead of a quick step that would end inside that swing and be lifted again unfitted
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst, hsrc, hdst = sys.argv[1:5]
s = open(src).read()
h = open(hsrc).read()

# character.h: the run's lead, after the walk's
h = sub(h, """    float strikeLead = 0.3f;                  // where the pose's heel strikes land ahead of the body, in strides (learned)
""", """    float strikeLead = 0.3f;                  // where the pose's heel strikes land ahead of the body, in strides (learned)
    float strikeLeadRun = 0.3f;               // the same at a jog or a run, in m (learned there: about 0.3 m at any run pace)
""")

# init
s = sub(s, """    strikeLead = 0.3f;
""", """    strikeLead = 0.3f;
    strikeLeadRun = 0.3f;
""")

# the lead the landing predictions use: the walk's (in strides) blending into the run's (in metres) from 2 to 3 m/s
s = sub(s, """    auto turnOver = [&](float sT) { return Clamp(in.turnRate * sT / Max(spd, 0.3f), -1.2f, 1.2f); };
""", """    auto turnOver = [&](float sT) { return Clamp(in.turnRate * sT / Max(spd, 0.3f), -1.2f, 1.2f); };
    // where the pose's heel strikes land ahead of the body (m): the walk's lead is a share of its stride, a jog's or a
    // run's about 0.3 m whatever its pace (a run's stride is far longer, its foot lands nearly under the body)
    const float runK = sstep(2.f, 3.f, spd);
    const float leadM = Lerp(cycle * A.strikeLead, A.strikeLeadRun, runK);
""")
s = sub(s, """                        A.strikeLead += (Clamp(obs, 0.1f, 0.5f) - A.strikeLead) * 0.3f;
""", """                        A.strikeLead += (Clamp(obs, 0.1f, 0.5f) - A.strikeLead) * 0.3f * (1.f - runK);
                        A.strikeLeadRun += (Clamp(obs * cycle, 0.05f, 0.8f) - A.strikeLeadRun) * 0.3f * runK;
""")
s = sub(s, """            m = landAhead(m, sT, cycle * A.strikeLead + A.stairShift[s] + 0.65f * L);""",
        """            m = landAhead(m, sT, leadM + A.stairShift[s] + 0.65f * L);""")
s = sub(s, """            lm = landAhead(lm, sT, cycle * A.strikeLead);""",
        """            lm = landAhead(lm, sT, leadM);""")

# the reach limit: on stairs, a foot past its stance (kept down by the slow walk's low swing) goes to the gait's swing
s = sub(s, """                if (length2(A.plantCorr[s]) > limR * limR || fabsf(A.corrYaw[s]) > limY) {
                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);""",
        """                if (length2(A.plantCorr[s]) > limR * limR || fabsf(A.corrYaw[s]) > limY) {
                    if (!walking && !A.footHold[s]) A.planted[s] = false;
                    else if (walking && A.planted[s] && swingU[s] > 0.f && swingU[s] < 0.5f && A.stairSeen < 4.f) {
                        // (on stairs or their landing, its stance over and only the slow walk's low swing keeping it down:
                        // let go as at a toe-off - the gait's swing takes it, its landing fitted to a tread. A quick step
                        // would end inside that swing, be lifted again at once and come down unfitted. Not late in the
                        // swing: a foot down early there would be planted again at once - it steps)
                        A.planted[s] = false;
                        A.footHold[s] = true;
                    } else if (A.stepT[o] < 0.f && (A.planted[o] || A.footHold[o])) quickStep(s);""")

open(dst, 'w').write(s)
open(hdst, 'w').write(h)
