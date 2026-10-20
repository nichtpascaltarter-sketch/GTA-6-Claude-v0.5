// Unity build entry point for Neon Tide.
#include <thread>
#include "core/math.cpp"
#include "core/noise.cpp"
#include "core/jobs.cpp"
#include "platform/win32.cpp"
#include "gfx/gfx.cpp"
// The Windows SDK RPC headers define small as char. Do not leak that macro
// into the game modules below, which use small as an ordinary identifier.
#ifdef small
#undef small
#endif
#include "ui/draw2d.cpp"
#include "ui/signs.cpp"
#include "render/mesh.cpp"
#include "world/worldmap.cpp"
#include "world/sites.cpp"
#include "world/roads.cpp"
#include "world/roadmesh.cpp"
#include "world/buildings.cpp"
#include "world/buildmesh.cpp"
#include "world/propmesh.cpp"
#include "world/cellgen.cpp"
#include "world/sitegeo.cpp"
#include "world/airport.cpp"
#include "world/port.cpp"
#include "world/landmarks.cpp"
#include "world/leisure.cpp"
#include "world/rural.cpp"
#include "world/transit.cpp"
#include "world/transitmesh.cpp"
#include "world/sitecell.cpp"
#include "world/facadedetail.cpp"
#include "world/interiorkit.cpp"
#include "world/interiorfurniture.cpp"
#include "world/interiorlayouts.cpp"
#include "world/interiorhomes.cpp"
#include "world/interiorvenues.cpp"
#include "world/interiorshops.cpp"
#include "world/interiorcivic.cpp"
#include "world/interiorindustrial.cpp"
#include "world/interiortower.cpp"
#include "world/interiorgarages.cpp"
#include "world/interiorresidences.cpp"
#include "world/interiors.cpp"
#include "world/gridcodec.cpp"
#include "world/lz.cpp"
#include "world/worldbin.cpp"
#include "world/roadsbin.cpp"
#include "world/handworld.cpp"
#include "sim/physics.cpp"
#include "render/renderer.cpp"
// Content modules developed in parallel; compiled in when present.
#if !defined(NO_VEHICLES) && __has_include("sim/vehicle_models.cpp")
#include "sim/vehicle_models.cpp"
#define HAVE_VEHICLE_MODELS 1
#endif
#if !defined(NO_CHARACTERS) && __has_include("anim/anim_all.cpp")
#include "anim/anim_all.cpp"
#define HAVE_CHARACTERS 1
#endif
#if !defined(NO_AUDIO) && __has_include("audio/audio_all.cpp")
#include "audio/audio_all.cpp"
#include "audio/speech.cpp"
#define HAVE_AUDIO 1
#endif
#if !defined(NO_VEHICLES) && __has_include("sim/vehicle_sim.cpp")
#include "sim/vehicle_sim.cpp"
#define HAVE_VEHICLE_SIM 1
#endif
#if !defined(NO_UI) && __has_include("ui/ui_all.cpp")
#include "ui/ui_all.cpp"
#define HAVE_GAME_UI 1
#endif
#include "platform/update_logic.cpp"
#include "platform/updater.cpp"
#include "game/app.cpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // a built update installs before any window exists, and the new build takes over in a new process
    int updateExit = 0;
    if (Updater::beforeStart(updateExit)) return updateExit;
    static Game::App app;
    if (!app.init()) return 1;
    Updater::startCheck();   // main's newer source builds in the background (never in automated runs)
    app.run();
    bool restart = Updater::stop();   // the main menu's "Restart now"
#ifdef HAVE_GAME_UI
    if (restart) app.saveSettings();
#endif
    app.shutdown();
    if (restart) Updater::restart();   // installs the built update and starts the game again
    return 0;
}
