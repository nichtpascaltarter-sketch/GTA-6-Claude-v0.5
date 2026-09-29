#include "draw2d.h"
#include "../platform/platform.h"

namespace UI {
namespace draw2d_detail {

struct Vtx {
    float x, y, u, v;
    u32 color, color2;
    float p0, p1, p2, p3;
};

struct Glyph {
    float u0, v0, u1, v1;       // atlas uv
    float x0, y0, x1, y1;       // quad offsets relative to pen (em units, y down from baseline)
    float advance;              // em units
    bool valid = false;
};

struct Font {
    Glyph glyphs[128];
    float ascent = 0.8f, descent = 0.2f;  // em units
    float spreadEm = 0.1f;                // SDF spread in em units
};

struct Batch {
    ID3D11ShaderResourceView* tex;
    int start, count;
    vec4 clip;      // circle: cx, cy, r, 1 | rect: x, y, w, h (mode in clipMode)
    int clipMode;   // 0 none, 1 circle, 2 rect
};

struct ClipCBData {
    vec4 screen;   // w, h, 1/w, 1/h
    vec4 clip;
    vec4 clipMode;
};

Font g_fonts[FONT_COUNT];
gfx::Texture g_atlas;
gfx::Buffer g_vb;
const int kMaxVerts = 65536 * 3;
std::vector<Vtx> g_verts;
std::vector<Batch> g_batches;
gfx::VertexShader g_vs;
ID3D11PixelShader* g_ps = nullptr;
gfx::CBuffer<ClipCBData> g_cb;
ID3D11BlendState* g_blend = nullptr;
int g_w = 1, g_h = 1;
vec4 g_clip;
int g_clipMode = 0;
ID3D11ShaderResourceView* g_curTex = nullptr;

const int kAtlasSize = 2048;
const int kEm = 128;         // render size
const int kSdfScale = 2;     // downsample factor
const int kSpread = 12;      // pixels at render resolution

void dt1d(const float* f, int n, float* d, int* v, float* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; q++) {
        float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.f * q - 2.f * v[k]);
        while (s <= z[k]) {
            k--;
            s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.f * q - 2.f * v[k]);
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

void edt(std::vector<float>& g, int w, int h) {
    int n = Max(w, h);
    std::vector<float> f(n), d(n), z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) f[y] = g[y * w + x];
        dt1d(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; y++) g[y * w + x] = d[y];
    }
    for (int y = 0; y < h; y++) {
        dt1d(&g[y * w], w, d.data(), v.data(), z.data());
        for (int x = 0; x < w; x++) g[y * w + x] = d[x];
    }
}

bool buildFont(Font& font, const char* face, const char* fallback, int weight, std::vector<u8>& atlas, int& penX, int& penY,
               int& rowH) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT hf = CreateFontA(-kEm, 0, 0, 0, weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    SelectObject(dc, hf);
    char actual[64] = {};
    GetTextFaceA(dc, 63, actual);
    if (_stricmp(actual, face) != 0 && fallback) {
        DeleteObject(hf);
        hf = CreateFontA(-kEm, 0, 0, 0, weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, fallback);
        SelectObject(dc, hf);
    }
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    font.ascent = (float)tm.tmAscent / kEm;
    font.descent = (float)tm.tmDescent / kEm;
    font.spreadEm = (float)kSpread / kEm;
    const int cell = kEm * 2;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cell;
    bi.bmiHeader.biHeight = -cell;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(dc, bmp);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, OPAQUE);
    const int origin = kEm / 2;
    for (int ch = 32; ch < 127; ch++) {
        Glyph& g = font.glyphs[ch];
        memset(bits, 0, (size_t)cell * cell * 4);
        char s[2] = {(char)ch, 0};
        TextOutA(dc, origin, origin, s, 1);
        GdiFlush();
        ABC abc;
        float advance;
        if (GetCharABCWidthsA(dc, (UINT)ch, (UINT)ch, &abc)) advance = (float)(abc.abcA + (int)abc.abcB + abc.abcC);
        else {
            SIZE sz;
            GetTextExtentPoint32A(dc, s, 1, &sz);
            advance = (float)sz.cx;
        }
        g.advance = advance / kEm;
        // bbox
        const u32* px = (const u32*)bits;
        int bx0 = cell, by0 = cell, bx1 = -1, by1 = -1;
        for (int y = 0; y < cell; y++)
            for (int x = 0; x < cell; x++)
                if ((px[y * cell + x] & 0xff) > 16) {
                    bx0 = Min(bx0, x); by0 = Min(by0, y); bx1 = Max(bx1, x); by1 = Max(by1, y);
                }
        g.valid = true;
        if (bx1 < 0) {  // space
            g.x0 = g.x1 = g.y0 = g.y1 = 0;
            g.u0 = g.u1 = g.v0 = g.v1 = 0;
            continue;
        }
        bx0 = Max(0, bx0 - kSpread); by0 = Max(0, by0 - kSpread);
        bx1 = Min(cell - 1, bx1 + kSpread); by1 = Min(cell - 1, by1 + kSpread);
        // align to downsample factor
        bx0 -= bx0 % kSdfScale; by0 -= by0 % kSdfScale;
        int w = bx1 - bx0 + 1, h = by1 - by0 + 1;
        w += (kSdfScale - w % kSdfScale) % kSdfScale;
        h += (kSdfScale - h % kSdfScale) % kSdfScale;
        std::vector<float> inside((size_t)w * h), outside((size_t)w * h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int sx = bx0 + x, sy = by0 + y;
                bool in = sx < cell && sy < cell && (px[sy * cell + sx] & 0xff) > 127;
                inside[(size_t)y * w + x] = in ? 0.f : 1e20f;
                outside[(size_t)y * w + x] = in ? 1e20f : 0.f;
            }
        edt(inside, w, h);
        edt(outside, w, h);
        int ow = w / kSdfScale, oh = h / kSdfScale;
        if (penX + ow + 1 >= kAtlasSize) { penX = 1; penY += rowH + 1; rowH = 0; }
        if (penY + oh + 1 >= kAtlasSize) { LOG("font atlas full"); break; }
        for (int y = 0; y < oh; y++)
            for (int x = 0; x < ow; x++) {
                float acc = 0;
                for (int sy = 0; sy < kSdfScale; sy++)
                    for (int sx = 0; sx < kSdfScale; sx++) {
                        size_t i = (size_t)(y * kSdfScale + sy) * w + (x * kSdfScale + sx);
                        float dOut = sqrtf(inside[i]), dIn = sqrtf(outside[i]);
                        float sd = dOut > 0 ? dOut - 0.5f : -(dIn - 0.5f);  // + outside
                        acc += sd;
                    }
                acc /= (float)(kSdfScale * kSdfScale);
                float v = 0.5f - acc / (2.f * kSpread);
                atlas[(size_t)(penY + y) * kAtlasSize + penX + x] = (u8)(Saturate(v) * 255.f + 0.5f);
            }
        g.u0 = (float)penX / kAtlasSize;
        g.v0 = (float)penY / kAtlasSize;
        g.u1 = (float)(penX + ow) / kAtlasSize;
        g.v1 = (float)(penY + oh) / kAtlasSize;
        float baseline = (float)origin + tm.tmAscent;
        g.x0 = (float)(bx0 - origin) / kEm;
        g.x1 = (float)(bx0 + w - origin) / kEm;
        g.y0 = (float)(by0 - baseline) / kEm;
        g.y1 = (float)(by0 + h - baseline) / kEm;
        penX += ow + 1;
        rowH = Max(rowH, oh);
    }
    DeleteObject(bmp);
    DeleteObject(hf);
    DeleteDC(dc);
    return true;
}

void flushBatchIfNeeded(ID3D11ShaderResourceView* tex) {
    bool needNew = g_batches.empty() || g_batches.back().tex != tex || g_batches.back().clipMode != g_clipMode ||
                   memcmp(&g_batches.back().clip, &g_clip, sizeof(vec4)) != 0;
    if (needNew) {
        Batch b;
        b.tex = tex;
        b.start = (int)g_verts.size();
        b.count = 0;
        b.clip = g_clip;
        b.clipMode = g_clipMode;
        g_batches.push_back(b);
    }
}

void pushQuad(const Vtx& a, const Vtx& b, const Vtx& c, const Vtx& d, ID3D11ShaderResourceView* tex) {
    if ((int)g_verts.size() + 6 > kMaxVerts) return;
    flushBatchIfNeeded(tex);
    g_verts.push_back(a);
    g_verts.push_back(b);
    g_verts.push_back(c);
    g_verts.push_back(a);
    g_verts.push_back(c);
    g_verts.push_back(d);
    g_batches.back().count += 6;
}

Vtx V(float x, float y, float u, float v, u32 c, u32 c2, float p0, float p1 = 0, float p2 = 0, float p3 = 0) {
    Vtx t;
    t.x = x; t.y = y; t.u = u; t.v = v; t.color = c; t.color2 = c2;
    t.p0 = p0; t.p1 = p1; t.p2 = p2; t.p3 = p3;
    return t;
}

}  // namespace draw2d_detail

using namespace draw2d_detail;

bool init() {
    std::vector<u8> atlas((size_t)kAtlasSize * kAtlasSize, 0);
    int penX = 1, penY = 1, rowH = 0;
    double t0 = Platform::timeSeconds();
    buildFont(g_fonts[FONT_BODY], "Segoe UI Semibold", "Arial", FW_SEMIBOLD, atlas, penX, penY, rowH);
    buildFont(g_fonts[FONT_HEADING], "Bahnschrift SemiBold Condensed", "Arial Narrow", FW_BOLD, atlas, penX, penY, rowH);
    buildFont(g_fonts[FONT_TITLE], "Impact", "Arial Black", FW_BLACK, atlas, penX, penY, rowH);
    LOG("Font atlas built in %.2f s", Platform::timeSeconds() - t0);
    g_atlas = gfx::createTexture2D(kAtlasSize, kAtlasSize, DXGI_FORMAT_R8_UNORM, gfx::TEX_SRV, 1, 1, atlas.data(), kAtlasSize);
    g_vb = gfx::createBuffer(kMaxVerts * sizeof(Vtx), sizeof(Vtx), gfx::BUF_VERTEX | gfx::BUF_DYNAMIC);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    g_vs = gfx::loadVS("ui.hlsl", "vsUI", layout, 5);
    g_ps = gfx::loadPS("ui.hlsl", "psUI");
    g_cb.create();
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    gfx::dev->CreateBlendState(&bd, &g_blend);
    g_verts.reserve(kMaxVerts);
    return true;
}

void shutdown() {
    g_atlas.release();
    g_vb.release();
    SAFE_RELEASE(g_blend);
}

void beginFrame(int w, int h) {
    g_w = Max(1, w);
    g_h = Max(1, h);
    g_verts.clear();
    g_batches.clear();
    g_clipMode = 0;
    g_clip = vec4(0);
}

int screenWidth() { return g_w; }
int screenHeight() { return g_h; }

void setClipCircle(float cx, float cy, float r) {
    if (r <= 0) { g_clipMode = 0; g_clip = vec4(0); }
    else { g_clipMode = 1; g_clip = vec4(cx, cy, r, 0); }
}
void setClipRect(float x, float y, float w, float h) {
    if (w <= 0) { g_clipMode = 0; g_clip = vec4(0); }
    else { g_clipMode = 2; g_clip = vec4(x, y, w, h); }
}

void rect(float x, float y, float w, float h, u32 c) {
    pushQuad(V(x, y, 0, 0, c, 0, 0), V(x + w, y, 1, 0, c, 0, 0), V(x + w, y + h, 1, 1, c, 0, 0), V(x, y + h, 0, 1, c, 0, 0), nullptr);
}
void gradientRect(float x, float y, float w, float h, u32 top, u32 bottom) {
    pushQuad(V(x, y, 0, 0, top, 0, 0), V(x + w, y, 1, 0, top, 0, 0), V(x + w, y + h, 1, 1, bottom, 0, 0),
             V(x, y + h, 0, 1, bottom, 0, 0), nullptr);
}
void roundRect(float x, float y, float w, float h, float r, u32 c, float border, u32 bc) {
    float hw = w * 0.5f, hh = h * 0.5f;
    float pad = 1.5f;
    float x0 = x - pad, y0 = y - pad, x1 = x + w + pad, y1 = y + h + pad;
    r = Clamp(r, 0.f, Min(hw, hh));
    float enc = Min(r, 999.f) + floorf(Clamp(border, 0.f, 99.f) * 10.f + 0.5f) * 1000.f;
    pushQuad(V(x0, y0, -hw - pad, -hh - pad, c, bc, 3, hw, hh, enc), V(x1, y0, hw + pad, -hh - pad, c, bc, 3, hw, hh, enc),
             V(x1, y1, hw + pad, hh + pad, c, bc, 3, hw, hh, enc), V(x0, y1, -hw - pad, hh + pad, c, bc, 3, hw, hh, enc), nullptr);
}
void circle(float cx, float cy, float r, u32 c, float thickness) {
    float e = r + 1.5f;
    pushQuad(V(cx - e, cy - e, -e, -e, c, 0, 4, r, thickness), V(cx + e, cy - e, e, -e, c, 0, 4, r, thickness),
             V(cx + e, cy + e, e, e, c, 0, 4, r, thickness), V(cx - e, cy + e, -e, e, c, 0, 4, r, thickness), nullptr);
}
void line(float x0, float y0, float x1, float y1, float width, u32 c) {
    vec2 d = normalize(vec2(x1 - x0, y1 - y0));
    vec2 n = perp(d) * (width * 0.5f);
    pushQuad(V(x0 + n.x, y0 + n.y, 0, 0, c, 0, 0), V(x1 + n.x, y1 + n.y, 1, 0, c, 0, 0), V(x1 - n.x, y1 - n.y, 1, 1, c, 0, 0),
             V(x0 - n.x, y0 - n.y, 0, 1, c, 0, 0), nullptr);
}
void image(ID3D11ShaderResourceView* srv, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 tint) {
    pushQuad(V(x, y, u0, v0, tint, 0, 2), V(x + w, y, u1, v0, tint, 0, 2), V(x + w, y + h, u1, v1, tint, 0, 2),
             V(x, y + h, u0, v1, tint, 0, 2), srv);
}
void imageRotated(ID3D11ShaderResourceView* srv, float cx, float cy, float w, float h, float ang, float u0, float v0, float u1,
                  float v1, u32 tint) {
    vec2 ax = vec2(cosf(ang), sinf(ang)), ay = perp(ax);
    vec2 c(cx, cy);
    vec2 p0 = c - ax * (w * 0.5f) - ay * (h * 0.5f), p1 = c + ax * (w * 0.5f) - ay * (h * 0.5f);
    vec2 p2 = c + ax * (w * 0.5f) + ay * (h * 0.5f), p3 = c - ax * (w * 0.5f) + ay * (h * 0.5f);
    pushQuad(V(p0.x, p0.y, u0, v0, tint, 0, 2), V(p1.x, p1.y, u1, v0, tint, 0, 2), V(p2.x, p2.y, u1, v1, tint, 0, 2),
             V(p3.x, p3.y, u0, v1, tint, 0, 2), srv);
}

float textWidth(const char* str, const TextStyle& st) {
    const Font& f = g_fonts[st.font];
    float w = 0, best = 0;
    for (const char* p = str; *p; p++) {
        if (*p == '\n') { best = Max(best, w); w = 0; continue; }
        unsigned ch = (unsigned char)*p;
        if (ch >= 128 || !f.glyphs[ch].valid) ch = '?';
        w += (f.glyphs[ch].advance + st.tracking) * st.size;
    }
    return Max(best, w);
}

float text(float x, float y, const char* str, const TextStyle& st) {
    const Font& f = g_fonts[st.font];
    float w = textWidth(str, st);
    if (st.align == ALIGN_CENTER) x -= w * 0.5f;
    else if (st.align == ALIGN_RIGHT) x -= w;
    float s = st.size;
    // SDF smoothing: px per em unit spread
    float pxRange = f.spreadEm * s * 2.f;
    float outlineSdf = st.outline / Max(pxRange, 1e-3f);
    auto emit = [&](float ox, float oy, u32 color, u32 color2, float outl) {
        float penX = x + ox, baseline = y + f.ascent * s + oy;
        for (const char* p = str; *p; p++) {
            if (*p == '\n') { penX = x + ox; baseline += s * 1.2f; continue; }
            unsigned ch = (unsigned char)*p;
            if (ch >= 128 || !f.glyphs[ch].valid) ch = '?';
            const Glyph& g = f.glyphs[ch];
            if (g.u1 > g.u0) {
                float x0 = penX + g.x0 * s, x1 = penX + g.x1 * s, y0 = baseline + g.y0 * s, y1 = baseline + g.y1 * s;
                pushQuad(V(x0, y0, g.u0, g.v0, color, color2, 1, pxRange, outl), V(x1, y0, g.u1, g.v0, color, color2, 1, pxRange, outl),
                         V(x1, y1, g.u1, g.v1, color, color2, 1, pxRange, outl), V(x0, y1, g.u0, g.v1, color, color2, 1, pxRange, outl),
                         g_atlas.srv);
            }
            penX += (g.advance + st.tracking) * s;
        }
    };
    if (st.shadow > 0.f) emit(st.shadow, st.shadow, withAlpha(0xff000000u, 0.6f * ((st.color >> 24) / 255.f)), 0, outlineSdf + 0.08f);
    emit(0, 0, st.color, st.outlineColor, outlineSdf);
    return w;
}

void textWrapped(float x, float y, float maxW, const char* str, const TextStyle& st, float lineSpacing) {
    std::string line, word;
    float lineY = y;
    auto flush = [&]() {
        if (!line.empty()) text(x, lineY, line.c_str(), st);
        lineY += st.size * lineSpacing;
        line.clear();
    };
    const char* p = str;
    while (true) {
        char ch = *p;
        if (ch == ' ' || ch == '\n' || ch == 0) {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (textWidth(candidate.c_str(), st) > maxW && !line.empty()) {
                flush();
                line = word;
            } else line = candidate;
            word.clear();
            if (ch == '\n') flush();
            if (ch == 0) break;
        } else word.push_back(ch);
        p++;
    }
    if (!line.empty()) flush();
}

void endFrame() {
    if (g_verts.empty()) return;
    auto* c = gfx::ctx;
    gfx::updateBuffer(g_vb, g_verts.data(), (u32)(g_verts.size() * sizeof(Vtx)));
    UINT stride = sizeof(Vtx), offset = 0;
    c->IASetVertexBuffers(0, 1, &g_vb.buf, &stride, &offset);
    c->IASetInputLayout(g_vs.layout);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(g_vs.vs, nullptr, 0);
    c->PSSetShader(g_ps, nullptr, 0);
    c->OMSetBlendState(g_blend, nullptr, 0xffffffff);
    c->OMSetDepthStencilState(gfx::states.depthOff, 0);
    c->RSSetState(gfx::states.cullNone);
    gfx::setViewport((float)g_w, (float)g_h);
    ID3D11SamplerState* samps[] = {gfx::states.linearClamp};
    c->PSSetSamplers(1, 1, samps);
    for (auto& b : g_batches) {
        if (b.count == 0) continue;
        g_cb.data.screen = vec4((float)g_w, (float)g_h, 1.f / g_w, 1.f / g_h);
        g_cb.data.clip = b.clip;
        g_cb.data.clipMode = vec4((float)b.clipMode, 0, 0, 0);
        g_cb.upload();
        ID3D11Buffer* cbs[] = {g_cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        ID3D11ShaderResourceView* srvs[2] = {g_atlas.srv, b.tex ? b.tex : g_atlas.srv};
        c->PSSetShaderResources(0, 2, srvs);
        c->Draw((UINT)b.count, (UINT)b.start);
    }
    ID3D11ShaderResourceView* nulls[2] = {};
    c->PSSetShaderResources(0, 2, nulls);
}

}  // namespace UI
