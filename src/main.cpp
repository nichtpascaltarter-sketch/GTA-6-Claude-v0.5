// Unity build entry point for Neon Tide.
#include <thread>
#include "core/math.cpp"
#include "core/noise.cpp"
#include "core/jobs.cpp"
#include "platform/win32.cpp"
#include "gfx/gfx.cpp"
#include "ui/draw2d.cpp"
#include "ui/signs.cpp"
#include "render/mesh.cpp"
#include "world/worldmap.cpp"
#include "world/roads.cpp"
#include "world/roadmesh.cpp"
#include "world/buildings.cpp"
#include "world/buildmesh.cpp"
#include "world/propmesh.cpp"
#include "world/cellgen.cpp"
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
#if __has_include("sim/vehicle_sim.cpp")
#include "sim/vehicle_sim.cpp"
#define HAVE_VEHICLE_SIM 1
#endif
#if __has_include("ui/ui_all.cpp")
#include "ui/ui_all.cpp"
#define HAVE_GAME_UI 1
#endif
#include "game/app.cpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    static Game::App app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
