// Post processing: auto exposure, tonemapping, color grading, output.
#include "common.hlsli"

Texture2D<float4> tHDR : register(t0);
Texture2D<float4> tBloom : register(t1);
RWStructuredBuffer<uint> uLumHist : register(u0);   // 64-bin log2 luminance histogram (fixed-point weights)
RWStructuredBuffer<float4> uExposure : register(u1);

cbuffer PostCB : register(b1) {
    float4 gPost0;  // x exposure compensation EV, y adaptation speed up, z speed down, w dt
    float4 gPost1;  // x bloom strength, y vignette, z grain, w saturation
    float4 gPost2;  // x contrast, y warmth, z min EV, w max EV
    float4 gPost3;  // x unused, y camera cut, z sharpen, w unused
    // Gameplay screen effects (Renderer::postFx)
    float4 gFx0;    // x saturation, y vignette, z chromatic aberration, w flash
    float4 gFx1;    // rgb tint, w blur
    float4 gFx2;    // rgb vignette color, w underwater
    float4 gFx3;    // rgb flash color, w extra grain
};
Texture2D<float> tSceneDepth : register(t5);

// Auto exposure from a luminance histogram (GTA-style metering): a trimmed geometric mean for the mid-tones plus a
// highlight constraint that keeps the bright end (95th percentile: sunlit streets, bright sky) below the tonemapper's
// shoulder, so a dark foreground cannot blow out the sunlit majority and a bright sky cannot crush a street.
static const float kHistMin = -12.0;   // log2 luminance of bin 0 (cd/m2)
static const float kHistScale = 2.0;   // bins per log2 unit (64 bins cover -12 .. +20)

// 1) Histogram: each 16x16 group bins a 64x64 region (4x4 subsample), center / lower-screen weighted.
groupshared uint gsHist[64];
[numthreads(16, 16, 1)]
void csLumHist(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    if (gi < 64) gsHist[gi] = 0;
    GroupMemoryBarrierWithGroupSync();
    uint2 base = gid.xy * 64 + tid.xy * 4;
    if (base.x < (uint)gScreen.x && base.y < (uint)gScreen.y) {
        float2 uv = (base + 2.0) * gScreen.zw;
        float3 c = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
        float lum = luminance(c) / max(gExposureBuf[0].x, 1e-12);
        float2 d = uv - 0.5;
        float w = lerp(0.35, 1.0, saturate(1.0 - dot(d, d) * 2.5));
        w *= lerp(0.6, 1.0, smoothstep(0.05, 0.55, uv.y));   // the upper screen (mostly sky) meters a bit less
        if (!(lum >= 0.0) || lum > 1e9) w = 0.0;               // NaN / Inf guard
        if (gRenderParams.w > 0.5 && uv.x >= gRenderParams.y) w = 0.0;   // debug view area does not drive exposure
        if (w > 0.0) {
            uint bin = (uint)clamp((log2(max(lum, 1e-6)) - kHistMin) * kHistScale, 0.0, 63.0);
            InterlockedAdd(gsHist[bin], (uint)(w * 64.0 + 0.5));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < 64 && gsHist[gi] > 0) InterlockedAdd(uLumHist[gi], gsHist[gi]);
}

float histLog(float bin) { return (bin + 0.5) / kHistScale + kHistMin; }

// 2) Metering + adaptation (clears the histogram for the next frame). gPost3.y > 0.5: camera cut, snap exposure.
[numthreads(1, 1, 1)]
void csExposure() {
    float h[64];
    float total = 0;
    [unroll] for (int b = 0; b < 64; b++) {
        h[b] = (float)uLumHist[b];
        total += h[b];
        uLumHist[b] = 0;
    }
    float4 prev = uExposure[0];
    if (total <= 0.0) return;
    // trimmed geometric mean between the 8th and 94th percentiles
    float lo = total * 0.08, hi = total * 0.94;
    float cum = 0, s = 0, ws = 0, p95 = histLog(63.0);
    bool p95Found = false;
    [unroll] for (int k = 0; k < 64; k++) {
        float a = cum, bnd = cum + h[k];
        float take = max(min(bnd, hi) - max(a, lo), 0.0);
        s += histLog((float)k) * take;
        ws += take;
        if (!p95Found && bnd >= total * 0.95) {
            p95 = histLog((float)k) + (h[k] > 0.0 ? (total * 0.95 - a) / h[k] - 0.5 : 0.0) / kHistScale;
            p95Found = true;
        }
        cum = bnd;
    }
    float avgLog = ws > 0.0 ? s / ws : histLog(31.0);
    float avgLum = exp2(avgLog);
    float targetEV = log2(max(avgLum, 1e-4) * 100.0 / 12.5) - gPost0.x;
    // highlight constraint: the 95th percentile should land at or below ~3.2 (pre-exposed), where the filmic curve
    // still shows texture; the mid-tones may darken towards that, but only partly (70%)
    float evHL = p95 - log2(1.2 * 3.2);
    if (evHL > targetEV) targetEV = lerp(targetEV, evHL, 0.7);
    targetEV = clamp(targetEV, gPost2.z, gPost2.w);
    float ev = prev.y;
    if (prev.w < 0.5 || gPost3.y > 0.5 || !(ev == ev)) ev = targetEV;
    float speed = targetEV > ev ? gPost0.y : gPost0.z;
    ev = lerp(ev, targetEV, 1.0 - exp(-speed * gPost0.w));
    float exposure = 1.0 / (1.2 * exp2(ev));
    // Keep the exposure the frame was rendered with: history buffers (TAA output, scene color pyramid) are
    // pre-exposed with it and get rescaled by prevExposureRatio() next frame.
    uExposure[1] = float4(prev.w < 0.5 ? exposure : prev.x, 0, 0, 0);
    uExposure[0] = float4(exposure, ev, avgLum, 1);
}

// ------------------------------------------------------------------------------------------------
// Bloom: 13-tap downsample (with Karis average on the first level) and tent upsample.
Texture2D<float4> tBloomSrc : register(t3);
Texture2D<float4> tBloomLow : register(t4);
RWTexture2D<float4> uBloomDst : register(u2);
cbuffer BloomCB : register(b2) {
    float4 gBloom;  // xy dst size, z first level (karis), w upsample radius
};
float3 karis(float3 c) { return c / (1.0 + luminance(c)); }

[numthreads(8, 8, 1)]
void csBloomDown(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gBloom.x || id.y >= (uint)gBloom.y) return;
    float2 texel = 1.0 / (gBloom.xy * 2.0);
    float2 uv = (id.xy + 0.5) / gBloom.xy;
    float3 a = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-2, -2), 0).rgb;
    float3 b = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(0, -2), 0).rgb;
    float3 c = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(2, -2), 0).rgb;
    float3 d = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-2, 0), 0).rgb;
    float3 e = tBloomSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
    float3 f = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(2, 0), 0).rgb;
    float3 g = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-2, 2), 0).rgb;
    float3 h = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(0, 2), 0).rgb;
    float3 i = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(2, 2), 0).rgb;
    float3 j = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-1, -1), 0).rgb;
    float3 k = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(1, -1), 0).rgb;
    float3 l = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-1, 1), 0).rgb;
    float3 m = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(1, 1), 0).rgb;
    float3 r;
    if (gBloom.z > 0.5) {
        float3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25, g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
        float w0 = 1.0 / (1.0 + luminance(g0)), w1 = 1.0 / (1.0 + luminance(g1)), w2 = 1.0 / (1.0 + luminance(g2)), w3 = 1.0 / (1.0 + luminance(g3)), w4 = 1.0 / (1.0 + luminance(g4));
        r = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5) / (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
    } else {
        r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    uBloomDst[id.xy] = float4(min(r, 30000.0), 1);
}

[numthreads(8, 8, 1)]
void csBloomUp(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gBloom.x || id.y >= (uint)gBloom.y) return;
    float2 uv = (id.xy + 0.5) / gBloom.xy;
    float2 t = gBloom.w / gBloom.xy;
    float3 s = tBloomLow.SampleLevel(sLinearClamp, uv + float2(-t.x, -t.y), 0).rgb;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(0, -t.y), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(t.x, -t.y), 0).rgb;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(-t.x, 0), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv, 0).rgb * 4.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(t.x, 0), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(-t.x, t.y), 0).rgb;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(0, t.y), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(t.x, t.y), 0).rgb;
    s /= 16.0;
    float3 cur = tBloomSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
    uBloomDst[id.xy] = float4(cur + s, 1);
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

// Disc blur of the HDR image (gameplay blur / underwater softening), radius in pixels.
float3 discBlur(float2 uv, float radius) {
    float3 s = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
    float2 px = radius / gScreen.xy;
    [unroll] for (int k = 0; k < 12; k++) {
        float a = k * 2.39996323;
        float r = sqrt((k + 0.5) / 12.0);
        s += tHDR.SampleLevel(sLinearClamp, uv + float2(cos(a), sin(a)) * r * px, 0).rgb;
    }
    return s / 13.0;
}

float4 psTonemap(VSOut i) : SV_Target {
    float2 uv = i.uv;
    float uw = gFx2.w;
    if (uw > 0.0) {
        // underwater refraction wobble
        uv += float2(sin(uv.y * 38.0 + gTime.x * 2.1), cos(uv.x * 31.0 + gTime.x * 1.7)) * 0.0022 * uw;
    }
    float3 c = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
    // Debug views (--debugview N) are shown linearly, without grading
    if (gRenderParams.w > 0.5 && i.uv.x >= gRenderParams.y) {
        if (gRenderParams.y > 0.0 && i.uv.x < gRenderParams.y + 1.5 / gScreen.x) return float4(1, 1, 0, 1);  // split divider
        return float4(linearToSrgb(saturate(c)), 1);
    }
    // light sharpening (compensates TAA softness)
    float2 px = 1.0 / gScreen.xy;
    float3 nb = tHDR.SampleLevel(sLinearClamp, uv + float2(px.x, 0), 0).rgb + tHDR.SampleLevel(sLinearClamp, uv - float2(px.x, 0), 0).rgb +
                tHDR.SampleLevel(sLinearClamp, uv + float2(0, px.y), 0).rgb + tHDR.SampleLevel(sLinearClamp, uv - float2(0, px.y), 0).rgb;
    float3 sharp = c + (c - nb * 0.25) * gPost3.z;
    c = max(lerp(c, sharp, saturate(1.0 - luminance(c) * 0.2)), 0.0);
    // Chromatic aberration: radial red/blue split
    float ca = gFx0.z;
    if (ca > 0.0) {
        float2 dir = (uv - 0.5) * ca * 0.012;
        c.r = tHDR.SampleLevel(sLinearClamp, uv + dir, 0).r;
        c.b = tHDR.SampleLevel(sLinearClamp, uv - dir, 0).b;
    }
    float blurAmt = saturate(gFx1.w + uw * 0.25);
    if (blurAmt > 0.0) c = lerp(c, discBlur(uv, 2.0 + blurAmt * 10.0), saturate(blurAmt * 2.0));
    c = lerp(c, tBloom.SampleLevel(sLinearClamp, uv, 0).rgb, gPost1.x);
    if (uw > 0.0) {
        // Underwater: absorption with distance, blue-green scattering, soft caustic shimmer
        float d = tSceneDepth.SampleLevel(sPointClamp, uv, 0);
        float dist = d > 0.0 ? linearDepth(d) : 200.0;
        float3 trans = exp(-float3(0.45, 0.09, 0.07) * dist);
        float amb = gExposureBuf[0].z * gExposureBuf[0].x;  // average scene luminance, pre-exposed
        float3 fogC = float3(0.05, 0.32, 0.36) * max(amb, 0.02) * 2.0;
        float2 cp = uv * float2(9.0, 6.0) + gTime.x * 0.35;
        float caustic = pow(abs(sin(cp.x + sin(cp.y * 1.3)) * sin(cp.y + sin(cp.x * 1.7))), 3.0);
        float3 uwc = c * trans * (1.0 + caustic * 0.35 * saturate(1.0 - dist / 25.0)) + fogC * (1.0 - trans);
        c = lerp(c, uwc, uw);
    }
    // White balance / warmth (sub-tropical grade) and saturation
    c *= float3(1.0 + gPost2.y * 0.06, 1.0, 1.0 - gPost2.y * 0.08);
    c *= gFx1.rgb;
    float l = luminance(c);
    c = max(lerp(l, c, gPost1.w * gFx0.x), 0.0);
    // Contrast as a power curve around scene mid grey (0.18): keeps black at 0 instead of clipping the shadows
    c = 0.18 * pow(max(c / 0.18, 0.0), gPost2.x);
    c = acesFitted(c);
    // vignette (grading) + gameplay colored vignette
    float2 d = i.uv - 0.5;
    float r2 = dot(d, d);
    c *= 1.0 - gPost1.y * r2 * 1.6;
    if (gFx0.y > 0.0) c = lerp(c, gFx2.rgb, saturate(gFx0.y * smoothstep(0.05, 0.5, r2 * 2.0)));
    // flash
    c = lerp(c, gFx3.rgb, gFx0.w);
    float3 outc = linearToSrgb(saturate(c));
    // film grain + dither to hide banding
    float n = ign(i.pos.xy, gTime.z) - 0.5;
    outc += n * (gPost1.z + gFx3.w * 0.08 + 1.0 / 255.0);
    return float4(outc, 1);
}
