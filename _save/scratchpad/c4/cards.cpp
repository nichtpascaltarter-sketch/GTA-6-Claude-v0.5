#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    for (int role = 0; role < 7; role++)
    for (int k = 0; k < 4; k++) {
        u32 seed = 1000 + role * 37 + k * 101;
        CharacterDesc d = randomCharacter(seed, role);
        Skeleton sk; buildSkeleton(d, sk);
        MeshB fin; buildFinalMesh(d, sk, fin);
        int cnt[5] = {0,0,0,0,0};
        for (size_t t = 0; t < fin.idx.size(); t += 3) cnt[cardKind(fin.v[fin.idx[t]])]++;
        printf("seed %u role %d hair %d fh %d: total %zu, scalp %d lash %d brow %d beard %d\n", seed, role, d.hairStyle, d.facialHair, fin.idx.size()/3, cnt[1], cnt[2], cnt[3], cnt[4]);
    }
}
