#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main() {
    CharacterDesc d = randomCharacter(12u, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk;
    detail::addBodyPrims(bc);
    printf("prims %zu hipHalfW %.3f\n", bc.sdf.prims.size(), D.hipHalfW);
    Pose p;
    sampleClip(sk, CLIP_IDLE, 0.f, p);
    mat4 m[B_COUNT];
    computeMatrices(sk, p, m, nullptr);
    for (int s = 0; s < 2; s++) {
        vec3 wr = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
        vec3 fingD = normalize(transformDir(m[s ? B_HAND_R : B_HAND_L], sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
        vec3 palm = wr + fingD * (0.45f * length(sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
        printf("side %d wrist (%.3f %.3f %.3f) palm (%.3f %.3f %.3f)\n", s, wr.x, wr.y, wr.z, palm.x, palm.y, palm.z);
        int bones[3] = {B_PELVIS, B_THIGH_L, B_THIGH_R};
        u32 masks[3] = {detail::MK_TORSO, detail::MK_LEG_L, detail::MK_LEG_R};
        for (int k = 0; k < 3; k++) {
            const mat4& M = m[bones[k]];
            vec3 dd = palm - M.c[3].xyz();
            vec3 local(dot(dd, M.c[0].xyz()), dot(dd, M.c[1].xyz()), dot(dd, M.c[2].xyz()));
            vec3 J = -sk.invBindModel[bones[k]].c[3].xyz();
            vec3 q = J + local;
            printf("   bone %d J (%.3f %.3f %.3f) mapped (%.3f %.3f %.3f) sdf %.3f  | direct sdf %.3f\n", bones[k], J.x, J.y, J.z, q.x, q.y, q.z,
                   bc.sdf.eval(q, masks[k]), bc.sdf.eval(palm, masks[k]));
        }
    }
    // sample the leg SDF along x at z = 0.85
    for (float x = -0.3f; x <= 0.31f; x += 0.05f) printf("x %.2f: legL %.3f legR %.3f torso %.3f all %.3f\n", x, bc.sdf.eval(vec3(x, 0, 0.85f), detail::MK_LEG_L),
        bc.sdf.eval(vec3(x, 0, 0.85f), detail::MK_LEG_R), bc.sdf.eval(vec3(x, 0, 0.85f), detail::MK_TORSO), bc.sdf.eval(vec3(x, 0, 0.85f), detail::MK_ALL));
}
