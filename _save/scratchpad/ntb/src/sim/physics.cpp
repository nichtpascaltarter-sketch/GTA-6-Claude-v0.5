#include "physics.h"
#include "../world/buildings.h"
#include "waves.h"

namespace Phys {

CollisionWorld* gCollision = nullptr;
WaveState gWaves;

namespace physics_detail {

u8 terrainSurface(float x, float y) {
    const World::WorldMap& m = *World::gMap;
    float sx = World::worldToTexel(x), sy = World::worldToTexel(y);
    int ix = Clamp((int)(sx + 0.5f), 0, World::kHeightRes - 1), iy = Clamp((int)(sy + 0.5f), 0, World::kHeightRes - 1);
    size_t i = (size_t)iy * World::kHeightRes + ix;
    vec4 a = unpackRGBA8(m.splat0[i]), b = unpackRGBA8(m.splat1[i]);
    float w[8] = {a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w};
    int best = 0;
    for (int k = 1; k < 8; k++)
        if (w[k] > w[best]) best = k;
    switch (best) {
        case World::TL_SAND: return SURF_SAND;
        case World::TL_GRASS: case World::TL_SAWGRASS: case World::TL_FOREST: return SURF_GRASS;
        case World::TL_DIRT: return SURF_DIRT;
        case World::TL_ROCK: return SURF_CONCRETE;
        case World::TL_MUD: return SURF_MUD;
        default: return SURF_DIRT;
    }
}

// Distance from point to a box footprint in 2D (box local frame), returns closest point
vec2 closestOnRect(vec2 p, vec2 c, vec2 ax, float hx, float hy, bool& inside) {
    vec2 ay = perp(ax);
    vec2 d = p - c;
    float lx = dot(d, ax), ly = dot(d, ay);
    inside = fabsf(lx) <= hx && fabsf(ly) <= hy;
    float cx = Clamp(lx, -hx, hx), cy = Clamp(ly, -hy, hy);
    if (inside) {
        // push to the nearest edge
        float ex = hx - fabsf(lx), ey = hy - fabsf(ly);
        if (ex < ey) cx = lx >= 0 ? hx : -hx;
        else cy = ly >= 0 ? hy : -hy;
    }
    return c + ax * cx + ay * cy;
}

}  // namespace physics_detail

using namespace physics_detail;

void CollisionWorld::insert(int id) {
    const Collider& c = colliders[id];
    float r = c.kind == COL_BOX ? sqrtf(c.he.x * c.he.x + c.he.y * c.he.y) : c.he.x;
    int x0 = (int)floorf((c.c.x - r) / kCell), x1 = (int)floorf((c.c.x + r) / kCell);
    int y0 = (int)floorf((c.c.y - r) / kCell), y1 = (int)floorf((c.c.y + r) / kCell);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) grid[gkey(x, y)].push_back(id);
}
void CollisionWorld::erase(int id) {
    const Collider& c = colliders[id];
    float r = c.kind == COL_BOX ? sqrtf(c.he.x * c.he.x + c.he.y * c.he.y) : c.he.x;
    int x0 = (int)floorf((c.c.x - r) / kCell), x1 = (int)floorf((c.c.x + r) / kCell);
    int y0 = (int)floorf((c.c.y - r) / kCell), y1 = (int)floorf((c.c.y + r) / kCell);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            auto it = grid.find(gkey(x, y));
            if (it == grid.end()) continue;
            auto& v = it->second;
            v.erase(std::remove(v.begin(), v.end(), id), v.end());
            if (v.empty()) grid.erase(it);
        }
}

void CollisionWorld::addCell(int key, const std::vector<World::CollisionBox>& boxes, const std::vector<World::PropInstance>& props) {
    if (cellColliders.count(key)) return;
    std::vector<int>& ids = cellColliders[key];
    auto alloc = [&](const Collider& c) {
        int id;
        if (!freeList.empty()) { id = freeList.back(); freeList.pop_back(); colliders[id] = c; }
        else { id = (int)colliders.size(); colliders.push_back(c); }
        ids.push_back(id);
        insert(id);
    };
    for (const auto& b : boxes) {
        Collider c;
        c.kind = COL_BOX;
        c.surface = SURF_CONCRETE;
        c.flags = 2;
        c.c = b.c;
        c.ax = b.ax;
        c.he = b.he;
        c.owner = key;
        alloc(c);
    }
    for (int pi = 0; pi < (int)props.size(); pi++) {
        const World::PropInstance& p = props[pi];
        if (isPropBroken(key, pi)) continue;
        Collider c;
        c.owner = key;
        c.propIndex = pi;
        c.surface = SURF_METAL;
        c.flags = 0;
        c.ax = vec2(cosf(p.yaw), sinf(p.yaw));
        switch (p.type) {
            case World::PROP_STREETLIGHT: case World::PROP_STREETLIGHT_DOUBLE: case World::PROP_TRAFFIC_LIGHT: case World::PROP_POWER_POLE:
                c.kind = COL_CYLINDER; c.c = p.pos; c.he = vec3(0.17f * p.scale, 0.17f * p.scale, 8.f * p.scale); c.flags = 1; break;
            case World::PROP_STOP_SIGN: case World::PROP_HYDRANT: case World::PROP_PARKING_METER:
                c.kind = COL_CYLINDER; c.c = p.pos; c.he = vec3(0.12f, 0.12f, 1.f); c.flags = 1; break;
            case World::PROP_PALM: case World::PROP_PALM_TALL: case World::PROP_TREE_OAK: case World::PROP_TREE_PINE: case World::PROP_CYPRESS:
                c.kind = COL_CYLINDER; c.c = p.pos; c.he = vec3(0.26f * p.scale, 0.26f * p.scale, 6.f * p.scale); c.surface = SURF_WOOD; break;
            case World::PROP_MANGROVE:
                c.kind = COL_CYLINDER; c.c = p.pos; c.he = vec3(0.8f * p.scale, 0.8f * p.scale, 2.f); c.surface = SURF_WOOD; break;
            case World::PROP_BENCH: case World::PROP_BIN: case World::PROP_DUMPSTER: case World::PROP_NEWS_BOX:
                c.kind = COL_BOX; c.he = p.type == World::PROP_DUMPSTER ? vec3(0.9f, 0.6f, 0.65f) : (p.type == World::PROP_BENCH ? vec3(0.9f, 0.25f, 0.45f) : vec3(0.3f, 0.3f, 0.5f));
                c.c = p.pos + vec3(0, 0, c.he.z); c.flags = 1; break;
            case World::PROP_BUS_STOP:
                c.kind = COL_BOX; c.he = vec3(1.8f, 0.3f, 1.25f); c.c = p.pos + vec3(0, 0.9f, 1.25f); break;
            case World::PROP_LIFEGUARD_TOWER:
                c.kind = COL_BOX; c.he = vec3(1.3f, 1.3f, 2.1f); c.c = p.pos + vec3(0, 0, 2.1f); c.surface = SURF_WOOD; break;
            default: continue;
        }
        alloc(c);
    }
}

void CollisionWorld::removeCell(int key) {
    auto it = cellColliders.find(key);
    if (it == cellColliders.end()) return;
    for (int id : it->second) {
        if (colliders[id].owner < 0) continue;  // already broken
        erase(id);
        colliders[id].owner = -1;
        freeList.push_back(id);
    }
    cellColliders.erase(it);
}

bool CollisionWorld::breakCollider(int i) {
    if (i < 0 || i >= (int)colliders.size() || colliders[i].owner < 0 || !(colliders[i].flags & 1)) return false;
    if (colliders[i].propIndex >= 0) brokenProps.insert(((long long)colliders[i].owner << 24) | (long long)colliders[i].propIndex);
    erase(i);
    colliders[i].flags = 0;
    colliders[i].he = vec3(0);
    return true;
}

void CollisionWorld::collidersNear(vec2 p, float r, std::vector<int>& out) const {
    out.clear();
    int x0 = (int)floorf((p.x - r) / kCell), x1 = (int)floorf((p.x + r) / kCell);
    int y0 = (int)floorf((p.y - r) / kCell), y1 = (int)floorf((p.y + r) / kCell);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            auto it = grid.find(gkey(x, y));
            if (it == grid.end()) continue;
            for (int id : it->second) out.push_back(id);
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

GroundHit CollisionWorld::ground(float x, float y, float zRef, float maxStep) const {
    GroundHit g;
    const World::WorldMap& m = *World::gMap;
    float limit = zRef + maxStep;
    float th = m.heightAt(x, y);
    g.z = th;
    g.normal = m.normalAt(x, y);
    g.surface = terrainSurface(x, y);
    float rz;
    if (World::gRoads && World::gRoads->surfaceHeight(vec2(x, y), &rz, limit)) {
        // road decks, sidewalks and bridges (road surface wins when at/above terrain or embedded slightly)
        if (rz > th - 0.5f) {
            g.z = rz;
            g.normal = vec3(0, 0, 1);
            g.surface = SURF_ASPHALT;
        }
    }
    // Rooftops / prop tops
    thread_local std::vector<int> ids;
    collidersNear(vec2(x, y), 0.1f, ids);
    for (int id : ids) {
        const Collider& c = colliders[id];
        if (c.owner < 0) continue;
        float top = c.kind == COL_BOX ? c.c.z + c.he.z : c.c.z + c.he.z;
        if (top > limit || top <= g.z) continue;
        bool inside;
        if (c.kind == COL_BOX) {
            closestOnRect(vec2(x, y), c.c.xy(), c.ax, c.he.x, c.he.y, inside);
            if (!inside) continue;
        } else {
            if (length(vec2(x, y) - c.c.xy()) > c.he.x) continue;
        }
        g.z = top;
        g.normal = vec3(0, 0, 1);
        g.surface = c.surface;
    }
    float wl = m.waterAt(x, y);
    if (wl > World::kNoWater + 1.f && wl > g.z) {
        g.water = true;
        g.waterZ = wl;
    }
    return g;
}

bool CollisionWorld::raycast(vec3 o, vec3 dir, float maxDist, RayHit& hit, bool includeTerrain) const {
    hit.t = maxDist;
    bool any = false;
    // Static colliders via DDA over the grid
    vec2 d2 = dir.xy();
    float cellT = 0;
    int cx = (int)floorf(o.x / kCell), cy = (int)floorf(o.y / kCell);
    int stepX = d2.x >= 0 ? 1 : -1, stepY = d2.y >= 0 ? 1 : -1;
    float tDeltaX = fabsf(d2.x) > 1e-8f ? kCell / fabsf(d2.x) : 1e30f;
    float tDeltaY = fabsf(d2.y) > 1e-8f ? kCell / fabsf(d2.y) : 1e30f;
    float nextX = ((cx + (stepX > 0 ? 1 : 0)) * kCell - o.x) / (fabsf(d2.x) > 1e-8f ? d2.x : 1e-8f);
    float nextY = ((cy + (stepY > 0 ? 1 : 0)) * kCell - o.y) / (fabsf(d2.y) > 1e-8f ? d2.y : 1e-8f);
    if (fabsf(d2.x) <= 1e-8f) nextX = 1e30f;
    if (fabsf(d2.y) <= 1e-8f) nextY = 1e30f;
    thread_local std::vector<int> tested;
    tested.clear();
    for (int iter = 0; iter < 512 && cellT <= hit.t; iter++) {
        auto it = grid.find(gkey(cx, cy));
        if (it != grid.end()) {
            for (int id : it->second) {
                if (std::find(tested.begin(), tested.end(), id) != tested.end()) continue;
                tested.push_back(id);
                const Collider& c = colliders[id];
                if (c.owner < 0) continue;
                if (c.kind == COL_BOX) {
                    // transform ray into box space
                    vec3 ax(c.ax, 0), ay(perp(c.ax), 0), az(0, 0, 1);
                    vec3 lo = o - c.c;
                    vec3 lro(dot(lo, ax), dot(lo, ay), dot(lo, az));
                    vec3 lrd(dot(dir, ax), dot(dir, ay), dot(dir, az));
                    vec3 inv(1.f / (fabsf(lrd.x) > 1e-9f ? lrd.x : 1e-9f), 1.f / (fabsf(lrd.y) > 1e-9f ? lrd.y : 1e-9f), 1.f / (fabsf(lrd.z) > 1e-9f ? lrd.z : 1e-9f));
                    vec3 t1 = (-c.he - lro) * inv, t2 = (c.he - lro) * inv;
                    vec3 tmn = vmin(t1, t2), tmx = vmax(t1, t2);
                    float tn = Max(tmn.x, Max(tmn.y, tmn.z)), tf = Min(tmx.x, Min(tmx.y, tmx.z));
                    if (tn <= tf && tf > 0 && tn < hit.t && tn >= 0) {
                        hit.t = tn;
                        vec3 n;
                        if (tn == tmn.x) n = ax * (lrd.x > 0 ? -1.f : 1.f);
                        else if (tn == tmn.y) n = ay * (lrd.y > 0 ? -1.f : 1.f);
                        else n = az * (lrd.z > 0 ? -1.f : 1.f);
                        hit.normal = n;
                        hit.collider = id;
                        hit.surface = c.surface;
                        any = true;
                    }
                } else {
                    // vertical cylinder
                    vec2 oc = o.xy() - c.c.xy();
                    float a = dot(d2, d2);
                    if (a < 1e-10f) continue;
                    float b = dot(oc, d2), cc = dot(oc, oc) - c.he.x * c.he.x;
                    float disc = b * b - a * cc;
                    if (disc < 0) continue;
                    float t = (-b - sqrtf(disc)) / a;
                    if (t < 0 || t >= hit.t) continue;
                    float z = o.z + dir.z * t;
                    if (z < c.c.z || z > c.c.z + c.he.z) continue;
                    hit.t = t;
                    vec2 hp = o.xy() + d2 * t;
                    hit.normal = vec3(normalize(hp - c.c.xy()), 0);
                    hit.collider = id;
                    hit.surface = c.surface;
                    any = true;
                }
            }
        }
        if (nextX < nextY) { cellT = nextX; nextX += tDeltaX; cx += stepX; }
        else { cellT = nextY; nextY += tDeltaY; cy += stepY; }
        if (fabsf(d2.x) <= 1e-8f && fabsf(d2.y) <= 1e-8f) break;
    }
    // Terrain / road surfaces: march
    if (includeTerrain) {
        float step = 1.5f;
        float prevT = 0;
        vec3 p = o;
        GroundHit g0 = ground(p.x, p.y, p.z + 0.05f, 0.f);
        bool above = p.z >= g0.z;
        for (float t = step; t <= hit.t + step; t += step) {
            if (t > hit.t) t = hit.t;
            vec3 q = o + dir * t;
            GroundHit g = ground(q.x, q.y, q.z + 0.3f, 0.f);
            if (above && q.z < g.z) {
                // bisection refine
                float lo = prevT, hi = t;
                for (int k = 0; k < 8; k++) {
                    float mid = (lo + hi) * 0.5f;
                    vec3 mq = o + dir * mid;
                    GroundHit mg = ground(mq.x, mq.y, mq.z + 0.3f, 0.f);
                    if (mq.z < mg.z) hi = mid;
                    else lo = mid;
                }
                if (hi < hit.t) {
                    hit.t = hi;
                    vec3 hp = o + dir * hi;
                    GroundHit hg = ground(hp.x, hp.y, hp.z + 0.3f, 0.f);
                    hit.normal = hg.normal;
                    hit.surface = hg.surface;
                    hit.collider = -1;
                    any = true;
                }
                break;
            }
            prevT = t;
            if (t >= hit.t) break;
            step = Min(step * 1.08f, 6.f);
        }
    }
    if (any) hit.pos = o + dir * hit.t;
    return any;
}

bool CollisionWorld::capsuleOverlap(vec3 base, float radius, float height, vec3& push, vec3& normal) const {
    thread_local std::vector<int> ids;
    collidersNear(base.xy(), radius + 2.f, ids);
    bool any = false;
    push = vec3(0);
    normal = vec3(0);
    vec2 p = base.xy();
    for (int id : ids) {
        const Collider& c = colliders[id];
        if (c.owner < 0) continue;
        float bot = c.kind == COL_BOX ? c.c.z - c.he.z : c.c.z;
        float top = c.kind == COL_BOX ? c.c.z + c.he.z : c.c.z + c.he.z;
        if (base.z + height < bot || base.z > top - 0.35f) continue;  // step over low tops
        vec2 q = p + push.xy();
        if (c.kind == COL_BOX) {
            bool inside;
            vec2 cp = closestOnRect(q, c.c.xy(), c.ax, c.he.x, c.he.y, inside);
            vec2 d = q - cp;
            float dist = length(d);
            if (inside) {
                vec2 n = dist > 1e-5f ? -d / dist : vec2(1, 0);
                push += vec3(n * (dist + radius), 0);
                normal += vec3(n, 0);
                any = true;
            } else if (dist < radius) {
                vec2 n = dist > 1e-5f ? d / dist : vec2(1, 0);
                push += vec3(n * (radius - dist), 0);
                normal += vec3(n, 0);
                any = true;
            }
        } else {
            vec2 d = q - c.c.xy();
            float dist = length(d);
            float rr = radius + c.he.x;
            if (dist < rr) {
                vec2 n = dist > 1e-5f ? d / dist : vec2(1, 0);
                push += vec3(n * (rr - dist), 0);
                normal += vec3(n, 0);
                any = true;
            }
        }
    }
    if (any && length2(normal) > 1e-8f) normal = normalize(normal);
    return any;
}

void CollisionWorld::boxContacts(vec3 center, const mat3& rot, vec3 half, std::vector<Contact>& out) const {
    out.clear();
    thread_local std::vector<int> ids;
    float r = length(half);
    collidersNear(center.xy(), r + 1.f, ids);
    // Sample points on the box: corners + face centers of the 4 sides at mid height + edge midpoints
    vec3 pts[20];
    int n = 0;
    for (int k = 0; k < 8; k++)
        pts[n++] = center + rot * vec3(k & 1 ? half.x : -half.x, k & 2 ? half.y : -half.y, k & 4 ? half.z : -half.z);
    pts[n++] = center + rot * vec3(half.x, 0, 0);
    pts[n++] = center + rot * vec3(-half.x, 0, 0);
    pts[n++] = center + rot * vec3(0, half.y, 0);
    pts[n++] = center + rot * vec3(0, -half.y, 0);
    pts[n++] = center + rot * vec3(half.x, half.y * 0.5f, 0);
    pts[n++] = center + rot * vec3(-half.x, half.y * 0.5f, 0);
    pts[n++] = center + rot * vec3(half.x, -half.y * 0.5f, 0);
    pts[n++] = center + rot * vec3(-half.x, -half.y * 0.5f, 0);
    for (int id : ids) {
        const Collider& c = colliders[id];
        if (c.owner < 0) continue;
        if (c.kind == COL_BOX) {
            vec3 ax(c.ax, 0), ay(perp(c.ax), 0);
            for (int i = 0; i < n; i++) {
                vec3 d = pts[i] - c.c;
                float lx = dot(d, ax), ly = dot(d, ay), lz = d.z;
                float px = c.he.x - fabsf(lx), py = c.he.y - fabsf(ly), pz = c.he.z - fabsf(lz);
                if (px <= 0 || py <= 0 || pz <= 0) continue;
                Contact ct;
                ct.point = pts[i];
                ct.collider = id;
                // minimal penetration axis, prefer horizontal pushes for tall buildings
                if (px < py && px < pz) { ct.normal = ax * (lx >= 0 ? 1.f : -1.f); ct.depth = px; }
                else if (py < pz) { ct.normal = ay * (ly >= 0 ? 1.f : -1.f); ct.depth = py; }
                else { ct.normal = vec3(0, 0, lz >= 0 ? 1.f : -1.f); ct.depth = pz; }
                out.push_back(ct);
            }
        } else {
            // cylinder: closest point on the vehicle box to the cylinder axis (in box frame)
            mat3 rt = transpose(rot);
            vec3 lc = rt * (vec3(c.c.xy(), center.z) - center);
            vec3 cp(Clamp(lc.x, -half.x, half.x), Clamp(lc.y, -half.y, half.y), 0);
            vec3 wcp = center + rot * cp;
            if (wcp.z < c.c.z || wcp.z > c.c.z + c.he.z) {
                float zlow = center.z - half.z;
                if (zlow > c.c.z + c.he.z) continue;
            }
            vec2 d = wcp.xy() - c.c.xy();
            float dist = length(d);
            if (dist < c.he.x) {
                Contact ct;
                ct.point = wcp;
                ct.normal = vec3(dist > 1e-5f ? d / dist : vec2(1, 0), 0);
                ct.depth = c.he.x - dist;
                ct.collider = id;
                out.push_back(ct);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
void RigidBody::setBoxInertia(float m, vec3 h) {
    mass = m;
    invMass = 1.f / m;
    vec3 d = h * 2.f;
    float ix = m / 12.f * (d.y * d.y + d.z * d.z), iy = m / 12.f * (d.x * d.x + d.z * d.z), iz = m / 12.f * (d.x * d.x + d.y * d.y);
    invInertiaLocal = vec3(1.f / ix, 1.f / iy, 1.f / iz);
}
vec3 RigidBody::invInertiaWorldMul(vec3 v) const {
    mat3 R = rotMat();
    vec3 l = transpose(R) * v;
    l = l * invInertiaLocal;
    return R * l;
}
void RigidBody::applyImpulse(vec3 j, vec3 r) {
    vel += j * invMass;
    angVel += invInertiaWorldMul(cross(r, j));
}
void RigidBody::integrate(float dt) {
    vel += force * (invMass * dt);
    angVel += invInertiaWorldMul(torque) * dt;
    pos = pos + vel * (double)dt;
    // integrate orientation
    quat w(angVel.x, angVel.y, angVel.z, 0);
    quat dq = w * rot;
    rot = normalize(quat(rot.x + 0.5f * dt * dq.x, rot.y + 0.5f * dt * dq.y, rot.z + 0.5f * dt * dq.z, rot.w + 0.5f * dt * dq.w));
    force = vec3(0);
    torque = vec3(0);
}

void resolveStaticContact(RigidBody& b, vec3 r, vec3 n, float depth, float e, float mu, float dt) {
    vec3 v = b.pointVelocity(r);
    float vn = dot(v, n);
    // positional correction (split impulse style: direct move)
    float corr = Max(depth - 0.01f, 0.f) * 0.6f;
    b.pos = b.pos + n * (double)corr;
    if (vn >= 0) return;
    vec3 rn = cross(r, n);
    float k = b.invMass + dot(n, cross(b.invInertiaWorldMul(rn), r));
    float j = -(1.f + e) * vn / Max(k, 1e-8f);
    b.applyImpulse(n * j, r);
    // friction
    v = b.pointVelocity(r);
    vec3 vt = v - n * dot(v, n);
    float vtl = length(vt);
    if (vtl > 1e-4f) {
        vec3 t = vt / vtl;
        vec3 rt = cross(r, t);
        float kt = b.invMass + dot(t, cross(b.invInertiaWorldMul(rt), r));
        float jt = Min(vtl / Max(kt, 1e-8f), mu * j);
        b.applyImpulse(-t * jt, r);
    }
    (void)dt;
}

void resolveBodyContact(RigidBody& a, RigidBody& b, vec3 p, vec3 n, float depth, float e, float mu) {
    vec3 ra = rel(p, a.pos), rb = rel(p, b.pos);
    vec3 va = a.pointVelocity(ra), vb = b.pointVelocity(rb);
    vec3 vr = vb - va;
    float vn = dot(vr, n);
    float corr = Max(depth - 0.01f, 0.f) * 0.5f;
    float tw = a.invMass + b.invMass;
    if (tw > 0) {
        a.pos = a.pos - n * (double)(corr * a.invMass / tw);
        b.pos = b.pos + n * (double)(corr * b.invMass / tw);
    }
    if (vn >= 0) return;
    vec3 rna = cross(ra, n), rnb = cross(rb, n);
    float k = a.invMass + b.invMass + dot(n, cross(a.invInertiaWorldMul(rna), ra)) + dot(n, cross(b.invInertiaWorldMul(rnb), rb));
    float j = -(1.f + e) * vn / Max(k, 1e-8f);
    a.applyImpulse(-n * j, ra);
    b.applyImpulse(n * j, rb);
    va = a.pointVelocity(ra);
    vb = b.pointVelocity(rb);
    vr = vb - va;
    vec3 vt = vr - n * dot(vr, n);
    float vtl = length(vt);
    if (vtl > 1e-4f) {
        vec3 t = vt / vtl;
        vec3 rta = cross(ra, t), rtb = cross(rb, t);
        float kt = a.invMass + b.invMass + dot(t, cross(a.invInertiaWorldMul(rta), ra)) + dot(t, cross(b.invInertiaWorldMul(rtb), rb));
        float jt = Min(vtl / Max(kt, 1e-8f), mu * j);
        a.applyImpulse(t * jt, ra);
        b.applyImpulse(-t * jt, rb);
    }
}

bool obbObb(vec3 ca, const mat3& ra, vec3 ha, vec3 cb, const mat3& rb, vec3 hb, vec3& normal, float& depth, vec3& point) {
    vec3 A[3] = {ra.c[0], ra.c[1], ra.c[2]}, B[3] = {rb.c[0], rb.c[1], rb.c[2]};
    vec3 d = cb - ca;
    float best = 1e30f;
    vec3 bestAxis;
    auto test = [&](vec3 L) -> bool {
        float l2 = length2(L);
        if (l2 < 1e-8f) return true;
        L = L / sqrtf(l2);
        float rA = ha.x * fabsf(dot(A[0], L)) + ha.y * fabsf(dot(A[1], L)) + ha.z * fabsf(dot(A[2], L));
        float rB = hb.x * fabsf(dot(B[0], L)) + hb.y * fabsf(dot(B[1], L)) + hb.z * fabsf(dot(B[2], L));
        float dist = fabsf(dot(d, L));
        float pen = rA + rB - dist;
        if (pen < 0) return false;
        if (pen < best) {
            best = pen;
            bestAxis = dot(d, L) < 0 ? -L : L;
        }
        return true;
    };
    for (int i = 0; i < 3; i++) if (!test(A[i])) return false;
    for (int i = 0; i < 3; i++) if (!test(B[i])) return false;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) if (!test(cross(A[i], B[j]))) return false;
    normal = bestAxis;
    depth = best;
    // contact point: average of corners of each box inside the other
    vec3 acc(0);
    int cnt = 0;
    mat3 rat = transpose(ra), rbt = transpose(rb);
    for (int k = 0; k < 8; k++) {
        vec3 pb = cb + rb * vec3(k & 1 ? hb.x : -hb.x, k & 2 ? hb.y : -hb.y, k & 4 ? hb.z : -hb.z);
        vec3 l = rat * (pb - ca);
        if (fabsf(l.x) <= ha.x && fabsf(l.y) <= ha.y && fabsf(l.z) <= ha.z) { acc += pb; cnt++; }
        vec3 pa = ca + ra * vec3(k & 1 ? ha.x : -ha.x, k & 2 ? ha.y : -ha.y, k & 4 ? ha.z : -ha.z);
        vec3 l2 = rbt * (pa - cb);
        if (fabsf(l2.x) <= hb.x && fabsf(l2.y) <= hb.y && fabsf(l2.z) <= hb.z) { acc += pa; cnt++; }
    }
    point = cnt ? acc / (float)cnt : (ca + cb) * 0.5f;
    return true;
}

}  // namespace Phys
