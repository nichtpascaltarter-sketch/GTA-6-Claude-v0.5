// Temporal anti-aliasing with velocity reprojection and YCoCg variance clipping.
#include "common.hlsli"

Texture2D<float4> tCurrent : register(t0);
Texture2D<float4> tHistory : register(t1);
Texture2D<float2> tVelocity : register(t2);
Texture2D<float> tDepth : register(t3);
Texture2D<float> tReactive : register(t4);   // particles / rain coverage: favor the current frame
RWTexture2D<float4> uOut : register(u0);

cbuffer TAACB : register(b1) {
    float4 gTAA;  // x: reset history (1), y: blend factor, z: sharpen, w: unused
};

float3 rgbToYCoCg(float3 c) {
    return float3(c.r * 0.25 + c.g * 0.5 + c.b * 0.25, c.r * 0.5 - c.b * 0.5, -c.r * 0.25 + c.g * 0.5 - c.b * 0.25);
}
float3 yCoCgToRgb(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

float3 tonemapW(float3 c) { return c / (1.0 + luminance(c)); }
float3 untonemapW(float3 c) { return c / max(1.0 - luminance(c), 1e-4); }

// Catmull-Rom 5-tap history sample
float3 sampleHistory(float2 uv) {
    float2 texSize = gScreen.xy;
    float2 pos = uv * texSize;
    float2 c = floor(pos - 0.5) + 0.5;
    float2 f = pos - c;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 tc12 = (c + w2 / w12) / texSize;
    float2 tc0 = (c - 1.0) / texSize;
    float2 tc3 = (c + 2.0) / texSize;
    float3 r = tHistory.SampleLevel(sLinearClamp, float2(tc12.x, tc0.y), 0).rgb * (w12.x * w0.y) +
               tHistory.SampleLevel(sLinearClamp, float2(tc0.x, tc12.y), 0).rgb * (w0.x * w12.y) +
               tHistory.SampleLevel(sLinearClamp, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y) +
               tHistory.SampleLevel(sLinearClamp, float2(tc3.x, tc12.y), 0).rgb * (w3.x * w12.y) +
               tHistory.SampleLevel(sLinearClamp, float2(tc12.x, tc3.y), 0).rgb * (w12.x * w3.y);
    float wsum = (w12.x * w0.y) + (w0.x * w12.y) + (w12.x * w12.y) + (w3.x * w12.y) + (w12.x * w3.y);
    return max(r / wsum, 0.0);
}

[numthreads(8, 8, 1)]
void csTAA(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gScreen.x || id.y >= (uint)gScreen.y) return;
    int2 p = int2(id.xy);
    float2 uv = (id.xy + 0.5) * gScreen.zw;
    // Neighborhood statistics + closest depth for velocity dilation
    float3 m1 = 0, m2 = 0;
    float3 cur = 0;
    float closest = 0;
    int2 closestP = p;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        int2 q = clamp(p + int2(x, y), int2(0, 0), int2(gScreen.xy) - 1);
        float3 c = tonemapW(sanitizeHDR(tCurrent[q].rgb));   // forward passes (water, particles, glass) write here too
        float3 ycc = rgbToYCoCg(c);
        m1 += ycc;
        m2 += ycc * ycc;
        if (x == 0 && y == 0) cur = c;
        float d = tDepth[q];
        if (d > closest) { closest = d; closestP = q; }
    }
    m1 /= 9.0;
    m2 /= 9.0;
    float3 sigma = sqrt(max(m2 - m1 * m1, 0.0));
    float3 bmin = m1 - sigma * 1.25, bmax = m1 + sigma * 1.25;

    float2 vel;
    float depth = tDepth[p];
    if (closest <= 0.0) {
        // sky: reproject direction at infinity
        float3 rel = reconstructPos(uv, 1e-6);
        float4 prev = mul(gPrevViewProj, float4(normalize(rel) * 1e5, 1));
        float2 prevUV = prev.xy / prev.w * float2(0.5, -0.5) + 0.5;
        vel = uv - prevUV;
    } else {
        vel = tVelocity[closestP];
        // Depth-based reprojection for static scene (also covers water whose depth overwrote the G-buffer)
        float3 rel = reconstructPos((closestP + 0.5) * gScreen.zw, closest);
        float4 prev = mul(gPrevViewProj, float4(rel, 1));
        float2 prevUV = prev.xy / prev.w * float2(0.5, -0.5) + 0.5;
        float2 vStatic = (closestP + 0.5) * gScreen.zw - prevUV;
        if (length(vel - vStatic) < 0.002) vel = vStatic;
        else if (all(vel == 0)) vel = vStatic;
    }
    float2 prevUV = uv - vel;
    bool offscreen = any(prevUV < 0.0) || any(prevUV > 1.0);
    float3 hist = tonemapW(sanitizeHDR(sampleHistory(prevUV)));
    float3 hy = rgbToYCoCg(hist);
    // Clip history toward the neighborhood mean
    float3 center = (bmin + bmax) * 0.5, ext = (bmax - bmin) * 0.5 + 1e-5;
    float3 d = hy - center;
    float3 ad = abs(d / ext);
    float ma = max(ad.x, max(ad.y, ad.z));
    if (ma > 1.0) hy = center + d / ma;
    hist = yCoCgToRgb(hy);
    float speed = length(vel * gScreen.xy);
    float blend = gTAA.y + saturate(speed * 0.02) * 0.2;
    blend = lerp(blend, 1.0, tReactive[p] * 0.9);
    if (gTAA.x > 0.5 || offscreen) blend = 1.0;
    float3 res = lerp(hist, cur, blend);
    uOut[id.xy] = float4(untonemapW(res), 1);
}
