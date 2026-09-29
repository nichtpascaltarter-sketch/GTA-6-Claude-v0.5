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
        } else if (autoplay == "shoot") {
            c.usingPad = true;                        // controller soft lock-on
            c.aim.down = fmodf(t, 3.f) > 0.15f;       // re-press to re-acquire targets
            c.aim.pressed = fmodf(t, 3.f) <= 0.15f + dt && fmodf(t, 3.f) > 0.15f;
            c.attack.down = c.aim.down && fmodf(t, 1.2f) < 0.6f;
            c.move = vec2(t > 8.f ? 0.4f : 0.f, 0.f);  // strafe later
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
                bool pausePressed = game.ctl.pause.pressed, mapPressed = game.ctl.map.pressed;
                if (menuOpen) {
                    game.ctl = Controls();
                    Platform::setGamepadRumble(0.f, 0.f);
                }
                game.paused = menuOpen;
                if (!menuOpen) {
                    env.timeOfDay += simDt * game.timeScale / 120.f;   // 1 game minute = 2 real seconds
                    if (env.timeOfDay >= 24.f) {
                        env.timeOfDay -= 24.f;
                        game.gameDay++;
                    }
                    env.gameSeconds += simDt * game.timeScale;
                    weather.update(env, simDt, game.rig.cam.pos);
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
                game.submitRender();
                game.updateAudioListener(dt);
                Render::Camera rc = game.rig.cam;
#ifdef HAVE_GAME_UI
                rc.fovY = Clamp(rc.fovY * menu.settings.fov / 60.f, 25.f * kDegToRad, 110.f * kDegToRad);
#endif
                cam = rc;
                renderer.render(rc, env, dt);
#ifdef HAVE_GAME_UI
                if (!menuOpen && pausePressed) openPause(UI::MENU_PAUSE);
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
                bool showHud = menu.screen == UI::MENU_NONE && menu.settings.showHud && game.hudVisible;
                if (showHud) UI::drawHud(hud, dt);
                drawCinematicOverlay();
            }
            if (state == AS_MENU || (state == AS_PLAYING && menu.screen != UI::MENU_NONE)) {
                prepareMenuData();
                UI::MenuAction act = UI::Menus::update(menu, in, dt);
                quit = handleMenuAction(act);
            }
#endif
            drawDebugText();
            UI::endFrame();
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
                if (autoTime >= autoShot * autoShotEvery + 1.5f && (renderer.world->pendingCount() == 0 || autoTime > autoShot * autoShotEvery + 6.f)) {
                    std::string path = shotPath(StrFormat("auto_%s_%02d", autoplay.c_str(), autoShot));
                    gfx::saveScreenshotBMP(path.c_str());
                    Ped* pl = game.playerPed();
                    if (pl) {
                        int pv = game.playerVehicle();
                        int np = 0, nv = 0;
                        for (auto& q : game.peds) np += q.used;
                        for (auto& q : game.vehicles) nv += q.used;
                        LOG("autoplay t=%.1f pos %.1f %.1f %.1f state %d health %.0f veh %d speed %.1f | peds %d vehicles %d wanted %d | cpu ms "
                            "player %.2f ai %.2f veh %.2f peds %.2f",
                            autoTime, pl->pos.x, pl->pos.y, pl->pos.z, (int)pl->state, pl->health, pv, pv >= 0 ? game.vehicles[pv].sim.speed() : length(pl->vel),
                            np, nv, game.pinfo.wanted, game.profPlayer, game.profAI, game.profVehicles, game.profPeds);
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
    }

    std::string settingsPath() { return Platform::userDataDir() + "settings.bin"; }
    void saveSettings() {
        FILE* f = fopen(settingsPath().c_str(), "wb");
        if (!f) return;
        u32 magic = 0x53544E31u, size = (u32)sizeof(UI::GameSettings);
        fwrite(&magic, 4, 1, f);
        fwrite(&size, 4, 1, f);
        fwrite(&menu.settings, sizeof(UI::GameSettings), 1, f);
        fclose(f);
    }
    void loadSettings() {
        FILE* f = fopen(settingsPath().c_str(), "rb");
        if (f) {
            u32 magic = 0, size = 0;
            UI::GameSettings s;
            if (fread(&magic, 4, 1, f) == 1 && fread(&size, 4, 1, f) == 1 && magic == 0x53544E31u && size == sizeof(UI::GameSettings) &&
                fread(&s, sizeof(UI::GameSettings), 1, f) == 1)
                menu.settings = s;
            fclose(f);
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
