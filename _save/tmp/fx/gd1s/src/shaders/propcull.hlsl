// GPU-driven props (render/props_render.cpp). Every prop of the near cells sits in a persistent instance buffer; each
// frame csPropCull tests them for the camera and the shadow cascades (distance cut per prototype, frustum) and appends
// the visible ones to per-prototype lists, one block of lists per view, in the layout the prop vertex shaders read as
// their instance stream (props.hlsl INSTPOS / INSTROT). csPropArgs then turns the list lengths into compacted
// DrawIndexedInstanced arguments and a draw count per view for ExecuteIndirect, and clears the lengths for the next
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
    uint pad;
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
    uint4 gCounts;          // x prototypes, y slots per view, z instances
};

StructuredBuffer<PropSource> tSources : register(t0);
StructuredBuffer<PropProto> tProtos : register(t1);
RWStructuredBuffer<PropOut> uOut : register(u0);
RWByteAddressBuffer uCounts : register(u1);     // per view and prototype: list length
RWByteAddressBuffer uArgs : register(u2);       // per view: up to gCounts.x D3D12_DRAW_INDEXED_ARGUMENTS (20 bytes)
RWByteAddressBuffer uDrawCount : register(u3);  // per view: draws in uArgs

static const uint kPropViews = 4;   // the camera, shadow cascades 0-2

[numthreads(64, 1, 1)]
void csPropCull(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gCounts.z) return;
    PropSource s = tSources[id.x];
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
        uOut[v * gCounts.y + P.segBase + slot] = o;
    }
}

// One thread per view: the prototypes in order, empty lists skipped
[numthreads(kPropViews, 1, 1)]
void csPropArgs(uint3 id : SV_DispatchThreadID) {
    uint v = id.x;
    uint n = 0;
    for (uint p = 0; p < gCounts.x; p++) {
        uint at = (v * gCounts.x + p) * 4;
        uint count = uCounts.Load(at);
        uCounts.Store(at, 0u);
        if (count == 0u) continue;
        PropProto P = tProtos[p];
        uint o = (v * gCounts.x + n) * 20;
        uArgs.Store4(o, uint4(P.indexCount, count, P.indexStart, asuint(P.baseVertex)));
        uArgs.Store(o + 16, v * gCounts.y + P.segBase);
        n++;
    }
    uDrawCount.Store(v * 4, n);
}
