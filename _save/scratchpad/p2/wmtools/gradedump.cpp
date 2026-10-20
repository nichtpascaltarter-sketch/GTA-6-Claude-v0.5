// gradedump (city scratch, not for the repo): world.bin's terrain.grade against its baked heights: how many texels
// changed, a histogram of the changes, and the largest ones with their positions. Build from the tree root:
// g++ -std=c++17 -O1 -I. wmtools/gradedump.cpp
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/world/lz.cpp"
#include "src/world/worldbin.cpp"
#include "src/world/gridcodec.cpp"
#include <algorithm>
using namespace World;
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 3;
    std::vector<u8> bin;
    u8 buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bin.insert(bin.end(), buf, buf + n);
    fclose(f);
    WorldBinView view;
    std::string why;
    if (!view.open(bin.data(), bin.size(), &why)) { printf("open: %s\n", why.c_str()); return 1; }
    const WorldBinView::Section* h = view.find("terrain.height");
    const WorldBinView::Section* g = view.find("terrain.grade");
    const WorldBinView::Section* w = view.find("terrain.water");
    if (!h || !g || !w) { printf("no terrain.height, terrain.water or terrain.grade\n"); return 1; }
    std::vector<i16> hv, gv, wv;
    GridInfo gi;
    if (!decodeGrid(h->data, h->size, hv, &gi) || !decodeGrid(g->data, g->size, gv, &gi) || !decodeGrid(w->data, w->size, wv, &gi)) { printf("decode failed\n"); return 1; }
    const int R = gi.width;
    size_t changed = 0, up = 0, down = 0, hist[6] = {};
    std::vector<std::pair<int, int>> big;
    for (size_t i = 0; i < gv.size(); i++) {
        if (!gv[i]) continue;
        changed++;
        (gv[i] > 0 ? up : down)++;
        const int a = abs(gv[i]);
        hist[a < 10 ? 0 : a < 30 ? 1 : a < 60 ? 2 : a < 100 ? 3 : a < 200 ? 4 : 5]++;
        big.push_back({a, (int)i});
    }
    std::sort(big.begin(), big.end(), [](const std::pair<int, int>& x, const std::pair<int, int>& y) { return x.first > y.first; });
    printf("grade: %zu texels changed (%zu raised, %zu lowered); |change| <0.1 m %zu, <0.3 %zu, <0.6 %zu, <1 %zu, <2 %zu, more %zu\n", changed, up, down, hist[0], hist[1], hist[2],
           hist[3], hist[4], hist[5]);
    for (size_t k = 0; k < big.size() && k < 25; k++) {
        const int i = big[k].second;
        const int tx = i % R, ty = i / R;
        printf("  %8.1f %8.1f  baked %6.2f  graded %6.2f  (%+.2f)  water class %d\n", -10240.0 + (tx + 0.5) * 8.0, -10240.0 + (ty + 0.5) * 8.0, hv[(size_t)i] * 0.01,
               (hv[(size_t)i] + gv[(size_t)i]) * 0.01, gv[(size_t)i] * 0.01, wv[(size_t)i]);
    }
    return 0;
}
