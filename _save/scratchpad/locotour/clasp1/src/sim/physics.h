// Collision world (static, streamed with world cells) and rigid-body helpers.
#pragma once
#include "../world/worldtypes.h"
#include "../world/roads.h"
#include <unordered_map>
#include <unordered_set>

namespace Phys {

enum SurfaceType : u8 { SURF_ASPHALT = 0, SURF_CONCRETE, SURF_GRASS, SURF_DIRT, SURF_SAND, SURF_WATER, SURF_WOOD, SURF_METAL, SURF_MUD };

enum ColliderKind : u8 { COL_BOX = 0, COL_CYLINDER };

struct Collider {
    u8 kind;
    u8 surface;
    u16 flags;      // 1 = breakable prop, 2 = building
    vec3 c;         // world center (box) / base center (cylinder)
    vec2 ax;        // box: unit x axis (horizontal); cylinder unused
    vec3 he;        // box half extents; cylinder: (radius, radius, height)
    int owner;      // cell key
    int propIndex = -1;  // index into the cell's prop list (prop colliders), -1 otherwise
};

struct RayHit {
    float t = 1e30f;
    vec3 pos, normal;
    int collider = -1;   // -1 terrain / road surface
    u8 surface = SURF_ASPHALT;
    int dynamicId = -1;  // hit a dynamic body (set by higher level queries)
};

struct GroundHit {
    float z = -1e9f;
    vec3 normal = vec3(0, 0, 1);
    u8 surface = SURF_GRASS;
    bool water = false;   // standing in water (water surface above ground)
    float waterZ = -1e9f;
};

class CollisionWorld {
public:
    void addCell(int key, const std::vector<World::CollisionBox>& boxes, const std::vector<World::PropInstance>& props);
    void removeCell(int key);
    bool hasCell(int key) const { return cellColliders.count(key) != 0; }

    // Highest walkable surface at (x,y) not above zRef + maxStep (terrain, roads/bridges, roofs, prop tops).
    GroundHit ground(float x, float y, float zRef, float maxStep = 0.6f) const;
    // Ray against static geometry (boxes, cylinders, terrain, road decks). dir must be normalized.
    bool raycast(vec3 o, vec3 dir, float maxDist, RayHit& hit, bool includeTerrain = true) const;
    // Push a vertical capsule (feet at base, radius, height) out of static colliders. Returns true if any contact;
    // accumulates the correction in `push` and the contact normal (horizontal) in `normal`.
    bool capsuleOverlap(vec3 base, float radius, float height, vec3& push, vec3& normal) const;
    // Oriented box (vehicle chassis) vs static colliders: contact points with normals and depths.
    struct Contact {
        vec3 point, normal;
        float depth;
        int collider;
    };
    void boxContacts(vec3 center, const mat3& rot, vec3 half, std::vector<Contact>& out) const;
    void collidersNear(vec2 p, float r, std::vector<int>& out) const;
    const Collider& collider(int i) const { return colliders[i]; }
    int colliderCount() const { return (int)colliders.size(); }
    // Remove (break) a collider, e.g. a streetlight knocked over. Returns false if already removed.
    // Broken props stay broken for the session (also when their cell streams out and back in).
    bool breakCollider(int i);
    size_t brokenCount() const { return brokenProps.size(); }
    bool isPropBroken(int cellKey, int propIndex) const {
        return !brokenProps.empty() && brokenProps.count(((long long)cellKey << 24) | (long long)propIndex) != 0;
    }

private:
    std::vector<Collider> colliders;
    std::vector<int> freeList;
    std::unordered_map<int, std::vector<int>> cellColliders;
    std::unordered_set<long long> brokenProps;
    // Spatial hash (16 m cells)
    static constexpr float kCell = 16.f;
    std::unordered_map<long long, std::vector<int>> grid;
    long long gkey(int x, int y) const { return ((long long)(y + 20000) << 32) | (u32)(x + 20000); }
    void insert(int id);
    void erase(int id);
};

extern CollisionWorld* gCollision;

// Rigid body state (vehicles, props in motion)
struct RigidBody {
    dvec3 pos;
    quat rot;
    vec3 vel, angVel;       // world space
    float mass = 1000.f, invMass = 1e-3f;
    vec3 invInertiaLocal;   // diagonal inverse inertia (body space)
    vec3 force, torque;
    void setBoxInertia(float m, vec3 half);
    mat3 rotMat() const { return mat3FromQuat(rot); }
    vec3 pointVelocity(vec3 worldPointRel) const { return vel + cross(angVel, worldPointRel); }  // rel = p - pos
    void addForceAt(vec3 f, vec3 worldPointRel) { force += f; torque += cross(worldPointRel, f); }
    void applyImpulse(vec3 j, vec3 worldPointRel);
    vec3 invInertiaWorldMul(vec3 v) const;
    void integrate(float dt);
};

// Impulse response along a normal for a contact between a rigid body and a static world (restitution e, friction mu)
void resolveStaticContact(RigidBody& b, vec3 pointRel, vec3 normal, float depth, float e, float mu, float dt);
// Two rigid bodies
void resolveBodyContact(RigidBody& a, RigidBody& b, vec3 point, vec3 normal, float depth, float e, float mu);

// OBB vs OBB (SAT) returning a contact (normal from a to b)
bool obbObb(vec3 ca, const mat3& ra, vec3 ha, vec3 cb, const mat3& rb, vec3 hb, vec3& normal, float& depth, vec3& point);

}  // namespace Phys
