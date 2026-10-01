// Sagittal kinematics of the left leg over a gait clip cycle vs normative walking data (Winter; Perry).
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
#include "../tools/native_stubs.cpp"
using namespace Anim;
static float sagAngle(vec3 a, vec3 b) {   // signed angle from a to b in the y-z plane (+ = b rotated forward/up from a)
    return atan2f(a.y * b.z - a.z * b.y, a.y * b.y + a.z * b.z);
}
int main(int argc, char** argv) {
    int clip = argc > 1 ? atoi(argv[1]) : (int)CLIP_WALK;
    CharacterDesc d = randomCharacter(140u, 0);
    d.age = 0.3f;
    Skeleton sk;
    buildSkeleton(d, sk);
    const ClipInfo& ci = detail::clipInfoId(clip);
    const float norm[11][3] = {{5, 30, 0}, {18, 25, -5}, {15, 15, 5}, {8, 5, 8}, {5, -5, 10}, {10, -10, 5}, {35, -5, -15}, {60, 10, -10}, {50, 25, 0}, {20, 30, 0}, {5, 30, 0}};
    printf("clip %s: phase%%  knee(norm)  hip(norm)  ankle(norm)  pelvis z\n", ci.name);
    for (int k = 0; k <= 20; k++) {
        float ph = k * 0.05f;
        Pose p;
        detail::sampleClipId(sk, clip, ph * ci.duration, p, 0u);
        mat4 m[B_COUNT];
        computeMatrices(sk, p, m, nullptr);
        vec3 hip = m[B_THIGH_L].c[3].xyz(), knee = m[B_CALF_L].c[3].xyz(), ank = m[B_FOOT_L].c[3].xyz();
        vec3 thigh = normalize(knee - hip), shank = normalize(ank - knee);
        vec3 foot = normalize(m[B_FOOT_L].c[1].xyz());
        vec3 pelvisUp = normalize(m[B_PELVIS].c[2].xyz());
        float kneeF = -sagAngle(thigh, shank) * kRadToDeg;              // flexion +
        float hipF = sagAngle(-pelvisUp, thigh) * kRadToDeg;             // thigh forward of the pelvis vertical = flexion
        vec3 shankPerp = normalize(vec3(0, -shank.z, shank.y));          // foot at 90 deg to the shank = neutral
        float ankF = sagAngle(vec3(0, shank.y, shank.z) * 0.f + vec3(0, -shank.z * -1.f, shank.y * 1.f) * 0.f + shankPerp, foot) * kRadToDeg;
        int i = k / 2;
        if (k % 2 == 0)
            printf("  %3d%%   %6.1f (%3.0f)  %6.1f (%3.0f)  %6.1f (%3.0f)  %.3f | ankle-hip y %6.3f z %6.3f  ankle z %.3f leg %.3f\n", k * 5, kneeF, norm[i][0], hipF, norm[i][1], ankF, norm[i][2], m[B_PELVIS].c[3].z, ank.y - hip.y, ank.z - hip.z, ank.z, length(ank - hip));
    }
}
