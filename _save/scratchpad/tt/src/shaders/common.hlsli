// Shared shader definitions. Must match FrameConstants in src/render/renderer.h.
#ifndef COMMON_HLSLI
#define COMMON_HLSLI

static const float PI = 3.14159265359;
static const float TWO_PI = 6.28318530718;

cbuffer FrameCB : register(b0) {
    float4x4 gViewProj;        // camera-relative world -> clip (jittered)
    float4x4 gViewProjNoJitter;
    float4x4 gInvViewProj;     // clip -> camera-relative world (jittered)
    float4x4 gView;            // camera-relative view
    float4x4 gProj;
    float4x4 gPrevViewProj;    // previous frame, relative to the *current* camera position (no jitter)
    float4 gCamPos;            // xyz world camera position, w = near plane
    float4 gCamPosWrap;        // xyz camera position modulo 2048 (for stable texturing), w = unused
    float4 gScreen;            // width, height, 1/width, 1/height
    float4 gJitter;            // xy current jitter in pixels, zw previous
    float4 gSunDir;            // xyz direction TO the sun (or moon at night), w = sun above horizon factor
    float4 gSunColor;          // rgb illuminance of the main light at the top of the atmosphere (lux), w = moon mode (0/1)
    float4 gSkyAmbient[9];     // SH9 irradiance coefficients (rgb)
    float4 gTime;              // x game seconds, y time of day hours, z frame index, w dt
    float4 gWeather;           // x rain intensity, y surface wetness, z cloud coverage, w wind strength
    float4 gWind;              // xy wind direction, z gust, w fog density base
    float4 gFog;               // x height fog density, y height falloff, z fog start, w aerial perspective scale
    float4 gExposure;          // x exposure multiplier, y 1/exposure, z ev100, w night factor
    float4 gCamForward;        // xyz forward, w fov y
    float4 gRenderParams;      // x shadow cascade count, y debug split (screen fraction, 0 = full), z SSR enabled, w debug view
    float4 gLightning;         // x flash intensity, yzw direction
    float4 gPlanetParams;      // x unused, y unused, z camera altitude km, w mie haze multiplier
    float4 gCloudShadow;       // xy center (world), z size (m), w strength
    float4 gFogParams0;        // x ground fog density (1/m), y height falloff (1/m), z reference height (m), w froxel far (m)
    float4 gFogParams1;        // x phase anisotropy g, y 1/log2(far/near), z froxel near (m), w volumetric fog enabled
    float4 gOverhead;          // xy overhead height map min corner (world xy), z map size (m), w enabled
    float4 gEnvProbe;          // xyz probe position relative to the camera, w max mip (0 = no probe, use the sky)
    float4 gSSParams;          // x AO enabled, y GI enabled, z SSR enabled, w SSR max roughness
    float4 gWeather2;          // x overcast (0..1), y storm (0..1), z puddle amount, w ripple animation time
    float4 gHalfScreen;        // half-resolution width, height, 1/width, 1/height
    float4 gAmbientParams;     // x urban enclosure (facade share of the horizon band), y lightning ambient flash (lux)
};

// Global resources bound once per frame at high slots (see Renderer::bindGlobals)
StructuredBuffer<float4> gSkySH : register(t32);          // 9 SH irradiance coefficients (pre-exposed off)
Texture2D<float4> gTransmittanceLUT : register(t33);
Texture3D<float4> gAerialLUT : register(t34);
Texture2DArray<float> gShadowMap : register(t35);
Texture2D<float4> gSkyViewLUT : register(t36);
Texture2D<float> gCloudShadowMap : register(t37);
Texture3D<float4> gFogVolume : register(t38);             // integrated froxel fog: rgb in-scatter (pre-exposed), a transmittance
TextureCube<float4> gEnvProbeTex : register(t39);            // dynamic reflection probe (prefiltered by roughness, not exposed)
StructuredBuffer<float4> gExposureBuf : register(t40);    // [0] x = exposure multiplier, y = ev100, z = avg lum; [1] x = previous frame's exposure
Texture2D<float> gOverheadMap : register(t41);            // highest static world surface (m) around the camera (rain occlusion)
Texture2D<float4> gRippleTex : register(t42);             // rain ripple rings: xy offset to drop center, z time offset, w mask
Texture2D<float> gTerrainHeightG : register(t43);         // global terrain heightmap (m)
SamplerState sPointClamp : register(s0);
SamplerState sLinearClamp : register(s1);
SamplerState sLinearWrap : register(s2);
SamplerState sAnisoWrap : register(s3);
SamplerComparisonState sShadowCmp : register(s4);
SamplerState sPointWrap : register(s5);
SamplerState sAnisoClamp : register(s6);

float preExposure() { return gExposureBuf[0].x; }
// Converts values pre-exposed with the previous frame's exposure (history buffers) to the current exposure.
float prevExposureRatio() { return gExposureBuf[0].x / max(gExposureBuf[1].x, 1e-12); }

// ------------------------------------------------------------------------------------------------
float3 srgbToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 linearToSrgb(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055; }
float3 hsvToRgbF(float h) { return saturate(abs(frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0); }
float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float sq(float x) { return x * x; }
float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

float2 octWrap(float2 v) { return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0 : -1.0); }
float2 octEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : octWrap(n.xy);
    return n.xy;
}
float3 octDecode(float2 f) {
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += n.xy >= 0.0 ? -t : t;
    return normalize(n);
}

// Reconstruct camera-relative world position from reversed-Z depth and uv.
float3 reconstructPos(float2 uv, float depth) {
    float4 ndc = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
    float4 p = mul(gInvViewProj, ndc);
    return p.xyz / p.w;
}
// Linear view distance from reversed infinite depth: depth = near / viewZ
float linearDepth(float depth) { return gCamPos.w / max(depth, 1e-7); }

// ------------------------------------------------------------------------------------------------
// Hashes and noise (GPU)
uint hashU(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
uint hash2u(uint2 v) { return hashU(v.x * 0x8da6b343u ^ hashU(v.y + 0x9e3779b9u)); }
uint hash3u(uint3 v) { return hashU(v.x * 0x8da6b343u ^ v.y * 0xd8163841u ^ hashU(v.z + 0x7f4a7c15u)); }
float hashF(uint x) { return (hashU(x) >> 8) * (1.0 / 16777216.0); }
float hash21(float2 p) { return (hash2u(asuint(int2(floor(p)))) >> 8) * (1.0 / 16777216.0); }
float hash31(float3 p) { return (hash3u(asuint(int3(floor(p)))) >> 8) * (1.0 / 16777216.0); }
float2 hash22(float2 p) {
    uint h = hash2u(asuint(int2(floor(p))));
    return float2((h >> 8) * (1.0 / 16777216.0), (hashU(h) >> 8) * (1.0 / 16777216.0));
}

float valueNoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i), b = hash21(i + float2(1, 0)), c = hash21(i + float2(0, 1)), d = hash21(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float valueNoise3(float3 p) {
    float3 i = floor(p), f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash31(i), n100 = hash31(i + float3(1, 0, 0)), n010 = hash31(i + float3(0, 1, 0)), n110 = hash31(i + float3(1, 1, 0));
    float n001 = hash31(i + float3(0, 0, 1)), n101 = hash31(i + float3(1, 0, 1)), n011 = hash31(i + float3(0, 1, 1)), n111 = hash31(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y), lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}
float2 gradDir(float2 i) { float a = hash21(i) * TWO_PI; return float2(cos(a), sin(a)); }
float gradNoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(gradDir(i), f), b = dot(gradDir(i + float2(1, 0)), f - float2(1, 0));
    float c = dot(gradDir(i + float2(0, 1)), f - float2(0, 1)), d = dot(gradDir(i + float2(1, 1)), f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 1.4142;  // ~[-1,1]
}
float fbm(float2 p, int oct) {
    float s = 0, a = 0.5, n = 0;
    [loop] for (int i = 0; i < oct; i++) {
        s += gradNoise(p) * a; n += a; a *= 0.5;
        p = mul(float2x2(1.6, 1.2, -1.2, 1.6), p);
    }
    return s / n;
}
float fbmValue(float2 p, int oct) {
    float s = 0, a = 0.5, n = 0;
    [loop] for (int i = 0; i < oct; i++) {
        s += valueNoise(p) * a; n += a; a *= 0.5;
        p = mul(float2x2(1.6, 1.2, -1.2, 1.6), p);
    }
    return s / n;
}
// Worley F1 and cell id
float worley(float2 p, out float2 cellId) {
    float2 i = floor(p), f = frac(p);
    float best = 8.0; cellId = i;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float2 g = float2(x, y);
        float2 o = hash22(i + g);
        float2 r = g + o - f;
        float d = dot(r, r);
        if (d < best) { best = d; cellId = i + g; }
    }
    return sqrt(best);
}

// Interleaved gradient noise for dithering / sample rotation
float ign(float2 pix, float frame) {
    pix += frame * 5.588238;
    return frac(52.9829189 * frac(dot(pix, float2(0.06711056, 0.00583715))));
}

// ------------------------------------------------------------------------------------------------
// PBR
float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d + 1e-7);
}
float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (gv + gl + 1e-7);
}
float3 F_Schlick(float3 f0, float VoH) { return f0 + (1.0 - f0) * pow5(1.0 - VoH); }
float3 F_SchlickRoughness(float3 f0, float NoV, float rough) {
    return f0 + (max(1.0 - rough, f0) - f0) * pow5(1.0 - NoV);
}
// Karis' analytic approximation of the split-sum environment BRDF
float2 envBRDFApprox(float rough, float NoV) {
    const float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1, 0.0425, 1.04, -0.04);
    float4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// Irradiance (divided by PI -> outgoing radiance for albedo 1) from SH9 radiance coefficients.
float3 evalSH9(float3 n) {
    float3 r = gSkySH[0].rgb * 0.886227;
    r += gSkySH[1].rgb * 1.023328 * n.y;
    r += gSkySH[2].rgb * 1.023328 * n.z;
    r += gSkySH[3].rgb * 1.023328 * n.x;
    r += gSkySH[4].rgb * 0.858086 * n.x * n.y;
    r += gSkySH[5].rgb * 0.858086 * n.y * n.z;
    r += gSkySH[6].rgb * 0.247708 * (3.0 * n.z * n.z - 1.0);
    r += gSkySH[7].rgb * 0.858086 * n.x * n.z;
    r += gSkySH[8].rgb * 0.429043 * (n.x * n.x - n.y * n.y);
    return max(r, 0.0) / PI;
}

// Shading model ids stored in gbuffer
#define SM_DEFAULT 0
#define SM_FOLIAGE 1
#define SM_SKIN 2
#define SM_CARPAINT 3
#define SM_WATER 4
#define SM_UNLIT 5
#define SM_HAIR 6
#define SM_CLOTH 7

#endif
