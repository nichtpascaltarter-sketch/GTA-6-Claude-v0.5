import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''            // (a swinging foot rises to higher ground ahead - a curb, a stair - at once, but goes down to lower ground only
            // late in its swing: it clears the edge it is leaving)
            const bool down = gIn[s] < *fz[s] - dzRoot;
            const float k = held[s] ? 1.f : 1.f - expf(-dt * (down ? (swingU[s] > 0.f && swingU[s] < 0.55f ? 2.f : 14.f) : 20.f));''', '''            // (a swinging foot - its probe where it will land - gets there by the end of its swing: down to lower ground
            // evenly over what is left of it, so it clears the edge it is leaving or follows a slope down; up onto higher
            // ground, a curb or a stair, in the first part of it)
            float k = held[s] ? 1.f : 1.f - expf(-dt * 14.f);
            if (!held[s] && swingU[s] > 0.f && cycle > 0.f) {
                const float tRem = (1.f - swingU[s]) * (1.f - duty) * cycle / Max(Max(0.f, in.speed), 0.3f);
                k = Saturate(dt / Max((gIn[s] < *fz[s] - dzRoot ? 1.f : 0.4f) * tRem, dt));
            }''')
open(p, 'w').write(s)
print("ok")
