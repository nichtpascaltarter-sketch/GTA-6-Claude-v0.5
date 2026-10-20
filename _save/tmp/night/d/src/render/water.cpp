// Water rendering (CDLOD grid aligned to the world + ocean skirt) and boat wakes. Included from renderer.cpp.
#include <deque>
#include <unordered_map>
namespace Render {

struct WaterCBData {
    vec4 params;
    vec4 morph[16];
    vec4 mode;
    vec4 reflection;  // x screen-space reflections enabled, y max HiZ iterations, z HiZ max mip
    vec4 wake;        // xy wake map origin (world), z size (m), w wakes present
};

// Boat wake trail: the live bow position plus a history of points (newest first).
struct WakePoint {
    dvec3 pos;   // z unused
    float time;
    float speed;
};
struct WakeTrail {
    std::deque<WakePoint> pts;
    dvec3 head;  // z unused
    vec2 heading = vec2(0, 1);
    float speed = 0.f, beam = 1.f, lastUpdate = 0.f;
};
struct WakeVtx {
    vec4 pos;    // map-local xy (m), signed lateral offset (m), distance behind the boat (m)
    vec4 info;   // age (s), wedge half-width (m), amplitude, beam (m)
    vec2 world;  // world xy (wrapped) for noise
};

struct WaterRenderer {
    gfx::VertexShader vs;
    gfx::PixelShader  ps = nullptr;
    gfx::ComputeShader  csWave = nullptr;
    gfx::Texture waveTex;
    gfx::Buffer nodeBuf, skirtVB, skirtIB;
    int skirtIndexCount = 0;
    gfx::CBuffer<WaterCBData> cb;
    gfx::BlendState  blend = nullptr;
    std::vector<vec2> waterMax[TerrainRenderer::kLevels];  // x: max water level in node, y: unused
    std::vector<vec4> nodes;
    int drawn = 0;
    // Boat wakes: trails from Renderer::addWake, rendered each frame into a 512^2 map around the camera
    static constexpr int kWakeRes = 512, kMaxWakeVerts = 16384;
    static constexpr float kWakeSize = 320.f;
    std::unordered_map<int, WakeTrail> trails;
    std::vector<WakeVtx> wakeVerts;
    gfx::Texture wakeTex;
    gfx::Buffer wakeVB;
    gfx::VertexShader vsWake;
    gfx::PixelShader  psWake = nullptr;
    gfx::BlendState  wakeBlend = nullptr;
    dvec3 wakeOrigin;   // z unused
    bool wakeActive = false;

    void init() {
        gfx::InputElement layout[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, gfx::PER_VERTEX, 0}};
        vs = gfx::loadVS("water.hlsl", "vsWater", layout, 1);
        ps = gfx::loadPS("water.hlsl", "psWater");
        csWave = gfx::loadCS("water.hlsl", "csWaveNormals");
        cb.create();
        nodeBuf = gfx::createBuffer(TerrainRenderer::kMaxNodes * 16, 16, gfx::BUF_STRUCTURED | gfx::BUF_DYNAMIC);
        waveTex = gfx::createTexture2D(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV | gfx::TEX_UAV | gfx::TEX_GENMIPS, 0, 1);
        gfx::ctx->setCS(csWave);
        gfx::ctx->csSetUAVs(0, 1, &waveTex.uav);
        gfx::ctx->dispatch(64, 64, 1);
        gfx::unbindCSResources(1, 1);
        gfx::ctx->generateMips(waveTex);
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
        gfx::BlendDesc bd;
        bd.rt[0] = {true, gfx::BLEND_SRC_ALPHA, gfx::BLEND_INV_SRC_ALPHA, gfx::BLENDOP_ADD, gfx::BLEND_ZERO, gfx::BLEND_ONE,
                    gfx::BLENDOP_ADD, gfx::WRITE_ALL};
        blend = gfx::createBlendState(bd);
        // wakes
        gfx::InputElement wl[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, gfx::PER_VERTEX, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, gfx::PER_VERTEX, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, gfx::PER_VERTEX, 0}};
        vsWake = gfx::loadVS("water.hlsl", "vsWake", wl, 3);
        psWake = gfx::loadPS("water.hlsl", "psWake");
        wakeTex = gfx::createTexture2D(kWakeRes, kWakeRes, DXGI_FORMAT_R16G16_FLOAT, gfx::TEX_RTV | gfx::TEX_SRV);
        wakeVB = gfx::createBuffer(kMaxWakeVerts * sizeof(WakeVtx), sizeof(WakeVtx), gfx::BUF_VERTEX | gfx::BUF_DYNAMIC);
        wakeBlend = gfx::states.additive;
    }

    // A boat reports its bow position every frame; a new trail point every 2 m (or 0.5 s), history up to 30 s.
    void addWake(int id, dvec3 pos, vec2 dir, float speed, float beam, float now) {
        WakeTrail& t = trails[id];
        t.head = dvec3(pos.x, pos.y, 0.0);
        if (length2(dir) > 1e-6f) t.heading = normalize(dir);
        t.speed = speed;
        t.beam = Max(beam, 0.3f);
        t.lastUpdate = now;
        if (speed < 0.5f) return;
        if (t.pts.empty() || length(rel(t.head, t.pts.front().pos)) > 2.f ||
            now - t.pts.front().time > 0.5f)
            t.pts.push_front({t.head, now, speed});
        while (!t.pts.empty() && (t.pts.size() > 120 || now - t.pts.back().time > 30.f)) t.pts.pop_back();
    }

    // Rebuilds the wake map around the camera from all live trails (returns false when there is nothing to draw).
    bool renderWakes(Renderer& r) {
        auto* c = gfx::ctx;
        float now = r.frame.time.x;
        for (auto it = trails.begin(); it != trails.end();) {
            WakeTrail& t = it->second;
            while (!t.pts.empty() && now - t.pts.back().time > 30.f) t.pts.pop_back();
            if (now - t.lastUpdate > 30.f || now < t.lastUpdate - 1.f) it = trails.erase(it);
            else ++it;
        }
        wakeVerts.clear();
        double texel = kWakeSize / kWakeRes;
        wakeOrigin = dvec3(floor(r.camera.pos.x / texel) * texel - kWakeSize * 0.5, floor(r.camera.pos.y / texel) * texel - kWakeSize * 0.5, 0.0);
        for (auto& kv : trails) {
            const WakeTrail& t = kv.second;
            if (t.pts.empty()) continue;
            // polyline: live head, then the history
            std::vector<WakePoint> line;
            line.push_back({t.head, now, t.speed});
            for (const WakePoint& p : t.pts) line.push_back(p);
            float sAcc = 0.f;
            WakeVtx prevL = {}, prevR = {};
            for (size_t k = 0; k < line.size(); k++) {
                vec2 d;
                if (k == 0) d = t.heading;   // the bow follows the hull's heading (no lag in turns)
                else if (k + 1 < line.size()) d = vec2((float)(line[k].pos.x - line[k + 1].pos.x), (float)(line[k].pos.y - line[k + 1].pos.y));
                else d = vec2((float)(line[k - 1].pos.x - line[k].pos.x), (float)(line[k - 1].pos.y - line[k].pos.y));
                if (k > 0) sAcc += length(vec2((float)(line[k - 1].pos.x - line[k].pos.x), (float)(line[k - 1].pos.y - line[k].pos.y)));
                float dl = length(d);
                vec2 fwd = dl > 1e-3f ? d / dl : vec2(0, 1);
                vec2 side(-fwd.y, fwd.x);
                float age = now - line[k].time;
                float W = Min(t.beam + 0.354f * sAcc, 45.f);
                float amp = Saturate(line[k].speed / 12.f);
                vec2 local((float)(line[k].pos.x - wakeOrigin.x), (float)(line[k].pos.y - wakeOrigin.y));
                vec2 wrapped((float)fmod(line[k].pos.x, 2048.0), (float)fmod(line[k].pos.y, 2048.0));
                WakeVtx L, R;
                vec2 pl = local + side * W, pr = local - side * W;
                L.pos = vec4(pl.x, pl.y, -W, sAcc);
                R.pos = vec4(pr.x, pr.y, W, sAcc);
                L.info = R.info = vec4(age, W, amp, t.beam);
                L.world = wrapped + side * W;
                R.world = wrapped - side * W;
                if (k > 0 && (int)wakeVerts.size() + 6 <= kMaxWakeVerts) {
                    wakeVerts.push_back(prevL); wakeVerts.push_back(prevR); wakeVerts.push_back(L);
                    wakeVerts.push_back(L); wakeVerts.push_back(prevR); wakeVerts.push_back(R);
                }
                prevL = L;
                prevR = R;
            }
        }
        float clear0[4] = {0, 0, 0, 0};
        c->clearRTV(wakeTex.rtv, clear0);
        if (wakeVerts.empty()) return false;
        gfx::updateBuffer(wakeVB, wakeVerts.data(), (u32)(wakeVerts.size() * sizeof(WakeVtx)));
        cb.data.wake = vec4((float)wakeOrigin.x, (float)wakeOrigin.y, kWakeSize, 1.f);
        cb.upload();
        c->setRenderTargets(1, &wakeTex.rtv, nullptr);
        gfx::setViewport((float)kWakeRes, (float)kWakeRes);
        c->setBlendState(wakeBlend);
        c->setDepthState(gfx::states.depthOff);
        c->setRasterState(gfx::states.cullNone);
        gfx::Resource  cbs[] = {r.frameCB.get(), cb.get()};
        c->vsSetCBs(0, 2, cbs);
        c->psSetCBs(0, 2, cbs);
        c->setInputLayout(vsWake.layout);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        UINT stride = sizeof(WakeVtx), offset = 0;
        c->setVertexBuffers(0, 1, &wakeVB.buf, &stride, &offset);
        c->setVS(vsWake.vs);
        c->setPS(psWake);
        c->draw((UINT)wakeVerts.size(), 0);
        r.stats.drawCalls++;
        c->setRenderTargets(0, nullptr, nullptr);
        c->setBlendState(gfx::states.opaque);
        return true;
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

    void draw(Renderer& r, TerrainRenderer& t, gfx::SRV  sceneColor, gfx::SRV  sceneDepth, float waveStrength,
              gfx::SRV  hiz, int hizMips) {
        auto* c = gfx::ctx;
        // Wake map first (it needs its own render target), then restore the HDR target for the water surface
        gfx::RTV  prevRT = nullptr;
        gfx::DSV  prevDS = nullptr;
        c->getRenderTargets(&prevRT, &prevDS);
        gfx::Viewport prevVP = c->getViewport();
        wakeActive = renderWakes(r);
        c->setRenderTargets(1, &prevRT, prevDS);
        c->setViewport(prevVP);
        c->setDepthState(gfx::states.depthGreaterWrite);
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
        cb.data.wake = vec4((float)wakeOrigin.x, (float)wakeOrigin.y, kWakeSize, wakeActive ? 1.f : 0.f);
        cb.upload();
        gfx::Resource  cbs[] = {cb.get()};
        c->vsSetCBs(1, 1, cbs);
        c->psSetCBs(1, 1, cbs);
        gfx::Resource  scb[] = {r.shadowCB.get()};
        c->psSetCBs(3, 1, scb);
        gfx::SRV  srvs[8] = {t.heightTex.srv, t.waterTex.srv, sceneColor, sceneDepth, waveTex.srv, hiz, nodeBuf.srv, wakeTex.srv};
        c->vsSetSRVs(0, 8, srvs);
        c->psSetSRVs(0, 8, srvs);
        c->setInputLayout(vs.layout);
        c->setTopology(gfx::TOPO_TRIANGLE_LIST);
        c->setVS(vs.vs);
        c->setPS(ps);
        c->setBlendState(blend);
        c->setRasterState(gfx::states.cullNone);
        if (drawn > 0) {
            gfx::updateBuffer(nodeBuf, nodes.data(), (u32)(nodes.size() * 16));
            UINT stride = 8, offset = 0;
            c->setVertexBuffers(0, 1, &t.vb.buf, &stride, &offset);
            c->setIndexBuffer(t.ib.buf, DXGI_FORMAT_R16_UINT, 0);
            c->drawIndexedInstanced((UINT)t.indexCount, (UINT)drawn, 0, 0, 0);
            r.stats.drawCalls++;
        }
        // Ocean skirt beyond the world square
        cb.data.mode.x = 1.f;
        cb.upload();
        UINT stride = 8, offset = 0;
        c->setVertexBuffers(0, 1, &skirtVB.buf, &stride, &offset);
        c->setIndexBuffer(skirtIB.buf, DXGI_FORMAT_R32_UINT, 0);
        c->drawIndexed((UINT)skirtIndexCount, 0, 0);
        r.stats.drawCalls++;
        gfx::SRV  nulls[8] = {};
        c->vsSetSRVs(0, 8, nulls);
        c->psSetSRVs(0, 8, nulls);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullBack);
    }
};

void Renderer::addWake(int boatId, dvec3 pos, vec2 dir, float speed, float beam) {
    if (water) water->addWake(boatId, pos, dir, speed, beam, frame.time.x);
}

}  // namespace Render
