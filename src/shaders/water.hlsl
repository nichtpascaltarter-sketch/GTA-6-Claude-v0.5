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

cbuffer WaterCB : register(b1) {
    float4 gWaterParams;   // x world half, y heightmap texel (m), z grid resolution, w wave strength (weather)
    float4 gMorph[16];     // per lod: morph start, end
    float4 gWaterMode;     // x: 0 = CDLOD world grid, 1 = ocean skirt; y: skirt inner radius; z: skirt outer; w: time scale
    float4 gWaterRefl;     // x screen-space reflections enabled, y max iterations, z HiZ max mip
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
    return normalize(float3(n, 1.0));
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
    // Absorption & scattering (tropical: turquoise shallows, deep blue offshore; greener in rivers/lake)
    bool fresh = i.level > 1.0;
    float3 absorb = fresh ? float3(0.45, 0.2, 0.25) : float3(0.38, 0.09, 0.07);
    float3 scatterCol = fresh ? float3(0.03, 0.06, 0.04) : float3(0.012, 0.05, 0.065);
    float murk = saturate(i.depth / 6.0);
    float3 trans = exp(-absorb * viewThick * (fresh ? 1.6 : 1.0));
    // Inscatter lit by sun + sky
    float3 sunE = mainLightIlluminance();
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
    float viewDepth = dot(i.rel, gCamForward.xyz);
    float shadow = sampleSunShadow(i.rel, float3(0, 0, 1), viewDepth, (uint2)i.pos.xy);
    float3 glint = sunE * spec * F * shadow * preExposure();
    // Foam: shoreline (thin water) + wave crests
    float3 foamTex = tWaveNormals.Sample(sLinearWrap, i.world / 6.0 + t * 0.01).zzz;
    float shore = saturate(1.0 - thickness / 0.8) * saturate(1.0 - i.depth / 1.5 + 0.5);
    float crest = saturate((gn.z < 0.9 ? (0.9 - gn.z) * 6.0 : 0.0)) * saturate(amp - 0.3);
    float foam = saturate((shore * 0.9 + crest) * foamTex.x * 1.6);
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
