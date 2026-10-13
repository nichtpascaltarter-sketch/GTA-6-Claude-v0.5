// glasses clearance: the head's SDF distance of every glasses vertex (frame, lenses, pads) within 6 cm of the eyes;
// prints the minimum and how many lie under 0.5 mm off the skin (poke-through risk) for reading glasses and aviators
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main() {
    using namespace Anim::detail;
    for (int kind = 0; kind < 3; kind++) {
        float worst = 1e9f;
        int under = 0, total = 0;
        for (u32 k = 0; k < 8; k++) {
            CharacterDesc d = randomCharacter(9100u + k * 7919u, (int)(k % 7));
            d.glasses = kind;
            d.hat = -1;
            Skeleton sk;
            buildSkeleton(d, sk);
            BodyDims D;
            computeDims(d, D);
            BuildCtx bc;
            bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
            buildBody(bc);
            SkinnedMeshData m;
            buildCharacterMesh(d, sk, m);
            vec3 eL = D.J[B_EYE_L], eR = D.J[B_EYE_R], ec = (eL + eR) * 0.5f;
            for (const VtxSkinned& v : m.verts) {
                u32 mat = v.mat & 0xffu;
                if (mat != MAT_CHROME && mat != MAT_PLASTIC && mat != MAT_METAL_PAINTED && mat != MAT_GLASS) continue;
                if (length(v.pos - ec) > 0.06f || v.pos.y < ec.y) continue;
                float dd = bc.sdf.eval(v.pos, MK_HEAD);
                worst = Min(worst, dd);
                if (dd < 0.0005f) under++;
                total++;
            }
        }
        printf("glasses kind %d: %d vertices in front of the eyes, closest %.1f mm off the skin, %d under 0.5 mm\n", kind, total, worst * 1000.f, under);
    }
}
