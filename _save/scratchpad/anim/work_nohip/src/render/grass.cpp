// GPU-driven grass and ground cover: compute placement from the terrain splat / heightmap / overhead map into
// two LOD append buffers (near detailed clumps, far wide clumps), drawn with DrawInstancedIndirect.
// Included from renderer.cpp.
namespace Render {

struct GrassInstanceGPU {
    vec3 rel;
    float yaw;
    float height, width;
    u32 colorType;
    float seed;
};
static_assert(sizeof(GrassInstanceGPU) == 32, "GrassInstanceGPU must match grass.hlsl");

struct GrassCBData {
    vec4 g0, g1, g2, g3;
    vec4 frustum[6];
};

struct GrassSystem {
    struct Lod {
        gfx::Buffer instances;   // append buffer
        gfx::Buffer args;        // DrawInstancedIndirect arguments
        int capacity = 0;
        int blades = 0, segments = 0;
        float cell = 0.f, rIn = 0.f, rOut = 0.f;
    };
    Lod lods[2];
    gfx::CBuffer<GrassCBData> cb;
    ID3D11ComputeShader* csPlace = nullptr;
    gfx::VertexShader vs;
    ID3D11PixelShader* ps = nullptr;
    float time = 0.f, prevTime = 0.f;
    int quality = -1;
    float distance = 0.f;

    void init() {
        csPlace = gfx::loadCS("grass.hlsl", "csGrassPlace");
        vs = gfx::loadVS("grass.hlsl", "vsGrass", nullptr, 0);
        ps = gfx::loadPS("grass.hlsl", "psGrass");
        cb.create();
    }

    void configure(int q, float dist) {
        if (q == quality && dist == distance) return;
        quality = q;
        distance = dist;
        static const float nearCell[4] = {0, 0.75f, 0.6f, 0.5f}, farCell[4] = {0, 1.6f, 1.3f, 1.1f};
        static const int nearBlades[4] = {0, 6, 8, 10}, farBlades[4] = {0, 4, 5, 6};
        static const int nearSegs[4] = {0, 3, 3, 4};
        float nearR = Min(dist * 0.35f, 30.f);
        lods[0].cell = nearCell[q];
        lods[0].rIn = 0.f;
        lods[0].rOut = nearR;
        lods[0].blades = nearBlades[q];
        lods[0].segments = nearSegs[q];
        lods[1].cell = farCell[q];
        lods[1].rIn = nearR - 5.f;
        lods[1].rOut = dist;
        lods[1].blades = farBlades[q];
        lods[1].segments = 2;
        for (Lod& l : lods) {
            float r = l.rOut;
            int cells = (int)ceilf(2.f * r / l.cell) + 1;
            int cap = Min(cells * cells, 262144);
            if (cap != l.capacity) {
                l.instances.release();
                l.args.release();
                l.capacity = cap;
                l.instances = gfx::createBuffer((u32)(cap * sizeof(GrassInstanceGPU)), sizeof(GrassInstanceGPU),
                                                gfx::BUF_STRUCTURED | gfx::BUF_UAV | gfx::BUF_APPEND);
            }
            u32 vpb = (u32)((l.segments - 1) * 6 + 3);
            u32 init[4] = {vpb * (u32)l.blades, 0, 0, 0};
            l.args.release();
            l.args = gfx::createBuffer(16, 4, gfx::BUF_INDIRECT, init);
        }
    }

    // Placement (compute) for this frame's camera. Needs the overhead map and terrain textures.
    void place(Renderer& r, TerrainRenderer& t, float dt) {
        const Settings& s = r.settings;
        int q = Clamp(s.grassQuality, 0, 3);
        if (q == 0 || !t.map) return;
        configure(q, Clamp(s.grassDistance, 20.f, 150.f));
        prevTime = time;
        time += dt;
        auto* c = gfx::ctx;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        for (int i = 0; i < 6; i++) cb.data.frustum[i] = fr.planes[i];
        dvec3 cam = r.camera.pos;
        for (int li = 0; li < 2; li++) {
            Lod& l = lods[li];
            double cellD = l.cell;
            double gx = floor((cam.x - l.rOut) / cellD) * cellD, gy = floor((cam.y - l.rOut) / cellD) * cellD;
            int n = (int)ceil(2.0 * l.rOut / cellD) + 1;
            cb.data.g0 = vec4((float)gx, (float)gy, l.cell, (float)n);
            cb.data.g1 = vec4(l.rIn, l.rOut, l.rOut * 0.78f, li == 0 ? 1.f : 0.75f);
            cb.data.g2 = vec4(0.35f + r.frame.weather.w * 0.9f + r.frame.weather.x * 0.4f, time, prevTime, (float)l.blades);
            cb.data.g3 = vec4((float)l.segments, (float)li, World::kWorldHalf, 0);
            cb.upload();
            ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
            c->CSSetConstantBuffers(0, 2, cbs);
            ID3D11ShaderResourceView* srvs[4] = {t.splat0Tex.srv, t.splat1Tex.srv, nullptr, r.weather->overheadValid ? r.weather->overheadGrass.srv : nullptr};
            c->CSSetShaderResources(0, 4, srvs);
            UINT zero = 0;
            c->CSSetUnorderedAccessViews(0, 1, &l.instances.uav, &zero);
            c->CSSetShader(csPlace, nullptr, 0);
            c->Dispatch(gfx::divUp(n, 8), gfx::divUp(n, 8), 1);
            ID3D11UnorderedAccessView* nu = nullptr;
            c->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
            c->CopyStructureCount(l.args.buf, 4, l.instances.uav);
        }
        gfx::unbindCSResources(4, 1);
    }

    // G-buffer pass (render targets and depth already bound).
    void draw(Renderer& r) {
        if (quality <= 0 || r.settings.grassQuality <= 0 || !r.terrain->map) return;
        auto* c = gfx::ctx;
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->RSSetState(gfx::states.cullNone);
        for (int li = 0; li < 2; li++) {
            Lod& l = lods[li];
            cb.data.g2 = vec4(0.35f + r.frame.weather.w * 0.9f + r.frame.weather.x * 0.4f, time, prevTime, (float)l.blades);
            cb.data.g3 = vec4((float)l.segments, (float)li, World::kWorldHalf, 0);
            cb.upload();
            ID3D11Buffer* cbs[] = {cb.get()};
            c->VSSetConstantBuffers(1, 1, cbs);
            c->PSSetConstantBuffers(1, 1, cbs);
            c->VSSetShaderResources(2, 1, &l.instances.srv);
            c->DrawInstancedIndirect(l.args.buf, 0);
            r.stats.drawCalls++;
        }
        ID3D11ShaderResourceView* nul = nullptr;
        c->VSSetShaderResources(2, 1, &nul);
        c->RSSetState(gfx::states.cullBack);
    }
};

}  // namespace Render
