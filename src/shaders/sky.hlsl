// Atmosphere LUT generation and sky rendering.
#include "skycommon.hlsli"

// For LUT generation the transmittance LUT is bound at t0 (it is being built, so not global yet)
Texture2D<float4> tTransmittance : register(t0);
Texture2D<float4> tMultiScatter : register(t1);
RWStructuredBuffer<float4> uSH : register(u2);

RWTexture2D<float4> uOut2D : register(u0);
RWTexture3D<float4> uOut3D : register(u1);

float3 lightTOA() { return gSunColor.rgb; }
float haze() { return gPlanetParams.w; }

float3 sampleTransmittance(float r, float mu) {
    float2 uv = transmittanceUV(r, mu);
    return tTransmittance.SampleLevel(sLinearClamp, uv, 0).rgb;
}
float3 sampleMultiScatter(float r, float muS) {
    float2 uv = saturate(float2(muS * 0.5 + 0.5, (r - kBottomRadius) / (kTopRadius - kBottomRadius)));
    uv = (uv * (MULTISCATTER_RES - 1) + 0.5) / MULTISCATTER_RES;
    return tMultiScatter.SampleLevel(sLinearClamp, uv, 0).rgb;
}

struct ScatterResult {
    float3 L;
    float3 transmittance;
    float3 multiScatAs1;
};

ScatterResult integrateScattering(float3 pos, float3 dir, float3 sunDir, float sampleCount, bool variableSamples,
                                  float tMaxMax, bool ground, bool rayMiePhase, bool useMultiScatter, float3 illum) {
    ScatterResult res;
    res.L = 0; res.transmittance = 1; res.multiScatAs1 = 0;
    float tBottom = raySphere(pos, dir, kBottomRadius);
    float tTop = raySphere(pos, dir, kTopRadius);
    float tMax = 0;
    if (tBottom < 0.0) {
        if (tTop < 0.0) return res;
        tMax = tTop;
    } else {
        tMax = tTop > 0.0 ? min(tTop, tBottom) : tBottom;
    }
    tMax = min(tMax, tMaxMax);
    float count = sampleCount;
    if (variableSamples) count = lerp(4.0, sampleCount, saturate(tMax * 0.01));
    float cosTheta = dot(sunDir, dir);
    float phR = rayleighPhase(cosTheta);
    float phM = cornetteShanksPhase(kMieG, cosTheta);
    const float uniformPhase = 1.0 / (4.0 * PI);
    float3 throughput = 1.0;
    float t = 0.0;
    [loop] for (float s = 0.0; s < count; s += 1.0) {
        float t0 = (s / count), t1 = ((s + 1.0) / count);
        t0 = t0 * t0 * tMax; t1 = t1 * t1 * tMax;  // quadratic distribution
        float dt = t1 - t0;
        float tc = t0 + dt * 0.3;
        float3 P = pos + tc * dir;
        MediumSample m = sampleMedium(P, haze());
        float3 sampleT = exp(-m.extinction * dt);
        float h = length(P);
        float3 up = P / h;
        float muS = dot(sunDir, up);
        float3 tSun = sampleTransmittance(h, muS);
        float3 phaseScat = rayMiePhase ? (m.mieScat * phM + m.rayleighScat * phR) : (m.scattering * uniformPhase);
        float earthShadow = raySphere(P, sunDir, kBottomRadius * 0.9995) >= 0.0 ? 0.0 : 1.0;
        float3 ms = useMultiScatter ? sampleMultiScatter(h, muS) : 0.0;
        float3 S = illum * (earthShadow * tSun * phaseScat + ms * m.scattering);
        float3 ext = max(m.extinction, 1e-9);
        float3 MSint = (m.scattering - m.scattering * sampleT) / ext;
        res.multiScatAs1 += throughput * MSint;
        float3 Sint = (S - S * sampleT) / ext;
        res.L += throughput * Sint;
        throughput *= sampleT;
    }
    if (ground && tBottom >= 0.0 && tMax == tBottom) {
        float3 P = pos + tBottom * dir;
        float h = length(P);
        float3 up = P / h;
        float nl = saturate(dot(up, sunDir));
        float3 tSun = sampleTransmittance(h, dot(sunDir, up));
        res.L += illum * tSun * throughput * nl * kGroundAlbedo / PI;
    }
    res.transmittance = throughput;
    return res;
}

// ------------------------------------------------------------------------------------------------
[numthreads(8, 8, 1)]
void csTransmittance(uint3 id : SV_DispatchThreadID) {
    if (id.x >= TRANSMITTANCE_W || id.y >= TRANSMITTANCE_H) return;
    float2 uv = (id.xy + 0.5) / float2(TRANSMITTANCE_W, TRANSMITTANCE_H);
    float r, mu;
    transmittanceParams(uv, r, mu);
    float3 pos = float3(0, 0, r);
    float3 dir = float3(0, sqrt(max(0.0, 1.0 - mu * mu)), mu);
    float tMax = raySphere(pos, dir, kTopRadius);
    float3 od = 0;
    const int N = 40;
    float dt = tMax / N;
    [loop] for (int i = 0; i < N; i++) {
        float3 P = pos + dir * ((i + 0.5) * dt);
        od += sampleMedium(P, haze()).extinction * dt;
    }
    uOut2D[id.xy] = float4(exp(-od), 1);
}

[numthreads(8, 8, 1)]
void csMultiScatter(uint3 id : SV_DispatchThreadID) {
    if (id.x >= MULTISCATTER_RES || id.y >= MULTISCATTER_RES) return;
    float2 uv = (id.xy + 0.5) / MULTISCATTER_RES;
    float cosSunZ = uv.x * 2.0 - 1.0;
    float3 sunDir = float3(0, sqrt(saturate(1.0 - cosSunZ * cosSunZ)), cosSunZ);
    float viewH = kBottomRadius + saturate(uv.y + 1e-3) * (kTopRadius - kBottomRadius - 1e-2);
    float3 pos = float3(0, 0, viewH);
    const float sqrtSamples = 8.0;
    float3 lumSum = 0, fmsSum = 0;
    [loop] for (int k = 0; k < 64; k++) {
        float i = 0.5 + floor(k / sqrtSamples);
        float j = 0.5 + (k - floor(k / sqrtSamples) * sqrtSamples);
        float theta = 2.0 * PI * i / sqrtSamples;
        float phi = acos(1.0 - 2.0 * j / sqrtSamples);
        float3 dir = float3(cos(theta) * sin(phi), sin(theta) * sin(phi), cos(phi));
        ScatterResult r = integrateScattering(pos, dir, sunDir, 20, false, 9000.0, true, false, false, 1.0);
        lumSum += r.L;
        fmsSum += r.multiScatAs1;
    }
    const float sphereSolidAngle = 4.0 * PI;
    const float isoPhase = 1.0 / sphereSolidAngle;
    float3 inScat = lumSum * sphereSolidAngle / 64.0 * isoPhase;
    float3 fms = fmsSum * sphereSolidAngle / 64.0 * isoPhase;
    float3 L = inScat / (1.0 - fms);
    uOut2D[id.xy] = float4(L, 1);
}

[numthreads(8, 8, 1)]
void csSkyView(uint3 id : SV_DispatchThreadID) {
    if (id.x >= SKYVIEW_W || id.y >= SKYVIEW_H) return;
    float2 uv = (id.xy + 0.5) / float2(SKYVIEW_W, SKYVIEW_H);
    float viewH = kBottomRadius + max(gPlanetParams.z, 0.002);
    float vHorizon = sqrt(max(viewH * viewH - kBottomRadius * kBottomRadius, 0.0));
    float cosBeta = vHorizon / viewH;
    float beta = acos(clamp(cosBeta, -1.0, 1.0));
    float zenithHorizonAngle = PI - beta;
    float cosViewZ;
    if (uv.y < 0.5) {
        float c = 2.0 * uv.y;
        c = 1.0 - c; c *= c; c = 1.0 - c;
        cosViewZ = cos(zenithHorizonAngle * c);
    } else {
        float c = uv.y * 2.0 - 1.0;
        c *= c;
        cosViewZ = cos(zenithHorizonAngle + beta * c);
    }
    float cu = uv.x * uv.x;
    float cosLightView = -(cu * 2.0 - 1.0);
    float3 pos = float3(0, 0, viewH);
    float cosSunZ = gSunDir.z;
    float3 sunDir = normalize(float3(0, sqrt(saturate(1.0 - cosSunZ * cosSunZ)), cosSunZ));
    float sinViewZ = sqrt(saturate(1.0 - cosViewZ * cosViewZ));
    float3 dir = float3(sinViewZ * sqrt(saturate(1.0 - cosLightView * cosLightView)), sinViewZ * cosLightView, cosViewZ);
    ScatterResult r = integrateScattering(pos, dir, sunDir, 30, true, 9000.0, false, true, true, lightTOA());
    uOut2D[id.xy] = float4(r.L, 1);
}

// Aerial perspective froxels: xy screen, z quadratic distance slices up to AP_MAX_KM.
[numthreads(4, 4, 4)]
void csAerialPerspective(uint3 id : SV_DispatchThreadID) {
    if (any(id >= AP_RES)) return;
    float2 uv = (id.xy + 0.5) / AP_RES;
    float3 rel = reconstructPos(uv, 0.5);
    float3 dir = normalize(rel);
    float slice = (id.z + 1.0) / AP_RES;
    float distKm = slice * slice * AP_MAX_KM;
    float3 pos = float3(0, 0, kBottomRadius + max(gPlanetParams.z, 0.002));
    float3 sunDir = gSunDir.xyz;
    // Integrate from camera to distKm with sample count proportional to slice
    float count = max(2.0, ceil(slice * 24.0));
    float cosTheta = dot(sunDir, dir);
    float phR = rayleighPhase(cosTheta);
    float phM = cornetteShanksPhase(kMieG, cosTheta);
    float3 L = 0, T = 1;
    float dt = distKm / count;
    [loop] for (float s = 0; s < count; s += 1.0) {
        float3 P = pos + dir * ((s + 0.5) * dt);
        if (length(P) < kBottomRadius) P = normalize(P) * (kBottomRadius + 0.001);
        MediumSample m = sampleMedium(P, haze());
        float3 sT = exp(-m.extinction * dt);
        float h = length(P);
        float3 up = P / h;
        float muS = dot(sunDir, up);
        float3 tSun = sampleTransmittance(h, muS);
        float3 ms = sampleMultiScatter(h, muS);
        float3 S = lightTOA() * (tSun * (m.mieScat * phM + m.rayleighScat * phR) + ms * m.scattering);
        float3 ext = max(m.extinction, 1e-9);
        L += T * (S - S * sT) / ext;
        T *= sT;
    }
    uOut3D[id] = float4(L, dot(T, 1.0 / 3.0));
}

// Project the sky (sky-view LUT) onto SH9. Single group of 64 threads.
groupshared float3 gsSH[64][9];
[numthreads(64, 1, 1)]
void csSkySH(uint gi : SV_GroupIndex) {
    float3 acc[9];
    [unroll] for (int k = 0; k < 9; k++) acc[k] = 0;
    // each thread integrates 16 directions (1024 total, stratified sphere)
    [loop] for (int s = 0; s < 16; s++) {
        uint idx = gi * 16 + s;
        float u = ((idx % 32) + 0.5) / 32.0, v = ((idx / 32) + 0.5) / 32.0;
        float phi = u * TWO_PI;
        float cosT = 1.0 - 2.0 * v;
        float sinT = sqrt(saturate(1.0 - cosT * cosT));
        float3 d = float3(sinT * cos(phi), sinT * sin(phi), cosT);
        float3 L = skyRadiance(d, false);
        if (d.z < 0.0) {
            // below the horizon: ground bounce approximation (sunlit ground albedo)
            float3 up = skyRadiance(float3(d.x, d.y, -d.z), false);
            L = up * 0.35 * saturate(gSunDir.z * 2.0 + 0.3) + 0.02 * L;
        }
        float w = 4.0 * PI / 1024.0;
        acc[0] += L * 0.282095 * w;
        acc[1] += L * 0.488603 * d.y * w;
        acc[2] += L * 0.488603 * d.z * w;
        acc[3] += L * 0.488603 * d.x * w;
        acc[4] += L * 1.092548 * d.x * d.y * w;
        acc[5] += L * 1.092548 * d.y * d.z * w;
        acc[6] += L * 0.315392 * (3.0 * d.z * d.z - 1.0) * w;
        acc[7] += L * 1.092548 * d.x * d.z * w;
        acc[8] += L * 0.546274 * (d.x * d.x - d.y * d.y) * w;
    }
    [unroll] for (int k2 = 0; k2 < 9; k2++) gsSH[gi][k2] = acc[k2];
    GroupMemoryBarrierWithGroupSync();
    if (gi < 9) {
        float3 s = 0;
        for (int t = 0; t < 64; t++) s += gsSH[t][gi];
        uSH[gi] = float4(s, 0);
    }
}
