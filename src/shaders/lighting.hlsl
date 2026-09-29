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
    return direct + ambientDiffuse + ambientSpec;
}

[numthreads(8, 8, 1)]
void csLighting(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)gScreen.x || id.y >= (uint)gScreen.y) return;
    float2 uv = (id.xy + 0.5) * gScreen.zw;
    float depth = tDepth[id.xy];
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
    if (g.shadingModel == SM_UNLIT) {
        color = g.albedo;
    } else {
        float3 sunE = mainLightIlluminance();
        float viewDepth = dot(relPos, gCamForward.xyz);
        float shadow = sampleSunShadow(relPos, g.normal, viewDepth, id.xy);
        float ao = min(g.ao, tAO.SampleLevel(sLinearClamp, uv, 0));
        color = shadeSurface(g, relPos, V, sunE, shadow, ao);
    }
    int dbg = (int)gRenderParams.w;
    if (dbg > 0) {
        float3 sunE = mainLightIlluminance();
        float viewDepth = dot(relPos, gCamForward.xyz);
        float shadow = sampleSunShadow(relPos, g.normal, viewDepth, id.xy);
        float3 o = 0;
        if (dbg == 1) o = g.albedo;
        else if (dbg == 2) o = g.normal * 0.5 + 0.5;
        else if (dbg == 3) o = evalSH9(g.normal) * preExposure() * 4.0;
        else if (dbg == 4) o = shadow;
        else if (dbg == 5) o = float3(g.rough, g.metal, g.ao);
        else if (dbg == 6) o = shadeSurface(g, relPos, V, sunE, shadow, 1.0) * preExposure();
        else if (dbg == 7) o = aerialPerspective(uv, dist).a;
        else if (dbg == 8) o = aerialPerspective(uv, dist).rgb * preExposure() * 10.0;
        else if (dbg == 9) o = sunE * preExposure();
        if (any(isnan(o))) o = float3(1, 0, 1);
        uHDR[id.xy] = float4(o, 1);
        return;
    }
    // Aerial perspective
    float4 ap = aerialPerspective(uv, dist);
    color = color * ap.a + ap.rgb;
    uHDR[id.xy] = float4(min(color * preExposure() + emissive * ap.a, 60000.0), 1);
}
