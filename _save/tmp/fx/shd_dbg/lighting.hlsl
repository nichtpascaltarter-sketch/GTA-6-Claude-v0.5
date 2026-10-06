// Deferred lighting: sun/moon with cascaded shadows, sky ambient, aerial perspective, sky background.
#include "gbuffer.hlsli"
#include "reflection.hlsli"
#include "shadow.hlsli"
#include "lights.hlsli"

Texture2D<float4> tAlbedo : register(t0);
Texture2D<float2> tNormal : register(t1);
Texture2D<float4> tMaterial : register(t2);
Texture2D<float3> tEmissive : register(t3);
Texture2D<float> tDepth : register(t4);
Texture2D<float4> tAOGI : register(t5);        // half res: rgb indirect diffuse (pre-exposed), a ambient visibility
Texture2D<float4> tClouds : register(t6);
Texture2D<float> tHalfDepth : register(t8);     // half-res linear depth (bilateral upsample of AO/GI)
Texture2D<float2> tHalfNormal : register(t9);
Texture2D<float4> tSSR : register(t10);         // screen-space reflections: rgb radiance (pre-exposed), a confidence
Texture2D<float4> tSkinLUT : register(t14);     // pre-integrated skin scattering by N.L and scatter width (render/skin.cpp)
RWTexture2D<float4> uHDR : register(u0);
// --debugview output, shown by the tonemap pass in the debug region. The lit image keeps going to uHDR there too, so
// TAA, exposure and the colour pyramid that screen-space reflections and GI sample stay physical (a debug view that
// shows reflections would otherwise reflect itself from the previous frame).
RWTexture2D<float4> uDebugView : register(u1);

// Depth/normal-aware upsample of the half-resolution AO + indirect diffuse.
float4 upsampleAOGI(uint2 pix, float z, float3 N) {
    if (gSSParams.x < 0.5) return float4(0, 0, 0, 1);
    float2 hp = (pix + 0.5) * 0.5 - 0.5;
    int2 b = (int2)floor(hp);
    float2 f = hp - b;
    int2 mx = int2(gHalfScreen.xy) - 1;
    float4 sum = 0;
    float wsum = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        int2 o = int2(k & 1, k >> 1);
        int2 q = clamp(b + o, int2(0, 0), mx);
        float bw = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
        float qz = tHalfDepth[q];
        float3 qn = octDecode(tHalfNormal[q] * 2.0 - 1.0);
        float w = bw * (exp(-abs(qz - z) / (0.04 * z + 0.03)) * pow(saturate(dot(qn, N) * 0.5 + 0.5), 8.0) + 1e-4);
        sum += tAOGI[q] * w;
        wsum += w;
    }
    return sum / max(wsum, 1e-6);
}

// Local lights (streetlights, windows, neon, headlights...). Positions relative to the camera.
StructuredBuffer<LightGPU> tLights : register(t7);
cbuffer LightCB : register(b2) {
    uint gLightCount;
    uint gInteriorCount;   // enterable interior volumes this frame (see below)
    uint2 gLightPad;
};

// ---- Enterable interiors (world/interiors.h; Renderer::interiorVolumes / uploadInteriors) ----------------------------
// Inside an interior volume (oriented box) the sky/probe ambient and the sky reflections are replaced by the room's own
// ambient plus the daylight entering through the room's openings: each portal is a rectangle lit by the outside
// radiance (sky SH / probe in the portal's outward direction), integrated with Lambert's polygon formula. Local lights
// only light the volume they sit in (tLightVolume: 0 outdoors, k + 1 inside volume k), so lamps don't leak through
// walls and street lights don't light rooms. Direct sun still comes in through the openings (the shell's shadows).
struct InteriorGPU {
    float4 c;      // xyz box center (camera-relative), w first portal
    float4 axis;   // xy unit x axis, z portal count, w sky bounce fraction
    float4 he;     // xyz half extents
    float4 amb;    // rgb room ambient radiance (irradiance / PI)
};
struct PortalGPU {
    float4 p0;     // xyz corner (camera-relative), w transmission
    float4 u;      // edge along the wall
    float4 v;      // edge up; cross(u, v) points into the room
};
StructuredBuffer<InteriorGPU> tInteriors : register(t11);
StructuredBuffer<PortalGPU> tPortals : register(t12);
StructuredBuffer<uint> tLightVolume : register(t13);
static int sInterior = -1;   // interior volume of the pixel being shaded (-1 outdoors)
static bool sHairCard = false;   // SM_HAIR pixel from a strand card: sHairT holds its strand direction
static float3 sHairT = float3(0, 0, -1);
// SM_SKIN pixel (decoded from the G-buffer, see psDynamic): scatter width (0 flat .. 1 a 2 mm radius), melanin
// (0 lightest .. 1 darkest skin) and thinness for transmission (0 none .. 1 an ear rim)
static float sSkinScatter = 0;
static float sSkinMelanin = 0;
static float sSkinTransl = 0;

int interiorAt(float3 relPos) {
    [loop] for (uint k = 0; k < gInteriorCount; k++) {
        InteriorGPU v = tInteriors[k];
        float3 d = relPos - v.c.xyz;
        float lx = dot(d.xy, v.axis.xy), ly = dot(d.xy, float2(-v.axis.y, v.axis.x));
        if (abs(lx) <= v.he.x + 0.05 && abs(ly) <= v.he.y + 0.05 && abs(d.z) <= v.he.z + 0.05) return (int)k;
    }
    return -1;
}
// Edge term of the polygon vector form factor (fitted theta / sin(theta), Heitz et al. 2016)
float3 portalEdge(float3 a, float3 b) {
    float x = dot(a, b);
    float y = abs(x);
    float t = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float u = 3.4175940 + (4.1616724 + y) * y;
    float v = t / u;
    float thetaSin = x > 0.0 ? v : 0.5 * rsqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(a, b) * thetaSin;
}
// Clamped-cosine form factor of a portal seen from p around direction n
float portalFormFactor(PortalGPU q, float3 p, float3 n) {
    float3 c0 = q.p0.xyz - p, c1 = c0 + q.v.xyz, c2 = c1 + q.u.xyz, c3 = c0 + q.u.xyz;   // winds away from p
    float3 l0 = normalize(c0), l1 = normalize(c1), l2 = normalize(c2), l3 = normalize(c3);
    float3 f = portalEdge(l0, l1) + portalEdge(l1, l2) + portalEdge(l2, l3) + portalEdge(l3, l0);
    return saturate(dot(f, n) / (2.0 * PI));
}
// Ambient irradiance / PI inside volume k: room ambient + daylight bounce + light entering through each portal
// Daylight bounced around a room (sun and sky off floors and walls): the sky's brightness with a warm-neutral
// tint, not its blue (the blue sky only shows where a surface sees it through an opening)
float3 interiorDayBounce(float amount) {
    float3 sky = evalSH9(float3(0, 0, 1));
    return amount * dot(sky, float3(0.2126, 0.7152, 0.0722)) * float3(1.0, 0.95, 0.88);
}
float3 interiorIrradiance(int k, float3 p, float3 n, float dist) {
    InteriorGPU v = tInteriors[k];
    float3 e = v.amb.rgb + interiorDayBounce(v.axis.w);
    uint first = (uint)v.c.w, count = (uint)v.axis.z;
    [loop] for (uint i = 0; i < count; i++) {
        PortalGPU q = tPortals[first + i];
        float ff = portalFormFactor(q, p, n) * q.p0.w;
        if (ff <= 0.0) continue;
        float3 outN = -normalize(cross(q.u.xyz, q.v.xyz));
        float3 a = ambientIrradiance(outN, dist);
        // light through a window is sky plus sunlit street and facades: pull it halfway to neutral
        e += lerp(a, dot(a, float3(0.2126, 0.7152, 0.0722)) * float3(1.0, 0.97, 0.92), 0.5) * ff;
    }
    return e;
}
// Reflection fallback inside volume k: the room's ambient, and the outside world where the reflection leaves
// through an opening (portal coverage around R, sharper for smooth surfaces)
float3 interiorReflection(int k, float3 p, float3 R, float rough, float dist) {
    InteriorGPU v = tInteriors[k];
    float3 room = v.amb.rgb + interiorDayBounce(v.axis.w);
    float3 e = room;
    uint first = (uint)v.c.w, count = (uint)v.axis.z;
    float cover = 0.0;
    [loop] for (uint i = 0; i < count; i++) {
        PortalGPU q = tPortals[first + i];
        cover += portalFormFactor(q, p, R) * q.p0.w;
    }
    cover = saturate(cover * lerp(4.0, 1.5, saturate(rough)));
    return lerp(e, envReflection(R, rough), cover);
}

// ---- Character shading models -----------------------------------------------------------------------------------
// Skin: pre-integrated subsurface scattering (render/skin.cpp: the diffuse falloff by N.L and scatter width, where red
// light, travelling furthest under the skin, softens and warms the terminator), warm shadow edges, a two-lobe specular
// (broad + tight oily sheen, F0 0.028) and transmission through thin parts lit from behind (ears, nostrils, fingers).
// The scattering tint fades towards neutral with melanin: in dark skin the epidermis absorbs most of the light that
// would travel under the surface, so its terminator stays deep brown instead of turning orange. `thickness` (m): how
// much tissue the light crosses (the sun: from the shadow map; local lights: from the part's thinness; large = none).
float skinScatterSat() { return lerp(1.0, 0.4, sSkinMelanin); }

float3 skinDiffuseFalloff(float NoL) {
    const float n = 64.0;
    float2 uv = float2(NoL * 0.5 + 0.5, sSkinScatter) * ((n - 1.0) / n) + 0.5 / n;
    float3 d = tSkinLUT.SampleLevel(sLinearClamp, uv, 0).rgb;
    return lerp(dot(d, float3(0.2126, 0.7152, 0.0722)).xxx, d, skinScatterSat());
}

float3 skinDirect(GBufferData g, float3 N, float3 V, float3 L, float shadow, float thickness) {
    float NoLr = dot(N, L);
    float3 diffuse = skinDiffuseFalloff(NoLr);
    // shadow edges warm slightly (light scattered under the skin from the lit side); deep shadow stays neutral
    float sh = saturate(shadow);
    float3 sh3 = saturate(sh + sh * (1.0 - sh) * float3(0.35, 0.0, -0.08) * skinScatterSat());
    float3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4), NoL = saturate(NoLr), NoH = saturate(dot(N, H)), VoH = saturate(dot(V, H));
    // the broad lobe of the skin's surface and a sharper one of its oil film (dual lobe, 85 / 15)
    float a1 = max(g.rough * g.rough, 0.01), a2 = max(sq(g.rough * 0.7), 0.006);
    float F = 0.028 + 0.972 * pow5(1.0 - VoH);
    float spec = (D_GGX(NoH, a1) * V_SmithGGXCorrelated(NoV, NoL, a1) * 0.85 + D_GGX(NoH, a2) * V_SmithGGXCorrelated(NoV, NoL, a2) * 0.15) * F;
    float3 r = g.albedo / PI * diffuse * (1.0 - F) * sh3 + spec * NoL * sh;
    // peach fuzz: the vellus hairs catch grazing light (Charlie sheen with the Ashikhmin visibility), a velvet rim on
    // side- and backlit faces where a smooth surface would only show a Fresnel glint; fainter on dark skin
    float invA = 1.0 / sq(0.45);
    float Dsheen = (2.0 + invA) * pow(max(1.0 - NoH * NoH, 1e-4), invA * 0.5) / (2.0 * PI);
    r += 0.04 * lerp(1.0, 0.45, sSkinMelanin) * Dsheen / (4.0 * (NoL + NoV - NoL * NoV) + 1e-4) * NoL * sh;
    // A zero thickness is ambiguous (the part is not in the shadow map, e.g. distant crowd LODs, or the point is on
    // the lit surface itself): assume 2 cm of tissue then, so nothing glows unless it is really thin and backlit.
    float t = thickness < 0.003 ? 0.02 : thickness;
    float3 transm = exp(-t / float3(0.012, 0.0045, 0.003)) * sSkinTransl * lerp(1.0, 0.5, sSkinMelanin);
    float fwd = saturate(dot(V, -L));
    r += g.albedo * transm * saturate(0.1 - NoLr) * fwd * fwd * (0.35 / PI);
    return r;
}

// Hair (Kajiya-Kay with Marschner-style shifts): strands run along the card tangent (strand cards) or the surface
// projection of "down" (the opaque shell); a white primary highlight shifted towards the root (R) and a broader
// highlight tinted by the hair colour, shifted towards the tip and sparkling per strand (TRT, sHairRnd); soft
// wrapped diffuse for the scattering hair volume. On cards the G-buffer's extra channel holds the strand's random
// and the card's depth in the hair volume (sHairDepth): light reaching an inner layer has crossed the layers above
// it, and no shadow map resolves strands, so inner layers get less of it and dimmer highlights.
static float sHairDepth = 0;   // strand cards: 0 outermost .. 1 innermost layer
static float sHairRnd = 0.5;   // per-strand random (cards) / strand noise (shell)

float3 hairDirect(GBufferData g, float3 N, float3 V, float3 L) {
    float3 T;
    if (sHairCard) {
        T = sHairT * (sHairT.z > 0.0 ? -1.0 : 1.0);   // stored sign-free: roots above tips
    } else {
        T = float3(0, 0, -1) + N * N.z;
        float tl = length(T);
        T = tl > 1e-3 ? T / tl : normalize(cross(N, float3(1, 0, 0)));
    }
    float3 H = normalize(L + V);
    float jit = (sHairRnd - 0.5) * 0.35;   // per-strand tilt: the highlight band breaks up into strands
    float3 T1 = normalize(T + N * (0.1 + jit)), T2 = normalize(T - N * (0.15 - jit));
    float h1 = dot(T1, H), h2 = dot(T2, H);
    float e1 = clamp(2.0 / max(sq(g.rough * 0.5), 1e-3) - 2.0, 8.0, 400.0);   // narrow R lobe (~6 degrees)
    float e2 = e1 * 0.3;
    float s1 = pow(sqrt(saturate(1.0 - h1 * h1)), e1) * (e1 + 2.0) / (2.0 * PI);
    float s2 = pow(sqrt(saturate(1.0 - h2 * h2)), e2) * (e2 + 2.0) / (2.0 * PI);
    float NoL = dot(N, L);
    float vis = saturate(NoL * 0.75 + 0.25);
    // cuticle reflectance stays low at grazing angles (fibres, not a smooth shell): no Schlick rim blow-up on
    // lashes, brows and the silhouette of the hair volume
    const float F = 0.05;
    // per-strand sparkle: on cards a few strands carry most of the highlight, so it breaks up instead of forming a
    // satin band (curly and coily hair especially)
    float sparkle = sHairCard ? 0.12 + 1.5 * sHairRnd * sHairRnd : 0.4 + 0.8 * sHairRnd;
    float3 spec = (s1 * F * 0.35 * sparkle + s2 * g.albedo * (0.3 + sHairRnd * 0.9) * 0.2 * lerp(1.0, 0.45, sHairDepth)) * vis;
    return (g.albedo / PI * saturate(NoL * 0.6 + 0.4) * 0.85 + spec) * lerp(1.0, 0.5, sHairDepth);
}

// Cloth: Lambert + Charlie sheen (Estevez & Kulla) with the Ashikhmin visibility; g.extra = sheen strength.
float3 clothDirect(GBufferData g, float3 N, float3 V, float3 L) {
    float NoL = saturate(dot(N, L)), NoV = max(dot(N, V), 1e-4);
    float3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float r = max(g.rough, 0.3);
    float invA = 1.0 / (r * r);
    float D = (2.0 + invA) * pow(max(1.0 - NoH * NoH, 1e-4), invA * 0.5) / (2.0 * PI);
    float Vs = 1.0 / (4.0 * (NoL + NoV - NoL * NoV) + 1e-4);
    float3 sheenC = lerp(float3(0.04, 0.04, 0.04), g.albedo, 0.6) * g.extra;
    return (g.albedo / PI + sheenC * D * Vs) * NoL;
}

float3 localLightBRDF(GBufferData g, float3 N, float3 V, float3 L) {
    // thin parts let a lamp behind them through (the transmission weight is 0 where tissue backs the skin: lids, lips)
    if (g.shadingModel == SM_SKIN) return skinDirect(g, N, V, L, 1.0, sSkinTransl > 0.0 ? lerp(0.006, 0.0015, sSkinTransl) : 1.0);
    if (g.shadingModel == SM_HAIR) return hairDirect(g, N, V, L);
    if (g.shadingModel == SM_CLOTH) return clothDirect(g, N, V, L);
    float3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    float a = max(g.rough * g.rough, 0.02);
    float3 diffColor = g.albedo * (1.0 - g.metal);
    float3 f0 = lerp(0.04, g.albedo, g.metal);
    float3 F = F_Schlick(f0, VoH);
    float3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
    float3 r = (diffColor / PI * (1.0 - F) + spec) * NoL;
    if (g.shadingModel == SM_FOLIAGE) r += diffColor * g.extra * saturate(dot(-N, L)) * 0.5 / PI;
    if (g.shadingModel == SM_CARPAINT) {
        float ca = 0.035 * 0.035;
        float Fc = 0.04 + 0.96 * pow5(1.0 - VoH);
        r += D_GGX(NoH, ca) * V_SmithGGXCorrelated(NoV, NoL, ca) * Fc * g.extra * NoL;
    }
    return r;
}

// Screen-space contact shadows: short ray march towards the sun through the depth buffer (fine detail the
// shadow cascades cannot resolve: wheels on the road, props on sidewalks, window frames, foliage clumps).
float contactShadow(float3 relPos, float viewDepth, uint2 pix) {
    float len = clamp(viewDepth * 0.012, 0.25, 2.5);
    const int steps = 10;
    float jit = ignTemporal(float2(pix), gTime.z, 5.0);
    float thickness = max(0.2, viewDepth * 0.006);
    float3 stepV = gSunDir.xyz * (len / steps);
    float3 p = relPos + stepV * jit + gSunDir.xyz * viewDepth * 0.0006;
    [loop] for (int i = 0; i < steps; i++) {
        p += stepV;
        float4 clip = mul(gViewProj, float4(p, 1));
        if (clip.w <= 0.0) break;
        float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        if (any(uv <= 0.0) || any(uv >= 1.0)) break;
        float d = tDepth.SampleLevel(sPointClamp, uv, 0);
        if (d <= 0.0) continue;
        float sceneZ = linearDepth(d);
        float rayZ = clip.w;
        float diff = rayZ - sceneZ;
        if (diff > 0.02 * (1.0 + rayZ * 0.01) && diff < thickness) return saturate((float)i / steps * 0.5);  // soften far end
    }
    return 1.0;
}

// Feature shadows on faces and hands (skin, eyes and teeth within kFeatureShadowDist): the same march at the scale
// of the features, which the shadow cascades and the contact shadows above are far too coarse for: the brow ridge
// over the upper lids and the eyes, the nose on the cheek, the lips on the teeth, the fingers. Without it an
// overhead lamp lights the upper lids as brightly as the brow, and the eyes read as goggles in a bright frame.
// 8 steps over 4.5 cm, starting a pixel off the surface; an occluder more than 4 cm in front of the ray (another
// person, a hand held up to the camera) casts nothing.
static const float kFeatureShadowDist = 12.0;
float featureShadow(float3 relPos, float3 N, float viewDepth, uint2 pix, float3 L) {
    const int steps = 8;
    float pxW = viewDepth * 2.0 * tan(gCamForward.w * 0.5) * gScreen.w;   // a pixel's size at this depth (m)
    float bias = max(0.0015, pxW * 0.5);
    float jit = ignTemporal(float2(pix), gTime.z, 7.0);
    float3 stepV = L * (0.045 / steps);
    float3 p = relPos + N * max(0.002, pxW * 0.75) + stepV * jit;
    [loop] for (int i = 0; i < steps; i++) {
        p += stepV;
        float4 clip = mul(gViewProj, float4(p, 1));
        if (clip.w <= 0.0) break;
        float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
        if (any(uv <= 0.0) || any(uv >= 1.0)) break;
        float d = tDepth.SampleLevel(sPointClamp, uv, 0);
        if (d <= 0.0) continue;
        float diff = clip.w - linearDepth(d);
        if (diff > bias && diff < 0.04) return saturate((float)i / steps * 0.6);   // the far end of the march softer
    }
    return 1.0;
}

// Eyes and teeth sit in the orbit and behind the lips: light from steeply above is cut off by the brow ridge (the
// upper lip for teeth) over 40-65 degrees of elevation, where the feature shadows find too little overhang
float eyeSocketLight(float3 L) { return lerp(1.0, 0.2, smoothstep(0.64, 0.9, L.z)); }

groupshared uint gsMinZ, gsMaxZ, gsLightCount;
groupshared uint gsLights[256];

float3 shadeSurface(GBufferData g, float3 relPos, float3 V, float3 sunE, float shadow, float ao, float3 gi, float4 ssr) {
    float3 N = g.normal;
    float3 L = gSunDir.xyz;
    float3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    float a = g.rough * g.rough;
    float3 diffColor = g.albedo * (1.0 - g.metal);
    float3 f0 = lerp(0.04, g.albedo, g.metal);
    float3 F = F_Schlick(f0, VoH);
    float3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
    float3 direct = (diffColor / PI * (1.0 - F) + spec) * NoL * sunE * shadow;
    float3 coatSpecAmb = 0;
    if (g.shadingModel == SM_SKIN) {
        // shadow-map thickness is too coarse for limbs right in front of the camera: ask for more tissue there
        float thick = dot(N, L) < 0.25 ? shadowThickness(relPos) : 1.0;
        if (thick >= 0.003) thick = max(thick, lerp(0.012, 0.004, saturate(length(relPos) / 1.5)));
        direct = skinDirect(g, N, V, L, shadow, thick) * sunE;
    } else if (g.shadingModel == SM_CLOTH) {
        direct = clothDirect(g, N, V, L) * sunE * shadow;
    } else if (g.shadingModel == SM_HAIR) {
        direct = hairDirect(g, N, V, L) * sunE * shadow;
    } else if (g.shadingModel == SM_CARPAINT) {
        // Clear coat layer over the base
        float ca = 0.035 * 0.035;
        float Fc = 0.04 + 0.96 * pow5(1.0 - VoH);
        float coat = D_GGX(NoH, ca) * V_SmithGGXCorrelated(NoV, NoL, ca) * Fc * g.extra;
        direct = direct * (1.0 - Fc * g.extra) + coat * NoL * sunE * shadow;
        // Clear coat reflects the environment sharply: screen-space hits over the probe / sky
        float Fcv = (0.04 + 0.96 * pow5(1.0 - NoV)) * g.extra;
        float3 Rc = reflect(-V, N);
        float3 coatEnv = lerp(sInterior >= 0 ? interiorReflection(sInterior, relPos, Rc, 0.03, length(relPos)) : envReflection(Rc, 0.03), ssr.rgb / preExposure(), ssr.a);
        coatSpecAmb = coatEnv * Fcv * horizonOcclusion(Rc, N);
    }
    if (g.shadingModel == SM_FOLIAGE) {
        // Leaf / frond / blade translucency: sunlight through the thin tissue, strongest looking into the sun (the
        // golden-hour glow of backlit palms and grass), tinted by the leaf (light leaves saturated yellow-green).
        // Shadowed by other geometry, not by the leaf itself: sampled beyond the leaf towards the sun.
        float back = saturate(dot(-N, L)) * 0.45 + pow(saturate(dot(V, -L)), 4.0) * 1.3;
        if (back > 0.01) {
            float shT = sampleSunShadowGeo(relPos + L * 0.4, L, dot(relPos, gCamForward.xyz), uint2(0, 0)) * cloudShadowAt(relPos);
            float3 tint = diffColor * (0.55 + diffColor * 2.2);
            direct += tint * g.extra * back * sunE * shT / PI;
        }
    }
    // Ambient: sky SH with multi-bounce AO approximation (Jimenez 2016)
    // Multi-bounce AO approximation (Jimenez 2016)
    float3 mbA = 2.0404 * diffColor - 0.3324;
    float3 mbB = -4.7951 * diffColor + 0.6417;
    float3 mbC = 2.7552 * diffColor + 0.6903;
    float3 aoMB = max(ao, ((ao * mbA + mbB) * ao + mbC) * ao);
    // skin: red light scatters out of creases and pores, so their occlusion turns warm instead of grey
    if (g.shadingModel == SM_SKIN) aoMB = pow(max(aoMB, 1e-4), lerp(1.0, float3(0.55, 0.85, 1.0), skinScatterSat()));
    // Sky/ground SH through the visibility term + one-bounce screen-space indirect diffuse (interiors: room ambient
    // + daylight through the openings)
    float3 ambIrr = sInterior >= 0 ? interiorIrradiance(sInterior, relPos, N, length(relPos)) : ambientIrradiance(N, length(relPos));
    float3 ambientDiffuse = diffColor * (ambIrr * aoMB + gi);
    if (g.shadingModel == SM_SKIN) ambientDiffuse *= lerp(1.0, float3(1.06, 0.98, 0.95), skinScatterSat());   // scattered through skin
    float3 R = reflect(-V, N);
    float2 ab = envBRDFApprox(g.rough, NoV);
    float specOcc = saturate(pow(NoV + ao, exp2(-16.0 * g.rough - 1.0)) - 1.0 + ao);
    // Environment reflection: screen-space hits where available, else the probe / sky (occluded by AO)
    float3 env = (sInterior >= 0 ? interiorReflection(sInterior, relPos, R, g.rough, length(relPos)) : envReflection(R, g.rough)) * specOcc;
    env = lerp(env, ssr.rgb / preExposure(), ssr.a);
    float3 ambientSpec = env * (f0 * ab.x + ab.y) * horizonOcclusion(R, N);
    // strands, not a mirror: the cuticle's weak white reflection plus the colour of the light that went through
    // (dark hair no longer mirrors a grey sky sheen)
    if (g.shadingModel == SM_HAIR) ambientSpec *= lerp(float3(0.07, 0.07, 0.07), g.albedo * 1.2, 0.5) * lerp(1.0, 0.5, sHairDepth);
    else if (g.shadingModel == SM_SKIN) {
        ambientSpec *= 0.7;   // F0 0.028, not 0.04
        ambientSpec += 0.04 * lerp(1.0, 0.45, sSkinMelanin) * ambIrr * aoMB * pow(1.0 - NoV, 4.0);   // peach fuzz rim
    }
    // cloth: fibres scatter ambient light forward at grazing angles (a soft sheen rim tinted by the dye), which
    // keeps clothing from reading flat and plastic in shade
    if (g.shadingModel == SM_CLOTH) ambientSpec += lerp(float3(0.04, 0.04, 0.04), g.albedo, 0.6) * g.extra * ambIrr * aoMB * pow(1.0 - NoV, 3.0) * 0.5;
    return direct + ambientDiffuse + ambientSpec + coatSpecAmb;
}

[numthreads(16, 16, 1)]
void csLighting(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
    bool inside = id.x < (uint)gScreen.x && id.y < (uint)gScreen.y;
    uint2 pix = min(id.xy, uint2(gScreen.xy) - 1);
    float depth = tDepth[pix];
    if (gi == 0) { gsMinZ = 0x7f7fffff; gsMaxZ = 0; gsLightCount = 0; }
    GroupMemoryBarrierWithGroupSync();
    if (depth > 0 && inside) {
        float lz = linearDepth(depth);
        InterlockedMin(gsMinZ, asuint(lz));
        InterlockedMax(gsMaxZ, asuint(lz));
    }
    GroupMemoryBarrierWithGroupSync();
    // Tile frustum from the four corner rays (camera at the origin)
    float tileMin = asfloat(gsMinZ), tileMax = asfloat(gsMaxZ);
    float2 t0 = (gid.xy * 16.0) * gScreen.zw, t1 = min((gid.xy * 16.0 + 16.0) * gScreen.zw, 1.0);
    float3 c00 = reconstructPos(float2(t0.x, t0.y), 1.0), c10 = reconstructPos(float2(t1.x, t0.y), 1.0);
    float3 c01 = reconstructPos(float2(t0.x, t1.y), 1.0), c11 = reconstructPos(float2(t1.x, t1.y), 1.0);
    float3 center = normalize(c00 + c10 + c01 + c11);
    float3 pl[4];
    pl[0] = normalize(cross(c00, c10));
    pl[1] = normalize(cross(c10, c11));
    pl[2] = normalize(cross(c11, c01));
    pl[3] = normalize(cross(c01, c00));
    [unroll] for (int k = 0; k < 4; k++) if (dot(pl[k], center) < 0) pl[k] = -pl[k];
    if (gsMaxZ > 0) {
        for (uint li = gi; li < gLightCount; li += 256) {
            LightGPU Lt = tLights[li];
            float vz = dot(Lt.pos, gCamForward.xyz);
            if (vz + Lt.radius < tileMin || vz - Lt.radius > tileMax) continue;
            bool vis = true;
            [unroll] for (int k2 = 0; k2 < 4; k2++) vis = vis && dot(pl[k2], Lt.pos) > -Lt.radius;
            if (!vis) continue;
            uint slot;
            InterlockedAdd(gsLightCount, 1, slot);
            if (slot < 256) gsLights[slot] = li;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (!inside) return;

    float2 uv = (id.xy + 0.5) * gScreen.zw;
    float3 relPos = reconstructPos(uv, max(depth, 1e-7));
    float3 V = -normalize(relPos);
    float4 clouds = tClouds.SampleLevel(sLinearClamp, uv, 0);
    if (depth <= 0.0) {
        float3 sky = skyRadiance(-V, true);
        sky = sky * clouds.a + clouds.rgb;
        if (gLightning.x > 0.0) {
            // lightning: clouds light up from within, strongest towards the bolt
            float toward = pow(saturate(dot(-V, gLightning.yzw)), 4.0);
            float cloudLit = 1.0 - clouds.a;
            sky += float3(0.75, 0.8, 1.0) * gLightning.x * (cloudLit * (350.0 + 2200.0 * toward) + 15.0 + 250.0 * toward);
        }
        float4 fv = froxelFog(uv, gFogParams0.w);
        float3 skyOut = min(sky * preExposure(), 60000.0) * fv.a + fv.rgb;
        int dbgS = (int)gRenderParams.w;
        if (dbgS > 0 && uv.x >= gRenderParams.y) uDebugView[id.xy] = float4(dbgS == 15 ? fv.rgb * 4.0 : (dbgS == 16 ? fv.aaa : (float3)0), 1);
        uHDR[id.xy] = float4(sanitizeHDR(skyOut), 1);
        return;
    }
    GBufferData g = unpackGBuffer(tAlbedo[id.xy], tNormal[id.xy], tMaterial[id.xy]);
    sInterior = gInteriorCount > 0 ? interiorAt(relPos) : -1;
    if (g.shadingModel == SM_HAIR) {
        sHairCard = decodeHairTangent(g.normal, g.metal, sHairT);
        g.metal = 0;
        // cards: per-strand random (high 5 bits) and depth in the hair volume (low 3 bits); the shell: strand noise
        uint e = (uint)(g.extra * 255.0 + 0.5);
        sHairRnd = sHairCard ? (e >> 3) / 31.0 : g.extra;
        sHairDepth = sHairCard ? (e & 7u) / 7.0 : 0.0;
    } else if (g.shadingModel == SM_SKIN) {
        // metal channel: scatter width; extra: melanin (high nibble) and transmission thinness (low nibble)
        uint e = (uint)(g.extra * 255.0 + 0.5);
        sSkinScatter = g.metal;
        sSkinMelanin = (e >> 4) / 15.0;
        sSkinTransl = (e & 15u) / 15.0;
        g.metal = 0;
    }
    float3 emissive = tEmissive[id.xy];
    float dist = length(relPos);
    float3 color;
    float3 sunE = mainLightIlluminance();
    float viewDepth = dot(relPos, gCamForward.xyz);
    float shadow = 1;
    float ao = 1;
    float4 aogi = float4(0, 0, 0, 1);
    float4 ssr = 0;
    if (g.shadingModel == SM_UNLIT) {
        color = g.albedo;
    } else {
        shadow = sampleSunShadow(relPos, g.normal, viewDepth, id.xy);
        // eyes: the lids' shadow and the brow's (finer than any shadow map)
        if (g.shadingModel == SM_EYE) shadow *= g.ao * eyeSocketLight(gSunDir.xyz);
        // contact shadows: not at grazing sun, where a march along the surface only finds the surface itself (the
        // shadow map covers that case)
        if (gRenderParams.z > 0.5 && shadow > 0.02 && viewDepth < 180.0 && dot(g.normal, gSunDir.xyz) > 0.2)
            shadow *= contactShadow(relPos, viewDepth, id.xy);
        // faces and hands close by: feature shadows from the sun and the strongest lamps (front-lit only: light
        // through an ear from behind is the transmission term's)
        bool featurePix = gRenderParams.z > 0.5 && (g.shadingModel == SM_SKIN || g.shadingModel == SM_EYE) &&
                          viewDepth < kFeatureShadowDist;
        if (featurePix && shadow > 0.02 && dot(g.normal, gSunDir.xyz) > 0.1)
            shadow *= featureShadow(relPos, g.normal, viewDepth, id.xy, gSunDir.xyz);
        aogi = upsampleAOGI(id.xy, linearDepth(depth), g.normal);
        ao = g.ao * aogi.a;
        ssr = gSSParams.z > 0.5 ? tSSR[id.xy] : float4(0, 0, 0, 0);
        color = shadeSurface(g, relPos, V, sunE, shadow, ao, aogi.rgb / preExposure(), ssr);
        // Local lights
        uint n = min(gsLightCount, 256u);
        float3 local = 0;
        // Submerged surfaces (seabed, pilings, hulls below the waterline): a lamp above the water reaches them only
        // through the surface (partly reflected, spread by refraction, absorbed on the way down); a lamp in the
        // water is absorbed along its path
        float3 wpos = relPos + gCamPos.xyz;
        float4 wl4 = gWaterLevelG.Gather(sPointClamp, (wpos.xy + 10240.0) / 20480.0);
        float waterZ = max(max(wl4.x, wl4.y), max(wl4.z, wl4.w));
        float submerged = waterZ > -999.0 && sInterior < 0 ? waterZ - wpos.z : 0.0;
        uint featureMarches = 0;
        for (uint i = 0; i < n; i++) {
            if (tLightVolume[gsLights[i]] != (uint)(sInterior + 1)) continue;   // lights stay in their own volume
            LightGPU Lt = tLights[gsLights[i]];
            float3 Lv = Lt.pos - relPos;
            float d2 = dot(Lv, Lv);
            if (d2 > Lt.radius * Lt.radius) continue;
            float d = sqrt(d2);
            Lv /= d;
            float x = d / Lt.radius;
            float win = saturate(1.0 - x * x * x * x);
            float att = win * win / max(d2, 0.3);
            att *= lightAngular(Lt, Lv);
            if (att <= 0) continue;
            if (submerged > 0.02) {
                bool lampAbove = Lt.pos.z + gCamPos.z > waterZ;
                att *= lampAbove ? 0.25 * exp(-0.7 * submerged / max(Lv.z, 0.2)) : exp(-0.35 * d);
            }
            float3 c = localLightBRDF(g, g.normal, V, Lv) * Lt.color * att;
            if (g.shadingModel == SM_EYE) c *= eyeSocketLight(Lv);
            // (at most two marches per pixel, for the lamps that light it visibly)
            if (featurePix && featureMarches < 2u && dot(g.normal, Lv) > 0.1 && luminance(c) * preExposure() > 0.01) {
                c *= featureShadow(relPos, g.normal, viewDepth, id.xy, Lv);
                featureMarches++;
            }
            local += c;
        }
        if (g.shadingModel == SM_EYE) local *= g.ao;
        color += local * lerp(0.6, 1.0, ao);
        if (gLightning.x > 0.0) {
            // lightning flash: sky-wide ambient burst + directional light from the bolt
            float3 diffC = g.albedo * (1.0 - g.metal);
            float3 flashE = float3(0.75, 0.8, 1.0) * gAmbientParams.y * (sInterior >= 0 ? 0.1 : 1.0);
            color += diffC / PI * flashE * ((0.5 + 0.5 * g.normal.z) * ao * 0.6 + saturate(dot(g.normal, gLightning.yzw)) * 0.6);
        }
    }
    int dbg = (int)gRenderParams.w;
    if (dbg > 0 && uv.x >= gRenderParams.y) {
        float3 o = 0;
        if (dbg == 1) o = g.albedo;
        else if (dbg == 2) o = g.normal * 0.5 + 0.5;
        else if (dbg == 3) o = ambientIrradiance(g.normal, dist) * preExposure() * 4.0;
        else if (dbg == 4) o = shadow;
        else if (dbg == 5) o = float3(g.rough, g.metal, g.ao);
        else if (dbg == 6) o = color * preExposure();
        else if (dbg == 7) o = aerialPerspective(uv, dist).a;
        else if (dbg == 8) o = aerialPerspective(uv, dist).rgb * preExposure() * 10.0;
        else if (dbg == 9) o = sunE * preExposure();
        else if (dbg == 10) o = min(gsLightCount, 64u) / 64.0;
        else if (dbg == 11) o = aogi.a;
        else if (dbg == 12) o = aogi.rgb * 4.0;
        else if (dbg == 13) o = lerp(float3(0.02, 0.0, 0.03), ssr.rgb, ssr.a);
        else if (dbg == 14) o = ssr.a;
        else if (dbg == 15) o = froxelFog(uv, viewDepth).rgb * 4.0;
        else if (dbg == 16) o = froxelFog(uv, viewDepth).a;
        else if (dbg == 17 || dbg == 18) {
            // Reflection probe as an equirectangular map over the screen (azimuth across, elevation +90 at the top
            // to -90 at the bottom of each half; geometry pixels only, so aim the camera at the ground). 17: left
            // half the capture (mip 0), right half the filtered mip 2 that the SH is projected from. 18: left half
            // the probe SH irradiance, right half the sky SH irradiance (x4).
            float2 e = float2(frac(uv.x * 2.0), uv.y);
            float az = e.x * TWO_PI, el = (0.5 - e.y) * PI;
            float3 d = float3(cos(el) * cos(az), cos(el) * sin(az), sin(el));
            if (dbg == 17) o = gEnvProbeTex.SampleLevel(sLinearClamp, d, uv.x < 0.5 ? 0.0 : 2.0).rgb * preExposure();
            else o = (uv.x < 0.5 ? evalSH9From(gProbeSH, d) : evalSH9(d)) * preExposure() * 4.0;
        }
        else if (dbg == 19) {
            // (debug) overhead map at this pixel: none (dark blue), at or below the terrain (black), lawn / bed level
            // (green), 0.08-0.6 m up (yellow), 0.6-2 m (orange), above (red); the class: bright = lawn, mid = soil
            float3 wpd = relPos + gCamPos.xyz;
            float th = gOverheadMap.SampleLevel(sPointClamp, overheadUV(wpd.xy), 0);
            float tg = gTerrainHeightG.SampleLevel(sLinearClamp, (wpd.xy + 10240.0) / 20480.0, 0);
            float dh = th - tg;
            o = th < -900.0 ? float3(0, 0, 0.3) : (dh < -0.35 ? float3(0, 0, 0) : (dh < 0.08 ? float3(0, 0.8, 0) : (dh < 0.6 ? float3(1, 1, 0) : (dh < 2.0 ? float3(1, 0.5, 0) : float3(1, 0, 0)))));
        }
        if (any(isnan(o))) o = float3(1, 0, 1);
        uDebugView[id.xy] = float4(o, 1);
    }
    float4 ap = aerialPerspective(uv, dist);
    color = color * ap.a + ap.rgb;
    // Emitters are capped in absolute radiance (neon / lamps at night up to 20000 nits, by day no sign or panel
    // exceeds 2500 nits), lit surfaces are capped before bloom (sun glints stay bright without flooding the frame)
    float emissiveCap = lerp(2500.0, 20000.0, gSkyGlow.w) * preExposure();
    float3 outC = min(color * preExposure(), 8000.0) + min(emissive, emissiveCap) * ap.a;
    float4 fv = froxelFog(uv, viewDepth);
    uHDR[id.xy] = float4(sanitizeHDR(outC * fv.a + fv.rgb), 1);
}
