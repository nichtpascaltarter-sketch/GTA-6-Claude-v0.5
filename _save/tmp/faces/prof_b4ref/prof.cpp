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
    const char* names[] = {"eyeballs","ears","brows","lids","mouth+?","spots/paint","hairLayer","facialHair","scalpCards","body","outfit","skinChannels","skinCh+AO"};
    printf("total %.1f ms/char\n", tt * 1000 / n);
    printf("governLod0   %.2f ms\n", Anim::detail::gProf[13] * 1000 / n); for (int i = 0; i < 12; i++) printf("%-12s %.2f ms\n", i == 4 ? "paint+spots" : (i == 5 ? "hairLayer" : (i == 6 ? "facialHair" : (i == 7 ? "scalpCards" : (i == 8 ? "body" : (i==9?"outfit":(i==10?"skinChannels":(i==11?"skinCh+AO":names[i]))))))), Anim::detail::gProf[i] * 1000 / n);
}
