#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
int main() {
    for (int i = 0; i < 40; i++) {
        Anim::CharacterDesc d = Anim::randomCharacter(1000 + i * 7919, i % 7);
        printf("%2d g=%d age=%.2f H=%.2f hair=%d fh=%d hat=%d gl=%d top=%d skin=%.2f,%.2f,%.2f anc=%d\n", i, (int)d.gender, d.age, d.height, d.hairStyle, d.facialHair, d.hat, d.glasses, d.top,
               d.skinTone.x, d.skinTone.y, d.skinTone.z, d.ancestry);
    }
}
