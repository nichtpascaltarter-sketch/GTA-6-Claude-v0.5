// Story, Act 4 "Undertow": after either ending of Act 3. Sandoval was only the front man: the money behind Solaris Pier
// is Ines Sable of Sable Maritime, who runs the Port Isle terminal, and El Cuervo is back as her enforcer. Wake (the
// diner firebombed, a Cuervo caught), Box Numbers (the container yard at night), Blue Line (a meeting on the SkyLine and
// a K9 team), Clear Air (the airport forecourt and airside, a plane stopped on the runway), Gator Country (Sawgrass
// overwatch in first person, an airboat chase after El Cuervo) and King Tide (Sable's warehouse in a storm: a staged
// scene inside it, the chase to her ship and the last choice). The broadcast ending (Holt in a cell) and the leverage
// ending (Holt still wearing the badge, on Sable's side) change who is waiting where and what everybody says.
#include "missions.h"

namespace Game {
namespace Interiors {
bool ready(const char* name);   // interiors_game.cpp: built (and streaming in first when it is not)
}
namespace mu {

bool endedBroadcast(GameWorld& g) { return flag(g, EX_ENDING) != 2; }

// A scene staged inside an interior waits (a few seconds at most) until the interior has streamed in: people placed in
// an interior that is not built yet are pushed out by the building's shell
bool interiorWait(const char* name, float& waited, float dt) {
    if (!interiorStage(name).ok()) return false;
    waited += dt;
    return !Interiors::ready(name) && waited < 8.f;
}

// A scene about to play beside a vehicle that is burning out (a chase target stopped with its engine shot dead
// catches fire and goes up seven seconds later): the fire goes out, so the people in the scene are not blown across
// it halfway through. The engine barely turns over afterwards.
void calmFires(GameWorld& g, vec3 at, float r) {
    for (Vehicle& v : g.vehicles) {
        if (!v.used || v.exploded || v.fireTimer <= 0.f || ::length(v.sim.body.pos.toVec3() - at) > r) continue;
        v.fireTimer = 0.f;
        v.sim.engineHealth = Max(v.sim.engineHealth, 1.f);
        v.sim.health = Max(v.sim.health, 1.f);
    }
}

// A cast member's voice under another name (a returning face: Chuy speaks with the Cuervo lieutenant's voice)
void sayAs(GameWorld& g, int cast, int ped, const char* name, const std::string& text, float pause = 0.25f) {
    DialogueLine l = line(name, text, pedAlive(g, ped) ? ped : -1, kCast[cast].color);
    l.hasVoice = true;
    l.voice = castVoice(cast);
    l.spoken = castTags(cast) + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

// A suspect on foot gives up: floored, cornered, or held at gunpoint close by. Hands up, brain off. True once he has.
bool givesUp(GameWorld& g, int p, float aimRange = 15.f) {
    if (!pedAlive(g, p)) return false;
    Ped& s = g.peds[p];
    if (s.brain.type == BRAIN_NONE && s.animIn.stance == 5) return true;
    Ped* pl = g.playerPed();
    if (!pl) return false;
    float d = ::length(pedPos(g, p) - playerPos(g));
    bool aimed = false;
    if (pl->aiming && pl->weapon != WPN_FISTS && d < aimRange) {
        vec3 to = g.pedChestPos(s) - g.pedHeadPos(*pl);
        float along = dot(to, pl->aimDir);
        aimed = along > 0.f && ::length(to - pl->aimDir * along) < 1.8f;
    }
    bool cornered = d < 2.6f && pl->state == PS_ONFOOT;
    bool floored = s.state == PS_RAGDOLL || s.state == PS_GETUP;
    if (!(aimed || cornered || floored)) return false;
    if (s.vehicle >= 0) g.removePedFromVehicle(p, true);
    setIdle(g, p, 5);
    return true;
}

// Remove the tracked mission blip of one ped (others stay)
void unblipPed(int ped) {
    if (ped < 0) return;
    auto& list = mission_detail::gTracked;
    list.erase(std::remove_if(list.begin(), list.end(), [ped](const TrackedBlip& b) { return b.ped == ped; }), list.end());
}

// A flat point on the Port Isle yard pad (z from the pad)
vec3 yardPoint(GameWorld& g, float x, float y) { return vec3(x, y, groundAt(g, x, y, 12.f)); }

// Dry land in the Sawgrass that an airboat can reach: ground above the water line with open water within reach,
// searched in rings around `c` from minR out to maxR (the first hit on each ring in angle order from `phase`)
bool swampLand(GameWorld& g, vec2 c, float minR, float maxR, float phase, vec3& out) {
    for (float r = minR; r <= maxR; r += 12.f) {
        int n = Max(8, (int)(r * kTwoPi / 14.f));
        for (int k = 0; k < n; k++) {
            float a = phase + kTwoPi * k / n;
            vec2 q = c + vec2(cosf(a), sinf(a)) * r;
            if (g.map->isWater(q.x, q.y)) continue;
            float z = groundAt(g, q.x, q.y);
            if (z < 0.05f || z > 3.5f) continue;
            bool shore = false;
            for (int j = 0; j < 8 && !shore; j++) {
                vec2 w = q + vec2(cosf(j * 0.785f), sinf(j * 0.785f)) * 14.f;
                shore = isWaterAt(g, w, 0.6f);
            }
            if (!shore) continue;
            out = vec3(q, z);
            return true;
        }
    }
    return false;
}

// ==================================================================================================================
// Act 4-1: "Wake" (Mari, with Dex). Lucha's dinner for the crew; a Cuervo car firebombs the diner's front. Chase it,
// catch the thrower alive, and hear the name behind it all: Ines Sable.
class MissionWake : public StoryMission {
public:
    int lucha = -1, tomas = -1, kit = -1, dex = -1, cuervoCar = -1, nando = -1, driver = -1;
    bool inDiner = false, waitingIn = false;
    float waitT = 0.f;
    vec3 front;
    const char* title() const override { return "Wake"; }
    const char* brief() const override {
        return "Weeks after Solaris One fell, Mama Lucha throws a dinner at the diner for the whole crew. Not everybody in Porto Sol is "
               "celebrating.";
    }
    long long reward() const override { return 8000; }

    void spawnCuervoCar(GameWorld& g, vec3 pos, float yaw) {
        int model = pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SEDAN}, 7);
        std::vector<int> crew;
        cuervoCar = spawnCrewCar(g, model, pos, yaw, castChar(g, CAST_THUG_B), 1, castChar(g, CAST_THUG_C), FAC_ENEMY, WPN_PISTOL, 0.2f, &crew);
        if (cuervoCar < 0) return;
        g.vehicles[cuervoCar].color0 = lin(0.45f, 0.04f, 0.05f);
        driver = crew.size() > 0 ? crew[0] : -1;
        nando = crew.size() > 1 ? crew[1] : -1;
        if (driver >= 0) {
            g.peds[driver].voice = castVoice(CAST_THUG_B);
            enemies.push_back(driver);
        }
        if (nando >= 0) {
            g.peds[nando].voice = castVoice(CAST_THUG_C);
            g.peds[nando].faction = FAC_CIVILIAN;   // the one who talks: Dex does not shoot at him
            g.peds[nando].maxHealth = g.peds[nando].health = 160.f;
        }
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        score(SC_NOIR, 0.3f, 30);
        bool bc = endedBroadcast(g);
        front = placeOffset(g, P.diner, 0.f, 0.2f);
        int model = pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SEDAN}, 3);
        if (checkpoint >= 1) {
            playerCar = placePlayer(g, curbOffset(g, P.diner, 8.f), P.diner.curbYaw, model, lin(0.55f, 0.06f, 0.2f));
            dex = spawnPartner(g, 1, playerPos(g), 0.f, WPN_PISTOL);
            if (dex >= 0 && playerCar >= 0) g.warpPedIntoVehicle(dex, playerCar, 1);
            buddy = dex;
            float yaw = P.diner.curbYaw;
            vec3 sp = curbOffset(g, P.diner, 70.f, &yaw);
            spawnCuervoCar(g, sp, yaw);
            beginChase(g);
            return;
        }
        // the dinner is staged inside the diner: at the door until it has streamed in
        if (interiorStage("Mama Lucha's").ok() && !Interiors::ready("Mama Lucha's")) {
            placePlayer(g, placeOffset(g, P.diner, -1.f, 0.5f), P.diner.yaw);
            g.fadeAlpha = 1.f;   // (black until the scene is set)
            g.fadeOut(100.f);
            waitingIn = true;
            return;
        }
        stageDinner(g);
    }

    void stageDinner(GameWorld& g) {
        const Places& P = gPlaces;
        bool bc = endedBroadcast(g);
        // the dinner inside the diner when the world has it: Lucha behind the counter, the crew along it
        InteriorStage in = interiorStage("Mama Lucha's");
        vec3 counterL, waiterL;
        inDiner = in.ok() && in.marker(World::IM_COUNTER, counterL) && in.scenario(World::SR_WAITER, 0, waiterL);
        if (inDiner) {
            lucha = spawnCast(g, CAST_LUCHA, in.at(vec3(counterL.x, waiterL.y, 0.f)), in.yaw(kPi), FAC_FRIEND);
            tomas = spawnCast(g, CAST_TOMAS, in.at(counterL + vec3(1.3f, 0.35f, 0.f)), in.yaw(0.f), FAC_FRIEND);
            kit = spawnCast(g, CAST_KIT, in.at(counterL + vec3(-1.4f, 0.4f, 0.f)), in.yaw(0.f), FAC_FRIEND);
            dex = spawnPartner(g, 1, in.at(in.entry() + vec3(0.6f, 0.6f, 0.f)), in.yaw(0.f), WPN_PISTOL);
            placePlayer(g, in.at(counterL), in.yaw(0.f));
        } else {
            lucha = spawnCast(g, CAST_LUCHA, P.diner.door, P.diner.yaw + kPi, FAC_FRIEND);
            tomas = spawnCast(g, CAST_TOMAS, placeOffset(g, P.diner, 2.f, 2.f), P.diner.yaw + kPi, FAC_FRIEND);
            kit = spawnCast(g, CAST_KIT, placeOffset(g, P.diner, -2.f, 2.f), P.diner.yaw + kPi, FAC_FRIEND);
            dex = spawnPartner(g, 1, placeOffset(g, P.diner, 3.f, 1.f), P.diner.yaw + kPi * 0.5f, WPN_PISTOL);
            placePlayer(g, placeOffset(g, P.diner, -1.f, 0.5f), P.diner.yaw);
        }
        buddy = dex;
        for (int p : {lucha, tomas, kit})
            if (p >= 0) g.peds[p].invincible = true;
        provideRide(g, P.diner, 12.f);
        vec3 lp = pedPos(g, lucha), tp = pedPos(g, tomas), kp = pedPos(g, kit), dp = pedPos(g, dex), mp = playerPos(g);
        for (int p : {lucha, tomas, kit, dex}) facePed(g, p, mp);
        std::vector<CutsceneShot> shots;
        if (inDiner) {
            float side = counterL.x > (in.d->x0 + in.d->x1) * 0.5f ? -1.f : 1.f;
            shots.push_back(shotRoom(in, vec3(counterL.x + side * 4.4f, Max(0.9f, counterL.y - 2.8f), 2.2f), lp, mp, 5.f));
        } else {
            establish(g, shots, lp, P.diner.yaw, 30.f, 11.f, 4.f);
        }
        shots.push_back(shotTwo(lp, mp, 6.f));
        shots.push_back(shotTwo(tp, kp, 6.f, 4.f, 42.f, -1.f));
        shots.push_back(shotTwo(kp, mp, 6.f));
        shots.push_back(shotOver(mp, dp, 6.f, -1.f));
        g.mCutscene(shots);
        if (bc) {
            say(g, CAST_LUCHA, lucha, "[happy]Sit, sit! The first dinner in a diner that is really mine. The bank has nothing to say about it anymore.");
            say(g, CAST_TOMAS, tomas, "[happy]Kit's show was on every radio on the block tonight. Old Rosa made me turn it up so the whole street could hear.");
            say(g, CAST_KIT, kit, "[happy:0.6]Holt's bail hearing is Monday. Sandoval's lawyers are sweating through their very expensive suits.");
            sayMe(g, "[calm]Tonight nobody talks about lawyers. Lucha's rules.");
            sayP(g, 1, dex, "[happy:0.3]I only came for the pozole.");
        } else {
            say(g, CAST_LUCHA, lucha, "[happy]Sit, sit! The yard is paid off, the deeds are home. Tonight we eat like kings.");
            say(g, CAST_TOMAS, tomas, "[happy]Two million dollars, Mari. Dad would have lost his mind. Then he'd have bought a bigger boat.");
            say(g, CAST_KIT, kit, "[sad:0.4]And Captain Holt still drives past the yard every night. Slowly. Smiling at the windows.");
            sayMe(g, "[calm]Let her look. The copies are in three safe deposit boxes.");
            sayP(g, 1, dex, "[happy:0.3]Four. I didn't tell you about the fourth.");
        }
        say(g, CAST_TOMAS, tomas, "[calm]Hey... that red car out there. That's the third time it's gone round the block.");
    }

    void attack(GameWorld& g) {
        const Place& D = gPlaces.diner;
        float yaw = D.curbYaw;
        vec3 sp = curbOffset(g, D, -14.f, &yaw);
        spawnCuervoCar(g, sp, yaw);
        // the bottle: glass, a burst of flame on the sidewalk under the window
        vec3 win = D.door + vec3(D.streetDir * 2.5f, 1.4f);
        spawnFx(FX_GLASS, dvec3(win), vec3(-D.outward, 0.2f), 14, 1.f);
        spawnFx(FX_FIRE, dvec3(front), vec3(0.f, 0.f, 1.f), 20, 1.4f);
        g.startFire(dvec3(front), 1.8f, 30.f);
#ifdef HAVE_AUDIO
        Audio::play(Audio::SFX_GLASS_BREAK, win, 1.f);
        Audio::play(Audio::SFX_EXPLOSION_SMALL, front, 0.7f);
#endif
        say(g, CAST_TOMAS, tomas, "[shout]Cuervos! They threw something through the window!");
        say(g, CAST_LUCHA, lucha, "[angry]My window! Tomas, the extinguisher! Mari, don't you let them get away!");
        sayP(g, 1, dex, "[shout]They're pulling out. The car's out front, come on!");
        beginChase(g);
    }

    void beginChase(GameWorld& g) {
        if (cuervoCar >= 0) {
            ScriptDriver& d = driveRoad(g, cuervoCar, gPlaces.diner.curb.xy() + gPlaces.diner.streetDir * 950.f, 21.f, true);
            d.rubberPed = g.player;
            d.rubberGap = 50.f;
            g.mBlipVehicle(cuervoCar, UI::BLIP_ENEMY);
        }
        setFollow(g, dex, g.player);
        g.mObjective("Chase the ~r~Cuervo car~s~.");
        score(SC_CHASE, 0.85f, 30);
        cp(g, 1);
        setStage(1);
    }

    void interrogate(GameWorld& g) {
        g.mClearBlips();
        for (int e : enemies)
            if (pedAlive(g, e)) setFlee(g, e, g.player);
        if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
        vec3 np = pedPos(g, nando);
        calmFires(g, np, 20.f);
        vec2 away = normalize(playerPos(g).xy() - np.xy() + vec2(1e-3f, 0.f));
        placePlayer(g, np + vec3(away * 2.2f, 0.f), yawTo(np.xy() + away * 2.2f, np.xy()));
        if (dex >= 0) {
            if (g.peds[dex].vehicle >= 0) g.removePedFromVehicle(dex, false);
            vec2 sidev(away.y, -away.x);
            placePed(g, dex, np + vec3(away * 2.6f + sidev * 1.8f, 0.f), 0.f);
            setIdle(g, dex, 0);
            facePed(g, dex, np);
        }
        facePed(g, nando, playerPos(g));
        vec3 mp = playerPos(g), dp = pedPos(g, dex);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotOver(mp, np, 6.f));
        shots.push_back(shotTwo(np, mp, 6.f));
        shots.push_back(shotOver(np, mp, 6.f, -1.f));
        shots.push_back(shotTwo(dp, mp, 6.f, 4.f, 42.f, -1.f));
        g.mCutscene(shots);
        sayAs(g, CAST_THUG_C, nando, "Nando", "[scared]Okay, okay! Don't shoot! It wasn't my idea, I swear on my mother!");
        sayMe(g, "[angry]You set fire to my family's diner. Whose idea was it?");
        sayAs(g, CAST_THUG_C, nando, "Nando", "[scared]El Cuervo's. He's back, man. Out since all the noise. He works for Sable now.");
        sayMe(g, "[calm]Who's Sable?");
        sayAs(g, CAST_THUG_C, nando, "Nando",
              "[scared:0.6]Ines Sable. Sable Maritime. The cranes, the trucks, the boats at Port Isle, all of it. Sandoval was her front man. "
              "She still wants the waterfront.");
        if (endedBroadcast(g))
            sayAs(g, CAST_THUG_C, nando, "Nando", "[scared:0.5]With Holt in a cell she brought her own security. Ex-military guys. With dogs.");
        else
            sayAs(g, CAST_THUG_C, nando, "Nando", "[scared:0.5]And your captain? Holt drinks coffee with her every Tuesday. You think your little tape scared anybody?");
        sayP(g, 1, dex, "[calm]His phone's full of numbers. Container numbers, if I had to guess.");
        sayMe(g, "[angry:0.5]Get out of here, Nando. If I see you on my street again, the Cuervos won't be your biggest problem.");
        next();
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (waitingIn) {
            if (interiorWait("Mama Lucha's", waitT, dt)) return MS_RUNNING;
            waitingIn = false;
            stageDinner(g);
            g.fadeIn(1.3f);
        }
        if (allyDown(g, dex, "Dex")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) attack(g);
                break;
            case 1: {
                updateBuddy(g);
                if (!pedAlive(g, nando)) return fail("Nando is dead. Whatever he knew died with him.");
                if (cuervoCar < 0) return fail("The Cuervos got away.");
                float dist = ::length(vehPos(g, cuervoCar) - playerPos(g));
                int drv = g.driverOf(cuervoCar);
                bool stopped = vehicleDisabled(g, cuervoCar) || g.vehicles[cuervoCar].sim.health < 420.f || drv < 0 || !pedAlive(g, drv);
                ScriptDriver* sd = driverFor(cuervoCar);
                if ((sd && sd->done && dist > 130.f) || dist > 520.f) return fail("The Cuervos got away.");
                if (stageTime > 6.f && stageTime < 6.1f) sayP(g, 1, dex, "[shout]Get us alongside. Or behind. Just don't lose them.");
                if (stageTime > 24.f && stageTime < 24.1f) sayMe(g, "[angry:0.6]Nobody burns Lucha's diner. Nobody.");
                if (stopped || (sd && sd->done)) {
                    releaseDriver(g, cuervoCar);
                    if (driver >= 0 && pedAlive(g, driver)) {
                        if (g.peds[driver].vehicle >= 0) g.removePedFromVehicle(driver, true);
                        setCombat(g, driver, g.player, 0.2f);
                    }
                    if (g.peds[nando].vehicle >= 0) g.removePedFromVehicle(nando, true);
                    setFlee(g, nando, g.player);
                    sayAs(g, CAST_THUG_C, nando, "Nando", "[scared]Run! Just run!");
                    g.mClearBlips();
                    g.mBlipPed(nando, UI::BLIP_ENEMY);
                    blipEnemies(g);
                    g.mObjective("Catch ~r~Nando~s~ alive. Corner him or hold him at gunpoint.");
                    next();
                }
                break;
            }
            case 2:
                updateBuddy(g);
                if (!pedAlive(g, nando)) return fail("Nando is dead. Whatever he knew died with him.");
                if (::length(pedPos(g, nando) - playerPos(g)) > 230.f) return fail("Nando got away.");
                if (givesUp(g, nando)) interrogate(g);
                break;
            case 3:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFlee(g, nando, g.player);
                    setFollow(g, dex, g.player);
                    goTo(g, gPlaces.diner.curb, 6.f, "Get back to ~y~the diner~s~.", true);
                    score(SC_NOIR, 0.35f, 30);
                    next();
                }
                break;
            case 4:
                updateBuddy(g);
                if (arrived(g)) {
                    clearGoal(g);
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    if (dex >= 0 && g.peds[dex].vehicle >= 0) g.removePedFromVehicle(dex, true);
                    const Place& D = gPlaces.diner;
                    if (lucha >= 0) placePed(g, lucha, D.door - vec3(D.outward * 1.2f, 0.f), D.yaw + kPi);
                    else lucha = spawnCast(g, CAST_LUCHA, D.door - vec3(D.outward * 1.2f, 0.f), D.yaw + kPi, FAC_FRIEND);
                    if (lucha >= 0) {
                        g.peds[lucha].invincible = true;
                        setIdle(g, lucha, 0);
                    }
                    vec3 lp = pedPos(g, lucha), mp = playerPos(g);
                    facePed(g, lucha, mp);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(lp, mp, 6.f));
                    shots.push_back(shotOver(mp, lp, 6.f));
                    g.mCutscene(shots);
                    say(g, CAST_LUCHA, lucha, "[calm]The fire is out. The window is the worst of it. Glass, I can buy.");
                    sayMe(g, "[calm]El Cuervo's back, Lucha. And there's somebody bigger behind him. A woman who owns the port.");
                    say(g, CAST_LUCHA, lucha, "[angry:0.4]Then you go and find her, mija. And you tell her Calle Luna is not for sale. Not to anybody.");
                    next();
                }
                break;
            case 5:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "El Cuervo is back, working for Ines Sable of Sable Maritime, the money behind Solaris Pier. Nando's phone is "
                                       "full of container numbers. Rook wants Dex at the yard.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                // (the car gets clear of the block first: the talk with Nando away from the burning window and its smoke)
                if (cuervoCar >= 0 && t.stageTime > 1.5f &&
                    (::length(vehPos(g, cuervoCar).xy() - gPlaces.diner.door.xy()) > 150.f || t.stageTime > 25.f)) {
                    g.vehicles[cuervoCar].sim.engineHealth = 0.f;
                    g.vehicles[cuervoCar].sim.health = 350.f;
                }
                break;
            case 2:
                if (t.stageTime > 1.f && pedAlive(g, nando)) {
                    t.exitVehicle();
                    t.teleport(pedPos(g, nando) + vec3(2.5f, 0.f, 0.f), 0.f);
                    g.knockDown(nando, vec3(0.f, 50.f, 20.f));
                }
                if (t.stageTime > 1.2f) t.killEnemies();
                break;
            case 4: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 4-2: "Box Numbers" (Dex). Nando's phone held three Sable Maritime container numbers. Into the Port Isle yard at
// night, between the stacks and down the lanes, to open all three; out to Rook's boat, round the terminal and south
// across the open water to Key Coral Marina with harbor security on the wake.
class MissionBoxNumbers : public StoryMission {
public:
    int rook = -1, boat = -1;
    StealthGroup yard;
    bool alarmed = false;
    vec3 boxes[3];
    bool opened[3] = {false, false, false};
    HoldAction hold[3];
    int found = 0;
    vec3 boatWater, boatQuay, entry;
    std::vector<int> chasers;
    float repath = 0.f, waitT = 0.f;
    bool waitingIn = false;
    const char* title() const override { return "Box Numbers"; }
    const char* brief() const override {
        return "Nando's phone held three Sable Maritime container numbers. Get into the Port Isle container yard at night, open all three "
               "and get out by water.";
    }
    long long reward() const override { return 12000; }

    void spawnYard(GameWorld& g) {
        struct Route {
            vec2 a, b;
        };
        // the gap between the two stack columns (container doors face it) and the lanes between the blocks
        const Route routes[7] = {{vec2(4289.f, -330.f), vec2(4289.f, -470.f)}, {vec2(4289.f, -500.f), vec2(4289.f, -640.f)},
                                 {vec2(4180.f, -415.f), vec2(4270.f, -415.f)}, {vec2(4310.f, -471.f), vec2(4420.f, -471.f)},
                                 {vec2(4289.f, -545.f), vec2(4289.f, -545.f)}, {vec2(4150.f, -608.f), vec2(4270.f, -608.f)},
                                 {vec2(4310.f, -358.f), vec2(4440.f, -358.f)}};
        for (int i = 0; i < 7; i++) {
            vec3 a = yardPoint(g, routes[i].a.x, routes[i].a.y), b = yardPoint(g, routes[i].b.x, routes[i].b.y);
            int e = spawnCast(g, i % 3 == 2 ? CAST_DOCKER : (i & 1 ? CAST_GUARD_B : CAST_GUARD_A), a, i == 4 ? 0.f : yawTo(a.xy(), b.xy()), FAC_ENEMY);
            if (e < 0) continue;
            arm(g, e, i % 3 == 0 ? WPN_SMG : WPN_PISTOL);
            g.peds[e].brain.accuracy = 0.28f;
            setIdle(g, e, 0);
            enemies.push_back(e);
            std::vector<vec3> patrol;
            if (::length(a - b) > 1.f) patrol = {a, b};
            yard.add(g, e, patrol, 17.f);
        }
    }

    void raiseAlarm(GameWorld& g) {
        if (alarmed) return;
        alarmed = true;
        yard.raise(g);
        blipEnemies(g);
        if (yard.spotter >= 0) say(g, CAST_GUARD_A, yard.spotter, "[shout]Somebody's in the stacks! Lane four! Lights on him!");
        radioLine(g, CAST_KIT, "[scared:0.5]That's the whole security net lighting up. Get what you came for and get out.");
        showMeter(g, "DETECTION", 0.f);
        score(SC_CHASE, 1.f, 31);
    }

    void showNext(GameWorld& g) {
        g.mClearMarkers();
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < 3; i++) {
            if (opened[i]) continue;
            g.mMarker(dvec3(boxes[i]), 1.3f, vec3(0.3f, 0.8f, 1.f));
            float d = ::length(boxes[i] - playerPos(g));
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0) g.mTarget(boxes[best].xy());
        g.mObjective(StrFormat("Open the three ~b~Sable containers~s~ in the stacks (%d/3). Stay out of sight.", found));
    }

    void beginYard(GameWorld& g) {
        spawnYard(g);
        showNext(g);
        score(SC_STEALTH, 0.45f, 31);
        cp(g, 1);
        setStage(2);
    }

    void openBox(GameWorld& g, int i) {
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_CAMERA_SHUTTER, 0.9f);
#endif
        static const char* const kDex[3] = {
            "[whisper:0.4]Fish crates. Full of rifles, packed in ice so the dogs don't smell the oil. Same stamp as the Sawgrass guns.",
            "[whisper:0.4]Cash. Shrink-wrapped and stacked like drywall. Somebody is paying a lot of people for a lot of something.",
            "[whisper:0.4]Paperwork. Sable Maritime manifests... and a map. The waterfront from the river mouth to Calle Luna, colored in as terminal expansion.",
        };
        static const char* const kKit[3] = {
            "[whisper:0.5]Photographing. That's box one. Same rifles the Cuervos used on the boatyard.",
            "[whisper:0.5]Cuervo payroll, and then some. That's box two.",
            "[scared:0.4]That's the boatyard. And Lucha's whole block. She's going to pave it for parking containers.",
        };
        sayMe(g, kDex[i]);
        radioLine(g, CAST_KIT, kKit[i]);
    }

    int spawnSecurityBoat(GameWorld& g, vec3 around, u32 seed) {
        vec3 w;
        if (!findWater(g, around.xy(), 2.f, w, 120.f)) return -1;
        int v = spawnBoat(g, w, 0.f, lin(0.85f, 0.85f, 0.88f), seed);
        if (v < 0) return -1;
        std::vector<int> crew;
        for (int s = 0; s < 2; s++) {
            int e = spawnCast(g, s ? CAST_GUARD_B : CAST_GUARD_A, w, 0.f, FAC_ENEMY);
            if (e < 0) continue;
            if (s >= (int)g.vassets[g.vehicles[v].model].spec.seats.size()) {
                g.despawnPed(e);
                continue;
            }
            g.warpPedIntoVehicle(e, v, s);
            arm(g, e, WPN_SMG);
            g.peds[e].brain.accuracy = 0.18f;
            if (s > 0) setCombat(g, e, g.player, 0.18f);
            enemies.push_back(e);
        }
        chasers.push_back(v);
        g.mBlipVehicle(v, UI::BLIP_ENEMY);
        return v;
    }

    void beginRun(GameWorld& g) {
        g.mClearBlips();
        g.mClearMarkers();
        if (boat >= 0) g.mBlipVehicle(boat, UI::BLIP_BOAT);
        spawnSecurityBoat(g, boatWater + vec3(160.f, -40.f, 0.f), 3);
        spawnSecurityBoat(g, boatWater + vec3(220.f, 30.f, 0.f), 5);
        goTo(g, gPlaces.keyCoralWater, 18.f, "Lose them on the way south to ~y~Key Coral Marina~s~.", true);
        radioLine(g, CAST_ROOK, "[shout]Harbor security boats! Round the terminal and south to Key Coral. My cousin keeps a slip at the marina.");
        score(SC_CHASE, 1.f, 31);
        cp(g, 2);
        setStage(4);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        if (g.env->timeOfDay > 5.f && g.env->timeOfDay < 21.f) g.env->timeOfDay = 22.5f;
        score(SC_STEALTH, 0.35f, 31);
        boxes[0] = yardPoint(g, 4279.6f, -380.35f);
        boxes[1] = yardPoint(g, 4298.6f, -506.05f);
        boxes[2] = yardPoint(g, 4279.6f, -569.65f);
        entry = yardPoint(g, 4078.f, -300.f);
        vec3 w;
        if (!findWater(g, vec2(4180.f, 150.f), 2.5f, w, 220.f)) w = P.portWater;
        boatWater = w;
        boatQuay = yardPoint(g, Clamp(w.x, 4000.f, 4560.f), 96.f);
        if (checkpoint >= 2) {
            boat = spawnBoat(g, boatWater, kPi * 0.5f, lin(0.2f, 0.35f, 0.55f), 2);
            placePlayer(g, boatQuay, 0.f);
            if (boat >= 0) g.warpPedIntoVehicle(g.player, boat, 0);
            rook = spawnCast(g, CAST_ROOK, boatWater, 0.f, FAC_FRIEND);
            if (rook >= 0 && boat >= 0) g.warpPedIntoVehicle(rook, boat, 1);
            beginRun(g);
            return;
        }
        if (checkpoint >= 1) {
            placePlayer(g, entry, -kPi * 0.5f);
            beginYard(g);
            return;
        }
        // the briefing is staged in Rook's garage: outside until it has streamed in
        if (interiorStage("Rook's Garage").ok() && !Interiors::ready("Rook's Garage")) {
            placePlayer(g, placeOffset(g, P.rookShop, 0.f, 0.5f), P.rookShop.yaw);
            g.fadeAlpha = 1.f;   // (black until the scene is set)
            g.fadeOut(100.f);
            waitingIn = true;
            return;
        }
        stageBriefing(g);
    }

    void stageBriefing(GameWorld& g) {
        const Places& P = gPlaces;
        // the briefing in Rook's garage when the world has it
        InteriorStage in = interiorStage("Rook's Garage");
        vec3 liftL;
        bool inside = in.ok() && in.marker(World::IM_CAR, liftL);
        if (inside) {
            vec3 f = vec3(liftL.x, liftL.y - 4.4f, 0.f);
            rook = spawnCast(g, CAST_ROOK, in.at(f + vec3(1.2f, 1.0f, 0.f)), in.yaw(kPi), FAC_FRIEND);
            placePlayer(g, in.at(f + vec3(-0.6f, -0.7f, 0.f)), in.yaw(0.f));
        } else {
            rook = spawnCast(g, CAST_ROOK, P.rookShop.door, P.rookShop.yaw + kPi, FAC_FRIEND);
            placePlayer(g, placeOffset(g, P.rookShop, 0.f, 0.5f), P.rookShop.yaw);
        }
        provideRide(g, P.rookShop, -14.f);
        vec3 rp = pedPos(g, rook), dp = playerPos(g);
        facePed(g, rook, dp);
        std::vector<CutsceneShot> shots;
        if (inside) shots.push_back(shotRoom(in, vec3(liftL.x + (liftL.x > (in.d->x0 + in.d->x1) * 0.5f ? -4.6f : 4.6f), 1.1f, 2.4f), rp, dp, 5.f));
        else establish(g, shots, rp, P.rookShop.yaw, 34.f, 14.f, 4.f);
        shots.push_back(shotTwo(rp, dp, 7.f));
        shots.push_back(shotOver(dp, rp, 6.f));
        shots.push_back(shotMove(vec3(4150.f, -260.f, 45.f), vec3(4290.f, -470.f, 10.f), vec3(4200.f, -300.f, 38.f), vec3(4290.f, -500.f, 8.f), 6.f, 50.f));
        g.mCutscene(shots);
        say(g, CAST_ROOK, rook, "[calm]Kit cracked the kid's phone. Three container numbers, all Sable Maritime, all sitting in the Port Isle stacks.");
        say(g, CAST_ROOK, rook, "[calm]Sable's boxes come in full and go out full, and the weight never matches the paperwork. I want to know why.");
        sayMe(g, "[calm]Night shift at the port. Security walks the lanes between the stacks.");
        if (endedBroadcast(g)) say(g, CAST_ROOK, rook, "[happy:0.3]Security is private now. No more Holt. These guys are paid better and they shoot straighter.");
        else say(g, CAST_ROOK, rook, "[angry:0.3]And Holt's people patrol the bridge. Don't give her an excuse, Dex.");
        say(g, CAST_ROOK, rook, "[calm]Open all three, take pictures, then walk off the north quay. I'll be in a boat. Try not to swim far.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (waitingIn) {
            if (interiorWait("Rook's Garage", waitT, dt)) return MS_RUNNING;
            waitingIn = false;
            stageBriefing(g);
            g.fadeIn(1.3f);
        }
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    goTo(g, entry, 18.f, "Get into the ~y~Port Isle~s~ container yard.", false, false);
                    next();
                }
                break;
            case 1:
                if (arrived(g) || (g.playerAt(vec2(4200.f, -420.f), 260.f) && playerPos(g).x > 4040.f)) {
                    clearGoal(g);
                    if (g.playerVehicle() >= 0 && g.hudHelpTimer <= 0.f) g.help("Leave the car. The yard is quieter on foot.", 4.f);
                    beginYard(g);
                }
                break;
            case 2: {
                if (!alarmed && yard.update(g, dt)) raiseAlarm(g);
                showMeter(g, "DETECTION", alarmed ? 0.f : yard.meter);
                for (int i = 0; i < 3; i++) {
                    if (opened[i]) continue;
                    if (hold[i].update(g, boxes[i], 1.9f, 2.2f, "cut the seal and look inside", dt)) {
                        opened[i] = true;
                        found++;
                        openBox(g, i);
                        showNext(g);
                    }
                }
                if (found >= 3) {
                    setFlag(g, EX_ACT4_SABLE, alarmed ? 2 : 1);
                    showMeter(g, "DETECTION", 0.f);
                    boat = spawnBoat(g, boatWater, kPi * 0.5f, lin(0.2f, 0.35f, 0.55f), 2);
                    rook = spawnCast(g, CAST_ROOK, boatWater, 0.f, FAC_FRIEND);
                    if (rook >= 0) g.peds[rook].invincible = true;
                    if (rook >= 0 && boat >= 0) g.warpPedIntoVehicle(rook, boat, 1);
                    if (boat >= 0) g.mBlipVehicle(boat, UI::BLIP_BOAT);
                    radioLine(g, CAST_ROOK, "[calm]Got all three? Good. I'm in a boat off the north quay. Jump in, the water's fine. Mostly.");
                    goTo(g, boatQuay, 4.f, "Get to ~y~Rook's boat~s~ off the north quay.");
                    next();
                }
                break;
            }
            case 3:
                if (boat >= 0 && !vehicleAlive(g, boat)) return fail("Rook's boat was destroyed.");
                if (boat >= 0 && g.playerInVehicle(boat)) beginRun(g);
                else if (boat >= 0 && g.playerAt(boatQuay.xy(), 16.f) && g.hudHelpTimer <= 0.f) g.help("Jump into the water and climb into ~b~Rook's boat~s~.", 3.f);
                break;
            case 4: {
                if (vehicleLost(g, boat, "boat")) return MS_FAILED;
                if (abandoned(g, boat, 120.f, "boat")) return MS_FAILED;
                repath -= dt;
                if (repath <= 0.f) {
                    repath = 2.5f;
                    for (int v : chasers) {
                        if (!vehicleAlive(g, v) || g.driverOf(v) < 0 || !pedAlive(g, g.driverOf(v))) {
                            releaseDriver(g, v);
                            continue;
                        }
                        std::vector<vec3> wps = {vehPos(g, v), playerPos(g)};
                        RoutePath path;
                        buildWaypointPath(wps, path, 25.f, false);
                        ScriptDriver& d = addDriver(g, v, path, 22.f, DRV_WATER);
                        d.stopAtEnd = false;
                    }
                }
                if (stageTime > 8.f && stageTime < 8.1f) sayMe(g, "[shout]They're gaining! Rook, shoot back or something!");
                if (stageTime > 9.f && stageTime < 9.1f) say(g, CAST_ROOK, rook, "[angry]I am shooting back! I'm just very bad at it!");
                if (arrived(g) && g.playerInVehicle(boat)) {
                    clearGoal(g);
                    g.mClearBlips();
                    for (int v : chasers) releaseDriver(g, v);
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    say(g, CAST_ROOK, rook, "[calm]Rifles, cash, and a map that turns Calle Luna into a parking lot for containers. Sable plays big.");
                    phoneLine(g, CAST_KIT, "[calm]One more name keeps showing up on the manifests. Hollis Pruitt. Sable's accountant. He signs everything.");
                    sayMe(g, "[calm]Then we find Mr. Pruitt.");
                    next();
                }
                break;
            }
            case 5:
                if (!g.mTalking()) {
                    g.storyBriefText = "The Sable containers held Cuervo rifles, cash and a plan to pave the Calle Luna waterfront. Kit is following "
                                       "Sable's courier: he meets El Cuervo's people on the SkyLine.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        showMeter(g, "DETECTION", 0.f);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1: testGoal(g, t, dt, 60.f); break;
            case 2:
                if (t.stageTime > 0.8f)
                    for (int i = 0; i < 3; i++)
                        if (!opened[i]) {
                            if (::length(playerPos(g).xy() - boxes[i].xy()) > 1.2f) t.teleport(boxes[i], 0.f);
                            break;
                        }
                break;
            case 3:
                if (t.stageTime > 0.8f && boat >= 0) t.enter(boat);
                break;
            case 4:
                if (t.stageTime > 1.5f) {
                    vec3 w;
                    findWater(g, gPlaces.keyCoralWater.xy(), 1.2f, w, 60.f);
                    t.teleport(w, 0.f);
                    t.stopVehicle();
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 4-3: "Blue Line" (Mari). Chuy meets Sable's courier on the SkyLine. Ride their train from Civic Center, hear the
// plan, catch the courier at the next station for his envelope, then shake off the K9 team that comes for Mari (Sable's
// private security after the broadcast, Holt's own dog unit after the deal).
class MissionBlueLine : public StoryMission {
public:
    int chuy = -1, voss = -1, vossCar = -1, envelope = -1;
    int station = -1, side = 0, train = -1, st2 = -1, sd2 = 0;
    u32 trainUid = 0;
    int phase = 0;        // 0 waiting on the platform, 1 boarding, 2 riding, 3 off at the next station, 4 on the street
    PedPath walk;
    vec3 platform, stairFoot;
    float waitT = 0.f, boardT = 0.f, stopT = 0.f, spotT = 0.f;
    bool heard = false, spotted = false, vossDriving = false;
    int handler = -1, dog = -1;
    u32 dogUid = 0;
    float k9T = 0.f;
    const char* title() const override { return "Blue Line"; }
    const char* brief() const override {
        return "Chuy, El Cuervo's lieutenant, meets Sable's courier on the SkyLine. Ride their train, find out what Sable is planning and take "
               "the courier's envelope.";
    }
    long long reward() const override { return 10000; }

    const World::MetroStation& stationAt(int i) const { return World::gTransit->metro.stations[i]; }

    void spawnPair(GameWorld& g) {
        if (chuy >= 0) return;
        vec3 a, b;
        if (station >= 0) {
            const World::MetroStation& S = stationAt(station);
            float L = metroSideSign(side);
            a = S.local(9.f, L * 4.8f, S.platformZ());
            b = S.local(10.4f, L * 4.6f, S.platformZ());
        } else {
            a = placeOffset(g, gPlaces.policeHq, 40.f, 0.3f);
            b = placeOffset(g, gPlaces.policeHq, 41.2f, 0.3f);
        }
        chuy = spawnCast(g, CAST_THUG_A, a, 0.f, FAC_CIVILIAN);
        voss = spawnCast(g, CAST_VOSS, b, 0.f, FAC_CIVILIAN);
        if (chuy >= 0) {
            arm(g, chuy, WPN_PISTOL);
            g.peds[chuy].brain.accuracy = 0.22f;
            g.peds[chuy].maxHealth = g.peds[chuy].health = 180.f;
            facePed(g, chuy, b);
            setIdle(g, chuy, 7);
        }
        if (voss >= 0) {
            g.peds[voss].maxHealth = g.peds[voss].health = 220.f;
            facePed(g, voss, a);
            setIdle(g, voss, 7);
        }
    }

    void beginPlatform(GameWorld& g) {
        spawnPair(g);
        g.mClearMarkers();
        g.mClearTarget();
        g.mBlipPed(chuy, UI::BLIP_ENEMY);
        g.mBlipPed(voss, UI::BLIP_ENEMY);
        g.mObjective("Get on the same train as ~r~Chuy~s~ and ~r~Voss~s~. Keep your distance until then.");
        g.help("SkyLine trains come by about every three minutes. Blend in with the commuters while you wait.", 6.f);
        score(SC_STEALTH, 0.4f, 32);
        cp(g, 1);
        phase = 0;
        waitT = 0.f;
        setStage(2);
    }

    // no SkyLine in this world: they meet in a parked car near the precinct instead, and bolt when Mari walks up
    void beginStreet(GameWorld& g) {
        spawnPair(g);
        int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 5);
        float yaw = gPlaces.policeHq.curbYaw;
        vossCar = spawnCar(g, model, curbOffset(g, gPlaces.policeHq, 44.f, &yaw), yaw, lin(0.04f, 0.04f, 0.05f));
        if (vossCar >= 0) {
            if (voss >= 0) g.warpPedIntoVehicle(voss, vossCar, 0);
            if (chuy >= 0) g.warpPedIntoVehicle(chuy, vossCar, 1);
            g.mBlipVehicle(vossCar, UI::BLIP_ENEMY);
        }
        goTo(g, vehPos(g, vossCar), 30.f, "Find ~r~Chuy~s~ and ~r~Voss~s~ near the precinct.", false, false);
        setStage(10);
    }

    void startVossRun(GameWorld& g) {
        // Voss makes for his car at the foot of the stair; Chuy covers him
        g.mClearBlips();
        g.mBlipPed(voss, UI::BLIP_ENEMY);
        if (vossCar < 0) {
            int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 5);
            Place curbPl = resolvePlace(g, stairFoot.xy());
            float yaw = curbPl.curbYaw;
            vossCar = spawnCar(g, model, curbOffset(g, curbPl, 18.f, &yaw), yaw, lin(0.04f, 0.04f, 0.05f));
        }
        g.mObjective("Stop ~r~Voss~s~ before he gets away with the envelope.");
        score(SC_CHASE, 0.9f, 32);
        cp(g, 2);
        setStage(4);
    }

    void dropEnvelope(GameWorld& g) {
        vec3 at = pedPos(g, voss) + vec3(0.6f, 0.4f, 0.3f);
        envelope = spawnPackage(g, at);
        g.mClearBlips();
        goTo(g, at, 1.5f, "Grab the ~g~envelope~s~.", false, false);
        sayAs(g, CAST_VOSS, voss, "Voss", "[scared:0.4]Take it. Take it! It's just paper. She'll kill me either way.");
        setStage(5);
    }

    void startK9(GameWorld& g) {
        bool bc = endedBroadcast(g);
        vec3 pp = playerPos(g);
        vec3 sp = pp + vec3(55.f, 20.f, 0.f);
        for (int k = 0; k < 10; k++) {
            float a = k * 0.63f;
            vec2 q = pp.xy() + vec2(cosf(a), sinf(a)) * 55.f;
            float x = 0.f;
            int wl = g.laneGraph.nearestWalk(q, 20.f, &x);
            if (wl < 0) continue;
            sp = g.laneGraph.walkPos(wl, x, 0.f, true);
            if (!g.inCameraView(sp + vec3(0, 0, 1.f), 2.f)) break;
        }
        handler = bc ? spawnCast(g, CAST_GUARD_A, sp, yawTo(sp.xy(), pp.xy()), FAC_ENEMY)
                     : g.mPed(g.randomCivilianChar(0x9D07u, 1), dvec3(sp), yawTo(sp.xy(), pp.xy()), FAC_POLICE);
        if (handler >= 0) {
            arm(g, handler, WPN_PISTOL);
            g.peds[handler].brain.accuracy = 0.2f;
            setGoto(g, handler, pp, 2.6f);
            dog = Wildlife::spawnK9(g, handler, &dogUid);
            g.mBlipPed(handler, UI::BLIP_ENEMY);
        }
        if (bc) {
            phoneLine(g, CAST_KIT, "[scared]Mari, Sable's security just rolled up behind you. They've got a dog. A big one.");
        } else {
            setWanted(g, 2);
            phoneLine(g, CAST_KIT, "[scared]That's Holt's K9 unit! She's using the whole department on you now.");
        }
        sayMe(g, "[shout]Of course they have a dog.");
        g.mObjective(bc ? "Lose the ~r~K9 team~s~: get in a car and put some distance between you." : "Lose the ~r~K9 unit~s~ and the ~b~police~s~.");
        g.help("The dog follows your scent on foot. In a car, the trail ends.", 5.f);
        score(SC_CHASE, 1.f, 32);
        setStage(6);
    }

    void start(GameWorld& g) override {
        station = metroReady() ? metroStationByName("Civic Center") : -1;
        if (station < 0 && metroReady()) station = metroStationNear(gPlaces.policeHq.pos.xy());
        side = 0;
        score(SC_NOIR, 0.35f, 32);
        if (station >= 0) {
            std::vector<vec3> up = metroStairPath(station, side, true, 6.f);
            stairFoot = up.front();
            platform = up.back();
        }
        if (checkpoint >= 2 && station >= 0) {
            // restart on the street below the next station: Voss comes down the stair
            const World::MetroLine& L = World::gTransit->metro;
            st2 = (station + 1) % (int)L.stations.size();
            sd2 = side;
            std::vector<vec3> down = metroStairPath(st2, sd2, false, 0.f);
            stairFoot = down.back();
            spawnPair(g);
            placePed(g, voss, down[Min(3, (int)down.size() - 1)], 0.f);
            placePed(g, chuy, down[Min(2, (int)down.size() - 1)], 0.f);
            walk.start(g, voss, std::vector<vec3>(down.begin() + Min(3, (int)down.size() - 1), down.end()), 3.2f);
            setCombat(g, chuy, g.player, 0.22f);
            g.peds[chuy].faction = FAC_ENEMY;
            enemies.push_back(chuy);
            placePlayer(g, stairFoot + vec3(-18.f, 6.f, 0.f), 0.f);
            startVossRun(g);
            return;
        }
        if (checkpoint >= 1 && station >= 0) {
            placePlayer(g, platform, atan2f(-stationAt(station).dir.x, stationAt(station).dir.y));
            beginPlatform(g);
            return;
        }
        if (station < 0) {
            placePlayer(g, placeOffset(g, gPlaces.policeHq, -60.f, 0.3f), gPlaces.policeHq.yaw);
            phoneLine(g, CAST_KIT, "[calm]Chuy meets Sable's courier near the precinct downtown, in a parked car. Get close, get the envelope.");
            beginStreet(g);
            return;
        }
        placePlayer(g, stairFoot + vec3(3.f, 0.f, 0.f), 0.f);
        std::vector<CutsceneShot> shots;
        const World::MetroStation& S = stationAt(station);
        vec3 plat = vec3(S.pos, S.platformZ());
        // from the side of the viaduct and above the street, where nothing stands between the lens and the platform
        if (!establish(g, shots, plat, atan2f(-S.dir.x, S.dir.y) + kPi * 0.5f, 60.f, 20.f, 6.f, 50.f))
            shots.push_back(shotMove(plat + vec3(S.right() * 70.f, 45.f), plat + vec3(0, 0, 2.f), plat + vec3(S.right() * 55.f, 35.f), plat + vec3(0, 0, 1.5f),
                                     6.f, 50.f));
        // the station overhead as Mari sees it from the foot of the stair, looking up from out in the street (an
        // over-the-shoulder from the stair foot only sees the underside of the viaduct)
        vec3 me = playerPos(g), look = vec3(me.xy(), me.z + 1.6f + (plat.z + 1.f - me.z - 1.6f) * 0.35f);
        bool framed = false;
        for (int k = 0; k < 4 && !framed; k++) {
            vec3 cam = me + vec3(S.right() * (k < 2 ? 8.f : -8.f) + S.dir * (k & 1 ? -4.f : 4.f), 1.5f);
            cam.z = groundAt(g, cam.x, cam.y, me.z + 3.f) + 1.5f;
            if (insideBuilding(g, cam) || !clearView(g, dvec3(cam), dvec3(me + vec3(0.f, 0.f, 1.5f)))) continue;
            shots.push_back(shotMove(cam, look, cam + (me - cam) * 0.12f, look, 5.f, 50.f));
            framed = true;
        }
        if (!framed) shots.push_back(shotOver(playerPos(g), plat, 5.f));
        g.mCutscene(shots);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PHONE_RING, 0.7f);
#endif
        phoneLine(g, CAST_KIT, "[calm]Nando's phone had one more thing. Chuy meets Sable's courier on the SkyLine. Civic Center, northbound. Today.");
        phoneLine(g, CAST_KIT, "[calm]The courier's called Voss. Big guy, black jacket. Ride their train, hear what they say, and get whatever he's carrying.");
        sayMe(g, "[calm]Chuy. Dex's old friend from the street race. This should be fun.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    goTo(g, platform, 2.5f, "Go up to the ~y~Civic Center platform~s~.");
                    next();
                }
                break;
            case 1:
                if (::length(playerPos(g).xy() - platform.xy()) < 5.f && fabsf(playerPos(g).z - platform.z) < 3.f) beginPlatform(g);
                else if (stageTime > 1.f && ::length(playerPos(g).xy() - platform.xy()) < 60.f && playerPos(g).z > platform.z - 3.f) beginPlatform(g);
                break;
            case 2: {   // the SkyLine leg
                if (!pedAlive(g, voss) || !pedAlive(g, chuy)) return fail("You blew it before they said anything useful.");
                vec3 vp = pedPos(g, voss);
                switch (phase) {
                    case 0: {   // waiting for a train
                        waitT += dt;
                        int c = metroCarAt(g, station, side, vp.xy());
                        if (c >= 0) {
                            train = c;
                            trainUid = g.vehicles[c].uid;
                            setGoto(g, voss, metroDoor(g, c, station, side, 0, 0.6f), 1.8f);
                            setGoto(g, chuy, metroDoor(g, c, station, side, 0, 0.6f) + vec3(0.6f, 0.f, 0.f), 1.8f);
                            boardT = 0.f;
                            phase = 1;
                        } else if (waitT > 240.f) {
                            return fail("Their train never came. They took a cab.");
                        }
                        break;
                    }
                    case 1: {   // boarding
                        boardT += dt;
                        if (!isMetroCar(g, train) || g.vehicles[train].uid != trainUid || metroCarStation(g, train) != station) {
                            if (g.peds[voss].vehicle == train && isMetroCar(g, train)) {
                                phase = 2;
                                break;
                            }
                            setIdle(g, voss, 7);
                            setIdle(g, chuy, 7);
                            phase = 0;
                            break;
                        }
                        vec3 dp = metroDoor(g, train, station, side, 0, 0.6f);
                        if (::length(vp.xy() - dp.xy()) > 1.2f && boardT < 5.f) break;
                        for (int p : {voss, chuy}) {
                            if (g.peds[p].vehicle >= 0) continue;
                            int seat = metroFreeSeat(g, train);
                            if (seat < 0) continue;
                            g.warpPedIntoVehicle(p, train, seat);
                            g.peds[p].brain.type = BRAIN_NONE;
                            g.peds[p].invincible = true;
                        }
                        g.mClearBlips();
                        g.mBlipVehicle(train, UI::BLIP_ENEMY);
                        g.mObjective("Get on the ~r~train~s~ with them.");
                        phase = 2;
                        stopT = 0.f;
                        break;
                    }
                    case 2: {   // riding
                        if (!isMetroCar(g, train) || g.vehicles[train].uid != trainUid) return fail("You lost them on the SkyLine.");
                        int pv = g.playerVehicle();
                        bool aboard = isMetroCar(g, pv) && ::length(vehPos(g, pv) - vehPos(g, train)) < 70.f;
                        int sd = -1;
                        int s2 = metroCarStation(g, train, &sd);
                        bool moving = s2 < 0;
                        if (moving && !aboard && ::length(vehPos(g, train).xy() - stationAt(station).pos) > 45.f && !heard)
                            return fail("You missed the train.");
                        if (moving && aboard && !heard) {
                            heard = true;
                            g.mObjective("Listen in. Stay on the train.");
                            sayAs(g, CAST_VOSS, voss, "Voss", "[calm]Sable says the airport run is tonight. Pruitt carries the drive himself. She doesn't trust the cloud.");
                            sayAs(g, CAST_THUG_A, chuy, "Chuy", "[calm]And the swamp? El Cuervo wants the last guns moved before the feds sniff around the fish camp.");
                            sayAs(g, CAST_VOSS, voss, "Voss", "[calm]After the flight. First the books leave the country. Then the guns. Then her, on the king tide.");
                            sayAs(g, CAST_THUG_A, chuy, "Chuy", "[happy:0.3]And Calle Luna?");
                            sayAs(g, CAST_VOSS, voss, "Voss", "[calm]Then there is no Calle Luna. Just a shipping lane with a nice view.");
                            sayMe(g, "[whisper:0.5]Over my dead body.");
                        }
                        if (s2 < 0 || s2 == station) {
                            stopT = 0.f;
                            break;
                        }
                        stopT += dt;
                        if (stopT < 1.8f) break;
                        // off at the next station: Voss down to the street, Chuy lingers on the platform
                        st2 = s2;
                        sd2 = sd;
                        vec3 out = metroDoor(g, train, s2, sd, 0, 0.9f);
                        for (int p : {voss, chuy}) {
                            g.peds[p].invincible = false;
                            placePed(g, p, out + vec3(p == chuy ? 0.8f : 0.f, 0.f, 0.f), yawTo(vehPos(g, train).xy(), out.xy()));
                        }
                        std::vector<vec3> down = metroStairPath(s2, sd, false, dot(out.xy() - stationAt(s2).pos, stationAt(s2).dir));
                        stairFoot = down.back();
                        walk.start(g, voss, down, 2.2f);
                        setIdle(g, chuy, 14);
                        g.mClearBlips();
                        g.mBlipPed(voss, UI::BLIP_ENEMY);
                        g.mBlipPed(chuy, UI::BLIP_ENEMY);
                        g.mObjective(StrFormat("Get off at ~y~%s~s~ and follow ~r~Voss~s~.", stationAt(s2).name.c_str()));
                        phase = 3;
                        spotT = 0.f;
                        break;
                    }
                    case 3: {   // off the train
                        walk.update(g, dt);
                        spotT += dt;
                        bool playerOff = g.playerVehicle() < 0;
                        float dc = ::length(playerPos(g) - pedPos(g, chuy));
                        if (!spotted && ((playerOff && dc < 25.f) || spotT > 14.f)) {
                            spotted = true;
                            g.peds[chuy].faction = FAC_ENEMY;
                            setCombat(g, chuy, g.player, 0.22f);
                            enemies.push_back(chuy);
                            sayAs(g, CAST_THUG_A, chuy, "Chuy", "[shout]Ortega! She's been on us the whole ride! Voss, go!");
                            walk.speed = 4.4f;
                            if (!walk.done()) setGoto(g, voss, walk.pts[walk.wp], 4.4f);
                            startVossRun(g);
                        }
                        break;
                    }
                }
                break;
            }
            case 4: {   // Voss runs for his car, then drives
                if (!pedAlive(g, voss)) return fail("Voss is dead. The envelope went up with his car.");
                if (!vossDriving) {
                    if (!walk.done()) walk.update(g, dt);
                    else if (vossCar >= 0 && g.peds[voss].vehicle != vossCar) {
                        vec3 cp = vehPos(g, vossCar);
                        if (::length(pedPos(g, voss).xy() - cp.xy()) < 3.f || g.peds[voss].brain.timer > 12.f) {
                            g.warpPedIntoVehicle(voss, vossCar, 0);
                            ScriptDriver& d = driveRoad(g, vossCar, cp.xy() + normalize(cp.xy() - playerPos(g).xy() + vec2(1e-3f, 0.f)) * 900.f, 20.f, true);
                            d.rubberPed = g.player;
                            d.rubberGap = 55.f;
                            vossDriving = true;
                            g.mClearBlips();
                            g.mBlipVehicle(vossCar, UI::BLIP_ENEMY);
                            sayMe(g, "[angry:0.4]He's got a car. Of course he's got a car.");
                        } else if (g.peds[voss].brain.type != BRAIN_GOTO) {
                            setGoto(g, voss, cp, 4.4f);
                        }
                    }
                    if (givesUp(g, voss)) {
                        dropEnvelope(g);
                        break;
                    }
                } else {
                    int d = vossCar >= 0 ? g.driverOf(vossCar) : -1;
                    bool stopped = vossCar < 0 || vehicleDisabled(g, vossCar) || g.vehicles[vossCar].sim.health < 420.f || d != voss;
                    ScriptDriver* sd = vossCar >= 0 ? driverFor(vossCar) : nullptr;
                    float dist = ::length(pedPos(g, voss) - playerPos(g));
                    if ((sd && sd->done && dist > 110.f) || dist > 520.f) return fail("Voss got away.");
                    if (stopped || (sd && sd->done)) {
                        if (vossCar >= 0) releaseDriver(g, vossCar);
                        if (g.peds[voss].vehicle >= 0) g.removePedFromVehicle(voss, true);
                        setFlee(g, voss, g.player);
                        vossDriving = false;
                        walk.pts.clear();
                        walk.wp = 0;
                        vossCar = -2;   // (no more running for it)
                        g.mClearBlips();
                        g.mBlipPed(voss, UI::BLIP_ENEMY);
                        g.mObjective("Catch ~r~Voss~s~. Corner him or hold him at gunpoint.");
                    }
                }
                break;
            }
            case 5:
                if (packageTaken(g, envelope) || grabNear(g, envelope, 1.6f)) {
                    g.mClearTarget();
                    setFlee(g, voss, g.player);
                    sayMe(g, "[calm]A boarding pass. Sable Air Freight, tonight, in the name of Hollis Pruitt. And a list of flight numbers.");
                    startK9(g);
                }
                break;
            case 6: {   // the K9 team
                k9T -= dt;
                bool handlerUp = pedAlive(g, handler);
                bool dogUp = dog >= 0 && Wildlife::k9Alive(dog, dogUid);
                vec3 pp = playerPos(g);
                if (k9T <= 0.f && dogUp) {
                    k9T = 0.5f;
                    vec3 dpos = Wildlife::k9Pos(dog);
                    bool onFoot = g.playerVehicle() < 0;
                    if (onFoot && ::length(dpos - pp) < 9.f) Wildlife::k9Command(g, dog, dogUid, Wildlife::K9_ATTACK, pp, g.player);
                    else if (onFoot) Wildlife::k9Command(g, dog, dogUid, Wildlife::K9_TRACK, pp, -1);
                    else Wildlife::k9Command(g, dog, dogUid, Wildlife::K9_ALERT, pp, -1);
                    if (handlerUp && g.peds[handler].brain.type == BRAIN_GOTO) g.peds[handler].brain.goal = dvec3(pp);
                }
                float dh = handlerUp ? ::length(pedPos(g, handler) - pp) : 1e9f;
                float dd = dogUp ? ::length(Wildlife::k9Pos(dog) - pp) : 1e9f;
                bool clear = (g.playerVehicle() >= 0 && Min(dh, dd) > 170.f) || (!handlerUp && !dogUp);
                if (clear && g.pinfo.wanted == 0) {
                    if (dogUp) Wildlife::k9Dismiss(g, dog, dogUid);
                    if (handlerUp) setFlee(g, handler, g.player);
                    g.mClearBlips();
                    phoneLine(g, CAST_KIT, "[calm]You lost them. Pruitt's flight is tonight. That's Dex's department: he still knows every hangar at that airport.");
                    sayMe(g, "[calm]Then I'd better wake him up.");
                    next();
                } else if (clear && g.hudHelpTimer <= 0.f) {
                    g.help("Now lose the ~b~police~s~.", 2.f);
                }
                break;
            }
            case 7:
                if (!g.mTalking()) {
                    g.storyBriefText = "Sable's accountant, Hollis Pruitt, flies out of Porto Sol International tonight on a Sable Air Freight plane "
                                       "with the books on a drive. Dex knows airports.";
                    return MS_PASSED;
                }
                break;
            case 10:   // (no SkyLine) the parked car
                if (vossCar >= 0 && g.playerAt(vehPos(g, vossCar).xy(), 55.f)) {
                    clearGoal(g);
                    ScriptDriver& d = driveRoad(g, vossCar, vehPos(g, vossCar).xy() + gPlaces.policeHq.streetDir * 900.f, 20.f, true);
                    d.rubberPed = g.player;
                    d.rubberGap = 55.f;
                    vossDriving = true;
                    sayAs(g, CAST_THUG_A, chuy, "Chuy", "[shout]That's Ortega! Drive!");
                    g.mObjective("Stop ~r~Voss~s~ before he gets away with the envelope.");
                    setStage(4);
                }
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        if (dog >= 0 && Wildlife::k9Alive(dog, dogUid)) Wildlife::k9Dismiss(g, dog, dogUid);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.8f) t.teleport(platform, 0.f);
                break;
            case 2:
                if (phase == 2 && isMetroCar(g, train) && !isMetroCar(g, g.playerVehicle()) && t.stageTime > 0.5f) {
                    int seat = metroFreeSeat(g, train);
                    if (seat >= 0) t.enter(train, seat);
                } else if (phase == 3) {
                    if (isMetroCar(g, g.playerVehicle())) {
                        t.exitVehicle();
                        placePlayer(g, metroDoor(g, train, st2, sd2, 1, 1.2f), 0.f);
                    }
                    if (fmodf(t.stageTime, 1.f) < dt && walk.wp < (int)walk.pts.size()) placePed(g, voss, walk.pts[walk.wp], 0.f);
                }
                break;
            case 4:
                if (t.stageTime > 1.f && fmodf(t.stageTime, 1.f) < dt && pedAlive(g, voss)) {
                    if (vossDriving && vossCar >= 0) {
                        g.vehicles[vossCar].sim.engineHealth = 0.f;
                        g.vehicles[vossCar].sim.health = 350.f;
                    } else {
                        t.exitVehicle();
                        t.teleport(pedPos(g, voss) + vec3(2.f, 0.f, 0.f), 0.f);
                        g.knockDown(voss, vec3(0.f, 40.f, 20.f));
                    }
                    if (pedAlive(g, chuy)) t.shoot(chuy);
                }
                break;
            case 5:
                if (envelope >= 0 && !packageTaken(g, envelope) && t.stageTime > 0.5f) t.teleport(g.pickups[envelope].pos.toVec3(), 0.f);
                break;
            case 6:
                if (t.stageTime > 1.f && g.playerVehicle() < 0) t.enter(spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3.f, 0.f, 0.f), 0.f));
                if (t.stageTime > 2.f && g.playerVehicle() >= 0 && fmodf(t.stageTime, 2.f) < dt) {
                    t.teleportNear(playerPos(g).xy(), 260.f);
                    g.pinfo.wanted = 0;
                    g.pinfo.wantedHeat = 0.f;
                }
                break;
            case 10: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 4-4: "Clear Air" (Dex). Pruitt checks in at departures like a tourist, then slips airside to a Sable Air Freight
// plane on the cargo apron. Tail him through the forecourt, follow the van across the aprons in an airport ops truck, and stop
// the plane before it leaves the runway: the drive with every Sable shipment goes with him otherwise.
class MissionClearAir : public StoryMission {
public:
    int pruitt = -1, van = -1, vanDriver = -1, plane = -1, pilot = -1, tug = -1, drive = -1, officer = -1;
    PedPath walk;
    float planeZ = 0.f, rollStart = 0.f, liftOff = 0.f, ramCd = 0.f, runD0 = 0.f;
    bool planeStopped = false, spooked = false, surrendered = false;
    const char* title() const override { return "Clear Air"; }
    const char* brief() const override {
        return "Sable's accountant, Hollis Pruitt, flies out tonight with every shipment on a drive. Follow him airside and keep his plane on "
               "the ground.";
    }
    long long reward() const override { return 15000; }

    vec3 ap(float x, float y) {
        float z = World::gSites ? World::gSites->airportZ : 4.f;
        return vec3(x, y, z);
    }
    vec3 apg(GameWorld& g, float x, float y) { return vec3(x, y, groundAt(g, x, y, (World::gSites ? World::gSites->airportZ : 4.f) + 3.f)); }

    void spawnAirside(GameWorld& g) {
        int vm = pickModel(g, {Vehicles::VC_VAN, Vehicles::VC_SERVICE}, 2);
        van = spawnCar(g, vm, apg(g, 640.f, 1108.f), kPi, lin(0.92f, 0.93f, 0.95f));
        if (van >= 0) {
            g.vehicles[van].color1 = lin(0.1f, 0.25f, 0.6f);
            vanDriver = spawnCast(g, CAST_GUARD_B, apg(g, 640.f, 1108.f), 0.f, FAC_ENEMY);
            if (vanDriver >= 0) {
                g.warpPedIntoVehicle(vanDriver, van, 0);
                arm(g, vanDriver, WPN_PISTOL);
            }
        }
        // the airport operations pickup, in apron yellow (a garbage truck on the apron would not fool anybody)
        tug = spawnCar(g, pickModel(g, {Vehicles::VC_PICKUP, Vehicles::VC_SERVICE, Vehicles::VC_VAN}, 7), apg(g, 662.f, 1126.f), kPi * 0.5f, lin(0.95f, 0.75f, 0.08f));
        plane = spawnAircraft(g, false, apg(g, -430.f, 1120.f), kPi, lin(0.95f, 0.95f, 0.97f));
        if (plane >= 0) {
            g.vehicles[plane].color1 = lin(0.1f, 0.25f, 0.6f);
            planeZ = (float)g.vehicles[plane].sim.body.pos.z;
            pilot = spawnCast(g, CAST_PILOT, apg(g, -430.f, 1120.f), 0.f, FAC_CIVILIAN);
            if (pilot >= 0) g.warpPedIntoVehicle(pilot, plane, 0);
        }
    }

    void startTaxi(GameWorld& g) {
        if (plane < 0) return;
        float z = planeZ;
        std::vector<vec3> wps = {vec3(-430.f, 1120.f, z), vec3(-430.f, 1030.f, z), vec3(-432.f, 963.f, z), vec3(-640.f, 962.f, z), vec3(-650.f, 930.f, z),
                                 vec3(-650.f, 812.f, z),  vec3(-640.f, 781.f, z),  vec3(-560.f, 780.f, z), vec3(220.f, 780.f, z),  vec3(520.f, 780.f, z + 18.f),
                                 vec3(900.f, 780.f, z + 70.f), vec3(1500.f, 780.f, z + 160.f)};
        RoutePath path;
        buildWaypointPath(wps, path, 20.f, true);
        rollStart = path.project(vec2(-560.f, 780.f), 0.f);
        liftOff = path.project(vec2(520.f, 780.f), rollStart);
        ScriptDriver& d = addDriver(g, plane, path, 9.f, DRV_KINEMATIC);
        d.kinSpeed = 0.f;
        g.mBlipVehicle(plane, UI::BLIP_ENEMY);
        g.mObjective("Stop the ~r~plane~s~ before it takes off. Shoot it or ram it.");
        score(SC_CHASE, 1.f, 33);
        cp(g, 2);
        setStage(4);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one (until the end)
        if (g.env->timeOfDay > 4.f && g.env->timeOfDay < 19.f) g.env->timeOfDay = 20.5f;
        score(SC_NOIR, 0.35f, 33);
        spawnAirside(g);
        if (checkpoint >= 2) {
            placePlayer(g, apg(g, -380.f, 1060.f), -kPi * 0.5f);
            if (tug >= 0) {
                teleportVehicle(g, tug, apg(g, -380.f, 1060.f), -kPi * 0.5f);
                g.warpPedIntoVehicle(g.player, tug, 0);
            }
            pruitt = spawnCast(g, CAST_PRUITT, apg(g, -430.f, 1120.f), 0.f, FAC_CIVILIAN);
            if (pruitt >= 0 && plane >= 0) g.warpPedIntoVehicle(pruitt, plane, 1);
            startTaxi(g);
            return;
        }
        vec3 curb = apg(g, 696.5f, 1590.f);
        pruitt = spawnCast(g, CAST_PRUITT, curb, kPi, FAC_CIVILIAN);
        if (pruitt >= 0) g.peds[pruitt].maxHealth = g.peds[pruitt].health = 200.f;
        placePlayer(g, apg(g, 728.f, 1655.f), kPi);
        std::vector<CutsceneShot> shots;
        vec3 term = ap(630.f, 1500.f);
        shots.push_back(shotMove(term + vec3(260.f, 180.f, 45.f), term + vec3(0, 0, 12.f), term + vec3(200.f, 120.f, 30.f), term + vec3(0, 0, 8.f), 6.f, 50.f));
        shots.push_back(shotOver(playerPos(g), pedPos(g, pruitt), 6.f));
        g.mCutscene(shots);
        phoneLine(g, CAST_KIT, "[calm]That's him. Pale blue shirt, the bag he never puts down. Hollis Pruitt checks in at departures like a tourist, for the cameras.");
        phoneLine(g, CAST_KIT, "[calm]Then he slips airside. Sable Air Freight flies from the west cargo apron tonight.");
        sayMe(g, "[calm]Airside. You want me to crash an airport, Kit?");
        phoneLine(g, CAST_KIT, "[happy:0.4]I want you to stop a plane. The crashing is optional.");
    }

    void autotestSpeed(float& s) {
        if (gMissions.test.active) s = Max(s, 5.f);
    }

    // Hands up for the airport police (the broadcast ending: they are honest now, and Dela Cruz vouches for Dex). The
    // mission takes the arrest over before the cuffs: the units stand down, one officer walks up, the radio clears him.
    void surrenderScene(GameWorld& g) {
        surrendered = true;
        g.ai.surrender = false;
        if (g.ai.surrenderCtl) {
            g.ai.surrenderCtl = false;
            g.playerControl = true;
        }
        g.pinfo.wanted = 0;
        g.pinfo.wantedHeat = 0.f;
        gMissions.suppressPolice = true;
        Ped* pl = g.playerPed();
        vec3 pp = playerPos(g);
        calmFires(g, pp, 30.f);   // (the plane, shot up on the apron)
        float pyaw = pl ? pl->yaw : 0.f;
        vec2 fw = dirFromYaw(pyaw);
        vec3 op = pp + vec3(fw * 3.4f + vec2(-fw.y, fw.x) * 0.8f, 0.f);
        op.z = groundAt(g, op.x, op.y, pp.z + 2.f);
        officer = spawnCast(g, CAST_COP_B, op, yawTo(op.xy(), pp.xy()), FAC_CIVILIAN);
        if (officer >= 0) {
            arm(g, officer, WPN_PISTOL, 2);
            setIdle(g, officer, 0);
        }
        if (pl) {
            pl->animIn.stance = 5;
            pl->pendingAction = Anim::CLIP_HANDS_UP;
        }
        vec3 o = pedPos(g, officer);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotTwo(o, pp, 6.f));
        shots.push_back(shotOver(o, pp, 7.f));
        shots.push_back(shotOver(pp, o, 7.f, -1.f));
        shots.push_back(shotTwo(pp, o, 6.f, 4.f, 42.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_COP_B, officer, "[shout]Airport police! Hands where I can see them! Don't you move!");
        sayMe(g, "[calm]They're up. There's a drive in my jacket that belongs to Agent Dela Cruz, state task force. Call her.");
        say(g, CAST_COP_B, officer, "[calm]Sure it does. Dispatch, get me a Dela Cruz at the state task force. Tell her I'm standing next to her drive.");
        narrator(g, "Dispatch", "Unit twelve, Dela Cruz confirms. He's working for her. Let him walk, and she says thanks for the plane.", "dispatcher", "[radio]",
                 kColOther);
        say(g, CAST_COP_B, officer, "[calm]Your lucky night, Calloway. Walk away. Slowly. Before I remember what you did to that plane.");
        g.mObjective("");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        ramCd -= dt;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    std::vector<vec3> path = {apg(g, 696.5f, 1420.f), apg(g, 696.5f, 1280.f), apg(g, 696.5f, 1175.f), apg(g, 686.f, 1156.f),
                                              apg(g, 668.f, 1138.f), apg(g, 646.f, 1112.f)};
                    float spd = 1.45f;
                    autotestSpeed(spd);
                    walk.start(g, pruitt, path, spd);
                    g.mBlipPed(pruitt, UI::BLIP_ENEMY);
                    g.mObjective("Follow ~r~Pruitt~s~ through the forecourt. Don't let him see you.");
                    next();
                }
                break;
            case 1: {   // the tail on foot
                if (!pedAlive(g, pruitt)) return fail("Pruitt is dead. The drive went with him.");
                vec3 pp = pedPos(g, pruitt);
                int r = spooked ? 0 : tail.update(g, pp, 5.f, 55.f, dt, 18.f, 3.f);
                if (r == 2 && !spooked) {
                    spooked = true;
                    sayAs(g, CAST_PRUITT, pruitt, "Pruitt", "[scared]No, no, no. Not tonight. Not tonight!");
                    walk.speed = 4.2f;
                    if (!walk.done()) setGoto(g, pruitt, walk.pts[walk.wp], 4.2f);
                    sayMe(g, "[angry:0.4]He made me. Doesn't matter, I know where he's going.");
                }
                if (walk.update(g, dt) || (van >= 0 && ::length(pp.xy() - vehPos(g, van).xy()) < 3.5f)) {
                    if (van >= 0) g.warpPedIntoVehicle(pruitt, van, 1);
                    g.mClearBlips();
                    if (van >= 0) {
                        std::vector<vec3> route = {vehPos(g, van), apg(g, 560.f, 1100.f), apg(g, 560.f, 965.f), apg(g, 300.f, 962.f), apg(g, -150.f, 962.f),
                                                   apg(g, -392.f, 964.f), apg(g, -404.f, 1072.f), apg(g, -412.f, 1098.f)};
                        RoutePath path;
                        buildWaypointPath(route, path, 14.f, true);
                        ScriptDriver& d = addDriver(g, van, path, 13.f);
                        d.aggressive = true;
                        d.stopAtEnd = true;
                        g.mBlipVehicle(van, UI::BLIP_ENEMY);
                    }
                    if (tug >= 0) g.mBlipVehicle(tug, UI::BLIP_VEHICLE);
                    g.mObjective("Follow the ~r~van~s~ airside. The yellow ~b~ops truck~s~ on the apron is yours.");
                    score(SC_CHASE, 0.7f, 33);
                    cp(g, 1);
                    next();
                }
                break;
            }
            case 2: {   // the van to the cargo apron
                if (van < 0 || !vehicleAlive(g, van)) {
                    setStage(3);
                    break;
                }
                if (tug >= 0 && g.playerInVehicle(tug)) unblipVehicle(tug);
                ScriptDriver* d = driverFor(van);
                if (vehicleDisabled(g, van) || (d && d->done)) setStage(3);
                break;
            }
            case 3: {   // aboard the plane
                if (!pedAlive(g, pruitt)) return fail("Pruitt is dead. The drive went with him.");
                if (van >= 0) releaseDriver(g, van);
                if (plane < 0 || !vehicleAlive(g, plane)) return fail("The plane is gone, and the drive with it.");
                if (g.peds[pruitt].vehicle != plane) {
                    if (g.peds[pruitt].vehicle >= 0) g.removePedFromVehicle(pruitt, true);
                    g.warpPedIntoVehicle(pruitt, plane, 1);
                }
                if (vanDriver >= 0 && pedAlive(g, vanDriver)) {
                    if (g.peds[vanDriver].vehicle >= 0) g.removePedFromVehicle(vanDriver, true);
                    setCombat(g, vanDriver, g.player, 0.2f);
                    enemies.push_back(vanDriver);
                    vanDriver = -1;
                }
                g.mClearBlips();
                sayMe(g, "[shout]He's in the plane. Engines are turning!");
                startTaxi(g);
                break;
            }
            case 4: {   // the plane
                if (plane < 0 || !vehicleAlive(g, plane)) return fail("The plane is gone, and the drive with it.");
                ScriptDriver* d = driverFor(plane);
                Vehicle& pv = g.vehicles[plane];
                // ramming it (the kinematic plane takes no knocks from the physics)
                int mine = g.playerVehicle();
                if (mine >= 0 && ramCd <= 0.f && ::length(vehPos(g, mine) - vehPos(g, plane)) < 7.f &&
                    ::length(g.vehicles[mine].sim.body.vel - pv.sim.body.vel) > 5.f) {
                    ramCd = 1.f;
                    g.damageVehicle(plane, 260.f, g.player, vec3(0.f), vec3(0.f));
                }
                bool crippled = pv.sim.health < 700.f || pv.sim.engineHealth < 450.f;
                if (d && !planeStopped) {
                    d->cruise = d->along < rollStart ? 10.f : 58.f;
                    if (d->along > liftOff) return fail("The plane took off. Pruitt and the drive are gone.");
                    if (crippled) {
                        planeStopped = true;
                        d->cruise = 0.f;
                        spawnFx(FX_DARK_SMOKE, dvec3(vehPos(g, plane) + vec3(0.f, 0.f, 1.2f)), vec3(0, 0, 1.f), 20, 1.4f);
                        sayMe(g, "[shout:0.5]Engine's smoking. She's coming down off the power.");
                    }
                }
                if (planeStopped) {
                    if (fmodf(stageTime, 0.4f) < dt) spawnFx(FX_DARK_SMOKE, dvec3(vehPos(g, plane) + vec3(pv.sim.forward().xy() * 1.5f, 1.3f)), vec3(0, 0, 1.f), 3, 1.f);
                    if (!d || d->kinSpeed < 0.6f) {
                        releaseDriver(g, plane);
                        pv.sim.body.vel = vec3(0.f);
                        if (pedAlive(g, pruitt)) {
                            if (g.peds[pruitt].vehicle >= 0) g.removePedFromVehicle(pruitt, true);
                            setFlee(g, pruitt, g.player);
                        }
                        if (pedAlive(g, pilot)) {
                            if (g.peds[pilot].vehicle >= 0) g.removePedFromVehicle(pilot, true);
                            setIdle(g, pilot, 5);
                            say(g, CAST_PILOT, pilot, "[scared]Don't shoot! I just fly the plane! I just fly the plane!");
                        }
                        g.mClearBlips();
                        g.mBlipPed(pruitt, UI::BLIP_ENEMY);
                        g.mObjective("Catch ~r~Pruitt~s~. Corner him or hold him at gunpoint.");
                        runD0 = pedAlive(g, pruitt) ? ::length(pedPos(g, pruitt) - playerPos(g)) : 0.f;   // (the plane may be stopped from afar)
                        next();
                    }
                }
                break;
            }
            case 5:
                if (!pedAlive(g, pruitt)) return fail("Pruitt is dead. The drive went with him.");
                if (::length(pedPos(g, pruitt) - playerPos(g)) > Max(260.f, runD0 + 150.f)) return fail("Pruitt got away on foot.");
                if (givesUp(g, pruitt)) {
                    drive = spawnPackage(g, pedPos(g, pruitt) + vec3(0.6f, 0.3f, 0.3f));
                    sayAs(g, CAST_PRUITT, pruitt, "Pruitt", "[scared]It's in the bag! Take it! I only did the arithmetic, I swear, I never touched a gun!");
                    g.mClearBlips();
                    goTo(g, g.pickups[drive].pos.toVec3(), 1.5f, "Grab ~g~Pruitt's drive~s~.", false, false);
                    next();
                }
                break;
            case 6:
                if (packageTaken(g, drive) || grabNear(g, drive, 1.6f)) {
                    g.mClearTarget();
                    bool bc = endedBroadcast(g);
                    if (bc) {
                        setWanted(g, 2);
                        phoneLine(g, CAST_KIT, "[scared:0.4]Airport police are rolling. They're not Holt's anymore, but they don't know you're the good guys.");
                        phoneLine(g, CAST_KIT, "[calm]Lose them, or put the gun away and your hands up. Agent Dela Cruz owes me one, she'll vouch for you.");
                        g.mObjective("Lose the ~b~police~s~, or surrender: gun away, hold ~i:UP|UP~.");
                    } else {
                        setWanted(g, 3);
                        phoneLine(g, CAST_KIT, "[scared]That's Holt's airport precinct. They've been waiting for an excuse. Don't let them take you. Run!");
                        g.mObjective("Lose the ~b~police~s~.");
                    }
                    score(SC_CHASE, 1.f, 33);
                    next();
                }
                break;
            case 7:
                if (endedBroadcast(g) && g.ai.surrender && !surrendered) {
                    surrenderScene(g);
                    break;
                }
                if (g.pinfo.wanted == 0 && (!surrendered || (!g.mInCutscene() && !g.mTalking()))) {
                    if (surrendered) {
                        Ped* pl = g.playerPed();
                        if (pl && pl->animIn.stance == 5) pl->animIn.stance = 0;   // hands down
                        if (pedAlive(g, officer)) setGoto(g, officer, pedPos(g, officer) + vec3(dirFromYaw(g.peds[officer].yaw) * -30.f, 0.f), 1.4f);
                    }
                    phoneLine(g, CAST_KIT, "[calm]I'm into the drive. Every Sable shipment for three years... and the last load of guns leaves from a fish camp in the Sawgrass. El Cuervo's there.");
                    sayMe(g, "[calm]Jonah's backyard. He'll want in.");
                    next();
                }
                break;
            case 8:
                if (!g.mTalking()) {
                    g.storyBriefText = "Pruitt's drive maps every Sable shipment. El Cuervo is guarding the last load of guns at a fish camp in the "
                                       "Sawgrass. Jonah knows the way.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (pedAlive(g, pruitt) && fmodf(t.stageTime, 1.f) < dt) {
                    vec3 pp = pedPos(g, pruitt);
                    t.teleport(pp + vec3(0.f, 22.f, 0.f), kPi);
                }
                break;
            case 2:
                if (van >= 0) fastForwardDriver(g, van, 60.f, dt);
                if (t.stageTime > 0.5f && tug >= 0 && g.playerVehicle() != tug) t.enter(tug);
                break;
            case 4:
                // keep the ops truck on the plane's tail, then knock the engine out
                if (plane >= 0 && fmodf(t.stageTime, 1.f) < dt) {
                    vec3 pp = vehPos(g, plane);
                    vec2 f = g.vehicles[plane].sim.forward().xy();
                    t.teleport(pp - vec3(normalize(f + vec2(1e-3f, 0.f)) * 28.f, 0.f), atan2f(-f.x, f.y));
                }
                if (t.stageTime > 3.f && plane >= 0 && !planeStopped) g.damageVehicle(plane, 400.f, g.player, vec3(0.f), vec3(0.f));
                break;
            case 5:
                if (t.stageTime > 1.f && pedAlive(g, pruitt) && fmodf(t.stageTime, 1.f) < dt) {
                    t.exitVehicle();
                    t.teleport(pedPos(g, pruitt) + vec3(2.f, 0.f, 0.f), 0.f);
                    g.knockDown(pruitt, vec3(0.f, 40.f, 20.f));
                }
                if (t.stageTime > 1.2f) t.killEnemies();
                break;
            case 6:
                if (drive >= 0 && !packageTaken(g, drive) && t.stageTime > 0.5f) t.teleport(g.pickups[drive].pos.toVec3(), 0.f);
                break;
            case 7:
                // the broadcast ending surrenders to the airport police (hands up with no gun out), the leverage one runs
                if (endedBroadcast(g) && !surrendered && t.stageTime > 1.f) {
                    Ped* pl = g.playerPed();
                    if (g.playerVehicle() >= 0) t.exitVehicle();
                    else if (pl) {
                        pl->weapon = WPN_FISTS;
                        g.ai.forceSurrender = true;
                    }
                } else if (!endedBroadcast(g) && t.stageTime > 2.f) {
                    g.pinfo.wanted = 0;
                    g.pinfo.wantedHeat = 0.f;
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 4-5: "Gator Country" (Mari, with Jonah). El Cuervo guards Sable's last load of guns at the old Pike fish camp in
// the Sawgrass. Airboat to a hummock, take the lookouts with a scoped rifle in first person, go in, then run El Cuervo
// down across the marsh.
class MissionGatorCountry : public StoryMission {
public:
    int jonah = -1, boat = -1, cuervo = -1, cuervoBoat = -1;
    std::vector<int> lookouts;
    vec3 camp, campWater, over, overWater;
    bool fpSaved = false, fpWas = false;
    const char* title() const override { return "Gator Country"; }
    const char* brief() const override {
        return "El Cuervo guards Sable's last load of guns at the old Pike fish camp in the Sawgrass. Jonah knows the way in. Bring him out.";
    }
    long long reward() const override { return 15000; }

    void findSites(GameWorld& g) {
        const Places& P = gPlaces;
        vec2 c = P.sawgrassDeep.xy() + vec2(240.f, -160.f);
        if (!swampLand(g, c, 0.f, 420.f, 0.4f, camp)) camp = rawSpot(g, c);
        vec2 toDock = normalize(P.sawgrassDock.xy() - camp.xy() + vec2(1e-3f, 0.f));
        if (!swampLand(g, camp.xy(), 110.f, 200.f, atan2f(toDock.y, toDock.x) - 0.6f, over)) over = camp + vec3(toDock * 140.f, 0.f);
        if (!findWater(g, camp.xy(), 0.7f, campWater, 80.f)) campWater = P.sawgrassDeep;
        if (!findWater(g, over.xy(), 0.7f, overWater, 60.f)) overWater = P.sawgrassWater;
    }

    void spawnLookouts(GameWorld& g) {
        for (int i = 0; i < 4; i++) {
            float a = kTwoPi * i / 4.f + 0.4f;
            vec2 q = camp.xy() + vec2(cosf(a), sinf(a)) * (6.f + 2.f * (i & 1));
            vec3 p(q, groundAt(g, q.x, q.y, camp.z + 3.f));
            int e = spawnCast(g, CAST_THUG_A + i % 4, p, a, FAC_ENEMY);
            if (e < 0) continue;
            arm(g, e, i == 0 ? WPN_RIFLE : WPN_SMG);
            g.peds[e].brain.accuracy = 0.22f;
            setIdle(g, e, i & 1 ? 14 : 10);
            enemies.push_back(e);
            lookouts.push_back(e);
        }
    }

    void setFirstPerson(GameWorld& g, bool on) {
        if (on && !fpSaved) {
            fpSaved = true;
            fpWas = g.rig.footFirstPerson;
            g.rig.footFirstPerson = true;
        } else if (!on && fpSaved) {
            fpSaved = false;
            g.rig.footFirstPerson = fpWas;
        }
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        if (g.env->timeOfDay < 5.f || g.env->timeOfDay > 10.f) g.env->timeOfDay = 6.4f;
        requestWeather(WX_CLEAR, true);
        score(SC_NOIR, 0.3f, 34);
        findSites(g);
        vec3 dock = P.sawgrassDock, w = P.sawgrassWater;
        boat = spawnAirboat(g, w, yawTo(dock.xy(), w.xy()), lin(0.2f, 0.45f, 0.25f));
        jonah = spawnCast(g, CAST_JONAH, dock, yawTo(dock.xy(), w.xy()), FAC_FRIEND);
        if (jonah >= 0) {
            g.peds[jonah].maxHealth = g.peds[jonah].health = 450.f;
            arm(g, jonah, WPN_RIFLE, 10);
        }
        buddy = jonah;
        arm(g, g.player, WPN_SNIPER, 4);
        spawnLookouts(g);
        if (checkpoint >= 1) {
            placePlayer(g, over, yawTo(over.xy(), camp.xy()));
            if (boat >= 0) teleportVehicle(g, boat, overWater, yawTo(overWater.xy(), camp.xy()));
            placePed(g, jonah, over + vec3(1.5f, -1.f, 0.f), 0.f);
            beginOverwatch(g);
            return;
        }
        placePlayer(g, dock + vec3(3.f, 0.f, 0.f), yawTo(dock.xy() + vec2(3.f, 0.f), dock.xy()));
        vec3 jp = pedPos(g, jonah), mp = playerPos(g);
        facePed(g, jonah, mp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, jp, yawTo(w.xy(), dock.xy()), 45.f, 14.f, 4.5f, 55.f);
        shots.push_back(shotTwo(jp, mp, 7.f));
        shots.push_back(shotOver(mp, jp, 6.f));
        shots.push_back(shotOver(jp, mp, 6.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_JONAH, jonah, "[calm]The old Pike fish camp. My granddaddy built it, the Cuervos took it over last spring. So this one's personal.");
        sayMe(g, "[calm]Dex says you're the best shot in the county.");
        say(g, CAST_JONAH, jonah, "[happy:0.4]Dex lies. Three counties. But today the rifle's yours. My eyes aren't what they were at dawn.");
        say(g, CAST_JONAH, jonah, "[calm]There's a hummock north of the camp. You take the lookouts from there, quiet, then we go in loud.");
        if (endedBroadcast(g))
            say(g, CAST_JONAH, jonah, "[calm]Word is El Cuervo sleeps with one eye open since Holt went down. Nobody's paying his lawyers anymore.");
        else
            say(g, CAST_JONAH, jonah, "[angry:0.3]Word is Holt sends a sheriff's boat by every week to keep folks off. Won't help him this morning.");
    }

    void beginOverwatch(GameWorld& g) {
        g.mClearBlips();
        g.mClearMarkers();
        for (int e : lookouts)
            if (pedAlive(g, e)) g.mBlipPed(e, UI::BLIP_ENEMY);
        g.mObjective("Get out on the hummock and take the ~r~lookouts~s~ with the rifle.");
        g.help("Aim with ~i:RMB|LT~ to look through the scope.", 6.f);
        score(SC_STEALTH, 0.5f, 34);
        cp(g, 1);
        setStage(2);
    }

    void beginAssault(GameWorld& g) {
        setFirstPerson(g, false);
        g.mClearBlips();
        for (int i = 0; i < 4; i++) {
            float a = kTwoPi * i / 4.f + 1.1f;
            vec2 q = camp.xy() + vec2(cosf(a), sinf(a)) * 10.f;
            gunman(g, CAST_THUG_A + (i + 1) % 4, vec3(q, groundAt(g, q.x, q.y, camp.z + 3.f)), a, i == 0 ? WPN_SHOTGUN : WPN_SMG, 0.24f);
        }
        blipEnemies(g);
        say(g, CAST_JONAH, jonah, "[shout]That's the lookouts. Back in the boat, we're going in!");
        goTo(g, camp, 18.f, "Clear the ~r~fish camp~s~.", false, false);
        g.mClearTarget();
        score(SC_CHASE, 1.f, 34);
        cp(g, 2);
        setStage(3);
    }

    void beginChase(GameWorld& g) {
        g.mClearBlips();
        std::vector<vec3> loop = waterLoop(g, camp, 330.f, 10, 0.6f, 0.2f);
        if (loop.size() < 4) loop = waterLoop(g, gPlaces.sawgrassDeep, 300.f, 10, 0.6f, 0.2f);
        cuervoBoat = spawnAirboat(g, campWater, yawTo(campWater.xy(), loop.empty() ? campWater.xy() : loop[0].xy()), lin(0.08f, 0.08f, 0.09f), 3);
        cuervo = spawnCast(g, CAST_CUERVO, campWater, 0.f, FAC_ENEMY);
        if (cuervo >= 0) {
            g.peds[cuervo].invincible = true;
            if (cuervoBoat >= 0) g.warpPedIntoVehicle(cuervo, cuervoBoat, 0);
        }
        if (cuervoBoat >= 0 && loop.size() >= 3) {
            RoutePath path;
            buildWaypointPath(loop, path, 18.f, true, true);
            ScriptDriver& d = addDriver(g, cuervoBoat, path, 16.f, DRV_WATER);
            d.loop = true;
            d.rubberPed = g.player;
            d.rubberGap = 45.f;
            g.vehicles[cuervoBoat].sim.health = 1000.f;
            g.mBlipVehicle(cuervoBoat, UI::BLIP_ENEMY);
        }
        say(g, CAST_CUERVO, cuervo, "[shout]Pike! You old ghost! You want your swamp back? Come and catch me!");
        g.mObjective("Stop ~r~El Cuervo~s~.");
        score(SC_CHASE, 1.f, 34);
        cp(g, 3);
        setStage(4);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (allyDown(g, jonah, "Jonah")) return MS_FAILED;
        if (::length(playerPos(g) - camp) < 300.f) drawCrates(g, camp + vec3(1.5f, 1.2f, 0.f), 0.9f, 9, 0x6A70u);
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, jonah, g.player);
                    if (boat >= 0) g.mBlipVehicle(boat, UI::BLIP_BOAT);
                    goTo(g, over, 22.f, "Take the airboat to the ~y~hummock~s~ north of the camp.", false, true);
                    next();
                }
                break;
            case 1:
                updateBuddy(g);
                if (boat >= 0 && g.playerInVehicle(boat)) unblipVehicle(boat);
                if (arrived(g)) {
                    clearGoal(g);
                    beginOverwatch(g);
                }
                break;
            case 2: {   // the overwatch: first person on foot, the scope on the lookouts
                Ped* pl = g.playerPed();
                bool onFoot = pl && pl->state == PS_ONFOOT;
                bool atHummock = ::length(playerPos(g).xy() - over.xy()) < 40.f;
                setFirstPerson(g, onFoot && atHummock);
                if (onFoot && atHummock && pl->weapon != WPN_SNIPER && pl->hasWeapon[WPN_SNIPER] && stageTime < 0.2f) pl->weapon = WPN_SNIPER;
                if (pedAlive(g, jonah) && g.peds[jonah].vehicle < 0 && atHummock) setIdle(g, jonah, 18);
                int alive = countAlive(g, lookouts);
                bool alerted = false;
                for (int e : lookouts)
                    if (pedAlive(g, e) && g.peds[e].lastAttacker == g.player) alerted = true;
                if (alerted || alive < (int)lookouts.size())
                    for (int e : lookouts)
                        if (pedAlive(g, e) && g.peds[e].brain.type == BRAIN_NONE) setCombat(g, e, g.player, 0.15f);
                if (stageTime > 30.f && stageTime < 30.1f && alive == (int)lookouts.size()) say(g, CAST_JONAH, jonah, "[whisper:0.4]Breathe out, then squeeze. Don't pull.");
                if (alive == 0) {
                    say(g, CAST_JONAH, jonah, "[happy:0.4]Your daddy teach you to shoot like that?");
                    sayMe(g, "[calm]My daddy taught me to fix boats. I taught myself the rest.");
                    beginAssault(g);
                }
                break;
            }
            case 3:
                updateBuddy(g, 60.f);
                if (aliveEnemies(g) == 0) {
                    clearGoal(g);
                    beginChase(g);
                }
                break;
            case 4: {   // El Cuervo on the water
                updateBuddy(g);
                if (cuervoBoat < 0) {
                    setStage(5);
                    break;
                }
                float d = ::length(vehPos(g, cuervoBoat) - playerPos(g));
                if (d > 480.f) return fail("El Cuervo got away into the marsh.");
                if (g.vehicles[cuervoBoat].sim.health < 520.f || vehicleDisabled(g, cuervoBoat)) {
                    releaseDriver(g, cuervoBoat);
                    setStage(5);
                }
                break;
            }
            case 5: {   // cornered
                g.mClearBlips();
                g.mObjective("");
                vec3 cp3 = cuervoBoat >= 0 ? vehPos(g, cuervoBoat) : campWater;
                calmFires(g, cp3, 25.f);
                int pv = g.playerVehicle();
                vec3 w;
                if (pv >= 0 && g.isBoat(pv) && findWater(g, cp3.xy() + vec2(7.f, 5.f), 0.6f, w, 30.f)) teleportVehicle(g, pv, w, yawTo(w.xy(), cp3.xy()));
                if (pedAlive(g, cuervo)) setIdle(g, cuervo, 5);
                vec3 mp = playerPos(g), cp4 = pedAlive(g, cuervo) ? pedPos(g, cuervo) : cp3;
                std::vector<CutsceneShot> shots;
                shots.push_back(shotTwo(cp4, mp, 7.f, 6.f));
                shots.push_back(shotMove(cp4 + vec3(-5.f, -4.f, 2.6f), cp4 + vec3(0, 0, 1.2f), cp4 + vec3(-4.f, -3.f, 2.2f), cp4 + vec3(0, 0, 1.3f), 7.f, 40.f));
                shots.push_back(shotMove(mp + vec3(4.f, 4.f, 2.5f), mp + vec3(0, 0, 1.2f), mp + vec3(3.f, 3.2f, 2.2f), mp + vec3(0, 0, 1.3f), 7.f, 40.f));
                g.mCutscene(shots);
                say(g, CAST_CUERVO, cuervo, "[angry:0.5]Ortega. You always did come in through the front door.");
                sayMe(g, "[calm]It's over, Cuervo. Sable's books are on Kit's computer. Her guns are sinking in Jonah's swamp.");
                say(g, CAST_CUERVO, cuervo, "[calm]Over? She's cutting everybody loose. Me. Pruitt. The dock bosses. Tonight she sails, on the king tide.");
                say(g, CAST_CUERVO, cuervo, "[calm]The Palmera Horizon. Everything she owns goes aboard from that warehouse on Port Isle.");
                if (endedBroadcast(g))
                    say(g, CAST_CUERVO, cuervo, "[happy:0.3]Holt talks to the feds from her cell now. Sable doesn't leave loose ends twice. Ask Holt's lawyer. Oh wait.");
                else
                    say(g, CAST_CUERVO, cuervo, "[happy:0.3]And your captain rides with her. Holt paid you two million to keep quiet. Sable pays her a lot more to come along.");
                say(g, CAST_JONAH, jonah, "[calm]What do we do with him?");
                sayMe(g, "[calm]The sheriff has a cell with his name on it. Tie him up, Jonah. Gently.");
                say(g, CAST_JONAH, jonah, "[happy:0.3]Gently as the gators would.");
                next();
                break;
            }
            case 6:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "El Cuervo is in a Sawgrass County cell. Sable sails tonight on the king tide, with everything from her Port Isle "
                                       "warehouse. The crew is meeting at the Ortega boatyard.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        setFirstPerson(g, false);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && boat >= 0 && g.playerVehicle() != boat) t.enter(boat);
                if (t.stageTime > 1.2f) t.teleport(overWater, yawTo(overWater.xy(), over.xy()));
                break;
            case 2:
                if (t.stageTime > 0.5f && g.playerVehicle() >= 0) {
                    t.exitVehicle();
                    t.teleport(over, yawTo(over.xy(), camp.xy()));
                }
                if (t.stageTime > 1.5f && fmodf(t.stageTime, 0.8f) < dt)
                    for (int e : lookouts)
                        if (pedAlive(g, e)) {
                            t.shoot(e);
                            break;
                        }
                break;
            case 3:
                if (t.stageTime > 1.f) t.teleport(camp + vec3(6.f, 6.f, 0.f), 0.f);
                if (t.stageTime > 2.f) t.killEnemies();
                break;
            case 4:
                if (t.stageTime > 1.5f && cuervoBoat >= 0) g.damageVehicle(cuervoBoat, 600.f, g.player, vec3(0.f), vec3(0.f));
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 4-6: "King Tide" (Mari, with Dex). The finale in a storm. Take Sable's warehouse on Port Isle; the scene inside
// (staged in the warehouse interior); the chase to her ship at the east berth; the last choice under the cranes, and
// the epilogue.
class MissionKingTide : public StoryMission {
public:
    int dex = -1, rook = -1, kit = -1, tomas = -1, lucha = -1;
    int sable = -1, holt = -1, suv = -1, agent = -1;
    std::vector<int> inside;
    Place wh;
    bool whInterior = false;
    vec3 quay, gangway;
    int choice = 0;
    int endPhase = 0;
    float endT = 0.f, runT = 0.f, stormCheck = 20.f, whWait = 0.f;
    bool holtYielded = false;
    const char* title() const override { return "King Tide"; }
    const char* brief() const override {
        return "Sable sails tonight on the king tide with everything she owns. Take her warehouse on Port Isle, stop her before she reaches her "
               "ship, and decide what Calle Luna gets out of it.";
    }
    long long reward() const override { return choice == 2 ? 400000 : 60000; }

    void findWarehouse(GameWorld& g) {
        whInterior = placeFromInterior(g, "Port Isle Warehouse", World::IK_WAREHOUSE, 0, wh);
        if (!whInterior) wh = resolvePlace(g, vec2(4004.f, -820.f));
        quay = yardPoint(g, 4522.f, -792.f);
        gangway = yardPoint(g, 4592.f, -772.f);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        gMissions.allowSwitch = false;
        const Places& P = gPlaces;
        if (g.env->timeOfDay > 4.f && g.env->timeOfDay < 21.f) g.env->timeOfDay = 22.f;
        requestWeather(WX_STORM, true);
        score(SC_NOIR, 0.35f, 35);
        findWarehouse(g);
        int model = pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SPORTS}, 4);
        if (checkpoint >= 1) {
            playerCar = placePlayer(g, curbOffset(g, wh, -40.f), wh.curbYaw, model, lin(0.85f, 0.2f, 0.45f));
            dex = spawnPartner(g, 1, playerPos(g), 0.f, WPN_RIFLE);
            if (dex >= 0 && playerCar >= 0) g.warpPedIntoVehicle(dex, playerCar, 1);
            buddy = dex;
            beginYard(g);
            return;
        }
        const Place& B = P.boatyard;
        dex = spawnPartner(g, 1, placeOffset(g, B, 2.5f, 1.f), B.yaw, WPN_RIFLE);
        buddy = dex;
        rook = spawnCast(g, CAST_ROOK, placeOffset(g, B, -2.f, 2.2f), B.yaw + kPi, FAC_FRIEND);
        kit = spawnCast(g, CAST_KIT, placeOffset(g, B, 0.5f, 2.8f), B.yaw + kPi, FAC_FRIEND);
        tomas = spawnCast(g, CAST_TOMAS, placeOffset(g, B, 4.2f, 2.4f), B.yaw + kPi, FAC_FRIEND);
        lucha = spawnCast(g, CAST_LUCHA, placeOffset(g, B, -4.f, 1.4f), B.yaw + kPi, FAC_FRIEND);
        for (int p : {rook, kit, tomas, lucha})
            if (p >= 0) g.peds[p].invincible = true;
        placePlayer(g, placeOffset(g, B, 0.f, 0.3f), B.yaw);
        playerCar = spawnCar(g, model, curbOffset(g, B, 12.f), B.curbYaw, lin(0.85f, 0.2f, 0.45f));
        vec3 mp = playerPos(g), rp = pedPos(g, rook), kp = pedPos(g, kit), tp = pedPos(g, tomas), lp = pedPos(g, lucha), dp = pedPos(g, dex);
        for (int p : {rook, kit, tomas, lucha, dex}) facePed(g, p, mp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, B.door, B.yaw, 36.f, 13.f, 5.f);
        shots.push_back(shotTwo(rp, mp, 6.f));
        shots.push_back(shotTwo(kp, mp, 6.f, 4.f, 42.f, -1.f));
        shots.push_back(shotTwo(tp, lp, 6.f));
        shots.push_back(shotTwo(dp, mp, 6.f));
        g.mCutscene(shots);
        say(g, CAST_ROOK, rook, "[calm]Sable's warehouse sits on the west side of Port Isle. Her ship, the Palmera Horizon, is tied up at the east berth.");
        if (flag(g, EX_ACT4_SABLE) == 2)
            say(g, CAST_ROOK, rook, "[calm]After our noise in her container yard, she hired every rent-a-gun in the county. Expect a crowd.");
        else
            say(g, CAST_ROOK, rook, "[calm]She still doesn't know who opened her containers. The warehouse crew is the usual size.");
        say(g, CAST_KIT, kit, "[calm]High tide is at midnight. King tide, storm surge on top. She sails the minute the water is high enough.");
        if (endedBroadcast(g)) {
            say(g, CAST_KIT, kit, "[happy:0.3]Since the broadcast, Agent Dela Cruz at the state task force actually returns my calls. She'll take whatever we bring her.");
        } else {
            say(g, CAST_KIT, kit, "[calm]And Holt's going with her. She signed out of the precinct this afternoon. Vacation, apparently.");
            sayP(g, 1, dex, "[angry:0.4]She paid us two million to stay out of her way, and she still found a way to run with the sharks.");
            say(g, CAST_KIT, kit, "[calm]There's an agent on the state task force, Dela Cruz. Holt's people never got to her. She'll come if we call.");
        }
        say(g, CAST_TOMAS, tomas, "[calm]I'm coming with you.");
        sayMe(g, "[calm]No, you're not. You're staying with Lucha, and the boatyard, and the lights on.");
        say(g, CAST_LUCHA, lucha, "[calm]Bring them both home, Marisol. And bring her down.");
        sayMe(g, "[calm]Let's go end this.");
    }

    void beginYard(GameWorld& g) {
        g.mClearBlips();
        goTo(g, wh.curb, 30.f, "Get to ~y~Sable's warehouse~s~ on Port Isle.", false, false);
        setFollow(g, dex, g.player);
        score(SC_NOIR, 0.5f, 35);
        cp(g, 1);
        setStage(1);
    }

    void spawnFrontGuards(GameWorld& g) {
        int n = flag(g, EX_ACT4_SABLE) == 2 ? 9 : 7;   // she doubled up after the container yard was alarmed
        for (int i = 0; i < n; i++) {
            float along = -10.f + 3.4f * i;
            vec3 p = placeOffset(g, wh, along, i & 1 ? -1.5f : 0.8f);
            gunman(g, i & 1 ? CAST_GUARD_B : CAST_GUARD_A, p, wh.yaw + kPi, i % 3 == 0 ? WPN_RIFLE : WPN_SMG, 0.26f);
        }
        blipEnemies(g);
        g.mObjective("Take out ~r~Sable's security~s~ at the warehouse.");
        score(SC_CHASE, 1.f, 35);
    }

    // The scene inside Sable's warehouse (staged in its interior when the world has it)
    void warehouseScene(GameWorld& g) {
        bool bc = endedBroadcast(g);
        InteriorStage in = interiorStage("Port Isle Warehouse");
        vec3 mpos, dpos, spos, g1, g2, hpos;
        float syaw = 0.f;
        std::vector<CutsceneShot> shots;
        if (in.ok()) {
            // the warehouse floor: Sable waits on the lane in front of the racking aisles (where the layout keeps a
            // worker's spot clear), the crew comes in through the door (the staging pallets stay clear of it)
            const World::InteriorDef& D = *in.d;
            vec3 e = in.entry();
            vec3 sL;
            if (!in.scenario(World::SR_WORKER, 2, sL) || sL.z > 0.5f) sL = vec3(D.x0 + (D.x1 - D.x0) * 0.55f, 7.4f, 0.f);
            sL.y = Clamp(sL.y, e.y + 4.f, D.depth - 8.f);
            spos = in.at(sL);
            mpos = in.at(e + vec3(0.f, 1.6f, 0.f));
            dpos = in.at(e + vec3(-1.4f, 1.0f, 0.f));
            g1 = in.at(sL + vec3(2.4f, 0.f, 0.f));
            g2 = in.at(sL + vec3(-2.4f, 0.f, 0.f));
            hpos = in.at(sL + vec3(-1.2f, -1.4f, 0.f));
            syaw = yawTo(spos.xy(), mpos.xy());
            float side = e.x > sL.x ? 1.f : -1.f;
            shots.push_back(shotRoom(in, vec3(e.x + side * 4.5f, Max(1.2f, e.y + 0.4f), 3.6f), mpos, spos, 6.f));
        } else {
            spos = placeOffset(g, wh, 0.f, 5.5f);
            mpos = placeOffset(g, wh, -1.f, -0.5f);
            dpos = placeOffset(g, wh, 1.5f, -0.8f);
            g1 = placeOffset(g, wh, 2.5f, 5.f);
            g2 = placeOffset(g, wh, -2.5f, 5.f);
            hpos = placeOffset(g, wh, -1.5f, 6.5f);
            syaw = wh.yaw + kPi;
            establish(g, shots, spos, wh.yaw, 30.f, 10.f, 4.f);
        }
        if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
        placePlayer(g, mpos, yawTo(mpos.xy(), spos.xy()));
        if (dex >= 0) {
            if (g.peds[dex].vehicle >= 0) g.removePedFromVehicle(dex, false);
            placePed(g, dex, dpos, yawTo(dpos.xy(), spos.xy()));
            setIdle(g, dex, 0);
        }
        sable = spawnCast(g, CAST_SABLE, spos, syaw, FAC_CIVILIAN);
        if (sable >= 0) g.peds[sable].invincible = true;
        for (vec3 gp : {g1, g2}) {
            int e = gunman(g, CAST_GUARD_A, gp, syaw, WPN_SMG, 0.25f);
            if (e >= 0) {
                setIdle(g, e, 0);
                inside.push_back(e);
            }
        }
        if (!bc) {
            holt = spawnCast(g, CAST_HOLT, hpos, syaw, FAC_ENEMY);
            if (holt >= 0) {
                arm(g, holt, WPN_PISTOL, 8);
                g.peds[holt].maxHealth = g.peds[holt].health = 420.f;
                g.peds[holt].brain.accuracy = 0.3f;
                setIdle(g, holt, 0);
            }
        }
        vec3 mp = playerPos(g), sp = pedPos(g, sable), dp = pedPos(g, dex);
        shots.push_back(shotOver(mp, sp, 7.f));
        shots.push_back(shotOver(sp, mp, 7.f, -1.f));
        shots.push_back(shotTwo(dp, mp, 6.f));
        if (holt >= 0) shots.push_back(shotTwo(pedPos(g, holt), sp, 6.f, 4.f, 42.f, -1.f));
        shots.push_back(shotOver(mp, sp, 6.f, -1.f));
        g.mCutscene(shots);
        score(SC_NOIR, 0.6f, 35);
        say(g, CAST_SABLE, sable, "[calm]Miss Ortega. Mr. Calloway. You have cost me a great deal of money. I've come to respect that.");
        sayMe(g, "[calm]It's over, Sable. Your accountant, your guns, your enforcer. The task force has all of it.");
        say(g, CAST_SABLE, sable, "[calm]Paper. The port runs on paper, and my lawyers eat paper for breakfast. Victor never understood that. Neither do you.");
        if (bc) {
            say(g, CAST_SABLE, sable, "[calm]Your little radio show cost me my captain. Reyna is singing to the federal government from a cell. I don't leave loose ends twice.");
        } else {
            say(g, CAST_HOLT, holt, "[calm]I told you to keep your copies safe, Ortega. You should have stayed home and counted your money.");
            sayP(g, 1, dex, "[calm]Captain. Fancy meeting you at a crime scene.");
        }
        sayMe(g, "[angry:0.5]You were going to pave Calle Luna for a parking lot. Lucha's diner. My father's yard.");
        say(g, CAST_SABLE, sable, "[calm]For a container terminal. Parking lots don't pay. The Palmera Horizon sails at high tide, with me on it. Goodbye.");
        next();
    }

    void insideFight(GameWorld& g) {
        for (int e : inside)
            if (pedAlive(g, e)) setCombat(g, e, g.player, 0.26f);
        if (pedAlive(g, holt)) setCombat(g, holt, g.player, 0.3f);
        // Sable slips out through the loading door while her people hold the floor
        if (pedAlive(g, sable)) {
            g.peds[sable].invincible = true;
            setGoto(g, sable, pedPos(g, sable) + vec3((wh.outward * 12.f), 0.f), 2.4f);
        }
        blipEnemies(g);
        if (holt >= 0) g.mBlipPed(holt, UI::BLIP_ENEMY);
        g.mObjective("Take out Sable's ~r~bodyguards~s~.");
        score(SC_CHASE, 1.f, 35);
        cp(g, 2);
        setStage(5);
    }

    void startSuvChase(GameWorld& g) {
        g.mClearBlips();
        int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 6);
        float yaw = wh.curbYaw;
        vec3 sp = curbOffset(g, wh, 30.f, &yaw);
        suv = spawnCar(g, model, sp, yaw, lin(0.03f, 0.03f, 0.035f));
        if (suv >= 0) {
            if (sable >= 0) {
                g.peds[sable].invincible = true;
                g.warpPedIntoVehicle(sable, suv, 0);
            }
            ScriptDriver& d = driveRoad(g, suv, quay.xy(), 19.f, true);
            d.rubberPed = g.player;
            d.rubberGap = 60.f;
            g.vehicles[suv].sim.health = 1000.f;
            g.mBlipVehicle(suv, UI::BLIP_ENEMY);
        }
        if (playerCar >= 0 && vehicleAlive(g, playerCar)) g.mBlipVehicle(playerCar, UI::BLIP_VEHICLE);
        radioLine(g, CAST_KIT, "[shout]She's out the loading door! Black SUV, heading across the yard for the Palmera Horizon!");
        sayP(g, 1, dex, "[shout]Car! Get to the car!");
        g.mObjective("Stop ~r~Sable~s~ before she reaches her ship.");
        score(SC_CHASE, 1.f, 35);
        cp(g, 3);
        setStage(6);
    }

    void quayScene(GameWorld& g) {
        g.mClearBlips();
        g.mObjective("");
        if (suv >= 0) releaseDriver(g, suv);
        if (pedAlive(g, sable) && g.peds[sable].vehicle >= 0) g.removePedFromVehicle(sable, false);
        vec3 sp = pedAlive(g, sable) ? pedPos(g, sable) : quay;
        calmFires(g, sp, 25.f);
        if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
        vec2 toShip = normalize(gangway.xy() - sp.xy() + vec2(1e-3f, 0.f));
        vec3 mp = sp - vec3(toShip * 5.5f, 0.f);
        mp.z = groundAt(g, mp.x, mp.y, sp.z + 3.f);
        placePlayer(g, mp, yawTo(mp.xy(), sp.xy()));
        if (pedAlive(g, sable)) {
            placePed(g, sable, sp, yawTo(sp.xy(), mp.xy()));
            setIdle(g, sable, 0);
        }
        if (dex >= 0) {
            if (g.peds[dex].vehicle >= 0) g.removePedFromVehicle(dex, false);
            vec2 sidev(toShip.y, -toShip.x);
            placePed(g, dex, mp + vec3(sidev * 2.2f - toShip * 0.8f, 0.f), yawTo(mp.xy(), sp.xy()));
            setIdle(g, dex, 0);
        }
        vec3 dp = pedPos(g, dex);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotMove(sp + vec3(-toShip * 30.f + vec2(0.f, 25.f), 30.f), sp + vec3(0, 0, 10.f), sp + vec3(-toShip * 24.f + vec2(0.f, 18.f), 20.f),
                                 sp + vec3(0, 0, 3.f), 6.f, 55.f));
        shots.push_back(shotOver(mp, sp, 7.f));
        shots.push_back(shotOver(sp, mp, 7.f, -1.f));
        shots.push_back(shotTwo(dp, mp, 6.f));
        g.mCutscene(shots);
        say(g, CAST_SABLE, sable, "[calm]Well. Here we are. The rain, the cranes, the ship. It's almost cinematic.");
        say(g, CAST_SABLE, sable, "[calm]There are four million dollars in cash cases in that car, Miss Ortega. Untraceable. Nobody will ever ask where they went.");
        say(g, CAST_SABLE, sable, "[calm]Take them, and I walk up that gangway and never see Porto Sol again. Or call your agent, and spend the next five years in court watching my lawyers work.");
        sayP(g, 1, dex, "[calm]Your call, Mari. Same as last time.");
        setStage(7);
    }

    void epilogue(GameWorld& g) {
        const Places& P = gPlaces;
        requestWeather(WX_FAIR, true);
        g.env->timeOfDay = 7.f;
        std::vector<CutsceneShot> shots;
        vec3 cranes(4570.f, -560.f, 20.f);
        shots.push_back(shotMove(cranes + vec3(-260.f, -180.f, 60.f), cranes + vec3(0, 0, 25.f), cranes + vec3(-200.f, -140.f, 45.f), cranes + vec3(0, 0, 18.f), 8.f, 55.f));
        if (!establish(g, shots, P.boatyard.door, P.boatyard.yaw, 34.f, 11.f, 8.f, 45.f))
            shots.push_back(shotMove(P.boatyard.door + vec3(-40.f, -30.f, 12.f), P.boatyard.door, P.boatyard.door + vec3(-25.f, -20.f, 6.f), P.boatyard.door + vec3(0, 0, 1.2f), 8.f, 45.f));
        establish(g, shots, P.diner.door, P.diner.yaw, 30.f, 10.f, 8.f, 45.f);
        shots.push_back(shotMove(P.riverMouth + vec3(-90.f, -70.f, 9.f), P.riverMouth + vec3(0.f, 0.f, 4.f), P.riverMouth + vec3(-75.f, -55.f, 34.f),
                                 P.riverMouth + vec3(60.f, 30.f, 14.f), 90.f, 55.f));
        g.mCutscene(shots, true);
        bool bc = endedBroadcast(g);
        if (choice == 1) {
            narrator(g, "Pulse FM News", "State and federal agents seized the cargo ship Palmera Horizon at Port Isle overnight. Sable Maritime chief Ines Sable "
                                    "faces charges of racketeering, arms trafficking and bribery of public officials.", "newsreader_female", "[news]", kColKit);
            narrator(g, "Pulse FM News", bc ? "Former police captain Reyna Holt has agreed to testify. The Calle Luna waterfront plan has been withdrawn, and "
                                              "the port authority announced a community lease for the Rio Sol boatyards."
                                            : "Police captain Reyna Holt was arrested at the scene. The Calle Luna waterfront plan has been withdrawn, and the "
                                              "port authority announced a community lease for the Rio Sol boatyards.",
                     "newsreader_female", "[news]", kColKit);
            sayP(g, 1, -1, "[calm]A community lease. Your dad's yard, for ninety-nine years.");
            sayP(g, 0, -1, "[happy:0.5]Ninety-nine years. Tomas will still find a way to sink something.");
            sayP(g, 1, -1, "[happy:0.4]Pozole?");
            sayP(g, 0, -1, "[happy]Pozole.");
        } else {
            narrator(g, "Pulse FM News", "The cargo ship Palmera Horizon left Port Isle during last night's storm, hours ahead of a federal search warrant. "
                                    "Its owner, Ines Sable, is believed to be aboard.", "newsreader_female", "[news]", kColKit);
            narrator(g, "Pulse FM News", bc ? "In Calle Luna, an anonymous donor has funded a new clinic and a year of free lunches at the Rio Sol school."
                                            : "Police captain Reyna Holt, reported missing since last night, was found at a Port Isle pier in handcuffs, "
                                              "with a copy of her own ledger. In Calle Luna, an anonymous donor has funded a new clinic.",
                     "newsreader_female", "[news]", kColKit);
            sayP(g, 1, -1, "[calm]Four million in wet cash. Kit is still ironing hundred dollar bills.");
            sayP(g, 0, -1, "[calm]A clinic. A school bus that doesn't break down. A roof for Rosa.");
            sayP(g, 1, -1, "[calm]And Sable?");
            sayP(g, 0, -1, "[sad:0.3]Sable is somebody else's problem now. The tide took her. For now.");
        }
    }

    // fade to black, the epilogue out of it, the title card, a fade out, free roam (as at the end of Act 3)
    MissionStatus ending(GameWorld& g, float dt, const char* afterword) {
        endT += dt;
        switch (endPhase) {
            case 0:
                if (g.mTalking() || g.mInCutscene()) break;
                g.fadeOut(1.1f);
                endPhase = 1;
                endT = 0.f;
                break;
            case 1:
                if (g.fadedOut() || endT > 2.5f) {
                    epilogue(g);
                    g.fadeIn(0.8f);
                    endPhase = 2;
                    endT = 0.f;
                }
                break;
            case 2:
                if (!g.mTalking() && endT > 2.f) {
                    gMissions.cardTitle = "NEON TIDE";
                    gMissions.cardSub = "Undertow - The End";
                    gMissions.cardT = 0.f;
                    gMissions.cardDelay = 0.4f;
                    gMissions.cardCentered = true;
                    endPhase = 3;
                    endT = 0.f;
                }
                break;
            case 3:
                if (endT > 6.4f || gMissions.cardT < 0.f) {
                    g.fadeOut(1.2f);
                    endPhase = 4;
                    endT = 0.f;
                }
                break;
            case 4:
                if (g.fadedOut() || endT > 2.5f) {
                    gMissions.shots.clear();
                    gMissions.shotIndex = -1;
                    g.fadeIn(0.7f);
                    g.storyBriefText = afterword;
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        if (passed) setFlag(g, EX_ACT4_CHOICE, choice);
        if (!passed) requestWeather(WX_FAIR, false);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, dex, "Dex")) return MS_FAILED;
        // the storm holds until the epilogue (the weather cycle would otherwise clear it mid-mission)
        stormCheck -= dt;
        if (stage < 9 && stormCheck <= 0.f) {
            stormCheck = 20.f;
            if (g.env && g.env->rain < 0.6f) requestWeather(WX_STORM, false);
        }
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) beginYard(g);
                break;
            case 1:
                updateBuddy(g);
                if (g.playerAt(wh.curb.xy(), 70.f)) {
                    clearGoal(g);
                    spawnFrontGuards(g);
                    sayP(g, 1, dex, "[shout]Welcome committee. Stay behind the containers!");
                    next();
                }
                break;
            case 2:
                updateBuddy(g, 50.f);
                if (aliveEnemies(g) == 0) {
                    g.mClearBlips();
                    goTo(g, wh.door, 1.6f, "Go into the ~y~warehouse~s~.");
                    next();
                }
                break;
            case 3:
                updateBuddy(g);
                if (arrived(g) && g.playerVehicle() < 0 && !interiorWait("Port Isle Warehouse", whWait, dt)) {
                    clearGoal(g);
                    warehouseScene(g);
                }
                break;
            case 4:
                if (!g.mInCutscene() && !g.mTalking()) insideFight(g);
                break;
            case 5: {
                updateBuddy(g, 40.f);
                if (pedAlive(g, holt) && !holtYielded && g.peds[holt].health < 180.f) {
                    holtYielded = true;
                    enemies.erase(std::remove(enemies.begin(), enemies.end(), holt), enemies.end());
                    g.peds[holt].faction = FAC_CIVILIAN;
                    g.peds[holt].invincible = true;
                    setIdle(g, holt, 5);
                    say(g, CAST_HOLT, holt, "[angry]Enough! Enough. I'm done. Tell your agent I want a deal.");
                    unblipPed(holt);
                }
                bool holtDown = holt < 0 || !pedAlive(g, holt) || holtYielded;
                if (pedAlive(g, sable) && stageTime > 4.f && g.peds[sable].vehicle < 0 && ::length(pedPos(g, sable) - playerPos(g)) > 6.f) {
                    g.peds[sable].brain.type = BRAIN_NONE;   // (out of the loading door)
                }
                if (aliveEnemies(g) == 0 && holtDown) startSuvChase(g);
                break;
            }
            case 6: {   // the chase to the ship
                updateBuddy(g);
                if (suv < 0 || !vehicleAlive(g, suv)) {
                    if (!pedAlive(g, sable)) return fail("Sable is dead. Nobody wanted it to end like that.");
                    quayScene(g);
                    break;
                }
                float d = ::length(vehPos(g, suv) - playerPos(g));
                ScriptDriver* sd = driverFor(suv);
                if (d > 600.f) return fail("Sable got away.");
                if (vehicleDisabled(g, suv) || g.vehicles[suv].sim.health < 450.f) {
                    quayScene(g);
                    break;
                }
                if (sd && sd->done) {
                    // on foot for the gangway: catch her
                    releaseDriver(g, suv);
                    if (pedAlive(g, sable)) {
                        g.removePedFromVehicle(sable, true);
                        setGoto(g, sable, gangway, 2.8f);
                    }
                    g.mObjective("Catch ~r~Sable~s~ before she's up the gangway.");
                    runT = 0.f;
                    setStage(20);
                }
                break;
            }
            case 20:
                updateBuddy(g);
                runT += dt;
                if (!pedAlive(g, sable)) return fail("Sable is dead. Nobody wanted it to end like that.");
                if (::length(pedPos(g, sable).xy() - gangway.xy()) < 1.5f && runT > 3.f) return fail("Sable made it aboard the Palmera Horizon.");
                if (::length(pedPos(g, sable) - playerPos(g)) < 4.f || givesUp(g, sable)) quayScene(g);
                break;
            case 7:
                if (!g.mInCutscene() && !g.mTalking() && !menuIs(MO_CHOICE)) {
                    std::vector<MenuItem> items(2);
                    items[0].label = "Turn her in";
                    items[0].detail = "Agent Dela Cruz is minutes away. Sable stands trial, the ship and the warehouse are seized, and the waterfront goes back "
                                      "to the people who live on it.";
                    items[0].id = 1;
                    items[1].label = "Take the money";
                    items[1].detail = "Four million in untraceable cash for Calle Luna. Sable walks up the gangway with nothing but her name and sails into "
                                      "the storm.";
                    items[1].id = 2;
                    menuOpen(g, MO_CHOICE, "KING TIDE", "What does Mari do with Sable?", items, true, 0xff40c0ffu);
                    next();
                }
                break;
            case 8:
                if (menuIs(MO_CHOICE) && gMenu.chosen > 0) {
                    choice = gMenu.chosen;
                    menuClose(g);
                    setFlag(g, EX_ACT4_CHOICE, choice);
                    if (choice == 1) {
                        sayMe(g, "[calm]No deal. Dex, call Dela Cruz.");
                        say(g, CAST_SABLE, sable, "[calm]You'll regret this. Not today. But you will.");
                        int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_SUV}, 9);
                        vec3 ap2 = yardPoint(g, quay.x - 55.f, -790.f);   // up the cross lane from the quay (clear of the stacks)
                        int car = spawnCar(g, model, ap2, yawTo(ap2.xy(), quay.xy()), lin(0.1f, 0.12f, 0.16f));
                        if (car >= 0) g.vehicles[car].sirenOn = true;
                        agent = spawnCast(g, CAST_AGENT, ap2 + vec3(3.f, 2.f, 0.f), yawTo(ap2.xy(), quay.xy()), FAC_CIVILIAN);
                        if (agent >= 0) setGoto(g, agent, pedPos(g, sable) + vec3(-2.f, -2.f, 0.f), 1.6f);
                        if (pedAlive(g, sable)) setIdle(g, sable, 5);
                        say(g, CAST_AGENT, agent, "[calm]Ines Sable, you're under arrest. Somebody get her out of the rain before she catches her death.");
                        say(g, CAST_AGENT, agent, "[calm]Ms. Ortega. Mr. Calloway. Kit Navarro said you'd have something for me. I didn't expect a whole shipping company.");
                        sayMe(g, "[calm]Keep the ship. We just want the neighborhood.");
                    } else {
                        sayMe(g, "[calm]Walk, Sable. Up the gangway. Don't look back and don't come back.");
                        say(g, CAST_SABLE, sable, "[calm]Pragmatic. I misjudged you, Miss Ortega. That doesn't happen often.");
                        if (pedAlive(g, sable)) setGoto(g, sable, gangway, 1.3f);
                        sayP(g, 1, dex, "[calm]Four cases. Heavy ones. Calle Luna is about to get a very generous anonymous donor.");
                    }
                    next();
                }
                break;
            case 9:
                return ending(g, dt,
                              choice == 1 ? "Mari turned Ines Sable over to the task force. The waterfront belongs to Calle Luna again, and Porto Sol is yours to explore."
                                          : "Mari took Sable's four million for Calle Luna and let her sail into the storm. Porto Sol is yours to explore.");
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && g.playerVehicle() < 0 && playerCar >= 0) t.enter(playerCar);
                testGoal(g, t, dt, 70.f);
                break;
            case 2: if (t.stageTime > 1.5f) t.killEnemies(); break;
            case 3:
                if (t.stageTime > 0.5f) {
                    t.exitVehicle();
                    t.teleport(wh.door, wh.yaw + kPi);
                }
                break;
            case 5:
                if (t.stageTime > 1.5f) {
                    t.killEnemies();
                    if (pedAlive(g, holt) && !holtYielded) g.peds[holt].health = 150.f;
                }
                break;
            case 6:
                if (t.stageTime > 1.5f && suv >= 0) g.damageVehicle(suv, 700.f, g.player, vec3(0.f), vec3(0.f));
                break;
            case 20:
                if (t.stageTime > 0.5f && pedAlive(g, sable)) t.teleport(pedPos(g, sable) + vec3(2.f, 0.f, 0.f), 0.f);
                break;
            case 8:
                if (menuIs(MO_CHOICE) && t.stageTime > 0.5f) {
                    const char* c = Platform::argValue("act4choice");
                    gMenu.chosen = c && (strcmp(c, "money") == 0 || strcmp(c, "2") == 0) ? 2 : 1;
                }
                break;
            default: break;
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// After the Act 3 ending: a quiet moment in free roam, then the Act 4 title card; Lucha's call follows it.
struct Act4State {
    float calm = 0.f;
    bool test = false;   // --missiontest act4_card: the unlock runs under the test harness too
};
Act4State gAct4;

void act4Update(GameWorld& g, float dt) {
    if (!storyDone(g, SF_SIGNAL) || flag(g, EX_ACT4_CARD)) return;
    if (!gAct4.test && (Platform::argValue("missiontest") || Platform::argValue("mission"))) return;
    gEco.callCooldown = Max(gEco.callCooldown, 5.f);   // (the story calls wait for the card)
    bool calm = !gMissions.active && !g.mInCutscene() && g.playerControl && !g.mTalking() && g.fadeAlpha < 0.05f && g.pinfo.wanted == 0 &&
                g.pinfo.deathTimer <= 0.f && !openWorldBusy();
    gAct4.calm = calm ? gAct4.calm + dt : 0.f;
    if (gAct4.calm < 10.f) return;
    setFlag(g, EX_ACT4_CARD, 1);
    gMissions.cardTitle = "UNDERTOW";
    gMissions.cardSub = "Act IV";
    gMissions.cardT = 0.f;
    gMissions.cardDelay = 0.3f;
    gMissions.cardCentered = true;
    score(SC_NOIR, 0.3f, 36);
    g.storyBriefText = "A month later, Calle Luna is quiet. Mama Lucha is throwing a dinner at the diner for the whole crew.";
    gEco.callCooldown = 12.f;
    LOG("Act 4 unlocked: title card");
}

}  // namespace mu
}  // namespace Game
