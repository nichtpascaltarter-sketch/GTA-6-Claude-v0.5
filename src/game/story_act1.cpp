// Story, Prologue + Act 1 "Calle Luna": Low Tide, Repo Man, Dry Dock, Pressure Cooker, Collateral, Pink Slips, Last Call.
// Mari protects her brother and the neighborhood; Dex, a repo man working for Rook, first meets her as an adversary
// and becomes her partner; the Cuervos' attacks lead back to Captain Holt and Victor Sandoval.
#include "missions.h"

namespace Game {
namespace mu {

// ==================================================================================================================
// Prologue: "Low Tide" (Mari). Tomas calls in a panic; pick him up at the old Vargas garage, shake off the Cuervos who
// follow, and bring him to Mama Lucha's diner.
class MissionLowTide : public StoryMission {
public:
    int tomas = -1, chaser = -1, lucha = -1;
    vec3 garage, garageCurb, diner, dinerDoor;
    float garageYaw = 0.f;
    const char* title() const override { return "Low Tide"; }
    const char* brief() const override {
        return "Tomas called in a panic from the old Vargas garage in Calle Luna. Pick him up, lose whoever is after him and get him "
               "to Mama Lucha's diner.";
    }
    long long reward() const override { return 1500; }

    void spawnChaser(GameWorld& g) {
        int model = pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SEDAN}, 1);
        if (model < 0) return;
        float yaw;
        vec3 sp = approachSpot(g, gPlaces.vargasGarage, -90.f, &yaw);
        chaser = attackCar(g, model, sp, yaw, 2, WPN_PISTOL, 0.18f, 0);
        if (chaser < 0) return;
        for (int s = 0; s < 2; s++) {
            int p = g.vehicles[chaser].seats[s];
            if (p >= 0) setCombat(g, p, g.player, 0.18f);
        }
        g.mBlipVehicle(chaser, UI::BLIP_ENEMY);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        garage = P.vargasGarage.pos;
        garageCurb = P.vargasGarage.curb;
        garageYaw = P.vargasGarage.curbYaw;
        diner = P.diner.curb;
        dinerDoor = P.diner.door;
        score(SC_NOIR, 0.3f, 0);
        tomas = spawnCast(g, CAST_TOMAS, P.vargasGarage.door, P.vargasGarage.yaw + kPi, FAC_FRIEND);
        if (tomas >= 0) {
            g.peds[tomas].maxHealth = g.peds[tomas].health = 220.f;
            setIdle(g, tomas, 8);   // on the phone
        }
        if (checkpoint >= 1) {
            // restart: already driving with Tomas, the Cuervos right behind
            int model = pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SEDAN}, 3);
            playerCar = placePlayer(g, curbOffset(g, P.vargasGarage, 8.f), garageYaw, model, lin(0.55f, 0.06f, 0.2f));
            if (playerCar >= 0 && tomas >= 0) g.warpPedIntoVehicle(tomas, playerCar, 1);
            setFollow(g, tomas, g.player);
            spawnChaser(g);
            g.mObjective("Lose the ~r~Cuervos~s~.");
            score(SC_CHASE, 0.7f, 0);
            setStage(3);
            return;
        }
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PHONE_RING, 0.8f);
#endif
        phoneLine(g, CAST_TOMAS, "[scared]Mari, it's me. I messed up. I need you at the old Vargas garage, right now. Please hurry.");
        sayMe(g, "[calm:0.6]Tomas? Hey, slow down. Stay there, I'm coming.");
        goTo(g, garageCurb, 6.f, "Get to the old ~y~Vargas garage~s~ in Calle Luna.", true);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, tomas, "Tomas")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (stageTime > 6.f && g.hudHelpTimer <= 0.f && g.playerVehicle() < 0 && stageTime < 8.f)
                    g.help("Steal a car: walk up to one and press ~i:F|Y~.", 5.f);
                if (arrived(g)) {
                    clearGoal(g);
                    playerCar = g.playerVehicle();
                    vec3 tp = pedPos(g, tomas);
                    std::vector<CutsceneShot> shots;
                    establish(g, shots, tp, garageYaw, 26.f, 9.f, 3.5f);
                    shots.push_back(shotTwo(tp, playerPos(g), 6.5f));
                    g.mCutscene(shots);
                    if (tomas >= 0) setGoto(g, tomas, playerPos(g), 3.5f);
                    gesture(g, tomas, Anim::CLIP_WAVE);
                    say(g, CAST_TOMAS, tomas, "[scared:0.7]They took the money I was holding for them. Two guys from the Cuervos. They think I skimmed it.");
                    sayMe(g, "Did you?");
                    say(g, CAST_TOMAS, tomas, "[scared]No! I swear. Just get me to Lucha's. She'll know what to do.");
                    next();
                }
                break;
            case 1:
                if (!g.mInCutscene() && !g.mTalking()) {
                    int pv = g.playerVehicle();
                    if (pv >= 0) {
                        int seat = g.freeSeat(pv, false);
                        if (seat > 0) g.warpPedIntoVehicle(tomas, pv, seat);
                    }
                    setFollow(g, tomas, g.player);
                    spawnChaser(g);
                    score(SC_CHASE, 0.7f, 0);
                    say(g, CAST_TOMAS, tomas, "[scared]Mari... that black car. It's them!");
                    g.mObjective("Lose the ~r~Cuervos~s~.");
                    cp(g, 1);
                    setStage(3);
                }
                break;
            case 3: {
                // lose the chasers: destroy the car / take out both, or get far away
                bool gone = chaser < 0 || !vehicleAlive(g, chaser) || aliveEnemies(g) == 0;
                float d = chaser >= 0 ? ::length(vehPos(g, chaser) - playerPos(g)) : 1e9f;
                if (d > 220.f) timer += dt;
                else timer = 0.f;
                if (stageTime > 4.f && stageTime < 4.1f) say(g, CAST_TOMAS, tomas, "[shout]They're shooting at us! Drive, drive!");
                if (stageTime > 18.f && stageTime < 18.1f) sayMe(g, "[shout:0.6]Hold on to something.");
                if (gone || timer > 3.f) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    releaseDriver(g, chaser);
                    score(SC_NOIR, 0.4f, 0);
                    sayMe(g, "[calm]I think we lost them.");
                    say(g, CAST_TOMAS, tomas, "[sad:0.5]Lucha's gonna kill me.");
                    sayMe(g, "[happy:0.5]Lucha's gonna feed you. Then she's gonna kill you.");
                    goTo(g, diner, 5.f, "Take Tomas to ~y~Mama Lucha's diner~s~.", true);
                    next();
                }
                break;
            }
            case 4: {
                bool withMe = g.peds[tomas].vehicle >= 0 && g.peds[tomas].vehicle == g.playerVehicle();
                bool onFootTogether = g.playerVehicle() < 0 && ::length(pedPos(g, tomas) - playerPos(g)) < 12.f;
                if (stageTime > 3.f && !withMe && !onFootTogether && g.hudHelpTimer <= 0.f) g.help("Pick up ~b~Tomas~s~.", 2.f);
                if (arrived(g) && (withMe || onFootTogether)) {
                    clearGoal(g);
                    g.mObjective("");
                    lucha = spawnCast(g, CAST_LUCHA, dinerDoor, gPlaces.diner.yaw + kPi, FAC_FRIEND);
                    if (lucha >= 0) g.peds[lucha].invincible = true;
                    if (g.peds[tomas].vehicle >= 0) g.removePedFromVehicle(tomas, true);
                    vec3 lp = pedPos(g, lucha);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(lp, playerPos(g), 5.f, 4.5f, 40.f, -1.f));
                    shots.push_back(shotOver(playerPos(g), lp, 7.f));
                    establish(g, shots, lp, gPlaces.diner.yaw, 30.f, 12.f, 4.f);
                    g.mCutscene(shots);
                    say(g, CAST_LUCHA, lucha, "[scared:0.8]Marisol! Dios mio, what happened to him?");
                    sayMe(g, "Cuervos. He says he owes them money.");
                    say(g, CAST_LUCHA, lucha, "[angry:0.6]Inside, both of you. Nobody touches my boy under my roof.");
                    say(g, CAST_TOMAS, tomas, "[sad:0.4]Thank you, Lucha. I'll fix this, I swear.");
                    setGoto(g, tomas, dinerDoor + vec3(gPlaces.diner.outward * 3.f, 0.f), 1.5f);
                    next();
                }
                break;
            }
            case 5:
                if (!g.mInCutscene() && !g.mTalking() && stageTime > 1.f) {
                    g.storyBriefText =
                        "Mama Lucha is hiding Tomas at the diner. The Cuervos will be back - and somebody is paying them. Across town, a repo "
                        "man named Dex Calloway is about to get a call from Rook.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 0:
                if (g.playerVehicle() < 0 && t.stageTime > 0.5f) {
                    int m = pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SEDAN});
                    int v = spawnCar(g, m, playerPos(g) + vec3(3, 0, 0), 0.f);
                    t.enter(v);
                }
                testGoal(g, t, dt);
                break;
            case 3:
                if (t.stageTime > 3.f) t.destroy(chaser);
                if (t.stageTime > 4.f) t.killEnemies();
                break;
            case 4: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 1-1: "Repo Man" (Dex). Rook sends Dex to take back a Cuervo lieutenant's unpaid muscle car.
class MissionRepoMan : public StoryMission {
public:
    int rook = -1, target = -1, chaseCar = -1;
    vec3 hangout, shopDoor;
    std::vector<int> loiterers;
    const char* title() const override { return "Repo Man"; }
    const char* brief() const override {
        return "Rook has a repo job: a lieutenant of the Cuervos stopped paying for his muscle car. Take it from his hangout in Calle Luna "
               "and bring it to Rook's salvage yard. Rook pays by the condition of the car.";
    }
    long long reward() const override { return 2000; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        shopDoor = P.rookShop.door;
        Place hang = resolvePlace(g, vec2(1450, -260));
        hangout = hang.curb;
        int model = pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SPORTS}, 1);
        target = spawnCar(g, model, hang.curb, hang.curbYaw, lin(0.08f, 0.35f, 0.3f));
        if (target >= 0) {
            g.vehicles[target].locked = false;
            g.vehicles[target].sim.engineOn = false;
            g.vehicles[target].parked = true;
        }
        // three Cuervos hanging around the car
        for (int i = 0; i < 3; i++) {
            vec3 p = placeOffset(g, hang, -3.f + i * 2.5f, 0.2f);
            int e = spawnCast(g, CAST_THUG_A + i, p, hang.yaw + kPi * 0.5f, FAC_ENEMY);
            if (e < 0) continue;
            arm(g, e, i == 1 ? WPN_SMG : WPN_PISTOL);
            g.peds[e].brain.accuracy = 0.25f;
            setIdle(g, e, i == 0 ? 7 : (i == 1 ? 0 : 8));
            loiterers.push_back(e);
            enemies.push_back(e);
        }
        score(SC_NOIR, 0.3f, 1);
        if (checkpoint >= 1) {
            // restart in the car with the Cuervos giving chase
            placePlayer(g, hangout + vec3(0, 0, 0.5f), hang.curbYaw);
            if (target >= 0) {
                teleportVehicle(g, target, hangout, hang.curbYaw);
                g.warpPedIntoVehicle(g.player, target, 0);
            }
            alarm(g);
            setStage(3);
            return;
        }
        rook = spawnCast(g, CAST_ROOK, shopDoor, P.rookShop.yaw + kPi, FAC_FRIEND);
        provideRide(g, P.rookShop, -14.f);
        vec3 rp = pedPos(g, rook);
        std::vector<CutsceneShot> shots;
        establish(g, shots, rp, P.rookShop.yaw, 34.f, 14.f, 4.f);
        shots.push_back(shotTwo(rp, playerPos(g), 7.f));
        shots.push_back(shotOver(playerPos(g), rp, 7.f, -1.f));
        shots.push_back(shotOver(rp, playerPos(g), 6.f));
        g.mCutscene(shots);
        facePed(g, rook, playerPos(g));
        say(g, CAST_ROOK, rook, "[happy:0.4]Dex. You look like you slept in a car again.");
        sayMe(g, "I did. What have you got for me, Rook?");
        say(g, CAST_ROOK, rook, "Green Gatorback. Belongs to a Cuervo lieutenant who stopped paying my cousin's lot. Four months.");
        sayMe(g, "A Cuervo. Great. And you want me to walk up and ask for the keys.");
        say(g, CAST_ROOK, rook, "I want it here, in one piece. Every dent comes out of your cut. It's parked in Calle Luna, south of the river.");
    }

    void alarm(GameWorld& g) {
        for (int e : loiterers)
            if (pedAlive(g, e)) setCombat(g, e, g.player, 0.25f);
        // one of them jumps into a second car and gives chase
        if (chaseCar < 0) {
            int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_COUPE}, 5);
            float yaw;
            vec3 sp = approachSpot(g, resolvePlace(g, vec2(1450, -260)), -60.f, &yaw);
            chaseCar = attackCar(g, model, sp, yaw, 2, WPN_PISTOL, 0.2f, 2);
            if (chaseCar >= 0) {
                for (int s = 0; s < 2; s++)
                    if (g.vehicles[chaseCar].seats[s] >= 0) setCombat(g, g.vehicles[chaseCar].seats[s], g.player, 0.2f);
                g.mBlipVehicle(chaseCar, UI::BLIP_ENEMY);
            }
        }
        g.mObjective("Lose the ~r~Cuervos~s~.");
        score(SC_CHASE, 0.75f, 1);
        cp(g, 1);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, target, "Gatorback")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (rook >= 0) setGoto(g, rook, shopDoor + vec3(gPlaces.rookShop.outward * 5.f, 0.f), 1.2f);
                    goTo(g, hangout, 30.f, "Go to the Cuervo hangout in ~y~Calle Luna~s~.", false, false);
                    g.mBlipVehicle(target, UI::BLIP_VEHICLE);
                    g.mClearTarget();
                    g.mTarget(hangout.xy(), UI::BLIP_VEHICLE);
                    next();
                }
                break;
            case 1:
                if (g.playerAt(hangout.xy(), 70.f)) {
                    g.mObjective("Steal the ~b~Gatorback~s~. The Cuervos won't give it up quietly.");
                    sayMe(g, "[calm]There she is. And there's the welcome committee.");
                    next();
                }
                break;
            case 2:
                if (g.playerInVehicle(target)) {
                    g.mClearTarget();
                    say(g, CAST_THUG_A, loiterers.empty() ? -1 : loiterers[0], "[shout]Hey! That's Chuy's ride! Get him!");
                    alarm(g);
                    setStage(3);
                } else {
                    // they notice someone creeping up with a gun out
                    Ped* pl = g.playerPed();
                    if (pl && pl->aiming && g.playerAt(hangout.xy(), 25.f)) {
                        alarm(g);
                        g.mObjective("Steal the ~b~Gatorback~s~.");
                        setStage(21);
                    }
                }
                break;
            case 21:   // fighting on foot before getting in
                if (g.playerInVehicle(target)) {
                    g.mObjective("Lose the ~r~Cuervos~s~.");
                    setStage(3);
                }
                if (abandoned(g, target, 200.f, "Gatorback")) return MS_FAILED;
                break;
            case 3: {
                if (abandoned(g, target, 150.f, "Gatorback")) return MS_FAILED;
                bool chaseGone = chaseCar < 0 || vehicleDisabled(g, chaseCar);
                float d = chaseGone ? 1e9f : ::length(vehPos(g, chaseCar) - playerPos(g));
                bool footGone = true;
                for (int e : loiterers)
                    if (pedAlive(g, e) && ::length(pedPos(g, e) - playerPos(g)) < 120.f) footGone = false;
                if (d > 230.f && footGone) timer += dt;
                else timer = 0.f;
                if (stageTime > 6.f && stageTime < 6.1f) sayMe(g, "[calm]Nothing personal, fellas. Just business.");
                if (timer > 3.f || (chaseGone && footGone)) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    score(SC_NOIR, 0.4f, 1);
                    goTo(g, gPlaces.rookShop.curb, 5.f, "Take the Gatorback to ~y~Rook's salvage yard~s~.", true);
                    sayMe(g, "[happy:0.6]Clean getaway. Rook owes me a beer.");
                    next();
                }
                break;
            }
            case 4:
                if (abandoned(g, target, 150.f, "Gatorback")) return MS_FAILED;
                if (g.playerVehicle() != target && stageTime > 2.f && g.hudHelpTimer <= 0.f) g.help("Get back in the ~b~Gatorback~s~.", 2.f);
                if (arrived(g) && g.playerInVehicle(target)) {
                    clearGoal(g);
                    if (rook < 0) rook = spawnCast(g, CAST_ROOK, shopDoor, gPlaces.rookShop.yaw + kPi, FAC_FRIEND);
                    else placePed(g, rook, shopDoor, gPlaces.rookShop.yaw + kPi);
                    float cond = Saturate(g.vehicles[target].sim.health / 1000.f);
                    bonus = (long long)(cond * 2500.f);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotVehicle(g, target, 4.f));
                    shots.push_back(shotTwo(pedPos(g, rook), vehPos(g, target), 8.f));
                    g.mCutscene(shots);
                    say(g, CAST_ROOK, rook, cond > 0.8f ? "[happy:0.5]Not a scratch. Look at you, a professional." : "You call this one piece? That's coming out of your cut.");
                    sayMe(g, "[angry:0.5]They had guns, Rook. You didn't mention guns.");
                    say(g, CAST_ROOK, rook, "Everybody has guns. Listen. The bank has another job. A pickup truck at a boatyard on the river. Ortega's yard.");
                    sayMe(g, "Boatyard. How hard can a boatyard be.");
                    next();
                }
                break;
            case 5:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "Dex delivered the Gatorback. Rook's next repo: a pickup truck at the Ortega boatyard on the Rio Sol.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }
    long long bonus = 0;
    void finish(GameWorld& g, bool passed) override {
        if (passed && bonus > 0) money(g, bonus);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1: if (t.stageTime > 0.5f) t.teleportNear(hangout.xy(), 12.f); break;
            case 2: if (t.stageTime > 0.8f) t.enter(target); break;
            case 3:
                if (t.stageTime > 2.f) t.destroy(chaseCar);
                if (t.stageTime > 3.f) t.killEnemies();
                break;
            case 4: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 1-2: "Dry Dock" (Dex). The repo at the Ortega boatyard goes sideways: Mari catches Dex, then the Cuervos show up
// with firebombs and the two of them fight them off together.
class MissionDryDock : public StoryMission {
public:
    int mari = -1, truck = -1;
    vec3 yard, yardCurb;
    int wave = 0;
    const char* title() const override { return "Dry Dock"; }
    const char* brief() const override {
        return "Repossess the Ortega boatyard's pickup truck for the bank. Someone at the yard is not going to like it.";
    }
    long long reward() const override { return 3000; }

    void spawnWave(GameWorld& g, int w) {
        const Place& B = gPlaces.boatyard;
        int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_SUV, Vehicles::VC_MUSCLE}, (u32)(w * 3 + 1));
        for (int k = 0; k < 2; k++) {
            float along = (k == 0 ? -1.f : 1.f) * (90.f + 25.f * w);
            float yaw;
            vec3 sp = approachSpot(g, B, along, &yaw);
            int v = attackCar(g, model, sp, yaw, 3, k == 0 ? WPN_PISTOL : (w > 0 ? WPN_SMG : WPN_PISTOL), 0.22f + 0.05f * w, (u32)(w * 2 + k));
            if (v < 0) continue;
            ScriptDriver& d = driveRoad(g, v, yardCurb.xy() + B.streetDir * (k == 0 ? -12.f : 12.f), 13.f, true);
            d.stopAtEnd = true;
        }
        // one molotov thrower per wave
        int e = enemies.empty() ? -1 : enemies.back();
        if (e >= 0) arm(g, e, WPN_MOLOTOV, 3);
        blipEnemies(g);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        yard = P.boatyard.door;
        yardCurb = P.boatyard.curb;
        int model = pickModel(g, {Vehicles::VC_PICKUP, Vehicles::VC_VAN}, 2);
        truck = spawnCar(g, model, curbOffset(g, P.boatyard, 6.f), P.boatyard.curbYaw, lin(0.85f, 0.85f, 0.8f));
        if (truck >= 0) g.vehicles[truck].sim.engineOn = false;
        score(SC_NOIR, 0.3f, 2);
        if (checkpoint >= 1) {
            placePlayer(g, placeOffset(g, P.boatyard, 2.f, 1.f), P.boatyard.yaw);
            mari = spawnPartner(g, 0, P.boatyard.door, P.boatyard.yaw + kPi, WPN_PISTOL);
            beginFight(g);
            return;
        }
        phoneLine(g, CAST_ROOK, "The truck's at the Ortega boatyard on the river. Bank papers are in the glovebox. In and out, Dex.");
        sayMe(g, "In and out. Famous last words.");
        provideRide(g, P.dexTrailer, 8.f);
        goTo(g, yardCurb, 8.f, "Go to the ~y~Ortega boatyard~s~.", true);
    }

    void beginFight(GameWorld& g) {
        g.mObjective("Protect the boatyard: take out the ~r~Cuervos~s~.");
        if (mari >= 0) setFollow(g, mari, g.player);
        buddy = mari;
        wave = 0;
        spawnWave(g, 0);
        score(SC_CHASE, 0.8f, 2);
        g.mClearTarget();
        cp(g, 1);
        setStage(3);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (allyDown(g, mari, "Mari")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (arrived(g)) {
                    clearGoal(g);
                    g.mObjective("Get in the ~b~pickup truck~s~.");
                    g.mBlipVehicle(truck, UI::BLIP_VEHICLE);
                    next();
                }
                break;
            case 1:
                if (g.playerAt(vehPos(g, truck).xy(), 7.f) || g.playerInVehicle(truck)) {
                    // Mari catches him in the act
                    if (g.playerInVehicle(truck)) g.removePedFromVehicle(g.player, false);
                    mari = spawnPartner(g, 0, gPlaces.boatyard.door, gPlaces.boatyard.yaw + kPi, WPN_FISTS);
                    if (mari >= 0) {
                        g.peds[mari].invincible = true;
                        facePed(g, mari, playerPos(g));
                    }
                    facePed(g, g.player, pedPos(g, mari));
                    vec3 mp = pedPos(g, mari), dp = playerPos(g);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotOver(dp, mp, 5.f));
                    shots.push_back(shotOver(mp, dp, 6.f, -1.f));
                    shots.push_back(shotTwo(mp, dp, 7.f));
                    shots.push_back(shotOver(dp, mp, 6.f));
                    g.mCutscene(shots);
                    sayP(g, 0, mari, "[angry:0.7]Step away from my truck. Slowly.");
                    sayMe(g, "[calm]Easy. I'm with the bank. Coastline Savings. You missed four payments.");
                    sayP(g, 0, mari, "[angry:0.5]Coastline bought our loan last month. Then they tripled it. You know who owns Coastline?");
                    sayMe(g, "I just drive the trucks, lady.");
                    sayP(g, 0, mari, "[angry:0.6]Victor Sandoval. He wants this whole riverfront. And he doesn't care how he gets it.");
                    sayMe(g, "Look, I'm sorry about your yard, but -");
                    sayP(g, 0, mari, "[scared:0.5]Wait. Hear that? Engines. Those aren't bankers.");
                    next();
                }
                break;
            case 2:
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (mari >= 0) {
                        g.peds[mari].invincible = false;
                        arm(g, mari, WPN_PISTOL, 20);
                    }
                    Ped* pl = g.playerPed();
                    if (pl && !pl->hasWeapon[WPN_PISTOL]) g.giveWeapon(g.player, WPN_PISTOL, 60);
                    sayMe(g, "[shout:0.5]Cuervos. With firebombs. You've got friends.");
                    sayP(g, 0, mari, "[shout:0.7]They're here to burn us out. You can run, bank man, or you can help.");
                    beginFight(g);
                }
                break;
            case 3: {
                updateBuddy(g);
                for (int v : enemyCars) dismountNear(g, v, yard, 30.f);
                if (stageTime > 3.f && stageTime < 3.1f) sayMe(g, "Guess I'm helping.");
                if (aliveEnemies(g) <= 1 && wave == 0) {
                    wave = 1;
                    spawnWave(g, 1);
                    sayP(g, 0, mari, "[shout]More coming from the bridge!");
                }
                if (aliveEnemies(g) == 0 && wave >= 1) {
                    g.mObjective("");
                    score(SC_NOIR, 0.3f, 2);
                    if (mari >= 0) {
                        setIdle(g, mari, 0);
                        g.peds[mari].invincible = true;
                    }
                    vec3 mp = pedPos(g, mari), dp = playerPos(g);
                    facePed(g, mari, dp);
                    facePed(g, g.player, mp);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(mp, dp, 6.f));
                    shots.push_back(shotOver(dp, mp, 6.f));
                    shots.push_back(shotOver(mp, dp, 7.f, -1.f));
                    g.mCutscene(shots);
                    sayP(g, 0, mari, "[calm]You didn't have to do that.");
                    sayMe(g, "[sad:0.4]Yeah, I did. Dex Calloway. I used to pull people out of the water for a living.");
                    sayP(g, 0, mari, "[happy:0.4]Marisol Ortega. Mari. So. You still taking my truck?");
                    sayMe(g, "Truck got stolen. Tell the bank. If the Cuervos come back, call me.");
                    next();
                }
                break;
            }
            case 4:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "Mari and Dex fought off the Cuervos at the Ortega boatyard. Sandoval's bank is squeezing Calle Luna, "
                                       "and the Cuervos do his dirty work.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 0: testGoal(g, t, dt); break;
            case 1: if (t.stageTime > 0.5f) t.teleportNear(vehPos(g, truck).xy(), 3.f); break;
            case 3: if (t.stageTime > 2.5f) t.killEnemies(); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 1-3: "Pressure Cooker" (Mari). A developer's fixer leans on Mama Lucha; Mari tails him to the Solaris Pier site and
// overhears him with Captain Holt.
class MissionPressure : public StoryMission {
public:
    int lucha = -1, fixer = -1, fixerCar = -1, holt = -1;
    std::vector<int> guards;
    StealthGroup watch;
    vec3 meet, siteCurb;
    int talkLine = 0;
    float outside = 0.f;
    const char* title() const override { return "Pressure Cooker"; }
    const char* brief() const override {
        return "A man in a suit came to buy Mama Lucha's diner and didn't take no for an answer. Follow his car without being noticed "
               "and find out who he works for.";
    }
    long long reward() const override { return 2500; }

    void spawnMeeting(GameWorld& g) {
        const Place& S = gPlaces.solarisPier;
        meet = placeOffset(g, S, 6.f, 10.f);
        siteCurb = S.curb;
        holt = spawnCast(g, CAST_HOLT, meet + vec3(1.2f, 0, 0), 0.f, FAC_CIVILIAN);
        if (holt >= 0) {
            g.peds[holt].invincible = true;
            setIdle(g, holt, 0);
        }
        int model = pickModel(g, {Vehicles::VC_SEDAN}, 3);
        int hc = spawnCar(g, model, curbOffset(g, S, -12.f), S.curbYaw, lin(0.1f, 0.1f, 0.12f));
        (void)hc;
        for (int i = 0; i < 2; i++) {
            vec3 gp = placeOffset(g, S, i == 0 ? -10.f : 18.f, i == 0 ? 6.f : 12.f);
            int gd = spawnCast(g, CAST_GUARD_A + i, gp, S.yaw + (i ? kPi : 0.f), FAC_ENEMY);
            if (gd < 0) continue;
            arm(g, gd, WPN_PISTOL);
            g.peds[gd].brain.accuracy = 0.35f;
            setIdle(g, gd, 0);
            guards.push_back(gd);
            enemies.push_back(gd);
            std::vector<vec3> patrol;
            if (i == 1) {
                patrol.push_back(gp);
                patrol.push_back(placeOffset(g, S, 26.f, 4.f));
                patrol.push_back(placeOffset(g, S, 10.f, 18.f));
            }
            watch.add(g, gd, patrol, 15.f);
        }
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        score(SC_STEALTH, 0.35f, 0);
        if (checkpoint >= 1) {
            spawnMeeting(g);
            fixer = spawnCast(g, CAST_BANKER, meet - vec3(1.2f, 0, 0), 0.f, FAC_CIVILIAN);
            if (fixer >= 0) g.peds[fixer].invincible = true;
            placePlayer(g, placeOffset(g, gPlaces.solarisPier, -70.f, 0.f), gPlaces.solarisPier.yaw);
            beginEavesdrop(g);
            return;
        }
        lucha = spawnCast(g, CAST_LUCHA, P.diner.door, P.diner.yaw + kPi, FAC_FRIEND);
        fixer = spawnCast(g, CAST_BANKER, placeOffset(g, P.diner, 1.5f, 1.5f), P.diner.yaw, FAC_CIVILIAN);
        if (lucha >= 0) g.peds[lucha].invincible = true;
        if (fixer >= 0) g.peds[fixer].invincible = true;
        facePed(g, lucha, pedPos(g, fixer));
        facePed(g, fixer, pedPos(g, lucha));
        int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_SUV}, 6);
        fixerCar = spawnCar(g, model, curbOffset(g, P.diner, 10.f), P.diner.curbYaw, lin(0.05f, 0.05f, 0.06f));
        // Mari's own coupe, parked down the street (the tail needs wheels)
        playerCar = spawnCar(g, pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SEDAN}, 3), curbOffset(g, P.diner, -18.f), P.diner.curbYaw,
                             lin(0.55f, 0.06f, 0.2f));
        vec3 lp = pedPos(g, lucha), fp = pedPos(g, fixer);
        placePlayer(g, placeOffset(g, P.diner, -8.f, 0.f), yawTo(placeOffset(g, P.diner, -8.f, 0.f).xy(), lp.xy()));
        std::vector<CutsceneShot> shots;
        establish(g, shots, lp, P.diner.yaw, 28.f, 10.f, 3.5f);
        shots.push_back(shotTwo(lp, fp, 7.f));
        shots.push_back(shotOver(fp, lp, 6.f));
        shots.push_back(shotOver(lp, fp, 6.f, -1.f));
        shots.push_back(shotTwo(lp, fp, 5.f, 6.f, 45.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_BANKER, fixer, "[calm]Mrs. Paredes, it's a generous offer. Twice what this place is worth.");
        say(g, CAST_LUCHA, lucha, "[angry:0.6]This place is worth forty years of my life. It is not for sale.");
        say(g, CAST_BANKER, fixer, "[calm]Everything is for sale. The only question is the price, and how long you wait to accept it.");
        say(g, CAST_LUCHA, lucha, "[angry]Out. Before I get my frying pan.");
        say(g, CAST_BANKER, fixer, "[calm]Think it over. The neighborhood is changing. Accidents happen.");
    }

    void beginEavesdrop(GameWorld& g) {
        g.mClearBlips();
        goTo(g, meet, 13.f, "Get close enough to ~y~listen in~s~. Stay out of sight of the ~r~guards~s~.", false, true, vec3(0.3f, 0.8f, 1.f));
        g.mClearMarkers();
        g.mMarker(dvec3(meet), 13.f, vec3(0.3f, 0.8f, 1.f));
        cp(g, 1);
        setStage(3);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    // the fixer gets into his sedan; he pulls out once Mari has a car (or after a short while)
                    if (fixer >= 0 && fixerCar >= 0) g.warpPedIntoVehicle(fixer, fixerCar, 0);
                    g.mBlipVehicle(fixerCar, UI::BLIP_VEHICLE);
                    if (g.playerVehicle() < 0 && vehicleAlive(g, playerCar)) {
                        g.mBlipVehicle(playerCar, UI::BLIP_GARAGE);
                        g.mObjective("Get in ~b~your car~s~ before the sedan pulls out.");
                    }
                    if (lucha >= 0) setGoto(g, lucha, gPlaces.diner.door + vec3(gPlaces.diner.outward * 3.f, 0.f), 1.2f);
                    next();
                }
                break;
            case 1:
                if (g.playerVehicle() >= 0 || stageTime > 14.f) {
                    unblipVehicle(playerCar);
                    Place S = gPlaces.solarisPier;
                    RoutePath via;
                    buildRoadPath(g, vehPos(g, fixerCar).xy(), gPlaces.stashHouse.curb.xy(), via);
                    RoutePath rest;
                    buildRoadPath(g, gPlaces.stashHouse.curb.xy(), S.curb.xy(), rest);
                    for (size_t i = 1; i < rest.pts.size(); i++) {
                        via.pts.push_back(rest.pts[i]);
                        via.limit.push_back(rest.limit[i]);
                    }
                    via.finish();
                    ScriptDriver& d = addDriver(g, fixerCar, via, 12.f);
                    d.aggressive = false;
                    d.obeyLimits = true;
                    g.mObjective("Follow the ~b~black sedan~s~. Don't get too close.");
                    sayMe(g, "[calm]Let's see who you really work for.");
                    next();
                }
                break;
            case 2: {
                int r = tail.update(g, vehPos(g, fixerCar), 14.f, 140.f, dt, 12.f, 6.f);
                if (r == 1) return fail("You lost the sedan.");
                if (r == 2) return fail("You were spotted.");
                ScriptDriver* d = driverFor(fixerCar);
                if (stageTime > 12.f && stageTime < 12.1f) sayMe(g, "Nice car for a real estate guy.");
                if (!d || d->done) {
                    spawnMeeting(g);
                    if (fixer >= 0) {
                        g.removePedFromVehicle(fixer, true);
                        setGoto(g, fixer, meet - vec3(1.2f, 0, 0), 1.3f);
                    }
                    g.mClearBlips();
                    beginEavesdrop(g);
                }
                break;
            }
            case 3: {
                // eavesdrop: conversation plays while the player is in range and unseen
                if (watch.update(g, dt)) {
                    say(g, CAST_GUARD_A, watch.spotter, "[shout]Hey! Who's there?");
                    showMeter(g, "DETECTION", 0.f);
                    return fail("You were spotted.");
                }
                showMeter(g, "DETECTION", watch.meter);
                if (fixer >= 0 && ::length(pedPos(g, fixer) - meet) < 2.5f) setIdle(g, fixer, 7);
                bool inRange = g.playerAt(meet.xy(), 14.f);
                if (inRange) outside = 0.f;
                else if (talkLine > 0) outside += dt;
                if (outside > 6.f) return fail("You missed the conversation.");
                if (inRange && !g.mTalking()) {
                    if (talkLine == 0) {
                        facePed(g, holt, pedPos(g, fixer));
                        facePed(g, fixer, pedPos(g, holt));
                        say(g, CAST_BANKER, fixer, "The old woman won't sign. The Ortega girl is making noise.");
                        say(g, CAST_HOLT, holt, "[calm]Then make her quiet. My officers won't answer calls from Calle Luna this month.");
                    } else if (talkLine == 1) {
                        say(g, CAST_BANKER, fixer, "Mister Sandoval wants the riverfront empty by summer. The Solaris Pier investors are nervous.");
                        say(g, CAST_HOLT, holt, "[calm]Tell Victor his investors can relax. The Cuervos will handle the rest, and my reports will call it gang trouble.");
                    } else if (talkLine == 2) {
                        say(g, CAST_BANKER, fixer, "And your fee?");
                        say(g, CAST_HOLT, holt, "[calm]Doubled. Neighborhoods don't burn themselves down.");
                    } else {
                        showMeter(g, "DETECTION", 0.f);
                        sayMe(g, "[whisper]Holt. The police captain. She's in Sandoval's pocket.");
                        g.mClearMarkers();
                        g.mClearTarget();
                        g.mObjective("Leave the construction site.");
                        next();
                        break;
                    }
                    talkLine++;
                }
                break;
            }
            case 4:
                if (watch.update(g, dt)) return fail("You were spotted.");
                if (!g.playerAt(meet.xy(), 70.f)) {
                    g.storyBriefText = "Captain Reyna Holt of the Porto Sol PD is protecting Victor Sandoval's land grab, and the Cuervos do "
                                       "the dirty work. Mari needs to tell Lucha - and keep Tomas out of sight.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && g.playerVehicle() < 0 && vehicleAlive(g, playerCar)) t.enter(playerCar);
                break;
            case 2: {
                // keep a safe distance behind the sedan (the sedan is fast-forwarded along its route)
                if (t.stageTime > 3.f) fastForwardDriver(g, fixerCar, 30.f, g.dtLast);
                if (fixerCar >= 0 && t.stageTime > 0.5f) {
                    vec3 fp = vehPos(g, fixerCar);
                    vec3 f = g.vehicles[fixerCar].sim.forward();
                    vec3 want = fp - f * 45.f;
                    if (::length(playerPos(g) - want) > 20.f) t.teleport(vec3(want.x, want.y, groundAt(g, want.x, want.y)), yawTo(want.xy(), fp.xy()));
                }
                break;
            }
            case 3:
                if (t.stageTime > 0.5f && !g.playerAt(meet.xy(), 12.f)) {
                    // approach from behind the meeting spot (away from the guards)
                    vec3 behind = meet - vec3(gPlaces.solarisPier.outward * -9.f, 0.f);
                    t.teleport(vec3(behind.x, behind.y, groundAt(g, behind.x, behind.y)), yawTo(behind.xy(), meet.xy()));
                    Ped* pl = g.playerPed();
                    if (pl) pl->animIn.crouch = true;
                }
                break;
            case 4: if (t.stageTime > 1.f) t.teleportNear(meet.xy() + gPlaces.solarisPier.streetDir * 120.f, 1.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 1-4: "Collateral" (Mari, with Dex). The Cuervos grabbed Tomas; storm their stash house, free him, get him out.
class MissionCollateral : public StoryMission {
public:
    int dex = -1, tomas = -1, lucha = -1;
    bool inDiner = false;   // the opening was staged inside Mama Lucha's
    vec3 house, houseCurb;
    int phase = 0;
    const char* title() const override { return "Collateral"; }
    const char* brief() const override {
        return "The Cuervos took Tomas as collateral for money he never stole. Dex knows where they keep people: a stash house in south "
               "Calle Luna. Get him out alive.";
    }
    long long reward() const override { return 4000; }

    void spawnDefenders(GameWorld& g) {
        const Place& H = gPlaces.stashHouse;
        int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_SUV, Vehicles::VC_VAN}, 7);
        // parked cars as cover
        for (int i = 0; i < 3; i++) {
            int v = spawnCar(g, model, curbOffset(g, H, -14.f + i * 12.f), H.curbYaw);
            if (v >= 0) g.vehicles[v].sim.engineOn = false;
        }
        const float spots[8][2] = {{-6, 2}, {-2, 5}, {3, 3}, {7, 6}, {10, 1}, {0, 9}, {-9, 7}, {5, 10}};
        for (int i = 0; i < 8; i++) {
            vec3 p = placeOffset(g, H, spots[i][0], spots[i][1]);
            WeaponType w = i % 3 == 0 ? WPN_SMG : (i == 5 ? WPN_SHOTGUN : WPN_PISTOL);
            int e = gunman(g, CAST_THUG_A + (i % 4), p, H.yaw + kPi, w, 0.22f);
            if (e >= 0) setIdle(g, e, i & 1 ? 7 : 0);
        }
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        house = P.stashHouse.door;
        houseCurb = P.stashHouse.curb;
        score(SC_NOIR, 0.35f, 3);
        tomas = spawnCast(g, CAST_TOMAS, placeOffset(g, P.stashHouse, 2.f, 12.f), P.stashHouse.yaw, FAC_FRIEND);
        if (tomas >= 0) {
            g.peds[tomas].invincible = true;
            setIdle(g, tomas, 5);   // hands up / tied
        }
        spawnDefenders(g);
        if (checkpoint >= 1) {
            int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 2);
            playerCar = placePlayer(g, curbOffset(g, P.stashHouse, -110.f), P.stashHouse.curbYaw, model);
            dex = spawnPartner(g, 1, curbOffset(g, P.stashHouse, -110.f), 0.f, WPN_SMG);
            if (dex >= 0 && playerCar >= 0) g.warpPedIntoVehicle(dex, playerCar, 1);
            buddy = dex;
            beginAssault(g);
            return;
        }
        // inside the diner when the world has it: Lucha behind the counter, Mari at the register, Dex by the door
        InteriorStage in = interiorStage("Mama Lucha's");
        vec3 counterL, waiterL;
        inDiner = in.ok() && in.marker(World::IM_COUNTER, counterL) && in.scenario(World::SR_WAITER, 0, waiterL);
        if (inDiner) {
            lucha = spawnCast(g, CAST_LUCHA, in.at(vec3(counterL.x, waiterL.y, 0.f)), in.yaw(kPi), FAC_FRIEND);
            dex = spawnPartner(g, 1, in.at(in.entry() + vec3(0.6f, 0.4f, 0.f)), in.yaw(0.f), WPN_SMG);
            placePlayer(g, in.at(counterL), in.yaw(0.f));
        } else {
            lucha = spawnCast(g, CAST_LUCHA, P.diner.door, P.diner.yaw + kPi, FAC_FRIEND);
            dex = spawnPartner(g, 1, placeOffset(g, P.diner, 3.f, 1.f), P.diner.yaw + kPi * 0.5f, WPN_SMG);
            placePlayer(g, placeOffset(g, P.diner, -2.f, 0.5f), P.diner.yaw);
        }
        if (lucha >= 0) g.peds[lucha].invincible = true;
        // Dex came in his pickup; it's parked at the curb
        playerCar = spawnCar(g, pickModel(g, {Vehicles::VC_PICKUP, Vehicles::VC_SUV}, 2), curbOffset(g, P.diner, 12.f), P.diner.curbYaw,
                             lin(0.18f, 0.2f, 0.22f));
        vec3 lp = pedPos(g, lucha), dp = pedPos(g, dex), mp = playerPos(g);
        facePed(g, lucha, mp);
        facePed(g, dex, mp);
        std::vector<CutsceneShot> shots;
        if (inDiner) {
            float side = counterL.x > (in.d->x0 + in.d->x1) * 0.5f ? -1.f : 1.f;   // look along the counter from its long side
            shots.push_back(shotRoom(in, vec3(counterL.x + side * 4.2f, Max(0.9f, counterL.y - 2.6f), 2.1f), lp, mp, 4.5f));
        } else {
            establish(g, shots, lp, P.diner.yaw, 30.f, 11.f, 3.5f);
        }
        shots.push_back(shotTwo(lp, mp, 6.f));
        shots.push_back(shotOver(mp, lp, 6.f));
        shots.push_back(shotTwo(dp, mp, 7.f, 4.f, 42.f, -1.f));
        shots.push_back(shotOver(mp, dp, 6.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_LUCHA, lucha, "[scared]They took him from the bus stop, Mari. In broad daylight. They want the money by tonight.");
        sayMe(g, "[sad:0.6]There is no money, Lucha. He never had it.");
        sayP(g, 1, dex, "The Cuervos keep a stash house on the south side. Anybody they grab ends up there first.");
        sayMe(g, "You're coming with me?");
        sayP(g, 1, dex, "I owe your truck an apology. Let's go get your brother.");
        buddy = dex;
    }

    void beginAssault(GameWorld& g) {
        goTo(g, house, 10.f, "Take out the ~r~Cuervos~s~ at the stash house.", false, false);
        g.mClearTarget();
        blipEnemies(g);
        for (int e : enemies)
            if (pedAlive(g, e)) setCombat(g, e, g.player, 0.22f);
        score(SC_CHASE, 0.85f, 3);
        cp(g, 1);
        setStage(2);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, dex, "Dex")) return MS_FAILED;
        if (allyDown(g, tomas, "Tomas")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, dex, g.player);
                    if (lucha >= 0) {
                        if (inDiner) setIdle(g, lucha, 0);   // she stays behind her counter
                        else setGoto(g, lucha, gPlaces.diner.door + vec3(gPlaces.diner.outward * 3.f, 0.f), 1.2f);
                    }
                    goTo(g, houseCurb, 6.f, "Go to the ~y~stash house~s~ in south Calle Luna.", false, false);
                    if (g.playerVehicle() < 0 && vehicleAlive(g, playerCar)) {
                        g.mBlipVehicle(playerCar, UI::BLIP_GARAGE);
                        g.help("Dex's pickup is parked at the curb.", 4.f);
                    }
                    next();
                }
                break;
            case 1:
                updateBuddy(g);
                if (g.playerVehicle() >= 0) unblipVehicle(playerCar);
                if (stageTime > 5.f && stageTime < 5.1f && g.playerVehicle() >= 0) {
                    sayP(g, 1, dex, "Your brother always this much trouble?");
                    sayMe(g, "[sad:0.3]Since he was six. He's a good kid. He just trusts the wrong people.");
                    sayP(g, 1, dex, "Runs in the neighborhood, I guess.");
                }
                if (g.playerAt(houseCurb.xy(), 90.f)) {
                    sayP(g, 1, dex, "[calm]Four, five... more inside. Use the cars for cover.");
                    beginAssault(g);
                }
                break;
            case 2:
                updateBuddy(g);
                if (aliveEnemies(g) == 0) {
                    g.mClearBlips();
                    vec3 tp = pedPos(g, tomas);
                    goTo(g, tp, 2.5f, "Free ~b~Tomas~s~.");
                    sayP(g, 1, dex, "[shout]Clear! Get your brother.");
                    next();
                }
                break;
            case 3:
                updateBuddy(g);
                if (arrived(g)) {
                    clearGoal(g);
                    setIdle(g, tomas, 0);
                    facePed(g, tomas, playerPos(g));
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(pedPos(g, tomas), playerPos(g), 6.f));
                    g.mCutscene(shots);
                    say(g, CAST_TOMAS, tomas, "[happy]Mari! I knew you'd come. Who's the big guy?");
                    sayMe(g, "A friend. Can you run?");
                    say(g, CAST_TOMAS, tomas, "[scared:0.5]I can run. Let's go before the rest of them get back.");
                    next();
                }
                break;
            case 4:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.peds[tomas].invincible = false;
                    g.peds[tomas].maxHealth = g.peds[tomas].health = 260.f;
                    setFollow(g, tomas, g.player);
                    // reinforcements
                    int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 9);
                    const Place& H = gPlaces.stashHouse;
                    for (int k = 0; k < 2; k++) {
                        float yaw;
                        vec3 sp = approachSpot(g, H, k ? 140.f : -150.f, &yaw);
                        int v = attackCar(g, model, sp, yaw, 3, WPN_SMG, 0.2f, (u32)(5 + k));
                        if (v < 0) continue;
                        for (int s = 0; s < 3; s++)
                            if (g.vehicles[v].seats[s] >= 0) setCombat(g, g.vehicles[v].seats[s], g.player, 0.2f);
                    }
                    blipEnemies(g);
                    sayP(g, 1, dex, "[shout]Company! Get Tomas in a car and get us out of here!");
                    g.mObjective("Get ~b~Tomas~s~ out of south Calle Luna.");
                    cp(g, 2);
                    next();
                }
                break;
            case 5: {
                updateBuddy(g);
                buddyUpdate(g, tomas, nullptr);
                for (int v : enemyCars) dismountNear(g, v, playerPos(g), 25.f);
                bool together = g.playerVehicle() >= 0 && g.peds[tomas].vehicle == g.playerVehicle();
                if (!g.playerAt(houseCurb.xy(), 260.f) && together) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    score(SC_NOIR, 0.4f, 3);
                    goTo(g, gPlaces.boatyard.curb, 6.f, "Take Tomas to the ~y~boatyard~s~.", true);
                    say(g, CAST_TOMAS, tomas, "[sad:0.5]They said the money was never the point. They said Sandoval wants the whole block scared.");
                    sayP(g, 1, dex, "[angry:0.5]Then we make them scared instead.");
                    next();
                }
                break;
            }
            case 6: {
                updateBuddy(g);
                bool together = g.peds[tomas].vehicle >= 0 && g.peds[tomas].vehicle == g.playerVehicle();
                if (arrived(g) && together) {
                    clearGoal(g);
                    g.storyBriefText = "Tomas is safe at the boatyard. The Cuervos grabbed him to scare the neighborhood for Sandoval. "
                                       "El Cuervo won't let this go.";
                    return MS_PASSED;
                }
                break;
            }
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) {
                    int v = spawnCar(g, pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f);
                    t.enter(v);
                    if (dex >= 0 && v >= 0) g.warpPedIntoVehicle(dex, v, 1);
                }
                testGoal(g, t, dt);
                break;
            case 2: if (t.stageTime > 2.f) t.killEnemies(); break;
            case 3: if (t.stageTime > 0.5f) t.teleportNear(goal.xy(), 1.f); break;
            case 5:
                if (t.stageTime > 1.f && g.playerVehicle() < 0) {
                    int v = spawnCar(g, pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f);
                    t.enter(v);
                }
                if (t.stageTime > 1.5f && g.playerVehicle() >= 0) {
                    if (g.peds[tomas].vehicle != g.playerVehicle()) g.warpPedIntoVehicle(tomas, g.playerVehicle(), 1);
                    t.driveToward(gPlaces.boatyard.curb.xy(), 45.f, dt);
                }
                if (t.stageTime > 3.f) t.killEnemies();
                break;
            case 6: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Race course used by story and side races: checkpoints on the road network, AI racers following the same path.
struct RaceCourse {
    RoutePath path;
    std::vector<vec3> checkpoints;
    std::vector<float> cpAlong;
    int next = 0;
    std::vector<int> racers;           // vehicles
    std::vector<float> racerAlong;
    bool water = false, air = false;
    float cpRadius = 9.f;

    void build(GameWorld& g, const std::vector<vec2>& via, float spacing = 170.f, bool isWater = false, float waterZ = 0.f) {
        water = isWater;
        path = RoutePath();
        if (isWater) {
            std::vector<vec3> wps;
            for (vec2 p : via) wps.push_back(vec3(p, waterZ));
            buildWaypointPath(wps, path, 30.f, true, false);
        } else {
            for (size_t i = 0; i + 1 < via.size(); i++) {
                RoutePath seg;
                buildRoadPath(g, via[i], via[i + 1], seg, 0.35f);
                size_t first = path.pts.empty() ? 0 : 1;
                for (size_t k = first; k < seg.pts.size(); k++) {
                    path.pts.push_back(seg.pts[k]);
                    path.limit.push_back(seg.limit[k]);
                }
            }
            path.finish();
        }
        checkpoints.clear();
        cpAlong.clear();
        float total = path.length();
        int n = Max(3, (int)(total / spacing));
        for (int i = 1; i <= n; i++) {
            float s = total * i / n;
            vec3 p = path.at(s);
            if (!water) p.z = groundAt(g, p.x, p.y, p.z + 3.f);
            checkpoints.push_back(p);
            cpAlong.push_back(s);
        }
        next = 0;
    }

    // Returns true when the player's vehicle passed the next checkpoint.
    bool updatePlayer(GameWorld& g) {
        if (next >= (int)checkpoints.size()) return false;
        vec3 pp = playerPos(g);
        float r = cpRadius + (air ? 6.f : 0.f);
        vec3 d = pp - checkpoints[next];
        if (!air) d.z *= 0.3f;
        if (::length(d) < r) {
            next++;
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_CHECKPOINT, 0.8f);
#endif
            return true;
        }
        return false;
    }

    void showMarkers(GameWorld& g) {
        g.mClearMarkers();
        if (next < (int)checkpoints.size()) {
            bool last = next + 1 == (int)checkpoints.size();
            g.mMarker(dvec3(checkpoints[next]), cpRadius * 0.6f, last ? vec3(1.f, 0.9f, 0.3f) : vec3(0.3f, 0.75f, 1.f));
            g.mTarget(checkpoints[next].xy(), last ? UI::BLIP_RACE : UI::BLIP_OBJECTIVE);
            if (!last) g.mMarker(dvec3(checkpoints[next + 1]), cpRadius * 0.35f, vec3(0.2f, 0.45f, 0.7f));
        }
    }

    float playerProgress(GameWorld& g, float hint) { return path.project(playerPos(g).xy(), hint, 300.f); }

    // 1-based race position of the player
    int position(GameWorld& g) {
        float pa = next > 0 ? cpAlong[next - 1] : 0.f;
        pa = Max(pa, path.project(playerPos(g).xy(), pa, 250.f));
        pa = Min(pa, next < (int)cpAlong.size() ? cpAlong[next] : path.length());
        int pos = 1;
        for (int v : racers) {
            ScriptDriver* d = driverFor(v);
            float a = d ? d->along : 0.f;
            if (d && d->done) a = path.length() + 1.f;
            if (a > pa) pos++;
        }
        return pos;
    }

    bool anyRacerFinished() {
        for (int v : racers) {
            ScriptDriver* d = driverFor(v);
            if (d && d->done) return true;
        }
        return false;
    }
};

// ==================================================================================================================
// Act 1-5: "Pink Slips" (Dex). Chuy, the Cuervo whose car Dex took, wants a rematch on the street: winner takes the
// loser's car and the Cuervos' pot.
class MissionPinkSlips : public StoryMission {
public:
    RaceCourse race;
    int chuy = -1;
    float countdown = 0.f;
    int lastCount = 4;
    const char* title() const override { return "Pink Slips"; }
    const char* brief() const override {
        return "Chuy, the Cuervo lieutenant whose Gatorback Dex repossessed, challenged Dex to a street race through Calle Luna and "
               "downtown. Winner takes the loser's car and five grand of Cuervo money.";
    }
    long long reward() const override { return 5000; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        std::vector<vec2> via = {P.raceCalle.curb.xy(), vec2(2000, -700), vec2(2500, -300), vec2(3300, -500), vec2(3500, 500),
                                 vec2(2700, 700), vec2(1900, 600), vec2(1300, 200), vec2(1250, -450)};
        race.build(g, via, 180.f);
        if (race.path.pts.size() < 2) return;
        vec2 t0;
        vec3 p0 = race.path.at(4.f, nullptr, &t0);
        float yaw = atan2f(-t0.x, t0.y);
        vec2 right(t0.y, -t0.x);
        int model = pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_MUSCLE, Vehicles::VC_COUPE}, 1);
        playerCar = placePlayer(g, p0 + vec3(right * -2.2f, 0.f), yaw, model, lin(0.1f, 0.3f, 0.75f));
        int rivals[3] = {pickModel(g, {Vehicles::VC_MUSCLE}, 0), pickModel(g, {Vehicles::VC_SPORTS}, 2), pickModel(g, {Vehicles::VC_COUPE}, 1)};
        for (int i = 0; i < 3; i++) {
            int m = rivals[i] >= 0 ? rivals[i] : model;
            vec3 sp = race.path.at(4.f - 9.f * ((i + 1) / 2)) + vec3(right * (i % 2 == 0 ? 2.2f : -2.2f), 0.f);
            if (i == 1) sp = p0 + vec3(right * 2.2f, 0.f);
            int v = spawnCar(g, m, sp, yaw, i == 0 ? lin(0.45f, 0.04f, 0.05f) : vec3(-1.f));
            if (v < 0) continue;
            int drv = g.mPed(castChar(g, CAST_THUG_A + i), dvec3(sp), yaw, FAC_CIVILIAN);
            if (drv >= 0) g.warpPedIntoVehicle(drv, v, 0);
            if (i == 0) chuy = drv;
            race.racers.push_back(v);
            ScriptDriver& d = addDriver(g, v, race.path, 34.f + i * 2.f);
            d.aggressive = true;
            d.racer = true;
            d.rubberPed = g.player;
            d.speedScale = 0.f;   // held at the line until GO
        }
        score(SC_CHASE, 0.5f, 4);
        std::vector<CutsceneShot> shots;
        if (playerCar >= 0) {
            establish(g, shots, p0, yaw + kPi, 30.f, 8.f, 3.5f);
            shots.push_back(shotVehicle(g, playerCar, 4.f, -1.f));
        }
        if (checkpoint == 0) {
            g.mCutscene(shots);
            say(g, CAST_THUG_A, chuy, "[angry]Repo man! You took my Gatorback. Tonight I take your ride and your dignity.");
            sayMe(g, "[calm]You stopped paying for it, Chuy. That's how cars work.");
            say(g, CAST_THUG_A, chuy, "[angry:0.4]Loser signs over his car. Winner takes the pot. Five grand.");
            sayMe(g, "[happy:0.3]Try to keep up.");
        }
        g.mObjective("");
        setStage(1);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, playerCar, "car")) return MS_FAILED;
        switch (stage) {
            case 1:
                if (!g.mInCutscene() && !g.mTalking()) {
                    countdown = 3.5f;
                    lastCount = 4;
                    next();
                }
                if (playerCar >= 0 && g.playerVehicle() == playerCar) {
                    g.vehicles[playerCar].ctl = Vehicles::VehicleControls();
                    g.vehicles[playerCar].ctl.brake = 1.f;
                }
                break;
            case 2: {
                countdown -= dt;
                int c = (int)ceilf(countdown);
                if (c != lastCount && c >= 1 && c <= 3) {
                    lastCount = c;
                    g.bigMessage(StrFormat("%d", c), "", 0xffffffffu);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_COUNTDOWN, 0.9f);
#endif
                }
                // hold the player at the line
                if (g.playerVehicle() >= 0 && countdown > 0.f) {
                    Vehicle& v = g.vehicles[g.playerVehicle()];
                    if (v.sim.speed() > 0.5f) v.sim.body.vel *= 0.5f;
                }
                if (countdown <= 0.f) {
                    g.bigMessage("GO!", "", 0xff33ff66u);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_GO, 0.9f);
#endif
                    for (int v : race.racers)
                        if (ScriptDriver* d = driverFor(v)) d->speedScale = 1.f;
                    score(SC_CHASE, 0.95f, 4);
                    race.showMarkers(g);
                    next();
                }
                break;
            }
            case 3: {
                if (g.playerVehicle() < 0) {
                    timer += dt;
                    if (timer > 15.f) return fail("You left the race.");
                    if (g.hudHelpTimer <= 0.f) g.help("Get back in a car and finish the race.", 2.f);
                } else timer = 0.f;
                if (race.updatePlayer(g)) race.showMarkers(g);
                int pos = race.position(g);
                g.missionCounterLabel = "POSITION";
                g.missionCounter = pos;
                g.missionCounterMax = (int)race.racers.size() + 1;
                g.mObjective(StrFormat("Win the race.  Checkpoint ~y~%d/%d~s~", race.next, (int)race.checkpoints.size()));
                if (stageTime > 20.f && stageTime < 20.1f) say(g, CAST_THUG_A, -1, pos == 1 ? "[angry]Who taught this guy to drive?" : "Eat my dust, repo man!");
                if (race.next >= (int)race.checkpoints.size()) {
                    g.missionCounterLabel.clear();
                    if (pos == 1) {
                        g.mClearMarkers();
                        g.mClearTarget();
                        g.mObjective("");
                        score(SC_NOIR, 0.4f, 4);
                        sayMe(g, "[happy:0.6]Pink slip, Chuy. Leave the keys with Rook.");
                        say(g, CAST_THUG_A, -1, "[angry]This isn't over. El Cuervo is going to hear about this.");
                        next();
                    } else {
                        return fail("You lost the race.");
                    }
                } else if (race.anyRacerFinished()) {
                    g.missionCounterLabel.clear();
                    return fail("Chuy won the race.");
                }
                break;
            }
            case 4:
                if (!g.mTalking() && stageTime > 1.f) {
                    g.storyBriefText = "Dex beat Chuy and took the Cuervos' pot. The money goes toward the Ortega boatyard's debt - and El Cuervo "
                                       "himself is now paying attention.";
                    for (int v : race.racers) releaseDriver(g, v);
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (stage == 3 && race.next < (int)race.checkpoints.size() && t.stageTime > 0.5f) {
            // hop from checkpoint to checkpoint slightly ahead of the racers
            if (fmodf(t.stageTime, 0.6f) < g.dtLast) {
                vec3 cpp = race.checkpoints[race.next];
                vec2 tan;
                race.path.at(race.cpAlong[race.next], nullptr, &tan);
                t.teleport(cpp - vec3(tan * 4.f, 0.f), atan2f(-tan.x, tan.y));
            }
        }
    }
};

// ==================================================================================================================
// Act 1-6: "Last Call" (Mari, with Dex). El Cuervo attacks Mama Lucha's diner at night; defend it through three waves,
// chase him down, and watch Captain Holt make him disappear.
class MissionLastCall : public StoryMission {
public:
    int dex = -1, lucha = -1, tomas = -1, cuervo = -1, cuervoCar = -1, holt = -1;
    int wave = 0;
    vec3 diner;
    float waveTimer = 0.f;
    const char* title() const override { return "Last Call"; }
    const char* brief() const override {
        return "El Cuervo is coming for Mama Lucha's diner. Defend it with Dex, protect Lucha, and don't let El Cuervo get away.";
    }
    long long reward() const override { return 6000; }

    void spawnWave(GameWorld& g, int w) {
        const Place& D = gPlaces.diner;
        int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_SUV, Vehicles::VC_MUSCLE}, (u32)(w * 5 + 2));
        int cars = w == 2 ? 1 : 2;
        for (int k = 0; k < cars; k++) {
            float along = (k == 0 ? -1.f : 1.f) * (100.f + 20.f * w) * (w == 1 ? -1.f : 1.f);
            float yaw;
            vec3 sp = approachSpot(g, D, along, &yaw);
            int v = attackCar(g, model, sp, yaw, 3, w >= 1 ? WPN_SMG : WPN_PISTOL, 0.2f + 0.04f * w, (u32)(w * 3 + k));
            if (v < 0) continue;
            ScriptDriver& d = driveRoad(g, v, D.curb.xy() + D.streetDir * (along < 0.f ? -14.f : 14.f), 15.f, true);
            d.stopAtEnd = true;
            // some of them go for Lucha
            if (k == 1 && lucha >= 0) {
                int p = g.vehicles[v].seats[1];
                if (p >= 0) g.peds[p].brain.target = lucha;
            }
        }
        if (w == 1 && !enemies.empty()) arm(g, enemies.back(), WPN_MOLOTOV, 3);
        if (w == 2) {
            // El Cuervo's SUV with his bodyguards
            int suv = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_PICKUP}, 1);
            float cyaw;
            vec3 sp = approachSpot(g, D, 150.f, &cyaw);
            cuervoCar = spawnCar(g, suv, sp, cyaw, lin(0.03f, 0.03f, 0.035f));
            if (cuervoCar >= 0) {
                cuervo = spawnCast(g, CAST_CUERVO, sp, 0.f, FAC_ENEMY);
                if (cuervo >= 0) {
                    g.warpPedIntoVehicle(cuervo, cuervoCar, 0);
                    g.peds[cuervo].invincible = true;
                    arm(g, cuervo, WPN_SMG);
                }
                for (int s = 1; s < 3; s++) {
                    int p = spawnCast(g, CAST_THUG_A + s, sp, 0.f, FAC_ENEMY);
                    if (p < 0) continue;
                    g.warpPedIntoVehicle(p, cuervoCar, s);
                    arm(g, p, WPN_RIFLE);
                    g.peds[p].brain.accuracy = 0.25f;
                    setCombat(g, p, g.player, 0.25f);
                    enemies.push_back(p);
                }
                ScriptDriver& d = driveRoad(g, cuervoCar, D.curb.xy() + D.streetDir * 30.f, 12.f, true);
                d.stopAtEnd = true;
                g.mBlipVehicle(cuervoCar, UI::BLIP_ENEMY);
            }
        }
        blipEnemies(g);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        diner = P.diner.pos;
        g.env->timeOfDay = Max(g.env->timeOfDay, 20.5f);
        g.mClearBlips();
        lucha = spawnCast(g, CAST_LUCHA, P.diner.door, P.diner.yaw + kPi, FAC_FRIEND);
        if (lucha >= 0) {
            g.peds[lucha].maxHealth = g.peds[lucha].health = 500.f;
            setIdle(g, lucha, 4);
        }
        dex = spawnPartner(g, 1, placeOffset(g, P.diner, 4.f, 0.5f), P.diner.yaw, WPN_RIFLE);
        buddy = dex;
        Ped* pl = g.playerPed();
        if (pl && !pl->hasWeapon[WPN_SMG]) g.giveWeapon(g.player, WPN_SMG, 150);
        score(SC_NOIR, 0.4f, 5);
        if (checkpoint >= 2) {
            // restart at the chase
            int model = pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SPORTS, Vehicles::VC_SEDAN}, 0);
            playerCar = placePlayer(g, curbOffset(g, P.diner, 20.f), P.diner.curbYaw, model);
            if (dex >= 0 && playerCar >= 0) g.warpPedIntoVehicle(dex, playerCar, 1);
            wave = 3;
            int suv = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_PICKUP}, 1);
            float cyaw;
            vec3 sp = curbOffset(g, P.diner, 90.f, &cyaw);
            cuervoCar = spawnCar(g, suv, sp, cyaw, lin(0.03f, 0.03f, 0.035f));
            cuervo = spawnCast(g, CAST_CUERVO, sp, 0.f, FAC_ENEMY);
            if (cuervo >= 0 && cuervoCar >= 0) {
                g.warpPedIntoVehicle(cuervo, cuervoCar, 0);
                g.peds[cuervo].invincible = true;
            }
            beginChase(g);
            return;
        }
        if (checkpoint >= 1) {
            placePlayer(g, placeOffset(g, P.diner, -3.f, 0.5f), P.diner.yaw);
            beginDefense(g);
            return;
        }
        tomas = spawnCast(g, CAST_TOMAS, placeOffset(g, P.diner, 1.f, 4.f), P.diner.yaw, FAC_FRIEND);
        if (tomas >= 0) g.peds[tomas].invincible = true;
        placePlayer(g, placeOffset(g, P.diner, -1.5f, 1.f), P.diner.yaw + kPi * 0.5f);
        vec3 lp = pedPos(g, lucha), mp = playerPos(g), dp = pedPos(g, dex);
        setIdle(g, lucha, 0);
        facePed(g, lucha, mp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, lp, P.diner.yaw, 32.f, 11.f, 4.f, 50.f);
        shots.push_back(shotTwo(lp, mp, 6.f));
        shots.push_back(shotTwo(dp, mp, 5.f, 4.f, 42.f, -1.f));
        vec3 street = curbOffset(g, P.diner, -80.f);
        shots.push_back(shotMove(mp + vec3(0, 0, 1.7f), street + vec3(0, 0, 1.f), mp + vec3(0, 0, 1.8f), street + vec3(0, 0, 1.2f), 4.f, 35.f));
        g.mCutscene(shots);
        say(g, CAST_LUCHA, lucha, "[happy:0.5]Eat, both of you. Nobody fights on an empty stomach in my diner.");
        sayMe(g, "[scared:0.4]Lucha, the Cuervos said they'd come back. Tonight.");
        sayP(g, 1, dex, "[calm]Then we'll be here. Rook lent me something with a bit more bite.");
        say(g, CAST_TOMAS, tomas, "[scared]Headlights. Three cars, no plates. Mari...");
        sayMe(g, "[shout]Everybody down! Lucha, stay behind the counter!");
    }

    void beginDefense(GameWorld& g) {
        if (tomas >= 0) setGoto(g, tomas, gPlaces.diner.door + vec3(gPlaces.diner.outward * 4.f, 0.f), 3.f);
        setIdle(g, lucha, 4);
        placePed(g, lucha, gPlaces.diner.door, gPlaces.diner.yaw);
        g.mObjective("Defend ~b~Mama Lucha's diner~s~.");
        wave = 0;
        spawnWave(g, 0);
        score(SC_CHASE, 0.85f, 5);
        cp(g, 1);
        setStage(2);
    }

    void beginChase(GameWorld& g) {
        // El Cuervo runs: flee route far to the south-west
        releaseDriver(g, cuervoCar);
        if (cuervoCar < 0) return;
        vec3 from = vehPos(g, cuervoCar);
        RoutePath p;
        buildRoadPath(g, from.xy(), gPlaces.raceGrove.curb.xy(), p, 1.f);
        ScriptDriver& d = addDriver(g, cuervoCar, p, 27.f);
        d.aggressive = true;
        d.rubberPed = g.player;
        d.rubberGap = 70.f;
        g.mClearBlips();
        g.mBlipVehicle(cuervoCar, UI::BLIP_ENEMY);
        g.mObjective("Stop ~r~El Cuervo~s~ before he gets away.");
        score(SC_CHASE, 1.f, 5);
        cp(g, 2);
        setStage(5);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, dex, "Dex")) return MS_FAILED;
        if (stage <= 4 && allyDown(g, lucha, "Mama Lucha")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) beginDefense(g);
                break;
            case 2: {
                updateBuddy(g, 45.f);
                for (int v : enemyCars) dismountNear(g, v, diner, 35.f);
                if (cuervoCar >= 0 && wave == 2) dismountNear(g, cuervoCar, diner, 45.f);
                int alive = aliveEnemies(g);
                if (alive <= 1 && wave < 2) {
                    waveTimer += dt;
                    if (waveTimer > 2.f) {
                        wave++;
                        waveTimer = 0.f;
                        spawnWave(g, wave);
                        if (wave == 1) sayP(g, 1, dex, "[shout]Second car, from the south! Watch the firebombs!");
                        else {
                            sayMe(g, "[angry:0.6]Black SUV. That's him. That's El Cuervo.");
                            say(g, CAST_CUERVO, -1, "[shout]Ortega! You and your repo man cost me money. Tonight your diner burns!");
                        }
                    }
                }
                if (wave == 2 && alive == 0) {
                    // his men are down: El Cuervo bolts
                    if (cuervo >= 0 && cuervoCar >= 0 && g.peds[cuervo].vehicle != cuervoCar) g.warpPedIntoVehicle(cuervo, cuervoCar, 0);
                    say(g, CAST_CUERVO, cuervo, "[shout]Forget this! Drive!");
                    sayP(g, 1, dex, "[shout]He's running! Grab a car, I'll ride shotgun!");
                    beginChase(g);
                }
                break;
            }
            case 5: {
                updateBuddy(g);
                if (cuervoCar < 0) return fail("El Cuervo got away.");
                float d = ::length(vehPos(g, cuervoCar) - playerPos(g));
                if (d > 380.f) timer += dt;
                else timer = 0.f;
                if (timer > 10.f) return fail("El Cuervo got away.");
                if (timer > 2.f && g.hudHelpTimer <= 0.f) g.help("El Cuervo is getting away!", 1.5f);
                if (stageTime > 8.f && stageTime < 8.1f) sayP(g, 1, dex, "[shout]Ram him! Put him into a wall!");
                if (stageTime > 25.f && stageTime < 25.1f) sayMe(g, "[angry:0.5]He drives like a coward.");
                ScriptDriver* dr = driverFor(cuervoCar);
                bool stopped = vehicleDisabled(g, cuervoCar) || (g.vehicles[cuervoCar].sim.health < 350.f) ||
                               (dr && dr->done) || (stageTime > 8.f && vehicleSpeed(g, cuervoCar) < 1.f && d < 25.f);
                if (stopped) {
                    releaseDriver(g, cuervoCar);
                    g.vehicles[cuervoCar].ctl = Vehicles::VehicleControls();
                    g.vehicles[cuervoCar].ctl.brake = 1.f;
                    g.mClearBlips();
                    g.mObjective("");
                    // cutscene: cornered, then Holt arrives
                    if (cuervo >= 0) {
                        g.removePedFromVehicle(cuervo, true);
                        setIdle(g, cuervo, 5);
                        g.peds[cuervo].faction = FAC_CIVILIAN;
                        g.peds[cuervo].invincible = true;
                    }
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    if (dex >= 0 && g.peds[dex].vehicle >= 0) g.removePedFromVehicle(dex, false);
                    vec3 cp3 = pedPos(g, cuervo), mp = playerPos(g);
                    if (dex >= 0) {
                        placePed(g, dex, mp + vec3(dirFromYaw(yawTo(cp3.xy(), mp.xy()) + 0.6f) * 2.5f, 0.f), yawTo(mp.xy(), cp3.xy()));
                        setIdle(g, dex, 0);
                    }
                    int pc = pickModel(g, {Vehicles::VC_POLICE, Vehicles::VC_SEDAN}, 0);
                    vec3 hp = cp3 + vec3(dirFromYaw(yawTo(mp.xy(), cp3.xy())) * 14.f, 0.f);
                    int hcar = spawnCar(g, pc, vec3(hp.x, hp.y, groundAt(g, hp.x, hp.y)), yawTo(hp.xy(), cp3.xy()));
                    if (hcar >= 0) g.vehicles[hcar].sirenOn = true;
                    holt = spawnCast(g, CAST_HOLT, hp + vec3(2.5f, 0, 0), yawTo(hp.xy(), mp.xy()), FAC_CIVILIAN);
                    if (holt >= 0) g.peds[holt].invincible = true;
                    facePed(g, g.player, cp3);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotOver(mp, cp3, 6.f));
                    shots.push_back(shotOver(cp3, mp, 5.f, -1.f));
                    shots.push_back(shotTwo(pedPos(g, holt), mp, 8.f));
                    shots.push_back(shotOver(mp, pedPos(g, holt), 7.f));
                    shots.push_back(shotOver(pedPos(g, holt), mp, 7.f, -1.f));
                    g.mCutscene(shots);
                    say(g, CAST_CUERVO, cuervo, "[angry:0.7]Go ahead, Ortega. Pull the trigger. See what happens to your little street.");
                    sayMe(g, "[angry]You don't get to burn us out. Not you. Not Sandoval.");
                    say(g, CAST_HOLT, holt, "[megaphone][shout]Porto Sol PD! Weapons down. Step away from the suspect.");
                    sayMe(g, "[angry:0.5]Captain Holt. Funny. Your officers never answer calls from Calle Luna.");
                    say(g, CAST_HOLT, holt, "[calm]I'll take it from here, Ms. Ortega. Go home. This neighborhood is changing. Change with it.");
                    sayP(g, 1, dex, "[calm]Mari. Let it go. Not tonight.");
                    next();
                }
                break;
            }
            case 6:
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (cuervo >= 0 && holt >= 0) {
                        setGoto(g, cuervo, pedPos(g, holt), 1.4f);
                        setGoto(g, holt, pedPos(g, holt) + vec3(4, 0, 0), 1.4f);
                    }
                    g.storyBriefText = "Holt took El Cuervo into custody - and everyone in Calle Luna knows he'll be back on the street by "
                                       "morning. Dex kept something from the fight: El Cuervo's phone. Kit Navarro at Pulse FM might crack it.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        switch (stage) {
            case 2:
                if (t.stageTime > 3.f && fmodf(t.stageTime, 2.f) < g.dtLast) t.killEnemies();
                break;
            case 5:
                if (t.stageTime > 0.5f && g.playerVehicle() < 0) {
                    int v = spawnCar(g, pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f);
                    t.enter(v);
                }
                if (t.stageTime > 1.f && cuervoCar >= 0) {
                    vec3 cp3 = vehPos(g, cuervoCar);
                    vec3 f = g.vehicles[cuervoCar].sim.forward();
                    if (::length(playerPos(g) - cp3) > 60.f) t.teleport(cp3 - f * 25.f, atan2f(-f.x, f.y));
                }
                if (t.stageTime > 6.f && cuervoCar >= 0) g.damageVehicle(cuervoCar, 700.f, g.player, vec3(0), vec3(0));
                break;
            default: break;
        }
    }
};

// New game: the opening. Porto Sol from over the bay with the title, then down the Calle Luna street to Mari, and the
// player's camera takes over from the last shot.
void openingShots(GameWorld& g) {
    const Places& P = gPlaces;
    Ped* pl = g.playerPed();
    if (!pl) return;
    std::vector<CutsceneShot> shots;
    vec3 city = P.solarisPlaza + vec3(0.f, 0.f, 70.f);
    vec2 in = normalize(city.xy() - P.bayCenter.xy() + vec2(0.01f, 0.f));
    vec3 c0 = P.bayCenter + vec3(-in * 250.f, 95.f), c1 = P.bayCenter + vec3(in * 120.f, 70.f);
    shots.push_back(shotMove(c0, city, c1, city - vec3(0.f, 0.f, 25.f), 10.f, 50.f));
    // down the street to Mari from behind her (she stands on the sidewalk facing along it), ending where the gameplay
    // camera will be, so the hand-off is a short ease instead of a swing around her
    vec3 mari = pl->pos.toVec3() + vec3(0.f, 0.f, 1.4f);
    vec2 sd = dirFromYaw(pl->yaw);
    shots.push_back(shotMove(mari + vec3(-sd * 150.f, 32.f), mari, mari + vec3(-sd * 55.f, 11.f), mari, 7.f, 46.f));
    establish(g, shots, pl->pos.toVec3(), pl->yaw, 12.f, 3.f, 4.5f, 45.f);
    g.mCutscene(shots, true);
    gMissions.holdForDialogue = false;
    gMissions.cardTitle = "NEON TIDE";
    gMissions.cardSub = "Porto Sol";
    gMissions.cardT = 0.f;
    gMissions.cardDelay = 1.8f;
    gMissions.cardCentered = true;
    g.fadeAlpha = 1.f;
    g.fadeIn(0.6f);
}

// The opening as a mission (hidden, repeatable): lets the test harness film it; passes when the shots are done.
class MissionOpening : public StoryMission {
public:
    const char* title() const override { return "Porto Sol"; }
    const char* brief() const override { return "The opening shots of a new game."; }
    long long reward() const override { return 0; }
    const char* passBanner() const override { return ""; }
    bool allowRetry() const override { return false; }
    void start(GameWorld& g) override { openingShots(g); }
    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (stageTime > 0.5f && !g.mInCutscene()) return MS_PASSED;
        return MS_RUNNING;
    }
};

}  // namespace mu
}  // namespace Game
