// Scan seeds for the most extreme faces (aspect, jaw/face ratio, nose length, chin) per sex.
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
int main() {
    struct Best { float v; u32 seed; int role; } hiA[2] = {{-1e9f, 0, 0}, {-1e9f, 0, 0}}, loA[2] = {{1e9f, 0, 0}, {1e9f, 0, 0}},
        hiJ[2] = {{-1e9f, 0, 0}, {-1e9f, 0, 0}}, hiN[2] = {{-1e9f, 0, 0}, {-1e9f, 0, 0}}, loE[2] = {{1e9f, 0, 0}, {1e9f, 0, 0}};
    for (int i = 0; i < 4000; i++) {
        u32 seed = 90001u + (u32)i * 7919u;
        int role = 0;
        Anim::CharacterDesc d = Anim::randomCharacter(seed, role);
        Anim::detail::BodyDims D;
        Anim::detail::computeDims(d, D);
        int g = d.gender == Anim::FEMALE;
        float asp = D.faceH / D.faceW, jr = D.jawW / D.faceW;
        if (asp > hiA[g].v) hiA[g] = {asp, seed, role};
        if (asp < loA[g].v) loA[g] = {asp, seed, role};
        if (jr > hiJ[g].v) hiJ[g] = {jr, seed, role};
        float nl = D.noseL * D.faceH;
        if (nl > hiN[g].v) hiN[g] = {nl, seed, role};
        if (D.eyeSize < loE[g].v) loE[g] = {D.eyeSize, seed, role};
    }
    for (int g = 0; g < 2; g++) {
        printf("%s longest %u %.3f | shortest %u %.3f | widest jaw %u %.3f | longest nose %u %.3f | smallest eyes %u %.3f\n", g ? "F" : "M",
               hiA[g].seed, hiA[g].v, loA[g].seed, loA[g].v, hiJ[g].seed, hiJ[g].v, hiN[g].seed, hiN[g].v, loE[g].seed, loE[g].v);
    }
}
