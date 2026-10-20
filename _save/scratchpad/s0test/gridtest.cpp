// Round-trip and robustness tests for src/world/gridcodec.cpp (host build, scratch only).
#include "src/core/base.h"
#include "src/world/gridcodec.cpp"
#include <cstdio>
#include <cmath>
#include <cstdlib>
using namespace World;

static u32 rngState = 12345u;
static u32 rnd() { rngState = rngState * 1664525u + 1013904223u; return rngState >> 8; }

static bool roundTrip(const char* name, const std::vector<i16>& v, int w, int h, GridType t) {
    std::vector<u8> enc;
    if (!encodeGrid(v.data(), w, h, t, enc)) { printf("FAIL %s: encode\n", name); return false; }
    std::vector<i16> dec;
    GridInfo gi;
    if (!decodeGrid(enc.data(), enc.size(), dec, &gi) || dec != v) { printf("FAIL %s: decode\n", name); return false; }
    // corruptions must fail cleanly or decode to something, never crash
    int rejected = 0;
    for (int k = 0; k < 200; k++) {
        std::vector<u8> bad = enc;
        size_t pos = kGridHeaderBytes + (bad.size() > kGridHeaderBytes ? rnd() % (bad.size() - kGridHeaderBytes) : 0);
        if (pos < bad.size()) bad[pos] ^= (u8)(1u << (rnd() % 8));
        std::vector<i16> d2;
        if (!decodeGrid(bad.data(), bad.size(), d2, nullptr)) rejected++;
    }
    // truncations
    for (size_t cut = 0; cut < enc.size(); cut += 1 + enc.size() / 37) {
        std::vector<i16> d2;
        if (decodeGrid(enc.data(), cut, d2, nullptr)) { printf("FAIL %s: truncated to %zu decoded\n", name, cut); return false; }
    }
    printf("ok   %-14s %5dx%-5d raw %8zu -> %8zu bytes (%5.1f%%), %d/200 corruptions rejected\n", name, w, h, v.size() * (t == GRID_U8 ? 1 : 2), enc.size(),
           100.0 * enc.size() / (v.size() * (t == GRID_U8 ? 1 : 2)), rejected);
    return true;
}

int main() {
    bool ok = true;
    // edge sizes
    for (int w = 1; w <= 5; w++)
        for (int h = 1; h <= 4; h++) {
            std::vector<i16> v((size_t)w * h);
            for (auto& x : v) x = (i16)((int)(rnd() % 65536) - 32768);
            char nm[32]; snprintf(nm, sizeof nm, "rand%dx%d", w, h);
            ok &= roundTrip(nm, v, w, h, GRID_I16);
            for (auto& x : v) x = 7;
            snprintf(nm, sizeof nm, "const%dx%d", w, h);
            ok &= roundTrip(nm, v, w, h, GRID_I16);
        }
    // extremes
    { std::vector<i16> v(64 * 64); for (size_t i = 0; i < v.size(); i++) v[i] = (i & 1) ? 32767 : -32768; ok &= roundTrip("extremes", v, 64, 64, GRID_I16); }
    // smooth terrain-like, with flat sea
    { int W = 700, H = 500; std::vector<i16> v((size_t)W * H);
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        double hgt = 300.0 * sin(x * 0.013) * cos(y * 0.017) + 120.0 * sin(x * 0.05 + y * 0.03) + (rnd() % 5);
        v[(size_t)y * W + x] = (i16)(hgt < -150 ? -150 : hgt); }
      ok &= roundTrip("smooth", v, W, H, GRID_I16); }
    // class map with regions
    { int W = 640, H = 480; std::vector<i16> v((size_t)W * H);
      for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) v[(size_t)y * W + x] = (i16)(((x / 50) * 7 + (y / 60) * 3) % 24);
      ok &= roundTrip("classes", v, W, H, GRID_U8); }
    // random u8
    { std::vector<i16> v(300 * 200); for (auto& x : v) x = (i16)(rnd() % 256); ok &= roundTrip("rand_u8", v, 300, 200, GRID_U8); }
    // bad args
    { std::vector<i16> v(4, 300); std::vector<u8> e; if (encodeGrid(v.data(), 2, 2, GRID_U8, e)) { printf("FAIL u8 range accepted\n"); ok = false; } }
    printf(ok ? "ALL OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
