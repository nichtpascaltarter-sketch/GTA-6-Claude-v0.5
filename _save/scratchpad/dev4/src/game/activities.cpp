// Side activities: street races (night races in Calle Luna and Sol Beach, the Overseas Highway run...), boat races,
// flight school, taxi fares, courier runs, vigilante and paramedic jobs, the shooting range challenge, stunt jumps
// (unique jumps + freestyle stunt bonus) and the 30 signal jammers ("hush boxes") hidden around Porto Sol.
#include "missions.h"

namespace Game {
namespace mu {

// garage helpers (shops.cpp)
int packPaint(vec3 c);
void saveOwnedMods(GameWorld& g, int v);
// handset (phone_game.cpp)
bool phoneWired();
void addMessage(GameWorld& g, const std::string& from, const std::string& text, int contactId, bool mission, int missionDef, vec2* location,
                int action, const char* actionLabel);

// ------------------------------------------------------------------------------------------------------------------
// Small models for activity props (built once, rendered as dynamic draw items)
Render::Model* gTargetModel = nullptr;
Render::Model* gJammerModel = nullptr;
Render::Model* gRingModel = nullptr;

void buildActivityModels(GameWorld& g) {
    if (gTargetModel || !g.renderer) return;
    {
        // shooting target: round board with rings on a post, facing -Y (toward the shooter), origin at the ground
        MeshData m;
        u32 matB = makeMat(MAT_PLASTIC), matP = makeMat(MAT_WOOD);
        m.boxAA(vec3(-0.04f, 0.02f, 0.f), vec3(0.04f, 0.1f, 1.05f), packRGBA8(0.45f, 0.32f, 0.2f, 1.f), matP);
        const float radii[5] = {0.45f, 0.36f, 0.25f, 0.14f, 0.06f};
        const vec3 cols[5] = {vec3(0.95f), vec3(0.1f), vec3(0.15f, 0.35f, 0.9f), vec3(0.9f, 0.1f, 0.1f), vec3(1.f, 0.85f, 0.1f)};
        for (int r = 0; r < 5; r++) {
            float y = -0.002f * r;
            u32 col = packRGBA8(cols[r].x, cols[r].y, cols[r].z, 1.f);
            u32 c = m.addVertex(vec3(0, y, 1.45f), vec3(0, -1, 0), vec3(1, 0, 0), vec2(0.5f, 0.5f), col, matB);
            u32 first = (u32)m.verts.size();
            const int seg = 28;
            for (int i = 0; i <= seg; i++) {
                float a = kTwoPi * i / seg;
                m.addVertex(vec3(cosf(a) * radii[r], y, 1.45f + sinf(a) * radii[r]), vec3(0, -1, 0), vec3(1, 0, 0), vec2(0, 0), col, matB);
            }
            for (int i = 0; i < seg; i++) m.tri(c, first + i, first + i + 1);   // CCW seen from -Y (the shooter)
        }
        m.boxAA(vec3(-0.47f, 0.f, 0.98f), vec3(0.47f, 0.03f, 1.92f), packRGBA8(0.3f, 0.3f, 0.3f, 1.f), matB);
        gTargetModel = g.renderer->dynamic->createModel(m);
    }
    {
        // jammer: grey box with an antenna and a red lamp
        MeshData m;
        u32 matM = makeMat(MAT_METAL_PAINTED), matE = makeMat(MAT_EMISSIVE);
        m.boxAA(vec3(-0.25f, -0.18f, 0.f), vec3(0.25f, 0.18f, 0.34f), packRGBA8(0.3f, 0.32f, 0.33f, 1.f), matM, true);
        m.cylinder(vec3(0.15f, 0.f, 0.34f), 0.012f, 0.008f, 0.55f, 6, packRGBA8(0.15f, 0.15f, 0.15f, 1.f), matM);
        m.boxAA(vec3(-0.08f, -0.2f, 0.22f), vec3(-0.02f, -0.17f, 0.28f), packRGBA8(1.f, 0.05f, 0.03f, 1.f), matE);
        m.boxAA(vec3(-0.2f, -0.19f, 0.05f), vec3(0.2f, -0.18f, 0.14f), packRGBA8(0.9f, 0.8f, 0.2f, 1.f), matM);
        gJammerModel = g.renderer->dynamic->createModel(m);
    }
    {
        // flight school ring: torus in the XZ plane (the aircraft flies along Y through it), emissive
        MeshData m;
        u32 matE = makeMat(MAT_EMISSIVE);
        const int seg = 40, side = 8;
        float R = 1.f, r = 0.06f;
        u32 start = (u32)m.verts.size();
        for (int i = 0; i <= seg; i++) {
            float a = kTwoPi * i / seg;
            vec3 c(cosf(a) * R, 0.f, sinf(a) * R);
            for (int j = 0; j <= side; j++) {
                float b = kTwoPi * j / side;
                vec3 n = normalize(vec3(cosf(a) * cosf(b), sinf(b), sinf(a) * cosf(b)));
                m.addVertex(c + n * r, n, vec3(-sinf(a), 0, cosf(a)), vec2((float)i / seg, (float)j / side), packRGBA8(1, 1, 1, 1), matE);
            }
        }
        for (int i = 0; i < seg; i++)
            for (int j = 0; j < side; j++) {
                u32 a = start + i * (side + 1) + j, b = a + side + 1;
                m.quadIdx(a, a + 1, b + 1, b);   // CCW seen from outside the tube
            }
        gRingModel = g.renderer->dynamic->createModel(m);
    }
}

void drawModel(GameWorld& g, Render::Model* model, vec3 pos, float yaw, vec3 scale, vec3 tint, float emissive, u64 id) {
    if (!model || !g.renderer) return;
    Render::DrawItem di;
    di.model = model;
    di.pos = dvec3(pos);
    di.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), yaw));
    di.scale = scale;
    di.tint0 = vec4(tint, 0.f);
    di.emissiveScale = emissive;
    di.castShadow = true;
    di.id = id;
    g.renderer->dynamic->submit(di);
}

// Ray from the camera through the reticle when the player fired this frame (used by targets and jammers). Computed
// once per simulation step so every caller sees the same shot.
struct ShotEvent {
    double time = -1.0;
    int lastShots = -1;
    bool fired = false;
    vec3 o, d;
};
ShotEvent gShot;

bool playerShotRay(GameWorld& g, vec3& o, vec3& d) {
    if (gShot.time != g.time) {
        gShot.time = g.time;
        int shots = g.pinfo.shotsFired;
        if (gShot.lastShots < 0) gShot.lastShots = shots;
        gShot.fired = shots != gShot.lastShots;
        gShot.lastShots = shots;
        gShot.o = g.rig.cam.pos.toVec3();
        gShot.d = g.rig.cam.forward();
    }
    o = gShot.o;
    d = gShot.d;
    return gShot.fired;
}

// Distance from point p to the ray (o, d); t along the ray.
float rayPointDist(vec3 o, vec3 d, vec3 p, float* tOut) {
    float t = dot(p - o, d);
    if (tOut) *tOut = t;
    if (t < 0.f) return 1e9f;
    return ::length(o + d * t - p);
}

// ------------------------------------------------------------------------------------------------------------------
// Races (street, boat, air). Courses are built from via points on first start.
struct RaceSpec {
    const char* id;
    const char* name;
    int sideFlag;
    int bestSlot;
    int domain;              // 0 road, 1 water, 2 air
    std::vector<vec2> via;
    long long fee, prize;
    int racers;
    float racerSpeed;
    bool night;
};

// Every race has a named rival on the grid (pole position, the fastest car) who talks before, during and after.
struct RaceRival {
    const char* race;
    const char* name;
    const char* persona;     // speech persona key (stock roles or a name: unknown keys get a stable hashed voice)
    bool female;
    u32 color;
    const char* intro;       // on the grid
    const char* taunt;       // mid race, while ahead of the player
    const char* beaten;      // the player won
    const char* gloat;       // the player lost
    // the rivalry: after the first win the rival comes back for rematches; after three, the final puts their car (a
    // trophy on the water) against the prize; once the final is won they greet the player with respect
    const char* rematch;
    const char* finalOffer;
    const char* finalLost;
    const char* respect;
    const char* trophy;      // water rivals: the trophy name ("" for road rivals, who hand over their car)
};
const RaceRival kRivals[] = {
    {"race_calle", "Chuy", "cast_thug_c", false, 0xff4040ffu, "[angry:0.5]You again, repo man? Tonight I win my slip back.",
     "[shout]Eat my exhaust!", "[angry]Rigged. This whole city is rigged.", "[happy]Pink slip energy, baby!",
     "[angry:0.4]Round two. I tuned the engine myself this time.",
     "[calm]Okay. Pink slips. My car against your prize money. Winner drives home in it.",
     "[sad:0.5]Take her. Just change the oil every three thousand. She deserves it.",
     "[calm]You got my car and my respect. Only one of those I want back.", ""},
    {"race_beach", "Nikki Vega", "racer_nikki", true, 0xffff66ccu, "[happy:0.6]Cute car. Shame it'll be looking at my taillights all night.",
     "[happy]Still with me? Adorable.", "[sad:0.4]Okay. Okay. Rematch next Friday.", "[happy]See you at the finish. Oh wait, I'm already there.",
     "[happy:0.5]Rematch, as promised. I brought a faster car and a better attitude. Mostly the car.",
     "[happy:0.4]Let's make it interesting. My title against your prize. Unless you like losing twice.",
     "[sad:0.3]Fine. She's yours. Tell her Nikki says hi.", "[happy:0.4]There's my favorite car thief. Drive nice.", ""},
    {"race_overseas", "Duke Marlow", "redneck", false, 0xff66aaffu, "[calm]Seven miles of bridge, no cops, no brakes. Keep up.",
     "[shout]Keys are mine, city kid!", "[angry:0.4]Well dang. You drive like the bridge owes you money.", "[happy:0.5]Welcome to the Keys, city kid.",
     "[calm]Came back for more bridge, city kid? The Keys don't forgive.",
     "[calm]Title to my ride against your winnings. Seven miles. No excuses.",
     "[sad:0.4]Well. My granddaddy won that title in a card game. Now you won it on a bridge.",
     "[happy:0.3]City kid. Still driving my old ride like it owes you money?", ""},
    {"race_grove", "Preston Hale", "racer_preston", false, 0xffaaddffu, "[calm]Daddy's car, my rules. Try not to scratch the hedges.",
     "[happy:0.5]Money can't buy talent. Oh wait, it can.", "[angry:0.5]This isn't over. My lawyer will hear about this.",
     "[happy:0.4]Told you. Talent and a trust fund.",
     "[angry:0.3]My lawyer says I should stop racing you. My lawyer doesn't drive.",
     "[calm]Pink slips. Daddy has a whole garage of these. He won't even notice.",
     "[scared:0.4]He's going to notice. Oh, he is definitely going to notice.",
     "[calm]Daddy noticed. I take the bus now. Thanks for that.", ""},
    {"race_keys", "Mama Juno", "old_woman", true, 0xff88ffccu, "[happy:0.4]I've been racing this loop since before you were born, sugar.",
     "[happy]Too slow, baby!", "[happy:0.3]Not bad. You'd have beaten me in nineteen eighty too.", "[happy]Still got it!",
     "[happy:0.4]Back again, sugar? I baked a pie for the winner. It's for me.",
     "[happy:0.3]At my age you race for keeps. My car against your purse, sugar.",
     "[happy:0.3]Well I'll be. Take her, baby. She's done her last lap with me.",
     "[happy:0.4]There's my champion. Come by for pie sometime.", ""},
    {"boat_bay", "Captain Ferro", "old_man", false, 0xffffcc66u, "[calm]The bay's choppy tonight. Hold your line and respect the buoys.",
     "[shout]Mind my wake!", "[happy:0.3]Good hands on that wheel. Your father would be proud.", "[happy:0.5]Salt water in your eyes? Happens to everyone.",
     "[calm]Back on the water? Good. The bay needs more people who respect it.",
     "[calm]The Harbor Cup. My trophy against your purse. Old rules.",
     "[happy:0.3]The Harbor Cup is yours. Put it where the sun can find it.",
     "[calm]Cup holder. Mind the buoys like you mean it.", "Harbor Cup"},
    {"boat_river", "Tito Reyes", "racer_tito", false, 0xff66ffffu, "[happy:0.5]Rio Sol's my river. You're just visiting.",
     "[shout]River rat coming through!", "[angry:0.4]Lucky wake. That's all that was.", "[happy]River rat wins again!",
     "[happy:0.4]The river remembers you. So do I.",
     "[calm]The River Crown is on the line. You win, you're king of the Rio Sol.",
     "[sad:0.4]Long live the king. Don't get used to it.",
     "[happy:0.3]Your majesty. Try not to sink my river.", "River Crown"},
};

const RaceRival* rivalFor(const char* raceId) {
    for (const RaceRival& r : kRivals)
        if (strcmp(r.race, raceId) == 0) return &r;
    return nullptr;
}

std::vector<RaceSpec> raceSpecs() {
    std::vector<RaceSpec> v;
    v.push_back({"race_calle", "Calle Luna Sprint", SIDE_RACE_CALLE, 0, 0,
                 {vec2(1100, -850), vec2(1900, -800), vec2(2500, -300), vec2(2450, 450), vec2(1500, 600), vec2(1100, 100), vec2(1150, -700)}, 500, 3000, 3, 30.f, true});
    v.push_back({"race_beach", "Sol Beach Nights", SIDE_RACE_BEACH, 1, 0,
                 {vec2(5150, -2300), vec2(5200, -800), vec2(5150, 1000), vec2(5150, 2800), vec2(5100, 4000)}, 800, 4000, 3, 33.f, true});
    v.push_back({"race_overseas", "Overseas Run", SIDE_RACE_HIGHWAY, 2, 0, {vec2(300, -6000), vec2(-3000, -8300), vec2(-7700, -9150)}, 1500, 8000, 3, 40.f,
                 false});
    v.push_back({"race_grove", "Grove Hills", SIDE_RACE_GROVE, 3, 0,
                 {vec2(1500, -2500), vec2(2800, -2600), vec2(3500, -3800), vec2(2000, -4300), vec2(1200, -3300), vec2(1500, -2600)}, 600, 3500, 3, 30.f, false});
    v.push_back({"race_keys", "Key Solano Loop", SIDE_RACE_KEYS, 4, 0,
                 {vec2(-7700, -9150), vec2(-8200, -9000), vec2(-8350, -9400), vec2(-7800, -9450), vec2(-7650, -9200)}, 400, 2500, 3, 28.f, false});
    v.push_back({"boat_bay", "Bay Regatta", SIDE_BOAT_BAY, 5, 1,
                 {vec2(4300, 600), vec2(4450, 1800), vec2(4600, 3000), vec2(4350, 2300), vec2(4250, 1200), vec2(4350, 400)}, 600, 4000, 3, 24.f, false});
    v.push_back({"boat_river", "Rio Sol Dash", SIDE_BOAT_RIVER, 6, 1,
                 {vec2(1650, 140), vec2(2150, 230), vec2(2600, 110), vec2(3200, 150), vec2(3950, 160), vec2(4300, 300)}, 400, 3000, 3, 20.f, false});
    return v;
}

// One line for the Jobs app: where the rivalry with this race's rival stands ("" when the race has no rival).
std::string rivalStatus(GameWorld& g, const char* raceId) {
    const RaceRival* rr = rivalFor(raceId);
    if (!rr) return "";
    for (const RaceSpec& rs : raceSpecs()) {
        if (strcmp(rs.id, raceId) != 0) continue;
        int wins = flag(g, EX_RIVAL_WINS + rs.bestSlot);
        if ((flag(g, EX_RIVAL_FINALS) >> rs.bestSlot) & 1) return StrFormat("Rival %s: beaten for good.", rr->name);
        if (wins >= 3) return StrFormat("Rival %s: the final is on, %s on the line.", rr->name, rr->trophy[0] ? rr->trophy : "their car");
        return StrFormat("Rival %s: %d of 3 wins to the final.", rr->name, wins);
    }
    return "";
}

class MissionRace : public StoryMission {
public:
    RaceSpec spec;
    RaceCourse race;
    float countdown = 0.f;
    int lastCount = 4;
    float raceTime = 0.f;
    bool paid = false;
    long long won = 0;
    int rivalCar = -1, rivalPed = -1;
    bool taunted = false;
    int rivalWins = 0;        // earlier wins against this race's rival
    bool finalRace = false;   // the rivalry's final: the rival's car (or trophy) is on the line
    bool finalDone = false;   // the final was won before: the rival greets with respect
    long long fee = 0;
    MissionRace(const RaceSpec& s) : spec(s) {}
    const char* title() const override { return spec.name; }
    const char* brief() const override { return "Win the race. Entry fee is paid at the start line; the winner takes the prize money."; }
    long long reward() const override { return won; }
    const char* passBanner() const override { return "RACE WON"; }
    const char* failBanner() const override { return "RACE LOST"; }

    void start(GameWorld& g) override {
        if (spec.night && g.env->timeOfDay > 5.f && g.env->timeOfDay < 20.f) g.env->timeOfDay = 21.f;
        bool water = spec.domain == 1;
        rivalWins = flag(g, EX_RIVAL_WINS + spec.bestSlot);
        finalDone = (flag(g, EX_RIVAL_FINALS) >> spec.bestSlot) & 1;
        finalRace = rivalFor(spec.id) && !finalDone && rivalWins >= 3;
        fee = finalRace ? spec.fee * 2 : spec.fee;
        std::vector<vec2> via = spec.via;
        float wz = 0.f;
        if (water) {
            for (vec2& p : via) {
                vec3 w;
                findWater(g, p, 2.f, w, 250.f);
                p = w.xy();
                wz = w.z;
            }
        }
        race.cpRadius = water ? 16.f : 10.f;
        race.build(g, via, water ? 220.f : 200.f, water, wz);
        if (race.path.pts.size() < 2) return;
        vec2 t0;
        vec3 p0 = race.path.at(6.f, nullptr, &t0);
        float yaw = atan2f(-t0.x, t0.y);
        vec2 right(t0.y, -t0.x);
        int model = water ? pickModel(g, {Vehicles::VC_BOAT, Vehicles::VC_JETSKI}, 0) : -1;
        int pv = g.playerVehicle();
        if (!water && pv >= 0 && !g.isBoat(pv) && !g.isAircraft(pv)) {
            playerCar = pv;
            teleportVehicle(g, pv, p0 + vec3(right * -2.4f, 0.f), yaw);
        } else {
            if (model < 0) model = pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_MUSCLE}, 1);
            playerCar = placePlayer(g, p0 + vec3(right * -2.4f, water ? 0.f : 0.f), yaw, model);
        }
        for (int i = 0; i < spec.racers; i++) {
            int m = water ? pickModel(g, {Vehicles::VC_BOAT, Vehicles::VC_JETSKI}, (u32)i + 1)
                          : pickModel(g, {i == 0 ? Vehicles::VC_SPORTS : (i == 1 ? Vehicles::VC_MUSCLE : Vehicles::VC_COUPE)}, (u32)i);
            if (m < 0) m = model;
            vec3 sp = race.path.at(6.f - 9.f * ((i + 1) / 2)) + vec3(right * (i % 2 == 0 ? 2.4f : -2.4f), 0.f);
            if (i == 0) sp = p0 + vec3(right * 2.4f, 0.f);
            int v = spawnCar(g, m, sp, yaw);
            if (v < 0) continue;
            int drv = g.mPed(g.randomCivilianChar(0xACE0u + (u32)i * 13u + hashString(spec.id), 0), dvec3(sp), yaw, FAC_CIVILIAN);
            if (drv >= 0) g.warpPedIntoVehicle(drv, v, 0);
            race.racers.push_back(v);
            if (i == 0) {
                // pole position: the rival, in the quickest car
                rivalCar = v;
                rivalPed = drv;
                if (const RaceRival* rr = rivalFor(spec.id)) {
                    if (drv >= 0) g.peds[drv].voice = Speech::persona(rr->persona, rr->female).voice;
                    g.vehicles[v].color0 = lin(((rr->color) & 255) / 255.f, ((rr->color >> 8) & 255) / 255.f, ((rr->color >> 16) & 255) / 255.f);
                }
            }
            float rivalPace = finalRace ? 3.6f : (rivalWins > 0 ? 3.0f : 2.6f);   // the rival brings more each time
            ScriptDriver& d = addDriver(g, v, race.path, spec.racerSpeed + (i == 0 ? rivalPace : i * 1.2f), water ? DRV_WATER : DRV_ROAD, 0);
            d.racer = true;
            d.rubberPed = g.player;
            d.speedScale = 0.f;
        }
        if (!paid) {
            money(g, -fee);
            paid = true;
        }
        const RaceRival* rr = rivalFor(spec.id);
        if (finalRace && rr)
            g.mObjective(StrFormat("~y~%s~s~  Rivalry final: $%lld buy-in against %s's %s", spec.name, fee, rr->name, rr->trophy[0] ? rr->trophy : "car"));
        else
            g.mObjective(StrFormat("~y~%s~s~  Entry fee $%lld, prize $%lld", spec.name, fee, spec.prize));
        // the grid: a low shot of the rival's car, then behind the player's, while the rival says their piece
        std::vector<CutsceneShot> shots;
        if (rivalCar >= 0) {
            vec3 rp = vehPos(g, rivalCar);
            vec3 f(t0, 0.f), r(right, 0.f);
            shots.push_back(shotMove(rp + f * 9.f + r * 3.f + vec3(0.f, 0.f, 0.7f), rp + vec3(0.f, 0.f, 0.8f), rp + f * 7.f + r * 1.5f + vec3(0.f, 0.f, 0.9f),
                                     rp + vec3(0.f, 0.f, 0.9f), 3.2f, 42.f));
        }
        if (playerCar >= 0) shots.push_back(shotVehicle(g, playerCar, 2.2f, -1.f, 48.f));
        if (!shots.empty()) g.mCutscene(shots);
        rivalSay(g, 0);
        score(SC_CHASE, 0.45f, 5 + spec.bestSlot);
        countdown = 4.f;
        lastCount = 4;
        setStage(1);
    }

    // 0 intro, 1 taunt, 2 beaten by the player, 3 gloat
    void rivalSay(GameWorld& g, int which) {
        const RaceRival* rr = rivalFor(spec.id);
        if (!rr) return;
        const char* opener = finalDone ? rr->respect : (finalRace ? rr->finalOffer : (rivalWins > 0 ? rr->rematch : rr->intro));
        const char* text = which == 0 ? opener : (which == 1 ? rr->taunt : (which == 2 ? (finalRace ? rr->finalLost : rr->beaten) : rr->gloat));
        DialogueLine l = line(rr->name, text, pedAlive(g, rivalPed) ? rivalPed : -1, rr->color);
        Speech::Persona p = Speech::persona(rr->persona, rr->female);
        l.hasVoice = true;
        l.voice = p.voice;
        l.spoken = p.tags() + speakableText(text);
        g.mSay(l);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, playerCar, spec.domain == 1 ? "boat" : "car")) return MS_FAILED;
        switch (stage) {
            case 1: {
                if (playerCar >= 0 && g.vehicles[playerCar].sim.speed() > 0.5f) g.vehicles[playerCar].sim.body.vel *= 0.5f;
                if (g.mInCutscene()) break;   // the grid shots first
                countdown -= dt;
                int c = (int)ceilf(countdown);
                if (c != lastCount && c >= 1 && c <= 3) {
                    lastCount = c;
                    g.bigMessage(StrFormat("%d", c), spec.name, 0xffffffffu);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_COUNTDOWN, 0.9f);
#endif
                }
                if (playerCar >= 0 && countdown > 0.f && g.vehicles[playerCar].sim.speed() > 0.5f) g.vehicles[playerCar].sim.body.vel *= 0.5f;
                if (countdown <= 0.f) {
                    g.bigMessage("GO!", "", 0xff33ff66u);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_GO, 0.9f);
#endif
                    for (int v : race.racers)
                        if (ScriptDriver* d = driverFor(v)) d->speedScale = 1.f;
                    race.showMarkers(g);
                    score(SC_CHASE, 0.95f, 5 + spec.bestSlot);
                    next();
                }
                break;
            }
            case 2: {
                raceTime += dt;
                if (g.playerVehicle() < 0) {
                    timer += dt;
                    if (timer > 12.f) return fail("You left the race.");
                    if (g.hudHelpTimer <= 0.f) g.help("Get back in and finish the race.", 2.f);
                } else timer = 0.f;
                if (race.updatePlayer(g)) race.showMarkers(g);
                int pos = race.position(g);
                if (!taunted && race.next >= (int)race.checkpoints.size() / 3 && pos > 1 && rivalCar >= 0 &&
                    ::length(vehPos(g, rivalCar) - playerPos(g)) < 60.f) {
                    taunted = true;
                    rivalSay(g, 1);
                }
                g.missionCounterLabel = "POSITION";
                g.missionCounter = pos;
                g.missionCounterMax = (int)race.racers.size() + 1;
                g.mObjective(StrFormat("~y~%s~s~  Checkpoint %d/%d   %d:%04.1f", spec.name, race.next, (int)race.checkpoints.size(), (int)(raceTime / 60.f),
                                       fmodf(raceTime, 60.f)));
                if (race.next >= (int)race.checkpoints.size()) {
                    g.missionCounterLabel.clear();
                    int cs = (int)(raceTime * 100.f);
                    int slot = EX_RACE_BEST + spec.bestSlot;
                    bool record = flag(g, slot) == 0 || cs < flag(g, slot);
                    if (record) setFlag(g, slot, cs);
                    if (pos == 1) {
                        won = spec.prize + (record ? 500 : 0);
                        setFlag(g, spec.sideFlag, 1);
                        g.notify("RACE WON", StrFormat("%s  %d:%04.1f%s", spec.name, (int)(raceTime / 60.f), fmodf(raceTime, 60.f), record ? "  NEW RECORD" : ""));
                        g.socialReport(UI::TE_RACE_WON, dvec3(playerPos(g)), spec.name);
                        rivalSay(g, 2);
                        winRivalry(g);
                        for (int v : race.racers) releaseDriver(g, v);
                        return MS_PASSED;
                    }
                    rivalSay(g, 3);
                    return fail(StrFormat("You finished %d%s.", pos, pos == 2 ? "nd" : (pos == 3 ? "rd" : "th")).c_str());
                }
                break;
            }
        }
        return MS_RUNNING;
    }

    // a win against the rival: count it; the final hands over the rival's car (into the garages, in the rival's colours
    // with their tuning) or, on the water, their trophy and a purse
    void winRivalry(GameWorld& g) {
        const RaceRival* rr = rivalFor(spec.id);
        if (!rr) return;
        setFlag(g, EX_RIVAL_WINS + spec.bestSlot, Min(rivalWins + 1, 99));
        LOG("[rivals] %s beaten in %s: %d win%s%s", rr->name, spec.name, rivalWins + 1, rivalWins ? "s" : "", finalRace ? ", the final" : "");
        if (!finalRace) {
            if (rivalWins + 1 == 3) {
                g.notify(rr->name, StrFormat("%s wants a final: next time the %s is on the line.", rr->name, rr->trophy[0] ? rr->trophy : "pink slip"));
                if (phoneWired()) {
                    int di = gMissions.findDef(spec.id);
                    vec2 loc = di >= 0 ? gMissions.defs[di].startPos : playerPos(g).xy();
                    std::string txt = rr->trophy[0] ? StrFormat("Three times? Fine. Next race the %s is on the line. Old rules.", rr->trophy)
                                                    : std::string("Three times? Fine. Next race, pink slips. My car against your prize. Don't chicken out.");
                    addMessage(g, rr->name, txt, -1, false, -1, &loc, 0, nullptr);
                }
            }
            return;
        }
        setFlag(g, EX_RIVAL_FINALS, flag(g, EX_RIVAL_FINALS) | (1 << spec.bestSlot));
        bool all = true;
        for (const RaceSpec& rs : raceSpecs())
            if (rivalFor(rs.id) && !((flag(g, EX_RIVAL_FINALS) >> rs.bestSlot) & 1)) all = false;
        if (all) setFlag(g, SIDE_RIVALS_ALL, 1);
        int m = rivalCar >= 0 && g.vehicles[rivalCar].used ? g.vehicles[rivalCar].model : -1;
        bool road = spec.domain == 0 && !rr->trophy[0];
        if (road && m >= 0 && std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), m) == g.ownedVehicleModels.end()) {
            Vehicle& rv = g.vehicles[rivalCar];
            g.ownedVehicleModels.push_back(m);
            int slot = (int)g.ownedVehicleModels.size() - 1;
            if (slot < 40) setFlag(g, EX_VEHICLE_PAINT + slot, packPaint(rv.color0));
            // the rival's tuning comes with it
            rv.mods.engine = 2;
            rv.mods.transmission = 2;
            rv.mods.turbo = true;
            rv.mods.finish = 1;
            rv.mods.neon = vec3((rr->color & 255) / 255.f, ((rr->color >> 8) & 255) / 255.f, ((rr->color >> 16) & 255) / 255.f);   // sRGB, like the kits
            saveOwnedMods(g, rivalCar);
            LOG("[rivals] pink slip: %s's %s joins the garages (slot %d)", rr->name, g.vassets[m].spec.name.c_str(), slot);
            g.notify("PINK SLIP", StrFormat("%s's car is yours, tuned as they left it. It waits in every safehouse garage.", rr->name));
        } else {
            long long purse = 10000;
            won += purse;
            g.notify(rr->trophy[0] ? rr->trophy : "PINK SLIP",
                     rr->trophy[0] ? StrFormat("The %s is yours, and a $%lld purse.", rr->trophy, purse)
                                   : StrFormat("You already own one like %s's car. They paid out $%lld instead.", rr->name, purse));
        }
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (stage == 2 && race.next < (int)race.checkpoints.size() && fmodf(t.stageTime, 0.5f) < g.dtLast && t.stageTime > 0.3f) {
            vec2 tan;
            race.path.at(race.cpAlong[race.next], nullptr, &tan);
            t.teleport(race.checkpoints[race.next] - vec3(tan * 3.f, 0.f), atan2f(-tan.x, tan.y));
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Porto Sol Night Series (either protagonist): Lalo Brisa's three-leg championship in the series' own cars - tuner
// coupes through Calle Luna, American muscle in Grove Hills, exotics down Sol Beach. Points after every leg (10, 6, 3,
// 1); the champion takes the purse and, the first time, the Arclight from the final leg. A retry restarts the failed
// leg with the points table as it stood.
namespace series_detail {

struct SeriesLeg {
    const char* name;
    const char* place;
    const char* car;              // the player's series car (model name; any car of the class when missing)
    const char* rivalCar;         // the class's other model on the grid
    Vehicles::VehicleClass cls;
    float pace;                   // the series drivers' cruise speed (m/s)
    std::vector<vec2> via;
};

std::vector<SeriesLeg> legs() {
    return {{"Tuner Night", "Calle Luna", "Kaito", "Cavell GT", Vehicles::VC_COUPE, 30.f,
             {vec2(1100, -850), vec2(2000, -700), vec2(2500, -300), vec2(2450, 450), vec2(1900, 600), vec2(1300, 200), vec2(1250, -450)}},
            {"American Muscle", "Grove Hills", "Scorch 392", "Gatorback '70", Vehicles::VC_MUSCLE, 32.f,
             {vec2(1500, -2600), vec2(1200, -3300), vec2(2000, -4300), vec2(3500, -3800)}},
            {"Exotics", "Sol Beach", "Arclight", "Orsa V12", Vehicles::VC_SUPER, 36.f,
             {vec2(5200, -800), vec2(5150, 1000), vec2(5150, 2800), vec2(5100, 4000)}}};
}

struct SeriesDriver {
    const char* name;
    const char* shortName;
    const char* persona;
    bool female;
    u32 color;                    // 0xAABBGGRR, like the race rivals
    const char* grid[3];          // on pole for that leg
    const char* taunt;            // ahead of the player mid leg
    const char* legWin;           // took a leg
    const char* title;            // took the championship
    const char* beaten;           // the player took the championship
};

const SeriesDriver kDrivers[3] = {
    {"Nikki Vega", "Nikki", "racer_nikki", true, 0xffff66ccu,
     {"[happy:0.5]Three legs, three cars, one Nikki. Try to make it interesting.",
      "[calm]Muscle cars. All engine, no manners. Just like Preston.",
      "[happy:0.6]Now we're talking. I have been dreaming about this one."},
     "[happy]Keep up, wildcard!", "[happy]That's how it's done, sugar.", "[happy]Series champion. Somebody get me a bigger shelf.",
     "[sad:0.3]Okay. Champion. Enjoy it. I'll be back next season."},
    {"Preston Hale", "Preston", "racer_preston", false, 0xffaaddffu,
     {"[calm]I had this coupe detailed twice. Please don't breathe on it.",
      "[happy:0.4]American muscle. Daddy says it's vulgar. I love it.",
      "[calm]An Arclight. I have two at home. Neither of them is this fast."},
     "[happy:0.5]Money can't buy talent. Oh wait.", "[happy:0.5]Talent and a trust fund. Undefeated combination.",
     "[happy]Champion. Somebody call my lawyer, I want this notarized.", "[angry:0.4]I'm appealing this. On what grounds? I'll think of something."},
    {"Duke Marlow", "Duke", "redneck", false, 0xff66aaffu,
     {"[calm]Little import thing. Feels like driving a toaster.",
      "[happy:0.5]Now this is a car. Eight cylinders and a bad attitude.",
      "[calm]Fancy Italian stuff. Let's see if it holds a line on the beach road."},
     "[shout]Coming through, city kid!", "[happy:0.4]Keys trained, city tested.", "[happy]Series champ. My granddaddy would have framed this.",
     "[calm]Fair's fair. You drove it like you stole it. Which, I hear, is your job."},
};

const int kPoints[4] = {10, 6, 3, 1};
const long long kFee = 1500;
const u32 kColLalo = 0xff30b0ffu;
const vec3 kLivery = vec3(0.62f, 0.05f, 0.3f);   // Night Series magenta (linear)

int driverChar(GameWorld& g, int i) {
    Anim::CharacterDesc d;
    d.seed = 0x5E41E5u + (u32)i * 977u;
    if (i == 0) {
        d.gender = Anim::FEMALE;
        d.height = 1.68f;
        d.weight = 0.35f;
        d.muscle = 0.35f;
        d.age = 0.22f;
        d.skinTone = vec3(0.62f, 0.44f, 0.32f);
        d.hairStyle = 4;
        d.hairColor = vec3(0.3f, 0.06f, 0.2f);
        d.top = 15;
        d.topColor = lin(0.95f, 0.35f, 0.7f);
        d.bottom = 0;
        d.bottomColor = lin(0.06f, 0.06f, 0.08f);
        d.shoes = 0;
        d.shoeColor = lin(0.95f, 0.95f, 0.95f);
        d.role = 4;
        return g.namedCharacter("series_nikki", d);
    }
    if (i == 1) {
        d.gender = Anim::MALE;
        d.height = 1.83f;
        d.weight = 0.4f;
        d.muscle = 0.4f;
        d.age = 0.24f;
        d.skinTone = vec3(0.9f, 0.76f, 0.65f);
        d.hairStyle = 10;
        d.hairColor = vec3(0.75f, 0.6f, 0.35f);
        d.top = 2;
        d.topColor = lin(0.55f, 0.8f, 0.95f);
        d.bottom = 3;
        d.bottomColor = lin(0.8f, 0.75f, 0.62f);
        d.shoes = 1;
        d.shoeColor = lin(0.35f, 0.2f, 0.1f);
        d.glasses = 1;
        d.role = 3;
        return g.namedCharacter("series_preston", d);
    }
    d.gender = Anim::MALE;
    d.height = 1.85f;
    d.weight = 0.7f;
    d.muscle = 0.6f;
    d.age = 0.55f;
    d.skinTone = vec3(0.8f, 0.6f, 0.48f);
    d.hairStyle = 2;
    d.hairColor = vec3(0.4f, 0.3f, 0.2f);
    d.facialHair = 3;
    d.top = 4;
    d.topColor = lin(0.6f, 0.15f, 0.1f);
    d.bottom = 0;
    d.bottomColor = lin(0.2f, 0.25f, 0.4f);
    d.shoes = 2;
    d.shoeColor = lin(0.3f, 0.2f, 0.1f);
    d.hat = 0;
    d.role = 5;
    return g.namedCharacter("series_duke", d);
}

int laloChar(GameWorld& g) {
    Anim::CharacterDesc d;
    d.seed = 0x1A10u;
    d.gender = Anim::MALE;
    d.height = 1.74f;
    d.weight = 0.55f;
    d.muscle = 0.35f;
    d.age = 0.45f;
    d.skinTone = vec3(0.55f, 0.38f, 0.27f);
    d.hairStyle = 8;
    d.hairColor = vec3(0.04f, 0.03f, 0.03f);
    d.facialHair = 1;
    d.top = 6;
    d.topColor = lin(0.62f, 0.05f, 0.3f);
    d.bottom = 3;
    d.bottomColor = lin(0.04f, 0.04f, 0.05f);
    d.shoes = 1;
    d.shoeColor = lin(0.02f, 0.02f, 0.02f);
    d.glasses = 0;
    d.role = 3;
    return g.namedCharacter("series_lalo", d);
}

void laloSay(GameWorld& g, int ped, const std::string& text, float pause = 0.25f) {
    DialogueLine l = line("Lalo Brisa", text, pedAlive(g, ped) ? ped : -1, kColLalo);
    Speech::Persona p = Speech::persona("promoter_lalo", false);
    l.hasVoice = true;
    l.voice = p.voice;
    l.spoken = p.tags() + "[accent:latino:0.5]" + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

void driverSay(GameWorld& g, int i, int ped, const char* text) {
    const SeriesDriver& D = kDrivers[Clamp(i, 0, 2)];
    DialogueLine l = line(D.name, text, pedAlive(g, ped) ? ped : -1, D.color);
    Speech::Persona p = Speech::persona(D.persona, D.female);
    l.hasVoice = true;
    l.voice = p.voice;
    l.spoken = p.tags() + speakableText(text);
    g.mSay(l);
}

vec3 driverColor(int i) {
    u32 c = kDrivers[Clamp(i, 0, 2)].color;
    return lin((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f);
}

int seriesModel(GameWorld& g, const char* name, Vehicles::VehicleClass cls, u32 seed) {
    int m = modelByName(g, name);
    if (m < 0) m = pickModel(g, {cls}, seed);
    if (m < 0) m = pickModel(g, {Vehicles::VC_SPORTS, Vehicles::VC_COUPE, Vehicles::VC_SEDAN}, seed);
    return m;
}

const char* ordinal(int n) {
    static const char* const kOrd[5] = {"", "first", "second", "third", "fourth"};
    return kOrd[Clamp(n, 0, 4)];
}

// A mission entity dropped mid mission (between legs): despawned and forgotten by the mission's cleanup lists.
void dropPed(GameWorld& g, int p) {
    if (p < 0 || p >= (int)g.peds.size() || !g.peds[p].used || g.peds[p].isPlayer) return;
    g.despawnPed(p);
    std::vector<int>& P = gMissions.peds;
    P.erase(std::remove(P.begin(), P.end(), p), P.end());
}

void dropVehicle(GameWorld& g, int v) {
    if (v < 0 || v >= (int)g.vehicles.size() || !g.vehicles[v].used) return;
    releaseDriver(g, v);
    std::vector<int>& P = gMissions.peds;
    for (int s = 0; s < 8; s++) {
        int p = g.vehicles[v].seats[s];
        if (p >= 0 && !g.peds[p].isPlayer) P.erase(std::remove(P.begin(), P.end(), p), P.end());
    }
    g.despawnVehicle(v, true);
    std::vector<int>& V = gMissions.vehicles;
    V.erase(std::remove(V.begin(), V.end(), v), V.end());
}

}  // namespace series_detail

class MissionNightSeries : public StoryMission {
public:
    int leg = 0;
    int pts[4] = {0, 0, 0, 0};        // 0 the player, 1..3 the series drivers
    int lastPlace[4] = {0, 0, 0, 0};  // places in the leg just run (tie-breaks)
    RaceCourse race;
    int lalo = -1, showCar = -1;
    int aiCar[3] = {-1, -1, -1}, aiPed[3] = {-1, -1, -1};
    float aiDone[3] = {-1.f, -1.f, -1.f};
    float countdown = 0.f, legTime = 0.f;
    int lastCount = 4;
    bool taunted = false, flagCalled = false;
    long long won = 0;
    const char* title() const override { return "Porto Sol Night Series"; }
    const char* brief() const override {
        return "Lalo Brisa's Night Series: three legs in three classes of his cars, points after every leg. Take the championship.";
    }
    long long reward() const override { return won; }
    const char* passBanner() const override { return "SERIES CHAMPION"; }
    const char* failBanner() const override { return "SERIES LOST"; }

    // checkpoint: the leg to run and the points table so far
    int encode() const { return 1 + leg + 4 * (pts[0] + 64 * (pts[1] + 64 * (pts[2] + 64 * pts[3]))); }
    void decode(int c) {
        c -= 1;
        leg = c % 4;
        c /= 4;
        for (int i = 0; i < 4; i++) {
            pts[i] = c % 64;
            c /= 64;
        }
        leg = Clamp(leg, 0, 2);
    }
    const char* me(GameWorld& g) const { return g.protagonistIndex == 0 ? "Mari" : "Dex"; }

    void start(GameWorld& g) override {
        using namespace series_detail;
        if (g.env->timeOfDay > 5.f && g.env->timeOfDay < 20.f) g.env->timeOfDay = 21.f;
        gMissions.suppressPolice = true;   // Lalo pays the precinct to look the other way on series nights
        if (checkpoint > 0) {
            decode(checkpoint);
            setupLeg(g);
            return;
        }
        money(g, -kFee);
        // Tide Customs, Calle Luna: the final leg's Arclight on the service lift, Lalo beside it
        InteriorStage in = interiorStage("Tide Customs Calle Luna");
        vec3 liftL;
        std::vector<SeriesLeg> L = legs();
        int prize = seriesModel(g, L[2].car, L[2].cls, 0);
        vec3 lp, mp;
        std::vector<CutsceneShot> shots;
        bool inBay = in.ok() && in.marker(World::IM_SERVICE, liftL);
        if (inBay) {
            // the drive-in lane between the roll-up door and the lift is clear by design: Lalo at the car's nose, the
            // player a few steps in from the door, the camera along the lane (the shop floor around it has pillars)
            float side = liftL.x > (in.d->x0 + in.d->x1) * 0.5f ? 1.f : -1.f;   // the wall beside the drive-in bay
            showCar = spawnCar(g, prize, in.at(liftL), in.yaw(kPi), kLivery);
            if (showCar >= 0) {
                g.vehicles[showCar].parked = true;   // on show: it must not roll off the lift into Lalo
                g.vehicles[showCar].sim.engineOn = false;
            }
            vec3 laloL(liftL.x + side * 0.9f, Max(1.9f, liftL.y - 3.1f), 0.f);
            vec3 meL(liftL.x - side * 0.2f, Max(1.1f, liftL.y - 5.1f), 0.f);
            lp = in.at(laloL);
            mp = in.at(meL);
            lalo = g.mPed(laloChar(g), dvec3(lp), in.yaw(kPi), FAC_FRIEND);
            placePlayer(g, mp, 0.f);
            // from the shop floor in front of the lift posts, diagonally toward the door (the posts stand beside the car)
            shots.push_back(shotRoom(in, vec3(liftL.x - side * 4.f, Max(1.5f, liftL.y - 1.2f), 1.9f), lp, mp, 6.5f));
        } else {
            // no shop interior in this world: the curb outside
            const Place& T = gPlaces.resprayCL;
            float yaw = T.curbYaw;
            vec3 cp0 = curbOffset(g, T, -6.f, &yaw);
            showCar = spawnCar(g, prize, cp0, yaw, kLivery);
            if (showCar >= 0) g.vehicles[showCar].parked = true;
            lp = placeOffset(g, T, -2.f, 1.5f);
            mp = placeOffset(g, T, -3.5f, 3.5f);
            lalo = g.mPed(laloChar(g), dvec3(lp), 0.f, FAC_FRIEND);
            placePlayer(g, mp, 0.f);
            establish(g, shots, lp, T.yaw, 14.f, 4.f, 4.f);
        }
        if (lalo >= 0) g.peds[lalo].brain.type = BRAIN_NONE;
        facePed(g, lalo, mp);
        facePed(g, g.player, lp);
        if (inBay) {
            // over the player's shoulder at Lalo and the car, then back over his at the player and the street, then a
            // low reveal of the prize from its nose
            shots.push_back(shotOver(mp, lp, 6.f));
            shots.push_back(shotOver(lp, mp, 5.f, -1.f));
            vec3 nose = in.at(liftL + vec3(0.f, -2.1f, 0.6f)), mid = in.at(liftL + vec3(0.f, 0.f, 0.7f));
            vec3 low = in.at(liftL + vec3(1.4f, -4.3f, 0.5f)), low2 = in.at(liftL + vec3(0.9f, -3.9f, 0.9f));
            shots.push_back(shotMove(low, nose, low2, mid, 5.f, 40.f));
        } else {
            shots.push_back(shotTwo(lp, mp, 6.f));
            if (showCar >= 0) shots.push_back(shotVehicle(g, showCar, 5.f, 1.f, 44.f));
        }
        g.mCutscene(shots);
        int titles = flag(g, EX_SERIES_WINS);
        if (titles == 0) {
            laloSay(g, lalo, "[happy:0.6]There's my wildcard! Welcome to the Porto Sol Night Series. Three legs, three classes, my cars.");
            laloSay(g, lalo, "[calm]Tuners in Calle Luna, muscle in Grove Hills, and this beauty down Sol Beach. Ten points a win, six for second.");
            sayMe(g, "[calm]And the prize?");
            laloSay(g, lalo, "[happy:0.5]Twenty grand, and the champion drives her home. The precinct has been paid to look the other way. Drive like you mean it.");
        } else {
            laloSay(g, lalo, "[happy:0.5]The champion returns! Same rules, same cars. The Arclight's already yours, so tonight it's for the purse.");
            sayMe(g, "[happy:0.3]And the bragging rights.");
            laloSay(g, lalo, "[happy:0.4]Those are free. Twelve grand isn't. Go.");
        }
        g.mObjective("");
        setStage(0);
    }

    // the grid of the current leg: the course, the player in the series car, the three series drivers
    void setupLeg(GameWorld& g) {
        using namespace series_detail;
        dropPed(g, lalo);
        lalo = -1;
        dropVehicle(g, showCar);
        showCar = -1;
        for (int i = 0; i < 3; i++) {
            dropVehicle(g, aiCar[i]);
            aiCar[i] = aiPed[i] = -1;
            aiDone[i] = -1.f;
        }
        std::vector<SeriesLeg> L = legs();
        const SeriesLeg& S = L[leg];
        race = RaceCourse();
        race.cpRadius = 10.f;
        race.build(g, S.via, 200.f);
        cp(g, encode());
        if (race.path.pts.size() < 2) {
            setStage(9);
            return;
        }
        vec2 t0;
        vec3 p0 = race.path.at(18.f, nullptr, &t0);
        vec3 p1 = race.path.at(9.f);
        float yaw = atan2f(-t0.x, t0.y);
        vec2 right(t0.y, -t0.x);
        int pm = seriesModel(g, S.car, S.cls, 0), rm = seriesModel(g, S.rivalCar, S.cls, 1);
        int old = playerCar;
        playerCar = placePlayer(g, p0 + vec3(right * -2.4f, 0.f), yaw, pm, kLivery);
        if (old >= 0 && old != playerCar) dropVehicle(g, old);
        for (int i = 0; i < 3; i++) {
            int slot = (i - leg + 3) % 3;   // the pole rotates: Nikki, then Preston, then Duke
            vec3 sp = slot == 0 ? p0 + vec3(right * 2.4f, 0.f) : p1 + vec3(right * (slot == 1 ? -2.4f : 2.4f), 0.f);
            int v = spawnCar(g, slot == 1 ? pm : rm, sp, yaw, driverColor(i));
            if (v < 0) continue;
            int drv = g.mPed(driverChar(g, i), dvec3(sp), yaw, FAC_CIVILIAN);
            if (drv >= 0) {
                g.warpPedIntoVehicle(drv, v, 0);
                g.peds[drv].voice = Speech::persona(kDrivers[i].persona, kDrivers[i].female).voice;
            }
            aiCar[i] = v;
            aiPed[i] = drv;
            race.racers.push_back(v);
            ScriptDriver& d = addDriver(g, v, race.path, S.pace + (slot == 0 ? 1.6f : 0.8f * slot), DRV_ROAD, 0);
            d.racer = true;
            d.rubberPed = g.player;
            d.speedScale = 0.f;
        }
        taunted = flagCalled = false;
        legTime = 0.f;
        timer = 0.f;
        g.mClearMarkers();
        g.mClearTarget();
        g.mObjective(StrFormat("~y~Leg %d of 3: %s~s~  %s", leg + 1, S.name, S.place));
        setStage(8);   // the grid comes up once the world around it has streamed in
    }

    bool streamed(GameWorld& g) const { return !g.renderer || !g.renderer->world || g.renderer->world->pendingCount() <= 2; }

    // the grid: the pole sitter's car low and close, then behind the player's; Lalo calls the leg
    void gridIntro(GameWorld& g) {
        using namespace series_detail;
        vec2 t0;
        race.path.at(18.f, nullptr, &t0);
        vec2 right(t0.y, -t0.x);
        std::vector<CutsceneShot> shots;
        int pole = aiCar[leg % 3];
        if (pole >= 0) {
            vec3 rp = vehPos(g, pole);
            vec3 f(t0, 0.f), r(right, 0.f);
            shots.push_back(shotMove(rp + f * 9.f + r * 3.f + vec3(0.f, 0.f, 0.7f), rp + vec3(0.f, 0.f, 0.8f), rp + f * 7.f + r * 1.5f + vec3(0.f, 0.f, 0.9f),
                                     rp + vec3(0.f, 0.f, 0.9f), 3.4f, 42.f));
        }
        if (playerCar >= 0) shots.push_back(shotVehicle(g, playerCar, 2.6f, -1.f, 48.f));
        if (!shots.empty()) g.mCutscene(shots);
        static const char* const kCalls[3] = {
            "[shout:0.5]Leg one, Tuner Night! Four coupes, one Calle Luna, zero brakes!",
            "[shout:0.5]Leg two, American Muscle! Grove Hills, hold on to your hedges!",
            "[shout:0.6]The final leg! Exotics down Sol Beach, and the title's on the line!"};
        laloSay(g, -1, kCalls[leg]);
        driverSay(g, leg % 3, aiPed[leg % 3], kDrivers[leg % 3].grid[leg]);
        score(SC_CHASE, 0.45f, 5 + leg);
        countdown = 4.f;
        lastCount = 4;
        setStage(1);
    }

    // standings: who (0 the player, 1..3 the drivers) by rank; ties go to the better place in the last leg
    std::vector<int> standings() const {
        std::vector<int> t = {0, 1, 2, 3};
        std::stable_sort(t.begin(), t.end(), [this](int a, int b) {
            if (pts[a] != pts[b]) return pts[a] > pts[b];
            return lastPlace[a] < lastPlace[b];
        });
        return t;
    }

    std::string table(GameWorld& g) const {
        std::string s;
        std::vector<int> t = standings();
        for (int k = 0; k < 4; k++) {
            const char* n = t[k] == 0 ? me(g) : series_detail::kDrivers[t[k] - 1].shortName;
            s += StrFormat("%s%d. %s %d", k ? "   " : "", k + 1, n, pts[t[k]]);
        }
        return s;
    }

    void scoreLeg(GameWorld& g) {
        using namespace series_detail;
        // finishing order: drivers who crossed the line (earliest first), the player, then the rest by distance covered
        std::vector<std::pair<float, int>> order;
        float len = race.path.length();
        order.push_back({legTime, 0});
        for (int i = 0; i < 3; i++) {
            float key;
            if (aiDone[i] >= 0.f) key = aiDone[i];
            else {
                ScriptDriver* d = aiCar[i] >= 0 ? driverFor(aiCar[i]) : nullptr;
                key = legTime + 1.f + (len - (d ? d->along : 0.f));
            }
            order.push_back({key, i + 1});
        }
        std::stable_sort(order.begin(), order.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first < b.first; });
        for (int p = 0; p < 4; p++) {
            lastPlace[order[p].second] = p + 1;
            pts[order[p].second] += kPoints[p];
        }
        int mine = lastPlace[0];
        LOG("[series] leg %d finished %s in %.1f s; points %d / %d / %d / %d", leg + 1, ordinal(mine), legTime, pts[0], pts[1], pts[2], pts[3]);
        if (mine == 1) {
            laloSay(g, -1, StrFormat("[shout:0.6]Leg %d goes to %s! Ten points!", leg + 1, me(g)));
        } else {
            int w = order[0].second - 1;
            laloSay(g, -1, StrFormat("[happy:0.5]%s takes leg %d! %s in %s place, %d point%s.", kDrivers[w].name, leg + 1, me(g), ordinal(mine),
                                     kPoints[mine - 1], kPoints[mine - 1] == 1 ? "" : "s"));
            driverSay(g, w, aiPed[w], kDrivers[w].legWin);
        }
        g.bigMessage(StrFormat("LEG %d: %s", leg + 1, mine == 1 ? "WON" : (mine == 2 ? "2ND" : (mine == 3 ? "3RD" : "4TH"))), table(g),
                     mine == 1 ? 0xff33ff66u : 0xffffffffu);
        g.notify("NIGHT SERIES", table(g));
        // a cool-down lap for the others
        for (int i = 0; i < 3; i++)
            if (aiCar[i] >= 0)
                if (ScriptDriver* d = driverFor(aiCar[i])) d->speedScale = 0.45f;
    }

    void awardCar(GameWorld& g) {
        int m = vehicleAlive(g, playerCar) ? g.vehicles[playerCar].model : -1;
        bool owned = m < 0 || std::find(g.ownedVehicleModels.begin(), g.ownedVehicleModels.end(), m) != g.ownedVehicleModels.end();
        if (owned || g.ownedVehicleModels.size() >= 40) {
            won += 8000;
            g.notify("NIGHT SERIES", "Your garages already hold that car. Lalo paid out $8,000 instead.");
            return;
        }
        Vehicle& v = g.vehicles[playerCar];
        g.ownedVehicleModels.push_back(m);
        v.mods.engine = 2;
        v.mods.transmission = 2;
        v.mods.turbo = true;
        v.mods.finish = 1;
        v.mods.neon = vec3(1.f, 0.3f, 0.7f);   // sRGB, like the kits
        saveOwnedMods(g, playerCar);
        LOG("[series] the %s joins the garages (slot %d)", g.vassets[m].spec.name.c_str(), (int)g.ownedVehicleModels.size() - 1);
        g.notify("THE ARCLIGHT IS YOURS", "Lalo's series car, in championship livery. It waits in every safehouse garage.");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        using namespace series_detail;
        if (stage >= 1 && stage <= 4 && vehicleLost(g, playerCar, "series car")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.fadeOut(1.4f);
                    setStage(5);
                }
                break;
            case 5:
                if (g.fadedOut()) setupLeg(g);
                break;
            case 8:
                if (playerCar >= 0 && g.vehicles[playerCar].used && g.vehicles[playerCar].sim.speed() > 0.5f) g.vehicles[playerCar].sim.body.vel *= 0.5f;
                if (streamed(g) || stageTime > 5.f) {
                    g.fadeIn(1.2f);
                    gridIntro(g);
                }
                break;
            case 1: {
                if (playerCar >= 0 && g.vehicles[playerCar].sim.speed() > 0.5f) g.vehicles[playerCar].sim.body.vel *= 0.5f;
                if (g.mInCutscene() || (g.mTalking() && stageTime < 9.f)) break;
                countdown -= dt;
                int c = (int)ceilf(countdown);
                if (c != lastCount && c >= 1 && c <= 3) {
                    lastCount = c;
                    g.bigMessage(StrFormat("%d", c), legs()[leg].name, 0xffffffffu);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_COUNTDOWN, 0.9f);
#endif
                }
                if (countdown <= 0.f) {
                    g.bigMessage("GO!", "", 0xff33ff66u);
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_RACE_GO, 0.9f);
#endif
                    for (int v : race.racers)
                        if (ScriptDriver* d = driverFor(v)) d->speedScale = 1.f;
                    race.showMarkers(g);
                    score(SC_CHASE, 0.95f, 5 + leg);
                    setStage(3);
                }
                break;
            }
            case 3: {
                legTime += dt;
                if (g.playerVehicle() < 0) {
                    timer += dt;
                    if (timer > 15.f) return fail("You left the race.");
                    if (g.hudHelpTimer <= 0.f) g.help("Get back in your series car and finish the leg.", 2.f);
                } else timer = 0.f;
                if (race.updatePlayer(g)) race.showMarkers(g);
                for (int i = 0; i < 3; i++) {
                    if (aiDone[i] >= 0.f || aiCar[i] < 0) continue;
                    ScriptDriver* d = driverFor(aiCar[i]);
                    if (d && d->done) {
                        aiDone[i] = legTime;
                        if (!flagCalled) {
                            flagCalled = true;
                            laloSay(g, -1, StrFormat("[shout:0.5]%s takes the flag in %s!", kDrivers[i].name, legs()[leg].place));
                        }
                    }
                }
                int pos = race.position(g);
                if (!taunted && race.next >= (int)race.checkpoints.size() / 2 && pos > 1) {
                    // the leader, if it's close enough to be heard over the engines
                    int lead = -1;
                    float best = -1.f;
                    for (int i = 0; i < 3; i++) {
                        ScriptDriver* d = aiCar[i] >= 0 ? driverFor(aiCar[i]) : nullptr;
                        if (d && d->along > best) {
                            best = d->along;
                            lead = i;
                        }
                    }
                    if (lead >= 0 && ::length(vehPos(g, aiCar[lead]) - playerPos(g)) < 70.f) {
                        taunted = true;
                        driverSay(g, lead, aiPed[lead], kDrivers[lead].taunt);
                    }
                }
                g.missionCounterLabel = "POSITION";
                g.missionCounter = pos;
                g.missionCounterMax = 4;
                g.mObjective(StrFormat("~y~Leg %d: %s~s~  Checkpoint %d/%d   %d:%04.1f   ~b~%d pts~s~", leg + 1, legs()[leg].name, race.next,
                                       (int)race.checkpoints.size(), (int)(legTime / 60.f), fmodf(legTime, 60.f), pts[0]));
                if (race.next >= (int)race.checkpoints.size()) {
                    g.missionCounterLabel.clear();
                    g.mClearMarkers();
                    g.mClearTarget();
                    scoreLeg(g);
                    setStage(4);
                }
                break;
            }
            case 4:
                if (stageTime < 4.5f || (g.mTalking() && stageTime < 12.f)) break;
                if (leg < 2) {
                    leg++;
                    g.fadeOut(1.2f);
                    setStage(5);
                    break;
                }
                return finale(g);
            case 6:   // champion: Lalo and the runner-up have their say
                if (stageTime > 1.5f && (!g.mTalking() || stageTime > 12.f)) return MS_PASSED;
                break;
            case 7:   // beaten on points
                if (stageTime > 1.5f && (!g.mTalking() || stageTime > 12.f)) return MS_FAILED;
                break;
            case 9:
                return fail("The series course could not be set up.");
        }
        return MS_RUNNING;
    }

    MissionStatus finale(GameWorld& g) {
        using namespace series_detail;
        std::vector<int> t = standings();
        int place = (int)(std::find(t.begin(), t.end(), 0) - t.begin()) + 1;
        g.mObjective("");
        if (place == 1) {
            int titles = flag(g, EX_SERIES_WINS);
            setFlag(g, EX_SERIES_WINS, Min(titles + 1, 999));
            setFlag(g, SIDE_SERIES, 1);
            won = titles == 0 ? 20000 : 12000;
            if (titles == 0) awardCar(g);
            laloSay(g, -1, StrFormat("[shout:0.7]Ladies and gentlemen, your Night Series champion, %s!", me(g)));
            int second = t[1] - 1;
            driverSay(g, second, aiPed[second], kDrivers[second].beaten);
            g.socialReport(UI::TE_RACE_WON, dvec3(playerPos(g)), "Porto Sol Night Series");
            LOG("[series] champion with %d points (titles %d)", pts[0], titles + 1);
            setStage(6);
            return MS_RUNNING;
        }
        int w = t[0] - 1;
        long long purse = place == 2 ? 3000 : (place == 3 ? 1000 : 0);
        if (purse > 0) money(g, purse);
        driverSay(g, w, aiPed[w], kDrivers[w].title);
        failReason = StrFormat("%s won the Night Series. You finished %s overall%s.", kDrivers[w].name, ordinal(place),
                               purse > 0 ? StrFormat(" and took $%lld", purse).c_str() : "");
        LOG("[series] finished %s overall with %d points", ordinal(place), pts[0]);
        setStage(7);
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        for (int i = 0; i < 3; i++)
            if (aiCar[i] >= 0) releaseDriver(g, aiCar[i]);
        g.missionCounterLabel.clear();
        if (g.fadeTarget > 0.f) g.fadeIn(1.6f);
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (stage == 3 && race.next < (int)race.checkpoints.size() && t.stageTime > 0.5f && fmodf(t.stageTime, 0.6f) < g.dtLast) {
            vec2 tan;
            race.path.at(race.cpAlong[race.next], nullptr, &tan);
            t.teleport(race.checkpoints[race.next] - vec3(tan * 4.f, 0.f), atan2f(-tan.x, tan.y));
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Ortega Harbor Runs (either protagonist): deliveries by boat from the Ortega boatyard slip on the Rio Sol, against the
// clock. The cargo takes every knock the boat takes (fragile loads more); the pay is the rate for what arrives intact
// plus a bonus for time to spare. Later runs bring the harbor patrol (don't let them come alongside) and Cuervo
// hijackers (outrun or sink them). Five runs clear the yard's debt with the bank.
struct HarborRun {
    const char* cargo;
    const char* dest;
    vec2 hint;          // water by the drop
    float seconds;
    long long pay;
    float fragile;      // cargo lost per point of hull damage (percent)
    int threat;         // 0 none, 1 harbor patrol, 2 hijackers
    const char* order;  // Tomas at the slip
    const char* reply;  // the protagonist
    const char* radio;  // Tomas after the drop
};

const HarborRun kHarborRuns[5] = {
    {"ice", "the Solaris Pier landing", vec2(2600, 150), 150.f, 600, 0.05f, 0,
     "[calm]Ice for the bars on the Solaris Pier. It's a short hop downriver, but it's hot out and ice doesn't wait.",
     "[happy:0.3]Cold drinks for rich people. Noble work.",
     "[happy:0.4]The pier says thanks, and that the ice was perfect. I'm choosing to believe them."},
    {"engine parts", "the Port Isle quay", vec2(4700, -300), 300.f, 900, 0.05f, 0,
     "[calm]The tug crews at Port Isle need these engine parts before the evening tide. Down the river, then south across the bay.",
     "[calm]On it. Try not to sell the boat while I'm gone.",
     "[happy:0.3]Port Isle paid. The tug captain says you drive like an Ortega. I told him that's a compliment."},
    {"crystal glassware", "the North Bay moorings", vec2(4500, 2500), 360.f, 1200, 0.14f, 0,
     "[scared:0.3]Crystal glasses for a yacht party at the North Bay moorings. Every bump costs us. Drive like Mom's in the back.",
     "[calm]Smooth as glass. Literally.",
     "[happy:0.4]The yacht people counted every glass. You'd think they were counting their money."},
    {"Jonah's crates", "the Key Coral marina", vec2(4000, -3300), 420.f, 2000, 0.05f, 1,
     "[whisper:0.4]Jonah's crates, for Key Coral. Don't ask what's in them. And don't let the harbor patrol ask either.",
     "[calm]What harbor patrol?",
     "[happy:0.3]Jonah says the crates arrived dry and unasked about. His words. He paid extra for the second part."},
    {"the fuel dock payroll", "the river mouth fuel dock", vec2(3900, 160), 240.f, 2500, 0.05f, 2,
     "[scared:0.4]Payroll for the fuel dock crews at the river mouth. Word got out. The Cuervos might try something on the river.",
     "[angry:0.3]Let them try.",
     "[happy:0.5]Payroll delivered. The crews cheered. Somebody said your name like it was a song."},
};

class MissionHarborRuns : public StoryMission {
public:
    int run = 0;
    int boat = -1, tomas = -1;
    vec3 drop;
    float clock = 0.f, cargo = 100.f, lastHealth = -1.f;
    std::vector<int> chasers;              // the patrol boat or the hijackers' boats
    float closeTime = 0.f, chaseTime = 0.f, repath = 0.f, farTime = 0.f;
    bool threatSpawned = false, threatOver = false;
    long long won = 0;
    const char* title() const override { return "Ortega Harbor Runs"; }
    const char* brief() const override {
        return "Take the Ortega boatyard's delivery boat down the Rio Sol and deliver the cargo before the clock runs out. Every knock the boat "
               "takes comes out of the pay.";
    }
    long long reward() const override { return won; }
    const char* passBanner() const override { return "DELIVERED"; }
    const char* failBanner() const override { return "DELIVERY FAILED"; }

    void start(GameWorld& g) override {
        const Places& P = gPlaces;
        run = flag(g, EX_HARBOR_LEVEL) % 5;
        const HarborRun& R = kHarborRuns[run];
        if (!findWater(g, R.hint, 1.5f, drop, 500.f)) drop = vec3(R.hint, g.map->waterAt(R.hint.x, R.hint.y));
        float yaw = yawTo(P.riverLaunch.xy(), P.riverMouth.xy());
        int model = modelByName(g, "Bonefish 28");
        if (model >= 0) {
            boat = g.mVehicle(model, dvec3(P.riverLaunch + vec3(0.f, 0.f, 0.3f)), yaw);
            if (boat >= 0) g.vehicles[boat].color0 = lin(0.92f, 0.9f, 0.84f);
        } else {
            boat = spawnBoat(g, P.riverLaunch, yaw, lin(0.92f, 0.9f, 0.84f), 1);
        }
        if (boat < 0) {
            setStage(9);
            return;
        }
        lastHealth = g.vehicles[boat].sim.health;
        tomas = spawnCast(g, CAST_TOMAS, P.boatyard.door, P.boatyard.yaw, FAC_FRIEND);
        if (tomas >= 0) g.peds[tomas].invincible = true;
        placePlayer(g, placeOffset(g, P.boatyard, 2.f, 3.f), P.boatyard.yaw + kPi);
        vec3 tp = pedPos(g, tomas), mp = playerPos(g);
        facePed(g, tomas, mp);
        facePed(g, g.player, tp);
        std::vector<CutsceneShot> shots;
        vec3 bp = P.riverLaunch;
        shots.push_back(shotMove(bp + vec3(-10.f, -12.f, 4.f), bp, bp + vec3(-6.f, -13.f, 3.f), bp, 4.5f, 45.f));
        shots.push_back(shotTwo(tp, mp, 6.f));
        g.mCutscene(shots);
        say(g, CAST_TOMAS, tomas, R.order);
        sayMe(g, R.reply);
        g.mObjective("");
        setStage(0);
    }

    // a chase boat (patrol or hijackers) steering for where the player is heading
    void steerChaser(GameWorld& g, int v, float cruise) {
        vec3 from = vehPos(g, v), to = playerPos(g);
        int pv = g.playerVehicle();
        if (pv >= 0) to += g.vehicles[pv].sim.body.vel * 1.5f;
        vec2 dir = ::length(to.xy() - from.xy()) > 1.f ? normalize(to.xy() - from.xy()) : vec2(0.f, 1.f);
        to = vec3(to.xy() + dir * 80.f, from.z);
        std::vector<vec3> wps = {vec3(from.xy(), from.z), to};
        RoutePath p;
        buildWaypointPath(wps, p, cruise, false, false);
        if (ScriptDriver* d = driverFor(v)) {
            d->path = p;
            d->along = 0.f;
            d->done = false;
            d->cruise = cruise;
        } else {
            addDriver(g, v, p, cruise, DRV_WATER, 0);
        }
    }

    void spawnThreat(GameWorld& g, const HarborRun& R) {
        threatSpawned = true;
        vec3 pp = playerPos(g);
        vec2 toDrop = ::length(drop.xy() - pp.xy()) > 1.f ? normalize(drop.xy() - pp.xy()) : vec2(1.f, 0.f);
        vec2 side(toDrop.y, -toDrop.x);
        if (R.threat == 1) {
            vec3 w;
            if (!findWater(g, pp.xy() + toDrop * 320.f + side * 160.f, 2.f, w, 250.f)) return;
            int v = spawnBoat(g, w, yawTo(w.xy(), pp.xy()), lin(0.9f, 0.92f, 0.95f), 0);
            if (v < 0) return;
            int cop = spawnCast(g, CAST_COP_A, w, 0.f, FAC_CIVILIAN);
            if (cop >= 0) g.warpPedIntoVehicle(cop, v, 0);
            chasers.push_back(v);
            g.mBlipVehicle(v, UI::BLIP_POLICE);
            steerChaser(g, v, 20.f);
            say(g, CAST_COP_A, cop, "[shout:0.6]Harbor patrol! Cut your engine and prepare to be boarded!");
            sayMe(g, "[calm]Not today.");
            g.help("Don't let the harbor patrol come alongside. Outrun them.", 5.f);
        } else {
            for (int k = 0; k < 2; k++) {
                vec3 w;
                if (!findWater(g, pp.xy() + toDrop * 520.f + side * (k ? 45.f : -45.f), 1.8f, w, 250.f)) continue;
                int v = spawnBoat(g, w, yawTo(w.xy(), pp.xy()), lin(0.08f, 0.08f, 0.09f), (u32)k);
                if (v < 0) continue;
                int seats = (int)g.vassets[g.vehicles[v].model].spec.seats.size();
                for (int s = 0; s < Min(2, seats); s++) {
                    int e = spawnCast(g, s == 0 ? CAST_THUG_A + k : CAST_THUG_C + k, w, 0.f, FAC_ENEMY);
                    if (e < 0) continue;
                    g.warpPedIntoVehicle(e, v, s);
                    if (s == 1) {
                        arm(g, e, WPN_SMG);
                        setCombat(g, e, g.player, 0.12f);
                    }
                    g.peds[e].brain.accuracy = 0.12f;
                    enemies.push_back(e);
                }
                chasers.push_back(v);
                g.mBlipVehicle(v, UI::BLIP_ENEMY);
                steerChaser(g, v, 21.f);
            }
            say(g, CAST_THUG_A, -1, "[angry:0.6]That's the payroll boat! Take it!");
            sayMe(g, "[angry:0.4]Here we go.");
            g.help("Cuervo hijackers! Outrun them or sink them.", 5.f);
        }
        LOG("[harbor] run %d: %d chase boat%s", run + 1, (int)chasers.size(), chasers.size() == 1 ? "" : "s");
    }

    // the chase: returns false when the patrol boarded the player
    bool updateThreat(GameWorld& g, const HarborRun& R, float dt) {
        if (!threatSpawned || threatOver) return true;
        chaseTime += dt;
        repath -= dt;
        vec3 pp = playerPos(g);
        float nearest = 1e9f;
        int active = 0;
        for (int v : chasers) {
            if (!vehicleAlive(g, v) || vehicleDisabled(g, v)) continue;
            int drv = g.vehicles[v].seats[0];
            if (drv < 0 || !pedAlive(g, drv)) continue;
            bool crewed = R.threat == 1;
            for (int s = 1; s < 8 && !crewed; s++) {
                int p = g.vehicles[v].seats[s];
                if (p >= 0 && pedAlive(g, p)) crewed = true;
            }
            if (!crewed) continue;   // hijackers with nobody left to shoot turn back
            active++;
            nearest = Min(nearest, ::length(vehPos(g, v) - pp));
            if (repath <= 0.f) steerChaser(g, v, R.threat == 1 ? 20.f : 21.f);
        }
        if (repath <= 0.f) repath = 1.f;
        if (R.threat == 1) {
            if (nearest < 22.f) {
                closeTime += dt;
                if (g.hudHelpTimer <= 0.f) g.help("The harbor patrol is alongside. Pull away!", 1.5f);
                if (closeTime > 4.f) return false;
            } else {
                closeTime = Max(0.f, closeTime - dt * 0.5f);
            }
        }
        farTime = nearest > 400.f ? farTime + dt : 0.f;
        if (active == 0 || farTime > 4.f || (R.threat == 1 && chaseTime > 80.f)) {
            threatOver = true;
            for (int v : chasers) {
                releaseDriver(g, v);
                unblipVehicle(v);
            }
            if (R.threat == 1) {
                g.notify("HARBOR PATROL", "You lost the harbor patrol.");
                sayMe(g, "[happy:0.4]Harbor patrol. Adorable.");
            } else {
                g.notify("HIJACKERS", active == 0 ? "The hijackers are finished." : "You lost the hijackers.");
                sayMe(g, "[calm]Payroll's still aboard. Keep moving.");
            }
            LOG("[harbor] threat over after %.0f s (%s)", chaseTime, active == 0 ? "beaten" : "outrun");
        }
        return true;
    }

    MissionStatus update(GameWorld& g, float dt) override {
        const Places& P = gPlaces;
        const HarborRun& R = kHarborRuns[run];
        if (stage == 9) return fail("The delivery boat could not be found.");
        if (vehicleLost(g, boat, "delivery boat")) return MS_FAILED;
        switch (stage) {
            case 0:
                if (!g.mInCutscene() && !g.mTalking()) {
                    g.mBlipVehicle(boat, UI::BLIP_BOAT);
                    g.mObjective(StrFormat("Get in the ~b~delivery boat~s~. Cargo: %s.", R.cargo));
                    next();
                }
                break;
            case 1:
                if (g.playerInVehicle(boat)) {
                    unblipVehicle(boat);
                    clock = R.seconds;
                    goTo(g, drop, 14.f, StrFormat("Deliver the %s to ~y~%s~s~.", R.cargo, R.dest), true, true, vec3(0.3f, 0.8f, 1.f));
                    score(SC_CHASE, 0.5f, 16);
                    next();
                }
                break;
            case 2: {
                if (abandoned(g, boat, 150.f, "delivery boat")) return MS_FAILED;
                clock -= dt;
                g.missionTimerHud = Max(0.f, clock);
                if (clock <= 0.f) return fail(StrFormat("Out of time. The %s never made it to %s.", R.cargo, R.dest).c_str());
                // every knock the hull takes, the cargo takes too
                float h = g.vehicles[boat].sim.health;
                if (h < lastHealth) cargo -= (lastHealth - h) * R.fragile;
                lastHealth = h;
                if (cargo <= 0.f) return fail(StrFormat("The %s is ruined.", R.cargo).c_str());
                g.missionCounterLabel = "CARGO";
                g.missionCounter = Max(0, (int)cargo);
                g.missionCounterMax = 100;
                if (R.threat > 0 && !threatSpawned) {
                    float fromLaunch = ::length(playerPos(g).xy() - P.riverLaunch.xy());
                    bool bay = ::length(playerPos(g).xy() - P.riverMouth.xy()) < 380.f;
                    if ((R.threat == 1 && (bay || fromLaunch > 2600.f)) || (R.threat == 2 && fromLaunch > 700.f)) {
                        spawnThreat(g, R);
                        score(SC_CHASE, 0.95f, 16);
                    }
                }
                if (!updateThreat(g, R, dt)) return fail("The harbor patrol came alongside and seized the cargo.");
                if (arrived(g)) {
                    g.missionTimerHud = -1.f;
                    g.missionCounterLabel.clear();
                    clearGoal(g);
                    long long pay = (long long)(R.pay * Clamp(cargo, 0.f, 100.f) / 100.f) + (long long)(clock * 3.f);
                    won = pay / 10 * 10;
                    int lvl = flag(g, EX_HARBOR_LEVEL) + 1;
                    setFlag(g, EX_HARBOR_LEVEL, lvl);
                    LOG("[harbor] run %d delivered: cargo %.0f%%, %.0f s to spare, $%lld", run + 1, cargo, clock, won);
                    g.notify("DELIVERED", StrFormat("%s: %d%% intact, %d s to spare.", R.cargo, (int)cargo, (int)clock));
                    if (lvl == 5 && !flag(g, SIDE_HARBOR_ALL)) {
                        setFlag(g, SIDE_HARBOR_ALL, 1);
                        won += 5000;
                        say(g, CAST_TOMAS, -1, "[happy:0.6]That's the last payment. The bank's off our backs. The yard is ours again. Five grand's yours, don't argue.");
                    } else {
                        say(g, CAST_TOMAS, -1, R.radio);
                    }
                    next();
                }
                break;
            }
            case 3:
                if (stageTime > 1.f && (!g.mTalking() || stageTime > 10.f)) return MS_PASSED;
                break;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        for (int v : chasers) releaseDriver(g, v);
        g.missionTimerHud = -1.f;
        g.missionCounterLabel.clear();
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        const Places& P = gPlaces;
        const HarborRun& R = kHarborRuns[run];
        if (stage == 1 && t.stageTime > 0.6f && !g.playerInVehicle(boat)) t.enter(boat, 0);
        if (stage != 2 || t.stageTime < 1.f || fmodf(t.stageTime, 1.f) >= g.dtLast) return;
        if (R.threat > 0 && !threatSpawned) {
            // out to where the chase starts, then let it run a few seconds
            t.teleport(P.riverMouth, yawTo(P.riverMouth.xy(), drop.xy()));
            return;
        }
        if (R.threat > 0 && !threatOver && chaseTime < 5.f) return;
        vec2 dir = ::length(drop.xy() - playerPos(g).xy()) > 1.f ? normalize(drop.xy() - playerPos(g).xy()) : vec2(1.f, 0.f);
        t.teleport(drop - vec3(dir * 5.f, 0.f), atan2f(-dir.x, dir.y));
        t.stopVehicle();
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Flight school: rings in the sky around the airport and the bay.
class MissionFlightSchool : public StoryMission {
public:
    int lesson = 0;
    int aircraft = -1;
    std::vector<vec3> rings;
    std::vector<float> ringYaw;
    int next = 0;
    float lessonTime = 0.f;
    long long won = 0;
    bool heliOnly = false;
    MissionFlightSchool(int l) : lesson(l) {}
    const char* title() const override {
        static const char* const kNames[3] = {"Flight School: Circuit", "Flight School: Pads", "Flight School: Coastal Run"};
        return kNames[Clamp(lesson, 0, 2)];
    }
    const char* brief() const override { return "Fly through every ring (or land on every pad) as fast as you can. Don't wreck the school's aircraft."; }
    long long reward() const override { return won; }

    void addRing(GameWorld& g, vec2 p, float agl, float yaw) {
        float gz = Max(groundAt(g, p.x, p.y, 400.f), 0.f);
        rings.push_back(vec3(p, gz + agl));
        ringYaw.push_back(yaw);
    }

    void start(GameWorld& g) override {
        heliOnly = lesson == 1 || !hasClass(g, Vehicles::VC_PLANE);
        const float rwY = 2080.f, rwX1 = 820.f;
        vec3 startPos(rwX1 - 60.f, rwY, groundAt(g, rwX1 - 60.f, rwY, 20.f));
        float yaw = kPi * 0.5f;   // facing west (-X)
        if (heliOnly) startPos = gPlaces.heliPad + vec3(0.f, 34.f, 0.f);
        aircraft = placePlayer(g, startPos, yaw, heliOnly ? pickModel(g, {Vehicles::VC_HELI}, 0) : pickModel(g, {Vehicles::VC_PLANE}, 0),
                               lin(0.95f, 0.95f, 0.95f));
        if (lesson == 0) {
            // circuit: climb out west, loop north over the Flats, back east over midtown and down final approach
            addRing(g, vec2(-600, 2080), 60.f, kPi * 0.5f);
            addRing(g, vec2(-1500, 2300), 120.f, kPi * 0.7f);
            addRing(g, vec2(-1300, 3300), 150.f, -kPi * 0.8f);
            addRing(g, vec2(0, 3800), 150.f, -kPi * 0.5f);
            addRing(g, vec2(1500, 3500), 140.f, -kPi * 0.3f);
            addRing(g, vec2(2300, 2600), 120.f, -kPi * 0.9f);
            addRing(g, vec2(1900, 2080), 70.f, kPi * 0.5f);
            addRing(g, vec2(1200, 2080), 30.f, kPi * 0.5f);
        } else if (lesson == 1) {
            // helipad landings: marked pads on the ground
            vec3 pads[3] = {gPlaces.hospital.pos, gPlaces.solarisPlaza + vec3(-20.f, -20.f, 0.f), gPlaces.heliPad};
            for (vec3 p : pads) {
                rings.push_back(p);
                ringYaw.push_back(0.f);
            }
        } else {
            addRing(g, vec2(2500, 1600), 90.f, -kPi * 0.5f);
            addRing(g, vec2(3500, 900), 70.f, -kPi * 0.6f);
            addRing(g, vec2(4300, 700), 40.f, -kPi * 0.5f);
            addRing(g, vec2(5000, 1250), 35.f, -kPi * 0.5f);
            addRing(g, vec2(5700, 1250), 25.f, -kPi * 0.5f);
            addRing(g, vec2(5600, -600), 40.f, kPi);
            addRing(g, vec2(4700, -1700), 60.f, kPi * 0.8f);
            addRing(g, vec2(3350, -750), 200.f, kPi * 0.5f);
            addRing(g, vec2(2200, 400), 80.f, kPi * 0.5f);
            addRing(g, vec2(900, 1400), 60.f, kPi * 0.6f);
        }
        next = 0;
        showNext(g);
        score(SC_CHASE, 0.5f, 12);
        g.mObjective(lesson == 1 ? "Land on each ~y~helipad~s~." : "Fly through the ~y~rings~s~.");
    }

    void showNext(GameWorld& g) {
        g.mClearMarkers();
        if (next < (int)rings.size()) {
            g.mTarget(rings[next].xy());
            if (lesson == 1) g.mMarker(dvec3(rings[next]), 6.f, vec3(0.3f, 0.8f, 1.f));
        }
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, aircraft, "aircraft")) return MS_FAILED;
        if (abandoned(g, aircraft, 60.f, "aircraft")) return MS_FAILED;
        lessonTime += dt;
        // draw the rings
        for (int i = next; i < (int)rings.size() && i < next + 3 && lesson != 1; i++) {
            vec3 col = i == next ? vec3(1.f, 0.85f, 0.2f) : vec3(0.3f, 0.7f, 1.f);
            drawModel(g, gRingModel, rings[i], ringYaw[i], vec3(18.f), col, i == next ? 2.f : 1.f, 0xA000000000ull + (u64)i);
        }
        if (next >= (int)rings.size()) return MS_RUNNING;
        vec3 pp = vehPos(g, aircraft);
        const Vehicles::VehicleState& s = g.vehicles[aircraft].sim;
        bool hit = false;
        if (lesson == 1) hit = ::length(pp.xy() - rings[next].xy()) < 14.f && s.agl < 2.5f && s.speed() < 2.f;
        else hit = ::length(pp - rings[next]) < 20.f;
        if (hit) {
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_CHECKPOINT, 0.8f);
#endif
            next++;
            showNext(g);
            if (next >= (int)rings.size()) {
                float par = lesson == 0 ? 150.f : (lesson == 1 ? 240.f : 180.f);
                int medal = lessonTime < par ? 3 : (lessonTime < par * 1.3f ? 2 : 1);
                won = 1000 + medal * 1000;
                setFlag(g, SIDE_FLIGHT_1 + lesson, 1);
                g.notify("FLIGHT SCHOOL", StrFormat("%s medal - %.0f s", medal == 3 ? "GOLD" : (medal == 2 ? "SILVER" : "BRONZE"), lessonTime));
                return MS_PASSED;
            }
        }
        g.mObjective(StrFormat("%s  %d/%d  %.0f s", lesson == 1 ? "Land on each ~y~helipad~s~." : "Fly through the ~y~rings~s~.", next,
                               (int)rings.size(), lessonTime));
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (next < (int)rings.size() && fmodf(t.stageTime, 0.6f) < g.dtLast && t.stageTime > 0.5f) {
            vec3 r = rings[next];
            t.teleport(lesson == 1 ? r + vec3(0, 0, 0.8f) : r, ringYaw[next]);
            t.stopVehicle();
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Taxi fares: in any taxi, press G / R3 to go on duty.
const vec2 kHospitals[] = {vec2(1650, 1050), vec2(3650, 3200), vec2(-2400, 1800), vec2(5200, -900), vec2(-6400, 5200),
                           vec2(900, 6800), vec2(-3900, -3100), vec2(7400, 4800)};

Place randomStreetPlace(GameWorld& g, vec2 around, float minD, float maxD, u32 seed) {
    for (int tries = 0; tries < 24; tries++) {
        u32 h = hash32(seed + (u32)tries * 7919u);
        float a = hashToFloat(h) * kTwoPi, d = Lerp(minD, maxD, hashToFloat(h >> 8));
        vec2 p = around + vec2(cosf(a), sinf(a)) * d;
        if (g.map->isWater(p.x, p.y)) continue;
        Place pl = resolvePlace(g, p);
        if (pl.edge < 0) continue;
        if (g.map->isWater(pl.pos.x, pl.pos.y)) continue;
        World::Region r = g.map->regionAt(pl.pos.x, pl.pos.y);
        if (r == World::REG_OCEAN) continue;
        return pl;
    }
    return resolvePlace(g, around + vec2(minD, 0.f));
}

const char* const kFareHail[] = {"Taxi! Over here!", "Hey, cab! Are you free?", "Finally, a taxi.", "Thank you. I'm late, please hurry."};
const char* const kFareHurry[] = {"Can you go any faster?", "I've got a meeting, come on!", "Nice driving. Keep it up.", "Are you sure this is the fastest way?"};
const char* const kFareCrash[] = {"Hey! Watch the road!", "Are you trying to kill me?", "I want a discount after that.", "Easy! This isn't a demolition derby!"};
const char* const kFareThanks[] = {"Thanks. Keep the change.", "Right on time. Here's a little extra.", "Finally. Here you go.", "Great ride. See you around."};

void sayFare(GameWorld& g, int ped, const std::string& text) {
    DialogueLine l = line("Passenger", text, pedAlive(g, ped) ? ped : -1, kColOther);
    g.mSay(l);
}

class MissionTaxi : public StoryMission {
public:
    int taxi = -1, fare = -1;
    Place pickup, dropoff;
    float fareTimer = 0.f;
    int fares = 0, streak = 0;
    long long earned = 0;
    float outside = 0.f;
    float lastImpact = 0.f;
    int complaints = 0;
    u32 seed = 1;
    bool endRequested = false;
    const char* title() const override { return "Taxi Shift"; }
    const char* brief() const override { return "Pick up passengers and drive them to their destinations. Faster rides earn bigger tips. Get out of the cab to end the shift."; }
    long long reward() const override { return 0; }

    void newFare(GameWorld& g) {
        seed = hash32(seed + (u32)(g.time * 10.0));
        pickup = randomStreetPlace(g, playerPos(g).xy(), 150.f, 420.f, seed);
        int ci = g.randomCivilianChar(seed >> 3, (seed & 7) == 3 ? 3 : 0);
        fare = g.mPed(ci, dvec3(pickup.pos), pickup.yaw + kPi * 0.5f, FAC_CIVILIAN);
        if (fare >= 0) setIdle(g, fare, 0);
        goTo(g, pickup.curb, 6.f, "Pick up the ~b~passenger~s~.", true, false);
        g.mClearTarget();
        g.mBlipPed(fare, UI::BLIP_FRIEND);
        g.mTarget(pickup.pos.xy(), UI::BLIP_TAXI_JOB);
        setStage(1);
    }

    void start(GameWorld& g) override {
        taxi = g.playerVehicle();
        score(SC_NOIR, 0.2f, 13);
        g.notify("TAXI", "On duty. Leave the cab or press ~i:G|RS~ again to end the shift.");
        newFare(g);
    }

    MissionStatus endShift(GameWorld& g) {
        if (fare >= 0 && pedAlive(g, fare) && g.peds[fare].vehicle >= 0) g.removePedFromVehicle(fare, true);
        setFlag(g, EX_TAXI_BEST, Max(flag(g, EX_TAXI_BEST), fares));
        g.notify("TAXI SHIFT OVER", StrFormat("%d fares, $%lld earned", fares, earned));
        return fares > 0 ? MS_PASSED : MS_FAILED;
    }
    const char* passBanner() const override { return "SHIFT OVER"; }
    const char* failBanner() const override { return "SHIFT OVER"; }
    bool allowRetry() const override { return false; }

    MissionStatus update(GameWorld& g, float dt) override {
        if (!vehicleAlive(g, taxi)) return fail("The taxi was wrecked.");
        if (g.playerVehicle() != taxi) {
            outside += dt;
            if (outside > 6.f) {
                failReason = "You left the taxi.";
                return endShift(g);
            }
        } else outside = 0.f;
        if (((g.ctl.special.pressed && stageTime > 1.f) || endRequested) && g.playerVehicle() == taxi) {
            failReason = "Shift ended.";
            return endShift(g);
        }
        switch (stage) {
            case 1: {
                if (!pedAlive(g, fare)) {
                    newFare(g);
                    break;
                }
                bool close = ::length(vehPos(g, taxi) - pedPos(g, fare)) < 12.f && vehicleSpeed(g, taxi) < 3.f;
                if (close) {
                    setGoto(g, fare, vehPos(g, taxi), 2.f);
                    if (::length(vehPos(g, taxi) - pedPos(g, fare)) < 4.f || stageTime > 30.f) {
                        int seat = g.freeSeat(taxi, false);
                        if (seat < 0) seat = 1;
                        g.warpPedIntoVehicle(fare, taxi, seat);
                        g.peds[fare].brain.type = BRAIN_NONE;
                        sayFare(g, fare, kFareHail[seed % 4]);
                        dropoff = randomStreetPlace(g, playerPos(g).xy(), 500.f, 2200.f, seed * 3u + 1u);
                        RoutePath rp;
                        buildRoadPath(g, playerPos(g).xy(), dropoff.curb.xy(), rp);
                        float dist = Max(rp.length(), 300.f);
                        fareTimer = dist / 11.f + 25.f;
                        g.mClearBlips();
                        std::string street = dropoff.edge >= 0 ? g.roads->edges[dropoff.edge].name : std::string("the destination");
                        goTo(g, dropoff.curb, 6.f, StrFormat("Take the passenger to ~y~%s~s~.", street.c_str()), true);
                        complaints = 0;
                        lastImpact = 0.f;
                        next();
                    }
                } else if (stageTime > 0.5f) {
                    vec3 tp = pedPos(g, fare);
                    if (::length(tp - playerPos(g)) < 60.f) facePed(g, fare, playerPos(g));
                }
                break;
            }
            case 2: {
                fareTimer -= dt;
                g.missionTimerHud = Max(0.f, fareTimer);
                lastImpact -= dt;
                const Vehicles::VehicleState& s = g.vehicles[taxi].sim;
                if (s.impactImpulse > 6000.f && lastImpact <= 0.f) {
                    lastImpact = 3.f;
                    complaints++;
                    sayFare(g, fare, kFareCrash[(seed + complaints) % 4]);
                }
                if (stageTime > 25.f && fmodf(stageTime, 25.f) < dt && fareTimer > 0.f) sayFare(g, fare, kFareHurry[(seed + (u32)stageTime) % 4]);
                if (fareTimer <= 0.f) {
                    sayFare(g, fare, "Forget it. I'll walk. You're not getting a cent.");
                    g.removePedFromVehicle(fare, true);
                    g.missionTimerHud = -1.f;
                    streak = 0;
                    clearGoal(g);
                    fare = -1;
                    setStage(4);
                    break;
                }
                if (arrived(g) && g.playerVehicle() == taxi) {
                    clearGoal(g);
                    g.missionTimerHud = -1.f;
                    float dist = ::length(dropoff.curb.xy() - pickup.curb.xy());
                    long long base = 20 + (long long)(dist * 0.03f);
                    long long tip = complaints == 0 ? (long long)(Saturate(fareTimer / 40.f) * 40.f) + streak * 5 : 0;
                    long long pay = base + tip;
                    money(g, pay);
                    earned += pay;
                    fares++;
                    streak++;
                    setFlag(g, EX_TAXI_FARES, flag(g, EX_TAXI_FARES) + 1);
                    setFlag(g, EX_TAXI_EARNED, flag(g, EX_TAXI_EARNED) + (int)pay);
                    sayFare(g, fare, kFareThanks[seed % 4]);
                    g.notify("FARE", StrFormat("$%lld (tip $%lld)  -  streak %d", pay, tip, streak));
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_CASH_REGISTER, 0.7f);
#endif
                    g.removePedFromVehicle(fare, true);
                    setGoto(g, fare, dropoff.door, 1.3f);
                    fare = -1;
                    if (streak == 10) {
                        setFlag(g, SIDE_TAXI, 1);
                        money(g, 1000);
                        g.bigMessage("TAXI STREAK", "10 fares in a row: +$1000", 0xff33ccffu);
                    }
                    if (flag(g, EX_TAXI_FARES) >= 50 && !flag(g, SIDE_TAXI_ALL)) {
                        setFlag(g, SIDE_TAXI_ALL, 1);
                        g.notify("SOL CABS", "Fifty fares. The owner of Sol Cabs wants to sell you the company.");
                    }
                    setStage(4);
                }
                break;
            }
            case 4:
                if (stageTime > 2.5f) newFare(g);
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 1:
                if (fare >= 0 && t.stageTime > 0.5f) {
                    vec3 fp = pedPos(g, fare);
                    if (::length(playerPos(g) - fp) > 10.f) t.teleport(pickup.curb, pickup.curbYaw);
                    t.stopVehicle();
                }
                break;
            case 2: testGoal(g, t, dt, 70.f); break;
            case 4:
                if (fares >= 2 && t.stageTime > 1.f) endRequested = true;
                break;
            default: break;
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Vigilante: in a police car, press G / R3. Stop fleeing suspects, level by level.
class MissionVigilante : public StoryMission {
public:
    int cruiser = -1, level = 0, suspectCar = -1;
    float levelTimer = 0.f;
    long long earned = 0;
    float outside = 0.f;
    bool endRequested = false;
    const char* title() const override { return "Vigilante"; }
    const char* passBanner() const override { return "VIGILANTE OVER"; }
    const char* failBanner() const override { return "VIGILANTE OVER"; }
    bool allowRetry() const override { return false; }
    const char* brief() const override { return "Take down fleeing suspects before they get away. Each level adds more, better armed criminals."; }
    long long reward() const override { return 0; }

    void newLevel(GameWorld& g) {
        level++;
        enemies.clear();
        enemyCars.clear();
        u32 seed = hash32((u32)level * 977u + (u32)(g.time * 3.0));
        Place sp = randomStreetPlace(g, playerPos(g).xy(), 280.f, 520.f, seed);
        Place dest = randomStreetPlace(g, sp.pos.xy(), 1800.f, 3000.f, seed * 5u);
        int n = Min(1 + level / 2, 4);
        int model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_MUSCLE, Vehicles::VC_SUV}, seed);
        WeaponType w = level < 3 ? WPN_PISTOL : (level < 7 ? WPN_SMG : WPN_RIFLE);
        suspectCar = attackCar(g, model, sp.curb, sp.curbYaw, n, w, 0.12f + level * 0.02f, seed);
        if (suspectCar >= 0) {
            ScriptDriver& d = driveRoad(g, suspectCar, dest.curb.xy(), 20.f + level * 1.5f, true);
            d.rubberPed = g.player;
            d.rubberGap = 80.f;
            for (int s = 1; s < 4; s++) {
                int p = g.vehicles[suspectCar].seats[s];
                if (p >= 0 && level >= 2) setCombat(g, p, g.player, 0.12f + level * 0.02f);
            }
            g.mBlipVehicle(suspectCar, UI::BLIP_ENEMY);
        }
        levelTimer = 120.f + 15.f * level;
        g.mObjective(StrFormat("Vigilante level ~y~%d~s~: take down the ~r~suspects~s~.", level));
        g.bigMessage(StrFormat("LEVEL %d", level), "Vigilante", 0xff3080ffu);
    }

    void start(GameWorld& g) override {
        cruiser = g.playerVehicle();
        if (cruiser >= 0) g.vehicles[cruiser].sirenOn = true;
        score(SC_CHASE, 0.7f, 14);
        newLevel(g);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (!vehicleAlive(g, cruiser)) return fail("Your police car was wrecked.");
        if (g.playerVehicle() != cruiser) {
            outside += dt;
            if (outside > 60.f) return fail("You abandoned the police car.");
        } else outside = 0.f;
        if (((g.ctl.special.pressed && stageTime > 1.f) || endRequested) && g.playerVehicle() == cruiser) {
            failReason = StrFormat("Vigilante ended at level %d.", level);
            return level > 1 ? MS_PASSED : MS_FAILED;
        }
        levelTimer -= dt;
        g.missionTimerHud = Max(0.f, levelTimer);
        if (levelTimer <= 0.f) {
            failReason = "The suspects got away.";
            return level > 1 ? MS_PASSED : MS_FAILED;
        }
        if (suspectCar >= 0) dismountNear(g, suspectCar, vehPos(g, suspectCar), 1e9f);
        if (suspectCar >= 0 && vehicleDisabled(g, suspectCar)) {
            for (int e : enemies)
                if (pedAlive(g, e) && g.peds[e].brain.type != BRAIN_COMBAT) setCombat(g, e, g.player, 0.2f);
        }
        if (aliveEnemies(g) == 0) {
            long long pay = 150 * level * level;
            money(g, pay);
            earned += pay;
            g.missionTimerHud = -1.f;
            g.notify("SUSPECTS DOWN", StrFormat("Level %d complete: +$%lld", level, pay));
            setFlag(g, EX_VIGILANTE_LEVEL, Max(flag(g, EX_VIGILANTE_LEVEL), level));
            if (level == 5) setFlag(g, SIDE_VIGILANTE, 1);
            if (level == 12) {
                setFlag(g, SIDE_VIGILANTE_ALL, 1);
                g.bigMessage("VIGILANTE COMPLETE", "Porto Sol thanks you", 0xff33ccffu);
                return MS_PASSED;
            }
            g.mClearBlips();
            if (suspectCar >= 0) releaseDriver(g, suspectCar);
            newLevel(g);
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (t.stageTime > 1.f && fmodf(t.stageTime, 2.f) < g.dtLast) {
            if (suspectCar >= 0) t.destroy(suspectCar);
            t.killEnemies();
        }
        if (level >= 3 && t.stageTime > 3.f) endRequested = true;
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Paramedic: in an ambulance, press G / R3. Pick up the injured and rush them to a hospital.
class MissionParamedic : public StoryMission {
public:
    int amb = -1, level = 0;
    std::vector<int> patients;
    std::vector<bool> aboard, delivered;
    float levelTimer = 0.f;
    float outside = 0.f;
    vec2 hospital;
    bool endRequested = false;
    const char* title() const override { return "Paramedic"; }
    const char* passBanner() const override { return "PARAMEDIC OVER"; }
    const char* failBanner() const override { return "PARAMEDIC OVER"; }
    bool allowRetry() const override { return false; }
    const char* brief() const override { return "Pick up injured people and bring them to a hospital before time runs out. The ambulance holds three patients."; }
    long long reward() const override { return 0; }

    vec2 nearestHospital(vec2 p) {
        // the emergency doors of the hospitals as the world built them (the ambulance bays are ~8 m from the door);
        // the fixed points only if the world has none
        vec2 best = kHospitals[0];
        float bd = 1e30f;
        if (World::gSites && World::gSites->generated)
            for (const World::NamedPlace& h : World::gSites->places)
                if (h.kind == World::PK_HOSPITAL && ::length(h.door - p) < bd) {
                    bd = ::length(h.door - p);
                    best = h.door;
                }
        if (bd < 1e29f) return best;
        for (vec2 h : kHospitals)
            if (::length(h - p) < ::length(best - p)) best = h;
        return best;
    }

    void newLevel(GameWorld& g) {
        level++;
        for (int p : patients)
            if (p >= 0 && g.peds[p].used && g.peds[p].vehicle >= 0) g.removePedFromVehicle(p, false);
        patients.clear();
        aboard.clear();
        delivered.clear();
        int n = Min(level, 6);
        u32 seed = hash32((u32)level * 331u + (u32)(g.time * 7.0));
        float total = 0.f;
        for (int i = 0; i < n; i++) {
            Place pl = randomStreetPlace(g, playerPos(g).xy(), 150.f, 600.f, seed + (u32)i * 101u);
            int ci = g.randomCivilianChar(seed + (u32)i, 0);
            int p = g.mPed(ci, dvec3(pl.pos), pl.yaw, FAC_CIVILIAN);
            if (p < 0) continue;
            g.peds[p].health = 30.f;
            setIdle(g, p, 4);
            patients.push_back(p);
            aboard.push_back(false);
            delivered.push_back(false);
            g.mBlipPed(p, UI::BLIP_FRIEND);
            total += ::length(pl.pos - playerPos(g));
        }
        hospital = nearestHospital(playerPos(g).xy());
        levelTimer = 50.f + total / 12.f + n * 15.f;
        g.mObjective(StrFormat("Paramedic level ~y~%d~s~: pick up the ~b~injured~s~.", level));
        g.bigMessage(StrFormat("LEVEL %d", level), "Paramedic", 0xff3040ffu);
    }

    void start(GameWorld& g) override {
        amb = g.playerVehicle();
        if (amb >= 0) g.vehicles[amb].sirenOn = true;
        score(SC_CHASE, 0.55f, 15);
        newLevel(g);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (!vehicleAlive(g, amb)) return fail("The ambulance was wrecked.");
        if (g.playerVehicle() != amb) {
            outside += dt;
            if (outside > 30.f) return fail("You abandoned the ambulance.");
        } else outside = 0.f;
        if (((g.ctl.special.pressed && stageTime > 1.f) || endRequested) && g.playerVehicle() == amb) {
            failReason = StrFormat("Paramedic ended at level %d.", level);
            return level > 1 ? MS_PASSED : MS_FAILED;
        }
        levelTimer -= dt;
        g.missionTimerHud = Max(0.f, levelTimer);
        if (levelTimer <= 0.f) {
            failReason = "A patient didn't make it.";
            return level > 1 ? MS_PASSED : MS_FAILED;
        }
        int seatsUsed = 0;
        for (size_t i = 0; i < patients.size(); i++)
            if (aboard[i] && !delivered[i]) seatsUsed++;
        vec3 ap = vehPos(g, amb);
        bool slow = vehicleSpeed(g, amb) < 3.f;
        for (size_t i = 0; i < patients.size(); i++) {
            int p = patients[i];
            if (delivered[i] || aboard[i] || !pedAlive(g, p)) continue;
            if (slow && seatsUsed < 3 && ::length(pedPos(g, p) - ap) < 9.f) {
                int seat = g.freeSeat(amb, false);
                if (seat > 0) g.warpPedIntoVehicle(p, amb, seat);
                aboard[i] = true;
                seatsUsed++;
                levelTimer += 8.f;
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_CHECKPOINT, 0.7f);
#endif
            }
        }
        // hospital drop
        g.mClearMarkers();
        if (seatsUsed > 0) {
            vec3 hp(hospital, groundAt(g, hospital.x, hospital.y));
            g.mMarker(dvec3(hp), 4.f, vec3(1.f, 0.3f, 0.3f));
            g.mTarget(hospital, UI::BLIP_HOSPITAL);
            if (::length(ap.xy() - hospital) < 18.f && slow) {
                for (size_t i = 0; i < patients.size(); i++) {
                    if (!aboard[i] || delivered[i]) continue;
                    delivered[i] = true;
                    int p = patients[i];
                    if (p >= 0 && g.peds[p].used) {
                        if (g.peds[p].vehicle >= 0) g.removePedFromVehicle(p, true);
                        g.peds[p].health = 100.f;
                        setGoto(g, p, hp + vec3(0, 6.f, 0), 1.2f);
                    }
                }
            }
        } else {
            g.mClearTarget();
        }
        int done = 0, alive = 0;
        for (size_t i = 0; i < patients.size(); i++) {
            done += delivered[i] ? 1 : 0;
            alive += (delivered[i] || pedAlive(g, patients[i])) ? 1 : 0;
        }
        if (alive < (int)patients.size()) {
            failReason = "A patient died.";
            return level > 1 ? MS_PASSED : MS_FAILED;
        }
        g.missionCounterLabel = "PATIENTS";
        g.missionCounter = done;
        g.missionCounterMax = (int)patients.size();
        if (done == (int)patients.size() && !patients.empty()) {
            long long pay = 100 * level * level + 200;
            money(g, pay);
            g.missionTimerHud = -1.f;
            g.notify("PATIENTS DELIVERED", StrFormat("Level %d complete: +$%lld", level, pay));
            setFlag(g, EX_PARAMEDIC_LEVEL, Max(flag(g, EX_PARAMEDIC_LEVEL), level));
            if (level == 5) setFlag(g, SIDE_PARAMEDIC, 1);
            if (level == 12) {
                setFlag(g, SIDE_PARAMEDIC_ALL, 1);
                g.pinfo.money += 5000;
                g.bigMessage("PARAMEDIC COMPLETE", "Porto Sol General thanks you", 0xff33ccffu);
                return MS_PASSED;
            }
            g.mClearBlips();
            newLevel(g);
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        g.missionCounterLabel.clear();
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (t.stageTime < 0.5f) return;
        for (size_t i = 0; i < patients.size(); i++)
            if (!aboard[i] && pedAlive(g, patients[i])) {
                vec3 p = pedPos(g, patients[i]);
                t.teleport(p + vec3(3.f, 0, 0), 0.f);
                t.stopVehicle();
                return;
            }
        t.teleport(vec3(hospital, groundAt(g, hospital.x, hospital.y)), 0.f);
        t.stopVehicle();
        if (level >= 2 && t.stageTime > 2.f) endRequested = true;
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Courier: Rapido Couriers in Calle Luna. Six drops across the city on a scooter, against the clock.
class MissionCourier : public StoryMission {
public:
    int bike = -1;
    std::vector<vec3> drops;
    std::vector<bool> done;
    float clock = 0.f;
    long long won = 0;
    const char* title() const override { return "Rapido Couriers"; }
    const char* brief() const override { return "Deliver every package before the clock runs out. Stop at each marked address."; }
    long long reward() const override { return won; }

    void start(GameWorld& g) override {
        const Place& D = gPlaces.courierDepot;
        int model = pickModel(g, {Vehicles::VC_SCOOTER, Vehicles::VC_MOTORBIKE, Vehicles::VC_COMPACT}, 0);
        bike = placePlayer(g, D.curb, D.curbYaw, model, lin(0.95f, 0.45f, 0.1f));
        u32 seed = hash32((u32)(g.time * 13.0) + 77u);
        for (int i = 0; i < 6; i++) {
            Place p = randomStreetPlace(g, D.pos.xy(), 350.f, 1500.f, seed + (u32)i * 131u);
            drops.push_back(p.curb);
            done.push_back(false);
        }
        clock = 240.f;
        score(SC_CHASE, 0.5f, 16);
        g.mObjective("Deliver the ~y~packages~s~ (0/6).");
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, bike, "scooter")) return MS_FAILED;
        clock -= dt;
        g.missionTimerHud = Max(0.f, clock);
        if (clock <= 0.f) {
            int n = 0;
            for (bool b : done) n += b ? 1 : 0;
            failReason = StrFormat("Out of time: %d of 6 delivered.", n);
            return MS_FAILED;
        }
        g.mClearMarkers();
        int n = 0;
        vec2 nearest(0, 0);
        float nd = 1e9f;
        for (size_t i = 0; i < drops.size(); i++) {
            if (done[i]) {
                n++;
                continue;
            }
            g.mMarker(dvec3(drops[i]), 2.5f);
            float d = ::length(drops[i].xy() - playerPos(g).xy());
            if (d < nd) {
                nd = d;
                nearest = drops[i].xy();
            }
            bool slow = g.playerVehicle() < 0 || vehicleSpeed(g, g.playerVehicle()) < 4.f;
            if (d < 7.f && slow) {
                done[i] = true;
                n++;
                money(g, 100);
                clock += 10.f;
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_CASH_REGISTER, 0.6f);
#endif
            }
        }
        if (n < (int)drops.size()) g.mTarget(nearest, UI::BLIP_DELIVERY_JOB);
        g.mObjective(StrFormat("Deliver the ~y~packages~s~ (%d/6).", n));
        if (n >= (int)drops.size()) {
            won = 500 + (long long)clock * 5;
            setFlag(g, SIDE_COURIER, 1);
            int lvl = flag(g, EX_COURIER_LEVEL) + 1;
            setFlag(g, EX_COURIER_LEVEL, lvl);
            if (lvl >= 5) setFlag(g, SIDE_COURIER_ALL, 1);
            g.missionTimerHud = -1.f;
            return MS_PASSED;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        if (fmodf(t.stageTime, 0.8f) < g.dtLast && t.stageTime > 0.5f)
            for (size_t i = 0; i < drops.size(); i++)
                if (!done[i]) {
                    t.teleport(drops[i], 0.f);
                    t.stopVehicle();
                    break;
                }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Shooting range at Palmetto Arms: pop-up targets for 60 seconds.
class MissionRange : public StoryMission {
public:
    struct Target {
        vec3 pos;
        float yaw;
        float up = -1.f;      // time left standing (> 0 visible)
        float anim = 0.f;
    };
    std::vector<Target> targets;
    vec3 stand;
    float clock = 60.f;
    int points = 0, hits = 0;
    float spawnTimer = 0.f;
    u32 seed = 3;
    long long won = 0;
    int savedWeapon = 0;
    const char* title() const override { return "Shooting Range"; }
    const char* brief() const override { return "Hit as many pop-up targets as you can in 60 seconds. Bullseyes score 10. Gold needs 350 points."; }
    long long reward() const override { return won; }

    void start(GameWorld& g) override {
        const Place& S = gPlaces.rangeFlats;
        vec2 out = S.outward, along = S.streetDir;
        stand = placeOffset(g, S, 0.f, 4.f);
        placePlayer(g, stand, atan2f(-out.x, out.y));
        for (int row = 0; row < 3; row++)
            for (int k = 0; k < 4; k++) {
                float dist = 12.f + row * 10.f;
                vec2 p = stand.xy() + out * dist + along * (-9.f + k * 6.f + (row & 1) * 3.f);
                Target t;
                t.pos = vec3(p, groundAt(g, p.x, p.y, stand.z + 4.f));
                t.yaw = atan2f(-(-out).x, (-out).y) + kPi;
                targets.push_back(t);
            }
        Ped* pl = g.playerPed();
        if (pl) {
            savedWeapon = pl->weapon;
            g.giveWeapon(g.player, WPN_PISTOL, 200);
            pl->weapon = WPN_PISTOL;
        }
        g.mObjective("Shoot the ~y~targets~s~! Aim with the right mouse button / LT.");
        score(SC_HEIST, 0.4f, 17);
        g.mMarker(dvec3(stand), 1.2f, vec3(0.3f, 0.8f, 1.f));
    }

    void scoreHit(GameWorld& g, Target& t, float offCenter) {
        int pts = offCenter < 0.08f ? 10 : (offCenter < 0.16f ? 8 : (offCenter < 0.26f ? 5 : 3));
        points += pts;
        hits++;
        t.up = -0.01f;
#ifdef HAVE_AUDIO
        Audio::play(Audio::SFX_IMPACT_WOOD, t.pos + vec3(0, 0, 1.4f), 0.9f);
#endif
        g.pinfo.hitMarker = 1.f;
    }

    MissionStatus update(GameWorld& g, float dt) override {
        clock -= dt;
        g.missionTimerHud = Max(0.f, clock);
        g.missionCounterLabel = "POINTS";
        g.missionCounter = points;
        g.missionCounterMax = 350;
        if (!g.playerAt(stand.xy(), 6.f)) return fail("You left the firing line.");
        // pop targets up
        spawnTimer -= dt;
        if (spawnTimer <= 0.f && clock > 0.f) {
            spawnTimer = 0.9f;
            seed = hash32(seed + 17u);
            int i = (int)(seed % targets.size());
            if (targets[i].up <= 0.f) targets[i].up = 2.2f - Min(1.f, (60.f - clock) / 60.f);
        }
        vec3 o, d;
        bool shot = playerShotRay(g, o, d);
        for (size_t i = 0; i < targets.size(); i++) {
            Target& t = targets[i];
            if (t.up > 0.f) {
                t.up -= dt;
                t.anim = Min(1.f, t.anim + dt * 8.f);
            } else {
                t.anim = Max(0.f, t.anim - dt * 6.f);
            }
            if (t.anim > 0.01f) {
                // board rises from the ground
                vec3 base = t.pos - vec3(0, 0, (1.f - t.anim) * 1.9f);
                drawModel(g, gTargetModel, base, t.yaw, vec3(1.f), vec3(1.f), 1.f, 0xB000000000ull + i);
                if (shot && t.up > 0.f && t.anim > 0.6f) {
                    vec3 c = base + vec3(0, 0, 1.45f);
                    float tt;
                    float off = rayPointDist(o, d, c, &tt);
                    if (off < 0.45f && tt < 80.f) {
                        scoreHit(g, t, off);
                        shot = false;
                    }
                }
            }
        }
        if (clock <= 0.f) {
            g.missionTimerHud = -1.f;
            g.missionCounterLabel.clear();
            int best = flag(g, EX_RANGE_BEST);
            if (points > best) setFlag(g, EX_RANGE_BEST, points);
            const char* medal = points >= 350 ? "GOLD" : (points >= 250 ? "SILVER" : (points >= 150 ? "BRONZE" : nullptr));
            if (!medal) return fail(StrFormat("%d points. Bronze needs 150.", points).c_str());
            won = points >= 350 ? 2500 : (points >= 250 ? 1200 : 500);
            if (points >= 350) setFlag(g, SIDE_RANGE, 1);
            g.notify("SHOOTING RANGE", StrFormat("%s - %d points, %d hits%s", medal, points, hits, points > best ? "  NEW BEST" : ""));
            return MS_PASSED;
        }
        return MS_RUNNING;
    }

    void finish(GameWorld& g, bool passed) override {
        (void)passed;
        g.missionCounterLabel.clear();
        Ped* pl = g.playerPed();
        if (pl && pl->hasWeapon[savedWeapon]) pl->weapon = (WeaponType)savedWeapon;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        (void)t;
        clock -= g.dtLast * 3.f;
        for (auto& tg : targets)
            if (tg.up > 0.f && tg.anim > 0.7f) scoreHit(g, tg, 0.05f);
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Bail bonds: Benny Salas of Palmera Bail Bonds pays for skips brought in. Each job is the next name on his list: find
// the fugitive in a district, run them down (on foot, in a car, or through their friends), make them give up and
// deliver them to police headquarters. Dead skips pay half. After the list, Benny keeps a rotation of smaller jobs.
struct Fugitive {
    const char* name;
    const char* crime;
    const char* district;
    vec2 hint;
    int behavior;            // 0 runs, 1 drives off, 2 fights with friends
    long long reward;
    const char* notice;      // when the player closes in
    const char* surrender;
};
const Fugitive kFugitives[] = {
    {"Rudy \"Two Tone\" Mercer", "skipped court on a paint job fraud", "Palmetto Flats", vec2(900, 2700), 0, 2500, "[scared]Aw no. Not today, man!",
     "[scared]Okay! Okay! I give up, don't hit me!"},
    {"Glenda Watts", "forty stolen jet skis", "Sol Beach", vec2(5150, 900), 1, 3500, "[angry:0.6]Benny sent you? Tell Benny to kiss my wake!",
     "[sad]Fine. Fine! Just don't scratch my hair."},
    {"Marco \"Bones\" Batista", "assault, three missed hearings", "south Calle Luna", vec2(1300, -700), 2, 4500, "[angry]Boys! We got a bounty hunter!",
     "[scared:0.7]Enough! I'm done, I'm done."},
    {"Deshawn Pruitt", "check fraud", "the Canvas District", vec2(3000, 1650), 0, 3000, "[scared:0.6]I know that look. Bye!",
     "[sad:0.5]My mom's gonna kill me before the judge does."},
    {"Lorna Keel", "grand theft boat", "Rio Sol Marina", vec2(2300, 260), 1, 4000, "[angry:0.5]You'll never catch me on land either!",
     "[sad]I should have stayed on the water."},
    {"Vince Dagostino", "running an illegal card game", "North City", vec2(2600, 4300), 2, 6000, "[angry]Nobody walks out of my game!",
     "[scared]Alright, alright. I fold."},
    {"Harlan Voss", "moonshine running", "Redland", vec2(800, -5400), 1, 5500, "[calm]Reckon I'll be leaving now.",
     "[sad:0.5]Take it easy on the truck. She's older than you."},
    {"Ezekiel \"Zeke\" Carter", "armed robbery", "Key Coral", vec2(4450, -4200), 2, 9000, "[shout]You picked the wrong island, hunter!",
     "[scared]You win. You win!"},
};

class MissionBounty : public StoryMission {
public:
    int level = 0;
    const Fugitive* f = nullptr;
    int fugitive = -1, getaway = -1;
    std::vector<int> friends;
    vec3 hideout;
    bool noticed = false, surrendered = false;
    float closeTime = 0.f, farTime = 0.f;
    long long won = 0;
    const char* title() const override { return "Bail Bonds"; }
    const char* brief() const override { return "Palmera Bail Bonds pays for skips brought in: alive is full price, dead is half."; }
    long long reward() const override { return won; }
    const char* passBanner() const override { return "BOUNTY COLLECTED"; }
    bool allowRetry() const override { return false; }

    void start(GameWorld& g) override {
        level = flag(g, EX_BOUNTY_LEVEL);
        int n = (int)ARRAY_COUNT(kFugitives);
        f = &kFugitives[level % n];
        // the hideout: a building frontage in the district, shifted a little every lap of the list
        u32 h = hash32((u32)level * 7919u + 0xB0u);
        vec2 jitter(hashToFloat(h) * 160.f - 80.f, hashToFloat(h >> 8) * 160.f - 80.f);
        Place hp = resolveFrontage(g, f->hint + (level >= n ? jitter : vec2(0.f)));
        hideout = hp.door;
        int ci = g.randomCivilianChar(0xF00Du + (u32)level * 131u, 0);
        fugitive = g.mPed(ci, dvec3(hideout), hp.yaw, FAC_CIVILIAN);
        if (fugitive >= 0) {
            g.peds[fugitive].brain.type = BRAIN_NONE;
            g.peds[fugitive].maxHealth = g.peds[fugitive].health = 180.f;
            setIdle(g, fugitive, 10);
        }
        if (f->behavior == 1) {
            int model = pickModel(g, {Vehicles::VC_MUSCLE, Vehicles::VC_SPORTS, Vehicles::VC_PICKUP}, (u32)level);
            getaway = spawnCar(g, model, curbOffset(g, hp, 7.f), hp.curbYaw);
        }
        if (f->behavior == 2) {
            for (int i = 0; i < 2 + level / 3; i++) {
                vec3 p = placeOffset(g, hp, -3.f + i * 2.5f, 1.5f);
                int e = gunman(g, CAST_THUG_A + (i % 4), p, hp.yaw + kPi, i == 0 ? WPN_SMG : WPN_PISTOL, 0.2f);
                if (e >= 0) {
                    setIdle(g, e, i & 1 ? 7 : 10);
                    friends.push_back(e);
                }
            }
        }
        goTo(g, hideout, 45.f, StrFormat("Find ~r~%s~s~ in %s.", f->name, f->district), false, false);
        g.notify("PALMERA BAIL BONDS", StrFormat("%s: %s. Bond $%lld alive, half dead. Last seen in %s.", f->name, f->crime, f->reward, f->district));
        score(SC_NOIR, 0.35f, 16);
    }

    void surrender(GameWorld& g) {
        surrendered = true;
        g.mClearBlips();
        g.mBlipPed(fugitive, UI::BLIP_FRIEND);
        Ped& fp = g.peds[fugitive];
        fp.brain.type = BRAIN_NONE;
        fp.invincible = true;
        fp.faction = FAC_CIVILIAN;
        if (fp.vehicle >= 0) g.removePedFromVehicle(fugitive, true);
        setIdle(g, fugitive, 5);   // hands up
        releaseDriver(g, getaway);
        for (int e : friends)
            if (pedAlive(g, e)) setFlee(g, e, g.player);
        say(g, CAST_THUG_B, fugitive, f->surrender);
        goTo(g, gPlaces.policeHq.curb, 6.f, StrFormat("Take ~b~%s~s~ to ~y~police headquarters~s~.", f->name), true);
        score(SC_NOIR, 0.4f, 16);
        next();
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (!pedAlive(g, fugitive) && !surrendered) {
            // dead skip: half the bond
            won = f->reward / 2;
            setFlag(g, EX_BOUNTY_LEVEL, level + 1);
            if (level + 1 >= (int)ARRAY_COUNT(kFugitives)) setFlag(g, SIDE_BOUNTY_ALL, 1);
            g.notify("PALMERA BAIL BONDS", StrFormat("%s is dead. Benny pays half: $%lld.", f->name, won));
            return MS_PASSED;
        }
        vec3 fpos = pedPos(g, fugitive);
        float d = ::length(fpos - playerPos(g));
        switch (stage) {
            case 0:
                if (d < 28.f) {
                    noticed = true;
                    clearGoal(g);
                    g.mBlipPed(fugitive, UI::BLIP_ENEMY);
                    say(g, CAST_THUG_B, fugitive, f->notice);
                    if (f->behavior == 0) {
                        setFlee(g, fugitive, g.player);
                        g.mObjective(StrFormat("Catch ~r~%s~s~. Tackle or rough them up; dead pays half.", f->name));
                    } else if (f->behavior == 1 && vehicleAlive(g, getaway)) {
                        g.warpPedIntoVehicle(fugitive, getaway, 0);
                        RoutePath path;
                        vec2 away = fpos.xy() + normalize(fpos.xy() - playerPos(g).xy() + vec2(0.01f, 0.f)) * 1500.f;
                        buildRoadPath(g, fpos.xy(), away, path);
                        ScriptDriver& dr = addDriver(g, getaway, path, 26.f);
                        dr.rubberPed = g.player;
                        dr.rubberGap = 70.f;
                        g.mBlipVehicle(getaway, UI::BLIP_ENEMY);
                        g.mObjective(StrFormat("Stop ~r~%s~s~'s car. Wreck it and grab them.", f->name));
                    } else {
                        setFlee(g, fugitive, g.player);
                        for (int e : friends)
                            if (pedAlive(g, e)) setCombat(g, e, g.player, 0.2f);
                        blipEnemies(g);
                        g.mObjective(StrFormat("Deal with the friends and catch ~r~%s~s~.", f->name));
                    }
                    next();
                }
                break;
            case 1: {
                Ped& fp = g.peds[fugitive];
                // a wrecked getaway car throws them out on foot
                if (fp.vehicle >= 0 && vehicleDisabled(g, fp.vehicle)) {
                    releaseDriver(g, fp.vehicle);
                    g.removePedFromVehicle(fugitive, true);
                    setFlee(g, fugitive, g.player);
                }
                bool onFoot = fp.vehicle < 0 && g.playerVehicle() < 0;
                if (onFoot && d < 2.6f) closeTime += dt;
                else closeTime = Max(0.f, closeTime - dt);
                if (fp.health < fp.maxHealth * 0.45f || closeTime > 1.4f || (fp.vehicle < 0 && d < 3.5f && fp.ragdoll)) {
                    surrender(g);
                    break;
                }
                if (d > 420.f) farTime += dt;
                else farTime = 0.f;
                if (farTime > 15.f) return fail(StrFormat("%s got away.", f->name).c_str());
                break;
            }
            case 2: {
                int pv = g.playerVehicle();
                Ped& fp = g.peds[fugitive];
                if (pv >= 0 && fp.vehicle != pv && ::length(fpos - vehPos(g, pv)) < 9.f) {
                    int seat = g.freeSeat(pv, false);
                    if (seat > 0) {
                        g.warpPedIntoVehicle(fugitive, pv, seat);
                        say(g, CAST_THUG_B, fugitive, "[sad:0.5]My lawyer's gonna hear about this.");
                    }
                }
                if (pv < 0 && fp.vehicle < 0 && ::length(fpos - playerPos(g)) > 4.f) setGoto(g, fugitive, playerPos(g), 1.3f);
                bool together = (pv >= 0 && fp.vehicle == pv) || (pv < 0 && ::length(fpos - playerPos(g)) < 6.f);
                if (arrived(g) && together) {
                    clearGoal(g);
                    won = f->reward;
                    setFlag(g, EX_BOUNTY_LEVEL, level + 1);
                    setFlag(g, EX_BOUNTY_ALIVE, flag(g, EX_BOUNTY_ALIVE) + 1);
                    if (level + 1 >= (int)ARRAY_COUNT(kFugitives)) setFlag(g, SIDE_BOUNTY_ALL, 1);
                    if (fp.vehicle >= 0) g.removePedFromVehicle(fugitive, true);
                    g.notify("PALMERA BAIL BONDS", StrFormat("%s delivered alive. Benny wires $%lld.", f->name, won));
                    return MS_PASSED;
                }
                break;
            }
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 0: if (t.stageTime > 0.5f) t.teleportNear(pedPos(g, fugitive).xy(), 20.f); break;
            case 1:
                if (t.stageTime > 2.f) {
                    t.killEnemies();
                    if (pedAlive(g, fugitive)) {
                        g.peds[fugitive].health = g.peds[fugitive].maxHealth * 0.3f;
                        if (g.peds[fugitive].vehicle >= 0) g.removePedFromVehicle(fugitive, true);
                    }
                }
                break;
            case 2:
                if (g.playerVehicle() < 0 && t.stageTime > 0.3f) {
                    int v = spawnCar(g, pickModel(g, {Vehicles::VC_SEDAN}), playerPos(g) + vec3(3, 0, 0), 0.f);
                    t.enter(v);
                }
                if (g.playerVehicle() >= 0 && g.peds[fugitive].vehicle != g.playerVehicle()) g.warpPedIntoVehicle(fugitive, g.playerVehicle(), 1);
                testGoal(g, t, dt);
                break;
            default: break;
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Rook's Wishlist: car theft to order from Rook's garage. Each order names a kind of car and where one was last seen;
// find it, take it (some have alarms, owners or trackers), lose any police and deliver it in one piece. Pay follows the
// car's value and condition; every lap of the list pays a little more.
struct WishOrder {
    Vehicles::VehicleClass cls;
    int where;               // 0 Sol Beach, 1 Grove Hills, 2 Midtown, 3 North City, 4 Redland, 5 Harlow, 6 Port Isle, 7 Downtown
    int twist;               // 0 none, 1 alarm (one star), 2 the owner is around, 3 tracker (two stars)
    const char* order;       // Rook's call
    const char* got;         // Rook at the garage door
};
const WishOrder kWishlist[] = {
    {Vehicles::VC_SPORTS, 0, 0, "[calm]Buyer wants something fast and loud. There's a sports car sleeping on a Sol Beach curb. Wake it up.",
     "[happy:0.5]Beautiful. Still warm."},
    {Vehicles::VC_COUPE, 1, 1, "[calm]Two door coupe in Grove Hills. Rich street, fancy alarms. Be quick about it.",
     "[happy:0.4]Alarm's still ringing in my ears. Nice work."},
    {Vehicles::VC_MUSCLE, 4, 2, "[calm]Muscle car out in Redland. The owner sleeps next to it. Wake him up gently.", "[happy:0.4]He loved that car. Now I love it."},
    {Vehicles::VC_SUV, 2, 0, "[calm]An SUV for a family man. Don't ask. Midtown.", "[calm]Clean. Just how the family man likes it."},
    {Vehicles::VC_SUPER, 7, 3, "[calm]The big one. A supercar downtown. It has a tracker, so the cops will come. Lose them before you bring it here.",
     "[happy]Now that is a paycheck on wheels."},
    {Vehicles::VC_PICKUP, 5, 2, "[calm]Pickup truck in Harlow. The owner has a bat and opinions.", "[happy:0.3]Opinions noted. Truck received."},
    {Vehicles::VC_VAN, 6, 1, "[calm]A delivery van from Port Isle, logo and all. Don't ask why.", "[calm]Perfect. The logo is the whole point."},
    {Vehicles::VC_SEDAN, 3, 3, "[calm]A nice sedan in North City. Tracker on board, so shake the tail first.", "[happy:0.4]Quiet car, loud chase. Good job."},
};

const char* wishClassName(Vehicles::VehicleClass c) {
    switch (c) {
        case Vehicles::VC_SPORTS: return "sports car";
        case Vehicles::VC_COUPE: return "coupe";
        case Vehicles::VC_MUSCLE: return "muscle car";
        case Vehicles::VC_SUV: return "SUV";
        case Vehicles::VC_SUPER: return "supercar";
        case Vehicles::VC_PICKUP: return "pickup";
        case Vehicles::VC_VAN: return "van";
        default: return "sedan";
    }
}

class MissionWishlist : public StoryMission {
public:
    int level = 0;
    const WishOrder* w = nullptr;
    int car = -1, owner = -1, rook = -1;
    vec3 hint;
    bool ownerAngry = false;
    long long won = 0;
    const char* title() const override { return "Rook's Wishlist"; }
    const char* brief() const override {
        return "Rook takes orders for cars. Find the one on his list, take it, lose any police and bring it to his garage in one piece.";
    }
    long long reward() const override { return won; }
    const char* passBanner() const override { return "CAR DELIVERED"; }
    bool allowRetry() const override { return false; }

    void start(GameWorld& g) override {
        level = flag(g, EX_WISHLIST_LEVEL);
        w = &kWishlist[level % (int)ARRAY_COUNT(kWishlist)];
        const Places& P = gPlaces;
        const Place* spots[8] = {&P.beachCondo, &P.grove, &P.midtownPark, &P.northCity, &P.redland, &P.harlow, &P.portGate, &P.solarisOne};
        const Place& spot = *spots[Clamp(w->where, 0, 7)];
        u32 h = hash32((u32)level * 7919u + 0x315Eu);
        float yaw = spot.curbYaw;
        vec3 at = curbOffset(g, spot, hashToFloat(h) * 60.f - 30.f, &yaw);
        int model = pickModel(g, {w->cls}, h);
        if (model < 0) model = pickModel(g, {Vehicles::VC_SEDAN, Vehicles::VC_COUPE}, h);
        car = spawnCar(g, model, at, yaw);
        if (car >= 0) g.vehicles[car].sim.engineOn = false;
        // the search area: a point up to 70 m off the car
        vec2 off(hashToFloat(h >> 8) * 2.f - 1.f, hashToFloat(h >> 16) * 2.f - 1.f);
        hint = at + vec3(normalize(off + vec2(0.01f, 0.f)) * (30.f + hashToFloat(h >> 4) * 40.f), 0.f);
        if (w->twist == 2) {
            // the owner hangs around the car with a bat
            owner = g.mPed(g.randomCivilianChar(h ^ 0x0A11u, 0), dvec3(placeOffset(g, spot, hashToFloat(h) * 60.f - 30.f, 2.f)), spot.yaw + kPi,
                           FAC_CIVILIAN);
            if (owner >= 0) {
                arm(g, owner, WPN_BAT);
                g.peds[owner].brain.accuracy = 0.2f;
                setIdle(g, owner, 10);
            }
        }
        static const char* const kWhere[8] = {"Sol Beach", "Grove Hills", "Midtown", "North City", "Redland", "Harlow", "Port Isle", "Downtown"};
        goTo(g, hint, 55.f, StrFormat("Find the ~b~%s~s~ on Rook's list. Last seen around %s.", wishClassName(w->cls), kWhere[Clamp(w->where, 0, 7)]),
             false, false);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_PHONE_RING, 0.7f);
#endif
        phoneLine(g, CAST_ROOK, w->order);
        score(SC_NOIR, 0.35f, 22);
    }

    // the owner (twist 2) notices anyone at the car
    void updateOwner(GameWorld& g) {
        if (ownerAngry || !pedAlive(g, owner)) return;
        if (::length(playerPos(g) - vehPos(g, car)) < 10.f || g.playerInVehicle(car)) {
            ownerAngry = true;
            g.peds[owner].faction = FAC_ENEMY;
            enemies.push_back(owner);
            setCombat(g, owner, g.player, 0.2f);
            g.mSay(line("Owner", "[angry][shout]Hey! Get away from my car!", owner, 0xffc8c8c8u));
        }
    }

    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (vehicleLost(g, car, "the car on the list")) return MS_FAILED;
        switch (stage) {
            case 0:
                updateOwner(g);
                if (::length(playerPos(g) - vehPos(g, car)) < 55.f) {
                    clearGoal(g);
                    g.mBlipVehicle(car, UI::BLIP_VEHICLE);
                    g.mObjective(StrFormat("Take the ~b~%s~s~.", wishClassName(w->cls)));
                    sayMe(g, "[calm]There it is.");
                    next();
                }
                break;
            case 1:
                updateOwner(g);
                if (g.playerInVehicle(car)) {
                    g.mClearBlips();
                    if (w->twist == 1) {
                        setWanted(g, 1);
                        g.notify("ALARM", "The car alarm went off. Lose the police before you deliver.");
                    } else if (w->twist == 3) {
                        setWanted(g, 2);
                        phoneLine(g, CAST_ROOK, "[calm]The tracker is live. Lose the cops before you come anywhere near my garage.");
                    }
                    goTo(g, gPlaces.rookShop.curb, 6.f, "Bring it to ~y~Rook's garage~s~ in one piece.", true);
                    next();
                }
                break;
            case 2: {
                updateOwner(g);
                if (!g.playerInVehicle(car) && g.hudHelpTimer <= 0.f) g.help("Get back in the car on the list.", 2.f);
                if (arrived(g) && g.playerInVehicle(car)) {
                    if (g.pinfo.wanted > 0) {
                        if (g.hudHelpTimer <= 0.f) g.help("Rook won't open up with the police on your tail. Lose them first.", 2.5f);
                        break;
                    }
                    clearGoal(g);
                    const Vehicle& v = g.vehicles[car];
                    float cond = Clamp((Clamp(v.sim.health, 0.f, 1000.f) - 250.f) / 750.f, 0.25f, 1.f);
                    long long base = Clamp((long long)(g.vassets[v.model].spec.price * 0.12f), 1500ll, 18000ll) + (level / (int)ARRAY_COUNT(kWishlist)) * 500;
                    won = (long long)(base * cond) / 50 * 50;
                    setFlag(g, EX_WISHLIST_LEVEL, level + 1);
                    if (level + 1 >= (int)ARRAY_COUNT(kWishlist)) setFlag(g, SIDE_WISHLIST_ALL, 1);
                    g.removePedFromVehicle(g.player, false);
                    rook = spawnCast(g, CAST_ROOK, gPlaces.rookShop.door, gPlaces.rookShop.yaw + kPi, FAC_FRIEND);
                    if (rook >= 0) facePed(g, rook, playerPos(g));
                    say(g, CAST_ROOK, rook, cond > 0.8f ? w->got : "[angry:0.4]You call this one piece? I'm taking the dents out of your cut.");
                    g.notify("ROOK'S WISHLIST", StrFormat("Order %d delivered: $%lld (%d%% condition).", level + 1, won, (int)(cond * 100.f)));
                    next();
                }
                break;
            }
            case 3:
                if (!g.mTalking()) {
                    if (car >= 0 && g.vehicles[car].used) g.vehicles[car].persistent = false;
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }

    void autotest(GameWorld& g, MissionTest& t) override {
        float dt = g.dtLast;
        switch (stage) {
            case 0: if (t.stageTime > 0.5f) t.teleportNear(vehPos(g, car).xy(), 20.f); break;
            case 1:
                if (t.stageTime > 0.5f) {
                    t.killEnemies();
                    t.enter(car);
                }
                break;
            case 2:
                if (g.pinfo.wanted > 0 && t.stageTime > 1.f) setWanted(g, 0);   // losing the police is the police system's test
                testGoal(g, t, dt, 50.f);
                break;
            default: break;
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Open world: stunt jumps (unique + freestyle) and the signal jammers
struct StuntSpot {
    vec3 pos;
    vec2 dir;
    float minAir = 0.6f;    // seconds in the air that count as clearing this jump
    float minDist = 16.f;   // meters from takeoff to landing
};

struct ActivitiesState {
    bool init = false;
    std::vector<StuntSpot> stunts;
    std::vector<vec3> jammers;
    // stunt tracking
    bool airborne = false;
    float airTime = 0.f;
    vec3 takeoff;
    float maxZ = 0.f;
    float spin = 0.f, flips = 0.f;
    int uniqueCandidate = -1;
    bool slowmo = false;
    int lastVehicle = -1;
    float blink = 0.f;
    vec3 lastPos;            // vehicle position last frame (a teleport cancels the jump being tracked)
    // the last landed jump (strangers.cpp: Jaz films one)
    double lastJumpTime = -1.0;
    float lastJumpDist = 0.f, lastJumpAir = 0.f, lastJumpHeight = 0.f;
    bool lastJumpUpright = false;
    vec3 lastJumpLanding;
};
ActivitiesState gAct;

// ------------------------------------------------------------------------------------------------------------------
// Stunt ramps: steel kicker ramps set up on open ground (beach sand, farm fields, airport grass, the Sawgrass levees)
// where the run-up and the landing zone are flat and clear of buildings, trees, roads and site structures - some jump a
// canal. Collision is a fine staircase of boxes whose tops follow the deck (the wheels ride it); a launch assist at the
// lip turns the car's speed into the ramp's climb angle so every jump leaves cleanly, for traffic as well as the player.
constexpr float kRampLen = 9.f, kRampHalfW = 2.6f, kRampH = 2.4f;
constexpr int kRampCollisionKey = 1000000;   // collision "cell" keys 1000000 + i (streaming cells use keys < 2 * 80 * 80)

struct StuntRamp {
    vec3 foot;          // center of the ramp's foot on the ground
    vec2 dir;           // jump direction
    bool waterGap = false;
    bool collision = false;
};

struct RampSearch {
    std::atomic<int> state{0};   // 0 idle, 1 searching (job), 2 done
    std::vector<StuntRamp> found;
    double ms = 0.0;
    int tested = 0;
};
RampSearch gRampSearch;
std::vector<StuntRamp> gRamps;
Render::Model* gRampModel = nullptr;

void buildRampModel(GameWorld& g) {
    if (gRampModel || !g.renderer) return;
    MeshData m;
    const float L = kRampLen, W = kRampHalfW, H = kRampH;
    u32 matD = makeMat(MAT_METAL_PAINTED), matW = makeMat(MAT_WOOD), matS = makeMat(MAT_METAL_BRUSHED);
    u32 deck = packRGBA8(0.17f, 0.18f, 0.2f, 1.f), yellow = packRGBA8(0.95f, 0.7f, 0.06f, 1.f), side = packRGBA8(0.62f, 0.42f, 0.24f, 1.f),
        steel = packRGBA8(0.55f, 0.56f, 0.58f, 1.f), orange = packRGBA8(0.9f, 0.32f, 0.06f, 1.f);
    const vec3 up = normalize(vec3(0.f, -H, L));
    auto deckPt = [&](float x, float y, float lift) { return vec3(x, y, H * y / L) + up * lift; };
    m.quadFacing(deckPt(-W, 0, 0.02f), deckPt(W, 0, 0.02f), deckPt(W, L, 0.02f), deckPt(-W, L, 0.02f), vec2(0, 0), vec2(2 * W, 0), vec2(2 * W, L),
                 vec2(0, L), deck, matD, up);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float x0 = sgn * (W - 0.4f), x1 = sgn * (W - 0.08f);
        m.quadFacing(deckPt(x0, 0, 0.035f), deckPt(x1, 0, 0.035f), deckPt(x1, L, 0.035f), deckPt(x0, L, 0.035f), vec2(0, 0), vec2(0.3f, 0), vec2(0.3f, L),
                     vec2(0, L), yellow, matD, up);
    }
    // chevrons pointing up the ramp
    for (int k = 0; k < 4; k++) {
        float y0 = 0.9f + k * 1.9f;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            vec3 a = deckPt(sgn * 1.7f, y0, 0.035f), b = deckPt(sgn * 1.7f, y0 + 0.5f, 0.035f), c = deckPt(0.f, y0 + 1.35f, 0.035f), d = deckPt(0.f, y0 + 0.85f, 0.035f);
            m.quadFacing(a, b, c, d, vec2(0, 0), vec2(0.5f, 0), vec2(0.5f, 1.f), vec2(0, 1.f), yellow, matD, up);
        }
    }
    // steel lip, side panels, back wall and an orange frame
    m.boxAA(vec3(-W, L - 0.45f, H - 0.1f), vec3(W, L + 0.05f, H + 0.03f), steel, matS);
    auto triFacing = [&](vec3 a, vec3 b, vec3 c, vec3 facing, u32 col, u32 mat) {
        vec3 n = cross(b - a, c - a);
        if (dot(n, facing) < 0.f) std::swap(b, c);
        n = normalize(cross(b - a, c - a));
        vec3 t = normalize(b - a);
        u32 i0 = m.addVertex(a, n, t, vec2(a.y, a.z), col, mat), i1 = m.addVertex(b, n, t, vec2(b.y, b.z), col, mat);
        u32 i2 = m.addVertex(c, n, t, vec2(c.y, c.z), col, mat);
        m.tri(i0, i1, i2);
    };
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float x = sgn * W;
        triFacing(vec3(x, 0.f, 0.f), vec3(x, L, 0.f), vec3(x, L, H), vec3((float)sgn, 0.f, 0.f), side, matW);
        m.boxAA(vec3(x - 0.06f, L - 0.12f, 0.f), vec3(x + 0.06f, L, H), orange, matD);                                        // rear post
        m.boxAA(vec3(x - 0.06f, L * 0.5f - 0.06f, 0.f), vec3(x + 0.06f, L * 0.5f + 0.06f, H * 0.5f), orange, matD);          // mid post
    }
    m.quadFacing(vec3(-W, L, 0.f), vec3(W, L, 0.f), vec3(W, L, H), vec3(-W, L, H), vec2(0, 0), vec2(2 * W, 0), vec2(2 * W, H), vec2(0, H), side, matW,
                 vec3(0, 1, 0));
    gRampModel = g.renderer->dynamic->createModel(m);
}

namespace ramp_detail {

float surfZ(const GameWorld& g, vec2 p) {
    float z;
    if (g.roads->surfaceHeight(p, &z)) return z;
    return g.map->heightAt(p.x, p.y);
}

bool propBlocks(u8 type) {
    switch (type) {
        case World::PROP_PALM: case World::PROP_PALM_TALL: case World::PROP_TREE_OAK: case World::PROP_TREE_PINE: case World::PROP_CYPRESS:
        case World::PROP_MANGROVE: case World::PROP_LIFEGUARD_TOWER: case World::PROP_STREETLIGHT: case World::PROP_STREETLIGHT_DOUBLE:
        case World::PROP_TRAFFIC_LIGHT: case World::PROP_POWER_POLE: case World::PROP_BUS_STOP:
            return true;
        default: return false;
    }
}

// Run-up (70 m), ramp, flight and landing (100 m past the lip) along dir from foot: flat, dry (the flight may cross
// water), away from roads, buildings, site structures and trees.
bool corridorClear(const GameWorld& g, vec2 foot, vec2 dir, bool& waterGap, std::unordered_map<int, std::vector<World::PropInstance>>& veg) {
    const World::WorldMap& map = *g.map;
    vec2 right(dir.y, -dir.x);
    if (map.isWater(foot.x, foot.y)) return false;
    float z0 = surfZ(g, foot);
    if (z0 < 0.3f) return false;
    const float s0 = -70.f, s1 = kRampLen + 100.f;
    waterGap = false;
    // cheap checks first: centerline, then the sides
    for (int pass = 0; pass < 2; pass++)
        for (float s = s0; s <= s1; s += 3.f)
            for (int wi = 0; wi < (pass == 0 ? 1 : 2); wi++) {
                float w = pass == 0 ? 0.f : (wi == 0 ? -3.6f : 3.6f);
                vec2 p = foot + dir * s + right * w;
                bool flight = s > kRampLen + 5.f && s < kRampLen + 52.f;
                if (map.isWater(p.x, p.y)) {
                    if (!flight) return false;
                    waterGap = true;
                    continue;
                }
                float z = surfZ(g, p);
                if (flight ? z > z0 + 2.5f : fabsf(z - z0) > 1.4f) return false;
                if (g.roads->nearRoad(p, 5.f)) return false;
                if (g.buildings->pointInBuilding(p, 3.5f)) return false;
                const World::Pad* pad = World::gSites ? World::gSites->padAt(p) : nullptr;
                if (pad && (pad->kind == World::PAD_RUNWAY || pad->kind == World::PAD_TAXIWAY || pad->kind == World::PAD_DECK || pad->kind == World::PAD_RAMP))
                    return false;
            }
    // site structures near the corridor
    if (World::gSites) {
        vec2 a = foot + dir * s0, b = foot + dir * s1;
        for (const World::SiteElem& e : World::gSites->elems) {
            if (e.h < 0.4f) continue;
            vec2 ab = b - a;
            float t = Clamp(dot(e.c - a, ab) / dot(ab, ab), 0.f, 1.f);
            if (::length(e.c - (a + ab * t)) > e.radius() + 8.f) continue;
            for (float s = s0; s <= s1; s += 3.f) {
                vec2 p = foot + dir * s;
                vec2 d = p - e.c;
                if (fabsf(dot(d, e.ax)) <= e.hx + 4.5f && fabsf(dot(d, perp(e.ax))) <= e.hy + 4.5f) return false;
            }
        }
    }
    // vegetation and props with collision (generated per streaming cell; cached here)
    float minx = Min(foot.x + dir.x * s0, foot.x + dir.x * s1) - 6.f, maxx = Max(foot.x + dir.x * s0, foot.x + dir.x * s1) + 6.f;
    float miny = Min(foot.y + dir.y * s0, foot.y + dir.y * s1) - 6.f, maxy = Max(foot.y + dir.y * s0, foot.y + dir.y * s1) + 6.f;
    int cx0 = (int)floorf((minx + World::kWorldHalf) / World::kCellSize), cx1 = (int)floorf((maxx + World::kWorldHalf) / World::kCellSize);
    int cy0 = (int)floorf((miny + World::kWorldHalf) / World::kCellSize), cy1 = (int)floorf((maxy + World::kWorldHalf) / World::kCellSize);
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            if (cx < 0 || cy < 0 || cx >= World::kCellsPerSide || cy >= World::kCellsPerSide) return false;
            int key = cy * World::kCellsPerSide + cx;
            auto it = veg.find(key);
            if (it == veg.end()) {
                std::vector<World::PropInstance> props;
                World::scatterVegetation(cx, cy, props);
                it = veg.emplace(key, std::move(props)).first;
            }
            for (const World::PropInstance& pr : it->second) {
                if (!propBlocks(pr.type)) continue;
                vec2 d = pr.pos.xy() - foot;
                float along = dot(d, dir), across = fabsf(dot(d, right));
                if (along > s0 - 2.f && along < s1 + 2.f && across < 5.f) return false;
            }
        }
    return true;
}

// Deterministic search (runs on a worker thread at startup).
void searchRamps(const GameWorld& g, std::vector<StuntRamp>& out, int& tested) {
    const World::WorldMap& map = *g.map;
    std::unordered_map<int, std::vector<World::PropInstance>> veg;
    struct Zone {
        World::Region reg;
        int want;
    };
    const Zone zones[] = {{World::REG_BEACH, 3}, {World::REG_FARMLAND, 2}, {World::REG_REDLAND, 2}, {World::REG_SAWGRASS, 1}, {World::REG_AIRPORT, 1},
                          {World::REG_SUBURBS, 1}, {World::REG_KEYS, 1},    {World::REG_GROVE, 1},   {World::REG_FLATS, 1},    {World::REG_PORT, 1},
                          {World::REG_KEY_CORAL, 1}, {World::REG_BAY_ISLAND, 1}};
    int got[World::REG_COUNT] = {};
    u32 seed = 0x5EA1D5u;
    for (int attempt = 0; attempt < 60000 && (int)out.size() < 14; attempt++) {
        seed = hash32(seed + 0x9E3779B9u);
        float x = -World::kWorldHalf * 0.92f + hashToFloat(seed) * World::kWorldHalf * 1.84f;
        float y = -World::kWorldHalf * 0.92f + hashToFloat(hash32(seed ^ 0xA5u)) * World::kWorldHalf * 1.84f;
        World::Region r = map.regionAt(x, y);
        int want = 0;
        for (const Zone& z : zones)
            if (z.reg == r) want = z.want;
        if (got[r] >= want) continue;
        if (map.isWater(x, y)) continue;
        bool spaced = true;
        for (const StuntRamp& o : out)
            if (::length(o.foot.xy() - vec2(x, y)) < 900.f) spaced = false;
        if (!spaced) continue;
        // directions: along the coast on the beach, otherwise eight headings from a random start
        float base = hashToFloat(hash32(seed ^ 0x77u)) * kTwoPi;
        for (int k = 0; k < 8; k++) {
            float a = base + k * (kTwoPi / 8.f);
            vec2 dir(cosf(a), sinf(a));
            bool gap = false;
            tested++;
            if (!corridorClear(g, vec2(x, y), dir, gap, veg)) continue;
            StuntRamp sr;
            sr.foot = vec3(x, y, surfZ(g, vec2(x, y)));
            sr.dir = dir;
            sr.waterGap = gap;
            out.push_back(sr);
            got[r]++;
            break;
        }
    }
}

}  // namespace ramp_detail

// Collision: one solid block under the tall back half of the ramp (cars can't drive through it from the side or
// behind); the deck itself is ridden kinematically by updateRamps.
void addRampCollision(StuntRamp& r, int index) {
    if (r.collision || !Phys::gCollision) return;
    std::vector<World::CollisionBox> boxes;
    const float u0 = kRampLen * 0.55f, top = kRampH * 0.55f;
    World::CollisionBox b;
    b.c = vec3(r.foot.xy() + r.dir * ((u0 + kRampLen) * 0.5f), r.foot.z + (top - 0.5f) * 0.5f);
    b.ax = vec2(r.dir.y, -r.dir.x);   // box x axis runs across the ramp, y along it
    b.he = vec3(kRampHalfW, (kRampLen - u0) * 0.5f, (top + 0.5f) * 0.5f);
    boxes.push_back(b);
    Phys::gCollision->addCell(kRampCollisionKey + index, boxes, {});
    r.collision = true;
}

// Ride height (body origin above the ground) per vehicle, measured while it drives on flat ground near a ramp.
struct RideHeight {
    u32 uid;
    float h;
};
std::vector<RideHeight> gRideHeights;

float rideHeightOf(GameWorld& g, int v, bool measure) {
    const Vehicle& veh = g.vehicles[v];
    for (RideHeight& r : gRideHeights)
        if (r.uid == veh.uid) {
            if (measure && veh.sim.wheelsOnGround > 0) {
                vec3 p = veh.sim.body.pos.toVec3();
                r.h = Lerp(r.h, Clamp(p.z - g.groundHeight(p.x, p.y, p.z), 0.2f, 1.6f), 0.2f);
            }
            return r.h;
        }
    vec3 p = veh.sim.body.pos.toVec3();
    float h = veh.sim.wheelsOnGround > 0 ? Clamp(p.z - g.groundHeight(p.x, p.y, p.z), 0.2f, 1.6f) : 0.6f;
    if (gRideHeights.size() > 64) gRideHeights.erase(gRideHeights.begin());
    gRideHeights.push_back({veh.uid, h});
    return h;
}

// Vehicles ride the deck kinematically: held at the deck height, pitched to its angle, their vertical speed matched to
// the climb, so they leave the lip with the ramp's launch angle and their full speed.
void updateRamps(GameWorld& g) {
    if (gRamps.empty()) return;
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    const float slope = kRampH / kRampLen, pitch = atanf(slope);
    for (size_t i = 0; i < gRamps.size(); i++) {
        StuntRamp& r = gRamps[i];
        float d = ::length(r.foot.xy() - pp.xy());
        if (d > 900.f) continue;
        drawModel(g, gRampModel, r.foot, atan2f(-r.dir.x, r.dir.y), vec3(1.f), vec3(1.f), 0.f, 0xD000000000ull + (u64)i);
        if (d > 400.f) continue;
        vec2 right(r.dir.y, -r.dir.x);
        for (int v = 0; v < (int)g.vehicles.size(); v++) {
            Vehicle& veh = g.vehicles[v];
            if (!veh.used || veh.exploded || veh.scripted || g.isBoat(v) || g.isAircraft(v)) continue;
            Vehicles::VehicleState& s = veh.sim;
            vec3 p = s.body.pos.toVec3();
            vec2 rel = p.xy() - r.foot.xy();
            float along = dot(rel, r.dir), across = dot(rel, right);
            if (fabsf(across) > kRampHalfW + 3.f || along < -12.f || along > kRampLen + 1.f) continue;
            if (along < -0.4f || fabsf(across) > kRampHalfW + 0.2f) {
                rideHeightOf(g, v, true);   // approaching: keep the ride height current
                continue;
            }
            vec2 fwdDir = s.forward().xy();
            float fwd = dot(s.body.vel.xy(), r.dir);
            if (dot(fwdDir, r.dir) < 0.5f || fwd < 1.f) continue;   // only cars driving up the ramp
            float deckZ = r.foot.z + kRampH * Clamp(along / kRampLen, 0.f, 1.f);
            float wantZ = deckZ + rideHeightOf(g, v, false);
            if ((float)s.body.pos.z > wantZ + 0.25f) continue;       // already flying over it
            s.body.pos.z = Max((double)wantZ, s.body.pos.z);
            s.body.vel.z = Max(s.body.vel.z, fwd * slope);
            // nose up along the deck, keeping the car's own heading
            vec2 h = normalize(fwdDir);
            float yaw = atan2f(-h.x, h.y);
            s.body.rot = quatAxisAngle(vec3(0, 0, 1), yaw) * quatAxisAngle(vec3(1, 0, 0), pitch * dot(h, r.dir));
            s.body.angVel = vec3(0.f);
            s.sleeping = false;
        }
    }
}

void findStuntSpots(GameWorld& g) {
    // road crests: sharp convex changes of the road profile where a fast car leaves the ground
    struct Cand {
        vec3 p;
        vec2 dir;
        float score;
    };
    std::vector<Cand> cands;
    for (const World::RoadEdge& e : g.roads->edges) {
        if (e.cls == World::RC_LANE || e.cls == World::RC_DIRT || e.length < 60.f) continue;
        for (float s = 20.f; s < e.length - 20.f; s += 8.f) {
            float z0 = e.posAt(s - 16.f).z, z1 = e.posAt(s).z, z2 = e.posAt(s + 16.f).z;
            float curv = (z0 - 2.f * z1 + z2) / (16.f * 16.f);
            float rise = z1 - Min(z0, z2);
            if (curv < -0.012f && rise > 1.2f) {
                vec3 p = e.posAt(s);
                vec3 t = e.tangentAt(s);
                float sc = -curv * 100.f + rise;
                cands.push_back({p, normalize(vec2(t.x, t.y)), sc});
            }
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (const Cand& c : cands) {
        bool farEnough = true;
        for (const StuntSpot& s : gAct.stunts)
            if (::length(s.pos.xy() - c.p.xy()) < 700.f) farEnough = false;
        if (!farEnough) continue;
        gAct.stunts.push_back({c.p, c.dir, 0.6f, 16.f});
        if (gAct.stunts.size() >= 6) break;
    }
    // stunt ramps: searched on a worker thread, added when ready (see updateRampSearch)
    gRampSearch.state = 1;
    const GameWorld* gp = &g;
    Jobs::submit([gp] {
        double t0 = TimeSeconds();
        ramp_detail::searchRamps(*gp, gRampSearch.found, gRampSearch.tested);
        gRampSearch.ms = (TimeSeconds() - t0) * 1000.0;
        gRampSearch.state.store(2);
    }, kJobNormal);
    LOG("Stunt jumps: %d road crests (%d candidates); ramps being placed", (int)gAct.stunts.size(), (int)cands.size());
}

void updateRampSearch(GameWorld& g) {
    if (gRampSearch.state.load() != 2) return;
    gRampSearch.state.store(3);
    gRamps = gRampSearch.found;
    int gaps = 0;
    for (size_t i = 0; i < gRamps.size(); i++) {
        StuntRamp& r = gRamps[i];
        addRampCollision(r, (int)i);
        gaps += r.waterGap ? 1 : 0;
        vec3 lip = r.foot + vec3(r.dir * kRampLen, kRampH);
        gAct.stunts.push_back({lip, r.dir, 0.9f, 24.f});
    }
    LOG("Stunt ramps: %d placed (%d over water) in %.0f ms (%d corridors tested); %d unique stunt jumps", (int)gRamps.size(), gaps, gRampSearch.ms,
        gRampSearch.tested, (int)gAct.stunts.size());
}

void placeJammers(GameWorld& g) {
    // 30 hush boxes on street corners across Porto Sol's urban districts (deterministic)
    u32 seed = 0x4A5B5u;
    int attempts = 0;
    while ((int)gAct.jammers.size() < 30 && attempts < 20000) {
        attempts++;
        seed = hash32(seed + 0x9E3779B9u);
        float x = -1200.f + hashToFloat(seed) * 7000.f;
        float y = -5000.f + hashToFloat(hash32(seed ^ 0x55u)) * 10500.f;
        World::Region r = g.map->regionAt(x, y);
        bool urban = r == World::REG_DOWNTOWN || r == World::REG_FINANCIAL || r == World::REG_MIDTOWN || r == World::REG_NORTH_CITY ||
                     r == World::REG_CALLE_LUNA || r == World::REG_BEACH || r == World::REG_FLATS || r == World::REG_GROVE ||
                     r == World::REG_PORT || r == World::REG_KEY_CORAL || r == World::REG_AIRPORT;
        if (!urban) continue;
        Place p = resolvePlace(g, vec2(x, y), hashToFloat(seed >> 5) * 20.f - 10.f);
        if (p.edge < 0) continue;
        vec2 q = p.pos.xy() + p.outward * 1.2f;
        if (g.map->isWater(q.x, q.y)) continue;
        bool close = false;
        for (const vec3& j : gAct.jammers)
            if (::length(j.xy() - q) < 380.f) close = true;
        if (close) continue;
        gAct.jammers.push_back(vec3(q, groundAt(g, q.x, q.y, p.pos.z + 3.f)));
    }
}

bool jammerDone(GameWorld& g, int i) { return (flag(g, EX_JAMMERS) >> i) & 1; }

void destroyJammer(GameWorld& g, int i) {
    setFlag(g, EX_JAMMERS, flag(g, EX_JAMMERS) | (1 << i));
    int n = 0;
    for (int k = 0; k < (int)gAct.jammers.size(); k++) n += jammerDone(g, k) ? 1 : 0;
    setFlag(g, EX_JAMMER_COUNT, n);
    spawnFx(FX_SPARKS, dvec3(gAct.jammers[i] + vec3(0, 0, 0.3f)), vec3(0, 0, 2.f), 20, 1.f);
    spawnFx(FX_SMOKE, dvec3(gAct.jammers[i] + vec3(0, 0, 0.3f)), vec3(0, 0, 1.f), 4, 0.6f);
#ifdef HAVE_AUDIO
    Audio::play(Audio::SFX_EXPLOSION_SMALL, gAct.jammers[i], 0.5f, 1.8f);
#endif
    long long reward = n == (int)gAct.jammers.size() ? 25000 : 500;
    money(g, reward);
    g.notify("SIGNAL JAMMER", StrFormat("%d of %d destroyed  (+$%lld)", n, (int)gAct.jammers.size(), reward));
    if (n == (int)gAct.jammers.size()) {
        setFlag(g, SIDE_JAMMERS_ALL, 1);
        g.bigMessage("ALL JAMMERS DESTROYED", "Pulse FM comes in loud and clear", 0xff33ccffu);
    }
}

void updateStunts(GameWorld& g, float dt) {
    int pv = g.playerVehicle();
    Ped* pl = g.playerPed();
    if (!pl) return;
    bool eligible = pv >= 0 && g.peds[g.player].seat == 0 && !g.isAircraft(pv) && !g.isBoat(pv);
    if (!eligible || pv != gAct.lastVehicle) {
        gAct.airborne = false;
        gAct.lastVehicle = pv;
        if (gAct.slowmo) {
            gAct.slowmo = false;
            g.timeScale = 1.f;
        }
        if (!eligible) return;
    }
    const Vehicles::VehicleState& s = g.vehicles[pv].sim;
    vec3 p = s.body.pos.toVec3();
    bool air = s.wheelsOnGround == 0 && !s.inWater;
    bool teleported = ::length(p - gAct.lastPos) > Max(40.f, s.speed() * 0.5f);
    gAct.lastPos = p;
    if (teleported) {
        // scripted placement (mission start, retry, test driver): not a jump
        gAct.airborne = false;
        if (gAct.slowmo) {
            gAct.slowmo = false;
            g.timeScale = 1.f;
        }
        return;
    }
    if (air && !gAct.airborne) {
        gAct.airborne = true;
        gAct.airTime = 0.f;
        gAct.takeoff = p;
        gAct.maxZ = p.z;
        gAct.spin = 0.f;
        gAct.flips = 0.f;
        gAct.uniqueCandidate = -1;
        if (s.speed() > 18.f) {
            vec2 vd = normalize(s.body.vel.xy());
            for (int i = 0; i < (int)gAct.stunts.size(); i++) {
                if ((flag(g, EX_STUNTS) >> i) & 1) continue;
                const StuntSpot& st = gAct.stunts[i];
                if (::length(st.pos.xy() - p.xy()) < 28.f && fabsf(dot(vd, st.dir)) > 0.7f) gAct.uniqueCandidate = i;
            }
        }
    }
    if (gAct.airborne) {
        gAct.airTime += dt / Max(g.timeScale, 0.05f);
        gAct.maxZ = Max(gAct.maxZ, p.z);
        vec3 f = s.forward();
        gAct.flips += fabsf(dot(s.body.angVel, s.right())) * dt / kTwoPi;
        gAct.spin += fabsf(dot(s.body.angVel, f)) * dt / kTwoPi;
        if (gAct.uniqueCandidate >= 0 && gAct.airTime > 0.35f && !gAct.slowmo && !gMissions.active) {
            gAct.slowmo = true;
            g.timeScale = 0.4f;
        }
        if (!air && gAct.airTime > 0.1f) {
            gAct.airborne = false;
            if (gAct.slowmo) {
                gAct.slowmo = false;
                g.timeScale = 1.f;
            }
            float dist = ::length(p.xy() - gAct.takeoff.xy());
            float height = gAct.maxZ - Max(gAct.takeoff.z, p.z);
            bool upright = s.up().z > 0.5f;
            gAct.lastJumpTime = g.time;
            gAct.lastJumpDist = dist;
            gAct.lastJumpAir = gAct.airTime;
            gAct.lastJumpHeight = height;
            gAct.lastJumpUpright = upright;
            gAct.lastJumpLanding = p;
            if (gMissions.test.active && gAct.airTime > 0.3f)
                LOG("[missiontest] stunt: %.2f s in the air, %.1f m, %.1f m high, unique spot %d, upright %d", gAct.airTime, dist, height,
                    gAct.uniqueCandidate, (int)upright);
            const StuntSpot* us = gAct.uniqueCandidate >= 0 ? &gAct.stunts[gAct.uniqueCandidate] : nullptr;
            if (us && gAct.airTime > us->minAir && dist > us->minDist && upright) {
                int i = gAct.uniqueCandidate;
                setFlag(g, EX_STUNTS, flag(g, EX_STUNTS) | (1 << i));
                int n = 0;
                for (int k = 0; k < (int)gAct.stunts.size(); k++) n += (flag(g, EX_STUNTS) >> k) & 1;
                setFlag(g, EX_STUNT_COUNT, n);
                money(g, 1500);
                g.bigMessage("UNIQUE STUNT BONUS", StrFormat("%d of %d  +$1500", n, (int)gAct.stunts.size()), 0xff33ccffu);
                g.socialReport(UI::TE_STUNT_JUMP, dvec3(p), nullptr, dist);
                if (n == (int)gAct.stunts.size()) {
                    setFlag(g, SIDE_STUNTS_ALL, 1);
                    money(g, 10000);
                }
            } else if (gAct.airTime > 1.4f && dist > 25.f) {
                int fl = (int)(gAct.flips + 0.3f), rolls = (int)(gAct.spin + 0.3f);
                long long bonus = (long long)(dist * 3.f + height * 12.f + fl * 250 + rolls * 150);
                if (!upright) bonus /= 2;
                const char* kind = gAct.airTime > 3.f || fl + rolls >= 2 ? "INSANE STUNT BONUS" : (fl + rolls >= 1 ? "DOUBLE STUNT BONUS" : "STUNT BONUS");
                money(g, bonus);
                g.bigMessage(kind, StrFormat("%.0f m, %.1f m high%s%s  +$%lld", dist, height, fl ? StrFormat(", %d flip%s", fl, fl > 1 ? "s" : "").c_str() : "",
                                             rolls ? StrFormat(", %d roll%s", rolls, rolls > 1 ? "s" : "").c_str() : "", bonus),
                             0xff40e0ffu);
                setFlag(g, EX_INSANE_BEST, Max(flag(g, EX_INSANE_BEST), (int)bonus));
                g.socialReport(UI::TE_STUNT_JUMP, dvec3(p), nullptr, dist);
            }
        }
    }
}

void updateJammers(GameWorld& g, float dt) {
    gAct.blink += dt;
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    vec3 o, d;
    bool shot = playerShotRay(g, o, d);
    int found = flag(g, EX_JAMMER_COUNT);
    for (int i = 0; i < (int)gAct.jammers.size(); i++) {
        if (jammerDone(g, i)) continue;
        const vec3& j = gAct.jammers[i];
        float dist = ::length(j - pp);
        if (dist > 160.f) continue;
        drawModel(g, gJammerModel, j, (float)i * 1.3f, vec3(1.f), vec3(1.f), 1.f, 0xC000000000ull + (u64)i);
        if (fmodf(gAct.blink + i * 0.37f, 1.2f) < 0.25f && dist < 90.f) spawnLight(dvec3(j + vec3(0, 0, 0.3f)), vec3(1.f, 0.05f, 0.02f) * 120.f, 3.f);
        // melee / shots
        if (dist < 1.8f && g.ctl.attack.pressed && pl->state == PS_ONFOOT && (pl->weapon == WPN_FISTS || weaponInfo(pl->weapon).clipSize == 0)) {
            destroyJammer(g, i);
            continue;
        }
        if (shot) {
            float t;
            float off = rayPointDist(o, d, j + vec3(0, 0, 0.2f), &t);
            if (off < 0.4f && t < 90.f && g.lineOfSight(dvec3(o), dvec3(j + vec3(0, 0, 0.25f) - d * 0.4f), g.player, g.playerVehicle())) {
                destroyJammer(g, i);
                shot = false;
                continue;
            }
        }
        // after ten, Kit triangulates the rest: blips within 400 m
        if (found >= 10 && dist < 400.f) {
            UI::Blip b;
            b.pos = j.xy();
            b.icon = UI::BLIP_COLLECTIBLE;
            b.color = 0xff3030ffu;
            b.scale = 0.7f;
            b.shortRange = true;
            b.label = "Signal jammer";
            g.missionBlips.push_back(b);
        }
    }
}

void activitiesUpdate(GameWorld& g, float dt) {
    if (!gAct.init) {
        gAct.init = true;
        buildActivityModels(g);
        buildRampModel(g);
        findStuntSpots(g);
        placeJammers(g);
    }
    updateRampSearch(g);
    updateRamps(g);
    updateStunts(g, dt);
    updateJammers(g, dt);
    // unique stunt jump blips nearby
    Ped* pl = g.playerPed();
    if (pl && !gMissions.active)
        for (int i = 0; i < (int)gAct.stunts.size(); i++) {
            if ((flag(g, EX_STUNTS) >> i) & 1) continue;
            if (::length(gAct.stunts[i].pos.xy() - pl->pos.toVec3().xy()) > 300.f) continue;
            UI::Blip b;
            b.pos = gAct.stunts[i].pos.xy();
            b.icon = UI::BLIP_STUNT_JUMP;
            b.shortRange = true;
            b.label = "Stunt jump";
            g.missionBlips.push_back(b);
        }
    // job vehicles: G / R3 in a taxi, police car or ambulance starts the job
    int pv = g.playerVehicle();
    if (pv >= 0 && !gMissions.active && g.peds[g.player].seat == 0 && g.playerControl) {
        Vehicles::VehicleClass c = g.vassets[g.vehicles[pv].model].spec.cls;
        const char* job = c == Vehicles::VC_TAXI ? "taxi" : (c == Vehicles::VC_POLICE ? "vigilante" : (c == Vehicles::VC_AMBULANCE ? "paramedic" : nullptr));
        if (job) {
            static int lastHintVeh = -1;
            const char* jobName = c == Vehicles::VC_TAXI ? "taxi" : (c == Vehicles::VC_POLICE ? "vigilante" : "paramedic");
            if (lastHintVeh != pv) {
                lastHintVeh = pv;
                g.help(StrFormat("Press ~i:G|RS~ to start the %s job, or use the phone.", jobName), 5.f);   // pad: R3
            }
            if (g.ctl.special.pressed && g.pinfo.wanted == 0) {
                int di = gMissions.findDef(job);
                if (di >= 0) {
                    gMissions.startCheckpoint = 0;
                    g.startMission(di);
                }
            }
        }
    }
}

}  // namespace mu
}  // namespace Game
