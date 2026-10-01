// Dynamic object rendering: rigid models and GPU-skinned characters. Included from renderer.cpp.
#include <unordered_map>

namespace Render {

struct ObjectCBData {
    mat4 world, prevWorld;
    vec4 tint0, tint1, params, params2;
    vec4 damage0, damage1, dmgBoxC, dmgBoxH;
    vec4 wounds[4];
};

struct Model {
    gfx::Buffer vb, ib;
    u32 indexCount = 0;
    u32 glassCount = 0;   // see-through windows (MAT_CAR_WINDOW): the last glassCount indices, drawn forward after lighting
    u32 cardStart = 0;    // skinned: first index of the hair strand cards (they come last); indexCount = no cards
    AABB bounds;
    bool skinned = false;
    void release() { vb.release(); ib.release(); }
};

// One draw of a model. Position in double precision; rotation as a 3x3 basis; optional bone palette.
struct DrawItem {
    const Model* model = nullptr;
    dvec3 pos;
    mat3 rot;
    vec3 scale = vec3(1, 1, 1);
    vec4 tint0 = vec4(1, 1, 1, 0), tint1 = vec4(1, 1, 1, 0);
    u32 lightBits = 0;
    float emissiveScale = 1.f;
    float wetExposed = 1.f;
    const mat4* bones = nullptr;  // model-space skinning matrices (bone * inverse bind), boneCount entries
    int boneCount = 0;
    u64 id = 0;                   // stable id for motion vectors (0 = none)
    bool castShadow = true;
    bool drawGlass = true;        // false: the see-through windows are shattered (the forward glass pass skips them)
    float paintFinish = 0.f;      // car paint finish (0 gloss, 1 metallic, 2 pearl, 3 matte, 4 chrome)
    float glassTint = 0.f;        // extra window tint 0..1
    // Vehicle crush deformation (rigid models): zone amounts 0..1 (front, rear, left, right) / (roof, under),
    // collision box center/half extents in model space (dmgBoxH.w > 0 enables the deformation).
    vec4 damage0 = vec4(0.f), damage1 = vec4(0.f), dmgBoxC = vec4(0.f), dmgBoxH = vec4(0.f);
    // Character wounds: bind-pose model-space position (xyz) + radius (w); w = 0 unused
    vec4 wounds[4] = {vec4(0.f), vec4(0.f), vec4(0.f), vec4(0.f)};
};

struct DynamicRenderer {
    gfx::VertexShader vsRigid, vsSkinned, vsRigidShadow, vsSkinnedShadow;
    ID3D11PixelShader* ps = nullptr;
    ID3D11PixelShader* psGlass = nullptr;
    ID3D11PixelShader* psHairCard = nullptr;       // hair strand cards (dithered alpha, strand tangent)
    gfx::VertexShader vsCardShadow;
    ID3D11PixelShader* psCardShadow = nullptr;     // alpha-tested card shadows
    gfx::CBuffer<ObjectCBData> cb;
    gfx::Buffer boneBuf, prevBoneBuf;
    static const int kMaxBones = 16384;
    std::vector<DrawItem> items;
    std::vector<mat4> bonesFrame, prevBonesFrame;
    std::vector<int> boneOffsets;
    struct PrevState {
        dvec3 pos;
        mat3 rot;
        vec3 scale;
        std::vector<mat4> bones;
        u32 frame;
    };
    std::unordered_map<u64, PrevState> prev;
    u32 frame = 0;
    MaterialLibrary* mats = nullptr;

    void init(MaterialLibrary* m) {
        mats = m;
        D3D11_INPUT_ELEMENT_DESC rigid[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        D3D11_INPUT_ELEMENT_DESC skinned[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R16G16_SNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R16G16_SNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"MATID", 0, DXGI_FORMAT_R32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"BONES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"WEIGHTS", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        vsRigid = gfx::loadVS("dynamic.hlsl", "vsRigid", rigid, 6);
        vsSkinned = gfx::loadVS("dynamic.hlsl", "vsSkinned", skinned, 8);
        vsRigidShadow = gfx::loadVS("dynamic.hlsl", "vsRigidShadow", rigid, 6);
        vsSkinnedShadow = gfx::loadVS("dynamic.hlsl", "vsSkinnedShadow", skinned, 8);
        ps = gfx::loadPS("dynamic.hlsl", "psDynamic");
        psGlass = gfx::loadPS("dynamic.hlsl", "psGlass");
        psHairCard = gfx::loadPS("dynamic.hlsl", "psHairCard");
        vsCardShadow = gfx::loadVS("dynamic.hlsl", "vsSkinnedShadowCard", skinned, 8);
        psCardShadow = gfx::loadPS("dynamic.hlsl", "psHairCardShadow");
        cb.create();
        boneBuf = gfx::createBuffer(kMaxBones * 64, 64, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        prevBoneBuf = gfx::createBuffer(kMaxBones * 64, 64, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
    }

    Model* createModel(const MeshData& m) {
        Model* md = new Model();
        if (m.indices.empty()) return md;
        // opaque triangles first, see-through windows last (their own forward pass)
        std::vector<u32> idx;
        idx.reserve(m.indices.size());
        u32 glass = 0;
        for (int pass = 0; pass < 2; pass++)
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
                bool isGlass = (m.verts[m.indices[t]].mat & 0xffu) == MAT_CAR_WINDOW;
                if (isGlass != (pass == 1)) continue;
                idx.insert(idx.end(), {m.indices[t], m.indices[t + 1], m.indices[t + 2]});
                if (isGlass) glass += 3;
            }
        md->vb = gfx::createBuffer((u32)(m.verts.size() * sizeof(VtxStatic)), sizeof(VtxStatic), gfx::BUF_VERTEX, m.verts.data());
        md->ib = gfx::createBuffer((u32)(idx.size() * 4), 4, gfx::BUF_INDEX, idx.data());
        md->indexCount = (u32)idx.size();
        md->glassCount = glass;
        md->bounds = m.bounds;
        return md;
    }
    Model* createSkinnedModel(const SkinnedMeshData& m) {
        Model* md = new Model();
        md->skinned = true;
        if (m.indices.empty()) return md;
        md->vb = gfx::createBuffer((u32)(m.verts.size() * sizeof(VtxSkinned)), sizeof(VtxSkinned), gfx::BUF_VERTEX, m.verts.data());
        md->ib = gfx::createBuffer((u32)(m.indices.size() * 4), 4, gfx::BUF_INDEX, m.indices.data());
        md->indexCount = (u32)m.indices.size();
        md->cardStart = md->indexCount;
        // Hair strand cards (MAT_HAIR with a card kind in bits 8-11) are the last triangles of the index buffer
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            u32 mt = m.verts[m.indices[t]].mat;
            if ((mt & 0xffu) == MAT_HAIR && ((mt >> 8) & 15u) != 0) { md->cardStart = (u32)t; break; }
        }
        md->bounds = m.bounds;
        return md;
    }

    void beginFrame() { items.clear(); }
    void submit(const DrawItem& d) {
        if (d.model && d.model->indexCount) items.push_back(d);
    }

    static mat4 worldRel(dvec3 pos, const mat3& rot, vec3 scale, dvec3 cam) {
        vec3 t = rel(pos, cam);
        return mat4(vec4(rot.c[0] * scale.x, 0), vec4(rot.c[1] * scale.y, 0), vec4(rot.c[2] * scale.z, 0), vec4(t, 1));
    }

    // Prepare bone palettes and per-item previous transforms (call once per frame before drawing)
    void prepare(Renderer& r) {
        bonesFrame.clear();
        prevBonesFrame.clear();
        boneOffsets.assign(items.size(), 0);
        for (size_t i = 0; i < items.size(); i++) {
            DrawItem& d = items[i];
            if (!d.bones || d.boneCount <= 0) continue;
            if ((int)bonesFrame.size() + d.boneCount > kMaxBones) { d.bones = nullptr; continue; }
            boneOffsets[i] = (int)bonesFrame.size();
            bonesFrame.insert(bonesFrame.end(), d.bones, d.bones + d.boneCount);
            auto it = d.id ? prev.find(d.id) : prev.end();
            if (it != prev.end() && (int)it->second.bones.size() == d.boneCount)
                prevBonesFrame.insert(prevBonesFrame.end(), it->second.bones.begin(), it->second.bones.end());
            else prevBonesFrame.insert(prevBonesFrame.end(), d.bones, d.bones + d.boneCount);
        }
        if (!bonesFrame.empty()) {
            gfx::updateBuffer(boneBuf, bonesFrame.data(), (u32)(bonesFrame.size() * 64));
            gfx::updateBuffer(prevBoneBuf, prevBonesFrame.data(), (u32)(prevBonesFrame.size() * 64));
        }
        (void)r;
    }

    void endFrame() {
        // Store current transforms as "previous" for motion vectors; drop stale entries
        frame++;
        for (auto& d : items) {
            if (!d.id) continue;
            PrevState& p = prev[d.id];
            p.pos = d.pos;
            p.rot = d.rot;
            p.scale = d.scale;
            p.frame = frame;
            if (d.bones && d.boneCount) p.bones.assign(d.bones, d.bones + d.boneCount);
            else p.bones.clear();
        }
        if ((frame & 63) == 0) {
            for (auto it = prev.begin(); it != prev.end();) {
                if (frame - it->second.frame > 30) it = prev.erase(it);
                else ++it;
            }
        }
    }

    void setObjectCB(Renderer& r, const DrawItem& d, int boneOffset) {
        dvec3 cam = r.camera.pos;
        cb.data.world = worldRel(d.pos, d.rot, d.scale, cam);
        auto it = d.id ? prev.find(d.id) : prev.end();
        if (it != prev.end()) cb.data.prevWorld = worldRel(it->second.pos, it->second.rot, it->second.scale, cam);
        else cb.data.prevWorld = cb.data.world;
        // prevWorld must be expressed relative to the *previous* camera for gPrevViewProj (which already compensates
        // the camera delta), i.e. relative to the current camera: gPrevViewProj handles camera motion.
        cb.data.tint0 = d.tint0;
        cb.data.tint1 = d.tint1;
        cb.data.params = vec4((float)d.lightBits, (float)boneOffset, d.wetExposed, d.emissiveScale);
        // y: window tint (rigid) / hair strand cards present over the hair shell (skinned)
        float y2 = d.model->skinned ? (d.model->cardStart < d.model->indexCount ? 1.f : 0.f) : d.glassTint;
        cb.data.params2 = vec4(d.model->skinned ? 1.f : 0.f, y2, 1.f, d.paintFinish);
        cb.data.damage0 = d.damage0;
        cb.data.damage1 = d.damage1;
        cb.data.dmgBoxC = d.dmgBoxC;
        cb.data.dmgBoxH = d.dmgBoxH;
        for (int w = 0; w < 4; w++) cb.data.wounds[w] = d.wounds[w];
        cb.upload();
    }

    void drawGBuffer(Renderer& r) {
        if (items.empty()) return;
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        ID3D11Buffer* cbs[] = {cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        ID3D11ShaderResourceView* srvs[3] = {mats->table.srv, mats->albedoArr.srv, mats->normalArr.srv};
        c->PSSetShaderResources(10, 3, srvs);
        ID3D11ShaderResourceView* bsrv[2] = {boneBuf.srv, prevBoneBuf.srv};
        c->VSSetShaderResources(20, 2, bsrv);
        c->PSSetShader(ps, nullptr, 0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->RSSetState(gfx::states.cullBack);
        for (size_t i = 0; i < items.size(); i++) {
            const DrawItem& d = items[i];
            mat4 w = worldRel(d.pos, d.rot, d.scale, r.camera.pos);
            AABB b = transformAABB(d.model->bounds.valid() ? d.model->bounds : AABB(vec3(-1), vec3(1)), w);
            if (d.model->skinned) { b.mn -= vec3(1.5f); b.mx += vec3(1.5f); }
            if (!fr.testAABB(b)) continue;
            setObjectCB(r, d, boneOffsets[i]);
            UINT stride = d.model->skinned ? sizeof(VtxSkinned) : sizeof(VtxStatic), offset = 0;
            c->IASetInputLayout(d.model->skinned ? vsSkinned.layout : vsRigid.layout);
            c->VSSetShader(d.model->skinned ? vsSkinned.vs : vsRigid.vs, nullptr, 0);
            c->IASetVertexBuffers(0, 1, &d.model->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(d.model->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            u32 opaqueCount = d.model->indexCount - d.model->glassCount;
            u32 cards = d.model->skinned ? d.model->indexCount - d.model->cardStart : 0;
            opaqueCount -= cards;
            if (opaqueCount) c->DrawIndexed(opaqueCount, 0, 0);
            r.stats.drawCalls++;
            r.stats.triangles += (int)opaqueCount / 3;
            if (cards) {
                // hair strand cards: two-sided, dithered coverage
                c->PSSetShader(psHairCard, nullptr, 0);
                c->RSSetState(gfx::states.cullNone);
                c->DrawIndexed(cards, d.model->cardStart, 0);
                c->PSSetShader(ps, nullptr, 0);
                c->RSSetState(gfx::states.cullBack);
                r.stats.drawCalls++;
                r.stats.triangles += (int)cards / 3;
            }
        }
        ID3D11ShaderResourceView* nulls[3] = {};
        c->PSSetShaderResources(10, 3, nulls);
        c->VSSetShaderResources(20, 2, nulls);
    }

    void drawShadow(Renderer& r, const mat4& lightVP, int cascade) {
        if (items.empty() || cascade >= 3) return;
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(lightVP);
        ID3D11Buffer* cbs[] = {cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->VSSetShaderResources(20, 1, &boneBuf.srv);
        c->PSSetShader(nullptr, nullptr, 0);
        for (size_t i = 0; i < items.size(); i++) {
            const DrawItem& d = items[i];
            if (!d.castShadow) continue;
            mat4 w = worldRel(d.pos, d.rot, d.scale, r.camera.pos);
            AABB b = transformAABB(d.model->bounds.valid() ? d.model->bounds : AABB(vec3(-1), vec3(1)), w);
            if (d.model->skinned) { b.mn -= vec3(1.5f); b.mx += vec3(1.5f); }
            if (!fr.testAABB(b)) continue;
            setObjectCB(r, d, boneOffsets[i]);
            UINT stride = d.model->skinned ? sizeof(VtxSkinned) : sizeof(VtxStatic), offset = 0;
            c->IASetInputLayout(d.model->skinned ? vsSkinnedShadow.layout : vsRigidShadow.layout);
            c->VSSetShader(d.model->skinned ? vsSkinnedShadow.vs : vsRigidShadow.vs, nullptr, 0);
            c->IASetVertexBuffers(0, 1, &d.model->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(d.model->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            u32 opaqueCount = d.model->indexCount - d.model->glassCount;   // windows let the sun into the cabin
            u32 cards = d.model->skinned ? d.model->indexCount - d.model->cardStart : 0;
            opaqueCount -= cards;
            if (opaqueCount) c->DrawIndexed(opaqueCount, 0, 0);
            r.stats.drawCalls++;
            if (cards && cascade < 2) {
                // strand cards: alpha-tested in the two near cascades (below a far texel they add nothing)
                c->VSSetShader(vsCardShadow.vs, nullptr, 0);
                c->PSSetShader(psCardShadow, nullptr, 0);
                c->DrawIndexed(cards, d.model->cardStart, 0);
                c->PSSetShader(nullptr, nullptr, 0);
                r.stats.drawCalls++;
            }
        }
        ID3D11ShaderResourceView* nul = nullptr;
        c->VSSetShaderResources(20, 1, &nul);
    }

    // See-through vehicle windows: forward pass into the lit HDR target (depth test, no depth write, premultiplied
    // alpha), far to near. Needs the frame globals (sky, shadows, probe, fog) bound; the caller sets HDR + depth targets.
    void drawGlass(Renderer& r) {
        glassOrder.clear();
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        for (size_t i = 0; i < items.size(); i++) {
            const DrawItem& d = items[i];
            if (!d.model->glassCount || d.model->skinned || !d.drawGlass) continue;
            mat4 w = worldRel(d.pos, d.rot, d.scale, r.camera.pos);
            AABB b = transformAABB(d.model->bounds.valid() ? d.model->bounds : AABB(vec3(-1), vec3(1)), w);
            if (!fr.testAABB(b)) continue;
            glassOrder.push_back({length2(rel(d.pos, r.camera.pos)), (int)i});
        }
        if (glassOrder.empty()) return;
        std::sort(glassOrder.begin(), glassOrder.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
        auto* c = gfx::ctx;
        ID3D11Buffer* cbs[] = {cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        ID3D11Buffer* scb[] = {r.shadowCB.get()};
        c->PSSetConstantBuffers(3, 1, scb);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->IASetInputLayout(vsRigid.layout);
        c->VSSetShader(vsRigid.vs, nullptr, 0);
        c->PSSetShader(psGlass, nullptr, 0);
        c->RSSetState(gfx::states.cullBack);
        float bf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(gfx::states.premultiplied, bf, 0xffffffffu);
        c->OMSetDepthStencilState(gfx::states.depthGreaterEqualNoWrite, 0);
        for (auto& o : glassOrder) {
            const DrawItem& d = items[o.second];
            setObjectCB(r, d, 0);
            UINT stride = sizeof(VtxStatic), offset = 0;
            c->IASetVertexBuffers(0, 1, &d.model->vb.buf, &stride, &offset);
            c->IASetIndexBuffer(d.model->ib.buf, DXGI_FORMAT_R32_UINT, 0);
            c->DrawIndexed(d.model->glassCount, d.model->indexCount - d.model->glassCount, 0);
            r.stats.drawCalls++;
            r.stats.triangles += (int)d.model->glassCount / 3;
        }
        c->OMSetBlendState(gfx::states.opaque, bf, 0xffffffffu);
        c->OMSetDepthStencilState(gfx::states.depthGreaterWrite, 0);
    }
    std::vector<std::pair<float, int>> glassOrder;
};

}  // namespace Render
