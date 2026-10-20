// Closest approach of the two shins (knee -> ankle) moving in a direction at a speed, aiming (several characters).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
static float segDist(vec3 p, vec3 a, vec3 b) {
    vec3 ab = b - a;
    float u = Saturate(dot(p - a, ab) / Max(length2(ab), 1e-8f));
    return length(a + ab * u - p);
}
int main(int argc, char** argv) {
    const float dt = 1.f / 60.f;
    float deg = atof(argv[1]), v = atof(argv[2]);
    vec2 dir(sinf(deg * kPi / 180.f), cosf(deg * kPi / 180.f));
    float worst = 1e9f;
    for (int ci = 0; ci < 8; ci++) {
        CharacterDesc d = randomCharacter(500u + ci * 13u, ci % 3);
        Skeleton sk;
        buildSkeleton(d, sk);
        Animator an;
        an.init(&sk, 3u + ci);
        an.setCharacter(d);
        AnimInput in;
        in.footProbes = true;
        in.weaponKind = 1;
        in.aiming = true;
        in.localMoveDir = dir;
        for (int f = 0; f < 300; f++) {
            in.speed = Min(v, f * dt * 11.f);
            an.update(in, dt);
            if (f < 120) continue;
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            vec3 kl = m[B_CALF_L].c[3].xyz(), al = m[B_FOOT_L].c[3].xyz(), kr = m[B_CALF_R].c[3].xyz(), ar = m[B_FOOT_R].c[3].xyz();
            for (int k = 0; k <= 6; k++) worst = Min(worst, segDist(lerp(kl, al, k / 6.f), kr, ar));
        }
    }
    printf("dir %.0f deg v %.1f: shins at least %.3f m apart\n", deg, v, worst);
}
