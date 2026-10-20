#include "../../../../../home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/src/anim/anim_all.cpp"
#include "../../../../../home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    double tBody = 0, tOut = 0, tAO = 0, tEmit = 0;
    int N = 8;
    size_t nv = 0;
    for (int i = 0; i < N; i++) {
        CharacterDesc d = randomCharacter(1000 + i * 7919, i % 7);
        Skeleton skel; buildSkeleton(d, skel);
        BodyDims D; computeDims(d, D);
        BuildCtx c; c.d = &d; c.D = &D; c.sk = &skel; c.skin = d.skinTone; c.lipCol = c.skin; c.palmCol = c.skin;
        double t0 = TimeSeconds();
        buildBody(c);
        double t1 = TimeSeconds();
        MeshB extra; std::vector<u8> hide(c.m.idx.size() / 3, 0);
        buildOutfit(c, extra, hide);
        MeshB fin; compactInto(c.m, hide, fin); fin.append(extra);
        double t2 = TimeSeconds();
        bakeOcclusion(c, fin);
        double t3 = TimeSeconds();
        fixUvSeams(fin); SkinnedMeshData out; emitMesh(fin, out);
        double t4 = TimeSeconds();
        tBody += t1 - t0; tOut += t2 - t1; tAO += t3 - t2; tEmit += t4 - t3; nv += fin.v.size();
    }
    printf("per character: body %.1f ms, outfit %.1f ms, AO %.1f ms, emit %.1f ms, verts %zu, prims %s\n", tBody / N * 1e3, tOut / N * 1e3, tAO / N * 1e3, tEmit / N * 1e3, nv / N, "");
}
