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
    for (int role = 0; role < 8; role++) {   // (7: prison inmates)
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
            CHECK(tris >= 6000 && tris <= 36400, "triangle count %d out of budget (role %d)", tris, role);   // LOD0: face ~6-8k, strand cards ~2-7k
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

// ------------------------------------------------------------------------------------------------
// Locomotion quality through the Animator, with the ped moving through the world: stance-foot skate, cadence and
// step length against published gait data, ground penetration, knee / elbow direction, hand-thigh and knee-knee
// clearance, stops that end with both feet planted, on-the-spot turns that step instead of spinning.

struct FootProbe {
    vec3 heel, ball, toe;
};
// Heel, ball and toe points of a foot in model space (bind rotations are identity: heel 0.21, ball 0.52, toe tip
// 0.79 foot lengths from the ankle, the ball 0.52 foot lengths ahead).
FootProbe footPoints(const Skeleton& sk, const mat4* m, int s) {
    int fb = s ? B_FOOT_R : B_FOOT_L;
    float ball = sk.bindLocalPos[s ? B_TOE_R : B_TOE_L].y, heel = ball * (0.21f / 0.52f), toe = ball * (0.79f / 0.52f);
    float ankH = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z +
                 sk.bindLocalPos[B_FOOT_L].z;
    FootProbe f;
    // bone-space offsets from the joint (bone space = model axes at bind)
    f.heel = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, -heel, -ankH));
    f.ball = m[fb].c[3].xyz() + transformDir(m[fb], vec3(0.f, ball, -ankH));
    int tb = s ? B_TOE_R : B_TOE_L;
    float toeZ = sk.bindLocalPos[B_ROOT].z + sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[B_THIGH_L].z + sk.bindLocalPos[B_CALF_L].z +
                 sk.bindLocalPos[B_FOOT_L].z + sk.bindLocalPos[B_TOE_L].z;
    f.toe = m[tb].c[3].xyz() + transformDir(m[tb], vec3(0.f, toe - ball, -toeZ));
    return f;
}

float segDist(vec3 p, vec3 a, vec3 b) {
    vec3 ab = b - a;
    float u = Saturate(dot(p - a, ab) / Max(length2(ab), 1e-8f));
    return length(a + ab * u - p);
}

struct GaitStats {
    int frames = 0, contactN = 0;
    float skateSum = 0.f, skateMax = 0.f, skate95 = 0.f, lowest = 1e9f;
    int steps = 0;
    float stepTime0 = -1.f, stepTime1 = -1.f;
    float kneeBack = 0.f, elbowIn = 0.f, handThigh = 1e9f, kneeKnee = 1e9f;
    std::vector<float> skates;
};

// Walk a character through the world at speed v (m/s) along +Y for `secs` s at 60 Hz, measuring after a warm-up.
// Body surface (signed distance model the mesh is cast from) for interpenetration checks: a point in posed model space
// is mapped into a bone's bind frame (bind rotations are identity) and tested against that part's primitives.
struct BodySdf {
    detail::BodyDims D;
    detail::BuildCtx bc;
    detail::Sdf part[3];   // torso (moves with the pelvis), left / right leg-only primitives (move with the thighs)
    void build(const CharacterDesc& d, const Skeleton& sk) {
        detail::computeDims(d, D);
        bc.d = &d;
        bc.D = &D;
        bc.sk = &sk;
        detail::addBodyPrims(bc);
        for (const detail::Prim& q : bc.sdf.prims) {
            if (q.mask & detail::MK_TORSO) part[0].prims.push_back(q);
            else if (q.mask & detail::MK_LEG_L) part[1].prims.push_back(q);
            else if (q.mask & detail::MK_LEG_R) part[2].prims.push_back(q);
        }
    }
    // distance of a posed point from part k (0 torso, 1 left leg, 2 right leg), in that part's bone's bind frame
    float dist(const Skeleton& sk, const mat4* m, int k, vec3 p) const {
        const int bone = k == 0 ? B_PELVIS : (k == 1 ? B_THIGH_L : B_THIGH_R);
        const mat4& M = m[bone];
        vec3 dd = p - M.c[3].xyz();
        vec3 local(dot(dd, M.c[0].xyz()), dot(dd, M.c[1].xyz()), dot(dd, M.c[2].xyz()));
        vec3 J = -sk.invBindModel[bone].c[3].xyz();
        return part[k].eval(J + local, detail::MK_ALL);
    }
};

GaitStats runGait(const Skeleton& sk, Animator& an, float v, float secs, float turnRate = 0.f, const BodySdf* body = nullptr) {
    GaitStats st;
    const float dt = 1.f / 60.f;
    vec3 root(0.f);
    float yaw = 0.f;
    FootProbe prev[2];
    bool prevIn[2] = {false, false}, wasUp[2] = {true, true};
    AnimInput in;
    in.speed = v;
    in.turnRate = turnRate;
    in.footProbes = true;
    const int n = (int)(secs / dt);
    const float warm = 2.f;
    for (int f = 0; f < n; f++) {
        float t = f * dt;
        yaw += turnRate * dt;   // like the game: turn, then move along the new heading
        vec2 fwd(-sinf(yaw), cosf(yaw));
        root = root + vec3(fwd.x, fwd.y, 0.f) * (v * dt);
        an.update(in, dt);
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        quat qy = quatAxisAngle(vec3(0, 0, 1), yaw);
        for (int s = 0; s < 2; s++) {
            FootProbe fpm = footPoints(sk, m, s), w;
            w.heel = root + rotate(qy, fpm.heel);
            w.ball = root + rotate(qy, fpm.ball);
            w.toe = root + rotate(qy, fpm.toe);
            if (t > warm) {
                st.lowest = Min(st.lowest, Min(w.heel.z, Min(w.ball.z, w.toe.z)));
                // contact: the lowest sole point within 4 mm of the ground, this frame and the previous one
                bool in0 = Min(w.heel.z, w.ball.z) < 0.004f;
                if (in0 && prevIn[s]) {
                    vec3 a = w.heel.z < w.ball.z ? w.heel : w.ball, b = w.heel.z < w.ball.z ? prev[s].heel : prev[s].ball;
                    float sp = length(vec2(a.x - b.x, a.y - b.y)) / dt;
                    st.skateSum += sp;
                    st.skateMax = Max(st.skateMax, sp);
                    st.skates.push_back(sp);
                    st.contactN++;
                }
                // steps: a heel coming down after being clear of the ground
                if (wasUp[s] && Min(w.heel.z, w.ball.z) < 0.012f) {
                    st.steps++;
                    if (st.stepTime0 < 0.f) st.stepTime0 = t;
                    st.stepTime1 = t;
                    wasUp[s] = false;
                }
                if (Min(w.heel.z, w.ball.z) > 0.03f) wasUp[s] = true;
                prevIn[s] = in0;
            } else {
                prevIn[s] = false;
                wasUp[s] = Min(w.heel.z, w.ball.z) > 0.03f;
            }
            prev[s] = w;
        }
        if (t > warm) {
            st.frames++;
            // knees point forward of the hip-ankle line, elbows behind / outside the shoulder-wrist line
            vec3 pelvisFwd = normalize(m[B_PELVIS].c[1].xyz());
            for (int s = 0; s < 2; s++) {
                vec3 hip = m[s ? B_THIGH_R : B_THIGH_L].c[3].xyz(), knee = m[s ? B_CALF_R : B_CALF_L].c[3].xyz(),
                     ank = m[s ? B_FOOT_R : B_FOOT_L].c[3].xyz();
                vec3 legD = normalize(ank - hip);
                vec3 off = knee - (hip + legD * dot(knee - hip, legD));
                st.kneeBack = Max(st.kneeBack, -dot(off, pelvisFwd));
                vec3 el = m[s ? B_FOREARM_R : B_FOREARM_L].c[3].xyz(), wr = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
                // elbow flexion about its hinge in the upper arm's frame (bind axes): negative = bent backwards
                const mat4& mu = m[s ? B_UPPERARM_R : B_UPPERARM_L];
                vec3 fw = normalize(wr - el);
                vec3 fl(dot(fw, normalize(mu.c[0].xyz())), dot(fw, normalize(mu.c[1].xyz())), dot(fw, normalize(mu.c[2].xyz())));
                vec3 b = normalize(sk.bindLocalPos[s ? B_FOREARM_R : B_FOREARM_L]);
                vec3 hinge = normalize(cross(b, vec3(0, 1, 0)));
                float flex = atan2f(dot(cross(b, fl), hinge), dot(b, fl));
                st.elbowIn = Max(st.elbowIn, -flex);   // hyperextension (rad)
                // hand (palm centre, middle fingertip) against the thighs and hips: the body's own surface model
                if (body) {
                    vec3 fingD = normalize(transformDir(m[s ? B_HAND_R : B_HAND_L], sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                    vec3 palm = wr + fingD * (0.45f * length(sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                    int mid3 = phalanxBone(s == 1, 1, 2);
                    vec3 tip = m[mid3].c[3].xyz() + transformDir(m[mid3], normalize(sk.bindLocalPos[mid3]) * sk.boneLength[mid3]);
                    const vec3 pts[2] = {palm, tip};
                    const float rad[2] = {sk.boneRadius[s ? B_HAND_R : B_HAND_L] * 0.75f, 0.008f};
                    for (int q = 0; q < 2; q++) {
                        float dd = Min(body->dist(sk, m, 0, pts[q]), Min(body->dist(sk, m, 1, pts[q]), body->dist(sk, m, 2, pts[q])));
                        st.handThigh = Min(st.handThigh, dd - rad[q]);
                    }
                }
            }
            st.kneeKnee = Min(st.kneeKnee, length(m[B_CALF_L].c[3].xyz() - m[B_CALF_R].c[3].xyz()));
        }
    }
    std::sort(st.skates.begin(), st.skates.end());
    if (!st.skates.empty()) st.skate95 = st.skates[(size_t)(st.skates.size() * 0.95f)];
    return st;
}

void testLocomotion() {
    // published cadence (steps/min) at a speed: walking from the walk ratio (step length / cadence, 0.0045-0.0078
    // m/(steps/min) scaled by height / 1.75, the low end for older adults: Sekiya & Nagasaki 1998), running from
    // treadmill / overground data (e.g. Cavanagh &
    // Kram 1989, Dorn et al. 2012): 150-172 at 2.4-3 m/s, 165-192 at 5 m/s, 180-215 at 7 m/s
    struct Case {
        u32 seed;
        int role;
        float age;
        int gender;
    };
    const Case people[] = {{12u, 0, 0.3f, 0}, {45u, 0, 0.25f, 1}, {77u, 3, 0.4f, 0}, {91u, 0, 0.92f, 1}, {140u, 2, 0.1f, 0}, {166u, 5, 0.5f, 0}};
    const float speeds[] = {0.9f, 1.2f, 1.4f, 1.7f, 2.0f, 2.4f, 3.0f, 5.0f, 7.0f};
    float worstSkate = 0.f, worst95 = 0.f, worstLow = 1e9f, worstKnee = 0.f, worstElbow = 0.f, worstHand = 1e9f, worstKK = 1e9f;
    int cadFail = 0, cadN = 0;
    for (const Case& c : people) {
        CharacterDesc d = randomCharacter(c.seed, c.role);
        d.age = c.age;
        d.gender = c.gender ? FEMALE : MALE;
        Skeleton sk;
        buildSkeleton(d, sk);
        BodySdf body;
        body.build(d, sk);
        for (float v : speeds) {
            Animator an;
            an.init(&sk, c.seed * 7u + 1u);
            an.setCharacter(d);
            GaitStats st = runGait(sk, an, v, 6.f, 0.f, &body);
            float dur = st.stepTime1 - st.stepTime0;
            float cad = st.steps > 2 && dur > 0.f ? (st.steps - 1) / dur * 60.f : 0.f;
            float stepLen = cad > 0.f ? v * 60.f / cad : 0.f;
            float mean = st.contactN ? st.skateSum / st.contactN : 0.f;
            // expected cadence band
            float hs = d.height / 1.75f, lo, hi;
            if (v <= 2.05f) {
                lo = sqrtf(60.f * v / (0.0078f * hs));
                hi = sqrtf(60.f * v / (0.0045f * hs));   // older / shorter-stepping people reach ~0.0045-0.005
            } else {
                lo = v < 2.7f ? 145.f : (v < 4.f ? 150.f : (v < 6.f ? 162.f : 178.f));
                hi = v < 2.7f ? 172.f : (v < 4.f ? 178.f : (v < 6.f ? 195.f : 218.f));
            }
            bool ok = cad >= lo && cad <= hi;
            cadN++;
            if (!ok) cadFail++;
            printf("  gait seed %3u %s age %.2f style %d v %.1f: cadence %5.1f (%.0f-%.0f)%s step %.2f m, skate mean %.3f p95 %.3f max %.3f m/s, "
                   "lowest %.3f, hand-thigh %.3f\n",
                   c.seed, c.gender ? "F" : "M", c.age, an.gaitStyle, v, cad, lo, hi, ok ? "" : " !", stepLen, mean, st.skate95, st.skateMax, st.lowest,
                   st.handThigh);
            CHECK(ok, "cadence %.1f outside %.0f-%.0f steps/min at %.1f m/s (seed %u)", cad, lo, hi, v, c.seed);
            if (v <= 5.f) {
                worstSkate = Max(worstSkate, mean);
                worst95 = Max(worst95, st.skate95);
            }
            worstLow = Min(worstLow, st.lowest);
            worstKnee = Max(worstKnee, st.kneeBack);
            worstElbow = Max(worstElbow, st.elbowIn);
            worstHand = Min(worstHand, st.handThigh);
            worstKK = Min(worstKK, st.kneeKnee);
        }
    }
    printf("locomotion: stance skate mean <= %.3f m/s (p95 %.3f) up to 5 m/s, lowest sole point %.3f m, knee behind the leg line %.3f m, "
           "elbow hyperextension %.3f rad, hand-thigh clearance %.3f m, knee-knee %.3f m, cadence in range %d/%d\n",
           worstSkate, worst95, worstLow, worstKnee, worstElbow, worstHand, worstKK, cadN - cadFail, cadN);
    CHECK(worstSkate < 0.02f, "stance feet skate at %.3f m/s", worstSkate);
    CHECK(worstLow > -0.012f, "feet sink %.3f m into the ground", worstLow);
    CHECK(worstKnee < 0.01f, "a knee bends backwards (%.3f m)", worstKnee);
    CHECK(worstElbow < 0.05f, "an elbow bends backwards (%.3f rad)", worstElbow);
    CHECK(worstHand > -0.01f, "a hand passes into a thigh or hip (%.3f m)", worstHand);
    CHECK(worstKK > 0.1f, "knees collide (%.3f m apart)", worstKK);
}

// Walking backwards, sideways and diagonally (the root moving along localMoveDir, as when aiming): planted feet stay
// put; the direction may reverse at once (straight back from walking forward).
void testDirections() {
    const float dt = 1.f / 60.f;
    const vec2 dirs[5] = {vec2(1, 0), vec2(-1, 0), vec2(0, -1), vec2(0.7071f, 0.7071f), vec2(-0.7071f, -0.7071f)};
    const char* names[5] = {"right", "left", "back", "fwd-right", "back-left"};
    float worst = 0.f, shins = 1e9f;   // shins: closest approach of the two shins (knee -> ankle), legs never through each other
    std::string line;
    for (int di = 0; di < 5; di++) {
        CharacterDesc d = randomCharacter(77u + (u32)di, 0);
        Skeleton sk;
        buildSkeleton(d, sk);
        Animator an;
        an.init(&sk, 5u);
        an.setCharacter(d);
        AnimInput in;
        in.footProbes = true;
        vec3 root(0);
        FootProbe prevF[2];
        bool prevOk[2] = {false, false};
        double sum = 0;
        int n = 0;
        for (int f = 0; f < 420; f++) {
            float t = f * dt;
            // walking forward first, then the new direction (a reversal for "back")
            vec2 dir = t < 1.5f ? vec2(0, 1) : dirs[di];
            in.localMoveDir = dir;
            in.speed = 1.2f;
            root = root + vec3(dir.x, dir.y, 0.f) * (in.speed * dt);
            an.update(in, dt);
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            if (t > 2.5f) {
                vec3 kl = m[B_CALF_L].c[3].xyz(), al = m[B_FOOT_L].c[3].xyz(), kr = m[B_CALF_R].c[3].xyz(), ar = m[B_FOOT_R].c[3].xyz();
                for (int k = 0; k <= 6; k++) shins = Min(shins, segDist(lerp(kl, al, k / 6.f), kr, ar));
            }
            for (int s = 0; s < 2; s++) {
                FootProbe fpm = footPoints(sk, m, s), w;
                w.heel = root + fpm.heel;
                w.ball = root + fpm.ball;
                bool h = w.heel.z < w.ball.z;
                vec3 a = h ? w.heel : w.ball, b = h ? prevF[s].heel : prevF[s].ball;
                bool ok = a.z < 0.004f;
                if (ok && prevOk[s] && t > 2.5f) {
                    sum += length(vec2(a.x - b.x, a.y - b.y)) / dt;
                    n++;
                }
                prevF[s] = w;
                prevOk[s] = ok;
            }
        }
        float mean = n ? (float)(sum / n) : 0.f;
        worst = Max(worst, mean);
        line += StrFormat(" %s %.3f", names[di], mean);
    }
    printf("directions at 1.2 m/s, planted-foot skate mean (m/s):%s; shins at least %.3f m apart\n", line.c_str(), shins);
    CHECK(worst < 0.03f, "feet slide walking in some direction (%.3f m/s)", worst);
    CHECK(shins > 0.08f, "the legs pass through each other walking in some direction (shins %.3f m apart)", shins);
}

// Stopping: within 1.5 s both feet are planted (no skating) and brought together into the standing stance; turning on
// the spot: the feet stay put between steps and step round instead of spinning.
void testStopsAndTurns() {
    float worstStopSkate = 0.f, worstSep = 0.f, worstTurnSkate = 0.f;   // worst single-frame planted-foot speed (m/s)
    double stopSum = 0.0, turnSum = 0.0;                                 // mean over contact frames
    int stopN = 0, turnN = 0;
    int minSteps = 1000;
    for (u32 sd = 1; sd <= 6; sd++) {
        CharacterDesc d = randomCharacter(sd * 313u, (int)(sd % 5));
        Skeleton sk;
        buildSkeleton(d, sk);
        // stop from a walk
        {
            Animator an;
            an.init(&sk, sd);
            an.setCharacter(d);
            const float dt = 1.f / 60.f;
            vec3 root(0.f);
            AnimInput in;
            in.footProbes = true;
            FootProbe prevF[2];
            float v = 0.f;
            for (int f = 0; f < 300; f++) {
                float t = f * dt;
                float want = t < 2.5f ? 1.4f : 0.f;
                // the game's ped controller: 11 m/s^2 speeding up, 16 slowing down
                v = want > v ? Min(want, v + 11.f * dt) : Max(want, v - 16.f * dt);
                root.y += v * dt;
                in.speed = v;
                an.update(in, dt);
                mat4 m[B_COUNT];
                computeMatrices(sk, an.pose, m, nullptr);
                for (int s = 0; s < 2; s++) {
                    FootProbe fpm = footPoints(sk, m, s), w;
                    w.heel = root + fpm.heel;
                    w.ball = root + fpm.ball;
                    // the sole point on the ground, compared with the same point in the previous frame
                    bool h = w.heel.z < w.ball.z;
                    vec3 a = h ? w.heel : w.ball, b = h ? prevF[s].heel : prevF[s].ball;
                    if (t > 3.8f && f > 0 && a.z < 0.004f) {
                        float sp = length(vec2(a.x - b.x, a.y - b.y)) / dt;
                        worstStopSkate = Max(worstStopSkate, sp);
                        stopSum += sp;
                        stopN++;
                    }
                    prevF[s] = w;
                }
                if (f == 299) {
                    vec3 a = m[B_FOOT_L].c[3].xyz(), b = m[B_FOOT_R].c[3].xyz();
                    worstSep = Max(worstSep, fabsf(a.y - b.y));
                }
            }
        }
        // turn on the spot
        {
            Animator an;
            an.init(&sk, sd + 50u);
            an.setCharacter(d);
            const float dt = 1.f / 60.f;
            AnimInput in;
            in.footProbes = true;
            float yaw = 0.f;
            FootProbe prevF[2];
            bool prevOk[2] = {false, false};
            int steps = 0;
            for (int f = 0; f < 240; f++) {
                float t = f * dt;
                in.turnRate = t > 0.5f && t < 3.5f ? 1.6f : 0.f;
                yaw += in.turnRate * dt;
                an.update(in, dt);
                steps += __builtin_popcount(an.footEvents);
                mat4 m[B_COUNT];
                computeMatrices(sk, an.pose, m, nullptr);
                quat q = quatAxisAngle(vec3(0, 0, 1), yaw);
                for (int s = 0; s < 2; s++) {
                    FootProbe fpm = footPoints(sk, m, s), w;
                    w.heel = rotate(q, fpm.heel);
                    w.ball = rotate(q, fpm.ball);
                    bool h = w.heel.z < w.ball.z;
                    vec3 a = h ? w.heel : w.ball, b = h ? prevF[s].heel : prevF[s].ball;
                    bool ok = a.z < 0.004f;
                    if (ok && prevOk[s] && t > 0.6f) {
                        float sp = length(vec2(a.x - b.x, a.y - b.y)) / dt;
                        worstTurnSkate = Max(worstTurnSkate, sp);
                        turnSum += sp;
                        turnN++;
                    }
                    prevF[s] = w;
                    prevOk[s] = ok;
                }
            }
            minSteps = Min(minSteps, steps);
        }
    }
    float stopMean = stopN ? (float)(stopSum / stopN) : 0.f, turnMean = turnN ? (float)(turnSum / turnN) : 0.f;
    printf("stops and turns: planted feet after a stop %.4f m/s mean (worst frame %.1f mm), final fore-aft foot offset %.2f m; turning on the "
           "spot: planted feet %.4f m/s mean (worst frame %.1f mm), at least %d steps for 4.8 rad\n",
           stopMean, worstStopSkate * 1000.f / 60.f, worstSep, turnMean, worstTurnSkate * 1000.f / 60.f, minSteps);
    CHECK(stopMean < 0.02f && worstStopSkate < 0.12f, "feet slide after stopping (%.3f m/s mean, %.3f m/s worst)", stopMean, worstStopSkate);
    CHECK(worstSep < 0.16f, "feet not brought together after a stop (%.2f m apart)", worstSep);
    CHECK(turnMean < 0.02f && worstTurnSkate < 0.12f, "feet slide while turning on the spot (%.3f m/s mean, %.3f m/s worst)", turnMean, worstTurnSkate);
    CHECK(minSteps >= 6, "turning on the spot takes too few steps (%d)", minSteps);
}

// Standing around for a while: weight shifts, settling steps, postures and fidgets from each person's habits, all
// with the feet planted (no skating), no sole below the ground, no knee or elbow bent backwards, hands clear of the
// body; people out of step with each other; breathing at 12-18 a minute at rest and faster after a run.
void testStanding() {
    const float dt = 1.f / 60.f, secs = 120.f;
    const int NP = 10;
    float skateSum = 0.f, skateWorst = 0.f, lowest = 1e9f, kneeBack = 0.f, elbowIn = 0.f, handBody = 1e9f, pocketBody = 1e9f;
    int skateN = 0, shifts = 0, settles = 0, postures = 0, fidgets = 0, badFrames = 0;
    int kinds[64] = {};
    float handKind[65];   // closest palm to the body per posture / fidget playing (index 64: none)
    for (float& h : handKind) h = 1e9f;
    std::vector<std::vector<float>> standSeries(NP);
    float rateLo = 1e9f, rateHi = 0.f;
    for (int i = 0; i < NP; i++) {
        CharacterDesc d = randomCharacter(900u + (u32)i * 53u, i % 7);
        if (i == 3) d.age = 0.9f;
        Skeleton sk;
        buildSkeleton(d, sk);
        BodySdf body;
        body.build(d, sk);
        Animator an;
        an.init(&sk, 4000u + (u32)i * 17u);
        an.setCharacter(d);
        rateLo = Min(rateLo, an.breathRate * 60.f);
        rateHi = Max(rateHi, an.breathRate * 60.f);
        AnimInput in;
        in.footProbes = true;
        if (i == 5) in.stance = 23;   // queueing
        if (i == 7) in.stance = 7;    // chatting (the talk clip's upper body over the weight shifts)
        FootProbe prevF[2];
        bool prevOk[2] = {false, false};
        float lastTarget = an.standTarget;
        int lastVar = -1, lastFid = -1;
        for (int f = 0; f < (int)(secs / dt); f++) {
            float t = f * dt;
            an.update(in, dt);
            if (an.standTarget != lastTarget) shifts++, lastTarget = an.standTarget;
            if (an.idleVar >= 0 && an.idleVar != lastVar) postures++, kinds[an.idleVar & 63]++;
            if (an.fidgetVar >= 0 && an.fidgetVar != lastFid) fidgets++, kinds[an.fidgetVar & 63]++;
            lastVar = an.idleVar;
            lastFid = an.fidgetVar;
            settles += (an.footEvents & 1u) + ((an.footEvents >> 1) & 1u);
            standSeries[i].push_back(an.standW);
            mat4 m[B_COUNT];
            computeMatrices(sk, an.pose, m, nullptr);
            bool finite = true;
            for (int b = 0; b < B_COUNT; b++) finite = finite && std::isfinite(m[b].c[3].x) && std::isfinite(m[b].c[3].z);
            if (!finite) badFrames++;
            for (int s = 0; s < 2; s++) {
                FootProbe w = footPoints(sk, m, s);
                bool h = w.heel.z < w.ball.z;
                vec3 a = h ? w.heel : w.ball, b = h ? prevF[s].heel : prevF[s].ball;
                bool ok = a.z < 0.004f;
                if (ok && prevOk[s] && t > 1.f) {
                    float sp = length(vec2(a.x - b.x, a.y - b.y)) / dt;
                    skateWorst = Max(skateWorst, sp);
                    skateSum += sp;
                    skateN++;
                }
                if (t > 1.f) lowest = Min(lowest, Min(w.heel.z, Min(w.ball.z, w.toe.z)));
                prevF[s] = w;
                prevOk[s] = ok;
            }
            if (t > 1.f && f % 3 == 0) {
                vec3 pelvisFwd = normalize(m[B_PELVIS].c[1].xyz());
                for (int s = 0; s < 2; s++) {
                    vec3 hip = m[s ? B_THIGH_R : B_THIGH_L].c[3].xyz(), knee = m[s ? B_CALF_R : B_CALF_L].c[3].xyz(),
                         ank = m[s ? B_FOOT_R : B_FOOT_L].c[3].xyz();
                    vec3 legD = normalize(ank - hip);
                    vec3 off = knee - (hip + legD * dot(knee - hip, legD));
                    kneeBack = Max(kneeBack, -dot(off, pelvisFwd));
                    vec3 el = m[s ? B_FOREARM_R : B_FOREARM_L].c[3].xyz(), wr = m[s ? B_HAND_R : B_HAND_L].c[3].xyz();
                    const mat4& mu = m[s ? B_UPPERARM_R : B_UPPERARM_L];
                    vec3 fw = normalize(wr - el);
                    vec3 fl(dot(fw, normalize(mu.c[0].xyz())), dot(fw, normalize(mu.c[1].xyz())), dot(fw, normalize(mu.c[2].xyz())));
                    vec3 bb = normalize(sk.bindLocalPos[s ? B_FOREARM_R : B_FOREARM_L]);
                    vec3 hinge = normalize(cross(bb, vec3(0, 1, 0)));
                    elbowIn = Max(elbowIn, -atan2f(dot(cross(bb, fl), hinge), dot(bb, fl)));
                    // the palm centre against the body (hands in pockets sit on the thigh, a clasp at the belly)
                    vec3 fingD = normalize(transformDir(m[s ? B_HAND_R : B_HAND_L], sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                    vec3 palm = wr + fingD * (0.45f * length(sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]));
                    float dd = Min(body.dist(sk, m, 0, palm), Min(body.dist(sk, m, 1, palm), body.dist(sk, m, 2, palm)));
                    dd -= sk.boneRadius[s ? B_HAND_R : B_HAND_L] * 0.75f;
                    int kind = an.fidgetVar >= 0 && an.fidgetW > 0.5f ? an.fidgetVar : (an.idleVar >= 0 && an.idleVarW > 0.5f ? an.idleVar : -1);
                    // hands in the pockets are meant to disappear into them (checked on their own)
                    if (an.idleVar == detail::IC_IDLE_POCKETS && an.idleVarW > 0.3f) pocketBody = Min(pocketBody, dd);
                    else handBody = Min(handBody, dd);
                    float& hk = handKind[kind >= 0 ? (kind & 63) : 64];
                    hk = Min(hk, dd);
                }
            }
        }
    }
    // people out of step: correlation of the weight-shift curves between pairs
    float corrSum = 0.f;
    int corrN = 0;
    for (int a = 0; a < NP; a++)
        for (int b = a + 1; b < NP; b++) {
            const std::vector<float>&x = standSeries[a], &y = standSeries[b];
            double mx = 0, my = 0;
            for (size_t k = 0; k < x.size(); k++) mx += x[k], my += y[k];
            mx /= x.size();
            my /= y.size();
            double sxy = 0, sxx = 0, syy = 0;
            for (size_t k = 0; k < x.size(); k++) sxy += (x[k] - mx) * (y[k] - my), sxx += (x[k] - mx) * (x[k] - mx), syy += (y[k] - my) * (y[k] - my);
            if (sxx > 1e-9 && syy > 1e-9) corrSum += fabsf((float)(sxy / sqrt(sxx * syy))), corrN++;
        }
    float corr = corrN ? corrSum / corrN : 0.f;
    // breathing after a run: 20 s at 6 m/s, then standing
    float rateRest = 0.f, rateAfter = 0.f, rateLater = 0.f;
    {
        CharacterDesc d = randomCharacter(4242u, 0);
        Skeleton sk;
        buildSkeleton(d, sk);
        Animator an;
        an.init(&sk, 77u);
        an.setCharacter(d);
        AnimInput in;
        auto rate = [&](float secs) {
            int n = 0;
            float prev = an.breathPh;
            for (int f = 0; f < (int)(secs / dt); f++) {
                an.update(in, dt);
                if (an.breathPh < prev) n++;
                prev = an.breathPh;
            }
            return n / secs * 60.f;
        };
        rateRest = rate(60.f);
        in.speed = 6.f;
        rate(20.f);
        in.speed = 0.f;
        rateAfter = rate(10.f);
        rate(50.f);
        rateLater = rate(30.f);
    }
    std::string kindStr;
    const struct { int id; const char* n; } names[] = {{detail::IC_IDLE_PHONE, "phone"}, {detail::IC_IDLE_CROSSARMS, "arms crossed"},
        {detail::IC_IDLE_POCKETS, "pockets"}, {detail::IC_IDLE_HIP, "hip"}, {detail::IC_IDLE_BEHIND, "behind"}, {detail::IC_IDLE_CLASP, "clasp"},
        {detail::IC_FIDGET_WATCH, "watch"}, {detail::IC_FIDGET_SCRATCH, "scratch"}, {detail::IC_FIDGET_TUG, "tug"}, {detail::IC_FIDGET_CHIN, "chin"},
        {detail::IC_FIDGET_YAWN, "yawn"}, {detail::IC_FIDGET_ARMS, "stretch"}, {detail::IC_FIDGET_TAP, "tap"}, {detail::IC_FIDGET_ROCK, "rock"},
        {detail::IC_IDLE_STRETCH, "neck roll"}};
    std::string handStr = handKind[64] < 1e8f ? StrFormat(" none %.3f", handKind[64]) : std::string();
    for (const auto& k : names) {
        kindStr += StrFormat(" %s %d", k.n, kinds[k.id & 63]);
        if (handKind[k.id & 63] < 1e8f) handStr += StrFormat(" %s %.3f", k.n, handKind[k.id & 63]);
    }
    float skate = skateN ? skateSum / skateN : 0.f;
    printf("standing (%d people x %.0f s): %d weight shifts, %d settling steps, %d postures, %d fidgets;%s\n", NP, secs, shifts, settles, postures, fidgets,
           kindStr.c_str());
    printf("  planted feet %.4f m/s mean (worst frame %.1f mm), lowest sole point %.3f m, knee behind the leg line %.3f m, elbow hyperextension "
           "%.3f rad, palm-body clearance %.3f m (in the pockets %.3f); weight shifts in step between people |r| %.2f; breathing %.1f-%.1f /min "
           "at rest, one person %.1f -> %.1f just after a 20 s run -> %.1f a minute later\n",
           skate, skateWorst * 1000.f * dt, lowest, kneeBack, elbowIn, handBody, pocketBody, corr, rateLo, rateHi, rateRest, rateAfter, rateLater);
    printf("  palm-body clearance by posture / fidget (m):%s\n", handStr.c_str());
    CHECK(badFrames == 0, "%d frames with non-finite bones", badFrames);
    CHECK(skate < 0.005f && skateWorst < 0.12f, "standing feet slide (%.4f m/s mean, %.3f m/s worst)", skate, skateWorst);
    CHECK(lowest > -0.012f, "feet sink %.3f m into the ground while standing", lowest);
    CHECK(kneeBack < 0.01f, "a knee bends backwards while standing (%.3f m)", kneeBack);
    CHECK(elbowIn < 0.05f, "an elbow bends backwards while standing (%.3f rad)", elbowIn);
    CHECK(handBody > -0.02f, "a hand passes into the body (%.3f m)", handBody);
    CHECK(pocketBody > -0.04f, "hands sink too deep for the pockets (%.3f m)", pocketBody);
    CHECK(shifts >= NP * 5 && postures >= NP && fidgets >= NP * 2, "too little going on: %d shifts, %d postures, %d fidgets", shifts, postures, fidgets);
    CHECK(corr < 0.35f, "people shift their weight in step (|r| %.2f)", corr);
    CHECK(rateLo >= 11.9f && rateHi <= 18.1f, "resting breathing %.1f-%.1f /min", rateLo, rateHi);
    CHECK(rateAfter > rateRest * 1.3f && rateLater < rateAfter, "breathing after a run %.1f (rest %.1f, later %.1f)", rateAfter, rateRest, rateLater);
}

// Greetings between two people (hug, handshake, cheek kiss) for a same-size and a tall / short pair: both animators
// stepped together, each given the other's chest (head for the kiss): the hands land behind the partner's back and
// the handshake's hands meet, the cheeks come side by side, the feet stay clear of each other's.
void testGreetings() {
    const float dt = 1.f / 60.f;
    struct Pair {
        u32 a, b;
        float ha, hb;
    };
    const Pair pairs[] = {{321u, 654u, 1.76f, 1.76f}, {777u, 888u, 1.86f, 1.58f}};
    float worstBehind = 1e9f, worstShake = 0.f, worstKiss = 0.f, worstFeet = 1e9f, hugIn = 1e9f, hugOut = -1e9f, hugHeads = 1e9f;
    for (const Pair& pr : pairs) {
        CharacterDesc d[2] = {randomCharacter(pr.a, 0), randomCharacter(pr.b, 0)};
        d[0].height = pr.ha;
        d[1].height = pr.hb;
        Skeleton sk[2];
        for (int k = 0; k < 2; k++) buildSkeleton(d[k], sk[k]);
        BodySdf body[2];
        for (int k = 0; k < 2; k++) body[k].build(d[k], sk[k]);
        const Clip clips[3] = {CLIP_HUG, CLIP_HANDSHAKE, CLIP_CHEEK_KISS};
        for (Clip c : clips) {
            Animator an[2];
            for (int k = 0; k < 2; k++) {
                an[k].init(&sk[k], 50u + (u32)k);
                an[k].setCharacter(d[k]);
            }
            const float dist = pairDistance(c, sk[0], sk[1]);
            const float peak = clipEventTime(c) + 0.3f;   // well into the contact
            const int bone = c == CLIP_CHEEK_KISS ? B_HEAD : B_CHEST;
            mat4 m[2][B_COUNT];
            for (float t = 0.f; t < clipInfo(c).duration; t += dt) {
                vec3 pb[2];
                for (int k = 0; k < 2; k++) {
                    quat q;
                    detail::boneModel(sk[k], an[k].pose, bone, q, pb[k]);
                }
                for (int k = 0; k < 2; k++) {
                    AnimInput in;
                    in.action = t < 0.1f ? (int)c : -1;
                    vec3 o = pb[1 - k];
                    in.grabTarget = vec3(-o.x, dist - o.y, o.z);
                    in.grabWeight = 1.f;
                    an[k].update(in, dt);
                    computeMatrices(sk[k], an[k].pose, m[k], nullptr);
                }
                // the second partner in the first one's model space: turned round, dist ahead
                auto toA = [&](vec3 p) { return vec3(-p.x, dist - p.y, p.z); };
                // feet apart all the way (ankles and toes)
                const int feet[4] = {B_FOOT_L, B_FOOT_R, B_TOE_L, B_TOE_R};
                for (int i = 0; i < 4; i++)
                    for (int j = 0; j < 4; j++) {
                        vec3 a = m[0][feet[i]].c[3].xyz(), b = toA(m[1][feet[j]].c[3].xyz());
                        worstFeet = Min(worstFeet, length(vec2(a.x - b.x, a.y - b.y)));
                    }
                if (fabsf(t - peak) > 0.5f * dt) continue;
                auto palm = [&](int k, int s) {
                    const mat4& hm = m[k][s ? B_HAND_R : B_HAND_L];
                    return hm.c[3].xyz() + transformDir(hm, sk[k].bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]) * 0.45f;
                };
                if (c == CLIP_HUG) {
                    // the heads side by side, not into each other (hair, caps)
                    vec3 ha = m[0][B_HEAD].c[3].xyz(), hb2 = toA(m[1][B_HEAD].c[3].xyz());
                    hugHeads = Min(hugHeads, length(ha - hb2));
                    if (getenv("ANIM_TEST_VERBOSE")) printf("  hug pair %u: heads %.3f m apart (%.3f %.3f %.3f) (%.3f %.3f %.3f)\n", pr.a, length(ha - hb2), ha.x, ha.y, ha.z, hb2.x, hb2.y, hb2.z);
                    // each palm behind the partner's chest joint (on its back), for both partners: in the partner's
                    // model space y' = dist - y, its back at negative y'
                    for (int k = 0; k < 2; k++)
                        for (int s = 0; s < 2; s++) {
                            float yp = dist - palm(k, s).y;
                            float behind = m[1 - k][B_CHEST].c[3].y - yp;
                            // the palm against the partner's body (its surface model, in its model space)
                            vec3 pp = toA(palm(k, s));
                            float gap = Min(body[1 - k].dist(sk[1 - k], m[1 - k], 0, pp),
                                            Min(body[1 - k].dist(sk[1 - k], m[1 - k], 1, pp), body[1 - k].dist(sk[1 - k], m[1 - k], 2, pp))) -
                                        sk[k].boneRadius[s ? B_HAND_R : B_HAND_L] * 0.75f;
                            if (getenv("ANIM_TEST_VERBOSE"))
                                printf("  hug pair %u: partner %d hand %d %.3f behind, %.3f from the skin\n", pr.a, k, s, behind, gap);
                            worstBehind = Min(worstBehind, behind);
                            hugIn = Min(hugIn, gap);
                            hugOut = Max(hugOut, gap);
                        }
                } else if (c == CLIP_HANDSHAKE) {
                    // the right palms together
                    worstShake = Max(worstShake, length(palm(0, 1) - toA(palm(1, 1))));
                } else {
                    vec3 a = m[0][B_HEAD].c[3].xyz(), b = toA(m[1][B_HEAD].c[3].xyz());
                    if (getenv("ANIM_TEST_VERBOSE")) printf("  kiss pair %u: heads (%.3f %.3f %.3f) (%.3f %.3f %.3f)\n", pr.a, a.x, a.y, a.z, b.x, b.y, b.z);
                    worstKiss = Max(worstKiss, length(a - b));
                }
            }
        }
    }
    printf("greetings: hug palms %.3f m behind the partner's chest joint (min), %.3f .. %.3f m from its skin, heads %.3f m apart; handshake "
           "palms %.3f m apart, cheek kiss heads %.3f m apart, feet at least %.3f m apart\n",
           worstBehind, hugIn, hugOut, hugHeads, worstShake, worstKiss, worstFeet);
    CHECK(hugHeads > 0.21f, "hugging heads too close (%.3f m)", hugHeads);
    CHECK(worstBehind > 0.03f, "a hugging hand is not round the partner's back (%.3f m)", worstBehind);
    CHECK(hugIn > -0.03f && hugOut < 0.06f, "hugging hands not on the partner's back (%.3f .. %.3f m from the skin)", hugIn, hugOut);
    CHECK(worstShake < 0.07f, "the handshake's hands miss (%.3f m)", worstShake);
    CHECK(worstKiss < 0.24f, "the cheek kiss's heads stay apart (%.3f m)", worstKiss);
    CHECK(worstFeet > 0.05f, "feet collide in a greeting (%.3f m)", worstFeet);
    // hats: brims keep people from cheek kisses and (unless the wearer is clearly taller) hugs
    CharacterDesc h1 = randomCharacter(5u, 0), h2 = randomCharacter(6u, 0);
    h1.hat = h2.hat = -1;
    h1.height = h2.height = 1.75f;
    CHECK(greetingFits(CLIP_CHEEK_KISS, h1, h2) && greetingFits(CLIP_HUG, h1, h2), "bare heads should fit every greeting");
    h1.hat = detail::HAT_SUNHAT;
    CHECK(!greetingFits(CLIP_CHEEK_KISS, h1, h2) && !greetingFits(CLIP_HUG, h1, h2) && greetingFits(CLIP_HANDSHAKE, h1, h2),
          "a sun hat of the same height: a handshake only");
    h1.height = 1.9f;
    h2.height = 1.6f;
    CHECK(greetingFits(CLIP_HUG, h1, h2), "a clearly taller sun hat wearer can hug");
    h1.hat = detail::HAT_CAP;
    CHECK(!greetingFits(CLIP_CHEEK_KISS, h1, h2) && greetingFits(CLIP_HUG, h1, h2), "a cap: a hug, no cheek kiss");
}

// Gaze: the eyes jump to a new target at once and the head follows with a lag, then the eyes stay on it as the head
// arrives; neck and eye limits hold for a target far behind; people glance about on their own (the curious more),
// and big gaze shifts often come with a blink.
// Impacts and injuries (AnimInput::hitDir / hitStrength / hitBone, legHurt, wounded, clutch, fallDir / fallBrace,
// stance 24): flinches go along the push and recover, a belly hit folds the body over, an arm hit flings the arm, a
// leg hit buckles the knee; a heavy hit staggers the body a few catching steps along the push (the root moved by
// staggerVelocity()) with the planted feet holding; a limp shortens the stance on the hurt leg; a wounded body
// hunches; a hand holds each wound on the body's own skin standing, walking, crouched and lying hurt; a falling body
// braces with its arms towards the ground.
struct ImpactRig {
    CharacterDesc d;
    Skeleton sk;
    Animator an;
    vec3 root = vec3(0);
    void init(u32 seed, int gender) {
        d = randomCharacter(seed, gender);
        buildSkeleton(d, sk);
        an.init(&sk, 7u);
        an.setCharacter(d);
    }
    void step(AnimInput& in, float dt) {
        an.update(in, dt);
        root = root + vec3(in.localMoveDir.x, in.localMoveDir.y, 0.f) * (in.speed * dt);
    }
    vec3 joint(int b) {
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        return m[b].c[3].xyz();
    }
    vec3 palm(int s) {
        quat q;
        vec3 p;
        detail::boneModel(sk, an.pose, s ? B_HAND_R : B_HAND_L, q, p);
        return p + rotate(q, sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L]) * 0.45f;
    }
    // where a hand should hold a wound now (palm centre just off the skin)
    vec3 woundTarget(int w, int hand) {
        quat q;
        vec3 p;
        detail::boneModel(sk, an.pose, an.skinWB[w], q, p);
        return p + rotate(q, an.skinW[w]) + rotate(q, detail::woundNormal(w)) * (sk.boneRadius[hand ? B_HAND_R : B_HAND_L] * 0.75f + 0.004f);
    }
};

// Planted-foot slide (world, m/s summed into sum / n): the lower of heel and ball, compared with the same point a frame
// earlier (the root at `root`).
struct SoleTrack {
    vec3 prevH[2], prevB[2];
    bool was[2] = {false, false};
    double sum = 0.0;
    int n = 0;
    void add(const Skeleton& sk, const Pose& pose, vec3 root, const bool planted[2], float dt, bool count) {
        mat4 m[B_COUNT];
        computeMatrices(sk, pose, m, nullptr);
        for (int s = 0; s < 2; s++) {
            FootProbe fp = footPoints(sk, m, s);
            vec3 h = root + fp.heel, b = root + fp.ball;
            bool useH = h.z < b.z;
            vec3 c = useH ? h : b, p = useH ? prevH[s] : prevB[s];
            if (count && planted[s] && was[s]) {
                sum += length(vec2(c.x - p.x, c.y - p.y)) / dt;
                n++;
            }
            prevH[s] = h;
            prevB[s] = b;
            was[s] = planted[s];
        }
    }
    float mean() const { return n ? (float)(sum / n) : 0.f; }
};

void testImpacts() {
    const float dt = 1.f / 60.f;
    std::string line;
    // ---- flinches standing: chest displacement against an unhit twin at the peak and after recovery
    struct Case { vec3 dir; int bone; float k; const char* name; };
    const Case cases[] = {
        {vec3(0, -1, 0), B_CHEST, 0.3f, "front light"}, {vec3(0, 1, 0), B_CHEST, 0.3f, "back light"},
        {vec3(1, 0, 0), B_CHEST, 0.45f, "left"},        {vec3(-1, 0, 0), B_CHEST, 0.45f, "right"},
        {vec3(0, -1, 0), B_SPINE1, 0.45f, "belly"},      {vec3(0, -1, 0), B_HEAD, 0.45f, "head"},
    };
    float minAlong = 1e9f, maxRest = 0.f, bellyFold = 0.f, headVsChest = 0.f;
    for (const Case& c : cases) {
        ImpactRig a, b;
        a.init(91u, 0);
        b.init(91u, 0);
        AnimInput in;
        in.footProbes = true;
        for (int f = 0; f < 90; f++) {
            a.step(in, dt);
            b.step(in, dt);
        }
        AnimInput hit = in;
        hit.hitDir = c.dir;
        hit.hitStrength = c.k;
        hit.hitBone = c.bone;
        a.step(hit, dt);
        b.step(in, dt);
        float peak = 0.f, peakHead = 0.f;
        vec3 at(0);
        for (int f = 0; f < 70; f++) {
            a.step(in, dt);
            b.step(in, dt);
            vec3 dc = (a.joint(B_NECK) - a.joint(B_PELVIS)) - (b.joint(B_NECK) - b.joint(B_PELVIS));
            vec3 dh = (a.joint(B_HEAD) - a.joint(B_NECK)) - (b.joint(B_HEAD) - b.joint(B_NECK));
            if (length(dc) > peak) peak = length(dc), at = dc;
            peakHead = Max(peakHead, length(dh));
        }
        if (c.bone == B_CHEST) minAlong = Min(minAlong, dot(at, c.dir) / Max(peak, 1e-6f));
        if (c.bone == B_SPINE1) bellyFold = dot(at, -c.dir);
        if (c.bone == B_HEAD) headVsChest = peakHead / Max(peak, 1e-4f);
        for (int f = 0; f < 50; f++) {
            a.step(in, dt);
            b.step(in, dt);
        }
        vec3 rest = (a.joint(B_NECK) - a.joint(B_PELVIS)) - (b.joint(B_NECK) - b.joint(B_PELVIS));
        maxRest = Max(maxRest, length(rest));
        line += StrFormat(" %s %.1f cm", c.name, peak * 100.f);
    }
    printf("impacts: flinch peaks (neck against the pelvis):%s; along the push >= %.2f, belly folds forwards %.3f m, head / trunk %.1f, left after 2 s %.4f m\n",
           line.c_str(), minAlong, bellyFold, headVsChest, maxRest);
    CHECK(minAlong > 0.6f, "a flinch does not go along the push (%.2f)", minAlong);
    CHECK(bellyFold > 0.01f, "a belly hit does not fold the body over the wound (%.3f m)", bellyFold);
    CHECK(headVsChest > 0.8f, "a head hit moves the head less than the trunk (%.2f)", headVsChest);
    CHECK(maxRest < 0.01f, "a flinch does not recover (%.4f m left)", maxRest);

    // ---- arm and leg hits standing; a light flinch while walking keeps the feet planted
    {
        ImpactRig a, b;
        a.init(92u, 1);
        b.init(92u, 1);
        AnimInput in;
        in.footProbes = true;
        for (int f = 0; f < 90; f++) a.step(in, dt), b.step(in, dt);
        AnimInput hit = in;
        hit.hitDir = vec3(0, -1, 0);
        hit.hitStrength = 0.45f;
        hit.hitBone = B_FOREARM_R;
        a.step(hit, dt);
        b.step(in, dt);
        float handMove = 0.f;
        for (int f = 0; f < 40; f++) {
            a.step(in, dt);
            b.step(in, dt);
            handMove = Max(handMove, dot(a.joint(B_HAND_R) - b.joint(B_HAND_R), vec3(0, -1, 0)));
        }
        for (int f = 0; f < 60; f++) a.step(in, dt), b.step(in, dt);
        hit.hitBone = B_THIGH_L;
        a.step(hit, dt);
        b.step(in, dt);
        float drop = 0.f, kneeL = 0.f;
        for (int f = 0; f < 40; f++) {
            a.step(in, dt);
            b.step(in, dt);
            drop = Max(drop, b.joint(B_PELVIS).z - a.joint(B_PELVIS).z);
            kneeL = Max(kneeL, length(a.joint(B_CALF_L) - b.joint(B_CALF_L)));
        }
        printf("impacts: arm hit flings the hand %.3f m along the push; leg hit drops the pelvis %.3f m, moves the knee %.3f m\n", handMove, drop, kneeL);
        CHECK(handMove > 0.05f, "an arm hit does not fling the arm (%.3f m)", handMove);
        CHECK(drop > 0.012f, "a leg hit does not buckle the knee (pelvis %.3f m)", drop);
    }
    {
        // walking: light hits every 0.7 s from the side; the planted feet keep still
        ImpactRig a;
        a.init(93u, 0);
        AnimInput in;
        in.footProbes = true;
        in.speed = 1.4f;
        SoleTrack tr;
        for (int f = 0; f < 360; f++) {
            AnimInput step = in;
            if (f > 60 && f % 42 == 0) {
                step.hitDir = vec3(f % 84 ? 1.f : -1.f, -0.3f, 0.f);
                step.hitStrength = 0.4f;
                step.hitBone = B_CHEST;
            }
            a.step(step, dt);
            tr.add(a.sk, a.an.pose, a.root, a.an.planted, dt, f > 90);
        }
        float skate = tr.mean();
        printf("impacts: walking through light hits, planted feet %.4f m/s\n", skate);
        CHECK(skate < 0.02f, "the feet slide under flinches while walking (%.4f m/s)", skate);
    }

    // ---- stagger: a heavy hit from the front while standing; the root moves by staggerVelocity()
    {
        float worstDist = 1e9f, worstSkate = 0.f, worstEnd = 0.f;
        int minSteps = 100;
        for (int t = 0; t < 4; t++) {
            ImpactRig a;
            a.init(94u + t, t & 1);
            AnimInput in;
            in.footProbes = true;
            for (int f = 0; f < 90; f++) a.step(in, dt);
            const vec3 dirs[4] = {vec3(0, -1, 0), vec3(0, 1, 0), vec3(1, 0, 0), vec3(-0.7f, -0.7f, 0)};
            AnimInput hit = in;
            hit.hitDir = dirs[t];
            hit.hitStrength = 1.f;
            hit.hitBone = B_CHEST;
            vec3 start = a.root;
            int steps = 0;
            SoleTrack tr;
            float endT = -1.f;
            for (int f = 0; f < 180; f++) {
                AnimInput st = f == 0 ? hit : in;
                // the game: the ped moves with the stagger's velocity
                vec3 v = a.an.staggerVelocity();
                st.speed = length(v);
                st.localMoveDir = st.speed > 1e-3f ? vec2(v.x, v.y) / st.speed : vec2(0, 1);
                a.step(st, dt);
                steps += __builtin_popcount(a.an.footEvents);
                tr.add(a.sk, a.an.pose, a.root, a.an.planted, dt, true);
                if (endT < 0.f && f > 5 && !a.an.staggering()) endT = f * dt;
            }
            float dist = dot(a.root - start, normalize(dirs[t]));
            worstDist = Min(worstDist, dist);
            worstSkate = Max(worstSkate, tr.mean());
            minSteps = Min(minSteps, steps);
            worstEnd = Max(worstEnd, endT < 0.f ? 9.f : endT);
        }
        printf("impacts: stagger (4 directions) carries the body at least %.2f m along the push in at least %d steps, steady after %.2f s, "
               "planted feet %.4f m/s\n", worstDist, minSteps, worstEnd, worstSkate);
        CHECK(worstDist > 0.25f, "a heavy hit hardly moves the body (%.2f m)", worstDist);
        CHECK(minSteps >= 2, "the stagger takes too few catching steps (%d)", minSteps);
        CHECK(worstEnd < 2.f, "the stagger does not settle (%.2f s)", worstEnd);
        CHECK(worstSkate < 0.05f, "the feet slide while staggering (%.4f m/s)", worstSkate);
    }

    // ---- limp: the stance on the hurt (right) leg is shorter than on the good one
    {
        ImpactRig a;
        a.init(95u, 0);
        AnimInput in;
        in.footProbes = true;
        in.speed = 1.0f;
        in.legHurt[1] = 1.f;
        int planted[2] = {0, 0};
        SoleTrack tr;
        for (int f = 0; f < 600; f++) {
            a.step(in, dt);
            for (int s = 0; s < 2; s++)
                if (f > 240 && a.an.planted[s]) planted[s]++;
            tr.add(a.sk, a.an.pose, a.root, a.an.planted, dt, f > 240);
        }
        float ratio = planted[1] / (float)Max(planted[0], 1), skate = tr.mean();
        printf("impacts: limp (right leg) stance hurt / good %.2f, planted feet %.4f m/s\n", ratio, skate);
        CHECK(ratio < 0.92f, "the limp does not shorten the stance on the hurt leg (%.2f)", ratio);
        CHECK(skate < 0.02f, "the feet slide while limping (%.4f m/s)", skate);
    }

    // ---- wounded: hunched (the head lower, the chest further forward)
    {
        ImpactRig a, b;
        a.init(96u, 1);
        b.init(96u, 1);
        AnimInput in, hurt;
        in.footProbes = hurt.footProbes = true;
        hurt.wounded = 1.f;
        for (int f = 0; f < 240; f++) a.step(hurt, dt), b.step(in, dt);
        float down = b.joint(B_HEAD).z - a.joint(B_HEAD).z, fwd = a.joint(B_CHEST).y - b.joint(B_CHEST).y;
        printf("impacts: wounded stance: head %.3f m lower, chest %.3f m further forward\n", down, fwd);
        CHECK(down > 0.03f && fwd > 0.02f, "a wounded body does not hunch (head %.3f m, chest %.3f m)", down, fwd);
    }

    // ---- clutching each wound: standing, walking, crouched, lying hurt
    {
        float worst = 0.f;
        std::string bad;
        const char* wn[WOUND_COUNT] = {"", "belly", "chest", "shoulder L", "shoulder R", "thigh L", "thigh R"};
        for (int mode = 0; mode < 4; mode++)
            for (int w = WOUND_BELLY; w < WOUND_COUNT; w++) {
                if (mode == 3 && (w == WOUND_THIGH_L || w == WOUND_THIGH_R)) continue;   // lying: belly, chest, shoulders
                ImpactRig a;
                a.init(97u + (u32)w, w & 1);
                AnimInput in;
                in.footProbes = true;
                in.clutch = w;
                in.speed = mode == 1 ? 1.2f : 0.f;
                in.crouch = mode == 2;
                in.stance = mode == 3 ? 24 : 0;
                float far = 0.f;
                int hand = w == WOUND_SHOULDER_L ? 1 : (w == WOUND_SHOULDER_R ? 0 : (w == WOUND_THIGH_L ? 0 : 1));
                for (int f = 0; f < 150; f++) {
                    a.step(in, dt);
                    if (f > 90) far = Max(far, length(a.palm(hand) - a.woundTarget(w, hand)));
                }
                if (far > worst) worst = far, bad = StrFormat("%s %s", wn[w], mode == 0 ? "standing" : (mode == 1 ? "walking" : (mode == 2 ? "crouched" : "lying")));
            }
        printf("impacts: a hand on the wound (each wound standing / walking / crouched / lying): palm at most %.3f m off (%s)\n", worst, bad.c_str());
        CHECK(worst < 0.035f, "a clutching hand misses the wound (%.3f m, %s)", worst, bad.c_str());
    }

    // ---- going over: the arms brace towards the ground in the fall's direction
    {
        const vec3 dirs[4] = {vec3(0, 1, 0), vec3(0, -1, 0), vec3(-1, 0, 0), vec3(1, 0, 0)};
        float worst = 1e9f, bw = 1.f;
        for (int t = 0; t < 4; t++) {
            ImpactRig a;
            a.init(98u, t & 1);
            AnimInput in;
            in.footProbes = true;
            for (int f = 0; f < 60; f++) a.step(in, dt);
            in.fallDir = dirs[t];
            in.fallBrace = 1.f;
            for (int f = 0; f < 12; f++) a.step(in, dt);
            bw = Min(bw, a.an.braceWeight());
            vec3 c = a.joint(B_SPINE2);
            // the hand(s) reaching furthest along the fall
            float reach = Max(dot(a.joint(B_HAND_L) - c, dirs[t]), dot(a.joint(B_HAND_R) - c, dirs[t]));
            worst = Min(worst, reach);
        }
        printf("impacts: bracing to fall (4 directions): after 0.2 s weight %.2f, a hand at least %.2f m out along the fall\n", bw, worst);
        CHECK(bw > 0.9f, "the brace is too slow (%.2f after 0.2 s)", bw);
        CHECK(worst > 0.25f, "the arms do not reach out to break the fall (%.2f m)", worst);
    }

    // ---- lying hurt (stance 24): on the ground, nothing through it
    {
        ImpactRig a;
        a.init(99u, 1);
        AnimInput in;
        in.stance = 24;
        float low = 1e9f, high = 0.f;
        for (int f = 0; f < 300; f++) {
            a.step(in, dt);
            if (f < 120) continue;
            mat4 m[B_COUNT];
            computeMatrices(a.sk, a.an.pose, m, nullptr);
            for (int b = 0; b < B_FIRST_DERIVED; b++) low = Min(low, m[b].c[3].z - a.sk.boneRadius[b] * 0.5f), high = Max(high, m[b].c[3].z);
        }
        printf("impacts: lying hurt: lowest joint surface %.3f m, highest joint %.2f m\n", low, high);
        CHECK(low > -0.03f && high < 0.75f, "lying hurt is not on the ground (lowest %.3f, highest %.2f)", low, high);
    }
}

void testGaze() {
    const float dt = 1.f / 60.f;
    CharacterDesc d = randomCharacter(2024u, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    auto eyeDir = [&](const mat4* m) { return normalize(m[B_EYE_L].c[1].xyz() + m[B_EYE_R].c[1].xyz()); };
    auto headYawOf = [&](const mat4* m) {
        vec3 f = m[B_HEAD].c[1].xyz();
        return atan2f(-f.x, f.y);
    };
    Animator an;
    an.init(&sk, 3u);
    an.setCharacter(d);
    AnimInput in;
    in.lookWeight = 1.f;
    // a point 60 degrees to the left at eye height, 3 m away, then one 45 degrees to the right
    const float a1 = 1.05f, a2 = -0.8f;
    auto pointAt = [&](float a) { return vec3(-sinf(a) * 3.f, cosf(a) * 3.f, 1.6f); };
    in.lookAt = pointAt(a1);
    mat4 m[B_COUNT];
    for (int f = 0; f < 120; f++) an.update(in, dt);
    computeMatrices(sk, an.pose, m, nullptr);
    float head0 = headYawOf(m);
    // a big switch (105 degrees): the eyes lead (at their limit in the head, towards the target, after 0.1 s), the
    // head lags, then arrives and the eyes are on the target
    in.lookAt = pointAt(a2);
    float eyeLead01 = 0.f, head01 = 0.f, head06 = 0.f, eyeErrSettled = 0.f;
    for (int f = 1; f <= 90; f++) {
        an.update(in, dt);
        computeMatrices(sk, an.pose, m, nullptr);
        vec3 eyesP = (m[B_EYE_L].c[3].xyz() + m[B_EYE_R].c[3].xyz()) * 0.5f;
        vec3 want = normalize(pointAt(a2) - eyesP);
        float err = acosf(Clamp(dot(eyeDir(m), want), -1.f, 1.f));
        if (f == 6) {
            vec3 e = eyeDir(m);
            eyeLead01 = headYawOf(m) - atan2f(-e.x, e.y);   // eyes turned right of the head (the target side) = +
            head01 = headYawOf(m);
        }
        if (f == 36) head06 = headYawOf(m);
        if (f == 90) eyeErrSettled = err;
    }
    // a small switch (20 degrees, within the eyes' range): the eyes there within 0.1 s
    in.lookAt = pointAt(a2 + 0.35f);
    float eyeErr01 = 0.f;
    for (int f = 1; f <= 6; f++) an.update(in, dt);
    {
        computeMatrices(sk, an.pose, m, nullptr);
        vec3 eyesP = (m[B_EYE_L].c[3].xyz() + m[B_EYE_R].c[3].xyz()) * 0.5f;
        eyeErr01 = acosf(Clamp(dot(eyeDir(m), normalize(pointAt(a2 + 0.35f) - eyesP)), -1.f, 1.f));
    }
    computeMatrices(sk, an.pose, m, nullptr);
    float headEnd = headYawOf(m);
    float lag01 = (head01 - head0) / Max(headEnd - head0, -1e-3f + (headEnd - head0 < 0.f ? 0.f : 2e-3f));
    float frac01 = (head01 - head0) / (headEnd - head0), frac06 = (head06 - head0) / (headEnd - head0);
    (void)lag01;
    // limits: a target straight behind-left
    in.lookAt = vec3(1.5f, -2.5f, 1.6f);
    float maxEye = 0.f, maxHead = 0.f;
    for (int f = 0; f < 120; f++) {
        an.update(in, dt);
        maxEye = Max(maxEye, fabsf(an.eyeYawS));
        maxHead = Max(maxHead, fabsf(an.headYawS));
    }
    // glances of one's own, curious against incurious; blinks with big shifts
    int glances[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        Animator g;
        g.init(&sk, 11u + (u32)k);
        g.setCharacter(d);
        g.lookiness = k ? 0.95f : 0.1f;
        AnimInput gi;
        bool was = false;
        for (int f = 0; f < 60 * 120; f++) {
            g.update(gi, dt);
            bool on = g.glanceT >= 0.f;
            if (on && !was) glances[k]++;
            was = on;
        }
    }
    printf("gaze: 105 deg switch: eyes %.0f deg ahead of the head at 0.1 s, head %.0f%% of its turn at 0.1 s, %.0f%% at 0.6 s, eyes %.1f deg "
           "off once settled; 20 deg switch: eyes %.1f deg off after 0.1 s; target behind: head turn <= %.2f rad (with the chest), eyes <= "
           "%.2f rad; glances in 2 min: incurious %d, curious %d\n",
           eyeLead01 * 57.3f, frac01 * 100.f, frac06 * 100.f, eyeErrSettled * 57.3f, eyeErr01 * 57.3f, maxHead, maxEye, glances[0], glances[1]);
    CHECK(eyeLead01 > 0.4f, "the eyes should lead the head into a turn (%.2f rad)", eyeLead01);
    CHECK(eyeErr01 < 0.07f, "the eyes are slow to a nearby target (%.2f rad after 0.1 s)", eyeErr01);
    CHECK(frac01 < 0.5f && frac06 > 0.85f, "the head should lag the eyes then arrive (%.2f at 0.1 s, %.2f at 0.6 s)", frac01, frac06);
    CHECK(eyeErrSettled < 0.06f, "the eyes miss the target once settled (%.3f rad)", eyeErrSettled);
    CHECK(maxHead <= 1.36f && maxEye <= 0.56f, "gaze beyond the neck / eye limits (head %.2f, eyes %.2f)", maxHead, maxEye);
    CHECK(glances[1] > glances[0] && glances[0] >= 3 && glances[1] <= 60, "glances: incurious %d, curious %d in 2 min", glances[0], glances[1]);
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

// ------------------------------------------------------------------------------------------------
// Clothing clipping (characters pass 4): garments, straps, shoes, bags and skirts must stay outside the body in motion.
// The dressed mesh and the complete skin (built without clothes, so the parts hidden under garments are there too) are
// skinned on the CPU in poses, and three things are measured on bone-compatible pairs (the same bone, its parent or its
// child: an arm swinging into the torso is the pose's business, not the clothes'):
//  - poke-through: an outward-facing cloth vertex more than 4 mm behind the skin where that skin is shown;
//  - collapse: an outward-facing cloth vertex more than 15 mm inside the body (hidden or not);
//  - crossings: shown skin triangles that cross cloth triangles in the pose but not in the bind pose (a knee through
//    the middle of a large skirt quad has no cloth vertex inside it). Hems and facings that tuck under the skin by
//    design cross it in the bind pose already and are not counted.
// Standing and walking are checked tightly, running loosely (a flexed ankle folding the shin onto the tongue of a
// trainer, an elbow bent into a rolled cuff); sitting is reported.
namespace clip {
static u64 cellKey(int x, int y, int z) { return ((u64)(u32)(x + 100000) << 40) ^ ((u64)(u32)(y + 100000) << 20) ^ (u64)(u32)(z + 100000); }
static vec3 skinPt(const mat4* M, const u8* b, const float* w, vec3 p, bool dir) {
    vec3 r(0);
    for (int k = 0; k < 4; k++) {
        if (w[k] <= 0.f) continue;
        const mat4& m = M[b[k]];
        r += (m.c[0].xyz() * p.x + m.c[1].xyz() * p.y + m.c[2].xyz() * p.z + (dir ? vec3(0) : m.c[3].xyz())) * w[k];
    }
    return r;
}
static bool related(const Skeleton& sk, int a, int b) { return a == b || sk.parent[a] == b || sk.parent[b] == a; }
static bool clothMat(u32 mat) { return !(mat == MAT_SKIN || mat == MAT_HAIR || mat == MAT_EYE || mat == MAT_CAR_GLASS); }
static bool segTri(vec3 p, vec3 q, vec3 a, vec3 b, vec3 c) {
    vec3 d = q - p, e1 = b - a, e2 = c - a, h = cross(d, e2);
    float det = dot(e1, h);
    if (fabsf(det) < 1e-14f) return false;
    float inv = 1.f / det;
    vec3 sv = p - a;
    float u = dot(sv, h) * inv;
    if (u < 0.f || u > 1.f) return false;
    vec3 qv = cross(sv, e1);
    float v = dot(d, qv) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    float t = dot(e2, qv) * inv;
    return t >= 0.f && t <= 1.f;
}
// skin / cloth triangle pairs that cross (key: skin triangle << 32 | cloth triangle) -> the skin's depth in front of
// the cloth (its deepest vertex); vertex normals `N` orient the cloth triangles
static void crossings(const SkinnedMeshData& m, const Skeleton& sk, const std::vector<vec3>& Q, const std::vector<vec3>& N,
                      std::unordered_map<u64, float>& out) {
    out.clear();
    std::unordered_map<u64, std::vector<u32>> cells;
    const float C = 0.03f;
    auto isCloth = [&](u32 i) { return clothMat(m.verts[i].mat & 0xffu); };
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        u32 a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
        if (!isCloth(a) || !isCloth(b) || !isCloth(c)) continue;
        vec3 lo = vmin(Q[a], vmin(Q[b], Q[c])), hi = vmax(Q[a], vmax(Q[b], Q[c]));
        for (int z = (int)floorf(lo.z / C); z <= (int)floorf(hi.z / C); z++)
            for (int y = (int)floorf(lo.y / C); y <= (int)floorf(hi.y / C); y++)
                for (int x = (int)floorf(lo.x / C); x <= (int)floorf(hi.x / C); x++) cells[cellKey(x, y, z)].push_back((u32)t);
    }
    std::vector<u32> stamp(m.indices.size() / 3 + 1, 0xffffffffu);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        u32 a = m.indices[t], b = m.indices[t + 1], c = m.indices[t + 2];
        if ((m.verts[a].mat & 0xffu) != MAT_SKIN || (m.verts[b].mat & 0xffu) != MAT_SKIN || (m.verts[c].mat & 0xffu) != MAT_SKIN) continue;
        vec3 A[3] = {Q[a], Q[b], Q[c]};
        vec3 lo = vmin(A[0], vmin(A[1], A[2])), hi = vmax(A[0], vmax(A[1], A[2]));
        for (int z = (int)floorf(lo.z / C); z <= (int)floorf(hi.z / C); z++)
            for (int y = (int)floorf(lo.y / C); y <= (int)floorf(hi.y / C); y++)
                for (int x = (int)floorf(lo.x / C); x <= (int)floorf(hi.x / C); x++) {
                    auto it = cells.find(cellKey(x, y, z));
                    if (it == cells.end()) continue;
                    for (u32 ct : it->second) {
                        if (stamp[ct / 3] == (u32)t) continue;
                        stamp[ct / 3] = (u32)t;
                        const VtxSkinned& sv = m.verts[a];
                        const VtxSkinned& cv = m.verts[m.indices[ct]];
                        bool ok = false;
                        for (int i = 0; i < 4 && !ok; i++)
                            for (int j = 0; j < 4 && !ok; j++)
                                if (sv.weights[i] >= 13 && cv.weights[j] >= 13 && related(sk, sv.bones[i], cv.bones[j])) ok = true;
                        if (!ok) continue;
                        u32 ca = m.indices[ct], cb = m.indices[ct + 1], cc = m.indices[ct + 2];
                        vec3 B[3] = {Q[ca], Q[cb], Q[cc]};
                        bool hit = false;
                        for (int k = 0; k < 3 && !hit; k++)
                            hit = segTri(A[k], A[(k + 1) % 3], B[0], B[1], B[2]) || segTri(B[k], B[(k + 1) % 3], A[0], A[1], A[2]);
                        if (!hit) continue;
                        vec3 fn = cross(B[1] - B[0], B[2] - B[0]);
                        float depth = 0.f;
                        if (length2(fn) > 1e-14f) {
                            fn = normalize(fn);
                            if (dot(fn, N[ca] + N[cb] + N[cc]) < 0.f) fn = -fn;
                            for (int k = 0; k < 3; k++) depth = Max(depth, dot(A[k] - B[0], fn));
                        }
                        out[((u64)(t / 3) << 32) | (u64)(ct / 3)] = depth;
                    }
                }
    }
}
struct Stat {
    long n = 0, poke = 0, collapse = 0, cross = 0;
    float worstPoke = 0.f, worstCollapse = 0.f;
};
// One character in one pose (the bind pose crossings are the baseline).
static void measure(const SkinnedMeshData& dressed, const detail::MeshB& body, const Skeleton& sk, const Pose& pose,
                    const std::unordered_map<u64, float>& bindCross, Stat& st) {
    mat4 model[B_COUNT], sm[B_COUNT];
    computeMatrices(sk, pose, model, sm);
    // posed complete skin with its dominant bones, and which of its vertices the dressed mesh shows
    std::vector<vec3> BP, BN, BB;
    std::vector<int> BD;
    for (const detail::BVert& v : body.v) {
        if (v.mat != MAT_SKIN || v.part == detail::PART_EYE || v.part == detail::PART_FACEDETAIL || v.part == detail::PART_MOUTH) continue;
        float w[4];
        int bb = 0;
        for (int k = 0; k < 4; k++) {
            w[k] = v.sw.w[k];
            if (w[k] > w[bb]) bb = k;
        }
        BP.push_back(skinPt(sm, v.sw.b, w, v.p, false));
        BN.push_back(normalize(skinPt(sm, v.sw.b, w, v.n, true)));
        BB.push_back(v.p);
        BD.push_back(v.sw.b[bb]);
    }
    std::unordered_map<u64, std::vector<u32>> grid, shownGrid;
    for (u32 i = 0; i < (u32)BP.size(); i++) grid[cellKey((int)floorf(BP[i].x / 0.02f), (int)floorf(BP[i].y / 0.02f), (int)floorf(BP[i].z / 0.02f))].push_back(i);
    std::vector<vec3> Q(dressed.verts.size()), QN(dressed.verts.size());
    for (size_t i = 0; i < dressed.verts.size(); i++) {
        const VtxSkinned& v = dressed.verts[i];
        float w[4];
        for (int k = 0; k < 4; k++) w[k] = v.weights[k] / 255.f;
        Q[i] = skinPt(sm, v.bones, w, v.pos, false);
        QN[i] = normalize(skinPt(sm, v.bones, w, unpackNormalOct(v.normal), true));
        if ((v.mat & 0xffu) == MAT_SKIN)
            shownGrid[cellKey((int)floorf(v.pos.x / 0.02f), (int)floorf(v.pos.y / 0.02f), (int)floorf(v.pos.z / 0.02f))].push_back((u32)i);
    }
    auto shown = [&](u32 bi) {
        vec3 b = BB[bi];
        int cx = (int)floorf(b.x / 0.02f), cy = (int)floorf(b.y / 0.02f), cz = (int)floorf(b.z / 0.02f);
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    auto it = shownGrid.find(cellKey(cx + dx, cy + dy, cz + dz));
                    if (it == shownGrid.end()) continue;
                    for (u32 i : it->second)
                        if (length2(dressed.verts[i].pos - b) < 0.006f * 0.006f) return true;
                }
        return false;
    };
    for (size_t i = 0; i < dressed.verts.size(); i++) {
        const VtxSkinned& v = dressed.verts[i];
        if (!clothMat(v.mat & 0xffu)) continue;
        st.n++;
        vec3 q = Q[i];
        int cx = (int)floorf(q.x / 0.02f), cy = (int)floorf(q.y / 0.02f), cz = (int)floorf(q.z / 0.02f);
        float best = 0.06f * 0.06f;
        int bi = -1;
        for (int dz = -3; dz <= 3; dz++)
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++) {
                    auto it = grid.find(cellKey(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (u32 j : it->second) {
                        bool ok = false;
                        for (int k = 0; k < 4 && !ok; k++)
                            if (v.weights[k] >= 13 && related(sk, BD[j], v.bones[k])) ok = true;
                        if (!ok) continue;
                        float d2 = length2(BP[j] - q);
                        if (d2 < best) {
                            best = d2;
                            bi = (int)j;
                        }
                    }
                }
        if (bi < 0 || dot(QN[i], BN[bi]) < 0.2f) continue;   // (facings, linings and caps face in by design)
        float depth = -dot(q - BP[bi], BN[bi]);
        if (length((q - BP[bi]) + BN[bi] * depth) > 0.012f) continue;   // off the skin sample's tangent plane
        if (depth > 0.004f && shown((u32)bi)) {
            st.poke++;
            st.worstPoke = Max(st.worstPoke, depth);
        }
        if (depth > 0.015f) {
            st.collapse++;
            st.worstCollapse = Max(st.worstCollapse, depth);
        }
    }
    std::unordered_map<u64, float> pairs;
    crossings(dressed, sk, Q, QN, pairs);
    std::unordered_map<u32, float> skinTris;
    for (auto& kv : pairs)
        if (!bindCross.count(kv.first)) {
            float& d = skinTris[(u32)(kv.first >> 32)];
            d = Max(d, kv.second);
        }
    for (auto& kv : skinTris)
        if (kv.second >= 0.002f) st.cross++;
}
}  // namespace clip

void testClothingClip() {
    using namespace detail;
    struct Case {
        u32 seed;
        int role;
        int top, bottom, outer, bag;   // -2: keep the random pick
        float weight;                  // < 0: keep
    };
    std::vector<Case> cases;
    for (int role = 0; role < 7; role++)
        for (int k = 0; k < 2; k++) cases.push_back({7000u + (u32)role * 131u + (u32)k * 977u, role, -2, -2, -2, -2, -1.f});
    const int outerTops[6] = {TOP_TSHIRT, TOP_TSHIRT, TOP_BLOUSE, TOP_TSHIRT, TOP_TANK, TOP_DRESS_SHIRT};
    for (int oc = 0; oc < OUT_COUNT; oc++) cases.push_back({8100u + (u32)oc * 17u, 0, outerTops[oc], BOT_JEANS, oc, -1, -1.f});
    for (int b = 0; b < BAG_COUNT; b++) cases.push_back({8300u + (u32)b * 29u, 0, TOP_TSHIRT, BOT_SHORTS, -1, b, -1.f});
    for (int k = 0; k < 4; k++) cases.push_back({8500u + (u32)k * 53u, 0, k < 2 ? TOP_TSHIRT : TOP_SUNDRESS, BOT_SKIRT, -1, -1, -1.f});
    for (int k = 0; k < 3; k++) cases.push_back({8700u + (u32)k * 71u, k & 1 ? 3 : 0, k < 2 ? TOP_TSHIRT : TOP_DRESS_SHIRT, k & 1 ? BOT_SLACKS : BOT_JEANS, -2, -2, 0.95f});
    cases.push_back({9901u, 4, TOP_BIKINI, BOT_BIKINI, -1, -1, -1.f});
    cases.push_back({9902u, 2, TOP_TANK, -2, -1, -1, -1.f});
    cases.push_back({9903u, 7, -2, -2, -2, -2, -1.f});   // prison inmate's coverall
    struct PoseDef {
        Clip c;
        float t;     // fraction of the clip
        int group;   // 0 standing / walking (tight), 1 running (loose), 2 sitting (reported)
    };
    const PoseDef poses[] = {{CLIP_IDLE, 0.3f, 0}, {CLIP_WALK, 0.f, 0}, {CLIP_WALK, 0.5f, 0}, {CLIP_RUN, 0.3f, 1}, {CLIP_RUN, 0.f, 1}, {CLIP_SIT_BENCH, 0.5f, 2}};
    clip::Stat grp[3];
    long evals[3] = {0, 0, 0};
    for (const Case& cs : cases) {
        CharacterDesc d = randomCharacter(cs.seed, cs.role);
        if (cs.top != -2) d.top = cs.top;
        if (cs.bottom != -2) d.bottom = cs.bottom;
        if (cs.outer != -2) d.outer = cs.outer;
        if (cs.bag != -2) d.bag = cs.bag;
        if (cs.weight >= 0.f) d.weight = cs.weight;
        if (d.top == TOP_SUNDRESS || d.top == TOP_BIKINI || d.bottom == BOT_SKIRT) d.gender = FEMALE;
        Skeleton sk;
        buildSkeleton(d, sk);
        SkinnedMeshData dressed;
        buildCharacterMesh(d, sk, dressed);
        BodyDims D;
        computeDims(d, D);
        BuildCtx bc;
        bc.d = &d;
        bc.D = &D;
        bc.sk = &sk;
        bc.skin = d.skinTone;
        bc.lipCol = bc.palmCol = bc.lipInner = bc.skin;
        buildBody(bc);
        std::unordered_map<u64, float> bindCross;
        {
            std::vector<vec3> Q(dressed.verts.size()), N(dressed.verts.size());
            for (size_t i = 0; i < Q.size(); i++) {
                Q[i] = dressed.verts[i].pos;
                N[i] = unpackNormalOct(dressed.verts[i].normal);
            }
            clip::crossings(dressed, sk, Q, N, bindCross);
        }
        clip::Stat cst[3];
        for (const PoseDef& pd : poses) {
            Pose pose;
            sampleClip(sk, pd.c, pd.t * clipInfo(pd.c).duration, pose, cs.seed);
            clip::Stat st;
            clip::measure(dressed, bc.m, sk, pose, bindCross, st);
            for (clip::Stat* S : {&grp[pd.group], &cst[pd.group]}) {
                S->n += st.n;
                S->poke += st.poke;
                S->collapse += st.collapse;
                S->cross += st.cross;
                S->worstPoke = Max(S->worstPoke, st.worstPoke);
                S->worstCollapse = Max(S->worstCollapse, st.worstCollapse);
            }
            evals[pd.group]++;
        }
        printf("  clip seed %u role %d top %d bottom %d outer %d bag %d w %.2f: stand/walk %ld/%ld/%ld  run %ld/%ld/%ld  sit %ld/%ld/%ld "
               "(poke / collapse / crossings)\n",
               cs.seed, cs.role, d.top, d.bottom, outerFits(d) ? d.outer : -1, d.bag, d.weight, cst[0].poke, cst[0].collapse, cst[0].cross, cst[1].poke,
               cst[1].collapse, cst[1].cross, cst[2].poke, cst[2].collapse, cst[2].cross);
    }
    const char* names[3] = {"standing / walking", "running", "sitting"};
    for (int g = 0; g < 3; g++)
        printf("clothing clip, %s: %ld cloth vertex tests, poke-through %.3f%% (worst %.1f mm), collapse %.3f%% (worst %.1f mm), %.1f skin "
               "triangles through cloth per pose\n",
               names[g], grp[g].n, 100.0 * grp[g].poke / Max(grp[g].n, 1L), grp[g].worstPoke * 1000.f, 100.0 * grp[g].collapse / Max(grp[g].n, 1L),
               grp[g].worstCollapse * 1000.f, (double)grp[g].cross / Max(evals[g], 1L));
    CHECK(grp[0].poke * 1000 <= grp[0].n, "standing / walking: cloth poking through shown skin %.3f%%", 100.0 * grp[0].poke / Max(grp[0].n, 1L));
    CHECK(grp[0].collapse * 5000 <= grp[0].n, "standing / walking: cloth collapsed into the body %.3f%%", 100.0 * grp[0].collapse / Max(grp[0].n, 1L));
    CHECK(grp[0].cross <= evals[0] * 12, "standing / walking: %.1f skin triangles through cloth per pose", (double)grp[0].cross / Max(evals[0], 1L));
    CHECK(grp[1].poke * 250 <= grp[1].n, "running: cloth poking through shown skin %.3f%%", 100.0 * grp[1].poke / Max(grp[1].n, 1L));
    CHECK(grp[1].collapse * 1000 <= grp[1].n, "running: cloth collapsed into the body %.3f%%", 100.0 * grp[1].collapse / Max(grp[1].n, 1L));
    CHECK(grp[1].cross <= evals[1] * 40, "running: %.1f skin triangles through cloth per pose", (double)grp[1].cross / Max(evals[1], 1L));
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

// Faces: the data the renderer's skin, eye and hair shading reads (material param bits, see face.cpp / meshutil.cpp),
// and the facial hair: every character's skin carries its regions (lips, wet mucosa, eyelids, ears, nose, mouth) and
// one melanin value that follows the skin tone; eyeballs carry a plausible radius, teeth the enamel bit; strand cards
// carry a layer depth; full beards and stubble get many beard cards, women none; face cards stay on the head; and the
// crowd LODs keep the skin bits.
void testFaces() {
    using namespace Anim::detail;
    double tb = 0.0;
    int nChars = 0;
    std::vector<std::pair<float, int>> melByLum;
    for (u32 k = 0; k < 14; k++) {
        CharacterDesc d = randomCharacter(7100u + k * 104729u, (int)(k % 8));
        if (k == 0) d.facialHair = FH_BEARD;
        if (k == 1) d.facialHair = FH_STUBBLE;
        if (k == 0 || k == 1) d.gender = MALE;
        if (k == 2) {
            d.gender = FEMALE;
            d.facialHair = -1;
        }
        if (k == 3) d.skinTone = srgbToLinear(vec3(0.24f, 0.15f, 0.1f));   // deep
        if (k == 4) d.skinTone = srgbToLinear(vec3(0.96f, 0.82f, 0.72f));  // fair
        Skeleton sk;
        buildSkeleton(d, sk);
        SkinnedMeshData L[3];
        double t0 = TimeSeconds();
        buildCharacterMeshLods(d, sk, L, 3);
        tb += TimeSeconds() - t0;
        nChars++;
        const SkinnedMeshData& m = L[0];
        int region[8] = {0, 0, 0, 0, 0, 0, 0, 0}, eyes = 0, eyeBad = 0, teeth = 0, beardCards = 0, cardDepthBad = 0, farCards = 0;
        int shaderPupil = 0, lidOcc = 0;
        int mel = -1;
        bool melSame = true, skinBit = true;
        vec3 head = -sk.invBindModel[B_HEAD].c[3].xyz();
        for (const VtxSkinned& v : m.verts) {
            u32 mat = v.mat & 0xffu, param = (v.mat >> 8) & 0x7fffffu;
            if (mat == MAT_SKIN) {
                if (!(param & 1u)) skinBit = false;
                if (((param >> 12) & 15u) == 0u) skinBit = false;   // oiliness 1..15 (all-zero bits 1-22 read as legacy skin)
                region[(param >> 1) & 7u]++;
                int ml = (int)((param >> 19) & 15u);
                if (mel < 0) mel = ml;
                else if (ml != mel) melSame = false;
            } else if (mat == MAT_EYE) {
                if (param & 1u) teeth++;
                else {
                    eyes++;
                    float r = 0.009f + 0.00002f * (float)((param >> 2) & 255u);
                    if (r < 0.0095f || r > 0.0145f) eyeBad++;
                    if (param & 2u) {
                        // shader-pupil eyes: the tangent is the optical axis (forward, as the eyes look in bind) and the
                        // colour alpha the lids' occlusion
                        shaderPupil++;
                        vec3 tg = unpackNormalOct(v.tangent);
                        if (tg.y < 0.95f) eyeBad++;
                        if (unpackRGBA8(v.color).w < 0.999f) lidOcc++;
                    }
                }
            } else if (mat == MAT_HAIR) {
                u32 kind = param & 15u;
                if (kind == 0) continue;
                if (((param >> 20) & 7u) > 7u) cardDepthBad++;
                if (kind == 4) beardCards++;
                if ((kind == 2 || kind == 3 || kind == 4) && length(v.pos - head) > 0.2f) farCards++;
            }
        }
        CHECK(skinBit, "faces: skin without the character-skin bit or with zero oiliness (k %u)", k);
        const bool earsHidden = d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_BOB || (d.hairStyle == HAIR_CURLY && d.gender == FEMALE);
        CHECK(region[1] > 20 && region[2] > 20 && region[3] > 20 && (region[4] > 100 || earsHidden) && region[5] > 5 && region[7] > 20,
              "faces: skin regions missing (k %u): lip %d mucosa %d lid %d ear %d nose %d mouth %d", k, region[1], region[2], region[3], region[4], region[5],
              region[7]);
        CHECK(melSame && mel >= 0, "faces: melanin must be one value per character (k %u)", k);
        CHECK(eyes > 600 && eyeBad == 0, "faces: eyeballs %d, %d with an implausible radius or axis (k %u)", eyes, eyeBad, k);
        CHECK(shaderPupil == 0 || (shaderPupil == eyes && lidOcc > 50), "faces: shader-pupil eyes mixed or without lid occlusion (k %u: %d of %d, %d)", k,
              shaderPupil, eyes, lidOcc);
        CHECK(teeth > 100, "faces: teeth not flagged as enamel (k %u: %d)", k, teeth);
        CHECK(cardDepthBad == 0, "faces: card depth out of range (k %u)", k);
        CHECK(farCards == 0, "faces: %d lash / brow / beard card vertices away from the head (k %u)", farCards, k);
        if (d.gender == MALE && (d.facialHair == FH_BEARD || d.facialHair == FH_STUBBLE))
            CHECK(beardCards > (d.facialHair == FH_BEARD ? 1500 : 1000), "faces: too few beard cards (k %u, fh %d): %d vertices", k, d.facialHair, beardCards);
        if (d.gender == FEMALE) CHECK(beardCards == 0, "faces: beard cards on a woman (k %u)", k);
        melByLum.push_back(std::make_pair(dot(d.skinTone, vec3(0.3f, 0.59f, 0.11f)), mel));
        // the crowd LODs keep the skin bits (the same melanin)
        for (int l = 1; l < 3; l++) {
            int skinV = 0, withMel = 0;
            for (const VtxSkinned& v : L[l].verts) {
                if ((v.mat & 0xffu) != MAT_SKIN) continue;
                skinV++;
                if ((int)((v.mat >> 27) & 15u) == mel) withMel++;
            }
            CHECK(skinV > 0 && withMel > skinV * 9 / 10, "faces: LOD%d skin lost its shading bits (k %u: %d of %d)", l, k, withMel, skinV);
        }
    }
    // melanin follows the skin tone: a darker skin never gets less melanin
    std::sort(melByLum.begin(), melByLum.end());
    for (size_t i = 1; i < melByLum.size(); i++)
        CHECK(melByLum[i].second <= melByLum[i - 1].second, "faces: melanin %d at luminance %.3f above %d at %.3f", melByLum[i].second, melByLum[i].first,
              melByLum[i - 1].second, melByLum[i - 1].first);
    printf("faces: %d characters, LOD build avg %.1f ms\n", nChars, tb * 1000.0 / nChars);
}

// Face shape round the eyes and the mouth (face.cpp, bodymesh.cpp, hair.cpp): the head grid's normals round the eyes
// change smoothly between neighbours (the lid-row fans at the corners used to fold into pleats, and the lid mound met
// the socket in a groove); the brows sit above the lids' fold with skin between; the corneas sit behind the brow
// ridge's front in profile; and a full beard's volume builds up gradually from the lips (no shelf under the lower lip).
void testFaceShape() {
    using namespace Anim::detail;
    float worstJump = 0.f, worstMean = 0.f, minGap = 1e9f, minDepth = 1e9f, worstShelf = 0.f, worstTint = 0.f, worstTrench = 0.f;
    int minJaw = 1 << 30;
    for (u32 k = 0; k < 10; k++) {
        CharacterDesc d = randomCharacter(5300u + k * 7919u, (int)(k % 7));
        if (k < 3) {
            d.gender = MALE;
            d.facialHair = FH_BEARD;
        }
        Skeleton sk;
        buildSkeleton(d, sk);
        BodyDims D;
        computeDims(d, D);
        BuildCtx bc;
        bc.d = &d;
        bc.D = &D;
        bc.sk = &sk;
        bc.skin = d.skinTone;
        bc.lipCol = bc.skin;
        bc.palmCol = bc.skin;
        buildBody(bc);
        const HeadInfo& H = bc.head;
        const int NC = H.cols;
        // neighbouring normals round the eyes (the margins' own edges excepted)
        float worst = 0.f, sum = 0.f;
        int cnt = 0;
        for (int j = H.rowLidLo - 1; j <= H.rowBrow + 1; j++)
            for (int c = 0; c < NC; c++) {
                const BVert& v = bc.m.v[H.grid[(size_t)j * NC + c]];
                float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
                if (fabsf(at - H.thetaEye) > 30.f * kDegToRad) continue;
                if (j == H.rowEyeHi || j == H.rowEyeLo || j + 1 == H.rowEyeLo || j - 1 == H.rowEyeHi) continue;
                const BVert& a = bc.m.v[H.grid[(size_t)(j + 1) * NC + c]];
                const BVert& b = bc.m.v[H.grid[(size_t)j * NC + (c + 1) % NC]];
                float a1 = acosf(Clamp(dot(v.n, a.n), -1.f, 1.f)) * kRadToDeg, a2 = acosf(Clamp(dot(v.n, b.n), -1.f, 1.f)) * kRadToDeg;
                worst = Max(worst, Max(a1, a2));
                sum += a1 + a2;
                cnt += 2;
            }
        float mean = sum / Max(cnt, 1);
        CHECK(worst < 100.f && mean < 17.f, "face shape: eye-region normals fold (k %u: worst %.0f deg, mean %.1f deg)", k, worst, mean);
        worstJump = Max(worstJump, worst);
        worstMean = Max(worstMean, mean);
        // the brow above the fold at the pupil's column (right eye)
        vec3 e = D.J[B_EYE_R];
        int best = 0;
        float bd = 1e9f;
        for (int c = 0; c < NC; c++) {
            float pa = bc.m.v[H.grid[(size_t)H.rowEyeHi * NC + c]].pa;
            if (pa < kPi && fabsf(pa - H.thetaEye) < bd) {
                bd = fabsf(pa - H.thetaEye);
                best = c;
            }
        }
        float foldZ = bc.m.v[H.grid[(size_t)H.rowLidHi * NC + best]].p.z;
        float browLo = 1e9f;
        for (const BVert& v : bc.m.v)
            if (v.mat == MAT_HAIR && cardKind(v) == CARD_BROW && fabsf(v.p.x - e.x) < 0.004f && (v.p.x > 0.f) == (e.x > 0.f)) browLo = Min(browLo, v.p.z);
        CHECK(browLo - foldZ > 0.002f, "face shape: brow on the lid (k %u: brow %.1f mm above the fold)", k, (browLo - foldZ) * 1000.f);
        minGap = Min(minGap, browLo - foldZ);
        // the brow's tint on the skin under its strands (a brow reads as a dark shape from a few metres only through it:
        // its cards cover about half the pixels): at the pupil's column, the darkest skin among the brow's rows against
        // the forehead above them (the tint used to sit a third of the brow's height above its strands, between rows)
        {
            auto lumAt = [&](int j) { return dot(bc.m.v[H.grid[(size_t)j * NC + best]].col, vec3(0.3f, 0.59f, 0.11f)); };
            float darkest = 1e9f, fore = 0.f;
            int nf = 0;
            for (int j = H.rowLidHi + 1; j < H.rows; j++) {
                float ph = bc.m.v[H.grid[(size_t)j * NC + best]].pb * kRadToDeg;
                if (ph >= 17.5f && ph < 27.f) darkest = Min(darkest, lumAt(j));
                else if (ph >= 30.f && ph < 42.f) {
                    fore += lumAt(j);
                    nf++;
                }
            }
            float ratio = nf ? darkest / Max(fore / (float)nf, 1e-4f) : 1.f;
            CHECK(ratio < 0.8f, "face shape: no brow tint on the skin (k %u: darkest %.2f of the forehead)", k, ratio);
            worstTint = Max(worstTint, ratio);
        }
        // the cornea's apex behind the brow ridge's front (profile through the eye centre, 8-22 mm above it)
        float browY = -1e9f;
        for (float dz = 0.008f; dz <= 0.022f; dz += 0.001f) {
            float y = e.y + 0.08f;
            for (int it = 0; it < 400; it++) {
                float f = bc.sdf.eval(vec3(e.x, y, e.z + dz), MK_HEAD);
                if (f <= 0.f) break;
                y -= Max(f * 0.8f, 0.0001f);
            }
            browY = Max(browY, y);
        }
        float depth = browY - (e.y + 1.0867f * H.eyeR);
        // (a low ridge with protruding eyes keeps only a few millimetres, as some East Asian faces do)
        CHECK(depth > 0.0025f, "face shape: eyes level with the brow ridge (k %u: cornea %.1f mm behind it)", k, depth * 1000.f);
        minDepth = Min(minDepth, depth);
        // beside the eye, over the lateral orbital rim (19 mm out from the eye centre), the face runs down from the brow's
        // tail to the cheekbone without a trench at eye level: the socket's shade stops at the rim instead of running on
        // to the temple, one dark band across the face
        {
            auto frontAt = [&](float x, float z) {
                float y = e.y + 0.08f;
                for (int it = 0; it < 400; it++) {
                    float f = bc.sdf.eval(vec3(x, y, z), MK_HEAD);
                    if (f <= 0.f) break;
                    y -= Max(f * 0.8f, 0.0001f);
                }
                return y;
            };
            const float xr = e.x + (e.x > 0.f ? 1.f : -1.f) * 0.019f * D.headS, dzr = 0.014f * D.headS;
            float trench = 0.5f * (frontAt(xr, e.z + dzr) + frontAt(xr, e.z - dzr)) - frontAt(xr, e.z);
            CHECK(trench < 0.004f, "face shape: a trench beside the eye (k %u: %.1f mm deep)", k, trench * 1000.f);
            worstTrench = Max(worstTrench, trench);
        }
        // a full beard next to the lips: the shell's height over the skin within 6 mm of the lower lip
        if (d.facialHair == FH_BEARD && d.gender == MALE) {
            SkinnedMeshData m;
            buildCharacterMesh(d, sk, m);
            std::vector<vec3> lips;
            for (const VtxSkinned& v : m.verts)
                if ((v.mat & 0xffu) == MAT_SKIN && (((v.mat >> 9) & 7u) == 1u)) lips.push_back(v.pos);
            float lipZ = 1e9f;
            for (const vec3& p : lips) lipZ = Min(lipZ, p.z);
            float shelf = 0.f;
            for (const VtxSkinned& v : m.verts) {
                if ((v.mat & 0xffu) != MAT_HAIR || ((v.mat >> 8) & 15u) != 0u || v.pos.z > lipZ + 0.002f) continue;
                float dl = 1e9f;
                for (const vec3& p : lips) dl = Min(dl, length(v.pos - p));
                if (dl > 0.006f) continue;
                shelf = Max(shelf, bc.sdf.eval(v.pos, MK_HEAD));
            }
            CHECK(!lips.empty() && shelf < 0.0035f, "face shape: beard shelf under the lower lip (k %u: shell %.1f mm over the skin)", k, shelf * 1000.f);
            worstShelf = Max(worstShelf, shelf);
            // the beard wraps the angle of the jaw (its back edge follows the jaw instead of a vertical cut in front of
            // it): beard strand cards rooted behind the cheek's middle, low on the jaw
            const vec3 hj = -sk.invBindModel[B_HEAD].c[3].xyz();
            int jawCards = 0;
            for (const VtxSkinned& v : m.verts) {
                if ((v.mat & 0xffu) != MAT_HAIR || ((v.mat >> 8) & 15u) != 4u) continue;
                // (behind 86 degrees round the head from the face's front, seen from the head grid's centre; the old
                // vertical cut was at 80)
                vec3 dq = (v.pos - hj) / D.headS;
                float thq = atan2f(fabsf(dq.x), dq.y - 0.006f) * kRadToDeg;
                if (thq > 86.f && dq.z < -0.025f) jawCards++;
            }
            CHECK(jawCards > 40, "face shape: the beard stops short of the jaw's angle (k %u: %d card vertices there)", k, jawCards);
            minJaw = Min(minJaw, jawCards);
        }
    }
    printf("face shape: eye-region normals worst %.0f deg (mean <= %.1f), brow >= %.1f mm above the fold, brow tint <= %.2f of the forehead, "
           "corneas >= %.1f mm behind the brow, trench beside the eyes <= %.1f mm, beard shell <= %.1f mm over the skin by the lips, >= %d beard card "
           "vertices round the jaw's angle\n",
           worstJump, worstMean, minGap * 1000.f, worstTint, minDepth * 1000.f, worstTrench * 1000.f, worstShelf * 1000.f, minJaw);
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
    // takedown between mismatched heights: with grabTarget the attacker's forearm still reaches the victim's throat
    {
        CharacterDesc dv = randomCharacter(77u, 0), da = randomCharacter(78u, 0);
        dv.height = 1.58f;
        da.height = 1.95f;
        Skeleton sv, sa;
        buildSkeleton(dv, sv);
        buildSkeleton(da, sa);
        Animator an;
        an.init(&sa, 5u);
        const float dt = 1.f / 60.f;
        float worstT = 0.f;
        for (int f = 0; f < 120; f++) {
            float tt = f * dt;
            AnimInput in;
            in.action = f == 0 ? CLIP_TAKEDOWN_ATTACKER : -1;
            Pose vp;
            sampleClip(sv, CLIP_TAKEDOWN_VICTIM, tt, vp);
            vec3 neck = jointPos(sv, vp, B_NECK) + clipRootMotion(sv, CLIP_TAKEDOWN_VICTIM, tt);
            vec3 root = vec3(0.f, -0.55f, 0.f) + clipRootMotion(sa, CLIP_TAKEDOWN_ATTACKER, tt);
            in.grabTarget = neck - root;
            in.grabWeight = 1.f;
            an.update(in, dt);
            if (tt > 0.6f) {
                vec3 e = jointPos(sa, an.pose, B_FOREARM_R), w = jointPos(sa, an.pose, B_HAND_R), th = in.grabTarget + vec3(0.f, 0.07f, 0.04f);
                vec3 ew = w - e;
                float u = Saturate(dot(th - e, ew) / Max(length2(ew), 1e-6f));
                worstT = Max(worstT, length(e + ew * u - th));
            }
        }
        printf("takedown (1.95 m attacker, 1.58 m victim): forearm to throat %.3f m\n", worstT);
        CHECK(worstT < 0.06f, "takedown IK misses the throat (%.3f m)", worstT);
    }
    // phone frame sits on the right palm
    {
        Animator an;
        an.init(&sk, 9u);
        AnimInput in;
        in.phoneBrowse = true;
        for (int f = 0; f < 40; f++) an.update(in, 1.f / 60.f);
        mat4 m[B_COUNT];
        computeMatrices(sk, an.pose, m, nullptr);
        vec3 pp, la, sc;
        phoneFrame(sk, m, pp, la, sc);
        vec3 hr = m[B_HAND_R].c[3].xyz(), head = m[B_HEAD].c[3].xyz();
        CHECK(length(pp - hr) < 0.12f && pp.z < head.z - 0.2f && dot(sc, normalize(head - pp)) > 0.3f,
              "browsing phone frame off (%.3f from the hand, screen facing %.2f)", length(pp - hr), dot(sc, normalize(head - pp)));
    }
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
// Derived bones: the forearm roll carries exactly half of the hand's twist about the forearm axis (the rule holdGrip
// relies on), and a closed fist brings every fingertip back to the palm side of the knuckles.
void testDerivedBones() {
    CharacterDesc d = randomCharacter(77, 0);
    Skeleton sk;
    buildSkeleton(d, sk);
    for (int side = 0; side < 2; side++) {
        const bool right = side == 1;
        const int fa = right ? B_FOREARM_R : B_FOREARM_L, hb = right ? B_HAND_R : B_HAND_L, rb = right ? B_FOREARM_ROLL_R : B_FOREARM_ROLL_L;
        vec3 ax = normalize(sk.bindLocalPos[hb]);
        vec3 fing = normalize(sk.bindLocalPos[right ? B_FINGERS_R : B_FINGERS_L]);
        vec3 pn = normalize(right ? cross(vec3(0, 1, 0), fing) : cross(fing, vec3(0, 1, 0)));
        Pose p;
        for (int b = 0; b < B_COUNT; b++) p.rot[b] = quat();
        p.rootOffset = vec3(0);
        const float twist = 1.3f;
        p.rot[hb] = quatAxisAngle(ax, twist) * quatAxisAngle(normalize(cross(ax, pn)), 0.4f);
        p.rot[right ? B_FINGERS_R : B_FINGERS_L] = quatAxisAngle(normalize(cross(fing, pn)), 1.45f);
        mat4 m[B_COUNT];
        computeMatrices(sk, p, m, nullptr);
        mat3 rf(m[fa].c[0].xyz(), m[fa].c[1].xyz(), m[fa].c[2].xyz()), rr(m[rb].c[0].xyz(), m[rb].c[1].xyz(), m[rb].c[2].xyz());
        quat local = normalize(conj(quatFromMat3(rf)) * quatFromMat3(rr));
        float p2 = local.x * ax.x + local.y * ax.y + local.z * ax.z;
        float got = 2.f * atan2f(local.w < 0.f ? -p2 : p2, fabsf(local.w));
        CHECK(fabsf(got - 0.5f * twist) < 0.01f, "forearm roll twist %.3f, expected %.3f", got, 0.5f * twist);
        // fist: each fingertip (the distal bone's far end) ends up behind the knuckle line, on the palm side
        mat3 rh(m[hb].c[0].xyz(), m[hb].c[1].xyz(), m[hb].c[2].xyz());
        vec3 F = rh * fing, P = rh * pn, W = m[hb].c[3].xyz();
        for (int f = 0; f < 4; f++) {
            int b3 = phalanxBone(right, f, 2);
            mat3 r3(m[b3].c[0].xyz(), m[b3].c[1].xyz(), m[b3].c[2].xyz());
            vec3 tip = m[b3].c[3].xyz() + r3 * (normalize(sk.bindLocalPos[b3]) * sk.boneLength[b3]);
            float along = dot(tip - W, F), palmSide = dot(tip - W, P);
            CHECK(along < length(sk.bindLocalPos[right ? B_FINGERS_R : B_FINGERS_L]) && palmSide > 0.f,
                  "fist: finger %d tip at %.3f along / %.3f palm side", f, along, palmSide);
        }
    }
    printf("derived bones: forearm roll = half the hand twist, fists close\n");
}

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

int main(int argc, char** argv) {
    using namespace animtest;
    // optional filter: only the tests whose names contain argv[1] (e.g. "anim_test Locomotion")
    const char* only = argc > 1 ? argv[1] : nullptr;
    auto run = [&](const char* name, void (*fn)()) {
        if (!only || strstr(name, only)) fn();
    };
    double t0 = TimeSeconds();
    Pose warm;
    CharacterDesc d0;
    Skeleton s0;
    buildSkeleton(d0, s0);
    sampleClip(s0, CLIP_IDLE, 0.f, warm);
    printf("clip library bake: %.1f ms\n", (TimeSeconds() - t0) * 1000.0);
    run("BindPose", testBindPose);
    run("PoseRoundTrip", testPoseRoundTrip);
    run("IK", testIK);
    run("Clips", testClips);
    run("Gait", testGait);
    run("Locomotion", testLocomotion);
    run("StopsAndTurns", testStopsAndTurns);
    run("Directions", testDirections);
    run("Standing", testStanding);
    run("Greetings", testGreetings);
    run("Gaze", testGaze);
    run("Impacts", testImpacts);
    run("Poses", testPoses);
    run("Animator", testAnimator);
    run("Driving", testDriving);
    run("Melee", testMelee);
    run("Visemes", testVisemes);
    run("DerivedBones", testDerivedBones);
    run("Lods", testLods);
    run("Faces", testFaces);
    run("FaceShape", testFaceShape);
    run("Mesh", testMesh);
    run("ClothingClip", testClothingClip);
    printf("%s (%d failures)\n", gFail ? "FAILED" : "ALL PASSED", gFail);
    return gFail ? 1 : 0;
}
