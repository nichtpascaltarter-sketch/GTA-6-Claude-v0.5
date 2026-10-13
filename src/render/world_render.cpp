// Streaming of world cells (background generation, GPU upload, LOD) and their rendering.
// Included from renderer.cpp.
#include "../world/buildings.h"
#include <unordered_map>

namespace World {
struct CellGeometry;
void generateCell(int cx, int cy, bool detail, CellGeometry& out);
const std::vector<int>& siteFarCells(float* range);  // world/sitecell.cpp: skyline landmark cells
}

namespace Render {

struct StreamCell {
    int cx = 0, cy = 0, lod = 0;
    std::atomic<int> state{0};  // 0 queued, 1 generated (cpu ready), 2 uploaded, 3 empty
    World::CellGeometry* geo = nullptr;
    gfx::Buffer vb, ib;
    u32 opaqueCount = 0, decalStart = 0, decalCount = 0;
    AABB bounds;  // cell-relative
    double lastUsed = 0;
    std::vector<World::LightInstance> lights;
    std::vector<vec2> lightArm;   // per light: a street lamp's arm direction (towards the road it lights), 0 = unknown
    std::vector<World::PropInstance> props;
    std::vector<World::CollisionBox> collision;
};

struct WorldRenderer {
    std::unordered_map<int, StreamCell*> cells;
    gfx::VertexShader vs, vsShadow;
    gfx::PixelShader  ps = nullptr;
    gfx::RasterState  decalRS = nullptr;
    MaterialLibrary* mats = nullptr;
    gfx::Buffer facadeBuf;
    gfx::Buffer facadeLightBuf;   // per facade: night architectural lighting (see uploadFacades)
    u32 nearUploads = 0;          // LOD0 cells uploaded so far (overhead map refresh when the near world changes)
    gfx::Texture signTex;
    float nearRadius = 420.f;
    float farRadius = 2300.f;
    int maxInFlight = 6;
    std::atomic<int> inFlight{0};
    int uploadsPerFrame = 4;
    int drawnCells = 0;

    static int key(int cx, int cy, int lod) { return (lod * World::kCellsPerSide + cy) * World::kCellsPerSide + cx; }

    void init(MaterialLibrary* m) {
        mats = m;
        gfx::InputElement layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, gfx::PER_VERTEX, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, gfx::PER_VERTEX, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, gfx::PER_VERTEX, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, gfx::PER_VERTEX, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, gfx::PER_VERTEX, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, gfx::PER_VERTEX, 0},
        };
        vs = gfx::loadVS("world.hlsl", "vsWorld", layout, 6);
        vsShadow = gfx::loadVS("world.hlsl", "vsWorldShadow", layout, 6);
        ps = gfx::loadPS("world.hlsl", "psWorld");
        gfx::RasterDesc rs;
        rs.cull = gfx::CULL_BACK;
        rs.depthBias = 8;
        rs.slopeBias = 2.f;
        decalRS = gfx::createRasterState(rs);
        // The facade tables and the sign atlas are read through the bindless arrays every frame: until a building
        // set arrives (Renderer::setWorld) they hold one blank facade and a blank sign
        uploadFacades(World::BuildingSet());
        u32 blank = 0;
        signTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &blank, 4);
    }

    // world.hlsl / overhead.hlsl: root constants 0..2 = the cell's origin relative to the view's reference point
    static void setCellOffset(vec3 off) {
        float o[3] = {off.x, off.y, off.z};
        gfx::ctx->setRootConstants(false, 0, 3, o);
    }

    // Resources the world shaders read through the bindless arrays (facade.hlsli): their heap indices for the frame
    // constants, and the transitions the slot model would otherwise make
    void bindlessFrame(u32 idx[4]) {
        idx[0] = gfx::bindlessIndex(facadeBuf.srv);
        idx[1] = gfx::bindlessIndex(signTex.srv);
        idx[2] = gfx::bindlessIndex(facadeLightBuf.srv);
        idx[3] = 0;
        gfx::ctx->prepareBindlessRead(facadeBuf.buf);
        gfx::ctx->prepareBindlessRead(signTex.res);
        gfx::ctx->prepareBindlessRead(facadeLightBuf.buf);
    }

    void uploadFacades(const World::BuildingSet& bs) {
        std::vector<World::FacadeGPU> f = bs.facades;
        for (auto& r : f) {
            int matId = (int)r.wallLayer;
            r.wallLayer = mats->infos[Clamp(matId, 0, (int)MAT_COUNT - 1)].layer;
        }
        if (f.empty()) f.push_back(World::FacadeGPU());
        facadeBuf.release();
        facadeBuf = gfx::createBuffer((u32)(f.size() * sizeof(World::FacadeGPU)), sizeof(World::FacadeGPU), gfx::BUF_STRUCTURED, f.data());
        // Night architectural lighting per facade: x facade top (m above the base), y crown (0 none, 1 wash,
        // 2 wash + LED lines on the wall edges), z palette index, w warm uplights along the base
        std::vector<vec4> fl(f.size(), vec4(0.f));
        for (const World::Building& b : bs.buildings) {
            if (b.facade >= fl.size()) continue;
            vec4& e = fl[b.facade];
            e.x = Max(e.x, b.height);
            u32 h = (b.seed ^ 0x9E3779B9u) * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            float r0 = (float)(h & 0xffffu) / 65535.f, r1 = (float)(h >> 16) / 65535.f;
            float crownP = 0.f, floodP = 0.f;
            switch ((World::BuildingStyle)b.style) {
                case World::BS_TOWER: crownP = 0.45f; floodP = 0.3f; break;
                case World::BS_DECO: crownP = 0.55f; floodP = 0.8f; break;
                case World::BS_MIDRISE: crownP = 0.15f; floodP = 0.3f; break;
                case World::BS_CONDO: crownP = 0.25f; floodP = 0.2f; break;
                case World::BS_CHURCH: floodP = 0.9f; break;
                default: break;
            }
            if (r0 < crownP) e.y = (b.style == World::BS_DECO || r1 > 0.55f) ? 2.f : 1.f;
            e.z = floorf(r1 * 7.99f);
            if (fmodf(r0 * 7.13f + r1 * 3.71f, 1.f) < floodP) e.w = 1.f;
        }
        facadeLightBuf.release();
        facadeLightBuf = gfx::createBuffer((u32)(fl.size() * sizeof(vec4)), sizeof(vec4), gfx::BUF_STRUCTURED, fl.data());
    }

    void requestCell(int cx, int cy, int lod, double now) {
        int k = key(cx, cy, lod);
        auto it = cells.find(k);
        if (it != cells.end()) {
            it->second->lastUsed = now;
            return;
        }
        if (inFlight.load() >= maxInFlight) return;
        StreamCell* c = new StreamCell();
        c->cx = cx;
        c->cy = cy;
        c->lod = lod;
        c->lastUsed = now;
        cells[k] = c;
        inFlight++;
        Jobs::submit([this, c] {
            c->geo = new World::CellGeometry();
            World::generateCell(c->cx, c->cy, c->lod == 0, *c->geo);
            c->state.store(1);
            inFlight--;
        }, kJobLow);
    }

    // Light spilling out of shop windows at night: a wide warm light every ~8 m along the street front of every
    // building with a storefront ground floor (type 5: night only, follows shop hours, see gatherLights). It sits
    // low (2.15 m) and close to the glass (0.45 m out), aimed out and down, so awnings / canopies above the
    // shopfront (which project 1.3-1.8 m from ~3.3 m) stay outside its cone and never come near the source.
    void addStorefrontLights(StreamCell* c) {
        const World::BuildingSet* bs = World::gBuildings;
        const int cps = World::kCellsPerSide;
        if (!bs || c->cx < 0 || c->cy < 0 || c->cx >= cps || c->cy >= cps) return;
        size_t ci = (size_t)c->cy * cps + c->cx;
        if (ci >= bs->cellLists.size()) return;
        for (int bi : bs->cellLists[ci]) {
            const World::Building& b = bs->buildings[(size_t)bi];
            if (b.facade >= bs->facades.size() || !(bs->facades[b.facade].flags & 1u)) continue;
            float len = 2.f * b.hx;
            int n = Max(1, (int)floorf(len / 8.f));
            for (int k = 0; k < n; k++) {
                float u = -b.hx + (k + 0.5f) * len / n;
                vec2 p = b.c + b.ax * u + b.front * (b.hy + 0.45f);
                World::LightInstance li;
                li.pos = vec3(p, b.baseZ + 2.15f);
                li.color = vec3(1.f, 0.83f, 0.64f) * 170.f;
                li.radius = 8.f;
                li.dir = normalize(vec3(b.front * 0.8f, -0.6f));
                li.cone = 0.2f;   // cos(outer) = 0.2: ~78 degrees around the out-and-down axis
                li.type = 5;
                c->lights.push_back(li);
            }
        }
    }

    // Street lamps hang off a pole on an arm over the road: the direction from the pole to the lamp tells the road
    // side, which gets most of the light (gatherLights: road-lighting distribution, lights.hlsli streetLampPattern)
    static void computeLampArms(StreamCell* c) {
        c->lightArm.assign(c->lights.size(), vec2(0.f));
        for (size_t i = 0; i < c->lights.size(); i++) {
            const World::LightInstance& li = c->lights[i];
            if (li.type != 0) continue;
            float best = 16.f;
            for (const World::PropInstance& pi : c->props) {
                if (pi.type != World::PROP_STREETLIGHT && pi.type != World::PROP_STREETLIGHT_DOUBLE && pi.type != World::PROP_POWER_POLE) continue;
                vec2 d = li.pos.xy() - pi.pos.xy();
                float dz = li.pos.z - pi.pos.z, d2 = dot(d, d);
                if (d2 < best && d2 > 0.25f && dz > 4.f && dz < 16.f) {
                    best = d2;
                    c->lightArm[i] = d * (1.f / sqrtf(d2));
                }
            }
        }
    }

    // Street lamp colours: installations change block to block (a 160 m grid): high-pressure sodium (~2050 K,
    // orange), warm-white LED (3000 K), neutral (4000 K) and cool-white (5000 K) LED, at the world's luminance.
    // props.hlsl picks the same colour for the lamp's lens from its position (lampColorAt).
    static vec3 streetLampColor(vec3 pos, vec3 worldColor) {
        u32 h = (u32)(int)floorf(pos.x / 160.f) * 73856093u ^ (u32)(int)floorf(pos.y / 160.f) * 19349663u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        float r = (float)(h & 1023u) / 1023.f;
        vec3 k = r < 0.38f ? vec3(1.f, 0.46f, 0.11f) : (r < 0.62f ? vec3(1.f, 0.61f, 0.31f) : (r < 0.86f ? vec3(1.f, 0.76f, 0.52f) : vec3(1.f, 0.87f, 0.74f)));
        float lumW = 0.2126f * worldColor.x + 0.7152f * worldColor.y + 0.0722f * worldColor.z;
        return k * (lumW / (0.2126f * k.x + 0.7152f * k.y + 0.0722f * k.z));
    }

    void upload(StreamCell* c) {
        World::CellGeometry* g = c->geo;
        u32 nOpaque = (u32)g->opaque.indices.size(), nDecal = (u32)g->decals.indices.size();
        if (nOpaque + nDecal == 0) {
            c->state.store(3);
        } else {
            std::vector<VtxStatic> verts = g->opaque.verts;
            std::vector<u32> idx = g->opaque.indices;
            u32 base = (u32)verts.size();
            verts.insert(verts.end(), g->decals.verts.begin(), g->decals.verts.end());
            for (u32 i : g->decals.indices) idx.push_back(base + i);
            c->vb = gfx::createBuffer((u32)(verts.size() * sizeof(VtxStatic)), sizeof(VtxStatic), gfx::BUF_VERTEX, verts.data());
            c->ib = gfx::createBuffer((u32)(idx.size() * 4), 4, gfx::BUF_INDEX, idx.data());
            c->opaqueCount = nOpaque;
            c->decalStart = nOpaque;
            c->decalCount = nDecal;
            c->bounds = g->opaque.bounds;
            if (g->decals.bounds.valid()) c->bounds.add(g->decals.bounds);
            c->state.store(2);
        }
        c->lights = std::move(g->lights);
        if (c->lod == 0) {
            addStorefrontLights(c);
            nearUploads++;
        }
        c->props = std::move(g->props);
        if (c->lod == 0) computeLampArms(c);
        c->collision = std::move(g->collision);
        if (c->lod == 0 && Phys::gCollision) Phys::gCollision->addCell(key(c->cx, c->cy, 0), c->collision, c->props);
        delete g;
        c->geo = nullptr;
    }

    void update(dvec3 cam, double now) {
        // Desired cells: near (LOD0) and far (LOD1) rings; nearest first
        int cps = World::kCellsPerSide;
        float cs = World::kCellSize;
        int ccx = (int)floor((cam.x + World::kWorldHalf) / cs), ccy = (int)floor((cam.y + World::kWorldHalf) / cs);
        int rFar = (int)ceilf(farRadius / cs) + 1;
        struct Req { float d; int cx, cy, lod; };
        std::vector<Req> reqs;
        for (int dy = -rFar; dy <= rFar; dy++)
            for (int dx = -rFar; dx <= rFar; dx++) {
                int cx = ccx + dx, cy = ccy + dy;
                if (cx < 0 || cy < 0 || cx >= cps || cy >= cps) continue;
                vec2 o = World::cellOrigin(cx, cy);
                // distance from camera to the cell rectangle
                float qx = Max(Max(o.x - (float)cam.x, 0.f), (float)cam.x - (o.x + cs));
                float qy = Max(Max(o.y - (float)cam.y, 0.f), (float)cam.y - (o.y + cs));
                float d = sqrtf(qx * qx + qy * qy);
                if (d < nearRadius) reqs.push_back({d, cx, cy, 0});
                else if (d < farRadius) reqs.push_back({d, cx, cy, 1});
                // keep far version alive under near cells briefly for seamless transitions
                if (d < nearRadius + 60.f && d >= nearRadius - 60.f) reqs.push_back({d + 1.f, cx, cy, 1});
            }
        // Skyline landmarks (Solaris One, cranes, masts, stadium...) keep their far LOD beyond the far ring
        {
            float landmarkRange = 0.f;
            for (int ci : World::siteFarCells(&landmarkRange)) {
                int cx = ci % cps, cy = ci / cps;
                vec2 o = World::cellOrigin(cx, cy);
                float qx = Max(Max(o.x - (float)cam.x, 0.f), (float)cam.x - (o.x + cs));
                float qy = Max(Max(o.y - (float)cam.y, 0.f), (float)cam.y - (o.y + cs));
                float d = sqrtf(qx * qx + qy * qy);
                if (d >= farRadius && d < landmarkRange) reqs.push_back({d, cx, cy, 1});
            }
        }
        std::sort(reqs.begin(), reqs.end(), [](const Req& a, const Req& b) { return a.d < b.d; });
        for (auto& r : reqs) requestCell(r.cx, r.cy, r.lod, now);
        // Upload finished cells (budgeted)
        int uploads = 0;
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->state.load() == 1 && uploads < uploadsPerFrame) {
                upload(c);
                uploads++;
            }
        }
        // Evict cells not requested recently (only when not being generated)
        std::vector<int> dead;
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            int st = c->state.load();
            if (now - c->lastUsed > 1.5 && (st == 2 || st == 3)) dead.push_back(kv.first);
        }
        for (int k : dead) {
            StreamCell* c = cells[k];
            if (c->lod == 0 && Phys::gCollision) Phys::gCollision->removeCell(k);
            c->vb.release();
            c->ib.release();
            delete c;
            cells.erase(k);
        }
    }

    // Is the near (LOD0) version of this cell ready? Used to hide the far version.
    bool nearReady(int cx, int cy) const {
        auto it = cells.find(key(cx, cy, 0));
        return it != cells.end() && (it->second->state.load() >= 2);
    }

    template <typename F>
    void forVisible(const Frustum& fr, dvec3 cam, bool decalsPass, F&& fn) {
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->state.load() != 2) continue;
            if (c->lod == 1 && nearReady(c->cx, c->cy)) continue;
            if (c->lod == 0 && decalsPass && c->decalCount == 0) continue;
            vec2 o = World::cellOrigin(c->cx, c->cy);
            vec3 off((float)(o.x - cam.x), (float)(o.y - cam.y), (float)(-cam.z));
            AABB b(c->bounds.mn + off, c->bounds.mx + off);
            if (!fr.testAABB(b)) continue;
            fn(c, off);
        }
    }

    void drawGBuffer(Renderer& r) { drawGBufferVP(r, r.viewProjNoJitter, r.camera.pos, true, true); }

    // G-buffer pass for an arbitrary view: vp and all cell offsets are relative to refPos.
    void drawGBufferVP(Renderer& r, const mat4& vp, dvec3 refPos, bool withDecals, bool mainView = false) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(vp);
        c->setInputLayout(vs.layout);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        c->setVS(vs.vs);
        c->setPS(ps);
        if (mainView) drawnCells = 0;
        forVisible(fr, refPos, false, [&](StreamCell* sc, vec3 off) {
            setCellOffset(off);
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->setVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->setIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            if (sc->opaqueCount) c->drawIndexed(sc->opaqueCount, 0, 0);
            r.stats.drawCalls++;
            if (mainView) {
                r.stats.triangles += sc->opaqueCount / 3;
                drawnCells++;
            }
        });
        // Decals (road paint) with depth bias
        c->setRasterState(decalRS);
        if (withDecals) forVisible(fr, refPos, true, [&](StreamCell* sc, vec3 off) {
            if (!sc->decalCount) return;
            setCellOffset(off);
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->setVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->setIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            c->drawIndexed(sc->decalCount, sc->decalStart, 0);
            r.stats.drawCalls++;
        });
        c->setRasterState(gfx::states.cullBack);
    }

    void drawShadow(Renderer& r, const mat4& lightVP, int cascade) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(lightVP);
        c->setInputLayout(vsShadow.layout);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        c->setVS(vsShadow.vs);
        c->setPS(nullptr);
        forVisible(fr, r.camera.pos, false, [&](StreamCell* sc, vec3 off) {
            // near cascades only need near cells
            if (cascade <= 1 && sc->lod == 1) return;
            setCellOffset(off);
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->setVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->setIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            if (sc->opaqueCount) c->drawIndexed(sc->opaqueCount, 0, 0);
            r.stats.drawCalls++;
        });
    }

    // Collect active local lights from near cells (camera-relative)
    void gatherLights(dvec3 cam, float night, float time, float hour, const Frustum& fr, std::vector<LightGPU>& out, int maxLights) {
        const float hc = hour < 12.f ? hour + 24.f : hour;   // evening-continuous clock (12..36)
        const float range = 380.f;
        bool anyBroken = Phys::gCollision && Phys::gCollision->brokenCount() > 0;
        std::vector<vec3> brokenPoles;
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->lod != 0 || c->state.load() != 2) continue;
            vec2 o = World::cellOrigin(c->cx, c->cy);
            float qx = Max(Max(o.x - (float)cam.x, 0.f), (float)cam.x - (o.x + World::kCellSize));
            float qy = Max(Max(o.y - (float)cam.y, 0.f), (float)cam.y - (o.y + World::kCellSize));
            if (qx * qx + qy * qy > range * range) continue;
            // Street lamps knocked down by gameplay switch off (lamp heads sit <= 3.6 m from the pole, 5-13 m up)
            brokenPoles.clear();
            if (anyBroken) {
                int cellKey = key(c->cx, c->cy, 0);
                for (size_t i = 0; i < c->props.size(); i++) {
                    const World::PropInstance& pi = c->props[i];
                    if ((pi.type == World::PROP_STREETLIGHT || pi.type == World::PROP_STREETLIGHT_DOUBLE) &&
                        Phys::gCollision->isPropBroken(cellKey, (int)i))
                        brokenPoles.push_back(pi.pos);
                }
            }
            for (size_t lix = 0; lix < c->lights.size(); lix++) {
                const World::LightInstance& li = c->lights[lix];
                if (!brokenPoles.empty() && li.type == 0) {
                    bool off = false;
                    for (const vec3& p : brokenPoles) {
                        vec2 d = li.pos.xy() - p.xy();
                        float dz = li.pos.z - p.z;
                        if (dot(d, d) < 3.6f * 3.6f && dz > 5.f && dz < 13.f) { off = true; break; }
                    }
                    if (off) continue;
                }
                float k = 0.f;
                switch (li.type) {
                    case 0: k = SmoothStep(0.05f, 0.35f, night); break;           // street
                    case 1: k = SmoothStep(0.2f, 0.6f, night); break;             // building / canopy
                    case 2: k = 0.25f + 0.75f * night; break;                     // neon
                    case 4: k = night * (fmodf(time + li.pos.x * 0.01f, 1.6f) < 0.25f ? 1.f : 0.f); break;
                    case 5: {   // shop window spill: shops close between 22:00 and 01:00, some keep display lighting on
                        u32 h = (u32)(int)floorf(li.pos.x * 2.f) * 73856093u ^ (u32)(int)floorf(li.pos.y * 2.f) * 19349663u;
                        h ^= h >> 15;
                        h *= 0x2c1b3c6du;
                        h ^= h >> 12;
                        bool open = hc < 22.f + 3.f * (float)(h & 1023u) / 1023.f;
                        bool display = ((h >> 10) & 1023u) < 410u;
                        k = (open || display) ? SmoothStep(0.15f, 0.5f, night) : 0.f;
                        break;
                    }
                    default: k = night; break;
                }
                if (k <= 0.01f) continue;
                vec3 rp = rel(li.pos, cam);
                if (length2(rp) > range * range) continue;
                if (!fr.testSphere(rp, li.radius)) continue;
                LightGPU g;
                g.pos = rp;
                g.radius = li.radius;
                g.color = (li.type == 0 ? streetLampColor(li.pos, li.color) : li.color) * k;
                vec2 arm = li.type == 0 && lix < c->lightArm.size() ? c->lightArm[lix] : vec2(0.f);
                if (length2(arm) > 0.5f) {
                    // road lighting: full cut-off at 80 degrees from the nadir, peak towards the road
                    g.dir = normalize(vec3(arm.x * 0.3f, arm.y * 0.3f, -1.f));
                    g.spotCos = 0.17f;
                    g.spotInner = 3.f;
                } else if (li.cone > 0.f && length2(li.dir) > 0.5f) {
                    // cone stores cos of the outer angle as (1 - cone) for wide down-facing street lamps
                    g.spotCos = Clamp(1.f - li.cone * 4.f, -0.95f, 0.99f);
                    g.spotInner = Min(0.999f, g.spotCos + 0.25f);
                    g.dir = li.dir;
                } else {
                    g.spotCos = -2.f;
                    g.spotInner = -1.f;
                    g.dir = vec3(0, 0, -1);
                }
                out.push_back(g);
                if ((int)out.size() >= maxLights) return;
            }
        }
    }

    int pendingCount() const {
        int n = 0;
        for (auto& kv : cells) n += kv.second->state.load() < 2;
        return n;
    }
};

}  // namespace Render
