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

// Stars: a fixed random field, one star in about 1 of 160 cells of ~0.26 degrees (some 3,000 over the sky, most of
// them faint), each drawn as a point about a pixel wide (a Gaussian in screen pixels: no star vanishes between
// pixels or crawls under the TAA jitter). Brightness spans five magnitudes (few bright, many faint), tint by
// spectral class, a slight twinkle. Peak radiance in cd/m2 for a bright star: a white point at the night exposure.
float3 starField(float3 dir) {
    float3 d = dir * 220.0;
    float3 c = floor(d);
    float h = hash31(c);
    if (h < 0.9938) return 0;
    float3 o = float3(hash31(c + 17.1), hash31(c + 31.7), hash31(c + 7.3)) - 0.5;
    float3 sp = normalize(c + 0.5 + o * 0.5);   // star direction, kept off the cell borders
    float px = gCamForward.w / max(gScreen.y, 1.0);   // a pixel's angle (rad)
    float a = length(dir - sp) / max(px, 1e-5);         // angular distance in pixels
    float mag = (h - 0.9938) / 0.0062;                  // 0 .. 1
    float bright = exp2(-6.5 * (1.0 - pow(mag, 3.0)));   // a few bright stars, many faint
    float tw = 0.8 + 0.2 * sin(gTime.x * (2.0 + h * 5.0) + mag * 100.0);
    float3 tint = lerp(float3(0.75, 0.84, 1.0), float3(1.0, 0.86, 0.68), hash31(c + 3.3));
    return tint * (14.0 * bright * tw * exp(-a * a * 1.4));
}

// Sky radiance (not exposed) in direction dir. withSun adds sun/moon disks and stars.
// Urban light pollution at night: street and building light scattered back by the air, strongest towards the
// horizon and towards the denser side of the city (gSkyGlow.yz). Sodium / warm LED dominated.
float3 cityGlowRadiance(float3 dir) {
    float amt = gSkyGlow.x * gSkyGlow.w;
    if (amt <= 0.0) return 0;
    float up = max(dir.z, 0.0);
    float horizon = exp(-up * 7.0);
    float lean = max(1.0 + dot(normalize(dir.xy + 1e-5), gSkyGlow.yz) * 1.5 * (1.0 - up), 0.2);
    return float3(1.0, 0.56, 0.3) * amt * (0.006 + 0.05 * horizon * lean);
}

// Illuminance (lux) the lit city sends up to a low cloud deck (lights the cloud base at night).
float3 cityUplight() { return float3(1.0, 0.56, 0.3) * gSkyGlow.x * gSkyGlow.w * 0.6; }

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
            // the moon: a bright disc with darker maria (a white disc with a glow at the night exposure), a soft
            // aureole of forward-scattered moonlight in the haze around it; follows the moonlight's fade at moonrise
            float moonFade = saturate(luminance(gSunColor.rgb) / 2.5);
            float moonCos = cos(0.0048 * 1.4);
            if (cosSun > moonCos) {
                float r = saturate((1.0 - cosSun) / (1.0 - moonCos));
                float mare = 0.72 + 0.28 * fbm(dir.xy * 900.0, 3);
                L += T * float3(0.95, 0.96, 1.0) * mare * 260.0 * moonFade * (1.0 - r * r * 0.3);
            }
            float cs = saturate(cosSun);
            L += T * float3(0.62, 0.68, 0.85) * moonFade * (1.2 * pow(cs, 3000.0) + 0.22 * pow(cs, 260.0) + 0.04 * pow(cs, 30.0));
        }
        // stars: washed out by the city's light pollution and near the bright moon, faint low in the haze
        float starVis = night * (1.0 - 0.75 * saturate(gSkyGlow.x)) * saturate(cosViewZ * 4.0) *
                        (gSunColor.w > 0.5 ? 1.0 - 0.8 * pow(saturate(dot(dir, sunDir)), 40.0) : 1.0);
        if (starVis > 0.0) L += T * starField(dir) * starVis;
    }
    // Natural night sky (airglow, starlight: a deep blue) + light pollution
    L += night * float3(0.0009, 0.0014, 0.0032) + cityGlowRadiance(dir);
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
    float4 v = gFogVolume.SampleLevel(sLinearClamp, float3(uv, w), 0);
    // a poisoned froxel falls back to clear air instead of blacking out everything behind it
    return anyNonFinite(v.rgb) || !(v.a >= 0.0 && v.a <= 1.0) ? float4(0, 0, 0, 1) : float4(min(v.rgb, 60000.0), v.a);
}
#endif