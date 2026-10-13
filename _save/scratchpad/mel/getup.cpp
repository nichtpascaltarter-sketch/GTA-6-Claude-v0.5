#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
using namespace Anim;
int main() {
    for (int g = 0; g < 2; g++) {
        CharacterDesc d = randomCharacter(21 + g, 0);
        d.gender = g ? FEMALE : MALE;
        d.height = g ? 1.64f : 1.8f;
        Skeleton sk;
        buildSkeleton(d, sk);
        Pose p;
        sampleClip(sk, CLIP_GET_UP_FRONT, 0.f, p);
        mat4 m[B_COUNT];
        computeMatrices(sk, p, m, nullptr);
        for (int b = B_HAND_L; b < B_COUNT; b++) {
            if (b != B_HAND_L && b != B_HAND_R && b < B_FIRST_DERIVED) continue;
            if (m[b].c[3].z < 0.02f) printf("g%d bone %d z %.3f\n", g, b, m[b].c[3].z);
        }
        // palm normal of the right hand in model space
        vec3 fing = normalize(sk.bindLocalPos[B_FINGERS_R]);
        vec3 pn = normalize(cross(vec3(0, 1, 0), fing));
        mat3 R(m[B_HAND_R].c[0].xyz(), m[B_HAND_R].c[1].xyz(), m[B_HAND_R].c[2].xyz());
        vec3 pw = R * pn, fw = R * fing;
        printf("g%d right palm normal (%.2f %.2f %.2f) fingers (%.2f %.2f %.2f) wrist z %.3f\n", g, pw.x, pw.y, pw.z, fw.x, fw.y, fw.z, m[B_HAND_R].c[3].z);
    }
}
