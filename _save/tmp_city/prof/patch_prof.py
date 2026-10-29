# SCRATCH ONLY (never committed): per-stage frame timers for the hand drive, logged at the end of an autoplay run.
import sys
root = sys.argv[1]
def patch(path, pairs):
    p = root + "/" + path
    s = open(p).read()
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:60], s.count(old))
        s = s.replace(old, new)
    open(p, "w").write(s)

HEAD = '''// ---- SCRATCH PROFILING, never committed: each frame's time split into stages by one running mark
namespace ScratchProf {
constexpr int kN = 48;
inline const char* name[kN] = {};
inline double cur[kN] = {};
inline double t = 0.0;
inline bool rendered = false;
inline void mark(int i, const char* nm) {
    double n = TimeSeconds();
    cur[i] += (n - t) * 1000.0;
    name[i] = nm;
    t = n;
}
}  // namespace ScratchProf
'''
patch("src/game/gameworld.cpp", [
    ('#include "gameworld.h"\n', '#include "gameworld.h"\n' + HEAD),
    ("""    updateTutorialHints(realDt);
    double t0 = TimeSeconds();
    Interiors::preUpdate(*this, dt);   // interiors_game.cpp (test walk-through controls)
    Transit::update(*this, dt);        // transit_game.cpp: trains, buses, ferries; handles boarding input before the player
    mu::fishingInput(*this);           // fishing.cpp: the rod takes the controls while it is out
    updatePlayer(dt);
    double t1 = TimeSeconds();
    updateAI(dt);
    updateAmbientTraffic(dt);
    updatePublicAddress(dt);
    double t2 = TimeSeconds();
    Vehicles::setSurfaceWetness(env->wetness);   // wet roads: longer stops, less grip at speed
    updateVehicles(dt);
    double t3 = TimeSeconds();
    updatePeds(dt);
    Interiors::update(*this, dt);      // interiors_game.cpp: streaming, collision, doors, NPCs, player inside
    double t4 = TimeSeconds();
    updateProjectiles(dt);
    updateFires(dt);
    updatePickups(dt);
    updateWanted(dt);
    updateSocial(realDt);
    double t5 = TimeSeconds();
    updateMissions(dt);
    updateGps(realDt);
    double t6 = TimeSeconds();
    sanitizeEntities();
    updateCamera(realDt);
    updateFirstPersonWeapon(realDt);   // after the camera: the gun is placed in front of the eyes
    updateFirstPersonMelee(realDt);
    updateCameraFades(realDt);
    Wildlife::update(*this, dt);   // animals around the player (wildlife.cpp; after the camera: LOD / spawning use it)
    updateRumble(realDt);
    updatePostFx(realDt);
    updateAutosave(realDt);
    double t7 = TimeSeconds();""", """    updateTutorialHints(realDt);
    ScratchProf::mark(4, "update: head (fades, waves, focus, hints)");
    double t0 = TimeSeconds();
    Interiors::preUpdate(*this, dt);   // interiors_game.cpp (test walk-through controls)
    ScratchProf::mark(5, "update: interiors pre");
    Transit::update(*this, dt);        // transit_game.cpp: trains, buses, ferries; handles boarding input before the player
    ScratchProf::mark(6, "update: transit");
    mu::fishingInput(*this);           // fishing.cpp: the rod takes the controls while it is out
    ScratchProf::mark(7, "update: fishing input");
    updatePlayer(dt);
    ScratchProf::mark(8, "update: player");
    double t1 = TimeSeconds();
    updateAI(dt);
    ScratchProf::mark(9, "update: ai");
    updateAmbientTraffic(dt);
    ScratchProf::mark(10, "update: ambient traffic");
    updatePublicAddress(dt);
    ScratchProf::mark(11, "update: public address");
    double t2 = TimeSeconds();
    Vehicles::setSurfaceWetness(env->wetness);   // wet roads: longer stops, less grip at speed
    updateVehicles(dt);
    ScratchProf::mark(12, "update: vehicles");
    double t3 = TimeSeconds();
    updatePeds(dt);
    ScratchProf::mark(13, "update: peds");
    Interiors::update(*this, dt);      // interiors_game.cpp: streaming, collision, doors, NPCs, player inside
    ScratchProf::mark(14, "update: interiors");
    double t4 = TimeSeconds();
    updateProjectiles(dt);
    ScratchProf::mark(15, "update: projectiles");
    updateFires(dt);
    ScratchProf::mark(16, "update: fires");
    updatePickups(dt);
    ScratchProf::mark(17, "update: pickups");
    updateWanted(dt);
    ScratchProf::mark(18, "update: wanted");
    updateSocial(realDt);
    ScratchProf::mark(19, "update: social");
    double t5 = TimeSeconds();
    updateMissions(dt);
    ScratchProf::mark(20, "update: missions");
    updateGps(realDt);
    ScratchProf::mark(21, "update: gps");
    double t6 = TimeSeconds();
    sanitizeEntities();
    ScratchProf::mark(22, "update: sanitize");
    updateCamera(realDt);
    ScratchProf::mark(23, "update: camera");
    updateFirstPersonWeapon(realDt);   // after the camera: the gun is placed in front of the eyes
    updateFirstPersonMelee(realDt);
    updateCameraFades(realDt);
    ScratchProf::mark(24, "update: fp weapon, melee, fades");
    Wildlife::update(*this, dt);   // animals around the player (wildlife.cpp; after the camera: LOD / spawning use it)
    ScratchProf::mark(25, "update: wildlife");
    updateRumble(realDt);
    updatePostFx(realDt);
    updateAutosave(realDt);
    ScratchProf::mark(26, "update: rumble, postfx, autosave");
    double t7 = TimeSeconds();"""),
])

patch("src/game/app.cpp", [
    ("""    struct AutoFrame {
        float t, ms, streamMs;
        std::string uploads;
    };
    std::vector<AutoFrame> autoFrames;""", """    struct AutoFrame {
        float t, ms, streamMs;
        std::string uploads;
    };
    std::vector<AutoFrame> autoFrames;
    struct SpFrame {   // (scratch profiling)
        float t, ms;
        bool render;
        float st[ScratchProf::kN];
    };
    std::vector<SpFrame> spFrames;
    double spLoopTop = 0.0;"""),
    ("""            Platform::beginFrameInput();
            if (!Platform::pumpMessages()) break;""", """            spLoopTop = TimeSeconds();
            Platform::beginFrameInput();
            if (!Platform::pumpMessages()) break;"""),
    ("""            if (!autoplay.empty() && autoplay != "benchmark" && state == AS_PLAYING && renderer.world)   // (the frame just ended)
                autoFrames.push_back({autoTime, (float)(rawFrameSec * 1000.0), (float)renderer.world->lastUpdateMs, renderer.world->lastUploads});
#endif""", """            if (!autoplay.empty() && autoplay != "benchmark" && state == AS_PLAYING && renderer.world)   // (the frame just ended)
                autoFrames.push_back({autoTime, (float)(rawFrameSec * 1000.0), (float)renderer.world->lastUpdateMs, renderer.world->lastUploads});
            {
                ScratchProf::cur[0] += (now - spLoopTop) * 1000.0;   // (this loop's top belongs to the frame that just ended)
                ScratchProf::name[0] = "loop top: input pump, resize";
                if (!autoplay.empty() && autoplay != "benchmark" && state == AS_PLAYING) {
                    SpFrame f;
                    f.t = autoTime;
                    f.ms = (float)(rawFrameSec * 1000.0);
                    f.render = ScratchProf::rendered;
                    for (int i = 0; i < ScratchProf::kN; i++) f.st[i] = (float)ScratchProf::cur[i];
                    spFrames.push_back(f);
                }
                for (int i = 0; i < ScratchProf::kN; i++) ScratchProf::cur[i] = 0.0;
                ScratchProf::rendered = false;
                ScratchProf::t = now;
            }
#endif"""),
    ("""                int pv = game.playerVehicle();
                readControls(in, icfg, pv >= 0, pv >= 0 && game.isAircraft(pv), dt, game.ctl);""", """                ScratchProf::mark(1, "frame top (to the playing branch)");
                int pv = game.playerVehicle();
                readControls(in, icfg, pv >= 0, pv >= 0 && game.isAircraft(pv), dt, game.ctl);"""),
    ("""                    double tu0 = TimeSeconds();
                    game.update(simDt);""", """                    ScratchProf::mark(3, "input, autoplay (the player's car AI), weather, clock");
                    double tu0 = TimeSeconds();
                    game.update(simDt);"""),
    ("""                if (doRender) game.submitRender();
                game.updateAudioListener(dt);""", """                ScratchProf::mark(27, "app: after game.update (death, F9, photo)");
                ScratchProf::rendered = doRender;
                if (doRender) game.submitRender();
                ScratchProf::mark(28, "submitRender (rendered frames)");
                game.updateAudioListener(dt);
                ScratchProf::mark(29, "audio listener");"""),
    ("""                    skippedDt = 0.f;
                } else renderer.world->update(rc.pos, TimeSeconds());""", """                    skippedDt = 0.f;
                    ScratchProf::mark(30, "render (rendered frames)");
                } else {
                    ScratchProf::mark(32, "app: camera, fov");
                    renderer.world->update(rc.pos, TimeSeconds());
                    ScratchProf::mark(31, "world streaming update (skipped frames)");
                }"""),
    ("""            UI::beginFrame(gfx::backbufferWidth(), gfx::backbufferHeight());
            viewer.drawOverlay(renderer, dt);
#if defined(HAVE_GAMEPLAY) && defined(HAVE_GAME_UI)
            if (state == AS_PLAYING) {
                game.fillHud(hud, dt);""", """            ScratchProf::mark(32, "app: camera, fov");
            UI::beginFrame(gfx::backbufferWidth(), gfx::backbufferHeight());
            viewer.drawOverlay(renderer, dt);
            ScratchProf::mark(33, "UI begin, viewer overlay");
#if defined(HAVE_GAMEPLAY) && defined(HAVE_GAME_UI)
            if (state == AS_PLAYING) {
                game.fillHud(hud, dt);
                ScratchProf::mark(34, "fillHud");"""),
    ("""                if (showHud) UI::drawHud(hud, dt);""", """                if (showHud) UI::drawHud(hud, dt);
                ScratchProf::mark(35, "drawHud");"""),
    ("""                drawCinematicOverlay();
            }""", """                drawCinematicOverlay();
                ScratchProf::mark(36, "mission overlay, phone, cinematic");
            }"""),
    ("""            drawDebugText();""", """            ScratchProf::mark(37, "menus");
            drawDebugText();"""),
    ("""            UI::endFrame();
#ifdef HAVE_GAMEPLAY
            if (state == AS_PLAYING && !game.requestScreenshot.empty()) {""", """            UI::endFrame();
            ScratchProf::mark(38, "debug text, UI end");
#ifdef HAVE_GAMEPLAY
            if (state == AS_PLAYING && !game.requestScreenshot.empty()) {"""),
    ("""            if (quit) break;
            gfx::present(autotest || autoplay == "benchmark" ? false : renderer.settings.vsync);   // benchmarks run unlocked""",
     """            if (quit) break;
            ScratchProf::mark(39, "shots, photos, gpu timers, automation");
            gfx::present(autotest || autoplay == "benchmark" ? false : renderer.settings.vsync);   // benchmarks run unlocked
            ScratchProf::mark(40, "present");"""),
    ("""        LOG("autoplay frames %zu: %.1f ms median, p99 %.1f, max %.1f; %zu over 33 ms; streaming %.2f ms median, p99 %.2f, max %.2f", autoFrames.size(),
            pct(ms, 0.5f), pct(ms, 0.99f), ms.back(), over, pct(sm, 0.5f), pct(sm, 0.99f), sm.back());""",
     """        LOG("autoplay frames %zu: %.1f ms median, p99 %.1f, max %.1f; %zu over 33 ms; streaming %.2f ms median, p99 %.2f, max %.2f", autoFrames.size(),
            pct(ms, 0.5f), pct(ms, 0.99f), ms.back(), over, pct(sm, 0.5f), pct(sm, 0.99f), sm.back());
        // (scratch profiling) frames from t = 3 s: each stage's mean and median, skipped and rendered frames apart
        for (int rr = 0; rr < 2; rr++) {
            std::vector<const SpFrame*> fs;
            for (const SpFrame& f : spFrames)
                if (f.t >= 3.f && f.render == (rr == 1)) fs.push_back(&f);
            if (fs.empty()) continue;
            std::vector<float> fm;
            double sumAll = 0.0;
            for (const SpFrame* f : fs) fm.push_back(f->ms);
            std::sort(fm.begin(), fm.end());
            double meanMs = 0.0;
            for (float v : fm) meanMs += v;
            meanMs /= fm.size();
            LOG("scratchprof %s frames %zu (t >= 3 s): frame mean %.3f ms, median %.3f", rr ? "rendered" : "skipped", fs.size(), meanMs, fm[fm.size() / 2]);
            for (int i = 0; i < ScratchProf::kN; i++) {
                if (!ScratchProf::name[i]) continue;
                std::vector<float> v;
                double sum = 0.0;
                for (const SpFrame* f : fs) {
                    v.push_back(f->st[i]);
                    sum += f->st[i];
                }
                std::sort(v.begin(), v.end());
                sumAll += sum / fs.size();
                LOG("scratchprof %s %2d %-52s mean %8.3f median %8.3f p90 %8.3f ms", rr ? "rendered" : "skipped", i, ScratchProf::name[i], sum / fs.size(), v[v.size() / 2],
                    v[(size_t)(0.9 * (v.size() - 1))]);
            }
            LOG("scratchprof %s stages' means sum to %.3f ms", rr ? "rendered" : "skipped", sumAll);
        }"""),
])
print("patched")
