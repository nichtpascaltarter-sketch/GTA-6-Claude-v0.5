T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3h (10-20 11:1x): the switchback walkers' turn on the landing as a half circle. In v3g the way turned two right
# angles on the landing; with the goal 0.8 m ahead it swung up to ~130 degrees off a walker's body, the AI's turn easing
# (ai_turn::follow) cut its pace to a tenth, and under 0.15 m/s for 0.9 s aiWalkRound took it as held up and stepped it
# 1.1 m aside, into the walls: walkers stood 15-35 s at the turn (st19ng). Now the flights' lines at 0.5 m either side
# and a half circle of 0.5 m round the inner wall's end (centre 0.25 m onto the landing, 0.35 m clear of its end wall):
# the goal 0.8 m ahead along it is 46 degrees off the way at most, where the AI keeps the full pace.
s=open(T+'block_player_v3g.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """        const float aT = kLqN * kLqTr + 0.55f;   // (the landing's middle)
        for (int k = 0; k < n; k++) {
            const float aTop = -0.5f - 0.7f * (float)(n - 1 - k), aFoot = -3.f - 1.2f * (float)k;
            lqNpcPath[k][1] = {lqW(-0.55f, aFoot), lqW(-0.55f, aT), lqW(0.55f, aT), lqW(0.55f, aTop), lqW(0.55f, aTop - 0.6f)};
            lqNpcPath[k][0] = {lqW(0.55f, aTop), lqW(0.55f, aT), lqW(-0.55f, aT), lqW(-0.55f, aFoot), lqW(-0.55f, aFoot - 0.6f)};""",
"""        // (the turn: a half circle of 0.5 m round the inner wall's end, its centre 0.25 m onto the landing - the AI's
        // turn easing keeps the full pace with the goal 0.8 m ahead along it, 46 degrees off the way at most)
        const float aC = kLqN * kLqTr + 0.25f, rC = 0.5f;
        std::vector<vec2> turn;
        for (int i = 0; i <= 12; i++) {
            const float th = kPi * (1.f - (float)i / 12.f);
            turn.push_back(lqW(rC * cosf(th), aC + rC * sinf(th)));
        }
        for (int k = 0; k < n; k++) {
            const float aTop = -0.5f - 0.7f * (float)(n - 1 - k), aFoot = -3.f - 1.2f * (float)k;
            std::vector<vec2>& U = lqNpcPath[k][1];
            U = {lqW(-rC, aFoot)};
            U.insert(U.end(), turn.begin(), turn.end());
            U.push_back(lqW(rC, aTop));
            U.push_back(lqW(rC, aTop - 0.6f));
            std::vector<vec2>& D = lqNpcPath[k][0];
            D = {lqW(rC, aTop)};
            D.insert(D.end(), turn.rbegin(), turn.rend());
            D.push_back(lqW(-rC, aFoot));
            D.push_back(lqW(-rC, aFoot - 0.6f));""")
s=sub(s, """        LOG("locoplayer: %d walkers on the switchback (one way at a time)", (int)lqWalkers.size());""",
"""        LOG("locoplayer: %d walkers on the switchback (one way at a time, a half circle round)", (int)lqWalkers.size());""")
# the state line: where (across / along the switchback), the AI's detour and held-up timers, the body's yaw off the way
s=sub(s, """                LOG("locoplayer switchback t%.0f walker %d: %s %s s %.1f of %.1f, z %.2f, v %.2f", tourT - lqNpcT0, k, w.up ? "up" : "down", lqNpcThere[k] ? "there" : w.started ? "going" : "waiting",
                    w.sNear, A.back(), (float)p.pos.z - lqZ0, length(vec2(p.vel.x, p.vel.y)));""", """                LOG("locoplayer switchback t%.0f walker %d: %s %s s %.1f of %.1f, z %.2f, v %.2f | x %.2f a %.2f, detour %.1f s, held %.1f s, yaw off %.0f deg", tourT - lqNpcT0, k,
                    w.up ? "up" : "down", lqNpcThere[k] ? "there" : w.started ? "going" : "waiting", w.sNear, A.back(), (float)p.pos.z - lqZ0, length(vec2(p.vel.x, p.vel.y)),
                    dot(pp - lqO, lqX), dot(pp - lqO, lqA), game.pedAI(w.id).detourT, game.pedAI(w.id).stuckTimer,
                    fabsf(AI::wrapPi(AI::dirYaw(lqPathAt(P, A, w.sNear + 0.8f) - pp + vec2(1e-5f, 0.f)) - p.yaw)) * 57.3f);""")
open(T+'block_player_v3h.cpp','w').write(s)
print('v3h written', len(s))
