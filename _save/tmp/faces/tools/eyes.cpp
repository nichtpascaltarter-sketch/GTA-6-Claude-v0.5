// prints eye heights of the viewer lineup characters (seed 1000 + 7919 i, role i % 7)
#include "../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        int i = atoi(argv[a]);
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        vec3 e = -sk.invBindModel[B_EYE_L].c[3].xyz();
        vec3 e2 = -sk.invBindModel[B_EYE_R].c[3].xyz();
        float lum = dot(d.skinTone, vec3(0.3f, 0.59f, 0.11f));
        printf("%d: seed %u role %d gender %d age %.2f h %.2f eyeL %.3f %.3f %.3f eyeR %.3f %.3f %.3f skinLum %.3f anc %d hair %d fh %d hat %d gl %d\n", i, d.seed, d.role, d.gender, d.age, d.height,
               e.x, e.y, e.z, e2.x, e2.y, e2.z, lum, d.ancestry, d.hairStyle, d.facialHair, d.hat, d.glasses);
    }
}
