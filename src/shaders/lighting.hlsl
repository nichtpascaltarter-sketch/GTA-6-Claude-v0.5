// Deferred lighting: sun/moon with cascaded shadows, sky ambient, aerial perspective, sky background.
#include "gbuffer.hlsli"
#include "skycommon.hlsli"
#include "shadow.hlsli"

Texture2D<float4> tAlbedo : register(t0);
Texture2D<float2> tNormal : register(t1);
Texture2D<float4> tMaterial : register(t2);
Texture2D<float3> tEmissive : register(t3);
Texture2D<float> tDepth : register(t4);
Texture2D<float> tAO : register(t5);
Texture2D<float4> tClouds : register(t6);
RWTexture2D<float4> uHDR : register(u0);

// Local lights (streetlights, windows, neon, headlights...). Positions relative to the camera.
struct LightGPU {
    float3 pos;
    float radius;
    float3 color;      // luminous intensity (cd) * rgb
    float spotCos;     // cos of outer cone angle; <= -1 for point lights
    float3 dir;        // spot direction
    float spotInner;   // cos of inner cone angle
};
StructuredBuffer<LightGPU> tLights : register(t7);
cbuffer LightCB : register(b2) {
    uint gLightCount;
    uint3 gLightPad;
};

float3 localLightBRDF(GBufferData g, float3 N, float3 V, float3 L) {
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

groupshared uint gsMinZ, gsMaxZ, gsLightCount;
groupshared uint gsLights[256];

float3 shadeSurface(GBufferData g, float3 relPos, float3 V, float3 sunE, float shadow, float ao) {
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
        // Wrapped diffuse with a reddish subsurface falloff
        float w = 0.45;
        float nlw = saturate((dot(N, L) + w) / (1.0 + w));
        float3 sss = float3(1.0, 0.45, 0.3) * (nlw - NoL) * g.extra;
        direct = (diffColor / PI * (NoL + max(sss, 0.0)) + spec * NoL) * sunE * shadow;
    } else if (g.shadingModel == SM_CLOTH) {
        float sheen = pow(1.0 - NoV, 4.0) * 0.35 * g.extra;
        direct += diffColor * sheen * NoL * sunE * shadow;
    } else if (g.shadingModel == SM_HAIR) {
        // Broad secondary highlight shifted towards the light (approximates anisotropic hair)
        float3 H2 = normalize(L + V + N * 0.3);
        float spec2 = pow(saturate(dot(N, H2)), 20.0) * 0.08;
        direct += g.albedo * spec2 * sunE * shadow * NoL;
    } else if (g.shadingModel == SM_CARPAINT) {
        // Clear coat layer over the base
        float ca = 0.035 * 0.035;
        float Fc = 0.04 + 0.96 * pow5(1.0 - VoH);
        float coat = D_GGX(NoH, ca) * V_SmithGGXCorrelated(NoV, NoL, ca) * Fc * g.extra;
        direct = direct * (1.0 - Fc * g.extra) + coat * NoL * sunE * shadow;
        float Fcv = (0.04 + 0.96 * pow5(1.0 - NoV)) * g.extra;
        float3 Rc = reflect(-V, N);
        coatSpecAmb = skyRadiance(normalize(float3(Rc.xy, max(Rc.z, 0.02))), false) * Fcv * saturate(1.0 + 1.5 * dot(Rc, N));
    }
    if (g.shadingModel == SM_FOLIAGE) {
        // Thin translucency: light passing through leaves
        float back = saturate(dot(-N, L)) * 0.6 + pow(saturate(dot(V, -L)), 6.0) * 0.8;
        direct += diffColor * g.extra * back * sunE * shadow / PI;
    }
    // Ambient: sky SH with multi-bounce AO approximation (Jimenez 2016)
    // Multi-bounce AO approximation (Jimenez 2016)
    float3 mbA = 2.0404 * diffColor - 0.3324;
    float3 mbB = -4.7951 * diffColor + 0.6417;
    float3 mbC = 2.7552 * diffColor + 0.6903;
    float3 aoMB = max(ao, ((ao * mbA + mbB) * ao + mbC) * ao);
    float3 ambientDiffuse = diffColor * evalSH9(N) * aoMB;
    float3 R = reflect(-V, N);
    float2 ab = envBRDFApprox(g.rough, NoV);
    float specOcc = saturate(pow(NoV + ao, exp2(-16.0 * g.rough - 1.0)) - 1.0 + ao);
    // Rough sky reflection: blend of SH (rough) and sky radiance in reflected direction (smooth)
    float3 skyRefl = lerp(skyRadiance(normalize(float3(R.xy, max(R.z, 0.02))), false), evalSH9(R) * PI, saturate(g.rough * 1.3));
    float horizonOcc = saturate(1.0 + 1.5 * dot(R, N));  // avoid reflecting below the surface
    float3 ambientSpec = skyRefl * (f0 * ab.x + ab.y) * specOcc * horizonOcc * horizonOcc;
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
        uHDR[id.xy] = float4(min(sky * preExposure(), 60000.0), 1);
        return;
    }
    GBufferData g = unpackGBuffer(tAlbedo[id.xy], tNormal[id.xy], tMaterial[id.xy]);
    float3 emissive = tEmissive[id.xy];
    float dist = length(relPos);
    float3 color;
    float3 sunE = mainLightIlluminance();
    float viewDepth = dot(relPos, gCamForward.xyz);
    float shadow = 1;
    float ao = 1;
    if (g.shadingModel == SM_UNLIT) {
        color = g.albedo;
    } else {
        shadow = sampleSunShadow(relPos, g.normal, viewDepth, id.xy);
        ao = min(g.ao, tAO.SampleLevel(sLinearClamp, uv, 0));
        color = shadeSurface(g, relPos, V, sunE, shadow, ao);
        // Local lights
        uint n = min(gsLightCount, 256u);
        float3 local = 0;
        for (uint i = 0; i < n; i++) {
            LightGPU Lt = tLights[gsLights[i]];
            float3 Lv = Lt.pos - relPos;
            float d2 = dot(Lv, Lv);
            if (d2 > Lt.radius * Lt.radius) continue;
            float d = sqrt(d2);
            Lv /= d;
            float x = d / Lt.radius;
            float win = saturate(1.0 - x * x * x * x);
            float att = win * win / max(d2, 0.3);
            if (Lt.spotCos > -1.0) att *= smoothstep(Lt.spotCos, Lt.spotInner, dot(-Lv, Lt.dir));
            if (att <= 0) continue;
            local += localLightBRDF(g, g.normal, V, Lv) * Lt.color * att;
        }
        color += local * lerp(0.6, 1.0, ao);
    }
    int dbg = (int)gRenderParams.w;
    if (dbg > 0) {
        float3 o = 0;
        if (dbg == 1) o = g.albedo;
        else if (dbg == 2) o = g.normal * 0.5 + 0.5;
        else if (dbg == 3) o = evalSH9(g.normal) * preExposure() * 4.0;
        else if (dbg == 4) o = shadow;
        else if (dbg == 5) o = float3(g.rough, g.metal, g.ao);
        else if (dbg == 6) o = color * preExposure();
        else if (dbg == 7) o = aerialPerspective(uv, dist).a;
        else if (dbg == 8) o = aerialPerspective(uv, dist).rgb * preExposure() * 10.0;
        else if (dbg == 9) o = sunE * preExposure();
        else if (dbg == 10) o = min(gsLightCount, 64u) / 64.0;
        if (any(isnan(o))) o = float3(1, 0, 1);
        uHDR[id.xy] = float4(o, 1);
        return;
    }
    float4 ap = aerialPerspective(uv, dist);
    color = color * ap.a + ap.rgb;
    uHDR[id.xy] = float4(min(color * preExposure() + emissive * ap.a, 60000.0), 1);
}
