// Procedural SDF icon atlas: blip icons, HUD glyphs and weapon silhouettes are described as polygon shapes
// (union / subtract operations), rasterized at 4x, converted to signed distance fields and packed into one R8 texture.
#include "ui_internal.h"

namespace UI {
namespace icons_detail {

using namespace uix;

typedef std::vector<vec2> Poly;

struct Op {
    std::vector<Poly> contours;   // even-odd fill of all contours
    bool subtract = false;
};

struct Shape {
    std::vector<Op> ops;
    Shape& add(const Poly& p) { Op o; o.contours.push_back(p); ops.push_back(o); return *this; }
    // Lists of polygons (strokes) are combined one by one so overlapping pieces union instead of cancelling out.
    Shape& add(const std::vector<Poly>& ps) { for (const Poly& p : ps) add(p); return *this; }
    Shape& sub(const Poly& p) { Op o; o.contours.push_back(p); o.subtract = true; ops.push_back(o); return *this; }
    Shape& sub(const std::vector<Poly>& ps) { for (const Poly& p : ps) sub(p); return *this; }
};

// ---- shape helpers (design units: icons 0..100 square, weapons 0..400 x 0..150, y down)
Poly circleP(vec2 c, float r, int n = 72) {
    Poly p;
    for (int i = 0; i < n; i++) {
        float a = kTwoPi * i / n;
        p.push_back(c + vec2(cosf(a), sinf(a)) * r);
    }
    return p;
}
Poly ellipseP(vec2 c, float rx, float ry, float rot = 0.f, int n = 72) {
    Poly p;
    float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < n; i++) {
        float a = kTwoPi * i / n;
        vec2 q(cosf(a) * rx, sinf(a) * ry);
        p.push_back(c + vec2(cr * q.x - sr * q.y, sr * q.x + cr * q.y));
    }
    return p;
}
Poly rectP(float x0, float y0, float x1, float y1) { return {vec2(x0, y0), vec2(x1, y0), vec2(x1, y1), vec2(x0, y1)}; }
Poly rrectP(float x0, float y0, float x1, float y1, float r, int seg = 10) {
    r = Min(r, Min(x1 - x0, y1 - y0) * 0.5f);
    Poly p;
    vec2 cs[4] = {vec2(x1 - r, y0 + r), vec2(x1 - r, y1 - r), vec2(x0 + r, y1 - r), vec2(x0 + r, y0 + r)};
    float start[4] = {-kHalfPi, 0.f, kHalfPi, kPi};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= seg; i++) {
            float a = start[k] + kHalfPi * i / seg;
            p.push_back(cs[k] + vec2(cosf(a), sinf(a)) * r);
        }
    return p;
}
Poly capsuleP(vec2 a, vec2 b, float r, int seg = 16) {
    vec2 d = normalize(b - a), n = perp(d);
    Poly p;
    for (int i = 0; i <= seg; i++) {  // around b: +n -> +d -> -n
        float t = kPi * i / seg;
        p.push_back(b + (n * cosf(t) + d * sinf(t)) * r);
    }
    for (int i = 0; i <= seg; i++) {  // around a: -n -> -d -> +n
        float t = kPi * i / seg;
        p.push_back(a - (n * cosf(t) + d * sinf(t)) * r);
    }
    return p;
}
std::vector<Poly> strokeP(const std::vector<vec2>& pts, float w) {
    std::vector<Poly> out;
    for (size_t i = 0; i + 1 < pts.size(); i++) out.push_back(capsuleP(pts[i], pts[i + 1], w * 0.5f));
    return out;
}
Poly starP(vec2 c, float R, float r, int n = 5, float rot = 0.f) {
    Poly p;
    for (int i = 0; i < n * 2; i++) {
        float a = rot - kHalfPi + kPi * i / n;
        float rr = (i & 1) ? r : R;
        p.push_back(c + vec2(cosf(a), sinf(a)) * rr);
    }
    return p;
}
Poly arcBandP(vec2 c, float r, float w, float a0, float a1, int n = 48) {
    // angles in radians, 0 = +x, y down (screen)
    Poly p;
    float ro = r + w * 0.5f, ri = r - w * 0.5f;
    for (int i = 0; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        p.push_back(c + vec2(cosf(a), sinf(a)) * ro);
    }
    for (int i = n; i >= 0; i--) {
        float a = a0 + (a1 - a0) * i / n;
        p.push_back(c + vec2(cosf(a), sinf(a)) * ri);
    }
    return p;
}
Poly xform(const Poly& p, vec2 origin, float rot, float scale, vec2 offset) {
    Poly o;
    float c = cosf(rot), s = sinf(rot);
    for (vec2 q : p) {
        vec2 d = (q - origin) * scale;
        o.push_back(origin + offset + vec2(c * d.x - s * d.y, s * d.x + c * d.y));
    }
    return o;
}
Shape xformShape(const Shape& sh, vec2 origin, float rot, float scale, vec2 offset) {
    Shape o;
    for (const Op& op : sh.ops) {
        Op n;
        n.subtract = op.subtract;
        for (const Poly& p : op.contours) n.contours.push_back(xform(p, origin, rot, scale, offset));
        o.ops.push_back(n);
    }
    return o;
}
// Smooth closed curve through control points (Catmull-Rom)
Poly smoothClosed(const std::vector<vec2>& c, int sub = 8) {
    Poly p;
    int n = (int)c.size();
    for (int i = 0; i < n; i++) {
        vec2 p0 = c[(i + n - 1) % n], p1 = c[i], p2 = c[(i + 1) % n], p3 = c[(i + 2) % n];
        for (int k = 0; k < sub; k++) {
            float t = (float)k / sub, t2 = t * t, t3 = t2 * t;
            p.push_back((p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f);
        }
    }
    return p;
}

// ---- rasterizer: binary coverage at pixel centers (even-odd per op), union/subtract across ops
void rasterOp(std::vector<u8>& mask, int W, int H, const Op& op, float scale, vec2 off) {
    struct E { float x0, y0, x1, y1; };
    std::vector<E> edges;
    for (const Poly& p : op.contours) {
        size_t n = p.size();
        for (size_t i = 0; i < n; i++) {
            vec2 a = p[i] * scale + off, b = p[(i + 1) % n] * scale + off;
            if (a.y == b.y) continue;
            edges.push_back({a.x, a.y, b.x, b.y});
        }
    }
    std::vector<float> xs;
    for (int y = 0; y < H; y++) {
        float sy = y + 0.5f;
        xs.clear();
        for (const E& e : edges) {
            float ya = Min(e.y0, e.y1), yb = Max(e.y0, e.y1);
            if (sy < ya || sy >= yb) continue;
            float t = (sy - e.y0) / (e.y1 - e.y0);
            xs.push_back(e.x0 + (e.x1 - e.x0) * t);
        }
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int xa = Max(0, (int)ceilf(xs[k] - 0.5f)), xb = Min(W - 1, (int)floorf(xs[k + 1] - 0.5f));
            for (int x = xa; x <= xb; x++) mask[(size_t)y * W + x] = op.subtract ? 0 : 1;
        }
    }
}

// Signed distance field from a binary mask, downsampled by `down`; spread in raster pixels. Output 0..255 (128 = edge).
void sdfFromMask(const std::vector<u8>& mask, int W, int H, int down, float spread, u8* out, int outPitch) {
    std::vector<float> inside((size_t)W * H), outside((size_t)W * H);
    for (size_t i = 0; i < mask.size(); i++) {
        inside[i] = mask[i] ? 0.f : 1e20f;
        outside[i] = mask[i] ? 1e20f : 0.f;
    }
    draw2d_detail::edt(inside, W, H);
    draw2d_detail::edt(outside, W, H);
    int ow = W / down, oh = H / down;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            float acc = 0;
            for (int sy = 0; sy < down; sy++)
                for (int sx = 0; sx < down; sx++) {
                    size_t i = (size_t)(y * down + sy) * W + (x * down + sx);
                    float dOut = sqrtf(inside[i]), dIn = sqrtf(outside[i]);
                    acc += dOut > 0 ? dOut - 0.5f : -(dIn - 0.5f);
                }
            acc /= (float)(down * down);
            float v = 0.5f - acc / (2.f * spread);
            out[(size_t)y * outPitch + x] = (u8)(Saturate(v) * 255.f + 0.5f);
        }
}

// ---- atlas layout
const int kAtlasW = 1024, kAtlasH = 1024;
const int kCell = 64;                // icon cell (atlas px)
const int kIconsPerRow = kAtlasW / kCell;
const int kWpnW = 256, kWpnH = 96;   // weapon cell
const int kWpnY0 = 384;              // weapons start row
const int kDown = 4;                 // raster supersampling
const float kSpreadRaster = 32.f;    // SDF spread (raster px) = 8 atlas px

gfx::Texture g_iconTex;
bool g_iconsReady = false;

void iconCellUV(int id, float& u0, float& v0, float& u1, float& v1) {
    int cx = (id % kIconsPerRow) * kCell, cy = (id / kIconsPerRow) * kCell;
    u0 = (float)cx / kAtlasW;
    v0 = (float)cy / kAtlasH;
    u1 = (float)(cx + kCell) / kAtlasW;
    v1 = (float)(cy + kCell) / kAtlasH;
}
void weaponCellUV(int w, float& u0, float& v0, float& u1, float& v1) {
    int cx = (w % 4) * kWpnW, cy = kWpnY0 + (w / 4) * kWpnH;
    u0 = (float)cx / kAtlasW;
    v0 = (float)cy / kAtlasH;
    u1 = (float)(cx + kWpnW) / kAtlasW;
    v1 = (float)(cy + kWpnH) / kAtlasH;
}

// ------------------------------------------------------------------------------------------------------------------
// Icon designs (100x100 units, content roughly within [12, 88])
Shape carSide() {
    Shape s;
    Poly body = smoothClosed({vec2(8, 62), vec2(10, 52), vec2(26, 47), vec2(38, 33), vec2(62, 32), vec2(76, 45), vec2(90, 49),
                              vec2(93, 60), vec2(88, 66), vec2(12, 66)}, 6);
    s.add(body);
    s.sub(Poly{vec2(41, 37), vec2(50, 37), vec2(50, 46), vec2(33, 46)});
    s.sub(Poly{vec2(54, 37), vec2(61, 37), vec2(70, 46), vec2(54, 46)});
    s.sub(circleP(vec2(27, 66), 13.5f));
    s.sub(circleP(vec2(73, 66), 13.5f));
    s.add(circleP(vec2(27, 66), 10.f));
    s.add(circleP(vec2(73, 66), 10.f));
    s.sub(circleP(vec2(27, 66), 3.5f));
    s.sub(circleP(vec2(73, 66), 3.5f));
    return s;
}
Shape heliSide() {
    Shape s;
    s.add(smoothClosed({vec2(18, 58), vec2(24, 44), vec2(42, 38), vec2(58, 42), vec2(64, 52), vec2(58, 64), vec2(34, 68), vec2(20, 66)}, 6));
    s.add(Poly{vec2(58, 46), vec2(92, 44), vec2(92, 50), vec2(60, 58)});
    s.add(Poly{vec2(84, 32), vec2(92, 32), vec2(94, 50), vec2(86, 50)});
    s.add(capsuleP(vec2(8, 28), vec2(76, 28), 2.8f));
    s.add(capsuleP(vec2(41, 28), vec2(41, 40), 3.5f));
    s.add(capsuleP(vec2(20, 80), vec2(62, 80), 2.6f));
    s.add(capsuleP(vec2(30, 66), vec2(28, 80), 2.4f));
    s.add(capsuleP(vec2(52, 64), vec2(54, 80), 2.4f));
    s.sub(smoothClosed({vec2(24, 52), vec2(28, 45), vec2(38, 42), vec2(38, 54)}, 5));
    return s;
}
Shape planeTop() {
    Shape s;
    s.add(capsuleP(vec2(50, 10), vec2(50, 88), 6.f));
    s.add(Poly{vec2(50, 36), vec2(92, 56), vec2(92, 63), vec2(50, 55), vec2(8, 63), vec2(8, 56)});
    s.add(Poly{vec2(50, 76), vec2(68, 86), vec2(68, 91), vec2(50, 87), vec2(32, 91), vec2(32, 86)});
    return s;
}
Shape pistolIcon() {
    Shape s;
    s.add(rrectP(18, 32, 86, 46, 3));
    s.add(Poly{vec2(24, 44), vec2(44, 44), vec2(38, 80), vec2(20, 80), vec2(18, 74)});
    s.add(strokeP({vec2(44, 46), vec2(46, 58), vec2(58, 60), vec2(60, 46)}, 4.f));
    return s;
}

Shape makeIcon(int id) {
    Shape s;
    const vec2 C0(50, 50);
    switch (id) {
    case BLIP_DOT: s.add(circleP(C0, 30)); break;
    case BLIP_PLAYER: s.add(Poly{vec2(50, 10), vec2(84, 86), vec2(50, 69), vec2(16, 86)}); break;
    case BLIP_WAYPOINT: {
        Poly pin;
        for (int i = 0; i <= 40; i++) {
            float a = kPi * 0.82f + (kTwoPi - kPi * 0.64f) * i / 40.f;
            pin.push_back(vec2(50, 38) + vec2(cosf(a), sinf(a)) * 27.f);
        }
        pin.push_back(vec2(50, 90));
        s.add(pin);
        s.sub(circleP(vec2(50, 38), 10.f));
        break;
    }
    case BLIP_MISSION: s.add(circleP(C0, 34)); break;
    case BLIP_OBJECTIVE: s.add(circleP(C0, 30)); break;
    case BLIP_ENEMY: s.add(circleP(C0, 28)); break;
    case BLIP_FRIEND: s.add(circleP(C0, 28)); break;
    case BLIP_POLICE: s.add(circleP(C0, 28)); break;
    case BLIP_POLICE_HELI: s = heliSide(); s.sub(starP(vec2(44, 54), 7.5f, 3.2f)); break;
    case BLIP_VEHICLE: s = carSide(); break;
    case BLIP_SAFEHOUSE:
        s.add(Poly{vec2(10, 50), vec2(50, 14), vec2(90, 50), vec2(82, 56), vec2(50, 28), vec2(18, 56)});
        s.add(rectP(68, 20, 78, 40));
        s.add(Poly{vec2(24, 50), vec2(50, 27), vec2(76, 50), vec2(76, 86), vec2(24, 86)});
        s.sub(rrectP(43, 62, 57, 87, 2));
        break;
    case BLIP_GUN_SHOP: s = xformShape(pistolIcon(), C0, 0.f, 1.05f, vec2(0, 0)); break;
    case BLIP_CLOTHES_SHOP:
        s.add(Poly{vec2(32, 16), vec2(42, 14), vec2(50, 22), vec2(58, 14), vec2(68, 16), vec2(90, 36), vec2(78, 50), vec2(70, 43),
                   vec2(70, 86), vec2(30, 86), vec2(30, 43), vec2(22, 50), vec2(10, 36)});
        s.sub(circleP(vec2(50, 13), 8.f));
        break;
    case BLIP_CAR_SHOP:
        s.add(rrectP(12, 44, 88, 72, 8));
        s.add(Poly{vec2(26, 24), vec2(74, 24), vec2(84, 46), vec2(16, 46)});
        s.sub(Poly{vec2(30, 29), vec2(70, 29), vec2(77, 43), vec2(23, 43)});
        s.sub(circleP(vec2(24, 56), 6.f));
        s.sub(circleP(vec2(76, 56), 6.f));
        s.sub(rrectP(38, 53, 62, 62, 2));
        s.add(rrectP(16, 70, 32, 86, 3));
        s.add(rrectP(68, 70, 84, 86, 3));
        break;
    case BLIP_GARAGE: {
        Shape w;
        w.add(capsuleP(vec2(24, 76), vec2(62, 38), 7.f));
        w.add(circleP(vec2(68, 32), 19.f));
        w.sub(Poly{vec2(64, 10), vec2(92, 38), vec2(80, 50), vec2(58, 28)});
        w.add(circleP(vec2(22, 78), 10.f));
        w.sub(circleP(vec2(22, 78), 4.f));
        s = w;
        break;
    }
    case BLIP_HOSPITAL:
        s.add(rrectP(37, 12, 63, 88, 5));
        s.add(rrectP(12, 37, 88, 63, 5));
        break;
    case BLIP_POLICE_STATION:
        s.add(smoothClosed({vec2(50, 10), vec2(70, 17), vec2(86, 20), vec2(84, 50), vec2(70, 74), vec2(50, 90), vec2(30, 74), vec2(16, 50), vec2(14, 20), vec2(30, 17)}, 5));
        s.sub(starP(vec2(50, 49), 21.f, 9.f));
        break;
    case BLIP_RACE: {
        s.add(capsuleP(vec2(20, 12), vec2(20, 90), 3.5f));
        Poly flag = {vec2(24, 14), vec2(86, 14), vec2(86, 58), vec2(24, 58)};
        s.add(flag);
        for (int gy = 0; gy < 4; gy++)
            for (int gx = 0; gx < 5; gx++)
                if ((gx + gy) & 1) s.sub(rectP(24 + gx * 12.4f, 14 + gy * 11.f, 24 + (gx + 1) * 12.4f, 14 + (gy + 1) * 11.f));
        break;
    }
    case BLIP_TAXI_JOB:
        s = carSide();
        s.add(rrectP(42, 20, 60, 30, 2));
        break;
    case BLIP_DELIVERY_JOB:
        s.add(Poly{vec2(50, 12), vec2(86, 30), vec2(86, 70), vec2(50, 88), vec2(14, 70), vec2(14, 30)});
        s.sub(strokeP({vec2(14, 30), vec2(50, 48), vec2(86, 30)}, 4.5f));
        s.sub(strokeP({vec2(50, 48), vec2(50, 90)}, 4.5f));
        s.sub(strokeP({vec2(32, 21), vec2(68, 39), vec2(68, 52)}, 5.f));
        break;
    case BLIP_VIGILANTE:
        s.add(arcBandP(C0, 30, 8, 0, kTwoPi, 72));
        for (int k = 0; k < 4; k++) {
            float a = kHalfPi * k;
            vec2 d(cosf(a), sinf(a));
            s.add(capsuleP(C0 + d * 16.f, C0 + d * 44.f, 4.f));
        }
        s.add(circleP(C0, 5.f));
        break;
    case BLIP_STUNT_JUMP:
        s.add(Poly{vec2(10, 86), vec2(62, 86), vec2(62, 62)});
        s.add(arcBandP(vec2(60, 88), 44, 7, -kPi * 0.93f, -kPi * 0.42f, 32));
        s.add(Poly{vec2(84, 44), vec2(78, 66), vec2(66, 48)});
        break;
    case BLIP_COLLECTIBLE:
        s.add(Poly{vec2(28, 18), vec2(72, 18), vec2(90, 40), vec2(50, 88), vec2(10, 40)});
        s.sub(strokeP({vec2(10, 40), vec2(90, 40)}, 3.5f));
        s.sub(strokeP({vec2(38, 40), vec2(50, 88)}, 3.f));
        s.sub(strokeP({vec2(62, 40), vec2(50, 88)}, 3.f));
        s.sub(strokeP({vec2(28, 18), vec2(38, 40), vec2(50, 18), vec2(62, 40), vec2(72, 18)}, 3.f));
        break;
    case BLIP_BOAT:
        s.add(Poly{vec2(8, 54), vec2(74, 54), vec2(94, 50), vec2(80, 72), vec2(22, 72), vec2(10, 64)});
        s.add(Poly{vec2(38, 54), vec2(48, 36), vec2(62, 36), vec2(70, 54)});
        s.sub(Poly{vec2(50, 40), vec2(60, 40), vec2(64, 50), vec2(46, 50)});
        s.add(strokeP({vec2(14, 82), vec2(30, 78), vec2(48, 82), vec2(66, 78), vec2(86, 82)}, 3.5f));
        break;
    case BLIP_HELI: s = heliSide(); break;
    case BLIP_PLANE: s = planeTop(); break;
    case BLIP_BAR:
        s.add(Poly{vec2(14, 16), vec2(86, 16), vec2(52, 54), vec2(48, 54)});
        s.sub(circleP(vec2(60, 28), 6.5f));
        s.add(capsuleP(vec2(50, 50), vec2(50, 80), 3.f));
        s.add(ellipseP(vec2(50, 83), 20, 4.5f));
        s.add(capsuleP(vec2(60, 28), vec2(76, 8), 1.8f));
        break;
    case BLIP_CONVENIENCE_STORE:
        s.add(Poly{vec2(18, 36), vec2(82, 36), vec2(88, 88), vec2(12, 88)});
        s.add(arcBandP(vec2(50, 38), 16, 6.f, kPi, kTwoPi, 32));
        s.sub(circleP(vec2(34, 48), 3.5f));
        s.sub(circleP(vec2(66, 48), 3.5f));
        break;
    case BLIP_BANK:
        s.add(Poly{vec2(10, 34), vec2(50, 12), vec2(90, 34)});
        s.add(rectP(12, 34, 88, 41));
        for (int k = 0; k < 4; k++) s.add(rectP(18 + k * 18.5f, 44, 28 + k * 18.5f, 74));
        s.add(rectP(14, 76, 86, 80));
        s.add(rectP(8, 82, 92, 88));
        s.sub(circleP(vec2(50, 27), 4.f));
        break;
    case BLIP_AIRPORT: {
        s.add(arcBandP(C0, 38, 6.f, 0, kTwoPi, 72));
        Shape p = xformShape(planeTop(), C0, kPi * 0.25f, 0.62f, vec2(0, 0));
        for (auto& op : p.ops) s.ops.push_back(op);
        break;
    }
    case BLIP_HIDEOUT:
        s.add(circleP(vec2(50, 42), 31));
        s.add(rrectP(33, 52, 67, 84, 6));
        s.sub(ellipseP(vec2(38, 44), 9.5f, 10.5f));
        s.sub(ellipseP(vec2(62, 44), 9.5f, 10.5f));
        s.sub(Poly{vec2(50, 55), vec2(45, 65), vec2(55, 65)});
        s.sub(rectP(41.5f, 72, 44.5f, 86));
        s.sub(rectP(48.5f, 72, 51.5f, 86));
        s.sub(rectP(55.5f, 72, 58.5f, 86));
        break;
    // ---------------------------------------------------------------- misc glyphs
    case ICO_STAR: s.add(starP(vec2(50, 53), 42.f, 17.5f)); break;
    case ICO_STAR_OUTLINE:
        s.add(starP(vec2(50, 53), 42.f, 17.5f));
        s.sub(starP(vec2(50, 53.5f), 30.f, 12.5f));
        break;
    case ICO_HEART: {
        Poly h;
        for (int i = 0; i < 96; i++) {
            float t = kTwoPi * i / 96.f;
            float x = 16.f * powf(sinf(t), 3.f);
            float y = 13.f * cosf(t) - 5.f * cosf(2 * t) - 2.f * cosf(3 * t) - cosf(4 * t);
            h.push_back(vec2(50 + x * 2.35f, 47 - y * 2.35f));
        }
        s.add(h);
        break;
    }
    case ICO_SHIELD:
        s.add(smoothClosed({vec2(50, 10), vec2(68, 18), vec2(86, 20), vec2(84, 50), vec2(70, 74), vec2(50, 90), vec2(30, 74), vec2(16, 50), vec2(14, 20), vec2(32, 18)}, 5));
        break;
    case ICO_BUBBLES:
        s.add(arcBandP(vec2(38, 60), 20, 7, 0, kTwoPi, 60));
        s.add(arcBandP(vec2(68, 32), 12, 5.5f, 0, kTwoPi, 48));
        s.add(circleP(vec2(72, 72), 7.f));
        break;
    case ICO_BOLT: s.add(Poly{vec2(60, 8), vec2(20, 56), vec2(46, 56), vec2(36, 92), vec2(80, 40), vec2(54, 40), vec2(68, 8)}); break;
    case ICO_MESSAGE:
        s.add(rrectP(10, 16, 90, 70, 16));
        s.add(Poly{vec2(24, 64), vec2(18, 90), vec2(46, 68)});
        s.sub(circleP(vec2(32, 43), 5.5f));
        s.sub(circleP(vec2(50, 43), 5.5f));
        s.sub(circleP(vec2(68, 43), 5.5f));
        break;
    case ICO_PHONE:
        s.add(rrectP(28, 8, 72, 92, 10));
        s.sub(rrectP(33, 18, 67, 78, 2));
        s.sub(circleP(vec2(50, 85), 3.5f));
        break;
    case ICO_MUSIC:
        s.add(capsuleP(vec2(64, 16), vec2(64, 70), 3.5f));
        s.add(ellipseP(vec2(53, 72), 14, 10, -0.35f));
        s.add(Poly{vec2(62, 14), vec2(86, 26), vec2(86, 42), vec2(66, 32)});
        break;
    case ICO_CLOCK:
        s.add(arcBandP(C0, 36, 8, 0, kTwoPi, 72));
        s.add(capsuleP(C0, vec2(50, 26), 4.f));
        s.add(capsuleP(C0, vec2(66, 58), 4.f));
        break;
    case ICO_CHECK: s.add(strokeP({vec2(16, 52), vec2(40, 76), vec2(86, 26)}, 14.f)); break;
    case ICO_CROSS:
        s.add(capsuleP(vec2(22, 22), vec2(78, 78), 7.f));
        s.add(capsuleP(vec2(78, 22), vec2(22, 78), 7.f));
        break;
    case ICO_GEAR: {
        s.add(circleP(C0, 29));
        for (int k = 0; k < 8; k++) {
            float a = kTwoPi * k / 8.f;
            Poly t = rrectP(43, 8, 57, 30, 3);
            s.add(xform(t, C0, a, 1.f, vec2(0, 0)));
        }
        s.sub(circleP(C0, 12));
        break;
    }
    case ICO_SAVE:
        s.add(Poly{vec2(12, 12), vec2(74, 12), vec2(88, 26), vec2(88, 88), vec2(12, 88)});
        s.sub(rrectP(24, 52, 76, 82, 3));
        s.sub(rectP(26, 12, 64, 34));
        s.add(rectP(52, 16, 60, 30));
        break;
    case ICO_MAP:
        s.add(Poly{vec2(10, 24), vec2(35, 14), vec2(35, 76), vec2(10, 86)});
        s.add(Poly{vec2(38, 14), vec2(62, 24), vec2(62, 86), vec2(38, 76)});
        s.add(Poly{vec2(65, 24), vec2(90, 14), vec2(90, 76), vec2(65, 86)});
        break;
    case ICO_STATS:
        s.add(rrectP(12, 58, 28, 88, 2));
        s.add(rrectP(34, 36, 50, 88, 2));
        s.add(rrectP(56, 48, 72, 88, 2));
        s.add(rrectP(78, 14, 94, 88, 2));
        break;
    case ICO_BRIEF:
        s.add(Poly{vec2(20, 8), vec2(62, 8), vec2(80, 26), vec2(80, 92), vec2(20, 92)});
        s.sub(Poly{vec2(60, 8), vec2(80, 28), vec2(60, 28)});
        s.add(Poly{vec2(64, 12), vec2(76, 24), vec2(64, 24)});
        for (int k = 0; k < 4; k++) s.sub(rectP(30, 40 + k * 12.f, k == 3 ? 56.f : 70.f, 45 + k * 12.f));
        break;
    case ICO_POWER:
        s.add(arcBandP(vec2(50, 54), 30, 9, -kHalfPi + 0.75f, -kHalfPi + kTwoPi - 0.75f, 60));
        s.add(capsuleP(vec2(50, 12), vec2(50, 48), 4.5f));
        break;
    case ICO_MONITOR:
        s.add(rrectP(10, 16, 90, 70, 6));
        s.sub(rrectP(17, 23, 83, 63, 2));
        s.add(rectP(44, 70, 56, 80));
        s.add(rrectP(28, 80, 72, 87, 3));
        break;
    case ICO_SPEAKER:
        s.add(Poly{vec2(10, 38), vec2(28, 38), vec2(52, 16), vec2(52, 84), vec2(28, 62), vec2(10, 62)});
        s.add(arcBandP(vec2(54, 50), 16, 6, -kPi * 0.28f, kPi * 0.28f, 24));
        s.add(arcBandP(vec2(54, 50), 30, 6, -kPi * 0.3f, kPi * 0.3f, 32));
        break;
    case ICO_GAMEPAD:
        s.add(smoothClosed({vec2(22, 30), vec2(50, 34), vec2(78, 30), vec2(92, 56), vec2(86, 76), vec2(72, 72), vec2(62, 60),
                            vec2(38, 60), vec2(28, 72), vec2(14, 76), vec2(8, 56)}, 6));
        s.sub(rectP(22, 42, 26, 56));
        s.sub(rectP(17, 47, 31, 51));
        s.sub(circleP(vec2(72, 42), 3.2f));
        s.sub(circleP(vec2(78, 49), 3.2f));
        s.sub(circleP(vec2(66, 49), 3.2f));
        s.sub(circleP(vec2(72, 56), 3.2f));
        break;
    case ICO_USER:
        s.add(circleP(vec2(50, 32), 18));
        s.add(ellipseP(vec2(50, 92), 36, 34));
        s.sub(rectP(0, 90, 100, 100));
        break;
    case ICO_MOUSE: s.add(rrectP(20, 6, 80, 94, 30)); break;
    case ICO_MOUSE_L: {
        Shape body;
        Poly p = rrectP(25, 11, 80, 94, 26);
        s.add(p);
        s.sub(rectP(47.5f, 0, 100, 100));
        s.sub(rectP(0, 42, 100, 100));
        break;
    }
    case ICO_MOUSE_R: {
        Poly p = rrectP(20, 11, 75, 94, 26);
        s.add(p);
        s.sub(rectP(0, 0, 52.5f, 100));
        s.sub(rectP(0, 42, 100, 100));
        break;
    }
    case ICO_MOUSE_WHEEL: s.add(capsuleP(vec2(50, 19), vec2(50, 33), 4.5f)); break;
    case ICO_DPAD:
        s.add(rrectP(37, 8, 63, 92, 5));
        s.add(rrectP(8, 37, 92, 63, 5));
        s.sub(circleP(C0, 6));
        break;
    case ICO_ARROW_UP: s.add(Poly{vec2(50, 16), vec2(86, 78), vec2(14, 78)}); break;
    case ICO_CHEVRON: s.add(strokeP({vec2(36, 18), vec2(68, 50), vec2(36, 82)}, 13.f)); break;
    case ICO_PLAYER_RING: s.add(arcBandP(C0, 34, 7, 0, kTwoPi, 72)); break;
    case ICO_PIN_DOT: s.add(circleP(C0, 16)); break;
    case ICO_LOAD:
        s.add(Poly{vec2(20, 30), vec2(40, 30), vec2(46, 38), vec2(82, 38), vec2(82, 80), vec2(20, 80)});
        s.sub(rectP(26, 46, 76, 74));
        s.add(Poly{vec2(51, 50), vec2(65, 62), vec2(51, 74)});
        s.add(rectP(34, 58, 52, 66));
        break;
    case ICO_SUN:
        s.add(circleP(C0, 18));
        for (int k = 0; k < 8; k++) {
            float a = kTwoPi * k / 8.f;
            vec2 d(cosf(a), sinf(a));
            s.add(capsuleP(C0 + d * 27.f, C0 + d * 40.f, 4.f));
        }
        break;
    case ICO_PALM: {
        s.add(strokeP({vec2(46, 92), vec2(50, 70), vec2(54, 50), vec2(56, 36)}, 7.f));
        float angs[6] = {-2.7f, -2.1f, -1.2f, -0.35f, 0.3f, 0.9f};
        for (float a : angs) {
            vec2 base(56, 34);
            vec2 d(cosf(a), sinf(a));
            vec2 tip = base + d * 36.f + vec2(0, 10.f + fabsf(d.x) * 6.f);
            vec2 mid = base + d * 20.f + vec2(0, -2.f);
            vec2 n = perp(normalize(tip - base));
            s.add(Poly{base + n * 2.f, mid + n * 6.f, tip, mid - n * 3.f, base - n * 2.f});
        }
        break;
    }
    case ICO_WAVE: {
        Poly w;
        for (int i = 0; i <= 40; i++) {
            float x = 8 + 84.f * i / 40.f;
            w.push_back(vec2(x, 50 - 12.f * sinf((x - 8) / 84.f * kTwoPi * 1.5f)));
        }
        for (int i = 40; i >= 0; i--) {
            float x = 8 + 84.f * i / 40.f;
            w.push_back(vec2(x, 64 - 12.f * sinf((x - 8) / 84.f * kTwoPi * 1.5f)));
        }
        s.add(w);
        break;
    }
    default: s.add(circleP(C0, 20)); break;
    }
    return s;
}

// ------------------------------------------------------------------------------------------------------------------
// Weapon silhouettes (400 x 150 units, pointing right)
Shape makeWeapon(int w) {
    Shape s;
    switch (w) {
    case 0: {  // fists (front view of a clenched fist)
        for (int k = 0; k < 4; k++) s.add(rrectP(146 + k * 29.f, 26 + (k == 0 || k == 3 ? 6.f : 0.f), 173 + k * 29.f, 78, 12));
        s.add(rrectP(140, 56, 266, 118, 22));
        s.sub(strokeP({vec2(150, 86), vec2(232, 78)}, 5.f));
        s.add(capsuleP(vec2(154, 90), vec2(228, 82), 10.f));
        s.sub(capsuleP(vec2(154, 90), vec2(228, 82), 13.5f));
        s.add(capsuleP(vec2(154, 90), vec2(228, 82), 10.f));
        s.add(rrectP(172, 112, 244, 140, 6));
        for (int k = 1; k < 4; k++) s.sub(rectP(145 + k * 29.f - 1.5f, 30, 145 + k * 29.f + 1.5f, 66));
        break;
    }
    case 1: {  // switchblade
        s.add(smoothClosed({vec2(190, 60), vec2(260, 57), vec2(330, 58), vec2(378, 70), vec2(340, 80), vec2(260, 82), vec2(190, 82)}, 5));
        s.add(rrectP(176, 52, 194, 90, 4));
        s.add(rrectP(40, 60, 182, 84, 11));
        s.sub(circleP(vec2(162, 72), 5.f));
        for (int k = 0; k < 4; k++) s.sub(rrectP(64 + k * 20.f, 64, 70 + k * 20.f, 80, 2));
        s.sub(strokeP({vec2(206, 76), vec2(330, 76)}, 2.5f));
        break;
    }
    case 2: {  // baseball bat
        Poly bat;
        const int n = 30;
        for (int i = 0; i <= n; i++) {
            float t = (float)i / n;
            float x = 44 + t * 322.f;
            float r = t < 0.28f ? 7.f : 7.f + SmoothStep(0.28f, 0.92f, t) * 13.f;
            bat.push_back(vec2(x, 75 - r));
        }
        for (int i = 0; i <= 12; i++) {
            float a = -kHalfPi + kPi * i / 12.f;
            bat.push_back(vec2(366, 75) + vec2(cosf(a), sinf(a)) * 20.f);
        }
        for (int i = n; i >= 0; i--) {
            float t = (float)i / n;
            float x = 44 + t * 322.f;
            float r = t < 0.28f ? 7.f : 7.f + SmoothStep(0.28f, 0.92f, t) * 13.f;
            bat.push_back(vec2(x, 75 + r));
        }
        s.add(bat);
        s.add(ellipseP(vec2(40, 75), 8, 13));
        for (int k = 0; k < 5; k++) s.sub(rectP(60 + k * 14.f, 66, 62 + k * 14.f, 84));
        break;
    }
    case 3: {  // pistol
        s.add(rrectP(84, 38, 300, 64, 5));
        s.add(rectP(96, 62, 290, 76));
        s.add(Poly{vec2(104, 72), vec2(170, 72), vec2(154, 130), vec2(104, 130), vec2(94, 122)});
        s.add(strokeP({vec2(166, 76), vec2(170, 98), vec2(196, 102), vec2(210, 96), vec2(212, 76)}, 7.f));
        s.add(capsuleP(vec2(186, 78), vec2(190, 94), 3.5f));
        s.add(rectP(88, 32, 104, 40));
        s.add(rectP(282, 33, 292, 40));
        for (int k = 0; k < 5; k++) s.sub(rectP(106 + k * 8.f, 42, 109 + k * 8.f, 60));
        s.sub(rrectP(206, 43, 250, 51, 2));
        for (int k = 0; k < 4; k++) s.sub(rectP(112, 90 + k * 9.f, 150 - k * 3.f, 93 + k * 9.f));
        break;
    }
    case 4: {  // revolver
        s.add(rrectP(196, 40, 350, 60, 4));
        s.add(rectP(200, 34, 344, 42));
        s.add(rectP(336, 26, 344, 36));
        s.add(rrectP(200, 60, 300, 70, 3));
        s.add(rrectP(140, 34, 204, 78, 9));
        for (int k = 0; k < 3; k++) s.sub(rrectP(150, 42 + k * 11.f, 196, 46 + k * 11.f, 2));
        s.add(rrectP(112, 36, 146, 82, 6));
        s.add(Poly{vec2(92, 22), vec2(106, 20), vec2(122, 38), vec2(110, 44)});
        s.add(smoothClosed({vec2(108, 70), vec2(142, 74), vec2(132, 104), vec2(126, 130), vec2(96, 134), vec2(86, 118), vec2(96, 88)}, 5));
        s.add(strokeP({vec2(142, 80), vec2(146, 100), vec2(170, 102), vec2(176, 80)}, 7.f));
        s.add(capsuleP(vec2(158, 82), vec2(160, 96), 3.5f));
        s.sub(circleP(vec2(114, 104), 4.f));
        break;
    }
    case 5: {  // SMG
        s.add(rrectP(96, 42, 292, 76, 6));
        s.add(rrectP(286, 50, 352, 66, 3));
        s.add(rrectP(348, 52, 368, 64, 2));
        for (int k = 0; k < 4; k++) s.sub(rrectP(298 + k * 12.f, 55, 306 + k * 12.f, 61, 2));
        s.add(rectP(122, 34, 262, 44));
        for (int k = 0; k < 9; k++) s.sub(rectP(128 + k * 15.f, 34, 134 + k * 15.f, 38));
        s.add(Poly{vec2(196, 74), vec2(226, 74), vec2(244, 128), vec2(214, 132)});
        s.add(Poly{vec2(138, 74), vec2(168, 74), vec2(162, 122), vec2(134, 122)});
        s.add(strokeP({vec2(168, 76), vec2(172, 92), vec2(190, 92), vec2(194, 76)}, 6.f));
        s.add(rrectP(252, 74, 284, 92, 4));
        s.add(rectP(44, 48, 100, 58));
        s.add(rrectP(34, 42, 50, 86, 4));
        s.sub(rrectP(206, 50, 250, 58, 2));
        break;
    }
    case 6: {  // assault rifle
        s.add(rrectP(118, 42, 300, 66, 4));
        for (int k = 0; k < 6; k++) s.sub(rrectP(224 + k * 12.f, 49, 232 + k * 12.f, 59, 2));
        s.add(rectP(298, 51, 372, 58));
        s.add(rectP(366, 48, 380, 61));
        s.add(Poly{vec2(318, 50), vec2(326, 34), vec2(334, 34), vec2(338, 50)});
        s.add(rrectP(136, 32, 214, 44, 3));
        s.add(rectP(128, 64, 214, 80));
        s.add(smoothClosed({vec2(186, 76), vec2(214, 76), vec2(220, 100), vec2(228, 124), vec2(202, 130), vec2(194, 102)}, 4));
        s.add(Poly{vec2(140, 76), vec2(164, 76), vec2(156, 120), vec2(132, 118)});
        s.add(Poly{vec2(126, 46), vec2(40, 50), vec2(30, 54), vec2(28, 88), vec2(62, 88), vec2(126, 70)});
        s.sub(Poly{vec2(64, 60), vec2(108, 58), vec2(64, 74)});
        s.add(strokeP({vec2(164, 80), vec2(166, 92), vec2(186, 92), vec2(186, 80)}, 6.f));
        break;
    }
    case 7: {  // pump shotgun
        s.add(rrectP(140, 44, 372, 56, 3));
        s.add(rrectP(164, 58, 330, 68, 3));
        s.add(rrectP(226, 54, 302, 76, 6));
        for (int k = 0; k < 5; k++) s.sub(rectP(236 + k * 13.f, 59, 240 + k * 13.f, 72));
        s.add(rrectP(108, 40, 172, 70, 5));
        s.add(Poly{vec2(112, 44), vec2(22, 56), vec2(16, 88), vec2(40, 92), vec2(116, 68)});
        s.sub(rectP(18, 62, 26, 88));
        s.add(strokeP({vec2(140, 70), vec2(142, 84), vec2(164, 86), vec2(166, 70)}, 6.f));
        s.add(capsuleP(vec2(150, 72), vec2(152, 82), 3.f));
        s.add(rectP(360, 40, 366, 46));
        break;
    }
    case 8: {  // sniper rifle
        s.add(rectP(196, 56, 386, 62));
        s.add(rrectP(376, 54, 390, 64, 2));
        s.add(smoothClosed({vec2(16, 62), vec2(80, 60), vec2(150, 58), vec2(230, 58), vec2(300, 62), vec2(298, 72), vec2(190, 76),
                            vec2(158, 76), vec2(150, 102), vec2(126, 102), vec2(122, 80), vec2(70, 82), vec2(22, 98), vec2(14, 86)}, 4));
        s.sub(Poly{vec2(90, 68), vec2(118, 68), vec2(112, 76), vec2(90, 76)});
        s.add(rrectP(128, 30, 256, 42, 5));
        s.add(Poly{vec2(236, 30), vec2(262, 24), vec2(266, 48), vec2(236, 42)});
        s.add(Poly{vec2(116, 28), vec2(136, 30), vec2(136, 42), vec2(116, 44)});
        s.add(rectP(158, 40, 168, 58));
        s.add(rectP(212, 40, 222, 58));
        s.add(capsuleP(vec2(150, 60), vec2(142, 72), 3.f));
        s.add(circleP(vec2(141, 74), 5.f));
        s.add(rrectP(170, 72, 192, 88, 2));
        s.add(strokeP({vec2(148, 76), vec2(150, 88), vec2(166, 88)}, 5.f));
        break;
    }
    case 9: {  // RPG
        s.add(rrectP(34, 56, 300, 76, 8));
        s.add(rrectP(26, 50, 46, 82, 4));
        s.add(Poly{vec2(296, 50), vec2(322, 46), vec2(384, 66), vec2(322, 86), vec2(296, 82)});
        s.sub(strokeP({vec2(322, 46), vec2(322, 86)}, 3.f));
        s.add(Poly{vec2(126, 74), vec2(148, 74), vec2(142, 110), vec2(120, 108)});
        s.add(Poly{vec2(198, 74), vec2(218, 74), vec2(214, 104), vec2(194, 104)});
        s.add(rrectP(166, 40, 190, 56, 3));
        s.add(rectP(174, 34, 182, 42));
        s.sub(rectP(60, 62, 110, 66));
        break;
    }
    case 10: {  // grenade
        s.add(ellipseP(vec2(200, 86), 40, 46));
        s.sub(strokeP({vec2(160, 72), vec2(240, 72)}, 3.f));
        s.sub(strokeP({vec2(158, 92), vec2(242, 92)}, 3.f));
        s.sub(strokeP({vec2(162, 112), vec2(238, 112)}, 3.f));
        s.sub(strokeP({vec2(186, 44), vec2(186, 130)}, 3.f));
        s.sub(strokeP({vec2(214, 44), vec2(214, 130)}, 3.f));
        s.add(rrectP(184, 30, 216, 46, 4));
        s.add(strokeP({vec2(214, 34), vec2(238, 40), vec2(246, 60), vec2(244, 96)}, 7.f));
        s.add(arcBandP(vec2(166, 32), 11, 4.f, 0, kTwoPi, 40));
        s.add(capsuleP(vec2(176, 34), vec2(186, 36), 2.f));
        break;
    }
    case 11: {  // molotov
        s.add(rrectP(172, 64, 228, 138, 10));
        s.add(smoothClosed({vec2(174, 70), vec2(186, 56), vec2(190, 40), vec2(210, 40), vec2(214, 56), vec2(226, 70)}, 4));
        s.sub(rectP(172, 84, 228, 87));
        s.sub(rrectP(180, 94, 220, 120, 3));
        s.add(rrectP(186, 98, 214, 116, 2));
        s.add(smoothClosed({vec2(200, 42), vec2(188, 30), vec2(192, 16), vec2(200, 6), vec2(204, 18), vec2(214, 12), vec2(212, 30)}, 5));
        s.add(strokeP({vec2(190, 40), vec2(182, 50), vec2(178, 60)}, 5.f));
        break;
    }
    case 12: {  // minigun
        for (int k = 0; k < 3; k++) s.add(rrectP(200, 48 + k * 11.f, 384, 55 + k * 11.f, 3));
        s.add(rrectP(252, 42, 264, 84, 3));
        s.add(rrectP(330, 42, 342, 84, 3));
        s.add(rrectP(370, 44, 380, 82, 3));
        s.add(rrectP(100, 38, 208, 90, 10));
        s.add(strokeP({vec2(122, 38), vec2(128, 22), vec2(178, 22), vec2(184, 38)}, 7.f));
        s.add(rrectP(118, 88, 172, 124, 5));
        s.sub(rectP(126, 96, 164, 99));
        s.sub(rectP(126, 104, 164, 107));
        s.sub(rectP(126, 112, 164, 115));
        s.add(capsuleP(vec2(80, 52), vec2(100, 52), 5.f));
        s.add(capsuleP(vec2(80, 76), vec2(100, 76), 5.f));
        s.add(capsuleP(vec2(78, 44), vec2(78, 84), 5.f));
        s.sub(circleP(vec2(150, 62), 9.f));
        break;
    }
    default: s.add(rrectP(100, 50, 300, 100, 10)); break;
    }
    return s;
}

void rasterShapeToAtlas(const Shape& sh, float designW, float designH, int cellW, int cellH, int ax, int ay, std::vector<u8>& atlas) {
    int W = cellW * kDown, H = cellH * kDown;
    float scale = (float)W / designW;
    std::vector<u8> mask((size_t)W * H, 0);
    for (const Op& op : sh.ops) rasterOp(mask, W, H, op, scale, vec2(0, 0));
    (void)designH;
    sdfFromMask(mask, W, H, kDown, kSpreadRaster, &atlas[(size_t)ay * kAtlasW + ax], kAtlasW);
}

}  // namespace icons_detail

namespace uix {
using namespace icons_detail;

void ensureIcons() {
    if (g_iconsReady) return;
    double t0 = TimeSeconds();
    std::vector<u8> atlas((size_t)kAtlasW * kAtlasH, 0);
    int jobs = ICO_COUNT + kWeaponIconCount;
    Jobs::parallelFor(jobs, [&](int j) {
        if (j < ICO_COUNT) {
            Shape s = makeIcon(j);
            int cx = (j % kIconsPerRow) * kCell, cy = (j / kIconsPerRow) * kCell;
            rasterShapeToAtlas(s, 100.f, 100.f, kCell, kCell, cx, cy, atlas);
        } else {
            int w = j - ICO_COUNT;
            Shape s = makeWeapon(w);
            int cx = (w % 4) * kWpnW, cy = kWpnY0 + (w / 4) * kWpnH;
            rasterShapeToAtlas(s, 400.f, 150.f, kWpnW, kWpnH, cx, cy, atlas);
        }
    });
    g_iconTex = gfx::createTexture2D(kAtlasW, kAtlasH, DXGI_FORMAT_R8_UNORM, gfx::TEX_SRV, 1, 1, atlas.data(), kAtlasW);
    setIconAtlas(g_iconTex.srv);
    g_iconsReady = true;
    LOG("UI icon atlas: %d icons + %d weapons in %.0f ms", (int)ICO_COUNT, kWeaponIconCount, (TimeSeconds() - t0) * 1000.0);
}

bool iconsReady() { return g_iconsReady; }

void drawIcon(int id, float cx, float cy, float size, u32 color, float outlinePx, u32 outlineColor, float angle, float soft) {
    if (!g_iconsReady) ensureIcons();
    float u0, v0, u1, v1;
    iconCellUV(id, u0, v0, u1, v1);
    float pxRange = (2.f * kSpreadRaster / (kCell * kDown)) * size;
    iconSdf(cx - size * 0.5f, cy - size * 0.5f, size, size, u0, v0, u1, v1, color, pxRange, outlinePx, outlineColor, soft, angle);
}

void drawIconGlow(int id, float cx, float cy, float size, u32 color, float spreadPx) {
    if (!g_iconsReady) ensureIcons();
    float u0, v0, u1, v1;
    iconCellUV(id, u0, v0, u1, v1);
    float pxRange = (2.f * kSpreadRaster / (kCell * kDown)) * size;
    float maxPx = 0.45f * pxRange;
    iconSdf(cx - size * 0.5f, cy - size * 0.5f, size, size, u0, v0, u1, v1, color, pxRange, Min(spreadPx, maxPx), color, 3.f, 0.f);
}

void drawWeapon(int weapon, float cx, float cy, float width, u32 color, float outlinePx, u32 outlineColor, float soft) {
    if (!g_iconsReady) ensureIcons();
    if (weapon < 0 || weapon >= kWeaponIconCount) return;
    float u0, v0, u1, v1;
    weaponCellUV(weapon, u0, v0, u1, v1);
    float h = width * (float)kWpnH / kWpnW;
    float pxRange = (2.f * kSpreadRaster / (kWpnW * kDown)) * width;
    iconSdf(cx - width * 0.5f, cy - h * 0.5f, width, h, u0, v0, u1, v1, color, pxRange, outlinePx, outlineColor, soft, 0.f);
}

u32 blipDefaultColor(BlipIcon icon) {
    switch (icon) {
    case BLIP_PLAYER: return kWhite;
    case BLIP_WAYPOINT: return kPink;
    case BLIP_MISSION: return kGold;
    case BLIP_OBJECTIVE: return kYellow;
    case BLIP_ENEMY: return kRed;
    case BLIP_FRIEND: return kBlue;
    case BLIP_POLICE: return kRed;
    case BLIP_POLICE_HELI: return kBlue;
    case BLIP_VEHICLE: return C(0.45f, 0.82f, 1.f);
    case BLIP_SAFEHOUSE: return C(0.36f, 0.94f, 0.62f);
    case BLIP_HOSPITAL: return C(1.f, 0.42f, 0.45f);
    case BLIP_POLICE_STATION: return C(0.42f, 0.66f, 1.f);
    case BLIP_RACE: return kWhite;
    case BLIP_STUNT_JUMP: return kOrange;
    case BLIP_COLLECTIBLE: return kCyan;
    case BLIP_HIDEOUT: return C(1.f, 0.36f, 0.36f);
    case BLIP_VIGILANTE: return C(0.5f, 0.75f, 1.f);
    case BLIP_TAXI_JOB: return kYellow;
    case BLIP_DELIVERY_JOB: return C(0.95f, 0.72f, 0.4f);
    case BLIP_AIRPORT: return C(0.8f, 0.86f, 1.f);
    default: return kWhite;
    }
}

const char* blipDefaultName(BlipIcon icon) {
    static const char* names[BLIP_COUNT] = {"Point of Interest", "You", "Waypoint", "Story Mission", "Objective", "Enemy",
                                            "Friendly", "Police", "Police Helicopter", "Vehicle", "Safehouse", "Gun Store",
                                            "Clothing Store", "Car Dealer", "Mod Garage", "Hospital", "Police Station", "Race",
                                            "Taxi Job", "Delivery Job", "Vigilante", "Stunt Jump", "Collectible", "Boat",
                                            "Helicopter", "Plane", "Bar", "Convenience Store", "Bank", "Airport", "Hideout"};
    return icon < BLIP_COUNT ? names[icon] : "";
}

bool blipIsRound(BlipIcon icon) {
    return icon == BLIP_DOT || icon == BLIP_OBJECTIVE || icon == BLIP_ENEMY || icon == BLIP_FRIEND || icon == BLIP_POLICE;
}

}  // namespace uix
}  // namespace UI
