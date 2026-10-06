#include "../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    CharacterDesc d = randomCharacter(1000, 0);
    Skeleton sk; buildSkeleton(d, sk);
    int clips[] = {14, 16, 15, 17};
    for (int c : clips) {
        for (float t : {0.0f, 0.02f, 0.05f}) {
            Pose p; sampleClip(sk, (Clip)c, t, p);
            mat4 m[B_COUNT]; computeMatrices(sk, p, m, nullptr);
            vec3 hr = m[B_HAND_R].c[3].xyz(), hl = m[B_HAND_L].c[3].xyz(), sr = m[B_UPPERARM_R].c[3].xyz();
            printf("%-12s t=%.2f handR (%.2f %.2f %.2f) handL (%.2f %.2f %.2f) shR (%.2f %.2f %.2f)\n", clipInfo((Clip)c).name, t, hr.x, hr.y, hr.z, hl.x, hl.y, hl.z, sr.x, sr.y, sr.z);
        }
    }
    // direct authoring check
    const detail::ClipLib& L = detail::clipLib();
    detail::Rig r; detail::aimPistolPose(L.ctx[0], r);
    printf("rig target R (%.2f %.2f %.2f) ik %d orient %d twist %.2f\n", r.arm[1].target.x, r.arm[1].target.y, r.arm[1].target.z, r.arm[1].ik, r.arm[1].orient, r.arm[1].twist);
    Pose p; detail::rigToPose(L.ctx[0], r, p);
    quat q; vec3 t; detail::boneModel(L.ctx[0].sk, p, B_HAND_R, q, t);
    printf("rigToPose handR (%.2f %.2f %.2f)\n", t.x, t.y, t.z);
    const detail::BakedClip& bc = L.clips[0][CLIP_AIM_PISTOL];
    printf("baked frames %d root0 (%.2f %.2f %.2f)\n", bc.frames, bc.root[0].x, bc.root[0].y, bc.root[0].z);
}
