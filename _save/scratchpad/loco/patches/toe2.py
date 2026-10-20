#!/usr/bin/env python3
# Item 2: the ascent toe clearance 5.5 cm*scale plus half an update's travel (the heel strike is seen up to an update
# late, the foot comes down that much further on). usage: toe2.py <animator.cpp>
import sys
p = sys.argv[1]
s = open(p).read()
old = '''            if (upAhead) hi = f - mg - footLen - 0.045f * scale;   // (the toes come forward 3-5 cm over the stance, rolling over the ball)'''
new = '''            // (the toes come forward 3-5 cm over the stance, rolling over the ball; and the heel strike is seen up to an update
            // late, the foot coming down that much further on)
            if (upAhead) hi = f - mg - footLen - 0.055f * scale - 0.5f * Max(in.speed, 0.f) * dt;'''
assert s.count(old) == 1, 'toe line not found'
s = s.replace(old, new)
open(p, 'w').write(s)
print('patched', p)
