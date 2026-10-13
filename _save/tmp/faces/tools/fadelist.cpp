// buzz-cut people of the viewer: their fade / line-up (hair.cpp's hairParams)
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        int i = atoi(argv[a]);
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton sk; buildSkeleton(d, sk);
        detail::BodyDims D; detail::computeDims(d, D);
        detail::BuildCtx bc; bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone;
        detail::HairParams h = detail::hairParams(bc);
        printf("c%d style %d fade %.2f lineUp %d crownBald %.2f\n", i, d.hairStyle, h.fade, (int)h.lineUp, h.crownBald);
    }
}
