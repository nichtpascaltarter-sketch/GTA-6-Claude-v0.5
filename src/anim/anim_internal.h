// Internal helpers shared by the character modules (skeleton, body/face/clothing/hair meshes, clips, animator).
// Everything lives in Anim::detail (named namespace: the game is a unity build).
#pragma once
#include "character.h"
#include "../core/rng.h"

namespace Anim {
namespace detail {

// Internal clips (not in the public Clip enum) baked with the library and sampled with sampleClipId().
enum InternalClip : int {
    IC_RIFLE_CARRY = CLIP_COUNT,   // long gun at the low ready (arm layer)
    IC_GUARD,                      // fists up, fighting stance (stance 19, meleeKind 0)
    IC_GUARD_KNIFE,                // knife fighting stance (stance 19, meleeKind 1)
    IC_GUARD_BAT,                  // bat cocked over the right shoulder (stance 19, meleeKind 2)
    IC_BLOCK_BAT,                  // bat held across in front of the face (stance 20, meleeKind 2)
    IC_END
};
void sampleClipId(const Skeleton& skel, int ci, float t, Pose& out, u32 variationSeed);
const ClipInfo& clipInfoId(int id);   // public or internal clip
// Lip sync: mouth shape of a viseme (Oculus order) scaled by w -> out[6] (jaw, upper lip, lower lip, corner yaw,
// corner pitch, tongue); applyMouthShape poses the speech bones (and the jaw when jaw >= 0).
void visemeShape(int v, float w, float* out);
void applyMouthShape(Pose& p, const float* shape, float jaw);
// Two-handed bat grip at clip time t: weight of the left hand on the handle and its grip centre's distance along the
// bat from the right fist (-: towards the knob), `reversed` = left thumb pointing back along the bat (overhand hold).
float batGrip(int clip, float t, float& dist, bool& reversed);
// Fist grip frame in hand-bone space (bind rotations are identity): centre = wrist + fingerDir * kGripAlong * palmLen
// + palmN * kGripPalm * palmLen; the handle axis out of the thumb side is +Y for both hands.
const float kGripAlong = 0.85f, kGripPalm = 0.3f;

// ------------------------------------------------------------------------------------------------
// Small math helpers
FORCEINLINE float sstep(float x) { x = Saturate(x); return x * x * (3.f - 2.f * x); }
FORCEINLINE float lstep(float a, float b, float x) { return Saturate((x - a) / (b - a)); }
FORCEINLINE float sstep(float a, float b, float x) { return sstep((x - a) / (b - a)); }
// Polynomial smooth min/max (k = blend radius, meters).
FORCEINLINE float sminf(float a, float b, float k) {
    if (k <= 0.f) return Min(a, b);
    float h = Max(k - fabsf(a - b), 0.f) / k;
    return Min(a, b) - h * h * k * 0.25f;
}
FORCEINLINE float smaxf(float a, float b, float k) { return -sminf(-a, -b, k); }
FORCEINLINE float bump(float x, float c, float w) { float d = (x - c) / w; return expf(-d * d); }
FORCEINLINE float angDiff(float a, float b) { return wrapAngle(a - b); }
// u coordinate around a tube with the texture seam placed at angle seamTheta.
FORCEINLINE float uWrap(float th, float seamTheta, float r) { return wrapAngle(th - seamTheta + kPi) * r; }
FORCEINLINE quat qx(float a) { return quatAxisAngle(vec3(1, 0, 0), a); }
FORCEINLINE quat qy(float a) { return quatAxisAngle(vec3(0, 1, 0), a); }
FORCEINLINE quat qz(float a) { return quatAxisAngle(vec3(0, 0, 1), a); }
FORCEINLINE quat qaa(vec3 axis, float a) { return quatAxisAngle(axis, a); }
FORCEINLINE float ease(float t) { t = Saturate(t); return t * t * (3.f - 2.f * t); }
FORCEINLINE float easeOut(float t) { t = Saturate(t); return 1.f - (1.f - t) * (1.f - t); }
FORCEINLINE float easeIn(float t) { t = Saturate(t); return t * t; }
FORCEINLINE vec3 srgb(float r, float g, float b) { return srgbToLinear(vec3(r, g, b)); }
FORCEINLINE u32 packColor(vec3 c, float a = 1.f) { return packRGBA8(c.x, c.y, c.z, a); }
inline vec3 mulColor(vec3 a, vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
// Orthonormal frame helpers
inline void orthoFrame(vec3 n, vec3& t, vec3& b) {
    t = normalize(anyPerp(n));
    b = cross(n, t);
}
inline mat3 frameFromXY(vec3 x, vec3 yHint) {
    x = normalize(x);
    vec3 z = normalize(cross(x, yHint));
    vec3 y = cross(z, x);
    return mat3(x, y, z);
}
// Rotation mapping (a0 -> a1) and (b0 -> b1 as close as possible); a/b need not be orthogonal.
inline quat quatFromTwoPairs(vec3 a0, vec3 b0, vec3 a1, vec3 b1) {
    mat3 f0 = frameFromXY(a0, b0), f1 = frameFromXY(a1, b1);
    return normalize(quatFromMat3(f1 * transpose(f0)));
}

// ------------------------------------------------------------------------------------------------
// Body dimensions derived deterministically from a CharacterDesc (shared by skeleton and mesh).
struct BodyDims {
    float H = 1.78f;        // barefoot standing height
    float fem = 0;          // 0 male .. 1 female
    float weight = 0.5f, muscle = 0.4f, age = 0.35f;
    float lift = 0;         // shoe sole thickness (everything is raised by this)
    float s = 1;            // global scale H / 1.78
    float headS = 1;        // head scale
    vec3 J[B_COUNT];        // bind joint positions, model space
    // limbs
    float upperArm, forearm, palmLen, fingerLen, thumbLen, handW, handT;
    float thigh, shin, footLen, footW, heelBack, ballFwd, toeFwd;
    float armAngle;         // A-pose angle from vertical (radians)
    vec3 armDir[2];         // [0] left, [1] right: A-pose direction of upper arm/forearm/hand
    vec3 palmN[2];          // palm normal (points out of the palm)
    vec3 thumbDir[2];
    vec3 legDir[2];         // thigh direction (hip -> knee)
    // torso shape
    float zCrotch, zHip, zWaist, zNavel, zChestLine, zArmpit, zAcromion, zNeckFront, zNeckBack;
    float hipHalfW, hipDepth, waistHalfW, waistDepth, chestHalfW, chestDepth, shoulderHalfW;
    float glute, bust, belly, trap, pecs;
    float neckR;
    // arm/leg radii at key points
    float rShoulder, rUpperArm, rElbow, rForearm, rWrist;
    float rThigh, rKnee, rCalf, rAnkle;
    // face shape (multipliers around 1)
    float faceW, jawW, chinP, chinH, noseL, noseW, noseP, noseBridge, lipFull, lipW, eyeSize, eyeTilt, eyeSpace,
        browH, browRidge, cheekB, earSize, earOut, foreheadSlope, lidFold, headLen;
};
void computeDims(const CharacterDesc& d, BodyDims& D);
// Shoe sole thickness for a shoe index.
float shoeLift(int shoes);

// ------------------------------------------------------------------------------------------------
// Signed distance primitives used to shape the body. Masks select which body part rays see a primitive.
enum : u32 {
    MK_TORSO = 1u << 0, MK_NECK = 1u << 1, MK_HEAD = 1u << 2, MK_ARM_L = 1u << 3, MK_ARM_R = 1u << 4,
    MK_LEG_L = 1u << 5, MK_LEG_R = 1u << 6, MK_HAND_L = 1u << 7, MK_HAND_R = 1u << 8, MK_FACE = 1u << 9,
    MK_FOOT_L = 1u << 10, MK_FOOT_R = 1u << 11,
    MK_ALL = 0xffffffffu
};
enum : u8 { PRIM_ELLIPSOID = 0, PRIM_ROUNDCONE, PRIM_PLANE };
enum : u8 { OP_UNION = 0, OP_SUB, OP_INTERSECT };

struct Prim {
    u8 type = PRIM_ELLIPSOID, op = OP_UNION;
    u32 mask = MK_ALL;
    float k = 0.02f;        // blend radius
    // ellipsoid / scaled round cone: local frame (rows = axes), center, radii
    vec3 c;                 // center (ellipsoid) or point a (round cone) or plane point
    vec3 ax, ay, az;        // local axes (orthonormal)
    vec3 r;                 // ellipsoid radii
    // round cone: from c along az for length len; radii ra -> rb; cross-section scale sx, sy (x/y of local frame)
    float len = 0, ra = 0, rb = 0, sx = 1, sy = 1;
    // bounding sphere
    vec3 bc;
    float br = 0;
};

struct Sdf {
    std::vector<Prim> prims;
    // Add helpers (return index)
    int ellipsoid(vec3 c, vec3 r, u32 mask, float k, vec3 ax = vec3(1, 0, 0), vec3 ay = vec3(0, 1, 0));
    int cone(vec3 a, vec3 b, float ra, float rb, u32 mask, float k, float sx = 1, float sy = 1, vec3 xHint = vec3(1, 0, 0));
    int plane(vec3 p, vec3 n, u32 mask);   // keeps the side the normal points to (intersection)
    float eval(vec3 p, u32 mask) const;
    // Evaluate only the listed primitives (in order), starting from `cap` (distances beyond it are not needed).
    float evalList(vec3 p, const u16* list, int n, float cap) const;
    vec3 grad(vec3 p, u32 mask) const;
    // First exit along a ray from an interior point (returns t). If o is outside, searches inwards first.
    float castOut(vec3 o, vec3 d, u32 mask, float tMax) const;
    // Newton projection onto the surface starting near it.
    vec3 project(vec3 p, u32 mask, int iters = 4) const;
};

// ------------------------------------------------------------------------------------------------
// Build-time vertex with float skin weights and body parametrization metadata.
struct SkinW {
    u8 b[4] = {0, 0, 0, 0};
    float w[4] = {1, 0, 0, 0};
};
SkinW skin1(int b);
SkinW skin2(int b0, int b1, float t);   // lerp b0 -> b1 by t
// Accumulating weights builder
struct WAcc {
    int n = 0;
    u8 b[12];
    float w[12];
    void add(int bone, float wt);
    SkinW finish() const;
};
SkinW lerpSkin(const SkinW& a, const SkinW& b, float t);

enum : u8 {
    PART_TORSO = 0, PART_NECK, PART_HEAD, PART_ARM, PART_HAND, PART_FINGER, PART_THUMB, PART_LEG, PART_EAR,
    PART_EYE, PART_FACEDETAIL, PART_MOUTH, PART_GARMENT, PART_HAIR, PART_ACC, PART_COUNT
};

struct BVert {
    vec3 p;             // bind-pose model position
    vec3 n;             // normal (computed later for the body)
    vec3 t;             // tangent hint (around direction)
    vec2 uv;
    vec3 col = vec3(1);
    float alpha = 1.f;
    u8 mat = MAT_SKIN;
    u8 part = PART_TORSO;
    u8 side = 0;        // 0 left / 1 right for limbs
    u8 flags = 0;
    SkinW sw;
    // parametrization: torso/neck: (z, theta); limbs: (along from joint, theta); head: (theta, phi)
    float pa = 0, pb = 0;
    float pc = 0;       // normalized length fraction: torso 0 (crotch) .. 1 (neck base); limbs along/length
    float uPer = 0;     // period of uv.x for closed tubes (0 = none); used to fix wrap seams on output
    vec3 axisPt;        // point on the part axis (used for ray casts / hems)
    vec3 bp;            // underlying body surface position (garments/hair keep the skin point they came from)
};

struct MeshB {
    std::vector<BVert> v;
    std::vector<u32> idx;
    u32 add(const BVert& b) { v.push_back(b); return (u32)v.size() - 1; }
    void tri(u32 a, u32 b, u32 c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); }
    // CCW quad as seen from outside
    void quad(u32 a, u32 b, u32 c, u32 d) { tri(a, b, c); tri(a, c, d); }
    void quadMirror(bool mirror, u32 a, u32 b, u32 c, u32 d) {
        if (mirror) quad(a, d, c, b); else quad(a, b, c, d);
    }
    void triMirror(bool mirror, u32 a, u32 b, u32 c) {
        if (mirror) tri(a, c, b); else tri(a, b, c);
    }
    // Recompute smooth normals over the given index range and vertex range (area weighted).
    void computeNormals(size_t idxBegin, size_t idxEnd);
    void computeNormalsAll() { computeNormals(0, idx.size()); }
    void append(const MeshB& o);
};

// Stitch two loops (vertex index arrays) with triangles. Both loops must run in the same rotational direction
// and roughly start at the same angle. `flip` inverts the winding.
void stitchLoops(MeshB& m, const std::vector<u32>& A, const std::vector<u32>& B, bool closed, bool flip);

// Duplicate vertices of triangles that straddle a uv wrap seam (uPer) so textures don't smear.
void fixUvSeams(MeshB& m);
// Convert the build mesh to the GPU vertex format.
void emitMesh(const MeshB& m, SkinnedMeshData& out);

// ------------------------------------------------------------------------------------------------
// Shared descriptor enums (documented in character_desc notes in clothing.cpp / randomCharacter)
enum HairStyle {
    HAIR_BALD = 0, HAIR_BUZZ, HAIR_SHORT, HAIR_CURLY, HAIR_PONYTAIL, HAIR_LONG, HAIR_BUN, HAIR_BRAIDS, HAIR_SLICKED,
    HAIR_BOB, HAIR_QUIFF, HAIR_STYLE_COUNT
};
enum TopKind {
    TOP_TSHIRT = 0, TOP_TANK, TOP_POLO, TOP_HAWAIIAN, TOP_DRESS_SHIRT, TOP_HOODIE, TOP_SUIT, TOP_POLICE, TOP_MEDIC,
    TOP_HIVIS, TOP_BIKINI, TOP_ONEPIECE, TOP_SUNDRESS, TOP_BLOUSE, TOP_NONE, TOP_CROP, TOP_OVERSIZED, TOP_COUNT
};
enum BottomKind {
    BOT_JEANS = 0, BOT_SHORTS, BOT_CARGO, BOT_SLACKS, BOT_SKIRT, BOT_TRUNKS, BOT_BIKINI, BOT_POLICE, BOT_BAGGY,
    BOT_LEGGINGS, BOT_WORK, BOT_HOTPANTS, BOT_COUNT
};
enum ShoeKind { SHOE_SNEAKER = 0, SHOE_DRESS, SHOE_BOOT, SHOE_SANDAL, SHOE_BARE, SHOE_FLATS, SHOE_RUNNER, SHOE_COUNT };
enum HatKind { HAT_CAP = 0, HAT_CAP_BACK, HAT_POLICE, HAT_HARDHAT, HAT_SUNHAT, HAT_FEDORA, HAT_BEANIE, HAT_BANDANA, HAT_COUNT };
enum GlassesKind { GL_SUN = 0, GL_AVIATOR, GL_READING, GL_COUNT };
enum FacialHairKind { FH_STUBBLE = 0, FH_MUSTACHE, FH_GOATEE, FH_BEARD, FH_SHORTBEARD, FH_COUNT };

// ------------------------------------------------------------------------------------------------
// Head/face construction shared data (built in face.cpp, used by hair/clothing).
struct HeadInfo {
    vec3 origin;            // model-space head joint (head frame axes = model axes in bind)
    vec3 C;                 // ray origin for the head grid (model space)
    int cols = 0, rows = 0;
    std::vector<u32> grid;  // rows*cols vertex indices into the body mesh (row-major, row 0 = neck junction)
    int rowEyeLo = 0, rowEyeHi = 0, rowMouthLo = 0, rowMouthHi = 0, rowBrow = 0, rowChin = 0, rowNoseBase = 0;
    int rowHairline = 0;
    vec3 eyeC[2];           // eyeball centers (model)
    float eyeR = 0.012f;
    vec3 earPos[2];         // ear root centers (model)
    vec3 lipCorner[2];
    float thetaEye = 0, thetaMouth = 0;
};

// Everything the mesh builders share.
struct BuildCtx {
    const CharacterDesc* d = nullptr;
    const BodyDims* D = nullptr;
    const Skeleton* sk = nullptr;
    Sdf sdf;
    MeshB m;                // connected skin surface first, then separate skin pieces (fingers, ears, eyes...)
    HeadInfo head;
    vec3 skin;              // linear skin albedo
    vec3 lipCol, palmCol;
    u32 neckTopFirst = 0;   // first vertex of the head grid row 0
    std::vector<u32> torsoTop;   // torso top ring (neck base) vertex indices
    size_t surfaceIdxEnd = 0;    // index count of the connected skin surface
    // per-vertex region flags
    enum : u8 { F_PALM = 1, F_SOLE = 2, F_LIP = 4, F_SCALP = 8, F_FACE = 16, F_BEARD = 32, F_NAIL = 64 };
};

// Skin weights of a torso surface point (bind pose), shared with garment/accessory builders.
SkinW torsoSkinWeights(const BodyDims& D, vec3 p);
void addHeadPrims(BuildCtx& c);
void buildHeadGrid(BuildCtx& c);            // creates the head grid (row 0 = neck top ring)
void buildFaceDetails(BuildCtx& c);         // eyes, ears, brows, lashes, mouth interior, teeth
void buildBody(BuildCtx& c);                // full skin body incl. head
// ---- Garment engine (clothing.cpp): offset shells extracted from the skin surface over a coverage field.
typedef std::function<float(const BVert&)> CovFn;
struct GarmentDef {
    CovFn cov;                                    // meters, positive = covered
    u8 mat = MAT_CLOTH;
    vec3 col = vec3(0.8f);
    float thick = 0.004f;                         // offset from the skin
    std::function<float(const BVert&)> extraFn;   // additional per-vertex offset (looseness / hair volume)
    int smooth = 1;                               // constrained smoothing iterations
    bool hem = true;
    float hideMargin = 0.012f;                    // coverage (m) needed to hide what is underneath
    bool hides = true;                            // hides skin/inner layers
    std::function<vec3(const BVert&, vec3)> colFn;
    u32 parts = 0xffffffffu;                      // which body parts it can cover (bit per PART_*)
    bool swapUV = false;                          // hair: texture streaks along the flow
};
struct OutfitCtx {
    BuildCtx& c;
    MeshB& out;
    std::vector<u8>& hideBody;
    std::vector<u8> hideOut;          // per triangle of `out`
    struct Layer {
        CovFn cov;
        float margin;
        size_t t0, t1;                // triangle range in out
    };
    std::vector<Layer> layers;
    // torso offsets of the already emitted top/bottom shells (so the next layer clears them at the waist)
    float botTorsoOff = 0.f, botTopZ = -1.f, topTorsoOff = 0.f;
    OutfitCtx(BuildCtx& cc, MeshB& o, std::vector<u8>& h) : c(cc), out(o), hideBody(h) {}
};
bool emitGarment(OutfitCtx& o, const GarmentDef& g);
// Hair thickness (m) over a head grid vertex for the character's hairstyle (used by hats and hair).
float hairVolumeAt(const BuildCtx& c, const BVert& v);
// Hair and facial hair (emitted first so hats can hide it).
void buildHairLayer(OutfitCtx& o);
// Garments, shoes, hats, glasses, jewelry (and hair). Marks covered skin triangles in `hideTri` (per triangle of c.m).
void buildOutfit(BuildCtx& c, MeshB& out, std::vector<u8>& hideTri);

}  // namespace detail
}  // namespace Anim
