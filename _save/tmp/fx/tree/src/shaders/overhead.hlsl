// Overhead (top-down) pass over the static world cells: depth gives the highest static surface (rain occlusion,
// dry areas under roofs, grass exclusion) and the color target flags lawns / medians (grass material facing
// up) so ground cover can grow on world meshes too.
#include "common.hlsli"

cbuffer DrawCB : register(b1) {
    float4 gCellOffset;   // xyz: cell origin - camera position
    float4 gDrawParams;
};
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
    o.pos = mul(gPassViewProj, float4(i.pos + gCellOffset.xyz, 1));
    o.mat = i.mat & 0xffu;
    o.nz = octDecode(i.nrm).z;
    return o;
}

float psOverhead(VSOut i) : SV_Target {
    return (i.mat == 25u && abs(i.nz) > 0.6) ? 1.0 : 0.0;   // MAT_GRASS surfaces facing up
}
