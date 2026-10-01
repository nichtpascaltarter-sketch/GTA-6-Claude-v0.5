#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    CharacterDesc d = randomCharacter(1000, 0);
    d.hat = -1; d.glasses = -1; d.gender = MALE; d.facialHair = 3;
    Skeleton sk; buildSkeleton(d, sk);
    BodyDims D; computeDims(d, D);
    BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = d.skinTone; c.lipCol = c.skin; c.palmCol = c.skin;
    buildBody(c);
    MeshB extra; std::vector<u8> hide(c.m.idx.size() / 3, 0);
    buildOutfit(c, extra, hide);
    const HeadInfo& H = c.head;
    int NRD = H.rows - 1;
    int hiddenRow[80] = {0}, totalRow[80] = {0};
    for (size_t t = 0; t < c.surfaceIdxEnd / 3; t++) {
        const BVert& v = c.m.v[c.m.idx[t * 3]];
        if (v.part != PART_HEAD) continue;
        int j = (int)lrintf((v.pc - 1.2f) * NRD);
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        if (at > 0.4f) continue;
        totalRow[j]++;
        if (hide[t]) hiddenRow[j]++;
    }
    printf("lipLo %d mouth %d/%d lipHi %d\n", H.rowLipLo, H.rowMouthLo, H.rowMouthHi, H.rowLipHi);
    for (int j = 0; j < 30; j++) printf("row %d: hidden %d / %d (front)\n", j, hiddenRow[j], totalRow[j]);
}
