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
    int resprayStage = 0;    // 0 idle, 1 fading out, 2 fading in
    int resprayVeh = -1;
    int saveFrames = -1;
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
        s.requiresFlag = req;
        gShops.shops.push_back(s);
    };
    shop(SHOP_GUNS, "Palmetto Arms", P.gunFlats, -1);
    shop(SHOP_GUNS, "Northside Arms", P.gunNorth, SF_LOW_TIDE);
    shop(SHOP_RESPRAY, "Tide Pro Paint & Body", P.resprayCL, -1);
    shop(SHOP_RESPRAY, "Ocean Drive Body Shop", P.resprayBeach, SF_LOW_TIDE);
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
// Respray: fade out, repaint + repair + clear an unseen wanted level, fade in
void updateRespray(GameWorld& g, ShopSite& s) {
    int pv = g.playerVehicle();
    bool at = pv >= 0 && ::length(vehPos(g, pv).xy() - s.marker.xy()) < 5.f && !g.isBoat(pv) && !g.isAircraft(pv);
    if (gShops.resprayStage == 0) {
        if (!at) {
            s.inside = false;
            return;
        }
        if (s.inside) return;
        if (vehicleSpeed(g, pv) > 3.f) return;
        s.inside = true;
        if (g.pinfo.wanted > 0 && g.pinfo.policeSeesPlayer) {
            g.help("The police are watching. Lose them before you respray.", 3.f);
            return;
        }
        const long long price = 300;
        if (g.pinfo.money < price) {
            g.help(StrFormat("%s: a respray costs $%lld.", s.name, price), 3.f);
            return;
        }
        gShops.resprayStage = 1;
        gShops.resprayVeh = pv;
        g.fadeOut(3.f);
        g.playerControl = false;
        g.vehicles[pv].ctl = Vehicles::VehicleControls();
        g.vehicles[pv].ctl.brake = 1.f;
    } else if (gShops.resprayStage == 1 && g.fadedOut()) {
        int v = gShops.resprayVeh;
        if (v >= 0 && g.vehicles[v].used) {
            Vehicle& veh = g.vehicles[v];
            const Vehicles::VehicleModel& spec = g.vassets[veh.model].spec;
            u32 h = hash32(veh.uid * 31u + (u32)(g.time * 10.0));
            veh.color0 = spec.paletteColors.empty() ? hsvToRgb(hashToFloat(h), 0.7f, 0.6f) : spec.paletteColors[h % spec.paletteColors.size()];
            if (length(veh.color0 - spec.liveryPrimary) < 0.01f && !spec.paletteColors.empty()) veh.color0 = spec.paletteColors[(h + 1) % spec.paletteColors.size()];
            veh.sim.health = 1000.f;
            veh.sim.engineHealth = 1000.f;
            for (float& z : veh.sim.damageZones) z = 0.f;
            for (int w = 0; w < veh.sim.wheelCount; w++) veh.sim.wheels[w].burst = false;
            veh.windowsBroken = false;   // new glass
            veh.glassHits = 0;
            veh.fireTimer = 0.f;
            veh.dirt = 0.f;
            auto it = std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), veh.model);
            if (it != g.ownedVehicleModels.end() && it - g.ownedVehicleModels.begin() < 40)
                setFlag(g, EX_VEHICLE_PAINT + (int)(it - g.ownedVehicleModels.begin()), packPaint(veh.color0));
        }
        money(g, -300);
        bool cleared = g.pinfo.wanted > 0;
        g.pinfo.wanted = 0;
        g.pinfo.wantedHeat = 0.f;
        g.pinfo.wantedCooldown = 0.f;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_CASH_REGISTER, 0.7f);
#endif
        g.notify("RESPRAY", cleared ? "New paint, engine fixed. The cops lost track of you.  -$300" : "New paint, engine fixed.  -$300");
        g.fadeIn(1.5f);
        g.playerControl = true;
        gShops.resprayStage = 0;
    }
}

// ------------------------------------------------------------------------------------------------------------------
void shopsUpdate(GameWorld& g, float dt) {
    (void)dt;
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
    for (ShopSite& s : gShops.shops)
        if (s.kind == SHOP_RESPRAY && (s.requiresFlag < 0 || storyDone(g, s.requiresFlag))) updateRespray(g, s);
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
