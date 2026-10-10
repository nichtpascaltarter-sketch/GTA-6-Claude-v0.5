#include "../../src/core/bits.h"
#include <cstdio>

static bool check(std::uint64_t mask) {
    unsigned lo = 64, hi = 64;
    for (unsigned i = 0; i < 64; ++i) {
        if (mask & (std::uint64_t(1) << i)) {
            if (lo == 64) lo = i;
            hi = i;
        }
    }
    if (BitScan::lowestSetBit64(mask) == lo && BitScan::highestSetBit64(mask) == hi) return true;
    std::fprintf(stderr, "Bit-scan mismatch for mask %llu\n", static_cast<unsigned long long>(mask));
    return false;
}

int main() {
    if (!check(0) || !check(~std::uint64_t(0))) return 1;
    for (unsigned i = 0; i < 64; ++i) {
        if (!check(std::uint64_t(1) << i)) return 1;
        for (unsigned j = i; j < 64; ++j) {
            if (!check((std::uint64_t(1) << i) | (std::uint64_t(1) << j))) return 1;
        }
    }
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    for (unsigned i = 0; i < 10000; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        if (!check(state)) return 1;
    }
    std::puts("Bit-scan regression tests passed (12146 masks).");
    return 0;
}
