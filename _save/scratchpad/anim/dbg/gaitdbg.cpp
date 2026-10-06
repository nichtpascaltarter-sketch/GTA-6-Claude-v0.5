// Debug timeline of one foot while walking through the world.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    float v = argc > 1 ? atof(argv[1]) : 1.4f;
    u32 seed = argc > 2 ? atoi(argv[2]) : 12u;
    int gs = argc > 3 ? atoi(argv[3]) : -1;
    CharacterDesc d = randomCharacter(seed, 0);
    if (argc > 4) d.age = atof(argv[4]);
    if (argc > 5) d.gender = atoi(argv[5]) ? FEMALE : MALE;
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 5u);
    an.setCharacter(d);
    if (gs >= 0) an.gaitStyle = gs;
    const float dt = 1.f / 60.f;
    AnimInput in;
    in.speed = v;
    in.footProbes = true;
    float rootY = 0.f;
    vec3 prevH(0), prevB(0);
    float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * 0.21f / 0.52f;
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
    for (int f = 0; f < 60 * 4; f++) {
        rootY += v * dt;
        an.update(in, dt);
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        vec3 H = m[B_FOOT_L].c[3].xyz() + transformDir(m[B_FOOT_L], vec3(0, -heel, -ankH)) + vec3(0, rootY, 0);
        vec3 B = m[B_FOOT_L].c[3].xyz() + transformDir(m[B_FOOT_L], vec3(0, ball, -ankH)) + vec3(0, rootY, 0);
        if (f > 120)
            printf("f %3d ph %.3f pl %d corr %6.3f,%6.3f yaw %6.3f | heel z %6.3f vy %6.3f | ball z %6.3f vy %6.3f | moveW %.2f\n", f, an.phase, an.planted[0],
                   an.plantCorr[0].x, an.plantCorr[0].y, an.corrYaw[0], H.z, (H.y - prevH.y) / dt, B.z, (B.y - prevB.y) / dt, an.moveW);
        prevH = H;
        prevB = B;
    }
}
