// Local light record (matches LightGPU in src/render/renderer.h) and the angular falloff shared by the deferred
// lighting, the volumetric fog and forward passes.
#ifndef LIGHTS_HLSLI
#define LIGHTS_HLSLI

struct LightGPU {
    float3 pos;        // camera-relative
    float radius;
    float3 color;      // luminous intensity (cd) * rgb
    float spotCos;     // cos of the outer cone angle; <= -1 for point lights
    float3 dir;        // spot direction
    float spotInner;   // cos of the inner cone angle; > 1.5 marks a vehicle headlight (low-beam pattern)
};

// Low-beam headlight pattern: sharp cut-off just below the horizon with a raised right-hand (kerb side)
// shoulder, wide horizontal spread and a hot core on the road; faint spill above the cut-off.
float headlightPattern(float3 fwd, float3 d) {
    float3 side = cross(fwd, float3(0, 0, 1));
    float sl = length(side);
    side = sl > 1e-3 ? side / sl : float3(1, 0, 0);
    float3 up = cross(side, fwd);
    float z = dot(d, fwd);
    if (z <= 0.05) return 0.0;
    float x = dot(d, side) / z, y = dot(d, up) / z;
    float cutLine = -0.035 + 0.045 * smoothstep(0.0, 0.25, x);
    float below = smoothstep(cutLine + 0.012, cutLine - 0.012, y);
    float wide = exp(-x * x / 0.2);
    float core = exp(-x * x / 0.018 - sq(y + 0.06) / 0.0035);
    float spill = 0.05 * exp(-(x * x + y * y) / 0.25);
    return saturate(below * (wide * 0.5 + core * 1.3) + spill) * smoothstep(0.05, 0.3, z);
}

// Angular attenuation of a light for the direction Lv (normalized, surface -> light).
float lightAngular(LightGPU L, float3 Lv) {
    if (L.spotCos <= -1.0) return 1.0;
    if (L.spotInner > 1.5) return headlightPattern(L.dir, -Lv);
    return smoothstep(L.spotCos, max(L.spotInner, L.spotCos + 1e-4), dot(-Lv, L.dir));   // equal edges: 0/0
}

#endif
