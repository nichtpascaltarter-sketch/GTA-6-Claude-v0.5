// Instanced props and vegetation.
#include "gbuffer.hlsli"

struct MaterialInfo {
    float layer, uvScale, roughScale, metal;
    float4 tint;
    float normalScale, shadingModel, flags, emissive;
};
StructuredBuffer<MaterialInfo> tMaterials : register(t10);
Texture2DArray<float4> tMatAlbedo : register(t11);
Texture2DArray<float4> tMatNormal : register(t12);
Texture2DArray<float4> tFoliage : register(t15);

struct VSIn {
    float3 pos : POSITION;
    float2 nrm : NORMAL;
    float2 tan : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint mat : MATID;
    float4 instPos : INSTPOS;   // xyz camera-relative, w scale
    float4 instRot : INSTROT;   // x cos(yaw), y sin(yaw), z wind phase, w flags (traffic phase etc.)
};
struct VSOut {
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float3 tan : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 color : COLOR0;
    nointerpolation uint mat : MATID;
    float4 curClip : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
    nointerpolation float4 inst : TEXCOORD6;
};

float3 rotZ(float3 v, float c, float s) { return float3(c * v.x - s * v.y, s * v.x + c * v.y, v.z); }

float3 windOffset(float3 local, float scale, float phase, uint matId) {
    bool foliage = matId == 32u || matId == 34u;  // MAT_LEAVES, MAT_PALM_FROND
    float h = max(local.z, 0.0) * scale;
    float w = gWeather.w * 0.7 + 0.15 + gWeather.x * 0.6;
    float t = gTime.x;
    float sway = sin(t * 0.9 + phase) * 0.6 + sin(t * 2.1 + phase * 1.7) * 0.25;
    float3 off = float3(gWind.xy, 0) * (sway + 0.8) * w * h * h * 0.0035;
    if (foliage) {
        float flutter = sin(t * 6.0 + phase * 3.0 + local.x * 2.0 + local.y * 1.7) * 0.05 * w;
        off += float3(flutter, flutter * 0.7, flutter * 1.2) * min(h * 0.1, 1.0);
    }
    return off;
}

VSOut vsProp(VSIn i) {
    VSOut o;
    float scale = i.instPos.w;
    float3 local = i.pos * scale;
    float3 p = rotZ(local, i.instRot.x, i.instRot.y);
    p += windOffset(i.pos, scale, i.instRot.z, i.mat & 0xffu);
    float3 rel = p + i.instPos.xyz;
    o.rel = rel;
    o.pos = mul(gViewProj, float4(rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(rel, 1));
    o.prevClip = mul(gPrevViewProj, float4(rel, 1));
    o.nrm = rotZ(octDecode(i.nrm), i.instRot.x, i.instRot.y);
    o.tan = rotZ(octDecode(i.tan), i.instRot.x, i.instRot.y);
    o.uv = i.uv;
    o.color = i.color;
    o.mat = i.mat;
    o.inst = i.instRot;
    return o;
}

cbuffer ShadowPassCB : register(b2) {
    float4x4 gShadowViewProj;
};
struct VSShadowOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint mat : MATID;
};
VSShadowOut vsPropShadow(VSIn i) {
    VSShadowOut o;
    float scale = i.instPos.w;
    float3 p = rotZ(i.pos * scale, i.instRot.x, i.instRot.y) + windOffset(i.pos, scale, i.instRot.z, i.mat & 0xffu);
    o.pos = mul(gShadowViewProj, float4(p + i.instPos.xyz, 1));
    o.uv = i.uv;
    o.color = i.color;
    o.mat = i.mat;
    return o;
}
void psPropShadow(VSShadowOut i) {
    uint m = i.mat & 0xffu;
    if (m == 32u || m == 34u) {
        float layer = floor(i.color.a * 8.0);
        float a = tFoliage.SampleLevel(sLinearWrap, float3(i.uv, layer), 1).a * 1.3;
        clip(a - 0.5);
    }
}

// Traffic signal phase: 0 red, 1 yellow, 2 green. Driven by the traffic system through instRot.w (phase*4 + 0.5)
// or by a local timer when w < 0.
float signalState(float4 inst) {
    if (inst.w >= 0) return floor(inst.w);
    float t = frac((gTime.x + inst.z * 10.0) / 32.0) * 32.0;
    return t < 15.0 ? 0 : (t < 28.0 ? 2 : 1);
}

GBufferOut psProp(VSOut i, bool front : SV_IsFrontFace) {
    uint matId = i.mat & 0xffu;
    MaterialInfo m = tMaterials[matId];
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 albedo;
    float rough = 0.6, metal = m.metal, ao = 1, extra = 0;
    uint sm = SM_DEFAULT;
    float3 emissive = 0;
    float3 n = N;
    if (matId == 32u || matId == 34u) {
        float layer = floor(i.color.a * 8.0);
        float4 f = tFoliage.Sample(sAnisoWrap, float3(i.uv, layer));
        // Preserve alpha-tested coverage in lower mips (thin leaflets would otherwise vanish)
        float mip = tFoliage.CalculateLevelOfDetail(sAnisoWrap, i.uv);
        float a = f.a * (1.0 + max(mip, 0.0) * 0.28);
        clip(a - 0.5);
        albedo = f.rgb * i.color.rgb * 1.1;
        rough = 0.55;
        sm = SM_FOLIAGE;
        extra = 0.65;
        // fake curved normals for crossed cards (rounded canopy look)
        n = normalize(N + float3(0, 0, 0.35));
        ao = 0.85;
    } else {
        float2 uv = i.uv * m.uvScale;
        float4 a = tMatAlbedo.Sample(sAnisoWrap, float3(uv, m.layer));
        float4 nr = tMatNormal.Sample(sAnisoWrap, float3(uv, m.layer));
        albedo = a.rgb * i.color.rgb;
        float3 T = normalize(i.tan - N * dot(i.tan, N));
        float3 B = cross(N, T);
        float2 nxy = nr.xy * 2.0 - 1.0;
        n = normalize(T * nxy.x + B * nxy.y + N * sqrt(saturate(1.0 - dot(nxy, nxy))));
        rough = saturate(nr.z * m.roughScale);
        ao = nr.w;
        if ((uint)m.flags & 16u) {
            // Emissive: street lamps glow at night; traffic lamps follow the signal state
            float night = gExposure.w;
            float intensity = i.color.a * 400.0 * m.emissive;
            uint param = (i.mat >> 8) & 0x7fffffu;
            if (param == 1u) {
                float lampId = floor(i.color.a * 4.0 + 0.5) - 1.0;  // 0 red, 1 yellow, 2 green
                float st = signalState(i.inst);
                float on = (lampId == 0 && st == 0) || (lampId == 1 && st == 1) || (lampId == 2 && st == 2) ? 1.0 : 0.04;
                emissive = i.color.rgb * on * 900.0;
                albedo = i.color.rgb * 0.15;
            } else {
                emissive = i.color.rgb * intensity * (0.03 + night) * 6.0;
                albedo = i.color.rgb * 0.8;
            }
        }
    }
    float wet = gWeather.y * saturate(N.z * 2.0 + 0.3);
    albedo *= lerp(1.0, 0.65, wet);
    rough = lerp(rough, 0.15, wet * 0.8);
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive, i.curClip, i.prevClip);
}
