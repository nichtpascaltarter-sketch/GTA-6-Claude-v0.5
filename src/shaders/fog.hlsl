// Froxel volumetric fog: height fog + humidity + rain haze lit by the sun (CSM + cloud shadows -> god rays),
// sky ambient and local lights (halos), with temporal reprojection, then front-to-back integration.
// Volume layout: x/y screen, z exponential view-depth slices from gFogParams1.z to gFogParams0.w.
#include "skycommon.hlsli"
#include "shadow.hlsli"
#include "lights.hlsli"

StructuredBuffer<LightGPU> tLights : register(t0);
Texture3D<float4> tFogHistory : register(t1);
Texture3D<float4> tInjected : register(t2);
Texture3D<float> tFogNoise : register(t3);       // tileable 64^3 value noise (generated at init)
RWTexture3D<float4> uFog : register(u0);
RWTexture3D<float> uFogNoise : register(u1);

// Enterable interiors (same records as the lighting pass, see InteriorVolume in renderer.h): inside a volume the
// fog is indoor air (thin, lit by the room ambient instead of the sky) and only the volume's own lamps scatter.
struct FogInterior {
    float4 c;      // xyz center (camera-relative)
    float4 axis;   // xy unit x axis
    float4 he;     // xyz half extents
    float4 amb;    // rgb room ambient radiance
};
StructuredBuffer<FogInterior> tFogInteriors : register(t4);
StructuredBuffer<uint> tFogLightVolume : register(t5);   // 0 outdoors, k + 1 inside volume k

cbuffer FogCB : register(b1) {
    float4 gFogVol;    // xyz volume size, w history valid
    float4 gFogJit;    // xyz jitter (froxel units), w light count
    float4 gFogMedia;  // x base humidity (1/m), y humidity scale height (m), z rain haze (1/m), w noise strength
    float4 gFogMisc;   // xy wind offset (m), z local light anisotropy, w ambient boost (lightning, 1 = none)
    float4 gFogInt;    // x interior volume count
};

int fogInteriorAt(float3 relPos) {
    uint n = (uint)gFogInt.x;
    [loop] for (uint k = 0; k < n; k++) {
        FogInterior v = tFogInteriors[k];
        float3 d = relPos - v.c.xyz;
        float lx = dot(d.xy, v.axis.xy), ly = dot(d.xy, float2(-v.axis.y, v.axis.x));
        if (abs(lx) <= v.he.x && abs(ly) <= v.he.y && abs(d.z) <= v.he.z) return (int)k;
    }
    return -1;
}

float sliceDepth(float w) { return gFogParams1.z * exp2(w / gFogParams1.y); }  // w in [0,1] -> view depth

float phaseHG(float g, float c) { return (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5)); }

float mediaDensity(float3 worldP) {
    float h = worldP.z;
    float fog = gFogParams0.x * exp(-max(h - gFogParams0.z, 0.0) * gFogParams0.y);
    float humid = gFogMedia.x * exp(-max(h, 0.0) / gFogMedia.y);
    float rain = gFogMedia.z * exp(-max(h, 0.0) / 600.0);
    // 64^3 tileable noise covering 256 m horizontally (2 octaves baked in)
    float3 np = float3(worldP.xy + gFogMisc.xy, worldP.z * 2.0) / 256.0;
    float n = tFogNoise.SampleLevel(sLinearWrap, np, 0);
    float mod = lerp(1.0, saturate(n * 1.6 - 0.1) * 1.6, gFogMedia.w);
    return (fog + humid + rain) * mod;
}

// Single-tap cascade shadow (temporal accumulation filters it)
float froxelSunShadow(float3 relPos, float viewDepth) {
    if (gSunDir.w <= 0.0) return 0.0;
    int count = (int)gShadowParams.y;
    [loop] for (int c = 0; c < count; c++) {
        if (viewDepth < gCascadeSplits[c]) {
            float4 sp = mul(gCascadeVP[c], float4(relPos + gSunDir.xyz * gCascadeTexel[c] * 2.0, 1));
            float2 suv = sp.xy * float2(0.5, -0.5) + 0.5;
            if (any(suv < 0.0) || any(suv > 1.0) || sp.z > 1.0) return 1.0;
            return gShadowMap.SampleCmpLevelZero(sShadowCmp, float3(suv, c), sp.z);
        }
    }
    return 1.0;
}

groupshared uint gsFogLights[64];
groupshared uint gsFogLightCount;

[numthreads(8, 8, 1)]
void csFogInject(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex, uint3 gid : SV_GroupID) {
    float3 size = gFogVol.xyz;
    // Tile light culling (8x8 froxels of one slice)
    if (gi == 0) gsFogLightCount = 0;
    GroupMemoryBarrierWithGroupSync();
    float z0 = sliceDepth(gid.z / size.z), z1 = sliceDepth((gid.z + 1.0) / size.z);
    if (z0 < 420.0) {
        float2 t0 = (gid.xy * 8.0) / size.xy, t1 = min((gid.xy * 8.0 + 8.0) / size.xy, 1.0);
        float3 c00 = reconstructPos(float2(t0.x, t0.y), 1.0), c10 = reconstructPos(float2(t1.x, t0.y), 1.0);
        float3 c01 = reconstructPos(float2(t0.x, t1.y), 1.0), c11 = reconstructPos(float2(t1.x, t1.y), 1.0);
        float3 center = normalize(c00 + c10 + c01 + c11);
        float3 pl[4];
        pl[0] = normalize(cross(c00, c10));
        pl[1] = normalize(cross(c10, c11));
        pl[2] = normalize(cross(c11, c01));
        pl[3] = normalize(cross(c01, c00));
        [unroll] for (int k = 0; k < 4; k++) if (dot(pl[k], center) < 0) pl[k] = -pl[k];
        uint n = (uint)gFogJit.w;
        for (uint li = gi; li < n; li += 64) {
            LightGPU L = tLights[li];
            float vz = dot(L.pos, gCamForward.xyz);
            if (vz + L.radius < z0 || vz - L.radius > z1) continue;
            bool vis = true;
            [unroll] for (int k2 = 0; k2 < 4; k2++) vis = vis && dot(pl[k2], L.pos) > -L.radius;
            if (!vis) continue;
            uint slot;
            InterlockedAdd(gsFogLightCount, 1, slot);
            if (slot < 64) gsFogLights[slot] = li;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (any((float3)id >= size)) return;

    float3 f = id + 0.5 + gFogJit.xyz;
    float2 uv = f.xy / size.xy;
    float viewDepth = sliceDepth(f.z / size.z);
    float3 dir = normalize(reconstructPos(uv, 1.0));
    float3 relPos = dir * (viewDepth / max(dot(dir, gCamForward.xyz), 0.05));
    float3 worldP = relPos + gCamPos.xyz;
    int vol = gFogInt.x > 0.5 ? fogInteriorAt(relPos) : -1;
    float sigmaT = mediaDensity(worldP) * (vol >= 0 ? 0.35 : 1.0);
    float sigmaS = sigmaT * 0.96;
    float3 V = dir;
    // sun / moon (indoors only where it shines through openings: the cascades see the walls and roof)
    float3 sunE = mainLightIlluminance();
    float sh = froxelSunShadow(relPos, viewDepth) * cloudShadowAt(relPos);
    float3 Lin = sunE * sh * phaseHG(gFogParams1.x, dot(gSunDir.xyz, V));
    // ambient (isotropic): sky outdoors, the room's own ambient indoors
    if (vol >= 0) Lin += tFogInteriors[vol].amb.rgb;
    else Lin += (evalSH9(float3(0, 0, 1)) * 0.65 + evalSH9(float3(0, 0, -1)) * 0.35) * gFogMisc.w;
    // local lights
    uint nl = min(gsFogLightCount, 64u);
    for (uint i = 0; i < nl; i++) {
        if (gFogInt.x > 0.5 && tFogLightVolume[gsFogLights[i]] != (uint)(vol + 1)) continue;   // lamps stay in their volume
        LightGPU L = tLights[gsFogLights[i]];
        float3 Lv = L.pos - relPos;
        float d2 = dot(Lv, Lv);
        if (d2 > L.radius * L.radius) continue;
        float d = sqrt(d2);
        Lv /= d;
        float x = d / L.radius;
        float win = saturate(1.0 - x * x * x * x);
        float att = win * win / max(d2, 0.5);
        att *= lightAngular(L, Lv);
        Lin += L.color * att * phaseHG(gFogMisc.z, dot(-Lv, V));
    }
    float4 cur = float4(sigmaS * Lin * preExposure(), sigmaT);
    // temporal reprojection (volume position of this point last frame)
    if (gFogVol.w > 0.5) {
        float4 pc = mul(gPrevViewProj, float4(relPos, 1));
        if (pc.w > 0.0) {
            float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
            float pw = log2(max(pc.w, gFogParams1.z) / gFogParams1.z) * gFogParams1.y;
            if (all(puv > 0.0) && all(puv < 1.0) && pw < 1.0) {
                float4 h = tFogHistory.SampleLevel(sLinearClamp, float3(puv, pw), 0);
                h.rgb *= prevExposureRatio();
                cur = lerp(h, cur, 0.12);
            }
        }
    }
    uFog[id] = cur;
}

// Front-to-back integration along each froxel column: rgb in-scattered radiance up to the far side of the
// slice (pre-exposed), a transmittance.
[numthreads(8, 8, 1)]
void csFogIntegrate(uint3 id : SV_DispatchThreadID) {
    float3 size = gFogVol.xyz;
    if (any((float2)id.xy >= size.xy)) return;
    float2 uv = (id.xy + 0.5) / size.xy;
    float3 dir = normalize(reconstructPos(uv, 1.0));
    float rayScale = 1.0 / max(dot(dir, gCamForward.xyz), 0.05);
    float3 L = 0;
    float T = 1.0;
    float prevZ = 0.0;
    uint n = (uint)size.z;
    for (uint z = 0; z < n; z++) {
        float z1 = sliceDepth((z + 1.0) / size.z);
        float ds = (z1 - prevZ) * rayScale;
        prevZ = z1;
        float4 m = tInjected[uint3(id.xy, z)];
        float st = max(m.a, 1e-7);
        float trans = exp(-st * ds);
        L += T * (m.rgb - m.rgb * trans) / st;
        T *= trans;
        uFog[uint3(id.xy, z)] = float4(L, T);
    }
}

// Tileable value noise volume for the fog density variation (two octaves, periods 8 and 16 cells).
float tileHash(int3 c, int period) { c = (c % period + period) % period; return hashF(hash3u(asuint(c))); }
float tileValueNoise(float3 p, int period) {
    int3 i = (int3)floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = tileHash(i, period), n100 = tileHash(i + int3(1, 0, 0), period);
    float n010 = tileHash(i + int3(0, 1, 0), period), n110 = tileHash(i + int3(1, 1, 0), period);
    float n001 = tileHash(i + int3(0, 0, 1), period), n101 = tileHash(i + int3(1, 0, 1), period);
    float n011 = tileHash(i + int3(0, 1, 1), period), n111 = tileHash(i + int3(1, 1, 1), period);
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y), lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}
[numthreads(4, 4, 4)]
void csFogNoise(uint3 id : SV_DispatchThreadID) {
    float3 p = (id + 0.5) / 64.0;
    float n = tileValueNoise(p * 8.0, 8) * 0.65 + tileValueNoise(p * 16.0 + 0.37, 16) * 0.35;
    uFogNoise[id] = n;
}
