// Static world geometry (roads, buildings, props): material-table driven PBR into the G-buffer.
#include "gbuffer.hlsli"
#include "facade.hlsli"

struct MaterialInfo {
    float layer, uvScale, roughScale, metal;
    float4 tint;
    float normalScale, shadingModel, flags, emissive;
};
StructuredBuffer<MaterialInfo> tMaterials : register(t10);
Texture2DArray<float4> tMatAlbedo : register(t11);
Texture2DArray<float4> tMatNormal : register(t12);

cbuffer DrawCB : register(b1) {
    float4 gCellOffset;   // xyz: cell origin - camera position
    float4 gDrawParams;   // x: lod fade (dither), y: wind sway strength, z,w unused
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
    float3 rel : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float4 tan : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 color : COLOR0;
    nointerpolation uint mat : MATID;
    float4 curClip : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
    float3 cellPos : TEXCOORD6;
};

float3 decodeOct(float2 e) { return octDecode(e); }

VSOut vsWorld(VSIn i) {
    VSOut o;
    float3 rel = i.pos + gCellOffset.xyz;
    o.rel = rel;
    o.pos = mul(gViewProj, float4(rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(rel, 1));
    o.prevClip = mul(gPrevViewProj, float4(rel, 1));
    o.nrm = decodeOct(i.nrm);
    o.tan = float4(decodeOct(i.tan), (i.mat & 0x80000000u) ? -1.0 : 1.0);
    o.uv = i.uv;
    o.color = i.color;
    o.mat = i.mat;
    o.cellPos = i.pos;
    return o;
}

cbuffer ShadowPassCB : register(b2) {
    float4x4 gShadowViewProj;
};
float4 vsWorldShadow(VSIn i) : SV_Position {
    float3 rel = i.pos + gCellOffset.xyz;
    return mul(gShadowViewProj, float4(rel, 1));
}

// ------------------------------------------------------------------------------------------------
float puddleMask(float3 worldP, float3 N) {
    if (gWeather.y <= 0.01 || N.z < 0.85) return 0;
    float2 p = worldP.xy - floor(worldP.xy / 1024.0) * 1024.0;
    float n = fbmValue(p * 0.18, 3);
    return smoothstep(0.62 - gWeather.y * 0.25, 0.7 - gWeather.y * 0.25, n) * gWeather.y;
}

GBufferOut psWorld(VSOut i, bool front : SV_IsFrontFace) {
    uint matId = i.mat & 0xffu;
    uint param = (i.mat >> 8) & 0x7fffffu;
    MaterialInfo m = tMaterials[matId];
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 T = normalize(i.tan.xyz - N * dot(i.tan.xyz, N));
    float3 B = cross(N, T) * i.tan.w;
    float3 worldP = i.rel + gCamPos.xyz;

    float3 albedo, n;
    float rough, metal, ao = 1;
    float3 emissive = 0;
    uint sm = (uint)m.shadingModel;
    float extra = 0;
    if ((uint)m.flags & 4) {
        // Procedural building facade (windows with interior mapping)
        FacadeResult f = shadeFacade(param, i.uv, N, T, B, i.rel, worldP, tMatAlbedo, tMatNormal);
        albedo = f.albedo * i.color.rgb;
        if (f.isWindow) albedo = f.albedo;
        n = f.normal;
        rough = f.rough;
        metal = f.metal;
        emissive = f.emissive;
        ao = f.ao;
    } else {
        float2 uv = i.uv * m.uvScale;
        float4 a = tMatAlbedo.Sample(sAnisoWrap, float3(uv, m.layer));
        float4 nr = tMatNormal.Sample(sAnisoWrap, float3(uv, m.layer));
        albedo = a.rgb * m.tint.rgb * i.color.rgb;
        float2 nxy = (nr.xy * 2.0 - 1.0) * m.normalScale;
        float3 nts = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
        n = normalize(T * nts.x + B * nts.y + N * nts.z);
        rough = saturate(nr.z * m.roughScale);
        metal = m.metal;
        ao = nr.w;
        if ((uint)m.flags & 16) emissive = albedo * i.color.a * 400.0 * m.emissive;
        if ((uint)m.flags & 8) { sm = SM_FOLIAGE; extra = 0.6; }
        // Large-scale variation to break tiling on big surfaces
        float2 wp = worldP.xy - floor(worldP.xy / 2048.0) * 2048.0;
        float macro = fbmValue(wp * 0.03 + worldP.z * 0.01, 2);
        albedo *= lerp(0.88, 1.08, macro);
    }
    // Rain wetness
    float wet = gWeather.y * saturate(N.z * 2.0 + 0.3);
    float puddle = puddleMask(worldP, N);
    albedo *= lerp(1.0, 0.6, wet * (1.0 - metal));
    rough = lerp(rough, 0.12, wet * 0.8);
    rough = lerp(rough, 0.02, puddle);
    n = normalize(lerp(n, N, puddle));
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive, i.curClip, i.prevClip);
}
