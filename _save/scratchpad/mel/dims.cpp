#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
int main(int argc, char** argv) {
    for (int i = 0; i < 40; i++) {
        Anim::CharacterDesc d = Anim::randomCharacter(1000 + i * 7919, i % 7);
        Anim::detail::BodyDims D;
        Anim::detail::computeDims(d, D);
        printf("%2d g=%d anc=%d age=%.2f faceW %.3f jawW %.3f chinP %.2f chinH %.3f faceH %.3f phil %.2f cheekB %.2f cheekH %.4f eyeSize %.3f noseL %.2f noseW %.2f jawFlare %.2f chinSq %.2f browH %.2f headLen %.3f\n",
               i, (int)d.gender, D.ancestry, d.age, D.faceW, D.jawW, D.chinP, D.chinH, D.faceH, D.philtrum, D.cheekB, D.cheekH, D.eyeSize, D.noseL, D.noseW, D.jawFlare, D.chinSquare, D.browH, D.headLen);
    }
}
