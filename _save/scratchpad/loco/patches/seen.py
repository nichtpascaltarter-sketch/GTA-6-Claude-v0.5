#!/usr/bin/env python3
# seen.py <animator.cpp in> <animator.cpp out>   (on c13b: animator 24831ff3; character.h unchanged)
# The stairs "seen" window counts the way walked, not the time: dt * clamp(speed / 1.2, 0.4, 1) - about 1.8 m at any
# pace (at 0.5 m/s the 1.5 s window lapsed on a switchback's landing and the swing toward the second flight never scanned)
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst = sys.argv[1:3]
s = open(src).read()
s = sub(s, """    if (walking) A.stairSeen += dt;   // (standing on stairs, they stay seen: its steps land on treads too)
""", """    // (seen for about 1.8 m of the way walked - at a slow walk across a landing as at a walk; standing on stairs, they stay
    // seen: its steps land on treads too)
    if (walking) A.stairSeen += dt * Clamp(Max(in.speed, 0.f) / 1.2f, 0.4f, 1.f);
""")
open(dst, 'w').write(s)
print('seen applied')
