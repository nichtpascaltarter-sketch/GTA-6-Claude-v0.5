// Shaders of the graphics layer self-test (gfx::selfTest, --gfxselftest): bindless arrays, root constants, the
// slot tables, async compute, ExecuteIndirect, append counters, read-only depth, long GPU work for the CPU waits,
// per-draw root constants selecting instance data, bindless reads from a pixel shader and descriptor tables shared
// by shaders.
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

// ---- long GPU work for the CPU-wait checks: gRootConstants[0].x steps of an integer LCG per thread, seeded with the
//      thread index ^ gRootConstants[0].y (gfx::selfTest computes the same values with the LCG's closed form)
[numthreads(64, 1, 1)]
void csSpin(uint3 id : SV_DispatchThreadID) {
    uint h = id.x ^ gRootConstants[0].y;
    for (uint i = 0; i < gRootConstants[0].x; i++) h = h * 1664525u + 1013904223u;
    uSeq[id.x] = h;
}

// ---- per-draw root constants selecting instance data (the dynamic objects' path): root constant 0 = instance,
//      root constant 1 = alpha, set once before the draws (later draws change only constant 0)
struct TestInstance {
    float4 rect;    // NDC x0, y0, x1, y1
    uint4 color;    // rgb 0..255
};
StructuredBuffer<TestInstance> tInstances : register(t22);
float4 vsInstance(uint vid : SV_VertexID) : SV_Position {
    float4 r = tInstances[gRootConstants[0].x].rect;
    return float4((vid & 1) ? r.z : r.x, (vid & 2) ? r.w : r.y, 0.5, 1);
}
float4 psInstance(float4 pos : SV_Position) : SV_Target {
    uint4 c = tInstances[gRootConstants[0].x].color;
    return float4(float3(c.rgb), (float)gRootConstants[0].y) / 255.0;
}

// ---- bindless reads in a pixel shader (the material set's path): a texture array layer written by a compute pass
//      and a structured buffer. gRootConstants[0]: x array index, y structured buffer index
StructuredBuffer<uint4> gBindlessTestUint4[] : register(t0, space15);
RWTexture2DArray<float4> uArrayOut : register(u0);
[numthreads(2, 2, 2)]
void csFillArray(uint3 id : SV_DispatchThreadID) {
    uArrayOut[id] = float4(id.z * 100 + id.x * 10 + id.y, 7 + id.z, 0, 255) / 255.0;
}
float4 psBindlessArray(float4 pos : SV_Position) : SV_Target {
    uint4 rc = gRootConstants[0];
    float4 t = gBindlessTex2DArray[rc.x].Load(int4(1, 0, 1, 0));
    uint4 s = gBindlessTestUint4[rc.y][1];
    return float4(t.r, t.g, s.x / 255.0, s.w / 255.0);
}

// ---- one descriptor table for two shaders: psTwo reads t0 and t1, psSecond only t1 (psTwo's table serves it)
Texture2D<float4> tFirst : register(t0);
Texture2D<float4> tSecond : register(t1);
float4 psTwo(float4 pos : SV_Position) : SV_Target {
    return float4(tFirst.Load(int3(0, 0, 0)).r, tSecond.Load(int3(0, 0, 0)).r, 0, 1);
}
float4 psSecond(float4 pos : SV_Position) : SV_Target { return float4(0, tSecond.Load(int3(0, 0, 0)).r, 1, 1); }
