#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main() {
    const int cs[] = {CLIP_WALK, CLIP_WALK_BACK, CLIP_STRAFE_L, CLIP_STRAFE_R, CLIP_CROUCH_WALK, CLIP_JOG};
    for (int c : cs) printf("%-12s duty %.3f  T %.3f speed %.2f stride %.3f\n", clipInfo((Clip)c).name, detail::clipDuty(c), clipInfo((Clip)c).duration, clipInfo((Clip)c).speed, clipInfo((Clip)c).duration * clipInfo((Clip)c).speed);
}
