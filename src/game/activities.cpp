// Side activities: street races (night races in Calle Luna and Sol Beach, the Overseas Highway run...), boat races,
// flight school, taxi fares, courier runs, vigilante and paramedic jobs, the shooting range challenge, stunt jumps
// (unique jumps + freestyle stunt bonus) and the 30 signal jammers ("hush boxes") hidden around Porto Sol.
#include "missions.h"

namespace Game {
namespace mu {

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

class MissionRace : public StoryMission {
public:
    RaceSpec spec;
    RaceCourse race;
    float countdown = 0.f;
    int lastCount = 4;
    float raceTime = 0.f;
    bool paid = false;
    long long won = 0;
    MissionRace(const RaceSpec& s) : spec(s) {}
    const char* title() const override { return spec.name; }
    const char* brief() const override { return "Win the race. Entry fee is paid at the start line; the winner takes the prize money."; }
    long long reward() const override { return won; }
    const char* passBanner() const override { return "RACE WON"; }
    const char* failBanner() const override { return "RACE LOST"; }

    void start(GameWorld& g) override {
        if (spec.night && g.env->timeOfDay > 5.f && g.env->timeOfDay < 20.f) g.env->timeOfDay = 21.f;
        bool water = spec.domain == 1;
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
            int drv = g.mPed(g.randomCivilianChar(0xACE0u + (u32)i * 13u, 0), dvec3(sp), yaw, FAC_CIVILIAN);
            if (drv >= 0) g.warpPedIntoVehicle(drv, v, 0);
            race.racers.push_back(v);
            ScriptDriver& d = addDriver(g, v, race.path, spec.racerSpeed + i * 1.2f, water ? DRV_WATER : DRV_ROAD, 0);
            d.racer = true;
            d.rubberPed = g.player;
            d.speedScale = 0.f;
        }
        if (!paid) {
            money(g, -spec.fee);
            paid = true;
        }
        g.mObjective(StrFormat("~y~%s~s~  Entry fee $%lld, prize $%lld", spec.name, spec.fee, spec.prize));
        score(SC_CHASE, 0.45f, 5 + spec.bestSlot);
        countdown = 4.f;
        lastCount = 4;
        setStage(1);
    }

    MissionStatus update(GameWorld& g, float dt) override {
        if (vehicleLost(g, playerCar, spec.domain == 1 ? "boat" : "car")) return MS_FAILED;
        switch (stage) {
            case 1: {
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
                        for (int v : race.racers) releaseDriver(g, v);
                        return MS_PASSED;
                    }
                    return fail(StrFormat("You finished %d%s.", pos, pos == 2 ? "nd" : (pos == 3 ? "rd" : "th")).c_str());
                }
                break;
            }
        }
        return MS_RUNNING;
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
// Taxi fares: in any taxi, press G / D-pad up to go on duty.
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
        g.notify("TAXI", "On duty. Leave the cab or press ~i:G|UP~ again to end the shift.");
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
// Vigilante: in a police car, press G / D-pad up. Stop fleeing suspects, level by level.
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
// Paramedic: in an ambulance, press G / D-pad up. Pick up the injured and rush them to a hospital.
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
        vec2 best = kHospitals[0];
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
};
ActivitiesState gAct;

// ------------------------------------------------------------------------------------------------------------------
// Stunt ramps: steel kicker ramps set up on open ground (beach sand, farm fields, airport grass, the Sawgrass levees)
// where the run-up and the landing zone are flat and clear of buildings, trees, roads and site structures - some jump a
// canal. Collision is a fine staircase of boxes whose tops follow the deck (the wheels ride it); a launch assist at the
// lip turns the car's speed into the ramp's climb angle so every jump leaves cleanly, for traffic as well as the player.
constexpr float kRampLen = 9.f, kRampHalfW = 2.6f, kRampH = 2.4f;
constexpr int kRampSteps = 18;
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

void addRampCollision(StuntRamp& r, int index) {
    if (r.collision || !Phys::gCollision) return;
    std::vector<World::CollisionBox> boxes;
    vec2 ax(r.dir.y, -r.dir.x);   // box x axis runs across the ramp, y along it
    for (int k = 0; k < kRampSteps; k++) {
        float top = kRampH * (k + 0.5f) / kRampSteps;
        float along = (k + 0.5f) * kRampLen / kRampSteps;
        World::CollisionBox b;
        b.c = vec3(r.foot.xy() + r.dir * along, r.foot.z + (top - 0.5f) * 0.5f);
        b.ax = ax;
        b.he = vec3(kRampHalfW, kRampLen / kRampSteps * 0.5f, (top + 0.5f) * 0.5f);
        boxes.push_back(b);
    }
    Phys::gCollision->addCell(kRampCollisionKey + index, boxes, {});
    r.collision = true;
}

// Launch assist: a vehicle crossing the lip leaves along the deck's climb angle with its full speed.
void updateRamps(GameWorld& g) {
    if (gRamps.empty()) return;
    Ped* pl = g.playerPed();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    const float slope = kRampH / kRampLen;
    const vec3 upT = normalize(vec3(0.f, 1.f, slope));   // along-deck tangent in (along, -, up) terms
    for (size_t i = 0; i < gRamps.size(); i++) {
        StuntRamp& r = gRamps[i];
        float d = ::length(r.foot.xy() - pp.xy());
        if (d > 900.f) continue;
        drawModel(g, gRampModel, r.foot, atan2f(-r.dir.x, r.dir.y), vec3(1.f), vec3(1.f), 0.f, 0xD000000000ull + (u64)i);
        if (d > 400.f) continue;
        vec2 right(r.dir.y, -r.dir.x);
        for (int v = 0; v < (int)g.vehicles.size(); v++) {
            Vehicle& veh = g.vehicles[v];
            if (!veh.used || veh.exploded || g.isBoat(v) || g.isAircraft(v)) continue;
            Vehicles::VehicleState& s = veh.sim;
            vec3 p = s.body.pos.toVec3();
            vec2 rel = p.xy() - r.foot.xy();
            float along = dot(rel, r.dir), across = dot(rel, right);
            if (fabsf(across) > kRampHalfW + 0.4f || along < kRampLen - 1.4f || along > kRampLen + 0.6f) continue;
            if (p.z < r.foot.z + kRampH * 0.55f || p.z > r.foot.z + kRampH + 2.5f) continue;   // actually on the deck
            float fwd = dot(s.body.vel.xy(), r.dir);
            if (fwd < 7.f) continue;
            float speed = Max(fwd, ::length(vec2(fwd, s.body.vel.z)));
            float wantZ = speed * upT.z;
            if (s.body.vel.z >= wantZ * 0.92f) continue;
            float lateral = dot(s.body.vel.xy(), right);
            vec2 horiz = r.dir * (speed * upT.y) + right * lateral;
            s.body.vel = vec3(horiz, wantZ);
            s.body.angVel *= 0.35f;
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
    // job vehicles: G / D-pad up in a taxi, police car or ambulance starts the job
    int pv = g.playerVehicle();
    if (pv >= 0 && !gMissions.active && g.peds[g.player].seat == 0 && g.playerControl) {
        Vehicles::VehicleClass c = g.vassets[g.vehicles[pv].model].spec.cls;
        const char* job = c == Vehicles::VC_TAXI ? "taxi" : (c == Vehicles::VC_POLICE ? "vigilante" : (c == Vehicles::VC_AMBULANCE ? "paramedic" : nullptr));
        if (job) {
            static int lastHintVeh = -1;
            const char* jobName = c == Vehicles::VC_TAXI ? "taxi" : (c == Vehicles::VC_POLICE ? "vigilante" : "paramedic");
            if (lastHintVeh != pv) {
                lastHintVeh = pv;
                // D-pad up opens the phone on a gamepad, so pad players start the job from the phone's job tile
                if (g.ctl.usingPad) g.help(StrFormat("Open the phone (~i:UP|UP~) and choose the %s job to start it.", jobName), 5.f);
                else g.help(StrFormat("Press ~i:G|UP~ to start the %s job, or use the phone.", jobName), 5.f);
            }
            if (g.ctl.special.pressed && !g.ctl.usingPad && g.pinfo.wanted == 0) {
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
