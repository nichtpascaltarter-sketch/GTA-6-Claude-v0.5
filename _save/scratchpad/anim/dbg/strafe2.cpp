#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    const float dt = 1.f / 60.f;
    vec2 dir(atof(argv[1]), atof(argv[2]));
    float v = atof(argv[3]);
    CharacterDesc d = randomCharacter(77u, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 5u);
    an.setCharacter(d);
    AnimInput in;
    in.speed = v;
    in.localMoveDir = dir;
    in.footProbes = true;
    vec3 root(0);
    float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * (0.21f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
    vec3 pH[2], pB[2];
    double sum[2] = {0, 0};
    int cnt[2] = {0, 0};
    for (int f = 0; f < 420; f++) {
        root = root + vec3(dir.x, dir.y, 0.f) * (v * dt);
        an.update(in, dt);
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        for (int s = 0; s < 2; s++) {
            int fb = s ? B_FOOT_R : B_FOOT_L;
            vec3 hh = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
            vec3 bb = root + m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
            bool hl = hh.z < bb.z;
            vec3 a = hl ? hh : bb, pa = hl ? pH[s] : pB[s];
            if (f > 150 && a.z < 0.004f && (hl ? pH[s].z : pB[s].z) < 0.004f) {
                float sp = length(vec2(a.x - pa.x, a.y - pa.y)) / dt;
                sum[s] += sp; cnt[s]++;
                if (sp > 0.3f && getenv("SHOW")) printf("   f %d foot %d sp %.2f planted %d phase %.3f z %.4f\n", f, s, sp, (int)an.planted[s], an.phase, a.z);
            }
            pH[s] = hh; pB[s] = bb;
        }
        if (f < 150 || f > 200 || !getenv("TRACE")) continue;
        vec3 h = root + m[B_FOOT_L].c[3].xyz() + transformDir(m[B_FOOT_L], vec3(0.f, -heel, -ankH));
        vec3 b = root + m[B_FOOT_L].c[3].xyz() + transformDir(m[B_FOOT_L], vec3(0.f, ball, -ankH));
        if (0) printf("f %d phase %.3f moveW %.2f plantOn %.2f pl %d heel (%.3f %.3f %.3f) ball (%.3f %.3f %.3f) corr (%.3f %.3f) stepT %.2f\n", f, an.phase, an.moveW, an.plantOn, (int)an.planted[0], h.x, h.y, h.z, b.x, b.y, b.z, an.plantCorr[0].x, an.plantCorr[0].y, an.stepT[0]);
    }
    printf("skate L %.3f (%d) R %.3f (%d)\n", cnt[0] ? sum[0] / cnt[0] : 0.0, cnt[0], cnt[1] ? sum[1] / cnt[1] : 0.0, cnt[1]);
}
