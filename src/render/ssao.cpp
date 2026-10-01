// Ambient occlusion + one-bounce screen-space indirect diffuse (visibility bitmask GTAO) at half resolution.
// Included from renderer.cpp (after screenspace.cpp).
namespace Render {

struct GTAOCBData {
    vec4 ao0, ao1, ao2;
};

struct AOSystem {
    gfx::Texture raw, history[2], filtered;
    int cur = 0;
    bool historyValid = false;
    gfx::ComputeShader csTrace = nullptr, csTemporal = nullptr, csBlur = nullptr;
    gfx::CBuffer<GTAOCBData> cb;
    int w = 0, h = 0;

    void init() {
        csTrace = gfx::loadCS("gtao.hlsl", "csGTAO");
        csTemporal = gfx::loadCS("gtao.hlsl", "csGTAOTemporal");
        csBlur = gfx::loadCS("gtao.hlsl", "csGTAOBlur");
        cb.create();
    }

    void resize(int halfW, int halfH) {
        raw.release();
        filtered.release();
        for (auto& t : history) t.release();
        w = halfW;
        h = halfH;
        using namespace gfx;
        raw = createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_UAV);
        filtered = createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_UAV);
        for (auto& t : history) t = createTexture2D(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_UAV);
        historyValid = false;
    }

    // Returns the SRV to use in the lighting pass (rgb = GI, a = AO) or nullptr when disabled.
    gfx::SRV  run(Renderer& r, ScreenSpaceSystem& ss) {
        const Settings& s = r.settings;
        if (!s.ssao || s.aoQuality <= 0) {
            historyValid = false;
            return nullptr;
        }
        auto* c = gfx::ctx;
        int q = Clamp(s.aoQuality, 1, 3);
        static const float slices[4] = {0, 1, 2, 3}, steps[4] = {0, 6, 8, 10};
        bool gi = s.ssgi;
        cb.data.ao0 = vec4(slices[q], steps[q], 1.6f, gi ? 6.f : 1.6f);
        cb.data.ao1 = vec4(0.45f, 1.15f, 1.0f, (float)h * 0.3f);
        cb.data.ao2 = vec4((historyValid && !r.cameraCut) ? 1.f : 0.f, 0.1f, gi ? 1.f : 0.f, ss.pyramidValid ? 1.f : 0.f);
        cb.upload();
        gfx::Resource  cbs[] = {r.frameCB.get(), cb.get()};
        c->csSetCBs(0, 2, cbs);
        // 1) trace
        gfx::SRV  srvs[3] = {ss.depthCur(), ss.halfNormal.srv, ss.colorPyramid.srv};
        c->csSetSRVs(0, 3, srvs);
        c->csSetUAVs(0, 1, &raw.uav);
        c->setCS(csTrace);
        c->dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
        gfx::unbindCSResources(7, 1);
        // 2) temporal
        int prev = cur;
        cur ^= 1;
        gfx::SRV  tsrv[7] = {ss.depthCur(), ss.halfNormal.srv, nullptr, history[prev].srv, ss.depthPrev(), r.gbVelocity.srv, raw.srv};
        c->csSetSRVs(0, 7, tsrv);
        c->csSetUAVs(0, 1, &history[cur].uav);
        c->setCS(csTemporal);
        c->dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
        gfx::unbindCSResources(7, 1);
        // 3) edge-aware blur for the lighting pass (history keeps the unblurred signal)
        gfx::SRV  bsrv[7] = {ss.depthCur(), ss.halfNormal.srv, nullptr, nullptr, nullptr, nullptr, history[cur].srv};
        c->csSetSRVs(0, 7, bsrv);
        c->csSetUAVs(0, 1, &filtered.uav);
        c->setCS(csBlur);
        c->dispatch(gfx::divUp(w, 8), gfx::divUp(h, 8), 1);
        gfx::unbindCSResources(7, 1);
        historyValid = true;
        return filtered.srv;
    }
};

}  // namespace Render
