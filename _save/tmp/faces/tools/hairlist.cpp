// lists the viewer's people (randomCharacter(1000 + i * 7919, i % 7)): hair style, hat, sex, age, colours
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
#ifndef HAVE_CHARACTERS
#define HAVE_CHARACTERS 1
#endif
#include "src/game/protagonists.h"
using namespace Anim;
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 80;
    for (int w = 0; w < 2; w++) { CharacterDesc d = Game::protagonistDesc(w); printf("protagonist %d style %d hat %d\n", w, d.hairStyle, d.hat); }
    for (int i = 0; i < n; i++) {
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        printf("c%d style %d hat %d sex %d age %.2f fh %d skin %.2f,%.2f,%.2f hair %.3f,%.3f,%.3f anc %d\n", i, d.hairStyle, d.hat,
               (int)d.gender, d.age, d.facialHair, d.skinTone.x, d.skinTone.y, d.skinTone.z, d.hairColor.x, d.hairColor.y, d.hairColor.z, (int)d.ancestry);
    }
}
