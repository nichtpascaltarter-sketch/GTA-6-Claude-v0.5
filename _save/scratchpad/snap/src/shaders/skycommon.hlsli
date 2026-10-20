// Sky evaluation shared by lighting, reflections and SH projection (uses global LUT bindings).
#ifndef SKYCOMMON_HLSLI
#define SKYCOMMON_HLSLI
#include "atmosphere.hlsli"

float3 sampleTransmittanceG(float r, float mu) {
    return gTransmittanceLUT.SampleLevel(sLinearClamp, transmittanceUV(r, mu), 0).rgb;
}
float cameraRadiusKm() { return kBottomRadius + max(gPlanetParams.z, 0.002); }
// Main light illuminance arriving at the camera altitude (before shadows), lux * exposure applied by caller
float3 mainLightIlluminance() {
    return gSunColor.rgb * sampleTransmittanceG(cameraRadiusKm(), gSunDir.z);
}

float3 starField(float3 dir) {
    float3 d = dir * 300.0;
    float3 c = floor(d);
    float h = hash31(c);
    float star = 0;
    if (h > 0.985) {
        float3 f = frac(d) - 0.5;
        float3 o = float3(hash31(c + 17.1), hash31(c + 31.7), hash31(c + 7.3)) - 0.5;
        float dist = length(f - o * 0.6);
        float bright = pow(saturate((h - 0.985) / 0.015), 3.0);
        float tw = 0.75 + 0.25 * sin(gTime.x * (2.0 + h * 5.0) + h * 100.0);
        star = smoothstep(0.08, 0.0, dist) * bright * tw;
    }
    float3 tint = lerp(float3(0.8, 0.85, 1.0), float3(1.0, 0.9, 0.75), hash31(c + 3.3));
    float band = exp(-sq(dot(dir, normalize(float3(0.3, -0.5, 0.8))) * 5.0));
    float mw = band * (0.4 + 0.6 * fbm(dir.xy * 6.0 + dir.z * 3.0, 4)) * 0.02;
    return tint * star * 2.0 + float3(0.6, 0.65, 0.9) * mw;
}

// Sky radiance (not exposed) in direction dir. withSun adds sun/moon disks and stars.
float3 skyRadiance(float3 dir, bool withSun) {
    float viewH = cameraRadiusKm();
    float3 pos = float3(0, 0, viewH);
    float cosViewZ = dir.z;
    float3 sunDir = gSunDir.xyz;
    float2 sunH = normalize(sunDir.xy + 1e-6);
    float2 dirH = normalize(dir.xy + 1e-6);
    float cosLightView = dot(sunH, dirH);
    bool groundHit = raySphere(pos, dir, kBottomRadius) >= 0.0;
    float2 uv = skyViewUV(viewH, cosViewZ, cosLightView, groundHit);
    float3 L = gSkyViewLUT.SampleLevel(sLinearClamp, uv, 0).rgb;
    float night = gExposure.w;
    if (withSun && !groundHit) {
        float3 T = sampleTransmittanceG(viewH, cosViewZ);
        float cosSun = dot(dir, sunDir);
        if (gSunColor.w < 0.5) {
            float sunCos = cos(0.00465 * 1.2);
            if (cosSun > sunCos) {
                float r = saturate((1.0 - cosSun) / (1.0 - sunCos));
                float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r * r));
                L += T * gSunColor.rgb * limb * 22000.0;
            }
        } else {
            float moonCos = cos(0.0048 * 1.4);
            if (cosSun > moonCos) {
                float r = saturate((1.0 - cosSun) / (1.0 - moonCos));
                float mare = 0.75 + 0.25 * fbm(dir.xy * 900.0, 3);
                L += T * float3(0.9, 0.92, 1.0) * mare * 1.2 * (1.0 - r * r * 0.3);
            }
            L += T * float3(0.5, 0.55, 0.7) * 0.03 * pow(saturate(cosSun), 400.0);
        }
        L += T * starField(dir) * night * 0.05 * saturate(cosViewZ * 4.0);
    }
    float horizon = exp(-max(cosViewZ, 0.0) * 9.0);
    L += night * (float3(0.0012, 0.0014, 0.0022) + float3(0.022, 0.012, 0.006) * horizon);
    return L;
}

// Aerial perspective lookup: returns (inscatter rgb, transmittance) for a camera-relative position
float4 aerialPerspective(float2 uv, float distMeters) {
    float slice = sqrt(saturate(distMeters * 0.001 * gFog.w / AP_MAX_KM));
    float4 ap = gAerialLUT.SampleLevel(sLinearClamp, float3(uv, slice - 0.5 / AP_RES), 0);
    // fade in over the first slice (the LUT's first slice is at non-zero distance)
    float w = saturate(slice * AP_RES);
    return float4(ap.rgb * w, lerp(1.0, ap.a, w));
}
// Froxel volumetric fog at a screen position / view depth: rgb in-scatter (pre-exposed), a transmittance.
float4 froxelFog(float2 uv, float viewDepth) {
    if (gFogParams1.w < 0.5) return float4(0, 0, 0, 1);
    float w = saturate(log2(max(viewDepth, gFogParams1.z) / gFogParams1.z) * gFogParams1.y);
    return gFogVolume.SampleLevel(sLinearClamp, float3(uv, w), 0);
}
#endif