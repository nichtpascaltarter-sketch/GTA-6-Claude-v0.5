#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    int c = argc > 1 ? atoi(argv[1]) : (int)CLIP_WALK_BACK;
    CharacterDesc d = randomCharacter(77u, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    const ClipInfo& ci = clipInfo((Clip)c);
    for (int k = 0; k <= 20; k++) {
        float ph = k * 0.05f;
        Pose p;
        sampleClip(sk, (Clip)c, ph * ci.duration, p, 0u);
        mat4 m[B_COUNT];
        computeMatrices(sk, p, m, nullptr);
        vec3 l = m[B_FOOT_L].c[3].xyz(), r = m[B_FOOT_R].c[3].xyz();
        printf("phase %.2f  L (%.3f %.3f %.3f)  R (%.3f %.3f %.3f)\n", ph, l.x, l.y, l.z, r.x, r.y, r.z);
    }
    printf("duty %.3f speed %.2f T %.2f\n", detail::clipDuty(c), ci.speed, ci.duration);
}
