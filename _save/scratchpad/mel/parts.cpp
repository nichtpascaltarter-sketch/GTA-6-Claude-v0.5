// Scratch: triangle counts per part for a few characters (LOD0 build pipeline pieces) + build timings.
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include <ctime>
static double cpuS() { return (double)std::clock() / CLOCKS_PER_SEC; }
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 12;
    u32 base = argc > 2 ? (u32)atoi(argv[2]) : 1000;
    const char* names[] = {"torso","neck","head","arm","hand","finger","thumb","leg","ear","eye","facedet","mouth","garment","hair","acc","p15","p16","p17"};
    double sum[20] = {0};
    double tFull = 0, tLods = 0, tBody = 0, tOut = 0; size_t trisFull = 0, l1 = 0, l2 = 0;
    for (int i = 0; i < n; i++) {
        CharacterDesc d = randomCharacter(base + i * 7919, i % 7);
        Skeleton sk; buildSkeleton(d, sk);
        BodyDims D; computeDims(d, D);
        BuildCtx c; c.d = &d; c.D = &D; c.sk = &sk; c.skin = saturate(d.skinTone); c.lipCol = c.skin; c.palmCol = c.skin;
        double tb0 = cpuS();
        buildBody(c);
        double tb1 = cpuS();
        MeshB extra; std::vector<u8> hide(c.m.idx.size() / 3, 0);
        buildOutfit(c, extra, hide);
        double tb2 = cpuS();
        tBody += tb1 - tb0; tOut += tb2 - tb1;
        int cnt[20] = {0};
        for (size_t t = 0; t < c.m.idx.size() / 3; t++) if (!hide[t]) cnt[c.m.v[c.m.idx[t * 3]].part]++;
        int hairT = 0, outT = 0;
        for (size_t t = 0; t < extra.idx.size() / 3; t++) { if (extra.v[extra.idx[t * 3]].mat == MAT_HAIR) hairT++; else outT++; }
        cnt[PART_HAIR] = hairT; cnt[PART_GARMENT] = outT; cnt[PART_ACC] = 0;
        printf("seed %u role %d g%d hair %d fh %d hat %d top %d bot %d:", d.seed, d.role, d.gender, d.hairStyle, d.facialHair, d.hat, d.top, d.bottom);
        int tot = 0;
        for (int p = 0; p < PART_COUNT; p++) { printf(" %s %d", names[p], cnt[p]); sum[p] += cnt[p]; tot += cnt[p]; }
        printf(" | total %d\n", tot);
        SkinnedMeshData m; double t0 = cpuS(); buildCharacterMesh(d, sk, m); tFull += cpuS() - t0; trisFull += m.indices.size() / 3;
        SkinnedMeshData L[3]; t0 = cpuS(); buildCharacterMeshLods(d, sk, L, 3); tLods += cpuS() - t0; l1 += L[1].indices.size() / 3; l2 += L[2].indices.size() / 3;
    }
    printf("avg:");
    double headSum = 0;
    for (int p = 0; p < PART_COUNT; p++) printf(" %s %.0f", names[p], sum[p] / n);
    headSum = (sum[PART_HEAD] + sum[PART_EAR] + sum[PART_EYE] + sum[PART_FACEDETAIL] + sum[PART_MOUTH]) / n;
    printf("\nhead(grid+ears+eyes+detail+mouth) %.0f  hair %.0f\n", headSum, sum[PART_HAIR] / n);
    printf("buildBody %.1f ms, buildOutfit %.1f ms\n", tBody * 1000 / n, tOut * 1000 / n);
    printf("LOD0 avg %zu tris, build %.1f ms | LODs %zu / %zu, build all %.1f ms\n", trisFull / n, tFull * 1000 / n, l1 / n, l2 / n, tLods * 1000 / n);
}
