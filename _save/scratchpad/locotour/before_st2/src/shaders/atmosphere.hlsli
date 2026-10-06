// Physically based atmosphere (after Hillaire 2020, "A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique"). Distances in kilometers.
#ifndef ATMOSPHERE_HLSLI
#define ATMOSPHERE_HLSLI
#include "common.hlsli"

static const float kBottomRadius = 6360.0;
static const float kTopRadius = 6460.0;
static const float3 kRayleighScattering = float3(5.802, 13.558, 33.1) * 1e-3;
static const float kRayleighScaleHeight = 8.0;
static const float kMieScattering = 3.996e-3;
static const float kMieExtinction = 4.440e-3;
static const float kMieScaleHeight = 1.2;
static const float kMieG = 0.80;
static const float3 kOzoneAbsorption = float3(0.650, 1.881, 0.085) * 1e-3;
static const float3 kGroundAlbedo = float3(0.3, 0.3, 0.3);

#define TRANSMITTANCE_W 256
#define TRANSMITTANCE_H 64
#define MULTISCATTER_RES 32
#define SKYVIEW_W 192
#define SKYVIEW_H 108
#define AP_RES 32
#define AP_MAX_KM 32.0

struct MediumSample {
    float3 scattering;
    float3 extinction;
    float3 rayleighScat;
    float mieScat;
};

MediumSample sampleMedium(float3 p, float hazeMul) {
    float h = max(length(p) - kBottomRadius, 0.0);
    float dR = exp(-h / kRayleighScaleHeight);
    float dM = exp(-h / kMieScaleHeight) * hazeMul;
    float dO = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    MediumSample m;
    m.rayleighScat = kRayleighScattering * dR;
    m.mieScat = kMieScattering * dM;
    m.scattering = m.rayleighScat + m.mieScat;
    m.extinction = m.rayleighScat + kMieExtinction * dM + kOzoneAbsorption * dO;
    return m;
}

float rayleighPhase(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
float cornetteShanksPhase(float g, float c) {
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + c * c) / pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5);
}

// Ray-sphere intersection (sphere at origin). Returns nearest positive t or -1.
float raySphere(float3 ro, float3 rd, float r) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    h = sqrt(h);
    float t0 = -b - h, t1 = -b + h;
    if (t1 < 0.0) return -1.0;
    return t0 >= 0.0 ? t0 : t1;
}
bool raySphere2(float3 ro, float3 rd, float r, out float t0, out float t1) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r * r;
    float h = b * b - c;
    t0 = t1 = -1.0;
    if (h < 0.0) return false;
    h = sqrt(h);
    t0 = -b - h; t1 = -b + h;
    return t1 >= 0.0;
}

// Transmittance LUT parameterization (Bruneton)
float2 transmittanceUV(float r, float mu) {
    float H = sqrt(kTopRadius * kTopRadius - kBottomRadius * kBottomRadius);
    float rho = sqrt(max(0.0, r * r - kBottomRadius * kBottomRadius));
    float discriminant = r * r * (mu * mu - 1.0) + kTopRadius * kTopRadius;
    float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
    float dMin = kTopRadius - r;
    float dMax = rho + H;
    float xMu = (d - dMin) / (dMax - dMin);
    float xR = rho / H;
    return float2(xMu, xR);
}
void transmittanceParams(float2 uv, out float r, out float mu) {
    float H = sqrt(kTopRadius * kTopRadius - kBottomRadius * kBottomRadius);
    float rho = H * uv.y;
    r = sqrt(rho * rho + kBottomRadius * kBottomRadius);
    float dMin = kTopRadius - r;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

// Sky-view LUT mapping: u = azimuth relative to sun, v = non-linear view zenith
float2 skyViewUV(float viewHeight, float cosViewZenith, float cosLightView, bool intersectGround) {
    float vhorizon = sqrt(max(viewHeight * viewHeight - kBottomRadius * kBottomRadius, 0.0));
    float cosBeta = vhorizon / viewHeight;
    float beta = acos(clamp(cosBeta, -1.0, 1.0));
    float zenithHorizonAngle = PI - beta;
    float v;
    if (!intersectGround) {
        float coord = acos(clamp(cosViewZenith, -1.0, 1.0)) / zenithHorizonAngle;
        coord = 1.0 - coord;
        coord = sqrt(saturate(coord));
        coord = 1.0 - coord;
        v = coord * 0.5;
    } else {
        float coord = (acos(clamp(cosViewZenith, -1.0, 1.0)) - zenithHorizonAngle) / beta;
        coord = sqrt(saturate(coord));
        v = coord * 0.5 + 0.5;
    }
    float u = sqrt(saturate(-cosLightView * 0.5 + 0.5));
    return float2(u, v);
}

#endif
