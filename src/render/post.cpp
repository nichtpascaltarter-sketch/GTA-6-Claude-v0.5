// Post processing: TAA, bloom, exposure adaptation and tonemapping. Included from renderer.cpp.
namespace Render {

struct PostCBData {
    vec4 p0, p1, p2, p3;
};
struct TAACBData {
    vec4 params;
};
struct BloomCBData {
    vec4 params;
};

struct PostSystem {
    gfx::Buffer exposureBuf, lumPartial;
    gfx::Texture whiteTex, blackTex;
    gfx::Texture history[2];
    gfx::Texture bloomDown, bloomUp;
    int bloomLevels = 6;
    int historyIndex = 0;
    bool historyValid = false;
    ID3D11ComputeShader *csReduce = nullptr, *csExposure = nullptr, *csTAA = nullptr, *csBloomDown = nullptr, *csBloomUp = nullptr;
    ID3D11PixelShader* psTonemap = nullptr;
    gfx::CBuffer<PostCBData> cb;
    gfx::CBuffer<TAACBData> taaCB;
    gfx::CBuffer<BloomCBData> bloomCB;
    int partialCount = 0;
    float exposureCompensation = 0.3f;
    ID3D11ShaderResourceView* finalSrv = nullptr;

    void init() {
        float initExp[4] = {2.5e-5f, 15.f, 0.f, 0.f};
        exposureBuf = gfx::createBuffer(16, 16, gfx::BUF_STRUCTURED | gfx::BUF_UAV, initExp);
        u32 white = 0xffffffffu, black = 0xff000000u;
        whiteTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &white, 4);
        blackTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &black, 4);
        csReduce = gfx::loadCS("post.hlsl", "csLumReduce");
        csExposure = gfx::loadCS("post.hlsl", "csExposure");
        csBloomDown = gfx::loadCS("post.hlsl", "csBloomDown");
        csBloomUp = gfx::loadCS("post.hlsl", "csBloomUp");
        csTAA = gfx::loadCS("taa.hlsl", "csTAA");
        psTonemap = gfx::loadPS("post.hlsl", "psTonemap");
        cb.create();
        taaCB.create();
        bloomCB.create();
    }

    void resize(int w, int h) {
        lumPartial.release();
        int gx = (w + 63) / 64, gy = (h + 63) / 64;
        partialCount = gx * gy;
        lumPartial = gfx::createBuffer((u32)(partialCount * 8), 4, gfx::BUF_STRUCTURED | gfx::BUF_UAV);
        for (auto& hh : history) hh.release();
        for (auto& hh : history) hh = gfx::createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        historyValid = false;
        bloomDown.release();
        bloomUp.release();
        int bw = Max(1, w / 2), bh = Max(1, h / 2);
        bloomLevels = Min(6, gfx::mipCount(bw, bh));
        bloomDown = gfx::createTexture2D(bw, bh, DXGI_FORMAT_R11G11B10_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_MIP_UAVS, bloomLevels, 1);
        bloomUp = gfx::createTexture2D(bw, bh, DXGI_FORMAT_R11G11B10_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_MIP_UAVS, bloomLevels, 1);
    }

    void runTAA(Renderer& r) {
        auto* c = gfx::ctx;
        int cur = historyIndex ^ 1;
        taaCB.data.params = vec4((!historyValid || r.cameraCut || !r.settings.taa) ? 1.f : 0.f, 0.08f, 0, 0);
        taaCB.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), taaCB.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        ID3D11ShaderResourceView* srvs[4] = {r.hdr.srv, history[historyIndex].srv, r.gbVelocity.srv, r.depth.srv};
        c->CSSetShaderResources(0, 4, srvs);
        c->CSSetUnorderedAccessViews(0, 1, &history[cur].uav, nullptr);
        c->CSSetShader(csTAA, nullptr, 0);
        c->Dispatch(gfx::divUp(r.width, 8), gfx::divUp(r.height, 8), 1);
        gfx::unbindCSResources(4, 1);
        historyIndex = cur;
        historyValid = true;
        finalSrv = history[cur].srv;
    }

    void runBloom(Renderer& r) {
        auto* c = gfx::ctx;
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get(), bloomCB.get()};
        c->CSSetConstantBuffers(0, 3, cbs);
        // Downsample chain
        c->CSSetShader(csBloomDown, nullptr, 0);
        for (int m = 0; m < bloomLevels; m++) {
            int w = Max(1, bloomDown.width >> m), h = Max(1, bloomDown.height >> m);
            bloomCB.data.params = vec4((float)w, (float)h, m == 0 ? 1.f : 0.f, 1.f);
            bloomCB.upload();
            ID3D11ShaderResourceView* src = m == 0 ? finalSrv : bloomDown.mipSrvs[m - 1];
            c->CSSetShaderResources(3, 1, &src);
            c->CSSetUnorderedAccessViews(2, 1, &bloomDown.mipUavs[m], nullptr);
            c->Dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
            ID3D11UnorderedAccessView* nu = nullptr;
            c->CSSetUnorderedAccessViews(2, 1, &nu, nullptr);
            ID3D11ShaderResourceView* ns = nullptr;
            c->CSSetShaderResources(3, 1, &ns);
        }
        // Upsample chain: up[last] = down[last] (copy via up with zero low), then accumulate upward
        c->CSSetShader(csBloomUp, nullptr, 0);
        for (int m = bloomLevels - 1; m >= 0; m--) {
            int w = Max(1, bloomUp.width >> m), h = Max(1, bloomUp.height >> m);
            bloomCB.data.params = vec4((float)w, (float)h, 0, 1.f);
            bloomCB.upload();
            ID3D11ShaderResourceView* srcs[2] = {bloomDown.mipSrvs[m], m == bloomLevels - 1 ? blackTex.srv : bloomUp.mipSrvs[m + 1]};
            c->CSSetShaderResources(3, 2, srcs);
            c->CSSetUnorderedAccessViews(2, 1, &bloomUp.mipUavs[m], nullptr);
            c->Dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
            ID3D11UnorderedAccessView* nu = nullptr;
            c->CSSetUnorderedAccessViews(2, 1, &nu, nullptr);
            ID3D11ShaderResourceView* ns[2] = {};
            c->CSSetShaderResources(3, 2, ns);
        }
    }

    void render(Renderer& r, float dt) {
        auto* c = gfx::ctx;
        cb.data.p0 = vec4(exposureCompensation, 2.2f, 1.4f, Clamp(dt, 0.f, 0.25f));
        cb.data.p1 = vec4(0.045f, 0.22f, 0.012f, 1.08f);
        cb.data.p2 = vec4(1.04f, 0.6f, -3.f, 16.f);
        cb.data.p3 = vec4((float)partialCount, r.cameraCut ? 1.f : 0.f, 0.35f, 0);
        cb.upload();
        runTAA(r);
        runBloom(r);
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        c->PSSetConstantBuffers(0, 2, cbs);
        // Exposure from the anti-aliased image
        c->CSSetShader(csReduce, nullptr, 0);
        c->CSSetShaderResources(0, 1, &finalSrv);
        c->CSSetShaderResources(40, 1, &exposureBuf.srv);
        c->CSSetUnorderedAccessViews(0, 1, &lumPartial.uav, nullptr);
        c->Dispatch(gfx::divUp(r.width, 64), gfx::divUp(r.height, 64), 1);
        ID3D11ShaderResourceView* nullSrv[3] = {};
        ID3D11UnorderedAccessView* nullUav[2] = {};
        c->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
        c->CSSetShaderResources(0, 3, nullSrv);
        c->CSSetShaderResources(40, 1, nullSrv);
        c->CSSetShader(csExposure, nullptr, 0);
        c->CSSetShaderResources(2, 1, &lumPartial.srv);
        c->CSSetUnorderedAccessViews(1, 1, &exposureBuf.uav, nullptr);
        c->Dispatch(1, 1, 1);
        c->CSSetUnorderedAccessViews(0, 2, nullUav, nullptr);
        c->CSSetShaderResources(0, 3, nullSrv);

        // Tonemap to back buffer
        ID3D11RenderTargetView* bb = gfx::backbufferRTV();
        c->OMSetRenderTargets(1, &bb, nullptr);
        gfx::setViewport((float)r.outWidth, (float)r.outHeight);
        c->OMSetDepthStencilState(gfx::states.depthOff, 0);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullNone);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(r.vsFullscreen.vs, nullptr, 0);
        c->PSSetShader(psTonemap, nullptr, 0);
        ID3D11ShaderResourceView* srvs[2] = {finalSrv, bloomUp.srv};
        c->PSSetShaderResources(0, 2, srvs);
        c->Draw(3, 0);
        c->PSSetShaderResources(0, 2, nullSrv);
    }
};

}  // namespace Render
