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
    bool valid = false;            // front cube holds a complete capture
    dvec3 cyclePos, frontPos;
    gfx::CBuffer<FrameConstants> frameCB;
    gfx::CBuffer<ProbeCBData> cb;
    ID3D11PixelShader* psLight = nullptr;
    ID3D11ComputeShader *csPrefilter = nullptr, *csSH = nullptr;
    gfx::Buffer shBuf;       // SH9 irradiance of the probe (one-bounce ambient around the camera)
    bool shValid = false;
    static constexpr float kNear = 0.5f;

    void init() {
        psLight = gfx::loadPS("envprobe.hlsl", "psProbeLight");
        csPrefilter = gfx::loadCS("envprobe.hlsl", "csPrefilter");
        csSH = gfx::loadCS("envprobe.hlsl", "csProbeSH");
        shBuf = gfx::createBuffer(9 * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_UAV);
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
        capture = createTexture2D(res, res, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_RTV | TEX_CUBE | TEX_GENMIPS | TEX_SLICE_RTVS, 0, 6);
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
        ID3D11Buffer* cbs[] = {frameCB.get()};
        c->VSSetConstantBuffers(0, 1, cbs);
        c->PSSetConstantBuffers(0, 1, cbs);
        // G-buffer of the face (terrain + static world; props and dynamic objects are skipped)
        float clear0[4] = {0, 0, 0, 0};
        c->ClearRenderTargetView(gAlbedo.rtv, clear0);
        c->ClearRenderTargetView(gNormal.rtv, clear0);
        c->ClearRenderTargetView(gMaterial.rtv, clear0);
        c->ClearRenderTargetView(gEmissive.rtv, clear0);
        c->ClearDepthStencilView(gDepth.dsv, D3D11_CLEAR_DEPTH, 0.f, 0);
        ID3D11RenderTargetView* rts[5] = {gAlbedo.rtv, gNormal.rtv, gMaterial.rtv, gEmissive.rtv, gVelocity.rtv};
        c->OMSetRenderTargets(5, rts, gDepth.dsv);
        gfx::setViewport((float)res, (float)res);
        c->OMSetDepthStencilState(gfx::states.depthGreaterWrite, 0);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullBack);
        r.terrain->drawGBufferVP(r, vp, cyclePos, 2.5f);
        r.world->drawGBufferVP(r, vp, cyclePos, true);
        c->OMSetRenderTargets(0, nullptr, nullptr);
        // Lighting into the capture cube face
        cb.data.p0 = vec4((float)face, (float)res, 0, 0);
        cb.data.p2 = vec4(rel(cyclePos, r.camera.pos), 0);  // probe-relative -> camera-relative (shadow cascades)
        cb.upload();
        ID3D11Buffer* pcbs[4] = {frameCB.get(), r.clouds->cb.get(), cb.get(), r.shadowCB.get()};
        c->PSSetConstantBuffers(0, 4, pcbs);
        ID3D11ShaderResourceView* srvs[8] = {r.clouds->shape.srv, r.clouds->detail.srv, r.clouds->weather.srv,
                                             gAlbedo.srv, gNormal.srv, gMaterial.srv, gEmissive.srv, gDepth.srv};
        c->PSSetShaderResources(0, 8, srvs);
        c->OMSetRenderTargets(1, &capture.sliceRtvs[face], nullptr);
        c->OMSetDepthStencilState(gfx::states.depthOff, 0);
        c->RSSetState(gfx::states.cullNone);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(r.vsFullscreen.vs, nullptr, 0);
        c->PSSetShader(psLight, nullptr, 0);
        c->Draw(3, 0);
        c->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* nulls[8] = {};
        c->PSSetShaderResources(0, 8, nulls);
        c->RSSetState(gfx::states.cullBack);
        r.bindFrame();
    }

    void prefilter(Renderer& r) {
        auto* c = gfx::ctx;
        c->GenerateMips(capture.srv);
        gfx::Texture& dst = filtered[front ^ 1];
        // mip 0: straight copy of the capture
        for (int f = 0; f < 6; f++)
            c->CopySubresourceRegion(dst.res, D3D11CalcSubresource(0, f, dst.mips), 0, 0, 0, capture.res,
                                     D3D11CalcSubresource(0, f, capture.mips), nullptr);
        ID3D11Buffer* cbs[] = {r.frameCB.get(), nullptr, cb.get()};
        c->CSSetConstantBuffers(0, 3, cbs);
        c->CSSetShader(csPrefilter, nullptr, 0);
        c->CSSetShaderResources(8, 1, &capture.srv);
        for (int m = 1; m < mips; m++) {
            int size = Max(1, res >> m);
            float t = (float)m / (float)(mips - 1);
            cb.data.p0 = vec4(0, (float)res, t * t, (float)size);
            cb.data.p1 = vec4((float)capture.mips, (float)res, m <= 2 ? 32.f : 48.f, 0);
            cb.upload();
            c->CSSetUnorderedAccessViews(0, 1, &dst.mipUavs[m], nullptr);
            c->Dispatch(gfx::divUp(size, 8), gfx::divUp(size, 8), 6);
            ID3D11UnorderedAccessView* nu = nullptr;
            c->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
        }
        ID3D11ShaderResourceView* ns = nullptr;
        c->CSSetShaderResources(8, 1, &ns);
        front ^= 1;
        frontPos = cyclePos;
        valid = true;
        // Irradiance SH of the new capture (blended with the previous one to avoid pops while moving)
        cb.data.p0 = vec4(0, (float)res, shValid && !r.cameraCut ? 0.5f : 1.f, 0);
        cb.data.p1 = vec4((float)Min(2, mips - 1), (float)res, 0, 0);
        cb.upload();
        c->CSSetShader(csSH, nullptr, 0);
        c->CSSetShaderResources(44, 1, &ns);  // global binding of the SH buffer (written below)
        c->VSSetShaderResources(44, 1, &ns);
        c->PSSetShaderResources(44, 1, &ns);
        c->CSSetShaderResources(8, 1, &filtered[front].srv);
        c->CSSetUnorderedAccessViews(1, 1, &shBuf.uav, nullptr);
        c->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* nu2 = nullptr;
        c->CSSetUnorderedAccessViews(1, 1, &nu2, nullptr);
        c->CSSetShaderResources(8, 1, &ns);
        shValid = true;
    }

    // One capture step per frame; a full synchronous capture after camera cuts / (re)creation.
    void update(Renderer& r) {
        const Settings& s = r.settings;
        if (!s.envProbe) {
            valid = false;
            shValid = false;
            return;
        }
        int wantRes = Clamp(s.envProbeRes, 32, 512);
        if (wantRes != res) create(wantRes);
        if (r.cameraCut || !valid) {
            cyclePos = r.camera.pos;
            for (int f = 0; f < 6; f++) captureFace(r, f);
            prefilter(r);
            step = 0;
            return;
        }
        if (step == 0) cyclePos = r.camera.pos;
        if (step < 6) captureFace(r, step);
        else prefilter(r);
        step = (step + 1) % 7;
    }

    ID3D11ShaderResourceView* srv() const { return valid ? filtered[front].srv : nullptr; }
    ID3D11ShaderResourceView* shSrv() const { return shValid ? shBuf.srv : nullptr; }
};

}  // namespace Render
