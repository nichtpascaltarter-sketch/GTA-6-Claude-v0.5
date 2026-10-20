// Midline profile of the head field (x = 0): inside dark, outside light, the face's surface black; 5 mm grid.
// usage: midslice out.ppm seed role
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    u32 seed = (u32)atoi(argv[2]);
    int role = atoi(argv[3]);
    float zTop = argc > 4 ? (float)atof(argv[4]) * 0.001f : 0.05f;
    CharacterDesc d = randomCharacter(seed, role);
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    detail::buildBody(bc);
    vec3 e = D.J[B_EYE_R];
    const int W = 500, H = 700;
    const float span = 0.1f;
    std::vector<vec3> img((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float fy = e.y - 0.06f + span * (x + 0.5f) / W, fz = e.z + zTop - span * (y + 0.5f) / W;
            vec3 p(0.f, fy, fz);
            float f = bc.sdf.eval(p, detail::MK_HEAD | detail::MK_NECK);
            vec3 c = f < 0.f ? vec3(0.25f) : vec3(0.9f);
            if (fabsf(f) < 0.0002f) c = vec3(0, 0, 0);
            float gy = fmodf(fabsf(fy - e.y) + 1e-4f, 0.005f), gz = fmodf(fabsf(fz - e.z) + 1e-4f, 0.005f);
            if (gy < 0.0002f || gz < 0.0002f) c = c * 0.85f + vec3(0, 0.1f, 0.15f);
            img[(size_t)y * W + x] = c;
        }
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (auto& c : img) {
        unsigned char rgb[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}
