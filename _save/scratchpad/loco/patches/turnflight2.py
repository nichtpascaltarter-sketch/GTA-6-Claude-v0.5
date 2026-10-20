#!/usr/bin/env python3
# turnflight2.py <animator.cpp in> <animator.cpp out>   (on c14b: animator 07d29e18; character.h unchanged)
# The late re-scan's keep (a swing re-scanned past mid-swing keeps its fitted tread unless another needs a much smaller
# move) at a walk only: it fades out from 2 to 3 m/s as the run's landing lead fades in. At a jog or a run it was mixed
# (the player's run up a straight flight: lifted 0.6 -> 2.2 %), and with the fade those paces are c13's again.
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst = sys.argv[1:3]
s = open(src).read()
s = sub(s, """        // (re-scanned past mid-swing with a tread already fitted: that tread unless another needs much less of a move - a
        // fit that late cannot be reached)
""", """        // (re-scanned past mid-swing with a tread already fitted: that tread unless another needs much less of a move - a
        // fit that late cannot be reached. At a walk: from 2 to 3 m/s it fades out as the run's landing lead fades in)
""")
s = sub(s, """(keepLand > -8.f && fabsf(gl - keepLand) > 0.03f ? 0.15f : 0.f);
""", """(keepLand > -8.f && fabsf(gl - keepLand) > 0.03f ? 0.15f * (1.f - runK) : 0.f);
""")
open(dst, 'w').write(s)
print('turnflight2 applied')
