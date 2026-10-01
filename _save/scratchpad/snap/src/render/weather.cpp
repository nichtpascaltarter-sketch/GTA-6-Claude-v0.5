// Weather rendering: overhead height map of the static world (rain occlusion, dry areas under roofs, grass
// placement), rain streaks + splashes, lightning bolts and the weather frame parameters. Included from
// renderer.cpp.
namespace Render {

struct RainCBData {
    vec4 r0, r1, r2, r3;
    vec4 lights[32];
};

struct WeatherSystem {
    // Overhead map
    static constexpr int kOverheadRes = 1024;
    static constexpr float kOverheadSize = 192.f;
    gfx::Texture overheadDepth, overheadHeight, overheadGrass;
    gfx::VertexShader vsOverhead;
    ID3D11PixelShader* psOverhead = nullptr;
    gfx::CBuffer<ShadowPassCBData> passCB;
    dvec3 overheadOrigin;          // world min corner of the map
    float overheadTop = 0.f, overheadRange = 600.f;
    bool overheadValid = false;
    int overheadAge = 0;
    ID3D11ComputeShader* csOverhead = nullptr;
    // Rain + lightning
    gfx::CBuffer<RainCBData> cb;
    gfx::VertexShader vsRain, vsSplash, vsBolt;
    ID3D11PixelShader *psRain = nullptr, *psSplash = nullptr, *psBolt = nullptr;
    ID3D11BlendState* blend = nullptr;
    gfx::Buffer boltBuf;
    static const int kMaxBoltSegments = 256;
    std::vector<vec4> boltSegs;   // pairs: (p0 world offset from boltOrigin, width), (p1 offset, brightness)
    dvec3 boltOrigin;
    vec3 boltDir = vec3(0, 1, 0);
    float lastLightning = 0.f;
    u32 rng = 0x9e3779b9u;
    float rippleTime = 0.f;

    float rnd() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (rng >> 8) * (1.f / 16777216.f);
    }

    void init() {
        overheadDepth = gfx::createTexture2D(kOverheadRes, kOverheadRes, DXGI_FORMAT_R32_TYPELESS, gfx::TEX_DSV | gfx::TEX_SRV);
        overheadHeight = gfx::createTexture2D(kOverheadRes, kOverheadRes, DXGI_FORMAT_R32_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        overheadGrass = gfx::createTexture2D(kOverheadRes, kOverheadRes, DXGI_FORMAT_R8_UNORM, gfx::TEX_SRV | gfx::TEX_RTV);
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        vsOverhead = gfx::loadVS("overhead.hlsl", "vsOverhead", layout, 6);
        psOverhead = gfx::loadPS("overhead.hlsl", "psOverhead");
        passCB.create();
        cb.create();
        csOverhead = gfx::loadCS("rain.hlsl", "csOverheadHeight");
        vsRain = gfx::loadVS("rain.hlsl", "vsRain", nullptr, 0);
        vsSplash = gfx::loadVS("rain.hlsl", "vsSplash", nullptr, 0);
        vsBolt = gfx::loadVS("rain.hlsl", "vsBolt", nullptr, 0);
        psRain = gfx::loadPS("rain.hlsl", "psRain");
        psSplash = gfx::loadPS("rain.hlsl", "psSplash");
        psBolt = gfx::loadPS("rain.hlsl", "psBolt");
        boltBuf = gfx::createBuffer(kMaxBoltSegments * 2 * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        // RT0: premultiplied color, RT1: reactive mask (max)
        D3D11_BLEND_DESC bd = {};
        bd.IndependentBlendEnable = TRUE;
        D3D11_RENDER_TARGET_BLEND_DESC& a = bd.RenderTarget[0];
        a.BlendEnable = TRUE;
        a.SrcBlend = D3D11_BLEND_ONE;
        a.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        a.BlendOp = D3D11_BLEND_OP_ADD;
        a.SrcBlendAlpha = D3D11_BLEND_ONE;
        a.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        a.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        a.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_RENDER_TARGET_BLEND_DESC& m = bd.RenderTarget[1];
        m.BlendEnable = TRUE;
        m.SrcBlend = D3D11_BLEND_ONE;
        m.DestBlend = D3D11_BLEND_ONE;
        m.BlendOp = D3D11_BLEND_OP_MAX;
        m.SrcBlendAlpha = D3D11_BLEND_ONE;
        m.DestBlendAlpha = D3D11_BLEND_ONE;
        m.BlendOpAlpha = D3D11_BLEND_OP_MAX;
        m.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        gfx::dev->CreateBlendState(&bd, &blend);
    }

    // Top-down depth of the static world cells around the camera (re-rendered when the camera moves a grid step
    // or periodically to pick up streamed cells).
    void updateOverhead(Renderer& r) {
        dvec3 cam = r.camera.pos;
        const double step = 16.0;
        dvec3 center(floor(cam.x / step) * step + step * 0.5, floor(cam.y / step) * step + step * 0.5, cam.z);
        dvec3 origin(center.x - kOverheadSize * 0.5, center.y - kOverheadSize * 0.5, 0.0);
        overheadAge++;
        if (overheadValid && origin.x == overheadOrigin.x && origin.y == overheadOrigin.y && overheadAge < 30 &&
            fabs(cam.z + 300.0 - overheadTop) < 150.0)
            return;
        overheadAge = 0;
        overheadOrigin = origin;
        overheadTop = (float)(cam.z + 300.0);
        auto* c = gfx::ctx;
        // camera-relative ortho view looking straight down from overheadTop
        vec3 eye((float)(center.x - cam.x), (float)(center.y - cam.y), overheadTop - (float)cam.z);
        mat4 view = mat4Translation(-eye);
        float hs = kOverheadSize * 0.5f;
        mat4 proj = orthoRH(-hs, hs, -hs, hs, 0.f, overheadRange);
        mat4 vp = proj * view;
        ID3D11ShaderResourceView* nullSrv = nullptr;
        c->VSSetShaderResources(41, 1, &nullSrv);
        c->PSSetShaderResources(41, 1, &nullSrv);
        c->CSSetShaderResources(41, 1, &nullSrv);
        float zero4[4] = {0, 0, 0, 0};
        c->ClearDepthStencilView(overheadDepth.dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
        c->ClearRenderTargetView(overheadGrass.rtv, zero4);
        c->OMSetRenderTargets(1, &overheadGrass.rtv, overheadDepth.dsv);
        gfx::setViewport((float)kOverheadRes, (float)kOverheadRes);
        c->OMSetDepthStencilState(gfx::states.depthLessWrite, 0);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullNone);
        passCB.data.viewProj = vp;
        passCB.upload();
        ID3D11Buffer* cbs[] = {r.world->drawCB.get(), passCB.get()};
        c->VSSetConstantBuffers(1, 2, cbs);
        c->IASetInputLayout(vsOverhead.layout);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vsOverhead.vs, nullptr, 0);
        c->PSSetShader(psOverhead, nullptr, 0);
        Frustum fr;
        fr.fromMatrix(vp);
        r.world->forVisible(fr, r.camera.pos, false, [&](StreamCell* sc, vec3 off) {
            if (sc->lod != 0 || !sc->opaqueCount) return;
            r.world->drawCB.data.cellOffset = vec4(off, 0);
            r.world->drawCB.data.params = vec4(0);
            r.world->drawCB.upload();
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->IASetVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            c->DrawIndexed(sc->opaqueCount, 0, 0);
            r.stats.drawCalls++;
        });
        c->OMSetRenderTargets(0, nullptr, nullptr);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullBack);
        // depth -> height
        cb.data.r3 = vec4(overheadTop, overheadRange, 0, 0);
        cb.upload();
        ID3D11Buffer* ccbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, ccbs);
        c->CSSetShaderResources(0, 1, &overheadDepth.srv);
        c->CSSetUnorderedAccessViews(0, 1, &overheadHeight.uav, nullptr);
        c->CSSetShader(csOverhead, nullptr, 0);
        c->Dispatch(kOverheadRes / 8, kOverheadRes / 8, 1);
        gfx::unbindCSResources(1, 1);
        overheadValid = true;
    }

    ID3D11ShaderResourceView* overheadSrv() const { return overheadValid ? overheadHeight.srv : nullptr; }

    // Weather-related frame constants.
    void setFrameParams(Renderer& r, const Environment& env, FrameConstants& f, float dt) {
        rippleTime += dt * (0.6f + env.rain * 0.8f);
        if (rippleTime > 1000.f) rippleTime -= 1000.f;
        float cover = Saturate(env.cloudCover + env.rain * 0.5f);
        float overcast = SmoothStep(0.55f, 1.f, cover);
        float storm = Saturate((env.rain - 0.5f) * 2.f);
        float puddles = Saturate(env.wetness * 1.2f - 0.15f);
        f.weather2 = vec4(overcast, storm, puddles, rippleTime);
        f.overhead = overheadValid ? vec4((float)overheadOrigin.x, (float)overheadOrigin.y, kOverheadSize, 1.f) : vec4(0.f);
        // Lightning: flash brightness + direction to the current bolt
        float flash = Saturate(env.lightning);
        f.lightning = vec4(flash, boltDir.x, boltDir.y, boltDir.z);
        f.ambientParams.y = flash * 2500.f;
        (void)r;
    }

    // New bolt on each rising edge of the flash intensity.
    void updateLightning(Renderer& r, const Environment& env, float cloudBase) {
        float fl = env.lightning;
        if (fl > 0.3f && lastLightning <= 0.1f) {
            boltSegs.clear();
            float ang = rnd() * kTwoPi;
            float dist = 1500.f + rnd() * 4000.f;
            dvec3 cam = r.camera.pos;
            boltOrigin = dvec3(cam.x + cosf(ang) * dist, cam.y + sinf(ang) * dist, 0.0);
            float ground = World::gMap ? Max(World::gMap->heightAt((float)boltOrigin.x, (float)boltOrigin.y), 0.f) : 0.f;
            vec3 top(rnd() * 200.f - 100.f, rnd() * 200.f - 100.f, cloudBase);
            vec3 bottom(0, 0, ground);
            buildBolt(top, bottom, 7.f, 1.f, 0);
            boltDir = normalize(vec3(cosf(ang) * dist, sinf(ang) * dist, cloudBase * 0.6f));
        }
        lastLightning = fl;
    }

    void buildBolt(vec3 a, vec3 b, float width, float bright, int depth) {
        // midpoint displacement: jagged main channel with a few branches
        std::vector<vec3> pts = {a, b};
        for (int it = 0; it < 6; it++) {
            std::vector<vec3> np;
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                vec3 p0 = pts[i], p1 = pts[i + 1];
                float len = length(p1 - p0);
                vec3 mid = (p0 + p1) * 0.5f + vec3(rnd() - 0.5f, rnd() - 0.5f, (rnd() - 0.5f) * 0.3f) * (len * 0.35f);
                np.push_back(p0);
                np.push_back(mid);
            }
            np.push_back(pts.back());
            pts.swap(np);
        }
        for (size_t i = 0; i + 1 < pts.size() && (int)boltSegs.size() < kMaxBoltSegments * 2; i++) {
            boltSegs.push_back(vec4(pts[i], width));
            boltSegs.push_back(vec4(pts[i + 1], bright));
            if (depth < 2 && rnd() < 0.06f) {
                vec3 dir = normalize(pts[i + 1] - pts[i]);
                vec3 side = normalize(vec3(rnd() - 0.5f, rnd() - 0.5f, -0.2f));
                vec3 end = pts[i] + normalize(dir + side) * (150.f + rnd() * 350.f);
                buildBolt(pts[i], end, width * 0.5f, bright * 0.45f, depth + 1);
            }
        }
    }

    // Forward pass (after particles, before TAA): rain streaks, splashes and the lightning bolt.
    void draw(Renderer& r, const Environment& env, ID3D11RenderTargetView* reactive) {
        bool rain = env.rain > 0.01f;
        bool bolt = env.lightning > 0.02f && !boltSegs.empty();
        if (!rain && !bolt) return;
        auto* c = gfx::ctx;
        static const int drops[4] = {9000, 16000, 26000, 40000};
        int q = Clamp(r.settings.rainQuality, 0, 3);
        int n = (int)(drops[q] * Saturate(env.rain * 1.2f));
        float wind = 1.f + env.wind * 7.f;
        cb.data.r0 = vec4((float)n, 36.f, 26.f, Saturate(0.4f + env.rain * 0.6f));
        cb.data.r1 = vec4(env.windDir.x * wind, env.windDir.y * wind, 8.5f + env.rain * 1.5f, 1.f / 45.f);
        // lights for the drops: brightest nearby lights of this frame
        const std::vector<LightGPU>& lf = r.lightsFrame;
        std::vector<std::pair<float, int>> best;
        for (int i = 0; i < (int)lf.size(); i++) {
            float d = length(lf[(size_t)i].pos);
            if (d > 60.f) continue;
            float w = (lf[(size_t)i].color.x + lf[(size_t)i].color.y + lf[(size_t)i].color.z) / Max(d * d, 4.f);
            best.push_back({-w, i});
        }
        std::sort(best.begin(), best.end());
        int nl = Min((int)best.size(), 16);
        for (int i = 0; i < nl; i++) {
            const LightGPU& L = lf[(size_t)best[(size_t)i].second];
            cb.data.lights[i * 2] = vec4(L.pos, L.radius);
            cb.data.lights[i * 2 + 1] = vec4(L.color, 0);
        }
        cb.data.r2 = vec4(4.f, 22.f, (float)nl, Saturate(env.lightning));
        cb.data.r3 = vec4(overheadTop, overheadRange, 0, 0);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->VSSetConstantBuffers(0, 2, cbs);
        c->PSSetConstantBuffers(0, 2, cbs);
        ID3D11RenderTargetView* rts[2] = {r.hdr.rtv, reactive};
        c->OMSetRenderTargets(2, rts, r.depthRO);
        gfx::setViewport((float)r.width, (float)r.height);
        c->OMSetDepthStencilState(gfx::states.depthGreaterEqualNoWrite, 0);
        float bf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(blend, bf, 0xffffffff);
        c->RSSetState(gfx::states.cullNone);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        if (rain && n > 0) {
            c->VSSetShader(vsRain.vs, nullptr, 0);
            c->PSSetShader(psRain, nullptr, 0);
            c->DrawInstanced(4, (UINT)n, 0, 0);
            ID3D11ShaderResourceView* atlas = r.particles->atlas.srv;
            c->PSSetShaderResources(1, 1, &atlas);
            c->VSSetShader(vsSplash.vs, nullptr, 0);
            c->PSSetShader(psSplash, nullptr, 0);
            c->DrawInstanced(4, (UINT)n, 0, 0);
            r.stats.drawCalls += 2;
        }
        if (bolt) {
            // bolt segments relative to the camera
            std::vector<vec4> segs(boltSegs.size());
            vec3 o = rel(boltOrigin, r.camera.pos);
            for (size_t i = 0; i < boltSegs.size(); i++) segs[i] = vec4(boltSegs[i].xyz() + o, boltSegs[i].w);
            gfx::updateBuffer(boltBuf, segs.data(), (u32)(segs.size() * 16));
            c->VSSetShaderResources(2, 1, &boltBuf.srv);
            c->VSSetShader(vsBolt.vs, nullptr, 0);
            c->PSSetShader(psBolt, nullptr, 0);
            c->DrawInstanced(4, (UINT)(segs.size() / 2), 0, 0);
            r.stats.drawCalls++;
        }
        c->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* nulls[3] = {};
        c->VSSetShaderResources(0, 3, nulls);
        c->PSSetShaderResources(0, 3, nulls);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullBack);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
};

}  // namespace Render
