#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    for (int i = 0; i < 12; i++) {
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton sk; buildSkeleton(d, sk);
        SkinnedMeshData L[3];
        buildCharacterMeshLods(d, sk, L, 3);
    }
}
