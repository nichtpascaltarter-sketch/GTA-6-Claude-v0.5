#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main() {
    for (int g = 0; g < 2; g++) {
        CharacterDesc d = randomCharacter(21 + g, 0);
        d.gender = g ? FEMALE : MALE;
        Skeleton sk;
        buildSkeleton(d, sk);
        Pose p;
        mat4 m[B_COUNT];
        const Clip cs[] = {CLIP_DEATH_FRONT, CLIP_DEATH_BACK, CLIP_GET_UP_FRONT, CLIP_GET_UP_BACK, CLIP_SUNBATHE, CLIP_KNOCKOUT};
        for (Clip c : cs) {
            float t = (c == CLIP_GET_UP_FRONT || c == CLIP_GET_UP_BACK) ? 0.f : clipInfo(c).duration;
            sampleClip(sk, c, t, p);
            computeMatrices(sk, p, m, nullptr);
            float lo = 1e9f; int lb = -1;
            for (int b = 0; b < B_COUNT; b++) if (m[b].c[3].z < lo) lo = m[b].c[3].z, lb = b;
            printf("%s %-14s lowest %.4f (bone %d)\n", g ? "F" : "M", clipInfo(c).name, lo, lb);
        }
    }
}
