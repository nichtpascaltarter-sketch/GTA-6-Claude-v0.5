// Procedural material texture generation (runs once at startup).
// Output: albedo (sRGB-encoded manually, alpha = height) and normal/roughness (xy normal, z roughness, w ao).
#include "common.hlsli"

RWTexture2DArray<float4> uAlbedo : register(u0);
RWTexture2DArray<float4> uNormal : register(u1);

cbuffer MatGenCB : register(b1) {
    uint gLayer;
    uint gType;
    uint gSize;
    uint gSeed;
    float4 gColorA;
    float4 gColorB;
    float4 gColorC;
    float4 gParams;  // generator specific
};

// ---------------------------------------------------------------------------------------------
// Tileable noise (period in lattice cells)
float hashP(float2 i, float P) { i = i - floor(i / P) * P; return (hash2u(asuint(int2(i)) + gSeed * 7919u) >> 8) * (1.0 / 16777216.0); }
float2 gradP(float2 i, float P) { float a = hashP(i, P) * TWO_PI; return float2(cos(a), sin(a)); }
float tnoise(float2 p, float P) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(gradP(i, P), f), b = dot(gradP(i + float2(1, 0), P), f - float2(1, 0));
    float c = dot(gradP(i + float2(0, 1), P), f - float2(0, 1)), d = dot(gradP(i + float2(1, 1), P), f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 1.4142;
}
float tvalue(float2 p, float P) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hashP(i, P), hashP(i + float2(1, 0), P), u.x), lerp(hashP(i + float2(0, 1), P), hashP(i + float2(1, 1), P), u.x), u.y);
}
float tfbm(float2 uv, float baseFreq, int oct, float gain) {
    float s = 0, a = 0.5, n = 0, f = baseFreq;
    [loop] for (int k = 0; k < oct; k++) {
        s += tnoise(uv * f, f) * a; n += a; a *= gain; f *= 2.0;
    }
    return s / n;
}
// Tileable worley: returns (F1, F2, cell hash)
float3 tworley(float2 uv, float freq) {
    float2 p = uv * freq;
    float2 i = floor(p), f = frac(p);
    float f1 = 8, f2 = 8, id = 0;
    [loop] for (int y = -1; y <= 1; y++)
    [loop] for (int x = -1; x <= 1; x++) {
        float2 g = float2(x, y);
        float2 c = i + g;
        float2 o = float2(hashP(c, freq), hashP(c + 57.0, freq));
        float d = length(g + o - f);
        if (d < f1) { f2 = f1; f1 = d; id = hashP(c + 113.0, freq); }
        else if (d < f2) f2 = d;
    }
    return float3(f1, f2, id);
}

struct Surf {
    float3 albedo;
    float height;
    float rough;
    float ao;
};

// ---------------------------------------------------------------------------------------------
// Generators. uv in [0,1) tiles.
Surf genSand(float2 uv) {
    Surf s;
    float ripples = sin((uv.y + tfbm(uv, 3, 3, 0.5) * 0.08) * TWO_PI * 22.0) * 0.5 + 0.5;
    float grain = tvalue(uv * 512, 512) * 0.6 + tvalue(uv * 256, 256) * 0.4;
    float big = tfbm(uv, 4, 4, 0.5) * 0.5 + 0.5;
    s.height = ripples * 0.35 + grain * 0.4 + big * 0.25;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, big);
    c *= 0.88 + grain * 0.2;
    // shell fragments / dark grains
    float speck = step(0.93, tvalue(uv * 300, 300));
    c = lerp(c, gColorC.rgb, speck * 0.6);
    s.albedo = c;
    s.rough = 0.85 + grain * 0.1;
    s.ao = 0.85 + ripples * 0.15;
    return s;
}

Surf genGrass(float2 uv, float dry) {
    Surf s;
    // Blades: anisotropic streaks in random directions per clump
    float3 w = tworley(uv, 24);
    float ang = w.z * TWO_PI;
    float2 dir = float2(cos(ang), sin(ang));
    float2 q = float2(dot(uv, dir), dot(uv, float2(-dir.y, dir.x)));
    float blades = tvalue(float2(q.x * 900.0, q.y * 60.0), 900) ;
    blades = pow(saturate(blades), 1.5);
    float clump = tfbm(uv, 6, 4, 0.55) * 0.5 + 0.5;
    float patchy = tfbm(uv, 2, 3, 0.5) * 0.5 + 0.5;
    s.height = blades * 0.6 + clump * 0.4;
    float3 green = lerp(gColorA.rgb, gColorB.rgb, clump);
    float3 c = lerp(green, gColorC.rgb, saturate(patchy * dry * 1.6 - 0.2));
    c *= 0.6 + blades * 0.6;
    // soil showing through between blades
    float soil = saturate(0.35 - blades) * 1.5 * (1.0 - clump);
    c = lerp(c, float3(0.28, 0.22, 0.15), soil * 0.5);
    s.albedo = c;
    s.rough = 0.8 + 0.15 * (1.0 - blades);
    s.ao = 0.55 + blades * 0.45;
    return s;
}

Surf genDirt(float2 uv) {
    Surf s;
    float base = tfbm(uv, 4, 5, 0.55) * 0.5 + 0.5;
    float3 pw = tworley(uv, 40);
    float pebbles = smoothstep(0.35, 0.15, pw.x) * step(0.55, pw.z);
    float3 cw = tworley(uv, 7);
    float cracks = smoothstep(0.06, 0.0, cw.y - cw.x);
    float fine = tvalue(uv * 400, 400);
    s.height = base * 0.5 + pebbles * 0.5 - cracks * 0.3 + fine * 0.1;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, base);
    c = lerp(c, gColorC.rgb * (0.7 + pw.z * 0.5), pebbles);
    c *= 0.9 + fine * 0.15 - cracks * 0.3;
    s.albedo = c;
    s.rough = 0.9 - pebbles * 0.15;
    s.ao = 1.0 - cracks * 0.5;
    return s;
}

Surf genRock(float2 uv) {
    Surf s;
    float layers = sin((uv.y * 6.0 + tfbm(uv, 3, 4, 0.5) * 0.9) * TWO_PI) * 0.5 + 0.5;
    float3 cw = tworley(uv, 5);
    float cracks = smoothstep(0.05, 0.0, cw.y - cw.x);
    float detail = tfbm(uv, 16, 5, 0.6) * 0.5 + 0.5;
    s.height = layers * 0.3 + detail * 0.5 + cw.x * 0.2 - cracks * 0.4;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, detail);
    c = lerp(c, gColorC.rgb, layers * 0.3);
    // lichen
    float lichen = smoothstep(0.55, 0.75, tfbm(uv + 0.3, 8, 4, 0.5) * 0.5 + 0.5);
    c = lerp(c, float3(0.45, 0.47, 0.30), lichen * 0.35);
    c *= 1.0 - cracks * 0.5;
    s.albedo = c;
    s.rough = 0.75 + detail * 0.2;
    s.ao = 1.0 - cracks * 0.6;
    return s;
}

Surf genMud(float2 uv) {
    Surf s;
    float base = tfbm(uv, 3, 5, 0.5) * 0.5 + 0.5;
    float puddle = smoothstep(0.55, 0.65, tfbm(uv, 2, 4, 0.5) * 0.5 + 0.5);
    float fine = tfbm(uv, 32, 3, 0.5) * 0.5 + 0.5;
    s.height = base * 0.6 + fine * 0.2 - puddle * 0.3;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, base);
    c = lerp(c, gColorC.rgb, puddle * 0.5);
    c *= 0.85 + fine * 0.2;
    s.albedo = c;
    s.rough = lerp(0.6, 0.15, puddle);
    s.ao = 0.9;
    return s;
}

Surf genLeafLitter(float2 uv) {
    Surf s;
    float3 acc = 0; float h = 0; float cover = 0;
    // several layers of leaf-shaped cells
    [loop] for (int l = 0; l < 3; l++) {
        float f = 30.0 + l * 11.0;
        float3 w = tworley(uv + l * 0.37, f);
        float leaf = smoothstep(0.42, 0.25, w.x);
        float3 lc = lerp(gColorA.rgb, gColorB.rgb, w.z);
        lc = lerp(lc, gColorC.rgb, step(0.8, w.z));
        acc = lerp(acc, lc, leaf * (1.0 - cover * 0.5));
        h = max(h, leaf * (0.5 + l * 0.2));
        cover = max(cover, leaf);
    }
    float soil = tfbm(uv, 8, 3, 0.5) * 0.5 + 0.5;
    float3 c = lerp(float3(0.16, 0.12, 0.08) * (0.8 + soil * 0.4), acc, cover);
    // twigs
    float2 tq = float2(uv.x * 25.0 + tfbm(uv, 4, 2, 0.5), uv.y * 3.0);
    float twig = smoothstep(0.03, 0.0, abs(frac(tq.x) - 0.5) - 0.0) * step(0.85, tvalue(tq * float2(1, 8), 25));
    c = lerp(c, float3(0.22, 0.16, 0.1), twig);
    s.albedo = c;
    s.height = h * 0.7 + soil * 0.3;
    s.rough = 0.85;
    s.ao = 0.6 + cover * 0.4;
    return s;
}

Surf genGravel(float2 uv) {
    Surf s;
    float3 w1 = tworley(uv, 90);
    float3 w2 = tworley(uv + 0.5, 60);
    float stone1 = smoothstep(0.5, 0.2, w1.x);
    float stone2 = smoothstep(0.5, 0.25, w2.x);
    float stones = max(stone1, stone2 * 0.8);
    float id = stone1 > stone2 ? w1.z : w2.z;
    float stain = tfbm(uv, 3, 4, 0.5) * 0.5 + 0.5;
    s.height = stones * (0.6 + id * 0.4);
    float3 c = lerp(gColorA.rgb, gColorB.rgb, id);
    c = lerp(gColorC.rgb, c, stones);
    c *= 0.8 + stain * 0.3;
    s.albedo = c;
    s.rough = 0.8 + (1.0 - stones) * 0.15;
    s.ao = 0.5 + stones * 0.5;
    return s;
}

#ifndef GEN
#define GEN 0
#endif
Surf evalSurf(float2 uv) {
#if GEN == 0
    return genSand(uv);
#elif GEN == 1
    return genGrass(uv, gParams.x);
#elif GEN == 2
    return genDirt(uv);
#elif GEN == 3
    return genRock(uv);
#elif GEN == 4
    return genMud(uv);
#elif GEN == 5
    return genLeafLitter(uv);
#else
    return genGravel(uv);
#endif
}

[numthreads(8, 8, 1)]
void csGenerate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gSize || id.y >= gSize) return;
    float texel = 1.0 / gSize;
    float2 uv = (id.xy + 0.5) * texel;
    Surf c = evalSurf(uv);
    Surf sx = evalSurf(frac(uv + float2(texel, 0)));
    Surf sy = evalSurf(frac(uv + float2(0, texel)));
    float strength = gParams.w > 0 ? gParams.w : 6.0;
    float2 grad = float2(sx.height - c.height, sy.height - c.height) * strength;
    float3 n = normalize(float3(-grad, 1.0));
    uAlbedo[uint3(id.xy, gLayer)] = float4(linearToSrgb(saturate(c.albedo)), saturate(c.height));
    uNormal[uint3(id.xy, gLayer)] = float4(n.xy * 0.5 + 0.5, saturate(c.rough), saturate(c.ao));
}
