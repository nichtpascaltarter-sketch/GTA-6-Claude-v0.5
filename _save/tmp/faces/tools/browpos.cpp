// Prints the brow's lower edge, the upper lid crease and margin heights (mm above the eye centre along the face, at
// the pupil's column) per character.
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 12;
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
        const detail::HeadInfo& H = bc.head;
        vec3 e = D.J[B_EYE_R];
        // the grid column nearest the pupil (right side)
        int best = 0;
        float bd = 1e9f;
        for (int k = 0; k < H.cols; k++) {
            float pa = bc.m.v[H.grid[(size_t)H.rowEyeHi * H.cols + k]].pa;
            if (pa > kPi) continue;
            if (fabsf(pa - H.thetaEye) < bd) { bd = fabsf(pa - H.thetaEye); best = k; }
        }
        auto zAt = [&](int j) { return (bc.m.v[H.grid[(size_t)j * H.cols + best]].p.z - e.z) * 1000.f; };
        // brow: the card vertices of kind 3 (brow) on the right side, lowest z near the pupil column
        float browLo = 1e9f, browHi = -1e9f;
        for (const detail::BVert& v : bc.m.v) {
            if (v.mat != MAT_HAIR || detail::cardKind(v) != detail::CARD_BROW) continue;
            if (fabsf(v.p.x - e.x) > 0.004f || (v.p.x > 0.f) != (e.x > 0.f)) continue;
            browLo = Min(browLo, (v.p.z - e.z) * 1000.f);
            browHi = Max(browHi, (v.p.z - e.z) * 1000.f);
        }
        printf("c%-2d fem %.1f: margin %+.1f  crease row %+.1f  fold row %+.1f  brow %+.1f..%+.1f mm (above the eye centre)\n", i, D.fem, zAt(H.rowEyeHi),
               zAt(H.rowEyeHi + 3), zAt(H.rowEyeHi + 4), browLo, browHi);
    }
    return 0;
}
