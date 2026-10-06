// CPU mirror of the water surface (src/shaders/water.hlsl gerstner()/waveAmplitude()) for buoyancy, swimming and
// splash effects. Keep kWaveDef and the formulas in sync with the shader.
#pragma once
#include "../world/worldmap.h"

namespace Phys {

struct WaveState {
    float time = 0.f;             // = Environment::gameSeconds (shader gTime.x)
    vec2 windDir = vec2(0.7f, 0.7f);  // = Environment::windDir (shader gWind.xy)
    float strength = 0.3f;        // = Environment::wind (shader gWaterParams.w)
};
extern WaveState gWaves;

inline float waveAmplitude(float depth) { return Saturate(depth / 18.f) * (0.35f + gWaves.strength * 1.6f) + 0.03f; }

// Returns the Gerstner displacement at undisplaced water-plane position p; optional normal.
inline vec3 gerstner(vec2 p, float amp, float t, vec3* nrm = nullptr) {
    static const vec4 kWaveDef[7] = {vec4(1.0f, 0.35f, 64.0f, 1.0f), vec4(0.8f, -0.6f, 41.0f, 0.7f), vec4(-0.3f, 1.0f, 27.0f, 0.45f),
                                     vec4(0.95f, 0.1f, 17.0f, 0.3f), vec4(0.2f, -1.0f, 11.0f, 0.2f), vec4(-0.7f, 0.7f, 7.0f, 0.12f),
                                     vec4(0.6f, 0.8f, 4.3f, 0.07f)};
    const int kWaves = 7;
    vec3 disp(0.f);
    vec3 dx(1, 0, 0), dy(0, 1, 0);
    for (int i = 0; i < kWaves; i++) {
        vec2 d = normalize(vec2(kWaveDef[i].x, kWaveDef[i].y) + gWaves.windDir * 0.4f);
        float L = kWaveDef[i].z;
        float k = kTwoPi / L;
        float c = sqrtf(9.81f / k);
        float a = kWaveDef[i].w * amp * 0.55f * (L / 64.f + 0.35f);
        float q = 0.55f / (k * a * kWaves + 1e-4f);
        float ph = k * (dot(d, p) - c * t);
        float s = sinf(ph), co = cosf(ph);
        disp += vec3(d * (q * a * co), a * s);
        float wa = k * a;
        dx += vec3(-q * d.x * d.x * wa * s, -q * d.x * d.y * wa * s, d.x * wa * co);
        dy += vec3(-q * d.x * d.y * wa * s, -q * d.y * d.y * wa * s, d.y * wa * co);
    }
    if (nrm) *nrm = normalize(cross(dx, dy));
    return disp;
}

// Water surface height (with waves) at world (x, y); returns false if there is no water body there.
// Uses two fixed-point iterations to invert the horizontal Gerstner displacement.
inline bool waterSurface(float x, float y, float& z, vec3* normal = nullptr) {
    const World::WorldMap& m = *World::gMap;
    float level = m.waterAt(x, y);
    if (level <= World::kNoWater + 1.f) return false;
    float depth = level - m.heightAt(x, y);
    float amp = waveAmplitude(Max(depth, 0.f));
    vec2 p(x, y), q = p;
    vec3 d;
    for (int it = 0; it < 2; it++) {
        d = gerstner(q, amp, gWaves.time);
        q = p - d.xy();
    }
    d = gerstner(q, amp, gWaves.time, normal);
    z = level + d.z;
    return true;
}

}  // namespace Phys
