// Dynamic object rendering: rigid models and GPU-skinned characters. Included from renderer.cpp.
#include <unordered_map>

namespace Render {

struct ObjectCBData {
    mat4 world, prevWorld;
    vec4 tint0, tint1, params, params2;
    vec4 damage0, damage1, dmgBoxC, dmgBoxH;
};

struct Model {
    gfx::Buffer vb, ib;
    u32 indexCount = 0;
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
    // Vehicle crush deformation (rigid models): zone amounts 0..1 (front, rear, left, right) / (roof, under),
    // collision box center/half extents in model space (dmgBoxH.w > 0 enables the deformation).
    vec4 damage0 = vec4(0.f), damage1 = vec4(0.f), dmgBoxC = vec4(0.f), dmgBoxH = vec4(0.f);
};

struct DynamicRenderer {
    gfx::VertexShader vsRigid, vsSkinned, vsRigidShadow, vsSkinnedShadow;
    ID3D11PixelShader* ps = nullptr;
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
        cb.create();
        boneBuf = gfx::createBuffer(kMaxBones * 64, 64, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        prevBoneBuf = gfx::createBuffer(kMaxBones * 64, 64, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
    }

    Model* createModel(const MeshData& m) {
        Model* md = new Model();
        if (m.indices.empty()) return md;
        md->vb = gfx::createBuffer((u32)(m.verts.size() * sizeof(VtxStatic)), sizeof(VtxStatic), gfx::BUF_VERTEX, m.verts.data());
        md->ib = gfx::createBuffer((u32)(m.indices.size() * 4), 4, gfx::BUF_INDEX, m.indices.data());
        md->indexCount = (u32)m.indices.size();
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
        cb.data.params2 = vec4(d.model->skinned ? 1.f : 0.f, 0, 1, 0);
        cb.data.damage0 = d.damage0;
        cb.data.damage1 = d.damage1;
        cb.data.dmgBoxC = d.dmgBoxC;
        cb.data.dmgBoxH = d.dmgBoxH;
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
            c->DrawIndexed(d.model->indexCount, 0, 0);
            r.stats.drawCalls++;
            r.stats.triangles += (int)d.model->indexCount / 3;
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
            c->DrawIndexed(d.model->indexCount, 0, 0);
            r.stats.drawCalls++;
        }
        ID3D11ShaderResourceView* nul = nullptr;
        c->VSSetShaderResources(20, 1, &nul);
    }
};

}  // namespace Render
