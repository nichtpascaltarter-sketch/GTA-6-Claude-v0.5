// Native unit tests for the character module: skeleton/mesh validity, bind-pose skinning, clip continuity and ground
// contact, IK accuracy, animator robustness and timing.
// Build: g++ -O2 -std=c++17 -I src tests/anim/anim_test.cpp -o /tmp/anim_test && /tmp/anim_test
#include "../../src/core/math.cpp"
#include "../../src/render/mesh.cpp"
#include "../../src/anim/anim_all.cpp"
#include "../../tools/native_stubs.cpp"

using namespace Anim;

namespace animtest {

int gFail = 0;
#undef CHECK
#define CHECK(cond, ...)                          \
    do {                                          \
        if (!(cond)) {                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                  \
            printf("\n");                         \
            gFail++;                              \
        }                                         \
    } while (0)

float quatAngle(quat a, quat b) {
    float d = fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.f * acosf(Min(1.f, d));
}

vec3 jointPos(const Skeleton& sk, const Pose& p, int b) {
    mat4 m[B_COUNT];
    computeMatrices(sk, p, m, nullptr);
    return m[b].c[3].xyz();
}

void testMesh() {
    double tTotal = 0;
    int triTotal = 0, n = 0, triMin = 1 << 30, triMax = 0;
    for (int role = 0; role < 7; role++) {
        for (int k = 0; k < 4; k++) {
            u32 seed = 1000 + role * 37 + k * 101;
            CharacterDesc d = randomCharacter(seed, role);
            Skeleton sk;
            buildSkeleton(d, sk);
            SkinnedMeshData m;
            double t0 = TimeSeconds();
            buildCharacterMesh(d, sk, m);
            double t1 = TimeSeconds();
            tTotal += t1 - t0;
            int tris = (int)m.indices.size() / 3;
            triTotal += tris;
            triMin = Min(triMin, tris);
            triMax = Max(triMax, tris);
            n++;
            bool okW = true, okB = true, okP = true;
            for (const VtxSkinned& v : m.verts) {
                int sum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
                if (sum != 255) okW = false;
                for (int j = 0; j < 4; j++)
                    if (v.bones[j] >= B_COUNT) okB = false;
                if (!(fabsf(v.pos.x) < 3.f && fabsf(v.pos.y) < 3.f && v.pos.z > -0.1f && v.pos.z < 2.5f)) okP = false;
            }
            CHECK(okW, "weights must sum to 255 (seed %u role %d)", seed, role);
            CHECK(okB, "bone index out of range (seed %u role %d)", seed, role);
            CHECK(okP, "vertex outside bounds (seed %u role %d)", seed, role);
            bool okI = true;
            for (u32 i : m.indices)
                if (i >= m.verts.size()) okI = false;
            CHECK(okI, "index out of range");
            CHECK(tris >= 6000 && tris <= 26000, "triangle count %d out of budget (role %d)", tris, role);
        }
    }
    printf("mesh: %d characters, tris avg %d (min %d, max %d), build avg %.1f ms\n", n, triTotal / n, triMin, triMax, tTotal / n * 1000.0);
}

void testBindPose() {
    CharacterDesc d = randomCharacter(77, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Pose p;
    for (int b = 0; b < B_COUNT; b++) p.rot[b] = sk.bindLocalRot[b];
    p.rootOffset = vec3(0);
    mat4 ms[B_COUNT], skm[B_COUNT];
    computeMatrices(sk, p, ms, skm);
    float err = 0;
    for (int b = 0; b < B_COUNT; b++)
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) err = Max(err, fabsf(skm[b].c[c][r] - (c == r ? 1.f : 0.f)));
    CHECK(err < 1e-4f, "bind pose skinning matrices must be identity (err %g)", err);
    for (int b = 1; b < B_COUNT; b++) CHECK(sk.parent[b] >= 0 && sk.parent[b] < b, "parent order bone %d", b);
    printf("bind pose: max skinning deviation %.2g\n", err);
}

// computeMatrices -> poseFromModelSpace -> computeMatrices must reproduce the model-space matrices.
void testPoseRoundTrip() {
    CharacterDesc d = randomCharacter(8, 3);
    Skeleton sk;
    buildSkeleton(d, sk);
    float worst = 0.f;
    const Clip cs[] = {CLIP_RUN, CLIP_DEATH_BACK, CLIP_SIT_DRIVE, CLIP_THROW, CLIP_GET_UP_FRONT};
    for (Clip c : cs) {
        Pose p, q;
        sampleClip(sk, c, 0.37f, p, 5);
        mat4 a[B_COUNT], b[B_COUNT];
        computeMatrices(sk, p, a, nullptr);
        poseFromModelSpace(sk, a, q);
        computeMatrices(sk, q, b, nullptr);
        for (int k = 0; k < B_COUNT; k++)
            for (int c4 = 0; c4 < 4; c4++) worst = Max(worst, length(a[k].c[c4].xyz() - b[k].c[c4].xyz()));
    }
    CHECK(worst < 1e-4f, "pose round trip error %g", worst);
    printf("poseFromModelSpace: round trip error %.2g\n", worst);
    // blendFrom keeps its crossfade when the get-up action starts
    Animator an;
    an.init(&sk, 3);
    Pose lying;
    sampleClip(sk, CLIP_DEATH_FRONT, 10.f, lying);
    an.blendFrom(lying, 0.4f);
    AnimInput in;
    in.action = CLIP_GET_UP_BACK;
    an.update(in, 1.f / 60.f);
    CHECK(an.snapW > 0.9f, "blendFrom crossfade dropped (%.2f)", an.snapW);
}

void testIK() {
    CharacterDesc d = randomCharacter(5, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Pose p;
    sampleClip(sk, CLIP_IDLE, 0.f, p);
    float worst = 0;
    Rng rng(9);
    for (int i = 0; i < 200; i++) {
        Pose q = p;
        // random reachable targets (inside 95% of the chain length)
        float legL = length(sk.bindLocalPos[B_CALF_L]) + length(sk.bindLocalPos[B_FOOT_L]);
        float armL = length(sk.bindLocalPos[B_FOREARM_R]) + length(sk.bindLocalPos[B_HAND_R]);
        vec3 hip = jointPos(sk, q, B_THIGH_L);
        vec3 d = normalize(vec3(rng.range(-0.4f, 0.4f), rng.range(-0.5f, 0.6f), -1.f)) * (legL * rng.range(0.55f, 0.95f));
        vec3 tgt = hip + d;
        solveTwoBoneIK(sk, q, B_THIGH_L, B_CALF_L, B_FOOT_L, tgt, hip + vec3(0, 1, -0.4f), 1.f);
        worst = Max(worst, length(jointPos(sk, q, B_FOOT_L) - tgt));
        vec3 sh = jointPos(sk, q, B_UPPERARM_R);
        vec3 da = normalize(vec3(rng.range(-0.3f, 1.f), rng.range(0.f, 1.f), rng.range(-1.f, 0.6f))) * (armL * rng.range(0.4f, 0.95f));
        vec3 ht = sh + da;
        solveTwoBoneIK(sk, q, B_UPPERARM_R, B_FOREARM_R, B_HAND_R, ht, sh + vec3(0.5f, -0.5f, -0.5f), 1.f);
        worst = Max(worst, length(jointPos(sk, q, B_HAND_R) - ht));
    }
    CHECK(worst < 0.002f, "IK error %.4f m", worst);
    printf("IK: worst end-effector error %.5f m\n", worst);
}

void testClips() {
    const u32 seeds[3] = {3, 44, 901};
    float worstLoop = 0, worstStep = 0;
    int worstLoopClip = -1, worstStepClip = -1;
    for (u32 si = 0; si < 3; si++) {
        CharacterDesc d = randomCharacter(seeds[si], (int)si);
        if (si == 1) d.gender = FEMALE;
        Skeleton sk;
        buildSkeleton(d, sk);
        for (int c = 0; c < CLIP_COUNT; c++) {
            const ClipInfo& ci = clipInfo((Clip)c);
            CHECK(ci.duration > 0.05f && ci.name && ci.name[0], "clip %d info", c);
            Pose a, b;
            int steps = Max(8, (int)(ci.duration * 60.f));
            for (int i = 0; i <= steps; i++) {
                float t = ci.duration * i / steps;
                sampleClip(sk, (Clip)c, t, b, seeds[si]);
                bool finite = true;
                for (int k = 0; k < B_COUNT; k++) {
                    float l = sqrtf(b.rot[k].x * b.rot[k].x + b.rot[k].y * b.rot[k].y + b.rot[k].z * b.rot[k].z + b.rot[k].w * b.rot[k].w);
                    if (!(fabsf(l - 1.f) < 1e-3f)) finite = false;
                }
                CHECK(finite, "clip %s non unit quaternion at t=%.2f", ci.name, t);
                if (i > 0) {
                    float dt = ci.duration / steps;
                    for (int k = 0; k < B_COUNT; k++) {
                        float w = quatAngle(a.rot[k], b.rot[k]) / dt;
                        if (w > worstStep) { worstStep = w; worstStepClip = c; }
                        if (w > 30.f && si == 0) printf("  fast joint: clip %s bone %d t=%.3f %.1f rad/s\n", ci.name, k, t, w);
                    }
                }
                a = b;
            }
            if (ci.loop) {
                Pose e, s0;
                sampleClip(sk, (Clip)c, ci.duration - 1e-4f, e);
                sampleClip(sk, (Clip)c, 0.f, s0);
                float m = 0;
                for (int k = 0; k < B_COUNT; k++) m = Max(m, quatAngle(e.rot[k], s0.rot[k]));
                m = Max(m, length(e.rootOffset - s0.rootOffset) * 10.f);
                if (m > worstLoop) { worstLoop = m; worstLoopClip = c; }
                CHECK(m < 0.05f, "loop seam of %s: %.3f", ci.name, m);
            }
        }
    }
    printf("clips: worst loop seam %.4f (%s), fastest joint %.1f rad/s (%s)\n", worstLoop, worstLoopClip >= 0 ? clipInfo((Clip)worstLoopClip).name : "-",
           worstStep, worstStepClip >= 0 ? clipInfo((Clip)worstStepClip).name : "-");
}

// Ground contact and foot sliding for locomotion clips.
void testGait() {
    CharacterDesc d = randomCharacter(12, 0);
    d.gender = MALE;
    Skeleton sk;
    buildSkeleton(d, sk);
    const Clip gaits[] = {CLIP_WALK, CLIP_JOG, CLIP_RUN, CLIP_SPRINT, CLIP_WALK_BACK, CLIP_STRAFE_L, CLIP_STRAFE_R, CLIP_CROUCH_WALK};
    for (Clip c : gaits) {
        const ClipInfo& ci = clipInfo(c);
        int n = 120;
        float minZ = 1e9f, maxZ = -1e9f, slide = 0, slideN = 0;
        vec3 prev[2];
        for (int i = 0; i <= n; i++) {
            float t = ci.duration * i / n;
            Pose p;
            sampleClip(sk, c, t, p);
            mat4 m[B_COUNT];
            computeMatrices(sk, p, m, nullptr);
            for (int s = 0; s < 2; s++) {
                vec3 toe = m[s ? B_TOE_R : B_TOE_L].c[3].xyz();
                vec3 ank = m[s ? B_FOOT_R : B_FOOT_L].c[3].xyz();
                float z = Min(toe.z, ank.z - 0.07f);
                minZ = Min(minZ, z);
                maxZ = Max(maxZ, ank.z);
                float bindAnk = sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[s ? B_THIGH_R : B_THIGH_L].z + sk.bindLocalPos[s ? B_CALF_R : B_CALF_L].z +
                                sk.bindLocalPos[s ? B_FOOT_R : B_FOOT_L].z;
                if (i > 0 && fabsf(ank.z - bindAnk) < 0.006f && fabsf(prev[s].z - bindAnk) < 0.006f && toe.z < bindAnk) {
                    // stance: in world space the planted foot should not move: model velocity ~ -speed
                    vec3 v = (ank - prev[s]) / (ci.duration / n);
                    float sl = fabsf(v.y + ci.speed * (c == CLIP_WALK_BACK ? -1.f : (c == CLIP_STRAFE_L || c == CLIP_STRAFE_R ? 0.f : 1.f)));
                    if (c == CLIP_STRAFE_L) sl = fabsf(v.x - ci.speed);
                    if (c == CLIP_STRAFE_R) sl = fabsf(v.x + ci.speed);
                    slide += sl;
                    slideN += 1;
                }
                prev[s] = ank;
            }
        }
        printf("gait %-12s speed %.1f: lowest foot point %.3f, max ankle %.3f, mean stance slide %.2f m/s\n", ci.name, ci.speed, minZ, maxZ,
               slideN > 0 ? slide / slideN : -1.f);
    }
}

void testPoses() {
    for (int g = 0; g < 2; g++) {
        CharacterDesc d = randomCharacter(21 + g, 0);
        d.gender = g ? FEMALE : MALE;
        d.height = g ? 1.64f : 1.8f;
        Skeleton sk;
        buildSkeleton(d, sk);
        Pose p;
        mat4 m[B_COUNT];
        const Clip seated[] = {CLIP_SIT_DRIVE, CLIP_SIT_PASSENGER};
        for (Clip c : seated) {
            sampleClip(sk, c, 0.f, p);
            computeMatrices(sk, p, m, nullptr);
            vec3 hip = (m[B_THIGH_L].c[3].xyz() + m[B_THIGH_R].c[3].xyz()) * 0.5f;
            printf("%s %s: hip (%.2f %.2f %.2f)\n", g ? "female" : "male", clipInfo(c).name, hip.x, hip.y, hip.z);
            CHECK(fabsf(hip.z - 0.5f) < 0.06f, "seated hip height %.2f", hip.z);
            if (c == CLIP_SIT_DRIVE) {
                vec3 hl = m[B_HAND_L].c[3].xyz(), hr = m[B_HAND_R].c[3].xyz();
                vec3 ch = m[B_CHEST].c[3].xyz(), sh = m[B_UPPERARM_R].c[3].xyz();
                printf("   hands L (%.2f %.2f %.2f) R (%.2f %.2f %.2f); chest y %.2f, shoulder z %.2f\n", hl.x, hl.y, hl.z, hr.x, hr.y, hr.z, ch.y, sh.z);
            }
        }
        const Clip deaths[] = {CLIP_DEATH_FRONT, CLIP_DEATH_BACK, CLIP_GET_UP_FRONT, CLIP_GET_UP_BACK, CLIP_SUNBATHE};
        for (Clip c : deaths) {
            float t = (c == CLIP_GET_UP_FRONT || c == CLIP_GET_UP_BACK) ? 0.f : clipInfo(c).duration;
            sampleClip(sk, c, t, p);
            computeMatrices(sk, p, m, nullptr);
            vec3 pel = m[B_PELVIS].c[3].xyz(), head = m[B_HEAD].c[3].xyz(), ch = m[B_CHEST].c[3].xyz();
            // face direction: head's +Y axis
            vec3 face = m[B_HEAD].c[1].xyz();
            float lowest = 1e9f;
            int lowB = 0;
            for (int b = 0; b < B_COUNT; b++)
                if (m[b].c[3].z < lowest) { lowest = m[b].c[3].z; lowB = b; }
            if (lowest < -0.02f) printf("   lowest bone %d\n", lowB);
            printf("%s %-13s t=%.2f: pelvis (%.2f %.2f %.2f) chest z %.2f head (%.2f %.2f %.2f) face (%.2f %.2f %.2f) lowest joint %.2f\n",
                   g ? "female" : "male", clipInfo(c).name, t, pel.x, pel.y, pel.z, ch.z, head.x, head.y, head.z, face.x, face.y, face.z, lowest);
            CHECK(pel.z < 0.3f && head.z < 0.35f, "%s should lie on the ground", clipInfo(c).name);
            CHECK(lowest > -0.03f, "%s penetrates the ground (%.2f)", clipInfo(c).name, lowest);
        }
    }
}

void testAnimator() {
    CharacterDesc d = randomCharacter(31, 1);
    Skeleton sk;
    buildSkeleton(d, sk);
    Animator an;
    an.init(&sk, 1234);
    AnimInput in;
    Rng rng(4);
    mat4 ms[B_COUNT], skm[B_COUNT];
    const int N = 20000;
    double t0 = TimeSeconds();
    bool finite = true;
    for (int i = 0; i < N; i++) {
        float u = (float)i / N;
        in.speed = 3.5f + 3.5f * sinf(u * 40.f);
        in.localMoveDir = normalize(vec2(sinf(u * 13.f), cosf(u * 7.f)));
        in.turnRate = sinf(u * 23.f);
        in.aiming = sinf(u * 31.f) > 0.3f;
        in.firing = in.aiming && sinf(u * 97.f) > 0.f;
        in.weaponKind = (i / 700) % 5;
        in.aimPitch = sinf(u * 11.f) * 0.8f;
        in.crouch = sinf(u * 5.f) > 0.7f;
        in.inAir = sinf(u * 17.f) > 0.9f;
        in.swimming = sinf(u * 3.f) > 0.95f;
        in.reloading = (i / 300) % 7 == 3;
        in.stance = (i / 1500) % 21;
        in.meleeKind = (i / 900) % 3;
        in.viseme = (i / 7) % 16 - 1;
        in.visemeNext = (i / 5) % 15;
        in.visemeWeight = 0.8f;
        in.visemeBlend = (float)(i % 7) / 7.f;
        in.speaking = (i / 400) % 3 == 1;
        in.listening = (i / 400) % 3 == 2;
        in.beat = Max(0.f, sinf(u * 300.f));
        in.phoneCall = (i / 1100) % 4 == 1;
        in.action = (i % 97 == 0) ? (int)(rng.next() % CLIP_COUNT) : -1;
        in.groundOffsetL = 0.1f * sinf(u * 50.f);
        in.groundOffsetR = -0.1f * sinf(u * 43.f);
        an.update(in, 1.f / 60.f);
        for (int b = 0; b < B_COUNT; b++) {
            const quat& q = an.pose.rot[b];
            if (!(q.x == q.x && q.w == q.w)) finite = false;
        }
    }
    double t1 = TimeSeconds();
    CHECK(finite, "animator produced NaNs");
    // typical walking update cost
    AnimInput w;
    w.speed = 1.6f;
    Animator a2;
    a2.init(&sk, 99);
    double t2 = TimeSeconds();
    for (int i = 0; i < N; i++) a2.update(w, 1.f / 60.f);
    double t3 = TimeSeconds();
    for (int i = 0; i < N; i++) computeMatrices(sk, a2.pose, ms, skm);
    double t4 = TimeSeconds();
    Animator a3;
    a3.init(&sk, 98);
    double t5 = TimeSeconds();
    for (int i = 0; i < N; i++) a3.update(w, 1.f / 60.f, true);
    double t6 = TimeSeconds();
    printf("animator: stress update %.2f us, walking update %.2f us (cheap %.2f us), computeMatrices %.2f us\n", (t1 - t0) / N * 1e6,
           (t3 - t2) / N * 1e6, (t6 - t5) / N * 1e6, (t4 - t3) / N * 1e6);
}

// LODs: triangle budgets, valid skinning, same silhouette (bounds) as the full mesh.
void testLods() {
    double tb = 0.0;
    for (u32 k = 0; k < 6; k++) {
        CharacterDesc d = randomCharacter(5000u + k * 7919u, (int)(k % 7));
        Skeleton sk;
        buildSkeleton(d, sk);
        SkinnedMeshData L[3];
        double t0 = TimeSeconds();
        buildCharacterMeshLods(d, sk, L, 3);
        tb += TimeSeconds() - t0;
        int t1 = (int)L[1].indices.size() / 3, t2 = (int)L[2].indices.size() / 3;
        CHECK(t1 >= 3500 && t1 <= 5200, "LOD1 triangles %d", t1);
        CHECK(t2 >= 1000 && t2 <= 1900, "LOD2 triangles %d", t2);
        for (int l = 1; l < 3; l++) {
            bool okW = true, okB = true;
            for (const VtxSkinned& v : L[l].verts) {
                int sum = 0;
                for (int i = 0; i < 4; i++) {
                    sum += v.weights[i];
                    if (v.bones[i] >= B_COUNT) okB = false;
                }
                if (sum != 255) okW = false;
            }
            for (u32 i : L[l].indices) okB = okB && i < L[l].verts.size();
            CHECK(okW && okB, "LOD%d skinning / indices invalid", l);
            vec3 dmin = L[l].bounds.mn - L[0].bounds.mn, dmax = L[l].bounds.mx - L[0].bounds.mx;
            float e = Max(Max(fabsf(dmin.x), Max(fabsf(dmin.y), fabsf(dmin.z))), Max(fabsf(dmax.x), Max(fabsf(dmax.y), fabsf(dmax.z))));
            CHECK(e < (l == 1 ? 0.03f : 0.09f), "LOD%d bounds differ by %.3f m", l, e);   // LOD2 has paddle hands (no fingers)
        }
    }
    printf("lods: build (full + LOD1 + LOD2) avg %.1f ms\n", tb * 1000.0 / 6);
}

// Driving: the hands must stay on the steering wheel rim (absolute interior geometry) for any character size.
void testDriving() {
    const vec3 wc(0.f, 0.5f, 0.9f), wn(0.f, -0.912f, 0.411f);
    float worst = 0.f;
    for (u32 sd = 1; sd <= 12; sd++) {
        CharacterDesc d = randomCharacter(sd * 131u, (int)(sd % 7));
        Skeleton sk;
        buildSkeleton(d, sk);
        Animator an;
        an.init(&sk, sd);
        AnimInput in;
        in.stance = 1;
        for (int f = 0; f < 240; f++) {
            in.localMoveDir = vec2(sinf(f * 0.05f), 1.f);
            an.update(in, 1.f / 60.f);
            if (f < 60) continue;   // stance crossfade
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            for (int s = 0; s < 2; s++) {
                vec3 h = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
                vec3 r = h - wc;
                // distance of the wrist from the rim torus (radius 0.185 + 0.03 wrist offset), in the wheel plane
                float along = dot(r, wn);
                vec3 inPlane = r - wn * along;
                float e = fabsf(length(inPlane) - 0.215f);
                worst = Max(worst, Max(e, fabsf(along + 0.02f)));
            }
        }
        vec3 hipL, hipR;
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        {
            float armL = length(sk.bindLocalPos[B_FOREARM_R]) + length(sk.bindLocalPos[B_HAND_R]);
            vec3 sh = m[B_UPPERARM_R].c[3].xyz(), hd = m[B_HAND_R].c[3].xyz();
            printf("  driver %u: h %.2f arm %.3f shoulder (%.2f %.2f %.2f) hand (%.2f %.2f %.2f) reach %.3f\n", sd, d.height, armL, sh.x, sh.y, sh.z, hd.x, hd.y,
                   hd.z, length(hd - sh));
        }
        hipL = m[B_THIGH_L].c[3].xyz();
        hipR = m[B_THIGH_R].c[3].xyz();
        CHECK(fabsf((hipL.z + hipR.z) * 0.5f - 0.5f) < 0.03f, "driver hips at %.2f (want 0.5)", (hipL.z + hipR.z) * 0.5f);
    }
    printf("driving: worst wrist distance from the wheel rim path %.3f m\n", worst);
    CHECK(worst < 0.1f, "hands off the steering wheel (%.3f m)", worst);
}

// Melee: event times, root motion, the synced takedown contact, the two-handed bat grip and the held knockout.
void testMelee() {
    for (int c = 0; c < CLIP_COUNT; c++) {
        float e = clipEventTime((Clip)c);
        if (e >= 0.f) CHECK(e < clipInfo((Clip)c).duration, "event of %s outside the clip (%.2f)", clipInfo((Clip)c).name, e);
    }
    CharacterDesc d0;
    Skeleton sk;
    buildSkeleton(d0, sk);
    // dodges: 1.2 m of root motion (scaled), and the in-place pose ends back in the guard over the root
    const Clip dodges[3] = {CLIP_DODGE_BACK, CLIP_DODGE_L, CLIP_DODGE_R};
    const vec3 dirs[3] = {vec3(0, -1, 0), vec3(-1, 0, 0), vec3(1, 0, 0)};
    for (int i = 0; i < 3; i++) {
        vec3 rm = clipRootMotion(sk, dodges[i], clipInfo(dodges[i]).duration);
        CHECK(dot(rm, dirs[i]) > 1.0f && dot(rm, dirs[i]) < 1.4f, "%s root motion %.2f", clipInfo(dodges[i]).name, dot(rm, dirs[i]));
        Pose a, b;
        sampleClip(sk, dodges[i], 0.f, a);
        sampleClip(sk, dodges[i], clipInfo(dodges[i]).duration, b);
        vec3 pa = jointPos(sk, a, B_PELVIS), pb = jointPos(sk, b, B_PELVIS);
        CHECK(length(pa - pb) < 0.03f, "%s does not end in place (%.3f)", clipInfo(dodges[i]).name, length(pa - pb));
    }
    CHECK(length(clipRootMotion(sk, CLIP_HOOK, 0.5f)) < 1e-6f, "strikes play in place");
    // takedown: at the grab the attacker's right forearm is at the victim's throat (attacker 0.55 m behind)
    {
        Pose v, a;
        float t = clipEventTime(CLIP_TAKEDOWN_ATTACKER) + 0.1f;
        sampleClip(sk, CLIP_TAKEDOWN_VICTIM, t, v);
        sampleClip(sk, CLIP_TAKEDOWN_ATTACKER, t, a);
        vec3 neck = jointPos(sk, v, B_NECK) + clipRootMotion(sk, CLIP_TAKEDOWN_VICTIM, t);
        vec3 off(0.f, -0.55f, 0.f);
        vec3 e = jointPos(sk, a, B_FOREARM_R) + off, w = jointPos(sk, a, B_HAND_R) + off;
        vec3 ew = w - e;
        float u = Saturate(dot(neck - e, ew) / Max(length2(ew), 1e-6f));
        float dist = length(e + ew * u - neck);
        printf("takedown: forearm to victim neck %.3f m\n", dist);
        CHECK(dist < 0.12f, "takedown forearm %.3f m from the neck", dist);
        // the victim ends lying face down with the pelvis over its (moved) root, like GET_UP_FRONT's start
        Pose endV, gu;
        sampleClip(sk, CLIP_TAKEDOWN_VICTIM, 3.f, endV);
        sampleClip(sk, CLIP_GET_UP_FRONT, 0.f, gu);
        vec3 p0 = jointPos(sk, endV, B_PELVIS), p1 = jointPos(sk, gu, B_PELVIS);
        CHECK(length(p0 - p1) < 0.08f, "takedown victim end vs get-up start %.3f", length(p0 - p1));
        sampleClip(sk, CLIP_KNOCKOUT, 1.5f, endV);
        p0 = jointPos(sk, endV, B_PELVIS);
        CHECK(length(p0 - p1) < 0.08f, "knockout end vs get-up start %.3f", length(p0 - p1));
    }
    // bat: the animator keeps the left fist on the handle for any skeleton
    float worst = 0.f;
    for (u32 sd = 1; sd <= 8; sd++) {
        CharacterDesc d = randomCharacter(sd * 977u, (int)(sd % 7));
        Skeleton s;
        buildSkeleton(d, s);
        Animator an;
        an.init(&s, sd);
        AnimInput in;
        in.stance = 19;
        in.meleeKind = 2;
        in.weaponKind = 3;
        for (int f = 0; f < 150; f++) {
            in.action = f == 60 ? CLIP_BAT_SWING : -1;
            an.update(in, 1.f / 60.f);
            if (f < 40) continue;
            mat4 m[B_COUNT];
            computeMatrices(s, an.pose, m, nullptr);
            vec3 gr, ax, pr, gl, axl, pl;
            handGrip(s, m, true, gr, ax, pr);
            handGrip(s, m, false, gl, axl, pl);
            worst = Max(worst, length(gl - (gr - ax * 0.095f)));
        }
    }
    printf("bat grip: worst left fist distance from the handle %.3f m\n", worst);
    CHECK(worst < 0.02f, "left hand off the bat handle (%.3f m)", worst);
    // knockout holds its last frame
    {
        Animator an;
        an.init(&sk, 3u);
        AnimInput in;
        in.stance = 19;
        for (int f = 0; f < 200; f++) {
            in.action = f == 10 ? CLIP_KNOCKOUT : -1;
            an.update(in, 1.f / 60.f);
        }
        vec3 pel = jointPos(sk, an.pose, B_PELVIS);
        CHECK(an.actionDone() && an.action == CLIP_KNOCKOUT && pel.z < 0.3f, "knockout should hold lying (pelvis z %.2f)", pel.z);
    }
}

// Visemes move the lips: 'aa' opens the mouth, 'U' pushes the lips forward and narrows the corners, 'PP' closes.
void testVisemes() {
    CharacterDesc d = randomCharacter(4242u, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    Pose rest;
    sampleClip(sk, CLIP_IDLE, 0.f, rest);
    auto shapeOf = [&](int v, vec3& lipUp, vec3& lipLo, float& width) {
        Pose p = rest;
        float s[6];
        detail::visemeShape(v, 1.f, s);
        detail::applyMouthShape(p, s, s[0]);
        mat4 m[B_COUNT];
        computeMatrices(sk, p, m, nullptr);
        // lip points: rest positions carried by their bones (bind offsets from the pivots)
        auto carry = [&](int b, vec3 bindP) { return transformPoint(m[b], bindP - vec3(inverse(sk.invBindModel[b]).c[3].xyz())); };
        vec3 head = inverse(sk.invBindModel[B_HEAD]).c[3].xyz();
        vec3 up0 = head + vec3(0, 0.0955f, -0.014f), lo0 = head + vec3(0, 0.0924f, -0.026f);
        lipUp = carry(B_LIP_UPPER, up0);
        lipLo = carry(B_LIP_LOWER, lo0);
        vec3 cl = carry(B_LIP_CORNER_L, head + vec3(-0.0245f, 0.0865f, -0.0185f));
        vec3 cr = carry(B_LIP_CORNER_R, head + vec3(0.0245f, 0.0865f, -0.0185f));
        width = length(cr - cl);
    };
    vec3 u0, l0, uA, lA, uU, lU;
    float w0, wA, wU;
    shapeOf(0, u0, l0, w0);
    shapeOf(10, uA, lA, wA);
    shapeOf(14, uU, lU, wU);
    float open0 = u0.z - l0.z, openA = uA.z - lA.z;
    printf("visemes: mouth opening sil %.3f aa %.3f; U lips forward %.3f / %.3f m, width %.3f -> %.3f\n", open0, openA, uU.y - u0.y, lU.y - l0.y, w0, wU);
    CHECK(openA > open0 + 0.012f, "'aa' should open the mouth (%.3f)", openA - open0);
    CHECK(uU.y - u0.y > 0.006f && lU.y - l0.y > 0.003f, "'U' should push the lips forward");
    CHECK(wU < w0 - 0.012f, "'U' should narrow the mouth (%.3f -> %.3f)", w0, wU);
}

}  // namespace animtest

int main() {
    using namespace animtest;
    double t0 = TimeSeconds();
    Pose warm;
    CharacterDesc d0;
    Skeleton s0;
    buildSkeleton(d0, s0);
    sampleClip(s0, CLIP_IDLE, 0.f, warm);
    printf("clip library bake: %.1f ms\n", (TimeSeconds() - t0) * 1000.0);
    testBindPose();
    testPoseRoundTrip();
    testIK();
    testClips();
    testGait();
    testPoses();
    testAnimator();
    testDriving();
    testMelee();
    testVisemes();
    testLods();
    testMesh();
    printf("%s (%d failures)\n", gFail ? "FAILED" : "ALL PASSED", gFail);
    return gFail ? 1 : 0;
}
