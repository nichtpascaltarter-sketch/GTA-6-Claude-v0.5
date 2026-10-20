// Planted-foot skate while strafing / walking backwards (the root moves along localMoveDir).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main() {
    const float dt = 1.f / 60.f;
    const vec2 dirs[4] = {vec2(1, 0), vec2(-1, 0), vec2(0, -1), vec2(0.7071f, 0.7071f)};
    const char* names[4] = {"strafe right", "strafe left", "backwards", "diagonal"};
    for (int di = 0; di < 4; di++)
        for (float v : {0.9f, 1.4f}) {
            CharacterDesc d = randomCharacter(77u, 0);
            Skeleton sk;
            buildSkeleton(d, sk);
            Animator an;
            an.init(&sk, 5u);
            an.setCharacter(d);
            AnimInput in;
            in.speed = v;
            in.localMoveDir = dirs[di];
            in.footProbes = true;
            vec3 root(0);
            vec3 prevH[2], prevB[2];
            bool prevOk[2] = {false, false};
            double sum = 0; int n = 0; float worst = 0;
            int bf = B_FOOT_L;
            float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * (0.21f / 0.52f);
            float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
            (void)bf;
            for (int f = 0; f < 360; f++) {
                root = root + vec3(dirs[di].x, dirs[di].y, 0.f) * (v * dt);
                an.update(in, dt);
                mat4 m[B_COUNT];
                computeMatrices(sk, an.pose, m, nullptr);
                for (int s = 0; s < 2; s++) {
                    int fb = s ? B_FOOT_R : B_FOOT_L;
                    vec3 h = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
                    vec3 b = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
                    bool hl = h.z < b.z;
                    vec3 a = hl ? h : b, pa = hl ? prevH[s] : prevB[s];
                    bool ok = a.z < 0.004f;
                    if (ok && prevOk[s] && f > 90) {
                        float sp = length(vec2(a.x - pa.x, a.y - pa.y)) / dt;
                        sum += sp; n++; worst = Max(worst, sp);

                    }
                    prevH[s] = h;
                    prevB[s] = b;
                    prevOk[s] = ok;
                }
            }
            printf("%-13s %.1f m/s: planted-foot skate mean %.3f m/s, worst %.3f m/s (%d contact frames)\n", names[di], v, n ? sum / n : 0.0, worst, n);
        }
}
