// scratch: the top of the head with each hat over the head joint (bind pose), vs the hairless/hair estimate
#define PREVIEW_NO_MAIN
#include "/home/user/GTA-6-Claude-v0.5/tests/anim/preview.cpp"
using namespace Anim::detail;
int main() {
    for (int hat = -1; hat < HAT_COUNT; hat++) {
        float worst = -1.f, best = 1.f;
        for (u32 seed = 1000; seed < 1030; seed++) {
            CharacterDesc d = randomCharacter(seed, 0);
            d.hat = hat;
            Skeleton sk;
            buildSkeleton(d, sk);
            SkinnedMeshData mesh;
            buildCharacterMesh(d, sk, mesh);
            Pose bind;
            mat4 ms[B_COUNT], skin[B_COUNT];
            computeMatrices(sk, bind, ms, skin);
            float top = -1e9f;
            for (const VtxSkinned& vx : mesh.verts) {
                mat4 m;
                for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
                for (int k = 0; k < 4; k++) {
                    float w = vx.weights[k] / 255.f;
                    if (w <= 0) continue;
                    for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + skin[vx.bones[k]].c[c] * w;
                }
                top = Max(top, transformPoint(m, vx.pos).z);
            }
            // over the bare estimate (height + 2.5 cm)
            float over = top - (d.height + 0.025f);
            worst = Max(worst, over);
            best = Min(best, over);
        }
        printf("hat %d: mesh top over height+2.5cm: %.3f .. %.3f\n", hat, best, worst);
    }
}
