// Room-light agreement self-test (render/roomlight_test.cpp, --roomlighttest; docs/HANDBUILT_WORLD.md 7.8): the HLSL
// roomLight() (roomlight.hlsli) over a sweep generated from the thread index - every schedule class and one past the
// table, seeds, days and one near 2^32, minutes, darkness levels and the windows flag - and over the header's test
// vectors. The CPU decodes the same index and compares with World::roomLight().
#include "bindless.hlsli"
#include "roomlight.hlsli"

static const uint kSweepNight[6] = {0u, 47u, 48u, 111u, 112u, 255u};

// index -> inputs: windows (2) fastest, then night (6), minute (111), day (15), seed (20), schedule (14)
void roomSweepInputs(uint i, out uint schedule, out uint seed, out uint windows, out uint day, out uint minute, out uint night) {
    windows = i % 2u;
    i /= 2u;
    const uint ni = i % 6u;
    i /= 6u;
    const uint mi = i % 111u;
    i /= 111u;
    const uint di = i % 15u;
    i /= 15u;
    const uint si = i % 20u;
    i /= 20u;
    schedule = i;
    night = kSweepNight[ni];
    minute = mi < 110u ? mi * 13u : 1439u;
    day = di < 14u ? di : 4294967290u;
    seed = si < 19u ? si * 205u : 4095u;   // (stride 205: the seeds include night owls, weekday and weekend)
}

RWStructuredBuffer<uint> uOut : register(u0);

// gRootConstants[0].y: the sweep's size; one output word packs four levels (byte b: index 4 * word + b)
[numthreads(64, 1, 1)]
void csRoomSweep(uint3 id : SV_DispatchThreadID) {
    const uint count = gRootConstants[0].y;
    uint packed = 0u;
    [loop] for (uint b = 0u; b < 4u; b++) {
        const uint i = id.x * 4u + b;
        if (i >= count) break;
        uint s, sd, w, d, m, n;
        roomSweepInputs(i, s, sd, w, d, m, n);
        packed |= roomLight(s, sd, w, d, m, n) << (b * 8u);
    }
    uOut[id.x] = packed;
}

// the test vectors: tVec[2 i] = (schedule, seed, windows, day), tVec[2 i + 1] = (minute, night, -, -);
// gRootConstants[0].y: the vector count
StructuredBuffer<uint4> tVec : register(t0);
[numthreads(64, 1, 1)]
void csRoomVectors(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gRootConstants[0].y) return;
    const uint4 a = tVec[id.x * 2u], b = tVec[id.x * 2u + 1u];
    uOut[id.x] = roomLight(a.x, a.y, a.z, a.w, b.x, b.y);
}
