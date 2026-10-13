// holdGrip's frame: for each hand, a handle axis and a palm direction, where do the fingers and the thumb point (model
// space, x right, y forward, z up)? Build: g++ -O2 -std=c++17 -I <tree>/src grip_test.cpp
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    CharacterDesc d = randomCharacter(1234u, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Pose base;
    sampleClip(sk, CLIP_IDLE, 0.f, base, 1u);
    for (int r = 0; r < 2; r++) {
        const float sx = r ? 1.f : -1.f;
        vec3 palm(sx, 0.f, 0.f);   // towards a partner on that side
        vec3 axes[4] = {vec3(0, 0, 1), vec3(0, 0, -1), vec3(0, 1, 0), vec3(0, -1, 0)};
        for (vec3 ax : axes) {
            Pose p = base;
            vec3 target(sx * 0.42f, 0.05f, 0.85f);
            holdGrip(sk, p, r == 1, target, ax, palm, vec3(sx * 0.35f, -0.1f, 0.4f), 0.5f, 0.5f, 1.f);
            mat4 m[B_COUNT];
            computeMatrices(sk, p, m, nullptr);
            int hb = r ? B_HAND_R : B_HAND_L, fb = r ? B_FINGERS_R : B_FINGERS_L, tb = r ? B_THUMB_R : B_THUMB_L;
            vec3 h = m[hb].c[3].xyz(), f = m[fb].c[3].xyz(), t = m[tb].c[3].xyz();
            vec3 fd = normalize(f - h), td = normalize(t - h);
            printf("%s hand  axis %5.1f %5.1f %5.1f  palm %5.1f: fingers %5.2f %5.2f %5.2f  thumb %5.2f %5.2f %5.2f\n", r ? "right" : "left ", ax.x, ax.y, ax.z,
                   palm.x, fd.x, fd.y, fd.z, td.x, td.y, td.z);
        }
    }
    return 0;
}
