// Native test for the wildlife models (src/game/animal_models.cpp): builds every species / variant, validates the
// meshes (finite data, bone indices, weights, consistent outward winding), exercises the procedural animation
// (finite skinning matrices, quadruped feet on the ground during stance, leash reaching the hand) and the boids
// kernel, and renders a contact sheet of posed models with a small software rasterizer (PPM) for visual checks.
// Build: g++ -O2 -std=c++17 -Isrc tests/wildlife/test_fauna.cpp -o /tmp/test_fauna -lpthread
// Run:   /tmp/test_fauna [out.ppm] [pose] [species substring] [cell px]   (pose: stand | walk | run | fly | sit)
#include "../../tools/native_stubs.cpp"
#include "../../src/core/math.cpp"
#include "../../src/core/noise.cpp"
#include "../../src/game/animal_models.cpp"
#include <cstring>

using namespace Fauna;

static int g_fail = 0;
#define EXPECT(c, ...)                                   \
    do {                                                 \
        if (!(c)) {                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
            printf(__VA_ARGS__);                         \
            printf("\n");                                \
            g_fail++;                                    \
        }                                                \
    } while (0)

static bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Winding check: fraction of triangles whose face normal agrees with their vertex normals
static float windingScore(const SkinnedMeshData& m) {
    int ok = 0, tot = 0;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const VtxSkinned& a = m.verts[m.indices[t]];
        const VtxSkinned& b = m.verts[m.indices[t + 1]];
        const VtxSkinned& c = m.verts[m.indices[t + 2]];
        vec3 fn = cross(b.pos - a.pos, c.pos - a.pos);
        if (length2(fn) < 1e-14f) continue;
        vec3 vn = unpackNormalOct(a.normal) + unpackNormalOct(b.normal) + unpackNormalOct(c.normal);
        tot++;
        if (dot(fn, vn) > 0.f) ok++;
    }
    return tot ? (float)ok / (float)tot : 1.f;
}

static void validateMesh(const char* what, const SkinnedMeshData& m, int maxBone, float minWinding = 0.93f) {
    EXPECT(!m.indices.empty(), "%s: empty", what);
    EXPECT(m.indices.size() % 3 == 0, "%s: index count", what);
    for (u32 i : m.indices) {
        if (i >= m.verts.size()) {
            EXPECT(false, "%s: index out of range", what);
            break;
        }
    }
    int badW = 0, badB = 0, badP = 0;
    for (const VtxSkinned& v : m.verts) {
        int s = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
        if (s != 255) badW++;
        for (int k = 0; k < 4; k++)
            if (v.weights[k] > 0 && v.bones[k] >= maxBone) badB++;
        if (!finite3(v.pos)) badP++;
    }
    EXPECT(badW == 0, "%s: %d vertices with weights != 255", what, badW);
    EXPECT(badB == 0, "%s: %d vertices with bone >= %d", what, badB, maxBone);
    EXPECT(badP == 0, "%s: %d non-finite positions", what, badP);
    float w = windingScore(m);
    EXPECT(w > minWinding, "%s: winding consistency %.3f", what, w);
}

// ---- CPU skinning + software rasterizer --------------------------------------------------------------------------
struct Image {
    int w, h;
    std::vector<vec3> c;
    std::vector<float> z;
    Image(int W, int H) : w(W), h(H), c((size_t)W * H, vec3(0.62f, 0.7f, 0.78f)), z((size_t)W * H, 1e30f) {}
};

static void drawModel(Image& img, int x0, int y0, int size, const SkinnedMeshData& m, const mat4* skin, float yawDeg, float pitchDeg,
                      vec3 center, float extent) {
    float yaw = yawDeg * kDegToRad, pit = pitchDeg * kDegToRad;
    // camera basis: looking at the centre from yaw/pitch
    vec3 fwd(-sinf(yaw) * cosf(pit), cosf(yaw) * cosf(pit), sinf(pit));
    vec3 right = normalize(cross(fwd, vec3(0, 0, 1)));
    vec3 up = cross(right, fwd);
    vec3 L = normalize(vec3(-0.4f, -0.5f, 0.8f));
    std::vector<vec3> P(m.verts.size()), N(m.verts.size()), Cc(m.verts.size());
    for (size_t i = 0; i < m.verts.size(); i++) {
        const VtxSkinned& v = m.verts[i];
        vec3 p(0.f), n(0.f);
        vec3 n0 = unpackNormalOct(v.normal);
        for (int k = 0; k < 4; k++) {
            if (!v.weights[k]) continue;
            float wt = v.weights[k] / 255.f;
            const mat4& M = skin[v.bones[k]];
            p += transformPoint(M, v.pos) * wt;
            n += transformDir(M, n0) * wt;
        }
        P[i] = p;
        N[i] = length2(n) > 1e-12f ? normalize(n) : vec3(0, 0, 1);
        vec4 col = unpackRGBA8(v.color);
        // linear -> display
        auto enc = [](float x) { return x <= 0.0031308f ? x * 12.92f : 1.055f * powf(x, 1.f / 2.4f) - 0.055f; };
        Cc[i] = vec3(enc(col.x), enc(col.y), enc(col.z));
        u32 mat = v.mat & 0xff;
        if (mat == MAT_EYE) Cc[i] = Cc[i] * 1.0f;
    }
    float scale = size * 0.45f / Max(extent, 1e-3f);
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        u32 ia = m.indices[t], ib = m.indices[t + 1], ic = m.indices[t + 2];
        vec3 wa = P[ia], wb = P[ib], wc = P[ic];
        vec3 fn = cross(wb - wa, wc - wa);
        if (dot(fn, fwd) >= 0.f) continue;   // back face (camera looks along fwd)
        auto proj = [&](vec3 p, float& sx, float& sy, float& sz) {
            vec3 d = p - center;
            sx = x0 + size * 0.5f + dot(d, right) * scale;
            sy = y0 + size * 0.5f - dot(d, up) * scale;
            sz = dot(d, fwd);
        };
        float ax, ay, az, bx, by, bz, cx, cy, cz;
        proj(wa, ax, ay, az);
        proj(wb, bx, by, bz);
        proj(wc, cx, cy, cz);
        int minX = Max(x0, (int)floorf(Min(ax, Min(bx, cx)))), maxX = Min(x0 + size - 1, (int)ceilf(Max(ax, Max(bx, cx))));
        int minY = Max(y0, (int)floorf(Min(ay, Min(by, cy)))), maxY = Min(y0 + size - 1, (int)ceilf(Max(ay, Max(by, cy))));
        float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (fabsf(area) < 1e-8f) continue;
        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
                float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float z = w0 * az + w1 * bz + w2 * cz;
                size_t idx = (size_t)y * img.w + x;
                if (z >= img.z[idx]) continue;
                img.z[idx] = z;
                vec3 n = normalize(N[ia] * w0 + N[ib] * w1 + N[ic] * w2);
                vec3 col = Cc[ia] * w0 + Cc[ib] * w1 + Cc[ic] * w2;
                float lam = Max(0.f, dot(n, L));
                float rim = powf(1.f - Max(0.f, -dot(n, fwd)), 3.f) * 0.15f;
                img.c[idx] = col * (0.38f + 0.62f * lam) + vec3(rim);
            }
    }
}

static void savePPM(const Image& img, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", img.w, img.h);
    for (const vec3& c : img.c) {
        unsigned char px[3] = {(unsigned char)(Saturate(c.x) * 255.f), (unsigned char)(Saturate(c.y) * 255.f), (unsigned char)(Saturate(c.z) * 255.f)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void poseFor(const ModelData& md, const char* mode, float t, Pose& P, mat4* skin, Frames& F) {
    const SpeciesInfo& si = speciesInfo(md.species);
    bool fly = !strcmp(mode, "fly"), walk = !strcmp(mode, "walk"), run = !strcmp(mode, "run"), sit = !strcmp(mode, "sit");
    switch (si.plan) {
        case PLAN_BIRD: {
            BirdAnim a;
            a.t = t;
            if (fly) {
                a.flap = t * 9.f;
                a.flapAmp = 1.f;
                a.fold = 0.f;
                a.legs = 0.f;
                a.neck = (md.species == SP_HERON || md.species == SP_PELICAN || md.species == SP_EGRET) ? -1.f : 0.f;
                a.soar = md.species == SP_VULTURE ? 1.f : 0.f;
            } else {
                a.fold = 1.f;
                a.legs = 1.f;
                a.walk = t * 8.f;
                a.walkAmt = walk || run ? 1.f : 0.f;
                a.sit = sit ? 1.f : 0.f;
            }
            animateBird(md, a, P);
            break;
        }
        case PLAN_QUAD: {
            QuadAnim a;
            a.t = t;
            a.speed = run ? 7.f : (walk ? 1.3f : 0.f);
            a.gait = run ? 3.f : 0.f;
            a.phase = t * quadCycleRate(md, a.speed, a.gait);
            a.sit = sit ? 1.f : 0.f;
            a.tailWag = 0.5f;
            animateQuad(md, a, P);
            break;
        }
        case PLAN_REPTILE: {
            ReptileAnim a;
            a.t = t;
            a.speed = walk || run ? 0.8f : 0.f;
            a.phase = t * reptileCycleRate(md, a.speed);
            a.lift = walk || run ? 1.f : 0.2f;
            a.swim = fly ? 1.f : 0.f;
            a.swimPhase = t * 6.f;
            a.jaw = sit ? 0.8f : 0.f;
            animateReptile(md, a, P);
            break;
        }
        default: {
            SwimAnim a;
            a.t = t;
            a.phase = t * 6.f;
            a.amp = 1.f;
            animateSwimmer(md, a, P);
            break;
        }
    }
    poseSkeleton(md.skel, P, skin, &F);
    if (md.species == SP_DOG) leashMatrices(md, F, md.collar + vec3(0.4f, 1.0f, 0.4f), 1.8f, false, skin);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp/fauna.ppm";
    const char* mode = argc > 2 ? argv[2] : "stand";
    std::vector<ModelData> models;
    double t0 = TimeSeconds();
    for (int sp = 0; sp < SP_COUNT; sp++)
        for (int v = 0; v < speciesInfo(sp).variants; v++) {
            models.emplace_back();
            double a = TimeSeconds();
            buildModel(sp, v, models.back());
            const ModelData& md = models.back();
            printf("%-20s v%d  bones %2d  lod0 %5zu v %5zu t  lod1 %5zu v  batch %5zu/%5zu v (cap %d x %d)  %.1f ms\n", speciesInfo(sp).name, v, md.skel.n,
                   md.lod[0].verts.size(), md.lod[0].indices.size() / 3, md.lod[1].verts.size(), md.batch[0].verts.size(), md.batch[1].verts.size(),
                   md.batchCap, md.batchN, (TimeSeconds() - a) * 1000.0);
        }
    printf("built %zu models in %.2f s\n", models.size(), TimeSeconds() - t0);
    // ---- validation
    for (const ModelData& md : models) {
        char name[64];
        snprintf(name, sizeof name, "%s/%d", speciesInfo(md.species).name, md.variant);
        EXPECT(md.skel.n > 0 && md.skel.n <= kMaxBones, "%s: bone count %d", name, md.skel.n);
        for (int b = 1; b < md.skel.n; b++) EXPECT(md.skel.parent[b] < b, "%s: parent order", name);
        if (!md.lod[0].verts.empty()) validateMesh(name, md.lod[0], md.skel.n);
        if (!md.lod[1].verts.empty()) validateMesh(name, md.lod[1], md.skel.n);
        if (!md.batch[0].verts.empty()) {
            validateMesh(name, md.batch[0], md.batchN * md.batchCap, 0.9f);
            validateMesh(name, md.batch[1], md.batchN * md.batchCap, 0.9f);
            EXPECT(md.batchN * md.batchCap <= 256, "%s: batch bones", name);
        }
    }
    // ---- animation: finite matrices over time; feet planted in stance
    mat4 skin[kMaxBones];
    Frames F;
    for (const ModelData& md : models) {
        const char* modes[] = {"stand", "walk", "run", "fly", "sit"};
        for (const char* mo : modes)
            for (int k = 0; k < 40; k++) {
                Pose P;
                poseFor(md, mo, k * 0.037f, P, skin, F);
                bool ok = true;
                for (int b = 0; b < md.skel.n; b++)
                    for (int c = 0; c < 4; c++)
                        if (!std::isfinite(skin[b].c[c].x) || !std::isfinite(skin[b].c[c].y) || !std::isfinite(skin[b].c[c].z)) ok = false;
                EXPECT(ok, "%s: non-finite pose (%s)", speciesInfo(md.species).name, mo);
                if (!ok) break;
            }
        if (speciesInfo(md.species).plan == PLAN_QUAD) {
            // standing: every toe on the ground
            Pose P;
            poseFor(md, "stand", 0.f, P, skin, F);
            for (int l = 0; l < 4; l++) {
                vec3 toe = F.p[md.legBone[l][2]] + rotate(F.r[md.legBone[l][2]], md.legEnd[l]);
                EXPECT(fabsf(toe.z) < 0.02f * md.legLen + 0.005f, "%s v%d: standing toe %d at z %.3f", speciesInfo(md.species).name, md.variant, l, toe.z);
            }
            // walking: the lowest foot touches the ground at every sampled time
            for (int k = 0; k < 20; k++) {
                poseFor(md, "walk", k * 0.05f, P, skin, F);
                float lo = 1e9f;
                for (int l = 0; l < 4; l++) lo = Min(lo, (F.p[md.legBone[l][2]] + rotate(F.r[md.legBone[l][2]], md.legEnd[l])).z);
                EXPECT(fabsf(lo) < 0.05f * md.legLen + 0.01f, "%s: walking lowest foot %.3f", speciesInfo(md.species).name, lo);
            }
        }
        if (md.species == SP_DOG) {
            Pose P;
            poseFor(md, "stand", 0.f, P, skin, F);
            // the last leash segment must end at the hand
            vec3 hand = md.collar + vec3(0.4f, 1.0f, 0.4f);
            vec3 q = md.skel.bind[QuadBone::LEASH0 + kLeashSegments - 1] + normalize(vec3(0, 0.35f, 1.f)) * 0.3f;
            vec3 end = transformPoint(skin[QuadBone::LEASH0 + kLeashSegments - 1], q);
            EXPECT(length(end - hand) < 0.03f, "dog leash end %.3f m from the hand", length(end - hand));
        }
    }
    // ---- boids: a flock stays together without collapsing
    {
        const int n = 40;
        vec3 pos[n], vel[n];
        Rng rng(7);
        for (int i = 0; i < n; i++) {
            pos[i] = vec3(rng.range(-20, 20), rng.range(-20, 20), rng.range(10, 30));
            vel[i] = vec3(rng.range(-3, 3), rng.range(-3, 3), 0.f);
        }
        BoidParams bp;
        bp.sepDist = 2.5f;
        bp.viewDist = 14.f;
        float dt = 1.f / 30.f;
        for (int step = 0; step < 900; step++) {
            vec3 acc[n];
            for (int i = 0; i < n; i++) {
                acc[i] = boidSteer(pos, vel, n, i, bp) * 2.f;
                vec3 toC = vec3(0, 0, 20) - pos[i];
                acc[i] += toC * 0.02f;
                float sp = length(vel[i]);
                acc[i] += (vel[i] / Max(sp, 0.1f)) * (9.f - sp) * 0.8f;
            }
            for (int i = 0; i < n; i++) {
                vel[i] += acc[i] * dt;
                pos[i] += vel[i] * dt;
            }
        }
        float minD = 1e9f, maxR = 0.f;
        vec3 c(0.f);
        for (int i = 0; i < n; i++) c += pos[i] / (float)n;
        for (int i = 0; i < n; i++) {
            EXPECT(finite3(pos[i]), "boid %d non-finite", i);
            maxR = Max(maxR, length(pos[i] - c));
            for (int j = i + 1; j < n; j++) minD = Min(minD, length(pos[i] - pos[j]));
        }
        printf("boids: min separation %.2f m, flock radius %.1f m\n", minD, maxR);
        EXPECT(minD > 0.5f, "boids collapse: min separation %.2f", minD);
        EXPECT(maxR < 60.f, "boids scatter: radius %.1f", maxR);
    }
    // ---- contact sheet (optionally only the species whose name contains argv[3], in cells of argv[4] pixels)
    {
        const char* only = argc > 3 ? argv[3] : nullptr;
        std::vector<const ModelData*> sheet;
        for (const ModelData& md : models)
            if (!only || strstr(speciesInfo(md.species).name, only)) sheet.push_back(&md);
        int cell = argc > 4 ? atoi(argv[4]) : 220;
        int cols = Min(7, Max((int)sheet.size(), 1));
        int rows = ((int)sheet.size() + cols - 1) / cols;
        Image img(cols * cell, Max(rows, 1) * cell * 2);
        for (size_t i = 0; i < sheet.size(); i++) {
            const ModelData& md = *sheet[i];
            Pose P;
            poseFor(md, mode, 0.3f, P, skin, F);
            const SkinnedMeshData& mesh = md.lod[0].verts.empty() ? md.batch[0] : md.lod[0];
            mat4 bskin[256];
            const mat4* use = skin;
            if (md.lod[0].verts.empty()) {   // batched model: show the first slot only
                for (int k = 0; k < 256; k++) bskin[k] = mat4(vec4(0.f), vec4(0.f), vec4(0.f), vec4(0.f, 0.f, -100.f, 1.f));
                for (int j = 0; j < md.batchN; j++) bskin[j] = skin[md.batchBones[j]];
                use = bskin;
            }
            AABB bb = md.lod[0].verts.empty() ? AABB(vec3(-0.3f), vec3(0.3f)) : md.lod[0].bounds;
            vec3 c = bb.center();
            float ext = Max(maxc(bb.extent()), 0.05f);
            if (md.species == SP_DOG) {
                c = vec3(0, 0, speciesInfo(md.species).height * 0.5f);
                ext = 0.8f;
            }
            int x = (int)(i % cols) * cell, y = (int)(i / cols) * cell * 2;
            drawModel(img, x, y, cell, mesh, use, 215.f, -12.f, c, ext);
            drawModel(img, x, y + cell, cell, mesh, use, 90.f, -5.f, c, ext);
        }
        savePPM(img, out);
        printf("wrote %s\n", out);
    }
    printf(g_fail ? "%d FAILURES\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
