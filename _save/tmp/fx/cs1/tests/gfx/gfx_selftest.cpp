// Standalone runner for gfx::selfTest (checks of the D3D12 features the graphics layer exposes) without the game.
// Build: see tests/gfx/build.sh. Run: gfx_selftest.exe [--d3ddebug]; exit code 0 when every check passed.
#include "../../src/core/math.cpp"
#include "../../src/platform/win32.cpp"
#include "../../src/gfx/gfx.cpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!Platform::init("Neon Tide gfx self-test", 320, 240, false, false)) return 2;
    if (!gfx::init(Platform::windowHandle(), Platform::clientWidth(), Platform::clientHeight(), Platform::hasArg("d3ddebug"))) {
        LOG("gfx init failed: %s", gfx::initError());
        return 3;
    }
    int failures = gfx::selfTest();
    gfx::shutdown();
    Platform::shutdown();
    return failures == 0 ? 0 : 1;
}
