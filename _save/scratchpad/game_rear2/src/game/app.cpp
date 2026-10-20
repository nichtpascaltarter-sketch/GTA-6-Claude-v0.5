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
    // --benchmark: scripted scenes timed on this machine (results in %LOCALAPPDATA%\NeonTide\benchmark.txt)
    int benchScene = -1;
    float benchT = 0.f, benchReportT = 0.f;
    bool benchMeasuring = false, benchDone = false;
    std::vector<float> benchFrameMs;
    double benchUpdateMs = 0.0, benchRenderMs = 0.0, rawFrameSec = 0.0;
    int benchPeds = 0, benchVehs = 0, benchSamples = 0;
    std::vector<std::string> benchLines;
    float benchAvgFps = 0.f, benchWorstLow = 1e9f;
    float benchSeconds = 20.f;   // --benchseconds N: measured time per scene
    bool benchFromMenu = false;  // started from Settings: return to the main menu afterwards instead of exiting
    float tourT = 0.f;
    bool tourShot = false, tourDone = false;
    int meleeVictim = -1;        // --autoplay melee: the civilian for the takedown
    int fadePed = -1;            // --autoplay camfade: the pedestrian placed around the camera
    float uiShotAt = 0.f;        // --autoplay uishots: screenshot time into the current step
    int renderEvery = 1;         // --renderevery N: automated runs render every Nth gameplay frame (+ screenshot frames)
    float skippedDt = 0.f;       // game time since the last rendered frame
    int shotSettle = 0;          // --renderevery N > 1: frames rendered in a row so far for the requested screenshot
    int frameCap = 0;            // Settings: frame-rate cap (0 = unlimited)
    bool lastFpSetting = false;  // last applied "first person on foot" default
    u32 playFrames = 0;
    // autoplay test scripts (--autoplay walk|drive|bike|fly|boat|shoot)
    std::string autoplay;
    float autoTime = 0.f;
    int autoShot = 0;
    float autoShotEvery = 4.f;
    float autoDuration = 30.f;
    int doorShot = 0;   // --autoplay cardoor: the next of its shots
    int doorNpc = -1;   // (test: --npcrear, a passenger in the rear seat getting out first)
    float menuCamT = 0.f;

    bool init() {
        int w = Platform::argValue("width") ? atoi(Platform::argValue("width")) : 1600;
        int h = Platform::argValue("height") ? atoi(Platform::argValue("height")) : 900;
        autotest = Platform::hasArg("autotest");
        if (!Platform::init("Neon Tide", w, h, !autotest && Platform::hasArg("fullscreen"), false)) return false;
        int workers = Max(2, (int)std::thread::hardware_concurrency() - 1);
        Jobs::init(workers);
        if (!gfx::init(Platform::windowHandle(), Platform::clientWidth(), Platform::clientHeight(), Platform::hasArg("d3ddebug"))) {
            std::string msg = StrFormat(
                "Neon Tide requires Direct3D 12: a graphics card and driver with Direct3D 12 support, on Windows 10 or newer.\n\n"
                "Direct3D 12 could not be started on this PC: %s.\n\n"
                "Installing the latest graphics driver may help. The game will now close.",
                gfx::initError());
            LOG("FATAL: %s", msg.c_str());
            if (!autotest) Platform::showMessageBox("Neon Tide - Direct3D 12 required", msg.c_str(), true);
            gfx::shutdown();
            Jobs::shutdown();
            Platform::shutdown();
            return false;
        }
        if (Platform::hasArg("gfxselftest")) {   // graphics layer self-test (bindless, indirect, async compute, aliasing...)
            int failures = gfx::selfTest();
            LOG("gfx self-test: %d failure(s)", failures);
            gfx::shutdown();
            Jobs::shutdown();
            Platform::shutdown();
            ExitProcess(failures == 0 ? 0 : 2);
        }
        UI::init();
#ifdef HAVE_AUDIO
        if (!Platform::hasArg("nosound") && !Audio::init()) LOG("Audio: no output device, continuing silently");
#endif
        loadStart = TimeSeconds();
        parseShots();
        if (const char* a = Platform::argValue("autoplay")) autoplay = a;
        if (Platform::hasArg("benchmark")) autoplay = "benchmark";
        if (const char* d = Platform::argValue("benchseconds")) benchSeconds = Clamp((float)atof(d), 1.f, 300.f);
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
        game.rig.footFirstPerson = Platform::hasArg("firstperson");   // start in the on-foot first-person view (tests)
        if (const char* ws = Platform::argValue("weaponshowcase")) {   // --weaponshowcase x,y,z (test render of every gun)
            double x = 0, y = 0, z = 0;
            if (sscanf(ws, "%lf,%lf,%lf", &x, &y, &z) == 3) {
                game.weaponShowcase = true;
                game.showcasePos = dvec3(x, y, z);
            }
        }
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
#ifdef HAVE_GAME_UI
        game.rig.footFirstPerson = menu.settings.firstPersonOnFoot || Platform::hasArg("firstperson");
#endif
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
        if (Platform::hasArg("wildlifetest")) mu::setFlag(game, mu::EX_INTRO_DONE, 1);   // wildlife test scenes: no prologue call
        game.autosaveEnabled = autoplay.empty() && !autotest;   // automated runs never write the autosave slot
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
        gfx::RTV  rtv = gfx::backbufferRTV();
        float clearColor[4] = {0.01f, 0.012f, 0.03f, 1.f};
        gfx::ctx->setRenderTargets(1, &rtv, nullptr);
        gfx::ctx->clearRTV(rtv, clearColor);
        gfx::setViewport((float)gfx::backbufferWidth(), (float)gfx::backbufferHeight());
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
        // automated runs test free roam: the prologue's opening call and cutscene would take over the camera
        if (!Platform::hasArg("prologue")) mu::setFlag(game, mu::EX_INTRO_DONE, 1);
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
        if (autoplay == "fpguns") pl->invincible = true;
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
            // (--meleeweapon knife|bat: the player fights with that instead of fists)
            Ped& p = game.peds[game.player];
            p.weapon = WPN_FISTS;
            if (const char* mw = Platform::argValue("meleeweapon")) {
                WeaponType w = !strcmp(mw, "knife") ? WPN_KNIFE : (!strcmp(mw, "bat") ? WPN_BAT : WPN_FISTS);
                if (w != WPN_FISTS) game.giveWeapon(game.player, w, 1);
                p.weapon = w;
            }
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
        if (autoplay == "benchmark") {
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);   // no prologue phone call: free roam only
            benchScene = -1;
            benchDone = false;
            benchLines.clear();
            benchAvgFps = 0.f;
            benchWorstLow = 1e9f;
            autoDuration = 1e9f;   // ends after the results screen
            weather.locked = true;
        }
        if (autoplay == "uishots") {
            // the HUD, pause menu, map, settings pages, stats, phone apps and the wanted HUD (auto_uishots_NN_name.bmp)
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);
            pl->invincible = true;
            tourStop = -1;
            tourT = 0.f;
            tourShot = tourDone = false;
            autoDuration = 1e9f;
        }
        if (autoplay == "camfade") {
            // a pedestrian held across the line of sight at several distances, beside the player and in front of the
            // first-person eyes (auto_camfade_NN_name.bmp)
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);
            pl->invincible = true;
            tourStop = -1;
            tourT = 0.f;
            tourShot = tourDone = false;
            autoDuration = 1e9f;
            vec3 pp = pl->pos.toVec3();
            fadePed = game.spawnPed(game.randomCivilianChar(0x6a11u, 0), dvec3(pp.x + 3.f, pp.y, pp.z), pl->yaw, FAC_CIVILIAN);
            if (fadePed >= 0) {
                game.peds[fadePed].persistent = true;
                game.peds[fadePed].brain.type = BRAIN_NONE;
            }
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
        if (autoplay == "cardoor") {
            // getting in and out through a car's door: the player at the curb beside a sedan's driver's door (or
            // --doorcar <model name>), and the tallest driver in the crowd sitting in a low coupe parked ahead of it (the
            // headroom fit); applyAutoplay presses the enter key, films it from beside the door and takes the shots
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);
            weather.locked = true;
            weather.setImmediate(WX_CLEAR);
            env.timeOfDay = 11.f;
            autoDuration = Platform::argValue("npcrear") ? 17.f : (Platform::argValue("doorseat") ? 16.f : 23.f);
            autoShotEvery = 1e6f;
            Ped& p = game.peds[game.player];
            auto modelNamed = [&](const char* nm) {
                for (int i = 0; i < (int)game.vassets.size(); i++)
                    if (game.vassets[i].spec.name.rfind(nm, 0) == 0) return i;
                return -1;
            };
            const char* dc = Platform::argValue("doorcar");
            int ma = modelNamed(dc ? dc : "Harbor"), mb = modelNamed("Gatorback");
            vec2 q(1720.f, 360.f);
            int lane = -1;
            float u = 0.f;
            for (int k = 0; k < 80 && lane < 0; k++) {
                vec2 probe = q + vec2(cosf(k * 2.4f), sinf(k * 2.4f)) * (5.f + k * 3.f);
                float uu = 0.f;
                int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                if (li < 0) continue;
                const AI::Lane& L = game.laneGraph.lanes[li];
                bool street = L.cls == World::RC_STREET || L.cls == World::RC_AVENUE || L.cls == World::RC_LANE;
                if (!street || L.right >= 0 || World::roadInfo((World::RoadClass)L.cls).shoulder < 1.8f || L.u1 - L.u0 < 80.f) continue;
                lane = li;
                u = Clamp(uu, L.u0 + 20.f, L.u1 - 40.f);
            }
            if (lane >= 0 && ma >= 0) {
                const AI::Lane& L = game.laneGraph.lanes[lane];
                const float yawL = AI::dirYaw(game.laneGraph.laneTangent(lane, u)), off = L.width * 0.5f + World::roadInfo((World::RoadClass)L.cls).shoulder * 0.5f;
                // facing against the lane in the parking strip: the driver's door to the sidewalk
                vec3 a3 = game.laneGraph.lanePos(lane, u, off), b3 = game.laneGraph.lanePos(lane, u - 9.f, off);
                game.ai.testCar[0] = game.spawnVehicle(ma, dvec3(a3.x, a3.y, game.groundHeight(a3.x, a3.y, a3.z + 2.f) + 0.4f), yawL + kPi, false, FAC_CIVILIAN);
                if (mb >= 0) {
                    game.ai.testCar[1] = game.spawnVehicle(mb, dvec3(b3.x, b3.y, game.groundHeight(b3.x, b3.y, b3.z + 2.f) + 0.4f), yawL + kPi, false, FAC_CIVILIAN);
                    int tall = -1;
                    for (int ci : game.charsMaleCivil)
                        if (tall < 0 || game.chars[ci].desc.height > game.chars[tall].desc.height) tall = ci;
                    int d = tall >= 0 && game.ai.testCar[1] >= 0 ? game.spawnPed(tall, dvec3(b3.x, b3.y, b3.z + 1.f), yawL, FAC_CIVILIAN) : -1;
                    if (d >= 0) {
                        game.peds[d].persistent = true;
                        game.peds[d].brain.type = BRAIN_NONE;
                        game.warpPedIntoVehicle(d, game.ai.testCar[1], 0);
                        LOG("autoplay cardoor: driver %d (%.2f m) in car %d", d, game.chars[tall].desc.height, game.ai.testCar[1]);
                    }
                }
                if (Platform::argValue("npcrear") && game.ai.testCar[0] >= 0 && !game.charsMaleCivil.empty()) {
                    int ci = game.charsMaleCivil[game.charsMaleCivil.size() / 2];
                    doorNpc = game.spawnPed(ci, dvec3(a3.x, a3.y, a3.z + 1.f), yawL, FAC_CIVILIAN);
                    if (doorNpc >= 0) {
                        game.peds[doorNpc].persistent = true;
                        game.peds[doorNpc].brain.type = BRAIN_NONE;
                        game.warpPedIntoVehicle(doorNpc, game.ai.testCar[0], 2);
                        LOG("autoplay cardoor: rear passenger %d (%.2f m) in car %d", doorNpc, game.chars[ci].desc.height, game.ai.testCar[0]);
                    }
                }
                vec3 sw = game.laneGraph.lanePos(lane, u, off + 1.9f);
                p.pos = dvec3(sw.x, sw.y, game.groundHeight(sw.x, sw.y, sw.z + 2.f));
                p.yaw = yawL + kPi * 0.5f;   // (facing the car)
                game.rig.yaw = p.yaw;
                LOG("autoplay cardoor: car %d (%s) at %.1f %.1f, the player at %.1f %.1f", game.ai.testCar[0], game.vassets[ma].spec.name.c_str(), a3.x, a3.y,
                    sw.x, sw.y);
            }
        }
        if (autoplay == "crowd" || autoplay == "panic" || autoplay == "chase" || autoplay == "rage" || autoplay == "soak" || autoplay == "parking" ||
            autoplay == "bender" || autoplay == "hwysoak" || autoplay == "venues" || autoplay == "takeover" || autoplay == "surrender" || autoplay == "search" || autoplay == "k9" ||
            autoplay == "places" || autoplay == "greet" || autoplay == "hurt" || autoplay == "arrest" || autoplay == "brawl" || autoplay == "events" ||
            autoplay == "pedstop" || autoplay == "copbreak" || autoplay == "life") {
            // AI scenario tests: crowd variety at four places and hours / gunfire panic -> police response -> arrest /
            // night car chase at 4 stars (PIT, boxing, roadblocks, helicopter searchlight) / rear-ending a bold driver
            mu::setFlag(game, mu::EX_INTRO_DONE, 1);
            weather.locked = true;
            weather.setImmediate(WX_CLEAR);
            Ped& p = game.peds[game.player];
            game.populationWarmup = 2.5f;
            if (autoplay == "crowd") {
                autoDuration = 4 * 7.f + 0.5f;   // four stops, 7 s each (applyAutoplay)
            } else if (autoplay == "venues") {
                autoDuration = 3 * 24.f + 0.5f;  // port gate, airport forecourt, Sawgrass causeway: 24 s each (applyAutoplay)
            } else if (autoplay == "places") {
                autoDuration = 4 * 20.f + 0.5f;  // promenade terraces, campus quad, the track, the cemetery: 20 s each
            } else if (autoplay == "greet") {
                autoDuration = 2 * 35.f + 0.5f;  // the airport curb, then a downtown sidewalk at midday (applyAutoplay)
            } else if (autoplay == "hurt") {
                // a passer-by knocked down hard on a downtown sidewalk at midday (applyAutoplay): down hurt, the people who
                // stop, the ambulance, the medic, up and into the ambulance
                autoDuration = 150.5f;   // (the ambulance comes from 120-180 m, the patient limps to it)
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 13.f;
            } else if (autoplay == "events") {
                // the ambient events one after another on downtown sidewalks in the afternoon (applyAutoplay)
                autoDuration = 6 * 40.f + 0.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 16.f;
            } else if (autoplay == "brawl") {
                // a fight breaking out on a Calle Luna sidewalk at night (applyAutoplay): the shouting, the fists, the
                // crowd filming, the police coming for the one who started it
                autoDuration = 150.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 22.5f;
                game.ai.forceEvent = 12;   // (EV_BRAWL)
            } else if (autoplay == "life") {
                // street life at its own pace (applyAutoplay): a downtown sidewalk in the afternoon for a few minutes,
                // nothing forced - what happens round the player, counted
                autoDuration = 200.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 16.f;
            } else if (autoplay == "copbreak") {
                // a patrol's coffee break downtown in the late morning (applyAutoplay): a patrol car cruising a block or
                // two away pulls over outside a shop, the crew stands by the car with a coffee, then back in and away
                autoDuration = 160.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 11.f;
                game.ai.forceEvent = 99;   // (no ambient events running into it)
                int pm = game.findVehicleModel(Vehicles::VC_POLICE, 7);
                for (int k = 0; k < 12 && pm >= 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 0.52f + 0.3f), sinf(k * 0.52f + 0.3f)) * 90.f;
                    float u = 0.f;
                    int lane = game.laneGraph.nearestLane(probe, vec2(0.f), 40.f, &u);
                    if (lane < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    if (L.flags & (AI::LF_DIRT | AI::LF_HIGHWAY | AI::LF_RAMP)) continue;
                    u = Clamp(u, L.u0 + 5.f, L.u1 - 12.f);
                    vec3 c3 = game.laneGraph.lanePos(lane, u);
                    int vid = game.spawnVehicle(pm, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, u)), true, FAC_POLICE);
                    if (vid < 0) continue;
                    game.vehicles[vid].faction = FAC_POLICE;
                    game.attachTraffic(vid, lane, u);
                    if (game.vehicles[vid].seats[0] >= 0) game.peds[game.vehicles[vid].seats[0]].brain.type = BRAIN_DRIVER;
                    int partner = game.spawnPed(game.randomCivilianChar(hash32((u32)vid * 7u), 1), game.vehicles[vid].sim.body.pos, 0.f, FAC_POLICE);
                    if (partner >= 0) {
                        int seat = game.freeSeat(vid, false);
                        if (seat > 0) {
                            game.warpPedIntoVehicle(partner, vid, seat);
                            game.peds[partner].brain.type = BRAIN_PASSENGER;
                        } else {
                            game.despawnPed(partner);
                        }
                    }
                    game.vehAI(vid).role = VR_POLICE;
                    game.ai.testCar[0] = vid;
                    LOG("autoplay copbreak: patrol car %d at %.0f %.0f", vid, c3.x, c3.y);
                    break;
                }
            } else if (autoplay == "pedstop") {
                // three sidewalk stops by officers walking a beat downtown in the afternoon (applyAutoplay): one let go,
                // one with a warrant (the cuffs, a car called), one who runs for it
                autoDuration = 240.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 15.f;
                game.ai.forceEvent = 99;   // (no ambient events running into it)
            } else if (autoplay == "arrest") {
                // two street arrests on a downtown sidewalk in the afternoon (applyAutoplay): a thief caught by an officer
                // on a foot beat, then one run down by a patrol unit sent to the call (the time for that one's walk back
                // to the unit's car, the witness statements and the car driving off with them)
                autoDuration = 260.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 15.f;
                game.ai.forceEvent = 99;   // (no ambient events running into the arrests)
            } else if (autoplay == "surrender" || autoplay == "search") {
                if (autoplay == "search") autoDuration = 100.5f;   // (the units can take a minute to get there; then the search)
                // wanted at two stars on a downtown corner, empty-handed: units converge; then the player gives up (hands up,
                // cuffed, the lighter bust) / slips away out of sight at night (the officers on foot fan out and check the
                // corners and doorways round the last-seen point, torches on)
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                p.yaw = 2.4f;
                game.rig.yaw = p.yaw;
                env.timeOfDay = autoplay == "search" ? 22.5f : 13.f;
                game.giveWeapon(game.player, WPN_PISTOL, 60);
                p.weapon = WPN_FISTS;
                p.maxHealth = p.health = 400.f;
                game.timeScale = 1.5f;
                game.pinfo.wantedHeat = 2.4f;
                game.pinfo.wanted = 2;
                game.pinfo.lastSeenPos = p.pos;
                game.pinfo.lastSeenTime = (float)game.time;
                LOG("autoplay %s: wanted 2 at %.0f %.0f, money %lld", autoplay.c_str(), q.x, q.y, game.pinfo.money);
            } else if (autoplay == "k9") {
                // wanted at two stars and slipping away on foot down a sidewalk before any unit arrives: the K9 unit
                // follows the scent trail, the dog finds the player; then the player makes a run for it (applyAutoplay)
                vec2 q(2713.f, 763.f);
                float x = 0.f;
                int wl = game.laneGraph.nearestWalk(q, 60.f, &x);
                vec2 dir(0.f, 1.f);
                if (wl >= 0) {
                    const AI::WalkLink& L = game.laneGraph.walkLinks[wl];
                    x = Clamp(x, 0.f, Max(L.length - 1.f, 0.f));
                    vec3 w3 = game.laneGraph.walkPos(wl, Min(x, 3.f), 0.f, true);
                    dir = game.laneGraph.walkTangent(wl, Min(x, 3.f), true);
                    q = w3.xy();
                }
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                p.yaw = atan2f(-dir.x, dir.y);
                game.rig.yaw = p.yaw;
                env.timeOfDay = 17.f;
                p.weapon = WPN_FISTS;
                p.maxHealth = p.health = 400.f;
                game.timeScale = 1.5f;
                game.pinfo.wantedHeat = 2.4f;
                game.pinfo.wanted = 2;
                game.pinfo.lastSeenPos = p.pos;
                game.pinfo.lastSeenTime = (float)game.time;
                game.ai.dispatchOff = true;   // (no patrol cars cruising past: the dog team alone works the trail)
                LOG("autoplay k9: wanted 2 at %.0f %.0f heading %.2f %.2f (walk link %d)", q.x, q.y, dir.x, dir.y, wl);
            } else if (autoplay == "takeover") {
                // a street takeover staged a block or two away at night: donuts, the crowd, then the police and the scatter
                vec2 q(2300.f, -150.f);
                float u = 0.f;
                int lane = game.laneGraph.nearestLane(q, vec2(0.f), 60.f, &u);
                if (lane >= 0) {
                    vec3 c3 = game.laneGraph.lanePos(lane, u, game.laneGraph.lanes[lane].width * 0.5f + 3.f);
                    p.pos = dvec3(c3.x, c3.y, game.groundHeight(c3.x, c3.y, c3.z + 2.f));
                }
                env.timeOfDay = 22.5f;
                game.ai.forceEvent = 11;   // (EV_TAKEOVER)
            } else if (autoplay == "bender") {
                // two ordinary cars in one lane: the front one waits (a light, a gap), the one behind is not paying
                // attention and bumps into it at walking pace - the drivers stop, get out and have words
                vec2 q(2713.f, 763.f);
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 60 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f), sinf(k * 2.4f)) * (6.f + k * 5.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    if (L.flags & (AI::LF_DIRT | AI::LF_HIGHWAY)) continue;
                    uu = Max(uu, L.u0 + 6.f);
                    if (L.u1 - uu < 70.f) continue;
                    if (dot(game.laneGraph.laneTangent(li, uu), game.laneGraph.laneTangent(li, uu + 50.f)) < 0.995f) continue;
                    lane = li;
                    u = uu;
                }
                if (lane >= 0) {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    int ma = game.findVehicleModel(Vehicles::VC_SEDAN, 5), mb = game.findVehicleModel(Vehicles::VC_COMPACT, 9);
                    vec3 a3 = game.laneGraph.lanePos(lane, u + 30.f), b3 = game.laneGraph.lanePos(lane, u + 12.f);
                    float yaw = AI::dirYaw(game.laneGraph.laneTangent(lane, u + 20.f));
                    game.ai.testCar[0] = ma >= 0 ? game.spawnVehicle(ma, dvec3(a3.x, a3.y, a3.z + 0.4f), yaw, true, FAC_CIVILIAN) : -1;
                    game.ai.testCar[1] = mb >= 0 ? game.spawnVehicle(mb, dvec3(b3.x, b3.y, b3.z + 0.4f), yaw, true, FAC_CIVILIAN) : -1;
                    for (int c : {game.ai.testCar[0], game.ai.testCar[1]}) {
                        if (c < 0 || game.vehicles[c].seats[0] < 0) continue;
                        game.peds[game.vehicles[c].seats[0]].brain.type = BRAIN_DRIVER;
                        game.attachTraffic(c, lane, c == game.ai.testCar[0] ? u + 30.f : u + 12.f);
                    }
                    if (game.ai.testCar[0] >= 0)
                        if (AI::Driver* d = game.traffic.get(game.ai.testCar[0])) {
                            d->mode = AI::DM_HOLD;
                            d->holdTimer = 10.f;
                        }
                    // (applyAutoplay rolls it into the other: its driver's brain is off meanwhile, so the traffic AI
                    //  does not brake for the car ahead - the physics still runs)
                    if (game.ai.testCar[1] >= 0 && game.vehicles[game.ai.testCar[1]].seats[0] >= 0)
                        game.peds[game.vehicles[game.ai.testCar[1]].seats[0]].brain.type = BRAIN_NONE;
                    game.ai.forceBender = true;
                    // the player watches from the sidewalk
                    vec3 sw = game.laneGraph.lanePos(lane, Max(L.u0, u - 14.f), L.width * 0.5f + World::roadInfo((World::RoadClass)L.cls).shoulder + 2.5f);   // (not too close: a knock right next to the player is not "their own")
                    p.pos = dvec3(sw.x, sw.y, game.groundHeight(sw.x, sw.y, sw.z + 2.f));
                    game.ai.testCam = vec3(game.laneGraph.lanePos(lane, u + 19.f, L.width * 0.5f + 3.5f).xy(), sw.z + 1.9f);   // (8 m short of the knock)
                    LOG("autoplay bender: cars %d (held) and %d (rolling) on lane %d", game.ai.testCar[0], game.ai.testCar[1], lane);
                }
                env.timeOfDay = 11.f;
            } else if (autoplay == "parking") {
                // on the sidewalk of a street with a parking strip: owners come back to their cars and drive off, cars
                // pull into free spots and their drivers walk off (applyAutoplay follows each one with the camera)
                vec2 q(1720.f, 360.f);
                int lane = -1;
                float u = 0.f;
                for (int k = 0; k < 80 && lane < 0; k++) {
                    vec2 probe = q + vec2(cosf(k * 2.4f), sinf(k * 2.4f)) * (5.f + k * 3.f);
                    float uu = 0.f;
                    int li = game.laneGraph.nearestLane(probe, vec2(0.f), 30.f, &uu);
                    if (li < 0) continue;
                    const AI::Lane& L = game.laneGraph.lanes[li];
                    bool street = L.cls == World::RC_STREET || L.cls == World::RC_AVENUE || L.cls == World::RC_LANE;
                    if (!street || L.right >= 0 || World::roadInfo((World::RoadClass)L.cls).shoulder < 1.8f || L.u1 - L.u0 < 80.f) continue;
                    lane = li;
                    u = Clamp(uu, L.u0 + 20.f, L.u1 - 40.f);
                }
                if (lane >= 0) {
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    vec3 sw = game.laneGraph.lanePos(lane, u, L.width * 0.5f + World::roadInfo((World::RoadClass)L.cls).shoulder + 1.8f);
                    p.pos = dvec3(sw.x, sw.y, game.groundHeight(sw.x, sw.y, sw.z + 2.f));
                    p.yaw = AI::dirYaw(game.laneGraph.laneTangent(lane, u));
                    game.rig.yaw = p.yaw;
                    LOG("autoplay parking: sidewalk at %.0f %.0f beside lane %d", sw.x, sw.y, lane);
                }
                env.timeOfDay = 10.5f;
                game.timeScale = 1.5f;
                game.ai.lifeBoost = 6.f;
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
                bool chase = autoplay == "chase", hwy = autoplay == "hwysoak", soak = autoplay == "soak" || hwy;
                vec2 q = hwy ? vec2(2342.f, 1143.f) : (chase || soak ? vec2(2640.f, 700.f) : vec2(2713.f, 763.f));
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
                        p.pos = game.vehicles[vid].sim.body.pos;   // (the population pass this frame measures from here)
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
                            game.vehicles[npc].persistent = true;   // test prop: never recycled
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
                if (chase && lane >= 0) {
                    // the cruiser that clocked us, 45 m back in the same lane with its lights on
                    int pm = game.findVehicleModel(Vehicles::VC_POLICE, 7);
                    const AI::Lane& L = game.laneGraph.lanes[lane];
                    float cu = u - 45.f;
                    if (pm >= 0 && cu > L.u0 + 2.f) {
                        vec3 c3 = game.laneGraph.lanePos(lane, cu);
                        int cop = game.spawnVehicle(pm, dvec3(c3.x, c3.y, c3.z + 0.4f), AI::dirYaw(game.laneGraph.laneTangent(lane, cu)), true, FAC_POLICE);
                        if (cop >= 0 && game.vehicles[cop].seats[0] >= 0) {
                            Vehicle& cv = game.vehicles[cop];
                            cv.faction = FAC_POLICE;
                            cv.sirenOn = true;
                            int cd = cv.seats[0];
                            game.peds[cd].brain.type = BRAIN_COMBAT;
                            game.peds[cd].brain.target = game.player;
                            game.attachTraffic(cop, lane, cu);
                            game.vehAI(cop).role = VR_POLICE;
                            game.vehAI(cop).task = PT_PURSUE;
                            LOG("autoplay chase: pursuing cruiser %d 45 m behind", cop);
                        }
                    }
                }
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
        } else if (autoplay == "cardoor") {
            // in at 1 s, out at 12 s; filmed from the sidewalk behind the driver's door, then the tall driver in the
            // coupe ahead from beside its door
            static const int doorSeat = Platform::argValue("doorseat") ? atoi(Platform::argValue("doorseat")) : 0;
            const bool npcFirst = doorNpc >= 0;   // (the rear passenger out at 1 s; the player in at 6 s, out at 13 s)
            const float tIn = npcFirst ? 6.f : 1.f, tOut = npcFirst ? 13.f : 12.f;
            if (npcFirst && t >= 1.f && t - dt < 1.f && game.peds[doorNpc].vehicle >= 0) {
                game.removePedFromVehicle(doorNpc, true);
                LOG("autoplay cardoor: rear passenger %d getting out, state %d door %d", doorNpc, (int)game.peds[doorNpc].state, game.peds[doorNpc].doorVehicle);
            }
            if (npcFirst && t >= 4.5f && t - dt < 4.5f && game.peds[doorNpc].used && game.peds[doorNpc].vehicle < 0) {
                // (out: off along the sidewalk, out of the player's way to the same door)
                const Vehicle& va = game.vehicles[game.ai.testCar[0]];
                vec3 away = va.sim.body.pos.toVec3() + rotate(va.sim.body.rot, vec3(-2.4f, -6.f, 0.f));
                mu::setGoto(game, doorNpc, away, 1.4f);
            }
            c.enter.pressed = (doorSeat == 0 && t >= tIn && t - dt < tIn) || (t >= tOut && t - dt < tOut);
            c.special.pressed = doorSeat != 0 && t >= tIn && t - dt < tIn;
            const int a = game.ai.testCar[0], b = game.ai.testCar[1];
            const int carCam = t < 19.f ? a : b;
            if (carCam >= 0 && game.vehicles[carCam].used) {
                const Vehicle& v = game.vehicles[carCam];
                const Vehicles::SeatSpec& ss = game.vassets[v.model].spec.seats[t < 19.f ? doorSeat : 0];
                mat3 R = v.sim.body.rotMat();
                vec3 seat = v.sim.body.pos.toVec3() + R * ss.pos;
                vec3 side = R * vec3(-1.f, 0.f, 0.f), fwd = R * vec3(0.f, 1.f, 0.f);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(t < 19.f ? seat + side * 3.6f - fwd * 2.4f + vec3(0.f, 0.f, 0.9f) : seat + side * 2.2f + fwd * 0.6f + vec3(0.f, 0.f, 0.35f));
                game.rig.scriptTarget = dvec3(seat + side * 0.4f + vec3(0.f, 0.f, t < 19.f ? 0.25f : 0.45f));
                game.rig.scriptFov = t < 19.f ? 50.f : 40.f;
            }
            static const float kShotRear[] = {1.6f, 2.2f, 2.6f, 3.0f, 3.3f, 3.6f, 3.9f, 4.2f, 4.6f, 5.4f,
                                              12.6f, 12.9f, 13.2f, 13.5f, 13.8f, 14.1f, 14.5f, 15.2f};
            static const float kShotNpc[] = {1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.2f, 6.6f, 7.2f, 7.8f, 8.4f, 9.0f, 10.0f, 13.6f, 14.2f, 14.8f, 15.4f, 16.2f};
            const float* kShotAt = npcFirst ? kShotNpc : kShotRear;
            const int nShots = npcFirst ? (int)(sizeof(kShotNpc) / sizeof(kShotNpc[0])) : (int)(sizeof(kShotRear) / sizeof(kShotRear[0]));
            if (doorShot < nShots && t >= kShotAt[doorShot] && game.requestScreenshot.empty()) {
                game.requestScreenshot = shotPath(StrFormat("auto_cardoor_%02d_%04.1fs", doorShot, kShotAt[doorShot]));
                const Ped* pl = game.playerPed();
                if (pl) LOG("autoplay cardoor: shot %d at %.1f s, state %d veh %d", doorShot, t, (int)pl->state, game.playerVehicle());
                doorShot++;
            }
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
        } else if (autoplay == "surrender") {
            // hands up once the first officers are close; the cuffs; the release (weapons back, half the fine)
            static bool asked = false, bustedSeen = false, released = false;
            static float logT = 0.f, shotT = 0.f;
            static int shots = 0;
            Ped* pl = game.playerPed();
            float nearest = 1e9f;
            int nearTactic = -1;
            for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                const Ped& q = game.peds[i];
                if (!q.used || q.faction != FAC_POLICE || q.health <= 0.f || q.state != PS_ONFOOT || !pl) continue;
                float d = length(rel(q.pos, pl->pos));
                if (d < nearest) {
                    nearest = d;
                    nearTactic = game.ai.ped[i].tactic;
                }
            }
            if (!asked && pl) {
                // (a caller stays on the line with the police until then: the units come straight in instead of searching
                //  the area, and the stars hold)
                game.pinfo.lastSeenPos = pl->pos;
                game.pinfo.lastSeenTime = (float)game.time;
                game.pinfo.wantedHeat = Max(game.pinfo.wantedHeat, 2.4f);
                game.pinfo.wantedCooldown = 0.f;
            }
            if (!asked && pl && (nearest < 40.f || t > 45.f) && t > 6.f) {
                asked = true;
                game.ai.forceSurrender = true;
                shotT = 0.5f;
                LOG("autoplay surrender: hands up at t=%.1f (nearest officer %.1f m)", t, nearest);
            }
            if (game.pinfo.busted && !bustedSeen) {
                bustedSeen = true;
                shotT = 0.3f;
                LOG("autoplay surrender: busted at t=%.1f money %lld", t, game.pinfo.money);
            }
            static float releasedAt = -1.f;
            if (bustedSeen && !game.pinfo.busted && !released && pl) {
                released = true;
                releasedAt = t;
                LOG("autoplay surrender: released at t=%.1f", t);
            }
            if (releasedAt >= 0.f && t > releasedAt + 1.f && pl) {
                // (a second on: the police have handed the weapons back and refunded half the fine by then)
                releasedAt = -1.f;
                LOG("autoplay surrender: after the release | pistol %d ammo %d | money %lld", (int)pl->hasWeapon[WPN_PISTOL], pl->ammo[WPN_PISTOL] + pl->clip[WPN_PISTOL],
                    game.pinfo.money);
            }
            if (asked && !released && pl && pl->state == PS_ONFOOT) {
                // a camera off the player's shoulder, looking at the officers coming in
                vec2 at = pl->pos.toVec3().xy();
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(at.x + 5.f, at.y - 6.f, pl->pos.z + 3.2f);
                game.rig.scriptTarget = dvec3(at.x, at.y, pl->pos.z + 1.1f);
                game.rig.scriptFov = 55.f;
            } else {
                game.rig.scriptActive = false;
            }
            shotT -= dt;
            if (asked && shotT <= 0.f && shots < 10 && !released) {
                shotT = 4.f;
                game.requestScreenshot = shotPath(StrFormat("auto_surrender_%02d", shots));
                shots++;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                LOG("autoplay surrender t=%.1f wanted %d surrender %d busted %d | nearest officer %.1f m tactic %d | %s", t, game.pinfo.wanted, (int)game.ai.surrender,
                    (int)game.pinfo.busted, nearest, nearTactic, game.aiCensusText(60.f).c_str());
            }
        } else if (autoplay == "search") {
            // once the units have seen the player, slip away out of sight (320 m off, beyond the search area, where no
            // car searching it comes by) and watch the officers on foot search the corners and doorways round the
            // last-seen point - the stars held for 25 s (out there they would go in 12)
            static bool gone = false;
            static vec2 lastSeen;
            static float logT = 0.f, shotT = 0.f, goneAt = 0.f;
            static int shots = 0;
            static u32 camUid = 0;
            static vec2 camOff(9.f, 0.f);
            Ped* pl = game.playerPed();
            int copsFoot = 0;
            for (const Ped& q : game.peds)
                copsFoot += q.used && q.faction == FAC_POLICE && q.health > 0.f && q.state == PS_ONFOOT && pl && length(rel(q.pos, pl->pos)) < 70.f;
            if (!gone && pl) {
                // (reported until then: the units come to the player, and the stars hold)
                game.pinfo.lastSeenPos = pl->pos;
                game.pinfo.lastSeenTime = (float)game.time;
                game.pinfo.wantedHeat = Max(game.pinfo.wantedHeat, 2.4f);
                game.pinfo.wantedCooldown = 0.f;
            }
            if (!gone && pl && ((copsFoot >= 2 && t > 10.f) || t > 55.f)) {
                gone = true;
                lastSeen = pl->pos.toVec3().xy();
                // the hiding place: a sidewalk some 320 m off, in whichever of eight directions is furthest from every
                // police car and officer (none of them may see it, or there is nothing to search for)
                vec2 hide = lastSeen + vec2(-320.f, 8.f);
                float bestD = -1.f;
                goneAt = t;
                for (int k = 0; k < 8; k++) {
                    vec2 h = lastSeen + vec2(cosf(k * kTwoPi / 8.f), sinf(k * kTwoPi / 8.f)) * 320.f;   // (beyond the 2-star search area: 300 m)
                    float x = 0.f;
                    int wl = game.laneGraph.nearestWalk(h, 30.f, &x);
                    if (wl < 0) continue;
                    h = game.laneGraph.walkPos(wl, x, 0.f, true).xy();
                    float md = 1e9f;
                    for (const Ped& q : game.peds)
                        if (q.used && q.faction == FAC_POLICE && q.health > 0.f) md = Min(md, length(q.pos.toVec3().xy() - h));
                    for (const Vehicle& v : game.vehicles)
                        if (v.used && v.faction == FAC_POLICE) md = Min(md, length(v.sim.body.pos.toVec3().xy() - h));
                    if (md > bestD) {
                        bestD = md;
                        hide = h;
                    }
                }
                LOG("autoplay search: hiding at %.0f %.0f, %.0f m from the nearest unit", hide.x, hide.y, bestD);
                pl->pos = dvec3(hide.x, hide.y, game.groundHeight(hide.x, hide.y, (float)pl->pos.z + 3.f));
                pl->vel = vec3(0.f);
                // (as if the slip had taken the ten seconds a real one would: units sent from now on come in round the
                //  last sighting, not round where the player - teleported - really is)
                game.pinfo.lastSeenTime = (float)game.time - 10.5f;
                shotT = 3.f;
                LOG("autoplay search: slipped away at t=%.1f (%d officers on foot), last seen %.0f %.0f", t, copsFoot, lastSeen.x, lastSeen.y);
            }
            if (gone && t < goneAt + 25.f && game.pinfo.wanted > 0 && !game.pinfo.policeSeesPlayer) game.pinfo.wantedCooldown = 0.f;   // (see above)
            if (gone) {
                vec2 ls = game.pinfo.lastSeenPos.toVec3().xy();
                // the camera on one officer working the search plan (the nearest to the last-seen point with a corner or
                // a doorway to check; kept while they search), 9 m off and 5 m up on a side with a clear view of them;
                // until there is one, the last-seen point from above
                int ci = -1;
                const int np = Min((int)game.peds.size(), (int)game.ai.ped.size());
                auto searcher = [&](int i) {
                    const Ped& q = game.peds[i];
                    return q.used && q.faction == FAC_POLICE && q.health > 0.f && q.state == PS_ONFOOT && game.ai.ped[i].uid == q.uid &&
                           game.ai.ped[i].tactic == FT_SEARCH && game.ai.ped[i].searchSpot >= 0;
                };
                for (int i = 0; i < np && camUid; i++)
                    if (game.peds[i].uid == camUid && searcher(i)) ci = i;
                if (ci < 0) {
                    camUid = 0;
                    float bd = 80.f;
                    for (int i = 0; i < np; i++) {
                        float d = length(game.peds[i].pos.toVec3().xy() - ls);
                        if (searcher(i) && d < bd) {
                            bd = d;
                            ci = i;
                        }
                    }
                    if (ci >= 0) camUid = game.peds[ci].uid;
                }
                if (ci >= 0) {
                    vec3 P = game.peds[ci].pos.toVec3();
                    auto clear = [&](vec2 o) {
                        vec3 cam = P + vec3(o, 5.f);
                        return game.lineOfSight(dvec3(cam), dvec3(P + vec3(0.f, 0.f, 1.2f)), ci, -1) &&
                               game.lineOfSight(dvec3(P + vec3(0.f, 0.f, 1.2f)), dvec3(cam), ci, -1);   // (both ways: the camera not inside a wall)
                    };
                    if (!clear(camOff)) {
                        float a0 = atan2f(camOff.y, camOff.x);
                        for (int k = 1; k < 8; k++) {
                            float a = a0 + (k & 1 ? 1.f : -1.f) * ((k + 1) / 2) * (kTwoPi / 8.f);
                            vec2 o = vec2(cosf(a), sinf(a)) * 9.f;
                            if (clear(o)) {
                                camOff = o;
                                break;
                            }
                        }
                    }
                    game.rig.scriptActive = true;
                    game.rig.scriptPos = dvec3(P + vec3(camOff, 5.f));
                    game.rig.scriptTarget = dvec3(P + vec3(0.f, 0.f, 1.f));
                    game.rig.scriptFov = 55.f;
                } else {
                    float gz = game.groundHeight(ls.x, ls.y, 30.f);
                    game.rig.scriptActive = true;
                    game.rig.scriptPos = dvec3(ls.x + 18.f, ls.y - 22.f, gz + 16.f);
                    game.rig.scriptTarget = dvec3(ls.x, ls.y, gz + 1.f);
                    game.rig.scriptFov = 60.f;
                }
                shotT -= dt;
                if (shotT <= 0.f && shots < 10) {
                    shotT = 5.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_search_%02d", shots));
                    shots++;
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                // the officers on foot round the last-seen point: how many, how many working a corner / doorway of the
                // search plan (and how many have got to theirs), torches on
                int foot = 0, searching = 0, atSpot = 0, torches = 0;
                vec2 ls = game.pinfo.lastSeenPos.toVec3().xy();
                for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                    const Ped& q = game.peds[i];
                    if (!q.used || q.faction != FAC_POLICE || q.health <= 0.f || q.state != PS_ONFOOT || game.ai.ped[i].uid != q.uid) continue;
                    if (length(q.pos.toVec3().xy() - ls) > 80.f) continue;
                    foot++;
                    const PedAI& qa = game.ai.ped[i];
                    searching += qa.tactic == FT_SEARCH;
                    atSpot += qa.tactic == FT_SEARCH && qa.searchSpot >= 0 && qa.searchLook >= 0.f;
                    torches += q.aiming;   // (the sweep: gun up, the light on it at night)
                }
                LOG("autoplay search t=%.1f wanted %d seen %d | round the last-seen point: %d officers on foot, %d searching, %d at a corner/doorway, %d sweeping | %s", t,
                    game.pinfo.wanted, (int)game.pinfo.policeSeesPlayer, foot, searching, atSpot, torches, game.aiCensusText(90.f).c_str());
            }
        } else if (autoplay == "k9") {
            // run 12 s round the block on the sidewalks, then stand still out of sight of where the trail starts: the K9
            // unit tracks the trail and the dog finds the player; 12 s after that the player runs for it and the dog is
            // sent after them
            static float logT = 0.f, shotT = 0.f, foundAt = -1.f;
            static int shots = 0;
            static AI::Walker run;
            static bool runInit = false;
            Ped* pl = game.playerPed();
            vec3 dogP;
            std::string st = game.aiK9Text(&dogP);
            bool active = st.rfind("k9: none", 0) != 0;
            if (pl && pl->state == PS_ONFOOT && !game.pinfo.busted) {
                bool dash = foundAt >= 0.f && t > foundAt + 12.f && t < foundAt + 22.f;
                if (t > 1.f && t < 13.f) {
                    // (the pedestrian navigator steers: along the sidewalk, round a corner, toward a point diagonally across
                    //  the block - the way faces the camera, the run is a sprint)
                    vec2 pp = pl->pos.toVec3().xy();
                    if (!runInit) {
                        runInit = true;
                        game.pedNav.place(run, pp, 0x9e37u, 30.f);
                        vec2 f = AI::yawDir(pl->yaw);
                        run.hasDest = true;
                        run.dest = pp + f * 45.f + AI::rightOf(f) * 45.f;
                        run.avoidCrossing = true;
                        run.speed = 6.5f;   // (the walker's progress keeps pace with the sprint)
                        run.hurry = 1.f;
                        LOG("autoplay k9: the run round the block toward %.0f %.0f (walk link %d)", run.dest.x, run.dest.y, run.link);
                    }
                    float fy = pl->yaw;
                    vec2 v = run.link >= 0 ? game.pedNav.step(run, pp, dt, -1, &fy) : AI::yawDir(pl->yaw);
                    if (length(v) > 0.2f) {
                        game.rig.yaw = AI::dirYaw(normalize(v));
                        c.move = vec2(0.f, 1.f);
                        c.sprint.down = true;
                    }
                } else if (dash) {
                    c.move = vec2(0.f, 1.f);
                    c.sprint.down = true;
                }
            }
            if (foundAt < 0.f && st.find("found 0.0") == std::string::npos && active && st.find("found") != std::string::npos) foundAt = t;
            if (active && pl) {
                // the camera behind the dog, looking the way it goes (at the player once it has found them)
                vec2 dp = dogP.xy(), pp = pl->pos.toVec3().xy();
                vec2 dirc = normalize(pp - dp + vec2(1e-4f, 0.f));
                vec2 cam = dp - dirc * 7.f + AI::rightOf(dirc) * 2.5f;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cam.x, cam.y, game.groundHeight(cam.x, cam.y, dogP.z + 3.f) + 2.4f);
                game.rig.scriptTarget = dvec3(dogP + vec3(dirc * 4.f, 0.6f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 14) {
                    shotT = 4.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_k9_%02d", shots));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                LOG("autoplay k9 t=%.1f wanted %d seen %d player %.1f %.1f state %d | %s", t, game.pinfo.wanted, (int)game.pinfo.policeSeesPlayer, pl ? pl->pos.x : 0.0,
                    pl ? pl->pos.y : 0.0, pl ? (int)pl->state : -1, st.c_str());
            }
        } else if (autoplay == "takeover") {
            // follow the takeover from across the crossing: shots every 8 s, its state every 2 s
            static float logT = 0.f, shotT = 6.f, since = -1.f;
            static int shots = 0;
            static vec2 camAt;
            int stage = -1, car = -1;
            vec3 at;
            std::string st = game.aiEventText(&stage, &at, &car);
            if (stage >= 0) {
                if (since < 0.f) {
                    since = 0.f;
                    // the camera on the sidewalk of the street leading toward the player, looking into the crossing
                    Ped* pl = game.playerPed();
                    vec2 from = pl ? pl->pos.toVec3().xy() : at.xy() + vec2(30.f, 0.f);
                    vec2 dirc = normalize(from - at.xy() + vec2(1e-3f, 0.f));
                    for (int n = 0; n < (int)game.laneGraph.nodes.size(); n++) {
                        if (length(game.roads->nodes[n].p - at.xy()) > 1.f) continue;
                        float best = -2.f;
                        for (const AI::Approach& A : game.laneGraph.nodes[n].approaches)
                            if (dot(A.dir, dirc) > best) {
                                best = dot(A.dir, dirc);
                                camAt = at.xy() + A.dir * 27.f + AI::rightOf(A.dir) * 7.5f;
                            }
                        break;
                    }
                    if (length2(camAt) < 1.f) camAt = at.xy() + dirc * 24.f;
                    LOG("autoplay takeover: staged at %.0f %.0f (t=%.1f)", at.x, at.y, t);
                }
                since += dt;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(camAt.x, camAt.y, game.groundHeight(camAt.x, camAt.y, at.z + 5.f) + 3.2f);
                vec3 look = at + vec3(0.f, 0.f, 1.f);
                if (stage > 0 && car >= 0 && car < (int)game.vehicles.size() && game.vehicles[car].used) look = game.vehicles[car].sim.body.pos.toVec3() + vec3(0.f, 0.f, 0.8f);
                game.rig.scriptTarget = dvec3(look);
                game.rig.scriptFov = 50.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 12) {
                    shotT = 8.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_takeover_%02d_s%d", shots, stage));
                    shots++;
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                // (the nearest police car: how the response is getting on)
                float copD = 1e9f, copV = 0.f;
                int copI = -1;
                for (int vi = 0; vi < (int)game.vehicles.size(); vi++) {
                    const Vehicle& pv = game.vehicles[vi];
                    if (pv.used && pv.faction == FAC_POLICE) {
                        float dd = length(pv.sim.body.pos.toVec3().xy() - at.xy());
                        if (dd < copD) {
                            copD = dd;
                            copV = pv.sim.speed();
                            copI = vi;
                        }
                    }
                }
                std::string cs;
                if (copI >= 0 && copI < (int)game.ai.veh.size()) {
                    const AI::Driver* dr = game.traffic.get(copI);
                    int drv = game.vehicles[copI].seats[0];
                    cs = StrFormat(" [car %d task %d mode %d held %.1f brain %d target %d]", copI, (int)game.ai.veh[copI].task, dr ? (int)dr->mode : -1,
                                   game.ai.veh[copI].heldUp, drv >= 0 ? (int)game.peds[drv].brain.type : -1, drv >= 0 ? game.peds[drv].brain.target : -1);
                }
                LOG("autoplay takeover t=%.1f | %s | wanted %d | nearest police car %.0f m (%.1f m/s)%s", t, st.c_str(), game.pinfo.wanted, copD < 1e8f ? copD : -1.f, copV,
                    cs.c_str());
            }
        } else if (autoplay == "events") {
            // six ambient events in turn, 40 s each: forced, watched from across the street, logged as they play out
            // (a mugging, a purse snatching, a fender bender, a police chase, a breakdown, a traffic stop)
            static const int kTypes[6] = {0, 1, 2, 4, 9, 10};   // EV_MUGGING, EV_PURSE, EV_CRASH, EV_CHASE, EV_BREAKDOWN, EV_TRAFFIC_STOP
            static float logT = 0.f, shotT = 3.f;
            static int shots = 0, cur = -1;
            int slot = Min((int)(t / 40.f), 5);
            if (slot != cur) {
                cur = slot;
                game.ai.forceEvent = kTypes[slot];
                shotT = 3.f;
                LOG("autoplay events: %d - forcing event type %d (t=%.1f)", slot, kTypes[slot], t);
            }
            int stage = -1;
            vec3 at;
            std::string st = game.aiEventsText(kTypes[slot], &stage, &at);
            if (stage >= 0) {
                game.ai.forceEvent = 99;   // (that one is on: no second, and no others mixing in)
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(at + vec3(7.f, -8.f, 4.5f));
                game.rig.scriptTarget = dvec3(at + vec3(0.f, 0.f, 1.f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 40) {
                    shotT = 8.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_events_%02d_e%d_s%d", shots, kTypes[slot], stage));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                LOG("autoplay events t=%.1f | %s | %s", t, st.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "brawl") {
            // follow the fight from across the sidewalk, then the one who started it (and the police after him)
            static float logT = 0.f, shotT = 2.f;
            static int shots = 0, starter = -1;
            static u32 starterUid = 0;
            int stage = -1;
            vec3 at;
            std::string st = game.aiBrawlText(&stage, &at, &starter, &starterUid);
            if (stage >= 0) game.ai.forceEvent = 99;   // (that one: no second fight, and no other events running into it)
            static bool deadLogged = false;
            if (!deadLogged && starter >= 0 && starter < (int)game.peds.size() && game.peds[starter].used && game.peds[starter].uid == starterUid &&
                game.peds[starter].health <= 0.f) {
                deadLogged = true;
                const Ped& q = game.peds[starter];
                int la = q.lastAttacker;
                LOG("autoplay brawl: the one who started it is dead (t=%.1f) - last hurt by %d%s%s", t, la,
                    la >= 0 && la < (int)game.peds.size() && game.peds[la].faction == FAC_POLICE ? " (police)" : "",
                    la >= 0 && la < (int)game.peds.size() && game.peds[la].vehicle >= 0 ? " (driving)" : "");
            }
            int s = starter >= 0 && starter < (int)game.peds.size() && game.peds[starter].used && game.peds[starter].uid == starterUid ? starter : -1;
            if (stage >= 0 || s >= 0) {
                vec3 P = s >= 0 ? (game.peds[s].state == PS_INVEHICLE && game.peds[s].vehicle >= 0 ? game.vehicles[game.peds[s].vehicle].sim.body.pos.toVec3() : game.peds[s].pos.toVec3()) : at;
                if (stage >= 0 && stage < 2) P = at;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(P + vec3(5.5f, -6.5f, 3.6f));
                game.rig.scriptTarget = dvec3(P + vec3(0.f, 0.f, 1.f));
                game.rig.scriptFov = 52.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 20) {
                    shotT = 5.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_brawl_%02d_s%d", shots, stage));
                    shots++;
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                std::string ss = "starter none";
                if (s >= 0) {
                    const Ped& q = game.peds[s];
                    ss = StrFormat("starter %d state %d act %d brain %d veh %d", s, (int)q.state, (int)game.ai.ped[s].activity, (int)q.brain.type, q.vehicle);
                }
                LOG("autoplay brawl t=%.1f | %s | %s | %s", t, st.c_str(), ss.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "arrest") {
            // stage 1: a passer-by turns thief and runs; an officer on a foot beat 16 m behind runs them down, cuffs them
            // and - with no car of their own - calls one: the thief sits on the kerb until it pulls over, then is walked to
            // it and put in the back. Stage 2: another thief, reported (a crime incident): a patrol unit is sent, the
            // officers run them down and walk them back to their own car. Onlookers stop, watch and film throughout.
            static int stage = 0, perp = -1, cop = -1;
            static u32 perpUid = 0, copUid = 0;
            static float logT = 0.f, shotT = 0.f, doneT = -1.f, nextAt = 5.f;
            static int shots = 0;
            static vec2 camOff(6.f, -6.f);
            Ped* pl = game.playerPed();
            const int np = Min((int)game.peds.size(), (int)game.ai.ped.size());
            auto alive = [&](int i, u32 uid) { return i >= 0 && i < np && game.peds[i].used && game.peds[i].uid == uid && game.peds[i].health > 0.f; };
            // a passer-by walking the sidewalk dmin..dmax from the player (the nearest)
            auto pickWalker = [&](float dmin, float dmax) {
                int best = -1;
                float bd = dmax;
                for (int i = 0; i < np && pl; i++) {
                    const Ped& q = game.peds[i];
                    if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || game.ai.ped[i].uid != q.uid ||
                        game.ai.ped[i].activity != ACT_WALK || q.brain.type != BRAIN_WANDER || game.ai.ped[i].leader >= 0)
                        continue;
                    float d = length(rel(q.pos, pl->pos));
                    if (d > dmin && d < bd) {
                        bd = d;
                        best = i;
                    }
                }
                return best;
            };
            if ((stage == 0 || stage == 2) && t > nextAt && pl) {
                perp = pickWalker(12.f, 45.f);
                nextAt = t + 1.f;
                if (perp >= 0) {
                    Ped& q = game.peds[perp];
                    perpUid = q.uid;
                    q.weapon = WPN_FISTS;
                    game.ai.ped[perp].activity = ACT_WALK;
                    vec2 qp = q.pos.toVec3().xy();
                    vec2 away = normalize(qp - pl->pos.toVec3().xy() + vec2(1e-3f, 0.f));
                    if (stage == 0) {
                        // the officer on the beat, 16 m back toward the player (on the sidewalk there)
                        vec2 cp = qp - away * 16.f;
                        float x = 0.f;
                        int wl = game.laneGraph.nearestWalk(cp, 25.f, &x);
                        if (wl >= 0) cp = game.laneGraph.walkPos(wl, x, 0.f, true).xy();
                        cop = game.spawnPed(game.randomCivilianChar(hash32(q.uid * 7u), 1), dvec3(cp.x, cp.y, game.groundHeight(cp.x, cp.y, (float)q.pos.z + 2.f)),
                                            atan2f(-away.x, away.y), FAC_POLICE);
                        if (cop >= 0) {
                            copUid = game.peds[cop].uid;
                            game.giveWeapon(cop, WPN_PISTOL, 60);
                            game.peds[cop].weapon = WPN_PISTOL;
                            game.peds[cop].brain.type = BRAIN_COMBAT;
                            game.peds[cop].brain.target = perp;
                            game.peds[cop].brain.accuracy = 0.4f;
                            game.pedAI(cop).homeVeh = -1;
                            q.brain.type = BRAIN_FLEE;
                            q.brain.target = cop;
                            q.brain.timer = 0.f;
                            stage = 1;
                            LOG("autoplay arrest: stage 1 at t=%.1f - beat officer %d after thief %d at %.0f %.0f", t, cop, perp, qp.x, qp.y);
                        }
                    } else {
                        // a thief reported by a witness: the dispatcher sends a unit
                        q.brain.type = BRAIN_FLEE;
                        q.brain.target = -1;
                        q.brain.goal = pl->pos;
                        q.brain.timer = 0.f;
                        Game::events_detail::addCrimeIncident(game, q.pos, perp);
                        cop = -1;
                        stage = 3;
                        LOG("autoplay arrest: stage 3 at t=%.1f - thief %d reported at %.0f %.0f", t, perp, qp.x, qp.y);
                    }
                    doneT = -1.f;
                    shotT = 1.f;
                }
            }
            // the officer on the case (stage 3: whoever has the thief as their target or prisoner)
            if (stage == 3 && !alive(cop, copUid) && alive(perp, perpUid)) {
                for (int i = 0; i < np; i++) {
                    const Ped& q = game.peds[i];
                    if (q.used && q.faction == FAC_POLICE && game.ai.ped[i].uid == q.uid && (q.brain.target == perp || game.ai.ped[i].escortPed == perp)) {
                        cop = i;
                        copUid = q.uid;
                        LOG("autoplay arrest: officer %d on thief %d (t=%.1f)", i, perp, t);
                        break;
                    }
                }
            }
            bool perpOk = alive(perp, perpUid);
            // a stage done: the thief in the back of a car (or gone)
            if ((stage == 1 || stage == 3) && (!perpOk || game.peds[perp].state == PS_INVEHICLE) && doneT < 0.f) {
                doneT = t;
                const char* how = perpOk ? "in the car" : (perp < 0 || perp >= np || !game.peds[perp].used || game.peds[perp].uid != perpUid ? "gone (despawned)" : "dead");
                LOG("autoplay arrest: stage %d done at t=%.1f - thief %s", stage, t, how);
            }
            if ((stage == 1 || stage == 3) && doneT >= 0.f && t > doneT + 10.f) {
                // (the car driving off with them for a few seconds more, then the next one / the end)
                stage = stage == 1 ? 2 : 4;
                nextAt = t;
            }
            // a witness statement in progress (police_statement, the officer in front of them): the camera on the two of
            // them instead while the thief sits in a car (logged once each)
            int stmtCop = -1;
            for (int i = 0; i < np && stmtCop < 0; i++) {
                const Ped& c = game.peds[i];
                const PedAI& ca = game.ai.ped[i];
                if (!c.used || c.faction != FAC_POLICE || ca.uid != c.uid || !Game::police_statement::taking(c)) continue;
                int w = ca.stmtWith;
                if (w >= 0 && w < np && game.peds[w].used && game.ai.ped[w].stmtT > 0.f) stmtCop = i;
            }
            static int stmtLogged = -1;
            if (stmtCop >= 0 && stmtCop != stmtLogged) {
                stmtLogged = stmtCop;
                LOG("autoplay arrest: officer %d taking witness %d's statement (t=%.1f)", stmtCop, game.ai.ped[stmtCop].stmtWith, t);
            }
            bool thiefInCar = perpOk && game.peds[perp].state == PS_INVEHICLE;
            // the camera: on the thief (or the car they are in), 8 m off and 4.5 m up on a side with a clear view
            if (perpOk && (stage == 1 || stage == 3) && !(stmtCop >= 0 && thiefInCar)) {
                const Ped& q = game.peds[perp];
                vec3 P = q.state == PS_INVEHICLE && q.vehicle >= 0 ? game.vehicles[q.vehicle].sim.body.pos.toVec3() : q.pos.toVec3();
                auto clear = [&](vec2 o) {
                    vec3 cam = P + vec3(o, 4.5f);
                    return game.lineOfSight(dvec3(cam), dvec3(P + vec3(0.f, 0.f, 1.f)), perp, q.vehicle) &&
                           game.lineOfSight(dvec3(P + vec3(0.f, 0.f, 1.f)), dvec3(cam), perp, q.vehicle);
                };
                if (!clear(camOff)) {
                    float a0 = atan2f(camOff.y, camOff.x);
                    for (int k = 1; k < 8; k++) {
                        float a = a0 + (k & 1 ? 1.f : -1.f) * ((k + 1) / 2) * (kTwoPi / 8.f);
                        vec2 o = vec2(cosf(a), sinf(a)) * 8.5f;
                        if (clear(o)) {
                            camOff = o;
                            break;
                        }
                    }
                }
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(P + vec3(camOff, 4.5f));
                game.rig.scriptTarget = dvec3(P + vec3(0.f, 0.f, 0.9f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 40 && (doneT < 0.f || t < doneT + 4.f)) {
                    shotT = 6.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_arrest_%02d_s%d", shots, stage));
                    shots++;
                }
            } else if (stmtCop >= 0) {
                // the statement: from the side, 6 m off, both in the frame
                vec3 A = game.peds[stmtCop].pos.toVec3(), W = game.peds[game.ai.ped[stmtCop].stmtWith].pos.toVec3();
                vec3 M = (A + W) * 0.5f;
                vec2 side = AI::rightOf(normalize(W.xy() - A.xy() + vec2(1e-4f, 0.f)));
                vec3 cam = M + vec3(side * 5.5f, 2.2f);
                if (!game.lineOfSight(dvec3(cam), dvec3(M + vec3(0.f, 0.f, 1.f)), stmtCop, -1)) cam = M + vec3(-side * 5.5f, 2.2f);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cam);
                game.rig.scriptTarget = dvec3(M + vec3(0.f, 0.f, 1.1f));
                game.rig.scriptFov = 50.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 40) {
                    shotT = 5.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_arrest_%02d_stmt", shots));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                std::string ps = "none", cs = "none";
                if (perpOk) {
                    const Ped& q = game.peds[perp];
                    ps = StrFormat("%d state %d act %d brain %d stance %d veh %d", perp, (int)q.state, (int)game.ai.ped[perp].activity, (int)q.brain.type,
                                   q.animIn.stance, q.vehicle);
                }
                if (alive(cop, copUid)) {
                    const Ped& c = game.peds[cop];
                    const PedAI& ca = game.ai.ped[cop];
                    float d = perpOk ? length(rel(c.pos, game.peds[perp].pos)) : -1.f;
                    int ec = ca.escortCar;
                    float carD = ec >= 0 && ec < (int)game.vehicles.size() && game.vehicles[ec].used ? length(rel(game.vehicles[ec].sim.body.pos, c.pos)) : -1.f;
                    ps += StrFormat(" | cop %d brain %d target %d state %d %.1f m off, car %d (%.0f m, task %d transport %d)", cop, (int)c.brain.type, c.brain.target,
                                    (int)c.state, d, ec, carD, ec >= 0 && ec < (int)game.ai.veh.size() ? (int)game.ai.veh[ec].task : -1,
                                    ec >= 0 && ec < (int)game.ai.veh.size() ? (int)game.ai.veh[ec].transportState : -1);
                    // (the car on its way: its speed, the stop line and the car ahead, how long it has stood)
                    if (ec >= 0 && ec < (int)game.vehicles.size() && game.vehicles[ec].used)
                        if (const AI::Driver* dr = game.traffic.get(ec))
                            ps += StrFormat(" [v %.1f mode %d stop %.0f obst %.0f stood %.0f]", game.vehicles[ec].sim.speed(), (int)dr->mode,
                                            Min(dr->stopDist, 999.f), Min(dr->obstDist, 999.f), ec < (int)game.ai.veh.size() ? game.ai.veh[ec].stopTimer : 0.f);
                }
                // (the officer still in their car: how the car is doing - speed, lane mode, held up, sight of them)
                if (alive(cop, copUid) && game.peds[cop].state == PS_INVEHICLE && game.peds[cop].vehicle >= 0) {
                    int cv = game.peds[cop].vehicle;
                    const AI::Driver* dr = game.traffic.get(cv);
                    ps += StrFormat(" [unit %d v %.1f mode %d held %.1f sight %.1f]", cv, game.vehicles[cv].sim.speed(), dr ? (int)dr->mode : -1,
                                    cv < (int)game.ai.veh.size() ? game.ai.veh[cv].heldUp : -1.f, cv < (int)game.ai.veh.size() ? game.ai.veh[cv].taskTimer : -1.f);
                }
                (void)cs;
                LOG("autoplay arrest t=%.1f stage %d | thief %s | %s", t, stage, ps.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "life") {
            // the camera turning slowly round the player; the census every 10 s (the totals: meetings, the way asked,
            // sidewalk stops, coffee breaks, statements, events, couples hand in hand at that moment)
            static float logT = 0.f, shotT = 5.f;
            static int shots = 0;
            Ped* pl = game.playerPed();
            if (pl) {
                vec3 P = pl->pos.toVec3();
                float a = t * 0.05f;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(P + vec3(cosf(a) * 9.f, sinf(a) * 9.f, 4.f));
                game.rig.scriptTarget = dvec3(P + vec3(-cosf(a) * 12.f, -sinf(a) * 12.f, 1.f));
                game.rig.scriptFov = 60.f;
            }
            shotT -= dt;
            if (shotT <= 0.f && shots < 20) {
                shotT = 10.f;
                game.requestScreenshot = shotPath(StrFormat("auto_life_%02d", shots));
                shots++;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 10.f;
                LOG("autoplay life t=%.1f | meets %d | %s", t, game.ai.meetsStarted, game.aiCensusText(60.f).c_str());
            }
        } else if (autoplay == "copbreak") {
            // 3 s in, the next patrol close by is to take its break (the test patrol, or another): the camera on the car
            // on a break (or the test car until one is), the car's state and its crew every 2 s
            static float logT = 0.f, shotT = 2.f;
            static int shots = 0, car = -1;
            static vec2 camOff(-7.f, 7.f);
            if (t > 3.f && t - dt <= 3.f) game.ai.forceCopBreak = true;
            int brk = -1;
            for (int i = 0; i < (int)game.vehicles.size() && i < (int)game.ai.veh.size(); i++)
                if (game.vehicles[i].used && game.vehicles[i].faction == FAC_POLICE && game.ai.veh[i].copBreak != 0) brk = i;
            if (brk >= 0) car = brk;
            int c = car >= 0 ? car : game.ai.testCar[0];
            if (c >= 0 && c < (int)game.vehicles.size() && game.vehicles[c].used) {
                const Vehicle& v = game.vehicles[c];
                vec3 P = v.sim.body.pos.toVec3();
                auto clear = [&](vec2 o) {
                    vec3 cam = P + vec3(o, 3.5f);
                    return game.lineOfSight(dvec3(cam), dvec3(P + vec3(0.f, 0.f, 1.f)), -1, c) && game.lineOfSight(dvec3(P + vec3(0.f, 0.f, 1.f)), dvec3(cam), -1, c);
                };
                if (!clear(camOff)) {
                    float a0 = atan2f(camOff.y, camOff.x);
                    for (int k = 1; k < 8; k++) {
                        float a = a0 + (k & 1 ? 1.f : -1.f) * ((k + 1) / 2) * (kTwoPi / 8.f);
                        vec2 o = vec2(cosf(a), sinf(a)) * 9.f;
                        if (clear(o)) {
                            camOff = o;
                            break;
                        }
                    }
                }
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(P + vec3(camOff, 3.5f));
                game.rig.scriptTarget = dvec3(P + vec3(0.f, 0.f, 0.8f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 40) {
                    shotT = 5.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_copbreak_%02d_b%d", shots, c < (int)game.ai.veh.size() ? (int)game.ai.veh[c].copBreak : -1));
                    shots++;
                }
                logT -= dt;
                if (logT <= 0.f) {
                    logT = 2.f;
                    const AI::Driver* dr = game.traffic.get(c);
                    std::string crew;
                    for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                        const Ped& q = game.peds[i];
                        if (!q.used || q.faction != FAC_POLICE || game.ai.ped[i].uid != q.uid || game.ai.ped[i].homeVeh != c) continue;
                        crew += StrFormat(" [%d state %d act %d brain %d carry %d stance %d %.1f m]", i, (int)q.state, (int)game.ai.ped[i].activity, (int)q.brain.type,
                                          (int)q.carry, q.animIn.stance, length(rel(q.pos, v.sim.body.pos)));
                    }
                    LOG("autoplay copbreak t=%.1f | car %d break %d parked %d speed %.1f mode %d driver %d |%s", t, c, c < (int)game.ai.veh.size() ? (int)game.ai.veh[c].copBreak : -1,
                        (int)v.parked, v.sim.speed(), dr ? (int)dr->mode : -1, v.seats[0], crew.c_str());
                }
            } else {
                game.rig.scriptActive = false;
            }
        } else if (autoplay == "pedstop") {
            // stage k = 1, 2, 3 (at 4 s, 70 s, 150 s): a pair of officers put on a sidewalk 20-30 m off walking their beat,
            // and the next stop forced with outcome k - 1 (police_stop: let go, a warrant, a run); the camera beside
            // whoever is doing the talking (then the escort or the chase), the state every 2 s
            static int stage = 0, focus = -1;
            static u32 focusUid = 0;
            static float logT = 0.f, shotT = 2.f, nextAt = 4.f;
            static int shots = 0;
            static vec2 camOff(6.f, -6.f);
            Ped* pl = game.playerPed();
            const int np = Min((int)game.peds.size(), (int)game.ai.ped.size());
            if (stage < 3 && t > nextAt && pl && game.ai.forceStop < 0) {
                vec2 pp = pl->pos.toVec3().xy();
                bool placed = false;
                for (int k = 0; k < 12 && !placed; k++) {
                    float ang = (float)(stage * 5 + k) * 2.4f;
                    vec2 probe = pp + vec2(cosf(ang), sinf(ang)) * 24.f;
                    float x = 0.f;
                    int wl = game.laneGraph.nearestWalk(probe, 12.f, &x);
                    if (wl < 0 || game.laneGraph.walkLinks[wl].kind != AI::WL_SIDEWALK) continue;
                    vec3 a3 = game.laneGraph.walkPos(wl, x, 0.f, true);
                    vec2 tdir = game.laneGraph.walkTangent(wl, x, true);
                    int ids[2] = {-1, -1};
                    for (int m = 0; m < 2; m++) {
                        vec2 at = a3.xy() + AI::rightOf(tdir) * (m == 0 ? 0.f : 0.85f);
                        int id = game.spawnPed(game.randomCivilianChar(hash32((u32)stage * 77u + (u32)m * 13u + 5u), 1),
                                               dvec3(at.x, at.y, game.groundHeight(at.x, at.y, a3.z + 1.5f)), AI::dirYaw(tdir), FAC_POLICE);
                        if (id < 0) break;
                        ids[m] = id;
                        game.giveWeapon(id, WPN_PISTOL, 60);
                        game.peds[id].weapon = WPN_FISTS;
                        game.peds[id].brain.type = BRAIN_WANDER;
                        game.peds[id].brain.edge = -1;
                        game.peds[id].brain.accuracy = 0.45f;
                        PedAI& qa = game.pedAI(id);
                        qa.role = PR_COP;
                        qa.activity = ACT_WALK;
                        qa.temper = 2;
                        qa.homeVeh = -1;
                        if (m == 1) {
                            qa.leader = ids[0];
                            qa.leaderUid = game.peds[ids[0]].uid;
                            qa.slot = vec2(0.85f, 0.f);
                            qa.actTimer = 1e4f;
                        }
                    }
                    if (ids[0] < 0) continue;
                    placed = true;
                    LOG("autoplay pedstop: stage %d at t=%.1f - officers %d and %d on the beat at %.0f %.0f, forcing outcome %d", stage + 1, t, ids[0], ids[1],
                        a3.x, a3.y, stage);
                }
                game.ai.forceStop = stage;
                stage++;
                nextAt = t + (stage == 1 ? 66.f : 80.f);
            }
            // the focus: the officer doing the talking, else one walking a prisoner, else one chasing somebody on foot
            if (focus < 0 || focus >= np || !game.peds[focus].used || game.peds[focus].uid != focusUid ||
                !(police_stop::stopping(game.peds[focus]) || police_escort::escorting(game.peds[focus]) || game.peds[focus].brain.type == BRAIN_COMBAT)) {
                focus = -1;
                for (int pass = 0; pass < 3 && focus < 0; pass++)
                    for (int i = 0; i < np && focus < 0; i++) {
                        const Ped& q = game.peds[i];
                        if (!q.used || q.faction != FAC_POLICE || q.state != PS_ONFOOT || game.ai.ped[i].uid != q.uid) continue;
                        bool want = pass == 0 ? police_stop::stopping(q) && !game.ai.ped[i].stopCover
                                              : (pass == 1 ? police_escort::escorting(q) : q.brain.type == BRAIN_COMBAT && q.brain.target >= 0 && q.brain.target != game.player);
                        if (want) {
                            focus = i;
                            focusUid = q.uid;
                        }
                    }
            }
            if (focus >= 0) {
                const Ped& c = game.peds[focus];
                vec3 P = c.pos.toVec3();
                int other = police_stop::stopping(c) ? game.ai.ped[focus].stopPed : (police_escort::escorting(c) ? game.ai.ped[focus].escortPed : c.brain.target);
                if (other >= 0 && other < np && game.peds[other].used) P = (P + game.peds[other].pos.toVec3()) * 0.5f;
                // (from the side of the two of them where there is a view: square to the line between them first)
                if (other >= 0 && other < np && game.peds[other].used && length2(camOff) > 1e-3f) {
                    vec2 ln = game.peds[other].pos.toVec3().xy() - c.pos.toVec3().xy();
                    if (length2(ln) > 0.04f) {
                        vec2 sq = AI::rightOf(normalize(ln)) * 5.5f;
                        if (dot(sq, camOff) < 0.f) sq = -sq;
                        camOff = sq;
                    }
                }
                auto clear = [&](vec2 o) {
                    vec3 cam = P + vec3(o, 2.4f);
                    return game.lineOfSight(dvec3(cam), dvec3(P + vec3(0.f, 0.f, 1.f)), focus, -1) && game.lineOfSight(dvec3(P + vec3(0.f, 0.f, 1.f)), dvec3(cam), focus, -1);
                };
                if (!clear(camOff)) {
                    float a0 = atan2f(camOff.y, camOff.x);
                    for (int k = 1; k < 8; k++) {
                        float a = a0 + (k & 1 ? 1.f : -1.f) * ((k + 1) / 2) * (kTwoPi / 8.f);
                        vec2 o = vec2(cosf(a), sinf(a)) * 5.5f;
                        if (clear(o)) {
                            camOff = o;
                            break;
                        }
                    }
                }
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(P + vec3(camOff, 2.4f));
                game.rig.scriptTarget = dvec3(P + vec3(0.f, 0.f, 1.f));
                game.rig.scriptFov = 50.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 45) {
                    shotT = 4.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_pedstop_%02d_s%d", shots, stage));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                std::string fs = "focus none";
                if (focus >= 0) {
                    const Ped& c = game.peds[focus];
                    const PedAI& ca = game.ai.ped[focus];
                    int s2 = police_stop::stopping(c) ? ca.stopPed : (police_escort::escorting(c) ? ca.escortPed : c.brain.target);
                    fs = StrFormat("officer %d brain %d target %d stance %d device %d", focus, (int)c.brain.type, c.brain.target, c.animIn.stance, (int)c.phoneBrowse);
                    if (s2 >= 0 && s2 < np && game.peds[s2].used) {
                        const Ped& q = game.peds[s2];
                        const PedAI& qa = game.ai.ped[s2];
                        fs += StrFormat(" | ped %d %.1f m off, state %d act %d brain %d stance %d clock %.1f id %d", s2, length(rel(q.pos, c.pos)), (int)q.state,
                                        (int)qa.activity, (int)q.brain.type, q.animIn.stance, qa.stopT, (int)q.phoneBrowse);
                    }
                }
                LOG("autoplay pedstop t=%.1f stage %d | %s | %s", t, stage, fs.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "hurt") {
            // 6 s in: the nearest passer-by knocked flat at very low health; the camera on them from then on
            static int victim = -1;
            static u32 victimUid = 0;
            static float logT = 0.f, shotT = 0.f;
            static int shots = 0;
            Ped* pl = game.playerPed();
            if (victim < 0 && t > 6.f && pl) {
                float best = 25.f;
                for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                    const Ped& q = game.peds[i];
                    if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || game.ai.ped[i].uid != q.uid ||
                        game.ai.ped[i].activity != ACT_WALK)
                        continue;
                    float d = length(rel(q.pos, pl->pos));
                    if (d < best) {
                        best = d;
                        victim = i;
                    }
                }
                if (victim >= 0) {
                    Ped& v = game.peds[victim];
                    victimUid = v.uid;
                    v.health = v.maxHealth * 0.15f;
                    v.legInjury = 25.f;
                    vec2 side = AI::rightOf(AI::yawDir(v.yaw));
                    game.knockDown(victim, vec3(side * 260.f, 40.f));
                    LOG("autoplay hurt: ped %d knocked down at %.0f %.0f (%.0f m from the player)", victim, v.pos.x, v.pos.y, best);
                }
            }
            bool ok = victim >= 0 && victim < (int)game.peds.size() && game.peds[victim].used && game.peds[victim].uid == victimUid;
            if (ok) {
                const Ped& v = game.peds[victim];
                vec2 vp = v.pos.toVec3().xy();
                vec2 cam = vp + vec2(4.5f, -4.5f);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cam.x, cam.y, v.pos.z + 2.6f);
                game.rig.scriptTarget = dvec3(vp.x, vp.y, v.pos.z + 0.5f);
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 16) {
                    shotT = 6.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_hurt_%02d", shots));
                    shots++;
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                std::string st = "none";
                if (ok) {
                    const Ped& v = game.peds[victim];
                    const PedAI& va = game.ai.ped[victim];
                    int medics = 0;
                    for (const Ped& q : game.peds) medics += q.used && q.faction == FAC_MEDIC && q.state == PS_ONFOOT && length(rel(q.pos, v.pos)) < 3.f;
                    st = StrFormat("victim state %d act %d stance %d care %.1f health %.0f/%.0f medics by %d", (int)v.state, (int)va.activity, v.animIn.stance, va.hurtCare,
                                   v.health, v.maxHealth, medics);
                    // the ambulances: how far from the scene, how fast, what they are doing
                    for (int k = 0; k < (int)game.vehicles.size() && k < (int)game.ai.veh.size(); k++) {
                        const Vehicle& amb = game.vehicles[k];
                        if (!amb.used || game.ai.veh[k].uid != amb.uid || game.ai.veh[k].role != VR_AMBULANCE) continue;
                        const AI::Driver* dr = game.traffic.get(k);
                        st += StrFormat(" | ambulance %d %.0f m off, %.1f m/s, task %d, mode %d, held %.1f", k, length(rel(amb.sim.body.pos, v.pos).xy()), amb.sim.speed(),
                                        (int)game.ai.veh[k].task, dr ? (int)dr->mode : -1, game.ai.veh[k].heldUp);
                    }
                } else if (victim >= 0) {
                    st = "victim gone (into the ambulance?)";
                }
                LOG("autoplay hurt t=%.1f | %s | %s", t, st.c_str(), game.aiCensusText(80.f).c_str());
            }
        } else if (autoplay == "places") {
            // the named places' own people (sites.cpp anchors, population.cpp place venues): the Ocean Promenade terraces
            // at lunch, the campus quad in the morning, runners on Tarpon Field in the evening, the cemetery in the
            // afternoon - the player on a path there, two looks each at the busiest groups of the place's people (a
            // scripted camera 12 m off with a clear view of them), a census at each
            static int stop = -1;
            static float stopT = 0.f;
            static int shots = 0;
            static int aimed = -1;
            static vec2 camAt[2], tgtAt[2];
            struct PlaceStop {
                const char* name;
                vec2 player;
                float hour;
            };
            const PlaceStop stops[4] = {{"promenade", {5347.f, 686.f}, 13.f},
                                        {"campus_quad", {1905.f, 3700.f}, 11.f},
                                        {"track", {2062.f, 3625.f}, 17.5f},
                                        {"cemetery", {1572.f, -735.f}, 15.f}};
            int want = Min((int)(t / 20.f), 3);
            Ped* pl = game.playerPed();
            if (want != stop && pl) {
                stop = want;
                stopT = 0.f;
                shots = 0;
                const PlaceStop& st = stops[stop];
                if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
                pl->pos = dvec3(st.player.x, st.player.y, game.groundHeight(st.player.x, st.player.y, 40.f));
                pl->vel = vec3(0.f);
                env.timeOfDay = st.hour;
                weather.setImmediate(WX_FAIR);
                game.populationWarmup = 2.5f;
                game.pinfo.wanted = 0;
                aimed = -1;
                camAt[0] = camAt[1] = st.player - vec2(0.f, 3.f);
                tgtAt[0] = tgtAt[1] = st.player + vec2(0.f, 20.f);
                LOG("autoplay places stop %d %s at %.0f %.0f, %.1f h", stop, st.name, st.player.x, st.player.y, st.hour);
            }
            stopT += dt;
            if (stop >= 0 && aimed < 0 && stopT > 5.f) {
                // the place's people round the stop (venue slots), the two busiest groups (the most others within 9 m,
                // the second one 22 m or more from the first); for each a camera 12 m off in the first of twelve
                // directions (from the stop's side round) with nothing in the way
                aimed = 1;
                const PlaceStop& st = stops[stop];
                std::vector<vec2> at;
                for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                    const Ped& q = game.peds[i];
                    if (!q.used || q.isPlayer || game.ai.ped[i].uid != q.uid || game.ai.ped[i].activity != ACT_VENUE) continue;
                    vec2 qp = q.pos.toVec3().xy();
                    if (length(qp - st.player) < 110.f) at.push_back(qp);
                }
                vec2 cen[2] = {st.player, st.player};
                int got = 0;
                for (int k = 0; k < 2; k++) {
                    int best = -1, bestN = 0;
                    for (size_t a = 0; a < at.size(); a++) {
                        if (k == 1 && got > 0 && length(at[a] - cen[0]) < 22.f) continue;
                        int n = 0;
                        for (size_t b = 0; b < at.size(); b++) n += length(at[b] - at[a]) < 9.f;
                        if (n > bestN) {
                            bestN = n;
                            best = (int)a;
                        }
                    }
                    if (best < 0) {
                        cen[k] = cen[0];
                        continue;
                    }
                    vec2 sum(0.f);
                    int n = 0;
                    for (size_t b = 0; b < at.size(); b++)
                        if (length(at[b] - at[best]) < 9.f) {
                            sum = sum + at[b];
                            n++;
                        }
                    cen[k] = sum * (1.f / (float)n);
                    got++;
                    LOG("autoplay places %s group %d: %d people round %.0f %.0f", st.name, k, n, cen[k].x, cen[k].y);
                }
                for (int k = 0; k < 2; k++) {
                    vec2 c0 = cen[k];
                    double tz = game.groundHeight(c0.x, c0.y, (float)pl->pos.z + 8.f) + 1.2;
                    vec2 toStop = st.player - c0;
                    float a0 = length(toStop) > 1.f ? atan2f(toStop.y, toStop.x) : 0.f;
                    if (k == 1 && got < 2) a0 += kPi;   // (only one group: the other side of it)
                    tgtAt[k] = c0;
                    camAt[k] = c0 + vec2(cosf(a0), sinf(a0)) * 12.f;
                    for (int j = 0; j < 12; j++) {
                        float a = a0 + (j & 1 ? 1.f : -1.f) * (float)((j + 1) / 2) * (kTwoPi / 12.f);
                        vec2 cp = c0 + vec2(cosf(a), sinf(a)) * 12.f;
                        double cz = game.groundHeight(cp.x, cp.y, (float)tz + 6.f) + 2.4;
                        if (game.lineOfSight(dvec3(cp.x, cp.y, cz), dvec3(c0.x, c0.y, tz), -1, -1)) {
                            camAt[k] = cp;
                            break;
                        }
                    }
                }
            }
            if (stop >= 0) {
                const PlaceStop& st = stops[stop];
                int view = stopT < 12.f ? 0 : 1;
                vec2 cp = camAt[view], tp = tgtAt[view];
                float cz = game.groundHeight(cp.x, cp.y, pl ? (float)pl->pos.z + 6.f : 40.f), tz = game.groundHeight(tp.x, tp.y, pl ? (float)pl->pos.z + 6.f : 40.f);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cp.x, cp.y, cz + 2.2f);
                game.rig.scriptTarget = dvec3(tp.x, tp.y, tz + 1.1f);
                game.rig.scriptFov = 50.f;
                const float at[2] = {7.f, 17.f};
                if (shots < 2 && stopT > at[shots]) {
                    game.requestScreenshot = shotPath(StrFormat("auto_places_%s_%d", st.name, shots));
                    LOG("autoplay places %s shot %d | %s", st.name, shots, game.aiCensusText(120.f).c_str());
                    shots++;
                }
            }
        } else if (autoplay == "greet") {
            // greetings (population.cpp greet*: a hug, a kiss on the cheek or a handshake): at the airport curb as the one
            // coming out reaches the driver waiting for them, or as a traveler says goodbye before going in; then on a
            // downtown sidewalk, acquaintances running into each other (pedai.cpp aiStreetMeets, made more frequent for the
            // test). The camera goes to each greeting as it starts, from the side at chest height, two shots each (the
            // contact, the hold), then back to the player to wait for the next
            static int stop = -1;
            static int ga = -1, gb = -1, seen = 0, gshots = 0;
            static float gT = 0.f, logT = 0.f;
            static vec2 gSide(1.f, 0.f);
            static float gAlong = 0.8f;
            Ped* pl = game.playerPed();
            int want = t < 35.f ? 0 : 1;
            if (want != stop && pl) {
                stop = want;
                vec3 pos;
                if (stop == 0) {
                    mu::computePlaces(game);
                    pos = mu::gPlaces.airport.pos;
                } else {
                    pos = vec3(2713.f, 763.f, 20.f);   // (the police tests' downtown corner)
                    game.ai.meetBoost = 5.f;
                    game.ai.meetGap = 3.f;
                }
                if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
                pl->pos = dvec3(pos.x, pos.y, game.groundHeight(pos.x, pos.y, pos.z + 2.f));
                pl->vel = vec3(0.f);
                env.timeOfDay = stop == 0 ? 11.f : 13.f;
                weather.setImmediate(WX_FAIR);
                game.populationWarmup = 2.5f;
                game.pinfo.wanted = 0;
                ga = gb = -1;
                game.rig.scriptActive = false;
                LOG("autoplay greet stop %d: %s at %.0f %.0f", stop, stop == 0 ? "the airport forecourt" : "a downtown sidewalk", pos.x, pos.y);
            }
            auto greeting = [&](int i) {
                return i >= 0 && i < (int)game.peds.size() && i < (int)game.ai.ped.size() && game.peds[i].used &&
                       game.ai.ped[i].uid == game.peds[i].uid && game.ai.ped[i].greetT > 0.f;
            };
            if (ga < 0 && pl && seen < 6) {
                for (int i = 0; i < (int)game.peds.size(); i++) {
                    if (!greeting(i) || !greeting(game.ai.ped[i].greetWith)) continue;
                    if (length(rel(game.peds[i].pos, pl->pos).xy()) > 260.f) continue;   // (the whole curb, the east plaza)
                    ga = i;
                    gb = game.ai.ped[i].greetWith;
                    gT = 0.f;
                    gshots = 0;
                    seen++;
                    // the camera's side: the player's side first, then round - whichever has a clear view of the two (no
                    // tree trunk, post or wall in between)
                    {
                        vec3 A = game.peds[ga].pos.toVec3(), B = game.peds[gb].pos.toVec3();
                        vec3 mid = (A + B) * 0.5f;
                        vec2 d = normalize(B.xy() - A.xy() + vec2(1e-4f, 0.f));
                        vec2 side(-d.y, d.x);
                        if (dot(rel(pl->pos, game.peds[ga].pos).xy(), side) < 0.f) side = -side;
                        const float alongs[3] = {0.8f, -0.8f, 2.2f};
                        gSide = side;
                        gAlong = 0.8f;
                        bool found = false;
                        for (int sgn = 0; sgn < 2 && !found; sgn++)
                            for (int k = 0; k < 3 && !found; k++) {
                                vec2 sd = sgn ? -side : side;
                                vec3 cam = mid + vec3(sd * 3.4f + d * alongs[k], 1.55f);
                                if (game.lineOfSight(dvec3(cam), dvec3(mid + vec3(0.f, 0.f, 1.3f)), ga, -1) &&
                                    game.lineOfSight(dvec3(cam), dvec3(mid + vec3(0.f, 0.f, 0.6f)), ga, -1)) {
                                    gSide = sd;
                                    gAlong = alongs[k];
                                    found = true;
                                }
                            }
                    }
                    LOG("autoplay greet %d: peds %d and %d, clip %d, %.1f s", seen, ga, gb, game.peds[ga].anim.action, game.ai.ped[ga].greetT);
                    break;
                }
            }
            if (ga >= 0) {
                gT += dt;
                if (!game.peds[ga].used || !game.peds[gb].used || gT > 4.f) {
                    ga = gb = -1;
                    game.rig.scriptActive = false;
                } else {
                    vec3 A = game.peds[ga].pos.toVec3(), B = game.peds[gb].pos.toVec3();
                    vec3 mid = (A + B) * 0.5f;
                    vec2 d = normalize(B.xy() - A.xy() + vec2(1e-4f, 0.f));
                    game.rig.scriptActive = true;
                    game.rig.scriptPos = dvec3(mid + vec3(gSide * 3.4f + d * gAlong, 1.55f));
                    game.rig.scriptTarget = dvec3(mid + vec3(0.f, 0.f, 1.25f));
                    game.rig.scriptFov = 40.f;
                    const float at[2] = {1.2f, 2.3f};
                    if (gshots < 2 && gT > at[gshots]) {
                        game.requestScreenshot = shotPath(StrFormat("auto_greet_%d_%d", seen, gshots));
                        LOG("autoplay greet %d shot %d | clip %d t %.2f | %.2f m apart", seen, gshots, game.peds[ga].anim.action,
                            game.ai.ped[ga].greetT, length(B.xy() - A.xy()));
                        gshots++;
                    }
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 5.f;
                LOG("autoplay greet t=%.0f seen %d | %s", t, seen, game.aiCensusText(140.f).c_str());
            }
        } else if (autoplay == "venues") {
            // the places with a working crowd of their own, at the scorecard tour's stops, hours and weather: the tour's own
            // view first (the player at the tour place, the same camera, a few slow steps), then two closer looks (scripted
            // camera), a census of the venue crowd at each shot
            static int stop = -1;
            static float stopT = 0.f;
            static int shots = 0;
            mu::computePlaces(game);
            const mu::Places& P = mu::gPlaces;
            struct VenueStop {
                const char* name;
                const mu::Place* pl;
                float hour;
                WeatherKind wx;
                float yawOff;
                vec2 cam[2], tgt[2];   // two closer looks (the port: the gate booths, the holding area; the airport: the
                                       // rank, the terminal curb; Sawgrass: the anglers, the airboat landing)
            };
            const VenueStop stops[3] = {
                {"port_gate", &P.portGate, 8.5f, WX_OVERCAST, 0.6f, {{4025.f, -188.f}, {4021.f, -204.f}}, {{4009.f, -175.f}, {4004.f, -218.f}}},
                {"airport_forecourt", &P.airport, 11.f, WX_FAIR, 0.6f, {{752.5f, 1204.f}, {716.f, 1352.f}}, {{749.5f, 1238.f}, {694.f, 1334.f}}},
                {"sawgrass", &P.sawgrassRoad, 7.2f, WX_FOG, 0.5f, {{-5052.f, 104.5f}, {-4990.f, -56.f}}, {{-5028.f, 101.f}, {-5000.f, -72.f}}}};
            int want = Min((int)(t / 24.f), 2);
            Ped* pl = game.playerPed();
            if (want != stop && pl) {
                stop = want;
                stopT = 0.f;
                shots = 0;
                const VenueStop& st = stops[stop];
                if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
                // (as the tour places the player: on the sidewalk facing along the street, the camera turned toward
                //  the frontage side)
                vec3 pos = st.pl->pos;
                pl->pos = dvec3(pos.x, pos.y, game.groundHeight(pos.x, pos.y, pos.z + 2.f));
                pl->vel = vec3(0.f);
                pl->yaw = atan2f(-st.pl->streetDir.x, st.pl->streetDir.y);
                vec2 sd = st.pl->streetDir, left(-sd.y, sd.x);
                float side = dot(left, st.pl->outward) >= 0.f ? 1.f : -1.f;
                game.rig.yaw = pl->yaw + st.yawOff * side;
                game.rig.pitch = -0.1f;
                game.rig.cut = true;
                game.rig.scriptActive = false;
                env.timeOfDay = st.hour;
                weather.setImmediate(st.wx);
                game.populationWarmup = 2.5f;
                game.pinfo.wanted = 0;
                LOG("autoplay venues stop %d %s at %.0f %.0f, %.1f h", stop, st.name, pos.x, pos.y, st.hour);
            }
            stopT += dt;
            if (stop >= 0) {
                const VenueStop& st = stops[stop];
                int view = stopT < 10.f ? 0 : (stopT < 17.f ? 1 : 2);
                // (the airport's last look: at a pair at the terminal curb - a goodbye, a pick-up - from along the walk)
                static bool curbAim = false;
                static vec2 curbCam, curbTgt;
                if (view < 2) curbAim = false;
                if (view == 2 && stop == 1 && !curbAim && pl) {
                    curbAim = true;
                    curbCam = st.cam[1];
                    curbTgt = st.tgt[1];
                    float best = 1e9f;
                    for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                        const Ped& q = game.peds[i];
                        const PedAI& qa = game.ai.ped[i];
                        if (!q.used || qa.uid != q.uid || qa.activity != ACT_VENUE) continue;
                        if (qa.venueMode != VM_FAREWELL && qa.venueMode != VM_SEEOFF && qa.venueMode != VM_MEET) continue;
                        float d = length(rel(q.pos, pl->pos).xy());
                        if (d < best) {
                            best = d;
                            curbTgt = q.pos.toVec3().xy() + vec2(0.4f, 0.f);
                            curbCam = curbTgt + vec2(-2.2f, 6.5f);
                        }
                    }
                    LOG("autoplay venues: the curb look at %.0f %.0f (a pair %.0f m off)", curbTgt.x, curbTgt.y, best);
                }
                if (view == 0) {
                    c.move = vec2(0.f, stopT > 3.f && stopT < 6.f ? 0.3f : 0.f);   // (the tour's few slow steps)
                } else {
                    vec2 cp = st.cam[view - 1], tp = st.tgt[view - 1];
                    if (view == 2 && stop == 1) cp = curbCam, tp = curbTgt;
                    float cz = game.groundHeight(cp.x, cp.y, pl ? (float)pl->pos.z + 3.f : 30.f), tz = game.groundHeight(tp.x, tp.y, pl ? (float)pl->pos.z + 3.f : 30.f);
                    game.rig.scriptActive = true;
                    game.rig.scriptPos = dvec3(cp.x, cp.y, cz + 1.8f);
                    game.rig.scriptTarget = dvec3(tp.x, tp.y, tz + 1.2f);
                    game.rig.scriptFov = 45.f;
                }
                const float at[3] = {7.5f, 14.5f, 21.5f};
                if (shots < 3 && stopT > at[shots]) {
                    game.requestScreenshot = shotPath(StrFormat("auto_venues_%s_%d", st.name, shots));
                    LOG("autoplay venues %s shot %d | %s", st.name, shots, game.aiCensusText(140.f).c_str());
                    shots++;
                }
            }
        } else if (autoplay == "bender") {
            // roll the inattentive driver into the waiting car, then watch the scene play out
            static bool released = false;
            static float shotAt[6] = {1.f, 3.f, 6.f, 12.f, 25.f, 45.f};
            static int shotIdx = 0;
            static float hitT = -1.f, logT = 0.f;
            int a = game.ai.testCar[0], b = game.ai.testCar[1];
            bool okA = a >= 0 && game.vehicles[a].used, okB = b >= 0 && game.vehicles[b].used;
            if (okA && okB && !released) {
                Vehicle& vb = game.vehicles[b];
                vb.ctl = Vehicles::VehicleControls();
                vb.ctl.hasDriver = vb.seats[0] >= 0;
                float gap = length(rel(vb.sim.body.pos, game.vehicles[a].sim.body.pos)) - game.vassets[vb.model].spec.boxHalf.y -
                            game.vassets[game.vehicles[a].model].spec.boxHalf.y;
                vb.ctl.throttle = vb.sim.speed() < 4.5f ? 0.45f : 0.f;
                // (released on the knock itself - the impulse, or the speed it lost in one frame: the impact check in the
                //  AI update then sees two ordinary drivers)
                static float lastSpeed = 0.f;
                float drop = lastSpeed - vb.sim.speed();
                lastSpeed = vb.sim.speed();
                if (vb.sim.impactImpulse > 800.f || drop > 0.8f || t > 16.f) {
                    released = true;
                    if (vb.seats[0] >= 0) game.peds[vb.seats[0]].brain.type = BRAIN_DRIVER;
                    hitT = t;
                    LOG("autoplay bender: contact at t=%.1f gap %.2f speed %.1f impulse %.0f", t, gap, vb.sim.speed(), vb.sim.impactImpulse);
                }
            }
            if (okA && okB) {
                vec3 mid = (game.vehicles[a].sim.body.pos.toVec3() + game.vehicles[b].sim.body.pos.toVec3()) * 0.5f;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(game.ai.testCam);
                game.rig.scriptTarget = dvec3(mid + vec3(0.f, 0.f, 0.7f));
                game.rig.scriptFov = 45.f;
            }
            if (hitT >= 0.f && shotIdx < 6 && t - hitT >= shotAt[shotIdx]) {
                game.requestScreenshot = shotPath(StrFormat("auto_bender_%02d_%.0fs", shotIdx, shotAt[shotIdx]));
                shotIdx++;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                std::string who;
                for (int c : {a, b}) {
                    if (c < 0 || !game.vehicles[c].used) continue;
                    const Vehicle& v = game.vehicles[c];
                    who += StrFormat(" | car %d speed %.1f parked %d hazard %d role %d driver %d", c, v.sim.speed(), (int)v.parked, (int)(v.indicator == 2), (int)game.vehAI(c).role, v.seats[0]);
                }
                for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size(); i++) {
                    const Ped& pp = game.peds[i];
                    if (!pp.used || game.ai.ped[i].uid != pp.uid || game.ai.ped[i].eventId < 0) continue;
                    who += StrFormat(" | ped %d act %d brain %d state %d at %.1f %.1f", i, (int)game.ai.ped[i].activity, (int)pp.brain.type, (int)pp.state, pp.pos.x, pp.pos.y);
                }
                LOG("autoplay bender t=%.1f%s", t, who.c_str());
            }
        } else if (autoplay == "parking") {
            // follow one owner / arriving car at a time from the sidewalk, a shot at each step
            static int trackVeh = -1, trackPed = -1, phase = -1, shots = 0;
            static bool arriving = false;
            static float trackT = 0.f, logT = 0.f, doneT = 0.f;
            Ped* pl = game.playerPed();
            static vec3 camAt;
            auto release = [&](const char* why) {
                if (trackVeh >= 0) LOG("autoplay parking: done with car %d (%s) after %.1f s", trackVeh, why, trackT);
                trackVeh = trackPed = phase = -1;
                game.rig.scriptActive = false;
            };
            if (trackVeh >= 0 && (!game.vehicles[trackVeh].used || trackT > 60.f)) release("timeout");
            if (trackVeh < 0 && pl) {
                // (arrivals first: rarer than owners driving off)
                for (int i = 0; i < (int)game.vehicles.size() && trackVeh < 0; i++)
                    if (game.vehicles[i].used && i < (int)game.ai.veh.size() && game.ai.veh[i].uid == game.vehicles[i].uid && game.ai.veh[i].parking == 1) {
                        trackVeh = i;
                        trackPed = game.vehicles[i].seats[0];
                        arriving = true;
                    }
                for (int i = 0; i < (int)game.peds.size() && i < (int)game.ai.ped.size() && trackVeh < 0; i++)
                    if (game.peds[i].used && game.ai.ped[i].uid == game.peds[i].uid && game.ai.ped[i].activity == ACT_DRIVE_OFF && game.ai.ped[i].homeVeh >= 0) {
                        trackVeh = game.ai.ped[i].homeVeh;
                        trackPed = i;
                        arriving = false;
                    }
                if (trackVeh >= 0) {
                    trackT = doneT = 0.f;
                    phase = -1;
                    // stand on the sidewalk a little behind the car
                    const Vehicle& v = game.vehicles[trackVeh];
                    vec3 cp = v.sim.body.pos.toVec3();
                    vec2 f = v.sim.forward().xy();
                    f = length2(f) > 1e-6f ? normalize(f) : vec2(0, 1);
                    // a fixed camera on the curb side a little behind the car (arriving: further back, it is still
                    // rolling up), the player out of shot behind it (population and streaming stay centred here)
                    float lu = 0.f, llat = 0.f;
                    int lane = game.laneGraph.nearestLane(cp.xy(), f, 8.f, &lu, &llat);
                    float side = 1.f;
                    if (lane >= 0) {
                        const AI::Lane& L = game.laneGraph.lanes[lane];
                        side = L.width * 0.5f + World::roadInfo((World::RoadClass)L.cls).shoulder + 1.2f - llat;   // to the sidewalk
                    }
                    side = Clamp(side, 2.5f, 6.f);
                    vec2 cam = cp.xy() - f * (arriving ? 16.f : 8.f) + AI::rightOf(f) * side;
                    camAt = vec3(cam, game.groundHeight(cam.x, cam.y, cp.z + 2.f) + 2.1f);
                    vec2 at = cam - f * 9.f;
                    if (pl->vehicle >= 0) game.removePedFromVehicle(game.player, false);
                    pl->pos = dvec3(at.x, at.y, game.groundHeight(at.x, at.y, cp.z + 2.f));
                    pl->vel = vec3(0.f);
                    LOG("autoplay parking: following %s car %d (%s) at %.0f %.0f, ped %d", arriving ? "arriving" : "departing", trackVeh,
                        game.vassets[v.model].spec.name.c_str(), cp.x, cp.y, trackPed);
                }
            }
            if (trackVeh >= 0 && pl) {
                trackT += dt;
                const Vehicle& v = game.vehicles[trackVeh];
                const VehAI& va = game.vehAI(trackVeh);
                vec3 look = v.sim.body.pos.toVec3();
                bool pedOk = trackPed >= 0 && trackPed < (int)game.peds.size() && game.peds[trackPed].used;
                const PedAI* pa = pedOk && trackPed < (int)game.ai.ped.size() && game.ai.ped[trackPed].uid == game.peds[trackPed].uid ? &game.ai.ped[trackPed] : nullptr;
                int ph = phase;
                const char* name = "";
                if (!arriving) {
                    if (pa && pa->activity == ACT_DRIVE_OFF) {
                        ph = pa->clipTimer < 0.f ? 0 : 1;
                        name = ph == 0 ? "owner_walks_up" : "getting_in";
                        if (ph == 0) look = (look + game.peds[trackPed].pos.toVec3()) * 0.5f;
                    } else if (va.pullOut == 1) {
                        ph = 2, name = "blinker_waiting";
                    } else if (va.pullOut == 2) {
                        ph = 3, name = "pulling_out";
                    } else if (v.seats[0] >= 0 && game.traffic.get(trackVeh)) {
                        ph = 4, name = "in_traffic";
                    }
                } else {
                    if (va.parking == 1) {
                        ph = 0, name = "easing_into_spot";
                    } else if (pa && pa->activity == ACT_LEAVE_CAR) {
                        ph = 1, name = "out_and_round";
                        look = (look + game.peds[trackPed].pos.toVec3()) * 0.5f;
                    } else if (pedOk && game.peds[trackPed].brain.type == BRAIN_GOTO) {
                        ph = 2, name = "off_inside";
                        look = game.peds[trackPed].pos.toVec3();
                    } else if (!pedOk && v.parked) {
                        ph = 3, name = "parked";
                    }
                }
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(camAt);
                game.rig.scriptTarget = dvec3(look + vec3(0.f, 0.f, 0.6f));
                game.rig.scriptFov = 50.f;
                if (ph != phase) {
                    phase = ph;
                    if (ph >= 0) {
                        game.requestScreenshot = shotPath(StrFormat("auto_parking_%02d_%s_%s", shots++, arriving ? "arrive" : "depart", name));
                        LOG("autoplay parking: car %d %s -> %s (t %.1f) | pullOut %d parking %d parked %d speed %.1f", trackVeh, arriving ? "arrive" : "depart", name, trackT,
                            (int)va.pullOut, (int)va.parking, (int)v.parked, v.sim.speed());
                    }
                }
                if ((!arriving && phase == 4) || (arriving && phase >= 2)) doneT += dt;
                // every few seconds: where the person is and what they are doing
                if (pedOk && (int)(trackT / 3.f) != (int)((trackT - dt) / 3.f)) {
                    const Ped& tp = game.peds[trackPed];
                    LOG("autoplay parking:   ped %d at %.1f %.1f state %d brain %d activity %d speed %.1f | car at %.1f %.1f", trackPed, tp.pos.x, tp.pos.y, (int)tp.state,
                        (int)tp.brain.type, pa ? (int)pa->activity : -1, length(tp.vel.xy()), v.sim.body.pos.x, v.sim.body.pos.y);
                }
                if (doneT > 4.f) release("seen through");
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 10.f;
                LOG("autoplay parking t=%.0f | %s", t, game.aiCensusText(90.f).c_str());
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
            if ((int)(t / 4.f) != (int)((t - dt) / 4.f)) {
                // every police unit: distance to us, speed, drive mode
                std::string units;
                Ped* me = game.playerPed();
                for (int i = 0; i < (int)game.vehicles.size() && me; i++) {
                    const Vehicle& o = game.vehicles[i];
                    if (!o.used || o.faction != FAC_POLICE) continue;
                    const AI::Driver* d = game.traffic.get(i);
                    units += StrFormat(" [%d %s %.0fm %.0fm/s m%d]", i, game.isAircraft(i) ? "heli" : "car", length(rel(o.sim.body.pos, me->pos)), o.sim.speed(),
                                       d ? (int)d->mode : -1);
                }
                LOG("autoplay chase t=%.1f wanted %d seen %d speed %.1f units:%s | %s | %s", t, game.pinfo.wanted, (int)game.pinfo.policeSeesPlayer,
                    pv >= 0 ? game.vehicles[pv].sim.speed() : 0.f, units.c_str(), game.aiCensusText(120.f).c_str(), game.aiDebugText().c_str());
            }
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
                std::string ahead = "none", npcTxt = "gone";
                for (int i = 0; i < (int)game.vehicles.size() && pv >= 0; i++) {
                    const Vehicle& o = game.vehicles[i];
                    if (!o.used || i == pv || !o.persistent) continue;
                    const AI::Driver* d = game.traffic.get(i);
                    npcTxt = StrFormat("car %d at %.1f m, speed %.1f, mode %d, rage %d, driver %d", i, length(rel(o.sim.body.pos, game.vehicles[pv].sim.body.pos)),
                                       o.sim.speed(), d ? (int)d->mode : -1, i < (int)game.ai.veh.size() ? (int)game.ai.veh[i].rage : -1, o.seats[0]);
                }
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
                LOG("autoplay rage t=%.1f npc: %s | ahead: %s | %s", t, npcTxt.c_str(), ahead.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "soak" || autoplay == "hwysoak") {
            // long drive on the traffic AI through the city while the clock runs (rush hour -> night), with a 3-star
            // chase at 8-10 min and a 4-star chase at 18-20 min; telemetry every 20 s, the own car unstuck if needed.
            // hwysoak: the same, but trips from one interchange to another across the map (on-ramps, merges, exits)
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
                    } else {
                        // city errands: route to one downtown / midtown / beach / Calle Luna spot after another, so the
                        // soak spends its time in dense traffic rather than wandering off onto the highways
                        static float routeT = 1e9f;
                        routeT += dt;
                        vec2 me = game.vehicles[pv].sim.body.pos.toVec3().xy();
                        bool arrived = d->mode == AI::DM_ROUTE && (length(d->dest - me) < 60.f || (d->destEdges.size() <= 1 && length(d->dest - me) < 250.f));
                        bool hwyTrips = autoplay == "hwysoak";
                        if (d->mode == AI::DM_FLEE || d->mode == AI::DM_NORMAL || arrived || routeT > (hwyTrips ? 480.f : 240.f)) {
                            static const vec2 kSpots[] = {{2713, 763}, {3165, -243}, {3350, -760}, {2700, 1300}, {1720, 360},
                                                          {5066, 1470}, {5372, 900}, {3093, 1600}, {2300, -150}, {3356, 662}};
                            // (next to interchanges, far apart: every trip takes an on-ramp, the motorway and an exit)
                            static const vec2 kHwy[] = {{1459, 5900}, {-115, 487}, {2553, 4096}, {-1685, 93}, {1395, 810},
                                                        {-144, 6007}, {2342, 1143}, {-1787, -1432}, {-1003, 5694}, {2745, 5486}};
                            const vec2* spots = hwyTrips ? kHwy : kSpots;
                            static int next = 0;
                            vec2 dest = spots[next % 10];
                            if (length(dest - me) < 150.f) dest = spots[++next % 10];
                            next++;
                            game.traffic.setDestination(*d, dest);
                            d->mode = AI::DM_ROUTE;
                            routeT = 0.f;
                            LOG("autoplay soak t=%.0f: driving to %.0f %.0f", t, dest.x, dest.y);
                        }
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
        } else if (autoplay == "benchmark") {
            updateBenchmark(c, dt);
        } else if (autoplay == "tour") {
            updateTour(c, dt);
        } else if (autoplay == "fpguns") {
            updateFpGuns(c, dt);
        } else if (autoplay == "camfade") {
            updateCamFade(c, dt);
        } else if (autoplay == "uishots") {
            updateUiShots(dt);
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
    // --autoplay fpguns (with --firstperson): the guns at the hip, down the sights, sprinting, reloading and through
    // scopes, one screenshot per step (auto_fpguns_NN_name.bmp)
    void updateFpGuns(Controls& c, float dt) {
        struct Step {
            WeaponType w;
            int state;   // 0 hip, 1 aiming, 2 sprinting, 3 reloading, 4 reloading (shot as the fresh magazine comes up)
            u8 comps;
            const char* name;
        };
        const Step steps[] = {
            {WPN_RIFLE, 1, 0, "rifle_reddot"},       {WPN_SMG, 1, WC_SCOPE | WC_GRIP, "smg_reflex"},
            {WPN_SNIPER, 1, 0, "sniper_scope"},      {WPN_RIFLE, 0, 0, "rifle_hip"},
            {WPN_PISTOL, 1, 0, "pistol_aim"},        {WPN_SHOTGUN, 1, 0, "shotgun_aim"},
            {WPN_SMG, 1, 0, "smg_irons"},            {WPN_REVOLVER, 1, 0, "revolver_aim"},
            {WPN_PISTOL, 0, 0, "pistol_hip"},        {WPN_RIFLE, 3, 0, "rifle_reload"},
            {WPN_PISTOL, 2, 0, "pistol_sprint"},     {WPN_RPG, 0, 0, "rpg_shoulder"},
            {WPN_SHOTGUN, 3, 0, "shotgun_reload"},   {WPN_REVOLVER, 3, 0, "revolver_reload"},
            {WPN_PISTOL, 3, 0, "pistol_reload"},     {WPN_RPG, 3, 0, "rpg_reload"},
            {WPN_RIFLE, 4, 0, "rifle_mag_in_hand"},  {WPN_PISTOL, 4, 0, "pistol_mag_in_hand"},
        };
        const int n = (int)(sizeof(steps) / sizeof(steps[0]));
        const float stepLen = 2.2f;
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl) return;
        // reload steps last the whole reload
        float curLen = tourStop >= 0 && tourStop < n && steps[tourStop].state >= 3 ? Max(stepLen, 0.8f + weaponInfo(steps[tourStop].w).reloadTime)
                                                                                   : stepLen;
        if (tourStop < 0 || tourT >= curLen) {
            tourStop = tourStop < 0 ? Min(tourFirst, n) : tourStop + 1;   // --tourstart / --tourcount pick steps
            tourT = 0.f;
            tourShot = false;
            if (tourStop >= n || tourStop >= tourFirst + tourCount) {
                tourDone = true;
                LOG("autoplay fpguns done");
                return;
            }
            const Step& st = steps[tourStop];
            game.giveWeapon(game.player, st.w, 200);
            pl->weapon = st.w;
            game.pinfo.wpnCompOwned[st.w] = game.pinfo.wpnCompFitted[st.w] = st.comps;
            if (st.state >= 3) pl->clip[st.w] = Min(pl->clip[st.w], weaponInfo(st.w).clipSize > 3 ? 3 : 0);   // something to reload
        }
        const Step& st = steps[tourStop];
        tourT += dt;
        c.aim.down = st.state == 1;
        c.aim.pressed = st.state == 1 && tourT <= dt * 1.5f;
        if (st.state == 2) {
            c.move = vec2(0.f, 1.f);
            c.sprint.down = true;
        }
        if (st.state >= 3) c.reload.pressed = tourT > 0.3f && tourT - dt <= 0.3f;
        // reloads: as the feed is worked (4: a little earlier, the fresh magazine on its way up in the hand)
        float shotAt = st.state >= 3 ? 0.3f + weaponInfo(st.w).reloadTime * (st.state == 4 ? 0.53f : 0.6f) : 1.6f;
        if (!tourShot && tourT >= shotAt) {
            tourShot = true;
            game.requestScreenshot = shotPath(StrFormat("auto_fpguns_%02d_%s", tourStop, st.name));
            LOG("autoplay fpguns %s: hold %.2f aim %.2f scope %.2f dot %.2f block %.2f", st.name, game.fpw.w, game.fpw.ads, game.fpw.scope,
                game.fpw.redDot, game.fpw.block);
        }
    }

    // --autoplay uishots: one screenshot per screen, with the game world behind the menus and the phone
    void updateUiShots(float dt) {
#ifdef HAVE_GAME_UI
        struct Step {
            const char* name;
            int menu;    // UI::MenuScreen, -1 closed
            int page;    // settings page
            int phone;   // -2 closed, -1 home screen, else the built-in app
            float at;    // screenshot time into the step
        };
        const Step steps[] = {
            {"hud", -1, 0, -2, 5.f},
            {"pause", UI::MENU_PAUSE, 0, -2, 1.5f},
            {"map", UI::MENU_MAP, 0, -2, 2.f},
            {"settings_display", UI::MENU_SETTINGS, 0, -2, 1.5f},
            {"settings_audio", UI::MENU_SETTINGS, 1, -2, 1.5f},
            {"settings_controls", UI::MENU_SETTINGS, 3, -2, 1.5f},
            {"settings_accessibility", UI::MENU_SETTINGS, 5, -2, 1.5f},
            {"stats", UI::MENU_STATS, 0, -2, 1.5f},
            {"brief", UI::MENU_BRIEF, 0, -2, 1.5f},
            {"phone_home", -1, 0, -1, 1.5f},
            {"phone_contacts", -1, 0, 0, 1.5f},
            {"phone_messages", -1, 0, 1, 1.5f},
            {"phone_tidegram", -1, 0, 2, 1.5f},
            {"phone_map", -1, 0, 4, 1.5f},
            {"wanted", -1, 0, -2, 8.f},
        };
        const int n = (int)(sizeof(steps) / sizeof(steps[0]));
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl) return;
        if (tourStop < 0 || (tourShot && game.requestScreenshot.empty() && tourT >= steps[tourStop].at + 0.3f)) {
            tourStop = tourStop < 0 ? Min(tourFirst, n) : tourStop + 1;   // --tourstart / --tourcount pick screens
            tourT = 0.f;
            tourShot = false;
            if (tourStop >= n || tourStop >= tourFirst + tourCount) {
                tourDone = true;
                if (menu.screen != UI::MENU_NONE) {
                    menu.screen = UI::MENU_NONE;
#ifdef HAVE_AUDIO
                    Audio::setPaused(false);
#endif
                }
                game.phone.open = false;
                LOG("autoplay uishots done");
                return;
            }
            const Step& st = steps[tourStop];
            UI::Menus::reset();
            if (st.menu >= 0) {
                if (menu.screen == UI::MENU_NONE) openPause((UI::MenuScreen)st.menu);
                menu.screen = (UI::MenuScreen)st.menu;
                menu.cursor = 0;
                menu.tab = 0;
                if (st.menu == UI::MENU_SETTINGS) UI::Menus::testSettingsPage(st.page);
            } else if (menu.screen != UI::MENU_NONE) {
                menu.screen = UI::MENU_NONE;
#ifdef HAVE_AUDIO
                Audio::setPaused(false);
#endif
            }
            if (st.phone >= -1) UI::Phone::testShow(game.phone, st.phone);
            else game.phone.open = false;
            if (!strcmp(st.name, "wanted")) {
                game.pinfo.wantedHeat = 5.5f;
                game.pinfo.wanted = 3;
                game.pinfo.lastSeenPos = pl->pos;
                game.pinfo.lastSeenTime = (float)game.time;
            }
        }
        tourT += dt;
        const Step& st = steps[tourStop];
        uiShotAt = st.at;
        if (!tourShot && tourT >= st.at) {
            tourShot = true;
            game.requestScreenshot = shotPath(StrFormat("auto_uishots_%02d_%s", tourStop, st.name));
            LOG("autoplay uishots %s", st.name);
        }
#else
        (void)dt;
        tourDone = true;
#endif
    }

    // --autoplay camfade: the pedestrian 1.3 m and 2.3 m out along the line of sight to the player (see-through,
    // then half faded), beside the player (solid) and 0.75 m in front of the first-person eyes (solid)
    void updateCamFade(Controls& c, float dt) {
        const char* names[] = {"occluder_near", "occluder_mid", "beside_player", "fp_close"};
        const int n = 4;
        const float stepLen = 2.5f;
        (void)c;
        if (tourDone) return;
        Ped* pl = game.playerPed();
        if (!pl || fadePed < 0 || !game.peds[fadePed].used) {
            if (!tourDone) LOG("autoplay camfade: no pedestrian");
            tourDone = true;
            return;
        }
        if (tourStop < 0 || tourT >= stepLen) {
            tourStop++;
            tourT = 0.f;
            tourShot = false;
            if (tourStop >= n) {
                tourDone = true;
                LOG("autoplay camfade done");
                return;
            }
            game.rig.footFirstPerson = tourStop == 3;
        }
        tourT += dt;
        Ped& q = game.peds[fadePed];
        vec3 pp = pl->pos.toVec3();
        vec3 cam = game.rig.cam.pos.toVec3();
        vec2 sight = normalize(vec2(pp.x - cam.x, pp.y - cam.y) + vec2(1e-4f, 0.f));
        vec2 at;
        if (tourStop <= 1) at = vec2(cam.x, cam.y) + sight * (tourStop == 0 ? 1.3f : 2.3f);
        else if (tourStop == 2) at = vec2(pp.x, pp.y) + vec2(sight.y, -sight.x) * 0.9f;
        else at = vec2(pp.x, pp.y) + vec2(-sinf(pl->yaw), cosf(pl->yaw)) * 0.75f;
        q.pos = dvec3(at.x, at.y, game.groundHeight(at.x, at.y, pp.z + 2.f));
        q.vel = vec3(0.f);
        vec2 look = vec2(pp.x - at.x, pp.y - at.y);
        if (tourStop <= 1) look = -sight;   // face the camera
        q.yaw = atan2f(-look.x, look.y);
        if (!tourShot && tourT >= 2.f) {
            tourShot = true;
            game.requestScreenshot = shotPath(StrFormat("auto_camfade_%02d_%s", tourStop, names[tourStop]));
            LOG("autoplay camfade %s: fade %.2f fp %d cam-ped %.2f m", names[tourStop], q.camFade, (int)game.rig.fpActive,
                length(q.pos.toVec3() + vec3(0.f, 0.f, 1.f) - cam));
        }
    }

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

#ifdef HAVE_GAMEPLAY
    // ---- --benchmark: five scripted scenes (streaming settled first), 20 s of unlocked frames each ----
    struct BenchScene {
        const char* name;
        const mu::Place* pl;
        float hour;
        WeatherKind wx;
        bool fly;
    };
    static const int kBenchScenes = 5;
    void benchScenes(BenchScene* out) {
        mu::computePlaces(game);
        const mu::Places& P = mu::gPlaces;
        out[0] = {"Downtown street, noon", &P.policeHq, 12.5f, WX_FAIR, false};
        out[1] = {"Sol Beach promenade, sunset", &P.beachPier, 18.9f, WX_CLEAR, false};
        out[2] = {"Calle Luna, night rain", &P.mariApt, 22.f, WX_RAIN, false};
        out[3] = {"City flyover, afternoon", &P.solarisOne, 15.f, WX_CLOUDY, true};
        out[4] = {"Sawgrass, foggy dawn", &P.sawgrassRoad, 7.f, WX_FOG, false};
    }

    void updateBenchmark(Controls& c, float dt) {
        c = Controls();
        if (benchDone) {
            benchReportT += dt;
            return;
        }
        BenchScene scenes[kBenchScenes];
        benchScenes(scenes);
        const float kMeasure = benchSeconds;
        Ped* pl = game.playerPed();
        if (!pl) return;
        if (benchScene < 0 || (benchMeasuring && benchT >= kMeasure)) {
            if (benchScene >= 0) benchFinishScene(scenes[benchScene].name);
            benchScene++;
            benchT = 0.f;
            benchMeasuring = false;
            if (benchScene >= kBenchScenes) {
                benchWriteReport();
                benchDone = true;
                game.rig.scriptActive = false;
                return;
            }
            weather.setImmediate(scenes[benchScene].wx);
            game.populationWarmup = 2.5f;
            renderer.cameraCut = true;
            LOG("benchmark scene %d: %s", benchScene, scenes[benchScene].name);
        }
        const BenchScene& sc = scenes[benchScene];
        env.timeOfDay = sc.hour;   // frozen: comparable runs
        benchT += dt;
        game.pinfo.wanted = 0;
        pl->health = pl->maxHealth;
        // scripted camera: a slow dolly along the street at eye height (street side, looking ahead and a little
        // toward the frontages), or a wide orbit over the towers for the flyover
        vec3 base = sc.pl->pos;
        vec2 sd = sc.pl->streetDir, out = sc.pl->outward;
        float t = benchMeasuring ? benchT : 0.f;
        dvec3 camPos, target;
        vec2 walker;
        if (!sc.fly) {
            vec2 q = vec2(base.x, base.y) - out * 1.5f + sd * (t * 1.6f - 16.f);
            float gz = game.groundHeight(q.x, q.y, base.z + 3.f);
            camPos = dvec3(q.x, q.y, gz + 1.7f);
            vec2 look = normalize(sd + out * 0.35f);
            target = camPos + dvec3(look.x * 10.f, look.y * 10.f, -0.35f);
            walker = q + out * 2.f;
        } else {
            float a = t * 0.05f;
            camPos = dvec3(base.x + cosf(a) * 520.f, base.y + sinf(a) * 520.f, 210.f);
            target = dvec3(base.x, base.y, 40.f);
            walker = vec2(base.x, base.y);
        }
        game.rig.scriptActive = true;
        game.rig.scriptPos = camPos;
        game.rig.scriptTarget = target;
        game.rig.scriptFov = 60.f;
        // the hidden player walks the sidewalk beside the camera so population and streaming centre on the view
        pl->pos = dvec3(walker.x, walker.y, game.groundHeight(walker.x, walker.y, base.z + 3.f));
        pl->vel = vec3(0.f);
        if (!benchMeasuring && benchT > 4.f && (renderer.world->pendingCount() == 0 || benchT > 25.f)) {
            benchMeasuring = true;
            benchT = 0.f;
            benchFrameMs.clear();
            benchUpdateMs = benchRenderMs = 0.0;
            benchPeds = benchVehs = benchSamples = 0;
        }
    }

    void benchFinishScene(const char* name) {
        std::vector<float> f = benchFrameMs;
        if (f.empty()) return;
        std::sort(f.begin(), f.end());
        double sum = 0.0;
        for (float x : f) sum += x;
        float avgMs = (float)(sum / f.size());
        auto pct = [&](float q) { return f[Min((size_t)(q * (f.size() - 1) + 0.5f), f.size() - 1)]; };
        float p99 = pct(0.99f), p999 = pct(0.999f);
        float avgFps = 1000.f / Max(avgMs, 0.01f), low1 = 1000.f / Max(p99, 0.01f), low01 = 1000.f / Max(p999, 0.01f);
        int n = Max(benchSamples, 1);
        std::string line = StrFormat("%-30s avg %6.1f fps | 1%% low %6.1f | 0.1%% low %6.1f | worst %6.1f ms | cpu update %5.2f ms, render submit "
                                     "%5.2f ms | %d peds, %d vehicles",
                                     name, avgFps, low1, low01, f.back(), benchUpdateMs / n, benchRenderMs / n, benchPeds / n, benchVehs / n);
        benchLines.push_back(line);
        benchAvgFps += avgFps / kBenchScenes;
        benchWorstLow = Min(benchWorstLow, low1);
        LOG("benchmark %s", line.c_str());
    }

    void benchWriteReport() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::string r = StrFormat("NEON TIDE benchmark  %04d-%02d-%02d %02d:%02d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        r += StrFormat("GPU: %s (%zu MB)   CPU threads: %u\n", gfx::adapterName().c_str(), gfx::adapterVideoMemoryMB(), std::thread::hardware_concurrency());
        r += StrFormat("Resolution %dx%d, quality preset %d, render scale %.2f, vsync off\n\n", gfx::backbufferWidth(), gfx::backbufferHeight(),
                       renderer.settings.quality, renderer.settings.renderScale);
        for (const std::string& l : benchLines) r += l + "\n";
        r += StrFormat("\nOverall: average %.1f fps, worst 1%% low %.1f fps\n", benchAvgFps, benchWorstLow < 1e8f ? benchWorstLow : 0.f);
        std::string path = Platform::userDataDir() + "benchmark.txt";
        if (FILE* f = fopen(path.c_str(), "wb")) {
            fwrite(r.data(), 1, r.size(), f);
            fclose(f);
        }
        if (FILE* f = fopen((Platform::userDataDir() + "benchmark_history.txt").c_str(), "ab")) {
            fwrite(r.data(), 1, r.size(), f);
            fputs("\n", f);
            fclose(f);
        }
        LOG("benchmark done, results in %s\n%s", path.c_str(), r.c_str());
    }
#endif

#if defined(HAVE_GAMEPLAY) && defined(HAVE_GAME_UI)
    void drawBenchmarkOverlay() {
        float W = (float)UI::screenWidth(), H = (float)UI::screenHeight(), k = H / 1080.f;
        UI::TextStyle ts;
        ts.size = 22.f * k;
        ts.shadow = 2.f * k;
        if (!benchDone) {
            BenchScene scenes[kBenchScenes];
            benchScenes(scenes);
            if (benchScene < 0 || benchScene >= kBenchScenes) return;
            std::string label = StrFormat("BENCHMARK  %d/%d  %s  %s", benchScene + 1, kBenchScenes, scenes[benchScene].name,
                                          benchMeasuring ? StrFormat("%.0f s", Max(0.f, benchSeconds - benchT)).c_str() : "(loading)");
            UI::text(40.f * k, 40.f * k, label.c_str(), ts);
            return;
        }
        float pw = Min(W - 80.f * k, 1500.f * k), ph = (170.f + 44.f * (float)benchLines.size()) * k;
        float x = (W - pw) * 0.5f, y = (H - ph) * 0.5f;
        UI::roundRect(x, y, pw, ph, 14.f * k, UI::rgba(0.02f, 0.03f, 0.05f, 0.88f), 2.f * k, UI::rgba(1.f, 0.35f, 0.65f, 0.9f));
        UI::TextStyle hs = ts;
        hs.font = UI::FONT_HEADING;
        hs.size = 40.f * k;
        UI::text(x + 36.f * k, y + 26.f * k, "BENCHMARK RESULTS", hs);
        ts.size = 20.f * k;
        UI::text(x + 36.f * k, y + 80.f * k, StrFormat("%s   %dx%d   preset %d", gfx::adapterName().c_str(), gfx::backbufferWidth(), gfx::backbufferHeight(),
                                                        renderer.settings.quality).c_str(), ts);
        float ly = y + 124.f * k;
        UI::TextStyle ls = ts;
        ls.size = 19.f * k;
        for (const std::string& l : benchLines) {
            UI::text(x + 36.f * k, ly, l.c_str(), ls);
            ly += 44.f * k;
        }
        UI::TextStyle os = ts;
        os.size = 24.f * k;
        os.color = UI::rgba(1.f, 0.8f, 0.3f);
        UI::text(x + 36.f * k, ly + 4.f * k, StrFormat("Average %.1f fps, worst 1%% low %.1f fps  -  saved to benchmark.txt (press Enter to exit)",
                                                      benchAvgFps, benchWorstLow < 1e8f ? benchWorstLow : 0.f).c_str(), os);
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
            rawFrameSec = now - lastTime;
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
                else if (game.weaponShowcase) game.submitWeaponShowcase();   // --shot runs without a game
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
                icfg.mouseSensitivityY = menu.settings.mouseSensitivityY;
                icfg.padSensitivityY = menu.settings.padSensitivityY;
                icfg.aimToggle = menu.settings.aimToggle;
                icfg.sprintToggle = menu.settings.sprintToggle;
                icfg.crouchToggle = menu.settings.crouchToggle;
                icfg.padLayout = menu.settings.padLayout;
                icfg.bindings = &menu.settings;
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
                game.hidePlayerModel = (photo && game.phone.photo.hidePlayer) || autoplay == "benchmark";
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
                    if (mu::gWeatherRequest >= 0 && mu::gWeatherRequest < WX_COUNT) {   // story missions (the Act 4 storm, dawn fog)
                        if (mu::gWeatherInstant) weather.setImmediate((WeatherKind)mu::gWeatherRequest);
                        else weather.transitionTo((WeatherKind)mu::gWeatherRequest, 30.f);
                        mu::gWeatherRequest = -1;
                    }
                    weather.update(env, simDt, game.rig.cam.pos);
                    if (weather.cur != wxBefore && (weather.cur == WX_RAIN || weather.cur == WX_STORM || weather.cur == WX_FOG))
                        game.socialReport(UI::TE_WEATHER, game.rig.cam.pos, weather.cur == WX_RAIN ? "rain" : (weather.cur == WX_STORM ? "storm" : "fog"));
                    double tu0 = TimeSeconds();
                    game.update(simDt);
                    if (benchMeasuring) benchUpdateMs += (TimeSeconds() - tu0) * 1000.0;
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
                                (!autoplay.empty() && (autoplay == "tour"      ? tourT > 6.4f
                                                       : autoplay == "uishots" ? tourT > uiShotAt - 0.6f
                                                                               : autoTime >= autoShot * autoShotEvery + 1.2f));
                bool doRender = renderEvery <= 1 || (playFrames++ % (u32)renderEvery) == 0 || shotSoon;
                if (doRender) game.submitRender();
                game.updateAudioListener(dt);
                Render::Camera rc = game.rig.cam;
#ifdef HAVE_GAME_UI
                if (!game.phone.photo.active) {
                    // Settings FOVs scale the rig's own: 60 degrees third person, 68 degrees first person (camera.cpp)
                    float fovScale = game.rig.fpActive ? menu.settings.fovFirstPerson / 68.f : menu.settings.fov / 60.f;
                    rc.fovY = Clamp(rc.fovY * fovScale, 25.f * kDegToRad, 110.f * kDegToRad);
                }
#endif
                cam = rc;
                game.renderCam = rc;
                skippedDt += dt;
                if (doRender) {
                    // frames skipped by --renderevery still count for time-based adaptation (exposure, particles)
                    double tr0 = TimeSeconds();
                    renderer.render(rc, env, Min(skippedDt, 0.75f));
                    if (benchMeasuring) {
                        benchRenderMs += (TimeSeconds() - tr0) * 1000.0;
                        benchFrameMs.push_back((float)(rawFrameSec * 1000.0));
                        int np = 0, nv = 0;
                        for (auto& q : game.peds) np += q.used;
                        for (auto& q : game.vehicles) nv += q.used;
                        benchPeds += np;
                        benchVehs += nv;
                        benchSamples++;
                    }
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
                if (autoplay == "benchmark") drawBenchmarkOverlay();
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
                // with --renderevery N the requested shot waits for a few frames rendered in a row: as the only frame
                // after up to N - 1 skipped ones its TAA, reflection, occlusion and fog history would be stale or reset
                if (renderEvery > 1 && shotSettle < 6) {
                    shotSettle++;
                } else {
                    gfx::saveScreenshotBMP(game.requestScreenshot.c_str());
                    game.requestScreenshot.clear();
                    shotSettle = 0;
                }
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
                if (autoplay == "benchmark") {
                    if (benchDone && (benchReportT > 20.f || (benchReportT > 1.f && (in.pressed(KEY_ESCAPE) || in.pressed(KEY_ENTER) || in.pressed(KEY_SPACE))))) {
                        if (!benchFromMenu) break;
                        benchFromMenu = benchDone = false;
                        benchReportT = 0.f;
                        autoplay.clear();
                        game.rig.scriptActive = false;
                        weather.locked = false;
                        openMainMenu();
                    }
                } else if (autoplay == "tour" || autoplay == "fpguns" || autoplay == "camfade" || autoplay == "uishots") {
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
            gfx::present(autotest || autoplay == "benchmark" ? false : renderer.settings.vsync);   // benchmarks run unlocked
            if (frameCap > 0 && !autotest && autoplay != "benchmark") {
                // frame-rate cap: sleep most of the remaining budget, then spin the last millisecond
                double budget = 1.0 / (double)frameCap, until = lastTime + budget;
                double rest = until - TimeSeconds();
                if (rest > 0.002) Sleep((DWORD)((rest - 0.0015) * 1000.0));
                while (TimeSeconds() < until) {}
            }
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
        for (const auto& st : mu::activityStats(game)) menu.stats.push_back({st.first, st.second});   // encounters, fishing, tours
        menu.briefTitle = game.missionTitle();   // empty between missions: FREE ROAM
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
            case UI::MA_RUN_BENCHMARK:   // from the settings menu: run the scenes, show the results, back to the main menu
                benchFromMenu = true;
                autoplay = "benchmark";
                startNewGame();
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
#ifdef HAVE_AUDIO
        Audio::setDialogueDucking(s.musicDucking);
#endif
        frameCap = s.frameRateCap;
#ifdef HAVE_GAMEPLAY
        game.settingsSubtitles = s.subtitles;
        game.settingsRadar = s.showRadar;
        game.settingsMetric = s.metricUnits;
        game.vibration = s.vibration;
        game.reduceFlashing = s.reduceFlashing;
        game.rig.shakeScale = Saturate(s.cameraShake);
        game.rig.vehicleAutoCenter = s.vehicleAutoCenter;
        game.rig.headBob = s.headBob;
        game.rig.fpVehicleDefault = s.firstPersonVehicle || Platform::hasArg("firstperson");   // tests: --firstperson everywhere
        if (s.firstPersonOnFoot != lastFpSetting) {   // a changed default applies now; V / Back still toggles in play
            lastFpSetting = s.firstPersonOnFoot;
            game.rig.footFirstPerson = s.firstPersonOnFoot;
        }
#endif
        renderer.settings.reduceFlashing = s.reduceFlashing;    // lightning, strobe / flicker emissives (renderer)
        renderer.settings.colorblindOn = s.colorblindMode != 0;  // scene colour-blind correction (the UI corrects itself)
        UI::colorblindMatrix(s.colorblindMode, renderer.settings.colorblind);
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
