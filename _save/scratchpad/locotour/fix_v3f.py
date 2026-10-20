T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3f (10-20 10:0x): the chasers. v3e put them 3 and 6 m behind the player's start: at the top that is past the
# platform's end (they came down to the street and never reached the flight), at the foot the 5 m/s one stood in the
# test switchback; and they were measured only until the player got to the other end. Now: beside the player's start on
# the other side of the flight (the 5 m/s one 0.6 m behind, the 3 m/s one 1.2 m: the faster ahead, as BRAIN_GOTO eases a
# walker to the pace of a slower one in front on its line), standing until the player sets off, measured until they get
# to the other end (the segment waits for them, at most 25 s), one line a segment on how they did. LOCO_CHASE=1: those
# segments only.
s=open(T+'block_player_v3e.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """    // two people chase the player up and down it (BRAIN_GOTO at 3 and 5 m/s, 3 and 6 m behind on the other side), their
    // feet measured too. LOCO_NPCSWITCH=n: then n people (at most 4) walk the switchback up and down for two minutes at""",
"""    // two people chase the player up and down it (BRAIN_GOTO at 3 and 5 m/s, from 1.2 and 0.6 m behind the player's start
    // on the other side of the flight, setting off with the player), their feet measured until they get to the other end
    // (the segment waits for them). LOCO_CHASE=1: those segments only (the straight flight run and sprinted, up and down,
    // the laps as set). LOCO_NPCSWITCH=n: then n people (at most 4) walk the switchback up and down for two minutes at""")
s=sub(s, """    int lqDeepLog = 0;
""", """    int lqDeepLog = 0;
    dvec3 lqChaseGoal[2];
    vec2 lqChasePrev[2];
    float lqChaseT0 = 0.f, lqChaseDist[2] = {0.f, 0.f}, lqChaseArr[2] = {-1.f, -1.f};
    bool lqChaseOn[2] = {false, false}, lqChaseGot[2] = {false, false};
""")
s=sub(s, """    bool lqChaserAlive(int k) const {
        const int id = lqChaser[k];
        return id >= 0 && id < (int)game.peds.size() && game.peds[id].used && game.peds[id].uid == lqChaserUid[k] && game.peds[id].state == PS_ONFOOT;
    }
""", """    bool lqChaserAlive(int k) const {
        const int id = lqChaser[k];
        return id >= 0 && id < (int)game.peds.size() && game.peds[id].used && game.peds[id].uid == lqChaserUid[k] && game.peds[id].state == PS_ONFOOT;
    }
    // the chasers under way: their feet measured on the flight, how far they went, when they got there (within 1 m of
    // the other end; at most 25 s after setting off). True while one of them is still on its way
    bool lqChaseStep(const LqSeg& g) {
        bool going = false;
        for (int k = 0; k < 2; k++) {
            if (!lqChaseOn[k] || lqChaseArr[k] >= 0.f) continue;
            if (!lqChaserAlive(k)) {
                lqChaseOn[k] = false;
                continue;
            }
            const Ped& q = game.peds[lqChaser[k]];
            lqMeasurePed(q, lqFeet[1 + k], 0, g.up ? 0 : 1, 3 + k);
            const vec2 qp = q.pos.toVec3().xy();
            lqChaseDist[k] += length(qp - lqChasePrev[k]);
            lqChasePrev[k] = qp;
            lqChaseGot[k] = length(qp - lqChaseGoal[k].toVec3().xy()) < 1.f;
            if (lqChaseGot[k] || tourT - lqChaseT0 > 25.f) lqChaseArr[k] = tourT - lqChaseT0;
            else going = true;
        }
        return going;
    }
    void lqChaseLog(int seg) {
        std::string s;
        for (int k = 0; k < 2; k++) {
            s += k == 0 ? " chase3 " : ", chase5 ";
            if (!lqChaseOn[k]) s += "-";
            else
                s += StrFormat("%s %.1f s %.1f m %.2f m/s", lqChaseGot[k] ? "there" : "stuck", lqChaseArr[k], lqChaseDist[k], lqChaseArr[k] > 0.f ? lqChaseDist[k] / lqChaseArr[k] : 0.f);
            lqChaseOn[k] = false;
        }
        LOG("locoplayer seg %d chasers:%s", seg, s.c_str());
    }
""")
s=sub(s, """            static const int kLaps = getenv("LOCO_PLAYERLAPS") ? Max(0, atoi(getenv("LOCO_PLAYERLAPS"))) : 2;
            for (int lap = 0; lap < kLaps; lap++)
                for (int g = 0; g < 3; g++)
                    for (int stc = 0; stc < 2; stc++)
                        for (int up = 1; up >= 0; up--) lqSegs.push_back({stc, up, g});""",
"""            static const int kLaps = getenv("LOCO_PLAYERLAPS") ? Max(0, atoi(getenv("LOCO_PLAYERLAPS"))) : 2;
            static const bool kChaseOnly = getenv("LOCO_CHASE") && atoi(getenv("LOCO_CHASE")) > 0;
            for (int lap = 0; lap < kLaps; lap++)
                for (int g = kChaseOnly ? 1 : 0; g < 3; g++)
                    for (int stc = 0; stc < (kChaseOnly ? 1 : 2); stc++)
                        for (int up = 1; up >= 0; up--) lqSegs.push_back({stc, up, g});""")
s=sub(s, """        if (lqSeg < 0 || lqPhase == 2) {
            if (lqSeg >= 0 && lqPhase == 2 && tourT - lqArrT < 0.6f) return;""", """        if (lqSeg < 0 || lqPhase == 2) {
            if (lqSeg >= 0 && lqPhase == 2) {
                // (the chasers still under way: measured until they get there, the segment waiting for them)
                const bool chasing = lqChaseStep(lqSegs[lqSeg]);
                if (tourT - lqArrT < 0.6f || chasing) return;
                if (lqChaseOn[0] || lqChaseOn[1]) lqChaseLog(lqSeg);
            }""")
s=sub(s, """            // the chasers: on the straight flight run or sprinted, 3 and 6 m behind the player on the other side
            if (g.stair == 0 && g.gait >= 1) {
                for (int k = 0; k < 2; k++) {
                    const vec2 q = lqPath[0] - d0 * (3.f + 3.f * (float)k) - lsRight * (g.up ? -0.9f : 0.9f);
                    const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, s0.z + 0.6f));
                    if (!lqChaserAlive(k)) {""", """            // the chasers: on the straight flight run or sprinted, beside the player's start on the other side of the
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
                    }
                    if (!lqChaserAlive(k)) {""")
s=sub(s, """                    q2.brain.type = BRAIN_GOTO;
                    const vec2 e = lqPath.back() - lsRight * (g.up ? -0.9f : 0.9f);
                    q2.brain.goal = dvec3(e.x, e.y, g.up ? lsTopC.z : lsFootC.z);
                    q2.brain.speed = k == 0 ? 3.f : 5.f;
                    q2.invincible = true;
                    for (int s = 0; s < 2; s++) lqFeet[1 + k][s] = LqFoot();""", """                    q2.brain.type = BRAIN_GOTO;
                    q2.brain.goal = q2.pos;   // (standing until the player sets off)
                    q2.brain.speed = k == 0 ? 3.f : 5.f;
                    q2.invincible = true;
                    const vec2 e = lqPath.back() + oth;
                    lqChaseGoal[k] = dvec3(e.x, e.y, g.up ? lsTopC.z : lsFootC.z);
                    lqChaseOn[k] = true;
                    lqChaseGot[k] = false;
                    lqChaseDist[k] = 0.f;
                    lqChaseArr[k] = -1.f;
                    lqChasePrev[k] = qz.xy();
                    for (int s = 0; s < 2; s++) lqFeet[1 + k][s] = LqFoot();""")
s=sub(s, """        if (lqPhase == 0) {
            if (lqT > 1.4f) lqPhase = 1, lqT = 0.f;
            return;
        }""", """        if (lqPhase == 0) {
            if (lqT > 1.4f) {
                lqPhase = 1, lqT = 0.f;
                // (the chasers set off with the player)
                lqChaseT0 = tourT;
                for (int k = 0; k < 2; k++)
                    if (lqChaseOn[k] && lqChaserAlive(k)) game.peds[lqChaser[k]].brain.goal = lqChaseGoal[k];
            }
            return;
        }""")
s=sub(s, """        lqMeasurePed(*pl, lqFeet[0], g.stair, g.up ? 0 : 1, g.gait);
        if (g.stair == 0 && g.gait >= 1)
            for (int k = 0; k < 2; k++)
                if (lqChaserAlive(k)) lqMeasurePed(game.peds[lqChaser[k]], lqFeet[1 + k], 0, g.up ? 0 : 1, 3 + k);""", """        lqMeasurePed(*pl, lqFeet[0], g.stair, g.up ? 0 : 1, g.gait);
        lqChaseStep(g);""")
open(T+'block_player_v3f.cpp','w').write(s)
print('v3f written', len(s))
