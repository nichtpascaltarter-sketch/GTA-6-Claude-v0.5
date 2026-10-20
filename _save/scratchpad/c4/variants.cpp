#include "core/math.cpp"
#include "core/rng.h"
#include <cstdio>
// variant finder: seeds S + i*7919 (preview --count) -> cornrows / box (braids) and locs / twists
int main(int argc, char** argv) {
    unsigned base = argc > 1 ? atoi(argv[1]) : 7000;
    for (int g = 0; g < 2; g++) {
        printf("gender %d:\n", g);
        for (int i = 0; i < 12; i++) {
            unsigned seed = base + i * 7919;
            Rng q(hash32(seed * 0x2C1B3C6Du + 0x297A2D39u));
            bool corn = q.chance(g == 1 ? 0.35f : 0.7f);
            Rng q2(hash32(seed * 0x61C88647u + 0x3Du));
            // locs stream: first draw = locs vs twists
            bool locs = q2.chance(g == 1 ? 0.75f : 0.45f);
            printf("  i=%2d seed %u: braids=%s locs=%s\n", i, seed, corn ? "cornrows" : "box", locs ? "long" : "twists");
        }
    }
}
