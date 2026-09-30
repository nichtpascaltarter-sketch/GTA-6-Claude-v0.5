// GPU particle system for gameplay effects (smoke, fire, explosions, sparks, blood, water, debris...), tracers
// and effect lights. CPU: request queue, slot allocation, per-type emission and timed lights. GPU: simulation,
// bitonic depth sort, lit soft billboards. Included from renderer.cpp (decals live in decals.cpp).
#include <algorithm>

namespace Render {

// Request queues filled by the gameplay API; consumed each frame by render().
struct ParticleSpawn {
    ParticleType type;
    dvec3 pos;
    vec3 dir;
    int count;
    float scale;
    vec3 tint;
};
struct TracerSpawn {
    dvec3 from, to;
};

struct ParticleGPU {
    vec3 pos;
    float age;
    vec3 vel;
    float life;
    float size0, size1, rot, rotVel;
    u32 typeCell;
    u32 color;
    float seed;
    float intensity;
};
static_assert(sizeof(ParticleGPU) == 64, "ParticleGPU must match particles.hlsl");

struct ParticleTypeGPU {
    vec4 motion, color0, color1, render, atlas, extra;
};

struct ParticleCBData {
    vec4 sim0, sim1, sim2, sim3, sort, lightCount;
    vec4 lights[32];
};

// Internal particle types (after the public ones)
enum InternalParticleType : int {
    IPT_TRACER = PT_COUNT, IPT_SIDEFLASH, IPT_SHOCK, IPT_GLOW, IPT_BLOOD_MIST, IPT_TYPE_COUNT
};

struct TimedLight {
    dvec3 pos;
    vec3 color;
    float radius, age, life;
    int kind;   // 0 flash (fast quadratic decay), 1 fire (flicker, refreshed while burning), 2 explosion
};

namespace particles_detail {
inline u32 packColor(vec3 c, float a) {
    return packRGBA8(Saturate(c.x), Saturate(c.y), Saturate(c.z), Saturate(a));
}
}  // namespace particles_detail

struct ParticleSystem {
    static const int kMaxRequestsPerFrame = 2048;
    static const int kMaxSpawnPerFrame = 4096;
    std::vector<ParticleSpawn> requests;
    std::vector<TracerSpawn> tracers;

    // Pool
    int capacity = 0;
    std::vector<float> deathTime;
    int cursor = 0;
    int alive = 0;
    double simTime = 0.0;
    float maxDeath = 0.f;
    std::vector<ParticleGPU> spawnList;
    std::vector<u32> spawnSlots;
    dvec3 origin;
    bool originSet = false;
    vec3 pendingShift = vec3(0);
    u32 rngState = 0x12345678u;
    std::vector<TimedLight> lights;

    gfx::Buffer pool, keys, spawnBuf, slotBuf, typeBuf;
    gfx::Texture atlas;
    gfx::CBuffer<ParticleCBData> cb;
    ID3D11ComputeShader *csEmit = nullptr, *csSim = nullptr, *csKeys = nullptr, *csSortLocal = nullptr,
                        *csSortGlobal = nullptr, *csSortMerge = nullptr, *csAtlas = nullptr;
    gfx::VertexShader vs;
    ID3D11PixelShader* ps = nullptr;
    int drawCount = 0;

    void queue(const ParticleSpawn& s) {
        if ((int)requests.size() < kMaxRequestsPerFrame) requests.push_back(s);
    }
    void queueTracer(const TracerSpawn& t) {
        if ((int)tracers.size() < 256) tracers.push_back(t);
    }

    float rnd() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (rngState >> 8) * (1.f / 16777216.f);
    }
    float rnd(float a, float b) { return a + (b - a) * rnd(); }
    vec3 rndSphere() {
        for (int i = 0; i < 8; i++) {
            vec3 v(rnd() * 2.f - 1.f, rnd() * 2.f - 1.f, rnd() * 2.f - 1.f);
            if (length2(v) <= 1.f) return v;
        }
        return vec3(0, 0, 0);
    }
    vec3 rndDir() {
        vec3 v = rndSphere();
        float l = length(v);
        return l > 1e-3f ? v / l : vec3(0, 0, 1);
    }

    void init(int budget) {
        csEmit = gfx::loadCS("particles.hlsl", "csEmit");
        csSim = gfx::loadCS("particles.hlsl", "csSimulate");
        csKeys = gfx::loadCS("particles.hlsl", "csKeys");
        csSortLocal = gfx::loadCS("particles.hlsl", "csSortLocal");
        csSortGlobal = gfx::loadCS("particles.hlsl", "csSortGlobal");
        csSortMerge = gfx::loadCS("particles.hlsl", "csSortMerge");
        csAtlas = gfx::loadCS("particles.hlsl", "csGenAtlas");
        vs = gfx::loadVS("particles.hlsl", "vsParticle", nullptr, 0);
        ps = gfx::loadPS("particles.hlsl", "psParticle");
        cb.create();
        spawnBuf = gfx::createBuffer(kMaxSpawnPerFrame * sizeof(ParticleGPU), sizeof(ParticleGPU), gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        slotBuf = gfx::createBuffer(kMaxSpawnPerFrame * 4, 4, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        buildTypes();
        // Procedural atlas: 8x8 cells of 128x128
        atlas = gfx::createTexture2D(1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_GENMIPS, 0, 1);
        auto* c = gfx::ctx;
        c->CSSetShader(csAtlas, nullptr, 0);
        c->CSSetUnorderedAccessViews(2, 1, &atlas.uav, nullptr);
        c->Dispatch(1024 / 8, 1024 / 8, 1);
        gfx::unbindCSResources(1, 3);
        c->GenerateMips(atlas.srv);
        resize(budget);
    }

    void resize(int budget) {
        int n = 2048;
        while (n < budget && n < 65536) n <<= 1;
        if (n == capacity) return;
        capacity = n;
        pool.release();
        keys.release();
        std::vector<ParticleGPU> zero((size_t)capacity);
        memset(zero.data(), 0, zero.size() * sizeof(ParticleGPU));
        pool = gfx::createBuffer((u32)(capacity * sizeof(ParticleGPU)), sizeof(ParticleGPU), gfx::BUF_STRUCTURED | gfx::BUF_UAV, zero.data());
        keys = gfx::createBuffer((u32)(capacity * 8), 8, gfx::BUF_STRUCTURED | gfx::BUF_UAV);
        deathTime.assign((size_t)capacity, -1.f);
        cursor = 0;
        alive = 0;
        maxDeath = 0.f;
    }

    void buildTypes() {
        std::vector<ParticleTypeGPU> t((size_t)IPT_TYPE_COUNT);
        auto set = [&](int i, vec4 motion, vec4 c0, vec4 c1, vec4 render, vec4 atl, vec4 extra) {
            t[(size_t)i] = {motion, c0, c1, render, atl, extra};
        };
        // motion: gravity, drag, buoyancy, wind follow | render: emissive, softness, stretch (s), additive
        // atlas: first cell, count, animated, fade-in | extra: restitution (<0 none), flutter, size exponent, normal-mapped
        set(PT_SMOKE, vec4(0, 1.2f, 0.6f, 0.8f), vec4(0.62f, 0.62f, 0.64f, 0.5f), vec4(0.72f, 0.72f, 0.74f, 0), vec4(0, 1.0f, 0, 0), vec4(0, 8, 0, 0.12f), vec4(-1, 0, 0.6f, 1));
        set(PT_DARK_SMOKE, vec4(0, 0.8f, 1.2f, 0.7f), vec4(0.05f, 0.05f, 0.05f, 0.85f), vec4(0.13f, 0.13f, 0.13f, 0), vec4(0, 1.5f, 0, 0), vec4(0, 8, 0, 0.06f), vec4(-1, 0, 0.5f, 1));
        set(PT_DUST, vec4(0.05f, 2.0f, 0.15f, 0.6f), vec4(0.52f, 0.45f, 0.35f, 0.45f), vec4(0.56f, 0.5f, 0.4f, 0), vec4(0, 0.8f, 0, 0), vec4(40, 4, 0, 0.1f), vec4(-1, 0, 0.5f, 1));
        set(PT_SPARKS, vec4(1.0f, 0.3f, 0, 0), vec4(1, 0.8f, 0.5f, 1), vec4(1, 0.35f, 0.1f, 0), vec4(1, 0.05f, 0.035f, 1), vec4(16, 1, 0, 0), vec4(0.35f, 0, 1, 0));
        set(PT_FIRE, vec4(0, 1.5f, 3.0f, 0.3f), vec4(1, 1, 1, 0.9f), vec4(1, 0.5f, 0.3f, 0), vec4(1, 0.4f, 0, 0.6f), vec4(8, 8, 1, 0.1f), vec4(-1, 0, 0.7f, 0));
        set(PT_EXPLOSION, vec4(0, 2.5f, 2.0f, 0.2f), vec4(1, 1, 1, 1), vec4(0.4f, 0.35f, 0.3f, 0), vec4(1, 2.0f, 0, 0.45f), vec4(48, 8, 1, 0.02f), vec4(-1, 0, 0.35f, 0));
        set(PT_BLOOD, vec4(1.0f, 0.8f, 0, 0), vec4(0.14f, 0.005f, 0.004f, 1), vec4(0.1f, 0.004f, 0.003f, 0.9f), vec4(0, 0.05f, 0.02f, 0), vec4(20, 4, 0, 0), vec4(0.0f, 0, 1, 1));
        set(PT_WATER_SPLASH, vec4(1.0f, 0.5f, 0, 0.1f), vec4(0.85f, 0.9f, 0.95f, 0.7f), vec4(0.9f, 0.95f, 1, 0), vec4(0, 0.3f, 0.02f, 0), vec4(36, 4, 0, 0), vec4(-1, 0, 0.8f, 1));
        set(PT_WAKE_SPRAY, vec4(0.6f, 1.5f, 0, 0.3f), vec4(0.9f, 0.93f, 0.95f, 0.5f), vec4(0.95f, 0.97f, 1, 0), vec4(0, 0.4f, 0, 0), vec4(36, 4, 0, 0.1f), vec4(-1, 0, 0.6f, 1));
        set(PT_TIRE_SMOKE, vec4(0, 1.8f, 0.35f, 0.7f), vec4(0.82f, 0.82f, 0.82f, 0.55f), vec4(0.86f, 0.86f, 0.86f, 0), vec4(0, 1.0f, 0, 0), vec4(0, 8, 0, 0.15f), vec4(-1, 0, 0.5f, 1));
        set(PT_EXHAUST, vec4(0, 2.5f, 0.25f, 0.6f), vec4(0.45f, 0.45f, 0.45f, 0.3f), vec4(0.6f, 0.6f, 0.6f, 0), vec4(0, 0.4f, 0, 0), vec4(0, 8, 0, 0.2f), vec4(-1, 0, 0.6f, 1));
        set(PT_MUZZLE_FLASH, vec4(0, 0, 0, 0), vec4(1, 1, 1, 1), vec4(1, 0.8f, 0.6f, 0), vec4(1, 0.05f, 0, 1), vec4(17, 1, 0, 0), vec4(-1, 0, 1, 0));
        set(PT_GLASS, vec4(1.0f, 0.2f, 0, 0), vec4(0.8f, 0.9f, 0.95f, 0.9f), vec4(0.8f, 0.9f, 0.95f, 0.6f), vec4(0, 0.02f, 0, 0), vec4(28, 4, 0, 0), vec4(0.3f, 0, 1, 1));
        set(PT_DEBRIS, vec4(1.0f, 0.25f, 0, 0), vec4(0.45f, 0.43f, 0.4f, 1), vec4(0.45f, 0.43f, 0.4f, 0.9f), vec4(0, 0.02f, 0, 0), vec4(24, 4, 0, 0), vec4(0.3f, 0, 1, 1));
        set(PT_LEAVES, vec4(0.12f, 1.2f, 0, 0.9f), vec4(1, 1, 1, 1), vec4(0.9f, 0.85f, 0.7f, 0.9f), vec4(0, 0.05f, 0, 0), vec4(32, 4, 0, 0.05f), vec4(0.0f, 2.5f, 1, 1));
        set(PT_RAIN_SPLASH, vec4(0.3f, 0, 0, 0), vec4(0.8f, 0.85f, 0.9f, 0.5f), vec4(0.8f, 0.85f, 0.9f, 0), vec4(0, 0.02f, 0, 0), vec4(44, 1, 0, 0), vec4(-1, 0, 0.5f, 0));
        set(PT_STEAM, vec4(0, 1.0f, 0.8f, 0.6f), vec4(0.85f, 0.85f, 0.88f, 0.3f), vec4(0.9f, 0.9f, 0.92f, 0), vec4(0, 0.8f, 0, 0), vec4(0, 8, 0, 0.2f), vec4(-1, 0, 0.5f, 1));
        set(PT_EMBERS, vec4(0.05f, 0.8f, 1.2f, 0.5f), vec4(1, 0.6f, 0.2f, 1), vec4(1, 0.25f, 0.05f, 0), vec4(1, 0.05f, 0.02f, 1), vec4(47, 1, 0, 0), vec4(-1, 1.0f, 1, 0));
        set(IPT_TRACER, vec4(0, 0, 0, 0), vec4(1, 0.85f, 0.6f, 1), vec4(1, 0.7f, 0.4f, 0.8f), vec4(1, 0.05f, 0.035f, 1), vec4(46, 1, 0, 0), vec4(-1, 0, 1, 0));
        set(IPT_SIDEFLASH, vec4(0, 0, 0, 0), vec4(1, 1, 1, 1), vec4(1, 0.7f, 0.4f, 0), vec4(1, 0.05f, -0.5f, 1), vec4(18, 1, 0, 0), vec4(-1, 0, 1, 0));
        set(IPT_SHOCK, vec4(0, 0, 0, 0), vec4(1, 0.9f, 0.7f, 0.5f), vec4(1, 0.8f, 0.6f, 0), vec4(1, 1.0f, 0, 1), vec4(45, 1, 0, 0), vec4(-1, 0, 0.4f, 0));
        set(IPT_GLOW, vec4(0, 0, 0, 0), vec4(1, 0.8f, 0.5f, 1), vec4(1, 0.4f, 0.1f, 0), vec4(1, 2.0f, 0, 1), vec4(19, 1, 0, 0), vec4(-1, 0, 1, 0));
        set(IPT_BLOOD_MIST, vec4(0.1f, 3.0f, 0, 0.3f), vec4(0.2f, 0.01f, 0.008f, 0.45f), vec4(0.22f, 0.02f, 0.015f, 0), vec4(0, 0.3f, 0, 0), vec4(0, 8, 0, 0.05f), vec4(-1, 0, 0.5f, 1));
        typeBuf = gfx::createBuffer((u32)(t.size() * sizeof(ParticleTypeGPU)), sizeof(ParticleTypeGPU), gfx::BUF_STRUCTURED, t.data());
    }

    // --------------------------------------------------------------------------------------------
    // Emission
    int allocSlot() {
        float now = (float)simTime;
        for (int n = 0; n < capacity; n++) {
            int s = cursor;
            cursor = (cursor + 1) % capacity;
            if (deathTime[(size_t)s] < now) return s;
        }
        return -1;
    }

    void emit(int type, dvec3 pos, vec3 vel, float size0, float size1, float life, u32 cell, vec3 color, float alpha,
              float intensity = 0.f, float rotVel = 0.f) {
        if ((int)spawnList.size() >= kMaxSpawnPerFrame) return;
        int slot = allocSlot();
        if (slot < 0) return;
        ParticleGPU p;
        p.pos = rel(pos, origin) - pendingShift;  // csSimulate applies a pending re-base shift to every particle
        p.age = 0.f;
        p.vel = vel;
        p.life = Max(life, 0.01f);
        p.size0 = size0;
        p.size1 = size1;
        p.rot = rnd() * kTwoPi;
        p.rotVel = rotVel;
        p.typeCell = (u32)type | (cell << 8);
        p.color = particles_detail::packColor(color, alpha);
        p.seed = rnd();
        p.intensity = intensity;
        deathTime[(size_t)slot] = (float)simTime + p.life;
        maxDeath = Max(maxDeath, deathTime[(size_t)slot]);
        spawnList.push_back(p);
        spawnSlots.push_back((u32)slot);
    }

    void addTimedLight(dvec3 pos, vec3 color, float radius, float life, int kind) {
        if (kind == 1) {
            // fires refresh their light instead of stacking one per emission call
            for (TimedLight& l : lights)
                if (l.kind == 1 && length2(rel(l.pos, pos)) < 2.5f * 2.5f) {
                    l.age = 0.f;
                    l.color = color;
                    l.radius = radius;
                    return;
                }
        }
        if (lights.size() >= 64) return;
        lights.push_back({pos, color, radius, 0.f, life, kind});
    }

    void spawn(const ParticleSpawn& s) {
        const float sc = s.scale;
        const vec3 up(0, 0, 1);
        vec3 d = s.dir;
        float dl = length(d);
        vec3 dn = dl > 1e-4f ? d / dl : up;
        int n = s.count;
        switch (s.type) {
            case PT_SMOKE:
            case PT_STEAM:
            case PT_TIRE_SMOKE:
            case PT_EXHAUST:
            case PT_DARK_SMOKE:
            case PT_DUST: {
                float s0 = 0.6f, grow = 3.8f, life0 = 3.f, life1 = 6.f, spread = 0.3f, jit = 0.4f;
                if (s.type == PT_DARK_SMOKE) { s0 = 1.0f; grow = 4.5f; life0 = 5.f; life1 = 9.f; }
                else if (s.type == PT_DUST) { s0 = 0.5f; grow = 3.f; life0 = 1.5f; life1 = 3.f; spread = 0.4f; jit = 0.8f; }
                else if (s.type == PT_TIRE_SMOKE) { s0 = 0.45f; grow = 6.f; life0 = 2.f; life1 = 4.f; spread = 0.2f; jit = 0.5f; }
                else if (s.type == PT_EXHAUST) { s0 = 0.08f; grow = 6.f; life0 = 0.8f; life1 = 1.4f; spread = 0.03f; jit = 0.2f; }
                else if (s.type == PT_STEAM) { s0 = 0.4f; grow = 3.f; life0 = 2.f; life1 = 4.f; spread = 0.2f; jit = 0.2f; }
                for (int i = 0; i < n; i++) {
                    vec3 o = rndSphere() * (spread * sc);
                    if (s.type == PT_DUST || s.type == PT_TIRE_SMOKE) o.z = fabsf(o.z) * 0.3f;
                    vec3 v = d + rndSphere() * jit;
                    if (s.type == PT_DARK_SMOKE) v += up * 0.8f;
                    float sz = s0 * sc * rnd(0.8f, 1.25f);
                    float b = rnd(0.9f, 1.1f);
                    emit(s.type, s.pos + o, v, sz, sz * grow * rnd(0.8f, 1.2f), rnd(life0, life1), (u32)(rnd() * 7.99f) + (s.type == PT_DUST ? 40u : 0u),
                         s.tint * b, 1.f, 0.f, rnd(-0.4f, 0.4f));
                }
                break;
            }
            case PT_SPARKS: {
                for (int i = 0; i < n * 2; i++) {
                    vec3 v = dn * rnd(3.f, 9.f) * (0.6f + 0.4f * sc) + rndSphere() * 2.5f;
                    emit(PT_SPARKS, s.pos, v, rnd(0.02f, 0.035f), 0.015f, rnd(0.3f, 0.9f), 16, s.tint, 1.f, rnd(12000.f, 25000.f));
                }
                break;
            }
            case PT_EMBERS: {
                for (int i = 0; i < n; i++) {
                    vec3 v = up * rnd(1.f, 2.2f) + rndSphere() * 1.0f + d;
                    emit(PT_EMBERS, s.pos + rndSphere() * 0.3f * sc, v, rnd(0.02f, 0.04f), 0.01f, rnd(1.f, 3.f), 47, s.tint, 1.f, rnd(4000.f, 9000.f));
                }
                break;
            }
            case PT_FIRE: {
                for (int i = 0; i < n; i++) {
                    vec3 o = rndSphere() * 0.3f * sc;
                    o.z = fabsf(o.z) * 0.4f;
                    vec3 v = up * rnd(1.f, 2.f) * (0.7f + 0.3f * sc) + rndSphere() * 0.3f + d * 0.3f;
                    float sz = rnd(0.5f, 0.8f) * sc;
                    emit(PT_FIRE, s.pos + o, v, sz, sz * rnd(1.2f, 1.6f), rnd(0.5f, 1.1f), 8, s.tint, 1.f, rnd(2200.f, 3500.f), rnd(-1.f, 1.f));
                }
                if (rnd() < 0.3f) spawn({PT_EMBERS, s.pos, vec3(0), 1, sc, vec3(1)});
                if (rnd() < 0.25f) spawn({PT_DARK_SMOKE, s.pos + dvec3(0, 0, 1.2 * sc), vec3(0, 0, 1.2f), 1, sc * 0.8f, vec3(1)});
                float fl = 0.75f + 0.25f * sinf((float)simTime * 23.f + (float)s.pos.x) * sinf((float)simTime * 13.7f);
                addTimedLight(s.pos + dvec3(0, 0, 0.6 * sc), vec3(1.0f, 0.5f, 0.18f) * (1600.f * sc * sc * fl), 9.f * sc, 0.15f, 1);
                break;
            }
            case PT_EXPLOSION: {
                for (int i = 0; i < 14; i++) {
                    vec3 v = rndDir() * rnd(3.f, 7.f) * sc + up * 2.f * sc;
                    float sz = rnd(2.f, 3.f) * sc;
                    emit(PT_EXPLOSION, s.pos + rndSphere() * 1.5f * sc, v, sz, sz * 2.2f, rnd(0.9f, 1.6f), 48, s.tint, 1.f, rnd(5000.f, 8000.f), rnd(-0.6f, 0.6f));
                }
                emit(IPT_GLOW, s.pos, vec3(0), 12.f * sc, 16.f * sc, 0.25f, 19, vec3(1), 1.f, 20000.f);
                emit(IPT_SHOCK, s.pos, vec3(0), 1.f, 26.f * sc, 0.35f, 45, vec3(1), 1.f, 2500.f);
                for (int i = 0; i < 12; i++) {
                    vec3 v = rndDir() * rnd(1.f, 3.f) * sc + up * rnd(1.5f, 3.f);
                    float sz = rnd(2.5f, 3.5f) * sc;
                    emit(PT_DARK_SMOKE, s.pos + rndSphere() * 2.f * sc, v, sz, sz * 3.2f, rnd(6.f, 10.f), (u32)(rnd() * 7.99f), vec3(1), 1.f, 0.f, rnd(-0.3f, 0.3f));
                }
                spawn({PT_SPARKS, s.pos, up * 2.f, 15, sc, vec3(1)});
                spawn({PT_DEBRIS, s.pos, up * 6.f, 12, sc, vec3(0.8f, 0.78f, 0.75f)});
                spawn({PT_EMBERS, s.pos, vec3(0), 16, sc, vec3(1)});
                addTimedLight(s.pos + dvec3(0, 0, 1.0), vec3(1.0f, 0.6f, 0.25f) * (180000.f * sc * sc), 32.f * sc, 1.4f, 2);
                break;
            }
            case PT_BLOOD: {
                for (int i = 0; i < n * 2; i++) {
                    vec3 v = d * rnd(0.8f, 1.6f) + dn * rnd(0.5f, 2.f) + rndSphere() * 1.2f;
                    emit(PT_BLOOD, s.pos, v, rnd(0.015f, 0.04f) * sc, 0.03f * sc, rnd(0.5f, 1.0f), 20 + (u32)(rnd() * 3.99f), s.tint, 1.f);
                }
                for (int i = 0; i < 2; i++)
                    emit(IPT_BLOOD_MIST, s.pos, d * 0.4f + rndSphere() * 0.3f, 0.2f * sc, 0.6f * sc, 0.35f, (u32)(rnd() * 7.99f), s.tint, 1.f);
                break;
            }
            case PT_WATER_SPLASH: {
                for (int i = 0; i < n * 3; i++) {
                    vec3 v = up * rnd(2.5f, 5.f) * sc + d + rndSphere() * 1.5f;
                    emit(PT_WATER_SPLASH, s.pos + rndSphere() * 0.3f * sc, v, rnd(0.05f, 0.15f) * sc, 0.12f * sc, rnd(0.8f, 1.4f), 36 + (u32)(rnd() * 3.99f), s.tint, 1.f);
                }
                spawn({PT_WAKE_SPRAY, s.pos, d * 0.3f + up * 0.8f, 3, sc, s.tint});
                break;
            }
            case PT_WAKE_SPRAY: {
                for (int i = 0; i < n; i++) {
                    vec3 v = d + up * rnd(0.5f, 1.5f) + rndSphere() * 0.8f;
                    float sz = rnd(0.3f, 0.5f) * sc;
                    emit(PT_WAKE_SPRAY, s.pos + rndSphere() * 0.3f * sc, v, sz, sz * 3.f, rnd(0.6f, 1.2f), 36 + (u32)(rnd() * 3.99f), s.tint, 1.f);
                }
                break;
            }
            case PT_MUZZLE_FLASH: {
                emit(PT_MUZZLE_FLASH, s.pos, vec3(0), 0.35f * sc * rnd(0.85f, 1.15f), 0.4f * sc, 0.05f, 17, s.tint, 1.f, 30000.f);
                emit(IPT_SIDEFLASH, s.pos, dn * 4.f * sc, 0.22f * sc, 0.25f * sc, 0.05f, 18, s.tint, 1.f, 25000.f);
                for (int i = 0; i < 3; i++)
                    emit(PT_SPARKS, s.pos, dn * rnd(6.f, 12.f) + rndSphere() * 2.f, 0.015f, 0.01f, rnd(0.08f, 0.2f), 16, vec3(1), 1.f, 15000.f);
                addTimedLight(s.pos, vec3(1.0f, 0.72f, 0.4f) * (3000.f * sc), 7.f * sc, 0.06f, 0);
                break;
            }
            case PT_GLASS: {
                for (int i = 0; i < n * 2; i++) {
                    vec3 v = d * rnd(0.5f, 1.2f) + dn * rnd(1.f, 3.f) + rndSphere() * 2.f + up;
                    emit(PT_GLASS, s.pos + rndSphere() * 0.1f * sc, v, rnd(0.02f, 0.07f) * sc, 0.05f * sc, rnd(2.f, 4.f), 28 + (u32)(rnd() * 3.99f), s.tint, 1.f, 0.f, rnd(-10.f, 10.f));
                }
                break;
            }
            case PT_DEBRIS: {
                for (int i = 0; i < n; i++) {
                    vec3 v = d * rnd(0.5f, 1.3f) + dn * rnd(1.f, 3.f) + rndSphere() * 2.f + up * 2.f;
                    float sz = rnd(0.04f, 0.15f) * sc;
                    emit(PT_DEBRIS, s.pos + rndSphere() * 0.2f * sc, v, sz, sz, rnd(2.f, 5.f), 24 + (u32)(rnd() * 3.99f), s.tint * rnd(0.8f, 1.1f), 1.f, 0.f, rnd(-8.f, 8.f));
                }
                break;
            }
            case PT_LEAVES: {
                static const vec3 kLeaf[4] = {vec3(0.22f, 0.38f, 0.08f), vec3(0.35f, 0.4f, 0.1f), vec3(0.45f, 0.35f, 0.12f), vec3(0.3f, 0.2f, 0.08f)};
                for (int i = 0; i < n; i++) {
                    vec3 v = d + rndSphere() * 0.5f;
                    vec3 col = kLeaf[(int)(rnd() * 3.99f)] * s.tint;
                    emit(PT_LEAVES, s.pos + rndSphere() * sc, v, rnd(0.06f, 0.12f), 0.1f, rnd(3.f, 6.f), 32 + (u32)(rnd() * 3.99f), col, 1.f, 0.f, rnd(-3.f, 3.f));
                }
                break;
            }
            case PT_RAIN_SPLASH: {
                for (int i = 0; i < n; i++) {
                    float sz = rnd(0.08f, 0.14f) * sc;
                    emit(PT_RAIN_SPLASH, s.pos + dvec3(0, 0, sz * 0.3), vec3(0), sz, sz * 1.3f, rnd(0.12f, 0.2f), 44, s.tint, 1.f);
                }
                break;
            }
            default: break;
        }
    }

    void spawnTracer(const TracerSpawn& t) {
        vec3 dv = rel(t.to, t.from);
        float dist = length(dv);
        if (dist < 0.5f) return;
        const float speed = 700.f;
        vec3 dir = dv / dist;
        emit(IPT_TRACER, t.from + dir * 1.5f, dir * speed, 0.03f, 0.025f, Max((dist - 1.5f) / speed, 0.02f), 46, vec3(1), 1.f, 15000.f);
    }

    // --------------------------------------------------------------------------------------------
    // Per-frame CPU update: emission, timed lights (added to the renderer's dynamic lights).
    void updateCPU(Renderer& r, float dt) {
        simTime += dt;
        resize(r.settings.particleBudget);
        dvec3 cam = r.camera.pos;
        if (!originSet) {
            origin = cam;
            originSet = true;
        } else if (length(rel(cam, origin)) > 3000.f) {
            // re-base the float positions around the camera (applied to live particles in csSimulate)
            dvec3 newOrigin = cam;
            pendingShift = rel(origin, newOrigin);
            origin = newOrigin;
        }
        spawnList.clear();
        spawnSlots.clear();
        for (const ParticleSpawn& s : requests) spawn(s);
        for (const TracerSpawn& t : tracers) spawnTracer(t);
        requests.clear();
        tracers.clear();
        // timed lights
        for (size_t i = 0; i < lights.size();) {
            TimedLight& l = lights[i];
            l.age += dt;
            if (l.age >= l.life) {
                lights[i] = lights.back();
                lights.pop_back();
                continue;
            }
            float x = l.age / l.life;
            float k = l.kind == 0 ? (1.f - x) * (1.f - x) : (l.kind == 1 ? 1.f : (x < 0.1f ? 1.f : powf(1.f - (x - 0.1f) / 0.9f, 2.5f)));
            DynamicLight dl;
            dl.pos = l.pos;
            dl.color = l.color * k;
            dl.radius = l.radius;
            r.addLight(dl);
            i++;
        }
        float now = (float)simTime;
        alive = 0;
        if (maxDeath > now) {
            for (float t : deathTime) alive += t > now;
        }
    }

    // GPU: emit + simulate + sort. Needs the frame's depth/normal (collisions).
    void simulate(Renderer& r, float dt) {
        auto* c = gfx::ctx;
        bool any = alive > 0 || !spawnList.empty();
        if (!any) {
            drawCount = 0;
            return;
        }
        cb.data.sim0 = vec4(dt, (float)spawnList.size(), (float)capacity, (float)simTime);
        cb.data.sim1 = vec4(rel(origin, r.camera.pos), 0);
        cb.data.sim2 = vec4((float)origin.x, (float)origin.y, (float)origin.z, 0);
        cb.data.sim3 = vec4(pendingShift, 2.f + r.frame.weather.w * 10.f);
        cb.data.sort = vec4(0, 0, (float)capacity, 0);
        cb.data.lightCount = vec4(0);
        cb.upload();
        pendingShift = vec3(0);
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        if (!spawnList.empty()) {
            gfx::updateBuffer(spawnBuf, spawnList.data(), (u32)(spawnList.size() * sizeof(ParticleGPU)));
            gfx::updateBuffer(slotBuf, spawnSlots.data(), (u32)(spawnSlots.size() * 4));
            ID3D11ShaderResourceView* s[3] = {typeBuf.srv, spawnBuf.srv, slotBuf.srv};
            c->CSSetShaderResources(0, 3, s);
            c->CSSetUnorderedAccessViews(0, 1, &pool.uav, nullptr);
            c->CSSetShader(csEmit, nullptr, 0);
            c->Dispatch(gfx::divUp((u32)spawnList.size(), 64), 1, 1);
            // emitted particles advance with the rest this frame
        }
        ID3D11ShaderResourceView* s[5] = {typeBuf.srv, nullptr, nullptr, r.depth.srv, r.gbNormal.srv};
        c->CSSetShaderResources(0, 5, s);
        c->CSSetUnorderedAccessViews(0, 1, &pool.uav, nullptr);
        c->CSSetShader(csSim, nullptr, 0);
        c->Dispatch(gfx::divUp(capacity, 64), 1, 1);
        gfx::unbindCSResources(8, 2);
        // keys + bitonic sort
        ID3D11ShaderResourceView* ps5 = pool.srv;
        c->CSSetShaderResources(5, 1, &ps5);
        c->CSSetUnorderedAccessViews(1, 1, &keys.uav, nullptr);
        c->CSSetShader(csKeys, nullptr, 0);
        c->Dispatch(gfx::divUp(capacity, 64), 1, 1);
        ID3D11ShaderResourceView* ns = nullptr;
        c->CSSetShaderResources(5, 1, &ns);
        int groups = capacity / 2048;
        c->CSSetShader(csSortLocal, nullptr, 0);
        c->Dispatch(groups, 1, 1);
        for (int k = 4096; k <= capacity; k <<= 1) {
            for (int j = k >> 1; j >= 2048; j >>= 1) {
                cb.data.sort = vec4((float)k, (float)j, (float)capacity, 0);
                cb.upload();
                c->CSSetShader(csSortGlobal, nullptr, 0);
                c->Dispatch(gfx::divUp(capacity / 2, 256), 1, 1);
            }
            cb.data.sort = vec4((float)k, 1024.f, (float)capacity, 0);
            cb.upload();
            c->CSSetShader(csSortMerge, nullptr, 0);
            c->Dispatch(groups, 1, 1);
        }
        ID3D11UnorderedAccessView* nu = nullptr;
        c->CSSetUnorderedAccessViews(1, 1, &nu, nullptr);
        drawCount = Min(alive, capacity);
    }

    // Forward pass into the HDR target: depth test against the scene (read-only), soft edges from the depth SRV.
    void draw(Renderer& r, ID3D11RenderTargetView* reactive, ID3D11BlendState* blendWithReactive) {
        if (drawCount <= 0) return;
        auto* c = gfx::ctx;
        // Local lights for particle lighting: the brightest nearby lights of this frame
        const std::vector<LightGPU>& lf = r.lightsFrame;
        std::vector<std::pair<float, int>> best;
        for (int i = 0; i < (int)lf.size(); i++) {
            float d = length(lf[(size_t)i].pos);
            if (d > 120.f) continue;
            float w = (lf[(size_t)i].color.x + lf[(size_t)i].color.y + lf[(size_t)i].color.z) / Max(d * d, 4.f);
            best.push_back({-w, i});
        }
        std::sort(best.begin(), best.end());
        int nl = Min((int)best.size(), 16);
        for (int i = 0; i < nl; i++) {
            const LightGPU& L = lf[(size_t)best[(size_t)i].second];
            cb.data.lights[i * 2] = vec4(L.pos, L.radius);
            cb.data.lights[i * 2 + 1] = vec4(L.color, 0);
        }
        cb.data.lightCount = vec4((float)nl, 0, 0, 0);
        cb.data.sim1 = vec4(rel(origin, r.camera.pos), 0);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get(), nullptr, r.shadowCB.get()};
        c->VSSetConstantBuffers(0, 4, cbs);
        c->PSSetConstantBuffers(0, 4, cbs);
        ID3D11ShaderResourceView* srvs[9] = {typeBuf.srv, nullptr, nullptr, nullptr, nullptr, pool.srv, keys.srv, atlas.srv, r.depth.srv};
        c->VSSetShaderResources(0, 9, srvs);
        c->PSSetShaderResources(0, 9, srvs);
        ID3D11RenderTargetView* rts[2] = {r.hdr.rtv, reactive};
        c->OMSetRenderTargets(2, rts, r.depthRO);
        gfx::setViewport((float)r.width, (float)r.height);
        c->OMSetDepthStencilState(gfx::states.depthGreaterEqualNoWrite, 0);
        float bf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(blendWithReactive, bf, 0xffffffff);
        c->RSSetState(gfx::states.cullNone);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        c->VSSetShader(vs.vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->DrawInstanced(4, (UINT)drawCount, 0, 0);
        r.stats.drawCalls++;
        c->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* nulls[9] = {};
        c->VSSetShaderResources(0, 9, nulls);
        c->PSSetShaderResources(0, 9, nulls);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullBack);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
};

void Renderer::spawnParticles(ParticleType type, dvec3 pos, vec3 dir, int count, float scale, vec3 tint) {
    if (!particles || count <= 0 || (int)type < 0 || (int)type >= PT_COUNT) return;
    particles->queue({type, pos, dir, Min(count, 64), Max(scale, 0.01f), tint});
}
void Renderer::addTracer(dvec3 from, dvec3 to) {
    if (!particles) return;
    particles->queueTracer({from, to});
}

}  // namespace Render
