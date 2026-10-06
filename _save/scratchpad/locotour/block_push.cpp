    // ---- --autoplay locopush (locomotion test build only): the player and a crowd on the Police HQ sidewalk at 13:00,
    // the ambient population off. A knot of six standing 5 m ahead (two rows of three, 0.65 m apart, each held on its
    // spot: BRAIN_GOTO to it). The player walks into the knot and through it (stick 0.44, about 1.36 m/s) for 8 s, walks
    // back through it for 8 s, runs through it (sprint) for 4 s, then stands while four people walk at it and past it
    // (BRAIN_GOTO 8 m behind it, across offsets -0.3 .. 0.6 m). Per phase: the player's actual speed and the speed it
    // asks for (its velocity) while touching anybody (centres within 0.62 m), the time from the first touch to the last,
    // how far the knot was shoved off its spots, how far the standing player was shoved, when the walkers got past it.
    struct LpPed {
        int id = -1;
        u32 uid = 0;
        vec2 spot;
    };
    std::vector<LpPed> lpKnot, lpWalk;
    vec2 lpDir, lpRight, lpBase;
    float lpZ = 0.f;
    int lpPhase = -1;
    float lpT = 0.f;
    float lpTouchT = 0.f, lpTouchAct = 0.f, lpTouchVel = 0.f, lpFirst = -1.f, lpLast = -1.f, lpKnotMax = 0.f, lpShove = 0.f, lpPastT = -1.f;
    float lpActSum = 0.f, lpVelSum = 0.f, lpMoveT = 0.f;
    int lpTouchN = 0, lpPast = 0;
    vec2 lpPrev, lpStand;
    bool lpPrevSet = false;

    bool lpAlive(const LpPed& q) const {
        return q.id >= 0 && q.id < (int)game.peds.size() && game.peds[q.id].used && game.peds[q.id].uid == q.uid;
    }
    int lpSpawn(u32 seed, vec2 at, float yaw, vec2 goal) {
        vec3 g(at.x, at.y, game.groundHeight(at.x, at.y, lpZ + 1.f));
        int id = Transit::tg::spawnCivilian(game, seed, g, yaw);
        if (id >= 0) {
            Ped& p = game.peds[id];
            p.brain.type = BRAIN_GOTO;
            p.brain.goal = dvec3(goal.x, goal.y, g.z);
            p.brain.speed = 1.3f;
        }
        return id;
    }
    void lpReport(const char* name) {
        LOG("locopush %d %s: touching %.2f s (%d frames) | player while touching: actual %.2f m/s, asked %.2f m/s | whole phase: actual %.2f, "
            "asked %.2f | first touch %.2f s, last %.2f s | knot shoved max %.2f m | standing player shoved %.2f m | walkers past %d (last at %.2f s)",
            lpPhase, name, lpTouchT, lpTouchN, lpTouchN ? lpTouchAct / lpTouchN : 0.f, lpTouchN ? lpTouchVel / lpTouchN : 0.f,
            lpMoveT > 0.f ? lpActSum / lpMoveT : 0.f, lpMoveT > 0.f ? lpVelSum / lpMoveT : 0.f, lpFirst, lpLast, lpKnotMax, lpShove, lpPast, lpPastT);
        lpTouchT = lpTouchAct = lpTouchVel = lpKnotMax = lpShove = lpActSum = lpVelSum = lpMoveT = 0.f;
        lpFirst = lpLast = lpPastT = -1.f;
        lpTouchN = lpPast = 0;
    }

    void updateLocoPush(Controls& c, float dt) {
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl) return;
        if (tourStop < 0) {
            tourStop = 0;
            tourT = 0.f;
            mu::computePlaces(game);
            const mu::Place& P = mu::gPlaces.policeHq;
            lpDir = normalize(P.streetDir);
            lpRight = vec2(lpDir.y, -lpDir.x);
            lpBase = P.pos.xy() + P.outward * 0.6f;
            lpZ = P.pos.z;
            if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
            pl->pos = dvec3(lpBase.x, lpBase.y, game.groundHeight(lpBase.x, lpBase.y, lpZ + 1.f));
            pl->vel = vec3(0.f);
            pl->yaw = AI::dirYaw(lpDir);
            pl->invincible = true;
            game.rig.yaw = pl->yaw;
            game.rig.pitch = -0.15f;
            game.rig.cut = true;
            env.timeOfDay = 13.f;
            weather.setImmediate(WX_FAIR);
            game.populationOff = true;
            game.pinfo.wanted = 0;
            locoRenderEvery = renderEvery;
            LOG("locopush: Police HQ sidewalk at %.1f %.1f, along %.2f %.2f", lpBase.x, lpBase.y, lpDir.x, lpDir.y);
        }
        tourT += dt;
        env.timeOfDay = 13.f;
        game.pinfo.wanted = 0;
        c = Controls();
        // (the ambient people already there when the population went off: away)
        if (tourT < 1.5f)
            for (int i = 0; i < (int)game.peds.size(); i++)
                if (i != game.player && game.peds[i].used && game.peds[i].state == PS_ONFOOT && length(rel(game.peds[i].pos, pl->pos)) < 60.f)
                    game.despawnPed(i);
        if (tourT > 1.5f && lpKnot.empty()) {
            for (int k = 0; k < 6; k++) {
                LpPed q;
                q.spot = lpBase + lpDir * (5.f + 0.65f * (float)(k / 3)) + lpRight * (0.65f * (float)(k % 3 - 1));
                q.id = lpSpawn(hash32(7300u + (u32)k * 31u), q.spot, AI::dirYaw(-lpDir), q.spot);
                if (q.id >= 0) q.uid = game.peds[q.id].uid;
                lpKnot.push_back(q);
            }
            LOG("locopush: a knot of %d standing 5 m ahead", (int)lpKnot.size());
        }
        const int phase = tourT < 3.f ? 0 : tourT < 11.f ? 1 : tourT < 19.f ? 2 : tourT < 23.f ? 3 : tourT < 35.f ? 4 : 5;
        static const char* kNames[6] = {"settle", "walk into the knot and through", "walk back through", "run through", "stand, four walk at the player",
                                        "done"};
        if (phase != lpPhase) {
            if (lpPhase >= 1) lpReport(kNames[lpPhase]);
            lpPhase = phase;
            lpT = 0.f;
            if (phase == 4) {
                lpStand = pl->pos.toVec3().xy();
                // four walkers 6 m ahead of the player, heading 8 m past it
                const vec2 f = normalize(vec2(-sinf(pl->yaw), cosf(pl->yaw)));
                const vec2 r(f.y, -f.x);
                for (int k = 0; k < 4; k++) {
                    const float off = -0.3f + 0.3f * (float)k;
                    LpPed q;
                    q.spot = lpStand - f * 8.f + r * off;
                    q.id = lpSpawn(hash32(7400u + (u32)k * 37u), lpStand + f * (6.f + 0.9f * (float)k) + r * off, AI::dirYaw(-f), q.spot);
                    if (q.id >= 0) q.uid = game.peds[q.id].uid;
                    lpWalk.push_back(q);
                }
            }
            if (phase == 5) {
                LOG("locopush done");
                tourDone = true;
                game.rig.scriptActive = false;
                renderEvery = locoRenderEvery;
                return;
            }
        }
        lpT += dt;
        // the player's walk
        if (phase == 1 || phase == 3) game.rig.yaw = AI::dirYaw(lpDir);
        if (phase == 2) game.rig.yaw = AI::dirYaw(-lpDir);
        if (phase >= 1 && phase <= 3) {
            c.move = vec2(0.f, phase == 3 ? 1.f : 0.44f);
            c.sprint.down = phase == 3;
        }
        // measured
        const vec2 pp = pl->pos.toVec3().xy();
        if (lpPrevSet && dt > 0.f && phase >= 1) {
            const float act = length(pp - lpPrev) / dt, vel = length(vec2(pl->vel.x, pl->vel.y));
            bool touch = false;
            for (int i = 0; i < (int)game.peds.size(); i++)
                if (i != game.player && game.peds[i].used && game.peds[i].state == PS_ONFOOT && length(rel(game.peds[i].pos, pl->pos).xy()) < 0.62f) touch = true;
            if (touch) {
                lpTouchT += dt;
                lpTouchN++;
                lpTouchAct += act;
                lpTouchVel += vel;
                if (lpFirst < 0.f) lpFirst = lpT;
                lpLast = lpT;
            }
            if (phase <= 3) {
                lpActSum += act * dt;
                lpVelSum += vel * dt;
                lpMoveT += dt;
            }
            for (const LpPed& q : lpKnot)
                if (lpAlive(q)) lpKnotMax = Max(lpKnotMax, length(game.peds[q.id].pos.toVec3().xy() - q.spot));
            if (phase == 4) {
                lpShove = Max(lpShove, length(pp - lpStand));
                int past = 0;
                const vec2 f = normalize(vec2(-sinf(pl->yaw), cosf(pl->yaw)));
                for (const LpPed& q : lpWalk)
                    if (lpAlive(q) && dot(game.peds[q.id].pos.toVec3().xy() - lpStand, f) < -1.5f) past++;
                if (past > lpPast) lpPastT = lpT;
                lpPast = Max(lpPast, past);
            }
        }
        lpPrev = pp;
        lpPrevSet = true;
    }
