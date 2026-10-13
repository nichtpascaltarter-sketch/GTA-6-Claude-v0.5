#!/bin/sh
# Extracts the shape + raster code from icons.cpp and builds a native previewer that writes the SDF atlas as an image.
SRC=/home/user/GTA-6-Claude-v0.5/src/ui
SP=$(dirname "$0")
sed -n '/^namespace icons_detail {/,/^}  \/\/ namespace icons_detail/p' $SRC/icons.cpp > $SP/icons_body.inc
sed -n "/enum IconId/,/};/p" $SRC/ui_internal.h > $SP/iconid.inc
cat > $SP/prev.cpp <<'EOT'
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include <cstdio>
namespace UI {
enum BlipIcon : unsigned char { BLIP_DOT = 0, BLIP_PLAYER, BLIP_WAYPOINT, BLIP_MISSION, BLIP_OBJECTIVE, BLIP_ENEMY, BLIP_FRIEND, BLIP_POLICE,
    BLIP_POLICE_HELI, BLIP_VEHICLE, BLIP_SAFEHOUSE, BLIP_GUN_SHOP, BLIP_CLOTHES_SHOP, BLIP_CAR_SHOP, BLIP_GARAGE, BLIP_HOSPITAL,
    BLIP_POLICE_STATION, BLIP_RACE, BLIP_TAXI_JOB, BLIP_DELIVERY_JOB, BLIP_VIGILANTE, BLIP_STUNT_JUMP, BLIP_COLLECTIBLE, BLIP_BOAT,
    BLIP_HELI, BLIP_PLANE, BLIP_BAR, BLIP_CONVENIENCE_STORE, BLIP_BANK, BLIP_AIRPORT, BLIP_HIDEOUT, BLIP_COUNT };
namespace uix {
#include "iconid.inc"
constexpr int kWeaponIconCount = 13;
}
namespace draw2d_detail {
void dt1d(const float* f, int n, float* d, int* v, float* z) {
    int k = 0; v[0] = 0; z[0] = -1e20f; z[1] = 1e20f;
    for (int q = 1; q < n; q++) {
        float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.f * q - 2.f * v[k]);
        while (s <= z[k]) { k--; s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.f * q - 2.f * v[k]); }
        k++; v[k] = q; z[k] = s; z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; q++) { while (z[k + 1] < q) k++; d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]]; }
}
void edt(std::vector<float>& g, int w, int h) {
    int n = Max(w, h);
    std::vector<float> f(n), d(n), z(n + 1); std::vector<int> v(n);
    for (int x = 0; x < w; x++) { for (int y = 0; y < h; y++) f[y] = g[y * w + x]; dt1d(f.data(), h, d.data(), v.data(), z.data()); for (int y = 0; y < h; y++) g[y * w + x] = d[y]; }
    for (int y = 0; y < h; y++) { dt1d(&g[y * w], w, d.data(), v.data(), z.data()); for (int x = 0; x < w; x++) g[y * w + x] = d[x]; }
}
}
namespace gfx { struct Texture { void release() {} }; }
using uix::ICO_COUNT;
#include "icons_body.inc"
}
using namespace UI;
using namespace UI::icons_detail;
using namespace UI::uix;
int main(int argc, char** argv) {
    std::vector<u8> atlas((size_t)kAtlasW * kAtlasH, 0);
    for (int j = 0; j < ICO_COUNT; j++) {
        Shape s = makeIcon(j);
        rasterShapeToAtlas(s, 100.f, 100.f, kCell, kCell, (j % kIconsPerRow) * kCell, (j / kIconsPerRow) * kCell, atlas);
    }
    for (int w = 0; w < kWeaponIconCount; w++) {
        Shape s = makeWeapon(w);
        rasterShapeToAtlas(s, 400.f, 150.f, kWpnW, kWpnH, (w % 4) * kWpnW, kWpnY0 + (w / 4) * kWpnH, atlas);
    }
    // render: upscale 2x with smooth threshold, white on dark with grid
    int S = 2, W = kAtlasW * S, H = 768 * S;
    FILE* f = fopen(argc > 1 ? argv[1] : "atlas.pgm", "wb");
    fprintf(f, "P5 %d %d 255\n", W, H);
    std::vector<u8> row(W);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float fx = (x + 0.5f) / S - 0.5f, fy = (y + 0.5f) / S - 0.5f;
            int x0 = Clamp((int)floorf(fx), 0, kAtlasW - 2), y0 = Clamp((int)floorf(fy), 0, kAtlasH - 2);
            float tx = fx - x0, ty = fy - y0;
            auto A = [&](int xx, int yy) { return atlas[(size_t)yy * kAtlasW + xx] / 255.f; };
            float d = Lerp(Lerp(A(x0, y0), A(x0 + 1, y0), tx), Lerp(A(x0, y0 + 1), A(x0 + 1, y0 + 1), tx), ty);
            float pxRange = 16.f * S / 4.f * 4.f / 4.f * 1.f;  // 16 atlas px per unit
            float cov = Saturate((d - 0.5f) * 16.f * S + 0.5f);
            bool grid = ((x / S) % 64 == 0 && y / S < 384) || (y / S >= 384 && ((x / S) % 256 == 0 || ((y / S - 384) % 96) == 0));
            float bg = grid ? 0.25f : 0.08f;
            row[x] = (u8)(Saturate(bg + cov * (1.f - bg)) * 255.f);
            (void)pxRange;
        }
        fwrite(row.data(), 1, W, f);
    }
    fclose(f);
    return 0;
}
EOT
g++ -O2 -std=c++17 -I/home/user/GTA-6-Claude-v0.5/src $SP/prev.cpp -o $SP/prev 2>&1 | grep -E "error" | head -20
$SP/prev $SP/atlas.pgm && convert $SP/atlas.pgm $SP/atlas.png && echo ok
