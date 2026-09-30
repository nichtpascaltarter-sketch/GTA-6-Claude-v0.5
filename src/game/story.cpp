// Story registry: every story mission and side activity with its start trigger, prerequisites, protagonist and
// availability window. Mission scripts live in story_act1/2/3.cpp and activities.cpp (all in namespace Game::mu).
#include "missions.h"

namespace Game {

using namespace mu;

namespace story_detail {

template <class T> Mission* makeMission() { return new T(); }

MissionDef storyDef(const char* id, const char* title, const char* contact, char letter, vec2 start, int storyIndex, int req, int req2,
                    int protagonist, int act, std::function<Mission*()> create) {
    MissionDef d;
    d.id = id;
    d.title = title;
    d.contact = contact;
    d.letter = letter;
    d.startPos = start;
    d.storyIndex = storyIndex;
    d.requiresFlag = req;
    d.requiresFlag2 = req2;
    d.setsFlag = storyIndex;
    d.repeatable = false;
    d.timeFrom = d.timeTo = 0.f;
    d.icon = UI::BLIP_MISSION;
    d.protagonist = protagonist;
    d.act = act;
    d.create = create;
    return d;
}

}  // namespace story_detail

using namespace story_detail;

void MissionManager::registerAll(GameWorld& g) {
    defs.clear();
    computePlaces(g);
    const Places& P = gPlaces;
    auto startAt = [](const Place& p, float along) { return p.pos.xy() + p.streetDir * along; };

    // ---- Prologue + Act 1: Calle Luna
    MissionDef d = storyDef("low_tide", "Low Tide", "Tomas", 'T', startAt(P.mariApt, 0.f), SF_LOW_TIDE, -1, -1, 0, 0, makeMission<MissionLowTide>);
    d.hidden = true;   // starts with the opening phone call of a new game
    defs.push_back(d);
    defs.push_back(storyDef("repo_man", "Repo Man", "Rook", 'R', startAt(P.rookShop, 3.f), SF_REPO_MAN, SF_LOW_TIDE, -1, 1, 1,
                            makeMission<MissionRepoMan>));
    defs.push_back(storyDef("dry_dock", "Dry Dock", "Rook", 'R', startAt(P.dexTrailer, 0.f), SF_DRY_DOCK, SF_REPO_MAN, -1, 1, 1,
                            makeMission<MissionDryDock>));
    defs.push_back(storyDef("pressure", "Pressure Cooker", "Mama Lucha", 'L', startAt(P.diner, -3.f), SF_PRESSURE, SF_LOW_TIDE, -1, 0, 1,
                            makeMission<MissionPressure>));
    defs.push_back(storyDef("collateral", "Collateral", "Mama Lucha", 'L', startAt(P.diner, -3.f), SF_COLLATERAL, SF_PRESSURE, SF_DRY_DOCK, 0, 1,
                            makeMission<MissionCollateral>));
    d = storyDef("pink_slips", "Pink Slips", "Chuy", 'C', startAt(P.raceCalle, 0.f), SF_PINK_SLIPS, SF_DRY_DOCK, -1, 1, 1, makeMission<MissionPinkSlips>);
    d.timeFrom = 20.f;
    d.timeTo = 5.f;
    defs.push_back(d);
    d = storyDef("last_call", "Last Call", "Mama Lucha", 'L', startAt(P.diner, -3.f), SF_LAST_CALL, SF_COLLATERAL, SF_PINK_SLIPS, 0, 1,
                 makeMission<MissionLastCall>);
    d.timeFrom = 19.f;
    d.timeTo = 4.f;
    defs.push_back(d);

    // ---- Act 2: the city
    const u32 kBoats = classBit(Vehicles::VC_BOAT) | classBit(Vehicles::VC_JETSKI) | classBit(Vehicles::VC_AIRBOAT);
    defs.push_back(storyDef("dead_air", "Dead Air", "Kit", 'K', startAt(P.pulseFm, -3.f), SF_DEAD_AIR, SF_LAST_CALL, -1, 0, 2,
                            makeMission<MissionDeadAir>));
    d = storyDef("velvet_rope", "Velvet Rope", "Kit", 'K', startAt(P.clubRiptide, -20.f), SF_VELVET_ROPE, SF_DEAD_AIR, -1, 0, 2,
                 makeMission<MissionVelvetRope>);
    d.timeFrom = 21.f;
    d.timeTo = 4.f;
    defs.push_back(d);
    defs.push_back(storyDef("bagman", "The Bagman", "Rook", 'R', startAt(P.policeHq, 150.f), SF_BAGMAN, SF_LAST_CALL, -1, 1, 2,
                            makeMission<MissionBagman>));
    d = storyDef("sawgrass_run", "Sawgrass Run", "Jonah", 'J', P.sawgrassDock.xy(), SF_SAWGRASS_RUN, SF_BAGMAN, -1, 1, 2,
                 makeMission<MissionSawgrassRun>);
    d.needsClasses = kBoats;
    defs.push_back(d);
    d = storyDef("riptide", "Riptide", "Tomas", 'T', startAt(P.boatyard, 2.f), SF_RIPTIDE, SF_VELVET_ROPE, -1, 0, 2, makeMission<MissionRiptide>);
    d.needsClasses = kBoats;
    defs.push_back(d);
    d = storyDef("heavy_lift", "Heavy Lift", "Rook", 'R', P.portGate.curb.xy() - vec2(120.f, 0.f), SF_HEAVY_LIFT, SF_SAWGRASS_RUN, -1, 1, 2,
                 makeMission<MissionHeavyLift>);
    d.timeFrom = 21.f;
    d.timeTo = 5.f;
    defs.push_back(d);
    d = storyDef("fireworks", "Fireworks", "Kit", 'K', P.pierRamp.xy() - vec2(6.f, 0.f), SF_FIREWORKS, SF_RIPTIDE, SF_HEAVY_LIFT, 0, 2,
                 makeMission<MissionFireworks>);
    d.timeFrom = 20.f;
    d.timeTo = 3.f;
    defs.push_back(d);
    d = storyDef("second_chance", "Second Chance", "Jonah", 'J', startAt(P.airport, 0.f), SF_SECOND_CHANCE, SF_HEAVY_LIFT, -1, 1, 2,
                 makeMission<MissionSecondChance>);
    d.needsClasses = classBit(Vehicles::VC_HELI);
    defs.push_back(d);
    d = storyDef("paper_trail", "Paper Trail", "Kit", 'K', P.keyCoral.curb.xy() + P.keyCoral.streetDir * -60.f, SF_PAPER_TRAIL, SF_FIREWORKS, -1, 0, 2,
                 makeMission<MissionPaperTrail>);
    d.timeFrom = 22.f;
    d.timeTo = 4.5f;
    defs.push_back(d);

    // ---- Act 3: the big score
    defs.push_back(storyDef("blueprints", "Blueprints", "Rook", 'R', startAt(P.rookShop, 3.f), SF_BLUEPRINTS, SF_PAPER_TRAIL, SF_SECOND_CHANCE, 1, 3,
                            makeMission<MissionBlueprints>));
    defs.push_back(storyDef("dress_rehearsal", "Dress Rehearsal", "Rook", 'R', startAt(P.rookShop, -4.f), SF_DRESS_REHEARSAL, SF_BLUEPRINTS, -1, 0, 3,
                            makeMission<MissionDressRehearsal>));
    defs.push_back(storyDef("solaris_one", "Solaris One", "The Crew", 'H', startAt(P.rookShop, 3.f), SF_SOLARIS_ONE, SF_DRESS_REHEARSAL, -1, 1, 3,
                            makeMission<MissionSolarisOne>));
    defs.push_back(storyDef("overseas", "Overseas", "Mama Lucha", 'L', startAt(P.redland, 0.f), SF_OVERSEAS, SF_SOLARIS_ONE, -1, 0, 3,
                            makeMission<MissionOverseas>));
    defs.push_back(storyDef("signal", "Signal", "Kit", 'K', startAt(P.pulseFm, -3.f), SF_SIGNAL, SF_OVERSEAS, -1, 0, 3, makeMission<MissionSignal>));

    // ---- side activities
    auto side = [&](const char* id, const char* title, const char* contact, vec2 start, int req, int sets, UI::BlipIcon icon,
                    std::function<Mission*()> create) {
        MissionDef s = storyDef(id, title, contact, 0, start, -1, req, -1, -1, 0, create);
        s.setsFlag = sets;
        s.repeatable = true;
        s.icon = icon;
        return s;
    };
    // jobs started from their vehicles (G / D-pad up in a taxi, police car or ambulance)
    MissionDef job = side("taxi", "Taxi Fares", "Sol Cabs", P.taxiDepot.pos.xy(), -1, SIDE_TAXI, UI::BLIP_TAXI_JOB, makeMission<MissionTaxi>);
    job.hidden = true;
    defs.push_back(job);
    job = side("vigilante", "Vigilante", "Police car", P.policeHq.pos.xy(), -1, SIDE_VIGILANTE, UI::BLIP_VIGILANTE, makeMission<MissionVigilante>);
    job.hidden = true;
    defs.push_back(job);
    job = side("paramedic", "Paramedic", "Ambulance", P.hospital.pos.xy(), -1, SIDE_PARAMEDIC, UI::BLIP_HOSPITAL, makeMission<MissionParamedic>);
    job.hidden = true;
    defs.push_back(job);
    defs.push_back(side("courier", "Rapido Couriers", "Rapido Couriers", P.courierDepot.pos.xy(), SF_LOW_TIDE, SIDE_COURIER, UI::BLIP_DELIVERY_JOB,
                        makeMission<MissionCourier>));
    defs.push_back(side("range", "Shooting Range", "Palmetto Arms", placeOffset(g, P.rangeFlats, 0.f, 4.f).xy(), -1, SIDE_RANGE, UI::BLIP_GUN_SHOP,
                        makeMission<MissionRange>));
    defs.push_back(side("bounty", "Bail Bonds", "Palmera Bail Bonds", resolveFrontage(g, vec2(3160.f, -430.f)).pos.xy(), SF_REPO_MAN, -1,
                        UI::BLIP_HIDEOUT, makeMission<MissionBounty>));
    defs.push_back(side("wishlist", "Rook's Wishlist", "Rook", startAt(P.rookShop, -14.f), SF_REPO_MAN, -1, UI::BLIP_GARAGE,
                        makeMission<MissionWishlist>));
    // strangers: one-time short stories, one part at a time (Mari)
    {
        vec2 rosa = rosaHome(g).pos.xy();
        auto stranger = [&](const char* id, const char* title, int sets, int req, int req2, std::function<Mission*()> create) {
            MissionDef s = storyDef(id, title, "Rosa", '?', rosa, -1, req, req2, 0, 0, create);
            s.setsFlag = sets;
            s.icon = UI::BLIP_FRIEND;
            return s;
        };
        defs.push_back(stranger("rosa_1", "Abuela Rosa: Tow Away", SIDE_ROSA_1, SF_LOW_TIDE, -1, makeMission<MissionRosaTowAway>));
        defs.push_back(stranger("rosa_2", "Abuela Rosa: Dominoes Night", SIDE_ROSA_2, SIDE_ROSA_1, SF_DRY_DOCK, makeMission<MissionRosaDominoes>));
        defs.push_back(stranger("rosa_3", "Abuela Rosa: Sunday Drive", SIDE_ROSA_3, SIDE_ROSA_2, SF_PRESSURE, makeMission<MissionRosaSunday>));
    }
    {
        vec2 velma = velmaHome(g).pos.xy();
        auto stranger = [&](const char* id, const char* title, int sets, int req, int req2, std::function<Mission*()> create) {
            MissionDef s = storyDef(id, title, "Velma", '?', velma, -1, req, req2, 1, 0, create);
            s.setsFlag = sets;
            s.icon = UI::BLIP_FRIEND;
            return s;
        };
        defs.push_back(stranger("velma_1", "Repo Karma", SIDE_VELMA_1, SF_DRY_DOCK, -1, makeMission<MissionVelmaKarma>));
        defs.push_back(stranger("velma_2", "Night Shift", SIDE_VELMA_2, SIDE_VELMA_1, SF_COLLATERAL, makeMission<MissionVelmaShift>));
        defs.push_back(stranger("velma_3", "Collections", SIDE_VELMA_3, SIDE_VELMA_2, SF_LAST_CALL, makeMission<MissionVelmaCollections>));
    }
    {
        vec2 jaz = jazSpot(g).pos.xy();
        auto stranger = [&](const char* id, const char* title, int sets, int req, int req2, std::function<Mission*()> create) {
            MissionDef s = storyDef(id, title, "Jaz", '?', jaz, -1, req, req2, -1, 0, create);
            s.setsFlag = sets;
            s.icon = UI::BLIP_FRIEND;
            return s;
        };
        defs.push_back(stranger("jaz_1", "Jaz: Golden Hour", SIDE_JAZ_1, SF_PRESSURE, -1, makeMission<MissionJazGoldenHour>));
        defs.push_back(stranger("jaz_2", "Jaz: Viral", SIDE_JAZ_2, SIDE_JAZ_1, SF_DEAD_AIR, makeMission<MissionJazViral>));
        defs.push_back(stranger("jaz_3", "Jaz: Deleted", SIDE_JAZ_3, SIDE_JAZ_2, SF_VELVET_ROPE, makeMission<MissionJazDeleted>));
    }
    for (const RaceSpec& rs : raceSpecs()) {
        Place startPlace = resolvePlace(g, rs.via[0]);
        vec2 sp = rs.domain == 1 ? startPlace.pos.xy() : startPlace.curb.xy();
        RaceSpec copy = rs;
        MissionDef r = side(rs.id, rs.name, rs.domain == 1 ? "Boat race" : "Street race", sp, SF_LOW_TIDE, rs.sideFlag,
                            rs.domain == 1 ? UI::BLIP_BOAT : UI::BLIP_RACE, [copy]() { return (Mission*)new MissionRace(copy); });
        if (rs.night) {
            r.timeFrom = 20.f;
            r.timeTo = 5.f;
        }
        if (rs.domain == 1) r.needsClasses = kBoats;
        defs.push_back(r);
    }
    const u32 kAir = classBit(Vehicles::VC_PLANE) | classBit(Vehicles::VC_HELI);
    for (int lesson = 0; lesson < 3; lesson++) {
        static const char* const kIds[3] = {"flight_1", "flight_2", "flight_3"};
        static const char* const kTitles[3] = {"Flight School: Circuit", "Flight School: Pads", "Flight School: Coastal Run"};
        MissionDef f = side(kIds[lesson], kTitles[lesson], "Flight school", startAt(P.airport, -8.f + 8.f * lesson), SF_LOW_TIDE, SIDE_FLIGHT_1 + lesson,
                            UI::BLIP_PLANE, [lesson]() { return (Mission*)new MissionFlightSchool(lesson); });
        f.needsClasses = lesson == 1 ? classBit(Vehicles::VC_HELI) : kAir;
        if (lesson > 0) f.requiresFlag2 = SIDE_FLIGHT_1 + lesson - 1;
        defs.push_back(f);
    }
    LOG("Missions registered: %d", (int)defs.size());
}

}  // namespace Game
