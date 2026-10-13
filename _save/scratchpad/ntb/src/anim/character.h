// Characters: skeleton, procedural body/outfit meshes, procedural animation clips and an animation runtime.
// Model space: +X right, +Y forward (the character faces +Y), +Z up; origin on the ground between the feet.
#pragma once
#include "../render/mesh.h"

namespace Anim {

// Skeleton layout used by every character (ragdoll-ready: the physics system builds bodies from these bones).
enum Bone : u8 {
    B_ROOT = 0,     // on the ground, moves with the character
    B_PELVIS,
    B_SPINE1, B_SPINE2, B_CHEST,
    B_NECK, B_HEAD,
    B_CLAVICLE_L, B_UPPERARM_L, B_FOREARM_L, B_HAND_L,
    B_CLAVICLE_R, B_UPPERARM_R, B_FOREARM_R, B_HAND_R,
    B_THIGH_L, B_CALF_L, B_FOOT_L, B_TOE_L,
    B_THIGH_R, B_CALF_R, B_FOOT_R, B_TOE_R,
    B_FINGERS_L, B_THUMB_L, B_FINGERS_R, B_THUMB_R,
    B_JAW, B_EYE_L, B_EYE_R,
    B_COUNT
};

struct Skeleton {
    int parent[B_COUNT];
    vec3 bindLocalPos[B_COUNT];   // translation relative to parent in bind pose
    quat bindLocalRot[B_COUNT];   // rotation relative to parent in bind pose
    mat4 invBindModel[B_COUNT];   // inverse of bind-pose model-space transform
    float boneLength[B_COUNT];
    float boneRadius[B_COUNT];    // approximate limb radius (for ragdoll capsules / hit detection)
};

// A pose: local rotations (relative to bind) + root translation. Blending is done on this representation.
struct Pose {
    quat rot[B_COUNT];            // local rotation (absolute local, i.e. replaces bindLocalRot)
    vec3 rootOffset;              // model-space offset of B_PELVIS from its bind position (bounce, crouch)
};

enum Gender : u8 { MALE = 0, FEMALE = 1 };

struct CharacterDesc {
    u32 seed = 1;
    Gender gender = MALE;
    float height = 1.78f;         // meters
    float weight = 0.5f;          // 0 thin .. 1 heavy
    float muscle = 0.4f;          // 0 .. 1
    float age = 0.35f;            // 0 young adult .. 1 elderly
    vec3 skinTone = vec3(0.6f, 0.42f, 0.32f);
    int hairStyle = 0;            // 0 bald/buzz ... see character agent docs
    vec3 hairColor = vec3(0.1f, 0.07f, 0.05f);
    int top = 0, bottom = 0, shoes = 0, hat = -1, glasses = -1, facialHair = -1;
    vec3 topColor = vec3(0.8f), bottomColor = vec3(0.2f, 0.25f, 0.4f), shoeColor = vec3(0.1f);
    int role = 0;                 // 0 civilian, 1 police, 2 gang, 3 business, 4 beach, 5 worker, 6 medic
};

// Generate a varied random civilian description for a region/role (deterministic from seed).
CharacterDesc randomCharacter(u32 seed, int role = 0);
void buildSkeleton(const CharacterDesc& d, Skeleton& out);
void buildCharacterMesh(const CharacterDesc& d, const Skeleton& skel, SkinnedMeshData& out);

enum Clip : u16 {
    CLIP_IDLE = 0, CLIP_IDLE_LOOK, CLIP_WALK, CLIP_JOG, CLIP_RUN, CLIP_SPRINT, CLIP_WALK_BACK, CLIP_STRAFE_L, CLIP_STRAFE_R,
    CLIP_CROUCH_IDLE, CLIP_CROUCH_WALK, CLIP_JUMP_START, CLIP_FALL, CLIP_LAND,
    CLIP_AIM_PISTOL, CLIP_AIM_RIFLE, CLIP_FIRE_PISTOL, CLIP_FIRE_RIFLE, CLIP_RELOAD, CLIP_THROW,
    CLIP_PUNCH_L, CLIP_PUNCH_R, CLIP_KICK, CLIP_BLOCK, CLIP_HIT_FRONT, CLIP_HIT_BACK, CLIP_STAGGER, CLIP_DEATH_FRONT, CLIP_DEATH_BACK,
    CLIP_SIT_DRIVE, CLIP_SIT_PASSENGER, CLIP_RIDE_BIKE, CLIP_ENTER_CAR_L, CLIP_EXIT_CAR_L, CLIP_ENTER_CAR_R, CLIP_EXIT_CAR_R,
    CLIP_SWIM_IDLE, CLIP_SWIM, CLIP_CLIMB, CLIP_VAULT,
    CLIP_COWER, CLIP_HANDS_UP, CLIP_FLEE, CLIP_TALK, CLIP_TALK_PHONE, CLIP_SIT_BENCH, CLIP_SMOKE, CLIP_DANCE, CLIP_WAVE,
    CLIP_POINT, CLIP_CHEER, CLIP_LEAN_WALL, CLIP_SUNBATHE, CLIP_JOG_IDLE, CLIP_GET_UP_FRONT, CLIP_GET_UP_BACK,
    CLIP_COUNT
};

struct ClipInfo {
    const char* name;
    float duration;   // seconds
    bool loop;
    float speed;      // root speed in m/s for locomotion clips (0 otherwise)
};
const ClipInfo& clipInfo(Clip c);
// Sample a clip at time t (seconds; wrapped for looping clips) into a pose.
void sampleClip(const Skeleton& skel, Clip c, float t, Pose& out, u32 variationSeed = 0);
void blendPoses(const Pose& a, const Pose& b, float w, Pose& out);
// Blend `layer` onto `base` only for the upper body (spine and up) with weight w.
void blendUpperBody(const Pose& base, const Pose& layer, float w, Pose& out);
// Convert a pose to model-space bone matrices and skinning matrices (model * invBind).
void computeMatrices(const Skeleton& skel, const Pose& pose, mat4* modelSpace, mat4* skinning);
// Two-bone IK helper (e.g. plant feet on uneven ground, hands on steering wheel / weapon).
void solveTwoBoneIK(const Skeleton& skel, Pose& pose, Bone upper, Bone lower, Bone end, vec3 targetModel, vec3 poleModel, float weight);

// High level animation state machine driven by gameplay each frame.
struct AnimInput {
    float speed = 0;          // horizontal speed (m/s)
    float turnRate = 0;       // rad/s (for leaning)
    vec2 localMoveDir = vec2(0, 1);  // movement direction relative to facing (for strafing)
    bool crouch = false, aiming = false, firing = false, reloading = false, inAir = false, swimming = false;
    int weaponKind = 0;       // 0 none, 1 pistol, 2 rifle/smg/shotgun, 3 melee, 4 thrown
    float aimPitch = 0;       // radians, + up
    int action = -1;          // one-shot Clip to play (punch, hit, death, enter car, ...), -1 none
    int stance = 0;           // 0 normal, 1 driving, 2 passenger, 3 bike, 4 cower, 5 hands up, 6 sit, 7 talk, 8 phone, 9 dance, ...
    float groundOffsetL = 0, groundOffsetR = 0;  // foot IK height offsets from terrain probes (m)
};

struct Animator {
    const Skeleton* skel = nullptr;
    Pose pose;
    float time = 0;
    // internal state (implementation defined)
    float phase = 0, locoBlend = 0, aimBlend = 0, crouchBlend = 0, airBlend = 0, swimBlend = 0;
    int action = -1;
    float actionTime = 0;
    int stance = 0;
    float stanceBlend = 0;
    int prevStance = 0;
    u32 seed = 0;
    bool actionFinished = true;
    void init(const Skeleton* s, u32 variationSeed);
    void update(const AnimInput& in, float dt);
    bool actionDone() const { return actionFinished; }
};

}  // namespace Anim
