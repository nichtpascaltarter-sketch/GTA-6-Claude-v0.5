// Per-ped animation cost: 24 characters x 900 frames per scenario, best of 5 runs (µs per Animator::update).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
#include <time.h>
// thread CPU time: robust on a loaded machine (time spent descheduled does not count)
static double cpuSeconds() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
int main() {
    const int NP = 24, NF = 900;
    std::vector<CharacterDesc> ds(NP);
    std::vector<Skeleton> sks(NP);
    for (int i = 0; i < NP; i++) {
        ds[i] = randomCharacter(700u + i * 37u, i % 7);
        buildSkeleton(ds[i], sks[i]);
    }
    Pose warm;
    sampleClip(sks[0], CLIP_WALK, 0.f, warm);
    const char* names[5] = {"walk near", "stand near", "walk far (cheap)", "stand far (cheap)", "walk near + ground probes"};
    for (int sc = 0; sc < 5; sc++) {
        double best = 1e9;
        for (int rep = 0; rep < 5; rep++) {
            std::vector<Animator> an(NP);
            for (int i = 0; i < NP; i++) {
                an[i].init(&sks[i], 100u + i);
#ifdef NEWAPI
                an[i].setCharacter(ds[i]);
#endif
            }
            AnimInput in;
            in.speed = (sc == 1 || sc == 3) ? 0.f : 1.35f;
            bool cheap = sc == 2 || sc == 3;
            // settle
            for (int f = 0; f < 60; f++)
                for (int i = 0; i < NP; i++) an[i].update(in, 1.f / 60.f, cheap);
            double t0 = cpuSeconds();
            for (int f = 0; f < NF; f++) {
                if (sc == 4) {
                    in.groundOffsetL = 0.03f * sinf(f * 0.05f);
                    in.groundOffsetR = -0.02f * sinf(f * 0.07f);
                }
                in.turnRate = 0.3f * sinf(f * 0.01f);
                for (int i = 0; i < NP; i++) an[i].update(in, 1.f / 60.f, cheap);
            }
            double t = (cpuSeconds() - t0) / (NF * NP) * 1e6;
            best = t < best ? t : best;
        }
        printf("%-28s %.2f us\n", names[sc], best);
    }
    mat4 ms[B_COUNT], skm[B_COUNT];
    Animator a;
    a.init(&sks[0], 1u);
    AnimInput in;
    in.speed = 1.35f;
    a.update(in, 1.f / 60.f);
    double best = 1e9;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = cpuSeconds();
        for (int f = 0; f < 20000; f++) computeMatrices(sks[f % NP], a.pose, ms, skm);
        double t = (cpuSeconds() - t0) / 20000 * 1e6;
        best = t < best ? t : best;
    }
    printf("%-28s %.2f us\n", "computeMatrices", best);
}
