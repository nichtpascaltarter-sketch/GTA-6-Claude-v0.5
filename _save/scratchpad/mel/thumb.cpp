#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include <cstdio>
using namespace Anim;
// Thumb / finger joint positions in the right hand's frame (F along the fingers, Y thumb side, P palm normal), cm
int main(int argc, char** argv) {
    CharacterDesc d = randomCharacter(11, 0);
    d.gender = MALE; d.height = 1.78f;
    Skeleton sk;
    buildSkeleton(d, sk);
    vec3 fing = normalize(sk.bindLocalPos[B_FINGERS_R]);
    vec3 pn = normalize(cross(vec3(0, 1, 0), fing));
    vec3 td = normalize(fing * 0.62f + vec3(0, 1, 0) * 0.66f + pn * 0.42f);
    float tv[] = {0.f, 0.2f, 0.55f, 0.85f, 1.f};
    float fv[] = {0.f, 0.35f, 0.9f, 0.9f, 1.f};
    for (int k = 0; k < 5; k++) {
        Pose p;
        for (int b = 0; b < B_COUNT; b++) p.rot[b] = quat();
        p.rootOffset = vec3(0);
        p.rot[B_FINGERS_R] = quatAxisAngle(normalize(cross(fing, pn)), fv[k] * 1.45f);
        p.rot[B_THUMB_R] = quatAxisAngle(normalize(cross(td, pn)), tv[k] * 0.9f);
        mat4 m[B_COUNT];
        computeMatrices(sk, p, m, nullptr);
        vec3 w = m[B_HAND_R].c[3].xyz();
        auto hf = [&](vec3 q) { vec3 r = q - w; return vec3(dot(r, fing), r.y, dot(r, pn)) * 100.f; };
        int t0 = phalanxBone(true, 4, 0);
        // tip: distal bone origin + its length along its local direction (bind dir of tip from IP)
        vec3 tipL = normalize(sk.bindLocalPos[t0 + 1]);
        (void)tipL;
        printf("thumb %.2f fingers %.2f\n", tv[k], fv[k]);
        vec3 cmc = hf(m[t0].c[3].xyz()), mcp = hf(m[t0 + 1].c[3].xyz()), ip = hf(m[t0 + 2].c[3].xyz());
        // tip from the skeleton: IP + rotated (bind tip - bind IP)
        detail::BodyDims D; detail::computeDims(d, D);
        vec3 tipB = D.fingTip[1][4] - D.J[t0 + 2];
        mat3 R(m[t0 + 2].c[0].xyz(), m[t0 + 2].c[1].xyz(), m[t0 + 2].c[2].xyz());
        vec3 tip = hf(m[t0 + 2].c[3].xyz() + R * tipB);
        printf("  CMC (%.1f %.1f %.1f) MCP (%.1f %.1f %.1f) IP (%.1f %.1f %.1f) tip (%.1f %.1f %.1f)\n", cmc.x, cmc.y, cmc.z, mcp.x, mcp.y, mcp.z, ip.x, ip.y, ip.z, tip.x, tip.y, tip.z);
        for (int f = 0; f < 4; f++) {
            int b0 = phalanxBone(true, f, 0);
            vec3 tb = D.fingTip[1][f] - D.J[b0 + 2];
            mat3 R3(m[b0 + 2].c[0].xyz(), m[b0 + 2].c[1].xyz(), m[b0 + 2].c[2].xyz());
            vec3 a = hf(m[b0].c[3].xyz()), b = hf(m[b0 + 1].c[3].xyz()), c = hf(m[b0 + 2].c[3].xyz()), e = hf(m[b0 + 2].c[3].xyz() + R3 * tb);
            printf("  f%d MCP (%.1f %.1f %.1f) PIP (%.1f %.1f %.1f) DIP (%.1f %.1f %.1f) tip (%.1f %.1f %.1f)\n", f, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, e.x, e.y, e.z);
        }
    }
    printf("grip centre (%.1f 0 %.1f)\n", detail::kGripAlong * length(sk.bindLocalPos[B_FINGERS_R]) * 100.f, detail::kGripPalm * length(sk.bindLocalPos[B_FINGERS_R]) * 100.f);
}
