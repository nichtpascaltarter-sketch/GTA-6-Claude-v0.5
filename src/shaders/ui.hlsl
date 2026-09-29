// 2D UI: solid, SDF text, images, rounded rects, circles. Premultiplied alpha output.
cbuffer UICB : register(b1) {
    float4 gUIScreen;   // w, h, 1/w, 1/h
    float4 gUIClip;
    float4 gUIClipMode;
};
Texture2D<float> tAtlas : register(t0);
Texture2D<float4> tImage : register(t1);
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

float4 psUI(VSOut i) : SV_Target {
    int mode = (int)(i.p.x + 0.5);
    float4 c = i.color;
    float a = c.a;
    float3 rgb = c.rgb;
    if (mode == 1) {
        float d = tAtlas.Sample(sLinear, i.uv);
        float pxRange = i.p.y;
        float fill = saturate((d - 0.5) * pxRange + 0.5);
        if (i.p.z > 0.0) {
            float ol = saturate((d - 0.5 + i.p.z) * pxRange + 0.5);
            rgb = lerp(i.color2.rgb, c.rgb, fill);
            a = max(fill * c.a, ol * i.color2.a);
        } else {
            a = fill * c.a;
        }
    } else if (mode == 2) {
        float4 t = tImage.Sample(sLinear, i.uv);
        rgb = t.rgb * c.rgb;
        a = t.a * c.a;
    } else if (mode == 3) {
        float2 halfSize = i.p.yz;
        float bw = floor(i.p.w / 1000.0) / 10.0;
        float r = i.p.w - floor(i.p.w / 1000.0) * 1000.0;
        float2 q = abs(i.uv) - halfSize + r;
        float sdf = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        float cov = saturate(0.5 - sdf);
        if (bw > 0.0) {
            float bmix = saturate(sdf + bw + 0.5);
            rgb = lerp(c.rgb, i.color2.rgb, bmix);
            a = lerp(c.a, i.color2.a, bmix) * cov;
        } else a = c.a * cov;
    } else if (mode == 4) {
        float r = i.p.y, th = i.p.z;
        float d = length(i.uv) - r;
        if (th > 0.0) d = abs(d + th * 0.5) - th * 0.5;
        a = c.a * saturate(0.5 - d);
    }
    int clipMode = (int)(gUIClipMode.x + 0.5);
    if (clipMode == 1) {
        float dist = length(i.screen - gUIClip.xy);
        a *= saturate(gUIClip.z - dist + 0.5);
    } else if (clipMode == 2) {
        float2 lo = gUIClip.xy, hi = gUIClip.xy + gUIClip.zw;
        if (any(i.screen < lo) || any(i.screen > hi)) a = 0;
    }
    return float4(rgb * a, a);
}
