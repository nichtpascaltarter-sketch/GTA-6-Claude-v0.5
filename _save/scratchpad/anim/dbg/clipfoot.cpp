// Sole heights of a gait clip sampled directly on the reference skeleton and on a character's, around a phase window.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
static void soles(const Skeleton& sk, const Pose& p, int s, float& hz, float& bz) {
    mat4 m[B_COUNT];
    computeMatrices(sk, p, m, nullptr);
    int fb = s ? B_FOOT_R : B_FOOT_L;
    float ball = sk.bindLocalPos[B_TOE_L].y, heel = ball * (0.21f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z + sk.bindLocalPos[B_FOOT_L].z;
    hz = (m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH))).z;
    bz = (m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH))).z;
}
int main(int argc, char** argv) {
    int band = atoi(argv[1]);
    float p0 = atof(argv[2]), p1 = atof(argv[3]);
    int s = atoi(argv[4]);
    u32 seed = argc > 5 ? atoi(argv[5]) : 100;
    int gender = argc > 6 ? atoi(argv[6]) : 0;
    const detail::ClipLib& L = detail::clipLib();
    CharacterDesc d = randomCharacter(seed, gender);
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 5u);
    an.setCharacter(d);
    int clip = band >= 100 ? band - 100 : detail::gaitClip(an.gaitStyle, band);
    printf("gait style %d\n", an.gaitStyle);
    int st = detail::skeletonStyle(sk) > 0.5f ? 1 : 0;
    const Skeleton& R = L.ctx[st].sk;
    float dur = detail::clipInfoId(clip).duration;
    printf("clip %d duty %.3f dur %.3f style %d\n", clip, detail::clipDuty(clip), dur, st);
    for (float ph = p0; ph <= p1 + 1e-4f; ph += 0.01f) {
        Pose a, b;
        detail::sampleClipId(R, clip, ph * dur, a, 0);
        detail::sampleClipId(sk, clip, ph * dur, b, 0);
        float h0, b0, h1, b1;
        soles(R, a, s, h0, b0);
        soles(sk, b, s, h1, b1);
        printf("ph %.2f  ref heel %.4f ball %.4f   char heel %.4f ball %.4f\n", ph, h0, b0, h1, b1);
    }
}
