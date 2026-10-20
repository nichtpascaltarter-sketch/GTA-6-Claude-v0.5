#include "../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    int wrong = 0, n = 0;
    float minF = 1, maxM = 0;
    for (u32 sd = 1; sd < 400; sd++) {
        CharacterDesc d = randomCharacter(sd * 7919u, sd % 7);
        Skeleton sk; buildSkeleton(d, sk);
        float st = detail::skeletonStyle(sk);
        bool fem = d.gender == FEMALE;
        if (fem) minF = Min(minF, st); else maxM = Max(maxM, st);
        if ((st > 0.5f) != fem) wrong++;
        n++;
    }
    printf("style detection: %d/%d wrong, min female style %.2f, max male style %.2f\n", wrong, n, minF, maxM);
}
