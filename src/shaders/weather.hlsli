// Surface wetness for the static material shaders (world, terrain, props, grass): darker porous albedo, lower
// roughness, puddles in flat/low areas with animated rain ripples, water streaks on facades. Surfaces under
// roofs / bridges stay dry (overhead height map of the static world).
#ifndef WEATHER_HLSLI
#define WEATHER_HLSLI
#include "common.hlsli"

// 1 where the sky is open above the point, 0 under cover. Soft (4 taps) near roof edges.
float skyExposure(float3 worldP) {
    if (gOverhead.w < 0.5) return 1.0;
    float2 uv = (worldP.xy - gOverhead.xy) / gOverhead.z;
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    float2 o = 0.35 / gOverhead.z;
    float4 h;
    h.x = gOverheadMap.SampleLevel(sPointClamp, uv + float2(-o.x, -o.y), 0);
    h.y = gOverheadMap.SampleLevel(sPointClamp, uv + float2(o.x, -o.y), 0);
    h.z = gOverheadMap.SampleLevel(sPointClamp, uv + float2(-o.x, o.y), 0);
    h.w = gOverheadMap.SampleLevel(sPointClamp, uv + float2(o.x, o.y), 0);
    float4 e = saturate((worldP.z - h + 0.35) / 0.3);
    return dot(e, 0.25);
}

// Expanding ring ripples from raindrops: returns a tangent-space normal offset (xy). One drop per cell,
// two layers with different scales and phases.
float2 rainRipples(float2 p, float t, float intensity) {
    float2 n = 0;
    [unroll] for (int layer = 0; layer < 2; layer++) {
        float scale = layer == 0 ? 2.3 : 3.7;
        float2 q = p * scale + layer * 17.31;
        float2 cell = floor(q);
        uint h = hash2u(asuint(int2(cell)));
        float2 center = cell + 0.2 + 0.6 * float2((h & 1023u) / 1023.0, ((h >> 10) & 1023u) / 1023.0);
        float phase = frac(t * (0.9 + layer * 0.3) + ((h >> 20) & 1023u) / 1023.0);
        float2 d = q - center;
        float dist = length(d);
        float radius = phase * 0.4;
        float x = (dist - radius) * 28.0;
        float ring = sin(clamp(x, -PI, PI)) * (1.0 - phase) * (1.0 - phase) * saturate(1.0 - dist / 0.42);
        n += (dist > 1e-4 ? d / dist : 0) * ring * 0.55 * intensity;
    }
    return n;
}

// Puddle coverage for flat ground: noise pattern biased towards terrain depressions.
float puddleMask(float3 worldP, float3 Ngeom) {
    float amount = gWeather2.z;
    if (amount <= 0.01 || Ngeom.z < 0.93) return 0.0;
    float2 p = worldP.xy - floor(worldP.xy / 1024.0) * 1024.0;
    float n = fbmValue(p * 0.11, 3) * 0.75 + fbmValue(p * 0.47 + 5.3, 2) * 0.25;
    // local concavity of the terrain (water collects in low spots)
    float2 tuv = (worldP.xy + 10240.0) / 20480.0;
    float2 e = 6.0 / 20480.0;
    float hc = gTerrainHeightG.SampleLevel(sLinearClamp, tuv, 0);
    float hn = (gTerrainHeightG.SampleLevel(sLinearClamp, tuv + float2(e.x, 0), 0) + gTerrainHeightG.SampleLevel(sLinearClamp, tuv - float2(e.x, 0), 0) +
                gTerrainHeightG.SampleLevel(sLinearClamp, tuv + float2(0, e.y), 0) + gTerrainHeightG.SampleLevel(sLinearClamp, tuv - float2(0, e.y), 0)) * 0.25;
    float concave = saturate((hn - hc) * 4.0);
    float flat = smoothstep(0.93, 0.99, Ngeom.z);
    float thr = lerp(0.72, 0.42, amount) - concave * 0.25;
    return smoothstep(thr, thr + 0.05, n) * flat;
}

// porosity: 0 (glass, metal, paint) .. 1 (asphalt, concrete, soil). Modifies the G-buffer inputs in place.
void applyWetness(inout float3 albedo, inout float rough, inout float3 n, float3 Ngeom, float3 worldP, float porosity, float allowPuddles) {
    float wetness = gWeather.y;
    if (wetness <= 0.001) return;
    float exposed = skyExposure(worldP);
    float up = saturate(Ngeom.z * 2.0 + 0.3);
    float wet = wetness * exposed;
    // Vertical surfaces: rain streaks running down facades
    if (abs(Ngeom.z) < 0.5) {
        float2 side = normalize(float2(-Ngeom.y, Ngeom.x) + 1e-5);
        float along = dot(worldP.xy, side);
        float streak = valueNoise(float2(along * 5.0, worldP.z * 0.35 + gTime.x * 0.08)) * 0.6 + valueNoise(float2(along * 13.0, worldP.z * 0.9)) * 0.4;
        wet *= lerp(0.35, 1.0, smoothstep(0.45, 0.75, streak));
    } else {
        wet *= up;
    }
    // Porous surfaces darken (water fills the pores), all surfaces get glossier
    albedo *= lerp(1.0, lerp(0.85, 0.45, porosity), wet);
    rough = lerp(rough, lerp(0.35, 0.1, saturate(porosity + 0.3)), wet * 0.9);
    // Puddles: a thin water film on flat ground, mirror-like with raindrop ripples
    float pud = allowPuddles * puddleMask(worldP, Ngeom) * exposed;
    if (pud > 0.0) {
        albedo *= lerp(1.0, 0.6, pud);
        rough = lerp(rough, 0.025, pud);
        float2 rip = gWeather.x > 0.01 ? rainRipples(worldP.xy, gWeather2.w, saturate(gWeather.x * 1.5)) : 0;
        float3 flatN = normalize(float3(rip, 1.0));
        n = normalize(lerp(n, flatN, pud));
    }
}

#endif
