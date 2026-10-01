// Instanced prop / vegetation rendering, GPU-driven. Every prop of the near cells sits in a persistent instance buffer,
// rebuilt when the set of near cells (or of props knocked down by gameplay) changes; the decor plants around the camera
// (vegdecor.cpp) are scattered into a second one by a compute pass each frame. One compute pass then culls both for the
// camera and the shadow cascades (distance per prototype, frustum) into per-prototype instance lists and writes
// compacted draw arguments (shaders/propcull.hlsl), and each pass draws every prototype with a single ExecuteIndirect.
// Traffic signals, whose lamps follow the AI's phases every frame, keep a small CPU-instanced path. Included from
// renderer.cpp.
namespace World {
struct PropPrototype;
void buildPropPrototype(PropType type, int variant, PropPrototype& p);
}

namespace Render {

static_assert(MAT_LEAVES == 32 && MAT_PALM_FROND == 34, "props.hlsl hardcodes foliage material ids");

struct PropInstanceGPU {
    vec4 pos;  // camera-relative xyz, scale
    vec4 rot;  // cos, sin, wind phase, flags
};

// propcull.hlsl
struct PropSourceGPU {
    vec4 posScale;  // world position, scale
    vec4 rot;       // cos(yaw), sin(yaw), wind phase, prototype index (as uint bits)
};
struct PropProtoGPU {
    float radius, lodDistance;
    u32 shadow, indexCount, indexStart;
    int baseVertex;
    u32 segBase, segSize;
};
static_assert(sizeof(PropSourceGPU) == 32 && sizeof(PropProtoGPU) == 32, "must match propcull.hlsl");

struct PropCullCBData {
    vec4 camHi, camLo;
    vec4 planes[24];   // 4 views x 6 planes
    vec4 view[4];      // x distance scale, y culled this frame, z shadow view
    u32 protoCount, slotsPerView, instanceCount, decorCapacity;
    vec4 decor0;       // decor grid: xy origin (world), z cell size, w cells per side
    vec4 decor1;       // x radius, y first decor prototype, z world half size, w on
};

struct FoliageCBData {
    u32 layer, size, pad0, pad1;
};

// What the decor placement reads (Renderer::render passes it: the terrain splat and the overhead map's classes)
struct DecorInputs {
    gfx::SRV splat0 = nullptr, splat1 = nullptr, overheadClass = nullptr;
    bool ready = false;   // terrain and overhead map available
};

struct PropRenderer {
    struct Proto {
        u32 indexStart = 0, indexCount = 0;
        int baseVertex = 0;
        float radius = 1.f, lodDistance = 100.f;
        bool shadow = true, foliage = false;
    };
    std::vector<Proto> protos;
    u32 worldProtos = 0;   // the world's prop prototypes come first, then the decor plants (vegdecor.cpp)
    int protoIndex[World::PROP_COUNT][8];
    int variantCount[World::PROP_COUNT];
    gfx::Buffer vb, ib, instBuf;
    static const int kMaxInstances = 65536;   // CPU path (traffic signals) per pass
    std::vector<std::vector<PropInstanceGPU>> buckets;
    std::vector<PropInstanceGPU> flat;
    gfx::VertexShader vs, vsShadow;
    gfx::PixelShader ps = nullptr, psShadow = nullptr;
    gfx::Texture foliageArr;
    int drawnInstances = 0;   // camera view, as of a couple of frames ago (GPU path) plus this frame's signals

    // GPU path. Views: 0 the camera, 1-3 shadow cascades 0-2 (props cast into the first three cascades only).
    static const u32 kViews = 4;
    gfx::ComputeShader csCull = nullptr, csArgs = nullptr, csDecor = nullptr;
    gfx::CommandSignature drawSig = nullptr;
    gfx::CBuffer<PropCullCBData> cullCB;
    gfx::Buffer sources, protoTable, lists, counts, args, drawCount, decor, decorCount;
    static const u32 kDecorCapacity = 6144;   // decor plants per frame (camera ring)
    gfx::Buffer statsReadback[3];
    u64 statsFrame[3] = {0, 0, 0};
    u32 sourceCount = 0;                  // world props in the instance buffer
    u32 slotCapacity = 0, listSlots = 0;  // list slots per view (all prototypes' segments), as allocated / in use
    std::vector<std::pair<int, StreamCell*>> sourceCells, cellScratch;
    size_t sourceBroken = 0;
    std::vector<PropSourceGPU> sourceCpu;
    std::vector<PropProtoGPU> protoCpu;
    bool culled = false;   // this frame's lists are valid (the draw passes skip the GPU path otherwise)

    static bool isSignal(int type) { return type == World::PROP_TRAFFIC_LIGHT || type == World::PROP_SIGNAL_SPAN; }

    void init(MaterialLibrary* mats) {
        (void)mats;
        std::vector<VtxStatic> verts;
        std::vector<u32> idx;
        for (int t = 0; t < World::PROP_COUNT; t++) {
            int nv = 1;
            switch (t) {
                case World::PROP_PALM: case World::PROP_TREE_OAK: case World::PROP_BUSH: nv = 4; break;
                case World::PROP_PALM_TALL: case World::PROP_TREE_PINE: case World::PROP_MANGROVE: case World::PROP_CYPRESS: case World::PROP_SAWGRASS: nv = 3; break;
                case World::PROP_STREETLIGHT: case World::PROP_TRAFFIC_LIGHT: case World::PROP_DUMPSTER: nv = 2; break;
                // street furniture variants (world/propmesh.cpp): shelter ad art, box colours, pole transformer/lamp, planter kinds
                case World::PROP_BUS_STOP: case World::PROP_NEWS_BOX: case World::PROP_POWER_POLE: nv = 4; break;
                case World::PROP_PLANTER: case World::PROP_BARRIER: case World::PROP_SIGNAL_SPAN: nv = 2; break;
                case World::PROP_STREET_TREE: nv = 3; break;
                default: nv = 1; break;
            }
            variantCount[t] = nv;
            for (int v = 0; v < 8; v++) protoIndex[t][v] = -1;
            for (int v = 0; v < nv; v++) {
                World::PropPrototype pp;
                World::buildPropPrototype((World::PropType)t, v, pp);
                Proto p;
                p.indexStart = (u32)idx.size();
                p.indexCount = (u32)pp.mesh.indices.size();
                p.baseVertex = (int)verts.size();
                p.radius = pp.radius;
                p.lodDistance = pp.lodDistance;
                p.shadow = pp.castsShadow;
                p.foliage = pp.foliage;
                verts.insert(verts.end(), pp.mesh.verts.begin(), pp.mesh.verts.end());
                idx.insert(idx.end(), pp.mesh.indices.begin(), pp.mesh.indices.end());
                protoIndex[t][v] = (int)protos.size();
                protos.push_back(p);
            }
        }
        worldProtos = (u32)protos.size();
        for (int t = 0; t < vegdecor::DECOR_TYPES; t++)
            for (int v = 0; v < vegdecor::kVariants[t]; v++) {
                vegdecor::DecorPrototype dp;
                vegdecor::build(t, v, dp);
                Proto p;
                p.indexStart = (u32)idx.size();
                p.indexCount = (u32)dp.mesh.indices.size();
                p.baseVertex = (int)verts.size();
                p.radius = dp.radius;
                p.lodDistance = dp.lodDistance;
                p.shadow = dp.shadow;
                p.foliage = true;
                verts.insert(verts.end(), dp.mesh.verts.begin(), dp.mesh.verts.end());
                idx.insert(idx.end(), dp.mesh.indices.begin(), dp.mesh.indices.end());
                protos.push_back(p);
            }
        vb = gfx::createBuffer((u32)(verts.size() * sizeof(VtxStatic)), sizeof(VtxStatic), gfx::BUF_VERTEX, verts.data());
        ib = gfx::createBuffer((u32)(idx.size() * 4), 4, gfx::BUF_INDEX, idx.data());
        instBuf = gfx::createBuffer(kMaxInstances * sizeof(PropInstanceGPU), sizeof(PropInstanceGPU), gfx::BUF_VERTEX | gfx::BUF_DYNAMIC);
        buckets.resize(protos.size());
        gfx::InputElement layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, gfx::PER_VERTEX, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, gfx::PER_VERTEX, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, gfx::PER_VERTEX, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, gfx::PER_VERTEX, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, gfx::PER_VERTEX, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, gfx::PER_VERTEX, 0},
            {"INSTPOS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, gfx::PER_INSTANCE, 1},
            {"INSTROT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, gfx::PER_INSTANCE, 1},
        };
        vs = gfx::loadVS("props.hlsl", "vsProp", layout, 8);
        vsShadow = gfx::loadVS("props.hlsl", "vsPropShadow", layout, 8);
        ps = gfx::loadPS("props.hlsl", "psProp");
        psShadow = gfx::loadPS("props.hlsl", "psPropShadow");
        // GPU-driven path
        csCull = gfx::loadCS("propcull.hlsl", "csPropCull");
        csArgs = gfx::loadCS("propcull.hlsl", "csPropArgs");
        csDecor = gfx::loadCS("propcull.hlsl", "csDecorPlace");
        drawSig = gfx::createCommandSignature(gfx::INDIRECT_DRAW_INDEXED);
        cullCB.create();
        u32 np = (u32)protos.size();
        protoCpu.resize(np);
        protoTable = gfx::createBuffer(np * sizeof(PropProtoGPU), sizeof(PropProtoGPU), gfx::BUF_STRUCTURED, nullptr, "prop prototypes");
        std::vector<u32> zeros(kViews * np * 5, 0u);
        counts = gfx::createBuffer(kViews * np * 4, 4, gfx::BUF_RAW | gfx::BUF_UAV, zeros.data(), "prop list lengths");
        args = gfx::createBuffer(kViews * np * 20, 4, gfx::BUF_RAW | gfx::BUF_UAV | gfx::BUF_INDIRECT, zeros.data(), "prop draw args");
        drawCount = gfx::createBuffer(kViews * 4, 4, gfx::BUF_RAW | gfx::BUF_UAV | gfx::BUF_INDIRECT, zeros.data(), "prop draw counts");
        for (gfx::Buffer& b : statsReadback) b = gfx::createBuffer(np * 20 + 4, 4, gfx::BUF_READBACK, nullptr, "prop draw stats");
        decor = gfx::createBuffer(kDecorCapacity * sizeof(PropSourceGPU), sizeof(PropSourceGPU), gfx::BUF_STRUCTURED | gfx::BUF_UAV, nullptr, "decor plants");
        decorCount = gfx::createBuffer(4, 4, gfx::BUF_RAW | gfx::BUF_UAV, zeros.data(), "decor plant count");
        // Foliage textures
        int fsize = Platform::hasArg("autotest") ? 256 : 512;
        foliageArr = createMaterialArray(fsize, 6, true);
        gfx::ComputeShader  cs = gfx::loadCS("foliage.hlsl", "csFoliage");
        gfx::CBuffer<FoliageCBData> fcb;
        fcb.create();
        gfx::ctx->setCS(cs);
        gfx::ctx->csSetUAVs(0, 1, &foliageArr.uav);
        for (int l = 0; l < 6; l++) {
            fcb.data.layer = (u32)l;
            fcb.data.size = (u32)fsize;
            fcb.upload();
            gfx::Resource  cbs[] = {fcb.get()};
            gfx::ctx->csSetCBs(1, 1, cbs);
            gfx::ctx->dispatch(gfx::divUp(fsize, 8), gfx::divUp(fsize, 8), 1);
        }
        gfx::unbindCSResources(1, 1);
        gfx::ctx->generateMips(foliageArr);
        fcb.release();
        LOG("Props: %zu prototypes, %zu verts", protos.size(), verts.size());
    }

    int protoFor(const World::PropInstance& p) const {
        int t = Clamp((int)p.type, 0, World::PROP_COUNT - 1);
        return protoIndex[t][p.variant % variantCount[t]];
    }

    static float windPhase(const World::PropInstance& pi) {
        return hashToFloat(hash2i((int)(pi.pos.x * 3.f), (int)(pi.pos.y * 3.f))) * kTwoPi;
    }

    // ---- GPU path

    // Rebuilds the instance buffer when the near cells (or the knocked-down props) changed: every prop but the traffic
    // signals, grouped by prototype (each prototype's list gets that many slots per view).
    template <typename CellMap>
    void syncSources(CellMap& cells) {
        cellScratch.clear();
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->lod == 0 && c->state.load() == 2) cellScratch.push_back({kv.first, c});
        }
        std::sort(cellScratch.begin(), cellScratch.end());
        size_t broken = Phys::gCollision ? Phys::gCollision->brokenCount() : 0;
        if (cellScratch == sourceCells && broken == sourceBroken && slotCapacity) return;
        sourceCells = cellScratch;
        sourceBroken = broken;
        u32 np = (u32)protos.size();
        std::vector<u32> perProto(np, 0u);
        auto visit = [&](auto&& fn) {
            for (auto& kc : sourceCells) {
                StreamCell* c = kc.second;
                int cellKey = WorldRenderer::key(c->cx, c->cy, 0);
                for (size_t i = 0; i < c->props.size(); i++) {
                    const World::PropInstance& pi = c->props[i];
                    if (isSignal(pi.type)) continue;
                    if (broken && Phys::gCollision->isPropBroken(cellKey, (int)i)) continue;   // knocked down by gameplay
                    int pr = protoFor(pi);
                    if (pr >= 0) fn(pi, (u32)pr);
                }
            }
        };
        visit([&](const World::PropInstance&, u32 pr) { perProto[pr]++; });
        // list segments: each world prototype as many slots as it has instances, each decor prototype room for a whole
        // frame's decor (one kind can fill a garden)
        u32 base = 0;
        for (u32 p = 0; p < np; p++) {
            const Proto& P = protos[p];
            PropProtoGPU& g = protoCpu[p];
            g.radius = P.radius;
            g.lodDistance = P.lodDistance;
            g.shadow = P.shadow ? 1u : 0u;
            g.indexCount = P.indexCount;
            g.indexStart = P.indexStart;
            g.baseVertex = P.baseVertex;
            g.segBase = base;
            g.segSize = p < worldProtos ? perProto[p] : kDecorCapacity;
            base += g.segSize;
        }
        sourceCount = 0;
        for (u32 p = 0; p < worldProtos; p++) sourceCount += perProto[p];
        u32 slots = base;
        sourceCpu.resize(sourceCount);
        std::vector<u32> fill(np, 0u);
        visit([&](const World::PropInstance& pi, u32 pr) {
            PropSourceGPU& s = sourceCpu[protoCpu[pr].segBase + fill[pr]++];   // (world segments come first, packed)
            s.posScale = vec4(pi.pos, pi.scale);
            u32 bits = pr;
            float protoBits;
            memcpy(&protoBits, &bits, 4);
            s.rot = vec4(cosf(pi.yaw), sinf(pi.yaw), windPhase(pi), protoBits);
        });
        gfx::updateBuffer(protoTable, protoCpu.data(), np * (u32)sizeof(PropProtoGPU));
        if (slots > slotCapacity || !slotCapacity) {
            // grow in steps (the near world gains and loses cells while moving)
            u32 cap = Max(Max(slots, slotCapacity + slotCapacity / 2), 16384u);
            sources.release();
            lists.release();
            sources = gfx::createBuffer(cap * sizeof(PropSourceGPU), sizeof(PropSourceGPU), gfx::BUF_STRUCTURED, nullptr, "prop instances");
            lists = gfx::createBuffer(kViews * cap * sizeof(PropInstanceGPU), sizeof(PropInstanceGPU),
                                      gfx::BUF_VERTEX | gfx::BUF_STRUCTURED | gfx::BUF_UAV, nullptr, "prop instance lists");
            slotCapacity = cap;
        }
        listSlots = slotCapacity;
        if (sourceCount) gfx::updateBuffer(sources, sourceCpu.data(), sourceCount * (u32)sizeof(PropSourceGPU));
    }

    // Scatters this frame's decor plants and culls every prop and plant for the camera and the shadow cascades that
    // render this frame. Runs after the cascades are set up and before the shadow and G-buffer passes draw (frame
    // constants and the global maps bound).
    template <typename CellMap>
    void cull(Renderer& r, CellMap& cells, const mat4* cascadeVP, const bool* cascadeRenders, int cascadeCount, const DecorInputs& in) {
        culled = false;
        syncSources(cells);
        readStats();
        if (!slotCapacity) return;
        auto* c = gfx::ctx;
        PropCullCBData& d = cullCB.data;
        dvec3 cam = r.camera.pos;
        vec3 hi((float)cam.x, (float)cam.y, (float)cam.z);
        d.camHi = vec4(hi, 0.f);
        d.camLo = vec4((float)(cam.x - (double)hi.x), (float)(cam.y - (double)hi.y), (float)(cam.z - (double)hi.z), 0.f);
        for (u32 v = 0; v < kViews; v++) {
            bool on = v == 0 || ((int)v - 1 < cascadeCount && cascadeRenders[v - 1]);
            Frustum fr;
            fr.fromMatrix(v == 0 ? r.viewProjNoJitter : cascadeVP[v - 1]);
            for (int k = 0; k < 6; k++) d.planes[v * 6 + k] = fr.planes[k];
            d.view[v] = vec4(v == 1 ? 0.6f : 1.f, on ? 1.f : 0.f, v > 0 ? 1.f : 0.f, 0.f);
        }
        d.protoCount = (u32)protos.size();
        d.slotsPerView = listSlots;
        d.instanceCount = sourceCount;
        d.decorCapacity = kDecorCapacity;
        // decor ring: a world-anchored grid of 1.2 m cells around the camera, by ground-cover quality (with the grass)
        static const float kDecorRadius[4] = {0.f, 40.f, 52.f, 64.f};
        int q = Clamp(r.settings.grassQuality, 0, 3);
        bool decorOn = q > 0 && in.ready;
        const float cell = 1.2f;
        float radius = kDecorRadius[q];
        double gx = floor((cam.x - radius) / cell) * cell, gy = floor((cam.y - radius) / cell) * cell;
        int n = (int)ceilf(2.f * radius / cell) + 2;
        d.decor0 = vec4((float)gx, (float)gy, cell, (float)n);
        d.decor1 = vec4(radius, (float)worldProtos, World::kWorldHalf, decorOn ? 1.f : 0.f);
        cullCB.upload();
        gfx::Resource cbs[] = {r.frameCB.get(), cullCB.get()};
        c->csSetCBs(0, 2, cbs);
        if (decorOn) {
            gfx::SRV srvs[5] = {nullptr, nullptr, in.splat0, in.splat1, in.overheadClass};
            c->csSetSRVs(0, 5, srvs);
            gfx::UAV uavs[6] = {nullptr, nullptr, nullptr, nullptr, decor.uav, decorCount.uav};
            c->csSetUAVs(0, 6, uavs);
            c->setCS(csDecor);
            c->dispatch(gfx::divUp((u32)n, 8), gfx::divUp((u32)n, 8), 1);
            gfx::unbindCSResources(5, 6);
        }
        gfx::SRV srvs[7] = {sources.srv, protoTable.srv, nullptr, nullptr, nullptr, decor.srv, decorCount.srv};
        c->csSetSRVs(0, 7, srvs);
        gfx::UAV uavs[4] = {lists.uav, counts.uav, args.uav, drawCount.uav};
        c->csSetUAVs(0, 4, uavs);
        c->setCS(csCull);
        c->dispatch(gfx::divUp(sourceCount + (decorOn ? kDecorCapacity : 0u), 64), 1, 1);
        gfx::unbindCSResources(7, 4);
        gfx::SRV argSrvs[2] = {nullptr, protoTable.srv};
        c->csSetSRVs(0, 2, argSrvs);
        gfx::UAV argUavs[6] = {nullptr, counts.uav, args.uav, drawCount.uav, nullptr, decorCount.uav};
        c->csSetUAVs(0, 6, argUavs);
        c->setCS(csArgs);
        c->dispatch(1, 1, 1);
        gfx::unbindCSResources(2, 6);
        // the camera view's draws, for the stats a couple of frames later
        u32 slot = (u32)(gfx::frameNumber() % 3);
        c->copyBufferRegion(statsReadback[slot].buf, 0, args.buf, 0, (u64)protos.size() * 20);
        c->copyBufferRegion(statsReadback[slot].buf, (u64)protos.size() * 20, drawCount.buf, 0, 4);
        statsFrame[slot] = gfx::frameNumber();
        culled = true;
    }

    // Instances and triangles the camera view drew, from the arguments of a frame the GPU has finished (frames in
    // flight: 2, so the one two frames back)
    int gpuInstances = 0, gpuTriangles = 0;
    void readStats() {
        u64 f = gfx::frameNumber();
        if (f < 2) return;
        u32 slot = (u32)((f - 2) % 3);
        if (statsFrame[slot] != f - 2) return;
        const u32* a = (const u32*)gfx::mapReadback(statsReadback[slot]);
        if (!a) return;
        u32 np = (u32)protos.size();
        u32 n = Min(a[np * 5], np);
        gpuInstances = gpuTriangles = 0;
        for (u32 i = 0; i < n; i++) {
            gpuInstances += (int)a[i * 5 + 1];
            gpuTriangles += (int)(a[i * 5] / 3 * a[i * 5 + 1]);
        }
    }

    void drawLists(Renderer& r, u32 view) {
        auto* c = gfx::ctx;
        gfx::Resource vbs[2] = {vb.buf, lists.buf};
        u32 strides[2] = {sizeof(VtxStatic), sizeof(PropInstanceGPU)}, offsets[2] = {0, 0};
        c->setVertexBuffers(0, 2, vbs, strides, offsets);
        c->setIndexBuffer(ib.buf, DXGI_FORMAT_R32_UINT, 0);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        u32 np = (u32)protos.size();
        c->executeIndirect(drawSig, np, args.buf, (u64)view * np * 20, drawCount.buf, (u64)view * 4);
        r.stats.drawCalls++;
    }

    // ---- CPU path (traffic signals)

    template <typename CellMap>
    void gatherSignals(CellMap& cells, dvec3 cam, const Frustum& fr, float distScale, bool shadowPass,
                       const std::function<int(int, vec2)>* signalFn = nullptr) {
        for (auto& b : buckets) b.clear();
        bool anyBroken = Phys::gCollision && Phys::gCollision->brokenCount() > 0;
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->lod != 0 || c->state.load() != 2) continue;
            int cellKey = WorldRenderer::key(c->cx, c->cy, 0);
            for (size_t idx = 0; idx < c->props.size(); idx++) {
                const World::PropInstance& pi = c->props[idx];
                if (!isSignal(pi.type)) continue;
                if (anyBroken && Phys::gCollision->isPropBroken(cellKey, (int)idx)) continue;  // knocked down by gameplay
                int pr = protoFor(pi);
                if (pr < 0) continue;
                const Proto& P = protos[pr];
                if (shadowPass && !P.shadow) continue;
                vec3 rp = rel(pi.pos, cam);
                float d = length(rp);
                if (d > P.lodDistance * distScale * Max(pi.scale, 0.5f)) continue;
                if (!fr.testSphere(rp + vec3(0, 0, P.radius * 0.5f * pi.scale), P.radius * pi.scale)) continue;
                PropInstanceGPU g;
                g.pos = vec4(rp, pi.scale);
                // the lamps follow the AI signal phases when the gameplay layer provides them
                float sigW = (signalFn && *signalFn) ? (float)(*signalFn)((int)pi.flags, pi.pos.xy()) + 0.25f : -1.f;
                g.rot = vec4(cosf(pi.yaw), sinf(pi.yaw), windPhase(pi), sigW);
                buckets[pr].push_back(g);
            }
        }
    }

    int drawBuckets(Renderer& r, bool shadow) {
        auto* c = gfx::ctx;
        flat.clear();
        std::vector<std::pair<int, std::pair<u32, u32>>> draws;
        for (size_t p = 0; p < buckets.size(); p++) {
            if (buckets[p].empty()) continue;
            u32 start = (u32)flat.size();
            u32 n = (u32)Min((size_t)(kMaxInstances - flat.size()), buckets[p].size());
            if (n == 0) break;
            flat.insert(flat.end(), buckets[p].begin(), buckets[p].begin() + n);
            draws.push_back({(int)p, {start, n}});
        }
        if (flat.empty()) return 0;
        gfx::updateBuffer(instBuf, flat.data(), (u32)(flat.size() * sizeof(PropInstanceGPU)));
        gfx::Resource  vbs[2] = {vb.buf, instBuf.buf};
        UINT strides[2] = {sizeof(VtxStatic), sizeof(PropInstanceGPU)}, offsets[2] = {0, 0};
        c->setVertexBuffers(0, 2, vbs, strides, offsets);
        c->setIndexBuffer(ib.buf, DXGI_FORMAT_R32_UINT, 0);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        for (auto& d : draws) {
            const Proto& P = protos[d.first];
            c->drawIndexedInstanced(P.indexCount, d.second.second, P.indexStart, P.baseVertex, d.second.first);
            r.stats.drawCalls++;
            if (!shadow) r.stats.triangles += (int)(P.indexCount / 3 * d.second.second);
        }
        return (int)flat.size();
    }

    // ---- passes

    template <typename CellMap>
    void drawGBuffer(Renderer& r, CellMap& cells) {
        auto* c = gfx::ctx;
        c->setInputLayout(vs.layout);
        c->setVS(vs.vs);
        c->setPS(ps);
        c->setRasterState(gfx::states.cullNone);
        if (culled) {
            drawLists(r, 0);
            r.stats.triangles += gpuTriangles;
        }
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        gatherSignals(cells, r.camera.pos, fr, 1.f, false, &r.signalLampFn);
        drawnInstances = gpuInstances + drawBuckets(r, false);
        c->setRasterState(gfx::states.cullBack);
    }

    template <typename CellMap>
    void drawShadow(Renderer& r, CellMap& cells, const mat4& lightVP, int cascade) {
        if (cascade >= 3) return;
        auto* c = gfx::ctx;
        c->setInputLayout(vsShadow.layout);
        c->setVS(vsShadow.vs);
        c->setPS(psShadow);
        if (culled) drawLists(r, (u32)cascade + 1);
        Frustum fr;
        fr.fromMatrix(lightVP);
        gatherSignals(cells, r.camera.pos, fr, cascade == 0 ? 0.6f : 1.f, true);
        drawBuckets(r, true);
        c->setPS(nullptr);
    }
};

}  // namespace Render
