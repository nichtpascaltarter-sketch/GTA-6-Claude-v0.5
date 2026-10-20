#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
struct FootProbe { vec3 heel, ball, toe; };
FootProbe footPoints(const Skeleton& sk, const mat4* m, int s) {
    int fb = s ? B_FOOT_R : B_FOOT_L;
    float ball = sk.bindLocalPos[s ? B_TOE_R : B_TOE_L].y, heel = ball * (0.21f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z +
                 sk.bindLocalPos[B_FOOT_L].z;
    FootProbe f;
    f.heel = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
    f.ball = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
    return f;
}
int main(int argc, char** argv) {
    const float dt = 1.f / 60.f;
    vec2 nd(atof(argv[1]), atof(argv[2]));
    u32 seed = argc > 3 ? atoi(argv[3]) : 80;
    float v = argc > 4 ? atof(argv[4]) : 1.2f;
    CharacterDesc d = randomCharacter(seed, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 5u);
    an.setCharacter(d);
    AnimInput in;
    in.footProbes = true;
    vec3 root(0);
    FootProbe prevF[2];
    bool prevOk[2] = {false, false};
    double sum = 0, sumS[2] = {0, 0};
    int n = 0, nS[2] = {0, 0};
    for (int f = 0; f < 420; f++) {
        float t = f * dt;
        vec2 dir = t < 1.5f ? vec2(0, 1) : nd;
        in.localMoveDir = dir;
        in.speed = v;
        root = root + vec3(dir.x, dir.y, 0.f) * (in.speed * dt);
        an.update(in, dt);
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        for (int s = 0; s < 2; s++) {
            FootProbe fpm = footPoints(sk, m, s), w;
            w.heel = root + fpm.heel;
            w.ball = root + fpm.ball;
            bool h = w.heel.z < w.ball.z;
            vec3 a = h ? w.heel : w.ball, b = h ? prevF[s].heel : prevF[s].ball;
            bool ok = a.z < 0.004f;
            if (ok && prevOk[s] && t > 2.5f) {
                float sp = length(vec2(a.x - b.x, a.y - b.y)) / dt;
                sum += sp; n++; sumS[s] += sp; nS[s]++;
                if (sp > 0.2f && getenv("SHOW")) printf("   f %d foot %d sp %.2f planted %d phase %.3f z %.4f %s hz %.4f bz %.4f\n", f, s, sp, (int)an.planted[s], an.phase, a.z, h ? "heel" : "ball", w.heel.z, w.ball.z);
            }
            if (getenv("TRACE") && s == atoi(getenv("TRACE")) && f >= 360 && f < 400) {
                float ph = an.phase - (s ? 0.5f : 0.f); ph -= floorf(ph);
                printf("f %d ph %.3f pl %d heel z %.4f ball z %.4f  hv %.2f bv %.2f\n", f, ph, (int)an.planted[s], w.heel.z, w.ball.z,
                       length(vec2(w.heel.x - prevF[s].heel.x, w.heel.y - prevF[s].heel.y)) / dt, length(vec2(w.ball.x - prevF[s].ball.x, w.ball.y - prevF[s].ball.y)) / dt);
            }
            prevF[s] = w;
            prevOk[s] = ok;
        }
    }
    printf("mean %.3f  L %.3f (%d) R %.3f (%d)\n", n ? sum / n : 0.0, nS[0] ? sumS[0] / nS[0] : 0.0, nS[0], nS[1] ? sumS[1] / nS[1] : 0.0, nS[1]);
}
