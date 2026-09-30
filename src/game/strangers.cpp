// Strangers: short side stories with recurring characters, unlocked one part at a time.
// "Repo Karma" (Dex, below): the nurse whose car Dex repossessed. "Abuela Rosa" (Mari, Calle Luna): Rosa Villanueva, 81, wants her late husband Ernesto's car back from the crooked
// Sunshine Towing, then a ride to dominoes night past the tow company's trucks, then one last drive to the beach where
// Ernesto proposed. She gives Mari the car at the end.
#include "missions.h"

namespace Game {
namespace mu {

const u32 kColRosa = 0xffb4a0ffu;

int rosaChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0x7053Au;
    d.gender = Anim::FEMALE;
    d.height = 1.54f;
    d.weight = 0.55f;
    d.muscle = 0.2f;
    d.age = 0.95f;
    d.skinTone = vec3(0.55f, 0.38f, 0.27f);
    d.hairStyle = 6;
    d.hairColor = vec3(0.82f, 0.82f, 0.8f);
    d.top = 13;
    d.topColor = lin(0.35f, 0.55f, 0.75f);
    d.bottom = 4;
    d.bottomColor = lin(0.18f, 0.18f, 0.22f);
    d.shoes = 5;
    d.shoeColor = lin(0.1f, 0.08f, 0.07f);
    d.glasses = 2;
    return g.namedCharacter("stranger_rosa", d);
}

void rosaSay(GameWorld& g, int ped, const std::string& text, float pause = 0.3f) {
    DialogueLine l = line("Rosa", text, pedAlive(g, ped) ? ped : -1, kColRosa);
    Speech::Persona p = Speech::persona("old_woman", true);
    l.hasVoice = true;
    l.voice = p.voice;
    l.spoken = p.tags() + "[accent:latino:0.7]" + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

// The car: Ernesto's green '70 Gatorback (the muscle car model of that name, else any muscle car)
int ernestoModel(GameWorld& g) {
    for (int i = 0; i < (int)g.vassets.size(); i++)
        if (g.vassets[i].spec.name.find("Gatorback") != std::string::npos) return i;
    return pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_COUPE}, 5);
}
const vec3 kErnestoGreen = vec3(0.02f, 0.19f, 0.09f);   // linear

Place rosaHome(GameWorld& g) { return resolveFrontage(g, vec2(1880.f, -140.f)); }

// ==================================================================================================================
// Part 1: "Tow Away" - steal Ernesto's car back from the Sunshine Towing impound lot and bring it home.
class MissionRosaTowAway : public StoryMission {
public:
    int rosa = -1, car = -1;
    Place home, lot;
    bool lotAwake = false;
    const char* title() const override { return "Abuela Rosa: Tow Away"; }
    const char* brief() const override {
        return "Sunshine Towing hauled Rosa Villanueva's late husband's car out of her driveway and wants two thousand dollars for it. Get it "
               "back.";
    }
    long long reward() const override { return 1500; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;
        home = rosaHome(g);
        lot = resolveFrontage(g, vec2(560.f, 3560.f));
        score(SC_NOIR, 0.25f, 17);
        rosa = g.mPed(rosaChar(g), dvec3(home.door), home.yaw + kPi, FAC_FRIEND);
        if (rosa >= 0) {
            g.peds[rosa].invincible = true;
            g.peds[rosa].voice = Speech::persona("old_woman", true).voice;
            setIdle(g, rosa, 7);
        }
        placePlayer(g, placeOffset(g, home, -1.5f, 1.f), home.yaw);
        provideRide(g, home, -12.f);
        vec3 rp = pedPos(g, rosa), mp = playerPos(g);
        facePed(g, rosa, mp);
        facePed(g, g.player, rp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, rp, home.yaw, 22.f, 7.f, 3.5f);
        shots.push_back(shotTwo(rp, mp, 6.f));
        g.mCutscene(shots);
        rosaSay(g, rosa, "[sad:0.5]Marisol, mija. Those Sunshine Towing crooks took Ernesto's car. Right out of my driveway.");
        sayMe(g, "[calm]Towed it for what, Abuela?");
        rosaSay(g, rosa, "[angry:0.5]For being beautiful! They want two thousand dollars. I have six hundred and a pension.");
        sayMe(g, "[calm]Keep your pension. I'll bring it home.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    goTo(g, lot.curb, 45.f, "Get Ernesto's car back from the ~y~Sunshine Towing~s~ impound lot in the Flats.", false, false);
                    next();
                }
                break;
            case 1:
                if (g.playerAt(lot.curb.xy(), 140.f) && car < 0) {
                    // the lot: Ernesto's car at the back, three tow-yard heavies with bats
                    car = spawnCar(g, ernestoModel(g), placeOffset(g, lot, 4.f, 9.f), lot.yaw + kPi * 0.5f, kErnestoGreen);
                    if (car >= 0) {
                        g.vehicles[car].locked = false;
                        g.vehicles[car].sim.engineOn = false;
                    }
                    for (int i = 0; i < 3; i++) {
                        vec3 p = placeOffset(g, lot, -5.f + i * 4.f, 5.f);
                        int e = gunman(g, CAST_THUG_A + i, p, lot.yaw + kPi, i == 2 ? WPN_PISTOL : WPN_BAT, 0.2f);
                        if (e >= 0) setIdle(g, e, i == 1 ? 10 : 7);
                    }
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                }
                if (car >= 0 && g.playerAt(vehPos(g, car).xy(), 25.f) && !lotAwake) {
                    lotAwake = true;
                    clearGoal(g);
                    for (int e : enemies)
                        if (pedAlive(g, e)) setCombat(g, e, g.player, 0.2f);
                    say(g, CAST_THUG_A, enemies.empty() ? -1 : enemies[0], "[angry]Hey! The lot's closed, lady!");
                    g.mObjective("Take ~b~Ernesto's car~s~. The tow yard boys don't want to let it go.");
                    next();
                }
                break;
            case 2:
                if (vehicleLost(g, car, "Ernesto's car")) return MS_FAILED;
                if (g.playerInVehicle(car)) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    sayMe(g, "[happy:0.4]Easy, old girl. Let's get you home.");
                    goTo(g, home.curb, 5.f, "Bring the car home to ~y~Rosa~s~.", true);
                    next();
                }
                break;
            case 3:
                if (vehicleLost(g, car, "Ernesto's car")) return MS_FAILED;
                if (arrived(g) && g.playerInVehicle(car)) {
                    clearGoal(g);
                    g.removePedFromVehicle(g.player, false);
                    setGoto(g, rosa, vehPos(g, car), 1.2f);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(pedPos(g, rosa), playerPos(g), 6.f));
                    g.mCutscene(shots);
                    rosaSay(g, rosa, "[happy]Ay, look at her! Not a scratch. Ernesto would kiss you, and then he would check the paint.");
                    sayMe(g, "[happy:0.4]Lock it in the garage for a while, okay?");
                    rosaSay(g, rosa, "[calm]Thursday is dominoes. I'll call you, mija.");
                    next();
                }
                break;
            case 4:
                if (!g.mInCutscene() && !g.mTalking()) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (car < 0) t.teleportNear(lot.curb.xy(), 60.f);
                else t.teleportNear(vehPos(g, car).xy(), 10.f);
                break;
            case 2:
                if (t.stageTime > 1.f) t.killEnemies();
                if (t.stageTime > 1.5f && car >= 0) t.enter(car);
                break;
            case 3: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Part 2: "Dominoes Night" - drive Rosa to the Club Social while Sunshine Towing's trucks try to ram her off the road.
class MissionRosaDominoes : public StoryMission {
public:
    int rosa = -1, car = -1;
    Place home, club;
    int wave = 0;
    const char* title() const override { return "Abuela Rosa: Dominoes Night"; }
    const char* brief() const override {
        return "Sonny Ruiz of Sunshine Towing swore Rosa would never make it to dominoes night in that car again. Drive her there.";
    }
    long long reward() const override { return 2500; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;
        home = rosaHome(g);
        club = resolveFrontage(g, vec2(1450.f, 60.f));
        score(SC_NOIR, 0.3f, 17);
        car = spawnCar(g, ernestoModel(g), home.curb, home.curbYaw, kErnestoGreen);
        rosa = g.mPed(rosaChar(g), dvec3(home.door), home.yaw + kPi, FAC_FRIEND);
        if (rosa >= 0) {
            g.peds[rosa].voice = Speech::persona("old_woman", true).voice;
            g.peds[rosa].maxHealth = g.peds[rosa].health = 250.f;
        }
        placePlayer(g, placeOffset(g, home, -2.f, 1.f), home.yaw);
        vec3 rp = pedPos(g, rosa), mp = playerPos(g);
        facePed(g, rosa, mp);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotTwo(rp, mp, 6.f));
        g.mCutscene(shots);
        rosaSay(g, rosa, "[happy:0.5]Dominoes night! The girls at the Club Social will die when they see the car.");
        rosaSay(g, rosa, "[angry:0.4]Sonny Ruiz from the tow company called. He says I'll never get there in it. Pah.");
        sayMe(g, "[calm]Buckle up, Abuela.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, rosa, "Rosa")) return MS_FAILED;
        if (vehicleLost(g, car, "Ernesto's car")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, rosa, g.player);
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    g.mObjective("Get in ~b~Ernesto's car~s~ with Rosa.");
                    next();
                }
                break;
            case 1: {
                bool together = g.playerInVehicle(car) && g.peds[rosa].vehicle == car;
                if (g.playerInVehicle(car) && g.peds[rosa].vehicle != car && ::length(pedPos(g, rosa) - vehPos(g, car)) < 8.f) {
                    int seat = g.freeSeat(car, false);
                    if (seat > 0) g.warpPedIntoVehicle(rosa, car, seat);
                }
                if (together) {
                    g.mClearBlips();
                    goTo(g, club.curb, 5.f, "Drive Rosa to the ~y~Club Social~s~.", true);
                    next();
                }
                break;
            }
            case 2: {
                buddyUpdate(g, rosa, nullptr);
                // Sunshine Towing's trucks come after the car
                if (wave == 0 && stageTime > 8.f) {
                    wave = 1;
                    int model = pickModel(g, {Vehicles::VC_SERVICE, Vehicles::VC_TRUCK, Vehicles::VC_PICKUP}, 3);
                    for (int k = 0; k < 2; k++) {
                        float yaw;
                        vec3 sp = approachSpot(g, resolvePlace(g, playerPos(g).xy()), k ? 120.f : -130.f, &yaw);
                        int v = attackCar(g, model, sp, yaw, 1, WPN_FISTS, 0.1f, (u32)(11 + k));
                        if (v < 0) continue;
                        g.vehicles[v].color0 = lin(0.95f, 0.75f, 0.1f);   // Sunshine yellow
                        if (g.vehicles[v].seats[0] >= 0) setCombat(g, g.vehicles[v].seats[0], g.player, 0.1f);
                    }
                    blipEnemies(g);
                    rosaSay(g, rosa, "[scared:0.6]Mija! Yellow trucks! That's Sonny's people!");
                    sayMe(g, "[shout:0.6]Hold on to your purse!");
                }
                if (wave == 1 && stageTime > 30.f && stageTime < 30.1f) rosaSay(g, rosa, "[angry:0.5]Sonny Ruiz, I know your mother!");
                bool together = g.peds[rosa].vehicle >= 0 && g.peds[rosa].vehicle == g.playerVehicle();
                if (arrived(g) && together) {
                    clearGoal(g);
                    g.mClearBlips();
                    for (int v : enemyCars) {
                        releaseDriver(g, v);
                        if (vehicleAlive(g, v) && g.vehicles[v].seats[0] >= 0) setFlee(g, g.vehicles[v].seats[0], g.player);
                    }
                    g.removePedFromVehicle(rosa, true);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(pedPos(g, rosa), vehPos(g, car), 6.f));
                    g.mCutscene(shots);
                    rosaSay(g, rosa, "[happy]On time and in style. Sonny Ruiz can eat his tow truck.");
                    rosaSay(g, rosa, "[calm]Sunday, mija. I want one more drive. I'll call.");
                    next();
                }
                break;
            }
            case 3:
                if (!g.mInCutscene() && !g.mTalking()) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && car >= 0) {
                    t.enter(car);
                    g.warpPedIntoVehicle(rosa, car, 1);
                }
                break;
            case 2:
                if (t.stageTime > 12.f) t.killEnemies();
                if (t.stageTime > 9.f) testGoal(g, t, dt);
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Part 3: "Sunday Drive" - Rosa's last drive to the beach where Ernesto proposed, before the sun goes down. She gives
// Mari the car.
class MissionRosaSunday : public StoryMission {
public:
    int rosa = -1, car = -1;
    Place home;
    vec3 beach;
    float clock = 0.f;
    const char* title() const override { return "Abuela Rosa: Sunday Drive"; }
    const char* brief() const override { return "Rosa wants one more drive in Ernesto's car: to the beach where he proposed, before the sun goes down."; }
    long long reward() const override { return 1000; }

    void start(GameWorld& g) override {
        home = rosaHome(g);
        // the beach at the end of the Deco strip: sand just above the waterline
        beach = gPlaces.beachSea;
        for (float x = beach.x; x > beach.x - 400.f; x -= 4.f)
            if (!g.map->isWater(x, beach.y)) {
                beach = vec3(x - 12.f, beach.y, groundAt(g, x - 12.f, beach.y));
                break;
            }
        if (g.env->timeOfDay < 16.5f || g.env->timeOfDay > 19.f) g.env->timeOfDay = 17.2f;
        score(SC_NOIR, 0.2f, 18);
        car = spawnCar(g, ernestoModel(g), home.curb, home.curbYaw, kErnestoGreen);
        rosa = g.mPed(rosaChar(g), dvec3(home.door), home.yaw + kPi, FAC_FRIEND);
        if (rosa >= 0) {
            g.peds[rosa].voice = Speech::persona("old_woman", true).voice;
            g.peds[rosa].invincible = true;
        }
        placePlayer(g, placeOffset(g, home, -2.f, 1.f), home.yaw);
        vec3 rp = pedPos(g, rosa), mp = playerPos(g);
        facePed(g, rosa, mp);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotTwo(rp, mp, 6.f));
        g.mCutscene(shots);
        rosaSay(g, rosa, "[calm]Take me to Sol Beach, mija. Where Ernesto proposed. Nineteen sixty eight.");
        rosaSay(g, rosa, "[happy:0.3]Before the sun goes down. And drive nice, he is watching.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, car, "Ernesto's car")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, rosa, g.player);
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    g.mObjective("Get in ~b~Ernesto's car~s~ with Rosa.");
                    next();
                }
                break;
            case 1: {
                if (g.playerInVehicle(car) && g.peds[rosa].vehicle != car && ::length(pedPos(g, rosa) - vehPos(g, car)) < 8.f) {
                    int seat = g.freeSeat(car, false);
                    if (seat > 0) g.warpPedIntoVehicle(rosa, car, seat);
                }
                if (g.playerInVehicle(car) && g.peds[rosa].vehicle == car) {
                    g.mClearBlips();
                    goTo(g, beach, 18.f, "Drive Rosa to ~y~Sol Beach~s~ before sunset. Keep the car in one piece.", false, true);
                    clock = 240.f;
                    next();
                }
                break;
            }
            case 2: {
                clock -= dt;
                g.missionTimerHud = clock;
                if (clock <= 0.f) return fail("The sun went down.");
                if (g.vehicles[car].sim.health < 550.f) return fail("You wrecked Ernesto's car.");
                if (stageTime > 20.f && stageTime < 20.1f) rosaSay(g, rosa, "[happy:0.4]He drove like you. Too fast. I pretended to hate it.");
                if (stageTime > 60.f && stageTime < 60.1f) rosaSay(g, rosa, "[sad:0.4]Forty one years. You'd think I would be used to the passenger seat by now.");
                bool together = g.peds[rosa].vehicle >= 0 && g.peds[rosa].vehicle == g.playerVehicle();
                if (arrived(g) && together) {
                    g.missionTimerHud = -1.f;
                    clearGoal(g);
                    g.removePedFromVehicle(rosa, true);
                    g.removePedFromVehicle(g.player, false);
                    vec3 rp = pedPos(g, rosa);
                    vec2 sea = normalize(gPlaces.beachSea.xy() - rp.xy());
                    setGoto(g, rosa, rp + vec3(sea * 6.f, 0.f), 1.f);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotMove(rp + vec3(-sea * 7.f + vec2(sea.y, -sea.x) * 2.f, 1.8f), rp + vec3(sea * 20.f, 2.f),
                                             rp + vec3(-sea * 5.f + vec2(sea.y, -sea.x) * 1.5f, 1.6f), rp + vec3(sea * 20.f, 1.5f), 9.f, 45.f));
                    shots.push_back(shotTwo(rp + vec3(sea * 5.f, 0.f), playerPos(g), 7.f));
                    g.mCutscene(shots);
                    rosaSay(g, rosa, "[sad:0.6]He said the ocean was the only thing bigger than his heart. [pause:0.6] He was a terrible poet.");
                    sayMe(g, "[calm]Sounds like a good man.");
                    rosaSay(g, rosa, "[happy:0.5]The car is yours now, mija. Ernesto would want it driven, not polished in a garage.");
                    sayMe(g, "[happy:0.4]I'll drive it nice. Mostly.");
                    next();
                }
                break;
            }
            case 3:
                if (!g.mInCutscene() && !g.mTalking()) {
                    // the gift: Ernesto's Gatorback joins the garages (green, as he kept it)
                    int m = ernestoModel(g);
                    if (m >= 0 && std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), m) == g.ownedVehicleModels.end()) {
                        g.ownedVehicleModels.push_back(m);
                        int slot = (int)g.ownedVehicleModels.size() - 1;
                        if (slot < 40) setFlag(g, EX_VEHICLE_PAINT + slot, packPaint(kErnestoGreen));
                        g.notify("ROSA", "Ernesto's '70 Gatorback is yours. It waits in every safehouse garage.");
                    }
                    if (car >= 0) g.vehicles[car].persistent = true;   // stays parked on the beach for now
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        g.missionTimerHud = -1.f;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && car >= 0) {
                    t.enter(car);
                    g.warpPedIntoVehicle(rosa, car, 1);
                }
                break;
            case 2: testGoal(g, t, dt, 60.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// "Repo Karma" (Dex): Velma Duarte, a night-shift nurse at Tidewater General, lost her car to one of Dex's repos. Dex
// quietly steals it back off Rook's lot, gets her to her shift on time, and runs off the Coastline Savings collector
// who has been leaning on her.
const u32 kColVelma = 0xff9fe6ffu;

int velmaChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0x5E1A4u;
    d.gender = Anim::FEMALE;
    d.height = 1.66f;
    d.weight = 0.4f;
    d.muscle = 0.3f;
    d.age = 0.3f;
    d.skinTone = vec3(0.45f, 0.3f, 0.22f);
    d.hairStyle = 8;
    d.hairColor = vec3(0.05f, 0.04f, 0.035f);
    d.top = 5;
    d.topColor = lin(0.35f, 0.7f, 0.72f);   // scrubs
    d.bottom = 9;
    d.bottomColor = lin(0.33f, 0.66f, 0.7f);
    d.shoes = 0;
    d.shoeColor = lin(0.95f, 0.95f, 0.95f);
    d.role = 6;
    return g.namedCharacter("stranger_velma", d);
}

void velmaSay(GameWorld& g, int ped, const std::string& text, float pause = 0.3f) {
    DialogueLine l = line("Velma", text, pedAlive(g, ped) ? ped : -1, kColVelma);
    Speech::Persona p = Speech::persona("stranger_velma", true);
    l.hasVoice = true;
    l.voice = p.voice;
    l.spoken = p.tags() + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

Place velmaHome(GameWorld& g) { return resolveFrontage(g, vec2(1520.f, 1300.f)); }
int velmaModel(GameWorld& g) { return pickModel(g, {Vehicles::VC_COMPACT, Vehicles::VC_SEDAN}, 11); }
const vec3 kVelmaBlue = vec3(0.12f, 0.22f, 0.5f);

// Part 1: "Repo Karma" - take Velma's car back off Rook's lot and bring it to her building.
class MissionVelmaKarma : public StoryMission {
public:
    int velma = -1, car = -1;
    Place home;
    const char* title() const override { return "Repo Karma"; }
    const char* brief() const override { return "The nurse whose car Dex repossessed lost two shifts without it. Get it back off Rook's lot, quietly."; }
    long long reward() const override { return 800; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;
        home = velmaHome(g);
        score(SC_NOIR, 0.25f, 19);
        velma = g.mPed(velmaChar(g), dvec3(home.door), home.yaw + kPi, FAC_FRIEND);
        if (velma >= 0) {
            g.peds[velma].invincible = true;
            g.peds[velma].voice = Speech::persona("stranger_velma", true).voice;
            setIdle(g, velma, 7);
        }
        car = spawnCar(g, velmaModel(g), curbOffset(g, gPlaces.rookShop, 16.f), gPlaces.rookShop.curbYaw, kVelmaBlue);
        if (car >= 0) g.vehicles[car].sim.engineOn = false;
        placePlayer(g, placeOffset(g, home, -1.5f, 1.f), home.yaw);
        provideRide(g, home, -12.f);
        vec3 vp = pedPos(g, velma), dp = playerPos(g);
        facePed(g, velma, dp);
        facePed(g, g.player, vp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, vp, home.yaw, 26.f, 9.f, 4.f);
        shots.push_back(shotTwo(vp, dp, 6.f));
        shots.push_back(shotOver(dp, vp, 6.f));
        g.mCutscene(shots);
        velmaSay(g, velma, "[angry:0.6]You. You're the repo man. You took my car last Tuesday.");
        sayMe(g, "[calm]You missed some payments. It's the job.");
        velmaSay(g, velma, "[sad:0.6]I missed some payments because Coastline tripled my rate. Then I missed two shifts because I had no car. I'm a nurse.");
        sayMe(g, "[sad:0.4]... Blue hatchback, right? Give me an hour.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (vehicleLost(g, car, "Velma's car")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    g.mTarget(vehPos(g, car).xy(), UI::BLIP_VEHICLE);
                    g.mObjective("Take ~b~Velma's car~s~ back off Rook's lot. Rook doesn't need to know.");
                    next();
                }
                break;
            case 1:
                if (g.playerInVehicle(car)) {
                    g.mClearBlips();
                    g.mClearTarget();
                    sayMe(g, "[whisper:0.4]Sorry, Rook. Karma's a repo man too.");
                    goTo(g, home.curb, 5.f, "Bring the car to ~y~Velma's building~s~.", true);
                    next();
                }
                break;
            case 2:
                if (arrived(g) && g.playerInVehicle(car)) {
                    clearGoal(g);
                    g.removePedFromVehicle(g.player, false);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(pedPos(g, velma), playerPos(g), 6.f));
                    g.mCutscene(shots);
                    velmaSay(g, velma, "[happy:0.6]That's my car. With a full tank? Who are you?");
                    sayMe(g, "[calm]Dex. Just a guy correcting a paperwork error.");
                    velmaSay(g, velma, "[happy:0.4]Well, Dex. If you ever get shot, ask for Velma at Tidewater General.");
                    next();
                }
                break;
            case 3:
                if (!g.mInCutscene() && !g.mTalking()) {
                    phoneLine(g, CAST_ROOK, "[calm]Somebody took a blue hatchback off my lot. Funny. You wouldn't know anything, would you?");
                    sayMe(g, "[calm]Never heard of it.");
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1: if (t.stageTime > 0.5f && car >= 0) t.enter(car); break;
            case 2: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// Part 2: "Night Shift" - get Velma to Tidewater General before her shift starts.
class MissionVelmaShift : public StoryMission {
public:
    int velma = -1, car = -1;
    Place home;
    float clock = 0.f;
    const char* title() const override { return "Night Shift"; }
    const char* brief() const override { return "Velma's car won't start and her shift at Tidewater General begins in a few minutes."; }
    long long reward() const override { return 1500; }

    void start(GameWorld& g) override {
        home = velmaHome(g);
        score(SC_CHASE, 0.5f, 19);
        velma = g.mPed(velmaChar(g), dvec3(home.door), home.yaw + kPi, FAC_FRIEND);
        if (velma >= 0) {
            g.peds[velma].voice = Speech::persona("stranger_velma", true).voice;
            g.peds[velma].maxHealth = g.peds[velma].health = 250.f;
        }
        placePlayer(g, placeOffset(g, home, -2.f, 1.f), home.yaw);
        car = playerCar = spawnCar(g, pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_MUSCLE}, 2), home.curb, home.curbYaw, lin(0.18f, 0.2f, 0.22f));
        std::vector<CutsceneShot> shots;
        shots.push_back(shotTwo(pedPos(g, velma), playerPos(g), 5.f));
        g.mCutscene(shots);
        velmaSay(g, velma, "[scared:0.6]Dex! The car won't start and my shift starts in five minutes. If I'm late again they'll fire me.");
        sayMe(g, "[calm]Get in. I know a shortcut. Several, actually.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, velma, "Velma")) return MS_FAILED;
        if (vehicleLost(g, car, "car")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, velma, g.player);
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    g.mObjective("Get in the ~b~car~s~ with Velma.");
                    next();
                }
                break;
            case 1:
                if (g.playerInVehicle(car) && g.peds[velma].vehicle != car && ::length(pedPos(g, velma) - vehPos(g, car)) < 8.f) {
                    int seat = g.freeSeat(car, false);
                    if (seat > 0) g.warpPedIntoVehicle(velma, car, seat);
                }
                if (g.playerInVehicle(car) && g.peds[velma].vehicle == car) {
                    g.mClearBlips();
                    goTo(g, gPlaces.hospital.curb, 6.f, "Get Velma to ~y~Tidewater General~s~ before her shift.", true);
                    clock = 50.f + ::length(gPlaces.hospital.curb - playerPos(g)) / 18.f;
                    next();
                }
                break;
            case 2: {
                clock -= dt;
                g.missionTimerHud = clock;
                if (clock <= 0.f) return fail("Velma was late for her shift.");
                if (stageTime > 12.f && stageTime < 12.1f) velmaSay(g, velma, "[scared:0.5]Red light! That was a red light!");
                if (stageTime > 30.f && stageTime < 30.1f) velmaSay(g, velma, "[happy:0.4]You drive like my ambulance guys. That's not a compliment.");
                bool together = g.peds[velma].vehicle >= 0 && g.peds[velma].vehicle == g.playerVehicle();
                if (arrived(g) && together) {
                    g.missionTimerHud = -1.f;
                    clearGoal(g);
                    g.removePedFromVehicle(velma, true);
                    setGoto(g, velma, gPlaces.hospital.door, 2.f);
                    velmaSay(g, velma, clock > 20.f ? "[happy]With time to spare! I owe you a coffee. A terrible hospital coffee."
                                                    : "[happy:0.5]Made it! Barely. Thank you, Dex!");
                    next();
                }
                break;
            }
            case 3:
                if (!g.mTalking()) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        g.missionTimerHud = -1.f;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && car >= 0) {
                    t.enter(car);
                    g.warpPedIntoVehicle(velma, car, 1);
                }
                break;
            case 2: testGoal(g, t, dt, 70.f); break;
            default: break;
        }
    }
};

// Part 3: "Collections" - the Coastline Savings collector is at Velma's door with two heavies. Run them off and take the
// file they're holding over her.
class MissionVelmaCollections : public StoryMission {
public:
    int velma = -1, collector = -1, file = -1;
    Place home;
    const char* title() const override { return "Collections"; }
    const char* brief() const override {
        return "Coastline Savings sent a collector and two heavies to Velma's building. They want the car, the apartment and her signature.";
    }
    long long reward() const override { return 3000; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;
        home = velmaHome(g);
        score(SC_CHASE, 0.7f, 19);
        velma = g.mPed(velmaChar(g), dvec3(home.door), home.yaw + kPi, FAC_FRIEND);
        if (velma >= 0) {
            g.peds[velma].invincible = true;
            g.peds[velma].voice = Speech::persona("stranger_velma", true).voice;
            setIdle(g, velma, 5);
        }
        collector = gunman(g, CAST_BANKER, placeOffset(g, home, 1.5f, 1.2f), home.yaw, WPN_PISTOL, 0.25f);
        for (int i = 0; i < 2; i++) gunman(g, CAST_GUARD_A + i, placeOffset(g, home, -2.f + i * 5.f, 2.5f), home.yaw, WPN_BAT, 0.2f);
        for (int e : enemies) setIdle(g, e, e == collector ? 7 : 10);
        placePlayer(g, curbOffset(g, home, -40.f), home.curbYaw);
        vec3 cp = pedPos(g, collector), vp = pedPos(g, velma);
        facePed(g, collector, vp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, cp, home.yaw, 28.f, 10.f, 4.f);
        shots.push_back(shotTwo(cp, vp, 6.f));
        shots.push_back(shotOver(vp, cp, 5.f));
        g.mCutscene(shots);
        say(g, CAST_BANKER, collector, "[calm]Ms. Duarte. Coastline Savings is a patient bank. Today it stopped being patient.");
        velmaSay(g, velma, "[scared:0.6]I paid you. I paid everything you asked.");
        say(g, CAST_BANKER, collector, "[calm]The rate changed. Sign here, or my friends help you move out.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    blipEnemies(g);
                    g.mObjective("Run off the ~r~Coastline collector~s~ and his heavies.");
                    next();
                }
                break;
            case 1:
                if (g.playerAt(pedPos(g, collector).xy(), 30.f) && stageTime > 1.f && !g.mTalking() && timer == 0.f) {
                    timer = 1.f;
                    for (int e : enemies)
                        if (pedAlive(g, e)) setCombat(g, e, g.player, 0.25f);
                    sayMe(g, "[angry:0.6]Coastline Savings. You're Sandoval's bank, right? I've got a message for him.");
                }
                if (aliveEnemies(g) == 0) {
                    g.mClearBlips();
                    vec3 fp = pedPos(g, collector);
                    file = spawnPackage(g, fp.z > -1e5f ? fp + vec3(0.6f, 0.f, 0.f) : placeOffset(g, home, 1.5f, 1.2f));
                    goTo(g, g.pickups[file].pos.toVec3(), 1.5f, "Grab the collector's ~g~file~s~.");
                    next();
                }
                break;
            case 2:
                if (grabNear(g, file, 1.8f)) {
                    clearGoal(g);
                    setIdle(g, velma, 7);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(pedPos(g, velma), playerPos(g), 6.f));
                    g.mCutscene(shots);
                    sayMe(g, "[calm]Your loan file. Forged signature on page three. Kit at Pulse FM will love this.");
                    velmaSay(g, velma, "[sad:0.4]Why are you doing this, Dex?");
                    sayMe(g, "[sad:0.5]Because I used to pull people out of the water. Somewhere along the way I started pushing them in.");
                    velmaSay(g, velma, "[happy:0.5]Well. Free check-ups for life. Don't make me use them.");
                    next();
                }
                break;
            case 3:
                if (!g.mInCutscene() && !g.mTalking()) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f) t.teleportNear(pedPos(g, collector).xy(), 8.f);
                if (t.stageTime > 2.f) t.killEnemies();
                break;
            case 2:
                if (file >= 0 && t.stageTime > 0.5f) t.teleport(g.pickups[file].pos.toVec3(), 0.f);
                break;
            default: break;
        }
    }
};

}  // namespace mu
}  // namespace Game
