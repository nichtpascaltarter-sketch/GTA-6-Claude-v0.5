#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
using namespace Anim::detail;
int main() {
    CharacterDesc d = randomCharacter(5062, 0);
    d.outer = 2; d.top = 0;
    Skeleton sk; buildSkeleton(d, sk);
    MeshB fin; buildFinalMesh(d, sk, fin);
    int nOut = 0, nOutBack = 0, nTee = 0;
    for (size_t t = 0; t < fin.idx.size(); t += 3) {
        const BVert &a = fin.v[fin.idx[t]], &b = fin.v[fin.idx[t+1]], &c = fin.v[fin.idx[t+2]];
        vec3 cen = (a.p + b.p + c.p) / 3.f;
        if (fabsf(cen.x) > 0.03f || cen.z < 1.15f || cen.z > 1.21f || cen.y < 0.f) continue;
        if (a.mat != MAT_CLOTH) continue;
        vec3 n = cross(b.p - a.p, c.p - a.p);
        bool grey = fabsf(a.col.x - a.col.y) < 0.02f && a.col.x < 0.35f;
        if (grey) { nOut++; if (n.y < 0.f) nOutBack++; }
        else nTee++;
        if (grey && fabsf(cen.x) < 0.015f) printf("grey tri at x %.3f y %.3f z %.3f n.y %.5f col %.3f part %d\n", cen.x, cen.y, cen.z, n.y, a.col.x, a.part);
    }
    printf("front strip: outer tris %d (%d facing back), other cloth %d\n", nOut, nOutBack, nTee);
}
