// Height of the hair shells over the head (mm), splatted in (theta, phi) map space from the head grid's ray origin:
// 0 mm black .. 6 mm white, card vertices skipped. usage: shellmap out.ppm seed role th0 th1 ph0 ph1 [res] [fh]
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
    float res = argc > 8 ? (float)atof(argv[8]) : 0.25f;
    CharacterDesc d = randomCharacter(seed, role);
    if (argc > 9) d.facialHair = atoi(argv[9]);
    d.hat = -1;
    Skeleton sk;
    buildSkeleton(d, sk);
    detail::BodyDims D;
    detail::computeDims(d, D);
    detail::BuildCtx bc;
    bc.d = &d; bc.D = &D; bc.sk = &sk; bc.skin = d.skinTone; bc.lipCol = bc.skin; bc.palmCol = bc.skin;
    detail::buildBody(bc);
    SkinnedMeshData m;
    buildCharacterMesh(d, sk, m);
    const vec3 C = bc.head.C;
    int W = (int)((th1 - th0) / res), Hh = (int)((ph1 - ph0) / res);
    std::vector<vec3> img((size_t)W * Hh, vec3(0.1f, 0.15f, 0.3f));
    std::vector<float> best((size_t)W * Hh, 1e9f);
    int n = 0;
    float hmin = 1e9f, hmax = -1e9f;
    for (const VtxSkinned& v : m.verts) {
        if ((v.mat & 0xffu) != MAT_HAIR || ((v.mat >> 8) & 15u) != 0u) continue;
        vec3 dd = v.pos - C;
        float th = atan2f(dd.x, dd.y) * kRadToDeg, ph = atan2f(dd.z, sqrtf(dd.x * dd.x + dd.y * dd.y)) * kRadToDeg;
        int x = (int)((th - th0) / res), y = (int)((ph1 - ph) / res);
        if (x < 0 || x >= W || y < 0 || y >= Hh) continue;
        float h = bc.sdf.eval(v.pos, detail::MK_HEAD) * 1000.f;
        hmin = Min(hmin, h);
        hmax = Max(hmax, h);
        n++;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int xx = x + dx, yy = y + dy;
                if (xx < 0 || xx >= W || yy < 0 || yy >= Hh) continue;
                size_t o = (size_t)yy * W + xx;
                float dist = (float)(dx * dx + dy * dy);
                if (dist > best[o]) continue;
                best[o] = dist;
                float t = Saturate(h / 6.f);
                img[o] = h < 0.f ? vec3(1, 0, 0) : vec3(t);
            }
    }
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, Hh);
    for (auto& c : img) {
        unsigned char rgb[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%d shell vertices in view, height %.2f .. %.2f mm\n", n, hmin, hmax);
    return 0;
}
