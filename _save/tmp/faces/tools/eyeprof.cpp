// Profile of the head field through the right eye (x = eye.x): the surface's forward position (y - eye.y, mm) at
// heights z - eye.z from -6 to +30 mm. usage: eyeprof seed role
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
    for (int zm = -6; zm <= 30; zm += 1) {
        float z = e.z + zm * 0.001f, ys = -1.f;
        for (float y = e.y + 0.04f; y > e.y - 0.01f; y -= 0.0001f)
            if (bc.sdf.eval(vec3(e.x, y, z), detail::MK_HEAD) < 0.f) { ys = y; break; }
        printf("z %+3d mm: surface y %+6.1f mm\n", zm, (ys - e.y) * 1000.f);
    }
}
