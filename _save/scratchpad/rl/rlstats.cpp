// scratch: statistics and test vectors for src/world/roomlight.h
#include "src/world/roomlight.h"
#include <cstdio>
using namespace World;
int main() {
    const char* names[RS_COUNT];
    for (int s = 0; s < RS_COUNT; s++) names[s] = roomScheduleName((u32)s);
    // share of rooms lit by hour, weekday (day 2 = Wednesday) and weekend (day 5 = Saturday), at full night, with windows
    for (int wk = 0; wk < 2; wk++) {
        printf("%s, night=255, windows=1: share lit by hour (of 2000 rooms)\n", wk ? "Saturday (day 5)" : "Wednesday (day 2)");
        printf("%-13s", "");
        for (int h = 0; h < 24; h++) printf("%4d", h);
        printf("\n");
        for (int s = 0; s < RS_COUNT; s++) {
            printf("%-13s", names[s]);
            for (int h = 0; h < 24; h++) {
                int lit = 0;
                for (u32 r = 0; r < 2000; r++) lit += roomLight((u32)s, roomSeed(r * 7919u + 3u, r % 5u, r % 11u), 1u, wk ? 5u : 2u, (u32)h * 60u + 30u, 255u) > 0u;
                printf("%4d", lit * 100 / 2000);
            }
            printf("\n");
        }
    }
    // daylight: homes at 20:30 by night level
    printf("home_living 20:30 Wednesday by night level: ");
    for (u32 n = 0; n <= 255; n += 32) {
        int lit = 0;
        for (u32 r = 0; r < 2000; r++) lit += roomLight(RS_HOME_LIVING, roomSeed(r * 7919u + 3u, 0, r), 1u, 2u, 1230u, n) > 0u;
        printf("%u:%d%% ", n, lit * 100 / 2000);
    }
    printf("\n");
    // day to day: rooms whose 20:30 state differs between day 2 and day 3; shops at 3:00 across days
    int diff = 0, shopDiff = 0;
    for (u32 r = 0; r < 2000; r++) {
        u32 sd = roomSeed(r * 7919u + 3u, 1, r);
        diff += (roomLight(RS_HOME_LIVING, sd, 1u, 2u, 1230u, 255u) > 0u) != (roomLight(RS_HOME_LIVING, sd, 1u, 3u, 1230u, 255u) > 0u);
        shopDiff += (roomLight(RS_SHOP, sd, 1u, 2u, 180u, 255u) > 0u) != (roomLight(RS_SHOP, sd, 1u, 3u, 180u, 255u) > 0u);
    }
    printf("home_living 20:30, day 2 vs 3: %d of 2000 differ; shop 3:00, day 2 vs 3: %d differ\n", diff, shopDiff);
    // test vectors: schedule seed windows day minute night -> level
    printf("VECTORS\n");
    u32 x = 12345u;
    for (int i = 0; i < 48; i++) {
        x = roomHash(x + 0x9e3779b9u);
        u32 s = x % RS_COUNT, seed = roomHash(x ^ 0xabcdefu), win = (x >> 5) & 1u, day = (x >> 8) % 9u, minute = (x >> 12) % 1440u, night = (x >> 22) & 255u;
        if (i % 3 == 0) night = 255u;
        printf("{%2u, 0x%08xu, %u, %u, %4u, %3u, %3u},\n", s, seed, win, day, minute, night, roomLight(s, seed, win, day, minute, night));
    }
    return 0;
}
