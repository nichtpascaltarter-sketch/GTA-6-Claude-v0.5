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
    float spotInner;   // cos of the inner cone angle; 1.5 .. 2.5 marks a vehicle headlight (low-beam pattern), > 2.5 a
                       // street lamp (road-lighting distribution, dir: main axis tilted towards the road)
};

// Road lighting (a full cut-off street lamp): the intensity rises from the nadir to a peak ~60 degrees out, where it
// reaches along the road, and falls to nothing by 80 degrees (no light above: the lamp's pool has an edge); behind the
// pole, over the pavement and the houses, about a third (the reflector throws the light over the road). axis: the
// lamp's main axis (down, tilted towards the road); d: lamp -> surface.
float streetLampPattern(float3 axis, float3 d) {
    float cn = -d.z;                                   // cos of the angle from the nadir
    if (cn <= 0.17) return 0.0;
    float vert = lerp(0.55, 1.0, smoothstep(0.98, 0.5, cn)) * smoothstep(0.17, 0.42, cn);
    float sn = sqrt(saturate(1.0 - cn * cn));
    float side = dot(d.xy, normalize(axis.xy + 1e-6)) / max(sn, 1e-4);   // +1 over the road, -1 behind the pole
    float lat = lerp(1.0, lerp(0.32, 1.0, smoothstep(-0.45, 0.35, side)), smoothstep(0.12, 0.55, sn));
    return vert * lat;
}

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
    if (L.spotInner > 2.5) return streetLampPattern(L.dir, -Lv);
    if (L.spotInner > 1.5) return headlightPattern(L.dir, -Lv);
    return smoothstep(L.spotCos, max(L.spotInner, L.spotCos + 1e-4), dot(-Lv, L.dir));   // equal edges: 0/0
}

#endif
