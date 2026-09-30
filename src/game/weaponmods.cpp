// Weapon components and tints: which guns take which attachments, prices, the attachment meshes (built in the weapon
// model's frame: grip at the origin, +Y along the barrel, +Z up), tint recolouring, and the gameplay queries (fitted
// components, magazine capacity). The effects live where they apply: combat.cpp (suppressor, grip, scope spread),
// camera.cpp (scope zoom), gameworld.cpp (drawing, flashlight beam), savegame.cpp (persistence).
#include "gameworld.h"

namespace Game {

namespace wmods_detail {

struct CompSpec {
    u8 avail;
    float muzzleY, muzzleZ;   // muzzle point in the weapon frame (suppressors thread on here)
    float boreR;              // suppressor radius
    float suppLen;
};
const CompSpec kSpec[WPN_COUNT] = {
    {0, 0.f, 0.f, 0.f, 0.f},                                                                      // fists
    {0, 0.f, 0.f, 0.f, 0.f},                                                                      // knife
    {0, 0.f, 0.f, 0.f, 0.f},                                                                      // bat
    {WC_SUPPRESSOR | WC_EXTMAG | WC_FLASHLIGHT, 0.175f, 0.055f, 0.0125f, 0.13f},                  // pistol
    {WC_SCOPE, 0.22f, 0.05f, 0.f, 0.f},                                                           // revolver
    {WC_SUPPRESSOR | WC_EXTMAG | WC_SCOPE | WC_FLASHLIGHT | WC_GRIP, 0.27f, 0.055f, 0.016f, 0.15f}, // SMG
    {WC_SUPPRESSOR | WC_EXTMAG | WC_SCOPE | WC_FLASHLIGHT | WC_GRIP, 0.6f, 0.055f, 0.018f, 0.17f},  // rifle
    {WC_SUPPRESSOR | WC_EXTMAG | WC_FLASHLIGHT | WC_GRIP, 0.62f, 0.06f, 0.02f, 0.19f},             // shotgun
    {WC_SUPPRESSOR | WC_EXTMAG, 0.78f, 0.06f, 0.018f, 0.2f},                                      // sniper
    {0, 0.f, 0.f, 0.f, 0.f},                                                                      // RPG
    {0, 0.f, 0.f, 0.f, 0.f},                                                                      // grenade
    {0, 0.f, 0.f, 0.f, 0.f},                                                                      // molotov
};

const char* const kCompNames[kWeaponCompCount] = {"Suppressor", "Extended Magazine", "Scope", "Flashlight", "Grip"};
const char* const kTintNames[kWeaponTints] = {"Stock", "Gold", "Platinum", "Flamingo Pink", "Everglade Green", "Gulf Blue"};
const vec3 kTintColors[kWeaponTints] = {vec3(0.f), vec3(0.8f, 0.6f, 0.2f), vec3(0.7f, 0.71f, 0.73f), vec3(0.9f, 0.4f, 0.58f),
                                        vec3(0.2f, 0.27f, 0.15f), vec3(0.1f, 0.27f, 0.52f)};

int compIndex(int bit) {
    for (int i = 0; i < kWeaponCompCount; i++)
        if (bit == (1 << i)) return i;
    return -1;
}

// weapon class price scale: handguns cheap, long guns dear
float priceScale(WeaponType w) {
    switch (w) {
        case WPN_PISTOL: case WPN_REVOLVER: return 0.7f;
        case WPN_SMG: return 0.9f;
        case WPN_RIFLE: case WPN_SHOTGUN: return 1.1f;
        case WPN_SNIPER: return 1.4f;
        default: return 1.f;
    }
}

}  // namespace wmods_detail

u8 weaponCompsAvailable(WeaponType w) { return (w >= 0 && w < WPN_COUNT) ? wmods_detail::kSpec[w].avail : 0; }

const char* weaponCompName(int compBit) {
    int i = wmods_detail::compIndex(compBit);
    return i >= 0 ? wmods_detail::kCompNames[i] : "";
}

int weaponCompPrice(WeaponType w, int compBit) {
    static const int kBase[kWeaponCompCount] = {1650, 1100, 1450, 350, 650};
    int i = wmods_detail::compIndex(compBit);
    if (i < 0) return 0;
    return (int)(kBase[i] * wmods_detail::priceScale(w) / 50.f + 0.5f) * 50;
}

const char* weaponTintName(int tint) { return wmods_detail::kTintNames[Clamp(tint, 0, kWeaponTints - 1)]; }

int weaponTintPrice(WeaponType w, int tint) {
    static const int kBase[kWeaponTints] = {0, 5200, 3900, 800, 650, 800};
    return (int)(kBase[Clamp(tint, 0, kWeaponTints - 1)] * wmods_detail::priceScale(w) / 50.f + 0.5f) * 50;
}

// ------------------------------------------------------------------------------------------------------------------
// Meshes (declared in assets.cpp, which builds the models at load)

// Dark finishes (receiver, slide, polymer, magazines) take the tint; wood, bright steel and olive furniture keep theirs.
void tintWeaponMesh(MeshData& m, int tint) {
    if (tint <= 0 || tint >= kWeaponTints) return;
    vec3 tc = wmods_detail::kTintColors[tint];
    for (VtxStatic& v : m.verts) {
        vec4 c = unpackRGBA8(v.color);
        float lum = c.x * 0.3f + c.y * 0.59f + c.z * 0.11f;
        if (lum > 0.1f) continue;
        vec3 n = tc * (0.8f + lum * 3.f);
        v.color = packRGBA8(n.x, n.y, n.z, c.w);
    }
}

void buildWeaponCompMesh(WeaponType w, int compBit, MeshData& m) {
    using namespace asset_detail;
    const wmods_detail::CompSpec& sp = wmods_detail::kSpec[w];
    if (!(sp.avail & compBit)) return;
    u32 dark = packRGBA8(0.05f, 0.052f, 0.056f, 1), poly = packRGBA8(0.045f, 0.045f, 0.045f, 1);
    u32 lens = packRGBA8(0.55f, 0.6f, 0.65f, 1), glassC = packRGBA8(0.2f, 0.35f, 0.3f, 1);
    u32 matMetal = makeMat(MAT_METAL_PAINTED), matPlastic = makeMat(MAT_PLASTIC), matGlass = makeMat(MAT_CAR_GLASS);
    vec3 F(0, 1, 0), U(0, 0, 1);
    switch (compBit) {
        case WC_SUPPRESSOR: {
            vec3 a(0.f, sp.muzzleY - 0.012f, sp.muzzleZ), b(0.f, sp.muzzleY + sp.suppLen, sp.muzzleZ);
            cylinderAB(m, a, a + vec3(0, 0.02f, 0), sp.boreR * 0.75f, sp.boreR, 12, dark, matMetal);   // collar
            cylinderAB(m, a + vec3(0, 0.02f, 0), b - vec3(0, 0.01f, 0), sp.boreR, sp.boreR, 14, dark, matMetal);
            cylinderAB(m, b - vec3(0, 0.01f, 0), b, sp.boreR, sp.boreR * 0.8f, 14, dark, matMetal);        // end cap
            break;
        }
        case WC_EXTMAG: {
            switch (w) {
                case WPN_PISTOL: {   // longer magazine protruding below the tilted grip
                    vec3 dir = normalize(vec3(0, -sinf(0.3f), -cosf(0.3f)));
                    vec3 c = vec3(0, 0.f, 0.03f) + dir * 0.13f;
                    obox(m, c, cross(vec3(1, 0, 0), dir) * -1.f, dir * -1.f, vec3(0.0125f, 0.015f, 0.024f), poly, matPlastic);
                    break;
                }
                case WPN_SMG: obox(m, vec3(0, 0.08f, -0.145f), vec3(0, 1, -0.1f), U, vec3(0.012f, 0.018f, 0.038f), dark, matMetal); break;
                case WPN_RIFLE:   // drum magazine
                    cylinderAB(m, vec3(-0.032f, 0.1f, -0.1f), vec3(0.032f, 0.1f, -0.1f), 0.056f, 0.056f, 18, dark, matMetal);
                    obox(m, vec3(0, 0.1f, -0.045f), vec3(0, 1, 0.25f), U, vec3(0.013f, 0.022f, 0.03f), dark, matMetal);
                    break;
                case WPN_SHOTGUN:   // magazine tube extended to the muzzle, side saddle with spare shells
                    cylinderAB(m, vec3(0, 0.55f, 0.037f), vec3(0, 0.615f, 0.037f), 0.01f, 0.01f, 10, dark, matMetal);
                    obox(m, vec3(0.024f, 0.02f, 0.045f), F, U, vec3(0.005f, 0.045f, 0.018f), poly, matPlastic);
                    for (int k = 0; k < 4; k++)
                        cylinderAB(m, vec3(0.03f, -0.015f + k * 0.023f, 0.03f), vec3(0.03f, -0.015f + k * 0.023f, 0.062f), 0.009f, 0.009f, 8,
                                   packRGBA8(0.62f, 0.08f, 0.06f, 1), matPlastic);
                    break;
                case WPN_SNIPER: obox(m, vec3(0, 0.03f, -0.066f), F, U, vec3(0.012f, 0.03f, 0.017f), dark, matMetal); break;
                default: break;
            }
            break;
        }
        case WC_SCOPE: {
            float z = w == WPN_REVOLVER ? 0.087f : (w == WPN_SMG ? 0.1f : 0.13f);
            if (w == WPN_SMG) {   // compact reflex sight
                obox(m, vec3(0, 0.1f, 0.093f), F, U, vec3(0.011f, 0.022f, 0.004f), dark, matMetal);
                obox(m, vec3(0, 0.117f, 0.108f), F, U, vec3(0.012f, 0.004f, 0.013f), dark, matMetal);
                obox(m, vec3(0, 0.112f, 0.108f), F, U, vec3(0.0095f, 0.0008f, 0.0105f), glassC, matGlass);
                break;
            }
            float y0 = w == WPN_REVOLVER ? 0.03f : -0.01f, y1 = w == WPN_REVOLVER ? 0.19f : 0.21f;
            float r = w == WPN_REVOLVER ? 0.011f : 0.016f;
            cylinderAB(m, vec3(0, y0, z), vec3(0, y1, z), r, r, 14, dark, matMetal);
            cylinderAB(m, vec3(0, y1, z), vec3(0, y1 + 0.03f, z), r, r * 1.35f, 14, dark, matMetal);   // objective bell
            cylinderAB(m, vec3(0, y0 - 0.025f, z), vec3(0, y0, z), r * 1.2f, r, 14, dark, matMetal);   // eyepiece
            cylinderAB(m, vec3(0, y1 + 0.027f, z), vec3(0, y1 + 0.029f, z), r * 1.28f, r * 1.28f, 14, lens, matGlass);   // lens
            for (int k = 0; k < 2; k++) {   // rings and mounts
                float y = Lerp(y0 + 0.02f, y1 - 0.02f, (float)k);
                obox(m, vec3(0, y, (z + sp.muzzleZ) * 0.5f + 0.01f), F, U, vec3(0.006f, 0.008f, (z - sp.muzzleZ) * 0.5f), dark, matMetal);
            }
            break;
        }
        case WC_FLASHLIGHT: {
            vec3 a, b;
            float r = 0.011f;
            switch (w) {
                case WPN_PISTOL: a = vec3(0, 0.1f, 0.012f), b = vec3(0, 0.158f, 0.012f); break;
                case WPN_SMG: a = vec3(0.024f, 0.17f, 0.05f), b = vec3(0.024f, 0.235f, 0.05f), r = 0.01f; break;
                case WPN_RIFLE: a = vec3(0, 0.3f, 0.016f), b = vec3(0, 0.375f, 0.016f), r = 0.012f; break;
                case WPN_SHOTGUN: a = vec3(0, 0.45f, 0.012f), b = vec3(0, 0.52f, 0.012f); break;
                default: return;
            }
            cylinderAB(m, a, b, r, r, 12, dark, matMetal);
            cylinderAB(m, b, b + vec3(0, 0.012f, 0), r, r * 1.25f, 12, dark, matMetal);
            cylinderAB(m, b + vec3(0, 0.0115f, 0), b + vec3(0, 0.0125f, 0), r * 1.1f, r * 1.1f, 12, lens, matGlass);
            break;
        }
        case WC_GRIP: {
            float y = w == WPN_SMG ? 0.16f : 0.3f, top = w == WPN_SMG ? 0.025f : (w == WPN_RIFLE ? 0.029f : 0.017f);
            obox(m, vec3(0, y, top - 0.004f), F, U, vec3(0.009f, 0.018f, 0.005f), dark, matMetal);   // rail clamp
            cylinderAB(m, vec3(0, y, top - 0.008f), vec3(0, y - 0.006f, top - 0.075f), 0.0115f, 0.0105f, 12, poly, matPlastic);
            break;
        }
        default: break;
    }
}

// ------------------------------------------------------------------------------------------------------------------
u8 GameWorld::weaponComps(const Ped& p, WeaponType w) const {
    if (!p.isPlayer || w < 0 || w >= WPN_COUNT) return 0;
    return pinfo.wpnCompFitted[w] & pinfo.wpnCompOwned[w] & weaponCompsAvailable(w);
}

int GameWorld::clipCapacity(const Ped& p, WeaponType w) const {
    int c = weaponInfo(w).clipSize;
    if (c > 1 && (weaponComps(p, w) & WC_EXTMAG)) c = (c * 8 + 4) / 5;   // +60 %
    return c;
}

}  // namespace Game
