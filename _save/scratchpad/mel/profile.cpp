// Midline profile of a character's face: max forward (y) of skin vertices within |x| < 1.5 mm per 2 mm z bin, relative
// to the eye joint height, plus face widths at a few heights (max |x| of skin with y > head joint y + 2 cm).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
#include <cstdlib>
using namespace Anim;
int main(int argc, char** argv) {
    int i = 0;
    CharacterDesc d = randomCharacter((u32)strtoul(argv[1], 0, 10), atoi(argv[2]));
    d.hat = -1; d.glasses = -1; d.facialHair = -1;
    Skeleton sk;
    buildSkeleton(d, sk);
    SkinnedMeshData m;
    buildCharacterMesh(d, sk, m);
    detail::BodyDims D;
    detail::computeDims(d, D);
    float ez = D.J[B_EYE_L].z, hy = D.J[B_HEAD].y;
    const int NB = 140;
    float prof[NB], wid[NB];
    for (int b = 0; b < NB; b++) { prof[b] = -1; wid[b] = 0; }
    for (auto& v : m.verts) {
        if ((v.mat & 255) != MAT_SKIN) continue;
        float dz = v.pos.z - ez;
        int b = (int)floorf((dz + 0.14f) / 0.001f);
        if (b < 0 || b >= NB) continue;
        if (fabsf(v.pos.x) < 0.004f) prof[b] = fmaxf(prof[b], v.pos.y - hy);
        if (v.pos.y > hy + 0.0f) wid[b] = fmaxf(wid[b], fabsf(v.pos.x));
    }
    printf("char %d g=%d H %.2f headS %.3f\n", i, (int)d.gender, d.height, D.headS);
    for (int b = NB - 1; b >= 0; b--) {
        float z = -0.14f + b * 0.001f;
        if (z > 0.03f) continue;
        printf("z %+6.1f mm  fwd %6.1f  halfW %5.1f  ", z * 1000, prof[b] * 1000, wid[b] * 1000);
        int n = prof[b] > 0 ? (int)((prof[b] - 0.06f) * 1000) : 0;
        for (int k = 0; k < n && k < 80; k++) putchar('#');
        putchar('\n');
    }
}
