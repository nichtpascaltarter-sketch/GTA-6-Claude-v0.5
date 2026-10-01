// Weather rendering: overhead height map of the static world (rain occlusion, dry areas under roofs, grass
// placement), rain streaks + splashes, lightning bolts and the weather frame parameters. Included from
// renderer.cpp.
namespace Render {

struct RainCBData {
    vec4 r0, r1, r2, r3;
    vec4 lights[48];   // 16 lights: (pos, radius), (color, spotCos), (dir, spotInner)
};

struct WeatherSystem {
    // Overhead map
    static constexpr int kOverheadRes = 1024;
    static constexpr float kOverheadSize = 192.f;
    gfx::Texture overheadDepth, overheadHeight, overheadGrass;
    gfx::VertexShader vsOverhead;
    gfx::PixelShader  psOverhead = nullptr;
    gfx::CBuffer<ShadowPassCBData> passCB;
    dvec3 overheadOrigin;          // world min corner of the map
    float overheadTop = 0.f, overheadRange = 600.f;
    u32 overheadUploads = 0;       // WorldRenderer::nearUploads when the map was rendered
    float gentleFlash = 0.f;       // reduce-flashing: smoothed lightning intensity
    bool overheadValid = false;
    int overheadAge = 0;
    gfx::ComputeShader  csOverhead = nullptr;
    // Rain + lightning
    gfx::CBuffer<RainCBData> cb;
    gfx::VertexShader vsRain, vsSplash, vsBolt;
    gfx::PixelShader psRain = nullptr, psSplash = nullptr, psBolt = nullptr;
    gfx::BlendState  blend = nullptr;
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
        gfx::InputElement layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, gfx::PER_VERTEX, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, gfx::PER_VERTEX, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, gfx::PER_VERTEX, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, gfx::PER_VERTEX, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, gfx::PER_VERTEX, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, gfx::PER_VERTEX, 0},
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
        gfx::BlendDesc bd;
        bd.independent = true;
        bd.rt[0] = {true, gfx::BLEND_ONE, gfx::BLEND_INV_SRC_ALPHA, gfx::BLENDOP_ADD, gfx::BLEND_ONE, gfx::BLEND_INV_SRC_ALPHA,
                    gfx::BLENDOP_ADD, gfx::WRITE_ALL};
        bd.rt[1] = {true, gfx::BLEND_ONE, gfx::BLEND_ONE, gfx::BLENDOP_MAX, gfx::BLEND_ONE, gfx::BLEND_ONE, gfx::BLENDOP_MAX,
                    gfx::WRITE_ALL};
        blend = gfx::createBlendState(bd);
    }

    // Top-down depth of the static world cells around the camera (re-rendered when the camera moves a grid step
    // or periodically to pick up streamed cells).
    void updateOverhead(Renderer& r) {
        dvec3 cam = r.camera.pos;
        const double step = 16.0;
        dvec3 center(floor(cam.x / step) * step + step * 0.5, floor(cam.y / step) * step + step * 0.5, cam.z);
        dvec3 origin(center.x - kOverheadSize * 0.5, center.y - kOverheadSize * 0.5, 0.0);
        overheadAge++;
        // re-rendered when the camera moves to another 16 m cell, when near world cells finished streaming in
        // (roads / lawns / roofs appearing) and every 30 frames for anything else that changed
        if (overheadValid && origin.x == overheadOrigin.x && origin.y == overheadOrigin.y && overheadAge < 30 &&
            fabs(cam.z + 300.0 - overheadTop) < 150.0 && r.world->nearUploads == overheadUploads)
            return;
        overheadAge = 0;
        overheadUploads = r.world->nearUploads;
        overheadOrigin = origin;
        overheadTop = (float)(cam.z + 300.0);
        auto* c = gfx::ctx;
        // camera-relative ortho view looking straight down from overheadTop
        vec3 eye((float)(center.x - cam.x), (float)(center.y - cam.y), overheadTop - (float)cam.z);
        mat4 view = mat4Translation(-eye);
        float hs = kOverheadSize * 0.5f;
        mat4 proj = orthoRH(-hs, hs, -hs, hs, 0.f, overheadRange);
        mat4 vp = proj * view;
        gfx::SRV  nullSrv = nullptr;
        c->vsSetSRVs(41, 1, &nullSrv);
        c->psSetSRVs(41, 1, &nullSrv);
        c->csSetSRVs(41, 1, &nullSrv);
        float zero4[4] = {0, 0, 0, 0};
        c->clearDepth(overheadDepth.dsv, 1.f);
        c->clearRTV(overheadGrass.rtv, zero4);
        c->setRenderTargets(1, &overheadGrass.rtv, overheadDepth.dsv);
        gfx::setViewport((float)kOverheadRes, (float)kOverheadRes);
        c->setDepthState(gfx::states.depthLessWrite);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullNone);
        passCB.data.viewProj = vp;
        passCB.upload();
        gfx::Resource  cbs[] = {r.world->drawCB.get(), passCB.get()};
        c->vsSetCBs(1, 2, cbs);
        c->setInputLayout(vsOverhead.layout);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        c->setVS(vsOverhead.vs);
        c->setPS(psOverhead);
        Frustum fr;
        fr.fromMatrix(vp);
        r.world->forVisible(fr, r.camera.pos, false, [&](StreamCell* sc, vec3 off) {
            if (sc->lod != 0 || !sc->opaqueCount) return;
            r.world->drawCB.data.cellOffset = vec4(off, 0);
            r.world->drawCB.data.params = vec4(0);
            r.world->drawCB.upload();
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->setVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->setIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            c->drawIndexed(sc->opaqueCount, 0, 0);
            r.stats.drawCalls++;
        });
        c->setRenderTargets(0, nullptr, nullptr);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullBack);
        // depth -> height
        cb.data.r3 = vec4(overheadTop, overheadRange, 0, 0);
        cb.upload();
        gfx::Resource  ccbs[] = {r.frameCB.get(), cb.get()};
        c->csSetCBs(0, 2, ccbs);
        c->csSetSRVs(0, 1, &overheadDepth.srv);
        c->csSetUAVs(0, 1, &overheadHeight.uav);
        c->setCS(csOverhead);
        c->dispatch(kOverheadRes / 8, kOverheadRes / 8, 1);
        gfx::unbindCSResources(1, 1);
        overheadValid = true;
    }

    gfx::SRV  overheadSrv() const { return overheadValid ? overheadHeight.srv : nullptr; }

    // Weather-related frame constants.
    void setFrameParams(Renderer& r, const Environment& env, FrameConstants& f, float dt) {
        rippleTime += dt * (0.6f + env.rain * 0.8f);
        if (rippleTime > 1000.f) rippleTime -= 1000.f;
        float cover = Saturate(env.cloudCover + env.rain * 0.5f);
        float overcast = SmoothStep(0.55f, 1.f, cover);
        float storm = Saturate((env.rain - 0.5f) * 2.f);
        float puddles = Saturate(env.wetness * 1.2f - 0.15f);
        f.weather2 = vec4(overcast, storm, puddles, rippleTime);
        f.cloudShadow.w = Lerp(0.88f, 1.f, Max(overcast, storm));   // how much of the cloud shadow map applies
        f.overhead = overheadValid ? vec4((float)overheadOrigin.x, (float)overheadOrigin.y, kOverheadSize, 1.f) : vec4(0.f);
        // Lightning: flash brightness + direction to the current bolt (reduce-flashing: ~30% and a slow fade)
        float flash = Saturate(env.lightning);
        if (r.settings.reduceFlashing) {
            gentleFlash = Max(flash * 0.3f, gentleFlash * expf(-dt * 1.2f));
            flash = gentleFlash;
        }
        f.lightning = vec4(flash, boltDir.x, boltDir.y, boltDir.z);
        f.ambientParams.y = flash * 2500.f;
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
    void draw(Renderer& r, const Environment& env, gfx::RTV  reactive) {
        bool rain = env.rain > 0.01f;
        bool bolt = r.frame.lightning.x > 0.02f && !boltSegs.empty();
        if (!rain && !bolt) return;
        auto* c = gfx::ctx;
        static const int drops[4] = {12000, 22000, 36000, 52000};
        int q = Clamp(r.settings.rainQuality, 0, 3);
        int n = (int)(drops[q] * Saturate(env.rain * 1.2f));
        float wind = 1.f + env.wind * 7.f;
        cb.data.r0 = vec4((float)n, 36.f, 26.f, Saturate(0.4f + env.rain * 0.6f));
        cb.data.r1 = vec4(env.windDir.x * wind, env.windDir.y * wind, 8.5f + env.rain * 1.5f, 1.f / 26.f);   // streaks ~0.4 m (1/26 s shutter)
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
            cb.data.lights[i * 3] = vec4(L.pos, L.radius);
            cb.data.lights[i * 3 + 1] = vec4(L.color, L.spotCos);
            cb.data.lights[i * 3 + 2] = vec4(L.dir, L.spotInner);
        }
        cb.data.r2 = vec4(4.f, 22.f, (float)nl, Saturate(r.frame.lightning.x));
        cb.data.r3 = vec4(overheadTop, overheadRange, 0, 0);
        cb.upload();
        gfx::Resource  cbs[] = {r.frameCB.get(), cb.get()};
        c->vsSetCBs(0, 2, cbs);
        c->psSetCBs(0, 2, cbs);
        gfx::RTV  rts[2] = {r.hdr.rtv, reactive};
        c->setRenderTargets(2, rts, r.depthRO);
        gfx::setViewport((float)r.width, (float)r.height);
        c->setDepthState(gfx::states.depthGreaterEqualNoWrite);
        float bf[4] = {0, 0, 0, 0};
        c->setBlendState(blend);
        c->setRasterState(gfx::states.cullNone);
        c->setInputLayout(nullptr);
        c->setTopology(gfx::TOPO_TRIANGLE_STRIP);
        if (rain && n > 0) {
            c->setVS(vsRain.vs);
            c->setPS(psRain);
            c->drawInstanced(4, (UINT)n, 0, 0);
            gfx::SRV  atlas = r.particles->atlas.srv;
            c->psSetSRVs(1, 1, &atlas);
            c->setVS(vsSplash.vs);
            c->setPS(psSplash);
            c->drawInstanced(4, (UINT)n, 0, 0);
            r.stats.drawCalls += 2;
        }
        if (bolt) {
            // bolt segments relative to the camera
            std::vector<vec4> segs(boltSegs.size());
            vec3 o = rel(boltOrigin, r.camera.pos);
            for (size_t i = 0; i < boltSegs.size(); i++) segs[i] = vec4(boltSegs[i].xyz() + o, boltSegs[i].w);
            gfx::updateBuffer(boltBuf, segs.data(), (u32)(segs.size() * 16));
            c->vsSetSRVs(2, 1, &boltBuf.srv);
            c->setVS(vsBolt.vs);
            c->setPS(psBolt);
            c->drawInstanced(4, (UINT)(segs.size() / 2), 0, 0);
            r.stats.drawCalls++;
        }
        c->setRenderTargets(0, nullptr, nullptr);
        gfx::SRV  nulls[3] = {};
        c->vsSetSRVs(0, 3, nulls);
        c->psSetSRVs(0, 3, nulls);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullBack);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
    }
};

}  // namespace Render
