#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main() {
    struct R { u32 seed; int g; } rs[2] = {{0xA11CEu, 1}, {0xDE7u, 0}};
    for (auto r : rs) {
        u32 seed = r.seed;
        CharacterDesc d = randomCharacter(seed, 0);
        for (int k = 0; k < 24 && (int)d.gender != r.g; k++) { seed = hash32(seed + 0x9e37u); d = randomCharacter(seed, 0); }
        printf("seed %x: gender %d top %d bottom %d shoes %d outer %d (fits %d) bag %d extras %08x hat %d glasses %d hair %d\n", r.seed, d.gender, d.top, d.bottom, d.shoes, d.outer, (int)detail::outerFits(d), d.bag, d.extras, d.hat, d.glasses, d.hairStyle);
    }
}
