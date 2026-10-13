#include "../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    CharacterDesc d = randomCharacter(3, 0);
    Skeleton sk; buildSkeleton(d, sk);
    int c = atoi(argv[1]); float t0 = atof(argv[2]), t1 = atof(argv[3]);
    int bones[4] = {atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 0, 0, 0};
    Pose prev; sampleClip(sk, (Clip)c, t0, prev);
    for (float t = t0; t <= t1; t += 1.f / 60.f) {
        Pose p; sampleClip(sk, (Clip)c, t, p);
        mat4 m[B_COUNT]; computeMatrices(sk, p, m, nullptr);
        printf("t=%.3f", t);
        for (int k = 0; k < 2; k++) {
            int b = bones[k];
            quat q = p.rot[b];
            float d = fabsf(q.x*prev.rot[b].x + q.y*prev.rot[b].y + q.z*prev.rot[b].z + q.w*prev.rot[b].w);
            printf("  b%d q(%.2f %.2f %.2f %.2f) step %.3f pos(%.2f %.2f %.2f)", b, q.x, q.y, q.z, q.w, 2.f*acosf(Min(1.f,d)), m[b].c[3].x, m[b].c[3].y, m[b].c[3].z);
        }
        printf("\n");
        prev = p;
    }
}
