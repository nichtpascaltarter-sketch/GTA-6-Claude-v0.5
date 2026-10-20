#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    for (int f = 0; f < 2; f++) {
        AuthorCtx A;
        makeAuthorCtx(A, f == 1);
        printf("%s s=%.3f footH=%.3f legLen=%.3f heelBack=%.3f ballFwd=%.3f\n", f ? "female" : "male", A.D.s, A.footH, A.legLen, A.heelBack, A.ballFwd);
        const char* names[] = {"root","pelvis","spine1","spine2","chest","neck","head","clavL","uarmL","farmL","handL","clavR","uarmR","farmR","handR","thighL","calfL","footL","toeL","thighR","calfR","footR","toeR","fingL","thumbL","fingR","thumbR","jaw","eyeL","eyeR"};
        for (int b = 0; b < B_COUNT; b++) printf("  %-7s J=(%.3f %.3f %.3f) len=%.3f\n", names[b], A.D.J[b].x, A.D.J[b].y, A.D.J[b].z, A.sk.boneLength[b]);
        printf("  armDirR=(%.3f %.3f %.3f) palmNR=(%.3f %.3f %.3f) upperArm %.3f forearm %.3f palmLen %.3f\n", A.D.armDir[1].x, A.D.armDir[1].y, A.D.armDir[1].z, A.D.palmN[1].x, A.D.palmN[1].y, A.D.palmN[1].z, A.D.upperArm, A.D.forearm, A.D.palmLen);
        Rig g; guardPose(A, g);
        Pose p; rigToPose(A, g, p);
        for (int b : {B_HAND_L, B_HAND_R, B_HEAD, B_CHEST, B_UPPERARM_R, B_PELVIS}) { quat q; vec3 t; boneModel(A.sk, p, b, q, t); printf("  guard %s at (%.3f %.3f %.3f)\n", names[b], t.x, t.y, t.z); }
    }
}
