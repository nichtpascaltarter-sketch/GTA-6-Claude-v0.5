// Largest angle between neighbouring head-grid normals round the eyes (the lid-row fans), per character
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 12;
    for (int i = 0; i < n; i++) {
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        detail::BodyDims D;
        detail::computeDims(d, D);
        detail::BuildCtx bc;
        bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
        detail::buildBody(bc);
        const detail::HeadInfo& H = bc.head;
        const int NC = H.cols;
        float worst = 0.f, sum = 0.f;
        int cnt = 0;
        for (int j = H.rowLidLo - 1; j <= H.rowBrow + 1; j++)
            for (int k = 0; k < NC; k++) {
                const detail::BVert& v = bc.m.v[H.grid[(size_t)j * NC + k]];
                float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
                if (fabsf(at - H.thetaEye) > 30.f * kDegToRad) continue;
                if (j == H.rowEyeHi || j == H.rowEyeLo || j + 1 == H.rowEyeLo || j - 1 == H.rowEyeHi) continue;   // the margins' own edges
                const detail::BVert& a = bc.m.v[H.grid[(size_t)(j + 1) * NC + k]];
                const detail::BVert& b = bc.m.v[H.grid[(size_t)j * NC + (k + 1) % NC]];
                float a1 = acosf(Clamp(dot(v.n, a.n), -1.f, 1.f)) * kRadToDeg, a2 = acosf(Clamp(dot(v.n, b.n), -1.f, 1.f)) * kRadToDeg;
                worst = Max(worst, Max(a1, a2));
                sum += a1 + a2;
                cnt += 2;
            }
        printf("c%-2d worst %.1f deg, mean %.2f deg\n", i, worst, sum / Max(cnt, 1));
    }
    return 0;
}
