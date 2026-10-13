// Instanced props and vegetation.
#include "gbuffer.hlsli"
#include "weather.hlsli"
#include "materials.hlsli"

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

float3 windOffset(float3 local, float scale, float phase, uint matId, float2 wxy) {
    bool foliage = matId == 32u || matId == 34u;  // MAT_LEAVES, MAT_PALM_FROND
    float h = max(local.z, 0.0) * scale;
    float w = gWeather.w * 0.7 + 0.15 + gWeather.x * 0.6;
    float t = gTime.x;
    // gust fronts travelling across the area (shared with the grass): trees lean further and thrash as one passes
    float gust = windGust(wxy, t);
    float sway = sin(t * 0.9 + phase) * 0.6 + sin(t * 2.1 + phase * 1.7) * 0.25;
    float3 off = float3(gWind.xy, 0) * (sway * (0.7 + 0.6 * gust) + 0.5 + 0.9 * gust) * w * h * h * 0.0035;
    if (foliage) {
        float flutter = sin(t * (6.0 + 3.0 * gust) + phase * 3.0 + local.x * 2.0 + local.y * 1.7) * 0.05 * w * (0.6 + gust);
        off += float3(flutter, flutter * 0.7, flutter * 1.2) * min(h * 0.1, 1.0);
    }
    return off;
}

VSOut vsProp(VSIn i) {
    VSOut o;
    float scale = i.instPos.w;
    float3 local = i.pos * scale;
    float3 p = rotZ(local, i.instRot.x, i.instRot.y);
    p += windOffset(i.pos, scale, i.instRot.z, i.mat & 0xffu, i.instPos.xy + gCamPos.xy);
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
    float3 p = rotZ(i.pos * scale, i.instRot.x, i.instRot.y) + windOffset(i.pos, scale, i.instRot.z, i.mat & 0xffu, i.instPos.xy + gCamPos.xy);
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
        float a = foliageArray().SampleLevel(sLinearWrap, float3(i.uv, layer), 1).a * 1.3;
        clip(a - 0.5);
    }
}

// Street lamp colour of an installation (a 160 m grid; must match world_render.cpp streetLampColor): high-pressure
// sodium, warm-white, neutral or cool-white LED, at unit luminance
float3 lampColorAt(float3 worldP) {
    int2 c = (int2)floor(worldP.xy / 160.0);
    uint h = (uint)c.x * 73856093u ^ (uint)c.y * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    float r = (float)(h & 1023u) / 1023.0;
    float3 k = r < 0.38 ? float3(1.0, 0.46, 0.11) : (r < 0.62 ? float3(1.0, 0.61, 0.31) : (r < 0.86 ? float3(1.0, 0.76, 0.52) : float3(1.0, 0.87, 0.74)));
    return k / luminance(k);
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
    MaterialInfo m = materialInfo(matId);
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 albedo;
    float rough = 0.6, metal = m.metal, ao = 1, extra = 0;
    uint sm = SM_DEFAULT;
    float3 emissive = 0;
    float3 n = N;
    if (matId == 32u || matId == 34u) {
        float layer = floor(i.color.a * 8.0);
        float4 f = foliageArray().Sample(sAnisoWrap, float3(i.uv, layer));
        // Preserve alpha-tested coverage in lower mips (thin leaflets would otherwise vanish)
        float mip = foliageArray().CalculateLevelOfDetail(sAnisoWrap, i.uv);
        float a = f.a * (1.0 + max(mip, 0.0) * 0.28);
        clip(a - 0.5);
        // per-plant colour variation (hue / vigour) from the instance's random wind phase; palm fronds waxier
        float pv = frac(i.inst.z * 0.1591 + 0.37);
        float3 vary = lerp(float3(1.07, 1.02, 0.82), float3(0.88, 1.0, 1.06), pv) * lerp(0.86, 1.1, frac(pv * 7.13));
        albedo = f.rgb * i.color.rgb * 1.1 * vary;
        rough = matId == 34u ? 0.42 : 0.55;
        sm = SM_FOLIAGE;
        extra = matId == 34u ? 0.8 : 0.65;
        // fake curved normals for crossed cards (rounded canopy look)
        n = normalize(N + float3(0, 0, 0.35));
        ao = 0.85;
        if (matId == 34u) {
            // Palm frond (uv.x across, 0.5 = midrib; uv.y 1 at the stalk .. 0 at the tip): a pale waxy rachis,
            // leaflet tips browned by salt and sun (more on some palms than others), and the crown's interior -
            // where the stalks meet - shaded by the fronds above
            float tip = 1.0 - i.uv.y;
            float rib = 1.0 - smoothstep(0.012, 0.035, abs(i.uv.x - 0.5));
            albedo = lerp(albedo, float3(0.36, 0.36, 0.16) * lerp(0.9, 1.15, pv), rib * 0.7);
            float edge = smoothstep(0.25, 0.48, abs(i.uv.x - 0.5));   // leaflet ends dry first
            float dry = smoothstep(0.62, 1.0, tip + edge * 0.25) * lerp(0.25, 0.9, frac(pv * 13.7));
            albedo = lerp(albedo, float3(0.4, 0.3, 0.14), dry);
            extra = lerp(extra, 0.35, dry);                 // dead tissue transmits less light
            ao *= lerp(1.0, 0.6, smoothstep(0.55, 1.0, i.uv.y));
        }
    } else {
        float2 uv = i.uv * m.uvScale;
        float4 a = matAlbedoArray().Sample(sAnisoWrap, float3(uv, m.layer));
        float4 nr = matNormalArray().Sample(sAnisoWrap, float3(uv, m.layer));
        albedo = a.rgb * i.color.rgb;
        float3 T = normalize(i.tan - N * dot(i.tan, N));
        float3 B = cross(N, T);
        float2 nxy = nr.xy * 2.0 - 1.0;
        n = normalize(T * nxy.x + B * nxy.y + N * sqrt(saturate(1.0 - dot(nxy, nxy))));
        rough = saturate(nr.z * m.roughScale);
        ao = nr.w;
        uint barkKind = matId == 33u ? (i.mat >> 8) & 0x7fffffu : 0u;
        if (barkKind == 2u || barkKind == 3u) {
            // Palm trunks (world/propmesh.cpp; uv.y = 3 x height along the trunk, uv.x around it in metres): leaf-scar
            // rings, deep and close on coconut palms (param 2), shallow and wider on the smooth grey royal palm (3).
            // The ring spacing wanders a little; the grooves are darker, and the lowest metre is weathered paler.
            bool royal = barkKind == 3u;
            float along = i.uv.y / 3.0;
            float spacing = royal ? 0.19 : 0.085;
            float wob = valueNoise(float2(along * 3.0, i.inst.z * 5.0)) * 0.35 + valueNoise(float2(i.uv.x * 6.0, along * 11.0)) * 0.15;
            float ph = frac(along / spacing + wob);
            float groove = smoothstep(0.0, 0.18, ph) * (1.0 - smoothstep(0.72, 1.0, ph));   // 1 on the ring's face
            float slope = (ph < 0.18 ? 1.0 : (ph > 0.72 ? -1.0 : 0.0)) * (royal ? 0.25 : 0.6);
            n = normalize(n + T * slope * (1.0 - groove));
            albedo *= lerp(royal ? 0.82 : 0.62, 1.0, groove);
            albedo = lerp(albedo, albedo * float3(1.12, 1.1, 1.06), (1.0 - smoothstep(0.4, 1.4, along)) * (royal ? 0.3 : 0.6));
            ao *= lerp(royal ? 0.9 : 0.7, 1.0, groove);
            rough = saturate(rough + (royal ? -0.05 : 0.08));
        }
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
                // street lamp lenses (colour alpha 0.6) glow in their installation's colour (sodium .. cool LED), the
                // colour world_render.cpp gives the lamp's light (streetLampColor: same 160 m grid and hash)
                float3 lc = abs(i.color.a - 0.6) < 0.03 ? lampColorAt(i.rel + gCamPos.xyz) * luminance(i.color.rgb) : i.color.rgb;
                emissive = lc * intensity * (0.03 + night) * 6.0;
                albedo = i.color.rgb * 0.8;
            }
        }
    }
    applyWetness(albedo, rough, n, N, i.rel + gCamPos.xyz, sm == SM_FOLIAGE ? 0.4 : saturate(rough * 1.2) * (1.0 - metal), 0.0);
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive, i.curClip, i.prevClip);
}
