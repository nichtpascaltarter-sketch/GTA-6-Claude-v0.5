// Verlet particle ragdolls: built from the current animated pose, simulated with distance constraints, joint-limit
// heuristics and collisions (ground, static colliders, vehicles), then mapped back onto the skeleton for skinning.
#include "gameworld.h"

namespace Game {

namespace ragdoll_detail {

enum RP : int {
    RP_PELVIS = 0, RP_SPINE, RP_CHEST, RP_HEAD,
    RP_SHOULDER_L, RP_ELBOW_L, RP_HAND_L, RP_SHOULDER_R, RP_ELBOW_R, RP_HAND_R,
    RP_HIP_L, RP_KNEE_L, RP_FOOT_L, RP_HIP_R, RP_KNEE_R, RP_FOOT_R,
    RP_COUNT
};

const int kParticleBone[RP_COUNT] = {
    Anim::B_PELVIS, Anim::B_SPINE2, Anim::B_CHEST, Anim::B_HEAD,
    Anim::B_UPPERARM_L, Anim::B_FOREARM_L, Anim::B_HAND_L, Anim::B_UPPERARM_R, Anim::B_FOREARM_R, Anim::B_HAND_R,
    Anim::B_THIGH_L, Anim::B_CALF_L, Anim::B_FOOT_L, Anim::B_THIGH_R, Anim::B_CALF_R, Anim::B_FOOT_R,
};

const float kParticleRadius[RP_COUNT] = {0.13f, 0.13f, 0.14f, 0.11f, 0.06f, 0.05f, 0.05f, 0.06f, 0.05f, 0.05f,
                                         0.08f, 0.06f, 0.06f, 0.08f, 0.06f, 0.06f};
const float kParticleMass[RP_COUNT] = {12.f, 9.f, 11.f, 5.f, 2.5f, 1.8f, 0.6f, 2.5f, 1.8f, 0.6f, 5.f, 3.5f, 1.2f, 5.f, 3.5f, 1.2f};

struct LinkDef {
    int a, b;
    float stiff;
    bool minOnly;   // only prevents getting closer than (rest * minFrac)
    float minFrac;
};

const LinkDef kLinks[] = {
    // spine and head
    {RP_PELVIS, RP_SPINE, 1.f, false, 0.f}, {RP_SPINE, RP_CHEST, 1.f, false, 0.f}, {RP_CHEST, RP_HEAD, 1.f, false, 0.f},
    // shoulders / arms
    {RP_CHEST, RP_SHOULDER_L, 1.f, false, 0.f}, {RP_CHEST, RP_SHOULDER_R, 1.f, false, 0.f}, {RP_SHOULDER_L, RP_SHOULDER_R, 1.f, false, 0.f},
    {RP_SHOULDER_L, RP_ELBOW_L, 1.f, false, 0.f}, {RP_ELBOW_L, RP_HAND_L, 1.f, false, 0.f},
    {RP_SHOULDER_R, RP_ELBOW_R, 1.f, false, 0.f}, {RP_ELBOW_R, RP_HAND_R, 1.f, false, 0.f},
    // hips / legs
    {RP_PELVIS, RP_HIP_L, 1.f, false, 0.f}, {RP_PELVIS, RP_HIP_R, 1.f, false, 0.f}, {RP_HIP_L, RP_HIP_R, 1.f, false, 0.f},
    {RP_HIP_L, RP_KNEE_L, 1.f, false, 0.f}, {RP_KNEE_L, RP_FOOT_L, 1.f, false, 0.f},
    {RP_HIP_R, RP_KNEE_R, 1.f, false, 0.f}, {RP_KNEE_R, RP_FOOT_R, 1.f, false, 0.f},
    // torso bracing (keeps the rib cage/pelvis block rigid-ish)
    {RP_SHOULDER_L, RP_HIP_L, 0.9f, false, 0.f}, {RP_SHOULDER_R, RP_HIP_R, 0.9f, false, 0.f},
    {RP_SHOULDER_L, RP_HIP_R, 0.6f, false, 0.f}, {RP_SHOULDER_R, RP_HIP_L, 0.6f, false, 0.f},
    {RP_SPINE, RP_SHOULDER_L, 0.8f, false, 0.f}, {RP_SPINE, RP_SHOULDER_R, 0.8f, false, 0.f},
    {RP_SPINE, RP_HIP_L, 0.8f, false, 0.f}, {RP_SPINE, RP_HIP_R, 0.8f, false, 0.f},
    {RP_PELVIS, RP_CHEST, 0.5f, false, 0.f},
    // neck bending limits
    {RP_HEAD, RP_SHOULDER_L, 0.4f, true, 0.8f}, {RP_HEAD, RP_SHOULDER_R, 0.4f, true, 0.8f}, {RP_HEAD, RP_SPINE, 0.5f, true, 0.85f},
    // elbows / knees can't fold completely
    {RP_SHOULDER_L, RP_HAND_L, 0.5f, true, 0.35f}, {RP_SHOULDER_R, RP_HAND_R, 0.5f, true, 0.35f},
    {RP_HIP_L, RP_FOOT_L, 0.5f, true, 0.4f}, {RP_HIP_R, RP_FOOT_R, 0.5f, true, 0.4f},
    // legs don't pass through each other too much
    {RP_KNEE_L, RP_KNEE_R, 0.3f, true, 0.6f}, {RP_FOOT_L, RP_FOOT_R, 0.3f, true, 0.4f},
};
constexpr int kLinkCount = sizeof(kLinks) / sizeof(kLinks[0]);

vec3 orthoRef(vec3 d, vec3 r) {
    vec3 o = r - d * dot(r, d);
    float l = length(o);
    if (l < 1e-4f) {
        o = fabsf(d.z) < 0.9f ? cross(d, vec3(0, 0, 1)) : cross(d, vec3(1, 0, 0));
        l = length(o);
    }
    return o / l;
}

// Rotation mapping frame (d0, r0) onto (d1, r1); d = primary axis, r = twist reference.
mat3 alignFrames(vec3 d0, vec3 r0, vec3 d1, vec3 r1) {
    vec3 a0 = normalize(d0), b0 = orthoRef(a0, r0), c0 = cross(a0, b0);
    vec3 a1 = normalize(d1), b1 = orthoRef(a1, r1), c1 = cross(a1, b1);
    mat3 F0(a0, b0, c0), F1(a1, b1, c1);
    return F1 * transpose(F0);
}

mat3 rotOf(const mat4& m) { return mat3(m.c[0].xyz(), m.c[1].xyz(), m.c[2].xyz()); }
mat4 makeMat4(const mat3& r, vec3 p) { return mat4(vec4(r.c[0], 0), vec4(r.c[1], 0), vec4(r.c[2], 0), vec4(p, 1)); }

}  // namespace ragdoll_detail

using namespace ragdoll_detail;

struct Ragdoll {
    vec3 p[RP_COUNT], prev[RP_COUNT];
    float rest[kLinkCount];
    mat4 bindModel[Anim::B_COUNT];   // model-space bind transforms
    vec3 bindPos[RP_COUNT];
    float settle = 0.f;
    float time = 0.f;
    bool frozen = false;
    int contactVehicle = -1;
};

void freeRagdoll(Ragdoll*& r) {
    delete r;
    r = nullptr;
}

namespace ragdoll_detail {

void buildBindModel(const Anim::Skeleton& sk, mat4* out) {
    for (int b = 0; b < Anim::B_COUNT; b++) out[b] = inverse(sk.invBindModel[b]);
}

// Map particles back to bone world transforms, then to skinning matrices relative to `origin`.
void extractPose(const Ragdoll& r, const Anim::Skeleton& sk, vec3 origin, mat4* bonesWorld, mat4* skin) {
    const mat4* B = r.bindModel;
    auto bpos = [&](int b) { return B[b].c[3].xyz(); };
    vec3 bindRight(1, 0, 0), bindUp(0, 0, 1);
    vec3 curRight = r.p[RP_HIP_R] - r.p[RP_HIP_L];
    vec3 curRightS = r.p[RP_SHOULDER_R] - r.p[RP_SHOULDER_L];
    mat3 world[Anim::B_COUNT];
    vec3 wpos[Anim::B_COUNT];
    bool done[Anim::B_COUNT] = {};
    auto setSeg = [&](int bone, int pa, int pb, vec3 d0, vec3 r0, vec3 r1) {
        mat3 Q = alignFrames(d0, r0, r.p[pb] - r.p[pa], r1);
        world[bone] = Q * rotOf(B[bone]);
        wpos[bone] = r.p[pa];
        done[bone] = true;
        return Q;
    };
    // torso
    mat3 Qp = setSeg(Anim::B_PELVIS, RP_PELVIS, RP_SPINE, r.bindPos[RP_SPINE] - r.bindPos[RP_PELVIS], bindRight, curRight);
    setSeg(Anim::B_SPINE2, RP_SPINE, RP_CHEST, r.bindPos[RP_CHEST] - r.bindPos[RP_SPINE], bindRight, (curRight + curRightS) * 0.5f);
    mat3 Qc = setSeg(Anim::B_CHEST, RP_CHEST, RP_HEAD, r.bindPos[RP_HEAD] - r.bindPos[RP_CHEST], bindRight, curRightS);
    // head: position at the particle, orientation from the chest->head direction with the shoulder line as twist
    {
        mat3 Q = alignFrames(r.bindPos[RP_HEAD] - r.bindPos[RP_CHEST], bindRight, r.p[RP_HEAD] - r.p[RP_CHEST], curRightS);
        world[Anim::B_HEAD] = Q * rotOf(B[Anim::B_HEAD]);
        wpos[Anim::B_HEAD] = r.p[RP_HEAD];
        done[Anim::B_HEAD] = true;
    }
    // limbs: twist reference follows the parent segment's rotated reference
    struct Limb {
        int upperBone, lowerBone, pa, pb, pc;
        bool arm;
    };
    const Limb limbs[4] = {
        {Anim::B_UPPERARM_L, Anim::B_FOREARM_L, RP_SHOULDER_L, RP_ELBOW_L, RP_HAND_L, true},
        {Anim::B_UPPERARM_R, Anim::B_FOREARM_R, RP_SHOULDER_R, RP_ELBOW_R, RP_HAND_R, true},
        {Anim::B_THIGH_L, Anim::B_CALF_L, RP_HIP_L, RP_KNEE_L, RP_FOOT_L, false},
        {Anim::B_THIGH_R, Anim::B_CALF_R, RP_HIP_R, RP_KNEE_R, RP_FOOT_R, false},
    };
    for (const Limb& l : limbs) {
        mat3 Qparent = l.arm ? Qc : Qp;
        vec3 ref0 = l.arm ? vec3(0, 1, 0) : vec3(0, 1, 0);   // forward in bind (character faces +Y)
        vec3 ref1 = Qparent * ref0;
        mat3 Qu = setSeg(l.upperBone, l.pa, l.pb, r.bindPos[l.pb] - r.bindPos[l.pa], ref0, ref1);
        setSeg(l.lowerBone, l.pb, l.pc, r.bindPos[l.pc] - r.bindPos[l.pb], ref0, Qu * ref0);
    }
    // remaining bones: rigid relative to their parent in bind pose (parents always have lower indices)
    for (int b = 0; b < Anim::B_COUNT; b++) {
        if (done[b]) continue;
        int par = sk.parent[b];
        if (par < 0) {
            // root: below the pelvis on the ground plane
            world[b] = mat3();
            wpos[b] = vec3(r.p[RP_PELVIS].x, r.p[RP_PELVIS].y, origin.z);
            done[b] = true;
            continue;
        }
        mat3 Rp = world[par];
        mat3 bindRelR = transpose(rotOf(B[par])) * rotOf(B[b]);
        vec3 bindRelP = transpose(rotOf(B[par])) * (bpos(b) - bpos(par));
        world[b] = Rp * bindRelR;
        wpos[b] = wpos[par] + Rp * bindRelP;
        done[b] = true;
    }
    for (int b = 0; b < Anim::B_COUNT; b++) {
        bonesWorld[b] = makeMat4(world[b], wpos[b]);
        skin[b] = makeMat4(world[b], wpos[b] - origin) * sk.invBindModel[b];
    }
}

}  // namespace ragdoll_detail

void GameWorld::knockDown(int pid, vec3 impulse) {
    if (pid < 0 || !peds[pid].used) return;
    Ped& p = peds[pid];
    if (p.state == PS_INVEHICLE || p.charIndex < 0) return;
    const CharEntry& ce = chars[p.charIndex];
    if (!p.ragdoll) {
        Ragdoll* r = new Ragdoll();
        buildBindModel(ce.skel, r->bindModel);
        quat q = quatAxisAngle(vec3(0, 0, 1), p.yaw);
        vec3 base = p.pos.toVec3();
        float dt = 1.f / 60.f;
        for (int i = 0; i < RP_COUNT; i++) {
            int b = kParticleBone[i];
            r->bindPos[i] = r->bindModel[b].c[3].xyz();
            vec3 lp = p.bones[b].c[3].xyz();
            r->p[i] = base + rotate(q, lp);
            r->prev[i] = r->p[i] - p.vel * dt;
        }
        for (int k = 0; k < kLinkCount; k++) r->rest[k] = length(r->bindPos[kLinks[k].a] - r->bindPos[kLinks[k].b]);
        p.ragdoll = r;
    }
    Ragdoll* r = p.ragdoll;
    r->frozen = false;
    r->settle = 0.f;
    float total = 0.f;
    for (float m : kParticleMass) total += m;
    float dt = 1.f / 60.f;
    for (int i = 0; i < RP_COUNT; i++) {
        float w = (i == RP_CHEST || i == RP_HEAD || i == RP_SHOULDER_L || i == RP_SHOULDER_R || i == RP_SPINE) ? 1.35f : (i >= RP_HIP_L ? 0.6f : 1.f);
        vec3 dv = impulse / total * w;
        r->prev[i] -= dv * dt;
    }
    if (p.state != PS_DEAD) {
        p.state = PS_RAGDOLL;
        p.stateTime = 0.f;
    }
    p.aiming = p.firing = false;
}

void GameWorld_updateRagdoll(GameWorld& g, Ped& p, float dt) {
    Ragdoll& r = *p.ragdoll;
    const CharEntry& ce = g.chars[p.charIndex];
    if (!r.frozen) {
        r.time += dt;
        int sub = 2;
        float h = Min(dt, 1.f / 30.f) / sub;
        for (int s = 0; s < sub; s++) {
            // integrate
            for (int i = 0; i < RP_COUNT; i++) {
                vec3 v = (r.p[i] - r.prev[i]) * 0.995f;
                r.prev[i] = r.p[i];
                r.p[i] += v + vec3(0, 0, -9.81f) * (h * h);
            }
            for (int it = 0; it < 6; it++) {
                for (int k = 0; k < kLinkCount; k++) {
                    const LinkDef& L = kLinks[k];
                    vec3 d = r.p[L.b] - r.p[L.a];
                    float len = length(d);
                    if (len < 1e-6f) continue;
                    float rest = r.rest[k];
                    float target = rest;
                    if (L.minOnly) {
                        float mn = rest * L.minFrac;
                        if (len >= mn) continue;
                        target = mn;
                    }
                    float wa = 1.f / kParticleMass[L.a], wb = 1.f / kParticleMass[L.b];
                    float corr = (len - target) / len * L.stiff / (wa + wb);
                    r.p[L.a] += d * (corr * wa);
                    r.p[L.b] -= d * (corr * wb);
                }
                // hinge limits: knees bend forward, elbows backward (relative to the torso)
                vec3 right = normalize(r.p[RP_HIP_R] - r.p[RP_HIP_L] + r.p[RP_SHOULDER_R] - r.p[RP_SHOULDER_L]);
                vec3 upT = normalize(r.p[RP_CHEST] - r.p[RP_PELVIS]);
                vec3 fwd = normalize(cross(upT, right));
                auto hinge = [&](int a, int m, int c, float sign) {
                    vec3 mid = (r.p[a] + r.p[c]) * 0.5f;
                    float v = dot(r.p[m] - mid, fwd) * sign;
                    if (v < 0.f) r.p[m] -= fwd * (v * sign * 0.5f);
                };
                hinge(RP_HIP_L, RP_KNEE_L, RP_FOOT_L, 1.f);
                hinge(RP_HIP_R, RP_KNEE_R, RP_FOOT_R, 1.f);
                hinge(RP_SHOULDER_L, RP_ELBOW_L, RP_HAND_L, -1.f);
                hinge(RP_SHOULDER_R, RP_ELBOW_R, RP_HAND_R, -1.f);
                // collisions
                for (int i = 0; i < RP_COUNT; i++) {
                    float rad = kParticleRadius[i];
                    vec3& q = r.p[i];
                    Phys::GroundHit gh = Phys::gCollision->ground(q.x, q.y, q.z + 0.4f, 0.2f);
                    if (q.z < gh.z + rad) {
                        float pen = gh.z + rad - q.z;
                        q.z += pen;
                        // friction: kill tangential motion proportionally to the penetration
                        vec3 v = q - r.prev[i];
                        float fr = Saturate(0.35f + pen * 8.f);
                        r.prev[i].x = Lerp(r.prev[i].x, q.x, fr);
                        r.prev[i].y = Lerp(r.prev[i].y, q.y, fr);
                        (void)v;
                    }
                    vec3 push, n;
                    if (it == 5 && Phys::gCollision->capsuleOverlap(q - vec3(0, 0, rad), rad, rad * 2.f, push, n)) q += push;
                }
            }
            // vehicles push bodies
            std::vector<int> vl;
            g.vehiclesNear(r.p[RP_PELVIS].xy(), 8.f, vl);
            for (int vi : vl) {
                Vehicle& v = g.vehicles[vi];
                const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
                mat3 R = v.sim.body.rotMat(), Rt = transpose(R);
                vec3 c = v.sim.body.pos.toVec3() + R * spec.boxCenter;
                for (int i = 0; i < RP_COUNT; i++) {
                    vec3 l = Rt * (r.p[i] - c);
                    vec3 he = spec.boxHalf + vec3(kParticleRadius[i]);
                    if (fabsf(l.x) >= he.x || fabsf(l.y) >= he.y || fabsf(l.z) >= he.z) continue;
                    float px = he.x - fabsf(l.x), py = he.y - fabsf(l.y), pz = he.z - fabsf(l.z);
                    vec3 nl = px < py && px < pz ? vec3(l.x >= 0 ? 1.f : -1.f, 0, 0) : (py < pz ? vec3(0, l.y >= 0 ? 1.f : -1.f, 0) : vec3(0, 0, l.z >= 0 ? 1.f : -1.f));
                    float pen = Min(px, Min(py, pz));
                    vec3 n = R * nl;
                    r.p[i] += n * pen;
                    // inherit vehicle surface velocity
                    vec3 vv = v.sim.body.pointVelocity(r.p[i] - v.sim.body.pos.toVec3());
                    vec3 pv = (r.p[i] - r.prev[i]) / h;
                    vec3 dv = vv - pv;
                    if (dot(dv, n) > 0.f) r.prev[i] -= n * (dot(dv, n) * h);
                }
            }
        }
        // settle detection
        float maxV = 0.f;
        for (int i = 0; i < RP_COUNT; i++) maxV = Max(maxV, length(r.p[i] - r.prev[i]) / (Min(dt, 1.f / 30.f) / 2.f));
        if (maxV < 0.35f) r.settle += dt;
        else r.settle = 0.f;
        if (p.state == PS_DEAD && r.settle > 2.f) r.frozen = true;
    }
    // keep the ped origin at the pelvis ground projection
    vec3 pel = r.p[RP_PELVIS];
    float gz = g.groundHeight(pel.x, pel.y, pel.z + 0.3f);
    p.pos = dvec3(pel.x, pel.y, gz);
    p.vel = (r.p[RP_PELVIS] - r.prev[RP_PELVIS]) / (Min(dt, 1.f / 30.f) * 0.5f);
    extractPose(r, ce.skel, p.pos.toVec3(), p.bones, p.skin);
    // get up when settled
    if (p.state == PS_RAGDOLL && ((r.settle > 0.7f && r.time > 1.2f) || r.time > 6.f)) {
        vec3 right = r.p[RP_HIP_R] - r.p[RP_HIP_L];
        vec3 up = r.p[RP_CHEST] - r.p[RP_PELVIS];
        vec3 front = cross(up, right);  // chest facing direction (character faces +Y: up x right = forward)
        bool faceDown = front.z < 0.f;
        vec3 headDir = r.p[RP_HEAD] - r.p[RP_PELVIS];
        // GET_UP_FRONT starts face down with the head towards +Y (facing), GET_UP_BACK on the back with the head
        // towards -Y (feet forward); both with the pelvis above the origin
        float yawHead = atan2f(-headDir.x, headDir.y);
        p.yaw = faceDown ? yawHead : yawHead + kPi;
        // crossfade the get-up from the ragdoll's final pose (bones relative to the new root) instead of cutting
        {
            mat4 rootInv = inverse(makeMat4(mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), p.yaw)), p.pos.toVec3()));
            mat4 model[Anim::B_COUNT];
            for (int b = 0; b < Anim::B_COUNT; b++) model[b] = rootInv * p.bones[b];
            Anim::Pose from;
            Anim::poseFromModelSpace(ce.skel, model, from);
            p.anim.blendFrom(from, 0.3f);
        }
        freeRagdoll(p.ragdoll);
        p.state = PS_GETUP;
        p.stateTime = 0.f;
        p.vel = vec3(0);
        p.grounded = true;
        p.pendingAction = faceDown ? Anim::CLIP_GET_UP_FRONT : Anim::CLIP_GET_UP_BACK;
        g.animatePed(p, 0.f);
    }
}

}  // namespace Game
