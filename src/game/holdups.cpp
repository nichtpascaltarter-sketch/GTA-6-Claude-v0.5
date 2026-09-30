// Store hold-ups: aim a gun at the clerk of a shop interior (convenience stores, gun shops, the clothing store, the
// diner register) to rob it. The clerk raises their hands, pleads, and empties the register into a cash bag on the
// counter while the gun stays on them; shouting (E while aiming) or a warning shot speeds them up; lowering the gun
// for too long trips the silent alarm. Some clerks keep a shotgun under the counter (more often in the Flats and
// in gun stores). Shoppers cower or run. The police come once the robbery is phoned in (at once with the alarm),
// Tidegram posts about it, and each shop can be robbed once every two game days (saved in the story flags).
// Test: --holduptest "<shop name>" (with --play --autoplay interior) walks in, robs the store and leaves, saving
// screenshots holdup_NN_<stage>.bmp into --shotdir.
namespace Game {
namespace holdups {

using World::InteriorDef;

const int kExRobbedDay = 300;    // storyFlags 300..331: game day + 1 when robbery slot k was last held up
const int kExRobberies = 332;    // storyFlags: store robberies (stats)
const int kExRobberyTake = 333;  // storyFlags: dollars taken in store robberies
const int kHoldSub = interiors_game::kScriptedSub;   // scenario ped this module drives (interiors_game does not pin it)

enum Phase : int { HP_NONE = 0, HP_PLEAD, HP_EMPTY, HP_HANDOVER, HP_AFTER, HP_ARMED, HP_ALARM };

struct State {
    int phase = HP_NONE;
    int def = -1, slot = -1;
    int clerk = -1;
    u32 clerkUid = 0;
    vec3 clerkHome, reg;      // clerk spot, register (bag) position
    float t = 0.f, progress = 0.f, maxTake = 0.f, noAim = 0.f, boost = 0.f, lineTimer = 0.f, workT = 0.f;
    int shotsSeen = 0;
    bool alarm = false, paid = false, reported = false;
    int bag = -1;
    double reportAt = -1.0;
    float hintCooldown = 0.f;
    int lineIdx = 0;
};
State gS;

struct Test {
    bool init = false;
    int def = -1;
    float t = 0.f;
    int shot = 0;
    int stage = 0;
    float stageT = 0.f;
};
Test gT;

bool robbableKind(u8 k) {
    return k == World::IK_CONVENIENCE || k == World::IK_GUNSHOP || k == World::IK_CLOTHES || k == World::IK_DINER || k == World::IK_BAR ||
           k == World::IK_BARBER || k == World::IK_TATTOO;
}

// Stable slot of a robbable interior (order of definition), -1 if not robbable / too many
int robberySlot(int def) {
    if (!World::gInteriors) return -1;
    int s = 0;
    for (int i = 0; i < (int)World::gInteriors->defs.size() && i <= def; i++) {
        if (!robbableKind(World::gInteriors->defs[i].kind)) continue;
        if (i == def) return s < 32 ? s : -1;
        s++;
    }
    return -1;
}

bool onCooldown(GameWorld& g, int slot) {
    int d = flag(g, kExRobbedDay + slot);
    return d > 0 && g.gameDay - (d - 1) < 2;
}

float faceYaw(vec2 from, vec2 to) {
    vec2 d = to - from;
    return atan2f(-d.x, d.y);
}

void turnPed(Ped& p, float want, float rate) {
    float d = wrapAngle(want - p.yaw);
    p.yaw = wrapAngle(p.yaw + Clamp(d, -rate, rate));
}

void speak(GameWorld& g, int pid, const std::string& text, const char* who, u32 color) {
    if (pid < 0 || pid >= (int)g.peds.size() || !g.peds[pid].used) return;
    Ped& p = g.peds[pid];
    std::string shown = text;
    size_t e = shown.find(']');
    if (!shown.empty() && shown[0] == '[' && e != std::string::npos) shown = shown.substr(e + 1);
    while (!shown.empty() && shown[0] == ' ') shown.erase(0, 1);
#ifdef HAVE_AUDIO
    Audio::speakAt(text.c_str(), p.voice, g.pedHeadPos(p), 1.f);
    g.startLipSync(pid, text.c_str(), p.voice);
    float dur = Audio::estimateSpeechDuration(text.c_str(), p.voice);
#else
    float dur = 1.6f;
#endif
    p.speechCooldown = dur + 0.3f;
    g.subtitle(who, shown, Max(dur, 1.4f) + 0.5f, color);
}

// Camera ray passes close to the target's chest (the player is aiming at them)
bool aimedAt(GameWorld& g, const Ped& target) {
    const Render::Camera& cam = g.rig.cam;
    vec3 f = cam.forward();
    vec3 chest = rel(target.pos, cam.pos) + vec3(0.f, 0.f, 1.25f);
    float along = dot(chest, f);
    if (along < 0.4f || along > 16.f) return false;
    return length(chest - f * along) < 0.6f + along * 0.025f;
}

bool armedWithGun(const Ped& p) { return p.weapon != WPN_FISTS && weaponInfo(p.weapon).clipSize > 0 && p.weapon != WPN_GRENADE && p.weapon != WPN_MOLOTOV; }

// Clerk of an interior: a scenario ped behind the counter (clerk / waiter / bartender / barber / artist)
int findClerk(GameWorld& g, int def) {
    using namespace interiors_game;
    if (def < 0 || def >= (int)gIS.slots.size() || !gIS.slots[def]) return -1;
    Loaded* L = gIS.slots[def];
    const InteriorDef& d = World::gInteriors->defs[def];
    const World::InteriorMarker* ctr = d.marker(World::IM_COUNTER);
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < L->peds.size(); i++) {
        int id = L->peds[i];
        if (id < 0 || id >= (int)g.peds.size()) continue;
        const Ped& p = g.peds[id];
        if (!p.used || p.uid != L->pedUid[i] || p.health <= 0.f) continue;
        u8 role = d.scenarios[L->pedScenario[i]].role;
        if (role != World::SR_CLERK && role != World::SR_WAITER && role != World::SR_BARTENDER && role != World::SR_BARBER && role != World::SR_ARTIST) continue;
        float dist = ctr ? length(p.pos.toVec3() - d.toWorld(ctr->pos)) : 0.f;
        if (dist < bestD) {
            bestD = dist;
            best = id;
        }
    }
    return best;
}

void scareBystanders(GameWorld& g, int def) {
    using namespace interiors_game;
    Loaded* L = def >= 0 && def < (int)gIS.slots.size() ? gIS.slots[def] : nullptr;
    if (!L) return;
    for (size_t i = 0; i < L->peds.size(); i++) {
        int id = L->peds[i];
        if (id < 0 || id >= (int)g.peds.size() || id == gS.clerk) continue;
        Ped& p = g.peds[id];
        if (!p.used || p.uid != L->pedUid[i] || p.health <= 0.f || p.brain.type != BRAIN_SCENARIO) continue;
        bool run = hashToFloat(hash32(p.uid * 97u)) < 0.35f;
        p.brain.type = run ? BRAIN_FLEE : BRAIN_COWER;
        p.brain.target = g.player;
        p.brain.timer = 0.f;
        p.animIn.expression = 4;
    }
}

void releaseClerk(GameWorld& g, bool cower) {
    if (gS.clerk < 0 || gS.clerk >= (int)g.peds.size()) return;
    Ped& p = g.peds[gS.clerk];
    if (!p.used || p.uid != gS.clerkUid) return;
    p.animIn.expression = -1;
    if (cower && p.health > 0.f && p.brain.type == BRAIN_SCENARIO) {
        p.brain.type = BRAIN_COWER;
        p.brain.target = g.player;
        p.brain.timer = 0.f;
        p.brain.sub = 0;
    }
}

void callPolice(GameWorld& g, bool now) {
    if (gS.reported) return;
    gS.reported = true;
    vec3 pos = gS.clerkHome;
    g.reportCrime(11, dvec3(pos), gS.clerk);
    if (now) {
        // the silent alarm goes straight to dispatch
        g.pinfo.wantedHeat = Max(g.pinfo.wantedHeat, 2.05f);
        g.pinfo.wantedCooldown = 0.f;
        if (Ped* pl = g.playerPed()) {
            g.pinfo.lastSeenPos = pl->pos;
            g.pinfo.lastSeenTime = (float)g.time;
        }
    }
}

void finish(GameWorld& g) {
    releaseClerk(g, true);
    if (gS.phase != HP_NONE) LOG("holdup: finished in '%s' (phase %d, take %.0f, paid %d, alarm %d)", World::gInteriors->defs[gS.def].name.c_str(), gS.phase,
                                 gS.maxTake * gS.progress, (int)gS.paid, (int)gS.alarm);
    gS = State();
}

void start(GameWorld& g, int def, int clerk) {
    const InteriorDef& d = World::gInteriors->defs[def];
    gS = State();
    gS.def = def;
    gS.slot = robberySlot(def);
    gS.clerk = clerk;
    Ped& ck = g.peds[clerk];
    gS.clerkUid = ck.uid;
    gS.clerkHome = ck.pos.toVec3();
    const World::InteriorMarker* ctr = d.marker(World::IM_COUNTER);
    vec3 cm = ctr ? d.toWorld(ctr->pos) : gS.clerkHome;
    gS.reg = lerp(cm, gS.clerkHome, 0.5f);
    gS.reg.z = gS.clerkHome.z + 1.02f;
    Ped* pl = g.playerPed();
    gS.shotsSeen = g.pinfo.shotsFired;
    u32 h = hash32(d.seed ^ (u32)g.gameDay * 2654435761u ^ (u32)(g.time * 3.0));
    // loot by kind (register + the safe for the bigger stores)
    float lo = 250.f, hi = 900.f;
    if (d.kind == World::IK_GUNSHOP) { lo = 700.f; hi = 2200.f; }
    else if (d.kind == World::IK_CLOTHES) { lo = 500.f; hi = 1500.f; }
    else if (d.kind == World::IK_DINER) { lo = 180.f; hi = 600.f; }
    gS.maxTake = Lerp(lo, hi, hashToFloat(h >> 4));
    float armedChance = d.kind == World::IK_GUNSHOP ? 0.35f : 0.07f;
    vec3 wp = gS.clerkHome;
    if (g.map && g.map->regionAt(wp.x, wp.y) == World::REG_FLATS) armedChance *= 2.2f;
    scareBystanders(g, def);
    ck.brain.sub = kHoldSub;
    ck.animIn.expression = 4;
    if (hashToFloat(h) < armedChance) {
        // the shotgun under the counter
        gS.phase = HP_ARMED;
        g.giveWeapon(clerk, WPN_SHOTGUN, 24);
        ck.weapon = WPN_SHOTGUN;
        ck.brain.type = BRAIN_COMBAT;
        ck.brain.target = g.player;
        ck.brain.timer = 0.f;
        ck.brain.accuracy = 0.45f;
        ck.animIn.expression = -1;
        static const char* kDefiant[] = {"[angry] Not in my store!", "[angry] Wrong store, cabron!", "[angry] I've been waiting for you, punk!"};
        speak(g, clerk, kDefiant[h % 3u], "Clerk", 0xffd0d0d0u);
        callPolice(g, false);
        LOG("holdup: armed clerk in '%s'", d.name.c_str());
        return;
    }
    gS.phase = HP_PLEAD;
    ck.brain.type = BRAIN_SCENARIO;
    ck.brain.scenario = 5;   // hands up
    ck.animIn.stance = 5;
    static const char* kPlead[] = {"[scared] Okay, okay! Don't shoot!", "[scared] Please! Take it, take everything!", "[scared] Easy, easy with that thing!",
                                   "[scared] I don't want any trouble!"};
    speak(g, clerk, kPlead[h % 4u], "Clerk", 0xffd0d0d0u);
    gS.lineTimer = 3.5f;
    if (pl) g.help("Keep the gun on the clerk. Press E to shout.", 5.f);
    LOG("holdup: started in '%s' (clerk %d, take up to $%.0f)", d.name.c_str(), clerk, gS.maxTake);
}

void spawnBag(GameWorld& g) {
    Pickup pk;
    pk.used = true;
    pk.type = PICK_MONEY;
    pk.amount = Max(20, (int)(gS.maxTake * gS.progress));
    pk.pos = dvec3(gS.reg);
    pk.life = 240.f;
    pk.timer = 1.2f;   // visible on the counter before it can be grabbed
    int idx = -1;
    for (int i = 0; i < (int)g.pickups.size(); i++)
        if (!g.pickups[i].used) {
            idx = i;
            break;
        }
    if (idx < 0) {
        g.pickups.push_back(pk);
        idx = (int)g.pickups.size() - 1;
    } else {
        g.pickups[idx] = pk;
    }
    gS.bag = idx;
}

void onPaid(GameWorld& g) {
    gS.paid = true;
    int take = Max(20, (int)(gS.maxTake * gS.progress));
    if (gS.slot >= 0) setFlag(g, kExRobbedDay + gS.slot, g.gameDay + 1);
    setFlag(g, kExRobberies, flag(g, kExRobberies) + 1);
    setFlag(g, kExRobberyTake, flag(g, kExRobberyTake) + take);
    const InteriorDef& d = World::gInteriors->defs[gS.def];
    g.socialReport(UI::TE_ROBBERY, dvec3(gS.clerkHome), d.name.c_str(), (float)take);
    // the clerk phones it in once the robber is out of the door
    if (!gS.reported) gS.reportAt = g.time + 10.0 + hashToFloat(hash32(gS.clerkUid)) * 12.0;
    LOG("holdup: cash bag taken ($%d) in '%s'", take, d.name.c_str());
}

void update(GameWorld& g, float dt) {
    if (!World::gInteriors) return;
    Ped* pl = g.playerPed();
    gS.hintCooldown = Max(0.f, gS.hintCooldown - dt);
    if (gS.phase == HP_NONE) {
        if (!pl || pl->state != PS_ONFOOT || !pl->aiming || !armedWithGun(*pl) || g.missionActive()) return;
        int def = Interiors::currentInterior();
        if (def < 0 || !robbableKind(World::gInteriors->defs[def].kind)) return;
        int clerk = findClerk(g, def);
        if (clerk < 0 || !aimedAt(g, g.peds[clerk])) return;
        int slot = robberySlot(def);
        if (slot >= 0 && onCooldown(g, slot)) {
            if (gS.hintCooldown <= 0.f) {
                speak(g, clerk, "[scared] Again? The register's empty, I swear! They cleaned us out already!", "Clerk", 0xffd0d0d0u);
                gS.hintCooldown = 12.f;
            }
            return;
        }
        start(g, def, clerk);
        return;
    }
    // ---- in progress
    gS.t += dt;
    const InteriorDef& d = World::gInteriors->defs[gS.def];
    bool clerkOk = gS.clerk >= 0 && gS.clerk < (int)g.peds.size() && g.peds[gS.clerk].used && g.peds[gS.clerk].uid == gS.clerkUid && g.peds[gS.clerk].health > 0.f;
    bool inside = Interiors::currentInterior() == gS.def;
    float away = pl ? length(pl->pos.toVec3() - gS.clerkHome) : 1e9f;
    // phoned-in report after the robber left
    if (gS.reportAt > 0.0 && g.time >= gS.reportAt) {
        gS.reportAt = -1.0;
        callPolice(g, false);
    }
    if (!clerkOk) {
        // shot or gone: the robbery is over (murder / assault reports come from combat)
        if (gS.phase == HP_PLEAD || gS.phase == HP_EMPTY) callPolice(g, false);
        if (gS.bag >= 0 && !gS.paid && !g.pickups[gS.bag].used) onPaid(g);
        if (away > 40.f || gS.phase == HP_ARMED) finish(g);
        return;
    }
    Ped& ck = g.peds[gS.clerk];
    vec2 toPl = pl ? pl->pos.toVec3().xy() : ck.pos.toVec3().xy();
    bool aimed = pl && pl->aiming && armedWithGun(*pl) && inside && aimedAt(g, ck);
    // warning shot (fired, and not into the clerk) and shouting speed the clerk up
    if (g.pinfo.shotsFired > gS.shotsSeen) {
        gS.shotsSeen = g.pinfo.shotsFired;
        if (gS.phase == HP_PLEAD || gS.phase == HP_EMPTY) {
            gS.boost = 4.f;
            gS.noAim = 0.f;
            speak(g, gS.clerk, "[panicked] Okay! Okay! I'm doing it, I'm doing it!", "Clerk", 0xffd0d0d0u);
            gS.lineTimer = 4.f;
        }
    }
    if (pl && pl->aiming && g.ctl.enter.pressed && (gS.phase == HP_PLEAD || gS.phase == HP_EMPTY)) {
        static const char* kShout[] = {"Empty the register! Now!", "Faster! In the bag!", "Don't even think about the alarm!", "Move it! All of it!"};
        speak(g, g.player, kShout[(gS.lineIdx++) % 4], "You", 0xffffffffu);
        gS.boost = Max(gS.boost, 3.f);
    }
    gS.boost = Max(0.f, gS.boost - dt);
    gS.lineTimer -= dt;
    switch (gS.phase) {
        case HP_PLEAD:
        case HP_EMPTY: {
            if (!inside && away > 6.f) {
                // walked out before the bag was ready
                callPolice(g, false);
                gS.phase = HP_AFTER;
                gS.t = 0.f;
                break;
            }
            if (gS.phase == HP_PLEAD && gS.t > 1.6f) {
                gS.phase = HP_EMPTY;
                gS.t = 0.f;
            }
            if (aimed) gS.noAim = 0.f;
            else gS.noAim += dt;
            if (gS.noAim > 4.5f) {
                // the gun went down too long: the silent alarm under the counter
                gS.alarm = true;
                gS.phase = HP_ALARM;
                gS.t = 0.f;
                ck.brain.scenario = 4;
                ck.animIn.stance = 4;
                speak(g, gS.clerk, "[panicked] Help! Somebody call the police!", "Clerk", 0xffd0d0d0u);
                callPolice(g, true);
                g.help("The clerk hit the silent alarm!", 4.f);
                break;
            }
            if (gS.phase == HP_EMPTY) {
                float rate = (aimed ? 1.f : 0.25f) * (gS.boost > 0.f ? 2.3f : 1.f) / 11.f;
                gS.progress = Min(1.f, gS.progress + dt * rate);
                // work the register in bursts, glancing back at the gun with hands up in between
                gS.workT += dt;
                bool working = aimed && fmodf(gS.workT, 3.4f) < 2.4f;
                ck.brain.scenario = working ? 0 : 5;
                ck.animIn.stance = working ? 0 : 5;
                turnPed(ck, working ? faceYaw(ck.pos.toVec3().xy(), gS.reg.xy()) : faceYaw(ck.pos.toVec3().xy(), toPl), dt * 5.f);
                if (gS.lineTimer <= 0.f && ck.speechCooldown <= 0.f) {
                    static const char* kWork[] = {"[scared] I'm going as fast as I can!", "[scared] Please, I have kids at home!", "[scared] It's not even my money, take it!",
                                                  "[scared] Almost, almost, don't shoot!", "[scared] Here, here, it's all going in!"};
                    speak(g, gS.clerk, kWork[(gS.lineIdx++) % 5], "Clerk", 0xffd0d0d0u);
                    gS.lineTimer = 4.f + hashToFloat(hash32(gS.clerkUid + (u32)gS.lineIdx)) * 3.f;
                }
                if (gS.progress >= 1.f) {
                    spawnBag(g);
                    gS.phase = HP_HANDOVER;
                    gS.t = 0.f;
                    ck.brain.scenario = 5;
                    ck.animIn.stance = 5;
                    speak(g, gS.clerk, "[scared] That's all of it! Please, just go!", "Clerk", 0xffd0d0d0u);
                    g.help("Grab the cash from the counter.", 4.f);
                }
            } else {
                turnPed(ck, faceYaw(ck.pos.toVec3().xy(), toPl), dt * 6.f);
            }
            break;
        }
        case HP_HANDOVER: {
            turnPed(ck, faceYaw(ck.pos.toVec3().xy(), toPl), dt * 6.f);
            if (!gS.paid && gS.bag >= 0 && gS.bag < (int)g.pickups.size() && !g.pickups[gS.bag].used) onPaid(g);
            if (gS.t > 6.f && gS.paid) {
                gS.phase = HP_AFTER;
                gS.t = 0.f;
                releaseClerk(g, true);
            }
            if (!inside && away > 8.f && !gS.paid) {
                // left the bag behind
                callPolice(g, false);
                gS.phase = HP_AFTER;
                gS.t = 0.f;
            }
            break;
        }
        case HP_ALARM:
            turnPed(ck, faceYaw(ck.pos.toVec3().xy(), toPl), dt * 6.f);
            if (gS.t > 3.f && ck.brain.type == BRAIN_SCENARIO) releaseClerk(g, true);
            if (away > 45.f) finish(g);
            break;
        case HP_ARMED:
            if (away > 45.f || gS.t > 90.f) finish(g);
            break;
        case HP_AFTER:
            if (!gS.paid && gS.bag >= 0 && gS.bag < (int)g.pickups.size() && !g.pickups[gS.bag].used) onPaid(g);
            if (away > 45.f && gS.reportAt < 0.0) finish(g);
            break;
        default: break;
    }
}

// ---- --holduptest: rob a store end to end with screenshots
std::string shotName(const char* stage) {
    std::string path = std::string("Z:\\tmp\\") + StrFormat("holdup_%02d_%s", gT.shot++, stage) + ".bmp";
    if (const char* dir = Platform::argValue("shotdir")) path = std::string(dir) + StrFormat("holdup_%02d_%s", gT.shot - 1, stage) + ".bmp";
    return path;
}

void steerLook(GameWorld& g, vec3 target, float dt) {
    const Render::Camera& cam = g.rig.cam;
    vec3 d = rel(dvec3(target), cam.pos);
    float wantYaw = atan2f(-d.x, d.y), wantPitch = atan2f(d.z, length(d.xy()));
    float dy = wrapAngle(wantYaw - cam.yaw), dp = wantPitch - cam.pitch;
    g.ctl.look = vec2(-Clamp(dy, -0.06f, 0.06f), Clamp(dp, -0.04f, 0.04f));
    (void)dt;
}

void testDrive(GameWorld& g, float dt) {
    const char* arg = Platform::argValue("holduptest");
    if (!arg || !World::gInteriors) return;
    Ped* pl = g.playerPed();
    if (!pl) return;
    const auto& defs = World::gInteriors->defs;
    if (!gT.init) {
        gT.init = true;
        gT.def = World::gInteriors->byName(arg);
        if (gT.def < 0) gT.def = World::gInteriors->byKind(World::IK_CONVENIENCE, 0);
        if (gT.def < 0) return;
        const InteriorDef& d = defs[gT.def];
        const World::InteriorMarker* out = d.marker(World::IM_DOOR_OUT);
        vec3 start = (out ? d.toWorld(out->pos) : d.toWorld(vec3(0.f, -3.f, 0.f))) - vec3(d.ay, 0.f) * 3.f;
        pl->pos = dvec3(start.x, start.y, g.groundHeight(start.x, start.y, start.z + 3.f));
        pl->yaw = atan2f(-d.ay.x, d.ay.y);
        pl->vel = vec3(0.f);
        g.rig.yaw = pl->yaw;
        g.rig.cut = true;
        g.populationOff = true;
        g.giveWeapon(g.player, WPN_PISTOL, 120);
        pl->weapon = WPN_PISTOL;
        g.pinfo.wanted = 0;
        g.pinfo.wantedHeat = 0.f;
        int ts = robberySlot(gT.def);
        if (ts >= 0) setFlag(g, kExRobbedDay + ts, 0);
        LOG("holduptest: '%s', player at %.1f %.1f", d.name.c_str(), start.x, start.y);
    }
    if (gT.def < 0) return;
    const InteriorDef& d = defs[gT.def];
    gT.t += dt;
    gT.stageT += dt;
    Controls& c = g.ctl;
    vec3 pp = pl->pos.toVec3();
    const World::InteriorMarker* entry = d.marker(World::IM_ENTRY);
    const World::InteriorMarker* ctr = d.marker(World::IM_COUNTER);
    vec3 door = entry ? d.toWorld(entry->pos) : d.toWorld(vec3(0.f, 1.2f, 0.f));
    vec3 counter = ctr ? d.toWorld(ctr->pos) : d.center();
    auto walkTo = [&](vec3 goal, float speed) {
        vec2 to = goal.xy() - pp.xy();
        if (length(to) < 0.5f) return true;
        float want = atan2f(-to.x, to.y);
        float dy = wrapAngle(want - g.rig.cam.yaw);
        c.look = vec2(-Clamp(dy, -0.06f, 0.06f), -Clamp(g.rig.cam.pitch + 0.1f, -0.02f, 0.02f));
        c.move = vec2(0.f, speed);
        return false;
    };
    auto next = [&](int s) {
        gT.stage = s;
        gT.stageT = 0.f;
    };
    switch (gT.stage) {
        case 0:   // settle outside, then walk in through the door
            if (gT.t < 3.f) break;
            if (gT.stageT > 0.f && gT.stageT < dt * 1.5f) g.requestScreenshot = shotName("outside");
            if (walkTo(door, 0.7f) || gT.stageT > 14.f) next(1);
            break;
        case 1:   // up to the counter
            if (walkTo(counter + (door - counter) * 0.25f, 0.6f) || gT.stageT > 12.f) next(2);
            break;
        case 2: {  // draw and aim at the clerk
            int ck = findClerk(g, gT.def);
            if (ck >= 0) steerLook(g, g.peds[ck].pos.toVec3() + vec3(0, 0, 1.3f), dt);
            c.aim.down = true;
            if (gS.phase != HP_NONE) {
                g.requestScreenshot = shotName("hands_up");
                next(3);
            } else if (gT.stageT > 15.f) {
                LOG("holduptest: no clerk reaction (clerk %d)", ck);
                next(6);
            }
            break;
        }
        case 3: {  // keep the gun on the clerk, shout once, watch the register being emptied
            if (gS.clerk >= 0) steerLook(g, g.peds[gS.clerk].pos.toVec3() + vec3(0, 0, 1.3f), dt);
            c.aim.down = true;
            if (gT.stageT > 2.f && gT.stageT < 2.f + dt * 1.5f) c.enter.pressed = true;
            if (gT.stageT > 4.5f && gT.stageT < 4.5f + dt * 1.5f) g.requestScreenshot = shotName("emptying");
            if (gS.phase == HP_HANDOVER || gS.phase == HP_ALARM || gS.phase == HP_ARMED || gS.phase == HP_NONE || gT.stageT > 40.f) {
                g.requestScreenshot = shotName(gS.phase == HP_HANDOVER ? "bag_on_counter" : "outcome");
                next(4);
            }
            break;
        }
        case 4:   // grab the bag
            if (gS.phase == HP_HANDOVER && gS.bag >= 0) {
                vec3 bag = gS.reg;
                if (walkTo(bag, 0.5f) || gS.paid || gT.stageT > 10.f) next(5);
            } else if (gT.stageT > 2.f) {
                next(5);
            }
            break;
        case 5:   // leave through the door and look back
            if (walkTo(door - vec3(d.ay, 0.f) * 5.f, 1.f) || gT.stageT > 16.f) {
                g.requestScreenshot = shotName("escape");
                next(6);
            }
            break;
        case 6:
            if (gT.stageT > 20.f && gT.stageT < 20.f + dt * 1.5f) {
                g.requestScreenshot = shotName("police");
                LOG("holduptest: done, wanted %d heat %.2f money %lld robberies %d", g.pinfo.wanted, g.pinfo.wantedHeat, g.pinfo.money, flag(g, kExRobberies));
            }
            break;
    }
}

}  // namespace holdups

namespace Interiors {
int storeRobberies(GameWorld& g) { return flag(g, holdups::kExRobberies); }
}  // namespace Interiors

}  // namespace Game
