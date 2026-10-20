#include "noise.h"
#include "rng.h"

static FORCEINLINE float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

float valueNoise2(float x, float y, u32 seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float u = fade(fx), v = fade(fy);
    float a = hashToFloat(hash3i(xi, yi, (int)seed)), b = hashToFloat(hash3i(xi + 1, yi, (int)seed));
    float c = hashToFloat(hash3i(xi, yi + 1, (int)seed)), d = hashToFloat(hash3i(xi + 1, yi + 1, (int)seed));
    return Lerp(Lerp(a, b, u), Lerp(c, d, u), v) * 2.f - 1.f;
}

static FORCEINLINE float grad2(u32 h, float x, float y) {
    // 8 gradient directions
    switch (h & 7) {
        case 0: return x + y;
        case 1: return x - y;
        case 2: return -x + y;
        case 3: return -x - y;
        case 4: return x * 1.41421f;
        case 5: return -x * 1.41421f;
        case 6: return y * 1.41421f;
        default: return -y * 1.41421f;
    }
}

float perlin2(float x, float y, u32 seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float u = fade(fx), v = fade(fy);
    float n00 = grad2(hash3i(xi, yi, (int)seed), fx, fy);
    float n10 = grad2(hash3i(xi + 1, yi, (int)seed), fx - 1.f, fy);
    float n01 = grad2(hash3i(xi, yi + 1, (int)seed), fx, fy - 1.f);
    float n11 = grad2(hash3i(xi + 1, yi + 1, (int)seed), fx - 1.f, fy - 1.f);
    return Lerp(Lerp(n00, n10, u), Lerp(n01, n11, u), v) * 0.7071f;
}

static FORCEINLINE float grad3(u32 h, float x, float y, float z) {
    u32 hh = h & 15;
    float u = hh < 8 ? x : y;
    float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : z);
    return ((hh & 1) ? -u : u) + ((hh & 2) ? -v : v);
}

float perlin3(float x, float y, float z, u32 seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
    float fx = x - xi, fy = y - yi, fz = z - zi;
    float u = fade(fx), v = fade(fy), w = fade(fz);
    auto H = [&](int a, int b, int c) { return hashCombine(hash3i(xi + a, yi + b, zi + c), seed); };
    float n000 = grad3(H(0, 0, 0), fx, fy, fz);
    float n100 = grad3(H(1, 0, 0), fx - 1, fy, fz);
    float n010 = grad3(H(0, 1, 0), fx, fy - 1, fz);
    float n110 = grad3(H(1, 1, 0), fx - 1, fy - 1, fz);
    float n001 = grad3(H(0, 0, 1), fx, fy, fz - 1);
    float n101 = grad3(H(1, 0, 1), fx - 1, fy, fz - 1);
    float n011 = grad3(H(0, 1, 1), fx, fy - 1, fz - 1);
    float n111 = grad3(H(1, 1, 1), fx - 1, fy - 1, fz - 1);
    float x0 = Lerp(Lerp(n000, n100, u), Lerp(n010, n110, u), v);
    float x1 = Lerp(Lerp(n001, n101, u), Lerp(n011, n111, u), v);
    return Lerp(x0, x1, w) * 0.9f;
}

float simplex2(float xin, float yin, u32 seed) {
    const float F2 = 0.36602540378f, G2 = 0.2113248654f;
    float s = (xin + yin) * F2;
    int i = (int)floorf(xin + s), j = (int)floorf(yin + s);
    float t = (i + j) * G2;
    float x0 = xin - (i - t), y0 = yin - (j - t);
    int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
    float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
    float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
    float n = 0.f;
    float t0 = 0.5f - x0 * x0 - y0 * y0;
    if (t0 > 0) { t0 *= t0; n += t0 * t0 * grad2(hash3i(i, j, (int)seed), x0, y0); }
    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 > 0) { t1 *= t1; n += t1 * t1 * grad2(hash3i(i + i1, j + j1, (int)seed), x1, y1); }
    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 > 0) { t2 *= t2; n += t2 * t2 * grad2(hash3i(i + 1, j + 1, (int)seed), x2, y2); }
    return 70.f * n * 0.9f;
}

float fbm2(float x, float y, int octaves, float lac, float gain, u32 seed) {
    float sum = 0.f, amp = 1.f, norm = 0.f;
    for (int i = 0; i < octaves; i++) {
        sum += perlin2(x, y, seed + (u32)i * 1013u) * amp;
        norm += amp;
        amp *= gain;
        // rotate domain a little each octave to break grid alignment
        float nx = x * 0.8f - y * 0.6f, ny = x * 0.6f + y * 0.8f;
        x = nx * lac; y = ny * lac;
    }
    return sum / norm;
}

float ridged2(float x, float y, int octaves, float lac, float gain, u32 seed) {
    float sum = 0.f, amp = 0.5f, weight = 1.f;
    for (int i = 0; i < octaves; i++) {
        float n = 1.f - fabsf(perlin2(x, y, seed + (u32)i * 7919u));
        n *= n;
        n *= weight;
        weight = Saturate(n * 2.f);
        sum += n * amp;
        amp *= gain;
        float nx = x * 0.8f - y * 0.6f, ny = x * 0.6f + y * 0.8f;
        x = nx * lac; y = ny * lac;
    }
    return sum;
}

float worley2(float x, float y, u32 seed, vec2* cellPoint, u32* cellHash) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float best = 1e9f;
    vec2 bp;
    u32 bh = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            u32 h = hash3i(xi + dx, yi + dy, (int)seed);
            vec2 p((float)(xi + dx) + hashToFloat(h), (float)(yi + dy) + hashToFloat(hash32(h)));
            float d = length2(p - vec2(x, y));
            if (d < best) { best = d; bp = p; bh = h; }
        }
    if (cellPoint) *cellPoint = bp;
    if (cellHash) *cellHash = bh;
    return sqrtf(best);
}

float warpedFbm2(float x, float y, int octaves, float warp, u32 seed) {
    float qx = fbm2(x, y, 4, 2.f, 0.5f, seed + 11);
    float qy = fbm2(x + 5.2f, y + 1.3f, 4, 2.f, 0.5f, seed + 23);
    return fbm2(x + warp * qx, y + warp * qy, octaves, 2.f, 0.5f, seed + 37);
}
