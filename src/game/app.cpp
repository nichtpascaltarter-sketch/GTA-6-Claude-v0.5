// Application: window/device setup, background world generation with a loading screen, front-end menus,
// the gameplay loop, pause menu, debug free camera and automated test hooks (--shot, --autoplay).
#include "../world/worldmap.h"
#include "../world/roads.h"
#include "../world/buildings.h"
#include "../render/renderer.h"
#include "../ui/draw2d.h"
#include "../platform/platform.h"

#include "../game/viewer.cpp"

#if defined(HAVE_CHARACTERS) && defined(HAVE_VEHICLE_SIM) && defined(HAVE_VEHICLE_MODELS) && defined(HAVE_AUDIO)
#define HAVE_GAMEPLAY 1
#include "../game/game_all.cpp"
#endif

namespace Game {

struct Shot {
    dvec3 pos;
    float yaw, pitch, time;
    std::string name;
};

enum AppState { AS_LOADING = 0, AS_MENU, AS_PLAYING, AS_FREECAM };

struct App {
    World::WorldMap map;
    World::RoadNetwork roads;
    World::BuildingSet buildings;
    Phys::CollisionWorld collision;
    Viewer viewer;
    Render::Renderer renderer;
    Render::Camera cam;
    Render::Environment env;
#ifdef HAVE_GAMEPLAY
    GameWorld game;
    WeatherSystem weather;
#endif
#ifdef HAVE_GAME_UI
    UI::MenuState menu;
    UI::HudState hud;
#endif
    AppState state = AS_LOADING;
    std::thread loader;
    std::atomic<int> loadStage{0};
    std::atomic<bool> loadDone{false};
    double loadStart = 0;
    float camSpeed = 40.f;
    bool showDebug = false;
    double lastTime = 0;
    float fps = 0, frameMs = 0;
    std::vector<Shot> shots;
    int shotIndex = 0, shotFrame = 0;
    int shotSettleFrames = 12;
    int shotWait = 0;
    bool autotest = false;
    bool pendingPhoto = false;   // photo mode: save the next finished frame
    // --autoplay tour: visit districts at different hours, one screenshot per stop (scorecard evidence)
    int tourStop = -1;
    int tourFirst = 0, tourCount = 99;   // --tourstart N --tourcount M: run a slice of the tour (repro a single stop)
    float tourT = 0.f;
    bool tourShot = false, tourDone = false;
    int meleeVictim = -1;        // --autoplay melee: the civilian for the takedown
    int renderEvery = 1;         // --renderevery N: automated runs render every Nth gameplay frame (+ screenshot frames)
    float skippedDt = 0.f;       // game time since the last rendered frame
    u32 playFrames = 0;
    // autoplay test scripts (--autoplay walk|drive|bike|fly|boat|shoot)
    std::string autoplay;
    float autoTime = 0.f;
    int autoShot = 0;
    float autoShotEvery = 4.f;
    float autoDuration = 30.f;
    float menuCamT = 0.f;

    bool init() {
        int w = Platform::argValue("width") ? atoi(Platform::argValue("width")) : 1600;
        int h = Platform::argValue("height") ? atoi(Platform::argValue("height")) : 900;
        autotest = Platform::hasArg("autotest");
        if (!Platform::init("Neon Tide", w, h, !autotest && Platform::hasArg("fullscreen"), false)) return false;
        int workers = Max(2, (int)std::thread::hardware_concurrency() - 1);
        Jobs::init(workers);
        if (!gfx::init(Platform::windowHandle(), Platform::clientWidth(), Platform::clientHeight(), Platform::hasArg("d3ddebug")))
            FatalError("Could not initialize Direct3D 11. A DirectX 11 capable GPU and up-to-date drivers are required.");
        UI::init();
#ifdef HAVE_AUDIO
        if (!Platform::hasArg("nosound") && !Audio::init()) LOG("Audio: no output device, continuing silently");
#endif
        loadStart = TimeSeconds();
        parseShots();
        if (const char* a = Platform::argValue("autoplay")) autoplay = a;
        if (const char* d = Platform::argValue("autoduration")) autoDuration = (float)atof(d);
        if (const char* d = Platform::argValue("autoevery")) autoShotEvery = (float)atof(d);
        if (const char* d = Platform::argValue("renderevery")) renderEvery = Max(1, atoi(d));
        if (const char* d = Platform::argValue("tourstart")) tourFirst = Max(0, atoi(d));
        if (const char* d = Platform::argValue("tourcount")) tourCount = Max(1, atoi(d));
        // Generate the world on a background thread while the loading screen animates
        loader = std::thread([this] {
            map.generate();
            World::gMap = &map;
            loadStage = 1;
            roads.generate(map);
            World::gRoads = &roads;
            loadStage = 2;
            buildings.generate(map, roads);
            World::gBuildings = &buildings;
            loadStage = 3;
            loadDone = true;
        });
        lastTime = TimeSeconds();
        return true;
    }

    // GPU-side setup once the CPU world exists (main thread)
    void finishLoading() {
        if (loader.joinable()) loader.join();
        Phys::gCollision = &collision;
        renderer.init(Platform::clientWidth(), Platform::clientHeight());
        renderer.setWorld(&map);
        viewer.init(renderer, map);
        cam.pos = dvec3(3000, -300, 60);
        cam.yaw = 0.f;
        cam.pitch = -0.1f;
        env.timeOfDay = 10.f;
        if (const char* t = Platform::argValue("time")) env.timeOfDay = (float)atof(t);
        if (const char* d = Platform::argValue("debugview")) renderer.debugView = atoi(d);
        if (const char* cc = Platform::argValue("clouds")) env.cloudCover = (float)atof(cc);
        if (const char* rr = Platform::argValue("rain")) {
            env.rain = (float)atof(rr);
            env.wetness = env.rain;
        }
        if (const char* ff = Platform::argValue("fog")) env.fogDensity = (float)atof(ff);
#ifdef HAVE_GAMEPLAY
        game.init(&renderer, &env, &map, &roads, &buildings);
        game.rig.footFirstPerson = Platform::hasArg("firstperson");   // start in the on-foot first-person view
        weather.seed = (u32)(TimeSeconds() * 1000.0) | 1u;
        if (Platform::argValue("rain") || Platform::argValue("clouds") || Platform::argValue("fog") || !shots.empty() || autotest) {
            weather.locked = true;
            WeatherKind k = WX_FAIR;
            if (const char* rr = Platform::argValue("rain")) k = atof(rr) > 0.7 ? WX_STORM : (atof(rr) > 0.15 ? WX_RAIN : k);
            if (const char* ff = Platform::argValue("fog")) k = atof(ff) > 0.3 ? WX_FOG : k;
            if (const char* cc = Platform::argValue("clouds")) k = atof(cc) > 0.75 ? WX_OVERCAST : (atof(cc) > 0.5 ? WX_CLOUDY : (atof(cc) < 0.2 ? WX_CLEAR : k));
            weather.setImmediate(k);
        }
#endif
#ifdef HAVE_GAME_UI
        UI::hudInit();
        loadSettings();
#endif
        state = AS_FREECAM;
        LOG("Init done in %.2f s (shaders: %d compiled in %.2f s)", TimeSeconds() - loadStart, gfx::shaderCompileCount(), gfx::shaderCompileSeconds());
        if (!shots.empty() || Platform::argValue("viewer")) {
            showDebug = !autotest || Platform::hasArg("debugtext");
        } else {
#ifdef HAVE_GAMEPLAY
            if (Platform::hasArg("play") || !autoplay.empty()) startNewGame();
            else openMainMenu();
#endif
        }
    }

    void openMainMenu() {
        state = AS_MENU;
        menuCamT = 0.f;
        env.timeOfDay = 18.4f;
#ifdef HAVE_GAME_UI
        menu.screen = UI::MENU_MAIN;
        menu.cursor = 0;
        menu.canContinue = newestSlot() >= 0;
#endif
#ifdef HAVE_AUDIO
        Audio::setPaused(true);
        Audio::setRadioStation(-1);
        Audio::setScore(7, 0.25f);
#endif
    }

    void startNewGame() {
#ifdef HAVE_GAMEPLAY
        game.newGame();
        if (const char* t = Platform::argValue("time")) env.timeOfDay = (float)atof(t);   // test override
#ifdef HAVE_GAME_UI
        UI::hudReset();
#endif
        state = AS_PLAYING;
        if (!weather.locked) weather.setImmediate(WX_FAIR);
#ifdef HAVE_GAME_UI
        menu.screen = UI::MENU_NONE;
#endif
#ifdef HAVE_AUDIO
        Audio::setPaused(false);
        Audio::setScore(0, 0.f);
#endif
        if (!autoplay.empty()) setupAutoplay();
#endif
    }

    void parseShots() {
        // --shot x,y,z,yawDeg,pitchDeg,hour,name  (repeatable)
        for (int i = 1; i < Platform::argCount(); i++) {
            const char* a = Platform::arg(i);
            if (strcmp(a, "--shot") == 0 && i + 1 < Platform::argCount()) {
                Shot s;
                double x, y, z;
                float yaw, pitch, t;
                char name[256] = {};
                if (sscanf(Platform::arg(i + 1), "%lf,%lf,%lf,%f,%f,%f,%255s", &x, &y, &z, &yaw, &pitch, &t, name) == 7) {
                    s.pos = dvec3(x, y, z);
                    s.yaw = yaw * kDegToRad;
                    s.pitch = pitch * kDegToRad;
                    s.time = t;
                    s.name = name;
                    shots.push_back(s);
                }
                i++;
            }
        }
        if (const char* f = Platform::argValue("settle")) shotSettleFrames = atoi(f);
    }

    std::string shotPath(const std::string& name) {
        std::string path = std::string("Z:\\tmp\\") + name + ".bmp";
        if (const char* dir = Platform::argValue("shotdir")) path = std::string(dir) + name + ".bmp";
        return path;
    }

    void updateFreeCamera(float dt) {
        InputState& in = Platform::input();
        bool look = in.down(KEY_MOUSE_RIGHT) || Platform::hasArg("mouselook");
        Platform::setMouseCaptured(look);
        if (look) {
            cam.yaw -= in.mouseDelta.x * 0.0025f;
            cam.pitch -= in.mouseDelta.y * 0.0025f;
        }
        cam.yaw -= in.pad.rightStick.x * 2.2f * dt;
        cam.pitch += in.pad.rightStick.y * 1.6f * dt;
        cam.pitch = Clamp(cam.pitch, -1.5f, 1.5f);
        if (in.wheelDelta != 0) camSpeed = Clamp(camSpeed * powf(1.25f, in.wheelDelta), 2.f, 3000.f);
        vec3 f = cam.forward(), r = cam.right();
        vec3 mv(0, 0, 0);
        if (in.down(KEY_W)) mv += f;
        if (in.down(KEY_S)) mv -= f;
        if (in.down(KEY_D)) mv += r;
        if (in.down(KEY_A)) mv -= r;
        if (in.down(KEY_E) || in.down(KEY_SPACE)) mv += vec3(0, 0, 1);
        if (in.down(KEY_Q) || in.down(KEY_CONTROL)) mv -= vec3(0, 0, 1);
        mv += f * in.pad.leftStick.y + r * in.pad.leftStick.x;
        mv += vec3(0, 0, in.pad.rightTrigger - in.pad.leftTrigger);
        float sp = camSpeed * (in.down(KEY_SHIFT) ? 5.f : 1.f);
        cam.pos = cam.pos + mv * (sp * dt);
        float ground = map.heightAt((float)cam.pos.x, (float)cam.pos.y);
        if (cam.pos.z < ground + 1.5) cam.pos.z = ground + 1.5;
        if (in.down(KEY_PGUP)) env.timeOfDay += dt * 2.f;
        if (in.down(KEY_PGDN)) env.timeOfDay -= dt * 2.f;
        if (env.timeOfDay >= 24.f) env.timeOfDay -= 24.f;
        if (env.timeOfDay < 0.f) env.timeOfDay += 24.f;
    }

    // Slow cinematic flight over the city behind the main menu
    void updateMenuCamera(float dt) {
        menuCamT += dt;
        float t = menuCamT * 0.018f;
        vec2 c(2400.f, 1500.f);
        float r = 1500.f;
        vec2 p = c + vec2(cosf(t), sinf(t)) * r;
        cam.pos = dvec3(p.x, p.y, 260.0 + 40.0 * sin(t * 2.3));
        vec2 look = c - p;
        cam.yaw = atan2f(-look.x, look.y) + 0.35f;
        cam.pitch = -0.2f;
        cam.fovY = 55.f * kDegToRad;
    }

    void drawLoadingScreen(float dt) {
        ID3D11RenderTargetView* rtv = gfx::backbufferRTV();
        float clearColor[4] = {0.01f, 0.012f, 0.03f, 1.f};
        gfx::ctx->OMSetRenderTargets(1, &rtv, nullptr);
        gfx::ctx->ClearRenderTargetView(rtv, clearColor);
        D3D11_VIEWPORT vp = {0, 0, (float)gfx::backbufferWidth(), (float)gfx::backbufferHeight(), 0, 1};
        gfx::ctx->RSSetViewports(1, &vp);
        UI::beginFrame(gfx::backbufferWidth(), gfx::backbufferHeight());
        float progress = Saturate((loadStage.load() + Saturate((float)(TimeSeconds() - loadStart) / 12.f)) / 4.f);
#ifdef HAVE_GAME_UI
        menu.screen = UI::MENU_LOADING;
        menu.loadingProgress = progress;
        static const char* tips[] = {
            "Police lose track of you faster once you leave the search area shown on the radar.",
            "Neon Shells are hidden all over Palmera. Find them all for a big reward.",
            "Hold the handbrake while turning to drift through tight corners.",
            "Radio stations keep playing even when you are not listening.",
            "Storms roll in quickly in the afternoon - wet roads reduce grip.",
            "Headshots deal extra damage. Aim for the head.",
        };
        menu.loadingTip = tips[((int)(TimeSeconds() / 6.0)) % 6];
        UI::Menus::update(menu, Platform::input(), dt);
#else
        (void)dt;
        UI::TextStyle st;
        st.font = UI::FONT_TITLE;
        st.size = 64.f;
        st.align = UI::ALIGN_CENTER;
        float W = (float)gfx::backbufferWidth(), H = (float)gfx::backbufferHeight();
        UI::text(W * 0.5f, H * 0.42f, "NEON TIDE", st);
        UI::rect(W * 0.3f, H * 0.6f, W * 0.4f, 6.f, UI::rgba(1, 1, 1, 0.15f));
        UI::rect(W * 0.3f, H * 0.6f, W * 0.4f * progress, 6.f, UI::rgba(1.f, 0.2f, 0.6f, 1.f));
#endif
        UI::endFrame();
        gfx::present(true);
    }

    void drawDebugText() {
        if (!showDebug) return;
        UI::TextStyle st;
        st.size = 18.f;
        st.shadow = 1.5f;
        World::Region reg = map.regionAt((float)cam.pos.x, (float)cam.pos.y);
        std::string s = StrFormat("NEON TIDE  |  %.0f fps (%.2f ms)\npos %.0f %.0f %.0f  |  %s\ntime %05.2f  draws %d  tris %dk  cells %d (pending %d)  lights %d",
                                  fps, frameMs, cam.pos.x, cam.pos.y, cam.pos.z, World::regionInfo(reg).name, env.timeOfDay,
                                  renderer.stats.drawCalls, renderer.stats.triangles / 1000, renderer.world->drawnCells, renderer.world->pendingCount(),
                                  renderer.stats.lights);
#ifdef HAVE_GAMEPLAY
        int np = 0, nv = 0;
        for (auto& p : game.peds) np += p.used;
        for (auto& v : game.vehicles) nv += v.used;
        s += StrFormat("\npeds %d  vehicles %d  wanted %d  |  cpu ms: player %.2f ai %.2f veh %.2f peds %.2f misc %.2f mis %.2f cam %.2f", np, nv,
                       game.pinfo.wanted, game.profPlayer, game.profAI, game.profVehicles, game.profPeds, game.profMisc, game.profMissions,
                       game.profCamera);
#endif
        UI::roundRect(10, 10, 980, 100, 8, UI::rgba(0, 0, 0, 0.45f));
        UI::text(20, 16, s.c_str(), st);
    }

    void probePixels() {
        float e[4] = {};
        gfx::readbackBuffer(renderer.post->exposureBuf.buf, e, 16);
        LOG("exposure %.4g ev %.2f avgLum %.4g", e[0], e[1], e[2]);
        int w = renderer.width, h = renderer.height;
        int pts[5][2] = {{w / 2, h / 2}, {w / 2, h * 3 / 4}, {w / 2, h / 8}, {w / 4, h * 7 / 8}, {w * 3 / 4, h / 3}};
        for (auto& p : pts) {
            float px[4];
            gfx::readbackPixelsFloat4(renderer.hdr.res, DXGI_FORMAT_R16G16B16A16_FLOAT, p[0], p[1], px);
            float d[4];
            gfx::readbackPixelsFloat4(renderer.depth.res, DXGI_FORMAT_R32_FLOAT, p[0], p[1], d);
            LOG("  hdr(%d,%d) = %.4g %.4g %.4g  depth %.4g", p[0], p[1], px[0], px[1], px[2], d[0]);
        }
    }

#ifdef HAVE_GAMEPLAY
    void setupAutoplay() {
        Ped* pl = game.playerPed();
        if (!pl) return;
        if (autoplay == "drive" || autoplay == "fly" || autoplay == "boat" || autoplay == "bike") {
            Vehicles::VehicleClass cls = autoplay == "fly" ? Vehicles::VC_HELI
                                       : (autoplay == "boat" ? Vehicles::VC_BOAT : (autoplay == "bike" ? Vehicles::VC_MOTORBIKE : Vehicles::VC_SPORTS));
            int model = game.findVehicleModel(cls, 0);
            dvec3 pos = pl->pos;
            float yaw = pl->yaw;
            if (autoplay == "boat") {
                pos = dvec3(2600, 400, 1.0);
                yaw = 0.8f;
            }
            if (model >= 0) {
                int vid = game.spawnVehicle(model, pos + dvec3(0, 0, 0.5), yaw, false);
                if (vid >= 0) {
                    game.warpPedIntoVehicle(game.player, vid, 0);
                    game.vehicles[vid].persistent = true;
                }
            }
        }
        if (const char* wm = Platform::argValue("weaponmods")) {
            // test hook: every component fitted and the given tint on all guns (--weaponmods TINT)
            for (int w = 0; w < WPN_COUNT; w++) {
                game.pinfo.wpnCompOwned[w] = game.pinfo.wpnCompFitted[w] = weaponCompsAvailable((WeaponType)w);
                game.pinfo.wpnTint[w] = (u8)Clamp(atoi(wm), 0, kWeaponTints - 1);
                game.pinfo.wpnTintOwned[w] = 0xff;
            }
        }
        if (autoplay == "shoot") {
            game.giveWeapon(game.player, WPN_RIFLE, 300);
            game.peds[game.player].weapon = WPN_RIFLE;
            game.peds[game.player].armor = 100.f;
            // three armed hostiles ahead of the player
            Ped& p = game.peds[game.player];
            vec2 f(-sinf(p.yaw), cosf(p.yaw)), r(cosf(p.yaw), sinf(p.yaw));
            for (int i = 0; i < 3; i++) {
                vec2 q = p.pos.toVec3().xy() + f * (14.f + i * 4.f) + r * ((i - 1) * 5.f);
                float gz = game.groundHeight(q.x, q.y, (float)p.pos.z + 3.f);
                int ci = game.randomCivilianChar(0x5100u + i, 2);
                int e = game.spawnPed(ci, dvec3(q.x, q.y, gz), p.yaw + kPi, FAC_ENEMY);
                if (e < 0) continue;
                game.giveWeapon(e, WPN_PISTOL, 90);
                game.peds[e].weapon = WPN_PISTOL;
                game.peds[e].brain.type = BRAIN_COMBAT;
                game.peds[e].brain.target = game.player;
                game.peds[e].brain.accuracy = 0.2f;
                game.peds[e].persistent = true;
            }
            game.rig.yaw = p.yaw;
        }
        if (autoplay == "melee") {
            // a fist-fighter squaring up ahead, and an unaware civilian off to the side for the takedown at t = 10 s
            Ped& p = game.peds[game.player];
            p.weapon = WPN_FISTS;
            vec2 f(-sinf(p.yaw), cosf(p.yaw)), r(cosf(p.yaw), sinf(p.yaw));
            vec2 q = p.pos.toVec3().xy() + f * 3.5f;
            int e = game.spawnPed(game.randomCivilianChar(0x6100u, 2), dvec3(q.x, q.y, game.groundHeight(q.x, q.y, (float)p.pos.z + 3.f)), p.yaw + kPi,
                                  FAC_ENEMY);
            if (e >= 0) {
                game.peds[e].weapon = WPN_FISTS;
                game.peds[e].brain.type = BRAIN_COMBAT;
                game.peds[e].brain.target = game.player;
                game.peds[e].brain.accuracy = 0.6f;
                game.peds[e].persistent = true;
            }
            vec2 cq = p.pos.toVec3().xy() + r * 7.f + f * 2.f;
            meleeVictim = game.spawnPed(game.randomCivilianChar(0x6200u, 0), dvec3(cq.x, cq.y, game.groundHeight(cq.x, cq.y, (float)p.pos.z + 3.f)),
                                        p.yaw, FAC_CIVILIAN);
            if (meleeVictim >= 0) game.peds[meleeVictim].persistent = true;
            game.rig.yaw = p.yaw;
        }
        if (autoplay == "tour") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);   // no prologue phone call: free roam only
            tourStop = -1;
            tourT = 0.f;
            tourShot = tourDone = false;
            autoDuration = 1e9f;   // ends after the last stop
            weather.locked = true;
        }
        if (autoplay == "traffic" || autoplay == "wanted") {
            // AI tests: a busy downtown corner (traffic, pedestrians, signals) / a police chase at 3 stars
            Ped& p = game.peds[game.player];
            vec2 q = autoplay == "traffic" ? vec2(2713.f, 763.f) : vec2(2640.f, 700.f);
            p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
            p.yaw = autoplay == "traffic" ? 2.4f : 0.f;
            game.rig.yaw = p.yaw;
            game.populationWarmup = 2.5f;
            if (autoplay == "wanted") {
                game.giveWeapon(game.player, WPN_PISTOL, 200);
                p.weapon = WPN_PISTOL;
                p.armor = 100.f;
                game.pinfo.wantedHeat = 5.5f;
                game.pinfo.wanted = 3;
                game.pinfo.lastSeenPos = p.pos;
                game.pinfo.lastSeenTime = (float)game.time;
            }
        }
        if (autoplay == "crowd" || autoplay == "panic" || autoplay == "chase" || autoplay == "rage" || autoplay == "soak") {
            // AI scenario tests: crowd variety at four places and hours / gunfire panic -> police response -> arrest /
            // night car chase at 4 stars (PIT, boxing, roadblocks, helicopter searchlight) / rear-ending a bold driver
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);
            weather.locked = true;
            weather.setImmediate(WX_CLEAR);
            Ped& p = game.peds[game.player];
            game.populationWarmup = 2.5f;
            if (autoplay == "crowd") {
                autoDuration = 4 * 7.f + 0.5f;   // four stops, 7 s each (applyAutoplay)
            } else if (autoplay == "panic") {
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                p.yaw = 2.4f;
                game.rig.yaw = p.yaw;
                env.timeOfDay = 13.f;
                game.giveWeapon(game.player, WPN_PISTOL, 60);
                p.weapon = WPN_FISTS;
                game.timeScale = 1.5f;   // more game time per (slow) test frame
                // a patrol car cruising a couple of blocks away (the nearest free unit answers the call)
                int pm = game.findVehicleModel(Vehicles::VC_POLICE, 5);
                for (int k = 0; k < 12 && pm >= 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 0.52f), sinf(k * 0.52f)) * 150.f;
                    float u = 0.f;
                    int lane = game.laneGraph.nearestLane(probe, normalize(q - probe), 40.f, &u);
                    if (lane < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    if (L.flags & AI::LF_DIRT) continue;
                    u = Clamp(u, L.u0 + 5.f, L.u1 - 12.f);
                    vec3 c3 = game.laneGraph.lanePos(lane, u);
                    int vid = game.spawnVehicle(pm, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, u)), true, FAC_POLICE);
                    if (vid < 0) continue;
                    game.vehicles[vid].faction = FAC_POLICE;
                    game.attachTraffic(vid, lane, u);
                    if (game.vehicles[vid].seats[0] >= 0) game.peds[game.vehicles[vid].seats[0]].brain.type = BRAIN_DRIVER;
                    game.vehAI(vid).role = VR_POLICE;
                    LOG("autoplay panic: patrol car %d at %.0f %.0f", vid, c3.x, c3.y);
                    break;
                }
            } else {
                // the player's car on a long straight lane: chase = sports car at night, rage = sedan with a bold
                // driver stopped 22 m ahead in the same lane
                bool chase = autoplay == "chase", soak = autoplay == "soak";
                vec2 q = chase || soak ? vec2(2640.f, 700.f) : vec2(2713.f, 763.f);
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 60 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f), sinf(k * 2.4f)) * (6.f + k * 5.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    if (L.flags & AI::LF_DIRT) continue;
                    uu = Max(uu, L.u0 + 6.f);
                    if (L.u1 - uu < 75.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 60.f)) < 0.995f) continue;
                    lane = li;
                    u = uu;
                }
                int model = game.findVehicleModel(chase ? Vehicles::VC_SPORTS : Vehicles::VC_SEDAN, 3);
                if (lane >= 0 && model >= 0) {
                    vec3 c3 = game.laneGraph.lanePos(lane, u);
                    float yaw = AI::dirYaw(game.laneGraph.laneTangent(lane, u));
                    int vid = game.spawnVehicle(model, dvec3(c3.x, c3.y, c3.z + 0.4f), yaw, false);
                    if (vid >= 0) {
                        game.warpPedIntoVehicle(game.player, vid, 0);
                        game.vehicles[vid].persistent = true;
                        game.rig.yaw = yaw;
                        if (chase || soak) game.attachTraffic(vid, lane, u);
                    }
                    if (!chase && !soak) {
                        int nm = game.findVehicleModel(Vehicles::VC_COMPACT, 11);
                        vec3 n3 = game.laneGraph.lanePos(lane, u + 22.f);
                        int npc = nm >= 0 ? game.spawnVehicle(nm, dvec3(n3.x, n3.y, n3.z + 0.4f), yaw, true, FAC_CIVILIAN) : -1;
                        if (npc >= 0 && game.vehicles[npc].seats[0] >= 0) {
                            int drv = game.vehicles[npc].seats[0];
                            game.peds[drv].brain.type = BRAIN_DRIVER;
                            game.attachTraffic(npc, lane, u + 22.f);
                            game.pedAI(drv).temper = 2;   // bold: gets out and has words
                            if (AI::Driver* d = game.traffic.get(npc)) {
                                d->mode = AI::DM_HOLD;
                                d->holdTimer = 12.f;
                            }
                            LOG("autoplay rage: player car %d, bold driver %d in car %d 22 m ahead on lane %d", vid, drv, npc, lane);
                        }
                    }
                }
                env.timeOfDay = chase ? 22.f : (soak ? 7.5f : 11.f);   // soak: morning rush hour, then the clock runs
                if (chase) {
                    game.timeScale = 1.5f;
                    p.armor = 100.f;
                    p.maxHealth = p.health = 400.f;   // survive the whole chase
                    game.pinfo.wantedHeat = 9.5f;
                    game.pinfo.wanted = 4;
                    game.pinfo.lastSeenPos = p.pos;
                    game.pinfo.lastSeenTime = (float)game.time;
                }
            }
        }
        autoTime = 0.f;
        autoShot = 0;
    }

    void applyAutoplay(Controls& c, float dt) {
        autoTime += dt;
        float t = autoTime;
        c = Controls();
        if (autoplay == "walk") {
            c.move = vec2(0.f, 1.f);
            c.sprint.down = t > 6.f && t < 12.f;
            c.look = vec2(sinf(t * 0.5f) * 0.01f, 0.f);
            c.jump.pressed = fmodf(t, 7.f) < dt;
        } else if (autoplay == "drive" || autoplay == "bike") {
            // road-following: the traffic driver AI steers the player's vehicle (exercises both systems)
            int pv = game.playerVehicle();
            if (pv >= 0) {
                game.driveVehicleAI(pv, dt);
                const Vehicles::VehicleControls& vc = game.vehicles[pv].ctl;
                c.accel = vc.throttle;
                c.brake = vc.brake;
                c.steer = vc.steer;
                c.usingPad = true;
            }
            c.handbrake.down = t > 12.f && t < 12.6f;
        } else if (autoplay == "fly") {
            c.lift = t < 8.f ? 1.f : 0.1f;
            c.pitch = t > 8.f ? -0.35f : 0.f;
            c.yaw = t > 14.f ? 0.4f : 0.f;
        } else if (autoplay == "boat") {
            c.accel = 1.f;
            c.steer = t > 8.f ? 0.5f : 0.f;
        } else if (autoplay == "traffic" || autoplay == "wanted") {
            // traffic: stand at the corner and look around; wanted: run from the police with the gun drawn
            if (autoplay == "traffic") c.look = vec2(0.004f, 0.f);
            else {
                c.move = vec2(sinf(t * 0.23f) * 0.5f, 1.f);
                c.sprint.down = fmodf(t, 10.f) < 6.f;
                c.look = vec2(sinf(t * 0.3f) * 0.006f, 0.f);
            }
            if ((int)(t / 5.f) != (int)((t - dt) / 5.f)) LOG("autoplay %s t=%.1f %s", autoplay.c_str(), t, game.aiDebugText().c_str());
        } else if (autoplay == "crowd") {
            // four stops (beach afternoon, downtown lunch, club at night, park evening): look around, one named shot
            // and a census of what the crowd is doing at each
            static int stop = -1;
            static float stopT = 0.f;
            static bool shot = false;
            mu::computePlaces(game);
            const mu::Places& P = mu::gPlaces;
            struct CrowdStop {
                const char* name;
                const mu::Place* pl;
                float hour;
            };
            const CrowdStop stops[4] = {{"beach_afternoon", &P.beachCondo, 15.f}, {"downtown_lunch", &P.policeHq, 12.8f},
                                        {"club_night", &P.clubRiptide, 23.f}, {"park_evening", &P.midtownPark, 17.5f}};
            int want = Min((int)(t / 7.f), 3);
            Ped* pl = game.playerPed();
            if (want != stop && pl) {
                stop = want;
                stopT = 0.f;
                shot = false;
                const CrowdStop& st = stops[stop];
                if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
                vec3 pos = st.pl->pos;
                pl->pos = dvec3(pos.x, pos.y, game.groundHeight(pos.x, pos.y, pos.z + 2.f));
                pl->vel = vec3(0.f);
                pl->yaw = atan2f(-st.pl->streetDir.x, st.pl->streetDir.y);
                game.rig.yaw = pl->yaw + 0.25f;   // looking down the sidewalk, not into the facades
                game.rig.pitch = -0.1f;
                game.rig.cut = true;
                env.timeOfDay = st.hour;
                game.populationWarmup = 2.5f;
                LOG("autoplay crowd stop %d %s at %.0f %.0f, %.1f h", stop, st.name, pos.x, pos.y, st.hour);
            }
            stopT += dt;
            if (!shot && stop >= 0 && stopT > 6.2f) {
                shot = true;
                game.requestScreenshot = shotPath(StrFormat("auto_crowd_%02d_%s", stop, stops[stop].name));
                LOG("autoplay crowd %s | %s | %s", stops[stop].name, game.aiCensusText(70.f).c_str(), game.aiDebugText().c_str());
            }
        } else if (autoplay == "panic") {
            // look around, fire three shots into the air, holster and stand still: the crowd scatters (panic spreads,
            // the bold film, witnesses phone it in), officers respond and arrest the calm, unarmed player
            Ped* pl = game.playerPed();
            if (pl && pl->state == PS_ONFOOT && !game.pinfo.busted) {
                if (t > 3.f && t < 5.f) {
                    pl->weapon = WPN_PISTOL;
                    c.aim.down = true;
                    c.aim.pressed = t - dt <= 3.f;
                    game.rig.pitch = 0.5f;
                    c.attack.pressed = (t > 3.5f && t - dt <= 3.5f) || (t > 4.f && t - dt <= 4.f) || (t > 4.5f && t - dt <= 4.5f);
                } else {
                    if (t >= 5.f && pl->weapon != WPN_FISTS) pl->weapon = WPN_FISTS;
                    if (t >= 5.f && t - dt < 5.f) game.rig.pitch = -0.05f;
                    c.look = vec2(t < 3.f ? 0.004f : 0.006f, 0.f);
                }
            }
            if ((int)(t / 2.f) != (int)((t - dt) / 2.f))
                LOG("autoplay panic t=%.1f wanted %d heat %.2f busted %d | %s | %s", t, game.pinfo.wanted, game.pinfo.wantedHeat, (int)game.pinfo.busted,
                    game.aiCensusText(60.f).c_str(), game.aiDebugText().c_str());
        } else if (autoplay == "chase") {
            // flee through the night at 4 stars: the traffic driver steers the player's car in flee mode (runs the
            // lights), the police pursue (PIT, boxing, roadblocks ahead, helicopter with searchlight)
            int pv = game.playerVehicle();
            if (pv >= 0) {
                if (AI::Driver* d = game.traffic.get(pv)) {
                    d->mode = AI::DM_FLEE;
                    vec2 vp = game.vehicles[pv].sim.body.pos.toVec3().xy();
                    vec2 threat = vp - normalize(game.vehicles[pv].sim.forward().xy() + vec2(1e-4f, 0.f)) * 60.f;
                    float best = 1e9f;
                    for (int i = 0; i < (int)game.vehicles.size(); i++) {
                        const Vehicle& o = game.vehicles[i];
                        if (!o.used || i == pv || o.faction != FAC_POLICE) continue;
                        float dd = length(o.sim.body.pos.toVec3().xy() - vp);
                        if (dd < best) {
                            best = dd;
                            threat = o.sim.body.pos.toVec3().xy();
                        }
                    }
                    d->threat = threat;
                }
                game.driveVehicleAI(pv, dt);
                const Vehicles::VehicleControls& vc = game.vehicles[pv].ctl;
                c.accel = vc.throttle;
                c.brake = vc.brake;
                c.steer = vc.steer;
                c.usingPad = true;
            }
            if ((int)(t / 4.f) != (int)((t - dt) / 4.f))
                LOG("autoplay chase t=%.1f wanted %d speed %.1f | %s | %s", t, game.pinfo.wanted, pv >= 0 ? game.vehicles[pv].sim.speed() : 0.f,
                    game.aiCensusText(120.f).c_str(), game.aiDebugText().c_str());
        } else if (autoplay == "rage") {
            // roll into the stopped car ahead at ~8 m/s, then stay put: its bold driver gets out, storms up to the
            // window, yells and pounds on the glass
            static bool hit = false;
            static float lastSpeed = 0.f;
            int pv = game.playerVehicle();
            if (pv >= 0) {
                float spd = game.vehicles[pv].sim.speed();
                if (!hit && t > 1.5f && (spd < lastSpeed - 1.2f || t > 9.f)) {
                    hit = true;
                    LOG("autoplay rage: impact at t=%.1f (%.1f -> %.1f m/s)", t, lastSpeed, spd);
                }
                lastSpeed = spd;
                float fwdSpeed = game.vehicles[pv].sim.forwardSpeed();
                c.accel = t > 1.f && !hit && spd < 8.f ? 0.45f : 0.f;   // roll in at ~25-30 km/h
                c.brake = hit && fwdSpeed > 0.3f ? 1.f : 0.f;          // (holding brake at a standstill would reverse)
                c.handbrake.down = hit && fwdSpeed <= 0.3f;
                c.usingPad = true;
                c.look = vec2(hit ? 0.003f : 0.f, 0.f);
            }
            if ((int)(t / 2.5f) != (int)((t - dt) / 2.5f)) {
                // the car ahead: gap, speed, drive mode and road-rage state
                std::string ahead = "none";
                if (pv >= 0) {
                    const Vehicle& me = game.vehicles[pv];
                    vec2 mp = me.sim.body.pos.toVec3().xy(), mf = normalize(me.sim.forward().xy() + vec2(1e-4f, 0.f));
                    float best = 90.f;
                    for (int i = 0; i < (int)game.vehicles.size(); i++) {
                        const Vehicle& o = game.vehicles[i];
                        if (!o.used || i == pv) continue;
                        vec2 rp = o.sim.body.pos.toVec3().xy() - mp;
                        float along = dot(rp, mf);
                        if (along <= 0.f || along > best || fabsf(dot(rp, AI::rightOf(mf))) > 2.5f) continue;
                        best = along;
                        const AI::Driver* d = game.traffic.get(i);
                        ahead = StrFormat("car %d gap %.1f speed %.1f mode %d rage %d driver %d", i, along, o.sim.speed(), d ? (int)d->mode : -1,
                                          i < (int)game.ai.veh.size() ? (int)game.ai.veh[i].rage : -1, o.seats[0]);
                    }
                }
                LOG("autoplay rage t=%.1f ahead: %s | %s", t, ahead.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "soak") {
            // long drive on the traffic AI through the city while the clock runs (rush hour -> night), with a 3-star
            // chase at 8-10 min and a 4-star chase at 18-20 min; telemetry every 20 s, the own car unstuck if needed
            static float stuckT = 0.f;
            static int unsticks = 0, respawns = 0;
            Ped* pl = game.playerPed();
            if (pl) {
                pl->health = Max(pl->health, 250.f);   // survive the chases: the soak is about the city, not the player
                if (pl->health > 0.f && pl->state == PS_ONFOOT && game.playerVehicle() < 0 && !game.pinfo.busted) {
                    // lost the car (wrecked, dragged out): a fresh one on the nearest lane
                    float u = 0.f;
                    int lane = game.laneGraph.nearestLane(pl->pos.toVec3().xy(), vec2(0.f), 60.f, &u);
                    int model = game.findVehicleModel(Vehicles::VC_SEDAN, 3 + respawns);
                    if (lane >= 0 && model >= 0) {
                        const AI::Lane& L = game.laneGraph.lanes[lane];
                        u = Clamp(u, L.u0 + 4.f, L.u1 - 8.f);
                        vec3 c3 = game.laneGraph.lanePos(lane, u);
                        int vid = game.spawnVehicle(model, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, u)), false);
                        if (vid >= 0) {
                            game.warpPedIntoVehicle(game.player, vid, 0);
                            game.vehicles[vid].persistent = true;
                            game.attachTraffic(vid, lane, u);
                            respawns++;
                            LOG("autoplay soak t=%.0f: new car %d at %.0f %.0f (respawn %d)", t, vid, c3.x, c3.y, respawns);
                        }
                    }
                }
            }
            auto crossed = [&](float at) { return t >= at && t - dt < at; };
            if (crossed(480.f) || crossed(1080.f)) {
                bool four = t > 1000.f;
                game.pinfo.wanted = four ? 4 : 3;
                game.pinfo.wantedHeat = four ? 9.5f : 5.5f;
                if (pl) game.pinfo.lastSeenPos = pl->pos;
                game.pinfo.lastSeenTime = (float)game.time;
                LOG("autoplay soak t=%.0f: chase starts at %d stars", t, game.pinfo.wanted);
            }
            if (crossed(600.f) || crossed(1200.f)) {
                game.pinfo.wanted = 0;
                game.pinfo.wantedHeat = 0.f;
                LOG("autoplay soak t=%.0f: chase called off", t);
            }
            bool chasing = game.pinfo.wanted > 0 && ((t > 480.f && t < 600.f) || (t > 1080.f && t < 1200.f));
            int pv = game.playerVehicle();
            if (pv >= 0) {
                if (AI::Driver* d = game.traffic.get(pv)) {
                    if (chasing) {
                        d->mode = AI::DM_FLEE;
                        vec2 vp = game.vehicles[pv].sim.body.pos.toVec3().xy();
                        vec2 threat = vp - normalize(game.vehicles[pv].sim.forward().xy() + vec2(1e-4f, 0.f)) * 60.f;
                        float best = 1e9f;
                        for (int i = 0; i < (int)game.vehicles.size(); i++) {
                            const Vehicle& o = game.vehicles[i];
                            if (!o.used || i == pv || o.faction != FAC_POLICE) continue;
                            float dd = length(o.sim.body.pos.toVec3().xy() - vp);
                            if (dd < best) {
                                best = dd;
                                threat = o.sim.body.pos.toVec3().xy();
                            }
                        }
                        d->threat = threat;
                    } else if (d->mode == AI::DM_FLEE) {
                        d->mode = AI::DM_NORMAL;
                    }
                }
                game.driveVehicleAI(pv, dt);
                const Vehicles::VehicleControls& vc = game.vehicles[pv].ctl;
                c.accel = vc.throttle;
                c.brake = vc.brake;
                c.steer = vc.steer;
                c.usingPad = true;
                // our own car stuck for long (not at a light): log it and move on along the roads
                Vehicle& v = game.vehicles[pv];
                AI::Driver* d = game.traffic.get(pv);
                bool atLight = d && d->waitTime > 0.f && d->waitTime < 80.f;
                stuckT = v.sim.speed() < 0.5f && !atLight ? stuckT + dt : 0.f;
                if (stuckT > 45.f) {
                    stuckT = 0.f;
                    unsticks++;
                    vec3 vp = v.sim.body.pos.toVec3();
                    LOG("autoplay soak t=%.0f: PLAYER CAR STUCK at %.1f %.1f %s %d u %.1f mode %d | %s", t, vp.x, vp.y,
                        d && d->path < (int)game.laneGraph.lanes.size() ? "lane" : "conn", d ? d->path : -1, d ? d->u : 0.f, d ? (int)d->mode : -1,
                        game.aiTrafficHealthText().c_str());
                    float u = 0.f;
                    vec2 f = normalize(v.sim.forward().xy() + vec2(1e-4f, 0.f));
                    int lane = game.laneGraph.nearestLane(vp.xy() + f * 120.f, f, 60.f, &u);
                    if (lane >= 0) {
                        const AI::Lane& L = game.laneGraph.lanes[lane];
                        u = Clamp(u, L.u0 + 4.f, L.u1 - 8.f);
                        vec3 c3 = game.laneGraph.lanePos(lane, u);
                        Vehicles::resetVehicle(v.sim, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, u)));
                        game.traffic.detach(pv);
                        game.attachTraffic(pv, lane, u);
                    }
                }
            }
            if ((int)(t / 20.f) != (int)((t - dt) / 20.f)) {
                vec3 pp = pl ? pl->pos.toVec3() : vec3(0.f);
                LOG("autoplay soak t=%.0f tod %.2f pos %.0f %.0f speed %.1f wanted %d unsticks %d respawns %d | %s | %s | %s", t, env.timeOfDay, pp.x, pp.y,
                    pv >= 0 ? game.vehicles[pv].sim.speed() : 0.f, game.pinfo.wanted, unsticks, respawns, game.aiTrafficHealthText().c_str(),
                    game.aiCensusText(100.f).c_str(), game.aiDebugText().c_str());
            }
        } else if (autoplay == "tour") {
            updateTour(c, dt);
        } else if (autoplay == "melee") {
            // lock on, jab-cross-uppercut combos, a heavy hook, a held block, a dodge; then a rear takedown
            if (t < 9.5f) {
                c.aim.down = true;
                c.aim.pressed = t < dt * 1.5f;
                c.attack.pressed = (t > 1.f && t < 3.2f && fmodf(t, 0.35f) < dt) || (t > 7.8f && t < 9.f && fmodf(t, 0.4f) < dt);
                c.reload.pressed = t > 3.6f && t - dt <= 3.6f;          // heavy
                c.cover.down = t > 5.f && t < 6.5f;                     // block
                c.jump.pressed = t > 6.9f && t - dt <= 6.9f;            // dodge
                c.move = t > 6.9f && t < 7.1f ? vec2(1.f, 0.f) : vec2(0.f, 0.f);
            } else if (meleeVictim >= 0 && game.peds[meleeVictim].used && game.peds[meleeVictim].health > 0.f) {
                Ped& p = game.peds[game.player];
                Ped& v = game.peds[meleeVictim];
                if (t - dt < 9.5f) {   // sneak up: right behind the victim, crouched
                    vec2 vf(-sinf(v.yaw), cosf(v.yaw));
                    vec3 vp = v.pos.toVec3();
                    p.pos = dvec3(vp.x - vf.x * 1.1f, vp.y - vf.y * 1.1f, vp.z);
                    p.yaw = v.yaw;
                    p.animIn.crouch = true;
                    game.rig.yaw = v.yaw + 0.9f;
                    game.rig.cut = true;
                }
                c.attack.pressed = t > 10.f && t - dt <= 10.f;
            }
        }
        if (autoplay == "melee" && (int)(t / 1.f) != (int)((t - dt) / 1.f)) {
            const Ped& p = game.peds[game.player];
            LOG("autoplay melee t=%.1f move %d combo %d block %d dodge %.2f stagger %.2f takedown %.2f target %d health %.0f", t, p.meleeMove, p.meleeCombo,
                (int)p.blocking, p.dodgeT, p.meleeStagger, p.takedownT, p.meleeTarget, p.health);
        } else if (autoplay == "shoot") {
            c.usingPad = true;                        // controller soft lock-on
            c.aim.down = fmodf(t, 3.f) > 0.15f;       // re-press to re-acquire targets
            c.aim.pressed = fmodf(t, 3.f) <= 0.15f + dt && fmodf(t, 3.f) > 0.15f;
            c.attack.down = c.aim.down && fmodf(t, 1.2f) < 0.6f;
            c.move = vec2(t > 8.f ? 0.4f : 0.f, 0.f);  // strafe later
            if (fmodf(t, 3.f) < 0.5f) c.look = vec2(0.03f, 0.f);  // sweep between re-acquisitions
        }
    }
#endif

#ifdef HAVE_GAMEPLAY
    // District tour: teleport to story places across the map at chosen hours/weather, let streaming and the population
    // settle, walk a few steps and take one screenshot per stop (auto_tour_NN_name.bmp via requestScreenshot).
    void updateTour(Controls& c, float dt) {
        mu::computePlaces(game);
        const mu::Places& P = mu::gPlaces;
        struct Stop {
            const char* name;
            const mu::Place* pl;
            float hour;
            WeatherKind wx;
            float yawOff;
        };
        const Stop stops[] = {
            {"calle_luna_morning", &P.mariApt, 9.5f, WX_FAIR, 0.7f},       {"diner_noon", &P.diner, 12.5f, WX_CLEAR, -0.6f},
            {"downtown_afternoon", &P.policeHq, 14.f, WX_FAIR, 0.5f},      {"solaris_plaza", &P.solarisOne, 15.5f, WX_CLOUDY, 0.9f},
            {"midtown_park", &P.midtownPark, 16.5f, WX_FAIR, -0.8f},       {"beach_condos", &P.beachCondo, 17.5f, WX_CLEAR, 0.6f},
            {"beach_pier_sunset", &P.beachPier, 19.1f, WX_FAIR, -0.5f},    {"club_night", &P.clubRiptide, 22.5f, WX_CLEAR, 0.7f},
            {"downtown_rain_night", &P.policeHq, 21.5f, WX_RAIN, -0.7f},   {"port_gate", &P.portGate, 8.5f, WX_OVERCAST, 0.6f},
            {"airport", &P.airport, 11.f, WX_FAIR, -0.6f},                 {"key_coral_marina", &P.keyCoralMarina, 15.f, WX_CLEAR, 0.8f},
            {"sawgrass_dawn", &P.sawgrassRoad, 7.2f, WX_FOG, 0.5f},        {"grove_suburb", &P.grove, 17.f, WX_FAIR, -0.7f},
            {"lake_town", &P.lakeTown, 10.5f, WX_FAIR, 0.6f},              {"fort_castell", &P.fortCastell, 13.f, WX_CLOUDY, -0.5f},
        };
        const int n = (int)(sizeof(stops) / sizeof(stops[0]));
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl) return;
        if (tourStop < 0 || (tourShot && tourT > 8.5f)) {
            tourStop = tourStop < 0 ? Min(tourFirst, n) : tourStop + 1;
            tourT = 0.f;
            tourShot = false;
            if (tourStop >= n || tourStop >= tourFirst + tourCount) {
                tourDone = true;
                LOG("autoplay tour done: %d stops", Min(n, tourFirst + tourCount) - Min(tourFirst, n));
                return;
            }
            const Stop& st = stops[tourStop];
            if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
            vec3 pos = st.pl->pos;
            pl->pos = dvec3(pos.x, pos.y, game.groundHeight(pos.x, pos.y, pos.z + 2.f));
            pl->vel = vec3(0.f);
            pl->yaw = atan2f(-st.pl->streetDir.x, st.pl->streetDir.y);
            // camera on the street side of the player looking back at the frontages (not jammed into awnings/signs)
            vec2 sd = st.pl->streetDir, left(-sd.y, sd.x);
            float side = dot(left, st.pl->outward) >= 0.f ? 1.f : -1.f;
            game.rig.yaw = pl->yaw + fabsf(st.yawOff) * side;
            game.rig.pitch = -0.1f;
            game.rig.cut = true;
            env.timeOfDay = st.hour;
            weather.setImmediate(st.wx);
            game.populationWarmup = 2.5f;
            game.pinfo.wanted = 0;
            LOG("autoplay tour stop %d %s at %.0f %.0f, %.1f h", tourStop, st.name, pos.x, pos.y, st.hour);
        }
        tourT += dt;
        c.move = vec2(0.f, tourT > 3.f && tourT < 6.f ? 0.3f : 0.f);   // a few slow steps for natural poses
        if (!tourShot && tourT > 7.f && (renderer.world->pendingCount() == 0 || tourT > 14.f)) {
            game.requestScreenshot = shotPath(StrFormat("auto_tour_%02d_%s", tourStop, stops[tourStop].name));
            tourShot = true;
            int np = 0, nv = 0;
            for (auto& q : game.peds) np += q.used;
            for (auto& q : game.vehicles) nv += q.used;
            LOG("autoplay tour shot %d %s | peds %d vehicles %d | cpu ms ai %.2f veh %.2f peds %.2f", tourStop, stops[tourStop].name, np, nv,
                game.profAI, game.profVehicles, game.profPeds);
        }
    }
#endif

    void run() {
        while (true) {
            Platform::beginFrameInput();
            if (!Platform::pumpMessages()) break;
            if (Platform::wasResized() && state != AS_LOADING) {
                gfx::resize(Platform::clientWidth(), Platform::clientHeight());
                renderer.resize(Platform::clientWidth(), Platform::clientHeight());
            }
            double now = TimeSeconds();
            float dt = (float)Min(now - lastTime, 0.1);
            lastTime = now;
            frameMs = Lerp(frameMs, dt * 1000.f, 0.05f);
            fps = frameMs > 0 ? 1000.f / frameMs : 0;
            if (state == AS_LOADING) {
                if (loadDone.load()) finishLoading();
                else {
                    drawLoadingScreen(dt);
                    continue;
                }
            }
            InputState& in = Platform::input();
            if (in.pressed(KEY_F1)) showDebug = !showDebug;
            bool quit = false;
            bool autoShots = !shots.empty();
            float simDt = autotest && !autoShots ? 1.f / 30.f : dt;  // fixed steps for automated runs
            renderer.dynamic->beginFrame();
            if (state == AS_FREECAM) {
                if (autoShots) {
                    const Shot& s = shots[shotIndex];
                    cam.pos = s.pos;
                    cam.yaw = s.yaw;
                    cam.pitch = s.pitch;
                    env.timeOfDay = s.time;
                    dt = 1.f / 30.f;
                    if (shotFrame == 0) renderer.cameraCut = true;
                } else {
                    updateFreeCamera(dt);
                }
                env.gameSeconds += dt;
                viewer.update(renderer, map, dt);
#ifdef HAVE_GAMEPLAY
                if (in.pressed(KEY_F9) && game.player >= 0) state = AS_PLAYING;
                if (game.player >= 0) game.submitRender();
                else Interiors::submitFreecam(renderer, cam, env);   // enterable interiors without a game (--shot)
#endif
                renderer.render(cam, env, dt);
            }
#ifdef HAVE_GAMEPLAY
            else if (state == AS_MENU) {
                updateMenuCamera(dt);
                env.gameSeconds += dt;
                weather.update(env, dt, cam.pos);
                renderer.render(cam, env, dt);
            } else if (state == AS_PLAYING) {
                bool menuOpen = false;
#ifdef HAVE_GAME_UI
                menuOpen = menu.screen != UI::MENU_NONE;
#endif
                Platform::setMouseCaptured(!menuOpen && Platform::hasFocus() && !autotest);
                InputConfig icfg;
#ifdef HAVE_GAME_UI
                icfg.mouseSensitivity = menu.settings.mouseSensitivity;
                icfg.padSensitivity = menu.settings.padSensitivity;
                icfg.invertY = menu.settings.invertY;
#endif
                int pv = game.playerVehicle();
                readControls(in, icfg, pv >= 0, pv >= 0 && game.isAircraft(pv), dt, game.ctl);
                if (!autoplay.empty()) applyAutoplay(game.ctl, simDt);
                bool photoFreeze = false;
#ifdef HAVE_GAME_UI
                bool photo = game.phone.photo.active;
                if (photo) game.ctl = Controls();   // the phone flies the photo camera and takes all input (Esc exits it)
                else if (game.phone.capturingInput) maskPhoneInput(game.ctl);
                photoFreeze = photo && game.phone.photo.freeze;
                game.hidePlayerModel = photo && game.phone.photo.hidePlayer;
#endif
                bool pausePressed = game.ctl.pause.pressed, mapPressed = game.ctl.map.pressed;
                if (menuOpen) {
                    game.ctl = Controls();
                    Platform::setGamepadRumble(0.f, 0.f);
                }
                game.paused = menuOpen || photoFreeze;
                if (!menuOpen && !photoFreeze) {
                    env.timeOfDay += simDt * game.timeScale / 120.f;   // 1 game minute = 2 real seconds
                    if (env.timeOfDay >= 24.f) {
                        env.timeOfDay -= 24.f;
                        game.gameDay++;
                    }
                    env.gameSeconds += simDt * game.timeScale;
                    WeatherKind wxBefore = weather.cur;
                    weather.update(env, simDt, game.rig.cam.pos);
                    if (weather.cur != wxBefore && (weather.cur == WX_RAIN || weather.cur == WX_STORM || weather.cur == WX_FOG))
                        game.socialReport(UI::TE_WEATHER, game.rig.cam.pos, weather.cur == WX_RAIN ? "rain" : (weather.cur == WX_STORM ? "storm" : "fog"));
                    game.update(simDt);
                    if (game.pinfo.deathTimer > 3.8f) game.fadeOut(1.2f);
                    if (game.pinfo.deathTimer > 5.f && game.fadedOut()) {
                        GameWorld_respawnPlayer(game);
                        game.fadeIn(0.8f);
#ifdef HAVE_GAME_UI
                        UI::hudReset();
#endif
                    }
                }
                if (in.pressed(KEY_F9)) {
                    state = AS_FREECAM;
                    cam = game.rig.cam;
                }
#ifdef HAVE_GAME_UI
                if (game.phone.photo.active) {
                    // photo mode renders (and culls) from the phone's free camera; auto focus looks at the center
                    const UI::PhotoMode& ph = game.phone.photo;
                    game.rig.cam.pos = dvec3(ph.camPos);
                    game.rig.cam.yaw = ph.camYaw;
                    game.rig.cam.pitch = ph.camPitch;
                    game.rig.cam.roll = ph.camRoll;
                    game.rig.cam.fovY = ph.camFov;
                    WorldHit fh;
                    game.phone.photo.autoFocusDistance =
                        game.raycast(game.rig.cam.pos, game.rig.cam.forward(), 300.f, fh, game.hidePlayerModel ? game.player : -1, -1) ? fh.t : 0.f;
                }
#endif
                // --renderevery N: skip rendering most frames of automated runs (software rendering dominates), but always
                // render the frames around a screenshot and keep world streaming going
                bool shotSoon = !game.requestScreenshot.empty() ||
                                (!autoplay.empty() && (autoplay == "tour" ? tourT > 6.4f : autoTime >= autoShot * autoShotEvery + 1.2f));
                bool doRender = renderEvery <= 1 || (playFrames++ % (u32)renderEvery) == 0 || shotSoon;
                if (doRender) game.submitRender();
                game.updateAudioListener(dt);
                Render::Camera rc = game.rig.cam;
#ifdef HAVE_GAME_UI
                if (!game.phone.photo.active) rc.fovY = Clamp(rc.fovY * menu.settings.fov / 60.f, 25.f * kDegToRad, 110.f * kDegToRad);
#endif
                cam = rc;
                game.renderCam = rc;
                skippedDt += dt;
                if (doRender) {
                    // frames skipped by --renderevery still count for time-based adaptation (exposure, particles)
                    renderer.render(rc, env, Min(skippedDt, 0.75f));
                    skippedDt = 0.f;
                } else renderer.world->update(rc.pos, TimeSeconds());
#ifdef HAVE_GAME_UI
                if (!menuOpen && game.requestSaveMenu) {   // safehouse bed / save point (never during automated runs)
                    game.requestSaveMenu = false;
                    if (autoplay.empty()) openPause(UI::MENU_SAVE);
                } else if (!menuOpen && pausePressed) openPause(UI::MENU_PAUSE);
                else if (!menuOpen && mapPressed) openPause(UI::MENU_MAP);
#else
                (void)pausePressed;
                (void)mapPressed;
#endif
            }
#endif
            // ---- 2D: HUD, menus, debug
            UI::beginFrame(gfx::backbufferWidth(), gfx::backbufferHeight());
            viewer.drawOverlay(renderer, dt);
#if defined(HAVE_GAMEPLAY) && defined(HAVE_GAME_UI)
            if (state == AS_PLAYING) {
                game.fillHud(hud, dt);
                bool showHud = menu.screen == UI::MENU_NONE && menu.settings.showHud && game.hudVisible && !game.phone.photo.active;
                if (showHud) UI::drawHud(hud, dt);
                if (menu.screen == UI::MENU_NONE) drawMissionOverlay(game, dt);   // shop and choice menus
                if (menu.screen == UI::MENU_NONE) updatePhone(in, dt);           // after the HUD, before Menus::update
                drawCinematicOverlay();
            }
            if (state == AS_MENU || (state == AS_PLAYING && menu.screen != UI::MENU_NONE)) {
                prepareMenuData();
                UI::MenuAction act = UI::Menus::update(menu, in, dt);
                quit = handleMenuAction(act);
            }
#endif
            drawDebugText();
#ifdef HAVE_GAME_UI
            UI::setSceneDepth(renderer.depth.srv, cam.nearZ);   // photo mode depth of field
#endif
            UI::endFrame();
#ifdef HAVE_GAMEPLAY
            if (state == AS_PLAYING && !game.requestScreenshot.empty()) {   // mission tests: shots include the HUD
                gfx::saveScreenshotBMP(game.requestScreenshot.c_str());
                game.requestScreenshot.clear();
            }
            if (pendingPhoto) {   // the finished photo (the UI drew it without its panels this frame)
                pendingPhoto = false;
                std::string dir = Platform::userDataDir() + "Photos\\";
                CreateDirectoryA(dir.c_str(), nullptr);
                SYSTEMTIME st;
                GetLocalTime(&st);
                std::string path = dir + StrFormat("NeonTide_%04d%02d%02d_%02d%02d%02d.bmp", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                gfx::saveScreenshotBMP(path.c_str());
                LOG("Photo saved: %s", path.c_str());
            }
#endif
            gfx::gpuTimersResolve();
            // ---- automation
            if (autoShots && state == AS_FREECAM) {
                if (renderer.world->pendingCount() > 0 && shotWait < 600) shotWait++;
                else shotFrame++;
                if (shotFrame >= shotSettleFrames) {
                    shotWait = 0;
                    std::string path = shotPath(shots[shotIndex].name);
                    gfx::saveScreenshotBMP(path.c_str());
                    if (Platform::hasArg("probe")) probePixels();
                    LOG("Saved %s", path.c_str());
                    shotFrame = 0;
                    shotIndex++;
                    if (shotIndex >= (int)shots.size()) break;
                }
            }
#ifdef HAVE_GAMEPLAY
            if (!autoplay.empty() && state == AS_PLAYING) {
                if (autoplay == "tour") {
                    if (tourDone && game.requestScreenshot.empty()) break;
                } else if (autoTime >= autoShot * autoShotEvery + 1.5f && (renderer.world->pendingCount() == 0 || autoTime > autoShot * autoShotEvery + 6.f)) {
                    std::string path = shotPath(StrFormat("auto_%s_%02d", autoplay.c_str(), autoShot));
                    gfx::saveScreenshotBMP(path.c_str());
                    Ped* pl = game.playerPed();
                    if (pl) {
                        int pv = game.playerVehicle();
                        int np = 0, nv = 0;
                        for (auto& q : game.peds) np += q.used;
                        for (auto& q : game.vehicles) nv += q.used;
                        float memWs = 0.f, memPriv = 0.f;
                        Platform::memoryUsageMB(memWs, memPriv);
                        LOG("autoplay t=%.1f pos %.1f %.1f %.1f state %d health %.0f veh %d speed %.1f | peds %d vehicles %d wanted %d | cpu ms "
                            "player %.2f ai %.2f veh %.2f peds %.2f | mem %.0f MB (private %.0f)",
                            autoTime, pl->pos.x, pl->pos.y, pl->pos.z, (int)pl->state, pl->health, pv, pv >= 0 ? game.vehicles[pv].sim.speed() : length(pl->vel),
                            np, nv, game.pinfo.wanted, game.profPlayer, game.profAI, game.profVehicles, game.profPeds, memWs, memPriv);
                    }
                    autoShot++;
                }
                if (autoTime > autoDuration) break;
            }
#endif
            // --menushot N: screenshot of the front end after N frames (automated menu test)
            if (state == AS_MENU && autotest) {
                static int menuFrames = 0;
                const char* ms = Platform::argValue("menushot");
                if (ms && ++menuFrames == atoi(ms)) {
                    std::string path = shotPath("menu_main");
                    gfx::saveScreenshotBMP(path.c_str());
                    LOG("Saved %s", path.c_str());
                    quit = true;
                }
            }
            if (quit) break;
            gfx::present(autotest ? false : renderer.settings.vsync);
        }
    }

    int newestSlot() {
        int best = -1;
#ifdef HAVE_GAMEPLAY
        std::string bestStamp;
        for (int s = 0; s < 8; s++) {
            UI::SaveSlotInfo si;
            if (game.readSlotInfo(s, si) && si.timestamp > bestStamp) {
                bestStamp = si.timestamp;
                best = s;
            }
        }
#endif
        return best;
    }

#if defined(HAVE_GAMEPLAY) && defined(HAVE_GAME_UI)
    // Letterbox bars during cutscenes (with subtitles drawn above them) and full-screen fades.
    void drawCinematicOverlay() {
        float W = (float)gfx::backbufferWidth(), H = (float)gfx::backbufferHeight();
        if (game.letterbox > 0.f) {
            float e = game.letterbox * game.letterbox * (3.f - 2.f * game.letterbox);
            float bar = H * 0.115f * e;
            UI::rect(0, 0, W, bar, UI::rgba(0, 0, 0, 1));
            UI::rect(0, H - bar, W, bar, UI::rgba(0, 0, 0, 1));
            if (!game.subText.empty() && game.subTimer > 0.f) {
                UI::TextStyle st;
                st.size = H * 0.028f;
                st.align = UI::ALIGN_CENTER;
                st.shadow = 2.f;
                std::string line = game.subSpeaker.empty() ? game.subText : game.subSpeaker + ": " + game.subText;
                UI::textWrapped(W * 0.15f, H - bar + bar * 0.25f, W * 0.7f, line.c_str(), st);
            }
            if (e > 0.9f) {
                UI::TextStyle hint;
                hint.size = H * 0.018f;
                hint.align = UI::ALIGN_RIGHT;
                hint.color = UI::rgba(1, 1, 1, 0.55f);
                UI::text(W - H * 0.03f, H - bar * 0.35f, game.ctl.usingPad ? "(A) Skip" : "[Space] Skip", hint);
            }
        }
        if (game.fadeAlpha > 0.f) UI::rect(0, 0, W, H, UI::rgba(0, 0, 0, game.fadeAlpha));
    }

    // While the phone is open it uses the navigation keys (arrows / Enter / Backspace / wheel, D-pad / A / B).
    static void maskPhoneInput(Controls& c) {
        c.phone = c.sprint = c.reload = c.confirm = c.back = c.skip = Button();
        c.radioNext = c.radioPrev = c.lights = c.special = Button();
        c.weaponScroll = 0;
        c.menuNav = vec2(0.f, 0.f);
    }

    // Phone context + actions the app owns (save, map, waypoint, photos); contacts, messages and apps belong to the
    // story layer.
    void updatePhone(const InputState& in, float dt) {
        UI::PhoneState& ph = game.phone;
        Ped* pl = game.playerPed();
        vec3 pp = pl ? pl->pos.toVec3() : game.rig.cam.pos.toVec3();
        ph.timeOfDay = env.timeOfDay;
        ph.day = game.gameDay;
        switch (weather.cur) {
            case WX_CLOUDY: case WX_OVERCAST: ph.weather = 1; break;
            case WX_RAIN: ph.weather = 2; break;
            case WX_STORM: ph.weather = 3; break;
            case WX_FOG: ph.weather = 4; break;
            default: ph.weather = 0; break;
        }
        // coverage: full in town, patchy out in the Sawgrass and offshore
        World::Region reg = map.regionAt(pp.x, pp.y);
        ph.signal = reg == World::REG_SAWGRASS ? 1 : (map.heightAt(pp.x, pp.y) < -8.f ? 2 : 4);
        ph.battery = Clamp(1.f - (float)fmod(game.pinfo.playTime, 10800.0) / 13500.f, 0.2f, 1.f);
        ph.owner = game.protagonistIndex == 0 ? "Mari" : "Dex";
        ph.money = game.pinfo.money;
        ph.playerPos = pp;
        if (!ph.photo.active) {
            ph.cameraPos = game.rig.cam.pos.toVec3();
            ph.cameraYaw = game.rig.cam.yaw;
            ph.cameraPitch = game.rig.cam.pitch;
            ph.cameraFov = game.rig.cam.fovY;
        }
        bool onMission = gMissions.active != nullptr;
        ph.canQuickSave = !onMission && game.pinfo.wanted == 0 && pl && pl->health > 0.f && game.playerControl;
        ph.quickSaveNote = onMission ? "Not available during missions" : (game.pinfo.wanted > 0 ? "Lose the police first" : "");
        phoneRefresh(game, ph);   // contacts, messages, apps, story calls (phone_game.cpp)
        UI::PhoneAction a = UI::Phone::update(ph, hud, in, dt);
        // the protagonist holds the phone to the ear while a call rings out or is connected
        if (pl) {
            pl->phoneCall = ph.call == UI::CALL_OUTGOING || ph.call == UI::CALL_ACTIVE;
            pl->phoneBrowse = ph.open && !pl->phoneCall && !ph.photo.active;   // looking at the screen
        }
        if (a.type != UI::PA_NONE && phoneHandle(game, ph, a)) return;   // calls, messages and apps
        switch (a.type) {
            case UI::PA_SET_WAYPOINT:
                game.hasWaypoint = true;
                game.waypoint = a.pos;
                game.gpsRecalcTimer = 0.f;
                break;
            case UI::PA_OPEN_MAP: openPause(UI::MENU_MAP); break;
            case UI::PA_QUICK_SAVE:
                ph.toast = ph.canQuickSave && game.saveGame(6, "Quick Save - " + game.storyTitle) ? "Game saved" : "Can't save right now";
                break;
            case UI::PA_EXIT_PHOTO_MODE: game.rig.cut = true; break;
            case UI::PA_TAKE_PHOTO: pendingPhoto = true; break;
            default: break;
        }
    }

    void openPause(UI::MenuScreen s) {
        menu.screen = s;
        menu.cursor = 0;
        menu.tab = 0;
#ifdef HAVE_AUDIO
        Audio::setPaused(true);
        Audio::play2D(Audio::SFX_UI_SELECT, 0.6f);
#endif
    }

    void prepareMenuData() {
        Ped* pl = game.playerPed();
        if (pl) {
            menu.playerPos = pl->pos.toVec3().xy();
            menu.playerHeading = pl->yaw;
        }
        menu.hasWaypoint = game.hasWaypoint;
        menu.waypoint = game.waypoint;
        menu.gpsRoute = game.hasWaypoint ? game.gpsRoute : game.missionRoute;
        menu.mapBlips = game.staticBlips;
        for (auto& b : game.missionBlips) menu.mapBlips.push_back(b);
        menu.canSave = !game.missionActive();
        if (menu.slots.size() != 8) {
            menu.slots.resize(8);
            for (int s = 0; s < 8; s++) game.readSlotInfo(s, menu.slots[s]);
        }
        menu.stats.clear();
        const PlayerInfo& pi = game.pinfo;
        auto add = [&](const char* n, const std::string& v) { menu.stats.push_back({n, v}); };
        add("Game completion", StrFormat("%.1f%%", game.completion()));
        add("Time played", StrFormat("%dh %02dm", (int)(pi.playTime / 3600), (int)fmod(pi.playTime / 60, 60)));
        add("Money", StrFormat("$%lld", pi.money));
        add("Distance on foot", StrFormat("%.1f km", pi.distanceWalked / 1000.0));
        add("Distance driven", StrFormat("%.1f km", pi.distanceDriven / 1000.0));
        add("Neon Shells found", StrFormat("%d / %d", pi.collectiblesFound, game.shellCount));
        add("Vehicles stolen", StrFormat("%d", pi.vehiclesStolen));
        add("Kills / headshots", StrFormat("%d / %d", pi.kills, pi.headshots));
        add("Accuracy", StrFormat("%.0f%%", pi.shotsFired ? 100.f * pi.shotsHit / pi.shotsFired : 0.f));
        add("Highest wanted level", StrFormat("%.0f stars", pi.maxWanted));
        add("Wasted / busted", StrFormat("%d / %d", pi.deaths, pi.arrests));
        add("Stores robbed", StrFormat("%d", Interiors::storeRobberies(game)));
        menu.briefTitle = game.storyTitle;
        menu.briefText = game.missionBrief();
        menu.money = game.pinfo.money;
        menu.timeOfDay = env.timeOfDay;
        menu.day = game.gameDay;
        menu.playerName = game.protagonistIndex == 0 ? "Mari Ortega" : "Dex Calloway";
    }

    bool handleMenuAction(const UI::MenuAction& a) {
        switch (a.type) {
            case UI::MA_NEW_GAME:
                startNewGame();
                break;
            case UI::MA_CONTINUE:
            case UI::MA_LOAD_SLOT: {
                int slot = a.type == UI::MA_LOAD_SLOT ? a.slot : newestSlot();
                if (slot >= 0 && game.loadGame(slot)) {
                    state = AS_PLAYING;
                    menu.screen = UI::MENU_NONE;
                    UI::hudReset();
                    weather.setImmediate(WX_FAIR);
#ifdef HAVE_AUDIO
                    Audio::setPaused(false);
                    Audio::setScore(0, 0.f);
#endif
                }
                break;
            }
            case UI::MA_SAVE_SLOT:
                if (game.saveGame(a.slot, game.storyTitle)) game.notify("GAME SAVED", StrFormat("Slot %d", a.slot + 1));
                for (int s = 0; s < 8; s++) game.readSlotInfo(s, menu.slots[s]);
                break;
            case UI::MA_RESUME:
                menu.screen = UI::MENU_NONE;
#ifdef HAVE_AUDIO
                Audio::setPaused(false);
#endif
                break;
            case UI::MA_QUIT_TO_MENU:
                openMainMenu();
                break;
            case UI::MA_QUIT_GAME:
                return true;
            case UI::MA_SETTINGS_CHANGED:
                applySettings();
                saveSettings();
                break;
            case UI::MA_SET_WAYPOINT:
                game.hasWaypoint = true;
                game.waypoint = a.pos;
                game.gpsRecalcTimer = 0.f;
                break;
            case UI::MA_CLEAR_WAYPOINT:
                game.hasWaypoint = false;
                game.gpsRoute.clear();
                break;
            default: break;
        }
        return false;
    }
#endif

#ifdef HAVE_GAME_UI
    void applySettings() {
        const UI::GameSettings& s = menu.settings;
        renderer.settings.vsync = s.vsync;
        renderer.settings.applyPreset(s.quality);
        if (const char* q = Platform::argValue("quality")) renderer.settings.applyPreset(atoi(q));   // test runs: 0 low .. 3 ultra
        renderer.settings.renderScale = Clamp(s.renderScale, 0.5f, 1.f);
        renderer.settings.motionBlur = s.motionBlur;
        renderer.post->exposureCompensation = 0.3f + s.brightness * 0.8f;
        if (Platform::isFullscreen() != s.fullscreen && !autotest) Platform::setFullscreen(s.fullscreen);
#ifdef HAVE_AUDIO
        Audio::setMasterVolume(s.masterVolume);
        Audio::setSfxVolume(s.sfxVolume);
        Audio::setMusicVolume(s.radioVolume);
        Audio::setVoiceVolume(s.dialogueVolume);
#endif
#ifdef HAVE_GAMEPLAY
        game.settingsSubtitles = s.subtitles;
        game.settingsRadar = s.showRadar;
        game.settingsMetric = s.metricUnits;
        game.vibration = s.vibration;
#endif
        UI::applyUiSettings(s);   // subtitle size/backing/speaker colours, HUD scale, reticle, reduced HUD flashing, UI colour-blind matrix
    }

    // %LOCALAPPDATA%\NeonTide\settings.ini: readable key=value text; unknown or missing keys keep their defaults
    std::string settingsPath() { return Platform::userDataDir() + "settings.ini"; }
    void saveSettings() {
        FILE* f = fopen(settingsPath().c_str(), "wb");
        if (!f) return;
        std::string text = UI::settingsToText(menu.settings);
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
    void loadSettings() {
        FILE* f = fopen(settingsPath().c_str(), "rb");
        if (f) {
            std::string text;
            char buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, got);
            fclose(f);
            UI::settingsFromText(text, menu.settings);
        }
        if (autotest) menu.settings.fullscreen = false;
        applySettings();
    }
#endif

    void shutdown() {
        if (loader.joinable()) loader.join();
#ifdef HAVE_GAMEPLAY
        if (state != AS_LOADING) game.shutdown();
#endif
#ifdef HAVE_AUDIO
        Audio::shutdown();
#endif
        UI::shutdown();
        if (state != AS_LOADING) renderer.shutdown();
        gfx::shutdown();
        Jobs::shutdown();
        Platform::shutdown();
    }
};

}  // namespace Game
