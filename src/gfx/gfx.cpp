#include "gfx.h"
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <d3d11_1.h>
#include "../core/rng.h"
#include "../platform/platform.h"
#include "shaders_embedded.h"

namespace gfx {

ID3D11Device* dev = nullptr;
ID3D11DeviceContext* ctx = nullptr;
States states;

namespace {
IDXGISwapChain1* g_swap = nullptr;
IDXGIFactory2* g_factory = nullptr;
ID3D11Texture2D* g_bbTex = nullptr;
ID3D11RenderTargetView* g_bbRtv = nullptr;
int g_bbW = 0, g_bbH = 0;
bool g_allowTearing = false;
std::string g_adapterName;
size_t g_vramMB = 0;
ID3DUserDefinedAnnotation* g_annot = nullptr;

void createBackbufferViews() {
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&g_bbTex);
    D3D11_RENDER_TARGET_VIEW_DESC rd = {};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(g_bbTex, &rd, &g_bbRtv);
}

void createStates() {
    D3D11_RASTERIZER_DESC rs = {};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_BACK;
    rs.FrontCounterClockwise = TRUE;  // CCW front faces (right-handed meshes)
    rs.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rs, &states.cullBack);
    rs.CullMode = D3D11_CULL_FRONT;
    dev->CreateRasterizerState(&rs, &states.cullFront);
    rs.CullMode = D3D11_CULL_NONE;
    dev->CreateRasterizerState(&rs, &states.cullNone);
    rs.ScissorEnable = TRUE;
    dev->CreateRasterizerState(&rs, &states.cullNoneScissor);
    rs.ScissorEnable = FALSE;
    rs.FillMode = D3D11_FILL_WIREFRAME;
    dev->CreateRasterizerState(&rs, &states.wireframe);
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthBias = 0;
    rs.SlopeScaledDepthBias = 1.5f;
    rs.DepthBiasClamp = 0.02f;
    rs.DepthClipEnable = FALSE;  // pancaking for shadow casters behind the near plane
    dev->CreateRasterizerState(&rs, &states.shadowBias);

    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_GREATER;
    dev->CreateDepthStencilState(&ds, &states.depthGreaterWrite);
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    dev->CreateDepthStencilState(&ds, &states.depthGreaterEqualNoWrite);
    ds.DepthFunc = D3D11_COMPARISON_EQUAL;
    dev->CreateDepthStencilState(&ds, &states.depthEqualNoWrite);
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateDepthStencilState(&ds, &states.depthLessWrite);
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dev->CreateDepthStencilState(&ds, &states.depthOff);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    for (int i = 1; i < 8; i++) bd.RenderTarget[i] = bd.RenderTarget[0];
    dev->CreateBlendState(&bd, &states.opaque);
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    dev->CreateBlendState(&bd, &states.alpha);
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    dev->CreateBlendState(&bd, &states.premultiplied);
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    dev->CreateBlendState(&bd, &states.additive);
    D3D11_BLEND_DESC nb = {};
    nb.RenderTarget[0].RenderTargetWriteMask = 0;
    dev->CreateBlendState(&nb, &states.noColorWrite);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.MaxAnisotropy = 1;
    dev->CreateSamplerState(&sd, &states.pointClamp);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    dev->CreateSamplerState(&sd, &states.pointWrap);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&sd, &states.linearClamp);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    dev->CreateSamplerState(&sd, &states.linearWrap);
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 8;
    dev->CreateSamplerState(&sd, &states.anisoWrap);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&sd, &states.anisoClamp);
    sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sd.MaxAnisotropy = 1;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.f;
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    dev->CreateSamplerState(&sd, &states.shadowCmp);
}
}  // namespace

bool init(void* hwnd, int width, int height, bool debugLayer) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (debugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got;

    IDXGIFactory1* f1 = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f1))) return false;
    f1->QueryInterface(__uuidof(IDXGIFactory2), (void**)&g_factory);
    // Pick the adapter with the most dedicated video memory (discrete GPU on laptops).
    IDXGIAdapter1* best = nullptr;
    SIZE_T bestMem = 0;
    for (UINT i = 0;; i++) {
        IDXGIAdapter1* a = nullptr;
        if (f1->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d;
        a->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (d.DedicatedVideoMemory > bestMem || !best)) {
            SAFE_RELEASE(best);
            best = a;
            bestMem = d.DedicatedVideoMemory;
        } else a->Release();
    }
    HRESULT hr = D3D11CreateDevice(best, best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls, 2,
                                   D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr) && debugLayer) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(best, best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls, 2,
                               D3D11_SDK_VERSION, &dev, &got, &ctx);
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fls, 2, D3D11_SDK_VERSION, &dev, &got, &ctx);
    }
    if (FAILED(hr)) {
        LOG("D3D11CreateDevice failed: %08lx", (unsigned long)hr);
        SAFE_RELEASE(best);
        SAFE_RELEASE(f1);
        return false;
    }
    if (best) {
        DXGI_ADAPTER_DESC1 d;
        best->GetDesc1(&d);
        char name[256];
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), nullptr, nullptr);
        g_adapterName = name;
        g_vramMB = d.DedicatedVideoMemory / (1024 * 1024);
    }
    LOG("D3D11 device created: feature level %x, adapter '%s', VRAM %zu MB", (unsigned)got, g_adapterName.c_str(), g_vramMB);
    SAFE_RELEASE(best);

    IDXGIFactory5* f5 = nullptr;
    if (SUCCEEDED(f1->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) g_allowTearing = allow != 0;
        f5->Release();
    }
    SAFE_RELEASE(f1);
    if (!g_factory) return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = (UINT)width;
    sd.Height = (UINT)height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 3;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.Flags = g_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    hr = g_factory->CreateSwapChainForHwnd(dev, (HWND)hwnd, &sd, nullptr, nullptr, &g_swap);
    if (FAILED(hr)) {
        // Older systems: fall back to blt model
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.BufferCount = 1;
        sd.Flags = 0;
        g_allowTearing = false;
        hr = g_factory->CreateSwapChainForHwnd(dev, (HWND)hwnd, &sd, nullptr, nullptr, &g_swap);
        if (FAILED(hr)) {
            LOG("CreateSwapChainForHwnd failed: %08lx", (unsigned long)hr);
            return false;
        }
    }
    g_factory->MakeWindowAssociation((HWND)hwnd, DXGI_MWA_NO_ALT_ENTER);
    g_bbW = width;
    g_bbH = height;
    createBackbufferViews();
    createStates();
    ctx->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void**)&g_annot);
    return shaderCompilerInit();
}

void shutdown() {
    if (ctx) ctx->ClearState();
    SAFE_RELEASE(g_annot);
    SAFE_RELEASE(g_bbRtv);
    SAFE_RELEASE(g_bbTex);
    SAFE_RELEASE(g_swap);
    SAFE_RELEASE(g_factory);
    SAFE_RELEASE(ctx);
    SAFE_RELEASE(dev);
}

void resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == g_bbW && h == g_bbH)) return;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    SAFE_RELEASE(g_bbRtv);
    SAFE_RELEASE(g_bbTex);
    HRESULT hr = g_swap->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, g_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) LOG("ResizeBuffers failed %08lx", (unsigned long)hr);
    g_bbW = w;
    g_bbH = h;
    createBackbufferViews();
}

void present(bool vsync) {
    UINT flags = (!vsync && g_allowTearing && !Platform::isFullscreen()) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    if (!vsync && g_allowTearing) flags = DXGI_PRESENT_ALLOW_TEARING;
    HRESULT hr = g_swap->Present(vsync ? 1 : 0, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HRESULT reason = dev->GetDeviceRemovedReason();
        FatalError("The graphics device was removed (reason %08lx). Please update your GPU driver.", (unsigned long)reason);
    }
}

ID3D11RenderTargetView* backbufferRTV() { return g_bbRtv; }
ID3D11Texture2D* backbufferTex() { return g_bbTex; }
int backbufferWidth() { return g_bbW; }
int backbufferHeight() { return g_bbH; }
std::string adapterName() { return g_adapterName; }
size_t adapterVideoMemoryMB() { return g_vramMB; }

// ---------------------------------------------------------------------------
void Texture::release() {
    for (auto* v : mipUavs) SAFE_RELEASE(v);
    for (auto* v : mipSrvs) SAFE_RELEASE(v);
    for (auto* v : sliceRtvs) SAFE_RELEASE(v);
    for (auto* v : sliceDsvs) SAFE_RELEASE(v);
    mipUavs.clear();
    mipSrvs.clear();
    sliceRtvs.clear();
    sliceDsvs.clear();
    SAFE_RELEASE(srv);
    SAFE_RELEASE(uav);
    SAFE_RELEASE(rtv);
    SAFE_RELEASE(dsv);
    SAFE_RELEASE(res);
}

int mipCount(int w, int h) {
    int m = 1;
    while (w > 1 || h > 1) { w = Max(1, w / 2); h = Max(1, h / 2); m++; }
    return m;
}

static void depthFormats(DXGI_FORMAT fmt, DXGI_FORMAT& dsvFmt, DXGI_FORMAT& srvFmt) {
    switch (fmt) {
        case DXGI_FORMAT_R32_TYPELESS: dsvFmt = DXGI_FORMAT_D32_FLOAT; srvFmt = DXGI_FORMAT_R32_FLOAT; break;
        case DXGI_FORMAT_R24G8_TYPELESS: dsvFmt = DXGI_FORMAT_D24_UNORM_S8_UINT; srvFmt = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS: dsvFmt = DXGI_FORMAT_D16_UNORM; srvFmt = DXGI_FORMAT_R16_UNORM; break;
        case DXGI_FORMAT_R32G8X24_TYPELESS: dsvFmt = DXGI_FORMAT_D32_FLOAT_S8X24_UINT; srvFmt = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        default: dsvFmt = fmt; srvFmt = fmt; break;
    }
}

Texture createTexture2D(int w, int h, DXGI_FORMAT fmt, u32 flags, int mips, int layers, const void* initData, int initPitch) {
    Texture t;
    if (mips <= 0) mips = mipCount(w, h);
    t.width = w; t.height = h; t.mips = mips; t.layers = layers; t.format = fmt;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = (UINT)w;
    d.Height = (UINT)h;
    d.MipLevels = (UINT)mips;
    d.ArraySize = (UINT)layers;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Usage = (flags & TEX_DYNAMIC) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    if (flags & TEX_DYNAMIC) d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (flags & (TEX_SRV)) d.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (flags & TEX_RTV) d.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (flags & TEX_UAV) d.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if (flags & TEX_DSV) d.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (flags & TEX_CUBE) d.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (flags & TEX_GENMIPS) { d.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS; d.BindFlags |= D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; }
    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = initData;
    sub.SysMemPitch = (UINT)initPitch;
    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = dev->CreateTexture2D(&d, (initData && mips == 1 && layers == 1) ? &sub : nullptr, &tex);
    if (FAILED(hr)) FatalError("CreateTexture2D %dx%d fmt %d failed (%08lx)", w, h, (int)fmt, (unsigned long)hr);
    t.res = tex;
    if (initData && !(mips == 1 && layers == 1)) ctx->UpdateSubresource(tex, 0, nullptr, initData, (UINT)initPitch, 0);

    DXGI_FORMAT dsvFmt, srvFmt;
    depthFormats(fmt, dsvFmt, srvFmt);
    bool cube = (flags & TEX_CUBE) != 0;
    if (flags & (TEX_SRV | TEX_GENMIPS)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = srvFmt;
        if (cube) {
            if (layers > 6) { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; sv.TextureCubeArray.MipLevels = (UINT)mips; sv.TextureCubeArray.NumCubes = (UINT)layers / 6; }
            else { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; sv.TextureCube.MipLevels = (UINT)mips; }
        } else if (layers > 1) {
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            sv.Texture2DArray.MipLevels = (UINT)mips;
            sv.Texture2DArray.ArraySize = (UINT)layers;
        } else {
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = (UINT)mips;
        }
        dev->CreateShaderResourceView(tex, &sv, &t.srv);
    }
    if (flags & TEX_RTV) {
        D3D11_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = fmt;
        if (layers > 1) {
            rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rv.Texture2DArray.ArraySize = (UINT)layers;
        } else rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        dev->CreateRenderTargetView(tex, &rv, &t.rtv);
        if (flags & TEX_SLICE_RTVS) {
            for (int i = 0; i < layers; i++) {
                D3D11_RENDER_TARGET_VIEW_DESC r1 = {};
                r1.Format = fmt;
                r1.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                r1.Texture2DArray.FirstArraySlice = (UINT)i;
                r1.Texture2DArray.ArraySize = 1;
                ID3D11RenderTargetView* v = nullptr;
                dev->CreateRenderTargetView(tex, &r1, &v);
                t.sliceRtvs.push_back(v);
            }
        }
    }
    if (flags & TEX_DSV) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv = {};
        dv.Format = dsvFmt;
        if (layers > 1) {
            dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            dv.Texture2DArray.ArraySize = (UINT)layers;
        } else dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev->CreateDepthStencilView(tex, &dv, &t.dsv);
        if (flags & TEX_SLICE_RTVS) {
            for (int i = 0; i < layers; i++) {
                D3D11_DEPTH_STENCIL_VIEW_DESC d1 = {};
                d1.Format = dsvFmt;
                d1.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                d1.Texture2DArray.FirstArraySlice = (UINT)i;
                d1.Texture2DArray.ArraySize = 1;
                ID3D11DepthStencilView* v = nullptr;
                dev->CreateDepthStencilView(tex, &d1, &v);
                t.sliceDsvs.push_back(v);
            }
        }
    }
    if (flags & TEX_UAV) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = fmt;
        if (layers > 1) {
            uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uv.Texture2DArray.ArraySize = (UINT)layers;
        } else uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(tex, &uv, &t.uav);
        if (flags & TEX_MIP_UAVS) {
            for (int m = 0; m < mips; m++) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC u1 = uv;
                D3D11_SHADER_RESOURCE_VIEW_DESC s1 = {};
                s1.Format = srvFmt;
                if (layers > 1) {
                    u1.Texture2DArray.MipSlice = (UINT)m;
                    s1.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                    s1.Texture2DArray.MostDetailedMip = (UINT)m;
                    s1.Texture2DArray.MipLevels = 1;
                    s1.Texture2DArray.ArraySize = (UINT)layers;
                } else {
                    u1.Texture2D.MipSlice = (UINT)m;
                    s1.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    s1.Texture2D.MostDetailedMip = (UINT)m;
                    s1.Texture2D.MipLevels = 1;
                }
                ID3D11UnorderedAccessView* v = nullptr;
                dev->CreateUnorderedAccessView(tex, &u1, &v);
                t.mipUavs.push_back(v);
                ID3D11ShaderResourceView* s = nullptr;
                if (flags & TEX_SRV) dev->CreateShaderResourceView(tex, &s1, &s);
                t.mipSrvs.push_back(s);
            }
        }
    }
    return t;
}

Texture createTexture3D(int w, int h, int dd, DXGI_FORMAT fmt, u32 flags, int mips) {
    Texture t;
    t.width = w; t.height = h; t.depth = dd; t.mips = mips; t.format = fmt; t.is3D = true;
    D3D11_TEXTURE3D_DESC d = {};
    d.Width = (UINT)w;
    d.Height = (UINT)h;
    d.Depth = (UINT)dd;
    d.MipLevels = (UINT)mips;
    d.Format = fmt;
    d.Usage = D3D11_USAGE_DEFAULT;
    if (flags & TEX_SRV) d.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (flags & TEX_UAV) d.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if (flags & TEX_RTV) d.BindFlags |= D3D11_BIND_RENDER_TARGET;
    ID3D11Texture3D* tex = nullptr;
    HRESULT hr = dev->CreateTexture3D(&d, nullptr, &tex);
    if (FAILED(hr)) FatalError("CreateTexture3D failed (%08lx)", (unsigned long)hr);
    t.res = tex;
    if (flags & TEX_SRV) dev->CreateShaderResourceView(tex, nullptr, &t.srv);
    if (flags & TEX_UAV) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = fmt;
        uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uv.Texture3D.WSize = (UINT)dd;
        dev->CreateUnorderedAccessView(tex, &uv, &t.uav);
        if (flags & TEX_MIP_UAVS) {
            for (int m = 0; m < mips; m++) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC u1 = uv;
                u1.Texture3D.MipSlice = (UINT)m;
                u1.Texture3D.WSize = (UINT)Max(1, dd >> m);
                ID3D11UnorderedAccessView* v = nullptr;
                dev->CreateUnorderedAccessView(tex, &u1, &v);
                t.mipUavs.push_back(v);
                D3D11_SHADER_RESOURCE_VIEW_DESC s1 = {};
                s1.Format = fmt;
                s1.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                s1.Texture3D.MostDetailedMip = (UINT)m;
                s1.Texture3D.MipLevels = 1;
                ID3D11ShaderResourceView* s = nullptr;
                dev->CreateShaderResourceView(tex, &s1, &s);
                t.mipSrvs.push_back(s);
            }
        }
    }
    return t;
}

void uploadTexture2D(Texture& t, int mip, int layer, const void* data, int rowPitch) {
    UINT sub = D3D11CalcSubresource((UINT)mip, (UINT)layer, (UINT)t.mips);
    ctx->UpdateSubresource(t.res, sub, nullptr, data, (UINT)rowPitch, 0);
}

void Buffer::release() {
    SAFE_RELEASE(srv);
    SAFE_RELEASE(uav);
    SAFE_RELEASE(buf);
    size = 0;
}

Buffer createBuffer(u32 size, u32 stride, u32 flags, const void* init) {
    Buffer b;
    b.size = size;
    b.stride = stride;
    b.flags = flags;
    if (size == 0) return b;
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth = size;
    d.Usage = D3D11_USAGE_DEFAULT;
    if (flags & BUF_DYNAMIC) { d.Usage = D3D11_USAGE_DYNAMIC; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; }
    if (flags & BUF_STAGING) { d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; }
    if (flags & BUF_VERTEX) d.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (flags & BUF_INDEX) d.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (flags & BUF_CONSTANT) d.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    if (flags & (BUF_SRV | BUF_STRUCTURED | BUF_RAW)) {
        if (!(flags & BUF_STAGING)) d.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (flags & BUF_UAV) d.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if (flags & BUF_STRUCTURED) { d.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = stride; }
    if (flags & BUF_RAW) d.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (flags & BUF_INDIRECT) d.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    if (flags & BUF_STAGING) d.BindFlags = 0;
    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = init;
    HRESULT hr = dev->CreateBuffer(&d, init ? &sub : nullptr, &b.buf);
    if (FAILED(hr)) FatalError("CreateBuffer size %u failed (%08lx)", size, (unsigned long)hr);
    if ((flags & (BUF_SRV | BUF_STRUCTURED | BUF_RAW)) && !(flags & BUF_STAGING)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        if (flags & BUF_RAW) {
            sv.Format = DXGI_FORMAT_R32_TYPELESS;
            sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            sv.BufferEx.NumElements = size / 4;
            sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        } else if (flags & BUF_STRUCTURED) {
            sv.Format = DXGI_FORMAT_UNKNOWN;
            sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sv.Buffer.NumElements = size / stride;
        } else {
            sv.Format = DXGI_FORMAT_R32_UINT;
            sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sv.Buffer.NumElements = size / 4;
        }
        dev->CreateShaderResourceView(b.buf, &sv, &b.srv);
    }
    if (flags & BUF_UAV) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        if (flags & BUF_RAW) {
            uv.Format = DXGI_FORMAT_R32_TYPELESS;
            uv.Buffer.NumElements = size / 4;
            uv.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        } else if (flags & BUF_STRUCTURED) {
            uv.Format = DXGI_FORMAT_UNKNOWN;
            uv.Buffer.NumElements = size / stride;
            if (flags & BUF_APPEND) uv.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
        } else {
            uv.Format = DXGI_FORMAT_R32_UINT;
            uv.Buffer.NumElements = size / 4;
        }
        dev->CreateUnorderedAccessView(b.buf, &uv, &b.uav);
    }
    return b;
}

void updateBuffer(Buffer& b, const void* data, u32 size) {
    if (!b.buf || size == 0) return;
    if (size > b.size) size = b.size;
    if (b.flags & BUF_DYNAMIC) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(b.buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, data, size);
            ctx->Unmap(b.buf, 0);
        }
    } else {
        D3D11_BOX box = {0, 0, 0, size, 1, 1};
        ctx->UpdateSubresource(b.buf, 0, (b.flags & BUF_CONSTANT) ? nullptr : &box, data, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Shader compilation
namespace {
typedef HRESULT(WINAPI* PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
                                         UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI* PFN_D3DCreateBlob)(SIZE_T, ID3DBlob**);
PFN_D3DCompile pD3DCompile = nullptr;
PFN_D3DCreateBlob pD3DCreateBlob = nullptr;
u64 g_bundleHash = 0;
std::atomic<int> g_compileCount{0};
std::atomic<long long> g_compileMicros{0};
std::string g_cacheDir;

const EmbeddedFile* findEmbedded(const char* name) {
    for (int i = 0; i < g_embeddedShaderCount; i++)
        if (strcmp(g_embeddedShaders[i].name, name) == 0) return &g_embeddedShaders[i];
    return nullptr;
}

struct EmbeddedInclude : public ID3DInclude {
    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override {
        const char* name = fileName;
        // strip any leading path
        const char* s = strrchr(name, '/');
        if (s) name = s + 1;
        s = strrchr(name, '\\');
        if (s) name = s + 1;
        const EmbeddedFile* f = findEmbedded(name);
        if (!f) { LOG("Shader include not found: %s", fileName); return E_FAIL; }
        *ppData = f->data;
        *pBytes = f->size;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }
};

bool readFile(const std::string& path, std::vector<u8>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

ID3DBlob* compileBlob(const char* file, const char* entry, const char* target, const std::vector<ShaderDefine>& defines) {
    const EmbeddedFile* f = findEmbedded(file);
    if (!f) FatalError("Shader file not embedded: %s", file);
    std::string key = std::string(file) + "|" + entry + "|" + target;
    for (auto& d : defines) key += std::string("|") + d.name + "=" + (d.value ? d.value : "");
    u64 h = hash64(key.data(), key.size()) ^ (g_bundleHash * 0x9E3779B97F4A7C15ULL);
    char hname[64];
    snprintf(hname, sizeof(hname), "%016llx.cso", (unsigned long long)h);
    std::string cachePath = g_cacheDir + hname;
    std::vector<u8> cached;
    if (!Platform::hasArg("nocache") && readFile(cachePath, cached) && pD3DCreateBlob) {
        ID3DBlob* b = nullptr;
        if (SUCCEEDED(pD3DCreateBlob(cached.size(), &b))) {
            memcpy(b->GetBufferPointer(), cached.data(), cached.size());
            return b;
        }
    }
    std::vector<D3D_SHADER_MACRO> macros;
    for (auto& d : defines) macros.push_back({d.name, d.value ? d.value : "1"});
    macros.push_back({nullptr, nullptr});
    EmbeddedInclude inc;
    ID3DBlob *code = nullptr, *err = nullptr;
    double t0 = Platform::timeSeconds();
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
    HRESULT hr = pD3DCompile(f->data, f->size, file, macros.data(), &inc, entry, target, flags, 0, &code, &err);
    g_compileMicros += (long long)((Platform::timeSeconds() - t0) * 1e6);
    g_compileCount++;
    if (FAILED(hr)) {
        const char* msg = err ? (const char*)err->GetBufferPointer() : "(no message)";
        LOG("Shader compile error %s:%s (%s):\n%s", file, entry, target, msg);
        SAFE_RELEASE(err);
        FatalError("Failed to compile shader %s:%s. See log.txt for details.", file, entry);
    }
    if (err) {
        const char* msg = (const char*)err->GetBufferPointer();
        if (msg && strstr(msg, "warning") && Platform::hasArg("shaderwarnings")) LOG("Shader warnings %s:%s:\n%s", file, entry, msg);
        SAFE_RELEASE(err);
    }
    FILE* out = fopen(cachePath.c_str(), "wb");
    if (out) {
        fwrite(code->GetBufferPointer(), 1, code->GetBufferSize(), out);
        fclose(out);
    }
    return code;
}
}  // namespace

bool shaderCompilerInit() {
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    if (!m) {
        LOG("d3dcompiler_47.dll not found");
        return false;
    }
    pD3DCompile = (PFN_D3DCompile)(void*)GetProcAddress(m, "D3DCompile");
    pD3DCreateBlob = (PFN_D3DCreateBlob)(void*)GetProcAddress(m, "D3DCreateBlob");
    if (!pD3DCompile) return false;
    u64 h = 1469598103934665603ULL;
    for (int i = 0; i < g_embeddedShaderCount; i++) {
        h ^= hash64(g_embeddedShaders[i].data, g_embeddedShaders[i].size);
        h *= 1099511628211ULL;
    }
    g_bundleHash = h;
    g_cacheDir = Platform::userDataDir() + "shadercache\\";
    CreateDirectoryA(g_cacheDir.c_str(), nullptr);
    return true;
}

VertexShader loadVS(const char* file, const char* entry, const D3D11_INPUT_ELEMENT_DESC* layout, int layoutCount,
                    const std::vector<ShaderDefine>& defines) {
    VertexShader s;
    ID3DBlob* b = compileBlob(file, entry, "vs_5_0", defines);
    HRESULT hr = dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s.vs);
    if (FAILED(hr)) FatalError("CreateVertexShader %s:%s failed", file, entry);
    if (layout && layoutCount > 0) {
        hr = dev->CreateInputLayout(layout, (UINT)layoutCount, b->GetBufferPointer(), b->GetBufferSize(), &s.layout);
        if (FAILED(hr)) FatalError("CreateInputLayout %s:%s failed", file, entry);
    }
    b->Release();
    return s;
}
ID3D11PixelShader* loadPS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines) {
    ID3DBlob* b = compileBlob(file, entry, "ps_5_0", defines);
    ID3D11PixelShader* s = nullptr;
    if (FAILED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)))
        FatalError("CreatePixelShader %s:%s failed", file, entry);
    b->Release();
    return s;
}
ID3D11ComputeShader* loadCS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines) {
    ID3DBlob* b = compileBlob(file, entry, "cs_5_0", defines);
    ID3D11ComputeShader* s = nullptr;
    if (FAILED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)))
        FatalError("CreateComputeShader %s:%s failed", file, entry);
    b->Release();
    return s;
}
ID3D11GeometryShader* loadGS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines) {
    ID3DBlob* b = compileBlob(file, entry, "gs_5_0", defines);
    ID3D11GeometryShader* s = nullptr;
    if (FAILED(dev->CreateGeometryShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)))
        FatalError("CreateGeometryShader %s:%s failed", file, entry);
    b->Release();
    return s;
}
int shaderCompileCount() { return g_compileCount.load(); }
double shaderCompileSeconds() { return g_compileMicros.load() * 1e-6; }

// ---------------------------------------------------------------------------
void setViewport(float w, float h, float x, float y) {
    D3D11_VIEWPORT vp = {x, y, w, h, 0.f, 1.f};
    ctx->RSSetViewports(1, &vp);
}

void clearBindings() {
    ID3D11ShaderResourceView* nullSrv[16] = {};
    ID3D11UnorderedAccessView* nullUav[8] = {};
    ctx->VSSetShaderResources(0, 16, nullSrv);
    ctx->PSSetShaderResources(0, 16, nullSrv);
    ctx->CSSetShaderResources(0, 16, nullSrv);
    ctx->CSSetUnorderedAccessViews(0, 8, nullUav, nullptr);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void unbindCSResources(int srvCount, int uavCount) {
    ID3D11ShaderResourceView* nullSrv[32] = {};
    ID3D11UnorderedAccessView* nullUav[8] = {};
    ctx->CSSetShaderResources(0, (UINT)Min(srvCount, 32), nullSrv);
    ctx->CSSetUnorderedAccessViews(0, (UINT)Min(uavCount, 8), nullUav, nullptr);
}

// ---------------------------------------------------------------------------
// GPU timers
namespace {
struct GpuTimer {
    std::string name;
    ID3D11Query* begin[3] = {};
    ID3D11Query* end[3] = {};
    double ms = 0;
};
ID3D11Query* g_disjoint[3] = {};
std::vector<GpuTimer> g_timers;
std::vector<int> g_timerStack;
int g_timerFrame = 0;
int g_timerIndex = 0;
bool g_timersEnabled = false;
bool g_disjointOpen = false;
}  // namespace

void gpuTimerBegin(const char* name) {
    if (!Platform::hasArg("gputimers")) return;
    g_timersEnabled = true;
    int slot = g_timerFrame % 3;
    if (!g_disjoint[slot]) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        dev->CreateQuery(&qd, &g_disjoint[slot]);
    }
    if (!g_disjointOpen) { ctx->Begin(g_disjoint[slot]); g_disjointOpen = true; }
    if (g_timerIndex >= (int)g_timers.size()) g_timers.push_back(GpuTimer());
    GpuTimer& t = g_timers[g_timerIndex];
    t.name = name;
    if (!t.begin[slot]) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_TIMESTAMP, 0};
        dev->CreateQuery(&qd, &t.begin[slot]);
        dev->CreateQuery(&qd, &t.end[slot]);
    }
    ctx->End(t.begin[slot]);
    g_timerStack.push_back(g_timerIndex);
    g_timerIndex++;
}
void gpuTimerEnd() {
    if (!g_timersEnabled || g_timerStack.empty()) return;
    int slot = g_timerFrame % 3;
    GpuTimer& t = g_timers[g_timerStack.back()];
    g_timerStack.pop_back();
    ctx->End(t.end[slot]);
}
void gpuTimersResolve() {
    if (!g_timersEnabled) return;
    int slot = g_timerFrame % 3;
    if (g_disjointOpen) { ctx->End(g_disjoint[slot]); g_disjointOpen = false; }
    int readSlot = (g_timerFrame + 1) % 3;  // two frames old
    if (g_timerFrame >= 2 && g_disjoint[readSlot]) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        if (ctx->GetData(g_disjoint[readSlot], &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint) {
            for (auto& t : g_timers) {
                UINT64 a = 0, b = 0;
                if (t.begin[readSlot] && ctx->GetData(t.begin[readSlot], &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                    ctx->GetData(t.end[readSlot], &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
                    t.ms = t.ms * 0.9 + 0.1 * (double)(b - a) / (double)dj.Frequency * 1000.0;
            }
        }
    }
    g_timerFrame++;
    g_timerIndex = 0;
}
std::string gpuTimerReport() {
    std::string s;
    for (auto& t : g_timers) s += StrFormat("%-18s %6.2f ms\n", t.name.c_str(), t.ms);
    return s;
}

bool saveScreenshotBMP(const char* path) {
    D3D11_TEXTURE2D_DESC td;
    g_bbTex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &st))) return false;
    ctx->CopyResource(st, g_bbTex);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) { st->Release(); return false; }
    int w = (int)td.Width, h = (int)td.Height;
    int rowBytes = w * 3, pad = (4 - rowBytes % 4) % 4;
    FILE* f = fopen(path, "wb");
    if (!f) { ctx->Unmap(st, 0); st->Release(); return false; }
    u32 dataSize = (u32)((rowBytes + pad) * h);
    u8 hdr[54] = {'B', 'M'};
    u32 fileSize = 54 + dataSize;
    memcpy(hdr + 2, &fileSize, 4);
    u32 off = 54, hsz = 40, planes = 1, bpp = 24;
    memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &hsz, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &dataSize, 4);
    fwrite(hdr, 1, 54, f);
    std::vector<u8> row((size_t)(rowBytes + pad), 0);
    for (int y = h - 1; y >= 0; y--) {
        const u8* src = (const u8*)ms.pData + (size_t)y * ms.RowPitch;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = src[x * 4 + 2];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    ctx->Unmap(st, 0);
    st->Release();
    return true;
}

void beginEvent(const char* name) {
    if (!g_annot) return;
    wchar_t w[128];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 128);
    g_annot->BeginEvent(w);
}
void endEvent() {
    if (g_annot) g_annot->EndEvent();
}

}  // namespace gfx
