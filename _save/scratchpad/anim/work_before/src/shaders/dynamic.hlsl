// Dynamic objects: rigid models (vehicles, props in motion) and GPU-skinned characters.
#include "gbuffer.hlsli"
#include "reflection.hlsli"
#include "shadow.hlsli"

struct MaterialInfo {
    float layer, uvScale, roughScale, metal;
    float4 tint;
    float normalScale, shadingModel, flags, emissive;
};
StructuredBuffer<MaterialInfo> tMaterials : register(t10);
Texture2DArray<float4> tMatAlbedo : register(t11);
Texture2DArray<float4> tMatNormal : register(t12);
StructuredBuffer<float4x4> tBones : register(t20);
StructuredBuffer<float4x4> tPrevBones : register(t21);

cbuffer ObjectCB : register(b1) {
    float4x4 gWorld;      // model -> camera-relative world
    float4x4 gPrevWorld;  // previous model -> current camera-relative world
    float4 gTint0;        // primary paint / outfit color, a = dirt
    float4 gTint1;        // secondary color, a = damage
    float4 gObjParams;    // x light bits, y bone offset, z wetness, w emissive scale
    float4 gObjParams2;   // x skinned (1), y window tint 0..1, z fade, w paint finish (0 gloss 1 metallic 2 pearl 3 matte 4 chrome)
    float4 gDamage0;      // crush amount 0..1: front, rear, left, right
    float4 gDamage1;      // roof, underside
    float4 gDmgBoxC;      // model-space collision box center
    float4 gDmgBoxH;      // half extents, w > 0 enables deformation
    float4 gWounds[4];    // characters: bind-pose wound centers (xyz) + radius (w), w = 0 unused
};

float dmgHash(float3 p) { return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453); }
float dmgNoise(float3 p) {
    float3 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(lerp(dmgHash(i), dmgHash(i + float3(1, 0, 0)), f.x), lerp(dmgHash(i + float3(0, 1, 0)), dmgHash(i + float3(1, 1, 0)), f.x), f.y);
    float b = lerp(lerp(dmgHash(i + float3(0, 0, 1)), dmgHash(i + float3(1, 0, 1)), f.x), lerp(dmgHash(i + float3(0, 1, 1)), dmgHash(i + float3(1, 1, 1)), f.x), f.y);
    return lerp(a, b, f.z);
}
// Crumple the body toward its interior around damaged zones (irregular, stronger at the extremities).
float3 applyCrush(float3 p, inout float3 n) {
    if (gDmgBoxH.w <= 0.0) return p;
    float3 h = max(gDmgBoxH.xyz, 0.1);
    float3 q = (p - gDmgBoxC.xyz) / h;   // -1..1 inside the box
    float crumple = dmgNoise(p * 3.1) * 0.8 + dmgNoise(p * 7.3) * 0.4;
    float3 d = 0;
    d.y -= gDamage0.x * smoothstep(0.35, 1.05, q.y) * 0.42;
    d.y += gDamage0.y * smoothstep(0.35, 1.05, -q.y) * 0.38;
    d.x += gDamage0.z * smoothstep(0.25, 1.05, -q.x) * 0.22;
    d.x -= gDamage0.w * smoothstep(0.25, 1.05, q.x) * 0.22;
    d.z -= gDamage1.x * smoothstep(0.2, 1.05, q.z) * 0.3;
    d.z += gDamage1.y * smoothstep(0.3, 1.05, -q.z) * 0.1;
    float amount = dot(gDamage0, 1.0) + gDamage1.x;
    float3 off = d * (0.55 + crumple) + (float3(dmgNoise(p * 5.1 + 3.0), dmgNoise(p * 5.1 + 7.0), dmgNoise(p * 5.1 + 11.0)) - 0.5) * 0.03 * saturate(length(d) * 6.0);
    // crumpled panels catch the light differently
    float3 pert = float3(dmgNoise(p * 9.0) - 0.5, dmgNoise(p * 9.0 + 5.0) - 0.5, dmgNoise(p * 9.0 + 9.0) - 0.5);
    n = normalize(n + pert * saturate(length(d) * 5.0) * 1.2);
    return p + off * step(0.001, amount);
}

// Material ids used for special handling (must match MaterialId in render/mesh.h)
#define M_EMISSIVE 28u
#define M_CARPAINT 36u
#define M_PLASTIC 37u
#define M_LIGHT_HEAD 39u
#define M_LIGHT_TAIL 40u
#define M_SKIN 41u
#define M_HAIR 42u
#define M_CLOTH 43u
#define M_DENIM 44u
#define M_EYE 45u
#define M_CAR_GLASS 48u
#define M_LIGHT_INDICATOR 50u
#define M_CAR_WINDOW 52u

struct VSInRigid {
    float3 pos : POSITION;
    float2 nrm : NORMAL;
    float2 tan : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint mat : MATID;
};
struct VSInSkinned {
    float3 pos : POSITION;
    float2 nrm : NORMAL;
    float2 tan : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    uint mat : MATID;
    uint4 bones : BONES;
    float4 weights : WEIGHTS;
};
struct VSOut {
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float3 tan : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 color : COLOR0;
    nointerpolation uint mat : MATID;
    float4 curClip : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
    float3 localPos : TEXCOORD6;
};

VSOut finishVS(float3 localPos, float3 rel, float3 prevRel, float3 n, float3 t, float2 uv, float4 color, uint mat) {
    VSOut o;
    o.rel = rel;
    o.pos = mul(gViewProj, float4(rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(rel, 1));
    o.prevClip = mul(gPrevViewProj, float4(prevRel, 1));
    o.nrm = n;
    o.tan = t;
    o.uv = uv;
    o.color = color;
    o.mat = mat;
    o.localPos = localPos;
    return o;
}

VSOut vsRigid(VSInRigid i) {
    float3 ln = octDecode(i.nrm);
    float3 lp = applyCrush(i.pos, ln);
    float3 rel = mul(gWorld, float4(lp, 1)).xyz;
    float3 prevRel = mul(gPrevWorld, float4(lp, 1)).xyz;
    float3 n = normalize(mul((float3x3)gWorld, ln));
    float3 t = normalize(mul((float3x3)gWorld, octDecode(i.tan)));
    return finishVS(i.pos, rel, prevRel, n, t, i.uv, i.color, i.mat);
}

VSOut vsSkinned(VSInSkinned i) {
    uint off = (uint)gObjParams.y;
    float4x4 m = tBones[off + i.bones.x] * i.weights.x + tBones[off + i.bones.y] * i.weights.y +
                 tBones[off + i.bones.z] * i.weights.z + tBones[off + i.bones.w] * i.weights.w;
    float4x4 pm = tPrevBones[off + i.bones.x] * i.weights.x + tPrevBones[off + i.bones.y] * i.weights.y +
                  tPrevBones[off + i.bones.z] * i.weights.z + tPrevBones[off + i.bones.w] * i.weights.w;
    float3 lp = mul(m, float4(i.pos, 1)).xyz;
    float3 plp = mul(pm, float4(i.pos, 1)).xyz;
    float3 rel = mul(gWorld, float4(lp, 1)).xyz;
    float3 prevRel = mul(gPrevWorld, float4(plp, 1)).xyz;
    float3 n = normalize(mul((float3x3)gWorld, mul((float3x3)m, octDecode(i.nrm))));
    float3 t = normalize(mul((float3x3)gWorld, mul((float3x3)m, octDecode(i.tan))));
    return finishVS(i.pos, rel, prevRel, n, t, i.uv, i.color, i.mat);
}

cbuffer ShadowPassCB : register(b2) {
    float4x4 gShadowViewProj;
};
float4 vsRigidShadow(VSInRigid i) : SV_Position {
    float3 ln = octDecode(i.nrm);
    float3 rel = mul(gWorld, float4(applyCrush(i.pos, ln), 1)).xyz;
    return mul(gShadowViewProj, float4(rel, 1));
}
float4 vsSkinnedShadow(VSInSkinned i) : SV_Position {
    uint off = (uint)gObjParams.y;
    float4x4 m = tBones[off + i.bones.x] * i.weights.x + tBones[off + i.bones.y] * i.weights.y +
                 tBones[off + i.bones.z] * i.weights.z + tBones[off + i.bones.w] * i.weights.w;
    float3 rel = mul(gWorld, float4(mul(m, float4(i.pos, 1)).xyz, 1)).xyz;
    return mul(gShadowViewProj, float4(rel, 1));
}


// Camera fade (a pedestrian between the camera and the player, or at the lens) as dither coverage. Below ~30% the
// object is not drawn at all: a sparse dither of a body right in front of the camera reads as a grid of dark
// dashes across whatever is behind it, and TAA cannot average so few samples. The threshold steps evenly through
// [0,1) per pixel over time (golden ratio), so TAA converges to a smooth see-through body instead of a crawling
// pattern.
float camFadeCoverage() { return smoothstep(0.3, 1.0, gObjParams2.z); }
void camFadeClip(float2 pix, float coverage) { clip(coverage - ignTemporal(pix, gTime.z, 3.0) - 0.002); }

GBufferOut psDynamic(VSOut i, bool front : SV_IsFrontFace) {
    // faded objects (a pedestrian between the camera and the player) dither out
    if (gObjParams2.z < 0.999) camFadeClip(i.pos.xy, camFadeCoverage());
    uint matId = i.mat & 0xffu;
    MaterialInfo m = tMaterials[matId];
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 T = normalize(i.tan - N * dot(i.tan, N));
    float3 B = cross(N, T);
    float2 uv = i.uv * m.uvScale;
    float4 a = tMatAlbedo.Sample(sAnisoWrap, float3(uv, m.layer));
    float4 nr = tMatNormal.Sample(sAnisoWrap, float3(uv, m.layer));
    float3 albedo = a.rgb * i.color.rgb;
    float2 nxy = (nr.xy * 2.0 - 1.0) * m.normalScale;
    float3 n = normalize(T * nxy.x + B * nxy.y + N * sqrt(saturate(1.0 - dot(nxy, nxy))));
    float rough = saturate(nr.z * m.roughScale);
    float metal = m.metal;
    float ao = nr.w;
    uint sm = (uint)m.shadingModel;
    float extra = 0;
    float3 emissive = 0;
    uint lightBits = (uint)gObjParams.x;
    float dirt = gTint0.a;
    if (matId == M_CARPAINT) {
        // Paint color from the object tint (vertex color alpha selects primary/secondary)
        float3 paint = lerp(gTint0.rgb, gTint1.rgb, step(0.5, 1.0 - i.color.a));
        albedo = paint * lerp(1.0, a.r * 1.6, 0.25);
        metal = 0.25;
        rough = lerp(0.28, 0.6, dirt);
        extra = 1.0 - dirt * 0.7;  // clearcoat strength
        n = N;
        uint finish = (uint)(gObjParams2.w + 0.5);
        if (finish == 1u) {          // metallic: brighter flake, tighter base highlight
            metal = 0.6;
            rough = lerp(0.2, 0.55, dirt);
            albedo *= lerp(0.85, 1.25, a.r);
        } else if (finish == 2u) {   // pearl: soft two-tone shift across the panels
            float shift = saturate(abs(N.z) * 0.8 + 0.2 * a.r);
            albedo = lerp(albedo, albedo.gbr * 0.9 + 0.08, 0.25 * shift);
            metal = 0.4;
        } else if (finish == 3u) {   // matte: no clearcoat, diffuse
            metal = 0.0;
            rough = lerp(0.62, 0.8, dirt);
            extra = 0.0;
        } else if (finish == 4u) {   // chrome: mirror with the paint as a tint
            albedo = lerp(float3(0.95, 0.95, 0.95), paint, 0.35);
            metal = 1.0;
            rough = lerp(0.06, 0.3, dirt);
            extra = 0.2;
        }
        // grime towards the bottom of the body
        float grime = saturate((0.6 - i.localPos.z) * 1.5) * dirt;
        albedo = lerp(albedo, float3(0.18, 0.15, 0.12), grime * 0.6);
    } else if (matId == M_CAR_GLASS || matId == M_CAR_WINDOW) {
        albedo = float3(0.01, 0.012, 0.014);
        rough = 0.02;
        metal = 0;
        extra = 1.0;
        sm = SM_CARPAINT;
        n = N;
    } else if (matId == M_LIGHT_HEAD) {
        bool on = (lightBits & 1u) != 0;
        albedo = float3(0.8, 0.8, 0.8);
        rough = 0.05;
        emissive = on ? float3(1.0, 0.95, 0.85) * 3000.0 : 0;
    } else if (matId == M_LIGHT_TAIL) {
        bool on = (lightBits & 1u) != 0;
        bool brake = (lightBits & 2u) != 0;
        bool rev = (lightBits & 4u) != 0;
        albedo = float3(0.35, 0.02, 0.02);
        rough = 0.1;
        float inten = (on ? 180.0 : 0.0) + (brake ? 900.0 : 0.0);
        emissive = float3(1.0, 0.05, 0.03) * inten;
        // reverse lamp region marked by vertex color green
        if (i.color.g > 0.5) emissive = rev ? float3(1, 1, 1) * 800.0 : 0;
    } else if (matId == M_LIGHT_INDICATOR) {
        bool left = (lightBits & 8u) != 0, right = (lightBits & 16u) != 0;
        bool isLeft = i.localPos.x < 0;
        bool blink = frac(gTime.x * 1.5) < 0.5;
        // light-bar lenses (police/fire/ambulance) carry their colour in the vertex colour with alpha < 0.5;
        // indicator lenses (alpha >= 0.5) are amber
        bool sirenLens = i.color.a < 0.5;
        albedo = sirenLens ? i.color.rgb * 0.45 : float3(0.6, 0.35, 0.02);
        rough = 0.08;
        emissive = !sirenLens && ((isLeft && left) || (!isLeft && right)) && blink ? float3(1.0, 0.55, 0.05) * 700.0 : 0;
        // siren flashing (bit 5): only the light-bar lenses
        if ((lightBits & 32u) && sirenLens) {
            float ph = frac(gTime.x * 2.2 + (isLeft ? 0.5 : 0.0));
            float on = ph < 0.25 || (ph > 0.35 && ph < 0.55) ? 1.0 : 0.0;
            emissive = i.color.rgb * on * 4000.0;
        }
    } else if (matId == M_EMISSIVE) {
        emissive = albedo * i.color.a * 400.0 * m.emissive;
        // animated signage / light patterns (same param encoding as world.hlsl emissiveAnim; interiors use it)
        uint pat = (i.mat >> 8) & 15u;
        bool gentle = gRenderFlags.x > 0.5 && (pat == 1u || pat == 2u || pat == 5u || pat == 7u || pat == 8u || pat == 9u);
        if (gentle) {
            // Reduce flashing (accessibility): strobes, blinkers, fast hue cycles and TV flicker pulse gently
            float ph = ((i.mat >> 12) & 255u) / 256.0;
            emissive *= 0.55 + 0.25 * sin(gTime.x * 1.2 + ph * 6.2832);
        } else if (pat != 0u) {
            float ph = ((i.mat >> 12) & 255u) / 256.0, t = gTime.x;
            if (pat == 1u) emissive *= frac(t * 0.8 + ph) < 0.18 ? 1.6 : 0.03;
            else if (pat == 2u) emissive *= 0.2 + 1.3 * step(0.5, frac(t * 1.5 - ph * 4.0));
            else if (pat == 3u) emissive = hsvToRgbF(frac(t * 0.08 + ph)) * dot(emissive, 0.3333) * 1.4;
            else if (pat == 4u) emissive *= 0.45 + 0.55 * (0.5 + 0.5 * sin(t * 2.1 + ph * 6.2832));
            else if (pat == 5u) emissive *= frac(t * 0.5 - ph) < 0.06 ? 3.0 : 0.0;
            else if (pat == 6u) emissive *= gExposure.w;
            else if (pat == 7u) emissive *= frac(t * 0.33 + ph) < 0.5 ? 1.0 : 0.05;
            else if (pat == 8u) emissive = hsvToRgbF(frac(t * 0.45 + ph)) * dot(emissive, 0.3333) * 1.6;   // fast hue (club)
            else if (pat == 9u) emissive *= 0.6 + 0.4 * sin(t * 37.0 + ph * 40.0) * sin(t * 23.0 + ph * 7.0);   // TV flicker
        }
    } else if (matId == M_SKIN) {
        sm = SM_SKIN;
        // Character skin (material param bit 0) carries extra vertex data: colour.a = 1 - gloss (0 skin, ~0.2 oily
        // T-zone, ~0.65 nails, ~0.9 lip vermilion) and uv = wrinkle channel (uv.x crease phase, crease centres at
        // frac = 0.5; uv.y crease depth in mm), so its uv is not a surface mapping and the material textures are not
        // used. Other skin (animals) keeps its texture mapping.
        bool charSkin = ((i.mat >> 8) & 1u) != 0u;
        float gloss = charSkin ? 1.0 - i.color.a : 0.0;
        float nail = smoothstep(0.5, 0.6, gloss) * (1.0 - smoothstep(0.8, 0.85, gloss));
        float drift = a.r;
        if (charSkin) {
            n = N;
            ao = 1.0;
            drift = valueNoise3(i.localPos * 9.0 + 1.7);
        }
        // Mottling: subtle hemoglobin / melanin variation (bind-pose position: sticks to the animated skin); none on
        // nails
        float mott = valueNoise3(i.localPos * 38.0) * 0.6 + valueNoise3(i.localPos * 95.0 + 3.1) * 0.4;
        albedo = i.color.rgb * lerp(0.9, 1.04, drift) * lerp(1.0, lerp(float3(0.975, 1.0, 1.01), float3(1.035, 0.975, 0.965), mott), 1.0 - nail);
        // Curvature (1/m) from screen-space derivatives: thin, tightly curved parts (ears, nostrils, fingers) let
        // light through (stored for the transmission term), convex ridges (nose, brow, cheekbones) read oilier
        float3 dPx = ddx(i.rel), dPy = ddy(i.rel);
        float pxLen = max(length(dPx) + length(dPy), 1e-6);
        float curv = (length(ddx(N)) + length(ddy(N))) / pxLen;
        float thin = saturate((curv - 90.0) / 260.0);   // fingers ~125 /m, ear rims and nostril wings higher
        // right in front of the camera (first-person hands and forearms) pinched skinning makes the curvature
        // estimate spike: cap it below ~0.5 mm per pixel
        thin = min(thin, lerp(0.6, 1.0, saturate((pxLen * 0.5 - 0.0003) / 0.0004)));
        // character skin takes its oiliness from the gloss channel (T-zone, lips, lid margins), so its base stays
        // satin; other skin gets it from convex curvature
        rough = charSkin ? lerp(0.54, 0.44, saturate((curv - 20.0) / 90.0)) : lerp(0.5, 0.36, saturate((curv - 20.0) / 90.0));
        // Pores and fine creases as a bump from the bind-pose position, faded before they could alias; lips and
        // nails have none
        float detailW = saturate(1.6 - pxLen * 0.5 / 0.0009);
        float pore = 0;
        if (detailW > 0.0) {
            float h1 = valueNoise3(i.localPos * 1400.0);
            float h2 = valueNoise3(i.localPos * float3(240.0, 240.0, 1700.0) + 17.3);
            pore = smoothstep(0.6, 0.9, h1) * (1.0 - gloss);
            n = perturbBump(n, N, dPx, dPy, ((h2 - 0.5) * 0.4 - pore * 0.8) * 4e-5 * detailW * (1.0 - gloss));
            rough = saturate(rough + pore * 0.08 * detailW);
            ao *= 1.0 - pore * 0.3 * detailW;   // micro-occlusion inside the pores
        }
        rough = lerp(rough, 0.24, gloss);
        // Wrinkles: creases across the lines, faded before they get closer than ~3 pixels apart
        float cv = saturate(1.0 - abs(frac(i.uv.x) - 0.5) / 0.17);
        cv *= cv;
        float cDepth = saturate(i.uv.y / 0.25);
        float cw = charSkin ? saturate((0.35 - fwidth(i.uv.x)) / 0.2) * step(1e-4, i.uv.y) : 0.0;
        n = perturbBump(n, N, dPx, dPy, -cv * i.uv.y * 0.001 * cw);
        ao *= 1.0 - 0.4 * cv * cDepth * cw;
        albedo *= 1.0 - 0.06 * cv * cDepth * cw;
        extra = 0.5 + 0.5 * thin;   // SM_SKIN: 0.5 + thinness / 2 (subsurface always on)
    } else if (matId == M_HAIR) {
        sm = SM_HAIR;
        // Strand groups hang along the bind-pose vertical: per-strand brightness / hue jitter and dark gaps
        // between clumps (self-shadowing inside the hair volume)
        float strand = valueNoise3(i.localPos * float3(1100.0, 1100.0, 45.0));
        float clump = valueNoise3(i.localPos * float3(260.0, 260.0, 12.0) + 7.7);
        float3 hueJit = lerp(float3(1.03, 0.99, 0.95), float3(0.96, 1.0, 1.05), valueNoise3(i.localPos * float3(600.0, 600.0, 30.0) + 2.9));
        albedo = i.color.rgb * (0.65 + a.r * 0.5) * lerp(0.72, 1.15, strand) * hueJit;
        ao *= lerp(0.62, 1.0, saturate(clump * 1.4 - 0.1)) * lerp(0.85, 1.0, strand);
        rough = 0.4;
        extra = strand;   // sparkle of the secondary (coloured) highlight
        if (gObjParams2.y > 0.5) {
            // LOD0: strand cards cover this shell, which stands for the inner hair volume: occluded, darker and
            // without a continuous highlight band of its own (the cards carry the highlights)
            albedo *= 0.8;
            ao *= 0.6;
            extra *= 0.3;
            rough = 0.75;   // broad, dim lobe
        }
    } else if (matId == M_CLOTH || matId == M_DENIM) {
        sm = SM_CLOTH;
        bool denim = matId == M_DENIM;
        float3 dPx = ddx(i.rel), dPy = ddy(i.rel);
        float pxLen = max(length(dPx) + length(dPy), 1e-6);
        // Fold / crease occlusion from the signed curvature (concave = inside a fold)
        float curvS = (dot(ddx(N), dPx) + dot(ddy(N), dPy)) / max(dot(dPx, dPx) + dot(dPy, dPy), 1e-10);
        ao *= clamp(1.0 + min(curvS, 0.0) * 0.012, 0.55, 1.0);
        // Yarn relief in garment UV space (metres along the surface): jersey knit by default (T-shirts), plain weave
        // (param 1: shirts), 3/1 twill (denim), rib knit (param 3: sweaters, cuffs). Yarn paths wander by a fraction
        // of a period so no long straight lines form, and the relief fades out before a period drops below ~5
        // pixels: no moire or shimmer at any distance (the texture itself carries no yarn-scale pattern).
        uint style = denim ? 2u : ((i.mat >> 8) & 3u);
        float period = style == 1u ? 0.0008 : (style == 2u ? 0.0009 : (style == 3u ? 0.0022 : 0.0012));
        float mPerPx = pxLen * 0.5;
        float detailW = saturate((period / mPerPx - 5.0) / 3.0);
        if (detailW > 0.0) {
            float2 wob = float2(valueNoise(i.uv * 70.0), valueNoise(i.uv * 70.0 + 5.3)) - 0.5;
            float2 q = i.uv / period + wob * 0.8;
            float h;
            if (style == 1u) {
                h = 0.5 + 0.5 * cos(PI * q.x) * cos(PI * q.y);                       // over / under basket cells
            } else if (style == 2u) {
                h = pow(0.5 + 0.5 * cos(TWO_PI * (q.x - q.y * 0.34)), 1.5) * 0.85 + 0.15 * (0.5 + 0.5 * cos(TWO_PI * q.x));
            } else if (style == 3u) {
                h = pow(0.5 + 0.5 * cos(TWO_PI * q.x), 0.7) * (0.9 + 0.1 * cos(TWO_PI * q.y * 1.6));   // ribs
            } else {
                // jersey: columns (wales) of V-shaped loops, one course per 0.8 period
                float xl = abs(frac(q.x) - 0.5), yl = frac(q.y * 1.25);
                h = exp(-sq((xl - 0.1 - 0.22 * yl) / 0.13));
            }
            n = perturbBump(n, N, dPx, dPy, (h - 0.5) * 4e-5 * detailW);
            albedo *= 1.0 + (h - 0.5) * 0.1 * detailW;   // yarn tops catch a little more dye / light
        }
        // Crease channel (param bit 2; garments mark knees, ankles, elbows, waist bunching, skirt flutes): vertex
        // colour alpha = 1 - crease. Fine wrinkle ridges along the tangent (around the limb; uv.y runs along it),
        // ~9 mm apart with a little wander, faded before they get closer than ~4 pixels; the troughs are occluded and
        // read darker even in flat light.
        float crease = ((i.mat >> 8) & 4u) != 0u ? saturate(1.0 - i.color.a) : 0.0;
        if (crease > 0.0) {
            const float P = 0.009;
            float cw = saturate((P / mPerPx - 4.0) / 3.0);
            if (cw > 0.0) {
                float x = i.uv.y / P + (valueNoise(i.uv * 35.0) - 0.5) * 1.2;
                float ridge = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * frac(x) - 1.0));
                n = perturbBump(n, N, dPx, dPy, (ridge - 0.5) * 0.06 * P * crease * cw);
            }
            ao *= 1.0 - 0.35 * crease;
            albedo *= 1.0 - 0.12 * crease;
        }
        rough = saturate((denim ? 0.78 : 0.86) * lerp(0.94, 1.06, valueNoise3(i.localPos * 20.0)));
        extra = denim ? 0.35 : 0.75;   // sheen strength
    } else if (matId == M_EYE) {
        // Eye sphere (face.cpp): uv = (phase, polar) * 0.01, polar measured from the eye axis. Vertex colours give
        // pupil / iris / limbus / sclera rings; add iris fibres, a brighter collarette, sclera veins and lid shading.
        float pd = i.uv.y * 100.0 * 57.29578;
        float phase = i.uv.x * 100.0;
        if (pd > 11.0 && pd < 29.0) {
            float fib = valueNoise(float2(phase * 11.0, pd * 0.35)) * 0.6 + valueNoise(float2(phase * 31.0, pd * 1.1)) * 0.4;
            float collar = exp(-sq((pd - 17.5) / 1.6));
            albedo = albedo * lerp(0.72, 1.25, fib) * (1.0 + collar * 0.25);
            albedo *= lerp(1.0, 0.55, smoothstep(24.0, 28.5, pd));   // limbal ring
        } else if (pd >= 29.0) {
            float vein = smoothstep(0.9, 0.97, valueNoise(float2(phase * 16.0, pd * 0.45))) * saturate((pd - 42.0) / 35.0);
            // sclera: off-white, and darker overall: the socket / brow shadow the shadow maps cannot resolve
            albedo = lerp(albedo * float3(0.62, 0.6, 0.58), float3(0.45, 0.12, 0.1), vein * 0.4);
        }
        ao *= lerp(1.0, 0.45, saturate((pd - 45.0) / 35.0));   // the lids shade the edges of the eyeball
        // wet cornea over the iris: sharp catchlights from the env probe / SSR; the sclera is moist but diffuse
        rough = lerp(0.03, 0.24, smoothstep(26.0, 36.0, pd));
        metal = 0;
        n = N;
    }
    // Blood from wounds (skin, hair and clothing): irregular stains that spread downward, darker and glossier
    if (gObjParams2.x > 0.5 && (sm == SM_SKIN || sm == SM_CLOTH || sm == SM_HAIR || matId == M_CLOTH || matId == M_DENIM)) {
        float blood = 0;
        [unroll] for (int w = 0; w < 4; w++) {
            float r = gWounds[w].w;
            if (r <= 0.0) continue;
            float3 d = i.localPos - gWounds[w].xyz;
            d.z = d.z > 0.0 ? d.z * 1.8 : d.z * 0.6;   // runs down
            float n = dmgNoise(i.localPos * 38.0) * 0.5 + dmgNoise(i.localPos * 11.0);
            blood = max(blood, saturate((r * (0.75 + 0.5 * n) - length(d)) / (r * 0.35)));
        }
        albedo = lerp(albedo, float3(0.16, 0.012, 0.01), blood * 0.92);
        rough = lerp(rough, 0.28, blood);
    }
    // Rain wetness on upward surfaces
    float wet = gWeather.y * saturate(N.z * 2.0 + 0.3) * (gObjParams.z > 0 ? 1.0 : (gObjParams.z < 0 ? 0.0 : 0.5));   // < 0: indoors, never wet
    albedo *= lerp(1.0, 0.7, wet * (sm == SM_CARPAINT ? 0.3 : 1.0));
    rough = lerp(rough, 0.1, wet * 0.7);
    return packGBuffer(albedo, ao, n, rough, metal, sm, extra, emissive * gObjParams.w, i.curClip, i.prevClip);
}

// ------------------------------------------------------------------------------------------------------------------
// Hair strand cards (LOD0 characters; drawn after the opaque hair shell, culling off). mat = MAT_HAIR | param << 8:
// kind = (mat >> 8) & 15 (1 scalp / long hair, 2 eyelash, 3 eyebrow, 4 beard), seed = (mat >> 12) & 0xffff per card.
// uv.x runs across the card, uv.y along the strands (0 root .. 1 tip); the tangent is the strand direction; colour.a
// is the strand density. Coverage is a row of tapering, slightly wavy strands of individual lengths that resolves
// to its average once the strands get thinner than a pixel; it is dithered against a per-frame threshold, which TAA
// turns into soft, see-through edges and tips.
float hairHash(float x) { return frac(sin(x * 91.3458 + 17.17) * 47453.5453); }

float hairCardCoverage(float2 uv, uint kind, float seed, float density, float footprint, out float strandRnd) {
    float strands = kind == 2u ? 3.0 : (kind == 3u ? 5.0 : (kind == 4u ? 6.0 : 8.0));
    float x = uv.x * strands;
    float id = floor(x) + seed * 977.0;
    strandRnd = hairHash(id);
    float len = lerp(kind == 1u ? 0.7 : 0.82, 1.0, hairHash(id + 3.1));   // strands end at different lengths
    float along = uv.y / len;
    float alive = along < 1.0 ? 1.0 : 0.0;
    float taper = saturate(1.0 - along);
    float wave = (kind == 4u ? 0.16 : 0.06) * sin(uv.y * (kind == 4u ? 23.0 : 9.0) + strandRnd * 6.283);
    float c = 0.5 + (hairHash(id + 7.7) - 0.5) * 0.3 + wave;
    float halfW = lerp(0.24, 0.4, hairHash(id + 5.3)) * (0.3 + 0.7 * sqrt(taper));
    float prof = saturate((1.0 - abs(frac(x) - c) / max(halfW, 1e-3)) * 1.6) * alive;
    // strand cells per pixel: sharp strands up close, their average coverage once they are sub-pixel
    float cov = lerp(prof, halfW * 1.6 * alive, saturate(footprint * strands * 1.5 - 0.5));
    // thinner towards the card's side edges and the tips
    float edge = smoothstep(0.0, 0.14, uv.x) * smoothstep(1.0, 0.86, uv.x);
    return cov * edge * density * lerp(1.0, 0.7, smoothstep(0.55, 1.0, uv.y));
}

GBufferOut psHairCard(VSOut i, bool front : SV_IsFrontFace) {
    uint kind = (i.mat >> 8) & 15u;
    float seed = (float)((i.mat >> 12) & 0xffffu) * (1.0 / 65535.0);
    float rnd;
    float cov = hairCardCoverage(i.uv, kind, seed, i.color.a, fwidth(i.uv.x), rnd);
    clip(cov * camFadeCoverage() - ignTemporal(i.pos.xy, gTime.z, 3.0) - 0.002);   // one threshold for coverage and fade
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 T = i.tan - N * dot(i.tan, N);
    float tl = length(T);
    T = tl > 1e-5 ? T / tl : hairRefAxis(N);
    // Per-strand brightness and hue, tips lightened by the sun; roots darker and occluded by the layers above
    float3 hueJit = lerp(float3(1.04, 0.99, 0.94), float3(0.95, 1.0, 1.06), hairHash(rnd * 71.0 + 1.3));
    float3 albedo = i.color.rgb * lerp(0.75, 1.15, rnd) * hueJit * lerp(1.0, 1.12, smoothstep(0.5, 1.0, i.uv.y));
    float ao = kind == 1u ? lerp(0.5, 1.0, smoothstep(0.0, 0.55, i.uv.y)) : (kind == 2u ? 0.8 : 0.9);
    // brows and beards are short, coarse, fairly matte hairs: broader, dimmer highlights (less per-strand sparkle)
    float rough = kind == 2u ? 0.5 : (kind == 3u ? 0.55 : (kind == 4u ? 0.48 : 0.38));
    if (kind == 3u || kind == 4u) rnd *= 0.5;
    // rain soaks the hair: darker, glossier
    float wet = gWeather.y * (gObjParams.z > 0 ? 1.0 : (gObjParams.z < 0 ? 0.0 : 0.5));
    albedo *= lerp(1.0, 0.7, wet);
    rough = lerp(rough, 0.2, wet * 0.6);
    return packGBuffer(albedo, ao, N, rough, encodeHairTangent(N, T), SM_HAIR, rnd, 0.0, i.curClip, i.prevClip);
}

// Card shadows: alpha-tested at the card's average strand coverage (strands are far below a shadow texel)
struct VSCardShadowOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float density : TEXCOORD1;
};
VSCardShadowOut vsSkinnedShadowCard(VSInSkinned i) {
    VSCardShadowOut o;
    uint off = (uint)gObjParams.y;
    float4x4 m = tBones[off + i.bones.x] * i.weights.x + tBones[off + i.bones.y] * i.weights.y +
                 tBones[off + i.bones.z] * i.weights.z + tBones[off + i.bones.w] * i.weights.w;
    float3 rel = mul(gWorld, float4(mul(m, float4(i.pos, 1)).xyz, 1)).xyz;
    o.pos = mul(gShadowViewProj, float4(rel, 1));
    o.uv = i.uv;
    o.density = i.color.a;
    return o;
}
void psHairCardShadow(VSCardShadowOut i) {
    float edge = smoothstep(0.0, 0.14, i.uv.x) * smoothstep(1.0, 0.86, i.uv.x);
    float cov = i.density * edge * lerp(0.75, 0.2, smoothstep(0.4, 1.0, i.uv.y));
    clip(cov - 0.45);
}

// ------------------------------------------------------------------------------------------------------------------
// Vehicle windows, forward-shaded after the deferred lighting over the lit cabin (premultiplied alpha): environment
// reflection and sun glint with Fresnel, tint absorption (vertex colour alpha = clarity: 1 clear windscreen .. 0
// privacy glass), a dust film from the vehicle's dirt, aerial perspective and volumetric fog.
float4 psGlass(VSOut i, bool front : SV_IsFrontFace) : SV_Target {
    if (gObjParams2.z < 0.999) camFadeClip(i.pos.xy, camFadeCoverage());
    float3 N = normalize(i.nrm) * (front ? 1.0 : -1.0);
    float3 V = normalize(-i.rel);
    float NoV = saturate(dot(N, V));
    float dist = length(i.rel);
    float2 screenUV = i.pos.xy * gScreen.zw;
    float dirt = saturate(gTint0.a);
    float rough = lerp(0.02, 0.2, dirt);
    float3 R = reflect(-V, N);
    float3 refl = envReflection(R, rough) * preExposure() * horizonOcclusion(R, N);
    float F = 0.04 + 0.96 * pow5(1.0 - NoV);
    float3 L = gSunDir.xyz;
    float3 H = normalize(L + V);
    float NoL = saturate(dot(N, L));
    float a2 = max(rough * rough, 0.0016);
    float spec = D_GGX(saturate(dot(N, H)), a2) * V_SmithGGXCorrelated(NoV, NoL, a2) * NoL;
    float viewDepth = dot(i.rel, gCamForward.xyz);
    float shadow = sampleSunShadow(i.rel, N, viewDepth, (uint2)i.pos.xy);
    float3 sunE = mainLightIlluminance();
    float3 glint = sunE * spec * F * shadow * preExposure();
    // tint: part of the view into the cabin is absorbed (tinted towards the glass colour)
    float cover = lerp(0.8, 0.18, saturate(i.color.a));
    cover = lerp(cover, 0.94, saturate(gObjParams2.y));   // aftermarket window tint
    float3 tintCol = i.color.rgb * float3(0.02, 0.028, 0.026);
    // dust film scatters sky and sun light (dirty windows look milky)
    float3 skyE = evalSH9(N) * PI;
    float3 film = 0.35 * (skyE + sunE * NoL * shadow) / PI * dirt * 0.35 * preExposure();
    float a = saturate(F + (1.0 - F) * saturate(cover + dirt * 0.25));
    float3 col = refl * F + glint + film + tintCol * (1.0 - F) * cover * (skyE / PI) * preExposure();
    // the cabin behind is already fogged: attenuate the glass's own light, in-scatter weighted by its coverage
    float4 ap = aerialPerspective(screenUV, dist);
    col = col * ap.a + ap.rgb * preExposure() * a;
    float4 fv = froxelFog(screenUV, viewDepth);
    col = col * fv.a + fv.rgb * a;
    return float4(min(col, 60000.0), a);
}
