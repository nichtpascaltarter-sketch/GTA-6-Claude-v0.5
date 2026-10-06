// GPU-driven props (render/props_render.cpp). Every prop of the near cells sits in a persistent instance buffer; each
// frame csDecorPlace scatters the decor plants around the camera (render/vegdecor.cpp) into a second instance buffer,
// csPropCull tests both for the camera and the shadow cascades (distance cut per prototype, frustum) and appends the
// visible ones to per-prototype lists, one block of lists per view, in the layout the prop vertex shaders read as
// their instance stream (props.hlsl INSTPOS / INSTROT), and csPropArgs turns the list lengths into compacted
// DrawIndexedInstanced arguments and a draw count per view for ExecuteIndirect and clears the lengths for the next
// frame.
#include "common.hlsli"

struct PropSource {
    float4 posScale;   // world position (m), scale
    float4 rot;        // cos(yaw), sin(yaw), wind phase, asuint: prototype index
};

struct PropProto {
    float radius;       // bounding sphere radius at scale 1 (centred half a radius up from the anchor)
    float lodDistance;  // drawn out to this distance at scale 1
    uint shadow;        // casts shadows
    uint indexCount;
    uint indexStart;
    int baseVertex;
    uint segBase;       // first slot of this prototype's list within each view's block
    uint segSize;       // slots of the list
};

struct PropOut {
    float4 pos;   // camera-relative position, scale
    float4 rot;   // cos(yaw), sin(yaw), wind phase, -1 (no traffic signal phase)
};

cbuffer PropCullCB : register(b1) {
    float4 gCamHi;          // camera world position: float high part
    float4 gCamLo;          // and the remainder (camHi + camLo = the camera to double precision)
    float4 gPlanes[24];     // per view: 6 inward frustum planes, camera-relative (dot(n, p) + w >= 0 inside)
    float4 gViewCull[4];    // per view: x distance scale, y culled this frame (1 / 0), z shadow view (1 / 0)
    uint4 gCounts;          // x prototypes, y slots per view, z world instances, w decor capacity
    float4 gDecor0;         // decor grid: xy origin (world, a cell corner), z cell size (m), w cells per side
    float4 gDecor1;         // x radius (m), y first decor prototype, z world half size (m), w on (1 / 0)
};

StructuredBuffer<PropSource> tSources : register(t0);
StructuredBuffer<PropProto> tProtos : register(t1);
StructuredBuffer<PropSource> tDecor : register(t5);
ByteAddressBuffer tDecorCount : register(t6);
RWStructuredBuffer<PropOut> uOut : register(u0);
RWByteAddressBuffer uCounts : register(u1);     // per view and prototype: list length
RWByteAddressBuffer uArgs : register(u2);       // per view: up to gCounts.x D3D12_DRAW_INDEXED_ARGUMENTS (20 bytes)
RWByteAddressBuffer uDrawCount : register(u3);  // per view: draws in uArgs; then this frame's decor plant count (stats)
RWStructuredBuffer<PropSource> uDecor : register(u4);
RWByteAddressBuffer uDecorCount : register(u5);

static const uint kPropViews = 4;   // the camera, shadow cascades 0-2

// ---- decor placement (render/vegdecor.cpp: types and variant counts in the same order)
Texture2D<float4> tSplat0 : register(t2);         // sand, grass, dirt, rock
Texture2D<float4> tSplat1 : register(t3);         // mud, sawgrass, forest, urban
Texture2D<float> tOverheadClass : register(t4);   // overhead pass: 1 lawn, 0.25 planting-bed soil, 0 anything else

static const uint kDecorCroton = 0, kDecorFlowering = 1, kDecorFern = 2, kDecorPalmetto = 3, kDecorFlowers = 4;
static const uint kDecorVariants[5] = {3, 2, 2, 2, 3};
static const uint kDecorFirst[5] = {0, 3, 5, 7, 9};   // prototype offset of each type

// A world-anchored grid around the camera, one candidate per cell (deterministic per cell). Where: lawns (lawn meshes
// and town lawns on the terrain) and raised beds of soil along a wall, fence, hedge or building (foundation plantings:
// crotons, hibiscus, ixora; flowers in the beds), a few flower clumps in lawns and meadows, ferns and saw palmettos on
// the forest floor. Never on roads, sidewalks, lots, roofs or decks (the overhead pass gives the top static surface
// and whether it is lawn or bed), on paved or sandy ground, in water or on steep ground, and every plant keeps its
// reach clear of walls, steps, shrubs, paths and curbs.
[numthreads(8, 8, 1)]
void csDecorPlace(uint3 id : SV_DispatchThreadID) {
    float n = gDecor0.w;
    if (gDecor1.w < 0.5 || id.x >= (uint)n || id.y >= (uint)n) return;
    float cell = gDecor0.z;
    float2 cellMin = gDecor0.xy + float2(id.xy) * cell;
    int2 ci = int2(floor(cellMin / cell + 0.5));
    uint h = hash2u(asuint(ci) + uint2(0x2545f491u, 0x4f6cdd1du));
    float2 wp = cellMin + float2(hashF(h), hashF(h ^ 0x68bc21ebu)) * cell;
    float2 relXY = wp - gCamPos.xy;
    float dist = length(relXY);
    float R = gDecor1.x;
    if (dist > R || gOverhead.w < 0.5) return;
    float2 ouv = overheadUV(wp);
    if (any(ouv <= 0.0) || any(ouv >= 1.0)) return;
    float2 tuv = (wp + gDecor1.z) / (2.0 * gDecor1.z);
    float groundZ = gTerrainHeightG.SampleLevel(sLinearClamp, tuv, 0);
    float top = gOverheadMap.SampleLevel(sPointClamp, ouv, 0);
    float cls = tOverheadClass.SampleLevel(sPointClamp, ouv, 0);
    float r = hashF(h ^ 0x02e5be93u), pick = hashF(h ^ 0x51ed270bu);
    float size = lerp(0.75, 1.2, hashF(h ^ 0x3c6ef372u));
    uint type = 99u;
    float z = groundZ;
    uint site = 0u;   // 1 lawn, 2 raised bed of soil
    if (top > groundZ - 0.35) {
        // a world mesh: a lawn or a raised planting bed; anything else (road, sidewalk, lot, roof, deck) grows nothing
        z = top;
        if (cls > 0.75) site = 1u;
        else if (cls > 0.15 && cls < 0.4 && top - groundZ > 0.04 && top - groundZ < 0.45) site = 2u;
        else return;
    } else {
        // bare terrain: the splat says lawn (grass in town), woods, meadow or nothing (paved, sand, rock)
        float4 s0 = tSplat0.SampleLevel(sLinearClamp, tuv, 0);
        float4 s1 = tSplat1.SampleLevel(sLinearClamp, tuv, 0);
        float e = 2.0 / (2.0 * gDecor1.z);
        float hx = gTerrainHeightG.SampleLevel(sLinearClamp, tuv + float2(e, 0), 0) - gTerrainHeightG.SampleLevel(sLinearClamp, tuv - float2(e, 0), 0);
        float hy = gTerrainHeightG.SampleLevel(sLinearClamp, tuv + float2(0, e), 0) - gTerrainHeightG.SampleLevel(sLinearClamp, tuv - float2(0, e), 0);
        if (length(float2(hx, hy)) / 4.0 > 0.6) return;
        float grass = s0.y, forest = s1.z, urban = s1.w;
        if (forest > max(grass, 0.25)) {
            if (r < 0.2) type = pick < 0.72 ? kDecorFern : kDecorPalmetto;
        } else if (grass > 0.4 && urban > 0.15 && urban < 0.75) {
            site = 1u;   // a town lawn on the terrain
        } else if (grass > 0.45 && urban <= 0.15) {
            if (r < 0.03) type = kDecorFlowers;
            else if (r < 0.04 && forest > 0.12) type = kDecorPalmetto;
        }
    }
    if (site != 0u) {
        // something standing more than 1.2 m tall within 1.6 m (a wall or the eaves above it, a tall hedge, a garden
        // wall; not a stoop or a low hedge): a foundation planting. Soil away from one is a dirt road, a track or a
        // yard, and grows nothing.
        if (r >= (site == 1u ? 0.42 : 0.5)) return;
        float rise = 0.0;
        [unroll] for (uint k = 0; k < 8; k++) {
            float2 dir = float2(cos(k * (TWO_PI / 8.0)), sin(k * (TWO_PI / 8.0)));
            rise = max(rise, gOverheadMap.SampleLevel(sPointClamp, overheadUV(wp + dir * 0.9), 0) - z);
            rise = max(rise, gOverheadMap.SampleLevel(sPointClamp, overheadUV(wp + dir * 1.6), 0) - z);
        }
        bool foundation = rise > 1.2 && rise < 400.0;
        if (site == 2u) {
            if (foundation) type = pick < 0.45 ? kDecorFlowering : (pick < 0.75 ? kDecorFlowers : kDecorCroton);
        } else if (foundation) {
            type = pick < 0.55 ? kDecorCroton : kDecorFlowering;
        } else if (r < 0.025) {
            type = kDecorFlowers;
        }
    }
    if (type > 4u) return;
    // Room to grow: nothing standing (a wall, the eaves, a porch roof, a shrub) and no paving within the plant's
    // reach (ring 0); and the way to a door or along a path stays open: no low paved surface (the step of a stoop
    // under its porch roof, a path, a drive, a sidewalk, a curb) within 1.2 m of a shrub, 0.6 m of a flower clump or a
    // fern (rings 1-3 at a half, three quarters and all of that, staggered, so that a narrow step is not missed).
    bool lowPlant = type == kDecorFlowers || type == kDecorFern;
    float reach = (lowPlant ? 0.3 : (type == kDecorPalmetto ? 0.7 : 0.55)) * size;
    float keep = lowPlant ? 0.6 : 1.2;
    [unroll] for (uint k = 0; k < 32; k++) {
        uint ring = k >> 3;
        float a = (k & 7u) * (TWO_PI / 8.0) + ring * 0.39;
        float2 wk = wp + float2(cos(a), sin(a)) * (ring == 0u ? reach : keep * (0.25 + 0.25 * ring));
        float2 uk = overheadUV(wk);
        float tk = gOverheadMap.SampleLevel(sPointClamp, uk, 0);
        float gk = gTerrainHeightG.SampleLevel(sLinearClamp, (wk + gDecor1.z) / (2.0 * gDecor1.z), 0);
        if (tk <= gk - 0.35) continue;   // bare terrain
        bool paved = tOverheadClass.SampleLevel(sPointClamp, uk, 0) < 0.15;
        if (ring == 0u ? (tk > max(z, gk) + 0.15 || paved) : (paved && tk < max(z, gk) + 1.2)) return;
    }
    float wl = gWaterLevelG.SampleLevel(sPointClamp, tuv, 0);
    if (wl > -999.0 && wl > z - 0.05) return;
    // size: per plant, and shrinking into the ground at the edge of the ring
    float scale = size * (1.0 - smoothstep(R * 0.82, R, dist));
    if (scale < 0.08) return;
    uint variant = (h >> 9) % kDecorVariants[type];
    uint proto = (uint)gDecor1.y + kDecorFirst[type] + variant;
    float yaw = hashF(h ^ 0xa54ff53au) * TWO_PI;
    uint slot;
    uDecorCount.InterlockedAdd(0, 1u, slot);
    if (slot >= gCounts.w) return;
    PropSource s;
    s.posScale = float4(wp, z, scale);
    s.rot = float4(cos(yaw), sin(yaw), hashF(h ^ 0x1b873593u) * TWO_PI, asfloat(proto));
    uDecor[slot] = s;
}

// The world's props (persistent) and then this frame's decor
[numthreads(64, 1, 1)]
void csPropCull(uint3 id : SV_DispatchThreadID) {
    PropSource s;
    if (id.x < gCounts.z) {
        s = tSources[id.x];
    } else {
        uint j = id.x - gCounts.z;
        if (j >= min(tDecorCount.Load(0), gCounts.w)) return;
        s = tDecor[j];
    }
    uint proto = asuint(s.rot.w);
    PropProto P = tProtos[proto];
    float3 rp = (s.posScale.xyz - gCamHi.xyz) - gCamLo.xyz;
    float scale = s.posScale.w;
    float d = length(rp);
    float3 c = rp + float3(0, 0, P.radius * 0.5 * scale);
    float r = P.radius * scale;
    PropOut o;
    o.pos = float4(rp, scale);
    o.rot = float4(s.rot.xyz, -1.0);
    [unroll] for (uint v = 0; v < kPropViews; v++) {
        float4 vp = gViewCull[v];
        if (vp.y < 0.5 || (vp.z > 0.5 && P.shadow == 0u)) continue;
        if (d > P.lodDistance * vp.x * max(scale, 0.5)) continue;
        bool inside = true;
        [unroll] for (uint k = 0; k < 6; k++) {
            float4 pl = gPlanes[v * 6 + k];
            if (dot(pl.xyz, c) + pl.w < -r) inside = false;
        }
        if (!inside) continue;
        uint slot;
        uCounts.InterlockedAdd((v * gCounts.x + proto) * 4, 1u, slot);
        if (slot < P.segSize) uOut[v * gCounts.y + P.segBase + slot] = o;
    }
}

// One thread per view: the prototypes in order, empty lists skipped. The first thread also keeps the frame's decor
// count for the stats (it can exceed the buffer: then the plants past the end were dropped) and restarts it.
[numthreads(kPropViews, 1, 1)]
void csPropArgs(uint3 id : SV_DispatchThreadID) {
    uint v = id.x;
    uint n = 0;
    for (uint p = 0; p < gCounts.x; p++) {
        uint at = (v * gCounts.x + p) * 4;
        PropProto P = tProtos[p];
        uint count = min(uCounts.Load(at), P.segSize);
        uCounts.Store(at, 0u);
        if (count == 0u) continue;
        uint o = (v * gCounts.x + n) * 20;
        uArgs.Store4(o, uint4(P.indexCount, count, P.indexStart, asuint(P.baseVertex)));
        uArgs.Store(o + 16, v * gCounts.y + P.segBase);
        n++;
    }
    uDrawCount.Store(v * 4, n);
    if (v == 0u) {
        uDrawCount.Store(kPropViews * 4, uDecorCount.Load(0));
        uDecorCount.Store(0, 0u);
    }
}
