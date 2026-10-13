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
        SkinnedMeshData m; buildCharacterMesh(d, sk, m);
        int tris[6] = {0,0,0,0,0,0}, eye = 0, skin = 0, total = (int)m.indices.size() / 3;
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            const VtxSkinned& v = m.verts[m.indices[t]];
            u32 mat = v.mat & 0xff, prm = (v.mat >> 8) & 0x7fffff;
            if (mat == MAT_HAIR) tris[prm & 15]++;
            else if (mat == MAT_EYE) eye++;
            else if (mat == MAT_SKIN) skin++;
        }
        printf("c%d hair %d fh %d: total %d | shell %d scalp %d lash %d brow %d beard %d | eye %d skin %d\n", i, d.hairStyle, d.facialHair, total, tris[0], tris[1], tris[2], tris[3], tris[4], eye, skin);
    }
}
