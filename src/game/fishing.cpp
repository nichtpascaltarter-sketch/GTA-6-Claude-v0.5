// Fishing: cast from piers, docks, seawalls and river banks on foot, or from any boat that has come to a stop. A catch
// game that reads at a glance (hold to charge the cast, wait for the float to dip, strike, then reel while keeping the
// line tension out of the red: the fish runs in bursts and tires), sixteen species by habitat (the coast off the piers,
// the Keys flats, deep water offshore, the Rio Sol and the city canals, the Sawgrass, Lake Okahatchee) and time of day,
// a cooler the Palmera Angler bait shops buy from, and their trophy board (best weight per species, a trophy class for
// each). Tallies live in the extended story flags (EX_FISH_*); completion in SIDE_FISH_ALL / SIDE_FISH_TROPHY.
#include "missions.h"

namespace Game {
namespace mu {

enum FishHabitat : u8 { HAB_COAST = 0, HAB_FLATS, HAB_OFFSHORE, HAB_RIVER, HAB_SWAMP, HAB_LAKE, HAB_COUNT };
const char* const kHabitatName[HAB_COUNT] = {"the coast", "the Keys flats", "deep water", "the river", "the Sawgrass", "Lake Okahatchee"};

constexpr u8 habBit(int h) { return (u8)(1u << h); }

struct FishSpecies {
    const char* name;
    u8 habitats;          // FishHabitat bits
    float minLb, maxLb;   // weight range (most fish come in near the bottom of it)
    float trophyLb;       // trophy class from this weight
    float pricePerLb;     // what the bait shops pay
    float fight;          // pull and stamina: 0.3 easy .. 1 brutal
    float odds;           // relative odds among the species of a habitat
    u8 hours;             // 0 any time, 1 daylight, 2 low light (dawn, dusk, night)
    const char* tip;      // the trophy board's note on where and when
};

const FishSpecies kFish[] = {
    {"Gray Snapper", habBit(HAB_COAST) | habBit(HAB_FLATS), 0.8f, 7.f, 6.f, 4.5f, 0.35f, 1.f, 0, "Piers and docks on the coast and in the Keys, any hour."},
    {"Sheepshead", habBit(HAB_COAST), 1.f, 9.f, 7.8f, 3.5f, 0.4f, 0.8f, 1, "Around the pier pilings on the coast, in daylight."},
    {"Snook", habBit(HAB_COAST) | habBit(HAB_RIVER), 2.f, 24.f, 20.5f, 5.5f, 0.65f, 0.5f, 2, "The river and the coast at dawn, dusk or after dark."},
    {"Redfish", habBit(HAB_COAST) | habBit(HAB_FLATS), 2.f, 22.f, 19.f, 5.f, 0.6f, 0.6f, 0, "Shallow water on the coast and the Keys flats."},
    {"Tarpon", habBit(HAB_COAST) | habBit(HAB_FLATS), 25.f, 150.f, 130.f, 3.f, 1.f, 0.14f, 2, "The silver king. Coast and Keys in low light. Patience."},
    {"Bonefish", habBit(HAB_FLATS), 2.f, 13.f, 11.3f, 9.f, 0.8f, 0.4f, 1, "The Keys flats in daylight. Fast, long runs."},
    {"Great Barracuda", habBit(HAB_FLATS) | habBit(HAB_OFFSHORE), 5.f, 48.f, 41.5f, 3.2f, 0.75f, 0.45f, 0, "The Keys and deep water. Mind the teeth."},
    {"Mahi-mahi", habBit(HAB_OFFSHORE), 6.f, 48.f, 41.5f, 6.f, 0.75f, 0.8f, 1, "Deep water far from shore, from a boat, in daylight."},
    {"Kingfish", habBit(HAB_OFFSHORE), 8.f, 55.f, 48.f, 4.5f, 0.7f, 0.6f, 0, "Deep water far from shore, from a boat."},
    {"Sailfish", habBit(HAB_OFFSHORE), 35.f, 120.f, 107.f, 4.f, 1.f, 0.14f, 1, "Deep water in daylight. The fight of a lifetime."},
    {"Largemouth Bass", habBit(HAB_RIVER) | habBit(HAB_SWAMP) | habBit(HAB_LAKE), 1.f, 13.f, 11.2f, 6.f, 0.5f, 1.f, 0, "Fresh water everywhere: river, Sawgrass, lake."},
    {"Peacock Bass", habBit(HAB_RIVER), 1.f, 10.f, 8.6f, 8.f, 0.6f, 0.45f, 1, "The Rio Sol and the city canals, in daylight."},
    {"Channel Catfish", habBit(HAB_RIVER) | habBit(HAB_SWAMP) | habBit(HAB_LAKE), 2.f, 32.f, 27.5f, 3.f, 0.55f, 0.8f, 2, "Fresh water after dark."},
    {"Florida Gar", habBit(HAB_SWAMP) | habBit(HAB_LAKE), 2.f, 13.f, 11.3f, 2.5f, 0.45f, 0.8f, 0, "The Sawgrass and the lake."},
    {"Bowfin", habBit(HAB_SWAMP), 2.f, 15.f, 13.f, 3.f, 0.6f, 0.5f, 2, "The Sawgrass at dawn or dusk."},
    {"Black Crappie", habBit(HAB_LAKE), 0.5f, 4.f, 3.4f, 7.f, 0.3f, 0.85f, 0, "The Lake Okahatchee docks."},
};
constexpr int kFishCount = (int)(sizeof(kFish) / sizeof(kFish[0]));
static_assert(kFishCount <= 16, "the trophy board keeps two species per saved int (EX_FISH_BEST .. +7)");
constexpr int kCoolerMax = 10;

// best weight per species in tenths of a pound, two species per int (low / high 16 bits)
float fishBest(GameWorld& g, int s) {
    int v = flag(g, EX_FISH_BEST + s / 2);
    return (float)((s & 1) ? ((unsigned)v >> 16) & 0xffffu : (unsigned)v & 0xffffu) * 0.1f;
}
void setFishBest(GameWorld& g, int s, float lb) {
    int slot = EX_FISH_BEST + s / 2;
    unsigned v = (unsigned)flag(g, slot), t = (unsigned)Clamp((int)(lb * 10.f + 0.5f), 0, 65535);
    v = (s & 1) ? ((v & 0xffffu) | (t << 16)) : ((v & 0xffff0000u) | t);
    setFlag(g, slot, (int)v);
}
bool fishLanded(GameWorld& g, int s) { return (flag(g, EX_FISH_SPECIES) >> s) & 1; }
int fishSpeciesLanded(GameWorld& g) {
    int n = 0;
    for (int s = 0; s < kFishCount; s++) n += fishLanded(g, s);
    return n;
}
int fishValue(int s, float lb) {
    bool trophy = lb >= kFish[s].trophyLb;
    return Max(3, (int)(lb * kFish[s].pricePerLb * (trophy ? 1.5f : 1.f) + 0.5f));
}

// ------------------------------------------------------------------------------------------------------------------
// Where the water is and what lives in it
World::Region nearestLandRegion(GameWorld& g, vec2 p, float maxR) {
    for (float r = 24.f; r <= maxR; r += 36.f)
        for (int k = 0; k < 12; k++) {
            float a = kTwoPi * k / 12.f;
            vec2 q = p + vec2(cosf(a), sinf(a)) * r;
            if (!g.map->isWater(q.x, q.y)) return g.map->regionAt(q.x, q.y);
        }
    return World::REG_OCEAN;
}

u8 fishHabitatAt(GameWorld& g, vec2 p, bool boat) {
    float w = g.map->waterAt(p.x, p.y);
    if (w <= World::kNoWater + 1.f) return HAB_COAST;
    if (fabsf(w - World::kLakeLevel) < 0.35f) return HAB_LAKE;   // Lake Okahatchee's surface sits above sea level
    World::Region here = g.map->regionAt(p.x, p.y);
    if (here == World::REG_SAWGRASS) return HAB_SWAMP;
    World::Region land = nearestLandRegion(g, p, 420.f);
    float depth = w - g.map->heightAt(p.x, p.y);
    if (land == World::REG_SAWGRASS) return HAB_SWAMP;
    if (land == World::REG_KEYS || land == World::REG_KEY_TOWN) return depth > 9.f && boat ? HAB_OFFSHORE : HAB_FLATS;
    if (boat && depth > 6.f && g.map->coastDistance(p.x, p.y) < -350.f) return HAB_OFFSHORE;
    if (here != World::REG_OCEAN && land != World::REG_BEACH && land != World::REG_KEY_CORAL && land != World::REG_BAY_ISLAND &&
        land != World::REG_PORT && land != World::REG_GULF_TOWN)
        return HAB_RIVER;   // inland water in the city and the suburbs: the Rio Sol and the canals
    return HAB_COAST;
}

bool fishLowLight(float hour) { return hour < 8.f || hour > 18.f; }

// A fish for this habitat and hour: species by their odds (their hours favoured), weight skewed toward the small end
int pickFish(GameWorld& g, u8 hab, Rng& rng, float& weight) {
    float hour = g.env ? g.env->timeOfDay : 12.f;
    float w[kFishCount], sum = 0.f;
    for (int s = 0; s < kFishCount; s++) {
        w[s] = 0.f;
        if (!(kFish[s].habitats & habBit(hab))) continue;
        float t = 1.f;
        if (kFish[s].hours == 1) t = fishLowLight(hour) ? 0.3f : 1.f;
        if (kFish[s].hours == 2) t = fishLowLight(hour) ? 1.6f : 0.35f;
        w[s] = kFish[s].odds * t;
        sum += w[s];
    }
    int pick = 0;
    float r = rng.f() * sum;
    for (int s = 0; s < kFishCount; s++) {
        if (w[s] <= 0.f) continue;
        pick = s;
        r -= w[s];
        if (r <= 0.f) break;
    }
    const FishSpecies& f = kFish[pick];
    float u = rng.f();
    weight = f.minLb + (f.maxLb - f.minLb) * powf(u, 3.f);
    weight = floorf(weight * 10.f + 0.5f) * 0.1f;
    return pick;
}

// ------------------------------------------------------------------------------------------------------------------
// The session
enum FishPhase : u8 { FP_OFF = 0, FP_READY, FP_CHARGE, FP_FLY, FP_WAIT, FP_BITE, FP_FIGHT, FP_LANDED, FP_LOST };

struct FishSpot {
    std::string name;
    vec3 pos;
    u8 hab = HAB_COAST;
};
struct FishMarket {
    std::string name;
    vec3 pos;
};

struct FishInput {
    bool castDown = false, castPressed = false, castReleased = false, quit = false, move = false;
};

struct FishingState {
    bool init = false;
    std::vector<FishSpot> spots;
    std::vector<FishMarket> markets;
    Rng rng = Rng(0xF15Bu, 0x7Au);
    // session
    int phase = FP_OFF;
    float t = 0.f;
    bool fromBoat = false;
    int boat = -1;
    int savedWeapon = WPN_FISTS;
    u8 savedCarry = CARRY_NONE;
    u8 hab = HAB_COAST;
    float power = 0.f, powerDir = 1.f;
    vec2 dir = vec2(0.f, 1.f);
    vec3 origin, target, bobber, flyFrom;
    float castDist = 0.f, flyT = 0.f;
    float biteAt = 0.f, nibbleAt = 0.f, dip = 0.f, window = 0.f;
    int species = -1;
    float weight = 0.f;
    float dist = 0.f, maxDist = 0.f, tension = 0.f, stamina = 1.f, runT = 0.f, pull = 0.f, slackT = 0.f, sway = 0.f, splashT = 0.f;
    bool running = false;
    float runSide = 1.f;
    // result card
    int cardSpecies = -1;
    float cardWeight = 0.f;
    int cardValue = 0;
    bool cardTrophy = false, cardNew = false, cardRecord = false, cardKept = false;
    std::string cardTitle, cardText;
    // cooler
    std::vector<std::pair<int, float>> cooler;
    // input (captured before the player update, which then gets none)
    FishInput in;
    float hintCd = 0.f;
    int hintKind = -1;
    // bait shop
    int marketInside = -1;
    int menuPage = 0;   // 0 the counter, 1 the trophy board
    // tests (--missiontest fish_*)
    bool test = false;
    FishInput testIn;
    vec2 testDir = vec2(0.f);
    int landed = 0, lost = 0;
    int lastSpecies = -1;
    u8 lastHab = HAB_COAST;
};
FishingState gFish;

// Lake Okahatchee's shore on the Lake Town side: walk from the town toward the lake's middle until the next steps are
// lake water (its surface sits at World::kLakeLevel); stand on the last dry spot, facing the water
bool fishLakeShore(GameWorld& g, vec3& stand, vec2& dir) {
    vec2 from = gPlaces.lakeTown.pos.xy(), lakeC(-2400.f, 7000.f);
    dir = normalize(lakeC - from);
    vec2 p = from;
    for (int i = 0; i < 500; i++) {
        vec2 q = p + dir * 3.f;
        float w = g.map->waterAt(q.x + dir.x * 3.f, q.y + dir.y * 3.f);
        if (fabsf(w - World::kLakeLevel) < 0.3f && isWaterAt(g, q + dir * 3.f, 0.6f)) {
            stand = vec3(p, groundAt(g, p.x, p.y, World::kLakeLevel + 6.f));
            return true;
        }
        p = q;
    }
    return false;
}

void fishingSetupSpots(GameWorld& g) {
    FishingState& F = gFish;
    F.spots.clear();
    F.markets.clear();
    const Places& P = gPlaces;
    auto spot = [&](const char* name, vec3 p, u8 hab) {
        FishSpot s;
        s.name = name;
        s.pos = p;
        s.hab = hab;
        F.spots.push_back(s);
    };
    spot("Fishing: Sol Beach Pier", P.pierEnd, HAB_COAST);
    spot("Fishing: Key Coral Marina", P.keyCoralMarina.pos, HAB_COAST);
    spot("Fishing: Rio Sol", P.riverLaunch, HAB_RIVER);
    spot("Fishing: Sawgrass causeway", P.sawgrassDock, HAB_SWAMP);
    spot("Fishing: Ten Palms", P.tenPalms.pos, HAB_COAST);
    bool lake = false;
    int keys = 0;
    if (World::gSites) {
        for (const World::SiteElem& e : World::gSites->elems) {
            if (e.kind != World::SK_DOCK) continue;
            vec2 tip = e.c + e.ax * Max(e.p[0] - 2.f, 4.f);
            if (!lake && fabsf(e.z - World::kLakeLevel) < 0.5f) {
                lake = true;
                spot("Fishing: Lake Okahatchee", vec3(tip, e.z + 1.f), HAB_LAKE);
            } else if (keys < 3 && e.c.y < -3000.f && e.variant == 0) {
                keys++;
                spot("Fishing: Keys dock", vec3(tip, e.z + 1.f), HAB_FLATS);
            }
        }
    }
    if (!lake) {
        vec3 stand;
        vec2 dir;
        if (fishLakeShore(g, stand, dir)) {
            lake = true;
            spot("Fishing: Lake Okahatchee", stand, HAB_LAKE);
        }
    }
    auto market = [&](const char* name, vec3 p) {
        FishMarket m;
        m.name = name;
        m.pos = p;
        F.markets.push_back(m);
    };
    LOG("fishing: %d spots (lake %s, %d Keys docks)", (int)F.spots.size(), lake ? "yes" : "no", keys);
    market("Palmera Angler Bait & Tackle", P.pierRamp);
    market("Palmera Angler Bait & Tackle", P.sawgrassDock + vec3(normalize(P.sawgrassDock.xy() - P.sawgrassWater.xy() + vec2(1e-3f, 0.f)) * 14.f, 0.f));
    market("Palmera Angler Bait & Tackle", P.keyCoralMarina.door);
    for (FishMarket& m : F.markets) m.pos.z = groundAt(g, m.pos.x, m.pos.y, m.pos.z + 3.f);
}

// The rod's tip (on foot: from the hand holding the rod, as carry.cpp draws it; in a boat: over the gunwale)
vec3 fishRodTip(GameWorld& g, vec3* handOut = nullptr, vec3* dirOut = nullptr) {
    Ped* pl = g.playerPed();
    if (!pl) return vec3(0.f);
    FishingState& F = gFish;
    vec3 up(0.f, 0.f, 1.f), fwd(dirFromYaw(pl->yaw), 0.f);
    vec3 hand, rodDir;
    if (!F.fromBoat && pl->charIndex >= 0 && pl->charIndex < (int)g.chars.size()) {
        vec3 gpos, gaxis, gpalm;
        Anim::handGrip(g.chars[pl->charIndex].skel, pl->bones, true, gpos, gaxis, gpalm);
        mat3 R = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), pl->yaw));
        hand = pl->pos.toVec3() + R * gpos;
        rodDir = normalize(fwd * 0.8f + up * 0.6f);
    } else {
        vec3 d(F.dir, 0.f);
        vec3 side = normalize(cross(d, up));
        hand = pl->pos.toVec3() + up * 1.05f + d * 0.3f + side * 0.2f;
        rodDir = normalize(d * 0.75f + up * 0.65f);
    }
    if (handOut) *handOut = hand;
    if (dirOut) *dirOut = rodDir;
    return hand + rodDir * 2.1f;
}

// ---- line, float and (from a boat) the rod
Render::Model* gFishLine = nullptr;
Render::Model* gFishFloat = nullptr;

void fishingSubmit(GameWorld& g) {
    FishingState& F = gFish;
    if (!g.renderer || F.phase == FP_OFF) return;
    Render::DynamicRenderer* dyn = g.renderer->dynamic;
    if (!gFishLine) {
        // hi-vis line and a big float: both drawn a little larger than life so they read at a cast's distance
        MeshData m;
        u32 mono = packRGBA8(0.95f, 0.92f, 0.35f, 1.f);
        cylinderAB(m, vec3(0.f), vec3(0.f, 0.f, 1.f), 0.007f, 0.007f, 4, mono, makeMat(MAT_PLASTIC), false);
        gFishLine = dyn->createModel(m);
        MeshData b;
        u32 red = packRGBA8(0.95f, 0.1f, 0.06f, 1.f), white = packRGBA8(0.97f, 0.97f, 0.94f, 1.f);
        cylinderAB(b, vec3(0.f, 0.f, -0.09f), vec3(0.f, 0.f, 0.f), 0.02f, 0.065f, 12, red, makeMat(MAT_PLASTIC));
        cylinderAB(b, vec3(0.f, 0.f, 0.f), vec3(0.f, 0.f, 0.07f), 0.065f, 0.018f, 12, white, makeMat(MAT_PLASTIC));
        cylinderAB(b, vec3(0.f, 0.f, 0.07f), vec3(0.f, 0.f, 0.19f), 0.008f, 0.006f, 6, red, makeMat(MAT_PLASTIC));
        gFishFloat = dyn->createModel(b);
    }
    vec3 hand, rodDir;
    vec3 tip = fishRodTip(g, &hand, &rodDir);
    Ped* pl = g.playerPed();
    if (F.fromBoat && pl && g.carryModels[CARRY_ROD]) {
        vec3 up(0.f, 0.f, 1.f);
        vec3 zz = normalize(up - rodDir * dot(up, rodDir));
        Render::DrawItem r;
        r.model = g.carryModels[CARRY_ROD];
        r.rot = mat3(cross(rodDir, zz), rodDir, zz);
        r.pos = dvec3(hand);
        r.id = 0xF1500000001ull;
        dyn->submit(r);
    }
    if (F.phase == FP_READY || F.phase == FP_CHARGE) return;
    // the float: in flight, bobbing, dipping on a bite, pulled under and about by a hooked fish
    vec3 fl = F.bobber;
    if (F.phase == FP_FIGHT) fl.z -= 0.12f;
    else fl.z += 0.02f * sinf(F.t * 2.3f) - F.dip * 0.09f;
    if (gFishFloat && F.phase != FP_LANDED && F.phase != FP_LOST) {
        Render::DrawItem d;
        d.model = gFishFloat;
        d.pos = dvec3(fl);
        d.rot = mat3();
        d.castShadow = false;
        d.id = 0xF1500000002ull;
        dyn->submit(d);
    }
    // the line: a few straight pieces sagging with the slack
    vec3 end = F.phase == FP_LANDED || F.phase == FP_LOST ? tip - vec3(0.f, 0.f, 0.6f) : fl + vec3(0.f, 0.f, 0.04f);
    float len = ::length(end - tip);
    if (!gFishLine || len < 0.05f) return;
    float slack = F.phase == FP_FIGHT ? (1.f - Saturate(F.tension * 1.4f)) : (F.phase == FP_FLY ? 0.1f : 0.7f);
    float sag = len * 0.07f * slack;
    const int n = 7;
    vec3 prev = tip;
    for (int i = 1; i <= n; i++) {
        float s = (float)i / n;
        vec3 p = tip + (end - tip) * s - vec3(0.f, 0.f, sag * 4.f * s * (1.f - s));
        vec3 seg = p - prev;
        float sl = ::length(seg);
        if (sl > 1e-3f) {
            vec3 z = seg / sl;
            vec3 x = fabsf(z.z) < 0.95f ? normalize(cross(vec3(0.f, 0.f, 1.f), z)) : vec3(1.f, 0.f, 0.f);
            Render::DrawItem d;
            d.model = gFishLine;
            d.pos = dvec3(prev);
            d.rot = mat3(x, cross(z, x), z);
            d.scale = vec3(1.f, 1.f, sl);
            d.castShadow = false;
            d.id = 0xF1500000010ull + (u64)i;
            dyn->submit(d);
        }
        prev = p;
    }
}

// ---- starting and stopping
bool fishingAllowed(GameWorld& g) {
    Ped* pl = g.playerPed();
    return pl && pl->health > 0.f && g.pinfo.deathTimer <= 0.f && !gMissions.active && g.playerControl && !g.mInCutscene() && !gMenu.open &&
           g.pinfo.wanted == 0 && !g.phone.open && !g.pinfo.busted;
}

// Nothing in the way of a cast (a pier's shop front, a wall, a moored hull): a ray at chest height out over the water
bool fishLineClear(GameWorld& g, vec3 from, vec2 dir, float dist) {
    WorldHit h;
    return !g.raycast(dvec3(from + vec3(0.f, 0.f, 1.4f)), vec3(dir, 0.f), dist, h, g.player, g.playerVehicle(), false);
}

// On foot at the water's edge facing open water, or at the wheel of a boat that has (nearly) stopped
int fishingSpotHere(GameWorld& g, vec2* dirOut = nullptr) {
    Ped* pl = g.playerPed();
    if (!pl) return 0;
    int pv = g.playerVehicle();
    if (pv >= 0) {
        if (!g.isBoat(pv) || pl->seat != 0 || g.vehicles[pv].sim.speed() > 1.6f) return 0;
        vec2 p = vehPos(g, pv).xy();
        if (!g.map->isWater(p.x, p.y)) return 0;
        if (dirOut) *dirOut = normalize(g.rig.cam.forward().xy() + vec2(1e-4f, 0.f));
        return 2;
    }
    if (pl->state != PS_ONFOOT) return 0;
    vec2 f = dirFromYaw(pl->yaw);
    vec3 pp = pl->pos.toVec3();
    if (!fishLineClear(g, pp, f, 7.f)) return 0;
    for (float d : {2.5f, 4.5f, 6.5f}) {
        vec2 q = pp.xy() + f * d;
        float w = g.map->waterAt(q.x, q.y);
        if (w <= World::kNoWater + 1.f || w - g.map->heightAt(q.x, q.y) < 0.6f) continue;
        if (pp.z - w < -0.4f || pp.z - w > 12.f) continue;   // standing above the water, not in it or high over it
        if (dirOut) *dirOut = f;
        return 1;
    }
    return 0;
}

void fishingStart(GameWorld& g, int kind) {
    FishingState& F = gFish;
    Ped* pl = g.playerPed();
    if (!pl) return;
    F.phase = FP_READY;
    F.t = 0.f;
    F.fromBoat = kind == 2;
    F.boat = F.fromBoat ? g.playerVehicle() : -1;
    F.savedWeapon = pl->weapon;
    F.savedCarry = pl->carry;
    pl->weapon = WPN_FISTS;
    pl->aiming = pl->firing = false;
    if (!F.fromBoat) pl->carry = CARRY_ROD;
    F.power = 0.f;
    F.dip = 0.f;
    F.tension = 0.f;
    g.help(F.fromBoat ? "Hold ~i:LMB|RT~ to cast over the side, release to let it fly. ~i:F|Y~ puts the rod away."
                      : "Hold ~i:LMB|RT~ to cast, release to let it fly. ~i:F|Y~ puts the rod away, or just walk off.",
           7.f);
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_WEAPON_SWITCH, 0.35f, 0.8f);
#endif
}

void fishingStop(GameWorld& g) {
    FishingState& F = gFish;
    if (F.phase == FP_OFF) return;
    F.phase = FP_OFF;
    Ped* pl = g.playerPed();
    if (pl) {
        if (!F.fromBoat) pl->carry = F.savedCarry == CARRY_ROD ? (u8)CARRY_NONE : F.savedCarry;
        if (F.savedWeapon > WPN_FISTS && F.savedWeapon < WPN_COUNT && pl->hasWeapon[F.savedWeapon]) pl->weapon = (WeaponType)F.savedWeapon;
    }
    F.boat = -1;
    F.fromBoat = false;
}

bool fishingActive() { return gFish.phase != FP_OFF; }

// A new game or a loaded save (openworld.cpp): the rod goes away and the cooler is emptied (it is not saved)
void fishingReset(GameWorld& g) {
    fishingStop(g);
    gFish.cooler.clear();
    gFish.marketInside = -1;
}

// Before the player update: the rod has the attack button, and the player (or the boat) holds still while it is out.
// The camera still turns (the cast goes where it looks).
void fishingInput(GameWorld& g) {
    FishingState& F = gFish;
    F.in = FishInput();
    if (F.phase == FP_OFF) return;
    Controls& c = g.ctl;
    if (F.test) {
        F.in = F.testIn;
        F.testIn.castPressed = F.testIn.castReleased = false;
    } else {
        F.in.castDown = c.attack.down;
        F.in.castPressed = c.attack.pressed;
        F.in.castReleased = c.attack.released;
        F.in.quit = c.enter.pressed || c.special.pressed || c.jump.pressed;
        F.in.move = F.fromBoat ? (c.accel > 0.5f || c.brake > 0.5f) : ::length(c.move) > 0.55f;
    }
    c.move = vec2(0.f);
    c.attack = c.aim = c.jump = c.sprint = c.enter = c.special = c.crouch = c.reload = c.cover = c.weaponWheel = Button();
    c.weaponScroll = 0;
    c.weaponSlot = -1;
    c.accel = c.brake = c.steer = 0.f;
    c.handbrake = Button();
}

// ---- the catch
void fishingLand(GameWorld& g) {
    FishingState& F = gFish;
    int s = F.species;
    const FishSpecies& fs = kFish[s];
    bool isNew = !fishLanded(g, s);
    float best = fishBest(g, s);
    bool record = F.weight > best + 0.05f;
    if (record) setFishBest(g, s, F.weight);
    setFlag(g, EX_FISH_SPECIES, flag(g, EX_FISH_SPECIES) | (1 << s));
    setFlag(g, EX_FISH_CAUGHT, flag(g, EX_FISH_CAUGHT) + 1);
    bool trophy = F.weight >= fs.trophyLb;
    F.cardSpecies = s;
    F.cardWeight = F.weight;
    F.cardValue = fishValue(s, F.weight);
    F.cardTrophy = trophy;
    F.cardNew = isNew;
    F.cardRecord = record && !isNew;
    F.cardKept = (int)F.cooler.size() < kCoolerMax;
    if (F.cardKept) F.cooler.push_back({s, F.weight});
    F.cardTitle = fs.name;
    F.cardText = F.cardKept ? StrFormat("Into the cooler (%d/%d). Worth about $%d at a Palmera Angler shop.", (int)F.cooler.size(), kCoolerMax, F.cardValue)
                            : std::string("The cooler is full, so this one goes back. Sell your catch at a Palmera Angler shop.");
    F.landed++;
    F.lastSpecies = s;
    F.lastHab = F.hab;
    Ped* pl = g.playerPed();
    vec3 at = pl ? pl->pos.toVec3() : F.bobber;
    spawnFx(FX_WATER_SPLASH, dvec3(F.bobber), vec3(0.f, 0.f, 1.f), 10, 0.6f + Min(F.weight / 60.f, 1.2f));
#ifdef HAVE_AUDIO
    Audio::play(F.weight > 20.f ? Audio::SFX_SPLASH_BIG : Audio::SFX_SPLASH_SMALL, F.bobber, 0.8f);
    Audio::play2D(isNew || trophy ? Audio::SFX_PICKUP_COLLECTIBLE : Audio::SFX_PICKUP_CASH, 0.6f);
#endif
    (void)at;
    // a word from the angler on the special ones
    if (trophy || isNew || record) {
        static const char* const kTrophy[] = {"[happy]Look at the size of you! That one's going on the wall.", "[happy]Trophy class. Nobody is going to believe this."};
        static const char* const kNew[] = {"[happy:0.5]Never landed one of those before.", "[happy:0.4]Well, hello. You're a new one."};
        static const char* const kRecord[] = {"[happy:0.4]Biggest one yet.", "[happy:0.5]Now that is a fish."};
        int k = (int)(F.rng.next() & 1u);
        sayMe(g, trophy ? kTrophy[k] : (isNew ? kNew[k] : kRecord[k]));
    }
    if (trophy && !flag(g, SIDE_FISH_TROPHY)) {
        setFlag(g, SIDE_FISH_TROPHY, 1);
        money(g, 2500);
        g.notify("TROPHY CLASS", StrFormat("A %.1f lb %s. Palmera Angler pays a $2,500 bounty on trophy fish.", F.weight, fs.name));
    }
    if (!flag(g, SIDE_FISH_ALL) && fishSpeciesLanded(g) >= kFishCount) {
        setFlag(g, SIDE_FISH_ALL, 1);
        money(g, 5000);
        g.notify("PALMERA ANGLER", "Every species on the trophy board. The $5,000 grand slam prize is yours.");
    }
    LOG("fishing: landed %s %.1f lb (%s%s%s) in %s", fs.name, F.weight, trophy ? "trophy " : "", isNew ? "new " : "", record ? "record" : "", kHabitatName[F.hab]);
}

void fishingLose(GameWorld& g, const char* why) {
    FishingState& F = gFish;
    F.phase = FP_LOST;
    F.t = 0.f;
    F.cardSpecies = -1;
    F.cardTitle = "IT GOT AWAY";
    F.cardText = why;
    F.lost++;
    g.rumble(0.2f, 0.5f);
#ifdef HAVE_AUDIO
    Audio::play(Audio::SFX_SPLASH_SMALL, F.bobber, 0.7f, 0.8f);
#endif
    LOG("fishing: lost a fish (%s)", why);
}

// ---- per frame (after the player update)
void fishingUpdate(GameWorld& g, float dt) {
    FishingState& F = gFish;
    if (!F.init && gPlaces.ready) {
        F.init = true;
        fishingSetupSpots(g);
    }
    Ped* pl = g.playerPed();
    if (!pl) return;
    F.hintCd -= dt;
    // blips: the good spots and the bait shops (short range; a busy map already has plenty on it)
    if (!gMissions.active) {
        for (const FishSpot& s : F.spots) {
            UI::Blip b;
            b.pos = s.pos.xy();
            b.icon = UI::BLIP_FISH;
            b.shortRange = true;
            b.scale = 0.8f;
            b.label = s.name.c_str();
            g.missionBlips.push_back(b);
        }
        for (const FishMarket& m : F.markets) {
            UI::Blip b;
            b.pos = m.pos.xy();
            b.icon = UI::BLIP_FISH;
            b.color = UI::rgba(1.f, 0.8f, 0.3f);
            b.shortRange = true;
            b.label = m.name.c_str();
            g.missionBlips.push_back(b);
        }
    }
    if (F.phase == FP_OFF) {
        if (!fishingAllowed(g)) return;
        vec2 dir;
        int kind = fishingSpotHere(g, &dir);
        if (kind && F.hintKind != kind && F.hintCd <= 0.f) {
            g.help(kind == 2 ? "Press ~i:G|RS~ to fish from the boat." : "Press ~i:G|RS~ to fish here.", 4.f);
            F.hintKind = kind;
            F.hintCd = 20.f;
        }
        if (!kind) F.hintKind = -1;
        if (kind && g.ctl.special.pressed && pl->state == (kind == 2 ? PS_INVEHICLE : PS_ONFOOT)) {
            F.dir = dir;
            fishingStart(g, kind);
        }
        return;
    }
    // stop: a mission, the police, a cutscene, death, the water, a boat under way, the player's own choice
    bool abort = gMissions.active || g.mInCutscene() || pl->health <= 0.f || g.pinfo.deathTimer > 0.f || g.pinfo.wanted > 0 || g.pinfo.busted;
    if (F.fromBoat) abort = abort || g.playerVehicle() != F.boat || (F.boat >= 0 && g.vehicles[F.boat].sim.speed() > 4.f);
    else abort = abort || pl->state != PS_ONFOOT || pl->vehicle >= 0;
    bool calm = F.phase != FP_FIGHT && F.phase != FP_BITE && F.phase != FP_FLY;
    if (abort || (calm && (F.in.quit || F.in.move))) {
        if (F.phase == FP_FIGHT && !abort) fishingLose(g, "You dropped the rod.");
        fishingStop(g);
        return;
    }
    F.t += dt;
    if (!F.fromBoat) {
        pl->weapon = WPN_FISTS;
        pl->carry = CARRY_ROD;
    }
    vec2 camDir = normalize(g.rig.cam.forward().xy() + vec2(1e-4f, 0.f));
    if (F.test && ::length(F.testDir) > 0.5f) camDir = F.testDir;
    switch (F.phase) {
        case FP_READY:
            F.dip = 0.f;
            if (F.in.castPressed) {
                F.phase = FP_CHARGE;
                F.t = 0.f;
                F.power = 0.f;
                F.powerDir = 1.f;
            }
            break;
        case FP_CHARGE: {
            // the power swings up and back while the button is held (release near the top for a long cast)
            F.power += F.powerDir * dt * 0.85f;
            if (F.power > 1.f) {
                F.power = 1.f;
                F.powerDir = -1.f;
            } else if (F.power < 0.f) {
                F.power = 0.f;
                F.powerDir = 1.f;
            }
            if (!F.fromBoat) pl->yaw = atan2f(-camDir.x, camDir.y);
            if (!F.in.castDown || F.in.castReleased) {
                F.dir = camDir;
                vec3 hand;
                F.origin = F.fromBoat ? vehPos(g, F.boat) : pl->pos.toVec3();
                float want = 6.f + F.power * 24.f;
                bool ok = false;
                for (float d = want; d >= 5.f && !ok; d -= 2.5f) {
                    vec2 q = F.origin.xy() + F.dir * d;
                    if (isWaterAt(g, q, 0.6f) && fishLineClear(g, F.fromBoat ? F.origin + vec3(0.f, 0.f, 0.6f) : F.origin, F.dir, d)) {
                        F.castDist = d;
                        F.target = vec3(q, g.map->waterAt(q.x, q.y));
                        ok = true;
                    }
                }
                if (!ok) {
                    g.help("No open water that way. Turn toward the water and cast again.", 3.f);
                    F.phase = FP_READY;
                    F.t = 0.f;
                    break;
                }
                F.flyFrom = fishRodTip(g, &hand);
                F.bobber = F.flyFrom;
                F.flyT = 0.45f + F.castDist * 0.02f;
                F.phase = FP_FLY;
                F.t = 0.f;
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_WHOOSH, F.flyFrom, 0.5f, 1.3f);
#endif
            }
            break;
        }
        case FP_FLY: {
            float s = Saturate(F.t / F.flyT);
            F.bobber = F.flyFrom + (F.target - F.flyFrom) * s + vec3(0.f, 0.f, sinf(s * kPi) * (1.5f + F.castDist * 0.12f));
            if (s >= 1.f) {
                F.bobber = F.target;
                F.hab = fishHabitatAt(g, F.target.xy(), F.fromBoat);
                float hour = g.env ? g.env->timeOfDay : 12.f;
                float wait = F.rng.range(4.f, 13.f) * (fishLowLight(hour) ? 0.75f : 1.f) * (g.env && g.env->rain > 0.2f ? 0.8f : 1.f);
                if (F.test) wait = Min(wait, 5.f);
                F.biteAt = wait;
                F.nibbleAt = F.rng.f() < 0.65f ? F.rng.range(1.5f, wait - 0.8f) : 1e9f;
                F.phase = FP_WAIT;
                F.t = 0.f;
                spawnFx(FX_WATER_SPLASH, dvec3(F.target), vec3(0.f, 0.f, 1.f), 3, 0.3f);
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_SPLASH_SMALL, F.target, 0.35f, 1.5f);
#endif
                g.help(StrFormat("Fishing %s. Wait for the float to go under, then strike with ~i:LMB|RT~.", kHabitatName[F.hab]), 5.f);
            }
            break;
        }
        case FP_WAIT: {
            // a nibble or two first: striking at a nibble spooks the fish
            float nib = F.t - F.nibbleAt;
            F.dip = nib > 0.f && nib < 0.45f ? sinf(nib / 0.45f * kPi) * 0.5f : 0.f;
            if (nib > 0.45f) F.nibbleAt = F.t + 1.5f < F.biteAt - 0.8f && F.rng.f() < 0.4f ? F.t + F.rng.range(1.2f, 2.5f) : 1e9f;
            if (F.in.castPressed) {
                if (F.dip > 0.05f) {
                    g.help("Too early. That was only a nibble, and you spooked it.", 3.f);
                    F.biteAt = F.t + F.rng.range(5.f, 9.f);
                    F.nibbleAt = 1e9f;
                } else {
                    F.phase = FP_READY;   // reel in the empty line
                    F.t = 0.f;
                }
                break;
            }
            if (F.t >= F.biteAt) {
                F.species = pickFish(g, F.hab, F.rng, F.weight);
                const FishSpecies& fs = kFish[F.species];
                F.window = 0.95f + (1.f - fs.fight) * 0.45f + (F.test ? 0.6f : 0.f);
                F.phase = FP_BITE;
                F.t = 0.f;
                g.rumble(0.5f, 0.4f);
                spawnFx(FX_WATER_SPLASH, dvec3(F.bobber), vec3(0.f, 0.f, 1.f), 4, 0.35f);
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_SPLASH_SMALL, F.bobber, 0.6f, 1.2f);
                Audio::play2D(Audio::SFX_UI_NOTIFY, 0.45f, 1.3f);
#endif
            }
            break;
        }
        case FP_BITE:
            F.dip = 1.f;
            if (F.in.castPressed) {
                const FishSpecies& fs = kFish[F.species];
                F.phase = FP_FIGHT;
                F.t = 0.f;
                F.dist = ::length(F.bobber.xy() - F.origin.xy());
                F.maxDist = F.dist + 45.f;
                F.tension = 0.35f;
                F.stamina = 1.f;
                F.running = true;
                F.runT = F.rng.range(0.8f, 1.6f) * (0.6f + 0.6f * fs.fight);
                F.runSide = F.rng.f() < 0.5f ? -1.f : 1.f;
                F.slackT = 0.f;
                F.sway = 0.f;
                F.splashT = 0.f;
                g.rumble(0.7f, 0.7f);
                g.help("Hold ~i:LMB|RT~ to reel in. Ease off when the line goes red, keep it from going slack.", 6.f);
            } else if (F.t > F.window) {
                F.phase = FP_LOST;
                F.t = 0.f;
                F.cardSpecies = -1;
                F.cardTitle = "MISSED IT";
                F.cardText = "It took the bait and left. Strike as soon as the float goes under.";
                F.lost++;
            }
            break;
        case FP_FIGHT: {
            const FishSpecies& fs = kFish[F.species];
            float size = Saturate((F.weight - fs.minLb) / Max(fs.maxLb - fs.minLb, 0.1f));
            float str = fs.fight * (0.6f + 0.4f * size) * (0.4f + 0.6f * F.stamina);
            F.runT -= dt;
            if (F.runT <= 0.f) {
                F.running = !F.running;
                F.runT = F.running ? F.rng.range(0.8f, 2.2f) * (0.6f + 0.6f * fs.fight) * (0.5f + 0.5f * F.stamina) : F.rng.range(1.2f, 2.8f);
                if (F.running) F.runSide = F.rng.f() < 0.5f ? -1.f : 1.f;
            }
            F.pull = F.running ? str * (0.85f + 0.3f * sinf(F.t * 5.3f)) : str * 0.18f;
            bool reel = F.in.castDown;
            float target = reel ? 0.35f + 0.75f * F.pull : F.pull * 0.3f;
            float rate = reel ? 1.5f : 1.2f;
            F.tension += (target - F.tension) * Saturate(rate * dt);
            float reelSpeed = 3.4f - 1.2f * size;
            if (reel) F.dist -= reelSpeed * (1.f - 0.7f * Saturate(F.pull)) * dt;
            else F.dist += F.pull * 5.f * dt * (F.running ? 1.f : 0.2f);
            F.stamina = Max(0.f, F.stamina - dt * (reel ? 0.055f : 0.028f) * (F.running ? 1.5f : 1.f) * (1.25f - 0.5f * fs.fight));
            F.sway += dt * (F.running ? 1.8f : 0.6f) * F.runSide;
            F.slackT = F.tension < 0.08f ? F.slackT + dt : 0.f;
            vec2 side(-F.dir.y, F.dir.x);
            vec2 at = F.origin.xy() + F.dir * Max(F.dist, 1.f) + side * sinf(F.sway) * Min(F.dist * 0.25f, 6.f);
            float wz = g.map->waterAt(at.x, at.y);
            if (wz > World::kNoWater + 1.f) F.bobber = vec3(at, wz);
            F.splashT -= dt;
            if (F.running && F.splashT <= 0.f) {
                F.splashT = F.rng.range(0.35f, 0.9f);
                spawnFx(FX_WATER_SPLASH, dvec3(F.bobber), vec3(0.f, 0.f, 1.f), 3 + (int)(4.f * str), 0.3f + 0.5f * str);
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_SPLASH_SMALL, F.bobber, 0.25f + 0.4f * str, F.rng.range(0.8f, 1.2f));
#endif
            }
            if (reel) g.rumble(0.05f + 0.25f * F.tension, 0.1f * F.pull);
            if (F.tension >= 1.f) {
                fishingLose(g, "The line snapped. Ease off the reel when the line goes red.");
            } else if (F.slackT > 3.f) {
                fishingLose(g, "Slack line: it threw the hook. Keep reeling between its runs.");
            } else if (F.dist > F.maxDist) {
                fishingLose(g, "It ran out all the line. Reel in whenever it stops pulling.");
            } else if (F.dist <= 2.f) {
                fishingLand(g);
                F.phase = FP_LANDED;
                F.t = 0.f;
            }
            break;
        }
        case FP_LANDED:
        case FP_LOST:
            F.dip = 0.f;
            if (F.t > (F.test ? 1.5f : 4.5f) || (F.t > 0.8f && F.in.castPressed)) {
                F.phase = FP_READY;
                F.t = 0.f;
            }
            break;
        default: break;
    }
    // on foot, the camera settles behind the angler looking down the line (unless the player is looking around)
    if (!F.fromBoat && (F.phase == FP_WAIT || F.phase == FP_BITE || F.phase == FP_FIGHT) && g.rig.noInputTime > 1.2f) {
        vec2 to = F.bobber.xy() - pl->pos.toVec3().xy();
        if (::length(to) > 2.f) g.rig.yaw += wrapAngle(atan2f(-to.x, to.y) - g.rig.yaw) * Saturate(dt * 1.5f);
    }
    fishingSubmit(g);
}

// ------------------------------------------------------------------------------------------------------------------
// Palmera Angler bait shops: sell the cooler, read the trophy board
long long coolerValue() {
    long long v = 0;
    for (const auto& f : gFish.cooler) v += fishValue(f.first, f.second);
    return v;
}

void fishMenuOpen(GameWorld& g, int page) {
    FishingState& F = gFish;
    F.menuPage = page;
    std::vector<MenuItem> items;
    if (page == 0) {
        MenuItem sell;
        sell.label = F.cooler.empty() ? "Sell the catch (cooler empty)" : StrFormat("Sell the catch (%d fish)", (int)F.cooler.size());
        sell.price = F.cooler.empty() ? -1 : coolerValue();
        sell.enabled = !F.cooler.empty();
        sell.id = 1;
        std::string d;
        for (const auto& f : F.cooler) d += StrFormat("%s%s %.1f lb", d.empty() ? "" : ", ", kFish[f.first].name, f.second);
        sell.detail = d.empty() ? std::string("Land something first. We pay by the pound, half as much again for trophy fish.") : d;
        items.push_back(sell);
        MenuItem board;
        board.label = "Trophy board";
        board.right = StrFormat("%d / %d", fishSpeciesLanded(g), kFishCount);
        board.detail = "Best weight for every species you have landed, and what's still missing.";
        board.id = 2;
        items.push_back(board);
        MenuItem bye;
        bye.label = "Leave";
        bye.id = 3;
        items.push_back(bye);
        menuOpen(g, MO_FISH, "PALMERA ANGLER", "Bait & Tackle", items, true, 0xff2aa6e8u);
        return;
    }
    for (int s = 0; s < kFishCount; s++) {
        MenuItem it;
        bool got = fishLanded(g, s);
        float best = fishBest(g, s);
        it.label = got ? kFish[s].name : "? ? ?";
        it.right = got ? StrFormat("%.1f lb%s", best, best >= kFish[s].trophyLb ? "  TROPHY" : "") : std::string("");
        it.checked = false;
        it.detail = got ? StrFormat("%s Trophy class from %.0f lb.", kFish[s].tip, kFish[s].trophyLb) : std::string(kFish[s].tip);
        it.id = 100 + s;
        items.push_back(it);
    }
    menuOpen(g, MO_FISH, "TROPHY BOARD", StrFormat("%d fish landed, %d of %d species", flag(g, EX_FISH_CAUGHT), fishSpeciesLanded(g), kFishCount), items, true,
             0xff2aa6e8u);
}

long long fishSell(GameWorld& g) {
    FishingState& F = gFish;
    long long v = coolerValue();
    if (v <= 0) return 0;
    int n = (int)F.cooler.size();
    F.cooler.clear();
    money(g, v);
    setFlag(g, EX_FISH_EARNED, flag(g, EX_FISH_EARNED) + (int)v);
    g.notify("PALMERA ANGLER", StrFormat("Sold %d fish for $%lld.", n, v));
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_CASH_REGISTER, 0.7f);
#endif
    LOG("fishing: sold %d fish for $%lld", n, v);
    return v;
}

void fishMarketsUpdate(GameWorld& g) {
    FishingState& F = gFish;
    Ped* pl = g.playerPed();
    if (!pl || F.markets.empty()) return;
    vec3 pp = pl->pos.toVec3();
    int inside = -1;
    for (int i = 0; i < (int)F.markets.size(); i++) {
        const FishMarket& m = F.markets[i];
        float d = ::length(m.pos - pp);
        if (d < 120.f && d > 1.6f && !gMissions.active && !menuIs(MO_FISH)) worldMarker(m.pos, 0.8f, vec3(0.3f, 0.75f, 1.f));
        if (d < 1.4f && pl->state == PS_ONFOOT && F.phase == FP_OFF) inside = i;
    }
    if (inside >= 0 && F.marketInside != inside && fishingAllowed(g)) fishMenuOpen(g, 0);
    F.marketInside = inside;
    if (!menuIs(MO_FISH)) return;
    if (inside < 0 && !F.test) {
        menuClose(g);
        return;
    }
    if (gMenu.cancelled) {
        if (F.menuPage == 1) fishMenuOpen(g, 0);
        else menuClose(g);
        return;
    }
    if (gMenu.chosen == 1) {
        fishSell(g);
        fishMenuOpen(g, 0);
    } else if (gMenu.chosen == 2) {
        fishMenuOpen(g, 1);
    } else if (gMenu.chosen == 3) {
        menuClose(g);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// HUD: the cast power, the float, the fight (tension with its zones, the line out, the fish's strength), the catch card
void fishingDraw(GameWorld& g, float W, float H) {
    FishingState& F = gFish;
    if (F.phase == FP_OFF || g.mInCutscene()) return;
    float u = H / 1080.f;
    float cx = W * 0.5f, by = H - 250.f * u;
    UI::TextStyle ts;
    ts.font = UI::FONT_HEADING;
    ts.size = 22.f * u;
    ts.align = UI::ALIGN_CENTER;
    ts.shadow = 2.f * u;
    ts.color = 0xffffffffu;
    float pw = 460.f * u;
    // the cooler, top right of the panel
    UI::TextStyle cs = ts;
    cs.font = UI::FONT_BODY;
    cs.size = 17.f * u;
    cs.align = UI::ALIGN_RIGHT;
    cs.color = UI::rgba(1.f, 1.f, 1.f, 0.8f);
    UI::text(cx + pw * 0.5f, by - 58.f * u, StrFormat("COOLER %d/%d  $%lld", (int)F.cooler.size(), kCoolerMax, coolerValue()).c_str(), cs);
    switch (F.phase) {
        case FP_READY:
            UI::roundRect(cx - pw * 0.5f, by - 30.f * u, pw, 44.f * u, 8.f * u, UI::rgba(0.f, 0.f, 0.f, 0.55f));
            UI::text(cx, by - 20.f * u, "HOLD TO CAST", ts);
            break;
        case FP_CHARGE: {
            UI::roundRect(cx - pw * 0.5f, by - 30.f * u, pw, 44.f * u, 8.f * u, UI::rgba(0.f, 0.f, 0.f, 0.6f));
            UI::gradientRectH(cx - pw * 0.5f + 6.f * u, by - 24.f * u, (pw - 12.f * u) * F.power, 32.f * u, UI::rgba(0.2f, 0.7f, 1.f, 0.9f),
                              UI::rgba(0.4f, 1.f, 0.6f, 0.95f));
            UI::text(cx, by - 20.f * u, StrFormat("CAST  %d m", (int)(6.f + F.power * 24.f)).c_str(), ts);
            break;
        }
        case FP_FLY:
        case FP_WAIT: {
            UI::roundRect(cx - pw * 0.5f, by - 30.f * u, pw, 44.f * u, 8.f * u, UI::rgba(0.f, 0.f, 0.f, 0.5f));
            float b = F.dip;
            UI::circle(cx - pw * 0.5f + 26.f * u, by - 8.f * u + b * 8.f * u, 9.f * u, UI::rgba(0.9f, 0.15f, 0.1f, 1.f));
            UI::text(cx, by - 20.f * u, F.phase == FP_FLY ? "..." : (b > 0.05f ? "A NIBBLE... WAIT" : "WAITING FOR A BITE"), ts);
            break;
        }
        case FP_BITE: {
            float pulse = 0.75f + 0.25f * sinf(F.t * 22.f);
            UI::roundRect(cx - pw * 0.5f, by - 36.f * u, pw, 56.f * u, 10.f * u, UI::rgba(0.9f, 0.5f, 0.05f, 0.85f * pulse));
            UI::TextStyle bs = ts;
            bs.font = UI::FONT_TITLE;
            bs.size = 40.f * u;
            UI::text(cx, by - 32.f * u, "STRIKE!", bs);
            break;
        }
        case FP_FIGHT: {
            float h = 118.f * u, x0 = cx - pw * 0.5f, y0 = by - 84.f * u;
            UI::roundRect(x0, y0, pw, h, 10.f * u, UI::rgba(0.f, 0.f, 0.f, 0.62f));
            // tension: slack (grey), good (green), careful (amber), snapping (red)
            float bx = x0 + 16.f * u, bw = pw - 32.f * u, bh = 20.f * u, bty = y0 + 34.f * u;
            UI::rect(bx, bty, bw * 0.08f, bh, UI::rgba(0.45f, 0.45f, 0.45f, 0.8f));
            UI::rect(bx + bw * 0.08f, bty, bw * 0.72f, bh, UI::rgba(0.15f, 0.65f, 0.3f, 0.85f));
            UI::rect(bx + bw * 0.8f, bty, bw * 0.12f, bh, UI::rgba(0.95f, 0.65f, 0.1f, 0.9f));
            UI::rect(bx + bw * 0.92f, bty, bw * 0.08f, bh, UI::rgba(0.9f, 0.12f, 0.1f, 0.95f));
            float tx = bx + bw * Saturate(F.tension);
            UI::rect(tx - 3.f * u, bty - 6.f * u, 6.f * u, bh + 12.f * u, 0xffffffffu);
            UI::TextStyle ls = ts;
            ls.font = UI::FONT_BODY;
            ls.size = 16.f * u;
            ls.align = UI::ALIGN_LEFT;
            UI::text(bx, y0 + 10.f * u, "LINE TENSION", ls);
            ls.align = UI::ALIGN_RIGHT;
            bool warn = F.tension > 0.85f, slack = F.slackT > 0.8f;
            if (warn || slack) {
                ls.color = warn ? UI::rgba(1.f, 0.35f, 0.25f, 0.6f + 0.4f * sinf(F.t * 16.f)) : UI::rgba(0.8f, 0.85f, 1.f, 0.6f + 0.4f * sinf(F.t * 10.f));
                UI::text(bx + bw, y0 + 10.f * u, warn ? "EASE OFF" : "REEL IN", ls);
            }
            // the line out: how far the fish is, and how much line is left before it runs it out
            float ly = bty + bh + 16.f * u;
            float frac = Saturate(F.dist / Max(F.maxDist, 1.f));
            UI::rect(bx, ly + 8.f * u, bw, 3.f * u, UI::rgba(1.f, 1.f, 1.f, 0.35f));
            UI::circle(bx + bw * frac, ly + 9.5f * u, 7.f * u, UI::rgba(0.4f, 0.85f, 1.f, 1.f));
            ls.color = UI::rgba(1.f, 1.f, 1.f, 0.85f);
            ls.align = UI::ALIGN_LEFT;
            UI::text(bx, ly + 18.f * u, StrFormat("%d m out", (int)F.dist).c_str(), ls);
            ls.align = UI::ALIGN_RIGHT;
            const FishSpecies& fs = kFish[F.species];
            const char* feel = F.weight >= fs.trophyLb * 0.8f || F.weight > 40.f ? "Something BIG" : (F.stamina < 0.3f ? "It's tiring" : "Fish on");
            UI::text(bx + bw, ly + 18.f * u, feel, ls);
            break;
        }
        case FP_LANDED:
        case FP_LOST: {
            float a = Saturate(F.t * 4.f);
            float ch = 128.f * u, x0 = cx - pw * 0.5f, y0 = by - 100.f * u;
            UI::roundRect(x0, y0, pw, ch, 12.f * u, UI::rgba(0.02f, 0.05f, 0.08f, 0.85f * a));
            UI::rect(x0, y0, 5.f * u, ch, F.phase == FP_LANDED ? (F.cardTrophy ? UI::rgba(1.f, 0.8f, 0.2f, a) : UI::rgba(0.2f, 0.7f, 1.f, a))
                                                               : UI::rgba(0.9f, 0.3f, 0.2f, a));
            UI::TextStyle t1 = ts;
            t1.font = UI::FONT_TITLE;
            t1.size = 34.f * u;
            t1.align = UI::ALIGN_LEFT;
            t1.color = UI::withAlpha(0xffffffffu, a);
            UI::text(x0 + 20.f * u, y0 + 10.f * u, F.cardTitle.c_str(), t1);
            UI::TextStyle t2 = t1;
            t2.font = UI::FONT_HEADING;
            t2.size = 20.f * u;
            if (F.phase == FP_LANDED) {
                t2.align = UI::ALIGN_RIGHT;
                t2.color = UI::withAlpha(F.cardTrophy ? UI::rgba(1.f, 0.82f, 0.25f) : 0xffffffffu, a);
                std::string tag = StrFormat("%.1f lb", F.cardWeight);
                if (F.cardTrophy) tag += "  TROPHY";
                if (F.cardNew) tag += "  NEW";
                else if (F.cardRecord) tag += "  RECORD";
                UI::text(x0 + pw - 18.f * u, y0 + 20.f * u, tag.c_str(), t2);
            }
            UI::TextStyle t3 = t2;
            t3.font = UI::FONT_BODY;
            t3.size = 17.f * u;
            t3.align = UI::ALIGN_LEFT;
            t3.color = UI::rgba(1.f, 1.f, 1.f, 0.85f * a);
            UI::textWrapped(x0 + 20.f * u, y0 + 58.f * u, pw - 40.f * u, F.cardText.c_str(), t3);
            break;
        }
        default: break;
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Pause-menu statistics
std::vector<std::pair<std::string, std::string>> fishingStats(GameWorld& g) {
    std::vector<std::pair<std::string, std::string>> out;
    int caught = flag(g, EX_FISH_CAUGHT);
    out.push_back({"Fish caught", StrFormat("%d", caught)});
    out.push_back({"Trophy board species", StrFormat("%d / %d", fishSpeciesLanded(g), kFishCount)});
    int best = -1;
    float bw = 0.f;
    for (int s = 0; s < kFishCount; s++)
        if (fishLanded(g, s) && fishBest(g, s) > bw) {
            bw = fishBest(g, s);
            best = s;
        }
    if (best >= 0) out.push_back({"Heaviest fish", StrFormat("%s, %.1f lb", kFish[best].name, bw)});
    out.push_back({"Fish sold", StrFormat("$%d", flag(g, EX_FISH_EARNED))});
    return out;
}

// ------------------------------------------------------------------------------------------------------------------
// Test hooks (--missiontest fish_pier, fish_boat, fish_swamp, fish_river, fish_lake, fish_market)
struct FishTest {
    int kind = 0;           // 0 pier (coast), 1 boat offshore, 2 swamp, 3 river, 4 lake, 5 market
    u8 wantHab = HAB_COAST;
    int stage = 0;
    float t = 0.f;
    int landed0 = 0;
    long long money0 = 0;
    int result = 0;         // 0 running, 1 passed, -1 failed
    std::string text;
    bool reeling = false;
};
FishTest gFishTest;

// Where each fishing test stands: the water's edge facing the water (or a boat on open water), and the time of day
bool fishTestSpot(GameWorld& g, int kind, vec3& pos, float& yaw, bool& boat, vec3& water) {
    const Places& P = gPlaces;
    boat = kind == 1;
    auto edge = [&](vec3 from, vec3 waterHint) {
        // walk from the land point toward the water until the next step is water: stand there, face it
        vec2 d = normalize(waterHint.xy() - from.xy() + vec2(1e-3f, 0.f));
        vec2 p = from.xy();
        for (int i = 0; i < 120; i++) {
            vec2 q = p + d * 1.5f;
            if (isWaterAt(g, q + d * 2.f, 0.6f)) break;
            p = q;
        }
        pos = vec3(p, groundAt(g, p.x, p.y, from.z + 4.f));
        yaw = atan2f(-d.x, d.y);
        water = vec3(p + d * 8.f, 0.f);
    };
    // stand at a spot over the water (a pier's end): face the direction with the most open water and nothing in the way
    auto overWater = [&](vec3 e) {
        vec3 st(e.xy(), groundAt(g, e.x, e.y, e.z + 3.f));
        vec2 best(0.f, 1.f);
        int bestN = -1;
        for (int k = 0; k < 24; k++) {
            float a = kTwoPi * k / 24.f;
            vec2 d(cosf(a), sinf(a));
            if (!fishLineClear(g, st, d, 12.f)) continue;
            int n = 0;
            for (float r = 4.f; r <= 20.f; r += 4.f) n += isWaterAt(g, e.xy() + d * r, 0.8f);
            if (n > bestN) {
                bestN = n;
                best = d;
            }
        }
        pos = st;
        yaw = atan2f(-best.x, best.y);
        water = vec3(e.xy() + best * 10.f, 0.f);
        return bestN > 2;
    };
    switch (kind) {
        case 0: return overWater(P.pierEnd);   // the end of the Sol Beach Pier
        case 1: {   // deep water off Sol Beach, far from shore
            vec3 w;
            vec2 hint = P.beachSea.xy();
            vec2 out = normalize(hint - P.bayCenter.xy() + vec2(1e-3f, 0.f));
            for (int k = 0; k < 8; k++) {
                vec2 q = hint + out * (500.f + 150.f * k);
                if (findWater(g, q, 8.f, w, 120.f) && g.map->coastDistance(w.x, w.y) < -380.f) {
                    pos = w;
                    yaw = atan2f(-out.x, out.y);
                    water = w;
                    return true;
                }
            }
            return false;
        }
        case 2: edge(P.sawgrassDock, P.sawgrassWater); return true;
        case 3: edge(placeOffset(g, P.boatyard, 0.f, 1.f), P.riverLaunch); return true;
        case 4: {   // the lake's fishing pier, else its shore by Lake Town
            for (const FishSpot& s : gFish.spots)
                if (s.hab == HAB_LAKE && isWaterAt(g, s.pos.xy(), 0.3f) && overWater(s.pos)) return true;
            vec3 stand;
            vec2 d;
            if (!fishLakeShore(g, stand, d)) return false;
            pos = stand;
            yaw = atan2f(-d.x, d.y);
            water = vec3(stand.xy() + d * 8.f, 0.f);
            return true;
        }
        case 5: {   // the Sol Beach Pier shop counter
            if (gFish.markets.empty()) return false;
            pos = gFish.markets[0].pos + vec3(3.f, 0.f, 0.f);
            yaw = 0.f;
            water = pos;
            return true;
        }
    }
    return false;
}

int fishTestKind(const std::string& id) {
    static const char* const kIds[] = {"fish_pier", "fish_boat", "fish_swamp", "fish_river", "fish_lake", "fish_market"};
    for (int i = 0; i < 6; i++)
        if (id == kIds[i]) return i;
    return -1;
}

bool fishTestPrepare(GameWorld& g, int kind, float& hour) {
    FishingState& F = gFish;
    if (!F.init) {
        F.init = true;
        fishingSetupSpots(g);
    }
    static const u8 kHab[6] = {HAB_COAST, HAB_OFFSHORE, HAB_SWAMP, HAB_RIVER, HAB_LAKE, HAB_COAST};
    FishTest& T = gFishTest;
    T = FishTest();
    T.kind = kind;
    T.wantHab = kHab[kind];
    vec3 pos, water;
    float yaw = 0.f;
    bool boat = false;
    if (!fishTestSpot(g, kind, pos, yaw, boat, water)) return false;
    hour = kind == 1 ? 10.f : (kind == 5 ? 21.5f : 17.5f);   // (the bait shop after dark: its marker under the night exposure)
    if (boat) {
        int model = pickModel(g, {Vehicles::VC_BOAT}, 3);
        int v = placePlayer(g, pos, yaw, model, lin(0.9f, 0.9f, 0.86f));
        if (v < 0) return false;
    } else {
        placePlayer(g, pos, yaw);
    }
    if (kind == 5) {
        // the market test sells a cooler landed by the others (or a fresh one)
        if (F.cooler.empty()) {
            F.cooler.push_back({0, 3.4f});
            F.cooler.push_back({3, 8.2f});
        }
    }
    return true;
}

void fishTestStart(GameWorld& g) {
    FishingState& F = gFish;
    FishTest& T = gFishTest;
    F.test = true;
    F.testIn = FishInput();
    F.testDir = vec2(0.f);
    T.stage = 0;
    T.t = 0.f;
    T.landed0 = F.landed;
    T.money0 = g.pinfo.money;
    T.result = 0;
    Ped* pl = g.playerPed();
    if (pl && g.playerVehicle() < 0) F.testDir = dirFromYaw(pl->yaw);
    else if (g.playerVehicle() >= 0) F.testDir = normalize(g.vehicles[g.playerVehicle()].sim.forward().xy() * 0.3f + vec2(1.f, 0.f));
}

bool fishTestRunning() { return gFishTest.result == 0; }
int fishTestResult() { return gFishTest.result; }
std::string fishTestText() { return gFishTest.text; }
int fishTestStage() { return gFishTest.stage; }

void fishTestEnd(GameWorld& g, bool ok, const std::string& text) {
    FishTest& T = gFishTest;
    T.result = ok ? 1 : -1;
    T.text = text;
    fishingStop(g);
    if (menuIs(MO_FISH)) menuClose(g);
    gFish.test = false;
}

// The scripted angler: start, cast long, strike on the bite, reel with the tension kept in the green
void fishTestStep(GameWorld& g, MissionTest& t) {
    FishingState& F = gFish;
    FishTest& T = gFishTest;
    if (T.result != 0) return;
    float dt = g.dtLast;
    T.t += dt;
    if (T.t > 240.f) {
        fishTestEnd(g, false, StrFormat("timeout (phase %d, landed %d, lost %d)", F.phase, F.landed - T.landed0, F.lost));
        return;
    }
    if (T.kind == 5) {   // the bait shop: walk up (a look at its marker), walk in, sell, read the board, leave
        switch (T.stage) {
            case 0:
                if (T.t > 0.6f) {
                    // a few steps out from the counter with a clear line to it, facing it
                    vec3 mk = F.markets[0].pos, at = lookoutSpot(g, mk, 7.f, playerPos(g));
                    t.teleport(at, atan2f(-(mk.x - at.x), mk.y - at.y));
                    T.stage = 3;
                    T.t = 0.f;
                }
                break;
            case 3:
                if (T.t > 3.f) {
                    t.screenshot("approach");
                    T.stage = 4;
                    T.t = 0.f;
                }
                break;
            case 4:
                if (T.t > 1.5f) {
                    t.teleport(F.markets[0].pos, 0.f);
                    T.stage = 1;
                    T.t = 0.f;
                }
                break;
            case 1:
                if (menuIs(MO_FISH) && F.menuPage == 0 && T.t > 0.6f) {
                    t.screenshot("market");
                    gMenu.chosen = 1;
                    fishSell(g);
                    fishMenuOpen(g, 1);
                    T.stage = 2;
                    T.t = 0.f;
                } else if (T.t > 6.f) {
                    fishTestEnd(g, false, "the shop menu did not open");
                }
                break;
            case 2:
                if (T.t > 1.2f) {
                    t.screenshot("board");
                    menuClose(g);
                    bool paid = g.pinfo.money > T.money0 && F.cooler.empty();
                    fishTestEnd(g, paid, paid ? StrFormat("sold for $%lld", g.pinfo.money - T.money0) : std::string("nothing was paid"));
                }
                break;
        }
        return;
    }
    FishInput& in = F.testIn;
    switch (T.stage) {
        case 0:   // pick up the rod (on foot: turn toward open water first; walls stream in after the test placed the player)
            if (T.t > 1.f) {
                int kind = fishingSpotHere(g);
                Ped* me = g.playerPed();
                if (!kind && me && g.playerVehicle() < 0) {
                    float y0 = me->yaw;
                    for (int k = 1; k < 24 && !kind; k++) {
                        me->yaw = y0 + kTwoPi * k / 24.f;
                        kind = fishingSpotHere(g);
                    }
                    if (!kind) me->yaw = y0;
                    else F.testDir = dirFromYaw(me->yaw);
                }
                if (!kind) {
                    Ped* pl = g.playerPed();
                    if (T.t > 6.f) fishTestEnd(g, false, StrFormat("no fishing spot here (player at %.0f %.0f)", pl ? pl->pos.x : 0.0, pl ? pl->pos.y : 0.0));
                    break;
                }
                fishingStart(g, kind);
                t.log("fishing from %s", kind == 2 ? "a boat" : "the shore");
                T.stage = 1;
                T.t = 0.f;
            }
            break;
        case 1:   // cast: hold for most of the power swing
            if (F.phase == FP_READY && T.t > 0.5f) {
                in.castPressed = true;
                in.castDown = true;
            } else if (F.phase == FP_CHARGE) {
                in.castDown = F.t < 0.95f;
                if (!in.castDown) in.castReleased = true;
            } else if (F.phase == FP_WAIT) {
                t.log("cast %.0f m into %s", F.castDist, kHabitatName[F.hab]);
                t.screenshot("cast");
                T.stage = 2;
                T.t = 0.f;
            }
            break;
        case 2:   // the bite: strike on the dip (not on a nibble)
            in.castDown = false;
            if (F.phase == FP_BITE && F.t > 0.15f) {
                in.castPressed = true;
                in.castDown = true;
                T.stage = 3;
                T.t = 0.f;
                T.reeling = true;
            } else if (F.phase == FP_READY || F.phase == FP_LOST) {
                T.stage = 1;
                T.t = 0.f;
            }
            break;
        case 3:   // the fight: reel while the tension is under the amber, ease off above it
            if (F.phase == FP_FIGHT) {
                if (T.reeling && F.tension > 0.84f) T.reeling = false;
                else if (!T.reeling && F.tension < 0.62f) T.reeling = true;
                in.castDown = T.reeling;
                if (T.t > 1.5f && T.t - dt <= 1.5f) t.screenshot("fight");
            } else if (F.phase == FP_LANDED) {
                t.screenshot("landed");
                bool habOk = F.lastHab == T.wantHab;
                t.log("landed %s %.1f lb in %s", kFish[F.lastSpecies].name, F.cardWeight, kHabitatName[F.lastHab]);
                if (!habOk) {
                    fishTestEnd(g, false, StrFormat("caught in %s, expected %s", kHabitatName[F.lastHab], kHabitatName[T.wantHab]));
                } else if (!(kFish[F.lastSpecies].habitats & habBit(T.wantHab))) {
                    fishTestEnd(g, false, StrFormat("%s does not live in %s", kFish[F.lastSpecies].name, kHabitatName[T.wantHab]));
                } else {
                    T.stage = 4;
                    T.t = 0.f;
                }
            } else if (F.phase == FP_LOST || F.phase == FP_READY) {
                t.log("lost one (%s), casting again", F.cardText.c_str());
                in.castDown = false;
                if (F.lost > 4) fishTestEnd(g, false, "lost five fish in a row");
                T.stage = 1;
                T.t = 0.f;
            }
            break;
        case 4:   // put the rod away
            in.castDown = false;
            if (T.t > 1.f) {
                in.quit = true;
                if (F.phase == FP_OFF || T.t > 2.5f) {
                    fishTestEnd(g, F.landed > T.landed0, StrFormat("landed %s (%.1f lb) in %s", kFish[F.lastSpecies].name, F.cardWeight, kHabitatName[F.lastHab]));
                }
            }
            break;
    }
}

}  // namespace mu
}  // namespace Game
