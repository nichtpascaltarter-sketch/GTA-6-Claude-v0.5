// Sawgrass Airboat Tours (side activity, from Jonah at the airboat dock): three tourists, Jonah's airboat and a route
// through the marsh. Slow down by the gators so the tourists get their pictures, give them a thrill on the open water,
// keep the hull off the banks and bring everyone back to the dock. Three routes: the Loop and Gator Alley by day,
// Night Eyes after dark (the lights on, look for the eyes). The tip jar pays by the stars; the best rating of each
// route is kept (EX_TOUR_STARS, 3 bits a route), gators seen add up (EX_GATORS_SPOTTED), and five stars on all three
// routes is SIDE_TOURS_ALL.
#include "missions.h"

namespace Game {
namespace mu {

struct TourRoute {
    const char* name;
    const char* blurb;
    vec2 centerOff;   // the loop's centre relative to the Sawgrass deep water
    float radius;
    int points;       // checkpoints around the loop
    float phase;
    int gators;
    bool night;
};
const TourRoute kTourRoutes[3] = {
    {"The Sawgrass Loop", "A gentle loop through the open marsh with three gator hangouts. Good for first-timers.", vec2(0.f, 0.f), 250.f, 7, 0.3f, 3,
     false},
    {"Gator Alley", "The long way round, through the channels where the big ones bask. Five gators, tight turns.", vec2(260.f, 160.f), 360.f, 9, 1.7f,
     5, false},
    {"Night Eyes", "After dark with the lights on. Look for the eyes shining back at you. Four gators.", vec2(-180.f, 120.f), 230.f, 6, 2.9f, 4,
     true},
};
constexpr int kTourRouteCount = 3;

int tourStars(GameWorld& g, int route) { return (flag(g, EX_TOUR_STARS) >> (route * 3)) & 7; }
void setTourStars(GameWorld& g, int route, int stars) {
    int v = flag(g, EX_TOUR_STARS);
    v &= ~(7 << (route * 3));
    v |= Clamp(stars, 0, 7) << (route * 3);
    setFlag(g, EX_TOUR_STARS, v);
}
bool tourNight(float hour) { return hour >= 20.f || hour < 5.f; }

// the tourists' lines, by moment (each tourist speaks with their own voice)
const char* const kTourBoard[] = {"Is it true you can see alligators right from the boat?", "I read they can run thirty miles an hour. That's not true, right?",
                                  "I'm filming the whole thing. Say hi to my followers!"};
const char* const kTourSpeed[] = {"Whooo! Faster!", "My hat! There goes my hat!", "I can't feel my face and I love it!",
                                  "This is better than the roller coaster!", "Do it again! Do that again!"};
const char* const kTourNear[] = {"Is that a log? That is not a log.", "There! By the bank! Slow down, slow down!", "Something moved in the grass over there!"};
const char* const kTourNearNight[] = {"Eyes! I see eyes in the water!", "Two red lights, right there. Those aren't lights, are they?"};
const char* const kTourSpotted[] = {"Got it! I got the picture!", "He's smiling at me. Why is he smiling at me?", "Look at the size of him! That's a dinosaur!",
                                    "Nobody back home is going to believe this."};
const char* const kTourBump[] = {"Ow! Is that part of the tour?", "I would like to live to see my grandkids, please.", "We hit something! Was it a gator?"};
const char* const kTourFast[] = {"Wait, go back! We missed it!", "Too fast! All I got was a blur!"};

struct TourGator {
    vec3 at;              // where it lies: a bank beside the route, or the water next to it
    bool bank = false;
    int group = -1;
    u32 uid = 0;
    float seen = 0.f;     // sighting progress 0..1
    bool spotted = false, called = false, tooFast = false;
};

class MissionAirboatTours : public StoryMission {
public:
    int route = 0;
    int boat = -1, jonah = -1;
    int tourists[3] = {-1, -1, -1};
    const char* touristNames[3] = {"Gail", "Marty", "Priya"};
    std::vector<vec3> cps;
    int nextCp = 0;
    std::vector<TourGator> gators;
    float tourT = 0.f, thrill = 0.f, damage = 0.f, lastHealth = -1.f, par = 0.f, chatterCd = 0.f, bumpCd = 0.f, awayT = 0.f;
    int spotted = 0, stars = 0, speaker = 0;
    int lineIx = 0;
    long long pay = 0;
    const char* title() const override { return "Sawgrass Airboat Tours"; }
    const char* brief() const override {
        return "Take Jonah's tourists out through the Sawgrass in the airboat. Slow down by the gators so they get their pictures, give them a "
               "thrill on the open water and bring everybody back in one piece. They tip by the stars.";
    }
    long long reward() const override { return pay; }
    const char* passBanner() const override { return "TOUR COMPLETE"; }
    const char* failBanner() const override { return "TOUR CANCELLED"; }

    void touristSay(GameWorld& g, int who, const std::string& text, const char* tags = "") {
        int p = tourists[who % 3];
        if (!pedAlive(g, p)) return;
        DialogueLine l = line(touristNames[who % 3], text, p, 0xffb8e6ffu);
        l.hasVoice = true;
        l.voice = g.peds[p].voice;
        l.spoken = std::string(tags) + speakableText(text);
        l.pause = 0.2f;
        g.mSay(l);
    }
    template <size_t N> void chatter(GameWorld& g, const char* const (&pool)[N], const char* tags = "") {
        if (g.mTalking()) return;
        speaker = (speaker + 1) % 3;
        touristSay(g, speaker, pool[(lineIx++) % N], tags);
    }

    void start(GameWorld& g) override {
        const Places& P = gPlaces;
        vec3 dock = P.sawgrassDock, water = P.sawgrassWater;
        float boatYaw = yawTo(water.xy(), P.sawgrassDeep.xy());
        boat = spawnAirboat(g, water, boatYaw, lin(0.1f, 0.38f, 0.32f), 7);
        if (boat < 0) {
            setStage(9);
            return;
        }
        g.vehicles[boat].persistent = true;
        vec2 toWater = normalize(water.xy() - dock.xy() + vec2(1e-3f, 0.f));
        vec2 side(-toWater.y, toWater.x);
        jonah = spawnCast(g, CAST_JONAH, dock + vec3(side * 2.2f, 0.f), yawTo(dock.xy(), water.xy()), FAC_FRIEND);
        if (jonah >= 0) g.peds[jonah].invincible = true;
        for (int i = 0; i < 3; i++) {
            u32 seed = hash32((u32)(flag(g, EX_TOUR_COUNT) * 7 + i) * 2654435761u + 0x70u);
            int ci = wardrobeChar(g, seed, LK_TOURIST, dock.xy());   // vacationers: sightseers' clothes (no swimwear in the marsh)
            int p = g.mPed(ci, dvec3(dock + vec3(side * (-1.5f - i), 0.f)), yawTo(dock.xy(), water.xy()), FAC_CIVILIAN);
            if (p < 0) continue;
            g.peds[p].voice = Speech::presetVoice(g.peds[p].female, seed);
            g.peds[p].maxHealth = g.peds[p].health = 160.f;
            g.warpPedIntoVehicle(p, boat, i + 1);
            tourists[i] = p;
        }
        lastHealth = g.vehicles[boat].sim.health;
        placePlayer(g, dock - vec3(toWater * 3.f, 0.f), yawTo(dock.xy(), water.xy()));
        vec3 jp = pedPos(g, jonah), mp = playerPos(g), bp = vehPos(g, boat);
        facePed(g, jonah, mp);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotMove(bp + vec3(-side * 14.f - toWater * 4.f, 5.f), bp + vec3(0, 0, 1.f), bp + vec3(-side * 10.f - toWater * 6.f, 3.5f),
                                 bp + vec3(0, 0, 1.2f), 5.f, 45.f));
        shots.push_back(shotTwo(jp, mp, 7.f));
        shots.push_back(shotOver(mp, jp, 6.f));
        g.mCutscene(shots);
        say(g, CAST_JONAH, jonah, "[calm]Tourist season. They pay good money to see a gator up close and not get eaten. Your job is both halves of that.");
        say(g, CAST_JONAH, jonah, "[calm]Go slow by the gators so they get their pictures. Open her up on the straight water, they love that. And stay off the banks.");
        if (tourNight(g.env ? g.env->timeOfDay : 12.f))
            say(g, CAST_JONAH, jonah, "[calm]After dark you find them by their eyes. Red, like brake lights. Lights on, nice and easy.");
        sayMe(g, "[calm]Slow by the gators, fast on the straights, nobody gets eaten. Got it.");
        g.mObjective("");
        setStage(0);
    }

    // The loop for this route (water checkpoints), and the gators along it
    void buildRoute(GameWorld& g) {
        const Places& P = gPlaces;
        const TourRoute& R = kTourRoutes[route];
        vec3 c = P.sawgrassDeep + vec3(R.centerOff, 0.f);
        std::vector<vec3> loop = waterLoop(g, c, R.radius, R.points, 0.8f, R.phase);
        if ((int)loop.size() < 4) loop = waterLoop(g, P.sawgrassDeep, 220.f, 7, 0.6f, R.phase);
        if (loop.empty()) loop.push_back(P.sawgrassDeep);
        // start at the loop point nearest the dock
        int first = 0;
        float bd = 1e9f;
        for (int i = 0; i < (int)loop.size(); i++) {
            float d = ::length(loop[i].xy() - P.sawgrassWater.xy());
            if (d < bd) {
                bd = d;
                first = i;
            }
        }
        cps.clear();
        for (int i = 0; i < (int)loop.size(); i++) cps.push_back(loop[(first + i) % loop.size()]);
        if (cps.size() < 2) cps.push_back(P.sawgrassDeep + vec3(60.f, 40.f, 0.f));   // (a marsh with no loop: out and back)
        float len = 0.f;
        vec3 prev = P.sawgrassWater;
        for (const vec3& p : cps) {
            len += ::length(p.xy() - prev.xy());
            prev = p;
        }
        len += ::length(prev.xy() - P.sawgrassWater.xy());
        // gators beside the legs, spread along the loop: basking on a bank when there is one close by, else in the water
        gators.clear();
        int n = (int)cps.size();
        for (int k = 0; k < R.gators; k++) {
            int leg = 1 + (k * (n - 1)) / Max(R.gators, 1);
            leg = Clamp(leg, 1, n - 1);
            vec3 a = cps[leg - 1], b = cps[leg];
            vec3 mid = a + (b - a) * (0.35f + 0.3f * hashToFloat(hash32((u32)(k + 1) * 977u + (u32)route)));
            vec2 d = normalize(b.xy() - a.xy() + vec2(1e-3f, 0.f));
            vec2 side((k & 1) ? -d.y : d.y, (k & 1) ? d.x : -d.x);
            TourGator tg;
            vec3 land;
            if (swampLand(g, mid.xy() + side * 14.f, 0.f, 30.f, atan2f(side.y, side.x), land)) {
                tg.at = land;
                tg.bank = true;
            } else {
                vec2 q = mid.xy() + side * 12.f;
                float w = g.map->waterAt(q.x, q.y);
                tg.at = vec3(q, w > World::kNoWater + 1.f ? w - 0.35f : mid.z);
            }
            gators.push_back(tg);
        }
        par = len / 11.f + R.gators * 9.f + 20.f;
        nextCp = 0;
        LOG("tours: %s, %d checkpoints, %d gators, %.0f m, par %.0f s", R.name, (int)cps.size(), (int)gators.size(), len, par);
    }

    void chooseRoute(GameWorld& g) {
        std::vector<MenuItem> items;
        bool night = tourNight(g.env ? g.env->timeOfDay : 12.f);
        for (int r = 0; r < kTourRouteCount; r++) {
            MenuItem it;
            it.label = kTourRoutes[r].name;
            int st = tourStars(g, r);
            it.right = st > 0 ? StrFormat("Best %d/5", st) : std::string("New");
            it.enabled = kTourRoutes[r].night == night;
            it.detail = std::string(kTourRoutes[r].blurb) + (kTourRoutes[r].night ? (night ? "" : " Runs after 8 pm.") : (night ? " Daytime only." : ""));
            it.id = r + 1;
            items.push_back(it);
        }
        int cursor = night ? 2 : (tourStars(g, 0) >= 5 ? 1 : 0);
        menuOpen(g, MO_CHOICE, "AIRBOAT TOURS", "Pick today's route", items, true, 0xff38b07au, cursor);
    }

    void beginTour(GameWorld& g) {
        buildRoute(g);
        if (boat >= 0) {
            g.mBlipVehicle(boat, UI::BLIP_BOAT);
            if (kTourRoutes[route].night) g.vehicles[boat].lightsOn = true;
        }
        g.mObjective("Get in the ~b~airboat~s~.");
        setStage(2);
    }

    void showCheckpoint(GameWorld& g) {
        g.mClearMarkers();
        if (nextCp < (int)cps.size()) {
            g.mTarget(cps[nextCp].xy());
            g.mMarker(dvec3(cps[nextCp]), 8.f, vec3(0.3f, 0.9f, 0.6f));
            g.mObjective(StrFormat("~y~%s~s~  Waypoint %d/%d   Gators %d/%d", kTourRoutes[route].name, nextCp + 1, (int)cps.size(), spotted, (int)gators.size()));
        } else {
            g.mTarget(gPlaces.sawgrassWater.xy());
            g.mMarker(dvec3(gPlaces.sawgrassWater), 6.f, vec3(1.f, 0.85f, 0.2f));
            g.mObjective(StrFormat("Bring the tourists back to the ~y~dock~s~.   Gators %d/%d", spotted, (int)gators.size()));
        }
    }

    // Gators come out when the boat is near (the wildlife pool recycles far ones; they are put back if it did)
    vec3 gatorPos(const TourGator& t) const {
        using namespace wild_detail;
        if (t.group >= 0 && t.group < (int)gW.groups.size() && gW.groups[t.group].used && gW.groups[t.group].uid == t.uid &&
            !gW.groups[t.group].members.empty()) {
            int ai = gW.groups[t.group].members[0];
            if (ai >= 0 && ai < (int)gW.animals.size() && gW.animals[ai].used) return gW.animals[ai].pos;
        }
        return t.at;
    }
    bool gatorLive(const TourGator& t) const {
        using namespace wild_detail;
        return t.group >= 0 && t.group < (int)gW.groups.size() && gW.groups[t.group].used && gW.groups[t.group].uid == t.uid;
    }

    void updateGators(GameWorld& g, float dt, vec3 bp, float speed) {
        bool night = kTourRoutes[route].night;
        for (TourGator& t : gators) {
            float d = ::length(bp.xy() - t.at.xy());
            if (d < 260.f && !gatorLive(t) && !t.spotted) {
                t.group = wild_detail::spawnGator(g, t.at, t.bank);
                t.uid = t.group >= 0 ? wild_detail::gW.groups[t.group].uid : 0u;
            }
            if (t.spotted) continue;
            vec3 gp = gatorPos(t);
            float dg = ::length(bp.xy() - gp.xy());
            if (!t.called && dg < 70.f) {
                t.called = true;
                if (night) chatter(g, kTourNearNight, "[excited]");
                else chatter(g, kTourNear, "[excited]");
            }
            if (dg < 26.f) {
                if (speed < 7.f) {
                    t.seen += dt / 2.2f;
                    showMeter(g, "GATOR SIGHTING", t.seen);
                    if (t.seen >= 1.f) {
                        t.spotted = true;
                        spotted++;
                        showMeter(g, "GATOR SIGHTING", 0.f);
                        chatter(g, kTourSpotted, "[happy]");
                        // the cameras go off
                        for (int i = 0; i < 3; i++)
                            if (pedAlive(g, tourists[i])) spawnLight(g.peds[tourists[i]].pos + dvec3(0.0, 0.0, 1.4), vec3(1.f, 0.97f, 0.9f) * 30000.f, 6.f);
#ifdef HAVE_AUDIO
                        Audio::play(Audio::SFX_CAMERA_SHUTTER, bp, 0.7f);
                        Audio::play(Audio::SFX_GATOR_HISS, gp, 0.8f);
#endif
                        showCheckpoint(g);
                    }
                } else if (!t.tooFast) {
                    t.tooFast = true;
                    chatter(g, kTourFast, "[sad:0.3]");
                    g.help("Slow down by the gators so the tourists can get their pictures.", 4.f);
                }
            } else if (t.seen > 0.f && dg > 40.f) {
                showMeter(g, "GATOR SIGHTING", 0.f);
            }
        }
    }

    int rate() const {
        float gPart = gators.empty() ? 1.f : spotted / (float)gators.size();
        float smooth = 1.f - Saturate(damage / 250.f);
        float thrillPart = Saturate(thrill / 18.f);
        float s = 1.f + 2.f * gPart + smooth + 0.6f * thrillPart + (tourT <= par ? 0.4f : 0.f);
        return Clamp((int)floorf(s + 0.25f), 1, 5);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (stage == 9) return fail("There is no airboat to take the tourists out in.");
        if (stage >= 2 && vehicleLost(g, boat, "airboat")) return MS_FAILED;
        for (int i = 0; i < 3; i++)
            if (tourists[i] >= 0 && !pedAlive(g, tourists[i])) return fail("One of the tourists didn't make it back. Jonah is closing the tours for the day.");
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    if (gMissions.test.active) {
                        const char* r = Platform::argValue("tourroute");
                        route = r ? Clamp(atoi(r), 0, kTourRouteCount - 1) : (tourNight(g.env->timeOfDay) ? 2 : 0);
                        if (kTourRoutes[route].night != tourNight(g.env->timeOfDay)) g.env->timeOfDay = kTourRoutes[route].night ? 21.f : 10.f;
                        beginTour(g);
                    } else {
                        chooseRoute(g);
                        next();
                    }
                }
                break;
            case 1:
                if (menuIs(MO_CHOICE) && gMenu.chosen > 0) {
                    route = gMenu.chosen - 1;
                    menuClose(g);
                    beginTour(g);
                } else if (menuIs(MO_CHOICE) && gMenu.cancelled) {
                    menuClose(g);
                    return fail("Maybe another time. Jonah takes the tourists out himself.");
                }
                break;
            case 2:   // aboard
                if (g.playerVehicle() == boat && g.peds[g.player].seat == 0) {
                    unblipVehicle(boat);
                    sayMe(g, "[happy:0.3]Keep your hands inside the boat, folks. The gators out here work on commission.");
                    touristSay(g, 0, kTourBoard[0], "[excited]");
                    touristSay(g, 1, kTourBoard[1], "[scared:0.3]");
                    touristSay(g, 2, kTourBoard[2], "[happy]");
                    tourT = 0.f;
                    lastHealth = g.vehicles[boat].sim.health;
                    score(SC_CHASE, 0.45f, 40);
                    showCheckpoint(g);
                    next();
                }
                break;
            case 3:
            case 4: {   // the tour, then back to the dock
                tourT += dt;
                chatterCd -= dt;
                bumpCd -= dt;
                vec3 bp = vehPos(g, boat);
                float speed = vehicleSpeed(g, boat);
                bool aboard = g.playerVehicle() == boat;
                awayT = aboard ? 0.f : awayT + dt;
                if (awayT > 25.f) return fail("You left the tourists stranded in the swamp.");
                if (!aboard && awayT < dt * 1.5f) g.help("Get back in the airboat. The tourists are waiting.", 4.f);
                if (tourT > par * 3.f + 90.f) return fail("The tour ran so long the tourists called for a ride. From a swamp.");
                // the ride: speed on the open water thrills them, knocks on the banks don't
                if (aboard && speed > 15.f) {
                    thrill += dt;
                    if (chatterCd <= 0.f && g.map->isWater(bp.x, bp.y)) {
                        chatter(g, kTourSpeed, "[excited]");
                        chatterCd = 14.f;
                    }
                }
                float h = g.vehicles[boat].sim.health;
                if (lastHealth >= 0.f && h < lastHealth - 1.f) {
                    float hit = lastHealth - h;
                    damage += hit;
                    if (hit > 15.f && bumpCd <= 0.f) {
                        chatter(g, kTourBump, "[scared:0.4]");
                        bumpCd = 6.f;
                    }
                }
                lastHealth = h;
                updateGators(g, dt, bp, speed);
                if (stage == 3) {
                    if (nextCp < (int)cps.size() && ::length(bp.xy() - cps[nextCp].xy()) < 16.f) {
                        nextCp++;
#ifdef HAVE_AUDIO
                        Audio::play2D(Audio::SFX_CHECKPOINT, 0.5f);
#endif
                        showCheckpoint(g);
                        if (nextCp >= (int)cps.size()) next();
                    }
                } else if (aboard && ::length(bp.xy() - gPlaces.sawgrassWater.xy()) < 14.f && speed < 3.f) {
                    finishTour(g);
                    setStage(5);
                }
                break;
            }
            case 5:
                if (!g.mTalking()) {
                    // everyone off, onto the dock
                    for (int i = 0; i < 3; i++)
                        if (pedAlive(g, tourists[i]) && g.peds[tourists[i]].vehicle >= 0) {
                            g.removePedFromVehicle(tourists[i], false);
                            placePed(g, tourists[i], gPlaces.sawgrassDock + vec3(-1.5f - i, 1.f, 0.f), 0.f);
                        }
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void finishTour(GameWorld& g) {
        g.mClearMarkers();
        g.mClearTarget();
        showMeter(g, "GATOR SIGHTING", 0.f);
        stars = rate();
        int tip = 45 * stars;
        pay = 3 * (120 + tip);
        int best = tourStars(g, route);
        if (stars > best) setTourStars(g, route, stars);
        setFlag(g, EX_TOUR_COUNT, flag(g, EX_TOUR_COUNT) + 1);
        setFlag(g, EX_GATORS_SPOTTED, flag(g, EX_GATORS_SPOTTED) + spotted);
        std::string starStr;
        for (int i = 0; i < 5; i++) starStr += i < stars ? "*" : "-";
        g.notify("TOUR RATING", StrFormat("%s  %d/5   Gators %d/%d   $%lld with tips%s", starStr.c_str(), stars, spotted, (int)gators.size(), pay,
                                          stars > best && best > 0 ? "   NEW BEST" : ""));
        if (stars >= 5) touristSay(g, 0, "Best tour ever. Five stars, obviously. I'm telling everyone!", "[happy]");
        else if (stars >= 3) touristSay(g, 1, "Nice gators. The driving could use a little work.", "[calm]");
        else touristSay(g, 2, "Well. That was three hours of my vacation.", "[sad:0.4]");
        sayMe(g, stars >= 4 ? "[happy:0.4]Welcome back to dry land, folks. Tips are appreciated, gator bites are not."
                            : "[calm]And that's the Sawgrass. Mind the step, the dock bites too.");
        bool all = true;
        for (int r = 0; r < kTourRouteCount; r++) all = all && tourStars(g, r) >= 5;
        if (all && !flag(g, SIDE_TOURS_ALL)) {
            setFlag(g, SIDE_TOURS_ALL, 1);
            money(g, 5000);
            g.notify("SAWGRASS TOURS", "Five stars on every route. Jonah splits the season's bonus with you: $5,000.");
        }
        LOG("tours: %s done in %.0f s (par %.0f), gators %d/%d, thrill %.0f s, damage %.0f -> %d stars, $%lld", kTourRoutes[route].name, tourT, par, spotted,
            (int)gators.size(), thrill, damage, stars, pay);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 2:
                if (t.stageTime > 0.5f && boat >= 0 && g.playerVehicle() != boat) t.enter(boat);
                break;
            case 3:
            case 4: {
                if (t.stageTime < 0.8f || boat < 0 || g.playerVehicle() != boat) break;
                // the next gator not yet seen whose leg comes up: park beside it until the sighting is done
                vec3 bp = vehPos(g, boat);
                for (TourGator& tg : gators) {
                    if (tg.spotted) continue;
                    vec3 gp = gatorPos(tg);
                    if (::length(bp.xy() - gp.xy()) > 60.f && ::length(cps[Min(nextCp, (int)cps.size() - 1)].xy() - gp.xy()) > 220.f) continue;
                    if (::length(bp.xy() - gp.xy()) > 20.f) {
                        vec3 w;
                        if (findWater(g, gp.xy(), 0.5f, w, 40.f)) teleportVehicle(g, boat, w, yawTo(w.xy(), gp.xy()));
                        else teleportVehicle(g, boat, vec3(gp.xy() + vec2(8.f, 0.f), gp.z + 0.4f), 0.f);
                    }
                    g.vehicles[boat].sim.body.vel = vec3(0.f);
                    return;
                }
                if (fmodf(t.stageTime, 1.2f) < dt) {
                    vec3 to = nextCp < (int)cps.size() ? cps[nextCp] : gPlaces.sawgrassWater;
                    teleportVehicle(g, boat, to, yawTo(bp.xy(), to.xy()));
                    g.vehicles[boat].sim.body.vel = vec3(0.f);
                }
                break;
            }
            default: break;
        }
    }
};

std::vector<std::pair<std::string, std::string>> tourStats(GameWorld& g) {
    std::vector<std::pair<std::string, std::string>> out;
    out.push_back({"Airboat tours run", StrFormat("%d", flag(g, EX_TOUR_COUNT))});
    std::string best;
    for (int r = 0; r < kTourRouteCount; r++) best += StrFormat("%s%d/5", r ? "  " : "", tourStars(g, r));
    out.push_back({"Tour ratings (Loop, Alley, Night)", best});
    out.push_back({"Gators spotted on tours", StrFormat("%d", flag(g, EX_GATORS_SPOTTED))});
    return out;
}

}  // namespace mu
}  // namespace Game
