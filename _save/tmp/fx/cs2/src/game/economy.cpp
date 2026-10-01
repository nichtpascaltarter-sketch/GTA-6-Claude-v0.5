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
    // Act 4
    {"wake", CAST_LUCHA, {"Mija, a whole month and nobody has shot at you. That calls for a dinner. Tonight at the diner, the whole crew.",
                          ">I'll bring Tomas. And an appetite.", nullptr},
     "LUCHA: Dinner at the diner tonight. Everybody's coming."},
    {"box_numbers", CAST_ROOK, {"Kit cracked Nando's phone. Three container numbers at Port Isle, all Sable Maritime. Come by the garage.",
                                ">Night work. My favorite.", nullptr},
     "ROOK: Container numbers. Come by the garage."},
    {"blue_line", -1, {nullptr, nullptr, nullptr}, "KIT: Found one more thing on Nando's phone. Come by my studio in the Flats."},
    {"clear_air", CAST_KIT, {"Dex, Sable's accountant flies out tonight with her books on a drive. Hollis Pruitt. Be at airport departures.",
                             ">I hate airports.", nullptr},
     "KIT: Airport departures, tonight."},
    {"gator_country", CAST_JONAH, {"Miss Ortega, it's Jonah Pike. El Cuervo is sitting on Sable's guns at my granddaddy's old fish camp. "
                                   "Meet me at the airboat dock.", ">I'll be there at first light.", nullptr},
     "JONAH: Airboat dock in the Sawgrass. First light."},
    {"king_tide", CAST_TOMAS, {"Everybody's at the boatyard, Mari. Kit says the tide peaks at midnight, and the storm's coming in early.",
                               ">Tell them I'm on my way.", nullptr},
     "TOMAS: Everybody's at the boatyard. Tonight."},
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
    if (phoneWired() && gEco.callCooldown <= 0.f) {
        // open-world texts: side jobs, shops and properties as the story opens them up (one per cooldown)
        struct WorldText {
            int after;
            const char* from;
            const char* text;
            int where;   // 0 courier, 1 gun shop, 2 street race, 3 Tide Customs, 4 beach race, 5 condo, 6 river race, 7 flight school,
                         // 8 taxi depot, 9 range, 10 Sawgrass dock (wildlife census), 11..13 the strangers Rosa, Velma, Jaz,
                         // 14 the Night Series, 15 the harbor runs, 16 the pier bait shop (fishing), 17 the airboat tours
        };
        static const WorldText kTexts[] = {
            {SF_LOW_TIDE, "Rapido Couriers", "Fast wheels, faster legs? Rapido Couriers pays per drop. Come by the depot.", 0},
            {SF_REPO_MAN, "Chuy", "Street races in Calle Luna. Bring something fast. Or don't, I like winning.", 2},
            {SF_DRY_DOCK, "Tide Customs", "Paint, tuning, armor, neon. Pull into Tide Customs and we'll make it yours.", 3},
            {SF_PRESSURE, "Palmetto Arms", "Grand reopening. Ten percent off body armor for Calle Luna residents.", 1},
            {SF_DEAD_AIR, "Kit", "Need a quick buck? The Sol Beach Nights races start on the Deco strip after eight.", 4},
            {SF_BAGMAN, "Sol Cabs", "Drivers wanted. Own car not required, ours are yellow. Get in a cab and start a shift.", 8},
            {SF_VELVET_ROPE, "Dynasty Realty", "Your credit just got interesting. The Sol Beach Condo is on the market.", 5},
            {SF_SAWGRASS_RUN, "Jonah", "When you're bored of shooting at people, there's a boat race on the Rio Sol.", 6},
            {SF_SECOND_CHANCE, "Skyline Flight School", "Loved the rescue on the news. Lessons at the airport, first one's the circuit.", 7},
            {SF_PAPER_TRAIL, "Palmetto Arms", "The range is open late. Score three fifty and the ammo's on us.", 9},
            {SF_REPO_MAN, "Wildlife Trust",
             "Wild Porto Sol census! Photograph every species you meet with your phone camera. $250 for each new one, $10,000 for the full "
             "field guide.",
             10},
            // (append only: the delivered texts are a bitmask by index in saves)
            {SF_LOW_TIDE, "Mama Lucha", "Mija, Rosa Villanueva from down the street is asking for you. Something about Ernesto's car. Be nice, "
                                        "she's eighty one.", 11},
            {SF_DRY_DOCK, "Rook", "That blue hatchback we pulled last week? The owner's a nurse. Keeps calling the shop. Not my problem. Maybe yours.",
             12},
            {SF_PRESSURE, "Tidegram", "@jazonthetide is looking for a driver at the Sol Beach cafe. Paid in exposure. And cash.", 13},
            {SF_PINK_SLIPS, "Lalo Brisa",
             "Heard you beat Chuy. The Porto Sol Night Series runs after eight from Tide Customs in Calle Luna. Three legs, my cars, your "
             "nerve.",
             14},
            {SF_COLLATERAL, "Tomas", "The yard is taking delivery runs by boat to pay down the bank. I can't drive a boat to save my life. "
                                     "Come by the slip?", 15},
            {SF_LOW_TIDE, "Palmera Angler",
             "Rods are on the house for Porto Sol locals. Fish off the Sol Beach Pier or any dock, or cut the engine anywhere on the water. We "
             "buy the catch by the pound.",
             16},
            {SF_SAWGRASS_RUN, "Jonah", "Tourist season. I need somebody to drive the airboat tours while I tell the gator jokes. Pays in tips. "
                                       "Come by the dock.", 17},
        };
        int sent = flag(g, EX_WORLD_TEXTS);
        for (int i = 0; i < (int)ARRAY_COUNT(kTexts); i++) {
            const WorldText& t = kTexts[i];
            if ((sent >> i) & 1 || !storyDone(g, t.after)) continue;
            setFlag(g, EX_WORLD_TEXTS, sent | (1 << i));
            const Places& P = gPlaces;
            vec2 loc;
            auto defStart = [&](const char* id) {
                int di = gMissions.findDef(id);
                return di >= 0 ? gMissions.defs[di].startPos : P.courierDepot.pos.xy();
            };
            switch (t.where) {
                case 0: loc = P.courierDepot.pos.xy(); break;
                case 1: loc = P.gunFlats.pos.xy(); break;
                case 2: loc = defStart("race_calle"); break;
                case 3: loc = P.resprayCL.curb.xy(); break;
                case 4: loc = defStart("race_beach"); break;
                case 5: loc = P.beachCondo.pos.xy(); break;
                case 6: loc = defStart("boat_river"); break;
                case 7: loc = defStart("flight_1"); break;
                case 8: loc = P.taxiDepot.pos.xy(); break;
                case 10: loc = P.sawgrassDock.xy(); break;
                case 11: loc = defStart("rosa_1"); break;
                case 12: loc = defStart("velma_1"); break;
                case 13: loc = defStart("jaz_1"); break;
                case 14: loc = defStart("series"); break;
                case 15: loc = defStart("harbor"); break;
                case 16: loc = P.pierRamp.xy(); break;
                case 17: loc = defStart("sawgrass_tours"); break;
                default: loc = defStart("range"); break;
            }
            addMessage(g, t.from, t.text, -1, false, -1, &loc, 0, nullptr);
            g.notify(t.from, t.text);
            gEco.callCooldown = 30.f;
            return;
        }
    }
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
        total += (long long)businessIncome(g, gShops.businesses[i].income) * days;
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
        if (owned) it.right = StrFormat("+$%d/day", businessIncome(g, b.income));
        else if (avail) it.price = b.price;
        else it.right = "LOCKED";
        it.enabled = owned || avail;
        it.detail = std::string(b.desc) + StrFormat(" Income $%d a day.", businessIncome(g, b.income));
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
        std::string rival = rivalStatus(g, d.id);
        if (!rival.empty()) it.detail = rival;
        if (strcmp(d.id, "wishlist") == 0) {
            int lv = flag(g, EX_WISHLIST_LEVEL), n = (int)ARRAY_COUNT(kWishlist);
            it.detail = StrFormat("Order %d of %d: a %s. Select to mark Rook's garage.", lv % n + 1, n, wishClassName(kWishlist[lv % n].cls));
        } else if (strcmp(d.id, "series") == 0) {
            int titles = flag(g, EX_SERIES_WINS);
            it.detail = titles > 0 ? StrFormat("Night Series titles: %d. Three legs, points 10/6/3/1; the champion takes $12,000.", titles)
                                   : std::string("Three legs in Lalo's cars, points 10/6/3/1. The champion takes $20,000 and the Arclight.");
        } else if (strcmp(d.id, "harbor") == 0) {
            int lv = flag(g, EX_HARBOR_LEVEL);
            const HarborRun& r = kHarborRuns[lv % 5];
            it.detail = StrFormat("Next run: %s to %s. %s", r.cargo, r.dest, flag(g, SIDE_HARBOR_ALL) ? "The yard's debt is paid." : StrFormat("Run %d of 5 toward the yard's debt.", lv % 5 + 1).c_str());
        } else if (strcmp(d.id, "bounty") == 0) {
            int lv = flag(g, EX_BOUNTY_LEVEL), n = (int)ARRAY_COUNT(kFugitives);
            it.detail = StrFormat("Next skip: %s, who %s. Select to mark the office.", kFugitives[lv % n].name, kFugitives[lv % n].crime);
        }
        size_t idLen = strlen(d.id);
        if (d.letter == '?' && idLen > 2 && d.id[idLen - 2] == '_' && d.id[idLen - 1] >= '1' && d.id[idLen - 1] <= '9')   // strangers: rosa_1 ...
            it.detail = StrFormat("%s's story, part %c of 3. %s", d.contact, d.id[idLen - 1],
                                  d.setsFlag >= 0 && flag(g, d.setsFlag) ? "Done." : "Select to mark it on the map.");
        items.push_back(it);
    }
    MenuItem j;
    j.label = "Taxi / Vigilante / Paramedic";
    j.detail = "Get into a taxi, police car or ambulance and press G (R3 on a controller) to start the job.";
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
        int best = d.setsFlag >= 0 && d.setsFlag < 32 ? flag(g, EX_STORY_BEST + d.setsFlag) : 0;
        if (best > 0) it.detail += StrFormat(" Best time %d:%02d.", best / 600, (best / 10) % 60);
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

// The phone key (Up / D-pad up) opens the handset; the app draws it and routes its actions to phoneHandle(). While
// wanted and empty-handed on foot, holding the key surrenders (police.cpp), so the phone opens on a tap's release.
void updatePhoneKey(GameWorld& g) {
    static double pressT = -1.0;
    if (g.ctl.phone.pressed) pressT = g.time;
    Ped* pl = g.playerPed();
    if (!pl || g.phone.open || gMenu.open) return;
    int pv = g.playerVehicle();
    bool vehicleOk = pv < 0 || !g.isAircraft(pv);
    bool surrenderable = g.pinfo.wanted > 0 && pl->state == PS_ONFOOT && pl->weapon == WPN_FISTS && g.phone.call == UI::CALL_NONE;
    bool open = surrenderable ? g.ctl.phone.released && pressT >= 0.0 && g.time - pressT < 0.35 : g.ctl.phone.pressed;
    if (open && g.playerControl && !g.mInCutscene() && vehicleOk && pl->health > 0.f && g.pinfo.deathTimer <= 0.f) g.phone.open = true;
}

void economyUpdate(GameWorld& g, float dt) {
    updateIncome(g);
    updatePhoneKey(g);
    updateStoryCalls(g, dt);
}

}  // namespace mu
}  // namespace Game
