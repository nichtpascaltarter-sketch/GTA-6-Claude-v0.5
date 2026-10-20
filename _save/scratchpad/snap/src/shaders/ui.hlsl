// 2D UI: solid, SDF text, images, rounded rects, circles, capsules, arcs, SDF icons, map, frosted backdrop, AA triangles,
// photo grading (depth of field + filters). Premultiplied alpha output (mode + 32 = additive: alpha written as 0).
cbuffer UICB : register(b1) {
    float4 gUIScreen;   // w, h, 1/w, 1/h
    float4 gUIClip;
    float4 gUIClipMode; // x mode (0 none, 1 circle, 2 rect, 3 rounded rect), y rounded-rect radius
};
Texture2D<float> tAtlas : register(t0);
Texture2D<float4> tImage : register(t1);
Texture2D<float> tIcons : register(t2);
Texture2D<float4> tBlur : register(t3);
Texture2D<float4> tScene : register(t4);   // full-resolution copy of the frame before the UI
Texture2D<float> tDepth : register(t5);    // scene depth (reversed-Z infinite) for photo DOF
Texture2D<float4> tHalf : register(t6);    // half-resolution copy (light blur level)
SamplerState sLinear : register(s1);

cbuffer UIPhotoCB : register(b3) {
    float4 gPhA;   // filter, strength, exposure (EV), contrast
    float4 gPhB;   // saturation, temperature, vignette, grain
    float4 gPhC;   // dof, focus distance, blur scale, nearZ (0 = no depth: screen-space focus band)
    float4 gPhD;   // time, screen aspect
};

float photoLuma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// Photo mode filters (display-referred colors)
float3 photoFilter(int f, float3 c) {
    float l = photoLuma(c);
    if (f == 1) {            // neon nights: magenta shadows, cyan highlights, punchy
        float3 split = lerp(float3(0.95, 0.30, 0.80), float3(0.40, 0.95, 1.0), smoothstep(0.1, 0.8, l));
        c = lerp(c, c * split * 1.3, 0.6);
        c = (c - 0.5) * 1.12 + 0.5;
        float nl = photoLuma(c);
        c = lerp(float3(nl, nl, nl), c, 1.25);
    } else if (f == 2) {     // golden hour: warm, lifted amber shadows, soft contrast
        c *= float3(1.12, 1.0, 0.8);
        c = lerp(c, c * float3(1.04, 0.9, 0.72) + float3(0.07, 0.04, 0.0), 0.45);
        c = (c - 0.5) * 0.94 + 0.51;
    } else if (f == 3) {     // noir: high contrast black and white
        float g = saturate((l - 0.5) * 1.5 + 0.48);
        g = lerp(g, g * g * (3.0 - 2.0 * g), 0.5);
        c = float3(g, g, g);
    } else if (f == 4) {     // vintage 86: faded blacks, warm highlights, muted
        c = lerp(float3(l, l, l), c, 0.7);
        c = c * 0.84 + float3(0.08, 0.075, 0.06);
        c *= float3(1.07, 1.0, 0.88);
        c.g += (1.0 - l) * 0.025;
    } else if (f == 5) {     // chrome: cool, crisp, low saturation
        c = lerp(float3(l, l, l), c, 0.55);
        c *= float3(0.92, 1.0, 1.1);
        c = (c - 0.5) * 1.28 + 0.5;
    } else if (f == 6) {     // vapor: pastel pink / teal, low contrast
        c = lerp(c, lerp(float3(0.78, 0.45, 0.88), float3(0.55, 1.0, 0.95), l), 0.38);
        c = c * 0.8 + 0.13;
    } else if (f == 7) {     // sepia
        c = float3(l * 1.07 + 0.05, l * 0.88 + 0.025, l * 0.66 + 0.005);
    } else if (f == 8) {     // tropic: vivid teal and green
        c = lerp(float3(l, l, l), c, 1.45);
        c *= float3(1.0, 1.05, 1.0);
        c = (c - 0.5) * 1.08 + 0.5;
    } else if (f == 9) {     // pixel: posterized (the blocks come from snapped coordinates)
        c = floor(saturate(c) * 7.0 + 0.5) / 7.0;
    }
    return c;
}

struct VSIn {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 color2 : COLOR1;
    float4 p : TEXCOORD1;
};
struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 color2 : COLOR1;
    float4 p : TEXCOORD1;
    float2 screen : TEXCOORD2;
};

VSOut vsUI(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.x * gUIScreen.z * 2.0 - 1.0, 1.0 - i.pos.y * gUIScreen.w * 2.0, 0, 1);
    o.uv = i.uv;
    o.color = i.color;
    o.color2 = i.color2;
    o.p = i.p;
    o.screen = i.pos;
    return o;
}

float sdRoundRect(float2 p, float2 halfSize, float r) {
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// SDF glyph/icon shading shared by fonts and icons: d = distance field sample (0.5 = edge)
void sdfShade(float d, float4 c, float4 c2, float pxRange, float outline, float soft, inout float3 rgb, inout float a) {
    float fill = saturate((d - 0.5) * pxRange + 0.5);
    if (outline > 0.0) {
        float pr = pxRange / (1.0 + soft);
        float ol = saturate((d - 0.5 + outline) * pr + 0.5);
        if (soft > 0.0) ol = ol * ol * (3.0 - 2.0 * ol);
        ol *= saturate(d * 14.0);   // never reach the saturated border of the distance field (quad edges)
        rgb = lerp(c2.rgb, c.rgb, fill);
        a = max(fill * c.a, ol * c2.a);
    } else {
        a = fill * c.a;
    }
}

float4 psUI(VSOut i) : SV_Target {
    int rawMode = (int)(i.p.x + 0.5);
    bool additive = rawMode >= 32;
    int mode = additive ? rawMode - 32 : rawMode;
    float4 c = i.color;
    float a = c.a;
    float3 rgb = c.rgb;
    // Texture fetches with implicit derivatives are done outside of flow control.
    float4 timg = tImage.Sample(sLinear, i.uv);
    float mapE = timg.a * 2.0 - 1.0;
    float mapFw = max(fwidth(mapE), 1e-4);
    if (mode == 1) {
        float d = tAtlas.SampleLevel(sLinear, i.uv, 0);
        sdfShade(d, c, i.color2, i.p.y, i.p.z, i.p.w, rgb, a);
    } else if (mode == 2) {
        rgb = timg.rgb * c.rgb;
        a = timg.a * c.a;
    } else if (mode == 3) {
        // p.w = radius (< 1000) + border * 10 * 1000; decode with a tolerance for interpolation noise
        float2 halfSize = i.p.yz;
        float hi = floor(i.p.w / 1000.0 + 0.005);
        float bw = hi / 10.0;
        float r = max(i.p.w - hi * 1000.0, 0.0);
        float sdf = sdRoundRect(i.uv, halfSize, r);
        float cov = saturate(0.5 - sdf);
        if (bw > 0.0) {
            float bmix = saturate(sdf + bw + 0.5);
            rgb = lerp(c.rgb, i.color2.rgb, bmix);
            a = lerp(c.a, i.color2.a, bmix) * cov;
        } else a = c.a * cov;
    } else if (mode == 4) {
        float r = i.p.y, th = i.p.z, fe = i.p.w;
        float d = length(i.uv) - r;
        if (th > 0.0) d = abs(d + th * 0.5) - th * 0.5;
        if (fe > 0.0) {
            float t = saturate(0.5 - d / fe);
            a = c.a * t * t * (3.0 - 2.0 * t);
        } else a = c.a * saturate(0.5 - d);
    } else if (mode == 5) {
        float L = i.p.y, r = i.p.z, fe = i.p.w;
        float dx = i.uv.x - clamp(i.uv.x, 0.0, L);
        float d = length(float2(dx, i.uv.y)) - r;
        if (fe > 0.0) {
            float t = saturate(0.5 - d / fe);
            a = c.a * t * t * (3.0 - 2.0 * t);
        } else a = c.a * saturate(0.5 - d);
    } else if (mode == 6) {
        float R = i.p.y, ht = i.p.z, h = i.p.w;
        float2 q = float2(abs(i.uv.x), i.uv.y);
        float rr = length(q);
        float dRing = abs(rr - R) - ht;
        float dEnd;
        if (h < 1.5707) {
            float gap = i.color2.a * 64.0;
            dEnd = dot(q, float2(cos(h), sin(h))) + gap * 0.5;
        } else {
            float ang = atan2(q.x, -q.y);
            dEnd = (ang - h) * rr;
        }
        float d = max(dRing, dEnd);
        a = c.a * saturate(0.5 - d);
    } else if (mode == 7) {
        float d = tIcons.SampleLevel(sLinear, i.uv, 0);
        sdfShade(d, c, i.color2, i.p.y, i.p.z, i.p.w, rgb, a);
    } else if (mode == 8) {
        // World map: RGB land color, A = signed sqrt-encoded water depth (> 0.5 water). Crisp analytic coastline.
        float water = saturate(mapE / mapFw + 0.5);
        float depth = mapE * abs(mapE) * 48.0;
        float3 shallow = float3(0.078, 0.330, 0.400);
        float3 midw = float3(0.040, 0.170, 0.290);
        float3 deep = float3(0.022, 0.066, 0.165);
        float3 wcol = lerp(shallow, midw, saturate(depth / 5.0));
        wcol = lerp(wcol, deep, saturate((depth - 5.0) / 22.0));
        wcol *= i.color2.rgb * 1.0;
        float3 land = timg.rgb * c.rgb;
        float3 col = lerp(land, wcol, water);
        float coast = saturate(1.2 - abs(mapE) / mapFw * 0.8) * i.p.y;
        col = lerp(col, float3(0.45, 0.80, 0.92) * i.color2.rgb, coast * 0.55);
        rgb = col;
        a = c.a;
    } else if (mode == 9) {
        float2 halfSize = i.p.yz;
        float hi = floor(i.p.w / 1000.0 + 0.005);
        float sat = hi / 100.0;
        float r = max(i.p.w - hi * 1000.0, 0.0);
        float sdf = sdRoundRect(i.uv, halfSize, r);
        float cov = saturate(0.5 - sdf);
        float3 b = tBlur.SampleLevel(sLinear, i.screen * gUIScreen.zw, 0).rgb;
        float l = dot(b, float3(0.299, 0.587, 0.114));
        b = lerp(float3(l, l, l), b, sat) * c.rgb;
        rgb = lerp(b, i.color2.rgb, i.color2.a);
        a = c.a * cov;
    } else if (mode == 10) {
        float d = min(i.p.y, min(i.p.z, i.p.w));
        a = c.a * saturate(d + 0.5);
    } else if (mode == 11) {
        // Photo grading: uv = source screen uv, p.yz = position inside the drawn rect (0..1), p.w = rect aspect
        float2 uv = i.uv;
        int filter = (int)(gPhA.x + 0.5);
        if (filter == 9 && gPhA.y > 0.01) {
            float rows = lerp(540.0, 90.0, gPhA.y);
            float2 grid = float2(rows * gPhD.y, rows);
            uv = (floor(uv * grid) + 0.5) / grid;
        }
        float3 sharp = tScene.SampleLevel(sLinear, uv, 0).rgb;
        float3 col = sharp;
        if (gPhC.x > 0.5) {
            float coc;
            if (gPhC.w > 0.0) {
                uint dw, dh;
                tDepth.GetDimensions(dw, dh);
                int2 dp = int2(saturate(uv) * float2(dw - 1, dh - 1) + 0.5);
                float d = tDepth.Load(int3(dp, 0));
                float z = d > 0.0 ? gPhC.w / d : 1e6;
                coc = saturate(gPhC.z * abs(z - gPhC.y) / max(z, 0.05));
            } else {
                coc = saturate(gPhC.z * max(abs(uv.y - 0.52) - 0.08, 0.0) * 2.4);
            }
            float3 halfc = tHalf.SampleLevel(sLinear, uv, 0).rgb;
            float3 blur = tBlur.SampleLevel(sLinear, uv, 0).rgb;
            float t2 = coc * 2.0;
            col = t2 < 1.0 ? lerp(sharp, halfc, t2) : lerp(halfc, blur, t2 - 1.0);
        }
        col *= exp2(gPhA.z);
        float temp = gPhB.y;
        col *= float3(1.0 + 0.12 * temp, 1.0 + 0.015 * temp, 1.0 - 0.14 * temp);
        col = lerp(col, photoFilter(filter, col), gPhA.y);
        col = (col - 0.5) * gPhA.w + 0.5;
        float lum = photoLuma(col);
        col = lerp(float3(lum, lum, lum), col, gPhB.x);
        float2 q = i.p.yz * 2.0 - 1.0;
        q.x *= sqrt(max(i.p.w, 0.1));
        q.y /= sqrt(max(i.p.w, 0.1));
        float vig = smoothstep(1.55, 0.35, length(q));
        col *= lerp(1.0, vig, gPhB.z);
        float n = frac(sin(dot(floor(i.screen) + frac(gPhD.x * 7.13) * 97.0, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
        col += n * gPhB.w * 0.18;
        rgb = saturate(col);
        a = 1.0;
    }
    int clipMode = (int)(gUIClipMode.x + 0.5);
    if (clipMode == 1) {
        float dist = length(i.screen - gUIClip.xy);
        a *= saturate(gUIClip.z - dist + 0.5);
    } else if (clipMode == 2) {
        float2 lo = gUIClip.xy, hi = gUIClip.xy + gUIClip.zw;
        if (any(i.screen < lo) || any(i.screen > hi)) a = 0;
    } else if (clipMode == 3) {
        float2 hs = gUIClip.zw * 0.5;
        float sd = sdRoundRect(i.screen - (gUIClip.xy + hs), hs, gUIClipMode.y);
        a *= saturate(0.5 - sd);
    }
    return float4(rgb * a, additive ? 0.0 : a);
}

// ------------------------------------------------------------------------------------------------------------------
// Backdrop blur chain (fullscreen passes)
cbuffer UIBlurCB : register(b2) {
    float4 gBlurTexel;  // xy source texel size, zw direction in texels (0 = downsample)
};
Texture2D<float4> tBlurSrc : register(t0);

struct VSFullOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
VSFullOut vsUIFull(uint id : SV_VertexID) {
    VSFullOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}

// 2x downsample: 4 bilinear taps = 16 texel box with slight tent weighting
float4 psUIDown(VSFullOut i) : SV_Target {
    float2 t = gBlurTexel.xy;
    float3 s = tBlurSrc.SampleLevel(sLinear, i.uv + t * float2(-0.5, -0.5), 0).rgb;
    s += tBlurSrc.SampleLevel(sLinear, i.uv + t * float2(0.5, -0.5), 0).rgb;
    s += tBlurSrc.SampleLevel(sLinear, i.uv + t * float2(-0.5, 0.5), 0).rgb;
    s += tBlurSrc.SampleLevel(sLinear, i.uv + t * float2(0.5, 0.5), 0).rgb;
    return float4(s * 0.25, 1);
}

// 9-tap gaussian via 5 bilinear fetches along gBlurTexel.zw
float4 psUIBlur(VSFullOut i) : SV_Target {
    float2 d = gBlurTexel.xy * gBlurTexel.zw;
    float3 s = tBlurSrc.SampleLevel(sLinear, i.uv, 0).rgb * 0.2270270270;
    s += tBlurSrc.SampleLevel(sLinear, i.uv + d * 1.3846153846, 0).rgb * 0.3162162162;
    s += tBlurSrc.SampleLevel(sLinear, i.uv - d * 1.3846153846, 0).rgb * 0.3162162162;
    s += tBlurSrc.SampleLevel(sLinear, i.uv + d * 3.2307692308, 0).rgb * 0.0702702703;
    s += tBlurSrc.SampleLevel(sLinear, i.uv - d * 3.2307692308, 0).rgb * 0.0702702703;
    return float4(s, 1);
}
