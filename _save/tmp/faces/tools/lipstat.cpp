// Lip colour against the skin around the mouth (CIELAB of the vertex albedo): upper and lower vermilion, the corners,
// and the skin of the chin and the philtrum. usage: lipstat [viewer indices...] (p0 / p1 = the protagonists)
#include "src/core/math.cpp"
#include "src/render/mesh.cpp"
#include "src/anim/anim_all.cpp"
#include "tools/native_stubs.cpp"
#ifndef HAVE_CHARACTERS
#define HAVE_CHARACTERS 1
#endif
#include "src/game/protagonists.h"
using namespace Anim;
static vec3 toLab(vec3 lin) {
    // linear sRGB -> XYZ (D65) -> L*a*b*
    float X = 0.4124f * lin.x + 0.3576f * lin.y + 0.1805f * lin.z;
    float Y = 0.2126f * lin.x + 0.7152f * lin.y + 0.0722f * lin.z;
    float Z = 0.0193f * lin.x + 0.1192f * lin.y + 0.9505f * lin.z;
    auto f = [](float t) { return t > 0.008856f ? cbrtf(t) : 7.787f * t + 16.f / 116.f; };
    float fx = f(X / 0.9505f), fy = f(Y), fz = f(Z / 1.089f);
    return vec3(116.f * fy - 16.f, 500.f * (fx - fy), 200.f * (fy - fz));
}
static vec3 unpackLin(u32 c) { return vec3((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f); }
int main(int argc, char** argv) {
    printf("%-5s %-4s %-5s | %-17s %-17s %-17s | %-17s %-17s | %s\n", "who", "sex", "age", "upper L a b", "lower L a b", "corner L a b",
           "chin L a b", "philtrum L a b", "lower-chin dL da db");
    for (int a = 1; a < argc; a++) {
        CharacterDesc d;
        if (argv[a][0] == 'p') d = Game::protagonistDesc(atoi(argv[a] + 1));
        else { int i = atoi(argv[a]); d = randomCharacter(1000 + i * 7919, i % 7); }
        d.hat = -1; d.glasses = -1;
        Skeleton sk;
        buildSkeleton(d, sk);
        SkinnedMeshData m;
        buildCharacterMesh(d, sk, m);
        // lip vertices: skin with the region bits = 1
        std::vector<const VtxSkinned*> lip;
        float zmin = 1e9f, zmax = -1e9f, xmax = 0.f, ymean = 0.f;
        for (const VtxSkinned& v : m.verts) {
            u32 mat = v.mat & 0xffu, prm = (v.mat >> 8) & 0x7fffffu;
            if (mat != MAT_SKIN || !(prm & 1u) || ((prm >> 1) & 7u) != 1u) continue;
            lip.push_back(&v);
            zmin = Min(zmin, v.pos.z); zmax = Max(zmax, v.pos.z); xmax = Max(xmax, fabsf(v.pos.x)); ymean += v.pos.y;
        }
        if (lip.empty()) continue;
        ymean /= lip.size();
        const float zMid = zmin + 0.45f * (zmax - zmin);
        vec3 up(0), lo(0), co(0);
        int nu = 0, nl = 0, nc = 0;
        for (const VtxSkinned* v : lip) {
            vec3 c = unpackLin(v->color);
            if (fabsf(v->pos.x) > 0.8f * xmax) { co += c; nc++; continue; }
            if (fabsf(v->pos.x) > 0.5f * xmax) continue;
            if (v->pos.z > zMid) { up += c; nu++; } else { lo += c; nl++; }
        }
        vec3 chin(0), phil(0);
        int nch = 0, nph = 0;
        for (const VtxSkinned& v : m.verts) {
            u32 mat = v.mat & 0xffu, prm = (v.mat >> 8) & 0x7fffffu;
            if (mat != MAT_SKIN || !(prm & 1u) || ((prm >> 1) & 7u) != 0u) continue;
            if (fabsf(v.pos.x) > 0.012f || v.pos.y < ymean - 0.012f) continue;
            if (v.pos.z < zmin - 0.008f && v.pos.z > zmin - 0.022f) { chin += unpackLin(v.color); nch++; }
            if (v.pos.z > zmax + 0.003f && v.pos.z < zmax + 0.009f) { phil += unpackLin(v.color); nph++; }
        }
        vec3 U = toLab(up / (float)Max(nu, 1)), Lo = toLab(lo / (float)Max(nl, 1)), C = toLab(co / (float)Max(nc, 1));
        vec3 Ch = toLab(chin / (float)Max(nch, 1)), Ph = toLab(phil / (float)Max(nph, 1));
        printf("%-5s %-4s %5.2f | %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f | %+5.1f %+5.1f %+5.1f\n",
               argv[a], d.gender == FEMALE ? "F" : "M", d.age, U.x, U.y, U.z, Lo.x, Lo.y, Lo.z, C.x, C.y, C.z, Ch.x, Ch.y, Ch.z, Ph.x, Ph.y, Ph.z,
               Lo.x - Ch.x, Lo.y - Ch.y, Lo.z - Ch.z);
    }
}
