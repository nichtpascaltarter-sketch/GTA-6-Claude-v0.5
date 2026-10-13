// Midline (x = 0) and eye-column profiles of the head field: the surface's forward position (y, mm, relative to the
// eye centre's y) at heights z - eye.z from -30 to +30 mm. usage: midprof seed role | midprof p0|p1 (protagonists)
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
#ifndef HAVE_CHARACTERS
#define HAVE_CHARACTERS 1
#endif
#include "src/game/protagonists.h"
using namespace Anim;
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    CharacterDesc d = argv[1][0] == 'p' ? Game::protagonistDesc(atoi(argv[1] + 1)) : randomCharacter((u32)atoi(argv[1]), atoi(argv[2]));
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    detail::buildBody(bc);
    vec3 e = D.J[B_EYE_R];
    auto surf = [&](float x, float z) {
        for (float y = e.y + 0.06f; y > e.y - 0.02f; y -= 0.0001f)
            if (bc.sdf.eval(vec3(x, y, z), detail::MK_HEAD) < 0.f) return (y - e.y) * 1000.f;
        return -99.f;
    };
    float xs[4] = {0.f, e.x * 0.5f, e.x, e.x * 1.5f};
    printf("z(mm)  x=0(mid)  x=%.0fmm  x=%.0fmm(eye)  x=%.0fmm\n", xs[1] * 1000.f, xs[2] * 1000.f, xs[3] * 1000.f);
    for (int zm = -10; zm <= 30; zm += 2) {
        float z = e.z + zm * 0.001f;
        printf("%+4d  ", zm);
        for (float x : xs) printf("%8.1f  ", surf(x, z));
        printf("\n");
    }
}
