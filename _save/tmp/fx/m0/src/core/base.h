// Core base definitions shared by every module.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <functional>
#include <utility>

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE inline __attribute__((always_inline))
#endif

// Logging goes to a file in the user data folder and to the debugger output.
void LogPrintf(const char* fmt, ...);
[[noreturn]] void FatalError(const char* fmt, ...);

#define LOG(...) LogPrintf(__VA_ARGS__)
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) FatalError("Check failed: %s (%s:%d)", #cond, __FILE__, __LINE__); \
    } while (0)

template <typename T> FORCEINLINE T Min(T a, T b) { return a < b ? a : b; }
template <typename T> FORCEINLINE T Max(T a, T b) { return a > b ? a : b; }
template <typename T> FORCEINLINE T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
FORCEINLINE float Saturate(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
FORCEINLINE float Lerp(float a, float b, float t) { return a + (b - a) * t; }
FORCEINLINE float SmoothStep(float e0, float e1, float x) {
    float t = Saturate((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}
FORCEINLINE float Sign(float v) { return v < 0.f ? -1.f : 1.f; }
FORCEINLINE float Sq(float v) { return v * v; }

static const float kPi = 3.14159265358979323846f;
static const float kTwoPi = 6.28318530717958647692f;
static const float kHalfPi = 1.57079632679489661923f;
static const float kDegToRad = kPi / 180.f;
static const float kRadToDeg = 180.f / kPi;

// Simple string formatting helper.
std::string StrFormat(const char* fmt, ...);

// Monotonic seconds since program start.
double TimeSeconds();
