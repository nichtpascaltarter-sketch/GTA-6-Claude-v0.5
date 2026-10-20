// ear vertex colours: the brightest ear vertices (region 4 = SR_EAR) and the ear's mean, for one character
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
    CharacterDesc d = argc > 1 && argv[1][0] == 'p' ? Game::protagonistDesc(atoi(argv[1] + 1)) : randomCharacter(1000 + atoi(argc > 1 ? argv[1] : "0") * 7919, atoi(argc > 1 ? argv[1] : "0") % 7);
    d.hat = -1;
    Skeleton sk; buildSkeleton(d, sk);
    SkinnedMeshData m; buildCharacterMesh(d, sk, m);
    std::vector<std::pair<float, int>> ear;
    vec3 sum(0); int n = 0;
    for (size_t i = 0; i < m.verts.size(); i++) {
        const VtxSkinned& v = m.verts[i];
        if ((v.mat & 0xffu) != MAT_SKIN || ((v.mat >> 9) & 7u) != 4u) continue;
        vec3 c((v.color & 255) / 255.f, ((v.color >> 8) & 255) / 255.f, ((v.color >> 16) & 255) / 255.f);
        sum += c; n++;
        ear.push_back(std::make_pair(dot(c, vec3(0.2126f, 0.7152f, 0.0722f)), (int)i));
    }
    std::sort(ear.begin(), ear.end());
    printf("%d ear vertices, mean colour %.3f %.3f %.3f\n", n, sum.x / n, sum.y / n, sum.z / n);
    for (int k = (int)ear.size() - 1; k >= 0 && k >= (int)ear.size() - 6; k--) {
        const VtxSkinned& v = m.verts[ear[k].second];
        vec3 nn = unpackNormalOct(v.normal);
        printf("bright: lum %.3f col %.3f %.3f %.3f pos %.3f %.3f %.3f n %.2f %.2f %.2f alpha %u\n", ear[k].first, (v.color & 255) / 255.f, ((v.color >> 8) & 255) / 255.f,
               ((v.color >> 16) & 255) / 255.f, v.pos.x, v.pos.y, v.pos.z, nn.x, nn.y, nn.z, v.color >> 24);
    }
    for (int k = 0; k < 3 && k < (int)ear.size(); k++) {
        const VtxSkinned& v = m.verts[ear[k].second];
        printf("dark: lum %.3f pos %.3f %.3f %.3f\n", ear[k].first, v.pos.x, v.pos.y, v.pos.z);
    }
}
