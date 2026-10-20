// Scratch: verify strand-card conventions on the emitted LOD meshes.
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    int n = argc > 1 ? atoi(argv[1]) : 8;
    for (int i = 0; i < n; i++) {
        CharacterDesc d = randomCharacter(3000 + i * 7919, i % 7);
        Skeleton sk; buildSkeleton(d, sk);
        SkinnedMeshData L[3];
        buildCharacterMeshLods(d, sk, L, 3);
        for (int l = 0; l < 3; l++) {
            const SkinnedMeshData& m = L[l];
            size_t nt = m.indices.size() / 3;
            int kinds[16] = {0};
            size_t firstCard = nt; bool ordered = true; bool sawCard = false;
            float maxTanDot = 0.f;
            for (size_t t = 0; t < nt; t++) {
                const VtxSkinned& v = m.verts[m.indices[t * 3]];
                u32 mat = v.mat & 0xff, kind = (v.mat >> 8) & 15u;
                bool card = mat == MAT_HAIR && kind != 0;
                if (card) { kinds[kind]++; if (!sawCard) firstCard = t; sawCard = true; }
                else if (sawCard) ordered = false;
                if (card) {
                    vec3 nn = unpackNormalOct(v.normal), tt = unpackNormalOct(v.tangent);
                    maxTanDot = Max(maxTanDot, fabsf(dot(nn, tt)));
                }
            }
            printf("char %d lod %d: tris %zu cards: scalp %d lash %d brow %d beard %d, first card tri %zu, cards last %s, max |n.t| %.3f\n",
                   i, l, nt, kinds[1], kinds[2], kinds[3], kinds[4], firstCard, ordered ? "yes" : "NO", maxTanDot);
        }
        if (i == 0) {
            // one card vertex pair
            for (size_t v = 0; v < L[0].verts.size(); v++) {
                const VtxSkinned& x = L[0].verts[v];
                if ((x.mat & 0xff) == MAT_HAIR && ((x.mat >> 8) & 15u) == 1) {
                    vec4 cc = unpackRGBA8(x.color);
                    vec3 tt = unpackNormalOct(x.tangent);
                    printf("scalp card vertex: mat 0x%08x (kind %u seed %u) uv (%.2f %.2f) tangent (%.2f %.2f %.2f) col (%.3f %.3f %.3f a %.2f)\n", x.mat,
                           (x.mat >> 8) & 15u, (x.mat >> 12) & 0xffffu, x.uv.x, x.uv.y, tt.x, tt.y, tt.z, cc.x, cc.y, cc.z, cc.w);
                    break;
                }
            }
        }
    }
}
