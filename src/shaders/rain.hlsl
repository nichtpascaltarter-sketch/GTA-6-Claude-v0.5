// Weather: overhead height map conversion, camera-local rain streaks and splashes (occluded by roofs / bridges
// through the overhead map), and lightning bolts. Rain and bolts write a reactive mask so TAA does not smear
// them.
#include "skycommon.hlsli"
#include "shadow.hlsli"

cbuffer RainCB : register(b1) {
    float4 gRain0;        // x drop count, y box size (m), z box height (m), w intensity (0..1)
    float4 gRain1;        // xy wind velocity (m/s), z fall speed (m/s), w streak exposure time (s)
    float4 gRain2;        // x box center height above the camera (m), y splash distance (m), z light count, w bolt intensity
    float4 gRain3;        // x overhead map top (m, world), y overhead map depth range (m), zw unused
    float4 gRainLights[32];
};

Texture2D<float> tOverheadDepth : register(t0);
Texture2D<float4> tAtlas : register(t1);          // particle atlas (cell 44 = splash crown)
StructuredBuffer<float4> tBolt : register(t2);    // bolt segments: (p0 rel camera, width), (p1 rel camera, brightness)
RWTexture2D<float> uOverheadHeight : register(u0);

// Depth (top-down orthographic, standard Z) -> height of the highest static surface; -1000 where empty.
[numthreads(8, 8, 1)]
void csOverheadHeight(uint3 id : SV_DispatchThreadID) {
    uint w, h;
    tOverheadDepth.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;
    float d = tOverheadDepth[id.xy];
    uOverheadHeight[id.xy] = d < 1.0 ? gRain3.x - d * gRain3.y : -1000.0;
}

float surfaceHeightAt(float2 worldXY) {
    float2 tuv = (worldXY + 10240.0) / 20480.0;
    float hgt = gTerrainHeightG.SampleLevel(sLinearClamp, tuv, 0);
    float wl = gWaterLevelG.SampleLevel(sPointClamp, tuv, 0);
    if (wl > -999.0) hgt = max(hgt, wl);
    if (gOverhead.w > 0.5) {
        float2 ouv = overheadUV(worldXY);
        if (all(ouv > 0.0) && all(ouv < 1.0)) hgt = max(hgt, gOverheadMap.SampleLevel(sPointClamp, ouv, 0));
    }
    return hgt;
}

float phaseHG(float g, float c) { return (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5)); }

// Light reaching a drop (radiance scattered towards the camera, not exposed)
float3 dropLight(float3 rel, float3 V) {
    float3 L = (evalSH9(float3(0, 0, 1)) + evalSH9(-V)) * 0.5 * 0.6;
    L += mainLightIlluminance() * cloudShadowAt(rel) * phaseHG(0.75, dot(gSunDir.xyz, V)) * 0.04;
    int n = (int)gRain2.z;
    [loop] for (int i = 0; i < n; i++) {
        float4 lp = gRainLights[i * 2], lc = gRainLights[i * 2 + 1];
        float3 d = lp.xyz - rel;
        float d2 = dot(d, d);
        if (d2 > lp.w * lp.w) continue;
        float win = saturate(1.0 - sq(d2 / (lp.w * lp.w)));
        float3 ld = d * rsqrt(max(d2, 1e-4));
        L += lc.rgb * win / max(d2, 0.25) * phaseHG(0.55, dot(-ld, V)) * 2.0;
    }
    L += gAmbientParams.y * 0.02;  // lightning flash
    return L;
}

struct RainVSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 color : COLOR0;   // rgb pre-exposed radiance, a opacity
};

struct Drop {
    float3 rel;       // camera-relative head position
    float fall;
    float cycFrac;    // fall progress within the drop's cycle
    float topRel;     // cycle start height relative to the camera
};

Drop dropAt(uint inst) {
    uint h = hashU(inst * 747796405u + 2891336453u);
    float3 r3 = float3(hashF(h), hashF(h ^ 0x68bc21ebu), hashF(h ^ 0x02e5be93u));
    float B = gRain0.y, H = gRain0.z;
    Drop d;
    d.fall = gRain1.z * (0.85 + 0.3 * r3.z);
    float2 p = r3.xy * B + gRain1.xy * gTime.x;
    float2 relXY = p - gCamPos.xy;
    relXY -= B * round(relXY / B);
    d.cycFrac = frac(gTime.x * d.fall / H + hashF(h ^ 0x51ed270bu));
    d.topRel = gRain2.x + H * 0.5;
    d.rel = float3(relXY, d.topRel - d.cycFrac * H);
    return d;
}

RainVSOut vsRain(uint vid : SV_VertexID, uint inst : SV_InstanceID) {
    RainVSOut o = (RainVSOut)0;
    o.pos = float4(0, 0, -2, 1);
    Drop d = dropAt(inst);
    float3 world = d.rel + gCamPos.xyz;
    if (world.z < surfaceHeightAt(world.xy)) return o;   // below a roof, bridge, ground or water
    float dist = length(d.rel);
    float B = gRain0.y;
    float fade = smoothstep(0.6, 1.8, dist) * (1.0 - smoothstep(B * 0.3, B * 0.5, length(d.rel.xy)));
    if (fade <= 0.0) return o;
    float3 vel = float3(gRain1.xy, -d.fall);
    float3 head = d.rel, tail = d.rel - vel * gRain1.w;
    float3 V = normalize(-(head + tail) * 0.5);
    float3 axis = head - tail;
    float width = max(0.0035, dist * 0.0011);
    float3 side = normalize(cross(axis, V)) * width;
    float2 corner = float2(vid & 1u, vid >> 1u);
    float3 p = lerp(tail, head, corner.y) + side * (corner.x * 2.0 - 1.0);
    o.pos = mul(gViewProj, float4(p, 1));
    o.uv = corner;
    float alpha = gRain0.w * 0.3 * fade * saturate(0.0035 / width * 1.5);
    float3 L = dropLight(d.rel, -V) * preExposure();
    float4 fv = froxelFog(o.pos.xy / o.pos.w * float2(0.5, -0.5) + 0.5, dot(d.rel, gCamForward.xyz));
    o.color = float4(L * fv.a, alpha);
    return o;
}

struct RainOut {
    float4 color : SV_Target0;
    float reactive : SV_Target1;
};

RainOut psRain(RainVSOut i) {
    // soft across the streak, tapered at both ends
    float across = 1.0 - abs(i.uv.x * 2.0 - 1.0);
    float along = smoothstep(0.0, 0.25, i.uv.y) * smoothstep(1.0, 0.6, i.uv.y);
    float a = i.color.a * across * along;
    RainOut o;
    o.color = float4(i.color.rgb * a, a * 0.5);
    o.reactive = saturate(a * 5.0);
    return o;
}

// Splash crowns where drops land (same procedural drops as the streaks)
RainVSOut vsSplash(uint vid : SV_VertexID, uint inst : SV_InstanceID) {
    RainVSOut o = (RainVSOut)0;
    o.pos = float4(0, 0, -2, 1);
    Drop d = dropAt(inst);
    if (length(d.rel.xy) > gRain2.y) return o;
    float3 worldXY0 = d.rel + gCamPos.xyz;
    float hs = surfaceHeightAt(worldXY0.xy);
    float landFrac = (d.topRel + gCamPos.z - hs) / gRain0.z;
    if (landFrac <= 0.0 || landFrac >= 1.0) return o;
    float age = (d.cycFrac - landFrac) * gRain0.z / d.fall;
    const float life = 0.16;
    if (age < 0.0 || age > life) return o;
    float x = age / life;
    float3 base = float3(d.rel.xy, hs - gCamPos.z);
    float size = lerp(0.05, 0.13, sqrt(x));
    float3 camR = gView[0].xyz;
    float2 corner = float2(vid & 1u, vid >> 1u);
    float3 p = base + camR * (corner.x * 2.0 - 1.0) * size + float3(0, 0, corner.y * size * 1.2);
    o.pos = mul(gViewProj, float4(p, 1));
    o.uv = (float2(44 % 8, 44 / 8) + float2(corner.x, 1.0 - corner.y)) / 8.0;
    float3 V = normalize(-base);
    float3 L = dropLight(base, V) * preExposure();
    float4 fv = froxelFog(o.pos.xy / o.pos.w * float2(0.5, -0.5) + 0.5, dot(base, gCamForward.xyz));
    o.color = float4(L * 1.6 * fv.a, gRain0.w * (1.0 - x) * 0.8 * smoothstep(0.8, 2.0, length(base)));
    return o;
}

RainOut psSplash(RainVSOut i) {
    float4 t = tAtlas.Sample(sLinearClamp, i.uv);
    float a = t.a * i.color.a;
    RainOut o;
    o.color = float4(i.color.rgb * t.rgb * a, a * 0.5);
    o.reactive = saturate(a * 3.0);
    return o;
}

// Lightning bolt: camera-facing ribbons along the segments
RainVSOut vsBolt(uint vid : SV_VertexID, uint inst : SV_InstanceID) {
    RainVSOut o;
    float4 a = tBolt[inst * 2], b = tBolt[inst * 2 + 1];
    float3 p0 = a.xyz, p1 = b.xyz;
    float3 axis = p1 - p0;
    float3 V = normalize(-(p0 + p1) * 0.5);
    float3 side = normalize(cross(axis, V)) * a.w;
    float2 corner = float2(vid & 1u, vid >> 1u);
    float3 p = lerp(p0, p1, corner.y) + side * (corner.x * 2.0 - 1.0);
    o.pos = mul(gViewProj, float4(p, 1));
    o.uv = corner;
    float4 ap = aerialPerspective(o.pos.xy / o.pos.w * float2(0.5, -0.5) + 0.5, length(p));
    o.color = float4(float3(0.75, 0.8, 1.0) * b.w * gRain2.w * preExposure() * ap.a, 1);
    return o;
}

RainOut psBolt(RainVSOut i) {
    float across = 1.0 - abs(i.uv.x * 2.0 - 1.0);
    float core = pow(across, 4.0);
    RainOut o;
    o.color = float4(i.color.rgb * (core + across * 0.25), 0);
    o.reactive = 1.0;
    return o;
}
