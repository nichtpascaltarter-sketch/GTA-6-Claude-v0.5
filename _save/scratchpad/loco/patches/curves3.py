#!/usr/bin/env python3
# curves3.py <animator.cpp in> <animator.cpp out>   (after curves.py + curves2.py; character.h unchanged)
#  - the narrow-tread fit going up (the ball kept on a too-short tread) only for a tread not below where the foot was to
#    land: going down a tight switchback it fitted a too-short run two treads down, the foot came down short of it and
#    was planted at its height, 18 cm into the tread above
#  - the inner foot's lateral shift going down round a bend only on a bend of 0.5 /m or more (2 m radius and under): a
#    crowd weaving down a straight flight turns that way briefly and sank more feet (stairs17.crowd.down 1.4 -> 2.1 %)
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst = sys.argv[1:3]
s = open(src).read()
s = sub(s, """            // (going up, a tread too short for the foot whose toes-short landing would leave the ball off it - a winder's
            // narrow end: the ball on it at least, the toes nearer the riser ahead)
            if (lo > hi && upAhead && rearEdge && !upBehind && hi + L < r + mg) lo = hi = r + mg - L;
""", """            // (going up, a tread too short for the foot whose toes-short landing would leave the ball off it - a winder's
            // narrow end: the ball on it at least, the toes nearer the riser ahead. Not a run below where the foot was to
            // land: going down, that one is the far side of a turn)
            if (lo > hi && upAhead && rearEdge && !upBehind && hi + L < r + mg && g[k0] > gFoot - 0.03f) lo = hi = r + mg - L;
""")
s = sub(s, """            // put it, where the wedge treads are deeper: 3 cm for each 1/m of the bend's steady curvature, at most 7 cm)
            vec3 latNew(0.f);
            if (A.stairSeen < 1.5f && A.rootVz < -0.1f && A.turnRateS * in.turnRate > 0.f && (A.turnRateS > 0.f ? s == 0 : s == 1)) {
""", """            // put it, where the wedge treads are deeper: 3 cm for each 1/m of the bend's steady curvature, at most 7 cm - on
            // a bend of 0.5 /m or more: a straight flight's weave is not one)
            vec3 latNew(0.f);
            if (A.stairSeen < 1.5f && A.rootVz < -0.1f && A.turnRateS * in.turnRate > 0.f && (A.turnRateS > 0.f ? s == 0 : s == 1) &&
                fabsf(A.turnRateS) / Max(spd, 0.3f) > 0.5f) {
""")
open(dst, 'w').write(s)
print('curves3 applied')
