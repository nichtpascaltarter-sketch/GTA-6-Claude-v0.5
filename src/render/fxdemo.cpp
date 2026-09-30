// --fxdemo: stages gameplay effects in front of the camera (fire, smoke, sparks, explosions, tracers, muzzle
// flashes, decals and skid marks) for visual testing of the particle / decal systems without gameplay code.
// Included from renderer.cpp.
#include "../world/roads.h"

namespace Render {

namespace fxdemo_detail {
double groundAt(double x, double y) {
    float h = World::gMap ? World::gMap->heightAt((float)x, (float)y) : 0.f;
    float rz = 0.f;
    if (World::gRoads && World::gRoads->surfaceHeight(vec2((float)x, (float)y), &rz)) h = Max(h, rz);
    return h;
}
}  // namespace fxdemo_detail

void Renderer::fxDemo(float dt) {
    using fxdemo_detail::groundAt;
    static float t = 0.f, nextExplosion = 0.5f, nextTracer = 0.f, nextFlash = 0.f, acc20 = 0.f;
    static bool decalsPlaced = false;
    static dvec3 anchor;
    static float anchorYaw = 0.f;
    if (!decalsPlaced || length(rel(camera.pos, anchor)) > 5.f) {
        anchor = camera.pos;
        anchorYaw = camera.yaw;
        decalsPlaced = false;
    }
    t += dt;
    vec3 f(-sinf(anchorYaw), cosf(anchorYaw), 0.f), r(cosf(anchorYaw), sinf(anchorYaw), 0.f);
    auto at = [&](float ahead, float right) {
        dvec3 p = anchor + dvec3(f.x * ahead + r.x * right, f.y * ahead + r.y * right, 0.0);
        p.z = groundAt(p.x, p.y);
        return p;
    };
    acc20 += dt;
    bool tick20 = acc20 >= 0.05f;
    if (tick20) acc20 -= 0.05f;
    if (tick20) {
        spawnParticles(PT_FIRE, at(14.f, -4.f), vec3(0, 0, 0.5f), 3, 1.2f);
        spawnParticles(PT_TIRE_SMOKE, at(10.f, 3.5f) + dvec3(0, 0, 0.3), f * 1.5f + vec3(0, 0, 0.4f), 1, 1.f);
        spawnParticles(PT_SPARKS, at(7.f, 2.2f) + dvec3(0, 0, 0.6), vec3(-r.x, -r.y, 0.4f), 2, 1.f);
        spawnParticles(PT_EXHAUST, at(6.f, -1.8f) + dvec3(0, 0, 0.35), -f * 1.2f, 1, 1.f);
        spawnParticles(PT_STEAM, at(18.f, 1.f), vec3(0, 0, 0.8f), 1, 1.f);
    }
    if (t >= nextExplosion) {
        nextExplosion = t + 3.2f;
        spawnParticles(PT_EXPLOSION, at(28.f, 7.f) + dvec3(0, 0, 1.0), vec3(0, 0, 1), 1, 1.0f);
        spawnParticles(PT_GLASS, at(9.f, -3.f) + dvec3(0, 0, 1.2), vec3(r.x, r.y, 0.5f), 12, 1.f);
        spawnParticles(PT_DEBRIS, at(9.f, 3.f) + dvec3(0, 0, 0.5), vec3(-r.x, -r.y, 1.f), 10, 1.f);
        spawnParticles(PT_BLOOD, at(8.f, -1.f) + dvec3(0, 0, 1.2), f * 2.f, 8, 1.f);
        spawnParticles(PT_WATER_SPLASH, at(12.f, 0.f), vec3(0, 0, 1), 6, 1.f);
        spawnParticles(PT_LEAVES, at(11.f, -6.f) + dvec3(0, 0, 4.0), vec3(0.5f, 0.2f, 0), 10, 1.f);
    }
    if (t >= nextTracer) {
        nextTracer = t + 0.07f;   // a tracer in flight in nearly every captured frame
        addTracer(at(3.f, -4.f) + dvec3(0, 0, 1.5), at(60.f, 8.f) + dvec3(0, 0, 1.8));
        addTracer(at(-1.f, 0.9f) + dvec3(0, 0, 1.6), at(45.f, -2.5f) + dvec3(0, 0, 1.4));   // shot fired right past the camera
    }
    if (t >= nextFlash) {
        nextFlash = t + 0.12f;
        spawnParticles(PT_MUZZLE_FLASH, at(5.f, 1.2f) + dvec3(0, 0, 1.4), f, 1, 1.f);
    }
    if (!decalsPlaced) {
        decalsPlaced = true;
        vec3 up(0, 0, 1);
        for (int i = 0; i < 6; i++) addDecal(DECAL_BULLET_CONCRETE, at(5.5f + i * 0.35f, -1.2f + (i % 3) * 0.4f), up, 0.08f, i * 1.3f);
        addDecal(DECAL_BLOOD, at(7.5f, 0.3f), up, 0.9f, 0.7f);
        addDecal(DECAL_BLOOD_POOL, at(8.2f, -0.6f), up, 1.1f, 0.2f);
        addDecal(DECAL_SCORCH, at(28.f, 7.f), up, 5.f, 1.1f);
        addDecal(DECAL_SCORCH, at(13.f, 4.f), up, 2.5f, 2.1f);
        for (int i = 0; i < 4; i++) addDecal(DECAL_BULLET_METAL, at(6.5f + i * 0.4f, 1.5f), up, 0.06f, i * 0.9f);
    }
    // two curved skid strips (rear wheels of a drifting car)
    for (int w = 0; w < 2; w++) {
        float s = fmodf(t * 6.f, 26.f);
        float ahead = 4.f + s, lateral = -3.f + sinf(s * 0.18f) * 2.5f + w * 1.6f;
        addSkidMark(1000 + w, at(ahead, lateral), vec3(0, 0, 1), 0.24f, 0.9f);
    }
}

}  // namespace Render
