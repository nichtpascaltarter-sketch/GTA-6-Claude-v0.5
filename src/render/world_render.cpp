// Streaming of world cells (background generation, GPU upload, LOD) and their rendering.
// Included from renderer.cpp.
#include "../world/buildings.h"
#include <unordered_map>

namespace World {
struct CellGeometry;
void generateCell(int cx, int cy, bool detail, CellGeometry& out);
}

namespace Render {

struct DrawCBData {
    vec4 cellOffset;
    vec4 params;
};

struct StreamCell {
    int cx = 0, cy = 0, lod = 0;
    std::atomic<int> state{0};  // 0 queued, 1 generated (cpu ready), 2 uploaded, 3 empty
    World::CellGeometry* geo = nullptr;
    gfx::Buffer vb, ib;
    u32 opaqueCount = 0, decalStart = 0, decalCount = 0;
    AABB bounds;  // cell-relative
    double lastUsed = 0;
    std::vector<World::LightInstance> lights;
    std::vector<World::PropInstance> props;
    std::vector<World::CollisionBox> collision;
};

struct WorldRenderer {
    std::unordered_map<int, StreamCell*> cells;
    gfx::VertexShader vs, vsShadow;
    ID3D11PixelShader* ps = nullptr;
    ID3D11RasterizerState* decalRS = nullptr;
    gfx::CBuffer<DrawCBData> drawCB;
    MaterialLibrary* mats = nullptr;
    gfx::Buffer facadeBuf;
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
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        vs = gfx::loadVS("world.hlsl", "vsWorld", layout, 6);
        vsShadow = gfx::loadVS("world.hlsl", "vsWorldShadow", layout, 6);
        ps = gfx::loadPS("world.hlsl", "psWorld");
        drawCB.create();
        D3D11_RASTERIZER_DESC rs = {};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_BACK;
        rs.FrontCounterClockwise = TRUE;
        rs.DepthClipEnable = TRUE;
        rs.DepthBias = 8;
        rs.SlopeScaledDepthBias = 2.f;
        gfx::dev->CreateRasterizerState(&rs, &decalRS);
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
        c->props = std::move(g->props);
        c->collision = std::move(g->collision);
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

    void bindCommon(Renderer& r) {
        auto* c = gfx::ctx;
        c->IASetInputLayout(vs.layout);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* srvs[5] = {mats->table.srv, mats->albedoArr.srv, mats->normalArr.srv, facadeBuf.srv, signTex.srv};
        c->PSSetShaderResources(10, 5, srvs);
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

    void drawGBuffer(Renderer& r) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        bindCommon(r);
        c->VSSetShader(vs.vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        ID3D11Buffer* cbs[] = {drawCB.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        drawnCells = 0;
        forVisible(fr, r.camera.pos, false, [&](StreamCell* sc, vec3 off) {
            drawCB.data.cellOffset = vec4(off, 0);
            drawCB.data.params = vec4(0);
            drawCB.upload();
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->IASetVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            if (sc->opaqueCount) c->DrawIndexed(sc->opaqueCount, 0, 0);
            r.stats.drawCalls++;
            r.stats.triangles += sc->opaqueCount / 3;
            drawnCells++;
        });
        // Decals (road paint) with depth bias
        c->RSSetState(decalRS);
        forVisible(fr, r.camera.pos, true, [&](StreamCell* sc, vec3 off) {
            if (!sc->decalCount) return;
            drawCB.data.cellOffset = vec4(off, 0);
            drawCB.upload();
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->IASetVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            c->DrawIndexed(sc->decalCount, sc->decalStart, 0);
            r.stats.drawCalls++;
        });
        c->RSSetState(gfx::states.cullBack);
        ID3D11ShaderResourceView* nulls[5] = {};
        c->PSSetShaderResources(10, 5, nulls);
    }

    void drawShadow(Renderer& r, const mat4& lightVP, int cascade) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(lightVP);
        c->IASetInputLayout(vsShadow.layout);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vsShadow.vs, nullptr, 0);
        c->PSSetShader(nullptr, nullptr, 0);
        ID3D11Buffer* cbs[] = {drawCB.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        forVisible(fr, r.camera.pos, false, [&](StreamCell* sc, vec3 off) {
            // near cascades only need near cells
            if (cascade <= 1 && sc->lod == 1) return;
            drawCB.data.cellOffset = vec4(off, 0);
            drawCB.upload();
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->IASetVertexBuffers(0, 1, &sc->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(sc->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            if (sc->opaqueCount) c->DrawIndexed(sc->opaqueCount, 0, 0);
            r.stats.drawCalls++;
        });
    }

    int pendingCount() const {
        int n = 0;
        for (auto& kv : cells) n += kv.second->state.load() < 2;
        return n;
    }
};

}  // namespace Render
