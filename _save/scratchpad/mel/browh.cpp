#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
#include <cstdio>
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    u32 seeds[] = {191056, 1000, 8919, 16838, 1024, 2024};
    int roles[] = {3, 0, 1, 2, 0, 0};
    for (int k = 0; k < 6; k++) {
        CharacterDesc d = randomCharacter(seeds[k], roles[k]);
        Skeleton sk; buildSkeleton(d, sk);
        BodyDims D; computeDims(d, D);
        BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = saturate(d.skinTone); c.lipCol = c.skin; c.palmCol = c.skin;
        buildBody(c);
        const HeadInfo& H = c.head;
        vec3 eye = H.eyeC[1];
        // upper lid margin above the pupil: rowEyeHi vertex closest in x to the eye centre (right side)
        float lidZ = -1e9f, bx = 1e9f;
        for (int kk = 0; kk < H.cols; kk++) {
            const BVert& v = c.m.v[H.grid[(size_t)H.rowEyeHi * H.cols + kk]];
            if (v.p.x > 0.f && fabsf(v.p.x - eye.x) < bx) { bx = fabsf(v.p.x - eye.x); lidZ = v.p.z; }
        }
        float browLo = 1e9f, browHi = -1e9f;
        for (const BVert& v : c.m.v)
            if (cardKind(v) == CARD_BROW && v.p.x > 0.f && fabsf(v.p.x - eye.x) < 0.004f) { browLo = Min(browLo, v.p.z); browHi = Max(browHi, v.p.z); }
        printf("seed %u: C z %.4f  eye z %.4f  lid z %.4f  brow z %.4f..%.4f  => brow-lid %.1f mm, eye-C elev %.1f deg\n", seeds[k], H.C.z, eye.z, lidZ, browLo, browHi,
               (browLo - lidZ) * 1000.f, atan2f(eye.z - H.C.z, eye.y - H.C.y) / kDegToRad);
    }
}
