// Standing: worst planted-foot slide and worst palm clearance per person, with the state at those frames.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
struct FP { vec3 heel, ball; };
static FP feet(const Skeleton& sk, const mat4* m, int s) {
    int fb = s ? B_FOOT_R : B_FOOT_L;
    float ball = sk.bindLocalPos[s ? B_TOE_R : B_TOE_L].y, heel = ball * (0.21f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
    FP f;
    f.heel = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
    f.ball = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
    return f;
}
int main(int argc, char** argv) {
    const float dt = 1.f / 60.f;
    int only = argc > 1 ? atoi(argv[1]) : -1;
    for (int i = 0; i < 10; i++) {
        if (only >= 0 && i != only) continue;
        CharacterDesc d = randomCharacter(900u + (u32)i * 53u, i % 7);
        if (i == 3) d.age = 0.9f;
        Skeleton sk;
        buildSkeleton(d, sk);
        detail::BodyDims D;
        detail::computeDims(d, D);
        detail::BuildCtx bc;
        bc.d = &d; bc.D = &D; bc.sk = &sk;
        detail::addBodyPrims(bc);
        detail::Sdf part[3];
        for (const detail::Prim& q : bc.sdf.prims) {
            if (q.mask & detail::MK_TORSO) part[0].prims.push_back(q);
            else if (q.mask & detail::MK_LEG_L) part[1].prims.push_back(q);
            else if (q.mask & detail::MK_LEG_R) part[2].prims.push_back(q);
        }
        Animator an;
        an.init(&sk, 4000u + (u32)i * 17u);
        an.setCharacter(d);
        AnimInput in;
        in.footProbes = true;
        if (i == 5) in.stance = 23;
        float worst = 1e9f, worstSk = 0.f;
        char info[512] = "", infoSk[512] = "";
        FP prev[2];
        bool prevOk[2] = {false, false};
        for (int f = 0; f < 7200; f++) {
            an.update(in, dt);
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            for (int s = 0; s < 2; s++) {
                FP w = feet(sk, m, s);
                bool h = w.heel.z < w.ball.z;
                vec3 a = h ? w.heel : w.ball, b = h ? prev[s].heel : prev[s].ball;
                bool ok = a.z < 0.004f;
                if (ok && prevOk[s] && f > 60) {
                    float sp = length(vec2(a.x - b.x, a.y - b.y)) / dt;
                    if (sp > worstSk) {
                        worstSk = sp;
                        snprintf(infoSk, sizeof(infoSk), "t %.2f foot %d %s z %.4f planted %d stepT %.2f %.2f plantOn %.2f standW %.2f idleVar %d w %.2f fidget %d t %.2f w %.2f", f * dt, s, h ? "heel" : "ball", a.z,
                                 (int)an.planted[s], an.stepT[0], an.stepT[1], an.plantOn, an.standW, an.idleVar, an.idleVarW, an.fidgetVar, an.fidgetT, an.fidgetW);
                    }
                }
                prev[s] = w;
                prevOk[s] = ok;
            }
            if (f < 60 || f % 3) continue;
            for (int s = 0; s < 2; s++) {
                vec3 wr = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
                vec3 fingD = normalize(transformDir(m[s ? B_HAND_R : B_HAND_L], sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                vec3 palm = wr + fingD * (0.45f * length(sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                float best = 1e9f;
                int bk = -1;
                vec3 bl;
                for (int k = 0; k < 3; k++) {
                    int bone = k == 0 ? B_PELVIS : (k == 1 ? B_THIGH_L : B_THIGH_R);
                    const mat4& M = m[bone];
                    vec3 dd = palm - M.c[3].xyz();
                    vec3 local(dot(dd, M.c[0].xyz()), dot(dd, M.c[1].xyz()), dot(dd, M.c[2].xyz()));
                    vec3 J = -sk.invBindModel[bone].c[3].xyz();
                    float dist = part[k].eval(J + local, detail::MK_ALL);
                    if (dist < best) best = dist, bk = k, bl = J + local;
                }
                best -= sk.boneRadius[s ? B_HAND_R : B_HAND_L] * 0.75f;
                if (best < worst) {
                    worst = best;
                    snprintf(info, sizeof(info), "t %.2f hand %c part %d bindpt (%.3f %.3f %.3f) standW %.2f idleVar %d t %.2f w %.2f fidget %d t %.2f w %.2f", f * dt, s ? 'R' : 'L', bk, bl.x, bl.y, bl.z,
                             an.standW, an.idleVar, an.idleVarT, an.idleVarW, an.fidgetVar, an.fidgetT, an.fidgetW);
                }
            }
        }
        printf("person %d (%s wt %.2f): palm %.3f  %s\n   slide %.3f m/s  %s\n", i, d.gender ? "F" : "M", d.weight, worst, info, worstSk, infoSk);
    }
}
