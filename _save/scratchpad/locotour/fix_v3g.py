T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3g (10-20 10:3x): the switchback's walkers one way at a time. In v3e / v3f they went up 6 s apart, each turning back
# at the top on its own, and met the next coming up on a 1.1 m flight (their lanes 0.36 m apart, two capsules need
# 0.6 m): jammed - st19n / st20n had 410 steps up and 17 down in two minutes. Now: a file at the foot of flight 1, up one
# after another 4 s apart, each to its own spot on the top platform (the first up the furthest in), and when all are
# there (or a minute has gone) down so, the last up first, each to its own spot in the file at the foot; along the
# flights' middle; a goal 0.6 m past each spot (BRAIN_GOTO stops 0.6 m short of its goal). A line at each turn of the
# file and the walkers' state every 15 s.
s=open(T+'block_player_v3f.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """    // the laps as set). LOCO_NPCSWITCH=n: then n people (at most 4) walk the switchback up and down for two minutes at
    // the stair pace (LOCO_STAIRPACE, default 0.5 m/s; each keeping to its right, 6 s apart), their feet measured there.""",
"""    // the laps as set). LOCO_NPCSWITCH=n: then n people (at most 4) walk the switchback up and down for two minutes at
    // the stair pace (LOCO_STAIRPACE, default 0.5 m/s; one way at a time, in a file 4 s apart), their feet measured there.""")
s=sub(s, """    std::vector<LqWalker> lqWalkers;
    std::vector<vec2> lqNpcPath[2];   // [0 down, 1 up]: each keeping to its right
    std::vector<float> lqNpcAcc[2];
    LqStat lqNpcStat[2];              // [0 up, 1 down]
    float lqNpcT0 = -1.f;""", """    std::vector<LqWalker> lqWalkers;
    std::vector<vec2> lqNpcPath[4][2];   // [walker][0 down, 1 up]: from its spot at one end to its spot at the other
    std::vector<float> lqNpcAcc[4][2];
    LqStat lqNpcStat[2];                 // [0 up, 1 down]
    float lqNpcT0 = -1.f, lqNpcPhaseT = 0.f, lqNpcLogT = 0.f;
    bool lqNpcUp = true, lqNpcThere[4] = {false, false, false, false};
    int lqNpcTurns = 0;""")
s=sub(s, """    // the people on the switchback: the player out of the way, n walkers at its foot, each starting up 6 s after the last
    void lqStartNpcs(Ped& pl, int n) {
        const float aT = kLqN * kLqTr + 0.55f, o = 0.18f;
        lqNpcPath[1] = {lqW(-0.55f + o, -3.f), lqW(-0.55f + o, aT - o), lqW(0.55f - o, aT - o), lqW(0.55f - o, -1.6f)};
        lqNpcPath[0] = {lqW(0.55f + o, -1.6f), lqW(0.55f + o, aT + o), lqW(-0.55f - o, aT + o), lqW(-0.55f - o, -3.f)};
        for (int d = 0; d < 2; d++) {
            lqNpcAcc[d].assign(lqNpcPath[d].size(), 0.f);
            for (size_t i = 1; i < lqNpcPath[d].size(); i++) lqNpcAcc[d][i] = lqNpcAcc[d][i - 1] + length(lqNpcPath[d][i] - lqNpcPath[d][i - 1]);
        }""", """    // the people on the switchback: the player out of the way, n walkers in a file at the foot of flight 1 (the first
    // nearest it), up one after another 4 s apart, each to its own spot on the top platform (the first up the furthest
    // in), and when all are there (or a minute has gone) down so, the last up first, each back to its spot in the file -
    // one way at a time: on a 1.1 m flight two can't pass. Along the flights' middle; each way ends 0.6 m past its spot
    // (BRAIN_GOTO stops 0.6 m short of its goal)
    void lqStartNpcs(Ped& pl, int n) {
        n = Clamp(n, 0, 4);
        const float aT = kLqN * kLqTr + 0.55f;   // (the landing's middle)
        for (int k = 0; k < n; k++) {
            const float aTop = -0.5f - 0.7f * (float)(n - 1 - k), aFoot = -3.f - 1.2f * (float)k;
            lqNpcPath[k][1] = {lqW(-0.55f, aFoot), lqW(-0.55f, aT), lqW(0.55f, aT), lqW(0.55f, aTop), lqW(0.55f, aTop - 0.6f)};
            lqNpcPath[k][0] = {lqW(0.55f, aTop), lqW(0.55f, aT), lqW(-0.55f, aT), lqW(-0.55f, aFoot), lqW(-0.55f, aFoot - 0.6f)};
            for (int d = 0; d < 2; d++) {
                lqNpcAcc[k][d].assign(lqNpcPath[k][d].size(), 0.f);
                for (size_t i = 1; i < lqNpcPath[k][d].size(); i++) lqNpcAcc[k][d][i] = lqNpcAcc[k][d][i - 1] + length(lqNpcPath[k][d][i] - lqNpcPath[k][d][i - 1]);
            }
        }""")
s=sub(s, """        for (int k = 0; k < n; k++) {
            LqWalker w;
            const vec2 q = lqNpcPath[1][0] - lqA * (1.2f * (float)k) - lqX * (0.9f * (float)(k % 2));
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, lqZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9500u + (u32)k * 53u), qz, AI::dirYaw(lqA));
            if (w.id < 0) continue;
            w.uid = game.peds[w.id].uid;
            w.startAt = tourT + 2.f + 6.f * (float)k;""", """        for (int k = 0; k < n; k++) {
            LqWalker w;
            const vec2 q = lqNpcPath[k][1][0];
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, lqZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9500u + (u32)k * 53u), qz, AI::dirYaw(lqA));
            if (w.id < 0) {
                lqWalkers.push_back(w);   // (kept in its place: walker k's ways are k's)
                continue;
            }
            w.uid = game.peds[w.id].uid;
            w.startAt = tourT + 2.f + 4.f * (float)k;""")
s=sub(s, """        lqNpcT0 = tourT;
        LOG("locoplayer: %d walkers on the switchback", (int)lqWalkers.size());
    }""", """        lqNpcT0 = tourT;
        lqNpcPhaseT = tourT;
        lqNpcLogT = tourT;
        lqNpcUp = true;
        LOG("locoplayer: %d walkers on the switchback (one way at a time)", (int)lqWalkers.size());
    }""")
s=sub(s, """    void lqUpdateNpcs() {
        static const float kPace = getenv("LOCO_STAIRPACE") ? (float)atof(getenv("LOCO_STAIRPACE")) : 0.5f;
        for (int k = 0; k < (int)lqWalkers.size(); k++) {
            LqWalker& w = lqWalkers[k];
            if (w.id < 0 || w.id >= (int)game.peds.size() || !game.peds[w.id].used || game.peds[w.id].uid != w.uid || game.peds[w.id].state != PS_ONFOOT) continue;
            Ped& p = game.peds[w.id];
            if (!w.started) {
                if (tourT < w.startAt) continue;
                // (from where it waited onto the start of the way up)
                w.started = true;
                w.up = 1;
                w.sNear = 0.f;
            }
            const std::vector<vec2>& P = lqNpcPath[w.up];
            const std::vector<float>& A = lqNpcAcc[w.up];
            const vec2 pp = p.pos.toVec3().xy();
            if (w.waitUntil > tourT) {
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = p.pos;
                continue;
            }
            w.sNear = lqPathNear(P, A, pp, w.sNear);
            if (A.back() - w.sNear < 0.8f) {   // (there - BRAIN_GOTO stops 0.6 m short of its goal: a moment, then back the other way)
                w.up = 1 - w.up;
                w.sNear = 0.f;
                w.waitUntil = tourT + 1.f;
                continue;
            }
            const vec2 aim = lqPathAt(P, A, w.sNear + 0.8f);
            const float zr = w.up ? lqZ0 + lqTop() : lqZ0;
            p.brain.type = BRAIN_GOTO;
            p.brain.goal = dvec3(aim.x, aim.y, game.groundHeight(aim.x, aim.y, zr + 0.5f));
            p.brain.speed = kPace + 0.05f * (float)k;
            // measured on the flights and the landing
            if (p.charIndex >= 0 && p.charIndex < (int)game.chars.size()) lqMeasureNpc(p, w.feet, w.up ? 0 : 1);
        }
    }""", """    void lqUpdateNpcs() {
        static const float kPace = getenv("LOCO_STAIRPACE") ? (float)atof(getenv("LOCO_STAIRPACE")) : 0.5f;
        const int n = (int)lqWalkers.size();
        int alive = 0, there = 0;
        const bool logNow = tourT - lqNpcLogT >= 15.f;
        if (logNow) lqNpcLogT = tourT;
        for (int k = 0; k < n; k++) {
            LqWalker& w = lqWalkers[k];
            if (w.id < 0 || w.id >= (int)game.peds.size() || !game.peds[w.id].used || game.peds[w.id].uid != w.uid || game.peds[w.id].state != PS_ONFOOT) continue;
            Ped& p = game.peds[w.id];
            alive++;
            w.up = lqNpcUp ? 1 : 0;
            const std::vector<vec2>& P = lqNpcPath[k][w.up];
            const std::vector<float>& A = lqNpcAcc[k][w.up];
            const vec2 pp = p.pos.toVec3().xy();
            if (logNow)
                LOG("locoplayer switchback t%.0f walker %d: %s %s s %.1f of %.1f, z %.2f, v %.2f", tourT - lqNpcT0, k, w.up ? "up" : "down", lqNpcThere[k] ? "there" : w.started ? "going" : "waiting",
                    w.sNear, A.back(), (float)p.pos.z - lqZ0, length(vec2(p.vel.x, p.vel.y)));
            if (!w.started && tourT >= w.startAt) {
                w.started = true;
                w.sNear = 0.f;
            }
            if (!w.started || lqNpcThere[k]) {
                there += lqNpcThere[k] ? 1 : 0;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = p.pos;
                continue;
            }
            w.sNear = lqPathNear(P, A, pp, w.sNear);
            if (A.back() - w.sNear < 0.65f) {   // (at its spot: it waits there for the rest)
                lqNpcThere[k] = true;
                there++;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = p.pos;
                continue;
            }
            const vec2 aim = lqPathAt(P, A, w.sNear + 0.8f);
            const float zr = w.up ? lqZ0 + lqTop() : lqZ0;
            p.brain.type = BRAIN_GOTO;
            p.brain.goal = dvec3(aim.x, aim.y, game.groundHeight(aim.x, aim.y, zr + 0.5f));
            p.brain.speed = kPace;   // (all one pace: they keep their spacing)
            // measured on the flights and the landing
            if (p.charIndex >= 0 && p.charIndex < (int)game.chars.size()) lqMeasureNpc(p, w.feet, w.up ? 0 : 1);
        }
        // all there (or a minute gone): the other way, 4 s apart, the last there first (nearest the way back)
        if (alive > 0 && (there >= alive || tourT - lqNpcPhaseT > 60.f)) {
            LOG("locoplayer switchback turn %d: %d of %d %s after %.1f s", lqNpcTurns, there, alive, lqNpcUp ? "up" : "down", tourT - lqNpcPhaseT);
            lqNpcTurns++;
            lqNpcUp = !lqNpcUp;
            lqNpcPhaseT = tourT;
            for (int k = 0; k < n; k++) {
                lqNpcThere[k] = false;
                lqWalkers[k].started = false;
                lqWalkers[k].startAt = tourT + 1.f + 4.f * (float)(lqNpcUp ? k : n - 1 - k);
            }
        }
    }""")
s=sub(s, """                if (lqNpcT0 >= 0.f && tourT < lqNpcT0 + 125.f) {""", """                if (lqNpcT0 >= 0.f && tourT < lqNpcT0 + 170.f) {   // (v3g: 170 s - two rounds of the file up and down)""")
# the chasers' spots (st19ch: the 3 m/s one's two spots on the other side at the foot were taken - by the test
# switchback's platform or something else there - so it never ran up): a few spots each, the 3 m/s one on the player's
# side behind it (its goal 1.5 m short of the player's end), the 5 m/s one on the other side; each placement logged in
# the first lap
s=sub(s, """            // the chasers: on the straight flight run or sprinted, beside the player's start on the other side of the
            // flight, the one at 5 m/s 0.6 m behind it and the one at 3 m/s 1.2 m (the faster ahead: BRAIN_GOTO eases a
            // walker to the pace of a slower one in front on its line), standing until the player sets off (3 and 6 m
            // behind was past the platform's end at the top, and in the test switchback at the foot)
            for (int k = 0; k < 2; k++) lqChaseOn[k] = false;
            if (g.stair == 0 && g.gait >= 1) {
                const vec2 oth = lsRight * (g.up ? 0.9f : -0.9f);   // (from the player's side of the flight to the other)
                std::vector<int> ids;
                collision.collidersNear(s0.xy(), 6.f, ids);
                static const float kBack[2][2] = {{1.2f, 0.9f}, {0.6f, 0.3f}};
                for (int k = 0; k < 2; k++) {
                    // (the first free spot level with the player's start, of two)
                    vec3 qz(0.f);
                    bool ok = false;
                    for (int t = 0; t < 2 && !ok; t++) {
                        const vec2 q = lqPath[0] + oth - d0 * kBack[k][t];
                        const float z = game.groundHeight(q.x, q.y, s0.z + 0.6f);
                        if (z < -1e8f || fabsf(z - s0.z) > 0.2f || lqBlocked(q, 0.32f, z + 0.05f, z + 1.8f, ids)) continue;
                        qz = vec3(q.x, q.y, z);
                        ok = true;
                    }
                    if (!ok) {
                        LOG("locoplayer seg %d: no room for chaser %d beside the player's start", lqSeg, k);
                        continue;
                    }""", """            // the chasers: on the straight flight run or sprinted, the one at 5 m/s on the other side of the flight 0.6 m
            // behind the player's start, the one at 3 m/s on the player's side 1.2 m behind it (BRAIN_GOTO eases a walker
            // to the pace of a slower one in front on its line: each has a faster one or nobody ahead), standing until the
            // player sets off; the first free spot of a few, logged in the first lap (v3e's 3 and 6 m behind was past the
            // platform's end at the top, and in the test switchback at the foot)
            for (int k = 0; k < 2; k++) lqChaseOn[k] = false;
            if (g.stair == 0 && g.gait >= 1) {
                const vec2 oth = lsRight * (g.up ? 0.9f : -0.9f);   // (from the player's side of the flight to the other)
                std::vector<int> ids;
                collision.collidersNear(s0.xy(), 6.f, ids);
                // (the side - 0 the player's, 1 the other - and how far back)
                static const float kSpot[2][5][2] = {{{0.f, 1.2f}, {0.f, 0.9f}, {0.f, 1.6f}, {1.f, 1.4f}, {1.f, 2.0f}},
                                                     {{1.f, 0.6f}, {1.f, 0.3f}, {1.f, 1.0f}, {0.f, 2.0f}, {0.f, 2.4f}}};
                vec2 taken[3] = {s0.xy(), s0.xy(), s0.xy()};
                int nTaken = 1;
                for (int k = 0; k < 2; k++) {
                    vec3 qz(0.f);
                    int lane = -1;
                    float back = 0.f;
                    for (int t = 0; t < 5 && lane < 0; t++) {
                        const vec2 q = lqPath[0] + oth * kSpot[k][t][0] - d0 * kSpot[k][t][1];
                        const float z = game.groundHeight(q.x, q.y, s0.z + 0.6f);
                        if (z < -1e8f || fabsf(z - s0.z) > 0.2f || lqBlocked(q, 0.32f, z + 0.05f, z + 1.8f, ids)) continue;
                        bool clash = false;
                        for (int j = 0; j < nTaken; j++) clash = clash || length(q - taken[j]) < 0.7f;
                        if (clash) continue;
                        qz = vec3(q.x, q.y, z);
                        lane = (int)kSpot[k][t][0];
                        back = kSpot[k][t][1];
                    }
                    if (lane < 0) {
                        LOG("locoplayer seg %d: no room for chaser %d near the player's start", lqSeg, k);
                        continue;
                    }
                    taken[nTaken++] = qz.xy();
                    if (lqSeg < 4) LOG("locoplayer seg %d chaser %d: %s side, %.1f m behind the player's start", lqSeg, k, lane ? "the other" : "the player's", back);""")
s=sub(s, """                    const vec2 e = lqPath.back() + oth;
                    lqChaseGoal[k] = dvec3(e.x, e.y, g.up ? lsTopC.z : lsFootC.z);""", """                    // (its goal: the other side's end, or on the player's side 1.5 m short of the player's)
                    const vec2 e = lane ? lqPath.back() + oth : lqPath.back() - normalize(lqPath.back() - lqPath[0]) * 1.5f;
                    lqChaseGoal[k] = dvec3(e.x, e.y, g.up ? lsTopC.z : lsFootC.z);""")
open(T+'block_player_v3g.cpp','w').write(s)
print('v3g written', len(s))
