// Camera + object motion blur (tile max velocity, neighborhood max, depth-aware reconstruction filter).
#include "common.hlsli"

Texture2D<float4> tColor : register(t0);      // anti-aliased HDR (TAA output)
Texture2D<float2> tVelocity : register(t1);   // uv units, current - previous (G-buffer)
Texture2D<float> tDepth : register(t2);
Texture2D<float2> tTiles : register(t3);      // tile max (pixels)
RWTexture2D<float2> uTiles : register(u0);
RWTexture2D<float4> uOut : register(u1);

cbuffer MotionBlurCB : register(b1) {
    float4 gMB;   // x shutter scale, y max blur radius (px), z tile count x, w tile count y
};

// Screen velocity of a pixel in pixels per frame (sky/unset pixels are reprojected from camera motion).
float2 pixelVelocity(int2 p) {
    float2 v = tVelocity[p];
    float d = tDepth[p];
    float2 uv = (p + 0.5) * gScreen.zw;
    if (d <= 0.0 || all(v == 0)) {
        float3 rel = d > 0.0 ? reconstructPos(uv, d) : normalize(reconstructPos(uv, 1e-6)) * 1e5;
        float4 prev = mul(gPrevViewProj, float4(rel, 1));
        if (prev.w <= 0.0) return 0;
        float2 puv = prev.xy / prev.w * float2(0.5, -0.5) + 0.5;
        v = uv - puv;
    }
    float2 px = v * gScreen.xy * gMB.x;
    float l = length(px);
    return l > gMB.y ? px * (gMB.y / l) : px;
}

groupshared float2 gsV[256];
groupshared float gsL[256];
[numthreads(16, 16, 1)]
void csTileMax(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex, uint3 tid : SV_GroupThreadID) {
    int2 p = min(int2(gid.xy * 16 + tid.xy), int2(gScreen.xy) - 1);
    float2 v = pixelVelocity(p);
    gsV[gi] = v;
    gsL[gi] = dot(v, v);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 128; s > 0; s >>= 1) {
        if (gi < s && gsL[gi + s] > gsL[gi]) { gsL[gi] = gsL[gi + s]; gsV[gi] = gsV[gi + s]; }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) uTiles[gid.xy] = gsV[0];
}

[numthreads(8, 8, 1)]
void csNeighborMax(uint3 id : SV_DispatchThreadID) {
    int2 n = int2(gMB.zw);
    if (any(int2(id.xy) >= n)) return;
    float2 best = 0;
    float bl = -1;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float2 v = tTiles[clamp(int2(id.xy) + int2(x, y), int2(0, 0), n - 1)];
        float l = dot(v, v);
        if (l > bl) { bl = l; best = v; }
    }
    uTiles[id.xy] = best;
}

float cone(float dist, float len) { return saturate(1.0 - dist / max(len, 1e-3)); }
float cylinder(float dist, float len) { return 1.0 - smoothstep(0.95 * len, 1.05 * len, dist); }
float softDepth(float za, float zb) { return saturate(1.0 - (za - zb) / max(0.02 * min(za, zb), 0.05)); }

[numthreads(8, 8, 1)]
void csMotionBlur(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gScreen.xy)) return;
    int2 p = int2(id.xy);
    float4 c0 = tColor[p];
    float2 vn = tTiles[min(p / 16, int2(gMB.zw) - 1)];
    float ln = length(vn);
    if (ln < 0.5) { uOut[id.xy] = c0; return; }
    float2 vc = pixelVelocity(p);
    float lc = length(vc);
    float d0 = tDepth[p];
    float z0 = d0 > 0.0 ? linearDepth(d0) : 1e6;
    const int N = 12;
    float jitter = ign(float2(id.xy), gTime.z) - 0.5;
    float w0 = 1.0 / max(lc, 1.0);
    float4 sum = c0 * w0;
    float wsum = w0;
    [loop] for (int i = 0; i < N; i++) {
        float t = lerp(-1.0, 1.0, (i + 0.5 + jitter) / N);
        float2 off = vn * t * 0.5;
        int2 q = clamp(p + int2(round(off)), int2(0, 0), int2(gScreen.xy) - 1);
        float dq = tDepth[q];
        float zq = dq > 0.0 ? linearDepth(dq) : 1e6;
        float2 vq = pixelVelocity(q);
        float lq = length(vq);
        float dist = length(off);
        // background/foreground classification (McGuire et al. reconstruction filter)
        float f = softDepth(z0, zq);   // q in front of the center
        float b = softDepth(zq, z0);   // q behind the center
        float w = f * cone(dist, lq) + b * cone(dist, lc) + cylinder(dist, lq) * cylinder(dist, lc) * 2.0;
        sum += tColor[q] * w;
        wsum += w;
    }
    uOut[id.xy] = sum / max(wsum, 1e-4);
}
