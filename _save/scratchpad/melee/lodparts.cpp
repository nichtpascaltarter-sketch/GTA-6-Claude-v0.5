#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
static void stats(const char* tag, const MeshB& m) {
    int cnt[PART_COUNT] = {};
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) cnt[m.v[m.idx[t]].part]++;
    const char* nm[] = {"torso","neck","head","arm","hand","finger","thumb","leg","ear","eye","facedet","mouth","garment","hair","acc"};
    printf("%s:", tag);
    for (int p = 0; p < PART_COUNT; p++) if (cnt[p]) printf(" %s %d", nm[p], cnt[p]);
    printf("  total %zu\n", m.idx.size() / 3);
}
int main() {
    for (int i = 0; i < 3; i++) {
        CharacterDesc d = randomCharacter(2000 + i * 7919, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        MeshB m;
        buildFinalMesh(d, sk, m);
        stats("lod0", m);
        float pw[2][PART_COUNT];
        for (int l = 0; l < 2; l++) for (int p = 0; p < PART_COUNT; p++) pw[l][p] = 1.f;
        pw[0][PART_HEAD] = 1.6f; pw[1][PART_HEAD] = 2.5f; pw[0][PART_EYE] = pw[1][PART_EYE] = 1e6f; pw[0][PART_FINGER] = pw[0][PART_THUMB] = 0.6f;
        double t0 = TimeSeconds();
        stripForLod(m, sk, 1); decimateMesh(m, 4500, pw[0]); stats("lod1", m);
        stripForLod(m, sk, 2); decimateMesh(m, 1500, pw[1]); stats("lod2", m);
        printf("  lods %.1f ms\n", (TimeSeconds() - t0) * 1000.0);
    }
}
