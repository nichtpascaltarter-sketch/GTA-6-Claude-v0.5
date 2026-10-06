// Scratch: half-width (x, mm) of the head SDF at several heights z (rows) and depths y (columns), head space.
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    u32 seed = argc > 1 ? (u32)atoi(argv[1]) : 8919;
    CharacterDesc d = randomCharacter(seed, 0);
    Skeleton sk; buildSkeleton(d, sk);
    BodyDims D; computeDims(d, D);
    BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = d.skinTone; c.lipCol = c.skin; c.palmCol = c.skin;
    addBodyPrims(c);
    addHeadPrims(c);
    vec3 H0 = D.J[B_HEAD];
    float hs = D.headS;
    printf("y(mm):   ");
    for (float y = -0.04f; y <= 0.1001f; y += 0.01f) printf(" %5.0f", y * 1000.f);
    printf("\n");
    for (float z = 0.08f; z >= -0.0701f; z -= 0.01f) {
        printf("z=%5.0f:", z * 1000.f);
        for (float y = -0.04f; y <= 0.1001f; y += 0.01f) {
            float xF = -1;
            for (float x = 0.1f; x > 0.0f; x -= 0.0005f) {
                vec3 p = H0 + vec3(x, y, z) * hs;
                if (c.sdf.eval(p, MK_HEAD) < 0.f) { xF = x; break; }
            }
            printf(" %5.1f", xF * 1000.f);
        }
        printf("\n");
    }
}
