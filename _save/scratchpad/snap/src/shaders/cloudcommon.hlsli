// Volumetric cloud layer: parameters, noise textures and the density function shared by the cloud ray-march,
// the cloud shadow map and the reflection probe's sky.
#ifndef CLOUDCOMMON_HLSLI
#define CLOUDCOMMON_HLSLI
#include "skycommon.hlsli"

Texture3D<float4> tShapeNoise : register(t0);
Texture3D<float4> tDetailNoise : register(t1);
Texture2D<float4> tWeather : register(t2);

cbuffer CloudCB : register(b1) {
    float4 gCloud0;   // x coverage, y density, z bottom (m), w top (m)
    float4 gCloud1;   // xy wind offset (m), z time, w frame
    float4 gCloud2;   // x trace width, y trace height, z history valid, w rain darkening
    float4 gCloud3;   // x storm (0..1), y ray-march step scale, zw sub-pixel offset of this frame's trace
};

float remap(float v, float l0, float h0, float l1, float h1) { return l1 + (v - l0) * (h1 - l1) / (h0 - l0); }

// ------------------------------------------------------------------------------------------------
float heightFraction(float z) { return saturate((z - gCloud0.z) / (gCloud0.w - gCloud0.z)); }
float heightGradient(float hf, float type) {
    // stratus: thin bottom band; cumulus: rounded; cumulonimbus: tall
    float st = saturate(remap(hf, 0.0, 0.08, 0.0, 1.0)) * saturate(remap(hf, 0.1, 0.25, 1.0, 0.0));
    float cu = saturate(remap(hf, 0.0, 0.12, 0.0, 1.0)) * saturate(remap(hf, 0.35, 0.65, 1.0, 0.0));
    float cb = saturate(remap(hf, 0.0, 0.1, 0.0, 1.0)) * saturate(remap(hf, 0.75, 1.0, 1.0, 0.0));
    return type < 0.5 ? lerp(st, cu, type * 2.0) : lerp(cu, cb, type * 2.0 - 1.0);
}
float4 weatherAt(float2 xy) {
    float2 uv = (xy + gCloud1.xy) / 38000.0;
    return tWeather.SampleLevel(sLinearWrap, uv, 0);
}
float cloudDensity(float3 p, bool detail) {
    float hf = heightFraction(p.z);
    if (hf <= 0.0 || hf >= 1.0) return 0;
    float4 w = weatherAt(p.xy);
    float coverage = saturate(w.r * 0.85 + gCloud0.x * 1.05 - 0.62);
    float type = saturate(w.g * 0.7 + gCloud0.x * 0.5 - 0.1);
    float3 sp = (p + float3(gCloud1.xy * 1.2, 0)) / 4200.0;
    float4 sn = tShapeNoise.SampleLevel(sLinearWrap, sp, 0);
    float lowFreq = sn.g * 0.625 + sn.b * 0.25 + sn.a * 0.125;
    float base = remap(sn.r, lowFreq - 1.0, 1.0, 0.0, 1.0);
    base *= heightGradient(hf, type);
    float d = saturate(remap(base, 1.0 - coverage, 1.0, 0.0, 1.0)) * coverage;
    if (detail && d > 0.0) {
        float3 dp = (p + float3(gCloud1.xy * 2.0, gCloud1.z * 3.0)) / 900.0;
        float3 dn = tDetailNoise.SampleLevel(sLinearWrap, dp, 0).rgb;
        float df = dn.r * 0.625 + dn.g * 0.25 + dn.b * 0.125;
        df = lerp(df, 1.0 - df, saturate(hf * 4.0));
        d = saturate(remap(d, df * 0.22, 1.0, 0.0, 1.0));
    }
    return d * gCloud0.y;
}

float hgPhase(float g, float c) { return (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5)); }


#endif
