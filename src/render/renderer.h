// Frame renderer: deferred PBR pipeline with atmosphere, shadows and post-processing.
#pragma once
#include "../gfx/gfx.h"

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
};

struct ShadowConstants {
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;   // far distance of each cascade
    vec4 cascadeTexel;    // world size of a texel per cascade
    vec4 shadowParams;    // x map resolution, y cascade count, z fade start, w unused
    vec4 pad;
};

struct Settings {
    float renderScale = 1.f;
    int shadowRes = 2048;
    int shadowCascades = 4;
    bool vsync = true;
    bool ssao = true;
    bool ssr = true;
    bool taa = true;
    bool bloom = true;
    bool clouds = true;
    bool volumetrics = true;
    bool motionBlur = true;
    float fovDeg = 60.f;
    int quality = 2;  // 0 low, 1 medium, 2 high, 3 ultra
};

struct DrawStats {
    int drawCalls = 0;
    int triangles = 0;
    int terrainNodes = 0;
    void reset() { drawCalls = triangles = terrainNodes = 0; }
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

class Renderer {
public:
    bool init(int outW, int outH);
    void shutdown();
    void resize(int outW, int outH);
    void setWorld(World::WorldMap* map);
    // Render one frame to the back buffer.
    void render(const Camera& cam, const Environment& env, float dt);

    Settings settings;
    DrawStats stats;
    FrameConstants frame;
    gfx::CBuffer<FrameConstants> frameCB;
    gfx::CBuffer<ShadowConstants> shadowCB;

    // Targets (internal render resolution)
    int width = 0, height = 0;       // render resolution
    int outWidth = 0, outHeight = 0; // output resolution
    gfx::Texture depth;
    gfx::Texture gbAlbedo, gbNormal, gbMaterial, gbEmissive, gbVelocity;
    gfx::Texture hdr;
    gfx::Texture cloudsTex;  // cloud color+transmittance at quarter res (placeholder clear until clouds run)

    // Camera state
    Camera camera;
    dvec3 prevCamPos;
    mat4 prevViewProjNoJitter;
    mat4 viewRel, proj, projJitter, viewProj, viewProjNoJitter;
    vec2 jitter, prevJitter;
    u32 frameIndex = 0;
    int debugView = 0;
    bool cameraCut = false;  // set for one frame on camera teleports: snaps exposure and resets history
    float exposure = 1e-4f;  // current exposure multiplier (pre-exposure)
    float ev100 = 14.f;
    vec3 sunDir, lightTOA;
    bool moonLight = false;
    float nightFactor = 0.f;

    SkySystem* sky = nullptr;
    TerrainRenderer* terrain = nullptr;
    ShadowSystem* shadows = nullptr;
    PostSystem* post = nullptr;
    MaterialLibrary* materials = nullptr;
    WorldRenderer* world = nullptr;
    World::WorldMap* map = nullptr;

    ID3D11ComputeShader* csLighting = nullptr;
    gfx::VertexShader vsFullscreen;

    void createTargets();
    void releaseTargets();
    void updateFrameConstants(const Camera& cam, const Environment& env, float dt);
    void computeSunAndSky(const Environment& env);
    void bindFrame();  // binds FrameCB to all stages
};

}  // namespace Render
