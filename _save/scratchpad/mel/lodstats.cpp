#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
#include <chrono>
int main() {
    double sum[3] = {0, 0, 0}, mn[3] = {1e9, 1e9, 1e9}, mx[3] = {0, 0, 0}, tsum = 0;
    const int N = 24;
    for (int i = 0; i < N; i++) {
        Anim::CharacterDesc d = Anim::randomCharacter(0x1000u + i * 7919u, i % 7);
        Anim::Skeleton sk;
        Anim::buildSkeleton(d, sk);
        SkinnedMeshData m[3];
        auto t0 = std::chrono::steady_clock::now();
        Anim::buildCharacterMeshLods(d, sk, m, 3);
        tsum += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (int l = 0; l < 3; l++) {
            double t = m[l].indices.size() / 3.0;
            sum[l] += t; mn[l] = t < mn[l] ? t : mn[l]; mx[l] = t > mx[l] ? t : mx[l];
        }
    }
    for (int l = 0; l < 3; l++) printf("LOD%d tris avg %.0f min %.0f max %.0f\n", l, sum[l] / N, mn[l], mx[l]);
    printf("build all LODs avg %.0f ms\n", tsum * 1000 / N);
}
