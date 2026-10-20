// scratch: decodes the baked terrain grids (height, water, region) to raw little-endian i16 files, for the pilot's
// orientation maps (rendered in Python)
#include "src/world/gridcodec.cpp"
#include <cstdio>
#include <string>
#include <vector>
using namespace World;
static bool readAll(const std::string& p, std::vector<u8>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    const size_t got = fread(out.data(), 1, (size_t)n, f);
    fclose(f);
    return got == (size_t)n;
}
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const char* names[3] = {"height", "water", "region"};
    for (const char* name : names) {
        std::vector<u8> bytes;
        std::vector<i16> v;
        GridInfo gi;
        if (!readAll(std::string(argv[1]) + "/" + name + ".ntg", bytes) || !decodeGrid(bytes.data(), bytes.size(), v, &gi)) {
            fprintf(stderr, "%s: can't decode\n", name);
            return 1;
        }
        FILE* o = fopen((std::string(argv[2]) + "/" + name + ".i16").c_str(), "wb");
        if (!o) return 1;
        fwrite(v.data(), 2, v.size(), o);
        fclose(o);
        printf("%s %dx%d\n", name, gi.width, gi.height);
    }
    return 0;
}
