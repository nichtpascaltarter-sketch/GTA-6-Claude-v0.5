// Deferred decals (bullet holes, blood, scorch marks) projected into the G-buffer before lighting, tire skid
// strips, and the procedural decal atlases.
#include "gbuffer.hlsli"

struct DecalInstance {
    float4 center;   // xyz camera-relative center, w atlas cell
    float4 axisX;    // xyz tangent axis * half size, w opacity
    float4 axisY;    // xyz bitangent axis * half size, w glow (hot scorch emissive, nits)
    float4 axisZ;    // xyz normal axis * half depth, w unused
};
StructuredBuffer<DecalInstance> tDecals : register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D<float2> tNormalCopy : register(t2);
Texture2D<float4> tMaterialCopy : register(t3);
Texture2D<float4> tDecalAlbedo : register(t4);   // rgb albedo (linear), a opacity
Texture2D<float4> tDecalNormal : register(t5);   // xy normal, z roughness, w metalness
RWTexture2D<float4> uAlbedoOut : register(u0);
RWTexture2D<float4> uNormalOut : register(u1);

// ------------------------------------------------------------------------------------------------
// Atlas generation: 4x4 cells of 256x256
float craterHeight(float2 p, uint seed, float r) {
    float d = length(p);
    float rim = exp(-sq((d - r) / (r * 0.35))) * 0.35;
    float bowl = -saturate(1.0 - d / r) * 1.2;
    float n = fbm(p * 9.0 + seed * 3.7, 3) * 0.15;
    return bowl + rim + n;
}

[numthreads(8, 8, 1)]
void csGenDecals(uint3 id : SV_DispatchThreadID) {
    uint2 c = id.xy / 256u;
    uint cell = c.y * 4u + c.x;
    float2 p = ((id.xy % 256u) + 0.5) / 256.0 * 2.0 - 1.0;
    uint seed = cell * 7919u + 13u;
    float d = length(p);
    float ang = atan2(p.y, p.x);
    float4 alb = 0;
    float4 nrm = float4(0.5, 0.5, 0.9, 0);
    if (cell < 2u) {
        // bullet hole in concrete: dark crater, chipped rim, radial cracks, dust ring
        float r = 0.16 + cell * 0.04;
        float e = 2.0 / 256.0;
        float h = craterHeight(p, seed, r);
        float hx = craterHeight(p + float2(e, 0), seed, r) - craterHeight(p - float2(e, 0), seed, r);
        float hy = craterHeight(p + float2(0, e), seed, r) - craterHeight(p - float2(0, e), seed, r);
        float3 n = normalize(float3(-hx / (2 * e) * 0.15, hy / (2 * e) * 0.15, 1.0));
        float cracks = 0;
        [unroll] for (int k = 0; k < 5; k++) {
            float ca = hashF(seed + k) * TWO_PI;
            float len = 0.3 + hashF(seed + k + 9u) * 0.4;
            float da = abs(frac((ang - ca) / TWO_PI + 0.5) - 0.5) * TWO_PI;
            cracks = max(cracks, step(da * d, 0.012 * (1.0 - d / len)) * step(d, len) * step(r * 0.8, d));
        }
        float hole = 1.0 - smoothstep(r * 0.55, r * 0.8, d);
        float dust = exp(-sq(d / (r * 2.6))) * 0.5;
        float3 col = lerp(float3(0.62, 0.6, 0.56), float3(0.03, 0.03, 0.03), hole);
        col = lerp(col, float3(0.15, 0.14, 0.13), cracks);
        float a = saturate(max(max(hole, cracks), dust * (0.6 + 0.4 * fbm(p * 12.0, 2))) + smoothstep(r * 1.4, r * 0.9, d));
        alb = float4(col, a * smoothstep(1.0, 0.7, d));
        nrm = float4(n.xy * 0.5 + 0.5, lerp(0.95, 0.7, hole), 0);
    } else if (cell < 4u) {
        // bullet hole in metal: punched hole, bright deformed rim (bare metal), paint chips
        float r = 0.1 + (cell - 2u) * 0.03;
        float hole = 1.0 - smoothstep(r * 0.8, r, d);
        float rim = smoothstep(r * 0.8, r, d) * (1.0 - smoothstep(r * 1.2, r * 2.0 + 0.1 * fbm(p * 20.0, 2), d));
        float3 n = normalize(float3(p * (rim * 3.0) / max(d, 1e-3), 1.0));
        float3 col = lerp(float3(0.62, 0.62, 0.64), float3(0.02, 0.02, 0.02), hole);
        alb = float4(col, saturate(hole + rim));
        nrm = float4(n.xy * 0.5 + 0.5, lerp(0.25, 0.9, hole), rim);
    } else if (cell < 6u) {
        // bullet hole in glass: small hole, radial cracks and concentric fracture rings
        float r = 0.05;
        float hole = 1.0 - smoothstep(r * 0.7, r, d);
        float radial = 0;
        [loop] for (int k = 0; k < 14; k++) {
            float ca = hashF(seed + k) * TWO_PI;
            float len = 0.5 + hashF(seed + k + 31u) * 0.5;
            float da = abs(frac((ang - ca) / TWO_PI + 0.5) - 0.5) * TWO_PI;
            radial = max(radial, step(da * d, 0.006) * step(d, len));
        }
        float rings = 0;
        [unroll] for (int k2 = 1; k2 <= 3; k2++) {
            float rr = 0.12 * k2 + fbm(float2(ang * 3.0, k2), 2) * 0.04;
            rings = max(rings, exp(-sq((d - rr) * 90.0)) * step(0.3, frac(ang * 2.3 + k2 * 0.37)));
        }
        float frost = exp(-sq(d / 0.12)) * 0.6;
        float a = saturate(max(max(hole, radial * 0.9), max(rings * 0.8, frost)));
        alb = float4(lerp(float3(0.85, 0.88, 0.9), float3(0.05, 0.05, 0.05), hole), a);
        nrm = float4(0.5 + radial * 0.15, 0.5, lerp(0.05, 0.4, a), 0);
    } else if (cell < 10u) {
        // blood splat: main blob + satellite droplets + streaks
        float r = 0.35 + 0.15 * fbm(float2(ang * 2.0, cell), 3);
        float blob = smoothstep(r, r - 0.05, d);
        float drops = 0;
        [loop] for (int k = 0; k < 16; k++) {
            float ca = hashF(seed + k) * TWO_PI;
            float dist = 0.45 + hashF(seed + k + 51u) * 0.45;
            float rr = 0.02 + hashF(seed + k + 97u) * 0.05;
            float2 cpos = float2(cos(ca), sin(ca)) * dist;
            drops = max(drops, smoothstep(rr, rr * 0.6, length(p - cpos)));
            // streak towards the drop
            float da = abs(frac((ang - ca) / TWO_PI + 0.5) - 0.5) * TWO_PI;
            drops = max(drops, step(da * d, rr * 0.35) * step(d, dist) * step(r * 0.8, d));
        }
        float a = saturate(max(blob, drops));
        float wetEdge = smoothstep(r - 0.08, r, d) * blob;
        alb = float4(lerp(float3(0.095, 0.004, 0.003), float3(0.05, 0.002, 0.002), wetEdge), a);
        nrm = float4(0.5, 0.5, 0.18, 0);
    } else if (cell < 12u) {
        // scorch mark: soot blotch with streaky radial edge
        float r = 0.65 + 0.2 * fbm(float2(ang * 3.0, cell * 5.0), 4);
        float soot = saturate(1.0 - d / r);
        soot = pow(soot, 0.6) * (0.75 + 0.25 * fbm(p * 5.0 + cell, 4));
        alb = float4(float3(0.015, 0.013, 0.012), saturate(soot * 1.3));
        nrm = float4(0.5, 0.5, 0.98, 0);
    } else if (cell == 12u) {
        // blood pool
        float r = 0.8 + 0.12 * fbm(float2(ang * 2.0, 3.0), 3);
        float a = smoothstep(r, r - 0.04, d);
        alb = float4(float3(0.055, 0.002, 0.002), a);
        nrm = float4(0.5, 0.5, 0.06, 0);
    }
    uAlbedoOut[id.xy] = alb;
    uNormalOut[id.xy] = nrm;
}

// ------------------------------------------------------------------------------------------------
struct DecalVSOut {
    float4 pos : SV_Position;
    nointerpolation uint inst : TEXCOORD0;
};

// Unit cube from the vertex id (36 vertices, outward faces)
static const uint kCubeIdx[36] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};

DecalVSOut vsDecal(uint vid : SV_VertexID, uint inst : SV_InstanceID) {
    DecalInstance d = tDecals[inst];
    uint c = kCubeIdx[vid];
    float3 q = float3((c & 1u) ? 1.0 : -1.0, (c & 2u) ? 1.0 : -1.0, (c & 4u) ? 1.0 : -1.0);
    float3 p = d.center.xyz + d.axisX.xyz * q.x + d.axisY.xyz * q.y + d.axisZ.xyz * q.z;
    DecalVSOut o;
    o.pos = mul(gViewProj, float4(p, 1));
    o.inst = inst;
    return o;
}

struct DecalOut {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 emissive : SV_Target3;
};

DecalOut psDecal(DecalVSOut i) {
    DecalInstance d = tDecals[i.inst];
    uint2 pix = uint2(i.pos.xy);
    float depth = tDepth[pix];
    if (depth <= 0.0) discard;
    float2 uv = (pix + 0.5) * gScreen.zw;
    float3 P = reconstructPos(uv, depth);
    float3 lp = P - d.center.xyz;
    float3 local = float3(dot(lp, d.axisX.xyz) / dot(d.axisX.xyz, d.axisX.xyz), dot(lp, d.axisY.xyz) / dot(d.axisY.xyz, d.axisY.xyz),
                          dot(lp, d.axisZ.xyz) / dot(d.axisZ.xyz, d.axisZ.xyz));
    if (any(abs(local) > 1.0)) discard;
    uint model = (uint)(tMaterialCopy[pix].b * 255.0 + 0.5);
    if (model == SM_CARPAINT || model == SM_SKIN || model == SM_HAIR || model == SM_CLOTH) discard;  // not on dynamic objects
    float3 Ng = octDecode(tNormalCopy[pix] * 2.0 - 1.0);
    float3 Nd = normalize(d.axisZ.xyz);
    float facing = dot(Ng, Nd);
    if (facing < 0.25) discard;
    float fade = smoothstep(0.25, 0.55, facing) * saturate((1.0 - abs(local.z)) * 4.0) * d.axisX.w;
    uint cell = (uint)d.center.w;
    float2 cuv = (float2(cell % 4u, cell / 4u) + saturate(float2(local.x, -local.y) * 0.5 + 0.5)) / 4.0;
    float4 a = tDecalAlbedo.SampleLevel(sLinearClamp, cuv, 0);
    float4 n = tDecalNormal.SampleLevel(sLinearClamp, cuv, 0);
    float alpha = a.a * fade;
    if (alpha < 0.004) discard;
    float3 T = normalize(d.axisX.xyz), B = normalize(d.axisY.xyz);
    float2 nxy = n.xy * 2.0 - 1.0;
    float3 nn = normalize(T * nxy.x + B * nxy.y + Ng * sqrt(saturate(1.0 - dot(nxy, nxy))) * 1.0);
    DecalOut o;
    o.albedo = float4(a.rgb, alpha);
    o.normal = float4(octEncode(nn) * 0.5 + 0.5, 0, alpha);
    o.material = float4(n.z, n.w, 0, alpha);
    // hot scorch marks glow briefly (d.axisY.w = glow nits, masked by soot density)
    o.emissive = float4(float3(1.0, 0.32, 0.06) * d.axisY.w * a.a * a.a * preExposure(), 1);
    return o;
}

// ------------------------------------------------------------------------------------------------
// Tire skid strips: camera-relative triangle list (pos, u along, v across [-1, 1], opacity)
struct SkidVSIn {
    float3 pos : POSITION;
    float4 uvA : TEXCOORD0;   // x distance along (m), y across (-1..1), z opacity, w unused
};
struct SkidVSOut {
    float4 pos : SV_Position;
    float4 uvA : TEXCOORD0;
};
SkidVSOut vsSkid(SkidVSIn i) {
    SkidVSOut o;
    o.pos = mul(gViewProj, float4(i.pos, 1));
    o.uvA = i.uvA;
    return o;
}
DecalOut psSkid(SkidVSOut i) {
    float across = abs(i.uvA.y);
    // tread pattern + soft edges
    float tread = 0.75 + 0.25 * step(0.5, frac(i.uvA.x * 6.0 + (i.uvA.y > 0 ? 0.25 : 0.0)));
    float edge = smoothstep(1.0, 0.7, across);
    float n = 0.7 + 0.3 * valueNoise(float2(i.uvA.x * 3.0, i.uvA.y * 2.0));
    float alpha = saturate(i.uvA.z * edge * tread * n) * 0.85;
    DecalOut o;
    o.albedo = float4(0.012, 0.012, 0.012, alpha);
    o.normal = float4(0.5, 0.5, 0, 0);
    o.material = float4(0.55, 0.0, 0, alpha * 0.8);
    o.emissive = 0;
    return o;
}
