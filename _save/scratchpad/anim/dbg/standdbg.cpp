// Standing hands vs body: worst palm clearance per person, with the state at that frame.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
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
        float worst = 1e9f;
        char info[512] = "";
        for (int f = 0; f < 7200; f++) {
            an.update(in, dt);
            if (f < 60 || f % 3) continue;
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
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
                    vec3 pel = m[B_PELVIS].c[3].xyz();
                    snprintf(info, sizeof(info), "t %.2f hand %c part %d bindpt (%.3f %.3f %.3f) palm-model (%.3f %.3f %.3f) pelvis (%.3f %.3f %.3f) standW %.2f idleVar %d w %.2f fidget %d w %.2f",
                             f * dt, s ? 'R' : 'L', bk, bl.x, bl.y, bl.z, palm.x, palm.y, palm.z, pel.x, pel.y, pel.z, an.standW, an.idleVar, an.idleVarW, an.fidgetVar, an.fidgetW);
                }
            }
        }
        printf("person %d (%s wt %.2f h %.2f age %.2f armOut %.3f heavyK %.2f): worst %.3f  %s\n", i, d.gender ? "F" : "M", d.weight, d.height, d.age, an.armOut, an.heavyK, worst, info);
    }
}
