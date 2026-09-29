// Mission debugging and automated tests.
//   --mission <id>        start a new game directly in that mission (story flags before it are set, the required
//                         protagonist is selected and the player is moved next to the start).
//   --missiontest <ids>   same, then drives each mission with scripted teleports/controls (Mission::autotest), logs
//                         every stage transition, saves screenshots at key moments (--shotdir) and quits when done.
//                         <ids> is a mission id, a comma separated list, "story", "act1".."act3", "side" or "all".
//   --approach loud|quiet heist approach for the Act 3 missions (default quiet).
#include "missions.h"
#include <cstdarg>

namespace Game {

namespace mtest_detail {

struct TestRun {
    bool parsed = false;
    bool started = false;
    std::vector<std::string> queue;
    int index = -1;
    bool running = false;
    bool waitingEnd = false;
    float endTimer = 0.f;
    float cutsceneTime = 0.f;
    int lastShotIndex = -1;
    float shotDelay = -1.f;
    std::string shotTag;
    int passed = 0, failed = 0;
    std::vector<std::string> results;
    std::string single;       // --mission (play) id
    bool playOnly = false;
    float quitTimer = -1.f;
    int pendingShotFrames = 0;
    std::string pendingShotPath;
    float setupDelay = 0.f;
    float timeLimit = 480.f;
};
TestRun gRun;

std::string shotDir() {
    const char* d = Platform::argValue("shotdir");
    return d ? std::string(d) : std::string("Z:\\tmp\\story\\");
}

}  // namespace mtest_detail

using namespace mtest_detail;

// ------------------------------------------------------------------------------------------------------------------
// MissionTest actions
void MissionTest::teleport(vec3 p, float yaw) {
    GameWorld& G = *g;
    int pv = G.playerVehicle();
    if (pv >= 0 && G.peds[G.player].seat == 0) {
        float z = G.isBoat(pv) || G.isAircraft(pv) ? p.z : mu::groundAt(G, p.x, p.y, p.z + 3.f);
        mu::teleportVehicle(G, pv, vec3(p.x, p.y, z), yaw);
    } else {
        if (pv >= 0) G.removePedFromVehicle(G.player, false);
        mu::placePlayer(G, p, yaw);
    }
    G.rig.cut = true;
}

void MissionTest::teleportNear(vec2 p, float dist) {
    GameWorld& G = *g;
    vec2 from = mu::playerPos(G).xy();
    vec2 dir = length(from - p) > 0.5f ? normalize(from - p) : vec2(1, 0);
    vec2 q = p + dir * dist;
    int pv = G.playerVehicle();
    if (pv >= 0 && G.isBoat(pv)) {
        vec3 w;
        mu::findWater(G, q, 1.5f, w, 200.f);
        teleport(w, mu::yawTo(w.xy(), p));
        return;
    }
    if (pv >= 0 && G.isAircraft(pv)) {
        teleport(vec3(q, mu::groundAt(G, q.x, q.y) + 40.f), mu::yawTo(q, p));
        return;
    }
    vec3 at(q, mu::groundAt(G, q.x, q.y, mu::playerPos(G).z + 50.f));
    teleport(at, mu::yawTo(q, p));
}

void MissionTest::enter(int veh, int seat) {
    if (veh < 0 || !g->vehicles[veh].used) return;
    g->warpPedIntoVehicle(g->player, veh, seat);
    g->vehicles[veh].playerUsed = true;
}

void MissionTest::exitVehicle() {
    if (g->playerVehicle() >= 0) g->removePedFromVehicle(g->player, false);
}

void MissionTest::killEnemies(float radius) {
    GameWorld& G = *g;
    vec3 pp = mu::playerPos(G);
    for (int id : gMissions.peds) {
        if (!mu::pedAlive(G, id) || G.peds[id].faction != FAC_ENEMY) continue;
        if (length(G.peds[id].pos.toVec3() - pp) > radius) continue;
        G.peds[id].invincible = false;
        G.killPed(id, G.player, vec3(0, 0, 1), DMG_BULLET);
    }
}

void MissionTest::destroy(int veh) {
    if (veh < 0 || veh >= (int)g->vehicles.size() || !g->vehicles[veh].used || g->vehicles[veh].exploded) return;
    g->vehicles[veh].sim.engineHealth = 0.f;
    g->vehicles[veh].fireTimer = Max(g->vehicles[veh].fireTimer, 6.9f);
}

void MissionTest::shoot(int ped) {
    if (!mu::pedAlive(*g, ped)) return;
    g->peds[ped].invincible = false;
    g->killPed(ped, g->player, vec3(0, 0, 1), DMG_BULLET);
}

// Moves the player's vehicle along a road route toward p at `speed` (teleport steps), so chases and escorts play out.
void MissionTest::driveToward(vec2 p, float speed, float dt) {
    GameWorld& G = *g;
    int pv = G.playerVehicle();
    if (pv < 0) {
        teleportNear(p, 2.f);
        return;
    }
    if (length(routeGoal - p) > 5.f || route.empty()) {
        routeGoal = p;
        route.clear();
        if (G.isBoat(pv) || G.isAircraft(pv)) {
            route.push_back(mu::playerPos(G).xy());
            route.push_back(p);
        } else {
            G.computeRoute(mu::playerPos(G).xy(), p, true, route);
        }
    }
    vec3 pos = mu::vehPos(G, pv);
    // advance along the polyline by speed*dt
    float step = speed * dt;
    while (route.size() >= 2) {
        vec2 a = pos.xy(), b = route[1];
        float d = length(b - a);
        if (d > step) {
            vec2 np = a + (b - a) / d * step;
            float yaw = mu::yawTo(a, b);
            float z = G.isBoat(pv) ? pos.z : (G.isAircraft(pv) ? Max(pos.z, mu::groundAt(G, np.x, np.y) + 40.f) : mu::groundAt(G, np.x, np.y, pos.z + 4.f));
            mu::teleportVehicle(G, pv, vec3(np, z), yaw);
            // a gentle velocity only: the car is moved by teleports, and a fast body hitting a curb would hurt the occupants
            G.vehicles[pv].sim.body.vel = vec3(normalize(b - a) * Min(speed, 7.f), 0.f);
            return;
        }
        step -= d;
        pos = vec3(b, pos.z);
        route.erase(route.begin());
    }
    if (!route.empty()) {
        float z = G.isBoat(pv) ? pos.z : mu::groundAt(G, route[0].x, route[0].y, pos.z + 4.f);
        vec3 f = G.vehicles[pv].sim.forward();
        mu::teleportVehicle(G, pv, vec3(route[0], z), atan2f(-f.x, f.y));
    }
    stopVehicle();
}

void MissionTest::stopVehicle() {
    int pv = g->playerVehicle();
    if (pv < 0) return;
    g->vehicles[pv].sim.body.vel = vec3(0);
    g->vehicles[pv].sim.body.angVel = vec3(0);
}

void MissionTest::screenshot(const char* tag) {
    std::string path = shotDir() + StrFormat("%s_%02d_%s.bmp", id.c_str(), shots++, tag);
    gRun.pendingShotPath = path;
    gRun.pendingShotFrames = 0;
    g->requestScreenshot = path;
    log("screenshot %s", path.c_str());
}

void MissionTest::log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG("[missiontest] %s t=%.1f: %s", id.c_str(), missionTime, buf);
}

// Generic driver: go to the objective target, clear out enemies.
void Mission::autotest(GameWorld& g, MissionTest& t) {
    if (t.stageTime < 1.5f) return;
    if (mission_detail::gHasTarget) {
        if (g.playerVehicle() >= 0) t.driveToward(mission_detail::gTarget, 45.f, g.dtLast);
        else t.teleportNear(mission_detail::gTarget, 1.f);
    }
    if (t.stageTime > 3.f) t.killEnemies();
}

// ------------------------------------------------------------------------------------------------------------------
namespace mtest_detail {

void skipCutscene(GameWorld& g) {
    MissionManager& M = gMissions;
    if (M.shotIndex < 0) return;
    M.shotIndex = -1;
    M.shots.clear();
    g.playerControl = true;
    g.hudVisible = true;
    g.rig.scriptActive = false;
    g.rig.cut = true;
}

std::vector<std::string> expandQueue(const std::string& arg) {
    std::vector<std::string> out;
    const MissionManager& M = gMissions;
    auto addIf = [&](auto pred) {
        std::vector<std::pair<int, std::string>> v;
        for (const MissionDef& d : M.defs)
            if (pred(d)) v.push_back({d.storyIndex >= 0 ? d.storyIndex : 1000 + (int)v.size(), d.id});
        std::sort(v.begin(), v.end());
        for (auto& p : v) out.push_back(p.second);
    };
    if (arg == "all") addIf([](const MissionDef&) { return true; });
    else if (arg == "story") addIf([](const MissionDef& d) { return d.storyIndex >= 0; });
    else if (arg == "side") addIf([](const MissionDef& d) { return d.storyIndex < 0; });
    else if (arg.size() == 4 && arg.compare(0, 3, "act") == 0) {
        int act = arg[3] - '0';
        addIf([act](const MissionDef& d) { return d.storyIndex >= 0 && d.act == act; });
    } else {
        size_t s = 0;
        while (s <= arg.size()) {
            size_t e = arg.find(',', s);
            if (e == std::string::npos) e = arg.size();
            if (e > s) out.push_back(arg.substr(s, e - s));
            s = e + 1;
        }
    }
    return out;
}

// Prepare the world for mission `id`: prerequisites, protagonist, time of day, position.
bool prepareMission(GameWorld& g, const std::string& id) {
    MissionManager& M = gMissions;
    int di = M.findDef(id.c_str());
    if (di < 0) {
        LOG("[missiontest] unknown mission id '%s'", id.c_str());
        return false;
    }
    const MissionDef& d = M.defs[di];
    if ((int)g.storyFlags.size() < kFlagCount) g.storyFlags.resize(kFlagCount, 0);
    if (d.storyIndex >= 0) {
        for (int i = 0; i < mu::SF_STORY_COUNT; i++) g.storyFlags[i] = i < d.storyIndex ? 1 : 0;
    } else {
        // side content: unlock the story far enough for its prerequisites
        if (d.requiresFlag >= 0) g.storyFlags[d.requiresFlag] = 1;
        if (d.requiresFlag2 >= 0) g.storyFlags[d.requiresFlag2] = 1;
    }
    if (g.storyFlags[mu::EX_HEIST_APPROACH] == 0) {
        const char* ap = Platform::argValue("approach");
        g.storyFlags[mu::EX_HEIST_APPROACH] = ap && strcmp(ap, "loud") == 0 ? 2 : 1;
    }
    g.storyFlags[mu::EX_INTRO_DONE] = 1;
    if (d.protagonist >= 0 && d.protagonist != g.protagonistIndex) mu::switchProtagonist(g, d.protagonist, true);
    if (d.timeFrom != d.timeTo) g.env->timeOfDay = fmodf(d.timeFrom + 0.75f, 24.f);
    Ped* pl = g.playerPed();
    if (pl) {
        pl->health = pl->maxHealth;
        pl->armor = 100.f;
        g.giveWeapon(g.player, WPN_PISTOL, 120);
        g.giveWeapon(g.player, WPN_SMG, 240);
    }
    g.pinfo.wanted = 0;
    g.pinfo.wantedHeat = 0.f;
    g.pinfo.money = Max<long long>(g.pinfo.money, 20000);
    // stand next to the start trigger, then start directly
    vec2 sp = d.startPos;
    mu::placePlayer(g, vec3(sp + vec2(2.f, 0.f), mu::groundAt(g, sp.x, sp.y)), 0.f);
    M.cooldown = 0.f;
    M.retry.def = -1;
    M.retry.pending = false;
    M.startCheckpoint = 0;
    g.startMission(di);
    return M.active != nullptr;
}

}  // namespace mtest_detail

// Called every frame from updateOpenWorld.
void updateMissionTest(GameWorld& g, float dt) {
    MissionManager& M = gMissions;
    TestRun& R = gRun;
    MissionTest& T = M.test;
    if (!R.parsed) {
        R.parsed = true;
        if (const char* a = Platform::argValue("missiontest")) {
            R.queue = expandQueue(a);
            T.active = !R.queue.empty();
            if (const char* tl = Platform::argValue("testlimit")) R.timeLimit = (float)atof(tl);
        } else if (const char* m = Platform::argValue("mission")) {
            R.single = m;
            R.playOnly = true;
        }
        T.g = &g;
    }
    // pending screenshot: the app saves it after the frame is complete; fall back to a direct capture
    if (!R.pendingShotPath.empty()) {
        if (g.requestScreenshot.empty()) R.pendingShotPath.clear();
        else if (++R.pendingShotFrames > 3) {
            gfx::saveScreenshotBMP(R.pendingShotPath.c_str());
            g.requestScreenshot.clear();
            R.pendingShotPath.clear();
        }
    }
    Ped* pl = g.playerPed();
    if (!pl) return;
    if (R.playOnly) {
        R.setupDelay += dt;
        if (R.setupDelay > 1.5f && !R.started) {
            R.started = true;
            prepareMission(g, R.single);
        }
        return;
    }
    if (!T.active) return;
    // faster, lighter runs: double simulation speed, thinner ambient population
    if (pl->health > 0.f && g.pinfo.deathTimer <= 0.f) g.timeScale = 2.f;
    g.pedDensityScale = 0.35f;
    g.trafficDensityScale = 0.35f;
    if (R.quitTimer >= 0.f) {
        R.quitTimer -= dt;
        if (R.quitTimer < 0.f) {
            LOG("[missiontest] SUMMARY: %d passed, %d failed", R.passed, R.failed);
            for (const std::string& s : R.results) LOG("[missiontest]   %s", s.c_str());
            PostQuitMessage(0);
        }
        return;
    }
    // start the next mission
    if (!R.running) {
        R.setupDelay += dt;
        if (R.setupDelay < 2.f) return;
        R.index++;
        if (R.index >= (int)R.queue.size()) {
            R.quitTimer = 2.f;
            return;
        }
        T.id = R.queue[R.index];
        T.shots = 0;
        T.missionTime = 0.f;
        T.lastStage = -1;
        T.stageTime = 0.f;
        T.route.clear();
        T.routeGoal = vec2(1e9f, 1e9f);
        R.cutsceneTime = 0.f;
        R.lastShotIndex = -1;
        R.shotDelay = 1.0f;
        R.shotTag = "start";
        if (!prepareMission(g, T.id)) {
            R.failed++;
            R.results.push_back(T.id + ": could not start");
            R.setupDelay = 0.f;
            return;
        }
        T.log("started (%s)", M.active->title());
        R.running = true;
        return;
    }
    T.missionTime += dt;
    if (!M.active) {
        // ended: pass or fail
        bool failedRun = M.retry.def >= 0;
        if (!R.waitingEnd) {
            R.waitingEnd = true;
            R.endTimer = 0.f;
            T.log("%s at stage %d%s%s", failedRun ? "FAILED" : "PASSED", T.lastStage, failedRun ? " - " : "", failedRun ? g.hudBigSub.c_str() : "");
            if (failedRun) R.failed++;
            else R.passed++;
            R.results.push_back(StrFormat("%s: %s (stage %d, %.0f s)%s%s", T.id.c_str(), failedRun ? "FAILED" : "passed", T.lastStage, T.missionTime,
                                          failedRun ? " - " : "", failedRun ? g.hudBigSub.c_str() : ""));
            T.screenshot(failedRun ? "failed" : "passed");
        }
        R.endTimer += dt;
        if (R.endTimer > 2.5f) {
            R.waitingEnd = false;
            R.running = false;
            R.setupDelay = 0.f;
            M.retry.def = -1;
            M.retry.pending = false;
            M.retry.timer = 0.f;
            if (pl->health <= 0.f) {
                pl->health = pl->maxHealth;
                g.pinfo.deathTimer = 0.f;
            }
        }
        return;
    }
    Mission& m = *M.active;
    if (m.stage != T.lastStage) {
        T.log("stage %d -> %d", T.lastStage, m.stage);
        T.lastStage = m.stage;
        T.stageTime = 0.f;
        R.shotDelay = 0.8f;
        R.shotTag = StrFormat("stage%d", m.stage);
    }
    T.stageTime += dt;
    // cutscenes: screenshot, then skip
    if (M.shotIndex >= 0) {
        if (M.shotIndex != R.lastShotIndex) {
            R.lastShotIndex = M.shotIndex;
            R.cutsceneTime = 0.f;
        }
        R.cutsceneTime += dt;
        if (R.cutsceneTime > 0.9f && R.shotDelay < -0.5f && M.shotIndex == 0 && R.shotTag != "cut") {
            R.shotTag = "cut";
            T.screenshot("cutscene");
        }
        if (R.cutsceneTime > 1.6f) {
            if (M.shotIndex + 1 < (int)M.shots.size() && M.shotIndex < 1) {
                M.shotIndex++;
                M.shotTime = 0.f;
            } else {
                skipCutscene(g);
            }
        }
        return;
    }
    R.lastShotIndex = -1;
    if (R.shotDelay >= 0.f) {
        R.shotDelay -= dt;
        if (R.shotDelay < 0.f) {
            T.screenshot(R.shotTag.c_str());
            R.shotDelay = -1.f;
        }
    }
    // keep the player alive through the scripted run (the test checks mission flow, not combat balance)
    pl->health = Max(pl->health, pl->maxHealth * 0.6f);
    pl->invincible = true;
    m.autotest(g, T);
    if (T.missionTime > R.timeLimit) {
        T.log("TIMEOUT at stage %d", m.stage);
        g.mEnd(false, "Test timeout");
    }
}

}  // namespace Game
