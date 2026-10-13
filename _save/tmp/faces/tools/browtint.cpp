// Brow tint on the skin at the pupil's column: luminance of the head-grid vertices from the lid fold up the forehead.
// usage: browtint seed role
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    CharacterDesc d = randomCharacter((u32)atoi(argv[1]), atoi(argv[2]));
    Skeleton sk;
    buildSkeleton(d, sk);
    BodyDims D;
    computeDims(d, D);
    BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    buildBody(bc);
    const HeadInfo& H = bc.head;
    const int NC = H.cols;
    for (int pass = 0; pass < 3; pass++) {
        float off = (pass - 1) * 6.f * kDegToRad;   // columns at the pupil and 6 degrees either side
        int best = 0;
        float bd = 1e9f;
        for (int c = 0; c < NC; c++) {
            float pa = bc.m.v[H.grid[(size_t)H.rowEyeHi * NC + c]].pa;
            if (pa < kPi && fabsf(pa - (H.thetaEye + off)) < bd) { bd = fabsf(pa - (H.thetaEye + off)); best = c; }
        }
        printf("column %d (theta %.1f):\n", best, bc.m.v[H.grid[(size_t)H.rowEyeHi * NC + best]].pa * kRadToDeg);
        for (int j = H.rowLidHi; j < H.rows && j < H.rowLidHi + 12; j++) {
            const BVert& v = bc.m.v[H.grid[(size_t)j * NC + best]];
            printf("  row %2d phi %5.1f  col %.3f %.3f %.3f  lum %.3f\n", j, v.pb * kRadToDeg, v.col.x, v.col.y, v.col.z, dot(v.col, vec3(0.3f, 0.59f, 0.11f)));
        }
    }
    // the same vertices in the final mesh (bind pose): nearest skin vertex colour (RGBA8)
    {
        SkinnedMeshData fm;
        buildCharacterMesh(d, sk, fm);
        int best = 0;
        float bd = 1e9f;
        for (int c = 0; c < NC; c++) {
            float pa = bc.m.v[H.grid[(size_t)H.rowEyeHi * NC + c]].pa;
            if (pa < kPi && fabsf(pa - H.thetaEye) < bd) { bd = fabsf(pa - H.thetaEye); best = c; }
        }
        for (int j = H.rowLidHi; j < H.rows && j < H.rowLidHi + 9; j++) {
            vec3 p = bc.m.v[H.grid[(size_t)j * NC + best]].p;
            float b2 = 1e9f;
            u32 col = 0;
            for (const VtxSkinned& v : fm.verts) {
                if ((v.mat & 0xffu) != MAT_SKIN) continue;
                float d2 = length2(v.pos - p);
                if (d2 < b2) { b2 = d2; col = v.color; }
            }
            printf("  final row %2d: nearest skin vertex %.2f mm away, colour %3u %3u %3u\n", j, sqrtf(b2) * 1000.f, col & 255u, (col >> 8) & 255u, (col >> 16) & 255u);
        }
    }
    printf("hair %.3f %.3f %.3f  skin %.3f %.3f %.3f  browThick %.2f browH %.2f fem %.2f\n", d.hairColor.x, d.hairColor.y, d.hairColor.z, d.skinTone.x, d.skinTone.y, d.skinTone.z, D.browThick, D.browH, D.fem);
    return 0;
}
