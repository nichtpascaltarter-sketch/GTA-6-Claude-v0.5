// Portable 64-bit bit scans. An empty mask returns 64, outside the valid indices.
#pragma once
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace BitScan {
inline unsigned lowestSetBit64(std::uint64_t mask) {
    if (!mask) return 64;
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, mask);
    return static_cast<unsigned>(index);
#else
    return static_cast<unsigned>(__builtin_ctzll(mask));
#endif
}

inline unsigned highestSetBit64(std::uint64_t mask) {
    if (!mask) return 64;
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse64(&index, mask);
    return static_cast<unsigned>(index);
#else
    return 63u - static_cast<unsigned>(__builtin_clzll(mask));
#endif
}
}  // namespace BitScan
