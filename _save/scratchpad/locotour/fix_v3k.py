T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3k (10-20 12:2x): the spiral's walker 5 cm inside the walking line. st20spi / st21spi (v3i): it climbed in stops and
# starts (6% of the time moving): its 0.3 m capsule on the 0.55 m walking line touches the outer wall's inner face
# (0.85 m) and snags on the corners of the wall's 10-degree boxes as it drifts out. Now its line is 0.5 m (0.05 m clear).
s=open(T+'block_player_v3j.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """    static constexpr float kSpRin = 0.1f, kSpRout = 0.85f, kSpRise = 0.17f, kSpA = 0.5235988f, kSpWalk = 0.55f;""",
"""    static constexpr float kSpRin = 0.1f, kSpRout = 0.85f, kSpRise = 0.17f, kSpA = 0.5235988f, kSpWalk = 0.55f;
    // (v3k: the walker's line 5 cm inside the kit's walking line - on it, its 0.3 m capsule touches the outer wall at
    // 0.85 m and snags on the wall boxes' corners: v3i's walker moved 6% of the time)
    static constexpr float kSpLine = 0.5f;""")
s=sub(s, """        for (int k = 0; k <= 48; k++) U.push_back(spAt(kSpWalk, th0 + (th1 - th0) * (float)k / 48.f));""",
"""        for (int k = 0; k <= 48; k++) U.push_back(spAt(kSpLine, th0 + (th1 - th0) * (float)k / 48.f));""")
s=sub(s, """        for (int k = 0; k <= 56; k++) D.push_back(spAt(kSpWalk, th1 - (th1 - th0 + 1.2f) * (float)k / 56.f));""",
"""        for (int k = 0; k <= 56; k++) D.push_back(spAt(kSpLine, th1 - (th1 - th0 + 1.2f) * (float)k / 56.f));""")
s=sub(s, """            const vec2 q = spAt(kSpWalk, th0);""", """            const vec2 q = spAt(kSpLine, th0);""")
s=sub(s, """                aim = pp + normalize(along + out * Clamp(2.5f * (kSpWalk - r), -0.6f, 0.6f)) * 0.8f;""",
"""                aim = pp + normalize(along + out * Clamp(2.5f * (kSpLine - r), -0.6f, 0.6f)) * 0.8f;""")
s=sub(s, """                const float e = length(pp - spC) - kSpWalk;""", """                const float e = length(pp - spC) - kSpLine;""")
s=sub(s, """        LOG("locoplayer: %d walkers on the spiral (on the walking line)", (int)spWalkers.size());""",
"""        LOG("locoplayer: %d walkers on the spiral (on the walking line, at 0.5 m)", (int)spWalkers.size());""")
open(T+'block_player_v3k.cpp','w').write(s)
print('v3k written', len(s))
