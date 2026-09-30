// Wildlife. Species, procedural skeletons, lofted skinned meshes with LODs and procedural animation live in
// animal_models.cpp (namespace Fauna, engine independent: only math + mesh data, so it also builds natively for
// tests); the living-world simulation (spawning around the player by region and time of day, flocking, behaviours,
// reactions, damage, sounds, render submission) lives in wildlife.cpp (namespace Game::Wildlife).
// Model space of every animal: +X right, +Y forward (the animal faces +Y), +Z up. Ground animals have their origin on
// the ground under the body, fliers and swimmers at the body centre.
#pragma once
#include "../render/mesh.h"
#include "../core/rng.h"

namespace Fauna {

enum Species : u8 {
    // birds
    SP_GULL = 0, SP_PELICAN, SP_PIGEON, SP_HERON, SP_EGRET, SP_SPOONBILL, SP_FLAMINGO, SP_IBIS, SP_VULTURE, SP_PARROT,
    // reptiles
    SP_GATOR, SP_IGUANA,
    // marine life
    SP_DOLPHIN, SP_MANATEE, SP_TURTLE, SP_FISH,
    // mammals on land
    SP_DOG, SP_CAT, SP_RACCOON, SP_DEER, SP_COW, SP_HORSE,
    // more birds (appended so the values above stay stable: the photo census stores species bits)
    SP_SANDPIPER, SP_GRACKLE, SP_FRIGATE, SP_CORMORANT, SP_CEGRET,
    SP_SQUIRREL,
    SP_COUNT
};

enum BodyPlan : u8 { PLAN_BIRD = 0, PLAN_REPTILE, PLAN_CETACEAN, PLAN_FISH, PLAN_TURTLE, PLAN_QUAD };

constexpr int kMaxBones = 40;
constexpr int kBatchBonesMax = 8;   // bones per animal in the batched (instanced-by-skinning) LOD meshes
constexpr int kMaxVariants = 6;

// ---- skeleton: bone frames are aligned with the model axes in the bind pose (bind rotations are identity), so a
// pose is just a local rotation (+ optional local scale) per bone and the skinning matrix is world * T(-bind).
struct Skel {
    int n = 0;
    int parent[kMaxBones];
    vec3 bind[kMaxBones];           // model-space joint positions in the bind pose
    int add(int par, vec3 p) {
        parent[n] = par;
        bind[n] = p;
        return n++;
    }
};

struct Pose {
    quat q[kMaxBones];              // local rotations relative to the bind frame
    vec3 s[kMaxBones];              // local scale of the bone's own vertices (not inherited by children)
    float g[kMaxBones];             // uniform scale inherited by the whole sub-chain (collapses spread wings, leashes)
    vec3 rootPos;                   // model-space offset of bone 0 from its bind position
    quat rootRot;                   // extra rotation of bone 0 about its joint (whole-body pitch/roll)
    void reset(int n) {
        for (int i = 0; i < n; i++) {
            q[i] = quat();
            s[i] = vec3(1.f);
            g[i] = 1.f;
        }
        rootPos = vec3(0.f);
        rootRot = quat();
    }
};

// World (model-space) joint frames of a posed skeleton.
struct Frames {
    quat r[kMaxBones];
    vec3 p[kMaxBones];
    float g[kMaxBones];
};

// Forward kinematics; writes skinning matrices (model * inverse bind) and optionally the joint frames.
void poseSkeleton(const Skel& sk, const Pose& pose, mat4* skin, Frames* frames = nullptr);

// ---- procedural animation inputs (filled by the simulation each frame)
struct BirdAnim {
    float flap = 0.f;       // wing beat phase (radians)
    float flapAmp = 0.f;    // 0 glide .. 1 full beats (1.3 = takeoff power strokes)
    float fold = 0.f;       // 0 wings spread .. 1 folded against the body (perched)
    float legs = 0.f;       // 0 tucked (flight) .. 1 standing
    float walk = 0.f;       // walking cycle phase (radians)
    float walkAmt = 0.f;    // 0..1
    float headYaw = 0.f, headPitch = 0.f;   // look direction relative to the body (radians)
    float neck = 0.f;       // waders: 0 S-curve .. 1 fully extended strike; fliers: -1 retracted (herons in flight)
    float peck = 0.f;       // 0..1 head down to the ground (pigeons pecking, waders feeding)
    float mouth = 0.f;      // 0..1 bill open (calls, pelican pouch)
    float tail = 0.f;       // 0..1 tail fanned (landing, braking)
    float dive = 0.f;       // 0..1 plunge-dive pose (wings swept back)
    float soar = 0.f;       // 0..1 soaring wing shape (dihedral, splayed primaries)
    float flare = 0.f;      // 0..1 landing flare (body upright, wings cupped forward)
    float sit = 0.f;        // 0..1 resting on the belly / floating on water (legs hidden)
    float dead = 0.f;       // 0..1 limp
    float t = 0.f;          // free running time (idle fidgets)
};

struct QuadAnim {
    float phase = 0.f;      // gait cycle phase 0..1
    float speed = 0.f;      // m/s over ground
    float gait = 0.f;       // 0 walk, 1 trot, 2 canter, 3 gallop (fractional values blend)
    float turn = 0.f;       // yaw rate (rad/s): the spine bends into the turn
    float headDown = 0.f;   // 0..1 grazing / sniffing
    float lookYaw = 0.f, lookPitch = 0.f;
    float sit = 0.f;        // 0..1 sitting (dogs, cats, raccoons)
    float lie = 0.f;        // 0..1 lying down
    float alert = 0.f;      // 0..1 ears up, head raised
    float mouth = 0.f;      // 0..1 jaw open (bark, pant, bite)
    float tailWag = 0.f;    // 0..1 wag amplitude
    float crouch = 0.f;     // 0..1 stalking / cowering
    float rear = 0.f;       // 0..1 horse rearing, dog jumping up
    float dead = 0.f;       // 0..1 lying limp on its side
    float t = 0.f;
};

struct ReptileAnim {
    float phase = 0.f;      // walk cycle 0..1
    float speed = 0.f;
    float swim = 0.f;       // 0 walking .. 1 swimming (legs tucked, tail drives)
    float swimPhase = 0.f;  // radians
    float turn = 0.f;
    float jaw = 0.f;        // 0..1 mouth open
    float lift = 0.f;       // 0 belly on the ground .. 1 high walk
    float headYaw = 0.f, headPitch = 0.f;
    float hiss = 0.f;       // 0..1 inflated, head raised, mouth open
    float roll = 0.f;       // death roll angle (radians, whole body about its axis)
    float dead = 0.f;
    float t = 0.f;
};

struct SwimAnim {           // dolphins, manatees, fish, turtles
    float phase = 0.f;      // tail beat / flipper stroke phase (radians)
    float amp = 1.f;        // 0..1.5 beat amplitude
    float turn = 0.f;       // yaw rate
    float pitch = 0.f;      // body pitch (radians), used to curve the spine for arcs
    float headYaw = 0.f;
    float dead = 0.f;
    float t = 0.f;
};

// ---- species description
struct SpeciesInfo {
    const char* name;
    BodyPlan plan;
    int variants;
    float length;           // nose to tail (m)
    float height;           // standing height / body depth (m)
    float wingspan;         // birds
    float hp;               // health
    float mass;             // kg (vehicle impacts)
};
const SpeciesInfo& speciesInfo(int sp);

// Named bones of each body plan (indices into the skeleton).
// FOLDL/FOLDR: the folded wings lying along the flanks (perched); the spread wings WL*/WR* collapse into the shoulder.
namespace BirdBone { enum : int { BODY = 0, NECK1, NECK2, NECK3, HEAD, JAW, TAIL, WL1, WL2, WL3, WR1, WR2, WR3, LL1, LL2, LL3, LR1, LR2, LR3, FOLDL, FOLDR, COUNT }; }
namespace QuadBone {
enum : int {
    BODY = 0, PELVIS, CHEST, NECK1, NECK2, HEAD, JAW, EAR_L, EAR_R, TAIL1, TAIL2, TAIL3,
    FL1, FL2, FL3, FR1, FR2, FR3, HL1, HL2, HL3, HR1, HR2, HR3,
    LEASH0, LEASH1, LEASH2, LEASH3, LEASH4, LEASH5, COUNT
};
}
namespace ReptBone {
enum : int { BODY = 0, PELVIS, CHEST, NECK, HEAD, JAW, TAIL1, TAIL2, TAIL3, TAIL4, TAIL5, TAIL6,
             FL1, FL2, FL3, FR1, FR2, FR3, HL1, HL2, HL3, HR1, HR2, HR3, COUNT };
}
namespace SwimBone { enum : int { BODY = 0, FRONT, HEAD, BACK1, BACK2, TAILFIN, FIN_L, FIN_R, FIN_L2, FIN_R2, COUNT }; }
constexpr int kLeashSegments = 6;

// A built model: skeleton, per-LOD meshes (CPU side until uploaded by the game), plus the batched LOD meshes that pack
// up to `batchCap` animals into one draw (each animal = batchN consecutive bones, driven by batchBones of the skeleton).
struct ModelData {
    int species = 0, variant = 0;
    Skel skel;
    int batchN = 0;
    int batchBones[kBatchBonesMax] = {0, 0, 0, 0, 0, 0, 0, 0};
    SkinnedMeshData lod[2];         // individual meshes: 0 detailed, 1 reduced (same skeleton)
    SkinnedMeshData batch[2];       // batched meshes: 0 mid distance, 1 far (flocks / shoals); empty if unused
    int batchCap = 0;
    // leg chains for IK: bones (upper, lower, foot) and the ground contact offset from the foot joint (bind)
    int legs = 0;
    int legBone[4][3];
    vec3 legEnd[4];
    float legLen = 0.f;             // hip height (quadrupeds) / leg reach
    float scale = 1.f;              // nominal size multiplier baked into the mesh
    vec3 collar;                    // dogs: leash attachment point (bind, model space)
    vec3 mouth;                     // bite / call source (bind, model space)
    vec3 headTip;                   // nose / bill tip (bind, model space)
};

void buildModel(int species, int variant, ModelData& out);

// Quadruped gait: cycles per second for a speed (m/s) and gait (0 walk .. 3 gallop); the simulation advances
// QuadAnim::phase by this rate so the feet do not slide.
float quadCycleRate(const ModelData& m, float speed, float gait);
// Reptile walk cycle rate.
float reptileCycleRate(const ModelData& m, float speed);

// Procedural animation: pose from the plan-specific inputs.
void animateBird(const ModelData& m, const BirdAnim& a, Pose& pose);
void animateQuad(const ModelData& m, const QuadAnim& a, Pose& pose, const float* footGround = nullptr);   // footGround: 4 toe height offsets
void animateReptile(const ModelData& m, const ReptileAnim& a, Pose& pose);
void animateSwimmer(const ModelData& m, const SwimAnim& a, Pose& pose);
// Leash bones (dogs): rope from the collar to `handModel` (dog model space) with slack; writes skinning matrices of
// the leash bones in `skin` (after poseSkeleton). hidden = collapse the leash (strays).
void leashMatrices(const ModelData& m, const Frames& fr, vec3 handModel, float ropeLen, bool hidden, mat4* skin);

// Flocking: boids steering (separation, alignment, cohesion) for agent `self` among `n` neighbours.
struct BoidParams {
    float sepDist = 2.f, viewDist = 12.f;
    float wSep = 2.f, wAli = 1.f, wCoh = 0.6f;
};
vec3 boidSteer(const vec3* pos, const vec3* vel, int n, int self, const BoidParams& bp);

}  // namespace Fauna

// ---- Read-only queries for other gameplay systems (defined in wildlife.cpp) -----------------------------------------
namespace Render {
struct Camera;
}
namespace Game {
struct GameWorld;
namespace Wildlife {
struct Sighting {
    int species;    // Fauna::Species
    int count;      // animals of this species that qualified
    float size;     // largest share of the frame height covered by one of them
    float centre;   // that animal's offset from the frame centre (0 centre .. 1 frame edge, the larger of x / y)
    float dist;     // metres from the camera to that animal
};
// Species of the living animals inside the camera's frame (vertical fov cam.fovY, the given aspect), within maxDist,
// not hidden behind world geometry (a collision raycast per candidate) and covering at least minSize of the frame
// height. Animals under water only count when the camera is under water too (or they are just below the surface).
// Sorted by on-screen size, largest first; returns the number written (at most maxOut, one entry per species).
int sightings(const Render::Camera& cam, float maxDist, Sighting* out, int maxOut, float aspect = 16.f / 9.f, float minSize = 0.04f);
// The same, species ids only (16:9 frame, 4% of the frame height at least).
int visibleSpecies(const Render::Camera& cam, float maxDist, int* outSpecies, int maxOut);
// Common name ("roseate spoonbill"), or "" for an invalid id; speciesCount() == Fauna::SP_COUNT.
const char* speciesName(int species);
int speciesCount();

// ---- police dogs (K9, driven by police.cpp): a German shepherd on a leash at its handler's side -------------------------
enum K9Cmd : u8 {
    K9_HEEL = 0,   // at the handler's side (a dog that was released runs back and is leashed again)
    K9_TRACK,      // pulls ahead of the handler toward `at`, nose down (following a scent trail)
    K9_ALERT,      // stands ahead of the handler barking at `at`
    K9_ATTACK      // slips the leash, runs `targetPed` down, bites, takes a fleeing suspect to the ground, stands over them
};
// The dog for `handler` (a police officer on foot); -1 when the wildlife pool is full or not ready. *uidOut: its handle.
int spawnK9(GameWorld& g, int handler, u32* uidOut);
void k9Command(GameWorld& g, int group, u32 uid, int cmd, vec3 at, int targetPed);
bool k9Alive(int group, u32 uid);         // still there and on its feet
vec3 k9Pos(int group);
bool k9Loose(int group);                  // released (not on the leash)
void k9Dismiss(int group, u32 uid);       // the unit is done: an ordinary leashed dog again (despawns with the rest)
}  // namespace Wildlife
}  // namespace Game
