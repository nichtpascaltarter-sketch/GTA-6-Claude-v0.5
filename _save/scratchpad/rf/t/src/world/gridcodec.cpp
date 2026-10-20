// Lossless grid codec for the baked terrain (gridcodec.h).
//
// Stream: a 32-byte little-endian header, then the bits of the values in row-major order, most significant bit first.
//   header: "NTG1" | width u32 | height u32 | type u8 | version u8 | 0 u16 | payload bytes u32 | hash u64 | 0 u32
// Per value, with a = left, b = up, c = up-left, d = up-right (the row above the first is zeros; at the row's ends the
// missing neighbours take b's value):
//   - if a == b == c == d: run mode. The run of values equal to a is sent in blocks of 2^kRunBits[i] ('1' per full
//     block, i up after each), then either the row ends ('1' flushes a partial block) or a '0', the remaining count in
//     kRunBits[i] bits, and the value that broke the run as a residual against b in its own context; then i down by one.
//   - otherwise the residual against the median edge predictor of a, b and c, in the context given by the bit length of
//     the activity |d - b| + |b - c| + |c - a|.
// A residual e is mapped to m = 2e (e >= 0) or -2e - 1 and sent as Golomb-Rice with the context's k: m >> k in unary
// (ones, then a zero), then the low k bits. 24 ones escape to m in 17 bits. Each context keeps the sum of |e| and a count,
// halved at 64, and k is the smallest with count << k >= sum.
#include "gridcodec.h"

namespace World {

namespace gridcodec_detail {

constexpr u32 kMagic = 0x3147544Eu;   // "NTG1" read as a little-endian u32
constexpr u8 kVersion = 1;
constexpr int kActivityContexts = 16;
constexpr int kRunContext = kActivityContexts;   // the value that breaks a run
constexpr int kContexts = kActivityContexts + 1;
constexpr u32 kResetCount = 64;
constexpr int kEscapeOnes = 24;
constexpr int kRawBits = 17;   // a mapped residual is below 2^17, since |e| <= 65535
const u8 kRunBits[32] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15};

inline u32 lowMask(int bits) { return bits >= 32 ? 0xffffffffu : ((1u << bits) - 1u); }
inline int absInt(int v) { return v < 0 ? -v : v; }
inline int bitLength(u32 v) {
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}
inline int predictMed(int a, int b, int c) {
    const int lo = a < b ? a : b, hi = a < b ? b : a;
    if (c >= hi) return lo;
    if (c <= lo) return hi;
    return a + b - c;
}
inline int activityContext(int a, int b, int c, int d) {
    const int n = bitLength((u32)(absInt(d - b) + absInt(b - c) + absInt(c - a)));
    return n < kActivityContexts - 1 ? n : kActivityContexts - 1;
}

struct Contexts {
    u32 sum[kContexts];
    u32 count[kContexts];
    Contexts() {
        for (int i = 0; i < kContexts; i++) {
            sum[i] = 4;
            count[i] = 1;
        }
    }
    int param(int ctx) const {
        int k = 0;
        while ((count[ctx] << k) < sum[ctx] && k < 24) k++;
        return k;
    }
    void update(int ctx, u32 absErr) {
        sum[ctx] += absErr;
        if (++count[ctx] >= kResetCount) {
            sum[ctx] >>= 1;
            count[ctx] >>= 1;
        }
    }
};

struct BitWriter {
    std::vector<u8>& out;
    u64 acc = 0;
    int bits = 0;   // pending bits in acc (fewer than 8 between calls)
    explicit BitWriter(std::vector<u8>& o) : out(o) {}
    void put(u32 v, int count) {   // count <= 32
        acc = (acc << count) | (v & lowMask(count));
        bits += count;
        while (bits >= 8) {
            bits -= 8;
            out.push_back((u8)(acc >> bits));
        }
        acc &= (1ull << bits) - 1ull;
    }
    void ones(int count) {
        while (count > 0) {
            const int c = count > 24 ? 24 : count;
            put(lowMask(c), c);
            count -= c;
        }
    }
    void flush() {
        if (bits > 0) out.push_back((u8)(acc << (8 - bits)));
        acc = 0;
        bits = 0;
    }
};

struct BitReader {
    const u8* p;
    const u8* end;
    u64 acc = 0;
    int bits = 0;
    bool overrun = false;
    BitReader(const u8* b, const u8* e) : p(b), end(e) {}
    u32 get(int count) {   // count <= 32
        while (bits < count) {
            acc = (acc << 8) | (p < end ? *p : 0u);
            if (p < end) p++;
            else overrun = true;
            bits += 8;
        }
        bits -= count;
        const u32 v = (u32)(acc >> bits) & lowMask(count);
        acc &= (1ull << bits) - 1ull;
        return v;
    }
    int bit() { return (int)get(1); }
};

void putResidual(BitWriter& w, Contexts& cx, int ctx, int e) {
    const int k = cx.param(ctx);
    const u32 m = e >= 0 ? (u32)e << 1 : ((u32)(-e) << 1) - 1u;
    const u32 q = m >> k;
    if (q >= (u32)kEscapeOnes) {
        w.ones(kEscapeOnes);
        w.put(m, kRawBits);
    } else {
        w.ones((int)q);
        w.put(0u, 1);
        w.put(m & lowMask(k), k);
    }
    cx.update(ctx, (u32)absInt(e));
}

int getResidual(BitReader& r, Contexts& cx, int ctx) {
    const int k = cx.param(ctx);
    int q = 0;
    while (q < kEscapeOnes && r.bit()) q++;
    const u32 m = q == kEscapeOnes ? r.get(kRawBits) : (((u32)q << k) | r.get(k));
    const int e = (m & 1u) ? -(int)((m + 1u) >> 1) : (int)(m >> 1);
    cx.update(ctx, (u32)absInt(e));
    return e;
}

void put32(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}
u32 get32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

}  // namespace gridcodec_detail

u64 gridHash(const i16* values, size_t count) {
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < count; i++) {
        const u16 v = (u16)values[i];
        h = (h ^ (u64)(v & 0xffu)) * 0x100000001b3ull;
        h = (h ^ (u64)(v >> 8)) * 0x100000001b3ull;
    }
    return h;
}

bool encodeGrid(const i16* values, int width, int height, GridType type, std::vector<u8>& out) {
    using namespace gridcodec_detail;
    out.clear();
    if (!values || width <= 0 || height <= 0 || width > 65535 || height > 65535 || (type != GRID_I16 && type != GRID_U8)) return false;
    const size_t count = (size_t)width * (size_t)height;
    if (type == GRID_U8)
        for (size_t i = 0; i < count; i++)
            if (values[i] < 0 || values[i] > 255) return false;
    out.resize(kGridHeaderBytes, 0);
    BitWriter w(out);
    Contexts cx;
    int runIndex = 0;
    const i16* prevRow = nullptr;
    for (int y = 0; y < height; y++) {
        const i16* row = values + (size_t)y * (size_t)width;
        int x = 0;
        while (x < width) {
            const int b = prevRow ? prevRow[x] : 0;
            const int a = x > 0 ? row[x - 1] : b;
            const int c = x > 0 ? (prevRow ? prevRow[x - 1] : 0) : b;
            const int d = x + 1 < width ? (prevRow ? prevRow[x + 1] : 0) : b;
            if (a == b && b == c && c == d) {
                int run = 0;
                while (x + run < width && row[x + run] == a) run++;
                int left = run;
                while (left >= (1 << kRunBits[runIndex])) {
                    w.put(1u, 1);
                    left -= 1 << kRunBits[runIndex];
                    if (runIndex < 31) runIndex++;
                }
                if (x + run == width) {   // the run reaches the end of the row
                    if (left > 0) w.put(1u, 1);
                    x = width;
                    continue;
                }
                w.put(0u, 1);
                w.put((u32)left, kRunBits[runIndex]);
                x += run;
                const int up = prevRow ? prevRow[x] : 0;
                putResidual(w, cx, kRunContext, row[x] - up);
                if (runIndex > 0) runIndex--;
                x++;
                continue;
            }
            putResidual(w, cx, activityContext(a, b, c, d), row[x] - predictMed(a, b, c));
            x++;
        }
        prevRow = row;
    }
    w.flush();
    u8* h = out.data();
    put32(h, kMagic);
    put32(h + 4, (u32)width);
    put32(h + 8, (u32)height);
    h[12] = (u8)type;
    h[13] = kVersion;
    put32(h + 16, (u32)(out.size() - kGridHeaderBytes));
    const u64 hash = gridHash(values, count);
    put32(h + 20, (u32)hash);
    put32(h + 24, (u32)(hash >> 32));
    return true;
}

bool readGridInfo(const u8* data, size_t size, GridInfo* info) {
    using namespace gridcodec_detail;
    if (!data || size < kGridHeaderBytes || get32(data) != kMagic || data[13] != kVersion) return false;
    GridInfo gi;
    gi.width = (int)get32(data + 4);
    gi.height = (int)get32(data + 8);
    gi.type = data[12];
    gi.payloadBytes = get32(data + 16);
    gi.hash = (u64)get32(data + 20) | ((u64)get32(data + 24) << 32);
    if (gi.width <= 0 || gi.height <= 0 || gi.width > 65535 || gi.height > 65535 || (gi.type != GRID_I16 && gi.type != GRID_U8)) return false;
    if (gi.payloadBytes > size - kGridHeaderBytes) return false;
    if (info) *info = gi;
    return true;
}

bool decodeGrid(const u8* data, size_t size, std::vector<i16>& values, GridInfo* info) {
    using namespace gridcodec_detail;
    GridInfo gi;
    if (!readGridInfo(data, size, &gi)) return false;
    const int width = gi.width, height = gi.height;
    const int lo = gi.type == GRID_U8 ? 0 : -32768, hi = gi.type == GRID_U8 ? 255 : 32767;
    values.assign((size_t)width * (size_t)height, 0);
    BitReader r(data + kGridHeaderBytes, data + kGridHeaderBytes + gi.payloadBytes);
    Contexts cx;
    int runIndex = 0;
    const i16* prevRow = nullptr;
    for (int y = 0; y < height; y++) {
        i16* row = values.data() + (size_t)y * (size_t)width;
        int x = 0;
        while (x < width) {
            const int b = prevRow ? prevRow[x] : 0;
            const int a = x > 0 ? row[x - 1] : b;
            const int c = x > 0 ? (prevRow ? prevRow[x - 1] : 0) : b;
            const int d = x + 1 < width ? (prevRow ? prevRow[x + 1] : 0) : b;
            if (a == b && b == c && c == d) {
                bool rowDone = false;
                while (r.bit()) {
                    const int block = 1 << kRunBits[runIndex];
                    const int n = block < width - x ? block : width - x;
                    for (int k = 0; k < n; k++) row[x++] = (i16)a;
                    if (n == block && runIndex < 31) runIndex++;
                    if (x == width) {
                        rowDone = true;
                        break;
                    }
                }
                if (rowDone) break;
                const int left = (int)r.get(kRunBits[runIndex]);
                if (left >= width - x) return false;   // a run that breaks must leave the value that breaks it
                for (int k = 0; k < left; k++) row[x++] = (i16)a;
                const int up = prevRow ? prevRow[x] : 0;
                const int v = up + getResidual(r, cx, kRunContext);
                if (v < lo || v > hi) return false;
                row[x++] = (i16)v;
                if (runIndex > 0) runIndex--;
                if (r.overrun) return false;
                continue;
            }
            const int v = predictMed(a, b, c) + getResidual(r, cx, activityContext(a, b, c, d));
            if (v < lo || v > hi) return false;
            row[x++] = (i16)v;
        }
        if (r.overrun) return false;
        prevRow = row;
    }
    if (gridHash(values.data(), values.size()) != gi.hash) return false;
    if (info) *info = gi;
    return true;
}

}  // namespace World
