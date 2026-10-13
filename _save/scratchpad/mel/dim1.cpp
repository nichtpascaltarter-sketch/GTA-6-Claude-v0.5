#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    Anim::CharacterDesc d = Anim::randomCharacter((u32)strtoul(argv[1], 0, 10), atoi(argv[2]));
    Anim::detail::BodyDims D;
    Anim::detail::computeDims(d, D);
    printf("g=%d anc=%d age=%.2f H %.2f w %.2f headS %.3f faceW %.3f jawW %.3f chinP %.2f chinH %.3f faceH %.3f phil %.2f cheekB %.2f eyeSize %.3f eyeSpace %.3f noseL %.2f noseW %.2f noseP %.2f lipW %.2f lipFull %.2f jawFlare %.2f chinSq %.2f browH %.2f headLen %.3f foreheadH %.2f\n",
           (int)d.gender, D.ancestry, d.age, d.height, d.weight, D.headS, D.faceW, D.jawW, D.chinP, D.chinH, D.faceH, D.philtrum, D.cheekB, D.eyeSize, D.eyeSpace, D.noseL, D.noseW, D.noseP, D.lipW, D.lipFull, D.jawFlare, D.chinSquare, D.browH, D.headLen, D.foreheadH);
}
