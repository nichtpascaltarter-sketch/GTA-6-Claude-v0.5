#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include <cstdio>
int main() {
    for (int i = 0; i < 24; i++) {
        Anim::CharacterDesc d = Anim::randomCharacter(1000 + i * 7919, i % 7);
        printf("i=%d hair=%d hat=%d\n", i, d.hairStyle, d.hat);
    }
    return 0;
}
