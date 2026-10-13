#include "core/math.cpp"
#include "core/rng.h"
#include <cstdio>
int main() {
    for (unsigned seed = 7300; seed < 7400; seed++) {
        Rng q(hash32(seed * 0x1656667Bu + 0x29u));
        float lenF = q.f();
        if (q.chance(0.12f)) printf("male long curls seed %u lenF %.2f\n", seed, lenF);
    }
}
