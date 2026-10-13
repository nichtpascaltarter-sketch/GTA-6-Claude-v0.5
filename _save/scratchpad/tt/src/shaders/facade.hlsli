// Procedural building facades: window layouts, curtain walls, storefronts and interior-mapped rooms.
// Facade uv: u = meters along the wall from the building corner, v = meters above the building base.
#ifndef FACADE_HLSLI
#define FACADE_HLSLI
#include "common.hlsli"

struct FacadeGPU {
    float floorH, groundH, bayW, winW;   // winW: window width fraction of bay
    float winH, sillH, roomDepth, style; // winH: window height fraction of floor
    uint wallColor, frameColor, glassColor, flags;
    float wallLayer, litFrac, seed, signIndex;
};
StructuredBuffer<FacadeGPU> tFacades : register(t13);
Texture2D<float4> tSigns : register(t14);   // shop sign atlas (8 x 64 grid of 256x32 cells)

// style: 0 punched windows, 1 curtain wall, 2 ribbon windows, 3 balconies/condo, 4 industrial (few windows),
//        5 house (shuttered windows), 6 art deco (vertical fins + eyebrows)
// flags: bit0 storefront ground floor, bit1 has sign band, bit2 neon trim, bit3 blinds common, bit4 office

float3 unpackColor(uint c) { return float3(c & 255, (c >> 8) & 255, (c >> 16) & 255) / 255.0; }

struct FacadeResult {
    float3 albedo;
    float3 normal;
    float rough;
    float metal;
    float3 emissive;
    float ao;
    bool isWindow;
};

// Interior mapping: returns radiance-ish color (0..1 albedo when lit by room light) of a virtual room.
float3 interiorRoom(float3 roomPos, float3 dir, float3 roomSize, uint roomHash, out float lightAmt, bool office) {
    // roomPos: entry point in room space (x along facade, y up, z into room), dir into room (z>0)
    float3 invD = 1.0 / (dir + (dir == 0) * 1e-5);
    float3 tPlanes = max((roomSize * (dir > 0) - roomPos) * invD, 0.0);
    float t = min(min(tPlanes.x, tPlanes.y), tPlanes.z);
    float3 hit = roomPos + dir * t;
    float h0 = hashF(roomHash), h1 = hashF(roomHash + 1u), h2 = hashF(roomHash + 2u), h3 = hashF(roomHash + 3u);
    float3 wallC = lerp(float3(0.75, 0.72, 0.66), float3(0.55, 0.62, 0.7), h1);
    if (h2 > 0.8) wallC = float3(0.72, 0.55, 0.45);
    float3 floorC = office ? float3(0.25, 0.26, 0.28) : lerp(float3(0.35, 0.22, 0.12), float3(0.6, 0.58, 0.55), h3);
    float3 ceilC = float3(0.85, 0.85, 0.83);
    float3 c;
    if (t == tPlanes.z) {
        c = wallC * 0.85;
        // picture / shelf / window on the back wall
        float2 q = hit.xy / roomSize.xy;
        if (!office && h0 > 0.4 && abs(q.x - 0.5) < 0.18 && abs(q.y - 0.55) < 0.12) c = lerp(float3(0.2, 0.3, 0.45), float3(0.6, 0.3, 0.2), h2);
        if (office && abs(frac(q.x * 3.0) - 0.5) < 0.35 && q.y < 0.45) c = float3(0.3, 0.3, 0.32);
    } else if (t == tPlanes.y) {
        c = dir.y > 0 ? ceilC : floorC;
        if (dir.y > 0 && office) {
            float2 q = frac(hit.xz / float2(1.2, 1.2));
            if (abs(q.x - 0.5) < 0.35 && abs(q.y - 0.5) < 0.1) c = float3(1.4, 1.4, 1.35);
        }
    } else {
        c = wallC;
    }
    // Furniture box (sofa / desk) near the back
    float3 bmin = float3(roomSize.x * (0.15 + h0 * 0.3), 0.0, roomSize.z * (0.55 + h1 * 0.2));
    float3 bmax = bmin + float3(roomSize.x * 0.35, office ? 0.75 : 0.85, roomSize.z * 0.25);
    float3 t0 = (bmin - roomPos) * invD, t1 = (bmax - roomPos) * invD;
    float3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tn = max(max(tmin.x, tmin.y), tmin.z), tf = min(min(tmax.x, tmax.y), tmax.z);
    if (tn < tf && tn > 0 && tn < t) c = office ? float3(0.35, 0.33, 0.3) : lerp(float3(0.3, 0.12, 0.08), float3(0.15, 0.2, 0.35), h2);
    // depth darkening
    c *= lerp(1.0, 0.55, saturate(t / (roomSize.z * 1.5)));
    lightAmt = 1;
    return c;
}

FacadeResult shadeFacade(uint id, float2 uv, float3 N, float3 T, float3 B, float3 rel, float3 worldP,
                         Texture2DArray<float4> albedoArr, Texture2DArray<float4> normalArr) {
    FacadeGPU f = tFacades[id];
    FacadeResult r;
    r.metal = 0;
    r.emissive = 0;
    r.ao = 1;
    r.isWindow = false;
    uint style = (uint)f.style;
    uint seed = asuint(f.seed);
    float3 wallC = unpackColor(f.wallColor);
    float3 frameC = unpackColor(f.frameColor);
    float3 glassC = unpackColor(f.glassColor);
    uint flags = f.flags;

    // Wall material sample (tiling in meters, layer from table)
    float2 wuv = uv / 3.0;
    float4 wa = albedoArr.Sample(sAnisoWrap, float3(wuv, f.wallLayer));
    float4 wn = normalArr.Sample(sAnisoWrap, float3(wuv, f.wallLayer));
    float3 albedo = wa.rgb * wallC * 1.6;
    float2 nxy = wn.xy * 2.0 - 1.0;
    float3 nts = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
    float rough = wn.z;
    float ao = wn.w;

    // Floor / bay cells
    bool ground = uv.y < f.groundH;
    float floorIdx = ground ? 0 : floor((uv.y - f.groundH) / f.floorH) + 1;
    float fy = ground ? uv.y : (uv.y - f.groundH) - (floorIdx - 1) * f.floorH;
    float fh = ground ? f.groundH : f.floorH;
    float bay = f.bayW;
    float bayIdx = floor(uv.x / bay);
    float fx = uv.x - bayIdx * bay;
    uint roomHash = hash3u(uint3((uint)(bayIdx + 1000), (uint)floorIdx, seed));

    float winW = bay * f.winW;
    float winH = fh * f.winH;
    float sill = f.sillH;
    bool storefront = ground && (flags & 1u);
    if (storefront) { winW = bay * 0.92; sill = 0.35; winH = fh - 1.3; }
    if (style == 1 && !storefront) { winW = bay - 0.12; winH = fh - 0.1; sill = 0.05; }       // curtain wall
    if (style == 2 && !storefront) { winW = bay; sill = fh * 0.38; winH = fh * 0.45; }        // ribbon
    if (style == 4 && !storefront) { winH = fh * 0.25; sill = fh * 0.6; winW = bay * 0.5; }   // industrial high windows
    float x0 = (bay - winW) * 0.5;
    float2 wl = float2(fx - x0, fy - sill);  // position within window rect
    bool inWin = wl.x >= 0 && wl.x <= winW && wl.y >= 0 && wl.y <= winH;
    // Industrial: only some bays have windows
    if (style == 4 && hashF(roomHash + 9u) < 0.5 && !storefront) inWin = false;
    // Sign band above storefronts
    bool signBand = (flags & 2u) && ground && fy > fh - 1.05 && fy < fh - 0.15;

    // Frame around the window
    float frameW = style == 1 ? 0.06 : 0.09;
    float edgeD = inWin ? min(min(wl.x, winW - wl.x), min(wl.y, winH - wl.y)) : -1;
    bool inFrame = inWin && edgeD < frameW;
    // Mullions for curtain walls and storefronts
    if (inWin && (style == 1 || storefront)) {
        float mx = frac(wl.x / (winW / max(1.0, floor(winW / 1.4))));
        if (min(mx, 1.0 - mx) * (winW / max(1.0, floor(winW / 1.4))) < 0.03) inFrame = true;
    }
    // Art deco eyebrows (shading ledges above windows) and vertical fins
    if (style == 6 && !ground) {
        if (fy > sill + winH && fy < sill + winH + 0.18) { albedo = wallC * 0.95; nts = float3(0, -0.6, 0.8); ao *= 0.8; }
        float finX = frac(uv.x / (bay * 2.0));
        if (abs(finX - 0.5) < 0.03) { albedo = unpackColor(f.frameColor); }
    }
    // Recess shadow around windows (fake depth)
    if (!inWin && style != 1) {
        float2 dd = float2(max(-wl.x, wl.x - winW), max(-wl.y, wl.y - winH));
        float outside = max(dd.x, dd.y);
        if (outside < 0.12 && outside > 0) ao *= lerp(0.75, 1.0, outside / 0.12);
        // sill ledge
        if (wl.x > -0.08 && wl.x < winW + 0.08 && wl.y < 0 && wl.y > -0.1 && style != 2) { albedo = lerp(albedo, float3(0.8, 0.8, 0.78), 0.6); nts = float3(0, 0.7, 0.7); }
    }
    // Floor slab lines for curtain walls
    if (style == 1 && !ground && fy < 0.35) { inWin = false; albedo = frameC * 0.8; rough = 0.4; r.metal = 0.6; }

    float3 outAlbedo = albedo;
    float3 outN = normalize(T * nts.x + B * nts.y + N * nts.z);
    if (signBand) {
        // Shop sign from the atlas: 8 columns x 64 rows of 256x32 cells
        uint si = (uint)f.signIndex + (uint)bayIdx / 3u;
        float2 cellUV = float2(frac(uv.x / (bay * 3.0)), 1.0 - saturate((fy - (fh - 1.05)) / 0.9));
        float2 atlasUV = (float2(si % 8u, (si / 8u) % 64u) + cellUV) / float2(8.0, 64.0);
        float4 sg = tSigns.SampleLevel(sLinearClamp, atlasUV, 0);
        float3 bg = unpackColor(f.frameColor) * 0.6;
        outAlbedo = lerp(bg, sg.rgb, sg.a);
        rough = 0.35;
        // Signs glow at night
        float night = gExposure.w;
        r.emissive = sg.rgb * sg.a * (30.0 + 220.0 * night) * ((flags & 4u) ? 1.5 : 0.6);
        outN = N;
    } else if (inFrame) {
        outAlbedo = frameC;
        rough = 0.35;
        r.metal = style == 1 ? 0.7 : 0.0;
        outN = N;
    } else if (inWin) {
        r.isWindow = true;
        // View ray into the room (room space: x along T, y up, z into the building)
        float3 V = normalize(-rel);
        float3 dir = normalize(float3(-dot(V, T), -dot(V, B), dot(V, N)));
        float roomW = storefront ? bay : bay;
        float3 roomSize = float3(roomW, fh, f.roomDepth);
        float3 roomPos = float3(fx, fy, 0.02);
        bool office = (flags & 16u) != 0;
        float la;
        float3 room = interiorRoom(roomPos, dir, roomSize, roomHash, la, office);
        // Blinds / curtains
        float blindsAmt = hashF(roomHash + 5u);
        bool blinds = (flags & 8u) ? blindsAmt < 0.6 : blindsAmt < 0.25;
        float blindLevel = hashF(roomHash + 6u);
        bool coveredByBlind = blinds && (wl.y / winH) > blindLevel;
        float3 blindC = office ? float3(0.7, 0.7, 0.68) : lerp(float3(0.8, 0.75, 0.65), float3(0.5, 0.2, 0.15), hashF(roomHash + 7u));
        // Lighting schedule
        float hour = gTime.y;
        float evening = smoothstep(17.5, 19.5, hour) * (1.0 - smoothstep(23.0, 25.0, hour)) + (hour < 6.0 ? 0.25 : 0.0);
        float dayOffice = office ? smoothstep(7.0, 8.5, hour) * (1.0 - smoothstep(18.0, 21.0, hour)) : 0;
        float litProb = saturate(f.litFrac * 0.75 * (evening + dayOffice * 0.9) + (storefront ? 0.85 : 0));
        bool lit = hashF(roomHash + 11u) < litProb;
        float3 lightC = lerp(float3(1.0, 0.72, 0.45), float3(0.95, 0.97, 1.0), office ? 0.85 : hashF(roomHash + 12u) * 0.6);
        if (!office && hashF(roomHash + 13u) > 0.93) lightC = float3(0.4, 0.55, 1.0);  // TV glow
        float3 inside = coveredByBlind ? blindC : room;
        // Interior radiance: lit rooms emit, unlit rooms show dim daylight interior
        float dayInterior = saturate(gSunDir.z * 3.0 + 0.1) * (1.0 - gExposure.w);
        float3 glassTint = glassC;
        float3 interiorAlbedo = inside * glassTint;
        float3 em = 0;
        if (lit) em = inside * lightC * (storefront ? 38.0 : 14.0) * (0.6 + 0.8 * hashF(roomHash + 14u));
        // Glass surface: dark reflective; interior visible through it
        outAlbedo = interiorAlbedo * 0.35 * dayInterior + glassTint * 0.02;
        r.emissive = em * glassTint * 1.2;
        rough = 0.04;
        r.metal = 0.0;
        outN = N;
        // slight waviness of glass panes for reflection variation
        float2 pane = float2(bayIdx, floorIdx);
        float3 wob = float3(hashF(roomHash + 21u) - 0.5, hashF(roomHash + 22u) - 0.5, 0) * 0.02;
        outN = normalize(N + T * wob.x + B * wob.y);
    }
    // Balconies style: horizontal slab edges every floor + railing band
    if (style == 3 && !ground) {
        if (fy < 0.25) { outAlbedo = float3(0.85, 0.85, 0.83); outN = N; r.isWindow = false; r.emissive = 0; rough = 0.8; }
        else if (fy < 1.1 && !inWin) { outAlbedo = lerp(outAlbedo, frameC, 0.5); }
    }
    // Neon trim at roof line / floor lines for art deco
    if ((flags & 4u) && style == 6) {
        float band = abs(fy - (fh - 0.1));
        if (band < 0.05 && !ground) {
            float3 neon = hsvToRgbF(hashF(seed));
            r.emissive += neon * (20.0 + 260.0 * gExposure.w);
            outAlbedo = neon;
        }
    }
    r.albedo = outAlbedo;
    r.normal = outN;
    r.rough = rough;
    r.ao = ao;
    return r;
}
#endif
