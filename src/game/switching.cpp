// Protagonists: switching between Mari and Dex in free roam (the other one keeps their position, health and weapons),
// the sky-cam transition, and outfits (each outfit is a named character built from the protagonist's base look).
#include "missions.h"

namespace Game {
namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Outfits
struct Outfit {
    const char* name;
    const char* desc;
    int price;           // clothing store price (0 = starting outfit / story unlock)
    bool story;          // unlocked by the story, not sold
};

const Outfit kOutfits[2][8] = {
    {   // Mari
        {"Street", "Mari's everyday look: pink top, dark jeans, white sneakers.", 0, false},
        {"Boatyard Coveralls", "Work tee, heavy work pants, boots and a backwards cap. Smells of two-stroke oil.", 0, false},
        {"Night Out", "A red sundress for Sol Beach nights. Opens doors with velvet ropes.", 1400, false},
        {"Beach Day", "Swimsuit, sun hat and shades.", 600, false},
        {"Biker", "Black hoodie, jeans, boots and sunglasses.", 900, false},
        {"Business", "White blouse, slacks and flats. Looks like money.", 1800, false},
        {"Maintenance Crew", "Hi-vis vest, work pants and a hard hat. Nobody looks twice at the help.", 0, true},
        {"Track Suit", "Teal hoodie and leggings with running shoes. Built for speed.", 700, false},
    },
    {   // Dex
        {"Work Jacket", "Dex's olive work shirt, dark jeans and boots.", 0, false},
        {"Coast Guard Tee", "Faded orange tee, jeans, boots and an old unit cap.", 0, false},
        {"Hawaiian", "Loud shirt, shorts, sandals and aviators. Key Solano chic.", 500, false},
        {"Sharp Suit", "Charcoal suit, dress shoes. For meetings with people who own towers.", 2600, false},
        {"Tactical", "Dark hoodie, work pants, boots and a beanie.", 1100, false},
        {"Beach Bum", "Tank top, swim trunks, sandals and a cap.", 400, false},
        {"Maintenance Crew", "Hi-vis vest, work pants and a hard hat. Nobody looks twice at the help.", 0, true},
        {"Night Out", "Black polo, slacks and dress shoes.", 1200, false},
    },
};
const int kOutfitCount = 8;

int gBaseChar[2] = {-1, -1};
bool gSwitching = false;   // set while the player ped is being replaced by a character switch

Anim::CharacterDesc outfitDesc(GameWorld& g, int who, int outfit) {
    Anim::CharacterDesc d = g.chars[gBaseChar[who]].desc;
    d.hat = -1;
    d.glasses = -1;
    if (who == 0) {
        switch (outfit) {
            case 1: d.top = 0; d.topColor = lin(0.85f, 0.85f, 0.82f); d.bottom = 10; d.bottomColor = lin(0.2f, 0.28f, 0.42f); d.shoes = 2; d.shoeColor = lin(0.3f, 0.2f, 0.12f); d.hat = 1; break;
            case 2: d.top = 12; d.topColor = lin(0.8f, 0.08f, 0.14f); d.bottom = 4; d.bottomColor = lin(0.8f, 0.08f, 0.14f); d.shoes = 5; d.shoeColor = lin(0.08f, 0.05f, 0.05f); d.hairStyle = 5; break;
            case 3: d.top = 10; d.topColor = lin(0.1f, 0.65f, 0.75f); d.bottom = 6; d.bottomColor = lin(0.1f, 0.65f, 0.75f); d.shoes = 3; d.shoeColor = lin(0.9f, 0.85f, 0.7f); d.hat = 4; d.glasses = 0; break;
            case 4: d.top = 5; d.topColor = lin(0.06f, 0.06f, 0.07f); d.bottom = 0; d.bottomColor = lin(0.12f, 0.12f, 0.16f); d.shoes = 2; d.shoeColor = vec3(0.03f); d.glasses = 0; break;
            case 5: d.top = 13; d.topColor = lin(0.95f, 0.95f, 0.93f); d.bottom = 3; d.bottomColor = lin(0.12f, 0.12f, 0.15f); d.shoes = 5; d.shoeColor = vec3(0.02f); d.hairStyle = 6; break;
            case 6: d.top = 9; d.topColor = lin(0.95f, 0.75f, 0.1f); d.bottom = 10; d.bottomColor = lin(0.2f, 0.24f, 0.32f); d.shoes = 2; d.shoeColor = lin(0.3f, 0.2f, 0.1f); d.hat = 3; break;
            case 7: d.top = 5; d.topColor = lin(0.1f, 0.6f, 0.6f); d.bottom = 9; d.bottomColor = lin(0.05f, 0.05f, 0.06f); d.shoes = 6; d.shoeColor = lin(0.95f, 0.95f, 0.95f); d.hairStyle = 4; break;
            default: break;
        }
    } else {
        switch (outfit) {
            case 1: d.top = 0; d.topColor = lin(0.95f, 0.45f, 0.12f); d.bottom = 0; d.bottomColor = lin(0.2f, 0.25f, 0.4f); d.shoes = 2; d.shoeColor = lin(0.25f, 0.17f, 0.1f); d.hat = 0; break;
            case 2: d.top = 3; d.topColor = lin(0.1f, 0.45f, 0.7f); d.bottom = 1; d.bottomColor = lin(0.75f, 0.68f, 0.52f); d.shoes = 3; d.shoeColor = lin(0.35f, 0.25f, 0.15f); d.glasses = 1; break;
            case 3: d.top = 6; d.topColor = lin(0.18f, 0.19f, 0.22f); d.bottom = 3; d.bottomColor = lin(0.18f, 0.19f, 0.22f); d.shoes = 1; d.shoeColor = vec3(0.02f); break;
            case 4: d.top = 5; d.topColor = lin(0.08f, 0.09f, 0.1f); d.bottom = 10; d.bottomColor = lin(0.12f, 0.13f, 0.12f); d.shoes = 2; d.shoeColor = vec3(0.03f); d.hat = 6; break;
            case 5: d.top = 1; d.topColor = lin(0.95f, 0.95f, 0.95f); d.bottom = 5; d.bottomColor = lin(0.2f, 0.45f, 0.75f); d.shoes = 3; d.shoeColor = lin(0.3f, 0.3f, 0.3f); d.hat = 0; break;
            case 6: d.top = 9; d.topColor = lin(0.95f, 0.75f, 0.1f); d.bottom = 10; d.bottomColor = lin(0.2f, 0.24f, 0.32f); d.shoes = 2; d.shoeColor = lin(0.3f, 0.2f, 0.1f); d.hat = 3; break;
            case 7: d.top = 2; d.topColor = lin(0.05f, 0.05f, 0.06f); d.bottom = 3; d.bottomColor = lin(0.15f, 0.15f, 0.17f); d.shoes = 1; d.shoeColor = vec3(0.02f); break;
            default: break;
        }
    }
    return d;
}

int outfitChar(GameWorld& g, int who, int outfit) {
    if (gBaseChar[who] < 0) return g.protagonistChar[who];
    if (outfit <= 0 || outfit >= kOutfitCount) return gBaseChar[who];
    std::string key = StrFormat("outfit_%d_%d", who, outfit);
    return g.namedCharacter(key, outfitDesc(g, who, outfit));
}

int currentOutfit(GameWorld& g, int who) { return flag(g, who == 0 ? EX_OUTFIT_MARI : EX_OUTFIT_DEX); }

bool outfitOwned(GameWorld& g, int who, int outfit) {
    if (outfit <= 1) return true;
    return (flag(g, who == 0 ? EX_OUTFITS_MARI : EX_OUTFITS_DEX) >> outfit) & 1;
}

void setOutfitOwned(GameWorld& g, int who, int outfit) {
    int f = who == 0 ? EX_OUTFITS_MARI : EX_OUTFITS_DEX;
    setFlag(g, f, flag(g, f) | (1 << outfit));
}

// Keep the player ped (and protagonistChar used by spawnPlayer) in the selected outfit; handles loads and respawns.
void enforceOutfit(GameWorld& g) {
    for (int w = 0; w < 2; w++)
        if (gBaseChar[w] < 0) gBaseChar[w] = g.protagonistChar[w];
    for (int w = 0; w < 2; w++) {
        int want = outfitChar(g, w, currentOutfit(g, w));
        if (want >= 0) g.protagonistChar[w] = want;
    }
    Ped* pl = g.playerPed();
    if (!pl) return;
    int want = g.protagonistChar[Clamp(g.protagonistIndex, 0, 1)];
    if (want >= 0 && pl->charIndex != want && pl->state != PS_RAGDOLL && pl->state != PS_DEAD && !pl->ragdoll) {
        pl->charIndex = want;
        pl->anim.init(&g.chars[want].skel, pl->uid * 2654435761u);
        pl->female = g.chars[want].desc.gender == Anim::FEMALE;
        pl->voice = protagonistVoice(g.protagonistIndex);
        g.animatePed(*pl, 0.f);
    }
}

void wearOutfit(GameWorld& g, int who, int outfit) {
    setFlag(g, who == 0 ? EX_OUTFIT_MARI : EX_OUTFIT_DEX, outfit);
    enforceOutfit(g);
}

// ------------------------------------------------------------------------------------------------------------------
// The protagonist not being played is kept in extended flags (position in decimeters, yaw in milliradians).
struct ProtagonistState {
    bool valid = false;
    vec3 pos;
    float yaw = 0.f;
    float health = 200.f, armor = 0.f;
    u32 weapons = 1;
    int ammo[WPN_COUNT] = {};
    int weapon = 0;
};

ProtagonistState captureCurrent(GameWorld& g) {
    ProtagonistState s;
    Ped* pl = g.playerPed();
    if (!pl) return s;
    s.valid = true;
    s.pos = pl->pos.toVec3();
    int pv = pl->vehicle;
    if (pv >= 0) s.pos = vehPos(g, pv) + vec3(g.vehicles[pv].sim.right().xy() * -2.5f, 0.f);
    s.yaw = pl->yaw;
    s.health = pl->health;
    s.armor = pl->armor;
    s.weapons = 0;
    for (int w = 0; w < WPN_COUNT; w++) {
        if (pl->hasWeapon[w]) s.weapons |= 1u << w;
        s.ammo[w] = pl->ammo[w];
    }
    s.weapon = (int)pl->weapon;
    return s;
}

void storeOther(GameWorld& g, const ProtagonistState& s) {
    setFlag(g, EX_OTHER_VALID, s.valid ? 1 : 0);
    setFlag(g, EX_OTHER_X, (int)lroundf(s.pos.x * 10.f));
    setFlag(g, EX_OTHER_Y, (int)lroundf(s.pos.y * 10.f));
    setFlag(g, EX_OTHER_Z, (int)lroundf(s.pos.z * 10.f));
    setFlag(g, EX_OTHER_YAW, (int)lroundf(s.yaw * 1000.f));
    setFlag(g, EX_OTHER_HEALTH, (int)s.health);
    setFlag(g, EX_OTHER_ARMOR, (int)s.armor);
    setFlag(g, EX_OTHER_WEAPONS, (int)s.weapons);
    setFlag(g, EX_OTHER_WEAPON, s.weapon);
    for (int w = 0; w < WPN_COUNT && w < 12; w++) setFlag(g, EX_OTHER_AMMO + w, s.ammo[w]);
}

ProtagonistState loadOther(GameWorld& g, int who) {
    ProtagonistState s;
    if (flag(g, EX_OTHER_VALID)) {
        s.valid = true;
        s.pos = vec3(flag(g, EX_OTHER_X) * 0.1f, flag(g, EX_OTHER_Y) * 0.1f, flag(g, EX_OTHER_Z) * 0.1f);
        s.yaw = flag(g, EX_OTHER_YAW) * 0.001f;
        s.health = (float)Max(flag(g, EX_OTHER_HEALTH), 60);
        s.armor = (float)flag(g, EX_OTHER_ARMOR);
        s.weapons = (u32)flag(g, EX_OTHER_WEAPONS) | 1u;
        for (int w = 0; w < WPN_COUNT && w < 12; w++) s.ammo[w] = flag(g, EX_OTHER_AMMO + w);
        s.weapon = flag(g, EX_OTHER_WEAPON);
        return s;
    }
    // first switch: at home, with a pistol
    const Place& home = who == 0 ? gPlaces.mariApt : gPlaces.dexTrailer;
    s.valid = true;
    s.pos = home.pos;
    s.yaw = home.yaw;
    s.health = 200.f;
    s.weapons = (1u << WPN_FISTS) | (1u << WPN_PISTOL) | (who == 1 ? (1u << WPN_BAT) : (1u << WPN_KNIFE));
    s.ammo[WPN_PISTOL] = 60;
    s.weapon = WPN_FISTS;
    return s;
}

// Switches the controlled protagonist. `instant` skips the sky-cam transition (tests, mission starts).
bool switchProtagonist(GameWorld& g, int who, bool instant) {
    if (who == g.protagonistIndex || who < 0 || who > 1) return false;
    Ped* pl = g.playerPed();
    if (!pl || pl->health <= 0.f) return false;
    ProtagonistState cur = captureCurrent(g);
    ProtagonistState nxt = loadOther(g, who);
    vec3 oldPos = pl->pos.toVec3();
    if (pl->vehicle >= 0) {
        int v = pl->vehicle;
        g.removePedFromVehicle(g.player, false);
        g.vehicles[v].playerUsed = true;
        g.vehicles[v].parked = true;
        g.vehicles[v].ctl = Vehicles::VehicleControls();
    }
    int old = g.player;
    gSwitching = true;
    g.player = -1;
    g.peds[old].isPlayer = false;
    g.despawnPed(old);
    g.protagonistIndex = who;
    enforceOutfit(g);
    float z = groundAt(g, nxt.pos.x, nxt.pos.y, nxt.pos.z + 3.f);
    g.spawnPlayer(dvec3(nxt.pos.x, nxt.pos.y, Max(z, nxt.pos.z - 2.f)), nxt.yaw);
    Ped* np = g.playerPed();
    if (np) {
        np->health = Clamp(nxt.health, 40.f, np->maxHealth);
        np->armor = nxt.armor;
        for (int w = 0; w < WPN_COUNT; w++) {
            np->hasWeapon[w] = (nxt.weapons >> w) & 1u;
            np->ammo[w] = nxt.ammo[w];
            np->clip[w] = Min(weaponInfo((WeaponType)w).clipSize, np->ammo[w]);
        }
        np->hasWeapon[WPN_FISTS] = true;
        np->weapon = (WeaponType)Clamp(nxt.weapon, 0, (int)WPN_COUNT - 1);
        if (!np->hasWeapon[np->weapon]) np->weapon = WPN_FISTS;
    }
    storeOther(g, cur);
    g.pinfo.wanted = 0;
    g.pinfo.wantedHeat = 0.f;
    g.hasWaypoint = false;
    if (!instant && np) {
        // sky cam: rise above the old spot, sweep across, drop onto the new protagonist
        vec3 npos = np->pos.toVec3();
        float dist = length(npos.xy() - oldPos.xy());
        float h = Clamp(dist * 0.35f, 120.f, 600.f);
        std::vector<CutsceneShot> shots;
        shots.push_back(shotMove(oldPos + vec3(0, -4.f, 3.f), oldPos + vec3(0, 0, 1.f), oldPos + vec3(0, -6.f, h), oldPos, 1.1f, 55.f));
        shots.push_back(shotMove(oldPos + vec3(0, -6.f, h), oldPos, npos + vec3(0, -6.f, h), npos, dist > 50.f ? 1.4f : 0.6f, 60.f));
        shots.push_back(shotMove(npos + vec3(0, -6.f, h), npos, npos + vec3(-sinf(nxt.yaw) * -4.f, cosf(nxt.yaw) * -4.f, 2.2f), npos + vec3(0, 0, 1.2f), 1.1f, 55.f));
        g.mCutscene(shots, true);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_HELI_FLYBY, 0.35f, 1.6f);
#endif
    }
    g.notify(who == 0 ? "MARI" : "DEX", who == 0 ? "Marisol Ortega - Calle Luna" : "Dex Calloway - Palmetto Flats");
    return true;
}

bool switchUnlocked(GameWorld& g) { return storyDone(g, SF_LOW_TIDE); }

}  // namespace mu
}  // namespace Game
