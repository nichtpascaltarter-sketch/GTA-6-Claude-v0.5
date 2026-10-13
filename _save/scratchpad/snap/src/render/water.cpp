// Water rendering (CDLOD grid aligned to the world + ocean skirt). Included from renderer.cpp.
namespace Render {

struct WaterCBData {
    vec4 params;
    vec4 morph[16];
    vec4 mode;
    vec4 reflection;  // x screen-space reflections enabled, y max HiZ iterations, z HiZ max mip
};

struct WaterRenderer {
    gfx::VertexShader vs;
    ID3D11PixelShader* ps = nullptr;
    ID3D11ComputeShader* csWave = nullptr;
    gfx::Texture waveTex;
    gfx::Buffer nodeBuf, skirtVB, skirtIB;
    int skirtIndexCount = 0;
    gfx::CBuffer<WaterCBData> cb;
    ID3D11BlendState* blend = nullptr;
    std::vector<vec2> waterMax[TerrainRenderer::kLevels];  // x: max water level in node, y: unused
    std::vector<vec4> nodes;
    int drawn = 0;

    void init() {
        D3D11_INPUT_ELEMENT_DESC layout[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        vs = gfx::loadVS("water.hlsl", "vsWater", layout, 1);
        ps = gfx::loadPS("water.hlsl", "psWater");
        csWave = gfx::loadCS("water.hlsl", "csWaveNormals");
        cb.create();
        nodeBuf = gfx::createBuffer(TerrainRenderer::kMaxNodes * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        waveTex = gfx::createTexture2D(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_GENMIPS, 0, 1);
        gfx::ctx->CSSetShader(csWave, nullptr, 0);
        gfx::ctx->CSSetUnorderedAccessViews(0, 1, &waveTex.uav, nullptr);
        gfx::ctx->Dispatch(64, 64, 1);
        gfx::unbindCSResources(1, 1);
        gfx::ctx->GenerateMips(waveTex.srv);
        // Ocean skirt ring: 256 angular x 24 radial
        std::vector<vec2> v;
        std::vector<u32> idx;
        const int A = 256, Rn = 24;
        for (int r = 0; r <= Rn; r++)
            for (int a = 0; a <= A; a++) v.push_back(vec2((float)a / A, (float)r / Rn));
        for (int r = 0; r < Rn; r++)
            for (int a = 0; a < A; a++) {
                u32 i0 = r * (A + 1) + a, i1 = i0 + 1, i2 = i0 + (A + 1), i3 = i2 + 1;
                idx.insert(idx.end(), {i0, i2, i1, i1, i2, i3});
            }
        skirtIndexCount = (int)idx.size();
        skirtVB = gfx::createBuffer((u32)(v.size() * 8), 8, gfx::BUF_VERTEX, v.data());
        skirtIB = gfx::createBuffer((u32)(idx.size() * 4), 4, gfx::BUF_INDEX, idx.data());
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        gfx::dev->CreateBlendState(&bd, &blend);
    }

    void setMap(const World::WorldMap& m, TerrainRenderer& t) {
        const int R = World::kHeightRes;
        int texPerLeaf = (int)(t.leafSize / World::kHeightCell);
        int res0 = R / texPerLeaf;
        for (int l = 0; l < TerrainRenderer::kLevels; l++) waterMax[l].assign((size_t)t.levelRes[l] * t.levelRes[l], vec2(-1e9f, 0));
        for (int ny = 0; ny < res0; ny++)
            for (int nx = 0; nx < res0; nx++) {
                float mx = -1e9f;
                for (int y = ny * texPerLeaf; y <= Min(R - 1, (ny + 1) * texPerLeaf); y++)
                    for (int x = nx * texPerLeaf; x <= Min(R - 1, (nx + 1) * texPerLeaf); x++) {
                        float w = m.waterLevel[(size_t)y * R + x];
                        if (w > World::kNoWater + 1.f) mx = Max(mx, w);
                    }
                waterMax[0][(size_t)ny * res0 + nx] = vec2(mx, 0);
            }
        for (int l = 1; l < TerrainRenderer::kLevels; l++) {
            int r = t.levelRes[l], pr = t.levelRes[l - 1];
            for (int y = 0; y < r; y++)
                for (int x = 0; x < r; x++) {
                    float mx = -1e9f;
                    for (int k = 0; k < 4; k++) mx = Max(mx, waterMax[l - 1][(size_t)(y * 2 + (k >> 1)) * pr + (x * 2 + (k & 1))].x);
                    waterMax[l][(size_t)y * r + x] = vec2(mx, 0);
                }
        }
    }

    bool selectNode(TerrainRenderer& t, int lod, int nx, int ny, dvec3 cam, const Frustum& fr) {
        float wmax = waterMax[lod][(size_t)ny * t.levelRes[lod] + nx].x;
        vec2 mm = t.minmax[lod][(size_t)ny * t.levelRes[lod] + nx];
        float size = t.leafSize * (float)(1 << lod);
        float x0 = -World::kWorldHalf + nx * size, y0 = -World::kWorldHalf + ny * size;
        AABB b(vec3((float)(x0 - cam.x), (float)(y0 - cam.y), (float)(-3.f - cam.z)),
               vec3((float)(x0 + size - cam.x), (float)(y0 + size - cam.y), (float)(Max(wmax, 0.f) + 3.f - cam.z)));
        if (lod < TerrainRenderer::kLevels - 1 && !TerrainRenderer::boxInSphere(b, t.ranges[lod])) return false;
        if (wmax < -1e8f || mm.x > wmax + 0.5f) return true;  // no water in this node
        if (!fr.testAABB(b)) return true;
        auto emit = [&](int q) {
            if ((int)nodes.size() >= TerrainRenderer::kMaxNodes) return;
            float half = size * 0.5f;
            nodes.push_back(vec4(x0 + (q & 1) * half, y0 + (q >> 1) * half, half, (float)lod));
        };
        if (lod == 0 || !TerrainRenderer::boxInSphere(b, t.ranges[lod - 1])) {
            for (int q = 0; q < 4; q++) emit(q);
            return true;
        }
        for (int q = 0; q < 4; q++)
            if (!selectNode(t, lod - 1, nx * 2 + (q & 1), ny * 2 + (q >> 1), cam, fr)) emit(q);
        return true;
    }

    void draw(Renderer& r, TerrainRenderer& t, ID3D11ShaderResourceView* sceneColor, ID3D11ShaderResourceView* sceneDepth, float waveStrength,
              ID3D11ShaderResourceView* hiz, int hizMips) {
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        nodes.clear();
        int top = TerrainRenderer::kLevels - 1;
        for (int y = 0; y < t.levelRes[top]; y++)
            for (int x = 0; x < t.levelRes[top]; x++) selectNode(t, top, x, y, r.camera.pos, fr);
        drawn = (int)nodes.size();
        cb.data.params = vec4(World::kWorldHalf, World::kHeightCell, (float)t.gridM, waveStrength);
        for (int l = 0; l < TerrainRenderer::kLevels; l++) {
            float lo = l > 0 ? t.ranges[l - 1] : 0.f, hi = t.ranges[l];
            if (l == TerrainRenderer::kLevels - 1) { lo = 1e8f; hi = 1e8f + 1.f; }
            cb.data.morph[l] = vec4(Lerp(lo, hi, 0.62f), hi * 0.96f, 0, 0);
        }
        cb.data.mode = vec4(0, World::kWorldHalf, 45000.f, 1.f);
        bool ssr = r.settings.waterSSR && hiz;
        cb.data.reflection = vec4(ssr ? 1.f : 0.f, r.settings.ssrQuality >= 3 ? 64.f : 40.f, (float)(hizMips - 1), 0.f);
        cb.upload();
        ID3D11Buffer* cbs[] = {cb.get()};
        c->VSSetConstantBuffers(1, 1, cbs);
        c->PSSetConstantBuffers(1, 1, cbs);
        ID3D11Buffer* scb[] = {r.shadowCB.get()};
        c->PSSetConstantBuffers(3, 1, scb);
        ID3D11ShaderResourceView* srvs[7] = {t.heightTex.srv, t.waterTex.srv, sceneColor, sceneDepth, waveTex.srv, hiz, nodeBuf.srv};
        c->VSSetShaderResources(0, 7, srvs);
        c->PSSetShaderResources(0, 7, srvs);
        c->IASetInputLayout(vs.layout);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->OMSetBlendState(blend, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullNone);
        if (drawn > 0) {
            gfx::updateBuffer(nodeBuf, nodes.data(), (u32)(nodes.size() * 16));
            UINT stride = 8, offset = 0;
            c->IASetVertexBuffers(0, 1, &t.vb.buf, &stride, &offset);
            c->IASetIndexBuffer(t.ib.buf, DXGI_FORMAT_R16_UINT, 0);
            c->DrawIndexedInstanced((UINT)t.indexCount, (UINT)drawn, 0, 0, 0);
            r.stats.drawCalls++;
        }
        // Ocean skirt beyond the world square
        cb.data.mode.x = 1.f;
        cb.upload();
        UINT stride = 8, offset = 0;
        c->IASetVertexBuffers(0, 1, &skirtVB.buf, &stride, &offset);
        c->IASetIndexBuffer(skirtIB.buf, DXGI_FORMAT_R32_UINT, 0);
        c->DrawIndexed((UINT)skirtIndexCount, 0, 0);
        r.stats.drawCalls++;
        ID3D11ShaderResourceView* nulls[7] = {};
        c->VSSetShaderResources(0, 7, nulls);
        c->PSSetShaderResources(0, 7, nulls);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullBack);
    }
};

}  // namespace Render
