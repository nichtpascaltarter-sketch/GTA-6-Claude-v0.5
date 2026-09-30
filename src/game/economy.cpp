// Economy and story contact: story phone calls and texts that point the player to newly available missions, the contact
// calls, the lists behind the handset's apps (Wheels.ps vehicles, Dynasty Realty listings, side jobs, mission replay)
// and the daily income of owned businesses. The handset itself is driven from phone_game.cpp.
#include "missions.h"

namespace Game {
namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Story calls: delivered once when the mission becomes available (to the right protagonist)
struct StoryCall {
    const char* mission;
    int cast;                     // caller (-1: text only)
    const char* lines[3];         // caller lines (and the protagonist reply as the last entry when it starts with '>')
    const char* text;             // text message shown as a notification
};

const StoryCall kCalls[] = {
    {"repo_man", CAST_ROOK, {"Dex. It's Rook. I've got a job with your name on it. Come by the yard.", ">On my way.", nullptr}, "ROOK: Job at the salvage yard."},
    {"dry_dock", CAST_ROOK, {"The bank has another repo for you. A boatyard on the river. Paperwork's at your trailer.", ">Boats. Great.", nullptr},
     "ROOK: Boatyard repo. Papers at your trailer."},
    {"pressure", CAST_LUCHA, {"Mari, mija, can you come to the diner? There's a man here in a very expensive suit.", ">I'll be right there, Lucha.", nullptr},
     "LUCHA: Come to the diner."},
    {"collateral", CAST_LUCHA, {"Marisol! They took Tomas! Come to the diner, quick!", ">I'm coming. Don't open the door for anyone.", nullptr},
     "LUCHA: They took Tomas!"},
    {"pink_slips", -1, {nullptr, nullptr, nullptr}, "CHUY: Tonight. Calle Luna, south side. You, me, and your pink slip, repo man."},
    {"last_call", CAST_LUCHA, {"Dinner tonight. You and your big friend. I insist. Nobody says no to Lucha.", ">We'll be there.", nullptr},
     "LUCHA: Dinner tonight at the diner."},
    {"dead_air", CAST_TOMAS, {"Mari, my friend Kit at Pulse FM can crack that phone Dex grabbed. She's downtown.", ">Tell her I'm on my way.", nullptr},
     "TOMAS: Kit at Pulse FM can crack the phone."},
    {"velvet_rope", CAST_KIT, {"Friday is tonight. Club Riptide on Sol Beach. Wear something nice, the bouncer is picky.", ">I own one nice thing.", nullptr},
     "KIT: Club Riptide tonight. Dress up."},
    {"bagman", CAST_ROOK, {"Holt's bagman does his rounds today. He starts at the precinct downtown. Feel like robbing a cop?", ">Always.", nullptr},
     "ROOK: Holt's bagman, downtown."},
    {"sawgrass_run", CAST_JONAH, {"Dex Calloway. It's Jonah Pike. The Cuervos are running boats through my swamp. Come see me at the airboat dock.",
                                  ">Jonah? I'll be there.", nullptr},
     "JONAH: Airboat dock in the Sawgrass."},
    {"riptide", CAST_TOMAS, {"Kit found Sandoval's cash boat. Dad's old racer is ready at the yard. I tuned it myself. Well, mostly.", ">Mostly. Great.", nullptr},
     "TOMAS: The racer is ready at the boatyard."},
    {"heavy_lift", CAST_ROOK, {"There's a container at Port Isle with Sandoval's name all over it. Tonight, Dex.", ">I'll bring a crowbar.", nullptr},
     "ROOK: Port Isle, tonight."},
    {"fireworks", CAST_KIT, {"Sandoval's throwing a launch party on the Sol Beach Pier tonight. Holt will be there. I need you with me.", ">I'll meet you at the pier.", nullptr},
     "KIT: Sol Beach Pier, tonight."},
    {"second_chance", -1, {nullptr, nullptr, nullptr}, "KIT: Jonah went fishing off Ten Palms and his radio went quiet. The airport has a helicopter. Just saying."},
    {"paper_trail", CAST_KIT, {"Holt keeps a private ledger at her villa on Key Coral. She's at a fundraiser tonight.", ">Then I'll pay her a visit.", nullptr},
     "KIT: Key Coral, Holt's villa, tonight."},
    {"blueprints", CAST_ROOK, {"Everybody's at my yard. We're doing it, Dex. Solaris One.", ">About time.", nullptr}, "ROOK: Everybody's at the yard."},
    {"dress_rehearsal", CAST_ROOK, {"Mari. I need you for the prep work. Come by the yard.", ">On my way.", nullptr}, "ROOK: Prep work at the yard."},
    {"solaris_one", CAST_KIT, {"Everything's ready. Tonight we take Solaris One. Meet the crew at Rook's.", ">Let's go make some noise. Or not.", nullptr},
     "KIT: Tonight. Rook's yard."},
    {"overseas", -1, {nullptr, nullptr, nullptr}, "DEX: Lying low at the Redland safehouse with the bags. Come here, Mari. Something's wrong."},
    {"signal", CAST_KIT, {"I've got everything spread out on my desk at Pulse FM. We need to decide what to do with it. Tonight.", ">I'm coming.", nullptr},
     "KIT: Come to Pulse FM."},
};

struct EconomyState {
    float callCooldown = 20.f;
};
EconomyState gEco;

// handset side (phone_game.cpp)
bool phoneWired();
void phoneIncomingStoryCall(GameWorld& g, int callIndex, int missionDef);
void missionText(GameWorld& g, int di, const StoryCall* c, bool missed);
void addMessage(GameWorld& g, const std::string& from, const std::string& text, int contactId, bool mission, int missionDef, vec2* location,
                int action, const char* actionLabel);

bool callDelivered(GameWorld& g, int storyIndex) {
    int f = storyIndex < 31 ? EX_PHONE_STEP : EX_PHONE_STEP2;
    return (flag(g, f) >> (storyIndex % 31)) & 1;
}
void markCall(GameWorld& g, int storyIndex) {
    int f = storyIndex < 31 ? EX_PHONE_STEP : EX_PHONE_STEP2;
    setFlag(g, f, flag(g, f) | (1 << (storyIndex % 31)));
}

void playCall(GameWorld& g, const StoryCall& c) {
#ifdef HAVE_AUDIO
    Audio::play2D(c.cast >= 0 ? Audio::SFX_PHONE_RING : Audio::SFX_PHONE_MSG, 0.8f);
#endif
    if (c.cast >= 0) {
        for (const char* l : c.lines) {
            if (!l) break;
            if (l[0] == '>') sayMe(g, l + 1);
            else phoneLine(g, c.cast, l);
        }
    }
    const char* colon = strchr(c.text, ':');
    std::string title = colon ? std::string(c.text, colon - c.text) : std::string("MESSAGE");
    g.notify(title, colon ? std::string(colon + 2) : std::string(c.text));
}

// Story phone calls and texts
void updateStoryCalls(GameWorld& g, float dt) {
    gEco.callCooldown -= dt;
    if (gMissions.active || g.mInCutscene() || g.mTalking() || !g.playerControl || gEco.callCooldown > 0.f) return;
    if (!flag(g, EX_INTRO_DONE)) return;
    for (int i = 0; i < (int)gMissions.defs.size(); i++) {
        const MissionDef& d = gMissions.defs[i];
        if (d.storyIndex < 0 || !g.missionAvailable(i) || callDelivered(g, d.storyIndex)) continue;
        const StoryCall* call = nullptr;
        for (const StoryCall& c : kCalls)
            if (strcmp(c.mission, d.id) == 0) call = &c;
        markCall(g, d.storyIndex);
        if (!call) continue;
        if (d.protagonist >= 0 && d.protagonist != g.protagonistIndex) {
            g.notify(d.protagonist == 0 ? "MARI" : "DEX", StrFormat("New mission from %s: %s. Switch characters with the phone.", d.contact, d.title));
            if (phoneWired()) missionText(g, i, call, false);
        } else if (phoneWired()) {
            phoneIncomingStoryCall(g, (int)(call - kCalls), i);   // rings the handset (text for text-only calls)
        } else {
            playCall(g, *call);
        }
        gEco.callCooldown = 25.f;
        break;
    }
    // one-off texts and tips
    if (storyDone(g, SF_LOW_TIDE) && !flag(g, EX_SWITCH_TIP)) {
        setFlag(g, EX_SWITCH_TIP, 1);
        g.help("You can now play as ~b~Dex~s~. Open the phone with ~i:UP|UP~ and choose Switch.", 8.f);
        gEco.callCooldown = 10.f;
    }
}

// Daily income from owned businesses (paid at the first frame of a new game day, up to a week of backlog)
void updateIncome(GameWorld& g) {
    int last = flag(g, EX_LAST_PAYDAY);
    if (last == 0) {
        setFlag(g, EX_LAST_PAYDAY, g.gameDay);
        return;
    }
    if (g.gameDay <= last) return;
    int days = Min(g.gameDay - last, 7);
    setFlag(g, EX_LAST_PAYDAY, g.gameDay);
    long long total = 0;
    int count = 0;
    for (size_t i = 0; i < gShops.businesses.size(); i++) {
        if (!businessOwned(g, (int)i)) continue;
        total += (long long)gShops.businesses[i].income * days;
        count++;
    }
    if (total <= 0) return;
    money(g, total);
    setFlag(g, EX_INCOME_TOTAL, flag(g, EX_INCOME_TOTAL) + (int)(total / 100));
    g.notify("BUSINESS INCOME", StrFormat("%d business%s paid $%lld%s", count, count > 1 ? "es" : "", total, days > 1 ? StrFormat(" (%d days)", days).c_str() : ""));
    if (phoneWired())
        addMessage(g, "Palmera Community Bank", StrFormat("Deposit: $%lld from your %d business%s.", total, count, count > 1 ? "es" : ""), -1, false, -1,
                   nullptr, 0, nullptr);
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_CASH_REGISTER, 0.6f);
#endif
}

// ------------------------------------------------------------------------------------------------------------------
// Contacts and app lists (shown by the handset, see phone_game.cpp)
struct ContactInfo {
    int cast;
    int unlock;
    const char* who;
};
const ContactInfo kContacts[] = {{CAST_TOMAS, -1, "Tomas"}, {CAST_LUCHA, -1, "Mama Lucha"}, {CAST_ROOK, SF_LOW_TIDE, "Rook"},
                                 {CAST_KIT, SF_LAST_CALL, "Kit"}, {CAST_JONAH, SF_BAGMAN, "Jonah"}};

// The next story mission for a contact (for hints)
const MissionDef* nextMissionOf(GameWorld& g, const char* contact) {
    for (int i = 0; i < (int)gMissions.defs.size(); i++) {
        const MissionDef& d = gMissions.defs[i];
        if (d.storyIndex >= 0 && g.missionAvailable(i) && strstr(d.contact, contact)) return &d;
    }
    return nullptr;
}

void callContact(GameWorld& g, int id) {
#ifdef HAVE_AUDIO
    Audio::play2D(Audio::SFX_PHONE_RING, 0.6f);
#endif
    if (id == 50) {
        int who = g.protagonistIndex == 0 ? 1 : 0;
        sayMe(g, who == 1 ? "Hey, Dex. Just checking in." : "Mari. You okay?");
        DialogueLine l = line(who == 1 ? "Dex" : "Mari", who == 1 ? "Still breathing. Call me if the Cuervos show up." : "I'm fine. Go get some sleep, Dex.", -1,
                              who == 1 ? kColDex : kColMari);
        l.phone = true;
        l.hasVoice = true;
        l.voice = protagonistVoice(who);
        l.spoken = protagonistTags(who) + speakableText(l.text);
        g.mSay(l);
        return;
    }
    const ContactInfo& c = kContacts[id];
    const MissionDef* d = nextMissionOf(g, c.who);
    if (d) {
        phoneLine(g, c.cast, StrFormat("I need you for something. Come find me, it's marked on your map. We call it %s.", d->title));
        sayMe(g, "I'll be there.");
        return;
    }
    static const char* const kChat[5][2] = {
        {"Mari! I'm fine, I swear. I'm helping Lucha wash dishes. Forever, apparently.", "Good. Stay out of trouble."},
        {"Are you eating? You look thin on the phone. Come by, I made rice and beans.", "Soon, Lucha. I promise."},
        {"You calling to chat? I charge for chatting.", "Never mind, Rook."},
        {"You're on speaker, I'm mid set. Say hi to Porto Sol!", "Hi, Porto Sol."},
        {"Gators are biting, moonshine's cooking. Life's good out here.", "Save me a jar."},
    };
    int k = Clamp(id, 0, 4);
    phoneLine(g, c.cast, kChat[k][0]);
    sayMe(g, kChat[k][1]);
}

std::vector<MenuItem> realtyItems(GameWorld& g) {
    std::vector<MenuItem> items;
    for (size_t i = 0; i < gShops.safehouses.size(); i++) {
        const Safehouse& s = gShops.safehouses[i];
        bool owned = safehouseOwned(g, (int)i);
        bool avail = s.requiresFlag < 0 || storyDone(g, s.requiresFlag);
        MenuItem it;
        it.label = s.name;
        it.id = (int)i;
        if (owned) it.right = "OWNED";
        else if (avail) it.price = s.price;
        else it.right = "LOCKED";
        it.enabled = owned || avail;
        it.detail = owned ? "Safehouse: save, rest, wardrobe and garage. Select to mark it." : (avail ? "For sale. Select to mark it, then walk in to buy." : "Not on the market yet.");
        items.push_back(it);
    }
    for (size_t i = 0; i < gShops.businesses.size(); i++) {
        const Business& b = gShops.businesses[i];
        bool owned = businessOwned(g, (int)i);
        bool avail = b.requiresFlag < 0 || flag(g, b.requiresFlag);
        MenuItem it;
        it.label = b.name;
        it.id = 100 + (int)i;
        if (owned) it.right = StrFormat("+$%d/day", b.income);
        else if (avail) it.price = b.price;
        else it.right = "LOCKED";
        it.enabled = owned || avail;
        it.detail = std::string(b.desc) + StrFormat(" Income $%d a day.", b.income);
        items.push_back(it);
    }
    return items;
}

std::vector<MenuItem> jobItems(GameWorld& g) {
    std::vector<MenuItem> items;
    for (int i = 0; i < (int)gMissions.defs.size(); i++) {
        const MissionDef& d = gMissions.defs[i];
        if (d.storyIndex >= 0 || d.hidden) continue;
        bool avail = g.missionAvailable(i);
        MenuItem it;
        it.label = d.title[0] ? d.title : d.id;
        it.id = i;
        it.enabled = avail || (d.requiresFlag < 0 || flag(g, d.requiresFlag));
        it.right = avail ? "" : (d.timeFrom != d.timeTo ? StrFormat("%02.0f:00-%02.0f:00", d.timeFrom, d.timeTo) : std::string("LOCKED"));
        it.detail = d.setsFlag >= 0 && flag(g, d.setsFlag) ? "Completed. Select to mark it on the map." : "Select to mark it on the map.";
        items.push_back(it);
    }
    MenuItem j;
    j.label = "Taxi / Vigilante / Paramedic";
    j.detail = "Get into a taxi, police car or ambulance and press G (D-pad up) to start the job.";
    j.enabled = false;
    j.id = -5;
    items.push_back(j);
    return items;
}

std::vector<MenuItem> replayItems(GameWorld& g) {
    std::vector<MenuItem> items;
    for (int i = 0; i < (int)gMissions.defs.size(); i++) {
        const MissionDef& d = gMissions.defs[i];
        if (d.storyIndex < 0 || !flag(g, d.setsFlag)) continue;
        MenuItem it;
        it.label = d.title;
        it.id = i;
        it.right = d.protagonist == 0 ? "MARI" : (d.protagonist == 1 ? "DEX" : "");
        it.detail = StrFormat("Act %d. Contact: %s.", d.act, d.contact);
        items.push_back(it);
    }
    if (items.empty()) {
        MenuItem it;
        it.label = "Nothing to replay yet";
        it.enabled = false;
        items.push_back(it);
    }
    return items;
}

void setWaypoint(GameWorld& g, vec2 p) {
    g.hasWaypoint = true;
    g.waypoint = p;
    g.gpsRecalcTimer = 0.f;
    g.notify("GPS", "Waypoint set.");
}

// The phone key (Up / D-pad up) opens the handset; the app draws it and routes its actions to phoneHandle().
void updatePhoneKey(GameWorld& g) {
    Ped* pl = g.playerPed();
    if (!pl || g.phone.open || gMenu.open) return;
    int pv = g.playerVehicle();
    bool vehicleOk = pv < 0 || !g.isAircraft(pv);
    if (g.ctl.phone.pressed && g.playerControl && !g.mInCutscene() && vehicleOk && pl->health > 0.f && g.pinfo.deathTimer <= 0.f) g.phone.open = true;
}

void economyUpdate(GameWorld& g, float dt) {
    updateIncome(g);
    updatePhoneKey(g);
    updateStoryCalls(g, dt);
}

}  // namespace mu
}  // namespace Game
