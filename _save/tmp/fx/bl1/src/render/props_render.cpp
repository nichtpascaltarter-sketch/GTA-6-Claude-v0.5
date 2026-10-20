// Instanced prop / vegetation rendering. Included from renderer.cpp.
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

struct FoliageCBData {
    u32 layer, size, pad0, pad1;
};

struct PropRenderer {
    struct Proto {
        u32 indexStart = 0, indexCount = 0;
        int baseVertex = 0;
        float radius = 1.f, lodDistance = 100.f;
        bool shadow = true, foliage = false;
    };
    std::vector<Proto> protos;
    int protoIndex[World::PROP_COUNT][8];
    int variantCount[World::PROP_COUNT];
    gfx::Buffer vb, ib, instBuf;
    static const int kMaxInstances = 65536;
    std::vector<std::vector<PropInstanceGPU>> buckets;
    std::vector<PropInstanceGPU> flat;
    gfx::VertexShader vs, vsShadow;
    gfx::PixelShader ps = nullptr, psShadow = nullptr;
    gfx::Texture foliageArr;
    int drawnInstances = 0;

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

    // Gather instances from near cells into buckets
    template <typename CellMap>
    void gather(CellMap& cells, dvec3 cam, const Frustum& fr, float distScale, bool shadowPass,
                const std::function<int(int, vec2)>* signalFn = nullptr) {
        for (auto& b : buckets) b.clear();
        bool anyBroken = Phys::gCollision && Phys::gCollision->brokenCount() > 0;
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->lod != 0 || c->state.load() != 2) continue;
            int cellKey = WorldRenderer::key(c->cx, c->cy, 0);
            for (size_t idx = 0; idx < c->props.size(); idx++) {
                const World::PropInstance& pi = c->props[idx];
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
                float phase = hashToFloat(hash2i((int)(pi.pos.x * 3.f), (int)(pi.pos.y * 3.f))) * kTwoPi;
                // traffic lamps follow the AI signal phases when the gameplay layer provides them
                float sigW = ((pi.type == World::PROP_TRAFFIC_LIGHT || pi.type == World::PROP_SIGNAL_SPAN) && signalFn && *signalFn) ? (float)(*signalFn)((int)pi.flags, pi.pos.xy()) + 0.25f : -1.f;
                g.rot = vec4(cosf(pi.yaw), sinf(pi.yaw), phase, sigW);
                buckets[pr].push_back(g);
            }
        }
    }

    void drawBuckets(Renderer& r, bool shadow) {
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
        if (flat.empty()) return;
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
        if (!shadow) drawnInstances = (int)flat.size();
    }

    template <typename CellMap>
    void drawGBuffer(Renderer& r, CellMap& cells) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        gather(cells, r.camera.pos, fr, 1.f, false, &r.signalLampFn);
        c->setInputLayout(vs.layout);
        c->setVS(vs.vs);
        c->setPS(ps);
        c->setRasterState(gfx::states.cullNone);
        drawBuckets(r, false);
        c->setRasterState(gfx::states.cullBack);
    }

    template <typename CellMap>
    void drawShadow(Renderer& r, CellMap& cells, const mat4& lightVP, int cascade) {
        if (cascade >= 3) return;
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(lightVP);
        gather(cells, r.camera.pos, fr, cascade == 0 ? 0.6f : 1.f, true);
        c->setInputLayout(vsShadow.layout);
        c->setVS(vsShadow.vs);
        c->setPS(psShadow);
        drawBuckets(r, true);
        c->setPS(nullptr);
    }
};

}  // namespace Render
