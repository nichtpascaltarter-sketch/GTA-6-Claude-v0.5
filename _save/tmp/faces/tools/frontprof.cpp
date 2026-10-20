// Debug: the head field's front surface depth (mm ahead of the eye centre) on vertical lines at x = 0, 15, 30, 40, 45,
// 50, 55 mm from the midline, every 2 mm from 20 mm below to 60 mm above the eye centre; with the surface's slope
// (degrees the surface faces up from horizontal, from the depth change). usage: frontprof seed role
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    CharacterDesc d = randomCharacter((u32)atoi(argv[1]), atoi(argv[2]));
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    detail::buildBody(bc);
    vec3 e = D.J[B_EYE_R];
    const float xs[7] = {0.f, 0.015f, 0.03f, 0.04f, 0.045f, 0.05f, 0.055f};
    auto front = [&](float x, float z) {
        float yy = e.y + 0.08f;
        for (int it = 0; it < 400; it++) {
            float f = bc.sdf.eval(vec3(x, yy, z), detail::MK_HEAD);
            if (f <= 0.f) break;
            yy -= Max(f * 0.8f, 0.0001f);
        }
        return (yy - e.y) * 1000.f;
    };
    printf("  dz  |");
    for (float x : xs) printf("   x%2.0f  (up) |", x * 1000.f);
    printf("\n");
    for (float dz = 0.06f; dz >= -0.0201f; dz -= 0.002f) {
        printf("%+5.0f |", dz * 1000.f);
        for (float x : xs) {
            float y0 = front(x, e.z + dz), y1 = front(x, e.z + dz + 0.001f), ym = front(x, e.z + dz - 0.001f);
            float slope = atan2f(-(y1 - ym), 2.f) * kRadToDeg;   // surface facing up: depth falls going up
            printf(" %+5.1f (%+3.0f) |", y0, slope);
        }
        printf("\n");
    }
    printf("cornea apex %+.1f mm, eye x %.1f mm\n", 1.0867f * bc.head.eyeR * 1000.f, e.x * 1000.f);
    return 0;
}
