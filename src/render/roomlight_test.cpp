// Room-light agreement self-test (--roomlighttest; docs/HANDBUILT_WORLD.md 7.8): the facade shader's HLSL roomLight()
// (shaders/roomlight.hlsli, run by shaders/roomlighttest.hlsl) against the C++ World::roomLight() (world/roomlight.h)
// over a sweep of 5,594,400 inputs and over the header's test vectors, with hand-built windows' facade flags
// (roomlight.h handWindowFlags) carrying the class. Included from renderer.cpp.
#include "../world/roomlight.h"

namespace Render {
namespace roomlight_test {

const u32 kSweepNight[6] = {0, 47, 48, 111, 112, 255};
const u32 kSweepCount = 14u * 20u * 15u * 111u * 6u * 2u;   // schedule x seed x day x minute x night x windows

struct RoomIn {
    u32 schedule, seed, windows, day, minute, night;
};
// index -> inputs, as roomlighttest.hlsl roomSweepInputs decodes it
inline RoomIn sweepInputs(u32 i) {
    RoomIn r;
    r.windows = i % 2u;
    i /= 2u;
    const u32 ni = i % 6u;
    i /= 6u;
    const u32 mi = i % 111u;
    i /= 111u;
    const u32 di = i % 15u;
    i /= 15u;
    const u32 si = i % 20u;
    i /= 20u;
    r.schedule = i;
    r.night = kSweepNight[ni];
    r.minute = mi < 110u ? mi * 13u : 1439u;
    r.day = di < 14u ? di : 4294967290u;
    r.seed = si < 19u ? si * 205u : 4095u;
    return r;
}

// The header's test vectors (tests/world/worldmodel_test.cpp testRoomLight): schedule, seed, windows, day, minute,
// night -> level
struct RoomVector {
    u32 schedule, seed, windows, day, minute, night, level;
};
const RoomVector kRoomVectors[] = {
        { 0, 2407, 1, 2, 1230, 255, 216},
        { 0, 4012, 1, 2,  420, 255, 240},
        { 0, 3024, 0, 2,   90, 255, 231},
        { 0, 1202, 1, 5,  600, 255, 207},
        { 0, 2071, 1, 5, 1410,  80, 200},
        { 0, 3553, 0, 3, 1230,   0, 244},
        { 0, 2835, 1, 0,   30, 255, 211},
        { 0, 1207, 1, 6,  125, 255, 198},
        { 1, 3721, 1, 2, 1230, 255,   0},
        { 1, 2247, 1, 2,  420, 255,   0},
        { 1, 3862, 0, 2,   90, 255,   0},
        { 1,  415, 1, 5,  600, 255,   0},
        { 1,  939, 1, 5, 1410,  80,   0},
        { 1,  255, 0, 3, 1230,   0,   0},
        { 1, 1977, 1, 0,   30, 255,   0},
        { 1, 4047, 1, 6,  125, 255,   0},
        { 2,  660, 1, 2, 1230, 255, 193},
        { 2, 2769, 1, 2,  420, 255, 174},
        { 2, 3383, 0, 2,   90, 255,   0},
        { 2,  253, 1, 5,  600, 255, 186},
        { 2, 3984, 1, 5, 1410,  80,   0},
        { 2, 2541, 0, 3, 1230,   0,   0},
        { 2, 3982, 1, 0,   30, 255,   0},
        { 2,  198, 1, 6,  125, 255,   0},
        // night owls: one on a weeknight at 1:00, one of Saturday night's extra ones (and on a weeknight), out by 3:00
        { 2, 1029, 1, 3,   60, 255, 150},
        { 2, 2030, 1, 6,   60, 255, 196},
        { 2, 2030, 1, 3,   60, 255,   0},
        { 2, 1029, 1, 3,  180, 255,   0},
        { 3, 3481, 1, 2, 1230, 255,   0},
        { 3,  122, 1, 2,  420, 255, 238},
        { 3, 1194, 0, 2,   90, 255,   0},
        { 3, 2957, 1, 5,  600, 255, 237},
        { 3, 3565, 1, 5, 1410,  80,   0},
        { 3,  966, 0, 3, 1230,   0,   0},
        { 3,  790, 1, 0,   30, 255,   0},
        { 3,  244, 1, 6,  125, 255,   0},
        { 4, 2956, 1, 2, 1230, 255,   0},
        { 4,  122, 1, 2,  420, 255,   0},
        { 4,  262, 0, 2,   90, 255,   0},
        { 4, 1209, 1, 5,  600, 255,   0},
        { 4,  501, 1, 5, 1410,  80,   0},
        { 4, 2561, 0, 3, 1230,   0,   0},
        { 4, 1087, 1, 0,   30, 255,   0},
        { 4,  533, 1, 6,  125, 255,   0},
        { 5, 1223, 1, 2, 1230, 255,   0},
        { 5, 3898, 1, 2,  420, 255,   0},
        { 5,  333, 0, 2,   90, 255,   0},
        { 5, 1419, 1, 5,  600, 255,   0},
        { 5,  502, 1, 5, 1410,  80,   0},
        { 5,  987, 0, 3, 1230,   0,   0},
        { 5, 3693, 1, 0,   30, 255,   0},
        { 5, 1951, 1, 6,  125, 255,   0},
        { 6, 1561, 1, 2, 1230, 255, 250},
        { 6, 3505, 1, 2,  420, 255,   0},
        { 6,  750, 0, 2,   90, 255,   0},
        { 6, 3314, 1, 5,  600, 255, 217},
        { 6, 2516, 1, 5, 1410,  80,   0},
        { 6, 2237, 0, 3, 1230,   0, 248},
        { 6, 1111, 1, 0,   30, 255,   0},
        { 6, 2446, 1, 6,  125, 255, 106},
        { 7, 3188, 1, 2, 1230, 255,   0},
        { 7, 2293, 1, 2,  420, 255,   0},
        { 7, 2476, 0, 2,   90, 255,   0},
        { 7, 3399, 1, 5,  600, 255,   0},
        { 7, 2788, 1, 5, 1410,  80,   0},
        { 7,  999, 0, 3, 1230,   0,   0},
        { 7, 2302, 1, 0,   30, 255,   0},
        { 7, 3564, 1, 6,  125, 255,   0},
        { 8, 1695, 1, 2, 1230, 255, 204},
        { 8, 1953, 1, 2,  420, 255,   0},
        { 8, 1263, 0, 2,   90, 255,   0},
        { 8, 2884, 1, 5,  600, 255, 252},
        { 8, 2456, 1, 5, 1410,  80, 238},
        { 8, 2214, 0, 3, 1230,   0, 206},
        { 8, 1774, 1, 0,   30, 255,   0},
        { 8, 3709, 1, 6,  125, 255,   0},
        { 9, 2932, 1, 2, 1230, 255, 200},
        { 9, 3929, 1, 2,  420, 255,   0},
        { 9, 2650, 0, 2,   90, 255, 224},
        { 9, 3780, 1, 5,  600, 255,   0},
        { 9, 1043, 1, 5, 1410,  80, 222},
        { 9, 1308, 0, 3, 1230,   0, 199},
        { 9,   39, 1, 0,   30, 255,   0},
        { 9, 1180, 1, 6,  125, 255, 205},
        {10,   48, 1, 2, 1230, 255,   0},
        {10, 2180, 1, 2,  420, 255, 231},
        {10, 2539, 0, 2,   90, 255,   0},
        {10, 2281, 1, 5,  600, 255,   0},
        {10, 2396, 1, 5, 1410,  80,   0},
        {10, 4036, 0, 3, 1230,   0,   0},
        {10,  662, 1, 0,   30, 255,   0},
        {10,  104, 1, 6,  125, 255,   0},
        {11, 1163, 1, 2, 1230, 255, 214},
        {11,  728, 1, 2,  420, 255, 201},
        {11, 1051, 0, 2,   90, 255, 237},
        {11,  662, 1, 5,  600, 255, 242},
        {11, 2277, 1, 5, 1410,  80,   0},
        {11,  833, 0, 3, 1230,   0, 244},
        {11, 1630, 1, 0,   30, 255, 204},
        {11, 2749, 1, 6,  125, 255, 198},
        {12, 1808, 1, 2, 1230, 255, 231},
        {12,  318, 1, 2,  420, 255,   0},
        {12, 2837, 0, 2,   90, 255,   0},
        {12,  293, 1, 5,  600, 255,   0},
        {12, 3906, 1, 5, 1410,  80,   0},
        {12, 1595, 0, 3, 1230,   0, 212},
        {12, 1213, 1, 0,   30, 255,   0},
        {12,  963, 1, 6,  125, 255,   0},
        // day types: Friday night's extra night owl, gone again on Sunday night; a bar at 3:30 after Friday and after
        // Thursday; an office on a Friday morning; a shop on a Sunday evening (weekend hours)
        { 2, 2030, 1, 5,   60, 255, 196},
        { 2, 2030, 1, 7,   60, 255,   0},
        { 9,  100, 0, 5,  210, 255, 252},
        { 9,  100, 0, 4,  210, 255,   0},
        { 7, 1502, 1, 4,  600,   0, 230},
        { 6, 2501, 1, 6, 1290, 255, 204},
};

// Hand-built windows (roomlight.h handWindowFlags): the class rides in the facade flags, and the vector's own schedule
// (never) must not be used. Two of the vectors above again: a living room on a Wednesday at 20:30, and a shop on a
// Sunday at 21:30, behind a storefront (flag 1)
struct HandWindowVector {
    u32 flags, seed, windows, day, minute, night, level;
};
const HandWindowVector kHandWindowVectors[] = {
        {World::handWindowFlags(World::RS_HOME_LIVING), 660, 1, 2, 1230, 255, 193},
        {World::handWindowFlags(World::RS_SHOP) | 1u, 2501, 1, 6, 1290, 255, 204},
};

}  // namespace roomlight_test

// Returns the number of failures (0: the HLSL and the C++ copies agree everywhere, and both match the vectors)
int roomLightSelfTest() {
    using namespace roomlight_test;
    const u32 words = (kSweepCount + 3u) / 4u;
    const u32 nr = (u32)(sizeof(kRoomVectors) / sizeof(kRoomVectors[0])), nh = (u32)(sizeof(kHandWindowVectors) / sizeof(kHandWindowVectors[0]));
    const u32 nv = nr + nh;
    gfx::ComputeShader csSweep = gfx::loadCS("roomlighttest.hlsl", "csRoomSweep");
    gfx::ComputeShader csVec = gfx::loadCS("roomlighttest.hlsl", "csRoomVectors");
    gfx::Buffer out = gfx::createBuffer(words * 4, 4, gfx::BUF_STRUCTURED | gfx::BUF_UAV, nullptr, "roomlight test sweep");
    std::vector<u32> vin((size_t)nv * 8);
    for (u32 i = 0; i < nv; i++) {
        u32* p = &vin[(size_t)i * 8];
        if (i < nr) {
            const RoomVector& v = kRoomVectors[i];
            p[0] = v.schedule; p[1] = v.seed; p[2] = v.windows; p[3] = v.day; p[4] = v.minute; p[5] = v.night; p[6] = 0; p[7] = 0;
        } else {   // (the facade flags in p[6]; the schedule never, which the flags must override)
            const HandWindowVector& v = kHandWindowVectors[i - nr];
            p[0] = World::RS_NEVER; p[1] = v.seed; p[2] = v.windows; p[3] = v.day; p[4] = v.minute; p[5] = v.night; p[6] = v.flags; p[7] = 0;
        }
    }
    gfx::Buffer vbuf = gfx::createBuffer(nv * 32, 16, gfx::BUF_STRUCTURED | gfx::BUF_SRV, vin.data(), "roomlight test vectors");
    gfx::Buffer vout = gfx::createBuffer(nv * 4, 4, gfx::BUF_STRUCTURED | gfx::BUF_UAV, nullptr, "roomlight test vector out");
    u32 rc[4] = {0, kSweepCount, 0, 0};
    gfx::ctx->setCS(csSweep);
    gfx::ctx->setRootConstants(true, 0, 4, rc);
    gfx::ctx->csSetUAVs(0, 1, &out.uav);
    gfx::ctx->dispatch((words + 63) / 64, 1, 1);
    rc[1] = nv;
    gfx::ctx->setCS(csVec);
    gfx::ctx->setRootConstants(true, 0, 4, rc);
    gfx::ctx->csSetSRVs(0, 1, &vbuf.srv);
    gfx::ctx->csSetUAVs(0, 1, &vout.uav);
    gfx::ctx->dispatch((nv + 63) / 64, 1, 1);
    std::vector<u32> got(words), gotV(nv);
    gfx::readbackBuffer(out.buf, got.data(), words * 4);
    gfx::readbackBuffer(vout.buf, gotV.data(), nv * 4);
    gfx::unbindCSResources(8, 8);
    int sweepBad = 0, lit = 0;
    for (u32 i = 0; i < kSweepCount; i++) {
        const RoomIn a = sweepInputs(i);
        const u32 cpu = World::roomLight(a.schedule, a.seed, a.windows, a.day, a.minute, a.night);
        const u32 gpu = (got[i / 4] >> ((i % 4) * 8)) & 255u;
        if (cpu != 0u) lit++;
        if (cpu != gpu) {
            if (sweepBad < 8)
                LOG("room-light self-test: sweep %u (schedule %u seed %u windows %u day %u minute %u night %u): GPU %u, C++ %u", i, a.schedule,
                    a.seed, a.windows, a.day, a.minute, a.night, gpu, cpu);
            sweepBad++;
        }
    }
    int vecGpu = 0, vecCpu = 0;
    for (u32 i = 0; i < nv; i++) {
        const bool hand = i >= nr;
        RoomVector v = hand ? RoomVector() : kRoomVectors[i];
        if (hand) {   // (the C++ takes the class from the flags as the shader does)
            const HandWindowVector& h = kHandWindowVectors[i - nr];
            v = RoomVector{World::handWindowSchedule(h.flags), h.seed, h.windows, h.day, h.minute, h.night, h.level};
        }
        const u32 cpu = World::roomLight(v.schedule, v.seed, v.windows, v.day, v.minute, v.night);
        if (gotV[i] != v.level) vecGpu++;
        if (cpu != v.level) vecCpu++;
        if (gotV[i] != v.level && vecGpu <= 8) LOG("room-light self-test: %s %u: GPU %u, expected %u", hand ? "hand-built window vector" : "vector", hand ? i - nr : i, gotV[i], v.level);
    }
    LOG("room-light self-test: %u sweep inputs (%d lit): %d differ between the GPU and the C++; %u test vectors (%u of hand-built "
        "windows): %d differ on the GPU, %d in the C++ -> %s",
        kSweepCount, lit, sweepBad, nv, nh, vecGpu, vecCpu, sweepBad + vecGpu + vecCpu == 0 ? "PASS" : "FAIL");
    out.release();
    vbuf.release();
    vout.release();
    return (sweepBad > 0 ? 1 : 0) + (vecGpu > 0 ? 1 : 0) + (vecCpu > 0 ? 1 : 0);
}

}  // namespace Render
