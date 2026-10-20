#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
#include <chrono>
using namespace Anim;
int main() {
    CharacterDesc d = randomCharacter(11, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Pose p;
    sampleClip(sk, CLIP_WALK, 0.3f, p);
    static mat4 m[B_COUNT], s[B_COUNT];
    double best = 1e9;
    for (int rep = 0; rep < 7; rep++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        const int N = 100000;
        for (int i = 0; i < N; i++) { p.rot[B_SPINE1].x += 1e-9f; computeMatrices(sk, p, m, s); }
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
        if (us < best) best = us;
    }
    printf("computeMatrices best %.3f us (B_COUNT %d) check %f\n", best, (int)B_COUNT, s[B_COUNT - 1].c[3].x);
}
