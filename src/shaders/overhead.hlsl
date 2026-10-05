// Overhead (top-down) pass over the static world cells: depth gives the highest static surface (rain occlusion,
// dry areas under roofs, grass exclusion) and the color target classes it: 1 lawns / medians (grass material facing
// up) so ground cover can grow on world meshes too, 0.25 soil facing up (planting beds, where the decor plants grow),
// 0 anything else.
#include "common.hlsli"
#include "bindless.hlsli"

// Per-draw root constants (WorldRenderer::setCellOffset): 0..2 cell origin relative to the camera
float3 cellOffset() { return asfloat(gRootConstants[0].xyz); }
cbuffer ShadowPassCB : register(b2) {
    float4x4 gPassViewProj;
};

struct VSIn {
    float3 pos : POSITION;
    float2 nrm : NORMAL;
    float2 tan : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint mat : MATID;
};
struct VSOut {
    float4 pos : SV_Position;
    nointerpolation uint mat : MATID;
    float nz : TEXCOORD0;
};

VSOut vsOverhead(VSIn i) {
    VSOut o;
    o.pos = mul(gPassViewProj, float4(i.pos + cellOffset(), 1));
    o.mat = i.mat & 0xffu;
    o.nz = octDecode(i.nrm).z;
    return o;
}

float psOverhead(VSOut i) : SV_Target {
    if (i.mat == 25u && abs(i.nz) > 0.6) return 1.0;   // MAT_GRASS surfaces facing up
    return (i.mat == 26u && i.nz > 0.6) ? 0.25 : 0.0;   // MAT_DIRT
}
