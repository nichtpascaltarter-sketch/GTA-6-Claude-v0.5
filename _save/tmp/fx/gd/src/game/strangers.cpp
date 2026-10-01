// Strangers: short side stories with recurring characters, unlocked one part at a time.
// "Abuela Rosa" (Mari, Calle Luna): Rosa Villanueva, 81, wants her late husband Ernesto's car back from the crooked
// Sunshine Towing, then a ride to dominoes night past the tow company's trucks, then one last drive to the beach where
// Ernesto proposed. She gives Mari the car at the end.
// "Velma" (Dex): the night-shift nurse whose car Dex repossessed. He steals it back, gets her to her shift on time and
// runs the Coastline Savings collector off her doorstep.
// "Jaz" (either protagonist): Jazmin Okafor, a Sol Beach influencer. Sunset photo stops against the clock, a stunt jump
// for her followers, then her stolen phone. Her shout-out raises the income of every business the player owns.
#include "missions.h"

namespace Game {
namespace mu {

const u32 kColRosa = 0xffb4a0ffu;

// a stranger's text after a part is done: where to find them for the next one
void strangerText(GameWorld& g, const char* from, const char* text, vec3 where) {
    if (!phoneWired()) return;
    vec2 loc = where.xy();
    addMessage(g, from, text, -1, false, -1, &loc, 0, nullptr);
}

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

    void finish(GameWorld& g, bool passed) override {
        if (passed)
            strangerText(g, "Rosa", "Mija, thank you for Ernesto's car. Thursday is dominoes night at the club. Would you drive an old woman? "
                                    "Those tow trucks are still circling.", rosaHome(g).door);
    }

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

    void finish(GameWorld& g, bool passed) override {
        if (passed) strangerText(g, "Rosa", "Sunday, if you have time. There is one more drive I need to take in that car.", rosaHome(g).door);
    }

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
                    // the last beat: pulling back and up from behind the two of them, into the sunset
                    shots.push_back(shotMove(rp + vec3(-sea * 3.f, 2.1f), rp + vec3(sea * 30.f, 2.5f), rp + vec3(-sea * 15.f, 7.5f),
                                             rp + vec3(sea * 45.f, 3.5f), 9.f, 50.f));
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

    void finish(GameWorld& g, bool passed) override {
        if (passed) strangerText(g, "Velma", "The car made a noise and now it won't start at all. My shift is at eight. Any chance?", velmaHome(g).door);
    }

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
        g.missionTimerHud = -1.f;
        if (passed)
            strangerText(g, "Velma", "A man from Coastline Savings keeps coming to my door. He says the loan is due again. I'm scared, Dex.",
                         velmaHome(g).door);
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

// ==================================================================================================================
// "Jaz" (either protagonist)
const u32 kColJaz = 0xffff80e0u;

int jazChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0x1A22u;
    d.gender = Anim::FEMALE;
    d.height = 1.71f;
    d.weight = 0.35f;
    d.muscle = 0.3f;
    d.age = 0.22f;
    d.skinTone = vec3(0.32f, 0.2f, 0.13f);
    d.hairStyle = 9;                          // bob, dyed magenta
    d.hairColor = vec3(0.55f, 0.12f, 0.4f);
    d.top = 15;                               // crop top
    d.topColor = lin(0.98f, 0.82f, 0.18f);
    d.bottom = 4;                             // skirt
    d.bottomColor = lin(0.93f, 0.93f, 0.95f);
    d.shoes = 0;
    d.shoeColor = lin(0.97f, 0.97f, 0.97f);
    d.glasses = 0;
    d.role = 4;
    return g.namedCharacter("stranger_jaz", d);
}

void jazSay(GameWorld& g, int ped, const std::string& text, float pause = 0.25f) {
    DialogueLine l = line("Jaz", text, pedAlive(g, ped) ? ped : -1, kColJaz);
    Speech::Persona p = Speech::persona("stranger_jaz", true);
    l.hasVoice = true;
    l.voice = p.voice;
    l.spoken = p.tags() + "[bright]" + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

Place jazSpot(GameWorld& g) { return gPlaces.cafeBeach; }

// Jaz climbs into the player's vehicle when it pulls up next to her (shared by all three parts)
bool jazRides(GameWorld& g, int jaz) {
    int pv = g.playerVehicle();
    if (pv < 0 || !pedAlive(g, jaz)) return false;
    if (g.peds[jaz].vehicle == pv) return true;
    if (g.peds[jaz].vehicle < 0 && ::length(pedPos(g, jaz) - vehPos(g, pv)) < 9.f) {
        int seat = g.freeSeat(pv, false);
        if (seat > 0) g.warpPedIntoVehicle(jaz, pv, seat);
    }
    return g.peds[jaz].vehicle == pv;
}

// Part 1: "Golden Hour" - three sunset photo stops before the light goes.
class MissionJazGoldenHour : public StoryMission {
public:
    int jaz = -1;
    Place cafe;
    std::vector<Place> stops;
    int stop = 0;
    float clock = 0.f;
    const char* title() const override { return "Jaz: Golden Hour"; }
    const char* brief() const override { return "Tidegram star Jaz Okafor needs three sunset photos before the light goes, and her driver ghosted her."; }
    long long reward() const override { return 1200; }

    void start(GameWorld& g) override {
        cafe = jazSpot(g);
        if (g.env->timeOfDay < 16.5f || g.env->timeOfDay > 19.f) g.env->timeOfDay = 16.9f;   // golden hour
        score(SC_CHASE, 0.3f, 21);
        stops = {gPlaces.beachPier, gPlaces.midtownPark, gPlaces.solarisOne};
        jaz = g.mPed(jazChar(g), dvec3(placeOffset(g, cafe, 1.2f, 1.6f)), cafe.yaw + kPi, FAC_FRIEND);
        if (jaz >= 0) {
            g.peds[jaz].voice = Speech::persona("stranger_jaz", true).voice;
            g.peds[jaz].maxHealth = g.peds[jaz].health = 250.f;
        }
        placePlayer(g, placeOffset(g, cafe, -1.4f, 2.6f), cafe.yaw);
        provideRide(g, cafe, -12.f);
        vec3 jp = pedPos(g, jaz), mp = playerPos(g);
        facePed(g, jaz, mp);
        facePed(g, g.player, jp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, jp, cafe.yaw, 26.f, 8.f, 4.f);
        shots.push_back(shotTwo(jp, mp, 6.f));
        shots.push_back(shotOver(mp, jp, 5.f));
        g.mCutscene(shots);
        jazSay(g, jaz, "[happy]Oh my gosh. You have a car. Perfect. My driver ghosted me and golden hour waits for no one.");
        sayMe(g, "[calm]Golden hour?");
        jazSay(g, jaz, "[happy:0.6]The sun is going down. I need three spots before it does. The pier, the park, the Solaris tower. Go go go!");
    }

    void headToStop(GameWorld& g) {
        static const char* const kNames[3] = {"Sol Beach Pier", "Midtown Park", "Solaris One"};
        goTo(g, stops[stop].curb, 6.f, StrFormat("Get Jaz to ~y~%s~s~ while the light lasts.", kNames[Clamp(stop, 0, 2)]), true);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, jaz, "Jaz")) return MS_FAILED;
        bool riding = jazRides(g, jaz);
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, jaz, g.player);
                    g.mObjective("Get in a car with ~b~Jaz~s~.");
                    next();
                }
                break;
            case 1:
                if (riding) {
                    float total = 0.f;
                    vec3 at = playerPos(g);
                    for (const Place& p : stops) {
                        total += ::length(p.curb - at);
                        at = p.curb;
                    }
                    clock = 50.f + total / 15.f;
                    jazSay(g, jaz, "[happy]First stop, the pier. The light on the water is everything.");
                    headToStop(g);
                    next();
                }
                break;
            case 2: {
                clock -= dt;
                g.missionTimerHud = clock;
                if (clock <= 0.f) return fail("The sun went down. No golden hour, no content.");
                if (!riding) {
                    if (g.peds[jaz].vehicle < 0) setFollow(g, jaz, g.player);
                    if (g.hudHelpTimer <= 0.f) g.help("Jaz needs the ride. Get her into a car.", 2.f);
                }
                if (stop == 0 && stageTime > 9.f && stageTime < 9.1f) jazSay(g, jaz, "[happy:0.4]Can you drive, like, more cinematically? Slower on the curves. Faster on the straights.");
                if (stop == 1 && stageTime > 7.f && stageTime < 7.1f) jazSay(g, jaz, "[calm]Two hundred thousand people are waiting for this sunset. No pressure.");
                if (stop == 2 && stageTime > 6.f && stageTime < 6.1f) jazSay(g, jaz, "[scared:0.4]The sun is touching the water. Hurry, hurry, hurry!");
                if (arrived(g) && riding && g.vehicles[g.playerVehicle()].sim.speed() < 4.f) {
                    clearGoal(g);
                    g.missionTimerHud = -1.f;
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotVehicle(g, g.playerVehicle(), 2.8f, stop % 2 ? -1.f : 1.f, 42.f));
                    g.mCutscene(shots);
                    static const char* const kPose[3] = {"[happy]Hold still. Chin up. Got it!", "[happy:0.5]Palm trees, golden light, a stranger's car. Iconic.",
                                                         "[happy]Oh, that's the one. That's the cover."};
                    jazSay(g, jaz, kPose[Clamp(stop, 0, 2)]);
                    timer = 0.f;
                    next();
                }
                break;
            }
            case 3:   // the photo
                if (stageTime > 0.7f && timer == 0.f) {
                    timer = 1.f;
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_CAMERA_SHUTTER, 0.9f);
#endif
                }
                if (timer > 0.f && stageTime < 0.85f + 0.12f) spawnLight(dvec3(pedPos(g, jaz) + vec3(0, 0, 1.5f)), vec3(1.f, 0.97f, 0.9f) * 400.f, 6.f);
                if (!g.mInCutscene() && !g.mTalking() && stageTime > 1.2f) {
                    stop++;
                    if (stop < (int)stops.size()) {
                        headToStop(g);
                        setStage(2);
                    } else {
                        int pv = g.playerVehicle();
                        if (pv >= 0) {
                            g.removePedFromVehicle(jaz, true);
                            g.removePedFromVehicle(g.player, false);
                        }
                        facePed(g, jaz, playerPos(g));
                        std::vector<CutsceneShot> shots;
                        shots.push_back(shotTwo(pedPos(g, jaz), playerPos(g), 7.f));
                        g.mCutscene(shots);
                        jazSay(g, jaz, "[happy]Two million views by morning, easy. You're in the background of all of them, by the way.");
                        sayMe(g, "[calm]Great. My face on the internet.");
                        jazSay(g, jaz, "[happy:0.5]Our face. Follow me. Literally. I'll text you.");
                        next();
                    }
                }
                break;
            case 4:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setGoto(g, jaz, placeOffset(g, stops.back(), 6.f, 3.f), 1.3f);
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        g.missionTimerHud = -1.f;
        if (passed) strangerText(g, "Jaz", "Two million views!! Next idea: a stunt jump. You in? Cafe, whenever.", jazSpot(g).door);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && rideCar >= 0) {
                    t.enter(rideCar);
                    if (g.playerVehicle() >= 0) g.warpPedIntoVehicle(jaz, g.playerVehicle(), 1);
                }
                break;
            case 2:
                testGoal(g, t, dt, 60.f);
                if (arrived(g) && g.playerVehicle() >= 0) g.vehicles[g.playerVehicle()].sim.body.vel = vec3(0.f);
                break;
            default: break;
        }
    }
};

// Part 2: "Viral" - a stunt ramp jump for Jaz's camera.
class MissionJazViral : public StoryMission {
public:
    int jaz = -1;
    Place cafe;
    int ramp = -1;                // index into gRamps (-1: any big jump counts)
    vec3 runUp, filmSpot, rampFoot;
    double since = 0.0;           // jumps landed after this time count
    int tries = 0;
    float bestDist = 0.f;
    const char* title() const override { return "Jaz: Viral"; }
    const char* brief() const override { return "Jaz wants a stunt jump for her followers. A car, a ramp, her camera. What could go wrong."; }
    long long reward() const override { return 2000; }

    void finish(GameWorld& g, bool passed) override {
        if (passed) strangerText(g, "Jaz", "HELP. Someone stole my phone. This is my backup. Come to the cafe, now, please!", jazSpot(g).door);
    }

    void start(GameWorld& g) override {
        cafe = jazSpot(g);
        score(SC_CHASE, 0.45f, 21);
        jaz = g.mPed(jazChar(g), dvec3(placeOffset(g, cafe, 1.2f, 1.6f)), cafe.yaw + kPi, FAC_FRIEND);
        if (jaz >= 0) {
            g.peds[jaz].voice = Speech::persona("stranger_jaz", true).voice;
            g.peds[jaz].invincible = true;
        }
        // the nearest stunt ramp (they are set up on open ground around the island)
        float best = 1e9f;
        for (int i = 0; i < (int)gRamps.size(); i++) {
            float d = ::length(gRamps[i].foot.xy() - cafe.pos.xy()) + (gRamps[i].waterGap ? 400.f : 0.f);
            if (d < best) {
                best = d;
                ramp = i;
            }
        }
        if (best > 3000.f) ramp = -1;   // nothing close: any big jump will do, filmed from the passenger seat
        if (ramp >= 0) {
            const StuntRamp& r = gRamps[ramp];
            rampFoot = r.foot;
            vec2 st = r.foot.xy() - r.dir * 70.f;
            runUp = vec3(st, groundAt(g, st.x, st.y, r.foot.z + 5.f));
            vec2 fs = r.foot.xy() + r.dir * 30.f + perp(r.dir) * 16.f;
            filmSpot = vec3(fs, groundAt(g, fs.x, fs.y, r.foot.z + 5.f));
        }
        placePlayer(g, placeOffset(g, cafe, -1.4f, 2.6f), cafe.yaw);
        provideRide(g, cafe, -12.f);
        vec3 jp = pedPos(g, jaz), mp = playerPos(g);
        facePed(g, jaz, mp);
        facePed(g, g.player, jp);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotTwo(jp, mp, 6.f));
        shots.push_back(shotOver(jp, mp, 5.f, -1.f));
        g.mCutscene(shots);
        jazSay(g, jaz, "[happy]Okay. Content idea. You, a car, a ramp, me filming. Instant legend.");
        sayMe(g, "[calm]And if I crash?");
        jazSay(g, jaz, "[happy:0.5]Then it's a blooper reel. Also content.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        bool riding = jazRides(g, jaz);
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, jaz, g.player);
                    g.mObjective("Get in a car with ~b~Jaz~s~.");
                    next();
                }
                break;
            case 1:
                if (riding) {
                    if (ramp >= 0) {
                        goTo(g, runUp, 14.f, "Drive Jaz to the ~y~stunt ramp~s~.");
                        jazSay(g, jaz, "[happy:0.5]There's a ramp out past the edge of town. I found it on a map. For science.");
                        next();
                    } else {
                        // no ramp set up: any big jump counts, she films from the passenger seat
                        g.mObjective("Find something to jump. Jaz is filming from the passenger seat.");
                        jazSay(g, jaz, "[happy:0.5]Find us a jump. Any jump. I'm rolling.");
                        since = g.time;
                        setStage(3);
                    }
                }
                break;
            case 2:
                if (!riding && g.peds[jaz].vehicle < 0) setFollow(g, jaz, g.player);
                if (arrived(g) && riding) {
                    clearGoal(g);
                    g.removePedFromVehicle(jaz, true);
                    setGoto(g, jaz, filmSpot, 2.2f);
                    g.mTarget(rampFoot.xy(), UI::BLIP_RACE);
                    g.mObjective("Hit the ~y~ramp~s~ and make it big. Jaz is filming from the side.");
                    jazSay(g, jaz, "[happy]I'll film from over there. Wait for my signal. Actually, don't wait. I'm always rolling.");
                    since = g.time;
                    next();
                }
                break;
            case 3: {
                if (gAct.lastJumpTime > since) {
                    since = gAct.lastJumpTime;
                    float dist = gAct.lastJumpDist, air = gAct.lastJumpAir;
                    bestDist = Max(bestDist, dist);
                    if (dist >= 25.f && air >= 1.f) {
                        g.mClearTarget();
                        vec3 land = gAct.lastJumpLanding;
                        g.socialReport(UI::TE_STUNT_JUMP, dvec3(land), nullptr, dist);
                        if (ramp >= 0 && g.peds[jaz].vehicle < 0) setGoto(g, jaz, land, 4.f);
                        jazSay(g, jaz, gAct.lastJumpUpright ? StrFormat("[shout]That was insane! %d meters! Posting it right now!", (int)dist)
                                                            : std::string("[scared:0.6]Oh no. Oh no. Are you okay? [pause:0.4][happy]That is amazing footage."));
                        timer = 0.f;
                        next();
                    } else if (dist > 4.f) {
                        tries++;
                        static const char* const kMore[3] = {"[sad:0.3]That was a hop. My grandma hops higher. Again!",
                                                             "[calm]More speed. Like, a lot more speed.", "[happy:0.4]Closer! Go again, faster this time!"};
                        jazSay(g, jaz, kMore[tries % 3]);
                    }
                }
                break;
            }
            case 4:
                timer += dt;
                if (timer > 3.5f && !g.mTalking()) {
                    sayMe(g, "[calm]How many views?");
                    jazSay(g, jaz, "[happy]Forty thousand. In one minute. You're a star. Well, the car is.");
                    next();
                }
                break;
            case 5:
                if (!g.mTalking()) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && rideCar >= 0) {
                    t.enter(rideCar);
                    if (g.playerVehicle() >= 0) g.warpPedIntoVehicle(jaz, g.playerVehicle(), 1);
                }
                break;
            case 2: testGoal(g, t, dt, 70.f); break;
            case 3: {
                // the ramp itself is covered by the roam test; here landed jumps are reported directly: a short hop
                // first (Jaz asks for another go), then a big one
                bool hop = t.stageTime > 1.5f && tries == 0, big = t.stageTime > 3.f && tries > 0;
                if ((hop || big) && gAct.lastJumpTime <= since) {
                    gAct.lastJumpTime = g.time;
                    gAct.lastJumpDist = big ? 34.f : 12.f;
                    gAct.lastJumpAir = big ? 1.6f : 0.6f;
                    gAct.lastJumpUpright = true;
                    gAct.lastJumpLanding = playerPos(g);
                }
                break;
            }
            default: break;
        }
    }
};

// Part 3: "Deleted" - a Tide Gossip snoop on a scooter grabbed Jaz's phone.
class MissionJazDeleted : public StoryMission {
public:
    int jaz = -1, thief = -1, scooter = -1, phone = -1;
    Place cafe;
    float farTime = 0.f, closeTime = 0.f, stuckTime = 0.f;
    const char* title() const override { return "Jaz: Deleted"; }
    const char* brief() const override { return "A gossip-site snoop on a scooter snatched Jaz's phone. Everything she has ever filmed is on it."; }
    long long reward() const override { return 3000; }

    void start(GameWorld& g) override {
        cafe = jazSpot(g);
        score(SC_CHASE, 0.7f, 21);
        jaz = g.mPed(jazChar(g), dvec3(placeOffset(g, cafe, 1.2f, 1.6f)), cafe.yaw + kPi, FAC_FRIEND);
        if (jaz >= 0) {
            g.peds[jaz].voice = Speech::persona("stranger_jaz", true).voice;
            g.peds[jaz].invincible = true;
            setIdle(g, jaz, 7);
        }
        placePlayer(g, placeOffset(g, cafe, -1.4f, 2.6f), cafe.yaw);
        provideRide(g, cafe, -10.f);
        // the snoop, already riding off down the street
        int model = pickModel(g, {Vehicles::VC_SCOOTER, Vehicles::VC_MOTORBIKE}, 3);
        scooter = spawnCar(g, model, curbOffset(g, cafe, 55.f), cafe.curbYaw, lin(0.9f, 0.1f, 0.5f));
        thief = g.mPed(g.randomCivilianChar(0x60551Bu, 0), dvec3(vehPos(g, scooter)), cafe.curbYaw, FAC_CIVILIAN);
        if (thief >= 0 && scooter >= 0) {
            g.peds[thief].brain.type = BRAIN_NONE;
            g.peds[thief].maxHealth = g.peds[thief].health = 160.f;
            g.warpPedIntoVehicle(thief, scooter, 0);
        }
        vec3 jp = pedPos(g, jaz), mp = playerPos(g);
        facePed(g, jaz, mp);
        facePed(g, g.player, jp);
        std::vector<CutsceneShot> shots;
        CutsceneShot s1 = shotTwo(jp, mp, 5.f);
        s1.handheld = 0.7f;
        shots.push_back(s1);
        g.mCutscene(shots);
        jazSay(g, jaz, "[scared]He took my phone! A guy on a scooter, right out of my hand! My whole life is on that phone!");
        jazSay(g, jaz, "[angry:0.6]Pink jacket. Tide Gossip. They've been following me for weeks. Go!");
    }

    void thiefBails(GameWorld& g) {
        releaseDriver(g, scooter);
        if (pedAlive(g, thief) && g.peds[thief].vehicle >= 0) g.removePedFromVehicle(thief, true);
        if (pedAlive(g, thief)) setFlee(g, thief, g.player);
        g.mClearBlips();
        g.mBlipPed(thief, UI::BLIP_ENEMY);
        g.mObjective("Catch the ~r~snoop~s~ on foot and get the phone back.");
    }

    void dropPhone(GameWorld& g) {
        vec3 p = pedPos(g, thief);
        if (p.z < -1e5f) p = playerPos(g);
        phone = spawnPackage(g, p + vec3(0.8f, 0.f, 0.f));
        g.mClearBlips();
        goTo(g, g.pickups[phone].pos.toVec3(), 1.5f, "Grab ~g~Jaz's phone~s~.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (vehicleAlive(g, scooter)) {
                        RoutePath path;
                        vec3 sp = vehPos(g, scooter);
                        vec2 away = sp.xy() + normalize(sp.xy() - cafe.pos.xy() + vec2(0.01f, 0.f)) * 1600.f;
                        buildRoadPath(g, sp.xy(), away, path);
                        ScriptDriver& dr = addDriver(g, scooter, path, 21.f);
                        dr.rubberPed = g.player;
                        dr.rubberGap = 80.f;
                        g.mBlipVehicle(scooter, UI::BLIP_ENEMY);
                    }
                    g.mObjective("Chase down the ~r~Tide Gossip snoop~s~. Knock him off that scooter.");
                    next();
                }
                break;
            case 1: {
                if (!pedAlive(g, thief)) {
                    dropPhone(g);
                    setStage(3);
                    break;
                }
                Ped& tp = g.peds[thief];
                float d = ::length(pedPos(g, thief) - playerPos(g));
                bool wrecked = !vehicleAlive(g, scooter) || vehicleDisabled(g, scooter) || tp.vehicle < 0;
                if (tp.vehicle >= 0 && vehicleAlive(g, scooter) && g.vehicles[scooter].sim.speed() < 2.f && d < 25.f) stuckTime += dt;
                else stuckTime = 0.f;
                if (wrecked || stuckTime > 2.5f) {
                    thiefBails(g);
                    say(g, CAST_THUG_C, thief, "[scared]It's just gossip, man! It's not personal!");
                    next();
                    break;
                }
                if (d > 450.f) farTime += dt;
                else farTime = 0.f;
                if (farTime > 15.f) return fail("The snoop got away with Jaz's phone.");
                break;
            }
            case 2: {
                if (!pedAlive(g, thief)) {
                    dropPhone(g);
                    next();
                    break;
                }
                Ped& tp = g.peds[thief];
                float d = ::length(pedPos(g, thief) - playerPos(g));
                if (g.playerVehicle() < 0 && d < 2.8f) closeTime += dt;
                else closeTime = Max(0.f, closeTime - dt);
                if (closeTime > 1.2f || tp.health < tp.maxHealth * 0.5f || (d < 3.5f && tp.ragdoll)) {
                    say(g, CAST_THUG_C, thief, "[scared]Take it! Take it! I never even unlocked it!");
                    tp.invincible = true;
                    setFlee(g, thief, g.player);
                    dropPhone(g);
                    next();
                    break;
                }
                if (d > 300.f) farTime += dt;
                else farTime = 0.f;
                if (farTime > 15.f) return fail("The snoop got away with Jaz's phone.");
                break;
            }
            case 3:
                if (grabNear(g, phone, 1.8f)) {
                    goTo(g, cafe.curb, 6.f, "Bring the phone back to ~b~Jaz~s~.", true);
                    sayMe(g, "[calm]Forty thousand unread messages. Wow.");
                    next();
                }
                break;
            case 4:
                if (arrived(g)) {
                    clearGoal(g);
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    vec3 jp = pedPos(g, jaz), mp = playerPos(g);
                    facePed(g, jaz, mp);
                    std::vector<CutsceneShot> shots;
                    establish(g, shots, jp, cafe.yaw, 24.f, 7.f, 3.5f);
                    shots.push_back(shotTwo(jp, mp, 6.f));
                    shots.push_back(shotOver(mp, jp, 6.f));
                    g.mCutscene(shots);
                    jazSay(g, jaz, "[happy]My phone! My life! My forty thousand unread messages!");
                    jazSay(g, jaz, "[calm]Hey. You didn't have to do any of this. So here's the deal.");
                    jazSay(g, jaz, "[happy:0.6]I'm giving every business you own a shout-out. Every single one. Watch the money roll in.");
                    sayMe(g, "[happy:0.4]Does that include the car wash?");
                    jazSay(g, jaz, "[happy]Especially the car wash.");
                    next();
                }
                break;
            case 5:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.notify("JAZ", "Her shout-out is live: every business you own now earns 25 percent more.");
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
                if (t.stageTime > 1.f && pedAlive(g, thief)) {
                    t.teleportNear(pedPos(g, thief).xy(), 10.f);
                    if (scooter >= 0 && g.vehicles[scooter].used) g.vehicles[scooter].sim.body.vel = vec3(0.f);
                }
                break;
            case 2:
                if (t.stageTime > 0.5f && pedAlive(g, thief)) g.peds[thief].health = g.peds[thief].maxHealth * 0.4f;
                break;
            case 3:
                if (phone >= 0 && t.stageTime > 0.5f) t.teleport(g.pickups[phone].pos.toVec3(), 0.f);
                break;
            case 4:
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) {
                    int v = spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f);
                    t.enter(v);
                }
                testGoal(g, t, dt, 60.f);
                break;
            default: break;
        }
    }
};

}  // namespace mu
}  // namespace Game
