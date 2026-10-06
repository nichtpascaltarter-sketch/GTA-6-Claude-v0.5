    // ---- --autoplay locostairs (locomotion test build only): the street stair of the first SkyLine station (platform
    // side 0) at 8:00 for 100 s, eight people walking it up and down (spawned at its top and its foot, each sent to the
    // other end on arrival, up on the right and down on the left), the camera at its foot. Every planted foot of anyone
    // on a flight of it (the ground 10 cm or more apart 0.3 m before and after the ped) within 25 m of the camera is
    // measured each frame as the locomotion harness does: across a riser (heel and toe tip over treads 3 cm or more
    // apart), sunk (a sole point 1 cm / 3 cm under the tread over it), lifted (all three 15 mm above it); and the slide
    // per step on the flights, as locotour counts it.
    struct LsPed {
        int id = -1;
        u32 uid = 0;
        int up = 0;   // heading up to the top (1) or down to the foot (0)
    };
    struct LsFoot {
        bool in = false;
        vec3 prev[3];
        float slide = 0.f;
        int n = 0, dir = 0;
    };
    std::vector<LsPed> lsPeds;
    std::vector<LsFoot> lsFeet;   // per ped slot, two feet
    std::vector<u32> lsFeetUid;
    vec2 lsDir, lsRight;          // the stair's way down (unit) and its right
    vec3 lsTopC, lsFootC;         // its centreline at the top landing edge and at the foot
    float lsLen = 0.f;
    int lsFrames[2] = {}, lsAcross[2] = {}, lsSunk[2] = {}, lsSunk3[2] = {}, lsLifted[2] = {}, lsScuffs[2] = {}, lsTrips = 0;
    float lsLowest[2] = {1e9f, 1e9f}, lsCpu = 0.f;
    int lsCpuN = 0;
    std::vector<float> lsSlide[2];

    // a goal at the top (up) or the foot, on the side its direction keeps to
    vec3 lsGoal(int up) const {
        float lat = up ? 0.45f : -0.45f;   // (the right of someone going up is the stair's left going down)
        vec3 c = up ? lsTopC - vec3(lsDir.x, lsDir.y, 0.f) * 1.2f : lsFootC + vec3(lsDir.x, lsDir.y, 0.f) * 2.f;
        vec2 r = lsRight * (up ? -lat : lat);
        return vec3(c.x + r.x, c.y + r.y, c.z);
    }

    void lsSend(LsPed& L) {
        Ped& p = game.peds[L.id];
        vec3 g = lsGoal(L.up);
        p.brain.type = BRAIN_GOTO;
        p.brain.goal = dvec3(g.x, g.y, g.z);
        p.brain.speed = 1.2f + 0.2f * hashToFloat(hash32(L.uid * 7u));
    }

    // on a flight of the stair: on its footprint, the ground stepping under the ped
    int lsOnFlight(const Ped& p) const {
        vec3 pp = p.pos.toVec3();
        vec2 d = pp.xy() - lsTopC.xy();
        float a = dot(d, lsDir), l = dot(d, lsRight);
        if (a < 0.3f || a > lsLen - 0.3f || fabsf(l) > 1.f) return -1;
        float g0 = game.groundHeight(pp.x - lsDir.x * 0.3f, pp.y - lsDir.y * 0.3f, pp.z + 0.3f);
        float g1 = game.groundHeight(pp.x + lsDir.x * 0.3f, pp.y + lsDir.y * 0.3f, pp.z - 0.3f);
        if (g0 - g1 < 0.1f) return -1;
        vec2 v(p.vel.x, p.vel.y);
        return dot(v, lsDir) < 0.f ? 0 : 1;   // 0 going up, 1 going down
    }

    void lsMeasure(float dt) {
        const float kDown = 0.008f;
        if (lsFeet.size() < game.peds.size() * 2) {
            lsFeet.resize(game.peds.size() * 2);
            lsFeetUid.resize(game.peds.size(), 0u);
        }
        lsCpu += game.profPeds;
        lsCpuN++;
        for (int i = 0; i < (int)game.peds.size(); i++) {
            const Ped& p = game.peds[i];
            int w = locoEligible(p) && p.visibleDist < 25.f ? lsOnFlight(p) : -1;
            if (lsFeetUid[i] != p.uid || w < 0) {
                lsFeetUid[i] = p.uid;
                lsFeet[i * 2] = lsFeet[i * 2 + 1] = LsFoot();   // (off the flights or out of view: open contacts dropped)
                if (w < 0) continue;
            }
            for (int s = 0; s < 2; s++) {
                vec3 P[3];
                locoFootPoints(p, s, P);
                float gz[3], low = 1e9f;
                bool down[3];
                for (int k = 0; k < 3; k++) {
                    gz[k] = game.groundHeight(P[k].x, P[k].y, P[k].z - 0.25f);
                    float h = gz[k] > -1e8f ? P[k].z - gz[k] : 1.f;
                    down[k] = h < kDown;
                    low = Min(low, h);
                }
                if (p.anim.planted[s]) {
                    lsFrames[w]++;
                    lsLowest[w] = Min(lsLowest[w], low);
                    if (fabsf(gz[0] - gz[2]) > 0.03f) lsAcross[w]++;
                    if (low < -0.01f) lsSunk[w]++;
                    if (low < -0.03f) lsSunk3[w]++;
                    if (low > 0.015f) lsLifted[w]++;
                }
                LsFoot& F = lsFeet[i * 2 + s];
                bool contact = down[0] || down[1] || down[2];
                if (contact) {
                    if (F.in) {
                        float best = 1e9f;
                        for (int k = 0; k < 3; k++)
                            if (down[k]) best = Min(best, length(vec2(P[k].x - F.prev[k].x, P[k].y - F.prev[k].y)));
                        F.slide += best;
                        F.n++;
                    } else {
                        F = LsFoot();
                        F.in = true;
                        F.dir = w;
                    }
                } else if (F.in) {
                    if (F.n < 2) lsScuffs[F.dir]++;
                    else lsSlide[F.dir].push_back(F.slide * 100.f);
                    F.in = false;
                }
                for (int k = 0; k < 3; k++) F.prev[k] = P[k];
            }
        }
    }

    void lsReport() {
        static const char* kW[2] = {"up", "down"};
        for (int w = 0; w < 2; w++) {
            double s = 0.0;
            for (float x : lsSlide[w]) s += x;
            int n = (int)lsSlide[w].size(), f = Max(lsFrames[w], 1);
            LOG("locostairs %-4s steps %4d | slide cm/step mean %.2f p90 %.2f max %.2f | scuffs %d", kW[w], n, n ? s / n : 0.0,
                locoPct(lsSlide[w], 0.9f), locoPct(lsSlide[w], 1.f), lsScuffs[w]);
            LOG("locostairs %-4s planted foot-frames %d | across a riser %.1f%% | sunk 1 cm %.1f%%, 3 cm %.1f%% | lifted %.1f%% | lowest sole %.1f mm", kW[w],
                lsFrames[w], 100.f * lsAcross[w] / f, 100.f * lsSunk[w] / f, 100.f * lsSunk3[w] / f, 100.f * lsLifted[w] / f, lsLowest[w] * 1000.f);
        }
        LOG("locostairs trips %d | peds cpu %.2f ms per frame (mean over %d frames)", lsTrips, lsCpuN ? lsCpu / lsCpuN : 0.f, lsCpuN);
    }

    void updateLocoStairs(Controls& c, float dt) {
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl || !World::gTransit || World::gTransit->metro.stations.empty()) return;
        if (tourStop < 0) {
            tourStop = 0;
            tourT = 0.f;
            const World::MetroStation& st = World::gTransit->metro.stations[0];
            const int side = 0, E = st.exitEnd[side];
            const float S = side == 0 ? 1.f : -1.f, H = World::transit_dims::kPlatformHalfLen;
            // the stair runs from the landing edge (along E (H - 3.4)) down towards the station's middle
            lsTopC = st.local(E * (H - 3.4f), S * 9.4f, st.platformZ());
            lsDir = st.dir * (float)-E;
            lsRight = vec2(lsDir.y, -lsDir.x);
            lsTopC.z = game.groundHeight(lsTopC.x - lsDir.x * 0.3f, lsTopC.y - lsDir.y * 0.3f, lsTopC.z + 0.3f);
            // its foot: down the centreline until the ground stops stepping down
            float z = lsTopC.z, a = 0.f, flat = 0.f;
            for (a = 0.05f; a < 60.f; a += 0.05f) {
                vec2 q = lsTopC.xy() + lsDir * a;
                float g = game.groundHeight(q.x, q.y, z + 0.1f);
                flat = g < z - 0.05f ? 0.f : flat + 0.05f;
                z = Min(z, g);
                if (flat > 2.5f && z < st.streetZ + 0.6f) break;
            }
            lsLen = a - flat;
            vec2 fq = lsTopC.xy() + lsDir * lsLen;
            lsFootC = vec3(fq.x, fq.y, game.groundHeight(fq.x, fq.y, z + 0.3f));
            if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
            // the player (the camera) on the ground 2.5 m past the foot and 2 m to the side, facing up the stair
            vec2 pp = fq + lsDir * 2.5f + lsRight * 2.f;
            pl->pos = dvec3(pp.x, pp.y, game.groundHeight(pp.x, pp.y, lsFootC.z + 1.f));
            pl->vel = vec3(0.f);
            pl->yaw = atan2f(lsDir.x, -lsDir.y);
            pl->invincible = true;
            game.rig.yaw = pl->yaw;
            game.rig.pitch = 0.12f;
            game.rig.cut = true;
            game.hudVisible = false;
            env.timeOfDay = 8.f;
            weather.setImmediate(WX_FAIR);
            game.populationWarmup = 2.5f;
            game.pinfo.wanted = 0;
            locoRenderEvery = renderEvery;
            LOG("locostairs: station %s side %d, stair top %.1f %.1f %.1f, foot %.1f %.1f %.1f, %.1f m long", st.name.c_str(), side, lsTopC.x, lsTopC.y,
                lsTopC.z, lsFootC.x, lsFootC.y, lsFootC.z, lsLen);
        }
        tourT += dt;
        env.timeOfDay = 8.f;
        game.pinfo.wanted = 0;
        c = Controls();
        if (tourT > 3.f && lsPeds.empty()) {
            // eight walkers: four on the top of the stair heading down, four past its foot heading up, 2.2 m apart
            for (int k = 0; k < 8; k++) {
                int up = k & 1;
                vec3 g = lsGoal(1 - up) + vec3(lsDir.x, lsDir.y, 0.f) * (2.2f * (float)(k / 2) + (up ? 0.f : 1.2f));
                g.z = game.groundHeight(g.x, g.y, g.z + 1.f);
                int id = Transit::tg::spawnCivilian(game, hash32(9100u + (u32)k * 77u), g, atan2f(lsDir.x * (up ? 1.f : -1.f), -lsDir.y * (up ? 1.f : -1.f)));
                if (id < 0) continue;
                LsPed L;
                L.id = id;
                L.uid = game.peds[id].uid;
                L.up = up;
                lsSend(L);
                lsPeds.push_back(L);
            }
            LOG("locostairs: %d walkers", (int)lsPeds.size());
        }
        for (LsPed& L : lsPeds) {
            if (L.id < 0 || L.id >= (int)game.peds.size() || !game.peds[L.id].used || game.peds[L.id].uid != L.uid) continue;
            Ped& p = game.peds[L.id];
            vec3 g = lsGoal(L.up);
            if (length(p.pos.toVec3().xy() - g.xy()) < 0.8f) {
                L.up = 1 - L.up;
                lsTrips++;
            }
            if (p.state == PS_ONFOOT && p.health > 0.f) lsSend(L);
        }
        if (tourT > 10.f) lsMeasure(dt);
        if (tourT > 110.f) {
            lsReport();
            tourDone = true;
            game.rig.scriptActive = false;
            renderEvery = locoRenderEvery;
        }
    }

