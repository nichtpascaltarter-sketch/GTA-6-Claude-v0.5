// Scratch analysis: triangle budget by part / material for LOD0 characters.
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;

int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 14;
    static const char* partN[PART_COUNT] = {"torso", "neck", "head", "arm", "hand", "finger", "thumb", "leg", "ear", "eye", "facedet", "mouth", "garment", "hair", "acc"};
    double tot[PART_COUNT * 2] = {};
    double cards = 0, all = 0, garm = 0, skin = 0;
    for (int i = 0; i < n; i++) {
        u32 seed = 1000 + i * 7919;
        CharacterDesc d = randomCharacter(seed, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        MeshB fin;
        double t0 = TimeSeconds();
        buildFinalMesh(d, sk, fin);
        double t1 = TimeSeconds();
        int cnt[PART_COUNT * 2] = {};
        int nc = 0;
        for (size_t t = 0; t < fin.idx.size(); t += 3) {
            const BVert& v = fin.v[fin.idx[t]];
            if (cardKind(v) != CARD_NONE) { nc++; continue; }
            int sk2 = v.mat == MAT_SKIN ? 0 : 1;
            cnt[v.part * 2 + sk2]++;
        }
        int T = (int)fin.idx.size() / 3;
        printf("seed %u role %d top %d bot %d shoes %d hair %d fh %d hat %d: %d tris (%.0f ms), cards %d\n", seed, i % 7, d.top, d.bottom, d.shoes, d.hairStyle,
               d.facialHair, d.hat, T, (t1 - t0) * 1000.0, nc);
        for (int p = 0; p < PART_COUNT; p++)
            if (cnt[p * 2] || cnt[p * 2 + 1]) printf("   %-8s skin %5d other %5d\n", partN[p], cnt[p * 2], cnt[p * 2 + 1]);
        for (int k = 0; k < PART_COUNT * 2; k++) tot[k] += cnt[k];
        cards += nc;
        all += T;
    }
    printf("AVERAGE over %d: total %.0f, cards %.0f\n", n, all / n, cards / n);
    for (int p = 0; p < PART_COUNT; p++) printf("   %-8s skin %7.0f other %7.0f\n", partN[p], tot[p * 2] / n, tot[p * 2 + 1] / n);
    return 0;
}
