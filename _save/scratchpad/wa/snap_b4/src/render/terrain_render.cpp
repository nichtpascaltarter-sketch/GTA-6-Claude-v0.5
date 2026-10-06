// CDLOD terrain renderer + terrain material generation. Included from renderer.cpp.
#include "../world/worldmap.h"

namespace Render {

struct TerrainCBData {
    vec4 params;
    vec4 morph[16];
};

struct MatGenCBData {
    u32 layer, type, size, seed;
    vec4 colorA, colorB, colorC, params;
};

// Texture array with UNORM UAV for generation and sRGB (or UNORM) SRV for sampling, full mips.
static gfx::Texture createMaterialArray(int size, int layers, bool srgb) {
    gfx::Texture t;
    int mips = gfx::mipCount(size, size);
    t.width = t.height = size;
    t.mips = mips;
    t.layers = layers;
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = d.Height = (UINT)size;
    d.MipLevels = (UINT)mips;
    d.ArraySize = (UINT)layers;
    d.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(gfx::dev->CreateTexture2D(&d, nullptr, &tex))) FatalError("material array creation failed");
    t.res = tex;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sv.Texture2DArray.MipLevels = (UINT)mips;
    sv.Texture2DArray.ArraySize = (UINT)layers;
    gfx::dev->CreateShaderResourceView(tex, &sv, &t.srv);
    D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
    uv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uv.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    uv.Texture2DArray.ArraySize = (UINT)layers;
    gfx::dev->CreateUnorderedAccessView(tex, &uv, &t.uav);
    return t;
}

struct TerrainRenderer {
    World::WorldMap* map = nullptr;
    gfx::Texture heightTex, splat0Tex, splat1Tex, waterTex;
    gfx::Texture albedoArr, normalArr;
    gfx::Buffer vb, ib, nodeBuf;
    int gridM = 32;
    int indexCount = 0;
    static const int kMaxNodes = 4096;
    gfx::VertexShader vs, vsShadow;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ComputeShader* csMatGen[7] = {};
    gfx::CBuffer<TerrainCBData> cb;
    gfx::CBuffer<MatGenCBData> matCB;
    // Quadtree min/max heights per level (level 0 = 64 m leaves)
    static const int kLevels = 7;
    float leafSize = 64.f;
    int levelRes[kLevels];
    std::vector<vec2> minmax[kLevels];
    float ranges[kLevels];
    std::vector<vec4> nodes;
    int drawnNodes = 0;

    void init() {
        std::vector<vec2> verts;
        for (int y = 0; y <= gridM; y++)
            for (int x = 0; x <= gridM; x++) verts.push_back(vec2((float)x / gridM, (float)y / gridM));
        std::vector<u16> idx;
        for (int y = 0; y < gridM; y++)
            for (int x = 0; x < gridM; x++) {
                u16 v00 = (u16)(y * (gridM + 1) + x), v10 = (u16)(v00 + 1), v01 = (u16)(v00 + gridM + 1), v11 = (u16)(v01 + 1);
                // alternate diagonal for better shape
                if ((x + y) & 1) {
                    idx.insert(idx.end(), {v00, v10, v11, v00, v11, v01});
                } else {
                    idx.insert(idx.end(), {v00, v10, v01, v10, v11, v01});
                }
            }
        indexCount = (int)idx.size();
        vb = gfx::createBuffer((u32)(verts.size() * sizeof(vec2)), sizeof(vec2), gfx::BUF_VERTEX, verts.data());
        ib = gfx::createBuffer((u32)(idx.size() * 2), 2, gfx::BUF_INDEX, idx.data());
        nodeBuf = gfx::createBuffer(kMaxNodes * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        D3D11_INPUT_ELEMENT_DESC layout[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        vs = gfx::loadVS("terrain.hlsl", "vsTerrain", layout, 1);
        vsShadow = gfx::loadVS("terrain.hlsl", "vsTerrainShadow", layout, 1);
        ps = gfx::loadPS("terrain.hlsl", "psTerrain");
        static const char* genIds[7] = {"0", "1", "2", "3", "4", "5", "6"};
        for (int g = 0; g < 7; g++) csMatGen[g] = gfx::loadCS("matgen.hlsl", "csGenerate", {{"GEN", genIds[g]}});
        cb.create();
        matCB.create();
        ranges[0] = 90.f;
        for (int i = 1; i < kLevels; i++) ranges[i] = ranges[i - 1] * 2.f;
        generateMaterials();
    }

    void genLayer(int layer, int type, vec3 a, vec3 b, vec3 c, vec4 params, int size) {
        gfx::ctx->CSSetShader(csMatGen[type], nullptr, 0);
        matCB.data.layer = (u32)layer;
        matCB.data.type = (u32)type;
        matCB.data.size = (u32)size;
        matCB.data.seed = (u32)(layer * 131 + 7);
        matCB.data.colorA = vec4(a, 1);
        matCB.data.colorB = vec4(b, 1);
        matCB.data.colorC = vec4(c, 1);
        matCB.data.params = params;
        matCB.upload();
        ID3D11Buffer* cbs[] = {matCB.get()};
        gfx::ctx->CSSetConstantBuffers(1, 1, cbs);
        gfx::ctx->Dispatch(gfx::divUp(size, 8), gfx::divUp(size, 8), 1);
    }

    void generateMaterials() {
        const int size = 1024;
        albedoArr = createMaterialArray(size, World::TL_COUNT, true);
        normalArr = createMaterialArray(size, World::TL_COUNT, false);
        auto* c = gfx::ctx;
        ID3D11UnorderedAccessView* uavs[] = {albedoArr.uav, normalArr.uav};
        c->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        genLayer(World::TL_SAND, 0, vec3(0.45f, 0.38f, 0.26f), vec3(0.58f, 0.50f, 0.36f), vec3(0.2f, 0.18f, 0.16f), vec4(0, 0, 0, 4), size);
        genLayer(World::TL_GRASS, 1, vec3(0.05f, 0.10f, 0.025f), vec3(0.10f, 0.16f, 0.04f), vec3(0.24f, 0.21f, 0.10f), vec4(0.35f, 0, 0, 5), size);
        genLayer(World::TL_DIRT, 2, vec3(0.12f, 0.08f, 0.05f), vec3(0.20f, 0.14f, 0.08f), vec3(0.30f, 0.28f, 0.25f), vec4(0, 0, 0, 7), size);
        genLayer(World::TL_ROCK, 3, vec3(0.18f, 0.17f, 0.15f), vec3(0.30f, 0.28f, 0.25f), vec3(0.22f, 0.18f, 0.14f), vec4(0, 0, 0, 9), size);
        genLayer(World::TL_MUD, 4, vec3(0.05f, 0.04f, 0.025f), vec3(0.09f, 0.07f, 0.045f), vec3(0.04f, 0.04f, 0.035f), vec4(0, 0, 0, 5), size);
        genLayer(World::TL_SAWGRASS, 1, vec3(0.14f, 0.13f, 0.05f), vec3(0.24f, 0.21f, 0.08f), vec3(0.32f, 0.26f, 0.12f), vec4(0.8f, 0, 0, 5), size);
        genLayer(World::TL_FOREST, 5, vec3(0.12f, 0.07f, 0.03f), vec3(0.18f, 0.12f, 0.05f), vec3(0.06f, 0.10f, 0.03f), vec4(0, 0, 0, 6), size);
        genLayer(World::TL_URBAN, 6, vec3(0.20f, 0.19f, 0.18f), vec3(0.32f, 0.31f, 0.29f), vec3(0.10f, 0.10f, 0.10f), vec4(0, 0, 0, 7), size);
        gfx::unbindCSResources(4, 2);
        c->GenerateMips(albedoArr.srv);
        c->GenerateMips(normalArr.srv);
    }

    void setMap(World::WorldMap* m) {
        map = m;
        const int R = World::kHeightRes;
        heightTex.release();
        splat0Tex.release();
        splat1Tex.release();
        waterTex.release();
        heightTex = gfx::createTexture2D(R, R, DXGI_FORMAT_R32_FLOAT, gfx::TEX_SRV, 1, 1, m->height.data(), R * 4);
        splat0Tex = gfx::createTexture2D(R, R, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, m->splat0.data(), R * 4);
        splat1Tex = gfx::createTexture2D(R, R, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, m->splat1.data(), R * 4);
        waterTex = gfx::createTexture2D(R, R, DXGI_FORMAT_R32_FLOAT, gfx::TEX_SRV, 1, 1, m->waterLevel.data(), R * 4);
        // Min/max quadtree
        int texPerLeaf = (int)(leafSize / World::kHeightCell);
        int res0 = R / texPerLeaf;
        for (int l = 0; l < kLevels; l++) {
            levelRes[l] = res0 >> l;
            minmax[l].assign((size_t)levelRes[l] * levelRes[l], vec2(1e9f, -1e9f));
        }
        for (int ny = 0; ny < res0; ny++)
            for (int nx = 0; nx < res0; nx++) {
                vec2 mm(1e9f, -1e9f);
                for (int y = ny * texPerLeaf; y <= Min(R - 1, (ny + 1) * texPerLeaf); y++)
                    for (int x = nx * texPerLeaf; x <= Min(R - 1, (nx + 1) * texPerLeaf); x++) {
                        float h = m->height[(size_t)y * R + x];
                        mm.x = Min(mm.x, h);
                        mm.y = Max(mm.y, h);
                    }
                minmax[0][(size_t)ny * res0 + nx] = mm;
            }
        for (int l = 1; l < kLevels; l++) {
            int r = levelRes[l], pr = levelRes[l - 1];
            for (int y = 0; y < r; y++)
                for (int x = 0; x < r; x++) {
                    vec2 mm(1e9f, -1e9f);
                    for (int k = 0; k < 4; k++) {
                        vec2 c = minmax[l - 1][(size_t)(y * 2 + (k >> 1)) * pr + (x * 2 + (k & 1))];
                        mm.x = Min(mm.x, c.x);
                        mm.y = Max(mm.y, c.y);
                    }
                    minmax[l][(size_t)y * r + x] = mm;
                }
        }
    }

    // Height range for a node (terrain detail + water plane bound)
    AABB nodeBox(int lod, int nx, int ny, dvec3 cam) const {
        float size = leafSize * (float)(1 << lod);
        vec2 mm = minmax[lod][(size_t)ny * levelRes[lod] + nx];
        float x0 = -World::kWorldHalf + nx * size, y0 = -World::kWorldHalf + ny * size;
        return AABB(vec3((float)(x0 - cam.x), (float)(y0 - cam.y), (float)(mm.x - 1.f - cam.z)),
                    vec3((float)(x0 + size - cam.x), (float)(y0 + size - cam.y), (float)(mm.y + 1.f - cam.z)));
    }

    static bool boxInSphere(const AABB& b, float r) {
        vec3 c = vmin(vmax(vec3(0, 0, 0), b.mn), b.mx);
        return length2(c) <= r * r;
    }

    void addQuadrant(int lod, int nx, int ny, int q) {
        if ((int)nodes.size() >= kMaxNodes) return;
        float size = leafSize * (float)(1 << lod);
        float half = size * 0.5f;
        float x0 = -World::kWorldHalf + nx * size + (q & 1) * half;
        float y0 = -World::kWorldHalf + ny * size + (q >> 1) * half;
        nodes.push_back(vec4(x0, y0, half, (float)lod));
    }

    // Returns false if the node is out of its LOD range (parent must cover it)
    bool selectNode(int lod, int nx, int ny, dvec3 cam, const Frustum& fr, float rangeScale) {
        AABB b = nodeBox(lod, nx, ny, cam);
        if (lod < kLevels - 1 && !boxInSphere(b, ranges[lod] * rangeScale)) return false;
        if (!fr.testAABB(b)) return true;
        if (lod == 0 || !boxInSphere(b, ranges[lod - 1] * rangeScale)) {
            for (int q = 0; q < 4; q++) addQuadrant(lod, nx, ny, q);
            return true;
        }
        for (int q = 0; q < 4; q++) {
            int cx = nx * 2 + (q & 1), cy = ny * 2 + (q >> 1);
            if (!selectNode(lod - 1, cx, cy, cam, fr, rangeScale)) addQuadrant(lod, nx, ny, q);
        }
        return true;
    }

    void select(dvec3 cam, const Frustum& fr, float rangeScale) {
        nodes.clear();
        int top = kLevels - 1;
        for (int y = 0; y < levelRes[top]; y++)
            for (int x = 0; x < levelRes[top]; x++) selectNode(top, x, y, cam, fr, rangeScale);
    }

    void uploadNodes(float rangeScale) {
        drawnNodes = (int)nodes.size();
        if (drawnNodes == 0) return;
        gfx::updateBuffer(nodeBuf, nodes.data(), (u32)(nodes.size() * 16));
        cb.data.params = vec4(World::kWorldHalf, World::kHeightCell, (float)World::kHeightRes, (float)gridM);
        for (int l = 0; l < kLevels; l++) {
            float lo = l > 0 ? ranges[l - 1] * rangeScale : 0.f;
            float hi = ranges[l] * rangeScale;
            if (l == kLevels - 1) { lo = 1e8f; hi = 1e8f + 1.f; }
            cb.data.morph[l] = vec4(Lerp(lo, hi, 0.62f), hi * 0.96f, leafSize * (float)(1 << l), 0);
        }
        cb.upload();
    }

    void bindCommon() {
        auto* c = gfx::ctx;
        UINT stride = 8, offset = 0;
        c->IASetVertexBuffers(0, 1, &vb.buf, &stride, &offset);
        c->IASetIndexBuffer(ib.buf, DXGI_FORMAT_R16_UINT, 0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* cbs[] = {cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        ID3D11ShaderResourceView* srvs[7] = {heightTex.srv, splat0Tex.srv, splat1Tex.srv, albedoArr.srv, normalArr.srv, waterTex.srv, nodeBuf.srv};
        c->VSSetShaderResources(0, 7, srvs);
        c->PSSetShaderResources(0, 7, srvs);
    }

    void drawGBuffer(Renderer& r) { drawGBufferVP(r, r.viewProjNoJitter, r.camera.pos, 1.f, true); }

    // G-buffer pass for an arbitrary view (reflection probe faces): vp is relative to refPos, which must also be
    // the gCamPos of the bound frame constants. rangeScale > 1 selects coarser LODs.
    void drawGBufferVP(Renderer& r, const mat4& vp, dvec3 refPos, float rangeScale, bool mainView = false) {
        if (!map) return;
        Frustum fr;
        fr.fromMatrix(vp);
        select(refPos, fr, rangeScale);
        uploadNodes(rangeScale);
        if (!drawnNodes) return;
        auto* c = gfx::ctx;
        bindCommon();
        c->IASetInputLayout(vs.layout);
        c->VSSetShader(vs.vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->DrawIndexedInstanced((UINT)indexCount, (UINT)drawnNodes, 0, 0, 0);
        r.stats.drawCalls++;
        if (mainView) {
            r.stats.terrainNodes += drawnNodes;
            r.stats.triangles += drawnNodes * indexCount / 3;
        }
        ID3D11ShaderResourceView* nulls[7] = {};
        c->VSSetShaderResources(0, 7, nulls);
        c->PSSetShaderResources(0, 7, nulls);
    }

    void drawShadow(Renderer& r, const mat4& lightVP, dvec3 cam) {
        if (!map) return;
        Frustum fr;
        fr.fromMatrix(lightVP);
        // Coarser LOD in shadows
        select(cam, fr, 1.6f);
        uploadNodes(1.6f);
        if (!drawnNodes) return;
        auto* c = gfx::ctx;
        bindCommon();
        c->IASetInputLayout(vsShadow.layout);
        c->VSSetShader(vsShadow.vs, nullptr, 0);
        c->PSSetShader(nullptr, nullptr, 0);
        c->DrawIndexedInstanced((UINT)indexCount, (UINT)drawnNodes, 0, 0, 0);
        r.stats.drawCalls++;
        ID3D11ShaderResourceView* nulls[7] = {};
        c->VSSetShaderResources(0, 7, nulls);
    }
};

}  // namespace Render
