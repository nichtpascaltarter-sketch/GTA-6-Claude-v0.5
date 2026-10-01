// Foliage card textures (albedo + alpha), generated at startup.
#include "common.hlsli"

RWTexture2DArray<float4> uFoliage : register(u0);
cbuffer FoliageCB : register(b1) {
    uint gFolLayer;
    uint gFolSize;
    uint2 gFolPad;
};

float leafShape(float2 p, float2 c, float ang, float len, float wid) {
    float2 d = p - c;
    float s, co;
    sincos(ang, s, co);
    float2 q = float2(co * d.x + s * d.y, -s * d.x + co * d.y);
    // pointed ellipse
    float t = saturate(q.x / len);
    float w = wid * sin(t * PI) * (1.0 - t * 0.35);
    return (q.x > 0 && q.x < len && abs(q.y) < w) ? 1.0 - abs(q.y) / max(w, 1e-4) : 0.0;
}

float4 palmFrond(float2 uv) {
    // u across (0.5 = rachis), v along (0 tip at top of texture? we use v = 1 - t in mesh: v=1 base, v=0 tip)
    float t = 1.0 - uv.y;
    float x = uv.x - 0.5;
    float rachis = abs(x) < 0.03 * (1.0 - t * 0.7) ? 1.0 : 0.0;
    // leaflets: angled blades from the rachis toward the tip
    float n = 34.0;
    float side = x < 0 ? -1.0 : 1.0;
    float ax = abs(x);
    float along = t + ax * 0.55;  // slant toward tip
    float f = frac(along * n);
    float id = floor(along * n);
    float bladeW = 0.55 * (1.0 - smoothstep(0.0, 0.5, ax)) + 0.18;
    float blade = step(f, bladeW) * step(ax, 0.5 * sin(saturate(t * 1.05) * PI * 0.95 + 0.08) + 0.02);
    float tipCut = hashF((uint)id * 7u + (side > 0 ? 3u : 1u)) * 0.12;
    blade *= step(ax, 0.48 - tipCut);
    float a = max(rachis, blade);
    float3 green = lerp(float3(0.08, 0.17, 0.04), float3(0.16, 0.26, 0.06), f);
    green = lerp(green, float3(0.35, 0.3, 0.12), smoothstep(0.35, 0.5, ax) * 0.6);
    if (rachis > 0.5) green = float3(0.35, 0.32, 0.15);
    return float4(green, a);
}

float4 leafCluster(float2 uv, bool flowers, bool mangrove) {
    float a = 0;
    float3 col = 0;
    float2 c = uv - 0.5;
    float radial = length(c);
    [loop] for (int i = 0; i < 70; i++) {
        uint h = (uint)i * 2654435761u + 12345u;
        float r = sqrt(hashF(h)) * 0.42;
        float ang = hashF(h + 1u) * TWO_PI;
        float2 lc = 0.5 + float2(cos(ang), sin(ang)) * r;
        float la = ang + (hashF(h + 2u) - 0.5) * 1.8;
        float len = mangrove ? 0.13 : 0.11;
        float wid = mangrove ? 0.045 : 0.035;
        float l = leafShape(uv, lc, la, len * (0.8 + hashF(h + 3u) * 0.5), wid);
        if (l > 0) {
            a = 1;
            float shade = 0.6 + 0.4 * l;
            float3 lcCol = lerp(float3(0.06, 0.14, 0.03), float3(0.14, 0.24, 0.05), hashF(h + 4u));
            if (mangrove) lcCol = lerp(float3(0.05, 0.12, 0.04), float3(0.1, 0.18, 0.06), hashF(h + 4u));
            col = lcCol * shade;
        }
    }
    if (flowers) {
        [loop] for (int j = 0; j < 30; j++) {
            uint h = (uint)j * 747796405u + 999u;
            float2 fc = 0.5 + (float2(hashF(h), hashF(h + 1u)) - 0.5) * 0.8;
            float d = length(uv - fc);
            if (d < 0.035) { a = 1; col = lerp(float3(0.75, 0.05, 0.35), float3(0.95, 0.35, 0.6), hashF(h + 2u)) * (1.0 - d * 10.0); }
        }
    }
    a *= step(radial, 0.5);
    return float4(col, a);
}

float4 pineNeedles(float2 uv) {
    float a = 0;
    float3 col = float3(0.08, 0.16, 0.05);
    [loop] for (int i = 0; i < 60; i++) {
        uint h = (uint)i * 2246822519u + 7u;
        float ang = hashF(h) * TWO_PI;
        float len = 0.25 + hashF(h + 1u) * 0.22;
        float2 dir = float2(cos(ang), sin(ang));
        float2 d = uv - 0.5;
        float along = dot(d, dir);
        float across = abs(dot(d, float2(-dir.y, dir.x)));
        if (along > 0 && along < len && across < 0.006 + 0.004 * (1.0 - along / len)) { a = 1; col = lerp(float3(0.07, 0.15, 0.05), float3(0.13, 0.22, 0.07), hashF(h + 2u)); }
    }
    return float4(col, a);
}

float4 grassBlades(float2 uv) {
    float a = 0;
    float3 col = 0;
    float t = 1.0 - uv.y;  // 0 bottom .. 1 top
    [loop] for (int i = 0; i < 40; i++) {
        uint h = (uint)i * 3266489917u + 5u;
        float x0 = hashF(h);
        float lean = (hashF(h + 1u) - 0.5) * 0.5;
        float hgt = 0.5 + hashF(h + 2u) * 0.5;
        float x = x0 + lean * t * t;
        float w = 0.012 * (1.0 - t / hgt);
        if (t < hgt && abs(uv.x - x) < w) {
            a = 1;
            col = lerp(float3(0.12, 0.13, 0.05), float3(0.35, 0.3, 0.12), t / hgt);
            col *= 0.8 + hashF(h + 3u) * 0.4;
        }
    }
    return float4(col, a);
}

[numthreads(8, 8, 1)]
void csFoliage(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gFolSize || id.y >= gFolSize) return;
    float2 uv = (id.xy + 0.5) / gFolSize;
    float4 c;
    if (gFolLayer == 0) c = palmFrond(uv);
    else if (gFolLayer == 1) c = leafCluster(uv, false, false);
    else if (gFolLayer == 2) c = pineNeedles(uv);
    else if (gFolLayer == 3) c = grassBlades(uv);
    else if (gFolLayer == 4) c = leafCluster(uv, false, true);
    else c = leafCluster(uv, true, false);
    uFoliage[uint3(id.xy, gFolLayer)] = float4(linearToSrgb(saturate(c.rgb)), c.a);
}
