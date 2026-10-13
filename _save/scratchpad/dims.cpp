#include "../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    for (int g = 0; g < 2; g++) {
        CharacterDesc d; d.seed = 12345; d.gender = g ? FEMALE : MALE; d.height = g ? 1.65f : 1.78f; d.weight = 0.45f; d.muscle = g ? 0.3f : 0.45f; d.age = 0.3f; d.shoes = 0;
        detail::BodyDims D; detail::computeDims(d, D);
        const char* names[] = {"ROOT","PELVIS","SPINE1","SPINE2","CHEST","NECK","HEAD","CLAV_L","UARM_L","FARM_L","HAND_L","CLAV_R","UARM_R","FARM_R","HAND_R","THIGH_L","CALF_L","FOOT_L","TOE_L","THIGH_R","CALF_R","FOOT_R","TOE_R","FING_L","THUMB_L","FING_R","THUMB_R","JAW","EYE_L","EYE_R"};
        printf("%s: s %.3f thigh %.3f shin %.3f upperArm %.3f forearm %.3f palm %.3f heelBack %.3f ballFwd %.3f toeFwd %.3f hipDepth %.3f chestDepth %.3f armAngle %.3f\n", g?"female":"male", D.s, D.thigh, D.shin, D.upperArm, D.forearm, D.palmLen, D.heelBack, D.ballFwd, D.toeFwd, D.hipDepth, D.chestDepth, D.armAngle);
        for (int b = 0; b < B_COUNT; b++) printf("  %-8s (%.3f %.3f %.3f)\n", names[b], D.J[b].x, D.J[b].y, D.J[b].z);
        printf("  armDir R (%.2f %.2f %.2f) palmN R (%.2f %.2f %.2f) thumbDir R (%.2f %.2f %.2f)\n", D.armDir[1].x, D.armDir[1].y, D.armDir[1].z, D.palmN[1].x, D.palmN[1].y, D.palmN[1].z, D.thumbDir[1].x, D.thumbDir[1].y, D.thumbDir[1].z);
    }
}
