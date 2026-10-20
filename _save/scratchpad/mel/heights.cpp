#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    for (int i = 0; i < 8; i++) {
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton sk; buildSkeleton(d, sk);
        vec3 eye = -sk.invBindModel[B_EYE_L].c[3].xyz();
        printf("%d: gender %d h %.3f eyeZ %.3f hair %d hat %d glasses %d fh %d top %d\n", i, d.gender, d.height, eye.z, d.hairStyle, d.hat, d.glasses, d.facialHair, d.top);
    }
}
