#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main() {
    double t0 = TimeSeconds();
    int n = 24;
    for (int k = 0; k < n; k++) {
        CharacterDesc d = randomCharacter(1000 + k * 7919, k % 7);
        Skeleton sk; buildSkeleton(d, sk);
        SkinnedMeshData m; buildCharacterMesh(d, sk, m);
    }
    double tt = TimeSeconds() - t0;
    const char* names[] = {"eyeballs","ears","brows","lids","paint+spots","hairLayer(all)","facialHair","scalpCards","body(incl face)","outfit","skinChannels","skinCh+AO","mouth"};
    printf("total %.1f ms/char\n", tt * 1000 / n);
    for (int i = 0; i < 13; i++) printf("%-16s %.2f ms\n", names[i], Anim::detail::gProf[i] * 1000 / n);
}
