// Dynamic environment probe: a small cubemap of the world/sky around the camera, captured one face per frame
// (small G-buffer with the regular material shaders, then a lighting pass) and GGX-prefiltered into roughness
// mips every 7th frame. Used when screen-space reflection rays miss. Included from renderer.cpp.
namespace Render {

struct ProbeCBData {
    vec4 p0, p1, p2;
};

struct EnvProbeSystem {
    int res = 0, mips = 6;
    gfx::Texture capture;          // lit capture (cube, full mip chain for filtered importance sampling)
    gfx::Texture filtered[2];      // prefiltered cubes: [front] is sampled by the lighting, [front ^ 1] is being built
    gfx::Texture gAlbedo, gNormal, gMaterial, gEmissive, gVelocity, gDepth;
    int front = 0;
    int step = 0;                  // 0..5 capture face, 6 prefilter + swap
    int stepWait = 0;              // frames since the last capture step (Settings::envProbeInterval)
    bool valid = false;            // front cube holds a complete capture
    dvec3 cyclePos, frontPos;
    gfx::CBuffer<FrameConstants> frameCB;
    gfx::CBuffer<ProbeCBData> cb;
    gfx::PixelShader  psLight = nullptr;
    gfx::ComputeShader csPrefilter = nullptr, csSH = nullptr, csDown = nullptr;
    gfx::Buffer shBuf;       // SH9 irradiance of the probe (one-bounce ambient around the camera)
    bool shValid = false;
    static constexpr float kNear = 0.5f;
    static constexpr int kMaxProbeLights = 128;
    std::vector<LightGPU> lights, gathered;   // outdoor world lights around the capture point (relative to cyclePos)
    gfx::Buffer lightBuf;

    void init() {
        psLight = gfx::loadPS("envprobe.hlsl", "psProbeLight");
        csPrefilter = gfx::loadCS("envprobe.hlsl", "csPrefilter");
        csSH = gfx::loadCS("envprobe.hlsl", "csProbeSH");
        csDown = gfx::loadCS("envprobe.hlsl", "csCubeDown");
        float zeroSH[9 * 4] = {};   // defined contents: the SH is blended over time from its previous value
        shBuf = gfx::createBuffer(9 * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_UAV, zeroSH);
        lightBuf = gfx::createBuffer(kMaxProbeLights * sizeof(LightGPU), sizeof(LightGPU), gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        frameCB.create();
        cb.create();
    }

    void release() {
        capture.release();
        for (auto& f : filtered) f.release();
        gAlbedo.release(); gNormal.release(); gMaterial.release(); gEmissive.release(); gVelocity.release(); gDepth.release();
    }

    void create(int resolution) {
        release();
        res = resolution;
        mips = Min(6, gfx::mipCount(res, res) - 2);
        using namespace gfx;
        capture = createTexture2D(res, res, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_RTV | TEX_UAV | TEX_CUBE | TEX_MIP_UAVS | TEX_SLICE_RTVS, 0, 6);
        for (auto& f : filtered) f = createTexture2D(res, res, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_UAV | TEX_CUBE | TEX_MIP_UAVS, mips, 6);
        gAlbedo = createTexture2D(res, res, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, TEX_RTV | TEX_SRV);
        gNormal = createTexture2D(res, res, DXGI_FORMAT_R16G16_UNORM, TEX_RTV | TEX_SRV);
        gMaterial = createTexture2D(res, res, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_RTV | TEX_SRV);
        gEmissive = createTexture2D(res, res, DXGI_FORMAT_R11G11B10_FLOAT, TEX_RTV | TEX_SRV);
        gVelocity = createTexture2D(res, res, DXGI_FORMAT_R16G16_FLOAT, TEX_RTV);
        gDepth = createTexture2D(res, res, DXGI_FORMAT_R32_TYPELESS, TEX_DSV | TEX_SRV);
        valid = false;
        step = 0;
    }

    // Face basis in the D3D cube convention (see envprobe.hlsl kFaceF/R/U).
    static void faceBasis(int f, vec3& F, vec3& R, vec3& U) {
        static const vec3 kF[6] = {vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1)};
        static const vec3 kR[6] = {vec3(0, 0, -1), vec3(0, 0, 1), vec3(1, 0, 0), vec3(1, 0, 0), vec3(1, 0, 0), vec3(-1, 0, 0)};
        static const vec3 kU[6] = {vec3(0, 1, 0), vec3(0, 1, 0), vec3(0, 0, -1), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 0)};
        F = kF[f];
        R = kR[f];
        U = kU[f];
    }

    // Street lamps, neon and building lights around the capture point, nearest first. Lamps inside enterable
    // interiors are left out (the capture does not see the rooms, their light must not leak onto the street).
    void gatherLights(Renderer& r, const Environment& env) {
        lights.clear();
        if (!r.world || r.nightFactor <= 0.f) return;
        Frustum all;
        for (vec4& pl : all.planes) pl = vec4(0.f, 0.f, 0.f, 1e30f);
        gathered.clear();
        r.world->gatherLights(cyclePos, r.nightFactor, env.gameSeconds, env.timeOfDay, all, gathered, Renderer::kMaxLights);
        for (const LightGPU& g : gathered) {
            bool inside = false;
            for (const InteriorVolume& v : r.interiorVolumes) {
                vec3 d = g.pos - rel(v.center, cyclePos);
                float lx = d.x * v.axis.x + d.y * v.axis.y, ly = -d.x * v.axis.y + d.y * v.axis.x;
                if (fabsf(lx) <= v.halfExtents.x + 0.3f && fabsf(ly) <= v.halfExtents.y + 0.3f && fabsf(d.z) <= v.halfExtents.z + 0.3f) {
                    inside = true;
                    break;
                }
            }
            if (!inside) lights.push_back(g);
        }
        auto nearFirst = [](const LightGPU& a, const LightGPU& b) { return length(a.pos) - a.radius < length(b.pos) - b.radius; };
        if ((int)lights.size() > kMaxProbeLights) {
            std::nth_element(lights.begin(), lights.begin() + kMaxProbeLights, lights.end(), nearFirst);
            lights.resize(kMaxProbeLights);
        }
        if (!lights.empty()) gfx::updateBuffer(lightBuf, lights.data(), (u32)(lights.size() * sizeof(LightGPU)));
    }

    void captureFace(Renderer& r, int face) {
        auto* c = gfx::ctx;
        vec3 F, R, U;
        faceBasis(face, F, R, U);
        // Right-handed view (x = -R): the image is mirrored horizontally relative to the cube face; the lighting
        // pass reads it back mirrored.
        vec3 X = -R, Y = U, Z = -F;
        mat4 view(vec4(X.x, Y.x, Z.x, 0), vec4(X.y, Y.y, Z.y, 0), vec4(X.z, Y.z, Z.z, 0), vec4(0, 0, 0, 1));
        mat4 proj = perspectiveReversedInfRH(kPi * 0.5f, 1.f, kNear);
        // All probe geometry is expressed relative to the capture position (cyclePos)
        mat4 vp = proj * view;
        FrameConstants f = r.frame;
        f.viewProj = vp;
        f.viewProjNoJitter = vp;
        f.invViewProj = inverse(vp);
        f.view = view;
        f.proj = proj;
        f.prevViewProj = vp;
        f.camForward = vec4(F, kPi * 0.5f);
        f.screen = vec4((float)res, (float)res, 1.f / res, 1.f / res);
        f.jitter = vec4(0);
        f.renderParams.y = 0.f;
        f.renderParams.w = 0.f;
        // Material shaders use gCamPos for world-space texturing and interior mapping: probe position
        f.camPos = vec4((float)cyclePos.x, (float)cyclePos.y, (float)cyclePos.z, kNear);
        f.camPosWrap = vec4((float)fmod(cyclePos.x, 2048.0), (float)fmod(cyclePos.y, 2048.0), (float)fmod(cyclePos.z, 2048.0), 0);
        frameCB.data = f;
        frameCB.upload();
        gfx::Resource  cbs[] = {frameCB.get()};
        c->vsSetCBs(0, 1, cbs);
        c->psSetCBs(0, 1, cbs);
        // G-buffer of the face (terrain + static world; props and dynamic objects are skipped)
        float clear0[4] = {0, 0, 0, 0};
        c->clearRTV(gAlbedo.rtv, clear0);
        c->clearRTV(gNormal.rtv, clear0);
        c->clearRTV(gMaterial.rtv, clear0);
        c->clearRTV(gEmissive.rtv, clear0);
        c->clearDepth(gDepth.dsv, 0.f);
        gfx::RTV  rts[5] = {gAlbedo.rtv, gNormal.rtv, gMaterial.rtv, gEmissive.rtv, gVelocity.rtv};
        c->setRenderTargets(5, rts, gDepth.dsv);
        gfx::setViewport((float)res, (float)res);
        c->setDepthState(gfx::states.depthGreaterWrite);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullBack);
        r.terrain->drawGBufferVP(r, vp, cyclePos, 2.5f);
        r.world->drawGBufferVP(r, vp, cyclePos, true);
        c->setRenderTargets(0, nullptr, nullptr);
        // Lighting into the capture cube face
        cb.data.p0 = vec4((float)face, (float)res, 0, 0);
        cb.data.p1.w = (float)lights.size();
        cb.data.p2 = vec4(rel(cyclePos, r.camera.pos), 0);  // probe-relative -> camera-relative (shadow cascades)
        cb.upload();
        gfx::Resource  pcbs[4] = {frameCB.get(), r.clouds->cb.get(), cb.get(), r.shadowCB.get()};
        c->psSetCBs(0, 4, pcbs);
        gfx::SRV  srvs[10] = {r.clouds->shape.srv, r.clouds->detail.srv, r.clouds->weather.srv,
                                              gAlbedo.srv, gNormal.srv, gMaterial.srv, gEmissive.srv, gDepth.srv, nullptr, lightBuf.srv};
        c->psSetSRVs(0, 10, srvs);
        c->setRenderTargets(1, &capture.sliceRtvs[face], nullptr);
        c->setDepthState(gfx::states.depthOff);
        c->setRasterState(gfx::states.cullNone);
        c->setInputLayout(nullptr);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        c->setVS(r.vsFullscreen.vs);
        c->setPS(psLight);
        c->draw(3, 0);
        c->setRenderTargets(0, nullptr, nullptr);
        gfx::SRV  nulls[10] = {};
        c->psSetSRVs(0, 10, nulls);
        c->setRasterState(gfx::states.cullBack);
        r.bindFrame();
    }

    void prefilter(Renderer& r) {
        auto* c = gfx::ctx;
        // capture mip chain: 2x2 box per face and level
        gfx::Resource  dcbs[] = {r.frameCB.get(), nullptr, cb.get()};
        c->csSetCBs(0, 3, dcbs);
        c->setCS(csDown);
        for (int m = 1; m < capture.mips; m++) {
            int size = Max(1, res >> m);
            cb.data.p0 = vec4(0, (float)res, 0, (float)size);
            cb.upload();
            c->csSetSRVs(10, 1, &capture.mipSrvs[m - 1]);
            c->csSetUAVs(0, 1, &capture.mipUavs[m]);
            c->dispatch(gfx::divUp(size, 8), gfx::divUp(size, 8), 6);
            gfx::UAV  nu = nullptr;
            c->csSetUAVs(0, 1, &nu);
            gfx::SRV  ns = nullptr;
            c->csSetSRVs(10, 1, &ns);
        }
        gfx::Texture& dst = filtered[front ^ 1];
        // mip 0: straight copy of the capture
        for (int f = 0; f < 6; f++)
            c->copySubresource(dst.res, gfx::subresource(0, f, dst.mips), capture.res, gfx::subresource(0, f, capture.mips));
        gfx::Resource  cbs[] = {r.frameCB.get(), nullptr, cb.get()};
        c->csSetCBs(0, 3, cbs);
        c->setCS(csPrefilter);
        c->csSetSRVs(8, 1, &capture.srv);
        for (int m = 1; m < mips; m++) {
            int size = Max(1, res >> m);
            float t = (float)m / (float)(mips - 1);
            cb.data.p0 = vec4(0, (float)res, t * t, (float)size);
            cb.data.p1 = vec4((float)capture.mips, (float)res, m <= 2 ? 32.f : 48.f, 0);
            cb.upload();
            c->csSetUAVs(0, 1, &dst.mipUavs[m]);
            c->dispatch(gfx::divUp(size, 8), gfx::divUp(size, 8), 6);
            gfx::UAV  nu = nullptr;
            c->csSetUAVs(0, 1, &nu);
        }
        gfx::SRV  ns = nullptr;
        c->csSetSRVs(8, 1, &ns);
        front ^= 1;
        frontPos = cyclePos;
        valid = true;
        // Irradiance SH of the new capture (blended with the previous one to avoid pops while moving)
        cb.data.p0 = vec4(0, (float)res, shValid && !r.cameraCut ? 0.5f : 1.f, 0);
        cb.data.p1 = vec4((float)Min(2, mips - 1), (float)res, 0, 0);
        cb.upload();
        c->setCS(csSH);
        c->csSetSRVs(44, 1, &ns);  // global binding of the SH buffer (written below)
        c->vsSetSRVs(44, 1, &ns);
        c->psSetSRVs(44, 1, &ns);
        c->csSetSRVs(8, 1, &filtered[front].srv);
        c->csSetUAVs(1, 1, &shBuf.uav);
        c->dispatch(1, 1, 1);
        gfx::UAV  nu2 = nullptr;
        c->csSetUAVs(1, 1, &nu2);
        c->csSetSRVs(8, 1, &ns);
        shValid = true;
    }

    // One capture step per frame; a full synchronous capture after camera cuts / (re)creation.
    void update(Renderer& r, const Environment& env) {
        const Settings& s = r.settings;
        if (!s.envProbe) {
            valid = false;
            shValid = false;
            return;
        }
        int wantRes = Clamp(s.envProbeRes, 32, 512);
        if (wantRes != res) create(wantRes);
        // full capture after cuts, on creation, and whenever the camera has left the captured surroundings
        if (r.cameraCut || !valid || length(rel(r.camera.pos, frontPos)) > 150.0f) {
            cyclePos = r.camera.pos;
            gatherLights(r, env);
            for (int f = 0; f < 6; f++) captureFace(r, f);
            prefilter(r);
            step = 0;
            return;
        }
        if (++stepWait < Clamp(s.envProbeInterval, 1, 8)) return;
        stepWait = 0;
        if (step == 0) {
            cyclePos = r.camera.pos;
            gatherLights(r, env);
        }
        if (step < 6) captureFace(r, step);
        else prefilter(r);
        step = (step + 1) % 7;
    }

    gfx::SRV  srv() const { return valid ? filtered[front].srv : nullptr; }
    gfx::SRV  shSrv() const { return shValid ? shBuf.srv : nullptr; }
};

}  // namespace Render
