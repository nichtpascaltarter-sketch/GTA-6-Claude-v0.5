// A vertical slice (plane x = X, model space) through the final character mesh: skin triangles black, hair shells
// red, strand cards green, over the head field (inside grey). usage: meshslice out.ppm seed role X(mm) yc(mm) zc(mm) span(mm)
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
int main(int argc, char** argv) {
    if (argc < 8) return 1;
    u32 seed = (u32)atoi(argv[2]);
    int role = atoi(argv[3]);
    float X = (float)atof(argv[4]) * 0.001f, yc = (float)atof(argv[5]) * 0.001f, zc = (float)atof(argv[6]) * 0.001f, span = (float)atof(argv[7]) * 0.001f;
    CharacterDesc d = randomCharacter(seed, role);
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
    const int W = 600, H = 600;
    std::vector<vec3> img((size_t)W * H);
    auto toPx = [&](float y, float z, int& px, int& py) {
        px = (int)((y - (yc - span * 0.5f)) / span * W);
        py = (int)(((zc + span * 0.5f) - z) / span * H);
    };
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++) {
            float y = yc - span * 0.5f + span * (px + 0.5f) / W, z = zc + span * 0.5f - span * (py + 0.5f) / H;
            float f = bc.sdf.eval(vec3(X, y, z), detail::MK_HEAD | detail::MK_NECK);
            vec3 c = f < 0.f ? vec3(0.55f) : vec3(0.95f);
            float gy = fmodf(fabsf(y) + 1e-5f, 0.005f), gz = fmodf(fabsf(z) + 1e-5f, 0.005f);
            if (gy < span / W || gz < span / H) c = c * 0.9f;
            img[(size_t)py * W + px] = c;
        }
    auto line = [&](vec3 a, vec3 b, vec3 col) {
        int x0, y0, x1, y1;
        toPx(a.y, a.z, x0, y0);
        toPx(b.y, b.z, x1, y1);
        int n = Max(abs(x1 - x0), abs(y1 - y0)) + 1;
        for (int i = 0; i <= n; i++) {
            int x = x0 + (x1 - x0) * i / n, y = y0 + (y1 - y0) * i / n;
            if (x >= 0 && x < W && y >= 0 && y < H) img[(size_t)y * W + x] = col;
        }
    };
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const VtxSkinned* v[3] = {&m.verts[m.indices[t]], &m.verts[m.indices[t + 1]], &m.verts[m.indices[t + 2]]};
        u32 mat = v[0]->mat & 0xffu;
        vec3 col;
        if (mat == MAT_SKIN) col = vec3(0, 0, 0);
        else if (mat == MAT_HAIR) col = ((v[0]->mat >> 8) & 15u) == 0u ? vec3(0.9f, 0, 0) : vec3(0, 0.6f, 0);
        else continue;
        vec3 pts[2];
        int np = 0;
        for (int k = 0; k < 3 && np < 2; k++) {
            vec3 a = v[k]->pos, b = v[(k + 1) % 3]->pos;
            float da = a.x - X, db = b.x - X;
            if ((da < 0.f) != (db < 0.f)) {
                float s = da / (da - db);
                pts[np++] = lerp(a, b, s);
            }
        }
        if (np == 2) line(pts[0], pts[1], col);
    }
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (auto& c : img) {
        unsigned char rgb[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    vec3 e = D.J[B_EYE_R];
    printf("eye at %.1f %.1f %.1f mm\n", e.x * 1000.f, e.y * 1000.f, e.z * 1000.f);
    return 0;
}
