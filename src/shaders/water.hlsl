// Water surface: ocean, bay, rivers, canals and the lake. Forward-shaded into the HDR buffer.
#include "reflection.hlsli"
#include "shadow.hlsli"
#include "ssrtrace.hlsli"

Texture2D<float> tHeight : register(t0);
Texture2D<float> tWaterLevel : register(t1);
Texture2D<float4> tSceneColor : register(t2);   // HDR copy (pre-exposed) for refraction
Texture2D<float> tSceneDepth : register(t3);
Texture2D<float4> tWaveNormals : register(t4);  // tileable wave normal map (xy normal, z foam mask, w height)
Texture2D<float2> tHiZ : register(t5);           // depth pyramid of the opaque scene (screen-space reflections)
StructuredBuffer<float4> tNodes : register(t6);
Texture2D<float2> tWake : register(t7);           // boat wakes around the camera: x foam, y height (m)

cbuffer WaterCB : register(b1) {
    float4 gWaterParams;   // x world half, y heightmap texel (m), z grid resolution, w wave strength (weather)
    float4 gMorph[16];     // per lod: morph start, end
    float4 gWaterMode;     // x: 0 = CDLOD world grid, 1 = ocean skirt; y: skirt inner radius; z: skirt outer; w: time scale
    float4 gWaterRefl;     // x screen-space reflections enabled, y max iterations, z HiZ max mip
    float4 gWake;          // xy wake map origin (world), z size (m), w 1 = wakes present
};

// Screen-space reflection of the opaque scene on the water surface (current frame, pre-exposed).
float4 waterSSR(float3 relPos, float3 R) {
    if (gWaterRefl.x < 0.5) return 0;
    ScreenRay ray = makeScreenRay(relPos, R, 1500.0);
    float3 hit;
    float iterFrac;
    if (!traceHiZ(tHiZ, ray, gHalfScreen.xy, (int)gWaterRefl.z, (int)gWaterRefl.y, hit, iterFrac)) return 0;
    if (any(hit.xy <= 0.0) || any(hit.xy >= 1.0)) return 0;
    float hd = tSceneDepth.SampleLevel(sPointClamp, hit.xy, 0);
    if (hd <= 0.0) return 0;
    float zr = linearDepth(max(hit.z, 1e-7)), zs = linearDepth(hd);
    if (abs(zr - zs) > max(0.4, zs * 0.04)) return 0;
    float2 edge = saturate(min(hit.xy, 1.0 - hit.xy) / float2(0.06, 0.12));
    float conf = edge.x * edge.y * saturate((1.0 - iterFrac) * 4.0);
    return float4(tSceneColor.SampleLevel(sLinearClamp, hit.xy, 0).rgb, conf);
}

struct VSOut {
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;
    float2 world : TEXCOORD1;
    float depth : TEXCOORD2;     // water column depth at vertex (m)
    float level : TEXCOORD3;
    float4 curClip : TEXCOORD4;
};

float2 worldToUVw(float2 w) { return (w + gWaterParams.x) / (2.0 * gWaterParams.x); }

// Gerstner wave set; amplitude scaled by `amp` (0..1)
static const int kWaves = 7;
static const float4 kWaveDef[kWaves] = {   // xy direction (normalized in use), z wavelength (m), w relative amplitude
    float4(1.0, 0.35, 64.0, 1.0), float4(0.8, -0.6, 41.0, 0.7), float4(-0.3, 1.0, 27.0, 0.45), float4(0.95, 0.1, 17.0, 0.3),
    float4(0.2, -1.0, 11.0, 0.2), float4(-0.7, 0.7, 7.0, 0.12), float4(0.6, 0.8, 4.3, 0.07)};

float3 gerstner(float2 p, float amp, float t, out float3 nrm) {
    float3 disp = 0;
    float3 dx = float3(1, 0, 0), dy = float3(0, 1, 0);
    [unroll] for (int i = 0; i < kWaves; i++) {
        float2 d = normalize(kWaveDef[i].xy + gWind.xy * 0.4);
        float L = kWaveDef[i].z;
        float k = TWO_PI / L;
        float c = sqrt(9.81 / k);
        float a = kWaveDef[i].w * amp * 0.55 * (L / 64.0 + 0.35);
        float q = 0.55 / (k * a * kWaves + 1e-4);
        float ph = k * (dot(d, p) - c * t);
        float s, co;
        sincos(ph, s, co);
        disp += float3(d * (q * a * co), a * s);
        float wa = k * a;
        dx += float3(-q * d.x * d.x * wa * s, -q * d.x * d.y * wa * s, d.x * wa * co);
        dy += float3(-q * d.x * d.y * wa * s, -q * d.y * d.y * wa * s, d.y * wa * co);
    }
    nrm = normalize(cross(dx, dy));
    return disp;
}

float waveAmplitude(float depth) {
    return saturate(depth / 18.0) * (0.35 + gWaterParams.w * 1.6) + 0.03;
}

VSOut vsWater(float2 grid : POSITION, uint inst : SV_InstanceID) {
    VSOut o;
    float2 w;
    float level = 0;
    float depth = 50;
    if (gWaterMode.x < 0.5) {
        float4 n = tNodes[inst];
        float N = gWaterParams.z;
        w = n.xy + grid * n.z;
        float3 relP = float3(w - gCamPos.xy, -gCamPos.z);
        float dist = length(relP);
        int lod = (int)n.w;
        float2 mr = gMorph[lod].xy;
        float morph = saturate((dist - mr.x) / max(mr.y - mr.x, 1e-3));
        float2 g = grid * N;
        g -= frac(g * 0.5) * 2.0 * morph;
        w = n.xy + (g / N) * n.z;
        float2 uv = worldToUVw(w);
        // max water level of the 4 nearest texels avoids slopes at lake shores
        float4 wl = tWaterLevel.Gather(sPointClamp, uv);
        level = max(max(wl.x, wl.y), max(wl.z, wl.w));
        if (level < -900.0) level = 0.0;
        float h = tHeight.SampleLevel(sLinearClamp, uv, 0);
        depth = level - h;
    } else {
        // Ocean skirt: ring from the world edge to the horizon, grid.x = angle fraction, grid.y = radial fraction
        float ang = grid.x * TWO_PI;
        float r = lerp(gWaterMode.y, gWaterMode.z, grid.y * grid.y);
        float2 dir = float2(cos(ang), sin(ang));
        // square-ish inner edge matching the world bounds
        float sq = gWaterMode.y / max(abs(dir.x), abs(dir.y));
        r = lerp(sq, gWaterMode.z, grid.y * grid.y);
        w = dir * r;
        level = 0;
        depth = 60;
    }
    float amp = waveAmplitude(depth);
    float3 wn;
    float t = gTime.x * gWaterMode.w;
    float3 disp = gerstner(w, amp, t, wn);
    float fade = saturate(1.0 - length(w - gCamPos.xy) / 3000.0);
    disp *= fade;
    float3 rel = float3(w + disp.xy - gCamPos.xy, level + disp.z - gCamPos.z);
    o.rel = rel;
    o.world = w + disp.xy;
    o.depth = depth;
    o.level = level;
    o.pos = mul(gViewProj, float4(rel, 1));
    o.curClip = mul(gViewProjNoJitter, float4(rel, 1));
    return o;
}

float3 sampleWaveNormal(float2 w, float t, float dist) {
    float2 wind = normalize(gWind.xy + float2(0.001, 0));
    float2 uvA = w / 23.0 + wind * t * 0.021;
    float2 uvB = w / 9.0 + float2(-wind.y, wind.x) * t * 0.037;
    float2 uvC = w / 3.1 + wind * t * 0.06;
    float3 a = tWaveNormals.Sample(sLinearWrap, uvA).xyz * 2.0 - 1.0;
    float3 b = tWaveNormals.Sample(sLinearWrap, uvB).xyz * 2.0 - 1.0;
    float3 c = tWaveNormals.Sample(sLinearWrap, uvC).xyz * 2.0 - 1.0;
    float detail = saturate(1.0 - dist / 400.0);
    float2 n = a.xy * 0.6 + b.xy * 0.5 + c.xy * 0.35 * detail;
    // Far field: two much larger, rotated scales blended by a slow noise, and the near scales fading out, so the
    // 23 m tile never repeats visibly towards the horizon
    float far = saturate((dist - 150.0) / 900.0);
    if (far > 0.0) {
        float2 r1 = float2(w.x * 0.8 - w.y * 0.6, w.x * 0.6 + w.y * 0.8);
        float2 r2 = float2(w.x * 0.34 + w.y * 0.94, -w.x * 0.94 + w.y * 0.34);
        float3 d = tWaveNormals.Sample(sLinearWrap, r1 / 97.0 + wind * t * 0.008).xyz * 2.0 - 1.0;
        float3 e = tWaveNormals.Sample(sLinearWrap, r2 / 263.0 - wind * t * 0.004).xyz * 2.0 - 1.0;
        float mixN = valueNoise(w / 700.0);
        float2 nf = lerp(d.xy, e.xy, mixN) * 0.8 + a.xy * 0.25;
        n = lerp(n, nf, far);
    }
    return normalize(float3(n, 1.0));
}

// Animated caustics on the sea floor (two warped wave interference patterns), 0..~2.
float caustics(float2 p, float t) {
    float2 q = p * 1.7;
    float2 wq = q + float2(sin(q.y * 1.3 + t * 0.9), cos(q.x * 1.1 - t * 0.8)) * 0.45;
    float c1 = 0.5 + 0.5 * sin(wq.x * 2.3 + sin(wq.y * 1.9 + t * 0.7) * 1.2);
    float2 wr = q * 1.37 + float2(cos(q.y * 0.9 - t * 0.6), sin(q.x * 1.4 + t * 0.75)) * 0.5;
    float c2 = 0.5 + 0.5 * sin(wr.y * 2.1 + sin(wr.x * 1.7 - t * 0.9) * 1.3);
    return pow(c1, 8.0) + pow(c2, 8.0);
}

float4 psWater(VSOut i) : SV_Target {
    float2 screenUV = i.pos.xy * gScreen.zw;
    float3 V = normalize(-i.rel);
    float dist = length(i.rel);
    float t = gTime.x * gWaterMode.w;
    // Geometric normal from waves (recomputed per pixel for crisp highlights) + detail normal maps
    float amp = waveAmplitude(i.depth);
    float3 gn;
    gerstner(i.world, amp * saturate(1.0 - dist / 3000.0), t, gn);
    float3 dn = sampleWaveNormal(i.world, t, dist);
    float calm = saturate(1.0 - amp);  // calm rivers/bay: smoother
    float3 N = normalize(float3(gn.xy + dn.xy * lerp(0.35, 0.18, calm), gn.z));
    // Boat wakes: Kelvin wave pattern (height -> normal) and foam trails from the wake map
    float wakeFoam = 0;
    if (gWake.w > 0.5) {
        float2 wuv = (i.world - gWake.xy) / gWake.z;
        if (all(wuv > 0.0) && all(wuv < 1.0)) {
            float texel = gWake.z / 512.0;
            float2 w0 = tWake.SampleLevel(sLinearClamp, wuv, 0);
            float hx = tWake.SampleLevel(sLinearClamp, wuv + float2(1.0 / 512.0, 0), 0).y - w0.y;
            float hy = tWake.SampleLevel(sLinearClamp, wuv + float2(0, 1.0 / 512.0), 0).y - w0.y;
            float edgeFade = saturate(min(min(wuv.x, 1.0 - wuv.x), min(wuv.y, 1.0 - wuv.y)) * 12.0);
            N = normalize(N + float3(-hx, -hy, 0) / texel * 1.5 * edgeFade);
            wakeFoam = saturate(w0.x) * edgeFade;
        }
    }
    // Distance: flatten normals to reduce aliasing
    N = normalize(lerp(N, float3(0, 0, 1), saturate(dist / 2500.0) * 0.7));

    // Scene behind the water
    float sceneD = tSceneDepth.SampleLevel(sPointClamp, screenUV, 0);
    float sceneDist = sceneD > 0 ? linearDepth(sceneD) : 1e6;
    float waterDist = linearDepth(i.pos.z);
    float thickness = max(sceneDist - waterDist, 0.0);
    // Refraction offset scaled by thickness
    float2 refrUV = screenUV + N.xy * 0.03 * saturate(thickness / 4.0) * saturate(1.0 - dist / 200.0);
    float refrD = tSceneDepth.SampleLevel(sPointClamp, refrUV, 0);
    if (refrD > 0 && linearDepth(refrD) < waterDist) refrUV = screenUV;  // don't refract foreground objects
    float3 refr = tSceneColor.SampleLevel(sLinearClamp, refrUV, 0).rgb;
    float viewThick = max((refrD > 0 ? linearDepth(tSceneDepth.SampleLevel(sPointClamp, refrUV, 0)) : 1e6) - waterDist, 0.0);
    bool fresh = i.level > 1.0;
    float3 sunE = mainLightIlluminance();
    float viewDepth = dot(i.rel, gCamForward.xyz);
    float shadow = sampleSunShadow(i.rel, float3(0, 0, 1), viewDepth, (uint2)i.pos.xy);
    // Sun caustics on a shallow sandy floor: focused light lines, strongest in the shallows and in direct sun
    if (!fresh && i.depth < 6.0) {
        float cz = caustics(i.world + N.xy * 0.6, t) * saturate(1.0 - i.depth / 6.0) * saturate(i.depth * 2.0);
        refr *= 1.0 + cz * 0.45 * shadow * saturate(gSunDir.z * 3.0);
    }
    // Absorption & scattering (tropical: bright turquoise over sand in the shallows, deep blue offshore; greener in
    // rivers and the lake)
    float3 absorb = fresh ? float3(0.45, 0.2, 0.25) : float3(0.42, 0.075, 0.06);
    float3 scatterCol = fresh ? float3(0.03, 0.06, 0.04) : lerp(float3(0.02, 0.09, 0.085), float3(0.012, 0.05, 0.065), saturate(i.depth / 8.0));
    float murk = saturate(i.depth / 6.0);
    float3 trans = exp(-absorb * viewThick * (fresh ? 1.6 : 1.0));
    // Inscatter lit by sun + sky
    float3 skyE = evalSH9(float3(0, 0, 1)) * PI;
    float3 inscatter = scatterCol * (sunE * saturate(gSunDir.z) * 0.08 + skyE * 0.12) / PI;
    float3 underwater = refr * trans + inscatter * preExposure() * (1.0 - trans);
    // Reflection: screen-space hits (city skyline, boats, piers) over the environment probe / sky
    float3 R = reflect(-V, N);
    R.z = abs(R.z);
    R = normalize(R);
    float3 refl = envReflection(R, 0.02) * preExposure();
    float4 ssrW = waterSSR(i.rel, R);
    refl = lerp(refl, ssrW.rgb, ssrW.a);
    float NoV = saturate(dot(N, V));
    float F = 0.02 + 0.98 * pow5(1.0 - NoV);
    // Sun glint (GGX, low roughness)
    float3 L = gSunDir.xyz;
    float3 H = normalize(L + V);
    float rough = lerp(0.06, 0.12, saturate(dist / 1500.0));
    float spec = D_GGX(saturate(dot(N, H)), rough * rough) * V_SmithGGXCorrelated(NoV, saturate(dot(N, L)), rough * rough) * saturate(dot(N, L));
    float3 glint = sunE * spec * F * shadow * preExposure();
    // Foam: shoreline (thin water) + wave crests + breaking lines rolling in over the shallows (bands of constant
    // depth that move shoreward, broken up along the beach, dissolving as they reach the swash zone)
    float3 foamTex = tWaveNormals.Sample(sLinearWrap, i.world / 6.0 + t * 0.01).zzz;
    float shore = saturate(1.0 - thickness / 0.8) * saturate(1.0 - i.depth / 1.5 + 0.5);
    float crest = saturate((gn.z < 0.9 ? (0.9 - gn.z) * 6.0 : 0.0)) * saturate(amp - 0.3);
    float breakers = 0;
    if (!fresh && i.depth < 3.0) {
        float waves = 0.4 + 0.6 * saturate(gWaterParams.w + gWind.z * 0.5);
        float band = pow(0.5 + 0.5 * sin(TWO_PI * (i.depth / 1.15 + t * 0.11) + valueNoise(i.world * 0.05) * 5.0), 10.0);
        float along = saturate(valueNoise(i.world * 0.11 + t * 0.05) * 1.8 - 0.35);
        breakers = band * along * saturate(1.0 - i.depth / 3.0) * saturate(i.depth * 3.0) * waves;
    }
    float foam = saturate((shore * 0.9 + crest + breakers * 1.3) * foamTex.x * 1.6 + wakeFoam * lerp(0.55, 1.0, foamTex.x));
    float3 foamCol = (sunE * saturate(gSunDir.z) * shadow + skyE) * 0.8 / PI * preExposure();
    float3 col = lerp(underwater, refl, F) + glint;
    col = lerp(col, foamCol, foam);
    // Aerial perspective + volumetric fog
    float4 ap = aerialPerspective(screenUV, dist);
    col = col * ap.a + ap.rgb * preExposure();
    float4 fv = froxelFog(screenUV, dot(i.rel, gCamForward.xyz));
    col = col * fv.a + fv.rgb;
    // Soft edge where water meets the shore (avoid hard line)
    float edge = saturate(thickness / 0.15);
    return float4(min(col, 60000.0), edge);
}

// ------------------------------------------------------------------------------------------------
// Boat wake ribbons rendered into the wake map (additive): one quad strip per boat trail, widening with the Kelvin
// wedge (19.5 degrees) behind the boat. Output x foam, y height (m).
struct WakeVSIn {
    float4 pos : POSITION;     // xy map-local position (m), z lateral offset (m, signed), w distance behind the boat (m)
    float4 info : TEXCOORD0;   // x age (s), y wedge half-width here (m), z amplitude (0..1, speed), w beam (m)
    float2 world : TEXCOORD1;  // world xy (wrapped) for stable noise
};
struct WakeVSOut {
    float4 pos : SV_Position;
    float2 latS : TEXCOORD0;
    float4 info : TEXCOORD1;
    float2 world : TEXCOORD2;
};
WakeVSOut vsWake(WakeVSIn i) {
    WakeVSOut o;
    float2 uv = i.pos.xy / gWake.z;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0);
    o.latS = i.pos.zw;
    o.info = i.info;
    o.world = i.world;
    return o;
}
float2 psWake(WakeVSOut i) : SV_Target {
    float x = abs(i.latS.x), sB = i.latS.y;
    float age = i.info.x, W = max(i.info.y, 0.1), amp = i.info.z, beam = i.info.w;
    float fromArm = W - x;
    // divergent waves: crests along the wedge arms, strongest just inside them, spacing growing downstream
    float lambda = 2.2 + 0.06 * sB;
    float div = sin(TWO_PI * fromArm / lambda) * exp(-max(fromArm, 0.0) / (5.0 + 0.15 * sB)) * saturate(fromArm / 0.8 + 1.0);
    // transverse waves across the wedge
    float trans = sin(TWO_PI * sB / (5.0 + amp * 9.0)) * saturate(1.0 - x / W) * 0.45;
    float fade = exp(-age / 16.0) * saturate(sB / 3.0);
    float height = amp * (div + trans) * fade * 0.28;
    // foam: churned centre trail + thin white lines on the arms right behind the boat, broken up by noise
    float n = valueNoise(i.world * 1.3) * 0.6 + valueNoise(i.world * 4.1) * 0.4;
    float core = exp(-sq(x / (beam * 0.9 + 0.05 * sB))) * exp(-age / 11.0) * saturate(n * 1.6 - 0.1);
    float arm = exp(-sq(fromArm / 0.7)) * exp(-age / 3.5) * saturate(n * 2.0 - 0.4);
    return float2((core + arm * 0.7) * amp, height);
}

// ------------------------------------------------------------------------------------------------
// Wave normal map generation: tileable sum of directional waves + noise; z = foam mask, w = height.
RWTexture2D<float4> uWaveOut : register(u0);
[numthreads(8, 8, 1)]
void csWaveNormals(uint3 id : SV_DispatchThreadID) {
    const float size = 512.0;
    float2 uv = (id.xy + 0.5) / size;
    float h = 0, hx = 0, hy = 0;
    [loop] for (int k = 0; k < 24; k++) {
        float fk = (float)k;
        float2 f = float2(round(cos(fk * 2.39996) * (3.0 + fk * 1.3)), round(sin(fk * 2.39996) * (3.0 + fk * 1.3)));
        if (dot(f, f) < 1.0) f = float2(1, 2);
        float a = 1.0 / (1.0 + length(f) * 0.35);
        float ph = TWO_PI * (dot(f, uv) + hashF((uint)k * 7u + 3u));
        h += a * sin(ph);
        hx += a * cos(ph) * TWO_PI * f.x;
        hy += a * cos(ph) * TWO_PI * f.y;
    }
    float3 n = normalize(float3(-hx * 0.004, -hy * 0.004, 1.0));
    // foam cells (tileable worley-like)
    float2 p = uv * 16.0;
    float2 ip = floor(p), fp = frac(p);
    float best = 8.0;
    [loop] for (int y = -1; y <= 1; y++)
    [loop] for (int x = -1; x <= 1; x++) {
        float2 c = ip + float2(x, y);
        float2 cw = c - floor(c / 16.0) * 16.0;
        float2 o = float2(hashF(hash2u((uint2)cw)), hashF(hash2u((uint2)cw) + 1u));
        best = min(best, length(float2(x, y) + o - fp));
    }
    float foam = saturate(1.0 - best * 1.3);
    foam = pow(foam, 0.7);
    uWaveOut[id.xy] = float4(n.xy * 0.5 + 0.5, foam, h * 0.1 + 0.5);
}
