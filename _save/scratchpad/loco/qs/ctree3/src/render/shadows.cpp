// Cascaded sun shadow maps (stabilized). Included from renderer.cpp.
namespace Render {

struct ShadowPassCBData {
    mat4 viewProj;
};

struct ShadowSystem {
    gfx::Texture map;
    int res = 2048;
    int cascades = 4;
    mat4 cascadeVP[4];
    float splits[4];
    float texelWorld[4];
    float depthRange[4] = {1, 1, 1, 1};   // light-space depth range (m) of each cascade (PCSS penumbra)
    dvec3 cascadeCam[4];                  // camera position each cascade was rendered from
    bool cascadeValid[4] = {false, false, false, false};
    bool renderThis[4] = {true, true, true, true};
    float texelRender[4] = {1, 1, 1, 1};
    gfx::CBuffer<ShadowPassCBData> passCB;
    float shadowDistance = 900.f;
    // Hooks for other geometry to render into the shadow map
    std::vector<std::function<void(Renderer&, const mat4&, int)>> casters;

    void init(int resolution) {
        passCB.create();
        setResolution(resolution);
    }

    // (Re)creates the cascade array (quality presets change the resolution at runtime).
    void setResolution(int resolution) {
        resolution = Clamp(resolution, 512, 8192);
        if (resolution == res && map.res) return;
        for (bool& v : cascadeValid) v = false;
        map.release();
        res = resolution;
        map = gfx::createTexture2D(res, res, DXGI_FORMAT_R32_TYPELESS, gfx::TEX_DSV | gfx::TEX_SRV | gfx::TEX_SLICE_RTVS, 1, 4);
    }

    void computeCascades(Renderer& r) {
        const Camera& cam = r.camera;
        float nearZ = 0.5f;
        float farZ = shadowDistance;
        float lambda = 0.88f;
        for (int i = 0; i < cascades; i++) {
            float p = (float)(i + 1) / cascades;
            float logS = nearZ * powf(farZ / nearZ, p);
            float linS = nearZ + (farZ - nearZ) * p;
            splits[i] = Lerp(linS, logS, lambda);
        }
        vec3 L = r.sunDir;
        if (L.z < 0.05f) L = normalize(vec3(L.x, L.y, 0.05f));
        // Light basis
        vec3 lz = L;  // towards the light
        vec3 lx = normalize(cross(vec3(0, 0, 1), lz));
        if (length2(cross(vec3(0, 0, 1), lz)) < 1e-6f) lx = vec3(1, 0, 0);
        vec3 ly = cross(lz, lx);
        float aspect = (float)r.width / (float)r.height;
        float tanY = tanf(cam.fovY * 0.5f), tanX = tanY * aspect;
        vec3 f = cam.forward(), right = normalize(cross(f, vec3(0, 0, 1))), up = cross(right, f);
        float prev = nearZ;
        for (int i = 0; i < cascades; i++) {
            float n = prev, fz = splits[i];
            prev = fz;
            // Bounding sphere of the frustum slice (camera-relative)
            float k = sqrtf(1.f + tanX * tanX + tanY * tanY);
            float cz = 0.5f * (n + fz) * (1.f + k * k);
            if (cz > fz) cz = fz;
            vec3 center = f * cz;
            vec3 farCorner = f * fz + right * (tanX * fz) + up * (tanY * fz);
            float radius = length(farCorner - center);
            radius = ceilf(radius * 16.f) / 16.f;
            float texel = 2.f * radius / res;
            texelWorld[i] = texel;
            // Snap center to the texel grid in absolute world space (stable while moving)
            double ax = cam.pos.x + center.x, ay = cam.pos.y + center.y, az = cam.pos.z + center.z;
            double px = ax * lx.x + ay * lx.y + az * lx.z;
            double py = ax * ly.x + ay * ly.y + az * ly.z;
            double pz = ax * lz.x + ay * lz.y + az * lz.z;
            px = floor(px / texel) * texel;
            py = floor(py / texel) * texel;
            dvec3 snapped(lx.x * px + ly.x * py + lz.x * pz, lx.y * px + ly.y * py + lz.y * pz, lx.z * px + ly.z * py + lz.z * pz);
            vec3 c = rel(snapped, cam.pos);
            float backExtra = 600.f + radius;  // casters behind (tall buildings, hills)
            vec3 eye = c + lz * backExtra;
            mat4 view = lookAtRH(eye, c, fabsf(lz.z) > 0.99f ? vec3(0, 1, 0) : vec3(0, 0, 1));
            // Use our basis explicitly for stability
            view = mat4(vec4(lx.x, ly.x, lz.x, 0), vec4(lx.y, ly.y, lz.y, 0), vec4(lx.z, ly.z, lz.z, 0),
                        vec4(-dot(lx, eye), -dot(ly, eye), -dot(lz, eye), 1));
            mat4 proj = orthoRH(-radius, radius, -radius, radius, 0.f, backExtra + radius);
            // Far cascades are re-rendered every other frame (alternating); in between they keep the matrix
            // they were rendered with, re-expressed relative to the moved camera.
            bool refresh = i < 2 || cascades < 4 || r.cameraCut || !cascadeValid[i] || ((int)(r.frameIndex & 1) == (i & 1));
            renderThis[i] = refresh;
            if (refresh) {
                cascadeVP[i] = proj * view;
                cascadeCam[i] = cam.pos;
                cascadeValid[i] = true;
                texelRender[i] = texel;
                depthRange[i] = backExtra + radius;
            }
        }
        ShadowConstants& sc = r.shadowCB.data;
        for (int i = 0; i < 4; i++) {
            int ci = Min(i, cascades - 1);
            sc.cascadeViewProj[i] = cascadeVP[ci] * mat4Translation(rel(cam.pos, cascadeCam[ci]));
            texelWorld[ci] = texelRender[ci];
        }
        sc.cascadeSplits = vec4(splits[0], splits[1], splits[2], splits[3]);
        sc.cascadeTexel = vec4(texelWorld[0], texelWorld[1], texelWorld[2], texelWorld[3]);
        sc.shadowParams = vec4((float)res, (float)cascades, 0.85f, r.settings.softShadows ? 1.f : 0.f);
        sc.pad = vec4(depthRange[0], depthRange[1], depthRange[2], depthRange[3]);
        r.shadowCB.upload();
    }

    // This frame's cascades (which ones re-render and their matrices); before render() and the prop cull, which
    // culls for them.
    void prepare(Renderer& r) {
        setResolution(r.settings.shadowRes);
        cascades = Clamp(r.settings.shadowCascades, 1, 4);
        computeCascades(r);
    }

    void render(Renderer& r) {
        auto* c = gfx::ctx;
        gfx::SRV  nullSrv = nullptr;
        c->psSetSRVs(35, 1, &nullSrv);
        c->csSetSRVs(35, 1, &nullSrv);
        c->vsSetSRVs(35, 1, &nullSrv);
        gfx::setViewport((float)res, (float)res);
        c->setRasterState(gfx::states.shadowBias);
        c->setDepthState(gfx::states.depthLessWrite);
        c->setBlendState(gfx::states.noColorWrite);
        for (int i = 0; i < cascades; i++) {
            if (!renderThis[i]) continue;
            gfx::DSV  dsv = map.sliceDsvs[i];
            c->clearDepth(dsv, 1.f);
            c->setRenderTargets(0, nullptr, dsv);
            passCB.data.viewProj = cascadeVP[i];
            passCB.upload();
            gfx::Resource  cbs[] = {passCB.get()};
            c->vsSetCBs(2, 1, cbs);
            r.terrain->drawShadow(r, cascadeVP[i], r.camera.pos);
            for (auto& fn : casters) fn(r, cascadeVP[i], i);
        }
        c->setRenderTargets(0, nullptr, nullptr);
        c->setBlendState(gfx::states.opaque);
        c->setRasterState(gfx::states.cullBack);
    }
};

}  // namespace Render
