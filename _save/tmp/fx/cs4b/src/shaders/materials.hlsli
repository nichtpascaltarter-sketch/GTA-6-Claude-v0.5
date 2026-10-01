// The generated material set (render/materials.cpp: the material table and the albedo / normal texture arrays) and
// the foliage cards (render/props_render.cpp), read through the bindless arrays: the renderer writes their heap
// indices into the frame constants (gBindlessMat, see Renderer::updateFrameConstants), so no pass binds them.
// Must match MaterialInfoGPU in render/materials.cpp.
#ifndef MATERIALS_HLSLI
#define MATERIALS_HLSLI
#include "common.hlsli"
#include "bindless.hlsli"

struct MaterialInfo {
    float layer, uvScale, roughScale, metal;
    float4 tint;
    float normalScale, shadingModel, flags, emissive;
};
StructuredBuffer<MaterialInfo> gBindlessMaterials[] : register(t0, space7);

MaterialInfo materialInfo(uint matId) { return gBindlessMaterials[gBindlessMat.x][matId]; }
Texture2DArray<float4> matAlbedoArray() { return gBindlessTex2DArray[gBindlessMat.y]; }   // layer = MaterialInfo.layer
Texture2DArray<float4> matNormalArray() { return gBindlessTex2DArray[gBindlessMat.z]; }   // xy normal, z roughness, w AO
Texture2DArray<float4> foliageArray() { return gBindlessTex2DArray[gBindlessMat.w]; }     // leaf / frond cards (alpha)

#endif
