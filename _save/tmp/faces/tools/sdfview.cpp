// Debug: the face SDF's own shading in (theta, phi) map space from the head grid's ray origin, with the grid's
// vertices dotted on top. usage: sdfview out.ppm seed role th0 th1 ph0 ph1 [res]
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    if (argc < 8) return 1;
    u32 seed = (u32)atoi(argv[2]);
    int role = atoi(argv[3]);
    float th0 = (float)atof(argv[4]), th1 = (float)atof(argv[5]), ph0 = (float)atof(argv[6]), ph1 = (float)atof(argv[7]);
    float res = argc > 8 ? (float)atof(argv[8]) : 0.1f;
    CharacterDesc d = randomCharacter(seed, role);
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    detail::buildBody(bc);
    const detail::HeadInfo& H = bc.head;
    int W = (int)((th1 - th0) / res), Hh = (int)((ph1 - ph0) / res);
    std::vector<vec3> img((size_t)W * Hh);
    vec3 L = normalize(vec3(0.35f, 1.f, 0.6f));
    if (const char* lv = getenv("SDF_LIGHT")) {
        vec3 l;
        sscanf(lv, "%f,%f,%f", &l.x, &l.y, &l.z);
        L = normalize(l);
    }
    const bool noDots = getenv("SDF_NODOTS") != nullptr;
    for (int y = 0; y < Hh; y++)
        for (int x = 0; x < W; x++) {
            float th = (th0 + (x + 0.5f) * res) * kDegToRad, ph = (ph1 - (y + 0.5f) * res) * kDegToRad;
            vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
            float t = bc.sdf.castOut(H.C, dir, detail::MK_HEAD, 0.22f * D.headS);
            vec3 p = H.C + dir * t;
            vec3 g = bc.sdf.grad(p, detail::MK_HEAD);
            vec3 n = length2(g) > 1e-12f ? normalize(g) : dir;
            float df = Max(0.f, (dot(n, L) + 0.3f) / 1.3f);
            img[(size_t)y * W + x] = vec3(0.1f + 0.9f * df);
        }
    // grid vertices
    for (int j = 0; j < (noDots ? 0 : H.rows); j++)
        for (int k = 0; k < H.cols; k++) {
            const detail::BVert& v = bc.m.v[H.grid[(size_t)j * H.cols + k]];
            float th = v.pa > kPi ? v.pa - kTwoPi : v.pa;
            int x = (int)((th * kRadToDeg - th0) / res), y = (int)((ph1 - v.pb * kRadToDeg) / res);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < W && yy >= 0 && yy < Hh) img[(size_t)yy * W + xx] = (j == H.rowEyeHi || j == H.rowEyeLo) ? vec3(1, 0, 0) : vec3(0, 0.6f, 1);
                }
        }
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, Hh);
    for (auto& c : img) {
        unsigned char rgb[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("rows %d cols %d eyeHi %d eyeLo %d lidHi %d lidLo %d brow %d\n", H.rows, H.cols, H.rowEyeHi, H.rowEyeLo, H.rowLidHi, H.rowLidLo, H.rowBrow);
    return 0;
}
