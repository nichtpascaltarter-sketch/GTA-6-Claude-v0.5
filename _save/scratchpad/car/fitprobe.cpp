// scratch: the seated pose's segments and the headroom slouch's efficiency
#define PREVIEW_NO_MAIN
#include "../../../../../../home/user/GTA-6-Claude-v0.5/tests/anim/preview.cpp"
#include <cstdlib>
using namespace Anim::detail;
static float coef[7] = {0.12f, 0.22f, 0.06f, 0.f, -0.05f, -0.13f, -0.09f};
static void applyK(const Skeleton& sk, const Pose& src, Pose& x, float k) {
    const vec3 F(0, 1, 0), R = normalize(cross(F, vec3(0, 0, 1)));
    const float s = Max(sk.boneLength[B_THIGH_L] / 0.43f, 0.5f);
    x = src;
    x.rootOffset = x.rootOffset + F * (coef[0] * s * k);
    quat q0; vec3 a0, b0;
    boneModel(sk, x, B_THIGH_L, q0, a0); boneModel(sk, x, B_THIGH_R, q0, b0);
    rotateModel(sk, x, B_PELVIS, quatAxisAngle(R, coef[1] * k));
    if (getenv("HIPPIVOT")) {
        quat q1; vec3 a1, b1;
        boneModel(sk, x, B_THIGH_L, q1, a1); boneModel(sk, x, B_THIGH_R, q1, b1);
        x.rootOffset = x.rootOffset + (a0 + b0 - a1 - b1) * 0.5f;
    }
    rotateModel(sk, x, B_SPINE1, quatAxisAngle(R, coef[2] * k));
    rotateModel(sk, x, B_SPINE2, quatAxisAngle(R, coef[3] * k));
    rotateModel(sk, x, B_CHEST, quatAxisAngle(R, coef[4] * k));
    rotateModel(sk, x, B_NECK, quatAxisAngle(R, coef[5] * k));
    rotateModel(sk, x, B_HEAD, quatAxisAngle(R, coef[6] * k));
}
int main(int argc, char** argv) {
    float H = argc > 1 ? atof(argv[1]) : 1.76f;
    if (argc > 8) for (int i = 0; i < 7; i++) coef[i] = atof(argv[2 + i]);
    CharacterDesc d = randomCharacter(1000u + 2u * 31u, 0);
    d.height = H;
    d.hat = -1;
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 77u);
    an.setCharacter(d);
    for (int f = 0; f < 60; f++) {
        AnimInput in;
        in.stance = getenv("STANCE") ? atoi(getenv("STANCE")) : 1;
        an.update(in, 1.f / 60.f);
    }
    const Pose P0 = an.pose;
    static const Bone bs[] = {B_PELVIS, B_SPINE1, B_SPINE2, B_CHEST, B_NECK, B_HEAD, B_THIGH_L, B_CALF_L, B_FOOT_L};
    static const char* nm[] = {"pelvis", "spine1", "spine2", "chest", "neck", "head", "thighL", "calfL", "footL"};
    printf("crownH %.3f\n", an.crownH);
    {
        quat qh; vec3 hp;
        boneModel(sk, P0, B_PELVIS, qh, hp);
        vec3 a = rotate(qh, vec3(0, 1, 0)), b = rotate(qh, vec3(0, 0, 1)), c = rotate(qh, vec3(1, 0, 0));
        printf("pelvis local +Y -> (%.2f %.2f %.2f)  +Z -> (%.2f %.2f %.2f)  +X -> (%.2f %.2f %.2f)\n", a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
        Pose bind;
        boneModel(sk, bind, B_PELVIS, qh, hp);
        a = rotate(qh, vec3(0, 1, 0));
        printf("bind pelvis +Y -> (%.2f %.2f %.2f)\n", a.x, a.y, a.z);
    }
    vec3 prev;
    for (int i = 0; i < 9; i++) {
        quat q; vec3 p;
        boneModel(sk, P0, bs[i], q, p);
        vec3 up = rotate(q, vec3(0, 0, 1)), fw = rotate(q, vec3(0, 1, 0));
        printf("  %-7s y %+.3f z %.3f", nm[i], p.y, p.z);
        if (i > 0 && i <= 5) { vec3 dd = p - prev; printf("  seg from prev: len %.3f, lean back %+.1f deg", length(dd), atan2f(-dd.y, dd.z) * 57.3f); }
        printf("  boneUp lean %+.1f\n", atan2f(-up.y, up.z) * 57.3f);
        prev = p;
    }
    auto crown = [&](const Pose& x) { quat q; vec3 h; boneModel(sk, x, B_HEAD, q, h); return h + rotate(q, vec3(0, 0, an.crownH)); };
    vec3 c0 = crown(P0);
    printf("crown y %+.3f z %.3f\n", c0.y, c0.z);
    auto evalK = [&](float k, float out[6]) {
        Pose x;
        applyK(sk, P0, x, k);
        vec3 c = crown(x);
        quat q; vec3 hp; boneModel(sk, x, B_HEAD, q, hp);
        quat q0; vec3 hp0; boneModel(sk, P0, B_HEAD, q0, hp0);
        vec3 fw = rotate(q, vec3(0, 1, 0)), fw0 = rotate(q0, vec3(0, 1, 0));
        quat qa; vec3 a1, a0, b1, b0;
        boneModel(sk, x, B_CHEST, qa, a1); boneModel(sk, P0, B_CHEST, qa, a0);
        boneModel(sk, x, B_UPPERARM_L, qa, b1); boneModel(sk, P0, B_UPPERARM_L, qa, b0);
        out[0] = c0.z - c.z; out[1] = c.y - c0.y; out[2] = x.rootOffset.y - P0.rootOffset.y;
        out[3] = a1.y - a0.y; out[4] = b1.y - b0.y; out[5] = (atan2f(fw.z, fw.y) - atan2f(fw0.z, fw0.y)) * 57.3f;
    };
    for (float want : {0.04f, 0.06f, 0.08f, 0.10f, 0.12f}) {
        float lo = 0.f, hi = 4.f, o[6];
        evalK(hi, o);
        if (o[0] < want) { printf("drop %.2f: unreachable (max %.3f)\n", want, o[0]); continue; }
        for (int it = 0; it < 40; it++) { float m = 0.5f * (lo + hi); evalK(m, o); if (o[0] < want) lo = m; else hi = m; }
        evalK(hi, o);
        printf("drop %.2f: k %.2f  head pitch %+5.1f  crown dy %+.3f  hips %+.3f  chest dy %+.3f  shoulder dy %+.3f\n", want, hi, o[5], o[1], o[2], o[3], o[4]);
    }
    if (getenv("SEARCH")) {
        u32 rs = 12345u;
        auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) * (1.f / 16777216.f); };
        float best = 1e9f, bc[7];
        const float want = getenv("WANT") ? (float)atof(getenv("WANT")) : 0.08f;
        for (int trial = 0; trial < 30000; trial++) {
            float cc[7];
            cc[0] = rnd() * (getenv("SLIDEMAX") ? (float)atof(getenv("SLIDEMAX")) : 0.06f);            // hips fwd
            cc[1] = rnd() * 0.3f;             // pelvis (back)
            for (int j = 2; j < 7; j++) cc[j] = -0.25f + rnd() * 0.33f;
            for (int j = 0; j < 7; j++) coef[j] = cc[j];
            float lo = 0.f, hi = 1.f, o[6];
            evalK(hi, o);
            if (o[0] < want) continue;
            for (int it = 0; it < 30; it++) { float m = 0.5f * (lo + hi); evalK(m, o); if (o[0] < want) lo = m; else hi = m; }
            evalK(hi, o);
            float joint = 0.f;
            for (int j = 1; j < 7; j++) joint += (cc[j] * hi * 57.3f) * (cc[j] * hi * 57.3f);
            // the head: level-ish (pitch), staying over the seat (crown dy within +-4 cm); the shoulders not pushed back
            // into the backrest (relative to the slid hips: o[4] - o[2] is how far back of the hips' slide)
            float back = Max(-(o[4]) - 0.04f, 0.f);
            float cost = Max(fabsf(o[5]) - 6.f, 0.f) * 1.f + Max(fabsf(o[1]) - 0.04f, 0.f) * 300.f + Max(o[2] - (getenv("SLIDEOK") ? (float)atof(getenv("SLIDEOK")) : 0.05f), 0.f) * 200.f + back * 300.f +
                         Max(o[3], 0.f) * 50.f + joint * 0.004f;
            if (cost < best) {
                best = cost;
                for (int j = 0; j < 7; j++) bc[j] = cc[j] * hi;   // normalized so that k = 1 gives the drop
                printf("cost %.2f: k1 coef %.3f %.3f %.3f %.3f %.3f %.3f %.3f | pitch %+.1f crown dy %+.3f hips %.3f chest %+.3f shoulder %+.3f\n", cost, bc[0], bc[1], bc[2], bc[3], bc[4], bc[5], bc[6], o[5], o[1], o[2], o[3], o[4]);
            }
        }
    }
}
