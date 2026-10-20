// LZ77 codec (lz.h).
#include "lz.h"
#include <cstring>

namespace World {

namespace lz_detail {

constexpr size_t kMinMatch = 4;
constexpr size_t kWindow = 65535;
constexpr int kHashBits = 16;
constexpr int kMaxChain = 48;

inline u32 read32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
inline u32 hash4(const u8* p) { return (read32(p) * 2654435761u) >> (32 - kHashBits); }

void putLength(std::vector<u8>& out, size_t n) {   // the part of a length beyond its nibble's 15
    while (n >= 255) {
        out.push_back(255);
        n -= 255;
    }
    out.push_back((u8)n);
}

void sequence(std::vector<u8>& out, const u8* lit, size_t litLen, size_t offset, size_t matchLen) {
    const size_t ml = matchLen ? matchLen - kMinMatch : 0;
    const u8 token = (u8)(((litLen < 15 ? litLen : 15) << 4) | (matchLen ? (ml < 15 ? ml : 15) : 0));
    out.push_back(token);
    if (litLen >= 15) putLength(out, litLen - 15);
    out.insert(out.end(), lit, lit + litLen);
    if (!matchLen) return;
    out.push_back((u8)offset);
    out.push_back((u8)(offset >> 8));
    if (ml >= 15) putLength(out, ml - 15);
}

}  // namespace lz_detail

void lzCompress(const u8* src, size_t size, std::vector<u8>& out) {
    using namespace lz_detail;
    std::vector<i32> head((size_t)1 << kHashBits, -1);
    std::vector<i32> prev(size, -1);
    size_t anchor = 0, i = 0;
    auto insert = [&](size_t p) {
        const u32 h = hash4(src + p);
        prev[p] = head[h];
        head[h] = (i32)p;
    };
    while (i + kMinMatch <= size) {
        const u32 h = hash4(src + i);
        size_t bestLen = 0, bestOff = 0;
        i32 cand = head[h];
        for (int chain = 0; cand >= 0 && chain < kMaxChain; chain++) {
            const size_t c = (size_t)cand;
            if (i - c > kWindow) break;
            if (read32(src + c) == read32(src + i)) {
                size_t len = kMinMatch;
                while (i + len < size && src[c + len] == src[i + len]) len++;
                if (len > bestLen) {
                    bestLen = len;
                    bestOff = i - c;
                }
            }
            cand = prev[c];
        }
        if (bestLen >= kMinMatch) {
            sequence(out, src + anchor, i - anchor, bestOff, bestLen);
            const size_t endM = i + bestLen;
            for (; i < endM; i++)
                if (i + kMinMatch <= size) insert(i);
            i = endM;
            anchor = i;
        } else {
            insert(i);
            i++;
        }
    }
    sequence(out, src + anchor, size - anchor, 0, 0);
}

bool lzDecompress(const u8* src, size_t size, u8* dst, size_t dstSize) {
    const u8* p = src;
    const u8* end = src + size;
    size_t o = 0;
    for (;;) {
        if (p >= end) return false;
        const u8 token = *p++;
        size_t lit = token >> 4;
        if (lit == 15) {
            u8 b;
            do {
                if (p >= end) return false;
                b = *p++;
                lit += b;
            } while (b == 255);
        }
        if ((size_t)(end - p) < lit || dstSize - o < lit) return false;
        memcpy(dst + o, p, lit);
        p += lit;
        o += lit;
        if (p == end) return o == dstSize;   // the last sequence: literals only
        if (end - p < 2) return false;
        const size_t off = (size_t)p[0] | ((size_t)p[1] << 8);
        p += 2;
        size_t len = (token & 15u) + lz_detail::kMinMatch;
        if ((token & 15u) == 15) {
            u8 b;
            do {
                if (p >= end) return false;
                b = *p++;
                len += b;
            } while (b == 255);
        }
        if (off == 0 || off > o || dstSize - o < len) return false;
        const u8* from = dst + o - off;
        for (size_t k = 0; k < len; k++) dst[o + k] = from[k];   // byte by byte: a match may overlap its own output
        o += len;
    }
}

}  // namespace World
