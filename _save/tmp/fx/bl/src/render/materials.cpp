// World material library: GPU-generated texture arrays + material table. Included from renderer.cpp.
#include "mesh.h"

namespace Render {

struct MaterialInfoGPU {
    float layer, uvScale, roughScale, metal;
    vec4 tint;
    float normalScale, shadingModel, flags, emissive;
};

enum MatFlags { MF_PAINT = 1, MF_GLASS = 2, MF_FACADE = 4, MF_FOLIAGE = 8, MF_EMISSIVE = 16, MF_UNLIT = 32, MF_CARPAINT = 64,
                MF_SKIN = 128, MF_HAIR = 256, MF_CLOTH = 512 };

struct MaterialLibrary {
    gfx::Texture albedoArr, normalArr;
    gfx::Buffer table;
    gfx::ComputeShader  gens[28] = {};
    int size = 1024;
    int layerCount = 0;
    MaterialInfoGPU infos[MAT_COUNT];

    struct Def {
        MaterialId id;
        int gen;
        vec3 a, b, c;
        vec4 params;
        float tileMeters, roughScale, metal, flags, emissive;
    };

    void init(TerrainRenderer& terrain) {
        size = Platform::hasArg("autotest") ? 256 : 1024;
        if (const char* q = Platform::argValue("texsize")) size = atoi(q);
        std::vector<Def> defs = {
            {MAT_ASPHALT, 7, vec3(0.045f, 0.045f, 0.047f), vec3(0.07f, 0.068f, 0.066f), vec3(0.16f, 0.155f, 0.15f), vec4(0, 0, 0, 5), 4.f, 1.f, 0, 0, 0},
            {MAT_ASPHALT_OLD, 7, vec3(0.08f, 0.078f, 0.074f), vec3(0.115f, 0.11f, 0.105f), vec3(0.2f, 0.19f, 0.18f), vec4(0, 0, 0, 5), 4.f, 1.f, 0, 0, 0},
            {MAT_CONCRETE, 8, vec3(0.33f, 0.32f, 0.3f), vec3(0.43f, 0.42f, 0.39f), vec3(0.22f, 0.2f, 0.17f), vec4(0, 0, 0, 4), 4.f, 1.f, 0, 0, 0},
            {MAT_SIDEWALK, 9, vec3(0.4f, 0.39f, 0.36f), vec3(0.5f, 0.48f, 0.45f), vec3(0.28f, 0.26f, 0.23f), vec4(0, 0, 0, 4), 3.f, 1.f, 0, 0, 0},
            {MAT_CURB, 8, vec3(0.45f, 0.44f, 0.42f), vec3(0.55f, 0.54f, 0.5f), vec3(0.3f, 0.28f, 0.25f), vec4(0, 0, 0, 4), 2.f, 1.f, 0, 0, 0},
            {MAT_PAINT_WHITE, 27, vec3(0.78f, 0.78f, 0.76f), vec3(0.3f, 0.3f, 0.29f), vec3(0.2f), vec4(0, 0, 0, 2), 3.f, 1.f, 0, MF_PAINT, 0},
            {MAT_PAINT_YELLOW, 27, vec3(0.7f, 0.5f, 0.06f), vec3(0.28f, 0.22f, 0.1f), vec3(0.2f), vec4(0, 0, 0, 2), 3.f, 1.f, 0, MF_PAINT, 0},
            {MAT_BRICK, 10, vec3(0.33f, 0.11f, 0.065f), vec3(0.48f, 0.2f, 0.11f), vec3(0.5f, 0.48f, 0.45f), vec4(0, 0, 0, 8), 2.f, 1.f, 0, 0, 0},
            {MAT_STUCCO, 11, vec3(0.86f, 0.85f, 0.82f), vec3(0.45f, 0.4f, 0.32f), vec3(0.3f), vec4(0, 0, 0, 3), 3.f, 1.f, 0, 0, 0},
            {MAT_PLASTER, 12, vec3(0.86f, 0.85f, 0.82f), vec3(0.45f, 0.4f, 0.32f), vec3(0.3f), vec4(0, 0, 0, 2), 4.f, 1.f, 0, 0, 0},
            {MAT_WOOD_SIDING, 13, vec3(0.82f, 0.82f, 0.8f), vec3(0.5f), vec3(0.3f), vec4(0, 0, 0, 5), 2.f, 1.f, 0, 0, 0},
            {MAT_ROOF_TILE, 14, vec3(0.42f, 0.14f, 0.065f), vec3(0.58f, 0.24f, 0.1f), vec3(0.18f, 0.22f, 0.1f), vec4(0, 0, 0, 7), 2.4f, 1.f, 0, 0, 0},
            {MAT_ROOF_SHINGLE, 15, vec3(0.1f, 0.1f, 0.1f), vec3(0.2f, 0.185f, 0.17f), vec3(0.2f), vec4(0, 0, 0, 6), 3.f, 1.f, 0, 0, 0},
            {MAT_ROOF_METAL, 16, vec3(0.55f, 0.56f, 0.58f), vec3(0.45f, 0.47f, 0.5f), vec3(0.32f, 0.16f, 0.07f), vec4(0, 0, 0, 5), 2.5f, 1.f, 0.6f, 0, 0},
            {MAT_ROOF_GRAVEL, 6, vec3(0.33f, 0.32f, 0.3f), vec3(0.47f, 0.45f, 0.42f), vec3(0.18f, 0.18f, 0.18f), vec4(0, 0, 0, 5), 2.f, 1.f, 0, 0, 0},
            {MAT_GLASS, 17, vec3(0.02f, 0.025f, 0.03f), vec3(0.02f), vec3(0.02f), vec4(0, 0, 0, 1), 4.f, 1.f, 0, MF_GLASS, 0},
            {MAT_METAL_PAINTED, 18, vec3(0.6f), vec3(0.6f), vec3(0.3f, 0.3f, 0.28f), vec4(0, 0, 0, 2), 2.f, 1.f, 0, 0, 0},
            {MAT_METAL_BRUSHED, 19, vec3(0.75f), vec3(0.75f), vec3(0.6f), vec4(0, 0, 0, 1), 1.f, 1.f, 1.f, 0, 0},
            {MAT_PAVERS, 20, vec3(0.42f, 0.28f, 0.19f), vec3(0.52f, 0.4f, 0.29f), vec3(0.3f, 0.28f, 0.25f), vec4(0, 0, 0, 6), 2.f, 1.f, 0, 0, 0},
            {MAT_MARBLE, 21, vec3(0.8f, 0.78f, 0.74f), vec3(0.45f, 0.43f, 0.4f), vec3(0.3f), vec4(0, 0, 0, 2), 2.f, 1.f, 0, 0, 0},
            {MAT_STONE, 22, vec3(0.48f, 0.43f, 0.36f), vec3(0.6f, 0.55f, 0.48f), vec3(0.28f, 0.26f, 0.24f), vec4(0, 0, 0, 7), 3.f, 1.f, 0, 0, 0},
            {MAT_CORRUGATED, 16, vec3(0.42f, 0.44f, 0.47f), vec3(0.36f, 0.38f, 0.4f), vec3(0.3f, 0.15f, 0.06f), vec4(0, 0, 0, 5), 2.f, 1.f, 0.7f, 0, 0},
            {MAT_FABRIC, 23, vec3(0.85f), vec3(0.25f), vec3(0.3f), vec4(1, 0, 0, 3), 2.f, 1.f, 0, 0, 0},
            {MAT_WOOD, 24, vec3(0.28f, 0.18f, 0.1f), vec3(0.4f, 0.27f, 0.16f), vec3(0.3f), vec4(0, 0, 0, 5), 2.f, 1.f, 0, 0, 0},
            {MAT_TILE_POOL, 25, vec3(0.1f, 0.45f, 0.65f), vec3(0.18f, 0.58f, 0.78f), vec3(0.75f, 0.75f, 0.72f), vec4(0, 0, 0, 3), 1.f, 1.f, 0, 0, 0},
            {MAT_GRASS, 1, vec3(0.05f, 0.1f, 0.025f), vec3(0.09f, 0.15f, 0.035f), vec3(0.2f, 0.19f, 0.09f), vec4(0.2f, 0, 0, 5), 3.f, 1.f, 0, 0, 0},
            {MAT_DIRT, 2, vec3(0.12f, 0.08f, 0.05f), vec3(0.2f, 0.14f, 0.08f), vec3(0.3f, 0.28f, 0.25f), vec4(0, 0, 0, 7), 3.f, 1.f, 0, 0, 0},
            {MAT_RUBBER, 8, vec3(0.025f), vec3(0.035f), vec3(0.02f), vec4(0, 0, 0, 2), 2.f, 1.1f, 0, 0, 0},
            {MAT_EMISSIVE, 17, vec3(1.f), vec3(1.f), vec3(1.f), vec4(0, 0, 0, 1), 4.f, 1.f, 0, MF_EMISSIVE, 1.f},
            {MAT_CHROME, 19, vec3(0.95f), vec3(0.95f), vec3(0.9f), vec4(0, 0, 0, 1), 1.f, 0.3f, 1.f, 0, 0},
            {MAT_CONCRETE_PANEL, 26, vec3(0.42f, 0.41f, 0.39f), vec3(0.52f, 0.5f, 0.47f), vec3(0.3f, 0.28f, 0.25f), vec4(0, 0, 0, 4), 4.f, 1.f, 0, 0, 0},
            {MAT_FACADE, 11, vec3(0.86f), vec3(0.45f, 0.4f, 0.32f), vec3(0.3f), vec4(0, 0, 0, 3), 3.f, 1.f, 0, MF_FACADE, 0},
            {MAT_LEAVES, 1, vec3(0.05f, 0.12f, 0.03f), vec3(0.1f, 0.18f, 0.04f), vec3(0.2f, 0.2f, 0.06f), vec4(0.1f, 0, 0, 4), 2.f, 1.f, 0, MF_FOLIAGE, 0},
            {MAT_BARK, 22, vec3(0.2f, 0.15f, 0.1f), vec3(0.3f, 0.24f, 0.17f), vec3(0.12f, 0.1f, 0.08f), vec4(0, 0, 0, 8), 1.5f, 1.f, 0, 0, 0},
            {MAT_PALM_FROND, 1, vec3(0.06f, 0.14f, 0.03f), vec3(0.12f, 0.2f, 0.05f), vec3(0.3f, 0.28f, 0.12f), vec4(0.3f, 0, 0, 4), 2.f, 1.f, 0, MF_FOLIAGE, 0},
            {MAT_SAND, 0, vec3(0.45f, 0.38f, 0.26f), vec3(0.58f, 0.5f, 0.36f), vec3(0.2f, 0.18f, 0.16f), vec4(0, 0, 0, 4), 4.f, 1.f, 0, 0, 0},
            {MAT_CARPAINT, 18, vec3(0.8f), vec3(0.8f), vec3(0.4f), vec4(0, 0, 0, 1), 1.f, 1.f, 0.2f, MF_CARPAINT, 0},
            {MAT_PLASTIC, 18, vec3(0.03f), vec3(0.035f), vec3(0.06f), vec4(0, 0, 0, 1), 1.f, 1.2f, 0, 0, 0},
            {MAT_LEATHER, 11, vec3(0.12f, 0.07f, 0.04f), vec3(0.1f, 0.06f, 0.035f), vec3(0.05f), vec4(0, 0, 0, 2), 0.5f, 0.8f, 0, 0, 0},
            {MAT_LIGHT_HEAD, 17, vec3(0.8f), vec3(0.8f), vec3(0.8f), vec4(0, 0, 0, 1), 1.f, 0.3f, 0, 0, 0},
            {MAT_LIGHT_TAIL, 17, vec3(0.4f, 0.02f, 0.02f), vec3(0.4f, 0.02f, 0.02f), vec3(0.3f), vec4(0, 0, 0, 1), 1.f, 0.3f, 0, 0, 0},
            {MAT_SKIN, 11, vec3(0.9f), vec3(0.8f, 0.7f, 0.65f), vec3(0.5f), vec4(0, 0, 0, 1), 0.25f, 1.f, 0, MF_SKIN, 0},
            {MAT_HAIR, 19, vec3(0.8f), vec3(0.8f), vec3(0.6f), vec4(0, 0, 0, 2), 0.15f, 1.f, 0, MF_HAIR, 0},
            {MAT_CLOTH, 23, vec3(0.85f), vec3(0.85f), vec3(0.3f), vec4(0, 0, 0, 2), 0.3f, 1.f, 0, MF_CLOTH, 0},
            {MAT_DENIM, 23, vec3(0.12f, 0.18f, 0.35f), vec3(0.08f, 0.13f, 0.28f), vec3(0.3f), vec4(0, 1, 0, 3), 0.25f, 1.f, 0, MF_CLOTH, 0},
            {MAT_EYE, 17, vec3(0.85f), vec3(0.85f), vec3(0.8f), vec4(0, 0, 0, 1), 1.f, 0.3f, 0, 0, 0},
            {MAT_TIRE, 15, vec3(0.025f), vec3(0.03f), vec3(0.02f), vec4(0, 0, 0, 6), 0.4f, 1.f, 0, 0, 0},
            {MAT_RIM, 19, vec3(0.8f), vec3(0.8f), vec3(0.7f), vec4(0, 0, 0, 1), 0.5f, 0.5f, 1.f, 0, 0},
            {MAT_CAR_GLASS, 17, vec3(0.01f), vec3(0.01f), vec3(0.01f), vec4(0, 0, 0, 1), 1.f, 0.2f, 0, MF_GLASS, 0},
            {MAT_INTERIOR, 23, vec3(0.06f), vec3(0.05f), vec3(0.04f), vec4(0, 0, 0, 2), 0.5f, 1.f, 0, 0, 0},
            {MAT_LIGHT_INDICATOR, 17, vec3(0.6f, 0.35f, 0.02f), vec3(0.6f, 0.35f, 0.02f), vec3(0.3f), vec4(0, 0, 0, 1), 1.f, 0.3f, 0, 0, 0},
            {MAT_DECAL_TEXT, 27, vec3(0.9f), vec3(0.9f), vec3(0.9f), vec4(0, 0, 0, 1), 1.f, 1.f, 0, 0, 0},
            {MAT_CAR_WINDOW, 17, vec3(0.01f), vec3(0.01f), vec3(0.01f), vec4(0, 0, 0, 1), 1.f, 0.2f, 0, MF_GLASS, 0},
            // interiors: tile grid generators reused with neutral colours (the vertex colour tints them)
            {MAT_TILE, 25, vec3(0.84f, 0.84f, 0.82f), vec3(0.8f, 0.8f, 0.78f), vec3(0.24f, 0.235f, 0.22f), vec4(0, 0, 0, 0.7f), 9.6f, 2.8f, 0, 0, 0},
            {MAT_CEILING_TILE, 25, vec3(0.88f, 0.88f, 0.86f), vec3(0.84f, 0.84f, 0.82f), vec3(0.62f, 0.62f, 0.6f), vec4(0, 0, 0, 2), 19.2f, 9.f, 0, 0, 0},
            {MAT_CARPET, 23, vec3(0.7f), vec3(0.62f), vec3(0.3f), vec4(0, 2, 0, 4), 0.35f, 1.f, 0, 0, 0},
            {MAT_WOOD_FLOOR, 24, vec3(0.5f, 0.36f, 0.22f), vec3(0.6f, 0.45f, 0.29f), vec3(0.3f), vec4(0, 0, 0, 4), 1.8f, 0.55f, 0, 0, 0},
        };
        layerCount = (int)defs.size();
        albedoArr = createMaterialArray(size, layerCount, true);
        normalArr = createMaterialArray(size, layerCount, false);
        auto* c = gfx::ctx;
        gfx::UAV  uavs[] = {albedoArr.uav, normalArr.uav};
        c->csSetUAVs(0, 2, uavs);
        for (size_t i = 0; i < defs.size(); i++) {
            const Def& d = defs[i];
            if (!gens[d.gen]) {
                std::string g = std::to_string(d.gen);
                gens[d.gen] = gfx::loadCS("matgen.hlsl", "csGenerate", {{"GEN", g.c_str()}});
            }
            c->setCS(gens[d.gen]);
            terrain.matCB.data.layer = (u32)i;
            terrain.matCB.data.type = (u32)d.gen;
            terrain.matCB.data.size = (u32)size;
            terrain.matCB.data.seed = (u32)(i * 977 + 13);
            terrain.matCB.data.colorA = vec4(d.a, 1);
            terrain.matCB.data.colorB = vec4(d.b, 1);
            terrain.matCB.data.colorC = vec4(d.c, 1);
            terrain.matCB.data.params = d.params;
            terrain.matCB.upload();
            gfx::Resource  cbs[] = {terrain.matCB.get()};
            c->csSetCBs(1, 1, cbs);
            c->dispatch(gfx::divUp(size, 8), gfx::divUp(size, 8), 1);
            MaterialInfoGPU& mi = infos[d.id];
            mi.layer = (float)i;
            mi.uvScale = 1.f / d.tileMeters;
            mi.roughScale = d.roughScale;
            mi.metal = d.metal;
            mi.tint = vec4(1, 1, 1, 1);
            mi.normalScale = 1.f;
            mi.shadingModel = (d.flags == MF_FOLIAGE) ? 1.f : (d.flags == MF_CARPAINT ? 3.f : (d.flags == MF_SKIN ? 2.f : (d.flags == MF_HAIR ? 6.f : (d.flags == MF_CLOTH ? 7.f : 0.f))));
            mi.flags = d.flags;
            mi.emissive = d.emissive;
        }
        gfx::unbindCSResources(4, 2);
        c->generateMips(albedoArr);
        c->generateMips(normalArr);
        table = gfx::createBuffer(sizeof(infos), sizeof(MaterialInfoGPU), gfx::BUF_STRUCTURED, infos);
        LOG("Material library: %d layers at %d^2", layerCount, size);
    }
};

}  // namespace Render
