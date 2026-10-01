// Props people carry: a roller suitcase pulled behind, a paper shopping bag, a coffee, a briefcase, an umbrella (open
// in the rain, furled otherwise), a fishing rod, binoculars on a strap, a surfboard under the arm. Meshes are built once
// with the other assets; every frame each prop is placed from the hand's grip frame (Anim::handGrip) or the chest and
// posed the way the object hangs, trails or stands whatever the arm is doing: bags and cases hang plumb from the fist,
// the suitcase's wheels stay on the ground behind the walker, a cup stays upright, the umbrella's canopy rides above
// the head. Pedestrians without a prop of their own put up an umbrella in steady rain (about half of them).
#include "gameworld.h"

namespace Game {

namespace Interiors {
bool isInside();   // interiors_game.cpp: the player is inside an interior
}

namespace carry_detail {

// Suitcase: origin at the wheel axle on the ground, +Z up the case to the telescopic handle's grip at kCaseGrip.
const float kCaseGrip = 1.02f;

void buildSuitcase(MeshData& m, u32 shell) {
    u32 matShell = makeMat(MAT_PLASTIC), matMetal = makeMat(MAT_METAL_BRUSHED), matRubber = makeMat(MAT_RUBBER);
    u32 dark = packRGBA8(0.05f, 0.05f, 0.055f, 1), alu = packRGBA8(0.62f, 0.63f, 0.65f, 1);
    vec3 F(0, 1, 0), U(0, 0, 1);
    obox(m, vec3(0, 0, 0.36f), F, U, vec3(0.2f, 0.12f, 0.31f), shell, matShell);                  // shell
    for (int k = 0; k < 3; k++)                                                                     // moulded ribs
        obox(m, vec3(-0.1f + k * 0.1f, 0.121f, 0.36f), F, U, vec3(0.018f, 0.003f, 0.27f), shell, matShell);
    obox(m, vec3(0, 0, 0.36f), F, U, vec3(0.203f, 0.123f, 0.004f), dark, matRubber);                // zip line
    for (int sx = -1; sx <= 1; sx += 2) {
        cylinderAB(m, vec3(sx * 0.17f, -0.075f, 0.03f), vec3(sx * 0.17f + sx * 0.02f, -0.075f, 0.03f), 0.03f, 0.03f, 10, dark, matRubber);
        obox(m, vec3(sx * 0.15f, -0.075f, 0.06f), F, U, vec3(0.025f, 0.03f, 0.03f), dark, matRubber);   // wheel housings
        cylinderAB(m, vec3(sx * 0.075f, -0.1f, 0.66f), vec3(sx * 0.075f, -0.1f, kCaseGrip), 0.009f, 0.009f, 8, alu, matMetal);   // handle tubes
    }
    obox(m, vec3(0, -0.1f, kCaseGrip), vec3(1, 0, 0), U, vec3(0.1f, 0.016f, 0.014f), dark, matRubber);   // grip bar
    obox(m, vec3(0, 0.0f, 0.675f), F, U, vec3(0.06f, 0.02f, 0.012f), dark, matRubber);            // top carry handle
}

// Shopping bag: origin at the handles' top (in the fist), hanging down -Z
void buildShopBag(MeshData& m, u32 paper, u32 band) {
    u32 matPaper = makeMat(MAT_FABRIC);
    vec3 F(0, 1, 0), U(0, 0, 1);
    obox(m, vec3(0, 0, -0.25f), F, U, vec3(0.08f, 0.16f, 0.17f), paper, matPaper);
    obox(m, vec3(0, 0, -0.14f), F, U, vec3(0.082f, 0.162f, 0.035f), band, matPaper);   // printed band (no brand)
    for (int s = -1; s <= 1; s += 2) {   // twisted paper handles, one per face, meeting in the fist
        obox(m, vec3(s * 0.04f, 0, -0.045f), normalize(vec3(-s * 0.4f, 0, 1)), vec3(0, 1, 0), vec3(0.004f, 0.05f, 0.004f), paper, matPaper);
    }
}

// Coffee: origin at the middle of the cup, +Z up
void buildCoffee(MeshData& m, u32 sleeve) {
    u32 matCup = makeMat(MAT_PLASTIC), matSleeve = makeMat(MAT_FABRIC);
    u32 white = packRGBA8(0.9f, 0.89f, 0.86f, 1), lid = packRGBA8(0.08f, 0.07f, 0.07f, 1);
    cylinderAB(m, vec3(0, 0, -0.07f), vec3(0, 0, 0.065f), 0.031f, 0.041f, 14, white, matCup);
    cylinderAB(m, vec3(0, 0, -0.02f), vec3(0, 0, 0.035f), 0.0365f, 0.0395f, 14, sleeve, matSleeve);
    cylinderAB(m, vec3(0, 0, 0.065f), vec3(0, 0, 0.078f), 0.043f, 0.038f, 14, lid, matCup);
}

// Briefcase: origin at the handle, hanging down -Z, long side along +Y (the walking direction)
void buildBriefcase(MeshData& m, u32 leather) {
    u32 matLeather = makeMat(MAT_LEATHER), matMetal = makeMat(MAT_METAL_BRUSHED);
    u32 gold = packRGBA8(0.7f, 0.58f, 0.3f, 1);
    vec3 F(0, 1, 0), U(0, 0, 1);
    obox(m, vec3(0, 0, -0.2f), F, U, vec3(0.045f, 0.21f, 0.15f), leather, matLeather);
    obox(m, vec3(0, 0, -0.035f), F, U, vec3(0.012f, 0.06f, 0.012f), leather, matLeather);   // handle
    for (int s = -1; s <= 1; s += 2) {
        obox(m, vec3(0, s * 0.055f, -0.047f), F, U, vec3(0.014f, 0.008f, 0.012f), gold, matMetal);   // handle posts
        obox(m, vec3(s * 0.046f, s * 0.13f, -0.07f), F, U, vec3(0.003f, 0.014f, 0.008f), gold, matMetal);   // latches
    }
}

// Umbrella canopy: origin at the top of the shaft, eight gores sloping down to the rim
void buildCanopy(MeshData& m, u32 cloth) {
    u32 matCloth = makeMat(MAT_FABRIC), matMetal = makeMat(MAT_METAL_BRUSHED);
    const int n = 8;
    const float R = 0.52f, drop = 0.2f;
    u32 c0 = m.addVertex(vec3(0, 0, 0.02f), vec3(0, 0, 1), vec3(1, 0, 0), vec2(0.5f, 0.5f), cloth, matCloth);
    for (int k = 0; k <= n; k++) {
        float a = kTwoPi * k / n;
        vec3 p(cosf(a) * R, sinf(a) * R, -drop);
        vec3 nrm = normalize(vec3(cosf(a) * drop, sinf(a) * drop, R));
        m.addVertex(p, nrm, vec3(-sinf(a), cosf(a), 0), vec2(0.5f + cosf(a) * 0.5f, 0.5f + sinf(a) * 0.5f), cloth, matCloth);
    }
    for (int k = 0; k < n; k++) {   // both faces (the underside is seen from below)
        m.tri(c0, c0 + 1 + k, c0 + 2 + k);
        m.tri(c0, c0 + 2 + k, c0 + 1 + k);
    }
    u32 steel = packRGBA8(0.3f, 0.3f, 0.32f, 1);
    cylinderAB(m, vec3(0, 0, 0.0f), vec3(0, 0, 0.07f), 0.008f, 0.004f, 6, steel, matMetal);   // ferrule
    for (int k = 0; k < n; k++) {   // ribs under the cloth
        float a = kTwoPi * (k + 0.5f) / n;
        vec3 tip(cosf(a) * R * 0.97f, sinf(a) * R * 0.97f, -drop - 0.01f);
        cylinderAB(m, vec3(0, 0, -0.015f), tip, 0.0035f, 0.0025f, 4, steel, matMetal, false);
    }
}

// Umbrella shaft: origin at the hand, +Z up to 1 m (drawn scaled to reach the canopy), a crook handle below the fist
void buildShaft(MeshData& m) {
    u32 matMetal = makeMat(MAT_METAL_BRUSHED), matWood = makeMat(MAT_WOOD);
    u32 steel = packRGBA8(0.32f, 0.32f, 0.34f, 1), wood = packRGBA8(0.3f, 0.17f, 0.08f, 1);
    cylinderAB(m, vec3(0, 0, 0.0f), vec3(0, 0, 1.0f), 0.0075f, 0.0075f, 8, steel, matMetal);
    cylinderAB(m, vec3(0, 0, -0.06f), vec3(0, 0, 0.04f), 0.014f, 0.014f, 10, wood, matWood);
}

// Furled umbrella: origin at the hand, hanging down as a walking stick would (-Z), the crook above the fist
void buildFurled(MeshData& m, u32 cloth) {
    u32 matCloth = makeMat(MAT_FABRIC), matWood = makeMat(MAT_WOOD), matMetal = makeMat(MAT_METAL_BRUSHED);
    u32 wood = packRGBA8(0.3f, 0.17f, 0.08f, 1), steel = packRGBA8(0.32f, 0.32f, 0.34f, 1);
    cylinderAB(m, vec3(0, 0, -0.1f), vec3(0, 0, -0.62f), 0.028f, 0.012f, 10, cloth, matCloth);
    cylinderAB(m, vec3(0, 0, -0.62f), vec3(0, 0, -0.8f), 0.006f, 0.005f, 6, steel, matMetal);
    cylinderAB(m, vec3(0, 0, -0.1f), vec3(0, 0, 0.03f), 0.012f, 0.012f, 8, wood, matWood);
    cylinderAB(m, vec3(0, 0, 0.03f), vec3(0, 0.05f, 0.07f), 0.011f, 0.011f, 8, wood, matWood);   // crook
}

// Fishing rod: origin at the grip, the rod along +Y from the butt behind the hand to the tip 2.1 m out
void buildRod(MeshData& m) {
    u32 matRod = makeMat(MAT_PLASTIC), matMetal = makeMat(MAT_METAL_BRUSHED), matRubber = makeMat(MAT_RUBBER);
    u32 blank = packRGBA8(0.12f, 0.14f, 0.2f, 1), cork = packRGBA8(0.55f, 0.42f, 0.27f, 1), silver = packRGBA8(0.7f, 0.7f, 0.72f, 1);
    cylinderAB(m, vec3(0, -0.34f, 0), vec3(0, 0.06f, 0), 0.014f, 0.012f, 8, cork, matRubber);       // handle
    cylinderAB(m, vec3(0, 0.06f, 0), vec3(0, 2.1f, 0), 0.008f, 0.0022f, 6, blank, matRod);          // blank
    cylinderAB(m, vec3(0, 0.02f, -0.02f), vec3(0, 0.02f, -0.075f), 0.004f, 0.004f, 6, silver, matMetal);   // reel foot
    cylinderAB(m, vec3(-0.025f, 0.02f, -0.09f), vec3(0.025f, 0.02f, -0.09f), 0.03f, 0.03f, 12, silver, matMetal);   // spool
    for (int k = 0; k < 5; k++) {   // line guides
        float y = 0.35f + k * 0.35f;
        cylinderAB(m, vec3(0, y, -0.004f), vec3(0, y, -0.03f + k * 0.003f), 0.0025f, 0.0025f, 4, silver, matMetal, false);
    }
}

// Binoculars: origin at the chest where they hang on the strap, barrels along +Y
void buildBinoculars(MeshData& m) {
    u32 matBody = makeMat(MAT_RUBBER), matGlass = makeMat(MAT_GLASS);
    u32 body = packRGBA8(0.08f, 0.09f, 0.08f, 1), lens = packRGBA8(0.1f, 0.14f, 0.2f, 1);
    for (int s = -1; s <= 1; s += 2) {
        cylinderAB(m, vec3(s * 0.035f, -0.06f, 0), vec3(s * 0.035f, 0.08f, 0), 0.026f, 0.03f, 12, body, matBody);
        cylinderAB(m, vec3(s * 0.035f, 0.08f, 0), vec3(s * 0.035f, 0.081f, 0), 0.024f, 0.024f, 12, lens, matGlass);
    }
    obox(m, vec3(0, 0.0f, 0.012f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.02f, 0.05f, 0.012f), body, matBody);   // hinge bridge
}

// Surfboard: origin at its middle, length along +Y (2.0 m), width along +Z, thickness along X; nose rocker and fins
void buildSurfboard(MeshData& m, u32 deck, u32 stripe) {
    u32 matBoard = makeMat(MAT_PLASTIC);
    const int n = 14;
    const float L = 1.0f, W = 0.26f, T = 0.035f;
    for (int i = 0; i < n; i++) {   // stations along the board: outline narrows to the nose and the tail
        float y0 = -L + 2.f * L * i / n, y1 = -L + 2.f * L * (i + 1) / n;
        auto half = [&](float y) {
            float t = (y + L) / (2.f * L);
            return W * sqrtf(Max(0.f, sinf(kPi * Clamp(t * 0.96f + 0.02f, 0.f, 1.f))));
        };
        auto rock = [&](float y) { return y > 0.6f ? (y - 0.6f) * (y - 0.6f) * 0.5f : 0.f; };   // nose kick
        float w0 = Max(half(y0), 0.02f), w1 = Max(half(y1), 0.02f), r0 = rock(y0), r1 = rock(y1);
        u32 col = (i == n / 2 || i == n / 2 - 1) ? stripe : deck;
        obox(m, vec3(0, (y0 + y1) * 0.5f, (r0 + r1) * 0.5f), normalize(vec3(0, y1 - y0, r1 - r0)), vec3(0, 0, 1),
             vec3(T, (y1 - y0) * 0.5f + 0.002f, (w0 + w1) * 0.5f), col, matBoard);
    }
    obox(m, vec3(-0.06f, -0.85f, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.05f, 0.05f, 0.004f), stripe, matBoard);   // fin
}

}  // namespace carry_detail

void GameWorld::loadCarryModels() {
    using namespace carry_detail;
    Render::DynamicRenderer* dyn = renderer->dynamic;
    auto make = [&](MeshData& m) { return m.empty() ? nullptr : dyn->createModel(m); };
    MeshData m;
    buildSuitcase(m, packRGBA8(0.12f, 0.2f, 0.34f, 1));
    carryModels[CARRY_SUITCASE] = make(m);
    m.clear();
    buildSuitcase(m, packRGBA8(0.55f, 0.1f, 0.12f, 1));
    carryAlt[CARRY_SUITCASE] = make(m);
    m.clear();
    buildShopBag(m, packRGBA8(0.62f, 0.47f, 0.3f, 1), packRGBA8(0.1f, 0.35f, 0.33f, 1));
    carryModels[CARRY_SHOPBAG] = make(m);
    m.clear();
    buildShopBag(m, packRGBA8(0.92f, 0.91f, 0.88f, 1), packRGBA8(0.8f, 0.2f, 0.45f, 1));
    carryAlt[CARRY_SHOPBAG] = make(m);
    m.clear();
    buildCoffee(m, packRGBA8(0.45f, 0.3f, 0.18f, 1));
    carryModels[CARRY_COFFEE] = make(m);
    m.clear();
    buildCoffee(m, packRGBA8(0.2f, 0.45f, 0.35f, 1));
    carryAlt[CARRY_COFFEE] = make(m);
    m.clear();
    buildBriefcase(m, packRGBA8(0.07f, 0.05f, 0.04f, 1));
    carryModels[CARRY_BRIEFCASE] = make(m);
    m.clear();
    buildBriefcase(m, packRGBA8(0.3f, 0.17f, 0.08f, 1));
    carryAlt[CARRY_BRIEFCASE] = make(m);
    m.clear();
    buildCanopy(m, packRGBA8(0.05f, 0.05f, 0.06f, 1));
    carryModels[CARRY_UMBRELLA] = make(m);
    m.clear();
    buildCanopy(m, packRGBA8(0.1f, 0.22f, 0.45f, 1));
    carryAlt[CARRY_UMBRELLA] = make(m);
    m.clear();
    buildShaft(m);
    umbrellaShaft = make(m);
    m.clear();
    buildFurled(m, packRGBA8(0.05f, 0.05f, 0.06f, 1));
    umbrellaFurled = make(m);
    m.clear();
    buildRod(m);
    carryModels[CARRY_ROD] = carryAlt[CARRY_ROD] = make(m);
    m.clear();
    buildBinoculars(m);
    carryModels[CARRY_BINOCULARS] = carryAlt[CARRY_BINOCULARS] = make(m);
    m.clear();
    buildSurfboard(m, packRGBA8(0.93f, 0.9f, 0.82f, 1), packRGBA8(0.1f, 0.5f, 0.55f, 1));
    carryModels[CARRY_SURFBOARD] = make(m);
    m.clear();
    buildSurfboard(m, packRGBA8(0.95f, 0.75f, 0.3f, 1), packRGBA8(0.85f, 0.25f, 0.2f, 1));
    carryAlt[CARRY_SURFBOARD] = make(m);
}

// What someone at a place would have in hand (deterministic from uid): context 0 street, 1 airport traveler,
// 2 shopping street, 3 office district, 4 beach, 5 angler, 6 birder
u8 GameWorld::pickCarry(u32 uid, int context) const {
    u32 h = hash32(uid * 2654435761u + 0x5a17u);
    float r = hashToFloat(h);
    switch (context) {
        case 1: return r < 0.8f ? CARRY_SUITCASE : (r < 0.9f ? CARRY_COFFEE : CARRY_NONE);
        case 2: return r < 0.45f ? CARRY_SHOPBAG : (r < 0.55f ? CARRY_COFFEE : CARRY_NONE);
        case 3: return r < 0.2f ? CARRY_BRIEFCASE : (r < 0.4f ? CARRY_COFFEE : CARRY_NONE);
        case 4: return r < 0.12f ? CARRY_SURFBOARD : CARRY_NONE;
        case 5: return CARRY_ROD;
        case 6: return CARRY_BINOCULARS;
        default: return r < 0.08f ? CARRY_COFFEE : (r < 0.14f ? CARRY_SHOPBAG : (r < 0.17f ? CARRY_BRIEFCASE : CARRY_NONE));
    }
}

// What a pedestrian actually has in hand this frame: its own prop, else an umbrella in steady rain outdoors (about
// half of the civilians) or a street default for wanderers; nothing while the hands are busy (a weapon, a fight,
// aiming), in a vehicle, swimming or ragdolling. The animator reads this for the arm poses (peds.cpp -> AnimInput).
// A phone at the ear or a cigarette (Anim::rightHandBusy stances) takes the right hand: a cup changes hands, and the
// right-hand loads that have no left-hand hold (case, umbrella, rod, board) are put away meanwhile and never handed
// out as a rain umbrella.
u8 GameWorld::effectiveCarry(const Ped& p) const {
    if (p.charIndex < 0 || p.ragdoll || p.state != PS_ONFOOT) return CARRY_NONE;
    if (p.weapon != WPN_FISTS || p.meleeMove >= 0 || p.aiming) return CARRY_NONE;
    u8 carry = p.carry;
    if (carry == CARRY_HANDSFREE) return CARRY_NONE;
    bool civilian = p.faction == FAC_CIVILIAN && !p.isPlayer;
    bool busyR = Anim::rightHandBusy(p.animIn.stance);
    if (carry == CARRY_NONE && civilian && !busyR && umbrellaWeather() && hash32(p.uid * 97u + 13u) % 100u < 48u &&
        p.brain.type != BRAIN_FLEE)
        carry = CARRY_UMBRELLA;
    if (carry == CARRY_NONE && civilian && p.brain.type == BRAIN_WANDER) carry = pickCarry(p.uid, 0);
    if (carry >= CARRY_COUNT) return CARRY_NONE;
    if (busyR && (carry == CARRY_SUITCASE || carry == CARRY_UMBRELLA || carry == CARRY_ROD || carry == CARRY_SURFBOARD))
        return CARRY_NONE;
    return carry;
}

// Steady rain where the player is (umbrellas up outdoors)
bool GameWorld::umbrellaWeather() const { return env && env->rain > 0.25f && !Interiors::isInside(); }

// Draws what pedestrian i carries; `body` is the ped's own draw item (position, yaw, fade).
void GameWorld::submitCarry(int i, const Render::DrawItem& body) {
    Ped& p = peds[i];
    if (p.visibleDist > 70.f) return;
    u8 carry = effectiveCarry(p);
    if (carry == CARRY_NONE) return;
    bool raining = umbrellaWeather();
    Render::DynamicRenderer* dyn = renderer->dynamic;
    const CharEntry& ce = chars[p.charIndex];
    bool alt = (p.uid & 1u) != 0;
    Render::Model* model = alt && carryAlt[carry] ? carryAlt[carry] : carryModels[carry];
    if (!model) return;
    mat3 pr = body.rot;
    vec3 fwd = pr * vec3(0.f, 1.f, 0.f), right = pr * vec3(1.f, 0.f, 0.f), up(0.f, 0.f, 1.f);
    // the hand that holds it: right for most, left for bags, coffee in the right unless a phone or cigarette is in it
    // (the animator's carryArms follows the same rule)
    bool rightHand = !(carry == CARRY_SHOPBAG || carry == CARRY_BRIEFCASE);
    if (carry == CARRY_COFFEE && (p.anim.phoneW > 0.3f || p.anim.browseW > 0.3f || Anim::rightHandBusy(p.animIn.stance)))
        rightHand = false;
    vec3 gpos, gaxis, gpalm;
    Anim::handGrip(ce.skel, p.bones, rightHand, gpos, gaxis, gpalm);
    vec3 hand = pr * gpos;   // relative to p.pos
    Render::DrawItem d;
    d.model = model;
    d.id = 0x6B0000000ull | ((u64)p.uid & 0xfffffffull);
    d.castShadow = p.visibleDist < 30.f;
    d.fade = body.fade;
    auto frame = [&](vec3 y, vec3 z) {   // columns x, y, z from a forward and an up hint
        vec3 zz = normalize(z - y * dot(z, y));
        return mat3(cross(y, zz), y, zz);
    };
    switch (carry) {
        case CARRY_SUITCASE: {
            // wheels on the ground behind the hand at the handle's length: pulled at whatever angle that gives
            float dz = Max(hand.z - 0.02f, 0.05f);
            float dh = sqrtf(Max(carry_detail::kCaseGrip * carry_detail::kCaseGrip - dz * dz, 0.f));
            vec3 wheels = vec3(hand.x, hand.y, 0.02f) - fwd * dh;
            vec3 axis = normalize(hand - wheels);
            vec3 x = normalize(right - axis * dot(right, axis));
            d.rot = mat3(x, cross(axis, x), axis);
            d.pos = p.pos + dvec3(wheels);
            break;
        }
        case CARRY_SHOPBAG:
        case CARRY_BRIEFCASE:   // hangs plumb from the fist, long side along the walk
            d.rot = frame(fwd, up);
            d.pos = p.pos + dvec3(hand + vec3(0.f, 0.f, 0.03f));
            break;
        case CARRY_COFFEE:   // upright in the fist
            d.rot = frame(fwd, up);
            d.pos = p.pos + dvec3(hand + gpalm * 0.01f);
            break;
        case CARRY_UMBRELLA: {
            if (!raining) {   // furled, hanging from the hand like a stick
                d.model = umbrellaFurled;
                d.rot = frame(fwd, up);
                d.pos = p.pos + dvec3(hand);
                break;
            }
            // canopy over the head (tilted back a little), the shaft from the hand up to it
            float topZ = (float)(p.bones[Anim::B_HEAD].c[3].z) + 0.42f;
            vec3 top = vec3(0.f, 0.f, topZ) - fwd * 0.08f + right * 0.05f;
            vec3 shaft = top - hand;
            float len = length(shaft);
            if (len < 0.2f) break;
            vec3 ax = shaft / len;
            vec3 x = normalize(right - ax * dot(right, ax));
            Render::DrawItem sd = d;
            sd.model = umbrellaShaft;
            sd.rot = mat3(x, cross(ax, x), ax);
            sd.pos = p.pos + dvec3(hand);
            sd.scale = vec3(1.f, 1.f, len);
            sd.id = d.id + (1ull << 36);
            if (umbrellaShaft) dyn->submit(sd);
            d.rot = mat3(x, cross(ax, x), ax);
            d.pos = p.pos + dvec3(top);
            d.castShadow = p.visibleDist < 45.f;
            break;
        }
        case CARRY_ROD:   // held up and out, over the water for anglers standing at the edge
            d.rot = frame(normalize(fwd * 0.8f + up * 0.6f), up);
            d.pos = p.pos + dvec3(hand);
            break;
        case CARRY_BINOCULARS: {   // on the strap at the chest
            vec3 chest = pr * p.bones[Anim::B_CHEST].c[3].xyz();
            d.rot = frame(fwd, up);
            d.pos = p.pos + dvec3(chest + fwd * 0.14f - up * 0.06f);
            break;
        }
        case CARRY_SURFBOARD: {   // under the arm on the outside, nose forward, deck against the hip
            d.rot = mat3(right, fwd, up);
            d.pos = p.pos + dvec3(hand + right * 0.06f + up * 0.24f);
            break;
        }
        default: return;
    }
    if (d.model) dyn->submit(d);
}

}  // namespace Game
