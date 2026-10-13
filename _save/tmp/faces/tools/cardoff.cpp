// buzz cut: how far the scalp cards' points lie from the head's visible surface (shell + skin triangles on the head)
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
using namespace Anim;
static vec3 closestOnTri(vec3 p, vec3 a, vec3 b, vec3 c) {
    vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return a;
    vec3 bp = p - b; float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) return a + ab * (d1 / (d1 - d3));
    vec3 cp = p - c; float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float den = 1.f / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}
int main(int argc, char** argv) {
    for (int k = 0; k < 6; k++) {
        CharacterDesc d = randomCharacter(8300u + k * 7919u, k % 7);
        d.hairStyle = argc > 1 ? atoi(argv[1]) : (int)detail::HAIR_BUZZ; d.hat = -1;
        Skeleton sk; buildSkeleton(d, sk);
        SkinnedMeshData m; buildCharacterMesh(d, sk, m);
        auto onHead = [](const VtxSkinned& v) { for (int i = 0; i < 4; i++) if (v.bones[i] == B_HEAD && v.weights[i] >= 200) return true; return false; };
        std::vector<int> surfT;
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            const VtxSkinned& v = m.verts[m.indices[t]];
            u32 mat = v.mat & 0xffu, kind = (v.mat >> 8) & 15u;
            if ((mat == MAT_SKIN || (mat == MAT_HAIR && kind == 0u)) && onHead(v)) surfT.push_back((int)t);
        }
        float worst = 0.f; vec3 wp; int n = 0, over4 = 0, over7 = 0;
        for (const VtxSkinned& v : m.verts) {
            if ((v.mat & 0xffu) != MAT_HAIR || ((v.mat >> 8) & 15u) != 1u) continue;
            float best = 1e9f;
            for (int t : surfT) {
                vec3 q = closestOnTri(v.pos, m.verts[m.indices[t]].pos, m.verts[m.indices[t + 1]].pos, m.verts[m.indices[t + 2]].pos);
                best = Min(best, length2(q - v.pos));
            }
            best = sqrtf(best); n++;
            if (best > 0.004f) over4++;
            if (best > 0.0055f && getenv("CARDOFF_LIST")) { vec3 hh = -sk.invBindModel[B_HEAD].c[3].xyz(); printf("   %.1f mm uv %.2f %.2f a %.2f rel (%.3f %.3f %.3f)\n", best * 1000.f, v.uv.x, v.uv.y, ((v.color >> 24) & 255) / 255.f, v.pos.x - hh.x, v.pos.y - hh.y, v.pos.z - hh.z); }
            if (best > 0.007f) over7++;
            if (best > worst) { worst = best; wp = v.pos; }
        }
        vec3 head = -sk.invBindModel[B_HEAD].c[3].xyz();
        printf("k%d: %d card points, worst %.1f mm at (%.3f %.3f %.3f) rel head (%.3f %.3f %.3f); >4 mm %d, >7 mm %d\n", k, n, worst * 1000.f, wp.x, wp.y, wp.z,
               wp.x - head.x, wp.y - head.y, wp.z - head.z, over4, over7);
    }
}
