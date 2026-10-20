// CDLOD terrain: instanced grid patches morphing between LODs, heightmap-displaced.
#include "common.hlsli"
#include "gbuffer.hlsli"
#include "weather.hlsli"

Texture2D<float> tHeight : register(t0);
Texture2D<float4> tSplat0 : register(t1);
Texture2D<float4> tSplat1 : register(t2);
Texture2DArray<float4> tTerrainAlbedo : register(t3);
Texture2DArray<float4> tTerrainNormal : register(t4);
Texture2D<float> tWaterLevel : register(t5);

cbuffer TerrainCB : register(b1) {
    float4 gTerrainParams;   // x world half size, y texel size (m), z heightmap res, w grid resolution (quads per side)
    float4 gMorphRanges[16]; // per LOD: x morph start, y morph end (distance), z node size
};

struct NodeInstance {
    float4 node;  // xy world origin, z size, w lod
};
StructuredBuffer<NodeInstance> tNodes : register(t6);

struct VSIn {
    float2 grid : POSITION;  // 0..1
    uint inst : SV_InstanceID;
};
struct VSOut {
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;     // camera-relative position
    float2 world : TEXCOORD1;   // absolute world xy
    float4 prevClip : TEXCOORD2;
    float4 curClip : TEXCOORD3;
};

#define TL_ROCK_IDX 3
float2 worldToUV(float2 w) { return (w + gTerrainParams.x) / (2.0 * gTerrainParams.x); }

float sampleHeight(float2 w) { return tHeight.SampleLevel(sLinearClamp, worldToUV(w), 0); }

VSOut vsTerrain(VSIn i) {
    NodeInstance n = tNodes[i.inst];
    float N = gTerrainParams.w;
    float2 w = n.node.xy + i.grid * n.node.z;
    float h = sampleHeight(w);
    float3 relP = float3(w - gCamPos.xy, h - gCamPos.z);
    float dist = length(relP);
    int lod = (int)n.node.w;
    float2 mr = gMorphRanges[lod].xy;
    float morph = saturate((dist - mr.x) / max(mr.y - mr.x, 1e-3));
    // morph toward the parent grid
    float2 g = i.grid * N;
    float2 fracPart = frac(g * 0.5) * 2.0;
    g -= fracPart * morph;
    w = n.node.xy + (g / N) * n.node.z;
    h = sampleHeight(w);
    VSOut o;
    o.world = w;
    o.rel = float3(w - gCamPos.xy, h - gCamPos.z);
    o.pos = mul(gViewProj, float4(o.rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(o.rel, 1));
    o.prevClip = mul(gPrevViewProj, float4(o.rel, 1));
    return o;
}

// Depth-only variant for shadow maps (uses the pass matrix in b2)
cbuffer ShadowPassCB : register(b2) {
    float4x4 gShadowViewProj;
};
float4 vsTerrainShadow(VSIn i) : SV_Position {
    NodeInstance n = tNodes[i.inst];
    float2 w = n.node.xy + i.grid * n.node.z;
    float h = sampleHeight(w);
    return mul(gShadowViewProj, float4(w - gCamPos.xy, h - gCamPos.z, 1));
}

// ------------------------------------------------------------------------------------------------
float3 terrainNormal(float2 w) {
    float e = gTerrainParams.y;
    float hl = sampleHeight(w - float2(e, 0)), hr = sampleHeight(w + float2(e, 0));
    float hd = sampleHeight(w - float2(0, e)), hu = sampleHeight(w + float2(0, e));
    return normalize(float3(hl - hr, hd - hu, 2.0 * e));
}

struct LayerSample {
    float3 albedo;
    float3 normalTS;
    float rough;
    float height;
};

LayerSample sampleLayer(int layer, float2 uv, float2 uvFar, float farBlend) {
    LayerSample s;
    float4 a = tTerrainAlbedo.Sample(sAnisoWrap, float3(uv, layer));
    float4 nr = tTerrainNormal.Sample(sAnisoWrap, float3(uv, layer));
    float4 aF = tTerrainAlbedo.Sample(sAnisoWrap, float3(uvFar, layer));
    // Distance blend with a larger-scale sample reduces visible tiling
    a.rgb = lerp(a.rgb, a.rgb * (aF.rgb / max(luminance(aF.rgb), 0.02)) * luminance(a.rgb) * 1.0, 0.35 * farBlend);
    s.albedo = a.rgb;
    s.height = a.a;
    s.normalTS = float3(nr.xy * 2.0 - 1.0, 0);
    s.normalTS.z = sqrt(saturate(1.0 - dot(s.normalTS.xy, s.normalTS.xy)));
    s.rough = nr.z;
    return s;
}

GBufferOut psTerrain(VSOut i) {
    float2 w = i.world;
    float2 uvT = worldToUV(w);
    float4 s0 = tSplat0.Sample(sLinearClamp, uvT);
    float4 s1 = tSplat1.Sample(sLinearClamp, uvT);
    // Break up the 8 m splat texels with noise
    float2 wrapped = w - floor(w / 2048.0) * 2048.0;
    float nz = fbmValue(wrapped * 0.15, 3) - 0.5;
    float weights[8] = {s0.x, s0.y, s0.z, s0.w, s1.x, s1.y, s1.z, s1.w};
    float3 N = terrainNormal(w);
    float dist = length(i.rel);
    float2 uv = wrapped / 4.0;   // 4 m tiling for detail
    float2 uvFar = wrapped / 37.0;
    float farBlend = saturate(dist / 60.0);
    // Height-blended layer mixing (sharpens transitions using layer height maps)
    float3 albedo = 0, nTS = 0;
    float rough = 0, wsum = 0;
    float bestH = -10;
    int bestIdx = 0;
    LayerSample ls[8];
    float hw[8];
    [unroll] for (int k = 0; k < 8; k++) {
        hw[k] = 0;
        ls[k].albedo = 0; ls[k].normalTS = float3(0, 0, 1); ls[k].rough = 1; ls[k].height = 0;
        if (weights[k] > 0.01) {
            ls[k] = sampleLayer(k, uv, uvFar, farBlend);
            hw[k] = weights[k] + ls[k].height * 0.35 + nz * 0.25;
            if (hw[k] > bestH) { bestH = hw[k]; bestIdx = k; }
        }
    }
    [unroll] for (int k2 = 0; k2 < 8; k2++) {
        if (weights[k2] > 0.01) {
            float bw = max(hw[k2] - bestH + 0.2, 0.0) * weights[k2];
            albedo += ls[k2].albedo * bw;
            nTS += ls[k2].normalTS * bw;
            rough += ls[k2].rough * bw;
            wsum += bw;
        }
    }
    albedo /= max(wsum, 1e-4);
    nTS = normalize(nTS / max(wsum, 1e-4) + float3(0, 0, 1e-4));
    rough /= max(wsum, 1e-4);
    // Close-up detail: the dominant layer at a higher, rotated frequency (grain, pebbles, soil crumbs)
    float detailW = saturate(1.0 - dist / 20.0);
    if (detailW > 0.0) {
        float2 duv = float2(uv.x * 0.8 - uv.y * 0.6, uv.x * 0.6 + uv.y * 0.8) * 3.7 + 0.21;
        float4 dn = tTerrainNormal.Sample(sAnisoWrap, float3(duv, bestIdx));
        nTS = normalize(float3(nTS.xy + (dn.xy * 2.0 - 1.0) * 0.6 * detailW, nTS.z));
        rough = saturate(rough * lerp(1.0, 0.75 + dn.z * 0.5, detailW * 0.5));
    }
    // Macro color variation
    float macro = fbmValue(wrapped * 0.004, 3);
    albedo *= lerp(0.82, 1.12, macro);
    // Tangent frame from terrain normal (world X as tangent)
    float3 T = normalize(cross(float3(0, 1, 0), N));
    float3 B = cross(N, T);
    float3 n = normalize(T * nTS.x + B * nTS.y + N * nTS.z);
    // Shoreline wetness (always) + rain wetness / puddles
    float wl = tWaterLevel.SampleLevel(sLinearClamp, uvT, 0);
    float h = i.rel.z + gCamPos.z;
    float shoreWet = wl > -999.0 ? saturate(1.0 - (h - wl) / 0.6) : 0.0;
    albedo *= lerp(1.0, 0.55, shoreWet);
    rough = lerp(rough, 0.08, shoreWet * 0.85);
    n = normalize(lerp(n, N, shoreWet * 0.7));
    applyWetness(albedo, rough, n, N, float3(w, h), 0.9, 1.0 - weights[TL_ROCK_IDX] * 0.8);

    GBufferOut o;
    o = packGBuffer(albedo, 1.0, n, rough, 0.0, SM_DEFAULT, 0.0, 0.0, i.curClip, i.prevClip);
    return o;
}
