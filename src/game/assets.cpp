// Load-time generation of gameplay assets: vehicle models, the character roster, weapon and pickup meshes.
#include "gameworld.h"

namespace Game {

namespace asset_detail {

// Cylinder between two points (arbitrary axis), optional caps.
void cylinderAB(MeshData& m, vec3 a, vec3 b, float r0, float r1, int seg, u32 color, u32 mat, bool caps = true) {
    vec3 axis = b - a;
    float h = length(axis);
    if (h < 1e-5f) return;
    vec3 z = axis / h;
    vec3 x = normalize(fabsf(z.z) < 0.9f ? cross(z, vec3(0, 0, 1)) : cross(z, vec3(1, 0, 0)));
    vec3 y = cross(z, x);
    u32 start = (u32)m.verts.size();
    for (int i = 0; i <= seg; i++) {
        float t = kTwoPi * i / seg;
        vec3 d = x * cosf(t) + y * sinf(t);
        vec3 n = normalize(d + z * ((r0 - r1) / h));
        vec3 tg = x * -sinf(t) + y * cosf(t);
        m.addVertex(a + d * r0, n, tg, vec2((float)i / seg, 0), color, mat);
        m.addVertex(b + d * r1, n, tg, vec2((float)i / seg, h), color, mat);
    }
    for (int i = 0; i < seg; i++) {
        u32 i0 = start + i * 2;
        m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
    }
    if (caps) {
        for (int e = 0; e < 2; e++) {
            float r = e ? r1 : r0;
            if (r < 1e-4f) continue;
            vec3 c = e ? b : a;
            vec3 n = e ? z : -z;
            u32 ci = m.addVertex(c, n, x, vec2(0.5f, 0.5f), color, mat);
            u32 first = (u32)m.verts.size();
            for (int i = 0; i <= seg; i++) {
                float t = kTwoPi * i / seg;
                vec3 d = x * cosf(t) + y * sinf(t);
                m.addVertex(c + d * r, n, x, vec2(0.5f + cosf(t) * 0.5f, 0.5f + sinf(t) * 0.5f), color, mat);
            }
            for (int i = 0; i < seg; i++) {
                if (e) m.tri(ci, first + i, first + i + 1);
                else m.tri(ci, first + i + 1, first + i);
            }
        }
    }
}

// Oriented box from center, forward (Y) direction and up hint.
void obox(MeshData& m, vec3 c, vec3 fwd, vec3 upHint, vec3 he, u32 color, u32 mat) {
    vec3 y = normalize(fwd);
    vec3 x = normalize(cross(y, upHint));
    vec3 z = cross(x, y);
    m.box(c, x, y, z, he, color, mat, true, 1.f);
}

void sphere(MeshData& m, vec3 c, float r, int seg, u32 color, u32 mat) {
    int rings = Max(3, seg / 2);
    u32 start = (u32)m.verts.size();
    for (int j = 0; j <= rings; j++) {
        float v = kPi * j / rings;
        for (int i = 0; i <= seg; i++) {
            float u = kTwoPi * i / seg;
            vec3 n(sinf(v) * cosf(u), sinf(v) * sinf(u), cosf(v));
            m.addVertex(c + n * r, n, vec3(-sinf(u), cosf(u), 0), vec2((float)i / seg, (float)j / rings), color, mat);
        }
    }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < seg; i++) {
            u32 a = start + j * (seg + 1) + i, b = a + seg + 1;
            m.quadIdx(a, b, b + 1, a + 1);
        }
}

// Weapon meshes: grip at the origin, barrel along +Y, up +Z.
void buildWeaponMesh(WeaponType w, MeshData& m) {
    u32 black = packRGBA8(0.035f, 0.036f, 0.04f, 1), gun = packRGBA8(0.07f, 0.072f, 0.078f, 1);
    u32 steel = packRGBA8(0.55f, 0.56f, 0.58f, 1), wood = packRGBA8(0.42f, 0.24f, 0.12f, 1);
    u32 poly = packRGBA8(0.05f, 0.05f, 0.05f, 1), tan = packRGBA8(0.45f, 0.38f, 0.26f, 1);
    u32 olive = packRGBA8(0.2f, 0.24f, 0.14f, 1);
    u32 matMetal = makeMat(MAT_METAL_PAINTED), matSteel = makeMat(MAT_METAL_BRUSHED), matPlastic = makeMat(MAT_PLASTIC);
    u32 matWood = makeMat(MAT_WOOD);
    vec3 F(0, 1, 0), U(0, 0, 1);
    auto grip = [&](vec3 base, float len, float ang, vec3 he, u32 col, u32 mat) {
        // grip tilted back by ang (rotation around X)
        vec3 dir = normalize(vec3(0, -sinf(ang), -cosf(ang)));
        vec3 c = base + dir * len * 0.5f;
        obox(m, c, cross(vec3(1, 0, 0), dir) * -1.f, dir * -1.f, he, col, mat);
    };
    switch (w) {
        case WPN_PISTOL: {
            obox(m, vec3(0, 0.075f, 0.055f), F, U, vec3(0.013f, 0.095f, 0.017f), gun, matMetal);        // slide
            obox(m, vec3(0, 0.07f, 0.03f), F, U, vec3(0.012f, 0.085f, 0.012f), poly, matPlastic);       // frame
            cylinderAB(m, vec3(0, 0.16f, 0.055f), vec3(0, 0.175f, 0.055f), 0.006f, 0.006f, 8, black, matSteel);
            grip(vec3(0, 0.0f, 0.03f), 0.11f, 0.3f, vec3(0.014f, 0.017f, 0.055f), poly, matPlastic);
            obox(m, vec3(0, 0.035f, 0.005f), F, U, vec3(0.004f, 0.025f, 0.003f), poly, matPlastic);    // trigger guard
            obox(m, vec3(0, 0.16f, 0.075f), F, U, vec3(0.003f, 0.004f, 0.004f), black, matMetal);      // sight
            break;
        }
        case WPN_REVOLVER: {
            cylinderAB(m, vec3(0, 0.05f, 0.05f), vec3(0, 0.22f, 0.05f), 0.009f, 0.009f, 10, steel, matSteel);  // barrel
            obox(m, vec3(0, 0.13f, 0.06f), F, U, vec3(0.006f, 0.09f, 0.006f), steel, matSteel);                 // rib
            cylinderAB(m, vec3(0, 0.02f, 0.045f), vec3(0, 0.065f, 0.045f), 0.02f, 0.02f, 12, steel, matSteel);  // drum
            obox(m, vec3(0, 0.01f, 0.03f), F, U, vec3(0.011f, 0.03f, 0.022f), steel, matSteel);                 // frame
            grip(vec3(0, -0.01f, 0.02f), 0.1f, 0.45f, vec3(0.013f, 0.016f, 0.05f), wood, matWood);
            break;
        }
        case WPN_SMG: {
            obox(m, vec3(0, 0.08f, 0.05f), F, U, vec3(0.02f, 0.13f, 0.03f), gun, matMetal);            // receiver
            cylinderAB(m, vec3(0, 0.2f, 0.055f), vec3(0, 0.27f, 0.055f), 0.01f, 0.01f, 10, black, matSteel);
            obox(m, vec3(0, 0.07f, -0.04f), vec3(0, 1, -0.1f), U, vec3(0.012f, 0.018f, 0.07f), black, matMetal);  // magazine
            grip(vec3(0, -0.02f, 0.02f), 0.1f, 0.25f, vec3(0.014f, 0.018f, 0.05f), poly, matPlastic);
            obox(m, vec3(0, -0.12f, 0.045f), F, U, vec3(0.008f, 0.09f, 0.012f), black, matMetal);     // stock bar
            obox(m, vec3(0, -0.21f, 0.03f), F, U, vec3(0.014f, 0.012f, 0.04f), black, matMetal);      // butt
            obox(m, vec3(0, 0.1f, 0.085f), F, U, vec3(0.006f, 0.05f, 0.006f), black, matMetal);        // rail
            break;
        }
        case WPN_RIFLE: {
            obox(m, vec3(0, 0.07f, 0.05f), F, U, vec3(0.022f, 0.16f, 0.035f), gun, matMetal);          // receiver
            obox(m, vec3(0, 0.3f, 0.055f), F, U, vec3(0.024f, 0.09f, 0.026f), black, matPlastic);      // handguard
            cylinderAB(m, vec3(0, 0.38f, 0.055f), vec3(0, 0.56f, 0.055f), 0.008f, 0.008f, 10, black, matSteel);
            cylinderAB(m, vec3(0, 0.56f, 0.055f), vec3(0, 0.6f, 0.055f), 0.012f, 0.011f, 10, black, matSteel);  // muzzle brake
            obox(m, vec3(0, 0.1f, -0.045f), vec3(0, 1, 0.25f), U, vec3(0.013f, 0.022f, 0.07f), black, matMetal);  // curved mag
            grip(vec3(0, -0.02f, 0.02f), 0.1f, 0.3f, vec3(0.014f, 0.018f, 0.05f), poly, matPlastic);
            obox(m, vec3(0, -0.2f, 0.035f), vec3(0, 1, 0.12f), U, vec3(0.018f, 0.13f, 0.035f), poly, matPlastic);  // stock
            obox(m, vec3(0, 0.12f, 0.1f), F, U, vec3(0.012f, 0.06f, 0.012f), black, matMetal);          // optic body
            cylinderAB(m, vec3(0, 0.06f, 0.11f), vec3(0, 0.18f, 0.11f), 0.016f, 0.016f, 12, black, matMetal);
            break;
        }
        case WPN_SHOTGUN: {
            cylinderAB(m, vec3(0, 0.05f, 0.06f), vec3(0, 0.62f, 0.06f), 0.011f, 0.011f, 10, gun, matSteel);  // barrel
            cylinderAB(m, vec3(0, 0.08f, 0.037f), vec3(0, 0.55f, 0.037f), 0.01f, 0.01f, 10, gun, matSteel);  // tube
            obox(m, vec3(0, 0.3f, 0.037f), F, U, vec3(0.02f, 0.07f, 0.02f), wood, matWood);             // pump
            obox(m, vec3(0, 0.02f, 0.045f), F, U, vec3(0.02f, 0.07f, 0.03f), gun, matMetal);            // receiver
            obox(m, vec3(0, -0.19f, 0.02f), vec3(0, 1, 0.2f), U, vec3(0.02f, 0.16f, 0.04f), wood, matWood);  // stock
            break;
        }
        case WPN_SNIPER: {
            cylinderAB(m, vec3(0, 0.1f, 0.06f), vec3(0, 0.78f, 0.06f), 0.011f, 0.008f, 10, black, matSteel);
            obox(m, vec3(0, 0.05f, 0.045f), F, U, vec3(0.024f, 0.15f, 0.03f), olive, matPlastic);       // chassis
            obox(m, vec3(0, -0.22f, 0.03f), vec3(0, 1, 0.15f), U, vec3(0.02f, 0.14f, 0.045f), olive, matPlastic);
            cylinderAB(m, vec3(0, -0.03f, 0.115f), vec3(0, 0.2f, 0.115f), 0.02f, 0.02f, 14, black, matMetal);   // scope
            cylinderAB(m, vec3(0, 0.2f, 0.115f), vec3(0, 0.25f, 0.115f), 0.02f, 0.026f, 14, black, matMetal);
            cylinderAB(m, vec3(0, -0.08f, 0.115f), vec3(0, -0.03f, 0.115f), 0.024f, 0.02f, 14, black, matMetal);
            cylinderAB(m, vec3(0.03f, 0.02f, 0.07f), vec3(0.05f, 0.02f, 0.07f), 0.006f, 0.006f, 6, steel, matSteel);  // bolt
            obox(m, vec3(0, 0.03f, -0.02f), F, U, vec3(0.012f, 0.03f, 0.03f), black, matMetal);        // mag
            grip(vec3(0, -0.05f, 0.02f), 0.09f, 0.35f, vec3(0.014f, 0.018f, 0.045f), olive, matPlastic);
            break;
        }
        case WPN_RPG: {
            cylinderAB(m, vec3(0, -0.45f, 0.08f), vec3(0, 0.55f, 0.08f), 0.042f, 0.042f, 14, olive, matMetal);
            cylinderAB(m, vec3(0, 0.55f, 0.08f), vec3(0, 0.68f, 0.08f), 0.05f, 0.05f, 12, olive, matMetal);      // warhead
            cylinderAB(m, vec3(0, 0.68f, 0.08f), vec3(0, 0.82f, 0.08f), 0.05f, 0.0f, 12, olive, matMetal);
            grip(vec3(0, 0.0f, 0.04f), 0.1f, 0.2f, vec3(0.014f, 0.018f, 0.05f), black, matPlastic);
            grip(vec3(0, 0.22f, 0.04f), 0.1f, 0.0f, vec3(0.014f, 0.018f, 0.05f), black, matPlastic);
            obox(m, vec3(-0.05f, 0.1f, 0.12f), F, U, vec3(0.012f, 0.04f, 0.02f), black, matMetal);        // sight
            break;
        }
        case WPN_GRENADE: {
            sphere(m, vec3(0, 0, 0.04f), 0.032f, 12, olive, matMetal);
            cylinderAB(m, vec3(0, 0, 0.07f), vec3(0, 0, 0.09f), 0.012f, 0.012f, 8, steel, matSteel);
            obox(m, vec3(0, 0.02f, 0.06f), vec3(0, 0.3f, -1), vec3(0, 1, 0), vec3(0.006f, 0.002f, 0.035f), steel, matSteel);
            break;
        }
        case WPN_MOLOTOV: {
            u32 glass = packRGBA8(0.25f, 0.45f, 0.2f, 1), rag = packRGBA8(0.75f, 0.7f, 0.6f, 1);
            cylinderAB(m, vec3(0, 0, 0.0f), vec3(0, 0, 0.14f), 0.035f, 0.035f, 12, glass, makeMat(MAT_GLASS));
            cylinderAB(m, vec3(0, 0, 0.14f), vec3(0, 0, 0.2f), 0.035f, 0.012f, 12, glass, makeMat(MAT_GLASS));
            cylinderAB(m, vec3(0, 0, 0.2f), vec3(0, 0.02f, 0.26f), 0.014f, 0.008f, 6, rag, makeMat(MAT_FABRIC));
            break;
        }
        case WPN_KNIFE: {
            obox(m, vec3(0, 0.0f, 0.0f), F, U, vec3(0.012f, 0.055f, 0.014f), black, matPlastic);          // handle
            obox(m, vec3(0, 0.1f, 0.004f), F, U, vec3(0.0025f, 0.055f, 0.013f), steel, makeMat(MAT_CHROME));
            obox(m, vec3(0, 0.057f, 0.0f), F, U, vec3(0.018f, 0.004f, 0.02f), black, matMetal);          // guard
            break;
        }
        case WPN_BAT: {
            cylinderAB(m, vec3(0, -0.08f, 0), vec3(0, 0.25f, 0), 0.016f, 0.02f, 10, tan, matWood);
            cylinderAB(m, vec3(0, 0.25f, 0), vec3(0, 0.78f, 0), 0.02f, 0.036f, 12, tan, matWood);
            cylinderAB(m, vec3(0, -0.1f, 0), vec3(0, -0.08f, 0), 0.024f, 0.024f, 10, tan, matWood);
            break;
        }
        default: break;
    }
}

// Ram-air canopy (cells along X), suspension lines to the harness at the origin. Canopy ~6.5 m above the harness.
void buildParachuteMesh(MeshData& m) {
    const int cells = 9, chord = 5;
    const float span = 8.2f, depth = 3.2f, height = 6.4f;
    u32 colA = packRGBA8(0.95f, 0.22f, 0.45f, 1), colB = packRGBA8(0.12f, 0.12f, 0.16f, 1);
    u32 mat = makeMat(MAT_FABRIC);
    auto surf = [&](float u, float v, float thick) {
        float x = (u - 0.5f) * span;
        float arch = 1.f - (2.f * u - 1.f) * (2.f * u - 1.f);   // anhedral arch
        float z = height - 1.3f * (1.f - arch);
        float y = (v - 0.4f) * depth;
        float camber = sinf(v * kPi) * 0.35f;
        return vec3(x * (0.92f + 0.08f * arch), y, z + camber + thick);
    };
    for (int layer = 0; layer < 2; layer++) {
        float th = layer ? -0.28f : 0.f;
        u32 start = (u32)m.verts.size();
        for (int j = 0; j <= chord; j++)
            for (int i = 0; i <= cells * 2; i++) {
                float u = (float)i / (cells * 2), v = (float)j / chord;
                vec3 p = surf(u, v, th);
                vec3 du = surf(Min(u + 0.01f, 1.f), v, th) - surf(Max(u - 0.01f, 0.f), v, th);
                vec3 dv = surf(u, Min(v + 0.01f, 1.f), th) - surf(u, Max(v - 0.01f, 0.f), th);
                vec3 n = normalize(cross(du, dv));
                if (layer) n = -n;
                u32 col = ((i / 2) & 1) ? colA : colB;
                m.addVertex(p, n, normalize(du), vec2(u * span, v * depth), col, mat);
            }
        int W = cells * 2 + 1;
        for (int j = 0; j < chord; j++)
            for (int i = 0; i < cells * 2; i++) {
                u32 a = start + j * W + i, b = a + 1, c = a + W, d = c + 1;
                if (layer) m.quadIdx(a, c, d, b);
                else m.quadIdx(a, b, d, c);
            }
    }
    // suspension lines
    u32 line = packRGBA8(0.85f, 0.85f, 0.85f, 1);
    for (int i = 0; i <= cells; i += 3)
        for (int j = 1; j < chord; j += 2) {
            vec3 top = surf((float)i / cells, (float)j / chord, -0.28f);
            vec3 bot = vec3(top.x > 0 ? 0.2f : -0.2f, 0.f, 1.5f);
            asset_detail::cylinderAB(m, bot, top, 0.012f, 0.012f, 3, line, makeMat(MAT_PLASTIC), false);
        }
}

void buildPickupMesh(int type, MeshData& m) {
    switch (type) {
        case PICK_MONEY: {
            u32 green = packRGBA8(0.35f, 0.55f, 0.32f, 1), band = packRGBA8(0.85f, 0.8f, 0.6f, 1);
            for (int i = 0; i < 3; i++) {
                vec3 c(i * 0.02f - 0.02f, (i - 1) * 0.015f, 0.02f + i * 0.036f);
                obox(m, c, vec3(sinf(i * 0.4f), cosf(i * 0.4f), 0), vec3(0, 0, 1), vec3(0.075f, 0.035f, 0.018f), green, makeMat(MAT_FABRIC));
                obox(m, c, vec3(sinf(i * 0.4f), cosf(i * 0.4f), 0), vec3(0, 0, 1), vec3(0.012f, 0.036f, 0.019f), band, makeMat(MAT_FABRIC));
            }
            break;
        }
        case PICK_HEALTH: {
            u32 white = packRGBA8(0.9f, 0.9f, 0.9f, 1), red = packRGBA8(0.8f, 0.05f, 0.05f, 1);
            m.boxAA(vec3(-0.16f, -0.1f, 0), vec3(0.16f, 0.1f, 0.2f), white, makeMat(MAT_PLASTIC));
            m.boxAA(vec3(-0.035f, -0.101f, 0.03f), vec3(0.035f, 0.101f, 0.17f), red, makeMat(MAT_PLASTIC));
            m.boxAA(vec3(-0.1f, -0.102f, 0.065f), vec3(0.1f, 0.102f, 0.135f), red, makeMat(MAT_PLASTIC));
            obox(m, vec3(0, 0, 0.22f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.06f, 0.012f, 0.02f), packRGBA8(0.1f, 0.1f, 0.1f, 1), makeMat(MAT_PLASTIC));
            break;
        }
        case PICK_ARMOR: {
            u32 navy = packRGBA8(0.08f, 0.1f, 0.16f, 1);
            obox(m, vec3(0, 0, 0.25f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.19f, 0.06f, 0.24f), navy, makeMat(MAT_FABRIC));
            obox(m, vec3(-0.12f, 0, 0.52f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.05f, 0.05f, 0.04f), navy, makeMat(MAT_FABRIC));
            obox(m, vec3(0.12f, 0, 0.52f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.05f, 0.05f, 0.04f), navy, makeMat(MAT_FABRIC));
            break;
        }
        case PICK_COLLECTIBLE: {
            // Neon seashell token (original collectible): spiral of glowing segments
            u32 glow = packRGBA8(1.0f, 0.3f, 0.8f, 1.f);
            for (int i = 0; i < 9; i++) {
                float a = i * 0.7f, r = 0.05f + i * 0.018f;
                vec3 c(cosf(a) * r * 0.6f, 0, 0.25f + sinf(a) * r * 0.6f);
                sphere(m, c, 0.03f + i * 0.006f, 10, glow, makeMat(MAT_EMISSIVE));
            }
            break;
        }
        case PICK_PACKAGE: {
            u32 card = packRGBA8(0.55f, 0.4f, 0.24f, 1), tape = packRGBA8(0.75f, 0.65f, 0.4f, 1);
            m.boxAA(vec3(-0.2f, -0.15f, 0), vec3(0.2f, 0.15f, 0.25f), card, makeMat(MAT_FABRIC));
            m.boxAA(vec3(-0.03f, -0.151f, 0.0f), vec3(0.03f, 0.151f, 0.251f), tape, makeMat(MAT_FABRIC));
            break;
        }
        default: break;
    }
}

// Smartphone held by peds (calls, idle scrolling): origin at the centre, long axis +Y, screen facing +Z.
void buildPhoneMesh(MeshData& m) {
    const vec3 Y(0, 1, 0), Z(0, 0, 1);
    u32 frame = packRGBA8(0.16f, 0.17f, 0.19f, 1), back = packRGBA8(0.07f, 0.08f, 0.1f, 1);
    obox(m, vec3(0.f), Y, Z, vec3(0.0355f, 0.0735f, 0.0038f), frame, makeMat(MAT_METAL_BRUSHED));
    obox(m, vec3(0.f, 0.f, -0.0039f), Y, Z, vec3(0.0345f, 0.0725f, 0.0004f), back, makeMat(MAT_PLASTIC));
    obox(m, vec3(-0.017f, 0.052f, -0.0048f), Y, Z, vec3(0.012f, 0.014f, 0.0011f), frame, makeMat(MAT_METAL_BRUSHED));
    sphere(m, vec3(-0.021f, 0.057f, -0.0058f), 0.0042f, 8, packRGBA8(0.02f, 0.02f, 0.03f, 1), makeMat(MAT_CAR_GLASS));
    sphere(m, vec3(-0.013f, 0.047f, -0.0058f), 0.0042f, 8, packRGBA8(0.02f, 0.02f, 0.03f, 1), makeMat(MAT_CAR_GLASS));
    // lit screen (a cool, dim social-feed white; alpha scales the emission)
    obox(m, vec3(0.f, 0.f, 0.0039f), Y, Z, vec3(0.0335f, 0.071f, 0.0003f), packRGBA8(0.62f, 0.68f, 0.8f, 0.55f), makeMat(MAT_EMISSIVE));
}

}  // namespace asset_detail

using namespace asset_detail;

void tintWeaponMesh(MeshData& m, int tint);                            // weaponmods.cpp
void buildWeaponCompMesh(WeaponType w, int compBit, MeshData& m);

void GameWorld::buildAssets() {
    double t0 = TimeSeconds();
    Render::DynamicRenderer* dyn = renderer->dynamic;
#ifdef HAVE_VEHICLE_MODELS
    int nv = Vehicles::modelCount();
    vassets.resize(nv);
    Jobs::parallelFor(nv, [&](int i) { Vehicles::buildModel(i, vassets[i].spec); });
    // lower detail versions for distant traffic (cheaper shells, merged wheels at LOD2)
    std::vector<MeshData> vlods((size_t)nv * 2), wlods((size_t)nv);
    Jobs::parallelFor(nv, [&](int i) { Vehicles::buildVehicleLods(i, &vlods[(size_t)i * 2], &wlods[(size_t)i]); });
    for (int i = 0; i < nv; i++) {
        VehicleAsset& a = vassets[i];
        for (int l = 0; l < 2; l++)
            a.bodyLod[l] = vlods[(size_t)i * 2 + l].empty() ? nullptr : dyn->createModel(vlods[(size_t)i * 2 + l]);
        a.wheelLod1 = wlods[(size_t)i].empty() ? nullptr : dyn->createModel(wlods[(size_t)i]);
        a.body = dyn->createModel(a.spec.body);
        a.wheel = a.spec.wheel.empty() ? nullptr : dyn->createModel(a.spec.wheel);
        a.rotor = a.spec.rotor.empty() ? nullptr : dyn->createModel(a.spec.rotor);
        a.tailRotor = a.spec.tailRotor.empty() ? nullptr : dyn->createModel(a.spec.tailRotor);
        // free CPU copies of the meshes (metadata stays)
        a.spec.body.clear();
        a.spec.wheel.clear();
        a.spec.rotor.clear();
        a.spec.tailRotor.clear();
    }
    LOG("Vehicle assets: %d models in %.2f s", nv, TimeSeconds() - t0);
#endif
#ifdef HAVE_CHARACTERS
    double t1 = TimeSeconds();
    // Roster: civilians of every kind, police, two gangs, medics, security, and the two protagonists.
    struct Req {
        u32 seed;
        int role;
        int forceGender;  // -1 any
    };
    std::vector<Req> reqs;
    for (int i = 0; i < 44; i++) reqs.push_back({0x1000u + (u32)i * 7919u, (i % 11 == 3) ? 3 : (i % 11 == 7 ? 5 : 0), i & 1});
    for (int i = 0; i < 10; i++) reqs.push_back({0x2000u + (u32)i * 104729u, 4, i & 1});       // beach
    for (int i = 0; i < 10; i++) reqs.push_back({0x3000u + (u32)i * 15485863u, 1, i % 4 == 3 ? 1 : 0});  // police
    for (int i = 0; i < 8; i++) reqs.push_back({0x4000u + (u32)i * 32452843u, 2, i % 5 == 4 ? 1 : 0});   // gang (Cuervos)
    for (int i = 0; i < 8; i++) reqs.push_back({0x5000u + (u32)i * 49979687u, 2, i % 5 == 2 ? 1 : 0});   // gang (Saints)
    for (int i = 0; i < 4; i++) reqs.push_back({0x6000u + (u32)i * 67867967u, 6, i & 1});      // medics
    int protoStart = (int)reqs.size();
    reqs.push_back({0xA11CEu, 0, 1});   // protagonist: Marisol "Mari" Ortega
    reqs.push_back({0xDE7u, 0, 0});     // protagonist: Dex Calloway
    int n = (int)reqs.size();
    chars.resize(n);
    std::vector<SkinnedMeshData> meshes((size_t)n * 3);   // LOD0..2 per character
    Jobs::parallelFor(n, [&](int i) {
        const Req& r = reqs[i];
        u32 seed = r.seed;
        Anim::CharacterDesc d = Anim::randomCharacter(seed, r.role);
        // re-roll the seed deterministically until the requested gender is produced
        for (int k = 0; k < 24 && r.forceGender >= 0 && (int)d.gender != r.forceGender; k++) {
            seed = hash32(seed + 0x9e37u);
            d = Anim::randomCharacter(seed, r.role);
        }
        if (i >= protoStart) {
            if (i == protoStart) {  // Mari: late 20s, athletic, dark wavy hair, casual street style
                d.gender = Anim::FEMALE;
                d.height = 1.68f;
                d.weight = 0.35f;
                d.muscle = 0.5f;
                d.age = 0.15f;
                d.skinTone = vec3(0.62f, 0.44f, 0.33f);
                d.hairColor = vec3(0.06f, 0.04f, 0.03f);
                d.topColor = vec3(0.85f, 0.2f, 0.45f);
                d.bottomColor = vec3(0.12f, 0.14f, 0.2f);
                d.shoeColor = vec3(0.9f, 0.9f, 0.88f);
            } else {                // Dex: early 30s, broad, short hair, stubble, work jacket
                d.gender = Anim::MALE;
                d.height = 1.84f;
                d.weight = 0.55f;
                d.muscle = 0.7f;
                d.age = 0.25f;
                d.skinTone = vec3(0.72f, 0.56f, 0.45f);
                d.hairColor = vec3(0.2f, 0.13f, 0.08f);
                d.topColor = vec3(0.24f, 0.3f, 0.22f);
                d.bottomColor = vec3(0.16f, 0.18f, 0.24f);
                d.shoeColor = vec3(0.25f, 0.17f, 0.1f);
            }
        }
        chars[i].desc = d;
        chars[i].role = r.role;
        Anim::buildSkeleton(d, chars[i].skel);
        Anim::buildCharacterMeshLods(d, chars[i].skel, &meshes[(size_t)i * 3], 3);
    });
    for (int i = 0; i < n; i++) {
        chars[i].model = dyn->createSkinnedModel(meshes[(size_t)i * 3]);
        for (int l = 0; l < 2; l++)
            chars[i].lods[l] = meshes[(size_t)i * 3 + 1 + l].indices.empty() ? nullptr : dyn->createSkinnedModel(meshes[(size_t)i * 3 + 1 + l]);
        int role = Clamp(chars[i].role, 0, 7);
        if (i < protoStart) {
            charsByRole[role].push_back(i);
            if (role == 0 || role == 3 || role == 5) (chars[i].desc.gender == Anim::FEMALE ? charsFemaleCivil : charsMaleCivil).push_back(i);
        }
    }
    protagonistChar[0] = protoStart;
    protagonistChar[1] = protoStart + 1;
    // bake the clip library now (the first sampleClip builds it) instead of hitching on the first spawn in game
    if (n > 0) {
        Anim::Pose warm;
        Anim::sampleClip(chars[0].skel, Anim::CLIP_IDLE, 0.f, warm);
    }
    // Animators keep pointers to CharEntry::skel: reserve room for named story characters so the vector never
    // reallocates after peds exist.
    chars.reserve(chars.size() + 192);
    LOG("Character assets: %d characters in %.2f s", n, TimeSeconds() - t1);
#endif
    for (int w = 1; w < WPN_COUNT; w++) {
        MeshData m;
        buildWeaponMesh((WeaponType)w, m);
        weaponModels[w] = m.empty() ? nullptr : dyn->createModel(m);
        weaponTintModels[w][0] = weaponModels[w];
        if (m.empty() || w == WPN_GRENADE || w == WPN_MOLOTOV || weaponInfo((WeaponType)w).clipSize == 0) continue;
        for (int t = 1; t < kWeaponTints; t++) {   // gun shop tints
            MeshData tm = m;
            tintWeaponMesh(tm, t);
            weaponTintModels[w][t] = dyn->createModel(tm);
        }
        for (int c = 0; c < kWeaponCompCount; c++) {   // attachments, drawn with the weapon's transform
            MeshData cm;
            buildWeaponCompMesh((WeaponType)w, 1 << c, cm);
            weaponCompModels[w][c] = cm.empty() ? nullptr : dyn->createModel(cm);
        }
    }
    for (int p = 0; p < 6; p++) {
        MeshData m;
        buildPickupMesh(p, m);
        pickupModels[p] = m.empty() ? nullptr : dyn->createModel(m);
    }
    {
        MeshData m;
        buildParachuteMesh(m);
        parachuteModel = dyn->createModel(m);
    }
    {
        MeshData m;
        buildPhoneMesh(m);
        phoneModel = dyn->createModel(m);
    }
    LOG("Game assets built in %.2f s", TimeSeconds() - t0);
}

int GameWorld::namedCharacter(const std::string& key, const Anim::CharacterDesc& desc) {
    auto it = namedChars.find(key);
    if (it != namedChars.end()) return it->second;
    if (chars.size() >= chars.capacity()) {
        LOG("namedCharacter: roster full, using a generic character for %s", key.c_str());
        return randomCivilianChar(hashString(key.c_str()), desc.role);
    }
    CharEntry ce;
    ce.desc = desc;
    ce.role = desc.role;
    Anim::buildSkeleton(desc, ce.skel);
    SkinnedMeshData md[3];
    Anim::buildCharacterMeshLods(desc, ce.skel, md, 3);
    ce.model = renderer->dynamic->createSkinnedModel(md[0]);
    for (int l = 0; l < 2; l++) ce.lods[l] = md[1 + l].indices.empty() ? nullptr : renderer->dynamic->createSkinnedModel(md[1 + l]);
    int id = (int)chars.size();
    chars.push_back(ce);
    namedChars[key] = id;
    return id;
}

int GameWorld::randomCivilianChar(u32 seed, int role) {
    if (role != 0 && role < 8 && !charsByRole[role].empty()) return charsByRole[role][seed % charsByRole[role].size()];
    const std::vector<int>& v = (seed & 1) ? charsFemaleCivil : charsMaleCivil;
    if (!v.empty()) return v[(seed >> 1) % v.size()];
    return chars.empty() ? -1 : (int)(seed % chars.size());
}

int GameWorld::findVehicleModel(Vehicles::VehicleClass cls, u32 seed) {
    // prefer civilian models (spawnWeight > 0: excludes police/service variants such as the patrol helicopter);
    // fall back to any model of the class
    for (int pass = 0; pass < 2; pass++) {
        int count = 0;
        for (auto& a : vassets)
            if (a.spec.cls == cls && (pass == 1 || a.spec.spawnWeight > 0.f)) count++;
        if (!count) continue;
        int k = (int)(seed % (u32)count);
        for (int i = 0; i < (int)vassets.size(); i++)
            if (vassets[i].spec.cls == cls && (pass == 1 || vassets[i].spec.spawnWeight > 0.f) && k-- == 0) return i;
    }
    return -1;
}

}  // namespace Game
