// Volumetric cloud layer: parameters, noise textures and the density function shared by the cloud ray-march,
// the cloud shadow map and the reflection probe's sky.
#ifndef CLOUDCOMMON_HLSLI
#define CLOUDCOMMON_HLSLI
#include "skycommon.hlsli"

Texture3D<float4> tShapeNoise : register(t0);
Texture3D<float4> tDetailNoise : register(t1);
Texture2D<float4> tWeather : register(t2);

cbuffer CloudCB : register(b1) {
    float4 gCloud0;   // x coverage, y density, z bottom (m), w top (m)
    float4 gCloud1;   // xy wind offset (m), z time, w frame
    float4 gCloud2;   // x trace width, y trace height, z history valid, w rain darkening
    float4 gCloud3;   // x storm (0..1), y ray-march step scale, zw sub-pixel offset of this frame's trace
};

float remap(float v, float l0, float h0, float l1, float h1) { return l1 + (v - l0) * (h1 - l1) / (h0 - l0); }

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

// Sun (or moon) light arriving at altitude zMeters, before shadows. Cloud layers see a whiter, stronger sun than
// the ground at golden hour and stay lit - orange, then pink - for a few minutes after the ground's sunset, until
// the sun drops below their own horizon.
float3 sunLightAtAltitude(float zMeters) {
    float r = kBottomRadius + max(zMeters, 2.0) * 0.001;
    float muH = -sqrt(max(1.0 - sq(kBottomRadius / r), 0.0));
    float vis = smoothstep(muH - 0.004, muH + 0.004, gSunDir.z);
    return gSunColor.rgb * sampleTransmittanceG(r, gSunDir.z) * vis;
}

// High cirrus near 9 km, above the cumulus: a thin sheet of wind-sheared ice-crystal streaks in patches that come
// and go with the large-scale weather. Thin (the sun and the blue sky show through), strongly forward scattering,
// and lit by the sun well after the ground's sunset. Returns in-scattered radiance (not exposed) and transmittance.
static const float kCirrusAlt = 9000.0;
float4 cirrusLayer(float3 dir) {
    if (dir.z <= 0.012) return float4(0, 0, 0, 1);
    float t = (kCirrusAlt - gCamPos.z) / dir.z;
    float2 p = gCamPos.xy + dir.xy * t;
    // Streak frame: along the upper-level westerlies (independent of the surface wind), drifting at 12 m/s. The
    // drift wraps every 720 km, a whole multiple of every along-wind period below.
    const float2 ax = float2(0.94, 0.342);
    float2 q = float2(dot(p, ax), dot(p, float2(-ax.y, ax.x)));
    q.x += frac(gCloud1.z / 60000.0) * 720000.0;
    // Where cirrus exists: broad patches from the weather map's smooth type field; more ahead of fronts (higher
    // coverage), none in storms (hidden by the deck anyway)
    float field = tWeather.SampleLevel(sLinearWrap, q / 180000.0 + float2(0.31, 0.77), 0).g;
    float amt = saturate(0.3 + 0.55 * gCloud0.x) * (1.0 - gCloud3.x);
    float cov = saturate((field - (0.66 - 0.36 * amt)) * 3.0);
    if (cov <= 0.0) return float4(0, 0, 0, 1);
    // Patches a few km across, bent by a low-frequency warp, filled with fibres ~15:1 elongated along the wind
    float warp = tShapeNoise.SampleLevel(sLinearWrap, float3(q / 45000.0, 0.13), 0).r;
    float patchN = tShapeNoise.SampleLevel(sLinearWrap, float3(q.x / 48000.0, q.y / 21000.0 + warp * 0.35, 0.37), 0).r;
    float4 fib = tShapeNoise.SampleLevel(sLinearWrap, float3(q.x / 60000.0, q.y / 4200.0 + warp * 0.6, 0.71), 0);
    float fibres = saturate(fib.a * 1.4 - 0.25) * 0.65 + saturate(fib.b * 1.3 - 0.3) * 0.35;
    float d = saturate(patchN * 1.7 - 0.5) * fibres * cov;
    if (d <= 0.002) return float4(0, 0, 0, 1);
    float tau = d * 0.5 / max(dir.z, 0.05) * 0.3;   // slant optical depth: thin overhead, thicker towards the horizon
    float T = exp(-tau);
    float cosT = dot(dir, gSunDir.xyz);
    float ph = lerp(hgPhase(-0.15, cosT), hgPhase(0.8, cosT), 0.6);
    float3 sunC = sunLightAtAltitude(kCirrusAlt);
    float3 amb = evalSH9(float3(0, 0, 1)) * PI;
    float3 L = (sunC * ph * 1.4 + amb * 0.06) * (1.0 - T);
    // thin upper air: a gentle fade towards the horizon
    float fade = exp(-t / 140000.0);
    return float4(L * fade, lerp(1.0, T, fade));
}


#endif
