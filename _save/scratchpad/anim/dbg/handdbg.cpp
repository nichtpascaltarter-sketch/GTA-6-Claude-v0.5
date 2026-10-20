#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    float v = argc > 1 ? atof(argv[1]) : 1.4f;
    u32 seed = argc > 2 ? atoi(argv[2]) : 12u;
    CharacterDesc d = randomCharacter(seed, 0);
    if (argc > 3) d.age = atof(argv[3]);
    if (argc > 4) d.gender = atoi(argv[4]) ? FEMALE : MALE;
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
    printf("parts: torso %zu legL %zu legR %zu\n", part[0].prims.size(), part[1].prims.size(), part[2].prims.size());
    Animator an;
    an.init(&sk, 5u);
    an.setCharacter(d);
    AnimInput in;
    in.speed = v;
    for (int f = 0; f < 240; f++) {
        an.update(in, 1.f / 60.f);
        if (f < 120) continue;
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        for (int s = 0; s < 2; s++) {
            vec3 wr = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
            vec3 fingD = normalize(transformDir(m[s ? B_HAND_R : B_HAND_L], sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
            vec3 palm = wr + fingD * (0.45f * length(sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
            int mid3 = phalanxBone(s == 1, 1, 2);
            vec3 tip = m[mid3].c[3].xyz() + transformDir(m[mid3], normalize(sk.bindLocalPos[mid3]) * sk.boneLength[mid3]);
            vec3 pts[2] = {palm, tip};
            for (int q = 0; q < 2; q++)
                for (int k = 0; k < 3; k++) {
                    int bone = k == 0 ? B_PELVIS : (k == 1 ? B_THIGH_L : B_THIGH_R);
                    const mat4& M = m[bone];
                    vec3 dd = pts[q] - M.c[3].xyz();
                    vec3 local(dot(dd, M.c[0].xyz()), dot(dd, M.c[1].xyz()), dot(dd, M.c[2].xyz()));
                    vec3 J = -sk.invBindModel[bone].c[3].xyz();
                    float dist = part[k].eval(J + local, detail::MK_ALL);
                    if (dist < -0.02f) printf("f %d side %d %s part %d dist %.3f posed (%.3f %.3f %.3f) bind (%.3f %.3f %.3f)\n", f, s, q ? "tip" : "palm", k, dist,
                                              pts[q].x, pts[q].y, pts[q].z, (J + local).x, (J + local).y, (J + local).z);
                }
        }
    }
}
