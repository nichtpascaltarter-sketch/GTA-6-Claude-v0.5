// scratch: the mesh's top of the head (hair included) over the head joint in the bind pose vs the animator's crownH,
// and the highest point when the head pitches forward
#define PREVIEW_NO_MAIN
#include "/home/user/GTA-6-Claude-v0.5/tests/anim/preview.cpp"
using namespace Anim::detail;
int main() {
    float worstUnder = 0.f, worstOver = 0.f;
    for (u32 seed = 1000; seed < 1060; seed++) {
        CharacterDesc d = randomCharacter(seed, 0);
        d.hat = -1;
        Skeleton sk;
        buildSkeleton(d, sk);
        SkinnedMeshData mesh;
        buildCharacterMesh(d, sk, mesh);
        Animator an;
        an.init(&sk, 77u);
        an.setCharacter(d);
        Pose bind;
        mat4 ms[B_COUNT], skin[B_COUNT];
        for (int pitchDeg : {0, 12, 25}) {
            Pose p = bind;
            p.rot[B_HEAD] = normalize(p.rot[B_HEAD] * quatAxisAngle(vec3(1, 0, 0), -pitchDeg * kDegToRad));
            computeMatrices(sk, p, ms, skin);
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
            quat q; vec3 h;
            boneModel(sk, p, B_HEAD, q, h);
            float est = (h + rotate(q, vec3(0, 0, an.crownH))).z;
            const float rS = Min(0.11f, an.crownH * 0.55f);
            float est2 = (h + rotate(q, vec3(0, 0, an.crownH - rS))).z + rS;
            if (seed < 1012) printf("seed %u h %.2f hair %d pitch %2d: mesh top %.3f  crown est %+.3f  sphere est %+.3f\n", seed, d.height, d.hairStyle, pitchDeg, top - h.z, est - top, est2 - top);
            if (pitchDeg == 0) { worstUnder = Min(worstUnder, est - top); worstOver = Max(worstOver, est - top); }
        }
    }
    printf("crown est - mesh top (upright): min %+.3f max %+.3f\n", worstUnder, worstOver);
}
