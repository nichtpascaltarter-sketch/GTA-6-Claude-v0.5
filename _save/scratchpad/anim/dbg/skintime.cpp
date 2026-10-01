#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
#include <time.h>
using namespace Anim;
using namespace Anim::detail;
static double now() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main() {
    double tDims = 0, tPrims = 0, tCast = 0;
    for (int i = 0; i < 40; i++) {
        CharacterDesc d = randomCharacter(100u + i * 7u, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        double t0 = now();
        BodyDims D;
        computeDims(d, D);
        double t1 = now();
        BuildCtx bc;
        bc.d = &d; bc.D = &D; bc.sk = &sk;
        addBodyPrims(bc);
        double t2 = now();
        const u32 mk = MK_TORSO | MK_LEG_L | MK_LEG_R;
        float s = D.s;
        vec3 a = vec3(0, 0, D.zHip + 0.07f * s);
        float tb = bc.sdf.castOut(a, vec3(0, -1, 0), mk, 0.5f);
        float tf = bc.sdf.castOut(vec3(0.06f * s, 0, D.zWaist + 0.05f * s), vec3(0, 1, 0), mk, 0.5f);
        float ts = bc.sdf.castOut(vec3(0, D.J[B_THIGH_R].y + 0.015f * s, D.J[B_THIGH_R].z + 0.1f * s), vec3(1, 0, 0), mk, 0.5f);
        double t3 = now();
        tDims += t1 - t0; tPrims += t2 - t1; tCast += t3 - t2;
        if (i < 12) printf("%2d %s wt %.2f h %.2f: back %.3f belly %.3f flank %.3f (prims %zu) waistDepth %.3f glute %.3f belly %.3f hipHalfW %.3f\n", i, d.gender ? "F" : "M", d.weight, d.height, tb, tf, ts, bc.sdf.prims.size(), D.waistDepth, D.glute, D.belly, D.hipHalfW);
    }
    printf("per character: dims %.1f us, prims %.1f us, 3 casts %.1f us\n", tDims / 40 * 1e6, tPrims / 40 * 1e6, tCast / 40 * 1e6);
}
