// Bindless access to the whole shader-visible descriptor heap. Every SRV / UAV the game creates has a permanent
// index into that heap (gfx::bindlessIndex, C++ side); pass indices in root constants or constant buffers and read
// through these arrays. Indices that vary within a wave need NonUniformResourceIndex(). The default root signatures
// map register spaces 1..15 (SRVs) and 1..8 (UAVs, resource binding tier 3) onto the whole heap, and 16 root
// constants onto register(b0, space100). Shaders are compiled with unbounded descriptor tables enabled (SM 5.1).
#ifndef BINDLESS_HLSLI
#define BINDLESS_HLSLI

cbuffer RootConstants : register(b0, space100) {
    uint4 gRootConstants[4];
};

Texture2D<float4> gBindlessTex2D[] : register(t0, space1);
Texture2DArray<float4> gBindlessTex2DArray[] : register(t0, space2);
Texture3D<float4> gBindlessTex3D[] : register(t0, space3);
TextureCube<float4> gBindlessTexCube[] : register(t0, space4);
ByteAddressBuffer gBindlessBuffers[] : register(t0, space5);
Texture2D<uint4> gBindlessTex2DUint[] : register(t0, space6);
// SRV spaces 7..15 are free for StructuredBuffer<T> arrays of the passes that need them, e.g.
//   StructuredBuffer<LightGPU> gBindlessLights[] : register(t0, space7);

RWTexture2D<float4> gBindlessRWTex2D[] : register(u0, space1);
RWTexture2DArray<float4> gBindlessRWTex2DArray[] : register(u0, space2);
RWTexture3D<float4> gBindlessRWTex3D[] : register(u0, space3);
RWByteAddressBuffer gBindlessRWBuffers[] : register(u0, space4);
// UAV spaces 5..8 are free for RWStructuredBuffer<T> arrays.

#endif
