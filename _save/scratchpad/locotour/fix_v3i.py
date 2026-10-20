T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3i (10-20 11:2x): the spiral's walker on the walking line from the start. st20sp (v3g): the walkers never got onto
# the spiral - one stood 45 s at the wall's gap (r 0.94), the other went round outside the wall - "0.00 m up" all run.
# Now one walker, placed on the walking line at 340 degrees (on the ground, in the wall's gap, clear of the landing's
# end), up round to the landing (stop at 280 degrees, as before), back down round to 330-345 degrees (stop by angle),
# and again; the way down runs on past the stop for the goal ahead.
s=open(T+'block_player_v3h.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """        // the way up: in from outside at 350 degrees to the walking line, round it from there (-10 degrees) to 285 degrees
        // on the landing (where the walkers stop: kSpStop); the way down the reverse
        const float th0 = -0.1745f, th1 = 4.9742f;
        std::vector<vec2>& U = spPath[1];
        U = {spAt(3.3f, th0), spAt(kSpWalk, th0)};
        for (int k = 1; k <= 40; k++) U.push_back(spAt(kSpWalk, th0 + (th1 - th0) * (float)k / 40.f));
        spPath[0] = U;
        std::reverse(spPath[0].begin(), spPath[0].end());
        for (int d = 0; d < 2; d++) {
            spAcc[d].assign(spPath[d].size(), 0.f);
            for (size_t i = 1; i < spPath[d].size(); i++) spAcc[d][i] = spAcc[d][i - 1] + length(spPath[d][i] - spPath[d][i - 1]);
        }
        spArc0[1] = spAcc[1][1];
        spArc1[1] = spAcc[1].back();
        spArc0[0] = 0.f;
        spArc1[0] = spAcc[0][spAcc[0].size() - 2];""", """        // (v3i) the way up: along the walking line from 340 degrees (on the ground, in the wall's gap, clear of the
        // landing's end) round to 285 degrees on the landing (where the walker stops: kSpStop); the way down back round,
        // on past 340 degrees for the goal ahead (it stops by angle, at 330-345). v3g's way in from outside (radial at 350
        // degrees) left the walkers at the wall's gap or going round outside it
        const float th0 = -0.3491f, th1 = 4.9742f;
        std::vector<vec2>& U = spPath[1];
        U.clear();
        for (int k = 0; k <= 48; k++) U.push_back(spAt(kSpWalk, th0 + (th1 - th0) * (float)k / 48.f));
        std::vector<vec2>& D = spPath[0];
        D.clear();
        for (int k = 0; k <= 56; k++) D.push_back(spAt(kSpWalk, th1 - (th1 - th0 + 1.2f) * (float)k / 56.f));
        for (int d = 0; d < 2; d++) {
            spAcc[d].assign(spPath[d].size(), 0.f);
            for (size_t i = 1; i < spPath[d].size(); i++) spAcc[d][i] = spAcc[d][i - 1] + length(spPath[d][i] - spPath[d][i - 1]);
        }
        spArc0[1] = 0.f;
        spArc1[1] = spAcc[1].back();
        spArc0[0] = 0.f;
        spArc1[0] = spAcc[0].back();""")
s=sub(s, """        n = Min(n, 2);
        const vec2 park = spAt(2.9f, 0.5236f);""", """        n = Min(n, 1);   // (v3i: one - the bottom's room on the walking line holds one)
        const vec2 park = spAt(2.9f, 0.5236f);""")
s=sub(s, """            const vec2 q = spAt(3.3f + 0.8f * (float)k, th0);
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, spZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9700u + (u32)k * 61u), qz, AI::dirYaw(normalize(spC - q)));""", """            const vec2 q = spAt(kSpWalk, th0);
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, spZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9700u + (u32)k * 61u), qz, AI::dirYaw(vec2(-sinf(th0), cosf(th0))));""")
s=sub(s, """        LOG("locoplayer: %d walkers on the spiral", (int)spWalkers.size());""", """        LOG("locoplayer: %d walkers on the spiral (on the walking line)", (int)spWalkers.size());""")
s=sub(s, """            const bool atStop = w.up && w.sNear > spArc0[1] + 0.5f && phi > kSpStop + 0.1745f && phi < kTwoPi - 0.2f;
            if ((!w.up && left < 0.8f) || atStop || w.waitUntil > 3.f) {""", """            const bool atStop = w.up && w.sNear > spArc0[1] + 0.5f && phi > kSpStop + 0.1745f && phi < kTwoPi - 0.2f;
            const bool atFoot = !w.up && w.sNear > 0.5f && phi > 5.934f && phi < 6.196f;   // (v3i: down round to 330-345 degrees)
            if (atFoot || atStop || w.waitUntil > 3.f) {""")
open(T+'block_player_v3i.cpp','w').write(s)
print('v3i written', len(s))
