// Character assembly: random population descriptions and the final skinned mesh (body + outfit + hair).
//
// CharacterDesc field conventions (indices used by randomCharacter and the mesh builders):
//   hairStyle : 0 bald, 1 buzz cut, 2 short, 3 curly/afro, 4 ponytail, 5 long straight, 6 bun, 7 braids,
//               8 slicked back, 9 bob, 10 quiff/side part
//   top       : 0 t-shirt, 1 tank top, 2 polo, 3 hawaiian shirt, 4 dress shirt (long sleeves), 5 hoodie,
//               6 suit jacket + shirt + tie, 7 police uniform shirt, 8 medic/EMT shirt, 9 hi-vis vest over tee,
//               10 bikini top, 11 one-piece swimsuit, 12 sundress, 13 blouse, 14 none (shirtless), 15 crop top,
//               16 oversized tee
//   bottom    : 0 jeans, 1 shorts, 2 cargo shorts, 3 slacks, 4 skirt, 5 swim trunks, 6 bikini bottom,
//               7 police trousers (with duty belt), 8 baggy jeans, 9 leggings, 10 work pants, 11 hot pants
//   shoes     : 0 sneakers, 1 leather dress shoes, 2 boots, 3 sandals/flip-flops, 4 barefoot, 5 flats, 6 running shoes
//   hat       : -1 none, 0 baseball cap, 1 backwards cap, 2 police cap, 3 hard hat, 4 sun hat, 5 fedora,
//               6 beanie, 7 bandana
//   glasses   : -1 none, 0 sunglasses, 1 aviators, 2 reading glasses
//   facialHair: -1 none, 0 stubble, 1 mustache, 2 goatee, 3 full beard, 4 short boxed beard
//   role      : 0 civilian, 1 police, 2 gang, 3 business, 4 beach, 5 worker, 6 medic
// Colors are linear RGB albedo. skinTone is written directly to the skin vertex color.
#include "anim_internal.h"
#include <unordered_map>

namespace Anim {

namespace detail {

// Compact a mesh by dropping triangles flagged in hide and unreferenced vertices.
static void compactInto(const MeshB& src, const std::vector<u8>& hideTri, MeshB& dst) {
    std::vector<u32> remap(src.v.size(), 0xffffffffu);
    size_t nt = src.idx.size() / 3;
    for (size_t t = 0; t < nt; t++) {
        if (t < hideTri.size() && hideTri[t]) continue;
        for (int k = 0; k < 3; k++) {
            u32 vi = src.idx[t * 3 + k];
            if (remap[vi] == 0xffffffffu) {
                remap[vi] = (u32)dst.v.size();
                dst.v.push_back(src.v[vi]);
            }
            dst.idx.push_back(remap[vi]);
        }
    }
}

// Skin palette (sRGB), light to deep. randomCharacter blends neighbours for a continuous range.
static const float kSkinPal[][3] = {
    {0.97f, 0.83f, 0.73f}, {0.94f, 0.76f, 0.63f}, {0.89f, 0.69f, 0.55f}, {0.83f, 0.62f, 0.47f}, {0.76f, 0.55f, 0.40f},
    {0.67f, 0.47f, 0.33f}, {0.57f, 0.38f, 0.26f}, {0.47f, 0.31f, 0.21f}, {0.38f, 0.24f, 0.16f}, {0.29f, 0.18f, 0.12f},
    {0.22f, 0.14f, 0.10f},
};

static vec3 skinFromT(float t) {
    const int n = (int)(sizeof(kSkinPal) / sizeof(kSkinPal[0]));
    t = Saturate(t) * (n - 1);
    int i = Min((int)t, n - 2);
    float f = t - i;
    vec3 a(kSkinPal[i][0], kSkinPal[i][1], kSkinPal[i][2]), b(kSkinPal[i + 1][0], kSkinPal[i + 1][1], kSkinPal[i + 1][2]);
    return srgbToLinear(lerp(a, b, f));
}

static vec3 pickColor(Rng& r, const float (*pal)[3], int n) {
    int i = (int)(r.next() % (u32)n);
    vec3 c(pal[i][0], pal[i][1], pal[i][2]);
    // slight variation in value/saturation
    float v = r.range(0.9f, 1.08f);
    return srgbToLinear(saturate(c * v));
}

// Clothing palettes (sRGB)
static const float kCasualTop[][3] = {
    {0.95f, 0.95f, 0.93f}, {0.12f, 0.12f, 0.13f}, {0.85f, 0.2f, 0.2f}, {0.2f, 0.35f, 0.7f}, {0.95f, 0.75f, 0.2f}, {0.2f, 0.6f, 0.55f},
    {0.95f, 0.5f, 0.6f}, {0.55f, 0.55f, 0.58f}, {0.3f, 0.45f, 0.25f}, {0.98f, 0.6f, 0.3f}, {0.5f, 0.75f, 0.9f}, {0.6f, 0.25f, 0.5f},
    {0.9f, 0.88f, 0.75f}, {0.15f, 0.2f, 0.35f}, {0.1f, 0.65f, 0.75f}, {0.8f, 0.4f, 0.15f},
};
static const float kDenim[][3] = {{1.f, 1.f, 1.f}, {0.8f, 0.85f, 0.95f}, {0.55f, 0.55f, 0.6f}, {0.35f, 0.33f, 0.35f}, {1.1f, 1.1f, 1.15f}};
static const float kPants[][3] = {
    {0.2f, 0.2f, 0.22f}, {0.75f, 0.68f, 0.52f}, {0.35f, 0.4f, 0.3f}, {0.15f, 0.18f, 0.28f}, {0.55f, 0.55f, 0.55f}, {0.9f, 0.9f, 0.88f},
    {0.45f, 0.35f, 0.25f}, {0.6f, 0.2f, 0.2f},
};
static const float kShorts[][3] = {
    {0.75f, 0.68f, 0.52f}, {0.2f, 0.25f, 0.4f}, {0.35f, 0.4f, 0.3f}, {0.9f, 0.9f, 0.88f}, {0.15f, 0.15f, 0.16f}, {0.8f, 0.35f, 0.3f},
    {0.3f, 0.55f, 0.7f}, {0.55f, 0.5f, 0.45f},
};
static const float kSwim[][3] = {
    {0.9f, 0.2f, 0.25f}, {0.1f, 0.1f, 0.12f}, {0.1f, 0.5f, 0.8f}, {0.95f, 0.6f, 0.1f}, {0.2f, 0.7f, 0.5f}, {0.95f, 0.4f, 0.65f},
    {0.95f, 0.95f, 0.9f}, {0.4f, 0.2f, 0.6f}, {0.1f, 0.75f, 0.85f},
};
static const float kSuit[][3] = {{0.08f, 0.08f, 0.1f}, {0.15f, 0.17f, 0.25f}, {0.3f, 0.3f, 0.32f}, {0.55f, 0.5f, 0.42f}, {0.85f, 0.83f, 0.78f}};
static const float kShoe[][3] = {{0.95f, 0.95f, 0.95f}, {0.1f, 0.1f, 0.1f}, {0.8f, 0.2f, 0.2f}, {0.2f, 0.3f, 0.6f}, {0.6f, 0.6f, 0.6f}, {0.9f, 0.85f, 0.7f}};
static const float kGang[][3] = {{0.75f, 0.1f, 0.12f}, {0.1f, 0.25f, 0.75f}, {0.1f, 0.55f, 0.25f}, {0.45f, 0.15f, 0.6f}, {0.95f, 0.75f, 0.1f}};
static const float kHair[][3] = {
    {0.03f, 0.025f, 0.022f}, {0.07f, 0.045f, 0.03f}, {0.13f, 0.08f, 0.045f}, {0.25f, 0.15f, 0.08f}, {0.4f, 0.26f, 0.13f},
    {0.62f, 0.45f, 0.25f}, {0.82f, 0.68f, 0.42f}, {0.45f, 0.18f, 0.08f},
};

}  // namespace detail

namespace detail {

// ------------------------------------------------------------------------------------------------
// Civilian archetypes: a coherent look per person, with colours from coordinated palettes (neutral bottoms, a limited
// set of top colours, layers in earth / navy / grey tones) rather than independent random picks.

enum CivStyle { CS_CASUAL = 0, CS_TOURIST, CS_STUDENT, CS_OFFICE, CS_ELDERLY, CS_SPORTY, CS_STREET, CS_COUNT };

static const float kNeutralTop[][3] = {{0.95f, 0.95f, 0.93f}, {0.12f, 0.12f, 0.13f}, {0.55f, 0.55f, 0.58f}, {0.15f, 0.2f, 0.35f}, {0.9f, 0.88f, 0.75f},
                                       {0.35f, 0.4f, 0.3f}, {0.6f, 0.55f, 0.48f}};
static const float kLayer[][3] = {{0.2f, 0.24f, 0.18f}, {0.12f, 0.15f, 0.26f}, {0.3f, 0.3f, 0.32f}, {0.08f, 0.08f, 0.09f}, {0.55f, 0.45f, 0.32f},
                                  {0.42f, 0.14f, 0.14f}, {0.75f, 0.72f, 0.64f}, {0.62f, 0.5f, 0.36f}};
static const float kKnit[][3] = {{0.85f, 0.82f, 0.74f}, {0.5f, 0.5f, 0.52f}, {0.15f, 0.18f, 0.3f}, {0.45f, 0.12f, 0.15f}, {0.62f, 0.48f, 0.32f},
                                 {0.3f, 0.36f, 0.3f}};
static const float kBag[][3] = {{0.06f, 0.06f, 0.07f}, {0.12f, 0.15f, 0.26f}, {0.35f, 0.35f, 0.37f}, {0.42f, 0.28f, 0.16f}, {0.3f, 0.34f, 0.24f},
                                {0.55f, 0.12f, 0.12f}, {0.75f, 0.68f, 0.55f}};
#define PAL(p) p, (int)(sizeof(p) / sizeof(p[0]))

static void civilianOutfit(CharacterDesc& d, Rng& r) {
    const bool fem = d.gender == FEMALE;
    const float age = d.age;   // 0 = 18 .. 1 = 80 years
    // archetype weights by age: students are young, the elderly old, office workers of working age
    float w[CS_COUNT] = {0.36f, 0.12f, 0.16f * (1.f - sstep(0.12f, 0.3f, age)), 0.13f * sstep(0.03f, 0.1f, age) * (1.f - sstep(0.6f, 0.75f, age)),
                         0.85f * sstep(0.62f, 0.75f, age), 0.08f * (1.f - sstep(0.4f, 0.7f, age)), 0.09f * (1.f - sstep(0.25f, 0.45f, age))};
    int st = r.weighted(w, CS_COUNT);
    Rng& R = r;
    auto pc = [&](const float (*pal)[3], int n) { return pickColor(R, pal, n); };
    auto chance = [&](float p) { return R.chance(p); };
    d.outer = -1;
    d.bag = -1;
    d.extras = ACC_EXPLICIT;
    d.topColor = pc(PAL(kCasualTop));
    switch (st) {
        case CS_TOURIST: {
            float t = R.f();
            d.top = t < 0.35f ? TOP_HAWAIIAN : (t < 0.7f ? TOP_TSHIRT : (fem ? (t < 0.85f ? TOP_TANK : TOP_BLOUSE) : TOP_POLO));
            float b = R.f();
            d.bottom = b < 0.4f ? BOT_SHORTS : (b < 0.7f ? BOT_CARGO : (fem && b < 0.85f ? BOT_SKIRT : BOT_JEANS));
            d.shoes = chance(0.45f) ? SHOE_SANDAL : (chance(0.6f) ? SHOE_RUNNER : SHOE_SNEAKER);
            float h = R.f();
            d.hat = h < 0.3f ? HAT_SUNHAT : (h < 0.55f ? HAT_CAP : -1);
            d.glasses = chance(0.5f) ? GL_SUN : -1;
            float bg = R.f();
            d.bag = bg < 0.35f ? BAG_BACKPACK : (bg < 0.6f ? BAG_CROSSBODY : (fem && bg < 0.72f ? BAG_TOTE : -1));
            if (chance(0.5f)) d.extras |= ACC_WATCH;
            break;
        }
        case CS_STUDENT: {
            float t = R.f();
            d.top = t < 0.4f ? TOP_TSHIRT : (t < 0.6f ? TOP_OVERSIZED : (t < 0.75f ? TOP_HOODIE : (fem ? TOP_CROP : TOP_TANK)));
            if (d.top != TOP_HOODIE && chance(0.22f)) d.outer = chance(0.6f) ? OUT_ZIPHOODIE : OUT_OVERSHIRT;
            float b = R.f();
            d.bottom = b < 0.5f ? BOT_JEANS : (b < 0.7f ? (fem ? BOT_LEGGINGS : BOT_CARGO) : (fem && b < 0.85f ? BOT_SKIRT : BOT_SHORTS));
            d.shoes = chance(0.8f) ? SHOE_SNEAKER : SHOE_RUNNER;
            float h = R.f();
            d.hat = h < 0.12f ? HAT_CAP_BACK : (h < 0.22f ? HAT_CAP : (h < 0.27f ? HAT_BEANIE : -1));
            d.bag = chance(0.6f) ? BAG_BACKPACK : (chance(0.3f) ? BAG_CROSSBODY : -1);
            if (chance(0.25f)) d.extras |= ACC_HEADPHONES;
            if (chance(0.3f)) d.extras |= ACC_BRACELET_L;
            if (chance(0.2f)) d.extras |= ACC_CUFFED_HEM;
            break;
        }
        case CS_OFFICE: {
            float t = R.f();
            if (fem) d.top = t < 0.5f ? TOP_BLOUSE : (t < 0.8f ? TOP_DRESS_SHIRT : TOP_POLO);
            else d.top = t < 0.65f ? TOP_DRESS_SHIRT : TOP_POLO;
            d.topColor = chance(0.55f) ? pickColor(R, kNeutralTop, 5) : pc(PAL(kCasualTop));
            if (d.top == TOP_DRESS_SHIRT && chance(0.4f)) d.extras |= ACC_ROLLED_SLEEVES;
            float b = R.f();
            d.bottom = fem ? (b < 0.45f ? BOT_SKIRT : (b < 0.8f ? BOT_SLACKS : BOT_JEANS)) : (b < 0.75f ? BOT_SLACKS : BOT_JEANS);
            float o = R.f();
            if (o < 0.18f) d.outer = OUT_BLAZER;
            else if (fem && o < 0.3f) d.outer = OUT_CARDIGAN;
            d.shoes = fem ? (chance(0.5f) ? SHOE_FLATS : SHOE_LOAFER) : (chance(0.55f) ? SHOE_DRESS : SHOE_LOAFER);
            if (chance(0.3f)) d.extras |= ACC_LANYARD;
            if (chance(0.65f)) d.extras |= ACC_WATCH;
            if (fem && chance(0.35f)) d.bag = BAG_TOTE;
            else if (!fem && chance(0.2f)) d.bag = BAG_CROSSBODY;
            d.glasses = chance(0.2f) ? GL_READING : -1;
            break;
        }
        case CS_ELDERLY: {
            float t = R.f();
            if (fem) d.top = t < 0.4f ? TOP_BLOUSE : (t < 0.65f ? TOP_POLO : (t < 0.85f ? TOP_TSHIRT : TOP_SUNDRESS));
            else d.top = t < 0.4f ? TOP_POLO : (t < 0.65f ? TOP_DRESS_SHIRT : (t < 0.85f ? TOP_HAWAIIAN : TOP_TSHIRT));
            d.topColor = chance(0.5f) ? pickColor(R, kNeutralTop, 7) : pc(PAL(kCasualTop));
            float b = R.f();
            d.bottom = fem ? (b < 0.45f ? BOT_SLACKS : (b < 0.8f ? BOT_SKIRT : BOT_SHORTS)) : (b < 0.55f ? BOT_SLACKS : (b < 0.8f ? BOT_SHORTS : BOT_JEANS));
            if (chance(0.3f)) d.outer = OUT_CARDIGAN;
            d.shoes = chance(0.4f) ? SHOE_LOAFER : (chance(0.5f) ? SHOE_SNEAKER : (fem ? SHOE_FLATS : SHOE_SANDAL));
            float h = R.f();
            d.hat = h < 0.25f ? HAT_SUNHAT : (h < 0.4f ? (fem ? HAT_SUNHAT : HAT_FEDORA) : (h < 0.5f ? HAT_CAP : -1));
            d.glasses = chance(0.55f) ? GL_READING : (chance(0.3f) ? GL_SUN : -1);
            if (chance(0.6f)) d.extras |= ACC_WATCH;
            if (fem && chance(0.4f)) d.extras |= ACC_EARRINGS;
            if (fem && chance(0.3f)) d.extras |= ACC_NECKLACE;
            if (fem && chance(0.3f)) d.bag = BAG_TOTE;
            break;
        }
        case CS_SPORTY: {
            d.top = chance(0.5f) ? TOP_TANK : (fem && chance(0.4f) ? TOP_CROP : TOP_TSHIRT);
            d.bottom = fem ? (chance(0.7f) ? BOT_LEGGINGS : BOT_SHORTS) : BOT_SHORTS;
            d.shoes = SHOE_RUNNER;
            d.hat = chance(0.3f) ? HAT_CAP : -1;
            d.glasses = chance(0.4f) ? GL_SUN : -1;
            if (chance(0.35f)) d.extras |= ACC_HEADPHONES;
            if (chance(0.4f)) d.extras |= ACC_BRACELET_L;
            break;
        }
        case CS_STREET: {
            float t = R.f();
            d.top = t < 0.35f ? TOP_OVERSIZED : (t < 0.6f ? TOP_HOODIE : (t < 0.8f ? TOP_TANK : TOP_TSHIRT));
            if (d.top == TOP_TANK && chance(0.3f)) d.outer = OUT_OVERSHIRT;
            d.bottom = chance(0.5f) ? BOT_BAGGY : (chance(0.5f) ? BOT_CARGO : BOT_JEANS);
            d.shoes = SHOE_SNEAKER;
            float h = R.f();
            d.hat = h < 0.25f ? HAT_CAP_BACK : (h < 0.4f ? HAT_CAP : (h < 0.5f ? HAT_BEANIE : -1));
            d.glasses = chance(0.25f) ? GL_SUN : -1;
            if (chance(0.4f)) d.extras |= ACC_NECKLACE;
            if (chance(0.3f)) d.extras |= ACC_BRACELET_R;
            if (chance(0.2f)) d.bag = BAG_CROSSBODY;
            break;
        }
        default: {   // casual
            float t = R.f();
            if (fem) {
                if (t < 0.2f) d.top = TOP_SUNDRESS;
                else if (t < 0.45f) d.top = TOP_TSHIRT;
                else if (t < 0.63f) d.top = TOP_TANK;
                else if (t < 0.73f) d.top = TOP_CROP;
                else if (t < 0.87f) d.top = TOP_BLOUSE;
                else d.top = TOP_POLO;
            } else {
                if (t < 0.36f) d.top = TOP_TSHIRT;
                else if (t < 0.52f) d.top = TOP_POLO;
                else if (t < 0.66f) d.top = TOP_HAWAIIAN;
                else if (t < 0.78f) d.top = TOP_TANK;
                else if (t < 0.88f) d.top = TOP_DRESS_SHIRT;
                else if (t < 0.94f) d.top = TOP_HOODIE;
                else d.top = TOP_OVERSIZED;
            }
            if (d.top == TOP_DRESS_SHIRT && chance(0.5f)) d.extras |= ACC_ROLLED_SLEEVES;
            float o = R.f();
            if ((d.top == TOP_TSHIRT || d.top == TOP_TANK) && o < 0.14f) d.outer = OUT_OVERSHIRT;
            else if ((d.top == TOP_TSHIRT || d.top == TOP_BLOUSE || d.top == TOP_SUNDRESS || d.top == TOP_CROP) && o < 0.22f) d.outer = OUT_JACKET;
            float b = R.f();
            if (d.top == TOP_SUNDRESS) d.bottom = BOT_SKIRT;
            else if (fem) d.bottom = b < 0.3f ? BOT_JEANS : (b < 0.5f ? BOT_SHORTS : (b < 0.62f ? BOT_HOTPANTS : (b < 0.8f ? BOT_SKIRT : BOT_LEGGINGS)));
            else d.bottom = b < 0.35f ? BOT_JEANS : (b < 0.62f ? BOT_SHORTS : (b < 0.8f ? BOT_CARGO : BOT_SLACKS));
            float sh = R.f();
            if (d.bottom == BOT_SLACKS) d.shoes = chance(0.5f) ? SHOE_DRESS : SHOE_LOAFER;
            else if (sh < 0.45f) d.shoes = SHOE_SNEAKER;
            else if (sh < 0.72f) d.shoes = SHOE_SANDAL;
            else if (sh < 0.86f) d.shoes = fem ? SHOE_FLATS : SHOE_RUNNER;
            else d.shoes = SHOE_RUNNER;
            if (d.bottom == BOT_JEANS && chance(0.15f)) d.extras |= ACC_CUFFED_HEM;
            float h = R.f();
            if (h < 0.14f) d.hat = HAT_CAP;
            else if (h < 0.18f) d.hat = HAT_CAP_BACK;
            else if (h < 0.23f) d.hat = HAT_SUNHAT;
            else if (h < 0.26f && !fem) d.hat = HAT_FEDORA;
            float g = R.f();
            if (g < 0.25f) d.glasses = GL_SUN;
            else if (g < 0.3f) d.glasses = GL_READING;
            else if (g < 0.42f && d.hat < 0) d.extras |= ACC_SUNGLASSES_UP;
            float bg = R.f();
            if (bg < 0.1f) d.bag = BAG_CROSSBODY;
            else if (fem && bg < 0.2f) d.bag = BAG_TOTE;
            else if (bg < 0.25f) d.bag = BAG_BACKPACK;
            if (chance(fem ? 0.3f : 0.12f)) d.extras |= ACC_NECKLACE;
            if (fem && chance(0.55f)) d.extras |= ACC_EARRINGS;
            if (chance(0.3f)) d.extras |= ACC_WATCH;
            if (fem && chance(0.3f)) d.extras |= ACC_BRACELET_R;
            break;
        }
    }
    // colours by garment: denim shades for jeans, neutrals or muted colours for trousers and shorts, a skirt matching
    // or contrasting the top; layers, knits and bags from their own palettes
    switch (d.bottom) {
        case BOT_JEANS: case BOT_BAGGY: d.bottomColor = pickColor(R, kDenim, 5); break;
        case BOT_SHORTS: case BOT_CARGO: case BOT_HOTPANTS: d.bottomColor = pickColor(R, kShorts, 8); break;
        case BOT_SKIRT: d.bottomColor = d.top == TOP_SUNDRESS ? d.topColor : (chance(0.5f) ? pickColor(R, kSuit, 4) : pc(PAL(kCasualTop))); break;
        case BOT_LEGGINGS: d.bottomColor = chance(0.7f) ? srgbToLinear(vec3(0.06f)) : pickColor(R, kPants, 8); break;
        default: d.bottomColor = pickColor(R, kPants, 8); break;
    }
    d.shoeColor = pickColor(R, kShoe, 6);
    if (d.shoes == SHOE_DRESS || d.shoes == SHOE_LOAFER) d.shoeColor = srgbToLinear(chance(0.5f) ? vec3(0.06f) : vec3(0.35f, 0.2f, 0.1f));
    d.outerColor = d.outer == OUT_CARDIGAN ? pc(PAL(kKnit)) : (d.outer == OUT_JACKET && chance(0.5f) ? pickColor(R, kDenim, 5) : pc(PAL(kLayer)));
    if (d.outer == OUT_BLAZER) d.outerColor = pickColor(R, kSuit, 4);
    d.bagColor = pc(PAL(kBag));
    if (st != CS_OFFICE && st != CS_ELDERLY && d.age > 0.7f && d.glasses < 0 && chance(0.4f)) d.glasses = GL_READING;
    // hats sit on hair that fits under them
    if (d.hat >= 0 && (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_BUN)) d.hat = -1;
}

// Layers, bags and small accessories for the uniformed / role outfits (civilians get theirs with their archetype), and
// the rules that keep any combination coherent.
static void accessorize(CharacterDesc& d) {
    Rng r(hash32(d.seed * 0x27D4EB2Fu + 0x165667B1u));
    const bool fem = d.gender == FEMALE;
    if (!(d.extras & ACC_EXPLICIT)) {
        d.extras = ACC_EXPLICIT;
        switch (d.role) {
            case 1: case 6:   // police, medics: a watch, nothing loose
                if (r.chance(0.6f)) d.extras |= ACC_WATCH;
                break;
            case 2:   // gang: chains and bracelets, sometimes an open zip hoodie over the tank, a crossbody bag
                if (r.chance(0.7f)) d.extras |= ACC_NECKLACE;
                if (r.chance(0.4f)) d.extras |= ACC_BRACELET_R;
                if (r.chance(0.3f)) d.extras |= ACC_WATCH;
                if (d.top == TOP_TANK && r.chance(0.25f)) {
                    d.outer = OUT_ZIPHOODIE;
                    d.outerColor = srgbToLinear(r.chance(0.5f) ? vec3(0.08f) : vec3(0.5f, 0.5f, 0.52f));
                }
                if (r.chance(0.15f)) {
                    d.bag = BAG_CROSSBODY;
                    d.bagColor = srgbToLinear(vec3(0.05f));
                }
                break;
            case 3:   // business: watches, lanyards, an open blazer over a blouse, totes
                if (r.chance(0.8f)) d.extras |= ACC_WATCH;
                if (r.chance(0.2f)) d.extras |= ACC_LANYARD;
                if (fem && d.top == TOP_BLOUSE && r.chance(0.35f)) {
                    d.outer = OUT_BLAZER;
                    d.outerColor = d.bottomColor;
                }
                if (fem && r.chance(0.4f)) {
                    d.bag = BAG_TOTE;
                    d.bagColor = srgbToLinear(r.chance(0.5f) ? vec3(0.06f) : vec3(0.42f, 0.26f, 0.14f));
                }
                if (fem && r.chance(0.6f)) d.extras |= ACC_EARRINGS;
                if (fem) d.shoes = r.chance(0.5f) ? SHOE_FLATS : SHOE_LOAFER;
                break;
            case 4:   // beach: bracelets, a tote, sunglasses on the head
                if (r.chance(0.35f)) d.extras |= ACC_BRACELET_L;
                if (r.chance(0.3f)) d.extras |= ACC_NECKLACE;
                if (fem && r.chance(0.5f)) d.extras |= ACC_EARRINGS;
                if (fem && r.chance(0.2f)) {
                    d.bag = BAG_TOTE;
                    d.bagColor = srgbToLinear(r.chance(0.5f) ? vec3(0.85f, 0.78f, 0.6f) : vec3(0.9f, 0.4f, 0.3f));
                }
                if (d.glasses < 0 && d.hat < 0 && r.chance(0.3f)) d.extras |= ACC_SUNGLASSES_UP;
                break;
            case 5:   // construction: a watch
                if (r.chance(0.3f)) d.extras |= ACC_WATCH;
                break;
            default: break;
        }
    }
    // coherence: layers only over tops that take them; no pushed-up sunglasses with a hat or glasses on; bags stay off
    // uniforms
    if (!outerFits(d)) d.outer = -1;
    if (d.hat >= 0 || d.glasses >= 0) d.extras &= ~ACC_SUNGLASSES_UP;
    if (d.role == 1 || d.role == 5 || d.role == 6) d.bag = -1;
}

}  // namespace detail

CharacterDesc randomCharacter(u32 seed, int role) {
    using namespace detail;
    CharacterDesc d;
    Rng r(hash32(seed ^ 0xA511E9B3u) + 1u, (u64)hash32(seed + 0x68E31DA4u) | 1u);
    d.seed = seed;
    d.role = Clamp(role, 0, 6);
    // gender: police/worker skew male, beach/business balanced
    float femP = 0.5f;
    if (d.role == 1) femP = 0.3f;
    if (d.role == 2) femP = 0.2f;
    if (d.role == 5) femP = 0.12f;
    if (d.role == 6) femP = 0.4f;
    d.gender = r.chance(femP) ? FEMALE : MALE;
    bool fem = d.gender == FEMALE;
    // age: 18..80 (normalized 0..1 maps to 18..80), working roles narrower
    float ageYears;
    if (d.role == 0 || d.role == 4) ageYears = 18.f + 62.f * powf(r.f(), 1.35f);
    else if (d.role == 2) ageYears = r.range(18.f, 38.f);
    else ageYears = r.range(22.f, 60.f);
    d.age = (ageYears - 18.f) / 62.f;
    // height: normal distribution by gender
    d.height = fem ? 1.635f + 0.07f * r.gauss() : 1.765f + 0.075f * r.gauss();
    d.height = Clamp(d.height, fem ? 1.48f : 1.58f, fem ? 1.84f : 2.0f);
    d.height -= 0.03f * sstep(0.7f, 1.f, d.age);
    // body type
    // (a long heavy tail: about one person in eight above 0.75, one in thirty above 0.9)
    float gw = r.gauss();
    float wt = 0.4f + 0.19f * gw + 0.12f * d.age + 0.14f * Sq(Max(0.f, gw - 0.7f));
    if (d.role == 4) wt -= 0.05f;
    if (d.role == 1 || d.role == 5) wt += 0.05f;
    d.weight = Clamp(wt, 0.05f, 1.f);
    float mu = (fem ? 0.28f : 0.45f) + 0.18f * r.gauss() - 0.15f * d.age;
    if (d.role == 1 || d.role == 5 || d.role == 2) mu += 0.12f;
    d.muscle = Clamp(mu, 0.f, 1.f);
    // skin tone: region mix (Latino, Black/Caribbean, White, Asian, mixed) -> continuous palette position
    float g = r.f();
    float st;
    if (g < 0.4f) st = r.range(0.2f, 0.62f), d.ancestry = 0;          // Latino / Mediterranean
    else if (g < 0.64f) st = r.range(0.52f, 1.0f), d.ancestry = 1;    // Black / Afro-Caribbean
    else if (g < 0.86f) st = r.range(0.0f, 0.32f), d.ancestry = 2;    // White
    else if (g < 0.95f) st = r.range(0.12f, 0.42f), d.ancestry = 3;   // East Asian
    else st = r.range(0.2f, 0.85f), d.ancestry = 4;                   // mixed / other
    d.skinTone = skinFromT(st);
    bool darkSkin = st > 0.55f;
    // hair color
    int hc;
    if (st > 0.45f) hc = r.chance(0.85f) ? 0 : 1;
    else if (st > 0.22f) hc = r.chance(0.6f) ? 0 : (r.chance(0.7f) ? 1 : 2);
    else hc = r.irange(0, 7);
    vec3 hcol(kHair[hc][0], kHair[hc][1], kHair[hc][2]);
    float gray = sstep(0.35f, 0.85f, d.age) * r.range(0.4f, 1.f);
    hcol = lerp(hcol, vec3(0.55f, 0.54f, 0.52f), gray * 0.85f);
    if (!fem && r.chance(0.05f)) hcol = vec3(0.7f, 0.66f, 0.55f);   // bleached
    d.hairColor = srgbToLinear(hcol);
    // hair style
    if (fem) {
        const float w[] = {0.01f, 0.02f, 0.08f, darkSkin ? 0.2f : 0.08f, 0.18f, 0.25f, 0.1f, darkSkin ? 0.18f : 0.03f, 0.03f, 0.1f, 0.02f};
        d.hairStyle = r.weighted(w, 11);
    } else {
        float bald = 0.03f + 0.35f * sstep(0.3f, 0.9f, d.age);
        const float w[] = {bald, 0.2f, 0.26f, darkSkin ? 0.12f : 0.04f, 0.02f, 0.02f, 0.005f, darkSkin ? 0.07f : 0.01f, 0.08f, 0.01f, 0.14f};
        d.hairStyle = r.weighted(w, 11);
    }
    // facial hair
    d.facialHair = -1;
    if (!fem && d.age > 0.02f) {
        float fh = r.f();
        if (fh < 0.18f) d.facialHair = FH_STUBBLE;
        else if (fh < 0.24f) d.facialHair = FH_MUSTACHE;
        else if (fh < 0.34f) d.facialHair = FH_GOATEE;
        else if (fh < 0.42f) d.facialHair = FH_BEARD;
        else if (fh < 0.52f) d.facialHair = FH_SHORTBEARD;
    }
    // ---- outfit by role
    d.hat = -1;
    d.glasses = -1;
    auto casualTopColor = [&]() { return pickColor(r, kCasualTop, (int)(sizeof(kCasualTop) / sizeof(kCasualTop[0]))); };
    switch (d.role) {
        case 1: {   // police
            d.top = TOP_POLICE;
            d.bottom = BOT_POLICE;
            d.shoes = r.chance(0.6f) ? SHOE_BOOT : SHOE_DRESS;
            d.topColor = srgbToLinear(vec3(0.12f, 0.16f, 0.3f));
            d.bottomColor = srgbToLinear(vec3(0.1f, 0.12f, 0.2f));
            d.shoeColor = srgbToLinear(vec3(0.06f));
            d.hat = r.chance(0.55f) ? HAT_POLICE : -1;
            if (r.chance(0.35f)) d.glasses = GL_AVIATOR;
            if (d.facialHair > FH_MUSTACHE) d.facialHair = r.chance(0.5f) ? FH_MUSTACHE : -1;
            if (fem && (d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_CURLY)) d.hairStyle = HAIR_BUN;
            break;
        }
        case 2: {   // gang
            vec3 gc = pickColor(r, kGang, 5);
            float t = r.f();
            d.top = t < 0.4f ? TOP_TANK : (t < 0.8f ? TOP_OVERSIZED : TOP_HOODIE);
            d.topColor = r.chance(0.5f) ? gc : srgbToLinear(r.chance(0.5f) ? vec3(0.95f) : vec3(0.1f));
            d.bottom = r.chance(0.6f) ? BOT_BAGGY : BOT_SHORTS;
            d.bottomColor = d.bottom == BOT_BAGGY ? pickColor(r, kDenim, 5) : srgbToLinear(r.chance(0.5f) ? vec3(0.12f) : vec3(0.7f, 0.64f, 0.5f));
            d.shoes = SHOE_SNEAKER;
            d.shoeColor = r.chance(0.5f) ? srgbToLinear(vec3(0.95f)) : gc;
            float hh = r.f();
            if (hh < 0.3f) d.hat = HAT_CAP_BACK;
            else if (hh < 0.45f) d.hat = HAT_BANDANA;
            else if (hh < 0.55f) d.hat = HAT_BEANIE;
            if (r.chance(0.35f)) d.glasses = GL_SUN;
            if (d.hat >= 0 && (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_BUN)) d.hairStyle = HAIR_BRAIDS;
            break;
        }
        case 3: {   // business
            if (fem && r.chance(0.5f)) {
                d.top = TOP_BLOUSE;
                d.topColor = pickColor(r, kCasualTop, 16);
                d.bottom = r.chance(0.55f) ? BOT_SKIRT : BOT_SLACKS;
                d.bottomColor = pickColor(r, kSuit, 4);
                d.shoes = SHOE_FLATS;
            } else {
                d.top = TOP_SUIT;
                d.topColor = pickColor(r, kSuit, 5);
                d.bottom = BOT_SLACKS;
                d.bottomColor = d.topColor;
                d.shoes = SHOE_DRESS;
            }
            d.shoeColor = srgbToLinear(r.chance(0.6f) ? vec3(0.06f) : vec3(0.35f, 0.2f, 0.1f));
            if (r.chance(0.12f)) d.glasses = GL_READING;
            if (d.hairStyle == HAIR_CURLY && !fem) d.hairStyle = HAIR_SHORT;
            break;
        }
        case 4: {   // beach
            if (fem) {
                d.top = r.chance(0.7f) ? TOP_BIKINI : TOP_ONEPIECE;
                d.bottom = d.top == TOP_BIKINI ? BOT_BIKINI : BOT_BIKINI;
                d.topColor = pickColor(r, kSwim, 9);
                d.bottomColor = r.chance(0.7f) ? d.topColor : pickColor(r, kSwim, 9);
            } else {
                d.top = r.chance(0.8f) ? TOP_NONE : TOP_TANK;
                d.topColor = casualTopColor();
                d.bottom = BOT_TRUNKS;
                d.bottomColor = pickColor(r, kSwim, 9);
            }
            d.shoes = r.chance(0.6f) ? SHOE_SANDAL : SHOE_BARE;
            d.shoeColor = pickColor(r, kShoe, 6);
            if (r.chance(0.5f)) d.glasses = GL_SUN;
            float hh = r.f();
            if (hh < 0.2f) d.hat = HAT_SUNHAT;
            else if (hh < 0.35f) d.hat = HAT_CAP;
            if (d.hat >= 0 && (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_BUN)) d.hat = -1;
            break;
        }
        case 5: {   // construction worker
            d.top = TOP_HIVIS;
            d.topColor = casualTopColor();
            d.bottom = r.chance(0.5f) ? BOT_JEANS : BOT_WORK;
            d.bottomColor = d.bottom == BOT_JEANS ? pickColor(r, kDenim, 5) : srgbToLinear(vec3(0.35f, 0.3f, 0.22f));
            d.shoes = SHOE_BOOT;
            d.shoeColor = srgbToLinear(vec3(0.4f, 0.26f, 0.12f));
            d.hat = r.chance(0.8f) ? HAT_HARDHAT : HAT_CAP;
            if (r.chance(0.3f)) d.glasses = GL_SUN;
            if (d.hairStyle == HAIR_CURLY || d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_BUN) d.hairStyle = fem ? HAIR_PONYTAIL : HAIR_SHORT;
            break;
        }
        case 6: {   // medic
            d.top = TOP_MEDIC;
            d.topColor = srgbToLinear(r.chance(0.5f) ? vec3(0.2f, 0.28f, 0.45f) : vec3(0.9f, 0.9f, 0.88f));
            d.bottom = BOT_WORK;
            d.bottomColor = srgbToLinear(vec3(0.12f, 0.14f, 0.22f));
            d.shoes = SHOE_BOOT;
            d.shoeColor = srgbToLinear(vec3(0.06f));
            if (fem && (d.hairStyle == HAIR_LONG || d.hairStyle == HAIR_CURLY)) d.hairStyle = HAIR_PONYTAIL;
            break;
        }
        default: {  // civilian, hot coastal city: an archetype sets the whole look
            civilianOutfit(d, r);
            break;
        }
    }
    accessorize(d);
    return d;
}

// Bake ambient occlusion from the body's signed distance field into the vertex colours (all layers: skin, clothes,
// hair). Samples the SDF along each normal and measures how much closer the body is than open space would be;
// darkens armpits, the crotch, the neck under the jaw, ear and lid folds, nostrils, the mouth cavity and clothing
// tucked against the body. Small-scale only (the renderer's screen-space AO handles the large scale).
namespace detail {
static void bakeOcclusion(const BuildCtx& c, MeshB& m) {
    const float s = c.D->s;
    // a 4 mm step catches the fine creases (lid crease, alar groove, mouth corners, ear folds, cloth seams)
    const float dk[4] = {0.004f, 0.01f, 0.025f, 0.055f};
    const float wk[4] = {0.25f, 0.33f, 0.26f, 0.16f};
    const float reach = 0.06f * s, cap = 0.065f * s;
    // uniform grid of candidate primitive lists (cells of 4 cm; a list holds every primitive whose bounding
    // sphere comes within reach + cap + blend of the cell), so each sample evaluates only nearby primitives
    const Sdf& sdf = c.sdf;
    vec3 lo(1e9f), hi(-1e9f);
    for (const BVert& v : m.v) {
        lo = vmin(lo, v.p);
        hi = vmax(hi, v.p);
    }
    const float cell = 0.04f;
    lo = lo - vec3(0.01f);
    int nx = Max(1, (int)ceilf((hi.x - lo.x) / cell) + 1), ny = Max(1, (int)ceilf((hi.y - lo.y) / cell) + 1),
        nz = Max(1, (int)ceilf((hi.z - lo.z) / cell) + 1);
    // candidate lists are built lazily, only for cells that contain vertices
    std::vector<int> cellStart((size_t)nx * ny * nz, -1), cellCount((size_t)nx * ny * nz, 0);
    std::vector<u16> items;
    items.reserve(64 * 1024);
    const float halfDiag = cell * 0.8661f;
    for (BVert& v : m.v) {
        u32 mat = v.mat;
        if (mat == MAT_EYE || mat == MAT_EMISSIVE || mat == MAT_CHROME) continue;
        vec3 n = v.n;
        if (!(length2(n) > 0.5f)) continue;
        int x = Clamp((int)((v.p.x - lo.x) / cell), 0, nx - 1), y = Clamp((int)((v.p.y - lo.y) / cell), 0, ny - 1),
            z = Clamp((int)((v.p.z - lo.z) / cell), 0, nz - 1);
        size_t ci = ((size_t)z * ny + y) * nx + x;
        if (cellStart[ci] < 0) {
            cellStart[ci] = (int)items.size();
            vec3 cc = lo + vec3((x + 0.5f) * cell, (y + 0.5f) * cell, (z + 0.5f) * cell);
            for (size_t i = 0; i < sdf.prims.size() && i < 65535; i++) {
                const Prim& q = sdf.prims[i];
                if (length(cc - q.bc) - q.br - halfDiag > reach + cap + q.k) continue;
                items.push_back((u16)i);
            }
            cellCount[ci] = (int)items.size() - cellStart[ci];
        }
        const u16* list = items.data() + cellStart[ci];
        int cnt = cellCount[ci];
        if (cnt == 0) continue;
        float s0 = Max(0.f, sdf.evalList(v.p, list, cnt, cap));
        float occ = 0.f;
        for (int k = 0; k < 4; k++) {
            float d = dk[k] * s;
            float sd = sdf.evalList(v.p + n * d, list, cnt, Min(cap, s0 + d));
            occ += wk[k] * Saturate((s0 + d - sd) / d);
        }
        float ao = 1.f - Saturate(occ * 1.25f - 0.05f);
        float k = mat == MAT_SKIN ? 0.42f : (mat == MAT_HAIR ? 0.3f : 0.35f);
        v.col = v.col * (1.f - k * (1.f - ao));
        // sky occlusion under the jaw: the chin and the jaw keep the sky off the upper neck (and the occiput off the
        // nape), which the samples along the normal miss; march up and out and darken by how soon the head is hit, so
        // the jaw line reads against the neck in flat or overhead light
        if (mat == MAT_SKIN && (v.part == PART_NECK || v.part == PART_HEAD) && n.z > -0.35f && n.z < 0.6f) {
            vec3 dir = normalize(vec3(0, 0, 0.85f) + vec3(n.x, n.y, 0.f) * 0.45f);
            const float ts[4] = {0.01f, 0.022f, 0.038f, 0.058f};
            float hit = 0.f;
            for (int j = 0; j < 4; j++) {
                float t = ts[j] * s;
                if (sdf.evalList(v.p + dir * t, list, cnt, cap) < -0.002f * s) {
                    hit = 1.f - 0.2f * (float)j;
                    break;
                }
            }
            v.col = v.col * (1.f - 0.2f * hit);
        }
    }
}
}  // namespace detail

namespace detail {

// Full-detail build mesh (before uv seam fixing and emission).
static void buildFinalMesh(const CharacterDesc& d, const Skeleton& skel, MeshB& fin) {
    BodyDims D;
    computeDims(d, D);
    BuildCtx c;
    c.d = &d;
    c.D = &D;
    c.sk = &skel;
    c.skin = saturate(d.skinTone);
    float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
    // lips: rosy on fair skin (hemoglobin shows through the thin vermilion), deeper and cooler on dark skin, whose inner
    // lip is lighter and pinker (two-tone lips)
    const float fairL = sstep(0.04f, 0.4f, lum);
    c.lipCol = lerp(mulColor(c.skin, vec3(0.8f, 0.6f, 0.66f)), mulColor(c.skin, vec3(0.84f, 0.52f, 0.56f)), fairL);
    c.lipInner = lerp(vmax(mulColor(c.skin, vec3(1.25f, 0.8f, 0.82f)), vec3(0.16f, 0.06f, 0.06f)), c.lipCol * vec3(1.02f, 0.94f, 0.96f), fairL);
    vec3 palmTarget = vec3(0.52f, 0.33f, 0.24f);
    c.palmCol = lerp(c.skin, vmax(c.skin, palmTarget), 0.7f);
    buildBody(c);
    MeshB extra;
    std::vector<u8> hide(c.m.idx.size() / 3, 0);
    buildOutfit(c, extra, hide);
    compactInto(c.m, hide, fin);
    fin.append(extra);
    applySkinChannels(c, fin);
    bakeOcclusion(c, fin);
}

// Remove what a LOD does not need: from LOD1 every strand card (scalp and beard cards: the shells stay; brows and
// lashes: their colour is painted onto the skin first) and tiny accessory pieces (buttons, rivets); at LOD2 also the
// lid tucks, the mouth interior (the far LOD never talks), the fingers (paddle hands) and small accessories
// (jewellery, glasses, badges, holster items); hats, bags and garments stay. The eyeballs become low-poly spheres with
// the same sclera / iris colours.
static void stripForLod(MeshB& m, const Skeleton& skel, int lod) {
    const u32 NT = (u32)(m.idx.size() / 3);
    std::vector<u8> drop(NT, 0);
    if (lod >= 1) {
        // brows and lashes (strand cards; at LOD2 also the lid tucks) -> skin tint, so the face still reads at a
        // distance; every strand card goes (the hair / beard shells stay, brightened back to the cards' tone)
        std::vector<u32> det;
        for (u32 i = 0; i < (u32)m.v.size(); i++)
            if (m.v[i].part == PART_FACEDETAIL && (lod >= 2 || cardKind(m.v[i]) != CARD_NONE)) det.push_back(i);
        for (BVert& v : m.v) {
            if (v.flags & BuildCtx::F_CARDSHELL) {
                v.col = v.col * (1.f / 0.72f);
                v.flags &= (u8)~BuildCtx::F_CARDSHELL;
            }
            // the fingers' joint creases would smear once their tubes are decimated
            if (v.part == PART_FINGER || v.part == PART_THUMB) v.uv.y = 0.f;
        }
        for (BVert& v : m.v) {
            if (v.part != PART_HEAD || !(v.flags & BuildCtx::F_FACE)) continue;
            float best = 1e9f;
            vec3 col;
            for (u32 j : det) {
                float d2 = length2(m.v[j].p - v.p);
                if (d2 < best) {
                    best = d2;
                    col = m.v[j].col;
                }
            }
            if (best < 0.006f * 0.006f) v.col = lerp(v.col, col, 0.75f * (1.f - sqrtf(best) / 0.006f));
        }
    }
    // accessory components (triangles connected through shared vertices)
    std::vector<u32> parent(m.v.size());
    for (u32 i = 0; i < (u32)m.v.size(); i++) parent[i] = i;
    auto find = [&](u32 x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (u32 t = 0; t < NT; t++) {
        const u32* tr = &m.idx[t * 3];
        if (m.v[tr[0]].part != PART_ACC) continue;
        u32 a = find(tr[0]), b = find(tr[1]), c = find(tr[2]);
        parent[b] = a;
        parent[find(c)] = find(a);
    }
    std::vector<vec3> bmin(m.v.size(), vec3(1e9f)), bmax(m.v.size(), vec3(-1e9f));
    for (u32 i = 0; i < (u32)m.v.size(); i++) {
        if (m.v[i].part != PART_ACC) continue;
        u32 r = find(i);
        bmin[r] = vmin(bmin[r], m.v[i].p);
        bmax[r] = vmax(bmax[r], m.v[i].p);
    }
    const float accMin = lod >= 2 ? 0.18f : 0.035f;
    for (u32 t = 0; t < NT; t++) {
        const BVert& v0 = m.v[m.idx[t * 3]];
        if (v0.part == PART_ACC) {
            u32 r = find(m.idx[t * 3]);
            vec3 e = bmax[r] - bmin[r];
            if (Max(e.x, Max(e.y, e.z)) < accMin) drop[t] = 1;
        }
        if (lod >= 2 && (v0.part == PART_FACEDETAIL || v0.part == PART_MOUTH || v0.part == PART_FINGER || v0.part == PART_THUMB))
            drop[t] = 1;
        if (cardKind(v0) != CARD_NONE) drop[t] = 1;
        if (v0.part == PART_EYE) drop[t] = 1;   // replaced below
        if (v0.mat != MAT_HAIR && (v0.matParam & kParamLodDetail)) drop[t] = 1;   // seams and stitch lines
    }
    MeshB out;
    std::vector<u32> remap(m.v.size(), 0xffffffffu);
    for (u32 t = 0; t < NT; t++) {
        if (drop[t]) continue;
        for (int k = 0; k < 3; k++) {
            u32 a = m.idx[t * 3 + k];
            if (remap[a] == 0xffffffffu) remap[a] = out.add(m.v[a]);
            out.idx.push_back(remap[a]);
        }
    }
    // low-poly eyeballs (same centre, radius and colours; skinned to the eye bones)
    for (int sd = 0; sd < 2; sd++) {
        int eb = sd ? B_EYE_R : B_EYE_L;
        vec3 ctr = -skel.invBindModel[eb].c[3].xyz();
        float r = 0.f;
        int n = 0;
        vec3 iris(0.1f, 0.06f, 0.03f), sclera(0.78f, 0.74f, 0.7f);
        float bestIris = 2.f;
        vec3 fw = normalize(vec3((sd ? 1.f : -1.f) * 0.04f, 1.f, 0.f));
        for (const BVert& v : m.v) {
            if (v.part != PART_EYE || v.side != sd) continue;
            r += length(v.p - ctr);
            n++;
            float c = dot(normalize(v.p - ctr), fw);
            if (fabsf(c - 0.93f) < bestIris) {
                bestIris = fabsf(c - 0.93f);
                iris = v.col;
            }
        }
        if (n == 0) continue;
        r /= n;
        vec3 ex = normalize(cross(fw, vec3(0, 0, 1))), ez = cross(ex, fw);
        const int NS = lod >= 2 ? 6 : 8;
        const float polar[] = {0.f, 20.f, 42.f, 75.f, 110.f};
        u32 base = (u32)out.v.size();
        for (int pi = 0; pi < 5; pi++) {
            float a = polar[pi] * kDegToRad;
            int cnt = pi == 0 ? 1 : NS;
            for (int k = 0; k < cnt; k++) {
                float ph = kTwoPi * k / NS;
                vec3 dir = fw * cosf(a) + (ex * cosf(ph) + ez * sinf(ph)) * sinf(a);
                BVert v;
                v.p = ctr + dir * r;
                v.n = dir;
                v.t = ex * -sinf(ph) + ez * cosf(ph);
                v.col = pi == 0 ? iris * 0.45f : (pi == 1 ? iris : (pi == 2 ? sclera : vec3(0.62f, 0.48f, 0.45f)));
                v.mat = MAT_EYE;
                v.part = PART_EYE;
                v.side = (u8)sd;
                v.sw = skin1(eb);
                out.add(v);
            }
        }
        for (int k = 0; k < NS; k++) out.tri(base, base + 1 + k, base + 1 + (k + 1) % NS);
        for (int pi = 1; pi < 4; pi++)
            for (int k = 0; k < NS; k++) {
                u32 a = base + 1 + (pi - 1) * NS + k, b = base + 1 + (pi - 1) * NS + (k + 1) % NS;
                u32 c = a + NS, d = b + NS;
                out.quad(a, c, d, b);
            }
        // make every triangle face outwards
        for (size_t t = out.idx.size() - (size_t)(NS + 3 * NS * 2) * 3; t < out.idx.size(); t += 3) {
            vec3 p0 = out.v[out.idx[t]].p, p1 = out.v[out.idx[t + 1]].p, p2 = out.v[out.idx[t + 2]].p;
            if (dot(cross(p1 - p0, p2 - p0), (p0 + p1 + p2) * (1.f / 3.f) - ctr) < 0.f) std::swap(out.idx[t + 1], out.idx[t + 2]);
        }
    }
    m = std::move(out);
}

// Layer offsets for the LODs: distance of every non-skin vertex to the nearest skin vertex (hashed grid).
static bool lodIsSkinSurface(const BVert& v) {
    return v.mat == MAT_SKIN && v.part != PART_MOUTH && v.part != PART_FACEDETAIL && v.part != PART_EYE;
}
static void computeLayerOffsets(MeshB& m) {
    const float cell = 0.02f;
    std::unordered_map<u64, std::vector<u32>> grid;
    auto key = [&](int x, int y, int z) { return ((u64)(u32)(x + 4096) << 42) | ((u64)(u32)(y + 4096) << 21) | (u64)(u32)(z + 4096); };
    for (u32 i = 0; i < (u32)m.v.size(); i++) {
        if (!lodIsSkinSurface(m.v[i])) continue;
        vec3 p = m.v[i].p / cell;
        grid[key((int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z))].push_back(i);
    }
    for (BVert& v : m.v) {
        if (lodIsSkinSurface(v) || v.mat == MAT_EYE) {
            v.layer = 0.f;
            continue;
        }
        vec3 p = v.p / cell;
        int cx = (int)floorf(p.x), cy = (int)floorf(p.y), cz = (int)floorf(p.z);
        float best = 0.04f * 0.04f;
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == grid.end()) continue;
                    for (u32 j : it->second) best = Min(best, length2(m.v[j].p - v.p));
                }
        v.layer = sqrtf(best);
    }
}

// Decimation flattens curved shells (chords cut inside the surface), so a tight layer would let what is under it
// poke through: push clothing, hair and accessories out along their normals, more for the outer layers.
static void inflateLayers(MeshB& m, int lod) {
    const float base = lod >= 2 ? 0.004f : 0.0015f, k = lod >= 2 ? 0.5f : 0.15f;
    for (BVert& v : m.v) {
        if (lodIsSkinSurface(v) || v.mat == MAT_EYE || (v.mat == MAT_SKIN && v.part == PART_FACEDETAIL)) continue;   // lid tucks stay on the eye
        vec3 n = length2(v.n) > 1e-12f ? normalize(v.n) : vec3(0);
        v.p = v.p + n * (base + k * Min(v.layer, 0.04f));
    }
}

}  // namespace detail

namespace detail {
// LOD0 budget: a rare heavy combination (a suit, a dense hairstyle and a beard) can pass ~34k triangles; the excess is
// collapsed from the body and clothing where they are flattest (the face, the hands, the hair and all strand cards
// are left alone).
static void governLod0(MeshB& m) {
    const int kBudget = 33600;
    int tris = (int)(m.idx.size() / 3);
    if (tris <= kBudget) return;
    float w[PART_COUNT];
    for (int p = 0; p < PART_COUNT; p++) w[p] = 1e6f;
    w[PART_TORSO] = w[PART_ARM] = w[PART_LEG] = w[PART_NECK] = 1.f;
    w[PART_GARMENT] = w[PART_ACC] = 2.f;
    decimateMesh(m, kBudget, w);
}
}  // namespace detail

void buildCharacterMesh(const CharacterDesc& d, const Skeleton& skel, SkinnedMeshData& out) {
    using namespace detail;
    MeshB fin;
    buildFinalMesh(d, skel, fin);
    governLod0(fin);
    fixUvSeams(fin);
    emitMesh(fin, out);
}

void buildCharacterMeshLods(const CharacterDesc& d, const Skeleton& skel, SkinnedMeshData* out, int lodCount) {
    using namespace detail;
    static const int kLodTris[3] = {0, 4500, 1500};
    // decimation importance per part (faces keep more of the budget; the rebuilt eyeballs are never collapsed)
    float partW[2][PART_COUNT];
    for (int l = 0; l < 2; l++)
        for (int p = 0; p < PART_COUNT; p++) partW[l][p] = 1.f;
    partW[0][PART_HEAD] = 1.6f;
    partW[1][PART_HEAD] = 2.5f;
    partW[0][PART_EYE] = partW[1][PART_EYE] = 1e6f;
    partW[0][PART_FINGER] = partW[0][PART_THUMB] = 0.6f;
    MeshB cur;
    buildFinalMesh(d, skel, cur);
    governLod0(cur);
    if (lodCount > 1) computeLayerOffsets(cur);
    for (int lod = 0; lod < lodCount && lod < 3; lod++) {
        if (lod > 0) {
            stripForLod(cur, skel, lod);
            decimateMesh(cur, kLodTris[lod], partW[lod - 1]);
        }
        MeshB m = cur;
        if (lod > 0) inflateLayers(m, lod);
        fixUvSeams(m);
        emitMesh(m, out[lod]);
    }
}

void buildCharacterMeshLod(const CharacterDesc& d, const Skeleton& skel, int lod, SkinnedMeshData& out) {
    lod = Clamp(lod, 0, 2);
    SkinnedMeshData tmp[3];
    buildCharacterMeshLods(d, skel, tmp, lod + 1);
    out = std::move(tmp[lod]);
}

}  // namespace Anim
