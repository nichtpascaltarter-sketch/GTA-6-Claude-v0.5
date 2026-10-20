// Syntax-check TU for the story/mission code: like src/main.cpp but with only the character headers.
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
#include "sim/vehicle_models.cpp"
#define HAVE_VEHICLE_MODELS 1
#include "anim/character.h"
#include "anim/anim_internal.h"
#define HAVE_CHARACTERS 1
#include "audio/audio_all.cpp"
#include "audio/speech.cpp"
#define HAVE_AUDIO 1
#include "sim/vehicle_sim.cpp"
#define HAVE_VEHICLE_SIM 1
#include "ui/ui_all.cpp"
#define HAVE_GAME_UI 1
#include "game/app.cpp"
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    static Game::App app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
