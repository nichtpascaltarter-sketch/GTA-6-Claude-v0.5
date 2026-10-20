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

// Low-beam headlight pattern (fwd: the lamp's aim, ~1.7 degrees down on a car): a sharp cut-off 0.7 degrees above the
// aim on the oncoming side (the road is lit out to ~35 m) rising on the kerb side, the most intense just under the
// cut-off and slightly right (1.6x the light's nominal intensity), falling off towards the foreground (half at 3.4 degrees under the cut-off) so the road
// is lit evenly from a few metres to the cut-off instead of a hot puddle in front of the bumper; a wide horizontal
// spread and faint spill above.
float headlightPattern(float3 fwd, float3 d) {
    float3 side = cross(fwd, float3(0, 0, 1));
    float sl = length(side);
    side = sl > 1e-3 ? side / sl : float3(1, 0, 0);
    float3 up = cross(side, fwd);
    float z = dot(d, fwd);
    if (z <= 0.05) return 0.0;
    float x = dot(d, side) / z, y = dot(d, up) / z;
    float cutLine = 0.012 + 0.035 * smoothstep(0.0, 0.25, x);
    float below = smoothstep(cutLine + 0.008, cutLine - 0.008, y);
    float a = max(cutLine - y, 0.0);
    float vert = 1.0 / (1.0 + sq(a / 0.06));
    float hor = exp(-x * x / 0.25) * 0.7 + exp(-sq(x - 0.06) / 0.02) * 0.9;   // peak ~1.6x the light's intensity
    float spill = 0.03 * exp(-(x * x + y * y) / 0.25);
    return (below * vert * hor + spill) * smoothstep(0.05, 0.3, z);
}

// Angular attenuation of a light for the direction Lv (normalized, surface -> light).
float lightAngular(LightGPU L, float3 Lv) {
    if (L.spotCos <= -1.0) return 1.0;
    if (L.spotInner > 2.5) return streetLampPattern(L.dir, -Lv);
    if (L.spotInner > 1.5) return headlightPattern(L.dir, -Lv);
    return smoothstep(L.spotCos, max(L.spotInner, L.spotCos + 1e-4), dot(-Lv, L.dir));   // equal edges: 0/0
}

#endif
