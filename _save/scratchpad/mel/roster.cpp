// The game's civilian roster (assets.cpp reqs 0..43) and the camfade pedestrian (randomCivilianChar(0x6a11, 0)).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
int main() {
    std::vector<int> fem, male;
    std::vector<u32> seeds;
    std::vector<int> roles;
    for (int i = 0; i < 44; i++) {
        u32 seed = 0x1000u + (u32)i * 7919u;
        int role = (i % 11 == 3) ? 3 : (i % 11 == 7 ? 5 : 0);
        int fg = i & 1;
        Anim::CharacterDesc d = Anim::randomCharacter(seed, role);
        for (int k = 0; k < 24 && (int)d.gender != fg; k++) {
            seed = hash32(seed + 0x9e37u);
            d = Anim::randomCharacter(seed, role);
        }
        seeds.push_back(seed);
        roles.push_back(role);
        (d.gender == Anim::FEMALE ? fem : male).push_back(i);
        printf("%2d seed %u role %d g %d age %.2f hair %d glasses %d\n", i, seed, role, (int)d.gender, d.age, d.hairStyle, d.glasses);
    }
    u32 s = 0x6a11u;
    const std::vector<int>& v = (s & 1) ? fem : male;
    int idx = v[(s >> 1) % v.size()];
    printf("camfade ped: roster %d seed %u role %d\n", idx, seeds[idx], roles[idx]);
}
