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


// ---------------------------------------------------------------------------------------------
// Urban materials. Each texture tiles; physical size set by the material table's uv scale.
float brickPattern(float2 uv, float rows, float cols, float mortar, out float2 cell, out float id) {
    float2 p = uv * float2(cols, rows);
    float row = floor(p.y);
    p.x += fmod(row, 2.0) * 0.5;
    cell = floor(p);
    float2 f = frac(p);
    id = hashP(cell + 17.0, cols * 2.0);
    float2 m = float2(mortar * cols / rows, mortar);   // same joint width across and along (cells are not square)
    float edge = min(min(f.x, 1.0 - f.x) / m.x, min(f.y, 1.0 - f.y) / m.y);
    return saturate(edge);
}

// Asphalt binder tone only: the texture repeats every 4 m, so it carries no distinctive features. The aggregate is
// added per pixel in world.hlsl (asphaltGrain, scaled to the pixel footprint: a magnified per-texel aggregate reads
// as coarse gravel), and so are cracks, sealant, patches, wheel tracks, oil drips, covers and drains (road space).
Surf genAsphalt(float2 uv) {
    Surf s;
    float mott = tfbm(uv, 24.0, 3, 0.55) * 0.5 + 0.5;    // ~17 cm binder richness
    float fine = tfbm(uv, 96.0, 2, 0.5) * 0.5 + 0.5;     // ~4 cm
    float big = tfbm(uv, 2.0, 3, 0.5) * 0.5 + 0.5;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, 0.3 + big * 0.4);
    c *= 0.9 + mott * 0.16 + (fine - 0.5) * 0.1;
    s.albedo = c;
    s.height = 0.5 + (mott - 0.5) * 0.1 + (fine - 0.5) * 0.08;
    s.rough = 0.86 + (mott - 0.5) * 0.08;
    s.ao = 1.0;
    return s;
}

Surf genConcrete(float2 uv) {
    Surf s;
    float big = tfbm(uv, 3, 5, 0.5) * 0.5 + 0.5;
    float fine = tvalue(uv * 500, 500);
    float pores = step(0.93, tvalue(uv * 900 + 1.3, 900));
    float stain = smoothstep(0.55, 0.8, tfbm(uv + 2.0, 2, 4, 0.55) * 0.5 + 0.5);
    s.height = 0.6 + big * 0.2 + fine * 0.1 - pores * 0.4;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, big);
    c *= 0.93 + fine * 0.1 - pores * 0.3;
    c = lerp(c, gColorC.rgb, stain * 0.35);
    s.albedo = c;
    s.rough = 0.85 + fine * 0.1;
    s.ao = 1.0 - pores * 0.4;
    return s;
}

Surf genSidewalk(float2 uv) {
    // 2x2 slabs per tile with tooled joints
    Surf s = genConcrete(uv);
    float2 f = frac(uv * 2.0);
    float2 slab = floor(uv * 2.0);
    float joint = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    float j = smoothstep(0.008, 0.02, joint);
    float tint = hashP(slab + 5.0, 2.0);
    s.albedo *= lerp(0.55, 1.0, j) * (0.94 + tint * 0.1);
    s.height = s.height * j + (1.0 - j) * 0.1;
    s.ao = min(s.ao, lerp(0.5, 1.0, j));
    return s;
}

// Running-bond brick at real size on the 2 m tile: 26 courses x 9 bricks (222 x 77 mm with 10 mm joints). Joints are
// recessed with rounded, slightly chipped arrises and occluded; each brick has its own tint (a few darker clinkers),
// a sandy face texture and a little soot / wear.
Surf genBrick(float2 uv) {
    Surf s;
    const float rows = 26.0, cols = 9.0, tile = 2.0;
    float2 p = uv * float2(cols, rows);
    float row = floor(p.y);
    p.x += fmod(row, 2.0) * 0.5;
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 wc = float2(fmod(cell.x, cols), cell.y);   // the half brick at the tile's right edge continues on the left
    float id = hashP(wc + 17.0, 64.0);
    float id2 = hashP(wc + 91.0, 64.0);
    // distance to the nearest joint centre line (m), chipped along the arrises
    float dx = min(f.x, 1.0 - f.x) * tile / cols, dy = min(f.y, 1.0 - f.y) * tile / rows;
    float chip = tvalue(uv * 900.0, 900.0);
    float d = min(dx, dy) - smoothstep(0.55, 0.9, chip) * 0.0025;
    float face = smoothstep(0.0045, 0.0085, d);        // 0 in the joint, 1 on the brick face (rounded arris)
    float grain = tvalue(uv * 1400.0, 1400.0) * 0.5 + tvalue(uv * 420.0, 420.0) * 0.5;
    float3 bc = lerp(gColorA.rgb, gColorB.rgb, id);
    bc *= lerp(float3(1.04, 0.98, 0.95), float3(0.95, 1.0, 1.05), id2) * (0.88 + grain * 0.22);
    bc = lerp(bc, bc * float3(0.55, 0.5, 0.52), step(0.92, id2));   // clinkers
    float soot = smoothstep(0.55, 0.85, tfbm(uv, 3.0, 3, 0.5) * 0.5 + 0.5);
    bc *= 1.0 - soot * 0.18;
    float3 mortarC = gColorC.rgb * (0.85 + tvalue(uv * 700.0, 700.0) * 0.25) * (1.0 - soot * 0.25);
    s.albedo = lerp(mortarC, bc, face);
    s.height = face * (0.75 + grain * 0.1) + (1.0 - face) * 0.25;
    s.rough = lerp(0.97, 0.85, face);
    s.ao = lerp(0.5, 1.0, smoothstep(0.002, 0.012, d));
    return s;
}

Surf genStucco(float2 uv) {
    Surf s;
    float bumps = tfbm(uv, 24, 5, 0.6) * 0.5 + 0.5;
    float swirl = tfbm(uv * 1.0 + tfbm(uv, 4, 3, 0.5) * 0.3, 8, 4, 0.5) * 0.5 + 0.5;
    float stain = smoothstep(0.5, 0.9, tfbm(uv, 2, 4, 0.5) * 0.5 + 0.5);
    s.height = bumps * 0.7 + swirl * 0.3;
    float3 c = gColorA.rgb * (0.92 + bumps * 0.1);
    c = lerp(c, gColorB.rgb, stain * 0.25);
    s.albedo = c;
    s.rough = 0.9;
    s.ao = 0.85 + bumps * 0.15;
    return s;
}

Surf genPlaster(float2 uv) {
    Surf s = genStucco(uv);
    float smooth = tfbm(uv, 6, 3, 0.5) * 0.5 + 0.5;
    s.height = s.height * 0.3 + smooth * 0.2;
    s.rough = 0.8;
    // vertical rain streaks
    float streak = tvalue(float2(uv.x * 80.0, uv.y * 2.0), 80);
    s.albedo = lerp(s.albedo, s.albedo * 0.8, smoothstep(0.7, 1.0, streak) * (1.0 - uv.y) * 0.5);
    return s;
}

Surf genSiding(float2 uv) {
    Surf s;
    float boards = 10.0;
    float f = frac(uv.y * boards);
    float bevel = smoothstep(0.0, 0.9, f);
    float grain = tvalue(float2(uv.x * 20.0, uv.y * 400.0), 20) * 0.5 + tfbm(uv, 16, 3, 0.5) * 0.25;
    s.height = bevel * 0.6 + grain * 0.1;
    float3 c = gColorA.rgb * (0.9 + grain * 0.15);
    c *= lerp(0.7, 1.0, smoothstep(0.0, 0.08, f));
    s.albedo = c;
    s.rough = 0.7;
    s.ao = lerp(0.6, 1.0, smoothstep(0.0, 0.1, f));
    return s;
}

Surf genRoofTile(float2 uv) {
    Surf s;
    // barrel (S) tiles: rows along y, waves along x
    float rows = 8.0, cols = 8.0;
    float2 p = uv * float2(cols, rows);
    float row = floor(p.y);
    float fx = frac(p.x + fmod(row, 2.0) * 0.5);
    float fy = frac(p.y);
    float wave = sin(fx * PI);
    float overlap = smoothstep(0.0, 0.25, fy);
    float id = hashP(float2(floor(p.x + fmod(row, 2.0) * 0.5), row), cols);
    s.height = wave * 0.7 * overlap + fy * 0.3;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, id);
    float moss = smoothstep(0.6, 0.9, tfbm(uv, 3, 4, 0.5) * 0.5 + 0.5) * (1.0 - wave);
    c = lerp(c, gColorC.rgb, moss * 0.4);
    c *= lerp(0.55, 1.0, overlap) * (0.85 + wave * 0.2);
    s.albedo = c;
    s.rough = 0.75;
    s.ao = lerp(0.5, 1.0, overlap);
    return s;
}

Surf genShingle(float2 uv) {
    Surf s;
    float2 cell; float id;
    float e = brickPattern(uv, 12.0, 6.0, 0.02, cell, id);
    float2 f = frac(uv * float2(6.0, 12.0));
    float grit = tvalue(uv * 600, 600);
    s.height = f.y * 0.8 + grit * 0.2;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, id * 0.7 + grit * 0.3);
    c *= lerp(0.6, 1.0, smoothstep(0.0, 0.15, f.y)) * lerp(0.8, 1.0, e);
    s.albedo = c;
    s.rough = 0.9;
    s.ao = lerp(0.6, 1.0, smoothstep(0.0, 0.2, f.y));
    return s;
}

Surf genMetalRoof(float2 uv) {
    Surf s;
    float ribs = 16.0;
    float fx = frac(uv.x * ribs);
    float rib = smoothstep(0.42, 0.5, fx) * smoothstep(0.58, 0.5, fx);
    float corr = sin(uv.x * ribs * TWO_PI * 3.0) * 0.5 + 0.5;
    float rust = smoothstep(0.55, 0.85, tfbm(uv, 3, 5, 0.55) * 0.5 + 0.5);
    s.height = lerp(corr * 0.4, 1.0, rib);
    float3 c = lerp(gColorA.rgb, gColorB.rgb, tfbm(uv, 2, 3, 0.5) * 0.5 + 0.5);
    c = lerp(c, gColorC.rgb, rust * 0.7);
    s.albedo = c;
    s.rough = lerp(0.35, 0.85, rust);
    s.ao = 0.85 + corr * 0.15;
    return s;
}

Surf genGlass(float2 uv) {
    Surf s;
    float dirt = tfbm(uv, 4, 4, 0.5) * 0.5 + 0.5;
    s.height = 0.5;
    s.albedo = gColorA.rgb * (0.9 + dirt * 0.2);
    s.rough = 0.05 + dirt * 0.08;
    s.ao = 1;
    return s;
}

Surf genPaintedMetal(float2 uv) {
    Surf s;
    float scratches = step(0.97, tvalue(float2(uv.x * 900.0, uv.y * 30.0), 900));
    float wear = smoothstep(0.6, 0.9, tfbm(uv, 4, 4, 0.5) * 0.5 + 0.5);
    s.height = 0.5 - scratches * 0.2;
    s.albedo = lerp(gColorA.rgb, gColorC.rgb, max(scratches, wear * 0.3));
    s.rough = 0.35 + wear * 0.3;
    s.ao = 1;
    return s;
}

Surf genBrushed(float2 uv) {
    Surf s;
    float streak = tvalue(float2(uv.x * 4.0, uv.y * 1200.0), 4) * 0.6 + tvalue(float2(uv.x * 16.0, uv.y * 600.0), 16) * 0.4;
    s.height = 0.5 + streak * 0.05;
    s.albedo = gColorA.rgb * (0.9 + streak * 0.15);
    s.rough = 0.3 + streak * 0.15;
    s.ao = 1;
    return s;
}

Surf genPavers(float2 uv) {
    Surf s;
    // herringbone-ish: alternate horizontal/vertical pavers in 2x1 blocks
    float2 p = uv * 12.0;
    float2 blk = floor(p / 2.0);
    bool flip = fmod(blk.x + blk.y, 2.0) >= 1.0;
    float2 q = flip ? p.yx : p;
    float2 f = frac(q * float2(0.5, 1.0));
    float2 cell = floor(q * float2(0.5, 1.0));
    float id = hashP(cell + (flip ? 50.0 : 0.0), 24.0);
    float edge = min(min(f.x, 1.0 - f.x) * 2.0, min(f.y, 1.0 - f.y));
    float isP = smoothstep(0.03, 0.09, edge);
    float fine = tvalue(uv * 400, 400);
    float3 c = lerp(gColorA.rgb, gColorB.rgb, id) * (0.9 + fine * 0.15);
    s.albedo = lerp(gColorC.rgb, c, isP);
    s.height = isP * 0.8 + fine * 0.1;
    s.rough = lerp(0.95, 0.75, isP);
    s.ao = lerp(0.5, 1.0, isP);
    return s;
}

Surf genMarble(float2 uv) {
    Surf s;
    float v = tfbm(uv + tfbm(uv, 3, 5, 0.6) * 0.6, 2, 5, 0.6);
    float vein = smoothstep(0.03, 0.0, abs(frac(v * 4.0) - 0.5) - 0.46);
    float2 tile = frac(uv * 2.0);
    float grout = smoothstep(0.004, 0.01, min(min(tile.x, 1.0 - tile.x), min(tile.y, 1.0 - tile.y)));
    s.albedo = lerp(gColorA.rgb, gColorB.rgb, vein * 0.8) * lerp(0.6, 1.0, grout);
    s.height = 0.5 * grout;
    s.rough = lerp(0.3, 0.15, grout);
    s.ao = 1;
    return s;
}

Surf genStone(float2 uv) {
    Surf s;
    float3 w = tworley(uv * float2(1.0, 1.6), 6);
    float edge = smoothstep(0.02, 0.12, w.y - w.x);
    float detail = tfbm(uv, 12, 4, 0.55) * 0.5 + 0.5;
    float3 c = lerp(gColorA.rgb, gColorB.rgb, w.z) * (0.85 + detail * 0.3);
    s.albedo = lerp(gColorC.rgb, c, edge);
    s.height = edge * (0.7 + detail * 0.3);
    s.rough = 0.85;
    s.ao = lerp(0.45, 1.0, edge);
    return s;
}

// Anisotropic tileable value noise (period P.x x P.y lattice cells)
float hashP2(float2 i, float2 P) { i = i - floor(i / P) * P; return (hash2u(asuint(int2(i)) + gSeed * 7919u) >> 8) * (1.0 / 16777216.0); }
float tvalue2(float2 p, float2 P) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hashP2(i, P), hashP2(i + float2(1, 0), P), u.x), lerp(hashP2(i + float2(0, 1), P), hashP2(i + float2(1, 1), P), u.x), u.y);
}

// Fabric: clothing, denim, upholstery, carpet, awning canvas. No periodic yarn-scale weave in the texture: a
// millimetre weave in a tiling texture reaches Nyquist in the box-filtered mips and turns into moire stripes across
// shirts and "pleated" collars. The weave / knit relief is added analytically by the object shaders, faded by the
// pixel footprint. The texture holds what survives filtering: heathered yarn tone (fine noise, longer along the
// warp), dye and wear variation at the centimetre scale, and optional wide stripes (gParams.x, awnings).
// gParams.y: 0 cloth, 1 denim (indigo warp streaks with pale weft flecks), 2 pile (carpet).
Surf genFabric(float2 uv) {
    Surf s;
    float style = gParams.y;
    float heather = tvalue2(uv * float2(150.0, 42.0), float2(150.0, 42.0)) * 0.6 + tvalue2(uv * float2(300.0, 84.0), float2(300.0, 84.0)) * 0.4;
    float wear = tfbm(uv, 4.0, 3, 0.5) * 0.5 + 0.5;
    float stripes = step(0.5, frac(uv.x * 8.0));
    float3 base = lerp(gColorA.rgb, gColorB.rgb, stripes * gParams.x);
    float tone = 0.94 + heather * 0.1 + (wear - 0.5) * 0.08;
    float h = 0.5 + (heather - 0.5) * 0.35;
    float rough = 0.95;
    if (style > 0.5 && style < 1.5) {
        // denim: rope-dyed warp gives vertical streaks a few mm wide and several cm long; the undyed weft shows as
        // pale flecks; wear lightens the high spots
        float streak = tvalue2(uv * float2(96.0, 5.0), float2(96.0, 5.0)) * 0.7 + tvalue2(uv * float2(190.0, 11.0), float2(190.0, 11.0)) * 0.3;
        float fleck = smoothstep(0.72, 0.92, tvalue2(uv * float2(260.0, 150.0), float2(260.0, 150.0)));
        tone = 0.86 + streak * 0.26 + (wear - 0.5) * 0.12;
        base = lerp(base, gColorC.rgb * 2.2, fleck * 0.18);
        h = 0.5 + (streak - 0.5) * 0.3 + fleck * 0.1;
        rough = 0.88;
    } else if (style > 1.5) {
        // pile: isotropic tufts
        float tuft = tvalue2(uv * 180.0, float2(180.0, 180.0));
        tone = 0.9 + tuft * 0.16 + (wear - 0.5) * 0.1;
        h = tuft;
    }
    s.albedo = base * tone;
    s.height = h;
    s.rough = rough;
    s.ao = 0.9 + h * 0.1;
    return s;
}

Surf genPlanks(float2 uv) {
    Surf s;
    float boards = 8.0;
    float fy = frac(uv.y * boards);
    float row = floor(uv.y * boards);
    float fx = frac(uv.x * 2.0 + hashP(float2(row, 3), boards) );
    float id = hashP(float2(row, floor(uv.x * 2.0 + hashP(float2(row, 3), boards))), boards * 2.0);
    float grain = tvalue(float2(uv.x * 30.0, uv.y * 900.0), 30) * 0.6 + tfbm(uv * float2(1, 8), 6, 3, 0.5) * 0.4;
    float gap = smoothstep(0.0, 0.05, min(fy, 1.0 - fy)) * smoothstep(0.0, 0.01, min(fx, 1.0 - fx));
    float3 c = lerp(gColorA.rgb, gColorB.rgb, id) * (0.8 + grain * 0.3);
    s.albedo = c * lerp(0.3, 1.0, gap);
    s.height = gap * (0.7 + grain * 0.2);
    s.rough = 0.8;
    s.ao = lerp(0.4, 1.0, gap);
    return s;
}

Surf genPoolTile(float2 uv) {
    Surf s;
    float2 f = frac(uv * 32.0);
    float2 cell = floor(uv * 32.0);
    float id = hashP(cell, 32.0);
    float grout = smoothstep(0.04, 0.1, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)));
    float3 c = lerp(gColorA.rgb, gColorB.rgb, id);
    s.albedo = lerp(gColorC.rgb, c, grout);
    s.height = grout * 0.6;
    s.rough = lerp(0.8, 0.1, grout);
    s.ao = 1;
    return s;
}

Surf genPanel(float2 uv) {
    Surf s = genConcrete(uv);
    float2 f = frac(uv * float2(2.0, 1.0));
    float joint = smoothstep(0.004, 0.012, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)));
    float bolts = 0;
    float2 bp = frac(uv * float2(8.0, 4.0)) - 0.5;
    bolts = smoothstep(0.06, 0.03, length(bp)) * 0.5;
    s.albedo *= lerp(0.45, 1.0, joint) * (1.0 - bolts * 0.4);
    s.height = s.height * joint;
    s.ao = min(s.ao, lerp(0.5, 1.0, joint));
    return s;
}

Surf genPaint(float2 uv) {
    Surf s;
    float wear = smoothstep(0.35, 0.75, tfbm(uv, 6, 5, 0.6) * 0.5 + 0.5);
    float grit = tvalue(uv * 500, 500);
    s.albedo = lerp(gColorA.rgb, gColorB.rgb, wear * 0.8) * (0.9 + grit * 0.15);
    s.height = 0.5 + (1.0 - wear) * 0.2;
    s.rough = lerp(0.55, 0.85, wear);
    s.ao = 1;
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
#elif GEN == 6
    return genGravel(uv);
#elif GEN == 7
    return genAsphalt(uv);
#elif GEN == 8
    return genConcrete(uv);
#elif GEN == 9
    return genSidewalk(uv);
#elif GEN == 10
    return genBrick(uv);
#elif GEN == 11
    return genStucco(uv);
#elif GEN == 12
    return genPlaster(uv);
#elif GEN == 13
    return genSiding(uv);
#elif GEN == 14
    return genRoofTile(uv);
#elif GEN == 15
    return genShingle(uv);
#elif GEN == 16
    return genMetalRoof(uv);
#elif GEN == 17
    return genGlass(uv);
#elif GEN == 18
    return genPaintedMetal(uv);
#elif GEN == 19
    return genBrushed(uv);
#elif GEN == 20
    return genPavers(uv);
#elif GEN == 21
    return genMarble(uv);
#elif GEN == 22
    return genStone(uv);
#elif GEN == 23
    return genFabric(uv);
#elif GEN == 24
    return genPlanks(uv);
#elif GEN == 25
    return genPoolTile(uv);
#elif GEN == 26
    return genPanel(uv);
#else
    return genPaint(uv);
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
