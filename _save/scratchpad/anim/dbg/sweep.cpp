// Direction sweep: moving at v while the travel direction (relative to the facing) turns at w deg/s through a full
// circle; planted-foot skate (mean / worst frame) and the hip turn along the way.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    const float dt = 1.f / 60.f;
    float v = atof(argv[1]), w = atof(argv[2]) * kPi / 180.f;
    double sum = 0, worst = 0; int n = 0;
    for (int ci = 0; ci < 6; ci++) {
        CharacterDesc d = randomCharacter(300u + ci * 11u, ci & 1);
        Skeleton sk;
        buildSkeleton(d, sk);
        Animator an;
        an.init(&sk, 9u + ci);
        an.setCharacter(d);
        AnimInput in;
        in.footProbes = true;
        in.weaponKind = 1;
        in.aiming = true;
        vec3 root(0);
        float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * (0.21f / 0.52f);
        float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
        vec3 pa[2][2];
        bool was[2] = {false, false};
        float T = 1.f + kTwoPi / w;
        for (int f = 0; f < (int)(T / dt); f++) {
            float t = f * dt;
            float a = t < 1.f ? 0.f : (t - 1.f) * w;
            vec2 dir(sinf(a), cosf(a));
            in.localMoveDir = dir;
            in.speed = Min(v, t * 11.f);
            root = root + vec3(dir.x, dir.y, 0.f) * (in.speed * dt);
            an.update(in, dt);
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            for (int s = 0; s < 2; s++) {
                int fb = s ? B_FOOT_R : B_FOOT_L;
                vec3 hh = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
                vec3 bb = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
                if (t > 1.f && an.planted[s] && was[s]) {
                    bool hl = hh.z < bb.z;
                    vec3 c = hl ? hh : bb, p = hl ? pa[s][0] : pa[s][1];
                    float sp = length(vec2(c.x - p.x, c.y - p.y)) / dt;
                    sum += sp; n++;
                    worst = Max(worst, (double)sp);
                    if (sp > 0.3f && getenv("SHOW")) printf("  ch %d t %.2f dir %.0f deg foot %d skate %.2f m/s hipTurn %.2f back %d\n", ci, t, a * 180.f / kPi, s, sp, an.hipTurn, (int)an.hipBack);
                }
                pa[s][0] = hh; pa[s][1] = bb;
                was[s] = an.planted[s];
            }
            if (getenv("HIP") && ci == 0 && f % 15 == 0) printf("t %.2f dir %.0f hipTurn %.2f back %d\n", t, a * 180.f / kPi, an.hipTurn, (int)an.hipBack);
        }
    }
    printf("sweep v %.1f at %.0f deg/s: planted skate mean %.4f m/s, worst frame %.3f m/s (%d frames)\n", v, w * 180.f / kPi, n ? sum / n : 0.0, worst, n);
}
