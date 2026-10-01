// LOD build profile: time per stage of buildCharacterMeshLods over a set of characters.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
#include <chrono>
using namespace Anim;
using namespace Anim::detail;
#include <time.h>
// thread CPU time (the test machine is shared and heavily loaded: wall time would mostly measure the wait for a core)
static double now() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
int main(int argc, char** argv) {
    int N = argc > 1 ? atoi(argv[1]) : 16;
    double tb = 0, tg = 0, tlo = 0, ts1 = 0, td1 = 0, te1 = 0, ts2 = 0, td2 = 0, te2 = 0, te0 = 0;
    long tris0 = 0, tris1in = 0, tris2in = 0;
    long partTris[PART_COUNT] = {}, matTris[64] = {};
    for (int i = 0; i < N; i++) {
        CharacterDesc d = randomCharacter(5000u + (u32)i * 7919u, i % 8);
        Skeleton sk;
        buildSkeleton(d, sk);
        double t0 = now();
        MeshB cur;
        buildFinalMesh(d, sk, cur);
        double t1 = now();
        governLod0(cur);
        double t2 = now();
        tris0 += cur.idx.size() / 3;
        { MeshB m = cur; fixUvSeams(m); SkinnedMeshData o; emitMesh(m, o); }
        double t3 = now();
        computeLayerOffsets(cur);
        double t4 = now();
        stripForLod(cur, sk, 1);
        tris1in += cur.idx.size() / 3;
        for (size_t t = 0; t < cur.idx.size(); t += 3) {
            const BVert& v0 = cur.v[cur.idx[t]];
            partTris[v0.part]++;
            matTris[v0.mat & 63]++;
        }
        double t5 = now();
        float partW[2][PART_COUNT];
        for (int l = 0; l < 2; l++) for (int p = 0; p < PART_COUNT; p++) partW[l][p] = 1.f;
        partW[0][PART_HEAD] = 1.6f; partW[1][PART_HEAD] = 2.5f;
        partW[0][PART_EYE] = partW[1][PART_EYE] = 1e6f;
        partW[0][PART_FINGER] = partW[0][PART_THUMB] = partW[1][PART_FINGER] = partW[1][PART_THUMB] = 1e6f;
        decimateMesh(cur, 4500, partW[0]);
        double t6 = now();
        { MeshB m = cur; inflateLayers(m, 1); fixUvSeams(m); SkinnedMeshData o; emitMesh(m, o); }
        double t7 = now();
        stripForLod(cur, sk, 2);
        tris2in += cur.idx.size() / 3;
        double t8 = now();
        decimateMesh(cur, 1500, partW[1]);
        double t9 = now();
        { MeshB m = cur; inflateLayers(m, 2); fixUvSeams(m); SkinnedMeshData o; emitMesh(m, o); }
        double t10 = now();
        tb += t1 - t0; tg += t2 - t1; te0 += t3 - t2; tlo += t4 - t3; ts1 += t5 - t4; td1 += t6 - t5; te1 += t7 - t6; ts2 += t8 - t7; td2 += t9 - t8; te2 += t10 - t9;
    }
    double k = 1000.0 / N;
    printf("per character (ms): build %.1f govern %.1f emit0 %.1f layerOff %.1f strip1 %.1f dec1 %.1f emit1 %.1f strip2 %.1f dec2 %.1f emit2 %.1f | total %.1f (lods %.1f)\n",
           tb * k, tg * k, te0 * k, tlo * k, ts1 * k, td1 * k, te1 * k, ts2 * k, td2 * k, te2 * k,
           (tb + tg + te0 + tlo + ts1 + td1 + te1 + ts2 + td2 + te2) * k, (tlo + ts1 + td1 + te1 + ts2 + td2 + te2) * k);
    printf("tris: lod0 %ld, into dec1 %ld, into dec2 %ld (avg)\n", tris0 / N, tris1in / N, tris2in / N);
    const char* pn[] = {"torso", "neck", "head", "arm", "hand", "finger", "thumb", "leg", "ear", "eye", "facedetail", "mouth", "garment", "hair", "acc"};
    printf("dec1 input by part:");
    for (int p = 0; p < PART_COUNT; p++) printf(" %s %ld", pn[p], partTris[p] / N);
    printf("\nby material:");
    for (int k = 0; k < 64; k++) if (matTris[k]) printf(" m%d %ld", k, matTris[k] / N);
    printf("\n");
}
