// Story, Act 2 "The City": Dead Air, Velvet Rope, The Bagman, Sawgrass Run, Riptide, Heavy Lift, Fireworks,
// Second Chance, Paper Trail. Kit Navarro and Jonah Pike join the crew; they follow Holt's money from Sol Beach
// nightlife through the bay, the Sawgrass and the Port Isle terminal, and take her private ledger.
#include "missions.h"

namespace Game {
namespace mu {

// ==================================================================================================================
// Act 2-1: "Dead Air" (Mari). Bring El Cuervo's phone to Kit at Pulse FM; Holt's officers raid the station and Mari
// has to get Kit and her gear out.
class MissionDeadAir : public StoryMission {
public:
    int kit = -1, car = -1;
    const char* title() const override { return "Dead Air"; }
    const char* brief() const override {
        return "Kit Navarro, DJ and hacker at Pulse FM, can crack El Cuervo's phone. Holt's officers are on their way to shut the station "
               "down. Get Kit out and lose the police.";
    }
    long long reward() const override { return 5000; }

    void start(GameWorld& g) override {
        const Place& S = gPlaces.pulseFm;
        score(SC_NOIR, 0.3f, 6);
        kit = spawnCast(g, CAST_KIT, S.door, S.yaw + kPi, FAC_FRIEND);
        if (kit >= 0) g.peds[kit].maxHealth = g.peds[kit].health = 300.f;
        int model = pickModel(g, {Vehicles::VC_COMPACT, Vehicles::VC_SEDAN}, 2);
        car = spawnCar(g, model, curbOffset(g, S, 9.f), S.curbYaw, lin(0.1f, 0.6f, 0.6f));
        if (checkpoint >= 1) {
            placePlayer(g, curbOffset(g, S, 9.f), S.curbYaw);
            if (car >= 0) {
                g.warpPedIntoVehicle(g.player, car, 0);
                if (kit >= 0) g.warpPedIntoVehicle(kit, car, 1);
            }
            beginEscape(g);
            return;
        }
        placePlayer(g, placeOffset(g, S, -3.f, 0.f), S.yaw);
        vec3 kp = pedPos(g, kit), mp = playerPos(g);
        facePed(g, kit, mp);
        facePed(g, g.player, kp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, kp, S.yaw, 40.f, 22.f, 4.f);
        shots.push_back(shotTwo(kp, mp, 6.f));
        shots.push_back(shotOver(mp, kp, 7.f));
        shots.push_back(shotOver(kp, mp, 6.f, -1.f));
        shots.push_back(shotTwo(kp, mp, 5.f, 5.f, 45.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_KIT, kit, "[happy:0.6]So you're the Ortega girl who punched a hole in the Cuervos. Tomas talks about you like you're a superhero.");
        sayMe(g, "Tomas talks too much. This is El Cuervo's phone. Can you crack it?");
        say(g, CAST_KIT, kit, "[happy:0.4]Cheap case, expensive secrets. Give me an hour and a coffee.");
        say(g, CAST_KIT, kit, "[scared:0.7]Oh no. Hear that? Holt's been trying to pull our license for a year. Somebody tipped her off.");
        sayMe(g, "[shout:0.5]Grab your gear. We're leaving.");
    }

    void beginEscape(GameWorld& g) {
        setWanted(g, 2);
        g.mClearTarget();
        g.mObjective("Lose the ~b~police~s~.");
        score(SC_CHASE, 0.85f, 6);
        cp(g, 1);
        setStage(3);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (allyDown(g, kit, "Kit")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, kit, g.player);
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    g.mObjective("Get Kit into the ~b~car~s~.");
                    next();
                }
                break;
            case 1: {
                bool together = g.playerVehicle() >= 0 && g.peds[kit].vehicle == g.playerVehicle();
                if (together) {
                    g.mClearBlips();
                    say(g, CAST_KIT, kit, "[shout]Drive! They'll have the whole block covered in a minute.");
                    beginEscape(g);
                }
                break;
            }
            case 3:
                buddyUpdate(g, kit, nullptr);
                if (stageTime > 6.f && stageTime < 6.1f) {
                    say(g, CAST_KIT, kit, "[scared:0.3]You know what the worst part is? I was halfway through my best set of the year.");
                    sayMe(g, "[shout:0.5]You can finish it in jail if we don't lose them.");
                }
                if (stageTime > 24.f && stageTime < 24.1f) say(g, CAST_KIT, kit, "[shout]Left! No - your other left!");
                if (g.pinfo.wanted == 0) {
                    score(SC_NOIR, 0.35f, 6);
                    say(g, CAST_KIT, kit, "[calm]We lost them. Head to the Flats. I keep a backup studio in a garage there. Don't judge.");
                    goTo(g, gPlaces.kitStudio.curb, 6.f, "Take Kit to her ~y~backup studio~s~ in the Flats.", true);
                    next();
                }
                break;
            case 4: {
                buddyUpdate(g, kit, nullptr);
                if (g.pinfo.wanted > 0) {
                    g.mObjective("Lose the ~b~police~s~.");
                    break;
                }
                bool together = g.peds[kit].vehicle >= 0 && g.peds[kit].vehicle == g.playerVehicle();
                if (arrived(g) && together) {
                    clearGoal(g);
                    g.removePedFromVehicle(kit, true);
                    if (g.playerVehicle() >= 0) g.removePedFromVehicle(g.player, false);
                    vec3 kp = pedPos(g, kit), mp = playerPos(g);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotTwo(kp, mp, 7.f));
                    shots.push_back(shotOver(mp, kp, 8.f));
                    g.mCutscene(shots);
                    say(g, CAST_KIT, kit, "[happy:0.4]Okay. El Cuervo is careful, but his phone isn't. Messages from a blocked number. Friday. Club Riptide.");
                    say(g, CAST_KIT, kit, "Holt meets Sandoval's accountant in the VIP cabana. If I could get into her phone, we'd have everything.");
                    sayMe(g, "[calm]Then we get into her phone.");
                    next();
                }
                break;
            }
            case 5:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "Kit cracked El Cuervo's phone: Holt meets Sandoval's accountant at Club Riptide on Sol Beach. Kit can "
                                       "clone Holt's phone if someone plants a chip in her jacket.";
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
                if (t.stageTime > 0.5f && car >= 0) {
                    t.enter(car);
                    g.warpPedIntoVehicle(kit, car, 1);
                }
                break;
            case 3:
                if (t.stageTime > 2.f) {
                    g.pinfo.wanted = 0;
                    g.pinfo.wantedHeat = 0.f;
                }
                break;
            case 4: testGoal(g, t, dt); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-2: "Velvet Rope" (Mari). Talk (or pay) past the bouncer at Club Riptide, slip into the VIP cabana and plant
// Kit's clone chip in Holt's jacket, then walk out.
class MissionVelvetRope : public StoryMission {
public:
    int bouncer = -1, holt = -1;
    std::vector<int> crowd;
    StealthGroup vip;
    vec3 anchor, entrance, dance, cabana;
    vec2 out, along;
    HoldAction plant;
    bool clean = true;
    bool entered = false;
    Audio::EmitterHandle music = 0;
    float strobe = 0.f;
    const char* title() const override { return "Velvet Rope"; }
    const char* brief() const override {
        return "Get into Club Riptide on Sol Beach, sneak into the VIP cabana and plant Kit's clone chip in Captain Holt's jacket. Don't "
               "get caught.";
    }
    long long reward() const override { return 7000 + (clean ? 2000 : 0); }

    void buildClub(GameWorld& g) {
        // Club Riptide is an open-air beach club on the sand east of the Ocean Promenade: find the shoreline at the
        // club's latitude and lay the dance floor, DJ, bar and VIP cabana out along the beach.
        const Place& C = gPlaces.clubRiptide;
        float shoreX = C.pos.x + 60.f;
        for (float x = C.pos.x; x < C.pos.x + 400.f; x += 2.f)
            if (g.map->isWater(x, C.pos.y)) {
                shoreX = x;
                break;
            }
        out = vec2(1, 0);
        along = vec2(0, 1);
        anchor = vec3(shoreX - 34.f, C.pos.y, 0.f);
        anchor.z = groundAt(g, anchor.x, anchor.y, C.pos.z + 4.f);
        auto at = [&](float fwd, float side) {
            vec2 p = anchor.xy() + out * fwd + along * side;
            if (g.map->isWater(p.x, p.y)) p.x = shoreX - 4.f;
            return vec3(p, groundAt(g, p.x, p.y, anchor.z + 4.f));
        };
        entrance = at(0.f, 0.f);
        dance = at(20.f, 0.f);
        cabana = at(24.f, 34.f);
        bouncer = spawnCast(g, CAST_BOUNCER, at(2.f, -1.8f), yawTo(at(2.f, -1.8f).xy(), anchor.xy()), FAC_CIVILIAN);
        if (bouncer >= 0) {
            g.peds[bouncer].invincible = true;
            setIdle(g, bouncer, 0);
        }
        // dancers
        for (int i = 0; i < 16; i++) {
            float a = kTwoPi * i / 16.f, r = 3.f + (i % 3) * 2.5f;
            vec3 p = dance + vec3(cosf(a) * r, sinf(a) * r, 0.f);
            p.z = groundAt(g, p.x, p.y, dance.z + 3.f);
            int ci = g.randomCivilianChar(0x7700u + (u32)i * 31u, i % 4 == 0 ? 4 : 0);
            int d = g.mPed(ci, dvec3(p), a + kPi, FAC_CIVILIAN);
            if (d < 0) continue;
            setIdle(g, d, i % 5 == 0 ? 7 : 9);
            crowd.push_back(d);
        }
        int dj = g.mPed(g.randomCivilianChar(0x7799u, 0), dvec3(at(20.f, -12.f)), yawTo(at(20.f, -12.f).xy(), dance.xy()), FAC_CIVILIAN);
        if (dj >= 0) setIdle(g, dj, 9);
        // Holt dancing near the cabana, guards around it
        holt = spawnCast(g, CAST_HOLT, at(20.f, 14.f), 0.f, FAC_CIVILIAN);
        if (holt >= 0) {
            g.peds[holt].invincible = true;
            setIdle(g, holt, 9);
        }
        vec3 g0 = at(18.f, 26.f), g1 = at(26.f, 44.f), g2 = at(12.f, 38.f);
        int ga = spawnCast(g, CAST_GUARD_A, g0, yawTo(g0.xy(), dance.xy()), FAC_ENEMY);
        int gb = spawnCast(g, CAST_GUARD_B, g1, yawTo(g1.xy(), cabana.xy()), FAC_ENEMY);
        int gc = spawnCast(g, CAST_GUARD_A, g2, 0.f, FAC_ENEMY);
        int gs[3] = {ga, gb, gc};
        for (int i = 0; i < 3; i++) {
            if (gs[i] < 0) continue;
            arm(g, gs[i], WPN_PISTOL);
            g.peds[gs[i]].brain.accuracy = 0.35f;
            setIdle(g, gs[i], 0);
            enemies.push_back(gs[i]);
        }
        vip.add(g, ga, {}, 13.f);
        vip.add(g, gb, {g1, at(26.f, 24.f)}, 13.f);
        vip.add(g, gc, {g2, at(12.f, 46.f), at(20.f, 50.f)}, 13.f);
#ifdef HAVE_AUDIO
        music = Audio::createEmitter(Audio::EMIT_RADIO_WORLD);
#endif
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        g.env->timeOfDay = (g.env->timeOfDay > 4.f && g.env->timeOfDay < 21.f) ? 21.5f : g.env->timeOfDay;
        buildClub(g);
        score(SC_STEALTH, 0.3f, 1);
        if (checkpoint >= 1) {
            placePlayer(g, dance - vec3(out * 8.f, 0.f), yawTo(dance.xy() - out * 8.f, cabana.xy()));
            entered = true;
            beginInside(g);
            return;
        }
        placePlayer(g, curbOffset(g, gPlaces.clubRiptide, -20.f), gPlaces.clubRiptide.curbYaw);
        std::vector<CutsceneShot> shots;
        establish(g, shots, dance, atan2f(-out.x, out.y) + kPi, 45.f, 18.f, 4.5f, 50.f);
        shots.push_back(shotArc(dance, 14.f, 3.f, 0.f, 1.2f, 6.f));
        shots.push_back(shotMove(cabana + vec3(out * -12.f, 3.f), pedPos(g, holt) + vec3(0, 0, 1.4f), cabana + vec3(out * -10.f, 2.5f),
                                 pedPos(g, holt) + vec3(0, 0, 1.5f), 5.f, 35.f));
        g.mCutscene(shots);
        phoneLine(g, CAST_KIT, "[calm]Club Riptide. Holt's already in the VIP cabana. She always leaves her jacket on the couch.");
        phoneLine(g, CAST_KIT, "[calm]Get the chip in the inside pocket and walk out like you own the place. Security will be on the rope.");
        sayMe(g, "[whisper:0.5]Walk in, plant it, walk out. Got it.");
    }

    void beginInside(GameWorld& g) {
        goTo(g, cabana, 2.f, "Plant the chip in ~y~Holt's jacket~s~ in the VIP cabana. Avoid the ~r~guards~s~.", false, true, vec3(0.3f, 0.8f, 1.f));
        g.mClearTarget();
        g.mTarget(cabana.xy());
        cp(g, 1);
        setStage(3);
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
#ifdef HAVE_AUDIO
        if (music) Audio::destroyEmitter(music);
        music = 0;
#endif
        showMeter(g, "DETECTION", 0.f);
    }

    MissionStatus update(GameWorld& g, float dt) override {
#ifdef HAVE_AUDIO
        if (music) Audio::setEmitter(music, dance + vec3(0, 0, 2.f), vec3(0), 3.f, 0.f, 0.f, 0.f, 1.f);
#endif
        // club lights
        strobe += dt;
        if (::length(playerPos(g) - dance) < 200.f) {
            vec3 cols[3] = {vec3(1.f, 0.2f, 0.7f), vec3(0.2f, 0.6f, 1.f), vec3(0.6f, 0.2f, 1.f)};
            for (int k = 0; k < 3; k++) {
                float a = strobe * (0.8f + 0.3f * k) + k * 2.1f;
                spawnLight(dvec3(dance + vec3(cosf(a) * 6.f, sinf(a) * 6.f, 4.f)), cols[(k + (int)(strobe * 2.f)) % 3] * 900.f, 14.f);
            }
        }
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    goTo(g, entrance, 2.f, "Get past the ~y~bouncer~s~.");
                    next();
                }
                break;
            case 1:
                if (arrived(g)) {
                    clearGoal(g);
                    facePed(g, bouncer, playerPos(g));
                    if (currentOutfit(g, 0) == 2) {
                        say(g, CAST_BOUNCER, bouncer, "[happy:0.5]Now that's a dress. Go on in, miss.");
                        entered = true;
                        setStage(2);
                    } else {
                        say(g, CAST_BOUNCER, bouncer, "[calm]Private party tonight. Not dressed like that.");
                        std::vector<MenuItem> items;
                        MenuItem pay;
                        pay.label = "Slip him some cash";
                        pay.price = 500;
                        pay.enabled = g.pinfo.money >= 500;
                        pay.detail = "Five hundred dollars buys a lot of goodwill on Sol Beach.";
                        pay.id = 1;
                        items.push_back(pay);
                        MenuItem leave;
                        leave.label = "Come back in the Night Out dress";
                        leave.detail = "Buy it at Threads on Sol Beach or change at a safehouse wardrobe, then come back.";
                        leave.id = 2;
                        items.push_back(leave);
                        menuOpen(g, MO_CHOICE, "CLUB RIPTIDE", "The bouncer blocks the rope", items, true, 0xffc040f0u);
                        next();
                    }
                }
                break;
            case 2:
                if (menuIs(MO_CHOICE)) {
                    if (gMenu.chosen == 1) {
                        menuClose(g);
                        money(g, -500);
#ifdef HAVE_AUDIO
                        Audio::play2D(Audio::SFX_CASH_REGISTER, 0.7f);
#endif
                        say(g, CAST_BOUNCER, bouncer, "[happy:0.4]Well, why didn't you say so. Enjoy your night.");
                        entered = true;
                    } else if (gMenu.chosen == 2 || gMenu.cancelled) {
                        menuClose(g);
                        sayMe(g, "[angry:0.4]Fine. I'll be back.");
                        goTo(g, entrance, 2.f, "Change into the ~y~Night Out~s~ dress (or bring $500), then return to the ~y~bouncer~s~.");
                        setStage(1);
                        break;
                    }
                }
                if (entered && !g.mTalking()) beginInside(g);
                break;
            case 3: {
                bool inVip = ::length(playerPos(g).xy() - cabana.xy()) < 30.f;
                if (inVip && vip.update(g, dt)) {
                    clean = false;
                    say(g, CAST_GUARD_A, vip.spotter, "[shout]Hey! VIP only! Get her!");
                    sayMe(g, "[angry:0.4]So much for walking out.");
                    score(SC_CHASE, 0.9f, 1);
                    for (int c : crowd) setFlee(g, c, g.player);
                    blipEnemies(g);
                }
                showMeter(g, "DETECTION", vip.alarm ? 0.f : vip.meter);
                if (plant.update(g, cabana, 1.8f, 3.f, "plant the chip", dt)) {
                    g.hudHelpTimer = 0.f;
                    g.mClearMarkers();
                    g.mClearTarget();
                    phoneLine(g, CAST_KIT, "[shout:0.5]I'm in. Everything on her phone is ours. Now get out of there.");
                    g.mObjective(vip.alarm ? "Get out of the club. Lose the ~r~guards~s~." : "Leave the club without being noticed.");
                    next();
                }
                break;
            }
            case 4: {
                bool inVip = ::length(playerPos(g).xy() - cabana.xy()) < 30.f;
                if (inVip && vip.update(g, dt)) {
                    clean = false;
                    score(SC_CHASE, 0.9f, 1);
                    blipEnemies(g);
                    g.mObjective("Get out of the club. Lose the ~r~guards~s~.");
                }
                showMeter(g, "DETECTION", vip.alarm ? 0.f : vip.meter);
                float d = ::length(playerPos(g).xy() - dance.xy());
                bool guardsNear = false;
                for (int e : enemies)
                    if (pedAlive(g, e) && ::length(pedPos(g, e) - playerPos(g)) < 90.f && vip.alarm) guardsNear = true;
                if (d > 110.f && !guardsNear) {
                    showMeter(g, "DETECTION", 0.f);
                    g.storyBriefText = clean ? "Mari planted Kit's chip without anyone noticing. Kit now reads everything on Captain Holt's phone."
                                             : "Mari planted Kit's chip and shot her way out of Club Riptide. Kit now reads Holt's phone - "
                                               "and Holt knows someone is coming for her.";
                    return MS_PASSED;
                }
                break;
            }
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        switch (stage) {
            case 1: if (t.stageTime > 0.5f) t.teleportNear(entrance.xy(), 0.5f); break;
            case 2:
                if (menuIs(MO_CHOICE) && t.stageTime > 0.6f) {
                    g.pinfo.money = Max<long long>(g.pinfo.money, 1000);
                    gMenu.items[0].enabled = true;
                    gMenu.chosen = 1;
                }
                break;
            case 3:
                if (t.stageTime > 0.5f && ::length(playerPos(g).xy() - cabana.xy()) > 1.2f) {
                    t.teleport(vec3(cabana.x, cabana.y, groundAt(g, cabana.x, cabana.y)), yawTo(cabana.xy(), dance.xy()));
                    Ped* pl = g.playerPed();
                    if (pl) pl->animIn.crouch = true;
                }
                break;
            case 4: if (t.stageTime > 0.5f) t.teleportNear(anchor.xy() - out * 130.f, 1.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-3: "The Bagman" (Dex). Tail Holt's collector on his rounds, take him down before he reaches the precinct,
// grab the bag and shake the police. He never drives the bag to the precinct: he parks under the SkyLine at Civic
// Center and rides one stop to Solaris, so the last leg is a chase onto the metro (ride his train, or beat it there).
class MissionBagman : public StoryMission {
public:
    int bagCar = -1, bagman = -1, bag = -1;
    std::vector<vec3> stops;
    Place stopPlace[2];
    int stopIndex = 0;
    int phase = 0;      // 0 driving to a stop, 1 walking to the door, 2 walking back
    vec3 door;
    // the SkyLine leg (station -1: no metro in this world, he drives to the precinct)
    int station = -1, side = 1, dest = -1;
    int metroPhase = 0; // 0 driving to the station, 1 up to the platform, 2 waiting, 3 boarding, 4 riding, 5 down to the street, 6 running
    int train = -1, boardDoor = 0;
    u32 trainUid = 0, bagmanUid = 0;
    vec3 parkSpot;
    PedPath walk;
    float waitT = 0.f, boardT = 0.f, stopT = 0.f;
    bool rideLine = false;
    int testShotPhase = 0;   // --missiontest: the SkyLine phase last photographed
    const char* title() const override { return "The Bagman"; }
    const char* brief() const override {
        return "Every week Holt's bagman collects protection money from half of Porto Sol. Follow him on his rounds, then take the bag "
               "before he delivers it to the precinct.";
    }
    long long reward() const override { return 12000; }

    void driveTo(GameWorld& g, vec3 to, float speed, bool aggressive) {
        ScriptDriver& d = driveRoad(g, bagCar, to.xy(), speed, aggressive);
        d.obeyLimits = !aggressive;
        if (aggressive) {
            d.rubberPed = g.player;
            d.rubberGap = 50.f;
        }
    }

    void start(GameWorld& g) override {
        const Places& P = gPlaces;
        // two collections a few blocks apart downtown (the tail runs about two minutes)
        stopPlace[0] = resolveFrontage(g, vec2(2800.f, 40.f));
        stopPlace[1] = resolveFrontage(g, vec2(2540.f, 380.f));
        stops = {stopPlace[0].curb, stopPlace[1].curb};
        // the SkyLine: Civic Center, one stop to Solaris (the precinct stands a short walk from it)
        station = metroStationByName("Civic Center");
        int solaris = metroStationByName("Solaris");
        if (station >= 0 && solaris >= 0) {
            const World::MetroLine& L = World::gTransit->metro;
            side = L.delta(L.stations[station].s, L.stations[solaris].s) < 0.f ? 1 : 0;   // the service that reaches Solaris first
            parkSpot = metroStairPath(station, side, true, 0.f)[0];
        } else {
            station = -1;
        }
        score(SC_STEALTH, 0.35f, 3);
        int model = pickModel(g, {Vehicles::VC_SEDAN}, 4);
        float byaw;
        vec3 sp = curbOffset(g, P.policeHq, 60.f, &byaw);
        if (checkpoint == 2 && station >= 0) {
            // restart on the last leg: the bagman pulls away from his second collection
            float yaw2 = stopPlace[1].curbYaw;
            bagCar = spawnCar(g, model, stopPlace[1].curb, yaw2, lin(0.55f, 0.55f, 0.58f));
            bagman = spawnCast(g, CAST_GUARD_B, stopPlace[1].curb, 0.f, FAC_ENEMY);
            if (bagman >= 0 && bagCar >= 0) {
                g.warpPedIntoVehicle(bagman, bagCar, 0);
                arm(g, bagman, WPN_PISTOL);
                g.peds[bagman].brain.accuracy = 0.3f;
                bagmanUid = g.peds[bagman].uid;
            }
            vec3 behind = stopPlace[1].curb - vec3(dirFromYaw(yaw2) * 45.f, 0.f);
            placePlayer(g, vec3(behind.xy(), groundAt(g, behind.x, behind.y)), yaw2, pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SEDAN}, 2),
                        lin(0.08f, 0.35f, 0.3f));
            lastLeg(g);
            return;
        }
        if (checkpoint == 1) {
            // restart with the bag, cops closing in
            placePlayer(g, stopPlace[1].curb + vec3(0, 0, 0.5f), stopPlace[1].curbYaw, pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SEDAN}, 2));
            setWanted(g, 3);
            g.mObjective("Lose the ~b~police~s~.");
            setStage(6);
            return;
        }
        bagCar = spawnCar(g, model, sp, byaw, lin(0.55f, 0.55f, 0.58f));
        bagman = spawnCast(g, CAST_GUARD_B, sp, 0.f, FAC_ENEMY);
        if (bagman >= 0 && bagCar >= 0) {
            g.warpPedIntoVehicle(bagman, bagCar, 0);
            arm(g, bagman, WPN_PISTOL);
            g.peds[bagman].brain.accuracy = 0.3f;
            bagmanUid = g.peds[bagman].uid;
        }
        float pyaw;
        vec3 pspot = curbOffset(g, P.policeHq, 150.f, &pyaw);
        placePlayer(g, pspot, pyaw, pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SEDAN}, 2),
                    lin(0.08f, 0.35f, 0.3f));
        phoneLine(g, CAST_ROOK, "[calm]Grey sedan leaving the precinct. That's Holt's bagman. He does two pickups, then takes it all back to her.");
        phoneLine(g, CAST_ROOK, "[calm]Stay on him, stay invisible. When he heads home with the bag, you take it.");
        sayMe(g, "[happy:0.5]Robbing a crooked cop. My favorite kind of Tuesday.");
        driveTo(g, stops[0], 13.f, false);
        g.mBlipVehicle(bagCar, UI::BLIP_VEHICLE);
        g.mObjective("Follow the ~b~bagman~s~. Keep your distance.");
    }

    // after the last collection: to the SkyLine at Civic Center (or, without a metro, straight to the precinct)
    void lastLeg(GameWorld& g) {
        g.mClearBlips();
        if (station >= 0) {
            driveTo(g, parkSpot, 20.f, true);
            phoneLine(g, CAST_ROOK, "[calm]Heads up. He never drives the bag to the precinct. He parks under the SkyLine at Civic Center and rides one stop to Solaris.");
            phoneLine(g, CAST_ROOK, "[calm]Nobody tails a man onto a train. That's the idea, anyway.");
            sayMe(g, "[happy:0.4]Then I take him before he gets on it. Or on it.");
            g.mObjective("Take down the ~r~bagman~s~ before he reaches the precinct!");
        } else {
            driveTo(g, gPlaces.policeHq.curb, 22.f, true);
            sayMe(g, "[whisper:0.4]That's the last stop. He's heading back to the precinct.");
            g.mObjective("Take down the ~r~bagman~s~ before he reaches the precinct!");
        }
        if (bagCar >= 0) g.mBlipVehicle(bagCar, UI::BLIP_ENEMY);
        score(SC_CHASE, 0.85f, 3);
        metroPhase = 0;
        setStage(1);
    }

    // no metro: he drives the bag to the precinct
    MissionStatus carChase(GameWorld& g) {
        if (!vehicleAlive(g, bagCar) && !pedAlive(g, bagman)) {
            // wreck: the bag lands next to it
            bag = spawnPackage(g, vehPos(g, bagCar) + vec3(2.f, 0, 0.2f));
            setStage(3);
            return MS_RUNNING;
        }
        ScriptDriver* d = driverFor(bagCar);
        if (d && d->done) return fail("The bagman made it to the precinct.");
        if (::length(vehPos(g, bagCar) - gPlaces.policeHq.curb) < 35.f) return fail("The bagman made it to the precinct.");
        bool stopped = vehicleDisabled(g, bagCar) || g.vehicles[bagCar].sim.health < 420.f || !pedAlive(g, bagman) ||
                       (pedAlive(g, bagman) && g.peds[bagman].vehicle != bagCar);
        if (stopped) {
            releaseDriver(g, bagCar);
            if (pedAlive(g, bagman)) {
                if (g.peds[bagman].vehicle >= 0) g.removePedFromVehicle(bagman, true);
                setFlee(g, bagman, g.player);
                say(g, CAST_GUARD_B, bagman, "[shout]You're dead! You know whose money this is?");
            }
            bag = spawnPackage(g, vehPos(g, bagCar) + vec3(g.vehicles[bagCar].sim.right().xy() * -2.2f, 0.3f));
            setStage(3);
        }
        return MS_RUNNING;
    }

    const World::MetroStation& stationAt(int i) const { return World::gTransit->metro.stations[i]; }

    // The SkyLine leg: he parks at Civic Center, walks up, rides one stop to Solaris and walks to the precinct. Down
    // anywhere, the bag drops where he fell; a train that runs off with him (the player far behind) takes him away.
    MissionStatus metroChase(GameWorld& g, float dt) {
        const Places& P = gPlaces;
        if (bagman < 0 || !g.peds[bagman].used || g.peds[bagman].uid != bagmanUid) return fail("The bagman got away on the SkyLine.");
        if (g.peds[bagman].health <= 0.f) {
            vec3 at = pedPos(g, bagman);
            if (metroPhase == 0 && vehicleAlive(g, bagCar)) at = vehPos(g, bagCar) + vec3(g.vehicles[bagCar].sim.right().xy() * -2.2f, 0.f);
            releaseDriver(g, bagCar);
            if (train >= 0) unblipVehicle(train);
            bag = spawnPackage(g, at + vec3(0.6f, 0.4f, 0.3f));
            LOG("[bagman] down in SkyLine phase %d", metroPhase);
            setStage(3);
            return MS_RUNNING;
        }
        vec3 bp = pedPos(g, bagman);
        switch (metroPhase) {
            case 0: {   // driving to Civic Center
                ScriptDriver* d = driverFor(bagCar);
                bool there = (d && d->done) || ::length(vehPos(g, bagCar).xy() - parkSpot.xy()) < 16.f;
                bool stopped = !vehicleAlive(g, bagCar) || vehicleDisabled(g, bagCar) || g.vehicles[bagCar].sim.health < 420.f ||
                               g.peds[bagman].vehicle != bagCar;
                if (!there && !stopped) break;
                releaseDriver(g, bagCar);
                if (g.peds[bagman].vehicle >= 0) g.removePedFromVehicle(bagman, true);
                bp = pedPos(g, bagman);
                if (!there && ::length(bp.xy() - parkSpot.xy()) > 450.f) {
                    // too far from the station to run for it: he bolts and the bag stays in the car
                    setFlee(g, bagman, g.player);
                    say(g, CAST_GUARD_B, bagman, "[shout]You're dead! You know whose money this is?");
                    bag = spawnPackage(g, vehPos(g, bagCar) + vec3(g.vehicles[bagCar].sim.right().xy() * -2.2f, 0.3f));
                    setStage(3);
                    return MS_RUNNING;
                }
                unblipVehicle(bagCar);
                g.mBlipPed(bagman, UI::BLIP_ENEMY);
                walk.start(g, bagman, metroStairPath(station, side, true, 0.f), there ? 1.8f : 4.6f);
                if (there) sayMe(g, "[whisper:0.5]He's parking under the SkyLine. Here we go.");
                else say(g, CAST_GUARD_B, bagman, "[shout]Back off! This is police business!");
                g.mObjective("Stop the ~r~bagman~s~ before he gets on the SkyLine!");
                LOG("[bagman] on foot to %s (%s)", stationAt(station).name.c_str(), there ? "parked" : "car stopped");
                metroPhase = 1;
                break;
            }
            case 1:   // up the stair, through the gates, onto the platform
                if (walk.update(g, dt)) {
                    setIdle(g, bagman, 0);
                    facePed(g, bagman, bp - vec3(stationAt(station).right() * metroSideSign(side), 0.f));
                    waitT = 0.f;
                    metroPhase = 2;
                    LOG("[bagman] waiting on the %s platform", stationAt(station).name.c_str());
                }
                break;
            case 2: {   // waiting for the train toward Solaris
                waitT += dt;
                int c = metroCarAt(g, station, side, bp.xy());
                if (c >= 0) {
                    train = c;
                    trainUid = g.vehicles[c].uid;
                    float bd = 1e9f;
                    for (int k = -1; k <= 1; k++) {
                        float dd = ::length(metroDoor(g, c, station, side, k, 0.6f).xy() - bp.xy());
                        if (dd < bd) {
                            bd = dd;
                            boardDoor = k;
                        }
                    }
                    setGoto(g, bagman, metroDoor(g, c, station, side, boardDoor, 0.6f), 2.6f);
                    boardT = 0.f;
                    metroPhase = 3;
                } else if (waitT > 200.f) {
                    // no train is coming: down the stair and on foot to the precinct
                    walk.start(g, bagman, metroStairPath(station, side, false, 0.f), 3.4f);
                    dest = station;
                    metroPhase = 5;
                }
                break;
            }
            case 3: {   // to the door and aboard
                boardT += dt;
                if (!isMetroCar(g, train) || g.vehicles[train].uid != trainUid || metroCarStation(g, train) != station) {
                    // the doors closed on him: the next train
                    setIdle(g, bagman, 0);
                    metroPhase = 2;
                    break;
                }
                vec3 dp = metroDoor(g, train, station, side, boardDoor, 0.6f);
                float toDoor = ::length(dp.xy() - bp.xy());
                if (toDoor > 1.1f && (boardT < 5.f || toDoor > 6.f)) {
                    if (g.peds[bagman].brain.type != BRAIN_GOTO) setGoto(g, bagman, dp, 3.2f);   // he runs for the doors, whatever happens
                    break;
                }
                int seat = metroFreeSeat(g, train);
                if (seat < 0) {
                    metroPhase = 2;
                    break;
                }
                g.warpPedIntoVehicle(bagman, train, seat);
                g.peds[bagman].invincible = true;
                g.peds[bagman].brain.type = BRAIN_NONE;
                stopT = 0.f;
                g.mBlipVehicle(train, UI::BLIP_ENEMY);
                g.mObjective("The ~r~bagman~s~ is on the SkyLine. Ride his train, or beat it to ~y~Solaris station~s~.");
                sayMe(g, "[angry:0.4]He's on the train. Fine. I can do trains.");
                LOG("[bagman] boarded a SkyLine car at %s (seat %d)", stationAt(station).name.c_str(), seat);
                metroPhase = 4;
                break;
            }
            case 4: {   // riding to the next station
                if (!isMetroCar(g, train) || g.vehicles[train].uid != trainUid || g.peds[bagman].vehicle != train)
                    return fail("The bagman got away on the SkyLine.");
                if (!rideLine && isMetroCar(g, g.playerVehicle())) {
                    rideLine = true;
                    sayMe(g, "[whisper:0.5]Same train. Easy. Wait for the doors.");
                }
                int sd = -1;
                int s2 = metroCarStation(g, train, &sd);
                if (s2 < 0 || s2 == station) {
                    stopT = 0.f;
                    break;
                }
                stopT += dt;
                if (stopT < 1.8f) break;
                // off onto the platform, down to the street, on to the precinct
                vec3 out = metroDoor(g, train, s2, sd, boardDoor, 0.9f);
                unblipVehicle(train);
                placePed(g, bagman, out, yawTo(vehPos(g, train).xy(), out.xy()));
                g.peds[bagman].invincible = false;
                g.mBlipPed(bagman, UI::BLIP_ENEMY);
                walk.start(g, bagman, metroStairPath(s2, sd, false, dot(out.xy() - stationAt(s2).pos, stationAt(s2).dir)), 3.4f);
                dest = s2;
                g.mObjective("Stop the ~r~bagman~s~ before he reaches the precinct!");
                sayMe(g, "[shout:0.4]There he is! Off at Solaris!");
                LOG("[bagman] off the train at %s", stationAt(s2).name.c_str());
                metroPhase = 5;
                break;
            }
            case 5:   // down to the street
                if (walk.update(g, dt)) {
                    setGoto(g, bagman, P.policeHq.door, 4.2f);
                    metroPhase = 6;
                }
                break;
            case 6:   // running for the precinct door
                if (::length(bp.xy() - P.policeHq.door.xy()) < 3.f) return fail("The bagman made it to the precinct.");
                if (g.peds[bagman].brain.type != BRAIN_GOTO) setGoto(g, bagman, P.policeHq.door, 4.2f);
                break;
        }
        return MS_RUNNING;
    }

    MissionStatus update(GameWorld& g, float dt) override {
        switch (stage) {
            case 0: {
                if (vehicleLost(g, bagCar, "bagman's car")) return MS_FAILED;
                vec3 bp = pedAlive(g, bagman) && g.peds[bagman].vehicle < 0 ? pedPos(g, bagman) : vehPos(g, bagCar);
                int r = tail.update(g, bp, 14.f, 150.f, dt, 12.f, 6.f);
                if (r == 1) return fail("You lost the bagman.");
                if (r == 2) return fail("The bagman spotted you.");
                ScriptDriver* d = driverFor(bagCar);
                if (phase == 0 && d && d->done) {
                    // out of the car, to the door, back
                    g.removePedFromVehicle(bagman, true);
                    const Place& sp = stopPlace[Min(stopIndex, 1)];
                    door = sp.door;
                    setGoto(g, bagman, door, 1.4f);
                    phase = 1;
                    timer = 0.f;
                } else if (phase == 1) {
                    timer += dt;
                    if (::length(pedPos(g, bagman) - door) < 1.5f || timer > 14.f) {
                        setIdle(g, bagman, 7);
                        if (timer > 16.f) {
                            setGoto(g, bagman, vehPos(g, bagCar), 1.4f);
                            phase = 2;
                        }
                    }
                } else if (phase == 2) {
                    if (::length(pedPos(g, bagman) - vehPos(g, bagCar)) < 3.f || g.peds[bagman].brain.timer > 20.f) {
                        g.warpPedIntoVehicle(bagman, bagCar, 0);
                        g.peds[bagman].brain.type = BRAIN_NONE;
                        stopIndex++;
                        phase = 0;
                        if (stopIndex < (int)stops.size()) {
                            driveTo(g, stops[stopIndex], 13.f, false);
                            sayMe(g, stopIndex == 1 ? "[whisper:0.5]One down. Where to next, pal?" : "");
                        } else {
                            cp(g, 2);
                            lastLeg(g);
                        }
                    }
                }
                if (stageTime > 18.f && stageTime < 18.1f) sayMe(g, "[whisper:0.5]Nice and easy. Just another car in traffic.");
                break;
            }
            case 1:
                if (stageTime > 6.f && stageTime < 6.1f) sayMe(g, "[angry:0.4]Time to repossess some dirty money.");
                return station >= 0 ? metroChase(g, dt) : carChase(g);
            case 2:
                setStage(3);
                break;
            case 3:
                if (bag >= 0 && stageTime < 0.1f) {
                    g.mClearBlips();
                    goTo(g, g.pickups[bag].pos.toVec3(), 1.5f, "Grab the ~g~bag~s~.", false, false);
                }
                if (packageTaken(g, bag) || grabNear(g, bag, 1.6f)) {
                    g.mClearTarget();
                    sayMe(g, "[happy]Got it. Oh, that's a lot of money.");
                    setWanted(g, 3);
                    g.mObjective("Lose the ~b~police~s~.");
                    score(SC_CHASE, 1.f, 3);
                    cp(g, 1);
                    setStage(6);
                }
                break;
            case 6:
                if (stageTime > 8.f && stageTime < 8.1f) sayMe(g, "[happy:0.5]Holt's going to be real upset with somebody tonight.");
                if (g.pinfo.wanted == 0) {
                    score(SC_NOIR, 0.35f, 3);
                    goTo(g, gPlaces.rookShop.curb, 5.f, "Take the money to ~y~Rook's salvage yard~s~.", true);
                    next();
                }
                break;
            case 7:
                if (g.pinfo.wanted > 0) {
                    g.mObjective("Lose the ~b~police~s~.");
                    break;
                }
                g.mObjective("Take the money to ~y~Rook's salvage yard~s~.");
                if (arrived(g)) {
                    clearGoal(g);
                    phoneLine(g, CAST_ROOK, "[happy:0.6]Two hundred grand of Holt's collections, and a ledger with every name that pays her. Kit's going to cry.");
                    sayMe(g, "[calm]Split it with the neighborhood. Lucha's roof needs fixing.");
                    next();
                }
                break;
            case 8:
                if (!g.mTalking()) {
                    g.storyBriefText = "Dex robbed Holt's bagman. The collections ledger names every business paying for protection - and "
                                       "shows the money going to Solaris Pier Holdings.";
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
                if (phase == 0 && t.stageTime > 3.f) fastForwardDriver(g, bagCar, 25.f, dt);
                if (bagCar >= 0 && t.stageTime > 0.5f) {
                    vec3 bp = vehPos(g, bagCar);
                    vec3 f = g.vehicles[bagCar].sim.forward();
                    vec3 want = bp - f * 50.f;
                    float d = ::length(playerPos(g) - bp);
                    if (d > 100.f || d < 20.f) t.teleport(vec3(want.x, want.y, groundAt(g, want.x, want.y)), yawTo(want.xy(), bp.xy()));
                }
                break;
            case 1:
                if (station < 0) {
                    if (t.stageTime > 2.f && bagCar >= 0) g.damageVehicle(bagCar, 800.f, g.player, vec3(0), vec3(0));
                    break;
                }
                // the SkyLine leg played out in full: follow him onto his train, get off with him, take him down
                if (metroPhase != testShotPhase && metroPhase > 0) {
                    testShotPhase = metroPhase;
                    t.screenshot(StrFormat("metro%d", metroPhase).c_str());
                }
                if (metroPhase == 0) {
                    fastForwardDriver(g, bagCar, 25.f, dt);
                    if (bagCar >= 0 && fmodf(t.stageTime, 1.f) < dt) {
                        vec3 bp = vehPos(g, bagCar);
                        vec3 want = bp - g.vehicles[bagCar].sim.forward() * 45.f;
                        float d = ::length(playerPos(g) - bp);
                        if (d > 90.f || d < 20.f) t.teleport(vec3(want.x, want.y, groundAt(g, want.x, want.y)), yawTo(want.xy(), bp.xy()));
                    }
                } else if ((metroPhase == 1 || metroPhase == 5) && fmodf(t.stageTime, 1.f) < dt && walk.wp < (int)walk.pts.size()) {
                    placePed(g, bagman, walk.pts[walk.wp], 0.f);   // skip ahead along the stair walk
                } else if (metroPhase == 4 && !isMetroCar(g, g.playerVehicle()) && t.stageTime > 1.f) {
                    int seat = metroFreeSeat(g, train);
                    if (seat >= 0) t.enter(train, seat);
                } else if (metroPhase >= 5) {
                    if (isMetroCar(g, g.playerVehicle())) t.exitVehicle();
                    else if (fmodf(t.stageTime, 1.f) < dt) {
                        t.teleportNear(pedPos(g, bagman).xy(), 4.f);
                        t.shoot(bagman);
                    }
                }
                break;
            case 3:
                if (bag >= 0 && !packageTaken(g, bag) && t.stageTime > 0.5f) {
                    t.exitVehicle();
                    t.teleport(g.pickups[bag].pos.toVec3(), 0.f);
                }
                break;
            case 6:
                if (t.stageTime > 2.f) {
                    g.pinfo.wanted = 0;
                    g.pinfo.wantedHeat = 0.f;
                }
                break;
            case 7:
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) t.enter(spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f));
                testGoal(g, t, dt);
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-4: "Sawgrass Run" (Dex, with Jonah). Airboat chase through the Sawgrass after the Cuervos' gun runners.
class MissionSawgrassRun : public StoryMission {
public:
    int jonah = -1, boat = -1;
    std::vector<int> runners;
    vec3 hammock;
    int destroyed = 0;
    const char* title() const override { return "Sawgrass Run"; }
    const char* brief() const override {
        return "Jonah Pike says the Cuervos run guns through the Sawgrass by airboat. Take his airboat, sink theirs, and find out where "
               "the guns come from.";
    }
    long long reward() const override { return 8000; }

    void spawnRunners(GameWorld& g) {
        std::vector<vec3> loop = waterLoop(g, gPlaces.sawgrassDeep, 320.f, 10, 0.6f, 0.4f);
        if (loop.size() < 4) loop = waterLoop(g, gPlaces.sawgrassWater, 260.f, 10, 0.5f, 0.1f);
        RoutePath path;
        buildWaypointPath(loop, path, 18.f, true, true);
        for (int i = 0; i < 3 && path.pts.size() > 3; i++) {
            float s = path.length() * i / 3.f;
            vec2 tan;
            vec3 p = path.at(s, nullptr, &tan);
            int v = spawnAirboat(g, p, atan2f(-tan.x, tan.y), lin(0.3f, 0.3f, 0.2f), 1);
            if (v < 0) continue;
            for (int s2 = 0; s2 < 2; s2++) {
                int e = spawnCast(g, CAST_THUG_A + (i + s2) % 4, p, 0.f, FAC_ENEMY);
                if (e < 0) continue;
                int seats = (int)g.vassets[g.vehicles[v].model].spec.seats.size();
                if (s2 >= seats) {
                    g.despawnPed(e);
                    continue;
                }
                g.warpPedIntoVehicle(e, v, s2);
                arm(g, e, s2 == 0 ? WPN_PISTOL : WPN_SMG);
                g.peds[e].brain.accuracy = 0.2f;
                if (s2 > 0) setCombat(g, e, g.player, 0.2f);
                enemies.push_back(e);
            }
            ScriptDriver& d = addDriver(g, v, path, 14.f + i * 1.5f, DRV_WATER);
            d.loop = true;
            d.along = s;
            runners.push_back(v);
            g.mBlipVehicle(v, UI::BLIP_ENEMY);
        }
        vec3 c = gPlaces.sawgrassDeep;
        // the hammock: dry land near the loop center
        hammock = c;
        for (float r = 0.f; r < 400.f; r += 10.f) {
            bool found = false;
            for (int k = 0; k < 12; k++) {
                vec2 q = c.xy() + vec2(cosf(k * 0.52f), sinf(k * 0.52f)) * r;
                if (!g.map->isWater(q.x, q.y)) {
                    hammock = vec3(q, groundAt(g, q.x, q.y));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        vec3 dock = gPlaces.sawgrassDock;
        vec3 w = gPlaces.sawgrassWater;
        score(SC_NOIR, 0.3f, 7);
        g.env->timeOfDay = Clamp(g.env->timeOfDay, 16.5f, 19.f);
        boat = spawnAirboat(g, w, yawTo(dock.xy(), w.xy()), lin(0.2f, 0.45f, 0.25f));
        jonah = spawnCast(g, CAST_JONAH, dock, yawTo(dock.xy(), w.xy()), FAC_FRIEND);
        if (jonah >= 0) {
            g.peds[jonah].maxHealth = g.peds[jonah].health = 400.f;
            arm(g, jonah, WPN_RIFLE, 10);
        }
        buddy = jonah;
        if (checkpoint >= 1) {
            placePlayer(g, dock, yawTo(dock.xy(), w.xy()));
            if (boat >= 0) {
                g.warpPedIntoVehicle(g.player, boat, 0);
                int seat = g.freeSeat(boat, false);
                if (seat > 0 && jonah >= 0) g.warpPedIntoVehicle(jonah, boat, seat);
            }
            beginChase(g);
            return;
        }
        placePlayer(g, dock + vec3(3.f, 0, 0), yawTo(dock.xy() + vec2(3.f, 0), dock.xy()));
        vec3 jp = pedPos(g, jonah), dp = playerPos(g);
        facePed(g, jonah, dp);
        std::vector<CutsceneShot> shots;
        establish(g, shots, jp, yawTo(w.xy(), dock.xy()), 45.f, 14.f, 4.5f, 55.f);
        shots.push_back(shotTwo(jp, dp, 7.f));
        shots.push_back(shotOver(dp, jp, 7.f));
        shots.push_back(shotOver(jp, dp, 6.f, -1.f));
        shots.push_back(shotTwo(jp, dp, 6.f, 5.f, 45.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_JONAH, jonah, "[happy:0.5]Well, look what the tide dragged in. The Coast Guard's favorite disappointment.");
        sayMe(g, "[happy:0.3]Hello, Jonah. You look old.");
        say(g, CAST_JONAH, jonah, "[sad:0.6]I am old. Still alive, though, and that's partly your doing. Don't make that face. I never blamed you for that night.");
        say(g, CAST_JONAH, jonah, "[angry:0.4]The Cuervos run airboats through my swamp every evening. Crates of rifles, from a boat in the Gulf.");
        sayMe(g, "[angry:0.5]Guns for Sandoval's little war on Calle Luna. Let's go sink some airboats.");
    }

    void beginChase(GameWorld& g) {
        spawnRunners(g);
        g.mObjective("Sink the ~r~Cuervo airboats~s~.");
        score(SC_CHASE, 0.9f, 7);
        cp(g, 1);
        setStage(2);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (allyDown(g, jonah, "Jonah")) return MS_FAILED;
        if (stage >= 3 && ::length(playerPos(g) - hammock) < 300.f) drawCrates(g, hammock + vec3(1.2f, 0.8f, 0.f), 0.6f, 7, 0x5A96u);
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.mBlipVehicle(boat, UI::BLIP_BOAT);
                    g.mObjective("Get in the ~b~airboat~s~.");
                    setFollow(g, jonah, g.player);
                    next();
                }
                break;
            case 1:
                if (vehicleLost(g, boat, "airboat")) return MS_FAILED;
                if (g.playerInVehicle(boat)) {
                    g.mClearBlips();
                    int seat = g.freeSeat(boat, false);
                    if (seat > 0 && jonah >= 0 && g.peds[jonah].vehicle != boat) g.warpPedIntoVehicle(jonah, boat, seat);
                    say(g, CAST_JONAH, jonah, "[shout:0.5]There. Three boats running dark, heading for the old channel. Get me close, I'll do the rest.");
                    beginChase(g);
                }
                break;
            case 2: {
                updateBuddy(g, 60.f);
                destroyed = 0;
                for (int v : runners) {
                    bool dead = vehicleDisabled(g, v);
                    if (!dead) {
                        dead = true;
                        for (int s = 0; s < 2; s++) {
                            int p = g.vehicles[v].seats[s];
                            if (p >= 0 && pedAlive(g, p)) dead = false;
                        }
                    }
                    if (dead) {
                        destroyed++;
                        releaseDriver(g, v);
                    }
                }
                g.missionCounterLabel = "AIRBOATS";
                g.missionCounter = destroyed;
                g.missionCounterMax = (int)runners.size();
                if (stageTime > 12.f && stageTime < 12.1f) say(g, CAST_JONAH, jonah, "[shout]Keep her steady! I can't hit a gator at this speed!");
                if (stageTime > 30.f && stageTime < 30.1f) sayMe(g, "[shout:0.6]You taught me to drive one of these, remember? Hold on.");
                if (destroyed >= (int)runners.size() || runners.empty()) {
                    g.missionCounterLabel.clear();
                    g.mClearBlips();
                    say(g, CAST_JONAH, jonah, "[calm]That's all of them. Their cargo came off that hammock over there. Let's have a look.");
                    goTo(g, hammock, 6.f, "Search the ~y~hammock~s~ for the Cuervos' stash.");
                    score(SC_NOIR, 0.35f, 7);
                    next();
                }
                break;
            }
            case 3:
                updateBuddy(g);
                if (arrived(g)) {
                    clearGoal(g);
                    std::vector<CutsceneShot> shots;
                    shots.push_back(shotMove(hammock + vec3(3.f, -3.f, 1.6f), hammock + vec3(0, 0, 0.4f), hammock + vec3(2.2f, -2.2f, 1.2f),
                                             hammock + vec3(0, 0, 0.3f), 4.f, 40.f));
                    shots.push_back(shotArc(hammock, 9.f, 3.f, 0.4f, 1.3f, 7.f));
                    g.mCutscene(shots);
                    say(g, CAST_JONAH, jonah, "[angry:0.4]Rifles. Enough for a small war. And look at the stencil on the crates.");
                    sayMe(g, "[angry:0.6]Solaris Pier Construction. He's not even hiding it.");
                    say(g, CAST_JONAH, jonah, "[sad:0.4]Men like Sandoval never think they need to hide.");
                    next();
                }
                break;
            case 4:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.storyBriefText = "Dex and Jonah sank the Cuervos' airboats in the Sawgrass. The rifle crates were stamped Solaris Pier "
                                       "Construction: Sandoval is arming the gang himself.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && boat >= 0) t.enter(boat);
                break;
            case 2:
                if (t.stageTime > 2.f) {
                    for (int v : runners) t.destroy(v);
                    t.killEnemies();
                }
                break;
            case 3:
                if (t.stageTime > 0.5f) {
                    t.exitVehicle();
                    t.teleport(hammock, 0.f);
                }
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-5: "Riptide" (Mari). Boat chase across the bay after Sandoval's cash boat; fish the money out of the water.
class MissionRiptide : public StoryMission {
public:
    int tomas = -1, boat = -1, cashBoat = -1;
    std::vector<int> bags;
    const char* title() const override { return "Riptide"; }
    const char* brief() const override {
        return "Kit found Sandoval's cash run: a speedboat carries his money from Port Isle to Key Coral every week. Take the fastest boat at "
               "the yard and stop it in the bay.";
    }
    long long reward() const override { return 10000; }

    void spawnCashBoat(GameWorld& g) {
        std::vector<vec3> wps;
        const Places& P = gPlaces;
        wps.push_back(P.portWater);
        vec3 w;
        if (findWater(g, vec2(4750.f, -1200.f), 3.f, w)) wps.push_back(w);
        if (findWater(g, vec2(4600.f, -2300.f), 3.f, w)) wps.push_back(w);
        wps.push_back(P.keyCoralWater);
        RoutePath path;
        buildWaypointPath(wps, path, 26.f, true, false);
        vec2 tan;
        vec3 p = path.at(0.f, nullptr, &tan);
        cashBoat = spawnBoat(g, p, atan2f(-tan.x, tan.y), lin(0.9f, 0.9f, 0.92f), 2);
        if (cashBoat < 0) return;
        g.vehicles[cashBoat].sim.health = 900.f;
        for (int s = 0; s < 2; s++) {
            int e = spawnCast(g, s == 0 ? CAST_GUARD_A : CAST_GUARD_B, p, 0.f, FAC_ENEMY);
            if (e < 0) continue;
            if (s >= (int)g.vassets[g.vehicles[cashBoat].model].spec.seats.size()) {
                g.despawnPed(e);
                continue;
            }
            g.warpPedIntoVehicle(e, cashBoat, s);
            arm(g, e, WPN_SMG);
            g.peds[e].brain.accuracy = 0.2f;
            if (s > 0) setCombat(g, e, g.player, 0.2f);
            enemies.push_back(e);
        }
        ScriptDriver& d = addDriver(g, cashBoat, path, 24.f, DRV_WATER);
        d.rubberPed = g.player;
        d.rubberGap = 90.f;
        g.mBlipVehicle(cashBoat, UI::BLIP_ENEMY);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        score(SC_NOIR, 0.3f, 8);
        boat = spawnBoat(g, P.riverLaunch, yawTo(P.riverLaunch.xy(), P.riverMouth.xy()), lin(0.85f, 0.2f, 0.45f));
        if (checkpoint >= 1) {
            vec3 w;
            findWater(g, P.portWater.xy() - vec2(250.f, 0.f), 2.f, w);
            placePlayer(g, w, yawTo(w.xy(), P.portWater.xy()));
            if (boat >= 0) {
                teleportVehicle(g, boat, w, yawTo(w.xy(), P.portWater.xy()));
                g.warpPedIntoVehicle(g.player, boat, 0);
            }
            beginChase(g);
            return;
        }
        tomas = spawnCast(g, CAST_TOMAS, P.boatyard.door, P.boatyard.yaw, FAC_FRIEND);
        if (tomas >= 0) g.peds[tomas].invincible = true;
        placePlayer(g, placeOffset(g, P.boatyard, 2.f, 3.f), P.boatyard.yaw + kPi);
        vec3 tp = pedPos(g, tomas), mp = playerPos(g);
        facePed(g, tomas, mp);
        facePed(g, g.player, tp);
        std::vector<CutsceneShot> shots;
        if (boat >= 0) shots.push_back(shotMove(P.riverLaunch + vec3(12.f, -10.f, 4.f), P.riverLaunch, P.riverLaunch + vec3(8.f, -12.f, 3.f), P.riverLaunch, 5.f, 45.f));
        shots.push_back(shotTwo(tp, mp, 7.f));
        shots.push_back(shotOver(mp, tp, 6.f));
        shots.push_back(shotOver(tp, mp, 6.f, -1.f));
        g.mCutscene(shots);
        say(g, CAST_TOMAS, tomas, "Kit called. The cash boat leaves Port Isle at nine, heads for Key Coral. Two guards, fast hull.");
        sayMe(g, "[happy:0.4]Faster than Dad's old racer? I rebuilt that engine twice.");
        say(g, CAST_TOMAS, tomas, "[sad]Nothing's faster than Dad's old racer. Just bring it back in one piece, okay? It's all we have left of him.");
        sayMe(g, "[calm]I'll bring it back. And a little something extra.");
    }

    void beginChase(GameWorld& g) {
        g.mClearTarget();
        g.mClearMarkers();
        spawnCashBoat(g);
        g.mObjective("Stop the ~r~cash boat~s~ before it reaches Key Coral.");
        score(SC_CHASE, 0.95f, 8);
        cp(g, 1);
        setStage(3);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (vehicleLost(g, boat, "boat")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.mBlipVehicle(boat, UI::BLIP_BOAT);
                    g.mObjective("Get in your ~b~boat~s~.");
                    next();
                }
                break;
            case 1:
                if (g.playerInVehicle(boat)) {
                    g.mClearBlips();
                    goTo(g, gPlaces.portWater, 40.f, "Head down the river to ~y~Port Isle~s~.", false, false);
                    next();
                }
                break;
            case 2:
                if (abandoned(g, boat, 120.f, "boat")) return MS_FAILED;
                if (stageTime > 10.f && stageTime < 10.1f) sayMe(g, "[shout:0.5]Come on, old girl. Show them what an Ortega engine can do.");
                if (g.playerAt(gPlaces.portWater.xy(), 320.f)) beginChase(g);
                break;
            case 3: {
                if (abandoned(g, boat, 150.f, "boat")) return MS_FAILED;
                if (cashBoat < 0) return fail("The cash boat got away.");
                ScriptDriver* d = driverFor(cashBoat);
                if (d && d->done) return fail("The cash boat reached Key Coral.");
                if (::length(vehPos(g, cashBoat) - playerPos(g)) > 600.f) return fail("The cash boat got away.");
                if (stageTime > 5.f && stageTime < 5.1f) sayMe(g, "[shout:0.5]There you are. Let's see what you've got.");
                bool dead = vehicleDisabled(g, cashBoat) || g.vehicles[cashBoat].sim.health < 350.f;
                if (!dead) {
                    dead = true;
                    for (int s = 0; s < 2; s++) {
                        int p = g.vehicles[cashBoat].seats[s];
                        if (p >= 0 && pedAlive(g, p)) dead = false;
                    }
                }
                if (dead) {
                    releaseDriver(g, cashBoat);
                    g.vehicles[cashBoat].ctl = Vehicles::VehicleControls();
                    g.mClearBlips();
                    vec3 cp3 = vehPos(g, cashBoat);
                    for (int i = 0; i < 3; i++) {
                        float a = i * 2.1f;
                        vec3 p = cp3 + vec3(cosf(a) * 9.f, sinf(a) * 9.f, 0.f);
                        float wz = g.map->waterAt(p.x, p.y);
                        p.z = wz > World::kNoWater + 1.f ? wz + 0.1f : cp3.z;
                        bags.push_back(spawnPackage(g, p));
                    }
                    sayMe(g, "[shout]Money overboard! Grab the bags before they sink.");
                    g.mObjective("Collect the floating ~g~cash bags~s~.");
                    score(SC_NOIR, 0.4f, 8);
                    next();
                }
                break;
            }
            case 4: {
                if (abandoned(g, boat, 150.f, "boat")) return MS_FAILED;
                int got = 0;
                for (int b : bags) got += grabNear(g, b, 4.5f) ? 1 : 0;
                g.missionCounterLabel = "CASH BAGS";
                g.missionCounter = got;
                g.missionCounterMax = (int)bags.size();
                g.mClearMarkers();
                for (int b : bags)
                    if (!packageTaken(g, b)) g.mMarker(g.pickups[b].pos, 1.5f, vec3(0.3f, 1.f, 0.4f));
                if (got >= (int)bags.size()) {
                    g.missionCounterLabel.clear();
                    g.mClearMarkers();
                    goTo(g, gPlaces.riverLaunch, 12.f, "Bring the boat back to the ~y~boatyard~s~.", false, false);
                    sayMe(g, "[happy:0.6]Sandoval's paying for the new roof on the diner. He just doesn't know it yet.");
                    next();
                }
                break;
            }
            case 5:
                if (abandoned(g, boat, 150.f, "boat")) return MS_FAILED;
                if (arrived(g)) {
                    clearGoal(g);
                    g.storyBriefText = "Mari sank Sandoval's cash run in the bay and brought her father's racer home in one piece. The "
                                       "money is going back into Calle Luna.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1: if (t.stageTime > 0.5f && boat >= 0) t.enter(boat); break;
            case 2: if (t.stageTime > 0.5f) t.teleportNear(gPlaces.portWater.xy(), 60.f); break;
            case 3:
                if (cashBoat >= 0 && t.stageTime > 1.f) {
                    vec3 cp3 = vehPos(g, cashBoat);
                    if (::length(playerPos(g) - cp3) > 80.f) t.teleportNear(cp3.xy(), 40.f);
                    if (t.stageTime > 3.f) g.damageVehicle(cashBoat, 700.f, g.player, vec3(0), vec3(0));
                }
                break;
            case 4:
                for (int b : bags)
                    if (!packageTaken(g, b) && t.stageTime > 0.5f) {
                        t.teleport(g.pickups[b].pos.toVec3(), 0.f);
                        break;
                    }
                break;
            case 5:   // the slip itself is water: put the boat on it (once a second, it has to settle)
                if (t.stageTime > 0.5f && fmodf(t.stageTime, 1.f) < dt) t.teleport(gPlaces.riverLaunch, mu::yawTo(playerPos(g).xy(), gPlaces.riverLaunch.xy()));
                break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-6: "Heavy Lift" (Dex). Sneak into the Port Isle container terminal at night, steal the truck carrying
// Sandoval's container from under the gantry cranes and drive it out through the gate.
class MissionHeavyLift : public StoryMission {
public:
    int truck = -1;
    StealthGroup yard;
    bool alarmed = false;
    const char* title() const override { return "Heavy Lift"; }
    const char* brief() const override {
        return "A container of Sandoval's 'construction materials' sits on a truck under the gantry cranes at Port Isle. Get into the "
               "terminal, take the truck and deliver it to Rook.";
    }
    long long reward() const override { return 15000; }

    void spawnYard(GameWorld& g) {
        const Places& P = gPlaces;
        int model = pickModel(g, {Vehicles::VC_TRUCK, Vehicles::VC_VAN, Vehicles::VC_PICKUP}, 0);
        truck = spawnCar(g, model, P.portTruck, 0.f, lin(0.12f, 0.3f, 0.7f));
        if (truck >= 0) g.vehicles[truck].sim.engineOn = false;
        struct G2 {
            vec2 a, b;
        };
        const G2 routes[6] = {{vec2(4070, -300), vec2(4070, -520)}, {vec2(4290, -290), vec2(4500, -290)}, {vec2(4520, -250), vec2(4520, -500)},
                              {vec2(4290, -540), vec2(4100, -540)}, {vec2(4450, -380), vec2(4450, -300)}, {vec2(4030, -160), vec2(4030, -200)}};
        for (int i = 0; i < 6; i++) {
            vec3 a = vec3(routes[i].a, groundAt(g, routes[i].a.x, routes[i].a.y, 10.f));
            vec3 b = vec3(routes[i].b, groundAt(g, routes[i].b.x, routes[i].b.y, 10.f));
            int e = spawnCast(g, i & 1 ? CAST_GUARD_B : CAST_DOCKER, a, yawTo(a.xy(), b.xy()), FAC_ENEMY);
            if (e < 0) continue;
            arm(g, e, i % 3 == 0 ? WPN_SMG : WPN_PISTOL);
            g.peds[e].brain.accuracy = 0.3f;
            setIdle(g, e, 0);
            enemies.push_back(e);
            std::vector<vec3> patrol;
            if (i != 5) patrol = {a, b};
            yard.add(g, e, patrol, 16.f);
        }
    }

    void raiseAlarm(GameWorld& g) {
        if (alarmed) return;
        alarmed = true;
        yard.raise(g);
        blipEnemies(g);
        // security SUVs from the gate
        int model = pickModel(g, {Vehicles::VC_SUV, Vehicles::VC_PICKUP}, 3);
        for (int k = 0; k < 2; k++) {
            vec3 sp = vec3(4060.f, -600.f - k * 180.f, 0.f);
            sp.z = groundAt(g, sp.x, sp.y, 10.f);
            std::vector<int> crew;
            int v = spawnCrewCar(g, model, sp, 0.f, castChar(g, CAST_GUARD_A), 1, castChar(g, CAST_GUARD_B), FAC_ENEMY, WPN_SMG, 0.22f, &crew);
            if (v < 0) continue;
            g.vehicles[v].color0 = lin(0.9f, 0.9f, 0.9f);
            for (int p : crew) {
                setCombat(g, p, g.player, 0.22f);
                enemies.push_back(p);
            }
            enemyCars.push_back(v);
        }
        score(SC_CHASE, 1.f, 9);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        if (g.env->timeOfDay > 5.f && g.env->timeOfDay < 21.f) g.env->timeOfDay = 22.5f;
        spawnYard(g);
        score(SC_STEALTH, 0.35f, 9);
        if (checkpoint >= 1) {
            placePlayer(g, P.portTruck, 0.f);
            if (truck >= 0) g.warpPedIntoVehicle(g.player, truck, 0);
            raiseAlarm(g);
            g.mObjective("Get the truck out of ~y~Port Isle~s~.");
            g.mTarget(P.portGate.curb.xy() - vec2(250.f, 0.f));
            setStage(3);
            return;
        }
        placePlayer(g, P.portGate.curb - vec3(120.f, 0, 0), -kPi * 0.5f);
        std::vector<CutsceneShot> shots;
        vec3 crane(4570.f, -380.f, 3.f);
        shots.push_back(shotMove(crane + vec3(-160.f, 60.f, 45.f), crane + vec3(0, 0, 40.f), crane + vec3(-140.f, 20.f, 30.f), crane + vec3(0, 0, 30.f), 5.f, 50.f));
        if (truck >= 0) shots.push_back(shotVehicle(g, truck, 5.f, 1.f, 45.f));
        g.mCutscene(shots);
        phoneLine(g, CAST_ROOK, "[calm]Blue cab under the gantry cranes, container already on the chassis. Terminal security walks the lanes.");
        phoneLine(g, CAST_ROOK, "[calm]Get in quiet. Once that engine starts, every guard on the island will know.");
        sayMe(g, "[whisper:0.5]Quiet is my middle name.");
        sayMe(g, "[whisper:0.5]It's actually Raymond. Don't tell anyone.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, truck, "truck")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.mBlipVehicle(truck, UI::BLIP_VEHICLE);
                    g.mObjective("Sneak into the terminal and steal the ~b~truck~s~ under the gantry cranes.");
                    next();
                }
                break;
            case 1:
                if (!alarmed && playerPos(g).x > 3960.f && yard.update(g, dt)) {
                    say(g, CAST_GUARD_A, yard.spotter, "[shout]Intruder in the yard!");
                    raiseAlarm(g);
                    g.mObjective("Get to the ~b~truck~s~!");
                }
                showMeter(g, "DETECTION", alarmed ? 0.f : yard.meter);
                if (g.playerInVehicle(truck)) {
                    showMeter(g, "DETECTION", 0.f);
                    sayMe(g, "[shout:0.4]Engine's loud. Here they come.");
                    raiseAlarm(g);
                    g.mClearBlips();
                    blipEnemies(g);
                    g.mObjective("Get the truck out of ~y~Port Isle~s~.");
                    g.mTarget(gPlaces.portGate.curb.xy() - vec2(250.f, 0.f));
                    cp(g, 1);
                    setStage(3);
                }
                break;
            case 3: {
                if (abandoned(g, truck, 150.f, "truck")) return MS_FAILED;
                for (int v : enemyCars) {
                    int drv = g.driverOf(v);
                    if (drv >= 0 && pedAlive(g, drv)) g.peds[drv].brain.type = BRAIN_COMBAT;
                }
                if (stageTime > 5.f && stageTime < 5.1f) sayMe(g, "[shout:0.5]Big truck, small gate. This is going to hurt.");
                vec3 pp = playerPos(g);
                bool offIsland = pp.x < 3860.f || !g.playerAt(vec2(4280.f, -470.f), 900.f);
                if (offIsland && g.playerInVehicle(truck)) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e) && ::length(pedPos(g, e) - pp) > 60.f) setFlee(g, e, g.player);
                    score(SC_NOIR, 0.45f, 9);
                    goTo(g, gPlaces.rookShop.curb, 6.f, "Deliver the truck to ~y~Rook's yard~s~.", true);
                    next();
                }
                break;
            }
            case 4:
                if (abandoned(g, truck, 150.f, "truck")) return MS_FAILED;
                if (g.playerVehicle() != truck && g.hudHelpTimer <= 0.f) g.help("Get back in the ~b~truck~s~.", 2.f);
                if (arrived(g) && g.playerInVehicle(truck)) {
                    clearGoal(g);
                    float cond = Saturate(g.vehicles[truck].sim.health / 1000.f);
                    bonus = (long long)(cond * 5000.f);
                    phoneLine(g, CAST_ROOK, "[calm]Let's see what's inside... Cash, and ledgers. Solaris Pier Holdings paying the Cuervos, paying Holt.");
                    phoneLine(g, CAST_ROOK, "[happy:0.5]This is the first piece of real paper we've got on Sandoval himself. Good work, Dex.");
                    next();
                }
                break;
            case 5:
                if (!g.mTalking()) {
                    g.storyBriefText = "The container held Solaris Pier Holdings' books: payments to the Cuervos and to Captain Holt. For the "
                                       "first time there is paper with Sandoval's name on it.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }
    long long bonus = 0;
    void finish(GameWorld& g, bool passed) override {
        showMeter(g, "DETECTION", 0.f);
        if (passed && bonus > 0) money(g, bonus);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (t.stageTime > 0.5f && truck >= 0) {
                    t.teleport(vehPos(g, truck) + vec3(-3.f, 0, 0), 0.f);
                    t.enter(truck);
                }
                break;
            case 3:
                if (t.stageTime > 0.5f) t.driveToward(gPlaces.portGate.curb.xy() - vec2(300.f, 0.f), 40.f, dt);
                if (t.stageTime > 2.f) t.killEnemies(80.f);
                break;
            case 4: testGoal(g, t, dt, 60.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-7: "Fireworks" (Mari, with Kit). Sandoval's Solaris Pier launch party on the Sol Beach Pier: escort Kit to the
// end of the pier, protect her while she records Sandoval and Holt, then get out.
class MissionFireworks : public StoryMission {
public:
    int kit = -1, sandoval = -1, holt = -1, ski = -1, car = -1;
    std::vector<int> crowd;
    float fwTimer = 0.f;
    u32 fwSeed = 1;
    int waveSent = 0;
    vec3 podium;
    const char* title() const override { return "Fireworks"; }
    const char* brief() const override {
        return "Sandoval launches Solaris Pier with a party on the Sol Beach Pier. Kit wants him and Holt on tape together. Keep her safe "
               "while she records, then get her out.";
    }
    long long reward() const override { return 9000; }

    void spawnParty(GameWorld& g) {
        const Places& P = gPlaces;
        vec3 plat = P.pierPlatform;
        podium = plat + vec3(40.f, -20.f, 0.f);
        podium.z = plat.z;
        sandoval = spawnCast(g, CAST_SANDOVAL, podium, kPi * 0.5f, FAC_CIVILIAN);
        holt = spawnCast(g, CAST_HOLT, podium + vec3(0, 2.f, 0), kPi * 0.5f, FAC_CIVILIAN);
        if (sandoval >= 0) {
            g.peds[sandoval].invincible = true;
            setIdle(g, sandoval, 7);
        }
        if (holt >= 0) {
            g.peds[holt].invincible = true;
            setIdle(g, holt, 0);
        }
        for (int i = 0; i < 18; i++) {
            float a = kTwoPi * i / 18.f;
            vec3 p = plat + vec3(-15.f + (i % 6) * 6.f, -15.f + (i / 6) * 8.f, 0.f);
            p.z = plat.z;
            int ci = g.randomCivilianChar(0x9100u + (u32)i * 17u, i % 3 == 0 ? 4 : 0);
            int c = g.mPed(ci, dvec3(p), a, FAC_CIVILIAN);
            if (c < 0) continue;
            setIdle(g, c, i % 4 == 0 ? 0 : 9);
            crowd.push_back(c);
        }
    }

    void sendGuards(GameWorld& g, int n, vec3 from) {
        for (int i = 0; i < n; i++) {
            vec3 p = from + vec3((i % 2) * 2.f, (i / 2) * 2.f, 0.f);
            gunman(g, i & 1 ? CAST_GUARD_B : CAST_GUARD_A, p, 0.f, i == 0 ? WPN_SMG : WPN_PISTOL, 0.25f, i == 1 && kit >= 0 ? kit : -2);
        }
        blipEnemies(g);
    }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        if (g.env->timeOfDay > 3.f && g.env->timeOfDay < 20.f) g.env->timeOfDay = 21.f;
        spawnParty(g);
        score(SC_NOIR, 0.35f, 10);
        kit = spawnCast(g, CAST_KIT, P.pierRamp + vec3(-4.f, 2.f, 0.f), -kPi * 0.5f, FAC_FRIEND);
        if (kit >= 0) g.peds[kit].maxHealth = g.peds[kit].health = 450.f;
        buddy = -1;
        // escape vehicles: a jet ski at the end of the pier, a car on the promenade
        vec3 w;
        if (findWater(g, P.pierEnd.xy() + vec2(15.f, 20.f), 2.f, w, 80.f)) ski = spawnBoat(g, w, -kPi * 0.5f, lin(0.95f, 0.4f, 0.1f), 1);
        car = spawnCar(g, pickModel(g, {Vehicles::VC_COUPE, Vehicles::VC_SPORTS}, 0), curbOffset(g, P.beachPier, 12.f), P.beachPier.curbYaw);
        if (checkpoint >= 1) {
            vec3 spot = P.pierEnd - vec3(20.f, 0, 0);
            placePlayer(g, spot, -kPi * 0.5f);
            placePed(g, kit, spot + vec3(2.f, 1.f, 0.f), kPi * 0.5f);
            beginWatch(g);
            return;
        }
        placePlayer(g, P.pierRamp + vec3(-4.f, 0.f, 0.f), -kPi * 0.5f);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotMove(P.pierPlatform + vec3(-160.f, -120.f, 30.f), P.pierPlatform + vec3(0, 0, 25.f), P.pierPlatform + vec3(-120.f, -100.f, 20.f),
                                 P.pierPlatform + vec3(0, 0, 35.f), 6.f, 50.f));
        shots.push_back(shotTwo(pedPos(g, sandoval), pedPos(g, holt), 8.f, 6.f, 40.f));
        shots.push_back(shotTwo(pedPos(g, kit), playerPos(g), 6.f));
        g.mCutscene(shots);
        say(g, CAST_SANDOVAL, sandoval, "[happy:0.4]Friends. Porto Sol deserves a waterfront that shines. Solaris Pier will bring jobs, light, and a future.");
        say(g, CAST_SANDOVAL, sandoval, "[calm]Some people fear change. I say, the tide always comes in. You can swim, or you can drown.");
        say(g, CAST_KIT, kit, "[angry:0.5]Did he just threaten a whole neighborhood at his own party? I need that on tape. With Holt standing next to him.");
        sayMe(g, "[whisper:0.4]End of the pier has the best angle. Stay close to me.");
    }

    void beginWatch(GameWorld& g) {
        g.mClearMarkers();
        g.mClearTarget();
        setIdle(g, kit, 8);
        g.mObjective("Protect ~b~Kit~s~ while she records.");
        g.missionTimerHud = 45.f;
        timer = 45.f;
        waveSent = 0;
        score(SC_CHASE, 0.8f, 10);
        cp(g, 1);
        setStage(3);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (allyDown(g, kit, "Kit")) return MS_FAILED;
        // fireworks over the pier all night
        fwTimer -= dt;
        if (fwTimer <= 0.f && ::length(playerPos(g) - gPlaces.pierPlatform) < 700.f) {
            fwTimer = 0.6f + hashToFloat(fwSeed * 13u) * 1.2f;
            fireworksBurst(g, gPlaces.pierPlatform + vec3(120.f, 0, 0), fwSeed++);
        }
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    setFollow(g, kit, g.player);
                    goTo(g, gPlaces.pierEnd - vec3(20.f, 0, 0), 3.f, "Escort ~b~Kit~s~ to the end of the pier.");
                    next();
                }
                break;
            case 1:
                if (stageTime > 8.f && stageTime < 8.1f) say(g, CAST_KIT, kit, "[happy:0.3]I used to come here as a kid. The coaster was scarier back then.");
                if (arrived(g) && ::length(pedPos(g, kit) - playerPos(g)) < 12.f) {
                    say(g, CAST_KIT, kit, "[whisper:0.5]Perfect. Directional mic is up. Just keep his goons off me for a minute.");
                    beginWatch(g);
                }
                break;
            case 3: {
                timer -= dt;
                g.missionTimerHud = Max(0.f, timer);
                facePed(g, kit, pedPos(g, sandoval));
                if (waveSent == 0 && timer < 37.f) {
                    waveSent = 1;
                    sendGuards(g, 2, gPlaces.pierPlatform + vec3(60.f, 10.f, 0.f));
                    say(g, CAST_KIT, kit, "[scared]Two of his security guys are coming this way. They saw the mic.");
                } else if (waveSent == 1 && timer < 22.f) {
                    waveSent = 2;
                    sendGuards(g, 3, gPlaces.pierPlatform + vec3(90.f, -12.f, 0.f));
                    sayMe(g, "[shout]More of them! Keep recording!");
                } else if (waveSent == 2 && timer < 8.f) {
                    waveSent = 3;
                    sendGuards(g, 2, gPlaces.pierEnd + vec3(-60.f, 14.f, 0.f));
                }
                if (timer <= 0.f) {
                    g.missionTimerHud = -1.f;
                    say(g, CAST_KIT, kit, "[shout]Got it! Sandoval, Holt, the threats, all of it. Now get me out of here!");
                    setFollow(g, kit, g.player);
                    sendGuards(g, 3, gPlaces.pierRamp + vec3(20.f, 0, 0));
                    if (ski >= 0) {
                        g.mBlipVehicle(ski, UI::BLIP_BOAT);
                        g.mObjective("Get Kit out: take the ~b~jet ski~s~ or fight back to the ~b~car~s~.");
                    } else {
                        g.mObjective("Get Kit out: fight back to the ~b~car~s~ on the promenade.");
                    }
                    if (car >= 0) g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    score(SC_CHASE, 1.f, 10);
                    next();
                }
                break;
            }
            case 4: {
                buddyUpdate(g, kit, &enemies);
                bool together = g.playerVehicle() >= 0 && g.peds[kit].vehicle == g.playerVehicle();
                if (g.playerVehicle() >= 0 && g.peds[kit].vehicle != g.playerVehicle()) {
                    int seat = g.freeSeat(g.playerVehicle(), false);
                    if (seat > 0 && ::length(pedPos(g, kit) - playerPos(g)) < 8.f) g.warpPedIntoVehicle(kit, g.playerVehicle(), seat);
                }
                if (together && !g.playerAt(gPlaces.pierPlatform.xy(), 300.f)) {
                    g.mClearBlips();
                    for (int e : enemies)
                        if (pedAlive(g, e)) setFlee(g, e, g.player);
                    score(SC_NOIR, 0.4f, 10);
                    goTo(g, g.isBoat(g.playerVehicle()) ? gPlaces.riverLaunch : gPlaces.pulseFm.curb, 8.f,
                         g.isBoat(g.playerVehicle()) ? "Take Kit to the ~y~boatyard~s~." : "Take Kit to ~y~Pulse FM~s~.", !g.isBoat(g.playerVehicle()));
                    say(g, CAST_KIT, kit, "[happy]Remind me never to go to a party with you again. That was amazing.");
                    next();
                }
                break;
            }
            case 5: {
                buddyUpdate(g, kit, nullptr);
                bool together = g.peds[kit].vehicle >= 0 && g.peds[kit].vehicle == g.playerVehicle();
                if (arrived(g) && together) {
                    clearGoal(g);
                    g.storyBriefText = "Kit recorded Sandoval threatening Calle Luna with Captain Holt at his side. The tape will matter - "
                                       "once they have Holt's private ledger to back it up.";
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
                if (t.stageTime > 0.5f) {
                    t.teleport(goal, -kPi * 0.5f);
                    placePed(g, kit, goal + vec3(2.f, 1.f, 0.f), 0.f);
                }
                break;
            case 3:
                timer -= dt * 3.f;   // fast forward the recording
                if (fmodf(t.stageTime, 1.5f) < dt) t.killEnemies();
                break;
            case 4:
                if (t.stageTime > 0.8f) {
                    t.killEnemies();
                    int v = car >= 0 ? car : spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g), 0.f);
                    t.enter(v);
                    if (kit >= 0 && v >= 0) g.warpPedIntoVehicle(kit, v, 1);
                    t.driveToward(gPlaces.pulseFm.curb.xy(), 60.f, dt);
                }
                break;
            case 5: testGoal(g, t, dt, 60.f); break;
            default: break;
        }
    }
};

// ==================================================================================================================
// Act 2-8: "Second Chance" (Dex). The Cuervos sank Jonah's boat off Ten Palms. Steal the helicopter at the airport,
// pull him out of the Gulf, fly him to the hospital. Dex gets the rescue he never finished.
class MissionSecondChance : public StoryMission {
public:
    int heli = -1, jonah = -1;
    float hold = 0.f;
    vec3 jonahSpot;
    const char* title() const override { return "Second Chance"; }
    const char* brief() const override {
        return "Jonah's boat went down off Ten Palms. He's in the water and running out of time. Take the helicopter at the airport and "
               "get him out.";
    }
    long long reward() const override { return 8000; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Places& P = gPlaces;
        score(SC_NOIR, 0.4f, 11);
        vec3 pad = P.heliPad + vec3(0.f, 34.f, 0.f);
        pad.z = groundAt(g, pad.x, pad.y, pad.z + 5.f);
        heli = spawnAircraft(g, true, pad, -kPi * 0.5f, lin(0.9f, 0.9f, 0.92f));
        jonahSpot = P.gulfWater;
        if (checkpoint >= 1) {
            placePlayer(g, pad, 0.f);
            if (heli >= 0) {
                vec3 air = pad + vec3(0, 0, 60.f);
                teleportVehicle(g, heli, air, yawTo(pad.xy(), jonahSpot.xy()));
                g.warpPedIntoVehicle(g.player, heli, 0);
            }
            beginFlight(g);
            return;
        }
        placePlayer(g, P.airport.pos, P.airport.yaw);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PHONE_RING, 0.8f);
#endif
        phoneLine(g, CAST_JONAH, "[scared]Dex... Mayday. They put a hole in my boat. Off Ten Palms. Water's cold... I can't hold on long.");
        sayMe(g, "[shout]Jonah! Stay with me. Keep your head up. I'm coming.");
        narrator(g, "Terminal PA", "Attention please. The north helipad is closed to unauthorized personnel. Thank you for flying Porto Sol.",
                 "announcer_female", "[pa]");
        g.mBlipVehicle(heli, UI::BLIP_HELI);
        g.mObjective("Steal the ~b~helicopter~s~ at the airport.");
    }

    void beginFlight(GameWorld& g) {
        g.mClearBlips();
        jonah = spawnCast(g, CAST_JONAH, jonahSpot, 0.f, FAC_FRIEND);
        if (jonah >= 0) g.peds[jonah].invincible = true;
        goTo(g, jonahSpot, 30.f, "Fly to ~y~Ten Palms~s~ and find Jonah.", false, false);
        g.missionTimerHud = 300.f;
        timer = 300.f;
        score(SC_CHASE, 0.7f, 11);
        cp(g, 1);
        setStage(2);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, heli, "helicopter")) return MS_FAILED;
        if (stage >= 2 && stage <= 3) {
            timer -= dt;
            g.missionTimerHud = Max(0.f, timer);
            if (timer <= 0.f) return fail("Jonah drowned.");
        }
        switch (stage) {
            case 0:
                if (g.playerInVehicle(heli)) {
                    sayMe(g, "Haven't flown one of these since the service. It's like riding a bike. A bike that wants to kill you.");
                    beginFlight(g);
                }
                break;
            case 2:
                if (stageTime > 12.f && stageTime < 12.1f) {
                    phoneLine(g, CAST_KIT, "[scared:0.5]Dex, I've got his radio beacon. Southwest of Ten Palms, about a mile out. Hurry.");
                }
                if (stageTime > 40.f && stageTime < 40.1f) {
                    sayMe(g, "[sad]Four years ago it was a night like this. Two fishermen in the water. I got to one of them.");
                    sayMe(g, "[angry:0.5]Not this time. Nobody else drowns on my watch.");
                }
                if (g.playerAt(jonahSpot.xy(), 250.f)) {
                    g.mClearMarkers();
                    g.mMarker(dvec3(jonahSpot), 6.f, vec3(0.3f, 0.8f, 1.f));
                    g.mObjective("Hover low over ~b~Jonah~s~ so he can grab the skid.");
                    say(g, CAST_JONAH, jonah, "[shout]Dex! Over here!");
                    next();
                }
                break;
            case 3: {
                int pv = g.playerVehicle();
                bool ok = false;
                if (pv == heli) {
                    const Vehicles::VehicleState& s = g.vehicles[heli].sim;
                    float horiz = ::length(s.body.pos.toVec3().xy() - pedPos(g, jonah).xy());
                    ok = horiz < 14.f && s.agl < 16.f && s.speed() < 6.f;
                    if (!ok && g.hudHelpTimer <= 0.f)
                        g.help(horiz >= 14.f ? "Move over Jonah." : (s.agl >= 16.f ? "Lower! Get down to the water." : "Steady... slow down."), 1.f);
                }
                hold = ok ? hold + dt : Max(0.f, hold - dt * 0.5f);
                if (ok) g.help(StrFormat("Hold steady...  %d%%", (int)(Saturate(hold / 4.f) * 100.f)), 0.3f);
                if (hold >= 4.f) {
                    g.missionTimerHud = -1.f;
                    g.mClearMarkers();
                    int seat = g.freeSeat(heli, false);
                    g.peds[jonah].invincible = false;
                    g.peds[jonah].maxHealth = g.peds[jonah].health = 160.f;
                    if (seat > 0) g.warpPedIntoVehicle(jonah, heli, seat);
                    say(g, CAST_JONAH, jonah, "[happy:0.4]Took your sweet time, Coast Guard.");
                    sayMe(g, "[calm]You're welcome. Now hold on, you're going to the hospital.");
                    say(g, CAST_JONAH, jonah, "[sad:0.4]Dex. Thank you. Now we're even.");
                    goTo(g, gPlaces.hospital.pos, 45.f, "Land near the ~y~hospital~s~.", false, true);
                    score(SC_NOIR, 0.45f, 11);
                    next();
                }
                break;
            }
            case 4: {
                if (allyDown(g, jonah, "Jonah")) return MS_FAILED;
                const Vehicles::VehicleState& s = g.vehicles[heli].sim;
                if (g.playerAt(goal.xy(), 60.f) && s.agl < 3.f && s.speed() < 2.f) {
                    clearGoal(g);
                    g.removePedFromVehicle(jonah, false);
                    g.storyBriefText = "Dex pulled Jonah out of the Gulf and flew him to the hospital. Four years late, he finished a rescue. "
                                       "Now it's time to finish Holt.";
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
            case 0: if (t.stageTime > 0.5f) t.enter(heli); break;
            case 2:
                if (t.stageTime > 0.5f) {
                    vec3 j = jonahSpot + vec3(40.f, 0.f, 40.f);
                    t.teleport(j, 0.f);
                }
                break;
            case 3:
                if (t.stageTime > 0.3f && jonah >= 0) {
                    vec3 j = pedPos(g, jonah);
                    t.teleport(vec3(j.x, j.y, g.map->waterAt(j.x, j.y) + 8.f), 0.f);
                    t.stopVehicle();
                    g.vehicles[heli].sim.agl = 8.f;
                }
                break;
            case 4:
                if (t.stageTime > 0.5f) {
                    vec3 h = gPlaces.hospital.pos + vec3(20.f, 0, 0);
                    t.teleport(vec3(h.x, h.y, groundAt(g, h.x, h.y) + 0.8f), 0.f);
                    t.stopVehicle();
                    g.vehicles[heli].sim.agl = 0.5f;
                }
                break;
            default: (void)dt; break;
        }
    }
};

// ==================================================================================================================
// Act 2-9: "Paper Trail" (Mari). Break into Captain Holt's Key Coral villa at night and crack the safe with her private
// payoff ledger, then get off the island.
class MissionPaperTrail : public StoryMission {
public:
    StealthGroup villa;
    vec3 gate, safe, house;
    HoldAction crack;
    bool alarmed = false;
    const char* title() const override { return "Paper Trail"; }
    const char* brief() const override {
        return "Captain Holt keeps a private ledger of every payoff in a safe at her Key Coral villa. She's at a fundraiser tonight. Get in, "
               "crack the safe, get out.";
    }
    long long reward() const override { return 10000; }

    void start(GameWorld& g) override {
        gMissions.suppressPolice = true;   // the story keeps regular police out of this one
        const Place& V = gPlaces.keyCoral;
        if (g.env->timeOfDay > 4.5f && g.env->timeOfDay < 22.f) g.env->timeOfDay = 23.f;
        house = V.door;
        gate = placeOffset(g, V, 0.f, 1.5f);
        safe = placeOffset(g, V, 5.f, 14.f);
        score(SC_STEALTH, 0.4f, 12);
        const float gp[5][4] = {{-8, 6, 8, 6}, {10, 12, 10, 22}, {-6, 18, 6, 18}, {0, 26, -12, 26}, {14, 4, 14, 4}};
        for (int i = 0; i < 5; i++) {
            vec3 a = placeOffset(g, V, gp[i][0], gp[i][1]), b = placeOffset(g, V, gp[i][2], gp[i][3]);
            int e = spawnCast(g, i & 1 ? CAST_GUARD_B : CAST_GUARD_A, a, yawTo(a.xy(), b.xy()), FAC_ENEMY);
            if (e < 0) continue;
            arm(g, e, i == 3 ? WPN_SHOTGUN : WPN_PISTOL);
            g.peds[e].brain.accuracy = 0.32f;
            setIdle(g, e, 0);
            enemies.push_back(e);
            std::vector<vec3> patrol;
            if (length(a - b) > 1.f) patrol = {a, b};
            villa.add(g, e, patrol, 15.f);
        }
        if (checkpoint >= 1) {
            placePlayer(g, gate, V.yaw);
            beginInside(g);
            return;
        }
        placePlayer(g, curbOffset(g, V, -60.f), V.curbYaw);
        std::vector<CutsceneShot> shots;
        establish(g, shots, house, V.yaw + kPi * 0.5f, 45.f, 16.f, 5.f, 50.f);
        shots.push_back(shotArc(safe, 18.f, 5.f, 0.3f, 1.0f, 5.f));
        g.mCutscene(shots);
        phoneLine(g, CAST_KIT, "[calm]Holt's at a police fundraiser downtown until midnight. Her private security walks the grounds.");
        phoneLine(g, CAST_KIT, "[whisper:0.5]The safe is in the pool house out back. Crouch, stay in the shadows, and don't let them see you.");
        sayMe(g, "[whisper]A police captain with a villa on Key Coral. Nobody ever asked how.");
    }

    void beginInside(GameWorld& g) {
        goTo(g, safe, 1.8f, "Crack the ~y~safe~s~ in the pool house. Stay out of sight.", false, true, vec3(0.3f, 0.8f, 1.f));
        cp(g, 1);
        setStage(2);
    }

    void alarm(GameWorld& g) {
        if (alarmed) return;
        alarmed = true;
        villa.raise(g);
        blipEnemies(g);
        score(SC_CHASE, 0.9f, 12);
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        showMeter(g, "DETECTION", 0.f);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (!alarmed && villa.update(g, dt)) {
            say(g, CAST_GUARD_A, villa.spotter, "[shout]Intruder! Around the pool!");
            alarm(g);
        }
        showMeter(g, "DETECTION", alarmed ? 0.f : villa.meter);
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    goTo(g, gate, 3.f, "Slip into ~y~Holt's villa~s~ grounds.");
                    next();
                }
                break;
            case 1:
                if (arrived(g)) beginInside(g);
                break;
            case 2:
                if (crack.update(g, safe, 1.6f, 6.f, "crack the safe", dt)) {
                    g.hudHelpTimer = 0.f;
                    clearGoal(g);
                    phoneLine(g, CAST_KIT, "[whisper:0.6]Is it there? Tell me it's there.");
                    sayMe(g, "[whisper]Leather ledger. Dates, amounts, initials. Sandoval's are on every page.");
                    phoneLine(g, CAST_KIT, "[happy:0.5]That's it. That's the whole story. Get off that island.");
                    g.mObjective(alarmed ? "Get off Key Coral. Lose the ~r~guards~s~." : "Get off ~y~Key Coral~s~ quietly.");
                    next();
                }
                break;
            case 3: {
                bool guardsNear = false;
                for (int e : enemies)
                    if (pedAlive(g, e) && ::length(pedPos(g, e) - playerPos(g)) < 80.f && alarmed) guardsNear = true;
                if (!g.playerAt(house.xy(), 320.f) && !guardsNear) {
                    showMeter(g, "DETECTION", 0.f);
                    goTo(g, gPlaces.pulseFm.curb, 6.f, "Bring the ledger to ~y~Kit~s~ at Pulse FM.", true);
                    score(SC_NOIR, 0.35f, 12);
                    next();
                }
                break;
            }
            case 4:
                if (arrived(g)) {
                    clearGoal(g);
                    g.storyBriefText = alarmed ? "Mari took Holt's ledger and shot her way off Key Coral. Holt knows exactly what's missing."
                                               : "Mari took Holt's private ledger without a trace. With the tape, the collections book and the "
                                                 "Solaris Pier files, the crew can finally go after Sandoval himself.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1: if (t.stageTime > 0.5f) t.teleport(gate, 0.f); break;
            case 2:
                if (t.stageTime > 0.5f && ::length(playerPos(g).xy() - safe.xy()) > 1.2f) {
                    t.teleport(safe, 0.f);
                    Ped* pl = g.playerPed();
                    if (pl) pl->animIn.crouch = true;
                }
                break;
            case 3: if (t.stageTime > 0.5f) t.teleportNear(house.xy() + vec2(0.f, 400.f), 1.f); break;
            case 4:
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) t.enter(spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f));
                testGoal(g, t, dt, 80.f);
                break;
            default: break;
        }
    }
};

}  // namespace mu
}  // namespace Game
