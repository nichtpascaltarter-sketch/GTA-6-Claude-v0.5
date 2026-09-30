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
    bool roam = false;        // running the free-roam checks instead of a mission
    std::string cutTag;       // last cutscene shot photographed
    int prepStage = 0;        // 1: placed at the start, waiting for the world to stream in
    double prepT0 = 0.0;
    std::vector<std::string> shotQueue;   // screenshots wait for streaming too
    double shotT0 = 0.0;
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
    if (gRun.shotQueue.empty()) gRun.shotT0 = TimeSeconds();
    gRun.shotQueue.push_back(path);
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
    // comma separated items; each is a mission id or a group: all, story, side, act1..act3
    size_t s = 0;
    while (s <= arg.size()) {
        size_t e = arg.find(',', s);
        if (e == std::string::npos) e = arg.size();
        std::string item = arg.substr(s, e - s);
        s = e + 1;
        if (item.empty()) continue;
        if (item == "all") addIf([](const MissionDef&) { return true; });
        else if (item == "story") addIf([](const MissionDef& d) { return d.storyIndex >= 0; });
        else if (item == "side") addIf([](const MissionDef& d) { return d.storyIndex < 0; });
        else if (item.size() == 4 && item.compare(0, 3, "act") == 0) {
            int act = item[3] - '0';
            addIf([act](const MissionDef& d) { return d.storyIndex >= 0 && d.act == act; });
        } else {
            out.push_back(item);
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
    // stand next to the start trigger; the mission starts once the world around it has streamed in
    vec2 sp = d.startPos;
    mu::placePlayer(g, vec3(sp + vec2(2.f, 0.f), mu::groundAt(g, sp.x, sp.y)), 0.f);
    // vehicle jobs start from behind the wheel of their vehicle
    int jobClass = id == "taxi" ? (int)Vehicles::VC_TAXI : (id == "vigilante" ? (int)Vehicles::VC_POLICE : (id == "paramedic" ? (int)Vehicles::VC_AMBULANCE : -1));
    if (jobClass >= 0) {
        mu::Place pl = mu::resolvePlace(g, sp);
        int v = mu::placePlayer(g, pl.curb, pl.curbYaw, mu::pickModel(g, {(Vehicles::VehicleClass)jobClass}));
        if (v < 0) LOG("[missiontest] no vehicle of class %d for %s", jobClass, id.c_str());
    }
    M.cooldown = 0.f;
    M.retry.def = -1;
    M.retry.pending = false;
    return true;
}

bool startPrepared(GameWorld& g, const std::string& id) {
    MissionManager& M = gMissions;
    int di = M.findDef(id.c_str());
    if (di < 0) return false;
    M.startCheckpoint = 0;
    g.startMission(di);
    return M.active != nullptr;
}

// World cells (render meshes and collision) around the camera are loaded
bool worldStreamed(GameWorld& g) { return !g.renderer || !g.renderer->world || g.renderer->world->pendingCount() <= 2; }

}  // namespace mtest_detail

// ------------------------------------------------------------------------------------------------------------------
// Free-roam test ("--missiontest roam"): shops, safehouse save / wardrobe / garage, property and business purchases,
// daily income, phone menus and calls, character switching, a signal jammer and the pier stunt jump. Every check is
// logged as "[missiontest] roam: ok|FAIL ...".
namespace mtest_detail {

struct RoamTest {
    int step = 0, phase = 0;
    float t = 0.f;
    long long money0 = 0;
    int value0 = 0, count0 = 0;
    int veh = -1;
    vec3 color0;
    std::string stamp0;
    int ok = 0, bad = 0;
    vec3 standAt;          // where roamStandOn put the player (re-placed once if an interior was still being built)
    bool restood = false;
};
RoamTest gRoam;

void roamCheck(bool pass, const std::string& what) {
    if (pass) gRoam.ok++;
    else gRoam.bad++;
    LOG("[missiontest] roam: %s %s", pass ? "ok  " : "FAIL", what.c_str());
}

void roamNext() {
    gRoam.step++;
    gRoam.phase = 0;
    gRoam.t = 0.f;
}

void roamStandOn(GameWorld& g, vec3 p) {
    mu::placePlayer(g, vec3(p.x, p.y, p.z + 0.5f), 0.f);
    g.rig.cut = true;
    gRoam.standAt = p;
    gRoam.restood = false;
}

// first enabled menu item matching pred, -1 if none (ids are returned, not indices)
template <class F>
int roamPick(F pred) {
    for (const mu::MenuItem& it : mu::gMenu.items)
        if (it.enabled && pred(it)) return it.id;
    return -1;
}

// Returns true when the whole sequence is done.
bool roamUpdate(GameWorld& g, float dt, MissionTest& T) {
    using namespace mu;
    RoamTest& R = gRoam;
    R.t += dt;
    Ped* pl = g.playerPed();
    if (!pl) return false;
    // wait for a menu to open: true once open; logs a failure and skips the step on timeout
    auto waitMenu = [&](int owner, const char* what) {
        if (menuIs(owner)) return true;
        // the interior's floor may only have appeared after the teleport: stand on the marker again
        vec3 pp = pl->pos.toVec3();
        if (R.t > 1.f && !R.restood && (::length(pp.xy() - R.standAt.xy()) > 1.2f || fabsf(pp.z - R.standAt.z) > 2.f)) {
            LOG("[missiontest] roam: %s: re-standing on the marker (player at %.1f %.1f %.1f, marker %.1f %.1f %.1f)", what, pp.x, pp.y, pp.z,
                R.standAt.x, R.standAt.y, R.standAt.z);
            roamStandOn(g, R.standAt);
            R.restood = true;
        }
        if (R.t > 5.f) {
            roamCheck(false, StrFormat("%s: menu did not open (player %.1f %.1f %.1f, marker %.1f %.1f %.1f, control %d, mission %d, menu %d)", what,
                                       pp.x, pp.y, pp.z, R.standAt.x, R.standAt.y, R.standAt.z, (int)g.playerControl, gMissions.active ? 1 : 0,
                                       (int)gMenu.open));
            if (gMenu.open) menuClose(g);
            roamNext();
        }
        return false;
    };
    switch (R.step) {
        case 0: {   // setup: story finished, lots of money, Mari at noon
            if ((int)g.storyFlags.size() < kFlagCount) g.storyFlags.resize(kFlagCount, 0);
            for (int i = 0; i < SF_STORY_COUNT; i++) g.storyFlags[i] = 1;
            g.storyFlags[EX_INTRO_DONE] = 1;
            g.storyFlags[EX_SWITCH_TIP] = 1;
            if (g.protagonistIndex != 0) switchProtagonist(g, 0, true);
            g.pinfo.money = 2000000;
            g.pinfo.wanted = 0;
            g.pinfo.wantedHeat = 0.f;
            g.env->timeOfDay = 12.f;
            LOG("[missiontest] roam: %d shops, %d safehouses, %d businesses, %d jammers, %d stunt jumps", (int)gShops.shops.size(),
                (int)gShops.safehouses.size(), (int)gShops.businesses.size(), (int)gAct.jammers.size(), (int)gAct.stunts.size());
            roamNext();
            break;
        }
        case 1: {   // gun store: buy a weapon, then leave
            const ShopSite& s = gShops.shops[0];
            if (R.phase == 0) {
                roamStandOn(g, s.marker);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_SHOP_GUNS, s.name)) break;
                int pick = roamPick([](const MenuItem& it) { return it.id < 100; });
                roamCheck(pick >= 0, StrFormat("%s lists %d items", s.name, (int)gMenu.items.size()));
                if (pick < 0) {
                    gMenuInject = -2;
                    roamNext();
                    break;
                }
                R.value0 = pick;
                R.money0 = g.pinfo.money;
                gMenuInject = pick;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.4f) {
                const WeaponInfo& wi = weaponInfo((WeaponType)R.value0);
                roamCheck(pl->hasWeapon[R.value0] && R.money0 - g.pinfo.money == wi.price, StrFormat("bought a %s for $%d", wi.name, wi.price));
                gMenuInject = -2;
                R.phase = 3;
                R.t = 0.f;
            } else if (R.phase == 3 && R.t > 0.4f) {
                roamCheck(!gMenu.open && g.playerControl, "gun store closes with Back and gives control back");
                roamNext();
            }
            break;
        }
        case 2: {   // clothing store: buy and wear an outfit
            const ShopSite& s = gShops.shops[4];
            int who = g.protagonistIndex;
            if (R.phase == 0) {
                roamStandOn(g, s.marker);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_SHOP_CLOTHES, s.name)) break;
                int pick = roamPick([](const MenuItem& it) { return it.price > 0; });
                roamCheck(pick >= 0, StrFormat("%s offers outfits (%d listed)", s.name, (int)gMenu.items.size()));
                if (pick < 0) {
                    gMenuInject = -2;
                    roamNext();
                    break;
                }
                R.value0 = pick;
                R.money0 = g.pinfo.money;
                gMenuInject = pick;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.6f) {
                roamCheck(outfitOwned(g, who, R.value0) && currentOutfit(g, who) == R.value0 && g.pinfo.money < R.money0,
                          StrFormat("bought and wearing '%s'", kOutfits[who][R.value0].name));
                gMenuInject = -2;
                R.phase = 3;
                R.t = 0.f;
            } else if (R.phase == 3 && R.t > 1.f) {
                roamCheck(!gMenu.open && g.playerPed() != nullptr && g.playerPed()->health > 0.f, "outfit applied, store closed");
                roamNext();
            }
            break;
        }
        case 3: {   // dealership: buy the cheapest car
            const ShopSite& s = gShops.shops[5];
            if (R.phase == 0) {
                roamStandOn(g, s.marker);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_SHOP_CARS, s.name)) break;
                int pick = roamPick([](const MenuItem&) { return true; });
                roamCheck(pick >= 0, StrFormat("%s lists %d vehicles", s.name, (int)gMenu.items.size()));
                if (pick < 0) {
                    gMenuInject = -2;
                    roamNext();
                    break;
                }
                R.value0 = pick;
                R.count0 = (int)g.ownedVehicleModels.size();
                R.money0 = g.pinfo.money;
                gMenuInject = pick;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.6f) {
                R.veh = -1;
                for (int v = 0; v < (int)g.vehicles.size(); v++)
                    if (g.vehicles[v].used && g.vehicles[v].model == R.value0 && ::length(vehPos(g, v) - s.place.curb) < 15.f) R.veh = v;
                const Vehicles::VehicleModel& m = g.vassets[R.value0].spec;
                roamCheck((int)g.ownedVehicleModels.size() == R.count0 + 1 && R.money0 - g.pinfo.money == m.price && R.veh >= 0 && !gMenu.open,
                          StrFormat("bought a %s %s for $%d, parked at the curb", m.maker.c_str(), m.name.c_str(), m.price));
                roamNext();
            }
            break;
        }
        case 4: {   // Tide Customs with one wanted star: quick respray, a neon kit and an engine upgrade, then leave
            const ShopSite& s = gShops.shops[2];
            auto step = [&](int next) {
                R.phase = next;
                R.t = 0.f;
            };
            if (R.phase == 0) {
                int model = R.value0 >= 0 && R.value0 < (int)g.vassets.size() ? R.value0 : pickModel(g, {Vehicles::VC_SEDAN});
                R.veh = placePlayer(g, s.marker, s.place.curbYaw, model);
                if (R.veh < 0) {
                    roamCheck(false, "Tide Customs: could not spawn a car");
                    roamNext();
                    break;
                }
                R.color0 = g.vehicles[R.veh].color0;
                g.vehicles[R.veh].sim.health = 500.f;
                R.money0 = g.pinfo.money;
                g.pinfo.wanted = 1;
                g.pinfo.wantedHeat = 0.5f;
                step(1);
            } else if (R.phase == 1) {
                if (menuIs(MO_RESPRAY)) {
                    roamCheck(g.pinfo.wanted == 0 && g.rig.scriptActive, StrFormat("%s: garage open, wanted cleared, garage camera", s.name));
                    gMenuInject = MI_RESPRAY;
                    step(2);
                } else if (R.t > 8.f) {
                    roamCheck(false, StrFormat("%s did not open (wanted %d, seen %d)", s.name, g.pinfo.wanted, (int)g.pinfo.policeSeesPlayer));
                    roamNext();
                }
            } else if (R.phase == 2 && R.t > 0.4f) {
                const Vehicle& v = g.vehicles[R.veh];
                roamCheck(R.money0 - g.pinfo.money == kRespray && length(v.color0 - R.color0) > 0.01f && v.sim.health >= 999.f, "quick respray: new paint, repaired");
                R.money0 = g.pinfo.money;
                gMenuInject = MP_NEON;
                step(3);
            } else if (R.phase == 3 && R.t > 0.4f) {
                roamCheck(gShops.modPage == MP_NEON && gMenu.items.size() > 3, StrFormat("neon page lists %d kits", (int)gMenu.items.size()));
                gMenuInject = 2;   // Ice Blue
                step(4);
            } else if (R.phase == 4 && R.t > 0.4f) {
                const Vehicle& v = g.vehicles[R.veh];
                roamCheck(length(v.mods.neon - kNeons[2].srgb) < 0.01f && R.money0 - g.pinfo.money == kNeonPrice, "Ice Blue neon installed");
                R.money0 = g.pinfo.money;
                gMenuInject = -2;
                step(5);
            } else if (R.phase == 5 && R.t > 0.4f) {
                gMenuInject = MP_ENGINE;
                step(6);
            } else if (R.phase == 6 && R.t > 0.4f) {
                gMenuInject = 2;   // engine level 2
                step(7);
            } else if (R.phase == 7 && R.t > 0.4f) {
                const Vehicle& v = g.vehicles[R.veh];
                roamCheck(v.mods.engine == 2 && g.pinfo.money < R.money0, StrFormat("engine level 2 fitted (-$%lld)", R.money0 - g.pinfo.money));
                gMenuInject = -2;
                step(8);
            } else if (R.phase == 8 && R.t > 0.4f) {
                gMenuInject = MI_LEAVE;
                step(9);
            } else if (R.phase == 9) {
                if (gShops.resprayStage == 0 && R.t > 0.5f) {
                    roamCheck(g.playerControl && !g.rig.scriptActive && !gMenu.open, "left the garage: control and camera restored");
                    roamNext();
                } else if (R.t > 8.f) {
                    roamCheck(false, StrFormat("garage exit stuck (stage %d)", gShops.resprayStage));
                    roamNext();
                }
            }
            break;
        }
        case 5: {   // safehouse: rest + save (the story module saves directly when the app does not open the save menu)
            Safehouse& h = gShops.safehouses[0];
            if (R.phase == 0) {
                UI::SaveSlotInfo si;
                R.stamp0 = g.readSlotInfo(7, si) ? si.timestamp : std::string();
                R.value0 = g.gameDay;
                pl->health = pl->maxHealth * 0.5f;
                roamStandOn(g, h.save);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1 && R.t > 1.5f) {
                UI::SaveSlotInfo si;
                bool saved = g.readSlotInfo(7, si);
                roamCheck(saved && !g.requestSaveMenu && pl->health >= pl->maxHealth * 0.99f,
                          StrFormat("%s: rested and saved to slot 8 ('%s', %s, was %s)", h.name, si.title.c_str(), si.timestamp.c_str(),
                                    R.stamp0.empty() ? "empty" : R.stamp0.c_str()));
                roamNext();
            }
            break;
        }
        case 6: {   // wardrobe: change back to another owned outfit
            Safehouse& h = gShops.safehouses[0];
            int who = g.protagonistIndex;
            if (R.phase == 0) {
                roamStandOn(g, h.wardrobe);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_WARDROBE, "wardrobe")) break;
                int pick = roamPick([](const MenuItem&) { return true; });
                roamCheck(pick >= 0, StrFormat("wardrobe lists %d outfits", (int)gMenu.items.size()));
                R.value0 = pick;
                if (pick >= 0) gMenuInject = pick;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.6f) {
                if (R.value0 >= 0) roamCheck(currentOutfit(g, who) == R.value0, StrFormat("wearing '%s' from the wardrobe", kOutfits[who][R.value0].name));
                gMenuInject = -2;
                R.phase = 3;
                R.t = 0.f;
            } else if (R.phase == 3 && R.t > 0.4f) {
                roamCheck(!gMenu.open, "wardrobe closed");
                roamNext();
            }
            break;
        }
        case 7: {   // garage: take out an owned vehicle
            Safehouse& h = gShops.safehouses[0];
            if (R.phase == 0) {
                roamStandOn(g, h.garage);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_GARAGE, "garage")) break;
                int pick = roamPick([](const MenuItem&) { return true; });
                roamCheck(pick >= 0, StrFormat("garage lists %d vehicles", (int)gMenu.items.size()));
                if (pick < 0) {
                    gMenuInject = -2;
                    roamNext();
                    break;
                }
                gMenuInject = pick;
                R.value0 = pick;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.6f) {
                bool out = h.garageVehicle >= 0 && g.vehicles[h.garageVehicle].used && g.vehicles[h.garageVehicle].model == R.value0;
                roamCheck(out && !gMenu.open, StrFormat("garage: %s parked outside", g.vassets[R.value0].spec.name.c_str()));
                if (out) {
                    const Vehicle& gv = g.vehicles[h.garageVehicle];
                    roamCheck(length(gv.mods.neon - kNeons[2].srgb) < 0.01f && gv.mods.engine == 2, "garage car kept its Tide Customs parts");
                }
                roamNext();
            }
            break;
        }
        case 8: {   // buy a safehouse
            const int idx = 2;
            Safehouse& h = gShops.safehouses[idx];
            if (R.phase == 0) {
                R.money0 = g.pinfo.money;
                roamStandOn(g, h.save);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_PROPERTY, h.name)) break;
                gMenuInject = 1;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.6f) {
                roamCheck(safehouseOwned(g, idx) && R.money0 - g.pinfo.money == h.price && !gMenu.open, StrFormat("bought %s for $%lld", h.name, h.price));
                roamNext();
            }
            break;
        }
        case 9: {   // buy a business, then collect a day of income
            const int idx = 1;
            Business& b = gShops.businesses[idx];
            if (R.phase == 0) {
                R.money0 = g.pinfo.money;
                roamStandOn(g, b.marker);
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (!waitMenu(MO_BUSINESS, b.name)) break;
                gMenuInject = 1;
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2 && R.t > 0.6f) {
                roamCheck(businessOwned(g, idx) && R.money0 - g.pinfo.money == b.price, StrFormat("bought %s for $%lld", b.name, b.price));
                setFlag(g, EX_LAST_PAYDAY, g.gameDay);
                R.money0 = g.pinfo.money;
                g.gameDay++;
                R.phase = 3;
                R.t = 0.f;
            } else if (R.phase == 3 && R.t > 0.5f) {
                long long got = g.pinfo.money - R.money0;
                roamCheck(got == b.income, StrFormat("next morning: business income $%lld (expected $%d)", got, b.income));
                roamNext();
            }
            break;
        }
        case 10: {   // handset (the app's side simulated here): contacts, a call, realty waypoint, Wheels.ps purchase
            static UI::PhoneState ph;
            auto act = [&](UI::PhoneActionType type, int id, int app) {
                UI::PhoneAction pa;
                pa.type = type;
                pa.id = id;
                pa.app = app;
                return phoneHandle(g, ph, pa);
            };
            if (R.phase == 0) {
                ph = UI::PhoneState();
                R.value0 = 0;
                roamStandOn(g, gShops.safehouses[0].save + vec3(6.f, 6.f, 0.f));
                R.phase = 1;
                R.t = 0.f;
            }
            phoneRefresh(g, ph);
            if (R.phase == 1 && R.t > 0.3f) {
                int apps = 0;
                for (const UI::PhoneListApp& ap : ph.apps) apps += ap.items.empty() && !ap.action ? 0 : 1;
                roamCheck(ph.contacts.size() >= 5 && apps >= 4, StrFormat("handset: %d contacts, %d apps", (int)ph.contacts.size(), (int)ph.apps.size()));
                act(UI::PA_CALL, 1, -1);   // Mama Lucha
                R.phase = 2;
                R.t = 0.f;
            } else if (R.phase == 2) {
                if (ph.call == UI::CALL_ACTIVE && R.value0 == 0) R.value0 = 1;
                if (R.value0 == 1 && ph.call == UI::CALL_NONE) {
                    roamCheck(true, StrFormat("called Mama Lucha: dialed, connected, hung up after %.1f s", R.t));
                    R.phase = 3;
                    R.t = 0.f;
                } else if (R.t > 30.f) {
                    roamCheck(false, StrFormat("call to Lucha stuck (call state %d)", (int)ph.call));
                    act(UI::PA_HANG_UP, -1, -1);
                    R.phase = 3;
                    R.t = 0.f;
                }
            } else if (R.phase == 3 && R.t > 0.3f) {
                g.hasWaypoint = false;
                act(UI::PA_APP_ITEM, 102, APP_REALTY);   // Club Riptide listing
                roamCheck(g.hasWaypoint && ::length(g.waypoint - gShops.businesses[2].marker.xy()) < 1.f, "realty listing sets a waypoint");
                int pick = -1;
                for (const UI::PhoneListApp& ap : ph.apps)
                    if (ap.id == APP_WHEELS)
                        for (const UI::PhoneListItem& it : ap.items)
                            if (it.enabled && it.price >= 0 && pick < 0) pick = it.id;
                R.count0 = (int)g.ownedVehicleModels.size();
                R.money0 = g.pinfo.money;
                if (pick >= 0) act(UI::PA_APP_ITEM, pick, APP_WHEELS);
                roamCheck(pick >= 0 && (int)g.ownedVehicleModels.size() == R.count0 + 1 && R.money0 - g.pinfo.money == g.vassets[pick].spec.price,
                          StrFormat("Wheels.ps delivered a %s", pick >= 0 ? g.vassets[pick].spec.name.c_str() : "-"));
                int msgs = (int)gPhone.messages.size();
                roamCheck(msgs > 0, StrFormat("handset has %d messages (newest: %s)", msgs, msgs ? gPhone.messages.back().text.c_str() : "-"));
                roamNext();
            }
            break;
        }
        case 11: {   // character switch (the full camera transition) and back
            if (R.phase == 0) {
                if (g.mTalking()) break;
                R.value0 = (int)g.peds[g.player].uid;
                bool started = switchProtagonist(g, 1, false);
                roamCheck(started, "switch to Dex started");
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                if (g.protagonistIndex == 1 && !gSwitching && g.playerControl && R.t > 1.f) {
                    roamCheck(true, StrFormat("playing as Dex after %.1f s", R.t));
                    switchProtagonist(g, 0, true);
                    R.phase = 2;
                    R.t = 0.f;
                } else if (R.t > 20.f) {
                    roamCheck(false, "switch to Dex did not finish");
                    roamNext();
                }
            } else if (R.phase == 2 && R.t > 1.f) {
                roamCheck(g.protagonistIndex == 0 && g.playerPed() != nullptr, "switched back to Mari");
                roamNext();
            }
            break;
        }
        case 12: {   // signal jammer
            if (gAct.jammers.empty()) {
                roamCheck(false, "no signal jammers placed");
                roamNext();
                break;
            }
            int j = 0;
            for (int k = 0; k < (int)gAct.jammers.size(); k++)
                if (!jammerDone(g, k)) {
                    j = k;
                    break;
                }
            if (R.phase == 0) {
                R.value0 = flag(g, EX_JAMMER_COUNT);
                R.money0 = g.pinfo.money;
                roamStandOn(g, gAct.jammers[j] + vec3(1.2f, 0.f, 0.f));
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1 && R.t > 1.f) {
                destroyJammer(g, j);
                roamCheck(flag(g, EX_JAMMER_COUNT) == R.value0 + 1 && g.pinfo.money > R.money0,
                          StrFormat("destroyed jammer %d at (%.0f, %.0f, %.1f)", j, gAct.jammers[j].x, gAct.jammers[j].y, gAct.jammers[j].z));
                roamNext();
            }
            break;
        }
        case 13: {   // a stunt ramp: run up at speed, launch, land; the unique stunt bonus must register
            if (R.phase == 0) {
                if (gRamps.empty()) {
                    if (R.t > 40.f) {
                        roamCheck(false, "no stunt ramps placed");
                        roamNext();
                    }
                    break;
                }
                const StuntRamp& rp = gRamps[0];
                R.value0 = flag(g, EX_STUNT_COUNT);
                vec2 st = rp.foot.xy() - rp.dir * 65.f;
                vec3 start(st, groundAt(g, st.x, st.y, rp.foot.z + 5.f));
                int model = pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_MUSCLE});
                R.veh = placePlayer(g, start, atan2f(-rp.dir.x, rp.dir.y), model);
                if (R.veh < 0) {
                    roamCheck(false, "stunt ramp: no car");
                    roamNext();
                    break;
                }
                R.color0 = vec3(rp.dir, 0.f);
                g.vehicles[R.veh].sim.body.vel = vec3(rp.dir * 30.f, 0.f);
                LOG("[missiontest] roam: ramp 0 at (%.0f, %.0f, %.1f) heading (%.2f, %.2f)%s, %d ramps", rp.foot.x, rp.foot.y, rp.foot.z, rp.dir.x, rp.dir.y,
                    rp.waterGap ? " over water" : "", (int)gRamps.size());
                R.phase = 1;
                R.t = 0.f;
            } else if (R.phase == 1) {
                int pv = g.playerVehicle();
                vec2 dir = R.color0.xy();
                if (pv >= 0 && R.t < 1.5f) {
                    Vehicles::VehicleState& vs = g.vehicles[pv].sim;
                    float fwd = dot(vs.body.vel.xy(), dir);
                    if (fwd < 30.f) vs.body.vel += vec3(dir * (30.f - fwd), 0.f);
                }
                if (R.t > 1.f && R.t < 1.1f) T.screenshot("ramp_runup");
                if (flag(g, EX_STUNT_COUNT) > R.value0) {
                    roamCheck(true, StrFormat("stunt ramp jump cleared after %.1f s", R.t));
                    T.screenshot("ramp_landed");
                    roamNext();
                } else if (R.t > 9.f) {
                    vec3 p = pv >= 0 ? vehPos(g, pv) : playerPos(g);
                    roamCheck(false, StrFormat("stunt ramp jump not registered (car at %.0f, %.0f, %.1f)", p.x, p.y, p.z));
                    roamNext();
                }
            }
            break;
        }
        default:
            LOG("[missiontest] roam: %d checks ok, %d failed", R.ok, R.bad);
            return true;
    }
    return false;
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
    // queued screenshots are requested once the world around the camera has streamed in (or after 8 s)
    if (!R.shotQueue.empty() && R.pendingShotPath.empty() && g.requestScreenshot.empty() &&
        (worldStreamed(g) || TimeSeconds() - R.shotT0 > 8.0)) {
        R.pendingShotPath = R.shotQueue.front();
        R.pendingShotFrames = 0;
        g.requestScreenshot = R.pendingShotPath;
        R.shotQueue.erase(R.shotQueue.begin());
        R.shotT0 = TimeSeconds();
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
            if (prepareMission(g, R.single)) startPrepared(g, R.single);
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
        if (R.prepStage == 1) {
            if (!worldStreamed(g) && TimeSeconds() - R.prepT0 < 20.0) return;
            R.prepStage = 0;
            if (!startPrepared(g, T.id)) {
                R.failed++;
                R.results.push_back(T.id + ": could not start");
                R.setupDelay = 0.f;
                return;
            }
            T.log("started (%s) after %.1f s of streaming", M.active->title(), TimeSeconds() - R.prepT0);
            R.running = true;
            return;
        }
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
        R.cutTag.clear();
        if (T.id == "roam") {
            gRoam = RoamTest();
            R.roam = true;
            R.running = true;
            T.log("started (free-roam checks)");
            return;
        }
        if (!prepareMission(g, T.id)) {
            R.failed++;
            R.results.push_back(T.id + ": could not start");
            R.setupDelay = 0.f;
            return;
        }
        R.prepStage = 1;
        R.prepT0 = TimeSeconds();
        return;
    }
    T.missionTime += dt;
    if (R.roam) {
        pl->health = Max(pl->health, pl->maxHealth * 0.6f);
        pl->invincible = true;
        if (roamUpdate(g, dt, T)) {
            R.roam = false;
            R.running = false;
            R.setupDelay = 0.f;
            if (gRoam.bad == 0) R.passed++;
            else R.failed++;
            R.results.push_back(StrFormat("roam: %d checks ok, %d failed (%.0f s)", gRoam.ok, gRoam.bad, T.missionTime));
            T.screenshot("roam_end");
        }
        return;
    }
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
        // one screenshot of each of the first two shots of every cutscene
        std::string tag = StrFormat("cut%d", M.shotIndex);
        tag = StrFormat("%d_%d_%s", m.stage, (int)M.shots.size(), tag.c_str());
        if (R.cutsceneTime > 0.9f && M.shotIndex < 2 && R.cutTag != tag) {
            R.cutTag = tag;
            T.screenshot(StrFormat("stage%d_cut%d", m.stage, M.shotIndex).c_str());
        }
        if (R.cutsceneTime > 1.7f) {
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
