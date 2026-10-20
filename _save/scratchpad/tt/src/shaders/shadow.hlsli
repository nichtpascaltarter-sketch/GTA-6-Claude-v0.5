// Cascaded shadow map sampling.
#ifndef SHADOW_HLSLI
#define SHADOW_HLSLI
#include "common.hlsli"

cbuffer ShadowCB : register(b3) {
    float4x4 gCascadeVP[4];
    float4 gCascadeSplits;   // far distance per cascade (view depth)
    float4 gCascadeTexel;    // world size of a shadow texel per cascade
    float4 gShadowParams;    // x resolution, y cascade count, z fade fraction, w unused
    float4 gShadowPad;
};

static const float2 kPoisson[12] = {
    float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
    float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893),
    float2(0.507, 0.064), float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};

float sampleSunShadowGeo(float3 relPos, float3 N, float viewDepth, uint2 pix);
float sampleCascade(int c, float3 relPos, float3 N, float noiseAngle) {
    float3 p = relPos + N * gCascadeTexel[c] * 2.0 + gSunDir.xyz * gCascadeTexel[c] * 1.0;
    float4 sp = mul(gCascadeVP[c], float4(p, 1));
    float2 suv = sp.xy * float2(0.5, -0.5) + 0.5;
    float z = sp.z;
    if (any(suv < 0.0) || any(suv > 1.0) || z > 1.0) return 1.0;
    float texel = 1.0 / gShadowParams.x;
    float radius = 1.6 * texel;
    float s = 0;
    float sn, cs;
    sincos(noiseAngle, sn, cs);
    float2x2 rot = float2x2(cs, -sn, sn, cs);
    [unroll] for (int i = 0; i < 12; i++) {
        float2 o = mul(rot, kPoisson[i]) * radius;
        s += gShadowMap.SampleCmpLevelZero(sShadowCmp, float3(suv + o, c), z);
    }
    return s / 12.0;
}

float cloudShadowAt(float3 relPos) {
    float2 w = relPos.xy + gCamPos.xy;
    float2 uv = (w - gCloudShadow.xy) / gCloudShadow.z + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    return lerp(1.0, gCloudShadowMap.SampleLevel(sLinearClamp, uv, 0), gCloudShadow.w);
}

float sampleSunShadow(float3 relPos, float3 N, float viewDepth, uint2 pix) {
    float cs = cloudShadowAt(relPos);
    return cs * sampleSunShadowGeo(relPos, N, viewDepth, pix);
}

float sampleSunShadowGeo(float3 relPos, float3 N, float viewDepth, uint2 pix) {
    int count = (int)gShadowParams.y;
    if (gSunDir.w <= 0.0) return 0.0;
    float noiseAngle = ign(pix, gTime.z) * TWO_PI;
    [loop] for (int c = 0; c < count; c++) {
        if (viewDepth < gCascadeSplits[c]) {
            float s = sampleCascade(c, relPos, N, noiseAngle);
            // blend into the next cascade near the far end
            float prevSplit = c > 0 ? gCascadeSplits[c - 1] : 0.0;
            float fadeStart = lerp(prevSplit, gCascadeSplits[c], 0.85);
            if (viewDepth > fadeStart && c + 1 < count) {
                float s2 = sampleCascade(c + 1, relPos, N, noiseAngle);
                s = lerp(s, s2, saturate((viewDepth - fadeStart) / (gCascadeSplits[c] - fadeStart)));
            } else if (viewDepth > fadeStart) {
                s = lerp(s, 1.0, saturate((viewDepth - fadeStart) / (gCascadeSplits[c] - fadeStart)));
            }
            return s;
        }
    }
    return 1.0;
}


#endif
