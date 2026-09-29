// Dynamic objects: rigid models (vehicles, props in motion) and GPU-skinned characters.
#include "gbuffer.hlsli"

struct MaterialInfo {
    float layer, uvScale, roughScale, metal;
    float4 tint;
    float normalScale, shadingModel, flags, emissive;
};
StructuredBuffer<MaterialInfo> tMaterials : register(t10);
Texture2DArray<float4> tMatAlbedo : register(t11);
Texture2DArray<float4> tMatNormal : register(t12);
StructuredBuffer<float4x4> tBones : register(t20);
StructuredBuffer<float4x4> tPrevBones : register(t21);

cbuffer ObjectCB : register(b1) {
    float4x4 gWorld;      // model -> camera-relative world
    float4x4 gPrevWorld;  // previous model -> current camera-relative world
    float4 gTint0;        // primary paint / outfit color, a = dirt
    float4 gTint1;        // secondary color, a = damage
    float4 gObjParams;    // x light bits, y bone offset, z wetness, w emissive scale
    float4 gObjParams2;   // x skinned (1), y time offset, z fade, w unused
    float4 gDamage0;      // crush amount 0..1: front, rear, left, right
    float4 gDamage1;      // roof, underside
    float4 gDmgBoxC;      // model-space collision box center
    float4 gDmgBoxH;      // half extents, w > 0 enables deformation
    float4 gWounds[4];    // characters: bind-pose wound centers (xyz) + radius (w), w = 0 unused
};

float dmgHash(float3 p) { return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453); }
float dmgNoise(float3 p) {
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(lerp(dmgHash(i), dmgHash(i + float3(1, 0, 0)), f.x), lerp(dmgHash(i + float3(0, 1, 0)), dmgHash(i + float3(1, 1, 0)), f.x), f.y);
    float b = lerp(lerp(dmgHash(i + float3(0, 0, 1)), dmgHash(i + float3(1, 0, 1)), f.x), lerp(dmgHash(i + float3(0, 1, 1)), dmgHash(i + float3(1, 1, 1)), f.x), f.y);
    return lerp(a, b, f.z);
}
// Crumple the body toward its interior around damaged zones (irregular, stronger at the extremities).
float3 applyCrush(float3 p, inout float3 n) {
    if (gDmgBoxH.w <= 0.0) return p;
    float3 h = max(gDmgBoxH.xyz, 0.1);
    float3 q = (p - gDmgBoxC.xyz) / h;   // -1..1 inside the box
    float crumple = dmgNoise(p * 3.1) * 0.8 + dmgNoise(p * 7.3) * 0.4;
    float3 d = 0;
    d.y -= gDamage0.x * smoothstep(0.35, 1.05, q.y) * 0.42;
    d.y += gDamage0.y * smoothstep(0.35, 1.05, -q.y) * 0.38;
    d.x += gDamage0.z * smoothstep(0.25, 1.05, -q.x) * 0.22;
    d.x -= gDamage0.w * smoothstep(0.25, 1.05, q.x) * 0.22;
    d.z -= gDamage1.x * smoothstep(0.2, 1.05, q.z) * 0.3;
    d.z += gDamage1.y * smoothstep(0.3, 1.05, -q.z) * 0.1;
    float amount = dot(gDamage0, 1.0) + gDamage1.x;
    float3 off = d * (0.55 + crumple) + (float3(dmgNoise(p * 5.1 + 3.0), dmgNoise(p * 5.1 + 7.0), dmgNoise(p * 5.1 + 11.0)) - 0.5) * 0.03 * saturate(length(d) * 6.0);
    // crumpled panels catch the light differently
    float3 pert = float3(dmgNoise(p * 9.0) - 0.5, dmgNoise(p * 9.0 + 5.0) - 0.5, dmgNoise(p * 9.0 + 9.0) - 0.5);
    n = normalize(n + pert * saturate(length(d) * 5.0) * 1.2);
    return p + off * step(0.001, amount);
}

// Material ids used for special handling (must match MaterialId in render/mesh.h)
#define M_EMISSIVE 28u
#define M_CARPAINT 36u
#define M_PLASTIC 37u
#define M_LIGHT_HEAD 39u
#define M_LIGHT_TAIL 40u
#define M_SKIN 41u
#define M_HAIR 42u
#define M_CLOTH 43u
#define M_DENIM 44u
#define M_EYE 45u
#define M_CAR_GLASS 48u
#define M_LIGHT_INDICATOR 50u

struct VSInRigid {
    float3 pos : POSITION;
    float2 nrm : NORMAL;
    float2 tan : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint mat : MATID;
};
struct VSInSkinned {
    float3 pos : POSITION;
    float2 nrm : NORMAL;
    float2 tan : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint mat : MATID;
    uint4 bones : BONES;
    float4 weights : WEIGHTS;
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
    float3 localPos : TEXCOORD6;
};

VSOut finishVS(float3 localPos, float3 rel, float3 prevRel, float3 n, float3 t, float2 uv, float4 color, uint mat) {
    VSOut o;
    o.rel = rel;
    o.pos = mul(gViewProj, float4(rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(rel, 1));
    o.prevClip = mul(gPrevViewProj, float4(prevRel, 1));
    o.nrm = n;
    o.tan = t;
    o.uv = uv;
    o.color = color;
    o.mat = mat;
    o.localPos = localPos;
    return o;
}

VSOut vsRigid(VSInRigid i) {
    float3 ln = octDecode(i.nrm);
    float3 lp = applyCrush(i.pos, ln);
    float3 rel = mul(gWorld, float4(lp, 1)).xyz;
    float3 prevRel = mul(gPrevWorld, float4(lp, 1)).xyz;
    float3 n = normalize(mul((float3x3)gWorld, ln));
    float3 t = normalize(mul((float3x3)gWorld, octDecode(i.tan)));
    return finishVS(i.pos, rel, prevRel, n, t, i.uv, i.color, i.mat);
}

VSOut vsSkinned(VSInSkinned i) {
    uint off = (uint)gObjParams.y;
    float4x4 m = tBones[off + i.bones.x] * i.weights.x + tBones[off + i.bones.y] * i.weights.y +
                 tBones[off + i.bones.z] * i.weights.z + tBones[off + i.bones.w] * i.weights.w;
    float4x4 pm = tPrevBones[off + i.bones.x] * i.weights.x + tPrevBones[off + i.bones.y] * i.weights.y +
                  tPrevBones[off + i.bones.z] * i.weights.z + tPrevBones[off + i.bones.w] * i.weights.w;
    float3 lp = mul(m, float4(i.pos, 1)).xyz;
    float3 plp = mul(pm, float4(i.pos, 1)).xyz;
    float3 rel = mul(gWorld, float4(lp, 1)).xyz;
    float3 prevRel = mul(gPrevWorld, float4(plp, 1)).xyz;
    float3 n = normalize(mul((float3x3)gWorld, mul((float3x3)m, octDecode(i.nrm))));
    float3 t = normalize(mul((float3x3)gWorld, mul((float3x3)m, octDecode(i.tan))));
    return finishVS(i.pos, rel, prevRel, n, t, i.uv, i.color, i.mat);
}

cbuffer ShadowPassCB : register(b2) {
    float4x4 gShadowViewProj;
};
float4 vsRigidShadow(VSInRigid i) : SV_Position {
    float3 ln = octDecode(i.nrm);
    float3 rel = mul(gWorld, float4(applyCrush(i.pos, ln), 1)).xyz;
    return mul(gShadowViewProj, float4(rel, 1));
}
float4 vsSkinnedShadow(VSInSkinned i) : SV_Position {
    uint off = (uint)gObjParams.y;
    float4x4 m = tBones[off + i.bones.x] * i.weights.x + tBones[off + i.bones.y] * i.weights.y +
                 tBones[off + i.bones.z] * i.weights.z + tBones[off + i.bones.w] * i.weights.w;
    float3 rel = mul(gWorld, float4(mul(m, float4(i.pos, 1)).xyz, 1)).xyz;
    return mul(gShadowViewProj, float4(rel, 1));
}

GBufferOut psDynamic(VSOut i, bool front : SV_IsFrontFace) {
    uint matId = i.mat & 0xffu;
    MaterialInfo m = tMaterials[matId];
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 T = normalize(i.tan - N * dot(i.tan, N));
    float3 B = cross(N, T);
    float2 uv = i.uv * m.uvScale;
    float4 a = tMatAlbedo.Sample(sAnisoWrap, float3(uv, m.layer));
    float4 nr = tMatNormal.Sample(sAnisoWrap, float3(uv, m.layer));
    float3 albedo = a.rgb * i.color.rgb;
    float2 nxy = (nr.xy * 2.0 - 1.0) * m.normalScale;
    float3 n = normalize(T * nxy.x + B * nxy.y + N * sqrt(saturate(1.0 - dot(nxy, nxy))));
    float rough = saturate(nr.z * m.roughScale);
    float metal = m.metal;
    float ao = nr.w;
    uint sm = (uint)m.shadingModel;
    float extra = 0;
    float3 emissive = 0;
    uint lightBits = (uint)gObjParams.x;
    float dirt = gTint0.a;
    if (matId == M_CARPAINT) {
        // Paint color from the object tint (vertex color alpha selects primary/secondary)
        float3 paint = lerp(gTint0.rgb, gTint1.rgb, step(0.5, 1.0 - i.color.a));
        albedo = paint * lerp(1.0, a.r * 1.6, 0.25);
        metal = 0.25;
        rough = lerp(0.28, 0.6, dirt);
        extra = 1.0 - dirt * 0.7;  // clearcoat strength
        n = N;
        // grime towards the bottom of the body
        float grime = saturate((0.6 - i.localPos.z) * 1.5) * dirt;
        albedo = lerp(albedo, float3(0.18, 0.15, 0.12), grime * 0.6);
    } else if (matId == M_CAR_GLASS) {
        albedo = float3(0.01, 0.012, 0.014);
        rough = 0.02;
        metal = 0;
        extra = 1.0;
        sm = SM_CARPAINT;
        n = N;
    } else if (matId == M_LIGHT_HEAD) {
        bool on = (lightBits & 1u) != 0;
        albedo = float3(0.8, 0.8, 0.8);
        rough = 0.05;
        emissive = on ? float3(1.0, 0.95, 0.85) * 3000.0 : 0;
    } else if (matId == M_LIGHT_TAIL) {
        bool on = (lightBits & 1u) != 0;
        bool brake = (lightBits & 2u) != 0;
        bool rev = (lightBits & 4u) != 0;
        albedo = float3(0.35, 0.02, 0.02);
        rough = 0.1;
        float inten = (on ? 180.0 : 0.0) + (brake ? 900.0 : 0.0);
        emissive = float3(1.0, 0.05, 0.03) * inten;
        // reverse lamp region marked by vertex color green
        if (i.color.g > 0.5) emissive = rev ? float3(1, 1, 1) * 800.0 : 0;
    } else if (matId == M_LIGHT_INDICATOR) {
        bool left = (lightBits & 8u) != 0, right = (lightBits & 16u) != 0;
        bool isLeft = i.localPos.x < 0;
        bool blink = frac(gTime.x * 1.5) < 0.5;
        albedo = float3(0.6, 0.35, 0.02);
        emissive = ((isLeft && left) || (!isLeft && right)) && blink ? float3(1.0, 0.55, 0.05) * 700.0 : 0;
        // siren lights (police/ambulance): bit 5, color from vertex color
        if (lightBits & 32u) {
            float ph = frac(gTime.x * 2.2 + (isLeft ? 0.5 : 0.0));
            float on = ph < 0.25 || (ph > 0.35 && ph < 0.55) ? 1.0 : 0.0;
            emissive = i.color.rgb * on * 4000.0;
            albedo = i.color.rgb * 0.3;
        }
    } else if (matId == M_EMISSIVE) {
        emissive = albedo * i.color.a * 400.0 * m.emissive;
    } else if (matId == M_SKIN) {
        albedo = i.color.rgb * lerp(0.85, 1.05, a.r);
        sm = SM_SKIN;
        rough = 0.5;
        extra = 0.6;
    } else if (matId == M_HAIR) {
        albedo = i.color.rgb * (0.6 + a.r * 0.6);
        sm = SM_HAIR;
        rough = 0.35;
    } else if (matId == M_CLOTH || matId == M_DENIM) {
        sm = SM_CLOTH;
        extra = 0.5;
    } else if (matId == M_EYE) {
        rough = 0.05;
        n = N;
    }
    // Blood from wounds (skin, hair and clothing): irregular stains that spread downward, darker and glossier
    if (gObjParams2.x > 0.5 && (sm == SM_SKIN || sm == SM_CLOTH || sm == SM_HAIR || matId == M_CLOTH || matId == M_DENIM)) {
        float blood = 0;
        [unroll] for (int w = 0; w < 4; w++) {
            float r = gWounds[w].w;
            if (r <= 0.0) continue;
            float3 d = i.localPos - gWounds[w].xyz;
            d.z = d.z > 0.0 ? d.z * 1.8 : d.z * 0.6;   // runs down
            float n = dmgNoise(i.localPos * 38.0) * 0.5 + dmgNoise(i.localPos * 11.0);
            blood = max(blood, saturate((r * (0.75 + 0.5 * n) - length(d)) / (r * 0.35)));
        }
        albedo = lerp(albedo, float3(0.16, 0.012, 0.01), blood * 0.92);
        rough = lerp(rough, 0.28, blood);
    }
    // Rain wetness on upward surfaces
    float wet = gWeather.y * saturate(N.z * 2.0 + 0.3) * (gObjParams.z > 0 ? 1.0 : 0.5);
    albedo *= lerp(1.0, 0.7, wet * (sm == SM_CARPAINT ? 0.3 : 1.0));
    rough = lerp(rough, 0.1, wet * 0.7);
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive * gObjParams.w, i.curClip, i.prevClip);
}
