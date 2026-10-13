// Tidegram: the in-game social feed shown by the phone. NPC accounts (locals, tourists, news, traffic, police, radio,
// food and fitness accounts, a resident conspiracy theorist) post text generated from templates with random
// alternatives, reacting a little later to what the game reports with tidegramReport(); ambient chatter follows the
// time of day and weather. Player photos taken with the phone are posted too and collect likes.
#include "ui_internal.h"

namespace UI {
namespace tide_detail {

using namespace uix;

enum PersonaKind : u8 {
    PK_LOCAL = 0, PK_TOURIST, PK_NEWS, PK_TRAFFIC, PK_POLICE, PK_WEATHER, PK_RADIO, PK_CONSPIRACY, PK_FOODIE, PK_FITNESS,
    PK_INFLUENCER, PK_PLAYER, PK_COUNT
};

struct Persona {
    std::string name, handle;
    PersonaKind kind = PK_LOCAL;
    u32 color = 0, color2 = 0;
    bool verified = false;
    int followers = 100;
};

struct Pending {
    double due = 0.0;
    TideEvent ev = TE_WANTED;
    std::string district, street, subject;
    float magnitude = 0.f;
    int tpl = -1;            // template index in the event table
};

struct Tpl {
    PersonaKind kind;
    u8 image;
    const char* text;
};

// Post images (procedural thumbnails drawn by the phone)
enum : u8 { IMG_NONE = 0, IMG_SUNSET, IMG_NIGHT, IMG_POLICE, IMG_BEACH, IMG_CAR, IMG_SMOKE, IMG_FOOD, IMG_RAIN, IMG_PALMS, IMG_DAY };

// Tokens: {D} district, {S} street, {V} subject, {N} number, {C} city, {H} district hashtag, {T} time of day phrase,
// {P} first name, {R} radio station. [a|b|c] picks one alternative.
const Tpl kWanted[] = {
    {PK_LOCAL, IMG_POLICE, "[Five|Six|Like ten] police cars just flew down {S} [with the sirens going|doing about a hundred|like it's a racetrack]. What did I miss? {H}"},
    {PK_LOCAL, IMG_NONE, "Helicopter circling over {D} again. [Guess nobody is sleeping tonight.|I can't hear my own TV.|Somebody is having a very bad day.]"},
    {PK_LOCAL, IMG_NONE, "Me, trying to cross {S} [for a coffee|to get to work|with my groceries]. The entire police department: [no.|absolutely not.|not today.]"},
    {PK_NEWS, IMG_POLICE, "[DEVELOPING|BREAKING]: Police [pursuit|operation] underway in {D}. Drivers are urged to avoid {S}."},
    {PK_POLICE, IMG_NONE, "Officers are responding to an incident in the {D} area. Please avoid {S} and follow instructions from officers on scene."},
    {PK_CONSPIRACY, IMG_NONE, "They call it a 'routine pursuit' in {D}. Routine for WHO? [Wake up|Open your eyes], {C}."},
    {PK_TOURIST, IMG_POLICE, "Is it [always|normally] this exciting in {C}?? Police everywhere on {S}. [My mom is going to freak.|Best vacation ever??]"},
    {PK_TRAFFIC, IMG_NONE, "Police activity on {S} in {D}. [Expect closures.|Seek alternate routes.|Delays likely.]"},
};
const Tpl kWantedHigh[] = {
    {PK_LOCAL, IMG_POLICE, "[Armored trucks|SWAT vans|Actual military-looking trucks] on {S}?? This is {C}, not a movie set."},
    {PK_NEWS, IMG_POLICE, "[BREAKING|UPDATE]: Heavy police presence in {D}. Multiple units and air support involved. Residents told to stay indoors."},
    {PK_CONSPIRACY, IMG_NONE, "Count the helicopters over {D}. Now ask yourself who's paying for them. [I'll wait.|Think about it.]"},
};
const Tpl kEscaped[] = {
    {PK_LOCAL, IMG_NONE, "The cops just [gave up and|turned around and] drove off. [Whoever that was: impressive.|Budget cuts, I guess.|Never seen anyone lose them like that.]"},
    {PK_NEWS, IMG_NONE, "Suspect escapes police in {D} after a pursuit through {S}. [Investigation ongoing.|No arrests have been made.]"},
    {PK_CONSPIRACY, IMG_NONE, "Nobody 'escapes' in {C} unless someone LETS them escape. [Connect the dots.|Just saying.]"},
};
const Tpl kBusted[] = {
    {PK_LOCAL, IMG_POLICE, "Someone just got arrested outside [the laundromat|a taco truck|my building] on {S}. [Went quietly, respect.|Tried to talk their way out. Did not work.]"},
    {PK_POLICE, IMG_NONE, "One suspect in custody following an incident in {D}. Thank you for your patience."},
};
const Tpl kWasted[] = {
    {PK_LOCAL, IMG_NONE, "Ambulance on {S} [right now|just now]. [Hope they're okay.|Stay safe out there, {D}.]"},
    {PK_NEWS, IMG_NONE, "Paramedics respond to a serious incident in {D}. [More details as they come in.|The area is expected to reopen shortly.]"},
};
const Tpl kCarStolen[] = {
    {PK_LOCAL, IMG_CAR, "Somebody just [jacked|took] a {V} on {S} in broad daylight. [The owner was still holding their coffee.|Insurance is going to love this.|Just drove off like it was theirs.]"},
    {PK_LOCAL, IMG_NONE, "PSA: do NOT leave your {V} running in {D}. [Ask me how I know.|Learned that today.|Gone in about four seconds.]"},
    {PK_TOURIST, IMG_NONE, "Is it normal here for people to swap cars at red lights? Saw it [twice|three times] on {S}. {H}"},
    {PK_CONSPIRACY, IMG_NONE, "Every stolen {V} in {C} ends up at the same warehouse. [Nobody wants to talk about it.|Follow the tow trucks.]"},
};
const Tpl kCrash[] = {
    {PK_LOCAL, IMG_CAR, "Just watched someone [plow through|take out|redecorate] a [fence|bus stop|hot dog stand|palm tree] on {S} doing like {N}. [Walked away too?!|Still processing.]"},
    {PK_TRAFFIC, IMG_NONE, "Collision reported on {S} in {D}. [Expect delays.|Right lane blocked.|Emergency crews on scene.]"},
    {PK_LOCAL, IMG_NONE, "{S} drivers are a different species. [Just saw a car go sideways.|That's all. That's the post.]"},
};
const Tpl kExplosion[] = {
    {PK_LOCAL, IMG_SMOKE, "Was that a [BOMB|gas main|transformer]?? Huge boom in {D} just now. [My windows are shaking.|Everyone ok?]"},
    {PK_NEWS, IMG_SMOKE, "Fire crews respond to an explosion in {D}. [No word yet on injuries.|Smoke visible across {C}.]"},
    {PK_CONSPIRACY, IMG_NONE, "[Third|Fourth] 'gas leak' this month in {C}. Sure. SURE."},
};
const Tpl kShooting[] = {
    {PK_LOCAL, IMG_NONE, "[Heard|Pretty sure I heard] gunshots near {S}. Everyone in {D} [stay inside|be careful]."},
    {PK_POLICE, IMG_NONE, "Reports of shots fired in {D}. Officers en route. Please avoid the area."},
    {PK_NEWS, IMG_POLICE, "Police investigating reports of gunfire near {S} in {D}."},
};
const Tpl kStunt[] = {
    {PK_LOCAL, IMG_CAR, "Somebody just jumped a car [over|across] {S}. [I have questions. Mostly: how.|Physics called in sick today.|{N} meters. I paced it out.]"},
    {PK_LOCAL, IMG_NONE, "Real life has no respawn button, {D} driver. [Still. Ten out of ten.|Anyway, sick jump.]"},
    {PK_INFLUENCER, IMG_CAR, "Filming a sunset reel in {D} and a car FLIES through my shot. [Honestly better content.|Posting it anyway.]"},
};
const Tpl kSpeeding[] = {
    {PK_LOCAL, IMG_NONE, "Something just went past my window at {N} on {S}. [Car? Rocket? Unclear.|Pretty sure it broke the sound barrier.]"},
    {PK_TRAFFIC, IMG_NONE, "Reminder: the speed limit on {S} is not a suggestion. [Slow down, {C}.|Drive safe.]"},
};
const Tpl kMission[] = {
    {PK_NEWS, IMG_NONE, "Police investigating a string of [incidents|disturbances] in {D} [tonight|today]. Witnesses describe it as '[loud|chaotic|very loud]'."},
    {PK_LOCAL, IMG_NONE, "No idea what went down in {D} but my whole block is talking about it. {H}"},
    {PK_CONSPIRACY, IMG_NONE, "Mark my words: what happened in {D} today is connected to the Causeway thing. [Nobody listens.|Screenshot this.]"},
    {PK_RADIO, IMG_NONE, "Wild day in {C}, folks. [Stay tuned to {R}|Keep it locked to {R}] for the soundtrack to whatever this is."},
};
// subject: a vehicle or a property name (no article in the templates)
const Tpl kPurchase[] = {
    {PK_LOCAL, IMG_NONE, "Word on {S} is somebody just paid cash for {V}. [Must be nice.|Rent's going up again, isn't it.|What do they DO for a living?]"},
    {PK_LOCAL, IMG_NONE, "{V} has a new owner. [Loud parties incoming.|Hope they're friendlier than the last one.|Welcome to {D}, I guess.]"},
    {PK_INFLUENCER, IMG_DAY, "Saw someone buy {V} like it was a smoothie. {C} energy is unmatched."},
};
const Tpl kFlyby[] = {
    {PK_LOCAL, IMG_NONE, "Some maniac just flew a [plane|jet|chopper] [under the power lines|between the towers|right over the pool] in {D}. My hair is still moving."},
    {PK_POLICE, IMG_NONE, "Low-flying aircraft reported over {D}. Aviation authorities have been notified."},
    {PK_TOURIST, IMG_NONE, "Do planes always fly this low in {C}?? Nearly lost my hat on {S}."},
};
const Tpl kRace[] = {
    {PK_LOCAL, IMG_CAR, "Street race down {S} [last night|just now]. [The winner wasn't even close.|My neighbor called the cops. Twice.]"},
    {PK_TRAFFIC, IMG_NONE, "Reports of street racing in {D}. Racing on public roads is illegal and dangerous."},
};
const Tpl kWeather[] = {
    {PK_WEATHER, IMG_RAIN, "{V} moving over {C}. [Stay off the causeways if you can.|Expect ponding on low roads.] Keep an eye on the forecast."},
    {PK_LOCAL, IMG_RAIN, "{V} in {C}: [ninety seconds of chaos then sunshine again.|every road becomes a river.|the drivers somehow get worse.]"},
};

// Ambient chatter
const Tpl kMorning[] = {
    {PK_LOCAL, IMG_BEACH, "[Coffee|Cafecito] and a [walk|run] along the water before the heat kicks in. Best part of the day. {H}"},
    {PK_FITNESS, IMG_BEACH, "5am crew on the {D} boardwalk. [No excuses.|Hydrate.|You showed up. That's the win.]"},
    {PK_TRAFFIC, IMG_NONE, "Morning commute: heavy on {S} into {D}. [Allow extra time.|Brake lights as far as the eye can see.]"},
};
const Tpl kMidday[] = {
    {PK_LOCAL, IMG_NONE, "It's [so hot|a thousand degrees] in {D} that my [car seat|phone|sunglasses] [melted|filed a complaint]."},
    {PK_TOURIST, IMG_BEACH, "Sol Beach at [noon|three in the afternoon] is [70% sunscreen|pure chaos]. Loving it."},
    {PK_FOODIE, IMG_FOOD, "Best [empanadas|fish tacos|pressed sandwiches|mango smoothies] in {D} is the truck on {S}. [Fight me.|I will not be taking questions.]"},
};
const Tpl kSunset[] = {
    {PK_LOCAL, IMG_SUNSET, "Sunset from {D} [hits different tonight.|never gets old.|is free and still the best thing in {C}.] {H}"},
    {PK_INFLUENCER, IMG_SUNSET, "Golden hour in {D}. [No filter needed.|Obsessed.|This city, honestly.] #GoldenHour"},
    {PK_TOURIST, IMG_SUNSET, "Nobody told me the sunsets in {C} look like this. [Moving here.|Extending my trip.]"},
};
const Tpl kNight[] = {
    {PK_LOCAL, IMG_NIGHT, "[The neon on {S}|Bass from the clubs in {D}] at [2am|midnight] is a whole mood."},
    {PK_RADIO, IMG_NIGHT, "Late night on {R}: synth, bass and the windows down. Where are you driving tonight, {C}?"},
    {PK_LOCAL, IMG_NONE, "Why is there always one guy revving a motorcycle on {S} at [1am|3am]. [Every night.|Every. Single. Night.]"},
};
const Tpl kRain[] = {
    {PK_LOCAL, IMG_RAIN, "Rain in {C}: [ninety seconds of chaos then sunshine again.|every road becomes a river.]"},
    {PK_WEATHER, IMG_RAIN, "Showers over {D} this [afternoon|evening]. Roads will be slick. [Drive safe.|Give yourself extra time.]"},
};
const Tpl kAnytime[] = {
    {PK_TRAFFIC, IMG_NONE, "The Solano Causeway is a parking lot again. [Every. Single. Day.|Bring snacks.]"},
    {PK_LOCAL, IMG_NONE, "Rent in {D} is [up again|now more than my car]. [Cool cool cool.|Moving onto a boat.]"},
    {PK_CONSPIRACY, IMG_NONE, "The gators in Sawgrass are organizing. I've seen the signs. [Literally, they knocked one over.|Nobody believes me.]"},
    {PK_RADIO, IMG_NONE, "Now playing on {R}: Neon Palms - Midnight Causeway. Windows down, {C}."},
    {PK_TOURIST, IMG_PALMS, "First time in {C}! Where do locals actually eat? Not the place on {S} with the giant shrimp."},
    {PK_LOCAL, IMG_NONE, "LOST: orange cat named [Mango|Chorizo|Biscuit], last seen near {S} in {D}. Very friendly, very judgmental."},
    {PK_FITNESS, IMG_NONE, "Leg day on the {D} boardwalk. [Don't talk to me.|Pray for me.]"},
    {PK_FOODIE, IMG_FOOD, "Ranking every [taco|burger|smoothie] spot in {D}. Day [4|7|12]. [I regret nothing.|My doctor has concerns.]"},
    {PK_INFLUENCER, IMG_DAY, "Brunch in {D}. [Outfit on point, coffee on point, city on point.|Living my best life, as they say.]"},
    {PK_LOCAL, IMG_NONE, "Overheard on {S}: '[I'm not lost, I'm exploring.|That's not a gator, that's a log. ...Right?|Who parks a boat there?]'"},
    {PK_NEWS, IMG_DAY, "City council approves [new bike lanes|another luxury tower|a bigger fireworks budget] for {D}. [Residents divided.|Reactions mixed.]"},
};

// ---- state
std::vector<Persona> g_personas;
std::vector<TidePost> g_posts;        // newest first
std::vector<Pending> g_pending;
double g_clock = 0.0;
double g_nextAmbient = 0.0;
double g_lastReport[TE_COUNT];
int g_unread = 0;
int g_nextId = 1;
u32 g_rng = 0x9E3779B9u;
bool g_inited = false;
// context captured by the last tick (used when reports arrive)
float g_timeOfDay = 12.f;
int g_weather = 0;
std::string g_owner = "you";

u32 rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
float rndf() { return (rnd() & 0xffffff) / 16777216.f; }
int rndi(int n) { return n > 0 ? (int)(rnd() % (u32)n) : 0; }

const char* const kFirst[] = {"Yesi", "Marco", "Tash", "Andre", "Keke", "Bruno", "Lila", "Omar", "Priya", "Jojo", "Rene", "Dani",
                              "Nico", "Ari", "Mika", "Tavo", "Luz", "Chayo", "Benny", "Isa", "Gabe", "Maya", "Tomi", "Rosa",
                              "Dev", "Kenji", "Amara", "Wes", "Paz", "Ivy", "Rafi", "Nell", "Cruz", "Lupe", "Theo", "Zuri"};
const char* const kLast[] = {"Ramos", "Okafor", "Nguyen", "Brooks", "Silva", "Moreau", "Kaplan", "Reyes", "Haddad", "Park",
                             "Duarte", "Fontaine", "Osei", "Vega", "Lindqvist", "Batista", "Chen", "Alvarez"};
const char* const kHandleWords[] = {"sunsets", "vibes", "inpalmera", "bayside", "waves", "daily", "rides", "sol", "keys", "eats",
                                    "shots", "wanders", "nights", "dreams"};

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return o;
}
std::string noSpaces(const std::string& s) {
    std::string o;
    for (char c : s)
        if (c != ' ' && c != '\'' && c != '.' && c != '-') o.push_back(c);
    return o;
}

// avatar gradients (top, bottom)
const float kAvatar[8][6] = {
    {1.00f, 0.36f, 0.62f, 0.62f, 0.16f, 0.66f}, {1.00f, 0.66f, 0.26f, 0.86f, 0.30f, 0.14f}, {0.30f, 0.86f, 0.62f, 0.10f, 0.50f, 0.42f},
    {0.46f, 0.52f, 1.00f, 0.30f, 0.20f, 0.72f}, {0.25f, 0.80f, 1.00f, 0.10f, 0.42f, 0.86f}, {0.98f, 0.80f, 0.30f, 0.80f, 0.40f, 0.10f},
    {0.95f, 0.45f, 0.45f, 0.62f, 0.14f, 0.34f}, {0.62f, 0.92f, 0.36f, 0.20f, 0.60f, 0.30f},
};

void addPersona(const char* name, const char* handle, PersonaKind k, bool verified, int followers) {
    Persona p;
    p.name = name;
    p.handle = handle;
    p.kind = k;
    p.verified = verified;
    p.followers = followers;
    int ci = (int)(g_personas.size() * 5 + 3) % 8;
    p.color = rgba(kAvatar[ci][0], kAvatar[ci][1], kAvatar[ci][2]);
    p.color2 = rgba(kAvatar[ci][3], kAvatar[ci][4], kAvatar[ci][5]);
    g_personas.push_back(p);
}

void initPersonas() {
    g_personas.clear();
    addPersona("Porto Sol Herald", "psherald", PK_NEWS, true, 412000);
    addPersona("Channel 6 Palmera", "ch6palmera", PK_NEWS, true, 890000);
    addPersona("Palmera Traffic", "palmeratraffic", PK_TRAFFIC, true, 96000);
    addPersona("Porto Sol PD", "portosolpd", PK_POLICE, true, 231000);
    addPersona("Palmera Weather Desk", "wxpalmera", PK_WEATHER, true, 150000);
    addPersona("Tide FM", "tidefm", PK_RADIO, true, 510000);
    addPersona("Truth Seeker", "gatorsknow", PK_CONSPIRACY, false, 2100);
    addPersona("Beto Eats", "betoeats", PK_FOODIE, false, 38000);
    addPersona("Coach Nia", "coachnia", PK_FITNESS, true, 120000);
    addPersona("Lani Luxe", "lanilux", PK_INFLUENCER, true, 1400000);
    // distinct first names (shuffled pool)
    std::vector<int> order((int)ARRAY_COUNT(kFirst));
    for (int i = 0; i < (int)order.size(); i++) order[i] = i;
    for (int i = (int)order.size() - 1; i > 0; i--) std::swap(order[i], order[rndi(i + 1)]);
    for (int i = 0; i < 22; i++) {
        bool tourist = i % 4 == 3;
        std::string first = kFirst[order[i % (int)order.size()]];
        std::string last = kLast[rndi((int)ARRAY_COUNT(kLast))];
        std::string handle;
        int pat = rndi(4);
        if (tourist) handle = pat < 2 ? lower(first) + "travels" : "wanderlust_" + lower(first);
        else if (pat == 0) handle = lower(first) + lower(last) + std::to_string(10 + rndi(89));
        else if (pat == 1) handle = lower(first) + "_" + kHandleWords[rndi((int)ARRAY_COUNT(kHandleWords))];
        else if (pat == 2) handle = lower(first) + "." + lower(last);
        else handle = "its" + lower(first) + std::to_string(rndi(10));
        std::string name = pat == 3 ? first : first + " " + last;
        addPersona(name.c_str(), handle.c_str(), tourist ? PK_TOURIST : PK_LOCAL, false, 80 + rndi(9000));
    }
}

int personaOfKind(PersonaKind k) {
    int count = 0;
    for (auto& p : g_personas) count += p.kind == k;
    if (!count) return rndi((int)g_personas.size());
    int pick = rndi(count);
    for (int i = 0; i < (int)g_personas.size(); i++)
        if (g_personas[i].kind == k && pick-- == 0) return i;
    return 0;
}

std::string randomDistrict() {
    const std::vector<MapLabel>& labels = mapLabels();
    std::vector<const MapLabel*> land;
    for (const MapLabel& l : labels)
        if (!l.water && l.importance < 4.f) land.push_back(&l);
    if (land.empty()) return "Porto Sol";
    return land[rndi((int)land.size())]->name;
}

std::string hashtag(const std::string& district) {
    std::string h = "#";
    bool up = true;
    for (char c : district) {
        if (c == ' ' || c == '-' || c == '\'' || c == '.') { up = true; continue; }
        if (up && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        h.push_back(c);
        up = false;
    }
    return h.size() > 1 ? h : std::string("#PortoSol");
}

std::string timePhrase(float tod) {
    if (tod < 5.f || tod >= 21.f) return "tonight";
    if (tod < 12.f) return "this morning";
    if (tod < 17.5f) return "this afternoon";
    return "this evening";
}

struct Ctx {
    std::string district, street, subject;
    float number = 0.f;
};

std::string expand(const char* tpl, const Ctx& c) {
    std::string out;
    for (const char* p = tpl; *p; p++) {
        if (*p == '[') {
            const char* e = strchr(p, ']');
            if (!e) break;
            std::vector<std::string> alts;
            std::string cur;
            for (const char* q = p + 1; q < e; q++) {
                if (*q == '|') { alts.push_back(cur); cur.clear(); }
                else cur.push_back(*q);
            }
            alts.push_back(cur);
            out += expand(alts[rndi((int)alts.size())].c_str(), c);
            p = e;
        } else if (*p == '{' && p[1] && p[2] == '}') {
            switch (p[1]) {
                case 'D': out += c.district; break;
                case 'S': out += c.street; break;
                case 'V': out += c.subject; break;
                case 'N': out += std::to_string((int)(c.number + 0.5f)); break;
                case 'C': out += "Porto Sol"; break;
                case 'H': out += hashtag(c.district); break;
                case 'T': out += timePhrase(g_timeOfDay); break;
                case 'P': out += kFirst[rndi((int)ARRAY_COUNT(kFirst))]; break;
                case 'R': out += "Tide FM"; break;
                default: break;
            }
            p += 2;
        } else out.push_back(*p);
    }
    return out;
}

bool wordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// Colors hashtags and mentions with the rich text codes the phone renders
std::string colorize(const std::string& s) {
    std::string o;
    size_t i = 0;
    while (i < s.size()) {
        char ch = s[i];
        bool start = (ch == '#' || ch == '@') && i + 1 < s.size() && wordChar(s[i + 1]) && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '(');
        if (start) {
            size_t j = i + 1;
            while (j < s.size() && wordChar(s[j])) j++;
            o += "~c~" + s.substr(i, j - i) + "~s~";
            i = j;
        } else {
            o.push_back(ch);
            i++;
        }
    }
    return o;
}

void addPost(int personaIdx, const std::string& text, int image, float heat, double time) {
    const Persona& pr = g_personas[Clamp(personaIdx, 0, (int)g_personas.size() - 1)];
    TidePost p;
    p.id = g_nextId++;
    p.author = pr.name;
    p.handle = pr.handle;
    p.text = colorize(text);
    p.color = pr.color;
    p.color2 = pr.color2;
    p.verified = pr.verified;
    p.player = pr.kind == PK_PLAYER;
    p.time = time;
    p.image = image;
    p.seed = rnd();
    // engagement target: a small share of the followers, more for hot topics
    float share = 0.004f + rndf() * 0.02f;
    p.likeTarget = Max(2.f, pr.followers * share * (0.6f + heat) + rndf() * 30.f);
    p.likes = 0;
    g_posts.insert(g_posts.begin(), p);
    if (g_posts.size() > 48) g_posts.pop_back();
    g_unread++;
}

const Tpl* eventTable(TideEvent ev, float mag, int& n) {
    switch (ev) {
        case TE_WANTED:
            if (mag >= 4.f) { n = (int)ARRAY_COUNT(kWantedHigh); return kWantedHigh; }
            n = (int)ARRAY_COUNT(kWanted);
            return kWanted;
        case TE_ESCAPED: n = (int)ARRAY_COUNT(kEscaped); return kEscaped;
        case TE_BUSTED: n = (int)ARRAY_COUNT(kBusted); return kBusted;
        case TE_WASTED: n = (int)ARRAY_COUNT(kWasted); return kWasted;
        case TE_CAR_STOLEN: n = (int)ARRAY_COUNT(kCarStolen); return kCarStolen;
        case TE_CRASH: n = (int)ARRAY_COUNT(kCrash); return kCrash;
        case TE_EXPLOSION: n = (int)ARRAY_COUNT(kExplosion); return kExplosion;
        case TE_SHOOTING: n = (int)ARRAY_COUNT(kShooting); return kShooting;
        case TE_STUNT_JUMP: n = (int)ARRAY_COUNT(kStunt); return kStunt;
        case TE_SPEEDING: n = (int)ARRAY_COUNT(kSpeeding); return kSpeeding;
        case TE_MISSION_PASSED: n = (int)ARRAY_COUNT(kMission); return kMission;
        case TE_PURCHASE: n = (int)ARRAY_COUNT(kPurchase); return kPurchase;
        case TE_LOW_FLYBY: n = (int)ARRAY_COUNT(kFlyby); return kFlyby;
        case TE_RACE_WON: n = (int)ARRAY_COUNT(kRace); return kRace;
        case TE_WEATHER: n = (int)ARRAY_COUNT(kWeather); return kWeather;
        default: n = 0; return nullptr;
    }
}

float eventCooldown(TideEvent ev) {
    switch (ev) {
        case TE_WANTED: return 75.f;
        case TE_SPEEDING: return 120.f;
        case TE_CRASH: return 60.f;
        case TE_SHOOTING: return 60.f;
        case TE_WEATHER: return 300.f;
        default: return 40.f;
    }
}

void postAmbient(double time) {
    const Tpl* table = kAnytime;
    int n = (int)ARRAY_COUNT(kAnytime);
    float tod = g_timeOfDay;
    float r = rndf();
    if (g_weather >= 2 && r < 0.45f) { table = kRain; n = (int)ARRAY_COUNT(kRain); }
    else if (r < 0.55f) {
        if (tod >= 5.5f && tod < 10.f) { table = kMorning; n = (int)ARRAY_COUNT(kMorning); }
        else if (tod >= 11.f && tod < 16.5f && g_weather < 2) { table = kMidday; n = (int)ARRAY_COUNT(kMidday); }
        else if (tod >= 17.5f && tod < 20.3f && g_weather < 2) { table = kSunset; n = (int)ARRAY_COUNT(kSunset); }
        else if (tod >= 21.f || tod < 4.f) { table = kNight; n = (int)ARRAY_COUNT(kNight); }
    }
    const Tpl& t = table[rndi(n)];
    Ctx c;
    c.district = randomDistrict();
    // a real street of that district when the map knows one near its label
    for (const MapLabel& l : mapLabels())
        if (l.name == c.district) {
            c.street = mapStreetAt(l.pos + vec2(rndf() * 300.f - 150.f, rndf() * 300.f - 150.f), 400.f);
            break;
        }
    if (c.street.empty()) c.street = "Ocean Drive";
    addPost(personaOfKind(t.kind), expand(t.text, c), t.image, 0.f, time);
}

void seedFeed() {
    // a few older posts so the feed is never empty
    int n = 7;
    for (int i = n; i >= 1; i--) postAmbient(g_clock - (double)i * (420.0 + rndf() * 900.0));
    std::stable_sort(g_posts.begin(), g_posts.end(), [](const TidePost& a, const TidePost& b) { return a.time > b.time; });
    for (TidePost& p : g_posts) p.likes = (int)p.likeTarget;
    g_unread = 0;
}

void ensureInit() {
    if (g_inited) return;
    g_inited = true;
    g_rng ^= (u32)(TimeSeconds() * 1000.0) * 2654435761u;
    if (!g_rng) g_rng = 0x1234567u;
    for (double& t : g_lastReport) t = -1e9;
    initPersonas();
    seedFeed();
    g_nextAmbient = g_clock + 40.0 + rndf() * 40.0;
}

void report(TideEvent ev, vec2 pos, const char* subject, float magnitude) {
    if ((int)ev >= (int)TE_COUNT) return;
    ensureInit();
    if (g_clock - g_lastReport[ev] < eventCooldown(ev)) {
        // escalations of the wanted level still get news coverage
        if (!(ev == TE_WANTED && magnitude >= 4.f && g_clock - g_lastReport[ev] > 20.0)) return;
    }
    g_lastReport[ev] = g_clock;
    int n = 0;
    const Tpl* table = eventTable(ev, magnitude, n);
    if (!table || n == 0) return;
    Pending base;
    base.ev = ev;
    base.magnitude = magnitude;
    base.subject = subject ? subject : "";
    if (ev == TE_WEATHER && base.subject.empty()) base.subject = "Rain";
    if (ev == TE_WEATHER && base.subject[0] >= 'a' && base.subject[0] <= 'z') base.subject[0] = (char)(base.subject[0] - 'a' + 'A');
    if (ev == TE_CAR_STOLEN && base.subject.empty()) base.subject = "car";
    if (ev == TE_PURCHASE && base.subject.empty()) base.subject = "new ride";
    base.district = mapReady() ? mapDistrictAt(pos) : std::string();
    if (base.district.empty()) base.district = "Porto Sol";
    base.street = mapReady() ? mapStreetAt(pos, 250.f) : std::string();
    if (base.street.empty()) base.street = "Ocean Drive";
    // 1-3 posts from different templates, staggered over the next half minute
    int posts = 1 + (magnitude >= 3.f || ev == TE_EXPLOSION || ev == TE_MISSION_PASSED ? 2 : rndi(2));
    posts = Min(posts, n);
    int used[3] = {-1, -1, -1};
    for (int k = 0; k < posts; k++) {
        int t = rndi(n);
        for (int tries = 0; tries < 8; tries++) {
            bool dup = false;
            for (int u = 0; u < k; u++) dup |= used[u] == t;
            if (!dup) break;
            t = (t + 1) % n;
        }
        used[k] = t;
        Pending p = base;
        p.tpl = t;
        p.due = g_clock + 3.0 + k * (6.0 + rndf() * 10.0) + rndf() * 6.0;
        g_pending.push_back(p);
    }
}

void tick(float dt, float timeOfDay, int weather, const std::string& owner) {
    ensureInit();
    g_timeOfDay = timeOfDay;
    g_weather = weather;
    if (!owner.empty()) g_owner = owner;
    g_clock += Clamp(dt, 0.f, 0.25f);
    for (size_t i = 0; i < g_pending.size();) {
        Pending& p = g_pending[i];
        if (p.due <= g_clock) {
            int n = 0;
            const Tpl* table = eventTable(p.ev, p.magnitude, n);
            if (table && p.tpl >= 0 && p.tpl < n) {
                const Tpl& t = table[p.tpl];
                Ctx c;
                c.district = p.district;
                c.street = p.street;
                c.subject = p.subject;
                c.number = p.magnitude;
                float heat = Min(2.f, 0.3f + p.magnitude * 0.25f + (p.ev == TE_EXPLOSION || p.ev == TE_STUNT_JUMP ? 0.8f : 0.f));
                addPost(personaOfKind(t.kind), expand(t.text, c), t.image, heat, g_clock);
            }
            g_pending.erase(g_pending.begin() + (long)i);
        } else i++;
    }
    if (g_clock >= g_nextAmbient) {
        postAmbient(g_clock);
        g_nextAmbient = g_clock + 55.0 + rndf() * 70.0;
    }
    // engagement grows toward its target (fast at first, then slower)
    for (TidePost& p : g_posts) {
        float age = (float)(g_clock - p.time);
        float target = p.likeTarget * (1.f - expf(-Max(age, 0.f) / 55.f));
        if ((float)p.likes < target) p.likes = (int)target;
    }
}

bool toggleLike(int postId) {
    for (TidePost& p : g_posts)
        if (p.id == postId) {
            p.liked = !p.liked;
            p.likes += p.liked ? 1 : -1;
            return p.liked;
        }
    return false;
}

void postPlayerPhoto(int snapshotId, int filter, vec2 pos, float timeOfDay) {
    ensureInit();
    // the player persona is created once and follows the current protagonist's name
    int idx = -1;
    for (int i = 0; i < (int)g_personas.size(); i++)
        if (g_personas[i].kind == PK_PLAYER) idx = i;
    if (idx < 0) {
        Persona me;
        me.kind = PK_PLAYER;
        me.followers = 2400;
        me.color = rgba(1.f, 0.3f, 0.66f);
        me.color2 = rgba(1.f, 0.72f, 0.2f);
        g_personas.push_back(me);
        idx = (int)g_personas.size() - 1;
    }
    g_personas[idx].name = g_owner;
    g_personas[idx].handle = noSpaces(lower(g_owner)) + "_ps";
    Ctx c;
    c.district = mapReady() ? mapDistrictAt(pos) : std::string();
    if (c.district.empty()) c.district = "Porto Sol";
    const char* light = timeOfDay >= 17.5f && timeOfDay < 20.3f ? "[Golden hour|Sunset|Last light]"
                        : (timeOfDay >= 20.3f || timeOfDay < 5.5f) ? "[Night shift|After dark|Neon hours]"
                        : timeOfDay < 10.f ? "[Early light|Morning|Sunrise walk]" : "[Afternoon|Sun's out|Daylight]";
    std::string caption = expand((std::string(light) + " in {D}. ").c_str(), c);
    const std::vector<std::string>& names = Phone::filterNames();
    if (filter > 0 && filter < (int)names.size()) caption += "#" + noSpaces(names[filter]) + " ";
    else caption += "#NoFilter ";
    caption += hashtag(c.district);
    addPost(idx, caption, 0, 1.2f, g_clock);
    TidePost& p = g_posts.front();
    p.snapshot = snapshotId;
    p.image = -1;
    p.likeTarget = 180.f + rndf() * 900.f;
    g_unread = Max(0, g_unread - 1);
}

}  // namespace tide_detail

void tidegramReport(TideEvent ev, vec2 pos, const char* subject, float magnitude) { tide_detail::report(ev, pos, subject, magnitude); }
int tidegramUnread() { return tide_detail::g_unread; }

namespace uix {
void tideTick(float dt, float timeOfDay, int weather, const std::string& owner) { tide_detail::tick(dt, timeOfDay, weather, owner); }
const std::vector<TidePost>& tidePosts() {
    tide_detail::ensureInit();
    return tide_detail::g_posts;
}
double tideClock() { return tide_detail::g_clock; }
void tideMarkSeen() { tide_detail::g_unread = 0; }
bool tideToggleLike(int postId) { return tide_detail::toggleLike(postId); }
void tidePostPlayerPhoto(int snapshotId, int filter, vec2 pos, float timeOfDay) {
    tide_detail::postPlayerPhoto(snapshotId, filter, pos, timeOfDay);
}
}  // namespace uix
}  // namespace UI
