// Deferred decals (bullet holes, blood, scorch marks) and tire skid strips, blended into the G-buffer after the
// geometry pass (before AO / reflections / lighting). Included from renderer.cpp.
#include <deque>
#include <unordered_map>

namespace Render {

struct DecalRequest {
    DecalType type;
    dvec3 pos;
    vec3 normal;
    float size, angle;
};
struct SkidRequest {
    int track;
    dvec3 pos;
    vec3 normal;
    float width, intensity;
};

struct DecalInstanceGPU {
    vec4 center, axisX, axisY, axisZ;
};

struct SkidVertex {
    vec3 pos;
    vec4 uvA;
};

struct DecalSystem {
    struct Decal {
        DecalType type;
        dvec3 pos;
        vec3 n, t;
        float size, depth, age, life;
        u32 cell;
    };
    struct SkidPoint {
        int track;
        dvec3 pos;
        vec3 normal;
        float width, intensity, age, dist;
        long long seq;       // global sequence number of this point
        long long prevSeq;   // previous point of the same strip (-1: strip start)
    };
    std::vector<DecalRequest> requests;
    std::vector<SkidRequest> skids;
    std::vector<Decal> decals;
    std::deque<SkidPoint> skidPoints;
    struct TrackState {
        dvec3 last;
        float dist;
        float lastTime;
        long long lastSeq;
    };
    long long nextSeq = 0;
    std::unordered_map<int, TrackState> tracks;
    float time = 0.f;
    static const int kMaxSkidPoints = 8192;

    gfx::Texture albedoAtlas, normalAtlas, normalCopy, materialCopy;
    gfx::Buffer instBuf, skidVB;
    int instCap = 0;
    static const int kMaxSkidVerts = kMaxSkidPoints * 6;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* dsBack = nullptr;
    ID3D11RasterizerState* rsSkid = nullptr;
    gfx::VertexShader vs, vsSkid;
    ID3D11PixelShader *ps = nullptr, *psSkid = nullptr;
    std::vector<DecalInstanceGPU> visible;
    std::vector<SkidVertex> skidVerts;

    void queue(const DecalRequest& d) {
        if (requests.size() < 1024) requests.push_back(d);
    }
    void queueSkid(const SkidRequest& s) {
        if (skids.size() < 1024) skids.push_back(s);
    }

    void init() {
        vs = gfx::loadVS("decals.hlsl", "vsDecal", nullptr, 0);
        ps = gfx::loadPS("decals.hlsl", "psDecal");
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        vsSkid = gfx::loadVS("decals.hlsl", "vsSkid", layout, 2);
        psSkid = gfx::loadPS("decals.hlsl", "psSkid");
        skidVB = gfx::createBuffer(kMaxSkidVerts * sizeof(SkidVertex), sizeof(SkidVertex), gfx::BUF_VERTEX | gfx::BUF_DYNAMIC);
        // Atlases (4x4 cells of 256x256), generated once
        albedoAtlas = gfx::createTexture2D(1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_GENMIPS, 0, 1);
        normalAtlas = gfx::createTexture2D(1024, 1024, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_GENMIPS, 0, 1);
        ID3D11ComputeShader* gen = gfx::loadCS("decals.hlsl", "csGenDecals");
        auto* c = gfx::ctx;
        ID3D11UnorderedAccessView* uavs[2] = {albedoAtlas.uav, normalAtlas.uav};
        c->CSSetShader(gen, nullptr, 0);
        c->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        c->Dispatch(1024 / 8, 1024 / 8, 1);
        gfx::unbindCSResources(1, 2);
        c->GenerateMips(albedoAtlas.srv);
        c->GenerateMips(normalAtlas.srv);
        gen->Release();
        // Blend: albedo / normal / roughness+metal lerp by alpha, emissive additive
        D3D11_BLEND_DESC bd = {};
        bd.IndependentBlendEnable = TRUE;
        for (int i = 0; i < 3; i++) {
            D3D11_RENDER_TARGET_BLEND_DESC& t = bd.RenderTarget[i];
            t.BlendEnable = TRUE;
            t.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO;
            t.DestBlendAlpha = D3D11_BLEND_ONE;
            t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        bd.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN;
        bd.RenderTarget[2].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN;
        D3D11_RENDER_TARGET_BLEND_DESC& e = bd.RenderTarget[3];
        e.BlendEnable = TRUE;
        e.SrcBlend = D3D11_BLEND_ONE;
        e.DestBlend = D3D11_BLEND_ONE;
        e.BlendOp = D3D11_BLEND_OP_ADD;
        e.SrcBlendAlpha = D3D11_BLEND_ZERO;
        e.DestBlendAlpha = D3D11_BLEND_ONE;
        e.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        e.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        gfx::dev->CreateBlendState(&bd, &blend);
        D3D11_DEPTH_STENCIL_DESC ds = {};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;  // box back faces behind the scene surface (reversed Z)
        gfx::dev->CreateDepthStencilState(&ds, &dsBack);
        D3D11_RASTERIZER_DESC rs = {};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.FrontCounterClockwise = TRUE;
        rs.DepthClipEnable = TRUE;
        rs.DepthBias = 16;
        rs.SlopeScaledDepthBias = 2.f;
        gfx::dev->CreateRasterizerState(&rs, &rsSkid);
    }

    void resize(int w, int h) {
        normalCopy.release();
        materialCopy.release();
        normalCopy = gfx::createTexture2D(w, h, DXGI_FORMAT_R16G16_UNORM, gfx::TEX_SRV);
        materialCopy = gfx::createTexture2D(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV);
    }

    void addDecal(const DecalRequest& q, int maxDecals) {
        Decal d;
        d.type = q.type;
        d.pos = q.pos;
        d.n = q.normal;
        vec3 ref = fabsf(d.n.z) < 0.9f ? vec3(0, 0, 1) : vec3(1, 0, 0);
        vec3 t0 = normalize(cross(ref, d.n));
        vec3 b0 = cross(d.n, t0);
        d.t = t0 * cosf(q.angle) + b0 * sinf(q.angle);
        d.size = Clamp(q.size, 0.02f, 20.f);
        d.depth = Max(0.08f, d.size * 0.4f);
        d.age = 0.f;
        u32 h = hash32((u32)(q.pos.x * 131.0) ^ hash32((u32)(q.pos.y * 71.0) + (u32)(q.pos.z * 29.0)));
        switch (q.type) {
            case DECAL_BULLET_CONCRETE: d.cell = 0 + (h & 1u); d.life = 240.f; d.size *= 1.6f; break;
            case DECAL_BULLET_METAL: d.cell = 2 + (h & 1u); d.life = 240.f; d.size *= 1.4f; break;
            case DECAL_BULLET_GLASS: d.cell = 4 + (h & 1u); d.life = 240.f; d.size *= 3.f; break;
            case DECAL_BLOOD: d.cell = 6 + (h & 3u); d.life = 180.f; break;
            case DECAL_SCORCH: d.cell = 10 + (h & 1u); d.life = 300.f; d.depth = Max(d.depth, 1.0f); break;
            case DECAL_BLOOD_POOL: d.cell = 12; d.life = 180.f; break;
            default: d.cell = 0; d.life = 120.f; break;
        }
        if ((int)decals.size() >= maxDecals) decals.erase(decals.begin());
        decals.push_back(d);
    }

    void addSkid(const SkidRequest& s) {
        auto it = tracks.find(s.track);
        long long prev = -1;
        float dist = 0.f;
        if (it != tracks.end()) {
            float gap = length(rel(s.pos, it->second.last));
            if (gap < 0.12f) return;  // too close to the last point: wait for the wheel to move
            if (gap < 3.f && time - it->second.lastTime < 0.35f) {
                prev = it->second.lastSeq;
                dist = it->second.dist + gap;
            }
        }
        SkidPoint p;
        p.track = s.track;
        p.pos = s.pos;
        p.normal = s.normal;
        p.width = Clamp(s.width, 0.05f, 1.f);
        p.intensity = s.intensity;
        p.age = 0.f;
        p.dist = dist;
        p.seq = nextSeq++;
        p.prevSeq = prev;
        tracks[s.track] = {s.pos, dist, time, p.seq};
        skidPoints.push_back(p);
        while ((int)skidPoints.size() > kMaxSkidPoints) skidPoints.pop_front();
    }

    void update(Renderer& r, float dt) {
        time += dt;
        int maxDecals = Clamp(r.settings.maxDecals, 16, 4096);
        for (const DecalRequest& q : requests) addDecal(q, maxDecals);
        for (const SkidRequest& s : skids) addSkid(s);
        requests.clear();
        skids.clear();
        for (size_t i = 0; i < decals.size();) {
            decals[i].age += dt;
            if (decals[i].age > decals[i].life) decals.erase(decals.begin() + (long)i);
            else i++;
        }
        for (SkidPoint& p : skidPoints) p.age += dt;
        while (!skidPoints.empty() && skidPoints.front().age > 150.f) skidPoints.pop_front();
        if (tracks.size() > 256) {
            for (auto it = tracks.begin(); it != tracks.end();) {
                if (time - it->second.lastTime > 2.f) it = tracks.erase(it);
                else ++it;
            }
        }
    }

    // Blends decals and skid marks into the G-buffer. Depth is bound read-only (depth test + SRV).
    void render(Renderer& r) {
        dvec3 cam = r.camera.pos;
        Frustum fr;
        fr.fromMatrix(r.viewProjNoJitter);
        visible.clear();
        for (const Decal& d : decals) {
            vec3 c = rel(d.pos, cam);
            float radius = d.size * 1.5f + d.depth;
            if (length2(c) > 250.f * 250.f || !fr.testSphere(c, radius)) continue;
            vec3 b = cross(d.n, d.t);
            float fade = Saturate((d.life - d.age) / 4.f) * Saturate(d.age * 20.f + 0.3f);
            float glow = d.type == DECAL_SCORCH ? 4000.f * Saturate(1.f - d.age / 6.f) * Saturate(1.f - d.age / 6.f) : 0.f;
            DecalInstanceGPU g;
            g.center = vec4(c, (float)d.cell);
            g.axisX = vec4(d.t * (d.size * 0.5f), fade);
            g.axisY = vec4(b * (d.size * 0.5f), glow);
            g.axisZ = vec4(d.n * d.depth, 0);
            visible.push_back(g);
        }
        buildSkidVerts(cam, fr);
        if (visible.empty() && skidVerts.empty()) return;
        auto* c = gfx::ctx;
        // Copies of normal/material: the decal shader reads the underlying surface while blending into it
        if (!visible.empty()) {
            c->CopyResource(normalCopy.res, r.gbNormal.res);
            c->CopyResource(materialCopy.res, r.gbMaterial.res);
        }
        ID3D11RenderTargetView* rts[4] = {r.gbAlbedo.rtv, r.gbNormal.rtv, r.gbMaterial.rtv, r.gbEmissive.rtv};
        c->OMSetRenderTargets(4, rts, r.depthRO);
        gfx::setViewport((float)r.width, (float)r.height);
        float bf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(blend, bf, 0xffffffff);
        ID3D11Buffer* cbs[] = {r.frameCB.get()};
        c->VSSetConstantBuffers(0, 1, cbs);
        c->PSSetConstantBuffers(0, 1, cbs);
        if (!visible.empty()) {
            if ((int)visible.size() > instCap) {
                instBuf.release();
                instCap = Max(64, (int)visible.size() * 2);
                instBuf = gfx::createBuffer((u32)(instCap * sizeof(DecalInstanceGPU)), sizeof(DecalInstanceGPU), gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
            }
            gfx::updateBuffer(instBuf, visible.data(), (u32)(visible.size() * sizeof(DecalInstanceGPU)));
            ID3D11ShaderResourceView* srvs[6] = {instBuf.srv, r.depth.srv, normalCopy.srv, materialCopy.srv, albedoAtlas.srv, normalAtlas.srv};
            c->VSSetShaderResources(0, 1, srvs);
            c->PSSetShaderResources(0, 6, srvs);
            c->OMSetDepthStencilState(dsBack, 0);
            c->RSSetState(gfx::states.cullFront);
            c->IASetInputLayout(nullptr);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c->VSSetShader(vs.vs, nullptr, 0);
            c->PSSetShader(ps, nullptr, 0);
            c->DrawInstanced(36, (UINT)visible.size(), 0, 0);
            r.stats.drawCalls++;
        }
        if (!skidVerts.empty()) {
            gfx::updateBuffer(skidVB, skidVerts.data(), (u32)(skidVerts.size() * sizeof(SkidVertex)));
            UINT stride = sizeof(SkidVertex), offset = 0;
            c->IASetVertexBuffers(0, 1, &skidVB.buf, &stride, &offset);
            c->IASetInputLayout(vsSkid.layout);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c->OMSetDepthStencilState(gfx::states.depthGreaterEqualNoWrite, 0);
            c->RSSetState(rsSkid);
            c->VSSetShader(vsSkid.vs, nullptr, 0);
            c->PSSetShader(psSkid, nullptr, 0);
            c->Draw((UINT)skidVerts.size(), 0);
            r.stats.drawCalls++;
        }
        c->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* nulls[6] = {};
        c->VSSetShaderResources(0, 6, nulls);
        c->PSSetShaderResources(0, 6, nulls);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->OMSetDepthStencilState(gfx::states.depthGreaterWrite, 0);
        c->RSSetState(gfx::states.cullBack);
    }

    void buildSkidVerts(dvec3 cam, const Frustum& fr) {
        skidVerts.clear();
        size_t n = skidPoints.size();
        if (n == 0) return;
        long long base = skidPoints.front().seq;  // sequence numbers are contiguous in the deque
        for (size_t i = 0; i < n; i++) {
            const SkidPoint& b = skidPoints[i];
            if (b.prevSeq < base) continue;  // strip start, or its predecessor already expired
            const SkidPoint& a = skidPoints[(size_t)(b.prevSeq - base)];
            vec3 pa = rel(a.pos, cam), pb = rel(b.pos, cam);
            if (length2(pa) > 200.f * 200.f) continue;
            vec3 mid = (pa + pb) * 0.5f;
            if (!fr.testSphere(mid, length(pb - pa) + 1.f)) continue;
            vec3 f = pb - pa;
            float fl = length(f);
            if (fl < 1e-3f) continue;
            f /= fl;
            vec3 sa = normalize(cross(a.normal, f)) * (a.width * 0.5f);
            vec3 sb = normalize(cross(b.normal, f)) * (b.width * 0.5f);
            vec3 la = pa + a.normal * 0.015f, lb = pb + b.normal * 0.015f;
            float fadeA = a.intensity * Saturate((150.f - a.age) / 30.f);
            float fadeB = b.intensity * Saturate((150.f - b.age) / 30.f);
            SkidVertex v0{la - sa, vec4(a.dist, -1, fadeA, 0)}, v1{la + sa, vec4(a.dist, 1, fadeA, 0)};
            SkidVertex v2{lb - sb, vec4(b.dist, -1, fadeB, 0)}, v3{lb + sb, vec4(b.dist, 1, fadeB, 0)};
            skidVerts.push_back(v0);
            skidVerts.push_back(v2);
            skidVerts.push_back(v1);
            skidVerts.push_back(v1);
            skidVerts.push_back(v2);
            skidVerts.push_back(v3);
            if ((int)skidVerts.size() + 6 > kMaxSkidVerts) break;
        }
    }
};

void Renderer::addDecal(DecalType type, dvec3 pos, vec3 normal, float size, float angle) {
    if (!decals || (int)type < 0 || (int)type >= DECAL_COUNT || size <= 0.f) return;
    float l = length(normal);
    decals->queue({type, pos, l > 1e-4f ? normal / l : vec3(0, 0, 1), size, angle});
}
void Renderer::addSkidMark(int trackId, dvec3 pos, vec3 normal, float width, float intensity) {
    if (!decals) return;
    float l = length(normal);
    decals->queueSkid({trackId, pos, l > 1e-4f ? normal / l : vec3(0, 0, 1), width, Saturate(intensity)});
}

}  // namespace Render
