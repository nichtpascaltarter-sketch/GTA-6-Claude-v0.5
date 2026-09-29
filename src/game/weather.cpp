// Dynamic weather: sub-tropical patterns (clear mornings, afternoon cumulus build-up, short thunderstorms, rare fog)
// with smooth transitions, lightning + thunder, and the game clock (1 game minute = 2 real seconds).
#include "gameworld.h"

namespace Game {

enum WeatherKind : u8 { WX_CLEAR = 0, WX_FAIR, WX_CLOUDY, WX_OVERCAST, WX_RAIN, WX_STORM, WX_FOG, WX_COUNT };

struct WeatherPreset {
    const char* name;
    float cloud, rain, fog, wind, haze;
};

namespace weather_detail {
const WeatherPreset kPresets[WX_COUNT] = {
    {"Clear", 0.12f, 0.f, 0.f, 0.2f, 0.9f},
    {"Fair", 0.35f, 0.f, 0.f, 0.3f, 1.0f},
    {"Cloudy", 0.62f, 0.f, 0.f, 0.4f, 1.15f},
    {"Overcast", 0.85f, 0.f, 0.05f, 0.5f, 1.35f},
    {"Rain", 0.9f, 0.55f, 0.08f, 0.6f, 1.5f},
    {"Thunderstorm", 1.0f, 1.0f, 0.1f, 0.9f, 1.6f},
    {"Fog", 0.5f, 0.f, 0.85f, 0.1f, 2.2f},
};
}  // namespace weather_detail

using namespace weather_detail;

struct WeatherSystem {
    WeatherKind cur = WX_FAIR, next = WX_FAIR;
    float blend = 1.f;          // 0 -> 1 from cur to next
    float blendSpeed = 0.f;
    float timeToChange = 900.f; // real seconds
    float lightningTimer = 5.f;
    float flash = 0.f;
    float thunderDelay = -1.f;
    float thunderVolume = 1.f;
    u32 seed = 12345;
    bool locked = false;        // missions / debug can pin the weather
    float wetness = 0.f;

    void setImmediate(WeatherKind k) {
        cur = next = k;
        blend = 1.f;
    }
    void transitionTo(WeatherKind k, float seconds) {
        if (k == next) return;
        cur = blend >= 1.f ? next : cur;
        next = k;
        blend = 0.f;
        blendSpeed = 1.f / Max(seconds, 1.f);
    }
    WeatherKind pickNext(float tod) {
        seed = hash32(seed + 0x9E37u);
        float r = hashToFloat(seed);
        bool afternoon = tod > 13.f && tod < 19.f;
        bool morning = tod > 5.f && tod < 9.f;
        if (morning && r < 0.12f) return WX_FOG;
        switch (next) {
            case WX_CLEAR: return r < 0.6f ? WX_FAIR : WX_CLEAR;
            case WX_FAIR: return afternoon ? (r < 0.5f ? WX_CLOUDY : WX_FAIR) : (r < 0.3f ? WX_CLEAR : (r < 0.7f ? WX_FAIR : WX_CLOUDY));
            case WX_CLOUDY: return afternoon ? (r < 0.35f ? WX_RAIN : (r < 0.55f ? WX_STORM : WX_OVERCAST)) : (r < 0.5f ? WX_FAIR : WX_OVERCAST);
            case WX_OVERCAST: return r < 0.45f ? WX_RAIN : (r < 0.6f ? WX_STORM : WX_CLOUDY);
            case WX_RAIN: return r < 0.25f ? WX_STORM : (r < 0.7f ? WX_CLOUDY : WX_OVERCAST);
            case WX_STORM: return r < 0.6f ? WX_RAIN : WX_CLOUDY;
            case WX_FOG: return r < 0.7f ? WX_FAIR : WX_CLEAR;
            default: return WX_FAIR;
        }
    }
    void update(Render::Environment& env, float dt, dvec3 listener) {
        if (!locked) {
            timeToChange -= dt;
            if (timeToChange <= 0.f) {
                transitionTo(pickNext(env.timeOfDay), 90.f + hashToFloat(hash32(seed)) * 120.f);
                timeToChange = 600.f + hashToFloat(hash32(seed * 3u)) * 900.f;
            }
        }
        blend = Min(1.f, blend + dt * blendSpeed);
        float t = blend * blend * (3.f - 2.f * blend);
        const WeatherPreset& a = kPresets[cur];
        const WeatherPreset& b = kPresets[next];
        env.cloudCover = Lerp(a.cloud, b.cloud, t);
        env.rain = Lerp(a.rain, b.rain, t);
        env.fogDensity = Lerp(a.fog, b.fog, t);
        env.wind = Lerp(a.wind, b.wind, t);
        env.haze = Lerp(a.haze, b.haze, t);
        // wind direction veers slowly
        float wa = atan2f(env.windDir.y, env.windDir.x) + sinf(env.gameSeconds * 0.002f) * dt * 0.01f;
        env.windDir = vec2(cosf(wa), sinf(wa));
        // surfaces get wet quickly in rain and dry slowly (faster in sunshine)
        if (env.rain > 0.05f) wetness = Min(1.f, wetness + dt * env.rain * 0.05f);
        else wetness = Max(0.f, wetness - dt * (env.timeOfDay > 8.f && env.timeOfDay < 18.f ? 0.004f : 0.0015f));
        env.wetness = wetness;
        // lightning during storms
        float stormy = Saturate((env.rain - 0.7f) / 0.3f);
        flash = Max(0.f, flash - dt * 6.f);
        if (stormy > 0.f) {
            lightningTimer -= dt * stormy;
            if (lightningTimer <= 0.f) {
                seed = hash32(seed + 17u);
                lightningTimer = 4.f + hashToFloat(seed) * 14.f;
                flash = 1.f;
                float dist = 800.f + hashToFloat(hash32(seed)) * 6000.f;
                thunderDelay = dist / 343.f;
                thunderVolume = Saturate(1.4f - dist / 7000.f);
            }
        }
        if (thunderDelay >= 0.f) {
            thunderDelay -= dt;
            if (thunderDelay < 0.f) {
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_THUNDER, thunderVolume);
#endif
            }
        }
        // flicker envelope
        float fl = flash > 0.f ? flash * (0.6f + 0.4f * sinf(flash * 40.f)) : 0.f;
        env.lightning = fl;
        (void)listener;
    }
};

}  // namespace Game
