// Radio: nine stations with persistent "live" timelines (songs from a generated catalog, DJ breaks,
// song talk-overs, commercial blocks, talk-show segments), original DJ/ad/talk scripts spoken with
// Speech::synthesize, broadcast processing, the speech worker thread, and the music producer thread
// that renders stations and the adaptive score ahead into ring buffers.
#include "audio_internal.h"

namespace Audio {
namespace detail {

std::atomic<i64> g_radioFrames{0};

namespace radio {

using namespace dsp;
using music::Genre;

// =============================================================================================
// Voices
static VoiceParams vp(float pitch, float formant, float speed, float breath, float rough, float expr) {
    VoiceParams v;
    v.pitch = pitch;
    v.formantScale = formant;
    v.speed = speed;
    v.breathiness = breath;
    v.roughness = rough;
    v.expressiveness = expr;
    return v;
}

enum AdVoice { AV_ANNOUNCER = 0, AV_ANNOUNCER_F, AV_MAN, AV_WOMAN, AV_FAST, AV_OLDMAN, AV_KID, AV_HYPE, AV_COUNT };
static VoiceParams adVoice(int kind, u32 seed) {
    Rng r(seed, 3);
    switch (kind) {
        case AV_ANNOUNCER: return vp(r.range(86.f, 96.f), 0.95f, 0.97f, 0.12f, 0.05f, 1.25f);
        case AV_ANNOUNCER_F: return vp(r.range(195.f, 215.f), 1.14f, 1.02f, 0.12f, 0.f, 1.3f);
        case AV_MAN: return vp(r.range(105.f, 128.f), r.range(0.97f, 1.03f), r.range(0.95f, 1.08f), 0.1f, r.range(0.f, 0.2f), 1.1f);
        case AV_WOMAN: return vp(r.range(185.f, 230.f), r.range(1.12f, 1.18f), r.range(0.95f, 1.1f), 0.15f, 0.f, 1.2f);
        case AV_FAST: return vp(r.range(118.f, 132.f), 1.02f, 1.38f, 0.08f, 0.1f, 1.4f);
        case AV_OLDMAN: return vp(r.range(95.f, 108.f), 0.96f, 0.85f, 0.25f, 0.45f, 1.f);
        case AV_KID: return vp(r.range(270.f, 310.f), 1.3f, 1.08f, 0.1f, 0.f, 1.5f);
        default: return vp(r.range(120.f, 135.f), 1.02f, 1.18f, 0.08f, 0.15f, 1.7f);
    }
}

// =============================================================================================
// Stations
struct StationDef {
    const char* name;
    const char* spoken;  // how the DJ says the station
    const char* genreName;
    Genre genre;
    const char* dj;
    VoiceParams djVoice;
    const char* ids[5];
    const char* banter[6];
    float introChance;  // chance the DJ talks over a song intro
    bool am;
};

static StationDef makeDefs(int i) {
    StationDef d = {};
    switch (i) {
        case 0:
            d = {"Neon Coast 88.1", "Neon Coast, eighty eight point one", "Synthwave / Retro 80s", Genre::Synthwave, "Vex Halloran",
                 vp(96.f, 0.96f, 0.93f, 0.28f, 0.05f, 0.9f),
                 {"You are cruising with Neon Coast, eighty eight point one. The eighties never ended. They just moved to Palmera.",
                  "Neon Coast, eighty eight point one. Chrome, sunsets, and synthesizers.",
                  "This is Vex Halloran on Neon Coast. Keep the windows down and the bass up.",
                  "Neon Coast, eighty eight point one. Sounds best at night, on an empty highway, going nowhere in particular.",
                  "Porto Sol after dark belongs to Neon Coast. Eighty eight point one."},
                 {"Somebody called the station asking if we play anything recorded after nineteen eighty nine. I hung up on them. Gently.",
                  "If your car does not have a cassette deck, please pull over and reflect on your life choices.",
                  "The neon on Ocean Drive is buzzing tonight. So is my coffee. Let's keep moving.",
                  "Remember, sunglasses at night are not a fashion choice. They are a lifestyle.",
                  "A listener from Sol Beach says our music makes her feel like she is in a montage. You are, darling. You are.",
                  "Every palm tree in Palmera looks better in purple light. Science has not confirmed this. I have."},
                 0.55f, false};
            break;
        case 1:
            d = {"The Heat 97.3", "The Heat, ninety seven point three", "Hip-Hop / Trap", Genre::HipHop, "Big Ray Salazar",
                 vp(112.f, 1.f, 1.1f, 0.1f, 0.15f, 1.5f),
                 {"The Heat, ninety seven point three. Porto Sol's hottest station. No air conditioning.",
                  "Big Ray on The Heat. If the bass is not rattling your license plate, turn it up.",
                  "Ninety seven three, The Heat. Southside to Sol Beach, we run the city.",
                  "You're locked in to The Heat. Humidity ninety percent. Vibes one hundred.",
                  "The Heat, ninety seven point three. Certified loud since day one."},
                 {"Shout out to everybody stuck on the causeway right now. Y'all got the best seats in the house for this one.",
                  "Somebody at the car wash on Flagler Avenue just said Big Ray is overrated. Bro, I can hear you. I'm everywhere.",
                  "The city says the new speed bumps are for safety. I say they are for bass testing.",
                  "If your grandma is in the car, apologize now. Actually no, grandma loves this one.",
                  "I got a text from a listener in Fort Castell. It just says, run it back. Nah. We don't run it back. We move forward.",
                  "Real talk, hydrate out there. Porto Sol heat is no joke. Drink water, then turn this up."},
                 0.6f, false};
            break;
        case 2:
            d = {"Radio Calor 101.5", "Radio Calor, ciento uno punto cinco", "Reggaeton / Latin Pop", Genre::Reggaeton, "Marisol Vega",
                 vp(212.f, 1.15f, 1.08f, 0.12f, 0.f, 1.5f),
                 {"Radio Calor, one oh one point five. Caliente, caliente, all day long.",
                  "Marisol Vega on Radio Calor. Move your hips, not your lane.",
                  "Radio Calor, the sound of summer in Porto Sol. Every single day is summer here.",
                  "You are listening to Radio Calor, one oh one five. Put the phone down and dance.",
                  "Radio Calor. From Little Havana Heights to the Coral Keys, we keep it hot."},
                 {"My mother called the station again to say I talk too fast. Mami, I love you. This next one is not for you.",
                  "The beach is packed, the ocean is warm, and the parking tickets are expensive. Walk, amigos.",
                  "If you are driving on the Sol Beach causeway right now, wave at the pelicans. They are judging you.",
                  "Somebody asked if reggaeton is just one song. Yes. It is one beautiful song, and it never ends.",
                  "Tonight at the Malecon the party starts at ten and ends when the police arrive. Be responsible.",
                  "Hydrate, dance, repeat. That is the Radio Calor lifestyle."},
                 0.6f, false};
            break;
        case 3:
            d = {"Pulse FM 104.8", "Pulse F M, one oh four point eight", "House / EDM", Genre::House, "DJ Kilowatt",
                 vp(124.f, 1.02f, 1.12f, 0.1f, 0.05f, 1.6f),
                 {"Pulse F M, one oh four point eight. Non stop from sunset to sunrise.",
                  "This is DJ Kilowatt on Pulse. Feel that kick drum in your chest? That is the station working.",
                  "Pulse F M. Four on the floor, four hundred percent humidity.",
                  "One oh four point eight, Pulse. The only station that sweats.",
                  "Pulse F M. If you can hear this, you are already on the dance floor."},
                 {"The warehouse party in the port district got shut down again. The fire marshal says the bass was a structural hazard. Respect.",
                  "Hands up if you are driving. Actually, no. Keep your hands on the wheel. Nod aggressively instead.",
                  "My doctor says my heart rate matches the tempo of this station. One hundred twenty four beats per minute. Healthy.",
                  "Sunrise at Sol Beach in two hours. Glow sticks recommended, sunscreen mandatory.",
                  "Remember, the drop is coming. It always comes. Trust the drop.",
                  "Pulse F M does not believe in silence. Silence is just a very long breakdown."},
                 0.5f, false};
            break;
        case 4:
            d = {"Riptide Rock 93.7", "Riptide Rock, ninety three seven", "Rock", Genre::Rock, "Duke Mahoney",
                 vp(90.f, 0.95f, 0.98f, 0.15f, 0.45f, 1.3f),
                 {"Riptide Rock, ninety three seven. Loud enough to scare the alligators.",
                  "Duke Mahoney, Riptide Rock. If it is not distorted, it is not dinner.",
                  "You are caught in the Riptide. Ninety three point seven. Do not swim against it.",
                  "Riptide Rock. Real guitars, real drums, real hearing damage.",
                  "Ninety three seven, Riptide Rock. Palmera's last line of defense against smooth jazz."},
                 {"I got pulled over last week for playing this station too loud. The officer asked me to turn it up. True story. Mostly.",
                  "My ears have been ringing since nineteen ninety four. It is the most beautiful note I know.",
                  "The Cypress Ridge biker rally is this weekend. Leave the leather at home, it is ninety five degrees.",
                  "To the guy in the minivan on the turnpike playing air drums. I see you, brother. Keep it steady.",
                  "Rock and roll is not dead. It just moved to Palmera for the tax benefits.",
                  "Somebody requested a ballad. We played it at double speed. You're welcome."},
                 0.55f, false};
            break;
        case 5:
            d = {"Blue Lagoon Lounge 90.9", "Blue Lagoon, ninety point nine", "Jazz Lounge", Genre::Jazz, "Lorraine Beaumont",
                 vp(172.f, 1.1f, 0.86f, 0.38f, 0.02f, 1.f),
                 {"Blue Lagoon, ninety point nine. Pour yourself something smooth.",
                  "Good evening, darlings. Lorraine Beaumont on Blue Lagoon.",
                  "Blue Lagoon Lounge. Where the ice cubes clink in time.",
                  "Ninety point nine, Blue Lagoon. Slow down. The ocean is not going anywhere.",
                  "You are listening to Blue Lagoon, the most relaxed ninety point nine megahertz in Palmera."},
                 {"The rain on the marina tonight sounds like a brush on a snare drum. Nature knows swing.",
                  "A gentleman once told me jazz is just wrong notes played on purpose. I married him. Briefly.",
                  "If you are stuck in traffic on the bridge, consider it a private concert with a view.",
                  "Somewhere in Porto Sol, someone is dancing alone in their kitchen. Good for you, sweetheart.",
                  "The humidity is terrible for the piano, but wonderful for the soul.",
                  "Loosen your tie. Nobody at this station is wearing shoes."},
                 0.5f, false};
            break;
        case 6:
            d = {"Sawgrass Country 99.5", "Sawgrass Country, ninety nine five", "Country / Americana", Genre::Country, "Hank Delacroix",
                 vp(104.f, 0.98f, 0.88f, 0.18f, 0.22f, 1.2f),
                 {"Sawgrass Country, ninety nine five. From the swamp to your pickup truck.",
                  "Hank Delacroix, Sawgrass Country. Pull up a lawn chair.",
                  "Ninety nine point five, Sawgrass Country. Mosquito tested, mama approved.",
                  "You are listening to Sawgrass Country. Where every song has a truck, a dog, or a heartbreak. Sometimes all three.",
                  "Sawgrass Country. Broadcasting from a double wide trailer with a very tall antenna."},
                 {"My neighbor's airboat woke up the whole county this morning. Also the gators. Mostly the gators.",
                  "Fishing report from Okahatchee. The fish are biting. So are the horseflies. Bring spray.",
                  "Folks keep asking if there is country music in Palmera. Friend, there is a swamp in Palmera. Of course there is country music.",
                  "The Cypress Ridge county fair has a pie contest this weekend. I am a judge. I am also hungry.",
                  "If you are reading this bumper sticker, you are too close. That goes for life, too.",
                  "My truck has two hundred thousand miles on it and one working speaker. It plays this station just fine."},
                 0.6f, false};
            break;
        case 7:
            d = {"Tidepool FM 106.1", "Tidepool, one oh six point one", "Lo-Fi / Chillhop", Genre::LoFi, "Juno",
                 vp(198.f, 1.13f, 0.9f, 0.42f, 0.f, 0.75f),
                 {"Tidepool, one oh six point one. Beats to drive slowly to.",
                  "Hey. It's Juno. You're on Tidepool. Take a breath.",
                  "Tidepool F M. Soft sounds for loud cities.",
                  "One oh six one, Tidepool. No rush. Nothing is on fire. Probably.",
                  "Tidepool. The station for the space between places."},
                 {"If you are studying right now, you are doing great. If you are driving right now, please stop studying.",
                  "The rain in Porto Sol today is the soft kind. The good kind. Windows up, though.",
                  "Somebody sent me a photo of their cat asleep on the car dashboard. This one is for the cat.",
                  "Remember to unclench your jaw. And your hands on the steering wheel. Relax your shoulders.",
                  "There is a ferry leaving Sol Beach every forty minutes. Nobody is ever in a hurry on it. Be the ferry.",
                  "I made tea. It went cold. That is the Tidepool way."},
                 0.4f, false};
            break;
        default:
            d = {"Palmera Public Talk 1040 AM", "Palmera Public Talk, ten forty A M", "Talk Radio", Genre::Talk, "Gordon Pike",
                 vp(110.f, 1.f, 1.f, 0.12f, 0.1f, 1.2f),
                 {"Palmera Public Talk, ten forty A M. Opinions you did not ask for, all day long.",
                  "You are listening to Palmera Public Talk. Our phone lines are open, unfortunately.",
                  "Ten forty A M, Palmera Public Talk. Informing the public since the invention of complaining.",
                  "Palmera Public Talk. The news, the views, and the occasional alligator.",
                  "This is ten forty A M, Palmera Public Talk. Stay informed. Stay indoors."},
                 {"", "", "", "", "", ""},
                 0.f, true};
            break;
    }
    return d;
}

constexpr int kStations = 9;
static StationDef g_defs[kStations];
static bool g_defsInit = false;
static void ensureDefs() {
    if (g_defsInit) return;
    for (int i = 0; i < kStations; i++) g_defs[i] = makeDefs(i);
    g_defsInit = true;
}

// =============================================================================================
// Catalog name generators (all names original)
static std::string pickStr(Rng& r, const char* const* arr, int n) { return arr[r.irange(0, n - 1)]; }
#define PICK(r, a) pickStr(r, a, (int)ARRAY_COUNT(a))

static void genNames(Genre g, Rng& r, std::string& artist, std::string& title, int artistIdx) {
    Rng ar((u64)(artistIdx * 7919 + (int)g * 104729 + 17), 11);
    switch (g) {
        case Genre::Synthwave: {
            static const char* a1[] = {"Chrome", "Midnight", "Laser", "Vapor", "Pastel", "Velvet", "Arcade", "Turbo", "Crystal", "Cobalt", "Violet", "Electric", "Coral", "Starlight", "Neon"};
            static const char* a2[] = {"Voyager", "Cassette", "Boulevard", "Dynasty", "Mirage", "Tiger", "Horizon", "Machine", "Knights", "Signal", "Riders", "Heartbreak", "Dolphin", "Tapes", "Skyline"};
            artist = PICK(ar, a1) + std::string(" ") + PICK(ar, a2);
            static const char* t1[] = {"Satellite", "Neon", "Midnight", "Coral", "Endless", "Chrome", "Electric", "Crimson", "Ocean", "Last"};
            static const char* t2[] = {"Heart", "Fever", "Skyline", "Highway", "Summer", "Paradise", "Memory", "Horizon", "Nights", "Signal", "Kiss", "Drive"};
            static const char* full[] = {"Last Night in Porto Sol", "Palm Tree Memory", "Racing the Sunrise", "Pink Sky Protocol", "Downtown Hologram",
                                         "Lovers on the Causeway", "Sunset Rewind", "Chrome Tears", "Night Shift Romance", "Video Tide"};
            title = r.chance(0.3f) ? PICK(r, full) : PICK(r, t1) + std::string(" ") + PICK(r, t2);
            break;
        }
        case Genre::HipHop: {
            static const char* p[] = {"Lil", "Young", "Big", "MC", "King", "Yung", "Baby", "Kid", "Sir", "DJ"};
            static const char* n[] = {"Tidewater", "Mangrove", "Stingray", "Gator", "Flamingo", "Riptide", "Heatwave", "Monsoon", "Cashflow", "Voltage", "Pelican", "Nightshift", "Barracuda", "Sawgrass", "Hurricane"};
            static const char* solo[] = {"Stingray Blanco", "Ocho Vueltas", "Deuce Marina", "Southside Sosa", "Kilo Coast", "Twelve Palms", "Rico Humidity", "Nautica Dre"};
            artist = ar.chance(0.3f) ? PICK(ar, solo) : PICK(ar, p) + std::string(" ") + PICK(ar, n);
            static const char* t[] = {"Drip in the Rain", "Big Moves", "No Sleep in Porto Sol", "Gold Teeth, Cold Heart", "Swamp King", "Wavy",
                                      "Heatwave Season", "Top Floor", "Palm Trees and Problems", "Run It Up", "Humidity", "Money on the Water",
                                      "Late Checkout", "Southside Sunrise", "Count It Twice", "Boat Shoes", "Tinted", "Causeway Flex", "Low Tide, High Life", "Mosquito Bite"};
            title = PICK(r, t);
            break;
        }
        case Genre::Reggaeton: {
            static const char* f[] = {"Nico", "Dani", "Luna", "Rafa", "Valen", "Kiko", "Mateo", "Sofi", "Leo", "Cami", "Chico", "Marisa"};
            static const char* s[] = {"Fuego", "Brisa", "Tormenta", "Coral", "Marea", "Oro", "Luz", "Palmera", "Playa", "Noche", "Sal", "Caliente"};
            artist = PICK(ar, f) + std::string(" ") + PICK(ar, s);
            static const char* t[] = {"Fuego en la Playa", "Bailame Lento", "Noche de Calor", "Mi Isla", "Sal y Arena", "Contigo en Porto Sol", "La Ola",
                                      "Perreo Tropical", "Luna Llena", "Sin Frenos", "Corazon de Coco", "Tu y Yo y el Mar", "Verano Eterno", "Dame Mas",
                                      "Brillo", "Marea Alta", "Ritmo del Malecon", "Cuarenta Grados", "Besos de Sal", "Fiesta en la Azotea"};
            title = PICK(r, t);
            break;
        }
        case Genre::House: {
            static const char* a1[] = {"Deep", "Sub", "Solar", "Night", "Blue", "Lumen", "Vibe", "Echo", "Tidal", "Future", "Velvet", "Open"};
            static const char* a2[] = {"Theory", "Collective", "Motion", "Society", "Frequency", "Palms", "Division", "System", "State", "Unit", "Assembly", "Machine"};
            artist = PICK(ar, a1) + std::string(" ") + PICK(ar, a2);
            static const char* t[] = {"Feel It Rising", "Take Me Higher", "Sunrise Session", "Keep Moving", "Lose Control Tonight", "Glowstick Hearts",
                                      "Crowd Control", "Bassline Therapy", "Open Air", "Midnight Ferry", "Hands in the Humidity", "Warehouse Prayer",
                                      "Afterglow", "Rooftop Pool", "Strobe Light Lullaby", "Sweat and Satellites", "Only Up From Here", "Body Language"};
            title = PICK(r, t);
            break;
        }
        case Genre::Rock: {
            static const char* a1[] = {"Rusty", "Loose", "Burning", "Wild", "Broken", "Salty", "Electric", "Rolling", "Dirty", "Lonely", "Screaming", "Rabid"};
            static const char* a2[] = {"Anchors", "Screws", "Coyotes", "Engines", "Hearts", "Sirens", "Tides", "Vultures", "Alligators", "Wires", "Pistons", "Hounds"};
            artist = std::string("The ") + PICK(ar, a1) + " " + PICK(ar, a2);
            static const char* t[] = {"Highway Burn", "Tear It Down", "Salt in the Wound", "Hurricane Heart", "Long Way Down", "Engine Blood",
                                      "Last Call in Fort Castell", "Nothing Left to Burn", "Rattlesnake Kiss", "Gasoline Sunset", "Drowning in Neon",
                                      "Swamp Thunder", "Concrete Palms", "Bad Blood Boulevard", "Break the Radio", "Riptide", "Hollow Man Walking", "Rust and Chrome"};
            title = PICK(r, t);
            break;
        }
        case Genre::Jazz: {
            static const char* f[] = {"Theo", "Clara", "Oscar", "Ruby", "Felix", "Irene", "Marcus", "Vivian", "Julian", "Hazel", "Lionel", "Odette"};
            static const char* s[] = {"Beaumont", "Lacroix", "Whitfield", "Monroe", "Castellane", "DuPree", "Holloway", "Sinclair", "Moreau", "Ashby"};
            static const char* suf[] = {"", " Trio", " Quartet", " Quintet", ""};
            artist = PICK(ar, f) + std::string(" ") + PICK(ar, s) + PICK(ar, suf);
            static const char* t[] = {"Blue Lagoon Nocturne", "Porto Sol After Dark", "Midnight at the Marina", "Velvet Hours", "Rain on Magnolia Street",
                                      "Last Ferry Home", "Moonlight over the Keys", "Slow Dance for Two", "Smoke and Mangroves", "The Humid Hour",
                                      "Blue Heron", "Satin Sunset", "Cocktails at Eight", "Harbor Lights", "Sway of the Palms", "A Room at the Flamingo"};
            title = PICK(r, t);
            break;
        }
        case Genre::Country: {
            static const char* f[] = {"Wade", "Cody", "Dale", "Bobbie", "Tammy", "Earl", "Rhett", "Darla", "Colt", "Maybelle", "Luanne", "Buck"};
            static const char* s[] = {"Culpepper", "Haskins", "Whitaker", "McCrae", "Tillman", "Boone", "Pruitt", "Calhoun", "Sawyer", "Dupree"};
            static const char* bands[] = {"The Swamp Cats", "The Okahatchee Ramblers", "Cypress Ridge Revival", "The Bait Shop Boys", "Two Lane Sisters"};
            artist = ar.chance(0.25f) ? PICK(ar, bands) : PICK(ar, f) + std::string(" ") + PICK(ar, s);
            static const char* t[] = {"Gator Moon", "Two Lane Highway Home", "Whiskey and Mosquitoes", "My Truck Knows the Way", "Sawgrass Sunday",
                                      "Porch Light", "Cypress Ridge Rain", "Bait Shop Romance", "Dirt Road Diary", "Heartache in Okahatchee",
                                      "Fried and Forgiven", "Hurricane Honey", "Last Dance at the Feed Store", "Airboat Angel", "Rusty Tailgate", "Sweet Tea and Sorrow"};
            title = PICK(r, t);
            break;
        }
        case Genre::LoFi: {
            static const char* a[] = {"tidepool kid", "slow ferry", "sleepy mangrove", "cassette coast", "quiet heron", "low tide study",
                                      "palm shadow", "sunday laundromat", "humid dreams", "night bus", "soft pelican", "warm static"};
            artist = PICK(ar, a);
            static const char* t[] = {"rain on the windshield", "sea breeze", "homework at the marina", "pelican nap", "warm tape", "cloudy sunday",
                                      "late ferry", "coffee with salt", "streetlights", "porch fan", "blue hour", "wet sidewalk", "window seat",
                                      "slow motion", "lighthouse", "leftover rice"};
            title = PICK(r, t);
            break;
        }
        default:
            artist = "Palmera Public Talk";
            title = "News";
            break;
    }
}

// =============================================================================================
// Scripts
static const char* kIntros[] = {
    "Here's {artist} with {title}.", "This is {title}, from {artist}.", "Coming up, {artist}. {title}.",
    "{artist}. {title}. Turn it up.", "One of my favorites. {artist}, {title}.", "Next up on {station}, it's {artist} with {title}.",
    "Right now, {title} by {artist}.", "Let's go. {artist}, with {title}."};
static const char* kOutros[] = {
    "That was {artist}, with {title}.", "{title}, from {artist}. Beautiful.", "You just heard {artist}. {title}.",
    "{artist} with {title}. Still stuck in my head.", "That was {title}. {artist}. You're welcome."};
static const char* kTimeLines[] = {
    "It's {time} in Porto Sol, and {weather}.", "{time} on {station}. Out there, {weather}.",
    "Quick look outside. {weather}. It is {time}. Carry on.", "Weather check. {weather}. And it is {time}, in case you lost track."};

static const char* timePhrase(float tod, Rng& r) {
    if (tod >= 5.f && tod < 11.f) { static const char* a[] = {"early morning", "the morning", "breakfast time"}; return r.pick(a); }
    if (tod >= 11.f && tod < 14.f) { static const char* a[] = {"lunch time", "the middle of the day", "high noon"}; return r.pick(a); }
    if (tod >= 14.f && tod < 18.f) { static const char* a[] = {"the afternoon", "late afternoon", "the lazy part of the afternoon"}; return r.pick(a); }
    if (tod >= 18.f && tod < 21.f) { static const char* a[] = {"early evening", "sunset time", "the golden hour"}; return r.pick(a); }
    static const char* a[] = {"late at night", "the middle of the night", "way past your bedtime"};
    return r.pick(a);
}
static const char* weatherPhrase(float rain, float tod, Rng& r) {
    if (rain > 0.6f) { static const char* a[] = {"it is pouring, so drive like you have somewhere to be, slowly", "the rain is coming down sideways, classic Palmera", "it's a proper storm, stay off the causeway if you can"}; return r.pick(a); }
    if (rain > 0.15f) { static const char* a[] = {"there's a light drizzle, the kind that makes everything shiny", "it's raining a little, just enough to ruin your hair", "some showers rolling through, nothing a good wiper blade can't handle"}; return r.pick(a); }
    if (tod >= 20.f || tod < 5.f) { static const char* a[] = {"it's still eighty degrees somehow", "clear skies, warm air, and way too many mosquitoes", "the humidity is taking the night off, just kidding, it never does"}; return r.pick(a); }
    static const char* a[] = {"it's ninety degrees of pure humidity", "clear skies and sunshine, wear sunscreen", "it is hot, it is sticky, it is Palmera", "the sun is out and the asphalt is melting"};
    return r.pick(a);
}

static std::string fill(const char* tmpl, const std::string& artist, const std::string& title, const StationDef& st, float tod, float rain, Rng& r) {
    std::string out;
    for (const char* p = tmpl; *p;) {
        if (*p == '{') {
            const char* e = strchr(p, '}');
            if (!e) break;
            std::string key(p + 1, (size_t)(e - p - 1));
            if (key == "artist") out += artist;
            else if (key == "title") out += title;
            else if (key == "station") out += st.spoken;
            else if (key == "time") out += timePhrase(tod, r);
            else if (key == "weather") out += weatherPhrase(rain, tod, r);
            else if (key == "dj") out += st.dj;
            p = e + 1;
        } else {
            out += *p++;
        }
    }
    return out;
}

// ---- Commercials (all products and copy are original)
struct AdLine {
    int voice;
    const char* text;
};
struct AdDef {
    const char* product;
    int bedGenre;  // music bed genre
    AdLine lines[7];
};
static const AdDef kAds[] = {
    {"Gator Grip Tires", 4,
     {{AV_ANNOUNCER, "Rain. Sand. Mysterious swamp slime. Palmera roads are out to get you."},
      {AV_MAN, "I hydroplaned into a mango stand. Twice. Same mango stand."},
      {AV_ANNOUNCER, "Gator Grip Tires. They bite the road so you don't have to."},
      {AV_ANNOUNCER, "Now forty percent off at every Gator Grip location. Tread responsibly."}}},
    {"Sunny Side Realty", 0,
     {{AV_ANNOUNCER_F, "Imagine waking up to ocean views, salty breezes, and a mortgage you can almost afford."},
      {AV_WOMAN, "We bought a condo on Sol Beach. The ocean is technically in the living room now, but the view is incredible."},
      {AV_ANNOUNCER_F, "Sunny Side Realty. Own a piece of Palmera, before it's underwater."},
      {AV_ANNOUNCER_F, "Flood insurance not included."}}},
    {"Captain Crab's Seafood Shack", 6,
     {{AV_OLDMAN, "Ahoy there. Captain Crab here. I've been frying fish since before fish had names."},
      {AV_OLDMAN, "Come on down to Captain Crab's on the Fort Castell pier. All you can eat, Tuesdays and most Thursdays."},
      {AV_KID, "The hush puppies are bigger than my head!"},
      {AV_ANNOUNCER, "Captain Crab's Seafood Shack. It's fresh. Ish."}}},
    {"Pelican Mutual Insurance", 5,
     {{AV_ANNOUNCER, "At Pelican Mutual, we understand that life in Palmera is unpredictable."},
      {AV_ANNOUNCER, "Hurricanes. Sinkholes. Alligators in the carport. Your cousin's boat."},
      {AV_ANNOUNCER, "That's why Pelican Mutual covers almost everything, eventually, after a brief review process of eighteen to thirty six months."},
      {AV_ANNOUNCER_F, "Pelican Mutual. We've got you. Mostly."}}},
    {"Swamp Juice Energy", 3,
     {{AV_HYPE, "Are you tired? Are you sweaty? Is it a Tuesday?"},
      {AV_HYPE, "Crack open a Swamp Juice! Eight hundred milligrams of pure regret! I mean, energy!"},
      {AV_MAN, "I drank one and I could hear colors. Also my heart."},
      {AV_FAST, "Swamp Juice is not a beverage. Do not consume more than one can per week. Side effects include shouting, sprinting, and seeing the future."}}},
    {"Barry Fontaine, Attorney at Law", 5,
     {{AV_FAST, "Hurt? Confused? Hurt and confused? Call Barry Fontaine, Attorney at Law."},
      {AV_FAST, "Slipped on a wet floor? Bitten by a flamingo? Your neighbor's tree fell on your other neighbor's car? Call Barry."},
      {AV_WOMAN, "Barry got me a settlement so big, I bought the store I slipped in."},
      {AV_FAST, "Barry Fontaine. One eight hundred, Sue Palmera. Past results do not guarantee anything, legally speaking."}}},
    {"Crazy Carl's Auto Mall", 1,
     {{AV_HYPE, "It's Crazy Carl, and I've lost my mind! And my prices!"},
      {AV_HYPE, "Every car on the lot must go! Some of them are already going, the parking brake is broken!"},
      {AV_MAN, "I came in for a sedan and left with three jet skis and a limousine."},
      {AV_HYPE, "Crazy Carl's Auto Mall, on the Porto Sol expressway, exit fourteen. No credit? No problem! No license? Let's talk!"}}},
    {"Okahatchee Airboat Tours", 6,
     {{AV_OLDMAN, "Y'all ever seen a gator up close? Real close? Uncomfortably close?"},
      {AV_OLDMAN, "Okahatchee Airboat Tours takes you deep into the Sawgrass, where the wild things are, and the cell service isn't."},
      {AV_WOMAN, "We saw forty alligators, three herons, and a guy named Travis who lives out there."},
      {AV_ANNOUNCER, "Okahatchee Airboat Tours. Ear protection provided. Hands inside the boat."}}},
    {"Fort Castell Casino and Buffet", 3,
     {{AV_ANNOUNCER_F, "Feeling lucky? Feeling hungry? Feeling both at once?"},
      {AV_ANNOUNCER_F, "The Fort Castell Casino and Buffet has two thousand slot machines and a shrimp tower taller than a lighthouse."},
      {AV_MAN, "I lost my car keys, my savings, and my dignity. But the crab legs were unlimited."},
      {AV_ANNOUNCER_F, "Fort Castell Casino and Buffet. The house always wins, but the buffet is open till four A M."}}},
    {"Iron Pelican Fitness", 3,
     {{AV_HYPE, "Beach season in Palmera lasts three hundred and sixty five days a year. Are you ready? You are not ready."},
      {AV_HYPE, "Iron Pelican Fitness. Open twenty four hours, because your insecurities never sleep."},
      {AV_WOMAN, "I joined last month. I haven't gone yet, but I own the shirt."},
      {AV_ANNOUNCER, "Iron Pelican Fitness. First month free. Cancellation requires a notarized letter and a blood oath."}}},
    {"Mangrove Mattress Company", 7,
     {{AV_ANNOUNCER_F, "Your mattress is older than your car. And your car is very old."},
      {AV_ANNOUNCER_F, "At Mangrove Mattress Company, every mattress is humidity tested, gator proof, and so soft you'll call in sick."},
      {AV_MAN, "I laid down to test one in the showroom. I woke up two days later. They let me keep it."},
      {AV_ANNOUNCER_F, "Mangrove Mattress. Sleep like the swamp is watching over you."}}},
    {"FizzPhone Ten", 0,
     {{AV_ANNOUNCER, "Introducing the FizzPhone Ten. It's thinner. It's shinier. It's a phone."},
      {AV_ANNOUNCER, "Now with four cameras, three of which work, and a battery that lasts almost until lunch."},
      {AV_WOMAN, "I dropped my FizzPhone in the ocean. It's still out there, taking beautiful photos of fish."},
      {AV_ANNOUNCER, "FizzPhone Ten. Because your old phone is embarrassing you."}}},
    {"Big Flamingo Burgers", 2,
     {{AV_KID, "Mom! Mom! Can we go to Big Flamingo?"},
      {AV_ANNOUNCER_F, "Big Flamingo Burgers. Home of the Double Pink, a burger so big it has its own zip code."},
      {AV_MAN, "I ate a Triple Pink and saw the face of God. He wanted fries."},
      {AV_ANNOUNCER_F, "Big Flamingo. Stand on one leg, get a free milkshake. Balance not guaranteed."}}},
    {"Coral Keys Cruise Line", 5,
     {{AV_ANNOUNCER, "Escape to the Coral Keys aboard the Majestic Manatee, the slowest cruise ship in the world."},
      {AV_WOMAN, "Seven days, six nights, and we only moved about four miles. It was the most relaxing week of my life."},
      {AV_ANNOUNCER, "Endless buffet. Karaoke. A captain who used to be a dentist."},
      {AV_ANNOUNCER, "Coral Keys Cruise Line. You're not going anywhere, and that's the point."}}},
    {"SunBurn Tanning", 1,
     {{AV_HYPE, "Why wait for the sun when you can pay for it?"},
      {AV_WOMAN, "Everyone at Sol Beach thinks I'm on vacation. I work in accounting."},
      {AV_HYPE, "SunBurn Tanning Salons. Twelve locations, zero shade. Get bronzed, get noticed, get a dermatologist."}}},
    {"Palmera State Lottery", 3,
     {{AV_ANNOUNCER_F, "This week's Palmera Mega Jackpot is two hundred million dollars."},
      {AV_MAN, "If I win, I'm buying an island. Then another island. Then a bridge between them."},
      {AV_ANNOUNCER_F, "Somebody has to win. Statistically, it isn't you. But what if it is?"},
      {AV_ANNOUNCER_F, "Palmera State Lottery. Please play responsibly. Proceeds support the Department of Pothole Awareness."}}},
};
constexpr int kAdCount = (int)ARRAY_COUNT(kAds);

// ---- Talk show material (Palmera Public Talk)
struct TalkTopic {
    const char* intro;
    const char* lines[12];  // alternating: '1' host A (Gordon), '2' host B (Dana), 'C' caller - encoded as first char
};
static const TalkTopic kTopics[] = {
    {"Today on Palmera Public Talk, the great causeway traffic crisis.",
     {"1Dana, I sat on the Sol Beach causeway for ninety minutes this morning. I watched a man teach his dog to drive.",
      "2Was the dog any good?", "1Better than half the people out there, frankly.",
      "2The city says the new toll lanes will fix everything. The toll is eleven dollars.", "1Eleven dollars to sit in a slightly faster traffic jam. That's the Palmera dream.",
      "CHi, long time listener. I think the traffic is caused by the pelicans. They fly low on purpose.",
      "2The pelicans. You think this is a pelican conspiracy.", "CI'm just saying, nobody investigates the pelicans.",
      "1Thank you, caller. We'll look into the pelicans.", "2We will not be looking into the pelicans."}},
    {"On today's show, alligators in swimming pools. A growing concern.",
     {"2Gordon, animal control received forty calls last week about alligators in residential pools.",
      "1Forty. That's more calls than the pizza place gets.", "2One woman says the gator has been in her pool so long it has a name now.",
      "1What's its name?", "2Kevin.", "1Of course it's Kevin.",
      "CYeah, hi. I live in Okahatchee and I just want to say, the gators were here first. We're in their pool.",
      "1That's a very philosophical position, sir.", "CAlso if anyone's seen my kayak, Kevin might have it.", "2Stay safe out there, folks. Kevin is watching."}},
    {"Coming up, hurricane season. Are you prepared?",
     {"1Hurricane season is here, and Dana, I have some tips.", "2I'm afraid to ask.",
      "1Tip one. Buy all the bread. Every loaf. Bread is currency during a storm.", "2That's not a real tip, Gordon.",
      "1Tip two. Tape an X on your windows. It does absolutely nothing, but it looks official.",
      "2Please listen to the actual emergency services, everyone.",
      "CHi, I've lived in Palmera forty years. My tip is, a hurricane party is just a regular party with worse lighting.",
      "1Now that's the Palmera spirit.", "2That is not the Palmera spirit. Evacuate when told to evacuate."}},
    {"Today we discuss the mayor's controversial new fountain.",
     {"2The mayor unveiled the new fountain at Liberty Plaza yesterday. It cost four million dollars.",
      "1And it's shaped like the mayor.", "2It's shaped like the mayor, holding a smaller fountain.",
      "1Which is also shaped like the mayor.", "2It's fountains all the way down, Gordon.",
      "CI think it's beautiful. I don't know what everybody's complaining about.", "1Sir, is this the mayor?",
      "CNo. This is, uh, a regular citizen. Named, uh, Bob.", "2Thank you, Bob. Or mister mayor."}},
    {"Up next, Bigfoot sightings in Cypress Ridge. Yes, again.",
     {"1Three separate hikers claim they saw a large, hairy creature near Cypress Ridge last weekend.",
      "2Gordon, it's ninety five degrees in Cypress Ridge. No creature would choose to be that hairy.",
      "1That's what makes it so terrifying.",
      "CI saw him. Big guy. Very polite. He was waiting in line at the bait shop.",
      "2He was in line. At a bait shop.", "CHe bought crickets and a lottery ticket.", "1A Bigfoot with hope. I love this state.",
      "2We'll be right back after these messages."}},
    {"This hour, tourists and the sunscreen question.",
     {"2Every summer, thousands of tourists arrive in Palmera, and every summer, they turn the color of a boiled shrimp.",
      "1I saw a tourist yesterday so sunburned he was glowing. I used him as a night light.",
      "2The city is considering sunscreen stations on Sol Beach.", "1Free sunscreen? That's socialism, Dana.",
      "2It's lotion, Gordon.",
      "CHi, I'm visiting from up north. What's a reasonable amount of sunscreen?", "1All of it. Every bottle. Bathe in it.",
      "2Reapply every two hours, and please drink water.", "CThank you. I'm already very red. Is that normal?", "1Welcome to Palmera."}},
    {"Next, a review of the Fort Castell casino buffet. Our producer went undercover.",
     {"1Our producer spent six hours at the Fort Castell casino buffet so you don't have to.", "2How is he doing?",
      "1He's resting. He's stable. He ate eleven plates of crab legs and a chocolate fountain.",
      "2How can you eat a fountain?", "1With commitment, Dana.",
      "CI work at the buffet. Please tell your producer the chocolate fountain is not for drinking directly.",
      "1I'll pass that along.", "2Some things you can't un-see."}},
    {"And now, road rage on the Porto Sol expressway. Why are we like this?",
     {"2A new study says Palmera drivers are the angriest in the nation.", "1I read that study while someone honked at me for reading.",
      "2You were reading while driving?", "1I was stopped. In traffic. For forty minutes.",
      "CYeah, hi, I just want to apologize to the silver sedan I yelled at this morning. You didn't deserve that. You deserved worse.",
      "2That's not an apology.", "1It's the most Palmera apology I've ever heard.",
      "2Take a breath out there, everyone. Maybe listen to something calming."}},
};
constexpr int kTopicCount = (int)ARRAY_COUNT(kTopics);

static const char* kHeadlines[] = {
    "In local news, the Porto Sol city council voted to rename Third Street to Third Street, following a heated debate.",
    "Police are searching for a man who stole a truckload of rubber flamingos. The flamingos are described as pink.",
    "A Sol Beach lifeguard rescued a swimmer, a jet ski, and a surprisingly calm manatee in a single afternoon.",
    "The Palmera Department of Transportation announced that the pothole on Harbor Boulevard is now officially a pond.",
    "A Cypress Ridge man set a new state record by eating forty key lime pies. He is expected to recover. The pies are not.",
    "Fort Castell harbor authorities remind boaters that the no wake zone is not a suggestion, and neither is the other boat.",
    "The Okahatchee county fair has been postponed after the petting zoo escaped. Residents are asked to report any loose goats.",
    "Scientists at Palmera State University confirmed the humidity is, in fact, getting personal.",
    "Traffic on the expressway is backed up after a truck spilled several thousand oranges. Drivers are advised to bring a bag.",
    "The Coral Keys ferry is running on schedule today, a spokesperson said, visibly surprised.",
};

// =============================================================================================
// Timeline
enum class SegType : u8 { Song, DjBreak, Ads, Talk, News };
struct Line {
    std::string text;
    VoiceParams voice;
    float at = 0.f;   // estimated start within segment
    float est = 0.f;  // estimated duration
};
struct Seg {
    SegType type = SegType::Song;
    double start = 0.0, dur = 0.0;
    int song = -1;
    u32 seed = 0;
    std::vector<Line> lines;
    Genre bedGenre = Genre::Synthwave;
    float bedStart = 0.f, bedLen = 0.f;
    bool jingle = false;
    float speechStart = 0.f;
};
struct SongInfo {
    u32 seed;
    std::string artist, title;
    float dur;
};

struct Station {
    int idx = 0;
    StationDef def;
    std::vector<SongInfo> catalog;
    std::vector<Seg> segs;
    double offset = 0.0;
    std::mutex m;
    // generation state
    std::vector<int> rotation;
    int rotPos = 0;
    int songsSinceBreak = 0;
    int topicCursor = 0, adCursor = 0, headlineCursor = 0;
    u32 seed = 0;
    int lastSong = -1;
};
static Station g_st[kStations];
static bool g_stInit = false;
static std::atomic<float> g_ctxTime{12.f}, g_ctxRain{0.f};
static u64 g_session = 0;

static float estSpeech(const std::string& t, const VoiceParams& v) {
    return Speech::estimateDuration(t.c_str(), v) * speechDurationScale();
}

static void initStation(int i) {
    Station& s = g_st[i];
    s.idx = i;
    s.def = g_defs[i];
    s.seed = hash32((u32)i * 0x9E3779B9u + 0xA11CEu);
    s.catalog.clear();
    s.segs.clear();
    if (s.def.genre != Genre::Talk) {
        Rng r(s.seed, 21);
        int artists = 10;
        for (int a = 0; a < artists; a++) {
            int artistId = (int)(s.seed % 1000u) * 16 + a;
            int songsPer = 2;
            for (int k = 0; k < songsPer; k++) {
                SongInfo si;
                si.seed = hash32(s.seed + (u32)(a * 31 + k * 7 + 1));
                Rng nr(si.seed, 4);
                std::string artist, title;
                for (int attempt = 0; attempt < 12; attempt++) {
                    genNames(s.def.genre, nr, artist, title, artistId);
                    bool dup = false;
                    for (auto& o : s.catalog)
                        if (o.title == title) { dup = true; break; }
                    if (!dup) break;
                    if (attempt == 11) title += (k == 0 ? " (Night Mix)" : " (Reprise)");
                }
                si.artist = artist;
                si.title = title;
                si.dur = music::songDuration(s.def.genre, si.seed);
                s.catalog.push_back(si);
            }
        }
        (void)r;
    }
    s.rotation.clear();
    s.rotPos = 0;
    s.songsSinceBreak = 0;
    Rng orr(s.seed ^ (u32)g_session, 5);
    s.topicCursor = orr.irange(0, kTopicCount - 1);
    s.adCursor = orr.irange(0, kAdCount - 1);
    s.headlineCursor = orr.irange(0, (int)ARRAY_COUNT(kHeadlines) - 1);
    s.offset = (double)orr.range(0.f, 1500.f);
    s.lastSong = -1;
}

static int nextSong(Station& s, Rng& r) {
    int n = (int)s.catalog.size();
    if (n == 0) return -1;
    if (s.rotPos >= (int)s.rotation.size()) {
        s.rotation.resize((size_t)n);
        for (int i = 0; i < n; i++) s.rotation[(size_t)i] = i;
        for (int i = n - 1; i > 0; i--) std::swap(s.rotation[(size_t)i], s.rotation[(size_t)r.irange(0, i)]);
        if (s.rotation[0] == s.lastSong && n > 1) std::swap(s.rotation[0], s.rotation[(size_t)n - 1]);
        s.rotPos = 0;
    }
    int song = s.rotation[(size_t)s.rotPos++];
    s.lastSong = song;
    return song;
}

// Adds lines sequentially starting at `t0`; returns the end time.
static float layoutLines(Seg& seg, float t0, float gap) {
    float t = t0;
    for (auto& l : seg.lines) {
        l.at = t;
        l.est = estSpeech(l.text, l.voice);
        t += l.est + gap;
    }
    return t;
}

static void appendAds(Station& s, Seg& seg, Rng& r, int count) {
    seg.type = SegType::Ads;
    float t = 0.4f;
    for (int a = 0; a < count; a++) {
        const AdDef& ad = kAds[s.adCursor % kAdCount];
        s.adCursor++;
        u32 vs = hash32((u32)s.adCursor * 131u + s.seed);
        VoiceParams voices[AV_COUNT];
        for (int k = 0; k < AV_COUNT; k++) voices[k] = adVoice(k, vs + (u32)k);
        float adStart = t;
        for (const AdLine& l : ad.lines) {
            if (!l.text) break;
            Line ln;
            ln.text = l.text;
            ln.voice = voices[l.voice];
            ln.at = t;
            ln.est = estSpeech(ln.text, ln.voice);
            seg.lines.push_back(ln);
            t += ln.est + r.range(0.25f, 0.45f);
        }
        (void)adStart;
        t += 1.2f;
    }
    seg.bedGenre = (Genre)kAds[(s.adCursor - 1) % kAdCount].bedGenre;
    seg.dur = t + 0.5f;
}

static void appendSegment(Station& s) {
    double start = s.segs.empty() ? 0.0 : s.segs.back().start + s.segs.back().dur;
    u32 idx = (u32)s.segs.size();
    Rng r(hashCombine(s.seed, idx) ^ (u32)g_session, 7);
    Seg seg;
    seg.start = start;
    seg.seed = hash32(s.seed + idx * 2654435761u);
    const StationDef& d = s.def;
    float tod = g_ctxTime.load(), rain = g_ctxRain.load();
    if (d.genre == Genre::Talk) {
        // talk station: show segments with news bulletins and ads
        int kind = (int)(idx % 4u);
        if (kind == 3) {
            appendAds(s, seg, r, r.irange(2, 3));
            seg.bedGenre = (Genre)kAds[(s.adCursor - 1) % kAdCount].bedGenre;
        } else if (kind == 1) {
            seg.type = SegType::News;
            seg.jingle = true;
            VoiceParams anchor = vp(188.f, 1.12f, 1.02f, 0.1f, 0.f, 1.2f);
            seg.lines.push_back(Line{"This is Palmera Public Talk news, at the top of the hour. Here are your headlines.", anchor, 0, 0});
            int hn = r.irange(2, 3);
            for (int k = 0; k < hn; k++) seg.lines.push_back(Line{kHeadlines[(s.headlineCursor++) % (int)ARRAY_COUNT(kHeadlines)], anchor, 0, 0});
            seg.lines.push_back(Line{d.ids[r.irange(0, 4)], d.djVoice, 0, 0});
            float end = layoutLines(seg, 3.5f, 0.6f);
            seg.speechStart = 3.5f;
            seg.bedGenre = Genre::Talk;
            seg.bedStart = 0.f;
            seg.bedLen = end + 1.f;
            seg.dur = end + 1.f;
        } else {
            seg.type = SegType::Talk;
            seg.jingle = r.chance(0.5f);
            const TalkTopic& tp = kTopics[(s.topicCursor++) % kTopicCount];
            VoiceParams hostA = d.djVoice;
            VoiceParams hostB = vp(192.f, 1.12f, 1.04f, 0.14f, 0.f, 1.3f);
            Rng cr(seg.seed, 9);
            VoiceParams caller = adVoice(cr.chance(0.5f) ? AV_MAN : (cr.chance(0.5f) ? AV_WOMAN : AV_OLDMAN), seg.seed);
            seg.lines.push_back(Line{tp.intro, hostA, 0, 0});
            for (const char* l : tp.lines) {
                if (!l || !*l) break;
                VoiceParams v = l[0] == '1' ? hostA : l[0] == '2' ? hostB : caller;
                seg.lines.push_back(Line{l + 1, v, 0, 0});
            }
            float st = seg.jingle ? 3.5f : 0.5f;
            float end = layoutLines(seg, st, 0.45f);
            seg.speechStart = st;
            seg.bedGenre = Genre::Talk;
            seg.bedStart = 0.f;
            seg.bedLen = seg.jingle ? 6.f : 0.f;
            seg.dur = end + 0.8f;
        }
        s.segs.push_back(seg);
        return;
    }
    // music stations
    bool breakDue = s.songsSinceBreak >= 2 && (s.songsSinceBreak >= 4 || r.chance(0.5f));
    if (breakDue && !s.segs.empty() && s.segs.back().type == SegType::Song) {
        s.songsSinceBreak = 0;
        if (r.chance(0.55f)) {
            appendAds(s, seg, r, r.irange(1, 3));
        } else {
            seg.type = SegType::DjBreak;
            seg.jingle = true;
            const Seg& prev = s.segs.back();
            if (prev.song >= 0 && r.chance(0.6f)) {
                const SongInfo& si = s.catalog[(size_t)prev.song];
                seg.lines.push_back(Line{fill(kOutros[r.irange(0, (int)ARRAY_COUNT(kOutros) - 1)], si.artist, si.title, d, tod, rain, r), d.djVoice, 0, 0});
            }
            seg.lines.push_back(Line{d.ids[r.irange(0, 4)], d.djVoice, 0, 0});
            if (r.chance(0.5f)) seg.lines.push_back(Line{fill(kTimeLines[r.irange(0, (int)ARRAY_COUNT(kTimeLines) - 1)], "", "", d, tod, rain, r), d.djVoice, 0, 0});
            else seg.lines.push_back(Line{d.banter[r.irange(0, 5)], d.djVoice, 0, 0});
            float end = layoutLines(seg, 3.2f, 0.5f);
            seg.speechStart = 3.2f;
            seg.bedGenre = d.genre;
            seg.bedStart = 2.5f;
            seg.bedLen = end - 2.5f + 1.5f;
            seg.dur = end + 1.0f;
        }
        s.segs.push_back(seg);
        return;
    }
    seg.type = SegType::Song;
    seg.song = nextSong(s, r);
    if (seg.song < 0) {
        seg.type = SegType::DjBreak;
        seg.lines.push_back(Line{d.ids[0], d.djVoice, 0, 0});
        seg.dur = layoutLines(seg, 1.f, 0.5f) + 1.f;
        s.segs.push_back(seg);
        return;
    }
    const SongInfo& si = s.catalog[(size_t)seg.song];
    seg.dur = Max(30.0, (double)si.dur - 2.0);
    if (r.chance(d.introChance)) {
        seg.lines.push_back(Line{fill(kIntros[r.irange(0, (int)ARRAY_COUNT(kIntros) - 1)], si.artist, si.title, d, tod, rain, r), d.djVoice, 0, 0});
        layoutLines(seg, 1.2f, 0.4f);
        seg.speechStart = 1.2f;
    }
    s.songsSinceBreak++;
    s.segs.push_back(seg);
}

// Ensures the timeline covers [0, t + ahead]. Caller holds s.m.
static void materialize(Station& s, double t, double ahead = 400.0) {
    for (int guard = 0; guard < 4096; guard++) {
        double end = s.segs.empty() ? 0.0 : s.segs.back().start + s.segs.back().dur;
        if (end > t + ahead) break;
        appendSegment(s);
    }
}
static int findSeg(const Station& s, double t) {
    int lo = 0, hi = (int)s.segs.size() - 1, ans = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (s.segs[(size_t)mid].start <= t) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return ans;
}

void ensureStations() {
    if (g_stInit) return;
    ensureDefs();
    for (int i = 0; i < kStations; i++) initStation(i);
    g_stInit = true;
}

}  // namespace radio

// =============================================================================================
// Speech worker
namespace speechw {
std::mutex g_m;
std::condition_variable g_cv;
std::vector<SpeechJob*> g_queue;
std::thread g_thread;
std::atomic<bool> g_quit{false};
bool g_async = false;
std::atomic<float> g_durScale{1.f};
std::atomic<int> g_calibN{0};
constexpr int kMaxQueuedDialogue = 48;

static void synthJob(SpeechJob* j) {
    j->state.store(1);
    std::vector<float> pcm;
    if (!j->text.empty()) Speech::synthesize(j->text.c_str(), j->voice, j->sampleRate, pcm);
    for (float& v : pcm) {
        if (!std::isfinite(v)) v = 0.f;
        v = Clamp(v, -1.f, 1.f);
    }
    float est = Speech::estimateDuration(j->text.c_str(), j->voice);
    float act = (float)pcm.size() / (float)Max(1, j->sampleRate);
    if (est > 0.3f && act > 0.1f) speechCalibrate(est, act);
    j->pcm.swap(pcm);
    j->state.store(2, std::memory_order_release);
    if (j->priority == 0) dialogSpeechReady(j);
}

static void workerMain() {
    backend::setThreadLowPriority();
    dsp::enableFlushDenormals();
    for (;;) {
        SpeechJob* j = nullptr;
        {
            std::unique_lock<std::mutex> lk(g_m);
            g_cv.wait(lk, [] { return g_quit.load() || !g_queue.empty(); });
            if (g_quit.load()) return;
            size_t best = 0;
            for (size_t i = 1; i < g_queue.size(); i++)
                if (g_queue[i]->priority < g_queue[best]->priority) best = i;
            j = g_queue[best];
            g_queue.erase(g_queue.begin() + (long)best);
        }
        synthJob(j);
        speechJobRelease(j);
    }
}
}  // namespace speechw

void speechJobRelease(SpeechJob* j) {
    if (j && j->refs.fetch_sub(1) == 1) delete j;
}
bool speechAsync() { return speechw::g_async; }
void speechCalibrate(float estimated, float actual) {
    float ratio = Clamp(actual / estimated, 0.5f, 2.f);
    int n = speechw::g_calibN.fetch_add(1);
    float cur = speechw::g_durScale.load();
    float k = n < 8 ? 1.f / (float)(n + 1) : 0.1f;
    speechw::g_durScale.store(cur + (ratio - cur) * k);
}
float speechDurationScale() { return speechw::g_durScale.load(); }

void speechSubmit(SpeechJob* j) {
    using namespace speechw;
    j->refs.fetch_add(1);
    if (!g_async) {
        if (j->priority > 0) {
            synthJob(j);
            speechJobRelease(j);
            return;
        }
        std::lock_guard<std::mutex> lk(g_m);
        if (g_queue.size() >= (size_t)kMaxQueuedDialogue) {
            j->state.store(2);
            dialogSpeechDropped(j);
            speechJobRelease(j);
            return;
        }
        g_queue.push_back(j);
        return;
    }
    SpeechJob* dropped = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_m);
        if (j->priority == 0) {
            // bound the dialogue backlog: drop the oldest queued line (it would play far too late anyway)
            int pending = 0;
            size_t oldest = g_queue.size();
            for (size_t i = 0; i < g_queue.size(); i++)
                if (g_queue[i]->priority == 0) {
                    if (oldest == g_queue.size()) oldest = i;
                    pending++;
                }
            if (pending >= kMaxQueuedDialogue) {
                dropped = g_queue[oldest];
                g_queue.erase(g_queue.begin() + (long)oldest);
            }
        }
        g_queue.push_back(j);
    }
    g_cv.notify_one();
    if (dropped) {
        dropped->state.store(2);
        dialogSpeechDropped(dropped);
        speechJobRelease(dropped);
    }
}

void speechPumpSync() {
    using namespace speechw;
    dsp::ScopedFlushDenormals ftz;
    for (;;) {
        SpeechJob* j = nullptr;
        {
            std::lock_guard<std::mutex> lk(g_m);
            if (g_queue.empty()) return;
            j = g_queue.front();
            g_queue.erase(g_queue.begin());
        }
        synthJob(j);
        speechJobRelease(j);
    }
}

void speechStart(bool async) {
    using namespace speechw;
    g_async = async;
    g_quit = false;
    if (async && !g_thread.joinable()) g_thread = std::thread(workerMain);
}
void speechStop() {
    using namespace speechw;
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_quit = true;
    }
    g_cv.notify_all();
    if (g_thread.joinable()) g_thread.join();
    std::vector<SpeechJob*> rest;
    {
        std::lock_guard<std::mutex> lk(g_m);
        rest.swap(g_queue);
    }
    for (SpeechJob* j : rest) {
        j->state.store(2);
        if (j->priority == 0) dialogSpeechDropped(j);
        speechJobRelease(j);
    }
    g_async = false;
}

namespace radio {

// =============================================================================================
// Segment playback
constexpr int kRadioSpeechRate = 32000;

struct VoiceChain {
    Biquad hp, pres, lp1, lp2;
    Compressor comp;
    bool am = false;
    void init(bool isAm) {
        am = isAm;
        hp.setHP(isAm ? 180.f : 85.f, 0.7f);
        pres.setPeak(3000.f, 0.9f, 3.f);
        lp1.setLP(isAm ? 4200.f : 12000.f, 0.7f);
        lp2.setLP(isAm ? 4200.f : 12000.f, 0.7f);
        comp.set(-20.f, 3.5f, 3.f, 90.f, 6.f, 4.f);
    }
    FORCEINLINE float process(float x) {
        x = pres.process(hp.process(x));
        x *= comp.computeGain(fabsf(x));
        x = lp2.process(lp1.process(x));
        if (am) x = fastTanh(x * 1.6f) * 0.7f;
        return x;
    }
};

struct SegPlayer {
    Seg seg;
    int station = 0;
    bool am = false;
    std::unique_ptr<music::SongPlayer> song, jingle;
    std::vector<SpeechJob*> jobs;
    int line = 0;
    double t = 0.0;
    double lastLineEnd = -10.0;
    double waitStart = -1.0;
    SpeechJob* clip = nullptr;
    double clipPos = 0.0, clipRate = 1.0;
    float duck = 1.f;
    float duckDepth = 0.3f;
    VoiceChain vc;
    float jingleGain = 1.f;
    float tmpL[kProdBlock], tmpR[kProdBlock];

    ~SegPlayer() {
        for (SpeechJob* j : jobs) speechJobRelease(j);
    }

    void submitLines(std::vector<SpeechJob*>* prefetched) {
        if (prefetched && prefetched->size() == seg.lines.size()) {
            jobs.swap(*prefetched);
            return;
        }
        if (prefetched) {
            for (SpeechJob* j : *prefetched) speechJobRelease(j);
            prefetched->clear();
        }
        jobs = makeJobs(seg, 1);
    }

    static std::vector<SpeechJob*> makeJobs(const Seg& s, int prio) {
        std::vector<SpeechJob*> out;
        for (const Line& l : s.lines) {
            SpeechJob* j = new SpeechJob();
            j->text = l.text;
            j->voice = l.voice;
            j->sampleRate = kRadioSpeechRate;
            j->priority = prio;
            out.push_back(j);
            speechSubmit(j);
        }
        return out;
    }

    void start(const Seg& s, int stationIdx, double offset, std::vector<SpeechJob*>* prefetched) {
        seg = s;
        station = stationIdx;
        const Station& st = g_st[stationIdx];
        am = st.def.am;
        vc.init(am);
        t = Max(0.0, offset);
        u32 offS = (u32)(t * kSR);
        switch (seg.type) {
            case SegType::Song: {
                const SongInfo& si = st.catalog[(size_t)seg.song];
                song.reset(new music::SongPlayer());
                song->start(music::composeSong(st.def.genre, si.seed), offS);
                duckDepth = dbToGain(-10.f);
                break;
            }
            default: {
                Genre bedG = seg.bedGenre;
                std::shared_ptr<music::SongData> jd, bd;
                if (seg.jingle) jd = music::composeJingle(seg.type == SegType::Talk || seg.type == SegType::News ? Genre::Talk : st.def.genre, seg.seed);
                if (seg.type == SegType::Ads) bd = music::composeBed(bedG, seg.seed ^ 0xADu, (float)seg.dur);
                else if (seg.bedLen > 0.f) bd = music::composeBed(bedG, seg.seed, seg.bedLen);
                if (jd && bd && jd->kitStyle == bd->kitStyle && jd->seed == bd->seed)
                    for (int k = 0; k < music::DK_COUNT; k++) jd->kitUsed[k] = bd->kitUsed[k] = jd->kitUsed[k] || bd->kitUsed[k];
                if (jd && offS < jd->length) {
                    jingle.reset(new music::SongPlayer());
                    jingle->start(jd, offS);
                }
                if (bd) {
                    u32 bs = (u32)(seg.bedStart * kSR);
                    if (offS < bs + bd->length) {
                        song.reset(new music::SongPlayer());
                        song->start(bd, offS > bs ? offS - bs : 0, jingle ? &jingle->kit : nullptr);
                        if (offS < bs) song->pos = 0;
                    }
                }
                duckDepth = dbToGain(seg.type == SegType::Ads ? -9.f : -7.f);
                break;
            }
        }
        submitLines(prefetched);
        // skip lines that are already over
        line = 0;
        while (line < (int)seg.lines.size() && seg.lines[(size_t)line].at + seg.lines[(size_t)line].est * 0.7f < (float)t) line++;
        lastLineEnd = -10.0;
    }

    bool speaking() const { return clip != nullptr; }

    void render(float* L, float* R, int n) {
        memset(L, 0, sizeof(float) * (size_t)n);
        memset(R, 0, sizeof(float) * (size_t)n);
        double segT = t;
        float bedStart = seg.bedStart;
        if (jingle) {
            jingle->render(tmpL, tmpR, n);
            for (int i = 0; i < n; i++) {
                L[i] += tmpL[i] * jingleGain;
                R[i] += tmpR[i] * jingleGain;
            }
            if (jingle->finished()) jingle.reset();
        }
        if (song) {
            if (seg.type == SegType::Song || segT + (double)n * kInvSR > (double)bedStart) {
                song->render(tmpL, tmpR, n);
                // apply ducking with per-sample smoothing
                float target = (clip || (line < (int)seg.lines.size() && waitStart >= 0.0)) ? duckDepth : 1.f;
                for (int i = 0; i < n; i++) {
                    duck += (target - duck) * (target < duck ? 0.0006f : 0.00012f);
                    L[i] += tmpL[i] * duck;
                    R[i] += tmpR[i] * duck;
                }
                if (song->finished()) song.reset();
            }
        }
        // speech
        for (int i = 0; i < n; i++) {
            double now = segT + (double)i * kInvSR;
            if (!clip && line < (int)seg.lines.size()) {
                const Line& ln = seg.lines[(size_t)line];
                double due = Max((double)ln.at, lastLineEnd + 0.35);
                if (now >= due) {
                    SpeechJob* j = jobs.size() > (size_t)line ? jobs[(size_t)line] : nullptr;
                    if (j && j->state.load(std::memory_order_acquire) == 2) {
                        if (j->pcm.empty()) {
                            line++;
                        } else {
                            clip = j;
                            clipPos = 0.0;
                            clipRate = (double)j->sampleRate / kSR;
                        }
                        waitStart = -1.0;
                    } else {
                        if (waitStart < 0.0) waitStart = now;
                        if (now - waitStart > 8.0) {
                            line++;
                            waitStart = -1.0;
                        }
                    }
                }
            }
            if (clip) {
                int ip = (int)clipPos;
                int len = (int)clip->pcm.size();
                if (ip + 1 >= len) {
                    clip = nullptr;
                    lastLineEnd = now;
                    line++;
                } else {
                    float fr = (float)(clipPos - (double)ip);
                    float s = clip->pcm[(size_t)ip] + (clip->pcm[(size_t)ip + 1] - clip->pcm[(size_t)ip]) * fr;
                    clipPos += clipRate;
                    float v = vc.process(s) * 0.8f;
                    L[i] += v;
                    R[i] += v;
                }
            }
        }
        t += (double)n * kInvSR;
    }
    bool done() const { return !song && !jingle && !clip; }
};

// =============================================================================================
// Station producer with broadcast processing (AGC, compressor, EQ, limiter)
struct StationProducer {
    int station = -1;
    u32 gen = 0;
    double stTime = 0.0;
    std::unique_ptr<SegPlayer> cur, prev;
    int curSeg = -1;
    float prevFade = 1.f;
    std::vector<SpeechJob*> nextJobs;
    int nextJobsSeg = -1;
    // broadcast chain
    float agcRms = 1e-4f, agcGain = 1.f;
    Compressor comp;
    LookaheadLimiter lim;
    Biquad lsL, lsR, hsL, hsR, lpL, lpR, lp2L, lp2R, hpL, hpR;
    bool am = false;
    float tmpL[kProdBlock], tmpR[kProdBlock];

    ~StationProducer() { release(); }
    void release() {
        cur.reset();
        prev.reset();
        for (SpeechJob* j : nextJobs) speechJobRelease(j);
        nextJobs.clear();
        nextJobsSeg = -1;
        station = -1;
        curSeg = -1;
    }
    void reset(int st, double t) {
        release();
        station = st;
        stTime = t;
        am = g_st[st].def.am;
        comp.set(-16.f, 3.f, 5.f, 150.f, 6.f, 3.f);
        lim.init(72, 0.89f, 60.f);
        if (am) {
            hpL.setHP(140.f, 0.7f); hpR = hpL;
            lpL.setLP(4300.f, 0.7f); lpR = lpL; lp2L = lpL; lp2R = lpL;
        } else {
            hpL.setHP(25.f, 0.7f); hpR = hpL;
            lsL.setLowShelf(90.f, 1.5f); lsR = lsL;
            hsL.setHighShelf(7000.f, 1.5f); hsR = hsL;
            lpL.setLP(15000.f, 0.7f); lpR = lpL; lp2L = lpL; lp2R = lpL;
        }
        agcGain = 1.f;
        agcRms = 1e-4f;
    }
    void renderBlock(float* L, float* R) {
        const int n = kProdBlock;
        Station& s = g_st[station];
        int idx;
        Seg segCopy, nextCopy;
        bool haveNext = false;
        {
            std::lock_guard<std::mutex> lk(s.m);
            materialize(s, stTime);
            idx = findSeg(s, stTime);
            if (idx != curSeg) segCopy = s.segs[(size_t)idx];
            if (idx + 1 < (int)s.segs.size() && nextJobsSeg != idx + 1 && !s.segs[(size_t)idx + 1].lines.empty()) {
                nextCopy = s.segs[(size_t)idx + 1];
                haveNext = true;
            }
        }
        if (idx != curSeg) {
            prev = std::move(cur);
            prevFade = 1.f;
            cur.reset(new SegPlayer());
            std::vector<SpeechJob*>* pre = (nextJobsSeg == idx) ? &nextJobs : nullptr;
            cur->start(segCopy, station, stTime - segCopy.start, pre);
            if (pre == nullptr) {
                for (SpeechJob* j : nextJobs) speechJobRelease(j);
            }
            nextJobs.clear();
            nextJobsSeg = -1;
            curSeg = idx;
        }
        if (haveNext) {
            for (SpeechJob* j : nextJobs) speechJobRelease(j);
            nextJobs = SegPlayer::makeJobs(nextCopy, 2);
            nextJobsSeg = idx + 1;
        }
        cur->render(L, R, n);
        if (prev) {
            prev->render(tmpL, tmpR, n);
            for (int i = 0; i < n; i++) {
                prevFade *= 0.99993f;
                L[i] += tmpL[i] * prevFade;
                R[i] += tmpR[i] * prevFade;
            }
            if (prev->done() || prevFade < 0.01f) prev.reset();
        }
        process(L, R, n);
        stTime += (double)n * kInvSR;
    }
    void process(float* L, float* R, int n) {
        // AGC: slow leveling toward -17 dBFS RMS, frozen in near-silence
        const float target = 0.141f;
        for (int i = 0; i < n; i++) {
            float x = 0.5f * (L[i] * L[i] + R[i] * R[i]);
            agcRms += (x - agcRms) * 0.00006f;
        }
        float rms = sqrtf(agcRms);
        if (rms > 0.006f) {
            float want = Clamp(target / rms, 0.4f, 3.2f);
            agcGain += (want - agcGain) * (want < agcGain ? 0.02f : 0.006f);
        }
        for (int i = 0; i < n; i++) {
            float l = L[i] * agcGain, r = R[i] * agcGain;
            if (am) {
                float m = 0.5f * (l + r);
                m = lp2L.process(lpL.process(hpL.process(m)));
                l = r = m;
            } else {
                l = lpL.process(hsL.process(lsL.process(hpL.process(l))));
                r = lpR.process(hsR.process(lsR.process(hpR.process(r))));
            }
            L[i] = l;
            R[i] = r;
        }
        comp.processStereo(L, R, n);
        lim.process(L, R, n);
    }
};

// =============================================================================================
// Adaptive score producer
struct ScoreProducer {
    struct Inst {
        music::ScoreGen gen;
        std::unique_ptr<music::SongPlayer> player;
        float fade = 0.f, fadeTarget = 1.f;
        int mood = -1;
        bool active() const { return player != nullptr; }
    };
    Inst inst[2];
    int curInst = 0;
    float gain = 0.f;
    float layer[4] = {0, 0, 0, 0};
    LookaheadLimiter lim;
    bool limInit = false;
    float tmpL[kProdBlock], tmpR[kProdBlock];

    void startInst(Inst& in, int mood) {
        in.gen.init(mood);
        in.player.reset(new music::SongPlayer());
        in.gen.appendChunk();
        in.player->start(in.gen.sd, 0);
        in.mood = mood;
        in.fade = 0.f;
        in.fadeTarget = 1.f;
        for (int k = 0; k < 4; k++) in.player->layerGain[k] = layer[k];
    }
    bool activeAny() const { return inst[0].active() || inst[1].active(); }
    void renderBlock(float* L, float* R, int mood, float intensity) {
        const int n = kProdBlock;
        if (!limInit) {
            lim.init(72, 0.89f, 80.f);
            limInit = true;
        }
        float blockSec = (float)n * kInvSR;
        float targetGain = intensity > 0.005f ? SmoothStep(0.f, 0.12f, intensity) * (0.75f + 0.25f * intensity) : 0.f;
        gain += (targetGain - gain) * (1.f - expf(-blockSec / 1.2f));
        float lt[4] = {SmoothStep(0.f, 0.15f, intensity), SmoothStep(0.2f, 0.4f, intensity), SmoothStep(0.45f, 0.6f, intensity),
                       SmoothStep(0.7f, 0.85f, intensity)};
        for (int k = 0; k < 4; k++) layer[k] += (lt[k] - layer[k]) * (1.f - expf(-blockSec / 1.5f));
        if (mood >= 0 && intensity > 0.005f) {
            Inst& c = inst[curInst];
            if (!c.active()) startInst(c, mood);
            else if (c.mood != mood) {
                c.fadeTarget = 0.f;
                curInst ^= 1;
                Inst& nw = inst[curInst];
                nw.player.reset();
                startInst(nw, mood);
            }
        }
        memset(L, 0, sizeof(float) * (size_t)n);
        memset(R, 0, sizeof(float) * (size_t)n);
        for (int k = 0; k < 2; k++) {
            Inst& in = inst[k];
            if (!in.active()) continue;
            music::SongPlayer& p = *in.player;
            // keep composition ahead of playback
            u32 bar = (u32)(240.f / in.gen.plan.bpm * kSR);
            while (in.gen.chunkEndSample(in.gen.chunk) < p.pos + bar * 4) in.gen.appendChunk();
            if (p.ev > 4096) {
                in.gen.sd->events.erase(in.gen.sd->events.begin(), in.gen.sd->events.begin() + (long)p.ev);
                p.ev = 0;
            }
            for (int l = 0; l < 4; l++) p.layerGain[l] = layer[l];
            p.render(tmpL, tmpR, n);
            float f0 = in.fade;
            in.fade += (in.fadeTarget - in.fade) * (1.f - expf(-blockSec / 1.f));
            if (in.fadeTarget <= 0.f && in.fade < 0.002f) {
                in.player.reset();
                in.mood = -1;
                continue;
            }
            for (int i = 0; i < n; i++) {
                float f = f0 + (in.fade - f0) * ((float)i / (float)n);
                L[i] += tmpL[i] * f;
                R[i] += tmpR[i] * f;
            }
            if (p.pos > 0x70000000u) in.fadeTarget = 0.f;  // extremely long session: restart
        }
        for (int i = 0; i < n; i++) {
            L[i] *= gain * 0.9f;
            R[i] *= gain * 0.9f;
        }
        lim.process(L, R, n);
        if (gain < 0.001f && targetGain <= 0.f) {
            for (auto& in : inst) { in.player.reset(); in.mood = -1; }
        }
    }
};

}  // namespace radio

// =============================================================================================
// Producer slots + music thread
namespace mthread {
ProducerSlot g_slots[kProducerSlots];
radio::StationProducer g_prod[kStationSlots];
radio::ScoreProducer g_score;
std::thread g_thread;
std::atomic<bool> g_quit{false};
std::atomic<bool> g_running{false};
std::mutex g_m;
int g_idleBlocks[kStationSlots] = {};

static int targetBlocks() { return g_running.load() ? 16 : kRingBlocks; }

// Renders at most one block per slot. Returns true if any work was done.
static bool pumpOnce(int target) {
    bool any = false;
    for (int i = 0; i < kStationSlots; i++) {
        ProducerSlot& ps = g_slots[i];
        radio::StationProducer& pr = g_prod[i];
        int want = ps.wantedStation.load(std::memory_order_acquire);
        u32 gen = ps.wantedGen.load(std::memory_order_acquire);
        if (want < 0 || want >= radio::kStations) {
            if (pr.station >= 0 && ++g_idleBlocks[i] > 200) pr.release();
            continue;
        }
        g_idleBlocks[i] = 0;
        if (pr.station != want || pr.gen != gen) {
            double rt = (double)g_radioFrames.load() / kSR;
            pr.reset(want, rt + radio::g_st[want].offset + 0.05);
            pr.gen = gen;
        }
        ProducerRing& ring = ps.ring;
        if (ring.filled() < target && ring.freeBlocks() > 0) {
            u32 w = ring.writeCount.load(std::memory_order_relaxed);
            ProducerRing::Block& b = ring.blocks[w % kRingBlocks];
            pr.renderBlock(b.l, b.r);
            b.gen = gen;
            ring.writeCount.store(w + 1, std::memory_order_release);
            any = true;
        }
    }
    // score
    ProducerSlot& ss = g_slots[kScoreSlot];
    int mood = ss.wantedStation.load(std::memory_order_acquire);
    float inten = ss.intensity.load(std::memory_order_acquire);
    bool scoreOn = (mood >= 0 && inten > 0.005f) || g_score.activeAny();
    ss.active.store(scoreOn, std::memory_order_release);
    if (scoreOn) {
        ProducerRing& ring = ss.ring;
        if (ring.filled() < target && ring.freeBlocks() > 0) {
            u32 w = ring.writeCount.load(std::memory_order_relaxed);
            ProducerRing::Block& b = ring.blocks[w % kRingBlocks];
            g_score.renderBlock(b.l, b.r, mood, mood >= 0 ? inten : 0.f);
            b.gen = 0;
            ring.writeCount.store(w + 1, std::memory_order_release);
            any = true;
        }
    }
    return any;
}

static void threadMain() {
    backend::setThreadHighPriority();
    dsp::enableFlushDenormals();
    while (!g_quit.load()) {
        bool worked;
        {
            std::lock_guard<std::mutex> lk(g_m);
            worked = pumpOnce(targetBlocks());
        }
        if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}
}  // namespace mthread

ProducerSlot& producerSlot(int i) { return mthread::g_slots[Clamp(i, 0, kProducerSlots - 1)]; }

void musicInit(u64 sessionSeed) {
    radio::g_session = sessionSeed;
    radio::ensureStations();
}

void musicStartThread() {
    using namespace mthread;
    if (g_thread.joinable()) return;
    music::pianoStartAsync();
    g_quit = false;
    g_running = true;
    g_thread = std::thread(threadMain);
}
void musicStopThread() {
    using namespace mthread;
    g_quit = true;
    if (g_thread.joinable()) g_thread.join();
    g_running = false;
}
bool musicThreadRunning() { return mthread::g_running.load(); }

void musicShutdown() {
    musicStopThread();
    std::lock_guard<std::mutex> lk(mthread::g_m);
    for (auto& p : mthread::g_prod) p.release();
    for (auto& in : mthread::g_score.inst) {
        in.player.reset();
        in.mood = -1;
    }
    music::pianoWait();
}

void musicPumpSync(int framesNeeded) {
    using namespace mthread;
    dsp::ScopedFlushDenormals ftz;
    std::lock_guard<std::mutex> lk(g_m);
    if (!music::g_piano.ready.load()) music::pianoWait();
    int need = (framesNeeded + kProdBlock - 1) / kProdBlock + 1;
    for (int guard = 0; guard < kRingBlocks * 2; guard++) {
        bool short_ = false;
        for (int i = 0; i < kProducerSlots; i++) {
            ProducerSlot& ps = g_slots[i];
            bool act = i < kStationSlots ? ps.wantedStation.load() >= 0 : (ps.wantedStation.load() >= 0 || g_score.activeAny());
            if (act && ps.ring.filled() < need && ps.ring.freeBlocks() > 0) short_ = true;
        }
        if (!short_) break;
        if (!pumpOnce(need)) break;
    }
}

int stationCount() { return radio::kStations; }
const char* stationName(int i) {
    radio::ensureDefs();
    return (i >= 0 && i < radio::kStations) ? radio::g_defs[i].name : "";
}
const char* stationGenre(int i) {
    radio::ensureDefs();
    return (i >= 0 && i < radio::kStations) ? radio::g_defs[i].genreName : "";
}
std::string stationNowPlaying(int i, double radioTimeSec) {
    if (i < 0 || i >= radio::kStations) return std::string();
    radio::ensureStations();
    radio::Station& s = radio::g_st[i];
    std::lock_guard<std::mutex> lk(s.m);
    double t = radioTimeSec + s.offset;
    radio::materialize(s, t);
    int idx = radio::findSeg(s, t);
    const radio::Seg& seg = s.segs[(size_t)idx];
    if (seg.type != radio::SegType::Song || seg.song < 0) return std::string();
    const radio::SongInfo& si = s.catalog[(size_t)seg.song];
    return si.artist + " - " + si.title;
}
void radioSetContext(float timeOfDay, float rain) {
    radio::g_ctxTime.store(timeOfDay);
    radio::g_ctxRain.store(rain);
}

}  // namespace detail
}  // namespace Audio
