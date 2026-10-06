#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
#include <time.h>
static double cpuSeconds() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main() {
    const int NP = 24, NF = 600;
    std::vector<CharacterDesc> ds(NP);
    std::vector<Skeleton> sks(NP);
    for (int i = 0; i < NP; i++) { ds[i] = randomCharacter(700u + i * 37u, i % 7); buildSkeleton(ds[i], sks[i]); }
    const char* names[3] = {"walk hurt (limp, wounded, clutch)", "stand clutching", "flinching walk (hit every 0.5 s)"};
    for (int sc = 0; sc < 3; sc++) {
        double best = 1e9;
        for (int rep = 0; rep < 3; rep++) {
            std::vector<Animator> an(NP);
            for (int i = 0; i < NP; i++) { an[i].init(&sks[i], 100u + i); an[i].setCharacter(ds[i]); }
            AnimInput in;
            in.footProbes = true;
            in.speed = sc == 1 ? 0.f : 1.2f;
            if (sc == 0) { in.legHurt[1] = 1.f; in.wounded = 0.7f; in.clutch = WOUND_THIGH_R; }
            if (sc == 1) in.clutch = WOUND_BELLY;
            for (int f = 0; f < 60; f++) for (int i = 0; i < NP; i++) an[i].update(in, 1.f / 60.f);
            double t0 = cpuSeconds();
            for (int f = 0; f < NF; f++) {
                AnimInput st = in;
                if (sc == 2 && f % 30 == 0) { st.hitDir = vec3(0, -1, 0); st.hitStrength = 0.4f; st.hitBone = B_CHEST; }
                for (int i = 0; i < NP; i++) an[i].update(st, 1.f / 60.f);
            }
            best = Min(best, (cpuSeconds() - t0) / (NF * NP) * 1e6);
        }
        printf("%-36s %.2f us\n", names[sc], best);
    }
}
