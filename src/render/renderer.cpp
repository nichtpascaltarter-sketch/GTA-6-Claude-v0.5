#include "renderer.h"
#include "../world/worldmap.h"
#include "../platform/platform.h"

namespace Render {

Renderer* gRenderer = nullptr;

mat4 Camera::viewRel() const {
    vec3 f = forward();
    vec3 up(0, 0, 1);
    if (fabsf(roll) > 1e-5f) {
        vec3 r = normalize(cross(f, up));
        vec3 u = cross(r, f);
        up = normalize(u * cosf(roll) + r * sinf(roll));
    }
    return lookAtRH(vec3(0, 0, 0), f, up);
}

// ------------------------------------------------------------------------------------------------
// Subsystems
struct SkySystem {
    gfx::Texture transmittance, multiScatter, skyView, aerial;
    gfx::Buffer shBuf;
    ID3D11ComputeShader *csTrans = nullptr, *csMulti = nullptr, *csView = nullptr, *csAerial = nullptr, *csSH = nullptr;
    float lastHaze = -1.f;

    void init() {
        transmittance = gfx::createTexture2D(256, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        multiScatter = gfx::createTexture2D(32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        skyView = gfx::createTexture2D(192, 108, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV);
        aerial = gfx::createTexture3D(32, 32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT, gfx::TEX_SRV | gfx::TEX_UAV, 1);
        shBuf = gfx::createBuffer(9 * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_UAV);
        csTrans = gfx::loadCS("sky.hlsl", "csTransmittance");
        csMulti = gfx::loadCS("sky.hlsl", "csMultiScatter");
        csView = gfx::loadCS("sky.hlsl", "csSkyView");
        csAerial = gfx::loadCS("sky.hlsl", "csAerialPerspective");
        csSH = gfx::loadCS("sky.hlsl", "csSkySH");
    }

    void update(Renderer& r, float haze) {
        auto* c = gfx::ctx;
        ID3D11Buffer* cbs[] = {r.frameCB.get()};
        c->CSSetConstantBuffers(0, 1, cbs);
        ID3D11SamplerState* samps[] = {gfx::states.pointClamp, gfx::states.linearClamp};
        c->CSSetSamplers(0, 2, samps);
        if (fabsf(haze - lastHaze) > 0.02f) {
            lastHaze = haze;
            c->CSSetShader(csTrans, nullptr, 0);
            c->CSSetUnorderedAccessViews(0, 1, &transmittance.uav, nullptr);
            c->Dispatch(gfx::divUp(256, 8), gfx::divUp(64, 8), 1);
            gfx::unbindCSResources(4, 2);
            c->CSSetShader(csMulti, nullptr, 0);
            c->CSSetShaderResources(0, 1, &transmittance.srv);
            c->CSSetUnorderedAccessViews(0, 1, &multiScatter.uav, nullptr);
            c->Dispatch(4, 4, 1);
            gfx::unbindCSResources(4, 2);
        }
        ID3D11ShaderResourceView* srvs[] = {transmittance.srv, multiScatter.srv};
        c->CSSetShaderResources(0, 2, srvs);
        c->CSSetShader(csView, nullptr, 0);
        c->CSSetUnorderedAccessViews(0, 1, &skyView.uav, nullptr);
        c->Dispatch(gfx::divUp(192, 8), gfx::divUp(108, 8), 1);
        ID3D11UnorderedAccessView* nullU = nullptr;
        c->CSSetUnorderedAccessViews(0, 1, &nullU, nullptr);
        c->CSSetShader(csAerial, nullptr, 0);
        c->CSSetUnorderedAccessViews(1, 1, &aerial.uav, nullptr);
        c->Dispatch(8, 8, 8);
        gfx::unbindCSResources(4, 3);
        // SH projection uses global bindings (t33 transmittance, t36 sky view)
        ID3D11ShaderResourceView* g[] = {transmittance.srv};
        c->CSSetShaderResources(33, 1, g);
        c->CSSetShaderResources(36, 1, &skyView.srv);
        ID3D11SamplerState* samps2[] = {gfx::states.pointClamp, gfx::states.linearClamp};
        c->CSSetSamplers(0, 2, samps2);
        c->CSSetShader(csSH, nullptr, 0);
        c->CSSetUnorderedAccessViews(2, 1, &shBuf.uav, nullptr);
        c->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* nulls[3] = {};
        c->CSSetUnorderedAccessViews(0, 3, nulls, nullptr);
    }
};

}  // namespace Render

#include "terrain_render.cpp"
#include "materials.cpp"
#include "world_render.cpp"
#include "props_render.cpp"
#include "dynamic.cpp"
#include "shadows.cpp"
#include "water.cpp"
#include "post.cpp"

namespace UI { gfx::Texture buildSignAtlas(const std::vector<std::string>& names); }

namespace Render {

bool Renderer::init(int w, int h) {
    gRenderer = this;
    outWidth = w;
    outHeight = h;
    frameCB.create();
    shadowCB.create();
    vsFullscreen = gfx::loadVS("post.hlsl", "vsFullscreen", nullptr, 0);
    csLighting = gfx::loadCS("lighting.hlsl", "csLighting");
    lightBuf = gfx::createBuffer(kMaxLights * sizeof(LightGPU), sizeof(LightGPU), gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
    lightCB.create();
    sky = new SkySystem();
    sky->init();
    shadows = new ShadowSystem();
    shadows->init(settings.shadowRes);
    post = new PostSystem();
    post->init();
    terrain = new TerrainRenderer();
    terrain->init();
    materials = new MaterialLibrary();
    materials->init(*terrain);
    world = new WorldRenderer();
    world->init(materials);
    shadows->casters.push_back([this](Renderer& r, const mat4& vp, int cascade) { world->drawShadow(r, vp, cascade); });
    water = new WaterRenderer();
    water->init();
    props = new PropRenderer();
    props->init(materials);
    dynamic = new DynamicRenderer();
    dynamic->init(materials);
    shadows->casters.push_back([this](Renderer& r, const mat4& vp, int cascade) { dynamic->drawShadow(r, vp, cascade); });
    shadows->casters.push_back([this](Renderer& r, const mat4& vp, int cascade) { props->drawShadow(r, world->cells, vp, cascade); });
    createTargets();
    return true;
}

void Renderer::shutdown() {
    releaseTargets();
}

void Renderer::setWorld(World::WorldMap* m) {
    map = m;
    terrain->setMap(m);
    water->setMap(*m, *terrain);
    if (World::gBuildings) {
        world->uploadFacades(*World::gBuildings);
        world->signTex = UI::buildSignAtlas(World::gBuildings->signNames);
    }
}

void Renderer::createTargets() {
    width = Max(64, (int)(outWidth * settings.renderScale));
    height = Max(64, (int)(outHeight * settings.renderScale));
    using namespace gfx;
    depth = createTexture2D(width, height, DXGI_FORMAT_R32_TYPELESS, TEX_DSV | TEX_SRV);
    gbAlbedo = createTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, TEX_RTV | TEX_SRV);
    gbNormal = createTexture2D(width, height, DXGI_FORMAT_R16G16_UNORM, TEX_RTV | TEX_SRV);
    gbMaterial = createTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_RTV | TEX_SRV);
    gbEmissive = createTexture2D(width, height, DXGI_FORMAT_R11G11B10_FLOAT, TEX_RTV | TEX_SRV);
    gbVelocity = createTexture2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TEX_RTV | TEX_SRV);
    hdr = createTexture2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_RTV | TEX_SRV | TEX_UAV);
    hdrCopy = createTexture2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV);
    depthCopy = createTexture2D(width, height, DXGI_FORMAT_R32_TYPELESS, TEX_SRV);
    // Clouds placeholder: fully transparent layer (rgb 0, transmittance 1) until volumetric clouds run
    u16 half1 = 0x3C00;
    u16 cl[4] = {0, 0, 0, half1};
    cloudsTex = createTexture2D(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV, 1, 1, cl, 8);
    post->resize(width, height);
}

void Renderer::releaseTargets() {
    depth.release();
    gbAlbedo.release();
    gbNormal.release();
    gbMaterial.release();
    gbEmissive.release();
    gbVelocity.release();
    hdr.release();
    hdrCopy.release();
    depthCopy.release();
    cloudsTex.release();
}

void Renderer::resize(int w, int h) {
    if (w == outWidth && h == outHeight) return;
    outWidth = w;
    outHeight = h;
    releaseTargets();
    createTargets();
}

static float halton(int i, int b) {
    float f = 1.f, r = 0.f;
    while (i > 0) {
        f /= (float)b;
        r += f * (float)(i % b);
        i /= b;
    }
    return r;
}

void Renderer::computeSunAndSky(const Environment& env) {
    const float lat = 25.8f * kDegToRad;
    float decl = 23.44f * kDegToRad * sinf(kTwoPi * (284.f + env.dayOfYear) / 365.f);
    float hourAngle = (env.timeOfDay - 12.f) * 15.f * kDegToRad;
    vec3 sun(-cosf(decl) * sinf(hourAngle), sinf(decl) * cosf(lat) - cosf(decl) * cosf(hourAngle) * sinf(lat),
             sinf(decl) * sinf(lat) + cosf(decl) * cosf(hourAngle) * cosf(lat));
    sun = normalize(sun);
    // Moon: roughly opposite the sun, offset in declination for variety
    float mh = hourAngle + kPi + 0.35f;
    float md = -decl * 0.6f + 0.2f;
    vec3 moon(-cosf(md) * sinf(mh), sinf(md) * cosf(lat) - cosf(md) * cosf(mh) * sinf(lat),
              sinf(md) * sinf(lat) + cosf(md) * cosf(mh) * cosf(lat));
    moon = normalize(moon);
    float sunElev = asinf(Clamp(sun.z, -1.f, 1.f)) * kRadToDeg;
    const float sunLux = 120000.f;
    if (sunElev > -6.5f) {
        moonLight = false;
        sunDir = sun;
        float fade = SmoothStep(-6.5f, -2.5f, sunElev);
        lightTOA = vec3(1.f, 0.985f, 0.96f) * sunLux * fade;
    } else {
        moonLight = true;
        sunDir = moon.z > 0.05f ? moon : normalize(vec3(moon.x, moon.y, 0.05f));
        float fade = SmoothStep(-6.5f, -10.f, sunElev);
        lightTOA = vec3(0.75f, 0.82f, 1.0f) * 3.0f * fade * SmoothStep(-0.05f, 0.15f, moon.z);
    }
    nightFactor = SmoothStep(-2.f, -9.f, sunElev);
}

void Renderer::updateFrameConstants(const Camera& cam, const Environment& env, float dt) {
    camera = cam;
    float aspect = (float)width / (float)height;
    viewRel = cam.viewRel();
    proj = perspectiveReversedInfRH(cam.fovY, aspect, cam.nearZ);
    prevJitter = jitter;
    if (settings.taa) {
        int idx = (int)(frameIndex % 16) + 1;
        jitter = vec2(halton(idx, 2) - 0.5f, halton(idx, 3) - 0.5f);
    } else {
        jitter = vec2(0, 0);
    }
    projJitter = proj;
    projJitter.c[2].x += jitter.x * 2.f / width;
    projJitter.c[2].y += jitter.y * 2.f / height;
    viewProj = projJitter * viewRel;
    viewProjNoJitter = proj * viewRel;
    if (frameIndex == 0) { prevCamPos = cam.pos; prevViewProjNoJitter = viewProjNoJitter; }
    // Previous view-proj expressed relative to the current camera position
    vec3 delta = rel(cam.pos, prevCamPos);
    mat4 prevRelToCur = prevViewProjNoJitter * mat4Translation(delta);

    computeSunAndSky(env);
    FrameConstants& f = frame;
    f.viewProj = viewProj;
    f.viewProjNoJitter = viewProjNoJitter;
    f.invViewProj = inverse(viewProj);
    f.view = viewRel;
    f.proj = projJitter;
    f.prevViewProj = prevRelToCur;
    f.camPos = vec4((float)cam.pos.x, (float)cam.pos.y, (float)cam.pos.z, cam.nearZ);
    f.camPosWrap = vec4((float)fmod(cam.pos.x, 2048.0), (float)fmod(cam.pos.y, 2048.0), (float)fmod(cam.pos.z, 2048.0), 0);
    f.screen = vec4((float)width, (float)height, 1.f / width, 1.f / height);
    f.jitter = vec4(jitter.x, jitter.y, prevJitter.x, prevJitter.y);
    f.sunDir = vec4(sunDir, sunDir.z > -0.02f ? 1.f : 0.f);
    f.sunColor = vec4(lightTOA, moonLight ? 1.f : 0.f);
    for (int i = 0; i < 9; i++) f.skyAmbient[i] = vec4(0);
    f.time = vec4(env.gameSeconds, env.timeOfDay, (float)(frameIndex % 1024), dt);
    f.weather = vec4(env.rain, env.wetness, env.cloudCover, env.wind);
    f.wind = vec4(env.windDir.x, env.windDir.y, 0.f, 0.f);
    f.fog = vec4(env.fogDensity, 0.15f, 0.f, 1.0f + env.fogDensity * 2.f + env.rain * 1.5f);
    f.exposure = vec4(exposure, 1.f / exposure, ev100, nightFactor);
    f.camForward = vec4(cam.forward(), cam.fovY);
    f.renderParams = vec4((float)settings.shadowCascades, 1.f, settings.ssr ? 1.f : 0.f, (float)debugView);
    f.lightning = vec4(env.lightning, 0, 0, 0);
    f.planetParams = vec4(0, 0, Max(0.001f, (float)cam.pos.z * 0.001f + 0.002f), env.haze * (1.f + env.rain * 2.f + env.fogDensity * 3.f));
    frameCB.data = f;
    frameCB.upload();
}

void Renderer::bindFrame() {
    ID3D11Buffer* cbs[] = {frameCB.get()};
    gfx::ctx->VSSetConstantBuffers(0, 1, cbs);
    gfx::ctx->PSSetConstantBuffers(0, 1, cbs);
    gfx::ctx->CSSetConstantBuffers(0, 1, cbs);
    gfx::ctx->GSSetConstantBuffers(0, 1, cbs);
    ID3D11SamplerState* samps[] = {gfx::states.pointClamp, gfx::states.linearClamp, gfx::states.linearWrap,
                                   gfx::states.anisoWrap, gfx::states.shadowCmp, gfx::states.pointWrap, gfx::states.anisoClamp};
    gfx::ctx->VSSetSamplers(0, 7, samps);
    gfx::ctx->PSSetSamplers(0, 7, samps);
    gfx::ctx->CSSetSamplers(0, 7, samps);
}

static void bindGlobals(Renderer& r, bool withShadow) {
    ID3D11ShaderResourceView* g[5] = {r.sky->shBuf.srv, r.sky->transmittance.srv, r.sky->aerial.srv,
                                      withShadow ? r.shadows->map.srv : nullptr, r.sky->skyView.srv};
    ID3D11ShaderResourceView* e[1] = {r.post->exposureBuf.srv};
    gfx::ctx->VSSetShaderResources(32, 5, g);
    gfx::ctx->PSSetShaderResources(32, 5, g);
    gfx::ctx->CSSetShaderResources(32, 5, g);
    gfx::ctx->VSSetShaderResources(40, 1, e);
    gfx::ctx->PSSetShaderResources(40, 1, e);
    gfx::ctx->CSSetShaderResources(40, 1, e);
}
static void unbindGlobals() {
    ID3D11ShaderResourceView* n[9] = {};
    gfx::ctx->VSSetShaderResources(32, 9, n);
    gfx::ctx->PSSetShaderResources(32, 9, n);
    gfx::ctx->CSSetShaderResources(32, 9, n);
}

void Renderer::render(const Camera& cam, const Environment& env, float dt) {
    stats.reset();
    auto* c = gfx::ctx;
    updateFrameConstants(cam, env, dt);
    bindFrame();
    unbindGlobals();
    world->update(cam.pos, TimeSeconds());
    dynamic->prepare(*this);
    gfx::gpuTimerBegin("sky");
    sky->update(*this, frame.planetParams.w);
    gfx::gpuTimerEnd();
    bindFrame();
    bindGlobals(*this, false);

    // Shadows
    gfx::gpuTimerBegin("shadows");
    shadows->render(*this);
    gfx::gpuTimerEnd();
    bindFrame();

    // G-buffer
    gfx::gpuTimerBegin("gbuffer");
    float clear0[4] = {0, 0, 0, 0};
    c->ClearRenderTargetView(gbAlbedo.rtv, clear0);
    c->ClearRenderTargetView(gbNormal.rtv, clear0);
    c->ClearRenderTargetView(gbMaterial.rtv, clear0);
    c->ClearRenderTargetView(gbEmissive.rtv, clear0);
    c->ClearRenderTargetView(gbVelocity.rtv, clear0);
    c->ClearDepthStencilView(depth.dsv, D3D11_CLEAR_DEPTH, 0.f, 0);
    ID3D11RenderTargetView* rts[5] = {gbAlbedo.rtv, gbNormal.rtv, gbMaterial.rtv, gbEmissive.rtv, gbVelocity.rtv};
    c->OMSetRenderTargets(5, rts, depth.dsv);
    gfx::setViewport((float)width, (float)height);
    c->OMSetDepthStencilState(gfx::states.depthGreaterWrite, 0);
    c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
    c->RSSetState(gfx::states.cullBack);
    terrain->drawGBuffer(*this);
    world->drawGBuffer(*this);
    props->drawGBuffer(*this, world->cells, materials);
    dynamic->drawGBuffer(*this);
    c->OMSetRenderTargets(0, nullptr, nullptr);
    gfx::gpuTimerEnd();

    // Local lights: static world lights + gameplay lights
    {
        Frustum fr;
        fr.fromMatrix(viewProjNoJitter);
        lightsFrame.clear();
        for (const DynamicLight& dl : dynamicLights) {
            LightGPU g;
            g.pos = rel(dl.pos, cam.pos);
            g.radius = dl.radius;
            g.color = dl.color;
            g.dir = dl.dir;
            g.spotCos = dl.spotCos;
            g.spotInner = dl.spotInner;
            if (length(g.pos) < 400.f && fr.testSphere(g.pos, g.radius)) lightsFrame.push_back(g);
        }
        world->gatherLights(cam.pos, nightFactor, env.gameSeconds, fr, lightsFrame, kMaxLights);
        if (!lightsFrame.empty()) gfx::updateBuffer(lightBuf, lightsFrame.data(), (u32)(lightsFrame.size() * sizeof(LightGPU)));
        lightCB.data.count = (u32)lightsFrame.size();
        lightCB.upload();
        stats.lights = (int)lightsFrame.size();
        dynamicLights.clear();
    }
    // Lighting
    gfx::gpuTimerBegin("lighting");
    bindGlobals(*this, true);
    ID3D11Buffer* scb[] = {shadowCB.get()};
    c->CSSetConstantBuffers(3, 1, scb);
    ID3D11Buffer* lcb[] = {lightCB.get()};
    c->CSSetConstantBuffers(2, 1, lcb);
    ID3D11ShaderResourceView* srvs[8] = {gbAlbedo.srv, gbNormal.srv, gbMaterial.srv, gbEmissive.srv, depth.srv,
                                         post->whiteTex.srv, cloudsTex.srv, lightBuf.srv};
    c->CSSetShaderResources(0, 8, srvs);
    c->CSSetUnorderedAccessViews(0, 1, &hdr.uav, nullptr);
    c->CSSetShader(csLighting, nullptr, 0);
    c->Dispatch(gfx::divUp(width, 16), gfx::divUp(height, 16), 1);
    gfx::unbindCSResources(8, 1);
    gfx::gpuTimerEnd();

    // Water (forward, reads copies of the lit scene and depth)
    gfx::gpuTimerBegin("water");
    c->CopyResource(hdrCopy.res, hdr.res);
    c->CopyResource(depthCopy.res, depth.res);
    c->OMSetRenderTargets(1, &hdr.rtv, depth.dsv);
    gfx::setViewport((float)width, (float)height);
    c->OMSetDepthStencilState(gfx::states.depthGreaterWrite, 0);
    water->draw(*this, *terrain, hdrCopy.srv, depthCopy.srv, env.wind);
    c->OMSetRenderTargets(0, nullptr, nullptr);
    gfx::gpuTimerEnd();

    // Post
    unbindGlobals();
    gfx::gpuTimerBegin("post");
    post->render(*this, dt);
    gfx::gpuTimerEnd();

    prevCamPos = cam.pos;
    prevViewProjNoJitter = viewProjNoJitter;
    dynamic->endFrame();
    frameIndex++;
    cameraCut = false;
}

}  // namespace Render
