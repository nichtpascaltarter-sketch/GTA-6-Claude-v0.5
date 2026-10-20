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
        if (in.ok() && in.marker(World::IM_SERVICE, liftL)) {
            float side = liftL.x > (in.d->x0 + in.d->x1) * 0.5f ? 1.f : -1.f;   // the wall beside the drive-in bay
            showCar = spawnCar(g, prize, in.at(liftL), in.yaw(kPi), kLivery);
            vec3 laloL(liftL.x + side * 1.9f, Max(1.4f, liftL.y - 2.9f), 0.f);
            vec3 meL(liftL.x - side * 0.4f, Max(1.2f, liftL.y - 5.4f), 0.f);
            lp = in.at(laloL);
            mp = in.at(meL);
            lalo = g.mPed(laloChar(g), dvec3(lp), in.yaw(kPi), FAC_FRIEND);
            placePlayer(g, mp, 0.f);
            shots.push_back(shotRoom(in, vec3(liftL.x - side * 2.6f, liftL.y + 1.6f, 2.1f), lp, mp, 6.5f));
        } else {
            // no shop interior in this world: the curb outside
            const Place& T = gPlaces.resprayCL;
            float yaw = T.curbYaw;
            vec3 cp0 = curbOffset(g, T, -6.f, &yaw);
            showCar = spawnCar(g, prize, cp0, yaw, kLivery);
            lp = placeOffset(g, T, -2.f, 1.5f);
            mp = placeOffset(g, T, -3.5f, 3.5f);
            lalo = g.mPed(laloChar(g), dvec3(lp), 0.f, FAC_FRIEND);
            placePlayer(g, mp, 0.f);
            establish(g, shots, lp, T.yaw, 14.f, 4.f, 4.f);
        }
        if (lalo >= 0) g.peds[lalo].brain.type = BRAIN_NONE;
        facePed(g, lalo, mp);
        facePed(g, g.player, lp);
        shots.push_back(shotTwo(lp, mp, 6.f));
        if (showCar >= 0) shots.push_back(shotVehicle(g, showCar, 5.f, 1.f, 44.f));
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
        // the grid: the pole sitter's car low and close, then behind the player's
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
                if (g.fadedOut()) {
                    setupLeg(g);
                    g.fadeIn(1.2f);
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
        if (!findWater(g, R.hint, 1.8f, drop, 500.f)) drop = vec3(R.hint, g.map->waterAt(R.hint.x, R.hint.y));
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
