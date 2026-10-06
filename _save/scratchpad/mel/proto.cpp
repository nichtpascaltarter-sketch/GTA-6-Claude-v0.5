#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
int main() {
    u32 seeds[2] = {0xA11CEu, 0xDE7u};
    int fg[2] = {1, 0};
    for (int i = 0; i < 2; i++) {
        u32 seed = seeds[i];
        Anim::CharacterDesc d = Anim::randomCharacter(seed, 0);
        for (int k = 0; k < 24 && (int)d.gender != fg[i]; k++) { seed = hash32(seed + 0x9e37u); d = Anim::randomCharacter(seed, 0); }
        printf("proto %d seed %u glasses %d hat %d hair %d fh %d\n", i, seed, d.glasses, d.hat, d.hairStyle, d.facialHair);
    }
}
