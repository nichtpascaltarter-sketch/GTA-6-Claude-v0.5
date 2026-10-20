// Shop sign atlas: renders generated business names with GDI into an RGBA texture (8 x 64 cells of 256x32).
#include "../gfx/gfx.h"
#include "../core/rng.h"

namespace UI {

gfx::Texture buildSignAtlas(const std::vector<std::string>& names) {
    const int cellW = 256, cellH = 32, cols = 8, rows = 64;
    const int W = cellW * cols, H = cellH * rows;
    std::vector<u32> pixels((size_t)W * H, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cellW * 2;
    bi.bmiHeader.biHeight = -cellH * 2;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    const char* faces[] = {"Impact", "Arial Black", "Georgia", "Segoe Script", "Bahnschrift", "Courier New", "Trebuchet MS", "Segoe UI Black"};
    const int weights[] = {FW_NORMAL, FW_BLACK, FW_BOLD, FW_BOLD, FW_BOLD, FW_BOLD, FW_BOLD, FW_BLACK};
    for (int i = 0; i < (int)names.size() && i < cols * rows; i++) {
        Rng r(hash32((u32)i * 7919u + 3u));
        int fi = r.irange(0, 7);
        const std::string& txt = names[i];
        // Fit the text: render at 2x then downsample
        int height = 44;
        HFONT hf = nullptr;
        SIZE sz = {0, 0};
        for (int tries = 0; tries < 12; tries++) {
            if (hf) DeleteObject(hf);
            hf = CreateFontA(-height, 0, 0, 0, weights[fi], fi == 2 && r.chance(0.3f), FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, faces[fi]);
            SelectObject(dc, hf);
            GetTextExtentPoint32A(dc, txt.c_str(), (int)txt.size(), &sz);
            if (sz.cx <= cellW * 2 - 20) break;
            height -= 4;
        }
        memset(bits, 0, (size_t)cellW * 2 * cellH * 2 * 4);
        int x0 = (cellW * 2 - sz.cx) / 2, y0 = (cellH * 2 - sz.cy) / 2;
        TextOutA(dc, x0, y0, txt.c_str(), (int)txt.size());
        GdiFlush();
        if (hf) DeleteObject(hf);
        // Sign color: neon-bright or classic white/red/yellow
        vec3 col;
        int ct = r.irange(0, 5);
        if (ct == 0) col = vec3(1.f, 1.f, 1.f);
        else if (ct == 1) col = vec3(1.f, 0.2f, 0.15f);
        else if (ct == 2) col = vec3(1.f, 0.85f, 0.2f);
        else col = hsvToRgb(r.f(), 0.75f, 1.f);
        int cx = i % cols, cy = i / cols;
        const u32* src = (const u32*)bits;
        for (int y = 0; y < cellH; y++)
            for (int x = 0; x < cellW; x++) {
                float cov = 0;
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++) cov += (float)(src[(y * 2 + sy) * cellW * 2 + x * 2 + sx] & 0xff) / 255.f;
                cov *= 0.25f;
                pixels[(size_t)(cy * cellH + y) * W + cx * cellW + x] = packRGBA8(col.x, col.y, col.z, cov);
            }
    }
    DeleteObject(bmp);
    DeleteDC(dc);
    gfx::Texture t = gfx::createTexture2D(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_GENMIPS, 0, 1);
    gfx::uploadTexture2D(t, 0, 0, pixels.data(), W * 4);
    gfx::ctx->GenerateMips(t.srv);
    return t;
}

}  // namespace UI
