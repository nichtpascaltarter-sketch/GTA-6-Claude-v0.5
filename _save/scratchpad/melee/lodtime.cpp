#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    double tb = 0, t1 = 0, t2 = 0;
    int n = 8;
    for (int i = 0; i < n; i++) {
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        double a = TimeSeconds();
        MeshB m;
        buildFinalMesh(d, sk, m);
        double b = TimeSeconds();
        stripForLod(m, 1);
        decimateMesh(m, 4500);
        double c = TimeSeconds();
        stripForLod(m, 2);
        decimateMesh(m, 1500);
        double e = TimeSeconds();
        tb += b - a; t1 += c - b; t2 += e - c;
    }
    printf("build %.1f ms, lod1 %.1f ms, lod2 %.1f ms\n", tb * 1000 / n, t1 * 1000 / n, t2 * 1000 / n);
}
