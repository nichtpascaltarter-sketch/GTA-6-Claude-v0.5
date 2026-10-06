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
    // speech / expression (no physics): lips and tongue, driven by the visemes (AnimInput::viseme*)
    B_LIP_UPPER, B_LIP_LOWER, B_LIP_CORNER_L, B_LIP_CORNER_R, B_TONGUE,
    B_BROW_L, B_BROW_R,   // eyebrows (raise / knit), with the forehead skin under them
    // Derived bones (no physics): computeMatrices / boneModel set their local rotations from controller bones, so
    // their Pose::rot entries are ignored and every clip / IK / blend keeps working on the controllers alone.
    //  - forearm roll (child of B_FOREARM_*, half way down the forearm): exactly half of B_HAND_*'s twist about the
    //    forearm axis (the hand's swing-twist decomposition about bindLocalPos[B_HAND_*]); the forearm skin between
    //    the elbow and the wrist blends forearm -> roll -> hand, so a pronated grip twists the forearm instead of
    //    pinching the wrist.
    //  - finger phalanges (proximal 1, middle 2, distal 3; the proximal ones are children of B_HAND_*): B_FINGERS_*
    //    is the curl controller, its rotation angle about the fingers' flexion axis (holdGrip / clips' convention:
    //    cross(finger dir, palm normal); fingers 0..1 -> 1.45 rad) drives all three joints of every finger (cascaded,
    //    fingers converging as they close, wrapped round holdGrip's handle at ~0.9, a fist at 1).
    //  - thumb (metacarpal 1 from the CMC joint at B_THUMB_*'s position, proximal 2, distal 3): B_THUMB_* is the
    //    thumb controller, its angle about the opposition axis (thumb 0..1 -> 0.9 rad) poses the whole thumb: open,
    //    relaxed beside the index finger, round a handle's far side, across the closed fingers.
    //  B_FINGERS_* / B_THUMB_* carry no skin; their matrices stay valid (handGrip reads B_HAND_* and B_FINGERS_*'s
    //  bind offset only).
    B_FOREARM_ROLL_L, B_FOREARM_ROLL_R,
    B_INDEX1_L, B_INDEX2_L, B_INDEX3_L, B_MIDDLE1_L, B_MIDDLE2_L, B_MIDDLE3_L,
    B_RING1_L, B_RING2_L, B_RING3_L, B_PINKY1_L, B_PINKY2_L, B_PINKY3_L, B_THUMB1_L, B_THUMB2_L, B_THUMB3_L,
    B_INDEX1_R, B_INDEX2_R, B_INDEX3_R, B_MIDDLE1_R, B_MIDDLE2_R, B_MIDDLE3_R,
    B_RING1_R, B_RING2_R, B_RING3_R, B_PINKY1_R, B_PINKY2_R, B_PINKY3_R, B_THUMB1_R, B_THUMB2_R, B_THUMB3_R,
    B_COUNT
};
// First derived bone; per hand the digits run index, middle, ring, pinky, thumb, three bones each.
const int B_FIRST_DERIVED = B_FOREARM_ROLL_L;
const int kHandPhalanges = B_INDEX1_R - B_INDEX1_L;   // 15 per hand
inline int phalanxBone(bool right, int finger, int joint) {   // finger 0 index .. 3 pinky, 4 thumb; joint 0..2
    return (right ? B_INDEX1_R : B_INDEX1_L) + finger * 3 + joint;
}

struct Skeleton {
    int parent[B_COUNT];
    vec3 bindLocalPos[B_COUNT];   // translation relative to parent in bind pose
    quat bindLocalRot[B_COUNT];   // rotation relative to parent in bind pose
    mat4 invBindModel[B_COUNT];   // inverse of bind-pose model-space transform
    float boneLength[B_COUNT];
    float boneRadius[B_COUNT];    // approximate limb radius (for ragdoll capsules / hit detection)
    // Constants of the derived bones, per hand (0 left, 1 right), filled by buildSkeleton: the forearm axis, the curl
    // controllers' axes, the fingers' flexion axes and the thumb's key directions / bind directions (pose.cpp).
    struct DerivedRig {
        vec3 forearmAxis, fingerCtlAxis, thumbCtlAxis;
        vec3 flexAxis[4];
        vec3 thumbKey[3][4];      // metacarpal, proximal, distal phalanx direction at the thumb curl keys
    } derived[2];
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
    int role = 0;                 // 0 civilian, 1 police, 2 gang, 3 business, 4 beach, 5 worker, 6 medic, 7 prison inmate
    int ancestry = -1;            // face shape tendencies: 0 Latin American / Mediterranean, 1 African / Caribbean,
                                  // 2 European, 3 East Asian, 4 mixed / other; -1 = from the skin tone
    // Layering and accessories (-1 / 0 = none; ignored where they do not go with the top, see character.cpp)
    int outer = -1;               // open layer over the top: 0 overshirt, 1 zip hoodie, 2 cardigan, 3 light jacket, 4 vest,
                                  // 5 blazer
    vec3 outerColor = vec3(0.3f);
    int bag = -1;                 // 0 backpack, 1 crossbody bag (on the back of the hip), 2 shoulder tote (see bagSide)
    vec3 bagColor = vec3(0.1f);
    u32 extras = 0;               // accessory bits (ACC_* in anim_internal.h): bracelets, lanyard badge, sunglasses pushed
                                  // up, rolled sleeves, ...
};

// Generate a varied random civilian description for a region/role (deterministic from seed).
CharacterDesc randomCharacter(u32 seed, int role = 0);
// Side a one-shoulder bag hangs on (0 left, 1 right): a crossbody bag sits behind that hip with its strap over the
// other shoulder, a tote hangs from that shoulder (the arm on that side can hold the strap / swing less).
inline int bagSide(const CharacterDesc& d) { return (int)((d.seed >> 5) & 1u); }
void buildSkeleton(const CharacterDesc& d, Skeleton& out);
void buildCharacterMesh(const CharacterDesc& d, const Skeleton& skel, SkinnedMeshData& out);
// Level-of-detail meshes on the same skeleton and skin weights (same silhouette and colours): out[0] full detail
// (~21-30k tris, close: ~8k of head and face, plus hair / beard / brow / lash strand cards as the last triangles of
// the index buffer, see the card conventions in anim_internal.h), out[1] ~4.5k tris (about 15-40 m, no strand cards:
// the hair shells remain), out[2] ~1.5k tris (40 m+, no lid tucks / mouth interior / fingers / small accessories).
// lodCount 1..3; building them together costs one full build plus the decimation.
void buildCharacterMeshLods(const CharacterDesc& d, const Skeleton& skel, SkinnedMeshData* out, int lodCount = 3);
void buildCharacterMeshLod(const CharacterDesc& d, const Skeleton& skel, int lod, SkinnedMeshData& out);

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
    // greetings between two people (both play the same clip at the same moment, facing each other with their roots
    // pairDistance() apart; see pairDistance for the partner input): an embrace with a short sway and pats on the back
    // (each has the right arm over the partner's shoulder, the left under the arm, heads to the right), a handshake
    // (right hands, two pumps), a kiss on the right cheek (a hand on the partner's upper arm)
    CLIP_HUG, CLIP_HANDSHAKE, CLIP_CHEEK_KISS,
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
// Paired greetings (CLIP_HUG, CLIP_HANDSHAKE, CLIP_CHEEK_KISS): the distance (m) between the two partners' roots, from
// both skeletons (chest depth, arm reach). Place them facing each other that far apart and start the clip on both in
// the same update. Each partner's AnimInput::grabTarget holds the other's B_CHEST joint (hug, handshake) or B_HEAD
// joint (cheek kiss) in its own model space with grabWeight 1 while the clip plays: hands then land on the partner's
// back / meet the partner's hand, and faces meet, whatever the two heights (without it the clips fit a partner of the
// same size).
float pairDistance(Clip c, const Skeleton& a, const Skeleton& b);
// Whether a greeting suits both people's hats (pick another one when it does not): a brim round the head (sun hat,
// fedora) goes through the partner's head where the heads come side by side (a cheek kiss; a hug unless its wearer
// is clearly the taller), and so does a peak or a hard hat's brim in a cheek kiss.
bool greetingFits(Clip c, const CharacterDesc& a, const CharacterDesc& b);
// Grip of a hand-held object, from computeMatrices' model-space matrices: `pos` = centre of the fist, `axis` = the
// direction a handle held in the fist points out of the thumb side (towards a bat's barrel or a knife's tip),
// `palm` = palm normal. Melee weapons attach to the right hand (right = true); for two-handed swings the animator
// keeps the left hand on the same handle, 9.5 cm below the right fist (towards the knob).
void handGrip(const Skeleton& skel, const mat4* modelSpace, bool right, vec3& pos, vec3& axis, vec3& palm);
// Phone prop frame for every phone pose (call at the ear, browsing, the idle phone check): `pos` = centre of the
// phone, `longAxis` = its length direction (towards the top edge), `screen` = screen normal. It lies in the right palm
// (screen away from the palm), or between both palms when the hands hold it together.
void phoneFrame(const Skeleton& skel, const mat4* modelSpace, vec3& pos, vec3& longAxis, vec3& screen);
// Hand on a handle (first-person weapon holds): IK the arm so the fist centre (handGrip's `pos`) lands on `pos`, with
// the handle axis out of the thumb side along `axis` and the palm facing `palm` (all model space); the elbow bends
// towards the model-space point `pole`; fingers / thumb curl 0 (open) .. 1 (closed round the handle).
void holdGrip(const Skeleton& skel, Pose& pose, bool right, vec3 pos, vec3 axis, vec3 palm, vec3 pole, float fingers, float thumb,
              float weight);

// A car door to get in or out through (CLIP_ENTER_CAR_* / CLIP_EXIT_CAR_*; _L = a door on the vehicle's left): the door
// and its seat as Vehicles::DoorSpec / SeatSpec describe them, here in the ped's model space (x right, y forward, z up,
// origin at its feet) while the clip plays. The game holds the ped still meanwhile: getting in starts where
// carEntrySpot() says (the walk to the door ends there), getting out is rooted where carExitSpot() says (the ped
// stands there at the end), and the occupant swings the door by Animator::carDoor().
struct CarDoorInfo {
    bool valid = false;          // false: the plain clip (no door, no seat)
    vec3 seat;                   // seated hip point
    vec3 fwd = vec3(0, 1, 0);    // the vehicle's forward axis (unit)
    vec3 out = vec3(-1, 0, 0);   // the door's outward normal (unit, horizontal)
    vec3 hinge, axis;            // the door's hinge point and axis (a positive turn swings it open)
    float maxOpen = 1.15f;       // fully open (rad)
    vec3 handle, handleIn;       // outer and inner handle of the shut door
    vec3 grip;                   // top of the shut door near its rear edge (the roof rail is just above it)
    vec3 front, rear;            // front and rear end of the opening at the sill's outer edge
    vec3 top[6];                 // the opening's top edge (the roof rail, down the A-pillar): 6 points rear to front
    float sillZ = 0.3f;          // top of the sill (the cabin floor)
    float roofZ = 1.3f;          // underside of the opening's top over the seat
    float headZ = 1.4f;          // the cabin's ceiling (headliner) over the seat (an open top: 9)
    // the driver's steering wheel in this frame: rim centre (a point like the others), unit column axis pointing at
    // the driver, rim radius (as AnimInput::wheelC / wheelN / wheelR describe it): the hands go to its rim getting in
    // (wheelR 0: the animator's typical rim)
    vec3 wheelC = vec3(0), wheelN = vec3(0);
    float wheelR = 0.f;
    bool driver = true;          // seated: the hands go to the steering wheel (else to the lap)
    bool belt = true;            // buckle up once in (and unbuckle first to get out): Animator::seatBelt()
};

// Where a ped stands to start getting in through `door`, and where it stands once out (the exit clip's root), given
// the door in the VEHICLE's frame (+y forward, z up, origin on the ground): position and facing yaw (0 = facing +y,
// positive = turned left).
void carEntrySpot(const CarDoorInfo& door, vec3& pos, float& yaw);
void carExitSpot(const CarDoorInfo& door, vec3& pos, float& yaw);
// Length (s) of a car clip with this door (the plain clip's length when the door is not valid).
float carClipLength(int clip, const CarDoorInfo& door);

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
                              // upper body stays in guard while the legs walk / strafe), 21 sit on the ground (beach towel),
                              // 22 lie face down (sunbathing), 23 wait in a queue (idle variations come more often),
                              // 24 down hurt: lying on the back, knees up, writhing (a hand on the wound with clutch),
                              // 25 cuffed: wrists crossed at the small of the back, shoulders rolled, head down (the
                              // upper body holds while the legs stand or walk; no props, no hand on a wound)
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
    // facial expression: -1 automatic (pain on hits, fear when cowering, anger in fights, smiles when dancing /
    // cheering, a per-ped resting mood), 0 neutral, 1 smile, 2 sad, 3 angry, 4 fear, 5 surprise, 6 pain
    int expression = -1;
    float expressionWeight = 1;
    float brow = 0;           // eyebrow pulse on top of the expression: + raise (stressed words, questions) .. - knit
    float nod = 0;            // head nod pulse 0..1 (chin down) on stressed words / agreement
    // conversation body language (standing or walking; ignored during actions, aiming, fights, vehicles, scenarios)
    bool speaking = false;    // talking: hands come up into gestures (palm-up explaining one / both hands, beat-ready),
                              // head tilts between phrases, weight on one hip now and then
    float beat = 0;           // beat gesture pulse 0..1 (accent envelope): a short down-stroke of the gesturing hand(s)
    float gestureAmount = 1;  // how animated: 0 still .. 1 normal .. 1.5 heated
    bool listening = false;   // listener: crossed arms / hand on hip / hands in pockets, occasional nods, head tilts
                              // (turn it to the speaker with lookAt / lookWeight)
    bool phoneCall = false;   // phone held to the right ear (standing or walking), the left arm stays free / gestures
    bool phoneBrowse = false; // looking down at a phone held at chest height: both hands standing, the right hand only
                              // while walking / jogging (~0.3 s blend). Place the prop with phoneFrame().
    // takedown contact for mismatched heights: the victim's B_NECK joint in this (attacker's) model space while playing
    // CLIP_TAKEDOWN_ATTACKER; the choke arm and the hand behind the head are IK'd onto it (weight 0 = as authored).
    // With no clip playing (on foot, any stance): a hand holds on to grabTarget (model space: the middle of what it
    // closes round, e.g. an escorted suspect's upper arm), the one on the nearer shoulder's side, fading in / out over
    // ~0.2 s with grabWeight and letting go when it is beyond the arm's reach; the body stands or walks on as usual.
    vec3 grabTarget = vec3(0);
    float grabWeight = 0;
    vec3 groundNormal = vec3(0, 0, 1);  // terrain normal under the ped in its model space (feet align to slopes)
    // groundOffsetL/R were probed under Animator::footProbe() (where each foot is / is about to land) instead of
    // below the hips: the animator then takes them as the ground under each foot as it is (no slope extrapolation)
    bool footProbes = false;
    // someone on foot passing close (standing or walking): their chest point in this ped's model space, their velocity
    // relative to this ped (model space, m/s) and how close it is (0 none .. 1 closing within ~1 m): the near shoulder
    // turns back to let them by, the body leans and the feet side-step away (the heading stays)
    vec3 passBy = vec3(0);
    vec2 passVel = vec2(0);
    float passWeight = 0;
    // the ground under a foot's toe tip or heel where Animator::wantsEdgeProbe asked for it (at Animator::edgeProbe,
    // like groundOffsetL/R): a foot down across a curb's or a stair's edge rests on the higher side
    float groundEdgeL = 0.f, groundEdgeR = 0.f;
    bool edgeProbes = false;
    // steering wheel of the vehicle driven (stance 1), model space (origin 0.5 m below the seat hip point, the
    // vehicle's yaw): rim centre, unit column axis pointing at the driver, rim radius; wheelR 0 = a car's typical rim
    vec3 wheelC = vec3(0), wheelN = vec3(0);
    float wheelR = 0.f;
    // seated in a vehicle (stances 1 / 2): the cabin's ceiling over the seat (the headliner), model space z (origin
    // 0.5 m below the seat hip point); 0 = no roof to keep under. A head that would be up into it fits under it: the
    // hips slide forward and the back slouches, the head tips to keep the eyes level (the feet stay put).
    float headroom = 0.f;
    float seatFloor = 0.f;    // ... and the floor under the feet (same frame; 0 = not known): the feet stand on it
    // prop in hand (the game's CarryProp order): 0 none, 1 roller suitcase (right hand, trailing behind), 2 shopping bag
    // (left, hanging), 3 coffee (right; the left while the phone is up: phoneW / browseW > 0.3, or in a
    // rightHandBusy stance), 4 briefcase (left, hanging), 5 umbrella (right: open with carryOpen, else furled and
    // hanging), 6 fishing rod (right, up and forward), 7 binoculars (on the chest: hands free), 8 surfboard (right arm
    // round it). Standing postures and fidgets leave a busy hand alone.
    int carry = 0;
    bool carryOpen = false;
    // ---- impacts and injuries (combat, AI, traffic)
    // A hit or a bump this update: set for the one update of the impact (several in quick succession add up).
    //   hitDir: the direction the impact pushes the body, model space (a shot from straight ahead pushes towards -y,
    //     a car from the left towards +x); its length does not matter.
    //   hitStrength 0..1: below 0.5 a flinch of the trunk, head and arms layered over whatever the legs do (standing,
    //     walking, running, aiming); from 0.5 a heavy hit that also knocks the body off balance: it catches itself with
    //     a few quick steps in the push direction (the game moves the ped by staggerVelocity() meanwhile) and steadies,
    //     unless the game knocks it down (fallBrace, then the ragdoll).
    //   hitBone: the bone struck (Bone; -1 = the chest): a head hit snaps the head, a chest hit throws the trunk back,
    //     a belly hit folds it over the wound, an arm hit flings the arm, a leg hit buckles that knee. After a heavy
    //     hit a free hand goes to the wound for a moment (keep it there with `clutch`).
    vec3 hitDir = vec3(0);
    float hitStrength = 0;
    int hitBone = -1;
    // Lasting injuries (0 healthy .. 1 badly hurt; hold them for as long as they last):
    //   legHurt[0 / 1]: the left / right leg: a limp (a short, stiff-kneed step on it, the body dipping over it and
    //     hurrying off it, the hurt foot barely clearing the ground); best with a slower speed from the game (~1 m/s);
    //   wounded: low health: a hunched, guarded stance and walk, little arm swing, laboured breathing;
    //   clutch (Wound): where a free hand holds a wound - standing, walking, crouched and lying hurt (stance 24). A
    //     hand holding a weapon is not free: the other one takes the belly / chest; an aimed weapon keeps both.
    float legHurt[2] = {0, 0};
    float wounded = 0;
    int clutch = 0;
    // Going over (the game is about to hand the body to the ragdoll): fallDir = the direction the body falls (model
    // space, horizontal), fallBrace 0..1 = going over: the arms reach out to break the fall, the chin tucks, the knees
    // give and the body tips into the fall. Hold it until braceWeight() is near 1 (~0.15 s) before starting the
    // ragdoll so that it starts from the bracing pose.
    vec3 fallDir = vec3(0);
    float fallBrace = 0;
    // ---- getting in / out of a car through an opening door: set it from the update that starts the clip until the
    // clip ends (see CarDoorInfo); without it the car clips play as before (no door, no seat)
    CarDoorInfo car;
};

// Where a hand holds a wound (AnimInput::clutch).
enum Wound : int { WOUND_NONE = 0, WOUND_BELLY, WOUND_CHEST, WOUND_SHOULDER_L, WOUND_SHOULDER_R, WOUND_THIGH_L, WOUND_THIGH_R, WOUND_COUNT };

// Stances whose clip holds something in the right hand (8 a phone at the ear, 10 a cigarette): a carried cup goes to
// the left hand and the game puts right-hand loads away meanwhile (carryArms and the game's effectiveCarry agree).
inline bool rightHandBusy(int stance) { return stance == 8 || stance == 10; }

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
    float hipTurn = 0.f;          // hips (and legs) turned towards the travel direction, + = right (the trunk faces ahead)
    bool hipBack = false;         // travelling well behind: the legs back-pedal along it rather than walk forwards
    int lastInAction = -1;        // AnimInput::action of the previous update (actions start on a change)
    float blinkT = -1.f, blinkNext = 2.f, gazeNext = 1.f, lookW = 0.f, slopeS = 0.f;
    vec2 gaze, gazeTarget, slopeN;
    bool extBlend = false;        // blendFrom() pending: keep its crossfade when the next action starts
    bool actionUpper = false, wasReloading = false;
    int stanceClip = -1;          // clip id currently driving the stance (guards depend on meleeKind)
    float gripW = 0.f, gripD = 0.f;   // two-handed bat grip: left hand IK weight, left grip distance along the bat
    float actYaw0 = 0.f;          // pelvis yaw of the action's first frame (upper-body actions keep the hip turn)
    float mouth[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};   // smoothed viseme shape (jaw, lips, corners, tongue)
    float exprS[8] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};   // smoothed expression (jaw, lips, corners, lids, brows)
    float browS = 0.f, nodS = 0.f;
    int idleVar = -1, idleCount = 0;   // idle variation playing (internal clip id) while standing around
    float idleVarT = 0.f, idleVarDur = 0.f, idleNext = 4.f, idleVarW = 0.f;
    // conversation layer state
    int gestMode = 0;
    float gestT = 0.f, gestDur = 0.f, gestR = 0.f, gestL = 0.f, palmR = 0.f, palmL = 0.f, beatS = 0.f, phoneW = 0.f;
    float tiltS = 0.f, tiltTarget = 0.f, tiltNext = 0.f, nodNext = 3.f, nodPhase = -1.f, autoNod = 0.f;
    float browseW = 0.f, browseL = 0.f, grabW = 0.f, holdW = 0.f;
    int holdSide = 1;             // the hand holding on to AnimInput::grabTarget outside clips (0 left, 1 right)
    float crownH = 0.12f;         // the top of the head (hair included) over the head joint (setCharacter)
    void conversation(const AnimInput& in, float dt, Pose& p);   // internal: gestures, listener cues, phone at the ear
    Pose snap;                    // pose captured at a discontinuity (crossfaded out over 1/snapRate s)
    // ---- per-person motion (setCharacter; defaults from the seed alone): walking style (detail::GaitStyle), arm
    //      swing scale, posture, cadence, arm clearance of a wide body, idle fidget set and timing, how much the
    //      person looks around
    int gaitStyle = 0;
    float armSwingK = 1.f, postureLean = 0.f, headPitchAdd = 0.f, cadenceK = 1.f, armOut = 0.f, energy = 0.5f;
    float lookiness = 0.5f, fidgetRate = 1.f;
    float heavyK = 0.f, athleticK = 0.f;   // build: heavy (wider base, more sway) / athletic (springier) 0..1
    u32 fidgetMask = 0xffffffffu;
    // ---- feet planted in the world (model space of the ped, carried by the root motion): contact pivot, yaw,
    //      correction of the animated foot (decays after lift-off), procedural steps while standing / turning
    float footHeel = 0.05f, footBall = 0.13f, footAnkleH = 0.08f;   // foot geometry (from the skeleton)
    vec3 plantP[2], plantCorr[2], stepFrom[2], probeP[2];
    float plantYaw[2] = {0.f, 0.f}, corrYaw[2] = {0.f, 0.f}, stepT[2] = {-1.f, -1.f}, stepDur[2] = {0.35f, 0.35f};
    float stepFromYaw[2] = {0.f, 0.f}, stepLift[2] = {0.f, 0.f};
    vec3 stepTo[2];               // a step's landing footprint (heel point) and yaw, held still for its last quarter
    float stepToYaw[2] = {0.f, 0.f};
    float legSink = 0.f;          // pelvis lowered so planted feet stay within reach
    float groundRaw[2] = {0.f, 0.f};          // last update's probed ground offsets (heights kept with the world)
    float rootVz = 0.f;                       // the root's vertical speed, read off the ground under planted feet
    bool plantedPrev[2] = {false, false};     // planted one update earlier (both probes under the same footprint)
    bool footHold[2] = {false, false};        // let go at toe-off but still on the ground: held until it lifts
    float pivotW[2] = {0.f, 0.f};             // pivoting on the ball in a quick turn (heel raised) 0..1
    float groundAhead[2] = {0.f, 0.f};        // a swinging foot's ground where it will land (probed every other update)
    bool probeAhead[2] = {false, false};      // the probe asked for is that landing spot (else near the foot)
    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
    vec3 edgeP[2];                            // a foot down near a step edge: where its toe tip / heel ground is wanted
    u8 edgeKind[2] = {0, 0};                  // the point asked for (0 none, 1 toe tip, 2 heel), alternating while down
    float groundToe[2] = {0.f, 0.f};          // the ground found there, above the slope through the foot's own (kept with
    float groundHeel[2] = {0.f, 0.f};         // the world; -1 not probed)
    float edgeLift[2] = {0.f, 0.f};           // the foot raised onto the higher side of an edge under it (m)
    bool planted[2] = {false, false};
    float pinZ[2] = {0.f, 0.f};       // height correction holding a planted sole on the ground (eases out after lift-off)
    float plantAge[2] = {0.f, 0.f};   // time since the foot was planted (the correction eases in)
    float plantOn = 0.f;          // foot planting weight (off in vehicles, actions, scenarios that move the feet)
    float bodyLag = 0.f;          // body yaw behind the root while turning on the spot (the feet step round)
    float headLead = 0.f;         // head / neck yaw leading into turns
    float accS = 0.f, accV = 0.f, prevSpeed = 0.f;   // start / stop lean (damped spring on the acceleration)
    float stepShift = 0.f;        // pelvis weight shift over the standing foot during a step
    u32 footEvents = 0;           // bit 0 / 1: left / right foot touched down in the last update (footsteps)
    quat armRest[2];              // upper arms hanging at rest (arm swing amplitude is scaled about it)
    // ---- standing life: weight on one leg (0 left .. 1 right), breathing (own rate, faster after exertion),
    //      fidgets layered over the upper body or the legs
    float standW = 0.5f, standTarget = 0.f, standNext = 3.f;
    float breathPh = 0.f, breathRate = 0.25f, exertion = 0.f;
    int fidgetVar = -1, fidgetCount = 0;
    float fidgetT = 0.f, fidgetDur = 0.f, fidgetNext = 5.f, fidgetW = 0.f;
    float standV = 0.f, standK = 5.f;   // weight shift: speed, spring rate (per person)
    float settleT = -1.f;         // time to the unloaded foot's settling step after a weight shift (-1 none)
    int stepReq = -1;             // foot asked to settle: 0 left, 1 right, 2 whichever is further out (-1 none)
    float breath = 0.f;           // breath now (0 exhaled .. 1 inhaled)
    quat restUp[8];               // spine, neck, head, jaw, eyes of the plain standing pose (layer offsets)
    vec3 restRoot;
    quat restArm[2][3];           // upper arm, forearm, hand of the plain standing pose (left, right)
    vec3 skinP[3];                // this body's skin where posed hands rest on it (bind pose, from the pelvis joint): the
                                  // right flank (hand on the hip), the small of the back, the belly
    // ---- gaze: the head's turn added to the animated head (rad, a critically damped spring lagging the target), the
    //      eyes leading it (fast), glances of the person's own (lookiness) when the game gives no target
    float headYawS = 0.f, headPitchS = 0.f, headYawV = 0.f, headPitchV = 0.f, eyeYawS = 0.f, eyePitchS = 0.f;
    float glanceT = -1.f, glanceDur = 0.f, glanceNext = 4.f, glanceYaw = 0.f, glancePitch = 0.f;
    float tgtYawPrev = 0.f, tgtPitchPrev = 0.f;
    int carryClip[2] = {-1, -1};  // carrying: arm pose clip per arm (left, right) and its weight
    float carryW[2] = {0.f, 0.f};
    float bagSwing[2] = {1.f, 1.f};   // arm swing on each side (a shoulder bag's side swings less)
    // ---- impacts and injuries: the flinch (trunk bend pitch / roll / twist, head whip, knee dip, arm jerks: damped
    //      springs kicked by each hit), the stagger (push velocity the body catches with steps, its lean), the hand
    //      reflexively at a fresh wound, the clutching / limp / hunch weights, going over
    vec3 flinch = vec3(0), flinchV = vec3(0), headFl = vec3(0), headFlV = vec3(0);
    float dipFl = 0.f, dipFlV = 0.f, armFl[2] = {0.f, 0.f}, armFlV[2] = {0.f, 0.f}, legFl[2] = {0.f, 0.f}, legFlV[2] = {0.f, 0.f};
    float cringe = 0.f, cringeV = 0.f;
    vec3 armFlDir[2] = {vec3(0, -1, 0), vec3(0, -1, 0)};
    vec2 limpLurch = vec2(0);   // the limp's body offset along the travel (ahead over the hurt leg, back over the good one)
    float limpDuty = 0.62f;
    vec2 pushV = vec2(0);         // stagger: root velocity (model space) the body is catching up with
    vec2 pushLean = vec2(0), pushLeanV = vec2(0);
    float staggerT = -1.f;        // time since the stagger began (-1 steady)
    int reflexWound = 0;          // a heavy hit's wound the hand goes to for a moment
    float reflexT = -1.f;
    int clutchCur = 0, clutchSide = 1;
    float clutchW = 0.f;          // the clutching hand's weight (fades between wounds)
    float limpW[2] = {0.f, 0.f}, woundedS = 0.f;
    vec3 fallDirS = vec3(0, 1, 0);
    float braceW = 0.f;
    vec3 skinW[WOUND_COUNT];      // this body's skin at each wound (bind model space) and the bone it moves with
    int skinWB[WOUND_COUNT] = {0, 0, 0, 0, 0, 0, 0};
    // getting in / out of a car (AnimInput::car)
    CarDoorInfo carIn;            // the door of the car clip playing (as last given)
    float carDoorS = -1.f;        // its opening this update (-1: none)
    float beltT = -1.f;           // buckling up (time into it, -1 not), after getting in
    bool belted = false;          // the seat belt is on
    bool beltSide = false;        // buckling up with the right hand (a passenger on the right)
    // Walking style and body language from the character: call after init.
    void setCharacter(const CharacterDesc& d);
    // Model-space ground point the game should probe for each foot (0 left, 1 right) before the next update: under
    // a planted foot, ahead of a swinging one (see AnimInput::footProbes).
    vec3 footProbe(int side) const { return probeP[side & 1]; }
    // A foot down near a step edge (a curb, a stair) also needs the ground at this model-space point before the next
    // update (AnimInput::groundEdgeL/R): under its toe tip and its heel in turn.
    bool wantsEdgeProbe(int side) const { return edgeKind[side & 1] != 0; }
    vec3 edgeProbe(int side) const { return edgeP[side & 1]; }
    // Steering wheel turn (rad, + = right) the hands hold while driving: draw the rim rotated by -wheelTurn() about
    // AnimInput::wheelN so it and the hands agree.
    float wheelTurn() const { return steerS * 1.2f; }
    void init(const Skeleton* s, u32 variationSeed);
    void update(const AnimInput& in, float dt) { update(in, dt, false); }
    // cheap = distant peds (LOD2): no foot / hand IK, no two-handed grip fix-up, no face (blinks, gaze, look-at,
    // visemes); the body layers, actions and crossfades still run. Can also be called at a reduced rate.
    void update(const AnimInput& in, float dt, bool cheap);
    // Crossfade from an externally produced pose (e.g. the ragdoll when a get-up starts) over `seconds`.
    void blendFrom(const Pose& from, float seconds);
    void faceOverlay(const AnimInput& in, float dt);   // internal: look-at, gaze, blinks, jaw (called by update)
    bool actionDone() const { return actionFinished; }
    // Stagger after a heavy hit (AnimInput::hitStrength >= 0.5): the velocity (m/s, model space, horizontal) the game
    // moves the ped with while it catches its balance - its own movement suspended meanwhile - and whether it still
    // is; zero / false once steady.
    vec3 staggerVelocity() const { return vec3(pushV.x, pushV.y, 0.f); }
    bool staggering() const { return staggerT >= 0.f; }
    // How far into the bracing pose a falling body is (AnimInput::fallBrace): start the ragdoll once it is near 1.
    float braceWeight() const { return braceW; }
    // Getting in / out of a car through a door (AnimInput::car): how far the occupant has the door open this update
    // (0 shut .. 1 = DoorSpec::maxAngle), -1 when not getting in or out; the game swings the door by it.
    float carDoor() const { return carDoorS; }
    // The seat belt is on (seated in a vehicle stance after buckling up, until unbuckled getting out): draw the strap.
    bool seatBelt() const { return belted; }
    // Drop what is left of the impacts (flinch, stagger, the reflex hand, bracing); blendFrom() and the get-up clips do
    // it themselves. Lasting injuries (legHurt, wounded, clutch) follow the input as before.
    void clearImpacts();
};

}  // namespace Anim
