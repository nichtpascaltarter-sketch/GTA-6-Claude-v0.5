#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    struct B { const char* n; int fem; float h, wt, mu; } bodies[] = {{"refM", 0, 1.78f, 0.45f, 0.45f}, {"refF", 1, 1.65f, 0.45f, 0.3f}, {"heavyM", 0, 1.76f, 0.9f, 0.4f}, {"thinF", 1, 1.6f, 0.1f, 0.2f}};
    for (auto& b : bodies) {
        CharacterDesc d = randomCharacter(777u, 0);
        d.gender = b.fem ? FEMALE : MALE; d.height = b.h; d.weight = b.wt; d.muscle = b.mu; d.age = 0.3f;
        Skeleton sk; buildSkeleton(d, sk);
        BodyDims D; computeDims(d, D);
        BuildCtx bc; bc.d = &d; bc.D = &D; bc.sk = &sk; addBodyPrims(bc);
        const u32 mk = MK_TORSO;
        float zc = D.J[B_CHEST].z;
        float front = bc.sdf.castOut(vec3(0, 0, zc), vec3(0, 1, 0), mk, 0.5f), back = bc.sdf.castOut(vec3(0, 0, zc), vec3(0, -1, 0), mk, 0.5f);
        float frontB = bc.sdf.castOut(vec3(0, 0, D.zChestLine), vec3(0, 1, 0), mk, 0.5f);
        printf("%-7s chest joint (%.3f %.3f %.3f) chestDepth %.3f bust %.3f  front %.3f back %.3f (at chest line front %.3f) r[CHEST] %.3f r[SPINE2] %.3f head z %.3f zChestLine %.3f arm %.3f\n", b.n, D.J[B_CHEST].x, D.J[B_CHEST].y, zc,
               D.chestDepth, D.bust, front, back, frontB, sk.boneRadius[B_CHEST], sk.boneRadius[B_SPINE2], D.J[B_HEAD].z, D.zChestLine, D.upperArm + D.forearm);
    }
}
