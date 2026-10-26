// Room light state (docs/HANDBUILT_WORLD.md 7.8): the HLSL copy of World::roomLight() (src/world/roomlight.h), for
// the facade's windows. It must give the CPU's answers bit for bit (the renderer's room-light self-test compares the
// two over a sweep and the header's test vectors), so it is uint arithmetic only (uint multiplies wrap mod 2^32 as
// the C++ u32 ones do), line by line from the header. The inputs are the frame's three integers - the game day, the
// minute of the day and how dark it is outside (0..255), all computed on the CPU - and the window record's schedule
// class (4 bits) and seed (12 bits). A change to the header's table or function is a change here too.
#ifndef ROOMLIGHT_HLSLI
#define ROOMLIGHT_HLSLI

// The header's kTable, packed: index (schedule * 2 + weekend) * 3 + k (weekend: day % 7 >= 5);
//   x = start | end << 16;  y = chance | level << 8 | jitter << 16 | daily << 24
static const uint2 kRoomIntervals[78] = {
    uint2(0x05a00000u, 0x0000ffffu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_ALWAYS, weekdays
    uint2(0x05a00000u, 0x0000ffffu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_ALWAYS, weekends
    uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_NEVER, weekdays
    uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_NEVER, weekends
    uint2(0x01d10186u, 0x011edc46u), uint2(0x05730438u, 0x012dffcdu), uint2(0x00000000u, 0x00000000u),   // RS_HOME_LIVING, weekdays
    uint2(0x02d0021cu, 0x012ddc46u), uint2(0x05a0041au, 0x012dffd7u), uint2(0x00000000u, 0x00000000u),   // RS_HOME_LIVING, weekends
    uint2(0x01d10168u, 0x011effaau), uint2(0x04ec0438u, 0x012dffc8u), uint2(0x00000000u, 0x00000000u),   // RS_HOME_KITCHEN, weekdays
    uint2(0x027601e0u, 0x012dff96u), uint2(0x050a0438u, 0x012dffc8u), uint2(0x00000000u, 0x00000000u),   // RS_HOME_KITCHEN, weekends
    uint2(0x01950159u, 0x0114965au), uint2(0x0591050au, 0x01289696u), uint2(0x00000000u, 0x00000000u),   // RS_HOME_BED, weekdays
    uint2(0x021c01c2u, 0x01289646u), uint2(0x05dc0546u, 0x012d9696u), uint2(0x00000000u, 0x00000000u),   // RS_HOME_BED, weekends
    uint2(0x01a40168u, 0x0119ff8cu), uint2(0x0546050au, 0x011eff5au), uint2(0x00000000u, 0x00000000u),   // RS_HOME_BATH, weekdays
    uint2(0x023a01e0u, 0x0128ff78u), uint2(0x05820546u, 0x011eff5au), uint2(0x00000000u, 0x00000000u),   // RS_HOME_BATH, weekends
    uint2(0x04ec01e0u, 0x001efff5u), uint2(0x070804ecu, 0x00006e64u), uint2(0x00000000u, 0x00000000u),   // RS_SHOP, weekdays
    uint2(0x0528021cu, 0x001efff0u), uint2(0x07080528u, 0x00006e64u), uint2(0x00000000u, 0x00000000u),   // RS_SHOP, weekends
    uint2(0x047401c2u, 0x001effebu), uint2(0x05280474u, 0x012dff28u), uint2(0x054604b0u, 0x0114c83cu),   // RS_OFFICE, weekdays
    uint2(0x03840258u, 0x012dff19u), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_OFFICE, weekends
    uint2(0x05640294u, 0x001efff0u), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_RESTAURANT, weekdays
    uint2(0x05820258u, 0x001efff0u), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_RESTAURANT, weekends
    uint2(0x06540438u, 0x002dffebu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_NIGHTLIFE, weekdays
    uint2(0x069003fcu, 0x002dfff5u), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_NIGHTLIFE, weekends
    uint2(0x01c20186u, 0x0114ff3cu), uint2(0x0474041au, 0x011eff50u), uint2(0x00000000u, 0x00000000u),   // RS_GARAGE, weekdays
    uint2(0x02b20258u, 0x012dff28u), uint2(0x043803c0u, 0x012dff28u), uint2(0x00000000u, 0x00000000u),   // RS_GARAGE, weekends
    uint2(0x05a00000u, 0x0000ffffu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_DUSK, weekdays
    uint2(0x05a00000u, 0x0000ffffu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_DUSK, weekends
    uint2(0x056402d0u, 0x001effffu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_PORCH, weekdays
    uint2(0x05a002d0u, 0x001effffu), uint2(0x00000000u, 0x00000000u), uint2(0x00000000u, 0x00000000u),   // RS_PORCH, weekends
};
static const uint kRoomScheduleCount = 13u;   // RS_COUNT
static const uint kRoomScheduleNever = 1u;    // RS_NEVER

// src/core/rng.h hash32's mixing (roomlight.h roomHash)
uint roomHash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// homes (2..5), garages (10), dusk (11) and porch (12) lights switch off in daylight
uint roomScheduleDaylit(uint s) { return ((s >= 2u && s <= 5u) || s == 10u || s == 11u || s == 12u) ? 1u : 0u; }

// The light level of a room, 0 (off) to 254 (roomlight.h roomLight)
uint roomLight(uint schedule, uint seed, uint windows, uint day, uint minute, uint night) {
    const uint rs = roomHash((seed & 4095u) * 0x9e3779b9u + 0x632be5abu);   // the 12-bit seed spread over 32 bits
    if (windows != 0u && roomScheduleDaylit(schedule) != 0u && night < 48u + ((rs >> 26) & 63u)) return 0u;
    const uint dimmer = 191u + ((rs >> 20) & 63u);
    const uint sc = schedule < kRoomScheduleCount ? schedule : kRoomScheduleNever;   // past the table: never
    uint best = 0u;
    [loop] for (uint back = 0u; back < 2u; back++) {   // today's intervals, then yesterday's that run past midnight
        if (back == 1u && day == 0u) break;
        const uint d = day - back;
        const uint base = (sc * 2u + (d % 7u >= 5u ? 1u : 0u)) * 3u;
        const uint m = minute + back * 1440u;          // the minute on that day's clock
        [loop] for (uint k = 0u; k < 3u; k++) {
            const uint2 iv = kRoomIntervals[base + k];
            const uint start = iv.x & 0xffffu, end = iv.x >> 16;
            const uint chance = iv.y & 255u, level = (iv.y >> 8) & 255u, jitter = (iv.y >> 16) & 255u, daily = iv.y >> 24;
            if (chance == 0u) continue;
            const uint h = roomHash(rs ^ roomHash((daily != 0u ? d * 0x632be5abu : 0x51ed270bu) + k * 0x2545f491u + 7u));
            if ((h & 255u) > chance) continue;          // this room doesn't have it (that day)
            const uint span = 2u * jitter + 1u;         // both ends move by 0..2*jitter, less jitter
            const uint s0 = start + (h >> 8) % span, s1 = end + (h >> 16) % span;
            if (m + jitter < s0 || m + jitter >= s1) continue;
            best = max(best, level * dimmer / 255u);
        }
    }
    return best;
}

#endif
