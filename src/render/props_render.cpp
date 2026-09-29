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
    ID3D11PixelShader *ps = nullptr, *psShadow = nullptr;
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
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"INSTPOS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INSTROT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        vs = gfx::loadVS("props.hlsl", "vsProp", layout, 8);
        vsShadow = gfx::loadVS("props.hlsl", "vsPropShadow", layout, 8);
        ps = gfx::loadPS("props.hlsl", "psProp");
        psShadow = gfx::loadPS("props.hlsl", "psPropShadow");
        // Foliage textures
        int fsize = Platform::hasArg("autotest") ? 256 : 512;
        foliageArr = createMaterialArray(fsize, 6, true);
        ID3D11ComputeShader* cs = gfx::loadCS("foliage.hlsl", "csFoliage");
        gfx::CBuffer<FoliageCBData> fcb;
        fcb.create();
        gfx::ctx->CSSetShader(cs, nullptr, 0);
        gfx::ctx->CSSetUnorderedAccessViews(0, 1, &foliageArr.uav, nullptr);
        for (int l = 0; l < 6; l++) {
            fcb.data.layer = (u32)l;
            fcb.data.size = (u32)fsize;
            fcb.upload();
            ID3D11Buffer* cbs[] = {fcb.get()};
            gfx::ctx->CSSetConstantBuffers(1, 1, cbs);
            gfx::ctx->Dispatch(gfx::divUp(fsize, 8), gfx::divUp(fsize, 8), 1);
        }
        gfx::unbindCSResources(1, 1);
        gfx::ctx->GenerateMips(foliageArr.srv);
        fcb.release();
        LOG("Props: %zu prototypes, %zu verts", protos.size(), verts.size());
    }

    int protoFor(const World::PropInstance& p) const {
        int t = Clamp((int)p.type, 0, World::PROP_COUNT - 1);
        return protoIndex[t][p.variant % variantCount[t]];
    }

    // Gather instances from near cells into buckets
    template <typename CellMap>
    void gather(CellMap& cells, dvec3 cam, const Frustum& fr, float distScale, bool shadowPass) {
        for (auto& b : buckets) b.clear();
        for (auto& kv : cells) {
            StreamCell* c = kv.second;
            if (c->lod != 0 || c->state.load() != 2) continue;
            for (const World::PropInstance& pi : c->props) {
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
                g.rot = vec4(cosf(pi.yaw), sinf(pi.yaw), phase, -1.f);
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
        ID3D11Buffer* vbs[2] = {vb.buf, instBuf.buf};
        UINT strides[2] = {sizeof(VtxStatic), sizeof(PropInstanceGPU)}, offsets[2] = {0, 0};
        c->IASetVertexBuffers(0, 2, vbs, strides, offsets);
        c->IASetIndexBuffer(ib.buf, DXGI_FORMAT_R32_UINT, 0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (auto& d : draws) {
            const Proto& P = protos[d.first];
            c->DrawIndexedInstanced(P.indexCount, d.second.second, P.indexStart, P.baseVertex, d.second.first);
            r.stats.drawCalls++;
            if (!shadow) r.stats.triangles += (int)(P.indexCount / 3 * d.second.second);
        }
        if (!shadow) drawnInstances = (int)flat.size();
    }

    template <typename CellMap>
    void drawGBuffer(Renderer& r, CellMap& cells, MaterialLibrary* mats) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        gather(cells, r.camera.pos, fr, 1.f, false);
        c->IASetInputLayout(vs.layout);
        c->VSSetShader(vs.vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        ID3D11ShaderResourceView* srvs[3] = {mats->table.srv, mats->albedoArr.srv, mats->normalArr.srv};
        c->PSSetShaderResources(10, 3, srvs);
        c->PSSetShaderResources(15, 1, &foliageArr.srv);
        c->RSSetState(gfx::states.cullNone);
        drawBuckets(r, false);
        c->RSSetState(gfx::states.cullBack);
        ID3D11ShaderResourceView* nulls[6] = {};
        c->PSSetShaderResources(10, 6, nulls);
    }

    template <typename CellMap>
    void drawShadow(Renderer& r, CellMap& cells, const mat4& lightVP, int cascade) {
        if (cascade >= 3) return;
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(lightVP);
        gather(cells, r.camera.pos, fr, cascade == 0 ? 0.6f : 1.f, true);
        c->IASetInputLayout(vsShadow.layout);
        c->VSSetShader(vsShadow.vs, nullptr, 0);
        c->PSSetShader(psShadow, nullptr, 0);
        c->PSSetShaderResources(15, 1, &foliageArr.srv);
        ID3D11SamplerState* samps[] = {gfx::states.linearWrap};
        c->PSSetSamplers(2, 1, samps);
        drawBuckets(r, true);
        c->PSSetShader(nullptr, nullptr, 0);
        ID3D11ShaderResourceView* nul = nullptr;
        c->PSSetShaderResources(15, 1, &nul);
    }
};

}  // namespace Render
