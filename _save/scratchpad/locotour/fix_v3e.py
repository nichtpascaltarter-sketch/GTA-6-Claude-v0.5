T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
s=open(T+'block_player.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
# the switchback: 17 cm risers (a ped's capsule steps over a top less than 0.35 m above its feet - two 17.5 cm risers
# within its 0.3 m radius were a wall)
s=sub(s, "two flights of eight 17.5 x 28 cm treads,", "two flights of eight 17 x 28 cm treads,")
s=sub(s, "    static constexpr float kLqR = 0.175f, kLqTr = 0.28f, kLqW = 1.1f, kLqLd = 1.1f;",
"    // (17 cm risers as the metro's: a ped steps over a top under 0.35 m above its feet and its capsule (0.3 m) reaches two\n    // treads on - two 17.5 cm risers were a wall)\n    static constexpr float kLqR = 0.17f, kLqTr = 0.28f, kLqW = 1.1f, kLqLd = 1.1f;")
# the spiral: 17 cm risers, nine treads (270 degrees) to a landing (270..330), the way in radial at 345 degrees
s=sub(s, """    // ---- the test spiral (LOCO_NPCSPIRAL=n, after the switchback's walkers): the kit's spiral (docs/HANDBUILT_WORLD.md:
    // 1.7 m across, the walking line 0.55 m from the centre, 30-degree treads) with 0.22 m risers - twelve treads round a
    // 0.2 m column, a full turn up to a landing over the first two treads (2 m and more above them), the outer wall round
    // it with the way in at the foot (-60..0 degrees) and the way out at the top (30..60 degrees, onto a platform).
    // Collision boxes only (six radial slabs a tread, meeting at the outer edge), a cell of its own. n walkers go up it
    // together at the stair pace, on the walking line, 6 s apart, wait for each other at the top, come down so, and again,
    // for two minutes (one way at a time: the stair is too narrow to pass on); their feet on the treads are measured as
    // the switchback's ("locoplayer spiral up / down npc" lines)
    static constexpr float kSpRin = 0.1f, kSpRout = 0.85f, kSpRise = 0.22f, kSpA = 0.5235988f, kSpWalk = 0.55f;
    static constexpr int kSpN = 12;""", """    // ---- the test spiral (LOCO_NPCSPIRAL=n, after the switchback's walkers): the kit's spiral (docs/HANDBUILT_WORLD.md:
    // 1.7 m across, the walking line 0.55 m from the centre, 30-degree treads) - nine treads round a 0.2 m column, 270
    // degrees up to a landing (270..330 degrees), the outer wall round it with the way in at 345 degrees (radial, from
    // outside). 17 cm risers, not the kit's 19-22: a ped's capsule steps over a top under 0.35 m above its feet and reaches
    // two treads on, so two higher risers are a wall to it (and the stair passes under nothing, so no headroom limits).
    // Collision boxes only (six radial slabs a tread, meeting at the outer edge), a cell of its own. Up to 2 walkers go up
    // it together at the stair pace, on the walking line, 6 s apart, wait for each other on the landing, come down so,
    // and again, for two minutes (one way at a time: the stair is too narrow to pass on, the landing holds two); their
    // feet on the treads are measured as the switchback's ("locoplayer spiral up / down npc" lines)
    static constexpr float kSpRin = 0.1f, kSpRout = 0.85f, kSpRise = 0.17f, kSpA = 0.5235988f, kSpWalk = 0.55f;
    static constexpr int kSpN = 9;""")
s=sub(s, """    std::vector<float> spAcc[2];
    LqStat spStat[2];              // [0 up, 1 down]""", """    std::vector<float> spAcc[2];
    float spArc0[2] = {0.f, 0.f}, spArc1[2] = {0.f, 0.f};   // where along each way the turn begins and ends
    LqStat spStat[2];              // [0 up, 1 down]""")
# the site: the disc and the way in at 345 degrees out to 3.6 m (the walkers wait there)
s=sub(s, """        const int nFlat = (int)pts.size();
        for (int iy = 0; iy < 5; iy++)
            for (int ix = 0; ix < 3; ix++) pts.push_back(vec2(0.55f + 0.7f * (float)ix, -2.8f - 0.85f * (float)iy));""", """        const int nFlat = (int)pts.size();
        for (int ir = 0; ir < 4; ir++) pts.push_back(vec2(cosf(6.0214f), sinf(6.0214f)) * (3.3f + 0.4f * (float)ir));   // (the waiting line at 345 degrees)""")
# build: the landing at 270..330, no way out, the wall gap 340..350 (the way in)
s=sub(s, """        for (int j = 0; j < 12; j++) slab(sl * ((float)j + 0.5f), kSpRin, kSpRout, halfT, zTop - 0.15f, zTop);   // (the landing, 0..60 degrees)
        slab(0.7854f, 0.8f, 3.f, 0.6f, zTop - 0.15f, zTop);                                                      // (the way out, at 45 degrees)""",
"""        for (int j = 0; j < 12; j++) slab(4.712389f + sl * ((float)j + 0.5f), kSpRin, kSpRout, halfT, Max(-0.3f, zTop - 0.3f), zTop);   // (the landing, 270..330 degrees)""")
s=sub(s, """            if (a0 >= 300.f) slab(th, kSpRout, kSpRout + 0.1f, wt, zTop - 0.25f, zTop + 1.1f);    // (the way in below)
            else if (a0 >= 30.f && a0 < 60.f) slab(th, kSpRout, kSpRout + 0.1f, wt, -0.3f, zTop - 0.2f);   // (the way out above)
            else slab(th, kSpRout, kSpRout + 0.1f, wt, -0.3f, zTop + 1.1f);""", """            if (a0 == 340.f) continue;   // (the way in: 340..350 degrees)
            slab(th, kSpRout, kSpRout + 0.1f, wt, -0.3f, zTop + 1.1f);""")
# paths: in radially at 345 degrees, round from 345 (-15) to 300 degrees on the landing; down the reverse
s=sub(s, """        // the way up: in along the tangent at 0 degrees from outside, round the walking line a full turn and on to 45
        // degrees on the landing, out along the way out; the way down the reverse
        std::vector<vec2>& U = spPath[1];
        U = {spC + vec2(kSpWalk, -2.4f), spC + vec2(kSpWalk, -0.05f)};
        for (int k = 1; k <= 40; k++) U.push_back(spAt(kSpWalk, (kTwoPi + 0.7854f) * (float)k / 40.f));
        U.push_back(spAt(2.6f, 0.7854f));
        spPath[0] = U;
        std::reverse(spPath[0].begin(), spPath[0].end());
        for (int d = 0; d < 2; d++) {
            spAcc[d].assign(spPath[d].size(), 0.f);
            for (size_t i = 1; i < spPath[d].size(); i++) spAcc[d][i] = spAcc[d][i - 1] + length(spPath[d][i] - spPath[d][i - 1]);
        }""", """        // the way up: in from outside at 345 degrees to the walking line, round it from there (-15 degrees) to 300 degrees
        // on the landing; the way down the reverse
        const float th0 = -0.2618f, th1 = 5.2360f;
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
        spArc1[0] = spAcc[0][spAcc[0].size() - 2];""")
s=sub(s, """        const vec2 park = spC + vec2(1.95f, -4.5f);""", """        n = Min(n, 2);
        const vec2 park = spAt(2.9f, 0.5236f);""")
s=sub(s, """            const vec2 q = spPath[1][0] - vec2(0.f, 1.2f * (float)k) + vec2(0.7f * (float)(k % 2), 0.f);
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, spZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9700u + (u32)k * 61u), qz, AI::dirYaw(vec2(0.f, 1.f)));""", """            const vec2 q = spAt(3.3f + 0.8f * (float)k, th0);
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, spZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9700u + (u32)k * 61u), qz, AI::dirYaw(normalize(spC - q)));""")
# steering: the turn per way from spArc0 / spArc1
s=sub(s, """            if (A.size() > 41 && w.sNear > A[1] + 0.1f && w.sNear < A[41] - 0.3f) {""", """            if (w.sNear > spArc0[w.up] + 0.1f && w.sNear < spArc1[w.up] - 0.3f) {""")
open(T+'block_player.cpp','w').write(s); print('v3e written')
