// Volumetric clouds: noise generation, half-resolution raymarch with temporal accumulation, cloud shadow map.
#include "skycommon.hlsli"

Texture3D<float4> tShapeNoise : register(t0);
Texture3D<float4> tDetailNoise : register(t1);
Texture2D<float4> tWeather : register(t2);
Texture2D<float4> tCloudHistory : register(t3);
Texture2D<float> tDepthFull : register(t4);
RWTexture3D<float4> uNoise3D : register(u0);
RWTexture2D<float4> uOut2D : register(u1);

cbuffer CloudCB : register(b1) {
    float4 gCloud0;   // x coverage, y density, z bottom (m), w top (m)
    float4 gCloud1;   // xy wind offset (m), z time, w frame
    float4 gCloud2;   // x half-res width, y height, z history valid, w rain darkening
    float4 gCloud3;   // xyz camera position delta (for reprojection), w unused
};

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
float remap(float v, float l0, float h0, float l1, float h1) { return l1 + (v - l0) * (h1 - l1) / (h0 - l0); }

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

// Raymarch at half resolution. Output rgb = inscattered radiance (pre-exposed), a = transmittance.
[numthreads(8, 8, 1)]
void csClouds(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gCloud2.x || id.y >= (uint)gCloud2.y) return;
    float2 uv = (id.xy + 0.5) / gCloud2.xy;
    // Skip pixels fully covered by geometry (check 2x2 full-res footprint)
    int2 fp = int2(id.xy * 2);
    float dmax = max(max(tDepthFull[fp], tDepthFull[fp + int2(1, 0)]), max(tDepthFull[fp + int2(0, 1)], tDepthFull[fp + int2(1, 1)]));
    float dmin = min(min(tDepthFull[fp], tDepthFull[fp + int2(1, 0)]), min(tDepthFull[fp + int2(0, 1)], tDepthFull[fp + int2(1, 1)]));
    float3 dir = normalize(reconstructPos(uv, 1e-5));
    float4 result = float4(0, 0, 0, 1);
    float camZ = gCamPos.z;
    if (dmin <= 0.0 && dir.z > -0.02 && gCloud0.x > 0.01) {
        // Intersect the cloud slab (flat, with a curvature term for far horizon distances)
        float z0 = gCloud0.z, z1 = gCloud0.w;
        float dz = max(dir.z, 0.015);
        float t0 = camZ < z0 ? (z0 - camZ) / dz : 0.0;
        float t1 = camZ < z1 ? (z1 - camZ) / dz : 0.0;
        t1 = min(t1, 60000.0);
        if (t1 > t0) {
            int steps = (int)lerp(48.0, 96.0, saturate(1.0 - dir.z * 2.0));
            float stepLen = (t1 - t0) / steps;
            float jitter = ign(id.xy, gCloud1.w);
            float t = t0 + stepLen * jitter;
            float3 sunDir = gSunDir.xyz;
            float cosTheta = dot(dir, sunDir);
            float phase = lerp(hgPhase(-0.25, cosTheta), hgPhase(0.75, cosTheta), 0.6);
            float3 sunL = mainLightIlluminance();
            float3 ambTop = evalSH9(float3(0, 0, 1)) * PI;
            float3 ambBot = evalSH9(float3(0, 0, -1)) * PI * 0.6;
            float T = 1.0;
            float3 L = 0;
            float sigma = 0.018;
            float zeroCount = 0;
            [loop] for (int s = 0; s < steps; s++) {
                float3 p = float3(gCamPos.xy, camZ) + dir * t;
                float d = cloudDensity(p, true);
                if (d > 0.001) {
                    // light march toward the sun (6 samples, growing steps)
                    float od = 0;
                    float ls = 25.0;
                    float3 lp = p;
                    [loop] for (int k = 0; k < 6; k++) {
                        lp += sunDir * ls;
                        od += cloudDensity(lp, k < 3) * ls;
                        ls *= 1.5;
                    }
                    // Multiple-scattering octaves (Wrenninge-style): later octaves see less extinction
                    float lightT = exp(-od * sigma) + 0.5 * exp(-od * sigma * 0.25) + 0.25 * exp(-od * sigma * 0.0625);
                    float powder = 1.0 - exp(-d * stepLen * sigma * 2.0);
                    float hf = heightFraction(p.z);
                    float3 amb = lerp(ambBot, ambTop, hf) * (0.35 + 0.65 * hf);
                    float3 S = (sunL * lightT * phase * lerp(0.7, 1.0, powder) + amb * 0.3) * d * sigma;
                    float stepT = exp(-d * sigma * stepLen);
                    L += T * (S - S * stepT) / max(d * sigma, 1e-6);
                    T *= stepT;
                    if (T < 0.02) break;
                }
                t += stepLen;
            }
            // Rain darkening
            L *= 1.0 - gCloud2.w * 0.6;
            // Aerial perspective: fade toward the sky with distance
            float mid = (t0 + t1) * 0.5;
            float fade = exp(-mid / 42000.0);
            T = lerp(1.0, T, fade);
            L *= fade;
            result = float4(L, T);
        }
    } else if (dmax > 0.0 && dmin > 0.0) {
        result = float4(0, 0, 0, 1);
    }
    // Temporal accumulation (reproject by direction; clouds are far away)
    if (gCloud2.z > 0.5) {
        float4 prevClip = mul(gPrevViewProj, float4(dir * 1e5, 1));
        float2 puv = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;
        if (all(puv > 0.0) && all(puv < 1.0) && prevClip.w > 0) {
            float4 h = tCloudHistory.SampleLevel(sLinearClamp, puv, 0);
            result = lerp(h, result, 0.12);
        }
    }
    uOut2D[id.xy] = result;
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
    uOut2D[id.xy] = float4(lerp(exp(-od * 0.018 * 0.5), 1.0, 0.25), 0, 0, 1);
}
