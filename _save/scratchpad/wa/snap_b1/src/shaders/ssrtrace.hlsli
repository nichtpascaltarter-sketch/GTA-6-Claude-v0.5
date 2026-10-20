// Hierarchical-Z screen-space ray tracing against the depth pyramid built in hiz.hlsl (reversed-Z; the pyramid
// stores the closest depth per cell in .x). Shared by the SSR pass and the forward water shader.
#ifndef SSRTRACE_HLSLI
#define SSRTRACE_HLSLI
#include "common.hlsli"

struct ScreenRay {
    float3 origin;   // (uv, depth)
    float3 dir;      // (duv, ddepth) per unit t; t = 1 reaches the projected end point
    float tMax;      // t where the ray leaves the screen or the valid depth range
};

// Builds a screen-space ray for the camera-relative world ray P + R * s. maxDist limits the world length.
ScreenRay makeScreenRay(float3 P, float3 R, float maxDist) {
    ScreenRay r;
    float nearZ = gCamPos.w;
    float vz = dot(P, gCamForward.xyz);
    float rz = dot(R, gCamForward.xyz);
    float len = maxDist;
    if (rz < 0.0) len = min(len, (vz - nearZ * 2.0) / -rz * 0.98);  // stay in front of the near plane
    float4 c0 = mul(gViewProj, float4(P, 1));
    float4 c1 = mul(gViewProj, float4(P + R * max(len, 0.01), 1));
    float3 s0 = float3(c0.xy / c0.w * float2(0.5, -0.5) + 0.5, c0.z / c0.w);
    float3 s1 = float3(c1.xy / c1.w * float2(0.5, -0.5) + 0.5, c1.z / c1.w);
    r.origin = s0;
    r.dir = s1 - s0;
    // clip against the screen rectangle
    float t = 1.0;
    if (r.dir.x > 0) t = min(t, (1.0 - s0.x) / r.dir.x); else if (r.dir.x < 0) t = min(t, -s0.x / r.dir.x);
    if (r.dir.y > 0) t = min(t, (1.0 - s0.y) / r.dir.y); else if (r.dir.y < 0) t = min(t, -s0.y / r.dir.y);
    r.tMax = max(t, 0.0);
    return r;
}

// Traverses the HiZ pyramid. Returns true with the hit position (uv, depth) when the ray reaches mip 0 below a
// surface within maxIter steps. mip0Size = HiZ mip 0 resolution (half screen).
bool traceHiZ(Texture2D<float2> hiz, ScreenRay ray, float2 mip0Size, int maxMip, int maxIter, out float3 hit, out float iterFrac) {
    float3 o = ray.origin, d = ray.dir;
    float3 invD = float3(abs(d.x) > 1e-9 ? 1.0 / d.x : 1e30, abs(d.y) > 1e-9 ? 1.0 / d.y : 1e30, abs(d.z) > 1e-12 ? 1.0 / d.z : 1e30);
    float2 floorOffset = float2(d.x >= 0 ? 1.0 : 0.0, d.y >= 0 ? 1.0 : 0.0);
    float2 uvNudge = float2(d.x >= 0 ? 1.0 : -1.0, d.y >= 0 ? 1.0 : -1.0) * 0.005 / mip0Size;
    // initial step: leave the origin cell at mip 0 (avoids self intersection)
    float2 cell0 = floor(o.xy * mip0Size);
    float2 plane0 = (cell0 + floorOffset) / mip0Size + uvNudge;
    float2 t0 = (plane0 - o.xy) * invD.xy;
    float t = min(t0.x, t0.y);
    float3 pos = o + d * t;
    int mip = 0;
    int i = 0;
    [loop] for (; i < maxIter && mip >= 0; i++) {
        if (t > ray.tMax) break;
        float2 mipSize = max(floor(mip0Size / exp2((float)mip)), 1.0);
        float2 cellPos = pos.xy * mipSize;
        float surf = hiz.Load(int3(min(int2(cellPos), int2(mipSize) - 1), mip)).x;
        float2 xyPlane = (floor(cellPos) + floorOffset) / mipSize + uvNudge;
        float3 tp = (float3(xyPlane, surf) - o) * invD;
        tp.z = d.z < 0.0 ? tp.z : 1e30;       // only rays moving away from the camera can meet the depth plane
        if (tp.z <= t) tp.z = 1e30;
        float tMin = min(min(tp.x, tp.y), tp.z);
        bool above = surf < pos.z;            // ray closer to the camera than the closest surface in the cell
        bool skipped = tMin != tp.z && above;
        t = above ? tMin : t;
        pos = o + d * t;
        mip += skipped ? 1 : -1;
        mip = min(mip, maxMip);
    }
    hit = pos;
    iterFrac = (float)i / maxIter;
    return mip < 0 && t <= ray.tMax;
}

// GGX distribution of visible normals (Heitz 2018). Ve: view direction in tangent space (z = normal).
float3 sampleGGXVNDF(float3 Ve, float alpha, float2 u) {
    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);
    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;
    float t1 = r * cos(phi), t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(saturate(1.0 - t1 * t1)) + s * t2;
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(saturate(1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(Nh.z, 1e-4)));
}

#endif
