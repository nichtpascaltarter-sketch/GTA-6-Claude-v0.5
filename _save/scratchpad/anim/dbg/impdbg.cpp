#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
int main() {
    const float dt = 1.f / 60.f;
    // brace per direction
    const vec3 dirs[4] = {vec3(0, 1, 0), vec3(0, -1, 0), vec3(-1, 0, 0), vec3(1, 0, 0)};
    for (int t = 0; t < 4; t++) {
        CharacterDesc d = randomCharacter(98u, t & 1);
        Skeleton sk; buildSkeleton(d, sk);
        Animator an; an.init(&sk, 7u); an.setCharacter(d);
        AnimInput in; in.footProbes = true;
        for (int f = 0; f < 60; f++) an.update(in, dt);
        in.fallDir = dirs[t]; in.fallBrace = 1.f;
        for (int f = 0; f < 12; f++) an.update(in, dt);
        mat4 m[B_COUNT]; computeMatrices(sk, an.pose, m, nullptr);
        vec3 c = m[B_SPINE2].c[3].xyz(), hl = m[B_HAND_L].c[3].xyz(), hr = m[B_HAND_R].c[3].xyz();
        printf("dir (%.0f %.0f): spine2 (%.2f %.2f %.2f) handL (%.2f %.2f %.2f) handR (%.2f %.2f %.2f) fallDirS (%.2f %.2f) brace %.2f\n", dirs[t].x, dirs[t].y, c.x, c.y, c.z, hl.x, hl.y, hl.z, hr.x, hr.y, hr.z, an.fallDirS.x, an.fallDirS.y, an.braceW);
    }
    // clutch per mode
    const char* wn[WOUND_COUNT] = {"", "belly", "chest", "shoulderL", "shoulderR", "thighL", "thighR"};
    for (int mode = 0; mode < 4; mode++)
        for (int w = WOUND_BELLY; w < WOUND_COUNT; w++) {
            if (mode == 3 && w >= WOUND_THIGH_L) continue;
            CharacterDesc d = randomCharacter(97u + (u32)w, w & 1);
            Skeleton sk; buildSkeleton(d, sk);
            Animator an; an.init(&sk, 7u); an.setCharacter(d);
            AnimInput in; in.footProbes = true; in.clutch = w; in.speed = mode == 1 ? 1.2f : 0.f; in.crouch = mode == 2; in.stance = mode == 3 ? 24 : 0;
            int hand = w == WOUND_SHOULDER_L ? 1 : (w == WOUND_SHOULDER_R ? 0 : (w == WOUND_THIGH_L ? 0 : 1));
            float far = 0.f, mean = 0.f; int n = 0;
            for (int f = 0; f < 150; f++) {
                an.update(in, dt);
                if (f <= 90) continue;
                quat q, qb; vec3 p, pb;
                detail::boneModel(sk, an.pose, hand ? B_HAND_R : B_HAND_L, q, p);
                vec3 palm = p + rotate(q, sk.bindLocalPos[hand ? B_FINGERS_R : B_FINGERS_L]) * 0.45f;
                detail::boneModel(sk, an.pose, an.skinWB[w], qb, pb);
                vec3 tgt = pb + rotate(qb, an.skinW[w]) + rotate(qb, detail::woundNormal(w)) * (sk.boneRadius[hand ? B_HAND_R : B_HAND_L] * 0.75f + 0.004f);
                float e = length(palm - tgt); far = Max(far, e); mean += e; n++;
            }
            printf("mode %d %-9s: max %.3f mean %.3f (clutchW %.2f cur %d side %d)\n", mode, wn[w], far, mean / n, an.clutchW, an.clutchCur, an.clutchSide);
        }
}
