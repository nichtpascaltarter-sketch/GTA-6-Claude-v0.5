#include "../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    CharacterDesc d = randomCharacter(1000, 0);
    Skeleton sk; buildSkeleton(d, sk);
    int c = atoi(argv[1]); float t = atof(argv[2]);
    Pose p; sampleClip(sk, (Clip)c, t, p);
    mat4 m[B_COUNT]; computeMatrices(sk, p, m, nullptr);
    const char* names[] = {"ROOT","PELVIS","SPINE1","SPINE2","CHEST","NECK","HEAD","CLAV_L","UARM_L","FARM_L","HAND_L","CLAV_R","UARM_R","FARM_R","HAND_R","THIGH_L","CALF_L","FOOT_L","TOE_L","THIGH_R","CALF_R","FOOT_R","TOE_R","FING_L","THUMB_L","FING_R","THUMB_R","JAW","EYE_L","EYE_R"};
    for (int b = 0; b < B_COUNT; b++) {
        quat q = p.rot[b]; float ang = 2.f * acosf(Min(1.f, fabsf(q.w)));
        vec3 y = m[b].c[1].xyz();
        printf("%-8s pos (%.2f %.2f %.2f) localAng %.2f  yAxis (%.2f %.2f %.2f)\n", names[b], m[b].c[3].x, m[b].c[3].y, m[b].c[3].z, ang, y.x, y.y, y.z);
    }
}
