// Reflection fallback when screen-space rays miss: the dynamic environment probe around the camera (prefiltered
// by roughness), or the sky (sky-view LUT + SH) when no probe is available. Returns radiance (not exposed).
#ifndef REFLECTION_HLSLI
#define REFLECTION_HLSLI
#include "skycommon.hlsli"

float3 skyReflection(float3 R, float rough) {
    float3 Rs = normalize(float3(R.xy, max(R.z, 0.02)));
    return lerp(skyRadiance(Rs, false), evalSH9(R) * PI, saturate(rough * 1.3));
}

float3 envReflection(float3 R, float rough) {
    if (gEnvProbe.w > 0.5) {
        float mip = sqrt(saturate(rough)) * gEnvProbe.w;
        return gEnvProbeTex.SampleLevel(sLinearClamp, R, mip).rgb;
    }
    return skyReflection(R, rough);
}

// Specular occlusion from the reflection direction dipping below the geometric surface.
float horizonOcclusion(float3 R, float3 N) {
    float h = saturate(1.0 + 1.5 * dot(R, N));
    return h * h;
}

#endif
