// G-buffer layout shared by all geometry passes and the lighting pass.
//   RT0 R8G8B8A8_UNORM_SRGB : albedo.rgb, material AO
//   RT1 R16G16_UNORM        : octahedral normal
//   RT2 R8G8B8A8_UNORM      : roughness, metalness, shading model id / 255, extra (translucency, clearcoat...)
//   RT3 R11G11B10_FLOAT     : emissive radiance (pre-exposed)
//   RT4 R16G16_FLOAT        : screen-space velocity in UV units (current - previous)
#ifndef GBUFFER_HLSLI
#define GBUFFER_HLSLI
#include "common.hlsli"

struct GBufferOut {
    float4 albedo : SV_Target0;
    float2 normal : SV_Target1;
    float4 material : SV_Target2;
    float3 emissive : SV_Target3;
    float2 velocity : SV_Target4;
};

float2 computeVelocity(float4 curClip, float4 prevClip) {
    float2 c = curClip.xy / curClip.w;
    float2 p = prevClip.xy / prevClip.w;
    return (c - p) * float2(0.5, -0.5);
}

GBufferOut packGBuffer(float3 albedo, float ao, float3 n, float rough, float metal, uint shadingModel, float extra,
                       float3 emissive, float4 curClip, float4 prevClip) {
    GBufferOut o;
    o.albedo = float4(saturate(albedo), saturate(ao));
    o.normal = octEncode(normalize(n)) * 0.5 + 0.5;
    o.material = float4(saturate(rough), saturate(metal), shadingModel / 255.0, saturate(extra));
    o.emissive = min(emissive * preExposure(), 60000.0);
    o.velocity = computeVelocity(curClip, prevClip);
    return o;
}

struct GBufferData {
    float3 albedo;
    float ao;
    float3 normal;
    float rough;
    float metal;
    uint shadingModel;
    float extra;
};

// Hair strand cards store the strand direction (a line, sign-free) in the metal channel of their SM_HAIR pixels, as
// an angle in the normal's tangent plane (0.7 degree steps). Codes below 1.5/255 mean "no tangent" (the opaque hair
// shell, lit along the surface projection of "down"). The lighting pass clears g.metal after decoding.
float3 hairRefAxis(float3 N) {
    float3 a = abs(N.z) < 0.8 ? float3(0, 0, 1) : float3(1, 0, 0);
    return normalize(cross(a, N));
}
float encodeHairTangent(float3 N, float3 T) {
    float3 r0 = hairRefAxis(N), r1 = cross(N, r0);
    float ang = atan2(dot(T, r1), dot(T, r0));
    if (ang < 0.0) ang += PI;
    return (2.0 + min(ang / PI, 0.999) * 253.0) / 255.0;
}
bool decodeHairTangent(float3 N, float code, out float3 T) {
    T = 0;
    if (code < 1.5 / 255.0) return false;
    float ang = (code * 255.0 - 2.0) / 253.0 * PI;
    float3 r0 = hairRefAxis(N), r1 = cross(N, r0);
    T = r0 * cos(ang) + r1 * sin(ang);
    return true;
}

GBufferData unpackGBuffer(float4 a, float2 n, float4 m) {
    GBufferData g;
    g.albedo = a.rgb;
    g.ao = a.a;
    g.normal = octDecode(n * 2.0 - 1.0);
    g.rough = max(m.r, 0.02);
    g.metal = m.g;
    g.shadingModel = (uint)(m.b * 255.0 + 0.5);
    g.extra = m.a;
    return g;
}

#endif
