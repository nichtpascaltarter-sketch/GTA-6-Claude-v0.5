#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
#include <algorithm>
int main() {
    std::vector<float> asp[2], fw[2], fh[2];
    for (int i = 0; i < 3000; i++) {
        Anim::CharacterDesc d = Anim::randomCharacter(7777 + i * 7919, i % 7);
        Anim::detail::BodyDims D;
        Anim::detail::computeDims(d, D);
        int g = d.gender == Anim::FEMALE;
        asp[g].push_back(D.faceH / D.faceW);
        fw[g].push_back(D.faceW);
        fh[g].push_back(D.faceH);
    }
    for (int g = 0; g < 2; g++) {
        auto pr = [&](const char* n, std::vector<float>& v) {
            std::sort(v.begin(), v.end());
            size_t N = v.size();
            printf("%s %s: min %.3f p5 %.3f p50 %.3f p95 %.3f max %.3f\n", g ? "F" : "M", n, v[0], v[N * 5 / 100], v[N / 2], v[N * 95 / 100], v[N - 1]);
        };
        pr("aspect", asp[g]);
        pr("faceW", fw[g]);
        pr("faceH", fh[g]);
    }
}
