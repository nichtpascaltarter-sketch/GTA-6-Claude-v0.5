// Froxel volumetric fog (height fog, humidity haze, rain haze) lit by the sun with CSM + cloud shadows, sky
// ambient and local lights; temporal reprojection and front-to-back integration. Included from renderer.cpp.
namespace Render {

struct FogCBData {
    vec4 vol, jit, media, misc, interior;
};

struct VolumetricFog {
    gfx::Texture inject[2], integrated;
    int cur = 0;
    bool historyValid = false;
    int w = 0, h = 0, d = 0;
    ID3D11ComputeShader *csInject = nullptr, *csIntegrate = nullptr;
    gfx::Texture noise;   // tileable density variation
    gfx::CBuffer<FogCBData> cb;
    vec2 windOffset;
    static constexpr float kNear = 0.5f;

    void init() {
        csInject = gfx::loadCS("fog.hlsl", "csFogInject");
        csIntegrate = gfx::loadCS("fog.hlsl", "csFogIntegrate");
        cb.create();
        noise = gfx::createTexture3D(64, 64, 64, DXGI_FORMAT_R8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV, 1);
        ID3D11ComputeShader* gen = gfx::loadCS("fog.hlsl", "csFogNoise");
        gfx::ctx->CSSetShader(gen, nullptr, 0);
        gfx::ctx->CSSetUnorderedAccessViews(1, 1, &noise.uav, nullptr);
        gfx::ctx->Dispatch(16, 16, 16);
        gfx::unbindCSResources(1, 2);
        gen->Release();
    }

    void ensure(int quality) {
        static const int dims[4][3] = {{0, 0, 0}, {96, 54, 48}, {160, 90, 64}, {192, 108, 96}};
        int q = Clamp(quality, 1, 3);
        if (dims[q][0] == w && dims[q][2] == d) return;
        w = dims[q][0];
        h = dims[q][1];
        d = dims[q][2];
        for (auto& t : inject) t.release();
        integrated.release();
        for (auto& t : inject) t = gfx::createTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV, 1);
        integrated = gfx::createTexture3D(w, h, d, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV, 1);
        historyValid = false;
    }

    bool enabled(const Settings& s) const { return s.volumetrics && s.fogQuality > 0; }
    ID3D11ShaderResourceView* output(const Settings& s) const { return enabled(s) && integrated.srv ? integrated.srv : nullptr; }

    // Fills the fog fields of the frame constants (called before the frame CB upload).
    void setFrameParams(Renderer& r, const Environment& env, FrameConstants& f) {
        const Settings& s = r.settings;
        if (!enabled(s)) {
            f.fogParams0 = vec4(0, 0, 0, s.fogDistance);
            f.fogParams1 = vec4(0.f);
            return;
        }
        float farD = Clamp(s.fogDistance, 500.f, 8000.f);
        float groundFog = env.fogDensity * 0.012f;
        f.fogParams0 = vec4(groundFog, 1.f / 45.f, 1.5f, farD);
        float g = Lerp(0.72f, 0.55f, Saturate(env.rain + env.fogDensity));  // strong forward scattering for god rays
        f.fogParams1 = vec4(g, 1.f / log2f(farD / kNear), kNear, 1.f);
    }

    ID3D11ShaderResourceView* run(Renderer& r, const Environment& env, float dt) {
        if (!enabled(r.settings)) {
            historyValid = false;
            return nullptr;
        }
        ensure(r.settings.fogQuality);
        auto* c = gfx::ctx;
        // the integrated volume is also bound as a global (t38): unbind it while it is being written
        ID3D11ShaderResourceView* nullSrv = nullptr;
        c->CSSetShaderResources(38, 1, &nullSrv);
        c->PSSetShaderResources(38, 1, &nullSrv);
        c->VSSetShaderResources(38, 1, &nullSrv);
        windOffset += env.windDir * (1.f + env.wind * 6.f) * dt;
        if (length(windOffset) > 5000.f) windOffset = vec2(0);
        static const float halton2[8] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f};
        static const float halton3[8] = {0.333f, 0.667f, 0.111f, 0.444f, 0.778f, 0.222f, 0.556f, 0.889f};
        int fi = (int)(r.frameIndex % 8);
        // Humid sub-tropical air: some haze always, more at night (glows around lights) and in rain
        float humidity = 0.00014f * env.haze * (1.f + 1.6f * r.nightFactor) + 0.0004f * env.fogDensity;
        cb.data.vol = vec4((float)w, (float)h, (float)d, (historyValid && !r.cameraCut) ? 1.f : 0.f);
        cb.data.jit = vec4(halton2[fi] - 0.5f, halton3[fi] - 0.5f, halton2[(fi * 3 + 1) % 8] - 0.5f, (float)r.lightsFrame.size());
        cb.data.media = vec4(humidity, 350.f, env.rain * 0.0022f, 0.35f + 0.3f * env.fogDensity);
        cb.data.misc = vec4(windOffset.x, windOffset.y, 0.35f, 1.f + env.lightning * 6.f);
        cb.data.interior = vec4((float)r.lightCB.data.interiorCount, 0, 0, 0);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get(), nullptr, r.shadowCB.get()};
        c->CSSetConstantBuffers(0, 4, cbs);
        int prev = cur;
        cur ^= 1;
        ID3D11ShaderResourceView* srvs[6] = {r.lightBuf.srv, inject[prev].srv, nullptr, noise.srv, r.interiorBuf.srv, r.lightVolumeBuf.srv};
        c->CSSetShaderResources(0, 6, srvs);
        c->CSSetUnorderedAccessViews(0, 1, &inject[cur].uav, nullptr);
        c->CSSetShader(csInject, nullptr, 0);
        c->Dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), d);
        gfx::unbindCSResources(6, 1);
        c->CSSetShaderResources(2, 1, &inject[cur].srv);
        c->CSSetUnorderedAccessViews(0, 1, &integrated.uav, nullptr);
        c->CSSetShader(csIntegrate, nullptr, 0);
        c->Dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
        gfx::unbindCSResources(3, 1);
        historyValid = true;
        return integrated.srv;
    }
};

}  // namespace Render
