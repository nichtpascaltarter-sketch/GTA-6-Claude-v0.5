// Procedural building facades: window layouts, curtain walls, storefronts and interior-mapped rooms.
// Facade uv: u = meters along the wall from the building corner, v = meters above the building base.
#ifndef FACADE_HLSLI
#define FACADE_HLSLI
#include "common.hlsli"
#include "skycommon.hlsli"
#include "materials.hlsli"

struct FacadeGPU {
    float floorH, groundH, bayW, winW;   // winW: window width fraction of bay
    float winH, sillH, roomDepth, style; // winH: window height fraction of floor
    uint wallColor, frameColor, glassColor, flags;
    float wallLayer, litFrac, seed, signIndex;
};
// The building set's tables (WorldRenderer::uploadFacades) through the bindless arrays (gBindlessFacade)
StructuredBuffer<FacadeGPU> gBindlessFacades[] : register(t0, space8);
StructuredBuffer<float4> gBindlessFloat4[] : register(t0, space9);
FacadeGPU facadeInfo(uint id) { return gBindlessFacades[gBindlessFacade.x][id]; }
Texture2D<float4> signAtlas() { return gBindlessTex2D[gBindlessFacade.y]; }   // shop signs: 8 x 64 grid of 256x32 cells
// Night architectural lighting per facade: x facade top (m above the base), y crown (0 none, 1 wash, 2 wash + LED
// lines on the wall edges), z palette index, w warm uplights along the base
float4 facadeLights(uint id) { return gBindlessFloat4[gBindlessFacade.z][id]; }

float3 archLightColor(uint idx) {
    // mostly white / warm white washes, a few signature colours
    static const float3 pal[8] = {float3(1.0, 0.8, 0.55), float3(0.88, 0.93, 1.0), float3(1.0, 0.86, 0.68), float3(1.0, 0.15, 0.6),
                                  float3(0.15, 0.7, 1.0), float3(0.5, 0.2, 1.0), float3(1.0, 0.78, 0.5), float3(0.2, 0.45, 1.0)};
    return pal[idx & 7u];
}

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

// Shelf wall of a shop: boards every 0.42 m up to 2.1 m holding rows of products (boxes, bottles, packaging).
float3 shelfPattern(float u, float v, uint h, float kind, float3 wallC) {
    if (v > 2.1 || v < 0.05) return wallC;
    float row = floor(v / 0.42), fv = frac(v / 0.42);
    if (fv < 0.07) return kind >= 0.9 ? float3(0.92, 0.92, 0.92) : float3(0.5, 0.5, 0.52);
    float cellW = 0.14 + hashF(h + (uint)row * 7u) * 0.14;
    float cell = floor(u / cellW), fu = frac(u / cellW);
    uint ph = hash3u(uint3((uint)(cell + 4096.0), (uint)row, h));
    float fill = 0.5 + hashF(ph) * 0.45;
    if (fu < 0.07 || fu > 0.93 || fv > 0.07 + fill * 0.93) return wallC * 0.25;   // shadowed shelf back
    float3 pc = hsvToRgbF(hashF(ph + 1u)) * lerp(0.35, 0.85, hashF(ph + 2u)) + 0.06;
    if (hashF(ph + 3u) < 0.3) pc = lerp(pc, float3(0.9, 0.9, 0.88), 0.7);   // white packaging
    if (kind >= 0.9) pc = lerp(float3(0.92, 0.93, 0.95), pc, 0.3);          // pharmacy: mostly white boxes
    return pc * lerp(0.8, 1.0, fu);
}

// Clothes rail at 1.6 m with garments of varying length, colour runs per rail section.
float3 garmentPattern(float u, float v, uint h, float3 wallC) {
    if (abs(v - 1.63) < 0.02) return float3(0.75, 0.75, 0.78);
    if (v > 1.61 || v < 0.55) return wallC;
    float g = floor(u / 0.085), fg = frac(u / 0.085);
    uint gh = hash3u(uint3((uint)(g + 8192.0), 3u, h));
    float len = 0.5 + hashF(gh) * 0.5;
    if (v < 1.61 - len || fg < 0.05) return wallC * 0.6;
    uint grp = hash3u(uint3((uint)(floor(u / 0.6) + 8192.0), 5u, h));
    float3 c = hsvToRgbF(hashF(grp) + (hashF(gh + 1u) - 0.5) * 0.08) * lerp(0.25, 0.75, hashF(grp + 2u)) + 0.05;
    return c * lerp(0.75, 1.0, sin(fg * PI));
}

// Interior mapping for a shop behind a storefront (one space per sign, 3 bays): grocery / pharmacy shelving,
// clothing rails or a cafe, with a free-standing fixture in the middle and ceiling light panels (> 1 = emitter).
float3 shopInterior(float3 roomPos, float3 dir, float3 roomSize, uint h) {
    float kind = hashF(h + 40u);
    bool cafe = kind >= 0.7 && kind < 0.9;
    bool clothing = kind >= 0.45 && kind < 0.7;
    float3 invD = 1.0 / (dir + (dir == 0) * 1e-5);
    float3 tPlanes = max((roomSize * (dir > 0) - roomPos) * invD, 0.0);
    float t = min(min(tPlanes.x, tPlanes.y), tPlanes.z);
    float3 wallC = cafe ? float3(0.42, 0.28, 0.18) : lerp(float3(0.62, 0.61, 0.58), float3(0.52, 0.58, 0.62), hashF(h + 41u));
    float3 brandC = hsvToRgbF(hashF(h + 50u)) * 0.75 + 0.05;   // shop's colour: stripe along the upper walls
    float W = roomSize.x, D = roomSize.z;
    float3 bmin, bmax;
    if (cafe) { bmin = float3(W * 0.12, 0.0, D * 0.3); bmax = float3(W * 0.88, 0.76, D * 0.42); }
    else if (clothing) { bmin = float3(W * 0.3, 0.55, D * 0.42); bmax = float3(W * 0.7, 1.65, D * 0.5); }
    else { bmin = float3(W * 0.2, 0.0, D * 0.45); bmax = float3(W * 0.8, 1.45, D * 0.6); }
    float3 t0 = (bmin - roomPos) * invD, t1 = (bmax - roomPos) * invD;
    float3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tn = max(max(tmin.x, tmin.y), tmin.z), tf = min(min(tmax.x, tmax.y), tmax.z);
    float3 c;
    if (tn < tf && tn > 0.0 && tn < t) {
        float3 p = roomPos + dir * tn;
        bool front = tn == tmin.z;   // the face looking at the window
        if (cafe) c = p.y > 0.72 ? float3(0.5, 0.36, 0.24) : float3(0.18, 0.12, 0.08);
        else if (clothing) c = front ? garmentPattern(p.x, p.y, h + 9u, wallC) : float3(0.3, 0.3, 0.32);
        else c = front ? shelfPattern(p.x, p.y, h + 11u, kind, float3(0.6, 0.6, 0.62)) : float3(0.55, 0.55, 0.57);
        t = tn;
    } else {
        float3 hit = roomPos + dir * t;
        if (t == tPlanes.y) {
            if (dir.y > 0) {
                float2 q = frac(hit.xz / float2(1.6, 1.8));
                bool panel = abs(q.x - 0.5) < 0.3 && abs(q.y - 0.5) < 0.1;
                c = cafe ? (panel ? float3(2.2, 1.5, 0.9) : float3(0.25, 0.18, 0.12)) : (panel ? float3(3.0, 3.0, 2.9) : float3(0.82, 0.82, 0.8));
            } else {
                float2 q = floor(hit.xz / 0.6);
                float chk = frac((q.x + q.y) * 0.5) * 2.0;
                c = cafe ? lerp(float3(0.3, 0.19, 0.11), float3(0.36, 0.23, 0.14), chk) : lerp(float3(0.78, 0.78, 0.75), float3(0.62, 0.62, 0.6), chk);
            }
        } else {
            bool back = t == tPlanes.z;
            float u = back ? hit.x : hit.z;
            if (cafe) {
                c = wallC;
                if (back && hit.y < 1.05) c = float3(0.22, 0.14, 0.09);                                             // counter
                if (back && hit.y > 1.5 && hit.y < 2.3) c = shelfPattern(u, hit.y - 1.5, h + 13u, 0.5, wallC) * 0.8;  // bottles
            } else if (clothing) {
                c = garmentPattern(u, hit.y, h + (back ? 17u : 19u), wallC);
            } else {
                c = shelfPattern(u, hit.y, h + (back ? 21u : 23u), kind, wallC);
            }
            if (!cafe && hit.y > 2.3) c = (hit.y > 2.45 && hit.y < 2.8) ? brandC : wallC * 0.7;   // brand stripe, darker frieze
        }
    }
    return c * lerp(1.0, 0.65, saturate(t / (D * 1.6)));
}

FacadeResult shadeFacade(uint id, float2 uv, float3 N, float3 T, float3 B, float3 rel, float3 worldP) {
    FacadeGPU f = facadeInfo(id);
    Texture2DArray<float4> albedoArr = matAlbedoArray();
    Texture2DArray<float4> normalArr = matNormalArray();
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
    float rough = wn.z;
    // Close-up detail: the wall layer again at a higher, rotated frequency (stucco grain, concrete pores, brick
    // texture) with roughness variation; fades out by 20 m
    float detailW = saturate(1.0 - length(rel) / 20.0);
    if (detailW > 0.0) {
        float2 duv = float2(wuv.x * 0.8 - wuv.y * 0.6, wuv.x * 0.6 + wuv.y * 0.8) * 5.3 + 0.41;
        float4 wn2 = normalArr.Sample(sAnisoWrap, float3(duv, f.wallLayer));
        nxy += (wn2.xy * 2.0 - 1.0) * 0.6 * detailW;
        rough = saturate(rough * lerp(1.0, 0.7 + wn2.z * 0.6, detailW * 0.6));
    }
    float3 nts = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
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

    // Weathering on the wall: dirt at the base of the building, rain streaks below window sills and slab edges
    if (!inWin) {
        float baseGrime = saturate(1.0 - uv.y / (0.9 + valueNoise(float2(uv.x * 1.7, 3.1)) * 0.8));
        float underSill = (wl.x > -0.05 && wl.x < winW + 0.05 && wl.y < 0.0) ? saturate(1.0 + wl.y / (0.6 + hashF(roomHash + 31u) * 1.4)) : 0.0;
        float drip = valueNoise(float2(uv.x * 11.0, uv.y * 0.6)) * valueNoise(float2(uv.x * 23.0 + 5.0, uv.y * 1.3));
        float streaks = underSill * saturate(drip * 3.0 - 0.2) * (style == 1 ? 0.0 : 1.0);
        // soot / dirt halo around the window openings (strongest at the head, where runoff collects)
        float2 wd = float2(max(max(-wl.x, wl.x - winW), 0.0), max(max(-wl.y, wl.y - winH), 0.0));
        float halo = saturate(1.0 - length(wd) / (0.22 + 0.12 * valueNoise(uv * 3.7))) * (wl.y > winH ? 1.0 : 0.55) * (style == 1 ? 0.0 : 1.0);
        float grime = saturate(baseGrime * 0.7 + streaks * 0.55 + halo * 0.3);
        albedo *= lerp(1.0, float3(0.6, 0.58, 0.55), grime);
        rough = saturate(rough + grime * 0.1);
    }
    float3 outAlbedo = albedo;
    float3 outN = normalize(T * nts.x + B * nts.y + N * nts.z);
    if (signBand) {
        // Shop sign from the atlas: 8 columns x 64 rows of 256x32 cells
        uint si = (uint)f.signIndex + (uint)bayIdx / 3u;
        float2 cellUV = float2(frac(uv.x / (bay * 3.0)), 1.0 - saturate((fy - (fh - 1.05)) / 0.9));
        float2 atlasUV = (float2(si % 8u, (si / 8u) % 64u) + cellUV) / float2(8.0, 64.0);
        float4 sg = signAtlas().SampleLevel(sLinearClamp, atlasUV, 0);
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
        bool office = (flags & 16u) != 0;
        // Storefronts: one shop per sign (3 bays) with its own interior, opening hours and lighting
        float shopIdx = floor(bayIdx / 3.0);
        uint shopHash = hash3u(uint3((uint)(shopIdx + 2000.0), 77u, seed));
        uint lh = storefront ? shopHash : roomHash;
        float la;
        float3 room;
        if (storefront) room = shopInterior(float3(fx + (bayIdx - shopIdx * 3.0) * bay, fy, 0.02), dir, float3(bay * 3.0, fh, f.roomDepth * 1.4), shopHash);
        else room = interiorRoom(float3(fx, fy, 0.02), dir, float3(bay, fh, f.roomDepth), roomHash, la, office);
        // Window coverings (homes and offices): none, a roller blind pulled down to some height, venetian blinds
        // (25 mm slats, tilted partly open) or a pair of curtains drawn in from the sides (folds). Lit rooms glow
        // through them (translucent fabric, light between the slats).
        float ctype = hashF(roomHash + 5u);
        float pRoller = (flags & 8u) ? 0.35 : 0.15;
        float pVenet = office ? 0.45 : 0.15;
        float pCurt = office ? 0.0 : 0.3;
        uint covering = storefront ? 0u : (ctype < pRoller ? 1u : (ctype < pRoller + pVenet ? 2u : (ctype < pRoller + pVenet + pCurt ? 3u : 0u)));
        float3 coverC = office ? float3(0.7, 0.7, 0.68) : lerp(float3(0.82, 0.76, 0.66), float3(0.5, 0.22, 0.16), hashF(roomHash + 7u));
        float cover = 0.0, coverTrans = 0.0;
        if (covering == 1u) {
            cover = step(1.0 - hashF(roomHash + 6u), wl.y / winH);   // hangs from the head of the window
            coverTrans = 0.35;
        } else if (covering == 2u) {
            float openAmt = 0.15 + hashF(roomHash + 6u) * 0.5;
            float raised = hashF(roomHash + 8u) < 0.3 ? hashF(roomHash + 9u) * 0.6 : 0.0;   // some pulled part way up
            float slat = frac(wl.y / 0.025);
            float px = max(length(rel) * 0.0012, 1e-4) / 0.025;     // slats per pixel: average out before they alias
            float slatCover = lerp(step(openAmt, slat), 1.0 - openAmt, saturate(px * 2.0 - 0.5));
            cover = slatCover * step(raised, wl.y / winH);
            coverTrans = 0.12;
            coverC *= 0.85 + 0.15 * slat;
        } else if (covering == 3u) {
            float drawn = lerp(0.18, 0.9, hashF(roomHash + 6u));
            float fromSide = min(wl.x, winW - wl.x);
            cover = step(fromSide, winW * 0.5 * drawn);
            coverC *= 0.72 + 0.28 * sin(wl.x * 40.0 + hashF(roomHash + 8u) * 6.0) * sin(wl.x * 40.0 + hashF(roomHash + 8u) * 6.0);
            coverTrans = 0.3;
        }
        // Lighting schedule: every room has its own switch-on / switch-off times, so windows light up one by
        // one at dusk and go dark gradually through the night (some stay lit all night, early risers at dawn).
        float hour = gTime.y;
        float hc = hour < 12.0 ? hour + 24.0 : hour;             // evening-continuous clock: 12..36
        bool occupied = hashF(roomHash + 11u) < f.litFrac * 0.85;
        float onT = 17.0 + hashF(roomHash + 15u) * 3.8;
        float offT = 21.3 + pow(hashF(roomHash + 16u), 1.6) * 7.0;
        bool allNight = hashF(roomHash + 17u) < 0.06;
        bool eveningLit = hc >= onT && (hc < offT || allNight) && hc < 30.5 + hashF(roomHash + 18u);
        bool morningLit = hour > 5.2 + hashF(roomHash + 19u) && hour < 7.4 + hashF(roomHash + 20u) * 1.3 && hashF(roomHash + 21u) < 0.35;
        bool officeLit = office && hour > 7.0 + hashF(roomHash + 22u) * 1.5 && hour < 17.5 + pow(hashF(roomHash + 23u), 3.0) * 5.0;
        bool shopOpen = storefront && hour > 7.0 + hashF(lh + 24u) && hour < 22.0 + hashF(lh + 25u) * 3.0;
        bool display = storefront && hashF(lh + 27u) < 0.4;   // closed shops that keep their display lighting on
        bool lit = storefront ? (shopOpen || display)
                              : (occupied && (office ? officeLit || (eveningLit && hashF(roomHash + 26u) < 0.3) : (eveningLit || morningLit)));
        float3 lightC = office ? lerp(float3(0.95, 0.9, 0.8), float3(0.85, 0.93, 1.0), hashF(roomHash + 12u))
                               : lerp(float3(1.0, 0.6, 0.3), float3(1.0, 0.82, 0.6), hashF(roomHash + 12u) * 0.8);
        if (!office && hashF(roomHash + 13u) > 0.93) lightC = float3(0.4, 0.55, 1.0);  // TV glow
        if (storefront) lightC = lerp(float3(1.0, 0.8, 0.58), float3(0.86, 0.94, 1.0), hashF(lh + 28u));  // halogen .. LED
        // Interior radiance: lit rooms emit; unlit rooms show a dim daylight interior. The room is radiance seen
        // through the glass (not a surface lit on the facade), so shadows falling on the glass do not darken it.
        float dayInterior = saturate(gSunDir.z * 3.0 + 0.1) * (1.0 - gExposure.w);
        // storefronts look into a lit shop through clear glass, not through the tower's tinted curtain-wall glass
        float3 glassTint = storefront ? lerp(glassC, float3(0.86, 0.9, 0.92), 0.6) : glassC;
        // daylight in the room: sky light through the windows plus sun patches on the floor bouncing around
        float3 dayE = evalSH9(N) * 0.4 + mainLightIlluminance() * saturate(dot(N, gSunDir.xyz)) * (0.12 / PI);
        float3 em = room * dayE * dayInterior;
        // shops are brightly lit inside (~160 nits of interior by day reads through the glass; 14 at night, when the
        // exposure has opened up), homes and offices keep a constant lamp level
        float shopNits = lerp(160.0, 14.0, gExposure.w);
        float lampNits = (storefront ? shopNits : 9.0) * (0.45 + 0.9 * hashF(lh + 14u));
        if (lit) em += room * lightC * lampNits;
        // a covering right behind the glass is lit from the street (albedo) and glows with the room light behind it
        float3 coverEm = lit ? coverC * coverTrans * lightC * lampNits : 0.0;
        em = lerp(em, coverEm, cover);
        // Glass surface: dark and reflective (Fresnel reflections of the street from SSR and the probe on top)
        outAlbedo = lerp(glassTint * 0.02, coverC * glassTint * 0.8, cover);
        r.emissive = em * glassTint * 1.2;
        rough = 0.04;
        r.metal = 0.0;
        outN = N;
        // slight waviness of glass panes for reflection variation
        float2 pane = float2(bayIdx, floorIdx);
        float3 wob = float3(hashF(roomHash + 21u) - 0.5, hashF(roomHash + 22u) - 0.5, 0) * 0.02;
        outN = normalize(N + T * wob.x + B * wob.y);
        // Closed shops without display lighting pull down ribbed roller shutters (some tagged with graffiti)
        if (storefront && !shopOpen && !display && hashF(lh + 29u) < 0.7) {
            float slat = frac(uv.y / 0.085);
            float3 metalC = float3(0.5, 0.51, 0.52) * lerp(0.75, 1.05, valueNoise(float2(uv.x * 0.7, uv.y * 3.0)));
            float tag = hashF(lh + 30u) < 0.45 ? saturate(valueNoise(uv * float2(1.4, 2.2) + hashF(lh + 31u) * 40.0) * 2.4 - 1.25) : 0.0;
            outAlbedo = lerp(metalC, hsvToRgbF(hashF(lh + 32u) + uv.x * 0.02) * 0.55, tag) * lerp(0.7, 1.0, sin(slat * PI));
            outN = normalize(N - B * (slat - 0.5) * 0.9);
            rough = lerp(0.42, 0.6, tag);
            r.metal = 0.65 * (1.0 - tag);
            r.emissive = 0;
            r.isWindow = false;
        }
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
    // Night architectural lighting: coloured crown washes (and LED lines up the wall edges) on towers, warm
    // scalloped uplights along the base of deco buildings, churches and some mid-rises
    float nightA = gExposure.w;
    if (nightA > 0.0) {
        float4 xl = facadeLights(id);
        float3 wash = 0;
        if (xl.y > 0.5 && xl.x > 12.0) {
            float3 cc = archLightColor((uint)xl.z);
            float below = xl.x - uv.y;   // meters below the roof line
            wash += cc * exp(-max(below, 0.0) / (2.5 + xl.x * 0.025)) * 38.0;
            if (xl.y > 1.5 && uv.y > xl.x * 0.35 && below > -0.5) {
                float lw = max(0.18, length(rel) * 0.0012);   // widen with distance (no sub-pixel shimmer)
                r.emissive += cc * step(uv.x, lw) * (400.0 * 0.18 / lw) * nightA;
            }
        }
        if (xl.w > 0.5) {
            float spot = pow(saturate(cos((fx / bay - 0.5) * PI)), 2.0);
            wash += float3(1.0, 0.72, 0.45) * exp(-uv.y / 5.5) * saturate(uv.y * 2.0) * lerp(0.35, 1.0, spot) * 45.0;
        }
        float3 recv = r.isWindow ? max(outAlbedo, 0.12) : outAlbedo;   // glass: lit mullions / frit
        r.emissive += recv * wash * (nightA / PI);
    }
    r.albedo = outAlbedo;
    r.normal = outN;
    r.rough = rough;
    r.ao = ao;
    return r;
}
#endif
