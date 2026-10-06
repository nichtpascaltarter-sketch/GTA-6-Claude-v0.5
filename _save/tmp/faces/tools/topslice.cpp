// Debug: horizontal slices of the head field (planes z = eye.z + dz, model space: x across, y forward) through the
// eyes, stacked top to bottom for dz = +16, +8, 0, -8 mm: inside dark, outside light, eyeballs red, 5 mm grid.
// usage: topslice out.ppm seed role
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    u32 seed = (u32)atoi(argv[2]);
    int role = atoi(argv[3]);
    CharacterDesc d = randomCharacter(seed, role);
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    detail::buildBody(bc);
    vec3 eR = D.J[B_EYE_R], eL = D.J[B_EYE_L];
    float er = bc.head.eyeR;
    const float dzs[4] = {0.016f, 0.008f, 0.f, -0.008f};
    const int W = 640, H = 200;   // 128 x 40 mm per slice
    std::vector<vec3> img((size_t)W * H * 4);
    for (int s = 0; s < 4; s++) {
        float z = eR.z + dzs[s];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                float fx = -0.064f + 0.128f * (x + 0.5f) / W, fy = eR.y + 0.025f - 0.04f * (y + 0.5f) / H;
                vec3 p(fx, fy, z);
                float f = bc.sdf.eval(p, detail::MK_HEAD);
                vec3 c = f < 0.f ? vec3(0.25f) : vec3(0.9f);
                if (fabsf(f) < 0.0002f) c = vec3(0, 0, 0);
                float de = Min(length(p - eR), length(p - eL)) - er;
                if (fabsf(de) < 0.00015f) c = vec3(1, 0, 0);
                float gx = fmodf(fabsf(fx) + 1e-4f, 0.005f), gy = fmodf(fabsf(fy - eR.y) + 1e-4f, 0.005f);
                if (gx < 0.0002f || gy < 0.0002f) c = c * 0.85f + vec3(0, 0.1f, 0.15f);
                if (y == 0) c = vec3(0, 0.6f, 0);
                img[((size_t)s * H + y) * W + x] = c;
            }
        // front surface depth (mm ahead of the eye centre) across x
        printf("dz %+3.0f mm:", dzs[s] * 1000.f);
        for (float fx = 0.f; fx <= 0.0605f; fx += 0.005f) {
            float yy = eR.y + 0.08f;
            for (int it = 0; it < 400; it++) {
                float f = bc.sdf.eval(vec3(fx, yy, z), detail::MK_HEAD);
                if (f <= 0.f) break;
                yy -= Max(f * 0.8f, 0.0001f);
            }
            printf(" x%2.0f:%+5.1f", fx * 1000.f, (yy - eR.y) * 1000.f);
        }
        printf("\n");
    }
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H * 4);
    for (auto& c : img) {
        unsigned char rgb[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("eyeR %.1f %.1f %.1f mm r %.1f mm (cornea apex %+.1f mm)\n", eR.x * 1000.f, eR.y * 1000.f, eR.z * 1000.f, er * 1000.f, 1.0867f * er * 1000.f);
    return 0;
}
