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
#include "clouds.cpp"
#include "screenspace.cpp"
#include "ssao.cpp"
#include "ssr.cpp"
#include "envprobe.cpp"
#include "volumetrics.cpp"
#include "particles.cpp"
#include "weather.cpp"
#include "decals.cpp"
#include "fxdemo.cpp"
#include "grass.cpp"

namespace UI { gfx::Texture buildSignAtlas(const std::vector<std::string>& names); }

// Per-pass timing. GPU timestamp queries (--gputimers) are unreliable on software rasterizers, which execute
// lazily; --synctimers additionally waits for the device to go idle at every pass boundary and measures wall
// time, which gives trustworthy relative costs (at the price of a serialized, slower frame).
namespace RenderPassTiming {
struct Entry {
    std::string name;
    double ms = 0;
};
std::vector<Entry> entries;
int index = 0, depth = 0;
double startTime = 0;
bool enabled = false, checked = false;
bool skipFrame = false;   // camera-cut frames (histories reset, SSR off) are not representative: not accumulated
ID3D11Query* idleQuery = nullptr;

void waitIdle() {
    if (!idleQuery) {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
        if (FAILED(gfx::dev->CreateQuery(&qd, &idleQuery))) return;
    }
    gfx::ctx->End(idleQuery);
    BOOL done = FALSE;
    while (gfx::ctx->GetData(idleQuery, &done, sizeof(done), 0) == S_FALSE) Sleep(0);
}
void begin(const char* name) {
    gfx::gpuTimerBegin(name);
    if (!checked) {
        enabled = Platform::hasArg("synctimers");
        checked = true;
    }
    if (!enabled || depth++ > 0) return;
    waitIdle();
    if (index >= (int)entries.size()) entries.push_back(Entry());
    entries[index].name = name;
    startTime = Platform::timeSeconds();
}
void end() {
    gfx::gpuTimerEnd();
    if (!enabled || --depth > 0) return;
    waitIdle();
    double ms = (Platform::timeSeconds() - startTime) * 1000.0;
    Entry& e = entries[index++];
    if (!skipFrame) e.ms = e.ms <= 0 ? ms : e.ms * 0.85 + ms * 0.15;
}
std::string report() {
    std::string s;
    double total = 0;
    for (const Entry& e : entries) {
        s += StrFormat("%-18s %8.2f ms\n", e.name.c_str(), e.ms);
        total += e.ms;
    }
    s += StrFormat("%-18s %8.2f ms\n", "total", total);
    return s;
}
void endFrame() { index = 0; }
}  // namespace RenderPassTiming

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
    // enterable interiors: volumes (4 x float4), portals (3 x float4), per-light volume ids (see uploadInteriors)
    interiorBuf = gfx::createBuffer(kMaxInteriorVolumes * 64, 64, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
    portalBuf = gfx::createBuffer(kMaxInteriorPortals * 48, 48, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
    lightVolumeBuf = gfx::createBuffer(kMaxLights * 4, 4, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
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
    clouds = new CloudSystem();
    clouds->init();
    props = new PropRenderer();
    props->init(materials);
    dynamic = new DynamicRenderer();
    dynamic->init(materials);
    shadows->casters.push_back([this](Renderer& r, const mat4& vp, int cascade) { dynamic->drawShadow(r, vp, cascade); });
    shadows->casters.push_back([this](Renderer& r, const mat4& vp, int cascade) { props->drawShadow(r, world->cells, vp, cascade); });
    particles = new ParticleSystem();
    particles->init(settings.particleBudget);
    decals = new DecalSystem();
    decals->init();
    ss = new ScreenSpaceSystem();
    ss->init();
    ao = new AOSystem();
    ao->init();
    ssrSys = new SSRSystem();
    ssrSys->init();
    envProbe = new EnvProbeSystem();
    envProbe->init();
    fog = new VolumetricFog();
    fog->init();
    weather = new WeatherSystem();
    weather->init();
    grass = new GrassSystem();
    grass->init();
    createTargets();
    if (const char* ds = Platform::argValue("debugsplit")) {
        debugView = atoi(ds);
        debugSplit = 0.5f;
    }
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
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC dv = {};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dv.Flags = D3D11_DSV_READ_ONLY_DEPTH;
        gfx::dev->CreateDepthStencilView(depth.res, &dv, &depthRO);
    }
    gbAlbedo = createTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, TEX_RTV | TEX_SRV);
    gbNormal = createTexture2D(width, height, DXGI_FORMAT_R16G16_UNORM, TEX_RTV | TEX_SRV);
    gbMaterial = createTexture2D(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_RTV | TEX_SRV);
    gbEmissive = createTexture2D(width, height, DXGI_FORMAT_R11G11B10_FLOAT, TEX_RTV | TEX_SRV);
    gbVelocity = createTexture2D(width, height, DXGI_FORMAT_R16G16_FLOAT, TEX_RTV | TEX_SRV);
    hdr = createTexture2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_RTV | TEX_SRV | TEX_UAV);
    hdrCopy = createTexture2D(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV);
    reactive = createTexture2D(width, height, DXGI_FORMAT_R8_UNORM, TEX_RTV | TEX_SRV);
    depthCopy = createTexture2D(width, height, DXGI_FORMAT_R32_TYPELESS, TEX_SRV);
    // Cloud layer before the first cloud pass: fully transparent (rgb 0, transmittance 1)
    u16 half1 = 0x3C00;
    u16 cl[4] = {0, 0, 0, half1};
    cloudsTex = createTexture2D(1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV, 1, 1, cl, 8);
    post->resize(width, height);
    if (clouds) clouds->resize(width, height);
    ss->resize(width, height);
    ao->resize(ss->halfW, ss->halfH);
    decals->resize(width, height);
    ssrSys->resize(width, height, ss->halfW, ss->halfH);
}

void Renderer::releaseTargets() {
    SAFE_RELEASE(depthRO);
    depth.release();
    gbAlbedo.release();
    gbNormal.release();
    gbMaterial.release();
    gbEmissive.release();
    gbVelocity.release();
    hdr.release();
    hdrCopy.release();
    reactive.release();
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
    sunElevation = sunElev;
}

bool Renderer::cameraInInterior() const {
    for (const InteriorVolume& v : interiorVolumes) {
        vec3 d = rel(camera.pos, v.center);
        float lx = d.x * v.axis.x + d.y * v.axis.y, ly = -d.x * v.axis.y + d.y * v.axis.x;
        if (fabsf(lx) <= v.halfExtents.x && fabsf(ly) <= v.halfExtents.y && fabsf(d.z) <= v.halfExtents.z) return true;
    }
    return false;
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
    // Aerial perspective distance scale: fog and rain haze are mostly handled by the froxel volume when enabled
    bool froxels = settings.volumetrics && settings.fogQuality > 0;
    f.fog = vec4(env.fogDensity, 0.15f, 0.f, froxels ? 1.0f + env.fogDensity * 0.5f + env.rain * 0.5f : 1.0f + env.fogDensity * 2.f + env.rain * 1.5f);
    f.exposure = vec4(exposure, 1.f / exposure, ev100, nightFactor);
    f.camForward = vec4(cam.forward(), cam.fovY);
    f.renderParams = vec4((float)settings.shadowCascades, debugView > 0 ? debugSplit : 0.f, settings.contactShadows ? 1.f : 0.f, (float)debugView);
    f.lightning = vec4(env.lightning, 0, 0, 0);
    f.cloudShadow = vec4((float)cam.pos.x, (float)cam.pos.y, 8192.f, 0.85f);
    f.planetParams = vec4(0, 0, Max(0.001f, (float)cam.pos.z * 0.001f + 0.002f), env.haze * (1.f + env.rain * 2.f + env.fogDensity * 3.f));
    bool aoOn = settings.ssao && settings.aoQuality > 0;
    bool ssrOn = settings.ssr && settings.ssrQuality > 0;
    f.ssParams = vec4(aoOn ? 1.f : 0.f, aoOn && settings.ssgi ? 1.f : 0.f, ssrOn && ss->pyramidValid ? 1.f : 0.f, settings.ssrMaxRoughness);
    f.halfScreen = vec4((float)ss->halfW, (float)ss->halfH, 1.f / ss->halfW, 1.f / ss->halfH);
    f.fogParams0 = vec4(0.f);
    f.fogParams1 = vec4(0.f);
    f.overhead = vec4(0.f);
    f.envProbe = vec4(0.f);
    f.weather2 = vec4(0.f);
    fog->setFrameParams(*this, env, f);
    weather->setFrameParams(*this, env, f, dt);
    // How much of the horizon band is taken by (partly sunlit) facades: drives the warm urban bounce in the sky SH
    float urban = 0.f;
    if (map) {
        const World::RegionInfo& ri = World::regionInfo(map->regionAt((float)cam.pos.x, (float)cam.pos.y));
        urban = ri.urban * Saturate((ri.minFloors + ri.maxFloors) * 0.5f / 8.f);
    }
    urbanEnclosure = frameIndex == 0 ? urban : Lerp(urbanEnclosure, urban, Clamp(dt * 0.5f, 0.f, 1.f));
    f.ambientParams = vec4(urbanEnclosure, 0.f, 0.f, 0.f);
    // Light pollution: city density at the camera and on a 2.5 km ring (the glow leans towards the denser side)
    vec3 glow(0.f);
    if (map) {
        float center = World::regionInfo(map->regionAt((float)cam.pos.x, (float)cam.pos.y)).urban;
        float ring = 0.f;
        vec2 lean(0.f);
        for (int k = 0; k < 8; k++) {
            vec2 d(cosf(k * 0.7853982f), sinf(k * 0.7853982f));
            float u = World::regionInfo(map->regionAt((float)(cam.pos.x + d.x * 2500.0), (float)(cam.pos.y + d.y * 2500.0))).urban;
            ring += u * 0.125f;
            lean += d * u * 0.25f;
        }
        glow = vec3(0.35f * center + 0.65f * ring, lean.x, lean.y);
    }
    cityGlow = frameIndex == 0 ? glow : lerp(cityGlow, glow, Clamp(dt * 0.3f, 0.f, 1.f));
    f.skyGlow = vec4(cityGlow.x, cityGlow.y, cityGlow.z, nightFactor);
    f.renderFlags = vec4(settings.reduceFlashing ? 1.f : 0.f, 0.f, 0.f, 0.f);
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

// Global shader resources (see common.hlsli): t32..t44
static void bindGlobals(Renderer& r, bool withShadow) {
    ID3D11ShaderResourceView* g[13] = {r.sky->shBuf.srv, r.sky->transmittance.srv, r.sky->aerial.srv,
                                       withShadow ? r.shadows->map.srv : nullptr, r.sky->skyView.srv,
                                       withShadow ? r.clouds->shadowMap.srv : nullptr,
                                       r.fog->output(r.settings), r.envProbe->srv(), r.post->exposureBuf.srv,
                                       r.weather->overheadSrv(), r.terrain->waterTex.srv, r.terrain->heightTex.srv,
                                       r.envProbe->shSrv()};
    gfx::ctx->VSSetShaderResources(32, 13, g);
    gfx::ctx->PSSetShaderResources(32, 13, g);
    gfx::ctx->CSSetShaderResources(32, 13, g);
}
static void unbindGlobals() {
    ID3D11ShaderResourceView* n[13] = {};
    gfx::ctx->VSSetShaderResources(32, 13, n);
    gfx::ctx->PSSetShaderResources(32, 13, n);
    gfx::ctx->CSSetShaderResources(32, 13, n);
}

// Enterable interiors (see InteriorVolume in renderer.h): camera-relative volume/portal records for the lighting
// pass, and the volume each local light sits in (lights only light their own volume; outdoor lights only outdoors).
void Renderer::uploadInteriors() {
    const dvec3 cam = camera.pos;
    int nv = Min((int)interiorVolumes.size(), kMaxInteriorVolumes);
    interiorGpu.clear();
    portalGpu.clear();
    int np = 0;
    for (int i = 0; i < nv; i++) {
        const InteriorVolume& v = interiorVolumes[i];
        int first = np, count = 0;
        for (int k = 0; k < v.portalCount && np < kMaxInteriorPortals; k++) {
            int pi = v.firstPortal + k;
            if (pi < 0 || pi >= (int)interiorPortals.size()) break;
            const InteriorPortal& p = interiorPortals[pi];
            portalGpu.push_back(vec4(rel(p.corner, cam), p.transmission));
            portalGpu.push_back(vec4(p.edgeU, 0.f));
            portalGpu.push_back(vec4(p.edgeV, 0.f));
            np++;
            count++;
        }
        interiorGpu.push_back(vec4(rel(v.center, cam), (float)first));
        interiorGpu.push_back(vec4(v.axis.x, v.axis.y, (float)count, v.skyBounce));
        interiorGpu.push_back(vec4(v.halfExtents, 0.f));
        interiorGpu.push_back(vec4(v.ambient, 0.f));
    }
    lightVolumeFrame.assign(lightsFrame.size(), 0u);
    for (size_t li = 0; li < lightsFrame.size() && nv > 0; li++) {
        vec3 p = lightsFrame[li].pos;
        for (int i = 0; i < nv; i++) {
            vec3 d = p - interiorGpu[(size_t)i * 4].xyz();
            vec2 ax = interiorGpu[(size_t)i * 4 + 1].xy();
            vec3 he = interiorGpu[(size_t)i * 4 + 2].xyz();
            float lx = d.x * ax.x + d.y * ax.y, ly = -d.x * ax.y + d.y * ax.x;
            if (fabsf(lx) <= he.x + 0.3f && fabsf(ly) <= he.y + 0.3f && fabsf(d.z) <= he.z + 0.3f) {
                lightVolumeFrame[li] = (u32)i + 1u;
                break;
            }
        }
    }
    if (!interiorGpu.empty()) gfx::updateBuffer(interiorBuf, interiorGpu.data(), (u32)(interiorGpu.size() * sizeof(vec4)));
    if (!portalGpu.empty()) gfx::updateBuffer(portalBuf, portalGpu.data(), (u32)(portalGpu.size() * sizeof(vec4)));
    if (!lightVolumeFrame.empty()) gfx::updateBuffer(lightVolumeBuf, lightVolumeFrame.data(), (u32)(lightVolumeFrame.size() * sizeof(u32)));
    lightCB.data.interiorCount = (u32)nv;
}

void Renderer::render(const Camera& cam, const Environment& env, float dt) {
    stats.reset();
    RenderPassTiming::skipFrame = cameraCut;
    auto* c = gfx::ctx;
    updateFrameConstants(cam, env, dt);
    if (Platform::hasArg("fxdemo")) fxDemo(dt);
    bindFrame();
    unbindGlobals();
    world->update(cam.pos, TimeSeconds());
    // Overhead height map of the static world (rain occlusion / dry areas / grass): refresh + frame constants
    weather->updateOverhead(*this);
    weather->updateLightning(*this, env, clouds->cloudBase);
    weather->setFrameParams(*this, env, frame, 0.f);
    frameCB.data = frame;
    frameCB.upload();
    bindFrame();
    dynamic->prepare(*this);
    RenderPassTiming::begin("sky");
    sky->update(*this, frame.planetParams.w);
    RenderPassTiming::end();
    bindFrame();
    bindGlobals(*this, false);

    // Shadows
    RenderPassTiming::begin("shadows");
    shadows->render(*this);
    RenderPassTiming::end();
    bindFrame();

    // Reflection probe: one cube face per frame (full capture after camera cuts)
    RenderPassTiming::begin("envprobe");
    bindGlobals(*this, true);
    envProbe->update(*this, env);
    bindFrame();
    frame.envProbe = envProbe->valid ? vec4(rel(envProbe->frontPos, cam.pos), (float)(envProbe->mips - 1)) : vec4(0.f);
    frame.ambientParams.z = envProbe->shValid ? 1.f : 0.f;
    frame.ambientParams.w = 280.f;
    frameCB.data = frame;
    frameCB.upload();
    bindGlobals(*this, false);
    RenderPassTiming::end();

    // Grass placement (GPU, from the terrain splat + overhead map)
    RenderPassTiming::begin("grass place");
    bindGlobals(*this, false);
    grass->place(*this, *terrain, dt);
    RenderPassTiming::end();

    // G-buffer
    RenderPassTiming::begin("gbuffer");
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
    grass->draw(*this);
    world->drawGBuffer(*this);
    props->drawGBuffer(*this, world->cells, materials);
    dynamic->drawGBuffer(*this);
    c->OMSetRenderTargets(0, nullptr, nullptr);
    RenderPassTiming::end();

    // Deferred decals + skid marks into the G-buffer
    RenderPassTiming::begin("decals");
    decals->update(*this, dt);
    decals->render(*this);
    RenderPassTiming::end();

    // Screen-space inputs (depth pyramid, previous frame color pyramid) and AO / indirect diffuse
    RenderPassTiming::begin("hiz+pyramid");
    ss->buildHiZ(*this);
    ss->buildColorPyramid(*this, post->finalSrv, post->historyValid && !cameraCut);
    RenderPassTiming::end();
    RenderPassTiming::begin("ao+gi");
    bindGlobals(*this, false);
    ID3D11ShaderResourceView* aoSrv = ao->run(*this, *ss);
    RenderPassTiming::end();
    RenderPassTiming::begin("ssr");
    ID3D11ShaderResourceView* ssrSrv = ssrSys->run(*this, *ss);
    RenderPassTiming::end();
    // Particles: emission (adds effect lights before the light gather) and GPU simulation / sort
    RenderPassTiming::begin("particles sim");
    particles->updateCPU(*this, dt);
    bindGlobals(*this, false);
    particles->simulate(*this, dt);
    RenderPassTiming::end();

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
            g.spotCos = dl.headlight ? 0.f : dl.spotCos;
            g.spotInner = dl.headlight ? 2.f : dl.spotInner;
            if (length(g.pos) < 400.f && fr.testSphere(g.pos, g.radius)) lightsFrame.push_back(g);
        }
        world->gatherLights(cam.pos, nightFactor, env.gameSeconds, env.timeOfDay, fr, lightsFrame, kMaxLights);
        if (!lightsFrame.empty()) gfx::updateBuffer(lightBuf, lightsFrame.data(), (u32)(lightsFrame.size() * sizeof(LightGPU)));
        lightCB.data.count = (u32)lightsFrame.size();
        uploadInteriors();   // interior volumes / portals + which volume each light belongs to (sets interiorCount)
        lightCB.upload();
        stats.lights = (int)lightsFrame.size();
        dynamicLights.clear();
    }
    // Clouds (half resolution, before lighting composites them over the sky)
    RenderPassTiming::begin("clouds");
    bindGlobals(*this, false);
    if (settings.clouds) clouds->update(*this, env, dt, ss->hiz.srv);
    RenderPassTiming::end();
    // Volumetric fog (needs the light list, shadow maps and the cloud shadow map)
    RenderPassTiming::begin("fog");
    bindGlobals(*this, true);
    fog->run(*this, env, dt);
    RenderPassTiming::end();
    // Lighting
    RenderPassTiming::begin("lighting");
    bindGlobals(*this, true);
    ID3D11Buffer* scb[] = {shadowCB.get()};
    c->CSSetConstantBuffers(3, 1, scb);
    ID3D11Buffer* lcb[] = {lightCB.get()};
    c->CSSetConstantBuffers(2, 1, lcb);
    ID3D11ShaderResourceView* srvs[14] = {gbAlbedo.srv, gbNormal.srv, gbMaterial.srv, gbEmissive.srv, depth.srv,
                                          aoSrv ? aoSrv : post->whiteTex.srv, settings.clouds ? clouds->output() : cloudsTex.srv,
                                          lightBuf.srv, ss->depthCur(), ss->halfNormal.srv, ssrSrv,
                                          interiorBuf.srv, portalBuf.srv, lightVolumeBuf.srv};
    c->CSSetShaderResources(0, 14, srvs);
    c->CSSetUnorderedAccessViews(0, 1, &hdr.uav, nullptr);
    c->CSSetShader(csLighting, nullptr, 0);
    c->Dispatch(gfx::divUp(width, 16), gfx::divUp(height, 16), 1);
    gfx::unbindCSResources(14, 1);
    RenderPassTiming::end();

    // Water (forward, reads copies of the lit scene and depth)
    RenderPassTiming::begin("water");
    c->CopyResource(hdrCopy.res, hdr.res);
    c->CopyResource(depthCopy.res, depth.res);
    c->OMSetRenderTargets(1, &hdr.rtv, depth.dsv);
    gfx::setViewport((float)width, (float)height);
    c->OMSetDepthStencilState(gfx::states.depthGreaterWrite, 0);
    water->draw(*this, *terrain, hdrCopy.srv, depthCopy.srv, env.wind, ss->hiz.srv, ss->hizMips);
    c->OMSetRenderTargets(0, nullptr, nullptr);
    RenderPassTiming::end();

    // Vehicle windows (forward, premultiplied over the lit cabins; depth test without write)
    RenderPassTiming::begin("glass");
    c->OMSetRenderTargets(1, &hdr.rtv, depth.dsv);
    gfx::setViewport((float)width, (float)height);
    dynamic->drawGlass(*this);
    c->OMSetRenderTargets(0, nullptr, nullptr);
    RenderPassTiming::end();

    // Particles (sorted, soft, lit), rain and lightning; both write the TAA reactive mask
    RenderPassTiming::begin("particles");
    float zero4[4] = {0, 0, 0, 0};
    c->ClearRenderTargetView(reactive.rtv, zero4);
    particles->draw(*this, reactive.rtv, weather->blend);
    RenderPassTiming::end();
    RenderPassTiming::begin("rain");
    weather->draw(*this, env, reactive.rtv);
    RenderPassTiming::end();

    // Post
    unbindGlobals();
    RenderPassTiming::begin("post");
    post->render(*this, dt);
    RenderPassTiming::end();

    prevCamPos = cam.pos;
    prevViewProjNoJitter = viewProjNoJitter;
    interiorVolumes.clear();
    interiorPortals.clear();
    dynamic->endFrame();
    frameIndex++;
    cameraCut = false;
    RenderPassTiming::endFrame();
    if ((frameIndex % 30) == 0 && Platform::hasArg("gputimers")) LOG("GPU timers (frame %u):\n%s", frameIndex, gfx::gpuTimerReport().c_str());
    if ((frameIndex % 30) == 0 && RenderPassTiming::enabled) LOG("Pass timings, device idle at boundaries (frame %u, %dx%d):\n%s", frameIndex, width, height, RenderPassTiming::report().c_str());
}

}  // namespace Render
