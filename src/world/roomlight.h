// The light state of a room (docs/HANDBUILT_WORLD.md 7.8): whether its lamps are on, and how brightly, at a game day
// and minute. One function serves everything that shows a room lit: the real fixtures of resident floors, the shops'
// spill lights, and the facade shader's windows. The facade uses an HLSL copy, which must give the same answers (the
// renderer's selftest compares the two), so this is integer arithmetic only, with one small table, written to port
// line by line (HLSL's uint arithmetic wraps the same way). tests/world/worldmodel_test.cpp pins it with test vectors:
// a change here is a change to the HLSL copy too.
#pragma once
#include "../core/base.h"

namespace World {

// The schedule classes. worldc gives each room one from its `sched=` word, its room type and its building's occupancy
// (a home's kitchen with sched=evening gets RS_HOME_KITCHEN), and each exterior light one from its `sched=`. The
// window records and the HLSL copy store the index: append new classes at the end (16 at most: 4 bits).
enum RoomSchedule : u8 {
    RS_ALWAYS = 0,      // stairs, corridors and lobbies of shared buildings
    RS_NEVER,           // stores, plant and utility rooms
    RS_HOME_LIVING,     // living and dining rooms, the halls of homes: evenings, a little in the morning
    RS_HOME_KITCHEN,    // breakfast and dinner
    RS_HOME_BED,        // late evening (dimmer: bedside lamps) and getting up
    RS_HOME_BATH,       // short spells morning and night
    RS_SHOP,            // opening hours; some keep their display lighting on through the night
    RS_OFFICE,          // the working day, a few late workers, the cleaners
    RS_RESTAURANT,      // lunch to late
    RS_NIGHTLIFE,       // bars and clubs: the evening to the small hours
    RS_GARAGE,          // home garages: leaving and coming home
    RS_DUSK,            // exterior lights on a photocell: on whenever it is dark
    RS_PORCH,           // exterior lights on a timer: from dusk to about 23:00
    RS_COUNT
};

inline const char* roomScheduleName(u32 s) {
    static const char* const kNames[RS_COUNT] = {"always", "never",     "home_living", "home_kitchen", "home_bed", "home_bath", "shop",
                                                 "office", "restaurant", "nightlife",   "garage",       "dusk",     "porch"};
    return s < (u32)RS_COUNT ? kNames[s] : "";
}

// Homes, garages and exterior lights switch off in daylight; shops, offices, restaurants, bars and shared halls stay on
inline u32 roomScheduleDaylit(u32 s) {
    return (s >= (u32)RS_HOME_LIVING && s <= (u32)RS_HOME_BATH) || s == (u32)RS_GARAGE || s == (u32)RS_DUSK || s == (u32)RS_PORCH ? 1u : 0u;
}

// An on-interval: from `start` to `end`, in minutes after the day's midnight (an end past 1440 runs on into the next
// day), at `level` (of 255). `chance` + 1 of 256 rooms have it (255: all of them; 0 marks an unused slot). `daily` 1:
// each room draws it anew every day (people at home), with its times moving by up to `jitter` minutes either way; 0: a
// room either has it or not, at the same times every day (a shop's hours).
struct RoomInterval {
    u16 start, end;
    u8 chance, level, jitter, daily;
};

// Three intervals per day type (weekdays, then weekends); a chance of 0 marks an unused one
inline const RoomInterval* roomScheduleIntervals(u32 schedule, u32 weekend) {
    static const RoomInterval kTable[RS_COUNT][2][3] = {
        // RS_ALWAYS
        {{{0, 1440, 255, 255, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, {{0, 1440, 255, 255, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_NEVER
        {{{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_HOME_LIVING: 6:30-7:45; 18:00-23:15 (weekends 9:00-12:00; 17:30-24:00); night owls: from about 22:00 a
        // lamp or the TV stays on until 1:30-3:00 in 1 living room of 17, the same ones every night (about 1 home in
        // 9, at two rooms a home on this schedule: living room, dining room, hall), 1 in 12 on Saturday and Sunday
        // nights (a weekend is Saturday and Sunday here, so Friday night keeps the weekdays' share)
        {{{390, 465, 70, 220, 30, 1}, {1080, 1395, 205, 255, 45, 1}, {1320, 1575, 14, 200, 45, 0}},
         {{540, 720, 70, 220, 45, 1}, {1050, 1440, 215, 255, 45, 1}, {1320, 1575, 21, 200, 45, 0}}},
        // RS_HOME_KITCHEN: 6:00-7:45; 18:00-21:00 (weekends 8:00-10:30; 18:00-21:30)
        {{{360, 465, 170, 255, 30, 1}, {1080, 1260, 200, 255, 45, 1}, {0, 0, 0, 0, 0, 0}},
         {{480, 630, 150, 255, 45, 1}, {1080, 1290, 200, 255, 45, 1}, {0, 0, 0, 0, 0, 0}}},
        // RS_HOME_BED: 5:45-6:45; 21:30-23:45 (weekends 7:30-9:00; 22:30-1:00)
        {{{345, 405, 90, 150, 20, 1}, {1290, 1425, 150, 150, 40, 1}, {0, 0, 0, 0, 0, 0}},
         {{450, 540, 70, 150, 40, 1}, {1350, 1500, 150, 150, 45, 1}, {0, 0, 0, 0, 0, 0}}},
        // RS_HOME_BATH: 6:00-7:00; 21:30-22:30 (weekends 8:00-9:30; 22:30-23:30)
        {{{360, 420, 140, 255, 25, 1}, {1290, 1350, 90, 255, 30, 1}, {0, 0, 0, 0, 0, 0}},
         {{480, 570, 120, 255, 40, 1}, {1350, 1410, 90, 255, 30, 1}, {0, 0, 0, 0, 0, 0}}},
        // RS_SHOP: 8:00-21:00, then display lighting until 6:00 in 4 shops of 10 (weekends 9:00-22:00)
        {{{480, 1260, 245, 255, 30, 0}, {1260, 1800, 100, 110, 0, 0}, {0, 0, 0, 0, 0, 0}},
         {{540, 1320, 240, 255, 30, 0}, {1320, 1800, 100, 110, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_OFFICE: 7:30-19:00; late workers 19:00-22:00; the cleaners 20:00-22:30 (weekends a few 10:00-15:00)
        {{{450, 1140, 235, 255, 30, 0}, {1140, 1320, 40, 255, 45, 1}, {1200, 1350, 60, 200, 20, 1}},
         {{600, 900, 25, 255, 45, 1}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_RESTAURANT: 11:00-23:00 (weekends 10:00-23:30)
        {{{660, 1380, 240, 255, 30, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, {{600, 1410, 240, 255, 30, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_NIGHTLIFE: 18:00-3:00 (weekends 17:00-4:00)
        {{{1080, 1620, 235, 255, 45, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, {{1020, 1680, 245, 255, 45, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_GARAGE: 6:30-7:30; 17:30-19:00 (weekends 10:00-11:30; 16:00-18:00)
        {{{390, 450, 60, 255, 20, 1}, {1050, 1140, 80, 255, 30, 1}, {0, 0, 0, 0, 0, 0}},
         {{600, 690, 40, 255, 45, 1}, {960, 1080, 40, 255, 45, 1}, {0, 0, 0, 0, 0, 0}}},
        // RS_DUSK: all day, so whenever it is dark
        {{{0, 1440, 255, 255, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, {{0, 1440, 255, 255, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
        // RS_PORCH: noon to 23:00, so from dusk (weekends to 0:00)
        {{{720, 1380, 255, 255, 30, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}, {{720, 1440, 255, 255, 30, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
    };
    return kTable[schedule < (u32)RS_COUNT ? schedule : (u32)RS_NEVER][weekend != 0u ? 1 : 0];
}

// The mixing of src/core/rng.h hash32, spelt out for the HLSL copy
inline u32 roomHash(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A room's seed, 12 bits (0..4095, as the window records store it): from its building's seed, the floor (its index in
// the plan) and the room (its index in the layout)
inline u32 roomSeed(u32 buildingSeed, u32 floorIndex, u32 roomIndex) {
    return roomHash(buildingSeed ^ roomHash(floorIndex * 0x9e3779b9u + roomIndex * 0x85ebca77u + 1u)) & 4095u;
}

// The minute of the day for roomLight(), 0..1439, from the game clock's hours (0..24). The renderer's frame constants
// and the CPU callers both take it from here, from the same float, so the two copies never split over a rounding.
inline u32 roomMinute(float timeOfDay) {
    const float m = timeOfDay * 60.f;
    return m > 0.f ? (m < 1439.f ? (u32)m : 1439u) : 0u;   // (NaN and negative times give 0)
}

// The light level of a room, 0 (off) to 254:
// - schedule: a RoomSchedule; seed: roomSeed() (only its low 12 bits count); windows: 1 when the room has windows,
//   and for exterior lights (so that a dusk or porch light waits for the dark);
// - day: the game day (day % 7: 0 Monday ... 5 Saturday, 6 Sunday); minute: 0..1439 into it, from roomMinute();
// - night: how dark it is outside, 0 (full day) to 255 (full night); the frame's value (the renderer's roomNight()),
//   the same for the CPU and the shader.
// A daylit class (homes, garages, dusk and porch lights) with windows switches on only once `night` passes the room's
// own threshold, 48-111 (the seed spreads the moment across rooms). Rooms differ a little in brightness (191-254 at
// full level).
inline u32 roomLight(u32 schedule, u32 seed, u32 windows, u32 day, u32 minute, u32 night) {
    const u32 rs = roomHash((seed & 4095u) * 0x9e3779b9u + 0x632be5abu);   // the 12-bit seed spread over 32 bits
    if (windows != 0u && roomScheduleDaylit(schedule) != 0u && night < 48u + ((rs >> 26) & 63u)) return 0u;
    const u32 dimmer = 191u + ((rs >> 20) & 63u);
    u32 best = 0u;
    for (u32 back = 0u; back < 2u; back++) {           // today's intervals, then yesterday's that run past midnight
        if (back == 1u && day == 0u) break;
        const u32 d = day - back;
        const RoomInterval* iv = roomScheduleIntervals(schedule, d % 7u >= 5u ? 1u : 0u);
        const u32 m = minute + back * 1440u;              // the minute on that day's clock
        for (u32 k = 0u; k < 3u; k++) {
            const RoomInterval r = iv[k];
            if (r.chance == 0u) continue;
            const u32 h = roomHash(rs ^ roomHash((r.daily != 0u ? d * 0x632be5abu : 0x51ed270bu) + k * 0x2545f491u + 7u));
            if ((h & 255u) > r.chance) continue;            // this room doesn't have it (that day)
            const u32 span = 2u * r.jitter + 1u;            // both ends move by 0..2*jitter, less jitter
            const u32 s0 = r.start + (h >> 8) % span, s1 = r.end + (h >> 16) % span;
            if (m + r.jitter < s0 || m + r.jitter >= s1) continue;
            const u32 lv = r.level * dimmer / 255u;
            if (lv > best) best = lv;
        }
    }
    return best;
}

}  // namespace World
