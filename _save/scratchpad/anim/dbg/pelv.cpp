#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    int clip = atoi(argv[1]);
    const ClipLib& L = clipLib();
    const AuthorCtx& A = L.ctx[argc > 2 ? atoi(argv[2]) : 0];
    GaitP g;
    gaitParams(clip, g);
    float tab[kPelvisTab];
    walkPelvis(A, g, tab);
    GaitP g0 = g;
    for (int i = 0; i < 50; i += 2) {
        Rig r;
        gaitPose(A, g0, i / 96.f, r);
        printf("ph %.3f raw %.4f smooth %.4f\n", i / 96.f, r.pelvis.z, tab[i]);
    }
    float mn = 1, mx = -1; for (int i = 0; i < 96; i++) { mn = Min(mn, tab[i]); mx = Max(mx, tab[i]); }
    printf("smoothed range %.4f\n", mx - mn);
}
