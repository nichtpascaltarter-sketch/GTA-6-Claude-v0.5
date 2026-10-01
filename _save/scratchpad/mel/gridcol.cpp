// Scratch: head grid vertices along a column (row, phi, head-space y/z in mm).
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    u32 seed = argc > 1 ? (u32)atoi(argv[1]) : 1000;
    int col = argc > 2 ? atoi(argv[2]) : 0;
    CharacterDesc d = randomCharacter(seed, 0);
    Skeleton sk; buildSkeleton(d, sk);
    BodyDims D; computeDims(d, D);
    BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = d.skinTone; c.lipCol = c.skin; c.palmCol = c.skin;
    buildBody(c);
    const HeadInfo& H = c.head;
    printf("rows %d cols %d mouth %d/%d eye %d/%d lip %d..%d\n", H.rows, H.cols, H.rowMouthLo, H.rowMouthHi, H.rowEyeLo, H.rowEyeHi, H.rowLipLo, H.rowLipHi);
    for (int j = 0; j < H.rows; j++) {
        const BVert& v = c.m.v[H.grid[(size_t)j * H.cols + col]];
        vec3 hp = (v.p - D.J[B_HEAD]) / D.headS;
        printf("j %2d th %6.1f ph %6.1f  x %6.1f y %6.1f z %6.1f\n", j, v.pa * kRadToDeg, v.pb * kRadToDeg, hp.x * 1000, hp.y * 1000, hp.z * 1000);
    }
}
