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
    // Reduce flashing (accessibility): blinking / strobing patterns become a slow gentle pulse
    if (gRenderFlags.x > 0.5 && (pat == 1u || pat == 2u || pat == 5u || pat == 7u))
        return col * (0.55 + 0.25 * sin(t * 1.2 + ph * 6.2832));
    if (pat == 1u) return col * (frac(t * 0.8 + ph) < 0.18 ? 1.6 : 0.03);
    if (pat == 2u) return col * (0.2 + 1.3 * step(0.5, frac(t * 1.5 - ph * 4.0)));
    if (pat == 3u) return hsvToRgbF(frac(t * 0.08 + ph)) * dot(col, 0.3333) * 1.4;
    if (pat == 4u) return col * (0.45 + 0.55 * (0.5 + 0.5 * sin(t * 2.1 + ph * 6.2832)));
    if (pat == 5u) return col * (frac(t * 0.5 - ph) < 0.06 ? 3.0 : 0.0);
    if (pat == 6u) return col * gExposure.w;
    if (pat == 7u) return col * (frac(t * 0.33 + ph) < 0.5 ? 1.0 : 0.05);
    return col;
}

static float sGutter = 0.0;   // asphalt pixel in the gutter strip along the kerb (wet: running water)

// Thin line mask with a minimum on-screen width (energy kept: thinner than ~2 px -> wider and fainter).
float lineMask(float d, float halfWidth, float px) {
    float w = max(halfWidth, px);
    return smoothstep(w, w * 0.35, d) * (halfWidth / w);
}

// Asphalt wear in road space (ruv: u across, v along the road, meters; world-aligned on junctions and lots):
// polished darker wheel tracks and an oil-drip band per ~3.5 m lane, occasional utility-cut patches with sealed
// seams, sparse meandering longitudinal cracks near lane joints and partial transverse cracks, some of them
// sealed with glossy tar snakes. Procedural in world space, so nothing repeats with the 4 m texture tile.
// Street furniture in the road surface: cast-iron manhole covers near the lane centres every few tens of metres and
// storm-drain grates in the gutter. Road space as in roadWear; roadW = carriageway width (0 = unknown: only the left
// kerb, u = 0, is known). Writes iron where a cover / grate is, height (m) for the bump.
void roadFurniture(float2 ruv, float roadW, float px, inout float3 albedo, inout float rough, inout float metal,
                   inout float ao, inout float hgt, out float gutter) {
    float u = ruv.x, v = ruv.y;
    // gutter: the strip along the kerb collects grit, leaves and oily grime (and runs with water in the rain)
    float dEdge = roadW > 0.0 ? min(u, roadW - u) : u;
    gutter = saturate(1.0 - dEdge / 0.45) * step(0.0, dEdge);
    // manhole covers: one per 48 m stretch (two in three stretches), in the first or second lane
    float mc = floor(v / 48.0);
    uint mh = hash2u(uint2(asuint((int)mc), 0x3c1u));
    if (hashF(mh) < 0.66) {
        float2 c = float2(hashF(mh + 1u) < 0.5 || roadW < 7.0 ? 1.75 : 5.25, mc * 48.0 + 6.0 + hashF(mh + 2u) * 36.0);
        float2 d = float2(u, v) - c;
        float r = length(d);
        if (r < 0.36) {
            float ring = frac(r / 0.045);
            float ang = atan2(d.y, d.x);
            float ribs = step(0.8, frac(ang / (TWO_PI / 24.0))) * step(0.1, r);
            float raised = r > 0.32 ? 1.0 : saturate(step(0.55, ring) * 0.7 + ribs * 0.6);
            float gap = smoothstep(0.305, 0.312, r) * (1.0 - smoothstep(0.318, 0.325, r));   // seat gap
            albedo = lerp(float3(0.11, 0.105, 0.1), float3(0.18, 0.17, 0.155), raised) * (1.0 - gap * 0.8);
            metal = 0.75;
            rough = lerp(0.62, 0.42, raised);   // raised pattern polished by tyres
            ao *= 1.0 - gap * 0.7;
            hgt += raised * 0.003 - gap * 0.004;
        }
    }
    // storm drains at the kerb: one per 32 m stretch on either side (alternating), grate bars across the flow
    float dc = floor(v / 32.0);
    uint dh = hash2u(uint2(asuint((int)dc), 0x5d9u));
    if (hashF(dh) < 0.6) {
        bool right = roadW > 0.0 && hashF(dh + 1u) < 0.5;
        float gu = right ? roadW - u : u;
        float gv = v - (dc * 32.0 + 4.0 + hashF(dh + 2u) * 24.0);
        if (gu > 0.04 && gu < 0.5 && abs(gv) < 0.45) {
            float frame = step(min(min(gu - 0.04, 0.5 - gu), 0.45 - abs(gv)), 0.035);
            float bar = step(0.45, frac(gv / 0.055));
            float solid = max(frame, bar);
            albedo = lerp(float3(0.008, 0.008, 0.008), float3(0.13, 0.12, 0.11), solid);
            metal = lerp(0.0, 0.75, solid);
            rough = lerp(0.95, 0.5, solid);
            ao *= lerp(0.15, 1.0, solid);
            hgt -= (1.0 - solid) * 0.02;
        }
    }
}

// Asphalt surface grain: light aggregate tops (~6 mm) in the dark binder, only where a stone covers ~3 pixels or
// more (with a little relief), plus smooth cluster and binder-richness tone at 2 and 7 cm that fades the same way.
// Nothing is magnified from a texture and nothing shimmers far away. Returns an albedo factor; hgt receives the
// micro height (m).
float asphaltGrain(float2 worldXY, float fp, out float hgt) {
    float2 p = worldXY - floor(worldXY / 48.0) * 48.0;
    float ifp = 1.0 / max(fp, 1e-5);
    float g = 0.0;
    hgt = 0.0;
    float vis = saturate((0.006 * ifp - 2.5) * 0.5);
    if (vis > 0.0) {
        float stone = smoothstep(0.55, 0.7, valueNoise(p / 0.006)) * (0.7 + 0.3 * valueNoise(p / 0.0035 + 7.1));
        g += (stone - 0.25) * 0.28 * vis;
        hgt = stone * 0.0008 * vis;
    }
    g += (valueNoise(p / 0.02 + 3.3) - 0.5) * 0.08 * saturate((0.02 * ifp - 2.0) * 0.5);
    g += (valueNoise(p / 0.07 + 9.1) - 0.5) * 0.07 * saturate((0.07 * ifp - 2.0) * 0.5);
    return 1.0 + g;
}

void roadWear(float2 ruv, float camDist, inout float3 albedo, inout float rough, inout float ao, out float rut) {
    rut = 0.0;
    float fade = saturate(1.6 - camDist / 110.0);
    if (fade <= 0.0) return;
    float u = ruv.x, v = ruv.y;
    float px = camDist * 0.0012;   // ~1.5-2 pixels at 1080p-1440p
    // lanes: rubber-darkened, slightly polished wheel tracks at +-0.9 m from the lane centre, oil drips between them
    float lu = frac(u / 3.5) * 3.5 - 1.75;
    float track = exp(-sq((abs(lu) - 0.9) / 0.32)) * (0.75 + 0.25 * valueNoise(float2(v * 0.08, u * 0.3)));
    float drip = saturate(valueNoise(float2(v * 0.3, floor(u / 3.5) * 5.1)) * 1.6 - 0.3);
    drip *= 0.6 + 0.4 * smoothstep(0.4, 0.8, valueNoise(float2(v * 2.3, u * 2.3)));   // individual drips
    float oil = exp(-sq(lu / 0.24)) * drip;
    rut = track * fade;
    albedo *= 1.0 - (0.22 * track + 0.28 * oil) * fade;
    rough = saturate(rough * (1.0 - (0.3 * track + 0.2 * oil) * fade));
    // utility-cut patches (one in ~14 cells of 14 m x 3.5 m)
    float2 pc = float2(floor(v / 14.0), floor(u / 3.5));
    uint ph = hash2u(asuint(int2(pc)) + 0x51u);
    if (hashF(ph) < 0.07) {
        float2 lo = float2(hashF(ph + 1u) * 6.0, hashF(ph + 2u) * 1.2);
        float2 sz = float2(1.5 + hashF(ph + 3u) * 4.5, 1.0 + hashF(ph + 4u) * 1.6);
        float2 q = float2(v - pc.x * 14.0, u - pc.y * 3.5) - lo;
        if (all(q > 0.0) && all(q < sz)) {
            bool fresh = hashF(ph + 5u) < 0.5;
            albedo *= fresh ? 0.75 : 1.1;
            rough = saturate(rough + (fresh ? -0.06 : 0.03));
            float edge = min(min(q.x, sz.x - q.x), min(q.y, sz.y - q.y));
            albedo *= 1.0 - 0.35 * lineMask(edge, 0.025, px) * fade;   // sealed seam
        }
    }
    // longitudinal cracks meandering near the lane joints, only along some stretches
    float lane = floor(u / 3.5 + 0.5);
    float lj = u - lane * 3.5;
    float meander = (valueNoise(float2(v * 0.19, lane * 3.1)) - 0.5) * 0.6 + (valueNoise(float2(v * 1.4, lane + 7.1)) - 0.5) * 0.07;
    float dL = abs(lj - meander);
    float presentL = smoothstep(0.58, 0.72, valueNoise(float2(v * 0.04, lane * 3.7 + 1.3)));
    // partial transverse cracks, one every few cells of 9 m
    float cell = floor(v / 9.0);
    uint th = hash2u(uint2(asuint((int)cell), 0x7a3u));
    float dT = 10.0, presentT = 0.0;
    if (hashF(th) < 0.3) {
        float v0 = cell * 9.0 + 1.0 + hashF(th + 1u) * 7.0;
        float jag = (valueNoise(float2(u * 1.6, cell)) - 0.5) * 0.4 + (valueNoise(float2(u * 6.5, cell + 3.0)) - 0.5) * 0.06;
        dT = abs(v - v0 - jag);
        float us = hashF(th + 2u) * 4.0, ue = us + 2.0 + hashF(th + 3u) * 6.0;
        presentT = smoothstep(us, us + 0.5, u) * smoothstep(ue, ue - 0.5, u);
    }
    // sealed cracks: a wide glossy tar band over the crack instead of an open line
    bool sealedL = valueNoise(float2(v * 0.013, lane * 1.9 + 4.0)) > 0.55;
    bool sealedT = hashF(th + 4u) < 0.4;
    float crack = max(lineMask(dL, 0.012, px) * presentL * (sealedL ? 0.0 : 1.0), lineMask(dT, 0.01, px) * presentT * (sealedT ? 0.0 : 1.0));
    float tar = max(lineMask(dL, 0.045, px) * presentL * (sealedL ? 1.0 : 0.0), lineMask(dT, 0.04, px) * presentT * (sealedT ? 1.0 : 0.0));
    albedo *= 1.0 - (crack * 0.45 + tar * 0.5) * fade;
    rough = saturate(lerp(rough, 0.35, tar * fade));
    ao *= 1.0 - crack * 0.35 * fade;
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
    float rut = 0;   // wheel-track strength on asphalt (wet-road water film)
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
        rough = saturate(nr.z * m.roughScale);
        // Close-up detail: the same layer at a higher, rotated frequency adds micro normals and roughness
        // variation (asphalt grain, concrete pores, stucco) and hides the base tiling
        float camDist = length(i.rel);
        float detailW = saturate(1.0 - camDist / 22.0) * (matId <= 1u ? 0.0 : 1.0);   // asphalt: procedural grain below
        if (detailW > 0.0) {
            float2 duv = float2(uv.x * 0.8 - uv.y * 0.6, uv.x * 0.6 + uv.y * 0.8) * 4.7 + 0.37;
            float4 nr2 = tMatNormal.Sample(sAnisoWrap, float3(duv, m.layer));
            nxy += (nr2.xy * 2.0 - 1.0) * m.normalScale * 0.55 * detailW;
            rough = saturate(rough * lerp(1.0, 0.7 + nr2.z * 0.6, detailW * 0.6));
        }
        float3 nts = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
        n = normalize(T * nts.x + B * nts.y + N * nts.z);
        // Worn / polished patches: large-scale roughness variation
        float2 wpr = worldP.xy - floor(worldP.xy / 1024.0) * 1024.0;
        rough = saturate(rough * lerp(0.82, 1.12, fbmValue(wpr * 0.45 + worldP.z * 0.2, 2)));
        metal = m.metal;
        ao = nr.w;
        if (matId <= 1u) {
            // MAT_ASPHALT, MAT_ASPHALT_OLD: aggregate grain, lane wear, cracks and patches, covers and drains
            float3 dPx = ddx(i.rel), dPy = ddy(i.rel);
            float fp = max(length(dPx), length(dPy));
            float hgt = 0.0;
            albedo *= asphaltGrain(worldP.xy, fp, hgt);
            roadWear(i.uv, length(i.rel), albedo, rough, ao, rut);
            float gutter = 0.0;
            float roadW = i.color.a < 0.995 ? i.color.a * 64.0 : 0.0;
            roadFurniture(i.uv, roadW, length(i.rel) * 0.0012, albedo, rough, metal, ao, hgt, gutter);
            albedo *= 1.0 - gutter * 0.3;
            rough = saturate(rough + gutter * 0.05);
            n = perturbBump(n, N, dPx, dPy, hgt);
            sGutter = gutter;
        } else if (matId == 5u || matId == 6u) {
            // road paint: chipped where the traffic wears it, grit showing through; the chips fade into an average
            // wear before they get smaller than ~3 pixels
            float3 dPx = ddx(i.rel), dPy = ddy(i.rel);
            float fp = max(length(dPx), length(dPy));
            float2 wp2 = worldP.xy - floor(worldP.xy / 64.0) * 64.0;
            float patchy = valueNoise(wp2 * 1.3 + 4.1) - 0.5;
            float chipsN = valueNoise(wp2 * 30.0) * 0.6 + valueNoise(wp2 * 75.0) * 0.4;
            float vis = saturate((0.013 / max(fp, 1e-5) - 2.5) * 0.5);
            float worn = lerp(saturate(0.25 + patchy * 0.6), smoothstep(0.6, 0.78, chipsN + patchy * 0.5), vis);
            albedo = lerp(albedo, float3(0.075, 0.072, 0.07), worn * 0.85);
            rough = lerp(rough, 0.9, worn);
        }
        if ((uint)m.flags & 16) {
            emissive = albedo * i.color.a * 400.0 * m.emissive;
            if (param != 0u) emissive = emissiveAnim(param, emissive);
        }
        if ((uint)m.flags & 8) { sm = SM_FOLIAGE; extra = 0.6; }
        // Large-scale variation to break tiling on big surfaces
        float2 wp = worldP.xy - floor(worldP.xy / 2048.0) * 2048.0;
        float macro = fbmValue(wp * 0.03 + worldP.z * 0.01, 2);
        albedo *= lerp(0.88, 1.08, macro);
        // Grime at the foot of walls (splash-back, dirt) on vertical surfaces
        if (abs(N.z) < 0.45 && metal < 0.5) {
            float ground = gTerrainHeightG.SampleLevel(sLinearClamp, (worldP.xy + 10240.0) / 20480.0, 0);
            float above = worldP.z - ground;
            float2 side = normalize(float2(-N.y, N.x) + 1e-5);
            float streak = valueNoise(float2(dot(worldP.xy, side) * 3.1, worldP.z * 9.0)) * 0.5 + valueNoise(float2(dot(worldP.xy, side) * 0.9, 1.3)) * 0.5;
            float grime = saturate(1.0 - above / (0.6 + streak * 0.9)) * saturate(above * 4.0 + 0.3);
            albedo *= lerp(1.0, float3(0.62, 0.6, 0.56), grime * 0.8);
            rough = saturate(rough + grime * 0.08);
        }
    }
    // Rain wetness (sheltered surfaces stay dry), puddles on flat ground with ripples, facade streaks
    float porosity = saturate(rough * 1.3 - 0.15) * (1.0 - metal);
    applyWetness(albedo, rough, n, N, worldP, porosity, 1.0);
    // Water lingers in the slightly rutted wheel tracks: glossy reflective streaks along the lanes when wet
    if ((rut > 0.0 || sGutter > 0.0) && gWeather.y > 0.01) {
        float film = saturate(gWeather.y * 1.3 - 0.2) * max(rut, sGutter) * skyExposure(worldP);
        rough = lerp(rough, 0.05, film * 0.85);
        albedo *= 1.0 - film * 0.12;
    }
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive, i.curClip, i.prevClip);
}
