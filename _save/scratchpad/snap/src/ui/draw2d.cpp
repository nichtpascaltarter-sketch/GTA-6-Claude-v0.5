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
    vec4 clip;      // circle: cx, cy, r, 1 | rect / rounded rect: x, y, w, h (mode in clipMode)
    int clipMode;   // 0 none, 1 circle, 2 rect, 3 rounded rect
    float clipRadius;
};

struct ClipCBData {
    vec4 screen;   // w, h, 1/w, 1/h
    vec4 clip;
    vec4 clipMode; // x mode, y rounded-rect radius
};

struct BlurCBData {
    vec4 texel;    // xy = source texel size, zw = direction (texels)
};

// Photo grading parameters (one set per frame, register b3)
struct PhotoCBData {
    vec4 a;   // filter, strength, exposure (EV), contrast
    vec4 b;   // saturation, temperature, vignette, grain
    vec4 c;   // dof (0/1), focus distance, blur scale, nearZ (0 = no depth: screen-space focus band)
    vec4 d;   // time, aspect (w/h), unused, unused
};

// Pixel shader modes (Vtx::p0); +32 = additive (alpha output 0)
enum Mode { M_SOLID = 0, M_FONT = 1, M_IMAGE = 2, M_RRECT = 3, M_CIRCLE = 4, M_CAPSULE = 5, M_ARC = 6, M_ICON = 7,
            M_MAP = 8, M_BACKDROP = 9, M_TRI = 10, M_PHOTO = 11 };

Font g_fonts[FONT_COUNT];
gfx::Texture g_atlas;
gfx::Buffer g_vb;
const int kMaxVerts = 65536 * 4;
std::vector<Vtx> g_verts;
std::vector<Batch> g_batches;
gfx::VertexShader g_vs;
ID3D11PixelShader* g_ps = nullptr;
gfx::CBuffer<ClipCBData> g_cb;
ID3D11BlendState* g_blend = nullptr;
int g_w = 1, g_h = 1;
vec4 g_clip;
int g_clipMode = 0;
float g_clipRadius = 0.f;
bool g_discard = false;
int g_frameIndex = 0;
ID3D11ShaderResourceView* g_iconSrv = nullptr;
// Backdrop blur
bool g_wantBackdrop = false;
gfx::VertexShader g_vsFull;
ID3D11PixelShader* g_psDown = nullptr;
ID3D11PixelShader* g_psBlur = nullptr;
gfx::CBuffer<BlurCBData> g_blurCB;
ID3D11Texture2D* g_copyTex = nullptr;
ID3D11ShaderResourceView* g_copySrv = nullptr;
gfx::Texture g_half, g_q1, g_q2;
int g_blurW = 0, g_blurH = 0;
DXGI_FORMAT g_blurFmt = DXGI_FORMAT_UNKNOWN;
bool g_blurValid = false;
// Photo grading (photoEffect) and scene depth for its depth of field
gfx::CBuffer<PhotoCBData> g_photoCB;
PhotoCBData g_photo;
bool g_photoUsed = false;
ID3D11ShaderResourceView* g_depthSrv = nullptr;
float g_depthNear = 0.f;
// Snapshots of finished frames (quarter resolution ring)
const int kSnapCount = 8;
gfx::Texture g_snap[kSnapCount];
int g_snapId[kSnapCount] = {-1, -1, -1, -1, -1, -1, -1, -1};
int g_snapNextId = 0;
int g_snapPending = -1;

const int kAtlasSize = 2048;
const int kEm = 128;         // render size
const int kSdfScale = 2;     // downsample factor
const int kSpread = 18;      // pixels at render resolution (outlines / soft shadows / glows need the range)

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
        GetTextFaceA(dc, 63, actual);
    }
    LOG("UI font '%s' -> '%s'", face, actual);
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

bool g_additive = false;

void flushBatchIfNeeded(ID3D11ShaderResourceView* tex) {
    bool needNew = g_batches.empty() || g_batches.back().tex != tex || g_batches.back().clipMode != g_clipMode ||
                   memcmp(&g_batches.back().clip, &g_clip, sizeof(vec4)) != 0 || g_batches.back().clipRadius != g_clipRadius;
    if (needNew) {
        Batch b;
        b.tex = tex;
        b.start = (int)g_verts.size();
        b.count = 0;
        b.clip = g_clip;
        b.clipMode = g_clipMode;
        b.clipRadius = g_clipRadius;
        g_batches.push_back(b);
    }
}

// Solid-color / procedural primitives do not depend on the batch texture: reuse the current batch when possible.
ID3D11ShaderResourceView* anyTex() { return g_batches.empty() ? nullptr : g_batches.back().tex; }

void pushQuad(const Vtx& a, const Vtx& b, const Vtx& c, const Vtx& d, ID3D11ShaderResourceView* tex) {
    if (g_discard || (int)g_verts.size() + 6 > kMaxVerts) return;
    flushBatchIfNeeded(tex);
    size_t base = g_verts.size();
    g_verts.push_back(a);
    g_verts.push_back(b);
    g_verts.push_back(c);
    g_verts.push_back(a);
    g_verts.push_back(c);
    g_verts.push_back(d);
    if (g_additive)
        for (size_t i = base; i < g_verts.size(); i++) g_verts[i].p0 += 32.f;
    g_batches.back().count += 6;
}

void pushTri(const Vtx& a, const Vtx& b, const Vtx& c, ID3D11ShaderResourceView* tex) {
    if (g_discard || (int)g_verts.size() + 3 > kMaxVerts) return;
    flushBatchIfNeeded(tex);
    size_t base = g_verts.size();
    g_verts.push_back(a);
    g_verts.push_back(b);
    g_verts.push_back(c);
    if (g_additive)
        for (size_t i = base; i < g_verts.size(); i++) g_verts[i].p0 += 32.f;
    g_batches.back().count += 3;
}

Vtx V(float x, float y, float u, float v, u32 c, u32 c2, float p0, float p1 = 0, float p2 = 0, float p3 = 0) {
    Vtx t;
    t.x = x; t.y = y; t.u = u; t.v = v; t.color = c; t.color2 = c2;
    t.p0 = p0; t.p1 = p1; t.p2 = p2; t.p3 = p3;
    return t;
}

// AA triangle: per-vertex distance to each edge (edges flagged interior get a huge constant distance).
void aaTri(vec2 a, vec2 b, vec2 c, u32 ca, u32 cb, u32 cc, bool eAB, bool eBC, bool eCA) {
    float area2 = fabsf(cross(b - a, c - a));
    if (area2 < 1e-6f) return;
    float lab = length(b - a), lbc = length(c - b), lca = length(a - c);
    const float kFar = 1e5f;
    // p1: distance to edge BC, p2: to edge CA, p3: to edge AB
    float dA_bc = eBC ? area2 / Max(lbc, 1e-6f) : kFar;
    float dB_ca = eCA ? area2 / Max(lca, 1e-6f) : kFar;
    float dC_ab = eAB ? area2 / Max(lab, 1e-6f) : kFar;
    Vtx va = V(a.x, a.y, 0, 0, ca, 0, M_TRI, dA_bc, eCA ? 0.f : kFar, eAB ? 0.f : kFar);
    Vtx vb = V(b.x, b.y, 0, 0, cb, 0, M_TRI, eBC ? 0.f : kFar, dB_ca, eAB ? 0.f : kFar);
    Vtx vc = V(c.x, c.y, 0, 0, cc, 0, M_TRI, eBC ? 0.f : kFar, eCA ? 0.f : kFar, dC_ab);
    pushTri(va, vb, vc, anyTex());
}

bool pointInTri(vec2 p, vec2 a, vec2 b, vec2 c) {
    float d1 = cross(b - a, p - a), d2 = cross(c - b, p - b), d3 = cross(a - c, p - c);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0), pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

void releaseBlur() {
    SAFE_RELEASE(g_copySrv);
    SAFE_RELEASE(g_copyTex);
    g_half.release();
    g_q1.release();
    g_q2.release();
    g_blurW = g_blurH = 0;
    g_blurValid = false;
}

// (Re)creates the full-resolution copy and the half / quarter targets for a render target of this size and format.
bool ensureCaptureTargets(const D3D11_TEXTURE2D_DESC& td) {
    if ((int)td.Width == g_blurW && (int)td.Height == g_blurH && td.Format == g_blurFmt && g_copyTex) return true;
    releaseBlur();
    D3D11_TEXTURE2D_DESC cd = td;
    cd.MipLevels = 1;
    cd.ArraySize = 1;
    cd.SampleDesc.Count = 1;
    cd.SampleDesc.Quality = 0;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cd.CPUAccessFlags = 0;
    cd.MiscFlags = 0;
    if (SUCCEEDED(gfx::dev->CreateTexture2D(&cd, nullptr, &g_copyTex))) gfx::dev->CreateShaderResourceView(g_copyTex, nullptr, &g_copySrv);
    int hw = Max(1, (int)td.Width / 2), hh = Max(1, (int)td.Height / 2);
    int qw = Max(1, (int)td.Width / 4), qh = Max(1, (int)td.Height / 4);
    g_half = gfx::createTexture2D(hw, hh, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_RTV);
    g_q1 = gfx::createTexture2D(qw, qh, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_RTV);
    g_q2 = gfx::createTexture2D(qw, qh, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_RTV);
    g_blurW = (int)td.Width;
    g_blurH = (int)td.Height;
    g_blurFmt = td.Format;
    return g_copyTex != nullptr;
}

// Pipeline state for the fullscreen passes below (restored by endFrame's own setup afterwards).
void beginBlitPasses() {
    auto* c = gfx::ctx;
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(g_vsFull.vs, nullptr, 0);
    c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
    c->OMSetDepthStencilState(gfx::states.depthOff, 0);
    c->RSSetState(gfx::states.cullNone);
    ID3D11SamplerState* samps[] = {gfx::states.linearClamp};
    c->PSSetSamplers(1, 1, samps);
}

void blitPass(ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, int sw, int sh, const gfx::Texture& dst, vec2 dir) {
    auto* c = gfx::ctx;
    ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
    c->PSSetShaderResources(0, 1, nullSrv);
    c->OMSetRenderTargets(1, &dst.rtv, nullptr);
    gfx::setViewport((float)dst.width, (float)dst.height);
    g_blurCB.data.texel = vec4(1.f / sw, 1.f / sh, dir.x, dir.y);
    g_blurCB.upload();
    ID3D11Buffer* cbs[] = {g_blurCB.get()};
    c->PSSetConstantBuffers(2, 1, cbs);
    c->PSSetShader(ps, nullptr, 0);
    c->PSSetShaderResources(0, 1, &src);
    c->Draw(3, 0);
    c->PSSetShaderResources(0, 1, nullSrv);
}

// Captures the currently bound render target: full-resolution copy (g_copySrv), half resolution (g_half) and a
// quarter-resolution blurred copy (g_q1).
void captureBackdrop() {
    auto* c = gfx::ctx;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    c->OMGetRenderTargets(1, &rtv, &dsv);
    if (!rtv) { SAFE_RELEASE(dsv); return; }
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    D3D11_TEXTURE2D_DESC td = {};
    ((ID3D11Texture2D*)res)->GetDesc(&td);
    if (ensureCaptureTargets(td) && td.SampleDesc.Count == 1) {
        c->CopyResource(g_copyTex, res);
        beginBlitPasses();
        blitPass(g_psDown, g_copySrv, g_blurW, g_blurH, g_half, vec2(0, 0));
        blitPass(g_psDown, g_half.srv, g_half.width, g_half.height, g_q1, vec2(0, 0));
        for (int it = 0; it < 2; it++) {
            blitPass(g_psBlur, g_q1.srv, g_q1.width, g_q1.height, g_q2, vec2(1.f + it, 0));
            blitPass(g_psBlur, g_q2.srv, g_q2.width, g_q2.height, g_q1, vec2(0, 1.f + it));
        }
        g_blurValid = true;
    }
    c->OMSetRenderTargets(1, &rtv, dsv);
    SAFE_RELEASE(res);
    SAFE_RELEASE(rtv);
    SAFE_RELEASE(dsv);
}

// Copies the finished frame (after the UI draws) into snapshot slot `id % kSnapCount` at quarter resolution.
void captureSnapshot(int id) {
    auto* c = gfx::ctx;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    c->OMGetRenderTargets(1, &rtv, &dsv);
    if (!rtv) { SAFE_RELEASE(dsv); return; }
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    D3D11_TEXTURE2D_DESC td = {};
    ((ID3D11Texture2D*)res)->GetDesc(&td);
    if (ensureCaptureTargets(td) && td.SampleDesc.Count == 1) {
        int slot = id % kSnapCount;
        int qw = Max(1, (int)td.Width / 4), qh = Max(1, (int)td.Height / 4);
        gfx::Texture& snap = g_snap[slot];
        if (snap.width != qw || snap.height != qh || !snap.rtv) {
            snap.release();
            snap = gfx::createTexture2D(qw, qh, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_RTV);
        }
        c->CopyResource(g_copyTex, res);
        beginBlitPasses();
        blitPass(g_psDown, g_copySrv, g_blurW, g_blurH, g_half, vec2(0, 0));
        blitPass(g_psDown, g_half.srv, g_half.width, g_half.height, snap, vec2(0, 0));
        g_snapId[slot] = id;
    }
    c->OMSetRenderTargets(1, &rtv, dsv);
    SAFE_RELEASE(res);
    SAFE_RELEASE(rtv);
    SAFE_RELEASE(dsv);
}

}  // namespace draw2d_detail

using namespace draw2d_detail;

u32 lerpColor(u32 a, u32 b, float t) {
    t = Saturate(t);
    u32 out = 0;
    for (int i = 0; i < 4; i++) {
        float ca = (float)((a >> (i * 8)) & 255), cb = (float)((b >> (i * 8)) & 255);
        u32 v = (u32)(ca + (cb - ca) * t + 0.5f);
        out |= (v & 255) << (i * 8);
    }
    return out;
}

bool init() {
    std::vector<u8> atlas((size_t)kAtlasSize * kAtlasSize, 0);
    int penX = 1, penY = 1, rowH = 0;
    double t0 = Platform::timeSeconds();
    buildFont(g_fonts[FONT_BODY], "Segoe UI Semibold", "Arial", FW_SEMIBOLD, atlas, penX, penY, rowH);
    buildFont(g_fonts[FONT_HEADING], "Bahnschrift SemiBold Condensed", "Arial Narrow", FW_BOLD, atlas, penX, penY, rowH);
    buildFont(g_fonts[FONT_TITLE], "Impact", "Arial Black", FW_BLACK, atlas, penX, penY, rowH);
    LOG("Font atlas built in %.2f s", Platform::timeSeconds() - t0);
    if (Platform::hasArg("dumpuifont")) {
        // test tooling: atlas + glyph metrics for offline UI previews
        std::string path = Platform::userDataDir() + "ui_font.bin";
        if (FILE* f = fopen(path.c_str(), "wb")) {
            u32 magic = 0x4E54464Eu, size = kAtlasSize, nf = FONT_COUNT;
            fwrite(&magic, 4, 1, f);
            fwrite(&size, 4, 1, f);
            fwrite(&nf, 4, 1, f);
            fwrite(g_fonts, sizeof(Font), FONT_COUNT, f);
            fwrite(atlas.data(), 1, atlas.size(), f);
            fclose(f);
            LOG("UI font atlas dumped to %s", path.c_str());
        }
    }
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
    g_vsFull = gfx::loadVS("ui.hlsl", "vsUIFull", nullptr, 0);
    g_psDown = gfx::loadPS("ui.hlsl", "psUIDown");
    g_psBlur = gfx::loadPS("ui.hlsl", "psUIBlur");
    g_cb.create();
    g_blurCB.create();
    g_photoCB.create();
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
    releaseBlur();
    for (int i = 0; i < kSnapCount; i++) {
        g_snap[i].release();
        g_snapId[i] = -1;
    }
    SAFE_RELEASE(g_blend);
}

void beginFrame(int w, int h) {
    g_w = Max(1, w);
    g_h = Max(1, h);
    g_verts.clear();
    g_batches.clear();
    g_clipMode = 0;
    g_clip = vec4(0);
    g_clipRadius = 0.f;
    g_discard = false;
    g_additive = false;
    g_wantBackdrop = false;
    g_photoUsed = false;
    g_frameIndex++;
}

int screenWidth() { return g_w; }
int screenHeight() { return g_h; }
int vertexCount() { return (int)g_verts.size(); }
int frameIndex() { return g_frameIndex; }
void discardDrawsUntilEndFrame() { g_discard = true; }
void setIconAtlas(ID3D11ShaderResourceView* srv) { g_iconSrv = srv; }
void setAdditive(bool additive) { g_additive = additive; }

void setClipCircle(float cx, float cy, float r) {
    g_clipRadius = 0.f;
    if (r <= 0) { g_clipMode = 0; g_clip = vec4(0); }
    else { g_clipMode = 1; g_clip = vec4(cx, cy, r, 0); }
}
void setClipRect(float x, float y, float w, float h) {
    g_clipRadius = 0.f;
    if (w <= 0) { g_clipMode = 0; g_clip = vec4(0); }
    else { g_clipMode = 2; g_clip = vec4(x, y, w, h); }
}
void setClipRoundRect(float x, float y, float w, float h, float radius) {
    if (w <= 0) { g_clipMode = 0; g_clip = vec4(0); g_clipRadius = 0.f; }
    else { g_clipMode = 3; g_clip = vec4(x, y, w, h); g_clipRadius = Clamp(radius, 0.f, Min(w, h) * 0.5f); }
}
ClipState getClip() {
    ClipState c;
    c.clip = g_clip;
    c.mode = g_clipMode;
    c.radius = g_clipRadius;
    return c;
}
void setClip(const ClipState& c) {
    g_clip = c.clip;
    g_clipMode = c.mode;
    g_clipRadius = c.radius;
}

void rect(float x, float y, float w, float h, u32 c) {
    pushQuad(V(x, y, 0, 0, c, 0, 0), V(x + w, y, 1, 0, c, 0, 0), V(x + w, y + h, 1, 1, c, 0, 0), V(x, y + h, 0, 1, c, 0, 0), anyTex());
}
void gradientRect(float x, float y, float w, float h, u32 top, u32 bottom) {
    pushQuad(V(x, y, 0, 0, top, 0, 0), V(x + w, y, 1, 0, top, 0, 0), V(x + w, y + h, 1, 1, bottom, 0, 0),
             V(x, y + h, 0, 1, bottom, 0, 0), anyTex());
}
void gradientRectH(float x, float y, float w, float h, u32 left, u32 right) {
    pushQuad(V(x, y, 0, 0, left, 0, 0), V(x + w, y, 1, 0, right, 0, 0), V(x + w, y + h, 1, 1, right, 0, 0),
             V(x, y + h, 0, 1, left, 0, 0), anyTex());
}
void gradientRect4(float x, float y, float w, float h, u32 tl, u32 tr, u32 br, u32 bl) {
    pushQuad(V(x, y, 0, 0, tl, 0, 0), V(x + w, y, 1, 0, tr, 0, 0), V(x + w, y + h, 1, 1, br, 0, 0), V(x, y + h, 0, 1, bl, 0, 0),
             anyTex());
}
static void roundRectImpl(float x, float y, float w, float h, float r, u32 ct, u32 cb, float border, u32 bc) {
    float hw = w * 0.5f, hh = h * 0.5f;
    float pad = 1.5f;
    float x0 = x - pad, y0 = y - pad, x1 = x + w + pad, y1 = y + h + pad;
    r = Clamp(r, 0.f, Min(hw, hh));
    float enc = Min(r, 990.f) + floorf(Clamp(border, 0.f, 99.f) * 10.f + 0.5f) * 1000.f;
    pushQuad(V(x0, y0, -hw - pad, -hh - pad, ct, bc, M_RRECT, hw, hh, enc), V(x1, y0, hw + pad, -hh - pad, ct, bc, M_RRECT, hw, hh, enc),
             V(x1, y1, hw + pad, hh + pad, cb, bc, M_RRECT, hw, hh, enc), V(x0, y1, -hw - pad, hh + pad, cb, bc, M_RRECT, hw, hh, enc),
             anyTex());
}
void roundRect(float x, float y, float w, float h, float r, u32 c, float border, u32 bc) { roundRectImpl(x, y, w, h, r, c, c, border, bc); }
void roundRectGradient(float x, float y, float w, float h, float r, u32 top, u32 bottom, float border, u32 bc) {
    roundRectImpl(x, y, w, h, r, top, bottom, border, bc);
}
void roundRectRotated(vec2 c, vec2 ax, float hw, float hh, float r, u32 col) {
    float pad = 1.5f;
    vec2 ay = perp(ax);
    float ex = hw + pad, ey = hh + pad;
    r = Clamp(r, 0.f, Min(hw, hh));
    float enc = Min(r, 990.f);
    vec2 p0 = c - ax * ex - ay * ey, p1 = c + ax * ex - ay * ey, p2 = c + ax * ex + ay * ey, p3 = c - ax * ex + ay * ey;
    pushQuad(V(p0.x, p0.y, -ex, -ey, col, 0, M_RRECT, hw, hh, enc), V(p1.x, p1.y, ex, -ey, col, 0, M_RRECT, hw, hh, enc),
             V(p2.x, p2.y, ex, ey, col, 0, M_RRECT, hw, hh, enc), V(p3.x, p3.y, -ex, ey, col, 0, M_RRECT, hw, hh, enc), anyTex());
}
void circle(float cx, float cy, float r, u32 c, float thickness) {
    float e = r + 1.5f;
    pushQuad(V(cx - e, cy - e, -e, -e, c, 0, M_CIRCLE, r, thickness), V(cx + e, cy - e, e, -e, c, 0, M_CIRCLE, r, thickness),
             V(cx + e, cy + e, e, e, c, 0, M_CIRCLE, r, thickness), V(cx - e, cy + e, -e, e, c, 0, M_CIRCLE, r, thickness), anyTex());
}
void circleSoft(float cx, float cy, float r, float feather, u32 c) {
    float e = r + feather * 0.5f + 1.5f;
    feather = Max(feather, 0.01f);
    pushQuad(V(cx - e, cy - e, -e, -e, c, 0, M_CIRCLE, r, 0, feather), V(cx + e, cy - e, e, -e, c, 0, M_CIRCLE, r, 0, feather),
             V(cx + e, cy + e, e, e, c, 0, M_CIRCLE, r, 0, feather), V(cx - e, cy + e, -e, e, c, 0, M_CIRCLE, r, 0, feather), anyTex());
}
void ringSoft(float cx, float cy, float r, float thickness, float feather, u32 c) {
    float e = r + feather * 0.5f + 1.5f;
    feather = Max(feather, 0.01f);
    thickness = Max(thickness, 0.01f);
    pushQuad(V(cx - e, cy - e, -e, -e, c, 0, M_CIRCLE, r, thickness, feather), V(cx + e, cy - e, e, -e, c, 0, M_CIRCLE, r, thickness, feather),
             V(cx + e, cy + e, e, e, c, 0, M_CIRCLE, r, thickness, feather), V(cx - e, cy + e, -e, e, c, 0, M_CIRCLE, r, thickness, feather),
             anyTex());
}
void line(float x0, float y0, float x1, float y1, float width, u32 c) {
    vec2 d = normalize(vec2(x1 - x0, y1 - y0));
    vec2 n = perp(d) * (width * 0.5f);
    pushQuad(V(x0 + n.x, y0 + n.y, 0, 0, c, 0, 0), V(x1 + n.x, y1 + n.y, 1, 0, c, 0, 0), V(x1 - n.x, y1 - n.y, 1, 1, c, 0, 0),
             V(x0 - n.x, y0 - n.y, 0, 1, c, 0, 0), anyTex());
}
void capsule(float x0, float y0, float x1, float y1, float width, u32 c, float feather) {
    vec2 a(x0, y0), b(x1, y1);
    vec2 d = b - a;
    float L = length(d);
    vec2 dir = L > 1e-4f ? d / L : vec2(1, 0);
    vec2 n = perp(dir);
    float r = width * 0.5f;
    float e = r + feather + 1.f;
    vec2 p0 = a - dir * e + n * e, p1 = b + dir * e + n * e, p2 = b + dir * e - n * e, p3 = a - dir * e - n * e;
    pushQuad(V(p0.x, p0.y, -e, e, c, 0, M_CAPSULE, L, r, feather), V(p1.x, p1.y, L + e, e, c, 0, M_CAPSULE, L, r, feather),
             V(p2.x, p2.y, L + e, -e, c, 0, M_CAPSULE, L, r, feather), V(p3.x, p3.y, -e, -e, c, 0, M_CAPSULE, L, r, feather), anyTex());
}
void polyline(const vec2* pts, int n, float width, u32 c, bool closed, float feather) {
    if (n < 2) return;
    for (int i = 0; i + 1 < n; i++) capsule(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, width, c, feather);
    if (closed && n > 2) capsule(pts[n - 1].x, pts[n - 1].y, pts[0].x, pts[0].y, width, c, feather);
}
void arc(float cx, float cy, float radius, float thickness, float midAngle, float halfAngle, u32 c, float gap) {
    u32 g2 = (u32)(Clamp(gap / 64.f, 0.f, 1.f) * 255.f + 0.5f) << 24;
    halfAngle = Clamp(halfAngle, 0.f, kPi);
    float ro = radius + thickness * 0.5f + 1.5f, ri = Max(0.f, radius - thickness * 0.5f - 1.5f);
    // local frame: arc centered on "up" (-y). Bounding box of the ring segment in that frame.
    float hx = ro * sinf(Min(halfAngle, kHalfPi)) + 1.f;
    float yTop = -ro;
    float yBot = Max(-ri * cosf(halfAngle), -ro * cosf(halfAngle)) + 1.f;
    vec2 ax(cosf(midAngle), sinf(midAngle)), ay = perp(ax);  // screen rotation (clockwise positive in y-down space)
    vec2 ctr(cx, cy);
    auto P = [&](float lx, float ly) { return ctr + ax * lx + ay * ly; };
    vec2 p0 = P(-hx, yTop), p1 = P(hx, yTop), p2 = P(hx, yBot), p3 = P(-hx, yBot);
    float ht = thickness * 0.5f;
    pushQuad(V(p0.x, p0.y, -hx, yTop, c, g2, M_ARC, radius, ht, halfAngle), V(p1.x, p1.y, hx, yTop, c, g2, M_ARC, radius, ht, halfAngle),
             V(p2.x, p2.y, hx, yBot, c, g2, M_ARC, radius, ht, halfAngle), V(p3.x, p3.y, -hx, yBot, c, g2, M_ARC, radius, ht, halfAngle), anyTex());
}
void triangle(vec2 a, vec2 b, vec2 c, u32 color) { aaTri(a, b, c, color, color, color, true, true, true); }
void triangle3(vec2 a, vec2 b, vec2 c, u32 ca, u32 cb, u32 cc) { aaTri(a, b, c, ca, cb, cc, true, true, true); }
void quad(vec2 a, vec2 b, vec2 c, vec2 d, u32 color) {
    aaTri(a, b, c, color, color, color, true, true, false);
    aaTri(a, c, d, color, color, color, false, true, true);
}
void quad4(vec2 a, vec2 b, vec2 c, vec2 d, u32 ca, u32 cb, u32 cc, u32 cd) {
    aaTri(a, b, c, ca, cb, cc, true, true, false);
    aaTri(a, c, d, ca, cc, cd, false, true, true);
}
void polygon(const vec2* pts, int n, u32 color) {
    if (n < 3) return;
    if (n == 3) { triangle(pts[0], pts[1], pts[2], color); return; }
    // Ear clipping on a copy (indices into the original array; boundary edges are consecutive original indices).
    std::vector<int> idx(n);
    float area = 0;
    for (int i = 0; i < n; i++) {
        idx[i] = i;
        area += cross(pts[i], pts[(i + 1) % n]);
    }
    float wind = area >= 0 ? 1.f : -1.f;
    auto isBoundary = [&](int i, int j) { int d = abs(i - j); return d == 1 || d == n - 1; };
    int guard = 0;
    while ((int)idx.size() > 3 && guard++ < n * n) {
        int m = (int)idx.size();
        bool clipped = false;
        for (int k = 0; k < m; k++) {
            int ia = idx[(k + m - 1) % m], ib = idx[k], ic = idx[(k + 1) % m];
            vec2 a = pts[ia], b = pts[ib], c = pts[ic];
            if (cross(b - a, c - b) * wind <= 0) continue;  // reflex
            bool inside = false;
            for (int q = 0; q < m && !inside; q++) {
                int iq = idx[q];
                if (iq == ia || iq == ib || iq == ic) continue;
                inside = pointInTri(pts[iq], a, b, c);
            }
            if (inside) continue;
            aaTri(a, b, c, color, color, color, isBoundary(ia, ib), isBoundary(ib, ic), isBoundary(ic, ia));
            idx.erase(idx.begin() + k);
            clipped = true;
            break;
        }
        if (!clipped) break;  // degenerate input
    }
    if (idx.size() == 3)
        aaTri(pts[idx[0]], pts[idx[1]], pts[idx[2]], color, color, color, isBoundary(idx[0], idx[1]), isBoundary(idx[1], idx[2]),
              isBoundary(idx[2], idx[0]));
}
void image(ID3D11ShaderResourceView* srv, float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 tint) {
    pushQuad(V(x, y, u0, v0, tint, 0, M_IMAGE), V(x + w, y, u1, v0, tint, 0, M_IMAGE), V(x + w, y + h, u1, v1, tint, 0, M_IMAGE),
             V(x, y + h, u0, v1, tint, 0, M_IMAGE), srv);
}
void imageRotated(ID3D11ShaderResourceView* srv, float cx, float cy, float w, float h, float ang, float u0, float v0, float u1,
                  float v1, u32 tint) {
    vec2 ax = vec2(cosf(ang), sinf(ang)), ay = perp(ax);
    vec2 c(cx, cy);
    vec2 p0 = c - ax * (w * 0.5f) - ay * (h * 0.5f), p1 = c + ax * (w * 0.5f) - ay * (h * 0.5f);
    vec2 p2 = c + ax * (w * 0.5f) + ay * (h * 0.5f), p3 = c - ax * (w * 0.5f) + ay * (h * 0.5f);
    pushQuad(V(p0.x, p0.y, u0, v0, tint, 0, M_IMAGE), V(p1.x, p1.y, u1, v0, tint, 0, M_IMAGE), V(p2.x, p2.y, u1, v1, tint, 0, M_IMAGE),
             V(p3.x, p3.y, u0, v1, tint, 0, M_IMAGE), srv);
}
void iconSdf(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color, float pxRange, float outline,
             u32 outlineColor, float soft, float angle) {
    float ol = outline > 0.f ? outline / Max(pxRange, 1e-3f) : 0.f;
    if (angle == 0.f) {
        pushQuad(V(x, y, u0, v0, color, outlineColor, M_ICON, pxRange, ol, soft), V(x + w, y, u1, v0, color, outlineColor, M_ICON, pxRange, ol, soft),
                 V(x + w, y + h, u1, v1, color, outlineColor, M_ICON, pxRange, ol, soft),
                 V(x, y + h, u0, v1, color, outlineColor, M_ICON, pxRange, ol, soft), anyTex());
        return;
    }
    vec2 ax = vec2(cosf(angle), sinf(angle)), ay = perp(ax);
    vec2 c(x + w * 0.5f, y + h * 0.5f);
    vec2 p0 = c - ax * (w * 0.5f) - ay * (h * 0.5f), p1 = c + ax * (w * 0.5f) - ay * (h * 0.5f);
    vec2 p2 = c + ax * (w * 0.5f) + ay * (h * 0.5f), p3 = c - ax * (w * 0.5f) + ay * (h * 0.5f);
    pushQuad(V(p0.x, p0.y, u0, v0, color, outlineColor, M_ICON, pxRange, ol, soft), V(p1.x, p1.y, u1, v0, color, outlineColor, M_ICON, pxRange, ol, soft),
             V(p2.x, p2.y, u1, v1, color, outlineColor, M_ICON, pxRange, ol, soft), V(p3.x, p3.y, u0, v1, color, outlineColor, M_ICON, pxRange, ol, soft),
             anyTex());
}
void mapQuad(ID3D11ShaderResourceView* srv, const vec2 p[4], const vec2 uv[4], u32 landTint, u32 waterTint, float coastLine) {
    pushQuad(V(p[0].x, p[0].y, uv[0].x, uv[0].y, landTint, waterTint, M_MAP, coastLine),
             V(p[1].x, p[1].y, uv[1].x, uv[1].y, landTint, waterTint, M_MAP, coastLine),
             V(p[2].x, p[2].y, uv[2].x, uv[2].y, landTint, waterTint, M_MAP, coastLine),
             V(p[3].x, p[3].y, uv[3].x, uv[3].y, landTint, waterTint, M_MAP, coastLine), srv);
}
void photoEffect(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const PhotoFx& fx) {
    g_wantBackdrop = true;
    g_photoUsed = true;
    g_photo.a = vec4((float)Clamp(fx.filter, 0, 31), Saturate(fx.strength), Clamp(fx.exposure, -4.f, 4.f), Clamp(fx.contrast, 0.2f, 2.f));
    g_photo.b = vec4(Clamp(fx.saturation, 0.f, 2.5f), Clamp(fx.temperature, -1.f, 1.f), Saturate(fx.vignette), Saturate(fx.grain));
    // blur scale: circle of confusion ~ focal length^2 / (f-stop * focus distance); normalized so f/2.8 at 50 mm-ish
    // framing gives a pleasant separation, longer lenses (narrow fov) blur more
    float fovScale = 0.87f / Clamp(fx.fovY, 0.15f, 2.2f);
    float blurScale = fx.dof ? Clamp((2.8f / Clamp(fx.aperture, 0.7f, 32.f)) * fovScale * fovScale * 0.55f, 0.f, 6.f) : 0.f;
    g_photo.c = vec4(fx.dof ? 1.f : 0.f, Max(fx.focusDistance, 0.05f), blurScale, g_depthSrv ? g_depthNear : 0.f);
    g_photo.d = vec4(fx.time, (float)g_w / (float)Max(g_h, 1), 0.f, 0.f);
    u32 c = 0xffffffffu;
    float ra = w / Max(h, 1.f);   // p1, p2: position inside the rect (vignette), p3: rect aspect
    pushQuad(V(x, y, u0, v0, c, 0, M_PHOTO, 0, 0, ra), V(x + w, y, u1, v0, c, 0, M_PHOTO, 1, 0, ra),
             V(x + w, y + h, u1, v1, c, 0, M_PHOTO, 1, 1, ra), V(x, y + h, u0, v1, c, 0, M_PHOTO, 0, 1, ra), anyTex());
}

void setSceneDepth(ID3D11ShaderResourceView* depthSrv, float nearZ) {
    g_depthSrv = depthSrv;
    g_depthNear = depthSrv ? Max(nearZ, 1e-4f) : 0.f;
}

int requestSnapshot() {
    if (g_snapPending >= 0) return g_snapPending;
    g_snapPending = g_snapNextId++;
    return g_snapPending;
}

ID3D11ShaderResourceView* snapshotSrv(int id) {
    if (id < 0) return nullptr;
    int slot = id % kSnapCount;
    return g_snapId[slot] == id ? g_snap[slot].srv : nullptr;
}

void backdrop(float x, float y, float w, float h, float r, u32 tint, u32 overlay, float saturation) {
    g_wantBackdrop = true;
    float hw = w * 0.5f, hh = h * 0.5f;
    float pad = 1.5f;
    float x0 = x - pad, y0 = y - pad, x1 = x + w + pad, y1 = y + h + pad;
    r = Clamp(r, 0.f, Min(Min(hw, hh), 990.f));
    float enc = r + floorf(Saturate(saturation) * 100.f + 0.5f) * 1000.f;
    pushQuad(V(x0, y0, -hw - pad, -hh - pad, tint, overlay, M_BACKDROP, hw, hh, enc),
             V(x1, y0, hw + pad, -hh - pad, tint, overlay, M_BACKDROP, hw, hh, enc),
             V(x1, y1, hw + pad, hh + pad, tint, overlay, M_BACKDROP, hw, hh, enc),
             V(x0, y1, -hw - pad, hh + pad, tint, overlay, M_BACKDROP, hw, hh, enc), anyTex());
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
    float s = st.size;
    // rotation pivot: the anchor at mid line height (before alignment)
    float pivX = x, pivY = y + s * 0.55f;
    bool rotated = st.angle != 0.f;
    float ca = rotated ? cosf(st.angle) : 1.f, sa = rotated ? sinf(st.angle) : 0.f;
    if (st.align == ALIGN_CENTER) x -= w * 0.5f;
    else if (st.align == ALIGN_RIGHT) x -= w;
    // SDF smoothing: px per em unit spread
    float pxRange = f.spreadEm * s * 2.f;
    float outlineSdf = st.outline / Max(pxRange, 1e-3f);
    bool grad = st.colorBottom != 0;
    // glyph corners are laid out unrotated, rotated around the pivot, then offset (shadows keep a screen direction)
    auto place = [&](float px, float py, float ox, float oy) {
        if (!rotated) return vec2(px + ox, py + oy);
        float dx = px - pivX, dy = py - pivY;
        return vec2(pivX + ca * dx - sa * dy + ox, pivY + sa * dx + ca * dy + oy);
    };
    auto emit = [&](float ox, float oy, u32 color, u32 colorB, u32 color2, float outl, float soft) {
        float penX = x, baseline = y + f.ascent * s;
        float top = baseline - f.ascent * s;
        for (const char* p = str; *p; p++) {
            if (*p == '\n') { penX = x; baseline += s * 1.2f; top = baseline - f.ascent * s; continue; }
            unsigned ch = (unsigned char)*p;
            if (ch >= 128 || !f.glyphs[ch].valid) ch = '?';
            const Glyph& g = f.glyphs[ch];
            if (g.u1 > g.u0) {
                float x0 = penX + g.x0 * s, x1 = penX + g.x1 * s, y0 = baseline + g.y0 * s, y1 = baseline + g.y1 * s;
                float k0 = st.skew * (baseline - y0), k1 = st.skew * (baseline - y1);
                u32 ct = color, cb = color;
                if (grad) {
                    float span = Max(f.ascent * s, 1.f);
                    ct = lerpColor(color, colorB, (y0 - top) / span);
                    cb = lerpColor(color, colorB, (y1 - top) / span);
                }
                vec2 q0 = place(x0 + k0, y0, ox, oy), q1 = place(x1 + k0, y0, ox, oy), q2 = place(x1 + k1, y1, ox, oy), q3 = place(x0 + k1, y1, ox, oy);
                pushQuad(V(q0.x, q0.y, g.u0, g.v0, ct, color2, M_FONT, pxRange, outl, soft),
                         V(q1.x, q1.y, g.u1, g.v0, ct, color2, M_FONT, pxRange, outl, soft),
                         V(q2.x, q2.y, g.u1, g.v1, cb, color2, M_FONT, pxRange, outl, soft),
                         V(q3.x, q3.y, g.u0, g.v1, cb, color2, M_FONT, pxRange, outl, soft), g_atlas.srv);
            }
            penX += (g.advance + st.tracking) * s;
        }
    };
    float alphaMul = (st.color >> 24) / 255.f;
    // The soft edge of an outline pass must fade out before the distance field saturates at the quad border:
    // (0.5 - outl) * pr >= 0.5 + margin with pr = pxRange / (1 + soft).
    auto clampOutline = [&](float outl, float soft) {
        float pr = pxRange / (1.f + soft);
        return Min(outl, 0.5f - 0.6f / Max(pr, 1e-3f));
    };
    if (st.glow > 0.f && st.glowColor) {
        float gl = clampOutline(st.glow / Max(pxRange, 1e-3f), 2.5f);
        if (gl > 0.f) emit(0, 0, st.glowColor, st.glowColor, st.glowColor, gl, 2.5f);
    }
    if (st.shadow > 0.f) {
        u32 sc = withAlpha(0xff000000u, (st.shadowSoft > 0.f ? 0.75f : 0.6f) * alphaMul);
        float soft = st.shadowSoft * 2.5f;
        float so = clampOutline(outlineSdf + 0.06f + st.shadowSoft * 0.12f, soft);
        emit(st.shadow, st.shadow, sc, sc, sc, Max(so, 0.f), soft);
    }
    emit(0, 0, st.color, grad ? st.colorBottom : st.color, st.outlineColor, outlineSdf, 0.f);
    return w;
}

float textWrapped(float x, float y, float maxW, const char* str, const TextStyle& st, float lineSpacing) {
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
    return lineY - y;
}

void endFrame() {
    auto* c = gfx::ctx;
    if (g_verts.empty()) {
        if (g_snapPending >= 0) {
            captureSnapshot(g_snapPending);
            g_snapPending = -1;
        }
        return;
    }
    if (g_wantBackdrop) captureBackdrop();
    // The depth buffer sampled for photo DOF must not stay bound as the depth target.
    ID3D11RenderTargetView* savedRtv = nullptr;
    ID3D11DepthStencilView* savedDsv = nullptr;
    bool photoDepth = g_photoUsed && g_depthSrv;
    if (photoDepth) {
        c->OMGetRenderTargets(1, &savedRtv, &savedDsv);
        c->OMSetRenderTargets(1, &savedRtv, nullptr);
    }
    if (g_photoUsed) {
        g_photoCB.data = g_photo;
        g_photoCB.upload();
        ID3D11Buffer* pcb[] = {g_photoCB.get()};
        c->PSSetConstantBuffers(3, 1, pcb);
    }
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
    ID3D11ShaderResourceView* blurSrv = (g_blurValid && g_q1.srv) ? g_q1.srv : g_atlas.srv;
    ID3D11ShaderResourceView* sceneSrv = (g_blurValid && g_copySrv) ? g_copySrv : g_atlas.srv;
    ID3D11ShaderResourceView* halfSrv = (g_blurValid && g_half.srv) ? g_half.srv : g_atlas.srv;
    ID3D11ShaderResourceView* depthSrv = photoDepth ? g_depthSrv : g_atlas.srv;
    for (auto& b : g_batches) {
        if (b.count == 0) continue;
        g_cb.data.screen = vec4((float)g_w, (float)g_h, 1.f / g_w, 1.f / g_h);
        g_cb.data.clip = b.clip;
        g_cb.data.clipMode = vec4((float)b.clipMode, b.clipRadius, 0, 0);
        g_cb.upload();
        ID3D11Buffer* cbs[] = {g_cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        ID3D11ShaderResourceView* srvs[7] = {g_atlas.srv, b.tex ? b.tex : g_atlas.srv, g_iconSrv ? g_iconSrv : g_atlas.srv, blurSrv,
                                             sceneSrv, depthSrv, halfSrv};
        c->PSSetShaderResources(0, 7, srvs);
        c->Draw((UINT)b.count, (UINT)b.start);
    }
    ID3D11ShaderResourceView* nulls[7] = {};
    c->PSSetShaderResources(0, 7, nulls);
    if (photoDepth) {
        c->OMSetRenderTargets(1, &savedRtv, savedDsv);
    }
    SAFE_RELEASE(savedRtv);
    SAFE_RELEASE(savedDsv);
    if (g_snapPending >= 0) {
        captureSnapshot(g_snapPending);
        g_snapPending = -1;
    }
}

}  // namespace UI
