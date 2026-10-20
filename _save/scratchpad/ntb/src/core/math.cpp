#include "math.h"

quat slerp(quat a, quat b, float t) {
    float d = dot(a, b);
    if (d < 0.f) { b = quat(-b.x, -b.y, -b.z, -b.w); d = -d; }
    if (d > 0.9995f) return nlerp(a, b, t);
    float th = acosf(Clamp(d, -1.f, 1.f));
    float s = sinf(th);
    float wa = sinf((1.f - t) * th) / s, wb = sinf(t * th) / s;
    return quat(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
}

quat quatFromTo(vec3 a, vec3 b) {
    float d = dot(a, b);
    if (d > 0.999999f) return quat();
    if (d < -0.999999f) {
        vec3 ax = normalize(anyPerp(a));
        return quatAxisAngle(ax, kPi);
    }
    vec3 c = cross(a, b);
    return normalize(quat(c.x, c.y, c.z, 1.f + d));
}

quat quatFromEuler(float yaw, float pitch, float roll) {
    quat qy = quatAxisAngle(vec3(0, 0, 1), yaw);
    quat qp = quatAxisAngle(vec3(1, 0, 0), pitch);
    quat qr = quatAxisAngle(vec3(0, 1, 0), roll);
    return qy * qp * qr;
}

mat3 operator*(const mat3& a, const mat3& b) { return mat3(a * b.c[0], a * b.c[1], a * b.c[2]); }
mat3 transpose(const mat3& m) {
    return mat3(vec3(m.c[0].x, m.c[1].x, m.c[2].x), vec3(m.c[0].y, m.c[1].y, m.c[2].y), vec3(m.c[0].z, m.c[1].z, m.c[2].z));
}
mat3 inverse(const mat3& m) {
    vec3 a = m.c[0], b = m.c[1], c = m.c[2];
    vec3 r0 = cross(b, c), r1 = cross(c, a), r2 = cross(a, b);
    float det = dot(a, r0);
    if (fabsf(det) < 1e-20f) return mat3();
    float inv = 1.f / det;
    // rows of inverse are r0, r1, r2 scaled
    return transpose(mat3(r0 * inv, r1 * inv, r2 * inv));
}
mat3 mat3FromQuat(quat q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return mat3(vec3(1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy)),
                vec3(2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx)),
                vec3(2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy)));
}
quat quatFromMat3(const mat3& m) {
    float m00 = m.c[0].x, m11 = m.c[1].y, m22 = m.c[2].z;
    float tr = m00 + m11 + m22;
    quat q;
    if (tr > 0.f) {
        float s = sqrtf(tr + 1.f) * 2.f;
        q.w = 0.25f * s;
        q.x = (m.c[1].z - m.c[2].y) / s;
        q.y = (m.c[2].x - m.c[0].z) / s;
        q.z = (m.c[0].y - m.c[1].x) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.f + m00 - m11 - m22) * 2.f;
        q.w = (m.c[1].z - m.c[2].y) / s;
        q.x = 0.25f * s;
        q.y = (m.c[1].x + m.c[0].y) / s;
        q.z = (m.c[2].x + m.c[0].z) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.f + m11 - m00 - m22) * 2.f;
        q.w = (m.c[2].x - m.c[0].z) / s;
        q.x = (m.c[1].x + m.c[0].y) / s;
        q.y = 0.25f * s;
        q.z = (m.c[2].y + m.c[1].z) / s;
    } else {
        float s = sqrtf(1.f + m22 - m00 - m11) * 2.f;
        q.w = (m.c[0].y - m.c[1].x) / s;
        q.x = (m.c[2].x + m.c[0].z) / s;
        q.y = (m.c[2].y + m.c[1].z) / s;
        q.z = 0.25f * s;
    }
    return normalize(q);
}

mat4 operator*(const mat4& a, const mat4& b) { return mat4(a * b.c[0], a * b.c[1], a * b.c[2], a * b.c[3]); }
mat4 transpose(const mat4& m) {
    mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r.c[i][j] = m.c[j][i];
    return r;
}
mat4 inverse(const mat4& mm) {
    // Standard cofactor expansion on a flat array (column-major: m[col*4+row]).
    const float* m = &mm.c[0].x;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r;
    if (fabsf(det) < 1e-30f) return r;
    det = 1.f / det;
    float* o = &r.c[0].x;
    for (int i = 0; i < 16; i++) o[i] = inv[i] * det;
    return r;
}
mat4 affineInverse(const mat4& m) {
    mat3 r(m.c[0].xyz(), m.c[1].xyz(), m.c[2].xyz());
    mat3 ri = inverse(r);
    vec3 t = ri * m.c[3].xyz();
    return mat4(vec4(ri.c[0], 0), vec4(ri.c[1], 0), vec4(ri.c[2], 0), vec4(-t, 1));
}
mat4 mat4Translation(vec3 t) {
    mat4 m;
    m.c[3] = vec4(t, 1);
    return m;
}
mat4 mat4Scale(vec3 s) {
    mat4 m;
    m.c[0].x = s.x; m.c[1].y = s.y; m.c[2].z = s.z;
    return m;
}
mat4 mat4FromQuat(quat q) {
    mat3 r = mat3FromQuat(q);
    return mat4(vec4(r.c[0], 0), vec4(r.c[1], 0), vec4(r.c[2], 0), vec4(0, 0, 0, 1));
}
mat4 mat4TRS(vec3 t, quat q, vec3 s) {
    mat3 r = mat3FromQuat(q);
    return mat4(vec4(r.c[0] * s.x, 0), vec4(r.c[1] * s.y, 0), vec4(r.c[2] * s.z, 0), vec4(t, 1));
}
mat4 mat4FromBasis(vec3 x, vec3 y, vec3 z, vec3 o) { return mat4(vec4(x, 0), vec4(y, 0), vec4(z, 0), vec4(o, 1)); }

mat4 lookAtRH(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    return mat4(vec4(s.x, u.x, -f.x, 0), vec4(s.y, u.y, -f.y, 0), vec4(s.z, u.z, -f.z, 0),
                vec4(-dot(s, eye), -dot(u, eye), dot(f, eye), 1));
}
mat4 perspectiveReversedInfRH(float fovY, float aspect, float zNear) {
    float f = 1.f / tanf(fovY * 0.5f);
    return mat4(vec4(f / aspect, 0, 0, 0), vec4(0, f, 0, 0), vec4(0, 0, 0, -1), vec4(0, 0, zNear, 0));
}
mat4 perspectiveReversedRH(float fovY, float aspect, float n, float fz) {
    float f = 1.f / tanf(fovY * 0.5f);
    float A = n / (fz - n), B = n * fz / (fz - n);
    return mat4(vec4(f / aspect, 0, 0, 0), vec4(0, f, 0, 0), vec4(0, 0, A, -1), vec4(0, 0, B, 0));
}
mat4 orthoRH(float l, float r, float b, float t, float zn, float zf) {
    return mat4(vec4(2.f / (r - l), 0, 0, 0), vec4(0, 2.f / (t - b), 0, 0), vec4(0, 0, -1.f / (zf - zn), 0),
                vec4(-(r + l) / (r - l), -(t + b) / (t - b), -zn / (zf - zn), 1));
}

AABB transformAABB(const AABB& b, const mat4& m) {
    vec3 c = transformPoint(m, b.center());
    vec3 e = b.extent();
    vec3 ne(fabsf(m.c[0].x) * e.x + fabsf(m.c[1].x) * e.y + fabsf(m.c[2].x) * e.z,
            fabsf(m.c[0].y) * e.x + fabsf(m.c[1].y) * e.y + fabsf(m.c[2].y) * e.z,
            fabsf(m.c[0].z) * e.x + fabsf(m.c[1].z) * e.y + fabsf(m.c[2].z) * e.z);
    return AABB(c - ne, c + ne);
}

void Frustum::fromMatrix(const mat4& m) {
    vec4 row[4];
    for (int i = 0; i < 4; i++) row[i] = vec4(m.c[0][i], m.c[1][i], m.c[2][i], m.c[3][i]);
    planes[0] = row[3] + row[0];
    planes[1] = row[3] - row[0];
    planes[2] = row[3] + row[1];
    planes[3] = row[3] - row[1];
    planes[4] = row[2];
    planes[5] = row[3] - row[2];
    for (int i = 0; i < 6; i++) {
        float l = length(planes[i].xyz());
        if (l > 1e-20f) planes[i] = planes[i] * (1.f / l);
    }
}
bool Frustum::testAABB(const AABB& b) const {
    for (int i = 0; i < 6; i++) {
        const vec4& p = planes[i];
        vec3 v(p.x >= 0 ? b.mx.x : b.mn.x, p.y >= 0 ? b.mx.y : b.mn.y, p.z >= 0 ? b.mx.z : b.mn.z);
        if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0.f) return false;
    }
    return true;
}
bool Frustum::testSphere(vec3 c, float r) const {
    for (int i = 0; i < 6; i++) {
        const vec4& p = planes[i];
        if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    }
    return true;
}

bool rayAABB(vec3 ro, vec3 rdInv, const AABB& b, float tMax, float& tHit) {
    float t1 = (b.mn.x - ro.x) * rdInv.x, t2 = (b.mx.x - ro.x) * rdInv.x;
    float tmin = Min(t1, t2), tmax = Max(t1, t2);
    t1 = (b.mn.y - ro.y) * rdInv.y; t2 = (b.mx.y - ro.y) * rdInv.y;
    tmin = Max(tmin, Min(t1, t2)); tmax = Min(tmax, Max(t1, t2));
    t1 = (b.mn.z - ro.z) * rdInv.z; t2 = (b.mx.z - ro.z) * rdInv.z;
    tmin = Max(tmin, Min(t1, t2)); tmax = Min(tmax, Max(t1, t2));
    if (tmax < Max(tmin, 0.f) || tmin > tMax) return false;
    tHit = Max(tmin, 0.f);
    return true;
}
bool raySphere(vec3 ro, vec3 rd, vec3 c, float r, float& t) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float cc = dot(oc, oc) - r * r;
    float h = b * b - cc;
    if (h < 0.f) return false;
    h = sqrtf(h);
    t = -b - h;
    if (t < 0.f) t = -b + h;
    return t >= 0.f;
}
bool rayTriangle(vec3 ro, vec3 rd, vec3 a, vec3 b, vec3 c, float& t, float& u, float& v) {
    vec3 e1 = b - a, e2 = c - a;
    vec3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (fabsf(det) < 1e-12f) return false;
    float inv = 1.f / det;
    vec3 s = ro - a;
    u = dot(s, p) * inv;
    if (u < 0.f || u > 1.f) return false;
    vec3 q = cross(s, e1);
    v = dot(rd, q) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    t = dot(e2, q) * inv;
    return t >= 0.f;
}

vec3 srgbToLinear(vec3 c) {
    auto f = [](float x) { return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f); };
    return vec3(f(c.x), f(c.y), f(c.z));
}
vec3 hsvToRgb(float h, float s, float v) {
    h = h - floorf(h);
    float r = fabsf(h * 6.f - 3.f) - 1.f, g = 2.f - fabsf(h * 6.f - 2.f), b = 2.f - fabsf(h * 6.f - 4.f);
    vec3 rgb(Saturate(r), Saturate(g), Saturate(b));
    return (vec3(1.f) + (rgb - vec3(1.f)) * s) * v;
}
u32 packRGBA8(float r, float g, float b, float a) {
    u32 R = (u32)(Saturate(r) * 255.f + 0.5f), G = (u32)(Saturate(g) * 255.f + 0.5f);
    u32 B = (u32)(Saturate(b) * 255.f + 0.5f), A = (u32)(Saturate(a) * 255.f + 0.5f);
    return R | (G << 8) | (B << 16) | (A << 24);
}
u32 packRGBA8(vec4 c) { return packRGBA8(c.x, c.y, c.z, c.w); }
vec4 unpackRGBA8(u32 c) {
    return vec4((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f, ((c >> 24) & 255) / 255.f);
}

u32 packNormalOct(vec3 n) {
    float s = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
    if (s < 1e-20f) return 0;
    float px = n.x / s, py = n.y / s;
    if (n.z < 0.f) {
        float ox = (1.f - fabsf(py)) * (px >= 0.f ? 1.f : -1.f);
        float oy = (1.f - fabsf(px)) * (py >= 0.f ? 1.f : -1.f);
        px = ox; py = oy;
    }
    i32 ix = (i32)lrintf(Clamp(px, -1.f, 1.f) * 32767.f);
    i32 iy = (i32)lrintf(Clamp(py, -1.f, 1.f) * 32767.f);
    return ((u32)(u16)(i16)ix) | (((u32)(u16)(i16)iy) << 16);
}
vec3 unpackNormalOct(u32 p) {
    float px = (float)(i16)(p & 0xffff) / 32767.f, py = (float)(i16)(p >> 16) / 32767.f;
    vec3 n(px, py, 1.f - fabsf(px) - fabsf(py));
    if (n.z < 0.f) {
        float ox = (1.f - fabsf(n.y)) * (n.x >= 0.f ? 1.f : -1.f);
        float oy = (1.f - fabsf(n.x)) * (n.y >= 0.f ? 1.f : -1.f);
        n.x = ox; n.y = oy;
    }
    return normalize(n);
}

float distPointSegment2D(vec2 p, vec2 a, vec2 b, float* tOut) {
    vec2 ab = b - a;
    float l2 = dot(ab, ab);
    float t = l2 > 1e-20f ? Saturate(dot(p - a, ab) / l2) : 0.f;
    if (tOut) *tOut = t;
    return length(p - (a + ab * t));
}
bool segmentIntersect2D(vec2 a, vec2 b, vec2 c, vec2 d, float* ta, float* tc) {
    vec2 r = b - a, s = d - c;
    float den = cross(r, s);
    if (fabsf(den) < 1e-12f) return false;
    vec2 ac = c - a;
    float t = cross(ac, s) / den;
    float u = cross(ac, r) / den;
    if (t < 0.f || t > 1.f || u < 0.f || u > 1.f) return false;
    if (ta) *ta = t;
    if (tc) *tc = u;
    return true;
}
bool pointInPolygon2D(vec2 p, const vec2* poly, int n) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
            inside = !inside;
    }
    return inside;
}
float polygonArea2D(const vec2* poly, int n) {
    float a = 0.f;
    for (int i = 0, j = n - 1; i < n; j = i++) a += cross(poly[j], poly[i]);
    return a * 0.5f;
}
