// Screen-space reflections: stochastic (GGX VNDF) rays traced at half resolution against the HiZ pyramid,
// colored from the previous frame's HDR pyramid (roughness-dependent mip), then a full-resolution bilateral
// resolve with temporal accumulation. Output rgb = reflected radiance (pre-exposed), a = confidence.
#include "gbuffer.hlsli"
#include "ssrtrace.hlsli"

Texture2D<float> tDepth : register(t0);          // full-res depth
Texture2D<float2> tNormal : register(t1);        // full-res normal
Texture2D<float4> tMaterial : register(t2);      // full-res material (roughness, metal, model)
Texture2D<float2> tHiZ : register(t3);           // depth pyramid (closest/farthest)
Texture2D<float4> tSceneColor : register(t4);    // previous frame HDR pyramid
Texture2D<float4> tTrace : register(t5);         // half-res trace result (resolve pass)
Texture2D<float4> tHistory : register(t6);       // previous resolved SSR
Texture2D<float2> tVelocity : register(t7);
Texture2D<float> tHalfDepth : register(t8);
Texture2D<float2> tHalfNormal : register(t9);
RWTexture2D<float4> uOut : register(u0);

cbuffer SSRCB : register(b1) {
    float4 gSSR0;   // x max iterations, y max mip, z max distance (m), w max roughness
    float4 gSSR1;   // x history valid, y color pyramid mips, z sub-pixel offset x, w sub-pixel offset y
};

float roughnessFade(float rough) { return saturate((gSSR0.w - rough) / max(gSSR0.w * 0.25, 1e-3)); }

[numthreads(8, 8, 1)]
void csSSRTrace(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gHalfScreen.xy)) return;
    int2 pix = min(int2(id.xy) * 2 + int2(gSSR1.zw), int2(gScreen.xy) - 1);
    float depth = tDepth[pix];
    float4 mat = tMaterial[pix];
    float rough = max(mat.r, 0.02);
    uint model = (uint)(mat.b * 255.0 + 0.5);
    if (model == SM_CARPAINT) rough = min(rough, 0.04);   // the clear coat dominates the reflection
    if (depth <= 0.0 || rough > gSSR0.w || model == SM_UNLIT || model == SM_FOLIAGE || model == SM_HAIR) {
        uOut[id.xy] = 0;
        return;
    }
    float2 uv = (pix + 0.5) * gScreen.zw;
    float3 P = reconstructPos(uv, depth);
    float3 V = -normalize(P);
    float3 N = octDecode(tNormal[pix] * 2.0 - 1.0);
    // Sample a reflection direction from the visible-normal distribution (mirror for very smooth surfaces). A frame
    // without history to average the stochastic rays over (the first after a cut, or one rendered after many skipped
    // frames) traces the mirror ray instead: its colour is still fetched from the cone footprint's blurrier mip, so
    // the reflection is smooth and glossy at once instead of a scatter of hit and missed rays.
    float3 R;
    float alpha = rough * rough;
    if (rough < 0.06 || gSSR1.x < 0.5) {
        R = reflect(-V, N);
    } else {
        float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        float3 T = normalize(cross(up, N)), B = cross(N, T);
        float3 Vt = float3(dot(V, T), dot(V, B), dot(V, N));
        uint h = hash3u(uint3(pix, (uint)gTime.z));
        float2 u = float2(hashF(h), hashF(h ^ 0x9e3779b9u));
        u.x *= 0.75;  // trim the long GGX tail: less noise, slightly narrower lobe
        float3 Ht = sampleGGXVNDF(Vt, alpha, u);
        float3 H = T * Ht.x + B * Ht.y + N * Ht.z;
        R = reflect(-V, H);
        if (dot(R, N) <= 0.0) R = reflect(-V, N);
    }
    // the ray starts a hair above the surface (0.25 % of the view distance): leaving a receding surface (ground
    // seen at a low angle) it would otherwise meet the depth plane of the next texel's nearest point of the same
    // surface, a false hit that ended most rays from wet ground (dark holes and smeared self-reflections)
    float3 Ps = P + N * (0.0025 * linearDepth(depth) + 0.002);
    ScreenRay ray = makeScreenRay(Ps, R, gSSR0.z);
    float3 hit;
    float iterFrac;
    bool found = traceHiZ(tHiZ, ray, gHalfScreen.xy, (int)gSSR0.y, (int)gSSR0.x, hit, iterFrac);
    float4 result = 0;
    if (found && all(hit.xy > 0.0) && all(hit.xy < 1.0)) {
        int2 hp = min(int2(hit.xy * gScreen.xy), int2(gScreen.xy) - 1);
        float hd = tDepth[hp];
        if (hd > 0.0) {
            // thickness test in linear depth: the ray must be at the surface, not behind it
            float zr = linearDepth(max(hit.z, 1e-7)), zs = linearDepth(hd);
            float thick = max(0.25, zs * 0.035);
            float3 HP = reconstructPos(hit.xy, hd);
            float3 hN = octDecode(tNormal[hp] * 2.0 - 1.0);
            float3 toHit = HP - P;
            float hitDist = length(toHit);
            // the surface must face the ray, and the ray must travel into it: a ray rising off wet ground can pass
            // within the depth tolerance of ground further on, but it cannot hit an upward-facing surface
            bool front = dot(hN, toHit) < 0.0 && dot(hN, R) < 0.05;
            if (abs(zr - zs) < thick && front && hitDist > 0.05) {
                // previous-frame color at the hit point
                float4 pc = mul(gPrevViewProj, float4(HP, 1));
                float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
                if (pc.w > 0.0 && all(puv > 0.0) && all(puv < 1.0)) {
                    // cone footprint -> pyramid mip (pyramid mip 0 is half resolution)
                    float coneTan = alpha * 0.7;
                    float footprintPx = hitDist * coneTan / max(linearDepth(depth) + hitDist, 1.0) * gScreen.y * 0.5;
                    float mip = clamp(log2(max(footprintPx, 1.0)), 0.0, gSSR1.y - 1.0);
                    float3 col = tSceneColor.SampleLevel(sLinearClamp, puv, mip).rgb * prevExposureRatio();
                    float2 edge = saturate(min(hit.xy, 1.0 - hit.xy) / 0.08);
                    float conf = edge.x * edge.y;
                    conf *= saturate((1.0 - iterFrac) * 4.0);
                    conf *= 1.0 - saturate((hitDist - gSSR0.z * 0.6) / (gSSR0.z * 0.4));
                    conf *= roughnessFade(rough);
                    result = float4(col, conf);
                }
            }
        }
    }
    uOut[id.xy] = float4(min(result.rgb, 60000.0), result.a);
}

// ------------------------------------------------------------------------------------------------
// Full-resolution resolve: roughness-scaled bilateral gather of the half-res rays + temporal accumulation.
[numthreads(8, 8, 1)]
void csSSRResolve(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gScreen.xy)) return;
    float depth = tDepth[id.xy];
    float4 mat = tMaterial[id.xy];
    float rough = max(mat.r, 0.02);
    if ((uint)(mat.b * 255.0 + 0.5) == SM_CARPAINT) rough = min(rough, 0.04);
    if (depth <= 0.0 || rough > gSSR0.w) { uOut[id.xy] = 0; return; }
    float2 uv = (id.xy + 0.5) * gScreen.zw;
    float z = linearDepth(depth);
    float3 N = octDecode(tNormal[id.xy] * 2.0 - 1.0);
    float2 hp = (id.xy + 0.5) * 0.5 - 0.5;
    int2 b = (int2)floor(hp);
    int2 mx = int2(gHalfScreen.xy) - 1;
    int radius = rough < 0.1 ? 1 : 2;
    float4 sum = 0;
    float wsum = 0;
    float4 m1 = 0, m2 = 0;
    float mn = 0;
    [loop] for (int y = -radius + 1; y <= radius; y++)
    [loop] for (int x = -radius + 1; x <= radius; x++) {
        int2 q = clamp(b + int2(x, y), int2(0, 0), mx);
        float4 s = tTrace[q];
        float qz = tHalfDepth[q];
        float3 qn = octDecode(tHalfNormal[q] * 2.0 - 1.0);
        float2 dq = (q + 0.5) * 2.0 - (id.xy + 0.5);
        float spatial = exp(-dot(dq, dq) / (2.0 * sq(0.8 + rough * 6.0)));
        float w = spatial * exp(-abs(qz - z) / (0.02 * z + 0.02)) * pow(saturate(dot(qn, N)), 16.0);
        sum += float4(s.rgb * s.a, s.a) * w;
        wsum += w;
        m1 += s;
        m2 += s * s;
        mn += 1;
    }
    float4 cur = 0;
    if (wsum > 1e-5) {
        cur.a = sum.a / wsum;
        cur.rgb = sum.a > 1e-5 ? sum.rgb / sum.a : 0;
    }
    m1 /= mn;
    float4 sigma = sqrt(max(m2 / mn - m1 * m1, 0.0));
    float4 result = cur;
    if (gSSR1.x > 0.5) {
        float3 rel = reconstructPos(uv, depth);
        float4 pc = mul(gPrevViewProj, float4(rel, 1));
        float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
        float2 vel = tVelocity[id.xy];
        if (any(vel != 0) && length(vel - (uv - puv)) > 0.002) puv = uv - vel;
        if (pc.w > 0.0 && all(puv > 0.0) && all(puv < 1.0)) {
            float4 h = tHistory.SampleLevel(sLinearClamp, puv, 0);
            h.rgb *= prevExposureRatio();
            float4 lo = m1 - sigma * 1.5 - 0.02, hi = m1 + sigma * 1.5 + 0.02;
            h = clamp(h, min(lo, cur), max(hi, cur));
            // smooth surfaces: reflections move with parallax, keep the history short
            float blend = lerp(0.35, 0.1, saturate(rough / 0.3));
            result = lerp(h, cur, blend);
        }
    }
    uOut[id.xy] = float4(max(result.rgb, 0.0), saturate(result.a));
}
