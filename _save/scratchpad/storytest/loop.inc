// Fast mission-test runner loop (scratch test tool, appended to a copy of src/main.cpp by build_storytest.sh).
namespace storytest {

void fastLoop(Game::App& a) {
    using namespace Game;
    int renderEvery = Platform::argValue("renderevery") ? Max(1, atoi(Platform::argValue("renderevery"))) : 20;
    long long frame = 0;
    float accDt = 0.f;
    int shotFrames = 0;
    double simStart = 0.0;
    double tUpdate = 0.0, tStream = 0.0, tRender = 0.0;
    long long simFrames = 0;
    a.lastTime = TimeSeconds();
    while (true) {
        Platform::beginFrameInput();
        if (!Platform::pumpMessages()) break;
        double now = TimeSeconds();
        float dt = (float)Min(now - a.lastTime, 0.1);
        a.lastTime = now;
        if (a.state == AS_LOADING) {
            if (a.loadDone.load()) {
                a.finishLoading();
                simStart = TimeSeconds();
            } else {
                Sleep(20);
                continue;
            }
        }
        if (a.state != AS_PLAYING) break;
        InputState& in = Platform::input();
        const float simDt = 1.f / 30.f;
        a.renderer.dynamic->beginFrame();
        InputConfig icfg;
        int pv = a.game.playerVehicle();
        readControls(in, icfg, pv >= 0, pv >= 0 && a.game.isAircraft(pv), dt, a.game.ctl);
        a.game.paused = false;
        a.env.timeOfDay += simDt * a.game.timeScale / 120.f;
        if (a.env.timeOfDay >= 24.f) {
            a.env.timeOfDay -= 24.f;
            a.game.gameDay++;
        }
        a.env.gameSeconds += simDt * a.game.timeScale;
        a.weather.update(a.env, simDt, a.game.rig.cam.pos);
        double tu0 = TimeSeconds();
        a.game.update(simDt);
        tUpdate += TimeSeconds() - tu0;
        simFrames++;
        if (a.game.pinfo.deathTimer > 3.8f) a.game.fadeOut(1.2f);
        if (a.game.pinfo.deathTimer > 5.f && a.game.fadedOut()) {
            GameWorld_respawnPlayer(a.game);
            a.game.fadeIn(0.8f);
            UI::hudReset();
        }
        // the safehouse save request is left to the story module's fallback (direct save) in this runner
        Render::Camera rc = a.game.rig.cam;
        a.cam = rc;
        accDt += simDt * a.game.timeScale;   // HUD/renderer time since the last drawn frame (simulation time)
        bool wantShot = !a.game.requestScreenshot.empty();
        bool doRender = (frame % renderEvery) == 0 || wantShot;
        double tr0 = TimeSeconds();
        if (doRender) {
            a.game.submitRender();
            float hudDt = Min(accDt, 2.f);
            a.renderer.render(rc, a.env, Min(accDt, 0.1f));
            accDt = 0.f;
            UI::beginFrame(gfx::backbufferWidth(), gfx::backbufferHeight());
            a.game.fillHud(a.hud, hudDt);
            if (a.game.hudVisible) UI::drawHud(a.hud, hudDt);
            drawMissionOverlay(a.game, hudDt);
            a.drawCinematicOverlay();
            UI::endFrame();
            if (wantShot && ++shotFrames >= 2) {
                gfx::saveScreenshotBMP(a.game.requestScreenshot.c_str());
                a.game.requestScreenshot.clear();
                shotFrames = 0;
            }
            gfx::present(false);
        } else {
            a.renderer.world->update(rc.pos, TimeSeconds());
        }
        (doRender ? tRender : tStream) += TimeSeconds() - tr0;
        frame++;
        if ((simFrames % 900) == 0) {
            LOG("[storytest] %lld frames, %.1f frames/s | per frame: update %.0f ms, stream %.0f ms, render %.0f ms (per drawn frame) | game ms: "
                "player %.1f ai %.1f veh %.1f peds %.1f misc %.1f missions %.1f camera %.1f",
                simFrames, simFrames / Max(0.001, TimeSeconds() - simStart), tUpdate * 1000.0 / 900.0, tStream * 1000.0 / 900.0,
                tRender * 1000.0 / Max(1.0, 900.0 / renderEvery), a.game.profPlayer, a.game.profAI, a.game.profVehicles, a.game.profPeds,
                a.game.profMisc, a.game.profMissions, a.game.profCamera);
            tUpdate = tStream = tRender = 0.0;
        }
    }
}

}  // namespace storytest

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    static Game::App app;
    if (!app.init()) return 1;
    storytest::fastLoop(app);
    app.shutdown();
    return 0;
}
