    // ---- --autoplay locoplayer (locomotion test build only): the player on stairs. The metro station's straight flight
    // (found as locostairs finds it) and a test-only switchback beside its foot (two flights of eight 17.5 x 28 cm treads,
    // 1.1 m wide, nine risers each, a 1.1 m landing between them and a platform at the top; a wall between the flights and
    // round the outside; collision boxes only - nothing drawn - added as a cell of their own) are walked (stick 0.49,
    // about 1.5 m/s), run (stick 1, 4.1 m/s) and sprinted (6.9 m/s), up and down, LOCO_PLAYERLAPS times each (default 2).
    // The player's feet on the flights and the landing are measured as locostairs measures walkers': planted frames sunk
    // 1 / 3 cm into a step or lifted 1.5 cm off it, the slide per step, scuffs. Running and sprinting the straight flight,
    // two people chase the player up and down it (BRAIN_GOTO at 3 and 5 m/s, 3 and 6 m behind on the other side), their
    // feet measured too. LOCO_NPCSWITCH=n: then n people (at most 4) walk the switchback up and down for two minutes at
    // the stair pace (LOCO_STAIRPACE, default 0.5 m/s; each keeping to its right, 6 s apart), their feet measured there.
    struct LqStat {
        std::vector<float> slide;
        int frames = 0, sunk = 0, sunk3 = 0, lifted = 0, scuffs = 0, deep = 0;
        float lowest = 1e9f;
    };
    struct LqFoot {
        bool in = false;
        vec3 prev[3];
        float slide = 0.f;
        int n = 0, bucket = -1;
    };
    struct LqSeg {
        int stair, up, gait;   // stair 0 straight, 1 switchback; gait 0 walk, 1 run, 2 sprint
    };
    LqStat lqStat[2][2][5];   // [stair][0 up, 1 down][gait 0-2 the player, 3 / 4 the chasers at 3 / 5 m/s]
    LqFoot lqFeet[3][2];      // the player, the two chasers
    std::vector<LqSeg> lqSegs;
    int lqSeg = -1, lqPhase = 0;   // 0 placed, settling; 1 under way; 2 arrived
    float lqT = 0.f, lqArrT = 0.f, lqDist = 0.f;
    std::vector<vec2> lqPath;
    std::vector<float> lqAcc;
    float lqNear = 0.f;
    bool lqSiteOk = false, lqStairOk = false;
    vec2 lqO, lqA, lqX;   // the switchback: the foot of flight 1's outer corner line (world xy), along its flights, across
    float lqZ0 = 0.f;
    int lqChaser[2] = {-1, -1};
    u32 lqChaserUid[2] = {0u, 0u};
    int lqDeepLog = 0;
    vec2 lqPrevPos;
    struct LqWalker {
        int id = -1;
        u32 uid = 0;
        int up = 1;
        float sNear = 0.f, waitUntil = -1.f, startAt = 0.f;
        bool started = false;
        LqFoot feet[2];
    };
    std::vector<LqWalker> lqWalkers;
    std::vector<vec2> lqNpcPath[2];   // [0 down, 1 up]: each keeping to its right
    std::vector<float> lqNpcAcc[2];
    LqStat lqNpcStat[2];              // [0 up, 1 down]
    float lqNpcT0 = -1.f;
    static vec2 lqPathAt(const std::vector<vec2>& P, const std::vector<float>& A, float s) {
        s = Clamp(s, 0.f, A.back());
        size_t i = 1;
        while (i + 1 < P.size() && A[i] < s) i++;
        const float seg = Max(A[i] - A[i - 1], 1e-4f);
        return lerp(P[i - 1], P[i], Saturate((s - A[i - 1]) / seg));
    }
    static float lqPathNear(const std::vector<vec2>& P, const std::vector<float>& A, vec2 q, float sNear) {
        float best = 1e9f, bs = sNear;
        for (size_t i = 1; i < P.size(); i++) {
            if (A[i] < sNear - 0.01f) continue;
            const vec2 ab = P[i] - P[i - 1];
            const float L2 = Max(dot(ab, ab), 1e-8f);
            const float sA = Max(A[i - 1] + Saturate(dot(q - P[i - 1], ab) / L2) * sqrtf(L2), sNear);
            const float d = length(lqPathAt(P, A, sA) - q);
            if (d < best - 1e-4f) best = d, bs = sA;
        }
        return Max(sNear, bs);
    }
    static constexpr float kLqR = 0.175f, kLqTr = 0.28f, kLqW = 1.1f, kLqLd = 1.1f;
    static constexpr int kLqN = 8;

    vec2 lqW(float x, float a) const { return lqO + lqX * x + lqA * a; }
    float lqTop() const { return (2 * kLqN + 2) * kLqR; }   // the top platform's height above lqZ0

    // a flat, open spot beside the stair's foot for the switchback (5 m x 6 m, no road lane and no collider within 6 m,
    // the ground level to 4 cm)
    bool lqFindSite() {
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 12; j++) {
                const float along = 4.f + 3.f * (float)i, across = (j & 1 ? 1.f : -1.f) * (4.f + 3.f * (float)(j / 2));
                const vec2 c = lsFootC.xy() + lsDir * along + lsRight * across;
                const float z = game.groundHeight(c.x, c.y, lsFootC.z + 2.f);
                if (z < -1e8f || fabsf(z - lsFootC.z) > 2.5f) continue;
                bool ok = true;
                for (int k = 0; k < 15 && ok; k++) {   // (along -3.3 .. 3.3 m, across -1.5 .. 1.5 m: the whole of it)
                    const vec2 q = c + lsDir * (1.65f * (float)(k % 5 - 2)) + lsRight * (1.5f * (float)(k / 5 - 1));
                    const float zq = game.groundHeight(q.x, q.y, z + 1.f);
                    ok = zq > -1e8f && fabsf(zq - z) < 0.04f;
                }
                if (!ok) continue;
                float u = 0.f;
                if (game.laneGraph.nearestLane(c, vec2(0.f), 6.f, &u) >= 0) continue;
                std::vector<int> nearby;
                collision.collidersNear(c, 6.f, nearby);
                if (!nearby.empty()) continue;
                // (the switchback's flights run along lsDir; it reaches 3.1 m back from the foot of flight 1, under the top
                // platform and the way to the flight, and 3.5 m on to the landing's end wall: centred on the spot)
                lqA = lsDir;
                lqX = vec2(lqA.y, -lqA.x);
                lqO = c - lqA * 0.17f;
                lqZ0 = z;
                return true;
            }
        return false;
    }

    void lqBuildSwitchback() {
        std::vector<World::CollisionBox> boxes;
        auto add = [&](float x0, float x1, float a0, float a1, float z0, float z1) {
            World::CollisionBox b;
            const vec2 c = lqW(0.5f * (x0 + x1), 0.5f * (a0 + a1));
            b.c = vec3(c.x, c.y, lqZ0 + 0.5f * (z0 + z1));
            b.ax = lqX;
            b.he = vec3(0.5f * (x1 - x0), 0.5f * (a1 - a0), 0.5f * (z1 - z0));
            boxes.push_back(b);
        };
        const float aL = kLqN * kLqTr, zL = (kLqN + 1) * kLqR, zTop = lqTop();
        for (int k = 1; k <= kLqN; k++) add(-kLqW, 0.f, (k - 1) * kLqTr, k * kLqTr, -0.3f, k * kLqR);   // flight 1, up along +a
        add(-kLqW, kLqW, aL, aL + kLqLd, -0.3f, zL);                                                   // the landing
        for (int j = 1; j <= kLqN; j++) add(0.f, kLqW, aL - j * kLqTr, aL - (j - 1) * kLqTr, -0.3f, zL + j * kLqR);   // flight 2, up along -a
        add(0.f, kLqW, -3.f, 0.f, -0.3f, zTop);   // the top platform, 3 m deep
        // walls: between the flights and along the platform's inner side, along both outer sides, round the landing's end
        // and the platform's
        add(-0.06f, 0.06f, -3.f, aL, -0.3f, zTop + 1.1f);
        add(-kLqW - 0.12f, -kLqW, -0.1f, aL + kLqLd + 0.12f, -0.3f, zL + 2.2f);
        add(kLqW, kLqW + 0.12f, -3.12f, aL + kLqLd + 0.12f, -0.3f, zTop + 1.1f);
        add(-kLqW - 0.12f, kLqW + 0.12f, aL + kLqLd, aL + kLqLd + 0.12f, -0.3f, zL + 2.2f);
        add(-0.06f, kLqW + 0.12f, -3.12f, -3.f, -0.3f, zTop + 1.1f);
        collision.addCell(0x6E0F0F0F, boxes, std::vector<World::PropInstance>());
        LOG("locoplayer: switchback at %.1f %.1f %.2f, flights along %.2f %.2f (%d boxes)", lqO.x, lqO.y, lqZ0, lqA.x, lqA.y, (int)boxes.size());
    }

    // the walking line of a segment (world xy) and where it starts (z: the ground there)
    void lqMakePath(const LqSeg& g, vec3& start) {
        lqPath.clear();
        if (g.stair == 0) {
            const vec2 foot = lsFootC.xy() + lsDir * 4.f, top = lsTopC.xy() - lsDir * 1.5f;
            const vec2 side = lsRight * (g.up ? -0.45f : 0.45f);
            if (g.up) lqPath = {foot + side, top + side};
            else lqPath = {top + side, foot + side};
        } else {
            const float aT = kLqN * kLqTr + 0.55f;
            std::vector<vec2> up = {lqW(-0.55f, -3.f), lqW(-0.55f, aT), lqW(0.55f, aT), lqW(0.55f, -1.6f)};
            if (!g.up) std::reverse(up.begin(), up.end());
            lqPath = up;
        }
        lqAcc.assign(lqPath.size(), 0.f);
        for (size_t i = 1; i < lqPath.size(); i++) lqAcc[i] = lqAcc[i - 1] + length(lqPath[i] - lqPath[i - 1]);
        lqNear = 0.f;
        const vec2 s0 = lqPath[0];
        const float zr = g.stair == 1 ? (g.up ? lqZ0 : lqZ0 + lqTop()) : (g.up ? lsFootC.z : lsTopC.z);
        start = vec3(s0.x, s0.y, game.groundHeight(s0.x, s0.y, zr + 0.6f));
    }
    vec2 lqAt(float s) const {
        s = Clamp(s, 0.f, lqAcc.back());
        size_t i = 1;
        while (i + 1 < lqPath.size() && lqAcc[i] < s) i++;
        const float seg = Max(lqAcc[i] - lqAcc[i - 1], 1e-4f);
        return lerp(lqPath[i - 1], lqPath[i], Saturate((s - lqAcc[i - 1]) / seg));
    }
    float lqNearest(vec2 q) {
        float best = 1e9f, bs = lqNear;
        for (size_t i = 1; i < lqPath.size(); i++) {
            if (lqAcc[i] < lqNear - 0.01f) continue;
            const vec2 ab = lqPath[i] - lqPath[i - 1];
            const float L2 = Max(dot(ab, ab), 1e-8f);
            const float u = Saturate(dot(q - lqPath[i - 1], ab) / L2);
            const float sA = Max(lqAcc[i - 1] + u * sqrtf(L2), lqNear);
            const float d = length(lqAt(sA) - q);
            if (d < best - 1e-4f) best = d, bs = sA;
            if (lqAcc[i - 1] > lqNear + 1.5f) break;
        }
        lqNear = Max(lqNear, bs);
        return lqNear;
    }

    // on the measured part: the straight flight's footprint (as lsOnFlight), the switchback's flights and landing
    bool lqOn(int stair, const Ped& p) const {
        const vec3 pp = p.pos.toVec3();
        if (stair == 0) {
            const vec2 d = pp.xy() - lsTopC.xy();
            const float a = dot(d, lsDir), l = dot(d, lsRight);
            if (a < 0.3f || a > lsLen - 0.3f || fabsf(l) > 1.f) return false;
            const float g0 = game.groundHeight(pp.x - lsDir.x * 0.3f, pp.y - lsDir.y * 0.3f, pp.z + 0.3f);
            const float g1 = game.groundHeight(pp.x + lsDir.x * 0.3f, pp.y + lsDir.y * 0.3f, pp.z - 0.3f);
            return g0 - g1 > 0.1f;
        }
        const vec2 d = pp.xy() - lqO;
        const float x = dot(d, lqX), a = dot(d, lqA);
        return fabsf(x) < kLqW + 0.05f && a > -0.3f && a < kLqN * kLqTr + kLqLd && !(x > 0.f && a < -0.05f);
    }

    void lqMeasurePed(const Ped& p, LqFoot* F, int bucketStair, int bucketUp, int gait) { lqMeasureInto(p, F, bucketStair, bucketUp, gait, lqStat[bucketStair][bucketUp][gait]); }
    // (gait 5: the walkers on the switchback)
    void lqMeasureInto(const Ped& p, LqFoot* F, int bucketStair, int bucketUp, int gait, LqStat& S) {
        if (p.charIndex < 0 || p.charIndex >= (int)game.chars.size()) return;
        const bool on = lqOn(bucketStair, p);
        for (int s = 0; s < 2; s++) {
            LqFoot& f = F[s];
            if (!on) {
                if (f.in && f.bucket >= 0) {
                    if (f.n < 2) S.scuffs++;
                    else S.slide.push_back(f.slide * 100.f);
                }
                f = LqFoot();
                continue;
            }
            vec3 P[3];
            locoFootPoints(p, s, P);
            float low = 1e9f;
            bool down[3];
            float hh[3];
            for (int k = 0; k < 3; k++) {
                const float gz = game.groundHeight(P[k].x, P[k].y, P[k].z - 0.25f);
                hh[k] = gz > -1e8f ? P[k].z - gz : 1.f;
                down[k] = hh[k] < 0.008f;
                low = Min(low, hh[k]);
            }
            if (p.anim.planted[s]) {
                S.frames++;
                S.lowest = Min(S.lowest, low);
                if (low < -0.01f) S.sunk++;
                if (low < -0.03f) S.sunk3++;
                if (low > 0.015f) S.lifted++;
                if (low < -0.12f) {
                    S.deep++;
                    if (lqDeepLog < 300) {
                        lqDeepLog++;
                        const Anim::Animator& A = p.anim;
                        LOG("locoplayer deep %s %s %s foot %d: h%6.1f b%6.1f t%6.1f cm | v%5.2f | age%.2f ballProbe %d stairLand %.2f stepT %.2f lead %.3f",
                            bucketStair ? "switchback" : "straight", bucketUp ? "down" : "up", gait == 0 ? "walk" : gait == 1 ? "run" : gait == 2 ? "sprint" : gait == 3 ? "chase3" : gait == 4 ? "chase5" : "npc",
                            s, hh[0] * 100.f, hh[1] * 100.f, hh[2] * 100.f, length(vec2(p.vel.x, p.vel.y)), A.plantAge[s], (int)A.ballProbe[s], A.stairLand[s], A.stepT[s],
                            A.strikeLead);
                    }
                }
            }
            const bool contact = down[0] || down[1] || down[2];
            if (contact) {
                if (f.in) {
                    float best = 1e9f;
                    for (int k = 0; k < 3; k++)
                        if (down[k]) best = Min(best, length(vec2(P[k].x - f.prev[k].x, P[k].y - f.prev[k].y)));
                    f.slide += best;
                    f.n++;
                } else {
                    f = LqFoot();
                    f.in = true;
                    f.bucket = 0;
                }
            } else if (f.in) {
                if (f.n < 2) S.scuffs++;
                else S.slide.push_back(f.slide * 100.f);
                if (f.n >= 2 && f.slide > 0.15f)
                    LOG("locoplayer bigslide %s %s %s foot %d: %.1f cm over %d frames | planted %d stepT %.2f", bucketStair ? "switchback" : "straight",
                        bucketUp ? "down" : "up", gait == 0 ? "walk" : gait == 1 ? "run" : gait == 2 ? "sprint" : gait == 3 ? "chase3" : gait == 4 ? "chase5" : "npc", s, f.slide * 100.f, f.n,
                        (int)p.anim.planted[s], p.anim.stepT[s]);
                f.in = false;
            }
            for (int k = 0; k < 3; k++) f.prev[k] = P[k];
        }
    }

    void lqReport() {
        for (int u = 0; u < 2; u++) {
            const LqStat& S = lqNpcStat[u];
            if (S.frames == 0 && S.slide.empty()) continue;
            double sum = 0.0;
            for (float x : S.slide) sum += x;
            const int n = (int)S.slide.size(), f = Max(S.frames, 1);
            LOG("locoplayer switchback %-4s npc    steps %3d | slide cm/step mean %.2f p90 %.2f max %.2f | scuffs %d | planted foot-frames %d | sunk 1 cm %.1f%%, 3 cm %.1f%% | "
                "lifted %.1f%% | deep %d | lowest sole %.1f mm",
                u ? "down" : "up", n, n ? sum / n : 0.0, locoPct(S.slide, 0.9f), locoPct(S.slide, 1.f), S.scuffs, S.frames, 100.f * S.sunk / f, 100.f * S.sunk3 / f,
                100.f * S.lifted / f, S.deep, S.lowest < 1e8f ? S.lowest * 1000.f : 0.f);
        }
        static const char* kG[5] = {"walk", "run", "sprint", "chase3", "chase5"};
        for (int st = 0; st < 2; st++)
            for (int u = 0; u < 2; u++)
                for (int g = 0; g < 5; g++) {
                    const LqStat& S = lqStat[st][u][g];
                    if (S.frames == 0 && S.slide.empty()) continue;
                    double sum = 0.0;
                    for (float x : S.slide) sum += x;
                    const int n = (int)S.slide.size(), f = Max(S.frames, 1);
                    LOG("locoplayer %-10s %-4s %-6s steps %3d | slide cm/step mean %.2f p90 %.2f max %.2f | scuffs %d | planted foot-frames %d | sunk 1 cm %.1f%%, 3 cm %.1f%% | "
                        "lifted %.1f%% | deep %d | lowest sole %.1f mm",
                        st ? "switchback" : "straight", u ? "down" : "up", kG[g], n, n ? sum / n : 0.0, locoPct(S.slide, 0.9f), locoPct(S.slide, 1.f), S.scuffs, S.frames,
                        100.f * S.sunk / f, 100.f * S.sunk3 / f, 100.f * S.lifted / f, S.deep, S.lowest < 1e8f ? S.lowest * 1000.f : 0.f);
                }
    }

    bool lqChaserAlive(int k) const {
        const int id = lqChaser[k];
        return id >= 0 && id < (int)game.peds.size() && game.peds[id].used && game.peds[id].uid == lqChaserUid[k] && game.peds[id].state == PS_ONFOOT;
    }

    // the people on the switchback: the player out of the way, n walkers at its foot, each starting up 6 s after the last
    void lqStartNpcs(Ped& pl, int n) {
        const float aT = kLqN * kLqTr + 0.55f, o = 0.18f;
        lqNpcPath[1] = {lqW(-0.55f + o, -3.f), lqW(-0.55f + o, aT - o), lqW(0.55f - o, aT - o), lqW(0.55f - o, -1.6f)};
        lqNpcPath[0] = {lqW(0.55f + o, -1.6f), lqW(0.55f + o, aT + o), lqW(-0.55f - o, aT + o), lqW(-0.55f - o, -3.f)};
        for (int d = 0; d < 2; d++) {
            lqNpcAcc[d].assign(lqNpcPath[d].size(), 0.f);
            for (size_t i = 1; i < lqNpcPath[d].size(); i++) lqNpcAcc[d][i] = lqNpcAcc[d][i - 1] + length(lqNpcPath[d][i] - lqNpcPath[d][i - 1]);
        }
        const vec2 park = lqW(-3.5f, -4.f);
        pl.pos = dvec3(park.x, park.y, game.groundHeight(park.x, park.y, lqZ0 + 1.f));
        pl.vel = vec3(0.f);
        pl.yaw = AI::dirYaw(lqA);
        game.rig.yaw = pl.yaw;
        game.rig.cut = true;
        for (int k = 0; k < 2; k++)
            if (lqChaserAlive(k)) game.despawnPed(lqChaser[k]);
        for (int k = 0; k < n; k++) {
            LqWalker w;
            const vec2 q = lqNpcPath[1][0] - lqA * (1.2f * (float)k) - lqX * (0.9f * (float)(k % 2));
            const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, lqZ0 + 1.f));
            w.id = Transit::tg::spawnCivilian(game, hash32(9500u + (u32)k * 53u), qz, AI::dirYaw(lqA));
            if (w.id < 0) continue;
            w.uid = game.peds[w.id].uid;
            w.startAt = tourT + 2.f + 6.f * (float)k;
            game.peds[w.id].brain.type = BRAIN_GOTO;
            game.peds[w.id].brain.goal = game.peds[w.id].pos;
            game.peds[w.id].invincible = true;
            lqWalkers.push_back(w);
        }
        lqNpcT0 = tourT;
        LOG("locoplayer: %d walkers on the switchback", (int)lqWalkers.size());
    }
    void lqUpdateNpcs() {
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
            if (A.back() - w.sNear < 0.4f) {   // (there: a moment, then back the other way)
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
    }
    void lqMeasureNpc(const Ped& p, LqFoot* F, int bucketUp) { lqMeasureInto(p, F, 1, bucketUp, 5, lqNpcStat[bucketUp]); }

    void updateLocoPlayer(Controls& c, float dt) {
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl || !World::gTransit || World::gTransit->metro.stations.empty()) return;
        const World::MetroStation& st = World::gTransit->metro.stations[0];
        if (tourStop < 0) {
            tourStop = 0;
            tourT = 0.f;
            const int side = 0, E = st.exitEnd[side];
            const float S = side == 0 ? 1.f : -1.f, H = World::transit_dims::kPlatformHalfLen;
            lsTopC = st.local(E * (H - 3.4f), S * 9.4f, st.platformZ());
            lsDir = st.dir * (float)-E;
            lsRight = vec2(lsDir.y, -lsDir.x);
            lsLen = 0.f;
            if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
            const vec2 pp = lsTopC.xy() + lsDir * 34.f + lsRight * 2.f;
            pl->pos = dvec3(pp.x, pp.y, game.groundHeight(pp.x, pp.y, st.streetZ + 1.f));
            pl->vel = vec3(0.f);
            pl->yaw = atan2f(lsDir.x, -lsDir.y);
            pl->invincible = true;
            game.rig.yaw = pl->yaw;
            game.rig.pitch = 0.12f;
            game.rig.cut = true;
            game.hudVisible = false;
            env.timeOfDay = 9.f;
            weather.setImmediate(WX_FAIR);
            game.populationOff = true;
            game.pinfo.wanted = 0;
            locoRenderEvery = renderEvery;
            static const int kLaps = getenv("LOCO_PLAYERLAPS") ? Max(1, atoi(getenv("LOCO_PLAYERLAPS"))) : 2;
            for (int lap = 0; lap < kLaps; lap++)
                for (int g = 0; g < 3; g++)
                    for (int stc = 0; stc < 2; stc++)
                        for (int up = 1; up >= 0; up--) lqSegs.push_back({stc, up, g});
        }
        tourT += dt;
        env.timeOfDay = 9.f;
        game.pinfo.wanted = 0;
        game.pinfo.stamina = 1.f;
        c = Controls();
        // (the people already about when the population went off: away)
        if (tourT < 2.f)
            for (int i = 0; i < (int)game.peds.size(); i++)
                if (i != game.player && game.peds[i].used && game.peds[i].state == PS_ONFOOT && length(rel(game.peds[i].pos, pl->pos)) < 80.f) game.despawnPed(i);
        if (!lqStairOk) {
            if (tourT > 4.f && fmodf(tourT, 1.f) < dt) {
                if (lsMeasureStair()) {
                    lqStairOk = true;
                    lqSiteOk = lqFindSite();
                    if (lqSiteOk) lqBuildSwitchback();
                    else LOG("locoplayer: no open flat spot for the switchback beside the stair's foot: the straight flight only");
                    LOG("locoplayer: station %s, stair top %.1f %.1f %.1f, foot %.1f %.1f %.1f, %.1f m long", st.name.c_str(), lsTopC.x, lsTopC.y, lsTopC.z, lsFootC.x,
                        lsFootC.y, lsFootC.z, lsLen);
                } else if (tourT > 40.f) {
                    LOG("locoplayer: no stair found at station %s", st.name.c_str());
                    tourDone = true;
                    game.rig.scriptActive = false;
                    renderEvery = locoRenderEvery;
                }
            }
            return;
        }
        // the next segment: the player placed at its start, facing along it, a moment to settle
        if (lqSeg < 0 || lqPhase == 2) {
            if (lqSeg >= 0 && lqPhase == 2 && tourT - lqArrT < 0.6f) return;
            do lqSeg++;
            while (lqSeg < (int)lqSegs.size() && lqSegs[lqSeg].stair == 1 && !lqSiteOk);
            if (lqSeg >= (int)lqSegs.size()) {
                static const int kNpc = getenv("LOCO_NPCSWITCH") ? Clamp(atoi(getenv("LOCO_NPCSWITCH")), 0, 4) : 0;
                if (kNpc > 0 && lqSiteOk && lqNpcT0 < 0.f) lqStartNpcs(*pl, kNpc);
                if (lqNpcT0 >= 0.f && tourT < lqNpcT0 + 125.f) {
                    lqSeg = (int)lqSegs.size() - 1;   // (stay here while they walk)
                    lqUpdateNpcs();
                    return;
                }
                lqReport();
                LOG("locoplayer done");
                tourDone = true;
                game.rig.scriptActive = false;
                renderEvery = locoRenderEvery;
                return;
            }
            const LqSeg& g = lqSegs[lqSeg];
            vec3 s0;
            lqMakePath(g, s0);
            const vec2 d0 = normalize(lqPath[1] - lqPath[0]);
            pl->pos = dvec3(s0.x, s0.y, s0.z);
            pl->vel = vec3(0.f);
            pl->yaw = AI::dirYaw(d0);
            game.rig.yaw = pl->yaw;
            game.rig.cut = true;
            for (int s = 0; s < 2; s++) lqFeet[0][s] = LqFoot();
            // the chasers: on the straight flight run or sprinted, 3 and 6 m behind the player on the other side
            if (g.stair == 0 && g.gait >= 1) {
                for (int k = 0; k < 2; k++) {
                    const vec2 q = lqPath[0] - d0 * (3.f + 3.f * (float)k) - lsRight * (g.up ? -0.9f : 0.9f);
                    const vec3 qz(q.x, q.y, game.groundHeight(q.x, q.y, s0.z + 0.6f));
                    if (!lqChaserAlive(k)) {
                        lqChaser[k] = Transit::tg::spawnCivilian(game, hash32(9300u + (u32)k * 71u), qz, AI::dirYaw(d0));
                        lqChaserUid[k] = lqChaser[k] >= 0 ? game.peds[lqChaser[k]].uid : 0u;
                    }
                    if (!lqChaserAlive(k)) continue;
                    Ped& q2 = game.peds[lqChaser[k]];
                    q2.pos = dvec3(qz.x, qz.y, qz.z);
                    q2.vel = vec3(0.f);
                    q2.yaw = AI::dirYaw(d0);
                    q2.brain.type = BRAIN_GOTO;
                    const vec2 e = lqPath.back() - lsRight * (g.up ? -0.9f : 0.9f);
                    q2.brain.goal = dvec3(e.x, e.y, g.up ? lsTopC.z : lsFootC.z);
                    q2.brain.speed = k == 0 ? 3.f : 5.f;
                    q2.invincible = true;
                    for (int s = 0; s < 2; s++) lqFeet[1 + k][s] = LqFoot();
                }
            } else {
                // (otherwise out of the way, standing)
                for (int k = 0; k < 2; k++)
                    if (lqChaserAlive(k)) {
                        Ped& q2 = game.peds[lqChaser[k]];
                        const vec2 q = lsFootC.xy() + lsDir * 12.f + lsRight * (5.f + 1.2f * (float)k);
                        q2.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, lsFootC.z + 1.f));
                        q2.vel = vec3(0.f);
                        q2.brain.type = BRAIN_GOTO;
                        q2.brain.goal = q2.pos;
                    }
            }
            lqPhase = 0;
            lqT = 0.f;
            lqDist = 0.f;
            lqPrevPos = s0.xy();
            return;
        }
        const LqSeg& g = lqSegs[lqSeg];
        lqT += dt;
        if (lqPhase == 0) {
            if (lqT > 1.4f) lqPhase = 1, lqT = 0.f;
            return;
        }
        // under way: steered along the walking line (the point 0.7 m - running 1.1 m - further along it)
        const vec2 pp = pl->pos.toVec3().xy();
        lqDist += length(pp - lqPrevPos);
        lqPrevPos = pp;
        const float sN = lqNearest(pp), left = lqAcc.back() - sN;
        const vec2 aim = lqAt(sN + (g.gait == 0 ? 0.7f : 1.1f));
        if (left > 0.35f && lqT < lqAcc.back() / (g.gait == 0 ? 1.2f : 3.f) + 10.f) {
            game.rig.yaw = AI::dirYaw(aim - pp + vec2(1e-5f, 0.f));
            c.move = vec2(0.f, g.gait == 0 ? 0.49f : 1.f);
            c.sprint.down = g.gait == 2;
        } else {
            lqPhase = 2;
            lqArrT = tourT;
            static const char* kG[3] = {"walk", "run", "sprint"};
            LOG("locoplayer seg %d: %s %s %s, %.1f s, %.1f m, %.2f m/s", lqSeg, g.stair ? "switchback" : "straight", g.up ? "up" : "down", kG[g.gait], lqT, lqDist,
                lqT > 0.f ? lqDist / lqT : 0.f);
        }
        // a shot of the player in the middle of each of the first lap's segments
        if (lqSeg < (int)lqSegs.size() / 2 && fabsf(sN - 0.5f * lqAcc.back()) < 0.3f && game.requestScreenshot.empty() && lqT > 0.3f)
            game.requestScreenshot = shotPath(StrFormat("locoplayer_%02d_%s_%s_%d", lqSeg, g.stair ? "switchback" : "straight", g.up ? "up" : "down", g.gait));
        // measured: the player's feet, and the chasers' on the straight flight
        lqMeasurePed(*pl, lqFeet[0], g.stair, g.up ? 0 : 1, g.gait);
        if (g.stair == 0 && g.gait >= 1)
            for (int k = 0; k < 2; k++)
                if (lqChaserAlive(k)) lqMeasurePed(game.peds[lqChaser[k]], lqFeet[1 + k], 0, g.up ? 0 : 1, 3 + k);
    }

