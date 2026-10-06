// Round-trip test of the settings text format (native).
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/jobs.cpp"
#include "src/gfx/gfx.h"
namespace gfx { FakeCtx g_fakeCtx; FakeCtx* ctx = &g_fakeCtx; }
#include "native_draw2d.cpp"
#include "src/ui/ui_internal.h"
#include "src/ui/ui_common.cpp"
#include "src/ui/settings.cpp"
#include "src/ui/icons.cpp"
using namespace UI;
int main() {
    GameSettings a;
    a.keyBinds[IA_JUMP][1] = 0xBD;    // '-'
    a.keyBinds[IA_RELOAD][0] = 0xBC;  // ','
    a.keyBinds[IA_AIM][1] = 0x21;     // PAGE UP
    a.keyBinds[IA_MAP][0] = 0;
    a.frameRateCap = 60; a.renderScale = 0.67f; a.colorblindMode = 2; a.fovFirstPerson = 88.f; a.padLayout = 2;
    std::string t = settingsToText(a);
    GameSettings b;
    settingsFromText(t, b);
    bool ok = memcmp(a.keyBinds, b.keyBinds, sizeof(a.keyBinds)) == 0 && b.frameRateCap == 60 && fabsf(b.renderScale - 0.67f) < 1e-4f &&
              b.colorblindMode == 2 && b.fovFirstPerson == 88.f && b.padLayout == 2;
    printf("%s", t.c_str());
    printf("round trip %s\n", ok ? "OK" : "FAILED");
    GameSettings c;
    settingsFromText("fov=500\nbind.jump=nonsense | 32\nframe_rate_cap=45\n", c);
    printf("sanitize fov %.0f jump %d/%d cap %d\n", c.fov, c.keyBinds[IA_JUMP][0], c.keyBinds[IA_JUMP][1], c.frameRateCap);
    return ok ? 0 : 1;
}
