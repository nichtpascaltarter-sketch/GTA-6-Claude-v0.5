// Deterministic random numbers and hashing. All world content derives from these.
#pragma once
#include "math.h"

FORCEINLINE u32 hash32(u32 x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
FORCEINLINE u32 hash2i(int x, int y) { return hash32((u32)x * 0x8da6b343U ^ hash32((u32)y + 0x9e3779b9U)); }
FORCEINLINE u32 hash3i(int x, int y, int z) { return hash32((u32)x * 0x8da6b343U ^ (u32)y * 0xd8163841U ^ hash32((u32)z + 0x7f4a7c15U)); }
FORCEINLINE u32 hashCombine(u32 a, u32 b) { return hash32(a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2))); }
FORCEINLINE float hashToFloat(u32 h) { return (float)(h >> 8) * (1.f / 16777216.f); }
FORCEINLINE u64 hash64(const void* data, size_t n) {
    const u8* p = (const u8*)data;
    u64 h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
FORCEINLINE u32 hashString(const char* s) {
    u32 h = 2166136261U;
    while (*s) { h ^= (u8)*s++; h *= 16777619U; }
    return h;
}

// PCG32 generator.
struct Rng {
    u64 state, inc;
    explicit Rng(u64 seed = 0x853c49e6748fea9bULL, u64 seq = 0xda3e39cb94b95bdbULL) {
        state = 0; inc = (seq << 1u) | 1u;
        next(); state += seed; next();
    }
    u32 next() {
        u64 old = state;
        state = old * 6364136223846793005ULL + inc;
        u32 xs = (u32)(((old >> 18u) ^ old) >> 27u);
        u32 rot = (u32)(old >> 59u);
        return (xs >> rot) | (xs << ((-(i32)rot) & 31));
    }
    float f() { return (float)(next() >> 8) * (1.f / 16777216.f); }  // [0,1)
    float range(float a, float b) { return a + (b - a) * f(); }
    int irange(int a, int b) { return b <= a ? a : a + (int)(next() % (u32)(b - a + 1)); }  // inclusive
    bool chance(float p) { return f() < p; }
    float gauss() {
        float u1 = Max(f(), 1e-7f), u2 = f();
        return sqrtf(-2.f * logf(u1)) * cosf(kTwoPi * u2);
    }
    vec2 inCircle() {
        float a = f() * kTwoPi, r = sqrtf(f());
        return vec2(cosf(a) * r, sinf(a) * r);
    }
    vec3 onSphere() {
        float z = range(-1.f, 1.f), a = f() * kTwoPi, r = sqrtf(Max(0.f, 1.f - z * z));
        return vec3(r * cosf(a), r * sinf(a), z);
    }
    template <typename T> const T& pick(const std::vector<T>& v) { return v[next() % v.size()]; }
    template <typename T, size_t N> const T& pick(const T (&arr)[N]) { return arr[next() % N]; }
    // Weighted pick: weights array of n floats, returns index.
    int weighted(const float* w, int n) {
        float s = 0; for (int i = 0; i < n; i++) s += w[i];
        float r = f() * s;
        for (int i = 0; i < n; i++) { r -= w[i]; if (r <= 0.f) return i; }
        return n - 1;
    }
};
