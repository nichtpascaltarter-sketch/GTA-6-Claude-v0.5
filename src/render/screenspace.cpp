// Shared screen-space inputs: hierarchical depth (HiZ), half-resolution depth/normals and the previous frame's
// scene color pyramid. Consumed by AO/GI (ssao.cpp), reflections (ssr.cpp), clouds and water.
// Included from renderer.cpp.
namespace Render {

struct HiZCBData {
    u32 srcW, srcH, dstW, dstH;
    vec4 params;
};

struct ScreenSpaceSystem {
    gfx::Texture hiz;            // R32G32F at half resolution with a full mip chain: x closest, y farthest depth
    gfx::Texture halfDepth[2];   // R32F linear view depth at half resolution (current / previous frame)
    gfx::Texture halfNormal;     // R16G16 octahedral world normal at half resolution
    gfx::Texture colorPyramid;   // RGBA16F previous-frame HDR (pre-exposed) at half resolution with mips
    int cur = 0;
    int halfW = 0, halfH = 0, hizMips = 1, colorMips = 1;
    bool pyramidValid = false;
    ID3D11ComputeShader *csHiZFirst = nullptr, *csHiZDown = nullptr, *csColorDown = nullptr;
    gfx::CBuffer<HiZCBData> cb;

    void init() {
        csHiZFirst = gfx::loadCS("hiz.hlsl", "csHiZFirst");
        csHiZDown = gfx::loadCS("hiz.hlsl", "csHiZDown");
        csColorDown = gfx::loadCS("hiz.hlsl", "csColorDown");
        cb.create();
    }

    void release() {
        hiz.release();
        for (auto& t : halfDepth) t.release();
        halfNormal.release();
        colorPyramid.release();
    }

    void resize(int w, int h) {
        release();
        halfW = Max(1, w / 2);
        halfH = Max(1, h / 2);
        hizMips = gfx::mipCount(halfW, halfH);
        colorMips = Min(6, hizMips);
        using namespace gfx;
        hiz = createTexture2D(halfW, halfH, DXGI_FORMAT_R32G32_FLOAT, TEX_SRV | TEX_UAV | TEX_MIP_UAVS, hizMips);
        for (auto& t : halfDepth) t = createTexture2D(halfW, halfH, DXGI_FORMAT_R32_FLOAT, TEX_SRV | TEX_UAV);
        halfNormal = createTexture2D(halfW, halfH, DXGI_FORMAT_R16G16_UNORM, TEX_SRV | TEX_UAV);
        colorPyramid = createTexture2D(halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_UAV | TEX_MIP_UAVS, colorMips);
        pyramidValid = false;
    }

    ID3D11ShaderResourceView* depthCur() const { return halfDepth[cur].srv; }
    ID3D11ShaderResourceView* depthPrev() const { return halfDepth[cur ^ 1].srv; }

    void setCB(u32 sw, u32 sh, u32 dw, u32 dh, float p0) {
        cb.data.srcW = sw;
        cb.data.srcH = sh;
        cb.data.dstW = dw;
        cb.data.dstH = dh;
        cb.data.params = vec4(p0, 0, 0, 0);
        cb.upload();
    }

    // Depth pyramid + half-resolution depth/normal from the finished G-buffer.
    void buildHiZ(Renderer& r) {
        auto* c = gfx::ctx;
        cur ^= 1;
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        setCB((u32)r.width, (u32)r.height, (u32)halfW, (u32)halfH, 0.f);
        ID3D11ShaderResourceView* srvs[2] = {r.depth.srv, r.gbNormal.srv};
        c->CSSetShaderResources(0, 2, srvs);
        ID3D11UnorderedAccessView* uavs[3] = {hiz.mipUavs[0], halfDepth[cur].uav, halfNormal.uav};
        c->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
        c->CSSetShader(csHiZFirst, nullptr, 0);
        c->Dispatch(gfx::divUp(halfW, 8), gfx::divUp(halfH, 8), 1);
        gfx::unbindCSResources(3, 3);
        c->CSSetShader(csHiZDown, nullptr, 0);
        int sw = halfW, sh = halfH;
        for (int m = 1; m < hizMips; m++) {
            int dw = Max(1, sw / 2), dh = Max(1, sh / 2);
            setCB((u32)sw, (u32)sh, (u32)dw, (u32)dh, 0.f);
            c->CSSetShaderResources(2, 1, &hiz.mipSrvs[m - 1]);
            c->CSSetUnorderedAccessViews(0, 1, &hiz.mipUavs[m], nullptr);
            c->Dispatch(gfx::divUp(dw, 8), gfx::divUp(dh, 8), 1);
            ID3D11UnorderedAccessView* nu = nullptr;
            c->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
            ID3D11ShaderResourceView* ns = nullptr;
            c->CSSetShaderResources(2, 1, &ns);
            sw = dw;
            sh = dh;
        }
    }

    // Downsample chain of the previous frame's anti-aliased HDR image.
    void buildColorPyramid(Renderer& r, ID3D11ShaderResourceView* src, bool valid) {
        pyramidValid = valid && src;
        if (!pyramidValid) return;
        auto* c = gfx::ctx;
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        c->CSSetShader(csColorDown, nullptr, 0);
        int sw = r.width, sh = r.height;
        for (int m = 0; m < colorMips; m++) {
            int dw = Max(1, halfW >> m), dh = Max(1, halfH >> m);
            setCB((u32)sw, (u32)sh, (u32)dw, (u32)dh, m == 0 ? 1.f : 0.f);
            ID3D11ShaderResourceView* s = m == 0 ? src : colorPyramid.mipSrvs[m - 1];
            c->CSSetShaderResources(3, 1, &s);
            c->CSSetUnorderedAccessViews(3, 1, &colorPyramid.mipUavs[m], nullptr);
            c->Dispatch(gfx::divUp(dw, 8), gfx::divUp(dh, 8), 1);
            ID3D11UnorderedAccessView* nu = nullptr;
            c->CSSetUnorderedAccessViews(3, 1, &nu, nullptr);
            ID3D11ShaderResourceView* ns = nullptr;
            c->CSSetShaderResources(3, 1, &ns);
            sw = dw;
            sh = dh;
        }
    }
};

}  // namespace Render
