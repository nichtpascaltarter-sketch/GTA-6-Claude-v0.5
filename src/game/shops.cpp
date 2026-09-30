// Shops and properties: gun stores (weapons, ammo, armor), respray shops (repaint, repair, lose the wanted level when
// unseen), the Threads clothing store and safehouse wardrobes (protagonist outfits), the Palm Motors dealership,
// safehouses (save + rest, wardrobe, garage with owned vehicles) and businesses that pay a daily income.
#include "missions.h"

namespace Game {
namespace mu {

enum ShopKind : int { SHOP_GUNS = 0, SHOP_RESPRAY, SHOP_CLOTHES, SHOP_CARS };

struct ShopSite {
    int kind;
    const char* name;
    Place place;
    vec3 marker;
    int requiresFlag;
    bool inside = false;
};

struct Safehouse {
    const char* name;
    Place place;
    long long price;
    int requiresFlag;
    int owner;               // -1 both protagonists, 0 Mari, 1 Dex (starting homes)
    vec3 save, wardrobe, garage;
    float garageYaw = 0.f;
    int garageVehicle = -1;
    u32 garageUid = 0;
    bool inSave = false, inWardrobe = false, inGarage = false, inBuy = false;
};

struct Business {
    const char* name;
    const char* desc;
    Place place;
    vec3 marker;
    long long price;
    int income;              // per game day
    int requiresFlag;
    UI::BlipIcon icon;
    bool inside = false;
};

struct ShopsState {
    bool init = false;
    std::vector<ShopSite> shops;
    std::vector<Safehouse> safehouses;
    std::vector<Business> businesses;
    int activeShop = -1, activeSafehouse = -1, activeBusiness = -1;
    int resprayStage = 0;    // Tide Customs visit: 0 idle, 1 fading into the garage, 2 menu open, 3 fading out
    int resprayVeh = -1;
    int saveFrames = -1;
    // Tide Customs
    int garageShop = -1;
    int modPage = 0;         // ModPage
    int modCursor = -1;      // cursor last previewed
    Vehicle::Mods savedMods; // the car as it was when the category opened (restored on Back)
    vec3 savedColor0, savedColor1;
    float camAngle = 0.f;
    bool clearedWanted = false;
};
ShopsState gShops;

bool businessOwned(GameWorld& g, int i) { return flag(g, EX_BUSINESS + i) != 0; }
bool safehouseOwned(GameWorld& g, int i) {
    const Safehouse& s = gShops.safehouses[i];
    return s.price == 0 || flag(g, EX_SAFEHOUSE + i) != 0;
}

void initShops(GameWorld& g) {
    const Places& P = gPlaces;
    auto shop = [&](int kind, const char* name, const Place& pl, int req) {
        ShopSite s;
        s.kind = kind;
        s.name = name;
        s.place = pl;
        s.marker = kind == SHOP_RESPRAY ? pl.curb : pl.pos;
        // walk-in shops with an interior: the counter inside
        vec3 counter;
        if (kind != SHOP_RESPRAY && World::interiorMarkerWorld(name, World::IM_COUNTER, counter)) s.marker = counter;
        s.requiresFlag = req;
        gShops.shops.push_back(s);
    };
    shop(SHOP_GUNS, "Palmetto Arms", P.gunFlats, -1);
    shop(SHOP_GUNS, "Northside Arms", P.gunNorth, SF_LOW_TIDE);
    shop(SHOP_RESPRAY, "Tide Customs Calle Luna", P.resprayCL, -1);
    shop(SHOP_RESPRAY, "Tide Customs Sol Beach", P.resprayBeach, SF_LOW_TIDE);
    shop(SHOP_CLOTHES, "Threads", P.threads, -1);
    shop(SHOP_CARS, "Palm Motors", P.palmMotors, -1);
    auto home = [&](const char* name, const Place& pl, long long price, int req, int owner) {
        Safehouse s;
        s.name = name;
        s.place = pl;
        s.price = price;
        s.requiresFlag = req;
        s.owner = owner;
        s.save = pl.pos;
        s.wardrobe = placeOffset(g, pl, 3.5f, 0.f);
        // safehouses with an interior: sleep/save at the bed, change at the wardrobe
        vec3 in;
        if (World::interiorMarkerWorld(name, World::IM_BED, in)) s.save = in;
        if (World::interiorMarkerWorld(name, World::IM_WARDROBE, in)) s.wardrobe = in;
        s.garage = curbOffset(g, pl, -9.f);
        s.garageYaw = pl.curbYaw;
        gShops.safehouses.push_back(s);
    };
    home("Mari's Apartment", P.mariApt, 0, -1, 0);
    home("Dex's Trailer", P.dexTrailer, 0, -1, 1);
    home("Sol Beach Condo", P.beachCondo, 85000, SF_VELVET_ROPE, -1);
    home("Key Coral Villa", P.keyCoralMarina, 350000, SF_PAPER_TRAIL, -1);
    home("Downtown Penthouse", P.downtownPenthouse, 500000, SF_SOLARIS_ONE, -1);
    auto biz = [&](const char* name, const char* desc, const Place& pl, long long price, int income, int req, UI::BlipIcon icon) {
        Business b;
        b.name = name;
        b.desc = desc;
        b.place = pl;
        b.marker = placeOffset(g, pl, 5.f, 0.f);
        b.price = price;
        b.income = income;
        b.requiresFlag = req;
        b.icon = icon;
        gShops.businesses.push_back(b);
    };
    biz("Ortega Boatyard", "Buy out Coastline Savings' loan on the family yard. Repairs and storage for the river crowd.", P.boatyard, 40000, 900,
        SF_LAST_CALL, UI::BLIP_BOAT);
    biz("Sunwash Car Wash", "A hand car wash on Sunrise Boulevard. Rook swears it's legitimate.", P.carwash, 25000, 600, SF_REPO_MAN, UI::BLIP_GARAGE);
    biz("Club Riptide", "Sol Beach's hottest open-air club. Velvet ropes, cabanas and a very large bouncer.", P.clubRiptide, 150000, 2800, SF_VELVET_ROPE,
        UI::BLIP_BAR);
    biz("Pike's Airboat Tours", "Jonah's airboat tours through the Sawgrass. Gators included.", resolvePlace(g, P.sawgrassDock.xy(), 0.f, true), 60000, 1200,
        SF_SAWGRASS_RUN, UI::BLIP_BOAT);
    biz("Pulse FM Ad Slots", "Sponsor Kit's station. Porto Sol's favorite pirate-hearted radio.", P.pulseFm, 80000, 1600, SF_DEAD_AIR, UI::BLIP_BAR);
    biz("Sol Cabs", "A taxi company with twelve cabs and one honest dispatcher. Needs 50 fares on your record.", P.taxiDepot, 45000, 1000, SIDE_TAXI_ALL,
        UI::BLIP_TAXI_JOB);
    biz("Rio Sol Marina Slips", "Boat slips at the Rio Sol Marina. Everyone needs somewhere to park a yacht.", resolvePlace(g, vec2(2280, 260)), 120000, 2200,
        SF_RIPTIDE, UI::BLIP_BOAT);
    biz("Lucha's Diner Partnership", "Mama Lucha finally lets someone else pay for the new fryer.", P.diner, 20000, 500, SF_SIGNAL, UI::BLIP_BAR);
}

// ------------------------------------------------------------------------------------------------------------------
// Gun store
int weaponUnlockFlag(int w) {
    switch (w) {
        case WPN_RIFLE: return SF_LAST_CALL;
        case WPN_GRENADE: return SF_COLLATERAL;
        case WPN_SNIPER: return SF_HEAVY_LIFT;
        case WPN_RPG: return SF_PAPER_TRAIL;
        case WPN_SHOTGUN: return SF_REPO_MAN;
        default: return -1;
    }
}

std::vector<MenuItem> gunItems(GameWorld& g) {
    std::vector<MenuItem> items;
    Ped* pl = g.playerPed();
    if (!pl) return items;
    for (int w = WPN_KNIFE; w < WPN_COUNT; w++) {
        const WeaponInfo& wi = weaponInfo((WeaponType)w);
        int unlock = weaponUnlockFlag(w);
        bool unlocked = unlock < 0 || storyDone(g, unlock);
        MenuItem it;
        it.id = w;
        if (!pl->hasWeapon[w] || wi.clipSize == 0) {
            it.label = wi.name;
            it.price = wi.price;
            it.checked = pl->hasWeapon[w];
            it.enabled = unlocked && !pl->hasWeapon[w] && g.pinfo.money >= wi.price;
            it.detail = unlocked ? StrFormat("Damage %.0f, range %.0f m%s", wi.damage * wi.pellets, wi.range, wi.clipSize ? StrFormat(", %d round magazine", wi.clipSize).c_str() : "")
                                 : std::string("Not available yet. Come back later in the story.");
        } else {
            it.label = std::string(wi.name) + " ammo";
            int rounds = wi.clipSize * 2;
            it.price = wi.ammoPrice * 2;
            it.right = StrFormat("+%d  $%d", rounds, wi.ammoPrice * 2);
            it.enabled = g.pinfo.money >= it.price && pl->ammo[w] < 9999;
            it.detail = StrFormat("You carry %d rounds.", pl->ammo[w]);
            it.id = 100 + w;
        }
        items.push_back(it);
    }
    MenuItem armor;
    armor.label = "Body Armor";
    armor.price = 500;
    armor.enabled = g.pinfo.money >= 500 && pl->armor < 100.f;
    armor.detail = StrFormat("Soaks up gunfire. Current armor %d%%.", (int)pl->armor);
    armor.id = 200;
    items.push_back(armor);
    return items;
}

void gunPurchase(GameWorld& g, int id) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    if (id == 200) {
        money(g, -500);
        pl->armor = 100.f;
    } else if (id >= 100) {
        int w = id - 100;
        const WeaponInfo& wi = weaponInfo((WeaponType)w);
        money(g, -wi.ammoPrice * 2);
        g.giveWeapon(g.player, (WeaponType)w, wi.clipSize * 2);
    } else {
        const WeaponInfo& wi = weaponInfo((WeaponType)id);
        money(g, -wi.price);
        g.giveWeapon(g.player, (WeaponType)id, Max(wi.clipSize, 1) * 3);
        pl->weapon = (WeaponType)id;
    }
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_PURCHASE, 0.8f);
#endif
}

// ------------------------------------------------------------------------------------------------------------------
// Clothes / wardrobe
std::vector<MenuItem> outfitItems(GameWorld& g, bool store) {
    std::vector<MenuItem> items;
    int who = g.protagonistIndex;
    int cur = currentOutfit(g, who);
    for (int i = 0; i < kOutfitCount; i++) {
        const Outfit& o = kOutfits[who][i];
        bool owned = outfitOwned(g, who, i);
        if (!store && !owned) continue;
        if (store && o.story && !owned) continue;
        MenuItem it;
        it.label = o.name;
        it.detail = o.desc;
        it.id = i;
        if (owned) {
            it.right = i == cur ? "WEARING" : "WEAR";
            it.enabled = i != cur;
        } else {
            it.price = o.price;
            it.enabled = g.pinfo.money >= o.price;
        }
        items.push_back(it);
    }
    return items;
}

// ------------------------------------------------------------------------------------------------------------------
// Vehicles
std::vector<MenuItem> dealershipItems(GameWorld& g) {
    std::vector<std::pair<int, int>> order;
    for (int i = 0; i < (int)g.vassets.size(); i++) {
        const Vehicles::VehicleModel& m = g.vassets[i].spec;
        if (m.price <= 0 || m.cls == Vehicles::VC_POLICE || m.cls == Vehicles::VC_TAXI || m.cls == Vehicles::VC_AMBULANCE || m.cls == Vehicles::VC_FIRETRUCK ||
            m.cls == Vehicles::VC_BUS)
            continue;
        order.push_back({m.price, i});
    }
    std::sort(order.begin(), order.end());
    std::vector<MenuItem> items;
    for (auto& o : order) {
        const Vehicles::VehicleModel& m = g.vassets[o.second].spec;
        MenuItem it;
        it.label = m.maker + " " + m.name;
        it.price = m.price;
        it.id = o.second;
        bool owned = std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), o.second) != g.ownedVehicleModels.end();
        it.checked = owned;
        it.enabled = !owned && g.pinfo.money >= m.price;
        it.detail = StrFormat("Top speed %.0f km/h, %.0f kW. %s", m.topSpeed * 3.6f, m.power,
                              owned ? "Already in your garages." : "Delivered here and kept in every safehouse garage.");
        items.push_back(it);
    }
    return items;
}

void loadOwnedMods(GameWorld& g, int v);

int spawnOwnedVehicle(GameWorld& g, int model, vec3 pos, float yaw) {
    int v = g.spawnVehicle(model, dvec3(pos + vec3(0, 0, 0.35f)), yaw, false);
    if (v >= 0) {
        g.vehicles[v].persistent = true;
        g.vehicles[v].playerUsed = true;
        g.vehicles[v].parked = true;
        g.vehicles[v].sim.engineOn = false;
        // remembered paint (garage slot)
        auto it = std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), model);
        if (it != g.ownedVehicleModels.end()) {
            int slot = (int)(it - g.ownedVehicleModels.begin());
            int packed = slot < 40 ? flag(g, EX_VEHICLE_PAINT + slot) : 0;
            if (packed > 0) {
                vec3 c(((packed >> 11) & 31) / 31.f, ((packed >> 5) & 63) / 63.f, (packed & 31) / 31.f);
                g.vehicles[v].color0 = c;
            }
        }
        loadOwnedMods(g, v);   // Tide Customs parts
    }
    return v;
}

int packPaint(vec3 c) {
    int r = (int)(Saturate(c.x) * 31.f + 0.5f), gg = (int)(Saturate(c.y) * 63.f + 0.5f), b = (int)(Saturate(c.z) * 31.f + 0.5f);
    return Max(1, (r << 11) | (gg << 5) | b);
}

std::vector<MenuItem> garageItems(GameWorld& g) {
    std::vector<MenuItem> items;
    for (size_t i = 0; i < g.ownedVehicleModels.size(); i++) {
        int m = g.ownedVehicleModels[i];
        if (m < 0 || m >= (int)g.vassets.size()) continue;
        MenuItem it;
        it.label = g.vassets[m].spec.maker + " " + g.vassets[m].spec.name;
        it.id = m;
        it.right = "TAKE OUT";
        it.detail = "Parks it at the curb outside.";
        items.push_back(it);
    }
    if (items.empty()) {
        MenuItem it;
        it.label = "No vehicles yet";
        it.detail = "Buy vehicles at Palm Motors in the Canvas District or on the phone (Wheels.ps).";
        it.enabled = false;
        items.push_back(it);
    }
    return items;
}

// ------------------------------------------------------------------------------------------------------------------
// Tide Customs (the two body shops): drive in and stop to open the garage. Entering unseen loses the police; inside,
// the car can be repaired, resprayed, repainted (primary / secondary colour, finish), tuned (engine, brakes,
// transmission, suspension, turbo), armored, tinted, lit with neon and given coloured tire smoke. Paint, finish,
// tint, neon and smoke preview live on the car while browsing and are restored on Back. Parts on owned vehicles are
// saved per garage slot (EX_VEHICLE_MODS) and come back when the car leaves a garage.
enum ModPage : int {
    MP_MAIN = 0, MP_PRIMARY, MP_SECONDARY, MP_FINISH, MP_ENGINE, MP_BRAKES, MP_TRANSMISSION, MP_SUSPENSION, MP_TURBO, MP_ARMOR, MP_TINT, MP_NEON,
    MP_SMOKE, MP_COUNT
};
enum ModMainItem : int { MI_REPAIR = 100, MI_RESPRAY, MI_LEAVE = 199 };

struct NamedColor {
    const char* name;
    vec3 srgb;
};
const NamedColor kPaints[] = {
    {"Midnight Black", vec3(0.02f, 0.02f, 0.025f)}, {"Pearl White", vec3(0.92f, 0.92f, 0.9f)},  {"Gunmetal", vec3(0.22f, 0.23f, 0.25f)},
    {"Harbor Silver", vec3(0.62f, 0.63f, 0.65f)},   {"Calle Red", vec3(0.62f, 0.04f, 0.06f)},   {"Sunset Orange", vec3(0.9f, 0.35f, 0.05f)},
    {"Cab Yellow", vec3(0.95f, 0.72f, 0.05f)},      {"Key Lime", vec3(0.45f, 0.8f, 0.1f)},      {"Racing Green", vec3(0.03f, 0.25f, 0.12f)},
    {"Tide Teal", vec3(0.05f, 0.55f, 0.55f)},       {"Gulf Blue", vec3(0.05f, 0.2f, 0.6f)},     {"Neon Purple", vec3(0.4f, 0.08f, 0.65f)},
    {"Flamingo Pink", vec3(0.95f, 0.35f, 0.6f)},    {"Dune Sand", vec3(0.76f, 0.66f, 0.48f)},   {"Bronze", vec3(0.45f, 0.28f, 0.12f)},
    {"Cuban Coffee", vec3(0.22f, 0.12f, 0.07f)},
};
const NamedColor kNeons[] = {
    {"None", vec3(0.f)},           {"Moonlight White", vec3(1.f)},        {"Ice Blue", vec3(0.45f, 0.85f, 1.f)}, {"Electric Blue", vec3(0.05f, 0.25f, 1.f)},
    {"Mint", vec3(0.2f, 1.f, 0.6f)}, {"Lime", vec3(0.5f, 1.f, 0.05f)},    {"Hot Pink", vec3(1.f, 0.1f, 0.6f)},   {"Ultraviolet", vec3(0.55f, 0.1f, 1.f)},
    {"Siren Red", vec3(1.f, 0.05f, 0.03f)}, {"Sunset Orange", vec3(1.f, 0.4f, 0.02f)}, {"Gold", vec3(1.f, 0.8f, 0.15f)},
};
const NamedColor kSmokes[] = {
    {"White", vec3(1.f)},        {"Black", vec3(0.05f)},      {"Red", vec3(0.9f, 0.1f, 0.08f)},    {"Orange", vec3(1.f, 0.45f, 0.08f)},
    {"Yellow", vec3(1.f, 0.85f, 0.15f)}, {"Green", vec3(0.2f, 0.85f, 0.25f)}, {"Blue", vec3(0.15f, 0.35f, 1.f)}, {"Purple", vec3(0.55f, 0.15f, 0.9f)},
    {"Pink", vec3(1.f, 0.4f, 0.75f)},
};
const char* const kFinishNames[5] = {"Gloss", "Metallic", "Pearlescent", "Matte", "Chrome"};
const int kFinishPrice[5] = {0, 400, 1200, 800, 6000};
const char* const kTintNames[4] = {"Stock", "Light Smoke", "Dark Smoke", "Limo"};
const int kTintPrice[4] = {0, 250, 500, 900};
const int kEnginePrice[4] = {0, 2500, 6000, 12000}, kBrakePrice[4] = {0, 1200, 3000, 6000}, kTransPrice[4] = {0, 1800, 4500, 9000},
          kSuspPrice[4] = {0, 900, 2200, 4500}, kArmorPrice[6] = {0, 2000, 4500, 8000, 12500, 18000};
const int kTurboPrice = 10000, kNeonPrice = 1500, kSmokePrice = 750, kRespray = 300;

int ownedSlot(GameWorld& g, int model) {
    auto it = std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), model);
    int slot = it == g.ownedVehicleModels.end() ? -1 : (int)(it - g.ownedVehicleModels.begin());
    return slot >= 0 && slot < 40 ? slot : -1;
}

vec3 unpackPaint(int packed) { return vec3(((packed >> 11) & 31) / 31.f, ((packed >> 5) & 63) / 63.f, (packed & 31) / 31.f); }

// Handling parts go through the vehicle simulation's upgrade fitting (idempotent)
void applyModHandling(GameWorld& g, int v) {
    Vehicle& veh = g.vehicles[v];
    Vehicles::VehicleUpgrades u;
    u.engine = veh.mods.engine;
    u.brakes = veh.mods.brakes;
    u.transmission = veh.mods.transmission;
    u.suspension = veh.mods.suspension;
    u.turbo = veh.mods.turbo;
    Vehicles::applyUpgrades(veh.sim, g.vassets[veh.model].spec, u);
}

// Saved parts of an owned vehicle: [color1 RGB565 | finish<<16 | tint<<19 | turbo<<21 | armor<<22 | engine<<25 |
// brakes<<27 | transmission<<29], [neon RGB565 | hasNeon<<16 | suspension<<17 | valid<<19], [smoke RGB565 | valid<<16]
void saveOwnedMods(GameWorld& g, int v) {
    const Vehicle& veh = g.vehicles[v];
    int slot = ownedSlot(g, veh.model);
    if (slot < 0) return;
    const Vehicle::Mods& m = veh.mods;
    int a = packPaint(veh.color1) & 0xffff;
    a |= (m.finish & 7) << 16 | (m.tint & 3) << 19 | (m.turbo ? 1 : 0) << 21 | (m.armor & 7) << 22 | (m.engine & 3) << 25 | (m.brakes & 3) << 27 |
         (m.transmission & 3) << 29;
    bool neon = m.neon.x + m.neon.y + m.neon.z > 0.01f;
    int b = (neon ? packPaint(m.neon) & 0xffff : 0) | (neon ? 1 : 0) << 16 | (m.suspension & 3) << 17 | 1 << 19;
    int c = (packPaint(m.smoke) & 0xffff) | 1 << 16;
    setFlag(g, EX_VEHICLE_PAINT + slot, packPaint(veh.color0));
    setFlag(g, EX_VEHICLE_MODS + slot * 3, a);
    setFlag(g, EX_VEHICLE_MODS + slot * 3 + 1, b);
    setFlag(g, EX_VEHICLE_MODS + slot * 3 + 2, c);
}

void loadOwnedMods(GameWorld& g, int v) {
    Vehicle& veh = g.vehicles[v];
    int slot = ownedSlot(g, veh.model);
    if (slot < 0) return;
    int b = flag(g, EX_VEHICLE_MODS + slot * 3 + 1);
    if (!((b >> 19) & 1)) return;   // never customized
    int a = flag(g, EX_VEHICLE_MODS + slot * 3), c = flag(g, EX_VEHICLE_MODS + slot * 3 + 2);
    Vehicle::Mods& m = veh.mods;
    veh.color1 = unpackPaint(a & 0xffff);
    m.finish = (u8)Min((a >> 16) & 7, 4);
    m.tint = (u8)((a >> 19) & 3);
    m.turbo = ((a >> 21) & 1) != 0;
    m.armor = (u8)Min((a >> 22) & 7, 5);
    m.engine = (u8)((a >> 25) & 3);
    m.brakes = (u8)((a >> 27) & 3);
    m.transmission = (u8)((a >> 29) & 3);
    m.neon = ((b >> 16) & 1) ? unpackPaint(b & 0xffff) : vec3(0.f);
    m.suspension = (u8)((b >> 17) & 3);
    m.smoke = ((c >> 16) & 1) ? unpackPaint(c & 0xffff) : vec3(1.f);
    applyModHandling(g, v);
}

float modPriceScale(GameWorld& g, int v) {
    const Vehicles::VehicleModel& spec = g.vassets[g.vehicles[v].model].spec;
    return Clamp(0.6f + spec.price / 150000.f, 0.6f, 2.5f);
}

long long repairPrice(GameWorld& g, int v) {
    const Vehicle& veh = g.vehicles[v];
    float dmg = (1000.f - Clamp(veh.sim.health, 0.f, 1000.f)) + (1000.f - Clamp(veh.sim.engineHealth, 0.f, 1000.f)) * 0.6f;
    int burst = 0;
    for (int w = 0; w < veh.sim.wheelCount; w++) burst += veh.sim.wheels[w].burst ? 1 : 0;
    if (dmg < 5.f && burst == 0 && !veh.windowsBroken) return 0;
    return 100 + (long long)(dmg * 1.2f) + burst * 150 + (veh.windowsBroken ? 250 : 0);
}

void repairVehicle(Vehicle& veh) {
    Vehicles::repairVehicle(veh.sim);   // bent steering, flooding, wreck flag, dents (vehicle sim)
    veh.sim.health = 1000.f;
    veh.sim.engineHealth = 1000.f;
    for (float& z : veh.sim.damageZones) z = 0.f;
    for (int w = 0; w < veh.sim.wheelCount; w++) veh.sim.wheels[w].burst = false;
    veh.windowsBroken = false;   // new glass
    veh.glassHits = 0;
    veh.fireTimer = 0.f;
    veh.dirt = 0.f;
}

std::string levelName(int level) { return level <= 0 ? std::string("Stock") : StrFormat("Level %d", level); }

std::vector<MenuItem> modItems(GameWorld& g, int v, int page) {
    std::vector<MenuItem> items;
    const Vehicle& veh = g.vehicles[v];
    const Vehicle::Mods& m = veh.mods;
    float f = modPriceScale(g, v);
    auto add = [&](const std::string& label, int id, long long price, bool installed, const std::string& detail) {
        MenuItem it;
        it.label = label;
        it.id = id;
        it.detail = detail;
        if (installed) it.right = "INSTALLED";
        else it.price = price;
        it.enabled = installed || g.pinfo.money >= price;
        items.push_back(it);
    };
    auto cat = [&](const char* label, int page2, const std::string& right, const char* detail) {
        MenuItem it;
        it.label = label;
        it.id = page2;
        it.right = right;
        it.detail = detail;
        items.push_back(it);
    };
    switch (page) {
        case MP_MAIN: {
            long long rp = repairPrice(g, v);
            MenuItem rep;
            rep.label = "Repair";
            rep.id = MI_REPAIR;
            if (rp > 0) rep.price = rp;
            else rep.right = "NO DAMAGE";
            rep.enabled = rp > 0 && g.pinfo.money >= rp;
            rep.detail = "Body, engine, tires and glass back to factory condition.";
            items.push_back(rep);
            MenuItem rs;
            rs.label = "Quick Respray";
            rs.id = MI_RESPRAY;
            rs.price = kRespray;
            rs.enabled = g.pinfo.money >= kRespray;
            rs.detail = "A random factory colour and a full repair. Nobody will recognize this car.";
            items.push_back(rs);
            cat("Primary Colour", MP_PRIMARY, "", "Main body colour.");
            cat("Secondary Colour", MP_SECONDARY, "", "Trim, stripes and accents.");
            cat("Paint Finish", MP_FINISH, kFinishNames[Min((int)m.finish, 4)], "Gloss, metallic, pearlescent, matte or chrome.");
            cat("Engine", MP_ENGINE, levelName(m.engine), "ECU tunes and internals: more power.");
            cat("Brakes", MP_BRAKES, levelName(m.brakes), "Bigger rotors and race pads: stops shorter.");
            cat("Transmission", MP_TRANSMISSION, levelName(m.transmission), "Faster shifts, better acceleration.");
            cat("Suspension", MP_SUSPENSION, levelName(m.suspension), "Lower and stiffer: sharper handling.");
            cat("Turbo", MP_TURBO, m.turbo ? "INSTALLED" : "None", "Forced induction. Hear it spool.");
            cat("Armor", MP_ARMOR, m.armor ? StrFormat("%d%%", m.armor * 15) : std::string("None"), "Plating that shrugs off crashes and gunfire.");
            cat("Window Tint", MP_TINT, kTintNames[Min((int)m.tint, 3)], "Keep your business private.");
            cat("Neon Kit", MP_NEON, m.neon.x + m.neon.y + m.neon.z > 0.01f ? "INSTALLED" : "None", "Underglow for the Sol Beach strip.");
            cat("Tire Smoke", MP_SMOKE, "", "Coloured smoke for burnouts and drifts.");
            MenuItem leave;
            leave.label = "Leave";
            leave.id = MI_LEAVE;
            items.push_back(leave);
            break;
        }
        case MP_PRIMARY:
        case MP_SECONDARY:
            for (int i = 0; i < (int)ARRAY_COUNT(kPaints); i++) {
                vec3 cur = page == MP_PRIMARY ? gShops.savedColor0 : gShops.savedColor1;
                bool inst = length(cur - lin(kPaints[i].srgb.x, kPaints[i].srgb.y, kPaints[i].srgb.z)) < 0.01f;
                add(kPaints[i].name, i, page == MP_PRIMARY ? 500 : 300, inst, "");
            }
            break;
        case MP_FINISH:
            for (int i = 0; i < 5; i++) add(kFinishNames[i], i, kFinishPrice[i], gShops.savedMods.finish == i, "");
            break;
        case MP_ENGINE:
            for (int i = 0; i < 4; i++) add(levelName(i), i, (long long)(kEnginePrice[i] * f), gShops.savedMods.engine == i, i ? StrFormat("+%d%% power", i * 8) : "");
            break;
        case MP_BRAKES:
            for (int i = 0; i < 4; i++) add(levelName(i), i, (long long)(kBrakePrice[i] * f), gShops.savedMods.brakes == i, "");
            break;
        case MP_TRANSMISSION:
            for (int i = 0; i < 4; i++) add(levelName(i), i, (long long)(kTransPrice[i] * f), gShops.savedMods.transmission == i, "");
            break;
        case MP_SUSPENSION:
            for (int i = 0; i < 4; i++) add(i == 0 ? std::string("Stock") : StrFormat("%s", i == 1 ? "Lowered" : (i == 2 ? "Street" : "Competition")), i,
                                            (long long)(kSuspPrice[i] * f), gShops.savedMods.suspension == i, "");
            break;
        case MP_TURBO:
            add("None", 0, 0, !gShops.savedMods.turbo, "");
            add("Turbo Tuning", 1, (long long)(kTurboPrice * f), gShops.savedMods.turbo, "Boost pressure for faster acceleration.");
            break;
        case MP_ARMOR:
            for (int i = 0; i <= 5; i++)
                add(i == 0 ? std::string("None") : StrFormat("Armor %d%%", i * 15), i, (long long)(kArmorPrice[i] * f), gShops.savedMods.armor == i,
                    i ? StrFormat("Takes %d%% less body damage.", i * 15) : "");
            break;
        case MP_TINT:
            for (int i = 0; i < 4; i++) add(kTintNames[i], i, kTintPrice[i], gShops.savedMods.tint == i, "");
            break;
        case MP_NEON:
            for (int i = 0; i < (int)ARRAY_COUNT(kNeons); i++) {
                bool inst = length(gShops.savedMods.neon - kNeons[i].srgb) < 0.02f;
                add(kNeons[i].name, i, i == 0 ? 0 : kNeonPrice, inst, i ? "Underglow shows best at night." : "");
            }
            break;
        case MP_SMOKE:
            for (int i = 0; i < (int)ARRAY_COUNT(kSmokes); i++) {
                bool inst = length(gShops.savedMods.smoke - kSmokes[i].srgb) < 0.02f;
                add(kSmokes[i].name, i, i == 0 ? 0 : kSmokePrice, inst, "");
            }
            break;
        default: break;
    }
    return items;
}

const char* modPageTitle(int page) {
    static const char* const kTitles[MP_COUNT] = {"TIDE CUSTOMS", "Primary Colour", "Secondary Colour", "Paint Finish", "Engine", "Brakes",
                                                   "Transmission", "Suspension", "Turbo", "Armor", "Window Tint", "Neon Kit", "Tire Smoke"};
    return page >= 0 && page < MP_COUNT ? kTitles[page] : "TIDE CUSTOMS";
}

void openModPage(GameWorld& g, int v, int page) {
    Vehicle& veh = g.vehicles[v];
    gShops.modPage = page;
    gShops.savedMods = veh.mods;
    gShops.savedColor0 = veh.color0;
    gShops.savedColor1 = veh.color1;
    gShops.modCursor = -1;
    std::vector<MenuItem> items = modItems(g, v, page);
    int cursor = 0;
    for (int i = 0; i < (int)items.size(); i++)
        if (items[i].right == "INSTALLED") cursor = i;
    const char* shopName = gShops.garageShop >= 0 ? gShops.shops[gShops.garageShop].name : "Tide Customs";
    std::string sub = page == MP_MAIN ? StrFormat("%s   Cash $%lld", shopName, g.pinfo.money) : StrFormat("%s   Cash $%lld", modPageTitle(page), g.pinfo.money);
    if (gMenu.open && gMenu.owner == MO_RESPRAY) {
        gMenu.items = items;
        gMenu.subtitle = sub;
        gMenu.cursor = cursor;
        gMenu.title = page == MP_MAIN ? "TIDE CUSTOMS" : modPageTitle(page);
    } else {
        menuOpen(g, MO_RESPRAY, page == MP_MAIN ? "TIDE CUSTOMS" : modPageTitle(page), sub, items, true, 0xff2aa6ffu, cursor);
    }
    g.playerControl = false;
}

// live preview of the highlighted option (visual categories only)
void previewMod(GameWorld& g, int v, int page, int id) {
    Vehicle& veh = g.vehicles[v];
    veh.mods = gShops.savedMods;
    veh.color0 = gShops.savedColor0;
    veh.color1 = gShops.savedColor1;
    if (id < 0) return;
    switch (page) {
        case MP_PRIMARY: veh.color0 = lin(kPaints[id].srgb.x, kPaints[id].srgb.y, kPaints[id].srgb.z); break;
        case MP_SECONDARY: veh.color1 = lin(kPaints[id].srgb.x, kPaints[id].srgb.y, kPaints[id].srgb.z); break;
        case MP_FINISH: veh.mods.finish = (u8)id; break;
        case MP_TINT: veh.mods.tint = (u8)id; break;
        case MP_NEON: veh.mods.neon = kNeons[id].srgb; break;
        case MP_SMOKE: veh.mods.smoke = kSmokes[id].srgb; break;
        default: break;
    }
}

void closeGarage(GameWorld& g) {
    int v = gShops.resprayVeh;
    if (v >= 0 && v < (int)g.vehicles.size() && g.vehicles[v].used && gShops.modPage != MP_MAIN) previewMod(g, v, gShops.modPage, -1);
    if (gMenu.open && gMenu.owner == MO_RESPRAY) menuClose(g);
    g.playerControl = false;
    gShops.resprayStage = 3;
    g.fadeOut(0.5f);
}

// buy the chosen option; returns true when something was bought
bool buyMod(GameWorld& g, int v, int page, int id) {
    Vehicle& veh = g.vehicles[v];
    std::vector<MenuItem> items = modItems(g, v, page);
    const MenuItem* it = nullptr;
    for (const MenuItem& m : items)
        if (m.id == id) it = &m;
    if (!it) return false;
    if (it->right == "INSTALLED") return false;
    long long price = Max(it->price, 0ll);
    if (g.pinfo.money < price) {
        g.help("You can't afford that.", 2.f);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_UI_ERROR, 0.6f);
#endif
        return false;
    }
    // commit: the preview already shows visual parts; handling parts are fitted now
    previewMod(g, v, page, id);
    switch (page) {
        case MP_ENGINE: veh.mods.engine = (u8)id; break;
        case MP_BRAKES: veh.mods.brakes = (u8)id; break;
        case MP_TRANSMISSION: veh.mods.transmission = (u8)id; break;
        case MP_SUSPENSION: veh.mods.suspension = (u8)id; break;
        case MP_TURBO: veh.mods.turbo = id == 1; break;
        case MP_ARMOR: veh.mods.armor = (u8)id; break;
        default: break;
    }
    applyModHandling(g, v);
    money(g, -price);
    gShops.savedMods = veh.mods;
    gShops.savedColor0 = veh.color0;
    gShops.savedColor1 = veh.color1;
    saveOwnedMods(g, v);
#ifdef HAVE_AUDIO
    Audio::play2D(price > 0 ? Audio::SFX_PURCHASE : Audio::SFX_UI_SELECT, 0.8f);
#endif
    return true;
}

void updateTideCustoms(GameWorld& g, ShopSite& s, int shopIndex, float dt) {
    int pv = g.playerVehicle();
    bool at = pv >= 0 && g.peds[g.player].seat == 0 && ::length(vehPos(g, pv).xy() - s.marker.xy()) < 5.f && !g.isBoat(pv) && !g.isAircraft(pv);
    if (gShops.resprayStage == 0) {
        if (!at) {
            s.inside = false;
            return;
        }
        if (s.inside || gMenu.open) return;
        if (vehicleSpeed(g, pv) > 3.f) return;
        s.inside = true;
        if (g.pinfo.wanted > 0 && g.pinfo.policeSeesPlayer) {
            g.help("The police are watching. Lose them before you pull into the garage.", 3.f);
            return;
        }
        gShops.resprayStage = 1;
        gShops.resprayVeh = pv;
        gShops.garageShop = shopIndex;
        g.fadeOut(0.5f);
        g.playerControl = false;
        g.vehicles[pv].ctl = Vehicles::VehicleControls();
        g.vehicles[pv].ctl.brake = 1.f;
        return;
    }
    int v = gShops.resprayVeh;
    bool vehOk = v >= 0 && v < (int)g.vehicles.size() && g.vehicles[v].used && !g.vehicles[v].exploded;
    if (gShops.garageShop != shopIndex) return;
    if (gShops.resprayStage == 1 && g.fadedOut()) {
        if (!vehOk) {
            gShops.resprayStage = 3;
            return;
        }
        // inside: the police lose track of an unseen car the moment the door closes
        gShops.clearedWanted = g.pinfo.wanted > 0;
        if (gShops.clearedWanted) {
            g.pinfo.wanted = 0;
            g.pinfo.wantedHeat = 0.f;
            g.pinfo.wantedCooldown = 0.f;
            g.notify("TIDE CUSTOMS", "The garage door rolls down. The cops lost track of you.");
        }
        Vehicle& veh = g.vehicles[v];
        veh.sim.body.vel = vec3(0.f);
        veh.sim.body.angVel = vec3(0.f);
        gShops.camAngle = atan2f(veh.sim.forward().y, veh.sim.forward().x) + 0.9f;
        g.rig.scriptActive = true;
        g.rig.cut = true;
        g.hudVisible = false;
        openModPage(g, v, MP_MAIN);
        g.fadeIn(0.6f);
        gShops.resprayStage = 2;
    }
    if (gShops.resprayStage == 2) {
        if (!vehOk || !menuIs(MO_RESPRAY)) {
            closeGarage(g);
            return;
        }
        // slow orbit around the car
        Vehicle& veh = g.vehicles[v];
        gShops.camAngle += dt * 0.22f;
        vec3 c = veh.sim.body.pos.toVec3();
        float r = length(g.vassets[veh.model].spec.boxHalf.xy()) * 1.9f + 2.2f;
        g.rig.scriptPos = dvec3(c + vec3(cosf(gShops.camAngle) * r, sinf(gShops.camAngle) * r, 1.6f));
        g.rig.scriptTarget = dvec3(c + vec3(0.f, 0.f, 0.3f));
        g.rig.scriptFov = 50.f;
        veh.ctl = Vehicles::VehicleControls();
        veh.ctl.brake = 1.f;
        int page = gShops.modPage;
        bool preview = page == MP_PRIMARY || page == MP_SECONDARY || page == MP_FINISH || page == MP_TINT || page == MP_NEON || page == MP_SMOKE;
        if (preview && gMenu.cursor != gShops.modCursor && gMenu.cursor >= 0 && gMenu.cursor < (int)gMenu.items.size()) {
            gShops.modCursor = gMenu.cursor;
            previewMod(g, v, page, gMenu.items[gMenu.cursor].id);
        }
        if (gMenu.cancelled) {
            if (page == MP_MAIN) {
                closeGarage(g);
            } else {
                previewMod(g, v, page, -1);
                openModPage(g, v, MP_MAIN);
            }
            return;
        }
        if (gMenu.chosen >= 0) {
            int id = gMenu.chosen;
            if (page == MP_MAIN) {
                if (id == MI_LEAVE) {
                    closeGarage(g);
                } else if (id == MI_REPAIR) {
                    long long rp = repairPrice(g, v);
                    if (rp > 0 && g.pinfo.money >= rp) {
                        repairVehicle(veh);
                        money(g, -rp);
#ifdef HAVE_AUDIO
                        Audio::play2D(Audio::SFX_CASH_REGISTER, 0.7f);
#endif
                        openModPage(g, v, MP_MAIN);
                    }
                } else if (id == MI_RESPRAY) {
                    if (g.pinfo.money >= kRespray) {
                        const Vehicles::VehicleModel& spec = g.vassets[veh.model].spec;
                        u32 h = hash32(veh.uid * 31u + (u32)(g.time * 10.0));
                        veh.color0 = spec.paletteColors.empty() ? hsvToRgb(hashToFloat(h), 0.7f, 0.6f) : spec.paletteColors[h % spec.paletteColors.size()];
                        if (length(veh.color0 - spec.liveryPrimary) < 0.01f && !spec.paletteColors.empty())
                            veh.color0 = spec.paletteColors[(h + 1) % spec.paletteColors.size()];
                        repairVehicle(veh);
                        money(g, -kRespray);
                        saveOwnedMods(g, v);
#ifdef HAVE_AUDIO
                        Audio::play2D(Audio::SFX_CASH_REGISTER, 0.7f);
#endif
                        openModPage(g, v, MP_MAIN);
                    }
                } else if (id > MP_MAIN && id < MP_COUNT) {
                    openModPage(g, v, id);
                }
            } else if (buyMod(g, v, page, id)) {
                int cur = gMenu.cursor;
                gMenu.items = modItems(g, v, page);
                gMenu.subtitle = StrFormat("%s   Cash $%lld", modPageTitle(page), g.pinfo.money);
                gMenu.cursor = Clamp(cur, 0, Max(0, (int)gMenu.items.size() - 1));
                gShops.modCursor = gMenu.cursor;
            }
        }
        return;
    }
    if (gShops.resprayStage == 3 && g.fadedOut()) {
        g.rig.scriptActive = false;
        g.rig.cut = true;
        g.hudVisible = true;
        g.playerControl = true;
        g.fadeIn(0.6f);
        gShops.resprayStage = 0;
        gShops.garageShop = -1;
        gShops.modPage = MP_MAIN;
        if (vehOk) saveOwnedMods(g, v);
    }
}

// ------------------------------------------------------------------------------------------------------------------
void shopsUpdate(GameWorld& g, float dt) {
    if (!gShops.init) {
        gShops.init = true;
        initShops(g);
    }
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    bool onFoot = pl->state == PS_ONFOOT;
    bool free = !gMissions.active && g.playerControl && !g.mInCutscene();
    // pending save request: if the app did not consume it, save straight into the safehouse slot
    if (gShops.saveFrames >= 0) {
        if (!g.requestSaveMenu) gShops.saveFrames = -1;
        else if (++gShops.saveFrames > 3) {
            g.requestSaveMenu = false;
            gShops.saveFrames = -1;
            if (g.saveGame(7, g.storyTitle)) g.notify("GAME SAVED", "Safehouse save (slot 8)");
        }
    }
    // ---- blips + markers
    for (size_t i = 0; i < gShops.shops.size(); i++) {
        ShopSite& s = gShops.shops[i];
        if (s.requiresFlag >= 0 && !storyDone(g, s.requiresFlag)) continue;
        UI::Blip b;
        b.pos = s.marker.xy();
        b.icon = s.kind == SHOP_GUNS ? UI::BLIP_GUN_SHOP : (s.kind == SHOP_RESPRAY ? UI::BLIP_GARAGE : (s.kind == SHOP_CLOTHES ? UI::BLIP_CLOTHES_SHOP : UI::BLIP_CAR_SHOP));
        b.shortRange = true;
        b.label = s.name;
        g.missionBlips.push_back(b);
        float d = ::length(s.marker - pp);
        if (d < 120.f && !gMissions.active) {
            worldMarker(s.marker, s.kind == SHOP_RESPRAY ? 3.f : 1.1f, s.kind == SHOP_RESPRAY ? vec3(0.9f, 0.5f, 1.f) : vec3(0.3f, 0.8f, 1.f));
        }
    }
    for (size_t i = 0; i < gShops.safehouses.size(); i++) {
        Safehouse& s = gShops.safehouses[i];
        bool owned = safehouseOwned(g, (int)i);
        bool available = s.requiresFlag < 0 || storyDone(g, s.requiresFlag);
        if (!owned && !available) continue;
        if (owned && s.owner >= 0 && s.owner != g.protagonistIndex && s.price == 0) {
            // the other protagonist's home: shown but dimmed
        }
        UI::Blip b;
        b.pos = s.save.xy();
        b.icon = UI::BLIP_SAFEHOUSE;
        b.label = s.name;
        b.shortRange = !owned;
        b.color = owned ? 0xffffffffu : 0xff60c060u;
        g.missionBlips.push_back(b);
    }
    for (size_t i = 0; i < gShops.businesses.size(); i++) {
        Business& b = gShops.businesses[i];
        bool owned = businessOwned(g, (int)i);
        bool avail = b.requiresFlag < 0 || flag(g, b.requiresFlag);
        if (!owned && !avail) continue;
        UI::Blip bl;
        bl.pos = b.marker.xy();
        bl.icon = b.icon;
        bl.label = b.name;
        bl.shortRange = true;
        bl.color = owned ? 0xff60ff60u : 0xff40c0ffu;
        g.missionBlips.push_back(bl);
    }
    // ---- menus in progress
    if (gShops.activeShop >= 0) {
        ShopSite& s = gShops.shops[gShops.activeShop];
        int owner = s.kind == SHOP_GUNS ? MO_SHOP_GUNS : (s.kind == SHOP_CLOTHES ? MO_SHOP_CLOTHES : MO_SHOP_CARS);
        if (!menuIs(owner)) gShops.activeShop = -1;
        else if (gMenu.cancelled) {
            menuClose(g);
            gShops.activeShop = -1;
        } else if (gMenu.chosen >= 0) {
            int id = gMenu.chosen;
            if (s.kind == SHOP_GUNS) {
                gunPurchase(g, id);
                menuRefresh(gunItems(g), StrFormat("Cash $%lld", g.pinfo.money));
            } else if (s.kind == SHOP_CLOTHES) {
                int who = g.protagonistIndex;
                if (!outfitOwned(g, who, id)) {
                    money(g, -kOutfits[who][id].price);
                    setOutfitOwned(g, who, id);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_PURCHASE, 0.8f);
#endif
                    int n = 0;
                    for (int w = 0; w < 2; w++)
                        for (int k = 0; k < kOutfitCount; k++) n += outfitOwned(g, w, k) && !kOutfits[w][k].story ? 1 : 0;
                    if (n >= 14) setFlag(g, SIDE_OUTFITS, 1);
                }
                wearOutfit(g, who, id);
                menuRefresh(outfitItems(g, true), StrFormat("Cash $%lld", g.pinfo.money));
            } else if (s.kind == SHOP_CARS) {
                const Vehicles::VehicleModel& m = g.vassets[id].spec;
                money(g, -m.price);
                g.ownedVehicleModels.push_back(id);
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_PURCHASE, 0.8f);
#endif
                int v = spawnOwnedVehicle(g, id, s.place.curb, s.place.curbYaw);
                (void)v;
                g.socialReport(UI::TE_PURCHASE, dvec3(s.place.curb), m.name.c_str(), (float)m.price);
                g.notify("PALM MOTORS", StrFormat("%s %s is yours. It's parked outside and waits in every safehouse garage.", m.maker.c_str(), m.name.c_str()));
                menuClose(g);
                gShops.activeShop = -1;
            }
        }
        return;
    }
    if (gShops.activeSafehouse >= 0) {
        Safehouse& s = gShops.safehouses[gShops.activeSafehouse];
        if (menuIs(MO_WARDROBE)) {
            if (gMenu.cancelled) {
                menuClose(g);
                gShops.activeSafehouse = -1;
            } else if (gMenu.chosen >= 0) {
                wearOutfit(g, g.protagonistIndex, gMenu.chosen);
                menuRefresh(outfitItems(g, false), kOutfits[g.protagonistIndex][currentOutfit(g, g.protagonistIndex)].name);
            }
        } else if (menuIs(MO_GARAGE)) {
            if (gMenu.cancelled) {
                menuClose(g);
                gShops.activeSafehouse = -1;
            } else if (gMenu.chosen >= 0) {
                // the previous garage car goes back inside unless the player is using it
                if (s.garageVehicle >= 0 && s.garageVehicle < (int)g.vehicles.size() && g.vehicles[s.garageVehicle].used &&
                    g.vehicles[s.garageVehicle].uid == s.garageUid && g.playerVehicle() != s.garageVehicle && !g.vehicles[s.garageVehicle].playerUsed)
                    g.despawnVehicle(s.garageVehicle, true);
                s.garageVehicle = spawnOwnedVehicle(g, gMenu.chosen, s.garage, s.garageYaw);
                if (s.garageVehicle >= 0) {
                    g.vehicles[s.garageVehicle].playerUsed = false;
                    s.garageUid = g.vehicles[s.garageVehicle].uid;
                }
                menuClose(g);
                gShops.activeSafehouse = -1;
            }
        } else if (menuIs(MO_PROPERTY)) {
            if (gMenu.cancelled || gMenu.chosen == 0) {
                menuClose(g);
                gShops.activeSafehouse = -1;
            } else if (gMenu.chosen == 1) {
                money(g, -s.price);
                setFlag(g, EX_SAFEHOUSE + gShops.activeSafehouse, 1);
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_PURCHASE, 0.8f);
#endif
                g.bigMessage("PROPERTY PURCHASED", s.name, 0xff33ccffu);
                g.socialReport(UI::TE_PURCHASE, dvec3(s.save), s.name, (float)s.price);
                int n = 0;
                for (size_t i = 0; i < gShops.safehouses.size(); i++) n += safehouseOwned(g, (int)i) ? 1 : 0;
                if (n == (int)gShops.safehouses.size()) setFlag(g, SIDE_SAFEHOUSES_ALL, 1);
                menuClose(g);
                gShops.activeSafehouse = -1;
            }
        } else {
            gShops.activeSafehouse = -1;
        }
        return;
    }
    if (gShops.activeBusiness >= 0) {
        Business& b = gShops.businesses[gShops.activeBusiness];
        if (!menuIs(MO_BUSINESS)) gShops.activeBusiness = -1;
        else if (gMenu.cancelled || gMenu.chosen == 0) {
            menuClose(g);
            gShops.activeBusiness = -1;
        } else if (gMenu.chosen == 1) {
            money(g, -b.price);
            setFlag(g, EX_BUSINESS + gShops.activeBusiness, 1);
            setFlag(g, EX_BUSINESS_DAY + gShops.activeBusiness, g.gameDay);
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_PURCHASE, 0.8f);
#endif
            g.bigMessage("BUSINESS PURCHASED", StrFormat("%s  +$%d a day", b.name, b.income), 0xff33ccffu);
            g.socialReport(UI::TE_PURCHASE, dvec3(b.marker), b.name, (float)b.price);
            int n = 0;
            for (size_t i = 0; i < gShops.businesses.size(); i++) n += businessOwned(g, (int)i) ? 1 : 0;
            if (n == (int)gShops.businesses.size()) setFlag(g, SIDE_BUSINESSES_ALL, 1);
            menuClose(g);
            gShops.activeBusiness = -1;
        }
        return;
    }
    // ---- respray works in and out of missions
    for (size_t i = 0; i < gShops.shops.size(); i++) {
        ShopSite& s = gShops.shops[i];
        if (s.kind == SHOP_RESPRAY && (s.requiresFlag < 0 || storyDone(g, s.requiresFlag))) updateTideCustoms(g, s, (int)i, dt);
    }
    if (gShops.resprayStage != 0) return;
    if (!free || gMenu.open) return;
    // ---- walk-in shop markers
    for (size_t i = 0; i < gShops.shops.size(); i++) {
        ShopSite& s = gShops.shops[i];
        if (s.kind == SHOP_RESPRAY || (s.requiresFlag >= 0 && !storyDone(g, s.requiresFlag))) continue;
        bool at = onFoot && ::length(pp.xy() - s.marker.xy()) < 1.4f && fabsf(pp.z - s.marker.z) < 2.5f;
        if (!at) {
            s.inside = false;
            continue;
        }
        if (s.inside) continue;
        s.inside = true;
        gShops.activeShop = (int)i;
        if (s.kind == SHOP_GUNS) menuOpen(g, MO_SHOP_GUNS, s.name, StrFormat("Cash $%lld", g.pinfo.money), gunItems(g), true, 0xff2030d0u);
        else if (s.kind == SHOP_CLOTHES) menuOpen(g, MO_SHOP_CLOTHES, s.name, StrFormat("Cash $%lld", g.pinfo.money), outfitItems(g, true), true, 0xffc060e0u);
        else menuOpen(g, MO_SHOP_CARS, s.name, StrFormat("Cash $%lld", g.pinfo.money), dealershipItems(g), true, 0xff40b0ffu);
        return;
    }
    // ---- safehouses: save/rest, wardrobe, garage, purchase
    for (size_t i = 0; i < gShops.safehouses.size(); i++) {
        Safehouse& s = gShops.safehouses[i];
        bool owned = safehouseOwned(g, (int)i);
        bool available = s.requiresFlag < 0 || storyDone(g, s.requiresFlag);
        float d = ::length(pp.xy() - s.save.xy());
        if (d > 200.f) continue;
        if (!owned) {
            if (!available) continue;
            worldMarker(s.save, 1.1f, vec3(0.3f, 1.f, 0.4f));
            bool at = onFoot && d < 1.4f;
            if (!at) {
                s.inBuy = false;
                continue;
            }
            if (s.inBuy) continue;
            s.inBuy = true;
            gShops.activeSafehouse = (int)i;
            std::vector<MenuItem> items(2);
            items[0].label = "Not now";
            items[0].id = 0;
            items[1].label = StrFormat("Buy for $%lld", s.price);
            items[1].enabled = g.pinfo.money >= s.price;
            items[1].detail = "Save point, wardrobe and a garage for your vehicles.";
            items[1].id = 1;
            menuOpen(g, MO_PROPERTY, s.name, "For sale", items, true, 0xff40c060u, 1);
            return;
        }
        if (s.owner >= 0 && s.owner != g.protagonistIndex) continue;   // the other protagonist's own home
        worldMarker(s.save, 0.9f, vec3(1.f, 0.85f, 0.2f));
        worldMarker(s.wardrobe, 0.9f, vec3(0.9f, 0.4f, 1.f));
        worldMarker(s.garage, 2.5f, vec3(0.3f, 0.8f, 1.f));
        bool atSave = onFoot && d < 1.3f;
        bool atWardrobe = onFoot && ::length(pp.xy() - s.wardrobe.xy()) < 1.3f;
        bool atGarage = onFoot && ::length(pp.xy() - s.garage.xy()) < 2.2f;
        if (atSave && !s.inSave) {
            s.inSave = true;
            // rest six hours, heal, then save
            g.env->timeOfDay += 6.f;
            if (g.env->timeOfDay >= 24.f) {
                g.env->timeOfDay -= 24.f;
                g.gameDay++;
            }
            pl->health = pl->maxHealth;
            g.requestSaveMenu = true;
            gShops.saveFrames = 0;
            g.notify(s.name, "You rest for six hours.");
            return;
        }
        if (!atSave) s.inSave = false;
        if (atWardrobe && !s.inWardrobe) {
            s.inWardrobe = true;
            gShops.activeSafehouse = (int)i;
            menuOpen(g, MO_WARDROBE, "Wardrobe", kOutfits[g.protagonistIndex][currentOutfit(g, g.protagonistIndex)].name, outfitItems(g, false), true, 0xffc060e0u);
            return;
        }
        if (!atWardrobe) s.inWardrobe = false;
        if (atGarage && !s.inGarage) {
            s.inGarage = true;
            gShops.activeSafehouse = (int)i;
            menuOpen(g, MO_GARAGE, "Garage", s.name, garageItems(g), true, 0xff40b0ffu);
            return;
        }
        if (!atGarage) s.inGarage = false;
    }
    // ---- businesses
    for (size_t i = 0; i < gShops.businesses.size(); i++) {
        Business& b = gShops.businesses[i];
        bool owned = businessOwned(g, (int)i);
        bool avail = b.requiresFlag < 0 || flag(g, b.requiresFlag);
        if (!owned && !avail) continue;
        float d = ::length(pp.xy() - b.marker.xy());
        if (d > 150.f) continue;
        worldMarker(b.marker, 1.f, owned ? vec3(0.3f, 1.f, 0.4f) : vec3(0.2f, 0.9f, 0.3f));
        bool at = onFoot && d < 1.3f;
        if (!at) {
            b.inside = false;
            continue;
        }
        if (b.inside) continue;
        b.inside = true;
        if (owned) {
            g.notify(b.name, StrFormat("Yours. Earns $%d a day, paid into your account every morning.", b.income));
            continue;
        }
        gShops.activeBusiness = (int)i;
        std::vector<MenuItem> items(2);
        items[0].label = "Not now";
        items[0].id = 0;
        items[1].label = StrFormat("Buy for $%lld", b.price);
        items[1].right = StrFormat("+$%d/day", b.income);
        items[1].enabled = g.pinfo.money >= b.price;
        items[1].detail = b.desc;
        items[1].id = 1;
        menuOpen(g, MO_BUSINESS, b.name, "Business for sale", items, true, 0xff40c060u, 1);
        return;
    }
}

}  // namespace mu
}  // namespace Game
