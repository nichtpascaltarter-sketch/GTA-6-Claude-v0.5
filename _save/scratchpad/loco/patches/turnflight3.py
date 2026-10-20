#!/usr/bin/env python3
# turnflight3.py <animator.cpp in> <animator.cpp out>   (on c14c: animator 4c1b658f; character.h unchanged)
# The late re-scan's keep at a steady pace only (x blendSteady): a walker stopping on a flight came down short of the
# far tread the keep held (stairs17.stops.down float max 43 -> 150 mm); with the gate that case is c13's again and the
# keep's gains on the interior and the straight stairs at 0.5 m/s stay.
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst = sys.argv[1:3]
s = open(src).read()
s = sub(s, """        // (re-scanned past mid-swing with a tread already fitted: that tread unless another needs much less of a move - a
        // fit that late cannot be reached. At a walk: from 2 to 3 m/s it fades out as the run's landing lead fades in)
""", """        // (re-scanned past mid-swing with a tread already fitted: that tread unless another needs much less of a move - a
        // fit that late cannot be reached. At a walk: from 2 to 3 m/s it fades out as the run's landing lead fades in. At
        // a steady pace: stopping, the foot comes down short of the tread it was fitted to)
""")
s = sub(s, """            const float cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f) + (keepLand > -8.f && fabsf(gl - keepLand) > 0.03f ? 0.15f * (1.f - runK) : 0.f);
""", """            const float keepCost = keepLand > -8.f && fabsf(gl - keepLand) > 0.03f ? 0.15f * (1.f - runK) * A.blendSteady : 0.f;
            const float cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f) + keepCost;
""")
open(dst, 'w').write(s)
print('turnflight3 applied')
