// GPU-driven grass: a compute pass scatters clumps on a world-anchored grid around the camera from the terrain
// splat (grass / sawgrass / forest floor / dune grass), excluding anything covered by roads, sidewalks or
// buildings (overhead height map), water and steep slopes; clumps are frustum culled into two LOD append
// buffers and drawn with procedural, wind-animated blades (plus small flowers) into the G-buffer.
#include "gbuffer.hlsli"
#include "weather.hlsli"

struct GrassInstance {
    float3 rel;       // camera-relative root position
    float yaw;
    float height;     // blade height (m), already faded with distance
    float width;      // blade base width (m)
    uint colorType;   // rgb8 base color | type << 24 (0 lawn, 1 meadow, 2 sawgrass, 3 forest floor, 4 dune)
    float seed;
};

cbuffer GrassCB : register(b1) {
    float4 gGrass0;       // xy grid origin (world, cell corner), z cell size (m), w grid dimension (cells per side)
    float4 gGrass1;       // x inner radius (skip), y outer radius, z fade start, w density scale
    float4 gGrass2;       // x wind strength, y time, z previous time, w blades per clump
    float4 gGrass3;       // x segments per blade, y lod (0 near, 1 far), z world half size, w unused
    float4 gFrustum[6];   // camera-relative planes (xyz normal pointing inside, w distance)
};

Texture2D<float4> tSplat0 : register(t0);   // sand, grass, dirt, rock
Texture2D<float4> tSplat1 : register(t1);   // mud, sawgrass, forest, urban
AppendStructuredBuffer<GrassInstance> uGrass : register(u0);
StructuredBuffer<GrassInstance> tGrass : register(t2);
Texture2D<float> tOverheadGrass : register(t3);   // 1 where the top static surface is a lawn / median mesh

float2 worldToTerrainUV(float2 w) { return (w + gGrass3.z) / (2.0 * gGrass3.z); }

[numthreads(8, 8, 1)]
void csGrassPlace(uint3 id : SV_DispatchThreadID) {
    float n = gGrass0.w;
    if (id.x >= (uint)n || id.y >= (uint)n) return;
    float cell = gGrass0.z;
    float2 cellMin = gGrass0.xy + float2(id.xy) * cell;
    int2 ci = int2(floor(cellMin / cell + 0.5));
    uint h = hash2u(asuint(ci) + uint2(0x9e37u, 0x7f4au) * (uint)(gGrass3.y + 1.0));
    float2 jitter = float2(hashF(h), hashF(h ^ 0x68bc21ebu));
    float2 wp = cellMin + jitter * cell;
    float2 relXY = wp - gCamPos.xy;
    float dist2 = dot(relXY, relXY);
    float rIn = gGrass1.x, rOut = gGrass1.y;
    if (dist2 < rIn * rIn || dist2 > rOut * rOut) return;
    float dist = sqrt(dist2);
    float2 tuv = worldToTerrainUV(wp);
    float4 s0 = tSplat0.SampleLevel(sLinearClamp, tuv, 0);
    float4 s1 = tSplat1.SampleLevel(sLinearClamp, tuv, 0);
    float grass = s0.y, sand = s0.x, sawg = s1.y, forest = s1.z, urban = s1.w, rock = s0.w, dirt = s0.z, mud = s1.x;
    // Lawns and medians modelled as world meshes (overhead pass flags their material)
    float groundZ = gTerrainHeightG.SampleLevel(sLinearClamp, tuv, 0);
    bool meshLawn = false;
    if (gOverhead.w > 0.5) {
        float2 ouv = (wp - gOverhead.xy) / gOverhead.z;
        if (all(ouv > 0.0) && all(ouv < 1.0)) {
            float top = gOverheadMap.SampleLevel(sPointClamp, ouv, 0);
            if (top > groundZ - 0.35) {
                if (tOverheadGrass.SampleLevel(sPointClamp, ouv, 0) < 0.5) return;   // road, sidewalk, lot, roof, bridge
                meshLawn = true;
                groundZ = top;
            }
        }
    }
    if (meshLawn) { grass = 1.0; urban = 0.4; sand = 0; sawg = 0; forest = 0; rock = 0; dirt = 0; mud = 0; }
    // break up the 8 m splat texels with noise so patches have organic edges
    float2 wr = wp - floor(wp / 2048.0) * 2048.0;
    float patch = fbmValue(wr * 0.09, 3);
    float cover = grass * 1.0 + sawg * 1.1 + forest * 0.55 + sand * 0.18 - (dirt + rock + mud) * 0.5 - urban * 0.6;
    cover += (patch - 0.5) * 0.7;
    float r = hashF(h ^ 0x02e5be93u);
    // thin out with distance (larger far clumps compensate) and by quality
    float keep = saturate(cover) * gGrass1.w;
    if (r > keep) return;
    // ground height / slope / water
    float hC = groundZ;
    float e = 2.0 / (2.0 * gGrass3.z);
    float hx = gTerrainHeightG.SampleLevel(sLinearClamp, tuv + float2(e, 0), 0) - gTerrainHeightG.SampleLevel(sLinearClamp, tuv - float2(e, 0), 0);
    float hy = gTerrainHeightG.SampleLevel(sLinearClamp, tuv + float2(0, e), 0) - gTerrainHeightG.SampleLevel(sLinearClamp, tuv - float2(0, e), 0);
    float slope = length(float2(hx, hy)) / 4.0;
    if (slope > 0.75 && !meshLawn) return;
    float wl = gWaterLevelG.SampleLevel(sPointClamp, tuv, 0);
    float waterDepth = wl > -999.0 && !meshLawn ? wl - hC : -1.0;
    uint type = 1;
    if (sawg > max(grass, forest)) type = 2;
    else if (forest > grass) type = 3;
    else if (sand > grass) type = 4;
    else if (urban > 0.18) type = 0;
    if (waterDepth > (type == 2 ? 0.45 : 0.03)) return;
    // clump parameters by type
    float hv = hashF(h ^ 0x51ed270bu);
    float height, width;
    float3 col;
    if (type == 0) { height = lerp(0.07, 0.16, hv); width = 0.012; col = float3(0.08, 0.17, 0.035); }
    else if (type == 1) { height = lerp(0.22, 0.55, hv * hv); width = 0.016; col = float3(0.1, 0.17, 0.04); }
    else if (type == 2) { height = lerp(0.7, 1.45, hv); width = 0.03; col = float3(0.24, 0.23, 0.09); }
    else if (type == 3) { height = lerp(0.18, 0.4, hv); width = 0.02; col = float3(0.07, 0.12, 0.035); }
    else { height = lerp(0.25, 0.55, hv); width = 0.014; col = float3(0.22, 0.24, 0.12); }
    // color variation: dry / lush patches
    float dry = saturate(fbmValue(wr * 0.035 + 7.0, 2) * 1.6 - 0.45);
    col = lerp(col, col * float3(1.9, 1.5, 1.2), dry * (type == 2 ? 0.3 : 0.6));
    col *= lerp(0.85, 1.15, hashF(h ^ 0x3c6ef372u));
    // distance fade: shrink into the ground towards the outer radius (and the inner edge of the far ring)
    float fade = 1.0 - smoothstep(gGrass1.z, rOut, dist);
    if (gGrass3.y > 0.5) fade *= smoothstep(rIn, rIn + 5.0, dist);
    else fade *= 1.0 - smoothstep(rOut - 5.0, rOut, dist);
    height *= fade * (gGrass3.y > 0.5 ? 1.15 : 1.0);
    if (height < 0.02) return;
    float3 rel = float3(relXY, hC - gCamPos.z);
    // frustum culling (sphere around the clump)
    float3 cc = rel + float3(0, 0, height * 0.5);
    float rad = height * 0.7 + cell * 0.8;
    [unroll] for (int k = 0; k < 6; k++) if (dot(gFrustum[k].xyz, cc) + gFrustum[k].w < -rad) return;
    GrassInstance g;
    g.rel = rel;
    g.yaw = hashF(h ^ 0xa54ff53au) * TWO_PI;
    g.height = height;
    g.width = width * (gGrass3.y > 0.5 ? 2.2 : 1.0);
    uint3 c8 = (uint3)(saturate(col) * 255.0 + 0.5);
    g.colorType = c8.r | (c8.g << 8) | (c8.b << 16) | (type << 24);
    g.seed = hashF(h ^ 0x1b873593u);
    uGrass.Append(g);
}

// ------------------------------------------------------------------------------------------------
struct GrassVSOut {
    float4 pos : SV_Position;
    float3 nrm : TEXCOORD0;
    float4 curClip : TEXCOORD1;
    float4 prevClip : TEXCOORD2;
    nointerpolation float4 color : COLOR0;   // rgb base color, a type
    float2 bladeUV : TEXCOORD3;              // x across (-1..1), y height fraction
    nointerpolation float flower : TEXCOORD4;
    float3 rel : TEXCOORD5;
};

float3 windBend(float3 root, float t, float heightFrac, float h, float seed) {
    float2 wd = normalize(gWind.xy + float2(1e-4, 0));
    float2 wp = root.xy + gCamPos.xy;
    float gust = valueNoise((wp - wd * t * 3.5) * 0.08) * 1.2 + 0.2;
    float sway = sin(t * 2.3 + dot(wp, wd) * 0.35 + seed * 6.28) * 0.35 + sin(t * 5.1 + seed * 19.0) * 0.1;
    float amt = gGrass2.x * (gust + sway) * heightFrac * heightFrac * h;
    return float3(wd * amt, -amt * amt * 0.35 / max(h, 0.05));
}

GrassVSOut vsGrass(uint vid : SV_VertexID, uint inst : SV_InstanceID) {
    GrassInstance g = tGrass[inst];
    uint segs = (uint)gGrass3.x;
    uint vpb = (segs - 1u) * 6u + 3u;         // vertices per blade (triangle list)
    uint blade = vid / vpb, v = vid % vpb;
    uint seg = v / 6u;
    uint corner = v % 6u;
    // segment quad corners: (0,0) (1,0) (0,1) / (1,0) (1,1) (0,1); tip triangle: (0,0) (1,0) (0.5,1)
    float2 qc;
    if (seg < segs - 1u) {
        static const float2 kQuad[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 0), float2(1, 1), float2(0, 1)};
        qc = kQuad[corner];
    } else {
        static const float2 kTip[3] = {float2(0, 0), float2(1, 0), float2(0.5, 1)};
        qc = kTip[corner % 3u];
    }
    float yf = (seg + qc.y) / segs;
    uint bh = hashU(asuint(g.seed * 16777216.0) + blade * 0x9e3779b9u);
    float r0 = hashF(bh), r1 = hashF(bh ^ 0x68bc21ebu), r2 = hashF(bh ^ 0x02e5be93u), r3 = hashF(bh ^ 0x51ed270bu);
    uint type = g.colorType >> 24;
    float spread = type == 2 ? 0.35 : 0.22;
    float2 off = (float2(r0, r1) - 0.5) * spread * (gGrass3.y > 0.5 ? 2.0 : 1.0);
    float ang = g.yaw + r2 * TWO_PI;
    float2 across = float2(cos(ang), sin(ang));
    float2 lean = float2(-across.y, across.x) * (r3 - 0.3) * (type == 2 ? 0.25 : 0.45);
    float h = g.height * lerp(0.6, 1.1, r1);
    float w = g.width * lerp(0.7, 1.3, r0) * (1.0 - yf * 0.85);
    bool flower = gGrass3.y < 0.5 && (type == 1 || type == 0) && r3 > 0.93;
    if (flower) { h *= 1.25; w = yf > 0.85 ? g.width * 3.5 : g.width * 0.5; }
    float3 root = g.rel + float3(off, 0);
    float3 p = root + float3(lean * yf * yf * h, yf * h) + float3(across * (qc.x - 0.5) * w, 0);
    float3 bend = windBend(root, gGrass2.y, yf, h, g.seed + r2);
    float3 bendPrev = windBend(root, gGrass2.z, yf, h, g.seed + r2);
    GrassVSOut o;
    o.rel = p + bend;
    o.pos = mul(gViewProj, float4(o.rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(o.rel, 1));
    o.prevClip = mul(gPrevViewProj, float4(p + bendPrev, 1));
    float3 bladeN = normalize(float3(-across.y, across.x, 0) + float3(0, 0, 0.3) - float3(lean, 0) * 0.5);
    o.nrm = bladeN;
    uint c = g.colorType;
    o.color = float4(float3(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u) / 255.0, (float)type);
    o.bladeUV = float2(qc.x * 2.0 - 1.0, yf);
    o.flower = flower ? 1.0 + floor(r2 * 4.0) : 0.0;
    return o;
}

GBufferOut psGrass(GrassVSOut i, bool front : SV_IsFrontFace) {
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    // soft normals: mostly up so clumps shade like a canopy, blade direction for variation
    float3 n = normalize(lerp(float3(0, 0, 1), N, 0.45));
    float yf = i.bladeUV.y;
    uint type = (uint)i.color.a;
    float3 base = i.color.rgb;
    float3 tipC = type == 2 ? base * float3(1.5, 1.35, 1.0) : base * float3(1.35, 1.45, 1.1);
    float3 albedo = lerp(base * 0.55, tipC, yf);
    float ao = lerp(0.45, 1.0, saturate(yf * 1.6));
    if (i.flower > 0.5 && yf > 0.85) {
        static const float3 kFlower[4] = {float3(0.85, 0.82, 0.75), float3(0.85, 0.7, 0.08), float3(0.55, 0.25, 0.7), float3(0.8, 0.15, 0.12)};
        albedo = kFlower[(uint)i.flower - 1u];
        ao = 1.0;
    }
    float rough = 0.55;
    float3 worldP = i.rel + gCamPos.xyz;
    applyWetness(albedo, rough, n, float3(0, 0, 1), worldP, 0.35, 0.0);
    return packGBuffer(albedo, ao, n, rough, 0.0, SM_FOLIAGE, 0.55, 0.0, i.curClip, i.prevClip);
}
