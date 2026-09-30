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
    // melee: strikes start and end in the stance-19 guard of their weapon (fists / knife / bat, see
    // AnimInput::meleeKind) and play on the upper body while strafing; contact frames: clipEventTime()
    CLIP_HOOK, CLIP_UPPERCUT, CLIP_BAT_SWING, CLIP_BAT_OVERHEAD, CLIP_KNIFE_SLASH, CLIP_KNIFE_STAB,
    CLIP_DODGE_BACK, CLIP_DODGE_L, CLIP_DODGE_R,   // 1.2 m of root motion (clipRootMotion), full body
    CLIP_HIT_HEAD, CLIP_HIT_BODY,
    CLIP_KNOCKOUT,                                 // collapses forward, ends lying face down (head +Y), holds
    CLIP_TAKEDOWN_ATTACKER, CLIP_TAKEDOWN_VICTIM,  // synced rear choke: attacker 0.55 m behind the victim, same facing
    CLIP_COUNTER,                                  // from the blocking guard: parry, then a two-handed shove
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
// Recover a Pose from model-space bone matrices (e.g. a ragdoll expressed relative to the ped root) for blending.
void poseFromModelSpace(const Skeleton& skel, const mat4* modelSpace, Pose& out);
// Time (s) of a clip's key moment: the contact frame of strikes and kicks, the release of THROW, the hand reaching
// the door handle in ENTER_CAR_*, the choke grab of the TAKEDOWN pair; -1 when the clip has none.
float clipEventTime(Clip c);
// Root motion: displacement of the ped origin from the start of the clip to time t (s), in the ped's model space at
// the clip start (x right, y forward), scaled for this skeleton. Only the dodges carry root motion (their pose is in
// place relative to an origin that follows this curve); every other clip plays in place. The animator never moves
// the ped: gameplay moves the capsule by the change of this curve each frame.
vec3 clipRootMotion(const Skeleton& skel, Clip c, float t);
// Grip of a hand-held object, from computeMatrices' model-space matrices: `pos` = centre of the fist, `axis` = the
// direction a handle held in the fist points out of the thumb side (towards a bat's barrel or a knife's tip),
// `palm` = palm normal. Melee weapons attach to the right hand (right = true); for two-handed swings the animator
// keeps the left hand on the same handle, 9.5 cm below the right fist (towards the knob).
void handGrip(const Skeleton& skel, const mat4* modelSpace, bool right, vec3& pos, vec3& axis, vec3& palm);

// High level animation state machine driven by gameplay each frame.
struct AnimInput {
    float speed = 0;          // horizontal speed (m/s)
    float turnRate = 0;       // rad/s (for leaning)
    vec2 localMoveDir = vec2(0, 1);  // movement direction relative to facing (for strafing); while driving
                                     // (stance 1) x = steering input -1 left .. +1 right (turns the wheel)
    bool crouch = false, aiming = false, firing = false, reloading = false, inAir = false, swimming = false;
    int weaponKind = 0;       // 0 none, 1 pistol, 2 rifle/smg/shotgun, 3 melee, 4 thrown
    float aimPitch = 0;       // radians, + up
    int action = -1;          // one-shot Clip to play (punch, hit, death, enter car, ...), -1 none
    int stance = 0;           // 0 normal, 1 driving, 2 passenger, 3 bike, 4 cower, 5 hands up, 6 sit, 7 talk, 8 phone, 9 dance, ...
                              // 10 smoke, 11 lean on wall, 12 sunbathe, 13 jog in place, 14 look around, 15 wave, 16 cheer,
                              // 17 point, 18 crouch, 19 fighting guard, 20 blocking guard (19/20: guard of meleeKind; the
                              // upper body stays in guard while the legs walk / strafe)
    float groundOffsetL = 0, groundOffsetR = 0;  // foot IK height offsets from terrain probes (m)
    // optional (defaults keep the automatic behaviour)
    int meleeKind = 0;        // melee weapon in hand for the fighting guards: 0 fists, 1 knife, 2 bat (two-handed)
    vec3 lookAt = vec3(0);    // point to look at in the ped's model space (x right, y forward, z up)
    float lookWeight = 0;     // 0 none .. 1 head/neck/eyes turn towards lookAt (limited, smoothed)
    float mouthOpen = -1;     // lip-sync jaw opening 0..1 from speech (-1 = clip/automatic)
    // lip-sync mouth shapes (Oculus viseme order: 0 sil, 1 PP, 2 FF, 3 TH, 4 DD, 5 kk, 6 CH, 7 SS, 8 nn, 9 RR, 10 aa,
    // 11 E, 12 I, 13 O, 14 U): the current viseme at visemeWeight, crossfading towards visemeNext by visemeBlend (0..1)
    int viseme = -1;          // -1 = none (mouth at rest / clip)
    float visemeWeight = 0;
    int visemeNext = -1;
    float visemeBlend = 0;
    vec3 groundNormal = vec3(0, 0, 1);  // terrain normal under the ped in its model space (feet align to slopes)
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
    // additional internal state (implementation defined)
    float speedS = 0, leanS = 0, fireT = 10.f, reloadW = 0, reloadT = 0, airT = 0, stanceTime = 0;
    float footL = 0, footR = 0, snapW = 0, snapRate = 5.f, moveW = 0, legScale = 1.f, styleF = 0, steerS = 0;
    vec2 dirS = vec2(0, 1);
    int lastInAction = -1;        // AnimInput::action of the previous update (actions start on a change)
    float blinkT = -1.f, blinkNext = 2.f, gazeNext = 1.f, lookW = 0.f, slopeS = 0.f;
    vec2 gaze, gazeTarget, slopeN;
    bool extBlend = false;        // blendFrom() pending: keep its crossfade when the next action starts
    bool actionUpper = false, wasReloading = false;
    int stanceClip = -1;          // clip id currently driving the stance (guards depend on meleeKind)
    float gripW = 0.f, gripD = 0.f;   // two-handed bat grip: left hand IK weight, left grip distance along the bat
    float actYaw0 = 0.f;          // pelvis yaw of the action's first frame (upper-body actions keep the hip turn)
    Pose snap;                    // pose captured at a discontinuity (crossfaded out over 1/snapRate s)
    void init(const Skeleton* s, u32 variationSeed);
    void update(const AnimInput& in, float dt);
    // Crossfade from an externally produced pose (e.g. the ragdoll when a get-up starts) over `seconds`.
    void blendFrom(const Pose& from, float seconds);
    void faceOverlay(const AnimInput& in, float dt);   // internal: look-at, gaze, blinks, jaw (called by update)
    bool actionDone() const { return actionFinished; }
};

}  // namespace Anim
