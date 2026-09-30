// Ground-truth-style ambient occlusion with a visibility bitmask (horizon sectors) and one-bounce screen-space
// indirect diffuse, at half resolution. Temporal accumulation + edge-aware spatial filter; the lighting pass
// performs a bilateral upsample.
// Output: rgb = indirect diffuse radiance (cosine-weighted average, pre-exposed with the current exposure),
//         a = ambient visibility (1 = unoccluded).
#include "common.hlsli"

Texture2D<float> tHalfDepth : register(t0);      // linear view depth (current)
Texture2D<float2> tHalfNormal : register(t1);    // octahedral world normal
Texture2D<float4> tSceneColor : register(t2);    // previous frame HDR pyramid (pre-exposed with the previous exposure)
Texture2D<float4> tHistory : register(t3);       // previous accumulated result
Texture2D<float> tPrevHalfDepth : register(t4);  // previous frame linear depth
Texture2D<float2> tVelocity : register(t5);      // full-res velocity (uv units, current - previous)
Texture2D<float4> tInput : register(t6);         // generic input for the filter passes
RWTexture2D<float4> uOut : register(u0);

cbuffer GTAOCB : register(b1) {
    float4 gAO0;   // x slices, y steps per side, z AO radius (m), w GI radius (m)
    float4 gAO1;   // x thickness (m), y AO strength, z GI strength, w max screen radius (half-res pixels)
    float4 gAO2;   // x history valid, y temporal blend, z GI enabled, w color pyramid valid
};

static const float kSectors = 32.0;

float3 viewPosFromDepth(float2 uv, float z) {
    float2 ndc = uv * float2(2, -2) + float2(-1, 1);
    return float3((ndc.x + gProj[0][2]) * z / gProj[0][0], (ndc.y + gProj[1][2]) * z / gProj[1][1], -z);
}
float3 viewToWorldDir(float3 v) { return mul(transpose((float3x3)gView), v); }
float3 worldToViewDir(float3 w) { return mul((float3x3)gView, w); }

uint sectorMask(float a0, float a1) {
    // a0 <= a1 in [0,1]; sets bits [floor(a0*32), ceil(a1*32))
    uint start = (uint)clamp(floor(a0 * kSectors), 0.0, kSectors);
    uint end = (uint)clamp(ceil(a1 * kSectors), 0.0, kSectors);
    uint count = end > start ? end - start : 0u;
    uint m = count >= 32u ? 0xffffffffu : ((1u << count) - 1u);
    return start >= 32u ? 0u : (m << start);
}

[numthreads(8, 8, 1)]
void csGTAO(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gHalfScreen.xy)) return;
    float2 uv = (id.xy + 0.5) * gHalfScreen.zw;
    float z = tHalfDepth[id.xy];
    if (z > 5e5) { uOut[id.xy] = float4(0, 0, 0, 1); return; }
    float3 P = viewPosFromDepth(uv, z);
    float3 V = normalize(-P);
    // Geometric normal from the depth buffer (smaller-difference side per axis): avoids false occlusion from
    // normal-mapped detail; falls back to the G-buffer normal where both sides are discontinuous.
    int2 ip = int2(id.xy), mx = int2(gHalfScreen.xy) - 1;
    float3 Pl = viewPosFromDepth(uv - float2(gHalfScreen.z, 0), tHalfDepth[clamp(ip - int2(1, 0), 0, mx)]);
    float3 Pr = viewPosFromDepth(uv + float2(gHalfScreen.z, 0), tHalfDepth[clamp(ip + int2(1, 0), 0, mx)]);
    float3 Pd = viewPosFromDepth(uv - float2(0, gHalfScreen.w), tHalfDepth[clamp(ip - int2(0, 1), 0, mx)]);
    float3 Pu = viewPosFromDepth(uv + float2(0, gHalfScreen.w), tHalfDepth[clamp(ip + int2(0, 1), 0, mx)]);
    float3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
    float3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;
    float3 N = worldToViewDir(octDecode(tHalfNormal[id.xy] * 2.0 - 1.0));
    float3 Nd = normalize(cross(dy, dx));
    if (dot(Nd, V) < 0) Nd = -Nd;
    float disc = min(abs(Pr.z - P.z), abs(P.z - Pl.z)) + min(abs(Pu.z - P.z), abs(P.z - Pd.z));
    if (disc < 0.05 * z + 0.05 && all(isfinite(Nd))) N = normalize(lerp(N, Nd, 0.85));
    // Projected radius in half-res pixels
    float pxPerMeter = gProj[1][1] * 0.5 * gHalfScreen.y / z;
    float giR = gAO0.w, aoR = gAO0.z;
    float radiusPx = min(giR * pxPerMeter, gAO1.w);
    if (radiusPx < 2.0) { uOut[id.xy] = float4(0, 0, 0, 1); return; }
    int slices = (int)gAO0.x, steps = (int)gAO0.y;
    float noiseA = ign(float2(id.xy), gTime.z);
    float noiseB = frac(ign(float2(id.xy) + 17.0, gTime.z * 1.618) + 0.5);
    float thickness = gAO1.x * (1.0 + z * 0.02);
    bool doGI = gAO2.z > 0.5 && gAO2.w > 0.5;
    float expRatio = prevExposureRatio();
    float aoSum = 0, wSum = 0;
    float3 giSum = 0;
    float giCover = 0, giCompW = 0;
    [loop] for (int s = 0; s < slices; s++) {
        float phi = (s + noiseA) / slices * PI;
        float2 dir = float2(cos(phi), sin(phi));
        float3 dirV = float3(dir.x, -dir.y, 0);
        float3 ortho = normalize(dirV - dot(dirV, V) * V);
        float3 axis = normalize(cross(dirV, V));
        float3 projN = N - axis * dot(N, axis);
        float projLen = length(projN);
        if (projLen < 1e-4) continue;
        float cosN = clamp(dot(projN / projLen, V), -1.0, 1.0);
        float n = (dot(projN, ortho) >= 0 ? 1.0 : -1.0) * acos(cosN);
        uint occAll = 0, occAO = 0;
        float3 giSlice = 0;
        float coverSlice = 0, compSlice = 0;
        [loop] for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0 : -1.0;
            [loop] for (int k = 0; k < steps; k++) {
                float t = (k + frac(noiseB + k * 0.618034)) / steps;
                float offPx = max(t * t * radiusPx, 1.0 + k);
                float2 sUV = uv + dir * sgn * offPx * gHalfScreen.zw;
                if (any(sUV <= 0.0) || any(sUV >= 1.0)) break;
                float sz = tHalfDepth.SampleLevel(sPointClamp, sUV, 0);
                float3 S = viewPosFromDepth(sUV, sz);
                float3 d = S - P;
                float dist = length(d);
                // Samples beyond the gather radius (background behind silhouettes) neither occlude nor emit:
                // their true solid angle is far below one sector.
                if (dist < 1e-3 || sz > 5e5 || dist > giR) continue;
                float3 dF = d / dist;
                float3 dB = normalize(d - V * thickness);
                float aF = sgn * acos(clamp(dot(dF, V), -1.0, 1.0)) - n;
                float aB = sgn * acos(clamp(dot(dB, V), -1.0, 1.0)) - n;
                float lo = clamp(min(aF, aB), -PI * 0.5, PI * 0.5), hi = clamp(max(aF, aB), -PI * 0.5, PI * 0.5);
                // cosine-weighted sectors: uniform in sin(angle from the normal)
                uint m = sectorMask(0.5 + 0.5 * sin(lo), 0.5 + 0.5 * sin(hi));
                if (m == 0u) continue;
                if (doGI) {
                    uint fresh = m & ~occAll;
                    if (fresh != 0u) {
                        float3 sN = octDecode(tHalfNormal.SampleLevel(sPointClamp, sUV, 0) * 2.0 - 1.0);
                        float facing = saturate(dot(worldToViewDir(sN), -dF) * 4.0);
                        if (facing > 0.0) {
                            float3 sw = viewToWorldDir(S);
                            float4 pc = mul(gPrevViewProj, float4(sw, 1));
                            float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
                            if (all(puv > 0.0) && all(puv < 1.0) && pc.w > 0.0) {
                                float3 L = tSceneColor.SampleLevel(sLinearClamp, puv, dist > 2.0 ? 1.0 : 0.0).rgb * expRatio;
                                // Firefly-resistant accumulation: a softly luminance-compressed average (glints and
                                // lamp cores are tamed, sunlit pavement still bounces at nearly full strength into the
                                // shade), rescaled by coverage
                                float cw = (countbits(fresh) / kSectors) * facing;
                                float comp = 1.0 / (1.0 + luminance(L) * (1.0 / 12.0));
                                giSlice += L * cw * comp;
                                compSlice += cw * comp;
                                coverSlice += cw;
                            }
                        }
                    }
                }
                occAll |= m;
                if (dist < aoR) occAO |= m;
            }
        }
        aoSum += projLen * (1.0 - countbits(occAO) / kSectors);
        giSum += projLen * giSlice;
        giCompW += projLen * compSlice;
        giCover += projLen * coverSlice;
        wSum += projLen;
    }
    float ao = wSum > 0 ? aoSum / wSum : 1.0;
    float3 gi = (wSum > 0 && giCompW > 0) ? giSum / giCompW * (giCover / wSum) : 0.0;
    ao = pow(saturate(ao), gAO1.y);
    gi = min(gi * gAO1.z, 60000.0);
    uOut[id.xy] = float4(gi, ao);
}

// ------------------------------------------------------------------------------------------------
// Temporal accumulation with depth-validated reprojection and neighborhood clamping.
[numthreads(8, 8, 1)]
void csGTAOTemporal(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gHalfScreen.xy)) return;
    float2 uv = (id.xy + 0.5) * gHalfScreen.zw;
    float4 cur = tInput[id.xy];
    float z = tHalfDepth[id.xy];
    if (z > 5e5) { uOut[id.xy] = float4(0, 0, 0, 1); return; }
    // Neighborhood statistics of the noisy input (variance clipping box)
    float4 m1 = cur, m2 = cur * cur;
    float n = 1;
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++) {
        if (x == 0 && y == 0) continue;
        int2 q = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(gHalfScreen.xy) - 1);
        if (tHalfDepth[q] > 5e5) continue;
        float4 v = tInput[q];
        m1 += v;
        m2 += v * v;
        n += 1;
    }
    m1 /= n;
    float4 sigma = sqrt(max(m2 / n - m1 * m1, 0.0));
    float4 mn = m1 - sigma * 2.0, mx = m1 + sigma * 2.0;
    float4 result = cur;
    if (gAO2.x > 0.5) {
        float3 P = viewPosFromDepth(uv, z);
        float3 wp = viewToWorldDir(P);
        float4 pc = mul(gPrevViewProj, float4(wp, 1));
        float2 puv = pc.xy / pc.w * float2(0.5, -0.5) + 0.5;
        float2 vel = tVelocity.SampleLevel(sPointClamp, uv, 0);
        float2 staticVel = uv - puv;
        if (any(vel != 0) && length(vel - staticVel) > 0.002) puv = uv - vel;  // moving object
        if (all(puv > 0.0) && all(puv < 1.0) && pc.w > 0.0) {
            float pz = tPrevHalfDepth.SampleLevel(sPointClamp, puv, 0);
            float expectZ = pc.w;
            if (abs(pz - expectZ) < 0.06 * expectZ + 0.05) {
                float4 h = tHistory.SampleLevel(sLinearClamp, puv, 0);
                h.rgb *= prevExposureRatio();
                h = clamp(h, mn, mx);
                // GI is noisier than AO: longer accumulation
                result = float4(lerp(h.rgb, cur.rgb, gAO2.y * 0.6), lerp(h.a, cur.a, gAO2.y));
            }
        }
    }
    uOut[id.xy] = float4(max(result.rgb, 0.0), saturate(result.a));
}

// Edge-aware 5x5 (stride 1) blur using depth and normal similarity.
[numthreads(8, 8, 1)]
void csGTAOBlur(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= (uint2)gHalfScreen.xy)) return;
    float z = tHalfDepth[id.xy];
    if (z > 5e5) { uOut[id.xy] = float4(0, 0, 0, 1); return; }
    float3 n = octDecode(tHalfNormal[id.xy] * 2.0 - 1.0);
    float4 sum = 0;
    float wsum = 0;
    [unroll] for (int y = -2; y <= 2; y++)
    [unroll] for (int x = -2; x <= 2; x++) {
        int2 q = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(gHalfScreen.xy) - 1);
        float qz = tHalfDepth[q];
        float3 qn = octDecode(tHalfNormal[q] * 2.0 - 1.0);
        float w = exp(-abs(qz - z) / (0.03 * z + 0.02)) * pow(saturate(dot(qn, n)), 8.0);
        w *= (x == 0 && y == 0) ? 1.0 : 0.6 / (1.0 + 0.25 * (x * x + y * y));
        sum += tInput[q] * w;
        wsum += w;
    }
    uOut[id.xy] = sum / max(wsum, 1e-5);
}
