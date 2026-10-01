// Mip chain generation for TEX_GENMIPS textures (gfx::Context::generateMips): each dispatch filters mip N-1 into
// mip N for every array slice (cubes are six slices). sRGB textures are read through their sRGB view (filtered in
// linear light) and re-encoded before the store through the UNORM view.
cbuffer MipGenConstants : register(b0, space100) {
    uint2 gDstSize;
    uint gSrgb;
    uint gLayers;
};
Texture2DArray<float4> gSrc : register(t0);
RWTexture2DArray<float4> gDst : register(u0);
SamplerState sLinearClamp : register(s1);

float3 linearToSrgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055;
    return float3(c.r <= 0.0031308 ? lo.r : hi.r, c.g <= 0.0031308 ? lo.g : hi.g, c.b <= 0.0031308 ? lo.b : hi.b);
}

[numthreads(8, 8, 1)]
void csMipGen(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gDstSize.x || id.y >= gDstSize.y || id.z >= gLayers) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(gDstSize);
    float4 c = gSrc.SampleLevel(sLinearClamp, float3(uv, (float)id.z), 0);
    if (gSrgb != 0) c.rgb = linearToSrgb(saturate(c.rgb));
    gDst[id] = c;
}
