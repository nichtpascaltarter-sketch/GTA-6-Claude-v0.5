// Scratch: midline profile of the head SDF (head space, mm) for one character.
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    u32 seed = argc > 1 ? (u32)atoi(argv[1]) : 8919;
    float xs = argc > 2 ? (float)atof(argv[2]) : 0.f;
    CharacterDesc d = randomCharacter(seed, 0);
    Skeleton sk; buildSkeleton(d, sk);
    BodyDims D; computeDims(d, D);
    BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = d.skinTone; c.lipCol = c.skin; c.palmCol = c.skin;
    addHeadPrims(c);
    vec3 H0 = D.J[B_HEAD];
    float hs = D.headS;
    printf("seed %u x=%.1fmm  (z mm : front y mm)\n", seed, xs);
    for (float z = 0.085f; z >= -0.075f; z -= 0.0025f) {
        // march from the front towards the back until inside
        float yF = -1;
        for (float y = 0.14f; y > 0.0f; y -= 0.0002f) {
            vec3 p = H0 + vec3(xs * 0.001f, y, z) * hs;
            if (c.sdf.eval(p, MK_HEAD) < 0.f) { yF = y; break; }
        }
        int bar = yF > 0 ? (int)((yF - 0.06f) * 1000.f) : 0;
        printf("%6.1f : %6.1f  %s\n", z * 1000.f, yF * 1000.f, std::string(Max(bar, 0), '#').c_str());
    }
}
