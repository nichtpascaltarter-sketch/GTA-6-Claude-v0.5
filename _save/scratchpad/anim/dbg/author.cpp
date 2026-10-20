// Author a gait frame directly: ankle targets vs solved ankle and the sole points (ball by the authoring's own offsets).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    int clip = atoi(argv[1]);
    float p0 = atof(argv[2]), p1 = atof(argv[3]);
    int s = atoi(argv[4]);
    const ClipLib& L = clipLib();
    const AuthorCtx& A = L.ctx[0];
    GaitP g;
    gaitParams(clip, g);
    printf("duty %.3f lift %.3f swingPeak %.2f\n", g.duty, g.lift, g.swingPeak);
    for (float ph = p0; ph <= p1 + 1e-4f; ph += 0.01f) {
        Rig r;
        gaitPose(A, g, ph, r);
        Pose p;
        rigToPose(A, r, p);
        mat4 m[B_COUNT];
        computeMatrices(A.sk, p, m, nullptr);
        int fb = s ? B_FOOT_R : B_FOOT_L;
        vec3 ank = m[fb].c[3].xyz();
        vec3 ball = ank + transformDir(m[fb], vec3(0.f, A.ballFwd, -A.footH));
        vec3 heel = ank + transformDir(m[fb], vec3(0.f, -A.heelBack, -A.footH));
        printf("ph %.2f pelvis z %.4f target ankle (%.3f %.3f %.4f) got (%.3f %.3f %.4f) heel z %.4f ball z %.4f pitch %.2f\n", ph, r.pelvis.z,
               r.leg[s].ankle.x, r.leg[s].ankle.y, r.leg[s].ankle.z, ank.x, ank.y, ank.z, heel.z, ball.z, r.leg[s].pitch);
    }
}
