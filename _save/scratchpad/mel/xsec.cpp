// Scratch: horizontal cross-sections of the head SDF (front y vs x) at several heights (head space, mm).
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
    addHeadPrims(c);
    vec3 H0 = D.J[B_HEAD];
    float hs = D.headS;
    for (int ai = 2; ai < argc; ai++) {
        float z = (float)atof(argv[ai]) * 0.001f;
        printf("z=%5.1f:", z * 1000.f);
        for (float x = -0.02f; x <= 0.0201f; x += 0.002f) {
            float yF = -1;
            for (float y = 0.14f; y > 0.0f; y -= 0.0002f) {
                vec3 p = H0 + vec3(x, y, z) * hs;
                if (c.sdf.eval(p, MK_HEAD) < 0.f) { yF = y; break; }
            }
            printf(" %5.1f", yF * 1000.f);
        }
        printf("\n");
    }
}
