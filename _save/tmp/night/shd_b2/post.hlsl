// Post processing: auto exposure, tonemapping, color grading, output.
#include "common.hlsli"
#include "skycommon.hlsli"

Texture2D<float4> tHDR : register(t0);
Texture2D<float4> tBloom : register(t1);
RWStructuredBuffer<uint> uLumHist : register(u0);   // 2 x 64-bin log2 luminance histograms: all pixels, non-sky
RWStructuredBuffer<float4> uExposure : register(u1);

cbuffer PostCB : register(b1) {
    float4 gPost0;  // x exposure compensation EV, y adaptation speed up, z speed down, w dt
    float4 gPost1;  // x bloom strength, y vignette, z grain, w saturation
    float4 gPost2;  // x contrast, y warmth, z min EV, w max EV
    float4 gPost3;  // x local exposure highlight scale (1 = off), y camera cut, z sharpen, w local exposure pivot (log2)
    // Gameplay screen effects (Renderer::postFx)
    float4 gFx0;    // x saturation, y vignette, z chromatic aberration, w flash
    float4 gFx1;    // rgb tint, w blur
    float4 gFx2;    // rgb vignette color, w underwater
    float4 gFx3;    // rgb flash color, w extra grain
    float4 gCb0;    // colour-blind correction matrix rows (xyz), gCb0.w = enabled
    float4 gCb1;
    float4 gCb2;
    float4 gShaft;  // crepuscular rays: xy sun position (uv), z strength (0 = off)
};
cbuffer BloomCB : register(b2) {
    float4 gBloom;  // xy dst size, z first level (karis), w upsample radius (local exposure: xy grid size in tiles)
};
Texture2D<float> tSceneDepth : register(t5);
Texture2D<float4> tDebugView : register(t6);   // --debugview values (lighting pass), shown in the debug region
Texture2D<float4> tShafts : register(t2);   // crepuscular ray radiance (pre-exposed, signed), quarter resolution

// Auto exposure from luminance histograms (GTA-style metering): a trimmed geometric mean of all pixels for the
// mid-tones, plus a gentle highlight constraint from the non-sky pixels (97th percentile: sunlit streets and
// facades) that pulls towards keeping them below the tonemapper's shoulder. The sky may clip; the time-of-day
// exposure floor (gPost2.z) stops a dark surface in front of the camera from opening the exposure to night levels.
static const float kHistMin = -12.0;   // log2 luminance of bin 0 (cd/m2)
static const float kHistScale = 2.0;   // bins per log2 unit (64 bins cover -12 .. +20)

// 1) Histogram: each 16x16 group bins a 64x64 region (4x4 subsample), center / lower-screen weighted.
groupshared uint gsHist[128];
[numthreads(16, 16, 1)]
void csLumHist(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    if (gi < 128) gsHist[gi] = 0;
    GroupMemoryBarrierWithGroupSync();
    uint2 base = gid.xy * 64 + tid.xy * 4;
    if (base.x < (uint)gScreen.x && base.y < (uint)gScreen.y) {
        float2 uv = (base + 2.0) * gScreen.zw;
        float3 c = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
        float lum = luminance(c) / max(gExposureBuf[0].x, 1e-12);
        float2 d = uv - 0.5;
        float w = lerp(0.35, 1.0, saturate(1.0 - dot(d, d) * 2.5));
        w *= lerp(0.6, 1.0, smoothstep(0.05, 0.55, uv.y));   // the upper screen (mostly sky) meters a bit less
        if (!(lum >= 0.0) || lum > 1e9) w = 0.0;               // NaN / Inf guard
        if (gRenderParams.w > 0.5 && uv.x >= gRenderParams.y) w = 0.0;   // debug view area does not drive exposure
        if (w > 0.0) {
            uint bin = (uint)clamp((log2(max(lum, 1e-6)) - kHistMin) * kHistScale, 0.0, 63.0);
            uint wq = (uint)(w * 64.0 + 0.5);
            InterlockedAdd(gsHist[bin], wq);
            if (tSceneDepth.SampleLevel(sPointClamp, uv, 0) > 0.0) InterlockedAdd(gsHist[64 + bin], wq);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < 128 && gsHist[gi] > 0) InterlockedAdd(uLumHist[gi], gsHist[gi]);
}

float histLog(float bin) { return (bin + 0.5) / kHistScale + kHistMin; }

// Local exposure (night): the eye adapts to a lit room seen through a window, a camera does not. Large bright areas
// (a lit lobby or shop, a floodlit wall) are compressed towards the street's exposure while keeping their own
// detail, like the local exposure of film scanners and UE5: the image's log luminance goes into a bilateral grid
// (Chen et al. 2007; 32x32 pixel tiles x 16 luminance bins), which gives every pixel the smooth "base" luminance of
// the region it belongs to without bleeding across edges (a dark window frame keeps its own base); the base above a
// pivot is scaled by gPost3.x (1 = off), detail within a region is kept. Small bright things (lamps, neon) blend
// towards the tile neighbourhood's mean, so they stay bright points with their bloom.
// gPost3.w: pivot, log2 of the pre-exposed luminance where the compression starts.
static const float kLxMin = -10.0;    // log2 pre-exposed luminance at the bottom of bin 0
static const float kLxStep = 1.25;    // log2 units per bin
static const float kLxBins = 16.0;
static const float kLxMeanBlend = 0.45;   // share of the tile neighbourhood's mean in the base
// The compression on the histogram's log values (absolute log2 luminance) for the metering, matching the image the
// player sees: a lit window filling part of the frame no longer darkens the street around it.
static const float kTS = 0.4, kTP = -2.0;
float lxCompressAbs(float l, float pivotAbs) { return l > pivotAbs ? pivotAbs + (l - pivotAbs) * kTS : l; }

// 2) Metering + adaptation (clears the histogram for the next frame). gPost3.y > 0.5: camera cut, snap exposure.
[numthreads(1, 1, 1)]
void csExposure() {
    float h[64], hs[64];
    float total = 0, totalS = 0;
    [unroll] for (int b = 0; b < 64; b++) {
        h[b] = (float)uLumHist[b];
        hs[b] = (float)uLumHist[64 + b];
        total += h[b];
        totalS += hs[b];
        uLumHist[b] = 0;
        uLumHist[64 + b] = 0;
    }
    float4 prev = uExposure[0];
    if (total <= 0.0) return;
    // local exposure (gPost3.x < 1): the metering sees the bright regions compressed as the player does
    float pivotAbs = kTP - log2(max(prev.x, 1e-12));
    bool lx = gPost3.x < 0.999 && prev.w > 0.5;
    // trimmed geometric mean between the 8th and 94th percentiles
    float lo = total * 0.08, hi = total * 0.94;
    float cum = 0, s = 0, ws = 0;
    [unroll] for (int k = 0; k < 64; k++) {
        float a = cum, bnd = cum + h[k];
        float take = max(min(bnd, hi) - max(a, lo), 0.0);
        s += (lx ? lxCompressAbs(histLog((float)k), pivotAbs) : histLog((float)k)) * take;
        ws += take;
        cum = bnd;
    }
    float avgLog = ws > 0.0 ? s / ws : histLog(31.0);
    float avgLum = exp2(avgLog);
    float targetEV = log2(max(avgLum, 1e-4) * 100.0 / 12.5) - gPost0.x;
    // Night keeps reading as night: the metering key falls with the scene's luminance (Krawczyk et al. 2005, the
    // "auto key", taken three quarters of the way), so a park or a street at night (metering 0.2-0.6 cd/m2) is not
    // lifted to the mid-grey of a day scene. Only ever darker, by at most 1 EV: anything metering above ~1.3 cd/m2
    // (day, dusk, lit interiors) is untouched, and the lamps' pools stay well above the filmic toe.
    float key = 1.03 - 2.0 / (2.0 + log10(avgLum + 1.0));
    targetEV += clamp(0.75 * log2(0.18 / max(key, 0.01)), 0.0, 1.0);
    // highlight constraint (non-sky): the 97th percentile should land at or below ~3.5 (pre-exposed), where the
    // filmic curve still shows texture; the mid-tones move a third of the way towards that, two thirds when the
    // bright part is large (even the 88th percentile above the shoulder: a lit lobby or shop front filling part of
    // the frame at night keeps its detail instead of burning out)
    if (totalS > total * 0.05) {
        float target97 = totalS * 0.97, target88 = totalS * 0.88, cumS = 0, p97 = histLog(63.0), p88 = histLog(63.0);
        bool found = false, found88 = false;
        [unroll] for (int k2 = 0; k2 < 64; k2++) {
            float a2 = cumS;
            cumS += hs[k2];
            float within = hs[k2] > 0.0 ? 1.0 / hs[k2] : 0.0;
            if (!found88 && cumS >= target88) {
                p88 = histLog((float)k2) + ((target88 - a2) * within - 0.5) / kHistScale;
                found88 = true;
            }
            if (!found && cumS >= target97) {
                p97 = histLog((float)k2) + ((target97 - a2) * within - 0.5) / kHistScale;
                found = true;
            }
        }
        if (lx) { p97 = lxCompressAbs(p97, pivotAbs); p88 = lxCompressAbs(p88, pivotAbs); }
        float evHL = p97 - log2(1.2 * 3.5);
        float pull = lerp(0.35, 0.65, saturate((p88 - log2(1.2 * 3.5) - targetEV) * 0.75));
        if (evHL > targetEV) targetEV = lerp(targetEV, evHL, pull);
    }
    // Sky constraint: when sky fills a good part of the frame and sits far above the metered mid-tones (a sunset
    // over a shaded foreground, a bright overcast dome over a dark street), pull part of the way towards keeping the
    // sky's median below the filmic shoulder (~1.1 pre-exposed) so it keeps its colour instead of clipping to white.
    // A clear day sky is well below that and is unaffected.
    float totalSky = total - totalS;
    if (totalSky > total * 0.15) {
        float target50 = totalSky * 0.5, cumK = 0, p50 = histLog(63.0);
        bool found50 = false;
        [unroll] for (int k3 = 0; k3 < 64; k3++) {
            float hk = max(h[k3] - hs[k3], 0.0);
            float a3 = cumK;
            cumK += hk;
            if (!found50 && cumK >= target50) {
                p50 = histLog((float)k3) + (hk > 0.0 ? (target50 - a3) / hk - 0.5 : 0.0) / kHistScale;
                found50 = true;
            }
        }
        float evSky = p50 - log2(1.2 * 1.1);
        if (evSky > targetEV) targetEV = lerp(targetEV, evSky, 0.5 * saturate((totalSky / total - 0.15) * 2.5));
    }
    targetEV = clamp(targetEV, gPost2.z, gPost2.w);
    float ev = prev.y;
    if (prev.w < 0.5 || gPost3.y > 0.5 || !(ev == ev)) ev = targetEV;
    float speed = targetEV > ev ? gPost0.y : gPost0.z;
    ev = lerp(ev, targetEV, 1.0 - exp(-speed * gPost0.w));
    float exposure = 1.0 / (1.2 * exp2(ev));
    // Keep the exposure the frame was rendered with: history buffers (TAA output, scene color pyramid) are
    // pre-exposed with it and get rescaled by prevExposureRatio() next frame.
    uExposure[1] = float4(prev.w < 0.5 ? exposure : prev.x, 0, 0, 0);
    uExposure[0] = float4(exposure, ev, avgLum, 1);
}

// ------------------------------------------------------------------------------------------------
// Local exposure grid (see kLxMin above). 1) Each 16x16 group bins one 32x32 pixel tile of the anti-aliased image
// (2x2 pixels a thread) by log luminance: per bin the mean log luminance and the share of the tile's pixels.
RWTexture3D<float2> uLxGrid : register(u3);   // (mean log luminance * weight, weight) per tile and bin
RWTexture2D<float> uLxMean : register(u4);    // mean log luminance of the tile's neighbourhood
Texture3D<float2> tLxRaw : register(t7);
groupshared uint gsLxSum[16], gsLxCnt[16];
[numthreads(16, 16, 1)]
void csLocalExpGrid(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    if (gi < 16) { gsLxSum[gi] = 0; gsLxCnt[gi] = 0; }
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (int k = 0; k < 4; k++) {
        uint2 p = gid.xy * 32 + tid.xy * 2 + uint2(k & 1, k >> 1);
        if (p.x < (uint)gScreen.x && p.y < (uint)gScreen.y) {
            float l = log2(max(luminance(tHDR[p].rgb), 1e-7));
            if (l == l) {   // NaN guard
                float lb = clamp(l, kLxMin, kLxMin + kLxBins * kLxStep - 0.01) - kLxMin;
                uint bi = min((uint)(lb / kLxStep), 15u);
                InterlockedAdd(gsLxSum[bi], (uint)(lb * 64.0 + 0.5));
                InterlockedAdd(gsLxCnt[bi], 1u);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < 16) {
        float n = (float)gsLxCnt[gi];
        float mean = n > 0.0 ? (float)gsLxSum[gi] / (64.0 * n) + kLxMin : 0.0;
        float w = n / 1024.0;
        uLxGrid[uint3(gid.xy, gi)] = float2(mean * w, w);
    }
}

// 2) Blur of the grid: a tent over 3x3 tiles and 3 bins (the bilateral base), and the mean log luminance over 5x5
// tiles (all bins: the neighbourhood's geometric mean). gBloom.xy: grid size in tiles.
[numthreads(8, 8, 1)]
void csLocalExpBlur(uint3 id : SV_DispatchThreadID) {
    int2 dim = (int2)gBloom.xy;
    if (id.x >= (uint)dim.x || id.y >= (uint)dim.y) return;
    float2 m = 0;
    [unroll] for (int y = -2; y <= 2; y++) {
        [unroll] for (int x = -2; x <= 2; x++) {
            int2 q = clamp((int2)id.xy + int2(x, y), int2(0, 0), dim - 1);
            float wt = (3.0 - abs((float)x)) * (3.0 - abs((float)y));
            [loop] for (int b = 0; b < 16; b++) m += tLxRaw[uint3(q, b)] * wt;
        }
    }
    uLxMean[id.xy] = m.y > 1e-6 ? m.x / m.y : kLxMin;
    [loop] for (int z = 0; z < 16; z++) {
        float2 s = 0;
        [unroll] for (int dz = -1; dz <= 1; dz++) {
            int zz = z + dz;
            if (zz < 0 || zz > 15) continue;
            [unroll] for (int y2 = -1; y2 <= 1; y2++) {
                [unroll] for (int x2 = -1; x2 <= 1; x2++) {
                    int2 q = clamp((int2)id.xy + int2(x2, y2), int2(0, 0), dim - 1);
                    s += tLxRaw[uint3(q, zz)] * ((2.0 - abs((float)x2)) * (2.0 - abs((float)y2)) * (2.0 - abs((float)dz)));
                }
            }
        }
        uLxGrid[uint3(id.xy, z)] = s / 64.0;
    }
}

// ------------------------------------------------------------------------------------------------
// Bloom: 13-tap downsample (with Karis average on the first level) and tent upsample.
Texture2D<float4> tBloomSrc : register(t3);
Texture2D<float4> tBloomLow : register(t4);
RWTexture2D<float4> uBloomDst : register(u2);
float3 karis(float3 c) { return c / (1.0 + luminance(c)); }

[numthreads(8, 8, 1)]
void csBloomDown(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gBloom.x || id.y >= (uint)gBloom.y) return;
    float2 texel = 1.0 / (gBloom.xy * 2.0);
    float2 uv = (id.xy + 0.5) / gBloom.xy;
    float3 a = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-2, -2), 0).rgb;
    float3 b = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(0, -2), 0).rgb;
    float3 c = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(2, -2), 0).rgb;
    float3 d = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-2, 0), 0).rgb;
    float3 e = tBloomSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
    float3 f = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(2, 0), 0).rgb;
    float3 g = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-2, 2), 0).rgb;
    float3 h = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(0, 2), 0).rgb;
    float3 i = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(2, 2), 0).rgb;
    float3 j = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-1, -1), 0).rgb;
    float3 k = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(1, -1), 0).rgb;
    float3 l = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(-1, 1), 0).rgb;
    float3 m = tBloomSrc.SampleLevel(sLinearClamp, uv + texel * float2(1, 1), 0).rgb;
    float3 r;
    if (gBloom.z > 0.5) {
        float3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25, g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25, g4 = (j + k + l + m) * 0.25;
        float w0 = 1.0 / (1.0 + luminance(g0)), w1 = 1.0 / (1.0 + luminance(g1)), w2 = 1.0 / (1.0 + luminance(g2)), w3 = 1.0 / (1.0 + luminance(g3)), w4 = 1.0 / (1.0 + luminance(g4));
        r = (g0 * w0 * 0.125 + g1 * w1 * 0.125 + g2 * w2 * 0.125 + g3 * w3 * 0.125 + g4 * w4 * 0.5) / (w0 * 0.125 + w1 * 0.125 + w2 * 0.125 + w3 * 0.125 + w4 * 0.5);
    } else {
        r = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    uBloomDst[id.xy] = float4(min(r, 30000.0), 1);
}

[numthreads(8, 8, 1)]
void csBloomUp(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gBloom.x || id.y >= (uint)gBloom.y) return;
    float2 uv = (id.xy + 0.5) / gBloom.xy;
    float2 t = gBloom.w / gBloom.xy;
    float3 s = tBloomLow.SampleLevel(sLinearClamp, uv + float2(-t.x, -t.y), 0).rgb;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(0, -t.y), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(t.x, -t.y), 0).rgb;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(-t.x, 0), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv, 0).rgb * 4.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(t.x, 0), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(-t.x, t.y), 0).rgb;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(0, t.y), 0).rgb * 2.0;
    s += tBloomLow.SampleLevel(sLinearClamp, uv + float2(t.x, t.y), 0).rgb;
    s /= 16.0;
    float3 cur = tBloomSrc.SampleLevel(sLinearClamp, uv, 0).rgb;
    // each wider level contributes a little less: glow and halo around lamps and neon without a wide haze smeared
    // across the frame
    uBloomDst[id.xy] = float4(cur + s * 0.72, 1);
}

// ------------------------------------------------------------------------------------------------
// Crepuscular rays through the clouds. Two radial passes at quarter resolution average the cloud transmittance
// along the screen-space path from each pixel to the sun (16 x 16 = 256 evenly spread taps): pass 0 (gBloom.z = 0)
// reads the cloud layer (transmittance in a, 1 where there is no sky), pass 1 the pass-0 result. Pass 1 turns the
// lit fraction into in-scattered sunlight relative to the fully lit air the sky model assumes: sunlit shafts
// brighten, the columns of air in cloud shadow darken. Mie forward phase, reddened golden-hour sunlight.
[numthreads(8, 8, 1)]
void csSunShafts(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gBloom.x || id.y >= (uint)gBloom.y) return;
    float2 uv = (id.xy + 0.5) / gBloom.xy;
    bool first = gBloom.z < 0.5;
    float2 stepUV = (gShaft.xy - uv) * (first ? 1.0 / 256.0 : 1.0 / 16.0);
    float lit = 0;
    [unroll] for (int k = 0; k < 16; k++) {
        float4 v = tBloomSrc.SampleLevel(sLinearClamp, uv + stepUV * k, 0);
        lit += first ? v.a : v.r;
    }
    lit *= 1.0 / 16.0;
    if (first) { uBloomDst[id.xy] = float4(lit, 0, 0, 1); return; }
    float3 dir = normalize(reconstructPos(uv, 1e-5));
    float c = dot(dir, gSunDir.xyz);
    const float g = 0.76;
    float ph = (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5));
    float3 L = mainLightIlluminance() * ph * gShaft.z * (lit - 0.55) * preExposure();
    uBloomDst[id.xy] = float4(clamp(L, -30000.0, 30000.0), 1);
}

// ------------------------------------------------------------------------------------------------
struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
VSOut vsFullscreen(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}

float3 acesFitted(float3 v) {
    // Stephen Hill's fit of the ACES RRT+ODT
    const float3x3 inM = float3x3(0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777);
    const float3x3 outM = float3x3(1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602);
    v = mul(inM, v);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    v = a / b;
    return saturate(mul(outM, v));
}

// Local exposure factor of a pixel (see csLocalExpGrid): the bilateral grid sliced at the pixel's position and log
// luminance gives its region's base; the base's part above the pivot is scaled by gPost3.x.
Texture3D<float2> tLxGrid : register(t8);
Texture2D<float> tLxMean : register(t9);
float localExposure(float3 c, float2 uv) {
    float l = log2(max(luminance(c), 1e-7));
    float2 gdim = ceil(gScreen.xy / 32.0);
    float2 guv = uv * gScreen.xy / (32.0 * gdim);
    float z = (clamp(l, kLxMin, kLxMin + kLxBins * kLxStep) - kLxMin) / (kLxStep * kLxBins);
    float2 g = tLxGrid.SampleLevel(sLinearClamp, float3(guv, z), 0);
    float base = g.y > 1e-5 ? g.x / g.y : l;
    // small bright things in a darker neighbourhood (lamps, neon, a lit window far away) lean towards the
    // neighbourhood's mean and keep most of their brightness; a dark frame next to a lit room keeps its own base
    // (no dark halo)
    base -= kLxMeanBlend * max(base - tLxMean.SampleLevel(sLinearClamp, guv, 0), 0.0);
    return exp2(-max(base - kTP, 0.0) * (1.0 - kTS));
}

// Disc blur of the HDR image (gameplay blur / underwater softening), radius in pixels.
float3 discBlur(float2 uv, float radius) {
    float3 s = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
    float2 px = radius / gScreen.xy;
    [unroll] for (int k = 0; k < 12; k++) {
        float a = k * 2.39996323;
        float r = sqrt((k + 0.5) / 12.0);
        s += tHDR.SampleLevel(sLinearClamp, uv + float2(cos(a), sin(a)) * r * px, 0).rgb;
    }
    return s / 13.0;
}

float4 psTonemap(VSOut i) : SV_Target {
    float2 uv = i.uv;
    float uw = gFx2.w;
    if (uw > 0.0) {
        // underwater refraction wobble
        uv += float2(sin(uv.y * 38.0 + gTime.x * 2.1), cos(uv.x * 31.0 + gTime.x * 1.7)) * 0.0022 * uw;
    }
    float3 c = tHDR.SampleLevel(sLinearClamp, uv, 0).rgb;
    // Debug views (--debugview N) are shown linearly, without grading (written apart from the lit image, which keeps
    // feeding TAA, exposure and the reflections)
    if (gRenderParams.w > 0.5 && i.uv.x >= gRenderParams.y) {
        if (gRenderParams.y > 0.0 && i.uv.x < gRenderParams.y + 1.5 / gScreen.x) return float4(1, 1, 0, 1);  // split divider
        return float4(linearToSrgb(saturate(tDebugView.SampleLevel(sPointClamp, i.uv, 0).rgb)), 1);
    }
    // light sharpening (compensates TAA softness)
    float2 px = 1.0 / gScreen.xy;
    float3 n0 = tHDR.SampleLevel(sLinearClamp, uv + float2(px.x, 0), 0).rgb, n1 = tHDR.SampleLevel(sLinearClamp, uv - float2(px.x, 0), 0).rgb;
    float3 n2 = tHDR.SampleLevel(sLinearClamp, uv + float2(0, px.y), 0).rgb, n3 = tHDR.SampleLevel(sLinearClamp, uv - float2(0, px.y), 0).rgb;
    float3 nb = n0 + n1 + n2 + n3;
    // the overshoot stays within the neighbourhood's range: no pale halo along a dark edge (a brow or lash line
    // against skin, a window frame against a lit room), detail between the extremes is still sharpened
    float3 sharp = clamp(c + (c - nb * 0.25) * gPost3.z, min(c, min(min(n0, n1), min(n2, n3))), max(c, max(max(n0, n1), max(n2, n3))));
    c = max(lerp(c, sharp, saturate(1.0 - luminance(c) * 0.2)), 0.0);
    // Chromatic aberration: radial red/blue split
    float ca = gFx0.z;
    if (ca > 0.0) {
        float2 dir = (uv - 0.5) * ca * 0.012;
        c.r = tHDR.SampleLevel(sLinearClamp, uv + dir, 0).r;
        c.b = tHDR.SampleLevel(sLinearClamp, uv - dir, 0).b;
    }
    float blurAmt = saturate(gFx1.w + uw * 0.25);
    if (blurAmt > 0.0) c = lerp(c, discBlur(uv, 2.0 + blurAmt * 10.0), saturate(blurAmt * 2.0));
    c = lerp(c, tBloom.SampleLevel(sLinearClamp, uv, 0).rgb, gPost1.x);
    if (gPost3.x < 0.999) c *= localExposure(c, i.uv);
    if (gShaft.z > 0.0) {
        // crepuscular rays over the sky and distant scenery (the froxel fog handles the air near the camera)
        float d0 = tSceneDepth.SampleLevel(sPointClamp, i.uv, 0);
        float air = d0 > 0.0 ? saturate((linearDepth(d0) - 250.0) / 2500.0) : 1.0;
        if (air > 0.0) c = max(c + tShafts.SampleLevel(sLinearClamp, i.uv, 0).rgb * air, c * 0.55);
    }
    if (uw > 0.0) {
        // Underwater: absorption with distance, blue-green scattering, soft caustic shimmer
        float d = tSceneDepth.SampleLevel(sPointClamp, uv, 0);
        float dist = d > 0.0 ? linearDepth(d) : 200.0;
        float3 trans = exp(-float3(0.45, 0.09, 0.07) * dist);
        float amb = gExposureBuf[0].z * gExposureBuf[0].x;  // average scene luminance, pre-exposed
        float3 fogC = float3(0.05, 0.32, 0.36) * max(amb, 0.02) * 2.0;
        float2 cp = uv * float2(9.0, 6.0) + gTime.x * 0.35;
        float caustic = pow(abs(sin(cp.x + sin(cp.y * 1.3)) * sin(cp.y + sin(cp.x * 1.7))), 3.0);
        float3 uwc = c * trans * (1.0 + caustic * 0.35 * saturate(1.0 - dist / 25.0)) + fogC * (1.0 - trans);
        c = lerp(c, uwc, uw);
    }
    // Night vision (mesopic): at moonlight levels the rods take over, colours fade and the scene shifts towards blue
    // (Purkinje: reds darken first, blue-greens hold). Weighted by the absolute luminance (this frame's exposure,
    // gExposureBuf[1]): fully below ~0.005 cd/m2, gone above ~2.5 cd/m2, so lamp pools, signs and lit rooms keep their
    // colour while the moonlit surroundings turn blue-grey. Night only (the eye is not dark-adapted by day).
    if (gExposure.w > 0.0) {
        float absL = luminance(c) / max(gExposureBuf[1].x, 1e-12);
        float m = gExposure.w * (1.0 - smoothstep(-2.3, 0.4, log10(max(absL, 1e-7))));
        float ys = dot(c, float3(0.08, 0.57, 0.35));   // rod response (neutral grey unchanged)
        c = lerp(c, ys * float3(0.82, 1.0, 1.36), m * 0.8);
    }
    // White balance / warmth (sub-tropical grade by day, neutral at night) and saturation
    c *= float3(1.0 + gPost2.y * 0.06, 1.0, 1.0 - gPost2.y * 0.08);
    c *= gFx1.rgb;
    float l = luminance(c);
    c = max(lerp(l, c, gPost1.w * gFx0.x), 0.0);
    // Contrast as a power curve around scene mid grey (0.18): keeps black at 0 instead of clipping the shadows
    c = 0.18 * pow(max(c / 0.18, 0.0), gPost2.x);
    c = acesFitted(c);
    // vignette (grading) + gameplay colored vignette
    float2 d = i.uv - 0.5;
    float r2 = dot(d, d);
    c *= 1.0 - gPost1.y * r2 * 1.6;
    if (gFx0.y > 0.0) c = lerp(c, gFx2.rgb, saturate(gFx0.y * smoothstep(0.05, 0.5, r2 * 2.0)));
    // flash
    c = lerp(c, gFx3.rgb, gFx0.w);
    // colour-blind correction (linear light, after tonemapping; the UI is drawn afterwards and corrects itself)
    if (gCb0.w > 0.5) c = saturate(float3(dot(gCb0.xyz, c), dot(gCb1.xyz, c), dot(gCb2.xyz, c)));
    float3 outc = linearToSrgb(saturate(c));
    // film grain + dither to hide banding
    float n = ign(i.pos.xy, gTime.z) - 0.5;
    outc += n * (gPost1.z + gFx3.w * 0.08 + 1.0 / 255.0);
    return float4(outc, 1);
}
