#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
bool rayCapsule(vec3 o, vec3 d, vec3 a, vec3 b, float r, float& t) {
    vec3 ba = b - a, oa = o - a;
    float baba = dot(ba, ba), bard = dot(ba, d), baoa = dot(ba, oa), rdoa = dot(d, oa), oaoa = dot(oa, oa);
    float A = baba - bard * bard;
    float B = baba * rdoa - baoa * bard;
    float C = baba * oaoa - baoa * baoa - r * r * baba;
    float h = B * B - A * C;
    if (A > 1e-8f && h >= 0.f) {
        float tt = (-B - sqrtf(h)) / A;
        float y = baoa + tt * bard;
        if (y > 0.f && y < baba && tt > 0.f) {
            t = tt;
            return true;
        }
        vec3 oc = y <= 0.f ? oa : o - b;
        B = dot(d, oc);
        C = dot(oc, oc) - r * r;
        h = B * B - C;
        if (h > 0.f) {
            tt = -B - sqrtf(h);
            if (tt > 0.f) {
                t = tt;
                return true;
            }
        }
        return false;
    }
    // degenerate (sphere)
    vec3 oc = oa;
    B = dot(d, oc);
    C = dot(oc, oc) - r * r;
    h = B * B - C;
    if (h < 0.f) return false;
    float tt = -B - sqrtf(h);
    if (tt <= 0.f) return false;
    t = tt;
    return true;
}

bool rayObb(vec3 o, vec3 d, vec3 c, const mat3& R, vec3 he, float& t, vec3& nrm) {
    mat3 Rt = transpose(R);
    vec3 lo = Rt * (o - c), ld = Rt * d;
    float tn = -1e30f, tf = 1e30f;
    int axis = 0;
    float sign = 1.f;
    for (int i = 0; i < 3; i++) {
        float oi = i == 0 ? lo.x : (i == 1 ? lo.y : lo.z), di = i == 0 ? ld.x : (i == 1 ? ld.y : ld.z);
        float hi = i == 0 ? he.x : (i == 1 ? he.y : he.z);
        if (fabsf(di) < 1e-9f) {
            if (oi < -hi || oi > hi) return false;
            continue;
        }
        float t1 = (-hi - oi) / di, t2 = (hi - oi) / di;
        float s = -1.f;
        if (t1 > t2) {
            std::swap(t1, t2);
            s = 1.f;
        }
        if (t1 > tn) {
            tn = t1;
            axis = i;
            sign = s;
        }
        tf = Min(tf, t2);
        if (tn > tf) return false;
    }
    if (tf < 0.f) return false;
    t = tn > 0.f ? tn : 0.f;
    vec3 ln(0, 0, 0);
    if (axis == 0) ln.x = sign;
    else if (axis == 1) ln.y = sign;
    else ln.z = sign;
    nrm = R * ln;
    return true;
}


int fails = 0;
void check(bool c, const char* m) { if (!c) { printf("FAIL %s\n", m); fails++; } }
int main() {
    float t; vec3 n;
    // capsule along z from (0,5,0) to (0,5,2), radius 0.3; ray from origin along +y at z=1
    check(rayCapsule(vec3(0,0,1), vec3(0,1,0), vec3(0,5,0), vec3(0,5,2), 0.3f, t) && fabsf(t - 4.7f) < 1e-3f, "capsule side hit");
    check(!rayCapsule(vec3(1,0,1), vec3(0,1,0), vec3(0,5,0), vec3(0,5,2), 0.3f, t), "capsule miss");
    check(rayCapsule(vec3(0,0,2.2f), vec3(0,1,0), vec3(0,5,0), vec3(0,5,2), 0.3f, t) && t > 4.7f && t < 5.0f, "capsule cap hit");
    check(!rayCapsule(vec3(0,6,1), vec3(0,1,0), vec3(0,5,0), vec3(0,5,2), 0.3f, t), "capsule behind");
    // rotated box: center (10,0,1), rotated 45 deg around z, half (1,2,1)
    mat3 R(vec3(cosf(0.785f), sinf(0.785f), 0), vec3(-sinf(0.785f), cosf(0.785f), 0), vec3(0,0,1));
    check(rayObb(vec3(0,0,1), vec3(1,0,0), vec3(10,0,1), R, vec3(1,2,1), t, n) && t > 7.5f && t < 9.f && n.x < 0.f, "obb hit");
    check(!rayObb(vec3(0,5,1), vec3(1,0,0), vec3(10,0,1), R, vec3(1,2,1), t, n), "obb miss");
    check(!rayObb(vec3(0,0,1), vec3(-1,0,0), vec3(10,0,1), R, vec3(1,2,1), t, n), "obb behind");
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails;
}
