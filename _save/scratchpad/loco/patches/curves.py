#!/usr/bin/env python3
# curves.py <animator.cpp in> <animator.cpp out> <character.h in> <character.h out>   (on c14: animator c427379e,
# character.h bd6d781f)
# Stairs that turn under the feet (winders, spirals):
#  - going up, a tread shorter than the foot along its way (a winder's narrow end) is fitted with the ball on it at least
#    (the heel over the nose behind, the toes nearer the riser ahead), not with the toes short of the riser and the ball
#    off the tread, where the ground under the ball is the tread below
#  - going down round a tight bend, the inner foot lands further out than its gait would put it (by 3 cm for each 1/m of
#    the bend's curvature, at most 7 cm), where the wedge treads are deeper
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst, hsrc, hdst = sys.argv[1:5]
s = open(src).read()
h = open(hsrc).read()

h = sub(h, """    float stairRear[2] = {9.f, 9.f};          // how far that fit's heel is ahead of the nose behind its tread, one lower (m, along stairDir; 9: none)
""", """    float stairRear[2] = {9.f, 9.f};          // how far that fit's heel is ahead of the nose behind its tread, one lower (m, along stairDir; 9: none)
    vec3 stairLat[2] = {vec3(0), vec3(0)};    // a swing's landing moved out from a tight bend's centre going down (kept with the world)
""")

# kept with the world, cleared with the fit
s = sub(s, """        A.stairDir[s] = rotate(qBack, A.stairDir[s]);
""", """        A.stairDir[s] = rotate(qBack, A.stairDir[s]);
        A.stairLat[s] = rotate(qBack, A.stairLat[s]);
""")
s = sub(s, """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairShift[s] = 0.f;""", """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairLat[s] = vec3(0);
            A.stairShift[s] = 0.f;""")
# the swing's correction heads for the landing moved out as well; the landing probe goes there
s = sub(s, """                const vec3 to = A.stairDir[s] * A.stairShift[s];""", """                const vec3 to = A.stairDir[s] * A.stairShift[s] + A.stairLat[s];""")
s = sub(s, """            m = landAhead(m, sT, leadM + A.stairShift[s] + 0.65f * L);""", """            m = landAhead(m, sT, leadM + A.stairShift[s] + 0.65f * L) + A.stairLat[s];""")
# the scan: the inner foot's landing moved out on a tight bend going down
s = sub(s, """            lm = landAhead(lm, sT, leadM);
""", """            lm = landAhead(lm, sT, leadM);
            // (down round a tight bend on stairs - a spiral, winders - the inner foot lands further out than its gait would
            // put it, where the wedge treads are deeper: 3 cm for each 1/m of the bend's curvature, at most 7 cm)
            vec3 latNew(0.f);
            if (A.stairSeen < 1.5f && A.rootVz < -0.1f && (in.turnRate > 0.f ? s == 0 : s == 1)) {
                const float curv = fabsf(in.turnRate) / Max(spd, 0.3f);
                const vec3 ltw = rotate(qz(turnOver(sT)), vec3(-md.y, md.x, 0.f));
                latNew = ltw * ((in.turnRate > 0.f ? -1.f : 1.f) * Min(0.03f * curv, 0.07f) * scale);
            }
            lm = lm + latNew;
""")
s = sub(s, """                A.stairDir[s] = sdw;
""", """                A.stairDir[s] = sdw;
                A.stairLat[s] = latNew;
""")
# the narrow tread going up: the ball on it at least
s = sub(s, """            if (upBehind) lo = r + mg + 0.04f * scale;
            else if (rearEdge) lo = r + mg - 0.7f * L;   // (on the forefoot: the ground under it is probed under the ball)
            // (a tread shorter than the foot""", """            if (upBehind) lo = r + mg + 0.04f * scale;
            else if (rearEdge) lo = r + mg - 0.7f * L;   // (on the forefoot: the ground under it is probed under the ball)
            // (going up, a tread too short for the foot whose toes-short landing would leave the ball off it - a winder's
            // narrow end: the ball on it at least, the toes nearer the riser ahead)
            if (lo > hi && upAhead && rearEdge && !upBehind && hi + L < r + mg) lo = hi = r + mg - L;
            // (a tread shorter than the foot""")

open(dst, 'w').write(s)
open(hdst, 'w').write(h)
