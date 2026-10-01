// Face half-widths (max |x| of skin in front of the ears) at heights relative to the eye line, and the neck.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
#include <cstdlib>
using namespace Anim;
int main(int argc, char** argv) {
    u32 seed = (u32)strtoul(argv[1], 0, 10);
    int role = atoi(argv[2]);
    CharacterDesc d = randomCharacter(seed, role);
    d.hat = -1; d.glasses = -1; d.facialHair = -1; d.hairStyle = 0;
    if (argc > 3) d.weight = (float)atof(argv[3]);
    Skeleton sk;
    buildSkeleton(d, sk);
    SkinnedMeshData m;
    buildCharacterMesh(d, sk, m);
    detail::BodyDims D;
    detail::computeDims(d, D);
    vec3 eye = (D.J[B_EYE_L] + D.J[B_EYE_R]) * 0.5f;
    printf("seed %u g %d w %.2f headS %.3f IPD %.1f mm\n", seed, (int)d.gender, d.weight, D.headS, length(D.J[B_EYE_L] - D.J[B_EYE_R]) * 1000);
    const float zs[] = {0.03f, 0.015f, 0.f, -0.015f, -0.03f, -0.045f, -0.06f, -0.075f, -0.09f, -0.105f, -0.12f, -0.14f, -0.16f};
    for (float dz : zs) {
        float w = 0.f, wAll = 0.f;
        for (auto& v : m.verts) {
            if ((v.mat & 255) != MAT_SKIN) continue;
            if (fabsf(v.pos.z - (eye.z + dz)) > 0.003f) continue;
            wAll = fmaxf(wAll, fabsf(v.pos.x));
            if (v.pos.y > eye.y - 0.045f) w = fmaxf(w, fabsf(v.pos.x));
        }
        printf("eye%+4.0f mm: front width %5.1f mm  (all skin %5.1f)\n", dz * 1000, 2 * w * 1000, 2 * wAll * 1000);
    }
}
