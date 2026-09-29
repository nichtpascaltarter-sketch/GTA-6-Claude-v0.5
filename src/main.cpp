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
#include "world/cellgen.cpp"
#include "render/renderer.cpp"
#include "game/app.cpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    static Game::App app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
