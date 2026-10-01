// 2D UI: solid, SDF text, images, rounded rects, circles, capsules, arcs, SDF icons, map, frosted backdrop, AA triangles.
// Premultiplied alpha output (mode + 32 = additive: alpha written as 0).
cbuffer UICB : register(b1) {
    float4 gUIScreen;   // w, h, 1/w, 1/h
    float4 gUIClip;
    float4 gUIClipMode; // x mode (0 none, 1 circle, 2 rect, 3 rounded rect), y rounded-rect radius
};
Texture2D<float> tAtlas : register(t0);
Texture2D<float4> tImage : register(t1);
Texture2D<float> tIcons : register(t2);
Texture2D<float4> tBlur : register(t3);
SamplerState sLinear : register(s1);

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
        float2 halfSize = i.p.yz;
        float bw = floor(i.p.w / 1000.0) / 10.0;
        float r = i.p.w - floor(i.p.w / 1000.0) * 1000.0;
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
        float sat = floor(i.p.w / 1000.0) / 100.0;
        float r = i.p.w - floor(i.p.w / 1000.0) * 1000.0;
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
