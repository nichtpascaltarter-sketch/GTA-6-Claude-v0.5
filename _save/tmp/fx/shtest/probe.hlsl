#include "bindless.hlsli"
cbuffer C : register(b0) { uint4 gIdx; };
SamplerState sAnisoWrap : register(s3);
struct MaterialInfo { float layer, uvScale, roughScale, metal; float4 tint; float normalScale, shadingModel, flags, emissive; };
StructuredBuffer<MaterialInfo> gBindlessMaterials[] : register(t0, space7);
float4 helper(Texture2DArray<float4> t, float3 uvw) { return t.Sample(sAnisoWrap, uvw); }
float4 psLocal(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    Texture2DArray<float4> albedo = gBindlessTex2DArray[gIdx.y];
    MaterialInfo m = gBindlessMaterials[gIdx.x][(uint)p.x & 7];
    float lod = albedo.CalculateLevelOfDetail(sAnisoWrap, uv);
    return albedo.Sample(sAnisoWrap, float3(uv, m.layer)) * m.tint + lod + helper(gBindlessTex2DArray[gIdx.z], float3(uv, 1));
}
