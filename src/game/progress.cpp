// Collectibles (Neon Shells), pickup placement (health/armor/weapons around the map) and progress tracking.
#include "gameworld.h"

namespace Game {

namespace progress_detail {

constexpr int kShellCount = 60;

// Deterministic collectible locations: spread over the whole map, snapped next to roads/beaches/piers.
std::vector<vec3> computeShellSpots(const World::WorldMap& map, const World::RoadNetwork& roads) {
    std::vector<vec3> out;
    u32 seed = 0x5EA5E11u;
    int attempts = 0;
    while ((int)out.size() < kShellCount && attempts < 20000) {
        attempts++;
        seed = hash32(seed + 0x9E3779B9u);
        float x = (hashToFloat(seed) * 2.f - 1.f) * (World::kWorldHalf - 600.f);
        float y = (hashToFloat(hash32(seed ^ 0xABCDu)) * 2.f - 1.f) * (World::kWorldHalf - 600.f);
        if (map.isWater(x, y)) continue;
        float h = map.heightAt(x, y);
        if (h < 0.3f) continue;
        // stay near a road (reachable) but a bit off it
        float s = 0, dist = 0, side = 0;
        int e = roads.nearestEdge(vec2(x, y), 250.f, &s, &dist, &side);
        if (e < 0) continue;
        const World::RoadEdge& ed = roads.edges[e];
        vec3 c = ed.posAt(s);
        vec3 t = ed.tangentAt(s);
        vec2 n = normalize(vec2(-t.y, t.x)) * (side >= 0 ? 1.f : -1.f);
        vec2 p = c.xy() + n * (ed.halfWidth + ed.sidewalk + 4.f + hashToFloat(seed >> 3) * 14.f);
        if (map.isWater(p.x, p.y)) continue;
        // not inside a building
        if (World::gBuildings && World::gBuildings->pointInBuilding(p, 2.5f)) continue;
        // keep them apart
        bool close = false;
        for (auto& o : out)
            if (length(o.xy() - p) < 450.f) {
                close = true;
                break;
            }
        if (close) continue;
        out.push_back(vec3(p, map.heightAt(p.x, p.y)));
    }
    return out;
}

}  // namespace progress_detail

using namespace progress_detail;

void GameWorld::placeWorldPickups() {
    placeStaticBlips();
    // Neon Shell collectibles
    std::vector<vec3> spots = computeShellSpots(*map, *roads);
    pinfo.collectibleFlags.resize(spots.size(), 0);
    for (int i = 0; i < (int)spots.size(); i++) {
        if (pinfo.collectibleFlags[i]) continue;
        Pickup pk;
        pk.used = true;
        pk.type = PICK_COLLECTIBLE;
        pk.collectibleId = i;
        vec3 p = spots[i];
        float gz = groundHeight(p.x, p.y, p.z + 2.f);
        pk.pos = dvec3(p.x, p.y, gz + 0.1f);
        pickups.push_back(pk);
    }
    shellCount = (int)spots.size();
    // Health and armor near hospitals / police stations, weapons in back alleys (respawning)
    struct Fixed {
        vec2 p;
        PickupType t;
        WeaponType w;
        int amount;
    };
    const Fixed fixed[] = {
        {vec2(1660, 1080), PICK_HEALTH, WPN_FISTS, 100}, {vec2(3640, 3230), PICK_HEALTH, WPN_FISTS, 100},
        {vec2(-2380, 1830), PICK_HEALTH, WPN_FISTS, 100}, {vec2(5230, -880), PICK_HEALTH, WPN_FISTS, 100},
        {vec2(2210, 640), PICK_ARMOR, WPN_FISTS, 100}, {vec2(-1200, 2400), PICK_ARMOR, WPN_FISTS, 100},
        {vec2(2890, 1720), PICK_WEAPON, WPN_BAT, 1}, {vec2(1320, 2210), PICK_WEAPON, WPN_PISTOL, 45},
        {vec2(4020, 480), PICK_WEAPON, WPN_SHOTGUN, 16}, {vec2(-600, 900), PICK_WEAPON, WPN_KNIFE, 1},
        {vec2(6100, 3900), PICK_WEAPON, WPN_SMG, 90}, {vec2(-4200, -2600), PICK_WEAPON, WPN_RIFLE, 60},
        {vec2(800, -1400), PICK_WEAPON, WPN_GRENADE, 4}, {vec2(-7300, 6100), PICK_WEAPON, WPN_SNIPER, 10},
        {vec2(3300, -2600), PICK_WEAPON, WPN_MOLOTOV, 5},
    };
    for (const Fixed& f : fixed) {
        float s = 0, side = 0;
        int e = roads->nearestEdge(f.p, 300.f, &s, nullptr, &side);
        vec2 p = f.p;
        if (e >= 0) {
            const World::RoadEdge& ed = roads->edges[e];
            vec3 c = ed.posAt(s);
            vec3 t = ed.tangentAt(s);
            vec2 n = normalize(vec2(-t.y, t.x)) * (side >= 0 ? 1.f : -1.f);
            p = c.xy() + n * (ed.halfWidth + Max(ed.sidewalk, 1.f) * 0.6f);
        }
        Pickup pk;
        pk.used = true;
        pk.type = f.t;
        pk.weapon = f.w;
        pk.amount = f.amount;
        pk.respawn = 120.f;
        pk.pos = dvec3(p.x, p.y, groundHeight(p.x, p.y, 200.f) + 0.05f);
        pickups.push_back(pk);
    }
}

// Hospitals and police stations (respawn/release points) on the map legend.
void GameWorld::placeStaticBlips() {
    staticBlips.clear();
    // hospitals as the world built them (sites: PK_HOSPITAL, on real street frontages in their towns); the fixed points
    // below only if the world has none
    int nHosp = 0;
    if (World::gSites && World::gSites->generated)
        for (const World::NamedPlace& h : World::gSites->places)
            if (h.kind == World::PK_HOSPITAL) {
                UI::Blip b;
                b.pos = h.pos;
                b.icon = UI::BLIP_HOSPITAL;
                b.shortRange = true;
                b.label = h.name.c_str();
                staticBlips.push_back(b);
                nHosp++;
            }
    static const vec2 hospitals[] = {vec2(1650, 1050), vec2(3650, 3200), vec2(-2400, 1800), vec2(5200, -900), vec2(-6400, 5200),
                                     vec2(900, 6800), vec2(-3900, -3100), vec2(7400, 4800)};
    static const char* hospitalNames[] = {"Tidewater General Hospital", "Midtown General", "Westbrook Hospital", "Sol Beach Clinic",
                                          "Harlow County Hospital", "Okahatchee Medical", "Redland Health", "Fort Castell Hospital"};
    for (int i = 0; i < 8 && nHosp == 0; i++) {
        UI::Blip b;
        b.pos = hospitals[i];
        b.icon = UI::BLIP_HOSPITAL;
        b.shortRange = true;
        b.label = hospitalNames[i];
        staticBlips.push_back(b);
    }
    static const vec2 stations[] = {vec2(2900, 300), vec2(1400, -500), vec2(4100, 2300), vec2(5050, 1800), vec2(-1500, 3200),
                                    vec2(2000, -3300), vec2(-5000, 5000), vec2(6600, 7300)};
    static const char* stationNames[] = {"PSPD Central", "PSPD Calle Luna", "PSPD Midtown", "Sol Beach Police", "Flats Precinct",
                                         "Grove Precinct", "Harlow Sheriff", "Fort Castell Police"};
    for (int i = 0; i < 8; i++) {
        float s = 0, side = 0;
        vec2 p = stations[i];
        int e = roads->nearestEdge(p, 300.f, &s, nullptr, &side);
        if (e >= 0) p = roads->edges[e].posAt(s).xy();
        UI::Blip b;
        b.pos = p;
        b.icon = UI::BLIP_POLICE_STATION;
        b.shortRange = true;
        b.label = stationNames[i];
        staticBlips.push_back(b);
    }
}

void GameWorld::onPickupCollected(const Pickup& pk) {
    if (pk.type == PICK_COLLECTIBLE && pk.collectibleId >= 0) {
        if (pk.collectibleId < (int)pinfo.collectibleFlags.size() && !pinfo.collectibleFlags[pk.collectibleId]) {
            pinfo.collectibleFlags[pk.collectibleId] = 1;
            pinfo.collectiblesFound++;
            long long reward = 250;
            if (pinfo.collectiblesFound == shellCount) reward = 50000;
            pinfo.money += reward;
            notify("NEON SHELL", StrFormat("%d of %d found  (+$%lld)", pinfo.collectiblesFound, shellCount, reward));
        }
    }
    if (pk.type == PICK_PACKAGE) missionPackagesCollected++;
}

void GameWorld::notify(const std::string& title, const std::string& text) {
    hudNoteTitle = title;
    hudNote = text;
    hudNoteTimer = 5.f;
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_UI_NOTIFY, 0.6f);
#endif
}

void GameWorld::help(const std::string& text, float seconds) {
    hudHelp = text;
    hudHelpTimer = seconds;
}

void GameWorld::bigMessage(const std::string& text, const std::string& sub, u32 color) {
    hudBig = text;
    hudBigSub = sub;
    hudBigColor = color;
    hudBigTime = 0.f;
}

void GameWorld::subtitle(const std::string& speaker, const std::string& text, float seconds, u32 color) {
    subSpeaker = speaker;
    subText = Speech::displayText(text.c_str());   // strip speech markup ([angry], [pause], stage directions)
    subTimer = seconds;
    subColor = color;
}

}  // namespace Game
