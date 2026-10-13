// Prints, per character, how far the cornea apex sits behind (+) or in front of (-) the brow ridge's front in profile
// (the face surface's forward-most point through the eye centre's vertical plane, 8-22 mm above the eye centre).
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 16;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        u32 seed = 1000 + i * 7919;
        CharacterDesc d = randomCharacter(seed, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        detail::BodyDims D;
        detail::computeDims(d, D);
        detail::BuildCtx bc;
        bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
        detail::buildBody(bc);
        vec3 e = D.J[B_EYE_R];
        float er = bc.head.eyeR;
        float corneaY = e.y + 1.0867f * er;
        float browY = -1e9f, browZ = 0;
        for (float dz = 0.008f; dz <= 0.022f; dz += 0.0005f) {
            // march backwards from far in front to the surface
            float y = e.y + 0.08f;
            for (int it = 0; it < 400; it++) {
                float f = bc.sdf.eval(vec3(e.x, y, e.z + dz), detail::MK_HEAD);
                if (f <= 0.f) break;
                y -= Max(f * 0.8f, 0.0001f);
            }
            if (y > browY) { browY = y; browZ = dz; }
        }
        // lid surface in front of the eye centre (straight ahead)
        float y = e.y + 0.08f;
        for (int it = 0; it < 400; it++) {
            float f = bc.sdf.eval(vec3(e.x, y, e.z), detail::MK_HEAD);
            if (f <= 0.f) break;
            y -= Max(f * 0.8f, 0.0001f);
        }
        printf("c%-2d fem %.1f age %.2f anc %d: cornea %.1f mm behind the brow front (brow %+.1f mm above the eye), lid front %+.1f mm vs cornea\n", i, D.fem, d.age,
               D.ancestry, (browY - corneaY) * 1000.f, browZ * 1000.f, (y - corneaY) * 1000.f);
        sum += (browY - corneaY) * 1000.f;
    }
    printf("mean %.1f mm\n", sum / n);
    return 0;
}
