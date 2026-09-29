// Static world geometry (roads, buildings, props): material-table driven PBR into the G-buffer.
#include "gbuffer.hlsli"
#include "facade.hlsli"
#include "weather.hlsli"

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


// Animated emissive patterns for signage and landmark lights (MAT_EMISSIVE param = pattern | phase << 4,
// written by world/sitegeo.cpp): aviation blink, marquee chase, colour cycle, pulse, sequenced flasher, night-only.
float3 emissiveAnim(uint param, float3 col) {
    uint pat = param & 15u;
    float ph = ((param >> 4) & 255u) / 256.0;
    float t = gTime.x;
    if (pat == 1u) return col * (frac(t * 0.8 + ph) < 0.18 ? 1.6 : 0.03);
    if (pat == 2u) return col * (0.2 + 1.3 * step(0.5, frac(t * 1.5 - ph * 4.0)));
    if (pat == 3u) return hsvToRgbF(frac(t * 0.08 + ph)) * dot(col, 0.3333) * 1.4;
    if (pat == 4u) return col * (0.45 + 0.55 * (0.5 + 0.5 * sin(t * 2.1 + ph * 6.2832)));
    if (pat == 5u) return col * (frac(t * 0.5 - ph) < 0.06 ? 3.0 : 0.0);
    if (pat == 6u) return col * gExposure.w;
    if (pat == 7u) return col * (frac(t * 0.33 + ph) < 0.5 ? 1.0 : 0.05);
    return col;
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
        if ((uint)m.flags & 16) {
            emissive = albedo * i.color.a * 400.0 * m.emissive;
            if (param != 0u) emissive = emissiveAnim(param, emissive);
        }
        if ((uint)m.flags & 8) { sm = SM_FOLIAGE; extra = 0.6; }
        // Large-scale variation to break tiling on big surfaces
        float2 wp = worldP.xy - floor(worldP.xy / 2048.0) * 2048.0;
        float macro = fbmValue(wp * 0.03 + worldP.z * 0.01, 2);
        albedo *= lerp(0.88, 1.08, macro);
    }
    // Rain wetness (sheltered surfaces stay dry), puddles on flat ground with ripples, facade streaks
    float porosity = saturate(rough * 1.3 - 0.15) * (1.0 - metal);
    applyWetness(albedo, rough, n, N, worldP, porosity, 1.0);
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive, i.curClip, i.prevClip);
}
