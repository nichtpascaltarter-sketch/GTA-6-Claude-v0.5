// Story, Act 3 "The Big Score": Blueprints (planning + approach choice), Dress Rehearsal (prep), Solaris One (the
// heist on Sandoval's supertall, quiet or loud), Overseas (the chase down the Overseas Highway to Key Solano) and
// Signal (the ending choice: broadcast the evidence on Pulse FM or use it as leverage).
#include "missions.h"

namespace Game {
namespace interiors_game {
bool rideElevator(GameWorld& g, int from, bool up);   // interiors_game.cpp (the heist test rides the express elevator)
}

namespace mu {

int heistApproach(GameWorld& g) { return flag(g, EX_HEIST_APPROACH) == 2 ? 2 : 1; }

// ==================================================================================================================
// Act 3-1: "Blueprints" (Dex, with Mari). Scope out Solaris One, photograph the security, choose the approach.
class MissionBlueprints : public StoryMission {
public:
    int mari = -1, rook = -1, kit = -1, jonah = -1;
    std::vector<vec3> spots;
    int shot = 0;
    float still = 0.f;
    const char* title() const override { return "Blueprints"; }
    const char* brief() const override {
        return "The crew is going after Sandoval's vault at the top of Solaris One: the stolen Calle Luna deeds, the Cuervo payroll and "
               "his undeclared cash. First, case the building.";
    }
    long long reward() const override { return 2000; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        score(SC_HEIST, 0.3f, 0);
        vec3 plaza = P.solarisPlaza;
        const vec2 offs[4] = {vec2(-58, -58), vec2(58, -58), vec2(58, 58), vec2(-58, 58)};
        for (vec2 o : offs) {
            vec2 q = plaza.xy() + o;
            spots.push_back(vec3(q, groundAt(g, q.x, q.y, plaza.z + 5.f)));
        }
        mari = spawnPartner(g, 0, placeOffset(g, P.rookShop, 3.f, 1.f), P.rookShop.yaw, WPN_PISTOL);
        buddy = mari;
        if (checkpoint >= 1) {
            placePlayer(g, curbOffset(g, P.solarisOne, -40.f), P.solarisOne.curbYaw);
            beginPhotos(g);
            return;
        }
        // the planning meeting around the car on Rook's lift, when the world has his garage
        InteriorStage in = interiorStage("Rook's Garage");
        vec3 liftL;
        bool inside = in.ok() && in.marker(World::IM_CAR, liftL);
        if (inside) {
            vec3 f = vec3(liftL.x, liftL.y - 4.6f, 0.f);   // the open floor between the roll-up door and the lift
            rook = spawnCast(g, CAST_ROOK, in.at(f + vec3(1.7f, 2.2f, 0.f)), in.yaw(kPi), FAC_FRIEND);
            kit = spawnCast(g, CAST_KIT, in.at(f + vec3(-1.7f, 2.0f, 0.f)), in.yaw(kPi), FAC_FRIEND);
            jonah = spawnCast(g, CAST_JONAH, in.at(f + vec3(2.6f, 0.4f, 0.f)), in.yaw(kPi), FAC_FRIEND);
            placePed(g, mari, in.at(f + vec3(-2.4f, 0.2f, 0.f)), in.yaw(0.f));
            placePlayer(g, in.at(f + vec3(0.2f, -0.9f, 0.f)), in.yaw(0.f));
        } else {
            rook = spawnCast(g, CAST_ROOK, P.rookShop.door, P.rookShop.yaw + kPi, FAC_FRIEND);
            kit = spawnCast(g, CAST_KIT, placeOffset(g, P.rookShop, -2.f, 3.f), P.rookShop.yaw + kPi, FAC_FRIEND);
            jonah = spawnCast(g, CAST_JONAH, placeOffset(g, P.rookShop, 5.f, 3.f), P.rookShop.yaw + kPi, FAC_FRIEND);
            placePlayer(g, placeOffset(g, P.rookShop, 0.f, 0.5f), P.rookShop.yaw);
        }
        provideRide(g, P.rookShop, -14.f);
        vec3 rp = pedPos(g, rook), kp = pedPos(g, kit), mp = pedPos(g, mari), dp = playerPos(g);
        for (int p : {rook, kit, jonah, mari}) facePed(g, p, dp);
        std::vector<CutsceneShot> shots;
        if (inside) shots.push_back(shotRoom(in, vec3(liftL.x + (liftL.x > (in.d->x0 + in.d->x1) * 0.5f ? -5.f : 5.f), 1.1f, 2.6f), rp, kp, 5.5f));   // front corner
        else shots.push_back(shotArc(rp, 9.f, 2.5f, 0.2f, 1.1f, 6.f));
        shots.push_back(shotTwo(kp, dp, 7.f));
        shots.push_back(shotTwo(rp, dp, 6.f, 4.5f, 42.f, -1.f));
        shots.push_back(shotTwo(mp, dp, 6.f));
        shots.push_back(shotMove(P.solarisPlaza + vec3(-120.f, -140.f, 8.f), P.solarisPlaza + vec3(0, 0, 60.f), P.solarisPlaza + vec3(-100.f, -120.f, 6.f),
                                 P.solarisPlaza + vec3(0, 0, 420.f), 7.f, 55.f));
        g.mCutscene(shots);
        say(g, CAST_KIT, kit, "[calm]Solaris One. Ninety floors. Sandoval's private vault sits right under the penthouse.");
        say(g, CAST_KIT, kit, "[calm]The Calle Luna deeds he stole with fake loans. The Cuervo payroll. And about four million in cash he never declared.");
        say(g, CAST_ROOK, rook, "[happy:0.4]Four million. I'm listening.");
        sayMe(g, "We're not doing this for the money, Rook.");
        say(g, CAST_ROOK, rook, "[happy:0.4]Speak for yourself. I've got a retirement to think about.");
        sayP(g, 0, mari, "[calm]The deeds are what matter. Every family he pushed out gets their home back.");
        say(g, CAST_JONAH, jonah, "[calm]Then we plan it right. Nobody goes in blind.");
        say(g, CAST_KIT, kit, "First we need eyes on the building. Dex, Mari, go take some pictures. Be tourists.");
    }

    void beginPhotos(GameWorld& g) {
        shot = 0;
        showSpot(g);
        cp(g, 1);
        setStage(2);
    }

    void showSpot(GameWorld& g) {
        g.mClearMarkers();
        if (shot < (int)spots.size()) {
            g.mMarker(dvec3(spots[shot]), 2.f, vec3(0.3f, 0.8f, 1.f));
            g.mTarget(spots[shot].xy());
            g.mObjective(StrFormat("Photograph Solaris One's security from the ~y~marked spots~s~ (%d/4).", shot));
        }
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, mari, "Mari")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, mari, g.player);
                    for (int p : {rook, kit, jonah})
                        if (p >= 0) setIdle(g, p, 7);
                    goTo(g, gPlaces.solarisOne.curb, 30.f, "Drive to ~y~Solaris One~s~ with Mari.", false, false);
                    next();
                }
                break;
            case 1:
                updateBuddy(g);
                if (stageTime > 6.f && stageTime < 6.1f && g.playerVehicle() >= 0) {
                    sayP(g, 0, mari, "[sad:0.6]My father fixed boats for forty years and never owned more than the yard. Sandoval owns a skyline.");
                    sayMe(g, "[calm]Not for long.");
                }
                if (g.playerAt(gPlaces.solarisPlaza.xy(), 160.f)) beginPhotos(g);
                break;
            case 2: {
                updateBuddy(g);
                if (shot >= (int)spots.size()) break;
                Ped* pl = g.playerPed();
                bool inSpot = pl && pl->state == PS_ONFOOT && ::length(pl->pos.toVec3().xy() - spots[shot].xy()) < 2.2f;
                if (pl && pl->state != PS_ONFOOT && g.playerAt(spots[shot].xy(), 20.f) && g.hudHelpTimer <= 0.f) g.help("Get out and take the photo on foot.", 2.f);
                still = inSpot && ::length(vec2(pl->vel.x, pl->vel.y)) < 0.5f ? still + dt : 0.f;
                if (inSpot) g.help(StrFormat("Hold still... taking the photo  %d%%", (int)(Saturate(still / 1.5f) * 100.f)), 0.3f);
                if (still > 1.5f) {
                    still = 0.f;
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_CAMERA_SHUTTER, 1.f);
#endif
                    static const char* const kLines[4] = {
                        "Loading dock. Roll-up door, two guards, one camera. Rook will love that door.",
                        "Security desk in the lobby. Keycards, turnstiles, and a very bored man named Carl.",
                        "Camera mast on the corner. I can loop that one from the station.",
                        "Service entrance. Maintenance crews come and go all night and nobody checks their faces.",
                    };
                    phoneLine(g, CAST_KIT, kLines[shot]);
                    shot++;
                    if (shot >= (int)spots.size()) {
                        g.mClearMarkers();
                        goTo(g, gPlaces.rookShop.curb, 6.f, "Head back to ~y~Rook's~s~ to plan the job.", true);
                        next();
                    } else {
                        showSpot(g);
                    }
                }
                break;
            }
            case 3:
                updateBuddy(g);
                if (arrived(g)) {
                    clearGoal(g);
                    if (rook < 0) rook = spawnCast(g, CAST_ROOK, gPlaces.rookShop.door, gPlaces.rookShop.yaw + kPi, FAC_FRIEND);
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    if (mari >= 0 && g.peds[mari].vehicle >= 0) g.removePedFromVehicle(mari, false);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(pedPos(g, rook), playerPos(g), 9.f));
                    g.mCutscene(shots);
                    say(g, CAST_ROOK, rook, "[calm]Two ways in. Quiet: we dress you as the maintenance crew and walk in the service door.");
                    say(g, CAST_ROOK, rook, "[calm]Loud: we take an armored truck through the loading dock and blow the vault. Your call, Dex.");
                    next();
                }
                break;
            case 4:
                if (!g.mInCutscene() && !g.mTalking() && !menuIs(MO_CHOICE)) {
                    std::vector<MenuItem> items(2);
                    items[0].label = "The Quiet Way";
                    items[0].detail = "Maintenance crew disguises through the service entrance. Less heat, bigger take, no margin for error.";
                    items[0].id = 1;
                    items[1].label = "The Loud Way";
                    items[1].detail = "Armored truck through the loading dock, charges on the vault, hold the dock. Fast, violent, expensive.";
                    items[1].id = 2;
                    menuOpen(g, MO_CHOICE, "SOLARIS ONE", "Choose the approach", items, true, 0xff30c0ffu);
                    next();
                }
                break;
            case 5:
                if (menuIs(MO_CHOICE) && gMenu.chosen > 0) {
                    int choice = gMenu.chosen;
                    menuClose(g);
                    setFlag(g, EX_HEIST_APPROACH, choice);
                    if (choice == 1) {
                        say(g, CAST_ROOK, rook, "[happy:0.3]Quiet it is. I'll find us a van and some very ugly uniforms.");
                        setOutfitOwned(g, 0, 6);
                        setOutfitOwned(g, 1, 6);
                    } else {
                        say(g, CAST_ROOK, rook, "[happy:0.6]Loud. I was hoping you'd say that. I know an armored truck with a lazy route.");
                    }
                    next();
                }
                break;
            case 6:
                if (!g.mTalking()) {
                    g.storyBriefText = heistApproach(g) == 1 ? "The crew chose the quiet way into Solaris One: maintenance disguises through the service entrance. Mari "
                                                               "needs to steal a Brightline Facilities van."
                                                             : "The crew chose the loud way: an armored truck through Solaris One's loading dock. Mari needs to "
                                                               "hijack the truck on its cash run.";
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
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) t.enter(spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f));
                t.driveToward(gPlaces.solarisPlaza.xy(), 60.f, dt);
                break;
            case 2:
                if (shot < (int)spots.size() && t.stageTime > 0.3f) {
                    t.exitVehicle();
                    if (::length(playerPos(g).xy() - spots[shot].xy()) > 1.f) t.teleport(spots[shot], 0.f);
                }
                break;
            case 3:
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) t.enter(spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f));
                testGoal(g, t, dt, 70.f);
                break;
            case 5:
                if (menuIs(MO_CHOICE) && t.stageTime > 0.5f) {
                    const char* ap = Platform::argValue("approach");
                    gMenu.chosen = ap && strcmp(ap, "loud") == 0 ? 2 : 1;
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 3-2: "Dress Rehearsal" (Mari). Prep for the approach: steal the maintenance van (quiet) or hijack the armored
// truck (loud), lose the police and bring it to Rook.
class MissionDressRehearsal : public StoryMission {
public:
    int prize = -1;      // the van / armored truck
    std::vector<int> guards;
    bool quiet = true;
    bool taken = false;
    const char* title() const override { return "Dress Rehearsal"; }
    const char* brief() const override {
        return "Prepare the Solaris One job: steal what the chosen approach needs and bring it to Rook's salvage yard.";
    }
    long long reward() const override { return 3000; }

    void start(GameWorld& g) override {
        quiet = heistApproach(g) == 1;
        const Places& P = gPlaces;
        score(SC_HEIST, 0.35f, 1);
        if (checkpoint == 0) provideRide(g, P.rookShop, 14.f);
        if (quiet) {
            const Place& D = P.flatsYard;
            int model = pickModel(g, {Vehicles::VC_VAN, Vehicles::VC_SERVICE, Vehicles::VC_PICKUP}, 1);
            prize = spawnCar(g, model, D.curb, D.curbYaw, lin(0.95f, 0.95f, 0.95f));
            if (prize >= 0) g.vehicles[prize].sim.engineOn = false;
            for (int i = 0; i < 2; i++) {
                vec3 p = placeOffset(g, D, -4.f + i * 8.f, 1.f);
                int e = spawnCast(g, CAST_GUARD_A + i, p, D.yaw + kPi * 0.5f, FAC_ENEMY);
                if (e < 0) continue;
                arm(g, e, WPN_PISTOL);
                g.peds[e].brain.accuracy = 0.3f;
                setIdle(g, e, i ? 8 : 7);
                guards.push_back(e);
                enemies.push_back(e);
            }
            phoneLine(g, CAST_ROOK, "[calm]Brightline Facilities keeps its maintenance vans at a depot in the Flats. White van, blue stripe.");
            phoneLine(g, CAST_ROOK, "[calm]Uniforms are in the back. Two guards on the lot, and they will call it in.");
            g.mBlipVehicle(prize, UI::BLIP_VEHICLE);
            g.mTarget(D.curb.xy(), UI::BLIP_VEHICLE);
            g.mObjective("Steal a ~b~Brightline maintenance van~s~ from the depot in the Flats.");
        } else {
            int model = pickModel(g, {Vehicles::VC_TRUCK, Vehicles::VC_VAN, Vehicles::VC_SUV}, 2);
            const Place& A = P.midtownPark;
            std::vector<int> crew;
            prize = spawnCrewCar(g, model, A.curb, A.curbYaw, castChar(g, CAST_GUARD_A), 1, castChar(g, CAST_GUARD_B), FAC_ENEMY, WPN_RIFLE, 0.3f, &crew);
            if (prize >= 0) {
                g.vehicles[prize].color0 = lin(0.25f, 0.27f, 0.22f);
                g.vehicles[prize].sim.health = 1000.f;
                for (int p : crew) {
                    guards.push_back(p);
                    enemies.push_back(p);
                }
                RoutePath path;
                buildRoadPath(g, A.curb.xy(), P.northCity.curb.xy(), path);
                ScriptDriver& d = addDriver(g, prize, path, 12.f);
                d.aggressive = false;
                d.obeyLimits = true;
                g.mBlipVehicle(prize, UI::BLIP_ENEMY);
            }
            placePlayer(g, curbOffset(g, A, -140.f), A.curbYaw, pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SUV}, 0));
            phoneLine(g, CAST_ROOK, "[calm]Armored truck, leaving Canvas Park on its cash run. Two guards with rifles. Stop it, drag them out, take it.");
            g.mObjective("Stop the ~r~armored truck~s~. Ram it until it can't drive.");
        }
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (vehicleLost(g, prize, quiet ? "van" : "armored truck")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (quiet) {
                    if (g.playerAt(vehPos(g, prize).xy(), 30.f)) {
                        for (int e : guards) setCombat(g, e, g.player, 0.3f);
                        blipEnemies(g);
                    }
                    if (g.playerInVehicle(prize)) {
                        taken = true;
                        setWanted(g, 2);
                        sayMe(g, "[shout:0.4]Got the van. Now to lose the tail.");
                        g.mClearBlips();
                        g.mClearTarget();
                        g.mObjective("Lose the ~b~police~s~.");
                        score(SC_CHASE, 0.8f, 1);
                        cp(g, 1);
                        setStage(3);
                    }
                } else {
                    ScriptDriver* d = driverFor(prize);
                    if (d && d->done) return fail("The armored truck finished its run.");
                    bool stopped = g.vehicles[prize].sim.health < 550.f || vehicleDisabled(g, prize) || !pedAlive(g, g.driverOf(prize));
                    if (stageTime > 8.f && stageTime < 8.1f) sayMe(g, "[happy:0.4]Big, slow and heavy. Just like my ex.");
                    if (stopped) {
                        releaseDriver(g, prize);
                        for (int p : guards)
                            if (pedAlive(g, p)) {
                                if (g.peds[p].vehicle >= 0) g.removePedFromVehicle(p, true);
                                setCombat(g, p, g.player, 0.3f);
                            }
                        g.mClearBlips();
                        blipEnemies(g);
                        g.mObjective("Take out the ~r~guards~s~.");
                        score(SC_CHASE, 0.9f, 1);
                        next();
                    }
                }
                break;
            case 1:
                if (aliveEnemies(g) == 0) {
                    g.mBlipVehicle(prize, UI::BLIP_VEHICLE);
                    g.mObjective("Get in the ~b~armored truck~s~.");
                    next();
                }
                break;
            case 2:
                if (g.playerInVehicle(prize)) {
                    taken = true;
                    g.mClearBlips();
                    setWanted(g, 3);
                    g.mObjective("Lose the ~b~police~s~.");
                    cp(g, 1);
                    next();
                }
                break;
            case 3:
                if (abandoned(g, prize, 150.f, quiet ? "van" : "truck")) return MS_FAILED;
                if (g.pinfo.wanted == 0) {
                    score(SC_HEIST, 0.4f, 1);
                    goTo(g, gPlaces.rookShop.curb, 6.f, quiet ? "Bring the van to ~y~Rook's~s~." : "Bring the truck to ~y~Rook's~s~.", true);
                    next();
                }
                break;
            case 4:
                if (abandoned(g, prize, 150.f, quiet ? "van" : "truck")) return MS_FAILED;
                if (g.pinfo.wanted > 0) {
                    g.mObjective("Lose the ~b~police~s~.");
                    break;
                }
                g.mObjective(quiet ? "Bring the van to ~y~Rook's~s~." : "Bring the truck to ~y~Rook's~s~.");
                if (arrived(g) && g.playerInVehicle(prize)) {
                    clearGoal(g);
                    phoneLine(g, CAST_ROOK, quiet ? "[happy:0.5]Beautiful. Four uniforms, one van, zero dignity. We go tomorrow night."
                                                  : "Now that's a truck. Jonah is welding plates on it as we speak. We go tomorrow.");
                    if (quiet) {
                        setOutfitOwned(g, 0, 6);
                        setOutfitOwned(g, 1, 6);
                    }
                    next();
                }
                break;
            case 5:
                if (!g.mTalking()) {
                    g.storyBriefText = "Everything is ready for Solaris One. Dex leads the crew in; Mari has his back.";
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
                if (quiet && t.stageTime > 0.5f) t.enter(prize);
                if (!quiet && t.stageTime > 1.f) g.damageVehicle(prize, 600.f, g.player, vec3(0), vec3(0));
                break;
            case 1: if (t.stageTime > 1.f) t.killEnemies(); break;
            case 2: if (t.stageTime > 0.5f) t.enter(prize); break;
            case 3:
                if (t.stageTime > 2.f) {
                    g.pinfo.wanted = 0;
                    g.pinfo.wantedHeat = 0.f;
                }
                break;
            case 4: testGoal(g, t, dt, 60.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 3-3: "Solaris One" (Dex, with Mari). The heist, quiet or loud, then the escape across downtown.
class MissionSolarisOne : public StoryMission {
public:
    int mari = -1, vehicle = -1, getaway = -1;
    bool quiet = true;
    float holdTimer = 0.f;
    int wave = 0;
    long long take = 0;
    int savedOutfit[2] = {0, 0};
    // the quiet way in, when the world has Solaris One's lobby and penthouse: the express elevator to Sandoval's vault
    bool tower = false;
    int lobbyDef = -1, topDef = -1;
    vec3 liftDown, liftUp, vaultAt, vaultDoor, topEntry;
    float crackT = 0.f;
    const char* title() const override { return "Solaris One"; }
    const char* brief() const override {
        return "Take Sandoval's vault at the top of Solaris One: the stolen deeds, the Cuervo payroll and his cash. Then get out of downtown "
               "alive.";
    }
    long long reward() const override { return take; }

    void securityWave(GameWorld& g, int n, float accuracy) {
        vec3 plaza = gPlaces.solarisPlaza;
        for (int i = 0; i < n; i++) {
            float a = (float)i / n * kTwoPi + wave * 0.7f;
            vec3 p = plaza + vec3(cosf(a) * 30.f, sinf(a) * 30.f, 0.f);
            p.z = groundAt(g, p.x, p.y, plaza.z + 4.f);
            gunman(g, i & 1 ? CAST_GUARD_B : CAST_GUARD_A, p, 0.f, i % 3 == 0 ? WPN_RIFLE : WPN_SMG, accuracy);
        }
        wave++;
        blipEnemies(g);
    }

    bool towerReady() {
        if (!World::gInteriors) return false;
        lobbyDef = World::gInteriors->byName("Solaris One");
        topDef = World::gInteriors->byName("Solaris One Penthouse");
        if (lobbyDef < 0 || topDef < 0) return false;
        const World::InteriorDef& lo = World::gInteriors->defs[lobbyDef];
        const World::InteriorDef& ph = World::gInteriors->defs[topDef];
        const World::InteriorMarker* a = lo.marker(World::IM_ELEVATOR);
        const World::InteriorMarker* b = ph.marker(World::IM_ELEVATOR_TOP);
        const World::InteriorMarker* e = ph.marker(World::IM_ENTRY);
        if (!a || !b || lo.link != topDef) return false;
        liftDown = lo.toWorld(a->pos);
        liftUp = ph.toWorld(b->pos);
        topEntry = e ? ph.toWorld(e->pos) : liftUp;
        // the round vault door stands on the penthouse's west pier (x = -0.7 of the half width, y = 2.9 in its frame)
        const World::InteriorMarker* v = ph.marker(World::IM_COUNTER);
        vaultDoor = ph.toWorld(vec3(-0.7f * ph.x1, 2.9f, 1.3f));
        vaultAt = ph.toWorld(v ? v->pos : vec3(-0.7f * ph.x1 + 1.3f, 2.9f, 0.f));
        return true;
    }

    // the penthouse view: the city through the glass 456 m up, then the two of them
    void topCutscene(GameWorld& g) {
        vec3 dp = playerPos(g), mp = pedPos(g, mari);
        vec2 out = normalize(gPlaces.mariApt.pos.xy() - dp.xy() + vec2(0.01f, 0.f));
        std::vector<CutsceneShot> shots;
        // out over the city lights, a few degrees below the horizon (steeper only shows the floor by the glass)
        shots.push_back(shotMove(dp + vec3(-out * 1.5f, 1.7f), dp + vec3(out * 500.f, -60.f), dp + vec3(out * 0.5f, 1.6f), dp + vec3(out * 500.f, -85.f),
                                 6.f, 55.f));
        shots.push_back(shotTwo(dp, mp, 6.f));
        g.mCutscene(shots);
        sayP(g, 0, mari, "[whisper:0.4]Look at this. He can see all of Calle Luna from up here.");
        sayMe(g, "[whisper:0.4]And tonight Calle Luna can see him. The vault's on the west side.");
    }

    // the guards come up in the elevator
    void topGuards(GameWorld& g) {
        for (int i = 0; i < 4; i++) {
            vec3 p = topEntry + vec3((i % 2) * 1.4f - 0.7f, (i / 2) * 1.4f - 0.7f, 0.2f);
            gunman(g, i & 1 ? CAST_GUARD_B : CAST_GUARD_A, p, 0.f, i == 0 ? WPN_RIFLE : WPN_SMG, 0.24f);
        }
        blipEnemies(g);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        quiet = heistApproach(g) == 1;
        tower = quiet && towerReady();
        const Places& P = gPlaces;
        savedOutfit[0] = currentOutfit(g, 0);
        savedOutfit[1] = currentOutfit(g, 1);
        if (quiet) {
            wearOutfit(g, 0, 6);
            wearOutfit(g, 1, 6);
        }
        score(SC_HEIST, 0.4f, 2);
        mari = spawnPartner(g, 0, placeOffset(g, P.rookShop, 2.f, 1.f), P.rookShop.yaw, WPN_RIFLE);
        buddy = mari;
        int model = quiet ? pickModel(g, {Vehicles::VC_VAN, Vehicles::VC_SERVICE, Vehicles::VC_PICKUP}, 1)
                          : pickModel(g, {Vehicles::VC_TRUCK, Vehicles::VC_VAN, Vehicles::VC_SUV}, 2);
        getaway = spawnCar(g, pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_SEDAN}, 1), curbOffset(g, P.solarisOne, 75.f), P.solarisOne.curbYaw,
                           lin(0.08f, 0.08f, 0.1f));
        if (checkpoint >= 2) {
            // loud: escape with the money
            vehicle = placePlayer(g, curbOffset(g, P.solarisOne, 10.f), P.solarisOne.curbYaw, model, lin(0.25f, 0.27f, 0.22f));
            if (mari >= 0 && vehicle >= 0) g.warpPedIntoVehicle(mari, vehicle, 1);
            beginEscape(g);
            return;
        }
        if (checkpoint >= 1) {
            placePlayer(g, curbOffset(g, P.solarisOne, 0.f), P.solarisOne.yaw);
            vehicle = spawnCar(g, model, curbOffset(g, P.solarisOne, -12.f), P.solarisOne.curbYaw, quiet ? lin(0.95f, 0.95f, 0.95f) : lin(0.25f, 0.27f, 0.22f));
            placePed(g, mari, placeOffset(g, P.solarisOne, 2.f, 1.f), P.solarisOne.yaw);
            beginFight(g);
            return;
        }
        vehicle = spawnCar(g, model, curbOffset(g, P.rookShop, 8.f), P.rookShop.curbYaw, quiet ? lin(0.95f, 0.95f, 0.95f) : lin(0.25f, 0.27f, 0.22f));
        // the send-off inside Rook's garage when the world has it, else at his door
        InteriorStage in = interiorStage("Rook's Garage");
        vec3 liftL;
        bool inside = in.ok() && in.marker(World::IM_CAR, liftL);
        int rook;
        if (inside) {
            vec3 f = vec3(liftL.x, liftL.y - 4.2f, 0.f);
            rook = spawnCast(g, CAST_ROOK, in.at(f + vec3(0.9f, 1.9f, 0.f)), in.yaw(kPi), FAC_FRIEND);
            placePed(g, mari, in.at(f + vec3(-1.6f, 0.1f, 0.f)), in.yaw(0.f));
            placePlayer(g, in.at(f + vec3(0.4f, -0.6f, 0.f)), in.yaw(0.f));
        } else {
            placePlayer(g, placeOffset(g, P.rookShop, 0.f, 0.5f), P.rookShop.yaw);
            rook = spawnCast(g, CAST_ROOK, P.rookShop.door, P.rookShop.yaw + kPi, FAC_FRIEND);
        }
        vec3 rp = pedPos(g, rook), dp = playerPos(g), mp = pedPos(g, mari);
        facePed(g, rook, dp);
        facePed(g, mari, rp);
        std::vector<CutsceneShot> shots;
        if (inside) shots.push_back(shotRoom(in, vec3(liftL.x + (liftL.x > (in.d->x0 + in.d->x1) * 0.5f ? -4.5f : 4.5f), 1.1f, 2.4f), rp, dp, 4.5f));   // front corner
        else establish(g, shots, rp, P.rookShop.yaw, 34.f, 14.f, 4.f);
        shots.push_back(shotTwo(rp, dp, 7.f));
        shots.push_back(shotTwo(mp, dp, 6.f, 4.f, 42.f, -1.f));
        if (vehicle >= 0) shots.push_back(shotVehicle(g, vehicle, 5.f));
        g.mCutscene(shots);
        if (quiet) {
            say(g, CAST_ROOK, rook, "[happy:0.5]Look at you two. The cleanest window washers in Porto Sol.");
            phoneLine(g, CAST_KIT, "[calm]I'll loop the cameras from the station. From the service door you'll have twenty minutes, not a second more.");
        } else {
            say(g, CAST_ROOK, rook, "[calm]Charges are set to cut the vault door, not the building. Try not to crash before you get there.");
            phoneLine(g, CAST_KIT, "[scared:0.3]Police response to the tower is four minutes. Holt will send everyone she has. Make it quick.");
        }
        sayP(g, 0, mari, "[calm]For Calle Luna.");
        sayMe(g, "[calm]For Calle Luna. Let's go.");
    }

    void beginFight(GameWorld& g) {
        g.mClearTarget();
        g.mClearMarkers();
        setFollow(g, mari, g.player);
        if (quiet) {
            securityWave(g, 6, 0.28f);
            g.mBlipVehicle(getaway, UI::BLIP_VEHICLE);
            g.mObjective("Fight your way to the ~b~getaway car~s~.");
        } else {
            holdTimer = 60.f;
            g.missionTimerHud = holdTimer;
            securityWave(g, 4, 0.25f);
            g.mObjective("Hold the ~y~loading dock~s~ while the charges cut the vault.");
        }
        score(SC_CHASE, 1.f, 2);
        cp(g, 1);
        setStage(4);
    }

    void beginEscape(GameWorld& g) {
        g.mClearBlips();
        g.mClearMarkers();
        if (quiet) {
            int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 4);
            for (int k = 0; k < 2; k++) {
                float yaw;
                vec3 sp = approachSpot(g, gPlaces.solarisOne, k ? -160.f : 170.f, &yaw);
                std::vector<int> crew;
                int v = spawnCrewCar(g, model, sp, yaw, castChar(g, CAST_GUARD_A), 2, castChar(g, CAST_GUARD_B),
                                     FAC_ENEMY, WPN_SMG, 0.2f, &crew);
                for (int p : crew) {
                    setCombat(g, p, g.player, 0.2f);
                    enemies.push_back(p);
                }
                if (v >= 0) enemyCars.push_back(v);
            }
            g.mObjective("Lose ~r~Sandoval's security~s~.");
        } else {
            setWanted(g, 4);
            g.mObjective("Lose the ~b~police~s~.");
        }
        score(SC_CHASE, 1.f, 2);
        cp(g, 2);
        setStage(6);
    }

    void finish(GameWorld& g, bool passed) override {
        if (quiet) {
            wearOutfit(g, 0, savedOutfit[0]);
            wearOutfit(g, 1, savedOutfit[1]);
        }
        if (passed) setFlag(g, EX_HEIST_TAKE, (int)(take / 1000));
        g.missionTimerHud = -1.f;
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, mari, "Mari")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, mari, g.player);
                    g.mBlipVehicle(vehicle, UI::BLIP_VEHICLE);
                    goTo(g, gPlaces.solarisOne.curb, 5.f, quiet ? "Drive the van to the ~y~service entrance~s~ of Solaris One." : "Drive the truck into the ~y~loading dock~s~.",
                         true);
                    next();
                }
                break;
            case 1:
                if (vehicleLost(g, vehicle, quiet ? "van" : "truck")) return MS_FAILED;
                if (abandoned(g, vehicle, 150.f, quiet ? "van" : "truck")) return MS_FAILED;
                updateBuddy(g);
                if (quiet && g.pinfo.wanted > 0) {
                    g.mObjective("Lose the ~b~police~s~ before going in.");
                    break;
                }
                if (stageTime > 8.f && stageTime < 8.1f) sayP(g, 0, mari, quiet ? "[whisper:0.4]Drive like a tired janitor. Slow and sad." : "Seatbelt. Now.");
                if (arrived(g) && g.playerInVehicle(vehicle)) {
                    clearGoal(g);
                    vec3 plaza = gPlaces.solarisPlaza;
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotMove(plaza + vec3(-60.f, -70.f, 2.f), plaza + vec3(0, 0, 30.f), plaza + vec3(-50.f, -60.f, 2.f), plaza + vec3(0, 0, 480.f), 7.f, 60.f));
                    if (!quiet) {
                        vec3 dock = gPlaces.solarisOne.door;
                        g.explode(dvec3(dock + vec3(gPlaces.solarisOne.outward * 6.f, 1.f)), 4.f, 30.f, -1);
                        shots.push_back(shot(dock + vec3(gPlaces.solarisOne.streetDir * 14.f, 3.f), dock, 4.f, 45.f));
                    }
                    g.mCutscene(shots);
                    if (tower) {
                        radioLine(g, CAST_KIT, "[whisper:0.5]Cameras looping. The express elevator to the top is yours.");
                        sayP(g, 0, mari, "[whisper:0.4]Twenty minutes. Walk like you belong here.");
                    } else if (quiet) {
                        radioLine(g, CAST_KIT, "[whisper:0.5]Cameras looping. Elevator to ninety is yours.");
                        sayP(g, 0, mari, "[whisper:0.4]Twenty minutes. See you at the top.");
                        radioLine(g, CAST_KIT, "[scared:0.6]Vault's open. Deeds, ledgers, cash, you're golden... wait. Sandoval's head of security just walked into the camera room.");
                        radioLine(g, CAST_KIT, "[shout]He's seen the loop. The lobby is filling up. Get out, now!");
                    } else {
                        sayMe(g, "[calm]Knock, knock.");
                        radioLine(g, CAST_ROOK, "[shout]Charges are cutting. Sixty seconds. Hold that dock!");
                    }
                    next();
                }
                break;
            case 2:
                if (!g.mInCutscene() && !g.mTalking() && tower) {
                    // on foot through the lobby to the express elevator
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    if (mari >= 0 && g.peds[mari].vehicle >= 0) g.removePedFromVehicle(mari, false);
                    setFollow(g, mari, g.player);
                    goTo(g, liftDown, 1.f, "Walk through the ~y~lobby~s~ and take the express elevator to the top.");
                    setStage(20);
                    break;
                }
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (quiet) {
                        // they came back down to the plaza with the loot
                        if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                        if (mari >= 0 && g.peds[mari].vehicle >= 0) g.removePedFromVehicle(mari, false);
                        vec3 exitSpot = gPlaces.solarisPlaza + vec3(0.f, -36.f, 0.f);
                        placePlayer(g, exitSpot, 0.f);
                        placePed(g, mari, exitSpot + vec3(1.5f, 0.5f, 0.f), 0.f);
                        g.bigMessage("20 MINUTES LATER", "", 0xffffffffu);
                    } else {
                        if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                        if (mari >= 0 && g.peds[mari].vehicle >= 0) g.removePedFromVehicle(mari, false);
                    }
                    beginFight(g);
                }
                break;
            case 4:
                updateBuddy(g, 45.f);
                if (quiet) {
                    if (aliveEnemies(g) <= 2 && wave < 3 && stageTime > 5.f) securityWave(g, 4, 0.28f);
                    bool together = g.playerVehicle() >= 0 && pedAlive(g, mari) && g.peds[mari].vehicle == g.playerVehicle();
                    if (g.playerInVehicle(getaway) && !together && g.hudHelpTimer <= 0.f) g.help("Wait for ~b~Mari~s~.", 1.5f);
                    if (together) {
                        take = 220000;
                        beginEscape(g);
                    }
                } else {
                    holdTimer -= dt;
                    g.missionTimerHud = Max(0.f, holdTimer);
                    if (aliveEnemies(g) <= 1 && wave < 4) securityWave(g, 4 + wave, 0.25f + wave * 0.03f);
                    if (holdTimer <= 0.f) {
                        g.missionTimerHud = -1.f;
                        radioLine(g, CAST_ROOK, "[shout]Vault's open! Load the cash into the truck and go!");
                        vec3 back = vehPos(g, vehicle) - vec3(g.vehicles[vehicle].sim.forward().xy() * 5.f, 0.f);
                        goTo(g, back, 2.f, "Load the loot into the ~b~truck~s~.");
                        next();
                    }
                }
                break;
            case 5:
                updateBuddy(g, 45.f);
                if (arrived(g)) {
                    clearGoal(g);
                    take = 180000 + (long long)(Saturate(g.vehicles[vehicle].sim.health / 1000.f) * 20000.f);
                    sayP(g, 0, mari, "[shout]Deeds are in the bag. Drive!");
                    g.mBlipVehicle(vehicle, UI::BLIP_VEHICLE);
                    g.mObjective("Get in the ~b~truck~s~.");
                    setStage(51);
                }
                break;
            case 51:
                updateBuddy(g, 45.f);
                if (g.playerInVehicle(vehicle)) {
                    if (mari >= 0 && g.peds[mari].vehicle != vehicle) {
                        int seat = g.freeSeat(vehicle, false);
                        if (seat > 0) g.warpPedIntoVehicle(mari, vehicle, seat);
                    }
                    beginEscape(g);
                }
                break;
            case 6: {
                updateBuddy(g);
                bool done;
                if (quiet) {
                    done = true;
                    for (int v : enemyCars)
                        if (vehicleAlive(g, v) && ::length(vehPos(g, v) - playerPos(g)) < 350.f && !vehicleDisabled(g, v)) done = false;
                    for (int e : enemies)
                        if (pedAlive(g, e) && ::length(pedPos(g, e) - playerPos(g)) < 120.f) done = false;
                    if (g.playerAt(gPlaces.solarisPlaza.xy(), 300.f)) done = false;
                } else {
                    done = g.pinfo.wanted == 0;
                }
                if (stageTime > 10.f && stageTime < 10.1f) sayP(g, 0, mari, "[happy:0.6]I'm holding every deed in Calle Luna in a gym bag.");
                if (done) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    score(SC_HEIST, 0.5f, 2);
                    goTo(g, gPlaces.redland.curb, 6.f, "Lie low at the ~y~safehouse in Redland~s~.", true);
                    next();
                }
                break;
            }
            case 20:   // riding up (the interiors' elevator: E in the cab)
                if (::length(playerPos(g) - liftUp) < 8.f) {
                    clearGoal(g);
                    placePed(g, mari, topEntry + vec3(0.8f, 0.8f, 0.f), 0.f);   // she rode up with him
                    facePed(g, mari, playerPos(g));
                    score(SC_NOIR, 0.35f, 2);
                    topCutscene(g);
                    next();
                }
                break;
            case 21:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, mari, g.player);
                    goTo(g, vaultAt, 1.4f, "Crack ~y~Sandoval's vault~s~.");
                    next();
                }
                break;
            case 22:
                if (arrived(g)) {
                    clearGoal(g);
                    Ped* pl = g.playerPed();
                    if (pl) {
                        vec2 d = vaultDoor.xy() - playerPos(g).xy();
                        pl->yaw = atan2f(-d.x, d.y);
                    }
                    radioLine(g, CAST_KIT, "[whisper:0.5]Okay. Twelve digits, and the wheel is noisy. Listen for the clicks.");
                    crackT = 0.f;
                    next();
                }
                break;
            case 23: {
                crackT += dt;
                g.missionCounterLabel = "VAULT";
                g.missionCounter = Min(100, (int)(crackT * 10.f));
                g.missionCounterMax = 100;
                if (crackT > 3.5f && crackT - dt <= 3.5f) sayP(g, 0, mari, "[whisper:0.4]Four. Nine. Come on, come on.");
                if (crackT > 7.f && crackT - dt <= 7.f) radioLine(g, CAST_KIT, "[whisper:0.4]Last wheel. Gently.");
                if (crackT >= 10.f) {
                    g.missionCounterLabel.clear();
                    take = 220000;
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_PURCHASE, 0.7f);
#endif
                    sayP(g, 0, mari, "[whisper][happy:0.4]It's open. The deeds. The ledgers. And that is a lot of cash.");
                    radioLine(g, CAST_KIT, "[scared:0.6]Guys. Sandoval's head of security just walked into the camera room. He's seen the loop.");
                    radioLine(g, CAST_KIT, "[shout]The elevator is coming up. Four of them!");
                    topGuards(g);
                    g.mObjective("Take out ~r~security~s~.");
                    score(SC_CHASE, 1.f, 2);
                    next();
                }
                break;
            }
            case 24:
                updateBuddy(g, 30.f);
                if (aliveEnemies(g) == 0) {
                    g.mClearBlips();
                    sayMe(g, "[shout:0.5]Elevator. Now. Before they send more.");
                    goTo(g, liftUp, 1.f, "Take the ~y~elevator~s~ back down.");
                    next();
                }
                break;
            case 25:   // riding down
                if (::length(playerPos(g) - liftDown) < 10.f) {
                    clearGoal(g);
                    placePed(g, mari, liftDown + vec3(0.8f, -0.8f, 0.f), 0.f);
                    radioLine(g, CAST_KIT, "[shout]The lobby is filling up. Out the front, the car is waiting!");
                    beginFight(g);
                }
                break;
            case 7:
                updateBuddy(g);
                if (!quiet && g.pinfo.wanted > 0) {
                    g.mObjective("Lose the ~b~police~s~.");
                    break;
                }
                if (arrived(g)) {
                    clearGoal(g);
                    sayMe(g, "[happy:0.5]Count it later. We got the deeds.");
                    sayP(g, 0, mari, "[happy]We did it, Dex. We actually did it.");
                    g.storyBriefText = "The crew cleaned out Sandoval's vault: the stolen Calle Luna deeds, the Cuervo payroll and his cash. "
                                       "Sandoval knows exactly who did it.";
                    next();
                }
                break;
            case 8:
                if (!g.mTalking()) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.3f && vehicle >= 0 && g.playerVehicle() != vehicle) {
                    t.enter(vehicle);
                    if (mari >= 0) g.warpPedIntoVehicle(mari, vehicle, 1);
                }
                // the scripted drive hops through traffic: keep the van in one piece (the test checks the heist, not the driving)
                if (vehicleAlive(g, vehicle)) {
                    g.vehicles[vehicle].sim.health = Max(g.vehicles[vehicle].sim.health, 700.f);
                    g.vehicles[vehicle].sim.engineHealth = Max(g.vehicles[vehicle].sim.engineHealth, 700.f);
                }
                testGoal(g, t, dt, 70.f);
                break;
            case 4:
                if (quiet) {
                    if (t.stageTime > 1.5f) t.killEnemies();
                    if (t.stageTime > 2.5f && getaway >= 0) {
                        t.enter(getaway);
                        if (mari >= 0) g.warpPedIntoVehicle(mari, getaway, 1);
                    }
                } else {
                    holdTimer -= dt * 4.f;
                    if (fmodf(t.stageTime, 1.5f) < dt) t.killEnemies();
                }
                break;
            case 5: if (t.stageTime > 0.5f) t.teleport(goal, 0.f); break;
            case 51: if (t.stageTime > 0.5f) t.enter(vehicle); break;
            // the tower: into the cab and up / down with the interiors' elevator ride
            case 20:
                if (t.stageTime > 0.5f && t.stageTime - dt <= 0.5f) {
                    t.teleport(liftDown, 0.f);
                    interiors_game::rideElevator(g, lobbyDef, true);
                }
                break;
            case 22: if (t.stageTime > 0.5f) t.teleport(vaultAt, 0.f); break;
            case 24: if (t.stageTime > 1.f) t.killEnemies(); break;
            case 25:
                if (t.stageTime > 0.5f && t.stageTime - dt <= 0.5f) {
                    t.teleport(liftUp, 0.f);
                    interiors_game::rideElevator(g, topDef, false);
                }
                break;
            case 6:
                if (t.stageTime > 1.f) {
                    t.killEnemies();
                    for (int v : enemyCars) t.destroy(v);
                    g.pinfo.wanted = 0;
                    g.pinfo.wantedHeat = 0.f;
                    t.driveToward(gPlaces.redland.curb.xy(), 80.f, dt);
                }
                break;
            case 7: testGoal(g, t, dt, 80.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 3-4: "Overseas" (Mari, with Dex). Sandoval takes Tomas and runs for Key Solano with Holt's escort. Chase the
// convoy down the Overseas Highway, storm the marina and stop Sandoval's boat.
class MissionOverseas : public StoryMission {
public:
    int dex = -1, car = -1, suv = -1, sandoval = -1, tomas = -1, holt = -1, escapeBoat = -1;
    std::vector<int> escorts;
    int escortsDown = 0;
    vec3 marina;
    const char* title() const override { return "Overseas"; }
    const char* brief() const override {
        return "Sandoval took Tomas and is running south to Key Solano with Holt's officers as escort. Catch the convoy on the Overseas "
               "Highway and end this.";
    }
    long long reward() const override { return 50000; }

    void spawnConvoy(GameWorld& g, float ahead) {
        const Places& P = gPlaces;
        RoutePath path;
        vec2 from = P.overseasStart.curb.xy();
        buildRoadPath(g, from, P.keySolano.curb.xy(), path, 1.f);
        if (path.pts.size() < 2) return;
        int suvModel = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_SEDAN}, 2);
        int escModel = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_PICKUP}, 1);
        int copModel = pickModel(g, {Vehicles::VC_POLICE, Vehicles::VC_SEDAN}, 0);
        float s0 = ahead;
        // lead: Sandoval's SUV; then two escorts and Holt's cruiser
        for (int i = 0; i < 4; i++) {
            float s = s0 + 45.f - i * 22.f;
            vec2 tan;
            vec3 p = path.at(s, nullptr, &tan);
            float yaw = atan2f(-tan.x, tan.y);
            int model = i == 0 ? suvModel : (i == 3 ? copModel : escModel);
            std::vector<int> crew;
            int v;
            if (i == 0) {
                v = spawnCar(g, model, p, yaw, lin(0.03f, 0.03f, 0.035f));
                sandoval = spawnCast(g, CAST_GUARD_A, p, yaw, FAC_ENEMY);
                if (sandoval >= 0 && v >= 0) {
                    g.warpPedIntoVehicle(sandoval, v, 0);
                    g.peds[sandoval].invincible = true;
                }
                suv = v;
            } else {
                v = spawnCrewCar(g, model, p, yaw, castChar(g, i == 3 ? CAST_COP_A : CAST_GUARD_B), 1, castChar(g, i == 3 ? CAST_COP_B : CAST_THUG_B),
                                 FAC_ENEMY, i == 3 ? WPN_SHOTGUN : WPN_SMG, 0.2f, &crew);
                for (int c = 0; c < (int)crew.size(); c++) {
                    if (c > 0) setCombat(g, crew[c], g.player, 0.2f);
                    enemies.push_back(crew[c]);
                }
                if (v >= 0) {
                    escorts.push_back(v);
                    if (i == 3) g.vehicles[v].sirenOn = true;
                }
            }
            if (v < 0) continue;
            ScriptDriver& d = addDriver(g, v, path, i == 0 ? 30.f : 29.f);
            d.aggressive = true;
            d.along = s;
            d.rubberPed = g.player;
            d.rubberGap = i == 0 ? 90.f : 50.f;
            g.mBlipVehicle(v, UI::BLIP_ENEMY);
        }
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        marina = P.keySolanoDock;
        score(SC_NOIR, 0.4f, 3);
        int model = pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_MUSCLE}, 0);
        if (checkpoint >= 2) {
            car = placePlayer(g, curbOffset(g, P.keySolano, -60.f), P.keySolano.curbYaw, model, lin(0.85f, 0.2f, 0.45f));
            dex = spawnPartner(g, 1, playerPos(g), 0.f, WPN_RIFLE);
            if (dex >= 0 && car >= 0) g.warpPedIntoVehicle(dex, car, 1);
            buddy = dex;
            beginMarina(g);
            return;
        }
        if (checkpoint >= 1) {
            car = placePlayer(g, P.overseasStart.curb, P.overseasStart.curbYaw, model, lin(0.85f, 0.2f, 0.45f));
            dex = spawnPartner(g, 1, playerPos(g), 0.f, WPN_RIFLE);
            if (dex >= 0 && car >= 0) g.warpPedIntoVehicle(dex, car, 1);
            buddy = dex;
            spawnConvoy(g, 120.f);
            beginEscorts(g);
            return;
        }
        car = spawnCar(g, model, curbOffset(g, P.redland, 8.f), P.redland.curbYaw, lin(0.85f, 0.2f, 0.45f));
        dex = spawnPartner(g, 1, placeOffset(g, P.redland, 2.f, 1.f), P.redland.yaw, WPN_RIFLE);
        buddy = dex;
        placePlayer(g, placeOffset(g, P.redland, 0.f, 0.5f), P.redland.yaw);
        vec3 dp = pedPos(g, dex), mp = playerPos(g);
        std::vector<CutsceneShot> shots;
        establish(g, shots, mp, P.redland.yaw, 34.f, 12.f, 4.f);
        shots.push_back(shotTwo(dp, mp, 6.f));
        shots.push_back(shotOver(mp, dp, 7.f));
        shots.push_back(shotOver(dp, mp, 6.f, -1.f));
        g.mCutscene(shots);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PHONE_RING, 0.8f);
#endif
        phoneLine(g, CAST_LUCHA, "[scared]Mija! They took Tomas! Sandoval's men, right out of my kitchen. He says bring everything to Key Solano, or else.");
        sayMe(g, "[calm]We're coming, Lucha. I promise.");
        sayP(g, 1, dex, "[calm]It's a trap, Mari.");
        sayMe(g, "[angry:0.5]Then we spring it.");
        phoneLine(g, CAST_KIT, "[shout:0.4]Sandoval's convoy just got on the Overseas Highway. Four cars. Holt's own cruiser is riding with him.");
    }

    void beginEscorts(GameWorld& g) {
        g.mObjective("Take out ~r~Sandoval's escorts~s~.");
        score(SC_CHASE, 1.f, 3);
        cp(g, 1);
        setStage(3);
    }

    void beginMarina(GameWorld& g) {
        g.mClearBlips();
        releaseDriver(g, suv);
        vec3 m = marina;
        sandoval = spawnCast(g, CAST_SANDOVAL, m + vec3(0.f, 4.f, 0.f), 0.f, FAC_CIVILIAN);
        tomas = spawnCast(g, CAST_TOMAS, m + vec3(3.f, 5.f, 0.f), 0.f, FAC_FRIEND);
        if (sandoval >= 0) g.peds[sandoval].invincible = true;
        if (tomas >= 0) {
            g.peds[tomas].invincible = true;
            setIdle(g, tomas, 5);
        }
        for (int i = 0; i < 7; i++) {
            float a = kTwoPi * i / 7.f;
            vec3 p = m + vec3(cosf(a) * 16.f, sinf(a) * 16.f, 0.f);
            if (g.map->isWater(p.x, p.y)) p = m + vec3(cosf(a) * 6.f, sinf(a) * 6.f, 0.f);
            p.z = groundAt(g, p.x, p.y, m.z + 4.f);
            gunman(g, i % 2 ? CAST_GUARD_B : CAST_GUARD_A, p, 0.f, i % 3 == 0 ? WPN_RIFLE : WPN_SMG, 0.26f);
        }
        goTo(g, m, 20.f, "Take out ~r~Sandoval's men~s~ at the Key Solano marina.", false, false);
        g.mClearTarget();
        blipEnemies(g);
        score(SC_CHASE, 1.f, 3);
        cp(g, 2);
        setStage(6);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, dex, "Dex")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, dex, g.player);
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    goTo(g, gPlaces.overseasStart.curb, 25.f, "Get to the ~y~Overseas Highway~s~.", false, false);
                    next();
                }
                break;
            case 1:
                updateBuddy(g);
                if (g.playerAt(gPlaces.overseasStart.curb.xy(), 500.f) && g.playerVehicle() >= 0) {
                    g.mClearBlips();
                    clearGoal(g);
                    spawnConvoy(g, 450.f);
                    g.mObjective("Catch up with ~r~Sandoval's convoy~s~.");
                    sayP(g, 1, dex, "[shout]There they are. Get me alongside, I'll take care of the escorts.");
                    score(SC_CHASE, 0.85f, 3);
                    next();
                }
                break;
            case 2: {
                updateBuddy(g);
                float d = suv >= 0 ? ::length(vehPos(g, suv) - playerPos(g)) : 0.f;
                for (int v : escorts) d = Min(d, ::length(vehPos(g, v) - playerPos(g)));
                if (d < 160.f) beginEscorts(g);
                if (suv >= 0) {
                    ScriptDriver* sd = driverFor(suv);
                    if (sd && sd->done) {
                        beginMarina(g);
                        break;
                    }
                }
                break;
            }
            case 3: {
                updateBuddy(g, 60.f);
                escortsDown = 0;
                for (int v : escorts) {
                    bool down = vehicleDisabled(g, v) || !pedAlive(g, g.driverOf(v));
                    if (down) {
                        escortsDown++;
                        releaseDriver(g, v);
                    }
                }
                g.missionCounterLabel = "ESCORTS";
                g.missionCounter = escortsDown;
                g.missionCounterMax = (int)escorts.size();
                if (suv >= 0) g.vehicles[suv].sim.health = Max(g.vehicles[suv].sim.health, 900.f);
                if (stageTime > 6.f && stageTime < 6.1f) sayP(g, 1, dex, "[angry:0.6]Holt's cruiser is in there too. No more badges between us and him.");
                if (stageTime > 22.f && stageTime < 22.1f) sayMe(g, "[angry:0.5]Seven miles of bridge and nowhere to hide. Good.");
                ScriptDriver* sd = suv >= 0 ? driverFor(suv) : nullptr;
                if (escortsDown >= (int)escorts.size() || (sd && sd->done)) {
                    g.missionCounterLabel.clear();
                    if (sd) {
                        sd->rubberGap = 0.f;
                        sd->cruise = 34.f;
                    }
                    g.mClearBlips();
                    if (suv >= 0) g.mBlipVehicle(suv, UI::BLIP_ENEMY);
                    g.mObjective("Follow ~r~Sandoval~s~ to Key Solano.");
                    sayP(g, 1, dex, "[shout:0.6]Escorts are done. He's running for Key Solano.");
                    next();
                }
                break;
            }
            case 4: {
                updateBuddy(g);
                if (suv >= 0) g.vehicles[suv].sim.health = Max(g.vehicles[suv].sim.health, 900.f);
                ScriptDriver* sd = suv >= 0 ? driverFor(suv) : nullptr;
                bool there = !sd || sd->done || ::length(vehPos(g, suv) - marina) < 120.f;
                if (there && g.playerAt(marina.xy(), 400.f)) beginMarina(g);
                else if (there && stageTime > 3.f) {
                    goTo(g, marina, 60.f, "Get to the ~y~Key Solano marina~s~.", false, false);
                }
                break;
            }
            case 6:
                updateBuddy(g, 50.f);
                if (aliveEnemies(g) == 0) {
                    // Sandoval runs for his boat
                    vec3 w;
                    bool haveWater = findWater(g, marina.xy(), 2.f, w, 120.f);
                    if (haveWater) {
                        std::vector<vec3> wps = {w};
                        vec3 far1;
                        findWater(g, w.xy() + normalize(w.xy() - marina.xy()) * 300.f, 3.f, far1, 200.f);
                        wps.push_back(far1);
                        vec3 far2;
                        findWater(g, w.xy() + normalize(w.xy() - marina.xy()) * 900.f + vec2(-300.f, -200.f), 3.f, far2, 300.f);
                        wps.push_back(far2);
                        escapeBoat = spawnBoat(g, w, yawTo(w.xy(), far1.xy()), lin(0.95f, 0.95f, 0.95f), 3);
                        if (escapeBoat >= 0) {
                            if (sandoval >= 0) g.warpPedIntoVehicle(sandoval, escapeBoat, 0);
                            RoutePath path;
                            buildWaypointPath(wps, path, 20.f, true, false);
                            ScriptDriver& d = addDriver(g, escapeBoat, path, 16.f, DRV_KINEMATIC);
                            d.kinSpeed = 4.f;
                            g.vehicles[escapeBoat].sim.health = 1000.f;
                            g.mBlipVehicle(escapeBoat, UI::BLIP_ENEMY);
                        }
                    }
                    if (escapeBoat < 0) {
                        // no boat in this build: he runs for his SUV instead
                        if (sandoval >= 0 && suv >= 0 && vehicleAlive(g, suv)) {
                            g.warpPedIntoVehicle(sandoval, suv, 0);
                            ScriptDriver& d = driveRoad(g, suv, gPlaces.raceKeys.curb.xy() + vec2(300.f, 200.f), 26.f, true);
                            d.rubberPed = g.player;
                            d.rubberGap = 60.f;
                            g.vehicles[suv].sim.health = 1000.f;
                            escapeBoat = suv;
                            g.mBlipVehicle(suv, UI::BLIP_ENEMY);
                        }
                    }
                    sayMe(g, "[shout]He's running! Tomas, stay down!");
                    g.mObjective("Stop ~r~Sandoval~s~!");
                    if (tomas >= 0) setIdle(g, tomas, 4);
                    next();
                }
                break;
            case 7: {
                updateBuddy(g);
                if (escapeBoat < 0) {
                    setStage(8);
                    break;
                }
                float d = ::length(vehPos(g, escapeBoat) - playerPos(g));
                if (d > 520.f) return fail("Sandoval escaped.");
                ScriptDriver* sd = driverFor(escapeBoat);
                if (sd && sd->done && d > 300.f) return fail("Sandoval escaped.");
                bool stopped = g.vehicles[escapeBoat].sim.health < 650.f || vehicleDisabled(g, escapeBoat);
                if (stopped) {
                    if (sd) sd->speedScale = 0.f;
                    releaseDriver(g, escapeBoat);
                    setStage(8);
                }
                break;
            }
            case 8: {
                g.mClearBlips();
                g.mObjective("");
                // the end of the road: Sandoval on the dock, Tomas free, Holt arrives
                if (sandoval >= 0) {
                    if (g.peds[sandoval].vehicle >= 0) g.removePedFromVehicle(sandoval, false);
                    placePed(g, sandoval, marina + vec3(0.f, 3.f, 0.f), yawTo(marina.xy(), playerPos(g).xy()));
                    setIdle(g, sandoval, 5);
                }
                if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                placePlayer(g, marina + vec3(-6.f, -4.f, 0.f), yawTo(marina.xy() + vec2(-6.f, -4.f), marina.xy()));
                if (dex >= 0) {
                    if (g.peds[dex].vehicle >= 0) g.removePedFromVehicle(dex, false);
                    placePed(g, dex, marina + vec3(-7.f, -1.f, 0.f), 0.f);
                    setIdle(g, dex, 0);
                    facePed(g, dex, pedPos(g, sandoval));
                }
                if (tomas >= 0) {
                    placePed(g, tomas, marina + vec3(-4.f, -6.f, 0.f), 0.f);
                    setIdle(g, tomas, 0);
                }
                int pc = pickModel(g, {Vehicles::VC_POLICE, Vehicles::VC_SEDAN}, 0);
                vec3 hp = marina + vec3(-30.f, -25.f, 0.f);
                hp.z = groundAt(g, hp.x, hp.y, marina.z + 4.f);
                int hc = spawnCar(g, pc, hp, yawTo(hp.xy(), marina.xy()));
                if (hc >= 0) g.vehicles[hc].sirenOn = true;
                holt = spawnCast(g, CAST_HOLT, hp + vec3(3.f, 2.f, 0.f), yawTo(hp.xy(), marina.xy()), FAC_CIVILIAN);
                if (holt >= 0) g.peds[holt].invincible = true;
                vec3 sp = pedPos(g, sandoval), mp = playerPos(g), hpp = pedPos(g, holt);
                std::vector<CutsceneShot> shots;
                shots.push_back(shotOver(mp, sp, 7.f));
                shots.push_back(shotOver(sp, mp, 7.f, -1.f));
                shots.push_back(shotTwo(pedPos(g, tomas), mp, 5.f));
                shots.push_back(shotTwo(hpp, mp, 7.f));
                shots.push_back(shotOver(mp, hpp, 7.f));
                shots.push_back(shotTwo(sp, hpp, 7.f, 6.f, 45.f, -1.f));
                g.mCutscene(shots);
                say(g, CAST_SANDOVAL, sandoval, "[angry:0.6]You think this changes anything? I own the land, the bank, the police. I own the tide.");
                sayMe(g, "[calm]You owned a gym bag full of stolen deeds. Now I do.");
                say(g, CAST_TOMAS, tomas, "[happy]Mari! You came. You actually came.");
                sayMe(g, "[calm]Always, little brother.");
                say(g, CAST_HOLT, holt, "[megaphone][shout]Porto Sol PD! Nobody move. This is my collar, Ortega. Hand over whatever you took from that tower.");
                sayMe(g, "[angry:0.6]Not to you, Captain. Not ever.");
                say(g, CAST_HOLT, holt, "[calm]Then we'll see whose story this city believes.");
                sayP(g, 1, dex, "[happy:0.4]Funny you say that. We know a DJ.");
                next();
                break;
            }
            case 9:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "Sandoval is finished at Key Solano and Tomas is safe. Holt walked away - for now. Everything the crew "
                                       "took is waiting at Pulse FM. Kit has a decision for Mari.";
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
                if (t.stageTime > 0.3f && g.playerVehicle() != car && car >= 0) {
                    t.enter(car);
                    if (dex >= 0) g.warpPedIntoVehicle(dex, car, 1);
                }
                if (t.stageTime > 0.6f) t.teleport(gPlaces.overseasStart.curb, gPlaces.overseasStart.curbYaw);
                break;
            case 2:
            case 4:
                if (suv >= 0 && t.stageTime > 0.5f) {
                    vec3 sp = vehPos(g, suv);
                    vec3 f = g.vehicles[suv].sim.forward();
                    if (::length(playerPos(g) - sp) > 90.f) t.teleport(sp - f * 60.f, atan2f(-f.x, f.y));
                }
                if (stage == 4 && t.stageTime > 3.f && suv >= 0) {
                    ScriptDriver* sd = driverFor(suv);
                    if (sd) {
                        vec2 tan;
                        vec3 p = sd->path.at(sd->path.length() - 5.f, nullptr, &tan);
                        teleportVehicle(g, suv, p, atan2f(-tan.x, tan.y));
                    }
                    t.teleport(marina + vec3(-40.f, -40.f, 0.f), 0.f);
                }
                break;
            case 3:
                if (t.stageTime > 1.f)
                    for (int v : escorts) t.destroy(v);
                break;
            case 6: if (t.stageTime > 1.5f) t.killEnemies(); break;
            case 7:
                if (escapeBoat >= 0 && t.stageTime > 1.f) g.damageVehicle(escapeBoat, 500.f, g.player, vec3(0), vec3(0));
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 3-5: "Signal" (Mari). The ending. Broadcast everything on Pulse FM (and hold the station while Kit is on air),
// or use it as leverage against Holt (and survive her double-cross on the pier).
class MissionSignal : public StoryMission {
public:
    int kit = -1, dex = -1, rook = -1, lucha = -1, holt = -1;
    int choice = 0;
    float airTime = 0.f;
    int wave = 0;
    int endPhase = 0;
    float endT = 0.f;
    const char* title() const override { return "Signal"; }
    const char* brief() const override {
        return "Everything the crew took is on Kit's desk at Pulse FM. Broadcast it to the whole city, or use it to make Holt pay. Either way, "
               "Calle Luna's story ends tonight.";
    }
    long long reward() const override { return choice == 2 ? 500000 : 25000; }

    void loyalists(GameWorld& g, vec3 at, int n, int target) {
        int model = pickModel(g, {Vehicles::VC_POLICE, Vehicles::VC_SEDAN}, (u32)wave);
        const Place& S = gPlaces.pulseFm;
        for (int k = 0; k < (n + 2) / 3; k++) {
            float along = (k & 1 ? 1.f : -1.f) * (110.f + 30.f * wave);
            float yaw = 0.f;
            vec3 sp = choice == 1 ? approachSpot(g, S, along, &yaw) : at + vec3(-60.f - 20.f * k, 8.f, 0.f);
            sp.z = groundAt(g, sp.x, sp.y, sp.z + 4.f);
            int v = attackCar(g, model, sp, yaw, 3, WPN_RIFLE, 0.25f, (u32)(k + wave));
            if (v >= 0) {
                g.vehicles[v].sirenOn = true;
                g.vehicles[v].color0 = lin(0.1f, 0.12f, 0.22f);
                if (choice == 1) {
                    ScriptDriver& d = driveRoad(g, v, S.curb.xy() + S.streetDir * (along < 0.f ? -15.f : 15.f), 18.f, true);
                    d.stopAtEnd = true;
                } else {
                    for (int s = 0; s < 3; s++) {
                        int p = g.vehicles[v].seats[s];
                        if (p >= 0) {
                            g.removePedFromVehicle(p, false);
                            setCombat(g, p, g.player, 0.25f);
                        }
                    }
                }
                for (int s = 1; s < 3; s++) {
                    int p = g.vehicles[v].seats[s];
                    if (p >= 0 && target >= 0) g.peds[p].brain.target = target;
                }
            }
        }
        wave++;
        blipEnemies(g);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Place& S = gPlaces.pulseFm;
        score(SC_NOIR, 0.3f, 4);
        kit = spawnCast(g, CAST_KIT, S.door, S.yaw + kPi, FAC_FRIEND);
        dex = spawnPartner(g, 1, placeOffset(g, S, 3.f, 1.5f), S.yaw + kPi * 0.5f, WPN_RIFLE);
        buddy = dex;
        if (kit >= 0) g.peds[kit].maxHealth = g.peds[kit].health = 600.f;
        choice = flag(g, EX_ENDING);
        if (checkpoint >= 1 && choice == 1) {
            placePlayer(g, placeOffset(g, S, -2.f, 0.5f), S.yaw);
            beginBroadcast(g);
            return;
        }
        if (checkpoint >= 1 && choice == 2) {
            placePlayer(g, gPlaces.pierEnd - vec3(30.f, 0, 0), -kPi * 0.5f);
            placePed(g, dex, gPlaces.pierEnd - vec3(32.f, 2.f, 0.f), -kPi * 0.5f);
            beginAmbush(g);
            return;
        }
        rook = spawnCast(g, CAST_ROOK, placeOffset(g, S, -4.f, 2.f), S.yaw + kPi, FAC_FRIEND);
        lucha = spawnCast(g, CAST_LUCHA, placeOffset(g, S, 1.f, 3.f), S.yaw + kPi, FAC_FRIEND);
        if (lucha >= 0) g.peds[lucha].invincible = true;
        placePlayer(g, placeOffset(g, S, -1.f, 0.f), S.yaw);
        vec3 kp = pedPos(g, kit), mp = playerPos(g), rp = pedPos(g, rook);
        for (int p : {kit, rook, lucha, dex}) facePed(g, p, mp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, kp, S.yaw, 45.f, 25.f, 5.f);
        shots.push_back(shotTwo(kp, mp, 7.f));
        shots.push_back(shotTwo(rp, mp, 7.f, 4.5f, 42.f, -1.f));
        shots.push_back(shotTwo(pedPos(g, lucha), mp, 6.f));
        shots.push_back(shotTwo(pedPos(g, dex), mp, 6.f, 4.f, 42.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_KIT, kit, "[calm]It's all here. The tape from the pier. Holt's ledger. The deeds. Sandoval's books. I can go live in two minutes.");
        say(g, CAST_ROOK, rook, "[calm]Or we sell it back. Holt would pay anything to make this go away. The yard, the diner, money for all of us.");
        say(g, CAST_LUCHA, lucha, "[calm]Money comes and goes, mija. The neighborhood remembers what you did with it.");
        sayP(g, 1, dex, "[calm]Your call, Mari. Whatever you decide, I'm with you.");
    }

    void beginBroadcast(GameWorld& g) {
        setIdle(g, kit, 8);
        airTime = 90.f;
        g.missionTimerHud = airTime;
        wave = 0;
        loyalists(g, gPlaces.pulseFm.pos, 6, kit);
        g.mObjective("Defend ~b~Pulse FM~s~ while Kit is on the air.");
        score(SC_CHASE, 1.f, 4);
        cp(g, 1);
        setStage(3);
    }

    void beginAmbush(GameWorld& g) {
        wave = 0;
        loyalists(g, gPlaces.pierEnd - vec3(60.f, 0, 0), 9, -1);
        g.mObjective("Survive ~r~Holt's ambush~s~.");
        score(SC_CHASE, 1.f, 4);
        cp(g, 1);
        setStage(13);
    }

    void finish(GameWorld& g, bool passed) override {
        g.missionTimerHud = -1.f;
        if (passed) setFlag(g, EX_ENDING, choice);
    }

    void epilogue(GameWorld& g) {
        const Places& P = gPlaces;
        vec3 by = P.boatyard.door;
        std::vector<CutsceneShot> shots;
        shots.push_back(shotMove(P.solarisPlaza + vec3(-200.f, -220.f, 60.f), P.solarisPlaza + vec3(0, 0, 300.f), P.solarisPlaza + vec3(-160.f, -200.f, 40.f),
                                 P.solarisPlaza + vec3(0, 0, 120.f), 7.f, 55.f));
        // the family boatyard, from wherever it can be seen whole
        if (!establish(g, shots, by, P.boatyard.yaw, 34.f, 11.f, 8.f, 45.f))
            shots.push_back(shotMove(by + vec3(-40.f, -30.f, 12.f), by, by + vec3(-25.f, -20.f, 6.f), by + vec3(0, 0, 1.2f), 8.f, 45.f));
        establish(g, shots, P.riverLaunch, yawTo(P.riverLaunch.xy(), P.riverMouth.xy()), 60.f, 20.f, 8.f, 55.f);
        // the last shot drifts up over the river mouth for as long as the voices and the title card need
        shots.push_back(shotMove(P.riverMouth + vec3(-90.f, -70.f, 9.f), P.riverMouth + vec3(0.f, 0.f, 4.f), P.riverMouth + vec3(-75.f, -55.f, 34.f),
                                 P.riverMouth + vec3(60.f, 30.f, 14.f), 90.f, 55.f));
        g.mCutscene(shots, true);
        if (choice == 1) {
            narrator(g, "Pulse FM News", "Breaking news. Porto Sol police captain Reyna Holt was arrested this morning after a Pulse FM broadcast aired "
                                    "recordings and ledgers linking her to developer Victor Sandoval.", "newsreader_female", "[news]", kColKit);
            narrator(g, "Pulse FM News", "Sandoval faces forty one counts. The Solaris Pier project is suspended, and the Calle Luna deeds are back "
                                    "with their families.", "newsreader_female", "[news]", kColKit);
            sayP(g, 1, -1, "[calm]Think it'll stick?");
            sayP(g, 0, -1, "[happy:0.5]It's on the radio. Everybody heard it. That sticks.");
            sayP(g, 0, -1, "[happy]Stay for dinner. Lucha's making pozole.");
            sayP(g, 1, -1, "[happy]Wouldn't miss it.");
        } else {
            narrator(g, "Pulse FM News", "In local news, the Solaris Pier project was quietly cancelled. Its developer left the country. Police captain "
                                    "Reyna Holt announced a new community outreach program in Calle Luna.", "newsreader_female", "[news]", kColKit);
            sayP(g, 1, -1, "[calm]Two million dollars. The yard's yours. Nobody's losing their home.");
            sayP(g, 0, -1, "[sad:0.5]And Holt is still wearing a badge.");
            sayP(g, 1, -1, "[calm]We won, Mari. Mostly. Keep the copies somewhere safe.");
            sayP(g, 0, -1, "[sad:0.4]Somewhere safe. In this town. That'll be the day.");
        }
    }

    // The ending: fade to black, the epilogue comes up out of it, the title card over the last shot, a fade out, and the
    // world fades back in for free roam.
    MissionStatus ending(GameWorld& g, float dt, const char* afterword) {
        endT += dt;
        switch (endPhase) {
            case 0:
                if (g.mTalking()) break;
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
                    gMissions.cardSub = "The End";
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
                    gMissions.shots.clear();   // the epilogue camera ends under black
                    gMissions.shotIndex = -1;
                    g.fadeIn(0.7f);
                    g.storyBriefText = afterword;
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, dex, "Dex")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking() && !menuIs(MO_CHOICE)) {
                    std::vector<MenuItem> items(2);
                    items[0].label = "Broadcast it";
                    items[0].detail = "Kit goes live on Pulse FM with everything. Holt and Sandoval face the whole city. Holt will send everyone "
                                      "she has left to shut the station down.";
                    items[0].id = 1;
                    items[1].label = "Use it as leverage";
                    items[1].detail = "Meet Holt on the Sol Beach Pier and make her pay for silence: the yard, the deeds and two million dollars. "
                                      "She will not play fair.";
                    items[1].id = 2;
                    menuOpen(g, MO_CHOICE, "SIGNAL", "What does Mari do with the evidence?", items, true, 0xff40c0ffu);
                    next();
                }
                break;
            case 1:
                if (menuIs(MO_CHOICE) && gMenu.chosen > 0) {
                    choice = gMenu.chosen;
                    menuClose(g);
                    setFlag(g, EX_ENDING, choice);
                    if (choice == 1) {
                        sayMe(g, "[calm]Put it on the air, Kit. All of it.");
                        say(g, CAST_KIT, kit, "[dj]Pulse FM, this is Kit Navarro, and tonight's show is about the people who sold our city. Stay tuned.");
                        sayP(g, 1, dex, "[shout]Here come Holt's loyalists. Everybody find cover.");
                        beginBroadcast(g);
                    } else {
                        sayMe(g, "[calm]Call Holt. Tell her the Sol Beach Pier. Tonight. Come alone.");
                        say(g, CAST_KIT, kit, "[scared:0.3]She won't come alone.");
                        sayMe(g, "[calm]I know.");
                        goTo(g, gPlaces.pierEnd - vec3(30.f, 0, 0), 3.f, "Meet Holt at the end of the ~y~Sol Beach Pier~s~.");
                        provideRide(g, gPlaces.pulseFm, 12.f);
                        setFollow(g, dex, g.player);
                        setStage(11);
                    }
                }
                break;
            case 3: {
                updateBuddy(g, 45.f);
                if (allyDown(g, kit, "Kit")) return MS_FAILED;
                airTime -= dt;
                g.missionTimerHud = Max(0.f, airTime);
                for (int v : enemyCars) dismountNear(g, v, gPlaces.pulseFm.pos, 35.f, kit);
                if (aliveEnemies(g) <= 2 && wave < 4 && airTime < 80.f - wave * 18.f) loyalists(g, gPlaces.pulseFm.pos, 6, kit);
                if (fmodf(airTime, 20.f) < dt && airTime > 5.f)
                    narrator(g, "Kit (on air)", "Holt's own ledger, page twelve. Fifty thousand from Solaris Pier Holdings. Porto Sol, you are listening to the truth.",
                             "cast_kit", "[dj]", kColKit);
                if (airTime <= 0.f) {
                    g.missionTimerHud = -1.f;
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    say(g, CAST_KIT, kit, "[happy]It's out. Every station in the state is picking it up. They can't stop it now.");
                    next();
                }
                break;
            }
            case 4:
                return ending(g, dt,
                              "Mari broadcast everything on Pulse FM. Holt is in custody, Sandoval faces trial, and the Calle Luna deeds are back with "
                              "their families. Porto Sol is yours to explore.");
            case 11:
                updateBuddy(g);
                if (arrived(g)) {
                    clearGoal(g);
                    vec3 hpos = gPlaces.pierEnd - vec3(10.f, 0, 0);
                    holt = spawnCast(g, CAST_HOLT, hpos, kPi * 0.5f, FAC_CIVILIAN);
                    if (holt >= 0) g.peds[holt].invincible = true;
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    vec3 hp = pedPos(g, holt), mp = playerPos(g);
                    facePed(g, g.player, hp);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(hp, mp, 7.f));
                    shots.push_back(shotOver(mp, hp, 7.f));
                    shots.push_back(shotOver(hp, mp, 7.f, -1.f));
                    shots.push_back(shotTwo(hp, mp, 6.f, 6.f, 45.f, -1.f));
                    g.mCutscene(shots);
                    say(g, CAST_HOLT, holt, "[calm]You have something of mine, Ortega.");
                    sayMe(g, "[calm]I have a lot of things of yours. Two million dollars. Every deed back where it belongs. And you never set foot in Calle Luna again.");
                    say(g, CAST_HOLT, holt, "[calm]Two million. Fine. The money is already moving. Pleasure doing business.");
                    say(g, CAST_HOLT, holt, "[angry:0.4]Take them.");
                    sayP(g, 1, dex, "[shout]Knew it!");
                    next();
                }
                break;
            case 12:
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (holt >= 0) setGoto(g, holt, gPlaces.pierRamp, 3.f);
                    beginAmbush(g);
                }
                break;
            case 13:
                updateBuddy(g, 50.f);
                if (aliveEnemies(g) <= 2 && wave < 3) loyalists(g, gPlaces.pierEnd - vec3(80.f, 0, 0), 6, -1);
                if (aliveEnemies(g) == 0 && wave >= 3) {
                    g.mClearBlips();
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_PHONE_RING, 0.8f);
#endif
                    phoneLine(g, CAST_HOLT, "[calm]Impressive. The money is in your account, Ortega. Keep your copies safe. I'll keep my badge.");
                    sayMe(g, "[angry:0.6]Stay out of my neighborhood, Captain.");
                    next();
                }
                break;
            case 14:
                return ending(g, dt,
                              "Mari traded the evidence for the deeds, the boatyard and two million dollars. Holt kept her badge, but not her grip on "
                              "Calle Luna. Porto Sol is yours to explore.");
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (menuIs(MO_CHOICE) && t.stageTime > 0.5f) {
                    const char* e = Platform::argValue("ending");
                    gMenu.chosen = e && strcmp(e, "leverage") == 0 ? 2 : 1;
                }
                break;
            case 3:
                airTime -= dt * 4.f;
                if (fmodf(t.stageTime, 1.5f) < dt) t.killEnemies();
                break;
            case 11:
                if (t.stageTime > 0.5f) {
                    t.exitVehicle();
                    t.teleport(goal, -kPi * 0.5f);
                    if (dex >= 0) placePed(g, dex, goal - vec3(2.f, 2.f, 0.f), 0.f);
                }
                break;
            case 13: if (fmodf(t.stageTime, 1.5f) < dt && t.stageTime > 1.f) t.killEnemies(); break;
            default: break;
        }
    }
};

}  // namespace mu
}  // namespace Game
