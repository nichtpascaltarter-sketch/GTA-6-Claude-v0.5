#include "bindless.hlsli"
cbuffer C : register(b0) { uint4 gIdx; };
SamplerState sAnisoWrap : register(s3);
Texture2DArray<float4> matAlbedo() { return gBindlessTex2DArray[gIdx.y]; }
float4 psRet(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return matAlbedo().Sample(sAnisoWrap, float3(uv, 0));
}
