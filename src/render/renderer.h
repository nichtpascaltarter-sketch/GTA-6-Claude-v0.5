// Frame renderer: deferred PBR pipeline with atmosphere, shadows and post-processing.
#pragma once
#include "../gfx/gfx.h"
#include <functional>

namespace World { struct WorldMap; }

namespace Render {

struct Camera {
    dvec3 pos;
    float yaw = 0.f;    // radians, 0 = looking north (+Y), increasing counter-clockwise (towards -X)
    float pitch = 0.f;  // radians, positive looks up
    float roll = 0.f;
    float fovY = 60.f * kDegToRad;
    float nearZ = 0.1f;
    vec3 forward() const { return vec3(-sinf(yaw) * cosf(pitch), cosf(yaw) * cosf(pitch), sinf(pitch)); }
    vec3 right() const { return normalize(cross(forward(), vec3(0, 0, 1))); }
    mat4 viewRel() const;  // view matrix with the camera at the origin
};

struct Environment {
    float timeOfDay = 10.f;   // hours
    int dayOfYear = 172;
    float cloudCover = 0.35f; // 0..1
    float rain = 0.f;         // 0..1
    float wetness = 0.f;      // 0..1 surface wetness
    float fogDensity = 0.f;   // extra ground fog 0..1
    float haze = 1.f;         // mie multiplier
    float wind = 0.3f;        // 0..1
    vec2 windDir = vec2(0.7f, 0.7f);
    float lightning = 0.f;    // flash intensity
    float gameSeconds = 0.f;  // continuous clock for animation
};

// Matches FrameCB in shaders/common.hlsli
struct FrameConstants {
    mat4 viewProj, viewProjNoJitter, invViewProj, view, proj, prevViewProj;
    vec4 camPos, camPosWrap, screen, jitter, sunDir, sunColor;
    vec4 skyAmbient[9];
    vec4 time, weather, wind, fog, exposure, camForward, renderParams, lightning, planetParams;
    vec4 cloudShadow;  // xy center (world), z size (m), w strength
    // Added for the screen-space / volumetric / weather effects (see common.hlsli for the meaning of each field)
    vec4 fogParams0;   // x ground density (1/m), y height falloff (1/m), z reference height (m), w volume far distance (m)
    vec4 fogParams1;   // x phase anisotropy g, y 1/log2(far/near), z volume near (m), w enabled
    vec4 overhead;     // xy overhead height map min corner (world xy), z size (m), w enabled
    vec4 envProbe;     // xyz probe capture position relative to the camera, w max mip (0 = no probe)
    vec4 ssParams;     // x AO enabled, y GI enabled, z SSR enabled, w SSR max roughness
    vec4 weather2;     // x overcast (0..1), y storm (0..1), z puddle amount, w ripple time
    vec4 halfScreen;   // half-resolution size and inverse
    vec4 ambientParams; // x urban enclosure (facade share of the horizon, 0..1), y lightning ambient (lux), zw unused
    vec4 skyGlow;      // x urban light pollution (0..1), yz direction towards the brighter city (xy, length = bias), w night
};

struct ShadowConstants {
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;   // far distance of each cascade
    vec4 cascadeTexel;    // world size of a texel per cascade
    vec4 shadowParams;    // x map resolution, y cascade count, z fade start, w soft (contact-hardening) shadows
    vec4 pad;             // light-space depth range (m) per cascade
};

struct Settings {
    float renderScale = 1.f;
    int shadowRes = 2048;
    int shadowCascades = 4;
    bool vsync = true;
    bool ssao = true;         // master toggle for ambient occlusion (quality in aoQuality)
    bool ssr = true;          // master toggle for screen-space reflections (quality in ssrQuality)
    bool taa = true;
    bool bloom = true;
    bool clouds = true;
    bool volumetrics = true;  // master toggle for froxel volumetric fog (quality in fogQuality)
    bool motionBlur = true;
    float motionBlurAmount = 1.f;  // shutter scale for motion blur (0..2; 1 = 180 degree shutter)
    float fovDeg = 60.f;
    int quality = 2;  // 0 low, 1 medium, 2 high, 3 ultra

    // Per-effect quality (set together by applyPreset, individually adjustable)
    int aoQuality = 2;            // 0 off, 1 low (1 slice), 2 high (2 slices), 3 ultra (3 slices, more steps)
    bool contactShadows = true;   // screen-space sun contact shadows in the lighting pass
    bool softShadows = true;      // contact-hardening sun shadows (blocker search, penumbra grows with distance)
    bool ssgi = true;             // one-bounce screen-space indirect diffuse (computed with the AO pass)
    int ssrQuality = 2;           // 0 off, 1 low (smooth surfaces only), 2 high (glossy), 3 ultra (more steps)
    float ssrMaxRoughness = 0.55f;
    bool waterSSR = true;         // screen-space reflections on the water surface
    bool envProbe = true;         // dynamic camera cubemap used when reflection rays miss
    int envProbeRes = 128;
    int envProbeInterval = 2;     // frames per probe step (6 face captures + 1 prefilter per refresh cycle)
    int fogQuality = 2;           // 0 off, 1 low (96x54x48), 2 high (160x90x64), 3 ultra (192x108x96)
    float fogDistance = 3000.f;   // far end of the froxel volume (m)
    int cloudQuality = 2;         // 0 low .. 3 ultra (ray-march steps; quarter-res trace + temporal reconstruction)
    int grassQuality = 2;         // 0 off, 1 low, 2 high, 3 ultra (density and blade detail)
    float grassDistance = 75.f;   // grass fades out towards this distance (m)
    int particleBudget = 20000;   // max simultaneous particles (pool rounded up to a power of two)
    int maxDecals = 512;
    int rainQuality = 2;          // 0 low .. 3 ultra (number of rain streaks)

    // Quality ladder. High is the default and the 1440p / RTX 4070-class target (all effects on, ~6-7 ms for
    // clouds + fog + SSR + SSGI); Medium trims step counts and resolutions; Low keeps the look with the
    // cheapest variants; Ultra raises sample counts, probe resolution and shadow resolution.
    enum Preset { PRESET_LOW = 0, PRESET_MEDIUM = 1, PRESET_HIGH = 2, PRESET_ULTRA = 3 };
    static const char* presetName(int q) {
        static const char* names[4] = {"Low", "Medium", "High", "Ultra"};
        return names[q < 0 ? 0 : (q > 3 ? 3 : q)];
    }
    // Sets every per-effect field from a global quality level (0 low, 1 medium, 2 high, 3 ultra).
    void applyPreset(int q) {
        quality = q < 0 ? 0 : (q > 3 ? 3 : q);
        static const int ao[4] = {1, 1, 2, 3}, ssrQ[4] = {1, 2, 2, 3}, fogQ[4] = {1, 1, 2, 3}, cloudQ[4] = {0, 1, 2, 3};
        static const int grassQ[4] = {1, 1, 2, 3}, shadowR[4] = {1024, 2048, 2048, 4096}, budget[4] = {6000, 12000, 20000, 32000};
        aoQuality = ao[quality];
        contactShadows = quality >= 1;
        softShadows = quality >= 2;
        ssao = true;
        ssgi = quality >= 1;
        ssrQuality = ssrQ[quality];
        ssr = true;
        ssrMaxRoughness = quality == 0 ? 0.3f : (quality == 1 ? 0.45f : 0.55f);
        waterSSR = quality >= 1;
        envProbe = true;
        envProbeRes = quality >= 3 ? 256 : 128;
        envProbeInterval = 4 - quality;   // Low 4, Medium 3, High 2, Ultra 1 frames per capture step
        fogQuality = fogQ[quality];
        volumetrics = true;
        cloudQuality = cloudQ[quality];
        grassQuality = grassQ[quality];
        grassDistance = quality == 0 ? 45.f : (quality == 1 ? 60.f : (quality == 2 ? 75.f : 90.f));
        particleBudget = budget[quality];
        maxDecals = quality == 0 ? 128 : (quality == 1 ? 256 : 512);
        rainQuality = quality;
        shadowRes = shadowR[quality];
    }
};

// Local light (matches LightGPU in shaders/lighting.hlsl). pos is camera-relative when uploaded.
struct LightGPU {
    vec3 pos;
    float radius;
    vec3 color;
    float spotCos;
    vec3 dir;
    float spotInner;
};

// A light submitted by gameplay in world space (headlights, muzzle flashes, sirens, fires)
struct DynamicLight {
    dvec3 pos;
    vec3 color;
    float radius;
    vec3 dir = vec3(0, 0, -1);
    float spotCos = -2.f, spotInner = -1.f;
    bool headlight = false;  // vehicle low beam: asymmetric cut-off pattern along dir (spotCos/Inner ignored)
};

// Enterable interiors (world/interiors.h), submitted by gameplay every frame like dynamic lights. Inside an interior
// volume the lighting pass replaces the sky/probe ambient and sky reflections with the room's own ambient plus the
// daylight entering through the room's openings (portals, Lambert polygon form factor), and local lights only light
// the volume that contains them (lamps don't leak through walls, street lights don't light rooms).
struct InteriorPortal {
    dvec3 corner;              // one corner of the opening (world)
    vec3 edgeU, edgeV;         // opening edges; cross(edgeU, edgeV) points into the room
    float transmission = 1.f;  // 0 closed opaque door .. 0.8 glass .. 1 open
};
struct InteriorVolume {
    dvec3 center;              // oriented box (world)
    vec2 axis = vec2(1, 0);    // unit horizontal x axis of the box
    vec3 halfExtents;
    vec3 ambient;              // room ambient radiance (irradiance / PI, same units as the sky SH)
    float skyBounce = 0.f;     // fraction of the sky irradiance bounced around inside by daylight
    int firstPortal = 0, portalCount = 0;   // range in Renderer::interiorPortals
};

// Gameplay particle effects (see ParticleSystem in particles.cpp for the per-type behavior).
// `dir` is an initial velocity / direction hint in m/s (e.g. (0,0,1.2) rising smoke, surface normal for sparks),
// `scale` ~1 is the typical size (explosions: radius / 6), `count` is the number of particles (1..64 per call).
enum ParticleType : int {
    PT_SMOKE = 0, PT_DARK_SMOKE, PT_DUST, PT_SPARKS, PT_FIRE, PT_EXPLOSION, PT_BLOOD, PT_WATER_SPLASH, PT_WAKE_SPRAY,
    PT_TIRE_SMOKE, PT_EXHAUST, PT_MUZZLE_FLASH, PT_GLASS, PT_DEBRIS, PT_LEAVES, PT_RAIN_SPLASH, PT_STEAM, PT_EMBERS,
    PT_COUNT
};

// Deferred decals projected onto the G-buffer (bullet holes, blood, scorch marks).
enum DecalType : int {
    DECAL_BULLET_CONCRETE = 0, DECAL_BULLET_METAL, DECAL_BULLET_GLASS, DECAL_BLOOD, DECAL_SCORCH, DECAL_BLOOD_POOL,
    DECAL_COUNT
};

// Gameplay-driven screen effects applied in the grading / tonemap pass every frame (defaults = no effect).
struct PostFxControls {
    float saturation = 1.f;        // 0 = greyscale, 1 = normal, > 1 more saturated
    vec3 tint = vec3(1);           // color multiplier of the final image
    float vignette = 0.f;          // extra vignette strength 0..1 (damage feedback, death screen)
    vec3 vignetteColor = vec3(0);  // vignette color (e.g. red for low health)
    float chromatic = 0.f;         // chromatic aberration 0..1 (impacts)
    float flash = 0.f;             // full-screen flash 0..1 (camera shutter, flashbang, explosion)
    vec3 flashColor = vec3(1);
    float blur = 0.f;              // full-screen blur 0..1
    float underwater = 0.f;        // 0..1: blue-green tint, depth fog, wobble, softened image
    float grain = 0.f;             // extra film grain 0..1
};

struct DrawStats {
    int drawCalls = 0;
    int triangles = 0;
    int terrainNodes = 0;
    int lights = 0;
    void reset() { drawCalls = triangles = terrainNodes = lights = 0; }
};

class Renderer;
extern Renderer* gRenderer;

// Passes plug into the renderer; each owns its resources.
struct SkySystem;
struct TerrainRenderer;
struct ShadowSystem;
struct PostSystem;
struct MaterialLibrary;
struct WorldRenderer;
struct WaterRenderer;
struct PropRenderer;
struct DynamicRenderer;
struct CloudSystem;
struct ParticleSystem;
struct DecalSystem;
struct ScreenSpaceSystem;
struct AOSystem;
struct SSRSystem;
struct EnvProbeSystem;
struct VolumetricFog;
struct WeatherSystem;
struct GrassSystem;

class Renderer {
public:
    bool init(int outW, int outH);
    void shutdown();
    void resize(int outW, int outH);
    void setWorld(World::WorldMap* map);
    // Render one frame to the back buffer.
    void render(const Camera& cam, const Environment& env, float dt);

    Settings settings;
    PostFxControls postFx;  // set by gameplay each frame (persistent until changed)
    // Traffic-signal lamp state from the AI signal phases (0 red, 1 amber, 2 green) for a signal prop on road edge
    // `edge` at `propPos`; unset = props run their own local timer
    std::function<int(int edge, vec2 propPos)> signalLampFn;
    DrawStats stats;
    FrameConstants frame;
    gfx::CBuffer<FrameConstants> frameCB;
    gfx::CBuffer<ShadowConstants> shadowCB;

    // Targets (internal render resolution)
    int width = 0, height = 0;       // render resolution
    int outWidth = 0, outHeight = 0; // output resolution
    gfx::Texture depth;
    ID3D11DepthStencilView* depthRO = nullptr;  // read-only view: depth test while sampling depth (decals, particles)
    gfx::Texture gbAlbedo, gbNormal, gbMaterial, gbEmissive, gbVelocity;
    gfx::Texture hdr, hdrCopy, depthCopy;
    gfx::Texture reactive;  // R8 mask written by particles / rain: TAA favors the current frame there (no smearing)
    gfx::Texture cloudsTex;  // cloud color+transmittance at quarter res (cleared to "no clouds" until the cloud pass runs)

    // Camera state
    Camera camera;
    dvec3 prevCamPos;
    mat4 prevViewProjNoJitter;
    mat4 viewRel, proj, projJitter, viewProj, viewProjNoJitter;
    vec2 jitter, prevJitter;
    u32 frameIndex = 0;
    int debugView = 0;
    float debugSplit = 0.f;  // > 0: debug view only right of this screen fraction (--debugsplit N)
    bool cameraCut = false;  // set for one frame on camera teleports: snaps exposure and resets history
    float exposure = 1e-4f;  // current exposure multiplier (pre-exposure)
    float ev100 = 14.f;
    vec3 sunDir, lightTOA;
    bool moonLight = false;
    float nightFactor = 0.f;
    float sunElevation = 0.f;    // degrees above the horizon
    bool cameraInInterior() const;  // camera inside one of this frame's interior volumes
    float urbanEnclosure = 0.f;  // smoothed urban density around the camera (ambient bounce model)
    vec3 cityGlow = vec3(0.f);   // smoothed light pollution: x amount, yz direction bias (sky glow at night)

    SkySystem* sky = nullptr;
    TerrainRenderer* terrain = nullptr;
    ShadowSystem* shadows = nullptr;
    PostSystem* post = nullptr;
    MaterialLibrary* materials = nullptr;
    WorldRenderer* world = nullptr;
    WaterRenderer* water = nullptr;
    PropRenderer* props = nullptr;
    DynamicRenderer* dynamic = nullptr;
    CloudSystem* clouds = nullptr;
    ScreenSpaceSystem* ss = nullptr;
    AOSystem* ao = nullptr;
    SSRSystem* ssrSys = nullptr;
    EnvProbeSystem* envProbe = nullptr;
    VolumetricFog* fog = nullptr;
    WeatherSystem* weather = nullptr;
    GrassSystem* grass = nullptr;
    World::WorldMap* map = nullptr;

    ID3D11ComputeShader* csLighting = nullptr;
    gfx::Buffer lightBuf;
    struct LightCBData { u32 count, interiorCount, pad[2]; };
    gfx::CBuffer<LightCBData> lightCB;
    std::vector<LightGPU> lightsFrame;
    std::vector<DynamicLight> dynamicLights;  // cleared each frame after rendering
    static const int kMaxLights = 4096;
    void addLight(const DynamicLight& l) { dynamicLights.push_back(l); }
    // Interior volumes + portals for this frame (cleared after rendering); see InteriorVolume
    std::vector<InteriorVolume> interiorVolumes;
    std::vector<InteriorPortal> interiorPortals;
    gfx::Buffer interiorBuf, portalBuf, lightVolumeBuf;
    static const int kMaxInteriorVolumes = 32, kMaxInteriorPortals = 128;
    std::vector<vec4> interiorGpu, portalGpu;
    std::vector<u32> lightVolumeFrame;
    void uploadInteriors();

    // Gameplay effects. All positions in world space; cheap to call every frame (requests are queued and
    // consumed by the GPU particle / decal systems during render()).
    void spawnParticles(ParticleType type, dvec3 pos, vec3 dir, int count, float scale = 1.f, vec3 tint = vec3(1));
    void addDecal(DecalType type, dvec3 pos, vec3 normal, float size, float angle);
    void addTracer(dvec3 from, dvec3 to);
    // Continuous tire strip: pass the same trackId every frame while the wheel skids; a new id starts a new strip.
    void addSkidMark(int trackId, dvec3 pos, vec3 normal, float width, float intensity);
    ParticleSystem* particles = nullptr;
    DecalSystem* decals = nullptr;
    gfx::VertexShader vsFullscreen;

    void createTargets();
    void releaseTargets();
    void updateFrameConstants(const Camera& cam, const Environment& env, float dt);
    void computeSunAndSky(const Environment& env);
    void bindFrame();  // binds FrameCB to all stages
    void fxDemo(float dt);  // --fxdemo: stages particles/decals in front of the camera (visual testing)
};

}  // namespace Render
