// Post processing: TAA, bloom, exposure adaptation and tonemapping. Included from renderer.cpp.
namespace Render {

struct PostCBData {
    vec4 p0, p1, p2, p3;
    vec4 fx0, fx1, fx2, fx3;  // gameplay PostFxControls
    vec4 cb0, cb1, cb2;       // colour-blind correction rows (w of cb0 = enabled)
    vec4 shaft;               // crepuscular rays: xy sun position (uv), z strength (0 = off)
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
    gfx::Texture shaftA, shaftB;   // crepuscular rays: radial pass 0 (lit fraction), pass 1 (radiance)
    gfx::ComputeShader  csShafts = nullptr;
    gfx::SRV  cloudSrv = nullptr;   // this frame's cloud layer (set by the renderer; null = none)
    int bloomLevels = 6;
    int historyIndex = 0;
    bool historyValid = false;
    gfx::ComputeShader csReduce = nullptr, csExposure = nullptr, csTAA = nullptr, csBloomDown = nullptr, csBloomUp = nullptr;
    gfx::ComputeShader csTileMax = nullptr, csNeighborMax = nullptr, csMotionBlur = nullptr;
    gfx::Texture mbTiles, mbNeighbor, mbOut;
    gfx::CBuffer<MotionBlurCBData> mbCB;
    int tilesX = 0, tilesY = 0;
    gfx::SRV  displaySrv = nullptr;  // TAA output after motion blur (bloom, exposure, tonemap input)
    gfx::PixelShader  psTonemap = nullptr;
    gfx::CBuffer<PostCBData> cb;
    gfx::CBuffer<TAACBData> taaCB;
    gfx::CBuffer<BloomCBData> bloomCB;
    float exposureCompensation = 0.3f;
    gfx::SRV  finalSrv = nullptr;

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
        csShafts = gfx::loadCS("post.hlsl", "csSunShafts");
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
        shaftA.release();
        shaftB.release();
        int sw = Max(1, w / 4), sh = Max(1, h / 4);
        shaftA = gfx::createTexture2D(sw, sh, DXGI_FORMAT_R16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        shaftB = gfx::createTexture2D(sw, sh, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
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
        gfx::Resource  cbs[] = {r.frameCB.get(), mbCB.get()};
        c->csSetCBs(0, 2, cbs);
        gfx::SRV  srvs[3] = {finalSrv, r.gbVelocity.srv, r.depth.srv};
        c->csSetSRVs(0, 3, srvs);
        c->csSetUAVs(0, 1, &mbTiles.uav);
        c->setCS(csTileMax);
        c->dispatch(tilesX, tilesY, 1);
        gfx::UAV  nu[2] = {};
        c->csSetUAVs(0, 1, nu);
        c->csSetSRVs(3, 1, &mbTiles.srv);
        c->csSetUAVs(0, 1, &mbNeighbor.uav);
        c->setCS(csNeighborMax);
        c->dispatch(gfx::divUp(tilesX, 8), gfx::divUp(tilesY, 8), 1);
        c->csSetUAVs(0, 1, nu);
        c->csSetSRVs(3, 1, &mbNeighbor.srv);
        c->csSetUAVs(1, 1, &mbOut.uav);
        c->setCS(csMotionBlur);
        c->dispatch(gfx::divUp(r.width, 8), gfx::divUp(r.height, 8), 1);
        gfx::unbindCSResources(4, 2);
        displaySrv = mbOut.srv;
    }

    void runTAA(Renderer& r) {
        auto* c = gfx::ctx;
        int cur = historyIndex ^ 1;
        taaCB.data.params = vec4((!historyValid || r.cameraCut || !r.settings.taa) ? 1.f : 0.f, 0.08f, 0, 0);
        taaCB.upload();
        gfx::Resource  cbs[] = {r.frameCB.get(), taaCB.get()};
        c->csSetCBs(0, 2, cbs);
        gfx::SRV  srvs[5] = {r.hdr.srv, history[historyIndex].srv, r.gbVelocity.srv, r.depth.srv, r.reactive.srv};
        c->csSetSRVs(0, 5, srvs);
        c->csSetUAVs(0, 1, &history[cur].uav);
        c->setCS(csTAA);
        c->dispatch(gfx::divUp(r.width, 8), gfx::divUp(r.height, 8), 1);
        gfx::unbindCSResources(5, 1);
        historyIndex = cur;
        historyValid = true;
        finalSrv = history[cur].srv;
    }

    void runBloom(Renderer& r) {
        auto* c = gfx::ctx;
        gfx::Resource  cbs[] = {r.frameCB.get(), cb.get(), bloomCB.get()};
        c->csSetCBs(0, 3, cbs);
        // Downsample chain
        c->setCS(csBloomDown);
        for (int m = 0; m < bloomLevels; m++) {
            int w = Max(1, bloomDown.width >> m), h = Max(1, bloomDown.height >> m);
            bloomCB.data.params = vec4((float)w, (float)h, m == 0 ? 1.f : 0.f, 1.f);
            bloomCB.upload();
            gfx::SRV  src = m == 0 ? displaySrv : bloomDown.mipSrvs[m - 1];
            c->csSetSRVs(3, 1, &src);
            c->csSetUAVs(2, 1, &bloomDown.mipUavs[m]);
            c->dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
            gfx::UAV  nu = nullptr;
            c->csSetUAVs(2, 1, &nu);
            gfx::SRV  ns = nullptr;
            c->csSetSRVs(3, 1, &ns);
        }
        // Upsample chain: up[last] = down[last] (copy via up with zero low), then accumulate upward
        c->setCS(csBloomUp);
        for (int m = bloomLevels - 1; m >= 0; m--) {
            int w = Max(1, bloomUp.width >> m), h = Max(1, bloomUp.height >> m);
            bloomCB.data.params = vec4((float)w, (float)h, 0, 1.f);
            bloomCB.upload();
            gfx::SRV  srcs[2] = {bloomDown.mipSrvs[m], m == bloomLevels - 1 ? blackTex.srv : bloomUp.mipSrvs[m + 1]};
            c->csSetSRVs(3, 2, srcs);
            c->csSetUAVs(2, 1, &bloomUp.mipUavs[m]);
            c->dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
            gfx::UAV  nu = nullptr;
            c->csSetUAVs(2, 1, &nu);
            gfx::SRV  ns[2] = {};
            c->csSetSRVs(3, 2, ns);
        }
    }

    // Crepuscular rays through cloud gaps (see csSunShafts): two quarter-resolution radial passes towards the sun.
    // Strength 0 when the sun is behind the camera or far off screen, at night, or without volumetric clouds.
    float shaftStrength(Renderer& r, vec2& sunUV) const {
        sunUV = vec2(0.5f, 0.5f);
        if (!cloudSrv || r.moonLight || r.nightFactor >= 1.f) return 0.f;
        vec4 sc = r.viewProjNoJitter * vec4(r.sunDir * 1000.f, 1.f);
        if (sc.w <= 1.f) return 0.f;
        sunUV = vec2(sc.x / sc.w * 0.5f + 0.5f, 0.5f - sc.y / sc.w * 0.5f);
        float off = Max(Max(-sunUV.x, sunUV.x - 1.f), Max(-sunUV.y, sunUV.y - 1.f));
        float onScreen = Saturate(1.f - off / 0.6f);
        // low sun: long, reddened paths through the hazy boundary layer; more with fog and rain haze
        float golden = 1.f - Saturate(r.sunElevation / 25.f);
        return 0.03f * Lerp(0.5f, 1.f, golden) * (1.f + r.frame.fog.x) * onScreen * (1.f - r.nightFactor);
    }

    void runShafts(Renderer& r) {
        auto* c = gfx::ctx;
        gfx::Resource  cbs[] = {r.frameCB.get(), cb.get(), bloomCB.get()};
        c->csSetCBs(0, 3, cbs);
        c->setCS(csShafts);
        gfx::SRV  lut[1] = {r.sky->transmittance.srv};
        c->csSetSRVs(33, 1, lut);
        c->csSetSRVs(40, 1, &exposureBuf.srv);
        for (int pass = 0; pass < 2; pass++) {
            gfx::Texture& dst = pass == 0 ? shaftA : shaftB;
            bloomCB.data.params = vec4((float)dst.width, (float)dst.height, (float)pass, 0.f);
            bloomCB.upload();
            gfx::SRV  src = pass == 0 ? cloudSrv : shaftA.srv;
            c->csSetSRVs(3, 1, &src);
            c->csSetUAVs(2, 1, &dst.uav);
            c->dispatch(gfx::divUp(dst.width, 8), gfx::divUp(dst.height, 8), 1);
            gfx::UAV  nu = nullptr;
            c->csSetUAVs(2, 1, &nu);
            gfx::SRV  ns = nullptr;
            c->csSetSRVs(3, 1, &ns);
        }
        gfx::SRV  ns1[1] = {};
        c->csSetSRVs(33, 1, ns1);
        c->csSetSRVs(40, 1, ns1);
    }

    void render(Renderer& r, float dt) {
        auto* c = gfx::ctx;
        // Night grade: slightly darker exposure (contrasty streets, lights and neon pop) and stronger bloom glow
        float night = r.nightFactor;
        // Storms: a little darker too (brooding sky instead of a blown-out grey dome)
        float storm = r.frame.weather2.y;
        cb.data.p0 = vec4(exposureCompensation - 0.55f * night - 0.35f * storm * (1.f - night), 2.2f, 1.4f, Clamp(dt, 0.f, 0.25f));
        cb.data.p1 = vec4(Lerp(0.06f, 0.115f, night), 0.22f + 0.08f * night, 0.012f, 1.08f + 0.06f * night);   // bloom: tighter chain (csBloomUp), stronger core
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
        vec2 sunUV;
        float shafts = shaftStrength(r, sunUV);
        cb.data.shaft = vec4(sunUV.x, sunUV.y, shafts, 0.f);
        cb.upload();
        runTAA(r);
        runMotionBlur(r, dt);
        runBloom(r);
        if (shafts > 0.f) runShafts(r);
        gfx::Resource  cbs[] = {r.frameCB.get(), cb.get()};
        c->csSetCBs(0, 2, cbs);
        c->psSetCBs(0, 2, cbs);
        // Exposure from the anti-aliased image: luminance histogram, then metering + adaptation
        c->setCS(csReduce);
        c->csSetSRVs(0, 1, &displaySrv);
        c->csSetSRVs(5, 1, &r.depth.srv);
        c->csSetSRVs(40, 1, &exposureBuf.srv);
        c->csSetUAVs(0, 1, &lumHist.uav);
        c->dispatch(gfx::divUp(r.width, 64), gfx::divUp(r.height, 64), 1);
        gfx::SRV  nullSrv[6] = {};
        gfx::UAV  nullUav[2] = {};
        c->csSetSRVs(0, 6, nullSrv);
        c->csSetSRVs(40, 1, nullSrv);
        c->setCS(csExposure);
        gfx::UAV  expUavs[2] = {lumHist.uav, exposureBuf.uav};
        c->csSetUAVs(0, 2, expUavs);
        c->dispatch(1, 1, 1);
        c->csSetUAVs(0, 2, nullUav);
        c->csSetSRVs(0, 3, nullSrv);

        // Tonemap to back buffer
        gfx::RTV  bb = gfx::backbufferRTV();
        c->setRenderTargets(1, &bb, nullptr);
        gfx::setViewport((float)r.outWidth, (float)r.outHeight);
        c->setDepthState(gfx::states.depthOff);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullNone);
        c->setInputLayout(nullptr);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        c->setVS(r.vsFullscreen.vs);
        c->setPS(psTonemap);
        gfx::SRV  srvs[6] = {displaySrv, bloomUp.srv, shafts > 0.f ? shaftB.srv : blackTex.srv, nullptr, nullptr, r.depth.srv};
        c->psSetSRVs(0, 6, srvs);
        c->psSetSRVs(40, 1, &exposureBuf.srv);
        c->draw(3, 0);
        gfx::SRV  nullSrv6[6] = {};
        c->psSetSRVs(0, 6, nullSrv6);
        c->psSetSRVs(40, 1, nullSrv6);
    }
};

}  // namespace Render
