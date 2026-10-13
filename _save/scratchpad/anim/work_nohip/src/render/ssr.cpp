// Screen-space reflections (half-res HiZ tracing + full-res resolve/temporal). Included from renderer.cpp.
namespace Render {

struct SSRCBData {
    vec4 p0, p1;
};

struct SSRSystem {
    gfx::Texture trace, history[2];
    int cur = 0;
    bool historyValid = false;
    ID3D11ComputeShader *csTrace = nullptr, *csResolve = nullptr;
    gfx::CBuffer<SSRCBData> cb;

    void init() {
        csTrace = gfx::loadCS("ssr.hlsl", "csSSRTrace");
        csResolve = gfx::loadCS("ssr.hlsl", "csSSRResolve");
        cb.create();
    }

    void resize(int w, int h, int halfW, int halfH) {
        trace.release();
        for (auto& t : history) t.release();
        trace = gfx::createTexture2D(halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        for (auto& t : history) t = gfx::createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        historyValid = false;
    }

    // Returns the full-resolution reflection texture (rgb pre-exposed radiance, a confidence) or nullptr.
    ID3D11ShaderResourceView* run(Renderer& r, ScreenSpaceSystem& ss) {
        const Settings& s = r.settings;
        if (!s.ssr || s.ssrQuality <= 0 || !ss.pyramidValid) {
            historyValid = false;
            return nullptr;
        }
        auto* c = gfx::ctx;
        int q = Clamp(s.ssrQuality, 1, 3);
        static const float iters[4] = {0, 32, 48, 72};
        static const int bayer[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
        const int* o = bayer[r.frameIndex & 3];
        float maxRough = q == 1 ? Min(s.ssrMaxRoughness, 0.3f) : s.ssrMaxRoughness;
        cb.data.p0 = vec4(iters[q], (float)(ss.hizMips - 1), 400.f, maxRough);
        cb.data.p1 = vec4((historyValid && !r.cameraCut) ? 1.f : 0.f, (float)ss.colorMips, (float)o[0], (float)o[1]);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        ID3D11ShaderResourceView* srvs[5] = {r.depth.srv, r.gbNormal.srv, r.gbMaterial.srv, ss.hiz.srv, ss.colorPyramid.srv};
        c->CSSetShaderResources(0, 5, srvs);
        c->CSSetUnorderedAccessViews(0, 1, &trace.uav, nullptr);
        c->CSSetShader(csTrace, nullptr, 0);
        c->Dispatch(gfx::divUp(ss.halfW, 8), gfx::divUp(ss.halfH, 8), 1);
        gfx::unbindCSResources(10, 1);
        int prev = cur;
        cur ^= 1;
        ID3D11ShaderResourceView* rs[10] = {r.depth.srv, r.gbNormal.srv, r.gbMaterial.srv, nullptr, nullptr,
                                            trace.srv, history[prev].srv, r.gbVelocity.srv, ss.depthCur(), ss.halfNormal.srv};
        c->CSSetShaderResources(0, 10, rs);
        c->CSSetUnorderedAccessViews(0, 1, &history[cur].uav, nullptr);
        c->CSSetShader(csResolve, nullptr, 0);
        c->Dispatch(gfx::divUp(r.width, 8), gfx::divUp(r.height, 8), 1);
        gfx::unbindCSResources(10, 1);
        historyValid = true;
        return history[cur].srv;
    }
};

}  // namespace Render
