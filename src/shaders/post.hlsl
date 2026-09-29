// Post processing: auto exposure, tonemapping, color grading, output.
#include "common.hlsli"

Texture2D<float4> tHDR : register(t0);
Texture2D<float4> tBloom : register(t1);
StructuredBuffer<float> tLumPartial : register(t2);
RWStructuredBuffer<float> uLumPartial : register(u0);
RWStructuredBuffer<float4> uExposure : register(u1);

cbuffer PostCB : register(b1) {
    float4 gPost0;  // x exposure compensation EV, y adaptation speed up, z speed down, w dt
    float4 gPost1;  // x bloom strength, y vignette, z grain, w saturation
    float4 gPost2;  // x contrast, y warmth, z min EV, w max EV
    float4 gPost3;  // x partial count, y, z, w
};

// 1) Partial reduction: each 16x16 group averages log luminance over a 64x64 region (4x4 subsample).
//    Output per group: (sum of weighted log2 luminance, sum of weights).
groupshared float2 gsLum[256];
[numthreads(16, 16, 1)]
void csLumReduce(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    uint2 base = gid.xy * 64 + tid.xy * 4;
    float2 acc = 0;
    if (base.x < (uint)gScreen.x && base.y < (uint)gScreen.y) {
        float2 uv = (base + 2.0) * gScreen.zw;
        float3 c = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
        float lum = luminance(c) / max(gExposureBuf[0].x, 1e-12);
        float2 d = uv - 0.5;
        float w = lerp(0.3, 1.0, saturate(1.0 - dot(d, d) * 2.5));
        if (!(lum >= 0.0) || lum > 1e9) { lum = 1.0; w = 0.0; }  // NaN / Inf guard
        acc = float2(log2(max(lum, 1e-4)) * w, w);
    }
    gsLum[gi] = acc;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 128; s > 0; s >>= 1) {
        if (gi < s) gsLum[gi] += gsLum[gi + s];
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) {
        uint groupsX = (uint)ceil(gScreen.x / 64.0);
        uint idx = gid.y * groupsX + gid.x;
        uLumPartial[idx * 2] = gsLum[0].x;
        uLumPartial[idx * 2 + 1] = gsLum[0].y;
    }
}

// 2) Final: average partials, adapt exposure. gPost3.y > 0.5 means "camera cut": snap exposure.
[numthreads(1, 1, 1)]
void csExposure() {
    uint n = (uint)gPost3.x;
    float s = 0, ws = 0;
    for (uint i = 0; i < n; i++) { s += tLumPartial[i * 2]; ws += tLumPartial[i * 2 + 1]; }
    float avgLog = s / max(ws, 1e-6);
    float avgLum = exp2(avgLog);
    float targetEV = log2(max(avgLum, 1e-4) * 100.0 / 12.5) - gPost0.x;
    targetEV = clamp(targetEV, gPost2.z, gPost2.w);
    float4 prev = uExposure[0];
    float ev = prev.y;
    if (prev.w < 0.5 || gPost3.y > 0.5 || !(ev == ev)) ev = targetEV;
    float speed = targetEV > ev ? gPost0.y : gPost0.z;
    ev = lerp(ev, targetEV, 1.0 - exp(-speed * gPost0.w));
    float exposure = 1.0 / (1.2 * exp2(ev));
    uExposure[0] = float4(exposure, ev, avgLum, 1);
}

// ------------------------------------------------------------------------------------------------
struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
VSOut vsFullscreen(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}

float3 acesFitted(float3 v) {
    // Stephen Hill's fit of the ACES RRT+ODT
    const float3x3 inM = float3x3(0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777);
    const float3x3 outM = float3x3(1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602);
    v = mul(inM, v);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    v = a / b;
    return saturate(mul(outM, v));
}

float4 psTonemap(VSOut i) : SV_Target {
    float3 c = tHDR.SampleLevel(sLinearClamp, i.uv, 0).rgb;
    c += tBloom.SampleLevel(sLinearClamp, i.uv, 0).rgb * gPost1.x;
    // White balance / warmth (sub-tropical grade) and saturation
    c *= float3(1.0 + gPost2.y * 0.06, 1.0, 1.0 - gPost2.y * 0.08);
    float l = luminance(c);
    c = max(lerp(l, c, gPost1.w), 0.0);
    c = acesFitted(c * 1.0);
    // contrast around mid grey in display space
    c = saturate((c - 0.5) * gPost2.x + 0.5);
    // vignette
    float2 d = i.uv - 0.5;
    c *= 1.0 - gPost1.y * dot(d, d) * 1.6;
    float3 outc = linearToSrgb(c);
    // film grain + dither to hide banding
    float n = ign(i.pos.xy, gTime.z) - 0.5;
    outc += n * (gPost1.z + 1.0 / 255.0);
    return float4(outc, 1);
}
