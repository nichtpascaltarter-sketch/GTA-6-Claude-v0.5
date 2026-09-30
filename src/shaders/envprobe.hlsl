// Dynamic reflection probe around the camera: lighting of the captured cube faces (small G-buffer rendered with
// the regular material shaders) and GGX prefiltering into roughness mips. Values are radiance (not exposed).
#include "cloudcommon.hlsli"
#include "gbuffer.hlsli"
#include "shadow.hlsli"
#include "lights.hlsli"

Texture2D<float4> tPAlbedo : register(t3);
Texture2D<float2> tPNormal : register(t4);
Texture2D<float4> tPMaterial : register(t5);
Texture2D<float3> tPEmissive : register(t6);
Texture2D<float> tPDepth : register(t7);
TextureCube<float4> tSourceCube : register(t8);
StructuredBuffer<LightGPU> tProbeLights : register(t9);   // outdoor world lights, positions relative to the probe
RWTexture2DArray<float4> uDestCube : register(u0);
RWStructuredBuffer<float4> uProbeSH : register(u1);

cbuffer ProbeCB : register(b2) {
    float4 gProbe0;   // x face, y resolution, z roughness (prefilter), w destination size (prefilter)
    float4 gProbe1;   // x source mip count, y source resolution, z sample count, w local light count (capture)
    float4 gProbe2;   // xyz capture position relative to the main camera (shadow cascades are camera-relative)
};

static const float3 kFaceF[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
static const float3 kFaceR[6] = {float3(0, 0, -1), float3(0, 0, 1), float3(1, 0, 0), float3(1, 0, 0), float3(1, 0, 0), float3(-1, 0, 0)};
static const float3 kFaceU[6] = {float3(0, 1, 0), float3(0, 1, 0), float3(0, 0, -1), float3(0, 0, 1), float3(0, 1, 0), float3(0, 1, 0)};

// Unnormalized direction of cube texel uv (component along the face axis = 1).
float3 faceDir(int face, float2 uv) { return kFaceF[face] + (2.0 * uv.x - 1.0) * kFaceR[face] + (1.0 - 2.0 * uv.y) * kFaceU[face]; }

// Sun shadow for arbitrary directions around the camera: first cascade whose projection contains the point.
float probeShadow(float3 relPos, float3 N) {
    if (gSunDir.w <= 0.0) return 0.0;
    int count = (int)gShadowParams.y;
    [loop] for (int c = 0; c < count; c++) {
        float3 p = relPos + N * gCascadeTexel[c] * 2.0 + gSunDir.xyz * gCascadeTexel[c];
        float4 sp = mul(gCascadeVP[c], float4(p, 1));
        float2 suv = sp.xy * float2(0.5, -0.5) + 0.5;
        if (all(suv > 0.01) && all(suv < 0.99) && sp.z < 1.0 && sp.z > 0.0) return sampleCascade(c, relPos, N, 0.0);
    }
    return 1.0;
}

// Cheap cloud layer for reflections: 8 density samples through the slab, analytic lighting.
float4 probeClouds(float3 dir) {
    if (dir.z < 0.01) return float4(0, 0, 0, 1);
    float4 cirrus = cirrusLayer(dir);
    if (gCloud0.x < 0.01) return cirrus;
    float camZ = gCamPos.z;
    float z0 = gCloud0.z, z1 = gCloud0.w;
    float t0 = max((z0 - camZ) / dir.z, 0.0), t1 = min(max((z1 - camZ) / dir.z, 0.0), 40000.0);
    if (t1 <= t0) return cirrus;
    const int N = 8;
    float dt = (t1 - t0) / N;
    float3 sunL = sunLightAtAltitude(0.5 * (z0 + z1));
    float cosT = dot(dir, gSunDir.xyz);
    float phase = lerp(hgPhase(-0.2, cosT), hgPhase(0.7, cosT), 0.5);
    float3 amb = evalSH9(float3(0, 0, 1)) * PI;
    float sigma = 0.018;
    float T = 1.0;
    float3 L = 0;
    [loop] for (int i = 0; i < N; i++) {
        float3 p = float3(gCamPos.xy, camZ) + dir * (t0 + (i + 0.5) * dt);
        float d = cloudDensity(p, false);
        if (d <= 0.001) continue;
        float hf = heightFraction(p.z);
        float lightT = exp(-d * sigma * 180.0) * 0.8 + 0.2 * hf;
        float3 S = (sunL * lightT * phase + amb * (0.25 + 0.4 * hf)) * d * sigma;
        float st = exp(-d * sigma * dt);
        L += T * (S - S * st) / max(d * sigma, 1e-6);
        T *= st;
        if (T < 0.03) break;
    }
    float fade = exp(-(t0 + t1) * 0.5 / 42000.0);
    L *= (1.0 - gCloud2.w * 0.6) * fade;
    float Tc = lerp(1.0, T, fade);
    return float4(L + cirrus.rgb * Tc, Tc * cirrus.a);
}

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 psProbeLight(VSOut i) : SV_Target {
    int face = (int)gProbe0.x;
    float res = gProbe0.y;
    float2 uv = i.pos.xy / res;
    float3 dirU = faceDir(face, uv);
    float3 dir = normalize(dirU);
    // The face was rendered with a right-handed view (mirrored horizontally relative to the cube convention)
    int2 gp = int2(res - 1.0 - floor(i.pos.x), floor(i.pos.y));
    float depth = tPDepth[gp];
    float3 L;
    if (depth <= 0.0) {
        L = skyRadiance(dir, false);
        float4 cl = probeClouds(dir);
        L = L * cl.a + cl.rgb;
    } else {
        float viewZ = gCamPos.w / depth;
        float3 relPos = dirU * viewZ;
        GBufferData g = unpackGBuffer(tPAlbedo[gp], tPNormal[gp], tPMaterial[gp]);
        float3 N = g.normal;
        float3 diff = g.albedo * (1.0 - g.metal);
        float NoL = saturate(dot(N, gSunDir.xyz));
        float shadow = NoL > 0.0 ? probeShadow(relPos + gProbe2.xyz, N) * cloudShadowAt(relPos) : 0.0;
        float3 sunE = mainLightIlluminance();
        L = diff / PI * sunE * NoL * shadow + diff * evalSH9(N);
        // Street lamps / neon / building lights (diffuse): lit streets and facades in night reflections and in
        // the probe irradiance that feeds the ambient light
        uint nl = (uint)gProbe1.w;
        [loop] for (uint li = 0; li < nl; li++) {
            LightGPU Lt = tProbeLights[li];
            float3 Lv = Lt.pos - relPos;
            float d2 = dot(Lv, Lv);
            if (d2 > Lt.radius * Lt.radius) continue;
            float d = sqrt(d2);
            Lv /= d;
            float x = d / Lt.radius;
            float win = saturate(1.0 - x * x * x * x);
            L += diff / PI * Lt.color * (win * win / max(d2, 0.3)) * saturate(dot(N, Lv)) * lightAngular(Lt, Lv);
        }
        if (g.shadingModel == SM_UNLIT) L = g.albedo;
        // Glossy dielectric/metal sky reflection (keeps windows and metal from looking chalky in reflections)
        float3 R = reflect(dir, N);
        float3 f0 = lerp(0.04, g.albedo, g.metal);
        float fr = (1.0 - g.rough) * (1.0 - g.rough);
        L += skyRadiance(normalize(float3(R.xy, max(R.z, 0.02))), false) * (f0 + (1.0 - f0) * pow5(1.0 - saturate(dot(N, -dir)))) * fr * 0.8;
        L += tPEmissive[gp] / max(preExposure(), 1e-12);
        // atmospheric haze with distance (probe directions are not covered by the aerial perspective froxels)
        float dist = viewZ * length(dirU);
        float T = exp(-dist * gPlanetParams.w / 16000.0);
        float3 horizon = skyRadiance(normalize(float3(dir.xy, 0.03)), false);
        L = L * T + horizon * (1.0 - T);
    }
    // never let a NaN / Inf into the cube: the SH below is blended over time, so a single bad texel would poison
    // the near-camera ambient for good (min() does not reliably drop NaN on every driver)
    return float4(sanitizeHDR(min(L, 60000.0)), 1);
}

// ------------------------------------------------------------------------------------------------
// GGX prefilter (split-sum, N = V = R) with filtered importance sampling from the captured cube's mips.
float radicalInverse(uint b) {
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return float(b) * 2.3283064365386963e-10;
}

[numthreads(8, 8, 1)]
void csPrefilter(uint3 id : SV_DispatchThreadID) {
    float size = gProbe0.w;
    if (id.x >= (uint)size || id.y >= (uint)size) return;
    int face = (int)id.z;
    float3 N = normalize(faceDir(face, (id.xy + 0.5) / size));
    float rough = gProbe0.z;
    float a = max(rough * rough, 1e-3);
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N)), B = cross(N, T);
    uint count = (uint)gProbe1.z;
    float srcRes = gProbe1.y;
    float texelSA = 4.0 * PI / (6.0 * srcRes * srcRes);
    float3 sum = 0;
    float wsum = 0;
    [loop] for (uint k = 0; k < count; k++) {
        float2 xi = float2((k + 0.5) / count, radicalInverse(k));
        float phi = TWO_PI * xi.x;
        float cosTh = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinTh = sqrt(saturate(1.0 - cosTh * cosTh));
        float3 H = T * (sinTh * cos(phi)) + B * (sinTh * sin(phi)) + N * cosTh;
        float3 L = 2.0 * dot(N, H) * H - N;
        float NoL = dot(N, L);
        if (NoL <= 0.0) continue;
        // pdf of L = D * NoH / (4 VoH) = D / 4 for N = V
        float NoH = cosTh;
        float D = D_GGX(NoH, a);
        float pdf = D * 0.25 + 1e-6;
        float sampleSA = 1.0 / (count * pdf);
        float mip = clamp(0.5 * log2(sampleSA / texelSA) + 1.0, 0.0, gProbe1.x - 1.0);
        sum += tSourceCube.SampleLevel(sLinearClamp, L, mip).rgb * NoL;
        wsum += NoL;
    }
    uDestCube[uint3(id.xy, face)] = float4(sum / max(wsum, 1e-5), 1);
}

// Project the finished probe onto SH9 (irradiance of the camera's surroundings), blended with the previous
// projection (gProbe0.z = blend towards the new value). Single group of 64 threads, 1024 directions.
groupshared float3 gsPSH[64][9];
[numthreads(64, 1, 1)]
void csProbeSH(uint gi : SV_GroupIndex) {
    float3 acc[9];
    [unroll] for (int k = 0; k < 9; k++) acc[k] = 0;
    [loop] for (int s = 0; s < 16; s++) {
        uint idx = gi * 16 + s;
        float u = ((idx % 32) + 0.5) / 32.0, v = ((idx / 32) + 0.5) / 32.0;
        float phi = u * TWO_PI;
        float cosT = 1.0 - 2.0 * v;
        float sinT = sqrt(saturate(1.0 - cosT * cosT));
        float3 d = float3(sinT * cos(phi), sinT * sin(phi), cosT);
        float3 L = sanitizeHDR(min(tSourceCube.SampleLevel(sLinearClamp, d, gProbe1.x).rgb, 30000.0));
        float w = 4.0 * PI / 1024.0;
        acc[0] += L * 0.282095 * w;
        acc[1] += L * 0.488603 * d.y * w;
        acc[2] += L * 0.488603 * d.z * w;
        acc[3] += L * 0.488603 * d.x * w;
        acc[4] += L * 1.092548 * d.x * d.y * w;
        acc[5] += L * 1.092548 * d.y * d.z * w;
        acc[6] += L * 0.315392 * (3.0 * d.z * d.z - 1.0) * w;
        acc[7] += L * 1.092548 * d.x * d.z * w;
        acc[8] += L * 0.546274 * (d.x * d.x - d.y * d.y) * w;
    }
    [unroll] for (int k2 = 0; k2 < 9; k2++) gsPSH[gi][k2] = acc[k2];
    GroupMemoryBarrierWithGroupSync();
    if (gi < 9) {
        float3 sum = 0;
        for (int t = 0; t < 64; t++) sum += gsPSH[t][gi];
        float4 prev = uProbeSH[gi];
        float3 res = anyNonFinite(prev.rgb) ? sum : lerp(prev.rgb, sum, gProbe0.z);   // uninitialised / bad history
        uProbeSH[gi] = float4(anyNonFinite(res) ? float3(0, 0, 0) : res, 0);
    }
}
