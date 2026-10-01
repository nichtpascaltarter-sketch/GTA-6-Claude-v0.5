import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''        rate = Min(rate, 2.4f) / ls;
        if (in.swimming) rate = Max(v, 0.5f) / stride(CLIP_SWIM) / ls;
        moveW = walkW;''', '''        rate = Min(rate, 2.4f) / ls;
        if (in.swimming) rate = Max(v, 0.5f) / stride(CLIP_SWIM) / ls;
        const float moveW0 = moveW;
        moveW = walkW;''')
rep('''        if (v > 0.02f || in.swimming) phase += dt * rate * limpF;''', '''        // setting off from standing: the foot without the weight steps first, at once (the phase starts just before its
        // toe-off; the planted feet stand meanwhile)
        if (moveW0 < 0.02f && walkW >= 0.02f && plantOn > 0.5f && cw < 0.5f && !in.swimming && !in.inAir &&
            (action < 0 || actionFinished || actionUpper)) {
            float ph = locoDuty - 0.03f - (standW < 0.5f ? 0.5f : 0.f);   // (the weight on the left: the right foot is free)
            phase = ph - floorf(ph);
        }
        if (v > 0.02f || in.swimming) phase += dt * rate * limpF;''')
open(p, 'w').write(s)
print("ok")
