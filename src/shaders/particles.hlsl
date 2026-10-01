// GPU particles: procedural texture atlas, simulation (gravity, drag, wind, buoyancy, ground + depth-buffer
// collisions), bitonic depth sort and lit soft billboards (premultiplied alpha: alpha-blended and additive in
// one sorted draw).
#include "gbuffer.hlsli"
#include "skycommon.hlsli"
#include "shadow.hlsli"
#include "lights.hlsli"

// ------------------------------------------------------------------------------------------------
// Data
struct Particle {
    float3 pos;       // relative to the simulation origin (world meters)
    float age;
    float3 vel;
    float life;       // <= 0: dead
    float size0, size1, rot, rotVel;
    uint typeCell;    // bits 0-7 type, 8-15 atlas cell, 16-31 flags
    uint color;       // RGBA8 tint (a = opacity scale)
    float seed;
    float intensity;  // emissive intensity (candela-ish, not exposed) for emissive types
};

struct TypeInfo {
    float4 motion;    // x gravity scale, y drag (1/s), z buoyancy (m/s^2), w wind follow (0..1)
    float4 color0;    // rgb multiplier at birth, a opacity at birth
    float4 color1;    // rgb multiplier at death, a opacity at death
    float4 render;    // x emissive (1) / lit (0), y softness (m), z velocity stretch (s), w additive (1 = pure additive)
    float4 atlas;     // x first cell, y cell count, z animated over life (1) / random variant (0), w fade-in fraction
    float4 extra;     // x collide restitution (<0: no collision), y flutter, z size curve exponent, w normal-mapped (1)
};

cbuffer ParticleCB : register(b1) {
    float4 gPSim0;     // x dt, y spawn count, z pool size, w time
    float4 gPSim1;     // xyz simulation origin relative to the camera, w unused
    float4 gPSim2;     // xyz simulation origin (world, float), w re-base shift flag
    float4 gPSim3;     // xyz position shift to apply this frame (re-base), w wind speed (m/s)
    float4 gPSort;     // x k, y j, z sort count, w unused
    float4 gPLightCount; // x number of particle lights
    float4 gPLights[32]; // triples: (pos rel camera, radius), (intensity, outer cone cos), (spot dir, inner cone cos)
};

StructuredBuffer<TypeInfo> tTypes : register(t0);
StructuredBuffer<Particle> tSpawn : register(t1);
StructuredBuffer<uint> tSpawnSlot : register(t2);
Texture2D<float> tDepth : register(t3);
Texture2D<float2> tNormalG : register(t4);
StructuredBuffer<Particle> tParticles : register(t5);
StructuredBuffer<uint2> tSorted : register(t6);
Texture2D<float4> tAtlas : register(t7);
RWStructuredBuffer<Particle> uParticles : register(u0);
RWStructuredBuffer<uint2> uKeys : register(u1);
RWTexture2D<float4> uAtlas : register(u2);

float4 unpackColor8(uint c) { return float4(c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255) / 255.0; }

// ------------------------------------------------------------------------------------------------
// Atlas generation: 8x8 cells of 128x128. Lit cells store a tangent-space normal in rgb (0.5 + 0.5 n) and
// opacity in a; emissive cells store color in rgb and opacity in a.
float2 cellUV(uint2 pix, out uint cell) {
    uint2 c = pix / 128u;
    cell = c.y * 8u + c.x;
    return ((pix % 128u) + 0.5) / 128.0 * 2.0 - 1.0;   // [-1, 1]
}

float puffHeight(float2 p, uint seed, float noiseAmt) {
    float h = 0;
    [loop] for (int i = 0; i < 7; i++) {
        uint hs = seed * 7919u + (uint)i * 104729u;
        float2 c = (float2(hashF(hs), hashF(hs + 1u)) - 0.5) * 0.9;
        float r = 0.32 + hashF(hs + 2u) * 0.3;
        float d2 = dot(p - c, p - c);
        h = max(h, sqrt(max(r * r - d2, 0.0)));
    }
    float n = fbm(p * 3.0 + seed * 13.1, 4);
    return max(h + n * noiseAmt, 0.0);
}

float4 genPuff(float2 p, uint seed, float noiseAmt, float edge) {
    float e = 2.0 / 128.0;
    float h = puffHeight(p, seed, noiseAmt);
    float hx = puffHeight(p + float2(e, 0), seed, noiseAmt) - puffHeight(p - float2(e, 0), seed, noiseAmt);
    float hy = puffHeight(p + float2(0, e), seed, noiseAmt) - puffHeight(p - float2(0, e), seed, noiseAmt);
    float3 n = normalize(float3(-hx / (2 * e), hy / (2 * e), 1.2));
    float a = smoothstep(0.0, edge, h) * saturate(1.0 - dot(p, p) * 0.6);
    return float4(n * 0.5 + 0.5, a);
}

float4 genFlame(float2 p, float frame) {
    // teardrop: wide at the bottom, tapering upward; turbulent noise scrolling up
    float y = -p.y;  // up
    float width = 0.55 * saturate(1.0 - (y + 0.7) / 1.6) + 0.05;
    float n = fbm(float2(p.x * 2.5, y * 1.8 - frame * 0.9) + frame * 3.1, 4);
    float x = p.x + n * 0.25 * (y + 1.0);
    float shape = saturate(1.0 - abs(x) / width) * smoothstep(-0.95, -0.6, y) * smoothstep(1.0, 0.2, y);
    float heat = saturate(shape * 1.6 + n * 0.3 - (y + 1.0) * 0.25);
    float3 col = lerp(float3(0.6, 0.08, 0.01), float3(1.0, 0.45, 0.08), saturate(heat * 1.5));
    col = lerp(col, float3(1.0, 0.9, 0.6), saturate(heat * 2.0 - 1.0));
    return float4(col, saturate(shape * 1.4));
}

float4 genFireball(float2 p, float frame) {
    // billowy fire ball, cooling to dark smoke over the frames
    float h = puffHeight(p * (1.0 + frame * 0.04), 91u + (uint)frame, 0.35);
    float a = smoothstep(0.0, 0.15, h) * saturate(1.0 - dot(p, p) * 0.5);
    float cool = frame / 7.0;
    float heat = saturate(h * 1.8 - cool * 0.9 + fbm(p * 4.0 + frame, 3) * 0.3);
    float3 col = lerp(float3(0.08, 0.05, 0.04), float3(0.9, 0.25, 0.03), saturate(heat * 2.0));
    col = lerp(col, float3(1.0, 0.8, 0.45), saturate(heat * 2.0 - 1.0));
    return float4(col, a);
}

float4 genGlow(float2 p, float sharp) {
    float d = length(p);
    float g = exp(-d * d * sharp) + exp(-d * 6.0) * 0.25;
    float3 col = lerp(float3(1.0, 0.55, 0.15), float3(1.0, 0.95, 0.8), exp(-d * d * sharp * 4.0));
    return float4(col, saturate(g));
}

float4 genStar(float2 p) {
    float d = length(p);
    float ang = atan2(p.y, p.x);
    float spikes = pow(abs(cos(ang * 3.0)), 18.0) * exp(-d * 2.2) + pow(abs(cos(ang * 3.0 + 0.52)), 30.0) * exp(-d * 3.5) * 0.6;
    float core = exp(-d * d * 22.0);
    float v = saturate(spikes + core + exp(-d * 4.0) * 0.3);
    float3 col = lerp(float3(1.0, 0.5, 0.12), float3(1.0, 0.92, 0.7), saturate(core * 2.0 + spikes));
    return float4(col, v);
}

float4 genSideFlash(float2 p) {
    // elongated flame along +x
    float x = p.x * 0.5 + 0.5;
    float w = 0.35 * (1.0 - x) * (0.7 + 0.3 * sin(x * 20.0));
    float v = saturate(1.0 - abs(p.y) / max(w, 1e-3)) * smoothstep(0.0, 0.1, x) * smoothstep(1.0, 0.7, x);
    float3 col = lerp(float3(1.0, 0.45, 0.1), float3(1.0, 0.9, 0.65), saturate(v * 1.5 - x));
    return float4(col, v);
}

float polygonMask(float2 p, uint seed, int sides, float jag, out float2 facet) {
    float ang = atan2(p.y, p.x);
    float sector = floor((ang + PI) / TWO_PI * sides);
    float r0 = 0.55 + hashF(seed + (uint)sector) * jag;
    float r1 = 0.55 + hashF(seed + (uint)((sector + 1) % sides)) * jag;
    float t = frac((ang + PI) / TWO_PI * sides);
    float r = lerp(r0, r1, t);
    facet = float2(hashF(seed * 3u + (uint)sector), hashF(seed * 5u + (uint)sector)) - 0.5;
    return saturate((r - length(p)) * 40.0);
}

float4 genDebris(float2 p, uint seed) {
    float2 facet;
    float m = polygonMask(p, seed, 7, 0.4, facet);
    float3 n = normalize(float3(facet * 1.4 + p * 0.3, 1.0));
    return float4(n * 0.5 + 0.5, m);
}

float4 genShard(float2 p, uint seed) {
    // thin triangle sliver
    float2 a = (float2(hashF(seed), hashF(seed + 1u)) - 0.5) * 1.6;
    float2 b = (float2(hashF(seed + 2u), hashF(seed + 3u)) - 0.5) * 1.6;
    float2 c = (float2(hashF(seed + 4u), hashF(seed + 5u)) - 0.5) * 0.6;
    float2 e0 = b - a, e1 = c - b, e2 = a - c;
    float s0 = e0.x * (p.y - a.y) - e0.y * (p.x - a.x);
    float s1 = e1.x * (p.y - b.y) - e1.y * (p.x - b.x);
    float s2 = e2.x * (p.y - c.y) - e2.y * (p.x - c.x);
    bool inside = (s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0);
    float3 n = normalize(float3(hashF(seed + 6u) - 0.5, hashF(seed + 7u) - 0.5, 1.0));
    return float4(n * 0.5 + 0.5, inside ? 1.0 : 0.0);
}

float4 genLeaf(float2 p, uint seed) {
    float a = hashF(seed) * 0.6 - 0.3;
    float2 q = float2(cos(a) * p.x + sin(a) * p.y, -sin(a) * p.x + cos(a) * p.y);
    float t = saturate(q.x * 0.5 + 0.5);
    float w = 0.42 * sin(t * PI) * (1.0 - t * 0.3);
    float inside = step(abs(q.y), w) * step(-0.95, q.x) * step(q.x, 0.95);
    float stem = step(abs(q.y), 0.02) * step(-1.0, q.x) * step(q.x, -0.8);
    float3 n = normalize(float3(0, -q.y * 1.2, 1.0));   // folded along the midrib
    return float4(n * 0.5 + 0.5, max(inside, stem));
}

float4 genDroplets(float2 p, uint seed, int count, float rmax) {
    float a = 0;
    float3 n = float3(0, 0, 1);
    [loop] for (int i = 0; i < count; i++) {
        uint hs = seed * 131u + (uint)i * 7477u;
        float2 c = (float2(hashF(hs), hashF(hs + 1u)) - 0.5) * 1.5;
        float r = rmax * (0.3 + hashF(hs + 2u) * 0.7);
        float2 d = (p - c) / r;
        float dd = dot(d, d);
        if (dd < 1.0) { a = 1.0; n = float3(d, sqrt(1.0 - dd)); }
    }
    return float4(n * 0.5 + 0.5, a);
}

float4 genSplashCrown(float2 p) {
    float y = -p.y;
    float ring = exp(-sq((length(float2(p.x, (y + 0.6) * 2.2)) - 0.55) * 9.0)) * step(-0.75, y) * step(y, -0.3);
    float drops = 0;
    [unroll] for (int i = 0; i < 6; i++) {
        float x = -0.6 + i * 0.24;
        float2 c = float2(x, -(0.1 + 0.4 * (1.0 - abs(x))));
        drops = max(drops, exp(-dot(p - c, p - c) * 180.0));
    }
    float v = saturate(ring + drops);
    return float4(0.85, 0.9, 1.0, v);
}

float4 genRing(float2 p) {
    float d = length(p);
    float v = exp(-sq((d - 0.8) * 14.0)) * (0.6 + 0.4 * fbm(p * 6.0, 2));
    return float4(1.0, 0.85, 0.6, saturate(v));
}

float4 genTracer(float2 p) {
    float v = exp(-p.y * p.y * 60.0) * smoothstep(1.0, 0.6, abs(p.x));
    float core = exp(-p.y * p.y * 400.0);
    return float4(lerp(float3(1.0, 0.7, 0.3), float3(1.0, 0.97, 0.85), core), saturate(v + core));
}

[numthreads(8, 8, 1)]
void csGenAtlas(uint3 id : SV_DispatchThreadID) {
    uint cell;
    float2 p = cellUV(id.xy, cell);
    float4 o = 0;
    if (cell < 8u) o = genPuff(p, cell + 1u, 0.35, 0.25);                      // smoke puffs
    else if (cell < 16u) o = genFlame(p, (float)(cell - 8u));                  // flame frames
    else if (cell == 16u) o = genGlow(p, 30.0);                                // spark / ember dot
    else if (cell == 17u) o = genStar(p);                                      // muzzle flash star
    else if (cell == 18u) o = genSideFlash(p);                                 // muzzle side flash
    else if (cell == 19u) o = genGlow(p, 4.0);                                 // soft glow
    else if (cell < 24u) o = genDroplets(p, cell * 17u, 9, 0.22);              // blood droplets
    else if (cell < 28u) o = genDebris(p, cell * 29u);                         // debris chunks
    else if (cell < 32u) o = genShard(p, cell * 31u);                          // glass shards
    else if (cell < 36u) o = genLeaf(p, cell * 37u);                           // leaves
    else if (cell < 40u) o = genDroplets(p, cell * 41u, 14, 0.12);             // water spray
    else if (cell < 44u) o = genPuff(p, cell * 43u, 0.6, 0.45);                // dust (softer, grainier)
    else if (cell == 44u) o = genSplashCrown(p);                               // rain splash crown
    else if (cell == 45u) o = genRing(p);                                      // shock ring
    else if (cell == 46u) o = genTracer(p);                                    // tracer streak
    else if (cell == 47u) o = genGlow(p, 12.0);                                // ember
    else if (cell < 56u) o = genFireball(p, (float)(cell - 48u));              // explosion fireball frames
    // premultiply-friendly: keep rgb meaningful where alpha is 0 (bilinear filtering at edges)
    uAtlas[id.xy] = float4(saturate(o.rgb), saturate(o.a));
}

// ------------------------------------------------------------------------------------------------
// Simulation
[numthreads(64, 1, 1)]
void csEmit(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gPSim0.y) return;
    uParticles[tSpawnSlot[id.x]] = tSpawn[id.x];
}

float groundHeight(float2 worldXY) {
    float2 tuv = (worldXY + 10240.0) / 20480.0;
    float h = gTerrainHeightG.SampleLevel(sLinearClamp, tuv, 0);
    if (gOverhead.w > 0.5) {
        float2 ouv = overheadUV(worldXY);
        if (all(ouv > 0.0) && all(ouv < 1.0)) h = max(h, gOverheadMap.SampleLevel(sPointClamp, ouv, 0));
    }
    return h;
}

[numthreads(64, 1, 1)]
void csSimulate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gPSim0.z) return;
    Particle p = uParticles[id.x];
    if (p.life <= 0.0) return;
    float dt = gPSim0.x;
    p.pos += gPSim3.xyz;  // simulation origin re-base
    p.age += dt;
    if (p.age >= p.life) { p.life = 0.0; uParticles[id.x] = p; return; }
    TypeInfo t = tTypes[p.typeCell & 255u];
    float3 wind = float3(gWind.xy, 0) * gPSim3.w;
    float3 acc = float3(0, 0, -9.81 * t.motion.x + t.motion.z);
    if (t.extra.y > 0.0) {
        // leaves: flutter and slow tumbling fall
        float ph = p.age * 4.3 + p.seed * 40.0;
        acc.xy += float2(sin(ph), cos(ph * 0.7)) * t.extra.y;
    }
    p.vel += acc * dt;
    // drag towards the wind velocity (smoke drifts with the wind, sparks mostly ballistic)
    float3 target = wind * t.motion.w;
    p.vel = target + (p.vel - target) * exp(-t.motion.y * dt);
    float3 np = p.pos + p.vel * dt;
    if (t.extra.x >= 0.0) {
        float3 wp = np + gPSim2.xyz;
        float gh = groundHeight(wp.xy);
        if (wp.z < gh + 0.01) {
            np.z = gh + 0.01 - gPSim2.z;
            if (p.vel.z < 0.0) p.vel.z = -p.vel.z * t.extra.x;
            p.vel.xy *= 0.6;
            p.rotVel *= 0.5;
            if (length(p.vel) < 0.35) { p.vel = 0; p.rotVel = 0; }
        } else {
            // walls and objects: bounce off the depth buffer where the particle is on screen
            float3 rel = np + gPSim1.xyz;
            float4 clip = mul(gViewProjNoJitter, float4(rel, 1));
            if (clip.w > 0.1) {
                float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
                if (all(uv > 0.0) && all(uv < 1.0)) {
                    int2 px = int2(uv * gScreen.xy);
                    float d = tDepth[px];
                    float sz = d > 0.0 ? linearDepth(d) : 1e6;
                    float pz = clip.w;
                    if (pz > sz + 0.02 && pz < sz + 0.6) {
                        float3 n = octDecode(tNormalG[px] * 2.0 - 1.0);
                        if (dot(p.vel, n) < 0.0) {
                            p.vel = reflect(p.vel, n) * t.extra.x;
                            np = p.pos;
                        }
                    }
                }
            }
        }
    }
    p.pos = np;
    p.rot += p.rotVel * dt;
    uParticles[id.x] = p;
}

// Sort keys: back to front (descending view depth); dead or culled particles sort last.
[numthreads(64, 1, 1)]
void csKeys(uint3 id : SV_DispatchThreadID) {
    uint n = (uint)gPSort.z;
    if (id.x >= n) return;
    uint key = 0xffffffffu;
    if (id.x < (uint)gPSim0.z) {
        Particle p = tParticles[id.x];
        if (p.life > 0.0) {
            float3 rel = p.pos + gPSim1.xyz;
            float z = dot(rel, gCamForward.xyz);
            float r = max(p.size0, p.size1) * 3.0 + 1.0;
            if (z > -r) key = ~asuint(max(z, 0.0) + 1.0);   // larger depth -> smaller key
        }
    }
    uKeys[id.x] = uint2(key, id.x);
}

// Bitonic sort (ascending by key). Local: 2048 elements per group in shared memory.
groupshared uint2 gsSort[2048];
void compareSwapShared(uint i, uint l, bool asc) {
    uint2 a = gsSort[i], b = gsSort[l];
    if ((a.x > b.x) == asc) { gsSort[i] = b; gsSort[l] = a; }
}

[numthreads(1024, 1, 1)]
void csSortLocal(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
    uint base = gid.x * 2048u;
    gsSort[gi] = uKeys[base + gi];
    gsSort[gi + 1024u] = uKeys[base + gi + 1024u];
    GroupMemoryBarrierWithGroupSync();
    for (uint k = 2u; k <= 2048u; k <<= 1u) {
        for (uint j = k >> 1u; j > 0u; j >>= 1u) {
            uint i = 2u * gi - (gi & (j - 1u));
            bool asc = ((base + i) & k) == 0u;
            compareSwapShared(i, i + j, asc);
            GroupMemoryBarrierWithGroupSync();
        }
    }
    uKeys[base + gi] = gsSort[gi];
    uKeys[base + gi + 1024u] = gsSort[gi + 1024u];
}

// Global merge step for strides j >= 2048 of stage k.
[numthreads(256, 1, 1)]
void csSortGlobal(uint3 id : SV_DispatchThreadID) {
    uint k = (uint)gPSort.x, j = (uint)gPSort.y;
    uint t = id.x;
    uint i = 2u * t - (t & (j - 1u));
    uint l = i + j;
    if (l >= (uint)gPSort.z) return;
    bool asc = (i & k) == 0u;
    uint2 a = uKeys[i], b = uKeys[l];
    if ((a.x > b.x) == asc) { uKeys[i] = b; uKeys[l] = a; }
}

// Finishes stage k for strides 1024..1 inside each 2048-element block.
[numthreads(1024, 1, 1)]
void csSortMerge(uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
    uint k = (uint)gPSort.x;
    uint base = gid.x * 2048u;
    gsSort[gi] = uKeys[base + gi];
    gsSort[gi + 1024u] = uKeys[base + gi + 1024u];
    GroupMemoryBarrierWithGroupSync();
    for (uint j = 1024u; j > 0u; j >>= 1u) {
        uint i = 2u * gi - (gi & (j - 1u));
        bool asc = ((base + i) & k) == 0u;
        compareSwapShared(i, i + j, asc);
        GroupMemoryBarrierWithGroupSync();
    }
    uKeys[base + gi] = gsSort[gi];
    uKeys[base + gi + 1024u] = gsSort[gi + 1024u];
}

// ------------------------------------------------------------------------------------------------
// Rendering
struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;          // atlas uv
    float2 uv2 : TEXCOORD1;         // next animation frame (animated types)
    nointerpolation float4 color : COLOR0;       // rgb tint * color curve, a opacity
    float4 light : COLOR1;                       // rgb ambient + local light radiance (not exposed, per corner), a frame blend
    nointerpolation float4 sun : COLOR2;         // rgb sun illuminance * shadow, a view depth
    nointerpolation float4 info : TEXCOORD2;     // x emissive, y softness, z additive, w normal-mapped
    float3 basisR : TEXCOORD3;
    float3 basisU : TEXCOORD4;
    nointerpolation float4 fog : TEXCOORD5;      // rgb in-scatter (pre-exposed), a transmittance
    float nearFade : TEXCOORD6;                  // emissive streaks passing right by the camera fade out
};

float2 atlasUV(uint cell, float2 corner) {
    return (float2(cell % 8u, cell / 8u) + corner) / 8.0;
}

VSOut vsParticle(uint vid : SV_VertexID, uint inst : SV_InstanceID) {
    VSOut o = (VSOut)0;
    uint idx = tSorted[inst].y;
    Particle p = tParticles[idx];
    if (p.life <= 0.0) { o.pos = float4(0, 0, -2, 1); return o; }
    TypeInfo t = tTypes[p.typeCell & 255u];
    float x = saturate(p.age / p.life);
    float size = lerp(p.size0, p.size1, pow(x, t.extra.z));
    float3 center = p.pos + gPSim1.xyz;
    float3 V = normalize(-center);
    float3 camR = gView[0].xyz, camU = gView[1].xyz;
    float2 corner = float2(vid & 1u, vid >> 1u);          // strip order: (0,0) (1,0) (0,1) (1,1)
    float2 q = corner * 2.0 - 1.0;
    float3 R, U;
    float stretch = t.render.z;
    float speed = length(p.vel);
    o.nearFade = 1.0;
    if (stretch != 0.0 && speed > 0.5) {
        // velocity-aligned: long axis along the motion; stretch > 0 trails behind the head (sparks, tracers),
        // stretch < 0 extends forward from the emitter (muzzle side flash)
        float3 vdir = p.vel / speed;
        float3 side = normalize(cross(vdir, V) + 1e-5);
        float len = size + speed * abs(stretch);
        R = vdir * len * 0.5;
        center += vdir * len * (stretch < 0.0 ? 0.25 : -0.25);
        float halfW = size * 0.5;
        if (t.render.x > 0.5) {
            // emissive streaks (tracers, sparks) stay thin lines: at most ~2 pixels wide at each end, and a streak
            // passing right by the camera fades out instead of sweeping across the screen as a wide hot band
            float endDist = max(length(center + R * q.x), 0.05);
            float pixelAngle = 2.0 * tan(gCamForward.w * 0.5) * gScreen.w;
            halfW = min(halfW, endDist * pixelAngle);
            if (len > 3.0) o.nearFade = saturate((endDist - 1.5) / 4.0);   // long streaks only (tracers)
        }
        U = side * halfW;
    } else {
        float s, c;
        sincos(p.rot, s, c);
        R = (camR * c + camU * s) * size * 0.5;
        U = (-camR * s + camU * c) * size * 0.5;
    }
    float3 pos = center + R * q.x + U * q.y;
    o.pos = mul(gViewProj, float4(pos, 1));
    uint cell = (p.typeCell >> 8) & 255u;
    float frame = 0;
    if (t.atlas.z > 0.5) {
        float f = x * (t.atlas.y - 1.0);
        frame = frac(f);
        cell = (uint)t.atlas.x + (uint)floor(f);
        o.uv2 = atlasUV(min(cell + 1u, (uint)(t.atlas.x + t.atlas.y - 1.0)), float2(corner.x, 1.0 - corner.y));
    }
    o.uv = atlasUV(cell, float2(corner.x, 1.0 - corner.y));
    float4 tint = unpackColor8(p.color);
    float fadeIn = t.atlas.w > 0.0 ? saturate(x / t.atlas.w) : 1.0;
    float4 curve = lerp(t.color0, t.color1, x);
    o.color = float4(tint.rgb * curve.rgb, tint.a * curve.a * fadeIn);
    o.info = float4(t.render.x > 0.5 ? p.intensity : 0.0, t.render.y, t.render.w, t.extra.w);
    o.basisR = normalize(R);
    o.basisU = normalize(U);
    // Lighting (per particle): sun with shadow, sky ambient, nearby local lights
    float viewDepth = dot(center, gCamForward.xyz);
    float3 sunE = mainLightIlluminance();
    float sh = sampleSunShadowGeo(center, float3(0, 0, 1), viewDepth, uint2(inst, idx)) * cloudShadowAt(center);
    o.sun = float4(sunE * sh, viewDepth);
    float3 amb = (evalSH9(float3(0, 0, 1)) + evalSH9(V)) * 0.5;
    // Local lights at this corner of the billboard (a puff next to a lamp gets a gradient across it, not one flat
    // level), through the spot cone or headlight beam. A puff is a volume: what it scatters is spread over its
    // radius (softened falloff), and of a lamp close to or inside it only the beam lights it (the cone's share of
    // all directions) - a smoking engine by a landing light glows, it does not turn into a white disc.
    float3 local = 0;
    if (t.render.x < 0.5) {
        float rad2 = sq(size * 0.5);
        int nl = (int)gPLightCount.x;
        [loop] for (int li = 0; li < nl; li++) {
            float4 l0 = gPLights[li * 3], l1 = gPLights[li * 3 + 1], l2 = gPLights[li * 3 + 2];
            LightGPU L;
            L.pos = l0.xyz;
            L.radius = l0.w;
            L.color = l1.rgb;
            L.spotCos = l1.w;
            L.dir = l2.xyz;
            L.spotInner = l2.w;
            float3 d = L.pos - pos;
            float d2 = dot(d, d);
            if (d2 > L.radius * L.radius) continue;
            float win = saturate(1.0 - sq(d2 / (L.radius * L.radius)));
            float ang = lightAngular(L, d * rsqrt(max(d2, 1e-6)));
            float coneShare = L.spotCos <= -1.0 ? 1.0 : (L.spotInner > 1.5 ? 0.03 : 0.5 - 0.5 * L.spotCos);
            ang = lerp(ang, coneShare, saturate(rad2 / max(d2, 1e-4)));
            local += L.color * (win * ang / max(d2 + 0.5 * rad2, 0.5));
        }
    }
    o.light = float4(amb + local / PI, frame);
    // Fog / aerial perspective
    float4 ap = aerialPerspective(o.pos.xy / o.pos.w * float2(0.5, -0.5) + 0.5, length(center));
    o.fog = float4(ap.rgb * preExposure(), ap.a);
    float4 fv = froxelFog(o.pos.xy / o.pos.w * float2(0.5, -0.5) + 0.5, viewDepth);
    o.fog = float4(o.fog.rgb * fv.a + fv.rgb, o.fog.a * fv.a);
    return o;
}

Texture2D<float> tSceneDepthP : register(t8);

struct ParticleOut {
    float4 color : SV_Target0;
    float reactive : SV_Target1;
};

ParticleOut psParticle(VSOut i) {
    float4 a = tAtlas.Sample(sLinearClamp, i.uv);
    if (i.light.a > 0.0) a = lerp(a, tAtlas.Sample(sLinearClamp, i.uv2), i.light.a);
    float alpha = a.a * i.color.a * i.nearFade;
    // soft particles
    float sd = tSceneDepthP[uint2(i.pos.xy)];
    float sceneZ = sd > 0.0 ? linearDepth(sd) : 1e6;
    float pz = linearDepth(i.pos.z);
    alpha *= saturate((sceneZ - pz) / max(i.info.y, 0.01));
    alpha *= saturate((pz - 0.3) / 0.6);   // fade near the camera
    if (alpha <= 0.002) discard;
    float3 col;
    if (i.info.x > 0.0) {
        // emissive (fire, sparks, flashes, tracers)
        col = a.rgb * i.color.rgb * i.info.x * preExposure();
        col *= 1.0 / (1.0 + luminance(col) / 24.0);   // soft ceiling: hot cores bloom, never white out the frame
        col = col * i.fog.a;
    } else {
        float3 albedo = i.color.rgb;
        float3 lit;
        if (i.info.w > 0.5) {
            float3 nt = a.rgb * 2.0 - 1.0;
            float3 V = normalize(cross(i.basisR, i.basisU));
            float3 N = normalize(i.basisR * nt.x + i.basisU * nt.y + V * nt.z);
            float3 L = gSunDir.xyz;
            float wrap = saturate((dot(N, L) + 0.4) / 1.4);
            // thin media forward scattering towards the camera
            float fwd = pow(saturate(dot(-V, L)), 6.0) * 0.6;
            lit = albedo * (i.sun.rgb * (wrap + fwd) / PI + i.light.rgb);
        } else {
            lit = albedo * (i.sun.rgb * 0.5 / PI + i.light.rgb);
        }
        col = (lit * preExposure()) * i.fog.a + i.fog.rgb;
    }
    // premultiplied output; additive types keep only a fraction of their coverage in alpha
    ParticleOut o;
    o.color = float4(col * alpha, alpha * (1.0 - i.info.z));
    // reactive mask: fast / emissive particles must not be smeared by TAA
    o.reactive = saturate(alpha * (i.info.x > 0.0 ? 2.0 : 0.6));
    return o;
}
