// Volumetric cloud system. Included from renderer.cpp.
namespace Render {

struct CloudCBData {
    vec4 c0, c1, c2, c3;
};

struct CloudSystem {
    gfx::Texture shape, detail, weather, shadowMap;
    gfx::Texture history[2];
    int cur = 0;
    bool valid = false;
    ID3D11ComputeShader *csShape = nullptr, *csDetail = nullptr, *csWeather = nullptr, *csClouds = nullptr, *csShadow = nullptr;
    gfx::CBuffer<CloudCBData> cb;
    vec2 windOffset;
    int w = 0, h = 0;

    void init() {
        csShape = gfx::loadCS("clouds.hlsl", "csShapeNoise");
        csDetail = gfx::loadCS("clouds.hlsl", "csDetailNoise");
        csWeather = gfx::loadCS("clouds.hlsl", "csWeather");
        csClouds = gfx::loadCS("clouds.hlsl", "csClouds");
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
        valid = false;
    }

    void update(Renderer& r, const Environment& env, float dt) {
        auto* c = gfx::ctx;
        windOffset += env.windDir * (6.f + env.wind * 18.f) * dt;
        if (windOffset.x > 38000.f) windOffset.x -= 38000.f;
        if (windOffset.y > 38000.f) windOffset.y -= 38000.f;
        float cover = Clamp(env.cloudCover + env.rain * 0.5f, 0.f, 1.f);
        cb.data.c0 = vec4(cover, 1.f + env.rain * 1.5f, 1250.f, 1250.f + Lerp(900.f, 3500.f, Saturate(cover * 1.3f - 0.3f)) + env.rain * 4000.f);
        cb.data.c1 = vec4(windOffset.x, windOffset.y, env.gameSeconds, (float)(r.frameIndex % 64));
        cb.data.c2 = vec4((float)w, (float)h, (valid && !r.cameraCut) ? 1.f : 0.f, env.rain);
        cb.data.c3 = vec4(0);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        int prev = cur;
        cur ^= 1;
        ID3D11ShaderResourceView* srvs[5] = {shape.srv, detail.srv, weather.srv, history[prev].srv, r.depth.srv};
        c->CSSetShaderResources(0, 5, srvs);
        c->CSSetUnorderedAccessViews(1, 1, &history[cur].uav, nullptr);
        c->CSSetShader(csClouds, nullptr, 0);
        c->Dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
        ID3D11UnorderedAccessView* nu = nullptr;
        c->CSSetUnorderedAccessViews(1, 1, &nu, nullptr);
        c->CSSetUnorderedAccessViews(1, 1, &shadowMap.uav, nullptr);
        c->CSSetShader(csShadow, nullptr, 0);
        c->Dispatch(32, 32, 1);
        gfx::unbindCSResources(5, 2);
        valid = true;
    }
    ID3D11ShaderResourceView* output() const { return history[cur].srv; }
};

}  // namespace Render
