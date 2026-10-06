// Vertical profile at the pupil's grid column (right eye) from below the lower lid up to the forehead: per head-grid
// row the height and depth (mm from the eye centre), the normal's elevation, the painted albedo's luminance and the
// brow cards' cover there. Usage: pupilprof i [i...] (viewer people: seed 1000 + i*7919, role i % 7)
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        int i = atoi(argv[a]);
        u32 seed = 1000 + i * 7919;
        CharacterDesc d = randomCharacter(seed, i % 7);
        Skeleton sk;
        buildSkeleton(d, sk);
        detail::BodyDims D;
        detail::computeDims(d, D);
        detail::BuildCtx bc;
        bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
        detail::buildBody(bc);
        const detail::HeadInfo& H = bc.head;
        vec3 e = D.J[B_EYE_R];
        int best = 0;
        float bd = 1e9f;
        for (int k = 0; k < H.cols; k++) {
            float pa = bc.m.v[H.grid[(size_t)H.rowEyeHi * H.cols + k]].pa;
            if (pa > kPi) continue;
            if (fabsf(pa - H.thetaEye) < bd) { bd = fabsf(pa - H.thetaEye); best = k; }
        }
        // brow card band at this column
        float browLo = 1e9f, browHi = -1e9f;
        for (const detail::BVert& v : bc.m.v) {
            if (v.mat != MAT_HAIR || detail::cardKind(v) != detail::CARD_BROW) continue;
            if (fabsf(v.p.x - e.x) > 0.003f || (v.p.x > 0.f) != (e.x > 0.f)) continue;
            browLo = Min(browLo, (v.p.z - e.z) * 1000.f);
            browHi = Max(browHi, (v.p.z - e.z) * 1000.f);
        }
        printf("c%d (seed %u role %d fem %.1f skin lum %.3f): rows eyeLo %d eyeHi %d lidLo %d lidHi %d brow %d hairline %d; brow cards z %+.1f..%+.1f mm\n", i, seed,
               i % 7, D.fem, dot(d.skinTone, vec3(0.2126f, 0.7152f, 0.0722f)), H.rowEyeLo, H.rowEyeHi, H.rowLidLo, H.rowLidHi, H.rowBrow, H.rowHairline, browLo, browHi);
        int j0 = Max(1, H.rowEyeLo - 4), j1 = Min(H.rows - 1, H.rowBrow + 9);
        for (int j = j0; j <= j1; j++) {
            const detail::BVert& v = bc.m.v[H.grid[(size_t)j * H.cols + best]];
            float elev = asinf(Clamp(v.n.z, -1.f, 1.f)) * kRadToDeg;
            float out = atan2f(v.n.x * (e.x > 0.f ? 1.f : -1.f), v.n.y) * kRadToDeg;
            float lum = dot(v.col, vec3(0.2126f, 0.7152f, 0.0722f));
            printf("  row %2d phi %5.1f  x %+5.1f z %+6.1f  y %+6.1f mm  n.elev %+6.1f deg  n.out %+6.1f  albedo %.3f (x%.2f of skin)%s%s\n", j, v.pb * kRadToDeg, (v.p.x - e.x) * 1000.f, (v.p.z - e.z) * 1000.f,
                   (v.p.y - e.y) * 1000.f, elev, out, lum, lum / Max(dot(d.skinTone, vec3(0.2126f, 0.7152f, 0.0722f)), 1e-4f),
                   j == H.rowEyeHi ? "  <- upper margin" : (j == H.rowEyeLo ? "  <- lower margin" : (j == H.rowBrow ? "  <- rowBrow" : "")),
                   j == H.rowEyeHi + 3 ? "  <- crease row" : "");
        }
    }
    return 0;
}
