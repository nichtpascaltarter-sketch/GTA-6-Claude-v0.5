// The animator's hold with AnimInput::grabAxis: a walking ped holding a point beside its hip (a held hand) with the
// default (upright) axis and with the way ahead; where do the fingers and the palm point (model space)?
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/grip/tree/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    for (int side = 0; side < 2; side++)
        for (int mode = 0; mode < 2; mode++) {
            CharacterDesc d = randomCharacter(4321u, 0);
            Skeleton sk;
            buildSkeleton(d, sk);
            Animator an;
            an.init(&sk, 7u);
            AnimInput in;
            in.speed = 1.3f;
            in.localMoveDir = vec2(0, 1);
            const float sx = side ? 1.f : -1.f;
            in.grabTarget = vec3(sx * 0.42f, 0.05f, 0.78f);   // beside the hip, a partner's hand
            in.grabWeight = 1.f;
            in.grabAxis = mode ? vec3(0, 1, 0) : vec3(0, 0, 1);
            vec3 fsum(0), psum(0);
            int n = 0;
            for (int f = 0; f < 240; f++) {
                an.update(in, 1.f / 60.f, false);
                if (f < 120) continue;
                mat4 m[B_COUNT];
                computeMatrices(sk, an.pose, m, nullptr);
                int hb = side ? B_HAND_R : B_HAND_L, fb = side ? B_FINGERS_R : B_FINGERS_L;
                vec3 h = m[hb].c[3].xyz(), fi = m[fb].c[3].xyz();
                fsum = fsum + normalize(fi - h);
                n++;
            }
            vec3 fd = fsum / (float)n;
            printf("%s hand, axis %s: fingers (mean dir) %5.2f %5.2f %5.2f\n", side ? "right" : "left ", mode ? "way ahead" : "upright  ", fd.x, fd.y, fd.z);
        }
    return 0;
}
