// Vector / matrix / quaternion math. World convention: X east, Y north, Z up.
// Matrices are column-major (m.c[column]) and transform column vectors (M * v),
// which matches HLSL's default column_major cbuffer packing with mul(M, v).
#pragma once
#include "base.h"

struct vec2 {
    float x, y;
    vec2() : x(0), y(0) {}
    constexpr vec2(float a, float b) : x(a), y(b) {}
    explicit vec2(float s) : x(s), y(s) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
FORCEINLINE vec2 operator+(vec2 a, vec2 b) { return vec2(a.x + b.x, a.y + b.y); }
FORCEINLINE vec2 operator-(vec2 a, vec2 b) { return vec2(a.x - b.x, a.y - b.y); }
FORCEINLINE vec2 operator*(vec2 a, vec2 b) { return vec2(a.x * b.x, a.y * b.y); }
FORCEINLINE vec2 operator/(vec2 a, vec2 b) { return vec2(a.x / b.x, a.y / b.y); }
FORCEINLINE vec2 operator*(vec2 a, float s) { return vec2(a.x * s, a.y * s); }
FORCEINLINE vec2 operator*(float s, vec2 a) { return vec2(a.x * s, a.y * s); }
FORCEINLINE vec2 operator/(vec2 a, float s) { float r = 1.f / s; return vec2(a.x * r, a.y * r); }
FORCEINLINE vec2 operator-(vec2 a) { return vec2(-a.x, -a.y); }
FORCEINLINE vec2& operator+=(vec2& a, vec2 b) { a.x += b.x; a.y += b.y; return a; }
FORCEINLINE vec2& operator-=(vec2& a, vec2 b) { a.x -= b.x; a.y -= b.y; return a; }
FORCEINLINE vec2& operator*=(vec2& a, float s) { a.x *= s; a.y *= s; return a; }
FORCEINLINE float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
FORCEINLINE float cross(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
FORCEINLINE float length(vec2 a) { return sqrtf(dot(a, a)); }
FORCEINLINE float length2(vec2 a) { return dot(a, a); }
FORCEINLINE vec2 normalize(vec2 a) { float l = length(a); return l > 1e-12f ? a / l : vec2(1, 0); }
FORCEINLINE vec2 perp(vec2 a) { return vec2(-a.y, a.x); }  // rotate +90 degrees
FORCEINLINE vec2 lerp(vec2 a, vec2 b, float t) { return a + (b - a) * t; }
FORCEINLINE vec2 vmin(vec2 a, vec2 b) { return vec2(Min(a.x, b.x), Min(a.y, b.y)); }
FORCEINLINE vec2 vmax(vec2 a, vec2 b) { return vec2(Max(a.x, b.x), Max(a.y, b.y)); }
FORCEINLINE float distance(vec2 a, vec2 b) { return length(a - b); }
FORCEINLINE vec2 rotate(vec2 v, float ang) { float c = cosf(ang), s = sinf(ang); return vec2(c * v.x - s * v.y, s * v.x + c * v.y); }

struct vec3 {
    float x, y, z;
    vec3() : x(0), y(0), z(0) {}
    constexpr vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3(vec2 v, float c) : x(v.x), y(v.y), z(c) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
    vec2 xy() const { return vec2(x, y); }
};
FORCEINLINE vec3 operator+(vec3 a, vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
FORCEINLINE vec3 operator-(vec3 a, vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
FORCEINLINE vec3 operator*(vec3 a, vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
FORCEINLINE vec3 operator/(vec3 a, vec3 b) { return vec3(a.x / b.x, a.y / b.y, a.z / b.z); }
FORCEINLINE vec3 operator*(vec3 a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
FORCEINLINE vec3 operator*(float s, vec3 a) { return vec3(a.x * s, a.y * s, a.z * s); }
FORCEINLINE vec3 operator/(vec3 a, float s) { float r = 1.f / s; return vec3(a.x * r, a.y * r, a.z * r); }
FORCEINLINE vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
FORCEINLINE vec3& operator+=(vec3& a, vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
FORCEINLINE vec3& operator-=(vec3& a, vec3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
FORCEINLINE vec3& operator*=(vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
FORCEINLINE vec3& operator*=(vec3& a, vec3 b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; return a; }
FORCEINLINE vec3& operator/=(vec3& a, float s) { float r = 1.f / s; a.x *= r; a.y *= r; a.z *= r; return a; }
FORCEINLINE bool operator==(vec3 a, vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
FORCEINLINE float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
FORCEINLINE vec3 cross(vec3 a, vec3 b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
FORCEINLINE float length(vec3 a) { return sqrtf(dot(a, a)); }
FORCEINLINE float length2(vec3 a) { return dot(a, a); }
FORCEINLINE vec3 normalize(vec3 a) { float l = length(a); return l > 1e-12f ? a / l : vec3(0, 0, 1); }
FORCEINLINE vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
FORCEINLINE vec3 vmin(vec3 a, vec3 b) { return vec3(Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z)); }
FORCEINLINE vec3 vmax(vec3 a, vec3 b) { return vec3(Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z)); }
FORCEINLINE vec3 vabs(vec3 a) { return vec3(fabsf(a.x), fabsf(a.y), fabsf(a.z)); }
FORCEINLINE float distance(vec3 a, vec3 b) { return length(a - b); }
FORCEINLINE float maxc(vec3 a) { return Max(a.x, Max(a.y, a.z)); }
FORCEINLINE float minc(vec3 a) { return Min(a.x, Min(a.y, a.z)); }
FORCEINLINE vec3 reflect(vec3 v, vec3 n) { return v - n * (2.f * dot(v, n)); }
FORCEINLINE vec3 saturate(vec3 v) { return vec3(Saturate(v.x), Saturate(v.y), Saturate(v.z)); }
// Returns a vector perpendicular to n (not normalized for general n).
FORCEINLINE vec3 anyPerp(vec3 n) { return fabsf(n.z) < 0.9f ? cross(n, vec3(0, 0, 1)) : cross(n, vec3(1, 0, 0)); }

struct vec4 {
    float x, y, z, w;
    vec4() : x(0), y(0), z(0), w(0) {}
    constexpr vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
    vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
    vec3 xyz() const { return vec3(x, y, z); }
    vec2 xy() const { return vec2(x, y); }
};
FORCEINLINE vec4 operator+(vec4 a, vec4 b) { return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
FORCEINLINE vec4 operator-(vec4 a, vec4 b) { return vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
FORCEINLINE vec4 operator*(vec4 a, vec4 b) { return vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
FORCEINLINE vec4 operator*(vec4 a, float s) { return vec4(a.x * s, a.y * s, a.z * s, a.w * s); }
FORCEINLINE vec4 operator*(float s, vec4 a) { return vec4(a.x * s, a.y * s, a.z * s, a.w * s); }
FORCEINLINE float dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
FORCEINLINE vec4 lerp(vec4 a, vec4 b, float t) { return a + (b - a) * t; }

struct ivec2 {
    int x, y;
    ivec2() : x(0), y(0) {}
    constexpr ivec2(int a, int b) : x(a), y(b) {}
};
FORCEINLINE bool operator==(ivec2 a, ivec2 b) { return a.x == b.x && a.y == b.y; }
FORCEINLINE bool operator!=(ivec2 a, ivec2 b) { return !(a == b); }

// ---------------------------------------------------------------------------
struct quat {
    float x, y, z, w;
    quat() : x(0), y(0), z(0), w(1) {}
    constexpr quat(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};
FORCEINLINE quat operator*(quat a, quat b) {
    return quat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
FORCEINLINE quat conj(quat q) { return quat(-q.x, -q.y, -q.z, q.w); }
FORCEINLINE float dot(quat a, quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
FORCEINLINE quat normalize(quat q) {
    float l = sqrtf(dot(q, q));
    if (l < 1e-12f) return quat();
    float r = 1.f / l;
    return quat(q.x * r, q.y * r, q.z * r, q.w * r);
}
FORCEINLINE quat quatAxisAngle(vec3 axis, float ang) {
    vec3 a = normalize(axis);
    float s = sinf(ang * 0.5f);
    return quat(a.x * s, a.y * s, a.z * s, cosf(ang * 0.5f));
}
FORCEINLINE vec3 rotate(quat q, vec3 v) {
    vec3 u(q.x, q.y, q.z);
    vec3 t = cross(u, v) * 2.f;
    return v + t * q.w + cross(u, t);
}
FORCEINLINE quat nlerp(quat a, quat b, float t) {
    if (dot(a, b) < 0.f) b = quat(-b.x, -b.y, -b.z, -b.w);
    return normalize(quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
}
quat slerp(quat a, quat b, float t);
// Rotation taking unit vector a onto unit vector b.
quat quatFromTo(vec3 a, vec3 b);
// Euler: yaw around Z, pitch around X (after yaw), roll around Y (forward).
quat quatFromEuler(float yaw, float pitch, float roll);

// ---------------------------------------------------------------------------
struct mat3 {
    vec3 c[3];
    mat3() { c[0] = vec3(1, 0, 0); c[1] = vec3(0, 1, 0); c[2] = vec3(0, 0, 1); }
    mat3(vec3 a, vec3 b, vec3 d) { c[0] = a; c[1] = b; c[2] = d; }
};
FORCEINLINE vec3 operator*(const mat3& m, vec3 v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z; }
mat3 operator*(const mat3& a, const mat3& b);
mat3 transpose(const mat3& m);
mat3 inverse(const mat3& m);
mat3 mat3FromQuat(quat q);
quat quatFromMat3(const mat3& m);

struct mat4 {
    vec4 c[4];
    mat4() { c[0] = vec4(1, 0, 0, 0); c[1] = vec4(0, 1, 0, 0); c[2] = vec4(0, 0, 1, 0); c[3] = vec4(0, 0, 0, 1); }
    mat4(vec4 a, vec4 b, vec4 d, vec4 e) { c[0] = a; c[1] = b; c[2] = d; c[3] = e; }
};
FORCEINLINE vec4 operator*(const mat4& m, vec4 v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z + m.c[3] * v.w; }
FORCEINLINE vec3 transformPoint(const mat4& m, vec3 p) { return (m.c[0] * p.x + m.c[1] * p.y + m.c[2] * p.z + m.c[3]).xyz(); }
FORCEINLINE vec3 transformDir(const mat4& m, vec3 d) { return (m.c[0] * d.x + m.c[1] * d.y + m.c[2] * d.z).xyz(); }
mat4 operator*(const mat4& a, const mat4& b);
mat4 transpose(const mat4& m);
mat4 inverse(const mat4& m);
mat4 affineInverse(const mat4& m);
mat4 mat4Translation(vec3 t);
mat4 mat4Scale(vec3 s);
mat4 mat4FromQuat(quat q);
mat4 mat4TRS(vec3 t, quat r, vec3 s);
mat4 mat4FromBasis(vec3 x, vec3 y, vec3 z, vec3 origin);
// Right-handed view matrix: camera looks along -Z in view space, +Y up in view space.
mat4 lookAtRH(vec3 eye, vec3 target, vec3 up);
// Reversed-Z infinite perspective for D3D (depth 1 at near plane, 0 at infinity).
mat4 perspectiveReversedInfRH(float fovY, float aspect, float zNear);
// Reversed-Z finite perspective.
mat4 perspectiveReversedRH(float fovY, float aspect, float zNear, float zFar);
// Orthographic RH to D3D depth [0,1] (standard, not reversed).
mat4 orthoRH(float l, float r, float b, float t, float zn, float zf);

// ---------------------------------------------------------------------------
struct AABB {
    vec3 mn, mx;
    AABB() : mn(1e30f), mx(-1e30f) {}
    AABB(vec3 a, vec3 b) : mn(a), mx(b) {}
    void add(vec3 p) { mn = vmin(mn, p); mx = vmax(mx, p); }
    void add(const AABB& b) { mn = vmin(mn, b.mn); mx = vmax(mx, b.mx); }
    bool valid() const { return mn.x <= mx.x; }
    vec3 center() const { return (mn + mx) * 0.5f; }
    vec3 extent() const { return (mx - mn) * 0.5f; }
    bool overlaps(const AABB& b) const {
        return mn.x <= b.mx.x && mx.x >= b.mn.x && mn.y <= b.mx.y && mx.y >= b.mn.y && mn.z <= b.mx.z && mx.z >= b.mn.z;
    }
    bool contains(vec3 p) const { return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z && p.z <= mx.z; }
};
AABB transformAABB(const AABB& b, const mat4& m);

struct Frustum {
    vec4 planes[6];  // inward-facing: dot(p.xyz, pt) + p.w >= 0 means inside
    void fromMatrix(const mat4& viewProj);
    bool testAABB(const AABB& b) const;
    bool testSphere(vec3 c, float r) const;
};

// Ray / shape intersection helpers. Return true and t on hit.
bool rayAABB(vec3 ro, vec3 rdInv, const AABB& b, float tMax, float& tHit);
bool raySphere(vec3 ro, vec3 rd, vec3 c, float r, float& t);
bool rayTriangle(vec3 ro, vec3 rd, vec3 a, vec3 b, vec3 c, float& t, float& u, float& v);

// Color helpers
vec3 srgbToLinear(vec3 c);
vec3 hsvToRgb(float h, float s, float v);
u32 packRGBA8(vec4 c);
u32 packRGBA8(float r, float g, float b, float a);
vec4 unpackRGBA8(u32 c);

// Octahedral normal encoding into two snorm16 packed into u32.
u32 packNormalOct(vec3 n);
vec3 unpackNormalOct(u32 p);

FORCEINLINE float wrapAngle(float a) {
    if (!(a > -1e6f && a < 1e6f)) return 0.f;   // NaN / inf / absurd input: never loop
    if (a > kPi || a < -kPi) a = remainderf(a, kTwoPi);   // one step to [-pi, pi]
    return a;
}
FORCEINLINE float approach(float cur, float target, float maxDelta) {
    if (cur < target) return Min(cur + maxDelta, target);
    return Max(cur - maxDelta, target);
}
// Frame-rate independent exponential smoothing factor.
FORCEINLINE float expDecay(float rate, float dt) { return 1.f - expf(-rate * dt); }

// Distance from point p to segment ab (2D), also returns param t.
float distPointSegment2D(vec2 p, vec2 a, vec2 b, float* tOut = nullptr);
// Segment-segment intersection in 2D.
bool segmentIntersect2D(vec2 a, vec2 b, vec2 c, vec2 d, float* ta = nullptr, float* tc = nullptr);
bool pointInPolygon2D(vec2 p, const vec2* poly, int n);
float polygonArea2D(const vec2* poly, int n);

// Double-precision position for world-space camera/object placement over a 20 km world.
struct dvec3 {
    double x, y, z;
    dvec3() : x(0), y(0), z(0) {}
    constexpr dvec3(double a, double b, double c) : x(a), y(b), z(c) {}
    explicit dvec3(vec3 v) : x(v.x), y(v.y), z(v.z) {}
    vec3 toVec3() const { return vec3((float)x, (float)y, (float)z); }
};
FORCEINLINE dvec3 operator+(dvec3 a, dvec3 b) { return dvec3(a.x + b.x, a.y + b.y, a.z + b.z); }
FORCEINLINE dvec3 operator-(dvec3 a, dvec3 b) { return dvec3(a.x - b.x, a.y - b.y, a.z - b.z); }
FORCEINLINE dvec3 operator+(dvec3 a, vec3 b) { return dvec3(a.x + b.x, a.y + b.y, a.z + b.z); }
FORCEINLINE dvec3 operator-(dvec3 a, vec3 b) { return dvec3(a.x - b.x, a.y - b.y, a.z - b.z); }
FORCEINLINE dvec3 operator*(dvec3 a, double s) { return dvec3(a.x * s, a.y * s, a.z * s); }
// Relative offset (a - b) as float: used for camera-relative rendering.
FORCEINLINE vec3 rel(dvec3 a, dvec3 b) { return vec3((float)(a.x - b.x), (float)(a.y - b.y), (float)(a.z - b.z)); }
FORCEINLINE vec3 rel(vec3 a, dvec3 b) { return vec3((float)((double)a.x - b.x), (float)((double)a.y - b.y), (float)((double)a.z - b.z)); }
