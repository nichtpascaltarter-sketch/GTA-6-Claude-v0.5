// Application: owns the world, renderer and game loop.
#include "../world/worldmap.h"
#include "../world/roads.h"
#include "../world/buildings.h"
#include "../render/renderer.h"
#include "../ui/draw2d.h"
#include "../platform/platform.h"

#include "../game/viewer.cpp"

namespace Game {

struct Shot {
    dvec3 pos;
    float yaw, pitch, time;
    std::string name;
};

struct App {
    World::WorldMap map;
    World::RoadNetwork roads;
    World::BuildingSet buildings;
    Phys::CollisionWorld collision;
    Viewer viewer;
    Render::Renderer renderer;
    Render::Camera cam;
    Render::Environment env;
    float camSpeed = 40.f;
    bool showDebug = true;
    bool timeRunning = false;
    double lastTime = 0;
    float fps = 0, frameMs = 0;
    std::vector<Shot> shots;
    int shotIndex = 0, shotFrame = 0;
    int shotSettleFrames = 12;
    int shotWait = 0;

    bool init() {
        int w = Platform::argValue("width") ? atoi(Platform::argValue("width")) : 1600;
        int h = Platform::argValue("height") ? atoi(Platform::argValue("height")) : 900;
        bool autotest = Platform::hasArg("autotest");
        if (!Platform::init("Neon Tide", w, h, !autotest && Platform::hasArg("fullscreen"), false)) return false;
        int workers = Max(2, (int)std::thread::hardware_concurrency() - 1);
        Jobs::init(workers);
        if (!gfx::init(Platform::windowHandle(), Platform::clientWidth(), Platform::clientHeight(), Platform::hasArg("d3ddebug")))
            FatalError("Could not initialize Direct3D 11. A DirectX 11 capable GPU and up-to-date drivers are required.");
        UI::init();
        double t0 = TimeSeconds();
        map.generate();
        World::gMap = &map;
        roads.generate(map);
        World::gRoads = &roads;
        buildings.generate(map, roads);
        World::gBuildings = &buildings;
        Phys::gCollision = &collision;
        renderer.init(Platform::clientWidth(), Platform::clientHeight());
        renderer.setWorld(&map);
        viewer.init(renderer, map);
        LOG("Init done in %.2f s (shaders: %d compiled in %.2f s)", TimeSeconds() - t0, gfx::shaderCompileCount(),
            gfx::shaderCompileSeconds());
        cam.pos = dvec3(3000, -300, 60);
        cam.yaw = 0.f;
        cam.pitch = -0.1f;
        env.timeOfDay = 10.f;
        parseShots();
        if (const char* t = Platform::argValue("time")) env.timeOfDay = (float)atof(t);
        if (const char* d = Platform::argValue("debugview")) renderer.debugView = atoi(d);
        if (const char* cc = Platform::argValue("clouds")) env.cloudCover = (float)atof(cc);
        if (const char* rr = Platform::argValue("rain")) { env.rain = (float)atof(rr); env.wetness = env.rain; }
        if (const char* ff = Platform::argValue("fog")) env.fogDensity = (float)atof(ff);
        lastTime = TimeSeconds();
        return true;
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
        if (in.pressed(KEY_F1)) showDebug = !showDebug;
        if (in.pressed(KEY_T)) timeRunning = !timeRunning;
        if (in.down(KEY_PGUP)) env.timeOfDay += dt * 2.f;
        if (in.down(KEY_PGDN)) env.timeOfDay -= dt * 2.f;
    }

    void drawDebug() {
        UI::beginFrame(gfx::backbufferWidth(), gfx::backbufferHeight());
        if (showDebug) {
            UI::TextStyle st;
            st.size = 18.f;
            st.shadow = 1.5f;
            World::Region reg = map.regionAt((float)cam.pos.x, (float)cam.pos.y);
            std::string s = StrFormat("NEON TIDE  |  %.0f fps (%.2f ms)\npos %.0f %.0f %.0f  |  %s\ntime %05.2f  draws %d  tris %dk  cells %d (pending %d)  lights %d",
                                      fps, frameMs, cam.pos.x, cam.pos.y, cam.pos.z, World::regionInfo(reg).name, env.timeOfDay,
                                      renderer.stats.drawCalls, renderer.stats.triangles / 1000, renderer.world->drawnCells, renderer.world->pendingCount(), renderer.stats.lights);
            UI::roundRect(10, 10, 560, 78, 8, UI::rgba(0, 0, 0, 0.45f));
            UI::text(20, 16, s.c_str(), st);
        }
        UI::endFrame();
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
        if (renderer.settings.clouds) {
            for (int k = 0; k < 5; k++) {
                float cl[4];
                int cx = renderer.clouds->w * (k + 1) / 6, cy = renderer.clouds->h / 5;
                gfx::readbackPixelsFloat4(renderer.clouds->history[renderer.clouds->cur].res, DXGI_FORMAT_R16G16B16A16_FLOAT, cx, cy, cl);
                LOG("  cloud(%d,%d) = %.4g %.4g %.4g T=%.3f", cx, cy, cl[0], cl[1], cl[2], cl[3]);
            }
            u16 sm[4] = {};
            float smv[4];
            gfx::readbackPixelsFloat4(renderer.clouds->shadowMap.res, DXGI_FORMAT_R32_FLOAT, 128, 128, smv);
            (void)sm;
            LOG("  cloud shadow center raw %.4g", smv[0]);
        }
    }

    void run() {
        while (true) {
            Platform::beginFrameInput();
            if (!Platform::pumpMessages()) break;
            if (Platform::wasResized()) {
                gfx::resize(Platform::clientWidth(), Platform::clientHeight());
                renderer.resize(Platform::clientWidth(), Platform::clientHeight());
            }
            double now = TimeSeconds();
            float dt = (float)Min(now - lastTime, 0.1);
            lastTime = now;
            frameMs = Lerp(frameMs, dt * 1000.f, 0.05f);
            fps = frameMs > 0 ? 1000.f / frameMs : 0;
            bool autoShots = !shots.empty();
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
            if (timeRunning) env.timeOfDay += dt / 60.f;
            if (env.timeOfDay >= 24.f) env.timeOfDay -= 24.f;
            if (env.timeOfDay < 0.f) env.timeOfDay += 24.f;
            env.gameSeconds += dt;
            renderer.dynamic->beginFrame();
            viewer.update(renderer, map, dt);
            renderer.render(cam, env, dt);
            drawDebug();
            gfx::gpuTimersResolve();
            if (autoShots) {
                // wait for streaming to settle (bounded) before counting frames
                if (renderer.world->pendingCount() > 0 && shotWait < 600) { shotWait++; }
                else shotFrame++;
                if (shotFrame >= shotSettleFrames) {
                    shotWait = 0;
                    std::string path = std::string("Z:\\tmp\\") + shots[shotIndex].name + ".bmp";
                    if (const char* dir = Platform::argValue("shotdir")) path = std::string(dir) + shots[shotIndex].name + ".bmp";
                    gfx::saveScreenshotBMP(path.c_str());
                    if (Platform::hasArg("probe")) probePixels();
                    LOG("Saved %s", path.c_str());
                    shotFrame = 0;
                    shotIndex++;
                    if (shotIndex >= (int)shots.size()) break;
                }
            }
            gfx::present(autoShots ? false : renderer.settings.vsync);
        }
    }

    void shutdown() {
        UI::shutdown();
        renderer.shutdown();
        gfx::shutdown();
        Jobs::shutdown();
        Platform::shutdown();
    }
};

}  // namespace Game
