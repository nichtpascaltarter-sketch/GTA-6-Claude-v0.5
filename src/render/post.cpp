// Post processing: TAA, bloom, exposure adaptation and tonemapping. Included from renderer.cpp.
namespace Render {

struct PostCBData {
    vec4 p0, p1, p2, p3;
    vec4 fx0, fx1, fx2, fx3;  // gameplay PostFxControls
    vec4 cb0, cb1, cb2;       // colour-blind correction rows (w of cb0 = enabled)
};
struct MotionBlurCBData {
    vec4 params;
};
struct TAACBData {
    vec4 params;
};
struct BloomCBData {
    vec4 params;
};

struct PostSystem {
    gfx::Buffer exposureBuf, lumHist;
    gfx::Texture whiteTex, blackTex;
    gfx::Texture history[2];
    gfx::Texture bloomDown, bloomUp;
    int bloomLevels = 6;
    int historyIndex = 0;
    bool historyValid = false;
    ID3D11ComputeShader *csReduce = nullptr, *csExposure = nullptr, *csTAA = nullptr, *csBloomDown = nullptr, *csBloomUp = nullptr;
    ID3D11ComputeShader *csTileMax = nullptr, *csNeighborMax = nullptr, *csMotionBlur = nullptr;
    gfx::Texture mbTiles, mbNeighbor, mbOut;
    gfx::CBuffer<MotionBlurCBData> mbCB;
    int tilesX = 0, tilesY = 0;
    ID3D11ShaderResourceView* displaySrv = nullptr;  // TAA output after motion blur (bloom, exposure, tonemap input)
    ID3D11PixelShader* psTonemap = nullptr;
    gfx::CBuffer<PostCBData> cb;
    gfx::CBuffer<TAACBData> taaCB;
    gfx::CBuffer<BloomCBData> bloomCB;
    float exposureCompensation = 0.3f;
    ID3D11ShaderResourceView* finalSrv = nullptr;

    void init() {
        float initExp[8] = {2.5e-5f, 15.f, 0.f, 0.f, 2.5e-5f, 0.f, 0.f, 0.f};
        exposureBuf = gfx::createBuffer(32, 16, gfx::BUF_STRUCTURED | gfx::BUF_UAV, initExp);
        u32 white = 0xffffffffu, black = 0xff000000u;
        whiteTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &white, 4);
        blackTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &black, 4);
        csReduce = gfx::loadCS("post.hlsl", "csLumHist");
        u32 zeroHist[128] = {};
        lumHist = gfx::createBuffer(128 * 4, 4, gfx::BUF_STRUCTURED | gfx::BUF_UAV, zeroHist);
        csExposure = gfx::loadCS("post.hlsl", "csExposure");
        csBloomDown = gfx::loadCS("post.hlsl", "csBloomDown");
        csBloomUp = gfx::loadCS("post.hlsl", "csBloomUp");
        csTAA = gfx::loadCS("taa.hlsl", "csTAA");
        psTonemap = gfx::loadPS("post.hlsl", "psTonemap");
        csTileMax = gfx::loadCS("motionblur.hlsl", "csTileMax");
        csNeighborMax = gfx::loadCS("motionblur.hlsl", "csNeighborMax");
        csMotionBlur = gfx::loadCS("motionblur.hlsl", "csMotionBlur");
        mbCB.create();
        cb.create();
        taaCB.create();
        bloomCB.create();
    }

    void resize(int w, int h) {
        for (auto& hh : history) hh.release();
        for (auto& hh : history) hh = gfx::createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        historyValid = false;
        bloomDown.release();
        bloomUp.release();
        int bw = Max(1, w / 2), bh = Max(1, h / 2);
        bloomLevels = Min(6, gfx::mipCount(bw, bh));
        bloomDown = gfx::createTexture2D(bw, bh, DXGI_FORMAT_R11G11B10_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_MIP_UAVS, bloomLevels, 1);
        bloomUp = gfx::createTexture2D(bw, bh, DXGI_FORMAT_R11G11B10_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_MIP_UAVS, bloomLevels, 1);
        mbTiles.release();
        mbNeighbor.release();
        mbOut.release();
        tilesX = (w + 15) / 16;
        tilesY = (h + 15) / 16;
        mbTiles = gfx::createTexture2D(tilesX, tilesY, DXGI_FORMAT_R16G16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        mbNeighbor = gfx::createTexture2D(tilesX, tilesY, DXGI_FORMAT_R16G16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        mbOut = gfx::createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
    }

    // Motion blur of the TAA output into mbOut (the TAA history itself stays sharp).
    void runMotionBlur(Renderer& r, float dt) {
        displaySrv = finalSrv;
        if (!r.settings.motionBlur || r.settings.motionBlurAmount <= 0.f || r.cameraCut || dt <= 0.f) return;
        auto* c = gfx::ctx;
        // Velocities are per frame; scale to a fixed 1/60 s x amount shutter so blur does not depend on frame rate
        float shutter = Clamp(r.settings.motionBlurAmount * 0.5f * (1.f / 60.f) / dt, 0.f, 1.5f);
        float maxPx = 0.04f * (float)r.height;
        mbCB.data.params = vec4(shutter, maxPx, (float)tilesX, (float)tilesY);
        mbCB.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), mbCB.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        ID3D11ShaderResourceView* srvs[3] = {finalSrv, r.gbVelocity.srv, r.depth.srv};
        c->CSSetShaderResources(0, 3, srvs);
        c->CSSetUnorderedAccessViews(0, 1, &mbTiles.uav, nullptr);
        c->CSSetShader(csTileMax, nullptr, 0);
        c->Dispatch(tilesX, tilesY, 1);
        ID3D11UnorderedAccessView* nu[2] = {};
        c->CSSetUnorderedAccessViews(0, 1, nu, nullptr);
        c->CSSetShaderResources(3, 1, &mbTiles.srv);
        c->CSSetUnorderedAccessViews(0, 1, &mbNeighbor.uav, nullptr);
        c->CSSetShader(csNeighborMax, nullptr, 0);
        c->Dispatch(gfx::divUp(tilesX, 8), gfx::divUp(tilesY, 8), 1);
        c->CSSetUnorderedAccessViews(0, 1, nu, nullptr);
        c->CSSetShaderResources(3, 1, &mbNeighbor.srv);
        c->CSSetUnorderedAccessViews(1, 1, &mbOut.uav, nullptr);
        c->CSSetShader(csMotionBlur, nullptr, 0);
        c->Dispatch(gfx::divUp(r.width, 8), gfx::divUp(r.height, 8), 1);
        gfx::unbindCSResources(4, 2);
        displaySrv = mbOut.srv;
    }

    void runTAA(Renderer& r) {
        auto* c = gfx::ctx;
        int cur = historyIndex ^ 1;
        taaCB.data.params = vec4((!historyValid || r.cameraCut || !r.settings.taa) ? 1.f : 0.f, 0.08f, 0, 0);
        taaCB.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), taaCB.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        ID3D11ShaderResourceView* srvs[5] = {r.hdr.srv, history[historyIndex].srv, r.gbVelocity.srv, r.depth.srv, r.reactive.srv};
        c->CSSetShaderResources(0, 5, srvs);
        c->CSSetUnorderedAccessViews(0, 1, &history[cur].uav, nullptr);
        c->CSSetShader(csTAA, nullptr, 0);
        c->Dispatch(gfx::divUp(r.width, 8), gfx::divUp(r.height, 8), 1);
        gfx::unbindCSResources(5, 1);
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
            ID3D11ShaderResourceView* src = m == 0 ? displaySrv : bloomDown.mipSrvs[m - 1];
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
        // Night grade: slightly darker exposure (contrasty streets, lights and neon pop) and stronger bloom glow
        float night = r.nightFactor;
        // Storms: a little darker too (brooding sky instead of a blown-out grey dome)
        float storm = r.frame.weather2.y;
        cb.data.p0 = vec4(exposureCompensation - 0.55f * night - 0.35f * storm * (1.f - night), 2.2f, 1.4f, Clamp(dt, 0.f, 0.25f));
        cb.data.p1 = vec4(Lerp(0.045f, 0.075f, night), 0.22f + 0.08f * night, 0.012f, 1.08f + 0.06f * night);
        // Exposure limits. By day a dark surface right in front of the camera (an awning, a wall, a car) must not
        // drive the exposure to night levels: the sunlit world around it would blow out. The floor follows the sun
        // (fully shaded daytime streets meter around EV 12.5); inside interiors the exposure may open up further.
        float minEV = Lerp(-3.f, 12.f, SmoothStep(-6.f, 15.f, r.sunElevation));
        if (r.cameraInInterior()) minEV = Min(minEV, 5.f);
        cb.data.p2 = vec4(1.04f, 0.6f, minEV, 16.f);
        cb.data.p3 = vec4(0.f, r.cameraCut ? 1.f : 0.f, 0.35f, 0);
        const PostFxControls& fx = r.postFx;
        cb.data.fx0 = vec4(Max(fx.saturation, 0.f), Saturate(fx.vignette), Saturate(fx.chromatic), Saturate(fx.flash));
        cb.data.fx1 = vec4(fx.tint, Saturate(fx.blur));
        cb.data.fx2 = vec4(fx.vignetteColor, Saturate(fx.underwater));
        cb.data.fx3 = vec4(fx.flashColor, Saturate(fx.grain));
        const float* m = r.settings.colorblind;
        cb.data.cb0 = vec4(m[0], m[1], m[2], r.settings.colorblindOn ? 1.f : 0.f);
        cb.data.cb1 = vec4(m[3], m[4], m[5], 0.f);
        cb.data.cb2 = vec4(m[6], m[7], m[8], 0.f);
        cb.upload();
        runTAA(r);
        runMotionBlur(r, dt);
        runBloom(r);
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        c->PSSetConstantBuffers(0, 2, cbs);
        // Exposure from the anti-aliased image: luminance histogram, then metering + adaptation
        c->CSSetShader(csReduce, nullptr, 0);
        c->CSSetShaderResources(0, 1, &displaySrv);
        c->CSSetShaderResources(5, 1, &r.depth.srv);
        c->CSSetShaderResources(40, 1, &exposureBuf.srv);
        c->CSSetUnorderedAccessViews(0, 1, &lumHist.uav, nullptr);
        c->Dispatch(gfx::divUp(r.width, 64), gfx::divUp(r.height, 64), 1);
        ID3D11ShaderResourceView* nullSrv[6] = {};
        ID3D11UnorderedAccessView* nullUav[2] = {};
        c->CSSetShaderResources(0, 6, nullSrv);
        c->CSSetShaderResources(40, 1, nullSrv);
        c->CSSetShader(csExposure, nullptr, 0);
        ID3D11UnorderedAccessView* expUavs[2] = {lumHist.uav, exposureBuf.uav};
        c->CSSetUnorderedAccessViews(0, 2, expUavs, nullptr);
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
        ID3D11ShaderResourceView* srvs[6] = {displaySrv, bloomUp.srv, nullptr, nullptr, nullptr, r.depth.srv};
        c->PSSetShaderResources(0, 6, srvs);
        c->PSSetShaderResources(40, 1, &exposureBuf.srv);
        c->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv6[6] = {};
        c->PSSetShaderResources(0, 6, nullSrv6);
        c->PSSetShaderResources(40, 1, nullSrv6);
    }
};

}  // namespace Render
