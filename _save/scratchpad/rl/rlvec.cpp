// scratch: the test vectors for src/world/roomlight.h (pasted into tests/world/worldmodel_test.cpp)
#include "src/world/roomlight.h"
#include <cstdio>
using namespace World;
int main() {
    struct P { u32 day, minute, night, windows; };
    const P pats[8] = {{2, 1230, 255, 1}, {2, 420, 255, 1}, {2, 90, 255, 0}, {5, 600, 255, 1}, {5, 1410, 80, 1}, {3, 1230, 0, 0}, {0, 30, 255, 1}, {6, 125, 255, 1}};
    int on = 0, n = 0;
    for (u32 s = 0; s < RS_COUNT; s++)
        for (u32 j = 0; j < 8; j++) {
            const P& p = pats[j];
            u32 seed = roomSeed(1000u + s * 31u + j, j, s);
            u32 lv = roomLight(s, seed, p.windows, p.day, p.minute, p.night);
            on += lv > 0;
            n++;
            printf("        {%2u, %4u, %u, %u, %4u, %3u, %3u},\n", s, seed, p.windows, p.day, p.minute, p.night, lv);
        }
    fprintf(stderr, "%d of %d on\n", on, n);
    return 0;
}
