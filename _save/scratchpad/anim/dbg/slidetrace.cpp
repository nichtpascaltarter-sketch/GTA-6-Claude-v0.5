// Trace one person's foot around a time: sole points, planting state, sink, pin.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
namespace Anim { namespace detail { extern bool gDbgIK; } }
using namespace Anim;
int main(int argc, char** argv) {
    int i = atoi(argv[1]);
    float t0 = atof(argv[2]), t1 = atof(argv[3]);
    int s = argc > 4 ? atoi(argv[4]) : 1;
    const float dt = 1.f / 60.f;
    CharacterDesc d = randomCharacter(900u + (u32)i * 53u, i % 7);
    if (i == 3) d.age = 0.9f;
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 4000u + (u32)i * 17u);
    an.setCharacter(d);
    AnimInput in;
    in.footProbes = true;
    if (i == 5) in.stance = 23;
    int fb = s ? B_FOOT_R : B_FOOT_L;
    float ball = sk.bindLocalPos[s ? B_TOE_R : B_TOE_L].y, heel = ball * (0.21f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
    for (int f = 0; f < (int)(t1 / dt); f++) {
        float t = f * dt;
        Anim::detail::gDbgIK = t >= t0;
        an.update(in, dt);
        if (t < t0) continue;
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        vec3 h = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
        vec3 b = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
        vec3 hip = m[s ? B_THIGH_R : B_THIGH_L].c[3].xyz(), ank = m[fb].c[3].xyz();
        float L = length(sk.bindLocalPos[s ? B_CALF_R : B_CALF_L]) + length(sk.bindLocalPos[fb]);
        printf("t %.3f heel (%.4f %.4f %.4f) ball (%.4f %.4f %.4f) pl %d plantP (%.4f %.4f) corr (%.4f %.4f) cy %.3f sink %.4f reach %.4f standW %.3f idle %d w %.3f root (%.4f %.4f %.4f)\n", t, h.x, h.y, h.z, b.x, b.y, b.z,
               (int)an.planted[s], an.plantP[s].x, an.plantP[s].y, an.plantCorr[s].x, an.plantCorr[s].y, an.corrYaw[s], an.legSink, length(ank - hip) / L, an.standW, an.idleVar, an.idleVarW,
               an.pose.rootOffset.x, an.pose.rootOffset.y, an.pose.rootOffset.z);
    }
}
