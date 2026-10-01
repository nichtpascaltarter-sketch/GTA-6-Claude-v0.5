// Volumetric cloud system: quarter-resolution ray-march (one ray per 2x2 half-res block, rotating over 4 frames)
// with temporal reconstruction at half resolution, plus the cloud shadow map. Included from renderer.cpp.
namespace Render {

struct CloudCBData {
    vec4 c0, c1, c2, c3;
};

struct CloudSystem {
    gfx::Texture shape, detail, weather, shadowMap;
    gfx::Texture trace;        // quarter-res (or half-res on ultra) ray-march result
    gfx::Texture history[2];   // half-res reconstructed clouds: rgb inscatter (not exposed), a transmittance
    int cur = 0;
    bool valid = false;
    ID3D11ComputeShader *csShape = nullptr, *csDetail = nullptr, *csWeather = nullptr, *csTrace = nullptr, *csReconstruct = nullptr,
                        *csShadow = nullptr;
    gfx::CBuffer<CloudCBData> cb;
    vec2 windOffset;
    int w = 0, h = 0;          // half resolution (output)
    int tw = 0, th = 0;        // trace resolution
    int traceMode = -1;        // 0 quarter (checkerboard), 1 half (ultra)
    float cloudBase = 1250.f, cloudTop = 2150.f;

    void init() {
        csShape = gfx::loadCS("clouds.hlsl", "csShapeNoise");
        csDetail = gfx::loadCS("clouds.hlsl", "csDetailNoise");
        csWeather = gfx::loadCS("clouds.hlsl", "csWeather");
        csTrace = gfx::loadCS("clouds.hlsl", "csCloudTrace");
        csReconstruct = gfx::loadCS("clouds.hlsl", "csCloudReconstruct");
        csShadow = gfx::loadCS("clouds.hlsl", "csCloudShadow");
        cb.create();
        shape = gfx::createTexture3D(128, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV, 1);
        detail = gfx::createTexture3D(32, 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV, 1);
        weather = gfx::createTexture2D(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV);
        shadowMap = gfx::createTexture2D(256, 256, DXGI_FORMAT_R16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        auto* c = gfx::ctx;
        c->CSSetShader(csShape, nullptr, 0);
        c->CSSetUnorderedAccessViews(0, 1, &shape.uav, nullptr);
        c->Dispatch(32, 32, 32);
        c->CSSetShader(csDetail, nullptr, 0);
        c->CSSetUnorderedAccessViews(0, 1, &detail.uav, nullptr);
        c->Dispatch(8, 8, 8);
        gfx::unbindCSResources(1, 2);
        c->CSSetShader(csWeather, nullptr, 0);
        c->CSSetUnorderedAccessViews(1, 1, &weather.uav, nullptr);
        c->Dispatch(64, 64, 1);
        gfx::unbindCSResources(1, 2);
    }

    void resize(int fullW, int fullH) {
        w = Max(1, fullW / 2);
        h = Max(1, fullH / 2);
        for (auto& t : history) {
            t.release();
            t = gfx::createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        }
        traceMode = -1;
        valid = false;
    }

    void ensureTrace(int mode) {
        if (mode == traceMode) return;
        traceMode = mode;
        tw = mode == 1 ? w : Max(1, (w + 1) / 2);
        th = mode == 1 ? h : Max(1, (h + 1) / 2);
        trace.release();
        trace = gfx::createTexture2D(tw, th, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
    }

    void update(Renderer& r, const Environment& env, float dt, ID3D11ShaderResourceView* hiz) {
        auto* c = gfx::ctx;
        int q = Clamp(r.settings.cloudQuality, 0, 3);
        ensureTrace(q >= 3 ? 1 : 0);
        windOffset += env.windDir * (6.f + env.wind * 18.f) * dt;
        if (windOffset.x > 38000.f) windOffset.x -= 38000.f;
        if (windOffset.y > 38000.f) windOffset.y -= 38000.f;
        // Storms (rain > 0.5): lower, thicker, darker overcast; fair weather is unchanged
        float storm = Saturate((env.rain - 0.5f) * 2.f);
        float cover = Clamp(env.cloudCover + env.rain * 0.5f, 0.f, 1.f);
        cover = Max(cover, storm * 0.92f);
        cloudBase = Lerp(1250.f, 700.f, storm);
        cloudTop = cloudBase + Lerp(900.f, 3500.f, Saturate(cover * 1.3f - 0.3f)) + env.rain * 2600.f;
        static const float stepScale[4] = {0.55f, 0.75f, 1.f, 1.15f};
        static const int bayer[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
        const int* o = bayer[r.frameIndex & 3];
        cb.data.c0 = vec4(cover, 1.f + env.rain * 1.5f + storm * 0.8f, cloudBase, cloudTop);
        cb.data.c1 = vec4(windOffset.x, windOffset.y, env.gameSeconds, (float)(r.frameIndex % 64));
        cb.data.c2 = vec4((float)tw, (float)th, (valid && !r.cameraCut) ? 1.f : 0.f, env.rain);
        cb.data.c3 = vec4(storm, stepScale[q], traceMode == 1 ? 0.f : (float)o[0], traceMode == 1 ? 0.f : (float)o[1]);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        // 1) trace
        ID3D11ShaderResourceView* srvs[6] = {shape.srv, detail.srv, weather.srv, nullptr, nullptr, hiz};
        c->CSSetShaderResources(0, 6, srvs);
        c->CSSetUnorderedAccessViews(1, 1, &trace.uav, nullptr);
        c->CSSetShader(csTrace, nullptr, 0);
        c->Dispatch(gfx::divUp(tw, 8), gfx::divUp(th, 8), 1);
        ID3D11UnorderedAccessView* nu = nullptr;
        c->CSSetUnorderedAccessViews(1, 1, &nu, nullptr);
        // 2) reconstruct at half resolution
        int prev = cur;
        cur ^= 1;
        ID3D11ShaderResourceView* rs[7] = {shape.srv, detail.srv, weather.srv, history[prev].srv, nullptr, hiz, trace.srv};
        c->CSSetShaderResources(0, 7, rs);
        c->CSSetUnorderedAccessViews(1, 1, &history[cur].uav, nullptr);
        c->CSSetShader(csReconstruct, nullptr, 0);
        c->Dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
        c->CSSetUnorderedAccessViews(1, 1, &nu, nullptr);
        // 3) cloud shadow map
        c->CSSetUnorderedAccessViews(1, 1, &shadowMap.uav, nullptr);
        c->CSSetShader(csShadow, nullptr, 0);
        c->Dispatch(32, 32, 1);
        gfx::unbindCSResources(7, 2);
        valid = true;
    }
    ID3D11ShaderResourceView* output() const { return history[cur].srv; }
};

}  // namespace Render
