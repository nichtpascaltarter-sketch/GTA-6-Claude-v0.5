// scratch: a sweep of roomLight() over random inputs, for the Python port to compare (port.py's 4th argument)
#include "src/world/roomlight.h"
#include <cstdio>
using namespace World;
int main() {
    u32 x = 12345u;
    auto next = [&]() {
        x = x * 1664525u + 1013904223u;
        return x >> 8;
    };
    for (int i = 0; i < 200000; i++) {
        const u32 s = next() % 15u, seed = next() % 65536u, w = next() % 3u;
        u32 day = next() % 40u;
        if (i % 97 == 0) day = 4294967290u + next() % 6u;   // the u32 wrap
        const u32 minute = next() % 1440u, night = next() % 256u;
        printf("%u %u %u %u %u %u %u\n", s, seed, w, day, minute, night, roomLight(s, seed, w, day, minute, night));
    }
    return 0;
}
