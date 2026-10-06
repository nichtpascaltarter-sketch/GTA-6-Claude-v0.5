// GPU particle system (gameplay effects), tracers, deferred decals and tire skid marks.
// Included from renderer.cpp.
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

struct ParticleSystem {
    static const int kMaxRequestsPerFrame = 2048;
    std::vector<ParticleSpawn> requests;
    std::vector<TracerSpawn> tracers;

    void queue(const ParticleSpawn& s) {
        if ((int)requests.size() < kMaxRequestsPerFrame) requests.push_back(s);
    }
    void queueTracer(const TracerSpawn& t) {
        if ((int)tracers.size() < 256) tracers.push_back(t);
    }
};

struct DecalRequest {
    DecalType type;
    dvec3 pos;
    vec3 normal;
    float size, angle;
};
struct SkidRequest {
    int track;
    dvec3 pos;
    vec3 normal;
    float width, intensity;
};

struct DecalSystem {
    std::vector<DecalRequest> requests;
    std::vector<SkidRequest> skids;
    void queue(const DecalRequest& d) {
        if (requests.size() < 1024) requests.push_back(d);
    }
    void queueSkid(const SkidRequest& s) {
        if (skids.size() < 1024) skids.push_back(s);
    }
};

void Renderer::spawnParticles(ParticleType type, dvec3 pos, vec3 dir, int count, float scale, vec3 tint) {
    if (!particles || count <= 0 || (int)type < 0 || (int)type >= PT_COUNT) return;
    particles->queue({type, pos, dir, Min(count, 64), Max(scale, 0.01f), tint});
}
void Renderer::addDecal(DecalType type, dvec3 pos, vec3 normal, float size, float angle) {
    if (!decals || (int)type < 0 || (int)type >= DECAL_COUNT || size <= 0.f) return;
    float l = length(normal);
    decals->queue({type, pos, l > 1e-4f ? normal / l : vec3(0, 0, 1), size, angle});
}
void Renderer::addTracer(dvec3 from, dvec3 to) {
    if (!particles) return;
    particles->queueTracer({from, to});
}
void Renderer::addSkidMark(int trackId, dvec3 pos, vec3 normal, float width, float intensity) {
    if (!decals) return;
    float l = length(normal);
    decals->queueSkid({trackId, pos, l > 1e-4f ? normal / l : vec3(0, 0, 1), width, Saturate(intensity)});
}

}  // namespace Render
