// Volumetric clouds: noise generation, quarter-resolution ray-march with checkerboard temporal reconstruction at
// half resolution, cloud shadow map.
#include "cloudcommon.hlsli"

Texture2D<float4> tCloudHistory : register(t3);
RWTexture3D<float4> uNoise3D : register(u0);
RWTexture2D<float4> uOut2D : register(u1);


// ------------------------------------------------------------------------------------------------
// Noise generation (tileable)
float hash3p(float3 p, float P) {
    p = p - floor(p / P) * P;
    return (hash3u(asuint(int3(p))) >> 8) * (1.0 / 16777216.0);
}
float worley3(float3 p, float P) {
    float3 i = floor(p), f = frac(p);
    float best = 8.0;
    [loop] for (int z = -1; z <= 1; z++)
    [loop] for (int y = -1; y <= 1; y++)
    [loop] for (int x = -1; x <= 1; x++) {
        float3 g = float3(x, y, z);
        float3 c = i + g;
        float3 o = float3(hash3p(c, P), hash3p(c + 31.0, P), hash3p(c + 67.0, P));
        float3 d = g + o - f;
        best = min(best, dot(d, d));
    }
    return sqrt(best);
}
float perlin3p(float3 p, float P) {
    float3 i = floor(p), f = frac(p);
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float r = 0;
    float n[8];
    [unroll] for (int k = 0; k < 8; k++) {
        float3 o = float3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        float3 c = i + o;
        float h = hash3p(c, P) * TWO_PI, h2 = hash3p(c + 11.0, P) * 2.0 - 1.0;
        float3 gdir = float3(cos(h) * sqrt(1 - h2 * h2), sin(h) * sqrt(1 - h2 * h2), h2);
        n[k] = dot(gdir, f - o);
    }
    float x0 = lerp(lerp(n[0], n[1], u.x), lerp(n[2], n[3], u.x), u.y);
    float x1 = lerp(lerp(n[4], n[5], u.x), lerp(n[6], n[7], u.x), u.y);
    return lerp(x0, x1, u.z);
}
float worleyFbm(float3 p, float freq) {
    return worley3(p * freq, freq) * 0.625 + worley3(p * freq * 2.0, freq * 2.0) * 0.25 + worley3(p * freq * 4.0, freq * 4.0) * 0.125;
}

[numthreads(4, 4, 4)]
void csShapeNoise(uint3 id : SV_DispatchThreadID) {
    float3 p = (id + 0.5) / 128.0;
    float pfbm = 0, amp = 1, fr = 4, norm = 0;
    [loop] for (int o = 0; o < 4; o++) { pfbm += perlin3p(p * fr, fr) * amp; norm += amp; amp *= 0.5; fr *= 2; }
    pfbm = pfbm / norm * 0.5 + 0.5;
    float w1 = 1.0 - worleyFbm(p, 4.0);
    float pw = remap(pfbm, w1 - 1.0, 1.0, 0.0, 1.0);  // Perlin-Worley
    float w2 = 1.0 - worleyFbm(p, 8.0), w3 = 1.0 - worleyFbm(p, 16.0), w4 = 1.0 - worleyFbm(p, 32.0);
    uNoise3D[id] = float4(saturate(pw), w2, w3, w4);
}
[numthreads(4, 4, 4)]
void csDetailNoise(uint3 id : SV_DispatchThreadID) {
    float3 p = (id + 0.5) / 32.0;
    uNoise3D[id] = float4(1.0 - worleyFbm(p, 2.0), 1.0 - worleyFbm(p, 4.0), 1.0 - worleyFbm(p, 8.0), 1);
}
// Weather map: r coverage, g cloud type (0 stratus .. 1 cumulonimbus), b precipitation
[numthreads(8, 8, 1)]
void csWeather(uint3 id : SV_DispatchThreadID) {
    float2 uv = (id.xy + 0.5) / 512.0;
    float c = 0, amp = 0.5, fr = 3, norm = 0;
    [loop] for (int o = 0; o < 5; o++) {
        c += (perlin3p(float3(uv * fr, 0.5), fr) * 0.5 + 0.5) * amp;
        norm += amp; amp *= 0.55; fr *= 2;
    }
    c /= norm;
    float t = perlin3p(float3(uv * 2.0, 3.7), 2.0) * 0.5 + 0.5;
    float cells = 1.0 - worley3(float3(uv * 10.0, 0.3), 10.0);
    uOut2D[id.xy] = float4(saturate(c * 0.7 + cells * 0.45 - 0.1), t, saturate(cells * c * 1.5 - 0.4), 1);
}

// Ray-march through the cloud slab. Returns rgb = inscattered radiance (not exposed), a = transmittance.
float4 marchClouds(float3 dir, float jitter) {
    float camZ = gCamPos.z;
    if (dir.z <= -0.02 || gCloud0.x <= 0.01) return float4(0, 0, 0, 1);
    float z0 = gCloud0.z, z1 = gCloud0.w;
    float dz = max(dir.z, 0.015);
    float t0 = camZ < z0 ? (z0 - camZ) / dz : 0.0;
    float t1 = camZ < z1 ? (z1 - camZ) / dz : 0.0;
    t1 = min(t1, 60000.0);
    if (t1 <= t0) return float4(0, 0, 0, 1);
    int steps = (int)(lerp(48.0, 96.0, saturate(1.0 - dir.z * 2.0)) * gCloud3.y);
    int lightSteps = gCloud3.y < 0.8 ? 4 : 6;
    float stepLen = (t1 - t0) / steps;
    float t = t0 + stepLen * jitter;
    float3 sunDir = gSunDir.xyz;
    float cosTheta = dot(dir, sunDir);
    float phase = lerp(hgPhase(-0.25, cosTheta), hgPhase(0.75, cosTheta), 0.6);
    float3 sunL = mainLightIlluminance();
    float3 ambTop = evalSH9(float3(0, 0, 1)) * PI;
    float3 ambBot = evalSH9(float3(0, 0, -1)) * PI * 0.6;
    float3 cityUp = cityUplight() * 0.29;   // lit city below: glowing cloud bases at night
    float T = 1.0;
    float3 L = 0;
    const float sigma = 0.018;
    [loop] for (int s = 0; s < steps; s++) {
        float3 p = float3(gCamPos.xy, camZ) + dir * t;
        float d = cloudDensity(p, true);
        if (d > 0.001) {
            // light march toward the sun (growing steps)
            float od = 0;
            float ls = 25.0;
            float3 lp = p;
            [loop] for (int k = 0; k < lightSteps; k++) {
                lp += sunDir * ls;
                od += cloudDensity(lp, k < 3) * ls;
                ls *= lightSteps == 4 ? 1.9 : 1.5;
            }
            // Multiple-scattering octaves (Wrenninge-style): later octaves see less extinction
            float lightT = exp(-od * sigma) + 0.5 * exp(-od * sigma * 0.25) + 0.25 * exp(-od * sigma * 0.0625);
            float powder = 1.0 - exp(-d * stepLen * sigma * 2.0);
            float hf = heightFraction(p.z);
            float3 amb = lerp(ambBot, ambTop, hf) * (0.35 + 0.65 * hf);
            float3 S = (sunL * lightT * phase * lerp(0.7, 1.0, powder) + amb * 0.3 + cityUp * (1.0 - hf) * (1.0 - hf)) * d * sigma;
            float stepT = exp(-d * sigma * stepLen);
            L += T * (S - S * stepT) / max(d * sigma, 1e-6);
            T *= stepT;
            if (T < 0.02) break;
        }
        t += stepLen;
    }
    // Rain / storm darkening (thick, water-laden cloud bases)
    L *= (1.0 - gCloud2.w * 0.6) * (1.0 - gCloud3.x * 0.35);
    // Aerial perspective: fade toward the sky with distance
    float mid = (t0 + t1) * 0.5;
    float fade = exp(-mid / 42000.0);
    return float4(L * fade, lerp(1.0, T, fade));
}

Texture2D<float2> tHiZ : register(t5);        // half-res depth pyramid (y = farthest depth of the footprint)
Texture2D<float4> tCloudTrace : register(t6); // quarter-res trace (reconstruction input)

// Trace: one ray per 2x2 block of half-res pixels, rotating through the block over 4 frames
// (gCloud3.zw = this frame's sub-pixel). Ultra quality traces every half-res pixel (offset 0, full size).
[numthreads(8, 8, 1)]
void csCloudTrace(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gCloud2.x || id.y >= (uint)gCloud2.y) return;
    bool full = gCloud2.x >= gHalfScreen.x - 0.5;
    int2 hp = full ? int2(id.xy) : min(int2(id.xy) * 2 + int2(gCloud3.zw), int2(gHalfScreen.xy) - 1);
    float4 result = float4(0, 0, 0, 1);
    if (tHiZ.Load(int3(hp, 0)).y <= 0.0) {  // some sky in the footprint
        float2 uv = (hp + 0.5) * gHalfScreen.zw;
        float3 dir = normalize(reconstructPos(uv, 1e-5));
        result = marchClouds(dir, ign(float2(hp), gCloud1.w));
    }
    uOut2D[id.xy] = result;
}

// Reconstruction at half resolution: the freshly traced pixel of each 2x2 block is blended with its history,
// the other three reproject their history (by direction: clouds are far away), clamped to the neighborhood of
// this frame's traces.
[numthreads(8, 8, 1)]
void csCloudReconstruct(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gHalfScreen.xy)) return;
    if (tHiZ.Load(int3(id.xy, 0)).y > 0.0) { uOut2D[id.xy] = float4(0, 0, 0, 1); return; }   // no sky here
    float2 uv = (id.xy + 0.5) * gHalfScreen.zw;
    bool full = gCloud2.x >= gHalfScreen.x - 0.5;
    int2 tp = full ? int2(id.xy) : int2(id.xy) / 2;
    int2 tmax = int2(gCloud2.xy) - 1;
    bool fresh = full || all((id.xy & 1u) == (uint2)gCloud3.zw);
    float4 cur = fresh ? tCloudTrace[min(tp, tmax)] : tCloudTrace.SampleLevel(sLinearClamp, uv, 0);
    float4 mn = cur, mx = cur;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        float4 v = tCloudTrace[clamp(tp + int2(x, y), int2(0, 0), tmax)];
        mn = min(mn, v);
        mx = max(mx, v);
    }
    float4 result = cur;
    if (gCloud2.z > 0.5) {
        float3 dir = normalize(reconstructPos(uv, 1e-5));
        float4 prevClip = mul(gPrevViewProj, float4(dir * 1e5, 1));
        float2 puv = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;
        if (all(puv > 0.0) && all(puv < 1.0) && prevClip.w > 0) {
            float4 h = tCloudHistory.SampleLevel(sLinearClamp, puv, 0);
            if (fresh) {
                result = lerp(h, cur, full ? 0.12 : 0.45);
            } else {
                float4 pad = (mx - mn) * 0.25 + float4(0.02, 0.02, 0.02, 0.01) * float4(mx.rgb, 1);
                result = clamp(h, mn - pad, mx + pad);
            }
        }
    }
    uOut2D[id.xy] = anyNonFinite(result.rgb) || !(result.a >= 0.0) ? float4(0, 0, 0, 1) : float4(min(result.rgb, 60000.0), saturate(result.a));
}

// Cloud shadow map: transmittance of sunlight through the cloud layer, top-down 2D map around the camera
[numthreads(8, 8, 1)]
void csCloudShadow(uint3 id : SV_DispatchThreadID) {
    float2 uv = (id.xy + 0.5) / 256.0;
    float2 xy = gCamPos.xy + (uv - 0.5) * 8192.0;
    float3 sunDir = gSunDir.xyz;
    float sz = max(sunDir.z, 0.08);
    float od = 0;
    [loop] for (int k = 0; k < 8; k++) {
        float z = lerp(gCloud0.z, gCloud0.w, (k + 0.5) / 8.0);
        float3 p = float3(xy + sunDir.xy / sz * z, z);
        od += cloudDensity(p, false) * (gCloud0.w - gCloud0.z) / 8.0;
    }
    // Thick overcast / storm decks block the sun almost completely; fair-weather cumulus keep a little forward-
    // scattered sunlight in their shadows
    float minT = lerp(0.12, 0.01, saturate(gCloud3.x + saturate((gCloud0.x - 0.7) * 3.3)));
    uOut2D[id.xy] = float4(lerp(minT, 1.0, exp(-od * 0.018 * 0.5)), 0, 0, 1);
}
