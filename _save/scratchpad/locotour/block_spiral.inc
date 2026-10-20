    // ---- the test spiral (LOCO_NPCSPIRAL=n, after the switchback's walkers): the kit's spiral (docs/HANDBUILT_WORLD.md:
    // 1.7 m across, the walking line 0.55 m from the centre, 30-degree treads) with 0.22 m risers - twelve treads round a
    // 0.2 m column, a full turn up to a landing over the first two treads (2 m and more above them), the outer wall round
    // it with the way in at the foot (-60..0 degrees) and the way out at the top (30..60 degrees, onto a platform).
    // Collision boxes only (six radial slabs a tread, meeting at the outer edge), a cell of its own. n walkers go up it
    // together at the stair pace, on the walking line, 6 s apart, wait for each other at the top, come down so, and again,
    // for two minutes (one way at a time: the stair is too narrow to pass on); their feet on the treads are measured as
    // the switchback's ("locoplayer spiral up / down npc" lines)
    static constexpr float kSpRin = 0.1f, kSpRout = 0.85f, kSpRise = 0.22f, kSpA = 0.5235988f, kSpWalk = 0.55f;
    static constexpr int kSpN = 12;
    vec2 spC = vec2(0.f);
    float spZ0 = 0.f, spT0 = -1.f;
    std::vector<vec2> spPath[2];   // [0 down, 1 up]
    std::vector<float> spAcc[2];
    LqStat spStat[2];              // [0 up, 1 down]
    std::vector<LqWalker> spWalkers;
    bool spUp = true;          // the way the walkers are going now
    float spPhaseT = 0.f;      // when they set off that way
    std::vector<int> spThere;  // each walker at the end of the way, waiting for the others: 0 not yet, else its arrival (1 first)
    int spArrived = 0;
    double spLineErr = 0.0;   // the walkers' distance from the centre less the walking line's, on the treads: sum, worst, frames
    float spLineAbs = 0.f;
    int spLineN = 0;
    vec2 spAt(float r, float th) const { return spC + vec2(cosf(th), sinf(th)) * r; }
    float spTop() const { return (float)kSpN * kSpRise; }

    // a flat, open 6 x 6 m spot beside the stair's foot, clear of the switchback
    bool spFindSite() {
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 12; j++) {
                const float along = 6.f + 3.f * (float)i, across = (j & 1 ? 1.f : -1.f) * (5.f + 3.f * (float)(j / 2));
                const vec2 c = lsFootC.xy() + lsDir * along + lsRight * across;
                if (lqSiteOk && length(c - (lqO + lqA * 1.6f)) < 9.f) continue;
                const float z = game.groundHeight(c.x, c.y, lsFootC.z + 2.f);
                if (z < -1e8f || fabsf(z - lsFootC.z) > 2.5f) continue;
                bool ok = true;
                for (int k = 0; k < 25 && ok; k++) {
                    const vec2 q = c + vec2(1.5f * (float)(k % 5 - 2), 1.5f * (float)(k / 5 - 2));
                    const float zq = game.groundHeight(q.x, q.y, z + 1.f);
                    ok = zq > -1e8f && fabsf(zq - z) < 0.04f;
                }
                if (!ok) continue;
                float u = 0.f;
                if (game.laneGraph.nearestLane(c, vec2(0.f), 6.f, &u) >= 0) continue;
                std::vector<int> nearby;
                collision.collidersNear(c, 6.f, nearby);
                if (!nearby.empty()) continue;
                spC = c;
                spZ0 = z;
                return true;
            }
        return false;
    }

    void spBuild() {
        std::vector<World::CollisionBox> boxes;
        auto slab = [&](float th, float r0, float r1, float halfTan, float z0, float z1) {
            World::CollisionBox b;
            const vec2 d(cosf(th), sinf(th));
            const vec2 c = spC + d * (0.5f * (r0 + r1));
            b.c = vec3(c.x, c.y, spZ0 + 0.5f * (z0 + z1));
            b.ax = d;
            b.he = vec3(0.5f * (r1 - r0), halfTan, 0.5f * (z1 - z0));
            boxes.push_back(b);
        };
        const float sl = kSpA / 6.f, halfT = kSpRout * tanf(0.5f * sl), zTop = spTop();
        for (int k = 0; k < kSpN; k++)
            for (int j = 0; j < 6; j++) {
                const float top = (float)(k + 1) * kSpRise;
                slab(kSpA * (float)k + sl * ((float)j + 0.5f), kSpRin, kSpRout, halfT, Max(-0.3f, top - 0.3f), top);
            }
        for (int j = 0; j < 12; j++) slab(sl * ((float)j + 0.5f), kSpRin, kSpRout, halfT, zTop - 0.15f, zTop);   // (the landing, 0..60 degrees)
        slab(0.7854f, 0.8f, 3.f, 0.6f, zTop - 0.15f, zTop);                                                      // (the way out, at 45 degrees)
        {
            World::CollisionBox b;   // (the column)
            b.c = vec3(spC.x, spC.y, spZ0 + 0.5f * (zTop + 1.f));
            b.ax = vec2(1.f, 0.f);
            b.he = vec3(kSpRin, kSpRin, 0.5f * (zTop + 1.f) + 0.3f);
            boxes.push_back(b);
        }
        const float wt = (kSpRout + 0.1f) * tanf(5.f * 0.0174533f) + 0.01f;
        for (int k = 0; k < 36; k++) {
            const float a0 = 10.f * (float)k, th = (a0 + 5.f) * 0.0174533f;
            if (a0 >= 300.f) slab(th, kSpRout, kSpRout + 0.1f, wt, zTop - 0.25f, zTop + 1.1f);    // (the way in below)
            else if (a0 >= 30.f && a0 < 60.f) slab(th, kSpRout, kSpRout + 0.1f, wt, -0.3f, zTop - 0.2f);   // (the way out above)
            else slab(th, kSpRout, kSpRout + 0.1f, wt, -0.3f, zTop + 1.1f);
        }
        collision.addCell(0x6E0F0F10, boxes, std::vector<World::PropInstance>());
        LOG("locoplayer: spiral at %.1f %.1f %.2f (%d boxes)", spC.x, spC.y, spZ0, (int)boxes.size());
    }

    // on the treads (not the landing): within the walls, above the first tread's foot and below the landing
    bool spOn(const Ped& p) const {
        const vec3 pp = p.pos.toVec3();
        const float r = length(pp.xy() - spC), h = pp.z - spZ0;
        return r > kSpRin && r < kSpRout + 0.05f && h > 0.12f && h < spTop() - 0.08f;
    }

    void spStart(Ped& pl, int n) {
        spT0 = -2.f;   // (tried)
        if (!spFindSite()) {
            LOG("locoplayer: no open flat spot for the spiral");
            return;
        }
        spBuild();
        // the way up: in along the tangent at 0 degrees from outside, round the walking line a full turn and on to 45
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
        }
        for (const LqWalker& w : lqWalkers)
            if (w.id >= 0 && w.id < (int)game.peds.size() && game.peds[w.id].used && game.peds[w.id].uid == w.uid) game.despawnPed(w.id);
        lqWalkers.clear();
        const vec2 park = spC + vec2(3.2f, -3.2f);
        pl.pos = dvec3(park.x, park.y, game.groundHeight(park.x, park.y, spZ0 + 1.f));
        pl.vel = vec3(0.f);
        pl.yaw = AI::dirYaw(normalize(spC - park));
        game.rig.yaw = pl.yaw;
        game.rig.cut = true;
        for (int k = 0; k < n; k++) {
            LqWalker w;
            const vec2 q = spPath[1][0] - vec2(0.f, 1.2f * (float)k) + vec2(0.7f * (float)(k % 2), 0.f);
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, spZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9700u + (u32)k * 61u), qz, AI::dirYaw(vec2(0.f, 1.f)));
            if (w.id < 0) continue;
            w.uid = game.peds[w.id].uid;
            w.startAt = tourT + 2.f + 6.f * (float)k;
            game.peds[w.id].brain.type = BRAIN_GOTO;
            game.peds[w.id].brain.goal = game.peds[w.id].pos;
            game.peds[w.id].invincible = true;
            spWalkers.push_back(w);
        }
        spT0 = spPhaseT = tourT;
        spUp = true;
        spThere.assign(spWalkers.size(), 0);
        spArrived = 0;
        LOG("locoplayer: %d walkers on the spiral", (int)spWalkers.size());
    }

    void spUpdate(float dt) {
        static const float kPace = getenv("LOCO_STAIRPACE") ? (float)atof(getenv("LOCO_STAIRPACE")) : 0.5f;
        int alive = 0, there = 0;
        for (int k = 0; k < (int)spWalkers.size(); k++) {
            LqWalker& w = spWalkers[k];
            if (w.id < 0 || w.id >= (int)game.peds.size() || !game.peds[w.id].used || game.peds[w.id].uid != w.uid || game.peds[w.id].state != PS_ONFOOT) continue;
            alive++;
            Ped& p = game.peds[w.id];
            if (tourT < w.startAt || spThere[k]) {   // (its turn not yet, or there: waiting)
                there += spThere[k] ? 1 : 0;
                p.brain.type = BRAIN_GOTO;
                p.brain.goal = p.pos;
                continue;
            }
            if (!w.started) {
                w.started = true;
                w.sNear = 0.f;
                w.waitUntil = 0.f;   // (here: how long it has stood near the end)
            }
            w.up = spUp ? 1 : 0;
            const std::vector<vec2>& P = spPath[w.up];
            const std::vector<float>& A = spAcc[w.up];
            const vec2 pp = p.pos.toVec3().xy();
            w.sNear = lqPathNear(P, A, pp, w.sNear);
            const float left = A.back() - w.sNear;
            // (at the end - or held up within 2 m of it by those already there, 3 s: there)
            w.waitUntil = left < 2.f && length(vec2(p.vel.x, p.vel.y)) < 0.05f ? w.waitUntil + dt : 0.f;
            if (left < 0.4f || w.waitUntil > 3.f) {
                spThere[k] = ++spArrived;
                continue;
            }
            // (on the turn: the aim 0.3 rad on round a circle a little wider than the walking line, so that the walker heading
            // for it goes round the line itself - aimed at the line, a follower cuts inside it; on the ways in and out, 0.5 m
            // on along them. Its ground is asked from the height the walker is at: the tread there, not one a turn up or down)
            vec2 aim = lqPathAt(P, A, w.sNear + 0.5f);
            if (A.size() > 41 && w.sNear > A[1] + 0.1f && w.sNear < A[41] - 0.3f) {
                const vec2 d = pp - spC;
                aim = spAt(kSpWalk / cosf(0.3f), atan2f(d.y, d.x) + (w.up ? 0.3f : -0.3f));
            }
            if (spOn(p)) {
                const float e = length(pp - spC) - kSpWalk;
                spLineErr += e;
                spLineAbs = Max(spLineAbs, fabsf(e));
                spLineN++;
            }
            p.brain.type = BRAIN_GOTO;
            p.brain.goal = dvec3(aim.x, aim.y, game.groundHeight(aim.x, aim.y, (float)p.pos.z + 0.4f));
            p.brain.speed = kPace;   // (all one pace: they keep their spacing)
            if (p.charIndex >= 0 && p.charIndex < (int)game.chars.size()) lqMeasureInto(p, w.feet, 2, w.up ? 0 : 1, 5, spStat[w.up ? 0 : 1]);
        }
        // all there (or a minute gone): the other way, 4 s apart, the last to come first (nearest the way back)
        if (alive > 0 && (there >= alive || tourT - spPhaseT > 60.f)) {
            spUp = !spUp;
            spPhaseT = tourT;
            const int n = (int)spWalkers.size();
            for (int k = 0; k < n; k++) {
                const int order = spThere[k] > 0 ? n - spThere[k] : n - 1;   // (0 = the last there: first away)
                spThere[k] = 0;
                spWalkers[k].started = false;
                spWalkers[k].startAt = tourT + 1.f + 4.f * (float)Clamp(order, 0, n - 1);
            }
            spArrived = 0;
        }
        if (game.requestScreenshot.empty() && tourT > spT0 + 30.f && tourT < spT0 + 30.1f)
            game.requestScreenshot = shotPath("locoplayer_spiral");
    }

