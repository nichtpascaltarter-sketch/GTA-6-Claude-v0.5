// Shaders of the graphics layer self-test (gfx::selfTest, --gfxselftest): bindless arrays, root constants, the
// slot tables, async compute, ExecuteIndirect, append counters and read-only depth.
#include "bindless.hlsli"

cbuffer TestCB : register(b1) {
    uint4 gTest;   // x: multiplier / value, y..w: free
};

// ---- bindless + root constants + regular tables (compute)
// gRootConstants[0]: x texture index, y raw buffer index, z output raw UAV index (tier 3) or ~0, w added constant
Texture2D<float4> tLocal : register(t3);
RWStructuredBuffer<uint> uLocal : register(u2);
[numthreads(64, 1, 1)]
void csBindless(uint3 id : SV_DispatchThreadID) {
    uint4 rc = gRootConstants[0];
    float4 t = gBindlessTex2D[NonUniformResourceIndex(rc.x)].Load(int3(id.x % 4, 0, 0));
    uint b = gBindlessBuffers[NonUniformResourceIndex(rc.y)].Load(id.x * 4);
    uint v = (uint)(t.r * 255.0 + 0.5) + b * 1000 + gTest.x + rc.w + (uint)(tLocal.Load(int3(0, 0, 0)).g * 255.0 + 0.5);
    uLocal[id.x] = v;
    if (rc.z != 0xffffffffu) gBindlessRWBuffers[NonUniformResourceIndex(rc.z)].Store(id.x * 4, v);
}

// ---- graphics: fullscreen triangle, texture from the PS table, value from a root CBV, color from root constants
float4 vsFull(uint vid : SV_VertexID) : SV_Position {
    float2 uv = float2((vid << 1) & 2, vid & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);
}
Texture2D<float4> tPS : register(t0);
float4 psTest(float4 pos : SV_Position) : SV_Target {
    float r = tPS.Load(int3(0, 0, 0)).r;
    return float4(r, (float)gTest.x / 255.0, (float)gRootConstants[0].x / 255.0, 1);
}

// ---- async compute: the direct queue writes a sequence, the compute queue doubles it
RWStructuredBuffer<uint> uSeq : register(u0);
[numthreads(64, 1, 1)]
void csWriteSeq(uint3 id : SV_DispatchThreadID) { uSeq[id.x] = id.x + gRootConstants[0].x; }

StructuredBuffer<uint> tSeq : register(t0);
[numthreads(64, 1, 1)]
void csDouble(uint3 id : SV_DispatchThreadID) { uSeq[id.x] = tSeq[id.x] * 2; }

// ---- ExecuteIndirect: GPU-written dispatch arguments (root constant + group counts per command) and a count
RWByteAddressBuffer uArgs : register(u0);
RWByteAddressBuffer uCount : register(u1);
[numthreads(1, 1, 1)]
void csMakeArgs(uint3 id : SV_DispatchThreadID) {
    for (uint i = 0; i < 3; i++) {
        uint base = i * 16;
        uArgs.Store(base + 0, i);   // root constant 0
        uArgs.Store(base + 4, 1);   // thread groups x, y, z
        uArgs.Store(base + 8, 1);
        uArgs.Store(base + 12, 1);
    }
    uCount.Store(0, 2);   // only the first two commands run
}
[numthreads(1, 1, 1)]
void csIndirectTarget(uint3 id : SV_DispatchThreadID) { uSeq[gRootConstants[0].x] = gRootConstants[0].x + 100; }

// ---- append counter
AppendStructuredBuffer<uint> uAppend : register(u0);
[numthreads(64, 1, 1)]
void csAppend(uint3 id : SV_DispatchThreadID) {
    if (id.x < gRootConstants[0].x) uAppend.Append(id.x);
}

// ---- read-only depth bound while the same depth is sampled
Texture2D<float> tDepth : register(t0);
float4 psDepthRead(float4 pos : SV_Position) : SV_Target { return float4(tDepth.Load(int3(pos.xy, 0)), 0, 0, 1); }

// ---- indirect draw: solid color from root constants
float4 psSolid(float4 pos : SV_Position) : SV_Target { return float4(gRootConstants[1]) / 255.0; }

// ---- root arguments that must survive a UAV clear between two dispatches: global table (t40), root constants
StructuredBuffer<uint> tGlobal : register(t40);
[numthreads(64, 1, 1)]
void csReadGlobal(uint3 id : SV_DispatchThreadID) { uSeq[id.x] = tGlobal[id.x] + gRootConstants[0].x; }
