T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3j (10-20 11:5x): the switchback walkers' goal at least 0.8 m from them. st19nh (v3h): the first up cut inside the
# half circle (0.2 m in) and its goal 0.8 m on along the way was 0.6 m off - BRAIN_GOTO does not move for one nearer -
# so it stood on the landing for a minute, the rest held up behind it. Now the goal goes on along the way until it is
# 0.8 m away (or the way's end).
s=open(T+'block_player_v3i.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """            const vec2 aim = lqPathAt(P, A, w.sNear + 0.8f);
            const float zr = w.up ? lqZ0 + lqTop() : lqZ0;""", """            // (the goal 0.8 m on along the way, and further while it is nearer than 0.8 m - BRAIN_GOTO stands for one
            // under 0.6 m, and a walker cutting inside the turn had it there)
            float sAim = w.sNear + 0.8f;
            vec2 aim = lqPathAt(P, A, sAim);
            while (length(aim - pp) < 0.8f && sAim < A.back()) {
                sAim += 0.1f;
                aim = lqPathAt(P, A, sAim);
            }
            const float zr = w.up ? lqZ0 + lqTop() : lqZ0;""")
s=sub(s, """        LOG("locoplayer: %d walkers on the switchback (one way at a time, a half circle round)", (int)lqWalkers.size());""",
"""        LOG("locoplayer: %d walkers on the switchback (one way at a time, a half circle round, goals 0.8 m off)", (int)lqWalkers.size());""")
open(T+'block_player_v3j.cpp','w').write(s)
print('v3j written', len(s))
