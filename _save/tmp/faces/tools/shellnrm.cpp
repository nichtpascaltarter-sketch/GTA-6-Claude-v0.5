// Beard / scalp shell normals against the skin's under them: for every shell vertex (MAT_HAIR, no card kind) of the
// head, the angle to the nearest skin vertex's normal, binned by the shell's height over the skin (mm).
// usage: shellnrm seed role [fh]
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
static vec3 unpackN(u32 n) { return unpackNormalOct(n); }
int main(int argc, char** argv) {
    if (argc < 3) return 1;
    CharacterDesc d = randomCharacter((u32)atoi(argv[1]), atoi(argv[2]));
    if (argc > 3) d.facialHair = atoi(argv[3]);
    d.hat = -1;
    Skeleton sk;
    buildSkeleton(d, sk);
    SkinnedMeshData m;
    buildCharacterMesh(d, sk, m);
    std::vector<vec3> sp, sn;
    for (const VtxSkinned& v : m.verts)
        if ((v.mat & 0xffu) == MAT_SKIN && v.pos.z > 1.4f) {
            sp.push_back(v.pos);
            sn.push_back(unpackN(v.normal));
        }
    const int NB = 8;
    double sum[NB] = {0}, mx[NB] = {0};
    int cnt[NB] = {0};
    for (const VtxSkinned& v : m.verts) {
        if ((v.mat & 0xffu) != MAT_HAIR || ((v.mat >> 8) & 15u) != 0u || v.pos.z < 1.4f) continue;
        float best = 1e9f;
        size_t bi = 0;
        for (size_t i = 0; i < sp.size(); i++) {
            float d2 = length2(sp[i] - v.pos);
            if (d2 < best) { best = d2; bi = i; }
        }
        float h = sqrtf(best) * 1000.f;
        int b = Min((int)(h / 0.5f), NB - 1);
        float ang = acosf(Clamp(dot(unpackN(v.normal), sn[bi]), -1.f, 1.f)) * kRadToDeg;
        sum[b] += ang;
        mx[b] = Max(mx[b], (double)ang);
        cnt[b]++;
    }
    for (int b = 0; b < NB; b++)
        if (cnt[b]) printf("height %.1f-%.1f mm: %5d verts, normal vs skin mean %5.1f deg, max %5.1f\n", b * 0.5f, (b + 1) * 0.5f, cnt[b], sum[b] / cnt[b], mx[b]);
    return 0;
}
