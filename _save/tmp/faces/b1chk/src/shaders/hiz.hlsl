// Hierarchical depth pyramid, half-resolution G-buffer data and the scene color pyramid.
// Depth is reversed-Z (1 near, 0 far/sky): x = closest depth (max) in the cell, y = farthest depth (min).
#include "common.hlsli"

Texture2D<float> tDepth : register(t0);       // full-resolution depth (mip 0 build)
Texture2D<float2> tNormal : register(t1);     // full-resolution octahedral normal (mip 0 build)
Texture2D<float2> tSrcMip : register(t2);     // previous HiZ mip
Texture2D<float4> tColorSrc : register(t3);   // color pyramid source (full-res TAA output or previous mip)
RWTexture2D<float2> uHiZ : register(u0);
RWTexture2D<float> uHalfDepth : register(u1);  // linear view depth of the closest pixel of the 2x2 footprint
RWTexture2D<float2> uHalfNormal : register(u2);
RWTexture2D<float4> uColorDst : register(u3);

cbuffer HiZCB : register(b1) {
    uint2 gSrcSize;   // source dimensions
    uint2 gDstSize;   // destination dimensions
    float4 gHiZParams; // x: 1 = first color level (Karis average + NaN guard)
};

// Source footprint of a destination texel: 2x2, extended to 3 texels on the last row/column of odd sources.
void footprint(uint2 id, out int2 base, out int2 extent) {
    base = int2(id) * 2;
    extent = int2(2, 2);
    if ((gSrcSize.x & 1u) && id.x == gDstSize.x - 1u) extent.x = 3;
    if ((gSrcSize.y & 1u) && id.y == gDstSize.y - 1u) extent.y = 3;
}

[numthreads(8, 8, 1)]
void csHiZFirst(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= gDstSize)) return;
    int2 base, ext;
    footprint(id.xy, base, ext);
    int2 last = int2(gSrcSize) - 1;
    float mx = 0.0, mn = 1.0;
    int2 best = base;
    [unroll] for (int y = 0; y < 3; y++)
    [unroll] for (int x = 0; x < 3; x++) {
        if (x < ext.x && y < ext.y) {
            int2 p = min(base + int2(x, y), last);
            float d = tDepth[p];
            if (d > mx) { mx = d; best = p; }
            mn = min(mn, d);
        }
    }
    uHiZ[id.xy] = float2(mx, mn);
    uHalfDepth[id.xy] = mx > 0.0 ? linearDepth(mx) : 1e6;
    uHalfNormal[id.xy] = tNormal[best];
}

[numthreads(8, 8, 1)]
void csHiZDown(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= gDstSize)) return;
    int2 base, ext;
    footprint(id.xy, base, ext);
    int2 last = int2(gSrcSize) - 1;
    float mx = 0.0, mn = 1.0;
    [unroll] for (int y = 0; y < 3; y++)
    [unroll] for (int x = 0; x < 3; x++) {
        if (x < ext.x && y < ext.y) {
            float2 v = tSrcMip[min(base + int2(x, y), last)];
            mx = max(mx, v.x);
            mn = min(mn, v.y);
        }
    }
    uHiZ[id.xy] = float2(mx, mn);
}

// Scene color pyramid (previous frame's anti-aliased HDR): used by reflections (rough = blurrier mip) and GI.
float3 guard(float3 c) { return (any(isnan(c)) || any(isinf(c))) ? 0.0 : min(c, 30000.0); }

[numthreads(8, 8, 1)]
void csColorDown(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= gDstSize)) return;
    float2 uv = (id.xy + 0.5) / float2(gDstSize);
    // One source texel = half a destination texel. The first level point-samples the 4 source texels (for the
    // Karis weights); lower levels take 4 bilinear taps one source texel away (4x4 tent footprint).
    float2 t = (gHiZParams.x > 0.5 ? 0.25 : 0.5) / float2(gDstSize);
    float3 a = tColorSrc.SampleLevel(sLinearClamp, uv + float2(-t.x, -t.y), 0).rgb;
    float3 b = tColorSrc.SampleLevel(sLinearClamp, uv + float2(t.x, -t.y), 0).rgb;
    float3 c = tColorSrc.SampleLevel(sLinearClamp, uv + float2(-t.x, t.y), 0).rgb;
    float3 d = tColorSrc.SampleLevel(sLinearClamp, uv + float2(t.x, t.y), 0).rgb;
    float3 r;
    if (gHiZParams.x > 0.5) {
        a = guard(a); b = guard(b); c = guard(c); d = guard(d);
        // Luma-weighted average suppresses fireflies in blurred reflections
        float wa = 1.0 / (1.0 + luminance(a)), wb = 1.0 / (1.0 + luminance(b));
        float wc = 1.0 / (1.0 + luminance(c)), wd = 1.0 / (1.0 + luminance(d));
        r = (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
    } else {
        // Wider tent for lower mips (smoother roughness transitions)
        float3 e = tColorSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
        r = (a + b + c + d) * 0.1875 + e * 0.25;
    }
    uColorDst[id.xy] = float4(r, 1);
}
